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

## 2026-09-01 — stale decoration transport (`397461170`)

Closed the viewer/transport edge without adding an editor. The installed
`fine-rain-project` command first runs the exact replay validator, then consumes
one ordered transaction of non-overlapping `{from,to,insert}` edits whose
coordinates are byte offsets in the admitted source. It applies the transaction
itself, constructs the next display identity with the same opaque document ID,
revision plus one, exact resulting SHA-256, and byte length, and maps every
source-evidence range into the displayed bytes.

The output is `fine.rainfall.projection.v1`, specified in
`fine/projection-schema.json`. Each annotation retains its old snapshot,
source-node, term, correspondence, event, scope path, and claim span separately
from its display span. With no transaction the validated trace is `current`.
After any transaction a surviving range is `transported`; a range collapsed by
deletion is `unplaced`; the display snapshot is explicitly
`admitted_by_rainfall: false` and has no Rainfall snapshot ID. A replacement
that writes the exact same bytes is still transported because the revision
changed. Neither matching text nor matching whole-file hash upgrades it.

Half-open interval behavior is fixed rather than delegated to a UI library. An
insertion before or at a range start shifts it, an insertion strictly inside
expands it, and an insertion at the end remains outside. Partial deletion keeps
the surviving prefix or suffix; exact deletion makes the marker unplaced; exact
replacement maps it to the replacement. Multiple edits are expressed in base
snapshot coordinates and accumulated in source order. Overlaps and two
insertions at the same byte are rejected as ambiguous.

The optional standalone HTML makes the distinction visible in text, border
style, and row state rather than color alone. Its stale banner says that the
claims belong to the previous revision and do not describe the displayed one;
every row also carries `data-state=current|transported|unplaced`. All document,
source, and term text is HTML-escaped. This is a static protocol witness, not a
CodeMirror integration, incremental parser, or claim that a transported range
still selects the same syntax.

The validator implementation moved into the importable `rainfall_replay.py`
module so projection cannot take a weaker parsing path. Validation was tightened
for the scope path and for the document name, syntax kind, and term text fields
the viewer reads. Projection logic similarly lives in an importable module;
the CLI files are thin installed entry points.

The first isolated projection build passed the CLI and hostile replay cases but
failed the direct interval tests with `ModuleNotFoundError: rainfall_project`:
CMake had installed the executable under its hyphenated command name, not an
importable module name. The implementation was split into
`rainfall_projection.py` plus `fine-rain-project`; the next clean build passed.

Install checks cover current, transported, and wholly unplaced projections;
byte-identical edits remaining stale; revision advancement; an unadmitted
display identity; overlapping-edit rejection; explicit stale HTML; and nine
range-boundary cases including multiple edits. The preceding hostile replay
suite still runs through the shared validator. Validation commands were:

```
cmake --build .build -j4
python fine/rainfall_project.py \
  fine/fixtures/check-counterexample.fine /tmp/check.rain \
  --edits /tmp/edits.json --html /tmp/projection.html
nix flake check
nix build --no-link --print-out-paths
```

The clean artifact is
`/nix/store/6gcdq7qiss1fsq0bck6cgq538y7bj776-fine-0.0.1`. The next live edge is
generation control: while a revision is compiling, retain only transported
annotations from its immediate predecessor; accept a completed trace only for
the generation and exact display snapshot that requested it; discard late old
generations without promoting or merging their claims.

## 2026-09-01 — exact generation admission (`837bbe1ac`)

Closed the pure admission boundary for racing live runs. The installed
`fine-rain-generation request` command binds an opaque generation ID to one
exact display identity: document, revision, SHA-256, and byte length. It emits
the structured arguments for the new extended rain form:

```
fine rain --document <id> --revision <n> --generation <id> <source.fine>
```

That form does not trust a caller-supplied hash; Fine reads the file and computes
the snapshot hash and size itself. It preserves the supplied document and
revision and uses the supplied generation as Rainfall's existing run identity,
including every event and strong term identity. The ordinary `fine rain
<source>` path still creates a fresh document, revision zero, and fresh run.

`fine-rain-generation admit` takes the current request and display bytes plus a
candidate's retained source and completed trace. It first runs the full replay
validator against the candidate's own source. A malformed or truncated
candidate is an error, not a stale result. It then recomputes the current display
identity and compares the candidate run, document, revision, hash, and length to
the request. Exact agreement emits `fine.rainfall.admission.v1` with an admitted
current projection. Any mismatch emits one whole discarded completion with a
specific reason: display advanced, late/unrequested generation, different
document, different revision, or different source. Candidate events are never
partially merged with transported markers.

The request/admission shape is recorded in `fine/generation-schema.json`. The
gate is deliberately pure and has no daemon or filesystem lock: the future host
must retain the newest request and its source, supply that request as “current,”
and never reuse a generation ID. Killing a predecessor is an optimization only;
a process that ignores cancellation can finish and will still fail admission.
This slice therefore establishes the comparison that a live host must perform,
not process scheduling, IPC, CodeMirror lifecycle, or persistent editor state.

The isolated install check runs revision zero and revision one under the same
document with distinct generations. Both requested completions are admitted and
produce only current annotations. It then presents revision zero after revision
one, a second run for the exact revision-one bytes under another generation, a
different document, a different revision, a different source under otherwise
matching identity, and a request whose displayed bytes advanced. Each is
discarded for the intended reason. A post-terminal hostile trace is rejected by
validation before admission. The test also verifies that the generation is the
run envelope on every event and that Fine preserved the requested document and
revision in the snapshot.

Validation commands were:

```
cmake --build .build -j4
python fine/rainfall_generation_cli.py request /tmp/rev1.fine \
  --document document:live-test --revision 1 --generation generation:1
./.build/fine rain --document document:live-test --revision 1 \
  --generation generation:1 /tmp/rev1.fine
python fine/rainfall_generation_cli.py admit \
  /tmp/request.json /tmp/rev1.fine /tmp/rev1.fine /tmp/rain1.jsonl
nix flake check
nix build --no-link --print-out-paths
```

The clean artifact is
`/nix/store/5vhfk2bpvzwjzyvxjwkj1l1w290f2m23-fine-0.0.1`. The next editor-facing
edge is the host transaction itself: atomically advance displayed bytes and the
current request, retain predecessor annotations only as transported, launch the
structured run, and replace them only with an admitted completion. The separate
solver edge remains observer coverage past the three current narrow boundaries.

## 2026-09-01 — atomic editor-neutral host transaction (`54320748e`)

Implemented the transaction as the installed `fine-rain-host` filesystem
harness. It is intentionally not a CodeMirror extension. Its authoritative
`state.json`, described by `fine/host-state-schema.json`, contains the exact
displayed UTF-8 source, recomputed document/revision/hash/length identity, current
generation, every generation record, and the annotations currently painted.
Immutable retained source and request files live beside it; completed traces are
retained separately.

`init` creates revision zero and its first request. `advance` takes the same
ordered byte-offset edit transaction as `fine-rain-project`. Under a POSIX host
`flock`, it applies the edit, maps every currently displayed range again, makes
current annotations transported and preserves already transported ones, keeps
collapsed annotations permanently unplaced, marks a pending predecessor
superseded, writes the new immutable request/source artifacts, and replaces
`state.json` using a same-directory temporary file, file fsync, rename, and
directory fsync. Artifacts are durable before state can name them. A crash before
the commit point can leave an orphan but cannot expose state pointing to a
partially written request or source.

`run` snapshots one requested or superseded generation under the lock and then
releases the lock before starting Fine as an argument vector without a shell.
The editor transaction is therefore not blocked by the solver. On completion it
validates the trace outside the lock, reacquires the lock, reloads the newest
display/request, and uses exact generation admission. An admitted completion
replaces the annotation set whole. A late completion is stored as historical
trace and a discarded generation record but cannot touch displayed annotations.
Fine parse/elaboration failure is recorded as `failed` and leaves whatever stale
markers are already displayed. `complete` exposes the same boundary for an
external worker. Retained trace writes tolerate an identical orphan from a crash
between trace persistence and state commit but reject different bytes at that
identity.

State loading does not merely parse JSON. It recomputes the display identity,
validates every embedded generation request and its key, requires retained paths
to be relative and non-traversing, checks the current request against the display,
and bounds every placed annotation span within the displayed bytes. This is
corruption detection, not an authentication boundary against someone who owns
the host directory.

The clean install check exercises a real race. Revision zero is run and admitted;
revision one transports its eleven markers. A slow wrapper starts revision one's
Fine process, revision two advances while that process sleeps, and the actual
revision-one completion is then discarded against revision two. Revision two is
run and admitted, restoring eleven current markers. The test verifies generation
records and every retained source/request artifact. A second host replaces the
whole program with malformed Fine; the requested process fails, its generation
is recorded failed, and no annotation is invented. All prior replay, projection,
and pure generation hostile tests remain in the same isolated build.

Validation commands were:

```
cmake --build .build -j4
python fine/rainfall_host_cli.py init /tmp/fine-live \
  fine/fixtures/check-counterexample.fine --document document:host-test
python fine/rainfall_host_cli.py run /tmp/fine-live --fine .build/fine
python fine/rainfall_host_cli.py advance /tmp/fine-live /tmp/edits.json
nix flake check
nix build --no-link --print-out-paths
```

The clean artifact is
`/nix/store/m5z6pbg8vz2h21f6hrdv918m60rqcxpn-fine-0.0.1`. The live protocol is
now closed through an editor-neutral host transaction. Remaining editor work is
integration and UX policy—IME deferral, viewport rendering, and process
lifecycle—not another evidence identity layer. The independent semantic edge is
truthful solver-search coverage beyond the three narrow observers.

## 2026-09-01 — exact MBQI-instance-to-clause evidence (`439fb34a3`)

Started by testing the proposed next observer boundary rather than assuming it
would yield information. Temporarily instrumented both discard returns in
`qi_queue::instantiate(entry&)`: redundancy-checker satisfaction and
rewrite-to-true. Rebuilt Fine and ran the two-state MBQI bisimulation fixture.
Both counts were exactly zero while the existing `on_binding` observer reported
six accepted instances. Reverted the probe completely. For this workload, a new
discard observer would add API and soft-fork surface while producing an empty
trace; that null result is why this slice did not add one.

The live trace did contain a narrower missing evidence edge. Each accepted
`z3.mbqi-instance` was followed eventually by a `z3.clause.infer` whose public
proof hint had head `inst`, but the two records were only adjacent-looking facts.
The existing `qi_queue` callback occurs before lemma insertion; Z3's clause-proof
path later constructs an `inst` proof term containing the exact preprocessed
quantifier, negated unsimplified ground body, `bind` arguments, and generation.
That is enough to prove the handoff without claiming anything after admission.

Changed `RainfallRecorder::record` to return its emitted event ID. The quantifier
observer registers a pending pair keyed by the quantifier and positive ground-body
strong handles. When the clause observer sees a structurally valid `inst` hint, it
recovers those same two live terms, consumes the pending pair exactly once, and
adds the accepted-instance event, quantifier handle, instance handle, and each
ground-binding handle to the clause event. It rejects malformed `inst` hints,
duplicate pending pairs, and `inst` clauses that do not match an observed accepted
instance. The explicit relation is
`accepted-instance-became-admitted-clause`; chronology is not evidence.

On the fixture there are six accepted MBQI instances and exactly six `inst` clause
inferences, with six distinct one-to-one event references. This proves only that
an accepted ground instance became an admitted quantifier lemma. It does not say
the lemma propagated, conflicted, repaired the candidate model, or caused `sat`.
The observer boundary remains blind to the auxiliary MBQI context, discarded
candidates, assignments, decisions, watched literals, and blocking work.

Replay validation now validates the entire public clause payload—proof hint,
literal handles, count, and dependency indices—and, for `inst`, requires a prior
accepted-instance event with identical quantifier and instance handles, known
ground bindings, and the exact relation. The schema declares that payload.
Install checks assert the six-way bijection and reject traces whose instance event
or ground term was exchanged for another valid reference. `fine-rain-validate`
now runs on the bisimulation trace as well as check traces.

Validation commands and results:

```
cmake --build .build -j4
./.build/fine rain fine/fixtures/two-state-bisim.fine > /tmp/join.rain
python fine/rainfall_validate.py \
  fine/fixtures/two-state-bisim.fine /tmp/join.rain
nix flake check
nix build --no-link --print-out-paths
```

The validator reported 216 events, 41 source nodes, 97 strong terms, and zero
source-term edges for this generated bisimulation path. The clean Nix artifact is
`/nix/store/dw36gbxviwviiclh0xazkyzac8cyrpsl-fine-0.0.1`.

## 2026-09-01 — source-facing bisimulation activity (`2760e9e31`)

The preceding instance-to-clause join was still visible only to someone reading
JSONL. The bisimulation trace had 41 declared source nodes but zero source-term
edges, so projecting it produced no annotations despite its six accepted MBQI
instances and public clause stream. This was not a rendering defect: the compiler
had never stated which Fine syntax generated the four solver assertions.

The bisimulation elaborator now emits four `source.term.evidence` edges from the
exact `proof bisimulation { ... }` declaration node to the fully elaborated
`labels-agree`, `left-step-matched`, `right-step-matched`, and `initial-related`
assertion terms. Their correspondence is explicitly `generated`, not `exact` or
`desugared`; preprocessed quantifiers and CDCL(T) clauses are still represented as
Z3 terms and acquire no fake Fine source syntax.

Extended the existing validator-admitted `fine-rain-project` path rather than
adding a second viewer. Projection joins each generated evidence term to the exact
`bisim.clause.assert` event carrying that same strong term handle. It then groups
accepted quantifier instances by the compiler-assigned qid role and follows the
explicit accepted-instance-to-admitted-clause reference added in `439fb34a3`.
Each activity record retains the accepted event, ground instance handle and text,
admitted clause event or null, and every binding handle and text. A missing clause
is rendered as accepted without observed admission instead of being dropped.
This is role-labelled activity, not a claim that the lemma caused the answer.

`fine.rainfall.projection.v1` annotations now carry either null activity or a
schema-described `bisimulation-clause-activity` payload. The standalone HTML
shows all four roles on the proof declaration, with collapsed instance bodies and
bindings. It groups annotations sharing one source node into one row, so the proof
source is not repeated four times; the JSON remains four separate exact claims.
Current/stale/unplaced state still belongs to the display projection as before,
and all source, term, role, event, and binding text is HTML-escaped.

The two-state fixture now produces 220 events, 41 source nodes, 97 strong terms,
four generated source-term edges, and four proof annotations. Their accepted
instance counts are exactly `1,3,2,0` for labels, left-step, right-step, and initial
roles. All six accepted instances have admitted lemma references and nonempty
ground bindings. The generated HTML contains one source row, four activity
sections, and collapsed details for all six handoffs. Existing check projections,
stale transport, generation admission, atomic host races, hostile replay cases,
and language fixtures remain in the same install check.

Validation commands:

```
cmake --build .build -j4
./.build/fine rain fine/fixtures/two-state-bisim.fine > /tmp/bisim-view.rain
python fine/rainfall_validate.py \
  fine/fixtures/two-state-bisim.fine /tmp/bisim-view.rain
python fine/rainfall_project.py \
  fine/fixtures/two-state-bisim.fine /tmp/bisim-view.rain \
  --html /tmp/bisim-view.html > /tmp/bisim-view.json
nix flake check
nix build --no-link --print-out-paths
```

The clean implementation artifact before this log-only commit was
`/nix/store/nbiip8n8v6rm4p2shgq27ggp22q4i5ca-fine-0.0.1`.

## 2026-09-01 — first live browser integration

Closed the first editor-facing vertical slice without creating a second evidence
owner. Added `fine-rain-live`, a Python-standard-library HTTP server and a dense
local browser UI around the existing atomic `fine-rain-host`. It binds only
`127.0.0.1`; the installed flake app is `nix run .#live -- HOST SOURCE` and finds
the sibling installed `fine` executable unless a development `--fine` path is
provided.

The browser submits its complete textarea value for simplicity. The server finds
the common Unicode prefix and suffix and turns the changed middle into exactly one
UTF-8 byte-offset transaction before calling `advance`. Tests include a four-byte
emoji replacement (`a😀c` to `a😺c`) whose edit is exactly bytes `[1,5)`. The
server serializes display edits, but each named Fine generation runs on a daemon
thread outside that edit lock. It polls only the locked authoritative host state;
it owns no revision, generation, admission, or annotation state of its own.

The evidence pane groups edges by source node, shows the current display range and
source excerpt, and exposes bisimulation role counts plus collapsed accepted
instance bodies. During a run the old annotations remain visibly `transported` or
`unplaced`; solver failure remains visible and does not erase the old claim state;
only ordinary host admission replaces them with `current` annotations. A 220 ms
textarea debounce is request-rate policy only and never upgrades evidence.

The HTTP edit boundary requires `application/json`, rejects a browser `Origin`
whose hostname is not loopback, applies a 2 MiB UTF-8 source limit, disables
caching and MIME sniffing, and sends a CSP forbidding framing and all nonlocal
connects. It does not claim IME transaction fidelity, incremental parsing,
collaboration, syntax highlighting, or remote service safety.

The install check now starts the actual threaded HTTP server around a deliberately
slow Fine wrapper, fetches the UI and state API, rejects hostile-origin and
non-JSON edits, observes transported evidence after the first edit, submits a
second UTF-8 edit before the first solver finishes, and requires the predecessor
generation to become `discarded`, the newest to become `admitted`, and the final
annotations to be wholly current. The existing lower-level slow-run/edit race
remains separate.

During the first clean build, `/tmp` filled (`tmpfs`, 3.2 GiB) because an older
`/tmp/fine-install.tNz2lj` tree and the current failed install prefix together
occupied about 2.6 GiB; CMake reported `No space left on device` while copying the
static binary. Removed only Lynn-owned temporary Fine artifacts, restoring 3.1
GiB, and the pending Nix build then completed. The final validation sequence is:

```
python -m py_compile fine/rainfall_live.py fine/rainfall_live_cli.py
nix flake check
nix build --no-link --print-out-paths
nix run --no-write-lock-file .#live -- HOST \
  fine/fixtures/check-valid.fine --port 0 --document document:nix-live-smoke
```

The pre-final clean artifact was
`/nix/store/0f4b21ira69sxpzdgsmg6qlkinw2v8pn-fine-0.0.1`; a final rebuild after
loopback-only CLI and CSP tightening follows this log entry.

Implementation commit: `56299c727`. The final dirty-tree validation artifact
before this log-only commit was
`/nix/store/ld8xnxwqqscqs21b18b7ri1x41nnv415-fine-0.0.1`.

## 2026-09-01 — external direct-subterm induction (`b3a4f6fb8`)

Closed the first induction vertical slice by putting the tactic in Fine rather
than reviving Z3's disabled `smt_induction` sources. Full local copies and text
extractions of Leino's *Automating Induction with an SMT Solver*, Reynolds and
Kuncak's *Induction for SMT Solvers*, and Amin--Leino--Rompf's *Computing with
an SMT Solver* are under `/root/reading/fine-induction/`; the source-facing
decision is `fine/research/induction-translation.md`. Leino's frontend rewrite
was the appropriate first boundary: Fine owns the well-founded order and emits
an ordinary quantified formula, while Z3 remains responsible only for solving
that formula. The more invasive CVC4/CVC5 work adds lazy induction choice,
variable-order search, and conjecture generation inside the solver; none was
needed to test the source-to-SMT path.

Added declaration-level structurally recursive functions with the deliberately
narrow Latte-shaped surface

```
function length(xs: List): Int {
  match xs {
    nil => 0,
    cons(_, tail) => 1 + length(tail),
  }
}
```

The elaborator registers the function with the same Z3 manager through
`Z3_add_rec_def`. It accepts only an exhaustive, nonduplicated match on one named
field-bearing-datatype parameter. A recursive self-call's matched argument must
be the name of a direct self-typed field bound by the current arm; a mutation
from `length(tail)` to `length(xs)` is rejected at the exact call argument.
There is no mutual recursion, general match expression, projection syntax,
nested/transitive decrease, or termination claim delegated to Z3.

Added `inducts(xs);` at the start of a check. For the source theorem
`P = assumptions -> guarantees`, Fine constructs the direct-field relation `R`
from the datatype declaration and asks Z3 to refute

```
(forall smaller. R(smaller, xs) -> P[xs := smaller]) -> P.
```

The check parameters are already arbitrary fresh same-manager constants, so an
unsatisfiable negation closes the universally quantified induction step. Other
check parameters remain fixed across the smaller premise. A datatype without a
direct recursive field and an unknown or nondatatype induction parameter are
rejected. A deliberately false mutation, `length(xs) == 1`, is refuted with the
source counterexample `xs: List = nil`, showing that the translation does not
make checks vacuously verify. Both verified and refuted output names the actual
`direct-subterm` induction policy.

The induction query disables MBQI and enables E-matching; quantifier events now
record both engine settings and say `ematching-only-query`, `mbqi-only-query`, or
`not-distinguished` rather than inferring MBQI merely from a Boolean with an
ambiguous name. Rainfall gives the function declaration and check declaration
compiler-owned `generated` edges, records `function.recursive-definition` and
`check.induction.translate`, and then keeps the query-scoped Z3 instance and
clause observers separate. The length trace has 120 events, 13 source nodes, 49
strong terms, and 11 source-term edges. It has no accepted quantifier-instance
event: preprocessing reduces the singleton direct-field premise to a ground
assumption clause. The trace honestly retains that assumption plus inferred
recursive-function clauses containing `tail`, `case-def`, and
`recfun-num-rounds`; no causal join is invented from their chronology.

The control experiment without `inducts(xs);` remained inside recursive
unfolding beyond the install check's two-second boundary. An earlier raw Z3
control process was accidentally left alive for about 25 minutes at nearly one
CPU before being noticed and killed; it had still not returned. This is kept as
a timeout control, not reported as a proof that the untransformed problem never
terminates.

Validation commands and retained checks:

```
cmake --build build/fine --target fine-bin -j2
./build/fine/fine run fine/fixtures/induction-length.fine
./build/fine/fine rain fine/fixtures/induction-length.fine > /tmp/induction.rain
python fine/rainfall_validate.py \
  fine/fixtures/induction-length.fine /tmp/induction.rain
nix flake check
nix build --no-link --print-out-paths
```

