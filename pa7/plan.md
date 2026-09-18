# PA7 (`cppgm++ --emit-semantics`) plan and handoff ledger

## Stage state

- Stage base commit: `91b13266e02a6083457e955235b56e075844a180` (the PA6 audit
  handoff, recorded on entry before any stage edit).
- Last reviewed commit: `91b13266` (no stage audit has run yet).
- Implementation commits: `f303ce5d` (the resolved-semantics layer, the tree,
  the dump and the driver), then the conversion, naming, statement and
  reference-binding increments listed under "Findings, changes and evidence".
- Target: `cppgm++ --emit-semantics -o <out> <src>...` runs translation phases
  1-7, the PA5 parse, the PA6 scope/type analysis, resolves expressions,
  statements, calls, conversions and the limited overload set, and writes the
  deterministic resolved-tree dump the checked-in `.ref` files define.
- Progress: **185 / 186** checked-in PA7 tests pass; `make
  test-report-through-pa6` passes 498 / 498; `perl
  scripts/cppgm_file_audit.pl --stage pa7 --paths dev/src` reports no issue; the
  personal differential harness agrees with the reference on 44 curated
  reproducers; the personal benchmark's dumps are byte-identical to the
  reference.

## Design / spec alignment

The stage is a seventh consumer of the shared phase 1-7 frontend, and the third
stage that owns a tree:

```text
file bytes -> TranslatedSource -> PPTokenizer -> PPTokenReader
   -> Preprocessor -> PostTokenStream -> SyntaxTokenSink
   -> SyntaxParser -> SyntaxArena -> Analyzer -> Model -> SemTree -> the dump
```

`dev/src/semantic/` gains four pieces beside the PA6 layer:

- `semantics_tree.h` is the resolved output tree: one node per printed line,
  holding the tag, the value category, the resolved type spelling and the
  trailing or leading label.  Keeping it apart from `SyntaxArena` is what lets
  the builder *replace* a node - a parenthesized expression disappears, an
  initializer becomes a child of the variable it initializes, a call's
  `argument-list` becomes the call's own children, a reference binding becomes
  the cast that made it - without mutating the parse.
- `semantic_expression.cpp` resolves one expression at a time to a type, a
  value category and, where a call is involved, the function the overload set
  chose.  It owns the standard-conversion subset, 4.4's recursive
  qualification rule, the 13.3.3.2 ranking and the reference bindings.
- `semantic_semantics.cpp` walks declarations and statements, reading back the
  scope and declaration facts the PA6 analysis recorded rather than deriving
  them again.
- `semantics_dump.cpp` and `semantic_driver.cpp` print the tree.

The two passes stay apart because they answer different questions.  The PA6
analysis decides what every declaration binds and which scope every statement
belongs to; this stage reads those answers through `Model::ScopeAt`,
`Model::DeclarationAt` and `Model::EntityScope`, so a scope is never opened
twice and a declarator is never analysed twice.  Two facts were added to the
model for it: the syntax node each scope and declaration landed at, and the
pointer-to-member type former and base-class list the call layer needs.

### What the stage reads, by area

- **Conversions.** 4.1 drops a prvalue's top-level cv; 4.2 and 4.3 decay an
  array and a function to a pointer, and only toward a pointer, so an array
  return type stays an exact match; 4.4's level rule admits `int**` to
  `const int* const*` and rejects `int**` to `const int**`; 4.10 admits an
  integer literal zero and `nullptr` to a pointer and to `nullptr_t`; 4.12
  makes a pointer convert to `bool`; 8.5.3/5 lets a cv-qualified lvalue
  reference bind a prvalue and forbids an unqualified one, and forbids an
  rvalue reference binding an lvalue; 13.3.3.1.4 converts to the referred type
  where a reference cannot bind the argument itself, and binds a base
  subobject by a derived-to-base conversion.
- **Ranking.** 13.3.3.2 compares rank, then the reference bindings (an rvalue
  reference to an rvalue, a binding to the argument over one to a temporary,
  the less cv-qualified of two otherwise equal targets), then a pointer
  conversion's proper subsequence, then a pointer conversion over a boolean
  one.
- **Value categories.** The dump prints what each operator yields: an
  assignment and a prefix increment yield the left operand's referred type as
  an lvalue, a postfix increment yields a prvalue, a subscript and a member
  access yield lvalues, a conditional yields an lvalue only for two lvalues of
  one type, and a call yields the value category its return type names.
