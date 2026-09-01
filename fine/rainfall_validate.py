#!/usr/bin/env python3
"""Validate one Fine source snapshot against a Rainfall JSONL replay."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any, Iterable


class ValidationError(ValueError):
    pass


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise ValidationError(message)


def load_events(lines: Iterable[str]) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    for line_number, line in enumerate(lines, 1):
        if not line.strip():
            continue
        try:
            event = json.loads(line)
        except json.JSONDecodeError as error:
            raise ValidationError(f"line {line_number}: invalid JSON: {error}") from error
        _require(isinstance(event, dict), f"line {line_number}: event is not an object")
        events.append(event)
    _require(bool(events), "rainfall is empty")
    return events


def validate(source: bytes, events: list[dict[str, Any]]) -> dict[str, int]:
    first = events[0]
    envelope = tuple(first.get(key) for key in ("run", "recorder", "manager"))
    _require(all(isinstance(value, str) and value for value in envelope),
             "first event has an incomplete run/recorder/manager envelope")

    documents: set[str] = set()
    snapshots: dict[str, dict[str, Any]] = {}
    source_nodes: dict[str, dict[str, Any]] = {}
    parse_node_ids: set[int] = set()
    terms: dict[str, dict[str, Any]] = {}
    term_handles: set[int] = set()
    event_ids: set[str] = set()
    evidence: list[dict[str, Any]] = []
    terminal_sequence: int | None = None

    for sequence, event in enumerate(events):
        _require(event.get("schema") == "fine.rainfall.v2",
                 f"event {sequence}: unsupported schema")
        _require(event.get("sequence") == sequence,
                 f"event {sequence}: non-contiguous sequence")
        _require(tuple(event.get(key) for key in ("run", "recorder", "manager")) == envelope,
                 f"event {sequence}: cross-run, cross-recorder, or cross-manager envelope")
        event_id = event.get("event_id")
        _require(isinstance(event_id, str) and event_id not in event_ids,
                 f"event {sequence}: missing or reused event ID")
        event_ids.add(event_id)
        _require(terminal_sequence is None,
                 f"event {sequence}: event after terminal run close at {terminal_sequence}")

        operation = event.get("operation")
        data = event.get("data")
        _require(isinstance(operation, str) and isinstance(data, dict),
                 f"event {sequence}: malformed operation or data")

        if operation == "source.document.declare":
            document = data.get("id")
            _require(isinstance(document, str) and document not in documents,
                     f"event {sequence}: missing or reused document ID")
            documents.add(document)
        elif operation == "source.snapshot.declare":
            snapshot = data.get("id")
            identity = data.get("identity")
            _require(isinstance(snapshot, str) and snapshot not in snapshots,
                     f"event {sequence}: missing or reused snapshot ID")
            _require(isinstance(identity, dict),
                     f"event {sequence}: malformed snapshot identity")
            _require(identity.get("document") in documents,
                     f"event {sequence}: snapshot names an unknown document")
            _require(isinstance(identity.get("revision"), int) and identity["revision"] >= 0,
                     f"event {sequence}: invalid snapshot revision")
            _require(identity.get("byte_length") == len(source),
                     f"event {sequence}: source byte length does not match snapshot")
            actual_hash = "sha256:" + hashlib.sha256(source).hexdigest()
            _require(identity.get("exact_source_hash") == actual_hash,
                     f"event {sequence}: source hash does not match snapshot")
            snapshots[snapshot] = identity
        elif operation == "source.node.declare":
            source_id = data.get("id")
            snapshot = data.get("snapshot")
            node_id = data.get("parse_local_node_id")
            span = data.get("span")
            _require(isinstance(source_id, str) and source_id not in source_nodes,
                     f"event {sequence}: missing or reused source node ID")
            _require(snapshot in snapshots,
                     f"event {sequence}: source node names an unknown snapshot")
            _require(isinstance(node_id, int) and node_id >= 0 and
                     node_id not in parse_node_ids,
                     f"event {sequence}: missing or reused parse-local node ID")
            _require(isinstance(span, dict) and isinstance(span.get("begin"), dict) and
                     isinstance(span.get("end"), dict),
                     f"event {sequence}: malformed source span")
            begin = span["begin"].get("offset")
            end = span["end"].get("offset")
            _require(isinstance(begin, int) and isinstance(end, int) and
                     0 <= begin <= end <= len(source),
                     f"event {sequence}: source span is outside the snapshot")
            source_nodes[source_id] = data
            parse_node_ids.add(node_id)
        elif operation == "term.declare":
            term = data.get("id")
            identity = data.get("identity")
            _require(isinstance(term, str) and term not in terms,
                     f"event {sequence}: missing or reused term ID")
            _require(isinstance(identity, dict),
                     f"event {sequence}: malformed term identity")
            _require(tuple(identity.get(key) for key in ("run", "recorder", "manager")) == envelope,
                     f"event {sequence}: term identity crosses its event envelope")
            handle = identity.get("handle")
            _require(isinstance(handle, int) and handle >= 0 and handle not in term_handles,
                     f"event {sequence}: missing or reused live term handle")
            terms[term] = data
            term_handles.add(handle)
        elif operation == "source.term.evidence":
            evidence.append(data)

        if operation in {"check.run.close", "synth.run.close", "bisim.run.close"}:
            terminal_sequence = sequence

    _require(len(documents) == 1, "replay must declare exactly one document")
    _require(len(snapshots) == 1, "replay must declare exactly one snapshot")
    _require(terminal_sequence == len(events) - 1,
             "replay has no terminal run close")

    allowed = {"exact", "desugared", "generated", "internal_z3"}
    for index, edge in enumerate(evidence):
        correspondence = edge.get("correspondence")
        _require(correspondence in allowed,
                 f"source edge {index}: unknown correspondence")
        term = edge.get("term")
        _require(term in terms, f"source edge {index}: unknown term handle")
        source_id = edge.get("source")
        if correspondence == "internal_z3":
            _require(source_id is None,
                     f"source edge {index}: internal_z3 edge carries a source node")
            continue
        _require(source_id in source_nodes,
                 f"source edge {index}: unknown source node")
        _require(edge.get("snapshot") == source_nodes[source_id].get("snapshot"),
                 f"source edge {index}: cross-snapshot source edge")

    return {
        "events": len(events),
        "source_nodes": len(source_nodes),
        "terms": len(terms),
        "source_term_edges": len(evidence),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("rainfall", type=Path)
    arguments = parser.parse_args()
    try:
        with arguments.rainfall.open() as stream:
            result = validate(arguments.source.read_bytes(), load_events(stream))
    except (OSError, ValidationError) as error:
        parser.exit(1, f"fine-rain-validate: {error}\n")
    print("valid rainfall: " + ", ".join(f"{key}={value}" for key, value in result.items()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