The install check also mutates the fixture to require rejection of a
nondecreasing recursive call and an unknown induction parameter, refutation of
the false `length(xs) == 1` claim, timeout of the no-induction control, exact
Rainfall event ordering and engine flags, assumption/inference clause coverage,
generated function/check source edges, and validation of any accepted
E-matching instance should a later Z3 preprocessing path retain one. All prior
language, synthesis, model, bisimulation, projection, generation, host-race, and
live-browser checks remain in the same clean build.

Clean implementation artifact:
`/nix/store/8a6wkr1ch7s1zffzfbqchk7k8dr2a08w-fine-0.0.1`.

## 2026-09-01 — total generated-term lift for Rainfall (`bb131bffbb`)

Closed the representation/provenance conflation in Rainfall. Every strong
`z3::expr` admitted by the term registry now receives a canonical
`fine.generated-term.v1` rendering irrespective of whether it came from the
Fine elaborator, an accepted quantifier instance, a post-preprocessing clause
literal, or a clause proof hint. Source ownership did not expand: only the
existing compiler-known `exact`, `desugared`, and `generated` edges carry Fine
source nodes. Internal origin remains separately visible through the term's
first-observation `origin` and the observer event that uses its handle.

The generated grammar is deliberately lower than ordinary source sugar. It has
three disjoint namespaces (`_s_` sort aliases, `_d_` declaration aliases, and
`_v_` bound variables), declaration applications, typed `numeral` forms, and
`forall`/`exists`/`lambda` forms. Quantifiers preserve binder symbols and sorts,
weight, qid, skid, patterns, and no-patterns. Each term declaration includes the
exact live sort and function-declaration bindings used by its rendering, with
Z3 symbols, declarations, kinds, parameters, domains, ranges, and diagnostic AST
IDs. Common builtins receive readable generated names such as `_d_select`, while
Z3-created recursive auxiliaries remain recognizable as
`_d_case_def_0_length`, `_d_recfun_num_rounds_0`, and `_d_tail_tail_cons`.
`z3_text_diagnostic` preserves the raw printer only for debugging; projection
continues to consume `text`, so raw SMT-LIB is no longer an admitted display
fallback.

The first implementation reparsed and reified directly in `term()`. That method
is also called inside Z3's quantifier and clause observer callbacks; constructing
new ASTs there disturbed the active query and the bisimulation run was canceled.
The retained design prints and takes a strong reference in the callback, resets
the observers after `solver.check()` returns, then calls `validate_terms()`
before the terminal run-close event. That pass regenerates the text, parses it
against the same manager-local bindings, reifies it, and requires
`Z3_is_eq_ast`. It emits exactly one `term.lift.validate` event per term. The
rendering and validation event share a SHA-256 digest; the offline validator
recomputes the declaration digest and requires the validating event to name the
same digest. A hostile replay that changes the text and one that changes the
exact-identity result are both rejected.

Current installed traces cover every live term at all three observer boundaries:

- `two-state-bisim`: 317 events, 41 source nodes, 97 terms, 97 exact validations,
  four source-term edges, 520,460 bytes. First-observation origins are 28
  semantic, 44 clause-literal, and 25 proof-hint terms.
- `induction-length`: 169 events, 13 source nodes, 49 terms, 49 exact validations,
  11 source-term edges, 182,155 bytes. Origins are 16 semantic, 31 clause-literal,
  and two proof-hint terms.
- synthesis, integer counterexample, datatype counterexample, and tuple
  counterexample traces also validate through the same installed parser.

The first two clean-install attempts exposed test assumptions rather than runtime
failures. The first source snapshot omitted the newly edited `runtime.cpp`, so a
generation/projection assertion observed an inconsistent staged tree. After
staging the complete slice, the install check reached a datatype assertion that
still expected raw Z3 text (`node 7 leaf leaf`); it now checks the generated form
`_d_node_node(numeral("7",_s_Int),_d_leaf_leaf,_d_leaf_leaf)`. All old hostile
snapshot, manager, handle, clause-instance, transport, generation, host-race,
and browser-race cases remain in the same install check.

Validation commands:

```
cmake --build build/fine --target fine-bin -j2
./build/fine/fine rain fine/fixtures/two-state-bisim.fine > /tmp/bisim-new.rain
python fine/rainfall_validate.py \
  fine/fixtures/two-state-bisim.fine /tmp/bisim-new.rain
./build/fine/fine rain fine/fixtures/induction-length.fine > /tmp/induction-new.rain
python fine/rainfall_validate.py \
  fine/fixtures/induction-length.fine /tmp/induction-new.rain
nix flake check
nix build --no-link --print-out-paths
```

Clean artifact:
`/nix/store/h659gridgfkszamw5nvd6vx3jass82vm-fine-0.0.1`.
Ordinary user-surface resugaring and materializing a generated result into editable
Fine source remain above this exact core; they may not replace or weaken it.

## 2026-09-01 — one typed match hole becomes source (`53b2feb42`)

Closed the first witness-to-source slice of Fine's interruptible-search
principle. `synth` may now contain one exhaustive declaration-level match whose
arms mix ordinary Fine expressions with whole-arm named holes such as
`?payload`. The parser gives each hole its ordinary parse-local node and exact
byte span. At Rainfall time `synth.hole.declare` combines that source node with
`snapshot:0`, expected type `Int`, grammar `fine.qf-lia-int.v1`, and strong term
handles for precisely the integer values available to the arm: unmatched
integer parameters and integer constructor fields. Duplicate hole names,
nested holes, non-exhaustive or repeated constructors, bad binding arity,
shadowing, non-datatype scrutinees, table parameters, and non-Int results are
rejected at the boundary.

Each open arm is instantiated at its constructor using fresh same-manager field
terms and run through the existing counterexample-guided refutation synthesizer
inside a named `synth-arm:<declaration>.<hole>` scope. Candidate grammar inputs
are now distinct from the complete semantic parameter vector, so non-Int values
may constrain the arm without entering the QF-LIA term grammar. The selected
witness is lifted with the arm's source names, printed, parsed as an ordinary
Fine expression, elaborated in the arm environment, and required to recover the
exact Z3 AST. The arm close records that verified body and term. After fresh
field terms are replaced with accessors of the original scrutinee, completed and
synthesized arms are assembled by datatype recognizers into one `ite` term. A
fresh public `query:match-verify` solver contains only the negation of the whole
completed specification; its open/result/close boundary is explicit in
Rainfall. This avoids turning a `verified: true` field into the evidence for the
claim.

`fine.match-witness` binds each open hole to one ordered, nonoverlapping exact
source range and the body from its verified arm close. Offline replay requires a
unique match witness for every match-synthesis run, one unique arm close for
every hole, known grammar terms, exact hole ranges, and byte-for-byte agreement
between each replacement and its verified arm body. Hostile traces moving the
range or changing `value` to `fallback` are rejected. The filled match still
emits a match witness with zero open arms so its whole-match verification remains
visible, but it has no hole, candidate-selection, or arm-synthesis events.

`fine-rain-host materialize` is the only write path for these results. It accepts
only the current admitted generation, reloads and validates the retained trace
against the current displayed bytes, checks that the admitted ranges still
contain holes, removes the trace-only `hole` keys, and passes all replacements
to the existing locked edit transaction. This advances the revision, transports
old annotations, durably issues a new immutable request/source pair, and leaves
the new source unadmitted until that generation completes. It does not edit a
file behind the host or reuse predecessor evidence.

The fixture begins as:

```fine
synth unwrap(input: MaybeInt, fallback: Int): Int {
  match input {
    none => fallback,
    some(value) => ?payload,
  }
  ensures {
    (input == none && result == fallback) || input == some(result);
  }
}
```

The arm grammar selects `value`; the CLI prints the completed match after two
ground instances and one core member. The open trace validates with 90 events,
17 source nodes, 17 strong terms, and one source-term edge. The host applies the
single `[156,164)` replacement, producing revision one and byte length 253. Its
new admitted trace has 35 events: one whole-match verification query and no
`synth.hole.declare` or `synth.candidate.select`. The checked-in
`synth-match-materialized.fine` separately exercises the same zero-enumeration
path.

The first manual host probe accidentally invoked `rainfall_host.py` directly;
it is an importable implementation module and therefore did nothing. Repeating
it through `rainfall_host_cli.py`, the same entry point installed as
`fine-rain-host`, completed the full init/run/materialize/run transaction. A
prospective call to `load_edits` with the trace replacement objects would also
have carried the extra evidence-only `hole` key; materialization deliberately
projects only `from`, `to`, and `insert` before loading the transaction.

Validation commands:

```
python -m py_compile fine/rainfall_host.py fine/rainfall_replay.py
cmake --build build/fine -j2
./build/fine/fine run fine/fixtures/synth-match-open.fine
./build/fine/fine run fine/fixtures/synth-match-materialized.fine
./build/fine/fine rain fine/fixtures/synth-match-open.fine > /tmp/match.rain
python fine/rainfall_validate.py \
  fine/fixtures/synth-match-open.fine /tmp/match.rain
nix flake check
nix build --no-link --print-out-paths
```

The clean install check includes both direct runs, replay admission, two hostile
replacement mutations, the actual atomic host transaction, admission of the new
generation, and the absence of grammar enumeration on rerun. All prior model,
synthesis, counterexample, induction, observer, projection, generation, host
race, and browser race checks remain in that same build.

Clean artifact:
`/nix/store/6lgbp02kzxiwpfn02vfm0dns8q66kk91-fine-0.0.1`.
The remaining match-slice edge is deliberately not papered over: arms run
sequentially inside one source generation. Per-arm cancellation and projection
of a live residual onto each hole still require a finer host/query boundary.

## 2026-09-01 — GADT/SMT representation survey (`5d78d8554`)

Corrected an initial false assumption about Z3's datatype API. This checkout has
native type variables, polymorphic datatype declarations, concrete datatype-sort
instantiation, and specialized constructor/accessor retrieval; ordinary
`List<A>` and `Either<A,B>` do not require a Fine-owned monomorphizer. The
remaining GADT boundary is constructor-specific result indices, which Z3's
uniform parametric datatype schema does not express.

`fine/research/gadts-smt.md` compares the relevant constraints from OutsideIn(X),
the injectivity/discriminability account in *The Essence of GADTs*, the
semi-decidable GADT exhaustiveness result, and F*'s broader SMT encoding. The
result is not a paper-inspired feature list but one representation test. Closed
index specialization preserves Fine's one-Z3-sort-per-value-type invariant but
cannot cover an open recursive index family. An erased carrier plus an `index`
or `HasType` invariant covers open indices but makes distinct Fine indexed types
share a Z3 sort and requires typing evidence for user-surface lifting. The latter
is a deliberate revision of the v1 boundary, not a datatype implementation
trick.

The proposed spike compares both encodings on `integer`, `less`, and polymorphic
`if_` constructors. It must test raw model inhabitants, branch-local index
evidence, index-directed synthesis grammar, and exact lift/reify. The deciding
failure is solver grammar: a frontend GADT that lets Z3 enumerate ill-indexed raw
terms and filters them afterward is rejected as ornamental.

## 2026-09-01 — synthesis scope correction (`fbfbaef94`)

Corrected a design conflation exposed by h's question about whether the current
`synth` feature is useful. Fine's ordinary queries do not construct programs.
The bisimulation path constructs a model value, `check` constructs a model
assignment when refuted, and induction asks whether a compiler-generated step
has a counterexample. Rainfall's total Fine rendering of their internal terms is
an observation/identity property; it is not source-program synthesis or proof
reconstruction.

The TODO, architecture, README, fixture guide, and synthesis pressure-test now
say so. `synth max` is retained as an experimental QF-LIA backend regression for
ground-instance selection, unsat-core assembly, independent verification, exact
round trip, and Rainfall coverage. The materialized match arm remains a closed
witness-to-source identity and host-admission experiment. Further per-arm live
projection and cancellation are paused: an arithmetic hole does not justify that
editor architecture.

The next synthesis test, if the backend survives, must consume a genuinely stuck
induction residual. It derives a bounded local helper-lemma or invariant grammar
from the residual's own variables, recursive calls, predicates, and subterms;
kills candidates on small constructor values; proves survivors in separate
generations; materializes the admitted lemma; and requires the original theorem
to rerun without enumeration. If that path does not reuse the public `synth`
declaration, the surface should be removed while keeping its lower engine as an
internal proof-search component.

Validation was documentation-only:

```
git diff --check
nix flake check
```

No runtime claim or clean artifact changed.

## 2026-09-01 — native indexed-family encoding follow-up

Surveyed alternatives to F*'s universal `Term`/`HasType` encoding after h
rejected that representation for Fine. The added section in
`fine/research/gadts-smt.md` separates five candidates rather than treating
"erasure" as one design:

- ATS keeps program values and static indices in separate sorts, represents
  proof-only indexed families as erasable `dataprop`s, and sends index constraints
  to a solver. This is the closest source-language precedent for Fine's `Step`.
- DML and Thoralf use the solver only for a restricted, explicitly sorted index
  constraint domain. They preserve a thin many-sorted target but do not provide
  solver-visible derivation induction.
- Inductively interpreted constrained Horn clauses represent proof-irrelevant
  indexed families as least predicates and perform induction on predicate
  derivations while delegating branch formulas to SMT. The cofinite dependent
  function field still requires an elaboration before ordinary finite CHCs.
- The index-erasure analyses of Brady--McBride--McKinna and Ghostbuster identify
  families whose indices are recoverable from constructor structure, permitting a
  native ADT plus checked/synthesized indices rather than a uniform raw carrier.
- SMT-LIB 3's current proposal adds dependent function types but explicitly keeps
  algebraic datatypes non-dependent, so there is no imminent standard native GADT
  target that removes the compiler representation choice.

The resulting candidate is a hybrid: ATS-style static index sorts and proof
erasure; native Z3 datatypes whenever indices are structurally synthesizable; and
an inductive-relation/CHC layer for ghost indexed families. This is research only;
no implementation, runtime, or artifact claim changed.

## 2026-09-01 — ATS-style proof-family boundary

h accepted the native-sorted direction after distinguishing object-language
`Tm`/`Ty` from F*'s universal SMT `Term`. The architecture and TODO now fix the
first indexed feature as a ghost, strictly-positive proof family. Its indices
are terms of existing native Z3 sorts; its witnesses are available to source
proof construction and induction but erased before model/value construction.
The compiler retains their constructor and recursive-premise correspondences as
Rainfall evidence before erasure.

This does not add ATS's complete static language, a Fine type-code universe,
runtime GADTs, raw Horn clauses, or user-defined object-language semantics. The
single acceptance fixture is full locally nameless preservation: the
abstraction-congruence constructor has a cofinite dependent proof field, and
Rainfall must retain the chosen constructor, opened native `Tm` indices, fresh
name, recursive `Step` premise, and induction hypothesis. Documentation only;
no implementation or clean artifact claim changed.

## 2026-09-01 — executable indexed-relation boundary probe

Inspected the actual Fine implementation before attempting indexed syntax. The
current parser has value datatypes and declaration-level `proof`, but no proof
family AST. The checker translates `inducts(xs)` to one ordinary quantified
direct-subterm hypothesis over a native datatype and asks the SMT solver to
refute that step with MBQI disabled. It has no inductive-relation manager to
which a `Step` family could honestly be elaborated.

Added the independent nested flake `fine/spikes/indexed-proof` to settle that
solver boundary first. Its three SMT problems establish:

- A recursively defined `Step : Tm × Tm` over a native `Tm` datatype has the
  required least-relation behavior under Horn fixedpoint semantics: a two-rule
  constructor path is reachable, while `Step(z,z)` is `unsat` because no rule
  produces it. There is no universal term carrier and no proof-witness sort.
- The superficially similar ordinary-SMT encoding, an uninterpreted Bool
  function plus constructor introduction axioms, accepts `Step(z,z)`. It cannot
  justify indexed inversion or induction because constructor junk remains legal.
- A name appearing only in a Horn body is an existential search variable for
  deriving the head. In the finite countermodel, `a` is fresh and its body step
  succeeds, so the abstraction head is derived, while another fresh name `b`
  simultaneously witnesses body-step failure. This concretely refutes the
  tempting translation of a cofinite `forall` premise to an ordinary rule body.

The architecture and TODO now stage the implementation honestly. The first
source slice admits only base and first-order recursive proof constructors and
rejects universal premises. The locally nameless slice must keep the universal
premise explicit until Fine elaborates an arbitrary-fresh branch together with
the freshness/equivariance obligation that makes representative choice sound.
First-order lambda lifting alone does not repair the quantifier polarity.

Validation:

```
cd fine/spikes/indexed-proof
nix run .
nix flake check
```

The run prints `sat`, `unsat` for the least relation, `sat` for the junk-admitting
ordinary encoding, and `sat`, `sat` for the simultaneously derived binder head
and cofinite counterexample, then reports that all boundary tests passed.

## 2026-09-01 — constrained-view decision

h accepted a small named-view mechanism for types that require particular proof
premises. A view refines one existing native carrier with a parameterized erased
`requires` proposition. `FreshFor(body, body_prime) over Name`, for example, is
represented by the same Z3 `Name` sort plus evidence of freshness; it is not a
wrapper datatype or a member of a universal type-code sort.

At a use site Fine checks that the current assumptions entail the instantiated
view requirement. Inside the receiving declaration the evidence is available as
an assumption. The check is semantic rather than a demand that the source repeat
an identical formula, and Rainfall must identify the assumption or derived clause
which discharged it. Views do not add general refinement subtyping, type
quantification, method synthesis, or first-class functions. The cofinite `Step`
constructor uses this mechanism only in its erased proof-function field.

## 2026-09-01 — elementary-topos usability target

h added a later language acceptance target: define an elementary topos and prove
generic results about it without the definition or each use becoming ceremonial.
The TODO fixes the required representation as explicit compile-time signatures,
not typeclass search or runtime records of solver declarations. An `Arrow(A,B)`
is a constrained view over one native `Hom` sort; category operations, selected
finite limits, exponentials, the subobject classifier, and their laws are named
signature members. Selected mediators prevent each theorem from reopening an
existential search for already supplied structure.

The first exit theorem is balancedness: a morphism which is monic and epic is an
isomorphism, with the signature laws available through an explicit
`ElementaryTopos` parameter rather than manually restated. A later subobject
Heyting-algebra construction is retained as the quotient/setoid pressure test;
it cannot identify raw mono representatives using host-language equality. This
is a future language target after the indexed locally nameless `Step` slice, not
a claim about the current parser or runtime.

## 2026-09-01 — first executable indexed proof family

Closed the first-order portion of the indexed `Step` milestone rather than
jumping directly from the SMT boundary probe to cofinite locally nameless
reduction.

### Surface and lowering

Added `ProofFamilyDecl` and `ProofConstructor` syntax. The accepted surface is:

```fine
proof family Step(before: Tm, after: Tm) {
  constructor root() {
    takes {}
    gives Step(atom, mark);
  }
  constructor under(before: Tm, after: Tm) {
    takes { Step(before, after); }
    gives Step(wrap(before), wrap(after));
  }
}
```

A family resolves every index to an existing native Fine/Z3 value sort, creates
one Bool-valued Z3 relation, and registers it with `z3::fixedpoint`. Each
constructor parameter is a native sorted constant; its direct proof-family
premises and family result elaborate through the ordinary typed expression
machinery, and the compiler adds the universally closed implication as one named
fixedpoint rule. There is no proof-witness datatype, universal `Term` carrier,
raw Horn surface, or ordinary-SMT introduction axiom.

The first executable consumer is intentionally narrower than a normal Fine
check: a parameterless, assumption-free `check` with exactly one ground ensured
family atom asks fixedpoint membership. A derivable atom prints `derived`; an
atom outside the constructor closure prints `not-derived`. Open queries, mixed
ordinary formulas, proof elimination, and derivation induction are rejected or
remain unimplemented rather than being given ordinary first-order validity
semantics.

### Quantifier-polarity guard

Every proof-constructor parameter must occur syntactically in the constructor
result indices. `reject-proof-family-universal.fine` puts `name` only in the
recursive premise and is rejected with:

```
parameter `name` does not occur in the constructor result; a premise-only
parameter would be one-witness search, not a universal proof field
```

This guard is deliberately stronger than the eventual indexed-family language.
It prevents the exact body-only Horn lowering shown unsound by
`fine/spikes/indexed-proof/cofinite-horn-is-existential.smt2`. The later
arbitrary-fresh/equivariance elaboration must replace the guard for an explicit
universal field; relaxing it without that evidence is not permitted.

### Rainfall boundary

Rainfall now declares the erased family relation and every admitted constructor
rule, including the exact generated rule/conclusion terms and premise counts.
The membership run retains the exact source query, the public fixedpoint result
and answer, and closes with `proof-check.run.close`. The family declaration also
has compiler-owned generated source edges to its exact rules, matching the
existing bisimulation-clause policy. The replay validator accepts the new close
operation as a terminal run. Coverage explicitly excludes Spacer's internal
rule-search steps. The positive trace validates with 52 events, 19 source nodes,
9 strong terms, and 7 source-term edges.

### Fixtures and validation

- `fine/fixtures/proof-family-step.fine` derives one contextual application.
- `fine/fixtures/proof-family-junk.fine` establishes that `Step(atom, atom)` is
  not in the least relation.
- `fine/fixtures/reject-proof-family-universal.fine` exercises the polarity
  rejection.
- The install check runs both membership outcomes, validates the positive
  Rainfall trace, and requires the universal-field fixture to fail.

Local vertical checks during implementation:

```
cmake --build .build -j2
.build/fine run fine/fixtures/proof-family-step.fine
.build/fine run fine/fixtures/proof-family-junk.fine
.build/fine run fine/fixtures/reject-proof-family-universal.fine
.build/fine rain fine/fixtures/proof-family-step.fine > /tmp/fine-proof-family.rain
python3 fine/rainfall_validate.py \
  fine/fixtures/proof-family-step.fine /tmp/fine-proof-family.rain
```

The first Rainfall validation attempt used the wrong script path
`python/fine_rain_validate.py`; the real installed-source path is
`fine/rainfall_validate.py`. After that correction the replay initially rejected
the new trace because `proof-check.run.close` was not in its explicit terminal
operation set. Extending that closed set fixed the schema boundary; no generic
suffix matching was introduced.

Final declarative validation after closing the execution guard:

