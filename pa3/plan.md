# PA3 (ppexpr) plan and audit record

## Stage state

- Stage base commit: `2b52b6e3306f9748c62914e64eec99d6317c9156` (recorded on
  entry, before stage edits).
- Last reviewed commit: `2b52b6e3306f9748c62914e64eec99d6317c9156`.
- Implementation commits: `16f7d2a4` (the evaluator), `02cd7532` (F1, F2),
  `93de55c2` (F3 and the nesting bound). Cleanups: `3894d18b`, `058b4a1f`,
  `b8184ea6`. This document is the consolidated plan and audit record for `pa3`;
  the commit that last touched it is the one that wrote this record.
- Target: `ppexpr` evaluates one controlling expression per logical source line
  and prints the result, `error`, or nothing for an empty line, then `eof`.

## Design / spec alignment

`ppexpr` is a third consumer of the phase 1-3 frontend, not a second frontend:

```text
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
  no second token representation is built, and an identifier's spelling is not
  copied - the two facts derived from it (`is_defined_word`, the handout's mock
  `defined` parity) are decided while the callback's spelling is still alive.
- `CtrlExpression` is a hand-written predictive parser over that buffer. One
  precedence-climbing loop covers all the grammar's binary levels (F3), and
  `?:` is the one production the loop does not: it sits below `||` and its arms
  are full `controlling-expression`s.
- Evaluation is lazy in exactly the two places the language is - the right
  operand of `&&`/`||`, and the arm of `?:` the condition did not choose. The
  handout's course-defined value errors (division by zero, the one signed
  quotient with no representation, a signed `+`/`-`/`*`/unary `-` that
  overflows, a shift count outside `[0, 64)`) are raised only when the
  sub-expression is evaluated; a dead sub-expression is still parsed and still
  typed, because 5.16's result type comes from both arms (`260-cond-ret-type`).
- All arithmetic is on the two promoted types the handout defines
  (`intmax_t`/`uintmax_t`), represented as one 64-bit image plus a signedness
  bit, so no per-node type object is built and no owning string is created on
  either side of the interface.
- Read-only tables only (the operator/precedence table, the fundamental-type
  tables); no process-global mutable cache.
- `dev/frontend_source_sets.mk` registers the `ctrl_expr/*` sources and the
  inherited `posttoken/*` sources for the `ppexpr` tool only.

### Architecture audit trace

The specification's audit asks for a nontrivial declaration and a demanded
template traced from source to ELF. PA3 has no declaration, template or object,
so the trace is the whole surface this stage owns: the path a controlling
expression's worst case takes.

`1 ? defined (a) : -9223372036854775807 - 1` on one logical line, followed by
one whose only token is invalid and one that is blank:

1. `ReadStandardInput` fills one buffer in 64 KiB blocks and moves it into
   `TranslatedSource`, which is its single owner. Phase 1/2 rewriting stays
   lazy on a bounded lookahead window (PA1).
2. `PPTokenizer` reports the preprocessing-tokens to `CtrlExprStream`, which
   forwards each to `PostTokenStream`. Spellings arrive as borrowed
   `const std::string&` from the recogniser's one reused buffer and never
   become owning strings here.
3. `PostTokenStream` classifies each token inside its own callback and reports
   the typed fact to `CtrlExprSink::EmitSimple`/`EmitIdentifier`/`EmitLiteral`.
   `EmitLiteral` promotes the literal in place; `EmitIdentifier` decides
   `is_defined_word` and the mock parity from the spelling while it is alive.
   Nothing but a `CtrlToken` record is retained.
4. `emit_new_line` reaches `CtrlExprStream::EndLine`, which calls
   `PostTokenStream::FinishGroup` (a pending maximal string-literal sequence
   may not cross a logical line) and then `CtrlExprSink::EndOfLine`.
5. `EndOfLine` reports `error` for a line the emit-time rules rejected, and
   otherwise runs `CtrlExpression::Evaluate` over the line's records. The
   parser walks the token buffer once; the conditional's untaken arm is parsed
   and typed but not evaluated, so the `-9223372036854775807 - 1` in the arm
   that is not chosen neither overflows nor is skipped in the type computation.
6. The result is appended to the sink's output block, which is written to the
   stream in 64 KiB pieces. A line whose only token is invalid (`#`) reaches
   step 5 with an empty buffer and a set rejection flag and prints `error`; a
   blank line reaches it with both clear and prints nothing.

Each preprocessing-token is classified exactly once, each line's tokens are
parsed exactly once, and there is no text roundtrip, no retry and no per-token
owning allocation. The one per-line allocation is the token vector, whose
capacity is retained across lines.

## Audit findings

Every finding below is reproducible from one `printf` and was found by an
instrument independent of the fixture suite; the fixture suite passed both
before and after each one.

| # | Surface | Class |
| --- | --- | --- |
| F1 | `?:` arm typing | a comparison's result type was only set when it was evaluated |
| F2 | signed overflow | `+`, `-`, `*` and unary `-` wrapped where the reference errors |
| F3 | precedence levels and nesting | twelve parser frames per level; an unbounded, `ulimit`-dependent stack |

### F1 - a `?:` arm's comparison was typed unsigned

```sh
printf "0 ? 1u == 1u : 1\n" | dev/ppexpr     # before: 1u
printf "0 ? 1u == 1u : 1\n" | ppexpr-ref     # 1
```

5.9 and 5.10 give a relational or equality operator the type `bool`, so its
result is signed `intmax_t` however unsigned its operands are. The first cut
set that result type only after the dead-branch early return, so a comparison
inside the arm a `?:` did not choose came back unsigned, and 5.16 then made the
whole conditional unsigned - a wrong *type*, and with it a wrong `u` suffix, on
a fully evaluated expression.

Scale: the fuzzer found it in 1 of 1 500 random inputs; the systematic sweep's
conditional arm matrix (9 conditions x 40 x 40 operands) covers the class. The
reducer is in `student.tests/ppexpr_differential.pl`.

The fix is the general one, not the case: `ApplyBinary` decides the result type
of every operator before the dead-branch return, because a dead arm is still
typed.

### F2 - a signed overflow wrapped where the reference errors

```sh
printf "9223372036854775807 + 1\n" | dev/ppexpr   # before: -9223372036854775808
printf "9223372036854775807 + 1\n" | ppexpr-ref   # error
printf "9223372036854775807 + 0\n" | ppexpr-ref   # 9223372036854775807
printf "1 << 63\n"                 | ppexpr-ref   # -9223372036854775808
```

A signed `+`, `-`, `*` whose mathematical value is not representable in
`intmax_t` is a course-defined error, the same treatment `500-integer-overflow`
gives `(1 << 63)/-1`; an unsigned one wraps, as 5.5/5.6/5.7 do modulo 2^64.
Shifts are exempt: `1 << 63` is the most negative value, not an error, and
`2 << 62` is not one either. Unary `-` is in the class too - the only negand
with no result is `INT64_MIN`:

```sh
printf -- "-(-9223372036854775807 - 1)\n" | ppexpr-ref   # error
printf -- "+(-9223372036854775807 - 1)\n" | ppexpr-ref   # -9223372036854775808
```

Scale: the fuzzer found three instances of the binary form over 1 500 inputs and
the sweep's operator matrix (18 operator spellings x 40 x 40 operands x 4
shapes) covers it. The reference's boundary was mapped before the fix by hand
over 16 operand pairs - `2 * 9223372036854775807`, `4611686018427387904 * 4`,
`-4611686018427387904 * 2`, `-9223372036854775807 - 1`, `1 << 63`, `0x8000…+1`
- which is what separates "signed overflow" from "shifts too" and from "unsigned
wraps". The reducers are in the curated list.

The check is made with `__builtin_add/sub/mul_overflow`, which defines each
operator exactly and keeps this code from relying on signed overflow of its
own. Both toolchains the repository's targets use accept it (g++ and clang++,
verified on the parser translation unit).

### F3 - one function per precedence level, and an unbounded parser stack

The grammar names one production per precedence level, so the first cut wrote
one function per level. The handout's own design note asks for the opposite:

> You should be aware that there is a technique in which you can collapse the
> calls to all the different binary operator expressions into one call. This is
> achieved by keeping a precedence table and making decisions about how to build
> the parse tree based upon it.

The stage now has that one loop over a table of `{token, operation,
precedence}`. `&&` and `||` keep their laziness inside the loop, because that is
the only place an operand's liveness is decided by a value rather than by the
schedule. Two things came out of it.

The first is cost. A nested expression needs four parser frames per level
instead of twelve; on the same 10.7 MB corpus the stage went from 0.6815 s to
0.6268 s (medians of 20 timed runs each; the A/A noise floor is 0.0023 s).

The second is correctness. Recursive descent grows the C stack with nesting, and
nothing bounded it, so the usable depth was a property of `ulimit -s` and the
failure was a SIGSEGV that lost the whole run's output:

```sh
perl -e 'print "(" x 20000, 1, ")" x 20000, "\n"' | dev/ppexpr   # before: no output at all
```

Measured before the fix: 6 000 nested parentheses evaluated, 8 000 died. After
the collapse the wall moved to between 20 000 and 25 000 - still a crash, just a
later one. The parser now counts its own frames (`DepthGuard` in
`ctrl_expr_parser.h`, incremented at the two places that recurse: the
parenthesis/conditional level and the prefix-operator chain) and reports an
expression past the bound as an invalid controlling expression:

```sh
perl -e 'print "(" x 4095, 1, ")" x 4095, "\n"' | dev/ppexpr   # 1
perl -e 'print "(" x 4100, 1, ")" x 4100, "\n"' | dev/ppexpr   # error, exit 0
```

The bound is 8 192 parser frames, which is 4 095 nested parentheses, 8 192
chained prefix operators or 4 096 conditional levels. A parenthesis level costs
two bytes of input, so the smallest input the bound can reject is a single 8 KB
expression; clang's default maximum bracket depth for comparison is 256.
`student.tests/ppexpr_differential.pl` generates both sides of the bound and
checks that inside it the two tools agree exactly and outside it this stage
reports `error` with a normal exit status and an untruncated output.

`DepthGuard` is deliberately not the *whole* answer to F3 (see the ledger): the
reference parses nesting from the heap and computes a value at any depth, so a
bound is a deviation, not a match. It replaces a `ulimit`-dependent crash with a
deterministic diagnostic, which is what every production compiler does, and the
remaining option - an explicit-stack parser - is recorded as the open question
rather than claimed.

## Checked and not defects

- **`--batch-stdin`.** The harness's worker flag is stripped before a real
  invocation, and the test runner's `main` consumes it when
  `WRAPPED_BATCH_STDIN` is set; the tool itself ignores its arguments, exactly
  as `pptoken` and `posttoken` do. The `ppexpr` *reference* binary is the one
  tool in the tree that answers an unknown argument with
  `ERROR: invalid usage`, so the flag is compared against this tool's own plain
  run rather than against the reference.
- **A course-defined value error in a dead arm.** `true?5:5/0` is `5` and
  `0&&(5<<64)` is `0` in both tools; the handout makes these errors of
  *evaluation*, not of the line. The checked-in `250-eval-order` fixes all four
  shapes (`?:`, `&&` and `||` in both directions) and `260-cond-ret-type` fixes
  a dead arm's type, and the same liveness is what makes F2's
  `9223372036854775807 + 1` a value rather than an error when it stands in an
  arm the condition did not choose (probed on all four shapes after F2).
- **A rejection anywhere on the line rejects the line.** The handout's
  "check that there are no `invalid` tokens and that all `literal` tokens are
  of `integral-literal` type" is a property of the token sequence, not of the
  evaluation: `true?5:3.2` is `error` and `true?5:"x"` is `error` in both
  tools, even though the arm is dead. The stage decides this at emit time, which
  is the same rule without a second pass.
- **A line that carries only a comment or whitespace produces no output line**,
  which is why `CtrlExprSink::EndOfLine` distinguishes an empty token buffer
  from a rejected one: an empty line and `/* blank */\n` print nothing, but a
  line whose only token is invalid (`#`, `@`) prints `error`.
