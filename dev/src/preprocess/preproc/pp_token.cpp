#include "preprocess/preproc/pp_token.h"

namespace cppgm
{
namespace preprocess
{

PPMacroPaint PPPaintArena::Add(PPMacroPaint paint, std::uint32_t id)
{
	if (paint != nullptr && id >= paint->low && id <= paint->high)
	{
		for (const PPMacroPaintNode* node = paint; node != nullptr; node = node->parent)
		{
			if (node->id == id)
				return paint;
		}
	}

	PPMacroPaintNode grown;
	grown.parent = paint;
	grown.id = id;
	grown.size = (paint != nullptr ? paint->size : 0) + 1;
	grown.low = paint != nullptr ? (id < paint->low ? id : paint->low) : id;
	grown.high = paint != nullptr ? (id > paint->high ? id : paint->high) : id;
	nodes_.push_back(grown);
	return &nodes_.back();
}

PPMacroPaint PPPaintArena::Union(PPMacroPaint left, PPMacroPaint right)
{
	if (left == nullptr)
		return right;
	if (right == nullptr)
		return left;
	if (left->size > right->size)
		return Union(right, left);

	// `left` is the shorter list.  Every name it carries is already in `right`
	// in the common case - an argument substituted into its own invocation
	// carries that invocation's paint - so the usual cost is one walk per name
	// and no allocation at all.
	PPMacroPaint result = right;
	for (const PPMacroPaintNode* node = left; node != nullptr; node = node->parent)
	{
		bool found = false;
		for (const PPMacroPaintNode* other = result; other != nullptr; other = other->parent)
		{
			if (other->id == node->id)
			{
				found = true;
				break;
			}
		}
		if (!found)
			result = Add(result, node->id);
	}
	return result;
}

} // namespace preprocess
} // namespace cppgm