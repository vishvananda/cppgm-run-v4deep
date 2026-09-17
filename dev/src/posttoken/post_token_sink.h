// The typed token stream a post-token pass produces.
//
// `posttoken` prints these facts in the PA2 text format, and later stages
// consume the same records without the format.  Keeping the format out of the
// post-token pass is what stops a tool's output requirement from forcing text
// through the compiler.

#pragma once

#include <cstddef>
#include <string>

#include "posttoken/fundamental_type.h"
#include "posttoken/simple_token.h"

namespace cppgm
{
namespace posttoken
{

struct IPostTokenSink
{
	virtual void EmitInvalid(const std::string& source) = 0;
	virtual void EmitSimple(const std::string& source, ETokenType type) = 0;
	virtual void EmitIdentifier(const std::string& source) = 0;
	virtual void EmitLiteral(const std::string& source, EFundamentalType type,
	                         const std::string& bytes) = 0;
	virtual void EmitLiteralArray(const std::string& source, std::size_t count,
	                              EFundamentalType type, const std::string& bytes) = 0;
	virtual void EmitUserDefinedCharacter(const std::string& source, const std::string& suffix,
	                                      EFundamentalType type, const std::string& bytes) = 0;
	virtual void EmitUserDefinedStringArray(const std::string& source, const std::string& suffix,
	                                        std::size_t count, EFundamentalType type,
	                                        const std::string& bytes) = 0;
	virtual void EmitUserDefinedInteger(const std::string& source, const std::string& suffix,
	                                    const std::string& prefix) = 0;
	virtual void EmitUserDefinedFloating(const std::string& source, const std::string& suffix,
	                                     const std::string& prefix) = 0;
	virtual void EmitEof() = 0;

	virtual ~IPostTokenSink() {}
};

} // namespace posttoken
} // namespace cppgm