#include "posttoken/fundamental_type.h"

namespace cppgm
{
namespace posttoken
{

namespace
{

const char* const kNames[FT_COUNT] =
{
	"signed char",
	"short int",
	"int",
	"long int",
	"long long int",
	"unsigned char",
	"unsigned short int",
	"unsigned int",
	"unsigned long int",
	"unsigned long long int",
	"wchar_t",
	"char",
	"char16_t",
	"char32_t",
	"bool",
	"float",
	"double",
	"long double",
	"void",
	"nullptr_t"
};

// System V AMD64 ABI, Table 3.1 Scalar Types.  `void` and `nullptr_t` are not
// scalars in that table; they never reach a literal hexdump, so they are given
// no size rather than a guessed one.
const std::size_t kSizes[FT_COUNT] =
{
	1, // signed char
	2, // short int
	4, // int
	8, // long int
	8, // long long int
	1, // unsigned char
	2, // unsigned short int
	4, // unsigned int
	8, // unsigned long int
	8, // unsigned long long int
	4, // wchar_t
	1, // char
	2, // char16_t
	4, // char32_t
	1, // bool
	4, // float
	8, // double
	16, // long double (x87 80-bit extended, padded to 16 bytes)
	0, // void
	0  // nullptr_t
};

} // namespace

const char* FundamentalTypeName(EFundamentalType type)
{
	return type < FT_COUNT ? kNames[type] : "";
}

std::size_t FundamentalTypeSize(EFundamentalType type)
{
	return type < FT_COUNT ? kSizes[type] : 0;
}

} // namespace posttoken
} // namespace cppgm