# PA1 implementation plan and audit record

## Stage state

- Stage base commit: `927dce14` (recorded on first entry, before stage edits).
- Implementation commits: `79f49057`, `4a644e92`, `888410a0`, `78e6711b`.
- This document is the consolidated plan and audit record for `pa1`; the audit
  commit is the one that last touched this file.
- Target: `pptoken` implements translation phases 1, 2 and 3 of N3485 2.2/2.5.

At stage entry `dev/pptoken.cpp` was the untouched course skeleton
(`PPTokenizer::process` threw `NotImplementedException`), so all 54
`pa1/tests/*.t` fixtures failed with `EXIT_NOT_IMPLEMENTED` (0/54, 0/1 stages).

## Design and spec alignment

```
immutable byte buffer (one owner, never copied after the read)
  -> streaming phase 1/2 cursor: UTF-8 decode (Windows-1252 for a byte that
     cannot begin a sequence), trigraph, universal-character-name, line splice
  -> greedy phase 3 preprocessing-token recognition, one pass
  -> IPPTokenStream events (the tool's required textual output)
```

- `dev/src/preprocess/tokens/pp_source_translation.{h,cpp}` owns the byte buffer
  and applies phases 1 and 2 lazily as the tokenizer pulls code points. A
  translation unit costs its source buffer and a bounded lookahead window; the
  recogniser never asks for more than 10 code points ahead, and the window is
  compacted in place. No intermediate owning token vector exists — spec §1
  "streaming token cursor", §8 "cross-phase data is the minimal typed fact set".
- The line index is built on the first `LocationOf` call and `ReportLocation`
  skips the lookup unless the consumer asks for it
  (`IPPTokenStream::wants_source_location`). No PA1–PA4 consumer wants one, so
  the tool builds no position index and does no per-token lookup: spec §9
  "telemetry ... has separable overhead and does not trigger extra analyses
  solely for reporting".
- `dev/src/preprocess/tokens/pp_tokenizer.{h,cpp}` is the single greedy pass. It
  keeps the spelling of the token about to be reported and nothing else; every
  lookahead is a bounded constant.
- Raw string literals read the untranslated buffer, because 2.2/1.3 reverts the
  phase 1 and 2 rewrites between their quotes; the body is still decoded so the
  reported spelling is the code point sequence, not the raw bytes. `ResumeAt`
  continues the pipeline after the closing quote; the pipeline state at a raw
  string boundary is the same as at any token boundary, so no rewrite can span
  the resume point.
- `dev/pptoken.cpp` is the stdin wrapper: chunked read into one buffer, moved
  into the cursor; the tool has no options.
- `dev/frontend_source_sets.mk` registers
  `preprocess/tokens/pp_source_translation` and `preprocess/tokens/pp_tokenizer`
  for `pptoken` only. The other tools are separate source sets because their
  subjects do not exist yet.
- There is no text roundtrip in the production path: phases 1–3 share code
  points and byte offsets, and the one textual step is the tool's required
  output format (spec §6, "writers are adapters for explicit tools").
- No process-global mutable state, no caches, no `shared_ptr` in the hot path
  (spec §8).

### Architecture audit trace

The specification's audit asks for a nontrivial declaration and a demanded
template traced from source to ELF. PA1 has no semantic engine, so the trace is
the whole surface this stage owns:

`#include_next <a>` reached through a splice and a trigraph, e.g.
`??=inc\\\nlude_next <a>\n`.

1. `??=` is rewritten to `#` by `EffectiveAt`/`TrigraphReplacement` before the
   tokenizer sees it (2.4, 2.2/1.1).
2. `\` + LF is deleted by `Produce`, so `inc` and `lude_next` are one logical
   line (2.2/1.2).
3. `EmitOperator(1)` reports `#` and records the directive role;
   `ScanIdentifier` reports `include_next`, recognises it as an include
   directive word, and the directive context is armed.
4. `StartsHeaderName`/`ScanHeaderName` recognise `<a>` as one `header-name`,
   because the h-char-sequence is non-empty.
5. `IPPTokenStream` receives the events in order;
   `DebugPPTokenStream` renders them.

A raw string with a byte that cannot begin a UTF-8 sequence
(`R"(\x80)"`) is traced the same way: the prefix and opening quote come from the
translated cursor, the body is decoded from the immutable buffer, `\x80` becomes
U+20AC, and the spelling is re-encoded; `ResumeAt` puts the cursor past the
closing quote. `operator""s` is traced through the recogniser's
`after_operator_` state, which holds the empty string literal and the suffix
apart.

## Audit findings

The stage's stated method is that behaviour the handout leaves open is fixed by
probing `reference-binaries/pptoken`. Five places did not match that oracle.
Every one is reproducible from a single line.

