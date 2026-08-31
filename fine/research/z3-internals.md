# Z3 internals map for Fine

Evidence below is from `fine/main` at
`6aa9416d4a9e2031ebec1a56de3473fe3a6507a3` (2026-08-30). Line ranges refer to
this checkout.

## 1. AST hash-consing and IDs

### The interning table

- `src/ast/ast.h:1026-1050` names the mechanism explicitly. `ast_eq_proc`
  first compares each node's stored structural hash and then calls
  `compare_nodes`; `ast_table` is the `chashtable` containing interned `ast*`
  nodes.
- `src/ast/ast.cpp:397-460` defines structural equality. In particular, an
  application is equal only when its declaration pointer, arity, and every
  argument pointer agree (`424-428`); a variable uses index and sort pointer
  (`429-432`); a quantifier includes binder sorts/names, body, weight, patterns,
  no-patterns, and qid treatment (`433-456`). Thus application children are
  compared by already-interned identity, not by printing or semantic equality.
- `src/ast/ast.cpp:500-530` computes structural hashes. Applications hash the
  child hashes plus the declaration hash (`514-517`). Hash collision alone
  does not establish identity because `ast_eq_proc` also invokes
  `compare_nodes`.
- `src/ast/ast.h:1506-1565` defines `ast_manager`; its central members are
  `m_ast_table`, `m_expr_id_gen`, and `m_decl_id_gen` (`1563-1565`).
- `src/ast/ast.cpp:1657-1687` is the actual intern point,
  `ast_manager::register_node_core`. It computes/stores the hash, calls
  `m_ast_table.insert_if_not_there`, destroys the candidate and returns the
  existing pointer on a hit (`1665-1676`), or assigns a new ID on a miss
  (`1678-1687`). For applications, allocation and the call to `register_node`
  are in `ast_manager::mk_app_core`, `src/ast/ast.cpp:2110-2131`; variables take
  the same route in `src/ast/ast.cpp:2291-2300`.

The identity fact Fine can rely on inside one `ast_manager` is therefore the
returned live pointer. Reconstructing the same admitted node through the same
manager returns that pointer.

### What `get_id()` does and does not guarantee

- Every `ast` stores `unsigned m_id` and exposes `get_id()` in
  `src/ast/ast.h:459-510`.
- Expression and declaration IDs occupy separate ranges:
  `c_first_decl_id == 1u << 31` in `src/ast/ast.h:570-590`; the two generators
  are initialized accordingly in `src/ast/ast.cpp:1305-1311`.
- The C API is a direct read of that field:
  `Z3_get_ast_id` and `Z3_get_func_decl_id` are in
  `src/api/api_ast.cpp:354-364`. Its public contract says IDs are unique over
  the set of **live** AST objects in `src/api/z3_api.h:5152-5169`.
- IDs are not durable trace keys by themselves. This checkout enables
  `RECYCLE_FREE_AST_INDICES` at `src/ast/ast.h:55`; when reference count reaches
  zero, `ast_manager::delete_node` removes the node and recycles its ID in
  `src/ast/ast.cpp:1770-1791`. `id_gen::mk` reuses recycled IDs and
  `id_gen::recycle` records them in `src/util/id_gen.h:24-47`.
- Even a live node's numeric ID can be reassigned by the public
  `ast_manager::compress_ids()` (`src/ast/ast.h:1615-1621`, implementation
  `src/ast/ast.cpp:1455-1469`). There are no call sites for `compress_ids()` in
  `src/` at this revision, but the operation exists.

Consequently, an event retaining an `expr_ref`/`ast_ref` retains both the
interned node and its pointer identity. A bare numeric ID is unique only in its
manager while its node remains live (and provided `compress_ids()` is not
called); it is not globally unique, cross-manager, or persistence-stable.

## 2. Rewrite and simplifier hook sites

There are three different granularities. All already have direct `expr*` /
`expr_ref` values, so none requires parsing TRACE output.

### Generic recursive rewriter: broadest term-level site

`rewriter_tpl<Config>` is the shared bottom-up engine in
`src/ast/rewriter/rewriter.h:199-373`. `default_rewriter_cfg` supplies the
extension points `pre_visit`, `reduce_app`, `reduce_quantifier`, `reduce_var`,
`get_macro`, and `get_subst` at `src/ast/rewriter/rewriter.h:375-399`. The class
is instantiated by the theory rewriter, model evaluator, bit blaster, macro
rewriters, and many tactic/simplifier-specific rewriters.

The exact before/after-producing paths are:

- substitution short-circuit: `rewriter_tpl::visit`,
  `src/ast/rewriter/rewriter_def.h:143-157`;
