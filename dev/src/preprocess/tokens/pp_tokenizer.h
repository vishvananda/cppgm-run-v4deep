// Preprocessing-token recognition (translation phase 3).

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "preprocess/tokens/IPPTokenStream.h"
#include "preprocess/tokens/pp_source_translation.h"

namespace cppgm
{
namespace preprocess
{

// Recognises the preprocessing-tokens of one translated source buffer and
// reports them to an IPPTokenStream.  The recogniser is a single greedy pass
// over the code point stream: each position is inspected once, and the only
// allocation is the spelling handed to the stream.
//
// Header-names are context sensitive ([lex.header]).  The recogniser tracks
// whether the most recent significant tokens were (start of line or new-line)
// then `#` then `include`, ignoring whitespace sequences and comments.
class PPTokenizer
{
public:
	PPTokenizer(const TranslatedSource& source, IPPTokenStream& output);

	// Recognises every token in the buffer and finishes with the eof event.
	void Tokenize();

private:
	// How the most recently emitted token affects header-name recognition.
	enum TokenRole
	{
		role_whitespace,
		role_new_line,
		role_hash,
		role_include,
		role_other
	};

	int CodeAt(std::size_t index) const;
	int Current() const;
	std::size_t MatchOperator(std::size_t index) const;
	bool MatchesOperator(std::size_t index, const char* spelling) const;

	void ReportLocation(std::size_t index) const;
	std::string TranslatedSpelling(std::size_t begin, std::size_t end) const;
	std::string PhysicalSpelling(std::size_t begin, std::size_t end) const;
	void NoteEmitted(TokenRole role);

	void EmitOperator(std::size_t begin, std::size_t length);
	void EmitNonWhitespaceCharacter(std::size_t begin);
	void ScanWhitespaceSequence();
	void ScanHeaderName();
	void ScanPPNumber();
	void ScanIdentifierLike();
	void ScanIdentifier();
	void ScanCharacterLiteral(std::size_t prefix_length);
	void ScanStringLiteral(std::size_t prefix_length);
	void ScanRawStringLiteral(std::size_t quote_offset);
	bool RawStringTerminatesAt(const std::vector<int>& physical, std::size_t at,
		std::size_t delimiter_begin, std::size_t delimiter_length) const;

	void SkipBlockComment();
	void SkipEscapeSequence();
	bool HasHexQuad(std::size_t begin, std::size_t count) const;
	bool ScanUdSuffix();
	std::size_t AdvancePastPhysical(std::size_t from, std::size_t physical_end) const;

	const TranslatedSource& source_;
	IPPTokenStream& output_;
	std::size_t position_;
	bool line_start_;
	bool after_hash_;
	bool after_include_;
};

} // namespace preprocess
} // namespace cppgm
