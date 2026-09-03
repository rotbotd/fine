# Fine proof-term roadmap

This roadmap records the destination chosen in the design conversation that led
to `fine/proof-terms`. It is ordered by executable slices, not by surface-feature
count. A slice closes only when its positive fixture, rejecting control, Rainfall
record, materialized source, no-search rerun, and browser language reference
agree.

## The turn

The former branch represented `Step(a, b)` and other propositions as `Bool`.
That made formulas such as `Step(a, b) <==> Step(b, c)` easy to submit to Z3,
but it left no Fine object inhabiting either side. The compiler could check
membership and own an induction branch table; it could not synthesize, retain,
or materialize the derivation itself. Spacer lemmas were useful observations of
search, but experiments showed that they projected away constructor support and
could not honestly be read back as source proofs.

Fine therefore changed destination rather than attaching proof syntax to that
representation. A proposition is now a proof type, and evidence for it is a
static source object. The current first proposition is
`Id(A, left, right)`. Bool-valued predicates from the old branch are a preserved
experiment, not the base of the new language.

The destination has four fixed parts:

1. **Two levels by construction.** `ValueType` and `ProofType` are disjoint;
   `ValueTerm` and `ProofEvidence` are disjoint. Runtime values have no proof
   variant, so erasure is not a later pass that can forget a case.
2. **Absorption without loss of source identity.** Introducing evidence for
   `Id(A, x, y)` contributes `x == y` to the lexical Z3 context. The evidence's
   source span, name, constructor, and provenance remain separately available to
   materialization and Rainfall.
3. **Caller-local coeffects.** A function declares proof demands with `takes`.
   Its body is checked under those hypothetical propositions. A call must provide
   matching evidence explicitly or find it among exact lexical caller bindings;
   there is no global instance or typeclass search.
4. **Inspectable synthesis.** Fine, not Z3, owns the grammar of source proofs.
   Z3 may reject candidates and discharge their obligations. Z3 propositions,
   fixedpoint summaries, and native unsat proofs are never presented as Fine
   proof terms.

Proofs are irrelevant to program execution, not interchangeable as source
records. Fine will not define equality between proofs in this line. Two pieces of
evidence for the same proposition may retain different source provenance, while
no Fine program can branch on that difference or return it as runtime data.

## Slice 0 — the boundary is executable (closed)

The current fixture is deliberately small:

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

This closes the initial boundary because the guarantee needs the absorbed
equality, the caller resolves `same` from exact local evidence, and
`fine materialize` writes `using [same = p]` before reparsing and rerunning with
implicit resolution disabled. Controls reject a missing coeffect, an unjustified
function, and a proof name used as an `Int`. Both the run boundary and runtime
close report zero proof values.

## Slice 1 — typed identity holes (closed)

Add a source hole whose expected type is already known to be an identity proof.
The first grammar has only two constructors: select exact local evidence, or form
`refl(value)` when the expected endpoints are the same manager-local value. A
candidate that cannot have the expected proof type must never enter enumeration.

Rainfall gives proof search its own events: hole opened, typed production
considered, candidate selected, and residual frontier. These events point back
to the source hole; ordinary Z3 observer traffic remains adjacent evidence
rather than being relabeled as synthesis.

Exit conditions:

- one hole selects an existing local proof;
- one hole constructs `refl`;
- a mismatched endpoint control exhausts the finite grammar and fails;
- materialization replaces the hole with the selected source term;
- the materialized file reparses and passes with proof search forbidden.

Closed by `identity-holes.fine`: `self` materializes as `refl(x)`, `copied`
selects `self`, and the earlier `other : Id(Int, y, y)` never appears in the
candidate stream. The close events retain zero and one residual candidates
respectively. `reject-empty-proof-hole.fine` has distinct exact endpoints and
fails with no well-typed production.

This slice tests the actual Fine gimmick without first inventing inductive
propositions, tactics, or a large term language.

## Slice 2 — named proof functions and composition (in progress)

Identity search becomes useful when its grammar can apply declared proof-level
functions such as symmetry and transitivity. These declarations live at the
proof level: their parameters and results are evidence, they may use absorbed
identity facts while checking, and they cannot be called from runtime value code.

