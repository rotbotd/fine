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
adds its equality proposition to the local solver context. A function `needs`
identity evidence from its caller; a call resolves that coeffect from exact
lexical proof evidence and may materialize the chosen proof as an explicit
`using` argument. The argument is checked again but never becomes a runtime
argument.

## Quarantined consumers

Bool-valued `predicate`, fixedpoint membership, compiler-owned predicate
induction, old `check`, arithmetic synthesis, and bisimulation are absent from
the new target. Their implementations remain inspectable at the preserved tag.
A later inductive proposition must introduce derivation terms from birth; it
must not reinterpret the old Bool relation as if it had always carried them.

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
  needs [given: Id(Bool, left, right)]
  -> Id(Bool, right, left);
```

has static value indices, virtual proof parameters, and a virtual result. Fine
introduces symbolic indices, absorbs the parameter propositions, and refutes the
negated result before admitting the declaration. There is no runtime function
and value expressions reject calls to it.

Explicit proof application keeps indices and evidence visibly separate as
`symm[left, right](given)`. A hole searches backward from its exact expected
identity type. It binds index parameters from a matching result, recursively
fills only the instantiated proof parameters, and admits application trees only
within cost three. The frontier order remains exact local evidence, applicable
`refl`, then proof functions in source order. Rainfall retains the application
function, ordered index arguments, child proof sources, cost, selected event,
and residual alternatives.

`identity-symmetry.fine` first constructs the non-definitional but valid Boolean
identity `x = (x == true)` using a checked zero-premise proof function. Reversing
it cannot use the local proof or `refl`; search selects
`symm[x, x == true](p)`. The materialized application reparses and reruns with
search forbidden. An input-preserving cyclic proof function with no base proof
exhausts the cost bound.

`identity-transitivity.fine` closes the missing-index case. Its declaration has
indices `(left, middle, right)` but the requested result mentions only `left` and
`right`. Search matches direct index occurrences in the proof-parameter types
against exact local evidence,
uses those constraints to recover `middle`, and only then recursively enumerates
both instantiated child types. The sole cost-three result is
`trans[left, middle, right](p, q)`. Rainfall preserves the three ordered indices
and the two child sources as separate arrays; the unrelated reflexive proof
`wrong` never enters the tree. `reject-transitivity-gap.fine` removes `q`; the
remaining reconstructible child costs two, making the application cost four,
so the fixed bound rejects it rather than accepting one-sided support.

This inference is deliberately finite and syntax-owned. An index absent from a
result is learned only by exact matching against lexical proof evidence, never by
mining an SMT proof or guessing a semantically equal pretty-printing.

## Bounded datatype-model selector

`--proof-selector z3` uses Z3 only after deterministic enumeration has produced
the complete typed frontier. Fine walks those candidate trees and compacts their
distinct ground productions into one recursive datatype. Local evidence and
reflexivity become nullary constructors; each ground named proof-function
application becomes a constructor with one recursive field per proof argument.
Recursive `carrier`, `src`, `dst`, `cost`, and `well` functions enforce the same
exact child types and total cost bound as the enumerator.

For `identity-transitivity.fine`, the compact grammar is precisely:

```text
apply:trans[left, middle, right]/2
local:p
local:q
```

Z3 assigns the hole `(apply-trans local-p local-q)`. `ProofLift` maps constructor
identities—not printed guesses—to `trans[left, middle, right](p, q)`. Fine then
requires that source and cost to match an exact deterministic candidate before
requesting materialization. `fine materialize --proof-selector z3` replaces the
hole, reparses the complete document, and reruns with search forbidden.

This is deliberately not yet a faster replacement for enumeration: the
deterministic frontier still supplies the reference grammar and residual list.
The slice establishes the ownership, model, lift, and recheck boundaries before
any attempt to generate a larger grammar without enumerating all complete trees.

## Static indexed-constructor admission

Ordinary runtime `enum` and static `proof inductive` deliberately have different
destinations. A runtime enum becomes one native Z3 datatype and may be constructed
or matched by value code. A proof inductive declaration contributes only an
indexed source constructor table. Its value indices elaborate to ordinary Z3
terms, but its inhabitants remain `ProofEvidence` and cannot enter execution.

The first family is `Even(value: Nat)`. `even_zero()` has exact result
`Even(zero)`. `even_next(previous: Nat)` requires virtual evidence
`Even(previous)` and has exact result `Even(succ(succ(previous)))`. Applying it
as `even_next[zero](zero_even)` checks the static argument, recursive proof field,
and result index by manager-local AST identity. A semantic equality at a
different term identity is not silently accepted.

This is not a hidden Bool predicate or a Z3 runtime proof datatype.

## Indexed proof-family elimination

A proof function may have a checked proof body. `match evidence` accepts only
indexed-family evidence and produces another proof; it cannot produce runtime
data. Patterns keep static value binders in brackets and virtual proof fields in
parentheses, matching constructor application syntax:

```fine
match evidence {
  even_zero() => shape_zero[value](refl(value)),
  even_next[previous](prior) =>
    shape_next[value, previous](refl(value), prior),
}
```

Fine first unifies each constructor result with the scrutinee's indices. A
symbolic proof-function index is refined to that result inside the arm. Thus the
base arm checks `refl(value) : Id(Nat, value, zero)`, while the recursive arm
checks `refl(value) : Id(Nat, value, succ(succ(previous)))`; `prior` is a usable
local `Even(previous)` proof. The exact branch environment, not a solver lemma,
owns these substitutions and binders.

Only reachable constructors count toward exhaustiveness. `match impossible {}`
eliminates evidence of a zero-constructor family into any proof type. The same
empty match is accepted for `Even(succ(zero))`, since neither constructor result
can unify with that index. Writing an unreachable arm is rejected rather than
asking its body to prove nonsense.
