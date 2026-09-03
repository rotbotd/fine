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
enum Nat { zero, succ(Nat) }

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

`even_next[zero](zero_even)` checks its explicit static index, its virtual proof
field, and the exact result index. Fine creates no corresponding runtime Z3
datatype and does not expose `Even(value)` as a runtime Bool. A proof constructor
therefore cannot occur in a value expression. This is the ATS split at the
current boundary: values carry the runtime data; indices and evidence constrain
it statically.

Proof functions with bodies eliminate indexed evidence by proof-level matching:

```fine
proof function expose_even(value: Nat)
  takes [evidence: Even(value)]
  -> EvenShape(value) {
  match evidence {
    even_zero() => shape_zero[value](refl(value)),
    even_next[previous](prior) =>
      shape_next[value, previous](refl(value), prior),
  }
}
```

Constructor-result unification happens before an arm is checked. The first arm
refines `value` to `zero`; the second refines it to
`succ(succ(previous))` and introduces both `previous` and `prior`. The two
`refl(value)` terms therefore check at different branch-local identity types.
This is definitional index refinement, not an equality proposition guessed by
Z3.

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

`--proof-selector z3` changes only selection. Fine still computes the complete
typed deterministic frontier first. It compacts the ground productions found
in those trees into a recursive datatype whose checking functions retain the
carrier, exact endpoint AST IDs, child types, and total cost. Z3 returns a ground
constructor tree; Fine lifts it to source and requires the source and cost to
name one exact reference candidate. The model cannot add a production, change a
type, or define the residual frontier.

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

Model selection adds `proof.model.grammar`, `proof.model.solve`, and
`proof.model.lift` as separate events. Replay requires the grammar to cite the
complete reference frontier, the solve to cite that grammar, the lift to cite a
typed candidate, and the later selection to use exactly that candidate.

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

Only `Int`, `Bool`, identity, reflexivity, proof aliases, checked named proof
functions, bounded typed identity holes, straight-line functions, guarantees,
lexical coeffects, lets, and assertions are present.
There are no ordinary datatypes, inductive propositions, proof matches,
identity transitivity search, general dependent types, universes, or inductive
proof constructor synthesis yet.
