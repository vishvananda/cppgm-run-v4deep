// The `--emit-ast` driver: phases 1-7 for each primary source, the PA5 parse,
// and the deterministic dump.

#pragma once

#include <string>
#include <vector>

namespace cppgm
{
namespace syntax
{

// Writes `translation units`' count and one `start`/`end` framed dump per
// source, in command-line order.  Throws on a preprocessing or parse failure.
void EmitAst(const std::vector<std::string>& sources, const std::string& outfile);

}  // namespace syntax
}  // namespace cppgm