- **`9223372036854775808` is `invalid` rather than a value.** That is 2.14.2's
  decimal rule (a decimal literal with no suffix has only signed candidate
  types) and it arrives from the inherited PA2 classifier, unchanged.
- **Phase 1-3 failures exit `EXIT_FAILURE`**, as `pptoken` does; the line output
  already written is informational then, and the stage keeps that behaviour
  because it is `pptoken`'s. The sweep's one-per-file list compares the exit
  status of both tools over an unterminated character literal and string
  literal, an unterminated raw string, a line splice, a malformed UCN, `%:` and
  `??/`.
- **Self-containment.** No `system`/`popen`/`exec`, no reference-binary
  invocation and no fixture-name recognition anywhere in the stage.

## Changes

| File | Change |
| --- | --- |
| `dev/src/preprocess/ctrl_expr/ctrl_expr_token.h` | new: the line's compact token record and the handout's mock `defined` parity |
| `dev/src/preprocess/ctrl_expr/ctrl_expr_stream.{h,cpp}` | new: the logical-line splitter and the sink that accumulates and evaluates a line |
| `dev/src/preprocess/ctrl_expr/ctrl_expr_parser.{h,cpp}` | new: the precedence-climbing parser, the laziness, and the arithmetic |
| `dev/ppexpr.cpp` | the tool: read stdin, run the pipeline, flush |
| `dev/src/posttoken/post_token_stream.{h,cpp}` | `FinishGroup` made public for the logical-line boundary |
| `dev/src/posttoken/fundamental_type.{h,cpp}` | `FundamentalTypeIsSigned` / `FundamentalTypeIsIntegral` |
| `dev/frontend_source_sets.mk` | register `ctrl_expr/*` and the inherited `posttoken/*` for `ppexpr` |

