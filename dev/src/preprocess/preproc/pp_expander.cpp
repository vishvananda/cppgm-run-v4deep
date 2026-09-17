#include "preprocess/preproc/pp_expander.h"

#include <utility>

#include "preprocess/preproc/pp_error.h"
#include "preprocess/preproc/pp_token_reader.h"

namespace cppgm
{
namespace preprocess
{

namespace
{

// How many finalized tokens are held before they are handed to the sink.  The
// block is small enough that a folded sequence costs almost nothing and large
// enough that the virtual call per token is not the texture of the run.
const std::size_t kFlushTokens = 4096;

PPToken Placemarker(const PPToken& head, const PPMacroPaint& paint)
{
	PPToken token;
	token.file = head.file;
	token.line = head.line;
	token.kind = kPPPlacemarker;
	token.paint = paint;
	return token;
}

// The spelling a `#` produces: the argument's tokens joined by a single space
// wherever the argument had whitespace, with the leading and trailing
// separations deleted, and `"` and `\` escaped so the result is a string
// literal that reads back as that spelling.
void StringizeArgument(const std::vector<PPToken>& raw, std::string& out)
{
	out.push_back('"');
	bool pending_space = false;
	bool started = false;
	for (std::size_t index = 0; index < raw.size(); ++index)
	{
		const PPToken& token = raw[index];
		if (token.kind == kPPWhitespace)
		{
			if (started)
				pending_space = true;
			continue;
		}
		if (pending_space)
		{
			out.push_back(' ');
			pending_space = false;
		}
		// A literal's spelling is quoted source, so its quotes and backslashes
		// are escaped to keep them in the produced string.  Anywhere else a
		// backslash is an ordinary character of the spelling and is copied as
		// written.
		const bool quoted = token.kind == kPPStringLiteral ||
		                    token.kind == kPPCharacterLiteral ||
		                    token.kind == kPPUserDefinedStringLiteral ||
		                    token.kind == kPPUserDefinedCharacterLiteral;
		for (std::size_t at = 0; at < token.spelling.size(); ++at)
		{
			const char character = token.spelling[at];
			if (quoted && (character == '"' || character == '\\'))
				out.push_back('\\');
			out.push_back(character);
		}
		started = true;
	}
	out.push_back('"');
}

void TrimWhitespace(std::vector<PPToken>& tokens)
{
	std::size_t begin = 0;
	while (begin < tokens.size() && tokens[begin].kind == kPPWhitespace)
		++begin;
	std::size_t end = tokens.size();
	while (end > begin && tokens[end - 1].kind == kPPWhitespace)
		--end;
	if (begin == 0 && end == tokens.size())
		return;
	std::vector<PPToken> trimmed(tokens.begin() + begin, tokens.begin() + end);
	tokens.swap(trimmed);
}

// The next part of a replacement list that is not white space, or the list's
// size when there is none.
std::size_t NextSignificantPart(const PPMacro& macro, std::size_t at)
{
	while (at < macro.parts.size() && macro.parts[at].kind == kPPBodyToken &&
	       macro.parts[at].token.kind == kPPWhitespace)
	{
		++at;
	}
	return at;
}

// True when the next significant part of the replacement list is a written
// `(` - the one thing that can turn the name before it into an invocation
// before the tokens after the macro are reached.
bool OpensInvocationPart(const PPMacro& macro, std::size_t at)
{
	const std::size_t next = NextSignificantPart(macro, at);
	return next < macro.parts.size() && macro.parts[next].kind == kPPBodyToken &&
	       PPTokenIsPunctuator(macro.parts[next].token, "(");
}

bool IsEmptyArgument(const std::vector<PPToken>& tokens)
{
	for (std::size_t index = 0; index < tokens.size(); ++index)
	{
		if (tokens[index].kind != kPPWhitespace)
			return false;
	}
	return true;
}

} // namespace

void MacroExpander::Expand(const std::vector<PPToken>& input, std::vector<PPToken>& output)
{
	frames_.push_back(Frame());
	Frame& frame = frames_.back();
	frame.stack.assign(input.rbegin(), input.rend());
	Run(frame, output);
	frames_.pop_back();
}

void MacroExpander::BeginTextSequence(IPPTextFeed& feed, IPPTextSink& sink)
{
	frames_.push_back(Frame());
	Frame& frame = frames_.back();
	frame.feed = &feed;
	frame.sink = &sink;
	Run(frame, chunk_);
}

void MacroExpander::PumpTextSequence()
{
	Frame& frame = frames_.back();
	Run(frame, chunk_);
}

void MacroExpander::FinishTextSequence()
{
	Frame& frame = frames_.back();
	frame.feed = nullptr;
	Run(frame, chunk_);
	FlushChunk(frame, chunk_);
	frames_.pop_back();
}

bool MacroExpander::Pull(Frame& frame)
{
	if (frame.feed == nullptr)
		return false;
	PPToken token;
	if (!frame.feed->NextTextToken(token))
		return false;
	frame.stack.insert(frame.stack.begin(), std::move(token));
	return true;
}

void MacroExpander::FlushChunk(Frame& frame, std::vector<PPToken>& output)
{
	if (frame.sink == nullptr || output.empty())
	{
		output.clear();
		return;
	}
	for (std::size_t index = 0; index < output.size(); ++index)
		frame.sink->EmitToken(output[index]);
	output.clear();
}

void MacroExpander::Run(Frame& frame, std::vector<PPToken>& output)
{
	std::vector<PPToken>& stack = frame.stack;
	for (;;)
	{
		// An argument list that is still open is finished before the head of
		// the next invocation is examined, because the head it belongs to has
		// already been consumed.
		if (frame.collecting != nullptr)
		{
			if (!CollectArguments(frame))
				break;
			CompleteInvocation(frame);
			continue;
		}

		if (stack.empty())
		{
			if (!Pull(frame))
				break;
			continue;
		}

		const PPToken& token = stack.back();
		if (token.kind == kPPPlacemarker)
		{
			// A placemarker is `##`'s empty operand.  Once the paste is done it
			// has no spelling to report and is deleted.
			stack.pop_back();
			continue;
		}
		if (token.kind == kPPIdentifier)
		{
			if (IsVariadicReferenceName(token.spelling))
			{
				// The course reserves the name for the variadic parameter of a
				// variadic definition; anywhere else it is a rejection.
				throw PreprocessError("`__VA_ARGS__` outside a variadic macro");
			}
			const PPMacro* macro = macros_.Find(token.spelling);
			if (macro != nullptr && !PPTokenIsPainted(token, macro->id))
			{
				// False means the invocation is undecided: its `(` or the rest
				// of its argument list has not arrived yet, so the sequence
				// waits here rather than guessing.
				if (!Invoke(frame, *macro, output))
					break;
				continue;
			}
		}
		output.push_back(std::move(stack.back()));
		stack.pop_back();
		if (frame.sink != nullptr && output.size() >= kFlushTokens)
			FlushChunk(frame, output);
	}
}

bool MacroExpander::Invoke(Frame& frame, const PPMacro& macro, std::vector<PPToken>& output)
{
	std::vector<PPToken>& stack = frame.stack;
	const PPToken head = stack.back();

	if (!macro.function_like)
	{
		frame.look = 0;
		stack.pop_back();
		if (macro.builtin != kPPBuiltinNone)
		{
			builtins_.ExpandBuiltinMacro(macro.builtin, head, output);
			return true;
		}
		Substitute(frame, macro, head);
		return true;
	}

	// A function-like macro name is an invocation only when the next token of
	// the sequence is `(`; white space between the two is allowed.  The tokens
	// the sequence has not produced yet are pulled rather than assumed absent:
	// the `(` may open the next line, and the name may be the last thing in the
	// file.
	std::size_t look = frame.look != 0 ? frame.look : 1;
	for (;;)
	{
		while (look < stack.size() &&
		       stack[stack.size() - 1 - look].kind == kPPWhitespace)
		{
			++look;
		}
		if (look < stack.size())
			break;
		if (frame.feed == nullptr)
			break;
		if (!Pull(frame))
		{
			frame.look = look;
			return false;
		}
	}
	frame.look = 0;

	if (look >= stack.size() ||
	    !PPTokenIsPunctuator(stack[stack.size() - 1 - look], "("))
	{
		// When it is not, the name is emitted as an ordinary identifier and is
		// never examined again, which is what leaves `CALL OPEN )` as
		// `CALL ( )`.
		output.push_back(std::move(stack.back()));
		stack.pop_back();
		return true;
	}

	// The name, the white space between it and the `(`, and the `(` itself
	// belong to the invocation and are consumed with it.  The arguments are
	// read by `CollectArguments`, which is resumed rather than restarted when
	// the sequence pauses, so it never has to recognize the `(` again.
	for (std::size_t popped = 0; popped <= look; ++popped)
		stack.pop_back();

	frame.arguments.clear();
	frame.arguments.push_back(Argument());
	frame.collecting_depth = 0;
	frame.collecting_head = head;
	frame.collecting = &macro;

	if (!CollectArguments(frame))
		return false;
	CompleteInvocation(frame);
	return true;
}

bool MacroExpander::CollectArguments(Frame& frame)
{
	std::vector<PPToken>& stack = frame.stack;
	for (;;)
	{
		if (stack.empty())
		{
			if (!Pull(frame))
			{
				// Only a sequence that has ended leaves the invocation
				// unterminated; one that has merely paused is resumed.
				if (frame.feed != nullptr)
					return false;
				throw PreprocessError("unterminated macro invocation");
			}
			continue;
		}

		PPToken token = std::move(stack.back());
		stack.pop_back();
		if (token.kind == kPPOpOrPunc)
		{
			if (token.spelling == "(")
			{
				++frame.collecting_depth;
			}
			else if (token.spelling == ")")
			{
				if (frame.collecting_depth == 0)
					return true;
				--frame.collecting_depth;
			}
			else if (token.spelling == "," && frame.collecting_depth == 0)
			{
				frame.arguments.push_back(Argument());
				continue;
			}
		}
		frame.arguments.back().raw.push_back(std::move(token));
	}
}

void MacroExpander::CompleteInvocation(Frame& frame)
{
	const PPMacro& macro = *frame.collecting;
	frame.collecting = nullptr;

	// The arguments are kept exactly as they were written.  The variadic
	// argument is the one place the separation after a comma survives into the
	// produced text: `#__VA_ARGS__` must see `second, and` as written, so no
	// argument is trimmed here.  Every other use trims what it needs.

	// `A()` is no argument at all for `#define A()`, and one empty argument for
	// `#define A(x)`.
	if (macro.parameters.empty() && !macro.variadic && frame.arguments.size() == 1 &&
	    IsEmptyArgument(frame.arguments[0].raw))
	{
		frame.arguments.clear();
	}

	if (macro.variadic)
	{
		if (frame.arguments.size() < macro.parameters.size())
			throw PreprocessError("wrong number of macro arguments");
	}
	else if (frame.arguments.size() != macro.parameters.size())
	{
		throw PreprocessError("wrong number of macro arguments");
	}

	if (macro.variadic)
	{
		// The variable argument is every argument past the named parameters,
		// joined back into the one token sequence it was written as.
		const std::size_t variadic_index = macro.parameters.size();
		Argument joined;
		if (frame.arguments.size() > variadic_index)
		{
			joined.raw = frame.arguments[variadic_index].raw;
			for (std::size_t index = variadic_index + 1; index < frame.arguments.size(); ++index)
			{
				PPToken comma;
				comma.spelling = ",";
				comma.kind = kPPOpOrPunc;
				comma.file = 0;
				comma.line = 0;
				joined.raw.push_back(comma);
				joined.raw.insert(joined.raw.end(), frame.arguments[index].raw.begin(),
				                  frame.arguments[index].raw.end());
			}
		}
		frame.arguments.resize(variadic_index);
		frame.arguments.push_back(joined);
	}

	Substitute(frame, macro, frame.collecting_head);
}

const std::vector<PPToken>& MacroExpander::PaintedArgument(
	Frame& frame, std::uint32_t index, bool raw, const PPMacroPaint& argument_paint)
{
	Argument& argument = frame.arguments[index];
	if (raw)
	{
		// `##`'s operand is the argument as written.  A substituted token starts
		// a fresh chain, so it carries none of the names the invocation had
		// accumulated: the names it does carry are the ones its own expansion
		// left on it, and the paste that consumes it is what decides the result.
		if (!argument.raw_painted_ready)
		{
			argument.raw_painted = argument.raw;
			TrimWhitespace(argument.raw_painted);
			for (std::size_t at = 0; at < argument.raw_painted.size(); ++at)
			{
				argument.raw_painted[at].paint = PPMacroPaint();
				argument.raw_painted[at].substituted = true;
			}
			argument.raw_painted_ready = true;
		}
		return argument.raw_painted;
	}
	if (!argument.expanded_ready)
	{
		// The argument is macro-replaced once, however many times the
		// definition names its parameter.
		Expand(argument.raw, argument.expanded);
		TrimWhitespace(argument.expanded);
		argument.expanded_ready = true;
	}
	if (!argument.expanded_painted_ready)
	{
		argument.expanded_painted = argument.expanded;
		for (std::size_t at = 0; at < argument.expanded_painted.size(); ++at)
		{
			argument.expanded_painted[at].paint = PPMacroPaintUnion(
				argument.expanded_painted[at].paint, argument_paint);
			argument.expanded_painted[at].substituted = true;
		}
		argument.expanded_painted_ready = true;
	}
	return argument.expanded_painted;
}

bool MacroExpander::NamesFunctionLikeMacro(const PPToken& token) const
{
	if (token.kind != kPPIdentifier)
		return false;
	const PPMacro* named = macros_.Find(token.spelling);
	return named != nullptr && named->function_like;
}

void MacroExpander::Substitute(Frame& frame, const PPMacro& macro, const PPToken& head)
{
	// Two paints, as `macros.md`'s nesting rule needs them.  A replacement list
	// is the one place the invocation's name is added to the head's chain, and a
	// parameter value is the one place the chain does not survive: a token that
	// came from a substitution carries its own names and the macro it is being
	// substituted into, but not the names the head had accumulated.
	const PPMacroPaint self_paint = PPMacroPaintAdd(
		head.substituted ? PPMacroPaint() : head.paint, macro.id);
	const PPMacroPaint argument_paint = PPMacroPaintAdd(
		head.substituted ? head.paint : PPMacroPaint(), macro.id);
	const std::uint32_t variadic_index = static_cast<std::uint32_t>(macro.parameters.size());

	// The parts in the order the definition wrote them, with every parameter
	// reference already resolved.  `paste` marks the `##` tokens this
	// definition wrote: a `##` that arrives through a parameter is an ordinary
	// token of the substituted text and must not paste anything, which is what
	// leaves `hash_hash`'s `# ## #` a token of its own.
	std::vector<PPToken> substituted;
	std::vector<bool> paste;
	substituted.reserve(macro.parts.size() + 8);
	paste.reserve(macro.parts.size() + 8);
	for (std::size_t index = 0; index < macro.parts.size();)
	{
		const PPBodyPart& part = macro.parts[index];
		// An object-like macro's replacement is painted whole: it is examined
		// again in place and every name in it is an invocation head by the time
		// it is reached.  So is a replacement part that cannot begin an
		// invocation - everything except a function-like macro name that no
		// written `(` follows.  That one is not painted: the parameter
		// references around it are replaced by arguments, so the `(` it would
		// have been invoked with is not the one the definition wrote, and
		// barring the name would bar an invocation the substituted text is free
		// to make.
		const bool carries_paint =
			!macro.function_like || part.kind != kPPBodyToken ||
			!NamesFunctionLikeMacro(part.token) || OpensInvocationPart(macro, index + 1);
		const PPMacroPaint own_paint = carries_paint ? self_paint : PPMacroPaint();

		// The GNU `, ## __VA_ARGS__`: an empty variable argument deletes the
		// comma, a non-empty one keeps it and is not pasted onto.
		const std::size_t comma_paste = NextSignificantPart(macro, index + 1);
		const std::size_t comma_argument = NextSignificantPart(macro, comma_paste + 1);
		if (part.kind == kPPBodyToken && PPTokenIsPunctuator(part.token, ",") &&
		    macro.variadic &&
		    comma_paste < macro.parts.size() && comma_argument < macro.parts.size() &&
		    macro.parts[comma_paste].kind == kPPBodyToken &&
		    PPTokenIsPasteOperator(macro.parts[comma_paste].token) &&
		    macro.parts[comma_argument].kind == kPPBodyParameter &&
		    macro.parts[comma_argument].parameter == variadic_index)
		{
			const std::vector<PPToken>& argument =
				PaintedArgument(frame, variadic_index, false, argument_paint);
			if (!argument.empty())
			{
				PPToken comma = part.token;
				comma.paint = own_paint;
				comma.substituted = false;
				substituted.push_back(comma);
				paste.push_back(false);
				substituted.insert(substituted.end(), argument.begin(), argument.end());
				paste.insert(paste.end(), argument.size(), false);
			}
			index = comma_argument + 1;
			continue;
		}

		if (part.kind == kPPBodyStringize)
		{
			const std::vector<PPToken>& argument = frame.arguments[part.parameter].raw;
			PPToken literal;
			StringizeArgument(argument, literal.spelling);
			literal.kind = kPPStringLiteral;
			literal.file = head.file;
			literal.line = head.line;
			literal.paint = own_paint;
			literal.substituted = false;
			substituted.push_back(literal);
			paste.push_back(false);
			++index;
			continue;
		}

		if (part.kind == kPPBodyParameter)
		{
			const std::vector<PPToken>& argument =
				PaintedArgument(frame, part.parameter, part.raw, argument_paint);
			if (argument.empty())
			{
				// A parameter next to `##` with nothing to substitute is the
				// operator's empty operand, not a deletion.
				if (part.raw)
				{
					PPToken placemarker = Placemarker(head, PPMacroPaint());
					placemarker.substituted = true;
					substituted.push_back(placemarker);
					paste.push_back(false);
				}
			}
			else
			{
				substituted.insert(substituted.end(), argument.begin(), argument.end());
				paste.insert(paste.end(), argument.size(), false);
			}
			++index;
			continue;
		}

		PPToken token = part.token;
		const bool is_paste = PPTokenIsPasteOperator(token);
		token.paint = own_paint;
		token.substituted = false;
		substituted.push_back(token);
		paste.push_back(is_paste);
		++index;
	}

	// `##` pastes what is on either side of it, left to right, so a parameter
	// with several tokens contributes only its edge token to the paste and the
	// rest of it is emitted unchanged.
	std::vector<PPToken> replaced;
	replaced.reserve(substituted.size());
	for (std::size_t index = 0; index < substituted.size();)
	{
		const PPToken& token = substituted[index];
		if (paste[index])
		{
			// 16.3.3: the operands are the tokens nearest the operator, and the
			// white space separating them from it is dropped.
			while (!replaced.empty() && replaced.back().kind == kPPWhitespace)
				replaced.pop_back();
			std::size_t right_index = index + 1;
			while (right_index < substituted.size() &&
			       substituted[right_index].kind == kPPWhitespace)
			{
				++right_index;
			}

			PPToken left;
			if (replaced.empty())
			{
				left = Placemarker(head, PPMacroPaint());
			}
			else
			{
				left = replaced.back();
				replaced.pop_back();
			}
			PPToken right = right_index < substituted.size()
				? substituted[right_index]
				: Placemarker(head, PPMacroPaint());

			if (left.kind == kPPPlacemarker)
			{
				replaced.push_back(right);
			}
			else if (right.kind == kPPPlacemarker)
			{
				replaced.push_back(left);
			}
			else
			{
				std::string spelling = left.spelling;
				spelling.append(right.spelling);
				std::vector<PPToken> pasted =
					RetokenizeSpelling(spelling, head.file, head.line);
				if (pasted.size() != 1)
					throw PreprocessError("invalid token paste");
				// A pasted token is the value of the invocation, so it is a
				// substituted token like any other.
				pasted[0].paint = PPMacroPaintUnion(left.paint, right.paint);
				pasted[0].substituted = true;
				pasted[0].file = head.file;
				pasted[0].line = head.line;
				replaced.push_back(pasted[0]);
			}
			index = right_index + 1;
			continue;
		}
		replaced.push_back(token);
		++index;
	}

	// 16.3: a replacement token is attributed to the invocation that produced
	// it, which is what makes `__LINE__` inside a macro report the line the
	// macro was used on.
	for (std::size_t index = 0; index < replaced.size(); ++index)
	{
		replaced[index].file = head.file;
		replaced[index].line = head.line;
	}

	// The replacement is examined again before the tokens that followed the
	// invocation, so it goes on the top of the stack, deepest token first.
	for (std::size_t index = replaced.size(); index > 0; --index)
		frame.stack.push_back(std::move(replaced[index - 1]));
}

} // namespace preprocess
} // namespace cppgm
