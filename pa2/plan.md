# PA2 (posttoken) plan and handoff ledger

- Stage base commit: `40f09bbd882b86638fce4a6d42f0dbbeb763c815`
- Last reviewed commit: `40f09bbd882b86638fce4a6d42f0dbbeb763c815`

## Design / spec alignment

`posttoken` is a staged tool over the shared frontend phase, not a second
frontend.  `dev/src/preprocess/tokens` (PA1) owns translation phases 1-3 and
already exposes them as a streaming cursor (`TranslatedSource` + `PPTokenizer`
+ `IPPTokenStream`); PA2 adds phases 4-7's tokenization as a *consumer* of that
stream:

    stdin -> TranslatedSource -> PPTokenizer -> PostTokenStream -> sink -> stdout

- Phase 4 is a no-op (no directives allowed in the input).
- Phase 6 string concatenation is the only place a post token outlives its
  callback, so `PostTokenStream` holds exactly one pending maximal
  string-literal group; every other preprocessing-token is classified and
  emitted inside its callback.  No vector of tokens is materialised.
- `PostTokenStream` produces typed facts (`EFundamentalType`, decoded code
  units, `ETokenType`); the PA2 text format lives in the tool
  (`dev/posttoken.cpp`) behind `IPostTokenSink`, so the output requirement does
  not force any later phase to build text.
- Read-only, bounded tables (course simple-token table, literal escapes) are
  static; no process-global mutable cache.

## Behavioural groups

1. simple/identifier/invalid classification: keyword + punctuator table,
   `#`/`##`/`%:`/`%:%:`/header-name/non-whitespace-character -> invalid.
   **done**
2. pp-number analysis: integer/floating/ud variants, integer type selection
   (2.14.2) and range checks.  **done**
3. character literals: escape decoding, Unicode range, per-prefix code unit
   fit, `char`/`int` split.  **done**
4. string literals and phase 6 concatenation: encoding-prefix union,
   ud-suffix union (suffix must begin with `_`), numeric escapes as single
   code units, UTF-8/16/32 encoding, raw strings.  **done**

## Remaining / handoff ledger

- Implementation: complete for the PA2 contract.  All 26 `pa2/tests` fixtures
  pass.
- Independent-review questions (not waived, not blockers for this handoff):
  - The ud-suffix rule "must begin with `_`" is course-defined behaviour taken
    from the checked-in fixtures (`700`, `750`) and the reference; it is a
    lexical filter here rather than the later [over.literal] semantic check on
    declared literal operators.  A later stage that sees the `operator""`
    declaration must not re-derive it from this table.
  - `PA2Decode_float/double/long_double` are the handout-mandated scan
    functions, so out-of-range floating literals produce the C++11 `num_get`
    saturation result (inf / 0) rather than a rejection.  The handout makes
    the range check optional and untested for PA2.
- Previous PAs: `make test-report-through-pa1` clean.

## Performance evidence

See the ledger section appended at handoff (compiler latency and peak RSS for
`posttoken`, A/B against the frozen PA1 `pptoken` frontend, and the corpus).
