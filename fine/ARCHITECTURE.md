# Fine v1 architecture

Fine began with an idea from h: stop treating Z3 as a one-way proof-obligation
sink. Fine reuses Z3's interned terms as its logical substrate, lifts solver
results back into source syntax, and exposes the solver's internal activity as
replayable “rainfall.” h handed the project to Lynn on 2026-08-31 and permitted
selective reuse of the Rust-like Latte surface syntax.

## The retraction

For admitted Z3 terms `x` and Fine syntax `y`:

```
reify(lift(x)) = x
normalize(y)   = lift(reify(y))
```

The first law is exact Z3 AST identity, not equivalence after simplification.
It makes `normalize` idempotent. A Z3 rewrite `x -> x'` can therefore be shown
as the Fine rewrite `lift(x) -> lift(x')` without parsing printed SMT-LIB.

Rainfall applies this law to every term admitted by its registry, including
post-preprocessing clause literals and proof hints. `fine.generated-term.v1` is
the total, declaration-bound core rendering: applications name exact function
declarations, numerals name their sorts, and quantifiers retain original binder
symbols/sorts, weight, qid/skid, patterns, and no-patterns. The emitted text is
parsed against the retained manager-local sort/declaration bindings and reified
to exact AST identity. Creating ASTs inside Z3's observer callbacks can cancel a
query, so callbacks only print and retain a strong term; `term.lift.validate`
performs the exact round trip after the solver returns and before run closure.
The validator requires exactly one such admission for every declared term.

This generated core is Fine syntax but not a claim that an internal term was
written by the user. `origin` independently records `semantic-z3`, clause
literal, or proof-hint provenance, and only compiler-known correspondences get
source edges. `z3_text_diagnostic` is retained for debugging but is neither an
admitted rendering nor used by projection. Ordinary source resugaring and
materialization sit above this exact layer.

## One universe of value types

Fine v1 has no first-class functions and no compiler-owned arrow type. Every
value type is a Z3 sort. Named functions and predicates are declarations with
domain sorts and a result sort, and cannot be stored or passed.

Model-produced finite maps use Z3 arrays. A relation on finite sorts `A` and
`B` is `Array(Product(A, B), Bool)`. Fine may spell selection as `r(a, b)`, but
reification constructs `select(r, pair(a, b))`.

This rule governs values, not erased proofs. Fine will not copy F*'s SMT
encoding in which source values and source types share one universal `Term`
sort and `HasType` axioms recover their distinctions. An object-language
datatype named `Tm` or `Ty` remains a literal, separate native Z3 datatype
sort. `Ty` is object data, not a code for Fine's own type universe.

## Indexed proof families

The first indexed-family feature is proof-only, following ATS's separation of
static indexed proofs from program values. A declaration such as
`Step(before: Tm, after: Tm)` may construct, assume, and eliminate total proof
witnesses during checking, but introduces no Z3 value sort for those witnesses.
Its indices are values of existing native Fine/Z3 sorts; constructor result
indices elaborate to equalities over those sorts. Proof erasure therefore does
not erase or box `Tm`, `Ty`, names, environments, integers, or other ordinary
values.

The compiler owns the strictly-positive inductive interpretation and the
indexed elimination/induction rule. It may lower a proof family internally to
an inductive predicate or constrained Horn clauses, but Fine exposes neither a
raw Horn-clause language nor an arbitrary user-defined object-language
semantics. This lowering is valid for ordinary first-order recursive premises
because the least relation excludes index pairs produced by no constructor.
Constructor introduction implications asserted to the ordinary SMT solver do
not have that property and are not an admissible replacement.

The implemented Horn-complete surface is `proof family F(indices) { constructor
C(parameters) { takes { atoms; } gives F(result_indices); } }`. Each family is
registered as a Z3 fixedpoint relation over the native index sorts and each
first-order constructor becomes one universally closed Horn rule. Constructor parameters
must occur in the result indices; this deliberately excludes premise-only
variables. A parameterless, assumption-free `check` containing exactly one
ground family atom is a public least-relation membership query. It reports
`derived` or `not-derived`; it is not routed through the ordinary counterexample
solver and produces no runtime proof value. Family atoms nested inside ordinary
formulas, open membership queries, proof elimination, and derivation induction
outside the explicit `inducts` form are not admitted yet.
Ground membership and fixedpoint invariant queries are rejected for a family
with any compiler-owned arbitrary field. Fine does not register any of that
family's constructors with fixedpoint: an incomplete least relation would be a
different object from the source family even if public queries were blocked.

