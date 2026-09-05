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

## Planned: infer staging before mixed compile-time/runtime elimination

- [x] Lower every value function into an immutable, typed Fine-owned flow graph
      with resolved locals, constructors, and direct calls. Canonical semantic
      keys ignore trivia and local spelling. Partition the exact call graph into
      SCCs, solve relational parameter-dependency summaries by monotone
      iteration, and cache each SCC by its graph plus imported summary
      fingerprints. The cache contains no source pointers or Z3 handles.
- [x] Infer availability rather than asking for a trusted `comptime` annotation.
      Use the finite-height abstract value shape `bottom | comptime(value) |
      runtime`; different constants join to `runtime` rather than changing from
      one constant to another.
- [x] Track executable control-flow edges together with values, as in SCCP, so a
      dead runtime arm cannot contaminate a compile-time result and phi-like
      joins inspect only live predecessors.
- [x] Cache an immutable exact abstract transfer for every function. Calls
      compose cached callee transfers without re-lowering source; strict
      arguments, live match edges, and recursive-call blocks remain observable.
      Transfer fingerprints, rather than conservative source-graph fallbacks,
      invalidate reverse callers. The current pure value language has no other
      effects yet; add effect rows only when source syntax can produce one.
- [ ] Keep “stageable from these inputs” separate from “safe for the compiler to
      execute now.” The current SCC/transfer analysis is exercised only by
      `stage-analysis-probe`; accepted value functions now use native definitions
      and admit direct structural recursion, but the staging evaluator still
      blocks every recursive SCC. Close the connection in three named parts:
      - [x] Signature pass: register every value-function name and native value
            sort before any body. A later function is visible as declared but is
            not callable until its definition has been checked and installed.
      - [ ] Recursive definition rule: calls no longer elaborate a callee body;
            every checked function is one native `recfun`/`recdef`. Direct
            self-recursion is accepted only when every changed argument is an
            exact recursive enum field descended from its corresponding
            parameter, with at least one strict change. Forward and mutually
            recursive definition groups remain rejected until one SCC can be
            checked and installed atomically. Keep induction/proof obligations
            separate from definitional unfolding: a symbolic nonnegativity fact
            still times out.
      - [ ] Staging permission: require Fine-owned structural termination
            evidence or an explicit bound before replacing a blocked recursive
            transfer with compile-time evaluation. Z3 accepts
            `loop(x) = loop(x) + 1`, and merely asserting a ground equation about
            `loop(0)` hangs before `solver.check()`, outside the solver timeout.
- [x] Use constructor availability for the first staged proof-to-value match.
      The SMT context must leave one feasible constructor, and every value field
      used by the residual arm must be recovered from a runtime index. Ambiguous
      runtime-dependent evidence and proof-only hidden fields are rejected.
- [x] Let an impossible indexed coeffect discharge an expected value type with
      zero arms. Indexed evidence contributes a necessary existential
      constructor-head cover; the empty branch checks the resulting context is
      unsatisfiable and lowers to staging bottom.
- [x] Include identity-shaped constructor arguments and `takes` demands in
      staged reachability and the existential head cover. A result-compatible
      constructor whose own identity demand is contradictory is unreachable;
      indexed recursive premises remain deliberately omitted.
- [x] Retain every staged constructor feasibility query in Rainfall as an exact
      result-index/identity-premise term and solver status. Replay closes the
      complete constructor set against the selected or impossible value match.
- [x] Keep constructor parameters absent from the family result existential in
      the head cover. One identity demand may choose a hidden witness; two
      contradictory demands make that constructor impossible.

Transfer exit test: one dead runtime branch preserves a compile-time value, one
live join of distinct constants becomes runtime, a mutually recursive
named-function group stabilizes, and the same identity function yields a
compile-time result for a known argument and a runtime result for a runtime
argument. A known constructor with a runtime payload crosses a function call and
selects one caller match arm. A strict argument's blocked recursion survives
even when the callee ignores its value. The proof controls reject elimination
when its constructor choice depends on runtime data and reject a used field that
exists only inside erased evidence. Empty `Never()` and unreachable-index value
matches produce bottom, while an empty match with a reachable constructor fails.

