// The PA7 function-template slice: declaring a template, naming one with a
// template-id, deducing its arguments from a call, and substituting.
//
// The stage's *Required Features* stop at non-template calls, so nothing here
// is a general template engine: a template is a function template whose
// parameters are all type parameters, an argument is a type, and deduction
// (14.8.2) walks only the shapes the PA7 slice can build - a bare parameter, a
// pointer, a reference and a function type.  What the layer does keep apart is
// what later assignments need apart: the *declaration* of the template, the
// *substitution* that turns a parameter list into a type, and the
// *instantiation* that declares the function the substituted type names.  A
// class template, a non-type parameter, a template-template parameter and a
// partial specialization are all absent rather than approximated.
//
// Nothing here runs during the PA6 analysis: the registry is filled while the
// unit is analysed, but a specialization is only created when the PA7 walk
// demands one, so `--emit-types` is unchanged.

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

// The word a simple-type-specifier may be written with, which is how a
// template argument written as `int` is read.
bool SimpleTypeWord(const string& word)
{
	return word == "void" || word == "bool" || word == "char" || word == "wchar_t" ||
	       word == "char16_t" || word == "char32_t" || word == "int" || word == "short" ||
	       word == "long" || word == "signed" || word == "unsigned" || word == "float" ||
	       word == "double";
}

string Trim(const string& text)
{
	size_t begin = 0;
	size_t end = text.size();
	while(begin < end && (text[begin] == ' ' || text[begin] == '\t'))
	{
		++begin;
	}
	while(end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t'))
	{
		--end;
	}
	return text.substr(begin, end - begin);
}

}  // namespace

// 14.1/2: the name a template declares belongs to the enclosing scope, so this
// records the template there rather than in the template-parameter scope the
// declaration itself was analysed in.
void Analyzer::NoteFunctionTemplate(int scope, const string& name,
                                    const vector<int>& parameters, int entity, int type,
                                    int declarator)
{
	if(name.empty() || parameters.empty())
	{
		return;
	}
	FunctionTemplate record;
	record.scope = scope;
	record.name = name;
	record.parameters = parameters;
	record.entity = entity;
	record.type = type;
	record.declarator = declarator;
	templates_.push_back(record);
}

// The templates a name denotes, following ordinary unqualified lookup: the
// nearest scope that declares the name answers, and an outer one is not
// consulted.  A qualified name resolves the qualifier first.
bool Analyzer::FindFunctionTemplates(int scope, const string& name,
                                     vector<int>& out) const
{
	string qualifier;
	string plain;
	SplitQualifiedName(name, qualifier, plain);
	if(!qualifier.empty())
	{
		const int target = model_.ResolveQualifier(scope, qualifier);
		for(size_t index = 0; index < templates_.size(); ++index)
		{
			if(templates_[index].scope == target && templates_[index].name == plain)
			{
				out.push_back(static_cast<int>(index));
			}
		}
		return !out.empty();
	}
	for(int current = scope; current >= 0; current = model_.ScopeOf(current).parent)
	{
		for(size_t index = 0; index < templates_.size(); ++index)
		{
			if(templates_[index].scope == current && templates_[index].name == plain)
			{
				out.push_back(static_cast<int>(index));
			}
		}
		if(!out.empty())
		{
			return true;
		}
	}
	return false;
}

// 14.5.6.1 with 14.3: an argument substituted for a template parameter reaches
// every place the parameter appears, so the substitution rebuilds the type
// through the same formers that built it.
int Analyzer::SubstituteType(int type, const map<int, int>& bindings) const
{
	if(type < 0)
	{
		return -1;
	}
	// The type graph grows while the substitution builds types, so the
	// record is a copy: a reference into it would not survive the next former.
	const Type record = model_.Get(type);
	switch(record.kind)
	{
	case kTypeTemplateParameter:
	{
		const map<int, int>::const_iterator found = bindings.find(type);
		return found == bindings.end() ? type : found->second;
	}
	case kTypeCv:
		return model_.Qualified(record.quals, SubstituteType(record.base, bindings));
	case kTypePointer:
		return model_.Pointer(SubstituteType(record.base, bindings));
	case kTypeLvalueReference:
		return model_.LvalueReference(SubstituteType(record.base, bindings));
	case kTypeRvalueReference:
		return model_.RvalueReference(SubstituteType(record.base, bindings));
	case kTypeArray:
		return model_.Array(record.bound, SubstituteType(record.base, bindings));
	case kTypeMemberPointer:
		return model_.MemberPointer(SubstituteType(record.base, bindings),
		                            SubstituteType(record.member, bindings));
	case kTypeFunction:
	{
		vector<int> params;
		for(size_t index = 0; index < record.params.size(); ++index)
		{
			params.push_back(SubstituteType(record.params[index], bindings));
		}
		return model_.Function(SubstituteType(record.base, bindings), params,
		                       record.varargs, record.quals, record.func_ref);
	}
	default:
		return type;
	}
}

