# Internal array round-trip spike

This spike exercises the exact retraction law with Z3's internal C++ AST API,
not `z3++`. It installs the array and datatype declaration plugins into one
`ast_manager`, creates the enum datatype `State = S0 | S1`, and admits the fragment needed for an
`Array(State, Bool)` witness:

```
bool       ::= false | true
state      ::= S0 | S1
array      ::= array(default: bool)
             | store(array, key: state, value: bool)
```

The demonstrated table is `S0 -> false, S1 -> true`. Its canonical form uses
`false` as the deterministic default and emits stores, in constructor order,
only for cells that differ from that default. Thus its internal Z3 term is a
constant array followed by one store at `S1`.

`lift` recognizes internal Z3 constructors and returns the small constructor
tree above; `reify` constructs those same Z3 constructors. There is deliberately
no SMT-LIB or text parser in the identity path. Reconstructing the term in the
same `ast_manager` hits Z3's hash-cons table, so the executable checks raw
`expr*` equality and prints both pointers and AST ids. The printed Fine text is
only display output.

## Dependencies and build

The dependency is a static build of this Z3 checkout. Static linkage is
intentional: internal C++ symbols are not a supported/exported shared-library
ABI. The spike includes headers from `src` (plus generated headers from the Z3
build), explicitly initializes Z3's internal memory/symbol subsystem, and links
`libz3.a`. A C++20 compiler, CMake, and Ninja are sufficient;
the repository flake provides them.

From the repository root:

```sh
cmake -S . -B build/fine -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DZ3_BUILD_LIBZ3_SHARED=OFF \
  -DZ3_BUILD_TEST_EXECUTABLES=OFF
cmake --build build/fine --target libz3 -j2

cmake -S fine/spikes/array-roundtrip \
  -B build/array-roundtrip -G Ninja \
  -DZ3_BUILD="$PWD/build/fine"
cmake --build build/array-roundtrip
./build/array-roundtrip/array-roundtrip
```

The spike's CMake project is isolated; it does not alter the root Z3 build.

## Current run result

The isolated project configures, compiles, and links against the static archive.
It uses `reg_decl_plugins` rather than registering only array and datatype
plugins: datatype finalization itself constructs `seq_util`, so sequence and
character plugins are an actual internal precondition even for this two-case
enum.

The executable returns zero and reports the same pointer and AST ID for the
original and reified store term:

```text
fine: store(array(default: false), key: S1, value: true)
reify(lift(witness)) pointer-identical: yes
```
