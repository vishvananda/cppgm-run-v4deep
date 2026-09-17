# PA1 implementation plan

## Stage state

- Stage base commit: `927dce14` (recorded on first entry, before stage edits)
- Last reviewed commit: `927dce14` (preserved; this turn's work is `79f49057`,
  `4a644e92`, `888410a0` and the handoff commit)
- Target: `pptoken` implements translation phases 1-3 of N3485 2.2 / 2.5.

At entry `dev/pptoken.cpp` was the untouched course skeleton: `PPTokenizer::process`
threw `NotImplementedException`, so all 54 `pa1/tests/*.t` fixtures failed with
`EXIT_NOT_IMPLEMENTED` (0/54 passing, 0/1 stages).

## Design and spec alignment

```
immutable byte buffer
  -> streaming phase 1/2 cursor: UTF-8 decode, trigraph replacement,
     universal-character-name replacement, line splicing
  -> greedy preprocessing-token recognition
  -> IPPTokenStream events
```

- `dev/src/preprocess/tokens/pp_source_translation.{h,cpp}` owns the byte buffer
  and applies phases 1 and 2 lazily as the tokenizer pulls code points.  A
  translation unit costs its source buffer, a line index and a bounded lookahead
  window; no intermediate owning token vectors exist, matching the spec's
  "streaming token cursor" and "minimal typed fact set" requirements.
- Raw string literals read the untranslated buffer, because `2.2/1.3` reverts the
  phase 1 and 2 rewrites between their quotes.  The prefix before the opening
  quote still comes from the translated stream, so a splice or trigraph there
  stays rewritten.  `ResumeAt` continues the pipeline after the closing quote;
  the pipeline state at a raw string boundary is the same as at any token
  boundary, so no rewrite can span the resume point.
- `dev/src/preprocess/tokens/pp_tokenizer.{h,cpp}` is the single greedy pass.  It
  keeps the spelling of the token it is about to report and nothing else.
- `dev/pptoken.cpp` is the stdin wrapper; it reads in blocks and moves the buffer
  into the cursor.

Behaviour that the handout leaves open was fixed by probing
`reference-binaries/pptoken`.  Each row is covered by a fixture, a curated
differential input, or both.

| Behaviour | Rule used | Evidence |
| --- | --- | --- |
| Phase order | trigraph, then UCN, then splice | `300-ucn-trigraph-ordering`, `100-line-splice-comment` |
| Backslash pairing | a `\` that does not introduce a UCN consumes the next code point verbatim, so `\\u0041` keeps both backslashes while `\\A` still forms a UCN | probes, curated differential inputs |
| UCN validity | value <= 0x10FFFF, not a surrogate, and >= 0xA0 unless `$`, `@`, `` ` `` | `300-invalid-universal-character-value-bad`, probes |
| UCN in literals | converted before tokenization, so `"\\u0041"` is untouched but `"A"` is a bad value | curated differential inputs |
| Escape validation | strict simple / octal / `\x`+digits / `\u`+quad spellings | `300-string-hex-escape-bad`, `300-string-octal-escape-bad` |
| Whitespace | ASCII space, tab, VT, FF, CR; LF is `new-line`; U+FEFF is an identifier body character mid-file | probes, `300-utf8-bom` |
| Raw strings | read untranslated; delimiter <= 16; invalid delimiter character and unterminated literal are errors | `200-trigraphs`, `300-raw-string-delimiter-bad`, probes |
| `header-name` | after (start of line or `new-line`) (`#` or `%:`) `include`, whitespace and comments ignored between; the h-char/q-char sequence must be non-empty, so `#include ""` and `#include <>` fall back to ordinary tokenization | `200-header-name`, probes, curated differential inputs |
| `<::` | split unless the following code point is `:` or `>`; end of file counts as neither | `400-angle-colon-madness`, probes |

## Remaining groups

1. (done this turn) Phase 1-3 for `pptoken`, streamed.
2. Later stages: register the two source basenames for `preproc`, `posttoken`,
   `ppexpr` and `cppgm++` in `dev/frontend_source_sets.mk` and drive the same
   cursor from those tools.  Not started.
3. Later stages, not waived: the spec's section 9 telemetry (phase time, peak
   memory and work counters) and its full benchmark set (loops, calls, memory
   access, floating point, self-hosting) belong to the stages that own those
   phases.  This stage ships the frontend benchmark only, because no other phase
   exists yet.

## Performance evidence

Protocol: `student.tests/pptoken_benchmark.pl`, a fixed 2.90 MB generated
translation unit, ABBA blocks with the reference wrapper as the paired baseline,
output checked identical before any timing is accepted.  `perf record` attributes
the remaining time to `std::__ostream_insert`, the shared output path, not to the
tokenizer.

| Revision | Latency (median) | Peak RSS (median) |
| --- | --- | --- |
| reference wrapper | 0.260 s | 7.3 MB |
| materialized pipeline (`79f49057`/`4a644e92`) | 0.58-0.74 s | 147.6 MB |
| streamed cursor (`888410a0`) | 0.280 s | 7.5 MB |

Whole-run spread was below 0.02 s and 0.2 MB in every block.  Two further corpora
agree: 2.9 MB of whitespace 0.05 s / 7.6 MB against 0.05 s / 7.5 MB, and 7.0 MB
of dense tokens 0.27 s / 13.5 MB against 0.27 s / 11.3 MB.

No optimisation level, IR bound or code-growth budget is mandated for this stage;
the handout's acceptance is the fixture contract.  The streamed cursor is not an
optimization of generated code, so no A/B runtime or text-size budget applies
yet.  Correctness is unchanged across the rewrite: identical stdout against the
reference on every corpus and on every differential input.

## Validation

- `make test-pa1` and `make test-report-through-pa1`: 54/54.
- `perl scripts/cppgm_file_audit.pl --stage pa1 --paths dev/src`: passed.
- `perl student.tests/pptoken_differential.pl`: 46 curated boundary inputs plus
  generated inputs, compared with the reference wrapper on stdout and exit
  status.  About 400 000 inputs passed across the turn, including all 46 curated
  ones on every run.  The 46 curated inputs are re-checked first on every run so
  a boundary regression is reported even when the seed changes.
- The differential sweep is a personal test; it is not part of the course
  contract and does not replace `make test-pa1`.

## Handoff ledger

- Turn start: 0/54 pa1 fixtures passing, every failure the skeleton's
  `EXIT_NOT_IMPLEMENTED`; no behaviour group was partially implemented.
- Turn end: 54/54 passing, earlier PAs not applicable (PA1 is the first stage).
- Boundary of this handoff: PA1's phase 1-3 surface is complete and matched
  against the reference; the tool is not yet driven from `preproc`, which is
  group 2 and needs the PA2 handout.  No related fix is practical without it.
- Open questions for independent audit, none waived:
  - Our choice of physical byte column for `set_source_location` is a documented
    self-selected definition; no fixture constrains it, and a later stage that
    consumes locations may require a different unit.
  - The `<::` rule and the empty-header-name fallback are matched against the
    reference wrapper, not derived from fixture text.  The reference could be
    wrong; the cited clauses (2.5.3, `header-name`'s non-empty h-char-sequence
    and q-char-sequence productions) support the implemented reading.
