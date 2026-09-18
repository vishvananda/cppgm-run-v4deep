// PA7's expression layer: the resolved type, value category and selected
// function of every expression the assignment's slice reaches.
//
// An expression is resolved once, in source order, against the scope the
// statement it belongs to was analysed in.  What the dump prints is the
// resolved shape rather than the parse: operands keep their own types, the
// result of an operator is the type the standard gives it, and a call shows
// the function the overload set chose.  Conversions are therefore classified
// here and not written into the tree - the tree is the answer, not the working.

#include "semantic/semantic_analyzer.h"

#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>

#include "posttoken/fundamental_type.h"

using namespace std;

namespace cppgm
{
namespace semantic
{

namespace
{

string AfterColon(const string& text)
{
	const size_t colon = text.find(':');
	return colon == string::npos ? string() : text.substr(colon + 1);
}

// The plain word a terminal node's label carries, for a keyword or punctuator
// written as `KW_CONST:const` or `OP_PLUS:+`.
string OperatorWord(const string& label)
{
	return AfterColon(label);
}

int DigitValue(char c)
{
	if(c >= '0' && c <= '9')
	{
		return c - '0';
	}
	if(c >= 'a' && c <= 'f')
	{
		return c - 'a' + 10;
	}
	if(c >= 'A' && c <= 'F')
	{
		return c - 'A' + 10;
	}
	return -1;
}

// The word a terminal label spells when it carries its token kind, as
// `KW_DOUBLE:double` does.
string KeywordWord(const string& label)
{
	if(label.compare(0, 3, "KW_") != 0)
	{
		return string();
	}
	const size_t colon = label.find(':');
	return colon == string::npos ? string() : label.substr(colon + 1);
}

// Whether a name is a simple-type-specifier's own word, which is how a
// functional cast writes `double(x)`.
bool IsSimpleTypeWord(const string& word)
{
	return word == "void" || word == "bool" || word == "char" || word == "wchar_t" ||
	       word == "char16_t" || word == "char32_t" || word == "int" || word == "short" ||
	       word == "long" || word == "signed" || word == "unsigned" || word == "float" ||
	       word == "double";
}

bool EndsWith(const string& text, const char* suffix)
{
	const size_t length = strlen(suffix);
	return text.size() >= length && text.compare(text.size() - length, length, suffix) == 0;
}

}  // namespace

const char* ValueCategoryName(int category)
{
	switch(category)
	{
	case kLvalue:
		return "lvalue";
	case kXvalue:
		return "xvalue";
	default:
		return "prvalue";
	}
}

// ---------------------------------------------------------------------------
// Type predicates
// ---------------------------------------------------------------------------

bool Analyzer::IsIntegralType(int type) const
{
	if(type < 0)
	{
		return false;
	}
	if(model_.Get(type).kind == kTypeCv)
	{
		type = model_.Get(type).base;
	}
	const Type& record = model_.Get(type);
	if(record.kind == kTypeEnum)
	{
		return true;
	}
	return record.kind == kTypeFundamental &&
	       posttoken::FundamentalTypeIsIntegral(
	           static_cast<posttoken::EFundamentalType>(record.base));
}

bool Analyzer::IsArithmeticType(int type) const
{
	if(type < 0)
	{
		return false;
	}
	if(model_.Get(type).kind == kTypeCv)
	{
		type = model_.Get(type).base;
	}
	const Type& record = model_.Get(type);
	if(record.kind == kTypeEnum)
	{
		return true;
	}
	if(record.kind != kTypeFundamental)
	{
		return false;
	}
	const posttoken::EFundamentalType fundamental =
	    static_cast<posttoken::EFundamentalType>(record.base);
	return fundamental != posttoken::FT_VOID && fundamental != posttoken::FT_NULLPTR_T;
}

bool Analyzer::IsReferenceType(int type) const
{
	if(type < 0)
	{
		return false;
	}
	const ETypeKind kind = model_.Get(type).kind;
	return kind == kTypeLvalueReference || kind == kTypeRvalueReference;
}

int Analyzer::ReferredType(int type) const
{
	if(!IsReferenceType(type))
	{
		return type;
	}
	return model_.Get(type).base;
}

bool Analyzer::IsScalarType(int type) const
{
	if(type < 0)
	{
		return false;
	}
	if(model_.Get(type).kind == kTypeCv)
	{
		type = model_.Get(type).base;
	}
	const Type& record = model_.Get(type);
	if(record.kind == kTypePointer || record.kind == kTypeMemberPointer)
	{
		return true;
	}
	if(record.kind == kTypeEnum)
	{
		return true;
	}
	if(record.kind != kTypeFundamental)
	{
		return false;
	}
	return record.base != posttoken::FT_VOID;
}

// 4.5 [conv.prom]: the promoted type of an integral operand.
int Analyzer::Promote(int type) const
{
	if(type < 0)
	{
		return type;
	}
	if(model_.Get(type).kind == kTypeCv)
	{
		type = model_.Get(type).base;
	}
	const Type& record = model_.Get(type);
	if(record.kind == kTypeEnum)
	{
		// 4.5/3: an unscoped enumeration promotes to its underlying type.  A
		// scoped one does not promote at all, which the callers guard.
		return record.underlying >= 0 ? record.underlying
		                              : model_.Fundamental(posttoken::FT_INT);
	}
	if(record.kind != kTypeFundamental)
	{
		return type;
	}
	switch(record.base)
	{
	case posttoken::FT_BOOL:
	case posttoken::FT_CHAR:
	case posttoken::FT_SIGNED_CHAR:
	case posttoken::FT_UNSIGNED_CHAR:
	case posttoken::FT_SHORT_INT:
	case posttoken::FT_UNSIGNED_SHORT_INT:
	case posttoken::FT_WCHAR_T:
	case posttoken::FT_CHAR16_T:
		return model_.Fundamental(posttoken::FT_INT);
	case posttoken::FT_CHAR32_T:
		return model_.Fundamental(posttoken::FT_UNSIGNED_INT);
	case posttoken::FT_FLOAT:
		return model_.Fundamental(posttoken::FT_DOUBLE);
	default:
		return type;
	}
}

namespace
{

// The rank an integral type takes in the usual arithmetic conversions (4.13).
int IntegralRank(int fundamental)
{
	switch(fundamental)
	{
	case posttoken::FT_BOOL:
		return 0;
	case posttoken::FT_CHAR:
	case posttoken::FT_SIGNED_CHAR:
	case posttoken::FT_UNSIGNED_CHAR:
		return 1;
	case posttoken::FT_SHORT_INT:
	case posttoken::FT_UNSIGNED_SHORT_INT:
		return 2;
	case posttoken::FT_INT:
	case posttoken::FT_UNSIGNED_INT:
		return 3;
	case posttoken::FT_LONG_INT:
	case posttoken::FT_UNSIGNED_LONG_INT:
		return 4;
	case posttoken::FT_LONG_LONG_INT:
	case posttoken::FT_UNSIGNED_LONG_LONG_INT:
		return 5;
	default:
		return 3;
	}
}

}  // namespace

// 5.9 [expr.arith.conv]: the common type of two arithmetic operands.
int Analyzer::UsualArithmetic(int left, int right) const
{
	const Type& a = model_.Get(left);
	const Type& b = model_.Get(right);
	const bool a_float = a.kind == kTypeFundamental && a.base >= posttoken::FT_FLOAT &&
	                     a.base <= posttoken::FT_LONG_DOUBLE;
	const bool b_float = b.kind == kTypeFundamental && b.base >= posttoken::FT_FLOAT &&
	                     b.base <= posttoken::FT_LONG_DOUBLE;
	if(a_float || b_float)
	{
		if((a_float && a.base == posttoken::FT_LONG_DOUBLE) ||
		   (b_float && b.base == posttoken::FT_LONG_DOUBLE))
		{
			return model_.Fundamental(posttoken::FT_LONG_DOUBLE);
		}
		if((a_float && a.base == posttoken::FT_DOUBLE) ||
		   (b_float && b.base == posttoken::FT_DOUBLE))
		{
			return model_.Fundamental(posttoken::FT_DOUBLE);
		}
		return model_.Fundamental(posttoken::FT_FLOAT);
	}
	const int pa = Promote(left);
	const int pb = Promote(right);
	if(pa == pb)
	{
		return pa;
	}
	const Type& na = model_.Get(pa);
	const Type& nb = model_.Get(pb);
	const bool sa = posttoken::FundamentalTypeIsSigned(
	    static_cast<posttoken::EFundamentalType>(na.base));
	const bool sb = posttoken::FundamentalTypeIsSigned(
	    static_cast<posttoken::EFundamentalType>(nb.base));
	const int ra = IntegralRank(na.base);
	const int rb = IntegralRank(nb.base);
	if(sa == sb)
	{
		return ra >= rb ? pa : pb;
	}
	const int unsigned_side = sa ? pb : pa;
	const int signed_side = sa ? pa : pb;
	const int unsigned_rank = sa ? rb : ra;
	const int signed_rank = sa ? ra : rb;
	if(unsigned_rank >= signed_rank)
	{
		return unsigned_side;
	}
	// The course ABI gives `long` the same width as `long long`, so a signed
	// `long` represents every unsigned `int` and the signed operand wins.
	return signed_rank >= 4 ? signed_side : unsigned_side;
}

int Analyzer::ClassOfPointer(int type) const
{
	if(type < 0)
	{
		return -1;
	}
	const Type& record = model_.Get(type);
	int base = -1;
	if(record.kind == kTypePointer)
	{
		base = record.base;
	}
	else if(record.kind == kTypeMemberPointer)
	{
		base = record.member;
	}
	else
	{
		return -1;
	}
	int cv = 0;
	if(model_.Get(base).kind == kTypeCv)
	{
		cv = model_.Get(base).quals;
		base = model_.Get(base).base;
	}
	return model_.Get(base).kind == kTypeClass ? base : -1;
}

// 4.10/1: the pointer type an integer literal zero may convert to.
int Analyzer::NullPointerTarget(int type) const
{
	if(type < 0)
	{
		return -1;
	}
	const ETypeKind kind = model_.Get(type).kind;
	return kind == kTypePointer || kind == kTypeMemberPointer ? type : -1;
}

// ---------------------------------------------------------------------------
// Qualification conversions (4.4)
// ---------------------------------------------------------------------------

namespace
{

// The cv-qualification signature of a type: one entry per pointer or array
// level, outermost first, and the type the chain ends at.
void Decompose(const Model& model, int type, vector<int>& levels, int& inner)
{
	int current = type;
	for(;;)
	{
		int quals = 0;
		if(model.Get(current).kind == kTypeCv)
		{
			quals = model.Get(current).quals;
			current = model.Get(current).base;
		}
		levels.push_back(quals);
		const ETypeKind kind = model.Get(current).kind;
		if(kind == kTypePointer || kind == kTypeMemberPointer || kind == kTypeArray)
		{
			current = model.Get(current).base;
			continue;
		}
		break;
	}
	inner = current;
}

}  // namespace

// 4.4/4: whether a prvalue of type `from` converts to `to` by a qualification
// conversion, and whether that conversion added a qualifier.
bool Analyzer::QualificationConvertible(int from, int to, bool& added) const
{
	added = false;
	if(from < 0 || to < 0)
	{
		return false;
	}
	int s = from;
	int r = to;
	int s_quals = 0;
	int r_quals = 0;
	if(model_.Get(s).kind == kTypeCv)
	{
		s_quals = model_.Get(s).quals;
		s = model_.Get(s).base;
	}
	if(model_.Get(r).kind == kTypeCv)
	{
		r_quals = model_.Get(r).quals;
		r = model_.Get(r).base;
	}
	if(s == r)
	{
		// 8.5.3/4: adding a qualifier at the top level is a qualification
		// conversion, and dropping one is not.
		if((s_quals & ~r_quals) != 0)
		{
			return false;
		}
		added = s_quals != r_quals;
		return true;
	}
	const Type& a = model_.Get(s);
	const Type& b = model_.Get(r);
	if(a.kind != b.kind)
	{
		return false;
	}
	if(a.kind == kTypeArray)
	{
		if(a.bound != b.bound)
		{
			return false;
		}
		return QualificationConvertible(a.base, b.base, added);
	}
	if(a.kind != kTypePointer && a.kind != kTypeMemberPointer)
	{
		return false;
	}
	vector<int> levels_a;
	vector<int> levels_b;
	int inner_a = -1;
	int inner_b = -1;
	Decompose(model_, from, levels_a, inner_a);
	Decompose(model_, to, levels_b, inner_b);
	if(levels_a.size() != levels_b.size() || inner_a != inner_b)
	{
		return false;
	}
	// Every qualifier the source carries must be present in the target, and a
	// level that changes qualification requires the levels inside it to be
	// const, which is what makes `int**` to `const int* const*` legal and
	// `int**` to `const int**` ill formed.
	for(size_t index = 1; index < levels_a.size(); ++index)
	{
		if((levels_a[index] & ~levels_b[index]) != 0)
		{
			return false;
		}
	}
	for(size_t index = 1; index < levels_a.size(); ++index)
	{
		if(levels_a[index] != levels_b[index])
		{
			for(size_t inner_index = 1; inner_index < index; ++inner_index)
			{
				if((levels_b[inner_index] & 1) == 0)
				{
					return false;
				}
			}
		}
	}
	added = levels_a != levels_b;
	return true;
}

// ---------------------------------------------------------------------------
// Standard conversion sequences
// ---------------------------------------------------------------------------

// The argument's type as a reference binds to it: a reference-typed expression
// is its referred type, and everything else keeps the type it printed with.
int Analyzer::SourceType(const Resolved& from) const
{
	return ReferredType(from.type);
}

// 13.3.3.1.1-13.3.3.1.5: the implicit conversion sequence from one argument to
// one parameter.
Analyzer::Conversion Analyzer::Convert(const Resolved& from, int target, int scope)
{
	(void)scope;
	Conversion result;
	result.target = target;
	const Type& parameter = model_.Get(target);

	if(parameter.kind == kTypeLvalueReference || parameter.kind == kTypeRvalueReference)
	{
		result.reference = true;
		result.rvalue_reference = parameter.kind == kTypeRvalueReference;
		const bool argument_lvalue = from.category == kLvalue;
		const int source = SourceType(from);
		// 8.5.3/5: an rvalue reference binds only to an rvalue, except that a
		// cv-qualified one still cannot bind an lvalue.
		if(result.rvalue_reference && argument_lvalue)
		{
			return result;
		}
		bool added = false;
		if(!QualificationConvertible(source, parameter.base, added))
		{
			return result;
		}
		if(!argument_lvalue && !result.rvalue_reference &&
		   (model_.Get(parameter.base).kind != kTypeCv))
		{
			// 8.5.3/5: a non-const lvalue reference does not bind a temporary.
			return result;
		}
		result.rank = 4;
		result.qualification = added;
		result.bound_to_lvalue = argument_lvalue;
		result.lvalue_to_rvalue = false;
		return result;
	}

	int source = SourceType(from);
	// 4.1: a prvalue of a cv-qualified scalar type is the unqualified type, so
	// the top-level qualifier of the argument does not stand in the way of a
	// by-value parameter (8.5/6).
	if(model_.Get(source).kind == kTypeCv)
	{
		source = model_.Get(source).base;
	}
	// 4.2 and 4.3: an array and a function decay to a pointer, which is an
	// lvalue transformation and so still an exact match.
	bool decayed = false;
	if(model_.Get(target).kind == kTypePointer)
	{
		if(model_.Get(source).kind == kTypeArray)
		{
			source = model_.Pointer(model_.Get(source).base);
			decayed = true;
		}
		else if(model_.Get(source).kind == kTypeFunction)
		{
			source = model_.Pointer(source);
			decayed = true;
		}
	}
	if(source == target)
	{
		result.rank = 4;
		result.lvalue_to_rvalue = from.category == kLvalue || decayed;
		return result;
	}
	if(model_.Get(source).kind == kTypeClass && model_.Get(target).kind == kTypeClass)
	{
		// Copy-initialisation of one class from another is a later assignment's
		// problem; the fixtures reach it only through a call that returns the
		// same class, which is an exact match.
		if(source == target)
		{
			result.rank = 4;
			return result;
		}
		return result;
	}
	if(QualificationConvertible(source, target, result.qualification))
	{
		result.rank = 4;
		result.lvalue_to_rvalue = from.category == kLvalue;
		return result;
	}
	if(IsIntegralType(source) && IsIntegralType(target) && !IsScopedEnum(source) &&
	   !IsScopedEnum(target))
	{
		const bool both_enum = model_.Get(source).kind == kTypeEnum &&
		                       model_.Get(target).kind == kTypeEnum;
		if(both_enum)
		{
			return result;
		}
		if(Promote(source) == target)
		{
			result.rank = 3;
			result.lvalue_to_rvalue = true;
			return result;
		}
		result.rank = 2;
		result.lvalue_to_rvalue = true;
		return result;
	}
	if(IsArithmeticType(source) && IsArithmeticType(target))
	{
		result.rank = 2;
		result.lvalue_to_rvalue = true;
		return result;
	}
	// 4.12: a pointer or a pointer to member converts to bool.
	if(IsScalarType(source) && IsScalarType(target) &&
	   model_.Get(target).kind == kTypeFundamental && model_.Get(target).base == posttoken::FT_BOOL &&
	   model_.Get(source).kind != kTypeFundamental)
	{
		result.rank = 2;
		result.boolean_conversion = true;
		result.lvalue_to_rvalue = true;
		return result;
	}
	// 4.10: a null pointer constant, or `nullptr`, becomes any pointer.
	if(from.null_zero || (model_.Get(source).kind == kTypeFundamental &&
	                      model_.Get(source).base == posttoken::FT_NULLPTR_T))
	{
		if(NullPointerTarget(target) >= 0)
		{
			result.rank = 2;
			result.pointer_conversion = true;
			result.lvalue_to_rvalue = true;
			return result;
		}
	}
	if(model_.Get(source).kind == kTypePointer && model_.Get(target).kind == kTypePointer)
	{
		bool proper = false;
		if(!PointerCompatible(source, target, proper))
		{
			return result;
		}
		result.rank = 2;
		result.pointer_conversion = true;
		result.proper_subsequence = proper;
		result.lvalue_to_rvalue = true;
		return result;
	}
	if(model_.Get(source).kind == kTypeMemberPointer &&
	   model_.Get(target).kind == kTypeMemberPointer)
	{
		bool proper = false;
		if(!PointerCompatible(source, target, proper))
		{
			return result;
		}
		result.rank = 2;
		result.pointer_conversion = true;
		result.proper_subsequence = proper;
		result.lvalue_to_rvalue = true;
		return result;
	}
	return result;
}

// 4.10/3: `T*` converts to `void*` and to a cv-qualified `void*`, and a derived
// pointer converts to a base pointer; the pointee qualification may only grow.
bool Analyzer::PointerCompatible(int from, int to, bool& proper_subsequence) const
{
	proper_subsequence = false;
	int s = from;
	int r = to;
	int s_quals = 0;
	int r_quals = 0;
	if(model_.Get(s).kind == kTypeCv)
	{
		s_quals = model_.Get(s).quals;
		s = model_.Get(s).base;
	}
	if(model_.Get(r).kind == kTypeCv)
	{
		r_quals = model_.Get(r).quals;
		r = model_.Get(r).base;
	}
	if(model_.Get(s).kind != model_.Get(r).kind)
	{
		return false;
	}
	if(model_.Get(s).kind == kTypeMemberPointer)
	{
		if(model_.Get(s).base != model_.Get(r).base)
		{
			return false;
		}
		return PointerCompatible(model_.Get(s).member, model_.Get(r).member, proper_subsequence);
	}
	if(model_.Get(s).kind != kTypePointer)
	{
		return false;
	}
	if(s == r)
	{
		proper_subsequence = (s_quals & ~r_quals) != 0;
		return true;
	}
	const int source_base = model_.Get(s).base;
	const int target_base = model_.Get(r).base;
	int source_cv = 0;
	int target_cv = 0;
	int source_plain = source_base;
	int target_plain = target_base;
	if(model_.Get(source_plain).kind == kTypeCv)
	{
		source_cv = model_.Get(source_plain).quals;
		source_plain = model_.Get(source_plain).base;
	}
	if(model_.Get(target_plain).kind == kTypeCv)
	{
		target_cv = model_.Get(target_plain).quals;
		target_plain = model_.Get(target_plain).base;
	}
	// `T*` to `void*`: the conversion is a proper subsequence of any conversion
	// that also changes qualification.
	if(model_.Get(target_plain).kind == kTypeFundamental &&
	   model_.Get(target_plain).base == posttoken::FT_VOID &&
	   (model_.Get(source_plain).kind != kTypeFundamental ||
	    model_.Get(source_plain).base != posttoken::FT_VOID))
	{
		if((source_cv & ~target_cv) != 0)
		{
			return false;
		}
		proper_subsequence = source_cv != target_cv;
		return true;
	}
	// A derived pointer to a base pointer, with qualification that only grows.
	if(model_.Get(source_plain).kind != kTypeClass ||
	   model_.Get(target_plain).kind != kTypeClass)
	{
		return false;
	}
	if(!model_.DerivesFrom(source_plain, target_plain))
	{
		return false;
	}
	if((source_cv & ~target_cv) != 0)
	{
		return false;
	}
	proper_subsequence = source_cv != target_cv;
	return true;
}

// 13.3.3.2/3: whether conversion sequence `a` is better than `b`, worse than
// it, or indistinguishable from it.
int Analyzer::CompareConversions(const Conversion& a, const Conversion& b) const
{
	if(a.rank != b.rank)
	{
		return a.rank > b.rank ? 1 : -1;
	}
	if(a.rank == 0)
	{
		return 0;
	}
	// A reference binding to an rvalue reference is better than one to an
	// lvalue reference when both bind the same kind of argument.
	if(a.reference && b.reference && a.rvalue_reference != b.rvalue_reference)
	{
		if(a.rvalue_reference && !a.bound_to_lvalue)
		{
			return 1;
		}
		if(b.rvalue_reference && !b.bound_to_lvalue)
		{
			return -1;
		}
	}
	if(a.proper_subsequence != b.proper_subsequence)
	{
		return a.proper_subsequence ? 1 : -1;
	}
	if(a.pointer_conversion != b.pointer_conversion)
	{
		return a.pointer_conversion ? 1 : -1;
	}
	if(a.qualification != b.qualification)
	{
		// The conversion that adds fewer qualifiers is the better one.
		return a.qualification ? -1 : 1;
	}
	return 0;
}

bool Analyzer::BetterSequence(const vector<Conversion>& a, const vector<Conversion>& b) const
{
	bool better = false;
	for(size_t index = 0; index < a.size() && index < b.size(); ++index)
	{
		const int order = CompareConversions(a[index], b[index]);
		if(order < 0)
		{
			return false;
		}
		if(order > 0)
		{
			better = true;
		}
	}
	return better;
}

// ---------------------------------------------------------------------------
// Candidate collection
// ---------------------------------------------------------------------------

// Every function named `name` visible from `scope`.  A scope that binds the
// name as something other than a function hides the outer ones, which is what
// ordinary unqualified lookup does.
void Analyzer::CollectFrom(int scope, const string& name, vector<Candidate>& out,
                           vector<int>& visited, bool& blocked) const
{
	for(size_t index = 0; index < visited.size(); ++index)
	{
		if(visited[index] == scope)
		{
			return;
		}
	}
	visited.push_back(scope);
	const Scope& record = model_.ScopeOf(scope);

	const map<string, int>::const_iterator direct = record.values.find(name);
	if(direct != record.values.end())
	{
		const Entity& entity = model_.EntityOf(direct->second);
		if(entity.kind != kEntityFunction)
		{
			blocked = true;
			return;
		}
		for(size_t index = 0; index < record.bindings.size(); ++index)
		{
			const Binding& binding = record.bindings[index];
			if(binding.kind == kBindingFunction && binding.name == name)
			{
				// 13.1/3: two declarations with one signature and one return
				// type are one function, not two candidates.
				bool seen = false;
				for(size_t existing = 0; existing < out.size(); ++existing)
				{
					// 13.1/3: two declarations of one signature are one
					// function, so the candidate set holds it once.
					seen = seen || (out[existing].type == binding.type) ||
					       (model_.SameSignature(out[existing].type, binding.type) &&
					        model_.SameFunctionType(out[existing].type, binding.type));
				}
				if(seen)
				{
					continue;
				}
				Candidate candidate;
				candidate.entity = binding.entity;
				candidate.type = binding.type;
				candidate.scope = scope;
				out.push_back(candidate);
			}
		}
		// A using-declaration copies the target's binding into this scope, so a
		// name bound here that denotes a function elsewhere is still this
		// scope's answer.
		return;
	}

	if(record.unnamed && record.parent >= 0)
	{
		CollectFrom(record.parent, name, out, visited, blocked);
		if(blocked || !out.empty())
		{
			return;
		}
	}
	for(size_t index = 0; index < record.inline_namespaces.size(); ++index)
	{
		CollectFrom(record.inline_namespaces[index], name, out, visited, blocked);
		if(blocked || !out.empty())
		{
			return;
		}
	}
	for(size_t index = 0; index < record.directives.size(); ++index)
	{
		CollectFrom(record.directives[index], name, out, visited, blocked);
		if(blocked || !out.empty())
		{
			return;
		}
	}
}

void Analyzer::CollectCandidates(int scope, const string& text, vector<Candidate>& out)
{
	string qualifier;
	string name;
	SplitQualifiedName(text, qualifier, name);
	if(!qualifier.empty())
	{
		const int target = model_.ResolveQualifier(scope, qualifier);
		vector<int> visited;
		bool blocked = false;
		CollectFrom(target, name, out, visited, blocked);
		return;
	}
	for(int current = scope; current >= 0; current = model_.ScopeOf(current).parent)
	{
		vector<int> visited;
		bool blocked = false;
		CollectFrom(current, name, out, visited, blocked);
		if(blocked)
		{
			out.clear();
			return;
		}
		if(!out.empty())
		{
			return;
		}
	}
}

// ---------------------------------------------------------------------------
// Names and types
// ---------------------------------------------------------------------------

string Analyzer::QualifiedScopeName(int scope) const
{
	if(scope < 0)
	{
		return string();
	}
	const Scope& record = model_.ScopeOf(scope);
	if(record.parent < 0)
	{
		return string();
	}
	string prefix = QualifiedScopeName(record.parent);
	if(record.kind != kScopeNamespace && record.kind != kScopeClass &&
	   record.kind != kScopeEnum)
	{
		return prefix;
	}
	if(record.name.empty() || record.name == "<unnamed>")
	{
		return prefix;
	}
	return prefix.empty() ? record.name : prefix + "::" + record.name;
}

// The name an entity prints with.  An unnamed namespace is not a component, so
// a name inside one prints as if the namespace were not there.
string Analyzer::QualifiedEntityName(int entity) const
{
	if(entity < 0)
	{
		return string();
	}
	const Entity& record = model_.EntityOf(entity);
	int scope = record.decl_scope;
	if(record.kind == kEntityClass || record.kind == kEntityEnum)
	{
		scope = record.scope;
	}
	const string prefix = QualifiedScopeName(scope);
	return prefix.empty() ? record.name : prefix + "::" + record.name;
}

// A member function's type as the dump prints it: the implicit object parameter
// is written out, so `void select() const` in `struct C` is
// `function of (pointer to const struct C) returning void` (9.3.1/3).
string Analyzer::BoundSpelling(int type, int scope) const
{
	if(type < 0)
	{
		return string();
	}
	const Type record = model_.Get(type);
	if(record.kind != kTypeFunction || model_.ScopeOf(scope).kind != kScopeClass)
	{
		return Spell(type);
	}
	const int class_entity = model_.ScopeOf(scope).entity;
	if(class_entity < 0)
	{
		return Spell(type);
	}
	const int class_type = model_.EntityOf(class_entity).type;
	const int qualified = model_.Qualified(record.quals, class_type);
	vector<int> params;
	params.push_back(model_.Pointer(qualified));
	for(size_t index = 0; index < record.params.size(); ++index)
	{
		params.push_back(record.params[index]);
	}
	return Spell(model_.Function(record.base, params, record.varargs,
	                                       0, 0));
}

string Analyzer::QualifiedEntitySpelling(int entity) const
{
	if(entity < 0)
	{
		return string();
	}
	return Spell(model_.EntityOf(entity).type);
}

}  // namespace semantic
}  // namespace cppgm

