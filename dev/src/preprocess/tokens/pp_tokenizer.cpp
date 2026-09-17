#include "preprocess/tokens/pp_tokenizer.h"

#include <cstddef>
#include <string>
#include <vector>

namespace cppgm
{
namespace preprocess
{

namespace
{

const int kEndOfFile = -1;
const int kLineFeed = 0x0A;
const std::size_t kMaxRawDelimiterLength = 16;

struct Range
{
	int first;
	int last;
};

// Annex E.1: code points that may appear in an identifier.
const Range kAnnexE1[] =
{
	{0xA8, 0xA8}, {0xAA, 0xAA}, {0xAD, 0xAD}, {0xAF, 0xAF},
	{0xB2, 0xB5}, {0xB7, 0xBA}, {0xBC, 0xBE}, {0xC0, 0xD6},
	{0xD8, 0xF6}, {0xF8, 0xFF}, {0x100, 0x167F}, {0x1681, 0x180D},
	{0x180F, 0x1FFF}, {0x200B, 0x200D}, {0x202A, 0x202E}, {0x203F, 0x2040},
	{0x2054, 0x2054}, {0x2060, 0x206F}, {0x2070, 0x218F}, {0x2460, 0x24FF},
	{0x2776, 0x2793}, {0x2C00, 0x2DFF}, {0x2E80, 0x2FFF}, {0x3004, 0x3007},
	{0x3021, 0x302F}, {0x3031, 0x303F}, {0x3040, 0xD7FF}, {0xF900, 0xFD3D},
	{0xFD40, 0xFDCF}, {0xFDF0, 0xFE44}, {0xFE47, 0xFFFD}, {0x10000, 0x1FFFD},
	{0x20000, 0x2FFFD}, {0x30000, 0x3FFFD}, {0x40000, 0x4FFFD}, {0x50000, 0x5FFFD},
	{0x60000, 0x6FFFD}, {0x70000, 0x7FFFD}, {0x80000, 0x8FFFD}, {0x90000, 0x9FFFD},
	{0xA0000, 0xAFFFD}, {0xB0000, 0xBFFFD}, {0xC0000, 0xCFFFD}, {0xD0000, 0xDFFFD},
	{0xE0000, 0xEFFFD}
};

// Annex E.2: code points that may not begin an identifier.
const Range kAnnexE2[] =
{
	{0x300, 0x36F}, {0x1DC0, 0x1DFF}, {0x20D0, 0x20FF}, {0xFE20, 0xFE2F}
};

bool InRanges(const Range* ranges, std::size_t count, int code_point)
{
	for (std::size_t index = 0; index < count; ++index)
	{
		if (code_point < ranges[index].first)
			return false;
		if (code_point <= ranges[index].last)
			return true;
	}
	return false;
}

bool InAnnexE1(int code_point)
{
	return code_point >= 0x80 &&
		InRanges(kAnnexE1, sizeof(kAnnexE1) / sizeof(kAnnexE1[0]), code_point);
}

bool InAnnexE2(int code_point)
{
	return code_point >= 0x80 &&
		InRanges(kAnnexE2, sizeof(kAnnexE2) / sizeof(kAnnexE2[0]), code_point);
}

bool IsDigit(int code_point)
{
	return code_point >= '0' && code_point <= '9';
}

bool IsNondigit(int code_point)
{
	return (code_point >= 'a' && code_point <= 'z') ||
		(code_point >= 'A' && code_point <= 'Z') || code_point == '_';
}

bool IsIdentifierStart(int code_point)
{
	if (IsNondigit(code_point))
		return true;
	return InAnnexE1(code_point) && !InAnnexE2(code_point);
}

bool IsIdentifierBody(int code_point)
{
	return IsNondigit(code_point) || IsDigit(code_point) || InAnnexE1(code_point);
}

bool IsHorizontalWhitespace(int code_point)
{
	return code_point == ' ' || code_point == '\t' || code_point == '\v' ||
		code_point == '\f' || code_point == '\r';
}

bool IsOctalDigit(int code_point)
{
	return code_point >= '0' && code_point <= '7';
}

bool IsHexDigit(int code_point)
{
	return IsDigit(code_point) || (code_point >= 'a' && code_point <= 'f') ||
		(code_point >= 'A' && code_point <= 'F');
}

bool IsSimpleEscape(int code_point)
{
	switch (code_point)
	{
	case '\'': case '"': case '?': case '\\':
	case 'a': case 'b': case 'f': case 'n':
	case 'r': case 't': case 'v':
		return true;
	default:
		return false;
	}
}

// 2.14.5: the characters a raw string delimiter may not contain.
bool IsRawDelimiterCodePoint(int code_point)
{
	switch (code_point)
	{
	case ' ': case '(': case ')': case '\\': case '"':
	case '\t': case '\v': case '\f': case '\n':
		return false;
	default:
		return true;
	}
}

const char* const kIdentifierLikeOperators[] =
{
	"new", "delete", "and", "and_eq", "bitand", "bitor", "compl", "not",
	"not_eq", "or", "or_eq", "xor", "xor_eq"
};

bool IsIdentifierLikeOperator(const std::string& spelling)
{
	for (std::size_t index = 0;
		index < sizeof(kIdentifierLikeOperators) / sizeof(kIdentifierLikeOperators[0]);
		++index)
	{
		if (spelling == kIdentifierLikeOperators[index])
			return true;
	}
	return false;
}

std::string EncodeTranslated(const std::vector<TranslatedCodePoint>& codes, std::size_t begin,
	std::size_t end)
{
	std::string encoded;
	encoded.reserve(end - begin);
	for (std::size_t index = begin; index < end; ++index)
		AppendCodePointUtf8(codes[index].code_point, encoded);
	return encoded;
}

} // namespace

PPTokenizer::PPTokenizer(const TranslatedSource& source, IPPTokenStream& output)
	: source_(source)
	, output_(output)
	, position_(0)
	, line_start_(true)
	, after_hash_(false)
	, after_include_(false)
{}

int PPTokenizer::CodeAt(std::size_t index) const
{
	if (index >= source_.translated.size())
		return kEndOfFile;
	return source_.translated[index].code_point;
}

int PPTokenizer::Current() const
{
	return CodeAt(position_);
}

bool PPTokenizer::MatchesOperator(std::size_t index, const char* spelling) const
{
	std::size_t offset = 0;
	for (const char* unit = spelling; *unit != '\0'; ++unit, ++offset)
	{
		if (CodeAt(index + offset) != static_cast<unsigned char>(*unit))
			return false;
	}
	return true;
}

// Longest-match recognition of the preprocessing-op-or-punc spellings, plus the
// 2.5.3 exception that a `<` followed by `::` is a token of its own unless the
// code point after the three is `:` or `>`.
std::size_t PPTokenizer::MatchOperator(std::size_t index) const
{
	if (CodeAt(index) == '<' && CodeAt(index + 1) == ':' && CodeAt(index + 2) == ':')
	{
		int following = CodeAt(index + 3);
		if (following != ':' && following != '>')
			return 1;
	}

	static const char* const four[] = { "%:%:" };
	static const char* const three[] = { "...", "<<=", ">>=", "->*" };
	static const char* const two[] =
	{
		"##", "<:", ":>", "<%", "%>", "%:", "::", ".*", "->", "++", "--", "<<",
		">>", "<=", ">=", "==", "!=", "&&", "||", "+=", "-=", "*=", "/=", "%=",
		"^=", "&=", "|="
	};
	static const char* const one[] =
	{
		"{", "}", "[", "]", "#", "(", ")", ";", ":", "?", ".", "+", "-", "*",
		"/", "%", "^", "&", "|", "~", "!", "=", "<", ">", ","
	};

	if (MatchesOperator(index, four[0]))
		return 4;
	for (std::size_t slot = 0; slot < sizeof(three) / sizeof(three[0]); ++slot)
	{
		if (MatchesOperator(index, three[slot]))
			return 3;
	}
	for (std::size_t slot = 0; slot < sizeof(two) / sizeof(two[0]); ++slot)
	{
		if (MatchesOperator(index, two[slot]))
			return 2;
	}
	for (std::size_t slot = 0; slot < sizeof(one) / sizeof(one[0]); ++slot)
	{
		if (MatchesOperator(index, one[slot]))
			return 1;
	}
	return 0;
}

void PPTokenizer::ReportLocation(std::size_t index) const
{
	if (index >= source_.translated.size())
		return;
	const TranslatedCodePoint& entry = source_.translated[index];
	output_.set_source_location(entry.line, entry.column);
}

std::string PPTokenizer::TranslatedSpelling(std::size_t begin, std::size_t end) const
{
	return EncodeTranslated(source_.translated, begin, end);
}

std::string PPTokenizer::PhysicalSpelling(std::size_t begin, std::size_t end) const
{
	return EncodeUtf8(source_.physical, begin, end);
}

void PPTokenizer::NoteEmitted(TokenRole role)
{
	switch (role)
	{
	case role_whitespace:
		return;
	case role_new_line:
		line_start_ = true;
		after_hash_ = false;
		after_include_ = false;
		return;
	case role_hash:
		after_hash_ = line_start_;
		after_include_ = false;
		line_start_ = false;
		return;
	case role_include:
		after_include_ = after_hash_;
		after_hash_ = false;
		line_start_ = false;
		return;
	default:
		line_start_ = false;
		after_hash_ = false;
		after_include_ = false;
		return;
	}
}

void PPTokenizer::Tokenize()
{
	for (;;)
	{
		std::size_t start = position_;
		int code_point = Current();
		if (code_point == kEndOfFile)
		{
			output_.emit_eof();
			return;
		}
		if (code_point == kLineFeed)
		{
			++position_;
			ReportLocation(start);
			NoteEmitted(role_new_line);
			output_.emit_new_line();
			continue;
		}
		if (IsHorizontalWhitespace(code_point) ||
			(code_point == '/' && (CodeAt(start + 1) == '*' || CodeAt(start + 1) == '/')))
		{
			ScanWhitespaceSequence();
			continue;
		}
		if (after_include_ && (code_point == '<' || code_point == '"'))
		{
			ScanHeaderName();
			continue;
		}
		if (code_point == '"')
		{
			ScanStringLiteral(0);
			continue;
		}
		if (code_point == '\'')
		{
			ScanCharacterLiteral(0);
			continue;
		}
		if (IsDigit(code_point) || (code_point == '.' && IsDigit(CodeAt(start + 1))))
		{
			ScanPPNumber();
			continue;
		}
		if (IsIdentifierStart(code_point))
		{
			ScanIdentifierLike();
			continue;
		}
		std::size_t length = MatchOperator(start);
		if (length != 0)
		{
			EmitOperator(start, length);
			continue;
		}
		EmitNonWhitespaceCharacter(start);
	}
}

void PPTokenizer::EmitOperator(std::size_t begin, std::size_t length)
{
	bool starts_directive = (length == 1 && CodeAt(begin) == '#') ||
		(length == 2 && CodeAt(begin) == '%' && CodeAt(begin + 1) == ':');
	position_ = begin + length;
	ReportLocation(begin);
	output_.emit_preprocessing_op_or_punc(TranslatedSpelling(begin, position_));
	NoteEmitted(starts_directive ? role_hash : role_other);
}

void PPTokenizer::EmitNonWhitespaceCharacter(std::size_t begin)
{
	position_ = begin + 1;
	ReportLocation(begin);
	output_.emit_non_whitespace_char(TranslatedSpelling(begin, position_));
	NoteEmitted(role_other);
}

void PPTokenizer::SkipBlockComment()
{
	position_ += 2;
	for (;;)
	{
		int code_point = Current();
		if (code_point == kEndOfFile)
			throw SourceError("unterminated block comment");
		if (code_point == '*' && CodeAt(position_ + 1) == '/')
		{
			position_ += 2;
			return;
		}
		++position_;
	}
}

// A whitespace-sequence is a maximal run of non-new-line whitespace and
// comments.  Because comments count as whitespace, several comments and the
// spacing around them form one sequence.
void PPTokenizer::ScanWhitespaceSequence()
{
	std::size_t start = position_;
	for (;;)
	{
		int code_point = Current();
		if (IsHorizontalWhitespace(code_point))
		{
			++position_;
			continue;
		}
		if (code_point == '/' && CodeAt(position_ + 1) == '*')
		{
			SkipBlockComment();
			continue;
		}
		if (code_point == '/' && CodeAt(position_ + 1) == '/')
		{
			position_ += 2;
			while (Current() != kLineFeed && Current() != kEndOfFile)
				++position_;
			continue;
		}
		break;
	}
	ReportLocation(start);
	NoteEmitted(role_whitespace);
	output_.emit_whitespace_sequence();
}

void PPTokenizer::ScanHeaderName()
{
	std::size_t start = position_;
	int closing = Current() == '<' ? '>' : '"';
	++position_;
	for (;;)
	{
		int code_point = Current();
		if (code_point == kEndOfFile || code_point == kLineFeed)
			throw SourceError("unterminated header name");
		++position_;
		if (code_point == closing)
			break;
	}
	ReportLocation(start);
	output_.emit_header_name(TranslatedSpelling(start, position_));
	NoteEmitted(role_other);
}

void PPTokenizer::ScanPPNumber()
{
	std::size_t start = position_;
	++position_;
	for (;;)
	{
		int code_point = Current();
		if (IsDigit(code_point) || IsNondigit(code_point) || InAnnexE1(code_point) ||
			code_point == '.')
		{
			++position_;
			continue;
		}
		int previous = CodeAt(position_ - 1);
		if ((code_point == '+' || code_point == '-') &&
			(previous == 'e' || previous == 'E'))
		{
			++position_;
			continue;
		}
		break;
	}
	ReportLocation(start);
	output_.emit_pp_number(TranslatedSpelling(start, position_));
	NoteEmitted(role_other);
}

// `u8`, `u`, `U` and `L` introduce a literal only when the code point that
// follows is the matching quote or a raw string `R"`; otherwise the spelling is
// an ordinary identifier.
void PPTokenizer::ScanIdentifierLike()
{
	std::size_t start = position_;
	int code_point = Current();
	if (code_point == 'u' || code_point == 'U' || code_point == 'L')
	{
		int second = CodeAt(start + 1);
		if (code_point == 'u' && second == '8')
		{
			int third = CodeAt(start + 2);
			if (third == '"' || (third == 'R' && CodeAt(start + 3) == '"'))
			{
				ScanStringLiteral(2);
				return;
			}
		}
		else if (second == '"' || (second == 'R' && CodeAt(start + 2) == '"'))
		{
			ScanStringLiteral(1);
			return;
		}
		else if (second == '\'')
		{
			ScanCharacterLiteral(1);
			return;
		}
	}
	else if (code_point == 'R' && CodeAt(start + 1) == '"')
	{
		ScanRawStringLiteral(1);
		return;
	}
	ScanIdentifier();
}

void PPTokenizer::ScanIdentifier()
{
	std::size_t start = position_;
	++position_;
	while (IsIdentifierBody(Current()))
		++position_;
	ReportLocation(start);
	std::string spelling = TranslatedSpelling(start, position_);
	if (IsIdentifierLikeOperator(spelling))
	{
		output_.emit_preprocessing_op_or_punc(spelling);
		NoteEmitted(role_other);
		return;
	}
	output_.emit_identifier(spelling);
	NoteEmitted(spelling == "include" ? role_include : role_other);
}

void PPTokenizer::SkipEscapeSequence()
{
	int next = CodeAt(position_ + 1);
	if (IsSimpleEscape(next))
	{
		position_ += 2;
		return;
	}
	if (IsOctalDigit(next))
	{
		position_ += 2;
		std::size_t digits = 1;
		while (digits < 3 && IsOctalDigit(Current()))
		{
			++position_;
			++digits;
		}
		return;
	}
	if (next == 'x')
	{
		position_ += 2;
		if (!IsHexDigit(Current()))
			throw SourceError("hex escape has no digits");
		while (IsHexDigit(Current()))
			++position_;
		return;
	}
	if (next == 'u')
	{
		if (!HasHexQuad(position_ + 2, 4))
			throw SourceError("invalid escape sequence");
		position_ += 6;
		return;
	}
	if (next == 'U')
	{
		if (!HasHexQuad(position_ + 2, 8))
			throw SourceError("invalid escape sequence");
		position_ += 10;
		return;
	}
	throw SourceError("invalid escape sequence");
}

bool PPTokenizer::HasHexQuad(std::size_t begin, std::size_t count) const
{
	for (std::size_t offset = 0; offset < count; ++offset)
	{
		if (!IsHexDigit(CodeAt(begin + offset)))
			return false;
	}
	return true;
}

bool PPTokenizer::ScanUdSuffix()
{
	if (!IsIdentifierStart(Current()))
		return false;
	++position_;
	while (IsIdentifierBody(Current()))
		++position_;
	return true;
}

void PPTokenizer::ScanCharacterLiteral(std::size_t prefix_length)
{
	std::size_t start = position_;
	position_ = start + prefix_length + 1;
	for (;;)
	{
		int code_point = Current();
		if (code_point == kEndOfFile || code_point == kLineFeed)
			throw SourceError("unterminated quoted literal");
		if (code_point == '\'')
		{
			++position_;
			break;
		}
		if (code_point == '\\')
		{
			SkipEscapeSequence();
			continue;
		}
		++position_;
	}
	bool user_defined = ScanUdSuffix();
	ReportLocation(start);
	std::string spelling = TranslatedSpelling(start, position_);
	if (user_defined)
		output_.emit_user_defined_character_literal(spelling);
	else
		output_.emit_character_literal(spelling);
	NoteEmitted(role_other);
}

void PPTokenizer::ScanStringLiteral(std::size_t prefix_length)
{
	std::size_t start = position_;
	if (CodeAt(start + prefix_length) == 'R')
	{
		ScanRawStringLiteral(prefix_length + 1);
		return;
	}
	position_ = start + prefix_length + 1;
	for (;;)
	{
		int code_point = Current();
		if (code_point == kEndOfFile || code_point == kLineFeed)
			throw SourceError("unterminated quoted literal");
		if (code_point == '"')
		{
			++position_;
			break;
		}
		if (code_point == '\\')
		{
			SkipEscapeSequence();
			continue;
		}
		++position_;
	}
	bool user_defined = ScanUdSuffix();
	ReportLocation(start);
	std::string spelling = TranslatedSpelling(start, position_);
	if (user_defined)
		output_.emit_user_defined_string_literal(spelling);
	else
		output_.emit_string_literal(spelling);
	NoteEmitted(role_other);
}

std::size_t PPTokenizer::AdvancePastPhysical(std::size_t from, std::size_t physical_end) const
{
	std::size_t index = from;
	while (index < source_.translated.size() && source_.translated[index].physical < physical_end)
		++index;
	return index;
}

bool PPTokenizer::RawStringTerminatesAt(const std::vector<int>& physical, std::size_t at,
	std::size_t delimiter_begin, std::size_t delimiter_length) const
{
	if (at + delimiter_length + 1 >= physical.size())
		return false;
	for (std::size_t offset = 0; offset < delimiter_length; ++offset)
	{
		if (physical[at + 1 + offset] != physical[delimiter_begin + offset])
			return false;
	}
	return physical[at + delimiter_length + 1] == '"';
}

// Raw string literals are read from the untranslated code points: 2.2/1.3
// reverts the phase 1 and 2 rewrites between the opening and closing quotes,
// so trigraphs, universal-character-names and line splices inside them stand.
void PPTokenizer::ScanRawStringLiteral(std::size_t quote_offset)
{
	std::size_t start = position_;
	std::size_t quote_index = start + quote_offset;
	std::size_t physical_begin = source_.translated[start].physical;
	std::size_t delimiter_begin = source_.translated[quote_index].physical + 1;
	const std::vector<int>& physical = source_.physical;

	std::size_t cursor = delimiter_begin;
	std::size_t delimiter_length = 0;
	for (;;)
	{
		if (cursor >= physical.size())
			throw SourceError("invalid raw string delimiter");
		int code_point = physical[cursor];
		if (code_point == '(')
			break;
		if (!IsRawDelimiterCodePoint(code_point))
			throw SourceError("invalid raw string delimiter");
		++delimiter_length;
		if (delimiter_length > kMaxRawDelimiterLength)
			throw SourceError("raw string delimiter is too long");
		++cursor;
	}
	++cursor;

	for (;;)
	{
		if (cursor >= physical.size())
			throw SourceError("unterminated raw string literal");
		if (physical[cursor] == ')' &&
			RawStringTerminatesAt(physical, cursor, delimiter_begin, delimiter_length))
			break;
		++cursor;
	}
	std::size_t physical_end = cursor + delimiter_length + 2;

	position_ = AdvancePastPhysical(quote_index, physical_end);
	std::size_t suffix_begin = position_;
	bool user_defined = ScanUdSuffix();
	ReportLocation(start);
	std::string spelling = PhysicalSpelling(physical_begin, physical_end);
	if (user_defined)
	{
		spelling += TranslatedSpelling(suffix_begin, position_);
		output_.emit_user_defined_string_literal(spelling);
	}
	else
	{
		output_.emit_string_literal(spelling);
	}
	NoteEmitted(role_other);
}

} // namespace preprocess
} // namespace cppgm
