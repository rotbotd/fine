# Two-level proof-term cut

The runnable Bool-predicate implementation is preserved at the annotated tag
`pre-pat-1d7222a23`. This branch does not port it incrementally. The old
frontend made every expression a Z3 value or formula; this branch starts from a
static proof level whose inhabitants can never enter the runtime-value
representation.

## Preserved substrate

- the Z3 soft fork and generic observer patches;
- manager-local strong Z3 terms and exact `reify(lift(x)) = x` validation;
- immutable source snapshots, byte spans, and Rainfall's event envelope;
- Rainfall validation, projection, generation, and host transaction modules;
- the Nix/CMake build and public Git ancestry.

## Replaced core

- the old parser AST and expression-first elaborator;
- the monolithic runtime and its predicate, synthesis, and bisimulation
  consumers;
- every old executable fixture and install check.

The first core has two disjoint representations. `ValueTerm` contains the only
objects that can reach execution or models. `ProofEvidence` exists only while
elaborating and checking. There is deliberately no sum type with value and proof
cases and no proof case in `ValueTerm`.

An identity proof remains exact source evidence. Introducing it automatically
adds its equality proposition to the local solver context. A function `takes`
identity or indexed evidence from its caller; a call resolves that coeffect from
exact lexical proof evidence and may materialize the chosen proof as an explicit
`using` argument. The argument is checked again but never becomes a runtime
argument.

An indexed coeffect can be matched in a value function only as a compile-time
reduction. Fine asks which declared constructors remain satisfiable under the
current identity constraints, requires exactly one, and elaborates only that
arm. A constructor value binder may enter the residual expression only when it
is recovered from a runtime family index. Thus `Tagged(value)` can return its
`tagged(field)` binder when the constructor result equates `field` with `value`;
`Hidden()` cannot return a constructor-only integer. Neither case adds a proof
variant to `ValueTerm`.

## Quarantined consumers

Bool-valued `predicate`, fixedpoint membership, compiler-owned predicate
induction, old `check`, arithmetic synthesis, and bisimulation are absent from
the new target. Their implementations remain inspectable at the preserved tag.
A later inductive proposition must introduce derivation terms from birth; it
must not reinterpret the old Bool relation as if it had always carried them.

Value-function verification has regained one model consumer without weakening
this quarantine. A satisfiable negated guarantee produces completed typed input
and result values, not proof evidence. Fine lifts, prints, reparses, and exactly
reifies those values, then reruns the original guarantee under the fixed inputs
and declared coeffects before exposing the counterexample. No solver proof is
turned into a Fine proof term.

## Admission test

The first slice must:

1. form `refl(x) : Id(A, x, x)` without constructing a runtime proof value;
2. absorb identity evidence into a Z3 context and use it to verify a function;
3. declare a coeffect, resolve it from exact caller-local evidence, and
   materialize the resolution as parseable Fine source;
4. reject a missing coeffect and any attempt to use a proof name as an `Int`;
5. retain formation, absorption, demand, resolution, and use separately in
   Rainfall while exact-validating every Z3 proposition term.

No predicate or general proposition syntax may be added merely to make the
example look larger.

## Second admission test

The first source proof search is deliberately smaller than coeffect resolution:
one typed identity hole, one finite grammar. It must:

1. exclude mismatched local evidence before enumeration;
2. choose exact local evidence before an applicable `refl` candidate;
3. synthesize `refl(left)` when no exact local evidence exists and both endpoints
   elaborate to exact same-manager identity;
4. fail with an empty typed grammar when neither production applies;
5. retain every typed candidate, selection, and residual frontier by explicit
   event reference in Rainfall;
6. replace the source hole, reparse, and rerun with all proof and coeffect search
   forbidden.

## Named proof-function slice

A declaration such as

```fine
proof function symm(left: Bool, right: Bool)
  takes [given: Id(Bool, left, right)]
  -> Id(Bool, right, left);
```

has static value indices, virtual proof parameters, and a virtual result. Fine
introduces symbolic indices, absorbs the parameter propositions, and refutes the
negated result before admitting the declaration. There is no runtime function
and value expressions reject calls to it.

Proof functions use ordinary value arguments and the same coeffect boundary as
runtime functions: `symm(left, right)` searches exact caller-local evidence,
while `symm(left, right) using [given = p]` supplies it explicitly. Proof
constructors use the same surface distinction: actual value or proof parameters
are positional, while `takes` introduces an omitted or explicitly named
coeffect. A hole searches backward from its exact expected
identity type. It binds index parameters from a matching result, recursively
fills only the instantiated proof parameters, and admits application trees only
within cost three. The frontier order remains exact local evidence, applicable
`refl`, then proof functions in source order. Rainfall retains the application
function, ordered index arguments, child proof sources, cost, selected event,
and residual alternatives.