- variable reduction: `process_var`, `src/ast/rewriter/rewriter_def.h:28-40`;
- nullary application reduction: `process_const`,
  `src/ast/rewriter/rewriter_def.h:80-130`;
- non-nullary application reduction: rewritten children are collected and
  `m_cfg.reduce_app` is called at
  `src/ast/rewriter/rewriter_def.h:267-328`; a successful result is committed or
  scheduled for bounded/full rewriting at `329-395`;
- macro expansion and no-rule congruence/reuse:
  `src/ast/rewriter/rewriter_def.h:398-471`, with completion of an expanded
  definition at `492-517`;
- quantifier reconstruction/reduction: `process_quantifier`,
  `src/ast/rewriter/rewriter_def.h:531-626`.

Traversal begins in `main_loop` (`src/ast/rewriter/rewriter_def.h:718-749`),
dispatches frames in `resume_core` (`756-811`), and is exposed by
`rewriter_tpl::operator()` (`814-827`). There is currently **no one term-result
commit function**: results are pushed on several paths above. A generic
before/after callback placed only around the non-nullary `reduce_app` call
would miss constants, substitutions, variable reductions, quantifiers, macro
expansion, cache hits, and congruence-only rebuilding. Conversely, a callback
at `operator()` sees only the root input/final output. This is the main coverage
risk in a generic hook.

For a non-nullary builtin step, the most exact pre-rewrite term is the
application of `f` to the already-rewritten `new_args` at
`src/ast/rewriter/rewriter_def.h:299-316`, not necessarily the original frame's
`t`: its children may already have changed. In proof-enabled mode the engine
materializes that application as `new_t` (`301-315`); without proofs it normally
does not, although all components needed to intern it are present.

### Builtin theory simplifier: clean local builtin-step site

- `th_rewriter_cfg` owns the bool/arith/BV/array/datatype/etc. sub-rewriters in
  `src/ast/rewriter/th_rewriter.cpp:46-85`.
- `th_rewriter_cfg::reduce_app_core` dispatches by theory family in
  `src/ast/rewriter/th_rewriter.cpp:169-247`, including array terms at
  `229-230`.
- Its wrapper `th_rewriter_cfg::reduce_app` has the rewritten `f,args`, returned
  `result`, and `br_status` together at
  `src/ast/rewriter/th_rewriter.cpp:648-684`. This is the narrowest clean place
  for a direct callback covering builtin theory application rules, including
  subsequent push/pull-ITE processing. It does not cover other
  `rewriter_tpl<Config>` instantiations or the generic paths listed above.
- Z3 already reconstructs the input application and emits ID-based proof-trace
  records in `log_rewrite_axiom_instantiation`,
  `src/ast/rewriter/th_rewriter.cpp:608-645`, called on a successful core
  rewrite at `652-654`. This demonstrates that both interned endpoint terms are
  available before logging; consuming this text is unnecessary. It also shows
  that the existing trace path fires before later push/pull-ITE changes at
  `656-683`, so it is not identical to the wrapper's final result.
- Quantifier theory rewriting is separate in
  `th_rewriter_cfg::reduce_quantifier`,
  `src/ast/rewriter/th_rewriter.cpp:844-967`.

The public root interfaces are `th_rewriter::operator()` in
`src/ast/rewriter/th_rewriter.h:29-63`, implemented at
`src/ast/rewriter/th_rewriter.cpp:1087-1127`. A callback there yields one
before/final-after pair per public invocation, not internal rainfall.

### Formula/pipeline simplifier boundaries

The modern formula-state adapter `rewriter_simplifier::reduce` loops over
dependent formulas, calls `th_rewriter`, and replaces each formula at
`src/ast/simplifiers/rewriter_simplifier.h:24-54`. The classic `simplify`
tactic does the analogous goal loop at
`src/tactic/core/simplify_tactic.cpp:23-69`. These are useful boundaries for
formula-level before/after events, but neither exposes subterm rewrite steps,
and other dependent-expression simplifiers perform transformations without
going through `th_rewriter`.

## 3. Model evaluation of array terms

### Public path and evaluator composition

- `model` owns a `model_evaluator` (`src/model/model.h:32-40`).
  `model::eval_expr` enters it at `src/model/model.cpp:91-101`, and
  `model::operator()` delegates directly at `src/model/model.cpp:573-581`.
- The public evaluator interface is in `src/model/model_evaluator.h:31-69`.
  `model_evaluator::operator()` runs its recursive rewriter and then calls
  `expand_stores` at `src/model/model_evaluator.cpp:830-840`.
