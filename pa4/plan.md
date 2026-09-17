# PA4 (preproc) plan and audit record

## Stage state

- Stage base commit: `e9377f975bade53affa3dcef6395873790a8d652` (the PA3 final
  audit, recorded on entry before any stage edit).
- Implementation commits: `1911b007` (phases 1-7, macro and directive groups),
  `8dfe91ac` (streaming text sequences, bounded paint walk).
- Final-audit commits: `459aaaf7` (streaming frontend), `487231e4` (paint
  arena), `be0a7a08` (three macro-rule divergences), `3b69b5c3` (the stage's
  missing benchmark), `a610328c` (include bound, unfinished `_Pragma`, `#line`
  operand) and the commit that last touched this file.
- Target: `preproc -o <out> <src>...` runs translation phases 1-7 for each
  primary source under the course's macro, conditional, include, line-control,
  pragma and predefined-macro rules, and writes the PA2 post-token dump with
  `preproc <n>` / `sof` / `eof` framing.
- `make test-report-through-pa4` passes 205/205 and
  `perl scripts/cppgm_file_audit.pl --stage pa4 --paths dev/src` reports no
  issue.

## Design / spec alignment

`preproc` is a fourth consumer of the shared phase 1-3 frontend, and the first
stage that owns preprocessing state:

```text
file bytes -> TranslatedSource -> PPTokenizer
   -> PPTokenReader (one PPPToken per callback, forwarded)
   -> Preprocessor (directive lines, conditionals, includes, macro state)
   -> MacroExpander (rescan stack, blue paint)
   -> IPPTextSink -> PostTokenSink -> TextPostTokenSink -> the dump
```

Section 1 asks the preprocessor to expose a streaming token cursor and not to
build successive owning vectors of preprocessing tokens.  It now does: the
tokenizer's callbacks are consumed one at a time, a directive line is
accumulated until its new-line and then handled, and a text sequence's tokens
are pushed to the expander as they arrive.  `MacroExpander` pulls them back
through `IPPTextFeed` and runs as far as the tokens seen so far allow, suspending
on the two constructs whose meaning still depends on tokens that have not
arrived - a function-like macro name whose `(` may open the next line, and an
argument list that is still open.  Nothing holds a translation unit.

- `preprocess/preproc/pp_token.*` is the one token record the preprocessor, the
  expander and the post-token pass share.  Its paint is a persistent list of
  macro identities held in a translation-unit arena, with the extremes of each
  node's tail cached, so an unpainted token costs a null pointer, painting a
  replacement costs one small node, and a name outside the tail's range is
  rejected without walking it.
- `pp_token_reader.*` is the `IPPTokenStream` that asks for source locations and
  forwards each phase 3 callback as a record.  `pptoken` keeps its own consumer;
  nothing of the PA1 contract moves.
- `pp_macro.*` parses and validates a definition once, at its `#define`, which
  is where the handout puts the `#`/`##`/`...`/`__VA_ARGS__` rejections, and
  compares two definitions the way 16.3 does, white-space separation included.
- `pp_expander.*` is the stack rescan of `macros.md` with per-token blue paint.
  Arguments are expanded lazily and once, only where a parameter reference needs
  the expanded form, which is what keeps `stringize(max(0))` legal and `# x x`
  an error.  A `##` pastes only the operators the definition wrote; a `##` that
  arrives through a parameter is ordinary text, which is what makes
  `hash_hash`'s `# ## #` work.
- `pp_preprocessor.*` splits directives from text-sequences, evaluates `#if` and
  `#elif` through the inherited PA3 evaluator, resolves includes, owns
  `__FILE__`, `__LINE__`, `#line`, `#pragma once`, `_Pragma` and the include
  depth bound, and reports each finalized token to the sink as it is produced.
- The PA3 evaluator gained a value-returning entry point
  (`CtrlExpression::EvaluateValue`, surfaced as `CtrlExprSink::Result`) so `#if`
  reads a number instead of parsing its text view back.  The PA2 text view moved
  to `posttoken/post_token_text.h`, where `posttoken` and `preproc` share it.

### The nesting rule