`identity-symmetry.fine` first constructs the non-definitional but valid Boolean
identity `x = (x == true)` using a checked zero-premise proof function. Reversing
it cannot use the local proof or `refl`; search selects
`symm(x, x == true) using [given = p]`. The materialized application reparses and reruns with
search forbidden. An input-preserving cyclic proof function with no base proof
exhausts the cost bound.

`identity-transitivity.fine` closes the missing-index case. Its declaration has
indices `(left, middle, right)` but the requested result mentions only `left` and
`right`. Search matches direct index occurrences in the proof-parameter types
against exact local evidence,
uses those constraints to recover `middle`, and only then recursively enumerates
both instantiated child types. The sole cost-three result is
`trans(left, middle, right) using [first = p, second = q]`. Rainfall preserves the three ordered indices
and the two child sources as separate arrays; the unrelated reflexive proof
`wrong` never enters the tree. `reject-transitivity-gap.fine` removes `q`; the
remaining reconstructible child costs two, making the application cost four,
so the fixed bound rejects it rather than accepting one-sided support.

This inference is deliberately finite and syntax-owned. An index absent from a
result is learned only by exact matching against lexical proof evidence, never by
mining an SMT proof or guessing a semantically equal pretty-printing.

## Bounded datatype-model selector

`--proof-selector z3` does not construct the deterministic candidate-tree
frontier. Starting from the requested identity type, Fine discovers applicable
local evidence, reflexivity, and instantiated proof-function productions, then
constructs exact bounded datatype states directly. Local evidence and
reflexivity become nullary constructors; each ground named proof-function
application becomes a constructor whose fields have the exact strictly cheaper
state sorts required by its proof arguments. The state sort itself fixes carrier,
endpoint AST IDs, cost, completeness, closed frontier, and open leaves; no
recursive scoring or typing function remains for Z3 to unfold.

For `identity-transitivity.fine`, the complete bounded grammar contains every
applicable production reachable within cost three, including irrelevant routes.
Its preferred root nevertheless assigns the hole `(apply-trans local-p local-q)`.
`ProofLift` maps constructor identities—not printed guesses—to
`trans(left, middle, right) using [first = p, second = q]`. Fine then
replaces the hole, reparses the complete document, and reruns with search
forbidden.

One-shot Rainfall retains the complete direct grammar as structured productions
and typed state transitions. Replay checks every transition's types, strict cost
decrease, score recurrence, and selected root; its residual is the state graph
with the lifted tree selected out. Live iterative search uses the same state
constructor but records counts rather than every transient graph. Budgets one
through four remain checked byte-for-byte against the separate enumerated oracle.
Its context-bound selector canonically orders the production set. When the set is
unchanged at the next cost, existing state sorts remain live and only newly
reachable higher-cost states are declared; production growth resets the state
family rather than pretending an immutable datatype can gain alternatives.

Several identity holes are searched in source order rather than as one joint
grammar. Each queued live model carries the completed concrete edits preceding
its hole. After lifting, Fine applies those edits and the current term to the
original concrete tree and reparses that cumulative source. Thus a later partial
view cannot silently restore an earlier hole.

## Static indexed-constructor admission

Ordinary runtime `enum` and static `proof inductive` deliberately have different
destinations. A runtime enum becomes one native Z3 datatype and may be constructed
or matched by value code. A proof inductive declaration contributes only an
indexed source constructor table. Its value indices elaborate to ordinary Z3
terms, but its inhabitants remain `ProofEvidence` and cannot enter execution.

The first family is `Even(value: Nat)`. `even_zero()` has exact result
`Even(zero)`. `even_next(previous: Nat)` requires virtual evidence
`Even(previous)` and has exact result `Even(succ(succ(previous)))`. Applying it
as `even_next(zero)` searches exact local evidence for its `prior` coeffect;
`even_next(zero) using [prior = zero_even]` makes the same choice explicitly.
The value argument, recursive proof demand, and result index are checked by
manager-local AST identity. A semantic equality at a different term identity is
not silently accepted.

This distinction is structural. Constructor parameters written in the ordinary
parameter list are positional, including proof-typed parameters when a
constructor must retain that particular proof. Parameters written in `takes`
are proof-irrelevant demands: their satisfying proof is not a child of the
constructed term. A match binds positional parameters from its pattern and
places every taken proof in the arm scope under the coeffect's declared name.

This is not a hidden Bool predicate or a Z3 runtime proof datatype.

## Indexed proof-family elimination

A proof function may have a checked proof body. `match evidence` accepts only
indexed-family evidence and produces another proof; it cannot produce runtime
data. Patterns bind actual constructor parameters in one ordinary positional
list. Taken coeffects need no pattern slot and reappear under their declaration
names:

