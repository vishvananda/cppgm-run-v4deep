#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace std;

#include "preprocess/tokens/DebugPPTokenStream.h"
#include "preprocess/tokens/IPPTokenStream.h"
#include "preprocess/tokens/pp_source_translation.h"
#include "preprocess/tokens/pp_tokenizer.h"
#include "support/not_implemented.h"

// pptoken runs translation phases 1, 2 and 3 over one C++ source file and
// reports the resulting preprocessing-token sequence.  The phases live in
// preprocess/tokens so that the later staged tools share them.

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

bool HasBatchStdinArg(int argc, char** argv)
{
	for (int i = 1; i < argc; i++)
	{
		if (string(argv[i]) == "--batch-stdin")
			return true;
	}
	return false;
}

int RunNotImplementedBatchMode()
{
	string line;
	while (getline(cin, line))
	{
		(void)line;
		cout << "EXIT_NOT_IMPLEMENTED" << endl;
	}
	return EXIT_SUCCESS;
}

int main(int argc, char** argv)
{
	try
	{
		if (HasBatchStdinArg(argc, argv))
			return RunNotImplementedBatchMode();

		string input = ReadStandardInput();

		DebugPPTokenStream output;
		cppgm::preprocess::TranslatedSource source(std::move(input));
		cppgm::preprocess::PPTokenizer tokenizer(source, output);
		tokenizer.Tokenize();

		return EXIT_SUCCESS;
	}
	catch (const NotImplementedException& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return CPPGM_EXIT_NOT_IMPLEMENTED;
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
