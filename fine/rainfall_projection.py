#!/usr/bin/env python3
"""Project validated Rainfall evidence through one source transaction."""

from __future__ import annotations

import argparse
import hashlib
import html
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from rainfall_replay import ValidationError, catalog, load_events, validate


class ProjectionError(ValueError):
    pass


@dataclass(frozen=True)
class Edit:
    begin: int
    end: int
    insert: bytes
    insert_text: str


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise ProjectionError(message)


def load_edits(value: Any, source_length: int) -> list[Edit]:
    if isinstance(value, dict):
        _require(set(value) == {"edits"}, "transaction object must contain only `edits`")
        value = value["edits"]
    _require(isinstance(value, list), "transaction must be an edit array")
    result: list[Edit] = []
    previous_begin = -1
    previous_end = 0
    for index, item in enumerate(value):
        _require(isinstance(item, dict), f"edit {index}: expected an object")
        _require(set(item) == {"from", "to", "insert"},
                 f"edit {index}: expected exactly from/to/insert")
        begin, end, insert = item["from"], item["to"], item["insert"]
        _require(isinstance(begin, int) and not isinstance(begin, bool),
                 f"edit {index}: from is not an integer byte offset")
        _require(isinstance(end, int) and not isinstance(end, bool),
                 f"edit {index}: to is not an integer byte offset")
        _require(isinstance(insert, str), f"edit {index}: insert is not UTF-8 text")
        _require(0 <= begin <= end <= source_length,
                 f"edit {index}: range is outside the source")
        _require(begin >= previous_end,
                 f"edit {index}: edits overlap or are not in source order")
        _require(not (begin == end == previous_begin == previous_end),
                 f"edit {index}: two insertions at one offset are ambiguous")
        result.append(Edit(begin, end, insert.encode("utf-8"), insert))
        previous_begin, previous_end = begin, end
    return result


def apply_edits(source: bytes, edits: list[Edit]) -> bytes:
    pieces: list[bytes] = []
    cursor = 0
    for edit in edits:
        pieces.extend((source[cursor:edit.begin], edit.insert))
        cursor = edit.end
    pieces.append(source[cursor:])
    return b"".join(pieces)


def transport_range(begin: int, end: int, edits: list[Edit]) -> tuple[int, int] | None:
    """Map a half-open byte range; a wholly deleted range becomes unplaced."""
    mapped_begin, mapped_end = begin, end
    accumulated_delta = 0
    for edit in edits:
        edit_begin = edit.begin + accumulated_delta
        edit_end = edit.end + accumulated_delta
        delta = len(edit.insert) - (edit.end - edit.begin)
        if edit_end <= mapped_begin:
            mapped_begin += delta
            mapped_end += delta
        elif edit_begin >= mapped_end:
            pass
        else:
            if mapped_begin >= edit_begin:
                mapped_begin = edit_begin
            if mapped_end <= edit_end:
                mapped_end = edit_begin + len(edit.insert)
            else:
                mapped_end += delta
        accumulated_delta += delta
    if mapped_begin >= mapped_end:
        return None
    return mapped_begin, mapped_end


def source_position(source: bytes, offset: int) -> dict[str, int]:
    line = source.count(b"\n", 0, offset) + 1
    previous_newline = source.rfind(b"\n", 0, offset)
    return {"offset": offset, "line": line, "column": offset - previous_newline}


def source_span(source: bytes, begin: int, end: int) -> dict[str, dict[str, int]]:
    return {"begin": source_position(source, begin), "end": source_position(source, end)}


