// One preprocessing-token of a translation unit, with the two things the
// preprocessor needs besides the spelling.
//
// The spelling is owned: a directive's tokens have to outlive the tokenizer
// callback that reported them, and macro replacement produces spellings that
// exist nowhere in the source.  The location is a file-name index plus a
// physical line - the two facts `__FILE__`, `__LINE__` and `#line` need - and
// it travels with the token so a replacement token can inherit the location of
// the invocation that produced it.
//
// The unavailable-macro-name set is the blue paint of `macros.md`: the macro
// names a token may no longer expand.  It is a persistent list shared between
// the tokens of one replacement, so a token that was never painted costs a
// null pointer, painting a replacement costs one small node, and a chain of
// macro invocations each longer than the last - the ordinary shape of a
// long helper chain - does not copy the whole set per expansion.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cppgm
{
namespace preprocess
{

enum EPPTokenKind : std::uint8_t
{
	kPPWhitespace,
	kPPNewLine,
	kPPHeaderName,
	kPPIdentifier,
	kPPNumber,
	kPPCharacterLiteral,
	kPPUserDefinedCharacterLiteral,
	kPPStringLiteral,
	kPPUserDefinedStringLiteral,
	kPPOpOrPunc,
	kPPNonWhitespaceChar,
	kPPEof,
	// The `##` operator's empty operand: a token that produces nothing unless
	// it is pasted with a token that does.
	kPPPlacemarker
};

// One macro name a token may no longer expand, and the names behind it.  The
// list is shared and never mutated, so `Add` is one allocation and a token
// that was never painted carries a null pointer.
struct PPMacroPaintNode
{
	std::shared_ptr<const PPMacroPaintNode> parent;
	std::uint32_t id;
	std::uint32_t size;
};

typedef std::shared_ptr<const PPMacroPaintNode> PPMacroPaint;

struct PPToken
{
	std::string spelling;
	PPMacroPaint paint;
	std::uint32_t file;
	std::uint32_t line;
	EPPTokenKind kind;
	// True when the token is the value of a macro parameter: it was substituted
	// for a parameter reference, or pasted from tokens that were.  `macros.md`
	// says the nestedness relationship does not survive parameter substitution,
	// and this is the bit that says so: a substituted token that becomes an
	// invocation head starts a fresh nesting chain for its own replacement while
	// its own paint still bars the names it already carries.
	bool substituted;
};

// True when `token` may no longer expand the macro whose identity is `id`.
// Names are added newest-first, and the name a token is checked against is
// usually the one most recently added, so the walk is short.
inline bool PPTokenIsPainted(const PPToken& token, std::uint32_t id)
{
	for (const PPMacroPaintNode* node = token.paint.get(); node != nullptr;
	     node = node->parent.get())
	{
		if (node->id == id)
			return true;
	}
	return false;
}

// The paint a token carries once the macro `id` is being expanded.  Adding a
// name the token already carries returns the same list.
PPMacroPaint PPMacroPaintAdd(const PPMacroPaint& paint, std::uint32_t id);

// The union of two paints, used when a substituted argument joins the paint of
// the invocation it is substituted into.  The shorter list is the one walked,
// so the wide paint of a long expansion chain is not the one scanned.
PPMacroPaint PPMacroPaintUnion(const PPMacroPaint& left, const PPMacroPaint& right);

// True for the preprocessing-op-or-punc spelled `spelling`.
inline bool PPTokenIsPunctuator(const PPToken& token, const char* spelling)
{
	return token.kind == kPPOpOrPunc && token.spelling == spelling;
}

// The `#` of a directive and the `#` of a replacement list are both spelled
// `#` or, as 2.5's digraph, `%:`.  The `##` operator has the same pair.
inline bool PPTokenIsHashOperator(const PPToken& token)
{
	return PPTokenIsPunctuator(token, "#") || PPTokenIsPunctuator(token, "%:");
}

inline bool PPTokenIsPasteOperator(const PPToken& token)
{
	return PPTokenIsPunctuator(token, "##") || PPTokenIsPunctuator(token, "%:%:");
}

// True for a token the tokenizer reports as a preprocessing-op-or-punc that
// spells a word: the alternative tokens `and`, `or_eq`, `bitand`, `new` and
// their neighbours, which 2.6 lets stand where an identifier stands.  An
// operator is never spelled with a leading identifier character, so the
// spelling decides it.
inline bool PPTokenIsWordOperator(const PPToken& token)
{
	if (token.kind != kPPOpOrPunc || token.spelling.empty())
		return false;
	const char first = token.spelling[0];
	return first == '_' || (first >= 'a' && first <= 'z') ||
	       (first >= 'A' && first <= 'Z');
}

// True when the token can be the operand of `defined` or the name a
// `#ifdef` tests: an identifier, or a keyword spelled as an operator word.
inline bool PPTokenIsIdentifierLike(const PPToken& token)
{
	return token.kind == kPPIdentifier || PPTokenIsWordOperator(token);
}

} // namespace preprocess
} // namespace cppgm