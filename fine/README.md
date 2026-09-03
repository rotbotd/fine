# Fine proof-term branch

This branch is the first executable cut of Fine's two-level core. Proof types
are virtual by construction: source and elaborator types for proof evidence are
disjoint from runtime values, so there is no runtime proof case to erase.

```fine
function replace(left: Int, right: Int) -> Int
  takes [same: Id(Int, left, right)]
  ensures { result == right; }
{
  left
}

run identity_coeffect {
  let x: Int = 7;
  let y: Int = x;
  proof p: Id(Int, x, y) = refl(x);
  let answer: Int = replace(x, y);
  assert answer == y;
}
```

The identity proof is retained as source evidence and automatically contributes
`x == y` to checking. The function's coeffect asks the caller to supply such a
proof. The first search rule selects an exact local proof; it does not perform
global instance resolution.

Ordinary runtime data uses Z3 native datatypes directly:

```fine
enum Nat { zero, succ(Nat) }

function predecessor(value: Nat) -> Nat {
  match value {
    zero => zero,
    succ(previous) => previous,
  }
}
```

Constructors have typed payloads, recursive self fields are allowed, and every
runtime match must cover each constructor once. Enum values may occur in an
identity type such as `Id(Nat, one, one)`, but its inhabitant is still static
proof evidence rather than a runtime datatype value.

Static indexed families use a separate form:

```fine
proof inductive Even(value: Nat) {
  even_zero() -> Even(zero);
  even_next(previous: Nat) takes [prior: Even(previous)]
    -> Even(succ(succ(previous)));
}

proof zero_even: Even(zero) = even_zero();
proof two_even: Even(succ(succ(zero))) = even_next[zero](zero_even);
```

The bracketed arguments are static value indices and the parenthesized arguments
are virtual proof fields. Proof functions may match indexed evidence in checked
proof bodies. Each arm refines the scrutinee index to its constructor result and
binds those two classes of fields separately. Exhaustiveness ignores impossible
constructors, so a zero-constructor family and an impossible concrete index both
admit `match evidence {}`. The match cannot return runtime data.

Structural proof recursion is explicit and proof-only:

```fine
proof function rebuild(value: Nat)
  takes [evidence: Even(value)]
  inducts(evidence)
  -> Rebuilt(value) {
  match evidence {
    even_zero() => rebuilt_zero(),
    even_next[previous](prior) =>
      rebuilt_next[previous](rebuild[previous](prior)),
  }
}
```

`inducts(evidence)` makes the function visible only as an induction hypothesis
inside its own body. A recursive source application must receive an exact
same-family proof field obtained by matching `evidence` (or one of its recursive
fields). Passing `evidence` again is rejected. No runtime recursive function is
created.

Build and run:

```sh
cmake -S . -B build/proof-core -G Ninja \
  -DZ3_BUILD_LIBZ3_SHARED=OFF \
  -DZ3_BUILD_EXECUTABLE=OFF \
  -DZ3_BUILD_TEST_EXECUTABLES=OFF \
  -DFINE_BUILD_EXECUTABLE=ON
cmake --build build/proof-core --target fine-bin
build/proof-core/fine run fine/fixtures/identity-coeffect.fine
build/proof-core/fine rain fine/fixtures/identity-coeffect.fine > trace.jsonl
python fine/rainfall_validate.py fine/fixtures/identity-coeffect.fine trace.jsonl
build/proof-core/fine materialize fine/fixtures/identity-coeffect.fine > explicit.fine
build/proof-core/fine run explicit.fine
build/proof-core/fine roundtrip fine/fixtures/cst-roundtrip-ugly.fine > unchanged.fine
```

`roundtrip` parses through Fine's lossless concrete token tree and emits the
owned bytes again. It preserves comments, tabs, line endings, blank lines, and
all other trivia exactly; the semantic AST remains a separate view over the
same source ranges.

`fine materialize` writes an explicit `using [same = p]` argument, reparses it,
and rechecks it with implicit resolution disabled before returning source.

Typed identity holes use `?` at the proof level. Their finite grammar contains
only exact local evidence and applicable reflexivity:

```fine
proof self: Id(Int, x, x) = ?;   // materializes as refl(x)
proof copied: Id(Int, x, x) = ?; // materializes as self
```

Ill-typed local proofs are excluded before enumeration. Rainfall records the
opened hole, every typed candidate, the selected candidate, and the complete
residual frontier. Materialization replaces each `?`, reparses, and reruns with
both proof and coeffect search forbidden before returning source.

An indexed proof hole uses a smaller exact grammar. Outside induction it can
select exact local family evidence. Inside a function annotated with
`inducts(evidence)`, it may also synthesize a self-application whose designated
argument is an exact recursive field:

