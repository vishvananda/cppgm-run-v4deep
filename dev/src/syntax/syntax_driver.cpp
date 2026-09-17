#include "syntax/syntax_driver.h"

#include <cstring>
#include <ctime>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "posttoken/post_token_stream.h"
#include "preprocess/preproc/pp_preprocessor.h"
#include "syntax/syntax_arena.h"
#include "syntax/syntax_parser.h"
#include "syntax/syntax_token.h"

using namespace std;

namespace cppgm
{
namespace syntax
{

namespace
{

using preprocess::IPPTextSink;
using preprocess::Preprocessor;
using preprocess::PPToken;

// Phase 7's records, collected into the vector the parser consumes.  A parser
// needs random access and unlimited lookahead, which is the one place in the
// frontend a token vector is the right shape.  The vector holds spelling ids,
// so the translation unit's text is stored once in the pool beside it.
class SyntaxTokenSink : public posttoken::IPostTokenSink
{
public:
	explicit SyntaxTokenSink(SyntaxSpellingPool& spellings)
		: spellings_(spellings)
	{}

	void EmitInvalid(const string& source) override
	{
		(void)source;
		invalid_ = true;
	}

	void EmitSimple(const string& source, posttoken::ETokenType type) override
	{
		Push(source, type, kNoLiteralFacts);
	}

	void EmitIdentifier(const string& source) override
	{
		Push(source, kIdentifierToken, kNoLiteralFacts);
	}

	void EmitLiteral(const string& source, posttoken::EFundamentalType type,
	                 const string& bytes) override
	{
		PushLiteral(source, 1, type, bytes);
	}

	void EmitLiteralArray(const string& source, size_t count,
	                      posttoken::EFundamentalType type, const string& bytes) override
	{
		PushLiteral(source, count, type, bytes);
	}

	void EmitUserDefinedCharacter(const string& source, const string& suffix,
	                              posttoken::EFundamentalType type,
	                              const string& bytes) override
	{
		(void)suffix;
		EmitLiteral(source, type, bytes);
	}

	void EmitUserDefinedStringArray(const string& source, const string& suffix,
	                                size_t count, posttoken::EFundamentalType type,
	                                const string& bytes) override
	{
		(void)suffix;
		EmitLiteralArray(source, count, type, bytes);
	}

	void EmitUserDefinedInteger(const string& source, const string& suffix,
	                            const string& prefix) override
	{
		(void)suffix;
		(void)prefix;
		// A user-defined integer literal's type is its literal operator's, so
		// the token keeps its spelling and no fundamental type.
		Push(source, kLiteralToken, kNoLiteralFacts);
	}

	void EmitUserDefinedFloating(const string& source, const string& suffix,
	                             const string& prefix) override
	{
		(void)suffix;
		(void)prefix;
		Push(source, kLiteralToken, kNoLiteralFacts);
	}

	void EmitEof() override
	{
		Push("", kEofToken, kNoLiteralFacts);
	}

	bool invalid() const
	{
		return invalid_;
	}

	const vector<SyntaxToken>& tokens() const
	{
		return tokens_;
	}

	const vector<SyntaxLiteralFacts>& literals() const
	{
		return literals_;
	}

private:
	void Push(const string& source, int kind, int literal)
	{
		SyntaxToken token;
		token.kind = kind;
		token.spelling = spellings_.Intern(source);
		token.literal = literal;
		tokens_.push_back(token);
	}

	void PushLiteral(const string& source, size_t count,
	                 posttoken::EFundamentalType type, const string& bytes)
	{
		SyntaxLiteralFacts facts;
		facts.fundamental_type = type;
		facts.count = count;
		facts.chars = bytes;
		literals_.push_back(facts);
		Push(source, kLiteralToken, static_cast<int>(literals_.size()) - 1);
	}

	SyntaxSpellingPool& spellings_;
	vector<SyntaxToken> tokens_;
	vector<SyntaxLiteralFacts> literals_;
	bool invalid_ = false;
};

// The preprocessor's finalized tokens handed to the post-token pass.
class PostTokenBridge : public IPPTextSink
{
public:
	explicit PostTokenBridge(posttoken::PostTokenStream& post)
		: post_(post)
	{}

	void EmitToken(const PPToken& token) override
	{
		switch(token.kind)
		{
		case preprocess::kPPIdentifier:
			post_.emit_identifier(token.spelling);
			return;
		case preprocess::kPPNumber:
			post_.emit_pp_number(token.spelling);
			return;
		case preprocess::kPPCharacterLiteral:
			post_.emit_character_literal(token.spelling);
			return;
		case preprocess::kPPUserDefinedCharacterLiteral:
			post_.emit_user_defined_character_literal(token.spelling);
			return;
		case preprocess::kPPStringLiteral:
			post_.emit_string_literal(token.spelling);
			return;
		case preprocess::kPPUserDefinedStringLiteral:
			post_.emit_user_defined_string_literal(token.spelling);
			return;
		case preprocess::kPPHeaderName:
			post_.emit_header_name(token.spelling);
			return;
		case preprocess::kPPOpOrPunc:
			post_.emit_preprocessing_op_or_punc(token.spelling);
			return;
		case preprocess::kPPNonWhitespaceChar:
			post_.emit_non_whitespace_char(token.spelling);
			return;
		default:
			return;
		}
	}

private:
	posttoken::PostTokenStream& post_;
};

}  // namespace

// `__DATE__` and `__TIME__`, taken once so every source of the run sees the
// same stamp.
void BuildStamp(string& date, string& time)
{
	const time_t now = ::time(nullptr);
	const char* stamp = asctime(localtime(&now));
	if(stamp == nullptr || strlen(stamp) < 24)
	{
		throw runtime_error("cannot read the build time");
	}
	date.assign(stamp + 4, 3);
	date.append(stamp + 7, 3);
	date.append(stamp + 19, 5);
	time.assign(stamp + 11, 8);
}

void ParseSource(const string& source, const string& build_date, const string& build_time,
                 ParsedSource& out)
{
	SyntaxTokenSink sink(out.spellings);
	posttoken::PostTokenStream tokens(sink);
	PostTokenBridge bridge(tokens);
	Preprocessor preprocessor(bridge, build_date, build_time);

	preprocessor.ProcessPrimarySource(source);
	tokens.emit_eof();

	if(sink.invalid())
	{
		throw runtime_error("invalid preprocessing-token");
	}
	out.literals = sink.literals();
	out.root = ParseTranslationUnit(sink.tokens(), out.spellings, out.arena);
}

void EmitAst(const vector<string>& sources, const string& outfile)
{
	ofstream out(outfile.c_str());
	if(!out)
	{
		throw runtime_error("cannot open the output file");
	}

	string build_date;
	string build_time;
	BuildStamp(build_date, build_time);

	out << sources.size() << " translation units\n";

	for(size_t index = 0; index < sources.size(); ++index)
	{
		ParsedSource parsed;
		ParseSource(sources[index], build_date, build_time, parsed);
		out << "start translation unit " << (index + 1) << '\n';
		parsed.arena.Write(out, parsed.root);
		out << "end translation unit\n";
	}
	out.flush();
	if(!out)
	{
		throw runtime_error("cannot write the output file");
	}
}

}  // namespace syntax
}  // namespace cppgm
