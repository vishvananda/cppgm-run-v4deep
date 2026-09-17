#include "preprocess/preproc/pp_token_reader.h"

#include <utility>

#include "preprocess/tokens/pp_source_translation.h"
#include "preprocess/tokens/pp_tokenizer.h"

namespace cppgm
{
namespace preprocess
{

std::vector<PPToken> ReadPreprocessingTokens(std::string bytes, std::uint32_t file)
{
	TranslatedSource source(std::move(bytes));
	PPTokenReader reader(file);
	PPTokenizer tokenizer(source, reader);
	tokenizer.Tokenize();
	return std::move(reader.Tokens());
}

std::vector<PPToken> RetokenizeSpelling(const std::string& spelling, std::uint32_t file,
                                        std::uint32_t line)
{
	TranslatedSource source(spelling);
	PPTokenReader reader(file);
	PPTokenizer tokenizer(source, reader);
	tokenizer.Tokenize();

	std::vector<PPToken> tokens;
	const std::vector<PPToken>& read = reader.Tokens();
	for (std::size_t index = 0; index < read.size(); ++index)
	{
		const PPToken& token = read[index];
		if (token.kind == kPPWhitespace || token.kind == kPPNewLine || token.kind == kPPEof)
			continue;
		tokens.push_back(token);
		// A pasted spelling that retokenizes into more than one
		// preprocessing-token is already not a paste; stop at the second so a
		// pathological spelling cannot grow the vector.
		if (tokens.size() > 1)
			break;
	}
	if (!tokens.empty())
		tokens[0].line = line;
	return tokens;
}

} // namespace preprocess
} // namespace cppgm