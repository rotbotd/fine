# Fine TODO

## Governing principle: interruptible solver evidence

Fine should let Z3 continue searching for a proof, model, or counterexample
without hiding the obligation behind one spinner. Rainfall makes that search
inspectable: every observed live Z3 term `x` is rendered as parseable Fine syntax
by `lift(x)`, and the trace validates exact same-manager
`reify(lift(x)) = x`. Rendering and provenance are independent. A solver-internal
term has a Fine rendering without being source code, a proof term, or a candidate
program.

At any time, the user can interrupt and constrain the next search by editing
ordinary Fine source: add an assertion, choose an induction, or provide a helper
lemma. Each edit creates a new exact source snapshot and solver generation; an
old Rainfall trace may remain visible as stale evidence but cannot make a claim
about the edited program. An explicit synthesis backend may also propose source,
but that is an additional search relation with a grammar and independent
verification. It is not implied by lifting ordinary solver traffic.

Models and counterexamples materialize as typed witnesses. Solver-produced code
or lemmas materialize only when a declared synthesis run binds a verified result
to an exact source replacement; later runs then reify and verify that source
instead of repeating synthesis. “Infinite fuel” names the interaction policy—an
ordinary query may be left running and interrupted at a useful boundary—not a
claim that Z3 normally constructs programs or that any query terminates.

## Closed prerequisite: total Rainfall lift

Rainfall originally retained strong same-manager handles but printed most
internal terms as Z3 text. Model, program, and counterexample values had exact
Fine lift/reparse coverage while the three internal observer boundaries did not.
This had to close before any source materialization could consume an observed
term.

- [x] Define parseable Fine forms for every sort, declaration, quantifier,
      application, proof-clause literal, recursive-function auxiliary, and
      generated symbol currently crossing the rewrite, accepted-instance, and
      clause observers.
- [x] Lift each term while its strong `z3::expr` and manager are live; serialize
      both the normalized Fine rendering and its independent origin.
- [x] Reparse and reify every serialized rendering during Rainfall validation and
      require exact AST identity, not equivalent pretty-printing.
- [x] Make projection and the live editor display the Fine rendering by default,
      with the Z3-internal/compiler/source provenance still visible.
- [x] Remove raw Z3 text as an admitted rendering path. Unsupported terms must
      fail at the coverage boundary and name the missing Fine form.

Exit test: the induction-length and two-state-bisimulation traces validate with
exact lift/reify identity for every declared term, contain no admitted raw-Z3
rendering fallback, and retain exactly the same source ownership edges as before.

Closed by the generated-term core: applications use declaration-bound Fine
names such as `_d_length_0`, `_d_is_nil`, and `_d_case_def_0_length`; numerals
carry explicit sort names; and quantified terms retain binder names/sorts,
weight, qid/skid, patterns, and no-patterns. Each declaration event keeps the
manager-local sort/declaration environment needed to parse it. Observer callbacks
only print and retain strong terms; exact reification is deliberately deferred
until the solver returns because callbacks may not construct ASTs. The raw Z3
printer survives only as a labelled diagnostic field and is never projected as
the term rendering. Ordinary user-surface resugaring remains later work; it must
consume this exact generated layer rather than replace it.

## Closed experiment: one materialized synthesis arm

- [x] Give an open source expression a stable, snapshot-scoped hole identity and
      a typed synthesis grammar.
- [x] Let a datatype match contain completed and open arms, and give each open
      arm a named Rainfall query scope with its own independently verified result.
- [x] Admit each solver-produced arm through the same AST constructors accepted
      from user source, assemble and verify the whole match, then materialize all
      open arms with one host-owned source transaction.
- [x] Re-run the materialized file and demonstrate verification with no grammar
      enumeration or synthesis query.

The fixture is `synth-match-open.fine`: `?payload` has the fixed
`fine.qf-lia-int.v1` grammar over `fallback` and the `some` field `value`. Its
admitted replacement is exactly `value`. `fine-rain-host materialize` applies
that replacement, advances the revision, issues a new generation, and the new
trace contains one whole-match verification query but no hole declaration or
candidate selection. This closes witness-to-source identity. Per-arm live
projection and cancellation are paused until a proof-directed consumer justifies
them; deriving `max` or filling an arithmetic match arm does not.

