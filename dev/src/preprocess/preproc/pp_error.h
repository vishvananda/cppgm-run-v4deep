// The rejection of a translation unit by phases 4 to 7.
//
// Phase 1-3 rejections are `SourceError`; this one carries the same weight for
// the directive, macro and token rules: a malformed directive, an invalid
// macro definition, an unresolvable include, an active `#error`, an
// erroneous controlling expression, an invalid token paste, and a
// preprocessing-token that phase 7 cannot classify.

#pragma once

#include <stdexcept>
#include <string>

namespace cppgm
{
namespace preprocess
{

class PreprocessError : public std::runtime_error
{
public:
	explicit PreprocessError(const std::string& message)
		: std::runtime_error(message)
	{}
};

} // namespace preprocess
} // namespace cppgm