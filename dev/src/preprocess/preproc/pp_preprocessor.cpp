#include "preprocess/preproc/pp_preprocessor.h"

#include <cctype>
#include <fstream>
#include <utility>

#include "preprocess/preproc/pp_error.h"
#include "preprocess/preproc/pp_token_reader.h"

namespace cppgm
{
namespace preprocess
{

namespace
{

// A directive's tokens end at the next new-line, and a whitespace-sequence
// inside the line is skipped without ever crossing that boundary.
void SkipLineWhitespace(const std::vector<PPToken>& tokens, std::size_t& at, std::size_t end)
{
	while (at < end && tokens[at].kind == kPPWhitespace)
		++at;
}

bool IsHash(const PPToken& token)
{
	return token.kind == kPPOpOrPunc &&
	       (token.spelling == "#" || token.spelling == "%:");
}

bool IsWhitespaceKind(EPPTokenKind kind)
{
	return kind == kPPWhitespace || kind == kPPNewLine;
}

// The tokens an expanded directive line consists of, with white space removed.
void SignificantTokens(const std::vector<PPToken>& tokens, std::vector<const PPToken*>& out)
{
	out.clear();
	for (std::size_t index = 0; index < tokens.size(); ++index)
	{
		if (!IsWhitespaceKind(tokens[index].kind) && tokens[index].kind != kPPPlacemarker)
			out.push_back(&tokens[index]);
	}
}

std::string ReadFileBytes(const std::string& path)
{
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	if (!in)
		throw PreprocessError("cannot open " + path);

	std::string bytes;
	const std::size_t chunk = 1 << 16;
	for (;;)
	{
		const std::size_t filled = bytes.size();
		bytes.resize(filled + chunk);
		in.read(&bytes[filled], static_cast<std::streamsize>(chunk));
		const std::streamsize got = in.gcount();
		bytes.resize(filled + static_cast<std::size_t>(got));
		if (got != static_cast<std::streamsize>(chunk))
			break;
	}
	return bytes;
}

// The integer a `#line` operand names, with any integer suffix ignored.
unsigned long long DecodeLineNumber(const std::string& spelling, bool& ok)
{
	ok = false;
	unsigned long long value = 0;
	std::size_t at = 0;
	int base = 10;
	if (spelling.size() > 2 && spelling[0] == '0' &&
	    (spelling[1] == 'x' || spelling[1] == 'X'))
	{
		base = 16;
		at = 2;
	}
	else if (spelling.size() > 1 && spelling[0] == '0')
	{
		base = 8;
		at = 1;
	}
	std::size_t digits = 0;
	for (; at < spelling.size(); ++at)
	{
		const char character = spelling[at];
		int digit = -1;
		if (character >= '0' && character <= '9')
			digit = character - '0';
		else if (base == 16 && character >= 'a' && character <= 'f')
			digit = character - 'a' + 10;
		else if (base == 16 && character >= 'A' && character <= 'F')
			digit = character - 'A' + 10;
		if (digit < 0 || digit >= base)
			break;
		value = value * static_cast<unsigned>(base) + static_cast<unsigned>(digit);
		++digits;
	}
	for (; at < spelling.size(); ++at)
	{
		const char suffix = spelling[at];
		if (suffix != 'u' && suffix != 'U' && suffix != 'l' && suffix != 'L')
			return 0;
	}
	if (digits == 0)
		return 0;
	ok = true;
	return value;
}

// The preprocessor's view of one synthesized number token.
PPToken NumberToken(const char* spelling, const PPToken& like)
{
	PPToken token;
	token.spelling = spelling;
	token.kind = kPPNumber;
	token.file = like.file;
	token.line = like.line;
	return token;
}

void FeedCtrlToken(posttoken::PostTokenStream& post, const PPToken& token)
{
	switch (token.kind)
	{
	case kPPWhitespace:
	case kPPNewLine:
	case kPPEof:
	case kPPPlacemarker:
		return;
	case kPPHeaderName: post.emit_header_name(token.spelling); return;
	case kPPIdentifier: post.emit_identifier(token.spelling); return;
	case kPPNumber: post.emit_pp_number(token.spelling); return;
	case kPPCharacterLiteral: post.emit_character_literal(token.spelling); return;
	case kPPUserDefinedCharacterLiteral:
		post.emit_user_defined_character_literal(token.spelling);
		return;
	case kPPStringLiteral: post.emit_string_literal(token.spelling); return;
	case kPPUserDefinedStringLiteral:
		post.emit_user_defined_string_literal(token.spelling);
		return;
	case kPPOpOrPunc: post.emit_preprocessing_op_or_punc(token.spelling); return;
	case kPPNonWhitespaceChar: post.emit_non_whitespace_char(token.spelling); return;
	}
}

} // namespace

bool DecodeOrdinaryStringLiteral(const std::string& spelling, std::string& out)
{
	std::size_t at = 0;
	if (spelling.compare(0, 2, "u8") == 0)
		at = 2;
	else if (!spelling.empty() &&
	         (spelling[0] == 'u' || spelling[0] == 'U' || spelling[0] == 'L'))
		at = 1;
	if (at >= spelling.size() || spelling[at] != '"')
		return false;
	if (spelling.size() < at + 2 || spelling[spelling.size() - 1] != '"')
		return false;

	const std::size_t end = spelling.size() - 1;
	++at;
	while (at < end)
	{
		const char character = spelling[at];
		if (character != '\\')
		{
			out.push_back(character);
			++at;
			continue;
		}
		++at;
		if (at >= end)
			return false;
		const char escape = spelling[at];
		++at;
		switch (escape)
		{
		case 'n': out.push_back('\n'); break;
		case 't': out.push_back('\t'); break;
		case 'r': out.push_back('\r'); break;
		case 'a': out.push_back('\a'); break;
		case 'b': out.push_back('\b'); break;
		case 'f': out.push_back('\f'); break;
		case 'v': out.push_back('\v'); break;
		case '0': case '1': case '2': case '3':
		case '4': case '5': case '6': case '7':
		{
			unsigned value = static_cast<unsigned>(escape - '0');
			for (unsigned count = 1; count < 3 && at < end; ++count)
			{
				const char digit = spelling[at];
				if (digit < '0' || digit > '7')
					break;
				value = value * 8 + static_cast<unsigned>(digit - '0');
				++at;
			}
			out.push_back(static_cast<char>(value));
			break;
		}
		default:
			// `\\`, `\"`, `\'`, `\?` and any other escape stand for the
			// character that follows the backslash.
			out.push_back(escape);
			break;
		}
	}
	return true;
}

Preprocessor::Preprocessor(IPPTextSink& sink, const std::string& build_date,
                           const std::string& build_time)
	: sink_(sink)
	, expander_(macros_, *this)
	, null_stream_(&null_buffer_)
	, ctrl_sink_(null_stream_)
	, ctrl_post_(ctrl_sink_)
	, counter_(0)
	, build_date_(build_date)
	, build_time_(build_time)
{}

void Preprocessor::ProcessPrimarySource(const std::string& path)
{
	macros_.Clear();
	pragma_once_.clear();
	counter_ = 0;
	stack_.clear();
	DefinePredefinedMacros();
	ProcessFile(path);
}

void Preprocessor::DefineBuiltin(const char* name, const char* body, EPPBuiltinMacro builtin)
{
	PPToken name_token;
	name_token.spelling = name;
	name_token.kind = kPPIdentifier;
	name_token.file = 0;
	name_token.line = 0;

	std::vector<PPToken> rest;
	if (body != nullptr)
	{
		rest = RetokenizeSpelling(body, 0, 0);
		if (rest.size() != 1)
			throw PreprocessError("bad predefined macro body");
	}

	PPMacro macro = ParseMacroDefinition(name_token, true, rest, macros_.TakeId());
	macro.builtin = builtin;
	macros_.Define(std::move(macro));
}

void Preprocessor::DefinePredefinedMacros()
{
	DefineBuiltin("__CPPGM__", "201303L", kPPBuiltinNone);
	DefineBuiltin("__cplusplus", "201103L", kPPBuiltinNone);
	DefineBuiltin("__STDC_HOSTED__", "1", kPPBuiltinNone);
	DefineBuiltin("__CPPGM_AUTHOR__", "\"Vishvananda Abrams\"", kPPBuiltinNone);
	DefineBuiltin("__FILE__", nullptr, kPPBuiltinFile);
	DefineBuiltin("__LINE__", nullptr, kPPBuiltinLine);
	DefineBuiltin("__COUNTER__", nullptr, kPPBuiltinCounter);
	DefineBuiltin("__DATE__", nullptr, kPPBuiltinDate);
	DefineBuiltin("__TIME__", nullptr, kPPBuiltinTime);
}

void Preprocessor::ExpandBuiltinMacro(EPPBuiltinMacro which, const PPToken& head,
                                      std::vector<PPToken>& out)
{
	switch (which)
	{
	case kPPBuiltinFile:
	{
		PPToken token;
		token.spelling = "\"";
		token.spelling += Current().presumed_file;
		token.spelling += "\"";
		token.kind = kPPStringLiteral;
		token.file = head.file;
		token.line = head.line;
		out.push_back(token);
		return;
	}
	case kPPBuiltinLine:
	{
		const unsigned long long line =
			static_cast<unsigned long long>(static_cast<long>(head.line) +
			                                Current().line_delta);
		out.push_back(NumberToken(std::to_string(line).c_str(), head));
		return;
	}
	case kPPBuiltinCounter:
		out.push_back(NumberToken(std::to_string(counter_++).c_str(), head));
		return;
	case kPPBuiltinDate:
	{
		PPToken token;
		token.spelling = "\"" + build_date_ + "\"";
		token.kind = kPPStringLiteral;
		token.file = head.file;
		token.line = head.line;
		out.push_back(token);
		return;
	}
	case kPPBuiltinTime:
	{
		PPToken token;
		token.spelling = "\"" + build_time_ + "\"";
		token.kind = kPPStringLiteral;
		token.file = head.file;
		token.line = head.line;
		out.push_back(token);
		return;
	}
	case kPPBuiltinNone:
		return;
	}
}

void Preprocessor::ProcessFile(const std::string& path)
{
	std::string bytes = ReadFileBytes(path);

	const std::uint32_t index = static_cast<std::uint32_t>(files_.size());
	files_.push_back(path);

	std::vector<PPToken> tokens = ReadPreprocessingTokens(std::move(bytes), index);

	FileState state;
	state.file = index;
	state.line_delta = 0;
	state.presumed_file = path;
	stack_.push_back(state);

	ProcessTokens(tokens);

	const bool unterminated = !stack_.back().conditionals.empty();
	stack_.pop_back();
	if (unterminated)
		throw PreprocessError("unterminated conditional inclusion");
}

void Preprocessor::ProcessTokens(const std::vector<PPToken>& tokens)
{
	const std::size_t count = tokens.size();
	std::size_t at = 0;
	bool line_start = true;
	while (at < count)
	{
		const PPToken& token = tokens[at];
		if (token.kind == kPPEof)
			break;
		if (token.kind == kPPWhitespace)
		{
			++at;
			continue;
		}
		if (token.kind == kPPNewLine)
		{
			++at;
			line_start = true;
			continue;
		}
		if (line_start && IsHash(token))
		{
			at = HandleDirective(tokens, at);
			line_start = true;
			continue;
		}

		// A text-sequence runs to the next directive, across new-lines: a
		// new-line is white space once the sequence is identified.
		const std::size_t begin = at;
		std::size_t end = count;
		while (at < count)
		{
			const PPToken& inner = tokens[at];
			if (inner.kind == kPPNewLine)
			{
				line_start = true;
				++at;
				continue;
			}
			if (inner.kind == kPPEof)
			{
				end = at;
				break;
			}
			if (inner.kind == kPPWhitespace)
			{
				++at;
				continue;
			}
			if (line_start && IsHash(inner))
			{
				end = at;
				break;
			}
			line_start = false;
			++at;
		}
		HandleTextSequence(tokens, begin, end);
	}
}

std::size_t Preprocessor::HandleDirective(const std::vector<PPToken>& tokens, std::size_t hash)
{
	std::size_t end = hash;
	while (end < tokens.size() && tokens[end].kind != kPPNewLine &&
	       tokens[end].kind != kPPEof)
	{
		++end;
	}
	std::size_t next = end;
	if (next < tokens.size() && tokens[next].kind == kPPNewLine)
		++next;

	std::size_t at = hash + 1;
	SkipLineWhitespace(tokens, at, end);

	std::string name;
	if (at < end && tokens[at].kind == kPPIdentifier)
	{
		name = tokens[at].spelling;
		++at;
	}

	if (name.empty())
	{
		// `#` alone is the null directive; a `#` followed by anything that is
		// not a directive name is a non-directive, which an active section
		// rejects and an inactive one ignores.
		if (at < end && Active())
			throw PreprocessError("invalid preprocessing directive");
		return next;
	}

	if (name == "if" || name == "ifdef" || name == "ifndef" || name == "elif" ||
	    name == "else" || name == "endif")
	{
		HandleConditional(name, tokens, at, end);
		return next;
	}

	if (!Active())
		return next;

	if (name == "include")
		HandleInclude(tokens, at, end);
	else if (name == "define")
		HandleDefine(tokens, at, end);
	else if (name == "undef")
		HandleUndef(tokens, at, end);
	else if (name == "line")
		HandleLine(tokens, at, end);
	else if (name == "pragma")
		HandlePragma(tokens, at, end);
	else if (name == "error")
		throw PreprocessError("#error directive");
	else
		throw PreprocessError("invalid preprocessing directive");

	return next;
}

void Preprocessor::HandleConditional(const std::string& name, const std::vector<PPToken>& tokens,
                                     std::size_t at, std::size_t end)
{
	FileState& state = Current();

	if (name == "endif")
	{
		if (state.conditionals.empty())
			throw PreprocessError("`#endif` without `#if`");
		state.conditionals.pop_back();
		return;
	}

	if (name == "else")
	{
		if (state.conditionals.empty())
			throw PreprocessError("`#else` without `#if`");
		Conditional& conditional = state.conditionals.back();
		if (conditional.seen_else)
			throw PreprocessError("`#else` after `#else`");
		conditional.seen_else = true;
		conditional.active = conditional.parent_active && !conditional.seen_true;
		return;
	}

	if (name == "elif")
	{
		if (state.conditionals.empty())
			throw PreprocessError("`#elif` without `#if`");
		Conditional& conditional = state.conditionals.back();
		if (conditional.seen_else)
			throw PreprocessError("`#elif` after `#else`");
		conditional.active = false;
		if (conditional.parent_active && !conditional.seen_true)
		{
			const bool value = EvaluateControllingExpression(tokens, at, end);
			conditional.active = value;
			conditional.seen_true = value;
		}
		return;
	}

	Conditional conditional;
	conditional.parent_active = Active();
	conditional.active = false;
	conditional.seen_true = false;
	conditional.seen_else = false;

	if (conditional.parent_active)
	{
		if (name == "if")
		{
			conditional.active = EvaluateControllingExpression(tokens, at, end);
		}
		else
		{
			SkipLineWhitespace(tokens, at, end);
			if (at >= end || tokens[at].kind != kPPIdentifier)
				throw PreprocessError("`#ifdef` without an identifier");
			const bool defined = macros_.Find(tokens[at].spelling) != nullptr;
			++at;
			SkipLineWhitespace(tokens, at, end);
			if (at != end)
				throw PreprocessError("extra tokens after `#ifdef`");
			conditional.active = (name == "ifdef") ? defined : !defined;
		}
		conditional.seen_true = conditional.active;
	}

	state.conditionals.push_back(conditional);
}

void Preprocessor::HandleDefine(const std::vector<PPToken>& tokens, std::size_t at,
                                std::size_t end)
{
	SkipLineWhitespace(tokens, at, end);
	if (at >= end || tokens[at].kind != kPPIdentifier)
		throw PreprocessError("`#define` without a macro name");

	const PPToken name = tokens[at];
	if (IsVariadicReferenceName(name.spelling))
		throw PreprocessError("`__VA_ARGS__` cannot be a macro name");
	++at;

	const bool space = at < end && tokens[at].kind == kPPWhitespace;
	std::vector<PPToken> rest(tokens.begin() + at, tokens.begin() + end);

	PPMacro macro = ParseMacroDefinition(name, space, rest, macros_.TakeId());
	const PPMacro* existing = macros_.Find(name.spelling);
	if (existing != nullptr && !MacrosAreIdentical(*existing, macro))
		throw PreprocessError("incompatible macro redefinition");
	macros_.Define(std::move(macro));
}

void Preprocessor::HandleUndef(const std::vector<PPToken>& tokens, std::size_t at,
                               std::size_t end)
{
	SkipLineWhitespace(tokens, at, end);
	if (at >= end || tokens[at].kind != kPPIdentifier)
		throw PreprocessError("`#undef` without a macro name");
	const std::string name = tokens[at].spelling;
	if (IsVariadicReferenceName(name))
		throw PreprocessError("`__VA_ARGS__` is not a macro name");
	++at;
	SkipLineWhitespace(tokens, at, end);
	if (at != end)
		throw PreprocessError("extra tokens after `#undef`");
	macros_.Undefine(name);
}

std::vector<PPToken> Preprocessor::ExpandLine(const std::vector<PPToken>& tokens,
                                              std::size_t at, std::size_t end)
{
	line_.assign(tokens.begin() + at, tokens.begin() + end);
	std::vector<PPToken> result;
	expander_.Expand(line_, result);
	return result;
}

void Preprocessor::HandleInclude(const std::vector<PPToken>& tokens, std::size_t at,
                                 std::size_t end)
{
	const std::vector<PPToken> expanded = ExpandLine(tokens, at, end);

	std::vector<const PPToken*> parts;
	SignificantTokens(expanded, parts);
	if (parts.size() != 1)
		throw PreprocessError("#include without a header name or string literal");

	std::string nextf;
	if (parts[0]->kind == kPPHeaderName)
	{
		const std::string& spelling = parts[0]->spelling;
		if (spelling.size() < 2 ||
		    !((spelling[0] == '<' && spelling[spelling.size() - 1] == '>') ||
		      (spelling[0] == '"' && spelling[spelling.size() - 1] == '"')))
		{
			throw PreprocessError("#include without a header name");
		}
		nextf = spelling.substr(1, spelling.size() - 2);
	}
	else if (parts[0]->kind == kPPStringLiteral)
	{
		if (!DecodeOrdinaryStringLiteral(parts[0]->spelling, nextf))
			throw PreprocessError("#include without a string literal");
	}
	else
	{
		throw PreprocessError("#include without a header name or string literal");
	}

	const std::string& presumed = Current().presumed_file;
	std::string relative;
	const std::size_t slash = presumed.rfind('/');
	if (slash != std::string::npos)
	{
		relative.assign(presumed, 0, slash + 1);
		relative += nextf;
	}

	std::string resolved;
	PreprocessorFileId fileid;
	if (!relative.empty() && GetPreprocessorFileId(relative, fileid))
		resolved = relative;
	else if (GetPreprocessorFileId(nextf, fileid))
		resolved = nextf;
	else
		throw PreprocessError("cannot find include file " + nextf);

	if (pragma_once_.find(fileid) != pragma_once_.end())
		return;

	ProcessFile(resolved);
}

void Preprocessor::HandleLine(const std::vector<PPToken>& tokens, std::size_t at,
                              std::size_t end)
{
	// `#line N` numbers the line that follows the directive, and the directive
	// may have been spliced out of several physical lines, so the line it is
	// counted from is the one the terminating new-line sits on.
	const unsigned long newline_line = end < tokens.size() && tokens[end].kind == kPPNewLine
		? static_cast<unsigned long>(tokens[end].line)
		: (end > 0 ? static_cast<unsigned long>(tokens[end - 1].line) : 1);

	const std::vector<PPToken> expanded = ExpandLine(tokens, at, end);
	std::vector<const PPToken*> parts;
	SignificantTokens(expanded, parts);
	if (parts.empty() || parts.size() > 2 || parts[0]->kind != kPPNumber)
		throw PreprocessError("malformed #line directive");

	bool ok = false;
	const unsigned long long number = DecodeLineNumber(parts[0]->spelling, ok);
	if (!ok || number == 0)
		throw PreprocessError("malformed #line directive");

	std::string file;
	if (parts.size() == 2)
	{
		if (parts[1]->kind != kPPStringLiteral ||
		    !DecodeOrdinaryStringLiteral(parts[1]->spelling, file))
		{
			throw PreprocessError("malformed #line directive");
		}
	}

	FileState& state = Current();
	state.line_delta = static_cast<long>(number) - static_cast<long>(newline_line) - 1;
	if (parts.size() == 2)
		state.presumed_file = file;
}

void Preprocessor::HandlePragma(const std::vector<PPToken>& tokens, std::size_t at,
                                std::size_t end)
{
	// `#pragma once` is the course's one supported pragma.  Everything else is
	// a pragma the implementation does not know and is ignored.
	SkipLineWhitespace(tokens, at, end);
	if (at < end && tokens[at].kind == kPPIdentifier && tokens[at].spelling == "once")
		ApplyPragmaOnce();
}

void Preprocessor::ApplyPragmaOnce()
{
	PreprocessorFileId fileid;
	if (GetPreprocessorFileId(Current().presumed_file, fileid))
		pragma_once_.insert(fileid);
}

void Preprocessor::ExecutePragma(const std::string& text)
{
	std::size_t at = 0;
	while (at < text.size() && std::isspace(static_cast<unsigned char>(text[at])))
		++at;
	const std::size_t begin = at;
	while (at < text.size() && !std::isspace(static_cast<unsigned char>(text[at])))
		++at;
	if (text.compare(begin, at - begin, "once") == 0)
		ApplyPragmaOnce();
}

void Preprocessor::HandleTextSequence(const std::vector<PPToken>& tokens, std::size_t begin,
                                      std::size_t end)
{
	if (begin >= end || !Active())
		return;

	sequence_.clear();
	sequence_.reserve(end - begin);
	for (std::size_t at = begin; at < end; ++at)
	{
		PPToken token = tokens[at];
		if (token.kind == kPPNewLine)
			token.kind = kPPWhitespace;
		sequence_.push_back(std::move(token));
	}

	expanded_.clear();
	expander_.Expand(sequence_, expanded_);
	ExecutePragmaOperators(expanded_);

	for (std::size_t at = 0; at < expanded_.size(); ++at)
	{
		const PPToken& token = expanded_[at];
		if (IsWhitespaceKind(token.kind) || token.kind == kPPPlacemarker)
			continue;
		sink_.EmitToken(token);
	}
}

void Preprocessor::ExecutePragmaOperators(std::vector<PPToken>& tokens)
{
	std::size_t write = 0;
	std::size_t at = 0;
	const std::size_t count = tokens.size();
	while (at < count)
	{
		const PPToken& token = tokens[at];
		if (token.kind != kPPIdentifier || token.spelling != "_Pragma")
		{
			tokens[write] = token;
			++write;
			++at;
			continue;
		}

		// `_Pragma ( string-literal )`, recognized only here, after every
		// macro has been replaced, and removed once it has run.
		std::size_t p = at + 1;
		while (p < count && tokens[p].kind == kPPWhitespace)
			++p;
		if (p >= count || !PPTokenIsPunctuator(tokens[p], "("))
			throw PreprocessError("malformed _Pragma operator");
		++p;
		while (p < count && tokens[p].kind == kPPWhitespace)
			++p;
		if (p >= count || tokens[p].kind != kPPStringLiteral)
			throw PreprocessError("malformed _Pragma operator");
		std::string text;
		if (!DecodeOrdinaryStringLiteral(tokens[p].spelling, text))
			throw PreprocessError("malformed _Pragma operator");
		++p;
		while (p < count && tokens[p].kind == kPPWhitespace)
			++p;
		if (p >= count || !PPTokenIsPunctuator(tokens[p], ")"))
			throw PreprocessError("malformed _Pragma operator");
		++p;

		ExecutePragma(text);
		at = p;
	}
	tokens.resize(write);
}

std::size_t Preprocessor::ResolveDefined(const std::vector<PPToken>& tokens, std::size_t at,
                                         std::size_t end, std::vector<PPToken>& out)
{
	++at;
	bool parenthesized = false;
	SkipLineWhitespace(tokens, at, end);
	if (at < end && PPTokenIsPunctuator(tokens[at], "("))
	{
		parenthesized = true;
		++at;
		SkipLineWhitespace(tokens, at, end);
	}
	// 16.1's `defined` takes an `identifier`; the course's table lets a keyword
	// spelled as an operator word - `and`, `or_eq` and their neighbours - stand
	// as one, which is what `200-defined-identifier-like-operator` checks.
	if (at >= end || !PPTokenIsIdentifierLike(tokens[at]))
		throw PreprocessError("invalid `defined` operator");

	const PPToken& named = tokens[at];
	const bool defined = macros_.Find(named.spelling) != nullptr;
	++at;

	if (parenthesized)
	{
		SkipLineWhitespace(tokens, at, end);
		if (at >= end || !PPTokenIsPunctuator(tokens[at], ")"))
			throw PreprocessError("invalid `defined` operator");
		++at;
	}

	out.push_back(NumberToken(defined ? "1" : "0", named));
	return at;
}

std::size_t Preprocessor::ResolveHasCppAttribute(const std::vector<PPToken>& tokens,
                                                 std::size_t at, std::size_t end,
                                                 std::vector<PPToken>& out)
{
	++at;
	SkipLineWhitespace(tokens, at, end);
	if (at >= end || !PPTokenIsPunctuator(tokens[at], "("))
		throw PreprocessError("invalid `__has_cpp_attribute` operator");
	const PPToken& head = tokens[at];
	++at;

	std::string name;
	while (at < end && !PPTokenIsPunctuator(tokens[at], ")"))
	{
		if (tokens[at].kind != kPPWhitespace)
			name += tokens[at].spelling;
		++at;
	}
	if (at >= end)
		throw PreprocessError("invalid `__has_cpp_attribute` operator");
	++at;

	// The course's one attribute.  The macro supports both spellings of it, and
	// reports the C++17 value; every other attribute is unknown here.
	const bool present = name == "no_unique_address" || name == "__no_unique_address__";
	out.push_back(NumberToken(present ? "201603" : "0", head));
	return at;
}

bool Preprocessor::EvaluateControllingExpression(const std::vector<PPToken>& tokens,
                                                 std::size_t at, std::size_t end)
{
	// `defined` and `__has_cpp_attribute` are operators of the expression, and
	// their operands are not macro-replaced, so they are resolved before the
	// line is macro-replaced.
	prepared_.clear();
	std::size_t p = at;
	while (p < end)
	{
		const PPToken& token = tokens[p];
		if (token.kind == kPPIdentifier && token.spelling == "defined")
		{
			p = ResolveDefined(tokens, p, end, prepared_);
			continue;
		}
		if (token.kind == kPPIdentifier && token.spelling == "__has_cpp_attribute")
		{
			p = ResolveHasCppAttribute(tokens, p, end, prepared_);
			continue;
		}
		prepared_.push_back(token);
		++p;
	}

	expanded_.clear();
	expander_.Expand(prepared_, expanded_);
	for (std::size_t index = 0; index < expanded_.size(); ++index)
		FeedCtrlToken(ctrl_post_, expanded_[index]);
	ctrl_post_.FinishGroup();

	CtrlExprValue value;
	const bool valid = ctrl_sink_.Result(value);
	ctrl_sink_.EndOfLine();
	ctrl_sink_.Flush();
	if (!valid)
		throw PreprocessError("invalid controlling expression");
	return value.bits != 0;
}

} // namespace preprocess
} // namespace cppgm