// The PA5 recursive-descent parser, one function per useful production of
// `pa5.gram`.
//
// The cursor is a position plus the one piece of token state the grammar needs:
// a `>>` whose first `>` a close-angle-bracket has taken, so the second half is
// the current logical token.  Every speculative alternative - a type-id against
// an assignment-expression, a template-id against a relational expression, a
// declaration against an expression statement - takes a mark of the cursor, the
// tree and the name table and rolls all three back when the alternative fails.
//
// The tree is the deliverable, so the parse builds it directly: there is no
// recognition tree to replace and no second pass over spellings.  Names the
// dump spells as text - an id-expression, a type-name, a declarator-id - are
// composed from the token range they cover.

#include "syntax/syntax_parser.h"

#include <cctype>
#include <cstddef>

#include "posttoken/simple_token.h"

using namespace std;

namespace cppgm
{
namespace syntax
{

namespace
{

using posttoken::ETokenType;
using posttoken::SimpleTokenName;

bool IsIdentChar(char c)
{
	return isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

}  // namespace

bool IsClassKeyKind(int kind)
{
	return kind == posttoken::KW_CLASS || kind == posttoken::KW_STRUCT ||
	       kind == posttoken::KW_UNION;
}

bool IsSimpleTypeSpecifierKind(int kind)
{
	switch(kind)
	{
	case posttoken::KW_BOOL:
	case posttoken::KW_CHAR:
	case posttoken::KW_CHAR16_T:
	case posttoken::KW_CHAR32_T:
	case posttoken::KW_DOUBLE:
	case posttoken::KW_FLOAT:
	case posttoken::KW_INT:
	case posttoken::KW_LONG:
	case posttoken::KW_SHORT:
	case posttoken::KW_SIGNED:
	case posttoken::KW_UNSIGNED:
	case posttoken::KW_VOID:
	case posttoken::KW_WCHAR_T:
		return true;
	default:
		return false;
	}
}

bool IsCvQualifierKind(int kind)
{
	return kind == posttoken::KW_CONST || kind == posttoken::KW_VOLATILE;
}

// The keywords `decl-specifier` allows beside a type name.
bool IsStorageSpecifierKind(int kind)
{
	switch(kind)
	{
	case posttoken::KW_TYPEDEF:
	case posttoken::KW_EXTERN:
	case posttoken::KW_STATIC:
	case posttoken::KW_INLINE:
	case posttoken::KW_VIRTUAL:
	case posttoken::KW_CONSTEXPR:
	case posttoken::KW_THREAD_LOCAL:
	case posttoken::KW_AUTO:
	case posttoken::KW_FRIEND:
	case posttoken::KW_EXPLICIT:
	case posttoken::KW_MUTABLE:
		return true;
	default:
		return false;
	}
}

bool IsMemberFunctionSpecifierKind(int kind)
{
	switch(kind)
	{
	case posttoken::KW_INLINE:
	case posttoken::KW_VIRTUAL:
	case posttoken::KW_EXPLICIT:
	case posttoken::KW_CONSTEXPR:
	case posttoken::KW_FRIEND:
	case posttoken::KW_STATIC:
		return true;
	default:
		return false;
	}
}

bool IsOperatorTokenKind(int kind)
{
	switch(kind)
	{
	case posttoken::OP_PLUS:
	case posttoken::OP_MINUS:
	case posttoken::OP_STAR:
	case posttoken::OP_DIV:
	case posttoken::OP_MOD:
	case posttoken::OP_XOR:
	case posttoken::OP_AMP:
	case posttoken::OP_BOR:
	case posttoken::OP_COMPL:
	case posttoken::OP_LNOT:
	case posttoken::OP_ASS:
	case posttoken::OP_LT:
	case posttoken::OP_GT:
	case posttoken::OP_PLUSASS:
	case posttoken::OP_MINUSASS:
	case posttoken::OP_STARASS:
	case posttoken::OP_DIVASS:
	case posttoken::OP_MODASS:
	case posttoken::OP_XORASS:
	case posttoken::OP_BANDASS:
	case posttoken::OP_BORASS:
	case posttoken::OP_LSHIFT:
	case posttoken::OP_RSHIFT:
	case posttoken::OP_LSHIFTASS:
	case posttoken::OP_RSHIFTASS:
	case posttoken::OP_EQ:
	case posttoken::OP_NE:
	case posttoken::OP_LE:
	case posttoken::OP_GE:
	case posttoken::OP_LAND:
	case posttoken::OP_LOR:
	case posttoken::OP_INC:
	case posttoken::OP_DEC:
	case posttoken::OP_COMMA:
	case posttoken::OP_ARROWSTAR:
	case posttoken::OP_ARROW:
	case posttoken::OP_DOTSTAR:
		return true;
	default:
		return false;
	}
}

bool IsAssignmentOperatorKind(int kind)
{
	switch(kind)
	{
	case posttoken::OP_ASS:
	case posttoken::OP_PLUSASS:
	case posttoken::OP_MINUSASS:
	case posttoken::OP_STARASS:
	case posttoken::OP_DIVASS:
	case posttoken::OP_MODASS:
	case posttoken::OP_XORASS:
	case posttoken::OP_BANDASS:
	case posttoken::OP_BORASS:
	case posttoken::OP_LSHIFTASS:
	case posttoken::OP_RSHIFTASS:
		return true;
	default:
		return false;
	}
}

bool IsCastKeywordKind(int kind)
{
	switch(kind)
	{
	case posttoken::KW_DYNAMIC_CAST:
	case posttoken::KW_STATIC_CAST:
	case posttoken::KW_REINTERPET_CAST:
	case posttoken::KW_CONST_CAST:
		return true;
	default:
		return false;
	}
}

bool IsPrefixUnaryOperatorKind(int kind)
{
	switch(kind)
	{
	case posttoken::OP_INC:
	case posttoken::OP_DEC:
	case posttoken::OP_STAR:
	case posttoken::OP_AMP:
	case posttoken::OP_PLUS:
	case posttoken::OP_MINUS:
	case posttoken::OP_LNOT:
	case posttoken::OP_COMPL:
		return true;
	default:
		return false;
	}
}

int BinaryOperatorLevel(int kind)
{
	switch(kind)
	{
	case posttoken::OP_LOR:
		return 1;
	case posttoken::OP_LAND:
		return 2;
	case posttoken::OP_BOR:
		return 3;
	case posttoken::OP_XOR:
		return 4;
	case posttoken::OP_AMP:
		return 5;
	case posttoken::OP_EQ:
	case posttoken::OP_NE:
		return 6;
	case posttoken::OP_LT:
	case posttoken::OP_GT:
	case posttoken::OP_LE:
	case posttoken::OP_GE:
		return 7;
	case posttoken::OP_LSHIFT:
	case posttoken::OP_RSHIFT:
		return 8;
	case posttoken::OP_PLUS:
	case posttoken::OP_MINUS:
		return 9;
	case posttoken::OP_STAR:
	case posttoken::OP_DIV:
	case posttoken::OP_MOD:
		return 10;
	case posttoken::OP_DOTSTAR:
	case posttoken::OP_ARROWSTAR:
		return 11;
	default:
		return 0;
	}
}

const char* SyntaxTokenKindName(int kind)
{
	switch(kind)
	{
	case kIdentifierToken:
		return "TT_IDENTIFIER";
	case kLiteralToken:
		return "TT_LITERAL";
	case kEofToken:
		return "ST_EOF";
	default:
		break;
	}
	return SimpleTokenName(static_cast<ETokenType>(kind));
}

// ---------------------------------------------------------------------------
// Cursor
// ---------------------------------------------------------------------------

Parser::Parser(const vector<SyntaxToken>& tokens, SyntaxArena& arena)
	: tokens_(tokens)
	, arena_(arena)
	, pos_(0)
	, rshift_split_(false)
	, angle_depth_(0)
	, nested_delim_(0)
	, declaration_only_(1)
	, next_angle_speculative_(false)
{
	scopes_.push_back(map<string, int>());
}

int Parser::KindAt(size_t offset) const
{
	if(offset == 0 && rshift_split_)
	{
		return posttoken::OP_GT;
	}
	const size_t index = pos_ + offset;
	return index < tokens_.size() ? tokens_[index].kind : kEofToken;
}

bool Parser::At(int kind, size_t offset) const
{
	return KindAt(offset) == kind;
}

bool Parser::AtAny(int first, int second, size_t offset) const
{
	const int kind = KindAt(offset);
	return kind == first || kind == second;
}

bool Parser::AtEof() const
{
	return KindAt() == kEofToken;
}

const SyntaxToken& Parser::TokenAt(size_t offset) const
{
	const size_t index = pos_ + offset;
	return index < tokens_.size() ? tokens_[index] : tokens_.back();
}

string Parser::Spelling(size_t offset) const
{
	if(offset == 0 && rshift_split_)
	{
		return string(">");
	}
	return TokenAt(offset).spelling;
}

const SyntaxToken& Parser::Current() const
{
	if(rshift_split_)
	{
		split_token_.kind = posttoken::OP_GT;
		split_token_.spelling = ">";
		return split_token_;
	}
	return TokenAt();
}

void Parser::Advance()
{
	if(rshift_split_)
	{
		rshift_split_ = false;
	}
	if(pos_ + 1 < tokens_.size())
	{
		++pos_;
	}
}

bool Parser::Accept(int kind)
{
	if(At(kind))
	{
		Advance();
		return true;
	}
	return false;
}

void Parser::Expect(int kind, const char* what)
{
	if(!Accept(kind))
	{
		throw SyntaxError(string("expected ") + what);
	}
}

size_t Parser::Position() const
{
	return pos_;
}

size_t Parser::EndPosition() const
{
	return rshift_split_ ? pos_ + 1 : pos_;
}

int Parser::Tag(const char* name)
{
	return arena_.Make(name);
}

int Parser::Named(const char* name, const string& label)
{
	return arena_.Make(name, label);
}

int Parser::Terminal(const char* name, const SyntaxToken& token)
{
	return arena_.Make(name, TokenLabel(token));
}

void Parser::Add(int parent, int child)
{
	arena_.AddChild(parent, child);
}

string Parser::JoinedText(size_t first, size_t last) const
{
	string text;
	for(size_t index = first; index < last && index < tokens_.size(); ++index)
	{
		const string& spelling = tokens_[index].spelling;
		if(!text.empty() && IsIdentChar(text[text.size() - 1]) && !spelling.empty() &&
		   IsIdentChar(spelling[0]))
		{
			text += ' ';
		}
		text += spelling;
	}
	return text;
}

string Parser::TokenLabel(const SyntaxToken& token)
{
	string label(SyntaxTokenKindName(token.kind));
	label += ':';
	label += token.spelling;
	return label;
}

Parser::Mark Parser::Take() const
{
	Mark mark;
	mark.pos = pos_;
	mark.rshift = rshift_split_;
	mark.nodes = arena_.NodeCount();
	mark.scopes = scopes_.size();
	mark.scope_size = scopes_.back().size();
	mark.classes = classes_.size();
	return mark;
}

void Parser::Rollback(const Mark& mark)
{
	pos_ = mark.pos;
	rshift_split_ = mark.rshift;
	arena_.DropTo(mark.nodes);
	while(scopes_.size() > mark.scopes)
	{
		scopes_.pop_back();
	}
	if(scopes_.back().size() > mark.scope_size)
	{
		map<string, int>& scope = scopes_.back();
		while(scope.size() > mark.scope_size)
		{
			map<string, int>::iterator it = scope.end();
			--it;
			scope.erase(it);
		}
	}
	while(classes_.size() > mark.classes)
	{
		classes_.pop_back();
	}
}

// ---------------------------------------------------------------------------
// Name categories
// ---------------------------------------------------------------------------

void Parser::PushScope()
{
	scopes_.push_back(map<string, int>());
}

void Parser::PopScope()
{
	if(scopes_.size() > 1)
	{
		scopes_.pop_back();
	}
}

void Parser::Bind(const string& name, int kind)
{
	if(!name.empty())
	{
		scopes_.back()[name] = kind;
	}
}

int Parser::Lookup(const string& name) const
{
	for(size_t index = scopes_.size(); index > 0; --index)
	{
		map<string, int>::const_iterator found = scopes_[index - 1].find(name);
		if(found != scopes_[index - 1].end())
		{
			return found->second;
		}
	}
	return kNameUnknown;
}

// The course's lexical fallback for a name no declaration has bound: a `C`,
// `Y` or `E` can stand for a type, a `T` for a type or a template name.
int Parser::Hinted(const string& name)
{
	for(size_t index = 0; index < name.size(); ++index)
	{
		const char c = name[index];
		if(c == 'C' || c == 'Y' || c == 'E' || c == 'T')
		{
			return kNameType;
		}
	}
	return kNameUnknown;
}

int Parser::NameKind(const string& name) const
{
	const int bound = Lookup(name);
	return bound != kNameUnknown ? bound : Hinted(name);
}

bool Parser::IsTypeName(const string& name) const
{
	const int kind = NameKind(name);
	return kind == kNameType || kind == kNameTemplate || kind == kNameNamespace;
}

bool Parser::IsTemplateName(const string& name) const
{
	const int kind = NameKind(name);
	return kind == kNameType || kind == kNameTemplate;
}

bool Parser::IsKnownValue(const string& name) const
{
	return Lookup(name) == kNameValue;
}

// ---------------------------------------------------------------------------
// Translation unit
// ---------------------------------------------------------------------------

int Parser::Run()
{
	const int unit = Tag("translation-unit");
	while(!AtEof())
	{
		Add(unit, Declaration());
	}
	return unit;
}

int ParseTranslationUnit(const vector<SyntaxToken>& tokens, SyntaxArena& arena)
{
	Parser parser(tokens, arena);
	return parser.Run();
}

bool Parser::AtAttributes() const
{
	return At(posttoken::OP_LSQUARE) && At(posttoken::OP_LSQUARE, 1);
}

// A hosted attribute carries no PA5 tree, so `[[...]]`, `__attribute__((...))`
// and `alignas(...)` are skipped whole.  The dump has no node for any of them,
// which is what a syntax-only parse needs.
int Parser::SkipAttributes()
{
	int skipped = 0;
	for(;;)
	{
		if(At(posttoken::OP_LSQUARE) && At(posttoken::OP_LSQUARE, 1))
		{
			Advance();
			Advance();
			int depth = 1;
			while(depth > 0 && !AtEof())
			{
				if(At(posttoken::OP_LSQUARE) && At(posttoken::OP_LSQUARE, 1))
				{
					Advance();
					Advance();
					++depth;
					continue;
				}
				if(At(posttoken::OP_RSQUARE) && At(posttoken::OP_RSQUARE, 1))
				{
					Advance();
					Advance();
					--depth;
					continue;
				}
				Advance();
			}
			++skipped;
			continue;
		}
		if(At(kIdentifierToken) && Spelling() == "__attribute__")
		{
			Advance();
			Expect(posttoken::OP_LPAREN, "`(`");
			int depth = 1;
			while(depth > 0 && !AtEof())
			{
				if(At(posttoken::OP_LPAREN))
				{
					++depth;
				}
				else if(At(posttoken::OP_RPAREN))
				{
					--depth;
				}
				Advance();
			}
			++skipped;
			continue;
		}
		if(At(posttoken::KW_ALIGNAS))
		{
			Advance();
			Expect(posttoken::OP_LPAREN, "`(`");
			int depth = 1;
			while(depth > 0 && !AtEof())
			{
				if(At(posttoken::OP_LPAREN))
				{
					++depth;
				}
				else if(At(posttoken::OP_RPAREN))
				{
					--depth;
				}
				Advance();
			}
			++skipped;
			continue;
		}
		break;
	}
	return skipped;
}

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------

int Parser::EmptyDeclaration()
{
	SkipAttributes();
	Expect(posttoken::OP_SEMICOLON, "`;`");
	return Tag("empty-declaration");
}

bool Parser::CanStartDeclSpecifier() const
{
	const int kind = KindAt();
	if(IsSimpleTypeSpecifierKind(kind) || IsCvQualifierKind(kind) ||
	   IsStorageSpecifierKind(kind) || kind == posttoken::KW_DECLTYPE ||
	   kind == posttoken::KW_ENUM || IsClassKeyKind(kind) ||
	   kind == posttoken::OP_COLON2 || kind == posttoken::KW_TYPENAME)
	{
		return true;
	}
	if(kind == kIdentifierToken)
	{
		// A name begins a decl-specifier-seq wherever only a declaration can
		// appear; inside a block the category of the name - of a qualified
		// name's last component - decides.
		return declaration_only_ > 0 || AtQualifiedTypeStart(0);
	}
	return false;
}

int Parser::DeclSpecifierSeq(bool& saw_type, bool& saw_typedef)
{
	SkipAttributes();
	if(!CanStartDeclSpecifier())
	{
		return kNoSyntaxNode;
	}
	const int seq = Tag("decl-specifier-seq");
	saw_type = false;
	saw_typedef = false;
	for(;;)
	{
		SkipAttributes();
		const int kind = KindAt();
		if(IsSimpleTypeSpecifierKind(kind) || IsCvQualifierKind(kind) ||
		   IsStorageSpecifierKind(kind))
		{
			if(IsSimpleTypeSpecifierKind(kind) || kind == posttoken::KW_AUTO)
			{
				// `auto` is a placeholder for a type, so it ends the type part
				// the same way a type name does.
				saw_type = true;
			}
			if(kind == posttoken::KW_TYPEDEF)
			{
				saw_typedef = true;
			}
			Add(seq, Terminal("decl-specifier", Current()));
			Advance();
			continue;
		}
		if(kind == posttoken::KW_DECLTYPE)
		{
			const size_t start = Position();
			Expect(posttoken::KW_DECLTYPE, "`decltype`");
			Expect(posttoken::OP_LPAREN, "`(`");
			const int operand = Expression();
			Expect(posttoken::OP_RPAREN, "`)`");
			const int node = Named("decl-specifier", JoinedText(start, EndPosition()));
			Add(node, operand);
			Add(seq, node);
			saw_type = true;
			continue;
		}
		if(kind == posttoken::KW_ENUM)
		{
			Add(seq, EnumSpecifier());
			saw_type = true;
			continue;
		}
		if(IsClassKeyKind(kind))
		{
			if(At(posttoken::OP_LBRACE, 1) ||
			   (At(kIdentifierToken, 1) && AtAny(posttoken::OP_LBRACE, posttoken::OP_LT, 2)))
			{
				Add(seq, ClassSpecifier(false));
			}
			else
			{
				Add(seq, ElaboratedTypeSpecifier());
			}
			saw_type = true;
			continue;
		}
		if(kind == posttoken::KW_TYPENAME)
		{
			// `typename` only marks the name that follows as a type.
			Advance();
			continue;
		}
		if(kind == posttoken::OP_COLON2)
		{
			const size_t start = Position();
			bool seen = false;
			QualifiedTypeName(seen, false);
			if(!seen)
			{
				break;
			}
			const size_t last = EndPosition();
			Add(seq, Named("decl-specifier", JoinedText(start, last)));
			saw_type = true;
			continue;
		}
		if(kind == kIdentifierToken)
		{
			// A name followed by `::` is a qualified name, and a declaration
			// is the only reading of one; a plain name needs a category.
			if(saw_type || (!At(posttoken::OP_COLON2, 1) && declaration_only_ == 0 &&
			                !IsTypeName(Spelling())))
			{
				// Once the sequence has a type specifier, a name begins the
				// declarator: `typedef int FILE` declares `FILE`.
				break;
			}
			const size_t start = Position();
			bool seen = false;
			QualifiedTypeName(seen, declaration_only_ == 0);
			if(!seen || At(posttoken::OP_COLON2))
			{
				// The `::` after the name belongs to the name that follows it,
				// so this is not a decl-specifier-seq at all: `C::operator int`
				// starts a special member definition and `N::f();` a call.
				throw SyntaxError("not a type name at token " + to_string(pos_) + " (`" +
				                  Spelling() + "`)");
			}
			const size_t last = EndPosition();
			if(last == start + 1)
			{
				Add(seq, Terminal("decl-specifier", tokens_[start]));
			}
			else
			{
				Add(seq, Named("decl-specifier", JoinedText(start, last)));
			}
			saw_type = true;
			continue;
		}
		break;
	}
	return seq;
}

void Parser::InitDeclaratorList(int parent, bool declare_type)
{
	const int list = Tag("init-declarator-list");
	for(;;)
	{
		const int init = Tag("init-declarator");
		Add(init, Declarator());
		Bind(declarator_name_, declare_type ? kNameType : kNameValue);
		if(At(posttoken::OP_ASS) || At(posttoken::OP_LBRACE) || At(posttoken::OP_LPAREN))
		{
			Add(init, Initializer());
		}
		Add(list, init);
		if(!Accept(posttoken::OP_COMMA))
		{
			break;
		}
	}
	Add(parent, list);
}

int Parser::Initializer()
{
	const int node = Tag("initializer");
	if(Accept(posttoken::OP_ASS))
	{
		if(At(posttoken::OP_LBRACE))
		{
			Add(node, BracedInitList());
		}
		else if(At(posttoken::KW_DEFAULT) || At(posttoken::KW_DELETE))
		{
			const int special = Named("special-initializer", Spelling());
			Advance();
			Add(node, special);
		}
		else
		{
			Add(node, AssignmentExpression());
		}
		return node;
	}
	if(At(posttoken::OP_LBRACE))
	{
		Add(node, BracedInitList());
		return node;
	}
	const int paren = Tag("paren-initializer");
	Expect(posttoken::OP_LPAREN, "`(`");
	if(!At(posttoken::OP_RPAREN))
	{
		for(;;)
		{
			if(At(posttoken::OP_LBRACE))
			{
				Add(paren, BracedInitList());
			}
			else
			{
				Add(paren, AssignmentExpression());
			}
			if(!Accept(posttoken::OP_COMMA))
			{
				break;
			}
		}
	}
	Expect(posttoken::OP_RPAREN, "`)`");
	Add(node, paren);
	return node;
}

int Parser::Declaration()
{
	if(AtAttributes() || At(posttoken::KW_ALIGNAS))
	{
		SkipAttributes();
		if(At(posttoken::OP_SEMICOLON))
		{
			return EmptyDeclaration();
		}
	}
	if(At(posttoken::OP_SEMICOLON))
	{
		return EmptyDeclaration();
	}
	if(At(posttoken::KW_NAMESPACE) ||
	   (At(posttoken::KW_INLINE) && At(posttoken::KW_NAMESPACE, 1)))
	{
		if(At(kIdentifierToken, 1) && At(posttoken::OP_ASS, 2))
		{
			return NamespaceAliasDefinition();
		}
		return NamespaceDefinition();
	}
	if(At(posttoken::KW_USING))
	{
		if(At(posttoken::KW_NAMESPACE, 1))
		{
			return UsingDirective();
		}
		if(At(kIdentifierToken, 1) && At(posttoken::OP_ASS, 2))
		{
			return AliasDeclaration();
		}
		return UsingDeclaration();
	}
	if(At(posttoken::KW_TEMPLATE))
	{
		return TemplateDeclaration();
	}
	if(At(posttoken::KW_EXTERN) && At(kLiteralToken, 1))
	{
		return LinkageSpecification();
	}
	if(At(posttoken::KW_EXTERN) && At(posttoken::KW_TEMPLATE, 1))
	{
		return ExplicitInstantiationDeclaration();
	}
	if(At(posttoken::KW_STATIC_ASSERT))
	{
		return StaticAssertDeclaration();
	}
	if(At(posttoken::KW_ENUM))
	{
		// `enum name { ... }`, `enum name : type ...` and `enum name ;` are
		// enum declarations; anything else is an elaborated type specifier in a
		// declaration of its own.
		size_t cursor = 1;
		if(At(posttoken::KW_CLASS, cursor) || At(posttoken::KW_STRUCT, cursor))
		{
			++cursor;
		}
		if(At(kIdentifierToken, cursor))
		{
			++cursor;
		}
		if(At(posttoken::OP_LBRACE, cursor) || At(posttoken::OP_COLON, cursor) ||
		   At(posttoken::OP_SEMICOLON, cursor))
		{
			return EnumSpecifier();
		}
		return DeclarationCommon(true);
	}
	if(IsClassKeyKind(KindAt()))
	{
		if(At(kIdentifierToken, 1) && At(posttoken::OP_SEMICOLON, 2))
		{
			return ClassForwardDeclaration();
		}
		// A class-specifier has a body or ends the declaration; anything else
		// is an elaborated type specifier in a declaration of its own.
		const Mark mark = Take();
		try
		{
			return ClassSpecifier(true);
		}
		catch(const SyntaxError&)
		{
		}
		Rollback(mark);
		return DeclarationCommon(true);
	}
	return DeclarationCommon(true);
}

int Parser::DeclarationCommon(bool allow_function_definition)
{
	// A declaration and a special member share a prefix and differ in whether a
	// decl-specifier-seq is there at all, so the ordinary declaration is tried
	// first and the special member forms are the fallback.
	const Mark start = Take();
	string reason;
	try
	{
		return DeclarationBody(allow_function_definition);
	}
	catch(const SyntaxError& error)
	{
		reason = error.what();
	}
	Rollback(start);
	if(StartsSpecialMember())
	{
		return SpecialMember();
	}
	throw SyntaxError(reason);
}

int Parser::DeclarationBody(bool allow_function_definition)
{
	bool saw_type = false;
	bool saw_typedef = false;
	const int specifiers = DeclSpecifierSeq(saw_type, saw_typedef);
	if(specifiers == kNoSyntaxNode)
	{
		throw SyntaxError("expected declaration at token " + to_string(pos_) + " (`" +
		                  Spelling() + "`)");
	}

	if(allow_function_definition)
	{
		const Mark mark = Take();
		int declarator = kNoSyntaxNode;
		bool have_declarator = true;
		try
		{
			declarator = Declarator();
		}
		catch(const SyntaxError&)
		{
			have_declarator = false;
		}
		const bool is_function = have_declarator && declarator_is_function_;
		if(have_declarator && At(posttoken::OP_LBRACE) && is_function)
		{
			const int node = Tag("function-definition");
			Add(node, specifiers);
			Add(node, declarator);
			Add(node, CompoundStatement());
			return node;
		}
		if(have_declarator && At(posttoken::KW_TRY) && is_function)
		{
			// A function try block belongs to the function definition.
			const int node = Tag("function-definition");
			Add(node, specifiers);
			Add(node, declarator);
			const int body = Tag("function-try-block");
			Expect(posttoken::KW_TRY, "`try`");
			Add(body, CompoundStatement());
			while(At(posttoken::KW_CATCH))
			{
				Add(body, Handler());
			}
			Add(node, body);
			return node;
		}
		Rollback(mark);
	}

	const int node = Tag("simple-declaration");
	Add(node, specifiers);
	if(Accept(posttoken::OP_SEMICOLON))
	{
		return node;
	}
	InitDeclaratorList(node, saw_typedef);
	Expect(posttoken::OP_SEMICOLON, "`;`");
	return node;
}

// ---------------------------------------------------------------------------
// Namespaces, using declarations, aliases
// ---------------------------------------------------------------------------

int Parser::NamespaceDefinition()
{
	const int node = Tag("namespace-definition");
	if(Accept(posttoken::KW_INLINE))
	{
		Add(node, Tag("inline"));
	}
	Expect(posttoken::KW_NAMESPACE, "`namespace`");
	string name("<unnamed>");
	if(At(kIdentifierToken))
	{
		name = Spelling();
		Advance();
	}
	arena_.SetLabel(node, name);
	if(name != "<unnamed>")
	{
		Bind(name, kNameNamespace);
	}
	Expect(posttoken::OP_LBRACE, "`{`");
	// A namespace's members are visible to the enclosing scope's later
	// declarations, so they bind there rather than in a scope of their own.
	++declaration_only_;
	while(!At(posttoken::OP_RBRACE) && !AtEof())
	{
		Add(node, Declaration());
	}
	--declaration_only_;
	Expect(posttoken::OP_RBRACE, "`}`");
	return node;
}

int Parser::NamespaceAliasDefinition()
{
	const int node = Tag("namespace-alias-definition");
	Expect(posttoken::KW_NAMESPACE, "`namespace`");
	const string name = Spelling();
	Advance();
	arena_.SetLabel(node, name);
	Bind(name, kNameNamespace);
	Expect(posttoken::OP_ASS, "`=`");
	const size_t start = Position();
	bool seen = false;
	QualifiedTypeName(seen, false);
	Add(node, Named("target", JoinedText(start, EndPosition())));
	Expect(posttoken::OP_SEMICOLON, "`;`");
	return node;
}

int Parser::UsingDirective()
{
	const int node = Tag("using-directive");
	Expect(posttoken::KW_USING, "`using`");
	Expect(posttoken::KW_NAMESPACE, "`namespace`");
	const size_t start = Position();
	bool seen = false;
	QualifiedTypeName(seen, false);
	Add(node, Named("target", JoinedText(start, EndPosition())));
	Expect(posttoken::OP_SEMICOLON, "`;`");
	return node;
}

int Parser::UsingDeclaration()
{
	const int node = Tag("using-declaration");
	Expect(posttoken::KW_USING, "`using`");
	const size_t start = Position();
	IdExpression("id-expression");
	Add(node, Named("target", JoinedText(start, EndPosition())));
	Expect(posttoken::OP_SEMICOLON, "`;`");
	return node;
}

int Parser::AliasDeclaration()
{
	const int node = Tag("alias-declaration");
	Expect(posttoken::KW_USING, "`using`");
	const string name = Spelling();
	Advance();
	arena_.SetLabel(node, name);
	Bind(name, kNameType);
	Expect(posttoken::OP_ASS, "`=`");
	Add(node, TypeId());
	Expect(posttoken::OP_SEMICOLON, "`;`");
	return node;
}

int Parser::StaticAssertDeclaration()
{
	const int node = Tag("static-assert-declaration");
	Expect(posttoken::KW_STATIC_ASSERT, "`static_assert`");
	Expect(posttoken::OP_LPAREN, "`(`");
	Add(node, AssignmentExpression());
	if(Accept(posttoken::OP_COMMA))
	{
		if(At(kLiteralToken))
		{
			Add(node, Named("message", Spelling()));
			Advance();
		}
	}
	Expect(posttoken::OP_RPAREN, "`)`");
	Expect(posttoken::OP_SEMICOLON, "`;`");
	return node;
}

int Parser::ExplicitInstantiationDeclaration()
{
	const int node = Tag("explicit-instantiation-declaration");
	Expect(posttoken::KW_EXTERN, "`extern`");
	Expect(posttoken::KW_TEMPLATE, "`template`");
	Add(node, Declaration());
	return node;
}

// `extern "C" { ... }` or `extern "C" declaration`.  The dump spells the
// linkage with the literal's content rather than its source text.
int Parser::LinkageSpecification()
{
	Expect(posttoken::KW_EXTERN, "`extern`");
	const string spelling = Spelling();
	Expect(kLiteralToken, "a string literal");
	string linkage = spelling;
	if(linkage.size() >= 2 && linkage[0] == '"' && linkage[linkage.size() - 1] == '"')
	{
		linkage = linkage.substr(1, linkage.size() - 2);
	}
	const int node = Named("linkage-specification", linkage);
	if(Accept(posttoken::OP_LBRACE))
	{
		while(!At(posttoken::OP_RBRACE) && !AtEof())
		{
			Add(node, Declaration());
		}
		Expect(posttoken::OP_RBRACE, "`}`");
		return node;
	}
	Add(node, Declaration());
	return node;
}

// ---------------------------------------------------------------------------
// Templates
// ---------------------------------------------------------------------------

int Parser::TemplateDeclaration()
{
	const int node = Tag("template-declaration");
	Expect(posttoken::KW_TEMPLATE, "`template`");
	PushScope();
	Add(node, TemplateParameterClause());
	Add(node, Declaration());
	PopScope();
	return node;
}

int Parser::TemplateParameterClause()
{
	const int node = Tag("template-parameter-clause");
	Expect(posttoken::OP_LT, "`<`");
	EnterAngle();
	if(!At(posttoken::OP_GT) && !At(posttoken::OP_RSHIFT))
	{
		TemplateParameterList(node);
	}
	CloseAngle();
	LeaveAngle();
	return node;
}

void Parser::TemplateParameterList(int parent)
{
	const int list = Tag("template-parameter-list");
	for(;;)
	{
		Add(list, TemplateParameter());
		if(!Accept(posttoken::OP_COMMA))
		{
			break;
		}
	}
	Add(parent, list);
}

int Parser::TemplateParameter()
{
	if(At(posttoken::KW_CLASS) || At(posttoken::KW_TYPENAME))
	{
		const int node = Tag("type-parameter");
		Add(node, Terminal("parameter-key", Current()));
		Advance();
		if(Accept(posttoken::OP_DOTS))
		{
			Add(node, Named("parameter-pack", "..."));
		}
		if(At(kIdentifierToken))
		{
			Bind(Spelling(), kNameType);
			Add(node, Named("identifier", Spelling()));
			Advance();
		}
		if(Accept(posttoken::OP_ASS))
		{
			const int def = Tag("default-template-argument");
			Add(def, TypeId());
			Add(node, def);
		}
		return node;
	}
	if(At(posttoken::KW_TEMPLATE))
	{
		// `template-parameter`'s second form: a marker for the keyword, then
		// the clause, the key and the name.
		const int node = Tag("type-parameter");
		Add(node, Tag("template-template-parameter"));
		Advance();
		Add(node, TemplateParameterClause());
		Add(node, Terminal("parameter-key", Current()));
		Expect(posttoken::KW_CLASS, "`class`");
		if(At(kIdentifierToken))
		{
			Bind(Spelling(), kNameTemplate);
			Add(node, Named("identifier", Spelling()));
			Advance();
		}
		if(Accept(posttoken::OP_ASS))
		{
			const int def = Tag("default-template-argument");
			Add(def, TypeId());
			Add(node, def);
		}
		return node;
	}

	const int node = Tag("non-type-template-parameter");
	bool saw_type = false;
	bool saw_typedef = false;
	Add(node, DeclSpecifierSeq(saw_type, saw_typedef));
	if(At(posttoken::OP_DOTS))
	{
		Add(node, Named("parameter-pack", "..."));
		Advance();
	}
	if(At(kIdentifierToken) || At(posttoken::OP_STAR) || At(posttoken::OP_AMP) ||
	   At(posttoken::OP_LAND))
	{
		if(At(kIdentifierToken))
		{
			Bind(Spelling(), kNameValue);
		}
		Add(node, Declarator());
	}
	if(Accept(posttoken::OP_ASS))
	{
		const int def = Tag("default-template-argument");
		Add(def, AssignmentExpression());
		Add(node, def);
	}
	return node;
}

// ---------------------------------------------------------------------------
// Classes and enums
// ---------------------------------------------------------------------------

int Parser::ClassDeclaration()
{
	if(At(kIdentifierToken, 1) && At(posttoken::OP_SEMICOLON, 2))
	{
		return ClassForwardDeclaration();
	}
	return ClassSpecifier(true);
}

int Parser::ClassForwardDeclaration()
{
	const int node = Tag("class-forward-declaration");
	Add(node, Terminal("class-key", Current()));
	Advance();
	const string name = Spelling();
	Expect(kIdentifierToken, "a class name");
	arena_.SetLabel(node, name);
	Bind(name, kNameType);
	Accept(posttoken::OP_SEMICOLON);
	return node;
}

// `class-key name` where a type name is expected, as in `sizeof(struct S)`.
int Parser::ElaboratedTypeSpecifier()
{
	const int node = Tag("class-forward-declaration");
	Add(node, Terminal("class-key", Current()));
	Advance();
	if(At(kIdentifierToken))
	{
		const string name = Spelling();
		arena_.SetLabel(node, name);
		Bind(name, kNameType);
		Advance();
	}
	return node;
}

int Parser::ClassSpecifier(bool require_semicolon)
{
	const int node = Tag("class-specifier");
	Add(node, Terminal("class-key", Current()));
	Advance();
	SkipAttributes();
	string name;
	if(At(kIdentifierToken))
	{
		name = Spelling();
		const size_t start = Position();
		Advance();
		if(At(posttoken::OP_LT) && TryTemplateIdTail())
		{
			// A specialization's class head keeps the argument list in its name.
		}
		arena_.SetLabel(node, JoinedText(start, EndPosition()));
		if(!name.empty())
		{
			Bind(name, kNameType);
		}
	}
	if(At(posttoken::OP_COLON))
	{
		Add(node, BaseClause());
	}
	if(!At(posttoken::OP_LBRACE))
	{
		// A body-less class-specifier is only a declaration when it ends here.
		Expect(posttoken::OP_SEMICOLON, "`;`");
		return node;
	}
	{
		PushScope();
		classes_.push_back(name);
		++declaration_only_;
		Advance();
		while(!At(posttoken::OP_RBRACE) && !AtEof())
		{
			Add(node, ClassMember());
		}
		--declaration_only_;
		Expect(posttoken::OP_RBRACE, "`}`");
		classes_.pop_back();
		PopScope();
	}
	if(require_semicolon || At(posttoken::OP_SEMICOLON))
	{
		Expect(posttoken::OP_SEMICOLON, "`;`");
	}
	return node;
}

int Parser::BaseClause()
{
	const int node = Tag("base-clause");
	Expect(posttoken::OP_COLON, "`:`");
	for(;;)
	{
		const int specifier = Tag("base-specifier");
		if(At(posttoken::KW_VIRTUAL))
		{
			Add(specifier, Terminal("virtual", Current()));
			Advance();
		}
		if(At(posttoken::KW_PUBLIC) || At(posttoken::KW_PRIVATE) ||
		   At(posttoken::KW_PROTECTED))
		{
			Add(specifier, Terminal("access-specifier", Current()));
			Advance();
		}
		if(Accept(posttoken::KW_VIRTUAL))
		{
			Add(specifier, Named("virtual", "KW_VIRTUAL:virtual"));
		}
		const size_t start = Position();
		bool seen = false;
		QualifiedTypeName(seen, false);
		Add(specifier, Named("base-name", JoinedText(start, EndPosition())));
		if(At(posttoken::OP_DOTS))
		{
			Add(specifier, Terminal("pack-expansion", Current()));
			Advance();
		}
		Add(node, specifier);
		if(!Accept(posttoken::OP_COMMA))
		{
			break;
		}
	}
	return node;
}

int Parser::AccessSpecifier()
{
	const int node = Terminal("access-specifier", Current());
	Advance();
	Expect(posttoken::OP_COLON, "`:`");
	return node;
}

int Parser::EnumSpecifier()
{
	const int node = Tag("enum-specifier");
	Expect(posttoken::KW_ENUM, "`enum`");
	if(At(posttoken::KW_CLASS) || At(posttoken::KW_STRUCT))
	{
		Add(node, Terminal("enum-key", Current()));
		Advance();
	}
	SkipAttributes();
	if(At(kIdentifierToken))
	{
		const string name = Spelling();
		arena_.SetLabel(node, name);
		Bind(name, kNameType);
		Advance();
	}
	if(Accept(posttoken::OP_COLON))
	{
		Add(node, TypeId());
	}
	if(Accept(posttoken::OP_LBRACE))
	{
		while(!At(posttoken::OP_RBRACE) && !AtEof())
		{
			if(Accept(posttoken::OP_COMMA))
			{
				continue;
			}
			const int enumerator = Tag("enumerator");
			const string name = Spelling();
			arena_.SetLabel(enumerator, name);
			Expect(kIdentifierToken, "an enumerator name");
			if(Accept(posttoken::OP_ASS))
			{
				Add(enumerator, AssignmentExpression());
			}
			Add(node, enumerator);
		}
		Expect(posttoken::OP_RBRACE, "`}`");
	}
	Accept(posttoken::OP_SEMICOLON);
	return node;
}

// ---------------------------------------------------------------------------
// Class members
// ---------------------------------------------------------------------------

int Parser::ClassMember()
{
	if(At(posttoken::OP_SEMICOLON))
	{
		return EmptyDeclaration();
	}
	if((At(posttoken::KW_PUBLIC) || At(posttoken::KW_PRIVATE) ||
	    At(posttoken::KW_PROTECTED)) &&
	   At(posttoken::OP_COLON, 1))
	{
		return AccessSpecifier();
	}
	if(StartsSpecialMember())
	{
		return SpecialMember();
	}
	const Mark mark = Take();
	bool saw_type = false;
	bool saw_typedef = false;
	const int specifiers = DeclSpecifierSeq(saw_type, saw_typedef);
	if(specifiers != kNoSyntaxNode)
	{
		int declarator = kNoSyntaxNode;
		if(!At(posttoken::OP_COLON))
		{
			try
			{
				declarator = Declarator();
			}
			catch(const SyntaxError&)
			{
				declarator = kNoSyntaxNode;
			}
		}
		if(At(posttoken::OP_COLON))
		{
			// A bit-field declaration: the declarator, when written, is the
			// field's name.
			const int node = Tag("bit-field-declaration");
			Add(node, specifiers);
			int field_declarator = declarator;
			if(field_declarator == kNoSyntaxNode && !At(posttoken::OP_COLON))
			{
				field_declarator = Declarator();
			}
			const int field = Tag("bit-field-declarator");
			if(field_declarator != kNoSyntaxNode)
			{
				Add(field, field_declarator);
			}
			Expect(posttoken::OP_COLON, "`:`");
			Add(field, AssignmentExpression());
			Add(node, field);
			while(Accept(posttoken::OP_COMMA))
			{
				const int next_field = Tag("bit-field-declarator");
				if(!At(posttoken::OP_COLON))
				{
					Add(next_field, Declarator());
				}
				Expect(posttoken::OP_COLON, "`:`");
				Add(next_field, AssignmentExpression());
				Add(node, next_field);
			}
			Expect(posttoken::OP_SEMICOLON, "`;`");
			return node;
		}
	}
	Rollback(mark);
	return Declaration();
}

int Parser::MemberSpecifiers()
{
	if(!IsMemberFunctionSpecifierKind(KindAt()))
	{
		return kNoSyntaxNode;
	}
	const int node = Tag("member-specifiers");
	while(IsMemberFunctionSpecifierKind(KindAt()))
	{
		if(At(posttoken::KW_EXPLICIT))
		{
			Add(node, Named("specifier", "explicit"));
			Advance();
			continue;
		}
		Add(node, Terminal("specifier", Current()));
		Advance();
	}
	return node;
}

// A member-function-specifier run followed by a special member name and its
// parameter clause is the one shape that starts a special-member declaration.
bool Parser::StartsSpecialMember()
{
	if(At(posttoken::KW_DELETE))
	{
		return false;
	}
	const Mark mark = Take();
	while(IsMemberFunctionSpecifierKind(KindAt()))
	{
		Advance();
	}
	bool ok = false;
	if(!AtEof())
	{
		SkipAttributes();
		const Mark name_mark = Take();
		try
		{
			SpecialMemberName();
			ok = At(posttoken::OP_LPAREN) || At(posttoken::OP_ASS);
		}
		catch(const SyntaxError&)
		{
			ok = false;
		}
		Rollback(name_mark);
	}
	Rollback(mark);
	return ok;
}

// The name a special member declaration or definition opens with: a
// constructor, a destructor, an operator function or a conversion function.
int Parser::SpecialMemberName()
{
	// A qualified name first: `C::C`, `C::~C`, `C::operator int` and the
	// operator forms.  The name after the specifier is a constructor only when
	// it repeats the last component of that specifier.
	const Mark mark = Take();
	const size_t start = Position();
	bool present = false;
	const string component = NestedNameSpecifier(present);
	if(present)
	{
		bool member = false;
		int node = kNoSyntaxNode;
		if(At(posttoken::KW_OPERATOR))
		{
			// Only a conversion function may be qualified: `C::operator+` is
			// an operator function, which a qualified special member name
			// does not allow.
			const Mark operator_mark = Take();
			Advance();
			const bool conversion = AtConversionTypeStart();
			Rollback(operator_mark);
			if(conversion)
			{
				node = UnqualifiedId("identifier");
				member = true;
			}
		}
		else if(At(posttoken::OP_COMPL))
		{
			Advance();
			Expect(kIdentifierToken, "a destructor name");
			member = true;
		}
		else if(At(kIdentifierToken) && Spelling() == component)
		{
			node = Named("identifier", Spelling());
			Advance();
			member = true;
		}
		if(member && (At(posttoken::OP_LPAREN) || At(posttoken::OP_ASS)))
		{
			(void)node;
			return Named("identifier", JoinedText(start, EndPosition()));
		}
	}
	Rollback(mark);

	if(!classes_.empty() && At(kIdentifierToken) && Spelling() == classes_.back())
	{
		const int node = Named("identifier", Spelling());
		Advance();
		return node;
	}
	if(At(posttoken::KW_OPERATOR))
	{
		return UnqualifiedId("identifier");
	}
	if(At(posttoken::OP_COMPL))
	{
		Advance();
		const string name = Spelling();
		Expect(kIdentifierToken, "a destructor name");
		return Named("identifier", "~" + name);
	}
	throw SyntaxError("expected a special member name");
}

int Parser::SpecialMember()
{
	const Mark mark = Take();
	const int specs = MemberSpecifiers();
	SkipAttributes();
	const size_t name_start = Position();
	const int name = SpecialMemberName();
	(void)name_start;
	const int declarator = Tag("declarator");
	Add(declarator, name);
	if(!At(posttoken::OP_LPAREN))
	{
		Rollback(mark);
		throw SyntaxError("expected a parameter clause");
	}
	Add(declarator, ParameterClause());
	while(StartsFunctionSuffix())
	{
		Add(declarator, FunctionSuffix(false));
	}
	if(At(posttoken::OP_ASS))
	{
		const int node = Tag("special-member-declaration");
		arena_.SetLabel(node, arena_.Text(arena_.Node(name).label));
		if(specs != kNoSyntaxNode)
		{
			Add(node, specs);
		}
		Add(node, declarator);
		const int initializer = Tag("special-member-initializer");
		Advance();
		if(At(posttoken::KW_DEFAULT))
		{
			Add(initializer, Named("default", TokenLabel(Current())));
			Advance();
		}
		else if(At(posttoken::KW_DELETE))
		{
			Add(initializer, Named("delete", TokenLabel(Current())));
			Advance();
		}
		else
		{
			Rollback(mark);
			throw SyntaxError("expected `default` or `delete`");
		}
		Add(node, initializer);
		Expect(posttoken::OP_SEMICOLON, "`;`");
		return node;
	}
	if(At(posttoken::OP_COLON) || At(posttoken::OP_LBRACE))
	{
		const int node = Tag("special-member-definition");
		arena_.SetLabel(node, arena_.Text(arena_.Node(name).label));
		if(specs != kNoSyntaxNode)
		{
			Add(node, specs);
		}
		Add(node, declarator);
		if(At(posttoken::OP_COLON))
		{
			Add(node, CtorInitializer());
		}
		Add(node, CompoundStatement());
		return node;
	}
	if(At(posttoken::OP_SEMICOLON))
	{
		const int node = Tag("special-member-declaration");
		arena_.SetLabel(node, arena_.Text(arena_.Node(name).label));
		if(specs != kNoSyntaxNode)
		{
			Add(node, specs);
		}
		Add(node, declarator);
		Accept(posttoken::OP_SEMICOLON);
		return node;
	}
	Rollback(mark);
	throw SyntaxError("expected a special member body");
}

int Parser::CtorInitializer()
{
	const int node = Tag("ctor-initializer");
	Expect(posttoken::OP_COLON, "`:`");
	for(;;)
	{
		const int initializer = Tag("mem-initializer");
		const size_t start = Position();
		bool seen = false;
		QualifiedTypeName(seen, false);
		if(!seen)
		{
			DecltypeSpecifier();
		}
		Add(initializer, Named("mem-initializer-id", JoinedText(start, EndPosition())));
		if(At(posttoken::OP_LBRACE))
		{
			Add(initializer, BracedInitList());
		}
		else
		{
			const int paren = Tag("paren-argument-list");
			Expect(posttoken::OP_LPAREN, "`(`");
			if(!At(posttoken::OP_RPAREN))
			{
				for(;;)
				{
					if(At(posttoken::OP_LBRACE))
					{
						Add(paren, BracedInitList());
					}
					else
					{
						Add(paren, AssignmentExpression());
					}
					if(!Accept(posttoken::OP_COMMA))
					{
						break;
					}
				}
			}
			Expect(posttoken::OP_RPAREN, "`)`");
			Add(initializer, paren);
		}
		if(At(posttoken::OP_DOTS))
		{
			Add(initializer, Terminal("pack-expansion", Current()));
			Advance();
		}
		Add(node, initializer);
		if(!Accept(posttoken::OP_COMMA))
		{
			break;
		}
	}
	return node;
}

// ---------------------------------------------------------------------------
// Declarators
// ---------------------------------------------------------------------------

int Parser::PtrOperator()
{
	return Terminal("ptr-operator", Current());
}

int Parser::Declarator()
{
	const int node = Tag("declarator");
	declarator_is_function_ = false;
	for(;;)
	{
		if(At(posttoken::OP_STAR) || At(posttoken::OP_AMP) || At(posttoken::OP_LAND))
		{
			Add(node, PtrOperator());
			Advance();
		}
		else if(At(posttoken::OP_COLON2) ||
		        (At(kIdentifierToken) && AtAny(posttoken::OP_COLON2, posttoken::OP_LT, 1)))
		{
			// A pointer to member: `nested-name-specifier *`.
			const Mark member = Take();
			const size_t start = Position();
			bool present = false;
			NestedNameSpecifier(present);
			if(!present || !At(posttoken::OP_STAR))
			{
				Rollback(member);
				break;
			}
			Advance();
			Add(node, Named("ptr-operator", JoinedText(start, EndPosition())));
		}
		else
		{
			break;
		}
		while(IsCvQualifierKind(KindAt()))
		{
			Add(node, Terminal("cv-qualifier", Current()));
			Advance();
		}
	}
	bool has_content = !arena_.Node(node).children.empty();
	if(At(posttoken::OP_LPAREN))
	{
		Advance();
		const int nested = Tag("nested-declarator");
		Add(nested, Declarator());
		Expect(posttoken::OP_RPAREN, "`)`");
		Add(node, nested);
		has_content = true;
	}
	else
	{
		// A parameter pack marker belongs to the declarator, on whichever side
		// of the name it was written.
		if(At(posttoken::OP_DOTS))
		{
			Add(node, Named("parameter-pack", "..."));
			Advance();
			has_content = true;
		}
		if(At(kIdentifierToken) || At(posttoken::OP_COLON2) ||
		   At(posttoken::KW_OPERATOR) || At(posttoken::OP_COMPL))
		{
			Add(node, DeclaratorId());
			has_content = true;
		}
		if(At(posttoken::OP_DOTS))
		{
			Add(node, Named("parameter-pack", "..."));
			Advance();
			has_content = true;
		}
	}
	if(!has_content)
	{
		// An empty declarator is not one; the caller's abstract reading is.
		arena_.DropTo(static_cast<size_t>(node));
		throw SyntaxError("expected a declarator");
	}
	// The name the suffixes see is the one this declarator declared; a
	// parameter clause inside them declares names of its own.
	const string own_name = declarator_name_;
	for(;;)
	{
		// A hosted attribute after the declarator-id carries no tree.
		SkipAttributes();
		if(At(posttoken::OP_LSQUARE))
		{
			Add(node, DeclaratorSuffix(node));
			declarator_is_function_ = false;
			continue;
		}
		if(At(posttoken::OP_LPAREN))
		{
			// A `(` that does not open a parameter clause belongs to the
			// initializer that follows the declarator, as in `C y(1, 2);`.
			const Mark mark = Take();
			int clause = kNoSyntaxNode;
			bool ok = true;
			try
			{
				clause = ParameterClause();
			}
			catch(const SyntaxError&)
			{
				ok = false;
			}
			if(!ok)
			{
				Rollback(mark);
				break;
			}
			Add(node, clause);
			declarator_is_function_ = true;
			while(StartsFunctionSuffix())
			{
				Add(node, FunctionSuffix(false));
			}
			continue;
		}
		break;
	}
	declarator_name_ = own_name;
	return node;
}

int Parser::DeclaratorId()
{
	declarator_name_.clear();
	if(At(kIdentifierToken) && !At(posttoken::OP_COLON2, 1) && !At(posttoken::OP_LT, 1))
	{
		declarator_name_ = Spelling();
	}
	return IdExpression("identifier");
}

int Parser::DeclaratorSuffix(int parent)
{
	(void)parent;
	const int node = Tag("array-suffix");
	Expect(posttoken::OP_LSQUARE, "`[`");
	if(!At(posttoken::OP_RSQUARE))
	{
		Add(node, AssignmentExpression());
	}
	Expect(posttoken::OP_RSQUARE, "`]`");
	return node;
}

bool Parser::StartsFunctionSuffix(size_t offset, bool lambda_mode) const
{
	const int kind = KindAt(offset);
	if(IsCvQualifierKind(kind) || kind == posttoken::KW_NOEXCEPT ||
	   kind == posttoken::KW_THROW || kind == posttoken::OP_ARROW ||
	   kind == posttoken::OP_AMP || kind == posttoken::OP_LAND)
	{
		return true;
	}
	if(kind == kIdentifierToken && !lambda_mode)
	{
		const string spelling = Spelling(offset);
		return spelling == "final" || spelling == "override";
	}
	return false;
}

int Parser::FunctionSuffix(bool lambda_mode)
{
	const int kind = KindAt();
	if(IsCvQualifierKind(kind))
	{
		const int node = Terminal("cv-qualifier", Current());
		Advance();
		return node;
	}
	if(kind == posttoken::OP_AMP || kind == posttoken::OP_LAND)
	{
		const int node = Named("ref-qualifier", Spelling());
		Advance();
		return node;
	}
	if(kind == posttoken::KW_NOEXCEPT)
	{
		const size_t start = Position();
		Advance();
		if(lambda_mode)
		{
			const int node = Tag("noexcept-specification");
			if(Accept(posttoken::OP_LPAREN))
			{
				Add(node, Expression());
				Expect(posttoken::OP_RPAREN, "`)`");
			}
			return node;
		}
		const int node = Tag("function-qualifier");
		if(Accept(posttoken::OP_LPAREN))
		{
			Add(node, Expression());
			Expect(posttoken::OP_RPAREN, "`)`");
		}
		arena_.SetLabel(node, JoinedText(start, EndPosition()));
		return node;
	}
	if(kind == posttoken::KW_THROW)
	{
		const size_t start = Position();
		Advance();
		const int node = Tag("function-qualifier");
		Expect(posttoken::OP_LPAREN, "`(`");
		if(!At(posttoken::OP_RPAREN))
		{
			for(;;)
			{
				bool is_type = false;
				TypeIdOrExpr(is_type);
				if(!Accept(posttoken::OP_COMMA))
				{
					break;
				}
			}
		}
		Expect(posttoken::OP_RPAREN, "`)`");
		arena_.SetLabel(node, JoinedText(start, EndPosition()));
		return node;
	}
	if(kind == posttoken::OP_ARROW)
	{
		const size_t start = Position();
		Advance();
		const int node = Tag("trailing-return-type");
		Add(node, TypeId());
		if(!lambda_mode)
		{
			arena_.SetLabel(node, JoinedText(start + 1, EndPosition()));
		}
		return node;
	}
	if(kind == kIdentifierToken)
	{
		const string spelling = Spelling();
		Advance();
		return Named("virt-specifier", "TT_IDENTIFIER:" + spelling);
	}
	throw SyntaxError("expected a function suffix");
}

int Parser::ParameterClause()
{
	const int node = Tag("parameter-clause");
	Expect(posttoken::OP_LPAREN, "`(`");
	++nested_delim_;
	SkipAttributes();
	if(At(posttoken::OP_DOTS))
	{
		Add(node, Named("parameter-pack", "..."));
		Advance();
		Expect(posttoken::OP_RPAREN, "`)`");
		--nested_delim_;
		return node;
	}
	if(!At(posttoken::OP_RPAREN))
	{
		ParameterDeclarationList(node);
	}
	Expect(posttoken::OP_RPAREN, "`)`");
	--nested_delim_;
	return node;
}

void Parser::ParameterDeclarationList(int parent)
{
	for(;;)
	{
		if(At(posttoken::OP_DOTS))
		{
			Add(parent, Named("parameter-pack", "..."));
			Advance();
			break;
		}
		Add(parent, ParameterDeclaration());
		if(!Accept(posttoken::OP_COMMA))
		{
			break;
		}
	}
}

int Parser::ParameterDeclaration()
{
	const int node = Tag("parameter-declaration");
	if(At(posttoken::OP_DOTS))
	{
		Add(node, Named("ellipsis", "..."));
		Advance();
		return node;
	}
	bool saw_type = false;
	bool saw_typedef = false;
	const int specifiers = DeclSpecifierSeq(saw_type, saw_typedef);
	if(specifiers == kNoSyntaxNode)
	{
		throw SyntaxError("expected a parameter declaration");
	}
	Add(node, specifiers);
	SkipAttributes();
	const int declarator = ParameterLikeDeclarator();
	if(declarator != kNoSyntaxNode)
	{
		Add(node, declarator);
	}
	SkipAttributes();
	// The pack marker written after the type but before the name, as in
	// `Args... args`, has no declarator to sit in.
	if(At(posttoken::OP_DOTS))
	{
		Add(node, Named("parameter-pack", "..."));
		Advance();
	}
	if(At(posttoken::OP_ASS))
	{
		const int init = Tag("default-argument");
		Add(init, Initializer());
		Add(node, init);
	}
	return node;
}

// An abstract declarator in a parameter prints as `declarator` when it carries
// a pointer operator, and as `abstract-declarator` when it is a suffix form.
int Parser::AbstractDeclaratorInParameter()
{
	return AbstractDeclaratorBody(true);
}

// A parameter, a handler's declaration and a condition all take the same
// declarator-or-abstract-declarator pair, and the named form wins whenever the
// tokens read as one: `char* p` is a named parameter, `char*` is not.
int Parser::ParameterLikeDeclarator()
{
	const Mark mark = Take();
	int declarator = kNoSyntaxNode;
	bool named = false;
	try
	{
		declarator = Declarator();
		named = At(posttoken::OP_COMMA) || At(posttoken::OP_RPAREN) ||
		        At(posttoken::OP_ASS) || At(posttoken::OP_DOTS) ||
		        At(posttoken::OP_COLON) || At(posttoken::OP_LBRACE);
	}
	catch(const SyntaxError&)
	{
		named = false;
	}
	if(named)
	{
		Bind(declarator_name_, kNameValue);
		return declarator;
	}
	Rollback(mark);
	if(At(posttoken::OP_STAR) || At(posttoken::OP_AMP) || At(posttoken::OP_LAND) ||
	   At(posttoken::OP_LPAREN) || At(posttoken::OP_LSQUARE))
	{
		return AbstractDeclaratorInParameter();
	}
	return kNoSyntaxNode;
}

int Parser::AbstractDeclaratorInTypeId()
{
	return AbstractDeclaratorBody(false);
}

int Parser::AbstractDeclaratorBody(bool in_parameter)
{
	const Mark mark = Take();
	const int node = Tag("abstract-declarator");
	bool any_ptr = false;
	while(At(posttoken::OP_STAR) || At(posttoken::OP_AMP) || At(posttoken::OP_LAND))
	{
		any_ptr = true;
		Add(node, PtrOperator());
		Advance();
		while(IsCvQualifierKind(KindAt()))
		{
			Add(node, Terminal("cv-qualifier", Current()));
			Advance();
		}
	}
	bool group = false;
	if(At(posttoken::OP_LPAREN) && (At(posttoken::OP_STAR, 1) ||
	   At(posttoken::OP_AMP, 1) || At(posttoken::OP_LAND, 1) ||
	   At(posttoken::OP_LPAREN, 1) || At(posttoken::OP_LSQUARE, 1)))
	{
		const Mark group_mark = Take();
		Advance();
		const int nested = Tag("nested-declarator");
		bool ok = true;
		try
		{
			Add(nested, AbstractDeclaratorBody(in_parameter));
		}
		catch(const SyntaxError&)
		{
			ok = false;
		}
		if(!ok || !At(posttoken::OP_RPAREN))
		{
			Rollback(group_mark);
		}
		else
		{
			Advance();
			Add(node, nested);
			group = true;
		}
	}
	if(!any_ptr && !group && !At(posttoken::OP_LPAREN) && !At(posttoken::OP_LSQUARE))
	{
		Rollback(mark);
		return Tag("abstract-declarator");
	}
	for(;;)
	{
		if(At(posttoken::OP_LSQUARE))
		{
			Add(node, DeclaratorSuffix(node));
			continue;
		}
		if(At(posttoken::OP_LPAREN))
		{
			Add(node, ParameterClause());
			while(StartsFunctionSuffix())
			{
				Add(node, FunctionSuffix(false));
			}
			continue;
		}
		break;
	}
	// A pointer operator anywhere in the abstract declarator makes it print as
	// a `declarator` in a parameter; a bare suffix form stays an
	// `abstract-declarator`.
	if(in_parameter && PtrOperatorsIn(node) > 0)
	{
		arena_.PutTag(node, "declarator");
	}
	return node;
}

// Counts the `ptr-operator` nodes in a subtree, which is what decides how an
// abstract declarator in a parameter prints.
int Parser::PtrOperatorsIn(int node) const
{
	int count = 0;
	const vector<int>& children = arena_.Node(node).children;
	for(size_t index = 0; index < children.size(); ++index)
	{
		const int child = children[index];
		if(arena_.Text(arena_.Node(child).tag) == "ptr-operator")
		{
			++count;
		}
		count += PtrOperatorsIn(child);
	}
	return count;
}

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

int Parser::TypeId()
{
	const int node = Tag("type-id");
	Add(node, TypeSpecifierSeq());
	if(At(posttoken::OP_STAR) || At(posttoken::OP_AMP) || At(posttoken::OP_LAND) ||
	   At(posttoken::OP_LPAREN) || At(posttoken::OP_LSQUARE))
	{
		Add(node, AbstractDeclaratorInTypeId());
	}
	return node;
}

// `conversion-function-id`'s type-id: a conversion function's name never
// carries a parameter clause, so the declarator part here is the pointer
// operators and array suffixes a `type-id` allows at that point and nothing
// else.
int Parser::ConversionTypeId()
{
	const int node = Tag("type-id");
	Add(node, TypeSpecifierSeq());
	const int abstract_node = Tag("abstract-declarator");
	while(At(posttoken::OP_STAR) || At(posttoken::OP_AMP) || At(posttoken::OP_LAND))
	{
		Add(abstract_node, PtrOperator());
		Advance();
		while(IsCvQualifierKind(KindAt()))
		{
			Add(abstract_node, Terminal("cv-qualifier", Current()));
			Advance();
		}
	}
	bool any = false;
	for(;;)
	{
		if(At(posttoken::OP_LSQUARE))
		{
			Add(abstract_node, DeclaratorSuffix(abstract_node));
			any = true;
			continue;
		}
		break;
	}
	if(!any && arena_.Node(abstract_node).children.empty())
	{
		arena_.DropTo(static_cast<size_t>(abstract_node));
		return node;
	}
	Add(node, abstract_node);
	return node;
}

int Parser::TypeSpecifierSeq()
{
	const int node = Tag("type-specifier-seq");
	bool any = false;
	for(;;)
	{
		const int kind = KindAt();
		if(IsSimpleTypeSpecifierKind(kind))
		{
			Add(node, Terminal("type-specifier", Current()));
			Advance();
			any = true;
			continue;
		}
		if(IsCvQualifierKind(kind))
		{
			Add(node, Terminal("cv-qualifier", Current()));
			Advance();
			any = true;
			continue;
		}
		if(kind == posttoken::KW_DECLTYPE)
		{
			Add(node, DecltypeSpecifier());
			any = true;
			continue;
		}
		if(kind == kIdentifierToken || kind == posttoken::OP_COLON2)
		{
			const size_t start = Position();
			bool seen = false;
			QualifiedTypeName(seen, false);
			if(!seen)
			{
				break;
			}
			Add(node, Named("type-name", JoinedText(start, EndPosition())));
			any = true;
			continue;
		}
		if(IsClassKeyKind(kind))
		{
			Add(node, ElaboratedTypeSpecifier());
			any = true;
			continue;
		}
		if(At(posttoken::KW_TYPENAME))
		{
			Advance();
			continue;
		}
		break;
	}
	if(!any)
	{
		throw SyntaxError("expected a type specifier");
	}
	return node;
}

// True when the tokens at `offset` begin a type-id whose final name is a type:
// `N::probe` is not one when `probe` is a value, which is what tells
// `sizeof(N::probe(x))` from `sizeof(N::T)`.
bool Parser::AtQualifiedTypeStart(size_t offset) const
{
	size_t cursor = offset;
	for(;;)
	{
		if(KindAt(cursor) != kIdentifierToken)
		{
			return AtTypeSpecifierStart(offset);
		}
		const bool is_type = IsTypeName(Spelling(cursor));
		++cursor;
		if(At(posttoken::OP_LT, cursor))
		{
			int depth = 0;
			do
			{
				if(At(posttoken::OP_LT, cursor))
				{
					++depth;
				}
				else if(At(posttoken::OP_GT, cursor) || At(posttoken::OP_RSHIFT, cursor))
				{
					--depth;
				}
				++cursor;
			} while(depth > 0 && KindAt(cursor) != kEofToken);
		}
		if(At(posttoken::OP_COLON2, cursor))
		{
			++cursor;
			continue;
		}
		return is_type;
	}
}

bool Parser::AtTypeSpecifierStart(size_t offset) const
{
	const int kind = KindAt(offset);
	if(IsSimpleTypeSpecifierKind(kind) || IsCvQualifierKind(kind) ||
	   kind == posttoken::KW_DECLTYPE || kind == posttoken::OP_COLON2 ||
	   kind == posttoken::KW_ENUM || kind == posttoken::KW_TYPENAME)
	{
		return true;
	}
	if(kind == kIdentifierToken)
	{
		return IsTypeName(Spelling(offset));
	}
	return IsClassKeyKind(kind);
}

// The last component of a `nested-name-specifier`, which is what a constructor
// definition's name has to match.
string Parser::NestedNameSpecifier(bool& present)
{
	present = false;
	string component;
	// `nested-name-specifier-root`, then any number of suffixes.  A component
	// may be a plain name or a simple-template-id, so `Box<T>::` and `N::` are
	// both roots and both suffixes.
	if(At(posttoken::OP_COLON2))
	{
		present = true;
		Advance();
	}
	else if(At(kIdentifierToken))
	{
		const Mark mark = Take();
		const string candidate = Spelling();
		Advance();
		if(At(posttoken::OP_LT) && TryTemplateIdTail() && At(posttoken::OP_COLON2))
		{
			present = true;
			component = candidate;
			Advance();
		}
		else if(At(posttoken::OP_COLON2))
		{
			present = true;
			component = candidate;
			Advance();
		}
		else
		{
			Rollback(mark);
			return component;
		}
	}
	else if(At(posttoken::KW_DECLTYPE))
	{
		const Mark decltype_mark = Take();
		DecltypeSpecifier();
		if(!At(posttoken::OP_COLON2))
		{
			Rollback(decltype_mark);
			return component;
		}
		present = true;
		Advance();
	}
	else
	{
		return component;
	}
	for(;;)
	{
		if(At(kIdentifierToken) && At(posttoken::OP_LT, 1))
		{
			const Mark mark = Take();
			const string candidate = Spelling();
			Advance();
			if(TryTemplateIdTail() && At(posttoken::OP_COLON2))
			{
				component = candidate;
				Advance();
				continue;
			}
			Rollback(mark);
		}
		if(At(kIdentifierToken) && At(posttoken::OP_COLON2, 1))
		{
			component = Spelling();
			Advance();
			Advance();
			continue;
		}
		break;
	}
	return component;
}

int Parser::QualifiedTypeName(bool& seen, bool require_type)
{
	seen = false;
	if(At(posttoken::KW_DECLTYPE))
	{
		DecltypeSpecifier();
		seen = true;
		return kNoSyntaxNode;
	}
	bool present = false;
	NestedNameSpecifier(present);
	if(!At(kIdentifierToken))
	{
		return kNoSyntaxNode;
	}
	if(!present && require_type && !IsTypeName(Spelling()))
	{
		return kNoSyntaxNode;
	}
	seen = true;
	last_type_name_ = Spelling();
	Advance();
	if(At(posttoken::OP_LT))
	{
		TryTemplateIdTail();
	}
	return kNoSyntaxNode;
}

int Parser::DecltypeSpecifier()
{
	const size_t start = Position();
	const int node = Tag("decltype-specifier");
	Expect(posttoken::KW_DECLTYPE, "`decltype`");
	Expect(posttoken::OP_LPAREN, "`(`");
	Add(node, Expression());
	Expect(posttoken::OP_RPAREN, "`)`");
	arena_.SetLabel(node, JoinedText(start, EndPosition()));
	return node;
}

// True when the innermost angle list was opened by a speculative template-id
// that has already consumed a logical operator: a `>` there is then an
// operator, not the list's closer.
bool Parser::AngleGuardSuspended() const
{
	return !angle_speculative_.empty() && angle_speculative_.back() != 0 &&
	       angle_logical_.back() != 0;
}

void Parser::NoteLogicalInAngle()
{
	if(angle_depth_ > 0 && !angle_logical_.empty())
	{
		angle_logical_.back() = 1;
	}
}

void Parser::EnterAngle()
{
	++angle_depth_;
	angle_delims_.push_back(nested_delim_);
	nested_delim_ = 0;
	angle_speculative_.push_back(next_angle_speculative_ ? 1 : 0);
	angle_logical_.push_back(0);
	next_angle_speculative_ = false;
}

void Parser::LeaveAngle()
{
	if(angle_depth_ > 0)
	{
		--angle_depth_;
	}
	if(!angle_speculative_.empty())
	{
		angle_speculative_.pop_back();
	}
	if(!angle_logical_.empty())
	{
		angle_logical_.pop_back();
	}
	if(!angle_delims_.empty())
	{
		nested_delim_ = angle_delims_.back();
		angle_delims_.pop_back();
	}
}

void Parser::CloseAngle()
{
	if(At(posttoken::OP_RSHIFT))
	{
		rshift_split_ = true;
		return;
	}
	Expect(posttoken::OP_GT, "`>`");
}

bool Parser::TryTemplateIdTail(bool speculative)
{
	if(!At(posttoken::OP_LT))
	{
		return false;
	}
	const Mark mark = Take();
	Advance();
	next_angle_speculative_ = speculative;
	EnterAngle();
	try
	{
		if(!At(posttoken::OP_GT) && !At(posttoken::OP_RSHIFT))
		{
			TemplateArgumentList();
		}
		CloseAngle();
		LeaveAngle();
		return true;
	}
	catch(const SyntaxError&)
	{
		LeaveAngle();
		Rollback(mark);
		return false;
	}
}

void Parser::TemplateArgumentList()
{
	for(;;)
	{
		TemplateArgument();
		if(!Accept(posttoken::OP_COMMA))
		{
			break;
		}
	}
}

// The choice between `type-id` and `assignment-expression` is the one place the
// grammar needs later semantics, so it is made speculatively: an argument that
// reads as a type-id and stops at `,` or the closer is one.
void Parser::TemplateArgument()
{
	if(AtTypeSpecifierStart())
	{
		const Mark mark = Take();
		bool ok = false;
		try
		{
			TypeId();
			ok = At(posttoken::OP_COMMA) || At(posttoken::OP_GT) || At(posttoken::OP_RSHIFT) ||
			     At(posttoken::OP_DOTS);
		}
		catch(const SyntaxError&)
		{
			ok = false;
		}
		if(ok)
		{
			// A pack expansion argument keeps the `...` its operand wrote.
			Accept(posttoken::OP_DOTS);
			return;
		}
		Rollback(mark);
	}
	AssignmentExpression();
	Accept(posttoken::OP_DOTS);
}

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

bool Parser::AtConversionTypeStart() const
{
	const int kind = KindAt();
	if(IsSimpleTypeSpecifierKind(kind) || IsCvQualifierKind(kind) ||
	   kind == posttoken::KW_DECLTYPE || kind == posttoken::OP_COLON2)
	{
		return true;
	}
	if(kind == posttoken::KW_ENUM || IsClassKeyKind(kind))
	{
		return true;
	}
	// A conversion function's type may be any name, declared or not: an
	// identifier can never be an operator token, so nothing else is possible.
	return kind == kIdentifierToken;
}

int Parser::UnqualifiedId(const char* tag)
{
	const size_t start = Position();
	if(At(posttoken::KW_OPERATOR))
	{
		Advance();
		string name("operator");
		if(At(kLiteralToken))
		{
			name += Spelling();
			Advance();
			if(At(kIdentifierToken))
			{
				name += Spelling();
				Advance();
			}
			if(At(posttoken::OP_LT) && TryTemplateIdTail())
			{
				return Named(tag, JoinedText(start, EndPosition()));
			}
			return Named(tag, name);
		}
		if(At(posttoken::KW_NEW) || At(posttoken::KW_DELETE))
		{
			name += Spelling();
			Advance();
			if(Accept(posttoken::OP_LSQUARE))
			{
				name += "[]";
				Expect(posttoken::OP_RSQUARE, "`]`");
			}
			return Named(tag, name);
		}
		if(At(posttoken::OP_LPAREN))
		{
			name += "()";
			Advance();
			Expect(posttoken::OP_RPAREN, "`)`");
			return Named(tag, name);
		}
		if(At(posttoken::OP_LSQUARE))
		{
			name += "[]";
			Advance();
			Expect(posttoken::OP_RSQUARE, "`]`");
			return Named(tag, name);
		}
		if(AtConversionTypeStart())
		{
			const size_t type_start = Position();
			ConversionTypeId();
			name += JoinedText(type_start, EndPosition());
			return Named(tag, name);
		}
		if(IsOperatorTokenKind(KindAt()))
		{
			Advance();
			if(At(posttoken::OP_LT) && TryTemplateIdTail())
			{
				return Named(tag, JoinedText(start, EndPosition()));
			}
			return Named(tag, name + TextAt(start + 1));
		}
		throw SyntaxError("expected an operator name");
	}
	if(At(posttoken::OP_COMPL))
	{
		Advance();
		if(At(posttoken::KW_DECLTYPE))
		{
			DecltypeSpecifier();
			return Named(tag, JoinedText(start, EndPosition()));
		}
		const string name = Spelling();
		Expect(kIdentifierToken, "a class name");
		return Named(tag, "~" + name);
	}
	if(At(kIdentifierToken))
	{
		const string name = Spelling();
		if(!IsKnownValue(name) && At(posttoken::OP_LT, 1))
		{
			const Mark mark = Take();
			Advance();
			if(TryTemplateIdTail(true))
			{
				return Named(tag, JoinedText(start, EndPosition()));
			}
			Rollback(mark);
		}
		Advance();
		return Named(tag, name);
	}
		throw SyntaxError("expected a name at token " + to_string(pos_) + " (`" + Spelling() +
	                  "`)");
}

string Parser::TextAt(size_t index) const
{
	return index < tokens_.size() ? tokens_[index].spelling : string();
}

int Parser::IdExpression(const char* tag)
{
	const size_t start = Position();
	const Mark mark = Take();
	bool present = false;
	NestedNameSpecifier(present);
	const bool qualified = present;
	if(!qualified)
	{
		Rollback(mark);
	}
	else
	{
		Accept(posttoken::KW_TEMPLATE);
	}
	const int node = UnqualifiedId(tag);
	if(qualified)
	{
		arena_.SetLabel(node, JoinedText(start, EndPosition()));
	}
	return node;
}

}  // namespace syntax
}  // namespace cppgm
