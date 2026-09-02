# Fine

Fine is a solver language built inside a soft fork of Z3. Its first parsed
program declares two finite transition systems, asks Z3 to fill an
array-shaped bisimulation hole, lifts the finite relation into parseable Fine
model syntax, parses and elaborates that printed witness through the same Z3
context, and checks exact AST identity.

From this checkout:

```sh
cmake -S . -B build/fine -G Ninja \
  -DZ3_BUILD_LIBZ3_SHARED=OFF \
  -DZ3_BUILD_EXECUTABLE=OFF \
  -DZ3_BUILD_TEST_EXECUTABLES=OFF \
  -DFINE_BUILD_EXECUTABLE=ON
cmake --build build/fine --target fine-bin
./build/fine/fine demo-bisim
./build/fine/fine run fine/fixtures/two-state-bisim.fine
./build/fine/fine run fine/fixtures/synth-max.fine
./build/fine/fine run fine/fixtures/synth-match-open.fine
./build/fine/fine run fine/fixtures/check-counterexample.fine
./build/fine/fine run fine/fixtures/check-valid.fine
./build/fine/fine run fine/fixtures/check-datatype-counterexample.fine
./build/fine/fine run fine/fixtures/check-tuple-counterexample.fine
./build/fine/fine run fine/fixtures/induction-length.fine
./build/fine/fine rain fine/fixtures/synth-max.fine
./build/fine/fine rain fine/fixtures/two-state-bisim.fine > bisim.rain.jsonl
python fine/rainfall_project.py fine/fixtures/two-state-bisim.fine bisim.rain.jsonl \
  --html bisim.html > bisim.projection.json
./build/fine/fine rain fine/fixtures/check-counterexample.fine > check.rain.jsonl
python fine/rainfall_validate.py fine/fixtures/check-counterexample.fine check.rain.jsonl
python fine/rainfall_project.py fine/fixtures/check-counterexample.fine check.rain.jsonl \
  --edits transaction.json --html projection.html --write-source edited.fine \
  > projection.json
python fine/rainfall_generation_cli.py request edited.fine \
  --document document:editor --revision 1 --generation generation:1 \
  > request.json
./build/fine/fine rain --document document:editor --revision 1 \
  --generation generation:1 edited.fine > generation-1.rain.jsonl
python fine/rainfall_generation_cli.py admit request.json edited.fine \
  edited.fine generation-1.rain.jsonl > admission.json
python fine/rainfall_host_cli.py init .fine-live edited.fine \
  --document document:editor > launch.json
python fine/rainfall_host_cli.py run .fine-live --fine ./build/fine/fine
python fine/rainfall_host_cli.py materialize .fine-live
python fine/rainfall_live_cli.py .fine-browser \
  fine/fixtures/two-state-bisim.fine --fine ./build/fine/fine
```

`demo-bisim` embeds that exact checked-in Fine fixture; it is not a second
hard-coded solver path. `nix build --no-link` runs both entry points as install
checks. The equivalent SMT-LIB fixture is in `fixtures/`; design invariants and
rainfall trace identity rules are in `ARCHITECTURE.md`. The twelve-angle
synthesis review and the narrowed built-in-semantics plan are recorded in
`research/synthesis-pressure-test.md`.

`rain` writes JSONL only. The experimental synthesis replay follows native
maximum synthesis through public solver queries, completed counterexample values,
candidate selection, labelled instance activation, the unsat core, conditional
assembly, the successful builtin theory-application reductions inside its one
public simplification, an independent verification query, and the checked Fine
source witness. Every event names its producer and coverage. The internal
events cover only the observed `th_rewriter::reduce_app` path; this stream does
not pretend to contain substitutions, quantifier rewrites, other rewriter
instances, or solver search. This QF-LIA example is a regression harness for a
refutation-synthesis backend, not the language's reason to exist. Ordinary
`check`, induction, and bisimulation runs ask Z3 for proofs, counterexamples, or
models; lifting their terms does not turn them into synthesized source programs.

The bisimulation replay disables E-matching and records the accepted,
nontrivial quantifier instances which reach Z3's `qi_queue` binding callback
during the MBQI-only query. Each preprocessed quantifier retains a Fine source
role through its qid. The public clause stream records assumptions, inferences,
and deletions after preprocessing. For quantifier lemmas, the `inst` proof hint
joins the exact accepted-instance event to its admitted clause and ground
bindings; sequence adjacency is not treated as evidence. The stream then records
every completed finite relation cell, deterministic constant-array-plus-stores
extensionalization, and the checked Fine model witness. It does not expose
MBQI's auxiliary-context search,
discarded candidates, blocking clauses, assignments, decisions, watched-literal
traffic, or the causal contribution of a clause to the result.

`check` closes the missing counterexample loop for admitted value inputs. Fine
asserts the source assumptions together with the negation of the guarantees. A
satisfiable query returns a typed `counterexample` declaration; every completed
Int, Bool, enum, binary tuple, or monomorphic datatype assignment is lifted,
printed, parsed, and elaborated back to the identical same-manager AST. Recursive
field-bearing enum constructors lower directly to Z3 datatypes. An
unsatisfiable query reports that no counterexample exists. An ordinary check
remains quantifier-free and check expressions have no match or projection;
`counterexample` is a returned witness form, not an executable declaration.