// 14.8.2.1: deduction matches the declared parameter type against the type of
// the argument.  A parameter that is itself a template parameter takes the
// argument; a pointer, reference or function type deduces through the same
// former; anything else is a non-deduced context and must already match.
bool Analyzer::DeduceArguments(int declared, int actual, map<int, int>& bindings) const
{
	if(declared < 0 || actual < 0)
	{
		return false;
	}
	const Type pattern = model_.Get(declared);
	if(pattern.kind == kTypeTemplateParameter)
	{
		// 14.8.2.1: a by-value parameter takes the argument's unqualified type,
		// so a top-level cv on the argument is not what the parameter deduces.
		int bare = actual;
		if(model_.Get(bare).kind == kTypeCv)
		{
			bare = model_.Get(bare).base;
		}
		const map<int, int>::const_iterator found = bindings.find(declared);
		if(found != bindings.end())
		{
			return model_.Same(found->second, bare);
		}
		bindings[declared] = bare;
		return true;
	}
	if(pattern.kind == kTypeCv)
	{
		// 14.8.2.1: the top-level cv of the argument is ignored where the
		// parameter is cv-qualified, because the qualification is what the
		// parameter adds rather than what it deduces.
		int bare = actual;
		if(model_.Get(bare).kind == kTypeCv)
		{
			bare = model_.Get(bare).base;
		}
		return DeduceArguments(pattern.base, bare, bindings);
	}
	if(pattern.kind == kTypePointer)
	{
		int target = actual;
		// 14.8.2.1: an array or function argument decays to a pointer before
		// the pointer form is matched.
		if(model_.Get(target).kind == kTypeArray)
		{
			target = model_.Pointer(model_.Get(target).base);
		}
		else if(model_.Get(target).kind == kTypeFunction)
		{
			target = model_.Pointer(target);
		}
		if(model_.Get(target).kind != kTypePointer)
		{
			return false;
		}
		return DeduceArguments(pattern.base, model_.Get(target).base, bindings);
	}
	if(pattern.kind == kTypeLvalueReference || pattern.kind == kTypeRvalueReference)
	{
		// 14.8.2.1: a reference parameter deduces against the type it refers
		// to, so whether the argument is an lvalue is 8.5.3's question and not
		// a deduction question.  The argument's cv stays, because a reference
		// parameter that does not write it deduces it.
		return DeduceArguments(pattern.base, ReferredType(actual), bindings);
	}
	if(pattern.kind == kTypeFunction)
	{
		const int target = ReferredType(actual);
		if(model_.Get(target).kind != kTypeFunction)
		{
			return false;
		}
		const Type function = model_.Get(target);
		if(function.params.size() != pattern.params.size())
		{
			return false;
		}
		for(size_t index = 0; index < pattern.params.size(); ++index)
		{
			if(!DeduceArguments(pattern.params[index], function.params[index], bindings))
			{
				return false;
			}
		}
		return DeduceArguments(pattern.base, function.base, bindings);
	}
	return model_.Same(model_.AdjustParameter(declared), model_.AdjustParameter(actual));
}

