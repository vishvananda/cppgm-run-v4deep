// The structured syntax tree the parser builds and the dump prints.
//
// A node is a tag name, an optional inline label and an ordered child list.
// Tags and labels are interned, so a node is three integers and a tree never
// copies a string; the dump is a preorder walk that prints the tag, then the
// label when there is one.
//
// Ownership is flat and bulk: nodes live in one vector that grows by index, and
// a child list is a vector of indices.  Later stages read the tree through the
// accessors rather than re-parsing the dump.

#pragma once

#include <cstddef>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace cppgm
{
namespace syntax
{

const int kNoSyntaxNode = -1;

struct SyntaxNode
{
	int tag;
	int label;
	std::vector<int> children;

	SyntaxNode()
		: tag(-1)
		, label(-1)
	{}
};

class SyntaxArena
{
public:
	SyntaxArena()
	{
		// Slot 0 is the empty string, so "no label" and "empty label" are one
		// value and a node's label is never a special case.
		text_.push_back(std::string());
	}

	// Interns `text` and returns its id.  Ids are stable for the arena's life.
	int Intern(const std::string& text);

	const std::string& Text(int id) const
	{
		return text_[static_cast<std::size_t>(id)];
	}

	int Make(const char* tag);
	int Make(const char* tag, const std::string& label);
	int MakeLabel(int tag, int label);

	// Sets a node's inline label after construction, for the productions whose
	// name is only known once the rest of the construct has been read.
	void SetLabel(int node, const std::string& label)
	{
		nodes_[static_cast<std::size_t>(node)].label = Intern(label);
	}

	// Drops every node created after `mark`, which is how a speculative
	// alternative releases the subtree it built before it failed.
	void DropTo(std::size_t mark)
	{
		if(mark < nodes_.size())
		{
			nodes_.resize(mark);
		}
	}

	// Renames a node, for the abstract declarator that prints under the name
	// its context gives it rather than the one its production has.
	void PutTag(int node, const char* tag)
	{
		nodes_[static_cast<std::size_t>(node)].tag = Intern(tag);
	}

	void AddChild(int parent, int child)
	{
		if(parent != kNoSyntaxNode && child != kNoSyntaxNode && parent != child)
		{
			nodes_[static_cast<std::size_t>(parent)].children.push_back(child);
		}
	}

	int Count() const
	{
		return static_cast<int>(nodes_.size());
	}

	// The node made last, for a parent that is created before the children it
	// wraps are known.
	int Last() const
	{
		return static_cast<int>(nodes_.size()) - 1;
	}

	const SyntaxNode& Node(int index) const
	{
		return nodes_[static_cast<std::size_t>(index)];
	}

	// The dump: one line per node, `2 * depth` spaces of indent, the tag, then
	// the label when the node has one.
	void Write(std::ostream& out, int root) const;

	// Nodes and bytes of storage the tree holds, for the stage's budget report.
	std::size_t NodeCount() const
	{
		return nodes_.size();
	}

	std::size_t StorageBytes() const;

private:
	std::vector<SyntaxNode> nodes_;
	std::vector<std::string> text_;
	std::unordered_map<std::string, int> text_ids_;
};

}  // namespace syntax
}  // namespace cppgm
