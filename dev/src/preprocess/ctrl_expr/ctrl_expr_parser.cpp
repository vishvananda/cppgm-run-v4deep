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

// The precedence levels of the handout's grammar, innermost first.  Each level
// hands off to the next-tighter one, exactly as the productions nest.
const CtrlExpression::BinOpEntry kMultiplicative[] =
{
	{posttoken::OP_STAR, CtrlExpression::BIN_MUL},
	{posttoken::OP_DIV, CtrlExpression::BIN_DIV},
	{posttoken::OP_MOD, CtrlExpression::BIN_MOD}
};

const CtrlExpression::BinOpEntry kAdditive[] =
{
	{posttoken::OP_PLUS, CtrlExpression::BIN_ADD},
	{posttoken::OP_MINUS, CtrlExpression::BIN_SUB}
};

const CtrlExpression::BinOpEntry kShift[] =
{
	{posttoken::OP_LSHIFT, CtrlExpression::BIN_SHL},
	{posttoken::OP_RSHIFT, CtrlExpression::BIN_SHR}
};

const CtrlExpression::BinOpEntry kRelational[] =
{
	{posttoken::OP_LT, CtrlExpression::BIN_LT},
	{posttoken::OP_GT, CtrlExpression::BIN_GT},
	{posttoken::OP_LE, CtrlExpression::BIN_LE},
	{posttoken::OP_GE, CtrlExpression::BIN_GE}
};

const CtrlExpression::BinOpEntry kEquality[] =
{
	{posttoken::OP_EQ, CtrlExpression::BIN_EQ},
	{posttoken::OP_NE, CtrlExpression::BIN_NE}
};

const CtrlExpression::BinOpEntry kAnd[] =
{
	{posttoken::OP_AMP, CtrlExpression::BIN_AND}
};

const CtrlExpression::BinOpEntry kExclusiveOr[] =
{
	{posttoken::OP_XOR, CtrlExpression::BIN_XOR}
};

const CtrlExpression::BinOpEntry kInclusiveOr[] =
{
	{posttoken::OP_BOR, CtrlExpression::BIN_OR}
};

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

// A signed `+`, `-` or `*` whose mathematical value is not representable in
// `intmax_t` is an error, and an unsigned one wraps.  The builtins give each
// operator that definition directly, so this code never relies on signed
// overflow of its own.
bool SignedOverflow(CtrlExpression::EBinOp op, long long left, long long right,
                    long long& result)
{
	switch (op)
	{
	case CtrlExpression::BIN_ADD:
		return __builtin_add_overflow(left, right, &result);
	case CtrlExpression::BIN_SUB:
		return __builtin_sub_overflow(left, right, &result);
	default:
		return __builtin_mul_overflow(left, right, &result);
	}
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
	return token.kind == kCtrlSimple && IsKeyword(token.simple);
}

bool CtrlExpression::LookupBinOp(ETokenType type, const BinOpEntry* entries,
                                 std::size_t count, EBinOp& op)
{
	for (std::size_t index = 0; index < count; ++index)
	{
		if (entries[index].token == type)
		{
			op = entries[index].op;
			return true;
		}
	}
	return false;
}

bool CtrlExpression::Expect(ETokenType type)
{
	if (AtEnd() || Peek().kind != kCtrlSimple || Peek().simple != type)
		return false;
	Consume();
	return true;
}

