// The structured syntax tree the parser builds and the dump prints.
//
// A node is a tag name, an optional inline label and its children.  Tags and
// labels are interned, so a node is four integers and a tree never copies a
// string; the dump is a preorder walk that prints the tag, then the label when
// there is one.
//
// Ownership is flat and bulk: nodes live in one vector that grows by index, and
// their children are edges in a second vector that grows the same way.  A node
// holds the first and last edge of its child list, and an edge names its child,
// its parent and the edge the parent held before it, so appending a child is
// one push and no node allocates per child.  Later stages read the tree through
// the accessors rather than re-parsing the dump.

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
	int first_child;  // edge index, or kNoSyntaxNode
	int last_child;   // edge index, or kNoSyntaxNode
	// The token range the node covers, and the literal facts a literal token
	// carries.  Later stages need both: an anonymous type is named from the
	// range it spans, and a constant expression reads a literal's value rather
	// than re-parsing its spelling.
	int start;
	int end;
	int literal;

	SyntaxNode()
		: tag(-1)
		, label(-1)
		, first_child(kNoSyntaxNode)
		, last_child(kNoSyntaxNode)
		, start(-1)
		, end(-1)
		, literal(-1)
	{}
};

// One child of one node.  The edges of a parent form a chain that runs
// backwards: `last_child` is the child appended most recently and `previous`
// is the one appended before it, so the chain read from `last_child` gives the
// children in reverse.  An edge carries its parent and the edge that preceded
// it, which is what restoring the parent after a dropped subtree needs: an edge
// says by itself how to undo itself, with no separate journal.
struct SyntaxEdge
{
	int child;
	int parent;
	int previous;
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

	// A position in the arena's life: the counts every later node and edge is
	// beyond.  A speculative alternative takes one and drops back to it.
	struct Mark
	{
		std::size_t nodes;
		std::size_t edges;
	};

	Mark Take() const
	{
		Mark mark;
		mark.nodes = nodes_.size();
		mark.edges = edges_.size();
		return mark;
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

	// Drops every node and edge created after `mark`, which is how a
	// speculative alternative releases the subtree it built before it failed.
	void Drop(const Mark& mark);

	// Renames a node, for the abstract declarator that prints under the name
	// its context gives it rather than the one its production has.
	void PutTag(int node, const char* tag)
	{
		nodes_[static_cast<std::size_t>(node)].tag = Intern(tag);
	}

	// The token range a node covers.  `start` is set where the node is made;
	// `end` is set where a production finishes a construct whose extent is only
	// known at the end, such as a class or enum body.
	void SetStart(int node, std::size_t position)
	{
		nodes_[static_cast<std::size_t>(node)].start = static_cast<int>(position);
	}

	void SetEnd(int node, std::size_t position)
	{
		nodes_[static_cast<std::size_t>(node)].end = static_cast<int>(position);
	}

	void SetLiteral(int node, int literal)
	{
		nodes_[static_cast<std::size_t>(node)].literal = literal;
	}

	int Start(int node) const
	{
		return nodes_[static_cast<std::size_t>(node)].start;
	}

	int End(int node) const
	{
		return nodes_[static_cast<std::size_t>(node)].end;
	}

	int Literal(int node) const
	{
		return nodes_[static_cast<std::size_t>(node)].literal;
	}

	void AddChild(int parent, int child);

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

	std::size_t ChildCount(int node) const
	{
		std::size_t count = 0;
		for(int edge = nodes_[static_cast<std::size_t>(node)].last_child;
		    edge != kNoSyntaxNode; edge = edges_[static_cast<std::size_t>(edge)].previous)
		{
			++count;
		}
		return count;
	}

	// Every child of `node`, in order, in one pass over the chain.  The chain
	// runs backwards, so walking to the `index`-th child costs a count and a
	// walk; a caller that wants all of them pays for both once here instead.
	void CollectChildren(int node, std::vector<int>& out) const
	{
		const std::size_t count = ChildCount(node);
		out.resize(count);
		int edge = nodes_[static_cast<std::size_t>(node)].last_child;
		for(std::size_t remaining = count; remaining > 0; --remaining)
		{
			out[remaining - 1] = edges_[static_cast<std::size_t>(edge)].child;
			edge = edges_[static_cast<std::size_t>(edge)].previous;
		}
	}

	// The `index`-th child of `node`, counted from the first.  The chain runs
	// backwards, so this walks to the child that many places from the end.
	int ChildAt(int node, std::size_t index) const
	{
		const std::size_t count = ChildCount(node);
		if(index >= count)
		{
			return kNoSyntaxNode;
		}
		int edge = nodes_[static_cast<std::size_t>(node)].last_child;
		for(std::size_t remaining = count - 1 - index; remaining > 0; --remaining)
		{
			edge = edges_[static_cast<std::size_t>(edge)].previous;
		}
		return edges_[static_cast<std::size_t>(edge)].child;
	}

	// The dump: one line per node, `2 * depth` spaces of indent, the tag, then
	// the label when the node has one.
	void Write(std::ostream& out, int root) const;

private:
	std::vector<SyntaxNode> nodes_;
	std::vector<SyntaxEdge> edges_;
	std::vector<std::string> text_;
	std::unordered_map<std::string, int> text_ids_;
};

}  // namespace syntax
}  // namespace cppgm