def project(source: bytes, events: list[dict[str, Any]], edits: list[Edit]) -> tuple[dict[str, Any], bytes]:
    validate(source, events)
    objects = catalog(events)
    displayed_source = apply_edits(source, edits)
    snapshot = objects["snapshot"]
    claim_identity = snapshot["identity"]
    current = not edits
    display_identity = {
        "document": claim_identity["document"],
        "revision": claim_identity["revision"] + (0 if current else 1),
        "exact_source_hash": "sha256:" + hashlib.sha256(displayed_source).hexdigest(),
        "byte_length": len(displayed_source),
    }
    bisimulation_assertions = {
        event["data"]["assertion"]: event
        for event in events
        if event["operation"] == "bisim.clause.assert"
    }
    admitted_lemmas = {
        event["data"]["quantifier_instance_event"]: event
        for event in events
        if event["operation"] == "z3.clause.infer"
        and event["data"].get("proof_hint_head") == "inst"
    }
    accepted_by_role: dict[str, list[dict[str, Any]]] = {}
    for event in events:
        if event["operation"] not in {"z3.mbqi-instance", "z3.quantifier-instance"}:
            continue
        role = event["data"].get("source_role")
        if isinstance(role, str):
            accepted_by_role.setdefault(role, []).append(event)

    def bisimulation_activity(term_reference: str) -> dict[str, Any] | None:
        assertion = bisimulation_assertions.get(term_reference)
        if assertion is None:
            return None
        role = assertion["data"]["role"]
        accepted = accepted_by_role.get("fine.bisim." + role, [])
        instances = []
        for event in accepted:
            lemma = admitted_lemmas.get(event["event_id"])
            bindings = [
                {"term": reference, "text": objects["terms"][reference]["text"]}
                for reference in (lemma["data"]["ground_bindings"] if lemma else [])
            ]
            instance_reference = event["data"]["instance"]
            instances.append({
                "accepted_event": event["event_id"],
                "instance": instance_reference,
                "instance_text": objects["terms"][instance_reference]["text"],
                "admitted_clause_event": lemma["event_id"] if lemma else None,
                "ground_bindings": bindings,
            })
        return {
            "kind": "bisimulation-clause-activity",
            "role": role,
            "assertion_event": assertion["event_id"],
            "accepted_instances": instances,
        }

    annotations: list[dict[str, Any]] = []
    for event in objects["evidence"]:
        edge = event["data"]
        node = objects["source_nodes"][edge["source"]]
        term = objects["terms"][edge["term"]]
        old_begin = node["span"]["begin"]["offset"]
        old_end = node["span"]["end"]["offset"]
        mapped = (old_begin, old_end) if current else transport_range(old_begin, old_end, edits)
        status = "current" if current else ("transported" if mapped else "unplaced")
        annotations.append({
            "status": status,
            "claim": {
                "snapshot": snapshot["id"],
                "source": edge["source"],
                "term": edge["term"],
                "correspondence": edge["correspondence"],
                "event_id": event["event_id"],
                "within": event["within"],
            },
            "syntax_kind": node["syntax_kind"],
            "claim_span": node["span"],
            "display_span": source_span(displayed_source, *mapped) if mapped else None,
            "term_text": term["text"],
            "activity": bisimulation_activity(edge["term"]),
        })
    result = {
        "schema": "fine.rainfall.projection.v1",
        "document": objects["document"],
        "claim_snapshot": snapshot,
        "display_snapshot": {
            "admitted_by_rainfall": current,
            **({"id": snapshot["id"]} if current else {}),
            "identity": display_identity,
        },
        "transaction": [
            {"from": edit.begin, "to": edit.end, "insert": edit.insert_text,
             "insert_byte_length": len(edit.insert)}
            for edit in edits
        ],
        "annotations": annotations,
    }
    return result, displayed_source


