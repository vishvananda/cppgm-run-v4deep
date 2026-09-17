#include "preprocess/ctrl_expr/ctrl_expr_stream.h"

#include "preprocess/ctrl_expr/ctrl_expr_parser.h"

namespace cppgm
{
namespace preprocess
{

using posttoken::EFundamentalType;
using posttoken::ETokenType;

void CtrlExprSink::EmitInvalid(const std::string&)
{
	// The line is `error` whatever the rest of it parses as, so the invalid
	// token is not recorded: nothing can make it an operand.
	rejected_ = true;
}

void CtrlExprSink::EmitSimple(const std::string& source, ETokenType type)
{
	CtrlToken token;
	token.kind = kCtrlSimple;
	token.simple = type;
	token.bits = 0;
	token.is_unsigned = false;
	token.is_defined_word = false;
	// A keyword is an `identifier_or_keyword` too, so `defined true` tests the
	// spelling `true`.
	token.defined_mock = MockIsDefinedIdentifier(source.data(), source.size());
	tokens_.push_back(token);
}

void CtrlExprSink::EmitIdentifier(const std::string& source)
{
	CtrlToken token;
	token.kind = kCtrlIdentifier;
	token.simple = posttoken::ETOKENTYPE_COUNT;
	token.bits = 0;
	token.is_unsigned = false;
	// `defined` is not a keyword of the course table, so the operator arrives
	// as an identifier and is recognised here, while the spelling is alive.
	token.is_defined_word = source == "defined";
	token.defined_mock = MockIsDefinedIdentifier(source.data(), source.size());
	tokens_.push_back(token);
}

void CtrlExprSink::EmitLiteral(const std::string&, EFundamentalType type,
                               const std::string& bytes)
{
	if (!posttoken::FundamentalTypeIsIntegral(type))
	{
		// A floating-literal, `void` or `nullptr_t` is not an
		// `integral-literal`, and the handout rejects the whole line for it.
		rejected_ = true;
		return;
	}

	// 16.1's course footnote: every `integral-literal` is interpreted as its
	// phase 7 type and then promoted to `intmax_t` or `uintmax_t` according to
	// whether that type is signed.  Both are 64 bits, so the promoted value is
	// the literal's code units, sign-extended from its own width when its type
	// is signed.
	const bool is_unsigned = !posttoken::FundamentalTypeIsSigned(type);
	const std::size_t size = posttoken::FundamentalTypeSize(type);
	unsigned long long value = 0;
	for (std::size_t index = 0; index < bytes.size(); ++index)
	{
		value |= static_cast<unsigned long long>(
			static_cast<unsigned char>(bytes[index])) << (8 * index);
	}
	if (!is_unsigned && size < sizeof(value))
	{
		const unsigned shift = 64 - 8 * static_cast<unsigned>(size);
		value = static_cast<unsigned long long>(static_cast<long long>(value << shift) >> shift);
	}

	CtrlToken token;
	token.kind = kCtrlLiteral;
	token.simple = posttoken::ETOKENTYPE_COUNT;
	token.bits = value;
	token.is_unsigned = is_unsigned;
	token.is_defined_word = false;
	token.defined_mock = false;
	tokens_.push_back(token);
}

void CtrlExprSink::EmitLiteralArray(const std::string&, std::size_t,
                                    EFundamentalType, const std::string&)
{
	// An array of anything is explicitly not an integral type.
	rejected_ = true;
}

void CtrlExprSink::EmitUserDefinedCharacter(const std::string&, const std::string&,
                                            EFundamentalType, const std::string&)
{
	// An `integral-literal` is not user defined.
	rejected_ = true;
}

void CtrlExprSink::EmitUserDefinedStringArray(const std::string&, const std::string&,
                                              std::size_t, EFundamentalType, const std::string&)
{
	rejected_ = true;
}

void CtrlExprSink::EmitUserDefinedInteger(const std::string&, const std::string&,
                                          const std::string&)
{
	rejected_ = true;
}

void CtrlExprSink::EmitUserDefinedFloating(const std::string&, const std::string&,
                                           const std::string&)
{
	rejected_ = true;
}

void CtrlExprSink::EmitEof()
{
	EndOfLine();
	Append("eof");
	EndLine();
}

void CtrlExprSink::EndOfLine()
{
	// A logical line that carried nothing at all - whitespace or a comment -
	// produces no output line.
	if (tokens_.empty() && !rejected_)
		return;

	if (rejected_)
	{
		Append("error");
	}
	else
	{
		CtrlExpression expression(tokens_.data(), tokens_.size());
		expression.Evaluate(buffer_);
	}
	EndLine();

	tokens_.clear();
	rejected_ = false;
}

void CtrlExprSink::EndLine()
{
	buffer_.push_back('\n');
	if (buffer_.size() >= kFlushThreshold)
		Flush();
}

void CtrlExprSink::Flush()
{
	if (buffer_.empty())
		return;
	out_.write(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
	buffer_.clear();
	out_.flush();
}

} // namespace preprocess
} // namespace cppgm
