# PA1 implementation plan

## Stage state

- Stage base commit: `927dce14` (recorded on first entry, before stage edits)
- Last reviewed commit: `927dce14`
- Target: `pptoken` implements translation phases 1-3 of N3485 2.2/2.5.

At entry `dev/pptoken.cpp` was the untouched course skeleton: `PPTokenizer::process`
threw `NotImplementedException`, so all 54 `pa1/tests/*.t` fixtures failed with
`EXIT_NOT_IMPLEMENTED` (0/54 passing, 0/1 stages).

## Design and spec alignment

The production pipeline for this stage is

```
immutable byte buffer
  -> UTF-8 decode to physical code points (BOM removed, final LF ensured)
  -> phase 1/2 stream: trigraph replacement, UCN replacement, line splicing
  -> preprocessing-token recognition
  -> IPPTokenStream events
```

- `dev/src/preprocess/tokens/pp_source_translation.{h,cpp}` owns the immutable
  buffer, the decoded physical code points and the phase 1-2 stream.  Every
  translated code point carries the physical index, line and column that
  produced it, so a raw string literal can read the untranslated spelling that
  [lex.pptoken]/3 requires and so tokens can report physical source locations
  without re-parsing.
- `dev/src/preprocess/tokens/pp_tokenizer.{h,cpp}` is the single-pass greedy
  recogniser.  It is linear in code points and allocates only the token spelling
  it hands to the stream.
- `dev/pptoken.cpp` is the stdin wrapper.

Reference probing against `reference-binaries/pptoken` fixed the ambiguous
points that the handout leaves open; each is covered by a fixture except where
noted:

| Behaviour | Rule used | Evidence |
| --- | --- | --- |
| Phase order | trigraph, then UCN, then splice | `300-ucn-trigraph-ordering`, `100-line-splice-comment` |
| Backslash pairing | a `\` not starting a UCN consumes the next code point verbatim, so `\\u0041` keeps both backslashes but `\\A` forms a UCN | probe: 2/3/4 backslash prefixes |
| UCN validity | value <= 0x10FFFF, not a surrogate, and >= 0xA0 unless `$`, `@`, `` ` `` | `300-invalid-universal-character-value-bad`, probe `
` |
| Escape validation | strict `simple`/octal/`\x`+digits/`\u`+quad escapes | `300-string-hex-escape-bad`, `300-string-octal-escape-bad` |
| Whitespace | ASCII space, tab, VT, FF, CR; LF is `new-line` | probe `\x0b`/`\x0c`/`\r` |
| Non-ASCII whitespace | not whitespace; U+FEFF is an identifier body character mid-file | probe U+00A0/U+2028/U+FEFF |
| Raw strings | read physical code points; delimiter <= 16; invalid delimiter character and unterminated literal are errors | `200-trigraphs`, `300-raw-string-delimiter-bad`, probes |
| `header-name` | only after (start of line or `new-line`) (`#` or `%:`) `include`, with whitespace/comments ignored between | `200-header-name`, `200-alternative-include-header-context` |
| `<::` | split unless the following code point is `:` or `>` (end of file counts as "neither") | `400-angle-colon-madness`, probe `<::` at EOF |

## Remaining groups

1. (done this turn) Translation phases 1-3 for `pptoken`.
2. Later stages: reuse the same tokenizer for `preproc`, `posttoken` and
   `ppexpr`; those tools must register the two source basenames in
   `dev/frontend_source_sets.mk`. Not started.

## Performance evidence

PA1 has no benchmark or optimisation-level acceptance in `spec.md`; the
stage-scoped acceptance is the fixture contract.  The recogniser is a single
greedy pass over the code point stream.  Two transient vectors (trigraph output,
UCN output) are released before tokenization; the retained state is the decoded
physical code points plus a 16-byte record per translated code point.  No
measurements are claimed beyond that structural argument; nothing in this stage
is an optimization, so no A/B latency/RSS budget applies yet.

## Validation

- `make test-pa1` (54 fixtures + exit-status sidecars)
- `make test-report-through-pa1`
- `perl scripts/cppgm_file_audit.pl --stage pa1 --paths dev/src`

## Handoff ledger

- Turn start: 0/54 pa1 fixtures passing; every failure was the skeleton's
  `EXIT_NOT_IMPLEMENTED`, so no behaviour group was partially implemented.
- This turn delivers group 1 in full.
- Open questions for independent audit: none recorded yet.