## Performance evidence

Protocol (spec §9): fixed binaries, flags and input; wall-time ABBA blocks; an
A/A arm measuring the reference against itself on the same schedule; paired
per-block differences with a robust noise floor; output verified byte-identical
to the reference before any timing is accepted; every observation kept.
PA3 produces no executable, so only compiler latency and peak RSS are reported -
there is no generated-program runtime or text size at this stage, and no
compiler telemetry surface is invented to report them.

Corpus: `/tmp/ppexpr_benchmark.txt`, 10 669 643 bytes, 40 000 generated blocks of
controlling expressions (operator precedence chains, mixed signedness, `defined`,
conditionals, every character-literal prefix, and a few lines the course
definition rejects). `student.tests/ppexpr_benchmark.pl 40000 5`.

| arm | latency (median [min..max]) | peak RSS (median [min..max]) |
| --- | --- | --- |
| `mine` | 0.6268 s [0.6233..0.6360] | 19.6 MB [19.5..19.6] |
| `ref`  | 0.9891 s [0.9840..1.0161] | 18.5 MB [18.4..18.6] |

- Paired per-block difference (mine - ref): -0.3620 s [-0.3655..-0.3597],
  **5 of 5 blocks negative**.
- A/A noise calibration: paired difference -0.0005 s [-0.0364..+0.0025], median
  absolute difference 0.0023 s, worst excursion 0.0364 s, and 0 of 5 A/A blocks
  reached the measured A/B effect. The A/B difference is about 10 times the
  worst excursion the reference produced against itself.
