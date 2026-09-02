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

The former Bool-predicate implementation remains runnable from tag
`pre-pat-1d7222a23`. The exact survival and deletion map is in
[`PROOF_TERMS.md`](PROOF_TERMS.md); current invariants are in
[`ARCHITECTURE.md`](ARCHITECTURE.md); experiments and closed decisions remain in
the append-only root [`LOG.md`](../LOG.md).