// ---------------------------------------------------------------------------
// Expression forms
// ---------------------------------------------------------------------------

namespace cppgm
{
namespace semantic
{

// 2.14: a literal's type is the fundamental type the token decoded to.  A
// string literal is an array of its element type, including the terminator,
// and is the one literal that is an lvalue.
Analyzer::Resolved Analyzer::SemLiteral(int node, int scope)
{
	(void)scope;
	Resolved result;
	const string& text = Label(node);
	const int index = arena_.Literal(node);
	const posttoken::EFundamentalType fundamental =
	    index >= 0 && static_cast<size_t>(index) < literals_.size()
	        ? literals_[index].fundamental_type
	        : posttoken::FT_INT;
	const size_t count = index >= 0 && static_cast<size_t>(index) < literals_.size()
	    ? literals_[index].count
	    : 1;
	if(text.find('"') != string::npos)
	{
		const int element = model_.Qualified(1, model_.Fundamental(fundamental));
		result.type = model_.Array(static_cast<long long>(count), element);
		result.category = kLvalue;
	}
	else
	{
		result.type = model_.Fundamental(fundamental);
		result.category = kPrvalue;
		if(IsIntegralType(result.type))
		{
			const Constant value = EvaluateLiteral(node);
			if(value.valid && value.value == 0)
			{
				result.null_zero = true;
			}
		}
	}
	result.node = sem_.Add("literal", result.category, Spell(result.type), text);
	return result;
}

Analyzer::Resolved Analyzer::SemKeywordLiteral(int node, int scope)
{
	(void)scope;
	Resolved result;
	const string word = OperatorWord(Label(node));
	if(word == "true" || word == "false")
	{
		result.type = model_.Fundamental(posttoken::FT_BOOL);
	}
	else if(word == "nullptr")
	{
		result.type = model_.Fundamental(posttoken::FT_NULLPTR_T);
	}
	else
	{
		throw SemanticError("unsupported keyword literal `" + word + "`");
	}
	result.category = kPrvalue;
	result.node = sem_.Add("literal", result.category, Spell(result.type), Label(node));
	return result;
}

// The type an object's name prints with: a reference prints the type it refers
// to, because the name denotes the object rather than the reference.
Analyzer::Resolved Analyzer::SemIdExpression(int node, int scope)
{
	Resolved result;
	const string& text = Label(node);
	const int entity = ResolveValueName(scope, text);
	if(entity < 0)
	{
		// A name that denotes a type is not an expression; the call layer turns
		// it into a functional cast where that is what the source wrote.
		string qualifier;
		string name;
		SplitQualifiedName(text, qualifier, name);
		const int type_entity = qualifier.empty()
		    ? model_.LookupTypeUnqualified(scope, name)
		    : model_.LookupTypeIn(model_.ResolveQualifier(scope, qualifier), name);
		if(type_entity >= 0)
		{
			result.type_name = true;
			result.type = model_.EntityOf(type_entity).type;
			result.entity = type_entity;
			return result;
		}
		// A simple-type-specifier written as a keyword is the functional cast's
		// own type name, which the parser keeps with its token kind.
		string word = KeywordWord(text);
		if(word.empty() && IsSimpleTypeWord(text))
		{
			word = text;
		}
		if(!word.empty())
		{
			vector<string> words;
			words.push_back(word);
			result.type_name = true;
			result.type = model_.Fundamental(FundamentalFromSpecifiers(words));
			return result;
		}
		throw SemanticError("unknown expression name `" + text + "`");
	}
	const Entity record = model_.EntityOf(entity);
	result.entity = entity;
	if(record.kind == kEntityEnumerator)
	{
		// 7.2/11: an enumerator is a named constant, which the dump prints as
		// the value it stands for.
		result.type = record.type;
		result.category = kPrvalue;
		ostringstream out;
		out << record.value;
		result.node = sem_.Add("literal", result.category, Spell(result.type),
		                       out.str());
		return result;
	}
	if(record.kind == kEntityFunction)
	{
		result.function = entity;
		result.type = record.type;
		result.category = kLvalue;
		vector<Candidate> candidates;
		CollectCandidates(scope, text, candidates);
		result.overloaded = candidates.size() > 1;
		if(!result.overloaded && !candidates.empty())
		{
			result.type = candidates[0].type;
		}
		result.node = sem_.Add("id-expression", result.category,
		                       Spell(ReferredType(result.type)), text);
		return result;
	}
	// 9.5/3: a name that an anonymous union injected into an enclosing scope
	// denotes a member of an object the source never named.
	const map<int, int>::const_iterator storage = union_storage_.find(MemberClassOf(entity));
	if(storage != union_storage_.end())
	{
		result.type = ReferredType(record.type);
		result.category = kLvalue;
		result.node = sem_.Add("member-expression", result.category,
		                       Spell(result.type), record.name);
		const int object = sem_.Add("id-expression", kLvalue,
		                            Spell(model_.EntityOf(storage->second).type),
		                            model_.EntityOf(storage->second).name);
		sem_.AddChild(result.node, object);
		return result;
	}
	result.type = ReferredType(record.type);
	result.category = kLvalue;
	result.node = sem_.Add("id-expression", result.category, Spell(result.type), text);
	return result;
}

// The class an entity is a member of, or -1 when it is not a class member.
int Analyzer::MemberClassOf(int entity) const
{
	if(entity < 0)
	{
		return -1;
	}
	const int scope = model_.EntityOf(entity).decl_scope;
	if(scope < 0 || model_.ScopeOf(scope).kind != kScopeClass)
	{
		return -1;
	}
	const int owner = model_.ScopeOf(scope).entity;
	return owner < 0 ? -1 : model_.EntityOf(owner).type;
}

Analyzer::Resolved Analyzer::SemParenthesized(int node, int scope)
{
	return SemExpr(ChildAt(node, 0), scope);
}

// 5.3 [expr.unary.op] and the two increment forms.
Analyzer::Resolved Analyzer::SemUnary(int node, int scope)
{
	Resolved result;
	const string op = OperatorWord(Label(node));
	const int operand_node = ChildAt(node, 0);
	if(op == "++" || op == "--")
	{
		const Resolved operand = SemExpr(operand_node, scope);
		if(operand.category != kLvalue || !IsScalarType(ReferredType(operand.type)))
		{
			throw SemanticError("the operand of `" + op + "` must be a modifiable lvalue");
		}
		result.type = ReferredType(operand.type);
		result.category = kLvalue;
		result.node = sem_.Add("unary-expression", result.category,
		                       Spell(result.type), Label(node));
		sem_.AddChild(result.node, operand.node);
		return result;
	}
	const Resolved operand = SemExpr(operand_node, scope);
	if(op == "&")
	{
		if(operand.category != kLvalue)
		{
			throw SemanticError("the operand of `&` must be an lvalue");
		}
		const int class_type = MemberClassOf(operand.entity);
		if(class_type >= 0 && model_.Get(operand.type).kind == kTypeFunction)
		{
			// 5.3.1/3 with 8.3.3: the address of a member function is a pointer
			// to member, and the operand prints the member's bound type.
			result.type = model_.MemberPointer(class_type, operand.type);
			sem_.SetType(operand.node, BoundSpelling(operand.type, ClassScopeOf(class_type)));
		}
		else
		{
			result.type = model_.Pointer(operand.type);
		}
		result.category = kPrvalue;
	}
	else if(op == "*")
	{
		// 4.2: an array operand decays to a pointer before the indirection.
		const int referred = ReferredType(operand.type);
		if(model_.Get(referred).kind == kTypeArray)
		{
			result.type = model_.Get(referred).base;
			result.category = kLvalue;
		}
		else
		{
			if(model_.Get(referred).kind != kTypePointer)
			{
				throw SemanticError("the operand of `*` must be a pointer");
			}
			result.type = model_.Get(referred).base;
			result.category = kLvalue;
		}
	}
	else if(op == "!")
	{
		if(!IsScalarType(operand.type))
		{
			throw SemanticError("the operand of `!` must be a scalar");
		}
		result.type = model_.Fundamental(posttoken::FT_BOOL);
		result.category = kPrvalue;
	}
	else if(op == "~")
	{
		if(!IsIntegralType(operand.type))
		{
			throw SemanticError("the operand of `~` must be integral");
		}
		result.type = Promote(operand.type);
		result.category = kPrvalue;
	}
	else if(op == "+" || op == "-")
	{
		if(!IsArithmeticType(operand.type))
		{
			throw SemanticError("the operand of `" + op + "` must be arithmetic");
		}
		result.type = IsIntegralType(operand.type) ? Promote(operand.type) : operand.type;
		result.category = kPrvalue;
	}
	else
	{
		throw SemanticError("unsupported unary operator `" + op + "`");
	}
	result.node = sem_.Add("unary-expression", result.category, Spell(result.type),
	                       Label(node));
	sem_.AddChild(result.node, operand.node);
	return result;
}

Analyzer::Resolved Analyzer::SemPostfix(int node, int scope)
{
	const string op = OperatorWord(Label(node));
	const Resolved operand = SemExpr(ChildAt(node, 0), scope);
	if(operand.category != kLvalue || !IsScalarType(ReferredType(operand.type)))
	{
		throw SemanticError("the operand of `" + op + "` must be a modifiable lvalue");
	}
	Resolved result;
	result.type = ReferredType(operand.type);
	result.category = kPrvalue;
	result.node = sem_.Add("postfix-expression", result.category, Spell(result.type),
	                       Label(node));
	sem_.AddChild(result.node, operand.node);
	return result;
}

// 5.6 to 5.15: the built-in binary operators over the supported operand
// categories.  Each operand keeps its own type; only the result changes.
Analyzer::Resolved Analyzer::SemBinary(int node, int scope)
{
	const string op = OperatorWord(Label(node));
	const Resolved left = SemExpr(ChildAt(node, 0), scope);
	const Resolved right = SemExpr(ChildAt(node, 1), scope);
	Resolved result;
	int left_type = ReferredType(left.type);
	int right_type = ReferredType(right.type);
	// 4.2: an array operand of a binary operator decays to a pointer, which is
	// what makes `a + 2` a pointer sum and `p - q` a pointer difference.
	if(model_.Get(left_type).kind == kTypeArray)
	{
		left_type = model_.Pointer(model_.Get(left_type).base);
	}
	if(model_.Get(right_type).kind == kTypeArray)
	{
		right_type = model_.Pointer(model_.Get(right_type).base);
	}
	const ETypeKind left_kind = left_type < 0 ? kTypeFundamental : model_.Get(left_type).kind;
	const ETypeKind right_kind = right_type < 0 ? kTypeFundamental : model_.Get(right_type).kind;
	const bool left_pointer = left_kind == kTypePointer || left_kind == kTypeMemberPointer;
	const bool right_pointer = right_kind == kTypePointer || right_kind == kTypeMemberPointer;
	const bool left_null = left.null_zero ||
	                       (model_.Get(left_type).kind == kTypeFundamental &&
	                        model_.Get(left_type).base == posttoken::FT_NULLPTR_T);
	const bool right_null = right.null_zero ||
	                        (model_.Get(right_type).kind == kTypeFundamental &&
	                         model_.Get(right_type).base == posttoken::FT_NULLPTR_T);

	if(op == ",")
	{
		result.type = right.type;
		result.category = right.category;
	}
	else if(op == "&&" || op == "||")
	{
		if(!IsScalarType(left_type) || !IsScalarType(right_type))
		{
			throw SemanticError("the operands of `" + op + "` must be scalars");
		}
		result.type = model_.Fundamental(posttoken::FT_BOOL);
		result.category = kPrvalue;
	}
	else if(op == "==" || op == "!=")
	{
		const bool arithmetic = IsArithmeticType(left_type) && IsArithmeticType(right_type);
		const bool pointers = (left_pointer || left_null) && (right_pointer || right_null) &&
		                      (left_pointer || right_pointer);
		if(!arithmetic && !pointers)
		{
			throw SemanticError("`" + op + "` needs comparable operands");
		}
		if(arithmetic && (left_pointer || right_pointer))
		{
			throw SemanticError("a pointer does not compare with an integer");
		}
		result.type = model_.Fundamental(posttoken::FT_BOOL);
		result.category = kPrvalue;
	}
	else if(op == "<" || op == ">" || op == "<=" || op == ">=")
	{
		if(!(IsArithmeticType(left_type) && IsArithmeticType(right_type)) &&
		   !(left_pointer && right_pointer))
		{
			throw SemanticError("`" + op + "` needs comparable operands");
		}
		result.type = model_.Fundamental(posttoken::FT_BOOL);
		result.category = kPrvalue;
	}
	else if(op == "+")
	{
		if(left_pointer && IsIntegralType(right_type))
		{
			result.type = left_type;
		}
		else if(right_pointer && IsIntegralType(left_type))
		{
			result.type = right_type;
		}
		else if(IsArithmeticType(left_type) && IsArithmeticType(right_type))
		{
			result.type = UsualArithmetic(left_type, right_type);
		}
		else
		{
			throw SemanticError("`+` needs arithmetic or pointer operands");
		}
		result.category = kPrvalue;
	}
	else if(op == "-")
	{
		if(left_pointer && IsIntegralType(right_type))
		{
			result.type = left_type;
		}
		else if(left_pointer && right_pointer)
		{
			// 5.7/6: the difference of two pointers is a `ptrdiff_t`.
			result.type = model_.Fundamental(posttoken::FT_LONG_INT);
		}
		else if(IsArithmeticType(left_type) && IsArithmeticType(right_type))
		{
			result.type = UsualArithmetic(left_type, right_type);
		}
		else
		{
			throw SemanticError("`-` needs arithmetic or pointer operands");
		}
		result.category = kPrvalue;
	}
	else if(op == "*" || op == "/")
	{
		if(!IsArithmeticType(left_type) || !IsArithmeticType(right_type))
		{
			throw SemanticError("`" + op + "` needs arithmetic operands");
		}
		result.type = UsualArithmetic(left_type, right_type);
		result.category = kPrvalue;
	}
	else if(op == "%" || op == "&" || op == "|" || op == "^")
	{
		if(!IsIntegralType(left_type) || !IsIntegralType(right_type))
		{
			throw SemanticError("`" + op + "` needs integral operands");
		}
		result.type = UsualArithmetic(left_type, right_type);
		result.category = kPrvalue;
	}
	else if(op == "<<" || op == ">>")
	{
		if(!IsIntegralType(left_type) || !IsIntegralType(right_type))
		{
			throw SemanticError("`" + op + "` needs integral operands");
		}
		result.type = Promote(left_type);
		result.category = kPrvalue;
	}
	else
	{
		throw SemanticError("unsupported binary operator `" + op + "`");
	}
	result.node = sem_.Add("binary-expression", result.category, Spell(result.type),
	                       Label(node));
	sem_.AddChild(result.node, left.node);
	sem_.AddChild(result.node, right.node);
	return result;
}

// 5.17 [expr.ass]: the result is the left operand's type and value category.
Analyzer::Resolved Analyzer::SemAssignment(int node, int scope)
{
	const string op = OperatorWord(Label(node));
	const Resolved left = SemExpr(ChildAt(node, 0), scope);
	const Resolved right = SemExpr(ChildAt(node, 1), scope);
	if(left.category != kLvalue || !IsScalarType(ReferredType(left.type)))
	{
		throw SemanticError("the left operand of `" + op + "` must be a modifiable lvalue");
	}
	const int left_type = ReferredType(left.type);
	if(op == "=")
	{
		Conversion conversion = Convert(right, left_type, scope);
		if(conversion.rank == 0)
		{
			throw SemanticError("the right operand of `=` does not convert to the left type");
		}
	}
	else
	{
		// 5.17/7: the compound form applies the operator to the operands and
		// converts the result back, so the operator's own constraints apply.
		const string base = op.substr(0, op.size() - 1);
		if((base == "&" || base == "|" || base == "^" || base == "%" || base == "<<" ||
		    base == ">>") &&
		   (!IsIntegralType(left_type) || !IsIntegralType(ReferredType(right.type))))
		{
			throw SemanticError("`" + op + "` needs integral operands");
		}
		if((base == "*" || base == "/") &&
		   (!IsArithmeticType(left_type) || !IsArithmeticType(ReferredType(right.type))))
		{
			throw SemanticError("`" + op + "` needs arithmetic operands");
		}
		if(base == "+")
		{
			const int right_type = ReferredType(right.type);
			const bool pointer = model_.Get(left_type).kind == kTypePointer ||
			                     model_.Get(left_type).kind == kTypeMemberPointer;
			if(!(pointer && IsIntegralType(right_type)) &&
			   !(IsArithmeticType(left_type) && IsArithmeticType(right_type)))
			{
				throw SemanticError("`+=` needs arithmetic or pointer operands");
			}
		}
	}
	Resolved result;
	result.type = ReferredType(left.type);
	result.category = kLvalue;
	result.node = sem_.Add("assignment-expression", result.category,
	                       Spell(result.type), Label(node));
	sem_.AddChild(result.node, left.node);
	sem_.AddChild(result.node, right.node);
	return result;
}

// 5.16 [expr.cond]: the supported scalar cases, including the mixed
// lvalue/prvalue `bool` pair the fixtures reach.
Analyzer::Resolved Analyzer::SemConditional(int node, int scope)
{
	const Resolved condition = SemExpr(ChildAt(node, 0), scope);
	const Resolved left = SemExpr(ChildAt(node, 1), scope);
	const Resolved right = SemExpr(ChildAt(node, 2), scope);
	if(!IsScalarType(ReferredType(condition.type)))
	{
		throw SemanticError("the condition of `?:` must be a scalar");
	}
	int left_type = ReferredType(left.type);
	int right_type = ReferredType(right.type);
	if(model_.Get(left_type).kind == kTypeArray)
	{
		left_type = model_.Pointer(model_.Get(left_type).base);
	}
	if(model_.Get(right_type).kind == kTypeArray)
	{
		right_type = model_.Pointer(model_.Get(right_type).base);
	}
	const bool left_pointer = model_.Get(left_type).kind == kTypePointer ||
	                          model_.Get(left_type).kind == kTypeMemberPointer;
	const bool right_pointer = model_.Get(right_type).kind == kTypePointer ||
	                           model_.Get(right_type).kind == kTypeMemberPointer;
	const bool left_null = left.null_zero ||
	                       (model_.Get(left_type).kind == kTypeFundamental &&
	                        model_.Get(left_type).base == posttoken::FT_NULLPTR_T);
	const bool right_null = right.null_zero ||
	                        (model_.Get(right_type).kind == kTypeFundamental &&
	                         model_.Get(right_type).base == posttoken::FT_NULLPTR_T);
	Resolved result;
	if(left_pointer && right_pointer)
	{
		// 5.16/6: two pointer operands combine to the composite pointer type,
		// which for the supported cases is the more qualified one.
		bool proper = false;
		if(PointerCompatible(left_type, right_type, proper))
		{
			result.type = right_type;
		}
		else if(PointerCompatible(right_type, left_type, proper))
		{
			result.type = left_type;
		}
		else
		{
			throw SemanticError("the operands of `?:` are not compatible pointers");
		}
	}
	else if((left_pointer && right_null) || (right_pointer && left_null))
	{
		result.type = left_pointer ? left_type : right_type;
	}
	else if(left_pointer || right_pointer)
	{
		throw SemanticError("the operands of `?:` are not compatible");
	}
	else if(left_type == right_type)
	{
		// 5.16/4-6: operands of one type have that type as the result, which
		// the fixtures reach for the `bool` and enumeration pairs.
		result.type = left_type;
	}
	else if(IsArithmeticType(left_type) && IsArithmeticType(right_type))
	{
		result.type = UsualArithmetic(left_type, right_type);
	}
	else if(model_.Get(left_type).kind == kTypeEnum || model_.Get(right_type).kind == kTypeEnum)
	{
		if(left_type != right_type)
		{
			throw SemanticError("the operands of `?:` are not the same enumeration");
		}
		result.type = left_type;
	}
	else if(left_type == right_type)
	{
		result.type = left_type;
	}
	else
	{
		throw SemanticError("the operands of `?:` are not compatible");
	}
	// 5.16/4: the result is an lvalue when both operands are lvalues of the
	// same type, and a prvalue otherwise.
	result.category = (left.category == kLvalue && right.category == kLvalue &&
	                   left_type == right_type) ? kLvalue : kPrvalue;
	result.node = sem_.Add("conditional-expression", result.category,
	                       Spell(result.type));
	sem_.AddChild(result.node, condition.node);
	sem_.AddChild(result.node, left.node);
	sem_.AddChild(result.node, right.node);
	return result;
}

// 5.2.1 [expr.sub]: one operand is an array or a pointer and the other an
// integral or unscoped enumeration value.
Analyzer::Resolved Analyzer::SemSubscript(int node, int scope)
{
	const Resolved left = SemExpr(ChildAt(node, 0), scope);
	const Resolved right = SemExpr(ChildAt(node, 1), scope);
	int array_side = -1;
	int index_side = -1;
	const int left_type = ReferredType(left.type);
	const int right_type = ReferredType(right.type);
	const ETypeKind left_kind = model_.Get(left_type).kind;
	const ETypeKind right_kind = model_.Get(right_type).kind;
	const bool left_index = IsIntegralType(left_type) || left_kind == kTypeEnum;
	const bool right_index = IsIntegralType(right_type) || right_kind == kTypeEnum;
	if((left_kind == kTypeArray || left_kind == kTypePointer) && right_index)
	{
		array_side = left.node;
		index_side = right.node;
		if(left_kind == kTypeArray)
		{
			// 5.2.1/1: `a[i]` is `*(a + i)`, so an array decays first.
			(array_side);
		}
	}
	else if((right_kind == kTypeArray || right_kind == kTypePointer) && left_index)
	{
		array_side = right.node;
		index_side = left.node;
	}
	else
	{
		throw SemanticError("`[]` needs an array or pointer and an integer");
	}
	const int source = (array_side == left.node) ? left_type : right_type;
	const int element = model_.Get(source).kind == kTypeArray ? model_.Get(source).base
	                                                          : model_.Get(source).base;
	Resolved result;
	result.type = element;
	result.category = kLvalue;
	result.node = sem_.Add("subscript-expression", result.category, Spell(result.type));
	// 5.2.1/1: `1[a]` is `a[1]`, and the dump prints the array operand first
	// whichever way the source wrote it.
	if(array_side == right.node)
	{
		sem_.AddChild(result.node, right.node);
		sem_.AddChild(result.node, left.node);
	}
	else
	{
		sem_.AddChild(result.node, left.node);
		sem_.AddChild(result.node, right.node);
	}
	return result;
}

// 5.3.3 [expr.sizeof], and the `alignof`/`typeid` forms that share its node.
Analyzer::Resolved Analyzer::SemSizeof(int node, int scope)
{
	const int operand = ChildAt(node, 0);
	int target = -1;
	if(Tag(operand) == "type-id")
	{
		target = BuildDeclarator(operand, -1, scope);
	}
	else if(Tag(operand) == "id-expression")
	{
		const int entity = ResolveValueName(scope, Label(operand));
		target = entity < 0 ? -1 : model_.EntityOf(entity).type;
	}
	else
	{
		const Resolved value = SemExpr(operand, scope);
		target = value.type;
	}
	if(target < 0)
	{
		throw SemanticError("`sizeof` needs a complete type");
	}
	unsigned long long amount = 0;
	if(!model_.SizeOf(target, amount))
	{
		throw SemanticError("`sizeof` of an incomplete type");
	}
	Resolved result;
	result.type = model_.Fundamental(posttoken::FT_UNSIGNED_LONG_INT);
	result.category = kPrvalue;
	result.node = sem_.Add("sizeof-expression", result.category, Spell(result.type));
	return result;
}

// 5.2.5 [expr.ref] and 9.2: a member access reaches a member of the class the
// object has, and a member of a `const` object is itself const.
Analyzer::Resolved Analyzer::SemMember(int node, int scope)
{
	const string label = Label(node);
	const string op = AfterColon(label);
	const Resolved object = SemExpr(ChildAt(node, 0), scope);
	int class_type = ReferredType(object.type);
	int object_cv = 0;
	if(model_.Get(class_type).kind == kTypeCv)
	{
		object_cv = model_.Get(class_type).quals;
		class_type = model_.Get(class_type).base;
	}
	if(op == "->")
	{
		if(model_.Get(class_type).kind != kTypePointer)
		{
			throw SemanticError("`->` needs a pointer to a class");
		}
		const int pointee = model_.Get(class_type).base;
		if(model_.Get(pointee).kind == kTypeCv)
		{
			object_cv = model_.Get(pointee).quals;
			class_type = model_.Get(pointee).base;
		}
		else
		{
			class_type = pointee;
		}
	}
	// A qualified member name searches the class the qualifier names, which may
	// be a base of the object's class (5.2.5/5).
	const string qualified = Label(ChildAt(node, 1));
	string qualifier;
	string member_name;
	SplitQualifiedName(qualified, qualifier, member_name);
	int class_scope = ClassScopeOf(class_type);
	if(!qualifier.empty())
	{
		class_scope = model_.ResolveQualifier(scope, qualifier);
	}
	if(class_scope < 0)
	{
		throw SemanticError("`" + qualified + "` does not name a class member");
	}
	const int member = model_.LookupValueIn(class_scope, member_name);
	if(member < 0)
	{
		throw SemanticError("no member `" + member_name + "`");
	}
	const Entity record = model_.EntityOf(member);
	Resolved result;
	result.entity = member;
	if(record.kind == kEntityFunction)
	{
		result.type = record.type;
		result.category = kLvalue;
		result.function = member;
	}
	else
	{
		result.type = model_.Qualified(object_cv, ReferredType(record.type));
		result.category = kLvalue;
	}
	// 5.2.5: the dump names the member the operator selected, so the label is
	// the operator's kind and the member's own name.
	const string prefix = label.compare(0, 7, "OP_ARROW") == 0 ? "OP_ARROW:" : "OP_DOT:";
	result.node = sem_.Add("member-expression", result.category, Spell(result.type),
	                       prefix + member_name);
	sem_.AddChild(result.node, object.node);
	return result;
}

// The class scope a class type owns, or -1.
int Analyzer::ClassScopeOf(int class_type) const
{
	int type = class_type;
	if(type < 0)
	{
		return -1;
	}
	if(model_.Get(type).kind == kTypeCv)
	{
		type = model_.Get(type).base;
	}
	if(model_.Get(type).kind != kTypeClass)
	{
		return -1;
	}
	return model_.Get(type).decl_scope;
}

// 8.5.4 [dcl.init.list]: a braced list takes the type of what it initialises.
Analyzer::Resolved Analyzer::SemBracedInit(int node, int scope)
{
	Resolved result;
	result.node = sem_.Add("braced-init-list");
	const vector<int> children = ChildrenOf(node);
	for(size_t index = 0; index < children.size(); ++index)
	{
		const Resolved element = SemExpr(children[index], scope);
		sem_.AddChild(result.node, element.node);
	}
	return result;
}

}  // namespace semantic
}  // namespace cppgm

