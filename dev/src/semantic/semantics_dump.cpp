// The deterministic resolved-tree dump.
//
// One line per node, indented two spaces per level.  A declaration prints its
// name and then its type; an expression prints its value category, its type and
// then whatever trailing label the operator carried.  Nothing else about a node
// is printed, so a node the builder did not create is a line the dump does not
// have.

#include <ostream>
#include <string>

#include "semantic/semantics_tree.h"

using namespace std;

namespace cppgm
{
namespace semantic
{

namespace
{

void WriteNode(ostream& out, const SemTree& tree, int node, int depth)
{
	for(int index = 0; index < depth; ++index)
	{
		out << "  ";
	}
	const SemNode& record = tree.Node(node);
	out << record.tag;
	if(record.label_first)
	{
		// A declaration's name is a field even when it is empty, so an unnamed
		// parameter still prints the space it would have occupied.
		if(!record.label.empty() || !record.type.empty())
		{
			out << ' ' << record.label;
		}
		if(!record.type.empty())
		{
			out << ' ' << record.type;
		}
	}
	else
	{
		if(!record.category.empty())
		{
			out << ' ' << record.category;
		}
		if(!record.type.empty())
		{
			out << ' ' << record.type;
		}
		if(!record.label.empty())
		{
			out << ' ' << record.label;
		}
	}
	out << '\n';
	for(size_t index = 0; index < record.children.size(); ++index)
	{
		WriteNode(out, tree, record.children[index], depth + 1);
	}
}

}  // namespace

void WriteSemanticsTree(ostream& out, const SemTree& tree, int root)
{
	WriteNode(out, tree, root, 0);
}

}  // namespace semantic
}  // namespace cppgm
