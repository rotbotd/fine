# Can Z3 synthesize Fine proof terms?

## Verdict

Z3 can do two adjacent jobs, but only one is a promising Fine backend.

- With proof production enabled, an unsatisfiable solver call returns a Z3
  proof object. This object explains solver inferences. It is not a term in
  Fine's proof language.
- If Fine defines a recursive datatype whose constructors are exactly the
  admitted source-proof grammar, plus an exact checking interpreter and a
  finite cost bound, Z3 can return a ground inhabitant as a model value. Fine
  can lift that constructor tree into source, reparse it, and check it normally.
- Plain Z3 does not provide a native SyGuS command or an induction method that
  turns arbitrary recursive propositions into source derivations. Universal
  proof-function synthesis crosses that boundary quickly.

The usable result is therefore bounded datatype-valued model synthesis behind
Fine's existing typed grammar. Native proof extraction must remain solver
evidence, never the source-term authority.

## What was inspected

The local soft fork exposes `solver::proof()` and the `Z3_OP_PR_*` family of
low-level inference nodes. It also exposes recursive datatypes, recursive
functions, models, fixedpoint answers, and the QSAT tactic. A full source scan
found no `synth-fun`, `synth_fun`, `SyGuS`, or `sygus` implementation in
`src`, `examples`, or `doc`.

The official proof-log guide says Z3 proof terms are low-level inference rules.
The newer logs contain big-step hints, and some hints require a general SMT
call rather than a small syntactic validator. The official datatype guide says
the ground recursive-datatype procedure does not lift to induction and that Z3
has no method for producing proofs by induction. These are the two decisive
limits for Fine.

The Reynolds--Deters--Kuncak--Barrett--Tinelli synthesis construction is still
directly useful as a design pattern: encode a syntax grammar as an algebraic
datatype, define its evaluation/checking relation, and use the datatype solver
to drive synthesis. Their implementation was in CVC4, not Z3, so it is evidence
for an encoding Fine can own rather than a hidden Z3 facility to enable.

Sources:

- [Z3 guide: inference logs and proofs](https://microsoft.github.io/z3guide/programming/Proof%20Logs/)
- [Z3 guide: datatypes and the induction limit](https://microsoft.github.io/z3guide/docs/theories/Datatypes/)
- [Programming Z3](https://theory.stanford.edu/~nikolaj/programmingz3.html)
- [Counterexample Guided Quantifier Instantiation for Synthesis in SMT](https://theory.stanford.edu/~barrett/pubs/RDK%2B15-abstract.html)

## Reproducible observations

The probe is in `fine/spikes/proof-term-synthesis`.

### Native proof objects are real and source-hostile

For asserted `a` and `not a`, Z3 returns the ordinary solver proof

```text
(unit-resolution (asserted a) (asserted (not a)) false)
```

For `left = right` and `not (right = left)` over an uninterpreted sort, the
proof contains a native `symm` node. This is tempting to map to a Fine
`symm(p)`, but it is not a stable source representation. Changing the carrier
to `Int` makes preprocessing route the same mathematical argument through
arithmetic normalization, rewrite, transitivity, monotonicity, and inequality
reasoning. The source proof should not depend on which theory owns equality or
which preprocessing path happened to run.

Native proof mining may later be useful as a whitelisted hint source. Any such
hint must refer back to unambiguous source evidence and still pass the ordinary
Fine elaborator. It cannot define the materialized term.

### Ground datatype-valued synthesis works

The first query declares

```text
SrcProof := local-p | refl(Int) | apply-symm(SrcProof)
```

with `local-p : Id(Int, 1, 2)`, recursive endpoint and cost functions, and an
unknown `hole : SrcProof`. Asking for endpoints `2, 1` at cost at most two
returns:

```text
(define-fun hole () SrcProof
  (apply-symm local-p))
```

The second query adds `local-12`, `local-23`, transitivity, recursive
well-formedness, and a cost bound. Asking for a proof from `3` to `1` returns a
constructor tree assembled from the lexical leaves and named applications.
This is genuine bounded ground proof-term synthesis, with one important
qualification: Z3 returns a model value because Fine supplied all syntax,
typing, scoping, and bounds.

### A universal recursive proof function does not follow

The last query declares an unknown function

```text
synth-symm : SrcProof -> SrcProof
```

and universally requires it to reverse the endpoints of every well-formed
recursive input proof. The default solver returns `unknown` by timeout at five
seconds. The specialized `qsat` tactic also returns `unknown`, immediately,
because the formula contains the uninterpreted candidate function. Increasing
fuel would not turn this into a language invariant: Z3's own datatype
documentation explicitly separates ground solving from induction.

## Fine integration boundary

The narrow implementation worth testing has three owned parts:

1. `ProofGrammar`: a datatype generated from the exact expected proof type,
   exact lexical proof IDs, and applicable named proof functions;
2. `ProofCheck`: constraints that reproduce Fine's elaborator and reject every
   ill-typed tree, plus an explicit depth or cost bound;
3. `ProofLift`: model-tree recovery followed by ordinary Fine parse/elaborate,
   with Rainfall retaining the grammar, bound, chosen model value, and final
   check as separate events.

The first vertical slice is the existing symmetry target: with
`p : Id(Int, x, y)` in scope and a named proof function `symm`, a hole expecting
`Id(Int, y, x)` should admit only `symm(p)`. This can be started after named
proof functions exist. The current deterministic enumerator remains the
reference semantics and the source of a complete residual frontier. A Z3 model
backend may choose a candidate from that grammar, but it must not silently
replace the grammar or claim a complete search frontier.

Spacer remains outside this path. A safe Spacer answer is a sufficient
invariant and can project away constructor support; prior Fine experiments
already demonstrated that loss. Compiler-owned induction and proof
materialization remain necessary for recursive propositions.

## Integrated result

The first product slice now lives in `src/fine/proof_model_selector.cpp`. Rather
than reimplementing Fine typing inside an unrelated solver grammar, it compacts
the already enumerated typed candidate trees into ground recursive productions.
The datatype interpreter tracks carrier token, exact endpoint AST IDs, child
requirements, well-formedness, and total cost. The returned constructor AST is
lifted structurally and must match one exact source-and-cost pair in the
deterministic frontier.

On `identity-transitivity.fine`, Z3 returns
`(apply-trans local-p local-q)` and Fine lifts it to
`trans[left, middle, right](p, q)`. `fine materialize --proof-selector z3`
reparses and rechecks that complete source with search forbidden. Rainfall
records the three boundary objects separately. This is a semantic integration
result, not yet a performance result: complete deterministic enumeration still
precedes compaction, and future work may only remove that prerequisite if it
preserves the same exact grammar and frontier claims.
