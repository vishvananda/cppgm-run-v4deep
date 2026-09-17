#include "preprocess/ctrl_expr/ctrl_expr_parser.h"

#include <limits>

#include "posttoken/simple_token.h"

namespace cppgm
{
namespace preprocess
{

namespace
{

using posttoken::ETokenType;

// The conditional is the grammar's outermost production, so `?` binds more
// loosely than every binary operator and nothing is ever reduced across it on
// the way in.  A prefix operator binds tighter than all of them.
const unsigned kConditionalPrecedence = 1;
const unsigned kPrefixPrecedence = 12;

// Appends the decimal spelling of an unsigned value, low digit last.  The
// buffer is a local array, so reporting a result allocates nothing.
void AppendDecimal(std::string& out, unsigned long long value)
{
	char digits[20];
	std::size_t count = 0;
	do
	{
		digits[count++] = static_cast<char>('0' + value % 10);
		value /= 10;
	} while (value != 0);
	while (count != 0)
		out.push_back(digits[--count]);
}

void AppendSigned(std::string& out, unsigned long long bits)
{
	const long long value = static_cast<long long>(bits);
	// The magnitude is taken in unsigned arithmetic so that the most negative
	// value has one, too.
	if (value < 0)
	{
		out.push_back('-');
		AppendDecimal(out, 0ull - bits);
	}
	else
	{
		AppendDecimal(out, bits);
	}
}

} // namespace

// The handout's controlling-expression grammar names one production per
// precedence level; this is the same information as a table, because the level
// loop is now one function.  `kConditionalPrecedence` is the number the two
// conditional markers take: a conditional already on the stack is never reduced
// over on the way in, which is what makes `a ? b : c ? d : e` group to the
// right, and a binary operator's precedence is always at least one above it.
const CtrlExpression::BinOpEntry CtrlExpression::kBinaryOps[] =
{
	{posttoken::OP_LOR, CtrlExpression::BIN_LOR, 2},
	{posttoken::OP_LAND, CtrlExpression::BIN_LAND, 3},
	{posttoken::OP_BOR, CtrlExpression::BIN_OR, 4},
	{posttoken::OP_XOR, CtrlExpression::BIN_XOR, 5},
	{posttoken::OP_AMP, CtrlExpression::BIN_AND, 6},
	{posttoken::OP_EQ, CtrlExpression::BIN_EQ, 7},
	{posttoken::OP_NE, CtrlExpression::BIN_NE, 7},
	{posttoken::OP_LT, CtrlExpression::BIN_LT, 8},
	{posttoken::OP_GT, CtrlExpression::BIN_GT, 8},
	{posttoken::OP_LE, CtrlExpression::BIN_LE, 8},
	{posttoken::OP_GE, CtrlExpression::BIN_GE, 8},
	{posttoken::OP_LSHIFT, CtrlExpression::BIN_SHL, 9},
	{posttoken::OP_RSHIFT, CtrlExpression::BIN_SHR, 9},
	{posttoken::OP_PLUS, CtrlExpression::BIN_ADD, 10},
	{posttoken::OP_MINUS, CtrlExpression::BIN_SUB, 10},
	{posttoken::OP_STAR, CtrlExpression::BIN_MUL, 11},
	{posttoken::OP_DIV, CtrlExpression::BIN_DIV, 11},
	{posttoken::OP_MOD, CtrlExpression::BIN_MOD, 11}
};

void CtrlExpression::Evaluate(const CtrlToken* tokens, std::size_t count, std::string& out)
{
	tokens_ = tokens;
	count_ = count;

	if (!Parse())
	{
		out.append("error");
		return;
	}

	const Value& value = values_.back();
	if (value.is_unsigned)
	{
		AppendDecimal(out, value.bits);
		out.push_back('u');
	}
	else
	{
		AppendSigned(out, value.bits);
	}
}

bool CtrlExpression::Parse()
{
	values_.clear();
	ops_.clear();
	expect_operand_ = true;

	// Every reader consumes the tokens it is made of: the `defined` forms are
	// the only ones that are more than one token, and the loop is over what is
	// left rather than over the tokens one at a time.
	for (at_ = 0; at_ < count_; )
	{
		if (!ReadToken())
			return false;
	}
	return Finish();
}

bool CtrlExpression::Finish()
{
	// A line that ended while an operand was still expected (`1 +`, `1 ? 2 :`)
	// is not a controlling expression.  Neither is one whose operator stack
	// still holds something: a `(` or a `?` that never found its pair leaves an
	// operand unconsumed, and a complete expression reduces to exactly one
	// value.
	if (expect_operand_ || !ReduceAll())
		return false;
	return ops_.empty() && values_.size() == 1;
}

bool CtrlExpression::ReadToken()
{
	// The token is copied out because the readers consume it, and `defined`
	// reads on from where it stands.
	const CtrlToken token = tokens_[at_];
	++at_;

	if (token.kind == kCtrlLiteral)
		return ReadValue(token.bits, token.is_unsigned);
	if (token.kind == kCtrlIdentifier)
	{
		// `defined` is not a keyword of the course table, so the operator
		// arrives as an identifier and is recognised here.
		if (token.is_defined_word)
			return ReadDefined();
		// The grammar's `identifier_or_keyword` production evaluates as 0.
		return ReadValue(0, false);
	}
	return ReadSimple(token.SimpleType());
}

bool CtrlExpression::ReadValue(unsigned long long bits, bool is_unsigned)
{
	// Two operands in a row (`1 2`) have no production between them.
	if (!expect_operand_)
		return false;

	values_.push_back(Value{bits, is_unsigned});
	expect_operand_ = false;
	return true;
}

bool CtrlExpression::ReadDefined()
{
	// The `defined` operator itself has already been consumed.
	if (!expect_operand_)
		return false;

	bool parenthesised = false;
	if (at_ < count_ && tokens_[at_].kind == kCtrlSimple
	    && tokens_[at_].simple == posttoken::OP_LPAREN)
	{
		parenthesised = true;
		++at_;
	}

	// Both forms need an `identifier_or_keyword`; `defined 5`, `defined ()` and
	// `defined ((a))` have no production.
	if (at_ >= count_ || !IsIdentifierOrKeyword(tokens_[at_]))
		return false;
	// The handout's mock result, decided while the spelling was still alive.
	const bool is_defined = tokens_[at_].defined_mock;
	++at_;
	if (parenthesised && !Expect(posttoken::OP_RPAREN))
		return false;

	return ReadValue(is_defined ? 1 : 0, false);
}

bool CtrlExpression::ReadSimple(ETokenType type)
{
	switch (type)
	{
	case posttoken::OP_LPAREN:
		return OpenParen();
	case posttoken::OP_RPAREN:
		return CloseParen();
	case posttoken::OP_QMARK:
		return OpenQuestion();
	case posttoken::OP_COLON:
		return CloseQuestion();
	default:
		break;
	}

	// A keyword is an `identifier_or_keyword`, so in a controlling expression it
	// is an operand and never an operator: `true` and `false` are the handout's
	// two course-defined values and every other keyword is 0.
	if (IsKeyword(type))
		return ReadValue(type == posttoken::KW_TRUE ? 1 : 0, false);

	// `+` and `-` are the two spellings that are both, and the operand position
	// is what decides: after an operand neither is a prefix operator, and before
	// one neither is a binary operator.
	if (expect_operand_)
	{
		EUnOp unop;
		if (!LookupPrefixOperator(type, unop))
			return false;
		const bool live = CurrentLive();
		ops_.push_back(Op{kPrefixPrecedence, kOpPrefix, static_cast<std::uint8_t>(unop), live, live});
		return true;
	}

	EBinOp binop;
	unsigned precedence = 0;
	if (!LookupBinOp(type, binop, precedence))
		return false;
	return BeginBinary(binop, precedence);
}

bool CtrlExpression::OpenParen()
{
	// `1 (2)` and `(1 2)` are both rejected: the `(` may not follow an operand,
	// and what follows it is the start of a controlling expression.
	if (!expect_operand_)
		return false;

	const bool live = CurrentLive();
	ops_.push_back(Op{kConditionalPrecedence, kOpOpen, 0, live, live});
	return true;
}

bool CtrlExpression::CloseParen()
{
	if (expect_operand_ || !ReduceAll())
		return false;
	if (ops_.empty() || ops_.back().kind != kOpOpen)
		return false;

	ops_.pop_back();
	// A parenthesised expression is an operand, so the parse continues with the
	// group where an operand stood.
	expect_operand_ = false;
	return true;
}

bool CtrlExpression::OpenQuestion()
{
	if (expect_operand_)
		return false;
	// The condition is complete and on the value stack, so everything that binds
	// tighter than the conditional is applied to it now.  `inclusive == false`
	// is the right associativity: a conditional already waiting for its `:` is
	// left where it is, so it is the *next* `?` that has to close.
	if (!ReduceOperators(kConditionalPrecedence, false))
		return false;

	// 5.16: which arm the condition chooses is known here, before either arm is
	// parsed, so the arm that will not be evaluated is already marked dead.  It
	// is still parsed and still typed.
	const bool live = CurrentLive();
	const bool operand_live = live && values_.back().bits != 0;
	ops_.push_back(Op{kConditionalPrecedence, kOpQuestion, 0, live, operand_live});
	expect_operand_ = true;
	return true;
}

bool CtrlExpression::CloseQuestion()
{
	// The `:` needs an arm on its left that is a complete controlling
	// expression, so everything reducible is reduced before the `?` is paired.
	if (expect_operand_ || !ReduceAll())
		return false;
	if (ops_.empty() || ops_.back().kind != kOpQuestion)
		return false;

	const Op question = ops_.back();
	ops_.pop_back();
	// The then-arm is one value on the stack, with the condition below it.
	if (values_.size() < 2)
		return false;
	const bool condition_true = values_[values_.size() - 2].bits != 0;
	const bool operand_live = question.live && !condition_true;
	ops_.push_back(Op{kConditionalPrecedence, kOpPending, 0, question.live, operand_live});
	expect_operand_ = true;
	return true;
}

bool CtrlExpression::BeginBinary(EBinOp op, unsigned precedence)
{
	// `inclusive == true` is left associativity: an operator of this level that
	// is already on the stack is applied to the left operand before the new one,
	// so `a - b - c` is `(a - b) - c`.
	if (!ReduceOperators(precedence, true))
		return false;

	// 5.14 and 5.15: the right operand is evaluated only when the left one has
	// not already decided the result.  The left operand is a value on the stack
	// already - every operator that binds tighter has been reduced - so both its
	// value and the liveness of the operand that follows are known here.  A dead
	// left operand is not read: its liveness is the region's, and it is false
	// exactly when nothing will be computed.
	const bool live = CurrentLive();
	bool operand_live = live;
	if (live && op == BIN_LAND)
		operand_live = values_.back().bits != 0;
	else if (live && op == BIN_LOR)
		operand_live = values_.back().bits == 0;

	ops_.push_back(Op{precedence, kOpBinary, static_cast<std::uint8_t>(op), live, operand_live});
	expect_operand_ = true;
	return true;
}

bool CtrlExpression::Reducible() const
{
	// `(` and `?` are markers rather than operations: they are only ever popped
	// by the `)` and `:` that match them.
	if (ops_.empty())
		return false;
	return ops_.back().kind != kOpOpen && ops_.back().kind != kOpQuestion;
}

bool CtrlExpression::ReduceOperators(unsigned precedence, bool inclusive)
{
	while (Reducible())
	{
		const unsigned top = ops_.back().precedence;
		if (top < precedence || (!inclusive && top == precedence))
			break;
		if (!ReduceTop())
			return false;
	}
	return true;
}

bool CtrlExpression::ReduceAll()
{
	return ReduceOperators(kConditionalPrecedence, true);
}

bool CtrlExpression::ReduceTop()
{
	const Op op = ops_.back();
	ops_.pop_back();

	if (op.kind == kOpPrefix)
		return ReducePrefix(op);
	if (op.kind == kOpBinary)
		return ReduceBinary(op);
	return ReduceConditional(op);
}

bool CtrlExpression::ReducePrefix(const Op& op)
{
	if (values_.empty())
		return false;

	const EUnOp unop = static_cast<EUnOp>(op.op);
	const Value operand = values_.back();
	values_.pop_back();

	Value result;
	result.bits = 0;
	// 5.3.3's `!` yields `bool`, so it is signed; `+`, `-` and `~` leave the
	// promoted operand's type alone.
	result.is_unsigned = unop == UN_LNOT ? false : operand.is_unsigned;
	if (op.live && !ApplyUnary(unop, operand.bits, operand.is_unsigned, result.bits))
		return false;

	values_.push_back(result);
	return true;
}

bool CtrlExpression::ReduceBinary(const Op& op)
{
	if (values_.size() < 2)
		return false;

	const Value right = values_.back();
	const Value left = values_[values_.size() - 2];
	values_.resize(values_.size() - 2);

	const EBinOp binop = static_cast<EBinOp>(op.op);
	Value result;
	result.bits = 0;
	result.is_unsigned = ResultIsUnsigned(binop, left.is_unsigned, right.is_unsigned);
	// A dead operator still gets its type, but its value is not computed - which
	// is what keeps `0 && 1/0` a value rather than an error.
	if (op.live && !ApplyBinary(binop, left.bits, left.is_unsigned, right.bits,
	                            right.is_unsigned, result.bits))
	{
		return false;
	}

	values_.push_back(result);
	return true;
}

bool CtrlExpression::ReduceConditional(const Op& op)
{
	if (values_.size() < 3)
		return false;

	const Value when_false = values_.back();
	const Value when_true = values_[values_.size() - 2];
	const Value condition = values_[values_.size() - 3];
	values_.resize(values_.size() - 3);

	// 5.16: the two arms undergo the usual arithmetic conversions, so the result
	// is unsigned when either arm's type is - whichever arm is chosen, and even
	// when the arm the condition did not choose is the unsigned one.  Only the
	// arm the condition chose was evaluated, so only that arm's value is read.
	Value result;
	result.bits = 0;
	result.is_unsigned = when_true.is_unsigned || when_false.is_unsigned;
	if (op.live)
		result.bits = condition.bits != 0 ? when_true.bits : when_false.bits;

	values_.push_back(result);
	return true;
}

bool CtrlExpression::Expect(ETokenType type)
{
	if (at_ >= count_ || tokens_[at_].kind != kCtrlSimple || tokens_[at_].simple != type)
		return false;
	++at_;
	return true;
}

bool CtrlExpression::IsKeyword(ETokenType type)
{
	// The keyword enumerators are the ones the table declares before the
	// operators; `defined` is not one of them.
	return type < posttoken::OP_LBRACE;
}

bool CtrlExpression::IsIdentifierOrKeyword(const CtrlToken& token)
{
	if (token.kind == kCtrlIdentifier)
		return true;
	return token.kind == kCtrlSimple && IsKeyword(token.SimpleType());
}

bool CtrlExpression::LookupBinOp(ETokenType type, EBinOp& op, unsigned& precedence)
{
	const std::size_t count = sizeof(kBinaryOps) / sizeof(kBinaryOps[0]);
	for (std::size_t index = 0; index < count; ++index)
	{
		if (kBinaryOps[index].token == type)
		{
			op = kBinaryOps[index].op;
			precedence = kBinaryOps[index].precedence;
			return true;
		}
	}
	return false;
}

bool CtrlExpression::LookupPrefixOperator(ETokenType type, EUnOp& op)
{
	switch (type)
	{
	case posttoken::OP_PLUS:
		op = UN_PLUS;
		return true;
	case posttoken::OP_MINUS:
		op = UN_MINUS;
		return true;
	case posttoken::OP_COMPL:
		op = UN_COMPL;
		return true;
	case posttoken::OP_LNOT:
		op = UN_LNOT;
		return true;
	default:
		return false;
	}
}

bool CtrlExpression::ResultIsUnsigned(EBinOp op, bool left_unsigned, bool right_unsigned)
{
	switch (op)
	{
	case BIN_LT:
	case BIN_GT:
	case BIN_LE:
	case BIN_GE:
	case BIN_EQ:
	case BIN_NE:
		// 5.9 and 5.10: a relational or equality operator yields `bool`, so the
		// result is signed `intmax_t` however unsigned its operands are.
		return false;
	case BIN_LAND:
	case BIN_LOR:
		// 5.14 and 5.15: `bool` again.
		return false;
	case BIN_SHL:
	case BIN_SHR:
		// 5.8: a shift converts its operands with integral promotion only, so
		// the result has the promoted left operand's type - the right operand's
		// signedness does not reach it.
		return left_unsigned;
	default:
		// 5.10's usual arithmetic conversions: the common type is unsigned when
		// either operand's is, and both are 64 bits.
		return left_unsigned || right_unsigned;
	}
}

bool CtrlExpression::SignedOverflow(EBinOp op, long long left, long long right, long long& result)
{
	switch (op)
	{
	case BIN_ADD:
		return __builtin_add_overflow(left, right, &result);
	case BIN_SUB:
		return __builtin_sub_overflow(left, right, &result);
	default:
		return __builtin_mul_overflow(left, right, &result);
	}
}

bool CtrlExpression::ApplyUnary(EUnOp op, unsigned long long operand, bool is_unsigned,
                                unsigned long long& result)
{
	if (op == UN_MINUS)
	{
		if (!is_unsigned)
		{
			// Negating the most negative value has no representation and is an
			// error, the same way a signed `+`, `-` or `*` that overflows is.
			long long negated;
			if (__builtin_sub_overflow(0LL, static_cast<long long>(operand), &negated))
				return false;
			result = static_cast<unsigned long long>(negated);
			return true;
		}
		result = 0ull - operand;
		return true;
	}
	if (op == UN_COMPL)
	{
		result = ~operand;
		return true;
	}
	if (op == UN_LNOT)
	{
		result = operand == 0 ? 1 : 0;
		return true;
	}
	// 5.3.1: integral promotion has already put the operand in its promoted
	// type, so unary plus changes nothing.
	result = operand;
	return true;
}

bool CtrlExpression::ApplyBinary(EBinOp op, unsigned long long left, bool left_unsigned,
                                 unsigned long long right, bool right_unsigned,
                                 unsigned long long& result)
{
	// 5.10's usual arithmetic conversions: the common type the operands are
	// converted to is unsigned when either operand's type is.  It is not the
	// result type - a comparison's result is `bool` - so it is derived here from
	// the operands.
	const bool is_unsigned = left_unsigned || right_unsigned;

	if (op == BIN_SHL || op == BIN_SHR)
	{
		// The handout's course definition: a negative right operand, or one
		// greater than or equal to the promoted width, is an error.  Both
		// conditions are read off the 64-bit image: a signed operand is negative
		// exactly when its image is at least 2^63, which this test already
		// rejects.  The shift itself is on the promoted left operand, whose type
		// is the result's.
		if (right >= 64)
			return false;
		const unsigned shift = static_cast<unsigned>(right);
		if (op == BIN_SHL)
			result = left << shift;
		else if (left_unsigned)
			result = left >> shift;
		else
			result = static_cast<unsigned long long>(static_cast<long long>(left) >> shift);
		return true;
	}

	switch (op)
	{
	case BIN_ADD:
	case BIN_SUB:
	case BIN_MUL:
	{
		if (!is_unsigned)
		{
			// An unsigned result wraps modulo 2^64, which the 64-bit images do
			// by themselves.  A signed one that overflows has no result at all,
			// so the course definition makes it an error - the same treatment
			// `500`'s `(1 << 63)/-1` gets.  Shifts are exempt: `1 << 63` is the
			// most negative value, not an error.
			long long exact;
			if (SignedOverflow(op, static_cast<long long>(left), static_cast<long long>(right),
			                   exact))
			{
				return false;
			}
			result = static_cast<unsigned long long>(exact);
			return true;
		}
		if (op == BIN_ADD)
			result = left + right;
		else if (op == BIN_SUB)
			result = left - right;
		else
			result = left * right;
		return true;
	}

	case BIN_DIV:
	case BIN_MOD:
	{
		if (is_unsigned)
		{
			if (right == 0)
				return false;
			result = op == BIN_DIV ? left / right : left % right;
			return true;
		}
		const long long signed_left = static_cast<long long>(left);
		const long long signed_right = static_cast<long long>(right);
		if (signed_right == 0)
			return false;
		// The one signed quotient that has no representation.
		if (signed_left == std::numeric_limits<long long>::min() && signed_right == -1)
			return false;
		const long long quotient = op == BIN_DIV ? signed_left / signed_right
		                                         : signed_left % signed_right;
		result = static_cast<unsigned long long>(quotient);
		return true;
	}

	case BIN_LT:
	case BIN_GT:
	case BIN_LE:
	case BIN_GE:
	{
		bool comparison;
		if (is_unsigned)
		{
			comparison = op == BIN_LT ? left < right :
			             op == BIN_GT ? left > right :
			             op == BIN_LE ? left <= right :
			                            left >= right;
		}
		else
		{
			const long long signed_left = static_cast<long long>(left);
			const long long signed_right = static_cast<long long>(right);
			comparison = op == BIN_LT ? signed_left < signed_right :
			             op == BIN_GT ? signed_left > signed_right :
			             op == BIN_LE ? signed_left <= signed_right :
			                            signed_left >= signed_right;
		}
		result = comparison ? 1 : 0;
		return true;
	}

	case BIN_EQ:
	case BIN_NE:
	{
		// Equality does not depend on the common type's signedness: converting
		// to it is the identity on the image either way.
		const bool equal = left == right;
		result = (op == BIN_EQ ? equal : !equal) ? 1 : 0;
		return true;
	}

	case BIN_AND:
		result = left & right;
		return true;
	case BIN_XOR:
		result = left ^ right;
		return true;
	case BIN_LOR:
	case BIN_LAND:
		// Reached only when the left operand did not decide the result, which is
		// the only case in which the right operand is a value.
		result = (op == BIN_LOR ? (left != 0 || right != 0) : (left != 0 && right != 0)) ? 1 : 0;
		return true;
	case BIN_OR:
		result = left | right;
		return true;
	default:
		return false;
	}
}

} // namespace preprocess
} // namespace cppgm
