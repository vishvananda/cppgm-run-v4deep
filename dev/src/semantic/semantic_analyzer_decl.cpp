// Declarations, specifiers, declarators, classes and enumerations.
//
// The type a declaration gives its name is composed from two halves: the
// decl-specifier-seq says what the base type is, and the declarator says how
// the name is reached from it.  `scopes-and-types.md` asks for the declarator's
// structure to be traversed rather than its token sequence flattened, which is
// what keeps `int *f(int)` and `int (*f)(int)` apart.

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

// The keyword or punctuator a terminal label spells, or an empty string when
// the label names something the semantic layer must resolve.  A one-token name
// is spelled with its token kind - `TT_IDENTIFIER:Y` - so the kind prefix alone
// does not make a label a keyword.
string KeywordWord(const string& label)
{
	if(label.compare(0, 3, "KW_") == 0 || label.compare(0, 3, "OP_") == 0)
	{
		const size_t colon = label.find(':');
		return colon == string::npos ? string() : label.substr(colon + 1);
	}
	return string();
}

// Whether a declarator carries the `...` that ends its own parameter list.
// A parameter clause nested in it belongs to a function type the parameter has,
// so its own pack marker is not this one (8.3.5/3).
bool HasParameterPack(const syntax::SyntaxArena& arena, int node)
{
	if(node < 0)
	{
		return false;
	}
	const string& tag = arena.Text(arena.Node(node).tag);
	if(tag == "parameter-pack")
	{
		return true;
	}
	if(tag == "parameter-clause")
	{
		return false;
	}
	std::vector<int> children;
	arena.CollectChildren(node, children);
	for(size_t index = 0; index < children.size(); ++index)
	{
		if(HasParameterPack(arena, children[index]))
		{
			return true;
		}
	}
	return false;
}

// The name a label spells, which is the text after `TT_IDENTIFIER:` when the
// name was written as one token.
string NameText(const string& label)
{
	if(label.compare(0, 14, "TT_IDENTIFIER:") == 0)
	{
		return label.substr(14);
	}
	return label;
}

string AfterColon(const string& text)
{
	const size_t colon = text.find(':');
	return colon == string::npos ? string() : text.substr(colon + 1);
}

// Whether a decl-specifier word names a type, which is what makes a class or
// enum specifier beside it the declaration's type or an intermediate one.
bool IsSimpleTypeWord(const string& word)
{
	return word == "void" || word == "bool" || word == "char" || word == "wchar_t" ||
	       word == "char16_t" || word == "char32_t" || word == "int" || word == "short" ||
	       word == "long" || word == "signed" || word == "unsigned" || word == "float" ||
	       word == "double" || word == "auto";
}

// A class-key terminal spells `KW_STRUCT:struct`.
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

}  // namespace

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

// A declarator-id may be qualified - `void n::f(T)` - so the name it declares
// is the last component and everything before it is the scope it lands in.
void Analyzer::SplitQualifiedName(const string& text, string& qualifier, string& name)
{
	int depth = 0;
	size_t split = string::npos;
	for(size_t index = 0; index + 1 < text.size(); ++index)
	{
		const char c = text[index];
		if(c == '<')
		{
			++depth;
		}
		else if(c == '>')
		{
			--depth;
		}
		else if(c == ':' && text[index + 1] == ':' && depth == 0)
		{
			split = index;
			++index;
		}
	}
	if(split == string::npos)
	{
		qualifier.clear();
		name = text;
		return;
	}
	qualifier = text.substr(0, split + 2);
	name = text.substr(split + 2);
}

void Analyzer::CollectDeclaratorName(int node, string& name)
{
	if(node < 0)
	{
		return;
	}
	const vector<int> children = ChildrenOf(node);
	for(size_t index = 0; index < children.size(); ++index)
	{
		const int child = children[index];
		const string& tag = Tag(child);
		if(tag == "identifier")
		{
			name = Label(child);
			return;
		}
		if(tag == "nested-declarator")
		{
			CollectDeclaratorName(ChildAt(child, 0), name);
			if(!name.empty())
			{
				return;
			}
		}
	}
}

string Analyzer::AnonymousClassName(int node)
{
	ostringstream out;
	out << "__anonymous_union_type__" << arena_.Start(node) << '_' << arena_.End(node);
	return out.str();
}

