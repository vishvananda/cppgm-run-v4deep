#include "posttoken/post_token_stream.h"

namespace cppgm
{
namespace posttoken
{

namespace
{

// The ud-suffix rule the course fixes for this stage: a ud-suffix has to begin
// with an underscore.  A spelling whose suffix does not is not a
// user-defined-literal, and the whole preprocessing-token is invalid.
bool IsAcceptableUDSuffix(const std::string& suffix)
{
	return suffix.empty() || suffix[0] == '_';
}

} // namespace

void PostTokenStream::emit_header_name(const std::string& data)
{
	FlushGroup();
	sink_.EmitInvalid(data);
}

void PostTokenStream::emit_identifier(const std::string& data)
{
	FlushGroup();
	ETokenType type;
	if (LookupSimpleToken(data.data(), data.size(), type))
		sink_.EmitSimple(data, type);
	else
		sink_.EmitIdentifier(data);
}

void PostTokenStream::emit_pp_number(const std::string& data)
{
	FlushGroup();
	EmitPPNumber(data);
}

void PostTokenStream::emit_character_literal(const std::string& data)
{
	FlushGroup();
	EmitCharacterLiteral(data, false);
}

void PostTokenStream::emit_user_defined_character_literal(const std::string& data)
{
	FlushGroup();
	EmitCharacterLiteral(data, true);
}

void PostTokenStream::emit_string_literal(const std::string& data)
{
	AppendStringLiteral(data);
}

void PostTokenStream::emit_user_defined_string_literal(const std::string& data)
{
	AppendStringLiteral(data);
}

void PostTokenStream::emit_preprocessing_op_or_punc(const std::string& data)
{
	FlushGroup();
	ETokenType type;
	// `#`, `##`, `%:` and `%:%:` are not in the course table and have no token
	// of their own, so they are the invalid preprocessing-op-or-puncs.
	if (LookupSimpleToken(data.data(), data.size(), type))
		sink_.EmitSimple(data, type);
	else
		sink_.EmitInvalid(data);
}

void PostTokenStream::emit_non_whitespace_char(const std::string& data)
{
	FlushGroup();
	sink_.EmitInvalid(data);
}

void PostTokenStream::emit_eof()
{
	FlushGroup();
	sink_.EmitEof();
}

void PostTokenStream::EmitPPNumber(const std::string& spelling)
{
	NumericLiteral literal;
	ClassifyPPNumber(spelling, literal);
	switch (literal.kind)
	{
	case NUM_INTEGER:
	{
		std::string bytes(FundamentalTypeSize(literal.integer_type), '\0');
		unsigned long long value = literal.integer_value;
		for (std::size_t index = 0; index < bytes.size(); ++index)
			bytes[index] = static_cast<char>((value >> (8 * index)) & 0xFF);
		sink_.EmitLiteral(spelling, literal.integer_type, bytes);
		return;
	}
	case NUM_FLOATING:
	{
		// The scan leaves `text` untouched, so it reads the spelling in place
		// rather than paying a copy on every floating-literal token.
		std::string bytes;
		ScanFloatingLiteral(literal.integer_type, spelling, bytes);
		sink_.EmitLiteral(spelling, literal.integer_type, bytes);
		return;
	}
	case NUM_UD_INTEGER:
		sink_.EmitUserDefinedInteger(spelling, literal.ud_suffix,
		                             spelling.substr(0, literal.prefix_length));
		return;
	case NUM_UD_FLOATING:
		sink_.EmitUserDefinedFloating(spelling, literal.ud_suffix,
		                              spelling.substr(0, literal.prefix_length));
		return;
	default:
		sink_.EmitInvalid(spelling);
		return;
	}
}

void PostTokenStream::EmitCharacterLiteral(const std::string& spelling, bool user_defined)
{
	CharacterLiteralSpelling split;
	SplitCharacterLiteralSpelling(spelling, split);

	std::string suffix;
	if (user_defined)
	{
		// The refusal is decided from the spelling, so an unusable suffix never
		// builds the string it would have been reported under.
		if (split.suffix_begin < spelling.size() && spelling[split.suffix_begin] != '_')
		{
			sink_.EmitInvalid(spelling);
			return;
		}
		suffix.assign(spelling, split.suffix_begin, std::string::npos);
	}

	unsigned long long code_point = 0;
	if (!IsCharacterPrefix(split.prefix) ||
		!DecodeCharacterLiteralBody(spelling, split, code_point) ||
		!IsValidCharacterCodePoint(code_point))
	{
		sink_.EmitInvalid(spelling);
		return;
	}

	// The course definition: an ordinary character-literal is `char` when its
	// code point is one UTF-8 code unit and `int` otherwise; `u`, `U` and `L`
	// take their standard type and are ill-formed when the code point does not
	// fit one code unit.
	EFundamentalType type = FT_INT;
	std::size_t size = 0;
	if (split.prefix == PREFIX_ORDINARY)
	{
		type = code_point <= 127 ? FT_CHAR : FT_INT;
		size = code_point <= 127 ? 1 : 4;
	}
	else
	{
		type = LiteralPrefixElementType(split.prefix);
		size = FundamentalTypeSize(type);
		if (code_point >= (1ull << (8 * size)))
		{
			sink_.EmitInvalid(spelling);
			return;
		}
	}

	std::string bytes(size, '\0');
	for (std::size_t index = 0; index < size; ++index)
		bytes[index] = static_cast<char>((code_point >> (8 * index)) & 0xFF);
	if (user_defined)
		sink_.EmitUserDefinedCharacter(spelling, suffix, type, bytes);
	else
		sink_.EmitLiteral(spelling, type, bytes);
}

void PostTokenStream::AppendStringLiteral(const std::string& spelling)
{
	StringLiteralSpelling split;
	SplitStringLiteralSpelling(spelling, split);

	if (!group_text_.empty())
		group_text_.push_back(' ');
	std::size_t base = group_text_.size();
	group_text_.append(spelling);

	PendingLiteral pending;
	pending.prefix = split.prefix;
	pending.raw = split.raw;
	pending.has_suffix = split.suffix_begin < spelling.size();
	pending.spelling_end = base + spelling.size();
	pending.body_begin = base + split.body_begin;
	pending.body_end = base + split.body_end;
	pending.suffix_begin = base + split.suffix_begin;
	group_.push_back(pending);
}

void PostTokenStream::FlushGroup()
{
	if (group_.empty())
		return;

	// 2.14.5.13 and 2.14.8.8: the whole maximal sequence takes one
	// encoding-prefix and one ud-suffix; two different non-default values make
	// the sequence ill-formed.
	ELiteralPrefix prefix = PREFIX_ORDINARY;
	bool prefix_seen = false;
	bool prefix_conflict = false;
	std::string suffix;
	bool suffix_seen = false;
	bool suffix_conflict = false;

	for (std::size_t index = 0; index < group_.size(); ++index)
	{
		const PendingLiteral& literal = group_[index];
		if (literal.prefix != PREFIX_ORDINARY)
		{
			if (prefix_seen && prefix != literal.prefix)
				prefix_conflict = true;
			prefix = literal.prefix;
			prefix_seen = true;
		}
		if (literal.has_suffix)
		{
			// The suffix is this literal's own tail, not the rest of the group.
			// Only the first spelling is materialised: the group carries one
			// suffix value or none, so a later element only has to agree with
			// it, and comparing against the buffer costs no allocation.
			const std::size_t length = literal.spelling_end - literal.suffix_begin;
			if (!suffix_seen)
			{
				suffix.assign(group_text_, literal.suffix_begin, length);
				suffix_seen = true;
			}
			else if (suffix.size() != length ||
				group_text_.compare(literal.suffix_begin, length, suffix) != 0)
			{
				suffix_conflict = true;
			}
		}
	}

	const std::string source = group_text_;
	bool ill_formed = prefix_conflict || suffix_conflict ||
		(suffix_seen && !IsAcceptableUDSuffix(suffix));

	std::string code_units;
	if (!ill_formed)
	{
		for (std::size_t index = 0; index < group_.size() && !ill_formed; ++index)
		{
			const PendingLiteral& literal = group_[index];
			StringLiteralSpelling split;
			split.prefix = literal.prefix;
			split.raw = literal.raw;
			split.body_begin = literal.body_begin;
			split.body_end = literal.body_end;
			split.suffix_begin = literal.suffix_begin;
			ill_formed = !EncodeStringLiteralBody(group_text_, split, prefix, code_units);
		}
	}

	if (ill_formed)
	{
		sink_.EmitInvalid(source);
		group_.clear();
		group_text_.clear();
		return;
	}

	unsigned width = LiteralPrefixWidth(prefix);
	std::size_t unit = width / 8;
	code_units.append(unit, '\0');
	std::size_t count = code_units.size() / unit;
	EFundamentalType type = LiteralPrefixElementType(prefix);
	if (suffix_seen)
		sink_.EmitUserDefinedStringArray(source, suffix, count, type, code_units);
	else
		sink_.EmitLiteralArray(source, count, type, code_units);

	group_.clear();
	group_text_.clear();
}

} // namespace posttoken
} // namespace cppgm