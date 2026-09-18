// PA7's declaration and statement walk over the analysed tree.
//
// The PA6 analysis already decided what every declaration binds and which scope
// every statement belongs to; this pass reads those answers back and builds the
// resolved tree the dump prints.  A declaration line is not printed for a class
// or an enumeration - the fixtures show those only through the objects and
// members they declare - while a namespace keeps its contents as children.

#include "semantic/semantic_analyzer.h"

#include <sstream>

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

// The plain word a terminal node's label carries, for a keyword written as
// `KW_CONSTEXPR:constexpr`.
string OperatorWord(const string& label)
{
	return AfterColon(label);
}

string Number(long long value)
{
	ostringstream out;
	out << value;
	return out.str();
}

}  // namespace

void Analyzer::BuildSemantics(int root)
{
	sem_root_ = sem_.Add("translation-unit");
	const int global = model_.GlobalScope();
	const vector<int> children = ChildrenOf(root);
	for(size_t index = 0; index < children.size(); ++index)
	{
		vector<int> declarations;
		SemDeclaration(children[index], global, declarations);
		for(size_t inner = 0; inner < declarations.size(); ++inner)
		{
			sem_.AddChild(sem_root_, declarations[inner]);
		}
	}
	SemanticsImplicitBodies();
}

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------

void Analyzer::SemDeclaration(int node, int scope, vector<int>& out)
{
	const string& tag = Tag(node);
	if(tag == "namespace-definition")
	{
		const int built = SemNamespaceDefinition(node, scope);
		if(built >= 0)
		{
			out.push_back(built);
		}
		return;
	}
	if(tag == "simple-declaration")
	{
		SemSimpleDeclaration(node, scope, out);
		return;
	}
	if(tag == "function-definition")
	{
		const int built = SemFunctionDefinition(node, scope);
		if(built >= 0)
		{
			out.push_back(built);
		}
		return;
	}
	if(tag == "alias-declaration")
	{
		const int built = SemAliasDeclaration(node, scope);
		if(built >= 0)
		{
			out.push_back(built);
		}
		return;
	}
	if(tag == "linkage-specification" || tag == "explicit-instantiation-declaration")
	{
		const vector<int> children = ChildrenOf(node);
		for(size_t index = 0; index < children.size(); ++index)
		{
			SemDeclaration(children[index], scope, out);
		}
		return;
	}
	if(tag == "template-declaration")
	{
		const int built = SemTemplateDeclaration(node, scope);
		if(built >= 0)
		{
			out.push_back(built);
		}
		return;
	}
	if(tag == "class-specifier")
	{
		// An unnamed class a declaration uses is not an object of its own, but
		// an anonymous union with no declarator is one (9.5/2).
		const int built = SemAnonymousUnionStorage(node, scope);
		if(built >= 0)
		{
			out.push_back(built);
		}
		return;
	}
	if(tag == "special-member-declaration" || tag == "special-member-definition")
	{
		// 12.1/5, 12.4/3: `= default` and `= delete` declare the special member
		// the class's own name calls, which the dump shows as a definition with
		// the body the source did not write.
		const int built = SemSpecialMember(node, scope);
		if(built >= 0)
		{
			out.push_back(built);
		}
		return;
	}
}

int Analyzer::SemSpecialMember(int node, int scope)
{
	(void)scope;
	const int declarator = FindChild(node, "declarator");
	const Model::DeclarationFact* fact = model_.DeclarationAt(declarator);
	if(fact == 0)
	{
		return -1;
	}
	const int definition = sem_.Add("function-definition", QualifiedEntityName(fact->entity),
	                                true);
	sem_.SetType(definition, BoundSpelling(fact->type, fact->scope));
	// 9.3.1/3: a special member is a non-static member function, so its
	// definition writes the implicit object parameter first.
	if(model_.ScopeOf(fact->scope).kind == kScopeClass)
	{
		const int class_entity = model_.ScopeOf(fact->scope).entity;
		const int class_type = model_.EntityOf(class_entity).type;
		const int qualified = model_.Qualified(model_.Get(fact->type).quals, class_type);
		const int built = sem_.Add("parameter", "this", true);
		sem_.SetType(built, Spell(model_.Pointer(qualified)));
		sem_.AddChild(definition, built);
	}
	const int clause = FindChild(declarator, "parameter-clause");
	if(clause >= 0)
	{
		const vector<int> parameters = ChildrenOf(clause);
		for(size_t index = 0; index < parameters.size(); ++index)
		{
			const Model::DeclarationFact* parameter =
			    model_.DeclarationAt(parameters[index]);
			if(parameter == 0)
			{
				continue;
			}
			const int built = sem_.Add("parameter", string(), true);
			sem_.SetType(built, Spell(model_.AdjustParameter(parameter->type)));
			sem_.AddChild(definition, built);
		}
	}
	sem_.AddChild(definition, sem_.Add("compound-statement"));
	return definition;
}

