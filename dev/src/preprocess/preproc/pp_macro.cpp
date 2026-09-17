#include "preprocess/preproc/pp_macro.h"

#include "preprocess/preproc/pp_error.h"

namespace cppgm
{
namespace preprocess
{

namespace
{

const std::size_t kNoIndex = static_cast<std::size_t>(-1);

void SkipWhitespace(const std::vector<PPToken>& tokens, std::size_t& at)
{
	while (at < tokens.size() && tokens[at].kind == kPPWhitespace)
		++at;
}

std::size_t NextSignificant(const std::vector<PPToken>& tokens, std::size_t at)
{
	SkipWhitespace(tokens, at);
	return at < tokens.size() ? at : kNoIndex;
}

std::size_t PreviousSignificant(const std::vector<PPToken>& tokens, std::size_t at)
{
	while (at > 0)
	{
		--at;
		if (tokens[at].kind != kPPWhitespace)
			return at;
	}
	return kNoIndex;
}

bool LookupParameter(const PPMacro& macro, const std::string& spelling, std::uint32_t& index)
{
	for (std::size_t position = 0; position < macro.parameters.size(); ++position)
	{
		if (macro.parameters[position] == spelling)
		{
			index = static_cast<std::uint32_t>(position);
			return true;
		}
	}
	if (macro.variadic && spelling == "__VA_ARGS__")
	{
		index = static_cast<std::uint32_t>(macro.parameters.size());
		return true;
	}
	return false;
}

// The replacement list with no leading or trailing whitespace and at most one
// whitespace token between two tokens, which is the form two definitions are
// compared in.
void NormalizeBody(const std::vector<PPToken>& rest, std::size_t at,
                   std::vector<PPToken>& body)
{
	body.clear();
	bool pending = false;
	bool started = false;
	for (std::size_t index = at; index < rest.size(); ++index)
	{
		const PPToken& token = rest[index];
		if (token.kind == kPPWhitespace)
		{
			if (started)
				pending = true;
			continue;
		}
		if (pending)
		{
			PPToken space;
			space.spelling = " ";
			space.file = token.file;
			space.line = token.line;
			space.kind = kPPWhitespace;
			body.push_back(space);
			pending = false;
		}
		body.push_back(token);
		started = true;
	}
}

// The `( ... )` of a function-like definition, starting at the `(`.  Returns
// the index just past the `)`.
std::size_t ParseParameterList(const std::vector<PPToken>& rest, std::size_t at,
                               PPMacro& macro)
{
	++at;
	SkipWhitespace(rest, at);
	if (at < rest.size() && PPTokenIsPunctuator(rest[at], ")"))
		return at + 1;

	for (;;)
	{
		SkipWhitespace(rest, at);
		if (at >= rest.size())
			throw PreprocessError("unterminated macro parameter list");

		if (PPTokenIsPunctuator(rest[at], "..."))
		{
			macro.variadic = true;
			++at;
			SkipWhitespace(rest, at);
			if (at >= rest.size() || !PPTokenIsPunctuator(rest[at], ")"))
				throw PreprocessError("`...` must be the last macro parameter");
			return at + 1;
		}

		if (rest[at].kind != kPPIdentifier)
			throw PreprocessError("macro parameter is not an identifier");
		const std::string name = rest[at].spelling;
		if (name == "__VA_ARGS__")
			throw PreprocessError("`__VA_ARGS__` cannot be a named macro parameter");
		for (std::size_t position = 0; position < macro.parameters.size(); ++position)
		{
			if (macro.parameters[position] == name)
				throw PreprocessError("duplicate macro parameter");
		}
		macro.parameters.push_back(name);
		++at;

		SkipWhitespace(rest, at);
		if (at >= rest.size())
			throw PreprocessError("unterminated macro parameter list");
		if (PPTokenIsPunctuator(rest[at], ")"))
			return at + 1;
		if (PPTokenIsPunctuator(rest[at], ","))
		{
			++at;
			continue;
		}
		throw PreprocessError("expected `,` or `)` in a macro parameter list");
	}
}

// Splits the normalized replacement list into the parts expansion substitutes.
// Every rejection case of the handout is a property of this list, so it is
// raised here, at the definition, and not when the macro happens to be used.
void BuildParts(PPMacro& macro)
{
	const std::vector<PPToken>& body = macro.body;
	macro.parts.clear();
	// Slot `parameters.size()` is the variadic argument, which only exists for
	// a variadic definition.
	macro.parameter_expanded.assign(macro.parameters.size() + (macro.variadic ? 1 : 0), false);

	for (std::size_t index = 0; index < body.size(); ++index)
	{
		const PPToken& token = body[index];

		// White space is a part of the replacement list like any other: `#`
		// keeps the separations the argument was written with, and a paste
		// operand is the nearest token either side of the operator.
		if (token.kind == kPPWhitespace)
		{
			PPBodyPart space;
			space.kind = kPPBodyToken;
			space.token = token;
			space.parameter = 0;
			space.raw = false;
			macro.parts.push_back(space);
			continue;
		}

		// 16.3.2's `#` is an operator of a function-like macro's replacement
		// list only.  In an object-like macro's list it is an ordinary
		// preprocessing-token, which is what lets `#define h # ## #` be a
		// definition at all: the two `#` are that operator's operands.
		if (macro.function_like && PPTokenIsHashOperator(token))
		{
			const std::size_t next = NextSignificant(body, index + 1);
			std::uint32_t parameter = 0;
			if (next != kNoIndex && body[next].kind == kPPIdentifier &&
			    LookupParameter(macro, body[next].spelling, parameter))
			{
				PPBodyPart part;
				part.kind = kPPBodyStringize;
				part.parameter = parameter;
				part.raw = true;
				macro.parts.push_back(part);
				index = next;
				continue;
			}
			// 16.3.2 makes every `#` of a function-like macro's replacement
			// list an operator, so one that does not stringize a parameter is
			// a rejection - including the `#` of a written `# ## #`, which is
			// only a definition because an object-like list has no such
			// operator.
			throw PreprocessError("`#` is not followed by a macro parameter");
		}
		else if (PPTokenIsPasteOperator(token))
		{
			// 16.3.3's operands are the tokens on either side, so a `##` at
			// either end of the list or next to another `##` has no such pair.
			const std::size_t previous = PreviousSignificant(body, index);
			if (previous == kNoIndex)
				throw PreprocessError("`##` cannot begin a macro replacement list");
			if (PPTokenIsPasteOperator(body[previous]))
				throw PreprocessError("`##` has no left operand");
			const std::size_t next = NextSignificant(body, index + 1);
			if (next != kNoIndex && PPTokenIsPasteOperator(body[next]))
				throw PreprocessError("`##` has no right operand");
		}
		else if (token.kind == kPPIdentifier)
		{
			std::uint32_t parameter = 0;
			if (LookupParameter(macro, token.spelling, parameter))
			{
				const std::size_t previous = PreviousSignificant(body, index);
				const std::size_t next = NextSignificant(body, index + 1);
				PPBodyPart part;
				part.kind = kPPBodyParameter;
				part.parameter = parameter;
				part.raw =
					(previous != kNoIndex && PPTokenIsPasteOperator(body[previous])) ||
					(next != kNoIndex && PPTokenIsPasteOperator(body[next]));
				macro.parts.push_back(part);
				// Only a reference that is neither stringized nor an operand of
				// `##` asks for the argument to be macro-expanded.
				if (!part.raw)
					macro.parameter_expanded[parameter] = true;
				continue;
			}
			if (token.spelling == "__VA_ARGS__")
				throw PreprocessError("`__VA_ARGS__` outside a variadic macro");
		}

		PPBodyPart part;
		part.kind = kPPBodyToken;
		part.token = token;
		part.parameter = 0;
		part.raw = false;
		macro.parts.push_back(part);
	}

	std::size_t last = macro.parts.size();
	while (last > 0 && macro.parts[last - 1].kind == kPPBodyToken &&
	       macro.parts[last - 1].token.kind == kPPWhitespace)
	{
		--last;
	}
	if (last > 0 && macro.parts[last - 1].kind == kPPBodyToken &&
	    PPTokenIsPasteOperator(macro.parts[last - 1].token))
	{
		throw PreprocessError("`##` cannot end a macro replacement list");
	}
}

} // namespace

PPMacro ParseMacroDefinition(const PPToken& name, bool space,
                             const std::vector<PPToken>& rest, std::uint32_t id)
{
	PPMacro macro;
	macro.name = name.spelling;
	macro.id = id;
	macro.function_like = false;
	macro.variadic = false;
	macro.builtin = kPPBuiltinNone;

	std::size_t at = 0;
	if (!space)
	{
		SkipWhitespace(rest, at);
		// `name (` with nothing between them is a function-like definition;
		// `name (` with a separation is an object-like one whose replacement
		// list starts with `(`.
		if (at < rest.size() && PPTokenIsPunctuator(rest[at], "("))
		{
			macro.function_like = true;
			at = ParseParameterList(rest, at, macro);
		}
		else
		{
			at = 0;
		}
	}

	NormalizeBody(rest, at, macro.body);
	BuildParts(macro);
	return macro;
}

bool MacrosAreIdentical(const PPMacro& left, const PPMacro& right)
{
	if (left.function_like != right.function_like || left.variadic != right.variadic ||
	    left.builtin != right.builtin || left.parameters != right.parameters)
	{
		return false;
	}
	if (left.body.size() != right.body.size())
		return false;
	for (std::size_t index = 0; index < left.body.size(); ++index)
	{
		if (left.body[index].kind != right.body[index].kind ||
		    left.body[index].spelling != right.body[index].spelling)
		{
			return false;
		}
	}
	return true;
}

bool IsVariadicReferenceName(const std::string& spelling)
{
	return spelling == "__VA_ARGS__";
}

} // namespace preprocess
} // namespace cppgm