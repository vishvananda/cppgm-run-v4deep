// The PA3 controlling-expression parser and evaluator.
//
// The grammar is the handout's `controlling-expression`, parsed by an
// operator-precedence parser driven by one explicit stack.  The handout's own
// design note names the technique - "collapse the calls to all the different
// binary operator expressions into one call ... by keeping a precedence table
// and making decisions about how to build the parse tree based upon it" - and
// it is also what lets the stage accept any nesting the source can spell.  A
// recursive-descent parser, this stage's first form, grows the C stack with the
// expression's nesting, so the depth it accepts is a property of `ulimit -s`
// rather than of the input and the failure is a SIGSEGV that loses the whole
// run's output; the reference parses nesting from the heap and computes a value
// at any depth.
//
// The parser evaluates as it reduces, so the only state a sub-expression
// produces is one value plus one signedness bit - there is no tree and no
// second pass over one.  Liveness travels with the operator stack instead: the
// liveness of the operand region that follows an operator is fixed when the
// operator is read, because the value it depends on is already on the value
// stack.  `0 && ...` knows its right operand is dead at the `&&`, and `a ? b :
// c` knows which arm is dead at the `?` and the `:`.  A dead sub-expression is
// still parsed and still typed - 5.16's result type comes from both arms even
// when one is not evaluated - but its value is not computed, so the handout's
// course-defined value errors (division by zero, an out-of-range shift) are
// raised only where the language evaluates.  Both stacks are heap vectors the
// stage keeps across logical lines, so a line is parsed in O(tokens) time and
// memory with no per-token allocation and no machine stack limit.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "preprocess/ctrl_expr/ctrl_expr_token.h"

namespace cppgm
{
namespace preprocess
{

// One controlling expression, parsed and evaluated into its output line.  The
// sink keeps one of these for the whole run, so the two stacks keep their
// capacity across lines.
class CtrlExpression
{
public:
	CtrlExpression()
		: tokens_(0)
		, count_(0)
		, at_(0)
		, expect_operand_(true)
	{}

	// Appends the line's result to `out`, without a newline: the decimal value,
	// with a `u` suffix when the promoted result type is unsigned, or `error`
	// when the token sequence is not a valid controlling expression - a grammar
	// mismatch, an unconsumed token, or one of the handout's course-defined
	// value errors reached during evaluation.
	void Evaluate(const CtrlToken* tokens, std::size_t count, std::string& out);

private:
	// A sub-expression's value: always one of the two promoted types the
	// handout fixes, held as its 64-bit two's complement image plus the bit that
	// says which of the two it is.
	struct Value
	{
		unsigned long long bits;
		bool is_unsigned;
	};

	enum EUnOp { UN_PLUS, UN_MINUS, UN_COMPL, UN_LNOT };

	// The binary operators in the order the grammar names their levels (a
	// higher number binds tighter).  Every one of them is left associative.
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

	// What the operator stack holds.  `kOpQuestion` is the `?` of a
	// conditional, waiting for its `:`; `kOpPending` is the `:`-completed form,
	// waiting for the arm it will take.  Both carry the conditional's
	// precedence, which is why a binary operator is never reduced over one.
	enum EOpKind : std::uint8_t { kOpPrefix, kOpBinary, kOpOpen, kOpQuestion, kOpPending };

	// The entry in the table the precedence loop and the binary lookup share:
	// the token that spells the operator, the operation, and how tightly it
	// binds.
	struct BinOpEntry
	{
		posttoken::ETokenType token;
		EBinOp op;
		unsigned precedence;
	};

	static const BinOpEntry kBinaryOps[];

	// One operator stack entry.  `op` is an `EUnOp` for `kOpPrefix` and an
	// `EBinOp` for `kOpBinary`; the other kinds carry no operation.  `live` is
	// the liveness of the operation itself and `operand_live` that of the
	// operand region that follows it - the two differ exactly where the
	// language decides an operand's liveness from a value: `&&`, `||` and the
	// two arms of `?:`.  Only a live operation computes its value; a dead one
	// still produces its result type.
	struct Op
	{
		unsigned precedence;
		EOpKind kind;
		std::uint8_t op;
		bool live;
		bool operand_live;
	};

	bool Parse();
	bool Finish();
	bool ReadToken();
	bool ReadValue(unsigned long long bits, bool is_unsigned);
	bool ReadDefined();
	bool ReadSimple(posttoken::ETokenType type);
	bool OpenParen();
	bool CloseParen();
	bool OpenQuestion();
	bool CloseQuestion();
	bool BeginBinary(EBinOp op, unsigned precedence);

	bool Reducible() const;
	bool ReduceOperators(unsigned precedence, bool inclusive);
	bool ReduceAll();
	bool ReduceTop();
	bool ReducePrefix(const Op& op);
	bool ReduceBinary(const Op& op);
	bool ReduceConditional(const Op& op);

	// The liveness of the operand region the parser is in: the top entry's
	// `operand_live`, or true outside every operation.
	bool CurrentLive() const
	{
		return ops_.empty() ? true : ops_.back().operand_live;
	}

	bool Expect(posttoken::ETokenType type);

	static bool IsKeyword(posttoken::ETokenType type);
	static bool IsIdentifierOrKeyword(const CtrlToken& token);
	static bool LookupBinOp(posttoken::ETokenType type, EBinOp& op, unsigned& precedence);
	static bool LookupPrefixOperator(posttoken::ETokenType type, EUnOp& op);

	static bool ResultIsUnsigned(EBinOp op, bool left_unsigned, bool right_unsigned);
	static bool SignedOverflow(EBinOp op, long long left, long long right, long long& result);
	static bool ApplyUnary(EUnOp op, unsigned long long operand, bool is_unsigned,
	                       unsigned long long& result);
	static bool ApplyBinary(EBinOp op, unsigned long long left, bool left_unsigned,
	                        unsigned long long right, bool right_unsigned,
	                        unsigned long long& result);

	const CtrlToken* tokens_;
	std::size_t count_;
	std::size_t at_;
	bool expect_operand_;

	// The value of each complete sub-expression, in the order the reductions
	// produce them, and the operators still waiting to be reduced.
	std::vector<Value> values_;
	std::vector<Op> ops_;
};

} // namespace preprocess
} // namespace cppgm