```fine
proof function even_pred(value: Nat)
  takes [evidence: Even(succ(succ(value)))]
  -> Even(value) {
  match evidence {
    even_next(previous) => prior,
  }
}
```

Fine first unifies each constructor result with the scrutinee's indices. A
symbolic proof-function index is refined to that result inside the arm. In
`even_pred`, only `even_next` can produce the scrutinee type; matching its result
refines `previous` to `value`, so the local `prior : Even(previous)` has the
required result type `Even(value)`. The exact branch environment, not a solver
lemma, owns this substitution and binder.

Only reachable constructors count toward exhaustiveness. `match impossible {}`
eliminates evidence of a zero-constructor family into any proof type. The same
empty match is accepted for `Even(succ(zero))`, since neither constructor result
can unify with that index. Writing an unreachable arm is rejected rather than
asking its body to prove nonsense.

## Structural induction hypotheses

A body-bearing proof function may declare exactly which indexed proof parameter
it structurally follows:

```fine
proof inductive Plus(a: Nat, b: Nat, c: Nat) {
  plus_zero(base: Nat) -> Plus(zero, base, base);
  plus_succ(a: Nat, b: Nat, c: Nat)
    takes [rest: Plus(a, b, c)]
    -> Plus(succ(a), b, succ(c));
}

proof function plus_shift(a: Nat, b: Nat, c: Nat)
  takes [evidence: Plus(a, b, c)]
  inducts(evidence)
  -> Plus(a, succ(b), succ(c)) {
  match evidence {
    plus_zero(base) => plus_zero(succ(base)),
    plus_succ(pa, pb, pc) =>
      plus_succ(pa, succ(pb), succ(pc)) using [rest = plus_shift(pa, pb, pc)],
  }
}
```

The recursive spelling does not denote a runtime call. During the match, Fine's
constructor table establishes that `rest` is a structurally smaller piece of
`evidence`; the omitted coeffect in `plus_shift(pa, pb, pc)` resolves to `rest`,
so the call is an application of the checked induction hypothesis.
Materialization writes `plus_shift(pa, pb, pc) using [evidence = rest]`. The
selected evidence must be an exact named descendant. Neither an arbitrary proof
expression nor the original `evidence` can pass the descent check.

The descent metadata is per field, not per constructor. A branching constructor
with `left_grows` and `right_grows` yields two separate induction-hypothesis uses
under the same parent, and a target constructor may require both recursive
results. Fine never replaces that pair with one marginal fact merely because the
indices or result families coincide.

This first termination rule deliberately covers proof-family structure only.
Recursion over runtime enums and user-supplied numeric measures remain absent.

## Indexed proof holes

`?` may inhabit an indexed family through two productions: exact lexical
evidence, then a structurally admitted use of the active induction hypothesis.
The second production first matches direct value-index occurrences in the proof
function result against the expected family indices. It then searches exact
locals for every instantiated proof parameter and requires the designated
argument to carry the function's structural root. The cost is one application
plus its proof arguments under the same total bound of three.

`proof-inductive-holes.fine` places the hole at `Rebuilt(previous)` in the
recursive `Even` arm. `wrong: Rebuilt(succ(previous))` is absent by exact index
identity, while `prior: Even(previous)` permits the sole IH candidate
`rebuild(previous) using [evidence = prior]`. A second run-level hole selects exact local
`zero_even`. Rainfall records each grammar, candidate, selection, and complete
residual frontier. Materialization produces the explicit calls and reruns with
proof search forbidden.

This is not constructor search. It also does not feed the indexed frontier into
the current Z3 datatype-model selector; asking for that selector fails explicitly.

## Static evidence selecting a runtime value

A value function may demand indexed evidence and match it only when no runtime
proof inspection survives. Fine tests each family constructor under the current
absorbed equalities. One feasible constructor selects one source arm at compile
time. A value field used by that arm must be structurally recoverable from an
ordinary runtime family index; a unique but unindexed hidden field is still
erased and cannot enter the value expression.

An indexed coeffect also contributes the disjunction of its possible outer
constructor heads to the lexical SMT context, existentially hiding constructor
value parameters. This is only a necessary condition: recursive proof fields are
not treated as automatically inhabited. When that head cover is inconsistent,
zero arms eliminate the impossible evidence into the expected value type. The
stage transfer records the result as bottom, not as an arbitrary runtime value.
Zero arms against a reachable constructor are rejected.

This remains declaration-time checking. Fine does not yet retain a symbolic
proof match for later call-site specialization, normalize proof applications to
constructor structure, or recover arbitrary hidden fields from a solver model.
