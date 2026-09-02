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

## Next: a proof hole worth searching

- [ ] Add a typed proof hole whose expected type is an identity.
- [ ] Give it a finite, type-directed grammar containing exact local evidence,
      `refl`, and named proof applications; ill-typed candidates must be absent
      before enumeration.
- [ ] Record each frontier/candidate/residual in Rainfall without presenting Z3
      traffic as a source proof.
- [ ] Materialize one synthesized proof term and demonstrate a later run with no
      proof search.
- [ ] Add identity symmetry and transitivity only when the hole fixture forces
      them; do not grow a general tactic language first.

## Later, only after identity search earns it

- [ ] Add proof-only elimination and reject elimination from proofs into runtime
      values.
- [ ] Design inductive propositions with derivation terms from birth. Do not
      retrofit the old Bool-valued `predicate` declaration.
- [ ] Recover datatypes and ordinary model/counterexample consumers one at a time
      against the new value representation.
- [ ] Connect source proof materialization to the editor host's atomic revision
      transaction.