```
nix flake check
nix build --no-link --print-out-paths
```

Both passed, including the install-check fixture matrix. The pre-source-edge
implementation tree realized as
`/nix/store/b8m34gqvghyivq5f94g5xvmznfzjh2cv-fine-0.0.1`; the final committed
artifact is recorded in the governing prompt after the clean build.

## 2026-09-01 — open proof-family invariants and Spacer callbacks

Closed the first useful open consumer of an indexed proof family without calling
it source derivation induction.

### Fixedpoint observer probe

Before extending the language, compiled a temporary C++ probe directly against
Fine's built `libz3.a`. It registered an integer `Step` relation, a nullary bad
state, and all three callbacks from `Z3_fixedpoint_add_callback`. With default
Spacer parameters the unsatisfiable invariant query emitted predecessor and
unfold callbacks but no lemmas. Inspection of
`spacer::context::new_lemma_eh` showed that lemma export is deliberately gated by
`spacer.p3.share_lemmas` and `spacer.p3.share_invariants`. Enabling both produced
exact relation-guarded lemma terms at levels 0 and 1, as well as the payload-free
predecessor/unfold crossings.

The temporary probes were `/tmp/fp-callback-probe.cpp` and
`/tmp/fp-callback-probe2.cpp`; representative commands were:

```
g++ -std=c++20 -I src/api -I .build/src -I src \
  /tmp/fp-callback-probe2.cpp .build/libz3.a -lpthread \
  -o /tmp/fp-callback-probe2
/tmp/fp-callback-probe2
```

A separate satisfiable probe used a binary bad relation and
`query_relations`. Its public answer contained a concrete hyper-resolution proof
ending in a tuple such as `query!0 2 1`, but the API did not return that tuple as
a typed model assignment. This is why the source slice does not fake a Fine
counterexample witness yet.

### Language translation

A parameterized check in a document containing a proof family now admits exactly
one direct family atom in `assumes` and ordinary Fine Boolean expressions in
`ensures`. For

```fine
check distinct_indices(before: Tm, after: Tm) {
  assumes { Step(before, after); }
  ensures { (before == after) == false; }
}
```

Fine registers a fresh nullary relation and the universally closed rule

```
Step(before, after) && !(before != after) -> counterexample
```

using the elaborated native terms. Query `unsat` verifies the invariant over the
least `Step` relation; `sat` refutes it. This is fixedpoint reachability rather
than an ordinary SMT implication, so `Step` never degrades to an unconstrained
Bool function. It is also not yet source proof elimination: there is no named
proof parameter, constructor match, index refinement, or recursive induction
hypothesis.

`proof-family-invariant.fine` verifies that the base `atom -> mark` step and all
contextual `wrap` steps have unequal indices. The negative fixture asks for
index equality and is refuted. Until the public fixedpoint proof is structurally
lifted, the latter prints `counterexample: fixedpoint reachability only` rather
than inventing a typed tuple.

### Rainfall callback boundary

Added `RainfallFixedpointObserver`, registered only for `fine rain`. It sets the
two Spacer export gates and records:

- `z3.spacer.lemma-export`: exact same-manager lemma term and level. Spacer can
  export a newly added or re-encountered lemma, so duplicates are allowed and no
  event is claimed to cause the result.
- `z3.spacer.predecessor`: one ordinal only; the callback has no rule, term,
  relation, or success payload.
- `z3.spacer.unfold`: one ordinal only, with the same strict payload boundary.

C callbacks are `noexcept`; any recorder exception is retained as an
`exception_ptr` and rethrown after the query rather than unwinding through C.
Only compiler-generated family rules and the counterexample rule receive Fine
source edges. Spacer lemmas remain independent internal terms. The positive
invariant trace validates with 80 events, 22 source nodes, 15 strong terms, and
11 source-term edges; it contains three lemma exports, predecessor activity, one
unfold callback, and the final public answer. The false trace also validates.

### Validation commands

```
cmake --build .build -j2
.build/fine run fine/fixtures/proof-family-invariant.fine
.build/fine run fine/fixtures/proof-family-invariant-false.fine
.build/fine rain fine/fixtures/proof-family-invariant.fine \
  > /tmp/fine-proof-invariant.rain
python3 fine/rainfall_validate.py \
  fine/fixtures/proof-family-invariant.fine \
  /tmp/fine-proof-invariant.rain
.build/fine rain fine/fixtures/proof-family-invariant-false.fine \
  > /tmp/fine-proof-invariant-false.rain
python3 fine/rainfall_validate.py \
  fine/fixtures/proof-family-invariant-false.fine \
  /tmp/fine-proof-invariant-false.rain
```

The flake install check now requires both semantic outcomes, validates the
positive trace, and requires all three public Spacer callback operations to be
present. The next source step remains explicit derivation elimination with
constructor-specific refined indices; this invariant query does not check that
TODO item by euphemism.

Declarative validation before commit:

```
nix flake check
nix build --no-link --print-out-paths
```

Both passed, including the expanded install-check matrix. The pre-commit tree
realized as `/nix/store/pvbh3nplaqi7kbk15n3jxqa13smq1aab-fine-0.0.1`.

## 2026-09-01 — two-recursive-premise Spacer pressure test

Followed the open invariant slice with one controlled increase in proof-family
shape: a `pairwise` constructor whose body contains two positive recursive
`Step` premises. The fixture uses native recursive `Tm` values:

```fine
constructor pairwise(before_first: Tm, after_first: Tm,
                     before_second: Tm, after_second: Tm) {
  takes {
    Step(before_first, after_first);
    Step(before_second, after_second);
  }
  gives Step(pair(before_first, before_second),
             pair(after_first, after_second));
}
```

The same open invariant says all related indices are unequal. The two base rules
swap `left` and `right`; the pairwise rule recursively relates both components.
The plain run verifies, and Rainfall validates with 92 events, 30 source nodes,
16 strong terms, and 12 source-term edges. Its compiler-owned
`fine.proof-constructor.rule` event for `pairwise` retains `premises: 2` and
`recursive_premises: 2` with the exact conjunctive Horn term.

The public Spacer lemma export does **not** retain the joined support. The three
observed exports were query falsity at level 0, the marginal invariant

```
Step(Step_0_n, Step_1_n) -> Step_0_n != Step_1_n
```

at level 0, and query falsity again at level 1. There was no lemma containing
both recursive body atoms or their four source parameters. This is semantically
adequate for the requested invariant but insufficient for reconstructing a
branch-sensitive induction explanation. It confirms the coverage boundary:
compiler-owned constructor rules know the conjunction; the public learned-lemma
callback reports a projected per-relation invariant after Spacer has already
forgotten that rule support.

Retain this null. Fine must not infer source constructor selection from exported
lemma shape or variable order. Explicit derivation elimination will need
compiler-owned branch and recursive-evidence identities before the query, then
may attach Spacer activity to those scopes only where an actual observer edge
exists.

Exploratory commands and artifacts:

```
.build/fine run /tmp/proof-family-two-premises.fine
.build/fine rain /tmp/proof-family-two-premises.fine \
  > /tmp/proof-family-two-premises.rain
python3 fine/rainfall_validate.py \
  /tmp/proof-family-two-premises.fine \
  /tmp/proof-family-two-premises.rain
```

Promoted source fixture: `fine/fixtures/proof-family-two-premises.fine`. The
flake install check verifies the semantic result, validates the trace, requires
the exact `pairwise` event to report two total and two recursive premises, and
requires at least one public Spacer lemma export without freezing its internal
printed variable names.

Declarative validation passed:

```
nix flake check
nix build --no-link --print-out-paths
```

The pre-commit tree realized as
`/nix/store/gy5fjb2pwzm6fm7ppm70mgc1phgr49zk-fine-0.0.1`.

## 2026-09-01 — compiler-owned first-order proof-family induction

Closed the first real derivation-induction slice instead of trying to read
proof branches back from Spacer's marginal learned lemmas. The `inducts`
parser now accepts either the existing datatype parameter name or a direct
proof-family application. The new form is deliberately narrow:

```fine
check distinct_indices(before: Tm, after: Tm) {
  inducts(Step(before, after));
  assumes { Step(before, after); }
  ensures { (before == after) == false; }
}
```

The induction target must be a declared proof family, must repeat as the sole
assumption, and must apply its indices to one check parameter each in family
order. The target and assumption are independently elaborated and required to
have exact Z3 AST identity. Proof-family atoms inside the guarantee remain
rejected.

`ProofFamilyInfo` now retains a compiler-owned constructor table beside the
fixedpoint declaration: constructor name and formal parameter terms, exact
result-index terms, total premise count, and exact index tuples for every
same-family recursive premise. This is the information the previous
`pairwise` pressure test established Spacer would not preserve for us. A branch
is generated by substituting the constructor result indices into the guarantee,
substituting each recursive premise's indices into one copy of the guarantee,
and checking

```
(induction_hypothesis_0 && ... && induction_hypothesis_n) && !branch_goal
```

with a separate ordinary Z3 solver. Constructor formals remain free constants,
so satisfiability is a concrete branch counterexample and unsatisfiability
closes the universally quantified branch. This first slice rejects a constructor
with any nonrecursive or other-family premise rather than silently dropping
needed evidence.

Rainfall opens a distinct compiler-owned scope per constructor. Each branch
retains the exact constructor-result relation term, result-specialized goal and
counterexample query, constructor parameter count, and one
`proof-induction.hypothesis` event for each exact recursive-premise/IH pair.
`proof-induction.run.close` is now a replay terminal. The proof witness erases
only after those branch identities exist; no runtime GADT or fabricated source
proof term is emitted.

Three fixtures fix the boundary. `proof-family-induction.fine` verifies `root`
and `under`; `proof-family-induction-false.fine` refutes equality and identifies
`root` as the failing constructor. `proof-family-induction-two-premises.fine`
verifies all three branches and its `pairwise` branch retains two distinct
recursive premise terms and two distinct induction-hypothesis terms. The trace
validates with 111 events, 33 source nodes, 21 strong terms, and 17 source-term
edges.

Local regression commands and observed outputs:

```
cmake --build .build -j2
.build/fine run fine/fixtures/proof-family-induction.fine
# verified-proof-induction: distinct_indices
# constructor-branches: 2 verified
.build/fine run fine/fixtures/proof-family-induction-false.fine
# refuted-proof-induction: equal_indices
# failed-constructor: root
.build/fine run fine/fixtures/proof-family-induction-two-premises.fine
# verified-proof-induction: distinct_indices
# constructor-branches: 3 verified
.build/fine rain fine/fixtures/proof-family-induction-two-premises.fine \
  > /tmp/fine-proof-family-induction-two-premises.rain
python3 fine/rainfall_validate.py \
  fine/fixtures/proof-family-induction-two-premises.fine \
  /tmp/fine-proof-family-induction-two-premises.rain
# valid rainfall: events=111, source_nodes=33, terms=21, source_term_edges=17
```

One C++ API failure was retained as a closed implementation detail:
`z3::expr::substitute` is non-const, so applying it directly to the const input
expression failed compilation. The instantiator now copies the strong expression
handle first and substitutes on the copy; identity and manager remain unchanged.

The flake install check runs both semantic controls, validates the two-premise
trace, requires the branch order and unsat results, and requires exactly two
distinct recursive-premise and IH references for `pairwise`. Remaining limits
are explicit source proof matches, existential fields, the arbitrary-fresh
cofinite branch, typed branch counterexamples, and construction/lifting of a
source proof witness.

Declarative validation passed with the staged source and expanded install-check
matrix:

```
nix flake check
nix build --no-link --print-out-paths
```

The pre-commit tree realized as
`/nix/store/5hfsri7hg1mpa1vy53axv1fz60facbby-fine-0.0.1`.

## 2026-09-02 — arbitrary constrained proof field without Horn quantifier fraud

Started the next locally nameless prerequisite at the exact polarity boundary
fixed by the indexed-proof spike. A name occurring only in a Horn body is an
existential witness, so the source does not encode cofinite quantification by
adding another fixedpoint variable. Instead Fine now has one narrow proof-only
function field:

```fine
view FreshApart(excluded: Name) over Name {
  requires {
    (value == excluded) == false;
  }
}

constructor under_abs(before: Tm, after: Tm, excluded: Name) {
  takes {
    arbitrary fresh: FreshApart(excluded) {
      Step(opened(before, fresh), opened(after, fresh), excluded);
    }
  }
  gives Step(abs(before), abs(after), excluded);
}
```

A `view` names an erased Bool proposition over one already-declared native
carrier. Its parameters and reserved carrier name `value` elaborate in the
ordinary Fine expression language. It creates no wrapper datatype or Z3 sort.
The first and only value-level use site in this slice is an `arbitrary` proof
field; general view-typed function parameters and caller entailment are still
unimplemented.

The arbitrary field retains a parse-local source object and a distinct strong
same-manager carrier constant, view name, instantiated requirement, scoped
recursive relation terms, and their exact index tuples. The field may not shadow
an ordinary constructor parameter, must use a declared view at the exact argument
types, and in this slice contains same-family recursive premises only. It is not
inserted into a Horn body.

The backend is all-or-nothing per proof family. If any constructor has an
arbitrary field, Fine does not register the family relation or **any** of its
constructors with fixedpoint. An intermediate implementation registered the
first-order constructors and blocked public queries against the resulting
partial relation. That was rejected: even unreachable through the CLI, the
partial relation is a different mathematical object from the source family and
would make its exact term handle lie. The final Rainfall relation event reports
`horn_complete: false` and `least_relation: false`; ordinary constructors appear
as `fine.proof-constructor.branch` with `lowered_to_horn: false`. Ground
membership and fixedpoint invariant checks give a precise rejection.

Proof-family induction consumes the compiler-owned constructor table. Before an
arbitrary field becomes a branch assumption, Fine checks the separate
availability formula

```
forall constructor_parameters. exists fresh. instantiated_view_requirement
```

by refuting its negation. This prevents an empty view from closing every branch
vacuously. The branch itself keeps `fresh` free and assumes the view requirement,
so the counterexample query covers every satisfying carrier value rather than
selecting one solver witness. Each scoped recursive premise yields a distinct
`proof-induction.arbitrary-hypothesis` event with the same binder and requirement
handles. Rainfall also maps the arbitrary-field source object directly to the
binder term, and records the availability formula/result separately from the
branch result.

The promoted executable fixture is
`fine/fixtures/proof-family-arbitrary-fresh-induction.fine`. `Name` is the
infinite native datatype `name(Int)`. The proxy `opened(body, fresh)` constructor
makes the opened-body identity visible without pretending this is already the
full locally nameless opening operation. Its `root` and `under_abs` branches both
verify. The admitted Rainfall trace validates with 159 events, 43 source nodes,
32 strong terms, and 36 source-term edges. It contains no
`fine.proof-constructor.rule` event; `root` is retained as a non-Horn branch,
`under_abs` retains one arbitrary field, the availability result is `available`,
and the scoped premise/IH pair shares the exact binder and view-requirement
handles from the constructor field.

Two controls prevent flattering interpretations. `reject-empty-arbitrary-view.fine`
uses the contradictory view `value == excluded && value != excluded`; Fine
rejects it with `arbitrary-fresh induction would be vacuous` before checking the
branch. `reject-arbitrary-fresh-membership.fine` attempts a ground fixedpoint
query and is rejected because the family has a constructor outside Horn
lowering. The existing two-recursive-premise first-order induction fixture still
verifies all three branches after the parser and constructor-IR changes.

Local commands:

```
cmake --build .build -j2
.build/fine run fine/fixtures/proof-family-arbitrary-fresh-induction.fine
.build/fine rain fine/fixtures/proof-family-arbitrary-fresh-induction.fine \
  > /tmp/proof-family-arbitrary-fresh-induction.rain
python3 fine/rainfall_validate.py \
  fine/fixtures/proof-family-arbitrary-fresh-induction.fine \
  /tmp/proof-family-arbitrary-fresh-induction.rain
.build/fine run fine/fixtures/proof-family-induction-two-premises.fine
```

This closes the representation and vacuity controls, not the locally nameless
exit. The fixture's `opened` constructor must still be replaced by actual
open/support operations, and the source freshness/equivariance argument must
remain explicit. Ordinary constrained-view arguments must later be discharged by
entailment at callers. There is still no proof value, explicit proof match,
existential constructor field, or typed branch counterexample.

The first declarative build failed in the new trace assertion, not in Fine: the
test expected the arbitrary carrier term to have exactly one source edge. It
correctly has several—one from the arbitrary-field node and ordinary exact edges
from each use of `fresh` in the requirement and opened recursive premise. The
assertion now filters those edges by the declared `proof.arbitrary-field` syntax
kind and requires exactly one owner edge while retaining the use-site edges.

After correcting the ownership assertion, declarative validation passed:

```
nix flake check
nix build --no-link --print-out-paths
```

The pre-commit tree realized as
`/nix/store/zv7h07qqzi7a48lmvcavn6hv4c66sai2-fine-0.0.1`.

## 2026-09-02 — constructive availability for finite cofinite support

Replaced the singleton-exclusion availability toy with an actual finite-support
representation before touching locally nameless opening. The first attempted
shape used an ordinary recursive `Names` list and

```
forall support. exists fresh. !contains(support, fresh)
```

as the existing arbitrary-field availability formula. A 20-second local control
produced no answer. That is a theorem about recursive finite support, not a model
query Z3 should be expected to invent a witness for. Retain the timeout: adding
fuel or accepting `unknown` would merely hide the missing construction.

The chosen representation uses Peano names and a cutoff:

```fine
enum Name { zero, succ(previous: Name) }
enum Support { below(limit: Name) }
```

`below(limit)` denotes the finite initial segment through `limit`; every finite
set of Peano names is contained in one such segment. `above(candidate, limit)`
is a structurally recursive Fine function, `outside(candidate, support)` unwraps
the cutoff, and `fresh_after(support)` returns the successor of its limit. The
view now states both the proposition and the proposed inhabitant:

```fine
view FreshFor(support: Support) over Name {
  requires { outside(value, support); }
  witness fresh_after(support);
}
```

This extends views with one optional carrier-valued witness expression over the
view parameters. The declaration elaborates and typechecks the witness but does
not trust it. At an arbitrary field, Fine instantiates the witness with the exact
constructor arguments, substitutes it for the view's carrier `value`, and asks
Z3 to refute

```
not (forall constructor_parameters.
       instantiated_requirement(instantiated_witness))
```

Only an unsatisfiable result admits the field. Without a declared witness the
previous direct `forall parameters. exists value. requirement` path remains.
The witness is solely availability evidence: the induction branch still uses a
separate free arbitrary carrier term, and its recursive premise and IH retain
that arbitrary term rather than the witness. This distinction is explicit in
Rainfall through `availability_mode`, `availability_witness`, `binder_term`, and
the separate availability and arbitrary-hypothesis events.

`proof-family-cofinite-support-induction.fine` promotes the Peano/cutoff
construction. Its two branches verify, and Rainfall validates with 230 events,
64 source nodes, 47 strong terms, and 53 source-term edges. The view declaration,
constructor field, and availability event retain the declared/instantiated
witness; the field binder is a different term and is the one shared by the
opened recursive premise and IH. The existing witness-free `FreshApart` fixture
still verifies through the direct existential mode.

`reject-invalid-view-witness.fine` changes `fresh_after(below(limit))` from
`succ(limit)` to `limit`. Fine proves neither folklore nor intent: the universal
requirement has a counterexample and the check is rejected with
`declared witness for constrained view FreshFor fails its requirement`. The
branch solver is never asked to launder the invalid availability evidence.

Local commands:

```
timeout 20 .build/fine run /tmp/proof-family-finite-support.fine
# status 124: direct recursive-list availability search retained as a null
cmake --build .build -j2
.build/fine run fine/fixtures/proof-family-cofinite-support-induction.fine
.build/fine rain fine/fixtures/proof-family-cofinite-support-induction.fine \
  > /tmp/proof-family-cofinite-support-induction.rain
python3 fine/rainfall_validate.py \
  fine/fixtures/proof-family-cofinite-support-induction.fine \
  /tmp/proof-family-cofinite-support-induction.rain
.build/fine run fine/fixtures/reject-invalid-view-witness.fine
```

This closes actual finite cofinite availability for the cutoff representation,
not term support or opening. The remaining semantic edge is to define real
locally nameless `open`, compute or carry a cutoff containing a term's finite
support, and retain the equivariance/injectivity argument relating arbitrary
choices above it. A chosen fresh name is never allowed to replace the arbitrary
branch binder.

Declarative validation passed:

```
nix flake check
nix build --no-link --print-out-paths
```

The pre-commit tree realized as
`/nix/store/qywg8909bkxrpzpg3hsqjjvmm6yn5jbb-fine-0.0.1`.

## 2026-09-02 — Runtime partitioned at executable semantic boundaries

`src/fine/runtime.cpp` had reached 3,178 lines and 205,694 bytes. It was no
longer one runtime mechanism: it contained the core type/elaboration path, the
erased proof-family compiler, synthesis orchestration, and the older
bisimulation/model round-trip consumer in one in-class definition.

The split is by executable ownership rather than a miscellaneous helpers file:

- `runtime_internal.h` owns the internal runtime representations and the single
  `Runtime` declaration shared by implementation units;
- `runtime.cpp` (1,356 lines) retains core type declaration, ordinary expression
  elaboration/lifting, checks, dispatch, and the public `fine::execute` boundary;
- `proof_runtime.cpp` (709 lines) owns proof-family/view declaration, ground
  membership, compiler-owned derivation induction, and Spacer invariant queries;
- `synthesis_runtime.cpp` (396 lines) owns whole-arm and top-level synthesis
  orchestration; and
- `bisimulation_runtime.cpp` (447 lines) owns the original bisimulation consumer
  plus surface model/table lift, reification, and printing.

This is a source partition only: member state, term identity, manager ownership,
and call order are unchanged. `runtime_internal.h` is private to the executable;
`runtime.h` remains the small public execution/error interface. CMake compiles
all four implementation units separately, so this is not an include-fragment
or a renamed 3,000-line file.

Validation:

```
clang-format -i src/fine/runtime_internal.h src/fine/runtime.cpp \
  src/fine/proof_runtime.cpp src/fine/synthesis_runtime.cpp \
  src/fine/bisimulation_runtime.cpp
git diff --cached --check
nix flake check
nix build --no-link --print-out-paths
```

