# Fine development log

This file is append-only. It records implementation history, experiments, exact
coverage claims, and closed failures. The current architecture belongs in the
project documentation; Lynn's runtime prompt should retain only the present
invariants, runnable state, and unresolved edge.

## 2026-08-31 — first model-to-syntax slice (`82ad50742`)

The first runnable vertical slice reused Z3's manager-local term identity and
closed the required `reify(lift(x)) = x` direction for a finite model witness.
Numeric Z3 AST IDs were retained only as diagnostics; live terms were held by
strong `z3::expr` references behind never-reused manager-scoped handles. The
initial internal mapping, SMT-LIB fixture, runtime identity spike, and rainfall
schema were added below `fine/`.

## 2026-08-31 — source parser and bisimulation runtime (`377ae5cc3`)

The hardcoded demo was replaced by a source-spanned parser and runtime.
`fine run fine/fixtures/two-state-bisim.fine` parses enums, Boolean tables, one
model-shaped table hole, and `proof`/`takes`/`gives`; `fine demo-bisim` embeds and
runs that checked-in source. Z3, with quantified two-directional clauses and
MBQI enabled, fills an array-shaped two-state bisimulation hole. Fine enumerates
the four finite cells into a deterministic constant-array-plus-stores term,
lifts it to Fine table syntax, reifies it, and checks exact same-manager AST
identity.

## 2026-08-31 — native synthesis slice (`cb66459fa`)

A twelve-angle pressure test was recorded and the first native synthesis path
was closed. Model witnesses print as typed, parseable `model` declarations, pass
through the ordinary parser/elaborator, and reify to exact same-manager AST
identity. Lifted enum values retain resolved enum identity rather than only case
text.

`fine run fine/fixtures/synth-max.fine` parses a generic Int `synth` plus
`ensures`, fairly enumerates Fine's built-in integer terms by exact size, grows
labelled failing instances, takes the unsat core, assembles the Reynolds-style
conditional, independently checks the untouched specification, then parses and
reifies the printed body. It returns
`if right >= left { right } else { left }`; projection and three-argument
fixtures are anti-hardcoding gates. Fine keeps one built-in semantics. It does
not expose user-defined object-language semantics, evaluators, raw Horn clauses,
or a SemGuS surface; SemGuS is only a source of internal backend ideas.

## 2026-08-31 — first public rainfall replay (`a1a13ab71`)

`fine rain fine/fixtures/synth-max.fine` began streaming JSONL for the native
synthesis run: public query boundaries and polarities, completed counterexample
assignments, deterministic candidate selection, labelled instance activation,
the unsat core, conditional assembly, one public simplification, a fresh
independent counterexample query, and the parse/reify-checked Fine source
witness.

The v2 schema uses six semantic kinds: object, scope, constraint, derive,
transform, and transition. Every event states producer coverage and uses
explicit evidence references rather than treating sequence as cause. Each term
is held by a strong `z3::expr` reference behind `(run, recorder, manager,
handle)`. Model evaluation is labelled equality under that completed model,
never a theory rewrite. This slice explicitly did not claim Z3-internal rewrite
or solver-search coverage.

## 2026-08-31 — scoped internal theory-rewrite producer (`ff74d15fb`, `38eb6d205`)

The generic Z3 commit added a synchronous optional observer to `th_rewriter`;
the separate Fine commit attached it only to native synthesis's postprocessing
simplifier. The max replay gained four non-reflexive `z3.theory-rewrite` events:
`right >= right -> true`, `right == right -> true`, `(... || true) -> true`, and
the final conjunction collapse.

Each event holds both same-manager endpoints strongly and records family,
declaration, continuation status, and exact producer coverage. Coverage is
limited to successful `th_rewriter_cfg::reduce_app` applications after child
rewriting, including push/pull-ITE work performed at that wrapper. It excludes
substitutions, variables, quantifiers, macros, cache hits, other rewriter
instantiations, and solver search. The generic Z3 observation hook and Fine
integration remain separate commits and patch series.

## 2026-08-31 — bisimulation public-boundary replay (`ff60e1c0c`)

`fine rain fine/fixtures/two-state-bisim.fine` emits 35 JSONL events: four
elaborated constraints, the MBQI-enabled public model query, four completed
finite-cell evaluations, deterministic false-default-array-plus-true-stores
extensionalization, and the parse/reify-checked Fine model witness. Model cells
are explicitly equality under the returned model. No solver-internal events are
emitted, so the replay does not claim which quantifier instantiations, MBQI
steps, or search decisions caused `sat`.

