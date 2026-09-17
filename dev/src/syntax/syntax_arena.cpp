#include "syntax/syntax_arena.h"

#include <ostream>

using namespace std;

namespace cppgm
{
namespace syntax
{

int SyntaxArena::Intern(const string& text)
{
	if(text.empty())
	{
		return 0;
	}
	unordered_map<string, int>::const_iterator found = text_ids_.find(text);
	if(found != text_ids_.end())
	{
		return found->second;
	}
	const int id = static_cast<int>(text_.size());
	text_.push_back(text);
	text_ids_.insert(make_pair(text, id));
	return id;
}

int SyntaxArena::Make(const char* tag)
{
	SyntaxNode node;
	node.tag = Intern(tag);
	node.label = 0;
	nodes_.push_back(node);
	return static_cast<int>(nodes_.size()) - 1;
}

int SyntaxArena::Make(const char* tag, const string& label)
{
	SyntaxNode node;
	node.tag = Intern(tag);
	node.label = Intern(label);
	nodes_.push_back(node);
	return static_cast<int>(nodes_.size()) - 1;
}

int SyntaxArena::MakeLabel(int tag, int label)
{
	SyntaxNode node;
	node.tag = tag;
	node.label = label;
	nodes_.push_back(node);
	return static_cast<int>(nodes_.size()) - 1;
}

void SyntaxArena::Write(ostream& out, int root) const
{
	// A preorder walk with an explicit stack, so a deeply nested tree costs no
	// host stack.  A node's line is printed when its frame is opened, which is
	// the moment its depth is known.
	struct Frame
	{
		int node;
		size_t child;
	};
	vector<Frame> stack;

	const SyntaxArena* self = this;
	ostream* stream = &out;
	struct Line
	{
		void operator()(const SyntaxArena* arena, ostream* target, int index,
		                size_t depth) const
		{
			const SyntaxNode& node = arena->nodes_[static_cast<size_t>(index)];
			for(size_t level = 0; level < depth; ++level)
			{
				*target << "  ";
			}
			*target << arena->Text(node.tag);
			if(node.label != 0)
			{
				*target << ' ' << arena->Text(node.label);
			}
			*target << '\n';
		}
	};
	Line write_line;

	write_line(self, stream, root, 0);
	Frame first;
	first.node = root;
	first.child = 0;
	stack.push_back(first);

	while(!stack.empty())
	{
		Frame& frame = stack.back();
		const SyntaxNode& node = nodes_[static_cast<size_t>(frame.node)];
		if(frame.child < node.children.size())
		{
			const int child = node.children[frame.child];
			++frame.child;
			write_line(self, stream, child, stack.size());
			Frame next;
			next.node = child;
			next.child = 0;
			stack.push_back(next);
			continue;
		}
		stack.pop_back();
	}
}

size_t SyntaxArena::StorageBytes() const
{
	size_t bytes = nodes_.capacity() * sizeof(SyntaxNode);
	for(size_t index = 0; index < nodes_.size(); ++index)
	{
		bytes += nodes_[index].children.capacity() * sizeof(int);
	}
	for(size_t index = 0; index < text_.size(); ++index)
	{
		bytes += text_[index].capacity() + sizeof(string);
	}
	return bytes;
}

}  // namespace syntax
}  // namespace cppgm
