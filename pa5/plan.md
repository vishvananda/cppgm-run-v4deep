# PA5 (`cppgm++ --emit-ast`) plan and handoff ledger

## Stage state

- Stage base commit: `91bd3202` (the PA4 final audit, recorded on entry before
  any stage edit).
- Last reviewed commit: `91bd3202`.
- Implementation commits: `2f4e97c4` (parser, arena, dump and driver) and the
  twenty-nine commits that follow it, up to the handoff commit that last touches
  this file.
- Target: `cppgm++ --emit-ast -o <out> <src>...` runs translation phases 1-7
  for each primary source, parses each translation unit with the PA5 syntax
  subset, and writes the deterministic AST dump the checked-in `.ref` files
  define.
- Progress: **184 / 188** checked-in PA5 tests pass, up from 0 / 188 at the
  turn's start.  `make test-report-through-pa4` passes 205 / 205 and
  `perl scripts/cppgm_file_audit.pl --stage pa5 --paths dev/src` reports no
  issue.

## Design / spec alignment

The parser is a fifth consumer of the shared phase 1-7 frontend and the first
stage that owns syntax:

```text
file bytes -> TranslatedSource -> PPTokenizer -> PPTokenReader
   -> Preprocessor -> PostTokenStream -> SyntaxTokenSink (phase 7 records)
   -> SyntaxParser (recursive descent over the token vector)
   -> SyntaxArena (structured AST) -> the dump
```

- `syntax/syntax_token.h` is one token record: a `SimpleTokenKind` (or the
  identifier/literal/eof pseudo-kinds), the spelling, and the literal facts
  later stages need.
- `syntax/syntax_arena.*` owns the nodes.  A node is a tag, an optional inline
  label and a child list; tags and labels are interned; the dump is a preorder
  walk with an explicit stack.
- `syntax/syntax_parser.*` is the parser: the cursor, the name table, node
  construction and the grammar, in two translation units (declarations/types in
  `syntax_parser.cpp`, statements/expressions in `syntax_parser_stmt.cpp`).
- `syntax/syntax_driver.*` is the `--emit-ast` driver.
- The name-category boundary of `parsing.md` is a scope stack of name facts
  recorded by declarations, with the documented lexical fallback for a name no
  declaration has resolved.

The parser follows the handout's guidance: one function per useful production,
a structured tree built directly (no recognition tree and no second pass over
spellings), and speculative alternatives that mark and roll back the cursor,
the tree, the name table and the angle state together.  The one piece of token
state the grammar needs - a `>>` whose first `>` a close-angle-bracket has
taken - is cursor state, so the second half stays the current logical token.

Names the dump spells as text (an `id-expression`, a `type-name`, a
`declarator-id`) are composed from the token range they cover: a name's
components are joined with a space only where writing them straight together
would change tokenization, which is why `C::operator=` has no space and
`C::operator int` does.

## Remaining groups

Four fixtures fail, each a corner of the ambiguity machinery rather than a
missing construct:

