// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

// posttoken: translation phases 1-7's tokenization.
//
// The source file is read from standard input.  Phases 1, 2 and 3 are the
// shared preprocessing frontend (`preprocess/tokens`), phase 4 is a no-op
// because the input carries no directives, and `PostTokenStream` (in
// `dev/src/posttoken`) performs phase 7's tokenization.  The input is rejected
// with EXIT_FAILURE when one of the earlier phases fails, as pptoken does.
//
// The PA2 text format itself lives in `posttoken/post_token_text.h`, because
// `preproc` prints its preprocessed tokens in it too; this file only wires the
// tokenizer to it and to standard output.

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

using namespace std;

#include "posttoken/post_token_stream.h"
#include "posttoken/post_token_text.h"
#include "preprocess/tokens/pp_source_translation.h"
#include "preprocess/tokens/pp_tokenizer.h"

namespace
{

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

		cppgm::posttoken::TextPostTokenSink output(cout);
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
