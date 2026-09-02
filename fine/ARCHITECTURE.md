# Fine two-level core

Fine is a solver language inside a soft fork of Z3. The pre-proof-term language
is preserved at Git tag `pre-pat-1d7222a23`. This branch begins again at the
parser and elaborator because its central distinction must be structural:
proofs are static evidence and cannot become runtime values.

## Two representations, not one tagged union

The source syntax has separate `ValueType` and `ProofType` nodes. The elaborator
has separate `ValueTerm` and `ProofEvidence` structures. `ValueTerm` is closed
over the value kinds which may reach execution and models; the first slice has
`Int` and `Bool`. `ProofEvidence` contains an identity type, source ownership,
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

## Contextual proof demand

A function declares evidence required from its caller:

```fine
function replace(left: Int, right: Int) -> Int
  needs [same: Id(Int, left, right)]
  ensures { result == right; }
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

`fine materialize` applies those insertions, reparses the resulting bytes, and
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

## Rainfall boundary

The existing manager-local term registry and `fine.generated-term.v1` renderer
survive the cut. Every absorbed identity proposition is a strong Z3 term and is
reparsed/reified to exact AST identity before run closure. Proof formation,
context absorption, coeffect declaration, demand instantiation, caller
resolution, and callee use are separate events. The run begins with an explicit
`proof.erasure.boundary` event and closes with `runtime_proof_values: 0`.

A proof hole separately records `proof.search.open`, every well-typed
`proof.search.candidate`, the exact candidate selected, and the unchosen finite
frontier at `proof.search.close`. Candidate event IDs, rather than callback
order, connect the selection and close. Replay validation checks that the
selected candidate plus the residual list exactly exhaust the enumerated
frontier and that every opened proof hole closes.

A proof source node is not falsely attached to a Z3 proof term. Z3 receives the
proposition which the source proof licenses; Fine retains the proof's own static
identity.

## Quarantine

The previous Bool-valued predicates, fixedpoint membership, predicate induction,
ordinary checks, synthesis runtime, and bisimulation runtime are not linked into
this target. Their exact implementation and fixtures remain at the preserved
tag. A future inductive proposition must introduce derivation inhabitants from
birth and may only use an erased predicate relation as a backend shadow.

## Current limits

Only `Int`, `Bool`, identity, reflexivity, proof aliases, typed identity holes,
straight-line functions, guarantees, lexical coeffects, lets, and assertions
are present.
There are no ordinary datatypes, inductive propositions, proof matches,
identity composition, named proof functions, general dependent types, universes,
or inductive proof constructor synthesis yet.