`macros.md`'s nesting rule is where the fixtures are exacting, so the fixtures
alone are not the test used here: the reference binary is compared against
directly on generated definitions and use sites, in the shapes the rule governs.

- A replacement's own tokens carry the invocation's chain, except for one part:
  a function-like macro name that the definition does not follow with a written
  `(`.  The `(` that invokes such a name, if any, comes from the substituted
  text or from the tokens after the invocation, so the name is not the head of
  an invocation this expansion made.  It keeps the head's own chain when the
  macro is function-like, whose arguments surround it, and keeps nothing from
  the expansion when the macro is object-like, whose replacement is re-examined
  in place; and it keeps the whole chain when it names this macro or a name the
  head was already unavailable for.
- A substituted argument starts a fresh chain: it joins the macro it is being
  substituted into, not the names the head had accumulated, while the paint it
  acquired while it was expanded stays on it.
- A token pasted from raw `##` operands is a substituted token like any other.
- `PPToken::substituted` is the bit that says so; `head.substituted` selects
  which of the two paints a replacement gets.

## Architecture audit trace

A guarded header and its use site, followed end to end:

1. `ProcessPrimarySource` clears the macro table, the `#pragma once` set, the
   predefined macros and the paint arena, then calls `ProcessFile`.  Each
   primary source is a fresh translation unit; the headers it includes share
   that state and its `__FILE__`.
2. `ProcessFile` reads the bytes once into a `TranslatedSource` and runs
   `PPTokenizer` over it.  The source keeps its own buffer and a bounded
   lookahead window; phase 1 and 2 rewrites are applied as code points are
   pulled, and the line index is built only because the reader asks for
   locations.
3. `PPTokenReader` stamps each callback with the file index and the physical
   line and hands the record to `Preprocessor::OnPreprocessingToken`.  The
   callback's spelling is borrowed, so the record owns its own copy - and
   nothing else does.
4. A token at line start after a `#` opens a directive line.  `#define`
   reaches `HandleDefine`, which splits the name from the replacement list, has
   `ParseMacroDefinition` parse and validate it once, and installs it under a
   fresh identity.  A redefinition that is not token-for-token identical is an
   error, which is checked before the install.
5. A text sequence's tokens go to `PushTextToken`, which opens a
   `MacroExpander` frame and pumps it after every token.  The frame's stack's
   back is the next token to examine; the head's paint is `head.paint` plus the
   invoked macro's identity, and the replacement goes on the stack in front of
   the tokens that follow the invocation, so it is examined again before them.
6. Two paints are computed per invocation, as the nesting rule needs them: one
   for the replacement's own tokens and one for a substituted argument.  An
   argument is expanded at most once and only if some reference needs the
   expanded form; a `#` or a `##` operand takes the raw form.
7. When the frame reaches a quiescent point - stack empty, no invocation open,
   no argument prescan running - the sequence's paint nodes are unreachable and
   the arena is released.  Nothing else releases them, because nothing else can
   name one.
8. A `#` at line start ends the sequence, drains the frame, and opens the next
   directive.  At the end of the file the frame is finished, the conditional
   nesting must be empty, the frame is popped, and the reader's state is
   returned to the including file.
9. Finalized tokens go to `IPPTextSink`.  `Preprocessor::EmitToken` drops white
   space and placemarkers and runs `_Pragma`, which must be complete before the
   sequence ends; everything else reaches `TextPostTokenSink`, which composes
   the PA2 view in blocks.  The text view is a view: no phase reads it back.

Every boundary in that trace is an explicit owner with a release point - the
source buffer at the end of `ProcessFile`, the directive line at the end of the
directive, the frame at the end of the sequence, the paint arena at the
quiescent point - and there is no text roundtrip, no global retry, no semantic
reconstruction and no per-node hot allocation left on the path.

## Audit findings

### F1 - a translation unit was held as owning token vectors

`PPTokenReader` collected every token of a file into a vector and
`ExpandToSink` moved that whole vector onto the rescan stack.  A 6 154 521-byte
translation unit of 68 000 use sites peaked at **528 MB** of RSS against the
reference's 40 MB, and a 4 288 890-byte file of plain declarations with no macro
use at all - an expander that never ran - at **339 MB** against 20 MB.  Peak
memory is one of the four dimensions section 9 requires to be reported, and
section 1 names the fix.

