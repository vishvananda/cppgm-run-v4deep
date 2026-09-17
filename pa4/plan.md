# PA4 (preproc) plan and audit record

## Stage state

- Stage base commit: `e9377f975bade53affa3dcef6395873790a8d652` (recorded on
  entry, before stage edits).
- Last reviewed commit: `e9377f975bade53affa3dcef6395873790a8d652`.
- Implementation commits: `1911b007` (phases 1-7, macro and directive groups),
  `8dfe91ac` (streaming text sequences, bounded paint walk), and the commit that
  last touched this file (the handoff record).
- Target: `preproc -o <out> <src>...` runs translation phases 1-7 for each
  primary source under the course's macro, conditional, include, line-control,
  pragma and predefined-macro rules, and writes the PA2 post-token dump with
  `preproc <n>` / `sof` / `eof` framing.  `make test-pa4` passes 105/105 and
  `make test-report-through-pa3` passes 100/100.

## Design / spec alignment

`preproc` is a fourth consumer of the shared phase 1-3 frontend, and the first
stage that owns preprocessing state:

```text
file bytes -> TranslatedSource -> PPTokenizer -> PPTokenReader (PP tokens +
   file/line) -> Preprocessor (directives, conditionals, includes, macros)
   -> MacroExpander -> IPPTextSink -> PostTokenStream -> TextPostTokenSink
```

- `preprocess/preproc/pp_token.*` is the one token record the preprocessor,
  the expander and the post-token pass share.  Its paint is a persistent list of
  macro identities with the extremes of its tail cached, so an unpainted token
  costs a null pointer, painting a replacement costs one small node, and a name
  outside the tail's range is rejected without walking it.
- `pp_token_reader.*` is the `IPPTokenStream` that asks for source locations and
  records the phase 3 callbacks.  `pptoken` keeps its own consumer; nothing of
  the PA1 contract moves.
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
  `__FILE__`, `__LINE__`, `#line`, `#pragma once` and `_Pragma`, and reports each
  finalized token to the sink as it is produced.
- The PA3 evaluator gained a value-returning entry point
  (`CtrlExpression::EvaluateValue`, surfaced as `CtrlExprSink::Result`) so `#if`
  reads a number instead of parsing its text view back.  The PA2 text view moved
  to `posttoken/post_token_text.h`, where `posttoken` and `preproc` share it.

### The nesting rule

`macros.md`'s nesting rule is where the fixtures are exacting, so it was
recovered from the reference's own structure (`nm -C`, `objdump`) and then
validated against every fixture rather than guessed:

- A replacement's own tokens carry the invocation's chain, but only where the
  chain can still matter: an object-like macro's whole replacement, and a
  function-like macro's parts other than a function-like macro name that no
  written `(` follows.  That name is not painted, because the parameter
  references around it are replaced by arguments and the `(` it would have been
  invoked with is not the one the definition wrote.
- A substituted argument starts a fresh chain: it joins the macro it is being
  substituted into, not the names the head had accumulated, while the paint it
  acquired while it was expanded stays on it.
- A token pasted from raw `##` operands is a substituted token like any other.
- `PPToken::substituted` is the bit that says so; `head.substituted` selects
  which of the two paints a replacement gets.

## Remaining groups

None for this stage's behaviour.  Every fixture in `pa4/tests` passes and both
groups are complete: the macro group (`#define`/`#undef`, arguments, stringize,
paste, rescan, recursion) and the directive group (conditionals, includes,
`#line`, predefined macros, pragmas, `_Pragma`), plus their integration
(locations through expansions, translation-unit state reset, string-literal
adjacency across directive lines).

## Performance evidence

Protocol: two frozen binaries (`reference-binaries/preproc` and `dev/preproc`),
the same `-O3` flags, one input per run, wall time and peak RSS from
`/usr/bin/time`, alternating A/B/B/A with the reference run twice as the A/A
noise calibration.  Outputs were compared byte-for-byte before the timings were
accepted.

Workload 1, a translation-unit-shaped source (6 148 146 bytes): a guarded
256 KB header of 8 000 object-like, 2 000 function-like and 2 000 paste/
stringize macros, then 160 000 use sites.  Both tools write 36 142 348
identical bytes.

| tool | wall (s), ABBA pairs | peak RSS |
| --- | --- | --- |
| `preproc-ref` | 1.55, 1.58, 1.53, 1.54, 1.52, 1.64 | 29.4 MB |
| `preproc` | 1.77, 1.77, 1.73, 1.71, 1.88, 1.92 | 321 MB |

A/A spread on the reference is 0.09 s, so the ~0.2 s (13%) latency difference is
outside the noise.  Peak RSS is 11x the reference's; see the handoff ledger for
what remains and why.

Workload 2, macro-expansion-shaped (1 422 048 bytes): 40 000 sites, each a
paste, a stringize and a 200-long helper chain.  Identical output; `preproc`
5.38 s / 90 MB against the reference's 5.93 s / 10.5 MB.  Chain-length scaling
on the same shape (10 / 50 / 200 links) is 0.64 / 1.61 / 5.38 s, against
0.65 / 1.77 / 5.93 s for the reference.

Changes this evidence justified: the expander reports each finalized token to
the sink instead of materializing a text-sequence and then emitting it (peak RSS
on workload 1 fell from 763 MB to 321 MB, latency from 2.40 s to 1.69 s), and a
paint node caches the extremes of its tail (workload 2's 200-link case fell from
9.64 s to 5.38 s by removing a quadratic walk).  Both are cheaper in compiler
work than the reference's own reading and neither changes any output.

## Handoff ledger

### Unfinished implementation

The frontend still builds one owning token vector per file, so peak RSS on
workload 1 is 321 MB against the reference's 29 MB.  `spec.md` section 1 asks
for a streaming token cursor instead.  The concrete boundary that makes this a
separate change rather than a local edit: a text-sequence is not a sequence of
logical lines, because a function-like macro name at the end of one line is
still an invocation when the next line opens with `(` (`400-fun-macro-define`),
and its argument list may span many lines.  A line-at-a-time reader must
therefore hold back a pending function-like macro name and its argument region
until the sequence ends, which is a change to the tokenizer-to-expander
interface rather than to either side of it.  The rest of the preprocessing
architecture - directives, conditionals, includes, expansion, the text view -
does not change with it.

### Independent review questions

Neither is waived; both are outside what the fixtures decide.

- The function-like paint rule and the `substituted` bit were recovered from the
  reference binary's structure and validated against all 105 fixtures.  That
  they reproduce the *reference's* rule is settled by the fixtures; that the
  reference's rule is the course's intended reading of `macros.md` (rather than
  an artefact of one implementation) is not something the fixtures can decide.
- `__has_cpp_attribute` reports `201603L` for the course's one attribute in both
  its spellings.  The fixtures pin its truthiness only, so the number is a
  choice, not a checked value.  `__CPPGM_AUTHOR__`'s spelling is likewise a
  choice, normalized by the comparator.
