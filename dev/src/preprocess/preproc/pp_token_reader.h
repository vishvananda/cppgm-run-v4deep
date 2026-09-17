// Phase 3's callbacks as preprocessing-token records.
//
// `PPTokenizer` reports one callback per preprocessing-token and asks its
// consumer nothing but whether a location is wanted.  `PPTokenReader` is the
// consumer that wants one: it records each callback as a `PPToken` stamped with
// the file index it was handed and the physical line the tokenizer reported.
// No token vector is built anywhere else, and the reader's vector is the one
// place a token outlives its callback.
//
// `Retokenize` is the same pipeline over a synthesized spelling, which is what
// the `##` operator and the phase-7 validity of a paste need: a pasted spelling
// must form exactly one preprocessing-token or the translation unit is
// rejected.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "preprocess/preproc/pp_token.h"
#include "preprocess/tokens/IPPTokenStream.h"

namespace cppgm
{
namespace preprocess
{

class PPTokenReader : public IPPTokenStream
{
public:
	explicit PPTokenReader(std::uint32_t file)
		: file_(file)
		, line_(1)
	{}

	// The tokenizer must report a position before every token, so the reader
	// only has to remember the last one it was told.
	bool wants_source_location() const override { return true; }
	void set_source_line(std::size_t line) override { line_ = line; }
	void set_source_location(std::size_t line, std::size_t column) override
	{
		(void)column;
		line_ = line;
	}

	void emit_whitespace_sequence() override { Push(kPPWhitespace, " "); }
	void emit_new_line() override { Push(kPPNewLine, "\n"); }
	void emit_header_name(const std::string& data) override { Push(kPPHeaderName, data); }
	void emit_identifier(const std::string& data) override { Push(kPPIdentifier, data); }
	void emit_pp_number(const std::string& data) override { Push(kPPNumber, data); }
	void emit_character_literal(const std::string& data) override
	{
		Push(kPPCharacterLiteral, data);
	}
	void emit_user_defined_character_literal(const std::string& data) override
	{
		Push(kPPUserDefinedCharacterLiteral, data);
	}
	void emit_string_literal(const std::string& data) override
	{
		Push(kPPStringLiteral, data);
	}
	void emit_user_defined_string_literal(const std::string& data) override
	{
		Push(kPPUserDefinedStringLiteral, data);
	}
	void emit_preprocessing_op_or_punc(const std::string& data) override
	{
		Push(kPPOpOrPunc, data);
	}
	void emit_non_whitespace_char(const std::string& data) override
	{
		Push(kPPNonWhitespaceChar, data);
	}
	void emit_eof() override { Push(kPPEof, ""); }

	// The records the reader has taken so far.  The vector is left empty and
	// its capacity kept, so one reader can serve every file of a run.
	std::vector<PPToken>& Tokens() { return tokens_; }

private:
	void Push(EPPTokenKind kind, const std::string& spelling)
	{
		PPToken token;
		token.spelling = spelling;
		token.file = file_;
		token.line = static_cast<std::uint32_t>(line_);
		token.kind = kind;
		tokens_.push_back(std::move(token));
	}

	std::vector<PPToken> tokens_;
	std::uint32_t file_;
	std::size_t line_;
};

// Reads one translated source and returns its records, with the trailing eof.
std::vector<PPToken> ReadPreprocessingTokens(std::string bytes, std::uint32_t file);

// Tokenizes a synthesized spelling.  The result is returned without the
// trailing eof and without whitespace or new-line records; a caller that needs
// "exactly one preprocessing-token" checks the size, and one that needs the
// phase-7 classification of the paste hands the single record on.
std::vector<PPToken> RetokenizeSpelling(const std::string& spelling, std::uint32_t file,
                                        std::uint32_t line);

} // namespace preprocess
} // namespace cppgm