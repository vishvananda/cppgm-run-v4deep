// The PA3 controlling-expression parser and evaluator.
//
// The grammar is the handout's `controlling-expression`, parsed by a
// hand-written predictive parser.  Every left-recursive production becomes an
// iterative loop, which fixes each operator's associativity without building a
// tree: the parser evaluates as it parses, and the only state a sub-expression
// produces is one value plus one signedness bit.
//
// Evaluation is lazy in exactly the two places the language is: the right
// operand of `&&`/`||` and the arm of `?:` the condition did not choose.  A
// `live` flag follows those edges; a dead sub-expression is still parsed and
// still typed - 5.16's result type comes from both arms even when one is not
// evaluated - but its value is not computed and the course-defined value
// errors (division by zero, an out-of-range shift) are not raised.  Every other
// error, including a grammar mismatch and an unconsumed token, is independent
// of the flag.

#pragma once

#include <cstddef>
#include <string>

#include "preprocess/ctrl_expr/ctrl_expr_token.h"

namespace cppgm
{
namespace preprocess
{

// One controlling expression, evaluated into its output line.
class CtrlExpression
{
public:
	CtrlExpression(const CtrlToken* tokens, std::size_t count)
		: tokens_(tokens)
		, count_(count)
		, at_(0)
		, depth_(0)
	{}

	// Appends the line's result to `out`, without a newline: the decimal value,
	// with a `u` suffix when the promoted result type is unsigned, or `error`
	// when the token sequence is not a valid controlling expression.
	void Evaluate(std::string& out);

	// The binary operators the token sequence can carry, and the precedence the
	// handout's grammar gives each of them (a higher number binds tighter).
	enum EBinOp
	{
		BIN_LOR, BIN_LAND,
		BIN_OR, BIN_XOR, BIN_AND,
		BIN_EQ, BIN_NE,
		BIN_LT, BIN_GT, BIN_LE, BIN_GE,
		BIN_SHL, BIN_SHR,
		BIN_ADD, BIN_SUB,
		BIN_MUL, BIN_DIV, BIN_MOD
	};

	struct BinOpEntry
	{
		posttoken::ETokenType token;
		EBinOp op;
		unsigned precedence;
	};

private:
	// A sub-expression's value: always one of the two promoted types the
	// handout fixes, held as its 64-bit two's complement image.
	struct Value
	{
		unsigned long long bits;
		bool is_unsigned;
	};

	// The parser is recursive descent, so the C stack grows with the
	// expression's nesting - an unparenthesised prefix chain and each
	// parenthesised or conditional level add frames.  The handout's own design
	// note accepts that call stack, but nothing bounds it: without a limit the
	// only bound is the process stack, so the same input would succeed or die
	// on SIGSEGV according to `ulimit -s`.  Every production compiler bounds
	// nesting too (clang's default maximum bracket depth is 256); an expression
	// nested past this one is an invalid controlling expression, which is the
	// line's `error`, rather than a crash.  The limit counts parser frames, so
	// it allows more than half of it in nested parentheses.
	static const std::size_t kMaxNesting = 8192;

	// Counts parser frames for as long as the object lives.
	class DepthGuard
	{
	public:
		DepthGuard(std::size_t& depth, std::size_t limit)
			: depth_(depth)
			, within_(++depth <= limit)
		{}

		~DepthGuard() { --depth_; }

		bool Within() const { return within_; }

	private:
		std::size_t& depth_;
		bool within_;
	};

	bool AtEnd() const { return at_ >= count_; }
	const CtrlToken& Peek() const { return tokens_[at_]; }
	void Consume() { ++at_; }
	bool Expect(posttoken::ETokenType type);

	// True for the keyword token types.  Keywords are `identifier_or_keyword`s
	// in a controlling expression and so are operands, never operators.
	static bool IsKeyword(posttoken::ETokenType type);
	static bool IsIdentifierOrKeyword(const CtrlToken& token);
	static bool LookupBinOp(posttoken::ETokenType type, BinOpEntry& entry);

	bool ParseControllingExpression(bool live, Value& out);
	// Every binary precedence level of the grammar in one loop: parse an
	// operand no looser than `min_precedence`, then keep applying the
	// operators that bind at least as tightly, leftward.  This is the collapse
	// the handout's design note recommends, and it is what keeps a nested
	// expression's stack cost proportional to its parenthesis depth rather
	// than to the number of precedence levels crossed at each one.
	bool ParseBinary(unsigned min_precedence, bool live, Value& out);
	bool ParseUnary(bool live, Value& out);
	bool ParsePrimary(bool live, Value& out);
	bool ParseDefined(Value& out);
	bool ApplyBinary(EBinOp op, bool live, const Value& lhs, const Value& rhs, Value& out);

	const CtrlToken* tokens_;
	std::size_t count_;
	std::size_t at_;
	std::size_t depth_;
};

} // namespace preprocess
} // namespace cppgm
