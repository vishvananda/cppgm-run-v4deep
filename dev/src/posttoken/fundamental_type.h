// The twenty fundamental types of 3.9.1 and the course ABI's layout for them.
//
// `char`, `signed char` and `unsigned char` share a representation, and so do
// `wchar_t` and `char32_t`, but the toolchain must treat each as a distinct
// type, so the enum identifies the type rather than its layout.  Later phases
// key type identity on this enumerator; the size comes from one ABI table so
// the two facts cannot drift apart.

#pragma once

#include <cstddef>

namespace cppgm
{
namespace posttoken
{

// 3.9.1: Fundamental Types
enum EFundamentalType
{
	// 3.9.1.2
	FT_SIGNED_CHAR,
	FT_SHORT_INT,
	FT_INT,
	FT_LONG_INT,
	FT_LONG_LONG_INT,

	// 3.9.1.3
	FT_UNSIGNED_CHAR,
	FT_UNSIGNED_SHORT_INT,
	FT_UNSIGNED_INT,
	FT_UNSIGNED_LONG_INT,
	FT_UNSIGNED_LONG_LONG_INT,

	// 3.9.1.1 / 3.9.1.5
	FT_WCHAR_T,
	FT_CHAR,
	FT_CHAR16_T,
	FT_CHAR32_T,

	// 3.9.1.6
	FT_BOOL,

	// 3.9.1.8
	FT_FLOAT,
	FT_DOUBLE,
	FT_LONG_DOUBLE,

	// 3.9.1.9
	FT_VOID,

	// 3.9.1.10
	FT_NULLPTR_T,

	FT_COUNT
};

// The source spelling of a fundamental type, as PA2 prints it.
const char* FundamentalTypeName(EFundamentalType type);

// Size in bytes from the System V AMD64 ABI scalar-type table.
std::size_t FundamentalTypeSize(EFundamentalType type);

// The course definition of which of them are signed, from the PA3 handout
// ("It is course-defined (and by the ABI and bootstrap) that following integral
// types are signed"): `bool`, `wchar_t`, `char`, `signed char` and the four
// signed integer types are signed; the unsigned integer types, `char16_t` and
// `char32_t` are unsigned.  This is `std::numeric_limits<T>::is_signed` for the
// course ABI, kept in the one table that already owns the types' layout so the
// two facts cannot drift apart.  Non-integral types report false.
bool FundamentalTypeIsSigned(EFundamentalType type);

// True for the integral types of 3.9.1 - everything but `float`, `double`,
// `long double`, `void` and `nullptr_t`.  PA3's `integral-literal` is a literal
// of one of these.
bool FundamentalTypeIsIntegral(EFundamentalType type);

} // namespace posttoken
} // namespace cppgm