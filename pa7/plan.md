# PA7 (`cppgm++ --emit-semantics`) plan and handoff ledger

## Stage state

- Stage base commit: `91b13266e02a6083457e955235b56e075844a180` (the PA6 audit
  handoff, recorded on entry before any stage edit).
- Last reviewed commit: the audit commit that carries this record; the whole
  stage was reviewed here against `spec.md`, the handout, the source and the
  reference, and the review's own findings are below.
- Implementation commits: `f303ce5d` (the resolved-semantics layer, the tree,
  the dump and the driver), then the conversion, naming, statement and
  reference-binding increments listed under "Findings, changes and evidence",
  then `edd6cbe8` (the function-template slice) and the two corrections the
  slice's own probing found (`23374e43`, `36a66bec`).  The records of those
  increments are `21fae408` and `a76ef415`.  The audit added the
  point-of-declaration work described below.
- Target: `cppgm++ --emit-semantics -o <out> <src>...` runs translation phases
  1-7, the PA5 parse, the PA6 scope/type analysis, resolves expressions,
  statements, calls, conversions and the limited overload set, and writes the
  deterministic resolved-tree dump the checked-in `.ref` files define.
- Progress: **186 / 186** checked-in PA7 tests pass; `make
  test-report-through-pa7` passes **684 / 684** across the seven tracked stages
  (pa1 54, pa2 26, pa3 20, pa4 105, pa5 188, pa6 105, pa7 186); `perl
  scripts/cppgm_file_audit.pl --stage pa7 --paths dev/src` passes with the two
  pre-existing header-body warnings and no fatal issue; the personal
  differential harness agrees with the reference on **75** curated reproducers;
  the personal benchmark's dumps are byte-identical to the reference; the
  semantic sources build with no warning under `-Wall -Wextra`.

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
- `semantic_template.cpp` owns the narrowest template layer the one fixture
  that reaches templates needs: registering a function template in the scope
  its name belongs to (14.1/2), substituting a written argument list (14.2),
  deducing the arguments a call leaves out (14.8.2), and declaring the
  specialization the walk demanded (14.7.1).  It is deliberately not a
  template engine: every parameter is a type parameter, every argument is a
  type, and deduction walks only the bare, pointer, reference and function
  shapes the slice can build.  A class template, a non-type parameter, a
  template-template parameter and a partial specialization are absent rather
  than approximated.
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
- **Templates.** 14.1/2 puts a template's name in the enclosing scope, so the
  registry records it there rather than in the template-parameter scope the
  declaration itself was analysed in, and nothing about `--emit-types` changes.
  14.2/4 matches the written argument list against the template parameter list,
  so two templates of one name that share a parameter list both produce a
  substitution; which one a template-id denotes is then 13.4's question,
  answered by the target type of the address, and the answer has to exist
  because a name that denotes several functions has no type of its own.  A
  call written with an explicit argument list ranks the specializations the
  list gives like any other candidate set.  A specialization is only
  *instantiated* once the ranking has chosen it, so the dump shows exactly the
  instantiations the unit demanded, after the declarations the source wrote.
- **Statements.** A condition declaration binds in the scope its statement owns
  (6.4/3), which the PA6 dump now shows too; a `for`-init declaration belongs
  to its loop (6.5.3/1); a case label is an integral constant expression; and
  `break`, `continue`, `default` and a `void` return are checked against the
  statement that encloses them.

### What limits a name lookup

The stage reads a tree the analysis has already finished, so a scope holds every
name the unit ever declares in it - including names declared *after* the use the
walk is resolving.  3.3.1 makes a name visible only from its own point of
declaration, and the reference enforces it: it rejects `void use() { take(1); }
void take(int);`, `int n = sizeof(m); int m;`, `void use() { n::f(); } namespace
n { void f(); }` and a using-declaration or using-directive written after the
use, while it accepts the same names where a later declaration is legitimately
visible (a member body, which 3.3.7/1 makes a complete-class context).

So the analysis records a point of declaration.  `Model::SetPoint` is the
position of the declaration being analysed, and every binding made while it is
current - the binding line, the scope's lookup entry, and the namespace a
using-directive nominates - carries it.  `Model::Visible` is the whole rule: a
declaration answers a use when its point precedes the use's, and a use that
carries no position (the analysis itself) sees everything.

