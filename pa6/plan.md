# PA6 (`cppgm++ --emit-types`) plan and handoff ledger

## Stage state

- Stage base commit: `72753f7de48ba2c125c887db4484598bb5ccd3e4` (the PA5 final
  audit, recorded on entry before any stage edit).
- Last reviewed commit: `dc7e6784` (the stage audit's eighteen readings), with
  `4e4f9018` (the differential guard for them) and this record following it.
- Implementation commits: `04cc34b2` (the semantic layer, the dump, the driver
  and the two PA5 tree extensions), `f4799570` (the child-iteration fix and
  four readings the sweep found), `9ac13101` (the class layout model),
  `fd0945a5` (the conditional operator and `sizeof` of an object), `dc7e6784`
  (the audit's fixes) and `4e4f9018`.
- Target: `cppgm++ --emit-types -o <out> <src>...` runs translation phases 1-7
  and the PA5 parse for each primary source, analyses each translation unit
  into scopes, declarations, entities and canonical types, and writes the
  deterministic scope/type dump the checked-in `.ref` files define.
- Progress: **105 / 105** checked-in PA6 tests pass, `make
  test-report-through-pa6` passes 498 / 498, `perl
  scripts/cppgm_file_audit.pl --stage pa6 --paths dev/src` reports no issue,
  and the two personal harnesses agree with the reference on 68 curated
  reproducers and 760 generated crossings.

## Design / spec alignment

The stage is a sixth consumer of the shared phase 1-7 frontend and the second
stage that owns a tree:

```text
file bytes -> TranslatedSource -> PPTokenizer -> PPTokenReader
   -> Preprocessor -> PostTokenStream -> SyntaxTokenSink
   -> SyntaxParser -> SyntaxArena -> Analyzer -> Model -> the dump
```

`dev/src/semantic/` holds the new layer:

- `semantic_model.*` is the graph `scopes-and-types.md` asks for, with the
  three ideas kept apart.  A *type* is an index into one interned table, so
  `const int` is one object however often it is written and `Same` is index
  equality; the key it is interned by is built from the operands' *indices*,
  never from a rendered spelling.  A class, enumeration or template parameter
  is nominal rather than structural, so it is never interned and its identity
  is the entity that owns it.  A *scope* owns an ordered list of declaration
  lines, the class's own data members in layout order, and the child scopes
  opened inside it; it keeps type, value and namespace names in separate maps,
  which is what 3.4.3 needs: a value named `N2` must not hide a namespace named
  `N2` from a namespace-only lookup.  An *entity* is what a declaration
  denotes, so a name declared twice prints twice while the entity stays one
  object - which is what array completion needs, because the entity holds the
  object's type and every line that denotes it prints the completed bound.
- `semantic_analyzer*.*` walks the PA5 tree once in source order, because a
  declaration's point matters: a later alias must not retroactively change an
  earlier resolved binding.  A declarator is traversed as a chain - pointer
  operators, then suffixes innermost-last, then a parenthesized inner
  declarator - which is what keeps `int *f(int)` and `int (*f)(int)` apart
  (8.3/1) and what makes `int a[2][3]` an array of two arrays of three.  A
  member function body is a complete-class context, so its scope is opened
  after the whole member list (3.3.7/1) while its parameter types are resolved
  where it is declared, which is what the reference does for
  `void f(T x) { } typedef char T;`.
- The three surfaces a declaration is read through are kept apart: the *node
  tag* says what the parser recognised, the *label* says what the source
  spelled, and neither is asked to stand in for the other.  A `decltype(...)`
  is found by the child the parser gave it, not by a spelling that begins with
  `decltype`; a keyword's word is read past its kind prefix while a punctuator
  the parser named itself (`ref-qualifier &&`) is its own label.
- A member function's cv-qualifiers and ref-qualifier are part of its type
  (8.3.5/6), and the three rules that read them are applied where the language
  applies them: a ref-qualifier needs a member function, a definition outside
  the class must find it declared (9.3.2/2), and one parameter-type-list cannot
  mix ref-qualified members with the rest (13.1/2).  Two declarations of one
  name are one function when their signatures and return types agree (13.1/3),
  and a function is defined once in a translation unit (3.2/1).
- `semantic_constant.cpp` is the integral constant subset of 5.19 the handout
  requires.  `&&`, `||` and `?:` do not evaluate the operand they do not
  select, so `1 || (1 / 0)` is a constant; `,` does evaluate its left operand
  and yields its right one (5.18/1); signed arithmetic that overflows is
  reported rather than wrapped; a scoped enumeration compares only with the
  same enumeration (7.2/9); `sizeof` and `alignof` take a type-id or the
  id-expression of an object (5.3.3/1).
- `semantic_dump.cpp` prints a scope's declarations in source order and then
  its child scopes, one line per declaration.  A `type` line prints the class
  or enum key its own declaration wrote with the name it bound, while a type
  used inside a compound spelling prints its canonical (unqualified) name, so
  `struct C; class C {};` prints two `type C` lines and one class scope.
- `semantic_driver.cpp` writes one translation unit at a time, each with its
  own `Model`, so a declaration in one source never makes a name visible in
  another (3.5).

Two pieces of the PA5 tree were extended rather than worked around:

- a node now records the token range it covers and the literal facts a literal
  token carries.  The first is what names an anonymous class-like type from the
  tokens it spans - `__anonymous_union_type__<class-key index>_<one past the
  closing brace>`, which the fixtures pin exactly - and the second is what lets
  a constant expression read a literal's value rather than re-parse its
  spelling.
- two parser readings were widened to the grammar the handout defines: an
  enumeration definition may name a member with a qualified name
  (`enum class writer::state : char { ... }`), and a class-specifier that is
  not a declaration of its own leaves the `;` to the declaration that wraps it,
  which is what the anonymous name's second number measures.

### What the audit reviewed

The whole stage was walked again from the sources rather than from this record:
every declaration form, every type former, lookup, the constant subset, the
dump and the driver, traced from source to the printed line.  The stage holds
no rendered text used as a semantic key, no roundtrip through a spelling, no
global retry and no per-node heap allocation on a hot path: interning is keyed
by operand indices, the declarator is traversed as a structure, and the dump is
the only place a type is ever rendered.

## Findings, changes and evidence

The audit's eighteen readings, all of them reachable from `pa6.gram`, none of
them reached by a checked-in fixture, and each one a defect because the
reference oracle disagrees and the standard agrees with the reference.  They
are listed by area in `dc7e6784` and guarded by a reduced reproducer each in
`4e4f9018`.  The three with the widest reach:

- **A ref-qualifier was silently dropped from the type.**  `void f() &&` printed
  as `void f() &`, so two distinct member functions were one type; the parser
  labels a cv-qualifier `KW_CONST:const` but a ref-qualifier with its spelling
  alone, and only the first form was read.  With the qualifier in the type, the
  three rules above became expressible and were added.
- **The class layout was not a layout.**  It walked the scope's declaration
  lines, so a static data member was counted inside the object, a union was
  laid out as a struct, an anonymous union's injected members were counted
  beside the union itself, and a reference member took the size of its referent
  rather than pointer size.  `sizeof` and `alignof` of a complete class are
  type-forming cases the handout reaches, so all four are now decided by a
  recorded member list.
- **A `for`-init declaration bound in the enclosing block.**  A selection or
  iteration statement owns a scope and so does its substatement (6.4/3,
  6.5.3/1); the loop's declaration was landing one level out.

## Performance evidence

The protocol is the one section 9 asks for and is what
`student.tests/types_benchmark.pl` implements: fixed binaries, flags and input;
wall-time ABBA blocks; an A/A arm that calibrates the noise floor; paired
per-block differences as well as per-label medians and spread; the produced
text's size beside latency and peak RSS; every observation kept in a TSV.  The
corpus is the shape this stage is paid for - declarations in nested namespaces,
class and enumeration definitions with member scopes, typedefs and alias
declarations, array declarators whose bounds are constant expressions, and
qualified and unqualified lookups into all of them.  `--emit-types` has no
executable output, so there is no generated-program runtime or text size at
this stage and none is invented.

Frozen protocol, 3 000 groups (961 451 B of source, a 1 581 878 B dump of
42 034 declaration and scope lines), 5 ABBA blocks, 20 timed runs per label,
dumps compared byte for byte before any timing is accepted:

| tool | latency (s) | peak RSS (MB) |
| --- | --- | --- |
| `cppgm++-ref` | 0.727 [0.724..0.740] | 79.4 [79.2..79.6] |
| `cppgm++` | 0.398 [0.395..0.405] | 37.5 [37.4..37.6] |

Paired difference (mine - ref): median -0.3308 s, 5 of 5 blocks negative,
MAD 0.0011 s, range [-0.3351..-0.3227].  A/A calibration on the same schedule:
paired difference median -0.0055 s, 5 of 5 blocks negative, MAD 0.0037 s,
range [-0.0196..-0.0017].  The latency effect is 89x the noise floor; the A/A
arm's own median is 1.5x its MAD, so the schedule carries a small systematic
bias and the effect is judged against the larger of the two spreads.  Every
observation is kept in `/tmp/pa6_types_benchmark/types_benchmark.tsv`.

The audit's fixes did not move either number: the run quoted before them was
0.403 s [0.398..0.405] against the reference's 0.735 s, and the run after is
0.398 s [0.395..0.405] against 0.727 s.  The class member list adds one vector
per class scope and one push per data member, and nothing on the walk became
superlinear.

The first measurement of this stage was the opposite: 12.760 s against the
reference's 0.730 s, a 17x regression, at a peak RSS of 37.1 MB.  The cause was
a quadratic walk, not the semantic work: the analyzer iterated a node's
children with `ChildAt(node, i)` for `i` in `0 .. ChildCount(node)`, and the
arena's child chain runs backwards, so both the count and the walk cost the
whole list - 364 million edge traversals for a translation unit of 27 000
declarations.  `SyntaxArena::CollectChildren` now fills a vector in one pass.
This is the only performance claim the stage makes, and it is a fix to a defect
rather than an optimization: no transform was added, nothing was skipped, and
the dump is byte-identical before and after.

The scaling the fix leaves is linear in the declarations, measured from outside
on the same corpus generator:

| groups | source bytes | latency (s) | peak RSS (MB) |
| --- | --- | --- | --- |
| 500 | 156 441 | 0.067 | 10.2 |
| 1 000 | 313 451 | 0.132 | 15.3 |
| 2 000 | 637 451 | 0.263 | 27.0 |
| 4 000 | 1 285 451 | 0.530 | 48.9 |

Doubling the input doubles both, which is what "semantic work tracks actual
declarations" means.  No optimization bodies, caches or telemetry surface were
added, so there is no compiler-work budget to justify and none is claimed.

## Validation

- `make test-report-through-pa6` - 498 / 498 (pa1-pa4 205 / 205, pa5 188 / 188,
  pa6 105 / 105).
- `perl scripts/cppgm_file_audit.pl --stage pa6 --paths dev/src` - pass (72
  files checked).
- `student.tests/types_benchmark.pl` - dumps byte-identical to the reference;
  latency and peak RSS reported above.
- `student.tests/types_differential.pl` - 68 curated reduced reproducers of the
  scope, lookup, declarator, layout, overload, statement-scope and rejection
  corners agree with the reference in exit status and dump.  The audit took the
  list from 34 to 68; the 34 it started from are the corners the fixtures reach
  only in passing, and the 34 added are the audit's own findings.
- `student.tests/types_sweep.pl` - 760 generated crossings of the declaration
  shapes this stage owns agree with the reference in exit status and dump.
- `dev/src/semantic/*.cpp` compile clean under `-Wall -Wextra`.

## Handoff ledger

### Unfinished implementation

None.  Every fixture passes, the differential and sweep harnesses agree with
the reference, and the required checks are green.

### Independent review questions

These are recorded, not waived.  None of them is a failing fixture; each is a
reading the stage chose where the handout does not pin the behaviour and the
checked-in oracle does not reach.

1. **A call whose callee names a class opens a scope.**  The reference prints a
   `scope function <name>` inside a class's scope when a body calls the class,
   as in `struct C { int m; }; int main() { C(); }`.  Nothing in the handout
   describes this, and it looks like an artifact of the reference's expression
   walker rather than a semantic rule, but `300-complete-class-member-type`
   asserts it: the fixture's `scope function holder` comes from `holder()` in
   `main`'s body.  The stage reproduces it narrowly - a `call-expression` whose
   callee resolves to a class or enumeration, once per class - because the
   fixture is the oracle.  A reviewer should decide whether a later stage
   should keep it.
2. **Expression analysis in statements is limited to this one artifact.**  The
   handout puts "semantic analysis of statements beyond creating nested block
   scopes" out of scope, so declarations inside a block are analysed and other
   expressions are not - except for the class call above.  A statement's
   substatement scopes are now built the way the reference builds them (a
   selection or iteration statement owns a scope, so does its substatement, and
   a handler owns one), which is what makes a `for`-init declaration belong to
   its loop.
3. **A class-type `sizeof` uses a simple layout model.**  `int a[sizeof(C)]`
   with `C` complete is a type-forming case the handout's required features
   reach, so `Model::ClassLayout` lays the recorded non-static data members out
   in declaration order at their own alignment (9.2), gives a union the size of
   its largest member (9.5/1), counts an anonymous union once (9.5/1) and gives
   an empty class size one (5.3.3/2).  It matches the reference on every shape
   the sweep and the audit cross, including nested classes, arrays of members
   and reference members, but it is not an ABI model: bases, virtual functions
   and bit-fields are outside what PA6 has to size.  A later stage owns the
   real layout.
4. **The reference rejects some inputs PA6 does not model.**  The sweep found
   `struct C; C c;` rejected by the reference as "class has no usable default
   constructor".  The stage now rejects the incomplete-type case (3.9/5) but
   does not model default-constructibility, which is initialization semantics
   and belongs to a later stage.

### Known differences from the reference

Each of these is a reduced reproducer the audit ran, where the reference and
this compiler still disagree.  Each is either outside what `pa6.gram` or the
handout's boundary admits, or an artifact of the reference's own
representation; none is reached by a checked-in fixture, and none is repaired
by fitting to it.  The differential harness holds the ones that are language
readings; the rest are listed here.

- **Function templates.**  `template<typename T> void f(T t);` makes the
  reference print `type-alias T typename __retained_template_parameter_shape_0`
  twice, with no binding for `f` and no scope for its body; a class template is
  printed the way this compiler prints both.  The synthetic name is the
  reference's own placeholder for a shape it does not bind, and reproducing it
  would mean emitting an internal placeholder rather than a language fact.
  This compiler binds the template parameter and the function, which is what
  "template declarations with type and template-template parameter scopes"
  asks for, and matches the reference exactly on every class-template shape the
  fixtures and the harnesses cross.
- **Pointer-to-member.**  `void (S::*p)() const;` is not a `ptr-operator` in
  `pa6.gram` (309-312 admit only `*`, `&` and `&&`), so the reference's
  `member-pointer of struct S to ...` spelling is outside the assignment's
  syntax.  This compiler accepts the form and spells it as a pointer.
- **An unnamed specifier in a class body.**  `struct S { struct { int a; } x; };`
  keeps the synthetic type name here and in the reference; `struct S { struct
  { int a; }; };` is a class member with no declarator, which 9.2 makes
  ill-formed, and the reference injects its members for `struct` and `union`
  but not for `class`.  The handout's boundary names namespace-scope anonymous
  specifiers only.
- **A conversion function.**  `struct S { operator int(); };` prints
  `function operatorint function of () returning int` in the reference and
  `returning void` here.  The parser keeps only the composed name `operatorint`
  and drops the conversion type-id, so the reference recovers `int` by reading
  the name back - a spelling-based recovery the specification's design notes
  tell this stage not to copy.
- **An out-of-class special member definition.**  `struct S { S(); };
  S::S() { }` makes the reference add two bindings and two function scopes per
  definition; this compiler adds the one each definition actually declares, in
  the class's own scope (9.3.2/2) rather than at global scope as it did before
  the audit.
- **An unnamed parameter of elaborated enum type.**  `enum E { a };
  void f(enum E);` is rejected by the reference and accepted here.  A named
  parameter of the same type (`void f(enum E e);`) is accepted by both, so the
  rejection follows the reference's unnamed-parameter path rather than a rule
  about the type.
- **The width of an unsigned constant.**  `int a[~0u > 0 ? 1 : 2];` is 1 for the
  reference and 2 here: this evaluator carries a value's signedness but not its
  width, so `~0u` is -1 rather than 4294967295.  The handout puts "full
  constant-expression semantics" out of scope and the fixtures reach only
  literal forms (`4294967295u` as a bound is `200-array-bound-literal-forms`),
  which agree.  The promotions and usual arithmetic conversions of 4.5 and 5.9
  are what PA7's expression typing needs and are left to it.
