// Post-token analysis of character-literals and string-literals (2.14.3, 2.14.5).
//
// The input is a PA1 spelling: the translated source characters with
// `universal-character-names` already replaced and `escape-sequences` left as
// written.  Everything here works from that spelling, so no phase-1 state has
// to be re-created and no source buffer is retained.
//
// A literal's *body* is the text between the delimiting quotes (between the
// `(` and `)delim"` for a raw string).  A body reads as a sequence of
// elements:
//
//   - a source character is a code point, which the sequence's encoding turns
//     into one or more code units;
//   - a simple escape sequence names a character, so it is a code point too;
//   - an octal or hexadecimal escape sequence is *numeric*: it contributes
//     exactly one code unit whose value is the escape's value, and it is never
//     re-encoded.
//
// That distinction is what makes `"\x3C0"` ill-formed while `"π"` is not.

#pragma once

#include <cstddef>
#include <string>

#include "posttoken/fundamental_type.h"

namespace cppgm
{
namespace posttoken
{

// The four encoding-prefixes of a string-literal, plus the ordinary form.
enum ELiteralPrefix
{
	PREFIX_ORDINARY,
	PREFIX_UTF8,   // u8
	PREFIX_UTF16,  // u
	PREFIX_UTF32,  // U
	PREFIX_WIDE    // L
};

// Reading a body element by element.
class LiteralBodyReader
{
public:
	LiteralBodyReader(const std::string& spelling, std::size_t begin, std::size_t end)
		: spelling_(spelling)
		, position_(begin)
		, end_(end)
	{}

	// Advances to the next element.  Returns false at the end of the body, or
	// when the body is malformed; `WellFormed` distinguishes the two.
	bool Next();

	bool WellFormed() const { return well_formed_; }

	// True when the element is an octal or hexadecimal escape sequence, whose
	// value is a code unit rather than a code point.
	bool IsNumericEscape() const { return numeric_; }

	// The code point of a character element, or the value of a numeric escape.
	unsigned long long Value() const { return value_; }

private:
	bool ReadEscape();

	const std::string& spelling_;
	std::size_t position_;
	std::size_t end_;
	bool well_formed_ = true;
	bool numeric_ = false;
	unsigned long long value_ = 0;
};

// A string-literal spelling taken apart into its prefix, body and ud-suffix.
struct StringLiteralSpelling
{
	ELiteralPrefix prefix = PREFIX_ORDINARY;
	bool raw = false;
	std::size_t body_begin = 0;
	std::size_t body_end = 0;
	std::size_t suffix_begin = 0;  // spelling.size() when there is no suffix
};

// A character-literal spelling taken apart the same way.
struct CharacterLiteralSpelling
{
	ELiteralPrefix prefix = PREFIX_ORDINARY;
	std::size_t body_begin = 0;
	std::size_t body_end = 0;
	std::size_t suffix_begin = 0;
};

// The prefix and body boundaries are read from the spelling, which the
// preprocessing-token recogniser has already validated, so both splits
// succeed for every spelling it can produce.
void SplitStringLiteralSpelling(const std::string& spelling, StringLiteralSpelling& out);
void SplitCharacterLiteralSpelling(const std::string& spelling, CharacterLiteralSpelling& out);

// The code-unit width and element type a string-literal sequence with this
// prefix encodes into.  Under the course ABI the widths are 8, 16, 32 and 32
// bits for `char`, `char16_t`, `char32_t` and `wchar_t`.
unsigned LiteralPrefixWidth(ELiteralPrefix prefix);
EFundamentalType LiteralPrefixElementType(ELiteralPrefix prefix);

// True when the prefix names a character element: a `u8` prefix is a string
// encoding only, and 2.14.3 has no `u8` character-literal.
bool IsCharacterPrefix(ELiteralPrefix prefix);

// Appends the code units of one code point under `prefix`'s encoding, and
// fails when the code point has no representation there.
bool AppendEncodedCodePoint(ELiteralPrefix prefix, unsigned long long code_point, std::string& out);

// Encodes the body of one string-literal of a sequence whose prefix union is
// `prefix`.  Numeric escapes must fit the sequence's code unit; a failure
// makes the whole sequence ill-formed.
bool EncodeStringLiteralBody(const std::string& spelling,
                             const StringLiteralSpelling& split,
                             ELiteralPrefix prefix,
                             std::string& out);

// Decodes a character-literal body to its single code point.  2.14.3 gives a
// character-literal exactly one `c-char`, so an empty or longer body is
// ill-formed.
bool DecodeCharacterLiteralBody(const std::string& spelling,
                                const CharacterLiteralSpelling& split,
                                unsigned long long& code_point);

// True when `code_point` is a code point a character-literal may name: the
// course definition is the valid Unicode range, `[0, 0xD800)` and
// `[0xE000, 0x110000)`.
bool IsValidCharacterCodePoint(unsigned long long code_point);

} // namespace posttoken
} // namespace cppgm