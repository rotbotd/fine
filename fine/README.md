# Fine

Fine is a solver language built inside a soft fork of Z3. Its first parsed
program declares two finite transition systems, asks Z3 to fill an
array-shaped bisimulation hole, lifts the finite relation into Fine table
syntax, reifies that syntax through the same Z3 context, and checks exact AST
identity.

From this checkout:

```sh
cmake -S . -B build/fine -G Ninja \
  -DZ3_BUILD_LIBZ3_SHARED=OFF \
  -DZ3_BUILD_EXECUTABLE=OFF \
  -DZ3_BUILD_TEST_EXECUTABLES=OFF
cmake --build build/fine --target fine-bin
./build/fine/fine demo-bisim
./build/fine/fine run fine/fixtures/two-state-bisim.fine
```

`demo-bisim` embeds that exact checked-in Fine fixture; it is not a second
hard-coded solver path. `nix build --no-link` runs both entry points as install
checks. The equivalent SMT-LIB fixture is in `fixtures/`; design invariants and
rainfall trace identity rules are in `ARCHITECTURE.md`.
