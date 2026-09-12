# Two-level proof core fixtures

- `playground-demo.fine` is the checked public introduction shared by this
  repository's primary README and the browser playground. It exercises a
  runtime enum and match, indexed proof introduction and elimination,
  structural proof induction, implicit constructor/function coeffects, and a
  Z3-selected identity hole in one executable document.
- `playground-demo-materialized.fine` is the exact explicit-coeffect and
  explicit-proof output of materializing that introduction; install checks
  require the generated source to match it byte-for-byte and rerun cleanly.
- `playground-demo-specialized.fine` is the exact default browser specialization:
  the nullary `zero_from_one` wrapper changes from a call to `zero`, while its
  adjacent comments and the still-open proof hole remain byte-identical.
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
- `proof-inductive-match.fine` checks a body-bearing eliminator for `Even(value)`.
  Its two arms refine `value` to distinct constructor indices before accepting
  `refl(value)`, and the recursive arm uses both `previous` and `prior`. The same
  file eliminates the empty family `Never()` and accepts zero arms for the
  impossible index `Even(succ(zero))`. A two-index control also prevents one
  symbolic index from being refined to two different constructor results.
- `proof-inductive-induction.fine` uses `inducts(evidence)` to rebuild an
  `Even` derivation. Its recursive spelling is accepted only on the exact
  same-family `prior` field and is recorded as an induction-hypothesis edge, not
  a runtime call.
- `proof-inductive-branching-induction.fine` rebuilds a binary derivation whose
  node constructor owns two recursive fields. Its target constructor needs both
  recursive results, and Rainfall must retain two distinct IH-use events under
  the same exact parent.
- `reject-nondecreasing-proof-recursion.fine` calls the annotated function on
  its root evidence rather than a recursive constructor field.
- `reject-recursion-without-inducts.fine` shows that an ordinary body-bearing
  proof function is not visible recursively.
- `proof-inductive-holes.fine` gives an indexed hole exactly one structurally
  valid IH application while a wrong-index local is excluded, then gives a
  second hole an exact local. Its materialized companion replaces both holes and
  reruns without search.
- `reject-empty-inductive-hole.fine` removes `inducts`, so the wrong-index local
  cannot prevent the indexed grammar from closing empty.
- `reject-nonexhaustive-proof-match.fine` omits one constructor that remains
  reachable for a symbolic index.
- `reject-unreachable-proof-match-arm.fine` writes a constructor arm after index
  refinement has proved that constructor impossible.
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
- `reject-needs-keyword.fine` keeps the removed `needs` spelling so the parser
  cannot silently retain both names for one static-input mechanism.
- `reject-proof-as-value.fine` tries to put proof evidence in an `Int` binding.
- `reject-unjustified-function.fine` removes the identity coeffect from the
  function whose guarantee needs it. Its satisfiable negated guarantee returns
  exact-roundtripped integer inputs and result rather than only a generic error.
- `reject-enum-function-counterexample.fine` forces a recursive `Nat` model
  value to lift as `succ(zero)` and checks the returned source witness in
  Rainfall.
- `reject-negative-function-counterexample.fine` forces `-1` through a declared
  identity coeffect, covering negative integer source syntax and retaining the
  coeffect name as the counterexample's assumed domain.
- `reject-result-parameter.fine` prevents a value or coeffect parameter from
  shadowing the function body's reserved `result` name inside `ensures`.
- `reject-result-coeffect.fine` applies the same reservation across the static
  coeffect namespace before proof absorption begins.
- `staged-proof-elimination.fine` checks compile-time constructor selection,
  impossible zero-arm elimination, indexed-premise and identity-premise
  propagation, joint hidden-witness support, and identity-rooted recovery of
  erased constructor fields. Its `FiniteReach(Phase)` section computes a
  four-state least constructor closure: `zero`, `one`, and `two` are reached in
  order, while the self-supported `stuck` index remains empty.
- `reject-empty-reachable-recursive-index.fine` tries to eliminate
  `FiniteReach(two)` with zero arms. The three-round constructor chain makes the
  `reach_two` arm mandatory, preventing the finite closure from proving only
  absence while missing reachable indices.
- `stage-diagnostic.fine` puts an exact `Nat` argument in the nullary wrapper
  `four_even`. The public staging diagnostic follows its accepted mutually
  recursive `even`/`odd` SCC to `comptime(true)` without blocking recursion.
- `stage-diagnostic-specialized.fine` is the exact `fine specialize` output.
  Only the wrapper expression becomes `true`; comments on both sides survive,
  and the entire document reparses, verifies, and stages to the same value.
- `stage-specialization-pass.fine` keeps two parameterized functions runtime as
  wholes while exposing one exact expression island in each selected match arm.
  `stage-specialization-pass-first.fine` is the exact source after specializing
  `simplify_inside`; `stage-specialization-pass-specialized.fine` is the exact
  second epoch after specializing `branch_refinement`. The real browser smoke
  installs and undoes both epochs independently.
- `identity-residualized-hidden-field-specialized.fine` is the certified staging
  control for erased constructor fields. The original coeffect and explicit
  identity-proof routes both stage to `off`; specializing the former replaces
  only its proof match and retains the adjacent comments. Constructor parameters
  and arm binders use different spellings to reject name-based substitution.
- `staged-residualized-expression.fine` makes the certified replacement the
  composed term `succ(visible)` rather than a name or nullary constructor. Its
  nullary wrapper stages to `comptime(succ(zero))`; the paired specialized file
  retains both adjacent comments around the nested emitted term.
- `identity-holes.fine` gives a typed identity hole a bounded finite
  grammar. The first hole forms `refl(x)`; the second selects exact local proof
  `self`; an earlier proof of `Id(Int, y, y)` is excluded before enumeration.
- `identity-holes-materialized.fine` is the exact replacement of both holes and
  its implicit coeffect; it reparses with both proof and coeffect search forbidden.
- `reject-empty-proof-hole.fine` asks for `Id(Int, x, y)` with distinct exact
  endpoints and no matching local evidence, so the typed grammar is empty.
- `identity-symmetry.fine` verifies two proof-level functions. Its reversed
  identity cannot use exact local evidence or `refl`, so bounded backward
  search selects `symm(x, x == true) using [given = p]` and retains the nested alternative.
- `identity-symmetry-materialized.fine` is the exact selected application tree
  and reruns with proof search forbidden.
- `identity-transitivity.fine` forces backward search to recover a middle index
  absent from the goal by matching exact local proof types. The only cost-three
  tree is `trans(left, middle, right) using [first = p, second = q]`; Rainfall retains the middle index
  and the two child proofs separately. With `--proof-selector z3`, an exact
  bounded state-datatype model lifts to that same source tree.
- `identity-transitivity-materialized.fine` is that exact two-child tree and
  reruns with proof search forbidden.
- `identity-checkpoint.fine` forces four live costs from an unchanged root hole
  through one partial transitivity tree to a complete proof.
- `identity-checkpoint-multi.fine` puts a cheap reflexive hole before the deeper
  original checkpoint hole, so live publication
  must retain the first completed concrete edit while searching the second.
- `identity-checkpoint-multi-materialized.fine` is the exact cumulative source
  emitted after both source-ordered searches close.
- `identity-checkpoint-multi-interrupted.fine` is the exact budget-two source:
  the first hole is complete while the later hole still contains one open leaf.
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