The full declarative fixture suite passed and the clean pre-commit realization
was `/nix/store/mg4n8wxsm0qdmmarwkx0z1cm9qgqmb63-fine-0.0.1`.

## 2026-09-02 — Real locally nameless opening and computed support; equivariance remains a null

The promoted `proof-family-cofinite-support-induction.fine` removes the visible
`opened(body, fresh)` proxy and the externally supplied cutoff datatype. Its
native `Tm` datatype is now the actual locally nameless representation:

```fine
enum Tm {
  bound(index: Int),
  free(name: Int),
  app(fn: Tm, argument: Tm),
  abs(body: Tm),
}
```

Two exhaustive recursive Fine functions define the operations rather than
asking the compiler to recognize their names. `open_at(term, depth, fresh)`
replaces `bound(depth)` by `free(fresh)`, recurses at the same depth through
application, and increments the depth below abstraction. `support_cutoff(term)`
recurses through the same datatype and returns an integer at least as large as
every free name in the term. Its zero base also safely bounds negative free
names; the construction needs a finite upper bound, not an exact set.

`FreshFor(before, after)` now has two requirements, placing its carrier above
the independently computed cutoffs of both bodies. Its declared witness is the
larger cutoff plus one. Fine universally verifies that witness against both
requirements, then uses a separate arbitrary integer in the `under_abs` branch:

```fine
arbitrary fresh: FreshFor(before, after) {
  Step(open_at(before, 0, fresh), open_at(after, 0, fresh));
}
```

The two-branch derivation induction verifies. The structured trace validates
with 395 events, 107 source nodes, 75 strong terms, and 120 source-term edges.
The flake assertions now require separate `function.recursive-definition`
events for `open_at` and `support_cutoff`; the arbitrary field's requirement must
contain `support_cutoff`, while its exact recursive premise must contain both the
retained arbitrary term and the `open_at` applications. The availability
witness remains a different strong term.

This is actual opening, support computation, and arbitrary-branch trace identity,
not yet the theorem that makes all above-cutoff choices interchangeable. I tried
the direct source statement

```
rename(open_at(term, depth, fresh), fresh, other)
  == open_at(term, depth, other)
```

under assumptions that `fresh` and `other` both exceed `support_cutoff(term)`.
Fine's ordinary `inducts(term)` translation exceeded twenty seconds without an
answer. No solver fuel was added. The exact failed source and reproduction are
retained in `fine/spikes/indexed-proof/open-equivariance-timeout.fine` and its
README. The likely missing object is constructor-owned induction evidence for
the nested `rename`/`open_at` recursion, not another freshness witness. Until it
is proved, Rainfall records availability and arbitrary scope but never calls it
equivariance.

Validation:

```
nix run . -- run fine/fixtures/proof-family-cofinite-support-induction.fine
nix run . -- rain fine/fixtures/proof-family-cofinite-support-induction.fine \
  > /tmp/proof-family-locally-nameless-open.rain
python3 fine/rainfall_validate.py \
  fine/fixtures/proof-family-cofinite-support-induction.fine \
  /tmp/proof-family-locally-nameless-open.rain

timeout 20 nix run . -- run \
  fine/spikes/indexed-proof/open-equivariance-timeout.fine
# status 124, deliberately retained

nix flake check
nix build --no-link --print-out-paths
```

The clean pre-commit realization was
`/nix/store/q37ghy05v9256r2582ipkg3m0kjiak77-fine-0.0.1`.

## 2026-09-02 — Rainfall located and removed the equivariance matching loop

The direct opening-equivariance check initially exceeded twenty seconds. Merely
retaining that timeout was the wrong diagnostic boundary: this is precisely the
case Rainfall is supposed to make non-opaque. I reran the exact source through a
line-buffered timed `fine rain` invocation. The killed run retained 553,053 events
and 505,173,153 bytes in twenty seconds:

- 553 accepted `z3.quantifier-instance` events, but only 64 distinct ground
  instance terms;
- 88,914 admitted clauses, 395,434 inferred clauses, and 1,565 deletions;
- 66,390 strong terms; and
- no public query result before timeout.

The trace showed that the induction hypothesis was accepted repeatedly, not
missing. Fine's old structural-induction translation built one formula

```
forall smaller.
  direct_subterm(smaller, term) -> theorem(smaller)
```

and left pattern choice to preprocessing. The resulting quantifier had three
patterns: `open_at(smaller, depth, fresh)`, `open_at(smaller, depth, other)`, and
`support_cutoff(smaller)`. Recursive-function unfolding manufactured terms such
as nested `body(abs(body(fn(body(...)))))`; each matched the hypothesis again
even though its direct-subterm guard was irrelevant. Rainfall therefore located
an E-matching selector-growth loop. Increasing fuel or tuning one trigger would
have preserved the wrong induction object.

Fine now compiles ordinary datatype induction in the same source-owned shape as
its proof-family induction. It constructs one guarded branch per datatype
constructor and one exact hypothesis per direct recursive field. Every such IH
is generalized over all remaining check parameters. This last part is semantic,
not merely an optimization: in the abstraction case the body hypothesis must be
available at `depth + 1`, not only at the outer free depth constant. No quantifier
ranges over arbitrary `Tm` values, so recursive unfolding cannot turn selector
chains into new candidate subterms.

Rainfall adds `check.induction.branch` and `check.induction.hypothesis` events.
The translation event now reports `order: constructor-direct-field`, the exact
constructor/recursive-position counts, number of generalized parameters, and
`responsibility: fine-generated-constructor-induction`. Generalized IH
quantifiers use field-specific qids such as
`fine.induction.opening_equivariant.abs.field0`.

The formerly timed-out source is promoted from the spike to
`fine/fixtures/induction-open-equivariance.fine`. It now verifies immediately.
Its complete admitted trace validates with 9,804 events and only 16 accepted
instances: seven for each application field and two for the abstraction body.
All accepted instance source roles are exact field qids; the former generic
`fine.induction.opening_equivariant.term` role is absent. The fixture asserts the
actual name-choice equation under two computed support bounds. The ordinary
length fixture still verifies, its false `length(xs) == 1` control still returns
`xs = nil` with exact parse/lift/reify identity, and its single recursive IH needs
no generalized quantifier.

Commands:

```
timeout 20 stdbuf -oL \
  /nix/store/r067ihihxnzqw0jd1ybaxkgkd20xigcy-fine-0.0.1/bin/fine rain \
  fine/spikes/indexed-proof/open-equivariance-timeout.fine \
  > /tmp/open-equivariance-timeout.rain
# status 124; 553053 lines, 505173153 bytes

cmake --build .build -j2
.build/fine run fine/fixtures/induction-length.fine
.build/fine run fine/fixtures/induction-open-equivariance.fine
.build/fine rain fine/fixtures/induction-open-equivariance.fine \
  > /tmp/open-equivariance-fixed.rain
python3 fine/rainfall_validate.py \
  fine/fixtures/induction-open-equivariance.fine \
  /tmp/open-equivariance-fixed.rain

nix flake check
nix build --no-link --print-out-paths
```

The complete declarative suite passed. The clean pre-commit realization was
`/nix/store/vfgj3afy5j548hy2gh7ds9nk8nwkq3b1-fine-0.0.1`.

## 2026-09-02 — verified reusable lemmas before the full STLC consumer

The opening-equivariance theorem had become an executable check, but the later
proof-family induction could not consume it. Re-running the same recursive
opening equality inside a constructor branch without structural induction still
exceeded a two-second control. This was the first justified consumer for a
source lemma boundary: it is required by the locally nameless target, unlike the
paused arithmetic synthesis demonstration.

Fine now parses `lemma` with the same parameters, `inducts`, `assumes`, and
`ensures` surface as `check`. Execution is source-ordered. A lemma runs its own
ordinary counterexample query immediately and is reusable only when that query
is `unsat`; a satisfiable lemma returns the usual typed, reparsed, exact-identity
counterexample, exits with status 1, and never runs the later executable
declaration. Successful admission universally closes the original theorem over
its source parameters with qid `fine.lemma.<name>`. It deliberately closes the
theorem, not Fine's generated constructor induction step. A lemma must occur
before proof-family declarations, because this slice admits the result only to
later ordinary SMT solvers, proof-induction branch solvers, and arbitrary-field
availability solvers. It does not silently add arbitrary background axioms to a
previously registered Spacer relation.

`Runtime` retains each admitted theorem as a strong same-manager `z3::expr`.
Rainfall gives the lemma declaration its own `decl.lemma` source kind, records
`lemma.admit` only after verification, and records each `lemma.use` with the exact
retained theorem and consumer/constructor (plus availability phase when
applicable). Thus source verification, universal closure, and later use remain
three distinct claims; query chronology alone is not treated as provenance.

The executable fixture `fine/fixtures/reusable-lemma-proof-induction.fine`
contains the actual locally nameless `Tm`, `open_at`, `support_cutoff`, and
`rename`. It first proves `opening_equivariant` by the new
constructor/direct-field structural induction. It then defines a native-index
`SafeOpening` proof family whose `generated` constructor fixes two names above
the term's computed support. The later derivation induction discharges its
opening equality from the admitted lemma. The full run reports one verified
lemma and one verified proof constructor. Its validated Rainfall trace contains
exactly one admission and one branch use. Deleting the lemma makes the symbolic
recursive opening branch exceed two seconds. A separate false integer lemma
returns `x = 0`, exits nonzero, and proves the following check never ran.

Commands during the slice:

```
cmake --build .build -j2
.build/fine run /tmp/reusable.fine
.build/fine rain /tmp/reusable.fine > /tmp/reusable.rain
.build/fine run fine/fixtures/reusable-lemma-proof-induction.fine
# control after deleting lemma:
timeout 5 .build/fine run /tmp/no-lemma.fine  # status 124
.build/fine run /tmp/false-lemma.fine         # status 1
```

The next semantic work is not “more lemma infrastructure.” It is the full STLC
fixture whose beta and abstraction branches determine which substitution,
opening, and typing lemmas are actually necessary.

## 2026-09-02 — one proof vocabulary; model search is `solve`

The first reusable-theorem slice used `lemma` because it retained a theorem but
no source proof term. h objected that Fine should simply use `proof`. The prior
name was defending a hypothetical distinction which the rest of the language
already rejects: `proof family` deliberately carries compiler-owned derivation
structure whose value witnesses erase. The more coherent surface gives `proof`
to both erased theorem declarations and indexed proof families, while moving the
old finite-model query out of the way.

The three forms now have non-overlapping grammar and behavior:

```
proof append_length(xs: List, ys: List) { ... }
proof family Step(before: Tm, after: Tm) { ... }
solve bisimulation { takes(...); gives(bisim); }
```

A named `proof` still has exactly the verified-theorem semantics implemented in
the preceding slice: Fine refutes its negation, universally closes the original
source theorem only on `unsat`, and then makes the theorem available to later
ordinary and proof-branch SMT queries. Rainfall now calls these boundaries
`proof.admit` and `proof.use`, gives the source node kind `decl.proof`, and uses
qid `fine.proof.<name>`. A refuted proof prints its typed counterexample and
stops. The obsolete `lemma` keyword has no compatibility alias.

The former `ProofDecl` model-query AST is renamed `SolveDecl`; its source node is
`decl.solve`, and `two-state-bisim.fine` now says `solve bisimulation`. This is
not cosmetic: the declaration asks Z3 to fill a `model` hole and returns that
model, so it was the form least entitled to monopolize the word `proof`.
Projection tests now require its four generated bisimulation clauses to attach
to `decl.solve`.

The executable `proof-append-length.fine` is the larger ordinary consumer h
requested. Fine proves `length(append(xs,ys)) = length(xs)+length(ys)` by exact
constructor/direct-field induction, then a later non-inductive
`three_chunk_size` check instantiates the retained proof twice. Removing the
proof makes that raw recursive check exceed two seconds. The existing locally
nameless fixture is renamed `reusable-proof-induction.fine` and still proves one
source theorem before using it in a native-index proof-family branch.

Local validation commands:

```
cmake --build .build -j2
.build/fine run fine/fixtures/proof-append-length.fine
.build/fine run fine/fixtures/reusable-proof-induction.fine
.build/fine run fine/fixtures/two-state-bisim.fine
.build/fine rain fine/fixtures/reusable-proof-induction.fine > /tmp/proof.rain
python3 fine/rainfall_validate.py \
  fine/fixtures/reusable-proof-induction.fine /tmp/proof.rain
.build/fine rain fine/fixtures/two-state-bisim.fine > /tmp/solve.rain
python3 fine/rainfall_validate.py fine/fixtures/two-state-bisim.fine /tmp/solve.rain
```

## 2026-09-02 — constructor-generated propositions are `predicate`, not proof values

The preceding vocabulary unification left one semantic lie: `proof family`
suggested a family of inhabitable Fine proof values, while the implementation
has always erased every derivation before value/model construction. h proposed
making the constructors the distinguishing feature instead. The surface is now:

```fine
predicate Step(before: Tm, after: Tm) {
  constructor root() {
    takes {}
    gives Step(atom, mark);
  }

  constructor under(before: Tm, after: Tm) {
    takes { Step(before, after); }
    gives Step(wrap(before), wrap(after));
  }
}
```

`predicate` is not an arbitrary formula declaration. Its `constructor` blocks
generate the smallest set of index tuples closed under those constructors.
This keeps the property that the ordinary SMT implications alone lacked:
`Step(atom, atom)` is excluded when no finite constructor derivation produces
it. Horn-complete predicates are registered as native-sort Z3 fixedpoint
relations. Predicates with an arbitrary constrained field retain their complete
compiler-owned constructor table for induction and remain wholly outside
fixedpoint rather than receiving an incomplete relation. `Step(...)` is still a
Bool expression at membership and assumption sites, while
`inducts(Step(...))` invokes Fine's retained constructor branches.

The parser now has three honest, disjoint declaration forms:

```
proof theorem(parameters) { assumes { ... } ensures { ... } }
predicate Relation(indices) { constructor ... }
solve model_query { takes(...); gives(model_hole); }
```

There is no `family` keyword or compatibility alias. `proof family Step` is
rejected by an install-check control. Internally the old `ProofFamilyDecl`,
`ProofFamilyInfo`, proof-family runtime, maps, and induction target were renamed
to `PredicateDecl`, `PredicateInfo`, `predicate_runtime.cpp`, predicate maps,
and predicate induction. This was deliberately more than a lexer alias: error
messages, output labels, fixture names, documentation, and Rainfall vocabulary
all changed with it.

Rainfall now owns the constructor-generated objects under
`decl.predicate`, `fine.predicate.relation`,
`fine.predicate-constructor.{rule,branch,arbitrary-field}`, and
`predicate-induction.*`. Data fields say `predicate`; the erasure claim is
`derivation_witness_erased`. Public output says `predicate: Step`,
`derivation-witness: erased`, `verified-predicate-induction`, and
`verified-predicate-invariant`. The replay validator recognizes
`predicate-check.run.close` and `predicate-induction.run.close`. Z3's own
`z3.spacer.lemma-export` remains unchanged because it names an actual Spacer
callback rather than Fine surface syntax. Reusable source theorems likewise
remain `proof`, with `proof.admit` and `proof.use` unchanged.

All former `proof-family-*.fine` fixtures are now `predicate-*.fine`; the
locally nameless theorem-reuse fixture remains
`reusable-proof-induction.fine` because its defining test is a named source
`proof` consumed by a later predicate branch. Representative runs preserve the
same semantics:

```
cmake --build .build -j2
.build/fine run fine/fixtures/predicate-step.fine
.build/fine run fine/fixtures/predicate-induction.fine
.build/fine run fine/fixtures/predicate-cofinite-support-induction.fine
.build/fine run fine/fixtures/reusable-proof-induction.fine
.build/fine run fine/fixtures/proof-append-length.fine
.build/fine run fine/fixtures/two-state-bisim.fine
.build/fine rain fine/fixtures/predicate-induction-two-premises.fine > /tmp/predicate.rain
python3 fine/rainfall_replay.py \
  fine/fixtures/predicate-induction-two-premises.fine /tmp/predicate.rain
nix flake check
nix build --no-link --print-out-paths
```

The dirty-tree Nix build, including the renamed executable fixtures, old-syntax
rejection, exact Rainfall assertions, live-host tests, and existing controls,
produced `/nix/store/bhy6dss02pzskbcfxhw07dbvayq4xz1d-fine-0.0.1`.
The remaining semantic boundary is unchanged: these predicates have no source
proof-term values, general inversion form, existential constructor fields, or
typed branch counterexamples. If Fine later acquires actual first-class proof
values, `proof family` remains available for that genuinely different feature.

## 2026-09-02 — public-repository boundary

h invited Fine to become public. The checkout still used the upstream
`Z3Prover/z3` remote and the root landing page was Z3's README, which made the
fork relation visible but hid Fine itself. The original document is preserved
verbatim as `README-Z3.md`; a new root `README.md` now leads with the exact
same-manager `reify(lift(x)) = x` boundary, Rainfall's separation of source
ownership from solver observation, one executable constructor-generated
`predicate` example, the current supported/unsupported surface, reproducible
Nix commands, and explicit links to the detailed architecture and experiment
log. The README example was extracted to `/tmp/readme-example.fine` and run
through the current compiler; its two constructor branches verify.

The publication audit used three independent checks:

```
git ls-files | grep -Ei '<credential-shaped filenames>'
git grep -nEI '<private-key and provider-token patterns>' HEAD
nix shell nixpkgs#gitleaks --command \
  gitleaks git . --no-banner --redact --report-format json \
  --report-path /tmp/fine-gitleaks.json
nix shell nixpkgs#gitleaks --command \
  gitleaks dir . --no-banner --redact --report-format json \
  --report-path /tmp/fine-tree-gitleaks.json
```

No credential-shaped filenames or direct token/private-key patterns are
tracked. Gitleaks scanned 23,456 commits and about 120 MB of Git history. Its 12
history findings were inspected at their exact commits: eleven are substrings
such as `z3_api.h` misclassified by the generic API-key rule, and one is a
GitHub Actions schema description containing literal `${{ secrets.MY_SECRET }}`
placeholders. The working-tree scan found the same `z3_api.h` false positive in
`src/api/js/scripts/parse-api.ts`. No finding contained a credential. The only
tracked PDF among the largest blobs is upstream Z3's
`examples/userPropagator/example.pdf`; Fine's downloaded research papers are
not in this repository.

Z3's existing MIT `LICENSE.txt`, full upstream Git history, and original README
are retained. The public repository is intended as `rotbotd/fine`, with the
local upstream remote preserved separately rather than overwritten so future
Z3 comparison remains explicit.

Publication completed at <https://github.com/rotbotd/fine>. The GitHub default
branch is `main`; the local historical branch remains `fine/main` and tracks it.
Remote names now state ownership accurately: `origin` is `rotbotd/fine`, while
`upstream` is `Z3Prover/z3`; an explicit `HEAD:main` push refspec prevents the
local slash-bearing name from creating a second public branch. Repository topics
are `z3`, `smt`, `programming-languages`, `formal-methods`, and
`theorem-proving`. GitHub Actions are disabled at repository scope for the
initial publication because the retained upstream tree contains Z3's large
workflow suite, not Fine-specific CI; the locked Nix install check remains the
release gate until a bounded Fine workflow is written.

## 2026-09-02 — contextual derivation induction

The first attempt to write the actual preservation theorem exposed an earlier
blocker than typing inversion: predicate induction required exactly its target
atom in `assumes` and exactly one check parameter per predicate index. A theorem
of the form

```
forall before after context.
  Step(before, after) -> A(before, context) -> G(after, context)
```

cannot use an IH which merely retains the branch's one free `context` constant.
The recursive proof often needs a different context value. Locally nameless
preservation will require environments and types to be generalized the same way
structural induction already generalizes non-induction parameters.

`execute_predicate_induction` now gives the syntax an exact division. The
leading check parameters remain the predicate indices and must appear directly,
in order, in `inducts(F(...))`. Every later check parameter is context. The first
`assumes` clause must still be the identical induction target; subsequent
clauses are conjoined as `A(indices, context)`. Guarantees form
`G(indices, context)`. For each recursive constructor premise at indices `r`,
Fine builds

```
forall context. A(r, context) -> G(r, context)
```

with fresh same-manager binder constants and a stable
`fine.predicate-induction.<check>.<constructor>.<premise>` qid. For the current
branch it separately substitutes the constructor result indices into both the
auxiliary assumptions and the guarantee, then asks whether
`IHs && A(result, context) && !G(result, context)` is satisfiable. With no extra
parameters or assumptions, the previous exact guarantee-shaped IH is preserved.
Arbitrary-field recursive premises use the same contextual-IH builder, so their
binder ownership and availability checks remain separate.

The executable discriminator is
`fine/fixtures/predicate-context-induction.fine`. `SameHeight` relates `zero` to
`zero` and closes under `succ`; `height` is a native recursive Fine function.
The theorem transports `height(before) <= ceiling` to `after`. In the recursive
branch the source assumption gives `height(before) <= ceiling - 1`, so Z3 must
instantiate the universally generalized IH at `ceiling - 1` before rebuilding
the successor bound. A fixed-ceiling IH cannot close that branch. The false
control strengthens the result to `height(after) + 1 <= ceiling` and correctly
names `root` as the failing constructor.

Rainfall's `predicate-induction.run.open` now retains context-parameter and
context-assumption counts plus the original auxiliary-assumption term. Each
recursive hypothesis retains its generalized-parameter count and exact
qid-bearing universal term. Each branch retains the separately specialized
context-assumption term, goal, counterexample query, and public result. The
install check verifies the universal rendering rather than accepting branch
success alone. Existing zero-context two-premise, cofinite arbitrary-field, and
reusable-proof traces still validate unchanged.

Commands and results:

```
cmake --build .build -j2
.build/fine run fine/fixtures/predicate-context-induction.fine
# verified-predicate-induction: preserves_ceiling
.build/fine run fine/fixtures/predicate-context-induction-false.fine
# refuted-predicate-induction: strict_ceiling; failed-constructor: root
.build/fine rain fine/fixtures/predicate-context-induction.fine > /tmp/context.rain
python3 fine/rainfall_replay.py \
  fine/fixtures/predicate-context-induction.fine /tmp/context.rain
nix build --no-link --print-out-paths
# /nix/store/k6ppijh0bip4jblzpxgyv9fkh5qs05y0-fine-0.0.1
```

