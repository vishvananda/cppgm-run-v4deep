// The starter kit's floating-literal scan.
//
// PA2 output is compared bit for bit, so the handout fixes the scan for
// floating-literals: `PA2Decode_float`, `PA2Decode_double` and
// `PA2Decode_long_double` below are the starter kit's functions, kept with
// their names and signatures.  Using them means an out-of-range literal
// yields whatever the standard library's `num_get` produces - the C++11
// result is a saturated infinity or a zero - which is why the handout makes
// the range check optional and does not test it for PA2.

#pragma once

#include <string>

namespace cppgm
{
namespace posttoken
{

float PA2Decode_float(const std::string& s);
double PA2Decode_double(const std::string& s);
long double PA2Decode_long_double(const std::string& s);

} // namespace posttoken
} // namespace cppgm