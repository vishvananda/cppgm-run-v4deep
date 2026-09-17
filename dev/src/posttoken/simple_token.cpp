// The course simple-token table, transcribed from the handout's list.
//
// A spelling maps to one ETokenType and a type maps to one termname.  The
// mapping is many-to-one in the spelling direction - `and` and `&&` are both
// OP_LAND, the digraphs share their punctuator's type - so the two directions
// are separate tables.
//
// The lookup index is built once, at static-initialisation time, from the
// spelling table.  Nothing mutates it afterwards.

#include "posttoken/simple_token.h"

#include <cstring>

namespace cppgm
{
namespace posttoken
{

namespace
{

struct SimpleTokenSpelling
{
	const char* spelling;
	ETokenType type;
};

const SimpleTokenSpelling kSimpleTokenSpellings[] =
{
	{"alignas", KW_ALIGNAS},
	{"alignof", KW_ALIGNOF},
	{"asm", KW_ASM},
	{"auto", KW_AUTO},
	{"bool", KW_BOOL},
	{"break", KW_BREAK},
	{"case", KW_CASE},
	{"catch", KW_CATCH},
	{"char", KW_CHAR},
	{"char16_t", KW_CHAR16_T},
	{"char32_t", KW_CHAR32_T},
	{"class", KW_CLASS},
	{"const", KW_CONST},
	{"constexpr", KW_CONSTEXPR},
	{"const_cast", KW_CONST_CAST},
	{"continue", KW_CONTINUE},
	{"decltype", KW_DECLTYPE},
	{"default", KW_DEFAULT},
	{"delete", KW_DELETE},
	{"do", KW_DO},
	{"double", KW_DOUBLE},
	{"dynamic_cast", KW_DYNAMIC_CAST},
	{"else", KW_ELSE},
	{"enum", KW_ENUM},
	{"explicit", KW_EXPLICIT},
	{"export", KW_EXPORT},
	{"extern", KW_EXTERN},
	{"false", KW_FALSE},
	{"float", KW_FLOAT},
	{"for", KW_FOR},
	{"friend", KW_FRIEND},
	{"goto", KW_GOTO},
	{"if", KW_IF},
	{"inline", KW_INLINE},
	{"int", KW_INT},
	{"long", KW_LONG},
	{"mutable", KW_MUTABLE},
	{"namespace", KW_NAMESPACE},
	{"new", KW_NEW},
	{"noexcept", KW_NOEXCEPT},
	{"nullptr", KW_NULLPTR},
	{"operator", KW_OPERATOR},
	{"private", KW_PRIVATE},
	{"protected", KW_PROTECTED},
	{"public", KW_PUBLIC},
	{"register", KW_REGISTER},
	{"reinterpret_cast", KW_REINTERPET_CAST},
	{"return", KW_RETURN},
	{"short", KW_SHORT},
	{"signed", KW_SIGNED},
	{"sizeof", KW_SIZEOF},
	{"static", KW_STATIC},
	{"static_assert", KW_STATIC_ASSERT},
	{"static_cast", KW_STATIC_CAST},
	{"struct", KW_STRUCT},
	{"switch", KW_SWITCH},
	{"template", KW_TEMPLATE},
	{"this", KW_THIS},
	{"thread_local", KW_THREAD_LOCAL},
	{"throw", KW_THROW},
	{"true", KW_TRUE},
	{"try", KW_TRY},
	{"typedef", KW_TYPEDEF},
	{"typeid", KW_TYPEID},
	{"typename", KW_TYPENAME},
	{"union", KW_UNION},
	{"unsigned", KW_UNSIGNED},
	{"using", KW_USING},
	{"virtual", KW_VIRTUAL},
	{"void", KW_VOID},
	{"volatile", KW_VOLATILE},
	{"wchar_t", KW_WCHAR_T},
	{"while", KW_WHILE},
	{"{", OP_LBRACE},
	{"<%", OP_LBRACE},
	{"}", OP_RBRACE},
	{"%>", OP_RBRACE},
	{"[", OP_LSQUARE},
	{"<:", OP_LSQUARE},
	{"]", OP_RSQUARE},
	{":>", OP_RSQUARE},
	{"(", OP_LPAREN},
	{")", OP_RPAREN},
	{"|", OP_BOR},
	{"bitor", OP_BOR},
	{"^", OP_XOR},
	{"xor", OP_XOR},
	{"~", OP_COMPL},
	{"compl", OP_COMPL},
	{"&", OP_AMP},
	{"bitand", OP_AMP},
	{"!", OP_LNOT},
	{"not", OP_LNOT},
	{";", OP_SEMICOLON},
	{":", OP_COLON},
	{"...", OP_DOTS},
	{"?", OP_QMARK},
	{"::", OP_COLON2},
	{".", OP_DOT},
	{".*", OP_DOTSTAR},
	{"+", OP_PLUS},
	{"-", OP_MINUS},
	{"*", OP_STAR},
	{"/", OP_DIV},
	{"%", OP_MOD},
	{"=", OP_ASS},
	{"<", OP_LT},
	{">", OP_GT},
	{"+=", OP_PLUSASS},
	{"-=", OP_MINUSASS},
	{"*=", OP_STARASS},
	{"/=", OP_DIVASS},
	{"%=", OP_MODASS},
	{"^=", OP_XORASS},
	{"xor_eq", OP_XORASS},
	{"&=", OP_BANDASS},
	{"and_eq", OP_BANDASS},
	{"|=", OP_BORASS},
	{"or_eq", OP_BORASS},
	{"<<", OP_LSHIFT},
	{">>", OP_RSHIFT},
	{">>=", OP_RSHIFTASS},
	{"<<=", OP_LSHIFTASS},
	{"==", OP_EQ},
	{"!=", OP_NE},
	{"not_eq", OP_NE},
	{"<=", OP_LE},
	{">=", OP_GE},
	{"&&", OP_LAND},
	{"and", OP_LAND},
	{"||", OP_LOR},
	{"or", OP_LOR},
	{"++", OP_INC},
	{"--", OP_DEC},
	{",", OP_COMMA},
	{"->*", OP_ARROWSTAR},
	{"->", OP_ARROW},
};

const char* const kSimpleTokenNames[ETOKENTYPE_COUNT] =
{
	"KW_ALIGNAS",
	"KW_ALIGNOF",
	"KW_ASM",
	"KW_AUTO",
	"KW_BOOL",
	"KW_BREAK",
	"KW_CASE",
	"KW_CATCH",
	"KW_CHAR",
	"KW_CHAR16_T",
	"KW_CHAR32_T",
	"KW_CLASS",
	"KW_CONST",
	"KW_CONSTEXPR",
	"KW_CONST_CAST",
	"KW_CONTINUE",
	"KW_DECLTYPE",
	"KW_DEFAULT",
	"KW_DELETE",
	"KW_DO",
	"KW_DOUBLE",
	"KW_DYNAMIC_CAST",
	"KW_ELSE",
	"KW_ENUM",
	"KW_EXPLICIT",
	"KW_EXPORT",
	"KW_EXTERN",
	"KW_FALSE",
	"KW_FLOAT",
	"KW_FOR",
	"KW_FRIEND",
	"KW_GOTO",
	"KW_IF",
	"KW_INLINE",
	"KW_INT",
	"KW_LONG",
	"KW_MUTABLE",
	"KW_NAMESPACE",
	"KW_NEW",
	"KW_NOEXCEPT",
	"KW_NULLPTR",
	"KW_OPERATOR",
	"KW_PRIVATE",
	"KW_PROTECTED",
	"KW_PUBLIC",
	"KW_REGISTER",
	"KW_REINTERPET_CAST",
	"KW_RETURN",
	"KW_SHORT",
	"KW_SIGNED",
	"KW_SIZEOF",
	"KW_STATIC",
	"KW_STATIC_ASSERT",
	"KW_STATIC_CAST",
	"KW_STRUCT",
	"KW_SWITCH",
	"KW_TEMPLATE",
	"KW_THIS",
	"KW_THREAD_LOCAL",
	"KW_THROW",
	"KW_TRUE",
	"KW_TRY",
	"KW_TYPEDEF",
	"KW_TYPEID",
	"KW_TYPENAME",
	"KW_UNION",
	"KW_UNSIGNED",
	"KW_USING",
	"KW_VIRTUAL",
	"KW_VOID",
	"KW_VOLATILE",
	"KW_WCHAR_T",
	"KW_WHILE",
	"OP_LBRACE",
	"OP_RBRACE",
	"OP_LSQUARE",
	"OP_RSQUARE",
	"OP_LPAREN",
	"OP_RPAREN",
	"OP_BOR",
	"OP_XOR",
	"OP_COMPL",
	"OP_AMP",
	"OP_LNOT",
	"OP_SEMICOLON",
	"OP_COLON",
	"OP_DOTS",
	"OP_QMARK",
	"OP_COLON2",
	"OP_DOT",
	"OP_DOTSTAR",
	"OP_PLUS",
	"OP_MINUS",
	"OP_STAR",
	"OP_DIV",
	"OP_MOD",
	"OP_ASS",
	"OP_LT",
	"OP_GT",
	"OP_PLUSASS",
	"OP_MINUSASS",
	"OP_STARASS",
	"OP_DIVASS",
	"OP_MODASS",
	"OP_XORASS",
	"OP_BANDASS",
	"OP_BORASS",
	"OP_LSHIFT",
	"OP_RSHIFT",
	"OP_RSHIFTASS",
	"OP_LSHIFTASS",
	"OP_EQ",
	"OP_NE",
	"OP_LE",
	"OP_GE",
	"OP_LAND",
	"OP_LOR",
	"OP_INC",
	"OP_DEC",
	"OP_COMMA",
	"OP_ARROWSTAR",
	"OP_ARROW",
};

// FNV-1a.  The keys are short ASCII spellings from a bounded table, so a
// byte-at-a-time hash distributes well over this many slots.
std::size_t HashSpelling(const char* spelling, std::size_t length)
{
	std::size_t hash = 2166136261u;
	for (std::size_t index = 0; index < length; ++index)
	{
		hash ^= static_cast<unsigned char>(spelling[index]);
		hash *= 16777619u;
	}
	return hash;
}

const std::size_t kSlotCount = 512;

struct Slot
{
	const char* spelling;
	std::size_t length;
	ETokenType type;
	bool used;
};

struct Index
{
	Slot slots[kSlotCount];

