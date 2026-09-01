# Reference transfer: live Rainfall without invented continuity

## The situation this design is for

Rainfall is eventually displayed beside a Fine document while that document is
being edited. A solver run may still be producing events after the user inserts
text, reparses the file, or starts another run. The hard case is not drawing a
line beside an expression. It is deciding what that line is allowed to claim
after the expression has moved or changed.

The governing distinction is:

- an event is true of the exact source snapshot and Z3 manager that produced it;
- a decoration may be transported through an edit so the display does not jump;
- transporting the decoration does not transport the event's evidential claim.

Thus an old `unsat` marker can move with edited text as a visibly stale marker,
but it cannot become evidence about the new text. Matching names, spans,
pretty-printed terms, or numeric AST IDs do not repair this break.

## Four identities, not one ID

The live projection needs four separately scoped references.

```text
document = (workspace, document)
snapshot = (document, revision, exact_source_hash)
source_node = (snapshot, parse_local_node_id, source_span, syntax_kind)
z3_term = (run, recorder, manager, never_reused_handle)
```

An evidence edge joins a source node to a Z3 term:

```text
source_edge = {
  source: source_node,
  term: z3_term,
  correspondence: exact | desugared | generated | internal_z3,
  producer: component_and_boundary,
  note?: text
}
```

`internal_z3` deliberately has no Fine source node. A post-preprocessing clause
literal is not made into Fine syntax merely because its text resembles a source
term. Conversely, one source node may have several desugared terms and one term
may have several source roles. The edge is therefore a relation, not a field on
either object.

The existing Rainfall term identity already supplies the last tuple. The first
three are missing. A source span alone is not a source identity: offset 80 in
revision 4 and offset 80 in revision 5 are different places even if they
contain the same bytes.

## What the inspected repositories actually contribute

### Latte

The useful part is not Latte's F* surface. It is the separation between a live
document identity and an immutable generated artifact, plus compiler-owned
correspondence metadata.

- `packages/language-core/src/compilation/checker-document-identity.ts`
  distinguishes the workspace/document resource from its URI.
- `checker-artifact-identity.ts` hashes the compiler identity, exact source,
  module name, and generated artifact rather than borrowing editor revision as
  artifact identity.
- `compilation-snapshot.ts` binds source hash, phase-attributed diagnostics,
  AST, IR, query ownership, and generated text into one immutable compilation
  result.
- `codegen/source-map.ts` labels mappings `exact`, `desugared`, or `generated`
  and chooses mappings by an explicit precedence rule.
- `query-ownership.ts` derives query ownership through the AST-to-IR identity
  join. It explicitly refuses to infer ownership from source ranges,
  declaration order, or parsing generated text. An unavailable table is not
  confused with an authoritative empty table.

Fine should take those distinctions. It should not take Latte's arrow types,
model/theory resolution, polymorphism, or F* emission pipeline.

### typescript-go

The parser provides useful implementation discipline: a scanner separate from
the parser, half-open text ranges, parent links, factory hooks for creation and
updates, pooled allocation, error recovery, and diagnostics retained on a
source-file object. Those are relevant when Fine stops aborting at the first
parse error and acquires an editor-facing parse result.

Its node IDs are not suitable as Fine's public source identity.
`internal/ast/utilities.go` assigns a process-global integer lazily to an AST
object. That is useful as an in-memory cache key, but it does not establish
continuity across parses. The repository's `reparser.go` primarily constructs
synthetic/reinterpreted nodes inside a parse (not a certified cross-edit
incremental tree), and the current README still says watch mode has no
incremental rechecking. Fine should not claim incremental source identity by
copying either mechanism.

### Dark

Dark supplies the correct shape for the edit boundary: source bytes remain
authoritative, coordinate transport is a typed operation over revisions, and
asynchronous capabilities are generation-bound. The particularly useful rule
is that expiry or staleness changes what the runtime may do; it does not prove
completion, failure, or semantic absence.

