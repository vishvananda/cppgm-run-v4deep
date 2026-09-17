# PA5 (cppgm++ --emit-ast) plan and handoff ledger

## Stage state

- Stage base commit: `91bd3202` (the PA4 final audit, recorded on entry before
  any stage edit).
- Last reviewed commit: `91bd3202`.
- Target: `cppgm++ --emit-ast -o <out> <src>...` runs translation phases 1-7
  for each primary source, parses each translation unit with the PA5 syntax
  subset, and writes the deterministic AST dump the checked-in `.ref` files
  define.
- `make test-report-through-pa4` passes; PA5 is the stage under
  implementation.

## Design / spec alignment

The parser is a fifth consumer of the shared phase 1-7 frontend and the first
stage that owns syntax:

```text
file bytes -> TranslatedSource -> PPTokenizer -> PPTokenReader
   -> Preprocessor -> PostTokenStream -> SyntaxTokenSink (phase 7 records)
   -> SyntaxParser (recursive descent over the token vector)
   -> SyntaxArena (structured AST) -> the dump
```

- `syntax/syntax_token.h` holds one token record: a `SimpleTokenKind` (or the
  identifier/literal/eof pseudo-kinds), the spelling, and the literal facts the
  later semantic stages need.
- `syntax/syntax_arena.*` owns the nodes. A node is a tag name, an optional
  inline label and a child list; the dumper walks it in preorder.
- `syntax/syntax_parser*.*` is the recursive descent parser, one function per
  useful grammar production of `pa5.gram`, split by area.
- The name-category boundary of `parsing.md` is a scope stack of name facts
  recorded by declarations, with the documented lexical fallback for names no
  declaration has resolved.

## Remaining groups

1. Token plumbing and the arena/dumper (infrastructure).
2. Declarations, declarators, class/enum/namespace, templates.
3. Expressions, statements, ambiguity resolution.
4. Reference-exact dump alignment and performance evidence.

## Handoff ledger

(populated at handoff)