// 14.2: a simple-template-id is a name followed by an argument list.  The
// parser keeps a template-id as one token range, so the name and the arguments
// are read back out of the range the source wrote.
bool Analyzer::SplitTemplateId(const string& text, string& name,
                               vector<string>& arguments) const
{
	const size_t open = text.find('<');
	if(open == string::npos || open == 0 || text.empty() || text[text.size() - 1] != '>')
	{
		return false;
	}
	name = Trim(text.substr(0, open));
	const string inner = text.substr(open + 1, text.size() - open - 2);
	if(Trim(inner).empty())
	{
		return !name.empty();
	}
	// A comma inside a nested angle list, a parameter list or a subscript
	// separates an argument from the type that writes it, not two arguments.
	int depth = 0;
	size_t start = 0;
	for(size_t index = 0; index < inner.size(); ++index)
	{
		const char c = inner[index];
		if(c == '<' || c == '(' || c == '[')
		{
			++depth;
		}
		else if(c == '>' || c == ')' || c == ']')
		{
			--depth;
		}
		else if(c == ',' && depth == 0)
		{
			arguments.push_back(Trim(inner.substr(start, index - start)));
			start = index + 1;
		}
	}
	arguments.push_back(Trim(inner.substr(start)));
	return !name.empty();
}

// The type a written template argument names.  PA7's slice has only type
// parameters, so an argument is a type: a name, or a fundamental type written
// with one or more keywords.
int Analyzer::ResolveTemplateArgument(int scope, const string& text)
{
	const string trimmed = Trim(text);
	if(trimmed.empty())
	{
		throw SemanticError("empty template argument");
	}
	string qualifier;
	string name;
	SplitQualifiedName(trimmed, qualifier, name);
	const int entity = qualifier.empty()
	    ? model_.LookupTypeUnqualified(scope, name)
	    : model_.LookupTypeIn(model_.ResolveQualifier(scope, qualifier), name);
	if(entity >= 0)
	{
		return model_.EntityOf(entity).type;
	}
	vector<string> words;
	size_t position = 0;
	while(position <= trimmed.size())
	{
		const size_t space = trimmed.find(' ', position);
		const string part = trimmed.substr(position, space == string::npos
		                                   ? string::npos : space - position);
		if(!SimpleTypeWord(part))
		{
			words.clear();
			break;
		}
		words.push_back(part);
		if(space == string::npos)
		{
			break;
		}
		position = space + 1;
	}
	if(!words.empty())
	{
		return model_.Fundamental(FundamentalFromSpecifiers(words));
	}
	throw SemanticError("unsupported template argument `" + trimmed + "`");
}

// 14.3/1 with 14.8.1: the substitution turns the template's own type into the
// type of one specialization, which is the function the instantiation
// declares.  Two demands for the same specialization are one function.
int Analyzer::InstantiateFunctionTemplate(int which, const map<int, int>& bindings)
{
	const FunctionTemplate& record = templates_[static_cast<size_t>(which)];
	const int type = SubstituteType(record.type, bindings);
	const pair<int, int> key(which, type);
	const map<pair<int, int>, int>::const_iterator found = specializations_.find(key);
	if(found != specializations_.end())
	{
		return found->second;
	}
	const int entity = model_.NewEntity(kEntityFunction, record.name);
	model_.EntityOf(entity).type = type;
	model_.EntityOf(entity).decl_scope = record.scope;
	model_.EntityOf(entity).body = -1;
	specializations_[key] = entity;
	Instantiation note;
	note.which = which;
	note.entity = entity;
	instantiations_.push_back(note);
	return entity;
}