This closes contextual parameters and ordinary setup assumptions, not STLC
preservation. Predicate atoms remain rejected in guarantees, and an auxiliary
`HasType` atom would still be opaque to the ordinary branch solver: Fine has no
constructor-owned inversion of that typing derivation. The next justified slice
is the smallest target-predicate construction/inversion mechanism forced by the
application and abstraction preservation branches, not a general relaxation of
predicate atoms inside formulas.

## 2026-09-02 — bounded predicate preservation

The contextual induction slice left the first actual preservation branch unable
to consume or produce a second constructor-generated predicate. An auxiliary
`Marked(before)` was only an uninterpreted Boolean application in the ordinary
branch solver, and `Marked(after)` was rejected syntactically as a guarantee.
The missing object was not a general formula rewrite. It was exactly one source
constructor layer on each side of the implication.

Each retained predicate constructor now keeps its complete elaborated premise
list in addition to its result indices and recursive-premise bookkeeping. For a
Horn-complete predicate atom `P(indices)`, Fine can therefore build the finite
one-layer formula

```
or over constructors C:
  exists C.parameters.
    indices == C.result_indices && C.premises
```

with the appropriate equality for every predicate index. A direct auxiliary
predicate assumption becomes the conjunction of the original atom and that
one-layer inversion. Keeping the atom is intentional: the branch receives both
the exact assumed proposition and the compiler-owned elimination resources,
rather than silently replacing source evidence with a derived formula. A sole
direct predicate guarantee is checked by refuting the same one-layer formula at
the constructor-specialized goal indices. Recursive premise atoms remain in the
formula and must be supplied by the predicate-induction IH. Nested or negative
predicate formulas, more than one ensured atom when a predicate is present, and
predicates containing arbitrary fields are rejected rather than incompletely
expanded.

The first implementation used the ordinary logical introduction direction. It
universally asserted every retained `Marked` constructor rule in each Step
branch. This is sound and the true preservation fixture verified, but the false
control did not return: the recursive rule
`Marked(x) -> Marked(succ(succ(x)))` caused E-matching to keep manufacturing
larger marked terms. After more than thirty seconds the `.build/fine` process
was still at approximately 99.7% CPU and 433 MB, and was killed. This null is
retained rather than hidden with a larger timeout. The replacement never installs
recursive universal axioms. It asks only for the bounded top-constructor
construction formula at the particular branch goal, which closes the true
fixture and refutes the false one immediately.

`fine/fixtures/predicate-preservation.fine` gives `Marked` the constructors
`base: Marked(zero)` and `grow: Marked(x) -> Marked(succ(succ(x)))`. Its `Step`
relation maps zero to two and is closed under adding two at both indices. The
recursive preservation branch now has to perform three distinct moves:

1. invert `Marked(succ(succ(before)))` to recover `Marked(before)`;
2. apply the exact Step IH to get `Marked(after)`;
3. select the `grow` alternative to construct `Marked(succ(succ(after)))`.

The theorem verifies in two branches. `predicate-preservation-false.fine` asks
for `Marked(succ(after))` and identifies `root` as the failing constructor.
This is a minimal preservation skeleton, not full locally nameless STLC.

Rainfall adds `predicate-induction.assumption.invert` and
`predicate-induction.goal.construct` events. Each records the Step branch owner,
secondary predicate name, original atom or goal, exact generated inversion or
construction, and constructor-alternative count. Assumption inversion also
retains the distinct conjunction used as the branch resource. The run-open
event counts direct predicate
assumptions and names the direct predicate guarantee. Branch-open retains the
original specialized assumptions separately from their inversion resources, and
the original goal separately from its construction resource. The install check
requires two `Marked` alternatives in both Step branches, a distinct recursive
IH in `under`, different atom/resource handles, two unsatisfiable public branch
results, and the absence of the discarded universal
`predicate-induction.goal-constructor` events.

Regression and release commands:

```
cmake --build .build -j2
for f in predicate-context-induction predicate-induction-two-premises \
  predicate-cofinite-support-induction reusable-proof-induction \
  predicate-preservation; do
  .build/fine run fine/fixtures/$f.fine
  .build/fine rain fine/fixtures/$f.fine > /tmp/$f.rain
  python3 fine/rainfall_replay.py schemas/rainfall-v2.schema.json \
    schemas/rainfall-v2-semantics.schema.json /tmp/$f.rain
done
.build/fine run fine/fixtures/predicate-preservation-false.fine
# refuted-predicate-induction: odd_mark_preservation
# failed-constructor: root
nix flake check
nix build --no-link --print-out-paths
# /nix/store/qcjd64z5yab6k82rhn2w37nl6l577744-fine-0.0.1
```

The remaining STLC edge is now narrower and harsher. A real typing derivation
will need environment lookup and beta/opening lemmas, while the abstraction rule
contains an arbitrary cofinite field. The current inversion is intentionally
limited to Horn-complete predicates, so it cannot yet eliminate precisely that
mixed first-order/arbitrary typing evidence. Broadening formula syntax before
that source-semantic rule is fixed would only obscure the boundary.

## 2026-09-02 — one-layer inversion of a total constrained field

The Horn-complete preservation slice left exactly the abstraction-shaped case
out: a secondary predicate used as an assumption or goal could not contain an
`arbitrary x: View(args) { recursive premises }` constructor field. Rejecting it
was safe, but it also prevented typing inversion at the locally nameless
boundary. Reusing the first-order one-layer formula without representing the
field would have been an incomplete second predicate wearing the source name.

The retained field already contained every required object: its free
same-manager binder template, instantiated view requirement, optional declared
availability witness, scoped recursive premise terms, and constructor
parameters. Its one-layer constructor alternative now has the exact first-order
shape

```
exists constructor_parameters.
  requested_indices == constructor_result_indices
  && ordinary_premises
  && availability
  && forall binder. requirement -> conjunction(scoped_recursive_premises)
```

`availability` is the view requirement instantiated at a declared witness when
one exists, otherwise `exists binder. requirement`. Before the predicate is used
for inversion or construction, Fine separately closes this formula over all
constructor parameters and refutes its negation. Previously admitted source
proofs are available to that check and get their own `proof.use` event. An
unknown or satisfiable availability query rejects the use rather than making the
constructor alternative false and quietly proving from an empty field. The
availability formula remains inside the alternative as well: it is part of the
exact source field evidence, not merely a compiler side condition.

The universal half is not a Horn body and is never reduced to one chosen name.
Its binder scopes both the requirement and every recursive premise. Constructor
parameters are quantified outside it, so a result-index match selects one top
constructor and then retains its total erased proof function. This is used in
both directions: an auxiliary atom keeps its original relation application plus
the one-layer inversion; a sole positive predicate goal must establish the same
one-layer formula. The secondary predicate remains `horn_complete = false` and
`least_relation = false`; no fixedpoint rule is registered for any of its
constructors.

The first scratch discriminator used a view whose requirement was merely true
and a recursive premise independent of its binder. It verified, but it did not
distinguish a total field from a decorative quantifier, so it was discarded.
The promoted fixture `predicate-arbitrary-preservation.fine` instead uses the
singleton constrained view

```
view At(expected: Nat) over Nat {
  requires { value == expected; }
  witness expected;
}
```

and gives `Marked.grow(value)` the field

```
arbitrary token: At(value) { Marked(token); }
```

with result `Marked(succ(succ(value)))`. Consequently the retained field is
literally `forall token. token == value -> Marked(token)`: the recursive premise
mentions the bound name and cannot be replaced by a single existential witness.
The existing even-step relation preserves `Marked`. Its root and recursive
branches both need the total field on the assumption side and again on the goal
side. The theorem verifies; the odd-result control refutes at `root`.

The invalid-witness control changes the declared witness to `succ(expected)`.
Fine rejects it before constructing either branch:

```
constrained field `Marked.grow.token` is unavailable; one-layer predicate use
would be vacuous
```

Rainfall adds one
`predicate-induction.one-layer.availability` transition for the unique Marked
field. For each Step branch it then records a
`predicate-induction.goal.arbitrary-field` and a corresponding assumption event.
Every field event owns the secondary predicate and constructor, consumer Step
constructor, binder and view, exact requirement, exact availability, exact
premise conjunction, total universal field, witness mode, and recursive-premise
count. The enclosing construction/inversion terms retain the outer existential
constructor parameter around that universal. The install check requires the
four `(root/under) x (goal/assumption)` uses, a non-Horn Marked relation, the
nested `exists`/`forall` renderings, the one successful availability result, two
unsatisfiable true branches, the false root result, and the invalid-witness
rejection.

Commands and results:

```
cmake --build .build -j2
.build/fine run fine/fixtures/predicate-arbitrary-preservation.fine
# verified-predicate-induction: marked_preservation
.build/fine run fine/fixtures/predicate-arbitrary-preservation-false.fine
# refuted-predicate-induction: odd_mark_preservation; failed-constructor: root
.build/fine run \
  fine/fixtures/predicate-arbitrary-preservation-invalid-witness.fine
# error: constrained field `Marked.grow.token` is unavailable
.build/fine rain fine/fixtures/predicate-arbitrary-preservation.fine \
  > /tmp/predicate-arbitrary-preservation.rain
python3 fine/rainfall_replay.py schemas/rainfall-v2.schema.json \
  schemas/rainfall-v2-semantics.schema.json \
  /tmp/predicate-arbitrary-preservation.rain
nix flake check
nix build --no-link --print-out-paths
# /nix/store/y3x5bnybs49jgpp68bvw5rk2gsrc7swc-fine-0.0.1
```

This closes the generic non-Horn secondary-predicate field, not locally nameless
preservation. In the actual abstraction branch, Step owns one arbitrary fresh
name while typing inversion/construction owns another total field binder. The
next executable theorem must show how opening equivariance lets the branch use
the Step IH at the name demanded by the typing field without identifying the two
binders or replacing either by the availability witness.

## 2026-09-02 — total induction hypotheses for arbitrary fields

The secondary-predicate field formula exposed a mismatch on the target side.
An `arbitrary fresh: View(...) { Step(opened...) }` constructor is an erased
function field, but predicate induction retained only one free binder constant,
its requirement, and one IH instance. A branch whose goal did not mention that
name could still verify: an unsatisfiable query with a free constant checks every
value satisfying the requirement. That was extensionally sufficient for the
old distinct-indices fixtures, but it was not a compositional representation of
the field. A second independently bound field could not consume the IH at its
own name.

The target arbitrary-field resource now has three explicit layers. Fine first
constructs one ordinary IH template per scoped recursive premise at the retained
free binder. It conjoins those templates and then binds the field as

```
forall binder.
  requirement -> conjunction(induction-hypothesis templates)
```

The separately verified availability resource is admitted beside this total IH.
With a declared witness it is the requirement instantiated at that witness;
without one it remains `exists binder. requirement`. The witness is therefore
only evidence that the domain is inhabited. It is never substituted for the
universally bound derivation name. Rainfall keeps the individual
`predicate-induction.arbitrary-hypothesis` events and adds
`predicate-induction.arbitrary.total-hypothesis`, containing the binder,
requirement, branch availability resource, IH-template conjunction, exact
universal term, and recursive-IH count.

The discriminator is `predicate-total-field-preservation.fine`. Both relations
are non-Horn. `Step.under_abs` owns an arbitrary `branch_name` with the recursive
premise

```
Step(open_at(before, 0, branch_name),
     open_at(after, 0, branch_name))
```

while `Marked.under_abs` owns an independently interned `fresh` and the analogous
Marked premise. Both use the same two-body `FreshFor` view, deliberately removing
opening equivariance from this test. The theorem flips the secondary evidence:

```
Step(before, after) && Marked(before, after)
  -> Marked(after, before)
```

In the abstraction branch, Marked inversion supplies its total premise at every
`fresh`; Marked construction demands the flipped premise at every `fresh`.
Step's total IH can be instantiated at that name even though its retained
binder handle is `branch_name`. The two handles remain unequal. The base branch
selects separate forward/backward Marked constructors.

This fixture discriminates the representation rather than merely documenting
it. During the slice, the new source was saved, `predicate_runtime.cpp` was
replaced with the exact predecessor `d46321c55` version, and the local binary was
rebuilt. The predecessor returned:

```
refuted-predicate-induction: marked_flip
failed-constructor: under_abs
```

Restoring the total-IH implementation and rebuilding verified both branches.
The source stayed identical across the two runs. The diagonal false control
asks for `Marked(after, after)` and still fails at `root`, so a total field does
not rubber-stamp positive predicate goals.

The install check validates both public results and the trace. It requires Step
and Marked to remain non-Horn, one `under_abs` total-IH event with binder
`branch_name`, a rendered universal containing the two Marked sides of the
contextual theorem, secondary goal/assumption fields bound by `fresh`, unequal
binder handles across the two derivation sites, and an unsatisfiable
`under_abs` branch. Existing arbitrary-field, cofinite-support, reusable-proof,
contextual-induction, two-premise, and preservation traces still validate.

Commands and results:

```
cmake --build .build -j2
.build/fine run fine/fixtures/predicate-total-field-preservation.fine
# verified-predicate-induction: marked_flip
.build/fine run fine/fixtures/predicate-total-field-preservation-false.fine
# refuted-predicate-induction: marked_diagonal; failed-constructor: root
.build/fine rain fine/fixtures/predicate-total-field-preservation.fine \
  > /tmp/predicate-total-field-preservation.rain
python3 fine/rainfall_replay.py schemas/rainfall-v2.schema.json \
  schemas/rainfall-v2-semantics.schema.json \
  /tmp/predicate-total-field-preservation.rain
nix flake check
nix build --no-link --print-out-paths
# /nix/store/r5di9b6dp60rha00qjxj4z1lf9cmh1pc-fine-0.0.1
```

This closes independent binder identity when the two total fields have the same
admissibility predicate. It does not yet use opening equivariance. The next
fixture must give the Step and typing fields genuinely different freshness
domains so the typing binder cannot directly instantiate the Step IH. That is
where a separately proved name-transport theorem becomes necessary rather than
ornamental.

## 2026-09-02 — reusable proofs by predicate induction

The first attempt to state a predicate-respecting renaming theorem exposed a
language contradiction before it exposed any solver behavior. A scratch
`proof marked_rename(...) { inducts(Marked(term)); ... }` necessarily followed
its `Marked` declaration, but `execute_check` rejected every reusable proof once
any predicate existed:

```
a reusable proof must precede predicate declarations; it is admitted only to
later SMT queries, never retrofitted into a fixedpoint relation
```

The no-retrofit invariant was correct; the declaration-order restriction was
not. A theorem established by derivation induction cannot precede the
constructor table it quantifies over. The restriction made the advertised
"proof has the same obligation as check" surface unable to state precisely the
type/predicate equivariance theorem required by the unequal-freshness-domain
fixture.

Fine now admits one deliberately narrow new proof shape. A reusable proof after
predicate declarations must write `inducts(P(indices))` and repeat that exact
atom first in `assumes`, so it enters the existing Fine-owned predicate-induction
path. Fine elaborates the exact source theorem

```
P(indices) && auxiliary assumptions -> guarantees
```

but does not assume it while checking itself. It runs the retained constructor
branches exactly as an ordinary predicate-induction check does. Only if every
branch query is unsatisfiable does it universally bind all index and context
parameters under `fine.proof.<name>`, append the strong same-manager theorem to
the later-SMT proof table, and record `proof.admit`. The theorem is never added
to `fixedpoint_`; the Rainfall event says `added_to_fixedpoint: false`. An
unknown branch remains an error. The first satisfiable branch prints
`refuted-proof`, names the failed constructor, returns status 1, and prevents all
later declarations from executing. Ordinary proofs declared before predicates
retain their typed-counterexample path. A proof after predicates without
predicate induction is rejected rather than silently treated as a fixedpoint
invariant or membership query.

`predicate-reusable-proof.fine` is discriminating rather than merely a green
example. `proof step_distinct` establishes by the two retained `Step`
constructors that every step has unequal indices. A later nonrecursive
`Request(before, after)` predicate has one constructor admitting every pair.
Its final induction check assumes both `Request(before, after)` and
`Step(before, after)` and asks for unequal indices. With the admitted theorem,
its sole `requested` branch is unsatisfiable. Removing the proof from the exact
same source produces:

```
refuted-predicate-induction: use_step_distinct
failed-constructor: requested
```

Thus the consumer does not happen to re-prove the result through its own
constructor table. `predicate-reusable-proof-false.fine` asks for equality
instead; `Step.root` refutes it, the process exits 1, and the later `unreachable`
check never runs.

Rainfall uses `proof.run.open` / `proof.run.close` for the reusable declaration,
so closing it does not falsely terminate a multi-declaration trace. Between
those scopes it retains both `predicate-induction.branch.result` events, then
the exact admitted universal theorem and qid. The consumer records one
`proof.use` on `Request.requested`. Validation checks the two proof branches,
admission only after both succeed, the later use, and the explicit fixedpoint
exclusion. An initial use of `predicate-induction.run.close` for the proof was
rejected correctly by `fine-rain-validate` as an event after a terminal run
close; changing the proof's scope operation fixed the trace rather than weakening
the validator.

Commands and results before the final clean commit build:

```
cmake --build build/fine -j2
build/fine/fine run fine/fixtures/predicate-reusable-proof.fine
# verified-proof: step_distinct
# verified-predicate-induction: use_step_distinct
build/fine/fine run fine/fixtures/predicate-reusable-proof-false.fine
# refuted-proof: step_equal; failed-constructor: root; exit 1
build/fine/fine rain fine/fixtures/predicate-reusable-proof.fine \
  > /tmp/pred-proof.rain
python3 fine/rainfall_validate.py \
  fine/fixtures/predicate-reusable-proof.fine /tmp/pred-proof.rain
# valid rainfall: events=169, source_nodes=44, terms=35, source_term_edges=31
nix flake check
# all checks passed
nix build --no-link --print-out-paths
# /nix/store/y3yndvm856wbvfdr7i3n4b00lzd4lrhm-fine-0.0.1
```

This closes theorem admission from compiler-owned predicate branches. It does
not close the unequal-domain opening theorem. The scratch `marked_rename` proof
now gets past the former syntax restriction but remains in its abstraction
branch beyond ten seconds without the needed equations relating renaming,
opening, support, and the predicate. The next slice can finally state and reuse
those predicate-respecting transport facts without putting them into Spacer.

## 2026-09-02 — alpha-local one-layer predicate construction

The first predicate-respecting renaming probe after `a42ff4627` exposed a
same-manager binder capture in Fine's own one-layer constructor expansion. The
probe defined a recursive native `Tm`, a structural `rename`, and the ordinary
constructor-generated predicate:

```
Named(name, term)
here(name): Named(name, free(name))
application(name, fn, argument):
  Named(name, fn) && Named(name, argument)
  -> Named(name, app(fn, argument))
abstraction(name, body): Named(name, body) -> Named(name, abs(body))
```

It then asked for the derivation-induction theorem

```
Named(name, term) -> Named(other, rename(term, name, other))
```

The `here` branch closed, but `application` remained past twenty seconds. A
three-second Rainfall prefix isolated the exact generated construction formula.
`one_layer` had built each alternative directly with the constructor table's
retained parameter constants and then called `exists` on those same constants.
When the consumer branch and the goal alternative were both
`Named.application`, the current branch's `name`, `fn`, and `argument` were the
same Z3 ASTs as the schema constants being bound. Z3 correctly abstracted every
occurrence, including those already present inside the goal atom. The purported
fixed source term

```
rename(app(Fine.predicate.Named.application.arg1,
           Fine.predicate.Named.application.arg2),
       Fine.predicate.Named.application.arg0,
       other)
```

therefore became a function of the alternative's existential variables. This
was not alpha-equivalent construction; it changed which object the branch was
proving. Earlier preservation tests avoided the collision accidentally because
the induction predicate was `Step` while the constructed/inverted predicate was
`Marked`.

Every one-layer alternative is now localized before any binder is made. Fine
creates a fresh same-sort constant for every retained constructor parameter and
arbitrary-field binder, substitutes the full vector through result indices,
ordinary premises, arbitrary requirements, recursive premises, and declared
witnesses, and only then existentially closes the constructor parameters and
universally closes an arbitrary field. The consumer atom is never included in
that substitution. Fresh names include the declaration, one-layer invocation,
constructor, and ordinal; identity does not rely on text after construction.
Rainfall records one `predicate-induction.one-layer.parameter` event per schema
parameter with the strong schema and local handles. Arbitrary-field events now
retain both `schema_binder_term` and the local `binder_term`.

The promoted `predicate-renaming-proof.fine` now verifies all three `Named`
branches and admits `named_rename`. A later nonrecursive `Request` induction uses
the theorem; deleting it makes `Request.requested` refute. The false control
changes the predicate's name index without renaming the term and fails at
`Named.here`, demonstrating that the repaired construction does not simply
admit same-predicate goals. Rainfall's `application` construction contains the
unchanged live branch `app(arg1,arg2)` outside the existential binders, while
its candidate fields have distinct `.one-layer.<n>.application.parameter<i>`
identities. All three schema/local pairs are unequal.

The clean predecessor artifact is an executable discriminator:

```
timeout 2 \
  /nix/store/2ac8q16x64l7jb53hcmldq2qjvplzsg0-fine-0.0.1/bin/fine run \
  fine/fixtures/predicate-renaming-proof.fine
# exit 124, no output

build/fine/fine run fine/fixtures/predicate-renaming-proof.fine
# verified-proof: named_rename
# verified-predicate-induction: use_named_rename
```

This probe also found a trace-lifecycle omission. A verified `proof.run.close`
is nonterminal because later declarations may use it, but a refuted proof stops
the document and its close is terminal. `rainfall_replay.py` previously treated
no proof close as terminal, so a complete refuted-proof trace failed with
`replay has no terminal run close`. It now accepts `proof.run.close` as terminal
exactly when its status is not `verified`. The false predicate-renaming proof's
211-event trace validates and remains a process-level failure.

Existing Horn preservation, non-Horn arbitrary preservation, total-field
preservation, and reusable predicate-proof fixtures still verify after
alpha-localization. Their Rainfall traces validate. The full dirty package build,
including every install check, produced:

