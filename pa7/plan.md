# PA7 (`cppgm++ --emit-semantics`) plan and handoff ledger

## Stage state

- Stage base commit: `91b13266e02a6083457e955235b56e075844a180` (the PA6 audit
  handoff, recorded on entry before any stage edit).
- Last reviewed commit: `91b13266` (no stage audit has run yet).
- Implementation commits: `f303ce5d` (the resolved-semantics layer, the tree,
  the dump and the driver), then the conversion, naming, statement and
  reference-binding increments listed under "Findings, changes and evidence",
  then `edd6cbe8` (the function-template slice) and the four corrections the
  slice's own probing found (`21fae408`, `23374e43`, `36a66bec` and the record
  commit).
- Target: `cppgm++ --emit-semantics -o <out> <src>...` runs translation phases
  1-7, the PA5 parse, the PA6 scope/type analysis, resolves expressions,
  statements, calls, conversions and the limited overload set, and writes the
  deterministic resolved-tree dump the checked-in `.ref` files define.
- Progress: **186 / 186** checked-in PA7 tests pass; `make
  test-report-through-pa6` passes 498 / 498; `perl
  scripts/cppgm_file_audit.pl --stage pa7 --paths dev/src` reports no issue; the
  personal differential harness agrees with the reference on 66 curated
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
byte for byte before any timing is accepted.  Measured on `23374e43`, after the
template slice:

| tool | latency (s) | peak RSS (MB) |
| --- | --- | --- |
| `cppgm++-ref` | 1.308 [1.290..1.319] | 147.9 [147.7..148.0] |
| `cppgm++` | 0.887 [0.880..1.091] | 129.7 [129.3..129.9] |

Paired difference (mine - ref): median -0.4211 s, 5 of 5 blocks negative, MAD
0.0177 s, range [-0.4387..-0.2120].  A/A calibration on the same schedule:
paired difference median +0.1036 s, 0 of 5 blocks negative, MAD 0.0104 s,
range [+0.0075..+0.1217].  The A/A arm took an excursion on this run - its
range is 12x its own MAD - so the honest noise floor is the MAD, and the
latency effect is 40x it.  Every observation is kept in
`/tmp/pa7_semantics_benchmark/semantics_benchmark.tsv`.  The dump is
byte-identical to the reference, so the latency difference is the compiler's
own work and not a different output.

The scaling is linear in the declarations and the expressions they hold,
measured from outside on the same corpus generator, on the same commit:

| groups | source bytes | latency (s) | peak RSS (MB) |
| --- | --- | --- | --- |
| 500 | 214 527 | 0.155 | 23.1 |
| 1 000 | 428 528 | 0.296 | 41.6 |
| 2 000 | 857 528 | 0.584 | 78.0 |
| 4 000 | 1 715 528 | 1.171 | 150.8 |

Doubling the input doubles both, which is what "semantic work tracks actual
declarations, lookup candidates and demanded specialization facts" means.  No
optimization bodies, caches or telemetry surface were added, so there is no
compiler-work budget to justify and none is claimed.

## Validation

- `make test-pa7` - 186 / 186.
- `make test-report-through-pa6` - 498 / 498 (pa1-pa4 205 / 205, pa5 188 / 188,
  pa6 105 / 105).
- `perl scripts/cppgm_file_audit.pl --stage pa7 --paths dev/src` - pass (77
  files checked, 2 warnings about the two headers' inline bodies).
- `student.tests/semantics_benchmark.pl` - dumps byte-identical to the
  reference; latency and peak RSS reported above.
- `student.tests/semantics_differential.pl` - 66 curated reduced reproducers of
  the conversion, ranking, value-category, naming, statement-scope, template
  and rejection corners agree with the reference in exit status and dump.
- `dev/src/semantic/*.cpp` compile clean under `-Wall -Wextra`.

## Handoff ledger

### Unfinished implementation

Two related pieces of the template slice are still missing, and both are
recorded with a reduced reproducer rather than waived.  Neither is a failing
fixture: the checked-in suite is 186 / 186.

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
   fixture set reaches templates only through declarations.
2. **A declaration is visible before it is declared.**  `void use() { take(1); }
   void take(int);` is rejected by the reference and by `g++ -std=c++11`, and
   accepted here, because the PA7 walk runs after the whole PA6 analysis and
   `CollectCandidates` reads the scope's final binding list.  The same holds for
   the template path: with `template<class T> void take(T); void use() {
   take(1); } void take(int);` the reference resolves the call to the
   instantiation and this compiler resolves it to the later non-template.  This
   is not a template defect - it is the shared lookup's, and it predates this
   stage - but it is the largest correctness gap the stage has.  Fixing it means
   recording the point each declaration landed at and filtering lookup by the
   use's own point, and that rule has to exempt a class's complete-class context
   (3.3.7), where a later member *is* visible; the change touches every lookup
   path in a stage that currently passes 186 / 186 and 498 / 498, so it is
   recorded for the audit rather than attempted here.

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

### Known differences from the reference

None inside the slice.  The differential harness's curated list holds the
readings that could have diverged - now including twenty-one template
reproducers - and every one of them agrees in exit status and dump.
