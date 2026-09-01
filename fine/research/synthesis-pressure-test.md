# Synthesis pressure test

This record captures the twelve-angle design review run on 2026-08-31 after
the first parsed bisimulation slice. It is a decision document, not a list of
features inherited from the papers. Fine has one built-in semantics. It is not
a host for user-defined object languages, user-written evaluators, raw Horn
clauses, or SemGuS-LIB.

## 2026-09-01 scope correction

The review selected a technically honest refutation-synthesis slice; it did not
establish that arithmetic program synthesis belongs in Fine's public pitch.
`synth max` and the later materialized match arm remain regression fixtures for
ground-instance selection, independent verification, exact source round trip,
and host admission. Ordinary model, counterexample, and induction runs do not
construct source programs merely because Rainfall lifts their terms.

Further editor machinery for general expression holes is paused. The next test
of this backend must consume a real proof failure: synthesize a helper lemma or
invariant from a retained induction residual, admit it in its own generation,
materialize it, and show the original theorem reruns without enumeration. If
that test does not reuse the public `synth` declaration, the declaration should
be removed while keeping the backend internal.

## The correction found by the review

The first slice originally stopped short of its advertised source round trip.
It lifted the canonical Z3 array into a private `SurfaceTable`, reified that
private tree, and printed `model bisim = table(...)`, which the parser did not
accept. The exact AST identity result was real, but the source-language claim
was not. Generated witnesses must pass through the ordinary parser and
elaborator before exact identity is claimed.

The same review found that an enum value was retained only by its printed case
name. A lifted value must retain the resolved constructor declaration (or an
equivalent declaration-stable identity); display text is rendering data.

## Three witness routes, one language

Fine may eventually use three different solver procedures. They must not be
collapsed into a single operation called model finding.

1. **Model decoding.** The current finite relation is a semantic value. A model
   interpretation is evidence used to enumerate its complete finite graph and
   build a canonical constant-array-plus-stores term.
2. **Bounded choice decoding.** A finite Fine program skeleton has retained,
   typed choice sites. Z3 assigns selectors and Fine decodes those assignments
   through the retained skeleton. This is the useful Rosette mechanism; it is
   not inversion of an arbitrary Z3 formula.
3. **Refutation synthesis.** For a single-invocation specification `Q(x,y)`,
   Fine accumulates ground refutations `not Q(k,t_i(k))`. When they are jointly
   inconsistent, an unsat core supplies the ordered pieces of an `ite` program.
   This is the Reynolds `SynthSI` route.

SemGuS contributes backend ideas rather than a fourth surface language:
program syntax can be represented as interned Z3 data, and Spacer may sometimes
exclude a whole recursive Fine grammar. If used, both the grammar and its
meaning remain compiler-owned Fine definitions.

## Representations and laws

There are three representations even though one manager may intern all Z3
nodes:

- span-erased Fine value/program syntax;
- admitted semantic Z3 terms;
- solver encodings or program-code Z3 terms used during synthesis.

For admitted semantic terms in one live manager and resolved scope:

```
reify_sem(lift_sem(a)) ==ptr a
```

`lift_sem(reify_sem(source))` is a contextual, idempotent normalization. It is
not required to preserve comments, spans, aliases, or equivalent but differently
shaped formulas.

A program-code encoding has a separate codec:

```
decode_G(encode_G(c)) = core(c)
encode_G(decode_G(k)) = canonical_encoding_G(k)
```

The second equation is deliberately canonical rather than raw identity: unused
selector bits, flattened-array tails, and other backend don't-cares may have
many encodings. The compiler-owned meaning map from code to a semantic term is
one-way and generally non-injective. It is never named `lift`.

Every successful synthesis result must establish all of the following:

- the decoded program is ground, closed, well typed, and admitted by the stated
  Fine grammar;
- lowering the program agrees with the compiler-owned grammar semantics;
- a fresh query against the untouched original specification finds no
  counterexample;
- rendering, parsing, elaborating, and reifying the returned semantic body
  preserves exact same-manager AST identity within the admitted fragment.

An example set is not the original specification. A model is not a proof. A
proof of reachability with an unrecoverable term is not a synthesized program.

## Selected first synthesis slice

