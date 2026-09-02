# Z3 proof-term synthesis probe

This probe separates three things that are easy to call "proof-term
synthesis" even though they have different ownership and guarantees:

1. Z3's native proof object for an unsatisfiable formula;
2. a model value of a Fine-owned algebraic datatype of source proofs; and
3. a universally correct function from recursive proofs to recursive proofs.

Run from anywhere inside the Fine development shell:

```sh
fine/spikes/proof-term-synthesis/run.sh
```

The two ground model queries deliberately make the expected source proof small
and bounded. The first has one lexical equality proof and a symmetry
constructor. The second has two lexical proofs, symmetry, transitivity, an
exact recursive well-formedness predicate, and a cost bound. A returned model
is therefore an inspectable candidate in Fine's grammar, not a Z3 proof object.

The universal query asks Z3 to synthesize one function that reverses every
well-formed recursive equality proof. The default solver times out under the
five-second bound, while the `qsat` tactic rejects the uninterpreted candidate
function. These are negative experiments, not completeness claims: they
demonstrate that the ground model construction must not be advertised as an
induction engine.

The generated `probe` binary is ignored by the repository and can be removed
without affecting any product build.

The runner also checks the exact ground witnesses, the different native proof
operator sets for uninterpreted and integer equality, and the bounded
universal-query result. A solver change which crosses one of those boundaries
therefore makes this spike fail visibly instead of silently changing the design
evidence.
