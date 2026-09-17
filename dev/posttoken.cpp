// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

// posttoken: translation phases 1-7's tokenization.
//
// The source file is read from standard input.  Phases 1, 2 and 3 are the
// shared preprocessing frontend (`preprocess/tokens`), phase 4 is a no-op
// because the input carries no directives, and `PostTokenStream` (in
// `dev/src/posttoken`) performs phase 7's tokenization.  The input is rejected
// with EXIT_FAILURE when one of the earlier phases fails, as pptoken does.
//
// This file owns only the PA2 text format: the typed token facts travel
// through `IPostTokenSink`, so no later phase has to produce or parse this
// text.

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

using namespace std;

#include "posttoken/fundamental_type.h"
#include "posttoken/post_token_sink.h"
#include "posttoken/post_token_stream.h"
#include "preprocess/tokens/pp_source_translation.h"
#include "preprocess/tokens/pp_tokenizer.h"

namespace
{

using cppgm::posttoken::EFundamentalType;
using cppgm::posttoken::ETokenType;
using cppgm::posttoken::FundamentalTypeName;
using cppgm::posttoken::IPostTokenSink;
using cppgm::posttoken::SimpleTokenName;

// The output is a pure function of the token facts, and it is the dominant
// cost of the tool, so lines are composed in one buffer and handed to the
// stream in blocks.  That trades a virtual call per field for a copy per
// line, and keeps the stream's own formatting machinery out of the hot path.
class TextPostTokenSink : public IPostTokenSink
{
public:
	void EmitInvalid(const string& source) override
	{
		Write("invalid ");
		Write(source);
		EndLine();
	}

	void EmitSimple(const string& source, ETokenType type) override
	{
		Write("simple ");
		Write(source);
		Write(" ");
		Write(SimpleTokenName(type));
		EndLine();
	}

	void EmitIdentifier(const string& source) override
	{
		Write("identifier ");
		Write(source);
		EndLine();
	}

	void EmitLiteral(const string& source, EFundamentalType type,
	                 const string& bytes) override
	{
		Write("literal ");
		Write(source);
		WriteType(type);
		WriteHexDump(bytes);
		EndLine();
	}

	void EmitLiteralArray(const string& source, size_t count, EFundamentalType type,
	                      const string& bytes) override
	{
		Write("literal ");
		Write(source);
		WriteArrayType(count, type);
		WriteHexDump(bytes);
		EndLine();
	}

	void EmitUserDefinedCharacter(const string& source, const string& suffix,
	                              EFundamentalType type, const string& bytes) override
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

	void EmitUserDefinedStringArray(const string& source, const string& suffix, size_t count,
	                                EFundamentalType type, const string& bytes) override
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

	void EmitUserDefinedInteger(const string& source, const string& suffix,
	                            const string& prefix) override
	{
		Write("user-defined-literal ");
		Write(source);
		Write(" ");
		Write(suffix);
		Write(" integer ");
		Write(prefix);
		EndLine();
	}

	void EmitUserDefinedFloating(const string& source, const string& suffix,
	                             const string& prefix) override
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

	void Flush()
	{
		if (!buffer_.empty())
		{
			cout.write(buffer_.data(), static_cast<streamsize>(buffer_.size()));
			buffer_.clear();
		}
		cout.flush();
	}

private:
	static const size_t kFlushThreshold = 1 << 16;

	void Write(const char* text)
	{
		buffer_.append(text);
		if (buffer_.size() >= kFlushThreshold)
			Flush();
	}

	void Write(const string& text)
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

	void WriteArrayType(size_t count, EFundamentalType type)
	{
		Write(" array of ");
		buffer_.append(to_string(count));
		Write(" ");
		Write(FundamentalTypeName(type));
	}

	// The ABI image of the literal's code units, one byte per code unit for
	// `char` and two hexadecimal digits per byte.
	void WriteHexDump(const string& bytes)
	{
		static const char kDigits[] = "0123456789ABCDEF";
		buffer_.push_back(' ');
		for (size_t index = 0; index < bytes.size(); ++index)
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

	string buffer_;
};

// Reads standard input into one buffer.  The read is chunked so the stream
// buffer moves whole blocks instead of one code unit per virtual call, and the
// buffer grows geometrically so no intermediate copy of the source survives.
//
// Reserving the length a regular file reports was measured and rejected: the
// buffered read still asks for a whole chunk past the end, and the string's
// growth policy then doubles away from the reservation rather than from the
// source size, which raised peak RSS on a 12.5 MB source from 20.1 MB to
// 27.9 MB against the reference's 18.9 MB.  Geometric growth from empty is the
// cheaper policy here.
string ReadStandardInput()
{
	const size_t Chunk = 1 << 16;
	string input;
	streambuf* source = cin.rdbuf();
	for (;;)
	{
		size_t filled = input.size();
		input.resize(filled + Chunk);
		streamsize got = source->sgetn(&input[filled], static_cast<streamsize>(Chunk));
		input.resize(filled + static_cast<size_t>(got));
		if (got != static_cast<streamsize>(Chunk))
			break;
	}
	return input;
}

} // namespace

int main(int argc, char** argv)
{
	// The tool takes the source on standard input and writes the token stream
	// to standard output; it has no options of its own.  `--batch-stdin` is
	// the harness worker flag and is inert here, exactly as it is for
	// `pptoken`: the test runner intercepts it before this function runs.
	(void)argc;
	(void)argv;
	try
	{
		ios_base::sync_with_stdio(false);

		string input = ReadStandardInput();

		TextPostTokenSink output;
		cppgm::preprocess::TranslatedSource source(std::move(input));
		cppgm::posttoken::PostTokenStream tokens(output);
		cppgm::preprocess::PPTokenizer tokenizer(source, tokens);
		tokenizer.Tokenize();
		output.Flush();

		return EXIT_SUCCESS;
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