Do not add 0-CFA, first-class function types, closures, or existential closure
packages for this slice. If closures are ever demanded by a concrete program,
the considered representation is an existential package
`exists capture. (capture, (Input, capture) -> Output)`: the closure as a whole
is not presented as a function type. Only then revisit how indirect callee sets
are discovered.

## Closed: honest top-level declaration surface

- [x] Parse enums, proof families, value functions, and proof functions in any
      source order instead of closing a declaration category forever when the
      next category begins.
- [x] Make `run` optional for definition-only documents while retaining at most
      one explicit named block for executable bindings and assertions.
- [x] Preserve source-ordered CST declaration ranges and reject a second `run`
      with a direct diagnostic.

Exit test: `top-level-declarations.fine` places `Plus` after `even_pred`, omits
`run`, roundtrips byte-for-byte, and verifies both proof functions. A generated
two-run control fails at the second declaration.

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
- [x] Give each static constructor actual value/proof parameters, optional
      proof-irrelevant `takes` demands, and an exact indexed result type.
- [x] Form nested constructor evidence while checking every explicit proof
      parameter, contextual proof demand, and result index by same-manager AST
      identity.
- [x] Retain family declaration, constructor application, and evidence formation
      separately in Rainfall; create neither a Z3 runtime datatype nor a runtime
      proof value.
- [x] Reject a wrong result index, a wrong recursive premise, and use of a proof
      constructor as a runtime function.

Exit test: `proof-inductive-even.fine` forms `Even(zero)` and then
`Even(succ(succ(zero)))` with `even_next(zero)`, resolving its `prior` coeffect
from exact local evidence. The family index is an ordinary runtime `Nat`; the
inhabitants and constructor remain static.
This closes introduction only. Proof matching, induction hypotheses, and holes
must be added as separate slices rather than inferred from these declarations.

## Closed: indexed proof-family match

- [x] Allow checked proof-function bodies without creating runtime functions.
- [x] Match only on indexed proof evidence and return only proof evidence.
- [x] Unify constructor results before checking arms, refine symbolic indices,
      bind actual parameters positionally, and reintroduce each constructor
      coeffect under its declared name.
- [x] Compute exhaustiveness after refinement; reject both missing reachable
      arms and supplied unreachable arms.
- [x] Admit zero-constructor families and zero-arm elimination, including an
      impossible concrete index of a nonempty family.

Exit test: `proof-inductive-match.fine` makes `refl(value)` inhabit a different
identity type in each `Even(value)` arm, uses `previous` and `prior`, eliminates
`Never()`, and accepts no arms for `Even(succ(zero))`. The two rejecting fixtures
separate non-exhaustiveness from an explicitly written impossible arm.

## Closed: first structural proof induction

- [x] Add `inducts(evidence)` only to body-bearing proof functions and require
      the named parameter to carry indexed-family evidence.
- [x] Expose a proof function to its own body only as an induction hypothesis.
- [x] Mark same-family fields from a match with their structural root and exact
      parent; propagate that ancestry through nested proof matches.
- [x] Accept a self-application only when the designated proof argument names an
      exact descendant, never the root evidence or an arbitrary proof expression.
- [x] Retain the function/root/parent/field edge in Rainfall without creating a
      runtime recursive call.
- [x] Force two recursive constructor coeffects to produce two distinct IH-use
      edges and require both explicit results in the target constructor.

Exit test: `proof-inductive-induction.fine` recursively rebuilds an `Even`
derivation. `reject-nondecreasing-proof-recursion.fine` passes the root evidence
again and fails the descent check; `reject-recursion-without-inducts.fine` cannot
name itself at all. `proof-inductive-branching-induction.fine` additionally
forces independent left/right IH uses under one parent. Numeric measures and
runtime recursion remain separate work.

## Closed: exact indexed proof holes

- [x] Admit exact lexical evidence at an indexed-family `?`.
- [x] Under `inducts(evidence)`, infer direct result indices and enumerate only
      self-applications whose designated argument is a structural descendant.
- [x] Exclude wrong-index locals and nondecreasing roots before candidate events.
- [x] Retain typed candidates, exact selection, and complete residual frontier
      in Rainfall; materialize and rerun with proof search forbidden.
