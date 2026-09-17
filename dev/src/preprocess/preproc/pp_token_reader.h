// Phase 3's callbacks as preprocessing-token records.
//
// `PPTokenizer` reports one callback per preprocessing-token and asks its
// consumer nothing but whether a location is wanted.  `PPTokenReader` is the
// consumer that wants one: it turns each callback into a `PPToken` stamped with
// the file index it was handed and the physical line the tokenizer reported,
// and passes it straight on.  The callback's spelling is borrowed, so the
// record that outlives it owns its own copy - and nothing else does.
//
// The reader keeps no vector: the preprocessor consumes each record as it
// arrives, which is what keeps a translation unit's cost at its source buffer
// and the construct currently being preprocessed rather than at every token it
// contains.
//
// `RetokenizeSpelling` is the same pipeline over a synthesized spelling, which
// is what the `##` operator and the phase-7 validity of a paste need: a pasted
// spelling must form exactly one preprocessing-token or the translation unit is
// rejected.  That spelling is one token's worth of text, so its records are
// collected.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "preprocess/preproc/pp_token.h"
#include "preprocess/tokens/IPPTokenStream.h"

namespace cppgm
{
namespace preprocess
{

// Where a preprocessing-token record goes once its callback has returned.
class IPPStreamTokenSink
{
public:
	virtual void OnPreprocessingToken(PPToken token) = 0;

	virtual ~IPPStreamTokenSink() {}
};

class PPTokenReader : public IPPTokenStream
{
public:
	PPTokenReader(std::uint32_t file, IPPStreamTokenSink& sink)
		: sink_(sink)
		, file_(file)
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

private:
	void Push(EPPTokenKind kind, const std::string& spelling)
	{
		PPToken token;
		token.spelling = spelling;
		token.file = file_;
		token.line = static_cast<std::uint32_t>(line_);
		token.kind = kind;
		sink_.OnPreprocessingToken(std::move(token));
	}

	IPPStreamTokenSink& sink_;
	std::uint32_t file_;
	std::size_t line_;
};

// Tokenizes a synthesized spelling.  The result is returned without the
// trailing eof and without whitespace or new-line records; a caller that needs
// "exactly one preprocessing-token" checks the size, and one that needs the
// phase-7 classification of the paste hands the single record on.
std::vector<PPToken> RetokenizeSpelling(const std::string& spelling, std::uint32_t file,
                                        std::uint32_t line);

} // namespace preprocess
} // namespace cppgm
