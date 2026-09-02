{
  description = "Fine, a two-level proof-search language inside a Z3 soft fork";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
    in {
      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [ cmake ninja python3 clang pkg-config git ];
      };

      packages.${system}.default = pkgs.stdenv.mkDerivation {
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

      apps.${system}.live = {
        type = "app";
        program = "${self.packages.${system}.default}/bin/fine-rain-live";
        meta.description = "Local browser editor for live Fine Rainfall evidence";
      };
    };
}
