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

} // namespace posttoken
} // namespace cppgm