The first induction slice deliberately translates rather than modifies Z3. A
structurally recursive `function` uses Latte's exhaustive `match` surface and is
registered through `Z3_add_rec_def`; Fine rejects a self-call unless the matched
argument is a direct recursive pattern field. `inducts(xs);` on a `check`
replaces its theorem with the weak direct-subterm induction step and gives the
ordinary quantified result to an otherwise untouched solver. The length fixture
is not provable by merely removing `inducts`: that control query remains in
Z3's recursive unfolding search past the install check's two-second boundary.
Rainfall labels the induction scheme as compiler-generated, then separately
records the E-matching-only query's admitted clauses and any accepted instances;
query scope never substitutes for a causal link. The translation, papers, and
remaining STLC boundary are in `research/induction-translation.md`.

Proof-family induction is a separate first-order path. Given an erased least
relation such as `Step(before, after)`, a check writes
`inducts(Step(before, after));` and repeats that exact atom in `assumes`. Fine
enumerates its retained proof constructors, substitutes each constructor's
result indices into the guarantee, and supplies one induction hypothesis at the
exact indices of every recursive premise. Each resulting branch is a separate
ordinary SMT refutation query. Rainfall keeps the constructor result and every
premise/hypothesis pair before the proof witness erases; it does not reconstruct
branches from Spacer's projected learned lemmas. This slice excludes explicit
proof matches, existential constructor fields, and typed branch counterexamples.

The first non-Horn proof field is explicit rather than smuggled through a body
variable:

```fine
arbitrary fresh: FreshApart(excluded) {
  Step(opened(before, fresh), opened(after, fresh), excluded);
}
```

`FreshApart` is a named `view ... over Name` proposition and creates no wrapper
sort. Fine keeps the arbitrary name, instantiated view requirement, opened
recursive atom, and resulting induction hypothesis as separate terms. It checks
that the view is inhabited for every constructor-parameter assignment before
using the requirement as a branch assumption, preventing an empty view from
proving anything. The field is never lowered to Horn; consequently membership
and fixedpoint-invariant queries reject a family containing it, and none of the
family's other constructors are registered with fixedpoint either. Full locally
nameless opening/support and its
freshness/equivariance proof remain the next test.

The first interruptible synthesis fixture fixes an exhaustive datatype match
while leaving one whole arm as `?payload`. Rainfall gives that source node a
snapshot-scoped typed grammar, records the independently verified lifted arm,
and binds its exact source span to the replacement text. Fine then assembles
and freshly verifies the whole match. `fine-rain-host materialize` accepts only
the current admitted trace and applies every verified arm replacement as one
host-owned edit transaction. Re-running the resulting source follows the normal
completed-arm path: its trace contains one whole-match verification query and
no candidate enumeration. Per-arm cancellation and residual display are not in
this slice and are paused until a proof-directed consumer justifies them. The
intended consumer is failure-directed helper-lemma or invariant search for a
stuck induction obligation, not arithmetic function generation.

For every rain, the first objects bind the run to a fresh opaque document and an
exact source snapshot (revision, SHA-256 hash, and byte length). Parsed declarations
and expressions have snapshot-scoped source identities. The `check` elaborator
emits explicit exact/desugared source-to-term evidence edges; generated witnesses
and internal Z3 terms remain unowned by source. Every declared term nevertheless
has a canonical `fine.generated-term.v1` rendering with explicit manager-local
sort and declaration bindings. Fine reparses that rendering after the solver
returns and requires exact same-manager AST identity; the raw Z3 printer is a
labelled diagnostic only. Provenance and Fine renderability are deliberately
independent. The installed
`fine-rain-validate` command admits only a replay whose snapshot, source nodes,
live term handles, manager, edge endpoints, chronology, and terminal state agree.

`fine-rain-project` first performs that same validation, then maps evidence
ranges through one ordered byte-offset transaction. Its JSON and standalone HTML
label every surviving old marker `transported`, label a wholly deleted marker
`unplaced`, and keep the claim snapshot separate from the unadmitted display
snapshot. With no transaction, evidence is `current`. Matching text and equal
hashes never upgrade an edited revision; only a separately validated Rainfall
trace for that revision can produce current evidence.

`fine-rain-generation request` binds one opaque generation to the exact current
display identity and prints the corresponding structured `fine rain` arguments.
`admit` validates a completed trace against its own retained source, then admits
it only when the current display still matches the request and the trace's run,
document, revision, hash, and length all match. A valid but late predecessor is
returned as `discarded`, never merged with current or transported annotations.

`fine-rain-host` is the editor-neutral transaction harness. `init` creates one
authoritative JSON state and immutable request/source artifacts. `advance`
holds a host lock while it applies an edit transaction, maps existing markers
to transported/unplaced, supersedes the previous request, issues the next one,
and atomically replaces the state. `run` executes Fine without holding that
lock, so edits remain responsive, then reopens the latest state for admission.
This deliberately permits a real late completion and proves that it is
discarded. `materialize` validates the current admitted match witness and feeds
its exact replacements back through one `advance`, so the new bytes are never
mistaken for evidence admitted under the predecessor revision. The host is a
filesystem protocol and test harness, not an editor UI.

`fine-rain-live` is the first deliberately small editor integration. It serves a
local browser textarea and an evidence pane from `127.0.0.1`, turns each changed
middle into one UTF-8 byte transaction, and starts a generation without blocking
the next edit. While Fine is running, the pane keeps the previous annotations
visibly `transported` or `unplaced`; only the host's ordinary admission path can
make them `current` again. Run the installed form with
`nix run .#live -- .fine-browser fine/fixtures/two-state-bisim.fine`. The browser is a protocol
client, not a second owner of revision or generation identity.
