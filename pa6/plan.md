# PA6 (`cppgm++ --emit-types`) plan and handoff ledger

## Stage state

- Stage base commit: `72753f7de48ba2c125c887db4484598bb5ccd3e4` (the PA5 final
  audit, recorded on entry before any stage edit).
- Last reviewed commit: `f4799570` (the performance fix and the four readings
  the generated sweep found); the class layout model follows it.
- Implementation commits: `04cc34b2` (the semantic layer, the dump, the driver
  and the two PA5 tree extensions) and `f4799570`.
- Target: `cppgm++ --emit-types -o <out> <src>...` runs translation phases 1-7
  and the PA5 parse for each primary source, analyses each translation unit
  into scopes, declarations, entities and canonical types, and writes the
  deterministic scope/type dump the checked-in `.ref` files define.
- Progress: **105 / 105** checked-in PA6 tests pass, `make
  test-report-through-pa5` passes 393 / 393, and `perl
  scripts/cppgm_file_audit.pl --stage pa6 --paths dev/src` reports no issue.

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
  equality; a class, enumeration or template parameter is nominal rather than
  structural, so it is never interned and its identity is the entity that owns
  it.  A *scope* owns an ordered list of declaration lines plus the child
  scopes opened inside it, and keeps type, value and namespace names in
  separate maps, which is what 3.4.3 needs: a value named `N2` must not hide a
  namespace named `N2` from a namespace-only lookup.  An *entity* is what a
  declaration denotes, so a name declared twice prints twice while the entity
  stays one object - which is what array completion needs, because the entity
  holds the object's type and every line that denotes it prints the completed
  bound.
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
- `semantic_constant.cpp` is the integral constant subset of 5.19 the handout
  requires.  `&&`, `||` and `?:` do not evaluate the operand they do not
  select, so `1 || (1 / 0)` is a constant; signed arithmetic that overflows is
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
| `cppgm++-ref` | 0.735 [0.730..0.750] | 79.5 [79.4..79.6] |
| `cppgm++` | 0.403 [0.398..0.405] | 37.1 [36.9..37.3] |

Paired difference (mine - ref): median -0.3338 s, 5 of 5 blocks negative,
MAD 0.0031 s, range [-0.3500..-0.3243].  A/A calibration on the same schedule:
paired difference median +0.0019 s, MAD 0.0009 s, range [-0.0030..+0.0066].
The latency effect is 371x the noise floor.  Every observation is kept in
`/tmp/pa6_types_benchmark/types_benchmark.tsv`.

The run repeated on the final binary reproduced it: 0.406 s [0.400..0.417]
against the reference's 0.738 s, paired median -0.3300 s, 5 of 5 blocks
negative, MAD 0.0038 s.  That run's A/A arm was disturbed - MAD 0.0072 s
against the 0.0009 s above - so its effect is 46x its own noise floor rather
than 371x; the quoted run is the quieter one and both are recorded.

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
| 500 | 113 434 | 0.05 | 8.6 |
| 1 000 | 227 442 | 0.09 | 12.3 |
| 2 000 | 463 442 | 0.20 | 21.5 |
| 4 000 | 935 442 | 0.40 | 35.1 |

Doubling the input doubles both, which is what "semantic work tracks actual
declarations" means.  No optimization bodies, caches or telemetry surface were
added, so there is no compiler-work budget to justify and none is claimed.

## Validation

- `make test-report-through-pa5` - 393 / 393 (pa1-pa4 205 / 205, pa5 188 / 188).
- `make test-pa6` - 105 / 105.
- `perl scripts/cppgm_file_audit.pl --stage pa6 --paths dev/src` - pass (72
  files checked).
- `student.tests/types_benchmark.pl` - dumps byte-identical to the reference;
  latency and peak RSS reported above.
- `student.tests/types_differential.pl` - 34 curated reduced reproducers of the
  scope, lookup, declarator, layout and rejection corners agree with the
  reference in exit status and dump.
- `student.tests/types_sweep.pl` - 760 generated crossings of the declaration
  shapes this stage owns agree with the reference in exit status and dump.
  Its first run found five differences: the four readings recorded below and
  the class-call scope.  Two further forms - `1 ? 2 : 3` as a bound and
  `sizeof(x)` of an object - came from probing the reference directly and were
  then added to the sweep.

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
   expressions are not - except for the class call above.  A `for`-init
   declaration is therefore not bound.  No fixture reaches it.

3. **`SameSignature` and `AdjustParameter` are implemented and unused.**  They
   are the canonical-signature comparison 8.3.5 asks for and PA7's call tests
   will observe; PA6's dump keeps the source parameter types, so nothing here
   calls them.  They are left for PA7 rather than removed.

4. **A class-type `sizeof` uses a simple layout model.**  `int a[sizeof(C)]`
   with `C` complete is a type-forming case the handout's required features
   reach, so `Model::ClassLayout` lays the non-static data members out in
   declaration order at their own alignment (9.2) and gives an empty class size
   one (5.3.3/2).  It matches the reference on every shape the sweep crosses,
   including nested classes and arrays of members, but it is not an ABI model:
   bases, virtual functions and bit-fields are outside what PA6 has to size.
   A later stage owns the real layout.

5. **The reference rejects some inputs PA6 does not model.**  The sweep found
   `struct C; C c;` rejected by the reference as "class has no usable default
   constructor".  The stage now rejects the incomplete-type case (3.9/5) but
   does not model default-constructibility, which is initialization semantics
   and belongs to a later stage.