int Analyzer::SemNamespaceDefinition(int node, int scope)
{
	(void)scope;
	const int built = sem_.Add("namespace-definition", Label(node), true);
	const int inner = model_.ScopeAt(node);
	if(inner < 0)
	{
		return built;
	}
	const vector<int> children = ChildrenOf(node);
	for(size_t index = 0; index < children.size(); ++index)
	{
		if(IsTag(children[index], "inline"))
		{
			continue;
		}
		vector<int> declarations;
		SemDeclaration(children[index], inner, declarations);
		for(size_t inner_index = 0; inner_index < declarations.size(); ++inner_index)
		{
			sem_.AddChild(built, declarations[inner_index]);
		}
	}
	return built;
}

// 9.5/1: `union { ... };` with no declarator declares an object with no name,
// which the dump calls `__anonymous_union_storage__<range>`.  Its members are
// names in the enclosing scope (9.5/3), so an id-expression that resolves to
// one reaches it through this object.
int Analyzer::SemAnonymousUnionStorage(int specifier, int scope)
{
	if(!Label(specifier).empty())
	{
		return -1;
	}
	const int entity = model_.LookupType(scope, AnonymousClassName(specifier));
	if(entity < 0 || model_.EntityOf(entity).kind != kEntityClass)
	{
		return -1;
	}
	const int class_type = model_.EntityOf(entity).type;
	if(model_.Get(class_type).class_key != kClassKeyUnion)
	{
		return -1;
	}
	ostringstream name;
	name << "__anonymous_union_storage__" << arena_.Start(specifier) << '_'
	     << arena_.End(specifier);
	const int storage = model_.NewEntity(kEntityObject, name.str());
	model_.EntityOf(storage).type = class_type;
	model_.EntityOf(storage).decl_scope = scope;
	union_storage_[class_type] = storage;
	const int variable = sem_.Add("variable", name.str(), true);
	sem_.SetType(variable, Spell(class_type));
	Resolved object;
	object.type = class_type;
	object.category = kLvalue;
	object.node = sem_.Add("id-expression", kLvalue, Spell(class_type), name.str());
	sem_.AddChild(variable, SemDefaultInitialization(object, class_type, scope));
	return variable;
}

void Analyzer::SemSimpleDeclaration(int node, int scope, vector<int>& out)
{
	const int seq = FindChild(node, "decl-specifier-seq");
	const int list = FindChild(node, "init-declarator-list");
	if(seq >= 0)
	{
		const vector<int> specifiers = ChildrenOf(seq);
		for(size_t index = 0; index < specifiers.size(); ++index)
		{
			if(!IsTag(specifiers[index], "class-specifier"))
			{
				continue;
			}
			if(list >= 0 && ChildCount(list) > 0)
			{
				continue;
			}
			const int built = SemAnonymousUnionStorage(specifiers[index], scope);
			if(built >= 0)
			{
				out.push_back(built);
			}
		}
	}
	if(list < 0)
	{
		return;
	}
	const vector<int> declarators = ChildrenOf(list);
	for(size_t index = 0; index < declarators.size(); ++index)
	{
		const int init = declarators[index];
		const int declarator = ChildAt(init, 0);
		const int initializer = ChildCount(init) > 1 ? ChildAt(init, 1) : -1;
		const Model::DeclarationFact* fact = model_.DeclarationAt(declarator);
		if(fact == 0)
		{
			continue;
		}
		string full;
		CollectDeclaratorName(declarator, full);
		string qualifier;
		string name;
		SplitQualifiedName(full, qualifier, name);
		if(fact->entity >= 0 && model_.EntityOf(fact->entity).kind == kEntityAlias)
		{
			const int alias = sem_.Add("type-alias", name, true);
			sem_.SetType(alias, Spell(fact->type));
			out.push_back(alias);
			continue;
		}
		if(model_.Get(fact->type).kind == kTypeFunction)
		{
			// A function declaration prints the qualified name the entity has,
			// which is what makes `namespace n { int g(); }` read `n::g`.
			const int declaration = sem_.Add("function-declaration",
			                                 QualifiedEntityName(fact->entity), true);
			sem_.SetType(declaration, BoundSpelling(fact->type, fact->scope));
			out.push_back(declaration);
			continue;
		}
		out.push_back(SemVariable(scope, name, fact->entity, fact->type, initializer,
		                          IsConstexprSpecifier(seq)));
		(void)node;
	}
}