| # | Reducer | Reference | Before the fix | Class |
| --- | --- | --- | --- | --- |
| F1 | `printf 'a\x80b\n'` | `identifier 4 a€b` (U+20AC) | exit 1 | phase 1 character mapping |
| F2 | `operator""s(int);` | `operator`, `string-literal ""`, `identifier s` | `user-defined-string-literal ""s` | literal-operator-id |
| F3 | `#include_next <math.h>` | `header-name <math.h>` | `<` `math` `.` `h` `>` | directive word |
| F4 | `0x1.5bf0a8b145769P+1` | one 20-byte pp-number | `0x1.5bf0a8b145769P` `+` `1` | pp-number exponent sign |
| F5 | `printf 'int a;\n' \| pptoken --batch-stdin` | the token stream | `EXIT_NOT_IMPLEMENTED` per line | skeleton stub |

**F1 — a byte in 0x80–0xBF was rejected instead of mapped.** These bytes can
never begin a UTF-8 sequence, so `SequenceLength` threw. The reference maps them
through Windows-1252 and re-encodes the result. The accepted physical source
character set is implementation-defined (N3485 2.2/1.1: "The set of physical
source file characters accepted is implementation-defined"), the course defines
the source character set as UTF-8, and the reference supplies the mapping the
whole course solution uses — `posttoken` does the same for `a\x80b`. Evidence:
for all 64 values the reference's code point is exactly the Windows-1252 entry,
with the five slots Windows-1252 leaves undefined (0x81, 0x8D, 0x8F, 0x90, 0x9D)
mapping to themselves. Scale: 958 of 3000 uniformly random byte-level inputs
diverged before the fix. The atom-based harness never produced this class: every
one of its atoms is either well-formed UTF-8 or an invalid lead byte, and a byte
that can never begin a sequence is neither.

**F2 — `operator""s` was not split.** Longest-match makes `""s` one
user-defined-string-literal, but a literal-operator-id is
`operator "" identifier` ([over.literal]), so the empty string literal has to
survive as its own token or the parser can never see it. The trigger measured
against the reference: the previous token is the identifier `operator`
(insensitive to intervening whitespace, comments and new-lines), and the literal
is empty, carries no encoding prefix and is not raw. `operator"a"s`,
`operator L""s`, `operator u8""s`, `operator R"(a)"s`, `operator''s` and
`operatorfoo""s` all stay whole. Real translation units depend on this:
`/usr/include/c++/13/bits/basic_string.h` declares `operator""s`.

**F3 — `#include_next` was not an include directive word.** The reference treats
it exactly like `include`, and nothing else: `#including`, `#includeX`,
`#includefoo` and `#import` are not directives that take a header-name. This is
the GNU extension the library headers are written in terms of —
`/usr/include/c++/13/math.h` and the other C-compatibility headers use it.
Header-name recognition itself is unchanged (the non-empty h-char/q-char
requirement, `%:`, start-of-line and the whitespace/comment context).

**F4 — a hexadecimal pp-number did not take a binary-exponent sign.** `+`/`-`
after `p`/`P` belongs to the pp-number only when it began with `0x`/`0X`, which
is the only place a binary exponent can appear; `1P+3`, `1p+3`, `10x1P+1`,
`00x1P+1` and `.0x1P+1` still split. `0x1P+`, `0xP+P`, `0xx1P+1` and
`0x1P+1P+1` join, so the rule is the prefix and not the surrounding characters.
Real translation units depend on this: `/usr/include/llvm-21/llvm/Support/MathExtras.h`
writes `0x1.5bf0a8b145769P+1`. The decimal rule (`e`/`E` sign in any pp-number)
is unchanged and the fixture `100-pp-number-exponent-sign-boundary` still pins
it.

**F5 — the skeleton's `--batch-stdin` stub survived the implementation.** Every
other not-yet-written tool answers that flag with a per-line
`EXIT_NOT_IMPLEMENTED` record; an implemented tool must not, and the reference
tokenizes normally. The harness only passes the flag together with
`WRAPPED_BATCH_STDIN=1`, which the test runner intercepts before `main`, so this
was latent rather than failing — but a tool that reports "not implemented" for
every request would silently fail if the routing ever changed.

### Harness findings

- `student.tests/pptoken_differential.pl` claimed in its own header that it
  reports "any difference in stdout or exit status", but skipped every status
  disagreement (`next if $mine_exit != $ref_exit`). The whole F1 class was
  therefore invisible to it. It now reports status differences, and it checks
  the command-line surface (F5) against the reference before the fuzz runs.
- The atom alphabet could never produce bytes that are not UTF-8, which is why
  F1 needed a different instrument. `student.tests/pptoken_byte_differential.pl`
  now drives the frontend with arbitrary byte sequences, weighting the bytes
  that cannot begin a UTF-8 sequence.

## Changes

| File | Change |
| --- | --- |
| `dev/src/preprocess/tokens/pp_source_translation.cpp` | Windows-1252 fallback for a stray byte in `DecodeAt`; line index built lazily; dead `kByteOrderMark` removed |
| `dev/src/preprocess/tokens/pp_source_translation.h` | document the lazy index and the accepted-byte rule |
| `dev/src/preprocess/tokens/pp_tokenizer.{h,cpp}` | `IsIncludeDirectiveWord` (`include`, `include_next`); `after_operator_` state and the `""suffix` split; `HasHexadecimalPrefix` and the binary-exponent sign; raw-string body decoded rather than byte-copied; `ReportLocation` gated on `wants_source_location` |
| `dev/src/preprocess/tokens/IPPTokenStream.h` | `wants_source_location()` hook |
| `dev/pptoken.cpp` | drop the `--batch-stdin` not-implemented stub and its unused include; `sync_with_stdio(false)` |
| `student.tests/pptoken_differential.pl` | report exit-status differences; check the CLI surface; curated reducers for F1–F4 |
| `student.tests/pptoken_byte_differential.pl` | new byte-level differential (F1) |
| `student.tests/pptoken_benchmark.pl` | A/A noise calibration, paired per-block differences, higher-resolution timing, all observations kept |

Correctness is otherwise unchanged: identical stdout and exit status against the
reference on every fixture, on 5982 real translation units, and on every
differential corpus below.

## Performance evidence

Frozen A/B: `dev/pptoken` (g++ -std=gnu++11 -O3) against
`reference-binaries/pptoken`; a fixed 2 900 390-byte generated translation unit
exercising identifiers, pp-numbers (including hexadecimal exponents),
punctuation, comments, escapes, raw strings and header-names; output verified
byte-identical before any timing is accepted. Wall-time ABBA blocks, an A/A arm
measuring the reference against itself for the noise floor, and every
observation written to `/tmp/pptoken_benchmark.samples.tsv`.

`perl student.tests/pptoken_benchmark.pl 40000 10`:

| Arm | Latency (median) | Peak RSS (median) | Paired per-block difference |
| --- | --- | --- | --- |
| A/B | ref 0.2724 s, mine 0.2389 s | ref 7.3 MB, mine 7.6 MB | −0.0336 s [−0.0375..−0.0297] |
| A/A | ref 0.2712 s, "mine" 0.2712 s | 7.3 MB, 7.3 MB | −0.0003 s [−0.0028..+0.0031] |

The A/A spread (0.0059 s) is the noise floor; the measured difference is 5.7
times it, so the latency result is repeatable rather than an artefact of the
harness.

Our own revisions were also A/B'd directly, which is the comparison the
specification's frozen-binary rule is about. The stage commit `78e6711b` was
built into a separate tree and run against the audited binary over a frozen
2 899 751-byte corpus that both revisions accept (the stray-byte line only the
audited binary handles is excluded), 10 ABBA blocks each:

| Arm | Base (pre-audit) | Audited | Paired per-block difference |
| --- | --- | --- | --- |
| A/B | 0.2851 s | 0.2372 s | +0.0479 s [+0.0423..+0.0524] |
| A/A | 0.2866 s | 0.2859 s | +0.0004 s [−0.0102..+0.0195] |

A 16.8% latency reduction, seven times the A/A spread. Two changes account for
it, measured by re-running the same protocol between commits: the output path
(`sync_with_stdio(false)`) took the 6-block figure from 0.2855 s to 0.2447 s
(≈14 points) and the telemetry opt-in from 0.2447 s to 0.2345 s (≈4 points).
The earlier materialized pipeline, retained for scale, was 0.58–0.74 s and
147.6 MB; the streamed cursor the stage already had was 0.280 s and 7.5 MB.

Memory is not a regression. On an empty input the tool maps 3764 KB against the
reference's 3576 KB — a 188 KB static difference present with no source at all.
Over the 2.90 MB corpus it grows 3956 KB against the reference's 3928 KB (0.7%
apart, same mechanism: one geometric source buffer). There is no generated
executable in this stage, so runtime and text size do not apply; they are owned
by the stages that emit code.