## Next language test: locally nameless indexed `Step`

Add one ATS-style ghost indexed predicate without adding a universal
`Term`/`HasType` encoding or changing ordinary Fine values into type-coded raw
carriers.

- [ ] Keep object `Tm`, `Ty`, `Env`, and `Name` as separate native Z3 sorts.
- [x] Parse and elaborate a strictly-positive constructor-generated indexed `predicate` whose
      constructor-specific result indices are native Fine terms. The first
      accepted constructors have only base and first-order recursive premises;
      reject a universal premise rather than miscompile it.
- [x] Demonstrate executably that a least native-sort Horn relation excludes
      constructor junk, intro-only SMT axioms admit junk, and a fresh variable
      present only in a Horn body means one working name rather than all fresh
      names (`fine/spikes/indexed-proof`).
- [ ] Admit a universally quantified constructor field by elaborating an
      arbitrary-fresh branch plus its explicit freshness/equivariance
      obligation, without exposing a raw Horn-clause surface.
- [ ] Add named constrained views over one existing native carrier. A use site
      must discharge the instantiated `requires` proposition by entailment; the
      callee receives it as erased evidence and Rainfall records its source.
      Do not add wrapper sorts, textual-premise matching, or general refinement
      subtyping.
- [x] Generate indexed pattern refinement and induction on a proof derivation,
      rather than structural induction on the indexed `Tm` value.
- [x] Admit one direct auxiliary predicate atom and one direct predicate goal in
      predicate induction through compiler-owned, one-constructor inversion and
      construction formulas. Preserve both exact resources in Rainfall and keep
      the recursive universal introduction-axiom matching loop as a rejected
      experiment.
- [x] Preserve an arbitrary constrained field inside those one-layer formulas as
      separately checked availability plus `forall binder. requirement ->
      recursive premises`; never turn the total field into one Horn witness.
- [x] Preserve the selected rule, opened bodies, arbitrary fresh name,
      recursive premise, and induction hypothesis as exact Rainfall evidence
      before erasing derivation witnesses from value/model construction.
- [ ] Verify preservation for full locally nameless reduction under
      abstractions, then rerun the admitted source without synthesis or hidden
      proof search.

Exit test: the abstraction-congruence constructor carries its arbitrary-name field
of recursive `Step` premises through elaboration, induction, solver checking,
exact Fine lift, and Rainfall projection. If that correspondence cannot close,
do not broaden the feature to runtime GADTs or a general static type universe.

The first-order sub-slice is closed by `predicate-step.fine`: `predicate`
constructors become rules of a registered Z3 least relation over the native
`Tm` datatype. A parameterless, assumption-free `check` can ask one ground
membership question. The contextual step is derived, `Step(atom, atom)` is not,
and Rainfall retains the relation, each constructor rule, exact query, public
fixedpoint answer, and erasure boundary. `reject-predicate-universal.fine`
rejects a constructor parameter absent from the result rather than treating a
body-only Horn variable as a universal field. This query form remains membership
only; derivation induction now has its own explicit `inducts(F(indices))` form.
Source proof construction, general constrained-view parameters, and inversion
remain later pieces.

The first open consumer is also closed. A parameterized `check` may assume
exactly one predicate atom and state ordinary Fine guarantees. Fine adds a fresh
counterexample relation and the rule `predicate(indices) && !guarantees -> Bad`,
then asks Spacer whether `Bad` is reachable. `predicate-invariant.fine`
proves that every contextual `Step` has unequal indices;
`predicate-invariant-false.fine` refutes equality. This is least-relation
invariant checking, not source proof elimination: a satisfiable query retains
Spacer's exact answer but does not yet lift a typed counterexample tuple.
Rainfall observes the public Spacer lemma-export, predecessor, and unfold
callbacks without assigning them source ownership or causal force. Explicit
constructor branches, refined indices, recursive hypotheses, and source proof
witnesses remain required before checking off derivation induction.

`predicate-two-premises.fine` pressure-tests one constructor whose body
requires two recursive `Step` atoms. Fine and Spacer verify the invariant, and
Rainfall retains the exact joined Horn rule with premise counts `2/2`. The
public lemma callback does not retain that join: Spacer currently exports the
marginal relation invariant `Step(x,y) -> x != y` plus query lemmas. This is an
intentional null result. Learned relation summaries cannot substitute for the
compiler-owned constructor branch and its two recursive evidence edges when
derivation induction is added.

