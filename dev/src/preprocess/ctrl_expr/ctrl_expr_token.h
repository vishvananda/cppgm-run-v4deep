// One typed token of a PA3 controlling expression.
//
// Phase 3 hands the logical line's preprocessing-tokens to the inherited PA2
// post-token pass, which reports them as typed facts.  PA3's token buffer keeps
// exactly the facts the grammar and its evaluation need, so a logical line
// costs one small record per token and nothing has to be recovered from a
// spelling later.  The two facts that would otherwise need the spelling -
// whether an identifier *is* the `defined` operator, and the mock `defined`
// result - are decided where the spelling is still at hand, at the reporting
// callback.

#pragma once

#include <cstddef>

#include "posttoken/simple_token.h"

namespace cppgm
{
namespace preprocess
{

enum ECtrlTokenKind
{
	// An integral-literal: `bits` holds its value already promoted to
	// `intmax_t`/`uintmax_t`, and `is_unsigned` says which of the two it is.
	kCtrlLiteral,
	// An identifier in preprocessing-token context.
	kCtrlIdentifier,
	// A punctuator, or a keyword used as an `identifier_or_keyword`.
	kCtrlSimple
};

struct CtrlToken
{
	ECtrlTokenKind kind;

	// `kCtrlSimple`: the punctuator or keyword type.
	posttoken::ETokenType simple;

	// `kCtrlLiteral`: the literal's value, sign-extended to 64 bits when its
	// type is signed.
	unsigned long long bits;

	// `kCtrlLiteral`: the literal's promoted type is unsigned.
	bool is_unsigned;

	// `kCtrlIdentifier`/`kCtrlSimple`: the token is spelled `defined`.
	bool is_defined_word;

	// `kCtrlIdentifier`/`kCtrlSimple`: the handout's mock `IsDefinedIdentifier`
	// result for this token's spelling.
	bool defined_mock;
};

// The PA3 handout's mock for the `defined` operator: as no macros are defined
// in PA3, an identifier is "defined" exactly when the first UTF-8 code unit of
// its spelling is odd.  Parity is preserved when a code unit above 0x7F is read
// as a signed `char`, so the comparison is made on the unsigned value.
inline bool MockIsDefinedIdentifier(const char* data, std::size_t size)
{
	if (size == 0)
		return false;
	return (static_cast<unsigned char>(data[0]) & 1u) != 0;
}

} // namespace preprocess
} // namespace cppgm
