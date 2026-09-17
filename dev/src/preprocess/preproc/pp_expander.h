// Macro replacement: the rescan of 16.3 with the course's nesting rule.
//
// The input is a text-sequence's tokens in a stack whose top is the next token
// to examine, exactly the structure `macros.md`'s design note suggests: an
// invocation's tokens are popped and its replacement pushed, so the tokens that
// follow the invocation in the source are examined again after it - which is
// how `f(g)b)` becomes `1 g (b)` and then `1 2 b`.
//
// Recursion is suppressed by blue paint, not by a global cutoff.  A token
// carries the macro names it may no longer expand; the tokens of a replacement
// join the head token's paint with the invoked name, and a substituted argument
// keeps the paint it acquired while it was expanded and joins the invocation's.
// That is what stops `a -> b -> a`, what makes `g(f)(g)(3)` leave the last `g`
// alone, and what keeps a name painted inside a parameter - `f(z)` with
// `#define z z[0]` - from expanding again after substitution.
//
// An argument is macro-expanded lazily and once: only a parameter reference
// that is neither stringized nor an operand of `##` asks for the expanded form,
// and the expansion is cached for every other reference to the same parameter.
// `stringize(max(0))` therefore stays legal while `# x x` does not.

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "preprocess/preproc/pp_macro.h"
#include "preprocess/preproc/pp_token.h"

namespace cppgm
{
namespace preprocess
{

// The replacement of the course's predefined macros, which depends on where
// and when the invocation happened rather than on a stored list.
class IMacroBuiltins
{
public:
	virtual void ExpandBuiltinMacro(EPPBuiltinMacro which, const PPToken& head,
	                                std::vector<PPToken>& out) = 0;

	virtual ~IMacroBuiltins() {}
};

class MacroExpander
{
public:
	MacroExpander(MacroTable& macros, IMacroBuiltins& builtins)
		: macros_(macros)
		, builtins_(builtins)
	{}

	// Macro-replaces `input` and appends the result to `output`.  The same
	// expander serves every call: the frames it needs are on the heap and keep
	// their capacity across the run.
	void Expand(const std::vector<PPToken>& input, std::vector<PPToken>& output);

private:
	// One argument of an invocation.  The expanded form and both painted forms
	// are produced on demand, because most arguments are used one way only.
	struct Argument
	{
		std::vector<PPToken> raw;
		std::vector<PPToken> expanded;
		std::vector<PPToken> raw_painted;
		std::vector<PPToken> expanded_painted;
		bool expanded_ready;
		bool raw_painted_ready;
		bool expanded_painted_ready;

		Argument()
			: expanded_ready(false)
			, raw_painted_ready(false)
			, expanded_painted_ready(false)
		{}
	};

	// One rescan in progress.  Argument prescan is a nested rescan, so the
	// stack and the arguments belong to the frame and not to the expander.
	struct Frame
	{
		std::vector<PPToken> stack;
		std::vector<Argument> arguments;
	};

	void Run(Frame& frame, std::vector<PPToken>& output);
	void Invoke(Frame& frame, const PPMacro& macro, std::vector<PPToken>& output);
	void CollectArguments(Frame& frame, const PPMacro& macro);
	void Substitute(Frame& frame, const PPMacro& macro, const PPToken& head);

	// The argument `index` as the definition substitutes it.  `raw` selects the
	// argument as written, which is what a stringized parameter and a `##`
	// operand need; `argument_paint` is the paint a substituted value joins.
	const std::vector<PPToken>& PaintedArgument(Frame& frame, std::uint32_t index,
	                                            bool raw, const PPMacroPaint& argument_paint);

	// True when the token spells a function-like macro, which is the only kind
	// of name whose invocation needs the `(` that follows it.
	bool NamesFunctionLikeMacro(const PPToken& token) const;

	MacroTable& macros_;
	IMacroBuiltins& builtins_;
	std::deque<Frame> frames_;
};

} // namespace preprocess
} // namespace cppgm