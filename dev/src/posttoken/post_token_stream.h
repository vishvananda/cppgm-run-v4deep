// Translation phase 7's tokenization: preprocessing-tokens to tokens.
//
// `PostTokenStream` is an `IPPTokenStream`, so it consumes the phase 3
// recogniser's callbacks directly and never sees a token vector.  Every
// preprocessing-token except a string-literal is classified and reported
// inside its own callback; the string-literal forms are the one place where
// phase 6 needs state, so a maximal sequence of adjacent string-literal
// preprocessing-tokens is held until the next token of another kind (or eof)
// arrives.
//
// String concatenation crosses white space, new-lines and comments, because
// phases 3 and 4 have already discarded those; the recogniser reports them and
// this stream ignores them, which keeps the sequence open across them.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "posttoken/post_literal.h"
#include "posttoken/post_token_sink.h"
#include "posttoken/pp_number.h"
#include "preprocess/tokens/IPPTokenStream.h"

namespace cppgm
{
namespace posttoken
{

class PostTokenStream : public IPPTokenStream
{
public:
	explicit PostTokenStream(IPostTokenSink& sink)
		: sink_(sink)
	{}

	void emit_whitespace_sequence() {}
	void emit_new_line() {}
	void emit_header_name(const std::string& data);
	void emit_identifier(const std::string& data);
	void emit_pp_number(const std::string& data);
	void emit_character_literal(const std::string& data);
	void emit_user_defined_character_literal(const std::string& data);
	void emit_string_literal(const std::string& data);
	void emit_user_defined_string_literal(const std::string& data);
	void emit_preprocessing_op_or_punc(const std::string& data);
	void emit_non_whitespace_char(const std::string& data);
	void emit_eof();

	// Resolves a pending maximal sequence of adjacent string-literals now,
	// rather than at the next preprocessing-token of another kind.  A consumer
	// that ends a run of tokens for a reason of its own - PA3 ends one at every
	// logical line, because phase 3's `new-line` splits them - calls this so a
	// group cannot concatenate across the boundary.
	void FinishGroup();

private:
	// One string-literal spelling inside the pending maximal sequence.  The
	// bounds index into `group_text_`, which holds the spellings joined by a
	// single space - the source the concatenated token prints.
	struct PendingLiteral
	{
		ELiteralPrefix prefix;
		bool raw;
		bool has_suffix;
		std::size_t spelling_end;
		std::size_t body_begin;
		std::size_t body_end;
		std::size_t suffix_begin;
	};

	void AppendStringLiteral(const std::string& spelling);
	void FlushGroup();
	void EmitCharacterLiteral(const std::string& spelling, bool user_defined);
	void EmitPPNumber(const std::string& spelling);

	IPostTokenSink& sink_;
	std::vector<PendingLiteral> group_;
	std::string group_text_;
};

} // namespace posttoken
} // namespace cppgm