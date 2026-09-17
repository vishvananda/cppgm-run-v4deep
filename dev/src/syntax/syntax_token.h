// One token of a translation unit, as the parser consumes it.
//
// Phase 7 reports most tokens through `posttoken::ETokenType`, which already
// names every keyword, operator and punctuator of the course table.  The three
// forms that have no `simple` spelling - an identifier, a literal and the end
// of a translation unit - get pseudo-kinds of their own, so a token is always
// one small integer plus its spelling.
//
// The spelling is the post-token spelling: the source text of an identifier,
// and the composed spelling of a literal, which is what the AST dump prints.

#pragma once

#include <cstddef>
#include <string>

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

struct SyntaxToken
{
	int kind;
	std::string spelling;

	// Literal facts.  `fundamental_type` is set for a literal with a scalar
	// type, and `chars` holds its bytes; `bytes` counts them, and a count
	// greater than one marks an array form (string literals).
	posttoken::EFundamentalType fundamental_type;
	std::size_t count;
	std::string chars;

	SyntaxToken()
		: kind(0)
		, fundamental_type(posttoken::FT_VOID)
		, count(0)
	{}
};

}  // namespace syntax
}  // namespace cppgm
