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

} // namespace

PPTokenizer::PPTokenizer(TranslatedSource& source, IPPTokenStream& output)
	: source_(source)
	, output_(output)
	, token_byte_offset_(0)
	, line_start_(true)
	, after_hash_(false)
	, after_include_(false)
{}

int PPTokenizer::CodeAt(std::size_t ahead) const
{
	return source_.CodeAt(ahead);
}

bool PPTokenizer::MatchesOperator(std::size_t ahead, const char* spelling) const
{
	std::size_t offset = 0;
	for (const char* unit = spelling; *unit != '\0'; ++unit, ++offset)
	{
		if (CodeAt(ahead + offset) != static_cast<unsigned char>(*unit))
			return false;
	}
	return true;
}

// Longest-match recognition of the preprocessing-op-or-punc spellings, plus the
// 2.5.3 exception that a `<` followed by `::` is a token of its own unless the
// code point after the three is `:` or `>`.
//
// Only spellings that begin with the leading code point can match, so the
// leading code point selects the candidate set and the comparisons run from the
// longest spelling to the shortest.
std::size_t PPTokenizer::MatchOperator(std::size_t ahead) const
{
	int lead = CodeAt(ahead);
	if (lead < 0 || lead > 0x7F)
		return 0;

	if (lead == '<' && CodeAt(ahead + 1) == ':' && CodeAt(ahead + 2) == ':')
	{
		int following = CodeAt(ahead + 3);
		if (following != ':' && following != '>')
			return 1;
	}

	switch (lead)
	{
	case '#':
		return MatchesOperator(ahead, "##") ? 2 : 1;
	case '%':
		if (MatchesOperator(ahead, "%:%:"))
			return 4;
		if (MatchesOperator(ahead, "%>") || MatchesOperator(ahead, "%:") ||
			MatchesOperator(ahead, "%="))
			return 2;
		return 1;
	case '&':
		if (MatchesOperator(ahead, "&&") || MatchesOperator(ahead, "&="))
			return 2;
		return 1;
	case '|':
		if (MatchesOperator(ahead, "||") || MatchesOperator(ahead, "|="))
			return 2;
		return 1;
	case '*':
		return MatchesOperator(ahead, "*=") ? 2 : 1;
	case '^':
		return MatchesOperator(ahead, "^=") ? 2 : 1;
	case '=':
		return MatchesOperator(ahead, "==") ? 2 : 1;
	case '!':
		return MatchesOperator(ahead, "!=") ? 2 : 1;
	case '/':
		return MatchesOperator(ahead, "/=") ? 2 : 1;
	case '+':
		if (MatchesOperator(ahead, "++") || MatchesOperator(ahead, "+="))
			return 2;
		return 1;
	case '-':
		if (MatchesOperator(ahead, "->*"))
			return 3;
		if (MatchesOperator(ahead, "->") || MatchesOperator(ahead, "--") ||
			MatchesOperator(ahead, "-="))
			return 2;
		return 1;
	case '.':
		if (MatchesOperator(ahead, "..."))
			return 3;
		return MatchesOperator(ahead, ".*") ? 2 : 1;
	case ':':
		if (MatchesOperator(ahead, "::") || MatchesOperator(ahead, ":>"))
			return 2;
		return 1;
	case '<':
		if (MatchesOperator(ahead, "<<="))
			return 3;
		if (MatchesOperator(ahead, "<<") || MatchesOperator(ahead, "<=") ||
			MatchesOperator(ahead, "<:") || MatchesOperator(ahead, "<%"))
			return 2;
		return 1;
	case '>':
		if (MatchesOperator(ahead, ">>="))
			return 3;
		if (MatchesOperator(ahead, ">>") || MatchesOperator(ahead, ">="))
			return 2;
		return 1;
	case '{': case '}': case '[': case ']': case '(': case ')':
	case ';': case '?': case ',': case '~':
		return 1;
	default:
		return 0;
	}
}

void PPTokenizer::BeginToken()
{
	spelling_.clear();
	token_byte_offset_ = source_.RawByteOffset();
}

