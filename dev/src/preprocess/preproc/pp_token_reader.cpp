#include "preprocess/preproc/pp_token_reader.h"

#include <utility>

#include "preprocess/tokens/pp_source_translation.h"
#include "preprocess/tokens/pp_tokenizer.h"

namespace cppgm
{
namespace preprocess
{

namespace
{

// The collector `RetokenizeSpelling` reads its one token through.  A pasted
// spelling that retokenizes into more than one preprocessing-token is already
// not a paste, so the second record is enough to answer and anything past it is
// dropped rather than grown.
class SpellingCollector : public IPPStreamTokenSink
{
public:
	void OnPreprocessingToken(PPToken token) override
	{
		if (tokens_.size() > 1)
			return;
		if (token.kind == kPPWhitespace || token.kind == kPPNewLine ||
		    token.kind == kPPEof)
		{
			return;
		}
		tokens_.push_back(std::move(token));
	}

	std::vector<PPToken>& Tokens() { return tokens_; }

private:
	std::vector<PPToken> tokens_;
};

} // namespace

std::vector<PPToken> RetokenizeSpelling(const std::string& spelling, std::uint32_t file,
                                        std::uint32_t line)
{
	TranslatedSource source(spelling);
	SpellingCollector collector;
	PPTokenReader reader(file, collector);
	PPTokenizer tokenizer(source, reader);
	tokenizer.Tokenize();

	// The collector stops at the second record, so a spelling that is not one
	// preprocessing-token is reported as two and the caller rejects it.
	std::vector<PPToken> tokens = std::move(collector.Tokens());
	if (!tokens.empty())
		tokens[0].line = line;
	return tokens;
}

} // namespace preprocess
} // namespace cppgm
