#!/usr/bin/env python3
"""Compare bounded proof-state grammars across increasing Rainfall epochs."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


PRODUCTION_FIELDS = (
    "kind", "source", "function", "index_arguments", "coeffects", "result", "arguments",
)


def production_key(production: dict[str, Any]) -> str:
    return json.dumps({field: production[field] for field in PRODUCTION_FIELDS},
                      sort_keys=True, separators=(",", ":"))


def load_grammar(path: Path) -> dict[str, Any]:
    events = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    grammars = [event["data"] for event in events if event.get("operation") == "proof.model.grammar"]
    if len(grammars) != 1:
        raise ValueError(f"{path}: expected one proof.model.grammar event, found {len(grammars)}")
    grammar = grammars[0]
    if grammar.get("candidate_trees_enumerated") is not False:
        raise ValueError(f"{path}: proof grammar was not constructed directly")
    return grammar


def state_alternatives(state: dict[str, Any], grammar: dict[str, Any]) -> set[tuple[str, tuple[str, ...]]]:
    productions = grammar["productions"]
    return {
        (production_key(productions[edge["production"]]), tuple(edge["children"]))
        for edge in state["alternatives"]
    }


def transition_profile(before: dict[str, Any], after: dict[str, Any]) -> dict[str, int]:
    before_productions = {production_key(item) for item in before["productions"]}
    after_productions = {production_key(item) for item in after["productions"]}
    if not before_productions <= after_productions:
        raise ValueError("later proof grammar removed an earlier production")

    before_states = {state["id"]: state for state in before["state_graph"]}
    after_states = {state["id"]: state for state in after["state_graph"]}
    shared = set(before_states) & set(after_states)
    directly_extended: set[str] = set()
    incompatible: set[str] = set()
    for state_id in shared:
        old = state_alternatives(before_states[state_id], before)
        new = state_alternatives(after_states[state_id], after)
        if old == new:
            continue
        if old < new:
            directly_extended.add(state_id)
        else:
            incompatible.add(state_id)
    if incompatible:
        raise ValueError(f"later proof grammar incompatibly changed {len(incompatible)} states")

    # An immutable parent which points to a versioned child also needs a new
    # datatype sort, even when its own transition descriptions did not change.
    affected = set(directly_extended)
    changed = True
    while changed:
        changed = False
        for state_id in shared - affected:
            state = after_states[state_id]
            if any(child in affected
                   for alternative in state["alternatives"]
                   for child in alternative["children"]):
                affected.add(state_id)
                changed = True

    return {
        "from_budget": before["max_cost"],
        "to_budget": after["max_cost"],
        "productions_before": len(before_productions),
        "productions_added": len(after_productions - before_productions),
        "states_before": len(before_states),
        "states_after": len(after_states),
        "shared_states": len(shared),
        "directly_extended_states": len(directly_extended),
        "affected_old_states": len(affected),
        "safely_reusable_states": len(shared - affected),
        "new_states": len(after_states) - len(shared),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("traces", nargs="+", type=Path,
                        help="Rainfall traces ordered by increasing proof budget")
    arguments = parser.parse_args()
    grammars = [load_grammar(path) for path in arguments.traces]
    if len(grammars) < 2:
        parser.error("at least two traces are required")
    budgets = [grammar["max_cost"] for grammar in grammars]
    if budgets != sorted(set(budgets)):
        raise ValueError("proof budgets must be strictly increasing")

    report = {
        "schema": "fine.proof-state-growth-profile.v1",
        "epochs": [
            {
                "budget": grammar["max_cost"],
                "productions": len(grammar["productions"]),
                "states": len(grammar["state_graph"]),
                "transitions": grammar["transitions"],
            }
            for grammar in grammars
        ],
        "growth": [transition_profile(before, after)
                   for before, after in zip(grammars, grammars[1:])],
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
