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

## One universe of value types

Fine v1 has no first-class functions and no compiler-owned arrow type. Every
value type is a Z3 sort. Named functions and predicates are declarations with
domain sorts and a result sort, and cannot be stored or passed.

Model-produced finite maps use Z3 arrays. A relation on finite sorts `A` and
`B` is `Array(Product(A, B), Bool)`. Fine may spell selection as `r(a, b)`, but
reification constructs `select(r, pair(a, b))`.

When a model supplies a finite map, Fine enumerates the complete finite domain,
constructs a canonical constant-array-plus-stores Z3 term, then lifts that term.
The model's `FuncInterp` is evidence used to construct the term; it is not the
term covered by the round-trip law.

## Surface retained from Latte

Use braced expression blocks, explicit call parentheses, named arguments,
enums, vertical matches, and `proof` / `takes` / `gives`. Use `let` rather than
Latte's web-oriented `const`. Do not copy Latte's typeclass machinery. This is
the surface direction, not a claim that every form is implemented: constructor
calls exist, while vertical `match` is still absent.

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

## Counterexample boundary

`check name(parameters) { assumes { ... } ensures { ... } }` turns a source
claim into one satisfiability query: the assumptions are conjoined with the
negation of the guarantees. `unsat` means there is no counterexample in the
admitted value domain. `sat` is not collapsed to a diagnostic string: Fine
completes every parameter under that returned model and emits a typed,
parseable `counterexample name { ... }` declaration.

The round-trip law applies separately to each assignment. Ints, Bools, finite
enums, binary tuples, and monomorphic algebraic datatypes lift to ordinary Fine
syntax; parsing and elaborating the result in the same manager must recover the
exact AST. Negative integers remain numerals. An enum declaration containing a
field-bearing constructor lowers directly to one Z3 datatype; its constructor
fields may refer directly to the enclosing type, so recursive trees are real
Z3 constructor terms rather than tagged arrays. Constructor calls are
positional even though declarations name their fields.

This datatype slice has no projection syntax, patterns, exhaustiveness check,
mutual recursion, parametric types, indexed constructors, codata, or datatype
synthesis. Recursive self-reference is admitted only as a direct field type;
other referenced types must already be declared. Pure nullary enums retain the
finite enumeration path used by bisimulation. The witness declaration itself
is a source envelope and is deliberately not executable. Checks still have no
quantifiers, arrays, shrinking, or claim that the returned model is a minimal
counterexample.

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
resulting `z3.clause.assume`, `z3.clause.infer`, and `z3.clause.delete` events
cover this admitted clause stream, not rejected candidate clauses, assignments,
decisions, watched-literal traffic, the auxiliary MBQI context, or the causal
contribution of a clause to the final answer. Clause literals may be internal
post-preprocessing Z3 terms, so their representation is labelled accordingly
rather than falsely printing them as Fine source.
