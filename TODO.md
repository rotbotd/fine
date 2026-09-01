# Fine TODO

## Governing principle: interruptible search that becomes code

Fine should let Z3 continue searching without requiring the user to finish a
proof or program skeleton first. Rainfall makes that search editable: every
observed live Z3 term `x` is rendered as parseable Fine syntax by `lift(x)`, and
the trace validates exact same-manager `reify(lift(x)) = x`. Rendering and
provenance are independent. A solver-internal term has a Fine rendering without
being falsely attributed to a source span.

At any time, the user can interrupt and constrain the next search by editing
ordinary Fine source: fill part of a function, add an assertion or helper lemma,
or split a parameter with a match and leave individual arms open. These are not
separate manual tactics. The same partial and completed Fine AST shapes belong
to the solver's synthesis grammar, so Z3 may eventually produce the match or
lemma that the user could have supplied earlier. Each edit creates a new exact
source snapshot and solver generation; an old Rainfall trace may remain visible
as stale evidence but cannot make a claim about the edited program.

When search succeeds, Fine materializes the lifted result as ordinary Fine code.
Later runs reify and verify that code instead of repeating synthesis. “Infinite
fuel” names this interaction policy—search may be left running and interrupted
at a useful boundary—not a termination claim about any individual Z3 query.

## Immediate truth gap: total Rainfall lift

Rainfall currently retains strong same-manager handles but prints most internal
terms as Z3 text. Model, program, and counterexample values have exact Fine
lift/reparse coverage; the three internal observer boundaries do not. Do not
build editable proof holes on the textual fallback.

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

## Next vertical slice: one interruptible match

- [x] Give an open source expression a stable, snapshot-scoped hole identity and
      a typed synthesis grammar.
- [x] Let a datatype match contain completed and open arms, and give each open
      arm a named Rainfall query scope with its own independently verified result.
- [ ] Split those arm scopes into independently cancellable host generations;
      the first slice still evaluates them sequentially inside one source-bound
      generation.
- [ ] Project the current residual Fine formula and observed activity onto each
      open arm without treating query scope as causal proof evidence.
- [ ] Interrupt one running arm, edit or fill another, and discard only
      generations bound to the predecessor snapshot.
- [x] Admit each solver-produced arm through the same AST constructors accepted
      from user source, assemble and verify the whole match, then materialize all
      open arms with one host-owned source transaction.
- [x] Re-run the materialized file and demonstrate verification with no grammar
      enumeration or synthesis query.

The closed fixture is `synth-match-open.fine`: `?payload` has the fixed
`fine.qf-lia-int.v1` grammar over `fallback` and the `some` field `value`. Its
admitted replacement is exactly `value`. `fine-rain-host materialize` applies
that replacement, advances the revision, issues a new generation, and the new
trace contains one whole-match verification query but no hole declaration or
candidate selection. The remaining edge is live residual projection and
per-arm cancellation, not witness-to-source identity.

## Failure-directed lemmas after the match slice

Use Yang--Fedyukovich--Gupta's AdtInd mechanism as a test, not as a second proof
language: retain a stuck lifted residual; derive a local grammar from only its
variables, functions, predicates, and subterms; let the user narrow its template;
refute candidates cheaply on small constructor values; prove survivors in
separate generations; and materialize an admitted helper lemma into Fine source.
The first fixture should reproduce a recursive-list obligation that genuinely
needs a concatenation/length lemma, and the second run must verify without lemma
enumeration.
