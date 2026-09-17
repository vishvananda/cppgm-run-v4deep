#include "posttoken/pa2_decode.h"

#include <sstream>

namespace cppgm
{
namespace posttoken
{

// use these 3 functions to scan `floating-literals` (see PA2)
// for example PA2Decode_float("12.34") returns "12.34" as a `float` type
float PA2Decode_float(const std::string& s)
{
	std::istringstream iss(s);
	float x;
	iss >> x;
	return x;
}

double PA2Decode_double(const std::string& s)
{
	std::istringstream iss(s);
	double x;
	iss >> x;
	return x;
}

long double PA2Decode_long_double(const std::string& s)
{
	std::istringstream iss(s);
	long double x;
	iss >> x;
	return x;
}

} // namespace posttoken
} // namespace cppgm