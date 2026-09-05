# Fine two-level core

Fine is a solver language inside a soft fork of Z3. The pre-proof-term language
is preserved at Git tag `pre-pat-1d7222a23`. This branch begins again at the
parser and elaborator because its central distinction must be structural:
proofs are static evidence and cannot become runtime values.

## Lossless concrete ownership

The lexer emits two coordinated views in one pass. The semantic token stream
feeds the existing AST parser; the concrete stream retains every identifier,
literal, symbol, whitespace byte, and line comment with an exact source span.
`ConcreteSyntaxTree` owns that stream, a document root with ordered declaration
children, and the semantic `Document`. Concatenating the concrete tokens must
reproduce the input byte-for-byte before parsing is considered successful.
The root accepts enums, proof families, value functions, and proof functions in
source order without treating their order as parser phases. At most one named
`run` block supplies executable bindings and assertions; definition-only
documents omit it entirely.

The AST still owns meaning. The concrete tree owns preservation and edits.
Every semantic span maps to a named `ConcreteRange`, so proof-hole replacements
and implicit coeffect insertions no longer pass anonymous byte pairs across the
materializer boundary. `fine roundtrip file.fine` exposes the identity check;
the build checks ordinary fixtures, deliberately ugly comments/tabs/spacing,
CRLF input, and exact materialization without normalizing surrounding trivia.

## Two representations, not one tagged union

The source syntax has separate `ValueType` and `ProofType` nodes. The elaborator
has separate `ValueTerm` and `ProofEvidence` structures. `ValueTerm` is closed
over the value kinds which may reach execution and models: `Int`, `Bool`, and
declared runtime enums. `ProofEvidence` contains an identity type, source ownership,
and formation history, but no runtime payload. There is no common term variant
with a proof case and no erasure pass which can accidentally forget to remove
one.

The first proof type is:

```fine
Id(Int, left, right)
```

`refl(value)` checks only when both endpoints elaborate to the exact same
manager-local Z3 value as `value`. Introducing a proof automatically places the
proposition `left == right` in the lexical SMT context. Rainfall retains the
source proof separately from that absorbed proposition.

Proofs are irrelevant to runtime behavior. Fine code cannot use a proof name as
an `Int` or `Bool`, and this core has no eliminator from proofs to values. Two
proofs may remain different source artifacts in Rainfall while contributing the
same proposition to checking.

## Ordinary runtime enums

`enum` adds a closed Z3 native datatype to the value level. Constructor payloads
may use `Int`, `Bool`, previously declared enums, or the enum itself recursively.
A runtime `match` compiles to the native recognizers and accessors and must name
every constructor exactly once. Its arms must agree on one runtime result type.

```fine
enum Nat {
  zero,
  succ(Nat),
}

function predecessor(value: Nat) -> Nat {
  match value {
    zero => zero,
    succ(previous) => previous,
  }
}
```

These datatypes do not blur the level boundary. `succ(zero)` is a `ValueTerm`.
An `Id(Nat, left, right)` inhabitant remains `ProofEvidence`, and no enum match
can inspect it. Static indexed families will use the separate `proof inductive`
form rather than pretending an indexed proof constructor is a runtime enum.

## Static indexed constructors

`proof inductive` declares an indexed family at the proof level. The family
indices are ordinary value terms, but its constructors and inhabitants exist
only as `ProofEvidence`:

```fine
proof inductive Even(value: Nat) {
  even_zero() -> Even(zero);
  even_next(previous: Nat)
    takes [prior: Even(previous)]
    -> Even(succ(succ(previous)));
}
```

`even_next(zero)` checks its explicit value parameter, searches exact local
evidence for the `prior` coeffect, and checks the exact result index.
`even_next(zero) using [prior = zero_even]` chooses the same demand explicitly.
Fine creates no corresponding runtime Z3 datatype and does not expose
`Even(value)` as a runtime Bool. A proof constructor therefore cannot occur in a
value expression. This is the ATS split at the current boundary: values carry
the runtime data; indices and evidence constrain it statically.

Constructor actual parameters use one ordinary positional list. A proof-typed
actual parameter is an explicit child. A parameter in `takes` is instead a
proof-irrelevant coeffect: callers omit it for exact lexical search or choose it
with `using`. A proof match binds only the actual parameters positionally; each
coeffect becomes a branch-local handle under its declared name.

