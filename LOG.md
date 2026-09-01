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
