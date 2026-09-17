#include "posttoken/post_literal.h"

#include <cstring>

namespace cppgm
{
namespace posttoken
{

namespace
{

const unsigned kMaxCodePoint = 0x10FFFF;

// The largest numeric escape this reader will keep exactly.  Any escape value
// at or above it fails every code-unit width, so clamping here loses nothing
// and keeps the accumulator from wrapping.  A hexadecimal escape sequence has
// no digit limit, so the clamp is reachable.
const unsigned long long kEscapeOverflow = 0x100000000ull;

bool IsIdentifierStart(int code_point)
{
	return code_point == '_' || (code_point >= 'a' && code_point <= 'z') ||
		(code_point >= 'A' && code_point <= 'Z') || code_point >= 0x80;
}

// Decodes one code point of a spelling.  The preprocessing-token recogniser
// built the spelling from decoded code points, so the bytes are well formed
// UTF-8 and the decoder does not need a rejection path beyond the end test.
bool DecodeCodePoint(const std::string& text, std::size_t position, std::size_t end,
                     unsigned long long& code_point, std::size_t& next)
{
	if (position >= end)
		return false;
	unsigned char lead = static_cast<unsigned char>(text[position]);
	std::size_t length = 1;
	unsigned long long value = 0;
	if (lead < 0x80)
	{
		value = lead;
	}
	else if ((lead & 0xE0) == 0xC0)
	{
		length = 2;
		value = lead & 0x1F;
	}
	else if ((lead & 0xF0) == 0xE0)
	{
		length = 3;
		value = lead & 0x0F;
	}
	else if ((lead & 0xF8) == 0xF0)
	{
		length = 4;
		value = lead & 0x07;
	}
	else
	{
		return false;
	}
	if (position + length > end)
		return false;
	for (std::size_t index = 1; index < length; ++index)
	{
		unsigned char continuation = static_cast<unsigned char>(text[position + index]);
		if ((continuation & 0xC0) != 0x80)
			return false;
		value = (value << 6) | (continuation & 0x3F);
	}
	code_point = value;
	next = position + length;
	return true;
}

int HexDigitValue(int code_point)
{
	if (code_point >= '0' && code_point <= '9')
		return code_point - '0';
	if (code_point >= 'a' && code_point <= 'f')
		return code_point - 'a' + 10;
	if (code_point >= 'A' && code_point <= 'F')
		return code_point - 'A' + 10;
	return -1;
}

} // namespace

bool LiteralBodyReader::Next()
{
	if (position_ >= end_)
		return false;
	unsigned char lead = static_cast<unsigned char>(spelling_[position_]);
	if (lead == '\\')
		return ReadEscape();

	unsigned long long code_point = 0;
	std::size_t next = 0;
	if (!DecodeCodePoint(spelling_, position_, end_, code_point, next))
	{
		well_formed_ = false;
		return false;
	}
	position_ = next;
	numeric_ = false;
	value_ = code_point;
	return true;
}

bool LiteralBodyReader::ReadEscape()
{
	// 2.14.3.3 Table 7: the simple escape sequences, which name a character
	// and therefore contribute a code point rather than a code unit.
	++position_;
	if (position_ >= end_)
	{
		well_formed_ = false;
		return false;
	}
	int introducer = static_cast<unsigned char>(spelling_[position_]);
	unsigned long long value = 0;
	bool numeric = false;
	switch (introducer)
	{
	case '\'': case '"': case '?': case '\\':
	case 'a': case 'b': case 'f': case 'n': case 'r': case 't': case 'v':
	{
		static const char kIntroducers[] = "'\"?\\abfnrtv";
		static const unsigned kValues[] =
			{ 0x27, 0x22, 0x3F, 0x5C, 0x07, 0x08, 0x0C, 0x0A, 0x0D, 0x09, 0x0B };
		const char* at = std::strchr(kIntroducers, introducer);
		value = kValues[at - kIntroducers];
		++position_;
		break;
	}
	case '0': case '1': case '2': case '3':
	case '4': case '5': case '6': case '7':
	{
		numeric = true;
		unsigned digits = 0;
		while (digits < 3 && position_ < end_ &&
			spelling_[position_] >= '0' && spelling_[position_] <= '7')
		{
			value = value * 8 + static_cast<unsigned>(spelling_[position_] - '0');
			if (value > kEscapeOverflow)
				value = kEscapeOverflow;
			++position_;
			++digits;
		}
		break;
	}
	case 'x':
	{
		numeric = true;
		++position_;
		unsigned digits = 0;
		while (position_ < end_)
		{
			int digit = HexDigitValue(static_cast<unsigned char>(spelling_[position_]));
			if (digit < 0)
				break;
			value = value * 16 + static_cast<unsigned>(digit);
			if (value > kEscapeOverflow)
				value = kEscapeOverflow;
			++position_;
			++digits;
		}
		if (digits == 0)
		{
			well_formed_ = false;
			return false;
		}
		break;
	}
	case 'u':
	case 'U':
	{
		// A `universal-character-name` is a code point.  Phase 1 replaces it
		// before this stage sees it, so reaching here means the sequence is
		// being read from a spelling that phase 1 did not rewrite.
		std::size_t count = introducer == 'u' ? 4 : 8;
		++position_;
		if (position_ + count > end_)
		{
			well_formed_ = false;
			return false;
		}
		for (std::size_t index = 0; index < count; ++index)
		{
			int digit = HexDigitValue(static_cast<unsigned char>(spelling_[position_ + index]));
			if (digit < 0)
			{
				well_formed_ = false;
				return false;
			}
			value = value * 16 + static_cast<unsigned>(digit);
		}
		position_ += count;
		break;
	}
	default:
		well_formed_ = false;
		return false;
	}

	numeric_ = numeric;
	value_ = value;
	return true;
}

void SplitStringLiteralSpelling(const std::string& spelling, StringLiteralSpelling& out)
{
	std::size_t position = 0;
	if (spelling.compare(0, 3, "u8\"") == 0 || spelling.compare(0, 4, "u8R\"") == 0)
	{
		out.prefix = PREFIX_UTF8;
		position = 2;
	}
	else if (spelling.size() >= 2 && spelling[1] == '"' &&
		(spelling[0] == 'u' || spelling[0] == 'U' || spelling[0] == 'L'))
	{
		out.prefix = spelling[0] == 'u' ? PREFIX_UTF16 :
			(spelling[0] == 'U' ? PREFIX_UTF32 : PREFIX_WIDE);
		position = 1;
	}
	else if (spelling.size() >= 3 && spelling[1] == 'R' && spelling[2] == '"' &&
		(spelling[0] == 'u' || spelling[0] == 'U' || spelling[0] == 'L'))
	{
		out.prefix = spelling[0] == 'u' ? PREFIX_UTF16 :
			(spelling[0] == 'U' ? PREFIX_UTF32 : PREFIX_WIDE);
		position = 1;
	}

	std::size_t after = 0;
	if (spelling[position] == 'R' && spelling[position + 1] == '"')
	{
		// [lex.string]: a raw string literal's body reverts the phase 1 and 2
		// rewrites, so the spelling copies it verbatim between the opening
		// `(` and the matching `)delim"`.
		out.raw = true;
		std::size_t delimiter_begin = position + 2;
		std::size_t open = spelling.find('(', delimiter_begin);
		std::size_t delimiter_length = open - delimiter_begin;
		out.body_begin = open + 1;
		// The body ends at the first `)delim"`; a ud-suffix may follow it, so
		// the terminator has to be located rather than measured from the end.
		std::size_t close = out.body_begin;
		while (close < spelling.size())
		{
			if (spelling[close] == ')' &&
				close + delimiter_length + 1 < spelling.size() &&
				spelling.compare(close + 1, delimiter_length, spelling,
				                 delimiter_begin, delimiter_length) == 0 &&
				spelling[close + 1 + delimiter_length] == '"')
				break;
			++close;
		}
		out.body_end = close;
		after = close + delimiter_length + 2;
	}
	else
	{
		out.raw = false;
		out.body_begin = position + 1;
		std::size_t quote = out.body_begin;
		while (quote < spelling.size())
		{
			if (spelling[quote] == '\\')
			{
				quote += 2;
				continue;
			}
			if (spelling[quote] == '"')
				break;
			++quote;
		}
		out.body_end = quote;
		after = quote + 1;
	}
	out.suffix_begin = spelling.size();
	if (after < spelling.size() && IsIdentifierStart(static_cast<unsigned char>(spelling[after])))
		out.suffix_begin = after;
}

void SplitCharacterLiteralSpelling(const std::string& spelling, CharacterLiteralSpelling& out)
{
	std::size_t position = 0;
	if (spelling.size() >= 2 && spelling[1] == '\'' &&
		(spelling[0] == 'u' || spelling[0] == 'U' || spelling[0] == 'L'))
	{
		out.prefix = spelling[0] == 'u' ? PREFIX_UTF16 :
			(spelling[0] == 'U' ? PREFIX_UTF32 : PREFIX_WIDE);
		position = 1;
	}
	out.body_begin = position + 1;
	std::size_t quote = out.body_begin;
	while (quote < spelling.size())
	{
		if (spelling[quote] == '\\')
		{
			quote += 2;
			continue;
		}
		if (spelling[quote] == '\'')
			break;
		++quote;
	}
	out.body_end = quote;
	out.suffix_begin = spelling.size();
	std::size_t after = quote + 1;
	if (after < spelling.size() && IsIdentifierStart(static_cast<unsigned char>(spelling[after])))
		out.suffix_begin = after;
}

unsigned LiteralPrefixWidth(ELiteralPrefix prefix)
{
	switch (prefix)
	{
	case PREFIX_UTF16: return 16;
	case PREFIX_UTF32: return 32;
	case PREFIX_WIDE: return 32;
	default: return 8;
	}
}

EFundamentalType LiteralPrefixElementType(ELiteralPrefix prefix)
{
	switch (prefix)
	{
	case PREFIX_UTF16: return FT_CHAR16_T;
	case PREFIX_UTF32: return FT_CHAR32_T;
	case PREFIX_WIDE: return FT_WCHAR_T;
	default: return FT_CHAR;
	}
}

bool IsCharacterPrefix(ELiteralPrefix prefix)
{
	return prefix != PREFIX_UTF8;
}

bool AppendEncodedCodePoint(ELiteralPrefix prefix, unsigned long long code_point, std::string& out)
{
	unsigned width = LiteralPrefixWidth(prefix);
	if (width == 8)
	{
		if (code_point > kMaxCodePoint)
			return false;
		unsigned value = static_cast<unsigned>(code_point);
		if (value < 0x80)
		{
			out.push_back(static_cast<char>(value));
		}
		else if (value < 0x800)
		{
			out.push_back(static_cast<char>(0xC0 | (value >> 6)));
			out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
		}
		else if (value < 0x10000)
		{
			out.push_back(static_cast<char>(0xE0 | (value >> 12)));
			out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
		}
		else
		{
			out.push_back(static_cast<char>(0xF0 | (value >> 18)));
			out.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
		}
		return true;
	}

	if (width == 16)
	{
		if (code_point > kMaxCodePoint)
			return false;
		if (code_point < 0x10000)
		{
			unsigned value = static_cast<unsigned>(code_point);
			out.push_back(static_cast<char>(value & 0xFF));
			out.push_back(static_cast<char>((value >> 8) & 0xFF));
			return true;
		}
		// 2.14.5: a code point outside the BMP is a surrogate pair.
		unsigned value = static_cast<unsigned>(code_point - 0x10000);
		unsigned lead = 0xD800 + (value >> 10);
		unsigned trail = 0xDC00 + (value & 0x3FF);
		out.push_back(static_cast<char>(lead & 0xFF));
		out.push_back(static_cast<char>((lead >> 8) & 0xFF));
		out.push_back(static_cast<char>(trail & 0xFF));
		out.push_back(static_cast<char>((trail >> 8) & 0xFF));
		return true;
	}

	if (code_point > 0xFFFFFFFFull)
		return false;
	unsigned value = static_cast<unsigned>(code_point);
	for (unsigned index = 0; index < 4; ++index)
		out.push_back(static_cast<char>((value >> (8 * index)) & 0xFF));
	return true;
}

bool EncodeStringLiteralBody(const std::string& spelling,
                             const StringLiteralSpelling& split,
                             ELiteralPrefix prefix,
                             std::string& out)
{
	unsigned width = LiteralPrefixWidth(prefix);

	if (split.raw)
	{
		// A raw string literal has no escape sequences: every character of the
		// body is a source character and is encoded as a code point.
		std::size_t position = split.body_begin;
		while (position < split.body_end)
		{
			unsigned long long code_point = 0;
			std::size_t next = 0;
			if (!DecodeCodePoint(spelling, position, split.body_end, code_point, next))
				return false;
			if (!AppendEncodedCodePoint(prefix, code_point, out))
				return false;
			position = next;
		}
		return true;
	}

	LiteralBodyReader reader(spelling, split.body_begin, split.body_end);
	while (reader.Next())
	{
		if (!reader.IsNumericEscape())
		{
			if (!AppendEncodedCodePoint(prefix, reader.Value(), out))
				return false;
			continue;
		}
		// A numeric escape sequence contributes exactly one code unit, whose
		// value must be representable in the sequence's element type.
		if (reader.Value() >= (1ull << width))
			return false;
		unsigned long long value = reader.Value();
		for (unsigned index = 0; index < width / 8; ++index)
			out.push_back(static_cast<char>((value >> (8 * index)) & 0xFF));
	}
	return reader.WellFormed();
}

bool DecodeCharacterLiteralBody(const std::string& spelling,
                                const CharacterLiteralSpelling& split,
                                unsigned long long& code_point)
{
	LiteralBodyReader reader(spelling, split.body_begin, split.body_end);
	if (!reader.Next() || !reader.WellFormed())
		return false;
	code_point = reader.Value();
	// 2.14.3: a character-literal is exactly one c-char.
	return !reader.Next() && reader.WellFormed();
}

bool IsValidCharacterCodePoint(unsigned long long code_point)
{
	if (code_point > kMaxCodePoint)
		return false;
	return code_point < 0xD800 || code_point >= 0xE000;
}

} // namespace posttoken
} // namespace cppgm