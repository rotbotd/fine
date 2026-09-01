"""Editor-neutral atomic host transaction for live Rainfall generations."""

from __future__ import annotations

import argparse
import fcntl
import json
import os
import secrets
import subprocess
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Iterator

from rainfall_generation import (GenerationError, admit, load_request,
                                 make_request, source_identity)
from rainfall_projection import (Edit, ProjectionError, apply_edits, load_edits,
                                 source_span, transport_range)
from rainfall_replay import ValidationError, load_events, validate


class HostError(ValueError):
    pass


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise HostError(message)


@contextmanager
def locked(host: Path, create: bool = False) -> Iterator[None]:
    if create:
        host.mkdir(parents=True, exist_ok=True)
    _require(host.is_dir(), f"host directory does not exist: {host}")
    lock_path = host / ".lock"
    with lock_path.open("a+b") as stream:
        fcntl.flock(stream.fileno(), fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(stream.fileno(), fcntl.LOCK_UN)


def _atomic_write(path: Path, data: bytes) -> None:
    temporary = path.with_name(f".{path.name}.{secrets.token_hex(8)}.tmp")
    try:
        with temporary.open("xb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        descriptor = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
    finally:
        if temporary.exists():
            temporary.unlink()


def _write_immutable(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    descriptor = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def _state_path(host: Path) -> Path:
    return host / "state.json"


def load_state(host: Path) -> dict[str, Any]:
    try:
        state = json.loads(_state_path(host).read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise HostError(f"cannot read host state: {error}") from error
    _require(isinstance(state, dict) and state.get("schema") == "fine.rainfall.host-state.v1",
             "unsupported host state")
    _require(isinstance(state.get("display_source"), str), "host display source is malformed")
    document = state.get("document")
    display = state.get("display_snapshot")
    _require(isinstance(document, dict) and isinstance(document.get("id"), str) and
             isinstance(document.get("display_name"), str),
             "host document is malformed")
    _require(isinstance(display, dict), "host display snapshot is malformed")
    _require(isinstance(display.get("revision"), int) and
             not isinstance(display["revision"], bool) and display["revision"] >= 0,
             "host display revision is malformed")
    source = state["display_source"].encode("utf-8")
    expected = source_identity(source, document["id"], display.get("revision"))
    _require(display == expected, "host display bytes disagree with their identity")
    generations = state.get("generations")
    current = state.get("current_generation")
    _require(isinstance(generations, dict) and current in generations,
             "host current generation is missing")
    for generation, record in generations.items():
        _require(isinstance(generation, str) and isinstance(record, dict),
                 "host generation record is malformed")
        request = load_request(record.get("request"))
        _require(request["generation"] == generation,
                 "host generation key disagrees with its request")
        _require(record.get("status") in {"requested", "superseded", "admitted", "discarded", "failed"},
                 "host generation status is malformed")
        for field in ("source_file", "request_file"):
            value = record.get(field)
            _require(isinstance(value, str) and value,
                     f"host {field} is missing")
            path = Path(value)
            _require(bool(str(path)) and not path.is_absolute() and ".." not in path.parts,
                     f"host {field} is not a retained relative path")
    current_request = generations[current]["request"]
    _require(current_request["display_snapshot"] == display,
             "host current request disagrees with the display snapshot")
    annotations = state.get("annotations")
    _require(isinstance(annotations, list), "host annotations are malformed")
    for annotation in annotations:
        _require(isinstance(annotation, dict) and
                 annotation.get("status") in {"current", "transported", "unplaced"},
                 "host annotation state is malformed")
        span = annotation.get("display_span")
        if annotation["status"] == "unplaced":
            _require(span is None, "unplaced host annotation retains a display span")
        else:
            _require(isinstance(span, dict) and isinstance(span.get("begin"), dict) and
                     isinstance(span.get("end"), dict), "placed host annotation has no span")
            begin, end = span["begin"].get("offset"), span["end"].get("offset")
            _require(isinstance(begin, int) and isinstance(end, int) and
                     0 <= begin < end <= len(source), "host annotation span is outside the display")
    return state


def save_state(host: Path, state: dict[str, Any]) -> None:
    _atomic_write(_state_path(host),
                  (json.dumps(state, separators=(",", ":")) + "\n").encode("utf-8"))


def _issue_generation(host: Path, state: dict[str, Any], source: bytes) -> dict[str, Any]:
    revision = state["display_snapshot"]["revision"]
    token = secrets.token_hex(12)
    generation = f"generation:{revision}:{token}"
    request = make_request(source, state["document"]["id"], revision, generation)
    source_file = Path("sources") / f"{revision}-{token}.fine"
    request_file = Path("requests") / f"{revision}-{token}.json"
    _write_immutable(host / source_file, source)
    _write_immutable(host / request_file,
                     (json.dumps(request, separators=(",", ":")) + "\n").encode())
    record = {
        "status": "requested",
        "request": request,
        "source_file": str(source_file),
        "request_file": str(request_file),
    }
    state["generations"][generation] = record
    state["current_generation"] = generation
    return record


def _launch_description(host: Path, state: dict[str, Any]) -> dict[str, Any]:
    generation = state["current_generation"]
    record = state["generations"][generation]
    return {
        "schema": "fine.rainfall.host-action.v1",
        "status": "requested",
        "generation": generation,
        "display_snapshot": state["display_snapshot"],
        "source": str((host / record["source_file"]).resolve()),
        "fine_arguments": record["request"]["rain_arguments"] +
                          [str((host / record["source_file"]).resolve())],
        "annotation_states": _annotation_counts(state["annotations"]),
    }


def _annotation_counts(annotations: list[dict[str, Any]]) -> dict[str, int]:
    result = {"current": 0, "transported": 0, "unplaced": 0}
    for annotation in annotations:
        result[annotation["status"]] += 1
    return result


def initialize(host: Path, source: bytes, display_name: str, document: str | None) -> dict[str, Any]:
    try:
        source_text = source.decode("utf-8")
    except UnicodeDecodeError as error:
        raise HostError("Fine host source is not UTF-8") from error
    with locked(host, create=True):
        _require(not _state_path(host).exists(), "host is already initialized")
        document_id = document or f"document:{secrets.token_hex(16)}"
        _require(bool(document_id), "document ID is empty")
        state = {
            "schema": "fine.rainfall.host-state.v1",
            "document": {"id": document_id, "display_name": display_name},
            "display_snapshot": source_identity(source, document_id, 0),
            "display_source": source_text,
            "current_generation": "",
            "generations": {},
            "annotations": [],
        }
        _issue_generation(host, state, source)
        save_state(host, state)
        return _launch_description(host, state)


def _transport_annotations(annotations: list[dict[str, Any]], displayed_source: bytes,
                           edits: list[Edit]) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for old in annotations:
        annotation = json.loads(json.dumps(old))
        span = annotation.get("display_span")
        if span is None:
            annotation["status"] = "unplaced"
            result.append(annotation)
            continue
        begin = span["begin"]["offset"]
        end = span["end"]["offset"]
        mapped = transport_range(begin, end, edits)
        annotation["status"] = "transported" if mapped else "unplaced"
        annotation["display_span"] = source_span(displayed_source, *mapped) if mapped else None
        result.append(annotation)
    return result


def _advance_locked(host: Path, state: dict[str, Any], edits: list[Edit]) -> dict[str, Any]:
    old_source = state["display_source"].encode("utf-8")
    _require(bool(edits), "an empty transaction does not advance a revision")
    displayed_source = apply_edits(old_source, edits)
    try:
        displayed_text = displayed_source.decode("utf-8")
    except UnicodeDecodeError as error:
        raise HostError("transaction produced non-UTF-8 Fine source") from error
    previous = state["generations"][state["current_generation"]]
    if previous["status"] == "requested":
        previous["status"] = "superseded"
    state["display_snapshot"] = source_identity(
        displayed_source, state["document"]["id"], state["display_snapshot"]["revision"] + 1)
    state["display_source"] = displayed_text
    state["annotations"] = _transport_annotations(state["annotations"], displayed_source, edits)
    _issue_generation(host, state, displayed_source)
    save_state(host, state)
    return _launch_description(host, state)


def advance(host: Path, edit_value: Any) -> dict[str, Any]:
    with locked(host):
        state = load_state(host)
        edits = load_edits(edit_value, len(state["display_source"].encode("utf-8")))
        return _advance_locked(host, state, edits)


def materialize(host: Path) -> dict[str, Any]:
    """Apply the current admitted match witness as one host-owned transaction."""
    with locked(host):
        state = load_state(host)
        generation = state["current_generation"]
        record = state["generations"][generation]
        _require(record["status"] == "admitted",
                 "only the current admitted generation can be materialized")
        trace_file = record.get("trace_file")
        _require(isinstance(trace_file, str) and trace_file,
                 "admitted generation has no retained trace")
        source = state["display_source"].encode("utf-8")
        events = load_events((host / trace_file).read_text().splitlines())
        validate(source, events)
        witnesses = [event for event in events
                     if event.get("operation") == "fine.match-witness"]
        _require(len(witnesses) == 1, "current trace has no unique match witness")
        replacements = witnesses[0]["data"].get("replacements")
        _require(isinstance(replacements, list) and replacements,
                 "current match witness has no open arms to materialize")
        for replacement in replacements:
            begin, end = replacement["from"], replacement["to"]
            _require(source[begin:end].startswith(b"?"),
                     "current source no longer contains the admitted hole text")
        edit_values = [
            {key: replacement[key] for key in ("from", "to", "insert")}
            for replacement in replacements
        ]
        edits = load_edits(edit_values, len(source))
        result = _advance_locked(host, state, edits)
        result["materialized_from"] = generation
        result["replacements"] = len(replacements)
        return result


def _record_completion(host: Path, state: dict[str, Any], generation: str,
                       admission: dict[str, Any], trace: bytes) -> None:
    record = state["generations"][generation]
    token = generation.rsplit(":", 1)[-1]
    trace_file = Path("traces") / f"{token}.jsonl"
    trace_path = host / trace_file
    if trace_path.exists():
        _require(trace_path.read_bytes() == trace,
                 "retained completion trace disagrees with an orphaned trace")
    else:
        _write_immutable(trace_path, trace)
    record["status"] = admission["status"]
    record["admission"] = admission
    record["trace_file"] = str(trace_file)
    if admission["status"] == "admitted":
        state["annotations"] = admission["projection"]["annotations"]


def complete(host: Path, generation: str, trace: bytes) -> dict[str, Any]:
    with locked(host):
        initial = load_state(host)
        _require(generation in initial["generations"], "completion names an unknown generation")
        record = initial["generations"][generation]
        _require(record["status"] in {"requested", "superseded"},
                 f"generation is already {record['status']}")
        candidate_source = (host / record["source_file"]).read_bytes()
    events = load_events(trace.decode("utf-8").splitlines())
    validate(candidate_source, events)
    with locked(host):
        state = load_state(host)
        record = state["generations"][generation]
        _require(record["status"] in {"requested", "superseded"},
                 f"generation is already {record['status']}")
        current = state["generations"][state["current_generation"]]["request"]
        admission = admit(current, state["display_source"].encode("utf-8"),
                          candidate_source, events)
        _record_completion(host, state, generation, admission, trace)
        save_state(host, state)
        return admission


def mark_failed(host: Path, generation: str, error: str) -> dict[str, Any]:
    with locked(host):
        state = load_state(host)
        _require(generation in state["generations"], "failure names an unknown generation")
        record = state["generations"][generation]
        _require(record["status"] in {"requested", "superseded"},
                 f"generation is already {record['status']}")
        result = {
            "schema": "fine.rainfall.host-completion.v1",
            "status": "failed",
            "generation": generation,
            "error": error,
        }
        record["status"] = "failed"
        record["failure"] = result
        save_state(host, state)
        return result


def run_generation(host: Path, fine: Path, generation: str | None) -> dict[str, Any]:
    with locked(host):
        state = load_state(host)
        selected = generation or state["current_generation"]
        _require(selected in state["generations"], "run names an unknown generation")
        record = state["generations"][selected]
        _require(record["status"] in {"requested", "superseded"},
                 f"generation is already {record['status']}")
        arguments = [str(fine)] + record["request"]["rain_arguments"] + [str(host / record["source_file"])]
    process = subprocess.run(arguments, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if process.returncode != 0:
        message = process.stderr.decode("utf-8", errors="replace").strip()
        return mark_failed(host, selected, message or f"fine exited {process.returncode}")
    return complete(host, selected, process.stdout)


def main() -> int:
    parser = argparse.ArgumentParser(description="atomic editor-neutral host for live Rainfall")
    commands = parser.add_subparsers(dest="command", required=True)
    init_parser = commands.add_parser("init")
    init_parser.add_argument("host", type=Path)
    init_parser.add_argument("source", type=Path)
    init_parser.add_argument("--document")
    init_parser.add_argument("--display-name")
    advance_parser = commands.add_parser("advance")
    advance_parser.add_argument("host", type=Path)
    advance_parser.add_argument("edits", type=Path)
    run_parser = commands.add_parser("run")
    run_parser.add_argument("host", type=Path)
    run_parser.add_argument("--fine", type=Path, required=True)
    run_parser.add_argument("--generation")
    complete_parser = commands.add_parser("complete")
    complete_parser.add_argument("host", type=Path)
    complete_parser.add_argument("generation")
    complete_parser.add_argument("rainfall", type=Path)
    materialize_parser = commands.add_parser("materialize")
    materialize_parser.add_argument("host", type=Path)
    state_parser = commands.add_parser("state")
    state_parser.add_argument("host", type=Path)
    arguments = parser.parse_args()
    try:
        if arguments.command == "init":
            result = initialize(arguments.host, arguments.source.read_bytes(),
                                arguments.display_name or str(arguments.source), arguments.document)
        elif arguments.command == "advance":
            result = advance(arguments.host, json.loads(arguments.edits.read_text()))
        elif arguments.command == "run":
            result = run_generation(arguments.host, arguments.fine, arguments.generation)
        elif arguments.command == "complete":
            result = complete(arguments.host, arguments.generation, arguments.rainfall.read_bytes())
        elif arguments.command == "materialize":
            result = materialize(arguments.host)
        else:
            with locked(arguments.host):
                result = load_state(arguments.host)
    except (OSError, UnicodeError, json.JSONDecodeError, subprocess.SubprocessError,
            ValidationError, ProjectionError, GenerationError, HostError) as error:
        parser.exit(1, f"fine-rain-host: {error}\n")
    print(json.dumps(result, separators=(",", ":")))
    return 0
