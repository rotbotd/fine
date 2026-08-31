# Fine

Fine is a solver language built inside a soft fork of Z3. Its first closed
loop asks Z3 to fill an array-shaped hole with a two-state bisimulation, lifts
the finite relation into Fine table syntax, reifies that syntax through the
same Z3 context, and checks exact AST identity.

From this checkout:

```sh
cmake -S . -B build/fine -G Ninja \
  -DZ3_BUILD_LIBZ3_SHARED=OFF \
  -DZ3_BUILD_EXECUTABLE=OFF \
  -DZ3_BUILD_TEST_EXECUTABLES=OFF
cmake --build build/fine --target fine-bin
./build/fine/fine demo-bisim
```

`nix build --no-link` performs the same build and runs the demo as an install
check. The equivalent SMT-LIB fixture is in `fixtures/`; design invariants and
rainfall trace identity rules are in `ARCHITECTURE.md`.
