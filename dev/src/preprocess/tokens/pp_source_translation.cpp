#include "preprocess/tokens/pp_source_translation.h"

#include <cstddef>

namespace cppgm
{
namespace preprocess
{

namespace
{

const int kLineFeed = 0x0A;
const int kByteOrderMark = 0xFEFF;
const int kMaxCodePoint = 0x10FFFF;

bool IsContinuationByte(unsigned char byte)
{
	return (byte & 0xC0) == 0x80;
}

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

void DecodeSourceBytes(const std::string& bytes, std::vector<int>& physical)
{
	std::size_t index = 0;
	while (index < bytes.size())
	{
		unsigned char lead = static_cast<unsigned char>(bytes[index]);
		std::size_t length = SequenceLength(lead);
		if (index + length > bytes.size())
			throw SourceError("truncated UTF-8 character");

		int value = length == 1 ? lead : (lead & (0xFF >> (length + 1)));
		for (std::size_t offset = 1; offset < length; ++offset)
		{
			unsigned char continuation = static_cast<unsigned char>(bytes[index + offset]);
			if (!IsContinuationByte(continuation))
				throw SourceError("invalid UTF-8 continuation byte");
			value = (value << 6) | (continuation & 0x3F);
		}

		if (value < MinimumValueForLength(length))
			throw SourceError("invalid UTF-8 scalar value");
		if (value > kMaxCodePoint || (value >= 0xD800 && value <= 0xDFFF))
			throw SourceError("invalid UTF-8 scalar value");

		physical.push_back(value);
		index += length;
	}
}

// Removes a byte order mark at the start of a translation unit and appends the
// line feed that 2.2/1.2 requires the buffer to end with.  An empty buffer
// stays empty: there is no final source line to terminate.
void NormalizeBuffer(std::vector<int>& physical)
{
	if (!physical.empty() && physical[0] == kByteOrderMark)
		physical.erase(physical.begin());
	if (!physical.empty() && physical.back() != kLineFeed)
		physical.push_back(kLineFeed);
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

// Replaces each `??x` trigraph with its single character.  A `??` that is not a
// trigraph leaves the first question mark alone and rescans from the next code
// point, which is what makes `???=` become `?` followed by `#`.
void ReplaceTrigraphs(const std::vector<int>& physical, std::vector<int>& codes,
	std::vector<std::uint32_t>& origin)
{
	std::size_t index = 0;
	while (index < physical.size())
	{
		if (physical[index] == '?' && index + 2 < physical.size() &&
			physical[index + 1] == '?')
		{
			int replacement = TrigraphReplacement(physical[index + 2]);
			if (replacement >= 0)
			{
				codes.push_back(replacement);
				origin.push_back(static_cast<std::uint32_t>(index));
				index += 3;
				continue;
			}
		}
		codes.push_back(physical[index]);
		origin.push_back(static_cast<std::uint32_t>(index));
		++index;
	}
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

bool ReadHexQuad(const std::vector<int>& codes, std::size_t begin, std::size_t count, int& value)
{
	if (begin + count > codes.size())
		return false;
	value = 0;
	for (std::size_t offset = 0; offset < count; ++offset)
	{
		int digit = 0;
		if (!HexDigitValue(codes[begin + offset], digit))
			return false;
		value = (value << 4) | digit;
	}
	return true;
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

bool ReadUniversalCharacterName(const std::vector<int>& codes, std::size_t index, int& value,
	std::size_t& consumed)
{
	if (index + 1 >= codes.size())
		return false;
	if (codes[index + 1] == 'u')
	{
		if (!ReadHexQuad(codes, index + 2, 4, value))
			return false;
		consumed = 6;
		return true;
	}
	if (codes[index + 1] == 'U')
	{
		if (!ReadHexQuad(codes, index + 2, 8, value))
			return false;
		consumed = 10;
		return true;
	}
	return false;
}

// Replaces universal-character-names.  A backslash that does not introduce one
// is copied together with the code point that follows it, so a doubled
// backslash shields its neighbour from universal-character-name replacement
// while an odd run still ends in a live escape.
void ReplaceUniversalCharacterNames(const std::vector<int>& codes,
	const std::vector<std::uint32_t>& origin, std::vector<int>& replaced,
	std::vector<std::uint32_t>& replaced_origin)
{
	std::size_t index = 0;
	while (index < codes.size())
	{
		std::size_t consumed = 0;
		int value = 0;
		if (codes[index] == '\\' && ReadUniversalCharacterName(codes, index, value, consumed))
		{
			ValidateUniversalCharacterValue(value);
			replaced.push_back(value);
			replaced_origin.push_back(origin[index]);
			index += consumed;
			continue;
		}
		std::size_t run = codes[index] == '\\' && index + 1 < codes.size() ? 2 : 1;
		for (std::size_t offset = 0; offset < run; ++offset)
		{
			replaced.push_back(codes[index + offset]);
			replaced_origin.push_back(origin[index + offset]);
		}
		index += run;
	}
}

// 2.2/1.2: each backslash immediately followed by a new-line is deleted.
void SpliceLines(const std::vector<int>& codes, const std::vector<std::uint32_t>& origin,
	std::vector<TranslatedCodePoint>& translated)
{
	std::size_t index = 0;
	while (index < codes.size())
	{
		if (codes[index] == '\\' && index + 1 < codes.size() &&
			codes[index + 1] == kLineFeed)
		{
			index += 2;
			continue;
		}
		TranslatedCodePoint entry;
		entry.physical = origin[index];
		entry.line = 0;
		entry.column = 0;
		entry.code_point = codes[index];
		translated.push_back(entry);
		++index;
	}
}

void FillPhysicalPositions(const std::vector<int>& physical, std::vector<std::uint32_t>& lines,
	std::vector<std::uint32_t>& columns)
{
	lines.resize(physical.size());
	columns.resize(physical.size());
	std::uint32_t line = 1;
	std::uint32_t column = 1;
	for (std::size_t index = 0; index < physical.size(); ++index)
	{
		lines[index] = line;
		columns[index] = column;
		if (physical[index] == kLineFeed)
		{
			++line;
			column = 1;
		}
		else
		{
			++column;
		}
	}
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

std::string EncodeUtf8(const std::vector<int>& codes, std::size_t begin, std::size_t end)
{
	std::string encoded;
	encoded.reserve(end - begin);
	for (std::size_t index = begin; index < end; ++index)
		AppendCodePointUtf8(codes[index], encoded);
	return encoded;
}

TranslatedSource TranslateSource(const std::string& bytes)
{
	TranslatedSource source;
	DecodeSourceBytes(bytes, source.physical);
	NormalizeBuffer(source.physical);

	std::vector<std::uint32_t> lines;
	std::vector<std::uint32_t> columns;
	FillPhysicalPositions(source.physical, lines, columns);

	std::vector<int> trigraph_codes;
	std::vector<std::uint32_t> trigraph_origin;
	trigraph_codes.reserve(source.physical.size());
	trigraph_origin.reserve(source.physical.size());
	ReplaceTrigraphs(source.physical, trigraph_codes, trigraph_origin);

	std::vector<int> replaced_codes;
	std::vector<std::uint32_t> replaced_origin;
	replaced_codes.reserve(trigraph_codes.size());
	replaced_origin.reserve(trigraph_codes.size());
	ReplaceUniversalCharacterNames(trigraph_codes, trigraph_origin, replaced_codes,
		replaced_origin);

	std::vector<int>().swap(trigraph_codes);
	std::vector<std::uint32_t>().swap(trigraph_origin);
	SpliceLines(replaced_codes, replaced_origin, source.translated);

	for (std::size_t index = 0; index < source.translated.size(); ++index)
	{
		std::uint32_t physical = source.translated[index].physical;
		source.translated[index].line = lines[physical];
		source.translated[index].column = columns[physical];
	}
	return source;
}

} // namespace preprocess
} // namespace cppgm