// A template-id written where a value belongs denotes the specialization its
// argument list names, which is a function the call layer and `&` both see as
// an ordinary function lvalue.
//
// 14.2/4: the argument list matches the template parameter list, so two
// templates of one name that share a parameter list both produce a
// specialization.  Which one the template-id denotes is then 13.4's question,
// answered by the target type of the address - the same rule that tells two
// member overloads apart - and the answer has to exist, because a name that
// denotes several functions has no type of its own.
Analyzer::Resolved Analyzer::SemTemplateId(int node, int scope, const string& name,
                                           const vector<string>& arguments, int target)
{
	Resolved result;
	vector<int> found;
	if(!FindFunctionTemplates(scope, name, found))
	{
		return result;
	}
	// A candidate is only *substituted* until the choice is made: 14.7.1
	// instantiates the specialization the program uses, and the dump prints
	// exactly the instantiations the unit demanded.
	struct Substituted
	{
		int which;
		std::map<int, int> bindings;
		int type;
	};
	vector<Substituted> matches;
	for(size_t index = 0; index < found.size(); ++index)
	{
		const FunctionTemplate& record = templates_[static_cast<size_t>(found[index])];
		if(record.parameters.size() != arguments.size() ||
		   model_.Get(record.type).kind != kTypeFunction)
		{
			continue;
		}
		Substituted match;
		match.which = found[index];
		bool deduced = true;
		for(size_t argument = 0; argument < arguments.size(); ++argument)
		{
			const int type = ResolveTemplateArgument(scope, arguments[argument]);
			if(!DeduceArguments(record.parameters[argument], type, match.bindings))
			{
				deduced = false;
				break;
			}
		}
		if(!deduced)
		{
			continue;
		}
		match.type = SubstituteType(record.type, match.bindings);
		matches.push_back(match);
	}
	if(matches.empty())
	{
		throw SemanticError("no matching function template `" + name + "`");
	}
	int chosen = 0;
	if(matches.size() > 1)
	{
		chosen = -1;
		const int wanted = target >= 0 && model_.Get(target).kind == kTypePointer
		    ? model_.Get(target).base
		    : target;
		for(size_t index = 0; index < matches.size(); ++index)
		{
			if(model_.Same(model_.AdjustFunction(matches[index].type), wanted))
			{
				chosen = static_cast<int>(index);
				break;
			}
		}
		if(chosen < 0)
		{
			throw SemanticError("the target type does not name one specialization of `" +
			                    name + "`");
		}
	}
	const int entity = InstantiateFunctionTemplate(matches[static_cast<size_t>(chosen)].which,
	                                               matches[static_cast<size_t>(chosen)].bindings);
	result.entity = entity;
	result.function = entity;
	result.type = model_.EntityOf(entity).type;
	result.category = kLvalue;
	result.node = sem_.Add("id-expression", result.category, Spell(result.type),
	                       Label(node));
	return result;
}

// 14.8.2 with 13.3: a call whose name denotes function templates deduces the
// specialization each template gives for these arguments, and the
// specializations that deduction produced are the candidate set the ordinary
// ranking then chooses from.
Analyzer::Resolved Analyzer::SemTemplateCall(int node, int scope, const string& text,
                                             const vector<int>& found,
                                             const vector<Resolved>& arguments)
{
	vector<Candidate> viable;
	for(size_t index = 0; index < found.size(); ++index)
	{
		const FunctionTemplate& record = templates_[static_cast<size_t>(found[index])];
		const Type declared = model_.Get(record.type);
		if(declared.kind != kTypeFunction || declared.params.size() != arguments.size())
		{
			continue;
		}
		map<int, int> bindings;
		bool deduced = true;
		for(size_t argument = 0; argument < arguments.size(); ++argument)
		{
			if(!DeduceArguments(declared.params[argument], SourceType(arguments[argument]),
			                    bindings))
			{
				deduced = false;
				break;
			}
		}
		if(!deduced)
		{
			continue;
		}
		// 14.3/1: an argument the call did not deduce leaves the substitution
		// without a type for that parameter, so this template is not a
		// candidate for the call.
		bool complete = true;
		for(size_t parameter = 0; parameter < record.parameters.size(); ++parameter)
		{
			if(bindings.find(record.parameters[parameter]) == bindings.end())
			{
				complete = false;
				break;
			}
		}
		if(!complete)
		{
			continue;
		}
		Candidate candidate;
		candidate.which = found[index];
		candidate.bindings = bindings;
		candidate.type = SubstituteType(record.type, bindings);
		candidate.scope = record.scope;
		viable.push_back(candidate);
	}
	if(viable.empty())
	{
		throw SemanticError("no matching function for `" + text + "`");
	}
	return SemNamedCall(node, scope, text, viable, arguments);
}