The PA7 walk carries the position of the construct it is reading through
`Analyzer::LimitScope`, which sets `visible_limit_` from the syntax node being
resolved and restores the enclosing limit on exit.  A resolution that carries no
position of its own therefore stays unbounded rather than inheriting a
neighbour's, which is what keeps the change from narrowing a lookup it does not
own.  The one deliberate exception is a deferred member body: the reference
resolves one after the whole unit, so `SemanticsImplicitBodies` opens the limit
(`visibility_open_`) for it.

`CollectFrom` keeps its fast path: a scope's bindings are in source order, so
when the name's last declaration precedes the use every earlier one does too,
and the candidate scan is the one it always was.  Only a name whose last
declaration follows the use pays for a search back to the last visible one, and
a name declared here only later leaves the enclosing scope to answer - which is
what keeps `int g(); void f() { g(); int g; }` resolving to the outer `g`.

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
- **`300-static-cast-overloaded-function-template-argument` needs templates.**
  The last red fixture writes `&hello<stream>` and calls `take(...)`, so the
  stage grew the template slice above.  It is the one place the handout's *Out
  Of Scope* list and the checked-in fixtures disagree: "template functions or
  template-aware overload resolution" is out of scope, and the fixture is a
  `tests/general/` intake case that reaches both.  The fixtures are the oracle,
  so the slice was built rather than the fixture waived; the reference's own
  `--emit-types` for the same input instantiates too, which is what says the
  behaviour is the reference's design rather than a bug in it.
- **An argument of type void is not a value** (5.2.2/4).  Found while probing
  the deduced-call path: `take(other(1))` where `other` returns void was
  accepted.  The reference fails on this input with an internal consistency
  error and `g++ -std=c++11` rejects it, so the call layer now rejects it at
  the argument list, where the rule lives, rather than at one call form.
- **A template-id is a name the constant layer must not call unknown.**
  `void (*p)(int) = &hello<int>;` was rejected with `unknown name`, because the
  PA6 analysis evaluates every initializer and `EvaluateIdentifier` threw when
  ordinary lookup found nothing.  14.2 makes the template-id a name for a
  function, which is not an integral constant expression but is not an unknown
  name either, so the layer now reads it as one and reports "not a constant".
- **An unevaluated operand demands no instantiation** (5.3.3/1).
  `sizeof(&hello<int>)` printed the instantiation in this compiler and prints
  none in the reference, because `sizeof`'s operand is unevaluated and 14.7.1
  instantiates a specialization only where a definition is required.  The
  substitution a `sizeof` operand made is now forgotten as well as the record,
  so a later evaluated demand is the one that instantiates it - which is what
  `student.tests/semantics_differential.pl`'s
  `a-later-demand-instantiates-what-sizeof-skipped` pins.
- **Deduction keeps or drops an argument's cv the way 14.8.2 says.**  The
  adjustment belongs at the top of the match and nowhere below it: the
  argument's top-level cv is ignored only where the parameter is not a
  reference, so `f(T)` takes a `const int` as `int` while `f(T&)` and `f(T*)`
  take it as `const int`; a cv-qualified parameter or pointee absorbs it, so
  `f(const T&)` and `f(const T*)` take it as `int` again.  Each of the five
  readings was checked against `g++ -std=c++11` before the reference was asked,
  and six reproducers pin them.

### Found by the audit

- **A name was visible before it was declared.**  This is the gap the handoff
  ledger recorded as the stage's largest, and the audit fixed it rather than
  carrying it further.  The PA7 walk resolves names against the finished
  analysis, so it saw every declaration in a scope; the reference resolves at
  the use, so it rejects a forward use (3.3.1).  Nine reduced reproducers now
  pin the rule in `student.tests/semantics_differential.pl` - a later function,
  a later overload, a later qualifier, a later initializer target, a later
  using-declaration and a later using-directive are each rejected, and
  `int g(); void f() { g(); int g; }` still resolves to the outer `g` because a
  declaration that is not yet visible does not hide one that is.  The design is
  above; the change is `Model::Visible`/`SetPoint`, `Analyzer::LimitScope`, the
  point a `Binding` and a scope's lookup entry now carry, and the point a
  using-directive's nomination carries.
- **A using-directive nominated from the whole unit rather than from itself.**
  `namespace n { int target(); } void use() { target(); } using namespace n;`
  was accepted; 7.3.4/2 with 3.3.1 makes the nomination take effect at the
  directive, so the reference rejects it.  The directive now carries its own
  point of declaration, and both the model's lookup walk and the candidate
  collector skip a directive written after the use.
