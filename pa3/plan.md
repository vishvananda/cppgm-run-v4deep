# PA3 (ppexpr) plan and audit record

## Stage state

- Stage base commit: `2b52b6e3306f9748c62914e64eec99d6317c9156` (recorded on
  entry, before stage edits).
- Last reviewed commit: `2b52b6e3306f9748c62914e64eec99d6317c9156`.
- Target: `ppexpr` evaluates one controlling expression per logical source line
  and prints the result, `error`, or nothing for an empty line, then `eof`.

## Design / spec alignment

`ppexpr` is a third consumer of the phase 1-3 frontend, not a second frontend:

```
stdin -> TranslatedSource -> PPTokenizer -> CtrlExprStream -> PostTokenStream
                                                          -> CtrlExprSink -> CtrlExpression
```

- `CtrlExprStream` is the `IPPTokenStream` that splits the phase 3 stream into
  logical lines: it ignores whitespace-sequences, forwards every other
  preprocessing-token to the inherited PA2 `PostTokenStream`, and ends the line
  on `new-line`/`eof`. `PostTokenStream` needed one added entry point
  (`FinishGroup`) so a pending maximal string-literal group is resolved at a
  logical-line boundary rather than only at eof.
- `CtrlExprSink` is the `IPostTokenSink` that records the line's typed tokens in
  one reused buffer and decides the line-level rejection rules (invalid token,
  non-integral literal, user-defined literal). The vector is the one place a
  token outlives its callback, it is cleared per line and its capacity is kept;
  no second token representation is built.
- `CtrlExpression` is a hand-written predictive parser over that buffer. It
  evaluates as it parses, with a `live` flag that suppresses value computation
  and value-dependent course-defined errors on the branch a short-circuit or an
  untaken conditional arm does not evaluate, while still parsing and typing
  that branch (`250-eval-order`, `260-cond-ret-type`).
- All arithmetic is on the two promoted types the handout defines
  (`intmax_t`/`uintmax_t`), represented as one 64-bit image plus a signedness
  bit, so no per-node type object is built.
- `dev/frontend_source_sets.mk` registers the `ctrl_expr/*` sources and the
  inherited `posttoken/*` sources for the `ppexpr` tool only.