This is also the proof-term answer to the original iff question. `P <==> Q` is
not equality between two Bool terms; its evidence contains a proof function from
`P` to `Q` and another from `Q` to `P`. Both directions are static and may be
searched or materialized independently.

Application enumeration is type-directed from the requested result. Fine should
instantiate only declarations whose result can be the exact expected proof type,
then recursively fill their proof arguments under a finite depth or cost bound.
There is still no search by theorem name, global registry, or arbitrary solver
entailment.

Exit conditions:

- a symmetry fixture can only close by applying a named proof function;
- a transitivity fixture records the two distinct input proofs and their composed
  result;
- a cyclic proof-function grammar terminates at the declared bound;
- the chosen application tree materializes and reruns without search.

This slice is closed. Proof functions use explicit static indices and
virtual `takes` parameters; their result proposition is checked under absorbed
inputs before the declaration enters search. `identity-symmetry.fine` can close
its reversed identity only with `symm[x, x == true](p)`, retains one nested
application as residual frontier, materializes exactly, and reruns without
search. `reject-cyclic-proof-search.fine` establishes that a recursive grammar
with no base inhabitant stops at cost three. `identity-transitivity.fine` then
recovers the result-absent middle index by matching lexical child proof types and
retains `p` and `q` distinctly in the single cost-three tree. Removing `q` leaves
only a cost-four reconstruction and fails, so search cannot accept marginal
support from one premise. Both materialized fixtures rerun without search.

The bounded model-selector follow-up is also closed. Fine compacts the exact
deterministic candidate trees into ground recursive datatype productions, asks
Z3 for a `well` tree at cost at most three, and lifts by datatype constructor
identity. On the transitivity fixture the model is exactly
`(apply-trans local-p local-q)`, lifting to the sole reference candidate. Rainfall
keeps grammar, model solve, lift, ordinary selection, and residual closure as
separate events; materialization reparses and reruns without either search. The
enumerator remains the reference and still computes the full frontier in this
slice, so this closes the semantic integration boundary rather than a
scalability claim.

## Slice 3 — proof-only elimination

Introduction and absorption should carry the language until a fixture genuinely
needs to consume proof structure. At that point Fine may add elimination whose
result is another proof. The first eliminator should expose exactly one missing
operation, such as transporting a proposition along identity, rather than adding
a general dependent match.

`identity-congruence.fine` establishes that ordinary congruence does not yet
force this slice. A checked proof function can absorb `Id(Bool, left, right)` and
establish identity after both values occur beneath another equality. Search
recovers the hidden indices from the local proof, and the selected application
materializes exactly. Do not add an eliminator merely to rename this behavior;
wait for a proof whose constructor must actually be consumed.

The hard boundary is syntactic: a proof eliminator cannot produce a `ValueTerm`,
and runtime function bodies cannot inspect constructors of `ProofEvidence`. A
negative fixture must attempt both and fail before lowering. Rainfall may retain
which evidence an eliminator used, but generated runtime code remains unchanged.

Exit conditions:

- one proof requires identity transport rather than mere context absorption;
- the transport term is retained and materialized;
- proof-to-value elimination is rejected by the AST/elaborator boundary;
- two distinct proofs of the same proposition cannot cause different runtime
  results.

## Slice 4 — inductive propositions from birth

Only after identity proof search and elimination work should Fine introduce an
indexed inductive proof type such as `Step(term, next)`. Its constructors create
derivation evidence. Constructor premises are proof fields, not Bool premises
that the compiler later decorates with imaginary evidence.

The ordinary runtime-data prerequisite arrived independently before this slice.
`enum` now declares a closed Z3 datatype with typed payloads and recursive self
fields. Runtime `match` is exhaustive and binds the datatype accessors in each
arm. `runtime-enum.fine` checks the boundary with recursive `Nat`: its values may
index or carry identity proof types, but its constructors remain `ValueTerm`s and
identity evidence remains unrepresentable at runtime.

This slice adds the deliberately separate `proof inductive` form for static
indexed constructor families. It must use ATS's split: ordinary values carry the
runtime representation, while constructor result indices and proof premises live
only in the static proof layer. This does not turn a family into a Bool predicate
and does not introduce a runtime proof datatype.