`Preprocessor` now consumes the phase 3 callbacks directly and streams a text
sequence into `MacroExpander`, which suspends on the two constructs that need
tokens not yet produced.  The work stack stays a `std::vector`: a first cut used
a deque for the front insert the streaming feed needs and cost 10% on the
macro-chain workload, which this does not.

### F2 - the paint list was an owning `shared_ptr` with a node per expansion

`PPMacroPaintNode` was `std::shared_ptr<const PPMacroPaintNode>`, so every macro
invocation allocated a node *and* a control block and every token that named one
paid an atomic reference count.  Section 8 says hot nodes must not use an owning
`shared_ptr` or individual allocation, and a node is immutable and shared with a
lifetime that is already known.

The paint is now a plain pointer into `PPPaintArena`, a translation-unit arena
released in bulk.  Releasing only at the ends of a translation unit was the
first cut and was wrong in the other direction: a chain of length n makes n
nodes per invocation, and a translation unit's body is normally one text
sequence, so a 200-link chain over 40 000 sites held 123 MB against the
reference's 6.6 MB.  The release point is the quiescent point of F1's frame,
which costs nothing on the common quiescent token and bounds the arena to one
invocation.

`PPToken` also gained default member initializers.  `paint` and `substituted`
were read uninitialized for every token the tokenizer reported; the old shared
pointer made that harmless because a source token's paint was null either way,
which a bare pointer is not.

### F3 - `#` was a stringizing operator in every replacement list

16.3.2 makes `#` one only in a function-like macro's replacement list; in an
object-like one it is an ordinary preprocessing-token.  `#define B #` is a
definition the reference accepts and this rejected, and the `#` of a written
`# ## #` - which the course's `hash_hash` fixture needs - was accepted only
because of a makeshift allowance that also let a function-like `# ## #` through,
which the reference rejects.

Reducer: `#define B #` alone; the reference exits 0, this exited 1.

### F4 - a `##` whose operand was another `##` was accepted

16.3.3's operands are the tokens either side of the operator, so `q ## ## E`
has no pair to concatenate.  The reference rejects it at the definition; this
accepted it until the macro was used, and then reported a different failure.

Reducer: `#define E(x) q ## ## E`; the reference exits 1, this exited 0.

### F5 - the open-name paint was the empty set for every macro

A function-like macro name that the definition does not follow with a written
`(` is the one replacement part not painted with the whole chain.  Setting it to
the empty set for every macro loses the head's chain for a function-like macro,
which is what carries a nested name's unavailability across the invocation.