namespace cppgm
{
namespace semantic
{

// The arguments of a call or functional cast, in source order.
int Analyzer::SemArgumentList(int node, int scope, vector<Resolved>& out)
{
	int list = FindChild(node, "argument-list");
	if(list < 0)
	{
		list = FindChild(node, "paren-argument-list");
	}
	if(list < 0)
	{
		return -1;
	}
	const vector<int> children = ChildrenOf(list);
	for(size_t index = 0; index < children.size(); ++index)
	{
		out.push_back(SemExpr(children[index], scope));
	}
	return list;
}

Analyzer::Resolved Analyzer::SemExpr(int node, int scope)
{
	if(node < 0)
	{
		throw SemanticError("missing expression");
	}
	const string& tag = Tag(node);
	if(tag == "literal")
	{
		return SemLiteral(node, scope);
	}
	if(tag == "keyword-literal")
	{
		return SemKeywordLiteral(node, scope);
	}
	if(tag == "id-expression")
	{
		return SemIdExpression(node, scope);
	}
	if(tag == "parenthesized-expression")
	{
		return SemParenthesized(node, scope);
	}
	if(tag == "unary-expression")
	{
		return SemUnary(node, scope);
	}
	if(tag == "postfix-expression")
	{
		return SemPostfix(node, scope);
	}
	if(tag == "binary-expression")
	{
		return SemBinary(node, scope);
	}
	if(tag == "assignment-expression")
	{
		return SemAssignment(node, scope);
	}
	if(tag == "conditional-expression")
	{
		return SemConditional(node, scope);
	}
	if(tag == "subscript-expression")
	{
		return SemSubscript(node, scope);
	}
	if(tag == "call-expression")
	{
		return SemCall(node, scope);
	}
	if(tag == "cast-expression")
	{
		return SemCast(node, scope);
	}
	if(tag == "sizeof-expression")
	{
		return SemSizeof(node, scope);
	}
	if(tag == "member-expression")
	{
		return SemMember(node, scope);
	}
	if(tag == "braced-init-list")
	{
		return SemBracedInit(node, scope);
	}
	throw SemanticError("expression form `" + tag + "` is outside the supported subset");
}

// 5.2.2 [expr.call]: a call through a function name, a function reference or a
// function pointer.
Analyzer::Resolved Analyzer::SemCall(int node, int scope)
{
	const int callee_node = ChildAt(node, 0);
	vector<Resolved> arguments;
	SemArgumentList(node, scope, arguments);

	if(Tag(callee_node) == "id-expression")
	{
		const string& text = Label(callee_node);
		if(text == "__builtin_constant_p" || text == "__builtin_abort")
		{
			return SemBuiltinCall(node, scope, text);
		}
		vector<Candidate> candidates;
		CollectCandidates(scope, text, candidates);
		if(!candidates.empty())
		{
			return SemNamedCall(node, scope, text, candidates, arguments);
		}
		const Resolved callee = SemExpr(callee_node, scope);
		if(callee.type_name)
		{
			return SemFunctionalCast(node, scope, callee.type, arguments);
		}
		return SemIndirectCall(node, scope, callee, arguments);
	}
	const Resolved callee = SemExpr(callee_node, scope);
	return SemIndirectCall(node, scope, callee, arguments);
}

// 13.3 [over.match]: the candidate set is ranked by the conversion sequences of
// its arguments, and the unique best one is the function the call names.
Analyzer::Resolved Analyzer::SemNamedCall(int node, int scope, const string& text,
                                          const vector<Candidate>& candidates,
                                          const vector<Resolved>& arguments)
{
	struct Ranked
	{
		Candidate candidate;
		vector<Conversion> conversions;
	};
	vector<Ranked> viable;
	for(size_t index = 0; index < candidates.size(); ++index)
	{
		const Type function = model_.Get(candidates[index].type);
		if(function.kind != kTypeFunction)
		{
			continue;
		}
		const size_t fixed = function.params.size();
		// 5.2.2/1: an argument for every parameter without a default, and no
		// argument beyond a list that does not end in `...`.
		if(arguments.size() < fixed || (!function.varargs && arguments.size() > fixed))
		{
			continue;
		}
		Ranked ranked;
		ranked.candidate = candidates[index];
		bool ok = true;
		for(size_t argument = 0; argument < arguments.size(); ++argument)
		{
			Conversion conversion;
			if(argument < fixed)
			{
				conversion = Convert(arguments[argument], function.params[argument], scope);
				if(conversion.rank == 0)
				{
					ok = false;
					break;
				}
			}
			else
			{
				// 13.3.3.1.3: an argument matched by `...` is the worst match.
				conversion.rank = 1;
			}
			ranked.conversions.push_back(conversion);
		}
		if(ok)
		{
			viable.push_back(ranked);
		}
	}
	if(viable.empty())
	{
		throw SemanticError("no matching function for `" + text + "`");
	}
	int best = -1;
	for(size_t index = 0; index < viable.size(); ++index)
	{
		bool beaten = false;
		for(size_t other = 0; other < viable.size(); ++other)
		{
			if(other == index)
			{
				continue;
			}
			if(BetterSequence(viable[other].conversions, viable[index].conversions))
			{
				beaten = true;
				break;
			}
		}
		if(beaten)
		{
			continue;
		}
		if(best >= 0)
		{
			throw SemanticError("ambiguous call to `" + text + "`");
		}
		best = static_cast<int>(index);
	}
	if(best < 0)
	{
		throw SemanticError("no matching function for `" + text + "`");
	}

	const Candidate chosen = viable[static_cast<size_t>(best)].candidate;
	const Type function = model_.Get(chosen.type);
	Resolved result;
	result.type = function.base;
	result.function = chosen.entity;
	if(model_.Get(result.type).kind == kTypeLvalueReference)
	{
		result.category = kLvalue;
	}
	else if(model_.Get(result.type).kind == kTypeRvalueReference)
	{
		result.category = kXvalue;
	}
	else
	{
		result.category = kPrvalue;
	}
	result.node = sem_.Add("call-expression", result.category, Spell(result.type));
	const int callee = sem_.Add("callee", QualifiedEntityName(chosen.entity), true);
	sem_.SetType(callee, BoundSpelling(chosen.type, chosen.scope));
	sem_.AddChild(result.node, callee);
	for(size_t index = 0; index < arguments.size(); ++index)
	{
		// 4.10/1: an integer literal zero that becomes a pointer prints as the
		// pointer it converted to.
		if(arguments[index].null_zero)
		{
			const int target = viable[static_cast<size_t>(best)].conversions[index].target;
			if(target >= 0 && NullPointerTarget(ReferredType(target)) >= 0)
			{
				sem_.SetType(arguments[index].node, Spell(ReferredType(target)));
			}
		}
		sem_.AddChild(result.node, arguments[index].node);
	}
	return result;
}

Analyzer::Resolved Analyzer::SemIndirectCall(int node, int scope, const Resolved& callee,
                                             const vector<Resolved>& arguments)
{
	int function = callee.type;
	if(function >= 0 && model_.Get(function).kind == kTypePointer)
	{
		function = model_.Get(function).base;
	}
	function = ReferredType(function);
	if(function < 0 || model_.Get(function).kind != kTypeFunction)
	{
		throw SemanticError("the callee is not a function");
	}
	const Type record = model_.Get(function);
	const size_t fixed = record.params.size();
	// 5.2.2/1: an indirect call's arity is fixed by the pointer's type.
	if(arguments.size() < fixed || (!record.varargs && arguments.size() > fixed))
	{
		throw SemanticError("the call has the wrong number of arguments");
	}
	for(size_t index = 0; index < arguments.size(); ++index)
	{
		if(index >= fixed)
		{
			continue;
		}
		const Conversion conversion = Convert(arguments[index], record.params[index], scope);
		if(conversion.rank == 0)
		{
			throw SemanticError("an argument does not convert to the parameter type");
		}
	}
	Resolved result;
	result.type = record.base;
	if(model_.Get(result.type).kind == kTypeLvalueReference)
	{
		result.category = kLvalue;
	}
	else if(model_.Get(result.type).kind == kTypeRvalueReference)
	{
		result.category = kXvalue;
	}
	else
	{
		result.category = kPrvalue;
	}
	result.node = sem_.Add("call-expression", result.category, Spell(result.type));
	sem_.AddChild(result.node, callee.node);
	for(size_t index = 0; index < arguments.size(); ++index)
	{
		sem_.AddChild(result.node, arguments[index].node);
	}
	return result;
}

// The two builtins the handout names: `__builtin_constant_p` is folded where
// it is written, and `__builtin_abort` is recognised without requiring its
// later control-flow lowering.
Analyzer::Resolved Analyzer::SemBuiltinCall(int node, int scope, const string& name)
{
	int list = FindChild(node, "argument-list");
	if(list < 0)
	{
		list = FindChild(node, "paren-argument-list");
	}
	const vector<int> arguments = list < 0 ? vector<int>() : ChildrenOf(list);
	Resolved result;
	if(name == "__builtin_constant_p")
	{
		if(arguments.size() != 1)
		{
			throw SemanticError("__builtin_constant_p takes one argument");
		}
		// 5.19: the query is about the propagated constant, so an argument that
		// is not one answers zero rather than failing.
		const Constant value = Evaluate(arguments[0], scope);
		result.type = model_.Fundamental(posttoken::FT_INT);
		result.category = kPrvalue;
		result.node = sem_.Add("literal", result.category, Spell(result.type),
		                       value.valid ? "1" : "0");
		return result;
	}
	if(!arguments.empty())
	{
		throw SemanticError("__builtin_abort takes no arguments");
	}
	if(builtin_abort_ < 0)
	{
		const int global = model_.GlobalScope();
		builtin_abort_ = model_.NewEntity(kEntityFunction, name);
		model_.EntityOf(builtin_abort_).type =
		    model_.Function(model_.Fundamental(posttoken::FT_VOID), vector<int>(), false);
		model_.EntityOf(builtin_abort_).decl_scope = global;
	}
	result.type = model_.Fundamental(posttoken::FT_VOID);
	result.category = kPrvalue;
	result.node = sem_.Add("call-expression", result.category, Spell(result.type));
	const int callee = sem_.Add("callee", name, true);
	sem_.SetType(callee, Spell(model_.EntityOf(builtin_abort_).type));
	sem_.AddChild(result.node, callee);
	return result;
}

// A functional cast: a type name applied to an argument list, which is either a
// conversion of one argument or a value-initialisation.
Analyzer::Resolved Analyzer::SemFunctionalCast(int node, int scope, int target,
                                               const vector<Resolved>& arguments)
{
	if(model_.Get(target).kind == kTypeClass || model_.Get(target).kind == kTypeEnum)
	{
		throw SemanticError("a class or enumeration functional cast is outside the PA7 slice");
	}
	Resolved result;
	result.type = target;
	result.category = kPrvalue;
	if(arguments.empty())
	{
		// 8.5/7: value-initialisation of a scalar is a zero.
		result.node = sem_.Add("literal", result.category, Spell(result.type), "0");
		return result;
	}
	if(arguments.size() != 1)
	{
		throw SemanticError("a functional cast takes one argument");
	}
	if(Convert(arguments[0], target, scope).rank == 0)
	{
		throw SemanticError("the argument does not convert to the cast's type");
	}
	result.node = sem_.Add("cast-expression", result.category, Spell(result.type));
	sem_.AddChild(result.node, arguments[0].node);
	return result;
}

// 5.4 and 5.2.9: the explicit casts the handout's slice reaches.  A cast to a
// reference type yields the operand itself re-typed, which is how the dump
// shows an xvalue cast.
Analyzer::Resolved Analyzer::SemCast(int node, int scope)
{
	const string label = Label(node);
	const int type_node = ChildAt(node, 0);
	const int operand_node = ChildAt(node, 1);
	const int target = BuildDeclarator(type_node, -1, scope);
	const Resolved operand = SemExpr(operand_node, scope);
	Resolved result;
	result.type = target;
	if(model_.Get(target).kind == kTypeLvalueReference)
	{
		result.category = kLvalue;
	}
	else if(model_.Get(target).kind == kTypeRvalueReference)
	{
		result.category = kXvalue;
	}
	else
	{
		result.category = kPrvalue;
	}
	const string word = AfterColon(label);
	const bool static_form = word == "static_cast";
	if(static_form)
	{
		int source = ReferredType(operand.type);
		if(model_.Get(source).kind == kTypeCv)
		{
			source = model_.Get(source).base;
		}
		const bool same = model_.Same(source, ReferredType(target));
		const bool integral_pair = IsIntegralType(source) && IsIntegralType(target);
		const bool arithmetic_pair = IsArithmeticType(source) && IsArithmeticType(target);
		const bool pointer_pair = model_.Get(source).kind == kTypePointer &&
		                          (model_.Get(ReferredType(target)).kind == kTypePointer ||
		                           model_.Get(ReferredType(target)).kind == kTypeMemberPointer);
		const bool null_source = operand.null_zero ||
		    (model_.Get(source).kind == kTypeFundamental &&
		     model_.Get(source).base == posttoken::FT_NULLPTR_T);
		const bool null_to_pointer = null_source &&
		    NullPointerTarget(ReferredType(target)) >= 0;
		const bool to_bool = model_.Get(ReferredType(target)).kind == kTypeFundamental &&
		                     model_.Get(ReferredType(target)).base == posttoken::FT_BOOL &&
		                     IsScalarType(source);
		const bool void_cast = model_.Get(ReferredType(target)).kind == kTypeFundamental &&
		                       model_.Get(ReferredType(target)).base == posttoken::FT_VOID;
		if(!same && !integral_pair && !arithmetic_pair && !pointer_pair &&
		   !null_to_pointer && !to_bool && !void_cast)
		{
			throw SemanticError("`static_cast` cannot perform this conversion");
		}
	}
	if(IsReferenceType(target))
	{
		// 5.2.9/4: a cast to a reference type denotes the operand itself with
		// the reference's type, which is how the dump shows an xvalue cast.
		sem_.SetType(operand.node, Spell(result.type));
		sem_.SetCategory(operand.node, result.category);
		result.node = operand.node;
		return result;
	}
	result.node = sem_.Add("cast-expression", result.category, Spell(result.type),
	                       label);
	sem_.AddChild(result.node, operand.node);
	return result;
}

}  // namespace semantic
}  // namespace cppgm

