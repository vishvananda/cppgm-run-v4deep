// The PA5 recursive-descent parser over `pa5.gram`.
//
// One token vector in, one structured tree out.  The name-category table is
// the syntactic boundary `parsing.md` describes: a stack of scopes recording
// what each declared name denotes, with the documented lexical fallback for a
// name no declaration has bound.

#pragma once

#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "syntax/syntax_arena.h"
#include "syntax/syntax_token.h"

namespace cppgm
{
namespace syntax
{

// A source that does not match the accepted syntax, or one whose parse needs a
// semantic decision PA5 does not make.  The driver turns it into EXIT_FAILURE.
class SyntaxError : public std::runtime_error
{
public:
	explicit SyntaxError(const std::string& what)
		: std::runtime_error(what)
	{}
};

// What a name denotes, as far as syntax alone can tell.
enum ENameKind
{
	kNameUnknown = 0,
	kNameType,
	kNameTemplate,
	kNameValue,
	kNameNamespace
};

class Parser
{
public:
	Parser(const std::vector<SyntaxToken>& tokens, SyntaxArena& arena);

	int Run();

private:
	// --- cursor ---------------------------------------------------------
	int KindAt(std::size_t offset = 0) const;
	bool At(int kind, std::size_t offset = 0) const;
	bool AtAny(int first, int second, std::size_t offset = 0) const;
	bool AtEof() const;
	const SyntaxToken& TokenAt(std::size_t offset = 0) const;
	std::string Spelling(std::size_t offset = 0) const;
	const SyntaxToken& Current() const;
	void Advance();
	bool Accept(int kind);
	void Expect(int kind, const char* what);
	std::size_t Position() const
	{
		return pos_;
	}
	std::size_t EndPosition() const;

	std::string JoinedText(std::size_t first, std::size_t last) const;

	// --- rollback -------------------------------------------------------
	struct Mark
	{
		std::size_t pos;
		bool rshift;
		std::size_t nodes;
		std::size_t scopes;
		std::size_t scope_size;
		std::size_t classes;
	};

	Mark Take() const;
	void Rollback(const Mark& mark);

	// --- nodes ----------------------------------------------------------
	int Tag(const char* name)
	{
		return arena_.Make(name);
	}
	int Named(const char* name, const std::string& label)
	{
		return arena_.Make(name, label);
	}
	int Terminal(const char* name, const SyntaxToken& token)
	{
		return arena_.Make(name, TokenLabel(token));
	}
	static std::string TokenLabel(const SyntaxToken& token);
	void Add(int parent, int child)
	{
		arena_.AddChild(parent, child);
	}

	// --- name categories ------------------------------------------------
	void PushScope();
	void PopScope();
	void Bind(const std::string& name, int kind);
	int Lookup(const std::string& name) const;
	static int Hinted(const std::string& name);
	int NameKind(const std::string& name) const;
	bool IsTypeName(const std::string& name) const;
	bool IsTemplateName(const std::string& name) const;
	bool IsKnownValue(const std::string& name) const;

	// --- translation unit -----------------------------------------------
	int Declaration();
	int DeclarationCommon(bool allow_function_definition);
	int DeclarationBody(bool allow_function_definition);
	int EmptyDeclaration();
	int SkipAttributes();
	bool AtAttributes() const;

	// --- namespaces and aliases -----------------------------------------
	int NamespaceDefinition();
	int NamespaceAliasDefinition();
	int UsingDirective();
	int UsingDeclaration();
	int AliasDeclaration();
	int StaticAssertDeclaration();
	int ExplicitInstantiationDeclaration();
	int LinkageSpecification();

	// --- templates ------------------------------------------------------
	int TemplateDeclaration();
	int TemplateParameterClause();
	void TemplateParameterList(int parent);
	int TemplateParameter();
	void EnterAngle();
	void LeaveAngle();
	void CloseAngle();
	bool TryTemplateIdTail(bool speculative = false);
	void TemplateArgumentList();
	void TemplateArgument();

	// --- classes and enums ----------------------------------------------
	int ClassDeclaration();
	int ClassSpecifier();
	int ClassForwardDeclaration();
	int ElaboratedTypeSpecifier();
	int EnumDeclaration();
	int EnumSpecifier();
	int BaseClause();
	int ClassMember();
	int MemberSpecifiers();
	bool StartsSpecialMember();
	int SpecialMember();
	int SpecialMemberName();
	int AccessSpecifier();
	int CtorInitializer();

	// --- declarations ---------------------------------------------------
	int DeclSpecifierSeq(bool& saw_type, bool& saw_typedef);
	bool CanStartDeclSpecifier() const;
	void InitDeclaratorList(int parent, bool declare_type);
	int Initializer();

