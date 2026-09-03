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
    holes: dict[str, dict[str, Any]] = {}
    arm_witnesses: dict[str, dict[str, Any]] = {}
    proof_holes: dict[str, dict[str, Any]] = {}
    proof_candidates: dict[str, dict[str, Any]] = {}
    proof_candidates_by_hole: dict[str, list[str]] = {}
    proof_selections: dict[str, dict[str, Any]] = {}
    proof_holes_closed: set[str] = set()
    proof_functions: dict[str, dict[str, Any]] = {}
    induction_hypothesis_uses: list[dict[str, Any]] = []
    proof_match_branches: dict[tuple[str, ...], list[dict[str, Any]]] = {}
    proof_matches: set[tuple[str, ...]] = set()
    proof_model_grammars: dict[str, dict[str, Any]] = {}
    proof_model_solves: dict[str, dict[str, Any]] = {}
    proof_model_lifts: dict[str, dict[str, Any]] = {}
    term_handles: set[int] = set()
    term_lift_validations: set[str] = set()
    event_ids: set[str] = set()
    events_by_id: dict[str, dict[str, Any]] = {}
    evidence: list[dict[str, Any]] = []
    terminal_sequence: int | None = None
    match_run = False
    match_witness_count = 0

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
                 f"event {sequence}: event after terminal close at {terminal_sequence}")

        operation = event.get("operation")
        data = event.get("data")
        _require(isinstance(operation, str) and isinstance(data, dict),
                 f"event {sequence}: malformed operation or data")
        if operation == "synth.run.open" and "matched_parameter" in data:
            match_run = True

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
        elif operation == "proof.inductive.match.branch":
            scope = tuple(within)
            refined = data.get("refined_indices")
            value_binders = data.get("value_binders")
            proof_binders = data.get("proof_binders")
            _require(len(scope) == 2 and scope[1].startswith("proof-match:") and
                     scope not in proof_matches and
                     isinstance(data.get("constructor"), str) and data["constructor"] and
                     isinstance(refined, list) and len(refined) == len(set(refined)) and
                     all(isinstance(name, str) and name for name in refined) and
                     isinstance(value_binders, list) and
                     all(isinstance(name, str) and name for name in value_binders) and
                     isinstance(proof_binders, list) and
                     all(isinstance(name, str) and name for name in proof_binders) and
                     len(value_binders + proof_binders) == len(set(value_binders + proof_binders)) and
                     data.get("runtime_value_created") is False,
                     f"event {sequence}: malformed indexed proof-match branch")
            proof_match_branches.setdefault(scope, []).append(data)
        elif operation == "proof.inductive.match":
            scope = tuple(within)
            branches = proof_match_branches.get(scope, [])
            _require(len(scope) == 2 and scope[1].startswith("proof-match:") and
                     scope not in proof_matches and
                     isinstance(data.get("scrutinee"), str) and data["scrutinee"] and
                     isinstance(data.get("family"), str) and data["family"] and
                     data.get("reachable_constructors") == len(branches) and
                     data.get("checked_arms") == len(branches) and
                     len(branches) == len({branch["constructor"] for branch in branches}) and
                     data.get("exhaustiveness_after_refinement") is True and
                     data.get("runtime_value_created") is False,
                     f"event {sequence}: malformed or incomplete indexed proof match")
            proof_matches.add(scope)
        elif operation == "proof.function.verify":
            function = data.get("function")
            parameter_sources = data.get("parameter_sources")
            status = data.get("status")
            checked_result = ((status == "unsat" and data.get("result_proposition") in terms) or
                              (status == "body-checked" and data.get("result_proposition") == ""))
            _require(isinstance(function, str) and function and
                     function not in proof_functions and
                     isinstance(parameter_sources, list) and
                     all(source_id in source_nodes
                         for source_id in parameter_sources) and
                     data.get("result_source") in source_nodes and
                     checked_result and
                     data.get("runtime_function_created") is False,
                     f"event {sequence}: malformed or repeated proof function")
            proof_functions[function] = data
        elif operation == "proof.induction.hypothesis.use":
            function = data.get("function")
            induction_parameter = data.get("induction_parameter")
            parent = data.get("parent_evidence")
            recursive = data.get("recursive_evidence")
            _require(within == [f"proof-function:{function}"] and
                     isinstance(function, str) and function and
                     isinstance(induction_parameter, str) and induction_parameter and
                     isinstance(parent, str) and parent and
                     isinstance(recursive, str) and recursive and recursive != parent and
                     isinstance(data.get("body"), str) and
                     data["body"].startswith(function) and
                     data.get("runtime_call_created") is False,
                     f"event {sequence}: malformed structural induction-hypothesis use")
            induction_hypothesis_uses.append(data)
        elif operation == "proof.function.apply":
            function = data.get("function")
            indices = data.get("index_arguments")
            arguments = data.get("proof_arguments")
            _require(function in proof_functions and
                     isinstance(data.get("body"), str) and
                     data["body"].startswith(function) and
                     isinstance(indices, list) and
                     all(isinstance(argument, str) and argument
                         for argument in indices) and
                     isinstance(arguments, list) and
                     all(isinstance(argument, str) and argument
                         for argument in arguments) and
                     data.get("runtime_call_created") is False,
                     f"event {sequence}: malformed proof function application")
        elif operation == "proof.search.open":
            hole = data.get("id")
            source_id = data.get("source")
            type_source = data.get("type_source")
            _require(isinstance(hole, str) and hole not in proof_holes,
                     f"event {sequence}: missing or reused proof hole ID")
            _require(source_id in source_nodes and type_source in source_nodes,
                     f"event {sequence}: proof hole names unknown source syntax")
            source_node = source_nodes[source_id]
            type_kind = source_nodes[type_source].get("syntax_kind")
            _require(source_node.get("syntax_kind") == "proof.expression.hole" and
                     type_kind in {"proof-type.identity", "proof-type.inductive"},
                     f"event {sequence}: proof hole source has the wrong syntax kind")
            span = source_node["span"]
            _require(source[span["begin"]["offset"]:span["end"]["offset"]] == b"?",
                     f"event {sequence}: proof hole source does not select `?`")
            identity_hole = type_kind == "proof-type.identity"
            grammar = data.get("grammar")
            expected_type = data.get("expected_type")
            checkpoint_mode = data.get("checkpoint_mode")
            identity_grammar = (["exact-local", "refl", "proof-application", "open"]
                                if checkpoint_mode else
                                ["exact-local", "refl", "proof-application"])
            type_shape_ok = ((identity_hole and isinstance(expected_type, str) and
                              expected_type.startswith("Id(") and
                              data.get("proposition") in terms and
                              grammar == identity_grammar and
                              isinstance(checkpoint_mode, bool)) or
                             (not identity_hole and isinstance(expected_type, str) and
                              expected_type and not expected_type.startswith("Id(") and
                              data.get("proposition") == "" and
                              grammar == ["exact-local", "induction-hypothesis"] and
                              data.get("nondecreasing_ih_candidates_enumerated") is False))
            _require(isinstance(data.get("binding"), str) and data["binding"] and
                     type_shape_ok and
                     isinstance(data.get("max_cost"), int) and
                     data["max_cost"] > 0 and
                     data.get("ill_typed_candidates_enumerated") is False,
                     f"event {sequence}: malformed typed proof hole")
            proof_holes[hole] = data
            proof_candidates_by_hole[hole] = []
        elif operation == "proof.search.candidate":
            hole = data.get("hole")
            _require(hole in proof_holes and hole not in proof_holes_closed,
                     f"event {sequence}: proof candidate names no open proof hole")
            production = data.get("production")
            complete = data.get("complete")
            closed_frontier = data.get("closed_frontier")
            open_leaves = data.get("open_leaves")
            _require(isinstance(data.get("body"), str) and data["body"] and
                     production in proof_holes[hole]["grammar"] and
                     data.get("expected_type") == proof_holes[hole]["expected_type"] and
                     data.get("exact_type") is True and
                     data.get("runtime_value_created") is False and
                     isinstance(data.get("cost"), int) and
                     0 <= data["cost"] <= proof_holes[hole]["max_cost"] and
                     isinstance(complete, bool) and
                     isinstance(closed_frontier, int) and closed_frontier >= 0 and
                     isinstance(open_leaves, int) and open_leaves >= 0 and
                     complete == (open_leaves == 0),
                     f"event {sequence}: malformed or ill-typed proof candidate")
            if production == "exact-local":
                _require(data.get("proof") == data["body"],
                         f"event {sequence}: local proof candidate loses its source binding")
            elif production == "refl":
                _require("proof" not in data and data["body"].startswith("refl(") and
                         data["body"].endswith(")"),
                         f"event {sequence}: reflexivity candidate is not Fine proof syntax")
            elif production == "open":
                _require(data["body"] == "?" and data["cost"] == 0 and
                         complete is False and closed_frontier == 0 and open_leaves == 1 and
                         proof_holes[hole].get("checkpoint_mode") is True,
                         f"event {sequence}: open candidate is not one typed residual hole")
            else:
                indices = data.get("index_arguments")
                arguments = data.get("proof_arguments")
                function = data.get("function")
                _require("proof" not in data and
                         isinstance(function, str) and function and
                         isinstance(indices, list) and
                         all(isinstance(argument, str) and argument
                             for argument in indices) and
                         isinstance(arguments, list) and
                         all(isinstance(argument, str) and argument
                             for argument in arguments) and
                         data["body"].startswith(function) and
                         ((not arguments and data["body"].endswith(")")) or
                          (arguments and " using [" in data["body"] and
                           data["body"].endswith("]") and
                           all(argument in data["body"] for argument in arguments))),
                         f"event {sequence}: proof application candidate loses its source tree")
                if production == "induction-hypothesis":
                    induction_parameter = data.get("induction_parameter")
                    parent = data.get("parent_evidence")
                    recursive = data.get("recursive_evidence")
                    _require(isinstance(induction_parameter, str) and induction_parameter and
                             isinstance(parent, str) and parent and
                             isinstance(recursive, str) and recursive and recursive != parent and
                             recursive in arguments,
                             f"event {sequence}: induction candidate loses its structural edge")
            _require(event_id not in proof_candidates,
                     f"event {sequence}: reused proof candidate event")
            proof_candidates[event_id] = data
            proof_candidates_by_hole[hole].append(event_id)
        elif operation == "proof.model.grammar":
            hole = data.get("hole")
            grammar = data.get("grammar")
            productions = data.get("productions")
            references = data.get("reference_candidates")
            _require(hole in proof_holes and hole not in proof_holes_closed and
                     isinstance(grammar, str) and grammar and
                     grammar not in proof_model_grammars and
                     data.get("max_cost") == proof_holes[hole]["max_cost"] and
                     isinstance(productions, list) and productions and
                     all(isinstance(production, str) and production
                         for production in productions) and
                     isinstance(data.get("preferred_complete"), bool) and
                     isinstance(data.get("preferred_closed_frontier"), int) and
                     data["preferred_closed_frontier"] >= 0 and
                     isinstance(data.get("preferred_open_leaves"), int) and
                     data["preferred_open_leaves"] >= 0 and
                     isinstance(data.get("preferred_cost"), int) and
                     0 <= data["preferred_cost"] <= data["max_cost"] and
                     isinstance(data.get("preferred_source"), str) and data["preferred_source"] and
                     references == proof_candidates_by_hole[hole],
                     f"event {sequence}: malformed proof model grammar")
            proof_model_grammars[grammar] = event
        elif operation == "proof.model.solve":
            hole = data.get("hole")
            grammar_event = events_by_id.get(data.get("grammar_event"))
            _require(hole in proof_holes and hole not in proof_holes_closed and
                     grammar_event is not None and
                     grammar_event.get("operation") == "proof.model.grammar" and
                     grammar_event["data"].get("hole") == hole and
                     data.get("grammar") == grammar_event["data"].get("grammar") and
                     data.get("complete") == grammar_event["data"].get("preferred_complete") and
                     data.get("closed_frontier") == grammar_event["data"].get("preferred_closed_frontier") and
                     data.get("open_leaves") == grammar_event["data"].get("preferred_open_leaves") and
                     data.get("cost") == grammar_event["data"].get("preferred_cost") and
                     data.get("status") == "sat" and
                     isinstance(data.get("model_value"), str) and
                     data["model_value"] and
                     isinstance(data.get("cost"), int) and
                     0 <= data["cost"] <= proof_holes[hole]["max_cost"] and
                     isinstance(data.get("complete"), bool) and
                     isinstance(data.get("closed_frontier"), int) and
                     data["closed_frontier"] >= 0 and
                     isinstance(data.get("open_leaves"), int) and
                     data["open_leaves"] >= 0 and
                     hole not in proof_model_solves,
                     f"event {sequence}: malformed proof datatype model result")
            proof_model_solves[hole] = event
        elif operation == "proof.model.lift":
            hole = data.get("hole")
            solve_event = events_by_id.get(data.get("solve_event"))
            candidate = proof_candidates.get(data.get("candidate"))
            _require(hole in proof_model_solves and hole not in proof_model_lifts and
                     solve_event is proof_model_solves[hole] and
                     candidate is not None and candidate.get("hole") == hole and
                     data.get("body") == candidate.get("body") and
                     data.get("complete") == candidate.get("complete") and
                     solve_event["data"].get("complete") == candidate.get("complete") and
                     solve_event["data"].get("closed_frontier") == candidate.get("closed_frontier") and
                     solve_event["data"].get("open_leaves") == candidate.get("open_leaves") and
                     (not proof_holes[hole].get("checkpoint_mode") or
                      data.get("body") ==
                      proof_model_grammars[solve_event["data"].get("grammar")]["data"].get("preferred_source")) and
                     data.get("in_reference_frontier") is True and
                     data.get("reparse_required") is True,
                     f"event {sequence}: proof model lift escaped its Fine frontier")
            proof_model_lifts[hole] = event
        elif operation == "proof.search.select":
            hole = data.get("hole")
            candidate_id = data.get("candidate")
            candidate = proof_candidates.get(candidate_id)
            _require(hole in proof_holes and hole not in proof_selections and
                     hole not in proof_holes_closed and candidate is not None and
                     candidate.get("hole") == hole,
                     f"event {sequence}: proof selection names no prior candidate")
            _require(data.get("body") == candidate.get("body") and
                     data.get("production") == candidate.get("production") and
                     data.get("complete") == candidate.get("complete"),
                     f"event {sequence}: proof selection changes its candidate")
            if hole in proof_model_lifts:
                _require(data.get("candidate") == proof_model_lifts[hole]["data"].get("candidate"),
                         f"event {sequence}: proof selection differs from lifted model")
            proof_selections[hole] = event
        elif operation == "proof.search.close":
            hole = data.get("hole")
            selection = proof_selections.get(hole)
            selected_candidate = data.get("selected_candidate")
            residual = data.get("residual_candidates")
            _require(hole in proof_holes and hole not in proof_holes_closed and
                     selection is not None and data.get("selection") == selection["event_id"] and
                     selected_candidate == selection["data"].get("candidate"),
                     f"event {sequence}: proof search close has no matching selection")
            _require(isinstance(residual, list) and len(residual) == len(set(residual)) and
                     all(candidate in proof_candidates and
                         proof_candidates[candidate].get("hole") == hole
                         for candidate in residual),
                     f"event {sequence}: malformed residual proof frontier")
            expected_residual = [candidate for candidate in proof_candidates_by_hole[hole]
                                 if candidate != selected_candidate]
            _require(selected_candidate not in residual and residual == expected_residual,
                     f"event {sequence}: proof search close loses or reorders its finite frontier")
            expected_status = "selected" if proof_candidates[selected_candidate].get("complete") else "checkpointed"
            _require(data.get("status") == expected_status and
                     data.get("materialization_requested") is True,
                     f"event {sequence}: proof search did not request source materialization")
            proof_holes_closed.add(hole)
        elif operation == "synth.hole.declare":
            hole = data.get("id")
            _require(isinstance(hole, str) and hole not in holes,
                     f"event {sequence}: missing or reused synthesis hole ID")
            _require(data.get("snapshot") in snapshots and data.get("source") in source_nodes,
                     f"event {sequence}: synthesis hole has unknown source identity")
            _require(source_nodes[data["source"]].get("snapshot") == data.get("snapshot") and
                     source_nodes[data["source"]].get("syntax_kind") == "expr.hole",
                     f"event {sequence}: synthesis hole source is not a hole in its snapshot")
            _require(isinstance(data.get("name"), str) and data["name"] and
                     data.get("expected_type") == "Int" and
                     data.get("grammar") == "fine.qf-lia-int.v1",
                     f"event {sequence}: synthesis hole has malformed typed grammar")
            inputs = data.get("grammar_inputs")
            _require(isinstance(inputs, list) and all(item in terms for item in inputs),
                     f"event {sequence}: synthesis hole grammar names unknown terms")
            holes[hole] = data
        elif operation == "synth.arm.close":
            hole = data.get("hole")
            _require(hole in holes and hole not in arm_witnesses and
                     isinstance(data.get("constructor"), str) and data["constructor"] and
                     isinstance(data.get("body"), str) and data["body"] and
                     data.get("semantic_term") in terms and data.get("status") == "verified",
                     f"event {sequence}: malformed or repeated synthesized arm witness")
            arm_witnesses[hole] = data
        elif operation == "fine.match-witness":
            match_witness_count += 1
            _require(match_run and match_witness_count == 1,
                     f"event {sequence}: unexpected or repeated match witness")
            replacements = data.get("replacements")
            _require(data.get("semantic_term") in terms and data.get("verified") is True and
                     isinstance(replacements, list) and
                     data.get("open_arms") == len(replacements),
                     f"event {sequence}: malformed verified match witness")
            ranges: list[tuple[int, int]] = []
            named_holes: set[str] = set()
            for replacement in replacements:
                _require(isinstance(replacement, dict) and
                         set(replacement) == {"hole", "from", "to", "insert"},
                         f"event {sequence}: malformed match replacement")
                hole = replacement.get("hole")
                begin, end = replacement.get("from"), replacement.get("to")
                _require(hole in holes and hole not in named_holes and
                         isinstance(begin, int) and isinstance(end, int) and
                         isinstance(replacement.get("insert"), str) and replacement["insert"],
                         f"event {sequence}: match replacement names an invalid hole")
                span = source_nodes[holes[hole]["source"]]["span"]
                _require((begin, end) == (span["begin"]["offset"], span["end"]["offset"]),
                         f"event {sequence}: match replacement moved outside its source hole")
                _require(hole in arm_witnesses and
                         replacement["insert"] == arm_witnesses[hole]["body"],
                         f"event {sequence}: match replacement disagrees with its verified arm witness")
                named_holes.add(hole)
                ranges.append((begin, end))
            _require(ranges == sorted(ranges) and
                     all(ranges[i - 1][1] <= ranges[i][0] for i in range(1, len(ranges))),
                     f"event {sequence}: match replacements overlap or are unordered")
            _require(named_holes == set(holes),
                     f"event {sequence}: match witness does not replace every open hole")
            _require(set(arm_witnesses) == set(holes),
                     f"event {sequence}: match witness has an unverified open arm")
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

        if operation == "proof-core.document.close":
            _require(within == [],
                     f"event {sequence}: definition-only document close has a run scope")
            _require(data.get("status") in {"verified", "checkpointed"},
                     f"event {sequence}: definition-only document close has an invalid status")
        if operation in {"check.run.close", "synth.run.close", "bisim.run.close", "predicate-check.run.close",
                         "predicate-induction.run.close", "proof-core.run.close", "proof-core.document.close"}:
            terminal_sequence = sequence
        if operation == "proof.run.close" and data.get("status") != "verified":
            terminal_sequence = sequence

    _require(len(documents) == 1, "replay must declare exactly one document")
    _require(len(snapshots) == 1, "replay must declare exactly one snapshot")
    _require(terminal_sequence == len(events) - 1,
             "replay has no terminal close")
    _require(term_lift_validations == set(terms),
             "replay does not exact-validate every generated Fine term")
    _require(not match_run or match_witness_count == 1,
             "match synthesis replay has no unique verified match witness")
    _require(proof_holes_closed == set(proof_holes),
             "typed proof search replay leaves an open proof hole")
    _require(set(proof_model_grammars) ==
             {event["data"]["grammar"] for event in proof_model_solves.values()} and
             set(proof_model_solves) == set(proof_model_lifts),
             "proof model replay leaves an unsolved or unlifted grammar")
    _require(all(use["function"] in proof_functions and
                 proof_functions[use["function"]].get("status") == "body-checked"
                 for use in induction_hypothesis_uses),
             "structural induction replay names an unverified proof function")

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
        "proof_holes": len(proof_holes),
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
