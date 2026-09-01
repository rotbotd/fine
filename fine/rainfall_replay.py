"""Validation and cataloguing for one Fine Rainfall replay."""

from __future__ import annotations

import hashlib
import json
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
    term_lift_validations: set[str] = set()
    event_ids: set[str] = set()
    events_by_id: dict[str, dict[str, Any]] = {}
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
        events_by_id[event_id] = event
        within = event.get("within")
        _require(isinstance(within, list) and
                 all(isinstance(scope, str) and scope for scope in within),
                 f"event {sequence}: malformed scope path")
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
            _require(isinstance(data.get("display_name"), str),
                     f"event {sequence}: malformed document display name")
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
            _require(isinstance(data.get("syntax_kind"), str) and data["syntax_kind"],
                     f"event {sequence}: malformed source syntax kind")
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
            _require(data.get("representation") == "fine.generated-term.v1",
                     f"event {sequence}: term has no canonical Fine rendering")
            _require(isinstance(data.get("origin"), str) and data["origin"],
                     f"event {sequence}: malformed term provenance")
            _require(isinstance(data.get("text"), str) and data["text"],
                     f"event {sequence}: malformed generated Fine term text")
            actual_rendering_hash = ("sha256:" +
                                     hashlib.sha256(data["text"].encode()).hexdigest())
            _require(data.get("rendering_hash") == actual_rendering_hash,
                     f"event {sequence}: generated Fine term hash disagrees with text")
            _require(isinstance(data.get("z3_text_diagnostic"), str),
                     f"event {sequence}: malformed diagnostic Z3 term text")
            _require(data.get("exact_reify_validation") == "pending",
                     f"event {sequence}: term declaration bypasses deferred exact validation")
            sorts = data.get("sort_bindings")
            declarations = data.get("declaration_bindings")
            _require(isinstance(sorts, list) and isinstance(declarations, list),
                     f"event {sequence}: malformed Fine lift bindings")
            sort_names = [item.get("name") for item in sorts
                          if isinstance(item, dict)]
            _require(len(sort_names) == len(sorts) and
                     all(isinstance(name, str) and name for name in sort_names) and
                     len(set(sort_names)) == len(sort_names),
                     f"event {sequence}: missing or reused Fine sort alias")
            _require(all(isinstance(item.get("z3_text"), str) and
                         isinstance(item.get("sort_kind"), int) and
                         isinstance(item.get("ast_id_at_observation"), int)
                         for item in sorts),
                     f"event {sequence}: malformed Fine sort binding")
            declaration_names = [item.get("name") for item in declarations
                                 if isinstance(item, dict)]
            _require(len(declaration_names) == len(declarations) and
                     all(isinstance(name, str) and name for name in declaration_names) and
                     len(set(declaration_names)) == len(declaration_names),
                     f"event {sequence}: missing or reused Fine declaration alias")
            _require(all(isinstance(item.get("z3_symbol"), str) and
                         isinstance(item.get("z3_declaration_text"), str) and
                         isinstance(item.get("decl_kind"), int) and
                         isinstance(item.get("parameters"), list) and
                         all(isinstance(value, str) for value in item["parameters"]) and
                         isinstance(item.get("ast_id_at_observation"), int) and
                         isinstance(item.get("domain"), list) and
                         all(sort in sort_names for sort in item["domain"]) and
                         item.get("range") in sort_names
                         for item in declarations),
                     f"event {sequence}: malformed Fine declaration binding")
            terms[term] = data
            term_handles.add(handle)
        elif operation == "term.lift.validate":
            term = data.get("term")
            _require(term in terms and term not in term_lift_validations,
                     f"event {sequence}: exact lift validates an unknown or repeated term")
            _require(data.get("parse_reify_exact_identity") is True,
                     f"event {sequence}: Fine lift lacks exact same-manager identity")
            _require(data.get("rendering_hash") == terms[term].get("rendering_hash"),
                     f"event {sequence}: exact lift validates a different rendering")
            term_lift_validations.add(term)
        elif operation == "source.term.evidence":
            evidence.append(data)
        elif operation.startswith("z3.clause."):
            proof_hint = data.get("proof_hint")
            literals = data.get("literals")
            dependencies = data.get("dependency_indices")
            _require(proof_hint in terms,
                     f"event {sequence}: clause names an unknown proof hint")
            _require(isinstance(literals, list) and
                     all(literal in terms for literal in literals),
                     f"event {sequence}: clause names an unknown literal")
            _require(data.get("literal_count") == len(literals),
                     f"event {sequence}: clause literal count disagrees")
            _require(isinstance(dependencies, list) and
                     all(isinstance(value, int) and value >= 0 for value in dependencies),
                     f"event {sequence}: malformed clause dependency indices")
            if operation == "z3.clause.infer" and data.get("proof_hint_head") == "inst":
                accepted_id = data.get("quantifier_instance_event")
                accepted = events_by_id.get(accepted_id)
                _require(accepted is not None and accepted is not event and
                         accepted.get("operation") in {
                             "z3.mbqi-instance", "z3.quantifier-instance"
                         },
                         f"event {sequence}: inst clause names no prior accepted instance")
                _require(data.get("quantifier") in terms and
                         data.get("instance") in terms,
                         f"event {sequence}: inst clause names unknown ground terms")
                _require(data.get("quantifier") == accepted["data"].get("quantifier") and
                         data.get("instance") == accepted["data"].get("instance"),
                         f"event {sequence}: inst clause disagrees with accepted instance")
                bindings = data.get("ground_bindings")
                _require(isinstance(bindings, list) and
                         all(binding in terms for binding in bindings),
                         f"event {sequence}: inst clause names unknown bindings")
                _require(data.get("relation") ==
                         "accepted-instance-became-admitted-clause",
                         f"event {sequence}: inst clause has an unknown relation")

        if operation in {"check.run.close", "synth.run.close", "bisim.run.close"}:
            terminal_sequence = sequence

    _require(len(documents) == 1, "replay must declare exactly one document")
    _require(len(snapshots) == 1, "replay must declare exactly one snapshot")
    _require(terminal_sequence == len(events) - 1,
             "replay has no terminal run close")
    _require(term_lift_validations == set(terms),
             "replay does not exact-validate every generated Fine term")

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


def catalog(events: list[dict[str, Any]]) -> dict[str, Any]:
    """Return validated replay objects indexed by their public references."""
    document_event = next(
        event for event in events if event["operation"] == "source.document.declare"
    )
    snapshot_event = next(
        event for event in events if event["operation"] == "source.snapshot.declare"
    )
    return {
        "document": document_event["data"],
        "snapshot": snapshot_event["data"],
        "source_nodes": {
            event["data"]["id"]: event["data"]
            for event in events
            if event["operation"] == "source.node.declare"
        },
        "terms": {
            event["data"]["id"]: event["data"]
            for event in events
            if event["operation"] == "term.declare"
        },
        "evidence": [
            event
            for event in events
            if event["operation"] == "source.term.evidence"
        ],
    }