The first-order derivation-induction sub-slice is closed by
`predicate-induction.fine`. `inducts(Step(before, after))` requires the exact
same atom first in `assumes`. Fine makes one compiler-owned branch per
retained constructor, substitutes that constructor's result indices into the
guarantee, and supplies one guarantee-shaped induction hypothesis at each
recursive premise's exact indices. The two-premise fixture retains two distinct
premise/hypothesis pairs in its `pairwise` branch and verifies all three
constructors; the false control identifies `root` as the failing branch. This is
real induction over Fine's constructor table, not a reconstruction from Spacer
lemmas. It still has no explicit source match body, proof-term value,
existential constructor field, or lifted branch counterexample.

Contextual derivation induction is closed by
`predicate-context-induction.fine`. Predicate indices remain the leading check
parameters; later parameters are universally generalized in every recursive
IH. Later `assumes` clauses become the IH antecedent and are independently
specialized to each constructor result in the branch query. The executable
`SameHeight` theorem needs the recursive IH at `ceiling - 1`; retaining only the
free source occurrence of `ceiling` cannot close it. Its false strict-ceiling
control fails at `root`. Rainfall retains the context arity, exact auxiliary
assumption, result-specialized branch assumption, qid-bearing universal IH, and
branch result. This admits the shape of a preservation context, but not yet its
substance in the contextual slice alone.

The minimal predicate-preservation skeleton is closed by
`predicate-preservation.fine`. `Marked(before)` is a second least
constructor-generated predicate. In the recursive `Step` branch, Fine inverts
`Marked(succ(succ(before)))` by one retained constructor layer, applies the
exact Step IH to obtain `Marked(after)`, and checks one constructor layer for
`Marked(succ(succ(after)))`. The odd-result control fails at `root`. Rainfall
retains the original assumption and goal, their separate inversion/construction
resources, both constructor alternatives, and the recursive IH. The first sound
implementation using universal constructor introduction axioms verified the
theorem but made the false control grow a recursive matching chain indefinitely;
the bounded formula replaces it rather than increasing fuel. This is the
preservation mechanism, not the full STLC theorem: typing/reduction predicates,
environment lookup, and beta substitution/opening lemmas remain unresolved.

The non-Horn secondary-predicate sub-slice is closed by
`predicate-arbitrary-preservation.fine`. Its `Marked.grow` constructor owns an
erased function field from the singleton view `At(value)` to
`Marked(token)`. One-layer inversion and construction retain the constructor's
existential `value` outside a separately available, universally bound `token`;
the premise therefore depends on the binder and cannot be replaced by a single
witness. The Step preservation theorem verifies, the odd-result control fails
at `root`, and the successor-witness control is rejected as unavailable before
vacuity can enter a branch. Rainfall retains one availability result and four
field-use events across the two Step branches. This supplies the general shape
needed to invert a typing predicate with a cofinite abstraction constructor. It
does not align that typing field's bound name with the independently arbitrary
name in the actual Step abstraction branch; the locally nameless preservation
fixture and the opening/equivariance use that forces the alignment remain next.

The target-predicate side is now genuinely total too.
`predicate-total-field-preservation.fine` gives both `Step.under_abs` and a
paired `Marked.under_abs` independent arbitrary binders over the same
`FreshFor` view. The theorem flips `Marked(before, after)` along a Step
derivation. Its abstraction branch can close only by instantiating Step's
`forall branch_name. FreshFor -> IH` at Marked's separately bound `fresh`;
retaining one free-name IH refutes that branch, as a rebuild of the predecessor
compiler confirmed. Rainfall keeps the individual IH template, availability
resource, and total IH separately, and the binder handles for Step and Marked
remain unequal. The diagonal false control fails at `root`. This closes total
target-field identity, not name transport: the two fields deliberately share a
freshness domain. The next fixture must make their requirements unequal so the
typing binder cannot directly instantiate the Step field and opening
equivariance is actually consumed.