// Appends the next `count` code points to the token spelling and drops them
// from the cursor.
void PPTokenizer::Consume(std::size_t count)
{
	for (std::size_t index = 0; index < count; ++index)
	{
		int code_point = CodeAt(index);
		if (code_point == kEndOfFile)
			break;
		AppendCodePointUtf8(code_point, spelling_);
	}
	source_.Advance(count);
}

void PPTokenizer::ReportLocation()
{
	SourceLocation location = source_.LocationOf(token_byte_offset_);
	output_.set_source_location(location.line, location.column);
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
		int code_point = CodeAt(0);
		if (code_point == kEndOfFile)
		{
			output_.emit_eof();
			return;
		}
		BeginToken();
		if (code_point == kLineFeed)
		{
			Consume(1);
			ReportLocation();
			NoteEmitted(role_new_line);
			output_.emit_new_line();
			continue;
		}
		if (IsHorizontalWhitespace(code_point) ||
			(code_point == '/' && (CodeAt(1) == '*' || CodeAt(1) == '/')))
		{
			ScanWhitespaceSequence();
			continue;
		}
		if (after_include_ && StartsHeaderName())
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
		if (IsDigit(code_point) || (code_point == '.' && IsDigit(CodeAt(1))))
		{
			ScanPPNumber();
			continue;
		}
		if (IsIdentifierStart(code_point))
		{
			ScanIdentifierLike();
			continue;
		}
		std::size_t length = MatchOperator(0);
		if (length != 0)
		{
			EmitOperator(length);
			continue;
		}
		EmitNonWhitespaceCharacter();
	}
}

void PPTokenizer::EmitOperator(std::size_t length)
{
	int first = CodeAt(0);
	int second = CodeAt(1);
	bool starts_directive = (length == 1 && first == '#') ||
		(length == 2 && first == '%' && second == ':');
	Consume(length);
	ReportLocation();
	output_.emit_preprocessing_op_or_punc(spelling_);
	NoteEmitted(starts_directive ? role_hash : role_other);
}

void PPTokenizer::EmitNonWhitespaceCharacter()
{
	Consume(1);
	ReportLocation();
	output_.emit_non_whitespace_char(spelling_);
	NoteEmitted(role_other);
}

void PPTokenizer::SkipBlockComment()
{
	source_.Advance(2);
	for (;;)
	{
		int code_point = CodeAt(0);
		if (code_point == kEndOfFile)
			throw SourceError("unterminated block comment");
		if (code_point == '*' && CodeAt(1) == '/')
		{
			source_.Advance(2);
			return;
		}
		source_.Advance(1);
	}
}

// A whitespace-sequence is a maximal run of non-new-line whitespace and
// comments.  Because comments count as whitespace, several comments and the
// spacing around them form one sequence.
void PPTokenizer::ScanWhitespaceSequence()
{
	for (;;)
	{
		int code_point = CodeAt(0);
		if (IsHorizontalWhitespace(code_point))
		{
			source_.Advance(1);
			continue;
		}
		if (code_point == '/' && CodeAt(1) == '*')
		{
			SkipBlockComment();
			continue;
		}
		if (code_point == '/' && CodeAt(1) == '/')
		{
			source_.Advance(2);
			while (CodeAt(0) != kLineFeed && CodeAt(0) != kEndOfFile)
				source_.Advance(1);
			continue;
		}
		break;
	}
	ReportLocation();
	NoteEmitted(role_whitespace);
	output_.emit_whitespace_sequence();
}

// A header-name needs at least one h-char or q-char.  `#include <>` and
// `#include ""` therefore fall back to ordinary tokenization: the delimiters
// are an operator or a string literal rather than a header-name.
bool PPTokenizer::StartsHeaderName() const
{
	int opening = CodeAt(0);
	if (opening != '<' && opening != '"')
		return false;
	int closing = opening == '<' ? '>' : '"';
	int following = CodeAt(1);
	return following != kEndOfFile && following != kLineFeed && following != closing;
}

