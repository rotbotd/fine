# Two-level proof core fixtures

- `runtime-enum.fine` declares recursive `Nat` as a runtime enum, constructs it,
  eliminates it with an exhaustive payload-binding match, verifies a symbolic
  reconstruction function, and uses `Nat` as the carrier of virtual identity
  evidence without creating a runtime proof value.
- `reject-nonexhaustive-enum-match.fine` omits the recursive constructor arm.
- `reject-enum-field-type.fine` supplies `Bool` to a recursive `Nat` field.
- `proof-inductive-even.fine` declares the static indexed family `Even(Nat)`.
  `even_zero` forms the base evidence; `even_next` takes an exact recursive
  proof field and changes the result index by two. Both inhabitants remain
  virtual while their indices are ordinary runtime `Nat` terms.
- `reject-proof-inductive-index.fine` applies the base constructor at
  `predecessor(succ(zero))`: solver-equal to `zero`, but not the exact
  manager-local result index the constructor produces.
- `reject-proof-inductive-premise.fine` supplies `Even(zero)` where the recursive
  constructor requires evidence at its explicit `previous` index.
- `reject-proof-constructor-as-value.fine` calls a static constructor in a
  runtime value binding.
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
- `identity-transitivity.fine` forces backward search to recover a middle index
  absent from the goal by matching exact local proof types. The only cost-three
  tree is `trans[left, middle, right](p, q)`; Rainfall retains the middle index
  and the two child proofs separately. With `--proof-selector z3`, the compacted
  recursive datatype model is `(apply-trans local-p local-q)` and lifts to that
  same source tree.
- `identity-transitivity-materialized.fine` is that exact two-child tree and
  reruns with proof search forbidden.
- `identity-congruence.fine` lifts a non-reflexive local identity through a
  nested Boolean expression. Result matching must recover both hidden indices
  from the proof parameter, and rendering must preserve the parentheses in the
  selected application so materialization reparses exactly.
- `identity-congruence-materialized.fine` is that explicit one-child
  application and reruns without search.
- `reject-transitivity-gap.fine` omits the second local child. Its reconstructible
  two-node alternative exceeds the same cost bound, so marginal support from the
  first child cannot admit transitivity.
- `reject-unjustified-proof-function.fine` declares an arbitrary identity with
  no premise; declaration checking refutes the declaration itself.
- `reject-proof-function-as-value.fine` tries to call virtual proof code from a
  runtime `Bool` binding.
- `reject-cyclic-proof-search.fine` supplies a premise-preserving loop but no
  base inhabitant; the finite cost bound is exhausted rather than recursing.
