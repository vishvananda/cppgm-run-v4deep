// One token of a translation unit, as the parser consumes it.
//
// Phase 7 reports most tokens through `posttoken::ETokenType`, which already
// names every keyword, operator and punctuator of the course table.  The three
// forms that have no `simple` spelling - an identifier, a literal and the end
// of a translation unit - get pseudo-kinds of their own, so a token is always
// two small integers: what it is, and which spelling in the translation unit's
// pool it was written with.
//
// The spelling is the post-token spelling: the source text of an identifier,
// and the composed spelling of a literal, which is what the AST dump prints.
// It is held by id rather than by value, so a repeated spelling is stored once
// and the parser's token vector is compact; the tokens a translation unit
// keeps are the ones the parse needs to look ahead over, not the text.

#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "posttoken/fundamental_type.h"
#include "posttoken/simple_token.h"

namespace cppgm
{
namespace syntax
{

// Pseudo-kinds, laid out after the simple-token table so a kind is a plain
// integer comparison everywhere.
enum ESyntaxTokenKind
{
	kIdentifierToken = posttoken::ETOKENTYPE_COUNT,  // TT_IDENTIFIER
	kLiteralToken,                                   // TT_LITERAL
	kEofToken,                                       // ST_EOF
	kSyntaxTokenKindCount
};

// The name a token's kind is spelled with in the PA5 dump: `KW_INT`, `OP_PLUS`,
// `TT_IDENTIFIER`, `TT_LITERAL` or `ST_EOF`.
const char* SyntaxTokenKindName(int kind);

// The spellings one translation unit's tokens share.  Interning happens as the
// tokens enter the frontend, so a spelling is compared, hashed and stored once
// per distinct text rather than once per occurrence.
class SyntaxSpellingPool
{
public:
	SyntaxSpellingPool();

	// Interns `text` and returns its id.  Ids are stable for the pool's life,
	// and id 0 is the empty spelling.
	int Intern(const std::string& text);

	const std::string& Text(int id) const
	{
		return texts_[static_cast<std::size_t>(id)];
	}

	// The id of `>`, the one spelling the parser synthesizes: the second half
	// of a `>>` that a close-angle-bracket took the first `>` of.
	int Greater() const
	{
		return greater_;
	}

private:
	std::vector<std::string> texts_;
	std::unordered_map<std::string, int> ids_;
	int greater_;
};

// A literal's decoded facts, which later stages evaluate.  They sit beside the
// token rather than inside it: only a literal token has any, and the parse
// itself reads none of them.
struct SyntaxLiteralFacts
{
	posttoken::EFundamentalType fundamental_type;
	std::size_t count;
	std::string chars;

	SyntaxLiteralFacts()
		: fundamental_type(posttoken::FT_VOID)
		, count(0)
	{}
};

const int kNoLiteralFacts = -1;

struct SyntaxToken
{
	int kind;
	int spelling;
	int literal;

	SyntaxToken()
		: kind(0)
		, spelling(0)
		, literal(kNoLiteralFacts)
	{}
};

}  // namespace syntax
}  // namespace cppgm