- **Six dead pieces in the semantic sources.**  `DigitValue` and `EndsWith` in
  `semantic_expression.cpp`, `Number` in `semantic_semantics.cpp`, an unused
  `cv` in `ClassOfPointer`, an `index_side` and a no-op statement in
  `SemSubscript`, and a duplicated `if(body < 0) return definition;` in
  `SemFunctionDefinition`.  `dev/src/semantic/*.cpp` now compiles with no
  warning under `-Wall -Wextra`, which the earlier record claimed and the build
  did not show.  `SemSubscript`'s element type was a ternary whose two arms were
  the same expression; it is now the operand type's own operand, with the
  comment that says why an array and a pointer agree.
- **The benchmark timed the reference through its wrapper.**  `pa7/cppgm++-ref`
  is `scripts/run_reference_binary.sh`, which starts a Perl process to check the
  binaries before exec'ing the compiler; measured on the frozen corpus that adds
  0.25 s to every reference run, and the earlier record reported it as the
  reference compiler's own latency (1.309 s rather than 1.044 s).  The benchmark
  now ensures the binaries once and times `reference-binaries/cppgm++`
  directly, so both arms time a compiler and nothing else.  The honest effect is
  smaller than the earlier record claimed and is reported below.

  The same wrapper is still what `pa5/syntax_benchmark.pl` and
  `pa6/types_benchmark.pl` invoke the reference through, so the absolute
  reference latency those two plans record, and the paired difference they
  rest their speed claim on, carry the wrapper's fixed quarter second: a
  constant cancels in an A/A arm and does not cancel in an A/B one.  Those are
  earlier stages' records and this audit did not rewrite them; the measurement
  above is the evidence for whoever corrects them, and the student numbers they
  quote are unaffected, because both of those labels run this compiler.

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
byte for byte before any timing is accepted.  Measured on the audit commit:

| tool | latency (s) | peak RSS (MB) |
| --- | --- | --- |
| `cppgm++-ref` (the binary) | 1.044 [1.040..1.212] | 147.8 [147.6..147.9] |
| `cppgm++` | 0.889 [0.884..1.052] | 130.4 [130.0..130.6] |

Paired difference (mine - ref): median -0.1512 s, 5 of 5 blocks negative, MAD
0.0096 s, range [-0.1628..-0.1395].  A/A calibration on the same schedule:
paired difference median +0.0114 s, 0 of 5 blocks negative, MAD 0.0074 s,
range [+0.0040..+0.1079].  The noise floor is the MAD, and the latency effect
is 20x it: the compiler is 14.5% faster than the reference on this corpus and
uses 11.8% less peak memory.  Every observation is kept in
`/tmp/pa7_semantics_benchmark/semantics_benchmark.tsv`.  The dump is
byte-identical to the reference, so the latency difference is the compiler's
own work and not a different output.

The earlier record reported the reference through `pa7/cppgm++-ref`, which adds
a fixed 0.25 s of wrapper startup to every run (1.309 s rather than 1.044 s);
the effect it reported, -0.4211 s, was that wrapper plus the real difference.
The wrapper is still what the correctness harnesses use; only the timing arm
changed, and the earlier numbers are kept above in "Findings, changes and
evidence" rather than silently replaced.

The point-of-declaration tables are the audit's one measurable cost.  On the
same 4 000-group corpus, measured either side of the change: 1.15 s / 154.4 MB
before, 1.16 s / 157.2 MB after - about 1% latency and 1.8% peak memory for
correct lookup, with the dump byte-identical either way.  Well inside the
level's budget, and the compiler is still ahead of the reference on both.

The scaling is linear in the declarations and the expressions they hold,
measured from outside on the same corpus generator, on the audit commit:

| groups | source bytes | latency (s) | peak RSS (MB) |
| --- | --- | --- | --- |
| 500 | 214 527 | 0.15 | 23.8 |
| 1 000 | 428 528 | 0.29 | 42.6 |
| 2 000 | 857 528 | 0.58 | 80.0 |
| 4 000 | 1 715 528 | 1.16 | 157.0 |

Doubling the input doubles both, which is what "semantic work tracks actual
declarations, lookup candidates and demanded specialization facts" means.  No
optimization bodies, caches or telemetry surface were added, so there is no
compiler-work budget to justify and none is claimed.

`--emit-semantics` has no executable output, so generated-program runtime and
generated text size do not exist at this stage; the only code this stage emits
is the dump, whose size is reported above beside the latency and the memory.
There is no executable benchmark to review here, and none is invented.

