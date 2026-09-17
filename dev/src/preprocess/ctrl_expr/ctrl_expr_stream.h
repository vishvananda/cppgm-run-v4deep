// PA3's logical-line splitter and its token accumulator.
//
// `CtrlExprStream` is the phase 3 consumer the handout's design note suggests:
// it forwards the preprocessing-token stream to the inherited PA2 post-token
// pass and ends a logical line at every `new-line`.  `CtrlExprSink` is that
// pass's sink: it records the line's typed tokens and, at the line boundary,
// runs the controlling-expression parser over them.
//
// The token vector is the one place a PA3 token outlives its callback - a
// logical line has to be complete before `defined ( a` can be told from a valid
// expression - so it is cleared per line and its capacity is kept, and nothing
// but the records the grammar reads is retained: an identifier's spelling is
// not copied, because the two facts derived from it (`is_defined_word` and the
// mock `defined` result) are decided while the callback's spelling is still
// alive.  The evaluator the line is handed to owns the two stacks the parse
// works on, so one object serves every line and their capacity is kept with the
// vector's.

#pragma once

#include <cstddef>
#include <ostream>
#include <string>
#include <vector>

#include "posttoken/fundamental_type.h"
#include "posttoken/post_token_sink.h"
#include "posttoken/post_token_stream.h"
#include "posttoken/simple_token.h"
#include "preprocess/ctrl_expr/ctrl_expr_parser.h"
#include "preprocess/ctrl_expr/ctrl_expr_token.h"
#include "preprocess/tokens/IPPTokenStream.h"

namespace cppgm
{
namespace preprocess
{

// Collects one logical line's typed tokens and evaluates it.
class CtrlExprSink : public posttoken::IPostTokenSink
{
public:
	explicit CtrlExprSink(std::ostream& out)
		: out_(out)
		, rejected_(false)
	{}

	void EmitInvalid(const std::string& source);
	void EmitSimple(const std::string& source, posttoken::ETokenType type);
	void EmitIdentifier(const std::string& source);
	void EmitLiteral(const std::string& source, posttoken::EFundamentalType type,
	                 const std::string& bytes);
	void EmitLiteralArray(const std::string& source, std::size_t count,
	                      posttoken::EFundamentalType type, const std::string& bytes);
	void EmitUserDefinedCharacter(const std::string& source, const std::string& suffix,
	                              posttoken::EFundamentalType type, const std::string& bytes);
	void EmitUserDefinedStringArray(const std::string& source, const std::string& suffix,
	                                std::size_t count, posttoken::EFundamentalType type,
	                                const std::string& bytes);
	void EmitUserDefinedInteger(const std::string& source, const std::string& suffix,
	                            const std::string& prefix);
	void EmitUserDefinedFloating(const std::string& source, const std::string& suffix,
	                             const std::string& prefix);
	void EmitEof();

	// The logical line ended: print its result, or print nothing at all when
	// the line was blank.  A line whose only token was rejected still prints
	// `error`, which is why the empty buffer and the flag are separate.
	void EndOfLine();

	// The line's value, without rendering it: `false` is the answer the text
	// view spells `error`.  PA4's `#if` reads this, because the truth of a
	// controlling expression is a property of the value and not of its decimal
	// spelling.  The line is left as it was, so a caller that wants both calls
	// this before ending the line.
	bool Result(CtrlExprValue& value);

	// Writes the output block to the stream.
	void Flush();

private:
	// The output is the tool's contract and the dominant cost, so a line is
	// composed in one block and handed over in chunks rather than flushed
	// through the stream one field at a time.
	static const std::size_t kFlushThreshold = 1 << 16;

	void Append(const char* text) { buffer_.append(text); }
	void EndLine();

	std::ostream& out_;
	std::vector<CtrlToken> tokens_;
	// One evaluator for the whole run: its value and operator stacks keep their
	// capacity across lines the way `tokens_` does.
	CtrlExpression expression_;
	std::string buffer_;
	// The line's emit-time rejection rules already failed, so it is `error`
	// whatever the rest of it parses as: an invalid token, a user-defined
	// literal, or a literal that is not an `integral-literal`.
	bool rejected_;
};

// The phase 3 consumer that splits the stream into logical lines.
class CtrlExprStream : public IPPTokenStream
{
public:
	explicit CtrlExprStream(CtrlExprSink& sink)
		: post_(sink)
		, sink_(sink)
	{}

	// A whitespace-sequence carries no token; the post-token pass ignores it
	// too, so the splitter has nothing to forward.
	void emit_whitespace_sequence() {}

	void emit_new_line()
	{
		EndLine();
	}

	void emit_header_name(const std::string& data) { post_.emit_header_name(data); }
	void emit_identifier(const std::string& data) { post_.emit_identifier(data); }
	void emit_pp_number(const std::string& data) { post_.emit_pp_number(data); }
	void emit_character_literal(const std::string& data) { post_.emit_character_literal(data); }
	void emit_user_defined_character_literal(const std::string& data)
	{
		post_.emit_user_defined_character_literal(data);
	}
	void emit_string_literal(const std::string& data) { post_.emit_string_literal(data); }
	void emit_user_defined_string_literal(const std::string& data)
	{
		post_.emit_user_defined_string_literal(data);
	}
	void emit_preprocessing_op_or_punc(const std::string& data)
	{
		post_.emit_preprocessing_op_or_punc(data);
	}
	void emit_non_whitespace_char(const std::string& data)
	{
		post_.emit_non_whitespace_char(data);
	}

	// The end of the input is also the end of its last logical line, and the
	// post-token pass reports `eof` to the sink once the pending maximal
	// string-literal sequence has been resolved.
	void emit_eof()
	{
		post_.emit_eof();
	}

private:
	// A logical line ends where the preprocessing-token stream has a `new-line`
	// or reaches eof.  It is also the boundary a pending maximal sequence of
	// adjacent string-literals must not cross, so the post-token pass is told
	// to resolve the group here rather than at the end of the input.
	void EndLine()
	{
		post_.FinishGroup();
		sink_.EndOfLine();
	}

	posttoken::PostTokenStream post_;
	CtrlExprSink& sink_;
};

} // namespace preprocess
} // namespace cppgm
