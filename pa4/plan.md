# PA4 (preproc) plan and audit record

## Stage state

- Stage base commit: `e9377f975bade53affa3dcef6395873790a8d652` (recorded on
  entry, before stage edits).
- Last reviewed commit: `e9377f975bade53affa3dcef6395873790a8d652`.
- Implementation commits: (recorded as the stage lands).
- Target: `preproc -o <out> <src>...` runs translation phases 1-7 for each
  primary source with the course's macro, conditional, include, line-control,
  pragma and predefined-macro rules, and writes the PA2 post-token dump with
  `preproc <n>` / `sof` / `/` `eof` framing.

## Design / spec alignment

`preproc` is a fourth consumer of the shared phase 1-3 frontend, and the first
one that owns preprocessing *state*:

```text
file bytes -> TranslatedSource -> PPTokenizer -> PPTokenReader (PP tokens +
   file/line) -> Preprocessor (directives, conditionals, includes, macros)
   -> PostTokenStream -> TextPostTokenSink -> the PA2 text view
```

- `PPToken` (in `preprocess/preproc/pp_token.h`) is the one token record the
  preprocessor, the expander and the post-token pass share. The spelling is
  owned by the record (a directive's tokens must outlive the tokenizer
  callback); the source location is an index into a file-name table plus a
  physical line; the unavailable-macro-name set is a shared immutable vector so
  an unpainted token costs a null pointer and painting a whole replacement
  costs one allocation.
- `PPTokenReader` (`preprocess/preproc/pp_token_reader.*`) is the
  `IPPTokenStream` that asks for source locations and records the phase 3
  callbacks as records. The `pptetoken` tool keeps its own callback consumer;
  nothing about the PA1 contract moves.
- `MacroTable` (`preprocess/preproc/pp_macro.*`) owns `#define`d macros. A body
  is normalized to at most one whitespace token between tokens and validated at
  definition time (a `#` or `##` that cannot be part of a paste, a non-final
  `...`, `__VA_ARGS__` outside a variadic body), which is where the handout's
  rejection cases are detected.
- `MacroExpander` (`preprocess/preproc/pp_expander.*`) is a stack rescan with
  per-token blue paint, as `macros.md` prescribes: a token carries the macro
  names it may no longer expand, a replacement's tokens join the head's paint
  with the invoked name, and a substituted argument's paint is kept and joined
  with the invocation's. Arguments are macro-expanded lazily - only when a
  parameter occurrence is neither stringized nor adjacent to `##` - which is
  what keeps `stringize(max(0))` legal and `# x x` an error.
- `Preprocessor` (`preprocess/preproc/pp_preprocessor.*`) is the driver: it
  splits the record stream into directives and text-sequences, evaluates
  `#if`/`#elif` through the inherited PA3 evaluator, resolves includes, owns
  `__FILE__`/`__LINE__`/`#line`/`#pragma once`/`_Pragma`, and emits expanded
  text-sequence tokens to the sink. State is per translation unit; a text
  sequence never crosses a primary-source boundary.
- The PA2 text view has one owner: `posttoken/post_token_text.h` holds the
  sink `posttoken` already used, and `preproc` reuses it rather than growing a
  second formatter. It gains one observer flag (an `invalid` record was
  reported) because `macros.md` makes an invalid phase-7 token an error even
  when macro replacement succeeded.
- The PA3 evaluator gains a value-returning entry point
  (`CtrlExpression::EvaluateValue`, surfaced as `CtrlExprSink::Result`) so
  `#if` reads a number instead of parsing the text view back.

## Remaining groups

1. Macro group (`#define`/`#undef`, arguments, stringize, paste, rescan,
   recursion) - `tests/macros/*`.
2. Directive group (conditionals, includes, `#line`, predefined macros,
   pragmas, `_Pragma`) - `tests/directives/*`.
3. Integration: locations through expansions, translation-unit state reset,
   string-literal adjacency across directive lines.

## Performance evidence

(Recorded at the stage handoff: compiler latency and peak RSS on a generated
translation unit, with the reference reading beside it.)

## Handoff ledger

- Unfinished implementation: (recorded at handoff).
- Independent review questions: (recorded at handoff).