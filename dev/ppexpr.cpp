// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

// ppexpr: controlling expressions for conditional inclusion, one per logical
// line.
//
// The source file is read from standard input.  Phases 1, 2 and 3 are the
// shared preprocessing frontend (`preprocess/tokens`); `CtrlExprStream` splits
// its token stream at phase 3's `new-line`, the inherited PA2 post-token pass
// classifies each line's tokens, and `CtrlExpression` parses and evaluates the
// result.  The input is rejected with EXIT_FAILURE when one of the earlier
// phases fails, as pptoken does.
//
// This file owns only the pipeline: the result format lives with the sink that
// produces it, and no later phase has to re-read the text.

#include <iostream>
#include <exception>
#include <string>
#include <utility>

using namespace std;

#include "preprocess/ctrl_expr/ctrl_expr_stream.h"
#include "preprocess/tokens/pp_source_translation.h"
#include "preprocess/tokens/pp_tokenizer.h"

namespace
{

using cppgm::preprocess::CtrlExprSink;
using cppgm::preprocess::CtrlExprStream;
using cppgm::preprocess::PPTokenizer;
using cppgm::preprocess::TranslatedSource;

// Reads standard input into one buffer.  The read is chunked so the stream
// buffer moves whole blocks instead of one code unit per virtual call, and the
// buffer grows geometrically so no intermediate copy of the source survives.
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
	// The tool's source is standard input and its result is standard output; it
	// has no options of its own.  `--batch-stdin` is the harness's worker flag
	// and is inert here, exactly as it is for `pptoken` and `posttoken`: the
	// test runner intercepts it before this function runs.
	(void)argc;
	(void)argv;
	try
	{
		// The result is this tool's whole output and its dominant cost, so the
		// C++ streams keep their own buffers instead of forwarding every
		// insertion to stdio.
		ios_base::sync_with_stdio(false);

		string input = ReadStandardInput();

		CtrlExprSink output(cout);
		CtrlExprStream lines(output);
		TranslatedSource source(std::move(input));
		PPTokenizer tokenizer(source, lines);
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
