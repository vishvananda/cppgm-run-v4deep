// The PA6 walk over the PA5 tree: build scopes, register declarations, build
// types from declarators, and evaluate the supported constant subset.
//
// The walk is one pass in source order, because a declaration's point matters:
// a later alias must not retroactively change an earlier resolved binding.  The
// one deliberate exception is a class member function body, which is a
// complete-class context (N3485 3.3.7/1): its scope is opened after the whole
// member list, so a member type declared later is visible in it.

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "semantic/semantic_model.h"
#include "syntax/syntax_arena.h"
#include "syntax/syntax_token.h"

namespace cppgm
{
namespace semantic
{

// The integral constant subset of 5.19 the handout requires.  `valid` is false
// for an expression the subset does not cover, which is not itself an error: a
// variable's initializer may be any expression.  A name that denotes nothing is
// an error and is reported where it is looked up.
struct Constant
{
	long long value;
	bool is_signed;
	bool valid;
	int type;

	Constant()
		: value(0)
		, is_signed(true)
		, valid(false)
		, type(-1)
	{}
};

// What a decl-specifier-seq declared: the type it formed, and the specifiers
// that change what the declaration means without changing the type model.
struct Specifiers
{
	int type;
	bool saw_type;
	bool is_typedef;
	bool is_constexpr;
	bool is_extern;
	bool is_static;
	bool is_inline;
	bool is_virtual;
	bool is_thread_local;
	bool is_friend;
	bool is_mutable;

	// The class or enum specifier this sequence introduced, when it did.  The
	// declaration's own class key is what its `type` line prints with.
	int class_specifier;
	int enum_specifier;
	int class_key;
	int enum_key;
	std::string declared_name;

	Specifiers()
		: type(-1)
		, saw_type(false)
		, is_typedef(false)
		, is_constexpr(false)
		, is_extern(false)
		, is_static(false)
		, is_inline(false)
		, is_virtual(false)
		, is_thread_local(false)
		, is_friend(false)
		, is_mutable(false)
		, class_specifier(-1)
		, enum_specifier(-1)
		, class_key(-1)
		, enum_key(-1)
	{}
};

// A member function definition whose body waits for the end of its class.
struct PendingBody
{
	int owner;
	std::string name;
	std::vector<std::pair<std::string, int> > parameters;
	int body;
};

class Analyzer
{
public:
	Analyzer(Model& model, const syntax::SyntaxArena& arena,
	         const syntax::SyntaxSpellingPool& spellings,
	         const std::vector<syntax::SyntaxLiteralFacts>& literals);

	// Analyses one translation unit's tree into the model's global scope.
	void Run(int root);

	// --- tree access ------------------------------------------------------
	const std::string& Tag(int node) const;
	const std::string& Label(int node) const;
	std::size_t ChildCount(int node) const;
	int ChildAt(int node, std::size_t index) const;
	std::vector<int> ChildrenOf(int node) const;
	int FindChild(int node, const char* tag) const;
	bool HasChild(int node, const char* tag) const;
	bool IsTag(int node, const char* tag) const;

private:
	// --- declarations -----------------------------------------------------
	void AnalyzeDeclaration(int node, int scope, int enclosing_class);
	void AnalyzeSimpleDeclaration(int node, int scope, int enclosing_class);
	void AnalyzeFunctionDefinition(int node, int scope, bool defer);
	void AnalyzeNamespaceDefinition(int node, int scope);
	void AnalyzeNamespaceAlias(int node, int scope);
	void AnalyzeUsingDirective(int node, int scope);
	void AnalyzeUsingDeclaration(int node, int scope);
	void AnalyzeAliasDeclaration(int node, int scope);
	void AnalyzeStaticAssert(int node, int scope);
	void AnalyzeTemplateDeclaration(int node, int scope, int enclosing_class);
	void AnalyzeLinkageSpecification(int node, int scope, int enclosing_class);
	void AnalyzeClassForward(int node, int scope);
	void AnalyzeBitField(int node, int scope);
	void AnalyzeSpecialMember(int node, int scope, int enclosing_class);

