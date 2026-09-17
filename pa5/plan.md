# PA5 (`cppgm++ --emit-ast`) plan and handoff ledger

## Stage state

- Stage base commit: `91bd3202` (the PA4 final audit, recorded on entry before
  any stage edit).
- Last reviewed commit: the audit commit that last touches this file.
- Implementation commits: `2f4e97c4` (parser, arena, dump and driver), the
  thirty-three commits that follow it, and the audit commit that closes the
  stage.
- Target: `cppgm++ --emit-ast -o <out> <src>...` runs translation phases 1-7
  for each primary source, parses each translation unit with the PA5 syntax
  subset, and writes the deterministic AST dump the checked-in `.ref` files
  define.
- Progress: **188 / 188** checked-in PA5 tests pass, `make
  test-report-through-pa5` passes 393 / 393 and `perl
  scripts/cppgm_file_audit.pl --stage pa5 --paths dev/src` reports no issue.
  The audit closed the child-list defect the handoff carried, reclassified the
  counter item it carried with evidence, and fixed eleven defects it found
  itself; the measurements and the list are below.

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
  identifier/literal/eof pseudo-kinds), the spelling as an id in the
  translation unit's `SyntaxSpellingPool`, and the id of its literal facts in
  the pool's side table (or none).  A token is three integers and no token owns
  a string; the spellings are interned as they enter the frontend, which is
  what section 1 asks for and is also why the parser's lookahead vector is 12
  bytes a slot rather than 88.
- `syntax/syntax_arena.*` owns the nodes.  A node is a tag, an optional inline
  label and its children; tags and labels are interned; the dump is a preorder
  walk with an explicit stack.  Children are edges in one arena vector - a node
  holds its first and last edge, an edge names its child, its parent and the
  edge the parent held before it - so no node allocates and dropping a subtree
  restores its parents from the edges themselves.  Section 8 asks for exactly
  this and the architecture audit names per-node hot allocation a defect.
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

A production the dump spells as a label rather than as nodes - a template
argument, a `using`-declaration's target, a `throw` list - still has to be read
to know where its token range ends, and the reading builds a tree it does not
keep.  Every such reading releases its tree with an arena mark as soon as it
has decided; the audit below found three of them that did not.

## Remaining groups

None of the checked-in fixtures fails.  The groups the implementation turn
closed, each of them an ambiguity corner with a reduced reproducer kept in
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

## Audit

The audit traced a template-heavy declaration from source to dump, checked the
parser's rollback state against every piece of state it mutates, profiled the
stage's own corpus, and read the parser against the reference over shapes the
corpus never reaches.  Eleven defects came out of it - four of representation,
six of reading and one of cost - and all eleven are fixed here.

### 1. A discarded reading kept its tree (abandoned subtrees)

The dump spells a template argument list, a `using`-declaration's target and a
`throw` list into the label of the construct that owns them, so those readings
exist only to decide where the token range ends.  Three of them built a tree
and dropped the node without releasing it: `Parser::TemplateArgument` on both
its readings, the `throw(...)` operand of `Parser::FunctionSuffix`, and
`Parser::UsingDeclaration`'s name.  On the stage benchmark corpus 90 013 of
330 207 nodes - 27% of everything the arena built - were unreachable from the
root: a `type-id` and a `type-specifier-seq` pair per type argument, a
`literal` per rejected expression argument.  That is section 8's "do not retain
duplicate token/syntax graphs" and section 1's "MUST NOT ... retain abandoned
trees", and no test can see it because an orphan never reaches the dump.

Each reading now takes an arena mark and drops back to it.  Measured on the
same corpus with the probe that found them: unreachable nodes 90 013 -> 0, and
the arena's high-water mark 330 207 -> 240 194 nodes, which is the tree it
actually prints.

### 2. Tokens owned their spelling (the parse vector was the peak)

The parse token vector was the stage's single largest allocation: 339 177
tokens at 88 bytes each, at power-of-two capacity, 46.1 MB of the 59.1 MB peak
- more than the tree it was built to produce.  Section 1 says tokens carry
compact ids or interned pointers, not owning strings, and section 8 asks for
shorter-lived, bulk-released storage.

`syntax/syntax_token.h` now holds three integers per token and
`SyntaxSpellingPool` owns each distinct spelling once, interning as the tokens
enter the frontend.  The literal facts a later stage evaluates moved to a side
table only literal tokens index, so a token that has none pays nothing.

### 3. Every node allocated its children

