// Classes, enumerations, members, statements and the bindings they register.
//
// A class or enumeration owns the scope its members live in.  That scope is
// registered once per scope the type is declared in, which is what a qualified
// definition outside its class needs: the class keeps the scope its own member
// declaration opened, and the definition registers a second one where it is
// written.  A class scope exists only once the class has a body; an
// enumeration's exists from its first declaration, which is why an opaque
// scoped enumeration still prints an empty `scope enum`.

#include "semantic/semantic_analyzer.h"

#include "posttoken/fundamental_type.h"

using namespace std;

namespace cppgm
{
namespace semantic
{

namespace
{

string AfterColon(const string& text)
{
	const size_t colon = text.find(':');
	return colon == string::npos ? string() : text.substr(colon + 1);
}

int ClassKeyOf(const string& spelling)
{
	if(spelling == "struct")
	{
		return kClassKeyStruct;
	}
	if(spelling == "union")
	{
		return kClassKeyUnion;
	}
	return kClassKeyClass;
}

int EnumKeyOf(const string& spelling)
{
	if(spelling == "class")
	{
		return kEnumKeyClass;
	}
	if(spelling == "struct")
	{
		return kEnumKeyStruct;
	}
	return kEnumKeyPlain;
}

}  // namespace

// ---------------------------------------------------------------------------
// Bindings
// ---------------------------------------------------------------------------

void Analyzer::AddTypeBinding(int scope, const string& name, int entity, int class_key,
                              int enum_key)
{
	Binding binding;
	binding.kind = kBindingType;
	binding.name = name;
	binding.entity = entity;
	binding.type = model_.EntityOf(entity).type;
	binding.class_key = class_key;
	binding.enum_key = enum_key;
	model_.AddBinding(scope, binding);
	model_.BindType(scope, name, entity);
}

int Analyzer::EntityType(int entity) const
{
	return entity < 0 ? -1 : model_.EntityOf(entity).type;
}

int Analyzer::FindOrCreateObject(int scope, const string& name, int type)
{
	const int found = model_.LookupValue(scope, name);
	if(found >= 0 && model_.EntityOf(found).kind == kEntityObject)
	{
		// A later compatible declaration may complete an incomplete array
		// (8.3.4/1): the entity is one object, so every line that denotes it
		// prints the completed bound.
		const int old = model_.EntityOf(found).type;
		if(model_.Get(old).kind == kTypeArray && model_.Get(old).bound < 0 &&
		   model_.Get(type).kind == kTypeArray && model_.Get(type).bound >= 0)
		{
			model_.EntityOf(found).type = type;
		}
		return found;
	}
	const int entity = model_.NewEntity(kEntityObject, name);
	model_.EntityOf(entity).type = type;
	model_.EntityOf(entity).decl_scope = scope;
	model_.BindValue(scope, name, entity);
	return entity;
}

int Analyzer::FindOrCreateFunction(int scope, const string& name, int type)
{
	const int found = model_.LookupValue(scope, name);
	if(found >= 0 && model_.EntityOf(found).kind == kEntityFunction)
	{
		// 13.1/3: a later declaration of one function must agree with it.  A
		// different parameter list or a different cv- or ref-qualifier is an
		// overload, which PA6 accepts; one signature with two return types is
		// ill formed.
		const int previous = model_.EntityOf(found).type;
		if(model_.MixedRefQualifier(previous, type))
		{
			throw SemanticError("member overload set mixes ref-qualified and unqualified `" +
			                    name + "`");
		}
		if(model_.SameSignature(previous, type) && !model_.SameFunctionType(previous, type))
		{
			throw SemanticError("conflicting function return type for `" + name + "`");
		}
		return found;
	}
	const int entity = model_.NewEntity(kEntityFunction, name);
	model_.EntityOf(entity).type = type;
	model_.EntityOf(entity).decl_scope = scope;
	model_.BindValue(scope, name, entity);
	return entity;
}

// 8.3.5/6: a ref-qualifier makes a function a member function, so it cannot be
// written on a function that is not one.
void Analyzer::CheckFunctionQualifiers(int scope, int type)
{
	if(model_.Get(type).kind != kTypeFunction || model_.Get(type).func_ref == 0)
	{
		return;
	}
	if(model_.ScopeOf(scope).kind == kScopeClass)
	{
		return;
	}
	throw SemanticError("ref-qualifier requires an ordinary non-static member function");
}

// 3.2/1: a function may be defined only once in a translation unit.  An
// overload set is one entity in the model but several functions, so the
// duplicate is the same signature defined twice, not the same name.
void Analyzer::NoteFunctionDefinition(int entity, const string& name, int scope, int type)
{
	const pair<int, int> key(scope, type);
	if(defined_functions_[key].count(name) != 0)
	{
		throw SemanticError("duplicate function definition of `" + name + "`");
	}
	defined_functions_[key].insert(name);
	model_.EntityOf(entity).body = 1;
}

// ---------------------------------------------------------------------------
// Name resolution
// ---------------------------------------------------------------------------

int Analyzer::ResolveTypeName(int scope, const string& text, bool elaborated_class,
                              bool elaborated_enum)
{
	(void)elaborated_class;
	(void)elaborated_enum;
	string qualifier;
	string name;
	SplitQualifiedName(text, qualifier, name);
	int entity = -1;
	if(qualifier.empty())
	{
		entity = model_.LookupTypeUnqualified(scope, name);
	}
	else
	{
		entity = model_.LookupTypeIn(model_.ResolveQualifier(scope, qualifier), name);
	}
	if(entity < 0)
	{
		throw SemanticError("unknown type name `" + text + "`");
	}
	return model_.EntityOf(entity).type;
}

int Analyzer::ResolveValueName(int scope, const string& text)
{
	string qualifier;
	string name;
	SplitQualifiedName(text, qualifier, name);
	if(qualifier.empty())
	{
		return model_.LookupValueUnqualified(scope, name);
	}
	return model_.LookupValueIn(model_.ResolveQualifier(scope, qualifier), name);
}

int Analyzer::ResolveNamespaceName(int scope, const string& text)
{
	string qualifier;
	string name;
	SplitQualifiedName(text, qualifier, name);
	if(qualifier.empty())
	{
		return model_.LookupNamespaceUnqualified(scope, name);
	}
	const int base = model_.ResolveQualifier(scope, qualifier);
	return model_.LookupNamespaceIn(base, name);
}

int Analyzer::CurrentEntityScope(int scope, const string& qualifier)
{
	if(qualifier.empty())
	{
		return scope;
	}
	const int target = model_.ResolveQualifier(scope, qualifier);
	CheckQualifiedDefinition(scope, target);
	return target;
}

// 7.3.1.2/2: a qualified definition of a namespace member may only appear in a
// namespace that encloses the member's own namespace.
void Analyzer::CheckQualifiedDefinition(int site, int target)
{
	const int site_namespace = model_.EnclosingNamespace(site);
	const int target_namespace = model_.EnclosingNamespace(target);
	if(site_namespace < 0 || target_namespace < 0 ||
	   !model_.NamespaceEncloses(site_namespace, target_namespace))
	{
		throw SemanticError("qualified definition outside an enclosing namespace");
	}
}

// ---------------------------------------------------------------------------
// Classes
// ---------------------------------------------------------------------------

int Analyzer::DeclareClass(int scope, const string& written, int key, bool has_body)
{
	string qualifier;
	string name;
	SplitQualifiedName(written, qualifier, name);
	int lookup_scope = scope;
	if(!qualifier.empty())
	{
		lookup_scope = model_.ResolveQualifier(scope, qualifier);
	}
	int entity = model_.LookupType(lookup_scope, name);
	if(entity < 0 || model_.EntityOf(entity).kind != kEntityClass)
	{
		entity = model_.NewEntity(kEntityClass, name);
		model_.EntityOf(entity).type = model_.NewClass(name, key);
		model_.BindType(lookup_scope, name, entity);
	}
	if(has_body)
	{
		(void)model_.ScopeFor(scope, entity, kScopeClass, written);
	}
	return entity;
}

int Analyzer::AnalyzeClassSpecifier(int node, int scope, const string& declared_name,
                                    bool is_static, int* out_key)
{
	const int key_node = FindChild(node, "class-key");
	const int key = key_node >= 0 ? ClassKeyOf(AfterColon(Label(key_node))) : kClassKeyClass;
	// 9.5/1: `union { ... } ;` with no declarator is an anonymous union, whose
	// members are names in the enclosing scope (9.5/3) and which at namespace
	// scope must be static (9.5/2) or it would have no linkage at all.
	// `union { ... } u;` declares the object `u` of an unnamed union type and
	// is neither: it injects nothing and needs no `static`.
	const bool anonymous_member = Label(node).empty() && declared_name.empty();
	if(anonymous_member && key == kClassKeyUnion && !is_static &&
	   model_.ScopeOf(scope).kind == kScopeNamespace)
	{
		throw SemanticError("an anonymous union at namespace scope must be static");
	}
	// A declarator gives its type a name only where that name can be used.  A
	// class member's declarator names the object, not the type, so an unnamed
	// class-specifier in a class body keeps its synthetic name and binds
	// nothing.
	const bool member = model_.ScopeOf(scope).kind == kScopeClass;
	const bool names_the_type =
	    !Label(node).empty() || (!declared_name.empty() && !member);
	string written = Label(node);
	if(written.empty())
	{
		written = names_the_type
		    ? (semantics_mode_ ? LocalClassName() : declared_name)
		    : AnonymousClassName(node);
	}
	const int entity = DeclareClass(scope, written, key, true);
	if(model_.Get(model_.EntityOf(entity).type).complete)
	{
		throw SemanticError("redefinition of `" + written + "`");
	}
	// 9.2/2: the class name is declared at the class-head, before the body is
	// read, so a declaration the body makes is a later one.
	if(names_the_type)
	{
		AddTypeBinding(scope, written, entity, key, -1);
	}
	const int class_scope = model_.ScopeFor(scope, entity, kScopeClass, written);
	// 10/1: the base-clause names the direct bases, which a derived-to-base
	// conversion and a qualified member access both read back.
	const int class_type = model_.EntityOf(entity).type;
	const int base_clause = FindChild(node, "base-clause");
	if(base_clause >= 0)
	{
		const vector<int> specifiers = ChildrenOf(base_clause);
		for(size_t index = 0; index < specifiers.size(); ++index)
		{
			const int base_name = FindChild(specifiers[index], "base-name");
			if(base_name < 0)
			{
				continue;
			}
			model_.AddBase(class_type, ResolveTypeName(scope, Label(base_name), false, false));
		}
	}
	ProcessClassBody(node, class_scope);
	// 9.4/1: the class is complete at the closing brace, so a member body may
	// name a member declared later in it.
	model_.Get(model_.EntityOf(entity).type).complete = true;
	if(anonymous_member && key == kClassKeyUnion)
	{
		// 9.5/3: an anonymous union's members are injected into the scope that
		// contains it, so `t` names the member without a member access.
		InjectUnionMembers(class_scope, scope);
		if(member)
		{
			// The union is one member of the class that contains it (9.5/1),
			// laid out where it was written; its own members are not members of
			// that class as well.
			model_.ScopeOf(scope).members.push_back(entity);
		}
	}
	if(out_key != 0)
	{
		*out_key = key;
	}
	return model_.EntityOf(entity).type;
}

void Analyzer::InjectUnionMembers(int class_scope, int scope)
{
	const Scope& members = model_.ScopeOf(class_scope);
	const size_t count = members.bindings.size();
	for(size_t index = 0; index < count; ++index)
	{
		const Binding& member = members.bindings[index];
		if(member.kind != kBindingVariable)
		{
			continue;
		}
		model_.AddBinding(scope, member);
		model_.BindValue(scope, member.name, member.entity);
	}
}

void Analyzer::ProcessClassBody(int node, int class_scope)
{
	const vector<int> children = ChildrenOf(node);
	for(size_t index = 0; index < children.size(); ++index)
	{
		const int child = children[index];
		const string& tag = Tag(child);
		if(tag == "class-key" || tag == "base-clause" || tag == "access-specifier" ||
		   tag == "empty-declaration")
		{
			continue;
		}
		AnalyzeDeclaration(child, class_scope, class_scope);
	}
	RunPendingBodies(class_scope);
}

void Analyzer::RunPendingBodies(int class_scope)
{
	vector<PendingBody> mine;
	vector<PendingBody> rest;
	for(size_t index = 0; index < pending_.size(); ++index)
	{
		if(pending_[index].owner == class_scope)
		{
			mine.push_back(pending_[index]);
		}
		else
		{
			rest.push_back(pending_[index]);
		}
	}
	pending_.swap(rest);
	for(size_t index = 0; index < mine.size(); ++index)
	{
		OpenFunctionScope(mine[index].owner, mine[index].name, mine[index].parameters,
		                  mine[index].body);
	}
}

void Analyzer::OpenFunctionScope(int owner, const string& name,
                                 const vector<pair<string, int> >& parameters, int body)
{
	const int function_scope = model_.NewScope(kScopeFunction, name, owner);
	for(size_t index = 0; index < parameters.size(); ++index)
	{
		Binding binding;
		binding.kind = kBindingParameter;
		binding.name = parameters[index].first;
		binding.type = parameters[index].second;
		model_.AddBinding(function_scope, binding);
		// 3.3.3/1: a parameter's name is visible in the function body, so the
		// parameter takes part in ordinary lookup like any other declaration.
		model_.BindValue(function_scope, binding.name, model_.NewEntity(kEntityObject,
		                                                               binding.name));
		model_.EntityOf(model_.LookupValue(function_scope, binding.name)).type = binding.type;
	}
	AnalyzeCompoundStatement(body, function_scope);
}

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

int Analyzer::AnalyzeEnumSpecifier(int node, int scope, bool declare,
                                   const string& declared_name, int* out_key)
{
	const int key_node = FindChild(node, "enum-key");
	const int key = key_node >= 0 ? EnumKeyOf(AfterColon(Label(key_node))) : kEnumKeyPlain;
	const bool scoped = key != kEnumKeyPlain;
	const bool has_body = arena_.Literal(node) == 1;
	// The enum-base is the type-id that follows the name, and the enumerators
	// follow it.
	int base_node = -1;
	{
		const vector<int> children = ChildrenOf(node);
		for(size_t index = 0; index < children.size(); ++index)
		{
			const int child = children[index];
			if(Tag(child) == "enum-key")
			{
				const int next = index + 1 < children.size() ? children[index + 1] : -1;
				base_node = Tag(next) == "type-id" ? next : -1;
				break;
			}
			if(Tag(child) == "type-id")
			{
				base_node = child;
				break;
			}
		}
	}
	const bool has_base = base_node >= 0;

	// As for a class-specifier: an unnamed enumeration takes the name a
	// declarator outside a class body gives it, while a class member's
	// declarator names the object and leaves the enumeration unnamed.
	const bool member = model_.ScopeOf(scope).kind == kScopeClass;
	const bool names_the_type =
	    !Label(node).empty() || (!declared_name.empty() && !member);
	string written = Label(node);
	if(written.empty())
	{
		written = names_the_type ? declared_name : AnonymousEnumName();
	}
	string qualifier;
	string name;
	SplitQualifiedName(written, qualifier, name);
	int lookup_scope = scope;
	if(!qualifier.empty())
	{
		lookup_scope = model_.ResolveQualifier(scope, qualifier);
	}

	int entity = qualifier.empty() ? model_.LookupType(lookup_scope, name)
	                               : model_.LookupTypeIn(lookup_scope, name);
	if(entity >= 0 && model_.EntityOf(entity).kind != kEntityEnum)
	{
		entity = -1;
	}
	if(entity < 0 && qualifier.empty() && !declare)
	{
		// An elaborated enumeration specifier names an enumeration that must
		// already exist, and an unqualified one is found by ordinary lookup.
		entity = model_.LookupTypeUnqualified(scope, name);
		if(entity >= 0 && model_.EntityOf(entity).kind != kEntityEnum)
		{
			entity = -1;
		}
	}
	// 7.2/3: an opaque declaration of an unscoped enumeration needs its
	// underlying type, because there is no enumerator list to infer one.  A
	// redeclaration is no different from a first declaration here, and a
	// specifier that names no enumeration at all is rejected for the same
	// reason.  An elaborated specifier that only *uses* an existing
	// enumeration is not a declaration and is left alone.
	if(!scoped && !has_body && !has_base && (declare || entity < 0))
	{
		throw SemanticError("opaque declaration of an unscoped enumeration");
	}
	bool created = false;
	if(entity < 0)
	{
		entity = model_.NewEntity(kEntityEnum, name);
		model_.EntityOf(entity).type = model_.NewEnum(name, key);
		model_.BindType(lookup_scope, name, entity);
		created = true;
	}
	const int canonical = model_.EntityOf(entity).type;

	// 7.2/2: the first declaration fixes the underlying type, which is the
	// type its enumerators need when no enum-base says otherwise.  A later
	// declaration that names a different one is ill formed.
	int underlying = -1;
	if(has_base)
	{
		underlying = BuildDeclarator(base_node, -1, scope);
	}
	else if(model_.Get(canonical).underlying < 0)
	{
		underlying = model_.Fundamental(posttoken::FT_INT);
	}
	if(underlying >= 0)
	{
		Type& record = model_.Get(canonical);
		if(record.underlying >= 0 && record.underlying != underlying)
		{
			throw SemanticError("conflicting underlying type for an enumeration");
		}
		record.underlying = underlying;
	}

	if(!qualifier.empty())
	{
		// A qualified *definition* - `enum class writer::state : char { ... }` -
		// names a member of the scope the qualifier names, so that scope gets the
		// enumeration's own name and scope, and the definition registers the
		// qualified name where it is written.  An elaborated specifier that only
		// *uses* the name - `enum S::E *p` - defines nothing and is the
		// enumeration it found, whose canonical name is its own.
		if(!declare && !has_body)
		{
			if(out_key != 0)
			{
				*out_key = key;
			}
			return canonical;
		}
		if(declare && created)
		{
			AddTypeBinding(lookup_scope, name, entity, -1, key);
		}
		model_.ScopeFor(lookup_scope, entity, kScopeEnum, name);
		const int defined = model_.NewEnum(written, key);
		model_.Get(defined).underlying = underlying;
		if(declare)
		{
			AddTypeBinding(scope, written, entity, -1, key);
		}
		const int qualified_scope = model_.ScopeFor(scope, entity, kScopeEnum, written);
		BindEnumerators(node, qualified_scope, defined);
		if(out_key != 0)
		{
			*out_key = key;
		}
		return defined;
	}

	// Only a scoped enumeration owns a scope: an unscoped one's enumerators are
	// members of the scope that contains it (7.2/11), which the dump shows by
	// printing them there and no scope of their own.
	const int enum_scope = scoped ? model_.ScopeFor(scope, entity, kScopeEnum, written) : scope;
	// An enumeration binds its name in a scope once: a later declaration of the
	// same enumeration in the same scope adds no second line.
	if(declare && created && names_the_type)
	{
		AddTypeBinding(scope, written, entity, -1, key);
	}
	if(!has_body)
	{
		if(out_key != 0)
		{
			*out_key = key;
		}
		return canonical;
	}
	BindEnumerators(node, enum_scope, canonical);
	if(out_key != 0)
	{
		*out_key = key;
	}
	return canonical;
}

// The enumerators of one enumeration, in the scope that owns them.  A value
// with no initializer continues from the one before it, starting at zero.
void Analyzer::BindEnumerators(int node, int enum_scope, int enum_type)
{
	long long next = 0;
	const vector<int> children = ChildrenOf(node);
	for(size_t index = 0; index < children.size(); ++index)
	{
		const int child = children[index];
		if(Tag(child) != "enumerator")
		{
			continue;
		}
		const string& enumerator_name = Label(child);
		if(ChildCount(child) > 0)
		{
			next = Evaluate(ChildAt(child, 0), enum_scope).value;
		}
		const int value_entity = model_.NewEntity(kEntityEnumerator, enumerator_name);
		model_.EntityOf(value_entity).type = enum_type;
		model_.EntityOf(value_entity).decl_scope = enum_scope;
		model_.EntityOf(value_entity).has_value = true;
		model_.EntityOf(value_entity).value = next;
		Binding binding;
		binding.kind = kBindingEnumerator;
		binding.name = enumerator_name;
		binding.type = enum_type;
		binding.value = next;
		binding.entity = value_entity;
		model_.AddBinding(enum_scope, binding);
		model_.BindValue(enum_scope, enumerator_name, value_entity);
		++next;
	}
}

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------

void Analyzer::AnalyzeSimpleDeclaration(int node, int scope, int enclosing_class)
{
	const int seq = FindChild(node, "decl-specifier-seq");
	const int list = FindChild(node, "init-declarator-list");
	string declared_name;
	if(list >= 0 && ChildCount(list) > 0)
	{
		string full;
		CollectDeclaratorName(ChildAt(ChildAt(list, 0), 0), full);
		string qualifier;
		SplitQualifiedName(full, qualifier, declared_name);
	}
	Specifiers spec;
	if(seq >= 0)
	{
		AnalyzeSpecifiers(seq, scope, spec, declared_name, list < 0);
	}
	if(!spec.saw_type || list < 0)
	{
		return;
	}
	const vector<int> children = ChildrenOf(list);
	for(size_t index = 0; index < children.size(); ++index)
	{
		const int init = children[index];
		const int declarator = ChildAt(init, 0);
		const int initializer = ChildCount(init) > 1 ? ChildAt(init, 1) : -1;
		string full;
		CollectDeclaratorName(declarator, full);
		string qualifier;
		string name;
		SplitQualifiedName(full, qualifier, name);
		// 11.3/6: a friend function declaration declares the function in the
		// nearest enclosing namespace, not as a member of the class that grants
		// the friendship, so the class's own scope does not bind the name.
		const int target = spec.is_friend ? model_.EnclosingNamespace(scope)
		                                  : CurrentEntityScope(scope, qualifier);
		if(!name.empty() && model_.LookupNamespace(target, name) >= 0)
		{
			// A name a namespace-definition bound denotes a namespace, and no
			// declaration may rebind it as a type or a value (7.3.1/2).
			throw SemanticError("`" + name + "` is already a namespace");
		}
		int type = BuildDeclarator(declarator, spec.type, scope);
		if(spec.is_constexpr && model_.Get(type).kind != kTypeFunction)
		{
			// 7.1.5/9: a constexpr object is const.
			type = model_.Qualified(1, type);
		}
		if(spec.is_typedef)
		{
			Binding binding;
			binding.kind = kBindingTypeAlias;
			binding.name = name;
			binding.type = type;
			model_.AddBinding(target, binding);
			const int entity = model_.NewEntity(kEntityAlias, name);
			model_.EntityOf(entity).type = type;
			model_.EntityOf(entity).decl_scope = target;
			model_.BindType(target, name, entity);
			model_.NoteDeclaration(declarator, type, entity, target);
			continue;
		}
		if(model_.Get(type).kind == kTypeFunction)
		{
			CheckFunctionQualifiers(target, type);
			const int entity = FindOrCreateFunction(target, name, type);
			Binding binding;
			binding.kind = kBindingFunction;
			binding.name = name;
			binding.type = type;
			binding.entity = entity;
			model_.AddBinding(target, binding);
			model_.NoteDeclaration(declarator, type, entity, target);
			continue;
		}
		// 3.9.1/9: void is incomplete and can never be completed, so no object
		// may have it; and 8.3.2/5 restricts an initializer-less reference to a
		// parameter, a return type, a class member, or an explicit `extern`.
		const Type& declared = model_.Get(type);
		if(declared.kind == kTypeFundamental && declared.base == posttoken::FT_VOID)
		{
			throw SemanticError("an object cannot have type void");
		}
		const bool reference = declared.kind == kTypeLvalueReference ||
		                       declared.kind == kTypeRvalueReference;
		if(reference && initializer < 0 && !spec.is_extern && enclosing_class < 0)
		{
			throw SemanticError("a reference must be initialized");
		}
		if(!spec.is_extern && model_.Get(type).kind == kTypeClass &&
		   !model_.Get(type).complete)
		{
			throw SemanticError("an object cannot be defined with an incomplete type");
		}
		const int entity = FindOrCreateObject(target, name, type);
		Binding binding;
		binding.kind = kBindingVariable;
		binding.name = name;
		binding.type = type;
		binding.entity = entity;
		model_.AddBinding(target, binding);
		model_.NoteDeclaration(declarator, type, entity, target);
		// 9.2: a non-static data member takes part in its class's layout, in
		// declaration order.  A static or thread-local member is not part of the
		// object (9.4.2/1), so it is not recorded here.
		if(model_.ScopeOf(target).kind == kScopeClass && !spec.is_static &&
		   !spec.is_extern && !spec.is_thread_local && !spec.is_friend)
		{
			model_.ScopeOf(target).members.push_back(entity);
		}
		if(initializer >= 0)
		{
			const Constant value = Evaluate(initializer, scope);
			if(value.valid && (reference || spec.is_constexpr ||
			                   model_.Get(type).kind == kTypeCv))
			{
				model_.EntityOf(entity).has_value = true;
				model_.EntityOf(entity).value = value.value;
			}
		}
	}
}

void Analyzer::AnalyzeFunctionDefinition(int node, int scope, bool defer)
{
	const int seq = FindChild(node, "decl-specifier-seq");
	const int declarator = FindChild(node, "declarator");
	const int body = FindChild(node, "compound-statement");
	string full;
	CollectDeclaratorName(declarator, full);
	string qualifier;
	string name;
	SplitQualifiedName(full, qualifier, name);
	// A definition of a namespace or class member is looked up in the scope its
	// qualified name names, so `void n::f(T)` resolves `T` in `n` (3.4.1/8).
	const int target = CurrentEntityScope(scope, qualifier);
	Specifiers spec;
	if(seq >= 0)
	{
		AnalyzeSpecifiers(seq, target, spec, name, false);
	}
	const int type = BuildDeclarator(declarator, spec.type, target);
	CheckFunctionQualifiers(target, type);
	const int entity = FindOrCreateFunction(target, name, type);
	// 9.3.2/2: a definition written outside its class must match the member the
	// class declared.  A ref-qualifier is what tells two such members apart
	// (9.3.1/3), so a definition that writes one must find it declared; the
	// other half of the rule - a cv-qualifier that differs - is left to the
	// overload resolution PA6 does not model.
	if(!qualifier.empty() && model_.Get(type).func_ref != 0 &&
	   model_.Get(model_.EntityOf(entity).type).func_ref != model_.Get(type).func_ref)
	{
		throw SemanticError("ref-qualifier requires an ordinary non-static member function");
	}
	Binding binding;
	binding.kind = kBindingFunction;
	binding.name = name;
	binding.type = type;
	binding.entity = entity;
	model_.AddBinding(target, binding);
	model_.NoteDeclaration(declarator, type, entity, target);
	if(body < 0)
	{
		return;
	}
	NoteFunctionDefinition(entity, name, target, type);
	vector<int> params;
	bool varargs = false;
	vector<pair<string, int> > names;
	BuildParameterClause(FindChild(declarator, "parameter-clause"), target, params, varargs,
	                     &names);
	if(defer)
	{
		PendingBody record;
		record.owner = target;
		record.name = name;
		record.parameters = names;
		record.body = body;
		pending_.push_back(record);
		// PA7 prints a member body after the unit's own declarations, so the
		// declarator is kept for the dump to walk again.
		deferred_bodies_.push_back(make_pair(declarator, body));
		return;
	}
	OpenFunctionScope(target, name, names, body);
}

void Analyzer::AnalyzeSpecialMember(int node, int scope, int enclosing_class)
{
	const int declarator = FindChild(node, "declarator");
	const int body = FindChild(node, "compound-statement");
	string full;
	CollectDeclaratorName(declarator, full);
	string qualifier;
	string name;
	SplitQualifiedName(full, qualifier, name);
	// 9.3.2/2: a constructor or destructor defined outside its class names the
	// member in the class's own scope, like any other qualified definition, so
	// `S::S() { }` is the constructor `S` and not a new name at global scope.
	const int target = CurrentEntityScope(scope, qualifier);
	const int type = BuildDeclarator(declarator, model_.Fundamental(posttoken::FT_VOID), target);
	const int entity = FindOrCreateFunction(target, name, type);
	Binding binding;
	binding.kind = kBindingFunction;
	binding.name = name;
	binding.type = type;
	binding.entity = entity;
	model_.AddBinding(target, binding);
	model_.NoteDeclaration(declarator, type, entity, target);
	if(body < 0)
	{
		return;
	}
	NoteFunctionDefinition(entity, name, target, type);
	vector<int> params;
	bool varargs = false;
	vector<pair<string, int> > names;
	BuildParameterClause(FindChild(declarator, "parameter-clause"), target, params, varargs, &names);
	if(enclosing_class >= 0)
	{
		PendingBody record;
		record.owner = target;
		record.name = name;
		record.parameters = names;
		record.body = body;
		pending_.push_back(record);
		return;
	}
	OpenFunctionScope(target, name, names, body);
}

void Analyzer::AnalyzeBitField(int node, int scope)
{
	const int field = FindChild(node, "bit-field-declarator");
	const int declarator = field < 0 ? -1 : FindChild(field, "declarator");
	if(declarator < 0)
	{
		return;
	}
	Specifiers spec;
	const int seq = FindChild(node, "decl-specifier-seq");
	if(seq >= 0)
	{
		AnalyzeSpecifiers(seq, scope, spec, string(), false);
	}
	const int type = BuildDeclarator(declarator, spec.type, scope);
	string name;
	CollectDeclaratorName(declarator, name);
	const int entity = FindOrCreateObject(scope, name, type);
	Binding binding;
	binding.kind = kBindingVariable;
	binding.name = name;
	binding.type = type;
	binding.entity = entity;
	model_.AddBinding(scope, binding);
	model_.NoteDeclaration(declarator, type, entity, scope);
	if(model_.ScopeOf(scope).kind == kScopeClass)
	{
		model_.ScopeOf(scope).members.push_back(entity);
	}
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

bool Analyzer::IsStatementTag(const string& tag) const
{
	return tag == "compound-statement" || tag == "expression-statement" ||
	       tag == "if-statement" || tag == "switch-statement" ||
	       tag == "while-statement" || tag == "do-statement" ||
	       tag == "for-statement" || tag == "for-init-statement" ||
	       tag == "return-statement" || tag == "break-statement" ||
	       tag == "continue-statement" || tag == "goto-statement" ||
	       tag == "throw-statement" || tag == "try-block" ||
	       tag == "handler" || tag == "labeled-statement" ||
	       tag == "case-statement" || tag == "default-statement" ||
	       tag == "then" || tag == "else" || tag == "function-try-block";
}

// Whether a child of `node` is the substatement position that gets a scope of
// its own: the `then` and `else` of an `if`, and the body of a loop or switch.
bool IsSubstatement(const string& parent, const string& child)
{
	if(parent == "if-statement")
	{
		return child == "then" || child == "else";
	}
	if(parent == "while-statement" || parent == "for-statement" ||
	   parent == "do-statement" || parent == "switch-statement")
	{
		return child != "condition" && child != "for-init-statement" &&
		       child != "iteration";
	}
	return false;
}

void Analyzer::AnalyzeCompoundStatement(int node, int scope)
{
	const int block = model_.NewScope(kScopeBlock, "", scope);
	model_.NoteScope(node, block);
	AnalyzeStatements(node, block);
}

// The contents of one block scope: declarations bind in it and statements are
// analysed in it.
void Analyzer::AnalyzeStatements(int node, int block)
{
	const vector<int> children = ChildrenOf(node);
	for(size_t index = 0; index < children.size(); ++index)
	{
		const int child = children[index];
		const string& tag = Tag(child);
		if(IsDeclarationTag(tag))
		{
			AnalyzeDeclaration(child, block, -1);
		}
		else if(tag == "compound-statement")
		{
			AnalyzeCompoundStatement(child, block);
		}
		else if(IsStatementTag(tag))
		{
			AnalyzeStatement(child, block);
		}
		ScanCalls(child, block);
	}
}

// One statement written in the substatement position of a selection or
// iteration statement.  The position is one scope: a body written as a compound
// or as a single declaration is the contents of that scope, while a nested
// selection or iteration opens a scope of its own inside it.
void Analyzer::AnalyzeSlotStatement(int node, int slot)
{
	model_.NoteScope(node, slot);
	const string& tag = Tag(node);
	if(IsDeclarationTag(tag))
	{
		AnalyzeDeclaration(node, slot, -1);
	}
	else if(tag == "compound-statement")
	{
		AnalyzeStatements(node, slot);
	}
	else if(IsStatementTag(tag))
	{
		AnalyzeStatement(node, slot);
	}
	ScanCalls(node, slot);
}

void Analyzer::AnalyzeSubstatement(int node, int slot)
{
	// An `if` wraps each of its two substatements in a `then` or an `else`
	// node; every other substatement position holds the statement itself.
	if(!IsTag(node, "then") && !IsTag(node, "else"))
	{
		AnalyzeSlotStatement(node, slot);
		return;
	}
	const vector<int> children = ChildrenOf(node);
	for(size_t index = 0; index < children.size(); ++index)
	{
		AnalyzeSlotStatement(children[index], slot);
	}
}

// A call whose callee names a class opens that class's constructor scope, which
// is what makes `holder()` show the function the class's name calls.
void Analyzer::ScanCalls(int node, int scope)
{
	if(node < 0)
	{
		return;
	}
	if(IsTag(node, "compound-statement"))
	{
		return;
	}
	if(IsTag(node, "call-expression"))
	{
		NoteClassCall(node, scope);
	}
	const vector<int> children = ChildrenOf(node);
	for(size_t index = 0; index < children.size(); ++index)
	{
		ScanCalls(children[index], scope);
	}
}

void Analyzer::NoteClassCall(int node, int scope)
{
	const int callee = ChildAt(node, 0);
	if(Tag(callee) != "id-expression")
	{
		return;
	}
	const string& text = Label(callee);
	string qualifier;
	string name;
	SplitQualifiedName(text, qualifier, name);
	if(!qualifier.empty())
	{
		return;
	}
	const int entity = model_.LookupTypeUnqualified(scope, name);
	if(entity < 0)
	{
		return;
	}
	const Entity& record = model_.EntityOf(entity);
	if(record.kind != kEntityClass && record.kind != kEntityEnum)
	{
		return;
	}
	const int target = record.scope;
	if(target < 0)
	{
		return;
	}
	const Scope& owner = model_.ScopeOf(target);
	for(size_t index = 0; index < owner.children.size(); ++index)
	{
		const Scope& child = model_.ScopeOf(owner.children[index]);
		if(child.kind == kScopeFunction && child.name == record.name)
		{
			return;
		}
	}
	model_.NewScope(kScopeFunction, record.name, target);
}

void Analyzer::AnalyzeStatement(int node, int scope)
{
	const string& tag = Tag(node);
	if(tag == "compound-statement")
	{
		AnalyzeCompoundStatement(node, scope);
		return;
	}
	// 6.4/3 and 6.5.3/1: a selection or iteration statement's substatement is
	// in a scope that also holds a declaration made in its condition or in its
	// `for`-init, so the statement owns a block of its own; a declaration in a
	// `for`-init belongs to the loop and not to the block around it.  A handler
	// owns one too, so its exception-declaration is scoped to it.
	const bool owns_scope = tag == "if-statement" || tag == "switch-statement" ||
	                        tag == "while-statement" || tag == "do-statement" ||
	                        tag == "for-statement" || tag == "handler";
	const int inner = owns_scope ? model_.NewScope(kScopeBlock, "", scope) : scope;
	model_.NoteScope(node, inner);
	const vector<int> children = ChildrenOf(node);
	for(size_t index = 0; index < children.size(); ++index)
	{
		const int child = children[index];
		const string& child_tag = Tag(child);
		// A body written as a single declaration is still the substatement, so
		// it is recognised before the declaration it holds.
		if(IsSubstatement(tag, child_tag))
		{
			AnalyzeSubstatement(child, model_.NewScope(kScopeBlock, "", inner));
		}
		else if(IsDeclarationTag(child_tag))
		{
			AnalyzeDeclaration(child, inner, -1);
		}
		else if(child_tag == "condition")
		{
			// 6.4/3: a declaration in a condition belongs to the scope the
			// statement owns, which is where its substatements are looked up.
			AnalyzeCondition(child, inner);
		}
		else if(IsStatementTag(child_tag))
		{
			AnalyzeStatement(child, inner);
		}
	}
}

// The declaration a condition introduces, bound in the scope the selection or
// iteration statement owns so that its substatements see it (6.4/3).
void Analyzer::AnalyzeCondition(int node, int scope)
{
	const int declaration = FindChild(node, "condition-declaration");
	if(declaration < 0)
	{
		return;
	}
	const int seq = FindChild(declaration, "decl-specifier-seq");
	const int declarator = FindChild(declaration, "declarator");
	Specifiers spec;
	if(seq >= 0)
	{
		AnalyzeSpecifiers(seq, scope, spec, string(), false);
	}
	const int type = declarator < 0 ? spec.type : BuildDeclarator(declarator, spec.type, scope);
	string name;
	CollectDeclaratorName(declarator, name);
	if(name.empty())
	{
		return;
	}
	const int entity = FindOrCreateObject(scope, name, type);
	Binding binding;
	binding.kind = kBindingVariable;
	binding.name = name;
	binding.type = type;
	binding.entity = entity;
	model_.AddBinding(scope, binding);
	model_.NoteDeclaration(declarator, type, entity, scope);
}

}  // namespace semantic
}  // namespace cppgm
