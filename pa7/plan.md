# PA7 (`cppgm++ --emit-semantics`) plan and handoff ledger

## Stage state

- Stage base commit: `91b13266e02a6083457e955235b56e075844a180` (the PA6 audit
  handoff, recorded on entry before any stage edit).
- Last reviewed commit: `91b13266` (no stage audit has run yet).
- Target: `cppgm++ --emit-semantics -o <out> <src>...` runs phases 1-7, the PA5
  parse, the PA6 scope/type analysis, resolves expressions, statements, calls,
  conversions and the limited overload set, and writes the deterministic
  resolved-tree dump the checked-in `.ref` files define.
- Progress: 0 / 186 at stage entry; see the ledger below for the running count.

## Design / spec alignment

The stage is a seventh consumer of the shared phase 1-7 frontend, and the third
stage that owns a tree:

```text
file bytes -> TranslatedSource -> PPTokenizer -> PPTokenReader
   -> Preprocessor -> PostTokenStream -> SyntaxTokenSink
   -> SyntaxParser -> SyntaxArena -> Analyzer -> Model -> semantics builder
   -> SemTree -> the dump
```

`dev/src/semantic/` gains three pieces beside the PA6 layer:

- `semantics_tree.h` is the resolved output tree: one node per printed line,
  holding the tag, the value category, the resolved type spelling and the
  trailing or leading label.  Keeping it apart from `SyntaxArena` is what lets
  the builder *replace* a node - a parenthesized expression disappears, an
  initializer becomes a child of the variable it initializes, a call's
  `argument-list` becomes the call's own children - without mutating the parse.
- `semantics_builder.*` is the walk.  It runs after the PA6 `Analyzer`, reads
  the `Model` that analysis built, and resolves each expression to a type, a
  value category and - where a call is involved - a selected function.  The
  scope in effect at a statement is the one PA6 opened for it, so the builder
  never re-derives scope structure.
- `semantics_dump.cpp` and `semantics_driver.cpp` print the tree.

## Findings, changes and evidence

(Recorded as the work lands.)

## Performance evidence

(To be measured with the PA7 protocol before handoff.)

## Validation

(Recorded at handoff.)

## Handoff ledger

### Unfinished implementation

The stage is not yet implemented; this section tracks the remaining groups.

### Independent review questions

None recorded yet.
