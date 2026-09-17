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
	{}

	// Appends the line's result to `out`, without a newline: the decimal value,
	// with a `u` suffix when the promoted result type is unsigned, or `error`
	// when the token sequence is not a valid controlling expression.
	void Evaluate(std::string& out);

	// The binary operators the token sequence can carry.  Each precedence level
	// is one table of these, so the level loop is written once.
	enum EBinOp
	{
		BIN_MUL, BIN_DIV, BIN_MOD,
		BIN_ADD, BIN_SUB,
		BIN_SHL, BIN_SHR,
		BIN_LT, BIN_GT, BIN_LE, BIN_GE,
		BIN_EQ, BIN_NE,
		BIN_AND, BIN_XOR, BIN_OR
	};

	struct BinOpEntry
	{
		posttoken::ETokenType token;
		EBinOp op;
	};

private:
	// A sub-expression's value: always one of the two promoted types the
	// handout fixes, held as its 64-bit two's complement image.
	struct Value
	{
		unsigned long long bits;
		bool is_unsigned;
	};

	typedef bool (CtrlExpression::*LevelFn)(bool live, Value& out);

	bool AtEnd() const { return at_ >= count_; }
	const CtrlToken& Peek() const { return tokens_[at_]; }
	void Consume() { ++at_; }
	bool Expect(posttoken::ETokenType type);

	// True for the keyword token types.  Keywords are `identifier_or_keyword`s
	// in a controlling expression and so are operands, never operators.
	static bool IsKeyword(posttoken::ETokenType type);
	static bool IsIdentifierOrKeyword(const CtrlToken& token);
	static bool LookupBinOp(posttoken::ETokenType type, const BinOpEntry* entries,
	                        std::size_t count, EBinOp& op);

	bool ParseControllingExpression(bool live, Value& out);
	bool ParseLogicalOr(bool live, Value& out);
	bool ParseLogicalAnd(bool live, Value& out);
	bool ParseInclusiveOr(bool live, Value& out);
	bool ParseExclusiveOr(bool live, Value& out);
	bool ParseAnd(bool live, Value& out);
	bool ParseEquality(bool live, Value& out);
	bool ParseRelational(bool live, Value& out);
	bool ParseShift(bool live, Value& out);
	bool ParseAdditive(bool live, Value& out);
	bool ParseMultiplicative(bool live, Value& out);
	// The loop every left-recursive production of the grammar becomes: parse
	// one operand of the next-tighter level, then keep applying this level's
	// operators to it leftward.
	bool ParseBinaryLevel(bool live, Value& out, LevelFn next,
	                      const BinOpEntry* entries, std::size_t count);
	bool ParseUnary(bool live, Value& out);
	bool ParsePrimary(bool live, Value& out);
	bool ParseDefined(Value& out);
	bool ApplyBinary(EBinOp op, bool live, const Value& lhs, const Value& rhs, Value& out);

	const CtrlToken* tokens_;
	std::size_t count_;
	std::size_t at_;
};

} // namespace preprocess
} // namespace cppgm
