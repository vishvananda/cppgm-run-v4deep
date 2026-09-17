# PA6 implementation plan

## Stage base

- Stage base commit: `72753f7de48ba2c125c887db4484598bb5ccd3e4`
- Last reviewed commit: `72753f7de48ba2c125c887db4484598bb5ccd3e4`
- Turn-start state: PA1–PA5 pass (393/393). PA6 is unimplemented: every
  `--emit-types` invocation exits `EXIT_NOT_IMPLEMENTED`, so all 105 PA6
  fixtures fail on exit status. Baseline pa6 failures: 105.

## Design / spec alignment

PA6 is the first semantic assignment: build scopes, declarations, entity
identity, lookup and canonical types directly on the PA5 AST, and print a
deterministic scope/type dump. The handout (`pa6/README.md`,
`pa6/scopes-and-types.md`) fixes the output grammar; the checked-in `.ref`
files are the oracle.

Evidence read from the fixtures and from the shipped `cppgm++-ref`
(observation only, per the reference policy):

- A scope prints its declarations first, in source order, then its child
  scopes in the order they were created.
- `scope namespace <global>` roots each translation unit; translation units
  are analysed independently and share no lexical scope.
- A `type` line prints `<key> <name-as-written>` for class/enum declarations,
  while a class or enum *used* inside a compound type prints its canonical
  (unqualified) name. `struct C; class C {};` therefore prints two `type C`
  lines with different keys and one class scope.
- An enum/class scope is registered once per owning scope: a later qualified
  definition in a different scope registers a second scope there, which is
  what `200-qualified-scoped-enum-definition-lookup` shows.
- Anonymous class-like types are named `__anonymous_union_type__<start>_<end>`
  over token indices (class-key index, one past the closing brace); anonymous
  enums are `__anonymous_enum<N>`.
- Member function bodies are complete-class contexts (N3485 3.3.7/1), so a
  class scope registers its member-function scopes after the whole member
  list; namespace-scope function scopes are registered at their declaration.
- The dump keeps *source* parameter types; canonical adjustment is only needed
  for identity, which PA7 observes.

## Remaining groups

1. **Model** — type interning, scopes, entities, declarations, printing.
2. **Declarations** — namespaces (incl. anonymous/inline/reopening), aliases,
   using declarations/directives, classes, enums, typedefs and alias
   declarations, declarator-derived type construction, array completion.
3. **Constants** — the supported integral constant subset for array bounds,
   enumerators and `static_assert`, including short-circuit evaluation and
   signed-overflow rejection.
4. **Rejections** — the declaration forms PA6 must reject (void object,
   uninitialised reference, non-enclosing qualified definition, alias
   reopened as a namespace, non-static anonymous union, scoped-enum/integer
   comparison, opaque unscoped enum, pointer to reference).
5. **Driver** — `--emit-types` wiring and multi-file translation units.

## Handoff ledger

(none yet)
