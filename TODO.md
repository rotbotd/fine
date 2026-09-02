# Fine TODO

## Governing principle: synthesize inspectable proofs, erase their runtime

Fine should search for a typed source proof, expose the unfinished search in
Rainfall, let the user interrupt and add structure, then materialize the exact
proof so later runs check it without repeating search. Z3 propositions and Z3's
own unsat proofs are not source proof terms. Fine owns the proof grammar and
rechecks every materialized term.

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

## Next: named proof functions

- [x] Add proof-level function declarations whose parameters and result are
      proof evidence and which cannot enter runtime value calls.
- [x] Make identity symmetry the first application-only hole: it must be
      impossible to close by exact local selection or `refl`.
- [x] Add type-directed application candidates only when the instantiated
      result has the exact expected proof type.
- [ ] Add transitivity only after symmetry materializes and reruns without
      search; retain its two input proofs distinctly in Rainfall.
- [x] Bound recursive application grammar so a cyclic proof-function set
      terminates without adding a tactic language or global theorem search.
- [ ] After symmetry works with deterministic enumeration, test a bounded
      Z3 datatype-model backend against the same exact grammar. Lift and recheck
      its chosen tree; do not treat Z3's native unsat proof as Fine source.

## Later, only after identity search earns it

- [ ] Add proof-only elimination and reject elimination from proofs into runtime
      values.
- [ ] Design inductive propositions with derivation terms from birth. Do not
      retrofit the old Bool-valued `predicate` declaration.
- [ ] Recover datatypes and ordinary model/counterexample consumers one at a time
      against the new value representation.
- [ ] Connect source proof materialization to the editor host's atomic revision
      transaction.
- [ ] After the proof syntax, Rainfall schema, and materialization contract
      survive a two-child proof function and the bounded Z3 selector, build the
      first browser vertical slice: the real Fine core in WASM, CodeMirror 6,
      and `identity-symmetry.fine` showing its candidate frontier, materialized
      edit, and search-free rerun. Publish only then at `fine.shit.yachts`.
