// Statements and expressions.

#include "syntax/syntax_parser.h"

#include <string>

#include "posttoken/simple_token.h"

using namespace std;

namespace cppgm
{
namespace syntax
{

namespace
{

bool IsCv(int kind)
{
	return kind == posttoken::KW_CONST || kind == posttoken::KW_VOLATILE;
}

}  // namespace

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

int Parser::Statement()
{
	const int kind = KindAt();
	if(kind == posttoken::OP_LBRACE)
	{
		return CompoundStatement();
	}
	if(kind == posttoken::KW_IF || kind == posttoken::KW_SWITCH)
	{
		return SelectionStatement();
	}
	if(kind == posttoken::KW_WHILE || kind == posttoken::KW_DO || kind == posttoken::KW_FOR)
	{
		return IterationStatement();
	}
	if(kind == posttoken::KW_BREAK || kind == posttoken::KW_CONTINUE ||
	   kind == posttoken::KW_GOTO || kind == posttoken::KW_RETURN || kind == posttoken::KW_THROW)
	{
		return JumpStatement();
	}
	if(kind == posttoken::KW_TRY)
	{
		return TryBlock();
	}
	if(kind == posttoken::KW_CASE || kind == posttoken::KW_DEFAULT)
	{
		return LabeledStatement();
	}
	if(kind == kIdentifierToken && At(posttoken::OP_COLON, 1))
	{
		return LabeledStatement();
	}
	return DeclarationOrStatement();
}

// A block item is a declaration or a statement, and the two share a prefix.
// The declaration reading wins where it is available; where it is not - as in
// `bool(true);`, which begins like a declaration but has no declarator - the
// statement reading is the one N3485 6.8 leaves.
int Parser::DeclarationOrStatement()
{
	if(StartsDeclarationStatement())
	{
		const Mark mark = Take();
		try
		{
			return Declaration();
		}
		catch(const SyntaxError&)
		{
		}
		Rollback(mark);
	}
	return ExpressionStatement();
}

int Parser::ExpressionStatement()
{
	const int node = Tag("expression-statement");
	if(!At(posttoken::OP_SEMICOLON))
	{
		Add(node, Expression());
	}
	Expect(posttoken::OP_SEMICOLON, "`;`");
	return node;
}

bool Parser::StartsDeclarationStatement() const
{
	if(At(posttoken::OP_SEMICOLON))
	{
		return true;
	}
	if(AtAttributes())
	{
		return true;
	}
	const int kind = KindAt();
	switch(kind)
	{
	case posttoken::KW_NAMESPACE:
	case posttoken::KW_USING:
	case posttoken::KW_TEMPLATE:
	case posttoken::KW_STATIC_ASSERT:
	case posttoken::KW_ENUM:
	case posttoken::KW_CLASS:
	case posttoken::KW_STRUCT:
	case posttoken::KW_UNION:
	case posttoken::KW_TYPEDEF:
	case posttoken::KW_CONSTEXPR:
	case posttoken::KW_THREAD_LOCAL:
	case posttoken::KW_EXTERN:
	case posttoken::KW_STATIC:
	case posttoken::KW_AUTO:
	case posttoken::KW_INLINE:
	case posttoken::KW_VIRTUAL:
	case posttoken::KW_DECLTYPE:
	case posttoken::KW_FRIEND:
		return true;
	default:
		break;
	}
	if(IsSimpleTypeSpecifierKind(kind) || IsCv(kind))
	{
		return true;
	}
	if(kind == kIdentifierToken)
	{
		if(At(posttoken::OP_COLON2, 1))
		{
			return true;
		}
		return IsTypeName(Spelling());
	}
	return false;
}

int Parser::CompoundStatement()
{
	const int node = Tag("compound-statement");
	Expect(posttoken::OP_LBRACE, "`{`");
	PushScope();
	// A block is the one place an expression statement is possible, so the
	// name categories decide the block-item choice here again.
	const int outer = declaration_only_;
	declaration_only_ = 0;
	while(!At(posttoken::OP_RBRACE) && !AtEof())
	{
		Add(node, Statement());
	}
	declaration_only_ = outer;
	PopScope();
	Expect(posttoken::OP_RBRACE, "`}`");
	return node;
}

int Parser::LabeledStatement()
{
	if(At(posttoken::KW_CASE))
	{
		const int node = Tag("case-statement");
		Advance();
		Add(node, Expression());
		Expect(posttoken::OP_COLON, "`:`");
		Add(node, Statement());
		return node;
	}
	if(At(posttoken::KW_DEFAULT))
	{
		const int node = Tag("default-statement");
		Advance();
		Expect(posttoken::OP_COLON, "`:`");
		Add(node, Statement());
		return node;
	}
	const int node = Named("labeled-statement", Spelling());
	Advance();
	Expect(posttoken::OP_COLON, "`:`");
	Add(node, Statement());
	return node;
}

int Parser::SelectionStatement()
{
	if(At(posttoken::KW_IF))
	{
		const int node = Tag("if-statement");
		Advance();
		Expect(posttoken::OP_LPAREN, "`(`");
		Add(node, Condition());
		Expect(posttoken::OP_RPAREN, "`)`");
		Add(node, Named("then", ""));
		const int then = arena_.Last();
		Add(then, Statement());
		if(Accept(posttoken::KW_ELSE))
		{
			const int otherwise = Tag("else");
			Add(otherwise, Statement());
			Add(node, otherwise);
		}
		return node;
	}
	const int node = Tag("switch-statement");
	Expect(posttoken::KW_SWITCH, "`switch`");
	Expect(posttoken::OP_LPAREN, "`(`");
	Add(node, Condition());
	Expect(posttoken::OP_RPAREN, "`)`");
	Add(node, Statement());
	return node;
}

int Parser::IterationStatement()
{
	if(At(posttoken::KW_WHILE))
	{
		const int node = Tag("while-statement");
		Advance();
		Expect(posttoken::OP_LPAREN, "`(`");
		Add(node, Condition());
		Expect(posttoken::OP_RPAREN, "`)`");
		Add(node, Statement());
		return node;
	}
	if(At(posttoken::KW_DO))
	{
		const int node = Tag("do-statement");
		Advance();
		Add(node, Statement());
		Expect(posttoken::KW_WHILE, "`while`");
		Expect(posttoken::OP_LPAREN, "`(`");
		Add(node, Condition());
		Expect(posttoken::OP_RPAREN, "`)`");
		Expect(posttoken::OP_SEMICOLON, "`;`");
		return node;
	}
	// `for ( for-init-statement condition? ; expression? ) statement`, or the
	// range form `for ( for-range-declaration : expression ) statement`.
	const int node = Tag("for-statement");
	Expect(posttoken::KW_FOR, "`for`");
	Expect(posttoken::OP_LPAREN, "`(`");
	int init = Tag("for-init-statement");
	if(At(posttoken::OP_SEMICOLON))
	{
		Advance();
	}
	else if(StartsDeclarationStatement())
	{
		// A `for` init that is a declaration stops at `:` when the range form
		// follows, and at `;` otherwise.
		const Mark mark = Take();
		bool range = false;
		try
		{
			bool saw_type = false;
			bool saw_typedef = false;
			const int specifiers = DeclSpecifierSeq(saw_type, saw_typedef);
			const int declarator = Declarator();
			if(specifiers != kNoSyntaxNode && At(posttoken::OP_COLON))
			{
				Add(init, specifiers);
				Add(init, declarator);
				Bind(declarator_name_, saw_typedef ? kNameType : kNameValue);
				range = true;
			}
		}
		catch(const SyntaxError&)
		{
			range = false;
		}
		if(!range)
		{
			Rollback(mark);
			init = Tag("for-init-statement");
			Add(init, Declaration());
		}
	}
	else
	{
		if(!At(posttoken::OP_SEMICOLON))
		{
			Add(init, Expression());
		}
		Expect(posttoken::OP_SEMICOLON, "`;`");
	}
	if(At(posttoken::OP_COLON))
	{
		// A range-based for: the init is the range declaration.
		Advance();
		arena_.PutTag(init, "range-declaration");
		arena_.PutTag(node, "range-for-statement");
		Add(node, init);
		const int range = Tag("range-initializer");
		Add(range, Expression());
		Add(node, range);
		Expect(posttoken::OP_RPAREN, "`)`");
		Add(node, Statement());
		return node;
	}
	Add(node, init);
	if(!At(posttoken::OP_SEMICOLON))
	{
		Add(node, Condition());
	}
	Expect(posttoken::OP_SEMICOLON, "`;`");
	if(!At(posttoken::OP_RPAREN))
	{
		Add(node, Named("iteration", ""));
		const int iteration = arena_.Last();
		Add(iteration, Expression());
	}
	Expect(posttoken::OP_RPAREN, "`)`");
	Add(node, Statement());
	return node;
}

int Parser::JumpStatement()
{
	const int kind = KindAt();
	if(kind == posttoken::KW_BREAK)
	{
		Advance();
		Expect(posttoken::OP_SEMICOLON, "`;`");
		return Tag("break-statement");
	}
	if(kind == posttoken::KW_CONTINUE)
	{
		Advance();
		Expect(posttoken::OP_SEMICOLON, "`;`");
		return Tag("continue-statement");
	}
	if(kind == posttoken::KW_GOTO)
	{
		Advance();
		const int node = Named("goto-statement", Spelling());
		Expect(kIdentifierToken, "a label");
		Expect(posttoken::OP_SEMICOLON, "`;`");
		return node;
	}
	if(kind == posttoken::KW_RETURN)
	{
		const int node = Tag("return-statement");
		Advance();
		if(!At(posttoken::OP_SEMICOLON))
		{
			Add(node, Expression());
		}
		Expect(posttoken::OP_SEMICOLON, "`;`");
		return node;
	}
	const int node = Tag("throw-statement");
	Expect(posttoken::KW_THROW, "`throw`");
	if(!At(posttoken::OP_SEMICOLON))
	{
		Add(node, AssignmentExpression());
	}
	Expect(posttoken::OP_SEMICOLON, "`;`");
	return node;
}

int Parser::TryBlock()
{
	const int node = Tag("try-block");
	Expect(posttoken::KW_TRY, "`try`");
	Add(node, CompoundStatement());
	while(At(posttoken::KW_CATCH))
	{
		Add(node, Handler());
	}
	return node;
}

int Parser::Handler()
{
	const int node = Tag("handler");
	Expect(posttoken::KW_CATCH, "`catch`");
	Expect(posttoken::OP_LPAREN, "`(`");
	const int declaration = Tag("exception-declaration");
	if(At(posttoken::OP_DOTS))
	{
		Add(declaration, Named("ellipsis", "..."));
		Advance();
	}
	else
	{
		bool saw_type = false;
		bool saw_typedef = false;
		Add(declaration, DeclSpecifierSeq(saw_type, saw_typedef));
		const int declarator = ParameterLikeDeclarator();
		if(declarator != kNoSyntaxNode)
		{
			Add(declaration, declarator);
		}
	}
	Add(node, declaration);
	Expect(posttoken::OP_RPAREN, "`)`");
	Add(node, CompoundStatement());
	return node;
}

int Parser::Condition()
{
	// A condition is a declaration or an expression, and the declaration form
	// needs a declarator followed by an initializer.
	const Mark mark = Take();
	bool saw_type = false;
	bool saw_typedef = false;
	const int specifiers = DeclSpecifierSeq(saw_type, saw_typedef);
	if(specifiers != kNoSyntaxNode && saw_type)
	{
		const Mark inner = Take();
		bool ok = false;
		int declarator = kNoSyntaxNode;
		try
		{
			declarator = Declarator();
			ok = At(posttoken::OP_ASS) || At(posttoken::OP_LBRACE) || At(posttoken::OP_LPAREN);
		}
		catch(const SyntaxError&)
		{
			ok = false;
		}
		if(ok)
		{
			const int node = Tag("condition");
			const int declaration = Tag("condition-declaration");
			Add(declaration, specifiers);
			Add(declaration, declarator);
			Add(declaration, Initializer());
			Add(node, declaration);
			return node;
		}
		Rollback(inner);
	}
	Rollback(mark);
	const int node = Tag("condition");
	Add(node, Expression());
	return node;
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

int Parser::Expression()
{
	int left = AssignmentExpression();
	while(At(posttoken::OP_COMMA))
	{
		const int node = Terminal("binary-expression", Current());
		Advance();
		Add(node, left);
		Add(node, AssignmentExpression());
		left = node;
	}
	return left;
}

int Parser::AssignmentExpression()
{
	const int left = ConditionalExpression();
	if(IsAssignmentOperatorKind(KindAt()))
	{
		const int node = Terminal("assignment-expression", Current());
		Advance();
		Add(node, left);
		Add(node, AssignmentExpression());
		return node;
	}
	return left;
}

int Parser::ConditionalExpression()
{
	const int left = BinaryExpression(1);
	if(Accept(posttoken::OP_QMARK))
	{
		NoteLogicalInAngle();
		const int node = Tag("conditional-expression");
		Add(node, left);
		Add(node, Expression());
		Expect(posttoken::OP_COLON, "`:`");
		Add(node, AssignmentExpression());
		return node;
	}
	return left;
}

int Parser::BinaryExpression(int level)
{
	if(level > 11)
	{
		return UnaryExpression();
	}
	int left = BinaryExpression(level + 1);
	for(;;)
	{
		const int kind = KindAt();
		if(BinaryOperatorLevel(kind) != level)
		{
			break;
		}
		// Inside a template argument list a `>` or `>>` at the outermost
		// delimiter level closes the list rather than comparing or shifting,
		// unless a logical operator has already been read in a speculative
		// template-id: `x < y || z > w` is then a comparison, not a template-id.
		if(angle_depth_ > 0 && nested_delim_ == 0 && !AngleGuardSuspended() &&
		   (kind == posttoken::OP_GT || kind == posttoken::OP_RSHIFT))
		{
			break;
		}
		if(level <= 2)
		{
			NoteLogicalInAngle();
		}
		const int node = Terminal("binary-expression", Current());
		Advance();
		Add(node, left);
		Add(node, BinaryExpression(level + 1));
		left = node;
	}
	return left;
}

int Parser::UnaryExpression()
{
	const int kind = KindAt();
	if(kind == posttoken::OP_INC || kind == posttoken::OP_DEC || kind == posttoken::OP_STAR ||
	   kind == posttoken::OP_AMP || kind == posttoken::OP_PLUS || kind == posttoken::OP_MINUS ||
	   kind == posttoken::OP_LNOT || kind == posttoken::OP_COMPL)
	{
		const int node = Terminal("unary-expression", Current());
		Advance();
		Add(node, UnaryExpression());
		return node;
	}
	if(kind == posttoken::KW_SIZEOF)
	{
		Advance();
		if(At(posttoken::OP_DOTS))
		{
			Advance();
			Expect(posttoken::OP_LPAREN, "`(`");
			const int node = Named("sizeof-pack-expression", Spelling());
			Expect(kIdentifierToken, "a pack name");
			Expect(posttoken::OP_RPAREN, "`)`");
			return node;
		}
		const int node = Tag("sizeof-expression");
		// A parenthesized operand is a type-id when one can be read there, and
		// an expression otherwise.  A name directly followed by `(` is the
		// expression reading: `sizeof(S())` is a functional cast, not a
		// function type.
		const bool name_then_paren =
		    AtTypeSpecifierStart(1) && KindAt(1) == kIdentifierToken &&
		    At(posttoken::OP_LPAREN, 2);
		if(At(posttoken::OP_LPAREN) && AtTypeSpecifierStart(1) && !name_then_paren)
		{
			const Mark mark = Take();
			Advance();
			++nested_delim_;
			bool ok = false;
			try
			{
				Add(node, TypeId());
				ok = At(posttoken::OP_RPAREN);
			}
			catch(const SyntaxError&)
			{
				ok = false;
			}
			--nested_delim_;
			if(ok)
			{
				Advance();
				return node;
			}
			Rollback(mark);
		}
		if(At(posttoken::OP_LPAREN))
		{
			// The parentheses around an expression operand are the operator's
			// own, so the node holds the expression and not a
			// parenthesized-expression.
			Advance();
			++nested_delim_;
			Add(node, Expression());
			--nested_delim_;
			Expect(posttoken::OP_RPAREN, "`)`");
			return node;
		}
		Add(node, UnaryExpression());
		return PostfixSuffixes(node);
	}
	if(kind == posttoken::KW_ALIGNOF || kind == posttoken::KW_TYPEID || kind == posttoken::KW_NOEXCEPT)
	{
		const int node = Terminal("type-trait-expression", Current());
		Advance();
		Expect(posttoken::OP_LPAREN, "`(`");
		++nested_delim_;
		if(kind == posttoken::KW_ALIGNOF)
		{
			Add(node, TypeId());
		}
		else
		{
			bool is_type = false;
			Add(node, TypeIdOrExpr(is_type));
		}
		--nested_delim_;
		Expect(posttoken::OP_RPAREN, "`)`");
		return PostfixSuffixes(node);
	}
	if(IsCastKeywordKind(kind))
	{
		const int node = Terminal("cast-expression", Current());
		Advance();
		Expect(posttoken::OP_LT, "`<`");
		EnterAngle();
		Add(node, TypeId());
		CloseAngle();
		LeaveAngle();
		Expect(posttoken::OP_LPAREN, "`(`");
		++nested_delim_;
		Add(node, Expression());
		--nested_delim_;
		Expect(posttoken::OP_RPAREN, "`)`");
		return PostfixSuffixes(node);
	}
	if(kind == posttoken::KW_NEW)
	{
		return PostfixSuffixes(NewExpression());
	}
	if(kind == posttoken::OP_COLON2 && At(posttoken::KW_NEW, 1))
	{
		return PostfixSuffixes(NewExpression());
	}
	if(kind == posttoken::KW_DELETE)
	{
		return PostfixSuffixes(DeleteExpression());
	}
	if(kind == posttoken::OP_COLON2 && At(posttoken::KW_DELETE, 1))
	{
		return PostfixSuffixes(DeleteExpression());
	}
	return PostfixExpression();
}

int Parser::NewExpression()
{
	const int node = Tag("new-expression");
	if(Accept(posttoken::OP_COLON2))
	{
		Add(node, Tag("global-scope"));
	}
	Expect(posttoken::KW_NEW, "`new`");
	if(At(posttoken::OP_LPAREN))
	{
		const int placement = Tag("placement");
		Advance();
		++nested_delim_;
		if(!At(posttoken::OP_RPAREN))
		{
			for(;;)
			{
				Add(placement, AssignmentExpression());
				if(!Accept(posttoken::OP_COMMA))
				{
					break;
				}
			}
		}
		Expect(posttoken::OP_RPAREN, "`)`");
		--nested_delim_;
		Add(node, placement);
	}
	// A parenthesized type-id after `new` is a pointer-to-array new; the
	// initializer's own `(` must not be read as a declarator, so the type here
	// is the restricted one.
	Add(node, ConversionTypeId());
	if(At(posttoken::OP_LBRACE))
	{
		const int init = Tag("initializer");
		Add(init, BracedInitList());
		Add(node, init);
	}
	else if(At(posttoken::OP_LPAREN))
	{
		const int init = Tag("initializer");
		const int paren = Tag("paren-initializer");
		Advance();
		++nested_delim_;
		if(!At(posttoken::OP_RPAREN))
		{
			for(;;)
			{
				Add(paren, AssignmentExpression());
				if(!Accept(posttoken::OP_COMMA))
				{
					break;
				}
			}
		}
		Expect(posttoken::OP_RPAREN, "`)`");
		--nested_delim_;
		Add(init, paren);
		Add(node, init);
	}
	return PostfixSuffixes(node);
}

int Parser::DeleteExpression()
{
	const int node = Tag("delete-expression");
	if(Accept(posttoken::OP_COLON2))
	{
		Add(node, Tag("global-scope"));
	}
	Expect(posttoken::KW_DELETE, "`delete`");
	if(Accept(posttoken::OP_LSQUARE))
	{
		Add(node, Tag("array-delete"));
		Expect(posttoken::OP_RSQUARE, "`]`");
	}
	Add(node, UnaryExpression());
	return node;
}

// `sizeof` and `typeid` take either a type-id or an expression; the type-id is
// tried first because the grammar puts it first.
int Parser::TypeIdOrExpr(bool& is_type)
{
	if(AtTypeSpecifierStart())
	{
		const Mark mark = Take();
		try
		{
			const int type = TypeId();
			if(At(posttoken::OP_RPAREN) || At(posttoken::OP_COMMA))
			{
				is_type = true;
				return type;
			}
		}
		catch(const SyntaxError&)
		{
		}
		Rollback(mark);
	}
	is_type = false;
	return Expression();
}

int Parser::PostfixExpression()
{
	return PostfixSuffixes(PrimaryExpression());
}

int Parser::PostfixSuffixes(int node)
{
	for(;;)
	{
		const int kind = KindAt();
		if(kind == posttoken::OP_LPAREN)
		{
			const int call = Tag("call-expression");
			Add(call, node);
			Add(call, ArgumentList());
			node = call;
			continue;
		}
		if(kind == posttoken::OP_LSQUARE)
		{
			const int subscript = Tag("subscript-expression");
			Add(subscript, node);
			Advance();
			++nested_delim_;
			Add(subscript, Expression());
			--nested_delim_;
			Expect(posttoken::OP_RSQUARE, "`]`");
			node = subscript;
			continue;
		}
		if(kind == posttoken::OP_DOT || kind == posttoken::OP_ARROW)
		{
			const int member = Terminal("member-expression", Current());
			Advance();
			Add(member, node);
			const size_t name_start = Position();
			const bool dependent = Accept(posttoken::KW_TEMPLATE);
			const int name = IdExpression("identifier");
			if(dependent)
			{
				arena_.SetLabel(name, JoinedText(name_start, EndPosition()));
			}
			Add(member, name);
			node = member;
			continue;
		}
		if(kind == posttoken::OP_INC || kind == posttoken::OP_DEC)
		{
			const int post = Terminal("postfix-expression", Current());
			Advance();
			Add(post, node);
			node = post;
			continue;
		}
		break;
	}
	return node;
}

int Parser::ParenArgumentList()
{
	const int node = Tag("paren-argument-list");
	Expect(posttoken::OP_LPAREN, "`(`");
	++nested_delim_;
	if(!At(posttoken::OP_RPAREN))
	{
		for(;;)
		{
			if(At(posttoken::OP_LBRACE))
			{
				Add(node, BracedInitList());
			}
			else
			{
				Add(node, AssignmentExpression());
			}
			if(!Accept(posttoken::OP_COMMA))
			{
				break;
			}
		}
	}
	Expect(posttoken::OP_RPAREN, "`)`");
	--nested_delim_;
	return node;
}

int Parser::ArgumentList()
{
	const int node = Tag("argument-list");
	Expect(posttoken::OP_LPAREN, "`(`");
	++nested_delim_;
	if(!At(posttoken::OP_RPAREN))
	{
		for(;;)
		{
			if(At(posttoken::OP_LBRACE))
			{
				Add(node, BracedInitList());
			}
			else
			{
				Add(node, AssignmentExpression());
			}
			if(!Accept(posttoken::OP_COMMA))
			{
				break;
			}
		}
	}
	Expect(posttoken::OP_RPAREN, "`)`");
	--nested_delim_;
	return node;
}

int Parser::BracedInitList()
{
	const int node = Tag("braced-init-list");
	Expect(posttoken::OP_LBRACE, "`{`");
	++nested_delim_;
	if(!At(posttoken::OP_RBRACE))
	{
		for(;;)
		{
			if(At(posttoken::OP_LBRACE))
			{
				Add(node, BracedInitList());
			}
			else
			{
				Add(node, AssignmentExpression());
			}
			if(!Accept(posttoken::OP_COMMA))
			{
				break;
			}
			if(At(posttoken::OP_RBRACE))
			{
				break;
			}
		}
	}
	Expect(posttoken::OP_RBRACE, "`}`");
	--nested_delim_;
	return node;
}

int Parser::PrimaryExpression()
{
	const int kind = KindAt();
	if(kind == kLiteralToken)
	{
		const int node = Named("literal", Spelling());
		Advance();
		return node;
	}
	if(kind == posttoken::KW_TRUE || kind == posttoken::KW_FALSE ||
	   kind == posttoken::KW_NULLPTR || kind == posttoken::KW_THIS)
	{
		const int node = Terminal("keyword-literal", Current());
		Advance();
		return node;
	}
	// A builtin type name applied to a parenthesized argument list is the
	// function-style cast, spelled as a call whose callee is the type name.
	if(IsSimpleTypeSpecifierKind(kind) && At(posttoken::OP_LPAREN, 1))
	{
		const int node = Tag("call-expression");
		Add(node, Named("id-expression", Spelling()));
		Advance();
		Add(node, ParenArgumentList());
		return node;
	}
	if(kind == posttoken::OP_LPAREN)
	{
		const Mark mark = Take();
		Advance();
		// A C-style cast is `( type-id ) unary-expression`; anything else is a
		// parenthesized expression.  The cast attempt runs before the
		// delimiter count changes, so every increment below is matched.
		if(AtTypeSpecifierStart())
		{
			bool ok = false;
			int type = kNoSyntaxNode;
			try
			{
				type = TypeId();
				ok = At(posttoken::OP_RPAREN);
			}
			catch(const SyntaxError&)
			{
				ok = false;
			}
			if(ok)
			{
				const Mark after = Take();
				Advance();
				++nested_delim_;
				bool cast = false;
				try
				{
					UnaryExpression();
					cast = true;
				}
				catch(const SyntaxError&)
				{
					cast = false;
				}
				--nested_delim_;
				Rollback(after);
				if(cast)
				{
					const int node = Named("cast-expression", "OP_LPAREN:");
					Add(node, type);
					Expect(posttoken::OP_RPAREN, "`)`");
					Add(node, UnaryExpression());
					return node;
				}
			}
		}
		Rollback(mark);
		Advance();
		++nested_delim_;
		const int node = Tag("parenthesized-expression");
		Add(node, Expression());
		Expect(posttoken::OP_RPAREN, "`)`");
		--nested_delim_;
		return node;
	}
	if(kind == posttoken::OP_LSQUARE)
	{
		return LambdaExpression();
	}
	if(kind == posttoken::OP_LBRACE)
	{
		return BracedInitList();
	}
	// `typename` in an expression only marks the name that follows it as a
	// type, which the dump does not keep.
	if(kind == posttoken::KW_TYPENAME)
	{
		Advance();
		return PrimaryExpression();
	}
	if(kind == posttoken::OP_LBRACE)
	{
		return BracedInitList();
	}
	if(At(kIdentifierToken) || At(posttoken::OP_COLON2) ||
	   (At(kIdentifierToken) && At(posttoken::OP_LT, 1)))
	{
		const Mark mark = Take();
		const int name = IdExpression("id-expression");
		if(At(posttoken::OP_LBRACE))
		{
			const int call = Tag("call-expression");
			Add(call, name);
			Add(call, BracedInitList());
			return call;
		}
		Rollback(mark);
	}
	return IdExpression("id-expression");
}

int Parser::LambdaExpression()
{
	const int node = Tag("lambda-expression");
	const size_t start = Position();
	Expect(posttoken::OP_LSQUARE, "`[`");
	if(!At(posttoken::OP_RSQUARE))
	{
		if(Accept(posttoken::OP_AMP))
		{
			if(Accept(posttoken::OP_COMMA))
			{
				CaptureList();
			}
		}
		else if(Accept(posttoken::OP_ASS))
		{
			if(Accept(posttoken::OP_COMMA))
			{
				CaptureList();
			}
		}
		else
		{
			CaptureList();
		}
	}
	Expect(posttoken::OP_RSQUARE, "`]`");
	Add(node, Named("lambda-introducer", JoinedText(start, EndPosition())));
	if(At(posttoken::OP_LPAREN))
	{
		const int declarator = Tag("lambda-declarator");
		Add(declarator, ParameterClause());
		if(Accept(posttoken::KW_MUTABLE))
		{
			Add(declarator, Named("lambda-specifier", "KW_MUTABLE:mutable"));
		}
		while(StartsFunctionSuffix(0, true))
		{
			Add(declarator, FunctionSuffix(true));
		}
		Add(node, declarator);
	}
	else if(At(posttoken::KW_MUTABLE) || At(posttoken::KW_NOEXCEPT) || At(posttoken::OP_ARROW))
	{
		const int declarator = Tag("lambda-declarator");
		if(Accept(posttoken::KW_MUTABLE))
		{
			Add(declarator, Named("lambda-specifier", "KW_MUTABLE:mutable"));
		}
		while(StartsFunctionSuffix(0, true))
		{
			Add(declarator, FunctionSuffix(true));
		}
		Add(node, declarator);
	}
	Add(node, CompoundStatement());
	return node;
}

void Parser::CaptureList()
{
	for(;;)
	{
		if(At(posttoken::KW_THIS))
		{
			Advance();
		}
		else
		{
			Accept(posttoken::OP_AMP);
			if(At(kIdentifierToken))
			{
				Advance();
			}
			else
			{
				throw SyntaxError("expected a capture");
			}
		}
		Accept(posttoken::OP_DOTS);
		if(!Accept(posttoken::OP_COMMA))
		{
			break;
		}
	}
}

}  // namespace syntax
}  // namespace cppgm
