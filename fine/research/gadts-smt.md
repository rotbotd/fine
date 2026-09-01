# GADTs at Fine's Z3 boundary

## The question

Z3's current parametric datatype API is not the obstacle to ordinary parametric
ADTs. This checkout exposes `Z3_mk_type_variable`,
`Z3_mk_polymorphic_datatype`, and `Z3_mk_datatype_sort`; its checked-in
`src/test/parametric_datatype.cpp` constructs a datatype over `alpha` and
`beta`, instantiates it at `Int` and `Bool`, and obtains the specialized
constructor and accessor declarations. Fine should use that API for uniform
families such as `List<A>` and `Either<A, B>`.

A GADT asks for something else: a constructor selects or constrains the index of
its result. In

```fine
index Expr<Result> {
  integer(value: Int): Expr<Int>,
  less(left: Expr<Int>, right: Expr<Int>): Expr<Bool>,
  if_<A>(condition: Expr<Bool>, yes: Expr<A>, no: Expr<A>): Expr<A>,
}
```

`integer` must not be an inhabitant of `Expr<Bool>`. A Z3 parametric datatype
schema gives every constructor a result at the schema's same parameters. It can
specialize field and accessor sorts when the family is instantiated, but it
cannot make one constructor absent at one argument or give that constructor a
non-uniform result argument. This is the actual boundary.

## What the literature separates

OutsideIn(X) identifies the source-language operation precisely. Matching a
GADT constructor introduces existential variables and *local given type
equalities*; obligations in the branch are wanted constraints under those
givens. Those equalities do not escape the branch. They also destroy principal
types in otherwise ordinary examples. Fine already requires declaration-level
signatures and has no local implicit generalization, so it should borrow the
local given/wanted discipline without importing GHC's inference problem or type
classes. See Vytiniotis, Peyton Jones, Schrijvers, and Sulzmann,
[OutsideIn(X)](https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/jfp-outsidein.pdf),
especially Sections 2.2, 4.1, and 5.

The semantic issue is stronger than carrying an equality token. Sieczkowski,
Stepanenko, Sterling, and Birkedal show that GADT reasoning depends on which type
constructors are injective and which distinct heads are discriminable; their
non-macro-expressibility result cuts against the slogan that GADTs are merely
existentials plus equality. Fine must therefore define its index equality and
head-disjointness at the compiler boundary. It must not ask an uninterpreted SMT
function to behave injectively without an explicit theory. See
[The Essence of Generalized Algebraic Data Types](https://iris-project.org/pdfs/2024-popl-gadt.pdf),
Sections 1–3.

Exhaustiveness is a separate search problem, not a corollary of branch typing.
Garrigue and Le Normand show that GADT inhabitedness can encode Horn-clause proof
search and machine execution; complete exhaustiveness is undecidable, and even
small wildcard expansion can be exponential. Fine must not market an unbounded
SMT query as a complete exhaustiveness checker. A bounded proof search or an
explicit impossible arm can leave useful evidence in Rainfall, but failure to
prove emptiness is not a counterexample. See
[GADTs and Exhaustiveness: Looking for the Impossible](https://www.math.nagoya-u.ac.jp/~garrigue/papers/gadtspm.pdf).

F* demonstrates the more general alternative: encode source types, typing, and
refinements into a first-order SMT universe, use semantic SMT checks for
unreachable match cases, and control quantifier instantiation with patterns and
fuel. That route supports indexed inductives far beyond ML GADTs, but it is the
architecture Fine explicitly declined when it removed a second type universe.
It also inherits the trigger-loop and fuel boundary that Rainfall would have to
show honestly. See the F* documentation on
[inductive pattern matching](https://fstar-lang.org/tutorial/book/part1/part1_inductives.html)
and [its Z3 encoding](https://fstar-lang.org/tutorial/book/under_the_hood/uth_smt.html),
and Swamy et al.,
[Dependent Types and Multi-monadic Effects in F*](https://fstar-lang.org/papers/mumon/paper.pdf).

## Two truthful encodings for Fine

### 1. Closed-index specialization

For every closed index demanded by the program, construct a distinct Z3 datatype
sort and define all mutually reachable index instances together. `Expr<Int>` and
`Expr<Bool>` are then genuinely different Z3 sorts. `integer` exists only in the
former, `less` only in the latter, while `if_` is specialized once per demanded
index. This preserves the v1 rule that every Fine value type is exactly a Z3
sort, gives model construction the right domain without side constraints, and
keeps context-free lift honest.

It is not a general implementation of GADTs. An open index variable denotes a
potentially unbounded family; non-uniform recursion can generate new
instantiations; separately compiled code cannot know the closed set; and the
locally nameless STLC object language has a recursive universe of object types.
Specialization is nevertheless a useful first experiment because it exposes
all constructor/result plumbing without changing Fine's type invariant.

### 2. Erased carrier plus an index invariant

Use one ordinary Z3 datatype `RawExpr`, one first-order datatype of index codes,
and a compiler-generated `index : RawExpr -> TypeCode` or `HasType` relation.
The source type `Expr<I>` elaborates to the carrier sort plus the invariant
`index(x) = I`. Constructor creation and solver grammars include their generated
index equations. A GADT pattern adds the constructor's index equation as a local
given; branch obligations are checked under it.

This handles open indices and makes typed synthesis a first-order SMT problem,
but `Expr<Int>` and `Expr<Bool>` are no longer distinct Z3 sorts. An unconstrained
raw model value may be ill-indexed. More seriously for Fine, a bare internal Z3
term no longer determines the indexed surface type: ordinary resugaring needs a
context or retained typing evidence. The generated-term core can still lift the
raw term exactly, but the stronger user-surface `lift(x): Expr<I>` must name the
admitted `index(x)=I` evidence. Adopting this encoding therefore revises, rather
than implements, the one-sort-per-Fine-type invariant.

Do not hide this choice behind the word "monomorphization." Native Z3 parametric
ADTs solve uniform parameters. Specialization and indexed refinements are only
needed for the constructor-specific result indices that make a GADT a GADT.

## Recommended experiment before a language decision

Build one isolated nested flake or spike, not a surface feature, with the three
constructors above and two backends: closed-index Z3 sorts and erased carrier
plus `index`. Require the same four observations from each:

1. `integer(1)` cannot inhabit `Expr<Bool>` in a raw solver model, not merely in
   Fine's source typechecker.
2. Matching `Expr<Bool>` introduces the exact local equality needed by `less`
   and excludes `integer` with explicit evidence.
3. A synthesis grammar for `Expr<Bool>` cannot enumerate an ill-indexed raw
   constructor and filter it only after selection.
4. Every returned constructor term reparses/reifies to exact same-manager AST
   identity, and Rainfall can state whether its indexed surface type came from
   the Z3 sort itself or from a separately admitted constraint.

The deciding observation is the third. Fine exists to let Z3 construct programs;
a frontend-only GADT check that leaves the solver enumerating ill-typed erased
terms is ornamental. If the erased encoding can make the grammar intrinsically
index-directed without turning all typing into F*'s quantified `HasType`
universe, it earns the invariant change. Otherwise the honest scope is
closed-index specialization, with general GADTs deferred beyond v1.
