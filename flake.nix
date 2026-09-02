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

          output="$($out/bin/fine run "$src/fine/fixtures/identity-coeffect.fine")"
          echo "$output"
          grep -F "verified function: replace" <<<"$output"
          grep -F "formed proof: p : Id(Int, x, y) (virtual)" <<<"$output"
          grep -F "resolved coeffect: replace.same <- p (lexical search)" <<<"$output"
          grep -F "runtime-proof-values: 0 (unrepresentable)" <<<"$output"

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
          grep -F "filled proof hole: reversed <- symm[x, x == true](p) (typed search)" \
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
          grep -F "filled proof hole: composed <- trans[left, middle, right](p, q) (typed search)" \
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
          grep -F "filled proof hole: composed <- trans[left, middle, right](p, q) (Z3 datatype model)" \
            <<<"$z3_transitivity_output"

          z3_transitivity_materialized="$(mktemp)"
          $out/bin/fine materialize --proof-selector z3 \
            "$src/fine/fixtures/identity-transitivity.fine" \
            > "$z3_transitivity_materialized"
          cmp "$src/fine/fixtures/identity-transitivity-materialized.fine" \
            "$z3_transitivity_materialized"

          congruence_output="$($out/bin/fine run --proof-selector z3 \
            "$src/fine/fixtures/identity-congruence.fine")"
          echo "$congruence_output"
          grep -F 'filled proof hole: lifted <- truth_congruence[x == false, (x == true) == false](p) (Z3 datatype model)' \
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
          assert applied[0]["data"]["body"] == "bool_eta[x]()"
          assert [e["data"]["body"] for e in candidates] == [
              "symm[x, x == true](p)",
              "symm[x, x == true](bool_eta[x]())",
          ]
          assert all(e["data"]["production"] == "proof-application"
                     for e in candidates)
          assert [e["data"]["cost"] for e in candidates] == [2, 2]
          selection = next(e for e in events if e["operation"] == "proof.search.select")
          assert selection["data"]["body"] == "symm[x, x == true](p)"
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
          assert candidate["body"] == "trans[left, middle, right](p, q)"
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
          assert grammar["data"]["max_cost"] == 3
          assert grammar["data"]["productions"] == [
              "apply:trans[left, middle, right]/2", "local:p", "local:q"
          ]
          assert len(grammar["data"]["reference_candidates"]) == 1
          assert solve["data"]["grammar_event"] == grammar["event_id"]
          assert solve["data"]["model_value"] == "(apply-trans local-p local-q)"
          assert solve["data"]["cost"] == 3
          assert lifted["data"]["solve_event"] == solve["event_id"]
          assert lifted["data"]["body"] == "trans[left, middle, right](p, q)"
          assert lifted["data"]["in_reference_frontier"] is True
          assert lifted["data"]["reparse_required"] is True
          assert selected["data"]["candidate"] == lifted["data"]["candidate"]
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

      playground = pkgs.buildNpmPackage {
        pname = "fine-playground";
        version = "0.1.0";
        src = ./playground;
        npmDepsHash = "sha256-q0/aG3fAf1teDpTDSVqXxHqAJGzqZ+Lh2bg8o+3ll9M=";
        npmBuildScript = "build";

        preBuild = ''
          mkdir -p public
          cp ${./fine/fixtures/identity-transitivity.fine} public/sample.fine
          cp ${self.packages.${system}.playground-wasm}/fine.mjs \
            ${self.packages.${system}.playground-wasm}/fine.wasm public/
        '';

        doCheck = true;
        checkPhase = ''
          runHook preCheck
          node smoke.mjs ${self.packages.${system}.playground-wasm} \
            ${./fine/fixtures/identity-transitivity.fine}
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
