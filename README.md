# Fine

Fine is a solver language developed inside a soft fork of Z3. The active
`fine/proof-terms` branch starts a two-level language in which proof evidence is
virtual by construction: proof and value syntax are separate, and the runtime
value representation has no proof case.

The executable core forms identity proofs, absorbs their propositions into Z3
contexts, declares caller-local proof coeffects, and fills typed identity holes
from a finite grammar of exact local evidence and reflexivity. Materialization
replaces the holes and writes explicit coeffect arguments, then reparses and
reruns with both searches forbidden.

See [`fine/README.md`](fine/README.md) to run it,
[`fine/ARCHITECTURE.md`](fine/ARCHITECTURE.md) for the current boundary, and
[`fine/ROADMAP.md`](fine/ROADMAP.md) for the ordered slices. The
former Bool-predicate and locally nameless implementation is preserved at tag
`pre-pat-1d7222a23`. Z3's original README is retained as
[`README-Z3.md`](README-Z3.md).