1. `200-qualified-member-comparison-template-arg` - a `>` inside a *base
   clause*'s template-argument list (`integral_constant<bool, R1::num <
   R2::num>`) is read as a relational operator, so the class-specifier ends
   before its body.  The angle guard is correct for an argument list entered
   from an expression, but the argument here is entered from a type position
   and the nested speculation for `R1::num<...>` leaves the guard's delimiter
   count wrong on failure.  The reducer is a two-line template class with a
   template-id base clause.
2. `200-forward-unknown-nested-template-in-ctor-body` - `typedef pointer<Y*,
   forward_delete<T, Y>, alloc_t> block_t;` inside a member function body whose
   class is declared earlier in the same namespace; the parameter clause of the
   enclosing constructor is misread.
3. `200-parenthesized-parameter-name-or-type` - N3485 8.2's
   `int f(int(value_type))` choice between a parenthesized parameter name and a
   parameter of function type.  `parsing.md` states the rule (the name category
   decides); the implementation still prefers the named reading.
4. `200-parenthesized-qualified-function-template-call` - a parenthesized
   qualified call whose callee is a template-id.

Earlier groups that this turn finished: the token plumbing and arena; simple
declarations, declarators and type-ids; namespaces, classes, enums and
templates; statements and expressions; the declaration/expression, type-id/
expression and template-id/relational ambiguities; the class-member, special
member, bit-field and pack forms; and the hosted attribute forms the corpus
uses.

## Performance evidence

Protocol (the one section 9 asks for): frozen binaries (`dev/cppgm++` and
`pa5/cppgm++-ref`), fixed flags and input, wall-time ABBA blocks with an A/A
arm, per-block paired differences, a median-absolute-deviation noise floor, and
every observation kept in a TSV.  `student.tests/syntax_benchmark.pl` runs the
whole protocol; its corpus is a template-heavy translation unit (class and
function templates with type, non-type and defaulted parameters, nested
template-ids, qualified ids into class members, inline member definitions),
every group repeated, so the run is dominated by the stage's own ambiguity
work rather than by startup.  The two binaries' dumps are compared byte for
byte before any timing is accepted.

Committed benchmark, 3 000 groups, 923 664 B of source, a 6 922 324 B dump,
5 ABBA blocks each arm (20 timed runs per label):

| tool | latency (s) | peak RSS (MB) |
| --- | --- | --- |
| `cppgm++-ref` | 0.670 [0.662..0.690] | 48.2 [48.1..48.4] |
| `cppgm++` | 0.438 [0.432..0.476] | 59.1 [59.1..59.2] |

A/A calibration on the same schedule: paired difference median +0.0027 s, MAD
0.0075 s, range [-0.0228..+0.0116].  The -0.227 s latency difference is 30x the
noise floor with 5 of 5 blocks negative, so it is a separable win.  Peak RSS is
**not** a win: the tree is a node vector with a `std::vector<int>` child list
per node, so the compiler holds 59.1 MB against the reference's 48.2 MB on this
corpus, a 1.23x constant factor.  The identified fix (one arena-backed child
list with a per-node range, or a reversed linked-list of child indices like the
reference's edge array) is a representation change, not an asymptotic one, and
is carried to the audit below.

`cppgm++ --emit-ast` has no executable output, so there is no generated-program
runtime or text size at this stage; the dump's size is reported instead, and no
telemetry surface is invented to report one.

## Validation

- `make test-report-through-pa4` - 205 / 205.
- `make test-pa5` - 184 / 188 (the four fixtures in "Remaining groups").
- `perl scripts/cppgm_file_audit.pl --stage pa5 --paths dev/src` - pass, no
  warnings (60 files checked).
- `student.tests/syntax_benchmark.pl` - dumps byte-identical to the reference;
  latency and peak RSS reported above.
- Whole-suite runs after each group above; the four remaining failures were
  reduced to two- to six-line reproducers before being recorded here.

## Handoff ledger

### Unfinished implementation

- The four fixture groups in "Remaining groups".  Each is an ambiguity corner
  with a recorded reducer; none is a missing construct, and three of the four
  need the same missing piece of machinery (an angle/delimiter state that
  restores exactly on a failed nested template-id speculation, which is what
  group 1's reducer isolates).  Group 3 additionally needs the parameter's
  parenthesized-name-vs-type choice, which `parsing.md` states and which the
  current `ParameterLikeDeclarator` does not implement.
- Whole-stage audit: not run yet.  Nothing here is waived; the failures are
  listed for the independent audit that follows this handoff.

### Carried to a later stage

- **Section 9's work counters.**  No counter surface is added at this stage, as
  in PA1-PA4, for the same reason: this tool's interface admits `--emit-ast` and
  `-o` and the README puts every other `-` argument outside it, while the file
  audit rejects behaviour driven by environment variables, so an *opt-in*
  counter surface has no compliant home.  Phase time and peak memory are
  reported by the benchmark, which observes them from outside.  This is a
  mandated requirement carried forward, not a waived one.
- **The child-list representation.**  Parsing is proportional to tokens as
  section 9 requires; the constant factor of peak RSS is the item above.

### Independent review questions

- The `sizeof`/`typeid` operand choice (`sizeof(S())` is the expression reading
  and `sizeof(int())` the type-id reading) is implemented as an empirical rule
  derived from the reference's behaviour on probes, not from a clause: N3485
  5.3.3 says the type-id reading wins where it is possible, and the reference
  disagrees for an identifier-led operand.  The rule and the probes that pin it
  are recorded beside the code.
- The name table keeps namespace and class member names visible to the
  enclosing scope, which is what `300-local-typedef-shadows-value-qualified-type`
  and the inherited-typedef fixtures need; the same choice is what makes a
  class body's later member visible to an earlier inline member body (a second,
  body-skipping pass over the class body).  The cost of that second pass is
  bounded by the presence of an inline member definition, and it is not visible
  in the benchmark above.
