// The integral constant-expression subset of 5.19 the handout requires.
//
// Only array bounds, enumerator values and `static_assert` reach this, so it
// evaluates the operators those need and reports anything else as not a
// constant rather than guessing.  Two rules are load-bearing: `&&` and `||`
// do not evaluate the operand they do not select, and arithmetic on signed
// values that overflows is ill formed rather than wrapping.

#include "semantic/semantic_analyzer.h"

#include <cstdlib>
#include <limits>

#include "posttoken/fundamental_type.h"

using namespace std;

namespace cppgm
{
namespace semantic
{

namespace
{

string AfterColon(const string& text)
{
	const size_t colon = text.find(':');
	return colon == string::npos ? string() : text.substr(colon + 1);
}

int DigitValue(char c)
{
	if(c >= '0' && c <= '9')
	{
		return c - '0';
	}
	if(c >= 'a' && c <= 'f')
	{
		return c - 'a' + 10;
	}
	if(c >= 'A' && c <= 'F')
	{
		return c - 'A' + 10;
	}
	return -1;
}

// The value of one escape sequence, reading from just after its backslash.
long long ReadEscape(const string& text, size_t& index)
{
	const char c = text[index];
	++index;
	switch(c)
	{
	case 'n':
		return '\n';
	case 't':
		return '\t';
	case 'r':
		return '\r';
	case 'a':
		return '\a';
	case 'b':
		return '\b';
	case 'f':
		return '\f';
	case 'v':
		return '\v';
	case '\\':
		return '\\';
	case '\'':
		return '\'';
	case '"':
		return '"';
	case '?':
		return '?';
	case 'x':
	{
		long long value = 0;
		while(index < text.size() && DigitValue(text[index]) >= 0)
		{
			value = value * 16 + DigitValue(text[index]);
			++index;
		}
		return value;
	}
	default:
		break;
	}
	if(c >= '0' && c <= '7')
	{
		long long value = c - '0';
		int digits = 1;
		while(digits < 3 && index < text.size() && text[index] >= '0' && text[index] <= '7')
		{
			value = value * 8 + (text[index] - '0');
			++index;
			++digits;
		}
		return value;
	}
	return static_cast<unsigned char>(c);
}

}  // namespace

Constant Analyzer::EvaluateLiteral(int node)
{
	Constant result;
	const string& text = Label(node);
	size_t index = 0;
	// A character literal may carry a `u`, `U` or `L` prefix; `u8` is not one
	// in C++11, but the recogniser composes it the same way.
	if(text.compare(0, 2, "u8") == 0)
	{
		index = 2;
	}
	while(index < text.size() && (text[index] == 'u' || text[index] == 'U' || text[index] == 'L'))
	{
		++index;
	}
	if(index < text.size() && text[index] == '\'')
	{
		++index;
		long long value = 0;
		while(index < text.size() && text[index] != '\'')
		{
			value = text[index] == '\\' ? ReadEscape(text, ++index)
			                            : static_cast<unsigned char>(text[index++]);
		}
		result.value = value;
		result.valid = true;
		return result;
	}

	// An integer literal: a base prefix, digits that may carry `'` separators,
	// and a suffix naming the type.
	string digits;
	size_t start = 0;
	int base = 10;
	if(text.size() > 1 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
	{
		base = 16;
		start = 2;
	}
	else if(text.size() > 1 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B'))
	{
		base = 2;
		start = 2;
	}
	else if(text.size() > 1 && text[0] == '0' && text[1] >= '0' && text[1] <= '7')
	{
		// 2.14.2: only a digit after the leading zero makes the literal octal.
		// `0u`, `0L` and `0` itself are decimal zero.
		base = 8;
		start = 1;
	}
	bool unsigned_suffix = false;
	for(size_t position = start; position < text.size(); ++position)
	{
		const char c = text[position];
		if(c == '\'')
		{
			continue;
		}
		if(DigitValue(c) >= 0)
		{
			digits += c;
			continue;
		}
		if(c == 'u' || c == 'U')
		{
			unsigned_suffix = true;
			continue;
		}
		if(c == 'l' || c == 'L' || c == 'z' || c == 'Z')
		{
			continue;
		}
		return result;
	}
	if(digits.empty())
	{
		return result;
	}
	unsigned long long value = 0;
	for(size_t position = 0; position < digits.size(); ++position)
	{
		value = value * static_cast<unsigned>(base) +
		        static_cast<unsigned>(DigitValue(digits[position]));
	}
	result.value = static_cast<long long>(value);
	result.is_signed = !unsigned_suffix;
	result.valid = true;
	return result;
}

Constant Analyzer::EvaluateIdentifier(int node, int scope)
{
	Constant result;
	const string& text = Label(node);
	const int entity = ResolveValueName(scope, text);
	if(entity < 0)
	{
		// 14.2: a template-id is a name, and the specialization it names is a
		// function rather than a value, so it is not an integral constant
		// expression - but it is not an unknown name either.
		string template_name;
		vector<string> template_arguments;
		if(text.find('<') != string::npos &&
		   SplitTemplateId(text, template_name, template_arguments))
		{
			vector<int> found;
			if(FindFunctionTemplates(scope, template_name, found))
			{
				return result;
			}
		}
		throw SemanticError("unknown name `" + text + "`");
	}
	const Entity& record = model_.EntityOf(entity);
	result.type = record.type;
	if(record.kind == kEntityEnumerator)
	{
		result.value = record.value;
		result.valid = true;
		return result;
	}
	if(record.kind == kEntityObject && record.has_value)
	{
		result.value = record.value;
		result.valid = true;
	}
	return result;
}

int Analyzer::EvaluateDecltype(int node, int scope)
{
	const int operand = ChildAt(node, 0);
	if(operand < 0)
	{
		throw SemanticError("decltype without an operand");
	}
	const string& tag = Tag(operand);
	bool parenthesized = false;
	int target = operand;
	if(tag == "parenthesized-expression")
	{
		parenthesized = true;
		target = ChildAt(operand, 0);
	}
	// 7.1.6.2/4: the type of an expression the subset can type without
	// evaluating it - a literal, a `sizeof`, a `nullptr` or a call of a
	// function already declared - is the type that expression has.
	if(Tag(target) == "keyword-literal")
	{
		const string word = AfterColon(Label(target));
		if(word == "nullptr")
		{
			return model_.Fundamental(posttoken::FT_NULLPTR_T);
		}
		if(word == "true" || word == "false")
		{
			return model_.Fundamental(posttoken::FT_BOOL);
		}
	}
	if(Tag(target) == "sizeof-expression" || Tag(target) == "type-trait-expression")
	{
		return model_.Fundamental(posttoken::FT_UNSIGNED_LONG_INT);
	}
	if(Tag(target) == "literal")
	{
		const Constant value = EvaluateLiteral(target);
		if(value.valid)
		{
			return model_.Fundamental(value.is_signed ? posttoken::FT_INT
			                                          : posttoken::FT_UNSIGNED_INT);
		}
	}
	if(Tag(target) == "call-expression")
	{
		const int callee = ChildAt(target, 0);
		if(Tag(callee) == "id-expression")
		{
			const int entity = ResolveValueName(scope, Label(callee));
			if(entity >= 0 && model_.EntityOf(entity).kind == kEntityFunction)
			{
				return model_.Get(model_.EntityOf(entity).type).base;
			}
		}
	}
	if(Tag(target) != "id-expression")
	{
		throw SemanticError("decltype operand is outside the supported subset");
	}
	const int entity = ResolveValueName(scope, Label(target));
	if(entity < 0)
	{
		throw SemanticError("unknown name `" + Label(target) + "` in decltype");
	}
	const Entity& record = model_.EntityOf(entity);
	if(parenthesized && record.kind == kEntityObject)
	{
		// 7.1.6.2/4: an unparenthesized id-expression names the declared type,
		// and a parenthesized lvalue names a reference to it.
		return model_.LvalueReference(record.type);
	}
	return record.type;
}

Constant Analyzer::EvaluateBinary(int node, int scope, const string& op)
{
	Constant result;
	const int left_node = ChildAt(node, 0);
	const int right_node = ChildAt(node, 1);

	// 5.14/1, 5.15/1: the operand not selected is not evaluated, so
	// `1 || (1 / 0)` is a constant expression.
	if(op == "||" || op == "&&")
	{
		const Constant left = Evaluate(left_node, scope);
		if(!left.valid)
		{
			return result;
		}
		const bool truth = left.value != 0;
		if(op == "||" ? truth : !truth)
		{
			result.value = truth ? 1 : 0;
			result.valid = true;
			return result;
		}
		const Constant right = Evaluate(right_node, scope);
		if(!right.valid)
		{
			return result;
		}
		result.value = right.value != 0 ? 1 : 0;
		result.valid = true;
		return result;
	}

	const Constant left = Evaluate(left_node, scope);
	const Constant right = Evaluate(right_node, scope);
	if(!left.valid || !right.valid)
	{
		return result;
	}
	// 5.18/1: a comma expression's value is its right operand, and the left one
	// is evaluated and discarded - so `(1, 2)` is a constant while `(1/0, 2)`
	// is not, because the discarded operand is still evaluated.
	if(op == ",")
	{
		return right;
	}
	const long long a = left.value;
	const long long b = right.value;
	long long value = 0;
	const bool both_signed = left.is_signed && right.is_signed;

	if(op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=")
	{
		// 5.9/2 with 7.2/9: a scoped enumeration does not convert to an
		// integer, so comparing one with anything but the same enumeration is
		// ill formed.
		const bool left_scoped = IsScopedEnum(left.type);
		const bool right_scoped = IsScopedEnum(right.type);
		if(left_scoped != right_scoped || (left_scoped && left.type != right.type))
		{
			throw SemanticError("a scoped enumeration does not compare with an integer");
		}
		const bool truth = op == "==" ? a == b : op == "!=" ? a != b
		                 : op == "<" ? a < b : op == ">" ? a > b
		                 : op == "<=" ? a <= b : a >= b;
		result.value = truth ? 1 : 0;
		result.valid = true;
		return result;
	}
	if(op == "+" || op == "-" || op == "*")
	{
		bool overflow = false;
		if(both_signed)
		{
			overflow = op == "+" ? __builtin_add_overflow(a, b, &value)
			         : op == "-" ? __builtin_sub_overflow(a, b, &value)
			                     : __builtin_mul_overflow(a, b, &value);
		}
		else
		{
			value = op == "+" ? a + b : op == "-" ? a - b : a * b;
		}
		if(overflow)
		{
			throw SemanticError("signed constant expression overflow");
		}
		result.value = value;
		result.is_signed = both_signed;
		result.valid = true;
		return result;
	}
	if(op == "/" || op == "%")
	{
		if(b == 0)
		{
			throw SemanticError("division by zero in a constant expression");
		}
		if(op == "/")
		{
			value = a / b;
		}
		else
		{
			value = a % b;
		}
		result.value = value;
		result.is_signed = both_signed;
		result.valid = true;
		return result;
	}
	if(op == "<<" || op == ">>")
	{
		if(b < 0 || b >= 64)
		{
			throw SemanticError("shift count out of range in a constant expression");
		}
		value = op == "<<" ? a << b : a >> b;
		result.value = value;
		result.is_signed = left.is_signed;
		result.valid = true;
		return result;
	}
	if(op == "&" || op == "|" || op == "^")
	{
		value = op == "&" ? (a & b) : op == "|" ? (a | b) : (a ^ b);
		result.value = value;
		result.is_signed = both_signed;
		result.valid = true;
		return result;
	}
	return result;
}

Constant Analyzer::EvaluateUnary(int node, int scope, const string& op)
{
	Constant result;
	const Constant operand = Evaluate(ChildAt(node, 0), scope);
	if(!operand.valid)
	{
		return result;
	}
	if(op == "!")
	{
		result.value = operand.value == 0 ? 1 : 0;
		result.valid = true;
		return result;
	}
	if(op == "-")
	{
		if(operand.is_signed && operand.value == numeric_limits<long long>::min())
		{
			throw SemanticError("signed constant expression overflow");
		}
		result.value = -operand.value;
		result.is_signed = operand.is_signed;
		result.valid = true;
		return result;
	}
	if(op == "+")
	{
		return operand;
	}
	if(op == "~")
	{
		result.value = ~operand.value;
		result.is_signed = operand.is_signed;
		result.valid = true;
		return result;
	}
	return result;
}

// A cast to an integral type converts the value to that type's representation
// (4.7), which is what `static_cast<unsigned char>(A::x)` needs.
Constant Analyzer::EvaluateCall(int node, int scope)
{
	Constant result;
	const int callee = ChildAt(node, 0);
	if(callee < 0 || Tag(callee) != "id-expression")
	{
		return result;
	}
	// A callee that names no type is an ordinary call, which this subset does
	// not evaluate; only a type name is a conversion of the argument.
	string qualifier;
	string name;
	SplitQualifiedName(Label(callee), qualifier, name);
	const int target = qualifier.empty() ? model_.LookupTypeUnqualified(scope, name) : -1;
	if(target < 0)
	{
		return result;
	}
	const Type& type = model_.Get(model_.EntityOf(target).type);
	if(type.kind != kTypeFundamental ||
	   !posttoken::FundamentalTypeIsIntegral(
	       static_cast<posttoken::EFundamentalType>(type.base)))
	{
		return result;
	}
	const int arguments = FindChild(node, "paren-argument-list");
	if(arguments < 0 || ChildCount(arguments) != 1)
	{
		return result;
	}
	const Constant operand = Evaluate(ChildAt(arguments, 0), scope);
	if(!operand.valid)
	{
		return result;
	}
	result.valid = true;
	result.type = target;
	return result;
}

Constant Analyzer::Evaluate(int node, int scope)
{
	Constant result;
	if(node < 0)
	{
		return result;
	}
	const string& tag = Tag(node);
	if(tag == "literal")
	{
		return EvaluateLiteral(node);
	}
	if(tag == "keyword-literal")
	{
		const string word = AfterColon(Label(node));
		if(word == "true" || word == "false")
		{
			result.value = word == "true" ? 1 : 0;
			result.valid = true;
		}
		return result;
	}
	if(tag == "id-expression")
	{
		return EvaluateIdentifier(node, scope);
	}
	if(tag == "parenthesized-expression")
	{
		return Evaluate(ChildAt(node, 0), scope);
	}
	if(tag == "initializer")
	{
		const int child = ChildAt(node, 0);
		if(child < 0)
		{
			return result;
		}
		const string& inner = Tag(child);
		if(inner == "braced-init-list" || inner == "paren-initializer" ||
		   inner == "special-initializer")
		{
			return result;
		}
		return Evaluate(child, scope);
	}
	if(tag == "binary-expression")
	{
		return EvaluateBinary(node, scope, AfterColon(Label(node)));
	}
	if(tag == "conditional-expression")
	{
		// 5.16: only the branch the condition selects is evaluated.
		const Constant condition = Evaluate(ChildAt(node, 0), scope);
		if(!condition.valid)
		{
			return result;
		}
		return Evaluate(ChildAt(node, condition.value != 0 ? 1 : 2), scope);
	}
	if(tag == "unary-expression")
	{
		return EvaluateUnary(node, scope, AfterColon(Label(node)));
	}
	if(tag == "call-expression")
	{
		return EvaluateCall(node, scope);
	}
	if(tag == "cast-expression")
	{
		const Constant operand = Evaluate(ChildAt(node, 1), scope);
		if(!operand.valid)
		{
			return result;
		}
		const int target = BuildDeclarator(ChildAt(node, 0), -1, scope);
		const Type& type = model_.Get(target);
		if(type.kind != kTypeFundamental ||
		   !posttoken::FundamentalTypeIsIntegral(
		       static_cast<posttoken::EFundamentalType>(type.base)))
		{
			return result;
		}
		result.value = TruncateTo(operand.value, type.base);
		result.is_signed = posttoken::FundamentalTypeIsSigned(
		    static_cast<posttoken::EFundamentalType>(type.base));
		result.type = target;
		result.valid = true;
		return result;
	}
	if(tag == "sizeof-expression" || tag == "type-trait-expression")
	{
		const int operand = ChildAt(node, 0);
		if(operand < 0)
		{
			return result;
		}
		int target = -1;
		if(Tag(operand) == "type-id")
		{
			target = BuildDeclarator(operand, -1, scope);
		}
		else if(Tag(operand) == "id-expression")
		{
			// `sizeof(x)` where `x` names an object is the type-forming case
			// with an id-expression operand, which 5.3.3/1 allows.
			const int entity = ResolveValueName(scope, Label(operand));
			target = entity < 0 ? -1 : model_.EntityOf(entity).type;
		}
		if(target < 0)
		{
			return result;
		}
		unsigned long long amount = 0;
		const bool align = AfterColon(Label(node)) == "alignof";
		const bool known = align ? model_.AlignOf(target, amount) : model_.SizeOf(target, amount);
		if(!known)
		{
			throw SemanticError(align ? "alignof of an incomplete type" : "sizeof of an incomplete type");
		}
		result.value = static_cast<long long>(amount);
		result.valid = true;
		return result;
	}
	return result;
}

// The value a conversion to an integral type produces: the low bits of the
// source value, read back with the destination's signedness.
long long Analyzer::TruncateTo(long long value, int fundamental) const
{
	const unsigned long long size =
	    posttoken::FundamentalTypeSize(static_cast<posttoken::EFundamentalType>(fundamental));
	if(size == 0 || size >= 8)
	{
		return value;
	}
	const unsigned long long mask = (1ull << (size * 8)) - 1ull;
	const unsigned long long truncated = static_cast<unsigned long long>(value) & mask;
	if(!posttoken::FundamentalTypeIsSigned(static_cast<posttoken::EFundamentalType>(fundamental)))
	{
		return static_cast<long long>(truncated);
	}
	const unsigned long long sign = 1ull << (size * 8 - 1);
	return static_cast<long long>((truncated ^ sign) - sign);
}

bool Analyzer::IsScopedEnum(int type) const
{
	if(type < 0)
	{
		return false;
	}
	const Type& record = model_.Get(type);
	return record.kind == kTypeEnum && record.enum_key != kEnumKeyPlain;
}

long long Analyzer::ArrayBound(int node, int scope)
{
	const Constant value = Evaluate(node, scope);
	if(!value.valid)
	{
		throw SemanticError("an array bound must be a constant expression");
	}
	if(value.value <= 0)
	{
		throw SemanticError("an array bound must be positive");
	}
	return value.value;
}

}  // namespace semantic
}  // namespace cppgm
