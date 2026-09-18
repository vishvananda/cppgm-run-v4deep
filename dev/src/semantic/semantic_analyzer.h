// The PA6 walk over the PA5 tree: build scopes, register declarations, build
// types from declarators, and evaluate the supported constant subset.
//
// The walk is one pass in source order, because a declaration's point matters:
// a later alias must not retroactively change an earlier resolved binding.  The
// one deliberate exception is a class member function body, which is a
// complete-class context (N3485 3.3.7/1): its scope is opened after the whole
// member list, so a member type declared later is visible in it.

#pragma once

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "semantic/semantic_model.h"
#include "semantic/semantics_tree.h"
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

	// PA7 gives an unnamed class type a synthetic name of its own rather than
	// the name of the declarator that uses it, so the class and the object it
	// declares are two different names.  The dump mode selects that reading.
	void SetSemanticsMode(bool semantics)
	{
		semantics_mode_ = semantics;
	}

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
	int MemberPointerClass(const std::string& spelling, int scope);
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
	void NoteFunctionDefinition(int entity, const std::string& name, int scope, int type);
	void AnalyzeCondition(int node, int scope);

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

	// --- helpers ----------------------------------------------------------
	void AddTypeBinding(int scope, const std::string& name, int entity, int class_key,
	                    int enum_key);
	int FundamentalFromSpecifiers(const std::vector<std::string>& words) const;
	std::string AnonymousClassName(int node);
	std::string AnonymousEnumName();
	std::string LocalClassName();
	int CurrentEntityScope(int scope, const std::string& qualifier);
	void CheckQualifiedDefinition(int site, int target);

	// --- PA7: the resolved-tree dump --------------------------------------
public:
	// Walks the analysed tree again and builds the resolved dump.
	void BuildSemantics(int root);

	SemTree& SemanticsTree()
	{
		return sem_;
	}

