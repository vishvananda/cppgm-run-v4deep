#include "semantic/semantic_analyzer.h"

#include <cctype>
#include <sstream>

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

}  // namespace

Analyzer::Analyzer(Model& model, const syntax::SyntaxArena& arena,
                   const syntax::SyntaxSpellingPool& spellings,
                   const vector<syntax::SyntaxLiteralFacts>& literals)
	: model_(model)
	, arena_(arena)
	, spellings_(spellings)
	, literals_(literals)
	, anonymous_enums_(0)
	, local_classes_(0)
	, semantics_mode_(false)
	, sem_root_(-1)
	, return_type_(-1)
	, return_is_void_(false)
	, loop_depth_(0)
	, switch_depth_(0)
	, builtin_abort_(-1)
{}

// ---------------------------------------------------------------------------
// Tree access
// ---------------------------------------------------------------------------

const string& Analyzer::Tag(int node) const
{
	static const string empty;
	return node < 0 ? empty : arena_.Text(arena_.Node(node).tag);
}

const string& Analyzer::Label(int node) const
{
	static const string empty;
	return node < 0 ? empty : arena_.Text(arena_.Node(node).label);
}

size_t Analyzer::ChildCount(int node) const
{
	return arena_.ChildCount(node);
}

int Analyzer::ChildAt(int node, size_t index) const
{
	return arena_.ChildAt(node, index);
}

vector<int> Analyzer::ChildrenOf(int node) const
{
	vector<int> children;
	arena_.CollectChildren(node, children);
	return children;
}

bool Analyzer::IsTag(int node, const char* tag) const
{
	return node >= 0 && Tag(node) == tag;
}

int Analyzer::FindChild(int node, const char* tag) const
{
	const vector<int> children = ChildrenOf(node);
	for(size_t index = 0; index < children.size(); ++index)
	{
		if(Tag(children[index]) == tag)
		{
			return children[index];
		}
	}
	return -1;
}

bool Analyzer::HasChild(int node, const char* tag) const
{
	return FindChild(node, tag) >= 0;
}

void Analyzer::Run(int root)
{
	const int global = model_.GlobalScope();
	const vector<int> children = ChildrenOf(root);
	for(size_t index = 0; index < children.size(); ++index)
	{
		AnalyzeDeclaration(children[index], global, -1);
	}
}

// ---------------------------------------------------------------------------
// Declaration dispatch
// ---------------------------------------------------------------------------

bool Analyzer::IsDeclarationTag(const string& tag) const
{
	return tag == "simple-declaration" || tag == "function-definition" ||
	       tag == "namespace-definition" || tag == "namespace-alias-definition" ||
	       tag == "using-directive" || tag == "using-declaration" ||
	       tag == "alias-declaration" || tag == "static-assert-declaration" ||
	       tag == "template-declaration" || tag == "linkage-specification" ||
	       tag == "empty-declaration" || tag == "explicit-instantiation-declaration" ||
	       tag == "class-specifier" || tag == "class-forward-declaration" ||
	       tag == "enum-specifier" || tag == "bit-field-declaration" ||
	       tag == "special-member-declaration" || tag == "special-member-definition" ||
	       tag == "access-specifier";
}

void Analyzer::AnalyzeDeclaration(int node, int scope, int enclosing_class)
{
	const string& tag = Tag(node);
	if(tag == "simple-declaration")
	{
		AnalyzeSimpleDeclaration(node, scope, enclosing_class);
	}
	else if(tag == "function-definition")
	{
		AnalyzeFunctionDefinition(node, scope, enclosing_class >= 0);
	}
	else if(tag == "namespace-definition")
	{
		AnalyzeNamespaceDefinition(node, scope);
	}
	else if(tag == "namespace-alias-definition")
	{
		AnalyzeNamespaceAlias(node, scope);
	}
	else if(tag == "using-directive")
	{
		AnalyzeUsingDirective(node, scope);
	}
	else if(tag == "using-declaration")
	{
		AnalyzeUsingDeclaration(node, scope);
	}
	else if(tag == "alias-declaration")
	{
		AnalyzeAliasDeclaration(node, scope);
	}
	else if(tag == "static-assert-declaration")
	{
		AnalyzeStaticAssert(node, scope);
	}
	else if(tag == "template-declaration")
	{
		AnalyzeTemplateDeclaration(node, scope, enclosing_class);
	}
	else if(tag == "linkage-specification" || tag == "explicit-instantiation-declaration")
	{
		AnalyzeLinkageSpecification(node, scope, enclosing_class);
	}
	else if(tag == "class-forward-declaration")
	{
		AnalyzeClassForward(node, scope);
	}
	else if(tag == "class-specifier")
	{
		int key = kClassKeyClass;
		AnalyzeClassSpecifier(node, scope, string(), false, &key);
	}
	else if(tag == "enum-specifier")
	{
		int key = kEnumKeyPlain;
		AnalyzeEnumSpecifier(node, scope, true, string(), &key);
	}
	else if(tag == "bit-field-declaration")
	{
		AnalyzeBitField(node, scope);
	}
	else if(tag == "special-member-declaration" || tag == "special-member-definition")
	{
		AnalyzeSpecialMember(node, scope, enclosing_class);
	}
}

