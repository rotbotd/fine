{
  description = "Fine, a two-level proof-search language inside a Z3 soft fork";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
      wasmSource = pkgs.lib.cleanSourceWith {
        src = self;
        filter = path: type:
          let relative = pkgs.lib.removePrefix "${toString self}/" (toString path);
          in relative == "CMakeLists.txt"
             || relative == "z3.pc.cmake.in"
             || relative == "cmake" || pkgs.lib.hasPrefix "cmake/" relative
             || relative == "examples" || pkgs.lib.hasPrefix "examples/" relative
             || relative == "scripts" || pkgs.lib.hasPrefix "scripts/" relative
             || relative == "src" || pkgs.lib.hasPrefix "src/" relative;
      };
      playgroundServer = pkgs.writeShellApplication {
        name = "fine-playground-service";
        runtimeInputs = [ pkgs.nodejs_22 ];
        text = ''
          exec ${self.packages.${system}.playground}/server/node_modules/.bin/vite \
            preview \
            --config ${self.packages.${system}.playground}/server/vite.config.js \
            --configLoader native \
            --outDir ${self.packages.${system}.playground}/site \
            --host 127.0.0.1 \
            --port "''${PORT:-4174}" \
            --strictPort
        '';
      };
    in {
      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [ cmake ninja python3 clang pkg-config git emscripten nodejs_22 ];
      };

      packages.${system} = {
      default = pkgs.stdenv.mkDerivation {
        pname = "fine";
        version = "0.1.0";
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

          live_lift="$($out/bin/fine live-lift-probe)"
          echo "$live_lift"
          grep -F "spacer-completed-while-lifter-blocked: true" <<<"$live_lift"
          grep -F "producer-completed-while-lifter-blocked: true" <<<"$live_lift"
          grep -F "observed: 12" <<<"$live_lift"
          grep -F "latest-observed: 11" <<<"$live_lift"
          grep -F "latest-published: 11" <<<"$live_lift"

          ${pkgs.python3}/bin/python "$src/fine/check_document_examples.py" "$src"

          demo_output="$($out/bin/fine run --proof-selector z3 \
            "$src/fine/fixtures/playground-demo.fine")"
          echo "$demo_output"
          grep -F 'verified proof function: plus_shift' <<<"$demo_output"
          grep -F 'filled proof hole: composed <- trans(left, middle, right) using [first = p, second = q] (Z3 datatype model)' \
            <<<"$demo_output"
          demo_materialized="$(mktemp)"
          $out/bin/fine materialize --proof-selector z3 \
            "$src/fine/fixtures/playground-demo.fine" > "$demo_materialized"
          cmp "$src/fine/fixtures/playground-demo-materialized.fine" "$demo_materialized"

          for source in \
            "$src/fine/fixtures/cst-roundtrip-ugly.fine" \
            "$src/fine/fixtures/identity-coeffect.fine" \
            "$src/fine/fixtures/playground-demo.fine" \
            "$src/fine/fixtures/runtime-enum.fine" \
            "$src/fine/fixtures/proof-inductive-match.fine" \
            "$src/fine/fixtures/proof-inductive-holes.fine" \
            "$src/fine/fixtures/top-level-declarations.fine"; do
            concrete_roundtrip="$(mktemp)"
            $out/bin/fine roundtrip "$source" > "$concrete_roundtrip"
            cmp "$source" "$concrete_roundtrip"
          done

          crlf_source="$(mktemp)"
          crlf_roundtrip="$(mktemp)"
          sed 's/$/\r/' "$src/fine/fixtures/cst-roundtrip-ugly.fine" > "$crlf_source"
          $out/bin/fine roundtrip "$crlf_source" > "$crlf_roundtrip"
          cmp "$crlf_source" "$crlf_roundtrip"

          eof_source="$(mktemp)"
          eof_roundtrip="$(mktemp)"
          eof_materialized="$(mktemp)"
          printf '%s' 'run eof_comment { assert true == true; } // no newline' > "$eof_source"
          $out/bin/fine roundtrip "$eof_source" > "$eof_roundtrip"
          cmp "$eof_source" "$eof_roundtrip"
          $out/bin/fine materialize --output "$eof_materialized" "$eof_source"
          cmp "$eof_source" "$eof_materialized"

          ugly_materialized="$(mktemp)"
          $out/bin/fine materialize "$src/fine/fixtures/cst-roundtrip-ugly.fine" > "$ugly_materialized"
          cmp "$src/fine/fixtures/cst-roundtrip-ugly-materialized.fine" "$ugly_materialized"

          definitions_output="$($out/bin/fine run \
            "$src/fine/fixtures/top-level-declarations.fine")"
          echo "$definitions_output"
          grep -F 'verified proof function: even_pred' <<<"$definitions_output"
          grep -F 'verified proof function: plus_shift' <<<"$definitions_output"
          grep -F 'resolved coeffect: plus_shift.evidence <- rest (lexical search)' <<<"$definitions_output"
          grep -F 'verified definitions' <<<"$definitions_output"
          definitions_materialized="$(mktemp)"
          $out/bin/fine materialize "$src/fine/fixtures/top-level-declarations.fine" \
            > "$definitions_materialized"
          cmp "$src/fine/fixtures/top-level-declarations-materialized.fine" \
            "$definitions_materialized"
          definitions_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/top-level-declarations.fine" \
            > "$definitions_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/top-level-declarations.fine" "$definitions_rain"
          grep -F '"operation":"proof-core.document.close"' "$definitions_rain"

          duplicate_runs="$(mktemp)"
          printf '%s\n' 'run first {}' 'run second {}' > "$duplicate_runs"
          duplicate_runs_error="$(mktemp)"
          if $out/bin/fine run "$duplicate_runs" > "$duplicate_runs_error" 2>&1; then
            echo 'document unexpectedly accepted two executable run blocks' >&2
            exit 1
          fi
          grep -F 'a document may contain at most one `run` declaration' \
            "$duplicate_runs_error"

          output="$($out/bin/fine run "$src/fine/fixtures/identity-coeffect.fine")"
          echo "$output"
          grep -F "verified function: replace" <<<"$output"
          grep -F "formed proof: p : Id(Int, x, y) (virtual)" <<<"$output"
          grep -F "resolved coeffect: replace.same <- p (lexical search)" <<<"$output"
          grep -F "runtime-proof-values: 0 (unrepresentable)" <<<"$output"

          enum_output="$($out/bin/fine run "$src/fine/fixtures/runtime-enum.fine")"
          echo "$enum_output"
          grep -F "declared enum: Nat (2 constructors)" <<<"$enum_output"
          grep -F "verified function: predecessor" <<<"$enum_output"
          grep -F "verified function: rebuild" <<<"$enum_output"
          grep -F "formed proof: same : Id(Nat, one, one) (virtual)" <<<"$enum_output"
          grep -F "runtime-value-kinds: Int, Bool, Nat" <<<"$enum_output"

          nonexhaustive_enum="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-nonexhaustive-enum-match.fine" \
              >"$nonexhaustive_enum" 2>&1; then
            echo "non-exhaustive enum match unexpectedly passed" >&2
            exit 1
          fi
          grep -F 'non-exhaustive match: missing `succ`' "$nonexhaustive_enum"

          wrong_enum_field="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-enum-field-type.fine" \
              >"$wrong_enum_field" 2>&1; then
            echo "wrong enum constructor field unexpectedly passed" >&2
            exit 1
          fi
          grep -F 'field 0 of `succ` has the wrong value type' "$wrong_enum_field"

          inductive_output="$($out/bin/fine run "$src/fine/fixtures/proof-inductive-even.fine")"
          echo "$inductive_output"
          grep -F "declared proof inductive: Even (2 constructors, static)" <<<"$inductive_output"
          grep -F "formed proof: zero_even : Even(zero) (virtual)" <<<"$inductive_output"
          grep -F "resolved coeffect: even_next.prior <- zero_even (lexical search)" <<<"$inductive_output"
          grep -F "formed proof: two_even : Even(succ(succ(zero))) (virtual)" <<<"$inductive_output"
          grep -F "runtime-proof-values: 0 (unrepresentable)" <<<"$inductive_output"

          inductive_materialized="$(mktemp)"
          $out/bin/fine materialize "$src/fine/fixtures/proof-inductive-even.fine" \
            > "$inductive_materialized"
          grep -F 'even_next(zero) using [prior = zero_even]' "$inductive_materialized"
          $out/bin/fine run "$inductive_materialized"

          proof_match_output="$($out/bin/fine run "$src/fine/fixtures/proof-inductive-match.fine")"
          echo "$proof_match_output"
          grep -F "declared proof inductive: Never (0 constructors, static)" <<<"$proof_match_output"
          grep -F "verified proof function: expose_even" <<<"$proof_match_output"
          grep -F "verified proof function: absurd" <<<"$proof_match_output"
          grep -F "verified proof function: impossible_even" <<<"$proof_match_output"
          grep -F "verified proof function: contradictory_indices" <<<"$proof_match_output"
          grep -F "formed proof: zero_shape : EvenShape(zero) (virtual)" <<<"$proof_match_output"

          proof_match_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/proof-inductive-match.fine" > "$proof_match_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/proof-inductive-match.fine" "$proof_match_rain"

          induction_output="$($out/bin/fine run "$src/fine/fixtures/proof-inductive-induction.fine")"
          echo "$induction_output"
          grep -F "verified proof function: rebuild" <<<"$induction_output"
          grep -F "formed proof: rebuilt : Rebuilt(zero) (virtual)" <<<"$induction_output"

          induction_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/proof-inductive-induction.fine" > "$induction_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/proof-inductive-induction.fine" "$induction_rain"
          grep -F '"operation":"proof.induction.hypothesis.use"' "$induction_rain"
          ${pkgs.python3}/bin/python - "$induction_rain" <<'PY'
          import json
          import sys

          with open(sys.argv[1], encoding="utf-8") as stream:
              events = [json.loads(line) for line in stream]
          branches = [event["data"] for event in events
                      if event["operation"] == "proof.inductive.match.branch"]
          recursive = next(branch for branch in branches
                           if branch["constructor"] == "even_next")
          assert recursive["value_binders"] == ["previous"], recursive
          assert recursive["proof_binders"] == [], recursive
          assert recursive["coeffect_binders"] == ["prior"], recursive
          rebuilt = next(event["data"] for event in events
                         if event["operation"] == "proof.inductive.constructor.apply"
                         and event["data"]["constructor"] == "rebuilt_next")
          assert rebuilt["value_arguments"] == ["previous"], rebuilt
          assert rebuilt["proof_arguments"] == [
              "rebuild(previous) using [evidence = prior]"
          ], rebuilt
          assert rebuilt["coeffects"] == [], rebuilt
          PY

          branching_output="$($out/bin/fine run "$src/fine/fixtures/proof-inductive-branching-induction.fine")"
          echo "$branching_output"
          grep -F "formed proof: result : Rebuilt(node(leaf, leaf)) (virtual)" <<<"$branching_output"

          branching_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/proof-inductive-branching-induction.fine" \
            > "$branching_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/proof-inductive-branching-induction.fine" "$branching_rain"
          ${pkgs.python3}/bin/python - "$branching_rain" <<'PY'
          import json
          import sys

          with open(sys.argv[1], encoding="utf-8") as stream:
              events = [json.loads(line) for line in stream]
          uses = [event["data"] for event in events
                  if event["operation"] == "proof.induction.hypothesis.use"]
          assert len(uses) == 2, uses
          assert {use["recursive_evidence"] for use in uses} == {"left_grows", "right_grows"}, uses
          assert {use["parent_evidence"] for use in uses} == {"evidence"}, uses
          assert {use["induction_parameter"] for use in uses} == {"evidence"}, uses
          PY

          inductive_hole_output="$($out/bin/fine run "$src/fine/fixtures/proof-inductive-holes.fine")"
          echo "$inductive_hole_output"
          grep -F "filled proof hole: rebuild.result.even_next.prior <- rebuild(previous) using [evidence = prior] (typed search)" \
            <<<"$inductive_hole_output"
          grep -F "filled proof hole: copied <- zero_even (typed search)" <<<"$inductive_hole_output"

          inductive_hole_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/proof-inductive-holes.fine" > "$inductive_hole_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/proof-inductive-holes.fine" "$inductive_hole_rain"
          ${pkgs.python3}/bin/python - "$inductive_hole_rain" <<'PY'
          import json
          import sys

          with open(sys.argv[1], encoding="utf-8") as stream:
              events = [json.loads(line) for line in stream]
          candidates = [event["data"] for event in events
                        if event["operation"] == "proof.search.candidate"]
          assert [(item["production"], item["body"]) for item in candidates] == [
              ("induction-hypothesis", "rebuild(previous) using [evidence = prior]"),
              ("exact-local", "zero_even"),
          ], candidates
          assert candidates[0]["recursive_evidence"] == "prior", candidates
          assert candidates[0]["parent_evidence"] == "evidence", candidates
          PY

          inductive_hole_materialized="$(mktemp)"
          $out/bin/fine materialize "$src/fine/fixtures/proof-inductive-holes.fine" \
            > "$inductive_hole_materialized"
          cmp "$src/fine/fixtures/proof-inductive-holes-materialized.fine" \
            "$inductive_hole_materialized"
          $out/bin/fine run "$inductive_hole_materialized"

          empty_inductive_hole="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-empty-inductive-hole.fine" \
              >"$empty_inductive_hole" 2>&1; then
            echo "empty indexed proof grammar unexpectedly passed" >&2
            exit 1
          fi
          grep -F 'has no candidate in grammar [exact-local, induction-hypothesis]' \
            "$empty_inductive_hole"

          unsupported_inductive_selector="$(mktemp)"
          if $out/bin/fine run --proof-selector z3 "$src/fine/fixtures/proof-inductive-holes.fine" \
              >"$unsupported_inductive_selector" 2>&1; then
            echo "indexed hole silently entered the identity Z3 selector" >&2
            exit 1
          fi
          grep -F 'Z3 proof selector does not yet cover indexed proof holes' \
            "$unsupported_inductive_selector"

          nondecreasing_recursion="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-nondecreasing-proof-recursion.fine" \
              >"$nondecreasing_recursion" 2>&1; then
            echo "nondecreasing proof recursion unexpectedly passed" >&2
            exit 1
          fi
          grep -F 'does not descend through a proof field of induction parameter `evidence`' \
            "$nondecreasing_recursion"

          unannotated_recursion="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-recursion-without-inducts.fine" \
              >"$unannotated_recursion" 2>&1; then
            echo "unannotated proof recursion unexpectedly passed" >&2
            exit 1
          fi
          grep -F 'unknown proof constructor `anything`' "$unannotated_recursion"

          nonexhaustive_proof_match="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-nonexhaustive-proof-match.fine" \
              >"$nonexhaustive_proof_match" 2>&1; then
            echo "non-exhaustive proof match unexpectedly passed" >&2
            exit 1
          fi
          grep -F 'non-exhaustive proof match: missing `even_next`' "$nonexhaustive_proof_match"

          unreachable_proof_match="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-unreachable-proof-match-arm.fine" \
              >"$unreachable_proof_match" 2>&1; then
            echo "unreachable proof match arm unexpectedly passed" >&2
            exit 1
          fi
          grep -F 'unreachable proof match arm `even_zero` must be omitted' "$unreachable_proof_match"

          legacy_needs="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-needs-keyword.fine" \
              >"$legacy_needs" 2>&1; then
            echo "legacy needs keyword unexpectedly passed" >&2
            exit 1
          fi
          grep -F 'expected `{`' "$legacy_needs"

          bad_inductive_index="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-proof-inductive-index.fine" \
              >"$bad_inductive_index" 2>&1; then
            echo "wrong proof-constructor result index unexpectedly passed" >&2
            exit 1
          fi
          grep -F 'proof constructor application `even_zero()` has the wrong result type' \
            "$bad_inductive_index"

          bad_inductive_premise="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-proof-inductive-premise.fine" \
              >"$bad_inductive_premise" 2>&1; then
            echo "wrong proof-constructor premise unexpectedly passed" >&2
            exit 1
          fi
          grep -F 'proof `zero_even` has the wrong inductive type' "$bad_inductive_premise"

          bad_explicit_constructor_proof="$(mktemp)"
          if $out/bin/fine run \
              "$src/fine/fixtures/reject-explicit-proof-constructor-parameter.fine" \
              >"$bad_explicit_constructor_proof" 2>&1; then
            echo "wrong explicit proof-constructor parameter unexpectedly passed" >&2
            exit 1
          fi
          grep -F 'proof `zero_even` has the wrong inductive type' \
            "$bad_explicit_constructor_proof"

          leaked_constructor="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-proof-constructor-as-value.fine" \
              >"$leaked_constructor" 2>&1; then
            echo "proof constructor unexpectedly entered runtime value code" >&2
            exit 1
          fi
          grep -F 'proof constructor `even_zero` cannot be called from a runtime value expression' \
            "$leaked_constructor"

          materialized="$(mktemp)"
          $out/bin/fine materialize "$src/fine/fixtures/identity-coeffect.fine" > "$materialized"
          cmp "$src/fine/fixtures/identity-coeffect-materialized.fine" "$materialized"
          materialized_output="$($out/bin/fine run "$materialized")"
          grep -F "resolved coeffect: replace.same <- p (explicit)" <<<"$materialized_output"

          hole_output="$($out/bin/fine run "$src/fine/fixtures/identity-holes.fine")"
          echo "$hole_output"
          grep -F "filled proof hole: self <- refl(x) (typed search)" <<<"$hole_output"
          grep -F "filled proof hole: copied <- self (typed search)" <<<"$hole_output"
          grep -F "resolved coeffect: hold.same <- self (lexical search)" <<<"$hole_output"

          holes_materialized="$(mktemp)"
          $out/bin/fine materialize "$src/fine/fixtures/identity-holes.fine" \
            > "$holes_materialized"
          cmp "$src/fine/fixtures/identity-holes-materialized.fine" \
            "$holes_materialized"
          holes_materialized_output="$($out/bin/fine run "$holes_materialized")"
          grep -F "formed proof: self : Id(Int, x, x) (virtual)" \
            <<<"$holes_materialized_output"
          grep -F "resolved coeffect: hold.same <- self (explicit)" \
            <<<"$holes_materialized_output"
          if grep -F "filled proof hole:" <<<"$holes_materialized_output"; then
            echo "materialized identity proof unexpectedly searched again" >&2
            exit 1
          fi

          symmetry_output="$($out/bin/fine run "$src/fine/fixtures/identity-symmetry.fine")"
          echo "$symmetry_output"
          grep -F "verified proof function: bool_eta" <<<"$symmetry_output"
          grep -F "verified proof function: symm" <<<"$symmetry_output"
          grep -F "filled proof hole: reversed <- symm(x, x == true) using [given = p] (typed search)" \
            <<<"$symmetry_output"

          symmetry_materialized="$(mktemp)"
          $out/bin/fine materialize "$src/fine/fixtures/identity-symmetry.fine" \
            > "$symmetry_materialized"
          cmp "$src/fine/fixtures/identity-symmetry-materialized.fine" \
            "$symmetry_materialized"
          symmetry_rerun="$($out/bin/fine run "$symmetry_materialized")"
          grep -F "formed proof: reversed : Id(Bool, x == true, x) (virtual)" \
            <<<"$symmetry_rerun"
          if grep -F "filled proof hole:" <<<"$symmetry_rerun"; then
            echo "materialized symmetry proof unexpectedly searched again" >&2
            exit 1
          fi

          transitivity_output="$($out/bin/fine run "$src/fine/fixtures/identity-transitivity.fine")"
          echo "$transitivity_output"
          grep -F "verified proof function: trans" <<<"$transitivity_output"
          grep -F "filled proof hole: composed <- trans(left, middle, right) using [first = p, second = q] (typed search)" \
            <<<"$transitivity_output"

          transitivity_materialized="$(mktemp)"
          $out/bin/fine materialize "$src/fine/fixtures/identity-transitivity.fine" \
            > "$transitivity_materialized"
          cmp "$src/fine/fixtures/identity-transitivity-materialized.fine" \
            "$transitivity_materialized"
          transitivity_rerun="$($out/bin/fine run "$transitivity_materialized")"
          grep -F "formed proof: composed : Id(Bool, left, right) (virtual)" \
            <<<"$transitivity_rerun"
          if grep -F "filled proof hole:" <<<"$transitivity_rerun"; then
            echo "materialized transitivity proof unexpectedly searched again" >&2
            exit 1
          fi

          z3_transitivity_output="$($out/bin/fine run --proof-selector z3 \
            "$src/fine/fixtures/identity-transitivity.fine")"
          echo "$z3_transitivity_output"
          grep -F "filled proof hole: composed <- trans(left, middle, right) using [first = p, second = q] (Z3 datatype model)" \
            <<<"$z3_transitivity_output"

          z3_transitivity_materialized="$(mktemp)"
          $out/bin/fine materialize --proof-selector z3 \
            "$src/fine/fixtures/identity-transitivity.fine" \
            > "$z3_transitivity_materialized"
          cmp "$src/fine/fixtures/identity-transitivity-materialized.fine" \
            "$z3_transitivity_materialized"

          checkpoint="$(mktemp)"
          checkpoint_rain="$(mktemp)"
          $out/bin/fine checkpoint --proof-budget 2 --output "$checkpoint" \
            --rain-output "$checkpoint_rain" \
            "$src/fine/fixtures/identity-checkpoint.fine"
          cmp "$src/fine/fixtures/identity-checkpoint-materialized.fine" "$checkpoint"

          checkpoint_complete="$(mktemp)"
          $out/bin/fine checkpoint --proof-budget 2 "$checkpoint" > "$checkpoint_complete"
          cmp "$src/fine/fixtures/identity-checkpoint-complete.fine" "$checkpoint_complete"
          $out/bin/fine run "$checkpoint_complete" | grep -F \
            'verified assertion: identity_checkpoint.0'

          shallow_checkpoint="$(mktemp)"
          $out/bin/fine checkpoint --proof-budget 1 \
            "$src/fine/fixtures/identity-checkpoint.fine" > "$shallow_checkpoint"
          cmp "$src/fine/fixtures/identity-checkpoint.fine" "$shallow_checkpoint"

          live_checkpoint="$(mktemp)"
          $out/bin/fine live-checkpoint --proof-limit 2 --output "$live_checkpoint" \
            "$src/fine/fixtures/identity-checkpoint.fine"
          cmp "$src/fine/fixtures/identity-checkpoint-materialized.fine" "$live_checkpoint"
          live_complete="$(mktemp)"
          live_complete_rain="$(mktemp)"
          $out/bin/fine live-checkpoint --output "$live_complete" --rain-output "$live_complete_rain" \
            "$src/fine/fixtures/identity-checkpoint.fine"
          if grep -F '= ?;' "$live_complete"; then
            echo "live proof search stopped before closing its source term" >&2
            exit 1
          fi
          $out/bin/fine run "$live_complete" | grep -F \
            'verified assertion: identity_checkpoint.0'
          ${pkgs.python3}/bin/python ${./fine/rainfall_replay.py} \
            "$src/fine/fixtures/identity-checkpoint.fine" "$live_complete_rain"
          ${pkgs.python3}/bin/python - "$live_complete_rain" <<'PY'
          import json
          import sys

          events = [json.loads(line) for line in open(sys.argv[1])]
          epochs = [event["data"] for event in events
                    if event["operation"] == "proof.search.live.model"]
          assert [epoch["budget"] for epoch in epochs] == [1, 2, 3, 4]
          assert [epoch["grammar_reset"] for epoch in epochs] == [True, True, True, False]
          assert [epoch["grammar_states_reused"] for epoch in epochs] == [0, 0, 0, 120]
          assert epochs[-1]["grammar_states"] == 199
          PY

          for direct_budget in 1 2 3 4; do
            reference_epoch="$(mktemp)"
            direct_epoch="$(mktemp)"
            $out/bin/fine checkpoint --proof-budget "$direct_budget" \
              --output "$reference_epoch" "$src/fine/fixtures/identity-checkpoint.fine"
            $out/bin/fine live-checkpoint --proof-limit "$direct_budget" \
              --output "$direct_epoch" "$src/fine/fixtures/identity-checkpoint.fine"
            cmp "$reference_epoch" "$direct_epoch"
          done

          two_live_holes_output="$(mktemp)"
          two_live_holes_rain="$(mktemp)"
          $out/bin/fine live-checkpoint --output "$two_live_holes_output" \
            --rain-output "$two_live_holes_rain" \
            "$src/fine/fixtures/identity-checkpoint-multi.fine"
          cmp "$src/fine/fixtures/identity-checkpoint-multi-materialized.fine" \
            "$two_live_holes_output"
          $out/bin/fine run "$two_live_holes_output" | grep -F \
            'verified assertion: identity_checkpoint.0'
          ${pkgs.python3}/bin/python ${./fine/rainfall_replay.py} \
            "$src/fine/fixtures/identity-checkpoint-multi.fine" "$two_live_holes_rain"
          two_live_holes_interrupted="$(mktemp)"
          $out/bin/fine live-checkpoint --proof-limit 2 --output "$two_live_holes_interrupted" \
            "$src/fine/fixtures/identity-checkpoint-multi.fine"
          cmp "$src/fine/fixtures/identity-checkpoint-multi-interrupted.fine" \
            "$two_live_holes_interrupted"

          open_checkpoint_run="$(mktemp)"
          if $out/bin/fine run "$checkpoint" > "$open_checkpoint_run" 2>&1; then
            echo "ordinary run accepted a checkpoint with a residual proof hole" >&2
            exit 1
          fi
          grep -F 'nested proof holes are not admitted' "$open_checkpoint_run"

          congruence_output="$($out/bin/fine run --proof-selector z3 \
            "$src/fine/fixtures/identity-congruence.fine")"
          echo "$congruence_output"
          grep -F 'filled proof hole: lifted <- truth_congruence(x == false, (x == true) == false) using [same = p] (Z3 datatype model)' \
            <<<"$congruence_output"

          congruence_materialized="$(mktemp)"
          $out/bin/fine materialize --proof-selector z3 \
            "$src/fine/fixtures/identity-congruence.fine" \
            > "$congruence_materialized"
          cmp "$src/fine/fixtures/identity-congruence-materialized.fine" \
            "$congruence_materialized"
          congruence_rerun="$($out/bin/fine run "$congruence_materialized")"
          grep -F 'formed proof: lifted : Id(Bool, (x == false) == true, ((x == true) == false) == true) (virtual)' \
            <<<"$congruence_rerun"
          if grep -F "filled proof hole:" <<<"$congruence_rerun"; then
            echo "materialized congruence proof unexpectedly searched again" >&2
            exit 1
          fi

          transitivity_gap="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-transitivity-gap.fine" \
              >"$transitivity_gap" 2>&1; then
            echo "transitivity search accepted a missing second child" >&2
            exit 1
          fi
          grep -F 'proof hole `impossible` has no well-typed candidate in bounded grammar' \
            "$transitivity_gap"

          empty_hole="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-empty-proof-hole.fine" \
              >"$empty_hole" 2>&1; then
            echo "empty proof-hole grammar unexpectedly passed" >&2
            exit 1
          fi
          grep -F 'proof hole `impossible` has no well-typed candidate in bounded grammar [exact-local, refl, proof-application]' \
            "$empty_hole"

          unjustified_proof_function="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-unjustified-proof-function.fine" \
              >"$unjustified_proof_function" 2>&1; then
            echo "unjustified proof function unexpectedly passed" >&2
            exit 1
          fi
          grep -F 'proof function `lie` does not establish its result from its proof parameters' \
            "$unjustified_proof_function"

          proof_function_value="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-proof-function-as-value.fine" \
              >"$proof_function_value" 2>&1; then
            echo "proof function unexpectedly entered runtime value code" >&2
            exit 1
          fi
          grep -F 'proof function `bool_eta` cannot be called from a runtime value expression' \
            "$proof_function_value"

          cyclic_proof_search="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-cyclic-proof-search.fine" \
              >"$cyclic_proof_search" 2>&1; then
            echo "cyclic proof search unexpectedly escaped its bound" >&2
            exit 1
          fi
          grep -F 'proof hole `impossible` has no well-typed candidate in bounded grammar' \
            "$cyclic_proof_search"

          missing="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-missing-coeffect.fine" >"$missing" 2>&1; then
            echo "missing coeffect unexpectedly passed" >&2
            exit 1
          fi
          grep -F 'missing caller proof for coeffect `replace.same' "$missing"

          leaked="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-proof-as-value.fine" >"$leaked" 2>&1; then
            echo "proof entered a runtime value unexpectedly" >&2
            exit 1
          fi
          grep -F 'proof `p` cannot inhabit a runtime value' "$leaked"

          unjustified="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-unjustified-function.fine" >"$unjustified" 2>&1; then
            echo "unjustified function unexpectedly verified" >&2
            exit 1
          fi
          grep -F 'does not satisfy its guarantees under declared coeffects' "$unjustified"
          grep -F 'counterexample replace {' "$unjustified"
          grep -F '  left: Int = ' "$unjustified"
          grep -F '  right: Int = ' "$unjustified"
          grep -F '  result: Int = ' "$unjustified"
          grep -F 'parse(print(lift(values))): exact ast identity' "$unjustified"

          enum_counterexample="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-enum-function-counterexample.fine" \
              >"$enum_counterexample" 2>&1; then
            echo "invalid enum function unexpectedly verified" >&2
            exit 1
          fi
          grep -F 'counterexample erase {' "$enum_counterexample"
          grep -F '  value: Nat = succ(zero);' "$enum_counterexample"
          grep -F '  result: Nat = zero;' "$enum_counterexample"
          grep -F 'parse(print(lift(values))): exact ast identity' "$enum_counterexample"

          negative_counterexample="$(mktemp)"
          if $out/bin/fine run "$src/fine/fixtures/reject-negative-function-counterexample.fine" \
              >"$negative_counterexample" 2>&1; then
            echo "invalid negative-input function unexpectedly verified" >&2
            exit 1
          fi
          grep -F 'counterexample negative_is_zero takes [negative] {' "$negative_counterexample"
          grep -F '  value: Int = -1;' "$negative_counterexample"
          grep -F '  result: Int = -1;' "$negative_counterexample"

          counterexample_rain="$(mktemp)"
          if $out/bin/fine rain "$src/fine/fixtures/reject-enum-function-counterexample.fine" \
              >"$counterexample_rain" 2>/dev/null; then
            echo "Rainfall enum counterexample unexpectedly verified" >&2
            exit 1
          fi
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/reject-enum-function-counterexample.fine" "$counterexample_rain"
          grep -F '"operation":"fine.counterexample.witness"' "$counterexample_rain"
          grep -F '"operation":"function.counterexample.close"' "$counterexample_rain"
          counterexample_mutated="$(mktemp)"
          ${pkgs.python3}/bin/python - "$counterexample_rain" "$counterexample_mutated" <<'PY'
          import json
          import sys

          with open(sys.argv[1], encoding="utf-8") as source, \
               open(sys.argv[2], "w", encoding="utf-8") as target:
              for line in source:
                  event = json.loads(line)
                  if event["operation"] == "fine.counterexample.verify":
                      event["data"]["original_guarantee_rechecked"] = False
                  target.write(json.dumps(event, separators=(",", ":")) + "\n")
          PY
          if ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
              "$src/fine/fixtures/reject-enum-function-counterexample.fine" \
              "$counterexample_mutated" >/dev/null 2>&1; then
            echo "mutated counterexample verification unexpectedly replayed" >&2
            exit 1
          fi

          rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/identity-coeffect.fine" > "$rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/identity-coeffect.fine" "$rain"
          ${pkgs.python3}/bin/python - "$rain" <<'PY'
          import json, pathlib, sys
          events = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
          operations = [event["operation"] for event in events]
          for required in [
              "proof.erasure.boundary",
              "coeffect.demand.declare",
              "proof.identity.form",
              "proof.context.absorb",
              "coeffect.demand.instantiate",
              "coeffect.resolve",
              "coeffect.use",
              "proof-core.run.close",
          ]:
              assert required in operations, required
          boundary = next(e for e in events if e["operation"] == "proof.erasure.boundary")
          assert boundary["data"]["runtime_value_kinds"] == ["Int", "Bool"]
          assert boundary["data"]["runtime_proof_variants"] == 0
          formed = next(e for e in events if e["operation"] == "proof.identity.form")
          assert formed["data"]["runtime_value_created"] is False
          resolved = next(e for e in events if e["operation"] == "coeffect.resolve")
          assert resolved["data"]["proof"] == "p"
          assert resolved["data"]["mode"] == "exact-local"
          used = next(e for e in events if e["operation"] == "coeffect.use")
          assert used["data"]["runtime_argument_created"] is False
          closed = events[-1]
          assert closed["operation"] == "proof-core.run.close"
          assert closed["data"]["runtime_proof_values"] == 0
          declarations = [e for e in events if e["operation"] == "term.declare"]
          validations = [e for e in events if e["operation"] == "term.lift.validate"]
          assert len(declarations) == len(validations) and declarations
          PY

          inductive_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/proof-inductive-even.fine" > "$inductive_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/proof-inductive-even.fine" "$inductive_rain"
          ${pkgs.python3}/bin/python - "$inductive_rain" <<'PY'
          import json, pathlib, sys
          events = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
          declaration = next(e for e in events if e["operation"] == "proof.inductive.declare")
          assert declaration["data"] == {
              "family": "Even", "indices": 1, "constructors": 2,
              "runtime_datatype_created": False,
          }
          applications = [e for e in events if e["operation"] == "proof.inductive.constructor.apply"]
          assert [e["data"]["constructor"] for e in applications] == ["even_zero", "even_next"]
          assert applications[1]["data"]["value_arguments"] == ["zero"]
          assert applications[1]["data"]["proof_arguments"] == []
          assert applications[1]["data"]["coeffects"] == ["zero_even"]
          constructor_resolution = next(
              e for e in events
              if e["operation"] == "coeffect.resolve"
              and e["data"].get("proof_constructor") is True
          )
          assert constructor_resolution["data"]["constructor"] == "even_next"
          assert constructor_resolution["data"]["coeffect"] == "prior"
          assert constructor_resolution["data"]["proof"] == "zero_even"
          assert constructor_resolution["data"]["proof_identity_observable"] is False
          assert all(e["data"]["runtime_value_created"] is False for e in applications)
          forms = [e for e in events if e["operation"] == "proof.inductive.form"]
          assert [e["data"]["proof_type"] for e in forms] == [
              "Even(zero)", "Even(succ(succ(zero)))"
          ]
          assert events[-1]["data"]["runtime_proof_values"] == 0
          PY

          hole_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/identity-holes.fine" > "$hole_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/identity-holes.fine" "$hole_rain"
          ${pkgs.python3}/bin/python - "$hole_rain" <<'PY'
          import json, pathlib, sys
          events = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
          holes = [e for e in events if e["operation"] == "proof.search.open"]
          candidates = [e for e in events if e["operation"] == "proof.search.candidate"]
          selections = [e for e in events if e["operation"] == "proof.search.select"]
          closes = [e for e in events if e["operation"] == "proof.search.close"]
          assert len(holes) == len(selections) == len(closes) == 2
          assert all(e["producer"]["component"] == "fine.typed-proof-search"
                     for e in holes + candidates + selections + closes)
          assert [e["data"]["body"] for e in candidates] == ["refl(x)", "self", "refl(x)"]
          assert all(e["data"]["exact_type"] is True for e in candidates)
          assert all(e["data"]["runtime_value_created"] is False for e in candidates)
          assert not any(e["data"]["body"] == "other" for e in candidates)
          assert closes[0]["data"]["residual_candidates"] == []
          assert len(closes[1]["data"]["residual_candidates"]) == 1
          forms = [e for e in events if e["operation"] == "proof.identity.form"]
          assert [e["data"]["formation"] for e in forms] == [
              "refl", "search:refl", "search:exact-local:self"
          ]
          closed = events[-1]
          assert closed["operation"] == "proof-core.run.close"
          assert closed["data"]["proof_holes_filled"] == 2
          assert closed["data"]["runtime_proof_values"] == 0
          PY

          symmetry_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/identity-symmetry.fine" \
            > "$symmetry_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/identity-symmetry.fine" "$symmetry_rain"
          ${pkgs.python3}/bin/python - "$symmetry_rain" <<'PY'
          import json, pathlib, sys
          events = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
          verified = [e for e in events if e["operation"] == "proof.function.verify"]
          applied = [e for e in events if e["operation"] == "proof.function.apply"]
          candidates = [e for e in events if e["operation"] == "proof.search.candidate"]
          assert [e["data"]["function"] for e in verified] == ["bool_eta", "symm"]
          assert applied[0]["data"]["body"] == "bool_eta(x)"
          assert [e["data"]["body"] for e in candidates] == [
              "symm(x, x == true) using [given = p]",
              "symm(x, x == true) using [given = bool_eta(x)]",
          ]
          assert all(e["data"]["production"] == "proof-application"
                     for e in candidates)
          assert [e["data"]["cost"] for e in candidates] == [2, 2]
          selection = next(e for e in events if e["operation"] == "proof.search.select")
          assert selection["data"]["body"] == "symm(x, x == true) using [given = p]"
          closed = events[-1]
          assert closed["operation"] == "proof-core.run.close"
          assert closed["data"]["proof_functions_verified"] == 2
          assert closed["data"]["runtime_proof_values"] == 0
          PY

          transitivity_rain="$(mktemp)"
          $out/bin/fine rain "$src/fine/fixtures/identity-transitivity.fine" \
            > "$transitivity_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/identity-transitivity.fine" "$transitivity_rain"
          ${pkgs.python3}/bin/python - "$transitivity_rain" <<'PY'
          import json, pathlib, sys
          events = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
          candidates = [e for e in events if e["operation"] == "proof.search.candidate"]
          assert len(candidates) == 1
          candidate = candidates[0]["data"]
          assert candidate["body"] == "trans(left, middle, right) using [first = p, second = q]"
          assert candidate["function"] == "trans"
          assert candidate["index_arguments"] == ["left", "middle", "right"]
          assert candidate["proof_arguments"] == ["p", "q"]
          assert candidate["cost"] == 3
          assert "wrong" not in candidate["proof_arguments"]
          selection = next(e for e in events if e["operation"] == "proof.search.select")
          assert selection["data"]["body"] == candidate["body"]
          close = next(e for e in events if e["operation"] == "proof.search.close")
          assert close["data"]["residual_candidates"] == []
          assert events[-1]["data"]["runtime_proof_values"] == 0
          PY

          z3_transitivity_rain="$(mktemp)"
          $out/bin/fine rain --proof-selector z3 \
            "$src/fine/fixtures/identity-transitivity.fine" \
            > "$z3_transitivity_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/identity-transitivity.fine" "$z3_transitivity_rain"
          ${pkgs.python3}/bin/python - "$z3_transitivity_rain" <<'PY'
          import json, pathlib, sys
          events = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
          grammar = next(e for e in events if e["operation"] == "proof.model.grammar")
          solve = next(e for e in events if e["operation"] == "proof.model.solve")
          lifted = next(e for e in events if e["operation"] == "proof.model.lift")
          selected = next(e for e in events if e["operation"] == "proof.search.select")
          close = next(e for e in events if e["operation"] == "proof.search.close")
          candidates = [e for e in events if e["operation"] == "proof.search.candidate"]
          assert grammar["data"]["max_cost"] == 3
          productions = grammar["data"]["productions"]
          assert grammar["data"]["candidate_trees_enumerated"] is False
          assert len(productions) == 40
          assert grammar["data"]["states"] == 23
          assert grammar["data"]["transitions"] == 39
          assert len(grammar["data"]["state_graph"]) == 23
          root = next(s for s in grammar["data"]["state_graph"]
                      if s["id"] == grammar["data"]["root_state"])
          root_production = productions[grammar["data"]["selected_root_production"]]
          assert root_production["kind"] == "proof-application"
          assert root_production["function"] == "trans"
          assert root_production["index_arguments"] == ["left", "middle", "right"]
          assert len(root_production["arguments"]) == 2
          assert any(edge["production"] == root_production["id"]
                     for edge in root["alternatives"])
          assert len(candidates) == 1
          assert candidates[0]["data"]["origin"] == "model-lift"
          assert solve["data"]["grammar_event"] == grammar["event_id"]
          assert solve["data"]["model_value"].startswith("(FineProofStateConstructor-")
          assert solve["data"]["cost"] == 3
          assert lifted["data"]["solve_event"] == solve["event_id"]
          assert lifted["data"]["body"] == "trans(left, middle, right) using [first = p, second = q]"
          assert lifted["data"]["in_bounded_grammar"] is True
          assert lifted["data"]["candidate_trees_enumerated"] is False
          assert lifted["data"]["reparse_required"] is True
          assert selected["data"]["candidate"] == lifted["data"]["candidate"]
          assert close["data"]["residual_candidates"] == []
          assert close["data"]["residual_grammar"] == grammar["data"]["grammar"]
          assert close["data"]["candidate_trees_enumerated"] is False
          PY

          bad_state_graph_rain="$(mktemp)"
          ${pkgs.python3}/bin/python - "$z3_transitivity_rain" "$bad_state_graph_rain" <<'PY'
          import json, pathlib, sys
          events = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
          grammar = next(e for e in events if e["operation"] == "proof.model.grammar")
          grammar["data"]["productions"][0]["result"]["carrier"] += 1
          pathlib.Path(sys.argv[2]).write_text("".join(json.dumps(e) + "\n" for e in events))
          PY
          if ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/identity-transitivity.fine" "$bad_state_graph_rain" 2>/dev/null; then
            echo "Rainfall replay accepted a proof grammar with a mistyped production" >&2
            exit 1
          fi

          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/identity-checkpoint.fine" "$checkpoint_rain"
          ${pkgs.python3}/bin/python - "$checkpoint_rain" <<'PY'
          import json, pathlib, sys
          events = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
          candidates = [e for e in events if e["operation"] == "proof.search.candidate"]
          selected = next(e for e in events if e["operation"] == "proof.search.select")
          solve = next(e for e in events if e["operation"] == "proof.model.solve")
          grammar = next(e for e in events if e["operation"] == "proof.model.grammar")
          close = next(e for e in events if e["operation"] == "proof.search.close")
          terminal = events[-1]
          assert len(candidates) == 1 and candidates[0]["data"]["origin"] == "model-lift"
          assert any(p["kind"] == "open" and p["source"] == "?"
                     for p in grammar["data"]["productions"])
          assert grammar["data"]["candidate_trees_enumerated"] is False
          assert selected["data"]["body"] == "trans(left, middle, right) using [first = p, second = ?]"
          assert selected["data"]["complete"] is False
          assert solve["data"]["complete"] is False
          assert solve["data"]["closed_frontier"] == 1
          assert solve["data"]["open_leaves"] == 1
          assert close["data"]["status"] == "checkpointed"
          assert close["data"]["residual_grammar"] == grammar["data"]["grammar"]
          assert not any(e["operation"] == "assert.verify" for e in events)
          assert terminal["operation"] == "proof-core.run.close"
          assert terminal["data"]["status"] == "checkpointed"
          assert terminal["data"]["proof_holes_checkpointed"] == 1
          PY

          state_profile_directory="$(mktemp -d)"
          for budget in 1 2 3 4; do
            $out/bin/fine rain --checkpoint --proof-budget "$budget" \
              "$src/fine/fixtures/identity-checkpoint.fine" \
              > "$state_profile_directory/$budget.rain"
          done
          ${pkgs.python3}/bin/python "$src/fine/profile_proof_state_growth.py" \
            "$state_profile_directory/1.rain" "$state_profile_directory/2.rain" \
            "$state_profile_directory/3.rain" "$state_profile_directory/4.rain" \
            > "$state_profile_directory/profile.json"
          cmp "$src/fine/research/proof-state-growth-profile.json" \
            "$state_profile_directory/profile.json"

          resumed_rain="$(mktemp)"
          $out/bin/fine rain --checkpoint --proof-budget 2 \
            "$src/fine/fixtures/identity-checkpoint-materialized.fine" > "$resumed_rain"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-validate \
            "$src/fine/fixtures/identity-checkpoint-materialized.fine" "$resumed_rain"
          ${pkgs.python3}/bin/python - "$resumed_rain" <<'PY'
          import json, pathlib, sys
          events = [json.loads(line) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()]
          selection = next(e for e in events if e["operation"] == "proof.search.select")
          assert selection["data"]["body"] == "symm(right, middle) using [given = bool_eta(right)]"
          assert selection["data"]["complete"] is True
          assert any(e["operation"] == "assert.verify" for e in events)
          assert events[-1]["data"]["status"] == "verified"
          PY

          projection="$(mktemp)"
          ${pkgs.python3}/bin/python $out/bin/fine-rain-project \
            "$src/fine/fixtures/identity-coeffect.fine" "$rain" > "$projection"
          ${pkgs.python3}/bin/python - "$projection" <<'PY'
          import json, pathlib, sys
          value = json.loads(pathlib.Path(sys.argv[1]).read_text())
          assert value["display_snapshot"]["admitted_by_rainfall"] is True
          assert value["annotations"]
          assert {item["status"] for item in value["annotations"]} == {"current"}
          PY

          runHook postInstallCheck
        '';
      };

      playground-wasm = pkgs.stdenv.mkDerivation {
        pname = "fine-playground-wasm";
        version = "0.1.0";
        src = wasmSource;
        nativeBuildInputs = with pkgs; [ cmake ninja python3 emscripten ];
        dontUseCmakeConfigure = true;
        dontConfigure = true;

        buildPhase = ''
          runHook preBuild
          emcmake cmake -S . -B build-wasm -G Ninja \
            -DCMAKE_BUILD_TYPE=MinSizeRel \
            -DZ3_BUILD_LIBZ3_SHARED=OFF \
            -DZ3_BUILD_EXECUTABLE=OFF \
            -DZ3_BUILD_TEST_EXECUTABLES=OFF \
            -DZ3_SINGLE_THREADED=ON \
            -DFINE_BUILD_EXECUTABLE=ON
          cmake --build build-wasm --target fine-bin
          runHook postBuild
        '';

        installPhase = ''
          runHook preInstall
          mkdir -p "$out"
          cp build-wasm/fine.mjs build-wasm/fine.wasm "$out/"
          runHook postInstall
        '';
      };

      playground-wasm-pthreads = pkgs.stdenv.mkDerivation {
        pname = "fine-playground-wasm-pthreads";
        version = "0.1.0";
        src = wasmSource;
        nativeBuildInputs = with pkgs; [ cmake ninja python3 emscripten ];
        dontUseCmakeConfigure = true;
        dontConfigure = true;

        buildPhase = ''
          runHook preBuild
          emcmake cmake -S . -B build-wasm-pthreads -G Ninja \
            -DCMAKE_BUILD_TYPE=MinSizeRel \
            -DCMAKE_C_FLAGS=-pthread \
            -DCMAKE_CXX_FLAGS=-pthread \
            -DZ3_BUILD_LIBZ3_SHARED=OFF \
            -DZ3_BUILD_EXECUTABLE=OFF \
            -DZ3_BUILD_TEST_EXECUTABLES=OFF \
            -DZ3_SINGLE_THREADED=OFF \
            -DFINE_BUILD_EXECUTABLE=ON \
            -DFINE_ENABLE_LIVE_LIFT=ON
          cmake --build build-wasm-pthreads --target fine-bin
          runHook postBuild
        '';

        installPhase = ''
          runHook preInstall
          mkdir -p "$out"
          cp build-wasm-pthreads/fine.* "$out/"
          runHook postInstall
        '';
      };

      playground = pkgs.buildNpmPackage {
        pname = "fine-playground";
        version = "0.1.0";
        src = ./playground;
        npmDepsHash = "sha256-yIB1xGWSt4wUSE3WvUF2I7edLE2H6ZOlUbox1mvWgsU=";
        npmBuildScript = "build";

        preBuild = ''
          mkdir -p public
          cp ${./fine/fixtures/playground-demo.fine} public/sample.fine
          cp ${self.packages.${system}.playground-wasm}/fine.mjs \
            ${self.packages.${system}.playground-wasm}/fine.wasm public/
          cp ${self.packages.${system}.playground-wasm-pthreads}/fine.mjs \
            public/fine-pthreads.mjs
          cp ${self.packages.${system}.playground-wasm-pthreads}/fine.wasm \
            public/fine-pthreads.wasm
        '';

        doCheck = true;
        checkPhase = ''
          runHook preCheck
          node smoke.mjs ${self.packages.${system}.playground-wasm} \
            ${./fine/fixtures/playground-demo.fine} \
            ${./fine/fixtures/cst-roundtrip-ugly.fine} \
            ${./fine/fixtures/cst-roundtrip-ugly-materialized.fine} \
            ${./fine/fixtures/identity-checkpoint.fine} \
            ${./fine/fixtures/identity-checkpoint-materialized.fine} \
            ${./fine/fixtures/identity-checkpoint-complete.fine} \
            ${./fine/fixtures/top-level-declarations.fine}
          cmp ${./fine/fixtures/playground-demo.fine} dist/sample.fine
          node pthread-smoke.mjs ${self.packages.${system}.playground-wasm-pthreads} \
            ${./fine/fixtures/identity-checkpoint.fine} \
            ${./fine/fixtures/identity-checkpoint-multi.fine} \
            ${./fine/fixtures/identity-checkpoint-multi-materialized.fine} \
            ${./fine/fixtures/identity-checkpoint-multi-interrupted.fine}
          node serve-smoke.mjs
          runHook postCheck
        '';

        installPhase = ''
          runHook preInstall
          mkdir -p "$out/site" "$out/server"
          cp -r dist/. "$out/site/"
          cp -r node_modules "$out/server/"
          cp package.json vite.config.js "$out/server/"
          runHook postInstall
        '';
      };
      };

      apps.${system} = {
      live = {
        type = "app";
        program = "${self.packages.${system}.default}/bin/fine-rain-live";
        meta.description = "Local browser editor for live Fine Rainfall evidence";
      };

      playground-service = {
        type = "app";
        program = "${playgroundServer}/bin/fine-playground-service";
        meta.description = "Serve Fine's browser playground on localhost";
      };
      };
    };
}
