// Macro definitions and the table of the ones currently defined.
//
// A definition is parsed once, at its `#define`, into the two things expansion
// needs: a parameter list with the name index each parameter reference resolves
// to, and a replacement list split into parts whose substitution rule is
// already decided.  `macros.md`'s rejection cases - a `#` that cannot stringize
// a parameter, a `##` with no operand, a non-final `...`, `__VA_ARGS__` outside
// a variadic body - are all properties of the definition and are raised there,
// which is where the course fixture expects them.
//
// The replacement list is kept in a normalized form as well: at most one
// whitespace token between tokens and no leading or trailing whitespace.  Two
// lists then compare by spelling, which is 16.3's "all white-space separations
// are considered identical" with the token spellings that may not differ.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "preprocess/preproc/pp_token.h"

namespace cppgm
{
namespace preprocess
{

// The course's predefined object-like macros.  Their replacement is not stored
// in the body: `__LINE__`, `__FILE__` and `__COUNTER__` depend on the position
// and the run, and `__DATE__`/`__TIME__` on the build, so expansion asks the
// preprocessor for the tokens rather than reading them from a list.
enum EPPBuiltinMacro : std::uint8_t
{
	kPPBuiltinNone,
	kPPBuiltinFile,
	kPPBuiltinLine,
	kPPBuiltinCounter,
	kPPBuiltinDate,
	kPPBuiltinTime
};

enum EPPBodyPartKind : std::uint8_t
{
	// A replacement token that is not a parameter reference.
	kPPBodyToken,
	// A parameter reference: the named parameter's index, or the variadic's
	// index (`parameters.size()`) for `__VA_ARGS__`.
	kPPBodyParameter,
	// `#` followed by a parameter reference.
	kPPBodyStringize
};

struct PPBodyPart
{
	PPToken token;
	std::uint32_t parameter;
	EPPBodyPartKind kind;
	// A parameter adjacent to `##` is substituted with the argument as written;
	// every other reference is substituted with the macro-expanded argument.
	bool raw;
};

struct PPMacro
{
	std::string name;
	std::uint32_t id;
	// True when the definition is `name (` with no whitespace between them.
	bool function_like;
	bool variadic;
	// The named parameters, without the trailing `...`.  The variadic
	// argument's index is `parameters.size()`.
	std::vector<std::string> parameters;
	// The normalized replacement list, for redefinition comparison.
	std::vector<PPToken> body;
	// The replacement list as the substitution rules see it, with whitespace
	// removed and every `#`/parameter decided.
	std::vector<PPBodyPart> parts;
	// Parameter i is referenced somewhere that needs its macro-expanded
	// argument.  An argument no reference asks for is never expanded, which is
	// what keeps `stringize(max(0))` legal.
	std::vector<bool> parameter_expanded;
	EPPBuiltinMacro builtin;
};

// The macro name and everything after it on a `#define` line.  `space` is
// whether a whitespace-sequence separates the name from the replacement list,
// which is what tells `#define f(x)` from `#define f (x)`.
PPMacro ParseMacroDefinition(const PPToken& name, bool space,
                             const std::vector<PPToken>& rest, std::uint32_t id);

// True when two definitions of the same name agree: same function-likeness,
// same parameter names in the same order, and replacement lists equal token for
// token including which separations are whitespace.
bool MacrosAreIdentical(const PPMacro& left, const PPMacro& right);

class MacroTable
{
public:
	MacroTable()
		: next_id_(1)
	{}

	// The macro named `name`, or null.  The pointer stays valid until the next
	// `Define` or `Undefine`, and no directive runs while a replacement is
	// being rescanned, so an expansion may hold one across its own rescans.
	const PPMacro* Find(const std::string& name) const
	{
		std::unordered_map<std::string, PPMacro>::const_iterator found = macros_.find(name);
		return found == macros_.end() ? nullptr : &found->second;
	}

	// Installs a definition, replacing any previous one.  A macro's identity is
	// its id: a redefinition is a different macro, so a name already painted on
	// a token never blocks the new definition by accident.
	void Define(PPMacro macro)
	{
		macros_[macro.name] = std::move(macro);
	}

	void Undefine(const std::string& name) { macros_.erase(name); }

	// A fresh identity for the next definition.
	std::uint32_t TakeId() { return next_id_++; }

	void Clear()
	{
		macros_.clear();
		next_id_ = 1;
	}

private:
	std::unordered_map<std::string, PPMacro> macros_;
	std::uint32_t next_id_;
};

// The identifier spellings the course reserves.  `__VA_ARGS__` is only legal
// as the variadic reference of a variadic function-like macro; the two other
// names are the course's function-like built-ins.
bool IsVariadicReferenceName(const std::string& spelling);

} // namespace preprocess
} // namespace cppgm