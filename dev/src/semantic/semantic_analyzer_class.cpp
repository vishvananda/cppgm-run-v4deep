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
	model_.BindValue(scope, name, entity);
	return entity;
}

int Analyzer::FindOrCreateFunction(int scope, const string& name, int type)
{
	const int found = model_.LookupValue(scope, name);
	if(found >= 0 && model_.EntityOf(found).kind == kEntityFunction)
	{
		return found;
	}
	const int entity = model_.NewEntity(kEntityFunction, name);
	model_.EntityOf(entity).type = type;
	model_.BindValue(scope, name, entity);
	return entity;
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

int Analyzer::AnalyzeClassSpecifier(int node, int scope, int enclosing_class,
                                    const string& declared_name, bool is_static, int* out_key)
{
	const int key_node = FindChild(node, "class-key");
	const int key = key_node >= 0 ? ClassKeyOf(AfterColon(Label(key_node))) : kClassKeyClass;
	const bool anonymous = Label(node).empty();
	// 9.5/2: an unnamed union at namespace scope is a member of that namespace
	// only when it is `static`; otherwise it would have no linkage at all.
	if(anonymous && key == kClassKeyUnion && !is_static &&
	   model_.ScopeOf(scope).kind == kScopeNamespace)
	{
		throw SemanticError("an anonymous union at namespace scope must be static");
	}
	string written = Label(node);
	if(written.empty())
	{
		written = declared_name.empty() ? AnonymousClassName(node) : declared_name;
	}
	const int entity = DeclareClass(scope, written, key, true);
	if(model_.Get(model_.EntityOf(entity).type).complete)
	{
		throw SemanticError("redefinition of `" + written + "`");
	}
	const int class_scope = model_.ScopeFor(scope, entity, kScopeClass, written);
	ProcessClassBody(node, class_scope);
	// 9.4/1: the class is complete at the closing brace, so a member body may
	// name a member declared later in it.
	model_.Get(model_.EntityOf(entity).type).complete = true;
	// An anonymous class that no declaration names keeps its synthetic name for
	// its own scope only; nothing binds it.
	if(!anonymous || !declared_name.empty())
	{
		AddTypeBinding(scope, written, entity, key, -1);
	}
	if(anonymous && key == kClassKeyUnion)
	{
		// 9.5/3: an anonymous union's members are injected into the scope that
		// contains it, so `t` names the member without a member access.
		InjectUnionMembers(class_scope, scope);
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

	const bool anonymous = Label(node).empty();
	string written = Label(node);
	if(written.empty())
	{
		written = declared_name.empty() ? AnonymousEnumName() : declared_name;
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
	bool created = false;
	if(entity < 0)
	{
		// 7.2/3: an opaque declaration of an unscoped enumeration needs its
		// underlying type, because there is no enumerator list to infer one.
		if(!scoped && !has_body && !has_base)
		{
			throw SemanticError("opaque declaration of an unscoped enumeration");
		}
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
		// A qualified definition names a member of the scope the qualifier
		// names, so that scope gets the enumeration's own name and scope, and
		// the definition registers the qualified name where it is written.
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
	if(declare && created && (!anonymous || !declared_name.empty()))
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
		AnalyzeSpecifiers(seq, scope, enclosing_class, spec, declared_name, list < 0);
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
		const int target = CurrentEntityScope(scope, qualifier);
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
			model_.BindType(target, name, entity);
			continue;
		}
		if(model_.Get(type).kind == kTypeFunction)
		{
			const int entity = FindOrCreateFunction(target, name, type);
			Binding binding;
			binding.kind = kBindingFunction;
			binding.name = name;
			binding.type = type;
			binding.entity = entity;
			model_.AddBinding(target, binding);
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

void Analyzer::AnalyzeFunctionDefinition(int node, int scope, int enclosing_class, bool defer)
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
		AnalyzeSpecifiers(seq, target, enclosing_class, spec, name, false);
	}
	const int type = BuildDeclarator(declarator, spec.type, target);
	const int entity = FindOrCreateFunction(target, name, type);
	Binding binding;
	binding.kind = kBindingFunction;
	binding.name = name;
	binding.type = type;
	binding.entity = entity;
	model_.AddBinding(target, binding);
	if(body < 0)
	{
		return;
	}
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
		return;
	}
	OpenFunctionScope(target, name, names, body);
}

void Analyzer::AnalyzeSpecialMember(int node, int scope, int enclosing_class)
{
	const int declarator = FindChild(node, "declarator");
	const int body = FindChild(node, "compound-statement");
	const int type = BuildDeclarator(declarator, model_.Fundamental(posttoken::FT_VOID), scope);
	string name;
	CollectDeclaratorName(declarator, name);
	const int entity = FindOrCreateFunction(scope, name, type);
	Binding binding;
	binding.kind = kBindingFunction;
	binding.name = name;
	binding.type = type;
	binding.entity = entity;
	model_.AddBinding(scope, binding);
	if(body < 0)
	{
		return;
	}
	vector<int> params;
	bool varargs = false;
	vector<pair<string, int> > names;
	BuildParameterClause(FindChild(declarator, "parameter-clause"), scope, params, varargs, &names);
	if(enclosing_class >= 0)
	{
		PendingBody record;
		record.owner = scope;
		record.name = name;
		record.parameters = names;
		record.body = body;
		pending_.push_back(record);
		return;
	}
	OpenFunctionScope(scope, name, names, body);
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
		AnalyzeSpecifiers(seq, scope, -1, spec, string(), false);
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

void Analyzer::AnalyzeCompoundStatement(int node, int scope)
{
	const int block = model_.NewScope(kScopeBlock, "", scope);
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
	if(IsTag(node, "compound-statement"))
	{
		AnalyzeCompoundStatement(node, scope);
		return;
	}
	const vector<int> children = ChildrenOf(node);
	for(size_t index = 0; index < children.size(); ++index)
	{
		const int child = children[index];
		const string& tag = Tag(child);
		if(IsDeclarationTag(tag))
		{
			AnalyzeDeclaration(child, scope, -1);
		}
		else if(IsStatementTag(tag))
		{
			AnalyzeStatement(child, scope);
		}
	}
}

}  // namespace semantic
}  // namespace cppgm
