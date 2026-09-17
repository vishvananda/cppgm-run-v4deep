#include "syntax/syntax_token.h"

using namespace std;

namespace cppgm
{
namespace syntax
{

SyntaxSpellingPool::SyntaxSpellingPool()
	: greater_(0)
{
	// Slot 0 is the empty spelling, so a token with no text and a token whose
	// text is empty are one value and a spelling is never a special case.
	texts_.push_back(string());
	greater_ = Intern(">");
}

int SyntaxSpellingPool::Intern(const string& text)
{
	if(text.empty())
	{
		return 0;
	}
	unordered_map<string, int>::const_iterator found = ids_.find(text);
	if(found != ids_.end())
	{
		return found->second;
	}
	const int id = static_cast<int>(texts_.size());
	texts_.push_back(text);
	ids_.insert(make_pair(text, id));
	return id;
}

}  // namespace syntax
}  // namespace cppgm
