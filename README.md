# Fine

Fine is a solver language developed inside a soft fork of Z3. The active
`fine/proof-terms` branch starts a two-level language in which proof evidence is
virtual by construction: proof and value syntax are separate, and the runtime
value representation has no proof case.

The executable core also declares recursive runtime enums and eliminates them
with exhaustive typed matches. Separate `proof inductive` declarations form
static indexed constructor evidence without creating runtime proof datatypes. It
checks structural induction over that evidence with explicit `inducts(...)`
clauses and exact recursive constructor fields. It
forms identity proofs, absorbs their propositions into Z3 contexts, declares
caller-local proof coeffects, and fills typed holes from finite exact grammars.
Identity holes admit local evidence, reflexivity, and bounded proof applications;
indexed holes currently admit exact locals and structurally valid induction-
hypothesis applications. Materialization
replaces the holes and writes explicit coeffect arguments, then reparses and
reruns with both searches forbidden.

Parsing is lossless without making whitespace semantic. Fine retains every
token, comment, and whitespace byte in a concrete syntax tree while the
existing AST remains the semantic view. Source materialization edits named
concrete ranges and is checked by an exact parse/render roundtrip.

Bounded identity search can now stop at a source-level checkpoint rather than
discarding unfinished work. `fine checkpoint --proof-budget n` ranks typed
partial trees, materializes checked subtrees around residual `?` leaves, and
reparses without allowing an open proof to enter the solver context. Running it
again resumes those nested holes.

See [`fine/README.md`](fine/README.md) to run it,
[`fine/ARCHITECTURE.md`](fine/ARCHITECTURE.md) for the current boundary, and
[`fine/ROADMAP.md`](fine/ROADMAP.md) for the ordered slices. The
former Bool-predicate and locally nameless implementation is preserved at tag
`pre-pat-1d7222a23`. Z3's original README is retained as
[`README-Z3.md`](README-Z3.md).
