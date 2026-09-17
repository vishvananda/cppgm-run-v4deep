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

## Handoff ledger

- Implementation: complete for the PA2 contract.  All 26 `pa2/tests` fixtures
  pass; `make test-report-through-pa2` is 80/80 and `make test-report-through-pa1`
  stays clean.
- Comparisons run beyond the fixtures (personal harnesses, not graded):
  - `student.tests/posttoken_differential.pl` - randomised differential over
    the pp-number grammar, type selection, escapes, phase 6 unions and raw
    strings.  1200 inputs x 3 seeds plus the curated boundary list: no
    divergence from the reference.
  - Exhaustive sweeps against the reference: 4436 generated pp-numbers,
    1000 character literals and 875 string literals, each compared one input
    per process.  No divergence in stdout or exit status.
- Independent-review questions (not waived, not blockers for this handoff):
  - The ud-suffix rule "must begin with `_`" is course-defined behaviour taken
    from the checked-in fixtures (`700`, `750`) and the reference; it is a
    lexical filter here rather than the later [over.literal] semantic check on
    declared literal operators.  A later stage that sees the `operator""`
    declaration must not re-derive it from this table.
  - `0b`/`0B` binary integer literals are accepted.  That is a C++14 form, not
    a C++11 one, but the course frontend takes it and
    `pa29/tests/preproc/400-host-binary-integer-ud-literal` requires it, so
    this stage owns it rather than a later one.
  - `ScanFloatingLiteral` does not read every literal through
    `PA2Decode_*`: a hexadecimal floating literal and a value outside the
    type's range are read with `strto*`.  A C++11 stream extraction cannot
    express either (the iostream grammar has no hexadecimal form; an
    out-of-range value is stored as the type's extreme, not as an infinity),
    and the reference reports the C library result in both.  In-range literals
    are unchanged.  The handout makes the range check optional and untested
    for PA2.  This is the one intentional deviation from the starter scan and
    is worth an independent look.
  - Phase 4 is a strict no-op here because PA2's input never contains a
    directive; the preprocessor that replaces it belongs to a later stage.

## Performance evidence

Protocol: fixed binaries, flags and input; wall-time ABBA blocks; an A/A arm
against the reference to calibrate noise; every observation kept in
`/tmp/posttoken_benchmark.samples.tsv`.  PA2 produces no executable, so only
compiler latency and peak RSS are reported.

Corpus: `/tmp/posttoken_benchmark.txt`, 7 234 484 bytes, 40 000 generated
blocks of the phase 4-7 token mix.  `student.tests/posttoken_benchmark.pl
40000 6`, and the student output is byte-identical to the reference on it.

| arm | latency (median [min..max]) | peak RSS (median [min..max]) |
| --- | --- | --- |
| `mine` | 0.3670 s [0.3633..0.3690] | 11.6 MB [11.5..11.6] |
| `ref`  | 0.6558 s [0.6516..0.6750] | 11.0 MB [10.9..11.0] |

- Paired per-block difference (mine - ref): -0.2914 s [-0.3110..-0.2878].
- A/A noise floor (spread): 0.0395 s.  The measured difference is ~7x the
  noise floor, so it is a real result, not a measurement artefact.
- Marginal cost of this stage: the same corpus through the stage-base PA1
  `pptoken` frontend takes 0.58 s / 11.57 MB (three runs: 0.58, 0.58, 0.57 s;
  11880, 11844, 11840 KB) against this stage's 0.36 s / 11.57 MB (11868,
  11844, 11812 KB), so phase 7 classification and phase 6 concatenation add no
  measurable latency or memory above the frontend.  `pptoken` writes one
  `cout` insertion per token field where this tool fills a 64 KiB block, which
  is the whole of the difference.
- Peak-RSS budget: this stage may add at most 1 MB / +10% over the reference
  frontend on this corpus.  Measured +0.6 MB / +5%, so it is inside budget.
  The growth is the pending maximal string-literal group plus the tool's
  64 KiB output block, both bounded by the group's spelling length.
- No optimization was needed to reach this: the win over the reference comes
  from the phases-1-3 streaming frontend inherited from PA1 and from emitting
  the text output in blocks.  No performance claim here depends on added
  compiler work, so there is no profitability budget to justify.
