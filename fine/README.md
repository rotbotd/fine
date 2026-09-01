# Fine

Fine is a solver language built inside a soft fork of Z3. Its first parsed
program declares two finite transition systems, asks Z3 to fill an
array-shaped bisimulation hole, lifts the finite relation into parseable Fine
model syntax, parses and elaborates that printed witness through the same Z3
context, and checks exact AST identity.

From this checkout:

```sh
cmake -S . -B build/fine -G Ninja \
  -DZ3_BUILD_LIBZ3_SHARED=OFF \
  -DZ3_BUILD_EXECUTABLE=OFF \
  -DZ3_BUILD_TEST_EXECUTABLES=OFF \
  -DFINE_BUILD_EXECUTABLE=ON
cmake --build build/fine --target fine-bin
./build/fine/fine demo-bisim
./build/fine/fine run fine/fixtures/two-state-bisim.fine
./build/fine/fine run fine/fixtures/synth-max.fine
./build/fine/fine run fine/fixtures/check-counterexample.fine
./build/fine/fine run fine/fixtures/check-valid.fine
./build/fine/fine run fine/fixtures/check-datatype-counterexample.fine
./build/fine/fine run fine/fixtures/check-tuple-counterexample.fine
./build/fine/fine rain fine/fixtures/synth-max.fine
./build/fine/fine rain fine/fixtures/two-state-bisim.fine
./build/fine/fine rain fine/fixtures/check-counterexample.fine > check.rain.jsonl
python fine/rainfall_validate.py fine/fixtures/check-counterexample.fine check.rain.jsonl
```

`demo-bisim` embeds that exact checked-in Fine fixture; it is not a second
hard-coded solver path. `nix build --no-link` runs both entry points as install
checks. The equivalent SMT-LIB fixture is in `fixtures/`; design invariants and
rainfall trace identity rules are in `ARCHITECTURE.md`. The twelve-angle
synthesis review and the narrowed built-in-semantics plan are recorded in
`research/synthesis-pressure-test.md`.

`rain` writes JSONL only. The synthesis replay follows native maximum
synthesis through public solver queries, completed counterexample values,
candidate selection, labelled instance activation, the unsat core, conditional
assembly, the successful builtin theory-application reductions inside its one
public simplification, an independent verification query, and the checked Fine
source witness. Every event names its producer and coverage. The internal
events cover only the observed `th_rewriter::reduce_app` path; this stream does
not pretend to contain substitutions, quantifier rewrites, other rewriter
instances, or solver search.

The bisimulation replay disables E-matching and records the accepted,
nontrivial quantifier instances which reach Z3's `qi_queue` binding callback
during the MBQI-only query. Each preprocessed quantifier retains a Fine source
role through its qid. The stream then records every completed finite relation
cell, deterministic constant-array-plus-stores extensionalization, and the
checked Fine model witness. It does not expose MBQI's auxiliary-context search,
discarded candidates, blocking clauses, or general CDCL(T) search.

`check` closes the missing counterexample loop for admitted value inputs. Fine
asserts the source assumptions together with the negation of the guarantees. A
satisfiable query returns a typed `counterexample` declaration; every completed
Int, Bool, enum, binary tuple, or monomorphic datatype assignment is lifted,
printed, parsed, and elaborated back to the identical same-manager AST. Recursive
field-bearing enum constructors lower directly to Z3 datatypes. An
unsatisfiable query reports that no counterexample exists. The current slice is
quantifier-free and has no pattern matching or projections; `counterexample` is
a returned witness form, not an executable declaration.

For every rain, the first objects bind the run to a fresh opaque document and an
exact source snapshot (revision, SHA-256 hash, and byte length). Parsed declarations
and expressions have snapshot-scoped source identities. The `check` elaborator
emits explicit exact/desugared source-to-term evidence edges; generated witnesses
and internal Z3 terms remain unowned by source. The installed
`fine-rain-validate` command admits only a replay whose snapshot, source nodes,
live term handles, manager, edge endpoints, chronology, and terminal state agree.