## Validation

- `make test-report-through-pa7` - 684 / 684 (pa1 54, pa2 26, pa3 20, pa4 105,
  pa5 188, pa6 105, pa7 186); `make test-pa7` alone is 186 / 186.
- `perl scripts/cppgm_file_audit.pl --stage pa7 --paths dev/src` - pass, with
  the two pre-existing warnings about the two headers' inline bodies and no
  fatal issue.
- `student.tests/semantics_benchmark.pl` - dumps byte-identical to the
  reference; latency and peak RSS reported above.
- `student.tests/semantics_differential.pl` - **75** curated reduced
  reproducers agree with the reference in exit status and dump: the conversion,
  ranking, value-category, naming, statement-scope and rejection corners, the
  function-template corners, and the nine point-of-declaration readings the audit
  added.
- `dev/src/semantic/*.cpp` compile with no warning under `-Wall -Wextra`.
- The two architecture traces below were run on the audit commit and their dumps
  compared byte for byte with the reference's.

### Architecture traces

**A declaration with a class, a member access and a conversion.**  For

```cpp
struct point { int x; int y; };
int take(const void* value);
int use(int a) { point p; p.x = a; return take(&p); }
```

the phases run once each and in one direction: bytes to `TranslatedSource`, the
token cursor to `PostTokenStream`, the parser to the `SyntaxArena`, and
`Analyzer::Run` over that arena in source order - `AnalyzeClassSpecifier` opens
the class scope and binds `x` and `y` with their points of declaration,
`BuildDeclarator` builds `take`'s function type from the pointer and `const void`
it names and `NoteDeclaration` records the fact at the declarator node.  No
syntax is re-parsed and no semantic decision is recomputed.  `BuildSemantics`
then reads those facts back: `SemFunctionDefinition` takes the recorded type and
scope, `SemSimpleDeclaration` reads `p`'s declaration fact rather than
re-analysing the declarator, the member access resolves through the recorded
class scope, and the call collects candidates from the scope's indexed values
with the callee's point as the visibility limit, ranks the one conversion
(4.10's object pointer to `const void*`), and prints the `callee` line.  The
dump is byte-identical to the reference's, including the `constructor-action`
the implicit constructor needs.

**A demanded template.**  For `template<class T> void take(T); void use() {
take(1); }`, `AnalyzeTemplateDeclaration` opens one template-parameter scope,
binds `T` there, analyses the declaration *in that scope* so the template's
own entity never enters the enclosing scope's value table, and registers the
template in the enclosing scope with the declaration's own scope (14.1/2).
The walk prints nothing for the declaration itself (`SemTemplateDeclaration`
returns no node, as the reference does) and, at the call, ordinary candidate
collection finds nothing, so `FindFunctionTemplates` is asked: 14.8.2 deduces
`T = int` from the argument, the specialization is *substituted* but not yet
instantiated, the ranking picks it, and only then does
`InstantiateFunctionTemplate` declare it - once, memoized by (template,
substituted type) in `specializations_`, and recorded in demand order for
`SemInstantiations` to print after the unit's own declarations.  The trace
prints `callee take function of (int) returning void` and one
`function-declaration take function of (int) returning void` line, byte-identical
to the reference.

## Handoff ledger

### Unfinished implementation

One piece of the template slice is still missing, recorded with a reduced
reproducer rather than waived.  It is not a failing fixture: the checked-in
suite is 186 / 186.

1. **A template definition is not instantiated as a definition.**
   `template<class T> void take(T) {} void use() { take(1); }` prints
   `function-definition take function of (int) returning void` with a
   `compound-statement` in the reference and `function-declaration` without one
   here.  The boundary is that a body is not a type: instantiating one means
   opening a scope for the specialization whose parameter entities carry the
   substituted types and re-walking the body there, while every nested statement
   still reads `Model::ScopeAt`, which the PA6 analysis filled with the
   *template's* scope.  That is a second instantiation engine, not an extension
   of the substitution this stage has, and the handout puts it out of scope
   twice over ("template functions", "class-aware call resolution").  The
   fixture set reaches templates only through declarations, and the audit kept
   the boundary rather than growing the engine inside an audit.

The audit's own gap - **a declaration visible before it is declared** - is
fixed; see "Found by the audit" above for the rule, the reduced reproducers and
the measurement.

### Known differences outside the slice

These are inputs the handout's *Out Of Scope* list covers, kept so the
divergence is visible rather than discovered later.  None is a checked-in
fixture.

- **Constructor selection.**  `take(stream())` and `take(value)` with a
  class-typed `value` need the reference's `constructor-action` node and an
  implicit constructor body.  `template<class T> void take(T); struct s {};
  void use() { take(s()); }` succeeds in the reference and is rejected here.
- **A reference-internal error.**  `template<class T> void take(T); template
  <class U> void other(U); void use() { take(other(1)); }` fails in the
  reference with `function template parameter metadata does not match
  declarator`, which is a consistency check rather than a language diagnostic.
  `g++ -std=c++11` rejects the input for the argument's type, and this compiler
  now rejects it for the same reason, so the two agree on the exit status and
  not on the message - diagnostic text is not compared.
- **A member body the unit does not demand.**  `struct S { int f() { return 0; }
  };` prints nothing in the reference and the body here, because the reference
  prints a deferred member body only where the unit demands it (3.2), and this
  stage prints every deferred body it recorded.  `struct S { int f() { return g();
  } }; int g();` is the same difference with a name in the body.  The one
  fixture that reaches an in-class body demands it
  (`300-deferred-demand-closure` takes the member's address), which is why the
  two agree there.  Both sides exit 0, so this is a dump difference inside
  "class-aware call resolution", which the handout puts out of scope.
- **A member named inside a member body.**  `struct S { int m; int f(); }; int
  S::f() { return m; }` prints `id-expression lvalue int m` here and
  `member-expression lvalue int m` over `id-expression prvalue pointer to struct
  S this` in the reference, which writes the implicit object parameter the
  handout's *Out Of Scope* list names.  Both exit 0.
- **The order of an implicit constructor against a deferred body.**  Where both
  appear, the reference prints them in demand order and this stage prints the
  deferred bodies first; the audit's `struct S { int f() { return 0; } }; int g()
  { S s; return s.f(); }` shows it.  No fixture pins the order, and both are
  class-surface lines the handout puts out of scope.

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
5. **A template-id with several specializations is resolved by its target.**
   `template<class T> void hello(T); template<class T> void hello(T, int);` and
   `&hello<stream>` inside `static_cast<void(*)(stream)>(...)` picks the
   one-parameter specialization, and it picks the same one whichever order the
   two templates are declared in.  Without the cast the reference fails with
   `invalid PA6 type identity`, which is why the stage reads 13.4's
   target-directed rule rather than declaration order.  A reviewer should
   decide whether a later stage keeps that reading or resolves the template-id
   against the target type only where one is written.
6. **The dump's instantiation order.**  An instantiation prints after the
   unit's own declarations, in the order the walk first demanded one, which is
   what the one fixture that reaches templates shows.  Where an instantiation
   and a deferred member body or an implicit constructor both appear the order
   between them is not pinned by any fixture; the stage prints the
   instantiations last.
7. **The resolved tree holds rendered text per node.**  A `SemNode` carries
   four `std::string` members - the tag, the value category, the resolved type
   spelling and the trailing label - plus a `std::vector<int>` of children, so a
   node with children costs one allocation for that vector and a heap block for
   every member that exceeds the small-string buffer, of which the type
   spelling always does.  The tree is the stage's *output* rather than a node
   the frontend revisits - it is built once per printed line, 267 094 of them on
   the frozen corpus, and walked once by the writer - so the audit left it
   alone: the compiler is already 14.5% faster than the reference on that corpus
   and uses 11.8% less peak memory, and re-cutting the representation (a type
   id rendered at write time, interned tags, children as arena edges like the
   syntax tree's) is a performance change with its own regression risk, not a
   correctness repair.  A later stage that starts consuming this tree rather
   than printing it should make that change first.

### Known differences from the reference

None inside the slice.  The differential harness's curated list holds the
readings that could have diverged - twenty-one template reproducers and the nine
point-of-declaration readings the audit added among them - and every one of them
agrees in exit status and dump.  The differences the audit found are all in the
class surface the handout's *Out Of Scope* list names, and they are listed above
with the reduced reproducer for each.

The audit also swept 74 further reduced probes outside the harness - 49 in the
first sweep and 25 in the second - covering forward and backward uses across
namespaces, blocks, classes, using-declarations, using-directives, namespace
aliases, enumerations, templates and initializers.  Every one agrees with the
reference on exit status; every probe that differs in the dump is a class-surface
item listed above, which the handout puts out of scope.