A second, still proof-erased consumer admits a parameterized `check` with
exactly one assumed family atom and ordinary Fine guarantees. The compiler
registers a fresh nullary counterexample relation and adds the universally
closed rule `family(indices) && !guarantees -> counterexample`; reachability is
refutation and unreachability verifies the invariant over the least relation.
This is not an ordinary-SMT implication, and it does not turn the family into an
uninterpreted predicate. It also is not source derivation induction: there are
no constructor branches, refined indices, recursive proof hypotheses, or lifted
proof witnesses. A satisfiable answer is retained exactly by Rainfall but is
reported only as fixedpoint reachability until its concrete index tuple can be
lifted honestly.

Explicit first-order derivation induction uses
`inducts(F(index_parameters))`. The target must be identical to the sole family
atom in `assumes`, with one check parameter per family index in declaration
order. Fine enumerates the retained constructors. For each branch it substitutes
the exact result indices into the guarantee, makes one guarantee-shaped induction
hypothesis at every recursive premise's indices, and asks ordinary SMT whether
the conjunction of those hypotheses with the negated branch goal is satisfiable.
Every constructor branch must be unsatisfiable; constructors with premises from
another family are rejected by this first slice.

An arbitrary constrained field has the source form
`arbitrary x: View(arguments) { recursive_atoms; }` inside a constructor's
`takes` block. It is retained as a proof-only function field and is never put in
the Horn body. During induction, `x` is a distinct free same-manager carrier
term scoped to that field, the instantiated view proposition is an assumption,
and every scoped recursive atom yields its own exact induction hypothesis. Fine
also separately verifies that the view is inhabited for every constructor
parameter assignment; otherwise the branch would close vacuously. Without a
declared view witness this is the direct `forall parameters. exists x.
View(arguments, x)` obligation. With one, Fine checks `forall parameters.
View(arguments, witness(arguments))`, which constructively entails availability.
The arbitrary branch still uses its independent free `x`, never the witness.
This is a compiler-owned induction rule, not a fixedpoint interpretation of the
arbitrary field.

The proof witness erases after branch construction rather than becoming a
runtime GADT. Rainfall retains the constructor result, result-specialized query,
and each exact recursive-premise/induction-hypothesis pair as compiler-owned
evidence. This does not yet provide an explicit source match body, existential
constructor fields, a full locally nameless cofinite/equivariance proof, or a
typed branch counterexample.

A universally quantified constructor field stays explicit in the proof-family
IR. In particular, putting the bound name only in a Horn rule body turns
`forall fresh names` into a search for one working name. Fine must not perform
that translation. Fine now retains the arbitrary carrier, its constrained-view
requirement, scoped recursive premise, availability obligation, and induction
hypothesis without Horn lowering. The remaining locally nameless slice must put
the real opening and support operations into those objects and retain the
freshness/equivariance argument that makes the source cofinite constructor
sound. This is an elaboration of the general family mechanism, not a primitive
`cofinite` solver operation. The polarity countertests remain in
`fine/spikes/indexed-proof`.

Erasure occurs only at the value/model boundary. Rainfall must first retain the
constructor branch, exact native index terms, recursive premise, and induction
hypothesis as compiler-owned evidence. A proof that disappears from model data
must not disappear from the explanation of why an SMT branch was admitted.
Runtime indexed datatypes and a general second universe remain out of scope.

## Constrained views

Fine may name an erased proposition carried by an existing native value type.
The intended surface is:

```fine
view FreshFor(left: Tm, right: Tm) over Name {
  requires {
    fresh(value, support(left, right));
  }
  witness fresh_after(support(left, right));
}
```

`x: FreshFor(body, body_prime)` is still represented by the native Z3 `Name`
sort. The view adds no wrapper value or solver sort; it requires erased evidence
for the instantiated `requires` clause. A caller must discharge that obligation,
and the callee receives it among its assumptions. Checking is by entailment, not
by looking for a textually identical expression in an `assumes` block. Rainfall
records the exact assumption or derived clause that discharged each view
obligation. The first implemented use site is the proof-only
`arbitrary x: FreshFor(...) { ... }` field: its proposition is supplied as an
induction-branch resource only after Fine proves the view is inhabited for every
constructor-parameter assignment. Ordinary function parameters with view types
and caller-side entailment discharge remain unimplemented.

