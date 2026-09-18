// The resolved output tree of the PA7 semantics dump.
//
// One node per printed line.  The tree is built rather than printed directly
// because the dump is not the parse: a parenthesized expression disappears, an
// initializer becomes a child of the variable it initializes, a call's
// `argument-list` becomes the call's own children, and a name that denotes an
// injected union member becomes a member access.  Keeping the resolved shape in
// its own tree means none of that mutates the parse.
//
// A node's line is `<tag>`, then - for an expression - the value category and
// the resolved type, and then the trailing label an operator carries; a
// declaration prints its name and its type instead.  `label_first` says which
// of the two orders the node uses.

#pragma once

#include <string>
#include <vector>

namespace cppgm
{
namespace semantic
{

// 5 [expr]: the value category of an expression.
enum EValueCategory
{
	kLvalue = 0,
	kPrvalue,
	kXvalue
};

const char* ValueCategoryName(int category);

struct SemNode
{
	std::string tag;
	std::string category;
	std::string type;
	std::string label;
	bool label_first;
	std::vector<int> children;

	SemNode()
		: label_first(false)
	{}
};

class SemTree
{
public:
	int Add(const std::string& tag)
	{
		SemNode node;
		node.tag = tag;
		nodes_.push_back(node);
		return static_cast<int>(nodes_.size()) - 1;
	}

	int Add(const std::string& tag, const std::string& label, bool label_first)
	{
		SemNode node;
		node.tag = tag;
		node.label = label;
		node.label_first = label_first;
		nodes_.push_back(node);
		return static_cast<int>(nodes_.size()) - 1;
	}

	int Add(const std::string& tag, int category, const std::string& type)
	{
		SemNode node;
		node.tag = tag;
		node.category = ValueCategoryName(category);
		node.type = type;
		nodes_.push_back(node);
		return static_cast<int>(nodes_.size()) - 1;
	}

	int Add(const std::string& tag, int category, const std::string& type,
	        const std::string& label)
	{
		SemNode node;
		node.tag = tag;
		node.category = ValueCategoryName(category);
		node.type = type;
		node.label = label;
		nodes_.push_back(node);
		return static_cast<int>(nodes_.size()) - 1;
	}

	void AddChild(int parent, int child)
	{
		if(parent >= 0 && child >= 0)
		{
			nodes_[static_cast<std::size_t>(parent)].children.push_back(child);
		}
	}

	void SetLabel(int node, const std::string& label)
	{
		nodes_[static_cast<std::size_t>(node)].label = label;
	}

	void SetType(int node, const std::string& type)
	{
		nodes_[static_cast<std::size_t>(node)].type = type;
	}

	void SetCategory(int node, int category)
	{
		nodes_[static_cast<std::size_t>(node)].category = ValueCategoryName(category);
	}

	const SemNode& Node(int index) const
	{
		return nodes_[static_cast<std::size_t>(index)];
	}

	std::size_t Size() const
	{
		return nodes_.size();
	}

private:
	std::vector<SemNode> nodes_;
};

}  // namespace semantic
}  // namespace cppgm
