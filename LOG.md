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

## 2026-09-01 — admitted CDCL(T) clause stream (`1b352e8ea`)

The bisimulation rainfall now retains Z3's existing public `on_clause`
registration for the lifetime of its single solver query. No generic soft-fork
hook was needed: `Z3_solver_register_on_clause` already reports clauses after
preprocessing as they are assumed by, inferred into, or deleted from CDCL(T).
A dedicated `RainfallClauseObserver` owns both the C++ callback and registration
so neither can outlive the solver query.

The observer conservatively classifies only dummy proof-hint heads documented
by the callback boundary: `assumption` becomes `z3.clause.assume`, `del` becomes
`z3.clause.delete`, and every other head becomes `z3.clause.infer` while
preserving the uninterpreted head and full proof-hint term. Every proof hint and
literal enters the existing never-reused, strong same-manager registry. Clause
events also retain literal count and proof-log dependency indices. The
post-preprocessing literals are explicitly represented as Z3 clause terms, not
misprinted as Fine source.

On the two-state MBQI fixture, the trace grew from 50 to 169 events. The public
clause stream contains 54 events: 6 assumptions, 43 inferences, and 5 deletions.
Observed inference hint heads include `inst`, `rup`, `smt`, and `tseitin`; the
six independently observed `qi_queue::on_binding` MBQI instances remain. All
clause events precede the public `sat` result. This covers clauses admitted to
or removed from this parent CDCL(T) query, not rejected candidates, assignments,
decisions, watched-literal activity, the auxiliary MBQI context, or the causal
contribution of one clause to the result.

The first isolated Nix build failed only in the install-check harness: passing
the now-large JSONL trace through the `RAIN` environment variable exceeded the
kernel argument/environment limit (`python: Argument list too long`, exit 126).
The trace itself was valid. The test was corrected to write the bisimulation
rain to a temporary file and pass only its path to Python. The second clean
build completed all runnable and structural checks.

Validation commands were:

```
cmake --build .build -j4
./.build/fine rain fine/fixtures/two-state-bisim.fine
nix flake check
nix build --no-link --print-out-paths
```

The flake test requires assumed, inferred, and deleted clause events; the
`assumption`, `inst`, and `del` proof-hint heads; event placement before the
query result; exact producer attribution; strong declared handles for every
literal and proof hint; and literal-count agreement. The clean artifact is
`/nix/store/l08m34rhzwh1xyw9n9kkz2w4a01mzxx1-fine-0.0.1`.

## 2026-09-01 — datatype and ordinary tuple counterexamples (`562f1716d`)

The first source-level algebraic datatype loop is closed through `check`.
Latte-shaped enum cases may now carry named fields, for example
`node(value: Int, left: Tree, right: Tree)`, and constructor calls are ordinary
Fine expressions. If an enum has any field-bearing constructor, elaboration
uses Z3's public datatype constructor API directly. A field whose type is the
enclosing enum uses Z3's recursive datatype-sort reference; no tag array,
uninterpreted constructor axiom, or other compatibility encoding was added.
Pure nullary enums retain the finite enumeration path used by the bisimulation
runtime.

The closed boundary is deliberately monomorphic and single-datatype recursive.
Field names are checked for duplicates and retained for argument diagnostics;
constructor calls are positional. Direct self fields are admitted, while other
field types must already exist. There is no mutual recursion, type parameter,
index, projection expression, pattern, match exhaustiveness, codata, datatype
synthesis, or table-valued field. F* and Dafny were not consulted because this
slice encountered no representation workaround: the source terms map directly
to Z3 constructors. They remain the comparison point before introducing any
bespoke solution at one of those missing boundaries.

The same change makes binary tuples ordinary elaborated check expressions and
parameters instead of restricting them to table keys. The check domain now
admits Int, Bool, finite enums, monomorphic datatypes, and binary tuples.
Type-directed model lifting recursively recognizes datatype constructors and
tuple components, prints Fine constructor/tuple syntax, parses it through the
ordinary expression parser, elaborates it in the same manager, and checks exact
AST identity for every returned assignment.

`check-datatype-counterexample.fine` declares recursive `Tree` and finite
`Mark`, forces `tree = node(7, leaf, leaf)` and `mark = marked`, refutes the
claim that they are the opposite cases, and returns both assignments in a
parseable witness. Its rain has 17 events: two completed model assignments,
seven strongly registered terms, the public query, and the exact witness.
`check-tuple-counterexample.fine` independently returns
`pair: (Int, Bool) = (7, true)`. Additional local probes closed a nested tree
round trip, an unsatisfiable identical-tree claim, a pure-enum assignment, and
a field-type error pointing at `true` in `node(true, leaf, leaf)`.

Validation commands were:

```
cmake --build .build -j4
./.build/fine run fine/fixtures/check-datatype-counterexample.fine
./.build/fine rain fine/fixtures/check-datatype-counterexample.fine
./.build/fine run fine/fixtures/check-tuple-counterexample.fine
./.build/fine run fine/fixtures/two-state-bisim.fine
./.build/fine run fine/fixtures/synth-max.fine
./.build/fine run fine/fixtures/check-counterexample.fine
nix flake check
nix build --no-link --print-out-paths
```

