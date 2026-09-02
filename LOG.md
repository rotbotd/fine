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
