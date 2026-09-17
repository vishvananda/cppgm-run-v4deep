#include "preprocess/tokens/pp_source_translation.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace cppgm
{
namespace preprocess
{

namespace
{

const int kLineFeed = 0x0A;
const int kByteOrderMark = 0xFEFF;
const int kMaxCodePoint = 0x10FFFF;

std::size_t SequenceLength(unsigned char lead)
{
	if (lead < 0x80)
		return 1;
	if (lead >= 0xC2 && lead <= 0xDF)
		return 2;
	if (lead >= 0xE0 && lead <= 0xEF)
		return 3;
	if (lead >= 0xF0 && lead <= 0xF4)
		return 4;
	throw SourceError("invalid UTF-8 leading byte");
}

int MinimumValueForLength(std::size_t length)
{
	if (length <= 2)
		return length == 1 ? 0 : 0x80;
	return length == 3 ? 0x800 : 0x10000;
}

bool HexDigitValue(int code_point, int& value)
{
	if (code_point >= '0' && code_point <= '9')
		value = code_point - '0';
	else if (code_point >= 'a' && code_point <= 'f')
		value = code_point - 'a' + 10;
	else if (code_point >= 'A' && code_point <= 'F')
		value = code_point - 'A' + 10;
	else
		return false;
	return true;
}

int TrigraphReplacement(int third)
{
	switch (third)
	{
	case '=': return '#';
	case '/': return '\\';
	case '\'': return '^';
	case '(': return '[';
	case ')': return ']';
	case '!': return '|';
	case '<': return '{';
	case '>': return '}';
	case '-': return '~';
	default: return -1;
	}
}

// 2.3/1: a universal-character-name may not designate a control character below
// 0xA0 other than $, @ or `, and may not designate a surrogate.
void ValidateUniversalCharacterValue(int value)
{
	if (value > kMaxCodePoint || (value >= 0xD800 && value <= 0xDFFF))
		throw SourceError("invalid universal character value");
	if (value < 0xA0 && value != 0x24 && value != 0x40 && value != 0x60)
		throw SourceError("invalid universal character value");
}

} // namespace

void AppendCodePointUtf8(int code_point, std::string& out)
{
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
}

TranslatedSource::TranslatedSource(std::string bytes)
	: buffer_(std::move(bytes))
	, front_(0)
	, next_byte_(0)
	, location_line_(0)
	, exhausted_(false)
{
	// A leading byte order mark is not part of the translation unit.
	if (buffer_.size() >= 3 && static_cast<unsigned char>(buffer_[0]) == 0xEF &&
		static_cast<unsigned char>(buffer_[1]) == 0xBB &&
		static_cast<unsigned char>(buffer_[2]) == 0xBF)
	{
		buffer_.erase(0, 3);
	}
	// 2.2/1.2: a source file that does not end in a new-line has one appended.
	if (!buffer_.empty() && buffer_[buffer_.size() - 1] != '\n')
		buffer_.push_back('\n');
	BuildLineIndex();
}

void TranslatedSource::BuildLineIndex()
{
	line_starts_.push_back(0);
	for (std::size_t index = 0; index < buffer_.size(); ++index)
	{
		if (buffer_[index] == '\n' && index + 1 < buffer_.size())
			line_starts_.push_back(index + 1);
	}
}

bool TranslatedSource::DecodeAt(std::size_t byte_offset, int& code_point,
	std::size_t& next) const
{
	if (byte_offset >= buffer_.size())
		return false;
	unsigned char lead = static_cast<unsigned char>(buffer_[byte_offset]);
	std::size_t length = SequenceLength(lead);
	if (byte_offset + length > buffer_.size())
		throw SourceError("truncated UTF-8 character");

	int value = length == 1 ? lead : (lead & (0xFF >> (length + 1)));
	for (std::size_t offset = 1; offset < length; ++offset)
	{
		unsigned char continuation = static_cast<unsigned char>(buffer_[byte_offset + offset]);
		if ((continuation & 0xC0) != 0x80)
			throw SourceError("invalid UTF-8 continuation byte");
		value = (value << 6) | (continuation & 0x3F);
	}
	if (value < MinimumValueForLength(length) || value > kMaxCodePoint ||
		(value >= 0xD800 && value <= 0xDFFF))
	{
		throw SourceError("invalid UTF-8 scalar value");
	}
	code_point = value;
	next = byte_offset + length;
	return true;
}

// The code point beginning at `at` after trigraph replacement, and the byte
// offset just past it.
bool TranslatedSource::EffectiveAt(std::size_t at, int& code_point, std::size_t& next) const
{
	std::size_t first_end = 0;
	int first = 0;
	if (!DecodeAt(at, first, first_end))
		return false;
	if (first == '?')
	{
		std::size_t second_end = 0;
		int second = 0;
		if (DecodeAt(first_end, second, second_end) && second == '?')
		{
			std::size_t third_end = 0;
			int third = 0;
			if (DecodeAt(second_end, third, third_end))
			{
				int replacement = TrigraphReplacement(third);
				if (replacement >= 0)
				{
					code_point = replacement;
					next = third_end;
					return true;
				}
			}
		}
	}
	code_point = first;
	next = first_end;
	return true;
}

// Reads `count` hexadecimal digits, each already past trigraph replacement.
bool TranslatedSource::ReadEscapeDigits(std::size_t at, std::size_t count, int& value,
	std::size_t& next) const
{
	value = 0;
	std::size_t cursor = at;
	for (std::size_t index = 0; index < count; ++index)
	{
		int digit = 0;
		std::size_t cursor_end = 0;
		if (!EffectiveAt(cursor, digit, cursor_end))
			return false;
		int hexadecimal = 0;
		if (!HexDigitValue(digit, hexadecimal))
			return false;
		value = (value << 4) | hexadecimal;
		cursor = cursor_end;
	}
	next = cursor;
	return true;
}

// Matches `u` or `U` followed by four or eight hexadecimal digits, all read
// after trigraph replacement.
bool TranslatedSource::ReadUniversalCharacterName(std::size_t at, int& value,
	std::size_t& end) const
{
	int introducer = 0;
	std::size_t introducer_end = 0;
	if (!EffectiveAt(at, introducer, introducer_end))
		return false;
	if (introducer != 'u' && introducer != 'U')
		return false;
	std::size_t digits_end = 0;
	if (!ReadEscapeDigits(introducer_end, introducer == 'u' ? 4 : 8, value, digits_end))
		return false;
	end = digits_end;
	return true;
}

void TranslatedSource::Push(int code_point, std::size_t byte_offset)
{
	Entry entry;
	entry.byte_offset = byte_offset;
	entry.code_point = code_point;
	pending_.push_back(entry);
}

void TranslatedSource::DropLast()
{
	pending_.pop_back();
}

// Produces one translated code point.  The order of the rewrites is the
// standard's: trigraph replacement, then universal-character-name replacement,
// then line splicing.  A backslash that does not introduce a
// universal-character-name consumes the code point after it verbatim, so a
// doubled backslash shields its neighbour from replacement; splicing then
// removes any backslash that ends up immediately before a new-line.
void TranslatedSource::Produce()
{
	if (next_byte_ >= buffer_.size())
	{
		exhausted_ = true;
		return;
	}

	std::size_t after = 0;
	int code_point = 0;
	EffectiveAt(next_byte_, code_point, after);

	if (code_point == '\\')
	{
		int value = 0;
		std::size_t name_end = 0;
		if (ReadUniversalCharacterName(after, value, name_end))
		{
			ValidateUniversalCharacterValue(value);
			Push(value, next_byte_);
			next_byte_ = name_end;
			return;
		}

		Push(code_point, next_byte_);
		if (after >= buffer_.size())
		{
			next_byte_ = after;
			return;
		}
		std::size_t partner_end = 0;
		int partner = 0;
		EffectiveAt(after, partner, partner_end);
		if (partner == kLineFeed)
		{
			DropLast();
			next_byte_ = partner_end;
			return;
		}
		Push(partner, after);
		next_byte_ = partner_end;
		if (partner != '\\' || partner_end >= buffer_.size())
			return;
		// A backslash arriving as the second half of a pair can itself be the
		// backslash of a splice.
		std::size_t following_end = 0;
		int following = 0;
		if (EffectiveAt(partner_end, following, following_end) && following == kLineFeed)
		{
			DropLast();
			next_byte_ = following_end;
		}
		return;
	}

	Push(code_point, next_byte_);
	next_byte_ = after;
}

void TranslatedSource::Compact()
{
	pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(front_));
	front_ = 0;
}

void TranslatedSource::Refill(std::size_t wanted)
{
	while (!exhausted_ && pending_.size() - front_ <= wanted)
		Produce();
}

void TranslatedSource::ResumeAt(std::size_t byte_offset)
{
	pending_.clear();
	front_ = 0;
	next_byte_ = byte_offset;
	exhausted_ = byte_offset >= buffer_.size();
}

SourceLocation TranslatedSource::LocationOf(std::size_t byte_offset)
{
	SourceLocation location;
	location.line = 1;
	location.column = 1;
	if (line_starts_.empty())
		return location;
	if (byte_offset >= buffer_.size())
		byte_offset = buffer_.size() == 0 ? 0 : buffer_.size() - 1;

	if (location_line_ >= line_starts_.size() || line_starts_[location_line_] > byte_offset)
	{
		std::vector<std::size_t>::const_iterator it =
			std::upper_bound(line_starts_.begin(), line_starts_.end(), byte_offset);
		location_line_ = it == line_starts_.begin()
			? 0 : static_cast<std::size_t>(it - line_starts_.begin()) - 1;
	}
	while (location_line_ + 1 < line_starts_.size() &&
		line_starts_[location_line_ + 1] <= byte_offset)
	{
		++location_line_;
	}
	location.line = location_line_ + 1;
	location.column = byte_offset - line_starts_[location_line_] + 1;
	return location;
}

} // namespace preprocess
} // namespace cppgm
