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
body to the identical same-manager semantic AST.

`synth-projection.fine` and `synth-max-three.fine` are anti-hardcoding gates:
the first closes with one core term and no conditional, while the second has a
renamed function and requires three core terms and nested conditionals.

The fixture prints both Z3's array value and all four selected cells; the cell
listing is the stable extensional expectation even if a Z3 release chooses a
different but equivalent store order.