A view may name one carrier-valued `witness` expression over its parameters.
This does not select the value used by an arbitrary branch. It only replaces a
difficult quantified availability search with a concrete, separately checked
inhabitant. Rainfall retains the declared and instantiated witness, the
requirement it must satisfy, and the later arbitrary binder as distinct terms.
An invalid witness rejects the proof branch rather than weakening the view.

Views are deliberately weaker than a general dependent type universe. They
refine one existing native carrier with named erased propositions; they do not
change runtime representation, quantify over Fine types, synthesize operations,
or make functions first-class. The cofinite `Step` field uses a proof-only
function from a `FreshFor` name to a recursive proof. Both the view evidence and
the proof function erase at the value/model boundary.

When a model supplies a finite map, Fine enumerates the complete finite domain,
constructs a canonical constant-array-plus-stores Z3 term, then lifts that term.
The model's `FuncInterp` is evidence used to construct the term; it is not the
term covered by the round-trip law.

## Surface retained from Latte

Use braced expression blocks, explicit call parentheses, named arguments,
enums, vertical matches, and `proof` / `takes` / `gives`. Use `let` rather than
Latte's web-oriented `const`. Do not copy Latte's typeclass machinery. Vertical
`match` is currently declaration-level only: recursive `function` and partial
`synth` declarations have it, but arbitrary expressions do not.

## First vertical slice

1. Declare a two-constructor state datatype and a finite transition relation.
2. Create an array-valued model hole for a candidate bisimulation relation.
3. Assert the two directional bisimulation clauses and the distinguished pair.
4. Ask Z3 for a model, extensionalize all four Boolean cells into one canonical
   array term, lift it, and print it in Fine syntax.
5. Render a typed `model` witness, parse it with Fine's ordinary parser,
   elaborate its table expression in the same context, and assert pointer
   identity with the canonical Z3 term inside the same `ast_manager`.

Rainfall follows this closed loop; it does not delay the first model return.

## Parsed vertical slice

The next closed loop accepts the same bisimulation as a Fine source file. Its
surface is deliberately narrower than its runtime substrate:

```fine
enum LeftState { left_0, left_1 }
enum RightState { right_0, right_1 }

let left_step: Table((LeftState, LeftState), Bool) =
  table(default: false) {
    (left_0, left_1): true,
    (left_1, left_0): true,
  };

let right_step: Table((RightState, RightState), Bool) =
  table(default: false) {
    (right_0, right_1): true,
    (right_1, right_0): true,
  };

let left_label: Table(LeftState, Bool) = table(default: false) {
  left_1: true,
};

let right_label: Table(RightState, Bool) = table(default: false) {
  right_1: true,
};

model bisim: Table((LeftState, RightState), Bool);

proof bisimulation {
  takes(
    relation: bisim,
    left_step: left_step,
    right_step: right_step,
    left_label: left_label,
    right_label: right_label,
    initial: (left_0, right_0),
  );
  gives(bisim);
}
```

`Table` is Z3 `Array`, including the product index sort; it is not a
compiler-owned function type. `model` introduces exactly one model-shaped
array hole. `proof bisimulation` is a named proof form, not a first-class
function: its named `takes` fields elaborate to the quantified two-directional
bisimulation clauses, and `gives` names the hole to return. The parser produces
syntax containing source spans and no Z3 objects. Elaboration resolves that
syntax against manager-owned enum, product, array, and Boolean sorts.

The parser accepts both a model-shaped hole, `model r: Table(...);`, and a
concrete model witness, `model r: Table(...) = table(...);`. The former is a
solver input; the latter is the parseable output form used to close the source
round trip. Proof/runtime policy, including the current one-hole restriction,
belongs to elaboration rather than the parser.

## Synthesis boundary

Fine has one compiler-owned semantics. It does not accept user-defined object
languages, evaluators, raw Horn clauses, or SemGuS-LIB. Candidate program
syntax and its semantic Z3 term are nevertheless distinct representations:
decoding a selector assignment or proof is not `lift`, and lowering a Fine body
to its Z3 meaning is a one-way compiler operation. See
`research/synthesis-pressure-test.md` for the selected refutation-synthesis
slice, result vocabulary, and provenance obligations.

