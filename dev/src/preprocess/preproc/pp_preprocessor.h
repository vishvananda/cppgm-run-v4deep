// Translation phases 4 to 7: the directive stream and the macro state.
//
// `Preprocessor` divides a translated source into preprocessing directives and
// text-sequences (the prefix `start-of-file #` / `new-line #` of `macros.md`),
// carries the macro state the directives change, and macro-replaces every
// text-sequence before handing its tokens to the sink.
//
// State is local to a translation unit.  A primary source starts from an empty
// macro table, an empty `#pragma once` set and a fresh `__COUNTER__`, and the
// headers it includes share that state and its `__FILE__`; two primary sources
// share nothing.  An included file gets a frame of its own, which is what makes
// `#line`, `__FILE__` and conditional nesting belong to the file that wrote
// them, and what restores the including file's `__FILE__` on return.

#pragma once

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <set>
#include <streambuf>
#include <string>
#include <vector>

#include "posttoken/post_token_stream.h"
#include "preprocess/ctrl_expr/ctrl_expr_stream.h"
#include "preprocess/preproc/pp_expander.h"
#include "preprocess/preproc/pp_file_identity.h"
#include "preprocess/preproc/pp_macro.h"
#include "preprocess/preproc/pp_token.h"

namespace cppgm
{
namespace preprocess
{

// The preprocessed tokens of a translation unit, one text-sequence token at a
// time.  White space and the `##` operator's placemarkers are not reported:
// neither survives phase 6, so neither reaches the post-token pass.
class IPPTextSink
{
public:
	virtual void EmitToken(const PPToken& token) = 0;

	virtual ~IPPTextSink() {}
};

class Preprocessor : public IMacroBuiltins
{
public:
	// `build_date` and `build_time` are the `asctime` fields `__DATE__` and
	// `__TIME__` report, computed once for the run.
	Preprocessor(IPPTextSink& sink, const std::string& build_date,
	             const std::string& build_time);

	// Preprocesses one primary source, resetting the macro, conditional and
	// `#pragma once` state first.
	void ProcessPrimarySource(const std::string& path);

	void ExpandBuiltinMacro(EPPBuiltinMacro which, const PPToken& head,
	                        std::vector<PPToken>& out) override;

private:
	// One `#if` group in progress.
	struct Conditional
	{
		bool parent_active;
		bool active;
		bool seen_true;
		bool seen_else;
	};

	// One file in progress: the identity a token of it carries, the line
	// mapping `#line` changes, the presumed `__FILE__`, and its own conditional
	// nesting.
	struct FileState
	{
		std::uint32_t file;
		long line_delta;
		std::string presumed_file;
		std::vector<Conditional> conditionals;
	};

	void DefinePredefinedMacros();
	void DefineBuiltin(const char* name, const char* body, EPPBuiltinMacro builtin);

	void ProcessFile(const std::string& path);
	void ProcessTokens(const std::vector<PPToken>& tokens);
	std::size_t HandleDirective(const std::vector<PPToken>& tokens, std::size_t hash);

	void HandleConditional(const std::string& name, const std::vector<PPToken>& tokens,
	                       std::size_t at, std::size_t end);
	void HandleDefine(const std::vector<PPToken>& tokens, std::size_t at, std::size_t end);
	void HandleUndef(const std::vector<PPToken>& tokens, std::size_t at, std::size_t end);
	void HandleInclude(const std::vector<PPToken>& tokens, std::size_t at, std::size_t end);
	void HandleLine(const std::vector<PPToken>& tokens, std::size_t at, std::size_t end);
	void HandlePragma(const std::vector<PPToken>& tokens, std::size_t at, std::size_t end);

	void HandleTextSequence(const std::vector<PPToken>& tokens, std::size_t begin,
	                        std::size_t end);
	void ExecutePragmaOperators(std::vector<PPToken>& tokens);

	// The tokens a `#if` or `#elif` line is evaluated from: the line's own
	// tokens with `defined` and `__has_cpp_attribute` resolved, then macro
	// replaced.
	bool EvaluateControllingExpression(const std::vector<PPToken>& tokens, std::size_t at,
	                                   std::size_t end);
	std::size_t ResolveDefined(const std::vector<PPToken>& tokens, std::size_t at,
	                           std::size_t end, std::vector<PPToken>& out);
	std::size_t ResolveHasCppAttribute(const std::vector<PPToken>& tokens, std::size_t at,
	                                   std::size_t end, std::vector<PPToken>& out);

	bool Active() const
	{
		const std::vector<Conditional>& nesting = stack_.back().conditionals;
		return nesting.empty() ? true : nesting.back().active;
	}

	FileState& Current() { return stack_.back(); }

	std::vector<PPToken> ExpandLine(const std::vector<PPToken>& tokens, std::size_t at,
	                                std::size_t end);

	void ExecutePragma(const std::string& text);
	void ApplyPragmaOnce();

	IPPTextSink& sink_;
	MacroTable macros_;
	MacroExpander expander_;

	// `#if` reads the evaluator's value, not its text view, so the view is
	// written to a buffer that discards it.
	class NullBuffer : public std::streambuf
	{
	protected:
		int overflow(int character) override { return character; }
	};

	NullBuffer null_buffer_;
	std::ostream null_stream_;
	CtrlExprSink ctrl_sink_;
	posttoken::PostTokenStream ctrl_post_;

	std::vector<std::string> files_;
	std::vector<FileState> stack_;
	std::set<PreprocessorFileId> pragma_once_;
	std::vector<PPToken> line_;
	std::vector<PPToken> sequence_;
	std::vector<PPToken> expanded_;
	std::vector<PPToken> prepared_;
	unsigned long long counter_;
	std::string build_date_;
	std::string build_time_;
};

// The characters of an ordinary string-literal's spelling, with the prefix and
// the delimiters removed and the simple escape sequences decoded.  Returns
// false for a spelling that is not an ordinary string-literal.
bool DecodeOrdinaryStringLiteral(const std::string& spelling, std::string& out);

} // namespace preprocess
} // namespace cppgm