# Fine TODO

## Governing principle: synthesize inspectable proofs, erase their runtime

Fine should search for a typed source proof, expose the unfinished search in
Rainfall, let the user interrupt and add structure, then materialize the exact
proof so later runs check it without repeating search. Z3 propositions and Z3's
own unsat proofs are not source proof terms. Fine owns the proof grammar and
rechecks every materialized term.

Every syntax-changing slice also updates the browser language reference and its
lexical highlighting before it closes. The playground must not advertise an
older language than the executable accepts.

Proof evidence is virtual by construction. Value and proof types have disjoint
ASTs; runtime values have no proof variant. Introducing identity evidence
retains its source term while automatically absorbing its equality proposition
into the lexical solver context. Functions declare contextual proof demands;
callers synthesize or supply the evidence.

## Closed: identity coeffect boundary

- [x] Preserve the former implementation at `pre-pat-1d7222a23` and cut the
      `fine/proof-terms` branch.
- [x] Replace the parser and elaborator with disjoint value/proof structures.
- [x] Form `refl(x) : Id(A, x, x)` without a runtime proof representation.
- [x] Absorb identity evidence automatically and verify a function guarantee
      that fails without the declared identity coeffect.
- [x] Instantiate a function coeffect at a call and resolve it from exact
      caller-local proof evidence.
- [x] Materialize the implicit choice as `using [same = p]`, reparse, and rerun
      with implicit resolution disabled.
- [x] Reject a missing coeffect and a proof name used as an `Int`.
- [x] Retain formation, absorption, demand, resolution, and use separately in
      Rainfall; exact-validate every absorbed Z3 proposition.

Exit test: `identity-coeffect.fine` passes; removing its proof fails at the call;
removing the function coeffect fails its guarantee; and `reject-proof-as-value`
shows proof evidence cannot enter execution.

## Closed: ordinary runtime enums

- [x] Add closed value-level enums backed by Z3 native datatypes, including
      recursive self fields and typed payload constructors.
- [x] Add exhaustive runtime matching with recognizers and typed accessors;
      reject missing constructors, repeated arms, and wrong binder arity.
- [x] Keep enum values inside `ValueTerm`, while permitting an enum sort as the
      carrier/index of virtual proof evidence.
- [x] Preserve the two-level boundary: enum constructors are runtime values and
      proof evidence still has no runtime variant.

Exit test: `runtime-enum.fine` constructs recursive `Nat`, symbolically rebuilds
and eliminates it, and checks `Id(Nat, one, one)` without creating a proof value.
The two rejecting controls catch non-exhaustive elimination and a mistyped
recursive field.

## Closed: indexed proof constructor introduction

- [x] Add `proof inductive Family(indices)`, distinct from runtime `enum` and
      from the former Bool-valued predicate declarations.
- [x] Give each static constructor explicit value indices, virtual proof fields,
      and an exact indexed result type.
- [x] Form nested constructor evidence while checking every recursive premise
      and result index by same-manager AST identity.
- [x] Retain family declaration, constructor application, and evidence formation
      separately in Rainfall; create neither a Z3 runtime datatype nor a runtime
      proof value.
- [x] Reject a wrong result index, a wrong recursive premise, and use of a proof
      constructor as a runtime function.

Exit test: `proof-inductive-even.fine` forms `Even(zero)` and then
`Even(succ(succ(zero)))` with `even_next[zero](zero_even)`. The family index is
an ordinary runtime `Nat`; the inhabitants and constructor remain static.
This closes introduction only. Proof matching, induction hypotheses, and holes
must be added as separate slices rather than inferred from these declarations.

## Closed: indexed proof-family match

- [x] Allow checked proof-function bodies without creating runtime functions.
- [x] Match only on indexed proof evidence and return only proof evidence.
- [x] Unify constructor results before checking arms, refine symbolic indices,
      and bind static values separately from virtual proof fields.
- [x] Compute exhaustiveness after refinement; reject both missing reachable
      arms and supplied unreachable arms.
- [x] Admit zero-constructor families and zero-arm elimination, including an
      impossible concrete index of a nonempty family.

Exit test: `proof-inductive-match.fine` makes `refl(value)` inhabit a different
identity type in each `Even(value)` arm, uses `previous` and `prior`, eliminates
`Never()`, and accepts no arms for `Even(succ(zero))`. The two rejecting fixtures
separate non-exhaustiveness from an explicitly written impossible arm.

## Closed: typed identity holes

- [x] Add `?` with an expected identity proof type.
- [x] Enumerate exact local evidence followed by applicable `refl`; exclude
      ill-typed local proofs before they become candidates.
- [x] Record opened holes, typed candidates, exact selections, and residual
      finite frontiers separately from Z3 observer traffic.
- [x] Replace both proof holes and implicit coeffects, then reparse and rerun
      with both searches forbidden.
- [x] Reject a hole whose type admits neither local evidence nor reflexivity.

Exit test: `identity-holes.fine` constructs one `refl`, selects one local proof,
retains the unchosen `refl` in Rainfall, and materializes byte-for-byte;
`reject-empty-proof-hole.fine` fails with an empty typed grammar.

## Closed: named proof functions

- [x] Add proof-level function declarations whose parameters and result are
      proof evidence and which cannot enter runtime value calls.
- [x] Make identity symmetry the first application-only hole: it must be
      impossible to close by exact local selection or `refl`.
- [x] Add type-directed application candidates only when the instantiated
      result has the exact expected proof type.
- [x] Add transitivity only after symmetry materializes and reruns without
      search; retain its two input proofs distinctly in Rainfall.
- [x] Bound recursive application grammar so a cyclic proof-function set
      terminates without adding a tactic language or global theorem search.
- [x] After symmetry works with deterministic enumeration, test a bounded
      Z3 datatype-model backend against the same exact grammar. Lift and recheck
      its chosen tree; do not treat Z3's native unsat proof as Fine source.

## Later, only after identity search earns it

- [ ] Add proof-only elimination only when a proof consumer cannot be expressed
      by context absorption and a checked proof function; reject elimination
      from proofs into runtime values.
- [ ] Design inductive propositions with derivation terms from birth. Do not
      retrofit the old Bool-valued `predicate` declaration.
- [x] Recover closed ordinary datatypes and runtime matching against the new
      value representation.
- [x] Add `proof inductive` as an indexed, static constructor family; do not
      reuse runtime enum matching or turn the family into a Bool predicate.
- [x] Add proof-producing elimination over `proof inductive`, retaining exact
      constructor and proof-field identity before any solver projection.
- [ ] Recover ordinary model/counterexample consumers one at a time.
- [ ] Connect source proof materialization to the editor host's atomic revision
      transaction.
- [x] After the proof syntax, Rainfall schema, and materialization contract
      survive a two-child proof function and the bounded Z3 selector, build the
      first browser vertical slice: the real Fine core in WASM, CodeMirror 6,
      and `identity-symmetry.fine` showing its candidate frontier, materialized
      edit, and search-free rerun. Publish only then at `fine.shit.yachts`.
