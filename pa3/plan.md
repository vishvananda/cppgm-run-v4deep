# PA3 (ppexpr) plan and audit record

## Stage state

- Stage base commit: `2b52b6e3306f9748c62914e64eec99d6317c9156` (recorded on
  entry, before stage edits).
- Last reviewed commit: `b0e4a4d1` (the harness's deep-nesting comparison).
- Implementation commits: `16f7d2a4` (the evaluator), `02cd7532` (F1, F2),
  `93de55c2` (F3 and the nesting bound), `3894d18b`, `058b4a1f`, `b8184ea6`
  (cleanups), `d772408f` (F4: the operator-precedence parser and the packed
  token record), `b0e4a4d1` (the harness). This document is the consolidated
  plan and audit record for `pa3`; the commit that last touched it is the one
  that wrote this record.
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
  The record itself is 16 bytes: the token type is a `unsigned short` and the
  kind an `enum : std::uint8_t`, which is what a line's tokens cost and nothing
  more (F4).
- `CtrlExpression` is an operator-precedence parser over that buffer, driven by
  one `{token, operation, precedence}` table (F3's collapse). It holds two heap
  vectors - the values a reduction has produced and the operators waiting to be
  reduced - and never recurses, so an expression nested past the machine's stack
  limit is parsed and evaluated like any other (F4). A `?` is pushed as a marker
  waiting for its `:` and the `:`-completed form as a marker waiting for the arm
  it takes; both carry the conditional's precedence, which is what makes
  `a ? b : c ? d : e` group right and `a ? b ? c : d : e` left.
- Evaluation happens as the parse reduces. A sub-expression's whole result is
  one value plus one signedness bit, and liveness travels with the operator
  stack: the liveness of the operand region that follows an operator is decided
  when the operator is read, because the value it depends on is already on the
  value stack. `0 && ...` knows its right operand is dead at the `&&`; `a ? b :
  c` knows which arm is dead at the `?` and at the `:`. A dead sub-expression is
  still parsed and still typed - 5.16's result type comes from both arms even
  when one is not evaluated (`260-cond-ret-type`) - but no value is computed for
  it and the handout's course-defined value errors (division by zero, the one
  signed quotient with no representation, a signed `+`/`-`/`*`/unary `-` that
  overflows, a shift count outside `[0, 64)`) are raised only where the language
  evaluates something.
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
   parser walks the token buffer once. `1 ? ...` pushes the condition's value
   and a `?` marker, and the marker carries the liveness of the then-arm
   (here: dead, because the condition is 1); at the `:` the completed
   conditional takes the else-arm's liveness from the same value. The arm that
   is not chosen is parsed and typed but not evaluated, so the
   `-9223372036854775807 - 1` in it neither overflows nor is skipped in the type
   computation.
6. The result is appended to the sink's output block, which is written to the
   stream in 64 KiB pieces. A line whose only token is invalid (`#`) reaches
   step 5 with an empty buffer and a set rejection flag and prints `error`; a
   blank line reaches it with both clear and prints nothing.

Each preprocessing-token is classified exactly once, each line's tokens are
parsed exactly once, and there is no text roundtrip, no retry and no per-token
owning allocation. The one per-line allocation is the token vector, whose
capacity is retained across lines; the parser's two stacks keep theirs the same
way.

The trace's second shape is the one that decides the parser's implementation: a
line of `(` repeated to any depth. Phase 1-3 hands the line over exactly as
above, but the parser's depth is now the heap's, not the process stack's - the
same line at 2 000 000 parentheses is parsed and evaluated (the reference's
value, `1`), with no machine-stack growth at all.

## Audit findings

Every finding below is reproducible from one `printf` and was found by an
instrument independent of the fixture suite; the fixture suite passed both
before and after each one.

| # | Surface | Class |
| --- | --- | --- |
| F1 | `?:` arm typing | a comparison's result type was only set when it was evaluated |
| F2 | signed overflow | `+`, `-`, `*` and unary `-` wrapped where the reference errors |
| F3 | precedence levels | twelve parser frames per level; one function per grammar level |
| F4 | nesting depth | the parser's stack was the machine's: a crash before F3's bound, and a wrong `error` after it |

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

The fix is the general one, not the case: a node's result type is decided
before any question of whether it is evaluated. In the final form that is
structural - `ResultIsUnsigned` is applied to every operator reduction whether
the operation is live or dead - and the result type is kept apart from the
operand conversions, which is the distinction the rewrite had to preserve
(`-5 < 5u` compares unsigned and yields a signed `bool`).

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

### F3 - one function per precedence level

The grammar names one production per precedence level, so the first cut wrote
one function per level, each of which recursed into the next. The handout's own
design note asks for the opposite:

> You should be aware that there is a technique in which you can collapse the
> calls to all the different binary operator expressions into one call. This is
> achieved by keeping a precedence table and making decisions about how to build
> the parse tree based upon it.

The stage now has that one loop over a table of `{token, operation,
precedence}`. Cost: a nested expression needs a couple of stack entries per
level instead of twelve parser frames; on the same 10.7 MB corpus the first form
read 0.6815 s and the collapsed form 0.6268 s (medians of 20 timed runs each,
against that run's 0.0023 s A/A noise floor). The final audit kept the collapse
and replaced the frame stack itself (F4).

### F4 - the parser's stack was the machine's stack

The collapse left the recursion, and with it the C stack, in place. Recursive
descent grows the stack with nesting and nothing bounded it, so the usable depth
was a property of `ulimit -s` and the failure was a SIGSEGV that lost the whole
run's output:

```sh
perl -e 'print "(" x 20000, 1, ")" x 20000, "\n"' | dev/ppexpr   # before: no output at all
```

The checkpoint audit bounded that with an explicit frame limit (`DepthGuard`,
8 192 frames = 4 095 nested parentheses) and reported the line as an invalid
controlling expression past it. That was the wrong fix, and it was recorded as
an open question rather than closed: it replaced one `ulimit`-dependent failure
with a course-defined `error` on an expression the grammar accepts, which the
handout's contract says must print a decimal literal - the reference parses
nesting from the heap and computes a value at any depth.

```sh
perl -e 'print "(" x 5000, 1, ")" x 5000, "\n"' | dev/ppexpr       # checkpoint audit: error
perl -e 'print "(" x 5000, 1, ")" x 5000, "\n"' | ppexpr-ref       # 1
perl -e 'print "!" x 100000, 1, "\n"' | dev/ppexpr                  # checkpoint audit: error
perl -e 'print "1 ? " x 20000, 2, ": 3" x 20000, "\n"' | dev/ppexpr # checkpoint audit: error
```

The bound was a self-imposed gate on a correctness surface - valid input
reported as invalid - so it is removed rather than reclassified: the parser now
holds its own operand and operator stacks on the heap, and its evaluation does
the same. Nothing in the stage is bounded by the machine stack any more, and no
input is rejected for being nested. The three unbounded recursions the grammar
has - parenthesis nesting, a prefix-operator chain and a conditional chain - are
each compared against the reference past the depth a C stack could carry, and
the value is the same:

```sh
for d in 4096 50000 200000 2000000;   # all: mine 1, reference 1
do perl -e "print '(' x $d, 1, ')' x $d, \"\n\"" | dev/ppexpr; done
perl -e 'print "!" x 2000000, 1, "\n"'                 | dev/ppexpr   # 1
perl -e 'print "1 ? " x 500000, 2, ": 3" x 500000, "\n"' | dev/ppexpr # 2
```

Scale: `student.tests/ppexpr_differential.pl` now compares the two tools on nine
deep inputs (those three shapes at 100 000-2 000 000 deep, plus the unbalanced
`((((1` and a lone `(((…`) as a permanent class, next to the reference-compared
curated list rather than in place of it.

Two things came out of the same rewrite and are part of the record:

- **The operand conversion is not the result type.** The parser now decides a
  node's *type* at reduction and its *value* only when live, and the first form
  of that passed the node's result type to the arithmetic. A comparison's result
  is `bool`, so `-5 < 5u` came out `1` where 5.10 converts the operands to the
  common unsigned type first. `200-signed-unsigned-comparison` fails on exactly
  that line, and the fix is to derive the conversion from the two operands
  (`ApplyBinary`) while the result type stays `ResultIsUnsigned`'s business.
- **A `defined` operand's tokens were over-consumed.** The first form advanced
  the cursor past the closing `)` of `defined (a)` as well as past the operand,
  so `521 * (521 * defined a)` reported `error`. `300-triple` fails on exactly
  that line. Both defects were caught by the fixture suite before the rewrite
  was committed, and are noted here because the classes are what a later
  constant-expression evaluator will meet again.

### Cost of the fix

The explicit stacks and the per-entry liveness are not free. On the same corpus,
in the same session and schedule, the checkpoint-audit binary and the final one
read:

| arm | latency (median [min..max]) |
| --- | --- |
| checkpoint audit (recursive, bounded) | 0.6268 s [0.6236..0.6538] |
| final (explicit stacks) | 0.6411 s [0.6389..0.6511] |
| paired per-block difference | **-0.0131 s**, 4 of 5 blocks negative |

and a second run of the same comparison read 0.6270 s against 0.6436 s
(-0.0163 s, 5 of 5 negative). The fix therefore costs about 2.3% of this
stage's latency - the price of a stack the input cannot overflow - and it is
recorded as such rather than as a win. The comparison is not like-for-like:
the checkpoint-audit binary is not a correct implementation of the contract for
input it crashes or rejects, so the reference remains the baseline this stage is
measured against (below).

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
  shapes (`?:`, `&&` and `||` in both directions), `260-cond-ret-type` fixes a
  dead arm's type, and the same liveness is what makes F2's
  `9223372036854775807 + 1` a value rather than an error when it stands in an
  arm the condition did not choose (probed on all four shapes after F2, and
  again after F4 - the rewrite moved the decision from a parameter to the
  operator stack entry, and the two agree on the 36 000 differential inputs
  below).
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
- **The token record's size.** One `CtrlToken` per token of a line is the line's
  dominant memory, and the record is 16 bytes - the 64-bit value, the token
  type, the kind and three flags - rather than 24: the course table has well
  under a thousand enumerators, so the type is an `unsigned short`, and the kind
  is an `enum : std::uint8_t`. The parser's operator-stack entry is 8 bytes for
  the same reason (its precedence, kind, operation and two liveness flags), and
  its value entry 16. Measured on the target with `sizeof`: 16 / 8 / 16.
- **Deep single-line inputs are O(source), not O(nesting frames).** The four
  worst shapes - 2 000 000 nested parentheses (4.0 MB), a 2 000 000-long prefix
  chain (2.0 MB), a 1 000 000-term additive chain (4.0 MB) and 500 000 nested
  conditionals (3.5 MB) - read 101.9 / 68.7 / 40.4 / 54.3 MB here against
  102.1 / 196.7 / 149.4 / 136.5 MB for the reference, with identical output.
  The parenthesis shape is at parity; the other three are well under it. These
  are self-selected diagnostics of the same read path PA1 owns, not a mandated
  limit.
- **Self-containment.** No `system`/`popen`/`exec`, no reference-binary
  invocation and no fixture-name recognition anywhere in the stage.

## Changes

| File | Change |
| --- | --- |
| `dev/src/preprocess/ctrl_expr/ctrl_expr_token.h` | new: the line's compact token record, the handout's mock `defined` parity, and the 16-byte layout |
| `dev/src/preprocess/ctrl_expr/ctrl_expr_stream.{h,cpp}` | new: the logical-line splitter and the sink that accumulates and evaluates a line |
| `dev/src/preprocess/ctrl_expr/ctrl_expr_parser.{h,cpp}` | new: the operator-precedence parser, the liveness on its operator stack, and the arithmetic |
| `dev/ppexpr.cpp` | the tool: read stdin, run the pipeline, flush |
| `dev/src/posttoken/post_token_stream.{h,cpp}` | `FinishGroup` made public for the logical-line boundary |
| `dev/src/posttoken/fundamental_type.{h,cpp}` | `FundamentalTypeIsSigned` / `FundamentalTypeIsIntegral` |
| `dev/frontend_source_sets.mk` | register `ctrl_expr/*` and the inherited `posttoken/*` for `ppexpr` |
| `student.tests/ppexpr_differential.pl` | the deep-nesting class compared against the reference; the generator's depth raised |

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
| `mine` | 0.6425 s [0.6399..0.6482] | 19.6 MB [19.6..19.6] |
| `ref`  | 0.9914 s [0.9878..0.9996] | 18.5 MB [18.4..18.6] |

- Paired per-block difference (mine - ref): -0.3474 s [-0.3494..-0.3444],
  **5 of 5 blocks negative**.
- A/A noise calibration: paired difference +0.0029 s [-0.0000..+0.0054], median
  absolute difference 0.0029 s, worst excursion 0.0054 s, and 0 of 5 A/A blocks
  reached the measured A/B effect. The A/B difference is about 64 times the
  worst excursion the reference produced against itself.
- The checkpoint audit's reading of the same protocol on the same corpus was
  0.6275 s / 19.6 MB against 0.9887 s / 18.5 MB; the revision that removed the
  nesting bound before F4's rewrite read 0.7102 s. The final revision sits
  between them, at 0.6425 s, and the ~2.3% it costs against the checkpoint
  audit's binary is the fix's price, measured paired above. It is a *third* of
  what the first (two-pass) form of the same fix cost, which is why the parser
  evaluates during reduction rather than building a tree: the tree form read
  0.7102 s, i.e. 11% over the checkpoint audit, and nothing in the contract
  needs the tree.
- Marginal cost of this stage: the same corpus, read by the three tools in one
  interleaved loop (medians of 7; `dev/ppexpr` 0.6401 s / 19.61 MB, the reference
  1.0608 s / 18.46 MB, the stage-base PA2 `posttoken` 0.7047 s / 19.61 MB). The
  controlling-expression evaluation therefore adds no measurable latency or
  memory above the phases 1-7 frontend it is built on, and the two readings
  agree with the ABBA table. The comparison is a like-for-like tool-to-tool
  reading, not a controlled ablation: `posttoken` writes one line per token
  where this tool writes one per logical line, so the reported difference
  bounds the stage's added work from above rather than isolating it.
- Peak RSS: the stage reads 1.1 MB / +6% over the reference on this corpus, and
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
- The stage's own structures add nothing measurable to this corpus: the parser's
  two stacks and the line's token vector are bounded by the longest line, and a
  line of this corpus is a few dozen tokens.

## Validation

- `perl scripts/cppgm_file_audit.pl --stage pa3 --paths dev/src` - pass, 38
  files checked.
- `make test-report-through-pa3` - **100/100 tests, 3/3 stages** (pa1 54, pa2 26,
  pa3 20); earlier assignments remain passing.
- `make build` with the assignment's `-std=gnu++11 -Wall -O3` - warning-free for
  every source in the `ppexpr` source set, under both g++ and clang++. The
  parser's translation unit also compiles with `clang++ -std=gnu++11 -Wall -O3`,
  which is the other toolchain the repository's targets use and the one that has
  to accept the overflow builtins.
- `student.tests/ppexpr_differential.pl 1500 SEED` for seeds 1-24 on the final
  revision - 36 000 inputs, no divergence in stdout or exit status, plus the
  curated list (including F1's, F2's and the deep-nesting class's reducers) on
  every run. Its curated list pins the reducers for F1, F2 and the nesting class,
  so the classes the fixtures do not reach are reproducible from the committed
  harness rather than only from this audit's scratch corpora.
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
- An independent conditional/type matrix - 2 160 lines crossing four conditions,
  ten then-arms and eight else-arms, each also wrapped in `+ 0u`, a `?:`
  condition, unary minus, a shift and a comparison - produces byte-identical
  output to the reference. This is the F1 class widened, and it is the matrix
  that would catch a result-type/operand-conversion confusion again.
- Independent adversarial probing beyond both harnesses, on this revision:
  nesting to 2 000 000 and unbalanced `((((1` at 5 000 deep, a 2 000 000-long
  prefix chain, a 1 000 000-term operator chain, 500 000 nested conditionals,
  `defined` with a comment between the operator and its operand, a NUL byte and
  a stray `;` inside an expression, splices inside a logical line, and the phase
  1-3 failures. All agree, including the deep inputs that the checkpoint audit's
  bound rejected.
- Two scratch instruments written for this audit, because the rewrite's risk was
  in precedence and error handling rather than in the value semantics the
  harnesses already covered:
  - every token sequence of up to five tokens over a 14-symbol alphabet (`1`,
    `2u`, `a`, `+`, `-`, `!`, `*`, `<`, `&&`, `?`, `:`, `(`, `)`, `defined a`),
    in three runs - lengths 1-3 (2 954 sequences), exactly 4 (38 416) and
    exactly 5 (537 824), 579 194 in all - differs nowhere, in stdout or exit
    status. The two longer runs are packed 400 sequences per file and reduced by
    line when a file differs. This is the grammar's whole neighbourhood: the
    sweeps elsewhere cross operators with operands, never arbitrary token
    sequences.
  - 16 000 randomly generated expressions of 3-8 nesting levels and up to a few
    hundred nodes, each with a 25% chance of a deleted token, differ nowhere.

## Handoff ledger

- Implementation: complete for the PA3 contract, with F1-F4 fixed and the
  nesting bound removed. No further work is owned by this stage.
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
  - **The constant-expression evaluator.** PA4's `#if` needs the same parse and
    the same liveness, and it needs it over a token sequence it has already
    expanded. `CtrlExpression` is deliberately that: its input is a token
    buffer, its output a string, and its liveness rules are written where they
    can be read. What it is *not* is a general expression parser - the grammar
    is the handout's controlling-expression grammar, which has no assignment,
    comma, cast or call.
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