The first native slice is a single-invocation integer function such as:

```fine
synth max(left: Int, right: Int): Int {
  ensures {
    result >= left;
    result >= right;
    result == left || result == right;
  }
}
```

Fine supplies one fixed recursive expression grammar and enumerates its ground
program terms fairly by exact size. There is no hidden depth bound. Each term is
lowered by concrete traversal to an ordinary Z3 expression; Z3 is not asked to
evaluate a recursive syntax interpreter.

The refutation ledger starts with `Gamma = true`. While `Gamma` is satisfiable,
Fine selects the first grammar term `t` for which `Gamma && Q(k,t(k))` is
satisfiable and activates the labelled instance `not Q(k,t(k))`. Once `Gamma`
is unsatisfiable, Fine keeps the unsat-core terms in insertion order and builds
the nested conditional described by the refutation-synthesis construction. For
maximum, deterministic enumeration selects the two projections and yields:

```fine
if right >= left { right } else { left }
```

The body is then independently verified. The engine must not inspect the
function name, contain a prebuilt maximum expression, or use a function model
as the returned program. Renaming the function, a projection-only fixture, and
a three-argument maximum are anti-hardcoding gates.

A direct one-shot encoding with a recursive expression datatype, recursive
evaluator, unknown program, and universally quantified integer maximum was
tested against this checkout and returned `unknown` after ten seconds. That
route is therefore not the first slice.

## Result vocabulary

The runner reports the witness kind and validates it against the original
obligation. The minimum result distinctions are:

- `model-value` — a semantic value such as the finite bisimulation table;
- `source-program` — a decoded and independently verified Fine body;
- `unrealizable` — proved no admitted program exists (not merely no program was
  found within a resource budget);
- `unknown` — the selected solver procedure could not decide;
- `exhausted` — an explicit finite/resource bound was reached;
- `witness-unavailable` — a solver result exists but Fine cannot reconstruct a
  checked source witness.

## Spacer boundary

Spacer's eager inlining, linear inlining, and slicing are enabled on ordinary
paths and are many-to-one. The final transformed rules cannot uniquely recover
the source program. Before using Spacer for synthesis, the smallest decisive
experiment is:

1. disable those three transformations;
2. enable proof traces;
3. recover a nonlinear datatype witness from the full `get_answer()` proof;
4. re-enable each transformation separately.

`get_ground_sat_answer()` is linear-only and is not a valid recovery API for
ordinary branching grammars. If transformed witnesses matter, Fine needs an
append-only provenance DAG containing both parent rules, the selected tail
position, unifier substitution, slice mask, and raw and final resolvents.
Semantic proof/model converters do not provide a unique syntactic inverse.

## Witness ledger and rainfall

Fine owns a ledger connecting:

```
source hole
  -> search representation
  -> query and active constraints
  -> model/core/derivation evidence
  -> canonical code or value
  -> semantic term
  -> independent validation
```

The ledger records solver scope, model-completion policy, candidate order,
grammar/version fingerprint, canonicalization recipe, and validation result.
Persisted logs store syntax/serialization plus run and manager identity, never
pointers or bare numeric AST IDs.

Rainfall uses six semantic verbs: `object`, `scope`, `constraint`, `derive`,
`transform`, and `transition`, with producer-specific operations underneath.
Chronology and scope nesting are not causality; evidence edges are explicit.
Every producer states its coverage. Dropped events produce a gap marker. A model
evaluation is equality under that model, not a theory rewrite; a candidate
discarded by symmetry did not “rewrite” unless a representative was actually
constructed.

The first textual replay is the native maximum synthesis: query, model evidence,
chosen term and policy, activated ground instance, unsat core, conditional
assembly, normalization, fresh counterexample query, and accepted candidate.
The existing bisimulation remains a separate model-decoding replay.

## Fork maintenance

Fine is off by default in an ordinary top-level Z3 build; its flake enables it.
Language code should eventually live outside upstream-owned `src/`, with only a
small internal bridge under an explicit build flag. Keep three rebaseable
series: upstream synchronization, generic/additive Z3 observation hooks, and
Fine language work. Unrelated upstream packaging fixes do not belong in Fine
feature commits.