	Index()
	{
		for (std::size_t slot = 0; slot < kSlotCount; ++slot)
			slots[slot].used = false;
		for (std::size_t index = 0;
			index < sizeof(kSimpleTokenSpellings) / sizeof(kSimpleTokenSpellings[0]);
			++index)
		{
			const char* spelling = kSimpleTokenSpellings[index].spelling;
			std::size_t length = std::strlen(spelling);
			std::size_t slot = HashSpelling(spelling, length) % kSlotCount;
			while (slots[slot].used)
				slot = (slot + 1) % kSlotCount;
			slots[slot].spelling = spelling;
			slots[slot].length = length;
			slots[slot].type = kSimpleTokenSpellings[index].type;
			slots[slot].used = true;
		}
	}
};

const Index kIndex;

} // namespace

const char* SimpleTokenName(ETokenType type)
{
	return type < ETOKENTYPE_COUNT ? kSimpleTokenNames[type] : "";
}

bool LookupSimpleToken(const char* spelling, std::size_t length, ETokenType& type)
{
	std::size_t slot = HashSpelling(spelling, length) % kSlotCount;
	while (kIndex.slots[slot].used)
	{
		const Slot& entry = kIndex.slots[slot];
		if (entry.length == length && std::memcmp(entry.spelling, spelling, length) == 0)
		{
			type = entry.type;
			return true;
		}
		slot = (slot + 1) % kSlotCount;
	}
	return false;
}

} // namespace posttoken
} // namespace cppgm
