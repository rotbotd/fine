# Fine proof-term branch

This branch is the first executable cut of Fine's two-level core. Proof types
are virtual by construction: source and elaborator types for proof evidence are
disjoint from runtime values, so there is no runtime proof case to erase.

```fine
function replace(left: Int, right: Int) -> Int
  needs [same: Id(Int, left, right)]
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
  even_next(previous: Nat) needs [prior: Even(previous)]
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
```

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

## Browser playground

The browser build compiles the ordinary Fine executable and this repository's
Z3 fork to WebAssembly. A bundled CodeMirror editor with a Fine-specific lexical
mode writes source into Emscripten's in-memory filesystem, invokes the same CLI
first in `run` mode and then in `rain` mode, and displays the ordinary result
beside formatted Rainfall events. The sample uses the Z3 datatype-model proof
selector.

```sh
nix build --no-link --print-out-paths .#playground
nix run .#playground-service
# open http://127.0.0.1:4174
```

`playground-wasm` is a separate flake package, so changing the HTML or browser
JavaScript does not rebuild the 11 MiB solver module. `playground/smoke.mjs`
runs the compiled module under Node and requires the grammar, model solve,
structural lift, and closed-run Rainfall events. The production service is a
static file server: source and solver execution remain in the visitor's browser.

The former Bool-predicate implementation remains runnable from tag
`pre-pat-1d7222a23`. The exact survival and deletion map is in
[`PROOF_TERMS.md`](PROOF_TERMS.md); current invariants are in
[`ARCHITECTURE.md`](ARCHITECTURE.md); the ordered executable slices are in
[`ROADMAP.md`](ROADMAP.md); experiments and closed decisions remain in the
append-only root [`LOG.md`](../LOG.md).