This is an explicit backend boundary, not a description of every Fine query.
Bisimulation constructs a model value, `check` constructs a counterexample
assignment when satisfiable, and induction refutes a compiler-generated step.
Rainfall makes their solver terms parseable but does not thereby reconstruct a
source program or structured proof. The public QF-LIA `synth` declaration is an
experimental harness. Its useful successor must be justified by a proof task,
currently failure-directed helper-lemma or invariant synthesis for a stuck
induction; maximum synthesis is only a closed plumbing test.

The first interruptible source slice admits an exhaustive datatype match inside
`synth`. A whole arm may be a named hole such as `?payload`; that parse-local
node becomes a snapshot-scoped Rainfall object with expected type `Int` and the
fixed `fine.qf-lia-int.v1` grammar. The grammar inputs are precisely the integer
parameters still in scope plus integer fields bound by that constructor. A
completed arm is elaborated normally. Each open arm is instantiated at its
constructor with fresh field terms, synthesized in a named `synth-arm` scope,
lifted to ordinary arm syntax, and reparsed/reified to the identical semantic
term. The resulting arms are assembled with the same datatype recognizers and
accessors used for a source match; a fresh public query refutes the negation of
the complete specification.

`fine.match-witness` does not merely print the result. It binds each exact hole
range to the body recorded by that arm's verified close event. Replay rejects a
moved range, changed insertion, unknown hole, or missing arm witness.
`fine-rain-host materialize` validates the retained trace against the current
admitted generation and applies all replacements under the host lock as one
ordinary revision advance. The new generation reparses the materialized arms
and performs only whole-match verification: it has no hole or candidate events.
Arm searches are still sequential inside one source generation, so this is not
yet independent per-arm cancellation or residual projection. Those extensions
are paused until a proof-directed synthesis task demonstrates that independently
editable expression holes are the right user boundary.

## External structural induction

Fine does not enable the disabled `smt_induction` source in the Z3 fork. Its
first induction form is a compiler translation over an existing monomorphic Z3
datatype. For a check theorem `P(x)` and the datatype's direct-recursive-field
relation `R`,

```text
forall x. P(x)
```

is reduced to the single step

```text
forall x. (forall smaller. R(smaller, x) -> P(smaller)) -> P(x).
```

The CLI check already represents an arbitrary `x` by a fresh same-manager
constant, so the public counterexample query is the negation of that step. The
well-foundedness argument belongs to Fine: `R` is generated only from recursive
fields of the Z3 datatype, and source recursive calls are accepted only when
the matched argument is one of those fields. Z3 receives no induction command.

This boundary is visible in Rainfall. `check.induction.translate` names the
source parameter, direct-subterm order, theorem, hypothesis, and generated step.
The check declaration has a `generated` edge to that step. Subsequent
`z3.quantifier-instance` and `z3.clause.*` events remain Z3 observations. If
preprocessing turns the direct-field hypothesis directly into ground clauses,
Fine reports those clause assumptions rather than inventing an instantiation
event. Neither chronology nor shared query scope claims that a particular
clause caused unsatisfiability.

`function` is currently narrower than a general match expression: it matches
one named field-bearing-datatype parameter, requires exactly one exhaustive arm
per constructor, forbids shadowing, and has no mutual recursion. Other
parameters may be held fixed during induction. There is no strong/transitive,
lexicographic, integer, nested, or conjecture-generating induction, and no fuel
surface yet. These exclusions matter for the locally nameless STLC target.

## Counterexample boundary

`check name(parameters) { assumes { ... } ensures { ... } }` turns a source
claim into one satisfiability query: the assumptions are conjoined with the
negation of the guarantees. `unsat` means there is no counterexample in the
admitted value domain. `sat` is not collapsed to a diagnostic string: Fine
completes every parameter under that returned model and emits a typed,
parseable `counterexample name { ... }` declaration.