Legality, profitability and invalidation: neither latency change is a transform
over IR, so there is no legality proof, no invalidation surface and no growth
budget to check. `sync_with_stdio(false)` removes a forwarding layer over stdio
and cannot change a byte of output — verified by byte-comparison on the corpus,
all 54 fixtures, the self-tokenized frontend, and 5982 real headers. The
telemetry gate removes work whose result no consumer reads; nothing is added in
its place. No pass was added that could trade code growth for runtime.

Declined, with evidence: the profile now attributes ~45% of the time to
`std::ostream` formatting in `DebugPPTokenStream`, which the reference pays too.
Collapsing each token into one buffered `write` is worth roughly another 10–15%
of latency, but that file is course-provided presentation code and no mandated
limit asks for it, so the measurement is recorded rather than acted on.

**Stage-scoped acceptance.** No latency, memory or code-size limit is mandated
for PA1: the mandated acceptance is the fixture contract plus the file audit, and
both pass unchanged. Nothing here reclassifies a mandated limit, weakens
coverage, or corrects a reference output. The self-selected targets the earlier
plan carried (beat the reference's RSS, hold latency under the pre-rewrite
figure) are reclassified as diagnostics: the reference's RSS is matched within
its own static footprint, and latency is now below the reference's, so neither
is an exit gate.

