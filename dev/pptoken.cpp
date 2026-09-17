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

		ostringstream buffer;
		buffer << cin.rdbuf();
		string input = buffer.str();

		DebugPPTokenStream output;
		cppgm::preprocess::TranslatedSource source = cppgm::preprocess::TranslateSource(input);
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
