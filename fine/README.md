# Fine proof-term branch

Fine is a solver language developed inside a soft fork of Z3. Its active core is
two-level by construction: runtime values and static proof evidence have separate
syntax and separate C++ representations, so a proof cannot leak into executable
data and there is no erasure pass that can forget to remove one.

This is the checked program shipped as the browser playground's default:

<!-- checked-example: playground-demo -->
```fine
enum Nat {
  zero,
  succ(Nat),
}

function predecessor(value: Nat) -> Nat {
  match value {
    zero => zero,
    succ(previous) => previous,
  }
}

proof inductive Even(value: Nat) {
  even_zero() -> Even(zero);
  even_next(previous: Nat)
    takes [prior: Even(previous)]
    -> Even(succ(succ(previous)));
}

proof function even_pred(value: Nat)
  takes [evidence: Even(succ(succ(value)))]
  -> Even(value) {
  match evidence {
    even_next(previous) => prior,
  }
}

proof inductive Plus(a: Nat, b: Nat, c: Nat) {
  plus_zero(base: Nat) -> Plus(zero, base, base);
  plus_succ(a: Nat, b: Nat, c: Nat)
    takes [rest: Plus(a, b, c)]
    -> Plus(succ(a), b, succ(c));
}

proof function plus_shift(a: Nat, b: Nat, c: Nat)
  takes [evidence: Plus(a, b, c)]
  inducts(evidence)
  -> Plus(a, succ(b), succ(c)) {
  match evidence {
    plus_zero(base) => plus_zero(succ(base)),
    plus_succ(pa, pb, pc) =>
      plus_succ(pa, succ(pb), succ(pc)) using [rest = plus_shift(pa, pb, pc)],
  }
}

proof function bool_eta(value: Bool)
  -> Id(Bool, value, value == true);

proof function symm(left: Bool, right: Bool)
  takes [given: Id(Bool, left, right)]
  -> Id(Bool, right, left);

proof function trans(left: Bool, middle: Bool, right: Bool)
  takes [first: Id(Bool, left, middle), second: Id(Bool, middle, right)]
  -> Id(Bool, left, right);

run playground {
  let one: Nat = succ(zero);
  assert predecessor(one) == zero;

  proof zero_even: Even(zero) = even_zero();
  proof two_even: Even(succ(succ(zero))) = even_next(zero);
  proof recovered: Even(zero) = even_pred(zero);
  proof zero_plus_zero: Plus(zero, zero, zero) = plus_zero(zero);
  proof shifted: Plus(zero, succ(zero), succ(zero)) = plus_shift(zero, zero, zero);

  let right: Bool = true;
  let middle: Bool = right == true;
  let left: Bool = middle == true;
  proof base_left: Id(Bool, middle, left) = bool_eta(middle);
  proof p: Id(Bool, left, middle) = symm(middle, left);
  proof base_right: Id(Bool, right, middle) = bool_eta(right);
  proof q: Id(Bool, middle, right) = symm(right, middle);
  proof composed: Id(Bool, left, right) = ?;
}
```
<!-- /checked-example -->

The program exercises the current boundary rather than an early identity-only
slice. `Nat` is a native Z3 runtime datatype and `predecessor` eliminates it as
ordinary data. `Even` and `Plus` are static indexed families: matching their
evidence refines indices, and `plus_shift` may call itself only with the exact
recursive field exposed beneath its `inducts(evidence)` root. Constructor and
function parameters in `takes` are proof-irrelevant coeffects. Fine selects exact
caller-local evidence when they are omitted; `using` records the same choice
explicitly.

The final identity hole has a finite typed grammar. Exact locals come first,
then applicable `refl`, then backward result-directed proof-function applications
up to the configured total cost. With `--proof-selector z3`, Fine instead
discovers the applicable typed productions directly and constructs exact
datatype states indexed by type, cost, completeness, and residual frontier.
The complete state graph—not a list of candidate trees—is retained in Rainfall.
Z3 selects one term, Fine lifts it by constructor identity, then the materialized
source is reparsed and checked normally. Here the selected source is:

```text
composed ← trans(left, middle, right) using [first = p, second = q]
```

`fine materialize` replaces the hole and every implicit coeffect choice through
lossless concrete ranges, reparses the result, and reruns it with both searches
forbidden. Comments, whitespace, line endings, and unrelated source bytes remain
exact.

## Run it

The repository flake owns the toolchain and the local Z3 fork:

