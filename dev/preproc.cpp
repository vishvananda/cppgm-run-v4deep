// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

// preproc: translation phases 1-7.
//
// Each command-line source is read relative to the working directory and
// preprocessed as a translation unit of its own; the output is the PA2 text
// view of every resulting token, framed by the number of primary sources and a
// `sof`/`eof` pair per source.
//
// The tool owns only its interface: `Preprocessor` performs phases 4 to 6 and
// reports typed tokens through `IPPTextSink`, and this file turns each record
// into the post-token stream's callback.  Nothing here parses the text view
// back.

#include <cstddef>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

#include "posttoken/post_token_stream.h"
#include "posttoken/post_token_text.h"
#include "preprocess/preproc/pp_preprocessor.h"

namespace
{

using cppgm::preprocess::IPPTextSink;
using cppgm::preprocess::PPToken;

// The preprocessed tokens of one translation unit are handed to the post-token
// pass as they are produced, so no stage holds a whole unit's token sequence
// and the PA2 view is never an intermediate form.
class PostTokenSink : public IPPTextSink
{
public:
	explicit PostTokenSink(cppgm::posttoken::PostTokenStream& post)
		: post_(post)
	{}

	void EmitToken(const PPToken& token) override
	{
		switch (token.kind)
		{
		case cppgm::preprocess::kPPIdentifier:
			post_.emit_identifier(token.spelling);
			return;
		case cppgm::preprocess::kPPNumber:
			post_.emit_pp_number(token.spelling);
			return;
		case cppgm::preprocess::kPPCharacterLiteral:
			post_.emit_character_literal(token.spelling);
			return;
		case cppgm::preprocess::kPPUserDefinedCharacterLiteral:
			post_.emit_user_defined_character_literal(token.spelling);
			return;
		case cppgm::preprocess::kPPStringLiteral:
			post_.emit_string_literal(token.spelling);
			return;
		case cppgm::preprocess::kPPUserDefinedStringLiteral:
			post_.emit_user_defined_string_literal(token.spelling);
			return;
		case cppgm::preprocess::kPPHeaderName:
			post_.emit_header_name(token.spelling);
			return;
		case cppgm::preprocess::kPPOpOrPunc:
			post_.emit_preprocessing_op_or_punc(token.spelling);
			return;
		case cppgm::preprocess::kPPNonWhitespaceChar:
			post_.emit_non_whitespace_char(token.spelling);
			return;
		case cppgm::preprocess::kPPWhitespace:
		case cppgm::preprocess::kPPNewLine:
		case cppgm::preprocess::kPPEof:
		case cppgm::preprocess::kPPPlacemarker:
			return;
		}
	}

private:
	cppgm::posttoken::PostTokenStream& post_;
};

// The build stamp `__DATE__` and `__TIME__` report, taken from `asctime` once
// so every source of the run sees the same one.  asctime prints
// `Www Mmm dd hh:mm:ss yyyy\n`; the date keeps the day's own two-column field,
// which is what the C library's `__DATE__` does for a single-digit day.
void BuildDateAndTime(string& date, string& time)
{
	const time_t now = ::time(nullptr);
	const char* stamp = asctime(localtime(&now));
	if (stamp == nullptr || strlen(stamp) < 24)
		throw runtime_error("cannot read the build time");
	date.assign(stamp + 4, 3);
	date.append(stamp + 7, 3);
	date.append(stamp + 19, 5);
	time.assign(stamp + 11, 8);
}

} // namespace

int main(int argc, char** argv)
{
	try
	{
		vector<string> args;
		for (int index = 1; index < argc; ++index)
			args.push_back(argv[index]);

		if (args.size() < 3 || args[0] != "-o")
			throw logic_error("invalid usage");

		const string outfile = args[1];
		const size_t nsrcfiles = args.size() - 2;

		string build_date;
		string build_time;
		BuildDateAndTime(build_date, build_time);

		ofstream out(outfile.c_str());
		if (!out)
			throw runtime_error("cannot open the output file");

		cppgm::posttoken::TextPostTokenSink text(out);
		cppgm::posttoken::PostTokenStream tokens(text);
		PostTokenSink sink(tokens);
		cppgm::preprocess::Preprocessor preprocessor(sink, build_date, build_time);

		out << "preproc " << nsrcfiles << endl;

		for (size_t index = 0; index < nsrcfiles; ++index)
		{
			const string& srcfile = args[index + 2];
			text.Flush();
			out << "sof " << srcfile << endl;

			preprocessor.ProcessPrimarySource(srcfile);

			tokens.emit_eof();
			text.Flush();
		}
		out.flush();

		// A preprocessing-token that phase 7 cannot classify fails the run even
		// though macro replacement succeeded.
		return text.HasInvalid() ? EXIT_FAILURE : EXIT_SUCCESS;
	}
	catch (exception& error)
	{
		cerr << "ERROR: " << error.what() << endl;
		return EXIT_FAILURE;
	}
}