The arbitrary-field mechanism now has a real locally nameless consumer at the
evidence boundary. `predicate-cofinite-support-induction.fine` represents
terms by bound indices, free integer names, application, and abstraction.
`open_at` increments depth below abstractions and replaces the selected bound
index by a free name; `support_cutoff` recursively computes a maximum containing
all free names. `FreshFor(before, after)` requires a name above both computed
cutoffs and proposes their maximum plus one as its separately checked
availability witness. `under_abs` then retains one arbitrary such name and the
exact recursive premise/IH over `open_at(before, 0, fresh)` and
`open_at(after, 0, fresh)`. Rainfall validates 395 events and keeps both recursive
function bodies, the support requirement, availability witness, distinct
arbitrary binder, two opened terms, premise, and IH as exact same-manager terms.
The older `FreshApart` and invalid-witness fixtures remain controls for the
generic mechanism.

This closes the trace-preservation item but not the quantified-field or STLC exit.
The direct name-choice theorem—renaming an opening at one above-cutoff name to a
second equals opening at the second—first exposed a matching loop in the old
guarded-universal induction translation. Rainfall showed 553 accepted instances
over only 64 distinct terms and more than 480,000 admitted/inferred clauses.
Fine now generates one branch per datatype constructor and one exact IH per
direct recursive field, generalized over the remaining check parameters. The
promoted `induction-open-equivariance.fine` verifies with three field-owned IHs
and 16 accepted instances instead of unbounded selector growth. The field can
now import that theorem through a deliberately small reusable-proof boundary.
`reusable-proof-induction.fine` verifies the opening-equivariance source proof
by constructor/direct-field induction, universally closes it only after
the counterexample query is unsatisfiable, and uses it to discharge a later
predicate constructor branch. Removing the proof makes the unassisted
recursive opening branch exceed the two-second control. Rainfall keeps one
exact admission edge and one exact branch-use edge. Full locally nameless
preservation still needs the actual typing/reduction predicates and the beta/opening
lemmas they force; the generic mechanism is no substitute for that
object-language work.

## Later language test: an elementary topos without ceremony

An elementary topos must be definable once and usable for generic theorems
without manually unpacking its operations and laws at every proof site.

- [ ] Add compile-time signatures with abstract native sorts, named first-order
      functions/predicates, views, and laws. Signature instances and parameters
      are explicit; do not add typeclass search or represent a signature as a
      runtime record of solver declarations.
- [ ] Represent `Arrow(A, B)` as a constrained view over one native `Hom` sort,
      discharged by `dom`/`cod`, rather than generating one Z3 sort per hom-set.
- [ ] Express category structure, chosen terminal objects and pullbacks, chosen
      exponentials with eval/curry, and a chosen subobject classifier. Keep the
      universal mediators named so ordinary use does not restart existential
      search for structure already supplied by the signature.
- [ ] Let a theorem take an explicit `E: ElementaryTopos` signature parameter;
      elaborate its members to fixed many-sorted first-order symbols and make
      the signature laws available as assumptions with Rainfall evidence.
- [ ] First exit theorem: prove internally that every arrow which is both monic
      and epic is an isomorphism, without restating or manually projecting the
      topos laws in the theorem body.
- [ ] Second pressure test, only after equality classes have an honest
      representation: construct the Heyting algebra of subobjects of a fixed
      object. Do not hide quotient/setoid obligations behind host-language
      equality or postprocess raw representatives.

The definition should read as the selected categorical structure and its
universal laws, not as generated SMT plumbing. Judge compactness by repeated
source ceremony, not line count: the solver trace may be large, but changing
from one elementary topos parameter to another must not duplicate the proof.

## Later synthesis test, if retained: failure-directed lemmas

Use Yang--Fedyukovich--Gupta's AdtInd mechanism as a test, not as a second proof
language: retain a stuck lifted residual; derive a local grammar from only its
variables, functions, predicates, and subterms; let the user narrow its template;
refute candidates cheaply on small constructor values; prove survivors in
separate generations; and materialize an admitted helper lemma into Fine source.
The first fixture should reproduce a recursive-list obligation that genuinely
needs a concatenation/length lemma, and the second run must verify without lemma
enumeration. Until that fixture exists, top-level `synth` is an experimental
QF-LIA regression surface, not part of Fine's language pitch. If proof-directed
lemma search does not reuse source expression holes, remove the public
declaration and retain the refutation engine only as an internal component.