void PPTokenizer::ScanHeaderName()
{
	int closing = CodeAt(0) == '<' ? '>' : '"';
	Consume(1);
	for (;;)
	{
		int code_point = CodeAt(0);
		if (code_point == kEndOfFile || code_point == kLineFeed)
			throw SourceError("unterminated header name");
		Consume(1);
		if (code_point == closing)
			break;
	}
	ReportLocation();
	output_.emit_header_name(spelling_);
	NoteEmitted(role_other);
}

void PPTokenizer::ScanPPNumber()
{
	Consume(1);
	for (;;)
	{
		int code_point = CodeAt(0);
		if (IsDigit(code_point) || IsNondigit(code_point) || InAnnexE1(code_point) ||
			code_point == '.')
		{
			Consume(1);
			continue;
		}
		std::size_t length = spelling_.size();
		char previous = length == 0 ? '\0' : spelling_[length - 1];
		if ((code_point == '+' || code_point == '-') && (previous == 'e' || previous == 'E'))
		{
			Consume(1);
			continue;
		}
		break;
	}
	ReportLocation();
	output_.emit_pp_number(spelling_);
	NoteEmitted(role_other);
}

// `u8`, `u`, `U` and `L` introduce a literal only when the code point that
// follows is the matching quote or a raw string `R"`; otherwise the spelling is
// an ordinary identifier.
void PPTokenizer::ScanIdentifierLike()
{
	int code_point = CodeAt(0);
	if (code_point == 'u' || code_point == 'U' || code_point == 'L')
	{
		int second = CodeAt(1);
		if (code_point == 'u' && second == '8')
		{
			int third = CodeAt(2);
			if (third == '"' || (third == 'R' && CodeAt(3) == '"'))
			{
				ScanStringLiteral(2);
				return;
			}
		}
		else if (second == '"' || (second == 'R' && CodeAt(2) == '"'))
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
	else if (code_point == 'R' && CodeAt(1) == '"')
	{
		ScanRawStringLiteral(1);
		return;
	}
	ScanIdentifier();
}

void PPTokenizer::ScanIdentifier()
{
	Consume(1);
	while (IsIdentifierBody(CodeAt(0)))
		Consume(1);
	ReportLocation();
	if (IsIdentifierLikeOperator(spelling_))
	{
		output_.emit_preprocessing_op_or_punc(spelling_);
		NoteEmitted(role_other);
		return;
	}
	bool is_include = spelling_ == "include";
	output_.emit_identifier(spelling_);
	NoteEmitted(is_include ? role_include : role_other);
}

bool PPTokenizer::HasHexQuad(std::size_t ahead, std::size_t count) const
{
	for (std::size_t offset = 0; offset < count; ++offset)
	{
		if (!IsHexDigit(CodeAt(ahead + offset)))
			return false;
	}
	return true;
}

void PPTokenizer::SkipEscapeSequence()
{
	int next = CodeAt(1);
	if (IsSimpleEscape(next))
	{
		Consume(2);
		return;
	}
	if (IsOctalDigit(next))
	{
		Consume(2);
		std::size_t digits = 1;
		while (digits < 3 && IsOctalDigit(CodeAt(0)))
		{
			Consume(1);
			++digits;
		}
		return;
	}
	if (next == 'x')
	{
		Consume(2);
		if (!IsHexDigit(CodeAt(0)))
			throw SourceError("hex escape has no digits");
		while (IsHexDigit(CodeAt(0)))
			Consume(1);
		return;
	}
	if (next == 'u')
	{
		if (!HasHexQuad(2, 4))
			throw SourceError("invalid escape sequence");
		Consume(6);
		return;
	}
	if (next == 'U')
	{
		if (!HasHexQuad(2, 8))
			throw SourceError("invalid escape sequence");
		Consume(10);
		return;
	}
	throw SourceError("invalid escape sequence");
}

void PPTokenizer::ScanCharacterLiteral(std::size_t prefix_length)
{
	Consume(prefix_length + 1);
	for (;;)
	{
		int code_point = CodeAt(0);
		if (code_point == kEndOfFile || code_point == kLineFeed)
			throw SourceError("unterminated quoted literal");
		if (code_point == '\'')
		{
			Consume(1);
			break;
		}
		if (code_point == '\\')
		{
			SkipEscapeSequence();
			continue;
		}
		Consume(1);
	}
	if (IsIdentifierStart(CodeAt(0)))
	{
		Consume(1);
		while (IsIdentifierBody(CodeAt(0)))
			Consume(1);
		ReportLocation();
		output_.emit_user_defined_character_literal(spelling_);
		NoteEmitted(role_other);
		return;
	}
	ReportLocation();
	output_.emit_character_literal(spelling_);
	NoteEmitted(role_other);
}

void PPTokenizer::ScanStringLiteral(std::size_t prefix_length)
{
	if (CodeAt(prefix_length) == 'R')
	{
		ScanRawStringLiteral(prefix_length + 1);
		return;
	}
	Consume(prefix_length + 1);
	for (;;)
	{
		int code_point = CodeAt(0);
		if (code_point == kEndOfFile || code_point == kLineFeed)
			throw SourceError("unterminated quoted literal");
		if (code_point == '"')
		{
			Consume(1);
			break;
		}
		if (code_point == '\\')
		{
			SkipEscapeSequence();
			continue;
		}
		Consume(1);
	}
	if (IsIdentifierStart(CodeAt(0)))
	{
		Consume(1);
		while (IsIdentifierBody(CodeAt(0)))
			Consume(1);
		ReportLocation();
		output_.emit_user_defined_string_literal(spelling_);
		NoteEmitted(role_other);
		return;
	}
	ReportLocation();
	output_.emit_string_literal(spelling_);
	NoteEmitted(role_other);
}

// Raw string literals are read from the untranslated buffer between their
// opening and closing quotes: 2.2/1.3 reverts the phase 1 and 2 rewrites there,
// so trigraphs, universal-character-names and line splices inside a raw string
// stand as written.  The prefix and the opening quote still come from the
// translated stream, and decoding still validates UTF-8.
void PPTokenizer::ScanRawStringLiteral(std::size_t quote_offset)
{
	std::size_t quote_byte = source_.ByteOffsetAt(quote_offset);
	Consume(quote_offset + 1);

	const std::string& buffer = source_.Buffer();
	std::size_t cursor = quote_byte + 1;
	std::size_t delimiter_begin = cursor;
	std::size_t delimiter_length = 0;
	for (;;)
	{
		int code_point = 0;
		std::size_t next = 0;
		if (!source_.DecodeAt(cursor, code_point, next))
			throw SourceError("invalid raw string delimiter");
		if (code_point == '(')
			break;
		if (!IsRawDelimiterCodePoint(code_point))
			throw SourceError("invalid raw string delimiter");
		++delimiter_length;
		if (delimiter_length > kMaxRawDelimiterLength)
			throw SourceError("raw string delimiter is too long");
		cursor = next;
	}
	std::string delimiter = buffer.substr(delimiter_begin, cursor - delimiter_begin);
	++cursor;

	for (;;)
	{
		int code_point = 0;
		std::size_t next = 0;
		if (!source_.DecodeAt(cursor, code_point, next))
			throw SourceError("unterminated raw string literal");
		if (code_point == ')' && next + delimiter.size() < buffer.size() &&
			buffer.compare(next, delimiter.size(), delimiter) == 0 &&
			buffer[next + delimiter.size()] == '"')
		{
			cursor = next + delimiter.size() + 1;
			break;
		}
		cursor = next;
	}

	spelling_ += buffer.substr(quote_byte + 1, cursor - (quote_byte + 1));
	source_.ResumeAt(cursor);

	if (IsIdentifierStart(CodeAt(0)))
	{
		Consume(1);
		while (IsIdentifierBody(CodeAt(0)))
			Consume(1);
		ReportLocation();
		output_.emit_user_defined_string_literal(spelling_);
		NoteEmitted(role_other);
		return;
	}
	ReportLocation();
	output_.emit_string_literal(spelling_);
	NoteEmitted(role_other);
}

} // namespace preprocess
} // namespace cppgm