- Marginal cost of this stage: the same corpus, read by the three tools in one
  interleaved loop (medians of 7; `dev/ppexpr` 0.62 s / 19.59 MB, the reference
  0.98 s / 18.57 MB, the stage-base PA2 `posttoken` 0.70 s / 19.59 MB). The
  controlling-expression evaluation therefore adds no measurable latency or
  memory above the phases 1-7 frontend it is built on, and the two readings
  agree with the ABBA table. The comparison is a like-for-like tool-to-tool
  reading, not a controlled ablation: `posttoken` writes one line per token
  where this tool writes one per logical line, so the reported difference
  bounds the stage's added work from above rather than isolating it.
- The F3 collapse is the one change made *for* performance, and it is on this
  table: the same corpus and schedule read 0.6815 s before it and 0.6268 s
  after, against a 0.0023 s noise floor. Nothing else here depends on added
  compiler work, so there is no other profitability budget to justify.
- Peak RSS: the stage reads 1.2 MB / +6% over the reference on this corpus, and
  the delta grows with the source - 1 208 KB / 2 056 KB / 4 056 KB on
  10 669 643 / 21 339 286 / 42 678 572 bytes of input, i.e. O(n) with a slope of
  ~0.12 bytes of RSS per source byte. It is the input buffer's
  geometric-growth slack, not the stage: the stage-base `posttoken`, which
  shares the read path, reads 19.59 MB to this stage's 19.59 MB on the same
  corpus. PA2 measured the alternative at the read site and rejected it
  (reserving the file length raised peak RSS on a 12.5 MB source from 20.1 MB
  to 27.9 MB), so this is an inherited cost with a recorded reason, and the
  comparison is a **self-selected diagnostic target, not a mandated limit**. The
  spec's requirement is that the addition be reported and stay bounded; it is,
  and it is bounded by a quantity proportional to the source, which is what §9
  asks preprocessing to be.

## Validation

- `perl scripts/cppgm_file_audit.pl --stage pa3 --paths dev/src` - pass, 38
  files checked.
- `make test-report-through-pa3` - **100/100 tests, 3/3 stages** (pa1 54, pa2 26,
  pa3 20); earlier assignments remain passing.
- `make build` with the assignment's `-std=gnu++11 -Wall -O3` - warning-free for
  every source in the `ppexpr` source set. The parser translation unit also
  compiles with `clang++ -std=gnu++11 -Wall -O3`, which is the other toolchain
  the repository's targets use and the one that has to accept the overflow
  builtins.
- `student.tests/ppexpr_differential.pl 1500 SEED` for seeds 1-18 - no
  divergence in stdout or exit status. Its curated list pins the reducers for
  F1, F2 and the nesting bound, so the classes the fixtures do not reach are
  reproducible from the committed harness rather than only from this audit's
  scratch corpora.
