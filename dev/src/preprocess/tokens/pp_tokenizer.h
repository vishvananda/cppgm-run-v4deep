// Preprocessing-token recognition (translation phase 3).

#pragma once

#include <cstddef>
#include <string>

#include "preprocess/tokens/IPPTokenStream.h"
#include "preprocess/tokens/pp_source_translation.h"

namespace cppgm
{
namespace preprocess
{

// Recognises the preprocessing-tokens of one translated source and reports them
// to an IPPTokenStream.  The recogniser is a single greedy pass: each code point
// is inspected once, and the only storage it keeps is the spelling of the token
// it is about to report plus the cursor's bounded lookahead window.
//
// Header-names are context sensitive ([lex.header]).  The recogniser tracks
// whether the most recent significant tokens were (start of line or new-line)
// then `#` then an include directive word (`include`, or `include_next`, the GNU
// extension the library headers are written in terms of), ignoring whitespace
// sequences and comments.
//
// A literal-operator-id is `operator "" identifier` ([over.literal]).  Read
// with the longest-match rule, `operator""s` would be two tokens - `operator`
// and the user-defined-string-literal `""s` - and the parser would never see
// the empty string literal the production names.  After an `operator` token the
// recogniser therefore splits `""suffix` into the string-literal and the
// suffix, which is what the reference frontend does and what a translation unit
// such as libstdc++'s <bits/basic_string.h> needs.
class PPTokenizer
{
public:
	PPTokenizer(TranslatedSource& source, IPPTokenStream& output);

	// Recognises every token in the source and finishes with the eof event.
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

	int CodeAt(std::size_t ahead) const;
	std::size_t MatchOperator(std::size_t ahead) const;
	bool MatchesOperator(std::size_t ahead, const char* spelling) const;
	bool HasHexadecimalPrefix() const;

	void BeginToken();
	void Consume(std::size_t count);
	void ReportLocation();
	void NoteEmitted(TokenRole role);

	void EmitOperator(std::size_t length);
	void EmitNonWhitespaceCharacter();
	void ScanWhitespaceSequence();
	bool StartsHeaderName() const;
	void ScanHeaderName();
	void ScanPPNumber();
	void ScanIdentifierLike();
	void ScanIdentifier();
	void ScanCharacterLiteral(std::size_t prefix_length);
	void ScanStringLiteral(std::size_t prefix_length);
	void ScanRawStringLiteral(std::size_t quote_offset);

	void SkipBlockComment();
	void SkipEscapeSequence();
	bool HasHexQuad(std::size_t ahead, std::size_t count) const;

	TranslatedSource& source_;
	IPPTokenStream& output_;
	std::string spelling_;
	std::size_t token_byte_offset_;
	bool line_start_;
	bool after_hash_;
	bool after_include_;
	bool after_operator_;
};

} // namespace preprocess
} // namespace cppgm