- The implementation is itself `rewriter_tpl<mev::evaluator_cfg>` at
  `src/model/model_evaluator.cpp:758-770`. Its config owns an `array_rewriter`
  and `array_util` (`46-69`) and forces select-over-store and select-over-ITE
  expansion on (`71-105`, specifically `97-98`).

### How array values and selections evaluate

- In `evaluator_cfg::reduce_app_core`, a nullary `as-array[f]` obtains `f`'s
  `func_interp` and asks it for an array expression at
  `src/model/model_evaluator.cpp:204-225`. An ordinary model constant,
  including one of array sort, is replaced by `get_const_interp(f)` at
  `226-243`.
- Once children are evaluated, array-family applications are sent to
  `array_rewriter::mk_app_core` at
  `src/model/model_evaluator.cpp:286-302` (array branch `290-291`). The array
  rewriter dispatches `OP_SELECT` at
  `src/ast/rewriter/array_rewriter.cpp:44-125`. Its concrete selection rules
  are `src/ast/rewriter/array_rewriter.cpp:243-317`: select of a matching store
  returns the stored value (`257-263`), a provably different store is skipped
  (`264-267`), a constant array returns its else value (`274-276`), lambda
  arrays beta-reduce (`279-295`), and `select(as-array[f], I)` becomes `f(I)`
  (`310-313`). Unknown store indices can expand to an ITE at
  `src/ast/rewriter/array_rewriter.cpp:320-403`, especially `355-381`.
- Model interpretation then evaluates the resulting function application by
  looking up matching value arguments in `func_interp`:
  `evaluator_cfg::evaluate` / `eval_fi`,
  `src/model/model_evaluator.cpp:118-149`; dispatch reaches it at
  `src/model/model_evaluator.cpp:306-313`.

### Turning a `FuncInterp` into an array AST

`func_interp` keeps graph entries, an else value, and a cached array expression
in `src/model/func_interp.h:90-111`, exposing `get_array_interp` at `147-150`.
The construction is in `src/model/func_interp.cpp:409-481`:

- if all else/entry arguments/results are ground, it creates an array sort, a
  constant array holding the else case, and a store for every non-default entry
  (`440-458`);
- if not ground, it creates an interpretation body and wraps it in a lambda
  (`423-437`);
- `get_array_interp` caches the resulting interned expression (`473-481`).

`model::unfold_as_array` is a small direct façade over this conversion at
`src/model/model.cpp:548-557`.

The evaluator also expands/completes residual `as-array` values in
`evaluator_cfg::expand_as_array`, `src/model/model_evaluator.cpp:324-365`.
After the recursive evaluation, `expand_stores` optionally normalizes an array
value into constant-array-plus-stores at
`src/model/model_evaluator.cpp:367-385`. The extraction routine recognizes a
store chain, constant array, index-set form, or `as-array` backed by a model
function in `src/model/model_evaluator.cpp:691-754`. Both behaviors are enabled
by default through the `array_as_stores` parameter
(`src/model/model_evaluator_params.pyg:1-8`).

Array equality is a separate best-effort path: array-sorted `=` dispatches to
`mk_array_eq` at `src/model/model_evaluator.cpp:249-277`; that procedure
extracts the two store/else representations and compares the finite entries at
`src/model/model_evaluator.cpp:536-585`, with its unique-index fast path at
`614-670`.

## Risks established by the source

1. Numeric `get_id()` values are not durable identities: deletion recycles
   them, managers have independent ID spaces, and `compress_ids()` can renumber
   live nodes. Retaining the AST through an `expr_ref`/`ast_ref` is what keeps
   pointer identity and prevents deletion/reuse.
2. `rewriter_tpl` is broad but has no single per-node completion choke point.
   A hook at only `th_rewriter_cfg::reduce_app`, existing TRACE emission, or a
   public `operator()` necessarily loses a different class of rewrite events.
3. `rewriter_tpl` is generic infrastructure, not a boundary around every Z3
   solver transformation. Direct solver propagation, clause manipulation, and
   simplifiers that do not instantiate/call this engine remain outside it.
4. `func_interp::get_array_interp` iterates the model's stored entries; it is a
   valid model-derived AST but does not enumerate a finite sort or impose a
   Fine-level canonical domain order. It should be treated as the current model
   representation/evidence, not assumed to be Fine's canonical finite witness.
5. Array evaluation is deliberately best effort. Without model completion an
   absent interpretation can leave a term unreduced (`model_evaluator.cpp:226-245`),
   and extraction fails for non-ground function interpretations
   (`model_evaluator.cpp:720-753`).
