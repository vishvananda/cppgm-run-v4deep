// Translation phases 1 and 2 for preprocessing-token recognition.
//
// Phase 1 maps the physical source characters to the course source character
// set (UTF-8) and replaces trigraphs and universal-character-names.  Phase 2
// splices physical lines.  The result feeds preprocessing-token recognition in
// phase 3, which lives in pp_tokenizer.h.

#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace preprocess
{

// Raised for source text that the phases of translation reject.
class SourceError : public std::runtime_error
{
public:
	explicit SourceError(const std::string& message)
		: std::runtime_error(message)
	{}
};

// One code point of the phase 1 and 2 stream.  `physical` indexes the decoded
// source so that a raw string literal, whose grammar suppresses those rewrites
// between its quotes, can read the spelling that the phases would otherwise
// have replaced.  Line and column are the physical position the tokenizer
// reports for the token that starts here.
struct TranslatedCodePoint
{
	std::uint32_t physical;
	std::uint32_t line;
	std::uint32_t column;
	int code_point;
};

// A source buffer decoded to code points together with the phase 1 and 2 stream
// built from it.  Both views are immutable once TranslateSource returns.
struct TranslatedSource
{
	std::vector<int> physical;
	std::vector<TranslatedCodePoint> translated;
};

// Decodes `bytes` as UTF-8, removes a leading byte order mark, ensures the
// buffer ends in a line feed, and then applies trigraph replacement,
// universal-character-name replacement and line splicing.
TranslatedSource TranslateSource(const std::string& bytes);

// Appends the UTF-8 encoding of one code point.
void AppendCodePointUtf8(int code_point, std::string& out);

// Encodes `codes[begin, end)` as UTF-8.
std::string EncodeUtf8(const std::vector<int>& codes, std::size_t begin, std::size_t end);

} // namespace preprocess
} // namespace cppgm
