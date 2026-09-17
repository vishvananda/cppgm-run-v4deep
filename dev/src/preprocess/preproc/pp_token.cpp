#include "preprocess/preproc/pp_token.h"

namespace cppgm
{
namespace preprocess
{

PPMacroPaint PPMacroPaintAdd(const PPMacroPaint& paint, std::uint32_t id)
{
	if (paint.get() != nullptr && id >= paint->low && id <= paint->high)
	{
		for (const PPMacroPaintNode* node = paint.get(); node != nullptr;
		     node = node->parent.get())
		{
			if (node->id == id)
				return paint;
		}
	}

	std::shared_ptr<PPMacroPaintNode> grown(new PPMacroPaintNode);
	grown->parent = paint;
	grown->id = id;
	grown->size = (paint ? paint->size : 0) + 1;
	grown->low = paint ? (id < paint->low ? id : paint->low) : id;
	grown->high = paint ? (id > paint->high ? id : paint->high) : id;
	return grown;
}

PPMacroPaint PPMacroPaintUnion(const PPMacroPaint& left, const PPMacroPaint& right)
{
	if (!left)
		return right;
	if (!right)
		return left;
	if (left->size > right->size)
		return PPMacroPaintUnion(right, left);

	// `left` is the shorter list.  Every name it carries is already in `right`
	// in the common case - an argument substituted into its own invocation
	// carries that invocation's paint - so the usual cost is one walk per name
	// and no allocation at all.
	PPMacroPaint result = right;
	for (const PPMacroPaintNode* node = left.get(); node != nullptr;
	     node = node->parent.get())
	{
		bool found = false;
		for (const PPMacroPaintNode* other = result.get(); other != nullptr;
		     other = other->parent.get())
		{
			if (other->id == node->id)
			{
				found = true;
				break;
			}
		}
		if (!found)
			result = PPMacroPaintAdd(result, node->id);
	}
	return result;
}

} // namespace preprocess
} // namespace cppgm