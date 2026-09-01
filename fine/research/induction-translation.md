# Induction as Fine-to-SMT translation

Fine's first induction slice is based on three full-paper reads and one current
solver account:

- K. Rustan M. Leino, *Automating Induction with an SMT Solver* (2011),
  https://www.microsoft.com/en-us/research/wp-content/uploads/2016/12/krml218.pdf
- Andrew Reynolds and Viktor Kuncak, *Induction for SMT Solvers* (VMCAI 2015),
  https://lara.epfl.ch/~kuncak/papers/ReynoldsKuncak15InductionSMTSolvers.pdf
- Nada Amin, K. Rustan M. Leino, and Tiark Rompf, *Computing with an SMT
  Solver* (2014), https://namin.seas.harvard.edu/pubs/tap-dafny.pdf
- cvc5, *Induction and Conjecture Generation in cvc5* (2024),
  https://cvc5.github.io/blog/2024/10/17/induction-and-conjecture-generation-in-cvc5.html

Leino's useful observation is that an SMT solver needs no native induction
operation. A frontend can replace `forall n. P(n)` with

```text
forall n. ((forall k. k < n -> P(k)) -> P(n))
```

for a frontend-owned well-founded order, prove this obligation, and then admit
the original theorem. Dafny chooses variables that prominently occur in
decreasing positions of recursive calls, allows an explicit override, and
reported solving 45 of the first 47 IsaPlanner problems with this small tactic.

Reynolds and Kuncak derive the dual least-counterexample form. Refuting
`forall x. P(x)` introduces a minimal counterexample `k`, so the search context
contains both `not P(k)` and `forall x. R(x,k) -> P(x)`. For an algebraic
datatype, weak `R` can mean “is a direct recursive field of” and needs no
transitive subterm relation. Their CVC4 implementation moves the transform into
skolemization to choose induction lazily, apply it to quantifiers created during
search, try variable orders, and synthesize equality subgoals from live DPLL(T)
state. Those are real advantages, but none is required for Fine's first closed
source-to-SMT loop.

Recursive function unfolding is a separate problem. Amin, Leino, and Rompf add
a structural fuel argument to recursive function axioms, with triggers that
unfold only nonzero fuel. Separate low-priority computation axioms recognize
literal-marked arguments and may unfold without consuming fuel. Fine currently
uses Z3's native recursive-function registration and admits only syntactically
structural self-calls; it has not yet adopted the paper's explicit fuel
translation. This is adequate for the length induction but not a general claim
about predictable recursive evaluation.

## Implemented boundary

For a check with arbitrary constants for its source parameters, Fine constructs
`P = assumptions -> guarantees`. If `inducts(x)` names a recursive datatype,
Fine enumerates every direct self-typed constructor field to construct `R`, then
asks Z3 to refute

```text
(forall smaller. R(smaller, x) -> P[x := smaller]) -> P.
```

The query disables MBQI and leaves E-matching enabled. Rainfall records the
function definition registration, the compiler-owned induction transformation,
the generated source-to-step edge, accepted E-matching instances if any survive
preprocessing, and the public post-preprocessing clause stream. In the length
fixture Z3 eliminates the singleton direct-field quantifier during preprocessing;
the honest trace therefore contains ground assumption clauses for
`length(tail(xs)) >= 0`, not a fictional accepted quantifier instance. Later
clauses visibly contain recursive-function `case-def` and `recfun-num-rounds`
terms. They are query-scoped observations, not a causal proof reconstruction.

## STLC exit beyond this slice

The target is preservation for call-by-value STLC in a locally nameless
representation with cofinite quantification. The next experiment must decide
from an executable encoding whether the induction object should be the term,
typing derivation, or reduction derivation. Fine still lacks inductive relations,
finite atom sets, opening and substitution definitions, cofinite premises,
inversion, and helper-lemma invocation. A larger syntax mockup would conceal
precisely which of those is forcing the proof, so the representation is not yet
declared.
