# Two-state bisimulation fixture

`two-state-bisim.fine` is the executable Fine source fixture.
`two-state-bisim-array.smt2` is its direct SMT-LIB counterpart. Both contain
two labelled transition systems. Each has
two states and alternates between state 0 and state 1. The model-shaped hole
`bisim` has sort `Array Pair Bool`, where `Pair` is the datatype product of a
left state and a right state. It is deliberately not declared as a two-argument
predicate: relation application is always an array `select`.

The assertions use quantified, two-directional bisimulation clauses (including
existential matching states) so this remains an MBQI fixture. The initial pair
`(left-0, right-0)` is required. Label agreement excludes both off-diagonal
pairs, and transition closure then requires `(left-1, right-1)`. Thus every
cell is fixed:

| pair | value |
| --- | --- |
| `(left-0, right-0)` | `true` |
| `(left-0, right-1)` | `false` |
| `(left-1, right-0)` | `false` |
| `(left-1, right-1)` | `true` |

The intended complete witness is the constant-false array with stores of
`true` at the two diagonal pairs:

```smt2
(store
  (store ((as const (Array Pair Bool)) false)
         (pair left-0 right-0) true)
  (pair left-1 right-1) true)
```

Run it directly with Z3:

```console
z3 fine/fixtures/two-state-bisim-array.smt2
```

Run the parsed Fine source through the fork:

```console
fine run fine/fixtures/two-state-bisim.fine
```

The first native synthesis fixture uses Fine's fixed integer-expression
semantics and the ground-instance refutation loop:

```console
fine run fine/fixtures/synth-max.fine
```

It synthesizes the two-argument maximum as a conditional, checks the returned
body against the untouched specification, then parses and reifies the printed
body to the identical same-manager semantic AST. This is a backend regression,
not a claim that maximum synthesis is a useful public feature or that ordinary
Fine solver runs construct programs.

`synth-projection.fine` and `synth-max-three.fine` are anti-hardcoding gates:
the first closes with one core term and no conditional, while the second has a
renamed function and requires three core terms and nested conditionals.

`synth-match-open.fine` is the first source-program interruption boundary. It
fixes an exhaustive `MaybeInt` match, leaves the whole `some` arm as the named
typed hole `?payload`, and keeps the `none` arm as ordinary source. The hole's
integer grammar contains the unmatched integer parameter and the constructor's
integer field. Fine synthesizes `value`, reparses and reifies that arm to exact
same-manager identity, assembles the same match representation used by source,
and refutes the negation of the completed match specification in a fresh query.
Rainfall retains the exact hole span and the verified arm body as one admitted
replacement. `fine-rain-host materialize` applies all such replacements in one
revision transaction. `synth-match-materialized.fine` is the expected result;
its trace has the whole-match verification query but no hole declaration,
candidate enumeration, or synthesis query.

Further per-arm editor machinery is deliberately paused. The materialization
fixture proves source identity and admission, but arithmetic arm completion does
not by itself justify an interactive program-synthesis surface. The next consumer
must be a helper lemma or invariant generated from a genuinely stuck induction.

`check-counterexample.fine` refutes a false subtraction claim and returns the
negative/positive assignment `a = -1, b = 1` as a parseable Fine
`counterexample` witness. The negative value gates literal lifting rather than
allowing the printer to smuggle SMT-LIB syntax into the source. `check-valid.fine`
uses addition over mathematical integers to exercise the separate `unsat` / no
counterexample path.

`check-datatype-counterexample.fine` declares a recursive `Tree` with `leaf`
and field-bearing `node` constructors, forces the model value
`node(7, leaf, leaf)`, and returns that exact constructor tree as parseable Fine
syntax alongside the nullary enum value `marked`.
`check-tuple-counterexample.fine` separately makes a binary tuple an ordinary
check parameter and returns `(7, true)`. Both fixtures gate exact same-manager
round trips rather than merely comparing printed text.

`induction-length.fine` introduces the first structurally recursive Fine
function and the first external induction translation. `length` is an
exhaustive match over a recursive `List`; its only self-call consumes the
pattern-bound tail. `inducts(xs)` asks Fine to generate weak direct-subterm
induction for non-negativity. With that line removed, the same Z3 fork remains
in recursive unfolding beyond two seconds; with it present, the query closes
immediately. Its Rainfall trace retains the generated theorem separately from
the later `case-def`, `recfun-num-rounds`, theory, and clause traffic.

`predicate-context-induction.fine` adds an ordinary assumption and one context
parameter to constructor-generated predicate induction. Its `SameHeight`
recursive branch must instantiate the universally generalized IH at
`ceiling - 1` to transport `height(before) <= ceiling` to the result. The false
strict-ceiling fixture fails at `root`. Their Rainfall traces distinguish the
source context assumption, constructor-result specialization, generalized IH,
and public branch result.

`predicate-preservation.fine` is the first theorem whose assumptions and goal
both contain a second constructor-generated predicate. The `under` branch must
invert one `Marked(succ(succ(before)))` layer, use the `Step` induction
hypothesis, and construct one `Marked(succ(succ(after)))` layer. Rainfall keeps
those three resources distinct. `predicate-preservation-false.fine` asks for an
odd marked result and fails at `root`; it also guards against reintroducing the
unbounded recursive constructor-axiom matching loop.

`predicate-arbitrary-preservation.fine` replaces `Marked.grow`'s ordinary
premise with an arbitrary constrained field. Its singleton `At(value)` view
makes the total field `forall token. token == value -> Marked(token)`, so the
recursive premise visibly depends on the binder. Fine verifies view
availability, retains that universal field inside both one-layer inversion and
construction, and proves the same Step preservation theorem without Horn
lowering `Marked`. The false fixture fails at `root`; the invalid-witness fixture
is rejected before an unavailable field can make a branch vacuous.

`predicate-total-field-preservation.fine` gives the target `Step.under_abs` and
secondary `Marked.under_abs` fields distinct binder identities over the same
`FreshFor` view. The abstraction branch proves a flipped Marked pair by
instantiating Step's total IH at Marked's bound name; the predecessor compiler's
single free-name IH refutes exactly `under_abs`. Rainfall retains the IH template,
availability resource, total universal IH, and both unequal binder handles. The
diagonal false control still fails at `root`.

The fixture prints both Z3's array value and all four selected cells; the cell
listing is the stable extensional expectation even if a Z3 release chooses a
different but equivalent store order.