Constructor introduction is now executable. `proof-inductive-even.fine`
declares `Even(value: Nat)` with a base constructor and a recursive constructor
whose virtual premise is `Even(previous)`. The constructor application
`even_next[zero](zero_even)` forms evidence only when its proof field and exact
result index agree. Rainfall retains declaration, constructor application, and
formation separately while reporting that no runtime datatype or proof value was
created. Controls reject a wrong index, a wrong recursive premise, and a proof
constructor called from runtime value code.

Proof-producing match is now executable. A body-bearing proof function may
scrutinize indexed evidence; constructor-result unification refines symbolic
indices before each arm is checked and binds static constructor parameters and
virtual proof fields separately. `proof-inductive-match.fine` forces both
refinements with `refl`, consumes the recursive `prior`, eliminates a
zero-constructor family, and omits both impossible constructors of
`Even(succ(zero))`. Exhaustiveness ranges over reachable constructors.

The first structural induction boundary is now executable. A proof function may
write `inducts(evidence)`, and a self-application is accepted only when its
designated argument is an exact same-family proof field descended from that
evidence. The recursive source application elaborates to an induction-hypothesis
edge, not a runtime call. `proof-inductive-induction.fine` rebuilds one indexed
derivation; controls reject both the root evidence passed again and recursion
without an `inducts` clause. Rainfall retains the root, immediate parent,
recursive field, and function separately.

The branching control is also closed. `proof-inductive-branching-induction.fine`
matches a binary-tree derivation whose node owns two same-family proof fields;
the target constructor requires both recursive results. Rainfall retains two
distinct IH edges, `left_grows` and `right_grows`, under the same exact parent,
so one child cannot stand in for joint support.

Typed holes inside recursive arms are now closed for the exact first grammar.
They enumerate matching locals and structurally admitted IH applications only;
wrong-index locals and nondecreasing roots are absent before enumeration.
`proof-inductive-holes.fine` uniquely selects `rebuild[previous](prior)`, retains
the typed frontier in Rainfall, materializes, and reruns without search. A second
hole selects exact local indexed evidence. Constructor synthesis and the Z3
datatype-model selector remain separate slices.

Fine may lower the proposition to a Z3 relation for checking and search, but the
source constructor table owns branch identity, field scope, recursive-premise
links, and induction-hypothesis links. Solver lemmas remain summaries. If Z3
projects two recursive premises into a unary invariant, Rainfall must show the
projection without losing the two source proof fields or claiming the projection
is their derivation.

Proof matching is initially proof-producing only. Runtime matches over ordinary
value ADTs are a separate value-language feature; the two constructs must not
share a representation merely because both have branches.

Exit conditions:

- constructors form and check indexed evidence;
- a proof match retains the exact constructor and field evidence used;
- a two-recursive-premise constructor retains two distinct induction hypotheses;
- an impossible constructor branch is rejected without fabricating a typed
  counterexample from a solver summary;
- the same proposition cannot be used as a runtime Bool or runtime value.

## Slice 5 — locally nameless STLC

The first serious consumer is locally nameless STLC, rebuilt against proof terms
rather than ported mechanically from the old branch. Ordinary value data supplies
terms, types, environments, opening, and free-name support. Indexed proof types
supply `HasType(...)` and `Step(...)` derivations. Progress and preservation are
proof functions over those derivations.

The abstraction rule is the forcing case. A cofinite premise is a total
proof-level field over arbitrary admissible fresh names, not one lucky free
constant and not an existential witness. Availability of a fresh name, the
arbitrary branch binder, the typing-field binder, the opened term, and the
recursive proof must remain distinct source-owned objects. When freshness domains
differ, a checked opening/renaming transport proof must move between them; Fine
must not identify binders to make the fixture pass.

Search begins with user-written theorem shape and outer matches. Fine fills typed
proof holes inside constructor arms and exposes the frontier in Rainfall. The user
can interrupt, add a match, assertion, or proof application, and resume. Once the
proof closes, materialization makes the next run verification-only.

Exit conditions:

- `HasType` and `Step` are indexed proof types rather than Bool predicates;
- one abstraction constructor carries a genuinely total cofinite proof field;
- progress and preservation retain constructor/IH ownership through Rainfall;
- a deliberately unequal freshness-domain fixture needs explicit transport;
- a false preservation arm fails at that arm rather than at an opaque root query;
- one completed theorem materializes and reruns without synthesis.