string Analyzer::AnonymousEnumName()
{
	++anonymous_enums_;
	ostringstream out;
	out << "__anonymous_enum" << anonymous_enums_;
	return out.str();
}

// ---------------------------------------------------------------------------
// Decl-specifier sequences
// ---------------------------------------------------------------------------

int Analyzer::FundamentalFromSpecifiers(const vector<string>& words) const
{
	bool unsigned_seen = false;
	bool signed_seen = false;
	bool short_seen = false;
	int longs = 0;
	bool has_char = false;
	for(size_t index = 0; index < words.size(); ++index)
	{
		const string& word = words[index];
		if(word == "unsigned")
		{
			unsigned_seen = true;
		}
		else if(word == "signed")
		{
			signed_seen = true;
		}
		else if(word == "short")
		{
			short_seen = true;
		}
		else if(word == "long")
		{
			++longs;
		}
		else if(word == "char")
		{
			has_char = true;
		}
		else if(word == "void")
		{
			return posttoken::FT_VOID;
		}
		else if(word == "bool")
		{
			return posttoken::FT_BOOL;
		}
		else if(word == "wchar_t")
		{
			return posttoken::FT_WCHAR_T;
		}
		else if(word == "char16_t")
		{
			return posttoken::FT_CHAR16_T;
		}
		else if(word == "char32_t")
		{
			return posttoken::FT_CHAR32_T;
		}
		else if(word == "float")
		{
			return posttoken::FT_FLOAT;
		}
		else if(word == "double")
		{
			return longs > 0 ? posttoken::FT_LONG_DOUBLE : posttoken::FT_DOUBLE;
		}
	}
	if(has_char)
	{
		if(unsigned_seen)
		{
			return posttoken::FT_UNSIGNED_CHAR;
		}
		return signed_seen ? posttoken::FT_SIGNED_CHAR : posttoken::FT_CHAR;
	}
	if(longs >= 2)
	{
		return unsigned_seen ? posttoken::FT_UNSIGNED_LONG_LONG_INT
		                     : posttoken::FT_LONG_LONG_INT;
	}
	if(longs == 1)
	{
		return unsigned_seen ? posttoken::FT_UNSIGNED_LONG_INT : posttoken::FT_LONG_INT;
	}
	if(short_seen)
	{
		return unsigned_seen ? posttoken::FT_UNSIGNED_SHORT_INT : posttoken::FT_SHORT_INT;
	}
	return unsigned_seen ? posttoken::FT_UNSIGNED_INT : posttoken::FT_INT;
}