```
nix flake check
# all checks passed
nix build --no-link --print-out-paths
# /nix/store/2sdzh58ina8fn66zqpkwp5ab1jrrmpbb-fine-0.0.1
```

The original cofinite transport edge is now sharper, not closed. The scratch
`Marked(term)` renaming proof no longer disappears in `app_case`; it closes the
three ordinary constructors and returns `sat` for `abs_case`. Its total IH says
that every fresh opening remains `Marked` after an arbitrary rename. Its goal
requires a total field over openings of the renamed body. Connecting those
terms needs the standard fresh intermediary, a commute theorem for rename with
open away from the renamed names, and predicate transport between two fresh
openings. The fixed compiler now exposes that missing argument rather than
capturing the terms that state it.

After adding the terminal refuted-proof replay control to the install check, the
complete dirty tree was rebuilt again:

```
nix flake check
# all checks passed
nix build --no-link --print-out-paths
# /nix/store/wnsxa8h5iivxq6dwbkj2vv6z0zz2vacn-fine-0.0.1
```

## 2026-09-02 — unequal freshness transport and the environment-renaming null

The full `Marked(term) -> Marked(rename(term, source, target))` scratch proof
from the alpha-localization slice still failed only at its cofinite `abs_case`.
I first made the standard fresh-intermediary argument executable rather than
adding another special binder primitive. The scratch `/tmp/marked-env.fine`
introduced a recursive finite `Renaming` environment, `rename_with`, and the
structural theorem

```
rename_with(open_at(term, depth, fresh), put(fresh, other, environment))
  == open_at(rename_with(term, environment), depth, other)
```

under the real `fresh >= support_cutoff(term) + 1` premise. The theorem itself
verified by Fine's direct-field datatype induction. The intended cofinite branch
would instantiate its total IH once with the extended environment, thereby
performing both the outer renaming and the fresh-name change without an
unbounded sequence of unary rename theorems.

That formulation is retained as a null, not promoted. Adding the universally
closed theorem to the later predicate-induction query made even `Marked`'s
`bound_case` exceed 60 seconds. A diagnostic two-second solver timeout confirmed
that the no-helper file reached only `abs_case`, while the helper theorem made
`bound_case` itself return `unknown: timeout`. I also spiked an explicit
single/multi-pattern surface locally. The strongest tested multi-pattern paired
`open_at(rename_with(term, put(source,target,rest)), depth, other)` with
`open_at(term, depth, fresh)` and covered every quantified parameter exactly.
It remained pathological in the branch solver. All parser/runtime changes for
that spike were reverted. This is the same concrete failure mode as the earlier
opening-equivariance loop: a valid universal recursive-function equality is not
yet a usable branch resource merely because it has been proved and admitted.
The next full locally nameless attempt needs either a better bounded
instantiation boundary or a source formulation whose required ground terms are
already present; giving the search more fuel is not accepted.

I then isolated the actual semantic step from that matching problem in
`predicate-unequal-fresh-transport.fine`. It has three named parts:

1. `Transport.opened` supplies real finite derivations at any name for a raw
   one-hole body.
2. `Transport.close` owns a total recursive field only for names at or above
   `support_cutoff(body) + 2`, while its result is fixed at the disjoint name
   `support_cutoff(body) + 1` and boxes that opening.
3. `named_rename`, proved separately by predicate induction, transports the
   total IH's `Named(branch_name, open(body, branch_name))` evidence to the
   smaller result name before the `Named.boxed_case` construction closes.

The domains are syntactically unequal, so the earlier same-domain trick of
instantiating the IH at the target binder is impossible. The fixture verifies
both reusable proofs and a later nonrecursive `Request` consumer. Deleting
`named_rename` refutes exactly `Transport.close`; deleting `transport_named`
refutes `Request.requested`. The false proof asks for `Named(name + 1, term)` and
fails at `Transport.opened`. This is a bounded model of the evidence route, not
the full `Tm.open_at` theorem.

The 538-event Rainfall run validates. It retains the `close` field's schema
binder, declared `cutoff + 2` witness, requirement, recursive premise, exact IH,
and total universal IH separately. The goal construction visibly contains
`cutoff + 1`. `named_rename` is admitted before `transport_named`, used in the
exact `proof:transport_named/branch:close` scope, and neither theorem enters
fixedpoint. The install check asserts those term texts and edges as well as both
deletion controls and the false proof.

Direct development commands:

```
build/fine/fine run /tmp/predicate-unequal-fresh-transport.fine
# verified-proof: named_rename
# verified-proof: transport_named
# verified-predicate-induction: use_transport

build/fine/fine run /tmp/unequal-no-named.fine
# refuted-proof: transport_named
# failed-constructor: close

build/fine/fine run /tmp/predicate-unequal-fresh-transport-false.fine
# refuted-proof: transport_named
# failed-constructor: opened

build/fine/fine rain /tmp/predicate-unequal-fresh-transport.fine \
  > /tmp/unequal.rain
python3 fine/rainfall_validate.py \
  /tmp/predicate-unequal-fresh-transport.fine /tmp/unequal.rain
# valid rainfall: events=538, source_nodes=125, terms=122, source_term_edges=95
```

The first Nix install-check build failed only in the new Rainfall assertion: it
counted both the availability-phase and final branch-phase `named_rename` uses,
while the assertion expected only the latter. The trace was correct. The test
now filters out events carrying a `phase` field and requires the one final use.
After that test correction:

```
nix build --no-link --print-out-paths
# /nix/store/21rs99d2hrdly7f32lqpy99kri4v0i40-fine-0.0.1
nix flake check
# all checks passed
```

## 2026-09-02 — proof-term branch cut and virtual identity coeffects

h changed the destination explicitly: Fine should synthesize source proof terms,
not merely use Bool-valued predicates as erased evidence. Identity-shaped proof
terms are absorbed into the proof context by default, and functions declare a
coeffect which asks Fine to find the required proof in the caller's lexical
scope. Proof types are virtual and must have no runtime term. Retrofitting that
shape onto the expression-first parser/runtime would preserve the wrong center,
so the implementation was cut rather than adapted.

The last runnable predicate implementation, commit `1d7222a23`, was preserved
and pushed as annotated tag `pre-pat-1d7222a23`. A public
`fine/proof-terms` branch was created from that commit. The cut removes 10,279
lines while adding 1,409 before this log entry. It deletes the linked
bisimulation, predicate, and synthesis runtimes, their shared runtime-internal
header, the embedded demo, and all 44 former executable fixtures. The generic
Z3 fork/observers, source snapshots, exact generated-term lift, Rainfall
recorder/validator/projector/generation/host modules, build ancestry, and
research documents remain. `fine/PROOF_TERMS.md` records the survival and
quarantine boundary. The old code remains inspectable and runnable at the tag;
it is not copied into a `legacy` directory on this branch.

The new parser has distinct `ValueType` and `ProofType` AST nodes. The new
elaborator has distinct `ValueTerm` and `ProofEvidence` C++ structures rather
than a tagged union. `ValueTerm` is closed over `Int` and `Bool` in this slice.
`ProofEvidence` owns an `IdentityType`, formation label, and source span but no
runtime payload. There is no proof variant which an erasure pass could forget
to remove. An attempted value lookup which resolves to the proof namespace is a
static error. Rainfall opens every run with `proof.erasure.boundary`, declaring
the two runtime value kinds and zero proof variants, and closes with
`runtime_proof_values: 0`.

The first identity surface is `Id(A, left, right)` with `refl(value)`.
Reflexivity is accepted only if both endpoints reify to the exact same
manager-local AST as the supplied value. A proof declaration retains its source
node and formation, then automatically projects only `left == right` into the
lexical Z3 context. The projection is a normal strong Rainfall term with the
existing `fine.generated-term.v1` exact lift/reparse/reify validation; the proof
source is not falsely described as a Z3 proof term.

Functions now have value parameters, a value result, optional identity
`needs`, optional Bool `ensures`, and a value body. Declaration checking creates
symbolic value parameters, introduces every `needs` binder as hypothetical
static evidence, absorbs its proposition, and asks Z3 to refute the negated
guarantee. In the admitted fixture:

```
function replace(left: Int, right: Int) -> Int
  needs [same: Id(Int, left, right)]
  ensures { result == right; }
{
  left
}
```

`left == right` from the coeffect is necessary. The control with no `needs`
clause is satisfiable and rejected with `does not satisfy its guarantees under
declared coeffects`.

At a call, Fine substitutes the value arguments into each declared identity
demand. The first proof-search grammar has exactly one rule: select the first
lexically introduced caller proof whose carrier and both endpoints have exact
AST identity with the instantiated demand. There is no global instance table,
approximate solver entailment, or constructor search. The selected evidence is
installed virtually under the callee's coeffect name and only its proposition
is absorbed. Rainfall keeps `coeffect.demand.declare`,
`coeffect.demand.instantiate`, `coeffect.resolve`, `proof.context.absorb`, and
`coeffect.use` separate; `coeffect.use` explicitly records that no runtime
argument was created.

`fine materialize` turns an implicit call such as `replace(x, y)` into the exact
source `replace(x, y) using [same = p]`. It applies byte-offset insertions,
reparses the resulting document, and executes it with implicit coeffect
resolution forbidden before emitting the source. The checked-in explicit
fixture is byte-identical to this output. Re-running it reports `(explicit)`;
the unmaterialized source reports `(lexical search)`.

Discriminating fixtures and observed outputs:

```
build/proof-core/fine run fine/fixtures/identity-coeffect.fine
# verified function: replace
# formed proof: p : Id(Int, x, y) (virtual)
# resolved coeffect: replace.same <- p (lexical search)
# verified assertion: identity_coeffect.0
# runtime-proof-values: 0 (unrepresentable)

build/proof-core/fine materialize fine/fixtures/identity-coeffect.fine \
  > /tmp/fine-materialized.fine
diff -u fine/fixtures/identity-coeffect-materialized.fine \
  /tmp/fine-materialized.fine
# no diff

build/proof-core/fine run fine/fixtures/reject-missing-coeffect.fine
# failure: missing caller proof for coeffect `replace.same ...`

build/proof-core/fine run fine/fixtures/reject-proof-as-value.fine
# failure: proof `p` cannot inhabit a runtime value

build/proof-core/fine run fine/fixtures/reject-unjustified-function.fine
# failure: function `replace` does not satisfy its guarantees under declared coeffects
```

The positive Rainfall trace has 31 events, seven source nodes, three exact Z3
terms, and four source-term edges. The validator was extended to recognize only
the new `proof-core.run.close` as an additional terminal operation. Projection
still admits the current snapshot and its source annotations. The Nix install
check asserts the positive run, exact materialized bytes, explicit rerun, all
three negative controls, required event separation, zero runtime proof variants,
term declaration/validation parity, and current projection.

Build record:

```
cmake -S . -B build/proof-core -G Ninja \
  -DZ3_BUILD_LIBZ3_SHARED=OFF \
  -DZ3_BUILD_EXECUTABLE=OFF \
  -DZ3_BUILD_TEST_EXECUTABLES=OFF \
  -DFINE_BUILD_EXECUTABLE=ON
cmake --build build/proof-core --target fine-bin -j16
# success

nix flake check --print-build-logs
# all checks passed (flake output evaluation)

nix build --no-link --print-out-paths
# /nix/store/ymfhr95bzpsiji2vrpybscf09ifbq9k1-fine-0.1.0
```

The next justified consumer is a typed identity proof hole. Its initial grammar
should contain exact lexical evidence, `refl`, and later named identity proof
applications; ill-typed candidates must be absent before enumeration. It must
materialize one actual proof term and rerun with no proof search before
inductive propositions or old predicates are reconsidered.

Post-commit verification of `8573e8718` used the clean Git source rather than
the earlier dirty-tree derivation:

```
nix flake check --print-build-logs
# all checks passed
nix build --no-link --print-out-paths
# /nix/store/67091p1ghr4qjs6hf5fp6blaq8vxvhjw-fine-0.1.0
```

## 2026-09-02: proof-term roadmap from the branch-cut discussion

Added `fine/ROADMAP.md` to preserve the design sequence that produced the
proof-term branch rather than leaving it scattered across chat. The decisive
failure was representational: the former `Step(a, b) : Bool` could be submitted
to Z3 and used by compiler-owned induction machinery, but it supplied no Fine
inhabitant that could be synthesized, inspected, or materialized. Experiments on
the preserved branch had already shown that Spacer summaries project away
constructor support, so reading them back as derivations would be dishonest.

The roadmap fixes four boundaries before ordering features: disjoint value and
proof levels; automatic proposition absorption while retaining source evidence;
caller-local proof coeffects; and a Fine-owned typed proof grammar with Z3 used
to reject candidates and discharge obligations rather than manufacture source
proofs. It also records the chosen proof-irrelevant cutoff: no equality between
proofs, no runtime observation of evidence, and no proof-to-value elimination,
while distinct evidence may retain distinct source provenance in Rainfall.

The ordered vertical slices are: the already closed identity-coeffect boundary;
typed identity holes; named proof functions and composition; proof-only
elimination; indexed inductive propositions formed as derivations; locally
nameless STLC with total cofinite proof fields and explicit opening transport;
cancellable search/frontier/materialization editor transactions; and selective
recovery of value-language consumers. Each slice has a positive fixture,
rejecting control, Rainfall ownership requirement, materialization requirement,
and verification-only rerun as its exit condition. Explicit non-goals prevent
the old predicate system, a Z3 term universe, global theorem search, or solver
proof reconstruction from returning by architectural drift.

## 2026-09-02: typed identity proof holes (`a8774885d`)

Closed the first proof-synthesis slice with a source-level `?` in proof
declarations. The parser represents it as `ProofExpr::Kind::hole`; it is not a
value-expression case and therefore cannot enter `ValueTerm`. The expected
`Id(A, left, right)` is elaborated before search. Fine then constructs the
complete finite grammar in this order:

1. caller/run-local proof bindings whose carrier and both endpoints have exact
   same-manager AST identity with the expected identity type;
2. `refl(left)` only when the elaborated left and right endpoints themselves
   have exact AST identity.

Mismatched local proofs are filtered before a candidate event exists. Z3 does
not enumerate, invent, or type the proof syntax in this slice. The first typed
candidate is selected deterministically; later typed candidates remain an
explicit residual frontier. An empty grammar fails at the hole with
`no well-typed candidate in grammar [exact-local, refl]`.

Materializations changed from insertion offsets to ordered replacement ranges.
This lets the same checked edit list replace an exact `?` span and insert a
`using [...]` coeffect argument. The second pass reparses the edited source with
both `require_materialized_proofs` and `require_explicit_coeffects`; any surviving
hole or implicit caller proof makes `fine materialize` fail before emitting
source.

Rainfall now records four source-proof operations distinct from ordinary solver
traffic: `proof.search.open`, `proof.search.candidate`,
`proof.search.select`, and `proof.search.close`. Open records the expected proof
type, proposition term, exact source-hole node, finite grammar, and the fact
that ill-typed candidates were not enumerated. Each candidate carries Fine
source text, its production, and exact-typing status. Selection refers to the
candidate event ID. Close refers to the selection and requires the selected
candidate followed by its residual IDs to equal the entire enumerated frontier.
`rainfall_replay.py` now enforces those references, validates that the source
span is exactly `?`, rejects a local candidate which loses its proof binding,
and requires every opened proof hole to close before the terminal event.

The discriminating fixture `fine/fixtures/identity-holes.fine` first introduces
`other : Id(Int, y, y)` with `y = 8`. The hole
`self : Id(Int, x, x)` sees only `refl(x)`; `other` is absent rather than emitted
and rejected. After `self` is formed, the `copied` hole sees `self` followed by
`refl(x)`, selects `self`, and retains `refl(x)` as its one residual candidate.
The same run resolves `hold.same` to `self`, so materialization exercises proof
replacement and coeffect insertion together. The checked-in materialized file
is byte-identical to the command output and its ordinary rerun contains no
`filled proof hole` line. `reject-empty-proof-hole.fine` uses distinct exact
values `7` and `8`, supplies no matching proof, and fails at its `?`.

Observed local and replay checks:

```
cmake --build build/proof-core --target fine-bin -j16
# success

build/proof-core/fine run fine/fixtures/identity-holes.fine
# filled proof hole: self <- refl(x) (typed search)
# filled proof hole: copied <- self (typed search)
# resolved coeffect: hold.same <- self (lexical search)
# runtime-proof-values: 0 (unrepresentable)

build/proof-core/fine materialize fine/fixtures/identity-holes.fine \
  > /tmp/identity-holes-materialized.fine
diff -u fine/fixtures/identity-holes-materialized.fine \
  /tmp/identity-holes-materialized.fine
# no diff

build/proof-core/fine rain fine/fixtures/identity-holes.fine \
  > /tmp/identity-holes.jsonl
python fine/rainfall_validate.py fine/fixtures/identity-holes.fine \
  /tmp/identity-holes.jsonl
# valid rainfall: events=52, source_nodes=11, terms=5,
# source_term_edges=4, proof_holes=2
```

The flake install check asserts exact output, byte materialization, the no-search
rerun, the empty-grammar control, candidate order `refl(x), self, refl(x)`,
absence of `other`, explicit residual size, formation provenance, terminal hole
count, and zero runtime proof values. Clean-source verification for
`a8774885d`:

```
nix flake check --print-build-logs
# all checks passed
nix build --no-link --print-out-paths
# /nix/store/ks67imra3ljrb4wak2ys0vxp0iri743g-fine-0.1.0
```

The next justified consumer is a named proof-level function. Identity symmetry
must be the first fixture because neither exact-local selection nor `refl` can
inhabit its reversed endpoint type; only a typed source application can close
the hole. Recursive application grammars need a finite bound before transitivity
or any larger proof search is admitted.

## 2026-09-02 — Z3 proof-term synthesis boundary

h asked whether Z3 can synthesize proof terms. The question was split into
native unsatisfiability proofs, ground Fine-source inhabitants, and universally
correct recursive proof functions; treating all three as one facility would
have hidden the ownership boundary the proof-term branch was created to enforce.

Local source inspection found `solver::proof()` and the complete low-level
`Z3_OP_PR_*` operator family, recursive datatypes/functions, model extraction,
fixedpoint answers, and QSAT. A full scan found no implementation of
`synth-fun`, `synth_fun`, `SyGuS`, or `sygus` in `src`, `examples`, or `doc`.
The official Z3 proof-log guide describes proof terms as low-level inference
rules and newer logs as big-step hints, some of which need an SMT call to
validate. The official datatype guide explicitly says ground ADT procedures do
not lift to induction and Z3 has no method for producing induction proofs. The
Reynolds et al. CEGQI synthesis paper contributes the useful ADT-grammar and
evaluation-axiom pattern, but its synthesis implementation was in CVC4.

Added the opt-in reproducible probe at
`fine/spikes/proof-term-synthesis`. It builds against the exact local Z3 static
library and exercises four discriminators:

1. Native proof mode returns
   `(unit-resolution (asserted a) (asserted (not a)) false)` for a Boolean
   contradiction.
2. Symmetry over an uninterpreted carrier contains the operator set
   `asserted mp rewrite symm unit-resolution`. The same formula over `Int`
   instead contains
   `asserted monotonicity mp not-or-elim rewrite trans unit-resolution`.
   Therefore mining a native `symm` happens to work in one theory path but is
   not a stable source proof representation.
3. A Fine-owned recursive `SrcProof` datatype with exact endpoint functions and
   a cost bound returns the ground model values
   `apply-symm local-p` and
   `apply-symm (apply-trans local-12 local-23)` for the two requested holes.
   This is real bounded ground proof-term synthesis because the model contains
   an inspectable constructor tree, while Fine still owns its grammar, typing,
   scoping, and recheck.
4. An unknown `synth-symm : SrcProof -> SrcProof` constrained to reverse every
   well-formed recursive proof returns `unknown` / `timeout` at five seconds.
   The specialized QSAT tactic also returns `unknown`, immediately, because the
   formula contains the uninterpreted candidate function. These do not prove
   impossibility, but they prevent the ground result from being mislabeled as
   induction or general proof-function synthesis.

The first compile attempt tried to classify proof nodes with a nonexistent
public `Z3_PROOF_SORT` enum and failed exactly at that identifier. The public
sort-kind API deliberately has no proof member; the corrected probe recognizes
the internal sort name `Proof` before collecting proof-declaration names.

Commands and observed result:

```sh
fine/spikes/proof-term-synthesis/run.sh
# native Boolean and both symmetry queries: unsat
# ground-symmetry-hole: sat, apply-symm local-p
# ground-composition-hole: sat, apply-symm (apply-trans local-12 local-23)
# universal-symmetry-scheme: unknown, timeout
# qsat-universal-symmetry-scheme: unknown, formula contains uninterpreted functions

rg -n 'synth-fun|synth_fun|SyGuS|sygus' src examples doc
# no matches
```

The closed design decision is narrower than adding a new backend now. Named
proof functions and deterministic typed application enumeration remain next.
After symmetry passes that route, Fine may test Z3 datatype-valued model search
behind the same exact grammar and then lift, reparse, and recheck its selection.
Rainfall would retain grammar, bound, model choice, and final Fine check as
separate facts. Native Z3 proofs remain solver evidence only; Spacer invariants
remain excluded because prior fixtures showed they project away constructor
support. Full analysis and primary-source links are in
`fine/research/z3-proof-term-synthesis.md`.

## 2026-09-02 — named proof functions and bounded symmetry search

Closed the symmetry half of roadmap slice 2 without adding proof values to the
runtime or allowing arbitrary solver entailment to masquerade as a proof term.
The new declaration surface is:

```fine
proof function symm(left: Bool, right: Bool)
  needs [given: Id(Bool, left, right)]
  -> Id(Bool, right, left);
```

Value parameters are static indices. `needs` parameters and the result are
virtual identity evidence. Declaration checking gives each index a symbolic Z3
constant, elaborates and absorbs each proof parameter, then refutes the negated
result proposition. A false zero-premise `lie(left,right) -> Id(left,right)` is
rejected at the declaration. Proof functions occupy a separate namespace and a
runtime value expression which calls one fails before Z3.

Explicit proof application separates indices from evidence as
`symm[left, right](given)`. Its elaborator checks the index arity and kinds,
instantiates the result to the exact expected manager-local identity type, then
recursively checks every proof argument against its instantiated parameter. The
application produces only `ProofEvidence`; Rainfall records
`proof.function.apply` with `runtime_call_created: false`.