- **Names.** A class type prints the qualified name of the scope it is a member
  of, so a nested class reads `struct N::S`; an enumeration prints its own
  name; a function declaration prints its entity's qualified name; an
  id-expression prints what the source wrote.  An unnamed namespace is not a
  component.
- **Statements.** A condition declaration binds in the scope its statement owns
  (6.4/3), which the PA6 dump now shows too; a `for`-init declaration belongs
  to its loop (6.5.3/1); a case label is an integral constant expression; and
  `break`, `continue`, `default` and a `void` return are checked against the
  statement that encloses them.

### What the PA5 tree gained

- `PrimaryExpression` accepts a functional cast written with a multi-word type
  name (`unsigned long(e)`) and with a `decltype` type name
  (`decltype(x)(1)`), both of which the shared grammar admits and only the
  single-keyword form was read for.  The `decltype` form is taken only where an
  argument list follows, so `decltype(f<T>())::value` still names a member.
- `AnalyzeSpecifiers` and `AnalyzeClassForward` look an elaborated class
  specifier up where it is written (3.4.4/2), so an elaborated `struct S`
  inside a block finds the `S` an outer scope declared instead of declaring a
  fresh incomplete class.

## Findings, changes and evidence

Every item below was found by running the checked-in suite and the differential
harness, and each is a reading the standard and the reference agree on:

- **The scope of a condition declaration.** `if (int x = f())` bound nothing in
  the PA6 analysis, because the handout puts statement analysis beyond scope
  creation out of scope for PA6.  The reference binds it in the scope the
  selection statement owns, and so does this stage now, in the same pass.
- **A parameter was not an ordinary name.** A parameter was registered as a
  declaration line but not in the scope's value map, so `return x;` could not
  resolve it.  It is now bound like any other declaration (3.3.3/1), and an
  unnamed parameter binds nothing.
- **A duplicate definition was the same *name*, not the same signature.**
  `int g(int)` and `long g(long)` are one entity in the model but two
  functions, so the duplicate is a (scope, signature, name) triple.
- **`C::*` is a pointer operator, not a token kind.** The declarator read the
  operator's label through the `KW_`/`OP_` prefix rule, which turns `C::*` into
  `:*`; the source-text form is now read as written.
- **A functional cast may name a keyword type.** `double(x)` reaches the call
  layer as an id-expression whose label is the word itself, not a token kind.
- **A nested-name-specifier is looked up before a using-directive's names.**
  3.4.3.1/1 with 7.3.4/3: `detail::frame_header` inside `websocket` reached the
  namespace a directive nominated rather than the nearer one the enclosing
  scopes declare.  The PA6 reference accepts that program and this compiler
  did not, so the fix is a defect repair in the shared lookup, not a PA7
  accommodation.
- **A member of a base class is a member of the derived one** (10/1), and a
  member reached through a const object is const (9.3.2/2).
- **The target type chooses the member an address denotes** (13.4), which is
  what tells two cv-qualified member overloads apart.

## Performance evidence

The protocol is the one section 9 asks for and is what
`student.tests/semantics_benchmark.pl` implements: fixed binaries, flags and
input; wall-time ABBA blocks; an A/A arm that calibrates the noise floor;
paired per-block differences as well as per-label medians and spread; the
produced text's size beside latency and peak RSS; every observation kept in a
TSV.  The corpus is the shape this stage is paid for - overload sets the call
layer ranks, conversions of every supported kind, pointer and reference
parameters, local declarations and control flow, and qualified and unqualified
calls into nested namespaces.  `--emit-semantics` has no executable output, so
there is no generated-program runtime or text size at this stage and none is
invented.

Frozen protocol, 3 000 groups (1 286 528 B of source, a 12 200 816 B dump of
267 094 resolved nodes), 5 ABBA blocks, 20 timed runs per label, dumps compared
byte for byte before any timing is accepted:

| tool | latency (s) | peak RSS (MB) |
| --- | --- | --- |
| `cppgm++-ref` | 1.335 [1.291..1.402] | 147.8 [147.5..148.0] |
| `cppgm++` | 0.889 [0.882..1.025] | 129.6 [129.4..129.8] |

Paired difference (mine - ref): median -0.4483 s, 5 of 5 blocks negative, MAD
0.0315 s, range [-0.4798..-0.2663].  A/A calibration on the same schedule:
paired difference median +0.0058 s, 2 of 5 blocks negative, MAD 0.0117 s,
range [-0.0077..+0.0422].  The latency effect is 38x the noise floor.  Every
observation is kept in
`/tmp/pa7_semantics_benchmark/semantics_benchmark.tsv`.

