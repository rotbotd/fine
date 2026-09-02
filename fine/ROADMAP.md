# Fine proof-term roadmap

This roadmap records the destination chosen in the design conversation that led
to `fine/proof-terms`. It is ordered by executable slices, not by surface-feature
count. A slice closes only when its positive fixture, rejecting control, Rainfall
record, materialized source, and no-search rerun agree.

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
3. **Caller-local coeffects.** A function declares proof demands with `needs`.
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
virtual `needs` parameters; their result proposition is checked under absorbed
inputs before the declaration enters search. `identity-symmetry.fine` can close
its reversed identity only with `symm[x, x == true](p)`, retains one nested
application as residual frontier, materializes exactly, and reruns without
search. `reject-cyclic-proof-search.fine` establishes that a recursive grammar
with no base inhabitant stops at cost three. `identity-transitivity.fine` then
recovers the result-absent middle index by matching lexical child proof types and
retains `p` and `q` distinctly in the single cost-three tree. Removing `q` leaves
only a cost-four reconstruction and fails, so search cannot accept marginal
support from one premise. Both materialized fixtures rerun without search.

## Slice 3 — proof-only elimination

Introduction and absorption should carry the language until a fixture genuinely
needs to consume proof structure. At that point Fine may add elimination whose
result is another proof. The first eliminator should expose exactly one missing
operation, such as transporting a proposition along identity, rather than adding
a general dependent match.

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

This slice includes only the ordinary value data needed to index the first
proof family. Closed `Ty` and `Tm` value ADTs may therefore arrive here, driven
by the fixture; this does not require GADTs or proof-valued runtime constructors.

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

This is where Rainfall becomes an editing instrument rather than a log viewer.
The user should see which source hole and production Fine is working on, not a
story inferred from solver callback order.

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
