// The PA2 text view of a post-token stream, in one place.
//
// `posttoken` is the tool that owns this format, and `preproc` prints its
// preprocessed tokens in the same format, so the formatter is shared rather
// than written twice.  It is a view and nothing else: the compiler's phases pass
// `IPostTokenSink`'s typed records, and no phase ever reads this text back.

#pragma once

#include <cstddef>
#include <iostream>
#include <string>

#include "posttoken/fundamental_type.h"
#include "posttoken/post_token_sink.h"
#include "posttoken/simple_token.h"

namespace cppgm
{
namespace posttoken
{

// The output is a pure function of the token facts, and it is the dominant
// cost of a tool that produces it, so lines are composed in one buffer and
// handed to the stream in blocks.  That trades a virtual call per field for a
// copy per line, and keeps the stream's own formatting machinery out of the
// hot path.
class TextPostTokenSink : public IPostTokenSink
{
public:
	explicit TextPostTokenSink(std::ostream& out)
		: out_(out)
		, invalid_(false)
	{}

	void EmitInvalid(const std::string& source) override
	{
		// An invalid preprocessing-token is an error of the phase that produced
		// it, which is why the fact is kept: `preproc` fails the whole
		// translation unit for one, `posttoken` does not.
		invalid_ = true;
		Write("invalid ");
		Write(source);
		EndLine();
	}

	void EmitSimple(const std::string& source, ETokenType type) override
	{
		Write("simple ");
		Write(source);
		Write(" ");
		Write(SimpleTokenName(type));
		EndLine();
	}

	void EmitIdentifier(const std::string& source) override
	{
		Write("identifier ");
		Write(source);
		EndLine();
	}

	void EmitLiteral(const std::string& source, EFundamentalType type,
	                 const std::string& bytes) override
	{
		Write("literal ");
		Write(source);
		WriteType(type);
		WriteHexDump(bytes);
		EndLine();
	}

	void EmitLiteralArray(const std::string& source, std::size_t count,
	                      EFundamentalType type, const std::string& bytes) override
	{
		Write("literal ");
		Write(source);
		WriteArrayType(count, type);
		WriteHexDump(bytes);
		EndLine();
	}

	void EmitUserDefinedCharacter(const std::string& source, const std::string& suffix,
	                              EFundamentalType type, const std::string& bytes) override
	{
		Write("user-defined-literal ");
		Write(source);
		Write(" ");
		Write(suffix);
		Write(" character ");
		Write(FundamentalTypeName(type));
		WriteHexDump(bytes);
		EndLine();
	}

	void EmitUserDefinedStringArray(const std::string& source, const std::string& suffix,
	                                std::size_t count, EFundamentalType type,
	                                const std::string& bytes) override
	{
		Write("user-defined-literal ");
		Write(source);
		Write(" ");
		Write(suffix);
		Write(" string");
		WriteArrayType(count, type);
		WriteHexDump(bytes);
		EndLine();
	}

	void EmitUserDefinedInteger(const std::string& source, const std::string& suffix,
	                            const std::string& prefix) override
	{
		Write("user-defined-literal ");
		Write(source);
		Write(" ");
		Write(suffix);
		Write(" integer ");
		Write(prefix);
		EndLine();
	}

	void EmitUserDefinedFloating(const std::string& source, const std::string& suffix,
	                             const std::string& prefix) override
	{
		Write("user-defined-literal ");
		Write(source);
		Write(" ");
		Write(suffix);
		Write(" floating ");
		Write(prefix);
		EndLine();
	}

	void EmitEof() override
	{
		Write("eof");
		EndLine();
	}

	// True once an invalid record has been reported.
	bool HasInvalid() const { return invalid_; }

	void Flush()
	{
		if (!buffer_.empty())
		{
			out_.write(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
			buffer_.clear();
		}
		out_.flush();
	}

private:
	static const std::size_t kFlushThreshold = 1 << 16;

	void Write(const char* text)
	{
		buffer_.append(text);
		if (buffer_.size() >= kFlushThreshold)
			Flush();
	}

	void Write(const std::string& text)
	{
		buffer_.append(text);
		if (buffer_.size() >= kFlushThreshold)
			Flush();
	}

	void WriteType(EFundamentalType type)
	{
		Write(" ");
		Write(FundamentalTypeName(type));
	}

	void WriteArrayType(std::size_t count, EFundamentalType type)
	{
		Write(" array of ");
		buffer_.append(std::to_string(count));
		Write(" ");
		Write(FundamentalTypeName(type));
	}

	// The ABI image of the literal's code units, one byte per code unit for
	// `char` and two hexadecimal digits per byte.
	void WriteHexDump(const std::string& bytes)
	{
		static const char kDigits[] = "0123456789ABCDEF";
		buffer_.push_back(' ');
		for (std::size_t index = 0; index < bytes.size(); ++index)
		{
			unsigned char value = static_cast<unsigned char>(bytes[index]);
			buffer_.push_back(kDigits[(value >> 4) & 0xF]);
			buffer_.push_back(kDigits[value & 0xF]);
		}
	}

	void EndLine()
	{
		buffer_.push_back('\n');
		if (buffer_.size() >= kFlushThreshold)
			Flush();
	}

	std::ostream& out_;
	std::string buffer_;
	bool invalid_;
};

} // namespace posttoken
} // namespace cppgm