`lemma` uses the same syntax and verification path as `check`, including
constructor/direct-field induction. On an unsatisfiable counterexample query,
Fine universally closes the original source theorem—not its generated induction
step—and retains it under a stable `fine.lemma.<name>` qid. Only then may later
ordinary solver and proof-induction branch queries assert it. Source order is
semantic: lemmas must precede proof-family and executable declarations, and a
refuted lemma terminates execution rather than leaving a missing assumption for
later code. Rainfall records admission separately from each use. Fixedpoint
relations never acquire these theorems as unowned background axioms.

The round-trip law applies separately to each assignment. Ints, Bools, finite
enums, binary tuples, and monomorphic algebraic datatypes lift to ordinary Fine
syntax; parsing and elaborating the result in the same manager must recover the
exact AST. Negative integers remain numerals. An enum declaration containing a
field-bearing constructor lowers directly to one Z3 datatype; its constructor
fields may refer directly to the enclosing type, so recursive trees are real
Z3 constructor terms rather than tagged arrays. Constructor calls are
positional even though declarations name their fields.

This datatype slice has no standalone projection syntax or general match
expression. Recursive functions provide one exhaustive declaration-level match,
but there is no mutual recursion, parametric types, indexed constructors,
codata, or datatype synthesis. Recursive self-reference is admitted only as a
direct field type; other referenced types must already be declared. Pure
nullary enums retain the finite enumeration path used by bisimulation. The
witness declaration itself is a source envelope and is deliberately not
executable. Ordinary checks still have no source quantifiers, arrays, shrinking,
or claim that the returned model is a minimal counterexample.

## Rainfall identity and coverage

Z3's numeric AST IDs are diagnostic labels, not durable identities. Z3 may
recycle an ID after a node dies, and `ast_manager::compress_ids()` can renumber
live nodes. A running rainfall recorder therefore assigns a never-reused handle
inside one named manager and keeps an `ast_ref` / `expr_ref` alive for every
handle. The event schema carries the manager, recorder handle, and diagnostic
AST ID together. A bare AST ID must never be used as a trace key.

The runnable public-boundary slice is `fine rain <source.fine>`. It emits JSONL
with six event kinds—`object`, `scope`, `constraint`, `derive`, `transform`, and
`transition`—and producer-specific operations. Scope nesting and sequence give
location and time, never causality; operations name evidence queries and term
references explicitly. Each term declaration carries identity
`(run, recorder, manager, handle)` and only
`ast_id_at_observation` as a diagnostic. The recorder holds every registered
`z3::expr` strongly until the run ends.

Every CLI rain now begins with a fresh opaque document identity and an immutable
snapshot identity containing revision zero, the exact SHA-256 source hash, and
byte length. Parser-assigned declaration and expression IDs are unique only
inside that snapshot. `source.node.declare` therefore always carries both the
snapshot reference and the parse-local ID; neither a span nor a repeated node ID
is a cross-parse identity. The compiler emits `source.term.evidence` edges for
the original `check` expressions while elaborating them, labelled `exact` for
names and literals or `desugared` for constructors and compound syntax. The
`proof bisimulation` declaration also has four `generated` edges to the exact
fully elaborated clause terms produced from that proof form. Generated witness
parses and post-preprocessing Z3 terms do not inherit those edges.

`fine-rain-validate <source.fine> <rain.jsonl>` replays this boundary without a
solver. It checks the exact source hash and size, one document/snapshot envelope,
contiguous events, parse-local node uniqueness and in-bounds spans, never-reused
live term handles, same-run/recorder/manager term identities, known endpoints for
every source edge, and a terminal run close. It rejects cross-snapshot edges,
unknown or reused handles, manager substitution, source-bearing `internal_z3`
edges, and events arriving after the run closed. This validator does not transport
evidence across edits; a viewer may move a stale decoration separately.
The recognized terminal runs include ordinary checks, proof-family membership
and induction checks, synthesis, and bisimulation. A proof-family trace records
compiler-owned relation/rule declarations and the public fixedpoint query/result, while making
no claim about Spacer's internal rule search.

For Rainfall runs, Fine additionally registers Z3's public fixedpoint callbacks
and enables Spacer's lemma/invariant export gates. `z3.spacer.lemma-export`
retains the exact same-manager lemma term and level; the callback can repeat a
lemma encountered again and never establishes that the lemma caused the final
answer. `z3.spacer.predecessor` and `z3.spacer.unfold` are payload-free boundary
crossings with ordinals only. They do not identify a source constructor, matched
rule, relation, obligation, success, or failure. All three operations remain
independent internal provenance; only compiler-generated family and
counterexample rules receive source correspondence edges.

