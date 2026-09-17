// The `--emit-types` driver: phases 1-7, the PA5 parse, the PA6 semantic
// analysis, and the deterministic scope/type dump.

#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace cppgm
{
namespace semantic
{

class Model;

// Writes `translation units`' count and one `start`/`end` framed scope tree per
// source, in command-line order.  Each translation unit is analysed on its own,
// so a declaration in one source never makes a name visible in another.  Throws
// on a preprocessing, parse or semantic failure.
void EmitTypes(const std::vector<std::string>& sources, const std::string& outfile);

// The body of one translation unit's dump, rooted at `translation-unit`.
void WriteScopeTree(std::ostream& out, const Model& model);

}  // namespace semantic
}  // namespace cppgm
