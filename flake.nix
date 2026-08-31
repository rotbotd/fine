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
          runHook postInstallCheck
        '';
      };
    };
}