// One declared object: its line, and whatever initialised it.
int Analyzer::SemVariable(int scope, const string& name, int entity, int type,
                          int initializer, bool is_constexpr)
{
	const int variable = sem_.Add("variable", name, true);
	int declared = type;
	const int built = SemInitializer(initializer, scope, declared, name, entity, is_constexpr);
	// 8.3.4/3: an initialiser completes an array of unknown bound, and the
	// declaration line prints the completed type.
	sem_.SetType(variable, Spell(declared));
	if(built >= 0)
	{
		sem_.AddChild(variable, built);
	}
	return variable;
}

// The value a declaration gives its object: an initializer, a braced list, a
// direct-initialisation, or - for a class with none - the constructor an object
// of that type needs (8.5/6).
int Analyzer::SemInitializer(int node, int scope, int& type, const string& name, int entity,
                             bool is_constexpr)
{
	const int plain = ReferredType(type);
	if(node < 0)
	{
		if(model_.Get(plain).kind == kTypeClass)
		{
			Resolved object;
			object.type = plain;
			object.category = kLvalue;
			object.node = sem_.Add("id-expression", kLvalue, Spell(plain), name);
			return SemDefaultInitialization(object, plain, scope);
		}
		return -1;
	}
	const int child = ChildAt(node, 0);
	if(child < 0)
	{
		return -1;
	}
	const string& tag = Tag(child);
	if(tag == "braced-init-list")
	{
		Resolved result = SemBracedInit(child, scope);
		// 8.3.4/3: an array of unknown bound takes the bound its initialiser
		// gives it, which is the type both lines print.
		if(model_.Get(plain).kind == kTypeArray && model_.Get(plain).bound < 0)
		{
			const int bound = model_.Array(static_cast<long long>(result.node >= 0
			        ? sem_.Node(result.node).children.size() : 0),
			        model_.Get(plain).base);
			type = bound;
			sem_.SetType(result.node, Spell(bound));
		}
		result.type = type;
		result.category = kLvalue;
		sem_.SetType(result.node, Spell(type));
		sem_.SetCategory(result.node, kLvalue);
		return result.node;
	}
	if(tag == "paren-initializer")
	{
		const vector<int> arguments = ChildrenOf(child);
		if(arguments.size() == 1)
		{
			return SemExpr(arguments[0], scope, ReferredType(type)).node;
		}
		if(arguments.empty() && model_.Get(plain).kind == kTypeClass)
		{
			Resolved object;
			object.type = plain;
			object.category = kLvalue;
			object.node = sem_.Add("id-expression", kLvalue, Spell(plain), name);
			return SemDefaultInitialization(object, plain, scope);
		}
		throw SemanticError("a direct-initialisation takes one argument");
	}
	if(tag == "special-initializer")
	{
		return -1;
	}
	Resolved value = SemExpr(child, scope, ReferredType(type));
	int plain_target = ReferredType(type);
	if(model_.Get(plain_target).kind == kTypeCv)
	{
		plain_target = model_.Get(plain_target).base;
	}
	if(value.null_zero && NullPointerTarget(plain_target) >= 0)
	{
		// 4.10/1: an integer literal zero that initialises a pointer prints as
		// the pointer it converted to.
		sem_.SetType(value.node, Spell(plain_target));
	}
	else if(is_constexpr)
	{
		// A constant object's initialiser is folded, so the literal carries the
		// type the object was declared with.
		sem_.SetType(value.node, Spell(type));
	}
	return value.node;
}