void Analyzer::AnalyzeSpecifiers(int node, int scope, int enclosing_class, Specifiers& out,
                                 const string& declared_name, bool declare_introduced)
{
	vector<string> words;
	int quals = 0;
	int named_type = -1;
	const vector<int> children = ChildrenOf(node);
	// The class or enum a declaration introduces is named by the declaration's
	// first declarator only when it is the type that declaration gives the
	// name: `typedef class { int c; } C;` names the class C, while a specifier
	// a later type specifier overrides keeps its anonymous name.
	size_t last_type = static_cast<size_t>(-1);
	for(size_t index = 0; index < children.size(); ++index)
	{
		const int child = children[index];
		const string& tag = Tag(child);
		if(tag == "class-specifier" || tag == "enum-specifier" ||
		   tag == "decltype-specifier" || tag == "class-forward-declaration" ||
		   tag == "type-name")
		{
			last_type = index;
			continue;
		}
		if(tag == "decl-specifier" || tag == "type-specifier" || tag == "cv-qualifier")
		{
			const string word = KeywordWord(Label(child));
			if((word.empty() && NameText(Label(child)).compare(0, 8, "decltype") == 0) ||
			   IsSimpleTypeWord(word))
			{
				last_type = index;
			}
		}
	}
	for(size_t index = 0; index < children.size(); ++index)
	{
		const int child = children[index];
		const string& tag = Tag(child);
		const string& label = Label(child);
		if(tag == "decl-specifier" || tag == "type-specifier" || tag == "cv-qualifier" ||
		   tag == "type-name")
		{
			const string word = KeywordWord(label);
			if(word.empty())
			{
				// A name, or a `decltype(...)` the specifier keeps whole.
				if(NameText(label).compare(0, 8, "decltype") == 0)
				{
					named_type = EvaluateDecltype(child, scope);
				}
				else
				{
					named_type = ResolveTypeName(scope, NameText(label), false, false);
				}
				out.saw_type = true;
				continue;
			}
			if(word == "const")
			{
				quals |= 1;
			}
			else if(word == "volatile")
			{
				quals |= 2;
			}
			else if(word == "typedef")
			{
				out.is_typedef = true;
			}
			else if(word == "extern")
			{
				out.is_extern = true;
			}
			else if(word == "static")
			{
				out.is_static = true;
			}
			else if(word == "inline")
			{
				out.is_inline = true;
			}
			else if(word == "virtual")
			{
				out.is_virtual = true;
			}
			else if(word == "constexpr")
			{
				out.is_constexpr = true;
			}
			else if(word == "thread_local")
			{
				out.is_thread_local = true;
			}
			else if(word == "friend")
			{
				out.is_friend = true;
			}
			else if(word == "mutable")
			{
				out.is_mutable = true;
			}
			else if(word == "explicit")
			{
			}
			else
			{
				words.push_back(word);
			}
			continue;
		}
		if(tag == "decltype-specifier")
		{
			named_type = EvaluateDecltype(child, scope);
			out.saw_type = true;
			continue;
		}
		if(tag == "class-forward-declaration")
		{
			// An elaborated class specifier names a type without declaring it,
			// and may find one an ordinary-name binding hides (3.4.4/2).
			const int key_node = FindChild(child, "class-key");
			const int key = ClassKeyOf(AfterColon(Label(key_node)));
			const string& written = Label(child);
			int entity = model_.LookupType(scope, written);
			if(entity < 0 || model_.EntityOf(entity).kind != kEntityClass)
			{
				entity = DeclareClass(scope, written, key, false);
			}
			named_type = model_.EntityOf(entity).type;
			out.saw_type = true;
			continue;
		}
		if(tag == "class-specifier")
		{
			int key = kClassKeyClass;
			named_type = AnalyzeClassSpecifier(child, scope, enclosing_class,
			                                   index == last_type ? declared_name : string(),
			                                   out.is_static, &key);
			out.class_specifier = child;
			out.class_key = key;
			out.saw_type = true;
			continue;
		}
		if(tag == "enum-specifier")
		{
			int key = kEnumKeyPlain;
			const bool has_body = arena_.Literal(child) == 1;
			named_type = AnalyzeEnumSpecifier(child, scope, declare_introduced || has_body,
			                                  index == last_type ? declared_name : string(),
			                                  &key);
			out.enum_specifier = child;
			out.enum_key = key;
			out.saw_type = true;
			continue;
		}
	}
	if(!words.empty())
	{
		named_type = model_.Fundamental(FundamentalFromSpecifiers(words));
		out.saw_type = true;
	}
	if(named_type >= 0)
	{
		out.type = model_.Qualified(quals, named_type);
	}
}

// ---------------------------------------------------------------------------
// Declarators
// ---------------------------------------------------------------------------