```fine
even_next[previous](prior, wrong) => rebuilt_next[previous](?)
// materializes the hole as rebuild[previous](prior)
```

The mismatched `wrong: Rebuilt(succ(previous))` and the nondecreasing root are
absent before enumeration. Constructor synthesis and the Z3 datatype-model
selector do not yet cover indexed holes; requesting that selector fails rather
than silently changing the grammar.

The optional Z3 proof selector compacts that same finite frontier into a
recursive datatype grammar. It asks Z3 for a bounded ground constructor tree,
lifts the model value back to Fine proof syntax, and rejects it unless it is an
exact member of the deterministic reference frontier:

```sh
build/proof-core/fine run --proof-selector z3 \
  fine/fixtures/identity-transitivity.fine
build/proof-core/fine rain --proof-selector z3 \
  fine/fixtures/identity-transitivity.fine > trace.jsonl
build/proof-core/fine materialize --proof-selector z3 \
  fine/fixtures/identity-transitivity.fine > explicit.fine
```

The final command reparses and rechecks `explicit.fine` with proof search
forbidden. The default remains deterministic enumeration so the complete
frontier stays a stable reference rather than an accidental solver model.

An intentionally bounded checkpoint writes the best partial proof as ordinary
Fine with typed holes:

```sh
build/proof-core/fine checkpoint --proof-budget 2 \
  fine/fixtures/identity-checkpoint.fine > partial.fine
# partial.fine contains trans[left, middle, right](p, ?)

build/proof-core/fine checkpoint --proof-budget 2 partial.fine > complete.fine
build/proof-core/fine run complete.fine
```

The first pass prefers one closed child over decorative applications around
only open leaves. It reparses the emitted source and type-checks the fixed tree
without treating `?` as evidence. The second pass resumes at that nested hole.
Use `fine rain --checkpoint --proof-budget 2 ...` to retain the typed partial
frontier and lifted model in Rainfall without emitting source.

## Browser playground

The browser build compiles the ordinary Fine executable and this repository's
Z3 fork to WebAssembly. A bundled CodeMirror editor with a Fine-specific lexical
mode writes source into Emscripten's in-memory filesystem, invokes the same CLI
first in `run` mode and then in `rain` mode, and displays the ordinary result
beside formatted Rainfall events. The sample uses the Z3 datatype-model proof
selector.

`materialize holes` runs the same checked materializer, reads its exact bytes
back from the in-memory filesystem, and replaces the CodeMirror document in one
transaction. One undo therefore restores the entire pre-materialization source,
including comments and whitespace. Failed materialization leaves the editor
untouched. The CLI's `materialize --output file` form exists so the browser does
not reconstruct source from line-oriented stdout callbacks.

`search checkpoints` creates a separate Web Worker with its own Wasm runtime and
repeatedly applies the selected proof budget to the last completed source. Each
epoch writes, reparses, and validates an exact MEMFS checkpoint before posting it
to the main thread. The same elaboration writes a Rainfall sidecar, so the trace
pane changes only when its corresponding validated source snapshot is published;
an epoch is never rerun merely to obtain presentation data. The editor remains
unchanged and read-only while search is active. `stop and materialize` terminates the worker immediately, discards the
in-flight epoch, and installs only the last posted source through the same one-
transaction edit. A settled search installs its last source automatically.

```sh
nix build --no-link --print-out-paths .#playground
nix run .#playground-service
# open http://127.0.0.1:4174
```

`playground-wasm` is a separate flake package, so changing the HTML or browser
JavaScript does not rebuild the 11 MiB solver module. `playground/smoke.mjs`
runs the compiled module under Node and requires the grammar, model solve,
structural lift, and closed-run Rainfall events. It also materializes the ugly
CST fixture byte-for-byte, applies the result as one editor transaction, and
requires one undo to recover the original bytes. Three checkpoint epochs must
produce the exact partial fixture, exact complete fixture, and then no change;
their paired traces must respectively close as checkpointed, verified, and
verified, with model lifts exactly in the two epochs which still contain holes.
The termination helper is required to kill its worker before dispatching the
editor transaction. The production service is a
static file server: source and solver execution remain in the visitor's browser.

The former Bool-predicate implementation remains runnable from tag
`pre-pat-1d7222a23`. The exact survival and deletion map is in
[`PROOF_TERMS.md`](PROOF_TERMS.md); current invariants are in
[`ARCHITECTURE.md`](ARCHITECTURE.md); the ordered executable slices are in
[`ROADMAP.md`](ROADMAP.md); experiments and closed decisions remain in the
append-only root [`LOG.md`](../LOG.md).
