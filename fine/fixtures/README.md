# Two-level proof core fixtures

- `identity-coeffect.fine` forms an elaborator-only identity proof, absorbs it,
  resolves a function coeffect from exact caller-local evidence, and verifies a
  guarantee which needs the absorbed equality.
- `identity-coeffect-materialized.fine` is the exact explicit-`using` output;
  it reruns with implicit coeffect search forbidden.
- `reject-missing-coeffect.fine` has no caller proof for a demanded identity.
- `reject-proof-as-value.fine` tries to put proof evidence in an `Int` binding.
- `reject-unjustified-function.fine` removes the identity coeffect from the
  function whose guarantee needs it.
- `identity-holes.fine` gives a typed identity hole a bounded finite
  grammar. The first hole forms `refl(x)`; the second selects exact local proof
  `self`; an earlier proof of `Id(Int, y, y)` is excluded before enumeration.
- `identity-holes-materialized.fine` is the exact replacement of both holes and
  its implicit coeffect; it reparses with both proof and coeffect search forbidden.
- `reject-empty-proof-hole.fine` asks for `Id(Int, x, y)` with distinct exact
  endpoints and no matching local evidence, so the typed grammar is empty.
- `identity-symmetry.fine` verifies two proof-level functions. Its reversed
  identity cannot use exact local evidence or `refl`, so bounded backward
  search selects `symm[x, x == true](p)` and retains the nested alternative.
- `identity-symmetry-materialized.fine` is the exact selected application tree
  and reruns with proof search forbidden.
- `reject-unjustified-proof-function.fine` declares an arbitrary identity with
  no premise; declaration checking refutes the declaration itself.
- `reject-proof-function-as-value.fine` tries to call virtual proof code from a
  runtime `Bool` binding.
- `reject-cyclic-proof-search.fine` supplies a premise-preserving loop but no
  base inhabitant; the finite cost bound is exhausted rather than recursing.
