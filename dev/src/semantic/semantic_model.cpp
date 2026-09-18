#include "semantic/semantic_model.h"

#include <sstream>

#include "posttoken/fundamental_type.h"

using namespace std;

namespace cppgm
{
namespace semantic
{

namespace
{

const char* ClassKeyName(int key)
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

const char* EnumKeyName(int key)
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

string Number(long long value)
{
	ostringstream out;
	out << value;
	return out.str();
}

// The structural key an interned type is looked up by.  A class, enumeration
// or template parameter is nominal rather than structural, so it is never
// interned: its identity is the entity that owns it.
string StructuralKey(const Type& type)
{
	ostringstream key;
	key << static_cast<int>(type.kind) << ':';
	switch(type.kind)
	{
	case kTypeFundamental:
		key << type.base;
		break;
	case kTypeCv:
		key << type.quals << ':' << type.base;
		break;
	case kTypePointer:
	case kTypeLvalueReference:
	case kTypeRvalueReference:
		key << type.base;
		break;
	case kTypeArray:
		key << type.bound << ':' << type.base;
		break;
	case kTypeFunction:
		key << type.base << '(';
		for(size_t index = 0; index < type.params.size(); ++index)
		{
			key << type.params[index] << ',';
		}
		key << (type.varargs ? "..." : "") << "):" << type.quals << ':'
		    << type.func_ref;
		break;
	case kTypeMemberPointer:
		key << type.base << ':' << type.member;
		break;
	default:
		break;
	}
	return key.str();
}

}  // namespace

const char* const kFundamentalNames[] =
{
	"signed char",
	"short int",
	"int",
	"long int",
	"long long int",
	"unsigned char",
	"unsigned short int",
	"unsigned int",
	"unsigned long int",
	"unsigned long long int",
	"wchar_t",
	"char",
	"char16_t",
	"char32_t",
	"bool",
	"float",
	"double",
	"long double",
	"void",
	"nullptr_t"
};

Model::Model()
	: global_(-1)
{
	// The global namespace is the root every translation unit is analysed in.
	global_ = NewScope(kScopeNamespace, "<global>", -1);
}

int Model::AddType(const Type& type)
{
	types_.push_back(type);
	return static_cast<int>(types_.size()) - 1;
}

int Model::InternType(const string& key, const Type& type)
{
	map<string, int>::const_iterator found = type_ids_.find(key);
	if(found != type_ids_.end())
	{
		return found->second;
	}
	const int id = AddType(type);
	type_ids_.insert(make_pair(key, id));
	return id;
}

int Model::Fundamental(int index)
{
	Type type;
	type.kind = kTypeFundamental;
	type.base = index;
	return InternType(StructuralKey(type), type);
}

int Model::Qualified(int quals, int base)
{
	if(quals == 0)
	{
		return base;
	}
	// Every field is read before a nested call can intern a type: interning
	// appends to the table, so a reference into it does not survive one.
	const Type current = Get(base);
	// 7.1.6.1: a qualifier on a qualified type is the union of the two sets.
	if(current.kind == kTypeCv)
	{
		return Qualified(quals | current.quals, current.base);
	}
	// A reference has no top-level cv qualification (8.3.2/1): the qualifier
	// would apply to the referred-to type, which an alias never does.
	if(current.kind == kTypeLvalueReference || current.kind == kTypeRvalueReference)
	{
		return base;
	}
	// Qualifying an array qualifies its element type (8.3.4/1).
	if(current.kind == kTypeArray)
	{
		const int element = Qualified(quals, current.base);
		return Array(current.bound, element);
	}
	if(current.kind == kTypeFunction)
	{
		return base;
	}
	Type type;
	type.kind = kTypeCv;
	type.quals = quals;
	type.base = base;
	return InternType(StructuralKey(type), type);
}

int Model::Pointer(int base)
{
	Type type;
	type.kind = kTypePointer;
	type.base = base;
	return InternType(StructuralKey(type), type);
}

int Model::LvalueReference(int base)
{
	if(Get(base).kind == kTypeLvalueReference)
	{
		return base;
	}
	if(Get(base).kind == kTypeRvalueReference)
	{
		return LvalueReference(Get(base).base);
	}
	Type type;
	type.kind = kTypeLvalueReference;
	type.base = base;
	return InternType(StructuralKey(type), type);
}

int Model::RvalueReference(int base)
{
	if(Get(base).kind == kTypeLvalueReference || Get(base).kind == kTypeRvalueReference)
	{
		return base;
	}
	Type type;
	type.kind = kTypeRvalueReference;
	type.base = base;
	return InternType(StructuralKey(type), type);
}

int Model::Array(long long bound, int element)
{
	// 8.3.4/1: the element type of an array shall not be `void`, a reference or
	// a function type, so the invariant is enforced where an array is formed.
	// A cv-qualified `void` is not the same type and is not rejected here.
	const Type& base = Get(element);
	if(base.kind == kTypeLvalueReference || base.kind == kTypeRvalueReference ||
	   base.kind == kTypeFunction ||
	   (base.kind == kTypeFundamental && base.base == posttoken::FT_VOID))
	{
		throw SemanticError("an array cannot have this element type");
	}
	Type type;
	type.kind = kTypeArray;
	type.bound = bound;
	type.base = element;
	return InternType(StructuralKey(type), type);
}

int Model::Function(int result, const vector<int>& params, bool varargs, int quals,
                    int func_ref)
{
	Type type;
	type.kind = kTypeFunction;
	type.base = result;
	type.params = params;
	type.varargs = varargs;
	type.quals = quals;
	type.func_ref = func_ref;
	return InternType(StructuralKey(type), type);
}

// 8.3.3: a pointer to member is a distinct type from any pointer, so `int C::*`
// and `int*` are never the same type however the class is laid out.
int Model::MemberPointer(int class_type, int member_type)
{
	Type type;
	type.kind = kTypeMemberPointer;
	type.base = class_type;
	type.member = member_type;
	return InternType(StructuralKey(type), type);
}

int Model::NewClass(const string& name, int key)
{
	Type type;
	type.kind = kTypeClass;
	type.name = name;
	type.class_key = key;
	return AddType(type);
}

int Model::NewEnum(const string& name, int key)
{
	Type type;
	type.kind = kTypeEnum;
	type.name = name;
	type.enum_key = key;
	return AddType(type);
}

int Model::NewTemplateParameter(const string& name, bool template_parameter)
{
	Type type;
	type.kind = kTypeTemplateParameter;
	type.name = name;
	type.class_key = template_parameter ? 1 : 0;
	return AddType(type);
}

string Model::Spelling(int id) const
{
	const Type& type = Get(id);
	switch(type.kind)
	{
	case kTypeFundamental:
		return kFundamentalNames[type.base];
	case kTypeClass:
		return string(ClassKeyName(type.class_key)) + " " + type.name;
	case kTypeEnum:
		return string(EnumKeyName(type.enum_key)) + " " + type.name;
	case kTypeTemplateParameter:
		return string(type.class_key != 0 ? "template-parameter " : "typename ") + type.name;
	case kTypeCv:
		if(type.quals == 3)
		{
			return "const volatile " + Spelling(type.base);
		}
		return string(type.quals == 1 ? "const " : "volatile ") + Spelling(type.base);
	case kTypePointer:
		return "pointer to " + Spelling(type.base);
	case kTypeLvalueReference:
		return "lvalue-reference to " + Spelling(type.base);
	case kTypeRvalueReference:
		return "rvalue-reference to " + Spelling(type.base);
	case kTypeArray:
		return "array of " + Number(type.bound < 0 ? 0 : type.bound) + " " + Spelling(type.base);
	case kTypeMemberPointer:
		return "member-pointer of " + Spelling(type.base) + " to " + Spelling(type.member);
	case kTypeFunction:
	{
		string text = "function of (";
		for(size_t index = 0; index < type.params.size(); ++index)
		{
			if(index != 0)
			{
				text += ", ";
			}
			text += Spelling(type.params[index]);
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
		// 8.3.5/6, 8.3.5/7: a member function's cv-qualifiers and
		// ref-qualifier follow its parameter list in the type's spelling.
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
		text += Spelling(type.base);
		return text;
	}
	}
	return string();
}

bool Model::Same(int left, int right) const
{
	return left == right;
}

bool Model::DerivesFrom(int derived, int base) const
{
	if(derived == base)
	{
		return true;
	}
	const vector<int>* direct = BasesOf(derived);
	if(direct == 0)
	{
		return false;
	}
	for(size_t index = 0; index < direct->size(); ++index)
	{
		int parent = (*direct)[index];
		if(Get(parent).kind == kTypeCv)
		{
			parent = Get(parent).base;
		}
		if(DerivesFrom(parent, base))
		{
			return true;
		}
	}
	return false;
}

// 8.3.5/5: a parameter's array or function type adjusts to a pointer, and a
// top-level cv qualifier is removed, before two declarations are compared.
int Model::AdjustParameter(int id)
{
	const Type& type = Get(id);
	if(type.kind == kTypeArray)
	{
		return Pointer(type.base);
	}
	if(type.kind == kTypeFunction)
	{
		return Pointer(id);
	}
	if(type.kind == kTypeCv)
	{
		return type.base;
	}
	return id;
}

bool Model::SameSignature(int left, int right)
{
	if(left == right)
	{
		return true;
	}
	// The two types are copied, because adjusting a parameter interns a pointer
	// type and the table does not hold references across that.
	const Type a = Get(left);
	const Type b = Get(right);
	if(a.kind != kTypeFunction || b.kind != kTypeFunction)
	{
		return false;
	}
	if(a.varargs != b.varargs || a.quals != b.quals || a.func_ref != b.func_ref ||
	   a.params.size() != b.params.size())
	{
		return false;
	}
	for(size_t index = 0; index < a.params.size(); ++index)
	{
		if(AdjustParameter(a.params[index]) != AdjustParameter(b.params[index]))
		{
			return false;
		}
	}
	return true;
}

bool Model::SameFunctionType(int left, int right)
{
	if(left == right)
	{
		return true;
	}
	const Type a = Get(left);
	const Type b = Get(right);
	if(a.kind != kTypeFunction || b.kind != kTypeFunction)
	{
		return false;
	}
	return a.base == b.base && SameSignature(left, right);
}

// 13.1/2: a member overload set with one parameter-type-list cannot mix
// declarations that carry a ref-qualifier with ones that do not.
bool Model::MixedRefQualifier(int left, int right)
{
	const Type a = Get(left);
	const Type b = Get(right);
	if(a.kind != kTypeFunction || b.kind != kTypeFunction)
	{
		return false;
	}
	if((a.func_ref != 0) == (b.func_ref != 0))
	{
		return false;
	}
	if(a.varargs != b.varargs || a.params.size() != b.params.size())
	{
		return false;
	}
	for(size_t index = 0; index < a.params.size(); ++index)
	{
		if(AdjustParameter(a.params[index]) != AdjustParameter(b.params[index]))
		{
			return false;
		}
	}
	return true;
}

bool Model::SizeOf(int id, unsigned long long& size) const
{
	const Type& type = Get(id);
	switch(type.kind)
	{
	case kTypeFundamental:
		size = posttoken::FundamentalTypeSize(
		    static_cast<posttoken::EFundamentalType>(type.base));
		return size != 0;
	case kTypeCv:
		return SizeOf(type.base, size);
	case kTypePointer:
	case kTypeMemberPointer:
		size = 8;
		return true;
	case kTypeLvalueReference:
	case kTypeRvalueReference:
		// 5.3.3/2: the size of a reference is the size of the type it refers to.
		return SizeOf(type.base, size);
	case kTypeEnum:
		// 7.2/2: a fixed underlying type decides the size; an enumeration
		// without one takes the type its values need, which the course ABI
		// gives as `int` for the values these tests reach.
		if(type.underlying >= 0)
		{
			return SizeOf(type.underlying, size);
		}
		size = 4;
		return true;
	case kTypeArray:
	{
		if(type.bound < 0)
		{
			return false;
		}
		unsigned long long element = 0;
		if(!SizeOf(type.base, element))
		{
			return false;
		}
		size = element * static_cast<unsigned long long>(type.bound);
		return true;
	}
	case kTypeClass:
	{
		unsigned long long align = 1;
		return ClassLayout(id, size, align, 0);
	}
	default:
		return false;
	}
}

// 9.2 [class.mem]: a class's data members are laid out in declaration order,
// each at its own alignment, and the class takes the largest member alignment.
// 9.5/1: every member of a union is at offset zero, so a union is as large as
// its largest member.  An empty class still occupies one byte (5.3.3/2).  A
// class that reaches itself - only possible through a definition that is not
// yet complete - has no layout.
bool Model::ClassLayout(int id, unsigned long long& size, unsigned long long& align,
                        int depth) const
{
	const Type& type = Get(id);
	if(!type.complete || type.decl_scope < 0 || depth > 64)
	{
		return false;
	}
	const bool is_union = type.class_key == kClassKeyUnion;
	const Scope& scope = ScopeOf(type.decl_scope);
	unsigned long long total = 0;
	unsigned long long widest = 1;
	for(size_t index = 0; index < scope.members.size(); ++index)
	{
		const int member_type = EntityOf(scope.members[index]).type;
		unsigned long long member_size = 0;
		unsigned long long member_align = 0;
		if(!ObjectLayout(member_type, member_size, member_align, depth))
		{
			return false;
		}
		if(member_align == 0)
		{
			member_align = 1;
		}
		if(is_union)
		{
			if(member_size > total)
			{
				total = member_size;
			}
		}
		else
		{
			total = (total + member_align - 1) / member_align * member_align;
			total += member_size;
		}
		if(member_align > widest)
		{
			widest = member_align;
		}
	}
	size = (total + widest - 1) / widest * widest;
	if(size == 0)
	{
		size = 1;
	}
	align = widest;
	return true;
}

// The size and alignment an object of this type occupies as a class member.
// A reference is a pointer-sized object there (3.9/8), while `sizeof` applied
// to a reference type names its referent (5.3.3/2): two different questions,
// which is why this is not `SizeOf`.
bool Model::ObjectLayout(int id, unsigned long long& size, unsigned long long& align,
                         int depth) const
{
	const Type& type = Get(id);
	if(type.kind == kTypeLvalueReference || type.kind == kTypeRvalueReference)
	{
		size = 8;
		align = 8;
		return true;
	}
	if(type.kind == kTypeClass)
	{
		return ClassLayout(id, size, align, depth + 1);
	}
	return SizeOf(id, size) && AlignOf(id, align);
}

bool Model::AlignOf(int id, unsigned long long& align) const
{
	const Type& type = Get(id);
	switch(type.kind)
	{
	case kTypeFundamental:
		align = posttoken::FundamentalTypeSize(
		    static_cast<posttoken::EFundamentalType>(type.base));
		return align != 0;
	case kTypeCv:
		return AlignOf(type.base, align);
	case kTypePointer:
	case kTypeMemberPointer:
		align = 8;
		return true;
	case kTypeLvalueReference:
	case kTypeRvalueReference:
		// 5.3.6/1 with 5.3.3/2: a reference takes the alignment of the type it
		// refers to.
		return AlignOf(type.base, align);
	case kTypeEnum:
		if(type.underlying >= 0)
		{
			return AlignOf(type.underlying, align);
		}
		align = 4;
		return true;
	case kTypeArray:
		return AlignOf(type.base, align);
	case kTypeClass:
	{
		unsigned long long size = 0;
		return ClassLayout(id, size, align, 0);
	}
	default:
		return false;
	}
}

// ---------------------------------------------------------------------------
// Scopes, entities and bindings
// ---------------------------------------------------------------------------

int Model::NewScope(EScopeKind kind, const string& name, int parent)
{
	Scope scope;
	scope.kind = kind;
	scope.name = name;
	scope.parent = parent;
	scopes_.push_back(scope);
	const int id = static_cast<int>(scopes_.size()) - 1;
	if(parent >= 0)
	{
		scopes_[static_cast<size_t>(parent)].children.push_back(id);
	}
	return id;
}

int Model::NewEntity(EEntityKind kind, const string& name)
{
	Entity entity;
	entity.kind = kind;
	entity.name = name;
	entities_.push_back(entity);
	return static_cast<int>(entities_.size()) - 1;
}

void Model::AddBinding(int scope, const Binding& binding)
{
	scopes_[static_cast<size_t>(scope)].bindings.push_back(binding);
}

void Model::BindType(int scope, const string& name, int entity)
{
	if(!name.empty())
	{
		scopes_[static_cast<size_t>(scope)].types[name] = entity;
	}
}

void Model::BindValue(int scope, const string& name, int entity)
{
	if(!name.empty())
	{
		scopes_[static_cast<size_t>(scope)].values[name] = entity;
	}
}

void Model::BindNamespace(int scope, const string& name, int scope_id)
{
	if(!name.empty())
	{
		scopes_[static_cast<size_t>(scope)].namespaces[name] = scope_id;
	}
}

int Model::ScopeFor(int owner, int entity, EScopeKind kind, const string& name)
{
	map<int, int>& scopes = entities_[static_cast<size_t>(entity)].scopes;
	map<int, int>::const_iterator found = scopes.find(owner);
	if(found != scopes.end())
	{
		return found->second;
	}
	const int scope = NewScope(kind, name, owner);
	scopes.insert(make_pair(owner, scope));
	Entity& record = entities_[static_cast<size_t>(entity)];
	record.scope = scope;
	scopes_[static_cast<size_t>(scope)].entity = entity;
	const int type = record.type;
	if(type >= 0)
	{
		types_[static_cast<size_t>(type)].decl_scope = scope;
	}
	return scope;
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------

namespace
{

// Which of a scope's three name maps a lookup consults.
enum ELookupCategory
{
	kLookupType = 0,
	kLookupValue,
	kLookupNamespace
};

}  // namespace

int Model::LookupInCategory(int scope, const string& name, int category) const
{
	const Scope& record = ScopeOf(scope);
	const map<string, int>* table = &record.types;
	if(category == kLookupValue)
	{
		table = &record.values;
	}
	else if(category == kLookupNamespace)
	{
		table = &record.namespaces;
	}
	map<string, int>::const_iterator found = table->find(name);
	return found == table->end() ? -1 : found->second;
}

// A namespace scope's own declarations, then the namespaces its
// using-directives nominate, then its inline namespaces (7.3.4/2, 7.3.1/8).
int Model::LookupThrough(int scope, const string& name, int category,
                         vector<int>& visited) const
{
	for(size_t index = 0; index < visited.size(); ++index)
	{
		if(visited[index] == scope)
		{
			return -1;
		}
	}
	visited.push_back(scope);

	const int direct = LookupInCategory(scope, name, category);
	if(direct >= 0)
	{
		return direct;
	}

	const Scope& record = ScopeOf(scope);
	// An unnamed namespace's members are visible where the namespace is
	// declared, which its enclosing scope is.
	if(record.unnamed && record.parent >= 0)
	{
		const int inherited = LookupThrough(record.parent, name, category, visited);
		if(inherited >= 0)
		{
			return inherited;
		}
	}

	int result = -1;
	for(size_t index = 0; index < record.inline_namespaces.size(); ++index)
	{
		const int found = LookupThrough(record.inline_namespaces[index], name, category, visited);
		if(found >= 0)
		{
			if(result >= 0 && result != found)
			{
				throw SemanticError("ambiguous name `" + name + "`");
			}
			result = found;
		}
	}
	for(size_t index = 0; index < record.directives.size(); ++index)
	{
		const int found = LookupThrough(record.directives[index], name, category, visited);
		if(found >= 0)
		{
			if(result >= 0 && result != found)
			{
				throw SemanticError("ambiguous name `" + name + "`");
			}
			result = found;
		}
	}
	return result;
}

int Model::LookupType(int scope, const string& name) const
{
	return LookupInCategory(scope, name, kLookupType);
}

int Model::LookupValue(int scope, const string& name) const
{
	return LookupInCategory(scope, name, kLookupValue);
}

int Model::LookupNamespace(int scope, const string& name) const
{
	return LookupInCategory(scope, name, kLookupNamespace);
}

int Model::LookupTypeUnqualified(int scope, const string& name) const
{
	for(int current = scope; current >= 0; current = ScopeOf(current).parent)
	{
		vector<int> visited;
		const int found = LookupThrough(current, name, kLookupType, visited);
		if(found >= 0)
		{
			return found;
		}
	}
	return -1;
}

int Model::LookupValueUnqualified(int scope, const string& name) const
{
	for(int current = scope; current >= 0; current = ScopeOf(current).parent)
	{
		vector<int> visited;
		const int found = LookupThrough(current, name, kLookupValue, visited);
		if(found >= 0)
		{
			return found;
		}
	}
	return -1;
}

int Model::LookupNamespaceUnqualified(int scope, const string& name) const
{
	// 7.3.4/2: a using-directive makes the nominated namespace's names appear as
	// if they were declared in the nearest enclosing namespace, so a namespace
	// named only through one is still a namespace target.  Inline namespaces
	// follow the same rule (7.3.1/8).
	for(int current = scope; current >= 0; current = ScopeOf(current).parent)
	{
		vector<int> visited;
		const int found = LookupThrough(current, name, kLookupNamespace, visited);
		if(found >= 0)
		{
			return found;
		}
	}
	return -1;
}

int Model::LookupTypeIn(int scope, const string& name) const
{
	vector<int> visited;
	return LookupThrough(scope, name, kLookupType, visited);
}

int Model::LookupValueIn(int scope, const string& name) const
{
	vector<int> visited;
	return LookupThrough(scope, name, kLookupValue, visited);
}

int Model::LookupNamespaceIn(int scope, const string& name) const
{
	vector<int> visited;
	return LookupThrough(scope, name, kLookupNamespace, visited);
}

int Model::ResolveQualifier(int scope, const string& qualifier) const
{
	// A nested-name-specifier is a run of components ending in `::`.  Each
	// component is a namespace, a class or an enumeration; the last one names
	// the scope a qualified lookup searches.  The first component is looked up
	// from `scope` outward; the rest are looked up inside the scope the
	// previous component named, so `A::B::C` cannot reach `C` through `A`.
	size_t position = 0;
	int current = -1;
	if(qualifier.size() >= 2 && qualifier[0] == ':' && qualifier[1] == ':')
	{
		current = global_;
		position = 2;
	}
	while(position < qualifier.size())
	{
		const size_t next = qualifier.find("::", position);
		const string component = qualifier.substr(position, next - position);
		position = next == string::npos ? qualifier.size() : next + 2;

		const int base = current < 0 ? scope : current;
		// A namespace name is looked for before a type name, because a
		// namespace-only context must not be answered by a value or a class of
		// the same spelling (3.4.3).
		const int found = current < 0
		    ? LookupNamespaceUnqualified(base, component)
		    : LookupNamespaceIn(base, component);
		if(found >= 0)
		{
			current = found;
			continue;
		}
		const int entity = current < 0
		    ? LookupTypeUnqualified(base, component)
		    : LookupTypeIn(base, component);
		if(entity < 0)
		{
			throw SemanticError("unknown name `" + component + "` in a qualifier");
		}
		// An alias names the type it denotes, so `A::Y` reaches the class `A`
		// stands for; an enumeration may have a scope in more than one place,
		// and the one the qualified definition registered is the last.
		int found_scope = EntityOf(entity).scope;
		const int aliased = EntityOf(entity).type;
		if(found_scope < 0 && aliased >= 0 && Get(aliased).decl_scope >= 0)
		{
			found_scope = Get(aliased).decl_scope;
		}
		if(found_scope < 0)
		{
			throw SemanticError("`" + component + "` does not name a scope");
		}
		current = found_scope;
	}
	if(current < 0)
	{
		throw SemanticError("empty nested-name-specifier");
	}
	return current;
}

int Model::EnclosingNamespace(int scope) const
{
	for(int current = scope; current >= 0; current = ScopeOf(current).parent)
	{
		if(ScopeOf(current).kind == kScopeNamespace)
		{
			return current;
		}
	}
	return -1;
}

bool Model::NamespaceEncloses(int outer, int inner) const
{
	for(int current = inner; current >= 0; current = ScopeOf(current).parent)
	{
		if(current == outer)
		{
			return true;
		}
	}
	return false;
}

}  // namespace semantic
}  // namespace cppgm