	// --- declarators ----------------------------------------------------
	int Declarator();
	int DirectDeclarator(int parent);
	int DeclaratorId();
	int DeclaratorSuffix(int parent);
	bool StartsDeclaratorSuffix(std::size_t offset = 0) const;
	int ParameterClause();
	void ParameterDeclarationList(int parent);
	int ParameterDeclaration();
	int FunctionSuffix(bool lambda_mode);
	bool StartsFunctionSuffix(std::size_t offset = 0, bool lambda_mode = false) const;
	int AbstractDeclaratorInTypeId();
	int AbstractDeclaratorInParameter();
	int ParameterLikeDeclarator();
	int AbstractDeclaratorBody(bool in_parameter);
	int PtrOperatorsIn(int node) const;
	int PtrOperator();

	// --- types ----------------------------------------------------------
	int TypeId();
	int ConversionTypeId();
	int TypeSpecifierSeq();
	int QualifiedTypeName(bool& seen, bool require_type);
	std::string NestedNameSpecifier(bool& present);
	int DecltypeSpecifier();
	bool AtTypeSpecifierStart(std::size_t offset = 0) const;
	// True when the innermost angle list was opened by a speculative
	// template-id that has already consumed a logical operator: a `>` there is
	// then an operator, not the list's closer.
	bool AngleGuardSuspended() const
	{
		return !angle_speculative_.empty() && angle_speculative_.back() != 0 &&
		       angle_logical_.back() != 0;
	}
	void NoteLogicalInAngle()
	{
		if(angle_depth_ > 0 && !angle_logical_.empty())
		{
			angle_logical_.back() = 1;
		}
	}

	// --- names ----------------------------------------------------------
	int IdExpression(const char* tag);
	int UnqualifiedId(const char* tag);
	bool AtConversionTypeStart() const;
	std::string TextAt(std::size_t index) const;

	// --- statements -----------------------------------------------------
	int Statement();
	int CompoundStatement();
	int LabeledStatement();
	int SelectionStatement();
	int IterationStatement();
	int JumpStatement();
	int TryBlock();
	int Handler();
	int Condition();
	int DeclarationOrStatement();
	int ExpressionStatement();
	void CaptureList();
	bool StartsDeclarationStatement() const;

	// --- expressions ----------------------------------------------------
	int Expression();
	int AssignmentExpression();
	int ConditionalExpression();
	int BinaryExpression(int level);
	int UnaryExpression();
	int PostfixExpression();
	int PostfixSuffixes(int node);
	int PrimaryExpression();
	int ArgumentList();
	int ParenArgumentList();
	int BracedInitList();
	int LambdaExpression();
	int NewExpression();
	int DeleteExpression();
	int TypeIdOrExpr(bool& is_type);

	const std::vector<SyntaxToken>& tokens_;
	SyntaxArena& arena_;
	std::size_t pos_;
	bool rshift_split_;
	mutable SyntaxToken split_token_;
	int angle_depth_;
	int nested_delim_;
	std::vector<std::map<std::string, int> > scopes_;
	std::vector<std::string> classes_;
	// The name the declarator just parsed declared, for the declaration that
	// has to bind it.
	std::string declarator_name_;
	bool declarator_is_function_;
	// Nonzero where only a declaration can appear.
	int declaration_only_;
	// Per open angle list: whether it came from a speculative template-id, and
	// whether a `||`, `&&` or `?:` was seen inside it.
	std::vector<char> angle_speculative_;
	std::vector<char> angle_logical_;
	std::vector<int> angle_delims_;
	bool next_angle_speculative_;
};

// Parses one translation unit's tokens and returns the root node, throwing
// `SyntaxError` when the source is outside the PA5 subset.
int ParseTranslationUnit(const std::vector<SyntaxToken>& tokens, SyntaxArena& arena);

// The name a token's kind is spelled with in the PA5 dump: `KW_INT`, `OP_PLUS`,
// `TT_IDENTIFIER`, `TT_LITERAL` or `ST_EOF`.
const char* SyntaxTokenKindName(int kind);

// The small predicates over the token vocabulary the parser shares.
bool IsClassKeyKind(int kind);
bool IsSimpleTypeSpecifierKind(int kind);
bool IsCvQualifierKind(int kind);
bool IsStorageSpecifierKind(int kind);
bool IsMemberFunctionSpecifierKind(int kind);
bool IsOperatorTokenKind(int kind);
bool IsAssignmentOperatorKind(int kind);
bool IsCastKeywordKind(int kind);
bool IsPrefixUnaryOperatorKind(int kind);
int BinaryOperatorLevel(int kind);

}  // namespace syntax
}  // namespace cppgm