The two-recursive-premise fixture fixes one additional coverage boundary. The
compiler trace retains the conjunction in the exact constructor rule and counts
both recursive premises. Spacer's exported learned lemma is instead a marginal
invariant guarded by one `Step` application; the public callback does not carry
the pair of rule supports from which it was learned. Fine must therefore build
branch-sensitive derivation evidence from its own constructor translation. It
must not reconstruct a source proof branch by reading conjunction or variable
order back out of learned lemmas, especially after Spacer projection and
variable elimination.

`fine-rain-project` is that deliberately weaker projection layer. It applies
one ordered transaction of non-overlapping UTF-8 insertions/replacements at
byte offsets to the validated source and maps half-open source ranges into the
new bytes. A surviving mapped range is `transported`; a range collapsed by
deletion is `unplaced`. The display snapshot retains the document ID and
advances the revision, but is explicitly `admitted_by_rainfall: false`. The
projection never alters the old claim snapshot and cannot emit `current` after
any transaction, even when the resulting bytes equal the old bytes. A new
validated trace, not range or text continuity, is the only route back to
`current`. The command can emit a standalone HTML table whose stale state is
both written in text and styled separately; color is not the sole distinction.
For a generated bisimulation clause, projection joins its exact assertion handle
to the compiler role, gathers accepted instances carrying that role, and follows
each explicit instance-to-lemma evidence edge. The JSON and collapsed HTML
details show the ground body and binding terms and distinguish acceptance from
lemma admission. This is activity attached to the proof declaration, not a claim
that preprocessed quantifiers or clauses are Fine source syntax, nor an account
of what caused the solver result.

Live generation admission is a separate boundary. A generation request binds an
opaque generation ID to the exact current document/revision/hash/length. The
host passes that generation as Rainfall's run ID through the structured
`fine rain --document ... --revision ... --generation ...` form and retains the
requested source until completion. `fine-rain-generation admit` first validates
the candidate trace against that retained source. It emits a current projection
only if the displayed bytes still equal the request and the candidate run plus
snapshot identity equal it field for field. A completion from a predecessor is
valid historical evidence but is returned whole as `discarded`; its events are
not merged. A request whose display has already advanced is discarded before
any candidate can replace transported markers. Cancellation may save work but
is not relied on for correctness.

`fine-rain-host` realizes the missing transaction as an editor-neutral durable
harness. Its authoritative `state.json` contains the displayed UTF-8 bytes,
their recomputed identity, the current generation, generation records, and the
annotations currently painted. Under one advisory host lock, `advance` applies
the ordered edit, maps every displayed range again, marks the prior pending
generation superseded, writes immutable source/request artifacts, and commits
the new state by fsync plus same-directory rename. Immutable artifacts are
durable before the state can name them; a crash before the rename can leave an
unreferenced artifact but cannot expose half a transaction.

The host releases its lock before running Fine. Completion validation may
therefore race with later edits; it reacquires the lock, reloads the newest
request and bytes, and uses the generation gate above. An admitted completion
replaces the annotation set whole. A late completion is retained as historical
trace plus a discarded record but does not touch displayed annotations. Solver
failure is recorded and leaves transported markers in place. This harness uses
POSIX `flock`, ordinary subprocess execution, and atomic local-filesystem rename;
it is not itself an editor, cross-machine collaboration protocol, or a claim
that cancellation succeeded.

Materialization uses the same transaction machinery rather than writing around
it. `materialize` accepts only the current admitted generation, reloads and
validates its retained trace against the current bytes, requires one match
witness with nonempty verified replacements, and checks that every named range
still contains a hole. It then performs one `_advance_locked` transaction. The
resulting source is deliberately unadmitted until the newly issued generation
finishes; old annotations are transported in the meantime.

`fine-rain-live` is a thin local browser client around this same host. It derives
one minimal UTF-8 byte edit from each submitted textarea value, calls `advance`,
starts the named generation asynchronously, and polls `state.json` through the
locked host API. The browser never admits evidence. During a run it can display
only the host's transported or unplaced predecessor annotations; current
annotations appear only after the ordinary completion gate replaces the set.
The HTTP boundary is loopback by default, accepts edits only as JSON, and rejects
non-loopback browser origins. This is the first editor integration, not an IME,
incremental parsing, or collaboration claim.

