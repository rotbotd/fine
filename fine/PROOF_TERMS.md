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
function, child proof sources, cost, selected event, and residual alternatives.

`identity-symmetry.fine` first constructs the non-definitional but valid Boolean
identity `x = (x == true)` using a checked zero-premise proof function. Reversing
it cannot use the local proof or `refl`; search selects
`symm[x, x == true](p)`. The materialized application reparses and reruns with
search forbidden. An input-preserving cyclic proof function with no base proof
exhausts the cost bound.

Transitivity is still withheld. Its middle index does not occur in the result,
so backward search must learn it from two proof arguments rather than pretending
all indices can be read from the goal.
