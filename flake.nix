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
