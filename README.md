# Fine

Fine is a solver language developed inside a soft fork of Z3. The active
`fine/proof-terms` branch starts a two-level language in which proof evidence is
virtual by construction: proof and value syntax are separate, and the runtime
value representation has no proof case.

The first executable slice forms identity proofs, absorbs their propositions
into Z3 contexts, declares function coeffects which demand identity evidence
from callers, resolves those demands from exact lexical proof evidence, and
materializes the chosen proof as explicit parseable source.

See [`fine/README.md`](fine/README.md) to run it,
[`fine/ARCHITECTURE.md`](fine/ARCHITECTURE.md) for the current boundary, and
[`fine/PROOF_TERMS.md`](fine/PROOF_TERMS.md) for the preserved/deleted map. The
former Bool-predicate and locally nameless implementation is preserved at tag
`pre-pat-1d7222a23`. Z3's original README is retained as
[`README-Z3.md`](README-Z3.md).