// Whether a decl-specifier-seq wrote `constexpr`.
bool Analyzer::IsConstexprSpecifier(int seq) const
{
	if(seq < 0)
	{
		return false;
	}
	const vector<int> children = ChildrenOf(seq);
	for(size_t index = 0; index < children.size(); ++index)
	{
		if(OperatorWord(Label(children[index])) == "constexpr")
		{
			return true;
		}
	}
	return false;
}

// The constructor an object definition of class type needs when it has no
// initializer, and the definition the dump prints at the end of the unit.
int Analyzer::SemDefaultInitialization(const Resolved& object, int class_type, int scope)
{
	const int constructor = ImplicitConstructor(class_type, scope);
	const int action = sem_.Add("constructor-action", QualifiedEntityName(constructor), true);
	const int call = sem_.Add("call-expression", kPrvalue,
	                          Spell(model_.Fundamental(posttoken::FT_VOID)));
	const int callee = sem_.Add("callee", QualifiedEntityName(constructor), true);
	const int class_scope = ClassScopeOf(class_type);
	sem_.SetType(callee, BoundSpelling(model_.EntityOf(constructor).type, class_scope));
	sem_.AddChild(call, callee);
	const int address = sem_.Add("unary-expression", kPrvalue,
	                             Spell(model_.Pointer(class_type)), "OP_AMP:&");
	sem_.AddChild(address, object.node);
	sem_.AddChild(call, address);
	sem_.AddChild(action, call);
	return action;
}

// The implicit default constructor of a class, created once per class and
// remembered so the dump can print its definition after the unit's own
// declarations.
int Analyzer::ImplicitConstructor(int class_type, int scope)
{
	(void)scope;
	map<int, int>::const_iterator found = implicit_ctors_.find(class_type);
	if(found != implicit_ctors_.end())
	{
		return found->second;
	}
	const int class_scope = ClassScopeOf(class_type);
	if(class_scope < 0)
	{
		throw SemanticError("an object of a class type has no constructor");
	}
	const int class_entity = model_.ScopeOf(class_scope).entity;
	const string name = class_entity < 0 ? string() : model_.EntityOf(class_entity).name;
	// 12.1/5: the implicitly-declared default constructor takes no parameters
	// of its own; the implicit object parameter is part of how it is called.
	if(model_.LookupValue(class_scope, name) >= 0)
	{
		throw SemanticError("the class declares its own constructors");
	}
	const int entity = model_.NewEntity(kEntityFunction, name);
	model_.EntityOf(entity).type =
	    model_.Function(model_.Fundamental(posttoken::FT_VOID), vector<int>(), false);
	model_.EntityOf(entity).decl_scope = class_scope;
	implicit_ctors_[class_type] = entity;
	implicit_classes_.push_back(class_type);
	return entity;
}

void Analyzer::SemanticsImplicitBodies()
{
	// A member body written inside its class is a complete-class context, so
	// the analysis read it after the member list; the dump prints it after the
	// unit's own declarations, which is where the reference puts it.
	for(size_t index = 0; index < deferred_bodies_.size(); ++index)
	{
		const Model::DeclarationFact* fact =
		    model_.DeclarationAt(deferred_bodies_[index].first);
		if(fact == 0 || HasTemplateParameter(fact->type))
		{
			continue;
		}
		const int definition = SemFunctionDefinition(0, 0, deferred_bodies_[index].first,
		                                             deferred_bodies_[index].second);
		if(definition >= 0)
		{
			sem_.AddChild(sem_root_, definition);
		}
	}
	for(size_t index = 0; index < implicit_classes_.size(); ++index)
	{
		const int class_type = implicit_classes_[index];
		const int constructor = implicit_ctors_[class_type];
		const int class_scope = ClassScopeOf(class_type);
		const int definition = sem_.Add("function-definition",
		                                QualifiedEntityName(constructor), true);
		sem_.SetType(definition, BoundSpelling(model_.EntityOf(constructor).type, class_scope));
		const int parameter = sem_.Add("parameter", "this", true);
		sem_.SetType(parameter, Spell(model_.Pointer(class_type)));
		sem_.AddChild(definition, parameter);
		const int body = sem_.Add("compound-statement");
		sem_.AddChild(definition, body);
		sem_.AddChild(sem_root_, definition);
	}
}