Hole search now has the ordered grammar `exact-local`, `refl`, then named proof
applications in declaration order, bounded by total tree cost three. Search
works backward from the expected result. In this slice a function is searchable
only when every static index can be bound from a simple parameter occurrence in
its result; after binding, the complete instantiated result is checked by exact
AST identity. This is deliberately insufficient for transitivity, whose middle
index occurs only in its premises. Each instantiated proof parameter is filled
recursively within the remaining budget, Cartesian argument combinations are
filtered by total cost, and the full finite application source is retained.

`identity-symmetry.fine` needs a genuinely non-definitional identity so that the
reversed goal cannot collapse to `refl`. Its separately checked `bool_eta`
function establishes `value = (value == true)`. The run explicitly forms
`p : Id(Bool, x, x == true)`, then asks for
`Id(Bool, x == true, x)`. Search produces exactly two cost-two terms in order:

```text
symm[x, x == true](p)
symm[x, x == true](bool_eta[x]())
```

The first is selected and the second remains in the residual frontier. The
materialized fixture is byte-identical and its rerun has no hole search.
`reject-cyclic-proof-search.fine` admits an identity-preserving `loop` theorem
but provides no base evidence for distinct endpoints; recursion reaches the
cost bound and returns the ordinary empty-grammar error. Additional controls
reject an unjustified proof function and any proof-function call in runtime
value code.

Rainfall now gives proof-function declarations, explicit applications, bounded
hole opening, application candidates, selection, and closure separate events.
Every application candidate includes its function, child proof sources, and
cost. Replay validation requires declarations before applications, checks all
declaration source and proposition references, verifies application candidates
retain their source trees, and still proves that the selected candidate plus
the residual frontier exhausts the deterministic enumeration.

Observed checks:

```sh
cmake --build build/proof-core --target fine-bin -j16
# success

build/proof-core/fine run fine/fixtures/identity-symmetry.fine
# verified proof function: bool_eta
# verified proof function: symm
# filled proof hole: reversed <- symm[x, x == true](p) (typed search)
# runtime-proof-values: 0 (unrepresentable)

build/proof-core/fine materialize fine/fixtures/identity-symmetry.fine \
  > /tmp/identity-symmetry.fine
diff -u fine/fixtures/identity-symmetry-materialized.fine \
  /tmp/identity-symmetry.fine
# no diff

build/proof-core/fine rain fine/fixtures/identity-symmetry.fine \
  > /tmp/identity-symmetry.rain
python fine/rainfall_validate.py fine/fixtures/identity-symmetry.fine \
  /tmp/identity-symmetry.rain
# valid rainfall: events=38, source_nodes=8, terms=6,
# source_term_edges=1, proof_holes=1

nix flake check --print-build-logs
# all checks passed
nix build --no-link --print-out-paths
# /nix/store/rrfq7yh97wszvli6ld0imj8jm0m00z62-fine-0.1.0
```

One environment failure was retained: after Nix auto-GC, the cached development
shell named `clang-format` at a removed store path and the first format/build
command failed with `clang-format: command not found`. Running
`codex-flake-refresh --force` restored the declared Clang tool and the build
then passed.

The remaining slice-2 edge is transitivity. Backward search must infer its
middle index jointly from two child proof types and retain both child trees as
distinct Rainfall inputs. Only after that deterministic grammar is closed will
the datatype-valued Z3 model selector be connected behind the same grammar.

Post-commit clean-source verification for `7f02e214d` reran every install check:

```sh
nix build --no-link --print-out-paths
# /nix/store/rxlz6jfrwvsby9gj6fnamgdz2ww5hmsm-fine-0.1.0
```

## 2026-09-02 — transitivity infers a result-absent middle index

Closed the second half of roadmap slice 2. The prior enumerator required every
static index of a proof function to occur as a direct parameter in its result.
That admitted symmetry but correctly made transitivity unavailable: for

```fine
proof function trans(left: Bool, middle: Bool, right: Bool)
  needs [first: Id(Bool, left, middle), second: Id(Bool, middle, right)]
  -> Id(Bool, left, right);
```

`left` and `right` came from the requested result while `middle` remained
unbound. The clean pre-change artifact reproduced the intended failure on the
new positive fixture:

```sh
/nix/store/rxlz6jfrwvsby9gj6fnamgdz2ww5hmsm-fine-0.1.0/bin/fine \
  run fine/fixtures/identity-transitivity.fine
# proof hole `composed` has no well-typed candidate ...
# exit 1
```

The deterministic enumerator now treats result matching as the initial partial
index substitution. If indices remain absent, it matches proof-parameter
identity patterns against exact lexical `ProofEvidence` types. A direct index
parameter may be bound only to the target's existing manager-local AST; a
previously bound parameter must have exact AST identity. Compound endpoint
patterns are checked only when their referenced indices are available. Partial
substitutions are memoized by ordered index name and Z3 AST ID. Once every index
is bound, Fine re-elaborates the complete result and demands exact type identity
before recursively enumerating both instantiated proof arguments. This is
finite unification over lexical evidence, not semantic equality search and not
mining a Z3 proof.

`ProofEvidence` now retains the source spelling of its two endpoints alongside
the manager-local terms. This lets the inferred middle index materialize as the
source expression which supplied it while the equality test remains AST-based.
An application candidate separately retains ordered `index_arguments` and
ordered `proof_arguments`; both generated and explicitly reapplied proof
functions emit those arrays in Rainfall, and replay rejects missing or malformed
arrays.

The first fixture draft was discarded as insufficiently discriminating. A
simple `x -> y -> z` chain built from the zero-premise `bool_eta` function
produced four valid cost-three transitivity trees: both local children and each
of their cost-one reconstructions. It demonstrated enumeration but could not
prove that both stored children were necessary. The retained fixture instead
makes each nonlocal reconstruction pass through `symm`, hence cost two. It binds

```text
p : Id(Bool, left, middle)
q : Id(Bool, middle, right)
```

as locals and asks for `Id(Bool, left, right)`. Under total cost three the only
candidate is exactly:

```fine
trans[left, middle, right](p, q)
```

Rainfall records indices `[left, middle, right]`, children `[p, q]`, and cost 3
as distinct data. The unrelated `wrong : Id(Bool, left, left)` never enters the
tree. `reject-transitivity-gap.fine` removes `q` but leaves its `base_right`:
reconstructing the second child as `symm[right, middle](base_right)` costs two,
so the transitivity tree costs four and the same bounded grammar is empty. This
is the control against accepting marginal support from only the first premise.

Observed checks:

```sh
cmake --build .build -j2
# success

.build/fine run fine/fixtures/identity-transitivity.fine
# verified proof function: trans
# filled proof hole: composed <- trans[left, middle, right](p, q) (typed search)
# runtime-proof-values: 0 (unrepresentable)

.build/fine materialize fine/fixtures/identity-transitivity.fine |
  diff -u fine/fixtures/identity-transitivity-materialized.fine -
# no diff

.build/fine run fine/fixtures/reject-transitivity-gap.fine
# proof hole `impossible` has no well-typed candidate ...
# exit 1

.build/fine rain fine/fixtures/identity-transitivity.fine \
  > /tmp/trans-rain.jsonl
python fine/rainfall_validate.py fine/fixtures/identity-transitivity.fine \
  /tmp/trans-rain.jsonl
# valid rainfall: events=82, source_nodes=21, terms=14,
# source_term_edges=3, proof_holes=1

nix flake check
# all checks passed
nix build --no-link --print-out-paths
# /nix/store/pva2mzhax3vbl2b4bzvz95n6918csr6s-fine-0.1.0
```

The bounded Z3 datatype-model selector is now the remaining immediate edge. It
must consume the same exact grammar and reproduce this two-child tree, then lift,
reparse, and recheck it. The deterministic enumerator remains the reference;
this slice does not yet infer an absent index whose only witness is itself a
nested synthesized proof rather than lexical evidence.

Post-commit clean-source verification for implementation commit `fcd285def`
reran the full install checks, including the positive materialized transitivity
fixture, the missing-child rejection, Rainfall replay, and every earlier proof
core control:

```sh
nix flake check
# all checks passed
nix build --no-link --print-out-paths
# /nix/store/1v9ivq1vp4f5891l0ig9rh4b8dzfjvfi-fine-0.1.0
```

## 2026-09-02 — bounded Z3 datatype-model proof selector

Closed the model-selector follow-up to deterministic proof-function composition.
The default enumerator remains the reference and still constructs the complete
bounded typed frontier. The new `--proof-selector z3` path compacts the ground
productions appearing in those candidate trees into a recursive datatype rather
than presenting Z3 with one nullary constructor per complete answer. Local proofs
and reflexivity are leaves. A ground named proof-function application is one
constructor with a recursive field for every proof argument.

The implementation is isolated in `src/fine/proof_model_selector.{h,cpp}` rather
than adding another solver subsystem to the already large `runtime.cpp`. Its
grammar records a carrier token and the existing manager-local Z3 AST IDs of
each identity endpoint. It defines recursive functions over the generated
proof datatype:

```text
carrier : Proof -> Int
src     : Proof -> Int
dst     : Proof -> Int
cost    : Proof -> Int
well    : Proof -> Bool
```

Each application case fixes its result type and requires every recursive child
to be `well` with the exact expected carrier and endpoint IDs. Cost is one plus
the costs of all children. The model query fixes the requested carrier, source,
destination, and maximum cost three. Thus Z3 chooses constructor structure but
cannot invent a Fine term, an index, a local source owner, or an endpoint.

`ProofCandidate` now retains its exact `IdentityType` and child candidate trees
in addition to the already visible source, indices, child source strings, and
cost. Runtime compaction deduplicates ground application productions by function,
source indices, result type, and ordered argument types. The model is lifted by
matching datatype constructor declaration identity, recursively lifting its
fields, and rendering the corresponding Fine application. Printed model text is
recorded for inspection but is never parsed to decide the source tree. The lifted
source and evaluated model cost must equal one candidate in the complete
deterministic frontier or the run fails.

The transitivity grammar compacts to exactly:

```text
apply:trans[left, middle, right]/2
local:p
local:q
```

Z3 returns:

```text
(apply-trans local-p local-q)
```

and structural lifting returns:

```fine
trans[left, middle, right](p, q)
```

Ten repeated runs selected that same unique cost-three tree. The selector also
handled the two-hole identity fixture, creating separate datatype names in one
solver context; it chose `refl(x)` for the first hole and exact local `self` for
the second. Symmetry selected the existing first source tree rather than its
nested residual alternative, but the correctness rule is only that any model
selection must occur in the exact reference frontier. Residual ordering is now
defined as original deterministic order with the selected event removed, so a
valid non-first model selection cannot falsify replay bookkeeping.

The CLI additions are:

```sh
fine run --proof-selector z3 FILE
fine rain --proof-selector z3 FILE
fine materialize --proof-selector z3 FILE
fine rain --proof-selector z3 --document ID --revision N --generation ID FILE
```

The materialization command performs the required whole-document boundary: run
model selection, lift and replace the source hole, parse the resulting source,
and rerun with both proof and coeffect search forbidden before emitting it. The
transitivity result is byte-identical to the deterministic checked-in materialized
fixture.

Rainfall separates `proof.model.grammar`, `proof.model.solve`, and
`proof.model.lift`. The grammar event cites the exact deterministic candidate
events and compact production list; solve cites the grammar and retains model
value and cost; lift cites the solve and exact candidate. Ordinary selection
must then name that lifted candidate. Replay rejects an incomplete grammar/solve/
lift chain, a model cost outside the declared bound, a lifted body outside the
reference frontier, or a later selection which silently changes the model's
choice.

One correction happened during review: the first datatype interpreter tracked
only endpoint AST IDs. Those IDs are manager-global and therefore already
sort-discriminating, but the grammar's `carrier` field was otherwise unused. I
added the recursive carrier function and exact carrier constraints at the root
and every application child rather than relying on that incidental Z3 identity
property.

One test-script failure was retained. The new live-snapshot command and Rainfall
validation succeeded, but a follow-up Python assertion incorrectly read
`events[0]["data"]["identity"]`; the first event is the document declaration,
not the snapshot declaration, so it raised `KeyError: 'identity'`. Selecting the
`source.snapshot.declare` event verified document `doc:test`, revision 7, and the
exact source hash.

Observed checks before the implementation commit:

```sh
clang-format -i src/fine/main.cpp src/fine/runtime.cpp \
  src/fine/proof_model_selector.cpp src/fine/proof_model_selector.h
cmake --build .build -j2
# success

.build/fine run --proof-selector z3 \
  fine/fixtures/identity-transitivity.fine
# filled proof hole: composed <- trans[left, middle, right](p, q)
#   (Z3 datatype model)

.build/fine materialize --proof-selector z3 \
  fine/fixtures/identity-transitivity.fine |
  diff -u fine/fixtures/identity-transitivity-materialized.fine -
# no diff

.build/fine rain --proof-selector z3 \
  fine/fixtures/identity-transitivity.fine > /tmp/trans-z3-rain.jsonl
python fine/rainfall_validate.py fine/fixtures/identity-transitivity.fine \
  /tmp/trans-z3-rain.jsonl
# valid rainfall: events=85, source_nodes=21, terms=14,
# source_term_edges=3, proof_holes=1

nix flake check
# all checks passed
nix build --no-link --print-out-paths
# /nix/store/xhp9cya1dqgwf94jahw7xvlmkzrlzapd-fine-0.1.0
```

This closes the semantic integration boundary, not a scalability result. Fine
still enumerates all complete candidates before compaction, both to supply the
reference frontier and to prevent a second grammar from growing inside the
solver backend. A later direct grammar generator may remove that cost only if
its productions, source ownership, exact type checks, and frontier claims remain
observationally identical to the deterministic reference.

Post-commit clean-source verification for implementation commit `c96d53ffe`
reran the complete install checks, including deterministic and Z3-selected
transitivity, exact materialization, model Rainfall replay, and every earlier
proof-core control:

```sh
nix flake check
# all checks passed
nix build --no-link --print-out-paths
# /nix/store/q3lr6h9jp3k5xrf4cm5zqwfyz8b5i1m4-fine-0.1.0
```

## 2026-09-02 — browser-owned Fine and Rainfall playground

The first public playground slice deliberately retained the executable boundary
instead of adding a JavaScript interpretation of Fine. `fine.mjs` is the regular
C++ CLI plus the fork's own static `libz3`, compiled by Emscripten. Browser code
writes the edited document to MEMFS, calls `run --proof-selector z3`, then calls
`rain --proof-selector z3` on the exact same bytes. The page exposes only a
textarea, a run button, ordinary output, and a formatted Rainfall pane. There is
no server-side source execution.

The declarative build has two layers. `playground-wasm` filters browser assets
out of its source and builds the 11 MiB solver module; `playground` adds the
three static page assets and the checked-in transitivity sample. This prevents a
frontend edit from triggering another complete Z3 cross-compile. The
`playground-service` flake app serves the immutable result on localhost port
4174, and `fine-playground.service` is the persistent rc-managed unit source.
Port 4173 was already occupied by an unrelated process and was left untouched.

Three failed builds were retained because each exposed a distinct boundary bug:

1. The generic derivation ran Z3's legacy `./configure` before the explicit
   Emscripten CMake build. `dontConfigure = true` now makes the build phase sole
   owner of configuration.
2. CMake emits the executable at the build root, not `src/fine`; the first smoke
   and install paths incorrectly named the latter. The wasm derivation now copies
   `build-wasm/fine.mjs` and `build-wasm/fine.wasm`.
3. The first datatype-model call escaped as an opaque `WebAssembly.Exception`.
   Fine and all Z3 components were not being compiled under the same explicit
   native Wasm exception mode. Once aligned, the real error became visible:
   `fine: thread constructor failed: Not supported`. The model selector's
   five-second wall-clock timeout creates a timer thread. Under Emscripten it now
   uses a five-million-unit Z3 resource limit instead, retaining a bounded call
   without pretending browser pthreads exist.

The final Node sequence reused one initialized module for `run`, `rain`, and a
second `run`; it returned statuses `0, 0, 0`, with respectively 13, 85, and 13
stdout lines and no stderr. The package smoke requires
`proof.model.grammar`, `proof.model.solve`, `proof.model.lift`, and
`proof-core.run.close` in the emitted JSONL.

Commands at closure:

```sh
nix flake check --no-build
# all outputs evaluate, including playground-wasm, playground, and service app

nix build --no-link --print-out-paths .#playground
# /nix/store/qb8r7dgfrhdrys9xvfaah4mfp8fdhm13-fine-playground-0.1.0

node playground/smoke.mjs \
  /nix/store/6xizq6rxzj8820kbhxmwnlhw36a0lj97-fine-playground-wasm-0.1.0 \
  fine/fixtures/identity-transitivity.fine
# wasm smoke passed with 85 Rainfall events
```

A local service smoke returned `200 OK` for the page and served `fine.wasm`
with `Content-Type: application/wasm` and byte length 10,662,220.
The unchanged native semantics also passed the complete install-check suite in
`/nix/store/822xsgb3qdbl3widd4mna173v8ilmi9s-fine-0.1.0`.

Commit `630d12175` was pushed to `origin/fine/proof-terms`. A clean-source native
rebuild then passed at
`/nix/store/am6ijaxmsm9q8v9yyk93qz0f09lfzgzb-fine-0.1.0`; the clean playground
artifact remained
`/nix/store/qb8r7dgfrhdrys9xvfaah4mfp8fdhm13-fine-playground-0.1.0`.
`rc-service add ./fine-playground.service` installed and started the localhost
server, while `rc-publish add fine 4174` installed its gateway. Both appear in
`multi-user.target`'s dependency list under `/etc/systemd/system.control`.
The public smoke at `https://fine.shit.yachts/` returned HTTP/2 200, the expected
page title, and the same 10,662,220-byte WebAssembly module with
`application/wasm`.

## 2026-09-02 — CodeMirror without a second language parser

The plain textarea was replaced by a locally bundled CodeMirror 6 editor. The
frontend pins `codemirror` 6.0.2, `@codemirror/language` 6.11.3,
`@lezer/highlight` 1.2.1, and esbuild 0.25.11 in
`playground/package-lock.json`; the deployed page has no CDN dependency. The
root ignore rules now admit only the playground's package manifest and lock.

Fine highlighting is deliberately lexical. It distinguishes proof declarations
and `Id` from runtime declarations and `Int`/`Bool`, and separately marks
definition sites, coeffect/guarantee keywords, booleans, numbers, operators,
comments, `refl`, `result`, and typed holes. It does not invent an independent
browser parser whose disagreements with `fine::syntax::parse` would look
authoritative. The C++ parser remains the only source of diagnostics.

The static `playground` derivation is now a `buildNpmPackage` with fixed npm hash
`sha256-t9A5fFAlRTfdL8HvekInKcGARsVnKd7t/XvSydRtowo=`. Esbuild keeps
`fine.mjs` external, minifies the editor bundle to 386 KiB, and then the package
copies the unchanged solver module. The source-filtered `playground-wasm`
derivation did not rebuild.

Validation before commit:

```sh
nix build --no-link --print-out-paths .#playground
# /nix/store/r4irwr0rwizfw0iz9244zh7maj93wkaj-fine-playground-0.1.0
# package check: wasm smoke passed with 85 Rainfall events

ls -lh /nix/store/r4irwr0rwizfw0iz9244zh7maj93wkaj-fine-playground-0.1.0/app.js
# 386K
```

Commit `1e029e025` was pushed to `origin/fine/proof-terms`. Restarting
`fine-playground.service` realized the same clean artifact and switched the live
page in place. The public HTML now contains the CodeMirror host rather than a
textarea; `app.js` returns HTTP/2 200 as `text/javascript`, byte length 395,144.

## 2026-09-02 — Live syntax reference above the playground

The playground now opens with a compact three-column language reference before
the editor: each row names a current form, states the check Fine performs, and
gives the smallest useful spelling. It covers runtime bindings and functions,
identity evidence, needed and explicit evidence, typed proof holes, proof
functions, and executable assertions. The introductory boundary is explicit:
only `Int` and `Bool` inhabit runtime values, while `Id` evidence is virtual and
cannot be inspected by a running program.

The sheet deliberately documents only syntax accepted by the current proof-term
branch. It does not present roadmap items as available language features. The
table remains open by default, can be collapsed, and scrolls horizontally rather
than crushing code examples on narrow displays.

Validation before commit:

```sh
git diff --check
nix build --no-link --print-out-paths .#playground
# /nix/store/gfdsbipw2cygkqcvn3f6c9k6s31zwjh4-fine-playground-0.1.0
# package check: wasm smoke passed with 85 Rainfall events
```

## 2026-09-02 — Vite delivery and negotiated Zstandard Wasm

The Python simple server was removed from the persistent playground path after a
remote client observed roughly one megabyte of Wasm transfer in two minutes. The
frontend is now built by Vite 8.2.2 and served by its preview server. Fine's
Emscripten module remains an external root asset rather than being interpreted or
folded into the JavaScript bundle.

The build precompresses `fine.wasm` with Node's Zstandard implementation at level
19. A narrow Vite preview middleware serves that exact file only when the request
advertises `Accept-Encoding: zstd`; it sets `Content-Encoding: zstd`, preserves
the `application/wasm` media type, varies the response by encoding, and otherwise
serves the original module with the same explicit shared-cache policy. The latter
matters because the public gateway does its own content negotiation: it can cache
the origin bytes and emit Zstandard at the edge rather than pulling 11 MiB through
the tunnel for every visitor. The 10,662,220-byte Wasm becomes 2,555,916 bytes on
the direct negotiated path. A client without Zstandard support still receives the
original bytes.

`playground/serve-smoke.mjs` starts the same Vite command used by the service and
checks both paths byte-for-byte: the plain response must equal `fine.wasm`, while
the negotiated response must equal `fine.wasm.zst` and carry the encoding header.
This caught Vite's default attempt to bundle its config into a temporary directory
inside the read-only Nix store. The service now uses `--configLoader native`, so
the runtime writes nothing into its package. The first public smoke also caught
Vite's host-header protection rejecting the published hostname; the preview
allowlist now names `fine.shit.yachts` explicitly rather than disabling the
protection.

Validation before commit:

