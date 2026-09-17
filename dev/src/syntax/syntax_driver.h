// The `--emit-ast` driver: phases 1-7 for each primary source, the PA5 parse,
// and the deterministic dump.

#pragma once

#include <string>
#include <vector>

#include "syntax/syntax_arena.h"
#include "syntax/syntax_token.h"

namespace cppgm
{
namespace syntax
{

// One translation unit's frontend output: the tokens' shared spellings, the
// literals' decoded facts, the parsed tree and its root.  A later mode reads
// the tree through the arena rather than re-parsing the dump, so the two
// stages share this one record.
struct ParsedSource
{
	SyntaxSpellingPool spellings;
	std::vector<SyntaxLiteralFacts> literals;
	SyntaxArena arena;
	int root;

	ParsedSource()
		: root(-1)
	{}
};

// `__DATE__` and `__TIME__`, taken once per run so every source sees the same
// stamp.
void BuildStamp(std::string& date, std::string& time);

// Phases 1-7 and the PA5 parse for one primary source.  Throws on a
// preprocessing or parse failure.
void ParseSource(const std::string& source, const std::string& build_date,
                 const std::string& build_time, ParsedSource& out);

// Writes `translation units`' count and one `start`/`end` framed dump per
// source, in command-line order.  Throws on a preprocessing or parse failure.
void EmitAst(const std::vector<std::string>& sources, const std::string& outfile);

}  // namespace syntax
}  // namespace cppgm
