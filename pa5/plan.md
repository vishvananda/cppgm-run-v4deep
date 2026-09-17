# PA5 (`cppgm++ --emit-ast`) plan and handoff ledger

## Stage state

- Stage base commit: `91bd3202` (the PA4 final audit, recorded on entry before
  any stage edit).
- Last reviewed commit: `91bd3202`.
- Implementation commits: `2f4e97c4` (parser, arena, dump and driver) and the
  thirty-three commits that follow it, up to the handoff commit that last
  touches this file.
- Target: `cppgm++ --emit-ast -o <out> <src>...` runs translation phases 1-7
  for each primary source, parses each translation unit with the PA5 syntax
  subset, and writes the deterministic AST dump the checked-in `.ref` files
  define.
- Progress: **188 / 188** checked-in PA5 tests pass.  `make
  test-report-through-pa4` passes 205 / 205 and `perl
  scripts/cppgm_file_audit.pl --stage pa5 --paths dev/src` reports no issue.

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
  construction and the grammar, in three translation units - declarations,
  classes and declarators in `syntax_parser.cpp`, statements and expressions in
  `syntax_parser_stmt.cpp`, and types, names and template arguments in
  `syntax_parser_type.cpp`.
- `syntax/syntax_driver.*` is the `--emit-ast` driver.
- The name-category boundary of `parsing.md` is a scope stack of name facts
  recorded by declarations, with the documented lexical fallback for a name no
  declaration has resolved.

The parser follows the handout's guidance: one function per useful production,
a structured tree built directly (no recognition tree and no second pass over
spellings), and speculative alternatives that mark and roll back the cursor,
the tree, the name table, the delimiter count and the angle state together.
The one piece of token state the grammar needs - a `>>` whose first `>` a
close-angle-bracket has taken - is cursor state, so the second half stays the
current logical token.

Two clauses carry most of the ambiguity work, and both are cited where they are
implemented rather than approximated:

- N3485 14.2/2 makes a name a template-name only where lookup found one, so a
  qualified name inside a template-argument list that is not one keeps its `<`
  as the less-than operator.  That is what leaves `ic<bool, R1::num < 2>` its
  closing `>`; a bare name keeps the course's lexical-fallback speculation,
  which is what `std::g<int>()` and `B<2>` need.
- N3485 8.2/7 makes a type-name nested in parentheses in a parameter clause a
  simple-type-specifier, so `int(value_type)` is a parameter of function type
  and `( ... )` is that same clause with a pack, since `...` is not a
  declarator-id.  The node keeps the name the named form gives it, which is why
  the dump prints `declarator` there and `abstract-declarator` for a keyword
  type.

A declaration's name is a name of the scope that encloses its template
parameter clause, not of that clause: a class or alias template is a
template-name there, which is what makes `B * p;` a declaration after
`template<class T> struct B {};`.

Names the dump spells as text (an `id-expression`, a `type-name`, a
`declarator-id`) are composed from the token range they cover: a name's
components are joined with a space only where writing them straight together
would change tokenization, which is why `C::operator=` has no space and
`C::operator int` does.

## Remaining groups

None of the checked-in fixtures fails.  The groups this turn closed, each of
them an ambiguity corner with a reduced reproducer kept in
`student.tests/syntax_differential.pl`:

1. A relational template argument that needs the enclosing list's `>`: the
   nested template-id reading of `R1::num < 2` would take it, so a qualified
   name is held to 14.2/2 inside an argument list while a bare name keeps the
   speculative reading.
2. The parameter's parenthesized name against a parameter of function type
   (8.2/7), including `( ... )` and the pointer operator that a parameter
   clause holds rather than the abstract declarator around it.
3. The name table's account of a failed alternative: bindings are undone by
   identity rather than by trimming a scope's map, which is ordered by name.
4. A template declaration's name as a name of the enclosing scope, with the
   template categories of 14.2/2.
5. The checkpoint's delimiter count, which a half-read parenthesized construct
   left raised, so a later `>` in an angle list read as an operator.

Earlier groups: the token plumbing and arena; simple declarations, declarators
and type-ids; namespaces, classes, enums and templates; statements and
expressions; the declaration/expression, type-id/expression and
template-id/relational ambiguities; the class-member, special member, bit-field
and pack forms; and the hosted attribute forms the corpus uses.

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

