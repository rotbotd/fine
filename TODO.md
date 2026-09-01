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

## Next synthesis test, if retained: failure-directed lemmas

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
