# PA2 (posttoken) plan and audit record

## Stage state

- Stage base commit: `40f09bbd882b86638fce4a6d42f0dbbeb763c815` (recorded on
  entry, before stage edits).
- Implementation commits: `8899a3cb`, `00a94e26`, `c8c41b7c`.
- Audit commits: `1017f183` (A1, A5, A6), `1e7b12d9` (A2), `1457280d` (A3), and
  the commit that last touched this file (A4 and this record).
- This document is the consolidated plan and audit record for `pa2`; the audit
  commit is the one that last touched this file.
- Target: `posttoken` runs translation phases 1-7's tokenization - phases 1-3
  through the shared frontend, phase 4 as the no-op the input requires, and
  phases 6-7 as a consumer of that stream.

## Design / spec alignment

`posttoken` is a staged tool over the shared frontend phase, not a second
frontend.  `dev/src/preprocess/tokens` (PA1) owns translation phases 1-3 and
already exposes them as a streaming cursor (`TranslatedSource` + `PPTokenizer`
+ `IPPTokenStream`); PA2 adds phases 4-7's tokenization as a *consumer* of that
stream:

```
stdin -> TranslatedSource -> PPTokenizer -> PostTokenStream -> sink -> stdout
```

- Phase 4 is a no-op (no directives allowed in the input).  Phase 6 string
  concatenation is the only place a post token outlives its callback, so
  `PostTokenStream` holds exactly one pending maximal string-literal group;
  every other preprocessing-token is classified and emitted inside its
  callback.  No vector of *tokens* is materialised - the pending group is a
  vector of `PendingLiteral` records plus one spelling buffer, and it is empty
  for every input that is not a run of adjacent string literals.
