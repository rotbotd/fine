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
          predicate_result="$($out/bin/fine run "$src/fine/fixtures/predicate-step.fine")"
          echo "$predicate_result"
          grep -F "derived: under_once" <<<"$predicate_result"
          grep -F "predicate: Step" <<<"$predicate_result"
          grep -F "derivation-witness: erased" <<<"$predicate_result"
          predicate_junk="$($out/bin/fine run "$src/fine/fixtures/predicate-junk.fine")"
          echo "$predicate_junk"
          grep -F "not-derived: constructor_junk" <<<"$predicate_junk"
          predicate_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/predicate-step.fine" > "$predicate_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/predicate-step.fine" "$predicate_rain"
          invariant="$($out/bin/fine run "$src/fine/fixtures/predicate-invariant.fine")"
          echo "$invariant"
          grep -F "verified-predicate-invariant: distinct_indices" <<<"$invariant"
          grep -F "counterexample: none" <<<"$invariant"
          false_invariant="$($out/bin/fine run "$src/fine/fixtures/predicate-invariant-false.fine")"
          echo "$false_invariant"
          grep -F "refuted-predicate-invariant: equal_indices" <<<"$false_invariant"
          grep -F "counterexample: fixedpoint reachability only" <<<"$false_invariant"
          invariant_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/predicate-invariant.fine" > "$invariant_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/predicate-invariant.fine" "$invariant_rain"
          grep -F '"operation":"z3.spacer.lemma-export"' "$invariant_rain"
          grep -F '"operation":"z3.spacer.predecessor"' "$invariant_rain"
          grep -F '"operation":"z3.spacer.unfold"' "$invariant_rain"
          two_premises="$($out/bin/fine run "$src/fine/fixtures/predicate-two-premises.fine")"
          echo "$two_premises"
          grep -F "verified-predicate-invariant: distinct_indices" <<<"$two_premises"
          two_premises_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/predicate-two-premises.fine" > "$two_premises_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/predicate-two-premises.fine" "$two_premises_rain"
          ${pkgs.python3}/bin/python - "$two_premises_rain" <<'PY'
          import json, pathlib, sys
          events = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
          rule = next(event for event in events
                      if event["operation"] == "fine.predicate-constructor.rule"
                      and event["data"]["constructor"] == "pairwise")
          assert rule["data"]["premises"] == 2
          assert rule["data"]["recursive_premises"] == 2
          assert any(event["operation"] == "z3.spacer.lemma-export" for event in events)
          PY
          predicate_induction="$($out/bin/fine run "$src/fine/fixtures/predicate-induction.fine")"
          echo "$predicate_induction"
          grep -F "verified-predicate-induction: distinct_indices" <<<"$predicate_induction"
          grep -F "constructor-branches: 2 verified" <<<"$predicate_induction"
          false_predicate_induction="$($out/bin/fine run "$src/fine/fixtures/predicate-induction-false.fine")"
          echo "$false_predicate_induction"
          grep -F "refuted-predicate-induction: equal_indices" <<<"$false_predicate_induction"
          grep -F "failed-constructor: root" <<<"$false_predicate_induction"
          contextual_induction="$($out/bin/fine run \
            "$src/fine/fixtures/predicate-context-induction.fine")"
          echo "$contextual_induction"
          grep -F "verified-predicate-induction: preserves_ceiling" <<<"$contextual_induction"
          contextual_induction_false="$($out/bin/fine run \
            "$src/fine/fixtures/predicate-context-induction-false.fine")"
          echo "$contextual_induction_false"
          grep -F "refuted-predicate-induction: strict_ceiling" <<<"$contextual_induction_false"
          grep -F "failed-constructor: root" <<<"$contextual_induction_false"
          contextual_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/predicate-context-induction.fine" \
            > "$contextual_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/predicate-context-induction.fine" "$contextual_rain"
          ${pkgs.python3}/bin/python - "$contextual_rain" <<'PY'
          import json, pathlib, sys
          events = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
          opened = next(event for event in events
                        if event["operation"] == "predicate-induction.run.open")
          assert opened["data"]["context_parameters"] == 1
          assert opened["data"]["context_assumptions"] == 1
          hypothesis = next(event for event in events
                            if event["operation"] == "predicate-induction.hypothesis")
          assert hypothesis["data"]["constructor"] == "under"
          assert hypothesis["data"]["generalized_parameters"] == 1
          terms = {event["data"]["id"]: event["data"]["text"]
                   for event in events if event["operation"] == "term.declare"}
          text = terms[hypothesis["data"]["induction_hypothesis"]]
          assert "forall[" in text
          assert "fine.predicate-induction.preserves_ceiling.under.premise0" in text
          branch = next(event for event in events
                        if event["operation"] == "predicate-induction.branch.open"
                        and event["data"]["constructor"] == "under")
          assert branch["data"]["context_assumptions"]
          assert branch["data"]["goal"]
          PY
          preservation="$($out/bin/fine run \
            "$src/fine/fixtures/predicate-preservation.fine")"
          echo "$preservation"
          grep -F "verified-predicate-induction: marked_preservation" <<<"$preservation"
          grep -F "constructor-branches: 2 verified" <<<"$preservation"
          preservation_false="$($out/bin/fine run \
            "$src/fine/fixtures/predicate-preservation-false.fine")"
          echo "$preservation_false"
          grep -F "refuted-predicate-induction: odd_mark_preservation" <<<"$preservation_false"
          grep -F "failed-constructor: root" <<<"$preservation_false"
          preservation_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/predicate-preservation.fine" \
            > "$preservation_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/predicate-preservation.fine" "$preservation_rain"
          ${pkgs.python3}/bin/python - "$preservation_rain" <<'PY'
          import json, pathlib, sys
          events = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
          opened = next(event for event in events
                        if event["operation"] == "predicate-induction.run.open")
          assert opened["data"]["predicate_assumptions"] == 1
          assert opened["data"]["predicate_guarantee"] == "Marked"
          inversions = [event for event in events
                        if event["operation"] == "predicate-induction.assumption.invert"]
          constructions = [event for event in events
                           if event["operation"] == "predicate-induction.goal.construct"]
          assert [event["data"]["constructor"] for event in inversions] == ["root", "under"]
          assert [event["data"]["constructor"] for event in constructions] == ["root", "under"]
          assert all(event["data"]["predicate"] == "Marked" for event in inversions + constructions)
          assert all(event["data"]["alternatives"] == 2 for event in inversions + constructions)
          assert not any(event["operation"] == "predicate-induction.goal-constructor" for event in events)
          terms = {event["data"]["id"]: event["data"]["text"]
                   for event in events if event["operation"] == "term.declare"}
          under_inversion = next(event for event in inversions
                                 if event["data"]["constructor"] == "under")
          under_construction = next(event for event in constructions
                                    if event["data"]["constructor"] == "under")
          assert len({under_inversion["data"][key]
                      for key in ("assumption", "inversion", "resource")}) == 3
          assert "exists[" in terms[under_inversion["data"]["inversion"]]
          assert "_d_Marked" in terms[under_inversion["data"]["inversion"]]
          assert "exists[" in terms[under_construction["data"]["construction"]]
          hypothesis = next(event for event in events
                            if event["operation"] == "predicate-induction.hypothesis"
                            and event["data"]["constructor"] == "under")
          assert "_d_Marked" in terms[hypothesis["data"]["induction_hypothesis"]]
          branch = next(event for event in events
                        if event["operation"] == "predicate-induction.branch.open"
                        and event["data"]["constructor"] == "under")
          assert branch["data"]["inverted_assumptions"] == 1
          assert branch["data"]["goal_constructor_alternatives"] == 2
          assert branch["data"]["context_assumptions"] != branch["data"]["context_resources"]
          assert branch["data"]["goal"] != branch["data"]["goal_resource"]
          results = [event for event in events
                     if event["operation"] == "predicate-induction.branch.result"]
          assert all(event["data"]["status"] == "unsat" for event in results)
          PY
          arbitrary_preservation="$($out/bin/fine run \
            "$src/fine/fixtures/predicate-arbitrary-preservation.fine")"
          echo "$arbitrary_preservation"
          grep -F "verified-predicate-induction: marked_preservation" <<<"$arbitrary_preservation"
          arbitrary_preservation_false="$($out/bin/fine run \
            "$src/fine/fixtures/predicate-arbitrary-preservation-false.fine")"
          echo "$arbitrary_preservation_false"
          grep -F "refuted-predicate-induction: odd_mark_preservation" \
            <<<"$arbitrary_preservation_false"
          grep -F "failed-constructor: root" <<<"$arbitrary_preservation_false"
          arbitrary_invalid="$(mktemp)"
          if $out/bin/fine run \
            "$src/fine/fixtures/predicate-arbitrary-preservation-invalid-witness.fine" \
            >"$arbitrary_invalid" 2>&1; then
            echo "invalid one-layer constrained-field witness unexpectedly passed" >&2
            exit 1
          fi
          grep -F 'constrained field `Marked.grow.token` is unavailable' "$arbitrary_invalid"
          arbitrary_preservation_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/predicate-arbitrary-preservation.fine" \
            > "$arbitrary_preservation_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/predicate-arbitrary-preservation.fine" \
            "$arbitrary_preservation_rain"
          ${pkgs.python3}/bin/python - "$arbitrary_preservation_rain" <<'PY'
          import json, pathlib, sys
          events = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
          terms = {event["data"]["id"]: event["data"]["text"]
                   for event in events if event["operation"] == "term.declare"}
          relations = {event["data"]["predicate"]: event
                       for event in events if event["operation"] == "fine.predicate.relation"}
          assert relations["Step"]["data"]["horn_complete"] is True
          assert relations["Marked"]["data"]["horn_complete"] is False
          assert relations["Marked"]["data"]["least_relation"] is False
          availability = [event for event in events
                          if event["operation"] == "predicate-induction.one-layer.availability"]
          assert len(availability) == 1
          assert availability[0]["data"]["predicate"] == "Marked"
          assert availability[0]["data"]["constructor"] == "grow"
          assert availability[0]["data"]["availability_mode"] == "declared-witness"
          assert availability[0]["data"]["domain_outcome"] == "available"
          fields = [event for event in events
                    if event["operation"] in {
                        "predicate-induction.goal.arbitrary-field",
                        "predicate-induction.assumption.arbitrary-field"}]
          assert [(event["data"]["consumer_constructor"], event["data"]["use"])
                  for event in fields] == [
                      ("root", "goal"), ("root", "assumption"),
                      ("under", "goal"), ("under", "assumption")]
          assert all(event["data"]["predicate"] == "Marked" for event in fields)
          assert all(event["data"]["predicate_constructor"] == "grow" for event in fields)
          assert all(event["data"]["binder"] == "token" for event in fields)
          assert all(event["data"]["view"] == "At" for event in fields)
          assert all(event["data"]["recursive_premises"] == 1 for event in fields)
          assert len({fields[0]["data"][key]
                      for key in ("requirement", "availability", "premises", "total_field")}) == 4
          total = terms[fields[0]["data"]["total_field"]]
          assert "forall[" in total
          assert "_d_implies" in total
          assert "_d_Marked(_v_0" in total
          under_goal = next(event for event in events
                            if event["operation"] == "predicate-induction.goal.construct"
                            and event["data"]["constructor"] == "under")
          under_assumption = next(event for event in events
                                  if event["operation"] == "predicate-induction.assumption.invert"
                                  and event["data"]["constructor"] == "under")
          assert "exists[" in terms[under_goal["data"]["construction"]]
          assert "forall[" in terms[under_goal["data"]["construction"]]
          assert "exists[" in terms[under_assumption["data"]["inversion"]]
          assert "forall[" in terms[under_assumption["data"]["inversion"]]
          results = [event for event in events
                     if event["operation"] == "predicate-induction.branch.result"]
          assert all(event["data"]["status"] == "unsat" for event in results)
          PY
          total_field_preservation="$($out/bin/fine run \
            "$src/fine/fixtures/predicate-total-field-preservation.fine")"
          echo "$total_field_preservation"
          grep -F "verified-predicate-induction: marked_flip" <<<"$total_field_preservation"
          total_field_false="$($out/bin/fine run \
            "$src/fine/fixtures/predicate-total-field-preservation-false.fine")"
          echo "$total_field_false"
          grep -F "refuted-predicate-induction: marked_diagonal" <<<"$total_field_false"
          grep -F "failed-constructor: root" <<<"$total_field_false"
          total_field_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/predicate-total-field-preservation.fine" \
            > "$total_field_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/predicate-total-field-preservation.fine" "$total_field_rain"
          ${pkgs.python3}/bin/python - "$total_field_rain" <<'PY'
          import json, pathlib, sys
          events = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
          terms = {event["data"]["id"]: event["data"]["text"]
                   for event in events if event["operation"] == "term.declare"}
          relations = {event["data"]["predicate"]: event
                       for event in events if event["operation"] == "fine.predicate.relation"}
          assert relations["Step"]["data"]["horn_complete"] is False
          assert relations["Marked"]["data"]["horn_complete"] is False
          total = next(event for event in events
                       if event["operation"] == "predicate-induction.arbitrary.total-hypothesis"
                       and event["data"]["constructor"] == "under_abs")
          assert total["data"]["binder"] == "branch_name"
          assert total["data"]["recursive_hypotheses"] == 1
          total_text = terms[total["data"]["total_hypothesis"]]
          assert "forall[" in total_text
          assert "_d_implies" in total_text
          assert total_text.count("_d_Marked") == 2
          goal_field = next(event for event in events
                            if event["operation"] == "predicate-induction.goal.arbitrary-field"
                            and event["data"]["consumer_constructor"] == "under_abs")
          assumption_field = next(event for event in events
                                  if event["operation"] == "predicate-induction.assumption.arbitrary-field"
                                  and event["data"]["consumer_constructor"] == "under_abs")
          assert goal_field["data"]["binder"] == "fresh"
          assert assumption_field["data"]["binder"] == "fresh"
          assert total["data"]["binder_term"] != goal_field["data"]["binder_term"]
          assert "forall[" in terms[goal_field["data"]["total_field"]]
          under = next(event for event in events
                       if event["operation"] == "predicate-induction.branch.result"
                       and event["data"]["constructor"] == "under_abs")
          assert under["data"]["status"] == "unsat"
          PY
          predicate_induction_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/predicate-induction-two-premises.fine" \
            > "$predicate_induction_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/predicate-induction-two-premises.fine" "$predicate_induction_rain"
          ${pkgs.python3}/bin/python - "$predicate_induction_rain" <<'PY'
          import json, pathlib, sys
          events = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
          results = [event for event in events
                     if event["operation"] == "predicate-induction.branch.result"]
          assert [event["data"]["constructor"] for event in results] == [
              "swap_left", "swap_right", "pairwise"]
          assert all(event["data"]["status"] == "unsat" for event in results)
          hypotheses = [event for event in events
                        if event["operation"] == "predicate-induction.hypothesis"
                        and event["data"]["constructor"] == "pairwise"]
          assert [event["data"]["premise_ordinal"] for event in hypotheses] == [0, 1]
          assert len({event["data"]["recursive_premise"] for event in hypotheses}) == 2
          assert len({event["data"]["induction_hypothesis"] for event in hypotheses}) == 2
          PY
          arbitrary_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/predicate-arbitrary-fresh-induction.fine" \
            > "$arbitrary_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/predicate-arbitrary-fresh-induction.fine" "$arbitrary_rain"
          ${pkgs.python3}/bin/python - "$arbitrary_rain" <<'PY'
          import json, pathlib, sys
          events = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
          one = lambda operation: next(event for event in events if event["operation"] == operation)
          view = one("fine.view.declare")
          assert view["data"]["view"] == "FreshApart"
          assert view["data"]["carrier"] == "Name"
          assert view["data"]["wrapper_sort"] is False
          relation = one("fine.predicate.relation")
          assert relation["data"]["horn_complete"] is False
          assert relation["data"]["least_relation"] is False
          retained = one("fine.predicate-constructor.branch")
          assert retained["data"]["constructor"] == "root"
          assert retained["data"]["lowered_to_horn"] is False
          assert not any(event["operation"] == "fine.predicate-constructor.rule" for event in events)
          field = one("fine.predicate-constructor.arbitrary-field")
          assert field["data"]["constructor"] == "under_abs"
          assert field["data"]["lowered_to_horn"] is False
          binder_edges = [event for event in events
                          if event["operation"] == "source.term.evidence"
                          and event["data"]["term"] == field["data"]["binder_term"]]
          source_kinds = {
              event["data"]["id"]: event["data"]["syntax_kind"]
              for event in events if event["operation"] == "source.node.declare"}
          arbitrary_edges = [event for event in binder_edges
                             if source_kinds[event["data"]["source"]] == "predicate.arbitrary-field"]
          assert len(arbitrary_edges) == 1
          availability = one("predicate-induction.arbitrary.availability")
          assert availability["data"]["domain_outcome"] == "available"
          hypothesis = one("predicate-induction.arbitrary-hypothesis")
          assert hypothesis["data"]["binder_term"] == field["data"]["binder_term"]
          assert hypothesis["data"]["requirement"] == field["data"]["requirement"]
          total = one("predicate-induction.arbitrary.total-hypothesis")
          assert total["data"]["binder_term"] == field["data"]["binder_term"]
          assert total["data"]["recursive_hypotheses"] == 1
          terms = {event["data"]["id"]: event["data"]["text"]
                   for event in events if event["operation"] == "term.declare"}
          assert "forall[" in terms[total["data"]["total_hypothesis"]]
          branch = next(event for event in events
                        if event["operation"] == "predicate-induction.branch.open"
                        and event["data"]["constructor"] == "under_abs")
          assert branch["data"]["arbitrary_fields"] == 1
          assert branch["data"]["recursive_hypotheses"] == 1
          result = next(event for event in events
                        if event["operation"] == "predicate-induction.branch.result"
                        and event["data"]["constructor"] == "under_abs")
          assert result["data"]["status"] == "unsat"
          PY
          empty_arbitrary="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-empty-arbitrary-view.fine" \
            >"$empty_arbitrary" 2>&1; then
            echo "expected an empty arbitrary view to be rejected" >&2
            exit 1
          fi
          grep -F "arbitrary-fresh induction would be vacuous" "$empty_arbitrary"
          partial_membership="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-arbitrary-fresh-membership.fine" \
            >"$partial_membership" 2>&1; then
            echo "expected partial Horn membership to be rejected" >&2
            exit 1
          fi
          grep -F "has an arbitrary-fresh constructor retained outside Horn lowering" \
            "$partial_membership"
          cofinite_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/predicate-cofinite-support-induction.fine" \
            > "$cofinite_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/predicate-cofinite-support-induction.fine" "$cofinite_rain"
          ${pkgs.python3}/bin/python - "$cofinite_rain" <<'PY'
          import json, pathlib, sys
          events = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
          one = lambda operation: next(event for event in events if event["operation"] == operation)
          functions = {event["data"]["function"]: event
                       for event in events
                       if event["operation"] == "function.recursive-definition"}
          assert set(functions) >= {"open_at", "support_cutoff"}
          assert functions["open_at"]["data"]["arms"] == 4
          assert functions["support_cutoff"]["data"]["arms"] == 4
          view = one("fine.view.declare")
          assert view["data"]["view"] == "FreshFor"
          assert view["data"]["availability"] == "declared-witness"
          assert view["data"]["witness"]
          field = one("fine.predicate-constructor.arbitrary-field")
          assert field["data"]["availability_witness"]
          assert field["data"]["availability_witness"] != field["data"]["binder_term"]
          availability = one("predicate-induction.arbitrary.availability")
          assert availability["data"]["availability_mode"] == "declared-witness"
          assert availability["data"]["availability_witness"] == field["data"]["availability_witness"]
          assert availability["data"]["domain_outcome"] == "available"
          hypothesis = one("predicate-induction.arbitrary-hypothesis")
          assert hypothesis["data"]["binder_term"] == field["data"]["binder_term"]
          assert hypothesis["data"]["binder_term"] != field["data"]["availability_witness"]
          assert hypothesis["data"]["recursive_premise"]
          assert hypothesis["data"]["induction_hypothesis"]
          terms = {event["data"]["id"]: event["data"]["text"]
                   for event in events if event["operation"] == "term.declare"}
          premise_text = terms[hypothesis["data"]["recursive_premise"]]
          assert "open_at" in premise_text
          assert terms[hypothesis["data"]["binder_term"]] in premise_text
          assert "support_cutoff" in terms[hypothesis["data"]["requirement"]]
          PY
          invalid_witness="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-invalid-view-witness.fine" \
            >"$invalid_witness" 2>&1; then
            echo "expected an invalid constrained-view witness to be rejected" >&2
            exit 1
          fi
          grep -F 'declared witness for constrained view `FreshFor` fails its requirement' \
            "$invalid_witness"
          rejected_universal="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-predicate-universal.fine" \
            >"$rejected_universal" 2>&1; then
            echo "accepted a premise-only predicate constructor parameter" >&2
            exit 1
          fi
          grep -F 'would be one-witness search, not a universal constructor field' "$rejected_universal"
          induction="$($out/bin/fine run "$src/fine/fixtures/induction-length.fine")"
          echo "$induction"
          grep -F "verified: length_nonnegative" <<<"$induction"
          grep -F "induction: direct-subterm on xs" <<<"$induction"
          grep -F "counterexample: none" <<<"$induction"
          equivariance="$($out/bin/fine run \
            "$src/fine/fixtures/induction-open-equivariance.fine")"
          echo "$equivariance"
          grep -F "verified: opening_equivariant" <<<"$equivariance"
          grep -F "induction: direct-subterm on term" <<<"$equivariance"
          grep -F "counterexample: none" <<<"$equivariance"
          reusable_proof="$($out/bin/fine run \
            "$src/fine/fixtures/reusable-proof-induction.fine")"
          echo "$reusable_proof"
          grep -F "verified-proof: opening_equivariant" <<<"$reusable_proof"
          grep -F "verified-predicate-induction: safe_opening" <<<"$reusable_proof"
          grep -F "constructor-branches: 1 verified" <<<"$reusable_proof"
          predicate_proof="$($out/bin/fine run \
            "$src/fine/fixtures/predicate-reusable-proof.fine")"
          echo "$predicate_proof"
          grep -F "verified-proof: step_distinct" <<<"$predicate_proof"
          grep -F "predicate: Step" <<<"$predicate_proof"
          grep -F "verified-predicate-induction: use_step_distinct" <<<"$predicate_proof"
          predicate_proof_false="$(mktemp)"
          if $out/bin/fine run \
            "$src/fine/fixtures/predicate-reusable-proof-false.fine" \
            >"$predicate_proof_false" 2>&1; then
            echo "admitted a refuted predicate-induction proof" >&2
            exit 1
          fi
          grep -F "refuted-proof: step_equal" "$predicate_proof_false"
          grep -F "failed-constructor: root" "$predicate_proof_false"
          if grep -F "unreachable" "$predicate_proof_false"; then
            echo "continued after a refuted predicate-induction proof" >&2
            exit 1
          fi
          predicate_proof_rain="$(mktemp)"
          $out/bin/fine rain \
            "$src/fine/fixtures/predicate-reusable-proof.fine" >"$predicate_proof_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/predicate-reusable-proof.fine" "$predicate_proof_rain"
          ${pkgs.python3}/bin/python - "$predicate_proof_rain" <<'PY'
          import json, pathlib, sys
          events = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
          admissions = [event for event in events if event["operation"] == "proof.admit"]
          assert len(admissions) == 1
          admission = admissions[0]
          assert admission["data"]["proof"] == "step_distinct"
          assert admission["data"]["predicate"] == "Step"
          assert admission["data"]["qid"] == "fine.proof.step_distinct"
          assert admission["data"]["verified_constructor_branches"] == 2
          assert admission["data"]["verified_before_admission"] is True
          assert admission["data"]["added_to_fixedpoint"] is False
          uses = [event for event in events if event["operation"] == "proof.use"]
          assert len(uses) == 1
          assert uses[0]["data"]["proof"] == "step_distinct"
          assert uses[0]["data"]["consumer"] == "use_step_distinct"
          assert uses[0]["data"]["constructor"] == "requested"
          proof_results = [event for event in events
                           if event["operation"] == "predicate-induction.branch.result"
                           and event["within"][0] == "proof:step_distinct"]
          assert [event["data"]["constructor"] for event in proof_results] == ["root", "under"]
          assert all(event["data"]["status"] == "unsat" for event in proof_results)
          PY
          no_predicate_proof="$(mktemp)"
          ${pkgs.python3}/bin/python - \
            "$src/fine/fixtures/predicate-reusable-proof.fine" "$no_predicate_proof" <<'PY'
          import pathlib, sys
          source = pathlib.Path(sys.argv[1]).read_text()
          start = source.index("proof step_distinct")
          end = source.index("\npredicate Request")
          pathlib.Path(sys.argv[2]).write_text(source[:start] + source[end + 1:])
          PY
          no_predicate_proof_output="$($out/bin/fine run "$no_predicate_proof")"
          echo "$no_predicate_proof_output"
          grep -F "refuted-predicate-induction: use_step_distinct" \
            <<<"$no_predicate_proof_output"
          grep -F "failed-constructor: requested" <<<"$no_predicate_proof_output"
          predicate_renaming="$($out/bin/fine run \
            "$src/fine/fixtures/predicate-renaming-proof.fine")"
          echo "$predicate_renaming"
          grep -F "verified-proof: named_rename" <<<"$predicate_renaming"
          grep -F "constructor-branches: 3 verified" <<<"$predicate_renaming"
          grep -F "verified-predicate-induction: use_named_rename" <<<"$predicate_renaming"
          predicate_renaming_false="$(mktemp)"
          if $out/bin/fine run \
            "$src/fine/fixtures/predicate-renaming-proof-false.fine" \
            >"$predicate_renaming_false" 2>&1; then
            echo "admitted predicate-index renaming without renaming the term" >&2
            exit 1
          fi
          grep -F "refuted-proof: name_capture" "$predicate_renaming_false"
          grep -F "failed-constructor: here" "$predicate_renaming_false"
          predicate_renaming_false_rain="$(mktemp)"
          if $out/bin/fine rain \
            "$src/fine/fixtures/predicate-renaming-proof-false.fine" \
            >"$predicate_renaming_false_rain"; then
            echo "refuted predicate proof rain unexpectedly returned success" >&2
            exit 1
          fi
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/predicate-renaming-proof-false.fine" \
            "$predicate_renaming_false_rain"
          predicate_renaming_rain="$(mktemp)"
          $out/bin/fine rain \
            "$src/fine/fixtures/predicate-renaming-proof.fine" >"$predicate_renaming_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/predicate-renaming-proof.fine" "$predicate_renaming_rain"
          ${pkgs.python3}/bin/python - "$predicate_renaming_rain" <<'PY'
          import json, pathlib, sys
          events = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
          terms = {event["data"]["id"]: event["data"]["text"]
                   for event in events if event["operation"] == "term.declare"}
          construction = next(event for event in events
                              if event["operation"] == "predicate-induction.goal.construct"
                              and event["within"][0] == "proof:named_rename"
                              and event["data"]["constructor"] == "application")
          text = terms[construction["data"]["construction"]]
          assert "_d_app_app(_d_Fine_predicate_Named_application_arg1," \
                 "_d_Fine_predicate_Named_application_arg2)" in text
          assert "Fine.predicate-induction.named_rename.one-layer." in text
          assert ".application.parameter" in text
          parameter_pairs = [event for event in events
                             if event["operation"] == "predicate-induction.one-layer.parameter"
                             and event["within"][0] == "proof:named_rename"
                             and event["data"]["consumer_constructor"] == "application"
                             and event["data"]["predicate_constructor"] == "application"]
          assert [event["data"]["parameter_ordinal"] for event in parameter_pairs] == [0, 1, 2]
          assert all(event["data"]["schema_parameter"] != event["data"]["local_parameter"]
                     for event in parameter_pairs)
          uses = [event for event in events if event["operation"] == "proof.use"]
          assert len(uses) == 1
          assert uses[0]["data"]["proof"] == "named_rename"
          assert uses[0]["data"]["consumer"] == "use_named_rename"
          assert uses[0]["data"]["constructor"] == "requested"
          PY
          no_renaming_proof="$(mktemp)"
          ${pkgs.python3}/bin/python - \
            "$src/fine/fixtures/predicate-renaming-proof.fine" "$no_renaming_proof" <<'PY'
          import pathlib, sys
          source = pathlib.Path(sys.argv[1]).read_text()
          start = source.index("proof named_rename")
          end = source.index("\npredicate Request")
          pathlib.Path(sys.argv[2]).write_text(source[:start] + source[end + 1:])
          PY
          no_renaming_proof_output="$($out/bin/fine run "$no_renaming_proof")"
          echo "$no_renaming_proof_output"
          grep -F "refuted-predicate-induction: use_named_rename" \
            <<<"$no_renaming_proof_output"
          grep -F "failed-constructor: requested" <<<"$no_renaming_proof_output"
          append_proof="$($out/bin/fine run \
            "$src/fine/fixtures/proof-append-length.fine")"
          echo "$append_proof"
          grep -F "verified-proof: append_length" <<<"$append_proof"
          grep -F "verified: three_chunk_size" <<<"$append_proof"
          old_lemma="$(mktemp)"
          sed 's/^proof append_length/lemma append_length/' \
            "$src/fine/fixtures/proof-append-length.fine" >"$old_lemma"
          if $out/bin/fine run "$old_lemma" >"$old_lemma.out" 2>&1; then
            echo "accepted the removed lemma alias" >&2
            exit 1
          fi
          grep -F "expected a Fine declaration" "$old_lemma.out"
          old_proof_family="$(mktemp)"
          sed 's/^predicate Step/proof family Step/' \
            "$src/fine/fixtures/predicate-step.fine" >"$old_proof_family"
          if $out/bin/fine run "$old_proof_family" >"$old_proof_family.out" 2>&1; then
            echo "accepted the removed proof-family syntax" >&2
            exit 1
          fi
          grep -F 'expected `(`' "$old_proof_family.out"
          false_proof="$(mktemp)"
          cat >"$false_proof" <<'EOF'
          proof impossible(x: Int) {
            assumes {}
            ensures { x == x + 1; }
          }
          check unreachable(x: Int) {
            assumes {}
            ensures { x == x; }
          }
          EOF
          if $out/bin/fine run "$false_proof" >"$false_proof.out" 2>&1; then
            echo "admitted a refuted reusable proof" >&2
            exit 1
          fi
          grep -F "refuted-proof: impossible" "$false_proof.out"
          if grep -F "unreachable" "$false_proof.out"; then
            echo "continued after a refuted reusable proof" >&2
            exit 1
          fi
          proof_rain="$(mktemp)"
          $out/bin/fine rain \
            "$src/fine/fixtures/reusable-proof-induction.fine" >"$proof_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/reusable-proof-induction.fine" "$proof_rain"
          ${pkgs.python3}/bin/python - "$proof_rain" <<'PY'
          import json, pathlib, sys
          events = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
          admissions = [event for event in events if event["operation"] == "proof.admit"]
          assert len(admissions) == 1
          assert admissions[0]["data"]["proof"] == "opening_equivariant"
          assert admissions[0]["data"]["qid"] == "fine.proof.opening_equivariant"
          assert admissions[0]["data"]["verified_before_admission"] is True
          uses = [event for event in events if event["operation"] == "proof.use"]
          assert len(uses) == 1
          assert uses[0]["data"]["proof"] == "opening_equivariant"
          assert uses[0]["data"]["consumer"] == "safe_opening"
          assert uses[0]["data"]["constructor"] == "generated"
          source_kinds = {event["data"]["syntax_kind"] for event in events
                          if event["operation"] == "source.node.declare"}
          assert "decl.proof" in source_kinds
          PY
          no_proof="$(mktemp)"
          ${pkgs.python3}/bin/python - \
            "$src/fine/fixtures/reusable-proof-induction.fine" "$no_proof" <<'PY'
          import pathlib, sys
          source = pathlib.Path(sys.argv[1]).read_text()
          start = source.index("proof opening_equivariant")
          end = source.index("\npredicate SafeOpening")
          pathlib.Path(sys.argv[2]).write_text(source[:start] + source[end + 1:])
          PY
          set +e
          timeout 2 $out/bin/fine run "$no_proof" >"$no_proof.out" 2>&1
          no_proof_status="$?"
          set -e
          test "$no_proof_status" -eq 124 || { cat "$no_proof.out"; exit 1; }
          bad_recursion="$(mktemp)"
          sed 's/length(tail)/length(xs)/' \
            "$src/fine/fixtures/induction-length.fine" > "$bad_recursion"
          if $out/bin/fine run "$bad_recursion" >"$bad_recursion.out" 2>&1; then
            echo "accepted a non-decreasing recursive call" >&2
            exit 1
          fi
          grep -F 'must pass a direct `List` pattern field' "$bad_recursion.out"
          bad_parameter="$(mktemp)"
          sed 's/inducts(xs)/inducts(ys)/' \
            "$src/fine/fixtures/induction-length.fine" > "$bad_parameter"
          if $out/bin/fine run "$bad_parameter" >"$bad_parameter.out" 2>&1; then
            echo "accepted an unknown induction parameter" >&2
            exit 1
          fi
          grep -F 'unknown induction parameter `ys`' "$bad_parameter.out"
          false_induction="$(mktemp)"
          sed 's/length(xs) >= 0/length(xs) == 1/' \
            "$src/fine/fixtures/induction-length.fine" > "$false_induction"
          refuted_induction="$($out/bin/fine run "$false_induction")"
          echo "$refuted_induction"
          grep -F "refuted: length_nonnegative" <<<"$refuted_induction"
          grep -F "induction: direct-subterm on xs" <<<"$refuted_induction"
          grep -F "xs: List = nil;" <<<"$refuted_induction"
          no_induction="$(mktemp)"
          sed '/  inducts(xs);/d' "$src/fine/fixtures/induction-length.fine" > "$no_induction"
          set +e
          ${pkgs.coreutils}/bin/timeout 2 $out/bin/fine run "$no_induction" >/dev/null 2>&1
          no_induction_status=$?
          set -e
          test "$no_induction_status" -eq 124
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
          match_open="$($out/bin/fine run "$src/fine/fixtures/synth-match-open.fine")"
          echo "$match_open"
          grep -F "source-match: synthesized unwrap with 1 open arms" <<<"$match_open"
          grep -F "some(value) => value" <<<"$match_open"
          grep -F "verification: no counterexample" <<<"$match_open"
          match_materialized="$($out/bin/fine run "$src/fine/fixtures/synth-match-materialized.fine")"
          echo "$match_materialized"
          grep -F "verified-match: unwrap with 0 open arms; selected 0 ground instances" \
            <<<"$match_materialized"
          grep -F "some(value) => value" <<<"$match_materialized"
          match_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/synth-match-open.fine" > "$match_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/synth-match-open.fine" "$match_rain"
          match_hostile_dir="$(mktemp -d)"
          ${pkgs.python3}/bin/python - "$match_rain" "$match_hostile_dir" <<'PY'
          import copy, json, pathlib, sys
          events = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
          target = pathlib.Path(sys.argv[2])
          for name in ("span", "insert"):
              changed = copy.deepcopy(events)
              witness = next(event for event in changed
                             if event["operation"] == "fine.match-witness")
              replacement = witness["data"]["replacements"][0]
              if name == "span":
                  replacement["from"] += 1
              else:
                  replacement["insert"] = "fallback"
              (target / f"{name}.rain").write_text(
                  "\n".join(json.dumps(event, separators=(",", ":"))
                             for event in changed) + "\n")
          PY
          for hostile in "$match_hostile_dir"/*.rain; do
            if ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
              "$src/fine/fixtures/synth-match-open.fine" "$hostile"; then
              echo "match Rainfall accepted a mutated source replacement" >&2
              exit 1
            fi
          done
          match_host="$(mktemp -d)"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-host init \
            "$match_host" "$src/fine/fixtures/synth-match-open.fine" >/dev/null
          ${pkgs.python3}/bin/python $out/bin/fine-rain-host run \
            "$match_host" --fine "$out/bin/fine" >/dev/null
          ${pkgs.python3}/bin/python $out/bin/fine-rain-host materialize \
            "$match_host" >/dev/null
          ${pkgs.python3}/bin/python - "$match_host" <<'PY'
          import json, pathlib, sys
          host = pathlib.Path(sys.argv[1])
          state = json.loads((host / "state.json").read_text())
          assert state["display_snapshot"]["revision"] == 1
          assert "some(value) => value" in state["display_source"]
          assert "?payload" not in state["display_source"]
          assert state["generations"][state["current_generation"]]["status"] == "requested"
          PY
          ${pkgs.python3}/bin/python $out/bin/fine-rain-host run \
            "$match_host" --fine "$out/bin/fine" >/dev/null
          ${pkgs.python3}/bin/python - "$match_host" <<'PY'
          import json, pathlib, sys
          host = pathlib.Path(sys.argv[1])
          state = json.loads((host / "state.json").read_text())
          record = state["generations"][state["current_generation"]]
          assert record["status"] == "admitted"
          events = [json.loads(line) for line in (host / record["trace_file"]).read_text().splitlines()]
          operations = [event["operation"] for event in events]
          assert "synth.hole.declare" not in operations
          assert "synth.candidate.select" not in operations
          witnesses = [event for event in events
                       if event["operation"] == "fine.match-witness"]
          assert len(witnesses) == 1
          assert witnesses[0]["data"]["open_arms"] == 0
          assert witnesses[0]["data"]["replacements"] == []
          assert operations.count("solver.query.open") == 1
          assert operations.count("solver.query.result") == 1
          PY
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
          assert all(event["data"]["representation"] ==
                     "fine.generated-term.v1" for event in terms)
          assert all(event["data"]["origin"] for event in terms)
          assert all(event["data"]["text"] and
                     event["data"]["z3_text_diagnostic"] is not None
                     for event in terms)
          validations = [event for event in events
                         if event["operation"] == "term.lift.validate"]
          assert {event["data"]["term"] for event in validations} == {
              event["data"]["id"] for event in terms
          }
          assert all(event["data"]["parse_reify_exact_identity"] is True
                     for event in validations)
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
          assert all(event["data"]["representation"] ==
                     "fine.generated-term.v1" for event in terms)
          assert all(event["data"]["origin"] for event in terms)
          validations = [event for event in events
                         if event["operation"] == "term.lift.validate"]
          assert {event["data"]["term"] for event in validations} == {
              event["data"]["id"] for event in terms
          }
          PY
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/two-state-bisim.fine" "$bisim_rain"

          induction_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/induction-length.fine" > "$induction_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/induction-length.fine" "$induction_rain"
          ${pkgs.python3}/bin/python - "$induction_rain" <<'PY'
          import json, sys
          with open(sys.argv[1]) as stream:
              events = [json.loads(line) for line in stream]
          operations = [event["operation"] for event in events]
          required = [
              "function.recursive-definition",
              "check.run.open",
              "check.induction.translate",
              "check.counterexample.assert",
              "solver.query.open",
              "solver.query.result",
              "check.run.close",
          ]
          positions = [operations.index(operation) for operation in required]
          assert positions == sorted(positions), (required, positions)
          translation = next(event for event in events
                             if event["operation"] == "check.induction.translate")
          assert translation["data"]["parameter"] == "xs"
          assert translation["data"]["order"] == "constructor-direct-field"
          assert translation["data"]["constructors"] == 2
          assert translation["data"]["recursive_positions"] == 1
          assert translation["data"]["generalized_parameters"] == 0
          assert translation["data"]["responsibility"] == \
              "fine-generated-constructor-induction"
          branches = [event for event in events
                      if event["operation"] == "check.induction.branch"]
          assert [event["data"]["constructor"] for event in branches] == [
              "nil", "cons"]
          hypotheses = [event for event in events
                        if event["operation"] == "check.induction.hypothesis"]
          assert [(event["data"]["constructor"],
                   event["data"]["field_ordinal"],
                   event["data"]["generalized_parameters"])
                  for event in hypotheses] == [("cons", 1, 0)]
          opened = next(event for event in events
                        if event["operation"] == "solver.query.open")
          assert opened["data"]["induction_translation"] is True
          assert opened["data"]["ematching"] is True
          assert opened["data"]["mbqi"] is False
          result = next(event for event in events
                        if event["operation"] == "solver.query.result")
          assert result["data"]["status"] == "unsat"
          assert result["data"]["domain_outcome"] == "verified"
          clauses = [event for event in events
                     if event["operation"].startswith("z3.clause.")]
          assert {event["operation"] for event in clauses} >= {
              "z3.clause.assume", "z3.clause.infer"
          }
          terms = {event["data"]["id"]: event["data"]["text"]
                   for event in events if event["operation"] == "term.declare"}
          clause_text = "\n".join(
              terms[reference]
              for event in clauses for reference in event["data"]["literals"])
          assert "_d_tail_tail_cons(_d_Fine_check_length_nonnegative_arg0)" in clause_text
          assert "_d_case_def_0_length" in clause_text
          assert "_d_recfun_num_rounds_0" in clause_text
          generated = [event for event in events
                       if event["operation"] == "source.term.evidence"
                       and event["data"]["correspondence"] == "generated"]
          source_kinds = {
              next(node["data"]["syntax_kind"] for node in events
                   if node["operation"] == "source.node.declare"
                   and node["data"]["id"] == edge["data"]["source"])
              for edge in generated
          }
          assert {"decl.function", "decl.check"} <= source_kinds
          for instance in (event for event in events
                           if event["operation"] == "z3.quantifier-instance"):
              assert instance["data"]["instantiation_engine"] == \
                  "ematching-only-query"
              assert instance["data"]["ematching"] is True
              assert instance["data"]["mbqi"] is False
          PY

          equivariance_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/induction-open-equivariance.fine" \
            > "$equivariance_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/induction-open-equivariance.fine" \
            "$equivariance_rain"
          ${pkgs.python3}/bin/python - "$equivariance_rain" <<'PY'
          import json, pathlib, sys
          events = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
          translation = next(event for event in events
                             if event["operation"] == "check.induction.translate")
          assert translation["data"]["order"] == "constructor-direct-field"
          assert translation["data"]["constructors"] == 4
          assert translation["data"]["recursive_positions"] == 3
          assert translation["data"]["generalized_parameters"] == 3
          branches = [event["data"]["constructor"] for event in events
                      if event["operation"] == "check.induction.branch"]
          assert branches == ["bound", "free", "app", "abs"]
          hypotheses = [event for event in events
                        if event["operation"] == "check.induction.hypothesis"]
          assert [(event["data"]["constructor"], event["data"]["field_ordinal"])
                  for event in hypotheses] == [("app", 0), ("app", 1), ("abs", 0)]
          assert all(event["data"]["generalized_parameters"] == 3
                     for event in hypotheses)
          instances = [event for event in events
                       if event["operation"] == "z3.quantifier-instance"]
          assert 0 < len(instances) < 100
          assert {event["data"]["source_role"] for event in instances} == {
              "fine.induction.opening_equivariant.app.field0",
              "fine.induction.opening_equivariant.app.field1",
              "fine.induction.opening_equivariant.abs.field0",
          }
          result = next(event for event in events
                        if event["operation"] == "solver.query.result")
          assert result["data"]["status"] == "unsat"
          PY

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
          assert all(item["syntax_kind"] == "decl.solve" for item in annotations)
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
          assert "_d_select" in html
          assert "(select " not in html
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

          false_lift = copy.deepcopy(events)
          validation = next(event for event in false_lift
                            if event["operation"] == "term.lift.validate")
          validation["data"]["parse_reify_exact_identity"] = False
          (output / "false-lift-identity.rain").write_text("".join(
              json.dumps(event, separators=(",", ":")) + "\n"
              for event in false_lift))

          changed_rendering = copy.deepcopy(events)
          declaration = next(event for event in changed_rendering
                             if event["operation"] == "term.declare")
          declaration["data"]["text"] += " "
          (output / "changed-lift-rendering.rain").write_text("".join(
              json.dumps(event, separators=(",", ":")) + "\n"
              for event in changed_rendering))
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
          live_host="$(mktemp -d)"
          live_work="$(mktemp -d)"
          cat > "$live_work/slow-fine" <<EOF
          #!${pkgs.runtimeShell}
          ${pkgs.coreutils}/bin/sleep 0.4
          exec "$out/bin/fine" "\$@"
          EOF
          chmod +x "$live_work/slow-fine"
          ${pkgs.python3}/bin/python - "$out/bin" "$live_host" \
            "$src/fine/fixtures/two-state-bisim.fine" "$live_work/slow-fine" <<'PY'
          import json, pathlib, sys, threading, time, urllib.error, urllib.request
          sys.path.insert(0, sys.argv[1])
          from rainfall_live import (LiveServer, initialize_session, make_handler,
                                     minimal_edit)

          host, source, fine = map(pathlib.Path, sys.argv[2:])
          edit = minimal_edit("a😀c", "a😺c")[0]
          assert edit == {"from": 1, "to": 5, "insert": "😺"}
          session = initialize_session(host, source, fine, "document:browser-test", False)
          server = LiveServer(("127.0.0.1", 0), make_handler(session))
          thread = threading.Thread(target=server.serve_forever, daemon=True)
          thread.start()
          base = f"http://127.0.0.1:{server.server_address[1]}"

          def get(path):
              with urllib.request.urlopen(base + path) as response:
                  return response.status, response.read()

          def post(text):
              body = json.dumps({"source": text}).encode()
              request = urllib.request.Request(
                  base + "/api/source", data=body, method="POST",
                  headers={"Content-Type": "application/json"})
              with urllib.request.urlopen(request) as response:
                  return response.status, json.loads(response.read())

          def wait_for(predicate, timeout=8):
              deadline = time.monotonic() + timeout
              while time.monotonic() < deadline:
                  state = json.loads(get("/api/state")[1])
                  if predicate(state):
                      return state
                  time.sleep(0.04)
              raise AssertionError("live editor state did not converge")

          try:
              status, page = get("/")
              assert status == 200 and b"fine rainfall" in page
              hostile = urllib.request.Request(
                  base + "/api/source", data=b'{"source":"bad"}', method="POST",
                  headers={"Content-Type": "application/json", "Origin": "https://evil.invalid"})
              try:
                  urllib.request.urlopen(hostile)
                  raise AssertionError("accepted a cross-origin browser edit")
              except urllib.error.HTTPError as error:
                  assert error.code == 403
              wrong_type = urllib.request.Request(
                  base + "/api/source", data=b'{"source":"bad"}', method="POST",
                  headers={"Content-Type": "text/plain"})
              try:
                  urllib.request.urlopen(wrong_type)
                  raise AssertionError("accepted a non-JSON editor request")
              except urllib.error.HTTPError as error:
                  assert error.code == 415
              state0 = wait_for(lambda s: s["generations"][s["current_generation"]]["status"]
                                          == "admitted")
              assert {a["status"] for a in state0["annotations"]} == {"current"}
              original = state0["display_source"]
              status, action1 = post("// first edit λ\n" + original)
              assert status == 202 and action1["display_snapshot"]["revision"] == 1
              stale1 = json.loads(get("/api/state")[1])
              assert stale1["generations"][stale1["current_generation"]]["status"] == "requested"
              assert {a["status"] for a in stale1["annotations"]} == {"transported"}
              status, action2 = post("// second edit\n// first edit λ\n" + original)
              assert status == 202 and action2["display_snapshot"]["revision"] == 2
              final = wait_for(lambda s: s["display_snapshot"]["revision"] == 2 and
                                         s["generations"][s["current_generation"]]["status"]
                                         == "admitted")
              assert final["display_source"].startswith("// second edit\n// first edit λ\n")
              assert {a["status"] for a in final["annotations"]} == {"current"}
              assert final["generations"][action1["generation"]]["status"] == "discarded"
              assert final["generations"][action2["generation"]]["status"] == "admitted"
          finally:
              server.shutdown()
              server.server_close()
              thread.join(timeout=2)
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
          assert any('_d_node_node(numeral("7",_s_Int),_d_leaf_leaf,_d_leaf_leaf)' in
                     event["data"]["text"]
                     for event in terms)
          PY
          runHook postInstallCheck
        '';
      };
      apps.${system}.live = {
        type = "app";
        program = "${self.packages.${system}.default}/bin/fine-rain-live";
        meta.description = "Local browser editor for live Fine Rainfall evidence";
      };
    };
}
