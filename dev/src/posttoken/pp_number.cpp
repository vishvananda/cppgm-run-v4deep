#include "posttoken/pp_number.h"

#include <climits>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "posttoken/pa2_decode.h"

namespace cppgm
{
namespace posttoken
{

namespace
{

bool IsDigit(char c)
{
	return c >= '0' && c <= '9';
}

bool IsHexDigit(char c)
{
	return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool IsIdentifierStart(char c)
{
	return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		static_cast<unsigned char>(c) >= 0x80;
}

bool IsIdentifierBody(char c)
{
	return IsIdentifierStart(c) || IsDigit(c);
}

unsigned DigitValue(char c)
{
	if (c >= '0' && c <= '9')
		return static_cast<unsigned>(c - '0');
	if (c >= 'a' && c <= 'f')
		return static_cast<unsigned>(c - 'a' + 10);
	return static_cast<unsigned>(c - 'A' + 10);
}

// The `integer-suffix` of 2.14.2 as the three facts the type table needs.
struct IntegerSuffix
{
	bool unsigned_suffix = false;
	bool long_suffix = false;
	bool long_long_suffix = false;
};

struct DigitRun
{
	unsigned long long value = 0;
	bool overflow = false;
	std::size_t end = 0;
	unsigned count = 0;
};

DigitRun ReadDigits(const std::string& text, std::size_t position, unsigned base)
{
	DigitRun run;
	run.end = position;
	while (run.end < text.size())
	{
		char c = text[run.end];
		if (!IsHexDigit(c))
			break;
		unsigned digit = DigitValue(c);
		if (digit >= base)
			break;
		if (run.value > (ULLONG_MAX - digit) / base)
			run.overflow = true;
		else
			run.value = run.value * base + digit;
		++run.count;
		++run.end;
	}
	return run;
}

// Reads an `integer-suffix` at `position` that must consume the rest of the
// spelling.  Returns false when it does not.
bool ReadIntegerSuffix(const std::string& text, std::size_t position, IntegerSuffix& suffix)
{
	std::size_t end = text.size();
	bool unsigned_seen = false;
	bool long_seen = false;
	bool long_long_seen = false;

	if (position < end && (text[position] == 'u' || text[position] == 'U'))
	{
		unsigned_seen = true;
		++position;
	}

	if (position < end && (text[position] == 'l' || text[position] == 'L'))
	{
		// `long-long-suffix` is `ll` or `LL`: the two letters must match, so
		// `lL` and `Ll` are not an integer-suffix at all.
		char first_long = text[position];
		++position;
		if (position < end && text[position] == first_long)
		{
			long_long_seen = true;
			++position;
		}
		else
		{
			long_seen = true;
		}
	}

	if (!unsigned_seen && position < end && (text[position] == 'u' || text[position] == 'U'))
	{
		unsigned_seen = true;
		++position;
	}

	// The empty `integer-suffix` is a suffix too: `123` has one, and it is
	// what makes the literal decimal in the first place.
	if (position != end)
		return false;

	suffix.unsigned_suffix = unsigned_seen;
	suffix.long_suffix = long_seen;
	suffix.long_long_suffix = long_long_seen;
	return true;
}

// The number of bits a candidate integer type has under the course ABI.
unsigned TypeBits(EFundamentalType type)
{
	switch (type)
	{
	case FT_INT: return 32;
	case FT_UNSIGNED_INT: return 32;
	case FT_LONG_INT: return 64;
	case FT_UNSIGNED_LONG_INT: return 64;
	case FT_LONG_LONG_INT: return 64;
	case FT_UNSIGNED_LONG_LONG_INT: return 64;
	default: return 0;
	}
}

bool Fits(unsigned long long value, EFundamentalType type)
{
	unsigned bits = TypeBits(type);
	if (bits == 0)
		return false;
	if (type == FT_INT || type == FT_LONG_INT || type == FT_LONG_LONG_INT)
		return bits == 64 ? value <= 0x7FFFFFFFFFFFFFFFull : value < (1ull << (bits - 1));
	return bits == 64 ? true : value < (1ull << bits);
}

// 2.14.2 Table 6: a literal's type is the first type in its `integer-suffix`'s
// list that can represent its value.
EFundamentalType SelectIntegerType(bool decimal, const IntegerSuffix& suffix,
                                   unsigned long long value, bool& found)
{
	EFundamentalType candidates[6];
	unsigned count = 0;

	if (suffix.unsigned_suffix && !suffix.long_suffix && !suffix.long_long_suffix)
	{
		candidates[count++] = FT_UNSIGNED_INT;
		candidates[count++] = FT_UNSIGNED_LONG_INT;
		candidates[count++] = FT_UNSIGNED_LONG_LONG_INT;
	}
	else if (suffix.unsigned_suffix && suffix.long_suffix)
	{
		candidates[count++] = FT_UNSIGNED_LONG_INT;
		candidates[count++] = FT_UNSIGNED_LONG_LONG_INT;
	}
	else if (suffix.unsigned_suffix)
	{
		// `ull`: 2.14.2 Table 6 gives this suffix exactly one type, unlike the
		// `ul` row it would otherwise fall through to.
		candidates[count++] = FT_UNSIGNED_LONG_LONG_INT;
	}
	else if (suffix.long_suffix)
	{
		candidates[count++] = FT_LONG_INT;
		if (!decimal)
			candidates[count++] = FT_UNSIGNED_LONG_INT;
		candidates[count++] = FT_LONG_LONG_INT;
		if (!decimal)
			candidates[count++] = FT_UNSIGNED_LONG_LONG_INT;
	}
	else if (suffix.long_long_suffix)
	{
		candidates[count++] = FT_LONG_LONG_INT;
		if (!decimal)
			candidates[count++] = FT_UNSIGNED_LONG_LONG_INT;
	}
	else if (decimal)
	{
		candidates[count++] = FT_INT;
		candidates[count++] = FT_LONG_INT;
		candidates[count++] = FT_LONG_LONG_INT;
	}
	else
	{
		candidates[count++] = FT_INT;
		candidates[count++] = FT_UNSIGNED_INT;
		candidates[count++] = FT_LONG_INT;
		candidates[count++] = FT_UNSIGNED_LONG_INT;
		candidates[count++] = FT_LONG_LONG_INT;
		candidates[count++] = FT_UNSIGNED_LONG_LONG_INT;
	}

	for (unsigned index = 0; index < count; ++index)
	{
		if (Fits(value, candidates[index]))
		{
			found = true;
			return candidates[index];
		}
	}
	found = false;
	return FT_INT;
}

// Reads a `binary-exponent-part` (`p` or `P`), which every hexadecimal
// floating-literal requires and no decimal one may use.
bool ReadBinaryExponentPart(const std::string& text, std::size_t position, std::size_t& end)
{
	if (position >= text.size() || (text[position] != 'p' && text[position] != 'P'))
		return false;
	++position;
	if (position < text.size() && (text[position] == '+' || text[position] == '-'))
		++position;
	std::size_t digits_begin = position;
	while (position < text.size() && IsDigit(text[position]))
		++position;
	if (position == digits_begin)
		return false;
	end = position;
	return true;
}

// Reads the `exponent-part` of a decimal floating-literal.
bool ReadExponentPart(const std::string& text, std::size_t position, std::size_t& end)
{
	if (position >= text.size() || (text[position] != 'e' && text[position] != 'E'))
		return false;
	++position;
	if (position < text.size() && (text[position] == '+' || text[position] == '-'))
		++position;
	std::size_t digits_begin = position;
	while (position < text.size() && IsDigit(text[position]))
		++position;
	if (position == digits_begin)
		return false;
	end = position;
	return true;
}

// The numeric part of a floating-literal, without its `floating-suffix` or
// ud-suffix.  Returns the position the numeric part ends at, or 0 when the
// spelling has no floating-literal prefix.  `hexadecimal` reports which of the
// two grammars matched.
std::size_t ReadFloatingNumber(const std::string& text, bool& hexadecimal)
{
	std::size_t size = text.size();
	hexadecimal = size >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');

	if (hexadecimal)
	{
		DigitRun whole = ReadDigits(text, 2, 16);
		std::size_t cursor = whole.end;
		if (cursor < size && text[cursor] == '.')
		{
			// hexadecimal-fractional-constant: digits_opt . digits
			DigitRun fraction = ReadDigits(text, cursor + 1, 16);
			if (fraction.count == 0 && whole.count == 0)
				return 0;
			std::size_t exponent_end = 0;
			if (!ReadBinaryExponentPart(text, fraction.end, exponent_end))
				return 0;
			return exponent_end;
		}
		if (whole.count == 0)
			return 0;
		std::size_t exponent_end = 0;
		if (!ReadBinaryExponentPart(text, cursor, exponent_end))
			return 0;
		return exponent_end;
	}

	// fractional-constant exponent-part_opt, or digit-sequence exponent-part.
	DigitRun whole = ReadDigits(text, 0, 10);
	std::size_t cursor = whole.end;
	if (cursor < size && text[cursor] == '.')
	{
		DigitRun fraction = ReadDigits(text, cursor + 1, 10);
		if (fraction.count == 0 && whole.count == 0)
			return 0;
		std::size_t exponent_end = 0;
		if (ReadExponentPart(text, fraction.end, exponent_end))
			return exponent_end;
		return fraction.end;
	}
	if (whole.count == 0)
		return 0;
	std::size_t exponent_end = 0;
	if (!ReadExponentPart(text, cursor, exponent_end))
		return 0;
	return exponent_end;
}

bool IsFloatingSuffix(char c)
{
	return c == 'f' || c == 'F' || c == 'l' || c == 'L';
}

// True when the text from `position` to the end is one identifier, which is
// what a ud-suffix has to be.
bool IsWholeIdentifier(const std::string& text, std::size_t position)
{
	if (position >= text.size() || !IsIdentifierStart(text[position]))
		return false;
	for (++position; position < text.size(); ++position)
	{
		if (!IsIdentifierBody(text[position]))
			return false;
	}
	return true;
}

} // namespace

void ClassifyPPNumber(const std::string& spelling, NumericLiteral& literal)
{
	literal.kind = NUM_INVALID;
	literal.integer_value = 0;
	literal.ud_suffix.clear();
	literal.prefix_length = 0;

	// The digit run of the integer-literal forms, kept for the user-defined
	// reading below: a ud-suffix follows a literal *without* its
	// integer-suffix.  A `0` prefix selects octal, `0x` hexadecimal and `0b`
	// binary; only a bare decimal run is decimal, which is the distinction
	// 2.14.2's type table is written in terms of.
	int radix = 10;
	std::size_t digits_begin = 0;
	bool binary = spelling.size() >= 2 && spelling[0] == '0' &&
		(spelling[1] == 'b' || spelling[1] == 'B');
	bool hexadecimal = spelling.size() >= 2 && spelling[0] == '0' &&
		(spelling[1] == 'x' || spelling[1] == 'X');
	if (binary)
	{
		radix = 2;
		digits_begin = 2;
	}
	else if (hexadecimal)
	{
		radix = 16;
		digits_begin = 2;
	}
	else if (spelling.size() >= 2 && spelling[0] == '0')
	{
		radix = 8;
	}
	DigitRun digits = ReadDigits(spelling, digits_begin, radix);

	if (digits.count > 0 && !digits.overflow)
	{
		IntegerSuffix suffix;
		if (ReadIntegerSuffix(spelling, digits.end, suffix))
		{
			bool found = false;
			EFundamentalType type = SelectIntegerType(radix == 10, suffix,
			                                         digits.value, found);
			if (found)
			{
				literal.kind = NUM_INTEGER;
				literal.integer_value = digits.value;
				literal.integer_type = type;
				return;
			}
		}
	}

	bool floating_hexadecimal = false;
	std::size_t floating_end = ReadFloatingNumber(spelling, floating_hexadecimal);
	if (floating_end != 0)
	{
		if (floating_end == spelling.size())
		{
			literal.kind = NUM_FLOATING;
			literal.integer_type = FT_DOUBLE;
			return;
		}
		if (IsFloatingSuffix(spelling[floating_end]) && floating_end + 1 == spelling.size())
		{
			literal.kind = NUM_FLOATING;
			literal.integer_type = (spelling[floating_end] == 'f' || spelling[floating_end] == 'F')
				? FT_FLOAT : FT_LONG_DOUBLE;
			return;
		}
		if (spelling[floating_end] == '_' && IsWholeIdentifier(spelling, floating_end))
		{
			literal.kind = NUM_UD_FLOATING;
			literal.prefix_length = floating_end;
			literal.ud_suffix.assign(spelling, floating_end, std::string::npos);
			return;
		}
	}

	if (digits.count > 0 && digits.end < spelling.size() && spelling[digits.end] == '_' &&
		IsWholeIdentifier(spelling, digits.end))
	{
		literal.kind = NUM_UD_INTEGER;
		literal.prefix_length = digits.end;
		literal.ud_suffix.assign(spelling, digits.end, std::string::npos);
		return;
	}

	literal.kind = NUM_INVALID;
}

namespace
{

// The System V AMD64 ABI stores the x87 80-bit extended value in sixteen
// bytes and leaves the six trailing bytes as padding.  The reference reads
// them as zero, so they are written explicitly rather than left to whatever
// the compiler happened to place on the stack.
void StoreLongDouble(long double value, std::string& bytes)
{
	const std::size_t significant = 10;
	std::size_t size = sizeof(value);
	bytes.assign(size, 0);
	std::memcpy(&bytes[0], &value, size < significant ? size : significant);
}

bool HasHexadecimalPrefix(const std::string& text)
{
	return text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
}

// The starter kit's scan is the course's required one, and it is used for
// every literal it converts.  Two cases are outside what a C++11 stream
// extraction can express, and in both the reference frontend reports the C
// library's correctly rounded result:
//
//   - 2.14.4 has a hexadecimal floating form, but the C++11 iostream grammar
//     does not, so `iss >> x` reads `0x1.8p3` as `0` and stops;
//   - an out-of-range value is stored as the type's extreme rather than as the
//     infinite value the C library returns.
//
// Both are read with `strto*`, which C99 and C++11 require to accept the
// hexadecimal form and which is correctly rounded.  The handout makes the
// range check optional and untested for PA2, so this changes no required
// behaviour; it removes a divergence from the reference.  A literal that
// really is the type's extreme is converted to that same extreme by `strto*`,
// so preferring its result cannot lose a representable one.
float ScanFloatText(const std::string& text)
{
	if (HasHexadecimalPrefix(text))
		return std::strtof(text.c_str(), 0);
	float value = PA2Decode_float(text);
	return value == std::numeric_limits<float>::max()
		? std::strtof(text.c_str(), 0) : value;
}

double ScanDoubleText(const std::string& text)
{
	if (HasHexadecimalPrefix(text))
		return std::strtod(text.c_str(), 0);
	double value = PA2Decode_double(text);
	return value == std::numeric_limits<double>::max()
		? std::strtod(text.c_str(), 0) : value;
}

long double ScanLongDoubleText(const std::string& text)
{
	if (HasHexadecimalPrefix(text))
		return std::strtold(text.c_str(), 0);
	long double value = PA2Decode_long_double(text);
	return value == std::numeric_limits<long double>::max()
		? std::strtold(text.c_str(), 0) : value;
}

} // namespace

void ScanFloatingLiteral(EFundamentalType type, const std::string& text, std::string& bytes)
{
	if (type == FT_FLOAT)
	{
		float value = ScanFloatText(text);
		bytes.assign(reinterpret_cast<const char*>(&value), sizeof(value));
		return;
	}
	if (type == FT_LONG_DOUBLE)
	{
		StoreLongDouble(ScanLongDoubleText(text), bytes);
		return;
	}
	double value = ScanDoubleText(text);
	bytes.assign(reinterpret_cast<const char*>(&value), sizeof(value));
}

} // namespace posttoken
} // namespace cppgm