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
#include <deque>
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
// list is shared and never mutated, so a token that was never painted carries a
// null pointer and painting a replacement costs one small node.
//
// `low` and `high` are the extremes of the names in the whole tail.  A name
// outside them cannot be in the list at all, which answers the common case - a
// long chain of helper macros, each with an identity outside its ancestors'
// range - without walking it.
struct PPMacroPaintNode
{
	const PPMacroPaintNode* parent;
	std::uint32_t id;
	std::uint32_t size;
	std::uint32_t low;
	std::uint32_t high;
};

// A node is owned by the translation unit's arena, not by the token that names
// it.  The list is immutable and shared, so no node ever has one owner, and
// every token that can name one is finished with by the time the arena is
// released - a shared ownership count would be paid on the hot path for a
// lifetime that is already known.
typedef const PPMacroPaintNode* PPMacroPaint;

// The paint nodes of one translation unit, released in bulk.
//
// A node is read through exactly two kinds of token: one on a live rescan
// stack, and one in the argument list of the invocation currently being
// substituted - those are the only places a paint is handed back to `Add`,
// `Union` or `PPTokenIsPainted`.  A node is therefore dead as soon as no such
// token names it, and `Clear` is called at those points and at no others.
// Nothing else may call it: a token kept in an ordinary local beyond its
// construct still holds a pointer, and although nothing dereferences it again,
// the release point is defined by what is read and not by what is destroyed.
class PPPaintArena
{
public:
	// The paint a token carries once the macro `id` is being expanded.  Adding
	// a name the token already carries returns the same list.
	PPMacroPaint Add(PPMacroPaint paint, std::uint32_t id);

	// The union of two paints, used when a substituted argument joins the paint
	// of the invocation it is substituted into.  The shorter list is the one
	// walked, so the wide paint of a long expansion chain is not the one
	// scanned.
	PPMacroPaint Union(PPMacroPaint left, PPMacroPaint right);

	// Releases every node at once.  Cheap when nothing has been painted since
	// the last call, which is the common case: a token sequence reaches the
	// quiescent point far more often than it invokes a macro.
	void Clear()
	{
		if (!nodes_.empty())
			nodes_.clear();
	}

private:
	// A deque: nodes are handed out by address and the arena grows while
	// earlier ones are still in use, so an existing node must not move.
	std::deque<PPMacroPaintNode> nodes_;
};

struct PPToken
{
	std::string spelling;
	PPMacroPaint paint = nullptr;
	std::uint32_t file = 0;
	std::uint32_t line = 0;
	EPPTokenKind kind = kPPIdentifier;
	// True when the token is the value of a macro parameter: it was substituted
	// for a parameter reference, or pasted from tokens that were.  `macros.md`
	// says the nestedness relationship does not survive parameter substitution,
	// and this is the bit that says so: a substituted token that becomes an
	// invocation head starts a fresh nesting chain for its own replacement while
	// its own paint still bars the names it already carries.
	bool substituted = false;
};

// True when `token` may no longer expand the macro whose identity is `id`.
// Names are added newest-first, and the name a token is checked against is
// usually the one most recently added, so the walk is short.
inline bool PPTokenIsPainted(const PPToken& token, std::uint32_t id)
{
	const PPMacroPaintNode* node = token.paint;
	if (node == nullptr || id < node->low || id > node->high)
		return false;
	for (; node != nullptr; node = node->parent)
	{
		if (node->id == id)
			return true;
	}
	return false;
}

// Where a text-sequence's finalized tokens go.  The expander reports each
// token as soon as nothing in the sequence can change it, so a translation
// unit is never held as one owning token vector.
class IPPTextSink
{
public:
	virtual void EmitToken(const PPToken& token) = 0;

	virtual ~IPPTextSink() {}
};

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