- [x] Reject an empty indexed grammar and reject use of the identity-only Z3
      selector rather than changing the candidate language silently.

Exit test: `proof-inductive-holes.fine` selects
`rebuild(previous) using [evidence = prior]` inside the recursive arm and
`zero_even` at run level.
`reject-empty-inductive-hole.fine` removes `inducts`, leaving a wrong-index local
but no admissible candidate. Constructor synthesis is not part of this slice.

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

- [x] Materialize a bounded partial identity proof as ordinary Fine with nested
      typed holes; rank closed frontier obligations ahead of decorative syntax,
      reparse the fixed subtree without absorbing open evidence, and resume the
      nested hole on a second checkpoint pass.
- [x] Drive fallback checkpointing from cooperative source epochs in a disposable
      browser worker; stop by killing the in-flight epoch and commit only the
      last validated snapshot as one undoable document transaction.
- [x] Prototype nonblocking live lifting for an unbounded solver run. The active
      solver thread must never wait for Fine rendering: each observed term gets
      a monotone sequence number and an independently owned snapshot suitable
      for a Wasm pthread, ownership passes through a bounded queue, and the Fine
      lifter frees that snapshot immediately after exact lift/reparse/reify
      validation. Do not retain a run-lifetime arena under infinite fuel and do
      not access one Z3 manager concurrently from two threads. A deliberately
      slow lifter must not extend solver time; cancellation must preserve the
      last validated source while safely discarding queued intermediate views.
- [x] Build and serve a separate pthread Wasm variant behind cross-origin
      isolation, with feature-detected fallback to the single-threaded module
      and a shared-memory smoke that runs the two C++ worker threads.
- [x] Connect the pthread pipeline to an open-ended iterative proof producer.
      Queue exact translated model snapshots, publish through bounded shared
      memory while the worker event loop is inside Wasm, and retain a view only
      after a second Fine module reparses and rechecks its full source. Stop must
      kill the producer before installing that last validated view.
- [x] Replace open-ended live search's per-cost candidate-tree enumeration with
      direct instantiated-production discovery and exact finite-state datatype
      grammars. Budgets one through four must remain byte-identical to the
      enumerated oracle, including equal-rank deterministic selection.
- [x] Keep one context-bound model selector across live costs. Canonically order
      productions, retain all state sorts when that vector is unchanged, and
      record resets and reused-state counts in Rainfall. The cost-four
      discriminator must reuse its 120 cost-at-most-three states.
- [x] Profile state-family resets before adding datatype versioning. On the
      checkpoint discriminator, production growth could retain 6 prior states
      while building the 28-state budget-two graph, then 22 prior states while
      building the 120-state budget-three graph; once the expensive family
      stabilizes, the existing path already reuses all 120 states. Keep
      the reset until a large late-growing grammar or browser profile justifies
      stable production IDs and transitive parent versioning.
- [x] Replace the one-shot selector's enumerated reference frontier with direct
      typed production discovery. Retain every structured state transition in
      Rainfall, validate its type/cost/score recurrence during replay, and name
      the graph minus the lifted tree as the compact complete residual.

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
- [x] Recover the first ordinary consumer: typed value-function counterexamples
      with completed inputs/result, source print/parse/reify identity, and a
      fresh check against the original guarantee under declared coeffects.
- [ ] Recover any further ordinary model/counterexample consumer only when a
      concrete source program needs it; do not port old `check` or model holes
      wholesale.
- [x] Connect completed source proof materialization to one atomic CodeMirror
      transaction; require one undo to restore the exact source bytes.
- [x] Feed an interrupted partial checkpoint through the same editor transaction;
      never expose the in-flight worker's Z3 or MEMFS state.
- [x] Preserve earlier completed concrete edits in every later-hole live snapshot.
      Search holes in source order, reparse each cumulative source, and make the
      pthread smoke reject a final mailbox view that drops the first replacement.
- [x] After the proof syntax, Rainfall schema, and materialization contract
      survive a two-child proof function and the bounded Z3 selector, build the
      first browser vertical slice: the real Fine core in WASM, CodeMirror 6,
      and `identity-symmetry.fine` showing its candidate frontier, materialized
      edit, and search-free rerun. Publish only then at `fine.shit.yachts`.