## Validation

- `make test-report-through-pa1`: 54/54 tests, 1/1 stages.
- `perl scripts/cppgm_file_audit.pl --stage pa1 --paths dev/src`: pass, 20 files.
- `make build`: all nine tools link.
- All 54 fixtures match the reference on exit status and, when the input is
  accepted, on stdout. Eight `.ref` files differ from the reference's stdout;
  all eight are `EXIT_FAILURE` fixtures, where failed-case stdout is an
  informational example (TESTING AND REFERENCES.md). They are preserved, not
  corrected.
- Differentials, each comparing stdout and exit status with the reference:
  - `student.tests/pptoken_differential.pl`: 30 000 inputs, 0 divergences.
  - `student.tests/pptoken_byte_differential.pl`: 20 000 arbitrary byte
    sequences, 0 divergences.
  - exhaustive two-byte sweep: 65 536 inputs, 0 divergences.
  - exhaustive three-byte sweep over a 24-byte alphabet:
    13 824 inputs, 0 divergences.
  - systematic literal-shape sweep: 16 937 inputs covering pp-number prefixes
    and exponent signs, escapes, raw strings and delimiters, header-name forms,
    directive spellings and operator-literal-ids, 0 divergences.
  - recursive real-translation-unit sweep over `/usr/include`,
    `/usr/lib/llvm-21/include` and `/usr/local/include`: 5 982 files,
    70 576 411 bytes, 0 divergences.
  - mutation fuzz over real headers (stray bytes, trigraphs, splices, directive
    and operator-literal insertions): 4 000 inputs, 0 divergences.
- Self-tokenizing: `dev/pptoken` run over its own seven frontend sources matches
  the reference token for token.
- The differentials and the benchmark are personal tests; they are not
  discovered by `make test` and do not replace the course contract.

## Remaining groups

1. (done) Phase 1–3 for `pptoken`, streamed, matched against the reference over
   valid and invalid input.
2. Later stages: register the two source basenames for `preproc`, `posttoken`,
   `ppexpr` and `cppgm++` in `dev/frontend_source_sets.mk` and drive the same
   cursor from those tools. Not started; needs the PA2 handout.
3. Later stages, not waived: the specification's section 9 telemetry (phase
   time, peak memory and work counters) and its full benchmark set (loops,
   calls, memory access, floating point, self-hosting) belong to the stages that
   own those phases. This stage ships the frontend benchmark and the
   `wants_source_location` pattern for separable telemetry, because no other
   phase exists yet and an in-process counter nobody reads would itself be the
   overhead section 9 forbids.

## Handoff ledger

- Turn start: 54/54 passing on the fixture contract, 0/1 stages failing.
- Audit end: 54/54 passing; earlier assignments not applicable (PA1 is first).
- Fixed this turn: F1–F5 above, plus the two harness defects. Every fix is
  covered by a differential input, and F1–F4 by curated cases that run first on
  every differential invocation so a regression is reported even when the seed
  changes.
- Boundary of this handoff: PA1's phase 1–3 surface is complete and matched
  against the reference over valid and invalid input. The tool is still not
  driven from `preproc`, `posttoken`, `ppexpr` or `cppgm++`; that is the next
  group and needs the PA2 handout.
- Open questions for independent audit, none waived:
  - The physical byte column reported by `set_source_location` is a documented
    self-selected definition; no fixture constrains it and no consumer reads it
    in this stage. A later stage that consumes locations may want a different
    unit — and must then override `wants_source_location`, or it will silently
    see no positions.
  - `<::`, the empty-header-name fallback, F1–F4 and the `operator""` split are
    matched against the reference wrapper rather than derived from fixture text.
    Where the standard speaks the implemented reading follows it (2.2/1.1 for
    the accepted physical character set, [over.literal] for the
    literal-operator-id, [lex.pptoken] longest-match); `include_next` is a GNU
    extension that the course solution implements throughout, and the reference
    could be wrong about any of them.