Proof functions with bodies eliminate indexed evidence by proof-level matching.
This checked eliminator can return `prior` only because matching the recursive
constructor refines `previous` to `value`:

```fine
proof function even_pred(value: Nat)
  takes [evidence: Even(succ(succ(value)))]
  -> Even(value) {
  match evidence {
    even_next(previous) => prior,
  }
}
```

Constructor-result unification happens before an arm is checked. Here the only
reachable result equates `succ(succ(value))` with
`succ(succ(previous))`, introduces `previous` and `prior`, and makes the local
`prior : Even(previous)` inhabit the required `Even(value)`. This is
definitional index refinement, not an equality proposition guessed by Z3.

Exhaustiveness is computed after refinement. A family with zero constructors
has a valid zero-arm match, and `Even(succ(zero))` likewise has no reachable
constructors. An unreachable arm must be omitted. Matches remain proof-producing:
neither the scrutinee nor its proof fields can enter a runtime value.

An optional `inducts(evidence)` clause turns self-application in a body-bearing
proof function into an explicit induction-hypothesis use. Fine exposes the
function to its own body only after confirming that `evidence` is an indexed
proof parameter. Each same-family proof field bound while matching that evidence
is tagged with its exact parent and structural root. A self-call is accepted only
when its designated argument names one of those descendants; a call on the root
evidence is rejected before Z3 is involved. Rainfall retains the function,
induction parameter, parent evidence, and recursive field separately. This is
structural induction over proof evidence, not general runtime recursion or a
numeric termination checker.

Indexed proof holes preserve that same boundary. Their deterministic grammar is
`[exact-local, induction-hypothesis]`: a local must have exact family and index
identity, while an IH application must first pass structural descent and then
instantiate direct result indices and every proof parameter exactly. The root
evidence and mismatched locals never become candidates. Materialization replaces
the hole with the chosen Fine term and reruns with search forbidden. Proof
constructor synthesis and the Z3 datatype-model selector remain outside this
grammar.

## Contextual proof demand

A function declares evidence required from its caller:

```fine
function replace(left: Int, right: Int) -> Int
  takes [same: Id(Int, left, right)]
  ensures {
    result == right;
  }
{
  left
}
```

The function is checked with `same` as hypothetical static evidence; automatic
absorption makes `left == right` available to Z3 and closes the guarantee.
Every call instantiates the identity type with its value arguments. Coeffect
resolution selects exact matching proof evidence from the caller's lexical
context. It has no global instance table or theorem search.

An implicit resolution:

```fine
replace(x, y)
```

can be materialized as:

```fine
replace(x, y) using [same = p]
```

`fine materialize` applies those concrete-range insertions, reparses the resulting bytes, and
reruns with all implicit coeffect resolution forbidden. An explicit proof
argument is checked again but never becomes a runtime argument.

## Typed identity holes

A proof declaration may leave its evidence open:

```fine
proof self: Id(Int, x, x) = ?;
```

The expected proof type determines a finite grammar before enumeration. Its
first version contains exact local evidence in lexical order followed by
`refl(left)` only when the elaborated endpoints have exact manager-local AST
identity. A local proof with a different identity type never becomes a
candidate, and a hole with no well-typed production fails rather than asking Z3
to invent an untyped term.

The selected term replaces the exact `?` byte range during `fine materialize`.
Hole replacements and implicit `using` insertions share one ordered source edit
list. The resulting document is reparsed and rerun with both proof-hole search
and implicit coeffect search forbidden before it is emitted.

`--proof-selector z3` uses the direct grammar path for one-shot search as well as
live search. Fine visits exact requested identity types, discovers only applicable
local, reflexivity, and instantiated proof-function productions, and constructs
finite datatype states indexed by carrier, exact endpoint AST IDs, total cost,
completeness, closed frontier, and open leaves. Constructor fields point only to
strictly cheaper state sorts. Z3 returns a ground constructor tree and Fine lifts
it to source. Rainfall retains every production and every typed state transition,
so the complete bounded residual is inspectable without first constructing every
candidate tree. The model cannot add a production, change a type, or escape the
recorded state graph.