The isolated build ran the legacy suite plus exact datatype, finite-enum, and
tuple witness assertions. The clean artifact is
`/nix/store/n2486qxpxi6l0nzsnc59i1bxjbn8h7bk-fine-0.0.1`.

## 2026-09-01 — reference transfer for a live Rainfall projection

Inspected h's Latte checkout, the native TypeScript parser port, Dark's editor
specification, the recovered Obsidian live-preview implementation, and
obsidian-demin against Fine's current parser and Rainfall v2 recorder. The
result is `docs/reference-transfer.md`.

The closed decision is that solver evidence remains bound to the exact source
snapshot and Z3 manager that produced it. A decoration may be mapped through a
source edit for visual continuity, but it becomes explicitly transported/stale
and cannot acquire truth about the new program from matching text, names, or
spans. The design separates document, snapshot, parse-local source-node, and
same-manager Z3-term identities, then represents source correspondence as an
evidence relation with exact/desugared/generated/internal alternatives.

Latte contributes the live-document versus immutable-artifact separation,
phase-owned compilation snapshots, explicit mapping kinds, and query ownership
derived from an AST-to-IR identity join rather than ranges. typescript-go
contributes parser construction and recovery techniques, but its lazy
process-global AST IDs and within-parse reparsing do not establish cross-edit
identity. Dark contributes typed revision transport and generation staleness,
but its current Lean tree has forty proof holes and is not treated as proved
infrastructure. The recovered Obsidian code contributes one observed UI rule:
map old decorations while composing or while demanded syntax is unavailable,
then rebuild from current syntax. Its numeric tree-length test is not promoted
into Fine semantics, and neither the proprietary source nor obsidian-demin
becomes a dependency.

The proposed first vertical slice is a document/snapshot object in Rainfall,
parse-local source objects, explicit source-to-term evidence for the existing
`check` path, and a replay validator with hostile cross-revision and
cross-manager tests. No implementation claim was made in this study commit.

## 2026-09-01 — exact source snapshots and replay validation (`6338312d4`)

Implemented the first vertical slice from `docs/reference-transfer.md`. Every
`fine rain` compilation now opens with an opaque document object and an exact
revision-zero snapshot carrying the input byte length and SHA-256 digest. The
parser assigns never-reused-within-this-parse IDs to declarations and
expressions. Rainfall declares those source nodes with full byte/line/column
spans, then records compiler-owned `source.term.evidence` edges while
elaborating the original `check` assumptions and guarantees. Names and literals
are labeled `exact`; compound syntax and constructor lowering are labeled
`desugared`. Generated witness reparses are deliberately outside source-edge
capture. All edges name the exact snapshot, a strong manager-scoped term
handle, and the enclosing check run.

Added `fine-rain-validate`, an installed replay validator rather than a viewer
that silently trusts JSON. It checks the schema envelope, contiguous sequence
and event IDs, unique document/snapshot/source/term identities, the supplied
source's exact digest and length, span bounds, source/edge snapshot agreement,
same-run/recorder/manager term identities, known edge endpoints, allowed
correspondence labels, and terminal run closure. It rejects source ownership
for post-preprocessing `internal_z3` terms. A valid counterexample trace has 46
events, 12 source nodes, 11 strong terms, and 11 source-to-term edges.

The install check mutates the fixture and trace in nine hostile ways: a
same-length source edit, an inserted byte that moves every later span, an edge
to an old snapshot, an unknown term, a reused live handle, a cross-manager term
identity, an `internal_z3` source edge, and an event appended after the terminal
run close. It also compiles the same path twice and requires distinct opaque
document IDs. All are rejected while the unmodified trace validates.

Two failed isolated builds were retained because they exposed harness and
identity assumptions. The first invoked the installed Python script through
`/usr/bin/env` during `installCheckPhase`, before Nix fixup had rewritten its
shebang; the checks now call the declared Python interpreter explicitly. The
second document-ID test failed because sandboxed clocks and `random_device`
were reproducible across processes; process identity was added to the opaque
nonce. A third build exposed a bad hostile-test fixture assumption—the test
tried to replace `10`, which was not present—and now changes `-1` to `-2` at
equal byte length. The final isolated build and flake evaluation pass.

The pre-existing Fine runtime sources were mechanically normalized with the
repository's checked-in clang-format configuration in the separate commit
`29d6ed626`; the semantic implementation commit is therefore reviewable as
826 insertions and 44 deletions rather than being mixed with formatting churn.

Validation commands were:

```
cmake --build .build -j4
./.build/fine rain fine/fixtures/check-counterexample.fine
python fine/rainfall_validate.py \
  fine/fixtures/check-counterexample.fine /tmp/check.rain
nix flake check
nix build --no-link --print-out-paths
```

The clean artifact is
`/nix/store/va1yc01rnpykaxgr7srpjfbfn4irdsva-fine-0.0.1`. The remaining edge is
not cross-revision truth transfer: it is a viewer transport that may preserve a
visibly stale decoration while a new snapshot compiles, plus broader truthful
solver-search coverage beyond the three current observer boundaries.