```sh
nix run . -- run --proof-selector z3 fine/fixtures/playground-demo.fine
nix run . -- rain --proof-selector z3 fine/fixtures/playground-demo.fine > trace.jsonl
nix run . -- materialize --proof-selector z3 \
  fine/fixtures/playground-demo.fine > explicit.fine
nix run . -- run explicit.fine
```

For an existing development build, replace `nix run . --` with
`build/proof-core/fine`. `fine roundtrip file.fine` exposes the exact concrete
parse/render identity check.

## Typed counterexamples

A value function is checked by asking whether its guarantees can be false under
its declared coeffects. When that query is satisfiable, Fine now completes every
input and the result in the model, lifts those values to ordinary Fine literals
or enum constructors, prints and reparses them, and requires exact same-manager
AST identity. It then fixes the lifted inputs in a fresh query and checks that
the original guarantee is impossible. Only after both checks does it print a
returned witness such as:

```text
counterexample erase {
  value: Nat = succ(zero);
  result: Nat = zero;
}
parse(print(lift(values))): exact ast identity
```

`counterexample` is deliberately not an executable declaration. It reports the
model of a failed function check; it does not add an assumption to a later run.
The returned grammar admits `Bool`, arbitrary integer numerals including
negative literals, and recursively constructed values of every declared runtime
enum. Rainfall retains each model evaluation, the checked source witness, the
fresh refutation check, and a distinct counterexample terminal event.

## Search and checkpoints

An ordinary proof hole is closed only by a complete typed candidate. `fine
checkpoint --proof-budget n` instead admits one typed open production lifting to
`?`, prefers complete roots, and otherwise ranks partial trees by closed frontier
before constructor cost. It materializes only the checked source tree. An open
proof is never inserted into the proof environment or absorbed into SMT, so no
later statement is checked under unfinished evidence. A later checkpoint pass
resumes the residual holes from the emitted source.

Indexed-family holes have a deliberately smaller grammar: exact local evidence
and structurally admitted induction-hypothesis applications. Wrong indices and
nondecreasing roots are absent before enumeration. Constructor synthesis and the
Z3 model selector do not yet apply to indexed holes.

## Browser playground

`https://fine.shit.yachts` runs the same C++ executable and local Z3 fork as an
11 MiB WebAssembly module. CodeMirror is only an editor and lexical highlighter;
it is not a second parser. `materialize holes` installs the CLI's exact source as
one transaction, so one undo restores the pre-action document.

Checkpoint search uses a dedicated Web Worker. On cross-origin-isolated clients,
a pthread producer runs open-ended iterative deepening while a separate Fine
module drains translated terms from a bounded shared-memory ring, reparses and
rechecks each full-source view, and retains only validated snapshots. Stop kills
the producer before installing the last validated source. The ordinary
single-threaded module remains the fallback. Multiple holes are searched in
source order. Every published view carries the exact completed edits from earlier
holes plus the current partial or complete term, so installing a later-hole view
cannot discard an earlier result. Each live epoch discovers its applicable
instantiated productions directly and builds exact bounded datatype states; it
does not enumerate concrete candidate trees before Z3 selection. Once that
production set stops growing, the next epoch retains the existing state sorts
and adds only new higher-cost states.

```sh
nix build --no-link --print-out-paths .#playground
nix run .#playground-service
# open http://127.0.0.1:4174
```

The playground package is separate from the Wasm packages, so frontend-only edits
do not rebuild Z3. Its smoke test runs the shipped sample through the Z3 selector,
checks the chosen source term, and separately covers exact CST materialization,
undo, partial/complete/settled checkpoint epochs, pthread shared memory, and the
COOP/COEP response headers.

## Boundaries and records

[`ARCHITECTURE.md`](ARCHITECTURE.md) states the current semantic ownership and
runtime/proof boundary. [`PROOF_TERMS.md`](PROOF_TERMS.md) explains the design
cut from Bool predicates to proof evidence. [`ROADMAP.md`](ROADMAP.md) records the
ordered executable slices. Failed routes, exact commands, and closed decisions
remain in the append-only root [`LOG.md`](../LOG.md).

Every `fine` code block in these four current documents must be a literal excerpt
of a passing fixture. The install check rejects invented placeholder syntax even
when it looks plausible enough to survive prose review.

The former Bool-predicate, fixedpoint, and locally nameless implementation remains
at tag `pre-pat-1d7222a23`; it is evidence, not compatibility surface.