	// --- specifiers and declarators --------------------------------------
	void AnalyzeSpecifiers(int node, int scope, Specifiers& out,
	                       const std::string& declared_name, bool declare_introduced);
	int BuildDeclarator(int node, int base, int scope);
	int BuildParameterClause(int node, int scope, std::vector<int>& params, bool& varargs,
	                         std::vector<std::pair<std::string, int> >* names);
	int BuildSuffix(int node, int base, int scope, int quals, int func_ref);
	void CollectDeclaratorName(int node, std::string& name);
	void SplitQualifiedName(const std::string& text, std::string& qualifier, std::string& name);

	// --- classes, enums, scopes ------------------------------------------
	int AnalyzeClassSpecifier(int node, int scope, const std::string& declared_name,
	                          bool is_static, int* out_key);
	int AnalyzeEnumSpecifier(int node, int scope, bool declare,
	                         const std::string& declared_name, int* out_key);
	int DeclareClass(int scope, const std::string& written, int key, bool has_body);
	void ProcessClassBody(int node, int class_scope);
	void RunPendingBodies(int class_scope);
	void BindEnumerators(int node, int enum_scope, int enum_type);
	void InjectUnionMembers(int class_scope, int scope);
	void OpenFunctionScope(int owner, const std::string& name,
	                       const std::vector<std::pair<std::string, int> >& parameters, int body);

	// --- statements -------------------------------------------------------
	void AnalyzeCompoundStatement(int node, int scope);
	void AnalyzeStatements(int node, int block);
	void AnalyzeSubstatement(int node, int slot);
	void AnalyzeSlotStatement(int node, int slot);
	void ScanCalls(int node, int scope);
	void NoteClassCall(int node, int scope);
	void AnalyzeStatement(int node, int scope);
	bool IsDeclarationTag(const std::string& tag) const;
	bool IsStatementTag(const std::string& tag) const;

	// --- lookup helpers ---------------------------------------------------
	int ResolveTypeName(int scope, const std::string& text, bool elaborated_class,
	                    bool elaborated_enum);
	int ResolveValueName(int scope, const std::string& text);
	int ResolveNamespaceName(int scope, const std::string& text);
	int EntityType(int entity) const;
	int FindOrCreateObject(int scope, const std::string& name, int type);
	int FindOrCreateFunction(int scope, const std::string& name, int type);
	void CheckFunctionQualifiers(int scope, int type);
	void NoteFunctionDefinition(int entity, const std::string& name);

	// --- constants --------------------------------------------------------
	Constant Evaluate(int node, int scope);
	Constant EvaluateBinary(int node, int scope, const std::string& op);
	Constant EvaluateUnary(int node, int scope, const std::string& op);
	Constant EvaluateLiteral(int node);
	Constant EvaluateIdentifier(int node, int scope);
	Constant EvaluateCall(int node, int scope);
	int EvaluateDecltype(int node, int scope);
	long long ArrayBound(int node, int scope);
	long long TruncateTo(long long value, int fundamental) const;
	bool IsScopedEnum(int type) const;

	// --- helpers ----------------------------------------------------------
	void AddTypeBinding(int scope, const std::string& name, int entity, int class_key,
	                    int enum_key);
	int FundamentalFromSpecifiers(const std::vector<std::string>& words) const;
	std::string AnonymousClassName(int node);
	std::string AnonymousEnumName();
	int CurrentEntityScope(int scope, const std::string& qualifier);
	void CheckQualifiedDefinition(int site, int target);

	Model& model_;
	const syntax::SyntaxArena& arena_;
	const syntax::SyntaxSpellingPool& spellings_;
	const std::vector<syntax::SyntaxLiteralFacts>& literals_;
	std::vector<PendingBody> pending_;
	long long anonymous_enums_;
};

}  // namespace semantic
}  // namespace cppgm
