// Translation phases 4 to 7: the directive stream and the macro state.
//
// `Preprocessor` divides a translated source into preprocessing directives and
// text-sequences (the prefix `start-of-file #` / `new-line #` of `macros.md`),
// carries the macro state the directives change, and macro-replaces every
// text-sequence before handing its tokens to the sink.
//
// The division is done on the tokenizer's own callbacks rather than on a token
// vector: a directive line is accumulated until its new-line and then handled,
// and a text-sequence's tokens are pushed to `MacroExpander` as they arrive.
// Nothing holds the translation unit, so its cost is its source buffer plus the
// directive line or the macro invocation currently open - not one record per
// token it contains.  `ReadPreprocessingTokens` and the per-file vector it
// built are gone with it.
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
#include <deque>
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
#include "preprocess/preproc/pp_token_reader.h"

namespace cppgm
{
namespace preprocess
{

class Preprocessor : public IMacroBuiltins,
                     public IPPTextSink,
                     public IPPTextFeed,
                     public IPPStreamTokenSink
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

	// The expander's side of the text-sequence interface: the placeholder
	// records are dropped, a `_Pragma` operator is collected and run, and
	// everything else is passed on to the consumer.
	void EmitToken(const PPToken& token) override;

	// The phase 3 callbacks, one preprocessing-token at a time.
	void OnPreprocessingToken(PPToken token) override;

	// The text-sequence feed the expander pulls from.
	bool NextTextToken(PPToken& token) override;

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
	// mapping `#line` changes, the presumed `__FILE__`, its own conditional
	// nesting, and where the directive/text-sequence split has got to.  The
	// last two are per file because an include suspends the including file in
	// the middle of both.
	struct FileState
	{
		std::uint32_t file;
		long line_delta;
		std::string presumed_file;
		std::vector<Conditional> conditionals;
		// The tokens of the directive line being accumulated, starting at its
		// `#` and ending at the new-line that terminates it.
		std::vector<PPToken> directive;
		bool line_start;
		bool in_directive;

		FileState()
			: file(0)
			, line_delta(0)
			, line_start(true)
			, in_directive(false)
		{}
	};

	void DefinePredefinedMacros();
	void DefineBuiltin(const char* name, const char* body, EPPBuiltinMacro builtin);

	void ProcessFile(const std::string& path);
	void DispatchDirective();
	// Hands one token of the current text-sequence to the expander, starting
	// the sequence if it is not already open.
	void PushTextToken(PPToken token);
	void EndTextSequence();

	std::size_t HandleDirective(const std::vector<PPToken>& tokens, std::size_t hash);

	void HandleConditional(const std::string& name, const std::vector<PPToken>& tokens,
	                       std::size_t at, std::size_t end);
	void HandleDefine(const std::vector<PPToken>& tokens, std::size_t at, std::size_t end);
	void HandleUndef(const std::vector<PPToken>& tokens, std::size_t at, std::size_t end);
	void HandleInclude(const std::vector<PPToken>& tokens, std::size_t at, std::size_t end);
	void HandleLine(const std::vector<PPToken>& tokens, std::size_t at, std::size_t end);
	void HandlePragma(const std::vector<PPToken>& tokens, std::size_t at, std::size_t end);

	void EmitPragmaToken(const PPToken& token);

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
	// How far into a `_Pragma` operator the stream has got: 0 outside one, 1
	// after `_Pragma`, 2 after `(`, 3 after its string-literal.
	std::size_t pragma_step_;
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
	// A deque, not a vector: an include pushes a frame while the including
	// file's handler still holds a reference to its own.
	std::deque<FileState> stack_;
	std::set<PreprocessorFileId> pragma_once_;
	// The text-sequence tokens the expander has not pulled yet.  The expander
	// is pumped after every push, so this holds the tokens of the construct
	// being read and not the rest of the sequence.
	std::deque<PPToken> text_;
	bool text_active_;
	std::vector<PPToken> line_;
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
