{
  description = "Fine, a solver language soft-forked from Z3";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
    in {
      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [
          cmake
          ninja
          python3
          clang
          pkg-config
          git
        ];
      };

      packages.${system}.default = pkgs.stdenv.mkDerivation {
        pname = "fine";
        version = "0.0.1";
        src = self;
        nativeBuildInputs = with pkgs; [ cmake ninja python3 ];
        cmakeFlags = [
          "-DZ3_BUILD_LIBZ3_SHARED=OFF"
          "-DZ3_BUILD_EXECUTABLE=OFF"
          "-DZ3_BUILD_TEST_EXECUTABLES=OFF"
          "-DFINE_BUILD_EXECUTABLE=ON"
        ];

        doInstallCheck = true;
        installCheckPhase = ''
          runHook preInstallCheck
          output="$($out/bin/fine demo-bisim)"
          echo "$output"
          grep -F "(left_0, right_0): true" <<<"$output"
          grep -F "(left_1, right_1): true" <<<"$output"
          grep -F "parse(print(lift(x))): exact ast identity" <<<"$output"
          parsed="$($out/bin/fine run "$src/fine/fixtures/two-state-bisim.fine")"
          echo "$parsed"
          grep -F "model bisim: Table((LeftState, RightState), Bool) = table(default: false)" <<<"$parsed"
          grep -F "(left_0, right_0): true" <<<"$parsed"
          grep -F "parse(print(lift(x))): exact ast identity" <<<"$parsed"
          synthesized="$($out/bin/fine run "$src/fine/fixtures/synth-max.fine")"
          echo "$synthesized"
          grep -F "source-program: synthesized max from 2 ground instances" <<<"$synthesized"
          grep -F "if (right >= left) { right } else { left }" <<<"$synthesized"
          grep -F "verification: no counterexample" <<<"$synthesized"
          grep -F "parse(print(lift(witness))): exact ast identity" <<<"$synthesized"
          refuted="$($out/bin/fine run "$src/fine/fixtures/check-counterexample.fine")"
          echo "$refuted"
          grep -F "refuted: subtraction_preserves_left" <<<"$refuted"
          grep -F "counterexample subtraction_preserves_left" <<<"$refuted"
          grep -F "a: Int = -1;" <<<"$refuted"
          grep -F "b: Int = 1;" <<<"$refuted"
          grep -F "parse(print(lift(values))): exact ast identity" <<<"$refuted"
          verified="$($out/bin/fine run "$src/fine/fixtures/check-valid.fine")"
          echo "$verified"
          grep -F "verified: addition_preserves_left" <<<"$verified"
          grep -F "counterexample: none" <<<"$verified"
          datatype="$($out/bin/fine run "$src/fine/fixtures/check-datatype-counterexample.fine")"
          echo "$datatype"
          grep -F "refuted: node_is_leaf" <<<"$datatype"
          grep -F "tree: Tree = node(7, leaf, leaf);" <<<"$datatype"
          grep -F "mark: Mark = marked;" <<<"$datatype"
          grep -F "parse(print(lift(values))): exact ast identity" <<<"$datatype"
          tuple="$($out/bin/fine run "$src/fine/fixtures/check-tuple-counterexample.fine")"
          echo "$tuple"
          grep -F "refuted: pair_claim" <<<"$tuple"
          grep -F "pair: (Int, Bool) = (7, true);" <<<"$tuple"
          grep -F "parse(print(lift(values))): exact ast identity" <<<"$tuple"
          projection="$($out/bin/fine run "$src/fine/fixtures/synth-projection.fine")"
          grep -F "source-program: synthesized keep from 1 ground instances; core kept 1" <<<"$projection"
          grep -Fx "value" <<<"$projection"
          three="$($out/bin/fine run "$src/fine/fixtures/synth-max-three.fine")"
          grep -F "source-program: synthesized largest from 3 ground instances; core kept 3" <<<"$three"
          grep -F "else { if" <<<"$three"
          grep -F "verification: no counterexample" <<<"$three"
          rain="$($out/bin/fine rain "$src/fine/fixtures/synth-max.fine")"
          echo "$rain"
          RAIN="$rain" ${pkgs.python3}/bin/python - <<'PY'
          import json, os
          events = [json.loads(line) for line in os.environ["RAIN"].splitlines()]
          assert events
          assert [event["sequence"] for event in events] == list(range(len(events)))
          assert all(event["schema"] == "fine.rainfall.v2" for event in events)
          operations = [event["operation"] for event in events]
          assert operations[:2] == [
              "source.document.declare", "source.snapshot.declare"
          ]
          required = [
              "synth.run.open",
              "synth.candidate.select",
              "synth.instance.activate",
              "solver.unsat-core",
              "synth.assemble-core",
              "z3.simplify",
              "synth.backend.accept",
              "fine.source-witness",
              "fine.witness.accept",
              "synth.run.close",
          ]
          positions = [operations.index(operation) for operation in required]
          assert positions == sorted(positions), (required, positions)
          internal = [event for event in events
                      if event["operation"] == "z3.theory-rewrite"]
          assert internal
          assert operations.index("synth.assemble-core") < internal[0]["sequence"]
          assert internal[-1]["sequence"] < operations.index("z3.simplify")
          assert all(event["producer"]["component"] ==
                     "z3.th_rewriter.reduce_app" for event in internal)
          terms = [event for event in events if event["operation"] == "term.declare"]
          handles = [event["data"]["identity"]["handle"] for event in terms]
          assert handles == list(range(len(handles)))
          verify = [event for event in events
                    if event["operation"] == "solver.query.result"
                    and event["data"].get("polarity") == "counterexample-exists"]
          assert verify[-1]["data"]["status"] == "unsat"
          assert verify[-1]["data"]["domain_outcome"] == "verified"
          models = [event for event in events
                    if event["operation"] == "solver.query.result"
                    and event["data"].get("model_assignments")]
          assert models
          assert all(event["data"]["relation"] == "equality-under-this-model"
                     for event in models)
          PY
          bisim_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/two-state-bisim.fine" > "$bisim_rain"
          ${pkgs.python3}/bin/python - "$bisim_rain" <<'PY'
          import json, sys
          with open(sys.argv[1]) as stream:
              events = [json.loads(line) for line in stream]
          assert events
          assert [event["sequence"] for event in events] == list(range(len(events)))
          operations = [event["operation"] for event in events]
          required = [
              "bisim.run.open",
              "bisim.clause.assert",
              "solver.query.open",
              "solver.query.result",
              "model.eval-cell",
              "bisim.extensionalize-model",
              "fine.model-witness",
              "fine.witness.accept",
              "bisim.run.close",
          ]
          positions = [operations.index(operation) for operation in required]
          assert positions == sorted(positions), (required, positions)
          clauses = [event for event in events
                     if event["operation"] == "bisim.clause.assert"]
          assert [event["data"]["role"] for event in clauses] == [
              "labels-agree", "left-step-matched", "right-step-matched",
              "initial-related"]
          queries = [event for event in events
                     if event["operation"] == "solver.query.result"]
          assert len(queries) == 1
          assert queries[0]["data"]["status"] == "sat"
          assert queries[0]["data"]["polarity"] == "model-exists"
          opened = [event for event in events
                    if event["operation"] == "solver.query.open"]
          assert len(opened) == 1
          assert opened[0]["data"]["mbqi"] is True
          assert opened[0]["data"]["ematching"] is False
          instances = [event for event in events
                       if event["operation"] == "z3.mbqi-instance"]
          assert instances
          assert all(event["sequence"] < queries[0]["sequence"]
                     for event in instances)
          assert all(event["producer"]["component"] ==
                     "z3.qi_queue.on_binding" for event in instances)
          assert all(event["data"]["instantiation_engine"] ==
                     "mbqi-only-query" for event in instances)
          assert {event["data"]["source_role"] for event in instances} == {
              "fine.bisim.labels-agree",
              "fine.bisim.left-step-matched",
              "fine.bisim.right-step-matched",
          }
          clause_events = [event for event in events
                           if event["operation"].startswith("z3.clause.")]
          assert clause_events
          assert all(event["sequence"] < queries[0]["sequence"]
                     for event in clause_events)
          assert all(event["producer"]["component"] == "z3.solver.on_clause"
                     for event in clause_events)
          assert {event["operation"] for event in clause_events} >= {
              "z3.clause.assume", "z3.clause.infer", "z3.clause.delete"
          }
          assert {event["data"]["proof_hint_head"] for event in clause_events} >= {
              "assumption", "inst", "del"
          }
          instance_clauses = [event for event in clause_events
                              if event["operation"] == "z3.clause.infer"
                              and event["data"]["proof_hint_head"] == "inst"]
          assert len(instance_clauses) == len(instances)
          events_by_id = {event["event_id"]: event for event in events}
          assert len({event["data"]["quantifier_instance_event"]
                      for event in instance_clauses}) == len(instances)
          for clause in instance_clauses:
              accepted = events_by_id[clause["data"]["quantifier_instance_event"]]
              assert accepted["operation"] == "z3.mbqi-instance"
              assert accepted["sequence"] < clause["sequence"]
              assert clause["data"]["quantifier"] == accepted["data"]["quantifier"]
              assert clause["data"]["instance"] == accepted["data"]["instance"]
              assert clause["data"]["ground_bindings"]
              assert clause["data"]["relation"] == \
                  "accepted-instance-became-admitted-clause"
          declared = {event["data"]["id"] for event in events
                      if event["operation"] == "term.declare"}
          assert all(reference in declared for event in clause_events
                     for reference in event["data"]["literals"])
          assert all(event["data"]["proof_hint"] in declared
                     for event in clause_events)
          assert all(event["data"]["literal_count"] ==
                     len(event["data"]["literals"])
                     for event in clause_events)
          cells = [event for event in events
                   if event["operation"] == "model.eval-cell"]
          assert len(cells) == 4
          assert all(event["data"]["relation"] == "equality-under-this-model"
                     for event in cells)
          assert all(event["data"]["model_completion"] for event in cells)
          witnesses = [event for event in events
                       if event["operation"] == "fine.model-witness"]
          assert len(witnesses) == 1
          assert witnesses[0]["data"]["parse_reify_exact_identity"] is True
          assert "(left_0, right_0): true" in witnesses[0]["data"]["source"]
          assert "z3.theory-rewrite" not in operations
          terms = [event for event in events if event["operation"] == "term.declare"]
          handles = [event["data"]["identity"]["handle"] for event in terms]
          assert handles == list(range(len(handles)))
          PY
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/two-state-bisim.fine" "$bisim_rain"

          bisim_projection="$(mktemp)"
          bisim_html="$(mktemp)"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-project \
            "$src/fine/fixtures/two-state-bisim.fine" "$bisim_rain" \
            --html "$bisim_html" > "$bisim_projection"
          ${pkgs.python3}/bin/python - "$bisim_projection" "$bisim_html" <<'PY'
          import json, pathlib, sys
          projection = json.loads(pathlib.Path(sys.argv[1]).read_text())
          html = pathlib.Path(sys.argv[2]).read_text()
          annotations = projection["annotations"]
          assert len(annotations) == 4
          assert len({item["claim"]["source"] for item in annotations}) == 1
          assert all(item["syntax_kind"] == "decl.proof" for item in annotations)
          assert all(item["claim"]["correspondence"] == "generated"
                     for item in annotations)
          activity = {item["activity"]["role"]: item["activity"]
                      for item in annotations}
          assert {role: len(item["accepted_instances"])
                  for role, item in activity.items()} == {
              "labels-agree": 1,
              "left-step-matched": 3,
              "right-step-matched": 2,
              "initial-related": 0,
          }
          instances = [instance for item in activity.values()
                       for instance in item["accepted_instances"]]
          assert len(instances) == 6
          assert all(instance["admitted_clause_event"] for instance in instances)
          assert all(instance["ground_bindings"] for instance in instances)
          assert "left-step-matched" in html
          assert "3 accepted instances, 3 admitted lemmas" in html
          assert html.count('<tr class="current"') == 1
          PY

          bisim_hostile="$(mktemp -d)"
          ${pkgs.python3}/bin/python - "$bisim_rain" "$bisim_hostile" <<'PY'
          import copy, json, pathlib, sys
          source, output = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
          events = [json.loads(line) for line in source.read_text().splitlines()]
          output.mkdir(exist_ok=True)
          instances = [event for event in events
                       if event["operation"] == "z3.mbqi-instance"]
          clause = next(event for event in events
                        if event["operation"] == "z3.clause.infer"
                        and event["data"]["proof_hint_head"] == "inst")

          wrong_event = copy.deepcopy(events)
          target = next(event for event in wrong_event
                        if event["event_id"] == clause["event_id"])
          target["data"]["quantifier_instance_event"] = instances[1]["event_id"]
          (output / "wrong-instance-event.rain").write_text("".join(
              json.dumps(event, separators=(",", ":")) + "\n"
              for event in wrong_event))

          wrong_term = copy.deepcopy(events)
          target = next(event for event in wrong_term
                        if event["event_id"] == clause["event_id"])
          target["data"]["instance"] = instances[1]["data"]["instance"]
          (output / "wrong-instance-term.rain").write_text("".join(
              json.dumps(event, separators=(",", ":")) + "\n"
              for event in wrong_term))
          PY
          for hostile in "$bisim_hostile"/*.rain; do
            if ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
              "$src/fine/fixtures/two-state-bisim.fine" "$hostile"; then
              echo "accepted hostile quantifier-clause join $hostile" >&2
              exit 1
            fi
          done
          check_rain="$($out/bin/fine rain "$src/fine/fixtures/check-counterexample.fine")"
          echo "$check_rain"
          RAIN="$check_rain" ${pkgs.python3}/bin/python - <<'PY'
          import json, os
          events = [json.loads(line) for line in os.environ["RAIN"].splitlines()]
          assert [event["sequence"] for event in events] == list(range(len(events)))
          operations = [event["operation"] for event in events]
          required = [
              "check.run.open",
              "check.counterexample.assert",
              "solver.query.open",
              "solver.query.result",
              "model.eval-assignment",
              "fine.counterexample-witness",
              "fine.witness.accept",
              "check.run.close",
          ]
          positions = [operations.index(operation) for operation in required]
          assert positions == sorted(positions), (required, positions)
          queries = [event for event in events
                     if event["operation"] == "solver.query.result"]
          assert len(queries) == 1
          assert queries[0]["data"]["status"] == "sat"
          assert queries[0]["data"]["polarity"] == "counterexample-exists"
          assert queries[0]["data"]["domain_outcome"] == "refuted"
          assignments = [event for event in events
                         if event["operation"] == "model.eval-assignment"]
          assert [event["data"]["parameter"] for event in assignments] == ["a", "b"]
          assert all(event["data"]["relation"] == "equality-under-this-model"
                     for event in assignments)
          witnesses = [event for event in events
                       if event["operation"] == "fine.counterexample-witness"]
          assert len(witnesses) == 1
          assert witnesses[0]["data"]["parse_reify_exact_identity"] is True
          assert "a: Int = -1;" in witnesses[0]["data"]["source"]
          assert "b: Int = 1;" in witnesses[0]["data"]["source"]
          assert "z3.theory-rewrite" not in operations
          assert "z3.mbqi-instance" not in operations
          source_nodes = [event for event in events
                          if event["operation"] == "source.node.declare"]
          edges = [event for event in events
                   if event["operation"] == "source.term.evidence"]
          assert source_nodes and edges
          declared_sources = {event["data"]["id"] for event in source_nodes}
          declared_terms = {event["data"]["id"] for event in events
                            if event["operation"] == "term.declare"}
          assert all(edge["data"]["source"] in declared_sources for edge in edges)
          assert all(edge["data"]["term"] in declared_terms for edge in edges)
          assert {edge["data"]["correspondence"] for edge in edges} == {
              "exact", "desugared"
          }
          PY
          check_rain_file="$(mktemp)"
          printf '%s\n' "$check_rain" > "$check_rain_file"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/check-counterexample.fine" "$check_rain_file"

          reopened_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/check-counterexample.fine" \
            > "$reopened_rain"
          ${pkgs.python3}/bin/python - "$check_rain_file" "$reopened_rain" <<'PY'
          import json, sys
          def document(path):
              with open(path) as stream:
                  return next(json.loads(line)["data"]["id"] for line in stream
                              if '"source.document.declare"' in line)
          assert document(sys.argv[1]) != document(sys.argv[2])
          PY

          hostile_dir="$(mktemp -d)"
          ${pkgs.python3}/bin/python - \
            "$src/fine/fixtures/check-counterexample.fine" \
            "$check_rain_file" "$hostile_dir" <<'PY'
          import copy, json, pathlib, sys
          source_path, rain_path, output = map(pathlib.Path, sys.argv[1:])
          source = source_path.read_bytes()
          events = [json.loads(line) for line in rain_path.read_text().splitlines()]
          output.mkdir(exist_ok=True)

          # Same byte length and stable spans do not transport a snapshot claim.
          changed = source.replace(b"-1", b"-2", 1)
          assert len(changed) == len(source) and changed != source
          (output / "changed.fine").write_bytes(changed)
          # Moving all following spans is also a different exact snapshot.
          (output / "moved.fine").write_bytes(b" " + source)

          def write(name, mutate):
              altered = copy.deepcopy(events)
              mutate(altered)
              (output / name).write_text(
                  "".join(json.dumps(event, separators=(",", ":")) + "\n"
                          for event in altered))

          write("cross-snapshot.rain", lambda value: next(
              event for event in value
              if event["operation"] == "source.term.evidence"
          )["data"].__setitem__("snapshot", "snapshot:old"))
          write("unknown-term.rain", lambda value: next(
              event for event in value
              if event["operation"] == "source.term.evidence"
          )["data"].__setitem__("term", "term:never"))
          def reuse_handle(value):
              terms = [event for event in value if event["operation"] == "term.declare"]
              terms[1]["data"]["identity"]["handle"] = terms[0]["data"]["identity"]["handle"]
          write("reused-handle.rain", reuse_handle)
          def wrong_manager(value):
              term = next(event for event in value if event["operation"] == "term.declare")
              term["data"]["identity"]["manager"] = "manager:other"
          write("cross-manager.rain", wrong_manager)
          write("internal-with-source.rain", lambda value: next(
              event for event in value
              if event["operation"] == "source.term.evidence"
          )["data"].__setitem__("correspondence", "internal_z3"))
          late = copy.deepcopy(events[-1])
          late["sequence"] = len(events)
          late["event_id"] = f"event:{len(events)}"
          late["operation"] = "late.old-generation"
          (output / "late-event.rain").write_text(
              "".join(json.dumps(event, separators=(",", ":")) + "\n"
                      for event in events + [late]))
          PY
          if ${pkgs.python3}/bin/python $out/bin/fine-rain-validate "$hostile_dir/changed.fine" "$check_rain_file"; then
            echo "accepted a span-preserving cross-revision replay" >&2; exit 1
          fi
          if ${pkgs.python3}/bin/python $out/bin/fine-rain-validate "$hostile_dir/moved.fine" "$check_rain_file"; then
            echo "accepted a moved-span cross-revision replay" >&2; exit 1
          fi
          for hostile in "$hostile_dir"/*.rain; do
            if ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
              "$src/fine/fixtures/check-counterexample.fine" "$hostile"; then
              echo "accepted hostile replay $hostile" >&2; exit 1
            fi
          done

          projection_dir="$(mktemp -d)"
          ${pkgs.python3}/bin/python - \
            "$src/fine/fixtures/check-counterexample.fine" "$projection_dir" <<'PY'
          import json, pathlib, sys
          source_path, output = map(pathlib.Path, sys.argv[1:])
          source = source_path.read_bytes()
          negative = source.index(b"-1")
          (output / "transport.json").write_text(json.dumps([
              {"from": 0, "to": 0, "insert": "// displayed revision\n"},
              {"from": negative, "to": negative + 2, "insert": "-2"},
          ]))
          (output / "delete.json").write_text(json.dumps([
              {"from": 0, "to": len(source), "insert": ""},
          ]))
          # Even byte-identical output belongs to a new, unadmitted revision.
          (output / "same-bytes.json").write_text(json.dumps([
              {"from": negative, "to": negative + 2, "insert": "-1"},
          ]))
          (output / "overlap.json").write_text(json.dumps([
              {"from": negative, "to": negative + 2, "insert": "-2"},
              {"from": negative + 1, "to": negative + 2, "insert": "1"},
          ]))
          PY
          ${pkgs.python3}/bin/python $out/bin/fine-rain-project \
            "$src/fine/fixtures/check-counterexample.fine" "$check_rain_file" \
            > "$projection_dir/current.json"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-project \
            "$src/fine/fixtures/check-counterexample.fine" "$check_rain_file" \
            --edits "$projection_dir/transport.json" \
            --html "$projection_dir/transport.html" \
            --write-source "$projection_dir/displayed.fine" \
            > "$projection_dir/transported.json"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-project \
            "$src/fine/fixtures/check-counterexample.fine" "$check_rain_file" \
            --edits "$projection_dir/delete.json" \
            > "$projection_dir/deleted.json"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-project \
            "$src/fine/fixtures/check-counterexample.fine" "$check_rain_file" \
            --edits "$projection_dir/same-bytes.json" \
            > "$projection_dir/same-bytes-out.json"
          if ${pkgs.python3}/bin/python $out/bin/fine-rain-project \
            "$src/fine/fixtures/check-counterexample.fine" "$check_rain_file" \
            --edits "$projection_dir/overlap.json"; then
            echo "accepted overlapping projection edits" >&2; exit 1
          fi
          ${pkgs.python3}/bin/python - "$projection_dir" <<'PY'
          import json, pathlib, sys
          root = pathlib.Path(sys.argv[1])
          read = lambda name: json.loads((root / name).read_text())
          current = read("current.json")
          transported = read("transported.json")
          deleted = read("deleted.json")
          same = read("same-bytes-out.json")
          assert {item["status"] for item in current["annotations"]} == {"current"}
          assert {item["status"] for item in transported["annotations"]} == {"transported"}
          assert {item["status"] for item in deleted["annotations"]} == {"unplaced"}
          assert {item["status"] for item in same["annotations"]} == {"transported"}
          assert current["display_snapshot"]["admitted_by_rainfall"] is True
          assert transported["display_snapshot"]["admitted_by_rainfall"] is False
          assert same["display_snapshot"]["admitted_by_rainfall"] is False
          assert (same["claim_snapshot"]["identity"]["exact_source_hash"] ==
                  same["display_snapshot"]["identity"]["exact_source_hash"])
          claim_revision = transported["claim_snapshot"]["identity"]["revision"]
          assert transported["display_snapshot"]["identity"]["revision"] == claim_revision + 1
          page = (root / "transport.html").read_text()
          assert 'data-state="transported"' in page
          assert "do not describe this one" in page
          PY
          PYTHONPATH="$out/bin" ${pkgs.python3}/bin/python - <<'PY'
          from rainfall_projection import Edit, transport_range
          edit = lambda begin, end, text: Edit(begin, end, text.encode(), text)
          assert transport_range(5, 10, [edit(0, 0, "xx")]) == (7, 12)
          assert transport_range(5, 10, [edit(5, 5, "xx")]) == (7, 12)
          assert transport_range(5, 10, [edit(10, 10, "xx")]) == (5, 10)
          assert transport_range(5, 10, [edit(7, 7, "xx")]) == (5, 12)
          assert transport_range(5, 10, [edit(5, 10, "")]) is None
          assert transport_range(5, 10, [edit(5, 10, "x")]) == (5, 6)
          assert transport_range(5, 10, [edit(3, 7, "")]) == (3, 6)
          assert transport_range(5, 10, [edit(8, 12, "")]) == (5, 8)
          assert transport_range(5, 10, [edit(0, 0, "x"), edit(7, 7, "y")]) == (6, 12)
          PY

          generation_dir="$(mktemp -d)"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-generation request \
            "$src/fine/fixtures/check-counterexample.fine" \
            --document document:live-test --revision 0 --generation generation:0 \
            > "$generation_dir/request0.json"
          $out/bin/fine rain --document document:live-test --revision 0 \
            --generation generation:0 "$src/fine/fixtures/check-counterexample.fine" \
            > "$generation_dir/rain0.jsonl"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-generation admit \
            "$generation_dir/request0.json" \
            "$src/fine/fixtures/check-counterexample.fine" \
            "$src/fine/fixtures/check-counterexample.fine" \
            "$generation_dir/rain0.jsonl" > "$generation_dir/admit0.json"

          ${pkgs.python3}/bin/python $out/bin/fine-rain-generation request \
            "$projection_dir/displayed.fine" \
            --document document:live-test --revision 1 --generation generation:1 \
            > "$generation_dir/request1.json"
          $out/bin/fine rain --document document:live-test --revision 1 \
            --generation generation:1 "$projection_dir/displayed.fine" \
            > "$generation_dir/rain1.jsonl"
          $out/bin/fine rain --document document:live-test --revision 1 \
            --generation generation:other "$projection_dir/displayed.fine" \
            > "$generation_dir/other-generation.jsonl"
          $out/bin/fine rain --document document:other --revision 1 \
            --generation generation:1 "$projection_dir/displayed.fine" \
            > "$generation_dir/other-document.jsonl"
          $out/bin/fine rain --document document:live-test --revision 2 \
            --generation generation:1 "$projection_dir/displayed.fine" \
            > "$generation_dir/other-revision.jsonl"
          $out/bin/fine rain --document document:live-test --revision 1 \
            --generation generation:1 "$src/fine/fixtures/check-counterexample.fine" \
            > "$generation_dir/other-source.jsonl"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-generation admit \
            "$generation_dir/request1.json" "$projection_dir/displayed.fine" \
            "$projection_dir/displayed.fine" "$generation_dir/rain1.jsonl" \
            > "$generation_dir/admit1.json"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-generation admit \
            "$generation_dir/request1.json" "$projection_dir/displayed.fine" \
            "$src/fine/fixtures/check-counterexample.fine" "$generation_dir/rain0.jsonl" \
            > "$generation_dir/late.json"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-generation admit \
            "$generation_dir/request1.json" "$projection_dir/displayed.fine" \
            "$projection_dir/displayed.fine" "$generation_dir/other-generation.jsonl" \
            > "$generation_dir/other-generation.json"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-generation admit \
            "$generation_dir/request1.json" "$projection_dir/displayed.fine" \
            "$projection_dir/displayed.fine" "$generation_dir/other-document.jsonl" \
            > "$generation_dir/other-document.json"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-generation admit \
            "$generation_dir/request1.json" "$projection_dir/displayed.fine" \
            "$projection_dir/displayed.fine" "$generation_dir/other-revision.jsonl" \
            > "$generation_dir/other-revision.json"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-generation admit \
            "$generation_dir/request1.json" "$projection_dir/displayed.fine" \
            "$src/fine/fixtures/check-counterexample.fine" "$generation_dir/other-source.jsonl" \
            > "$generation_dir/other-source.json"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-generation admit \
            "$generation_dir/request1.json" "$src/fine/fixtures/check-counterexample.fine" \
            "$projection_dir/displayed.fine" "$generation_dir/rain1.jsonl" \
            > "$generation_dir/advanced-display.json"
          if ${pkgs.python3}/bin/python $out/bin/fine-rain-generation admit \
            "$generation_dir/request1.json" "$projection_dir/displayed.fine" \
            "$src/fine/fixtures/check-counterexample.fine" "$hostile_dir/late-event.rain"; then
            echo "generation admission accepted an invalid candidate trace" >&2; exit 1
          fi
          ${pkgs.python3}/bin/python - "$generation_dir" <<'PY'
          import json, pathlib, sys
          root = pathlib.Path(sys.argv[1])
          read = lambda name: json.loads((root / name).read_text())
          assert read("admit0.json")["status"] == "admitted"
          admitted = read("admit1.json")
          assert admitted["status"] == "admitted"
          assert admitted["generation"] == "generation:1"
          assert {item["status"] for item in admitted["projection"]["annotations"]} == {"current"}
          assert read("late.json")["reason"] == "late-or-unrequested-generation"
          assert read("other-generation.json")["reason"] == "late-or-unrequested-generation"
          assert read("other-document.json")["reason"] == "different-document"
          assert read("other-revision.json")["reason"] == "different-revision"
          assert read("other-source.json")["reason"] == "different-source"
          assert read("advanced-display.json")["reason"] == "display-advanced-after-request"
          request = read("request1.json")
          assert request["rain_arguments"] == [
              "rain", "--document", "document:live-test", "--revision", "1",
              "--generation", "generation:1",
          ]
          with (root / "rain1.jsonl").open() as stream:
              events = [json.loads(line) for line in stream]
          assert {event["run"] for event in events} == {"generation:1"}
          snapshot = next(event for event in events
                          if event["operation"] == "source.snapshot.declare")
          assert snapshot["data"]["identity"]["document"] == "document:live-test"
          assert snapshot["data"]["identity"]["revision"] == 1
          PY

          host_dir="$(mktemp -d)"
          host_work="$(mktemp -d)"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-host init \
            "$host_dir" "$src/fine/fixtures/check-counterexample.fine" \
            --document document:host-test > "$host_work/init.json"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-host run \
            "$host_dir" --fine "$out/bin/fine" > "$host_work/admit0.json"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-host advance \
            "$host_dir" "$projection_dir/transport.json" > "$host_work/advance1.json"
          ${pkgs.python3}/bin/python - "$host_work" <<'PY'
          import json, pathlib, sys
          root = pathlib.Path(sys.argv[1])
          (root / "edit2.json").write_text(json.dumps([
              {"from": 0, "to": 0, "insert": "// displayed r2\n"},
          ]))
          PY
          cat > "$host_work/slow-fine" <<EOF
          #!${pkgs.runtimeShell}
          ${pkgs.coreutils}/bin/sleep 1
          exec "$out/bin/fine" "\$@"
          EOF
          chmod +x "$host_work/slow-fine"
          generation1="$(${pkgs.python3}/bin/python -c \
            'import json,sys; print(json.load(open(sys.argv[1]))["generation"])' \
            "$host_work/advance1.json")"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-host run \
            "$host_dir" --fine "$host_work/slow-fine" --generation "$generation1" \
            > "$host_work/late1.json" &
          host_pid=$!
          ${pkgs.coreutils}/bin/sleep 0.2
          ${pkgs.python3}/bin/python $out/bin/fine-rain-host advance \
            "$host_dir" "$host_work/edit2.json" > "$host_work/advance2.json"
          wait "$host_pid"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-host run \
            "$host_dir" --fine "$out/bin/fine" > "$host_work/admit2.json"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-host state \
            "$host_dir" > "$host_work/state.json"
          ${pkgs.python3}/bin/python - "$host_work" "$host_dir" <<'PY'
          import json, pathlib, sys
          root, host = map(pathlib.Path, sys.argv[1:])
          read = lambda name: json.loads((root / name).read_text())
          assert read("admit0.json")["status"] == "admitted"
          assert read("advance1.json")["annotation_states"] == {
              "current": 0, "transported": 11, "unplaced": 0,
          }
          assert read("advance2.json")["annotation_states"] == {
              "current": 0, "transported": 11, "unplaced": 0,
          }
          late = read("late1.json")
          assert late["status"] == "discarded"
          assert late["reason"] == "late-or-unrequested-generation"
          assert read("admit2.json")["status"] == "admitted"
          state = read("state.json")
          assert state["display_snapshot"]["revision"] == 2
          assert {item["status"] for item in state["annotations"]} == {"current"}
          generation1 = read("advance1.json")["generation"]
          generation2 = read("advance2.json")["generation"]
          assert state["generations"][generation1]["status"] == "discarded"
          assert state["generations"][generation2]["status"] == "admitted"
          for record in state["generations"].values():
              assert (host / record["source_file"]).is_file()
              assert (host / record["request_file"]).is_file()
          PY

          failure_host="$(mktemp -d)"
          failure_work="$(mktemp -d)"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-host init \
            "$failure_host" "$src/fine/fixtures/check-counterexample.fine" \
            --document document:failure-test > "$failure_work/init.json"
          ${pkgs.python3}/bin/python - \
            "$src/fine/fixtures/check-counterexample.fine" "$failure_work" <<'PY'
          import json, pathlib, sys
          source = pathlib.Path(sys.argv[1]).read_bytes()
          root = pathlib.Path(sys.argv[2])
          (root / "break.json").write_text(json.dumps([
              {"from": 0, "to": len(source), "insert": "check broken("},
          ]))
          PY
          ${pkgs.python3}/bin/python $out/bin/fine-rain-host advance \
            "$failure_host" "$failure_work/break.json" > "$failure_work/advance.json"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-host run \
            "$failure_host" --fine "$out/bin/fine" > "$failure_work/failed.json"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-host state \
            "$failure_host" > "$failure_work/state.json"
          ${pkgs.python3}/bin/python - "$failure_work" <<'PY'
          import json, pathlib, sys
          root = pathlib.Path(sys.argv[1])
          read = lambda name: json.loads((root / name).read_text())
          assert read("failed.json")["status"] == "failed"
          state = read("state.json")
          current = state["current_generation"]
          assert state["generations"][current]["status"] == "failed"
          assert state["annotations"] == []
          PY
          datatype_rain="$($out/bin/fine rain "$src/fine/fixtures/check-datatype-counterexample.fine")"
          RAIN="$datatype_rain" ${pkgs.python3}/bin/python - <<'PY'
          import json, os
          events = [json.loads(line) for line in os.environ["RAIN"].splitlines()]
          assignments = [event for event in events
                         if event["operation"] == "model.eval-assignment"]
          assert [event["data"]["parameter"] for event in assignments] == [
              "tree", "mark"
          ]
          assert all(event["data"]["relation"] == "equality-under-this-model"
                     for event in assignments)
          witnesses = [event for event in events
                       if event["operation"] == "fine.counterexample-witness"]
          assert len(witnesses) == 1
          witness = witnesses[0]["data"]
          assert witness["parse_reify_exact_identity"] is True
          assert "tree: Tree = node(7, leaf, leaf);" in witness["source"]
          assert "mark: Mark = marked;" in witness["source"]
          terms = [event for event in events
                   if event["operation"] == "term.declare"]
          assert any("node 7 leaf leaf" in event["data"]["text"]
                     for event in terms)
          PY
          runHook postInstallCheck
        '';
      };
    };
}
