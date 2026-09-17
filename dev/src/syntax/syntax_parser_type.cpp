// Types, names and template arguments.
//
// A type-id, a nested-name-specifier and a template-id are the productions that
// need the name-category table: whether `T(x)` is a parameter of function type,
// whether `N::m < 2` is a template-id and whether a declaration is a type or an
// expression all come down to what a name denotes.  This unit also owns the
// angle state, because a template-argument list is where a `>` stops being an
// operator.

#include "syntax/syntax_parser.h"

#include <cstddef>
#include <string>

#include "posttoken/simple_token.h"

using namespace std;

namespace cppgm
{
namespace syntax
{

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
	if(!At(posttoken::OP_GT))
	{
		throw SyntaxError("expected `>` at token " + to_string(pos_) + " (`" + Spelling() +
		                  "`)");
	}
	Advance();
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

int Parser::UnqualifiedId(const char* tag, bool qualified, bool template_keyword)
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
		// N3485 14.2/2: a name may be read as a template-name only where
		// lookup found one, or where the keyword `template` said so.  An
		// unqualified name is the exception the course's lexical fallback
		// needs - a template parameter's own name is not declared a template
		// either - and a failed speculation rolls everything back.
		//
		// A qualified name that is not known to be a template keeps the `>`
		// only where it could be the delimiter of an enclosing argument list,
		// so `ic<bool, R1::num < 2>` closes at that `>`; the template-id
		// reading would have swallowed it and lost the enclosing list.  Any
		// other context has no list to close, as in `std::g<int>()`.
		const bool in_argument_list = angle_depth_ > 0 && nested_delim_ == 0;
		const bool template_name = !qualified || template_keyword ||
		                           NameKind(name) == kNameTemplate || !in_argument_list;
		if(template_name && !IsKnownValue(name) && At(posttoken::OP_LT, 1))
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
	bool template_keyword = false;
	if(!qualified)
	{
		Rollback(mark);
	}
	else
	{
		template_keyword = Accept(posttoken::KW_TEMPLATE);
	}
	const int node = UnqualifiedId(tag, qualified, template_keyword);
	if(qualified)
	{
		arena_.SetLabel(node, JoinedText(start, EndPosition()));
	}
	return node;
}

}  // namespace syntax
}  // namespace cppgm