// ---------------------------------------------------------------------------
// Namespaces and using declarations
// ---------------------------------------------------------------------------

void Analyzer::AnalyzeNamespaceDefinition(int node, int scope)
{
	const string& written = Label(node);
	const bool inline_namespace = HasChild(node, "inline");
	int target = -1;
	if(written == "<unnamed>")
	{
		const int found = model_.LookupNamespace(scope, written);
		target = found >= 0 ? found : model_.NewScope(kScopeNamespace, written, scope);
		Scope& record = model_.ScopeOf(target);
		record.unnamed = true;
		if(found < 0)
		{
			// An unnamed namespace's members are visible in the enclosing
			// scope, which is what a using-directive does (7.3.1.1/1).
			model_.BindNamespace(scope, written, target);
			model_.ScopeOf(scope).directives.push_back(target);
		}
	}
	else
	{
		const int found = model_.LookupNamespace(scope, written);
		if(found < 0)
		{
			// A namespace-definition cannot extend a name a declaration
			// already bound, and cannot extend a namespace alias (7.3.2/3).
			if(model_.LookupValue(scope, written) >= 0 || model_.LookupType(scope, written) >= 0)
			{
				throw SemanticError("`" + written + "` is not a namespace");
			}
			target = model_.NewScope(kScopeNamespace, written, scope);
			model_.BindNamespace(scope, written, target);
		}
		else
		{
			if(model_.ScopeOf(scope).namespace_aliases.count(written) != 0)
			{
				throw SemanticError("namespace-definition names a namespace-alias");
			}
			target = found;
		}
	}
	if(inline_namespace)
	{
		model_.ScopeOf(target).inline_namespace = true;
		vector<int>& list = model_.ScopeOf(scope).inline_namespaces;
		bool present = false;
		for(size_t index = 0; index < list.size(); ++index)
		{
			present = present || list[index] == target;
		}
		if(!present)
		{
			list.push_back(target);
		}
	}
	const vector<int> children = ChildrenOf(node);
	for(size_t index = 0; index < children.size(); ++index)
	{
		const int child = children[index];
		if(IsTag(child, "inline"))
		{
			continue;
		}
		AnalyzeDeclaration(child, target, -1);
	}
}

void Analyzer::AnalyzeNamespaceAlias(int node, int scope)
{
	const string& name = Label(node);
	const int target_node = FindChild(node, "target");
	if(target_node < 0)
	{
		throw SemanticError("namespace-alias without a target");
	}
	const int target = ResolveNamespaceName(scope, Label(target_node));
	if(target < 0)
	{
		throw SemanticError("namespace-alias target `" + Label(target_node) +
		                    "` is not a namespace");
	}
	model_.BindNamespace(scope, name, target);
	model_.ScopeOf(scope).namespace_aliases[name] = true;
}

void Analyzer::AnalyzeUsingDirective(int node, int scope)
{
	const int target_node = FindChild(node, "target");
	if(target_node < 0)
	{
		throw SemanticError("using-directive without a target");
	}
	const int target = ResolveNamespaceName(scope, Label(target_node));
	if(target < 0)
	{
		throw SemanticError("using-directive target `" + Label(target_node) +
		                    "` is not a namespace");
	}
	model_.ScopeOf(scope).directives.push_back(target);
}

void Analyzer::AnalyzeUsingDeclaration(int node, int scope)
{
	const int target_node = FindChild(node, "target");
	if(target_node < 0)
	{
		throw SemanticError("using-declaration without a target");
	}
	const string& text = Label(target_node);
	// 7.3.3/1: a using-declaration shall not name a template-id.
	if(text.find('<') != string::npos)
	{
		throw SemanticError("using-declaration names a template-id");
	}
	string qualifier;
	string name;
	SplitQualifiedName(text, qualifier, name);
	const int target = model_.ResolveQualifier(scope, qualifier);
	const int type_entity = model_.LookupTypeIn(target, name);
	if(type_entity >= 0)
	{
		const Entity& record = model_.EntityOf(type_entity);
		if(record.kind == kEntityClass || record.kind == kEntityEnum)
		{
			AddTypeBinding(scope, name, type_entity, -1, -1);
		}
		else
		{
			Binding binding;
			binding.kind = kBindingTypeAlias;
			binding.name = name;
			binding.type = record.type;
			model_.AddBinding(scope, binding);
			model_.BindType(scope, name, type_entity);
		}
		return;
	}
	const int value_entity = model_.LookupValueIn(target, name);
	if(value_entity < 0)
	{
		throw SemanticError("using-declaration target `" + text + "` was not found");
	}
	const Entity& record = model_.EntityOf(value_entity);
	Binding binding;
	binding.entity = value_entity;
	binding.name = name;
	binding.type = record.type;
	if(record.kind == kEntityFunction)
	{
		binding.kind = kBindingFunction;
	}
	else if(record.kind == kEntityEnumerator)
	{
		binding.kind = kBindingEnumerator;
		binding.value = record.value;
	}
	else
	{
		binding.kind = kBindingVariable;
	}
	model_.AddBinding(scope, binding);
	model_.BindValue(scope, name, value_entity);
}