private:
	// An expression's resolved type, value category and the declaration it
	// denotes.  `node` is the tree node the expression printed as.
	struct Resolved
	{
		int node;
		int type;
		int category;
		int entity;      // the declaration an id-expression denotes, or -1
		int function;    // the function an expression denotes, or -1
		bool overloaded; // the name denotes more than one function
		bool type_name;  // the id-expression named a type rather than a value
		bool null_zero;  // an integer literal zero, the null pointer constant
		bool has_value;  // a propagated integral constant
		long long value;

		Resolved()
			: node(-1)
			, type(-1)
			, category(kPrvalue)
			, entity(-1)
			, function(-1)
			, overloaded(false)
			, type_name(false)
			, null_zero(false)
			, has_value(false)
			, value(0)
		{}
	};

	// One standard conversion sequence (13.3.3.1.1) from an argument to a
	// parameter.
	struct Conversion
	{
		int rank;             // 0 none, 1 ellipsis, 2 conversion, 3 promotion, 4 exact
		bool lvalue_to_rvalue;
		bool qualification;   // 4.4
		bool reference;       // the parameter is a reference
		bool rvalue_reference;
		bool bound_to_lvalue; // the reference bound an lvalue
		bool pointer_conversion;
		bool boolean_conversion;
		bool proper_subsequence;
		int target;

		Conversion()
			: rank(0)
			, lvalue_to_rvalue(false)
			, qualification(false)
			, reference(false)
			, rvalue_reference(false)
			, bound_to_lvalue(false)
			, pointer_conversion(false)
			, boolean_conversion(false)
			, proper_subsequence(false)
			, target(-1)
		{}
	};

	// A function the call layer is choosing between.
	struct Candidate
	{
		int entity;
		int type;   // the function's declared type
		int scope;  // the scope the declaration was found in

		Candidate()
			: entity(-1)
			, type(-1)
			, scope(-1)
		{}
	};

	// --- the declaration walk --------------------------------------------
	void SemDeclaration(int node, int scope, std::vector<int>& out);
	void SemSimpleDeclaration(int node, int scope, std::vector<int>& out);
	int SemFunctionDefinition(int node, int scope);
	int SemNamespaceDefinition(int node, int scope);
	int SemAliasDeclaration(int node, int scope);
	int SemTemplateDeclaration(int node, int scope);
	void SemLinkageSpecification(int node, int scope, std::vector<int>& out);
	int SemMemberPointerTarget(int node, int scope);
	int SemInitializer(int node, int scope, int& type, const std::string& name,
	                   int entity, bool is_constexpr);
	int SemVariable(int scope, const std::string& name, int entity, int type,
	                int initializer, bool is_constexpr);
	bool IsConstexprSpecifier(int seq) const;
	int SemAnonymousUnionStorage(int specifier, int scope);
	int SemSpecialMember(int node, int scope);

	// --- statements -------------------------------------------------------
	int SemStatement(int node, int scope);
	int SemCompoundStatement(int node, int scope);
	int SemCondition(int node, int scope, bool switch_context);
	int SemSlotStatement(int node, int scope);
	int SemForInit(int node, int scope);

	// --- expressions ------------------------------------------------------
	Resolved SemExpr(int node, int scope);
	Resolved SemLiteral(int node, int scope);
	Resolved SemKeywordLiteral(int node, int scope);
	Resolved SemIdExpression(int node, int scope);
	Resolved SemUnary(int node, int scope);
	Resolved SemPostfix(int node, int scope);
	Resolved SemBinary(int node, int scope);
	Resolved SemAssignment(int node, int scope);
	Resolved SemConditional(int node, int scope);
	Resolved SemSubscript(int node, int scope);
	Resolved SemCall(int node, int scope);
	Resolved SemCast(int node, int scope);
	Resolved SemSizeof(int node, int scope);
	Resolved SemMember(int node, int scope);
	Resolved SemBracedInit(int node, int scope);
	Resolved SemParenthesized(int node, int scope);
	Resolved SemFunctionalCast(int node, int scope, int target,
	                           const std::vector<Resolved>& arguments);
	int MemberClassOf(int entity) const;
	int ClassScopeOf(int class_type) const;
	int SemArgumentList(int node, int scope, std::vector<Resolved>& out);
	Resolved SemIndirectCall(int node, int scope, const Resolved& callee,
	                         const std::vector<Resolved>& arguments);
	Resolved SemBuiltinCall(int node, int scope, const std::string& name);
	Resolved SemNamedCall(int node, int scope, const std::string& text,
	                      const std::vector<Candidate>& candidates,
	                      const std::vector<Resolved>& arguments);

	// --- conversions and overload resolution ------------------------------
	Conversion Convert(const Resolved& from, int target, int scope);
	int SourceType(const Resolved& from) const;
	bool QualificationConvertible(int from, int to, bool& added) const;
	bool PointerCompatible(int from, int to, bool& proper_subsequence) const;
	int CompareConversions(const Conversion& a, const Conversion& b) const;
	bool BetterSequence(const std::vector<Conversion>& a,
	                    const std::vector<Conversion>& b) const;
	void CollectCandidates(int scope, const std::string& text,
	                       std::vector<Candidate>& out);
	void CollectFrom(int scope, const std::string& name,
	                 std::vector<Candidate>& out, std::vector<int>& visited,
	                 bool& blocked) const;
	std::string QualifiedEntityName(int entity) const;
	std::string QualifiedScopeName(int scope) const;
	std::string QualifiedEntitySpelling(int entity) const;
	std::string BoundSpelling(int type, int scope) const;
	std::string Spell(int id) const;
	bool IsScopedEnum(int type) const;

	// --- values -----------------------------------------------------------
	bool IsIntegralType(int type) const;
	bool IsArithmeticType(int type) const;
	bool IsScalarType(int type) const;
	bool IsReferenceType(int type) const;
	int ReferredType(int type) const;
	int Promote(int type) const;
	int UsualArithmetic(int left, int right) const;
	int NullPointerTarget(int type) const;
	int ClassOfPointer(int type) const;

	// --- implicit class machinery -----------------------------------------
	int ImplicitConstructor(int class_type, int scope);
	int SemDefaultInitialization(const Resolved& object, int class_type, int scope);
	void SemanticsImplicitBodies();

	Model& model_;
	const syntax::SyntaxArena& arena_;
	const syntax::SyntaxSpellingPool& spellings_;
	const std::vector<syntax::SyntaxLiteralFacts>& literals_;
	std::map<std::pair<int, int>, std::set<std::string> > defined_functions_;
	std::vector<PendingBody> pending_;
	long long anonymous_enums_;
	long long local_classes_;
	bool semantics_mode_;
	SemTree sem_;
	int sem_root_;
	int return_type_;
	bool return_is_void_;
	int loop_depth_;
	int switch_depth_;

	// The object an anonymous union's storage has, keyed by the union's type,
	// so a name the union injected reaches the member through it (9.5/3).
	std::map<int, int> union_storage_;
	int builtin_abort_;

	// The classes an object definition default-initialised, in the order they
	// first needed an implicit constructor, and the constructors already
	// created.  Both are per translation unit, like the model.
	std::vector<int> implicit_classes_;
	std::map<int, int> implicit_ctors_;
};

}  // namespace semantic
}  // namespace cppgm