Reducer:

    #define B(m,e) m(q B_ID)
    #define B_ID() B
    #define REM(x) x
    B(REM,4)()(REM,1)(

The reference expands `B_ID` where this did not.  The object-like half is what
`#define f(x) B` / `#define B C` distinguishes from it, and it is the case the
`920-deferred-helper-argument-prescan` fixture pins.

### F6 - an unfinished `_Pragma` was silently dropped

The handout makes an occurrence of `_Pragma` not followed by
`( string-literal )` an error, and recognizes the operator only inside a
text-sequence.  One that fell off the end of a sequence was dropped, and one
split across a directive was completed by the next sequence's tokens.

Reducer: `_Pragma` followed by a new-line and end of file; the reference exits
1, this exited 0.

### F7 - `#line` read its operand as a C integer literal

`#line 010` set 8 where the reference sets 10, `#line 09` was rejected where the
reference accepts 9, and `#line 0x10` and a magnitude past a signed 64-bit value
were accepted where the reference rejects both.  The operand is decimal digits
that post-tokenize to a positive integer of a signed 64-bit type and nothing
else.

### F8 - an include cycle ran until the machine's stack was gone

`#include` of a file that includes itself, directly or through a cycle, died on
SIGSEGV.  The reference stops at 255 nested includes and reports an error; the
bound here is the same one, so 255 is accepted and 256 is not.

Reducer: a file whose only line is `#include "self.cpp"`.

### Cost of the fixes

Nothing here is carried on the hot path.  F1 and F2 remove work; F3-F8 are
definition-time checks, one comparison per finalized token, and one counter
comparison per `ProcessFile`.  Latency and peak RSS are both lower than the
pre-audit binary's on every corpus measured (see below).

## Checked and not defects

- `macros.md`'s three documented traces - `f(f(x))`, `f(z)` with
  `#define z z[0]`, and `g(f)(g)(3)` - and its `f(g)b)` rescan example all
  produce exactly the documented output, under both the corrected rule and the
  reference.
- The GNU `, ## __VA_ARGS__` comma deletion, in both the empty and non-empty
  cases.
- `#pragma once` keyed on the host file identity, so distinct spellings of one
  file share the once state, and `#include`'s two-step search - `__FILE__`
  relative first, working directory second - with `__FILE__` set to whichever
  succeeded.
- Inactive-group directive ordering: a nested `#if` group's `#elif`/`#else`
  order is checked even when the group is inactive, and its controlling
  expression is not evaluated.
- The null directive, the non-directive (error when active, ignored when not),
  the identifier-like operator words `and`/`or_eq` as `defined` operands, and
  `__has_cpp_attribute` in both spellings.
- `RetokenizeSpelling` requires a paste to form exactly one preprocessing-token
  and stops the collector at the second record so a pathological spelling cannot
  grow the vector.
- A macro's identity is its id, so a redefinition is a different macro and a
  name painted on an old token never blocks the new definition by accident.
- The output is byte-identical to the reference on every corpus in the
  Validation section.  The file audit's self-containment checks - no test-suite
  path or concrete test name, no environment-variable-driven behaviour, no
  reading of expected or reference data - pass, and no compiler phase in
  `dev/src` spawns a process or names an external tool; the only `fork`/`exec`
  in the tree is the test runner under `dev/src/support/testing`.

## Changes

- `459aaaf7` - the reader forwards instead of collecting; the preprocessor
  consumes phase 3 callbacks; the expander runs from an `IPPTextFeed` and
  suspends on open constructs (F1).
- `487231e4` - `PPPaintArena` replaces the shared pointer; the arena is released
  at the frame's quiescent point; `PPToken` gains default member initializers
  (F2).
- `be0a7a08` - the `#` operator is function-like only; `##` may not take a `##`
  operand; the open-name paint is the head's chain for a function-like macro and
  empty for an object-like one (F3, F4, F5).
- `3b69b5c3` - `student.tests/preproc_benchmark.pl`, the stage's missing
  benchmark.
- `a610328c` - the include depth bound, the `_Pragma` sequence check, and the
  `#line` operand (F6, F7, F8).

## Performance evidence

Protocol: frozen binaries (`reference-binaries/preproc` and `dev/preproc`), the
same flags, one input per run, wall time and peak RSS from `/usr/bin/time`,
alternating A/B/B/A with the reference run twice as the A/A noise calibration,
per-block paired differences, a median-absolute-deviation noise floor, and every
observation kept in a TSV.  Outputs are compared byte for byte before any timing
is accepted.  `student.tests/preproc_benchmark.pl` runs the whole protocol.

Committed benchmark, two corpora, 40 000 sites each, 5 ABBA blocks:

| corpus | tool | latency (s) | peak RSS (MB) | dump (bytes) |
| --- | --- | --- | --- | --- |
| translation unit, 3 780 525 B | `preproc-ref` | 1.180 [1.169..1.503] | 35.6 | 35 070 238 |
| | `preproc` | 0.931 [0.902..1.528] | 31.1 | identical |
| expansion chain, 3 665 919 B | `preproc-ref` | 4.379 [4.360..4.412] | 13.4 | 13 992 310 |
| | `preproc` | 2.413 [2.404..2.428] | 8.8 | identical |

A/A noise calibration on the same schedule: median absolute difference
0.172 s on the translation-unit corpus (worst excursion 0.317 s) and 0.005 s on
the expansion-chain corpus (worst excursion 0.045 s).  The chain corpus's
latency difference is 1.97 s with 5 of 5 blocks negative, an order of magnitude
outside the noise floor.  The translation-unit corpus's 0.25 s difference is
*not* separable from the schedule on this machine in that block - the A/A arm
reached the same magnitude in 4 of 5 blocks - so it is reported as unresolved
rather than as a win; peak RSS is separable on both corpora.

Before/after, the audit's own corpora, medians of three runs each beside the
reference:

| corpus | before (`8b5cd0b9`) | after | reference |
| --- | --- | --- | --- |
| 6 154 521 B, 68 000 use sites | 2.44 s / 528 MB | 1.40 s / 34.4 MB | 1.82 s / 40.3 MB |
| 711 107 B, 8 000 sites, 200-link chain | 2.38 s / 59.4 MB | 1.56 s / 5.3 MB | 2.31 s / 6.6 MB |

The pre-audit binary's 528 MB and 59 MB are the same measurements F1 and F2
report; the "after" column is the tree as committed.  All four dumps are
byte-identical between the two binaries and the reference.

`preproc` has no executable output, so there is no generated-program runtime or
text size at this stage; the dump's size is reported instead, and no telemetry
surface is invented to report one.

## Validation

- `make test-report-through-pa4` - 205/205, all four stages.
- `perl scripts/cppgm_file_audit.pl --stage pa4 --paths dev/src` - pass.
- 56 hand-built boundary cases - invocations split across lines, a macro name
  ending a file, an argument list split across lines, a macro name before a
  blank-line run, `_Pragma` in every position, `#line`, includes, a file with no
  trailing new-line, `#define B #`, `#line 010` - each compared for dump and
  exit status against the reference.
- Generated corpora, each compared for dump and exit status against the
  reference: definition-shaped (random macro definitions whose bodies end in a
  bare macro name, followed by random use sites - the shapes F5 governs),
  text-shaped (directives interleaved with text sequences across odd new-lines),
  and directive-shaped (`#line` operands, `_Pragma` positions, include forms).
  Four thousand five hundred generated inputs across ten seeds in the final
  tree, with no mismatch.
- The four handout traces and the boundary cases above agree with the reference
  under the corrected rules.
- ASan and UBSan over the fixtures, the boundary cases, the generated corpora
  and the benchmark corpora: no diagnostics.
- Scale probes with no crash and exit status equal to the reference's: 20 000
  nested parentheses in one argument, 5 000 nested `#if`, 50 000 arguments, a
  2 000-link helper chain, a 200 000-token replacement list, and the
  255/256-include boundary either side.

## Handoff ledger

### Unfinished implementation

None for this stage's behaviour.  Every fixture in `pa4/tests` passes, both
groups are complete - the macro group (`#define`/`#undef`, arguments, stringize,
paste, rescan, recursion) and the directive group (conditionals, includes,
`#line`, predefined macros, pragmas, `_Pragma`) - and the streaming frontend
that section 1 asked for is in place.

### Carried to a later stage

- **Section 9's work counters.**  No counter surface is added at this stage, as
  in PA1-PA3, and the reason is now concrete rather than inherited: this tool's
  interface admits exactly one argument (`-o`) and the README puts every other
  `-` argument outside it, while the file audit rejects implementation behaviour
  that depends on environment variables, so an *opt-in* counter surface has no
  compliant home here.  An always-on one is not separable overhead.  Phase time
  and peak memory are reported for this stage by the benchmark, which observes
  them from outside; the counters section 9 names - tokens, expansions,
  definitions, include depth - belong with the driver that will run every phase.
  This is a mandated requirement carried forward, not a waived one.

### Independent review questions

- The open-name paint rule (F5) is the one part of the nesting rule that
  `macros.md`'s prose does not settle; its traces do not reach the corner.  It
  is now checked against the reference on generated inputs rather than only
  against the fixtures, and the residual doubt is bounded: the shapes that reach
  it are stated in the code beside the rule, and the generated corpora that
  target them agree with the reference across every seed tried.
- `__has_cpp_attribute` reports `201603L` for the course's one attribute in both
  its spellings.  The fixtures pin its truthiness only, so the number is a
  choice, not a checked value.  `__CPPGM_AUTHOR__`'s spelling is likewise a
  choice, normalized by the comparator.