void Analyzer::AnalyzeAliasDeclaration(int node, int scope)
{
	const string& name = Label(node);
	const int type_node = FindChild(node, "type-id");
	if(type_node < 0)
	{
		throw SemanticError("alias-declaration without a type");
	}
	const int type = BuildDeclarator(type_node, -1, scope);
	Binding binding;
	binding.kind = kBindingTypeAlias;
	binding.name = name;
	binding.type = type;
	model_.AddBinding(scope, binding);
	const int entity = model_.NewEntity(kEntityAlias, name);
	model_.EntityOf(entity).type = type;
	model_.EntityOf(entity).decl_scope = scope;
	model_.BindType(scope, name, entity);
	model_.NoteDeclaration(type_node, type, entity, scope);
}

void Analyzer::AnalyzeStaticAssert(int node, int scope)
{
	const int condition = ChildAt(node, 0);
	const Constant value = Evaluate(condition, scope);
	if(value.value == 0)
	{
		throw SemanticError("static_assert failed");
	}
}

void Analyzer::AnalyzeTemplateDeclaration(int node, int scope, int enclosing_class)
{
	const int parameters = model_.NewScope(kScopeTemplateParameters, "", scope);
	const int clause = FindChild(node, "template-parameter-clause");
	if(clause >= 0)
	{
		const int list = FindChild(clause, "template-parameter-list");
		if(list >= 0)
		{
			const vector<int> children = ChildrenOf(list);
			for(size_t index = 0; index < children.size(); ++index)
			{
				const int parameter = children[index];
				if(!IsTag(parameter, "type-parameter"))
				{
					continue;
				}
				const int identifier = FindChild(parameter, "identifier");
				if(identifier < 0)
				{
					continue;
				}
				const string& name = Label(identifier);
				const bool template_parameter = HasChild(parameter, "template-template-parameter");
				// A template-template parameter's own parameter clause declares
				// names that are visible only inside that clause (14.1/2), so
				// they are not bound here.
				const int type = model_.NewTemplateParameter(name, template_parameter);
				const int entity = model_.NewEntity(kEntityTemplateParameter, name);
				model_.EntityOf(entity).type = type;
				AddTypeBinding(parameters, name, entity, -1, -1);
			}
		}
	}
	const vector<int> children = ChildrenOf(node);
	for(size_t index = 0; index < children.size(); ++index)
	{
		const int child = children[index];
		if(IsTag(child, "template-parameter-clause"))
		{
			continue;
		}
		AnalyzeDeclaration(child, parameters, enclosing_class);
	}
}

void Analyzer::AnalyzeLinkageSpecification(int node, int scope, int enclosing_class)
{
	const vector<int> children = ChildrenOf(node);
	for(size_t index = 0; index < children.size(); ++index)
	{
		AnalyzeDeclaration(children[index], scope, enclosing_class);
	}
}

void Analyzer::AnalyzeClassForward(int node, int scope)
{
	const string& written = Label(node);
	if(written.empty())
	{
		return;
	}
	const int key_node = FindChild(node, "class-key");
	const int key = key_node >= 0 && AfterColon(Label(key_node)) == "union"
	    ? kClassKeyUnion
	    : (key_node >= 0 && AfterColon(Label(key_node)) == "struct"
	       ? kClassKeyStruct : kClassKeyClass);
	// An elaborated class specifier may find a type an ordinary-name binding
	// hides (3.4.4/2), so the type category is searched on its own.
	int entity = model_.LookupType(scope, written);
	if(entity >= 0 && model_.EntityOf(entity).kind == kEntityClass)
	{
		AddTypeBinding(scope, written, entity, key, -1);
		return;
	}
	entity = DeclareClass(scope, written, key, false);
	AddTypeBinding(scope, written, entity, key, -1);
}

}  // namespace semantic
}  // namespace cppgm