int Analyzer::SemFunctionDefinition(int node, int scope, int declarator, int body)
{
	(void)scope;
	if(declarator < 0)
	{
		declarator = FindChild(node, "declarator");
		body = FindChild(node, "compound-statement");
	}
	const Model::DeclarationFact* fact = model_.DeclarationAt(declarator);
	if(fact == 0)
	{
		return -1;
	}
	const int definition = sem_.Add("function-definition", QualifiedEntityName(fact->entity),
	                                true);
	sem_.SetType(definition, BoundSpelling(fact->type, fact->scope));
	// 9.3.1/3: a non-static member function takes an implicit object parameter,
	// which the dump writes out before the declared ones.
	if(model_.ScopeOf(fact->scope).kind == kScopeClass)
	{
		const int class_entity = model_.ScopeOf(fact->scope).entity;
		const int class_type = model_.EntityOf(class_entity).type;
		const int qualified = model_.Qualified(model_.Get(fact->type).quals, class_type);
		const int built = sem_.Add("parameter", "this", true);
		sem_.SetType(built, Spell(model_.Pointer(qualified)));
		sem_.AddChild(definition, built);
	}
	const int clause = FindChild(declarator, "parameter-clause");
	if(clause >= 0)
	{
		const vector<int> parameters = ChildrenOf(clause);
		for(size_t index = 0; index < parameters.size(); ++index)
		{
			const int declaration = parameters[index];
			if(!IsTag(declaration, "parameter-declaration") ||
			   FindChild(declaration, "ellipsis") >= 0)
			{
				continue;
			}
			const Model::DeclarationFact* parameter = model_.DeclarationAt(declaration);
			if(parameter == 0)
			{
				continue;
			}
			string name;
			int inner = FindChild(declaration, "declarator");
			if(inner < 0)
			{
				inner = FindChild(declaration, "abstract-declarator");
			}
			CollectDeclaratorName(inner, name);
			const int built = sem_.Add("parameter", name, true);
			// 8.3.5/5: a by-value parameter takes the unqualified type of its
			// own declaration, which is what the signature is built from.
			sem_.SetType(built, Spell(model_.AdjustParameter(parameter->type)));
			sem_.AddChild(definition, built);
		}
	}
	if(body < 0)
	{
		return definition;
	}
	// 6.6.3/2: a return statement is checked against the function's own return
	// type, which is the one this definition wrote.
	return_type_ = model_.Get(fact->type).base;
	return_is_void_ = model_.Get(return_type_).kind == kTypeFundamental &&
	                  model_.Get(return_type_).base == posttoken::FT_VOID;
	if(body < 0)
	{
		return definition;
	}
	const int built = SemCompoundStatement(body, model_.ScopeAt(body));
	sem_.AddChild(definition, built);
	return definition;
}

int Analyzer::SemAliasDeclaration(int node, int scope)
{
	(void)scope;
	const int type_node = FindChild(node, "type-id");
	const Model::DeclarationFact* fact = model_.DeclarationAt(type_node);
	const int alias = sem_.Add("type-alias", Label(node), true);
	if(fact != 0)
	{
		sem_.SetType(alias, Spell(fact->type));
	}
	return alias;
}

