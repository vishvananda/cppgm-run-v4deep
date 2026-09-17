// The deterministic scope/type dump.
//
// One line per declaration, then one line per child scope, indented two spaces
// per level.  The dump is a source-facing view over the shared graph: a `type`
// line prints the class or enum key its own declaration wrote, while a type
// used inside a compound spelling prints its canonical name, so
// `struct C; class C {};` prints two `type C` lines and one class scope.

#include <ostream>
#include <string>

#include "semantic/semantic_model.h"

using namespace std;

namespace cppgm
{
namespace semantic
{

namespace
{

const char* ClassKeyName(int key)
{
	switch(key)
	{
	case kClassKeyStruct:
		return "struct";
	case kClassKeyUnion:
		return "union";
	default:
		return "class";
	}
}

const char* EnumKeyName(int key)
{
	switch(key)
	{
	case kEnumKeyClass:
		return "enum class";
	case kEnumKeyStruct:
		return "enum struct";
	default:
		return "enum";
	}
}

void Indent(ostream& out, int depth)
{
	for(int index = 0; index < depth; ++index)
	{
		out << "  ";
	}
}

// What a `type` line prints for its type: the key this declaration wrote with
// the name it bound, or the type's own canonical spelling when the declaration
// introduced a template parameter rather than a class or enumeration.
string TypeSpelling(const Model& model, const Binding& binding)
{
	if(binding.class_key >= 0)
	{
		return string(ClassKeyName(binding.class_key)) + " " + binding.name;
	}
	if(binding.enum_key >= 0)
	{
		return string(EnumKeyName(binding.enum_key)) + " " + binding.name;
	}
	return model.Spelling(binding.type);
}

void WriteBinding(ostream& out, const Model& model, const Binding& binding, int depth)
{
	Indent(out, depth);
	switch(binding.kind)
	{
	case kBindingType:
		out << "type " << binding.name << ' ' << TypeSpelling(model, binding) << '\n';
		break;
	case kBindingTypeAlias:
		out << "type-alias " << binding.name << ' ' << model.Spelling(binding.type) << '\n';
		break;
	case kBindingEnumerator:
		out << "enumerator " << binding.name << ' ' << model.Spelling(binding.type) << ' '
		    << binding.value << '\n';
		break;
	case kBindingFunction:
		out << "function " << binding.name << ' ' << model.Spelling(binding.type) << '\n';
		break;
	case kBindingParameter:
		out << "parameter " << binding.name << ' ' << model.Spelling(binding.type) << '\n';
		break;
	case kBindingVariable:
		out << "variable " << binding.name << ' '
		    << model.Spelling(model.EntityOf(binding.entity).type) << '\n';
		break;
	}
}

string ScopeSpelling(const Scope& scope)
{
	switch(scope.kind)
	{
	case kScopeNamespace:
		return "scope namespace " + scope.name;
	case kScopeTemplateParameters:
		return "scope template-parameters";
	case kScopeClass:
		return "scope class " + scope.name;
	case kScopeEnum:
		return "scope enum " + scope.name;
	case kScopeFunction:
		return "scope function " + scope.name;
	case kScopeBlock:
		return "scope block";
	}
	return "scope";
}

void WriteScope(ostream& out, const Model& model, int scope, int depth)
{
	Indent(out, depth);
	out << ScopeSpelling(model.ScopeOf(scope)) << '\n';
	const Scope& record = model.ScopeOf(scope);
	for(size_t index = 0; index < record.bindings.size(); ++index)
	{
		WriteBinding(out, model, record.bindings[index], depth + 1);
	}
	for(size_t index = 0; index < record.children.size(); ++index)
	{
		WriteScope(out, model, record.children[index], depth + 1);
	}
}

}  // namespace

void WriteScopeTree(ostream& out, const Model& model)
{
	out << "translation-unit\n";
	WriteScope(out, model, model.GlobalScope(), 1);
}

}  // namespace semantic
}  // namespace cppgm