The transferable pieces are revision transport, explicit stale outcomes, and
generation leases. Markdown residual codecs, projection-claim conflict
algebras, and DOM correspondence are not Fine language features. Dark is also
unfinished evidence, not a library or theorem Fine can cite as discharged: the
current Lean tree contains forty `sorry`/`admit` occurrences.

### Recovered Obsidian behavior and obsidian-demin

The readable recovered implementation exposes one concrete UI policy in
`chunks/editor/LivePreviewDecorations.js`: while the syntax tree does not cover
the viewport, the editor is composing, or the mouse is down, it maps existing
decorations through the transaction instead of rebuilding them. When those
conditions clear, it rebuilds from the current syntax tree, viewport,
selection, and effects.

Fine should retain the two-stage behavior but not Obsidian's meaning for it:

1. map existing Rainfall decorations immediately through an edit, marking
   them stale;
2. rebuild current decorations only from a newly admitted Fine snapshot and
   its run.

Obsidian's `tree.length < viewport.to` is only a convenient measurement of the
condition it cares about: whether parsing covers the demanded range. Fine's
protocol should state the condition directly rather than turn that number into
semantic evidence. Selection-dependent hiding, Markdown token-class rules,
and ten-iteration cursor repair are observations about that editor, not a
Rainfall semantics. The proprietary tree remains read-only; obsidian-demin is
an inspection tool, not a Fine dependency.

## Live projection state machine

An editor-neutral first protocol needs these states for each run:

```text
running(snapshot, generation)
accepted(snapshot, generation, result)
superseded(snapshot, generation)
cancelled(snapshot, generation)
```

The display separately classifies each projected annotation:

```text
current       event snapshot equals the displayed snapshot
transported   location mapped to the displayed snapshot, claim remains old
unplaced      revision transport could not retain a location
```

On a source transaction:

1. create a new exact snapshot identity;
2. transport old visual ranges and label them `transported`;
3. parse the whole current document (incrementality is not yet needed);
4. start a new generation only if parsing and elaboration admit a run;
5. accept events only into the run and snapshot named by their envelope;
6. replace transported markers with current ones only when new evidence says
   so; never upgrade them because their text still matches.

During IME composition the viewer may continue step 2 and defer steps 3--4.
This is a responsiveness rule, not a fact about the program.

## First vertical slice

The first implementation should be smaller than an editor but strict enough
that the editor cannot later fake continuity.

1. Add a Rainfall document object containing an opaque document ID, monotonic
   revision, exact source hash, and byte length. The filename is display data.
2. Give parsed declarations and expressions parse-local IDs and emit source
   objects containing snapshot, span, and syntax kind.
3. Emit explicit source-to-term evidence edges during elaboration for the
   already runnable `check` fixture. Keep clause literals and other internal
   terms unowned by source.
4. Write a tiny replay validator which admits one snapshot plus JSONL and
   rejects cross-snapshot source edges, unknown term handles, reused handles,
   events after terminal run state, and an `internal_z3` edge carrying a source
   node.
5. Test the hostile cases: identical bytes in two documents, the same URI
   reopened with a new document identity, a span-preserving edit, a moved span,
   a late event from the old generation, and same printed Z3 text from two
   managers.

This slice does not require an LSP, incremental parser, CodeMirror extension,
or persistent AST IDs. Once it is closed, the next component is a viewer that
consumes only validator-admitted projections and visibly distinguishes current
from transported evidence.

## Rejected shortcuts

- Do not key source nodes by span, source text, declaration name, or a hash of
  the node text.
- Do not preserve a TypeScript-style in-memory node ID across reparses by
  heuristic matching and call it identity.
- Do not turn revision-transported decorations into current solver evidence.
- Do not attach every Z3 term to the nearest enclosing Fine range. Generated
  and internal terms are allowed to have no source representative.
- Do not make the viewer parse pretty-printed Rainfall terms to rediscover
  ownership. Ownership is compiler evidence or it is absent.