Benchmark, 3 000 groups, 923 664 B of source, a 6 922 324 B dump, 5 ABBA blocks
each arm (20 timed runs per label):

| tool | latency (s) | peak RSS (MB) |
| --- | --- | --- |
| `cppgm++-ref` | 0.661 [0.656..0.672] | 48.2 [48.0..48.3] |
| `cppgm++` | 0.458 [0.452..0.479] | 59.1 [59.0..59.2] |

A/A calibration on the same schedule: paired difference median -0.0024 s, MAD
0.0024 s, range [-0.0087..+0.0016].  The -0.207 s latency difference is 86x the
noise floor with 5 of 5 blocks negative, so it is a separable win.

The 186-fixture compiler of the previous handoff and the 188-fixture compiler
of this one were both measured on that corpus: 0.441 [0.437..0.447] against
0.458 [0.452..0.479], so the name table's account of its bindings and the
delimiter count in the checkpoint cost about 4%.  The first version of the
binding log filtered the whole log whenever a scope closed; the profiler put
that at 9.8% of the parse, and cutting the log back to where the closed
scope's bindings start recovered all but that 4%.

Peak RSS is **not** a win: the tree is a node vector with a `std::vector<int>`
child list per node, so the compiler holds 59.1 MB against the reference's
48.2 MB on this corpus, a 1.23x constant factor.  The identified fix (one
arena-backed child list with a per-node range, or a reversed linked-list of
child indices like the reference's edge array) is a representation change, not
an asymptotic one, and is carried to the audit below.

`cppgm++ --emit-ast` has no executable output, so there is no generated-program
runtime or text size at this stage; the dump's size is reported instead, and no
telemetry surface is invented to report one.

## Validation

- `make test-report-through-pa4` - 205 / 205.
- `make test-pa5` - 188 / 188.
- `perl scripts/cppgm_file_audit.pl --stage pa5 --paths dev/src` - pass, no
  warnings (61 files checked).
- `student.tests/syntax_benchmark.pl` - dumps byte-identical to the reference;
  latency and peak RSS reported above.
- `student.tests/syntax_differential.pl` - 20 reduced reproducers of the
  ambiguity and name-category corners agree with the reference in exit status
  and dump; one known difference is recorded below.
- Whole-suite runs after each group above; every failure this turn was reduced
  to a two- to six-line reproducer before being fixed.

## Handoff ledger

### Unfinished implementation

- Nothing that a checked-in fixture reaches is unfinished.  The one shape this
  compiler reads differently from the reference is the known difference below;
  it is a reference artifact in a region the handout puts outside the required
  boundary, not a missing construct, and it is recorded rather than waived.

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

- **A qualified name in a parameter's parenthesized position**: the reference
  reads `int q(int(x::C))` - after an earlier parameter has declared `x` a
  value - as a parameter clause whose parameter is the qualified name, and this
  compiler reads the same tokens as a declarator.  The two agree until `x` is a
  known value.  A parameter name may not be qualified, so the input is outside
  the boundary `parsing.md` states; the reproducer is in
  `student.tests/syntax_differential.pl` as the one known difference, and the
  harness reports if it ever goes away.
- **The template-name category is coarse**: a template declaration's entity is
  a template-name of the enclosing scope only where the declaration bound it as
  a type name, so a function template's name takes no category (it was invisible
  before this turn, and `validate<int>()` still reads as a template-id through
  the bare-name speculation).  Giving function templates their own category
  would make `N::f<int>` a template-id inside an argument list; no fixture
  reaches it, and the token-identity of the two categories is a later stage's
  question.
- The dump spells a non-type template parameter's bare default literal as
  `TT_LITERAL:0` when the parameter is one keyword type specifier with no
  declarator, and as `literal 0` otherwise.  The rule matches every case the
  reference was probed on (`int = 0`, `int N = 0`, `unsigned long = 0`,
  `enable_if<...>::type = 0`, `__enable_if_t<...> = 0`, `class T, int = 0`), but
  it is a dump-spelling artifact of the reference's parse paths, not a rule any
  document states, so it is recorded here as a checked-but-unexplained choice.
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