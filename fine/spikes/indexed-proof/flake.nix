{
  description = "Executable boundary tests for Fine indexed proof families";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
      probe = pkgs.writeShellApplication {
        name = "fine-indexed-proof-probe";
        runtimeInputs = with pkgs; [ coreutils gnugrep z3 ];
        text = ''
          root=${self}
          work="$(mktemp -d)"
          trap 'rm -rf "$work"' EXIT

          z3 "$root/native-step.smt2" > "$work/native"
          cat "$work/native"
          test "$(grep -c '^sat$' "$work/native")" -eq 1
          test "$(grep -c '^unsat$' "$work/native")" -eq 1

          z3 "$root/intro-axioms-admit-junk.smt2" > "$work/junk"
          cat "$work/junk"
          grep -Fxq 'sat' "$work/junk"

          z3 "$root/cofinite-horn-is-existential.smt2" > "$work/cofinite"
          cat "$work/cofinite"
          test "$(grep -c '^sat$' "$work/cofinite")" -eq 2

          printf '%s\n' 'indexed-proof boundary: all countertests passed'
        '';
      };
    in {
      packages.${system}.default = probe;
      apps.${system}.default = {
        type = "app";
        program = "${probe}/bin/fine-indexed-proof-probe";
      };
      checks.${system}.default = probe;
      devShells.${system}.default = pkgs.mkShell {
        packages = [ pkgs.z3 ];
      };
    };
}
