// Post-token analysis of a `pp-number` (2.14.2, 2.14.4, 2.14.8).
//
// A preprocessing-number is the widest token the phase 3 grammar has: it is
// any run of digits, identifier characters, `.` and exponent signs.  This
// stage decides which of the four numeric literal productions it is, or that
// it is none of them.  The analysis is a longest-match on one prefix of the
// spelling, with the ud-suffix read last, because a ud-suffix is the only
// alternative that can consume arbitrary trailing identifier characters.

#pragma once

#include <cstddef>
#include <string>

#include "posttoken/fundamental_type.h"

namespace cppgm
{
namespace posttoken
{

enum ENumericLiteralKind
{
	NUM_INVALID,
	NUM_INTEGER,
	NUM_FLOATING,
	NUM_UD_INTEGER,
	NUM_UD_FLOATING
};

struct NumericLiteral
{
	ENumericLiteralKind kind = NUM_INVALID;

	// NUM_INTEGER / NUM_UD_INTEGER: the value and the type 2.14.2 selects.
	unsigned long long integer_value = 0;
	EFundamentalType integer_type = FT_INT;

	// NUM_UD_*: the ud-suffix and the text before it.
	std::string ud_suffix;
	std::size_t prefix_length = 0;
};

// Classifies one pp-number spelling.  `literal.kind` is NUM_INVALID when the
// spelling matches none of the four productions, when its value does not fit
// the maximal type its `integer-suffix` allows, or when its ud-suffix does not
// begin with an underscore.
void ClassifyPPNumber(const std::string& spelling, NumericLiteral& literal);

// Scans a floating-literal whose `floating-suffix` and ud-suffix have already
// been removed, and writes the ABI image of the result into `bytes`.  The scan
// itself is the starter kit's PA2Decode_float/double/long_double, which the
// handout mandates for PA2 output; see posttoken/pa2_decode.h.
void ScanFloatingLiteral(EFundamentalType type, const std::string& text, std::string& bytes);

} // namespace posttoken
} // namespace cppgm