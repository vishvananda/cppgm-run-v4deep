// The semantic graph PA6 builds on the PA5 tree: canonical types, scopes,
// entities and the declarations that bind names in those scopes.
//
// The three ideas stay apart, as `scopes-and-types.md` asks.  A *type* is a
// node of an interned graph, so `const int` is one object however often it is
// written.  A *scope* is where a name is visible, and owns an ordered list of
// the declarations made in it plus the child scopes opened inside it.  An
// *entity* is what a declaration denotes: a class, an enumeration, a
// namespace, a function, or an object.  A declaration line of the dump is a
// binding record, so a name declared twice prints twice while the entity it
// denotes stays one object - which is what array completion and compatible
// redeclaration need.
//
// Types are indices rather than pointers: the graph grows while it is built,
// and a slot that names a type must survive the growth.  An object's type is
// held in the entity, so completing an incomplete array updates every
// declaration that denotes it.

#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace semantic
{

// A source PA6 is required to reject: an ill-formed declaration, a lookup that
// finds nothing or finds two things, or a constant expression outside the
// supported subset.
class SemanticError : public std::runtime_error
{
public:
	explicit SemanticError(const std::string& what)
		: std::runtime_error(what)
	{}
};

enum ETypeKind
{
	kTypeFundamental,
	kTypeClass,
	kTypeEnum,
	kTypeTemplateParameter,
	kTypeCv,
	kTypePointer,
	kTypeLvalueReference,
	kTypeRvalueReference,
	kTypeArray,
	kTypeFunction,
	kTypeMemberPointer
};

// 9 [class]: the three class keys, which the dump prints with a class type.
enum EClassKey
{
	kClassKeyClass = 0,
	kClassKeyStruct,
	kClassKeyUnion
};

// 7.2 [dcl.enum]: the three enum keys.  A scoped enumeration prints `enum
// class` or `enum struct`; an unscoped one prints `enum`.
enum EEnumKey
{
	kEnumKeyPlain = 0,
	kEnumKeyClass,
	kEnumKeyStruct
};

// The unknown array bound of 8.3.4, printed as `0` by the dump.
const long long kUnknownBound = -1;

struct Type
{
	ETypeKind kind;

	// cv-qualified, pointer, reference, array and function all name an operand
	// here: the qualified type, the pointee, the referred-to type, the element
	// type and the return type respectively.
	int base;

	// Bit 0 is `const`, bit 1 is `volatile`.
	int quals;

	// Array bound, or kUnknownBound.
	long long bound;

	// A function's parameter types, in source order, plus whether the list ends
	// in `...`.  A member function's cv-qualifiers live in `quals` and its
	// ref-qualifier in `func_ref`, because 8.3.5/6 makes both part of the
	// function type.
	std::vector<int> params;
	bool varargs;
	int func_ref;

	// A class, enumeration or template parameter's name, and its key.
	std::string name;
	int class_key;
	int enum_key;

	// A class or enumeration is complete once its definition has been read.
	bool complete;

	// An enumeration's fixed underlying type (7.2/2), or -1 when it has none.
	int underlying;

	// The scope a class or enumeration owns, so a qualified name reaches the
	// members of an alias for one.
	int decl_scope;

	// A pointer to member's member type (8.3.3).  `base` names the class.
	int member;

	Type()
		: kind(kTypeFundamental)
		, base(-1)
		, quals(0)
		, bound(kUnknownBound)
		, varargs(false)
		, func_ref(0)
		, class_key(kClassKeyClass)
		, enum_key(kEnumKeyPlain)
		, complete(false)
		, underlying(-1)
		, decl_scope(-1)
		, member(-1)
	{}
};

enum EScopeKind
{
	kScopeNamespace,
	kScopeTemplateParameters,
	kScopeClass,
	kScopeEnum,
	kScopeFunction,
	kScopeBlock
};

enum EBindingKind
{
	kBindingType,
	kBindingTypeAlias,
	kBindingEnumerator,
	kBindingFunction,
	kBindingVariable,
	kBindingParameter
};

enum EEntityKind
{
	kEntityClass,
	kEntityEnum,
	kEntityNamespace,
	kEntityFunction,
	kEntityObject,
	kEntityEnumerator,
	kEntityAlias,
	kEntityTemplateParameter
};

// One line of the scope dump: what a single declaration bound, in the shape
// the handout's output grammar gives it.
struct Binding
{
	EBindingKind kind;
	std::string name;

	// The type this line prints.  A variable and a function print their
	// entity's type, so completing an array updates every line that denotes it;
	// an alias, an enumerator and a parameter print the type their own
	// declaration gave them.
	int type;

	// The entity the declaration denotes, or -1 for a declaration that denotes
	// none (an alias, an enumerator, a parameter).
	int entity;

	// The source class or enum key a `type` line prints with, or -1 when the
	// line prints its type's own spelling.
	int class_key;
	int enum_key;

	// An enumerator's value.
	long long value;

	Binding()
		: kind(kBindingVariable)
		, type(-1)
		, entity(-1)
		, class_key(-1)
		, enum_key(-1)
		, value(0)
	{}
};

struct Entity
{
	EEntityKind kind;
	std::string name;

	// The entity's type.  For a class or enumeration this is the nominal type
	// object the entity owns; for an object it is the declared type, which a
	// later compatible declaration may complete.
	int type;

	// The scope a class, enumeration or namespace owns.  For an enumeration the
	// scopes map holds one entry per scope the enumeration was declared or
	// defined in, which is what a qualified definition outside its class needs.
	int scope;
	std::map<int, int> scopes;

	// An enumerator's or constant object's value, when it has one.
	bool has_value;
	long long value;
	bool value_is_signed;

	// Whether a function has a definition anywhere in the unit, and the
	// deferred body a class member function waits for.
	int body;

	// The scope the entity was first declared in, which is what its qualified
	// name is built from.
	int decl_scope;

	Entity()
		: kind(kEntityObject)
		, type(-1)
		, scope(-1)
		, has_value(false)
		, value(0)
		, value_is_signed(true)
		, body(-1)
		, decl_scope(-1)
	{}
};

struct Scope
{
	EScopeKind kind;
	std::string name;
	int parent;

	// The dump: declarations in source order, then child scopes in the order
	// they were opened.
	std::vector<Binding> bindings;
	std::vector<int> children;

	// A class's own non-static data members, in declaration order, as the
	// entity each declared.  This is the layout order 9.2 asks for, kept apart
	// from `bindings` because the two are not the same list: a static member is
	// a declaration line but not part of the object (9.4.2/1), and an anonymous
	// union is one member (9.5/1) whose own members are also names in this
	// scope (9.5/3).
	std::vector<int> members;

	// Lookup: the last entity each spelling denotes in this scope, kept apart
	// by category so a namespace-only context is not hidden by a value name
	// (N3485 3.4.3).
	std::map<std::string, int> types;
	std::map<std::string, int> values;
	std::map<std::string, int> namespaces;

	// Namespaces nominated by using-directives, and the inline child
	// namespaces a qualified or unqualified lookup also searches.
	std::vector<int> directives;
	std::vector<int> inline_namespaces;

	// The names this scope bound with a namespace-alias-definition.  An alias
	// is another name for a namespace rather than a namespace that can be
	// extended, so a namespace-definition may not reopen one (7.3.2/3).
	std::map<std::string, bool> namespace_aliases;

	// A namespace scope that is an unnamed namespace, and one whose names are
	// visible in its enclosing scope without qualification.
	bool unnamed;
	bool inline_namespace;

	// The entity a class or enumeration scope belongs to.
	int entity;

	Scope()
		: kind(kScopeNamespace)
		, parent(-1)
		, unnamed(false)
		, inline_namespace(false)
		, entity(-1)
	{}
};

// A deferred member-function body: the class scope registers the function
// scope after the whole member list, because a member body is a complete-class
// context (N3485 3.3.7/1).
struct DeferredBody
{
	int scope;        // the scope to open the function scope in
	int declarator;   // the declarator node whose suffixes build the type
	int specifiers;   // the decl-specifier-seq node, or kNoSyntaxNode
	int body;         // the compound-statement node
	int owner;        // the class scope, for the class's own scope chain
	std::string name;
};

class Model
{
public:
	Model();

	// --- types ----------------------------------------------------------
	int Fundamental(int index) const;
	int Qualified(int quals, int base) const;
	int Pointer(int base) const;
	int LvalueReference(int base) const;
	int RvalueReference(int base) const;
	int Array(long long bound, int element) const;
	int Function(int result, const std::vector<int>& params, bool varargs,
	             int quals = 0, int func_ref = 0) const;
	int NewClass(const std::string& name, int key);
	int NewEnum(const std::string& name, int key);
	int NewTemplateParameter(const std::string& name, bool template_parameter);
	int MemberPointer(int class_type, int member_type) const;

	const Type& Get(int id) const
	{
		return types_[static_cast<std::size_t>(id)];
	}

	Type& Get(int id)
	{
		return types_[static_cast<std::size_t>(id)];
	}

	// The recursive spelling of `scopes-and-types.md`.
	std::string Spelling(int id) const;

	// Whether two types are the same type, and the signature comparison that
	// applies parameter adjustment (8.3.5).
	bool Same(int left, int right) const;

	// 1.3.20: a function's signature is its adjusted parameter-type-list, its
	// `...`, and - for a member function - its cv-qualifiers and ref-qualifier.
	// The return type is not part of it, so `int f() const` and `int f()`
	// overload each other while `int f(int)` and `long f(int)` do not.
	bool SameSignature(int left, int right);

	// 13.1/3: two declarations refer to the same function when their signatures
	// and their return types agree.  A different parameter list is an overload,
	// which PA6 accepts; one signature with two return types is ill formed.
	bool SameFunctionType(int left, int right);

	// 13.1/2: member functions with one parameter-type-list cannot be
	// overloaded when some of them have a ref-qualifier and the rest do not.
	bool MixedRefQualifier(int left, int right);

	// 8.3.5/5: the type a parameter takes in a signature, which is what makes
	// `void f(int)` and `void f(int[3])` one function.
	int AdjustParameter(int id) const;

	// The adjusted form of a whole function type, which is what a signature
	// comparison and a target-directed member lookup compare.
	int AdjustFunction(int id) const;

	// The size and alignment of a type in the course ABI, for `sizeof` and
	// `alignof`.  An incomplete class or an unknown array bound has no size.
	bool SizeOf(int id, unsigned long long& size) const;
	bool AlignOf(int id, unsigned long long& align) const;

private:
	bool ClassLayout(int id, unsigned long long& size, unsigned long long& align,
	                 int depth) const;
	// The size and alignment an object of this type occupies as a class member.
	bool ObjectLayout(int id, unsigned long long& size, unsigned long long& align,
	                  int depth) const;

public:

	// --- scopes and entities --------------------------------------------
	int NewScope(EScopeKind kind, const std::string& name, int parent);
	Scope& ScopeOf(int id)
	{
		return scopes_[static_cast<std::size_t>(id)];
	}

	const Scope& ScopeOf(int id) const
	{
		return scopes_[static_cast<std::size_t>(id)];
	}

	int NewEntity(EEntityKind kind, const std::string& name);
	Entity& EntityOf(int id)
	{
		return entities_[static_cast<std::size_t>(id)];
	}

	const Entity& EntityOf(int id) const
	{
		return entities_[static_cast<std::size_t>(id)];
	}

	int GlobalScope() const
	{
		return global_;
	}

	// Appends a declaration line to `scope`, and records what the name now
	// denotes for lookup.
	void AddBinding(int scope, const Binding& binding);

	// Registers a class, enumeration or namespace entity under its name in
	// `scope`, so a later declaration finds the same entity.
	void BindType(int scope, const std::string& name, int entity);
	void BindValue(int scope, const std::string& name, int entity);
	void BindNamespace(int scope, const std::string& name, int scope_id);

	// The scope a class or enumeration uses in `owner`, creating and
	// registering one when the entity has none there yet.
	int ScopeFor(int owner, int entity, EScopeKind kind, const std::string& name);

	// --- lookup -----------------------------------------------------------
	// The entity a spelling denotes, or -1.  `kind` selects the category, so a
	// namespace-only lookup is not answered by a value of the same spelling.
	int LookupType(int scope, const std::string& name) const;
	int LookupValue(int scope, const std::string& name) const;
	int LookupNamespace(int scope, const std::string& name) const;

	// The same lookups starting at `scope` and following using-directives and
	// inline namespaces.  A name found through two nominated namespaces that
	// denote different entities is ambiguous and rejected (3.4.1).
	int LookupTypeUnqualified(int scope, const std::string& name) const;
	int LookupValueUnqualified(int scope, const std::string& name) const;
	int LookupNamespaceUnqualified(int scope, const std::string& name) const;

	// The first component of a nested-name-specifier.  3.4.3.1/1 with 7.3.4/3:
	// the names a using-directive nominates are considered only where the
	// enclosing scopes declare nothing of that spelling, so the nearest
	// enclosing declaration wins.
	int LookupTypeQualifier(int scope, const std::string& name) const;
	int LookupNamespaceQualifier(int scope, const std::string& name) const;

	// Qualified lookup: `scope` is the scope named by the qualifier.
	int LookupTypeIn(int scope, const std::string& name) const;
	int LookupValueIn(int scope, const std::string& name) const;
	int LookupNamespaceIn(int scope, const std::string& name) const;

	// Resolves a name written with a nested-name-specifier to the scope it
	// names, starting from `scope`.
	int ResolveQualifier(int scope, const std::string& qualifier) const;

	// The nearest enclosing namespace scope of `scope`, which is what the
	// qualified-definition rule of 7.3.1.2/2 compares.
	int EnclosingNamespace(int scope) const;
	bool NamespaceEncloses(int outer, int inner) const;

	// What one declaration bound at its declarator node: the type it gave the
	// name, the entity it denotes and the scope it landed in.  A later pass
	// reads this rather than re-analysing the declarator, which would declare
	// the same names twice.
	struct DeclarationFact
	{
		int type;
		int entity;
		int scope;

		DeclarationFact()
			: type(-1)
			, entity(-1)
			, scope(-1)
		{}
	};

	void NoteDeclaration(int node, int type, int entity, int scope)
	{
		if(node < 0)
		{
			return;
		}
		DeclarationFact fact;
		fact.type = type;
		fact.entity = entity;
		fact.scope = scope;
		declarations_[node] = fact;
	}

	const DeclarationFact* DeclarationAt(int node) const
	{
		std::map<int, DeclarationFact>::const_iterator found = declarations_.find(node);
		return found == declarations_.end() ? 0 : &found->second;
	}

	// The scope in effect at a syntax node, recorded by the analysis that opened
	// it.  A later pass reads it rather than re-deriving the scope structure.
	void NoteScope(int node, int scope)
	{
		node_scopes_[node] = scope;
	}

	int ScopeAt(int node) const
	{
		std::map<int, int>::const_iterator found = node_scopes_.find(node);
		return found == node_scopes_.end() ? -1 : found->second;
	}

	// The scope a class, enumeration or namespace entity owns, or -1.
	int EntityScope(int entity) const
	{
		return entity < 0 ? -1 : entities_[static_cast<std::size_t>(entity)].scope;
	}

	// The scope an entity's declaration landed in, which is what its qualified
	// name is built from.  For a class, enumeration or namespace this is the
	// scope the entity owns.
	int DeclaringScope(int entity) const
	{
		return entity < 0 ? -1 : entities_[static_cast<std::size_t>(entity)].decl_scope;
	}

	// 10 [class.derived]: the direct base classes of a class type, in the order
	// the base-clause wrote them.  A derived-to-base conversion and a qualified
	// member access both need them.
	void AddBase(int class_type, int base_type)
	{
		bases_[class_type].push_back(base_type);
	}

	const std::vector<int>* BasesOf(int class_type) const
	{
		std::map<int, std::vector<int> >::const_iterator found = bases_.find(class_type);
		return found == bases_.end() ? 0 : &found->second;
	}

	// Whether `derived` is `base` or has it as a base, directly or indirectly.
	bool DerivesFrom(int derived, int base) const;

private:
	int InternType(const std::string& key, const Type& type) const;
	int AddType(const Type& type) const;
	void CollectNominations(int scope, std::vector<int>& out) const;
	int LookupInCategory(int scope, const std::string& name, int category) const;
	int LookupThrough(int scope, const std::string& name, int category,
	                  std::vector<int>& visited) const;
	int LookupThroughDirect(int scope, const std::string& name, int category,
	                        std::vector<int>& visited) const;

	mutable std::vector<Type> types_;
	mutable std::map<std::string, int> type_ids_;
	std::vector<Scope> scopes_;
	std::vector<Entity> entities_;
	std::map<int, int> node_scopes_;
	std::map<int, DeclarationFact> declarations_;
	std::map<int, std::vector<int> > bases_;
	int global_;
};

// The fundamental type names, indexed the way `Fundamental` takes them.
extern const char* const kFundamentalNames[];

}  // namespace semantic
}  // namespace cppgm