- `student.tests/ppexpr_sweep.pl` - 190 694 systematic candidates: every binary
  operator spelling (punctuator and alternative) crossed with 40 operands and
  four parenthesisation shapes, every unary spelling crossed with the same
  operands, the conditional's condition/arm matrix, the `defined` forms over 21
  operands, seven white-space/comment separators, and every rejected token kind
  planted at ten skeleton positions. Candidates are packed one per line into
  400-line chunks, so the sweep costs ~950 process runs rather than one per
  candidate; each chunk asserts that neither tool failed a translation phase and
  that the output has one line per candidate, and a differing chunk is then
  reduced line by line. **0 differing candidates** on the final revision.
- Independent adversarial probing beyond both harnesses: nesting to 2 000 000,
  which is what showed the reference has no bound at all (F3); 200 000 chained
  prefix operators; 20 000-term operator chains; `defined` with a comment
  between the operator and its operand; a NUL byte and a stray `;` inside an
  expression; splices inside a logical line; and the phase 1-3 failures. All
  agree except the bounded nesting class.

## Handoff ledger

- Implementation: complete for the PA3 contract, with the findings above fixed.
- **F3's residual, and the one open question.** The reference parses nesting
  from the heap and computes a value at any depth; this stage bounds nesting at
  8 192 parser frames and reports `error` past it. Closing the gap means an
  explicit-stack parser - the shunting-yard/operator-precedence form with a heap
  value and operator stack - which would also have to preserve the laziness of
  `&&`, `||` and `?:`, since a parser that evaluates on pop cannot skip an
  operand. That is a real rewrite of the stage's control flow, and it buys only
  inputs no fixture, no realistic translation unit and no later stage's test
  exercises: the bound rejects 8 KB of parentheses and nothing smaller. It is
  recorded here as unfinished rather than waived, and as the question an
  independent audit should decide: whether matching the reference on unbounded
  nesting is worth the rewrite, against the deterministic diagnostic this stage
  has now. Nothing else in the stage depends on the answer.
- Owned by later stages, stated so they are not re-derived from this one:
  - **Identifier interning.** Spec §1 asks that identifiers be interned as they
    enter the frontend. A controlling expression never keys on a spelling: the
    two spelling-derived facts are the `defined` operator's identity and the
    handout's mock parity, both decided at the reporting callback, and a plain
    `identifier_or_keyword` operand is the constant 0. No intern table is built
    here; the parser's needs do not justify one, and the PA2 ledger's reason
    (the table has no consumer until the parser keys lookup on it) still holds.
  - **The handout's mock `defined`.** `MockIsDefinedIdentifier` is a PA3 course
    definition, not a language rule. PA4 replaces it with the real macro table;
    a later stage must not re-derive the parity rule from this module.
  - **Phase 4.** The line splitter is PA3's, not the preprocessor's: it exists
    because PA3's input has one controlling expression per logical line and
    because phase 3's `new-line` is the only boundary the input has. The
    preprocessor that owns directives and expansion belongs to PA4, and
    `PostTokenStream::FinishGroup` is the hook it should reuse rather than
    rediscover.
  - **Phase-time and peak-memory telemetry.** No counter surface is added at
    this stage: the spec's counters cover allocations, candidates,
    specialization transitions, caches, worklists and IR sizes, none of which
    exist yet, and the tool's only required output is the result lines. Phase
    latency and peak RSS are obtained externally by the benchmark, which is the
    same observation without a surface the stage has no consumer for.
  - **`__builtin_*_overflow`.** The three builtins are the only compiler
    extension this stage uses. They are accepted by both toolchains the
    repository's targets use and they define 5.5/5.6/5.7 exactly, which a
    hand-written check would have to re-derive with two's-complement reasoning
    of its own. A later stage that must build with a compiler lacking them
    should replace this one helper, not spread the checks out.
  - **The input buffer's geometric-growth slack.** Shared with PA1/PA2 by
    construction; the measurement and the rejected alternative are recorded at
    the read site in `dev/posttoken.cpp` and in the PA2 ledger. A later stage
    that changes the read path changes all three tools at once.
  - **The shared benchmark statistic.** `student.tests/posttoken_benchmark.pl`
    and this stage's benchmark use the robust median-absolute-difference noise
    floor; `student.tests/pptoken_benchmark.pl` still uses the A/A `max - min`
    range, which is the fragile statistic the PA2 ledger's A4 records. It is the
    same inherited item PA2 left open, still open, and it is not this stage's to
    close: nothing here depends on PA1's number.
