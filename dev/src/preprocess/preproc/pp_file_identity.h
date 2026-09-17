// File identity for `#pragma once`.
//
// `directives.md` fixes the course rule: two path spellings denote the same
// header when the host reports the same device and inode, which is what makes
// the several spellings `800-pragma-once` reaches one file through share one
// once state.  The scaffold supplied this helper; it lives here so the
// preprocessor owns the identity it keys on instead of reaching back into the
// tool's translation unit.

#pragma once

#include <string>
#include <utility>

namespace cppgm
{
namespace preprocess
{

typedef std::pair<unsigned long long, unsigned long long> PreprocessorFileId;

// Reports the host's identity for `path`.  Returns false when the path cannot
// be inspected, which is also how a missing include file is noticed.
bool GetPreprocessorFileId(const std::string& path, PreprocessorFileId& fileid);

} // namespace preprocess
} // namespace cppgm