void CtrlExpression::Evaluate(std::string& out)
{
	Value value;
	if (!ParseControllingExpression(true, value) || !AtEnd())
	{
		out.append("error");
		return;
	}
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

bool CtrlExpression::ParseControllingExpression(bool live, Value& out)
{
	Value condition;
	if (!ParseLogicalOr(live, condition))
		return false;

	if (AtEnd() || Peek().kind != kCtrlSimple || Peek().simple != posttoken::OP_QMARK)
	{
		out = condition;
		return true;
	}
	Consume();

	// `condition` is meaningful whenever `live` is; the untaken arm is parsed
	// and typed but not evaluated, and so is the whole expression when the
	// condition itself was dead.
	const bool taken = live && condition.bits != 0;
	const bool untaken = live && condition.bits == 0;

	Value when_true;
	if (!ParseControllingExpression(taken, when_true))
		return false;
	if (!Expect(posttoken::OP_COLON))
		return false;
	Value when_false;
	if (!ParseControllingExpression(untaken, when_false))
		return false;

	// 5.16: the two arms undergo the usual arithmetic conversions, so the
	// result is unsigned when either arm's type is, whichever arm is chosen.
	out.is_unsigned = when_true.is_unsigned || when_false.is_unsigned;
	if (taken)
		out.bits = when_true.bits;
	else if (untaken)
		out.bits = when_false.bits;
	else
		out.bits = 0;
	return true;
}

bool CtrlExpression::ParseLogicalOr(bool live, Value& out)
{
	Value left;
	if (!ParseLogicalAnd(live, left))
		return false;

	while (!AtEnd() && Peek().kind == kCtrlSimple && Peek().simple == posttoken::OP_LOR)
	{
		Consume();
		// 5.15: the right operand is evaluated only when the left one
		// compares equal to zero.
		const bool right_live = live && left.bits == 0;
		Value right;
		if (!ParseLogicalAnd(right_live, right))
			return false;
		out.bits = (left.bits != 0) || (right.bits != 0);
		out.is_unsigned = false;
		left = out;
	}

	out = left;
	return true;
}

bool CtrlExpression::ParseLogicalAnd(bool live, Value& out)
{
	Value left;
	if (!ParseInclusiveOr(live, left))
		return false;

	while (!AtEnd() && Peek().kind == kCtrlSimple && Peek().simple == posttoken::OP_LAND)
	{
		Consume();
		// 5.14: the right operand is evaluated only when the left one does not
		// compare equal to zero.
		const bool right_live = live && left.bits != 0;
		Value right;
		if (!ParseInclusiveOr(right_live, right))
			return false;
		out.bits = (left.bits != 0) && (right.bits != 0);
		out.is_unsigned = false;
		left = out;
	}

	out = left;
	return true;
}

bool CtrlExpression::ParseInclusiveOr(bool live, Value& out)
{
	return ParseBinaryLevel(live, out, &CtrlExpression::ParseExclusiveOr,
	                        kInclusiveOr, sizeof(kInclusiveOr) / sizeof(kInclusiveOr[0]));
}

bool CtrlExpression::ParseExclusiveOr(bool live, Value& out)
{
	return ParseBinaryLevel(live, out, &CtrlExpression::ParseAnd,
	                        kExclusiveOr, sizeof(kExclusiveOr) / sizeof(kExclusiveOr[0]));
}

bool CtrlExpression::ParseAnd(bool live, Value& out)
{
	return ParseBinaryLevel(live, out, &CtrlExpression::ParseEquality,
	                        kAnd, sizeof(kAnd) / sizeof(kAnd[0]));
}

bool CtrlExpression::ParseEquality(bool live, Value& out)
{
	return ParseBinaryLevel(live, out, &CtrlExpression::ParseRelational,
	                        kEquality, sizeof(kEquality) / sizeof(kEquality[0]));
}

bool CtrlExpression::ParseRelational(bool live, Value& out)
{
	return ParseBinaryLevel(live, out, &CtrlExpression::ParseShift,
	                        kRelational, sizeof(kRelational) / sizeof(kRelational[0]));
}

bool CtrlExpression::ParseShift(bool live, Value& out)
{
	return ParseBinaryLevel(live, out, &CtrlExpression::ParseAdditive,
	                        kShift, sizeof(kShift) / sizeof(kShift[0]));
}

bool CtrlExpression::ParseAdditive(bool live, Value& out)
{
	return ParseBinaryLevel(live, out, &CtrlExpression::ParseMultiplicative,
	                        kAdditive, sizeof(kAdditive) / sizeof(kAdditive[0]));
}

bool CtrlExpression::ParseMultiplicative(bool live, Value& out)
{
	return ParseBinaryLevel(live, out, &CtrlExpression::ParseUnary,
	                        kMultiplicative, sizeof(kMultiplicative) / sizeof(kMultiplicative[0]));
}

bool CtrlExpression::ParseBinaryLevel(bool live, Value& out, LevelFn next,
                                      const BinOpEntry* entries, std::size_t count)
{
	Value left;
	if ((this->*next)(live, left))
	{
		for (;;)
		{
			EBinOp op;
			if (AtEnd() || Peek().kind != kCtrlSimple ||
			    !LookupBinOp(Peek().simple, entries, count, op))
			{
				break;
			}
			Consume();
			Value right;
			if (!(this->*next)(live, right))
				return false;
			Value result;
			if (!ApplyBinary(op, live, left, right, result))
				return false;
			left = result;
		}
		out = left;
		return true;
	}
	return false;
}

bool CtrlExpression::ParseUnary(bool live, Value& out)
{
	if (!AtEnd() && Peek().kind == kCtrlSimple)
	{
		switch (Peek().simple)
		{
		case posttoken::OP_PLUS:
			// 5.3.1: integral promotion has already put the operand in its
			// promoted type, so unary plus changes nothing.
			Consume();
			return ParseUnary(live, out);

		case posttoken::OP_MINUS:
		{
			// The promoted type is unchanged.  An unsigned operand wraps,
			// which the 64-bit image does by itself; negating the most
			// negative signed value has no representation and is an error,
			// the same way a signed `+`, `-` or `*` that overflows is.
			Consume();
			if (!ParseUnary(live, out))
				return false;
			if (live && !out.is_unsigned)
			{
				long long result;
				if (__builtin_sub_overflow(0LL, static_cast<long long>(out.bits), &result))
					return false;
				out.bits = static_cast<unsigned long long>(result);
				return true;
			}
			out.bits = 0ull - out.bits;
			return true;
		}

		case posttoken::OP_COMPL:
			Consume();
			if (!ParseUnary(live, out))
				return false;
			out.bits = ~out.bits;
			return true;

		case posttoken::OP_LNOT:
			// 5.3.3: the result is `bool`, so it is signed `intmax_t`.
			Consume();
			if (!ParseUnary(live, out))
				return false;
			out.bits = live && out.bits == 0 ? 1 : 0;
			out.is_unsigned = false;
			return true;

		default:
			break;
		}
	}
	return ParsePrimary(live, out);
}

bool CtrlExpression::ParsePrimary(bool live, Value& out)
{
	if (AtEnd())
		return false;

	const CtrlToken& token = Peek();
	switch (token.kind)
	{
	case kCtrlLiteral:
		Consume();
		out.bits = token.bits;
		out.is_unsigned = token.is_unsigned;
		return true;

	case kCtrlIdentifier:
		if (token.is_defined_word)
			return ParseDefined(out);
		// The grammar's `identifier_or_keyword` production evaluates as 0.
		Consume();
		out.bits = 0;
		out.is_unsigned = false;
		return true;

	case kCtrlSimple:
		if (token.simple == posttoken::OP_LPAREN)
		{
			Consume();
			if (!ParseControllingExpression(live, out))
				return false;
			return Expect(posttoken::OP_RPAREN);
		}
		// `true` and `false` are the handout's course-defined special cases;
		// every other keyword is just an `identifier_or_keyword` and is 0.
		if (token.simple == posttoken::KW_TRUE)
			out.bits = 1;
		else if (token.simple == posttoken::KW_FALSE || IsKeyword(token.simple))
			out.bits = 0;
		else
			return false;
		out.is_unsigned = false;
		Consume();
		return true;
	}
	return false;
}

bool CtrlExpression::ParseDefined(Value& out)
{
	Consume();  // the `defined` operator
	if (AtEnd())
		return false;

	bool is_defined = false;
	if (Peek().kind == kCtrlSimple && Peek().simple == posttoken::OP_LPAREN)
	{
		Consume();
		if (AtEnd() || !IsIdentifierOrKeyword(Peek()))
			return false;
		is_defined = Peek().defined_mock;
		Consume();
		if (!Expect(posttoken::OP_RPAREN))
			return false;
	}
	else if (IsIdentifierOrKeyword(Peek()))
	{
		is_defined = Peek().defined_mock;
		Consume();
	}
	else
	{
		return false;
	}

	out.bits = is_defined ? 1 : 0;
	out.is_unsigned = false;
	return true;
}

bool CtrlExpression::ApplyBinary(EBinOp op, bool live, const Value& lhs, const Value& rhs,
                                 Value& out)
{
	// 5.8: a shift converts its operands with integral promotion only, so the
	// result has the promoted left operand's type - the right operand's
	// signedness does not reach it.
	if (op == BIN_SHL || op == BIN_SHR)
	{
		out.is_unsigned = lhs.is_unsigned;
		out.bits = 0;
		if (!live)
			return true;
		// The handout's course definition: a negative right operand, or one
		// greater than or equal to the promoted width, is an error.  Both
		// conditions are read off the 64-bit image: a signed operand is
		// negative exactly when its image is at least 2^63, which the second
		// test already rejects.
		if (!rhs.is_unsigned && static_cast<long long>(rhs.bits) < 0)
			return false;
		if (rhs.bits >= 64)
			return false;
		const unsigned shift = static_cast<unsigned>(rhs.bits);
		if (op == BIN_SHL)
			out.bits = lhs.bits << shift;
		else if (lhs.is_unsigned)
			out.bits = lhs.bits >> shift;
		else
			out.bits = static_cast<unsigned long long>(static_cast<long long>(lhs.bits) >> shift);
		return true;
	}

	// 5.10's usual arithmetic conversions between the two promoted types: the
	// common type is unsigned when either operand's is, and both are 64 bits,
	// so the conversion is the identity on the 64-bit image.
	const bool is_unsigned = lhs.is_unsigned || rhs.is_unsigned;

	// 5.9 and 5.10: a relational or equality operator yields `bool`, so its
	// result is signed `intmax_t` whatever the operands' common type was.  The
	// result's type is set before the dead-branch return, because a `?:` arm
	// that is not evaluated is still typed.
	const bool yields_bool = op == BIN_LT || op == BIN_GT || op == BIN_LE ||
	                         op == BIN_GE || op == BIN_EQ || op == BIN_NE;
	out.is_unsigned = yields_bool ? false : is_unsigned;
	out.bits = 0;
	if (!live)
		return true;

	switch (op)
	{
	case BIN_ADD:
	case BIN_SUB:
	case BIN_MUL:
		// An unsigned result wraps modulo 2^64, which the 64-bit images do by
		// themselves.  A signed one that overflows has no result at all, so the
		// course definition makes it an error - the same treatment `500`'s
		// `(1 << 63)/-1` gets.  Shifts are exempt: `1 << 63` is the most
		// negative value, not an error.
		if (!is_unsigned)
		{
			long long result;
			if (SignedOverflow(op, static_cast<long long>(lhs.bits),
			                   static_cast<long long>(rhs.bits), result))
			{
				return false;
			}
			out.bits = static_cast<unsigned long long>(result);
			return true;
		}
		if (op == BIN_ADD)
			out.bits = lhs.bits + rhs.bits;
		else if (op == BIN_SUB)
			out.bits = lhs.bits - rhs.bits;
		else
			out.bits = lhs.bits * rhs.bits;
		return true;

	case BIN_DIV:
	case BIN_MOD:
		if (is_unsigned)
		{
			if (rhs.bits == 0)
				return false;
			out.bits = op == BIN_DIV ? lhs.bits / rhs.bits : lhs.bits % rhs.bits;
			return true;
		}
		{
			const long long left = static_cast<long long>(lhs.bits);
			const long long right = static_cast<long long>(rhs.bits);
			if (right == 0)
				return false;
			// The one signed quotient that has no representation; the course
			// definition makes it an error rather than a trap.
			if (left == (std::numeric_limits<long long>::min)() && right == -1)
				return false;
			const long long result = op == BIN_DIV ? left / right : left % right;
			out.bits = static_cast<unsigned long long>(result);
			return true;
		}

	case BIN_LT:
	case BIN_GT:
	case BIN_LE:
	case BIN_GE:
	{
		bool result;
		if (is_unsigned)
		{
			result = op == BIN_LT ? lhs.bits < rhs.bits :
			         op == BIN_GT ? lhs.bits > rhs.bits :
			         op == BIN_LE ? lhs.bits <= rhs.bits :
			                        lhs.bits >= rhs.bits;
		}
		else
		{
			const long long left = static_cast<long long>(lhs.bits);
			const long long right = static_cast<long long>(rhs.bits);
			result = op == BIN_LT ? left < right :
			         op == BIN_GT ? left > right :
			         op == BIN_LE ? left <= right :
			                        left >= right;
		}
		out.bits = result ? 1 : 0;
		return true;
	}

	case BIN_EQ:
	case BIN_NE:
	{
		// Equality does not depend on the common type's signedness: converting
		// to it is the identity on the image either way.
		const bool equal = lhs.bits == rhs.bits;
		out.bits = (op == BIN_EQ ? equal : !equal) ? 1 : 0;
		return true;
	}

	case BIN_AND:
		out.bits = lhs.bits & rhs.bits;
		return true;
	case BIN_XOR:
		out.bits = lhs.bits ^ rhs.bits;
		return true;
	case BIN_OR:
		out.bits = lhs.bits | rhs.bits;
		return true;

	default:
		return false;
	}
}

} // namespace preprocess
} // namespace cppgm