### Resumable partial proof checkpoints

`fine checkpoint --proof-budget n file.fine` makes an unfinished search an
ordinary Fine term rather than a dump of solver internals. In checkpoint mode,
the identity-proof grammar adds a typed `open` production whose only lifted
syntax is `?`. Applications may therefore retain checked and open children as
separate named coeffects, for example
`trans(left, middle, right) using [first = p, second = ?]`. Fine ranks a complete
root above every partial tree. With no complete root it maximizes closed frontier
obligations, then minimizes constructor cost, then keeps deterministic grammar
order. A unary chain around an open leaf cannot beat the unchanged hole merely
for being deeper.

Fine compacts the productions of the complete typed partial frontier into exact
bounded datatype states. The model must reproduce its completeness,
closed-frontier count, open-leaf count, cost, and exact source tree before
lifting. The checkpoint then
replaces the original hole through its concrete range and reparses. Validation
checks every fixed proof application with synthesis disabled, never absorbs an
open proof into the SMT context, and stops the run at the incomplete declaration.
Running `checkpoint` again searches the nested holes in their now-explicit
context; `identity-checkpoint.fine` closes its second child on the second pass.

This remains the persistent source boundary. Browser interruption discards its
current worker and retains only a source which another Fine instance has
reparsed and rechecked; it never scrapes Z3's private learned clauses and calls
them proof structure.

The browser now has the atomic edit half of this boundary for completed search.
`materialize holes` asks the Wasm CLI to write exact materialized bytes to MEMFS,
then replaces the CodeMirror document with one transaction. A failed check makes
no edit, and one undo restores the exact prior bytes. That same transaction
primitive receives completed checkpoint epochs without inspecting a solver's
in-flight state.

Checkpoint interruption uses process ownership rather than solver-private
state. On a pthread-capable client, the dedicated Web Worker runs one
`live-checkpoint` command whose producer increases the exact bounded proof cost
without a predeclared final cost. Each epoch discovers applicable instantiated
productions directly from the expected type and lexical evidence, constructs
exact bounded datatype states without concrete candidate trees, and translates
the selected Z3 model into a private context. The Fine pthread lifts it while the
producer starts the next grammar. Its publisher writes full source plus score metadata
to an eight-slot shared-memory ring. The page drains that ring while the
worker's JavaScript event loop is blocked inside Wasm and uses its independent
Fine module to reparse and recheck every retained source. Only then does a view
enter the Rainfall pane or become the last installable checkpoint. Rainfall
retains production, exact-state, and transition counts and records that no
candidate-tree frontier was enumerated. The producer owns one context-bound
incremental selector. If two consecutive epochs have the same canonical
production vector, it retains every earlier datatype sort and constructs only
the newly reachable higher-cost states. A newly discovered production resets the
state family because Z3 datatype alternatives are immutable; Rainfall records
both resets and the exact number of states reused.

On clients without cross-origin isolation, the same Web Worker retains the
older cooperative protocol: it repeatedly runs `checkpoint --proof-budget n`
and posts each source-and-Rainfall epoch after validation. Under either path,
stop first terminates the owning worker and its in-flight Z3 context, then
installs only the last independently validated source in one atomic editor
transaction. A completed live run replaces the provisional mailbox event with
Rainfall from the exact completing execution. No partially written MEMFS file,
learned clause, or unvalidated mailbox payload crosses the edit boundary.

### Native nonblocking live lifting prototype

`LiveLiftPipeline` fixes the manager and allocation boundary for a later
unbounded run. The active solver thread is the only thread which touches its Z3
context. An observer assigns a monotone sequence number, translates the observed
term into a newly owned Z3 context, and places that snapshot in a bounded queue.
The Fine worker touches only the snapshot context, emits canonical generated
Fine, reparses/reifies it to exact identity in that context, publishes the view,
and destroys the term before destroying its context. There is no run-lifetime
arena and no concurrent access to one manager.

Queue pressure discards the oldest unrendered views while always accepting the
newest one. Cancellation reduces the queued tail to that newest snapshot; a
snapshot already being lifted may finish first, but the final publication must
carry the latest observed sequence. The publisher owns the last validated view
after the pipeline releases its snapshot.