## Slice 6 — interruption and editor transactions

Long-running proof search earns an interactive surface only after a finite hole
search is truthful. The runtime then needs three named pieces: a cancellable
search episode, a persistent typed frontier, and an atomic materialization
transaction. Cancellation must not mutate the source; resumption must begin from
the recorded typed frontier rather than an invented replay of Z3 internals; and
materialization must commit one reparsed, rechecked document revision.

The first checkpoint boundary is closed. `fine checkpoint --proof-budget n`
adds typed open leaves to the identity grammar, ranks partial trees by completed
root / closed frontier / constructor cost / grammar order, lifts the selected Z3
datatype model to ordinary nested `?` syntax, and reparses it. Open evidence is
never absorbed and later statements are not checked under it. A second command
resumes the nested hole and can close the same document. `identity-checkpoint`
forces one child of transitivity to close while the other remains open; at a
shallower budget the unchanged root hole beats decorative unary structure.

The editor transaction primitive is closed for ordinary completed
materialization: the Wasm CLI writes exact bytes to MEMFS and CodeMirror installs
them as one undoable change. Cooperative checkpoint epochs and interruption are
also closed in the browser. A disposable worker repeatedly materializes and
validates the previous epoch; each completed source is posted, while stop
terminates the in-flight epoch before the main thread commits only the last
posted source in one undoable edit. The source checkpoint, rather than a private
Z3 state, is the persistent frontier. Each source post carries Rainfall emitted
by that same elaboration, and the browser updates the trace pane only at this
paired boundary.

This is where Rainfall becomes an editing instrument rather than a log viewer.
The user should see which source hole and production Fine is working on, not a
story inferred from solver callback order.

The later live boundary is deliberately different from these source epochs. A
long-running Z3 search may continue on one solver-owning thread while a second
Fine worker trails it, lifts observed terms, and replaces only validated source
for the reader. Callback order receives monotone `(run, sequence)` identities;
presentation may lag and may drop intermediate frames, but it may not reorder
them or invent a solver state.

This requires ownership transfer, not concurrent use of the active Z3 manager.
Every queued observation must own a self-contained term snapshot whose storage
is released by the Fine lifter after `lift`, rendering, reparsing, reification,
and exact identity validation. In particular, an infinite-fuel search must not
accumulate observations in an arena whose lifetime is the solver run. The queue
is bounded, the last validated source/checkpoint is never dropped, and stopping
must be safe while the lifter trails the solver. Before changing the playground,
a native stress fixture must show that an artificially slow lifter does not
lengthen solver time and that cancellation causes neither concurrent-manager
access nor use-after-free; the same ownership code then moves to an Emscripten
pthread build with the required browser isolation headers.

## Slice 7 — recover value-language consumers when forced

Tuples, enums beyond the minimal STLC data, refinements, arrays, models, and
typed counterexamples return one consumer at a time. Each addition must preserve
the two-level boundary and exact generated-term validation. None is restored
merely because it existed on the old branch. The STLC fixture decides the first
required value features.

## Explicit non-goals

- no equality between proofs or higher identity types in this roadmap;
- no runtime proof values and no late erasure pass;
- no proof elimination into runtime data;
- no global typeclass, instance, or theorem search;
- no general universe of Fine types encoded as Z3 terms merely to simulate
  dependency;
- no arbitrary user-defined object-language semantics or raw Horn-rule surface;
- no reconstruction of constructor derivations from Spacer lemmas or Z3 unsat
  proofs;
- no compatibility port of the former Bool-predicate system without a new
  proof-term consumer.

## Preserved reference and review rule

The complete former implementation remains on public `main` and annotated tag
`pre-pat-1d7222a23`. Its locally nameless experiments, compiler-owned induction,
Rainfall traces, and failures are evidence for the new work, not code that must
survive unchanged.

At every slice, review asks one question first: does the representation make the
claimed behavior impossible to fake? A green solver result is not enough. The
source proof must exist, remain typed and owned through Rainfall, materialize as
Fine syntax, and verify again after search is disabled.