For `check`, rainfall records the one source-level refutation assertion, the
public query boundary and polarity, each completed parameter evaluation, and
the checked source witness. Those model assignments are equality under the
returned model. This producer exposes no arithmetic propagation, conflict
analysis, model construction, or other solver-internal search.

The bisimulation producer records all four public assertions, the one
MBQI-only satisfiability boundary, the accepted quantifier instances and clause
stream described below, completed evaluation of every cell in the finite
relation, deterministic extensionalization into an admitted array term, and
the exact source round trip. A completed model value is labelled only as
equality under that returned model. Accepted instances and clauses are evidence
about formulas admitted to the search; they do not by themselves explain which
later search steps caused `sat`.

The soft fork adds a synchronous optional observer to `th_rewriter`. Fine
attaches it only to the ordinary theory rewriter used for native synthesis
postprocessing. Every successful, non-reflexive `reduce_app` step emits a
`z3.theory-rewrite` transform with the application rebuilt from its
already-rewritten children and the wrapper's returned term. The callback runs
after push/pull-ITE handling at that wrapper boundary and records whether the
generic engine will recursively rewrite the result. Both endpoints are
interned in the same manager and enter rainfall's strong registry before the
callback returns.

There is also no honest single “all rewrites” hook. The generic recursive
rewriter commits results along several paths, while solver propagation and
some formula transformations do not pass through it at all. This hook covers
only the observed `th_rewriter_cfg::reduce_app` path; it misses substitutions,
variables, quantifiers, macro expansion, cache reuse, other rewriter
instantiations, and solver propagation. Later producers must be added rather
than calling this partial stream the solver's entire execution.

The bisimulation path uses a separate existing Z3 boundary rather than
pretending the theory-rewriter hook covers solver search. Its query sets
`mbqi=true` and `ematching=false`, assigns stable `fine.bisim.*` qids to the
three universal source clauses, and installs a read-only user-propagator
`on_binding` callback. Z3 invokes that callback in `qi_queue` only after an
instance survives the redundancy checker and rewrite-to-true rejection, and
before the corresponding lemma is inserted. Rainfall records the preprocessed
quantifier, ground instance, and preserved source qid as `z3.mbqi-instance`.
The callback always returns true and therefore never suppresses an instance.

That producer does not expose the auxiliary model-checking query, candidate
countermodels, inverse-term selection, discarded or duplicate candidates,
blocking clauses, or the later CDCL(T) consequences of the accepted instance.
Its MBQI attribution is a property of the query configuration—E-matching is
disabled—not provenance carried by the callback itself. Fresh auxiliary solver
contexts have different AST managers and deliberately do not enter the parent
rainfall registry; only accepted instances reaching the parent `qi_queue` do.

The same query also retains Z3's public `on_clause` registration for the exact
solver lifetime. This callback exposes clauses after preprocessing when they
are assumed by, inferred into, or deleted from CDCL(T). Rainfall classifies the
dummy proof-hint heads `assumption` and `del` only as the public API specifies;
every other hint is conservatively an inference and its uninterpreted head is
preserved. Each literal and proof hint receives a strong recorder handle. The
`inst` proof hint is the one place these two narrow observers meet: it contains
the exact preprocessed quantifier, negated unsimplified ground body, bindings,
and generation used to build the lemma. The recorder pairs its quantifier and
positive body handles with the pending `on_binding` event and consumes that
pair once. The resulting clause event explicitly references the accepted
instance event and ground bindings; adjacency in the stream is never used as
evidence. Replay validation rejects unknown events, changed term handles, and
relations other than `accepted-instance-became-admitted-clause`.

This join proves that one accepted ground instance became one admitted
quantifier lemma. It does not say the lemma caused a propagation, conflict,
model repair, or final result. The resulting `z3.clause.assume`,
`z3.clause.infer`, and `z3.clause.delete` events
cover this admitted clause stream, not rejected candidate clauses, assignments,
decisions, watched-literal traffic, the auxiliary MBQI context, or the causal
contribution of a clause to the final answer. Clause literals may be internal
post-preprocessing Z3 terms, so their representation is labelled accordingly
rather than falsely printing them as Fine source.