`fine live-lift-probe` exercises two distinct contracts. A real Spacer
new-lemma callback copies three exported lemmas while the Fine lifter is blocked
on a gate, and the fixedpoint query still returns. A deterministic twelve-term
burst then fills a three-slot queue under the same blocked lifter; cancellation
publishes the newest term, drops ten intermediate snapshots, and closes with
`latest_observed == latest_published == 11`. The counts diagnose ownership; the
gate, rather than a timing threshold, proves the producer does not wait for
rendering.

The prototype also has a separate Emscripten pthread build. The default Wasm
package remains single-threaded; `playground-wasm-pthreads` compiles Z3 and Fine
with shared memory, enables the live-lifting sources, and preallocates two worker
threads. The playground serves both content-hashed modules and selects the
pthread module only when `crossOriginIsolated` is true and `SharedArrayBuffer`
exists. Other clients retain the ordinary module rather than failing at startup.

The Fine origin sends `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp` on the document and every Wasm path,
including the custom precompressed response. The pthread smoke inspects the
module's live heap to require shared backing memory and runs the complete
two-thread C++ probe. The served-response smoke separately requires both headers
on HTML, ordinary Wasm, and pthread Wasm.

The visible pthread path now uses this queue. Its producer is open-ended
iterative deepening over direct exact bounded grammars. Each epoch discovers
applicable instantiated productions from the requested result and lexical proof
types, constructs states indexed by exact type, completeness, frontier, holes,
and cost, and gives those finite datatypes to Z3 without constructing candidate
trees. Budgets one through four remain byte-identical to the enumerated oracle.
The discriminator's production vector grows through budget three, then budget
four reuses all 120 earlier state sorts and adds 79 rather than rebuilding them.
Each hole owns its live selector and is searched in source order. Every queued
model snapshot also owns the already completed concrete edits that preceded that
hole. The lifter composes those edits with its current lifted term against the
original concrete tree before publishing, so a dropped intermediate frame or a
retained later-hole frame cannot erase an earlier completed replacement.

## Elaboration ownership

`runtime.cpp` is only the stable public adapter for diagnostics, execution, and
concrete source materialization. The semantics behind that adapter live in
`fine::elaboration`; there is no omnibus runtime or elaborator object.

Three owners have disjoint private state:

- `ValueElaborator` owns the Z3 context, runtime enum/constructor registry,
  value-function registry, value elaboration, and value-side progress counts.
- `ProofEngine` owns proof-family, proof-constructor, and proof-function
  registries; identity and indexed type elaboration; proof search, matching,
  induction; and proof-side progress counts.
- `DocumentRunner` owns source-order orchestration, run-local environments,
  Rainfall lifetime, materialization ranges, and the public `ExecutionResult`.

The value owner cannot see proof maps. It receives only `ProofContext`, whose
four operations are proof-name classification, identity-type elaboration, and
absorption. The proof owner depends on the value owner's public typed API and
cannot execute statements. `DocumentRunner` composes them through that interface
and a `MaterializationSink`; neither semantic owner receives the public result
object. At close, the runner reads their separate counters and assembles the
result. The compiler now enforces these boundaries through private members and
interfaces rather than relying on translation-unit convention.

`elaboration_internal.h` contains the shared private semantic vocabulary and
contracts. `ValueTerm` and `ProofEvidence` remain disjoint structures there.
Implementations are in `value_elaborator.cpp`, `proof_engine_types.cpp`,
`proof_engine_search.cpp`, `proof_engine_inductive.cpp`, and
`document_runner.cpp`.

## Cacheable value-flow boundary

`value_flow.cpp` lowers value functions into an immutable graph before staging
touches Z3. Every node has a resolved local, constructor, or direct function
identity and an ordinary Fine value type. Canonical semantic keys include the
resolved operation tree and types while excluding source positions, trivia,
local spelling, streams, Rainfall identities, pointers, and manager-local Z3
handles.