```sh
nix build --no-link --print-out-paths .#playground
# /nix/store/lvl6ixcnys5h88a5483gavrb9zb5mac1-fine-playground-0.1.0
# wasm smoke passed with 85 Rainfall events
# serve smoke passed: 10662220 -> 2555916 bytes

PORT=4175 nix run --no-write-lock-file .#playground-service
curl -H 'Accept-Encoding: zstd' -D - -o /dev/null \
  http://127.0.0.1:4175/fine.wasm
# Content-Encoding: zstd
# Content-Length: 2555916
```

The public gateway did not forward the browser's Zstandard negotiation to the
origin. It instead fetched the uncompressed 10.6 MiB module, recompressed it to a
3,550,655-byte edge response, marked that response `DYNAMIC`, and delivered it at
about 12 KiB/s in a 295-second measurement. The loopback publication socket moved
the precompressed object at 187 MiB/s, isolating that failure from both Python and
Vite.

Fine therefore exposes the precompressed module as the explicit
`fine.wasm.zst` asset. Before initializing Emscripten, the browser fetches a tiny
Zstandard-encoded sentinel. If decoding yields `fine-zstd-ok`, Emscripten's
`locateFile` selects the explicit object; otherwise it retains the ordinary Wasm
path. The explicit extension also changes the gateway's treatment: its first
response was a cache miss with the exact 2,555,916-byte payload, and the next was
a Cloudflare hit delivered in 2.08 seconds at 1.23 MiB/s. The transferred bytes
matched the build's `.zst` file exactly. This is the completed delivery fix; Vite
alone was not one.

The first deployment exposed a separate cache-identity bug: the previously fixed
`/app.js` URL remained a valid four-hour gateway hit, so a new page could still
execute the old loader. Vite now hashes its JavaScript and CSS entry names. The
Wasm content hash also enters the Emscripten module, ordinary Wasm, and compressed
Wasm filenames; `generated-assets.js` binds those names into the app at build
time. A new compiler therefore cannot be paired with a stale loader or module,
and old immutable assets remain harmless until the gateway evicts them.

The clean final artifact is
`/nix/store/qnzgk7v2wy3wzf3l6y7ii2w4cvwg296b-fine-playground-0.1.0`. Its page
selects `/assets/index-B8sDGEHz.js`, which selects
`/fine-a138be9b1e4c.wasm.zst`. After one 32.16-second cache fill, a public repeat
was a gateway hit: 2,555,916 exact bytes in 2.41 seconds at 1.06 MiB/s. The
compressed object hash matched the Nix artifact.

## 2026-09-02 — Selected proof terms in the browser result

The result pane now begins with a compact `binding ← body` list for every typed
proof hole selected during the Rainfall run, followed by the ordinary verification
diagnostics. It reads only accepted `proof.search.select` events and associates
their stable hole IDs with the source binding recorded by `proof.search.open`; it
does not scrape the executable's human-readable output or infer a proof from Z3
diagnostics.

The shared `playground/rainfall.js` extractor is exercised against the actual
WebAssembly transitivity run during the package check. The smoke requires exactly
`composed ← trans[left, middle, right](p, q)`, in addition to the existing 85-event
grammar, solve, lift, and closure requirements. This is the browser-visible
intermediate step before CST-owned source replacement: it proves that the selected
Fine term reaches the UI without yet editing user bytes.

Clean build before commit:

```sh
nix build --no-link --print-out-paths .#playground
# /nix/store/ghkc824qwp113v6yjdaxlphl8c8y6va8-fine-playground-0.1.0
```

## 2026-09-02 — Congruence without premature proof elimination

The first probe for proof-only elimination asked whether identity could be moved
through an ordinary value expression using only the existing absorbed context.
Two attempted fixtures were rejected before they could answer it: `+` and `!`
are not forms in the deliberately tiny current value grammar. A third attempt
bound two names to the exact same interned value, so `refl` closed the transformed
goal and failed to discriminate congruence from reflexivity.

The retained `identity-congruence.fine` uses only nested Boolean equalities. A
zero-premise `neg_characterization` creates non-reflexive local evidence, then
`truth_congruence` absorbs that evidence and proves equality beneath an additional
`== true`. Its result does not expose either static index directly, so backward
matching must recover both from the exact local proof parameter. The Z3 selector
chooses:

```text
truth_congruence[x == false, (x == true) == false](p)
```

This showed that ordinary congruence does not earn a new eliminator: the current
checked proof-function/context boundary already expresses it without inspecting
proof structure or creating a runtime proof value.

The first materialization failed on a real rendering defect. Nested equality
indices printed as `x == true == false`, although the parser admits only one
equality whose operands are primary expressions. `print_value` and
`print_value_substituted` now parenthesize equality children. The selected proof
therefore reparses with exact structure, and the checked-in materialized fixture
reruns without search.

Validation:

```sh
cmake --build build/proof-core --target fine-bin
build/proof-core/fine materialize --proof-selector z3 \
  fine/fixtures/identity-congruence.fine
# exact match: fine/fixtures/identity-congruence-materialized.fine

nix build --no-link --print-out-paths .#default
# /nix/store/gkrw31d25zqb2pdr6m7wi0kxyv4pr994-fine-0.1.0
```

The install check now requires the exact Z3-selected application, byte-for-byte
materialization, the parenthesized lifted identity on rerun, and absence of a
second proof search.

## 2026-09-02 — ordinary runtime enums before static indexed families

h chose a clean surface split: `enum` for ordinary runtime ADTs and a later
`proof inductive` form for ATS-style static indexed families. I closed the first
vertical slice rather than implementing both through one shared tagged term.

Implementation:

- extended `syntax::ValueType` with named enumeration sorts while preserving the
  disjoint `ValueType` / `ProofType` boundary;
- added top-level `enum` declarations with typed constructor payloads and
  recursive self fields;
- represented each enum as a native Z3 recursive datatype. Fine retains the
  exact constructor, recognizer, and accessor declarations returned by the same
  manager instead of reconstructing them from names;
- generalized runtime value kinds from the two built-ins to named sort identity.
  The proof-model selector now uses the manager-local Z3 sort id for identity
  carriers, so enum carriers remain distinct without inventing a parallel type
  code;
- added value-level `match` with constructor ownership checks, binder arity and
  payload typing, exactly-once exhaustiveness, a single result type, and lowering
  to native recognizers/accessors plus `ite`;
- constructor calls remain runtime `ValueTerm`s. `Id(Nat, ...)` evidence remains
  disjoint `ProofEvidence`, and the run boundary still reports zero runtime proof
  variants.

The executable fixture `fine/fixtures/runtime-enum.fine` declares recursive
`Nat = zero | succ(Nat)`, checks a symbolic reconstruction function over an
arbitrary `Nat`, constructs `one`, eliminates it through `predecessor`, and forms
`Id(Nat, one, one)` without creating a proof value. Controls separately reject a
missing `succ` arm and `succ(true)`. This is deliberately not `proof inductive`:
that next form must add indexed proof constructors and proof-producing
elimination without reusing runtime enum matching or lowering a family to Bool.

Local validation before the clean build:

```
cmake --build build/proof-core -j2
./build/proof-core/fine run fine/fixtures/runtime-enum.fine
./build/proof-core/fine rain fine/fixtures/runtime-enum.fine > /tmp/runtime-enum.rain
python3 fine/rainfall_validate.py fine/fixtures/runtime-enum.fine /tmp/runtime-enum.rain
python3 fine/rainfall_replay.py /tmp/runtime-enum.rain
```

Rainfall validation reported 25 events, 5 source nodes, 4 terms, 3 exact
source-term edges, and zero proof holes. The failed first draft of the positive
fixture used `refl(zero)` for `Id(Nat, answer, zero)`; Fine correctly rejected it
because the match-reduced `answer` is solver-equal but not exact manager-local
AST identity. The final fixture uses `refl(one)` only for identical endpoints and
keeps the semantic `answer == zero` check as an assertion.

The clean declared build and install-check suite completed with:

```
nix flake check --print-build-logs
nix build --no-link --print-out-paths .#default
```

`nix flake check` passed and the native artifact is
`/nix/store/phhsharb7400y2fc7z35hsbimgf8ap74-fine-0.1.0`.

## 2026-09-02 — `proof inductive` constructor-introduction slice

Immediately after closing ordinary runtime enums, I implemented the other half
of h's chosen surface split as a separate static representation rather than
sharing runtime enum machinery.

The new syntax is:

```fine
proof inductive Even(value: Nat) {
  even_zero() -> Even(zero);
  even_next(previous: Nat) needs [prior: Even(previous)]
    -> Even(succ(succ(previous)));
}
```

A proof family records the types of its ordinary value indices. Each constructor
has explicit static value parameters, virtual proof fields, and an indexed proof
result. Applications preserve that separation as
`even_next[zero](zero_even)`: square brackets instantiate static value
parameters; parentheses supply proof evidence. Family instances elaborate to a
new `InductiveType` containing the family identity and strong manager-local index
terms. `ProofEvidence` now carries either an `IdentityType` or an
`InductiveType`; neither representation was added to `ValueTerm`.

Constructor declarations are checked after registering their own family, so
recursive proof fields are legal. Every parameter type and result-family arity is
checked. Constructor application checks value argument types, recursively checks
proof fields, elaborates the instantiated result, and requires exact family and
same-manager AST identity for every index. It does not ask Z3 for semantic
equality. The discriminating control asks `even_zero()` to inhabit
`Even(predecessor(succ(zero)))`: that index is solver-equal to `zero`, but Fine
correctly rejects it because it is not the constructor's exact result term.
A second control supplies `Even(zero)` where `Even(succ(succ(zero)))` is required;
a third calls `even_zero()` in runtime value code.

Rainfall now separates `proof.inductive.declare`,
`proof.inductive.constructor.apply`, and `proof.inductive.form`. Each records that
no runtime datatype/value was created. The positive trace has 13 events, four
source nodes, no generated Z3 terms, and zero proof holes; both rainfall
validation and replay pass. `fine materialize` makes no changes to the explicit
constructor tree and its required search-free rerun passes.

This closes introduction only. I deliberately did not add a Bool projection,
proof match, induction hypotheses, or holes. The next slice must make
proof-producing elimination preserve constructor owner, exact proof fields, and
recursive hypotheses before any solver summary is involved.

Local commands:

```
cmake --build build/proof-core -j2
./build/proof-core/fine run fine/fixtures/proof-inductive-even.fine
./build/proof-core/fine rain fine/fixtures/proof-inductive-even.fine > /tmp/proof-inductive.rain
python3 fine/rainfall_validate.py fine/fixtures/proof-inductive-even.fine /tmp/proof-inductive.rain
python3 fine/rainfall_replay.py /tmp/proof-inductive.rain
./build/proof-core/fine materialize fine/fixtures/proof-inductive-even.fine > /tmp/proof-inductive-materialized.fine
cmp fine/fixtures/proof-inductive-even.fine /tmp/proof-inductive-materialized.fine
```

The declared check and clean install-check build completed with:

```
nix flake check --print-build-logs
nix build --no-link --print-out-paths .#default
```

All checks passed. Clean native artifact:
`/nix/store/1k0ii1k35s5afxkhkig9gdj56k4wynax-fine-0.1.0`.

## 2026-09-02 — browser reference catches up to the accepted language

h asked that the playground language reference remain current. The page had
fallen one syntax slice behind immediately: it documented identity proof
functions but not the newly accepted runtime enum/match or proof-inductive forms.

I expanded the open reference table with four accepted forms and their actual
checks: runtime enum declarations, typed constructor application, exhaustive
runtime match, and static indexed proof families/constructor evidence. The note
above the table now states the admitted declaration order and distinguishes
runtime types from both identity and inductive proof types.

The CodeMirror lexical mode now recognizes `enum`, `match`, `=>`, and the
`proof inductive` two-token declaration. Its stream state retains the names of
value types introduced by `enum` separately from proof types introduced by
`proof inductive`, so later occurrences of `Nat` and `Even` receive different
syntax roles without turning the highlighter into a second parser.

`playground/serve-smoke.mjs` now fetches the built page and requires the four
current reference rows. The repository close condition in `TODO.md` and
`fine/ROADMAP.md` now requires each syntax-changing slice to update both the
browser reference and lexical highlighting.

A direct `npm run build` initially failed with `ENOENT` for
`playground/public/fine.wasm`; this is expected outside the flake because the
playground derivation injects the current Wasm artifact during `preBuild`. The
correct declared command passed, including Wasm execution and served-asset
smokes:

```
nix build --no-link --print-out-paths .#playground
```

Artifact: `/nix/store/siyisjkz7mnr4pda192n2i3mynjsjxzi-fine-playground-0.1.0`.
I restarted `fine-playground.service`, verified both it and
`rc-publish-fine.service` active, and fetched both localhost and
`https://fine.shit.yachts/`; both contained the indexed-proof-family reference.

## 2026-09-02 — indexed proof-family elimination refines before exhaustiveness

h supplied the exact admission test rather than merely asking whether proof
matching existed: scrutinizing `evidence: Even(value)` must make
`refl(value) : Id(Nat, value, zero)` check in the base arm, make
`refl(value) : Id(Nat, value, succ(succ(previous)))` check in the recursive
arm, and bind both `previous` and `prior`. An empty family must eliminate into
an arbitrary proof, and a concrete impossible index such as
`Even(succ(zero))` must leave no demanded branches.

I added proof expressions of the form:

```fine
match evidence {
  even_zero() => shape_zero[value](refl(value)),
  even_next[previous](prior) =>
    shape_next[value, previous](refl(value), prior),
}
```

Square brackets bind constructor value parameters and parentheses bind virtual
proof fields, preserving constructor application syntax. The scrutinee is a
local proof name in this first eliminator slice. Proof functions may now use a
checked `{ proof-expression }` body. Semicolon declarations retain their old
identity-theorem behavior. Body-bearing functions can return either identity or
inductive evidence and can be applied later; identity hole search sees only
identity-result functions whose parameters are all identity evidence.

`ProofEvidence` retains the source syntax of inductive indices. For each family
constructor, the elaborator structurally unifies rigid scrutinee indices against
the constructor result using native runtime-constructor identities. A direct
symbolic proof-function index is flexible: the arm environment replaces it with
the constructor result before re-elaborating the expected proof type. Unbound
constructor parameters become distinct static symbols, then appear under the
source arm binders. Constructor proof fields become separate virtual locals and
identity-shaped fields are absorbed only in that arm.

Exhaustiveness runs over this reachable-constructor table. Missing reachable
arms fail. Supplied unreachable arms fail with a demand to omit them. A
zero-constructor family produces an empty table, so `match impossible {}` checks
at any proof result. `Even(succ(zero))` also produces an empty table because
neither `zero` nor `succ(succ(previous))` unifies with the index. A repeated
symbolic index is refined consistently: the `Split(value, value)` control has no
reachable constructor when the sole result is `Split(zero, succ(zero))`; the two
occurrences cannot silently overwrite one another.

The positive fixture `proof-inductive-match.fine` defines `EvenShape` so the
base and recursive arms must explicitly supply those two different `refl`
proofs. The recursive result also requires the pattern-bound `prior`, forcing
both kinds of binder to be usable. A run applies the checked eliminator to
`even_zero()` evidence. Separate controls reject one omitted symbolic branch and
one explicitly written impossible branch.

Rainfall records one `proof.inductive.match.branch` per reachable arm with its
constructor, refined source indices, static binders, and proof binders, followed
by `proof.inductive.match` at the same node-specific scope. Replay validates the
branch list, distinct binders, constructor uniqueness, counts, and zero-arm
case. The old replay rule initially rejected body-checked proof functions because
it required every `proof.function.verify` event to name an unsat proposition;
it now distinguishes `status: body-checked` with no solver term from the existing
`status: unsat` theorem check.

The architecture, proof-term guide, roadmap, TODO, fixture index, README, and
browser reference now describe branch refinement, reachable exhaustiveness, and
empty elimination. The playground smoke requires the two new reference rows;
the lexical mode gives `refl` its proof-term role and retains `match` as a
keyword without attempting to become a second parser.

Local checks:

```
cmake --build .build -j2
.build/fine run fine/fixtures/proof-inductive-match.fine
.build/fine rain fine/fixtures/proof-inductive-match.fine > /tmp/proof-match.rain
python fine/rainfall_validate.py fine/fixtures/proof-inductive-match.fine /tmp/proof-match.rain
python fine/rainfall_replay.py /tmp/proof-match.rain
nix flake check -L
nix build --no-link --print-out-paths .#default .#playground
```

The positive run verifies `expose_even`, `absurd`, `impossible_even`, and
`contradictory_indices`, then forms `zero_shape : EvenShape(zero)`. The Rainfall
trace has 36 events, 12 source nodes, zero Z3 terms, and zero proof holes. The
first clean combined build produced native
`/nix/store/jc8dzgm7hs6abvyv81bmsfq8fa1rcy0j-fine-0.1.0` and playground
`/nix/store/xjbsx1j164vqrrhhd66ishzgkvy63vd9-fine-playground-0.1.0`; a final
rebuild after the branch-event validation change follows below if its content
paths differ.

The final declared rebuild, including match-branch replay validation and the
repeated-index control, passed. Final clean artifacts:

```
/nix/store/xib8cmpg3nn9gwj72jrxjxxjxq2z3jhj-fine-0.1.0
/nix/store/sm1wbvydwk83fj3wjwsfqi3vavfwvaq6-fine-playground-0.1.0
```

## 2026-09-02 — `takes` replaces `needs` at every static-input declaration

h preferred Latte's `takes` spelling and left the final choice to me. I removed
`needs` rather than keeping two names for one mechanism. The declaration is a
static function/constructor input; caller omission triggers coeffect search, but
that search policy does not turn the parameter itself into an ambient condition.
The resulting surface is deliberately asymmetric and concrete:

```fine
proof function symm(left: Bool, right: Bool)
  takes [given: Id(Bool, left, right)]
  -> Id(Bool, right, left);

proof reversed: Id(Bool, right, left) = symm[left, right](given);
replace(x, y) using [same = p]
```

`takes` declares virtual evidence inputs. Parentheses still carry ordinary
runtime/static value parameters according to the declaration kind. `using`
still supplies a taken coeffect explicitly at a value call; omitting it retains
exact caller-local lexical resolution. Rainfall operation names remain about
coeffects because that is the checking mechanism, not the source keyword.

The parser now recognizes only `takes`; it does not retain `needs` as a
compatibility alias. Every executable and materialized fixture, architecture and
proof-term document, roadmap example, README example, install check, playground
sample, language-reference row, and CodeMirror keyword was migrated together.
`reject-needs-keyword.fine` preserves the removed spelling as a red test. The
served-page smoke requires `takes [` and rejects a reference that still contains
`<code>needs [`.

Validation:

```
cmake --build .build -j2
# every non-rejecting fine/fixtures/*.fine ran successfully
.build/fine run fine/fixtures/reject-needs-keyword.fine  # exits 1 at `needs`
nix flake check -L
nix build --no-link --print-out-paths .#default .#playground
```

The full native install check, materialization comparisons, Rainfall validation,
Wasm execution smoke, and served-page smoke pass. Pre-commit artifacts:

```
/nix/store/ahygax0jpfjlld2sbcni2vmzcnwdwbc1-fine-0.1.0
/nix/store/r422nz7zfy8bwsag29acjgyjw8srym4p-fine-playground-0.1.0
```

## 2026-09-03 — structural proof recursion is an induction-hypothesis edge (`6694f9aca`)

The termination boundary is now executable for recursive indexed proof evidence.
A body-bearing proof function may name one proof parameter with
`inducts(evidence)`. Fine verifies that the name belongs to an indexed-family
parameter, exposes the function to its own body, and treats a recursive spelling
as an induction-hypothesis use rather than a runtime call.

`ProofEvidence` now carries optional structural-root and immediate-parent names.
When proof matching binds a same-family constructor field beneath the designated
parameter, the field receives the root and exact scrutinee parent. Matching that
field later propagates the original root to its own recursive fields. A self-call
must pass an exact named field whose root is the declared induction parameter in
the corresponding proof-argument position; the ordinary parameter/result checks
then verify its instantiated indices and result. Passing the root evidence again,
passing a constructed expression, or omitting `inducts` cannot enter recursion.
This remains proof-family structural descent only: runtime recursion and numeric
well-founded measures were not introduced.

`proof-inductive-induction.fine` defines `Even` and a separate `Rebuilt` family.
Its `rebuild` function matches an `Even(value)` derivation and recursively
constructs `Rebuilt(value)` through the exact `prior: Even(previous)` field. The
base and recursive result indices both check. The nondecreasing control calls
`loop[value](evidence)` and fails specifically because `evidence` is the root,
not a descendant. The unannotated `anything(n): Absurd()` control cannot resolve
itself at all.

Rainfall emits `proof.induction.hypothesis.use` before the enclosing proof
function's verification event. It retains the function, declared root parameter,
immediate parent evidence, recursive field, and source body separately, with
`runtime_call_created: false`. Replay validates the early edge structurally and,
after the complete stream is read, requires its function to have a body-checked
verification event. Ordinary post-declaration proof applications retain their
existing event and ordering.

The parser, C++ elaborator, replay validator, fixture index, architecture,
proof-term guide, roadmap, TODO, root README, playground reference, CodeMirror
keyword table, and served-page smoke were updated together. The flake install
check runs the positive fixture, validates its Rainfall, requires the new event,
and asserts both rejecting diagnostics.

Validation before the implementation commit:

```
cmake --build .build -j2
.build/fine run fine/fixtures/proof-inductive-induction.fine
.build/fine rain fine/fixtures/proof-inductive-induction.fine > /tmp/fine-induction.rain
python fine/rainfall_replay.py fine/fixtures/proof-inductive-induction.fine /tmp/fine-induction.rain
.build/fine run fine/fixtures/reject-nondecreasing-proof-recursion.fine
.build/fine run fine/fixtures/reject-recursion-without-inducts.fine
nix flake check -L
nix build --no-link --print-out-paths .#default .#playground
```

The combined pre-commit build passed native install checks, materialization and
Rainfall replays, the Wasm CLI smoke, and the Vite served-page smoke. Its content
paths were:

```
/nix/store/3lvnghirkrl8qs8ngy1fxhwvai7nsf6l-fine-0.1.0
/nix/store/0qmxv9pimdqxmprqwlyfsn4idgh8ypfv-fine-playground-0.1.0
```