- `PostTokenStream` produces typed facts (`EFundamentalType`, decoded code
  units, `ETokenType`); the PA2 text format lives in the tool
  (`dev/posttoken.cpp`) behind `IPostTokenSink`, so the output requirement does
  not force any later phase to build text (spec §6, "writers are adapters for
  explicit tools").  No text roundtrip exists in the production path.
- Spellings arrive as borrowed `const std::string&` from the recogniser's one
  reused buffer, and the sink composes its lines in a 64 KiB block, so no
  per-token owning string is created on either side of the interface.
- Read-only, bounded tables (course simple-token table, literal escapes) are
  static and built once at static-initialisation; no process-global mutable
  cache exists (spec §8).
- `dev/frontend_source_sets.mk` registers the six `posttoken/*` sources for the
  `posttoken` tool only.

### Architecture audit trace

The specification's audit asks for a nontrivial declaration and a demanded
template traced from source to ELF.  PA2 has no declaration, template or
object, so the trace is the whole surface this stage owns.

A raw string inside a phase 6 group, `LR"a"b(a)b)x)a"b" "c"_x`, and a decimal
`pp-number` are traced together:

1. `ReadStandardInput` fills one buffer in 64 KiB blocks and moves it into
   `TranslatedSource`, which is its single owner.
2. Phase 1/2 rewriting is lazy: `CodeAt`/`DecodeAt` decode, splice, expand
   trigraphs and universal-character-names on demand, on a bounded lookahead
   window compacted in place.  The raw string's body is the exception - 2.2/1.3
   reverts those rewrites between the quotes - so `ScanRawStringLiteral` reads
   the delimiter and body from the untranslated buffer and `ResumeAt` puts the
   cursor back after the closing quote.
3. `ScanRawStringLiteral` validates the delimiter against 2.14.5's `d-char`
   set, finds the body by scanning for `)delim"`, and reports
   `emit_user_defined_string_literal` with the code-point spelling.
4. `PostTokenStream::AppendStringLiteral` opens or extends the pending group:
   it records the prefix, whether the literal was raw, whether it carried a
   ud-suffix, and the body/suffix offsets into `group_text_`.  It is the only
   place in the stage where a token outlives its callback.
5. The next preprocessing-token of any other kind (here `"c"_x` extends the
   group; `;` or eof would end it) reaches `FlushGroup`, which resolves the
   prefix union and the ud-suffix union of 2.14.5.13 / 2.14.8.8, encodes each
   body under the sequence's prefix, appends the terminating code unit and
   counts code units, then emits one fact.
6. `TextPostTokenSink` renders the fact into the output block.

The numeric path is the same shape: `emit_pp_number` -> `ClassifyPPNumber`
(longest match on one prefix of the spelling, then 2.14.2's type table) ->
`ScanFloatingLiteral` -> one `EmitLiteral`.  Each preprocessing-token is
classified exactly once, inside its own callback; there is no retry, no
whole-input rescan and no per-token owning allocation on the hot path beyond
the `std::string` locals that are small-string-optimised.

## Audit findings

Every finding below is reproducible from one `printf` and was found by an
instrument independent of the fixture suite; the fixture suite passed both
before and after each one.

| # | Surface | Class |
| --- | --- | --- |
| A1 | phase 3 raw-string delimiter | a double quote was rejected as a `d-char` |
| A2 | phase 7 hexadecimal floating-literal | converted with the wrong rounding |
| A3 | `PostTokenStream` hot path | three per-token allocations with no purpose |
| A4 | performance evidence | the noise floor was not a robust statistic |
| A5 | phase 3 raw-string delimiter length | the 16-character limit was read in the wrong unit |
| A6 | phase 3 escape scan | `\` followed by a NUL byte was rejected |
| A7 | phase 2 splice inside a UCN | reference defect, not copied (see below) |

### A1 - a raw-string delimiter containing `"` was rejected

```sh
printf 'R"a"b(x)a"b"\n' | dev/posttoken          # before: exit 1
printf 'R"a"b(x)a"b"\n' | posttoken-ref          # string-literal 12 R"a"b(x)a"b"
```

`IsRawDelimiterCodePoint` refused `"`.  N3485 2.14.5 (`doc/n3485.txt`:2113)
defines `d-char` as "any member of the basic source character set except:
space, the left parenthesis (, the right parenthesis ), the backslash \, and
the control characters representing horizontal tab, vertical tab, form feed,
and newline" - a double quote is *not* excluded, so `R"a"b(x)a"b"` is one
literal whose delimiter is `a"b`.  The reference frontend agrees.  The defect
is in the shared phase-3 recogniser and showed up through PA2's raw-string
surface: `dev/pptoken` reported `EXIT_FAILURE` for the same input.

Scale: `"` and `a"b` are 2 of the packed sweep's 12 raw delimiters, which is
1 260 of its 23 708 candidates.  The closing scan already looked for
`)delim"`, so the fix is to drop `"` from the rejected set.

### A2 - hexadecimal floating-literals rounded twice where the reference does

```sh
printf '0x1.0000000000000801p0\n' | dev/posttoken   # before: 010000000000F03F
printf '0x1.0000000000000801p0\n' | posttoken-ref   # 000000000000F03F
```

The literal is exactly 1 + 2049/2^64.  The stage converted it with `strtod`,
which is correctly rounded *for the literal's type* and gives the next double
above 1.0.  The reference reads a hexadecimal floating-literal as a
`long double` and narrows it, so the value first rounds to the 80-bit
neighbour 1 + 2^-53 and then, by round-half-to-even at the double halfway
point, down to 1.0.

Verified as a model, not by example: over 7 227 non-`long double` candidates,
`static_cast<T>(strtold(text))` reproduced the reference on all 7 227, while
`strtoT(text)` matched 7 185 - 42 divergences.  The two agree on every mantissa
short enough not to round at 80 bits, which is why `0x1.8p3`, `0x1p1024`,
`0x1p128f` and the ordinary cases never showed it, and the divergence needs at
least 16 fractional hexadecimal digits landing near a halfway point.

The reference's value is the double-rounded one and the stage's was the
correctly rounded one: 2.14.4 requires the correctly rounded value, so the
reference is the non-conforming side.  Unlike A7 this one is copied anyway,
because the two cases differ in what is at stake.  Here the *output* is the
thing being compared - the handout's PA2 rule is "bit-perfect compatibility
with test harness and reference implementation", the oracle is the reference's
byte image, and no checked-in fixture exercises the class - so matching costs
nothing that any requirement asks for, while the exception for correcting a
proven-wrong reference exists for outputs that a required behaviour depends on.
Decimal literals are unaffected (the reference's decimal result *is* the
correctly rounded one) and so is `long double`, where both sides use `strtold`.

### A3 - per-token allocations in the post-token pass

The specification's architecture audit makes "per-node hot allocation ... a
defect even when tests pass".  Three were present and none was load-bearing:

- `EmitPPNumber` copied the spelling into a fresh `std::string` before handing
  it to a scan that reads it by `const&`.
- `FlushGroup` materialised each element's ud-suffix with `substr` while
  deciding whether the elements agreed.  A group carries one suffix value or
  none, so only the first is materialised now and later elements are compared
  against the buffer in place.
- `EmitCharacterLiteral` built the suffix string before deciding the literal
  was invalid, so a rejected suffix allocated the string it was refused under.

One candidate was measured and rejected rather than assumed: the numeric scan
constructs a `std::istringstream` per floating-literal token, which looks like
a per-token allocation but is not - the stream and its buffer live in the
function's frame and the spelling is short enough for the small-string
optimisation.  No change was made.

### A4 - the noise floor was not a robust statistic

The benchmark took `max - min` of the A/A arm's paired per-block differences as
the noise floor.  A single delayed run moves that range by hundreds of
milliseconds, so the statistic is not a floor at all.  The recorded run
reported 0.0395 s and the ledger turned it into "~7x the noise floor"; a
re-run of the same protocol on a busy machine reported 0.3226 s (one A/A run
took 1.04 s instead of 0.65 s), which would have made the same result look
unresolved.  Both numbers are kept - the A/B result itself did not move.

The harness now reports, for both arms, the median absolute difference (the
robust floor), the full range, and the number of negative blocks; it also
reports how many A/A blocks reached the measured A/B effect, and its verdict
compares the A/B difference against the A/A *worst excursion* rather than a
multiple of the range.  All observations are still written to
`/tmp/posttoken_benchmark.samples.tsv`.

### A5 - the raw-string delimiter limit was read in the wrong unit

A delimiter is limited to 16 characters.  The reference keeps *two* counts of
it, and both are observable:

```sh
printf 'R"%s(y)%s"\n' "$(printf 'a%.0s' {1..17})" "$(printf 'a%.0s' {1..17})"
# both: ERROR: raw string delimiter is too long   (exit 1)
printf 'R"ééééééééé(y)ééééééééé"\n' | posttoken-ref
# invalid R"ééééééééé(y)ééééééééé"                (exit 0)
```

More than 16 **code points** is the `too long` error.  At most 16 code points
but more than 16 **bytes** is neither a literal nor an error: the whole
raw-string source, ud-suffix included, becomes one non-whitespace character and
phase 7 reports it as `invalid`.  This stage counted code points for both, so
it produced a literal where the reference produces `invalid` - a successful-run
stdout divergence, which the comparison rules treat as an oracle.

The reference's split is an artifact of a 16-byte delimiter buffer, but it is
the *only* definition of the accepted extension: 2.14.5 draws `d-char` from the
basic source character set, which contains no multi-byte character, so a
delimiter like `ééééééééé` is ill-formed either way and the standard's "16
characters" has nothing to say about it.  Reproducing the reference therefore
costs no required behaviour, and the two counts do.

Scale: measured over four code-unit widths (1, 2, 3 and 4 bytes) crossed with
1-20 code points, five encoding prefixes and three suffix shapes - 1 200 cases,
0 divergences after the fix.

### A6 - `\` followed by a NUL byte was rejected in phase 3

```sh
printf '"\\\0"\n' | posttoken-ref    # invalid "\<NUL>"   (exit 0)
printf '"\\\0"\n' | dev/posttoken    # before: exit 1
```

`SkipEscapeSequence` refused every unrecognised escape introducer, NUL
included.  The reference accepts a NUL there and reports `"<NUL>"` as an
ordinary string-literal preprocessing-token; phase 7 then refuses to decode it
and emits `invalid`.  The difference is exactly this one introducer value:
`"\p"` and `"\8"` are phase-3 errors in both frontends, and a NUL anywhere else
- inside a literal body, or outside a literal as its own token - is an ordinary
character in both.  A physical source character outside the basic source
character set is mapped in an implementation-defined manner (2.2/1.1), so the
reference's reading of NUL is the definition, and this stage now accepts the
escape in phase 3 and leaves the refusal to phase 7, which is what the
reference reports.

### A7 - a phase 2 splice inside a `\u` escape: reference defect, not copied

```sh
printf '"\\u\\\n0041"\n' | dev/posttoken     # literal "A" array of 2 char 4100
printf '"\\u\\\n0041"\n' | posttoken-ref     # ERROR: invalid escape sequence
```

The input is `"`, `\`, `u`, `\`, LF, `0`, `0`, `4`, `1`, `"`.  2.2/1.2 deletes
the backslash-newline, so the literal is `"A"` - a `c-char-sequence`
containing a `universal-character-name` (2.14.3), which is well-formed and names
`A`.  g++ confirms the language reading directly: the literal has size 2 and
its first byte is 65.

The reference's phase 1 recognises a `unicode-character-name` only when the hex
digits are contiguous bytes before phase 2, and its phase 3 escape scan has no
`\u` case, so a UCN that phase 2 assembles is rejected.  This stage recognises
a UCN from the translated stream, which is where 2.14.3 puts it, and accepts
the literal.

This is the one place where the reference and this stage remain different, and
it is deliberate: the reference rejects a valid program, [Testing and
references](../TESTING_AND_REFERENCES.md) says to "prefer the handout and C++11
standard to copying an erroneous reference result", and no checked-in fixture
and no mandated comparison exercises the class.  It is recorded here rather
than copied.  Comparing exit status is still the rule everywhere else; a
differential run that produces this input will report one mismatch, which is
this entry.

### Checked and not defects

- **A failed run prints fewer lines than the reference.**  `printf '123'\''456'`
  makes both tools exit 1; the reference has already streamed `literal 123`
  while this tool's 64 KiB block is discarded.  Not a defect:
  `scripts/compare_results_common.pl` `compare_text` returns success without
  comparing stdout whenever the reference's exit status is not `EXIT_SUCCESS`,
  and [Testing and references](../TESTING_AND_REFERENCES.md) says failed-case
  stdout is informational.
- **The `_`-leading ud-suffix rule.**  Course-defined, pinned by the checked-in
  fixtures `700-hard-string-concat` and `750-reserved-literal-operator-suffix`
  and by the reference; it is a lexical filter here and must not be re-derived
  by a later stage that sees the `operator""` declaration.
- **`0b`/`0B` binary integer literals.**  A C++14 form the course frontend
  takes; `pa29/tests/preproc/400-host-binary-integer-ud-literal` requires it.
- **Out-of-range floating-literals report the C library's value** rather than
  `invalid`.  The handout makes the range check optional and untested for PA2,
  and the reference reports the C library's result.
- **Self-containment.**  No `system`/`popen`/`exec`, no reference-binary
  invocation and no fixture-name recognition anywhere in the stage.

## Changes

| File | Change |
| --- | --- |
| `dev/src/preprocess/tokens/pp_tokenizer.cpp` | `IsRawDelimiterCodePoint` no longer rejects `"` (A1); the delimiter length keeps a code-point and a byte count and the second makes the token a non-whitespace character (A5); a NUL is accepted as an escape introducer (A6) |
| `dev/src/posttoken/pp_number.cpp` | hexadecimal floating-literals convert through `strtold` and narrow, matching the reference (A2); the deviation comment corrected |
| `dev/src/posttoken/post_token_stream.cpp` | drop the pointless spelling copy in `EmitPPNumber`; compare ud-suffixes in place instead of materialising each; decide the character-literal suffix refusal before building it (A3) |
| `dev/posttoken.cpp` | document why the read does *not* reserve the file size (see below) |
| `student.tests/posttoken_benchmark.pl` | robust noise floor, worst excursion, sign consistency and an A/A-reaches-the-effect count (A4) |

The read path keeps geometric growth after measuring the alternative:
reserving the length a regular file reports raised peak RSS on a 12.5 MB source
from 20.1 MB to 27.9 MB against the reference's 18.9 MB, because the buffered
read still asks for a chunk past the reservation and `std::string` then doubles
away from it.  The measurement is recorded at the call site so it is not
retried.

## Performance evidence

Protocol (spec §9): fixed binaries, flags and input; wall-time ABBA blocks; an
A/A arm measuring the reference against itself on the same schedule; paired
per-block differences with the robust noise floor described in A4; output
verified byte-identical to the reference before any timing is accepted; every
observation kept.  PA2 produces no executable, so only compiler latency and
peak RSS are reported - there is no generated-program runtime or text size at
this stage, and no compiler telemetry surface is invented to report them.

Corpus: `/tmp/posttoken_benchmark.txt`, 7 234 484 bytes, 40 000 generated blocks
of the phase 4-7 token mix.  `student.tests/posttoken_benchmark.pl 40000 12`.

| arm | latency (median [min..max]) | peak RSS (median [min..max]) |
| --- | --- | --- |
| `mine` | 0.3681 s [0.3636..0.3799] | 11.6 MB [11.5..11.6] |
| `ref`  | 0.6562 s [0.6506..0.6853] | 10.9 MB [10.9..11.0] |

- Paired per-block difference (mine - ref): -0.2885 s [-0.3178..-0.2733],
  **12 of 12 blocks negative**.
- A/A noise calibration: paired difference -0.0010 s [-0.0058..+0.0040], median
  absolute difference 0.0031 s, worst excursion 0.0058 s, and 0 of 12 A/A
  blocks reached the measured A/B effect.  The A/B difference is about 50 times
  the worst excursion the reference produced against itself.  The earlier
  recorded figures (difference -0.2914 s, A/A spread 0.0395 s) and the 0.3226 s
  re-run under load are kept in A4; this run is the one the table reports.
- Marginal cost of this stage: the same corpus through the stage-base PA1
  `pptoken` frontend takes 0.5736 s (median of 5; 11.8-11.9 MB) against this
  stage's 0.3668 s (median of 5; 11.8-11.9 MB), so phase 7 classification and
  phase 6 concatenation add no measurable latency or memory above the frontend.
  The comparison is a like-for-like tool-to-tool reading, not a controlled
  ablation: `pptoken` writes one `cout` insertion per token field where this
  tool fills a 64 KiB block, so the reported difference bounds the stage's
  added work from above rather than isolating phases 6-7.
- Peak RSS: the stage adds ~0.7 MB / +6% over the reference on this corpus.
  The budget below is a **self-selected diagnostic target, not a mandated
  limit**; the spec's requirement is that the addition be reported and stay
  bounded.  It is bounded by the 64 KiB output block, the pending string-literal
  group and the input buffer's geometric-growth slack, and the last of those is
  proportional to the source (measured 184 KB / 344 KB / 1052 KB on 0.73 MB /
  3.0 MB / 12.5 MB sources), i.e. O(n).
- No optimization was needed to reach the latency result: the win over the
  reference comes from the phases-1-3 streaming frontend inherited from PA1 and
  from emitting the text output in blocks.  No performance claim here depends
  on added compiler work, so there is no profitability budget to justify and no
  pipeline-wide work or growth budget is being drawn on.

## Validation

- `perl scripts/cppgm_file_audit.pl --stage pa2 --paths dev/src` - pass, 33
  files checked.
- `make test-report-through-pa2` - **80/80 tests, 2/2 stages** (pa1 54, pa2 26);
  earlier assignments remain passing.
- `make build` with the assignment's `-std=gnu++11 -Wall -O3` - warning-free for
  every source in the `posttoken` source set.
- `student.tests/posttoken_differential.pl 1500 SEED` for seeds 1-5 - no
  divergence in stdout or exit status.  Its curated list now pins the reducers
  for A1, A2, A5 and A6, so the classes the fixtures do not reach are
  reproducible from the committed harness rather than only from this audit's
  scratch corpora.
- Dense systematic sweep (packed one candidate per line, compared in chunks of
  500 and bisected only inside a failing chunk): 23 708 candidates covering
  every integer-suffix spelling crossed with the integer boundaries of all four
  bases, every floating shape crossed with floating- and ud-suffixes, character
  and string literals across all prefixes and suffixes, raw strings across 12
  delimiters, and the whole punctuator/keyword surface including near-misses -
  23 708 lines compared, **0 differing lines** on the final revision.
- `student.tests/pptoken_differential.pl` (2 000 inputs) and
  `student.tests/pptoken_byte_differential.pl` (4 000 byte-level inputs) - both
  clean, so the three phase-3 changes did not move the inherited PA1 contract.
- Raw-string delimiters: four code-unit widths (1, 2, 3, 4 bytes) crossed with
  1-20 code points, five encoding prefixes and three suffix shapes - 1 200
  cases, 0 divergences after A5.  A second sweep of 144 quote-in-delimiter
  shapes, from the independent fuzzer, also agrees.
- Hexadecimal floating-literals: 41 806 randomized candidates byte-identical to
  the reference after A2, covering normal, subnormal, overflow and boundary
  mantissas for `float`, `double` and `long double`.
- Decimal floating-literals: 3 000 many-digit candidates, no divergence; the
  out-of-range and saturated cases (`1e400`, `1.5e308`, `1e5000L`,
  `3.4028235677973370e38f`, `1.1897314953572317651e4932L`) all match.
- Independent adversarial fuzzing beyond the fixtures: roughly 600 000
  comparisons over 24 corpora covering raw strings, escapes, universal-
  character-names, phase 6 concatenation, phase 1-3 byte and splice handling,
  the pp-number grammar and the punctuator surface.  After A1, A2, A5 and A6
  the only remaining divergence is A7.

## Handoff ledger

- Implementation: complete for the PA2 contract, with the findings above fixed.
- Owned by later stages, stated so they are not re-derived from this one:
  - **Identifier interning.**  Spec §1 asks that identifiers be interned as
    they enter the frontend.  `posttoken` is a text-output tool whose contract
    is the spelling of every token, and phases 1-3 hand spellings through
    `IPPTokenStream` by borrowed reference with no owning string; the intern
    table has no consumer until the parser keys lookup on it.  The
    `simple_token` table is the bounded read-only metadata that lookup will sit
    beside, and this stage does not build a second index.
  - **The `_`-leading ud-suffix rule** is a lexical filter for this stage.  A
    later stage that sees the `operator""` declaration must not re-derive it
    from this table.
  - **Phase 4.**  It is a strict no-op here because PA2's input never contains a
    directive; the preprocessor that replaces it belongs to PA4.
  - **Phase-time and peak-memory telemetry.**  No counter surface is added at
    this stage: the spec's counters cover allocations, candidates,
    specialization transitions, caches, worklists and IR sizes, none of which
    exist yet, and the tool's only required output is the token stream.  Phase
    latency and peak RSS are obtained externally by the benchmark, which is the
    same observation without a surface the stage has no consumer for.
  - **The shared benchmark statistic.**  `student.tests/pptoken_benchmark.pl`
    still uses the A/A `max - min` range as its noise floor.  That is the same
    fragile statistic A4 fixed here.  PA1's recorded result is comfortably
    supported either way (0.0336 s against a 0.0059 s A/A range), so it was left
    untouched rather than reopening a closed stage; a later stage that
    re-derives either number should use the robust statistic.
  - **A7 is a deliberate divergence.**  A later stage that meets a phase 2
    splice inside a `\u` escape must not "fix" it by copying the reference: the
    input is a valid program (2.2/1.2, 2.14.3, g++), the reference rejects it,
    and the reasons are in A7.  It is the only input class where this stage and
    the reference disagree.
  - **The two count-based phase-3 rules** in A5 and A6 reproduce reference
    behaviour on inputs whose treatment is implementation-defined rather than
    standard-defined (2.14.5 `d-char` outside the basic source character set;
    2.2/1.1's mapping of a physical source character).  A later stage that
    widens those character sets should revisit them rather than assume they are
    language rules.