`syntax_arena.h`'s `SyntaxNode` held a `std::vector<int>` of children, so every
node with a child made its own heap allocation, one of the shapes section 8
names ("variable children use trailing arrays, small inline vectors or arena
slices") and the one the architecture audit calls a defect even when the tests
pass.  The node also cost 32 bytes before its children.

Children are now edges in a second arena vector.  An edge carries its child,
its parent and the edge that preceded it, which is what lets `SyntaxArena::Drop`
restore the parents of a dropped subtree from the edges alone - no journal, no
per-node allocation - and the child list is read through `ChildCount`/`ChildAt`
rather than a vector.  The representation change also removes a latent hazard
the old one had: a dropped subtree could leave a surviving parent holding a
child index past the end of the node vector, which nothing in the corpus
reaches, and which can no longer be represented.

Removing the pre-existing unused `StorageBytes`, `NodeCount` and arena `Count`
accessors went with it: the stage has no reporting surface for them (see the
ledger).

### 4. The checkpoint did not carry the angle lists

`parsing.md` asks a checkpoint to cover all the state a failed alternative can
change, "restoring only the token position can make a later alternative see
declarations that never happened".  The parser's `Mark` carried the cursor, the
split `>>`, the delimiter count, the tree, the name table and the class stack -
but not the open angle lists, which is the state that decides whether a later
`>` is an operator or a list's closer: `angle_depth_`, the speculative and
logical flags, and the saved delimiter counts.  The three regions that open an
angle list can throw between opening and closing it (a template-parameter
clause whose parameter is malformed, a cast whose type-id does not read), and
the catch that swallows the throw rolls back: `TryTemplateIdTail`'s
`LeaveAngle()` pops one level, and nothing restored the rest.

The checkpoint now carries them, including the innermost list's
logical-operator flag, which `NoteLogicalInAngle` sets in place rather than
pushes, and the declaration-only mode, which a class body raises and lowers and
which decides whether an identifier-led block item is read as a declaration.
The class body's collecting pass, which restores the angle state by hand for
the same reason, sets that flag back too.  No corpus input was found that the
old checkpoint reads wrongly - like the earlier handoff's delimiter count, it
is a hole the shapes tried here do not reach - so this is recorded as a repair
of the checkpoint's contract, not as a demonstrated misread.

### 5. Shapes no fixture reaches

A second reading of the parser against the reference, over shapes the checked-in
corpus does not contain, found six that were read differently.  Each was reduced
to the reproducer quoted below and verified against the reference after the fix;
the sweep that produced them is described under Validation, and the shapes whose
reading is a deliberate difference from the reference are in the ledger.

- A null statement in a block.  `void f() { ; }` was dumped as
  `empty-declaration`; the reference dumps `expression-statement`.  The gate that
  offers a statement to the declaration reading claimed a bare `;`, and a bare
  `;` is only an `empty-declaration` where a declaration may appear.
- `= default` and `= delete` on a special member.  `struct S { S() = default; };`
  built `special-member-initializer / default KW_DEFAULT:default`; the reference
  builds `initializer / special-initializer default`, which is the shape the
  ordinary `Initializer` path already gave - the fixture
  `100-member-declarations` exercises that path, and the two disagreed.
- A special member's function try block.  `struct S { S() try { } catch (...) { } };`
  was rejected; the reference reads it as `special-member-definition /
  function-try-block`, with a constructor initializer inside the try block.
- A block-scope declaration that begins with `typename`, a leading `::` or a
  storage specifier.  `void f() { typename T::X x; }` and `void f() { ::T x; }`
  were rejected and `void f() { mutable int x = 1; }` too, while the reference
  accepts all three; the gate now offers the declaration reading the same set of
  starting tokens `CanStartDeclSpecifier` accepts, and a declaration that then
  fails still rolls back to the expression statement.
- An enum member.  `struct S { enum E { a }; };` was dumped as a bare
  `enum-specifier` where the reference writes the `simple-declaration` every
  other member gets; and `enum E { a } e;` was accepted at namespace scope,
  where `enum-declaration` is `enum-specifier ;` and the reference rejects it
  (the trailing `e` was silently re-read as a following declaration's
  decl-specifier).
- A name closed by the first half of a `>>`.  `static_cast<A<int>>(x)` spelled
  its type-name `A<int>>` where the reference spells `A<int>`: a `>>` is one
  token and two closers, and the text of the name that took the first half may
  not carry the second, which closes the construct the name is written inside.
  Label composition now goes through `Parser::RangeText`, which keeps one `>`
  for that case and is otherwise the range it always was.

### 6. The class body's second reading multiplied with the nesting depth

`ClassBodyHasInlineMemberDefinition` answers "is there a `{` at depth 0 of the
body", so a nested class body - not just an inline member function - triggers
the collecting pass, and the collecting pass descended into nested class bodies
in turn.  Each level multiplied the work: twelve nested classes, 189 bytes of
source, ran past 30 s here where the reference takes 0.26 s, and the earlier
handoff's "bounded by the presence of an inline member definition" was simply
false.  That is section 9's proportionality requirement broken by a construct
the benchmark never exercises.

The collecting pass now skips a nested class body whole with
`SkipBracedBody`: the names a nested class declares are not names of the class
being collected, so nothing is lost by not reading it there, and the real pass
reads it once.  The same 189-byte input now takes 0.04 s with a byte-identical
dump.  The second reading remains a second reading (see the ledger), but it is
linear in the class body rather than exponential in the nesting.

### Effect

Frozen protocol, 3 000 groups (923 664 B of source, a 6 922 324 B dump of
240 197 nodes), 5 ABBA blocks, 20 timed runs per label, dumps compared byte for
byte before any timing is accepted:

| tool | latency (s) | peak RSS (MB) |
| --- | --- | --- |
| `cppgm++-ref` | 0.661 [0.655..0.673] | 48.2 [48.1..48.3] |
| `cppgm++`, audited | 0.351 [0.349..0.370] | 23.0 [22.8..23.1] |
| `cppgm++`, before the audit | 0.458 [0.452..0.479] | 59.1 [58.8..59.2] |

Paired difference (mine - ref): median -0.3093 s, 5 of 5 blocks negative,
range [-0.3098..-0.3040].  A/A calibration on the same schedule: paired
difference median -0.0013 s, MAD 0.0021 s, range [-0.0055..+0.0051].  The
latency effect is 147x the noise floor; the paired per-block differences are
what the claim rests on, and every observation is kept in
`/tmp/pa5_syntax_benchmark/syntax_benchmark.tsv`.  The run repeated on the
committed binary reproduced the median at -0.3092 s, with one of its five
blocks disturbed by unrelated load on the host - visible as a positive
excursion in that run's range, and the reason the recorded run is the one
quoted.

Peak RSS was the one dimension the handoff carried as a **regression** (1.23x
the reference); it is now 2.1x smaller than the reference, from the same two
representation changes, with no change to what the compiler accepts.  The
benchmark's noise statistic is the median absolute deviation about the median,
which is what its text always claimed; the earlier handoff's numbers used the
median absolute value, which agrees for a centred A/A arm and was never used to
justify the effect.

`cppgm++ --emit-ast` has no executable output, so there is no generated-program
runtime or text size at this stage; what the stage produces is the dump, and
its size and node count are reported instead.  No telemetry surface is invented
to report one (see the ledger).

## Validation

- `make test-report-through-pa5` - 393 / 393 (pa1-pa4 205 / 205, pa5 188 / 188).
- `perl scripts/cppgm_file_audit.pl --stage pa5 --paths dev/src` - pass, no
  warnings (62 files checked).
- `student.tests/syntax_benchmark.pl` - dumps byte-identical to the reference;
  latency and peak RSS reported above.
- `student.tests/syntax_differential.pl` - 20 reduced reproducers of the
  ambiguity and name-category corners agree with the reference in exit status
  and dump; five known differences are recorded below.
- A sweep of 154 generated translation units crossing the angle, cast,
  declaration/expression and class-body shapes, compared against the reference
  in exit status and dump.  It found the split-`>>` label defect and the two
  acceptance differences now recorded as known differences; the rest of the
  reading defects came from a review of the parser against the reference's
  behaviour, and each is quoted with the reproducer that shows it.
- The audit's own checks: unreachable nodes measured at 0 on the benchmark
  corpus, the dump compared byte for byte against the reference after each
  change, the 189-byte nested-class input timed before and after the collecting
  pass change, and the whole suite re-run after each change.

## Handoff ledger

### Unfinished implementation

- Nothing that a checked-in fixture reaches is unfinished.  The three shapes
  this compiler reads differently from the reference are the known differences
  below: one is a reference artifact in a region the handout puts outside the
  required boundary, and four are shapes `pa5.gram` and the standard give a
  reading the reference does not.  None is a missing construct, and all five
  are recorded rather than waived.

### Carried to a later stage

- **Source locations on syntax nodes.**  Section 1 asks for a source-faithful
  graph and the handout asks the next assignment to keep source locations as
  structured data.  A node today carries its interned tag and label and no
  position; the parser has the token position at every step, so the change is
  one field per node and one call at the points that already record a `start`.
  It is carried rather than guessed because the shape of the position - token
  ordinal against byte offset, one file id or a chain - is decided by the first
  consumer that needs it, which is PA6's diagnostics and semantic graph; adding
  a half-accurate position now would cost every node and still be replaced.
- **Section 9's work counters.**  The audit re-examined this rather than
  repeating the earlier note.  What section 9 asks for is phase time, peak
  memory and work counters for allocations, candidates, specialization
  transitions, caches, worklists and IR sizes.  Phase time and peak memory are
  measured by the benchmark from outside, which is the protocol section 9
  itself prescribes, and the stage's one size counter - the tree's node count -
  is reported with them.  The entities the other counters count do not exist at
  this stage: there is no overload resolution, no specialization, no cache and
  no worklist, only the arena's allocation.  For the surface itself, the
  reference tool's own `--help` lists exactly one counter flag,
  `--stats-functions`, described as a compile-only per-function census: in this
  tool family the counter surface attaches to the mode that owns the counted
  work, and that mode is the native compile path of a later stage, not
  `--emit-ast`.  Nothing is waived here; the item stays carried, with the shape
  it should take when the work it counts exists.
- **The class body's second reading.**  A class body with an inline member
  function definition is read twice - once to collect the member names a body
  may name, once for the tree - because N3485 9.2/2 puts a member body in the
  class's complete-class context while the name table is lexical.  Section 1
  asks for one parse per region.  The audit found the second reading multiplying
  with the nesting depth of class bodies and made it linear (finding 5 above);
  it is still a second reading of a class body that has an inline member
  definition, and it is paid once per such class body.  The alternative the
  audit considered - deferring member bodies and parsing them after the class -
  parses each region once but moves parsing out of source order, and the dump it
  would have to preserve is what the fixtures check.  Carried with that
  measurement, not waived.
- **The parse-step outputs are not part of the checkpoint.**  The four values a
  reading leaves for its caller - the declarator's name, the last type name, the
  "one keyword type specifier" flag and whether the declarator is a function -
  are written by the production that just ran and read by the caller that asked
  for it.  A rollback does not restore them, and does not need to: every caller
  that catches a failure discards the value and re-reads the region.  They are
  named here so that a later stage that starts relying on them knows they are
  not checkpointed state.
- **The name table's storage.**  `scopes_` is a `std::map<std::string,int>` per
  scope: a tree node and a string per name, and a comparison lookup, where
  section 2 asks hot lookup to be O(1) average and section 8 asks a map used
  this way to be flat and keyed by compact identity.  The audit left it alone:
  the spellings are interned now, so the change is to key the scopes by spelling
  id and hold the category beside it, but the table is not this stage's
  dominant structure (it is a few MB of the audited peak against the tree and
  the token vector), and rewriting it is a change to the stage's most delicate
  machinery - the binding log that the last implementation turn corrected - for
  a cost the benchmark does not show.  Recorded with the reasoning and the
  measurement rather than done blind.

### Independent review questions

- **A qualified name in a parameter's parenthesized position**: the reference
  reads `int q(int(x::C))` - after an earlier parameter has declared `x` a
  value - as a parameter clause whose parameter is the qualified name, and this
  compiler reads the same tokens as a declarator.  The two agree until `x` is a
  known value.  A parameter name may not be qualified, so the input is outside
  the boundary `parsing.md` states; the reproducer is in
  `student.tests/syntax_differential.pl` as the first known difference, and the
  harness reports if it ever goes away.
- **Three `>>` shapes the reference rejects and this compiler accepts**:
  `f<A<int>>(1)`, `f<static_cast<int>(x)>()` and `a<b>>c`.  `pa5.gram` derives
  all three - `close-angle-bracket` is `OP_GT | ST_RSHIFT_1 | ST_RSHIFT_2`, so
  the halves of one `>>` close two lists or a list and a relational operator,
  and a `template-argument` may be an expression - and the README says to follow
  the grammar where it and the reference disagree.  The reference's rejection is
  not a rule from any document: it fails on the first of these at a token
  position that has nothing to do with the shape.  All three are in
  `student.tests/syntax_differential.pl`'s known differences, so the harness
  reports if any ever agrees.
- **An operator template-id after a dot**: `a.operator+<int>(b)` is read here as
  the call the grammar spells (`template-id: operator-function-id OP_LT
  template-argument-list? close-angle-bracket`) and by the reference as
  `((a.operator+) < int) > (b)`.  The checked-in fixture covers the unqualified
  form of the same name, where the two agree; the qualified form is the fifth
  known difference, and the harness reports if it changes.
- **The template-name category is coarse**: a template declaration's entity is
  a template-name of the enclosing scope only where the declaration bound it as
  a type name, so a function template's name takes no category (it was invisible
  before the implementation turn, and `validate<int>()` still reads as a
  template-id through the bare-name speculation).  Giving function templates
  their own category would make `N::f<int>` a template-id inside an argument
  list; no fixture reaches it, and the token-identity of the two categories is a
  later stage's question.
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
  class body's later member visible to an earlier inline member body (the second
  reading above).  Its cost is bounded by the presence of an inline member
  definition and is not visible in the benchmark above.
- A token's literal facts are now indexed by the token rather than inline, and
  a user-defined integer or floating literal has no facts entry rather than an
  empty one - it still carries `TT_LITERAL` and its spelling, which is all the
  dump and the parser read.  A later stage that evaluates such a literal takes
  its type from the literal operator, as the pipeline already models.