The scaling is linear in the declarations and the expressions they hold,
measured from outside on the same corpus generator:

| groups | source bytes | latency (s) | peak RSS (MB) |
| --- | --- | --- | --- |
| 500 | 214 337 | 0.14 | 22.9 |
| 1 000 | 428 338 | 0.29 | 41.5 |
| 2 000 | 857 338 | 0.57 | 78.1 |
| 4 000 | 1 715 338 | 1.15 | 150.8 |

Doubling the input doubles both, which is what "semantic work tracks actual
declarations, lookup candidates and demanded specialization facts" means.  No
optimization bodies, caches or telemetry surface were added, so there is no
compiler-work budget to justify and none is claimed.

## Validation

- `make test-pa7` - 185 / 186 (see the known difference below).
- `make test-report-through-pa6` - 498 / 498 (pa1-pa4 205 / 205, pa5 188 / 188,
  pa6 105 / 105).
- `perl scripts/cppgm_file_audit.pl --stage pa7 --paths dev/src` - pass (76
  files checked, 2 warnings about the two headers' inline bodies).
- `student.tests/semantics_benchmark.pl` - dumps byte-identical to the
  reference; latency and peak RSS reported above.
- `student.tests/semantics_differential.pl` - 44 curated reduced reproducers of
  the conversion, ranking, value-category, naming, statement-scope and
  rejection corners agree with the reference in exit status and dump.
- `dev/src/semantic/*.cpp` compile clean under `-Wall -Wextra`.

## Handoff ledger

### Unfinished implementation

One checked-in fixture remains: `300-static-cast-overloaded-function-template-
argument` reaches **template-id instantiation and template argument
deduction**, which the PA7 handout puts out of scope ("template functions or
template-aware overload resolution" under *Out Of Scope*, and "template
functions" in the same list).  The fixture writes `&hello<stream>` and calls
`take(...)`, a function template whose parameter is deduced from the argument;
the reference prints the two instantiations as declarations after the unit's
own.  Nothing else in the stage depends on it: the other fixture that names a
member template (`300-static-cast-member-overload-prefers-nontemplate`) passes,
because its template member is simply not a candidate PA7 instantiates.

The boundary is concrete rather than budgetary: a template-id needs a
specialization the analysis never demanded, and a deduced call needs the
argument-deduction machinery of 14.8.2.  Both belong to the later assignment
that owns templates and demand, and neither can be approximated here without
either hard-coding the fixture or building a second, partial template engine
inside this stage.  This is unfinished *implementation* in the sense that the
fixture is red, and it is recorded rather than waived.

### Independent review questions

These are recorded, not waived.  None of them is a failing fixture; each is a
reading the stage chose where the handout does not pin the behaviour.

1. **A derived-to-base binding and a converted temporary print as casts.**
   `describe_reference(root)` with a `node` argument prints
   `cast-expression lvalue struct node_base` around the argument, and a
   reference bound to a converted temporary prints
   `cast-expression prvalue const long long int` around it.  Nothing in the
   handout describes either node; they are how the checked-in oracle shows a
   conversion the reference's own tree keeps, and the stage reproduces them
   because the fixtures are the oracle.  A reviewer should decide whether a
   later stage keeps them.
2. **An enumeration prints its own name, a class its qualified one.**  The
   reference prints `struct N::S` for a nested class but `enum class state` for
   a member enumeration declared out of class.  The stage follows the oracle on
   both, but the asymmetry looks like the reference's enumeration path rather
   than a rule about either type.
3. **`nullptr_t` is recognised without a declaration.**  The PA7 slice reaches
   `void take(nullptr_t)` with no declaration of the name; the reference
   accepts it, so the stage resolves `nullptr_t` to the fundamental type when
   ordinary lookup finds nothing.  It is gated to the semantics mode so the
   PA6 dump is unchanged.
4. **A class functional cast is rejected.**  `B b(A());` is ill formed for
   reasons of declaration syntax the PA7 slice does not model, and a
   class-typed functional cast with arguments is outside the handout's slice;
   the stage rejects it rather than building a constructor call it cannot rank.

### Known differences from the reference

None beyond the one red fixture above.  The differential harness's curated list
holds the readings that could have diverged; every one of them agrees.