namespace cppgm
{
namespace semantic
{

namespace
{

const char* ClassKeyWord(int key)
{
	switch(key)
	{
	case kClassKeyStruct:
		return "struct";
	case kClassKeyUnion:
		return "union";
	default:
		return "class";
	}
}

const char* EnumKeyWord(int key)
{
	switch(key)
	{
	case kEnumKeyClass:
		return "enum class";
	case kEnumKeyStruct:
		return "enum struct";
	default:
		return "enum";
	}
}

}  // namespace

// The PA7 spelling of a type.  It differs from the PA6 one in one place: a
// class type prints the qualified name of the scope it is a member of, so a
// nested class is `struct N::S` rather than `struct S`.
string Analyzer::Spell(int id) const
{
	if(id < 0)
	{
		return string();
	}
	const Type& type = model_.Get(id);
	switch(type.kind)
	{
	case kTypeFundamental:
		return kFundamentalNames[type.base];
	case kTypeClass:
	{
		const string qualified = QualifiedScopeName(type.decl_scope);
		return string(ClassKeyWord(type.class_key)) + " " +
		       (qualified.empty() ? type.name : qualified);
	}
	case kTypeEnum:
	{
		// An enumeration prints its own name, so a qualified definition of a
		// member enumeration reads as the enumeration rather than its path.
		const size_t split = type.name.rfind("::");
		const string name = split == string::npos ? type.name
		                                          : type.name.substr(split + 2);
		return string(EnumKeyWord(type.enum_key)) + " " + name;
	}
	case kTypeTemplateParameter:
		return string(type.class_key != 0 ? "template-parameter " : "typename ") + type.name;
	case kTypeCv:
		if(type.quals == 3)
		{
			return "const volatile " + Spell(type.base);
		}
		return string(type.quals == 1 ? "const " : "volatile ") + Spell(type.base);
	case kTypePointer:
		return "pointer to " + Spell(type.base);
	case kTypeLvalueReference:
		return "lvalue-reference to " + Spell(type.base);
	case kTypeRvalueReference:
		return "rvalue-reference to " + Spell(type.base);
	case kTypeArray:
	{
		ostringstream out;
		out << "array of " << (type.bound < 0 ? 0 : type.bound) << ' ' << Spell(type.base);
		return out.str();
	}
	case kTypeMemberPointer:
		return "member-pointer of " + Spell(type.base) + " to " + Spell(type.member);
	case kTypeFunction:
	{
		string text = "function of (";
		for(size_t index = 0; index < type.params.size(); ++index)
		{
			if(index != 0)
			{
				text += ", ";
			}
			// 8.3.5/5: the signature is the adjusted parameter-type-list.
			text += Spell(model_.AdjustParameter(type.params[index]));
		}
		if(type.varargs)
		{
			if(!type.params.empty())
			{
				text += ", ";
			}
			text += "...";
		}
		text += ')';
		if(type.quals == 3)
		{
			text += " const volatile";
		}
		else if(type.quals == 1)
		{
			text += " const";
		}
		else if(type.quals == 2)
		{
			text += " volatile";
		}
		if(type.func_ref == 1)
		{
			text += " &";
		}
		else if(type.func_ref == 2)
		{
			text += " &&";
		}
		text += " returning ";
		text += Spell(type.base);
		return text;
	}
	}
	return string();
}

}  // namespace semantic
}  // namespace cppgm