int Analyzer::BuildDeclarator(int node, int base, int scope)
{
	if(node < 0)
	{
		return base;
	}
	const string& tag = Tag(node);
	if(tag == "type-id")
	{
		Specifiers spec;
		AnalyzeSpecifiers(ChildAt(node, 0), scope, -1, spec, string(), false);
		const int abstract = ChildAt(node, 1);
		return abstract < 0 ? spec.type : BuildDeclarator(abstract, spec.type, scope);
	}
	if(tag == "decltype-specifier")
	{
		return EvaluateDecltype(node, scope);
	}
	if(tag == "nested-declarator")
	{
		return BuildDeclarator(ChildAt(node, 0), base, scope);
	}

	// The declarator is a chain: its pointer operators, then its suffixes, are
	// applied to the base, and a parenthesized inner declarator is applied to
	// that result (8.3/1).  The suffixes run innermost-last, so `int a[2][3]`
	// is an array of two arrays of three.
	vector<int> operators;
	vector<int> operator_quals;
	vector<int> suffixes;
	int nested = -1;
	const vector<int> children = ChildrenOf(node);
	for(size_t index = 0; index < children.size(); ++index)
	{
		const int child = children[index];
		const string& child_tag = Tag(child);
		if(child_tag == "ptr-operator")
		{
			const string& label = Label(child);
			const string spelling = label.compare(0, 3, "OP_") == 0
			    ? label.substr(label.find(':') + 1) : label;
			int kind = kTypePointer;
			if(spelling == "&")
			{
				kind = kTypeLvalueReference;
			}
			else if(spelling == "&&")
			{
				kind = kTypeRvalueReference;
			}
			operators.push_back(kind);
			operator_quals.push_back(0);
			continue;
		}
		if(child_tag == "cv-qualifier")
		{
			if(!operator_quals.empty())
			{
				const string word = AfterColon(Label(child));
				operator_quals[operator_quals.size() - 1] |= word == "const" ? 1 : 2;
			}
			continue;
		}
		if(child_tag == "array-suffix" || child_tag == "parameter-clause")
		{
			suffixes.push_back(child);
			continue;
		}
		if(child_tag == "nested-declarator")
		{
			nested = child;
		}
	}

	int type = base;
	for(size_t index = 0; index < operators.size(); ++index)
	{
		if(operators[index] == kTypePointer)
		{
			if(model_.Get(type).kind == kTypeLvalueReference ||
			   model_.Get(type).kind == kTypeRvalueReference)
			{
				throw SemanticError("cannot form a pointer to a reference");
			}
			type = model_.Pointer(type);
		}
		else if(operators[index] == kTypeLvalueReference)
		{
			type = model_.LvalueReference(type);
		}
		else
		{
			type = model_.RvalueReference(type);
		}
		type = model_.Qualified(operator_quals[index], type);
	}
	for(size_t index = suffixes.size(); index > 0; --index)
	{
		type = BuildSuffix(suffixes[index - 1], type, scope);
	}
	if(nested >= 0)
	{
		return BuildDeclarator(ChildAt(nested, 0), type, scope);
	}
	return type;
}

int Analyzer::BuildSuffix(int node, int base, int scope)
{
	if(Tag(node) == "array-suffix")
	{
		const int bound_node = ChildAt(node, 0);
		if(bound_node < 0)
		{
			return model_.Array(kUnknownBound, base);
		}
		return model_.Array(ArrayBound(bound_node, scope), base);
	}
	vector<int> params;
	bool varargs = false;
	BuildParameterClause(node, scope, params, varargs, 0);
	return model_.Function(base, params, varargs);
}

int Analyzer::BuildParameterClause(int node, int scope, vector<int>& params, bool& varargs,
                                   vector<pair<string, int> >* names)
{
	if(node < 0)
	{
		return 0;
	}
	const vector<int> children = ChildrenOf(node);
	for(size_t index = 0; index < children.size(); ++index)
	{
		const int child = children[index];
		const string& tag = Tag(child);
		if(tag == "parameter-pack")
		{
			varargs = true;
			continue;
		}
		if(tag != "parameter-declaration")
		{
			continue;
		}
		if(FindChild(child, "ellipsis") >= 0)
		{
			varargs = true;
			continue;
		}
		// `const char*...` writes the `...` in the last parameter's declarator,
		// so the parameter stays and the list ends there.
		const bool ends_list = HasParameterPack(arena_, child);
		const int seq = FindChild(child, "decl-specifier-seq");
		Specifiers spec;
		if(seq >= 0)
		{
			AnalyzeSpecifiers(seq, scope, -1, spec, string(), false);
		}
		// An unnamed parameter's declarator prints as `abstract-declarator` when
		// it is a suffix form, and it must not silently become the base type.
		int declarator = FindChild(child, "declarator");
		if(declarator < 0)
		{
			declarator = FindChild(child, "abstract-declarator");
		}
		const int type = declarator < 0 ? spec.type : BuildDeclarator(declarator, spec.type, scope);
		string name;
		CollectDeclaratorName(declarator, name);
		params.push_back(type);
		if(names != 0)
		{
			names->push_back(make_pair(name, type));
		}
		if(ends_list)
		{
			varargs = true;
		}
	}
	// 8.3.5/4: a sole unnamed parameter of type void is the empty list.
	if(params.size() == 1 && model_.Get(params[0]).kind == kTypeFundamental &&
	   model_.Get(params[0]).base == posttoken::FT_VOID)
	{
		params.clear();
		if(names != 0)
		{
			names->clear();
		}
	}
	return 0;
}

}  // namespace semantic
}  // namespace cppgm