int Analyzer::SemTemplateDeclaration(int node, int scope)
{
	(void)node;
	(void)scope;
	return -1;
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

int Analyzer::SemCompoundStatement(int node, int scope)
{
	const int built = sem_.Add("compound-statement");
	const vector<int> children = ChildrenOf(node);
	for(size_t index = 0; index < children.size(); ++index)
	{
		const int statement = SemStatement(children[index], scope);
		sem_.AddChild(built, statement);
	}
	return built;
}

int Analyzer::SemSlotStatement(int node, int scope)
{
	return SemStatement(node, scope < 0 ? model_.ScopeAt(node) : scope);
}

int Analyzer::SemStatement(int node, int scope)
{
	if(node < 0)
	{
		return -1;
	}
	const int noted = model_.ScopeAt(node);
	if(noted >= 0)
	{
		scope = noted;
	}
	else if(scope < 0)
	{
		scope = model_.GlobalScope();
	}
	const string& tag = Tag(node);
	if(tag == "compound-statement")
	{
		return SemCompoundStatement(node, scope);
	}
	if(tag == "simple-declaration")
	{
		const int wrapper = sem_.Add("simple-declaration");
		vector<int> declarations;
		SemSimpleDeclaration(node, scope, declarations);
		for(size_t index = 0; index < declarations.size(); ++index)
		{
			sem_.AddChild(wrapper, declarations[index]);
		}
		return wrapper;
	}
	if(tag == "alias-declaration")
	{
		return SemAliasDeclaration(node, scope);
	}
	if(tag == "expression-statement")
	{
		const int built = sem_.Add("expression-statement");
		const int child = ChildAt(node, 0);
		if(child >= 0)
		{
			sem_.AddChild(built, SemExpr(child, scope).node);
		}
		return built;
	}
	if(tag == "return-statement")
	{
		const int built = sem_.Add("return-statement");
		const int child = ChildAt(node, 0);
		if(child >= 0)
		{
			Resolved value = SemExpr(child, scope);
			if(return_is_void_)
			{
				throw SemanticError("a value cannot be returned from a void function");
			}
			if(Convert(value, ReferredType(return_type_), scope).rank == 0)
			{
				throw SemanticError("the returned value does not convert to the return type");
			}
			if(value.null_zero && NullPointerTarget(ReferredType(return_type_)) >= 0)
			{
				sem_.SetType(value.node, Spell(ReferredType(return_type_)));
			}
			sem_.AddChild(built, value.node);
		}
		return built;
	}
	if(tag == "if-statement")
	{
		const int built = sem_.Add("if-statement");
		const vector<int> children = ChildrenOf(node);
		for(size_t index = 0; index < children.size(); ++index)
		{
			const int child = children[index];
			if(IsTag(child, "condition"))
			{
				sem_.AddChild(built, SemCondition(child, scope, false));
			}
			else if(IsTag(child, "then") || IsTag(child, "else"))
			{
				const int slot = sem_.Add(Tag(child));
				const vector<int> inner = ChildrenOf(child);
				for(size_t part = 0; part < inner.size(); ++part)
				{
					sem_.AddChild(slot, SemSlotStatement(inner[part], -1));
				}
				sem_.AddChild(built, slot);
			}
		}
		return built;
	}
	if(tag == "while-statement" || tag == "do-statement" || tag == "for-statement")
	{
		const int built = sem_.Add(tag);
		++loop_depth_;
		const vector<int> children = ChildrenOf(node);
		for(size_t index = 0; index < children.size(); ++index)
		{
			const int child = children[index];
			if(IsTag(child, "condition"))
			{
				sem_.AddChild(built, SemCondition(child, scope, false));
			}
			else if(IsTag(child, "for-init-statement"))
			{
				sem_.AddChild(built, SemForInit(child, scope));
			}
			else if(IsTag(child, "iteration"))
			{
				const int iteration = sem_.Add("iteration");
				const int inner = ChildAt(child, 0);
				if(inner >= 0)
				{
					sem_.AddChild(iteration, SemExpr(inner, scope).node);
				}
				sem_.AddChild(built, iteration);
			}
			else
			{
				sem_.AddChild(built, SemSlotStatement(child, -1));
			}
		}
		--loop_depth_;
		return built;
	}
	if(tag == "switch-statement")
	{
		const int built = sem_.Add("switch-statement");
		++switch_depth_;
		const vector<int> children = ChildrenOf(node);
		for(size_t index = 0; index < children.size(); ++index)
		{
			const int child = children[index];
			if(IsTag(child, "condition"))
			{
				sem_.AddChild(built, SemCondition(child, scope, true));
			}
			else
			{
				sem_.AddChild(built, SemSlotStatement(child, -1));
			}
		}
		--switch_depth_;
		return built;
	}
	if(tag == "case-statement" || tag == "default-statement")
	{
		if(switch_depth_ == 0)
		{
			throw SemanticError("`" + tag.substr(0, tag.size() - 10) +
			                    "` outside a switch statement");
		}
		const int built = sem_.Add(tag);
		const vector<int> children = ChildrenOf(node);
		for(size_t index = 0; index < children.size(); ++index)
		{
			const int child = children[index];
			if(index == 0 && !IsStatementTag(Tag(child)) && !IsDeclarationTag(Tag(child)))
			{
				// 6.4.2/1: a case label is an integral constant expression.
				const Constant value = Evaluate(child, scope);
				if(!value.valid)
				{
					throw SemanticError("a case label must be a constant expression");
				}
				sem_.AddChild(built, SemExpr(child, scope).node);
				continue;
			}
			sem_.AddChild(built, SemSlotStatement(child, -1));
		}
		return built;
	}
	if(tag == "break-statement")
	{
		if(loop_depth_ == 0 && switch_depth_ == 0)
		{
			throw SemanticError("`break` outside a loop or switch");
		}
		return sem_.Add("break-statement");
	}
	if(tag == "continue-statement")
	{
		if(loop_depth_ == 0)
		{
			throw SemanticError("`continue` outside a loop");
		}
		return sem_.Add("continue-statement");
	}
	if(tag == "enum-specifier" || tag == "class-specifier" ||
	   tag == "class-forward-declaration" || tag == "bit-field-declaration" ||
	   tag == "special-member-declaration" || tag == "special-member-definition" ||
	   tag == "access-specifier")
	{
		// A declaration that binds nothing a later line needs still occupies
		// its own line in a block.
		const int wrapper = sem_.Add("simple-declaration");
		if(tag == "class-specifier")
		{
			const int built = SemAnonymousUnionStorage(node, scope);
			if(built >= 0)
			{
				sem_.AddChild(wrapper, built);
			}
		}
		return wrapper;
	}
	if(tag == "using-declaration" || tag == "using-directive" ||
	   tag == "empty-declaration" || tag == "static-assert-declaration" ||
	   tag == "namespace-alias-definition")
	{
		return -1;
	}
	throw SemanticError("statement form `" + tag + "` is outside the supported subset");
}

int Analyzer::SemForInit(int node, int scope)
{
	const int built = sem_.Add("for-init-statement");
	const int child = ChildAt(node, 0);
	if(child >= 0)
	{
		sem_.AddChild(built, SemStatement(child, scope));
	}
	return built;
}

// 6.4 [stmt.select]: a condition is an expression or a declaration, and a
// declaration form takes an initializer.
int Analyzer::SemCondition(int node, int scope, bool switch_context)
{
	const int built = sem_.Add("condition");
	const int child = ChildAt(node, 0);
	if(child < 0)
	{
		return built;
	}
	if(!IsTag(child, "condition-declaration"))
	{
		const Resolved value = SemExpr(child, scope);
		if(!IsScalarType(ReferredType(value.type)))
		{
			throw SemanticError("a condition must be a scalar");
		}
		if(!switch_context && IsScopedEnum(ReferredType(value.type)))
		{
			throw SemanticError("a scoped enumeration is not a condition");
		}
		if(switch_context && !IsIntegralType(ReferredType(value.type)))
		{
			throw SemanticError("a switch condition must be integral or an enumeration");
		}
		sem_.AddChild(built, value.node);
		return built;
	}
	const int declaration = sem_.Add("condition-declaration");
	const int declarator = FindChild(child, "declarator");
	const int initializer = FindChild(child, "initializer");
	const Model::DeclarationFact* fact = model_.DeclarationAt(declarator);
	string name;
	CollectDeclaratorName(declarator, name);
	if(fact == 0)
	{
		throw SemanticError("unsupported condition declaration");
	}
	const int variable = sem_.Add("variable", name, true);
	sem_.SetType(variable, Spell(fact->type));
	int declared = fact->type;
	const int built_initializer = SemInitializer(initializer, scope, declared, name,
	                                             fact->entity, false);
	if(built_initializer >= 0)
	{
		sem_.AddChild(variable, built_initializer);
	}
	sem_.AddChild(declaration, variable);
	sem_.AddChild(built, declaration);
	if(switch_context)
	{
		if(!IsIntegralType(ReferredType(fact->type)))
		{
			throw SemanticError("a switch condition must be integral or an enumeration");
		}
	}
	else if(IsScopedEnum(ReferredType(fact->type)))
	{
		throw SemanticError("a scoped enumeration is not a condition");
	}
	return built;
}

}  // namespace semantic
}  // namespace cppgm