// 14.2 with 14.3: a call written with an explicit argument list names the
// specialization the arguments give, and a name that denotes several templates
// gives one specialization per matching parameter list.  The call's own
// arguments then rank them, so `take<int>(1)` reaches the template whose
// function takes one parameter even when another `take` shares its parameter
// list.
Analyzer::Resolved Analyzer::SemExplicitTemplateCall(int node, int scope,
                                                     const string& name,
                                                     const vector<string>& arguments,
                                                     const vector<int>& found,
                                                     const vector<Resolved>& call_arguments)
{
	vector<Candidate> candidates;
	for(size_t index = 0; index < found.size(); ++index)
	{
		const FunctionTemplate& record = templates_[static_cast<size_t>(found[index])];
		if(record.parameters.size() != arguments.size() ||
		   model_.Get(record.type).kind != kTypeFunction)
		{
			continue;
		}
		map<int, int> bindings;
		bool deduced = true;
		for(size_t argument = 0; argument < arguments.size(); ++argument)
		{
			const int type = ResolveTemplateArgument(scope, arguments[argument]);
			if(!DeduceArguments(record.parameters[argument], type, bindings))
			{
				deduced = false;
				break;
			}
		}
		if(!deduced)
		{
			continue;
		}
		Candidate candidate;
		candidate.which = found[index];
		candidate.bindings = bindings;
		candidate.type = SubstituteType(record.type, bindings);
		candidate.scope = record.scope;
		candidates.push_back(candidate);
	}
	if(candidates.empty())
	{
		throw SemanticError("no matching function template `" + name + "`");
	}
	return SemNamedCall(node, scope, name, candidates, call_arguments);
}

// 14.7.1: the specialization a template candidate stands for exists once the
// ranking has chosen it, which is what keeps an unused specialization out of
// the dump.
int Analyzer::CandidateEntity(Candidate& candidate)
{
	if(candidate.which < 0)
	{
		return candidate.entity;
	}
	return InstantiateFunctionTemplate(candidate.which, candidate.bindings);
}

// The dump prints an instantiation after the unit's own declarations, as a
// function declaration with the parameters the template's clause wrote.
void Analyzer::SemInstantiations(vector<int>& out)
{
	for(size_t index = 0; index < instantiations_.size(); ++index)
	{
		const FunctionTemplate& record =
		    templates_[static_cast<size_t>(instantiations_[index].which)];
		const int entity = instantiations_[index].entity;
		const int type = model_.EntityOf(entity).type;
		const int declaration = sem_.Add("function-declaration",
		                                 QualifiedEntityName(entity), true);
		sem_.SetType(declaration, BoundSpelling(type, record.scope));
		out.push_back(declaration);
		if(model_.Get(type).kind != kTypeFunction)
		{
			continue;
		}
		// 9.3.1/3: a member function's definition writes the implicit object
		// parameter first, and so does its instantiation.
		if(model_.ScopeOf(record.scope).kind == kScopeClass)
		{
			const int class_entity = model_.ScopeOf(record.scope).entity;
			const int class_type = model_.EntityOf(class_entity).type;
			const int qualified = model_.Qualified(model_.Get(type).quals, class_type);
			const int built = sem_.Add("parameter", "this", true);
			sem_.SetType(built, Spell(model_.Pointer(qualified)));
			sem_.AddChild(declaration, built);
		}
		const int clause = FindChild(record.declarator, "parameter-clause");
		const vector<int> params = model_.Get(type).params;
		size_t slot = 0;
		if(clause >= 0)
		{
			const vector<int> children = ChildrenOf(clause);
			for(size_t child = 0; child < children.size(); ++child)
			{
				const int parameter = children[child];
				if(!IsTag(parameter, "parameter-declaration") ||
				   FindChild(parameter, "ellipsis") >= 0)
				{
					continue;
				}
				string name;
				int inner = FindChild(parameter, "declarator");
				if(inner < 0)
				{
					inner = FindChild(parameter, "abstract-declarator");
				}
				CollectDeclaratorName(inner, name);
				const int built = sem_.Add("parameter", name, true);
				if(slot < params.size())
				{
					// 8.3.5/5: the parameter the declaration takes is the
					// adjusted one, as it is for a written declaration.
					sem_.SetType(built, Spell(model_.AdjustParameter(params[slot])));
				}
				sem_.AddChild(declaration, built);
				++slot;
			}
		}
	}
}

}  // namespace semantic
}  // namespace cppgm