The clean Nix artifact was
`/nix/store/jzhnh4vx0jbd5ciwyk2iv3xc4j3x2fz8-fine-0.0.1`.

## 2026-08-31 — accepted MBQI-instance replay (`ce59633d9`)

The bisimulation query now disables E-matching, preserves three source qids
(`fine.bisim.labels-agree`, `fine.bisim.left-step-matched`, and
`fine.bisim.right-step-matched`), and attaches a non-blocking user propagator to
Z3's existing `qi_queue::on_binding` callback. No new generic Z3 hook was needed:
the public callback already sits after the redundancy checker and
rewrite-to-true rejection and before lemma insertion. Fine records each accepted
nontrivial ground instance as `z3.mbqi-instance`, with the preprocessed
quantifier, bound ground terms, preserved source role, and explicit
`mbqi-only-query` attribution.

The two-state fixture now emits 50 events. Six accepted instances occur before
the public `sat` result: one labels-agree instance, three left-step-matched
instances, and two right-step-matched instances. This is evidence that these
ground instances entered the query, not a claim that any one instance caused
the result. The callback does not expose candidates discarded before
`on_binding`, the auxiliary model-checker context, quantifier blocking, or CDCL
search decisions; fresh auxiliary contexts intentionally receive a no-op
observer because they use a different AST manager.

Validation used the local build plus both runnable slices, then the full clean
flake build:

```
cmake --build build/fine
./build/fine/fine run fine/fixtures/two-state-bisim.fine
./build/fine/fine rain fine/fixtures/two-state-bisim.fine
./build/fine/fine run fine/fixtures/synth-max.fine
nix build --no-link --print-out-paths
```

The flake checks assert MBQI enabled and E-matching disabled, a nonempty instance
set, all instance events preceding the result, the exact producer and engine,
and the exact three source roles. The clean artifact is
`/nix/store/m29hxiapp2awjz3ang87wvn5byy5p9qf-fine-0.0.1`.

## 2026-09-01 — source-level counterexample loop (`deaf510e8`)

The pitch's missing third result path is now executable. A `check` declaration
has typed Int/Bool parameters, an `assumes` block, and an `ensures` block. Fine
asks one public query for assumptions conjoined with the negation of all
guarantees. `unsat` reports no counterexample; `sat` completes each parameter
under the returned model and emits a typed, parseable `counterexample`
declaration. The latter is deliberately a returned witness envelope rather
than another executable declaration.

Each primitive assignment is lifted from the model value, printed, parsed by
the ordinary declaration parser, elaborated in the same manager, and checked
for exact AST identity. Negative integer literals were added to the source
grammar specifically so a Z3 negative numeral remains a numeral rather than
being disguised as a subtraction expression. The refuting fixture returns
`a: Int = -1` and `b: Int = 1` for a false subtraction lower-bound claim; a
separate addition fixture closes the no-counterexample path.

The 17-event rainfall for the refuting fixture contains the source-level
refutation assertion, public counterexample query, `sat` result, both completed
model assignments, the parsed Fine witness, and acceptance. Assignment values
are labelled equality under that model. There is no claim of minimality and no
coverage of arithmetic propagation, conflict analysis, model construction, or
other solver-internal search. The flake checks enforce event order, polarity,
outcome, assignment names and provenance, negative-literal source output, exact
round-trip identity, and the absence of the unrelated theory-rewrite and MBQI
producers.

Validation commands were:

```
cmake --build build/fine --target fine-bin -j2
./build/fine/fine run fine/fixtures/check-counterexample.fine
./build/fine/fine run fine/fixtures/check-valid.fine
./build/fine/fine rain fine/fixtures/check-counterexample.fine
./build/fine/fine run fine/fixtures/synth-max.fine
nix build --no-link --print-out-paths
```

The local Debug build's `.ninja_deps` repeatedly reported `premature end of
file; recovering`, which caused otherwise unnecessary full Z3 recompiles. Two
such recompiles completed successfully; a third redundant one was interrupted.
The isolated Nix build and all install checks completed cleanly, producing
`/nix/store/xlfz52hd2hl15rl6hy76lgs554mvvf2h-fine-0.0.1`.

After the clean artifact closed the slice, `ninja -C build/fine -t recompact`
recovered and rewrote both local Ninja databases. A following
`ninja -C build/fine -t deps` produced no warning; no further full rebuild was
needed because the isolated Nix build had already passed.
