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

void SyntaxArena::AddChild(int parent, int child)
{
	if(parent == kNoSyntaxNode || child == kNoSyntaxNode || parent == child)
	{
		return;
	}
	SyntaxEdge edge;
	edge.child = child;
	edge.parent = parent;
	edge.previous = nodes_[static_cast<size_t>(parent)].last_child;
	edges_.push_back(edge);
	const int index = static_cast<int>(edges_.size()) - 1;
	SyntaxNode& node = nodes_[static_cast<size_t>(parent)];
	node.last_child = index;
	if(node.first_child == kNoSyntaxNode)
	{
		node.first_child = index;
	}
}

void SyntaxArena::Drop(const Mark& mark)
{
	// The edges being dropped restore their parents on the way out: walking
	// them newest first, each one writes the parent's last edge back to the
	// edge that preceded it, so the lowest-indexed dropped edge of a parent
	// writes last and the parent ends as it was before the mark.  A parent
	// that the mark drops is written too, which is harmless - it is gone.
	for(size_t index = edges_.size(); index > mark.edges; --index)
	{
		const SyntaxEdge& edge = edges_[index - 1];
		SyntaxNode& parent = nodes_[static_cast<size_t>(edge.parent)];
		parent.last_child = edge.previous;
		if(edge.previous == kNoSyntaxNode)
		{
			parent.first_child = kNoSyntaxNode;
		}
	}
	edges_.resize(mark.edges);
	nodes_.resize(mark.nodes);
}

void SyntaxArena::Write(ostream& out, int root) const
{
	// A preorder walk with an explicit stack, so a deeply nested tree costs no
	// host stack: the stack holds the nodes still to print, each with the depth
	// its line is indented to.
	struct Entry
	{
		int node;
		size_t depth;
	};
	vector<Entry> stack;

	Entry first;
	first.node = root;
	first.depth = 0;
	stack.push_back(first);

	while(!stack.empty())
	{
		const Entry entry = stack.back();
		stack.pop_back();

		const SyntaxNode& node = nodes_[static_cast<size_t>(entry.node)];
		for(size_t level = 0; level < entry.depth; ++level)
		{
			out << "  ";
		}
		out << Text(node.tag);
		if(node.label != 0)
		{
			out << ' ' << Text(node.label);
		}
		out << '\n';

		// The children are pushed newest first, which puts the child appended
		// first on top of the stack and prints them in the order they were
		// parsed.
		for(int edge = node.last_child; edge != kNoSyntaxNode;
		    edge = edges_[static_cast<size_t>(edge)].previous)
		{
			Entry child;
			child.node = edges_[static_cast<size_t>(edge)].child;
			child.depth = entry.depth + 1;
			stack.push_back(child);
		}
	}
}

}  // namespace syntax
}  // namespace cppgm