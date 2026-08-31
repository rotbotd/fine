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
          grep -F "reify(lift(x)): exact ast identity" <<<"$output"
          parsed="$($out/bin/fine run "$src/fine/fixtures/two-state-bisim.fine")"
          echo "$parsed"
          grep -F "model bisim = table(default: false)" <<<"$parsed"
          grep -F "(left_0, right_0): true" <<<"$parsed"
          grep -F "reify(lift(x)): exact ast identity" <<<"$parsed"
          runHook postInstallCheck
        '';
      };
    };
}
