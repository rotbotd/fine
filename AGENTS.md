# Fine development rules

Fine is a soft fork of Z3. The primary agent owns architecture, naming,
invariants, integration, and review. Subagents receive bounded assignments;
they may return evidence or small patches, but must not redesign the language.
Never run more than three subagents concurrently.

## Non-negotiable v1 invariants

- Every value-bearing Fine type is a Z3 sort. Fine has no arrow type and no
  second type universe.
- Named functions are declarations, not values. There are no closures,
  partial application, higher-order arguments, typeclasses, theories, or
  instance/model resolution.
- A solver-produced finite function is an array over a finite enumerable index
  sort. A relation over `A` and `B` is an array from the product `(A, B)` to
  `Bool`; source application is syntax over `select`.
- `reify : Fine syntax -> Z3 term` and `lift : Z3 term -> Fine syntax` obey
  `reify(lift(x)) = x` on the admitted term domain. Do not replace AST identity
  with semantic equality.
- `lift(reify(y))` may normalize Fine syntax and must be idempotent.
- Trace events retain interned Z3 AST identities. Never synthesize identity
  from pretty-printed text.
- “model” means an actual Z3 model or model-shaped hole.

The first runnable artifact is a two-state finite bisimulation whose relation
is returned from a model-shaped hole and printed in Fine syntax. Keep h's
provenance in documentation.