def render_html(projection: dict[str, Any], displayed_source: bytes) -> str:
    annotations = projection["annotations"]
    state = ("current" if projection["display_snapshot"]["admitted_by_rainfall"]
             else "transported")
    if state == "current":
        warning = "current evidence — this trace was admitted for the displayed snapshot"
    else:
        warning = "stale evidence — these claims belong to the previous revision and do not describe this one"
    grouped: dict[str, list[dict[str, Any]]] = {}
    for item in annotations:
        grouped.setdefault(item["claim"]["source"], []).append(item)

    def render_activity(activity: dict[str, Any]) -> str:
        instances = activity["accepted_instances"]
        admitted = sum(instance["admitted_clause_event"] is not None
                       for instance in instances)
        details = []
        for instance in instances:
            binding_text = ", ".join(
                binding["text"] for binding in instance["ground_bindings"]
            ) or "no binding terms"
            admission = (f'admitted as {instance["admitted_clause_event"]}'
                         if instance["admitted_clause_event"] else
                         "accepted; no admitted lemma was observed")
            details.append(
                f'<details><summary>{html.escape(instance["accepted_event"])} — '
                f'{html.escape(admission)}</summary>'
                f'<div>bindings: <code>{html.escape(binding_text)}</code></div>'
                f'<pre>{html.escape(instance["instance_text"])}</pre></details>'
            )
        noun = "instance" if len(instances) == 1 else "instances"
        return (
            f'<section><strong>{html.escape(activity["role"])}</strong><br>'
            f'{len(instances)} accepted {noun}, {admitted} admitted lemmas'
            + "".join(details) + "</section>"
        )

    rows: list[str] = []
    for group in grouped.values():
        item = group[0]
        span = item["display_span"]
        if span is None:
            location = "unplaced"
            excerpt = ""
        else:
            begin, end = span["begin"]["offset"], span["end"]["offset"]
            location = f"{span['begin']['line']}:{span['begin']['column']}–{span['end']['line']}:{span['end']['column']}"
            excerpt = displayed_source[begin:end].decode("utf-8", errors="replace")
        activities = [entry["activity"] for entry in group
                      if entry["activity"] is not None]
        activity_html = "".join(render_activity(activity) for activity in activities) or "—"
        correspondences = ", ".join(dict.fromkeys(
            entry["claim"]["correspondence"] for entry in group
        ))
        if len(group) == 1:
            terms_html = f'<code>{html.escape(item["term_text"])}</code>'
        else:
            term_details = []
            for entry in group:
                label = (entry["activity"]["role"] if entry["activity"]
                         else entry["claim"]["term"])
                term_details.append(
                    f'<details><summary>{html.escape(label)}</summary>'
                    f'<pre>{html.escape(entry["term_text"])}</pre></details>'
                )
            terms_html = "".join(term_details)
        rows.append(
            f'<tr class="{item["status"]}" data-state="{item["status"]}">'
            f'<td>{html.escape(item["status"])}</td>'
            f'<td>{html.escape(location)}</td>'
            f'<td><code>{html.escape(excerpt)}</code></td>'
            f'<td>{html.escape(correspondences)}</td>'
            f'<td>{terms_html}</td>'
            f'<td>{activity_html}</td></tr>'
        )
    title = projection["document"].get("display_name", "Fine Rainfall")
    return f"""<!doctype html>
<html lang="en"><meta charset="utf-8"><title>{html.escape(title)} — Rainfall</title>
<style>
body {{ margin: 2rem; color: #dedede; background: #181818; font: 14px ui-monospace, monospace }}
h1 {{ font-size: 1rem }} .banner {{ padding: .8rem; border: 1px solid #6f6f6f; margin: 1rem 0 }}
.banner.transported {{ color: #ffd486; border-style: dashed; border-color: #a87520 }}
table {{ border-collapse: collapse; width: 100% }} th, td {{ padding: .5rem; border-bottom: 1px solid #393939; text-align: left }}
tr.transported td:first-child {{ color: #ffd486 }} tr.unplaced td:first-child {{ color: #ff8585 }}
tr.current td:first-child {{ color: #8fe1a2 }} code {{ white-space: pre-wrap }}
td {{ vertical-align: top }} section + section {{ margin-top: 1rem }}
details {{ margin-top: .5rem }} pre {{ max-height: 18rem; overflow: auto; white-space: pre-wrap }}
</style><body><h1>{html.escape(title)}</h1><div class="banner {state}">{html.escape(warning)}</div>
<table><thead><tr><th>state</th><th>display range</th><th>display text</th><th>edge</th><th>claimed term</th><th>solver activity</th></tr></thead>
<tbody>{''.join(rows)}</tbody></table></body></html>"""


def main() -> int:
    parser = argparse.ArgumentParser(
        description="project validator-admitted Rainfall evidence through one byte-offset transaction"
    )
    parser.add_argument("source", type=Path, help="exact source named by the Rainfall snapshot")
    parser.add_argument("rainfall", type=Path, help="Rainfall JSONL for that source")
    parser.add_argument("--edits", type=Path,
                        help="JSON array of non-overlapping {from,to,insert} byte edits")
    parser.add_argument("--html", type=Path, help="write a standalone visible projection")
    parser.add_argument("--write-source", type=Path, help="write the source obtained by applying the transaction")
    arguments = parser.parse_args()
    try:
        source = arguments.source.read_bytes()
        with arguments.rainfall.open() as stream:
            events = load_events(stream)
        edit_value = json.loads(arguments.edits.read_text()) if arguments.edits else []
        edits = load_edits(edit_value, len(source))
        projection, displayed_source = project(source, events, edits)
        if arguments.html:
            arguments.html.write_text(render_html(projection, displayed_source))
        if arguments.write_source:
            arguments.write_source.write_bytes(displayed_source)
    except (OSError, UnicodeError, json.JSONDecodeError, ValidationError, ProjectionError) as error:
        parser.exit(1, f"fine-rain-project: {error}\n")
    print(json.dumps(projection, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
