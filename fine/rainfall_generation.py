"""Pure generation requests and completion admission for live Rainfall."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

from rainfall_projection import project
from rainfall_replay import ValidationError, catalog, load_events, validate


class GenerationError(ValueError):
    pass


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise GenerationError(message)


def source_identity(source: bytes, document: str, revision: int) -> dict[str, Any]:
    return {
        "document": document,
        "revision": revision,
        "exact_source_hash": "sha256:" + hashlib.sha256(source).hexdigest(),
        "byte_length": len(source),
    }


def make_request(source: bytes, document: str, revision: int, generation: str) -> dict[str, Any]:
    _require(bool(document), "document ID is empty")
    _require(isinstance(revision, int) and not isinstance(revision, bool) and revision >= 0,
             "revision is not a nonnegative integer")
    _require(bool(generation), "generation ID is empty")
    return {
        "schema": "fine.rainfall.generation-request.v1",
        "generation": generation,
        "display_snapshot": source_identity(source, document, revision),
        "rain_arguments": [
            "rain", "--document", document, "--revision", str(revision),
            "--generation", generation,
        ],
    }


def load_request(value: Any) -> dict[str, Any]:
    _require(isinstance(value, dict), "request is not an object")
    _require(set(value) == {"schema", "generation", "display_snapshot", "rain_arguments"},
             "request has unknown or missing fields")
    _require(value["schema"] == "fine.rainfall.generation-request.v1",
             "unsupported request schema")
    generation = value["generation"]
    identity = value["display_snapshot"]
    _require(isinstance(generation, str) and generation, "request generation is empty")
    _require(isinstance(identity, dict) and
             set(identity) == {"document", "revision", "exact_source_hash", "byte_length"},
             "request snapshot identity is malformed")
    _require(isinstance(identity["document"], str) and identity["document"],
             "request document ID is empty")
    _require(isinstance(identity["revision"], int) and not isinstance(identity["revision"], bool) and
             identity["revision"] >= 0, "request revision is invalid")
    digest = identity["exact_source_hash"]
    _require(isinstance(digest, str) and digest.startswith("sha256:") and
             len(digest) == 71 and all(character in "0123456789abcdef" for character in digest[7:]),
             "request source hash is malformed")
    _require(isinstance(identity["byte_length"], int) and
             not isinstance(identity["byte_length"], bool) and identity["byte_length"] >= 0,
             "request byte length is invalid")
    expected_arguments = [
        "rain", "--document", identity["document"], "--revision",
        str(identity["revision"]), "--generation", generation,
    ]
    _require(value["rain_arguments"] == expected_arguments,
             "request rain arguments disagree with its identity")
    return value


def _discard(reason: str, request: dict[str, Any], candidate: dict[str, Any]) -> dict[str, Any]:
    return {
        "schema": "fine.rainfall.admission.v1",
        "status": "discarded",
        "reason": reason,
        "requested_generation": request["generation"],
        "candidate_run": candidate["run"],
        "requested_snapshot": request["display_snapshot"],
        "candidate_snapshot": candidate["snapshot"],
    }


def admit(request_value: Any, display_source: bytes, candidate_source: bytes,
          events: list[dict[str, Any]]) -> dict[str, Any]:
    request = load_request(request_value)
    validate(candidate_source, events)
    objects = catalog(events)
    requested = request["display_snapshot"]
    candidate = {
        "run": events[0]["run"],
        "snapshot": objects["snapshot"]["identity"],
    }
    actual_display = source_identity(display_source, requested["document"], requested["revision"])
    if actual_display != requested:
        return _discard("display-advanced-after-request", request, candidate)
    if candidate["run"] != request["generation"]:
        return _discard("late-or-unrequested-generation", request, candidate)
    if candidate["snapshot"]["document"] != requested["document"]:
        return _discard("different-document", request, candidate)
    if candidate["snapshot"]["revision"] != requested["revision"]:
        return _discard("different-revision", request, candidate)
    if (candidate["snapshot"]["exact_source_hash"] != requested["exact_source_hash"] or
            candidate["snapshot"]["byte_length"] != requested["byte_length"]):
        return _discard("different-source", request, candidate)
    projection, _ = project(candidate_source, events, [])
    return {
        "schema": "fine.rainfall.admission.v1",
        "status": "admitted",
        "generation": request["generation"],
        "snapshot": objects["snapshot"],
        "projection": projection,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="issue and admit exact Rainfall generations")
    commands = parser.add_subparsers(dest="command", required=True)
    request_parser = commands.add_parser("request")
    request_parser.add_argument("source", type=Path)
    request_parser.add_argument("--document", required=True)
    request_parser.add_argument("--revision", type=int, required=True)
    request_parser.add_argument("--generation", required=True)
    admit_parser = commands.add_parser("admit")
    admit_parser.add_argument("request", type=Path)
    admit_parser.add_argument("display_source", type=Path)
    admit_parser.add_argument("candidate_source", type=Path)
    admit_parser.add_argument("rainfall", type=Path)
    arguments = parser.parse_args()
    try:
        if arguments.command == "request":
            result = make_request(arguments.source.read_bytes(), arguments.document,
                                  arguments.revision, arguments.generation)
        else:
            request = json.loads(arguments.request.read_text())
            with arguments.rainfall.open() as stream:
                events = load_events(stream)
            result = admit(request, arguments.display_source.read_bytes(),
                           arguments.candidate_source.read_bytes(), events)
    except (OSError, json.JSONDecodeError, ValidationError, GenerationError) as error:
        parser.exit(1, f"fine-rain-generation: {error}\n")
    print(json.dumps(result, separators=(",", ":")))
    return 0