The exact named-call graph is partitioned into strongly connected components.
`stage_analysis.cpp` currently computes the first exported summary: which
parameters may affect a function's result. Mutually recursive components reach
their least dependency summaries by monotone iteration. Each SCC cache key is
its own semantic graph plus the fingerprints of imported summaries. A changed
callee therefore revisits reverse callers only when its exported dependency
summary changes; an unrelated SCC remains reusable. The cache owns only Fine
data, so a later session can replay a hit into a fresh Z3 manager rather than
retaining an invalid `z3::expr`.

This is the cache and call-graph substrate, not the completed staging pass.
`bottom | comptime(value) | runtime`, executable match edges, effect summaries,
and the proof-elimination consumer remain open. `stage-analysis-probe` checks a
cold and warm run, semantic invalidation, local-renaming stability, unrelated
reuse, and a mutually recursive dependency fixed point.

## Rainfall boundary

The existing manager-local term registry and `fine.generated-term.v1` renderer
survive the cut. Every absorbed identity proposition is a strong Z3 term and is
reparsed/reified to exact AST identity before run closure. Proof formation,
context absorption, coeffect declaration, demand instantiation, caller
resolution, and callee use are separate events. The run begins with an explicit
`proof.erasure.boundary` event and closes with `runtime_proof_values: 0`.

A deterministically searched proof hole records `proof.search.open`, every
well-typed `proof.search.candidate`, the exact candidate selected, and the
unchosen finite frontier at `proof.search.close`. Candidate event IDs, rather
than callback order, connect the selection and close. Replay validation checks
that the selected candidate plus the residual list exactly exhaust the enumerated
frontier.

Model selection adds `proof.model.grammar`, `proof.model.solve`, and
`proof.model.lift` as separate events. The grammar contains structured typed
productions and the complete bounded state graph. Replay checks canonical
production numbering, every transition's result and child types, strict child
cost decrease, the exact score recurrence, the root score, and the solve/lift/
selection chain. `proof.search.close` retains that grammar as the compact
residual with the lifted tree selected out; candidate trees were not enumerated.
Checkpoint traces additionally retain `complete`, `closed_frontier`, and
`open_leaves`; terminal status is `checkpointed`, and no later assertion may
appear in the trace after an open proof.

A proof source node is not falsely attached to a Z3 proof term. Z3 receives the
proposition which the source proof licenses; Fine retains the proof's own static
identity.

## Quarantine

The previous Bool-valued predicates, fixedpoint membership, predicate induction,
ordinary checks, synthesis runtime, and bisimulation runtime are not linked into
this target. Their exact implementation and fixtures remain at the preserved
tag. A future inductive proposition must introduce derivation inhabitants from
birth and may only use an erased predicate relation as a backend shadow.

The first recovered ordinary consumer is narrower: when a value function's
negated guarantee is satisfiable, the value elaborator completes its inputs and
result in that model. Each value is lifted into the existing value syntax,
printed, parsed as a returned counterexample witness, and reified to exact
same-manager AST identity. A fresh solver then fixes those inputs, restores the
declared coeffect propositions, and requires the original guarantee to be
unsatisfiable. This does not restore the old `check` declaration and does not
make `counterexample` executable source.

## Current implemented boundary

Runtime values include `Int`, `Bool`, and native Z3 enum datatypes with typed
payloads, recursive self fields, construction, and exhaustive matching. Value
functions are nonrecursive and may declare guarantees and lexical identity
coeffects. Failed guarantees return typed, exact-roundtripped counterexamples;
negative integer literals are accepted so every completed integer numeral has a
source form.

Static evidence includes identity, reflexivity, named proof functions, indexed
proof families, proof-only matching with index refinement and unreachable-arm
elision, structurally checked induction, and lexical coeffects. Typed holes can
select exact local evidence, reflexivity, bounded identity proof applications,
or structurally admitted induction-hypothesis applications. Identity search has
both deterministic and Z3 datatype-model selectors; checkpoint mode can retain
a typed partial source tree with unresolved holes.

There are no runtime proof values, proof elimination into runtime data, runtime
recursion, numeric termination measures, general dependent types, universes,
global theorem search, or proof-constructor synthesis. Live browser search is
source-ordered rather than a joint optimization over several holes. Both one-shot
Z3 selection and live epochs use direct production discovery. One-shot Rainfall
retains the complete structured state graph; live Rainfall retains production,
state, transition, reset, and reuse counts for each transient epoch.
