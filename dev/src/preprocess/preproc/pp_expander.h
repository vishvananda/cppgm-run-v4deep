// Macro replacement: the rescan of 16.3 with the course's nesting rule.
//
// The input is a text-sequence's tokens in a stack whose back is the next token
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
//
// A text-sequence is not held as one owning vector.  The sequence arrives from
// an `IPPTextFeed` a token at a time and the rescan runs as far as the tokens
// seen so far allow, then suspends.  Only a construct whose meaning still
// depends on tokens that have not arrived is retained: a function-like macro
// name whose `(` may yet follow, and an argument list that is still open.  Both
// are bounded by the source construct, not by the translation unit.

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

// The tokens of one text sequence, pulled in source order as the rescan needs
// them.  False means no token is available *now*: the sequence may still have
// more, which is why the rescan suspends rather than deciding.
class IPPTextFeed
{
public:
	virtual bool NextTextToken(PPToken& token) = 0;

	virtual ~IPPTextFeed() {}
};

class MacroExpander
{
public:
	MacroExpander(MacroTable& macros, IMacroBuiltins& builtins, PPPaintArena& paint)
		: macros_(macros)
		, builtins_(builtins)
		, paint_(paint)
	{}

	// Macro-replaces `input` and appends the result to `output`.  The whole
	// input is at hand, so the rescan never suspends.  Used for the bounded
	// inputs - a directive line, an argument prescan - and not for a
	// translation unit's text sequences.
	void Expand(const std::vector<PPToken>& input, std::vector<PPToken>& output);

	// Macro-replaces a text sequence that arrives from `feed`, reporting each
	// token to `sink` as soon as it is final.  `Begin` starts the sequence,
	// `Pump` continues it after the feed has grown, and `Finish` declares the
	// feed exhausted and drains what is left.
	void BeginTextSequence(IPPTextFeed& feed, IPPTextSink& sink);
	void PumpTextSequence();
	void FinishTextSequence();

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
	//
	// The stack's back is the next token to examine and its front is the token
	// furthest ahead, so a replacement goes on the back and a token the feed
	// supplies goes in front of the tokens that follow the invocation.
	struct Frame
	{
		std::vector<PPToken> stack;
		std::vector<Argument> arguments;
		// Set on the frame of a text sequence: the tokens come from here.
		IPPTextFeed* feed;
		// Set on the frame of a top-level text sequence: the reported tokens go
		// here, a chunk at a time, instead of accumulating in an output vector.
		IPPTextSink* sink;
		// The invocation whose argument list is still open.  The `(` has been
		// consumed, `arguments` holds what has been read, and `depth` is the
		// parenthesis nesting inside the argument being read.
		const PPMacro* collecting;
		PPToken collecting_head;
		unsigned collecting_depth;
		// How far the lookahead of the head token got before the feed ran dry,
		// or 0 when no head is waiting.  The head stays at the stack's back and
		// a supplied token goes in at the front, so the distance from the back
		// is unchanged and a resumed lookahead does not walk the same white
		// space twice.
		std::size_t look;

		Frame()
			: feed(nullptr)
			, sink(nullptr)
			, collecting(nullptr)
			, collecting_depth(0)
			, look(0)
		{}
	};

	void Run(Frame& frame, std::vector<PPToken>& output);
	void FlushChunk(Frame& frame, std::vector<PPToken>& output);
	// One more token of the sequence, or false when the feed has none now.
	bool Pull(Frame& frame);
	bool Invoke(Frame& frame, const PPMacro& macro, std::vector<PPToken>& output);
	bool CollectArguments(Frame& frame);
	void CompleteInvocation(Frame& frame);
	void Substitute(Frame& frame, const PPMacro& macro, const PPToken& head);

	// The argument `index` as the definition substitutes it.  `raw` selects the
	// argument as written, which is what a stringized parameter and a `##`
	// operand need; `argument_paint` is the paint a substituted value joins.
	const std::vector<PPToken>& PaintedArgument(Frame& frame, std::uint32_t index,
	                                            bool raw, PPMacroPaint argument_paint);

	// True when the token spells a function-like macro, which is the only kind
	// of name whose invocation needs the `(` that follows it.
	bool NamesFunctionLikeMacro(const PPToken& token) const;

	MacroTable& macros_;
	IMacroBuiltins& builtins_;
	// The translation unit's paint nodes.  The extenders hand one out and never
	// free it; the arena is released whole when the unit's last token has been
	// consumed.
	PPPaintArena& paint_;
	// The block a streamed text sequence is expanded into before it is handed
	// over.  Keeping the capacity across sequences keeps the expansion free of
	// per-token allocation.
	std::vector<PPToken> chunk_;
	std::deque<Frame> frames_;
};

} // namespace preprocess
} // namespace cppgm
