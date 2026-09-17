#!/usr/bin/perl
#
# Differential check for the pa6 scope/type stage.
#
# Runs the student compiler and the reference wrapper over a curated set of
# reduced reproducers and reports any difference in exit status or in the scope
# dump.  This is a personal test: it is not part of the course contract and is
# not discovered by `make test`.
#
#   perl student.tests/types_differential.pl
#
# The curated list holds the reduced reproducers for the corners the checked-in
# fixtures reach only in passing: what a qualified definition of a class member
# does to the scope it is written in and to the scope it names, which scope a
# lookup walks through when a using-directive is transitive, how far a
# declaration's point reaches into a later alias, and which of the declaration
# forms the handout requires PA6 to reject.  Each case says what the two
# readings of it are, because the point of the case is the choice.
#
# A difference is reported, never repaired: the reference is the oracle, and a
# case the reference itself reads oddly is a review question rather than a
# target.  The known differences below are listed and not counted as failures.

use strict;
use warnings;

use Cwd qw(abs_path);
use File::Basename qw(dirname);

my $root = dirname(dirname(abs_path($0)));
my $mine = "$root/dev/cppgm++";
my $reference = "$root/reference-binaries/cppgm++";

# name => [ what the case decides, source ]
my %cases = (
	'qualified-enum-definition-registers-twice' => [ 'a qualified definition names a member and registers the qualified name where it is written', <<'SRC' ],
namespace N { enum class E : int; }
enum class N::E : int { a = 1 };
N::E x;
SRC
	'elaborated-class-hidden-by-a-function' => [ 'an elaborated specifier may find a type an ordinary-name binding hides (3.4.4/2)', <<'SRC' ],
struct record { int value; };
int record(const char*);
struct record* result;
SRC
	'elaborated-enum-must-exist' => [ 'an elaborated enumeration specifier names an existing enumeration', <<'SRC' ],
struct S { enum E member; };
SRC
	'using-directive-is-transitive' => [ 'a directive nominates a namespace, not a copy of its contents', <<'SRC' ],
namespace A { typedef int TA; }
namespace B { using namespace A; }
namespace C { using namespace B; TA x; }
namespace A { typedef char TB; }
namespace C { TB y; }
SRC
	'namespace-target-is-not-a-value' => [ 'a value of the same spelling does not hide a namespace target', <<'SRC' ],
namespace N2 { typedef int T; }
namespace M { int N2; using namespace N2; T t; }
SRC
	'declaration-point-keeps-the-earlier-alias' => [ 'a later alias does not retroactively change an earlier binding', <<'SRC' ],
typedef int T;
T a;
typedef char T;
T b;
SRC
	'complete-class-context' => [ 'a member body sees a member type declared later (3.3.7/1)', <<'SRC' ],
struct holder {
  int f() { later value = 1; return value; }
  typedef int later;
};
SRC
	'anonymous-union-injects-members' => [ "an anonymous union's members are members of the enclosing scope (9.5/3)", <<'SRC' ],
static union { int t; long a; };
int f(int x) { t = x; return (int)a; }
SRC
	'array-completion-through-one-entity' => [ 'a later declaration completes an incomplete array (8.3.4/1)', <<'SRC' ],
extern int a[];
int a[10];
extern int a[];
SRC
	'reference-collapsing-through-an-alias' => [ 'a reference alias collapses with another reference (8.3.2/6)', <<'SRC' ],
typedef int& LRI;
typedef int&& RRI;
extern LRI& a;
extern LRI&& b;
extern RRI& c;
extern RRI&& d;
SRC
	'parameter-source-types-are-kept' => [ 'the dump keeps the source parameter types', <<'SRC' ],
void f(const int);
void g(void x(int), int* y);
void g(void (*)(int), int y[3]);
SRC
	'constant-short-circuit' => [ 'the operand not selected is not evaluated (5.14/1)', <<'SRC' ],
static_assert(1 || (1 / 0), "ok");
static_assert(!(0 && (1 / 0)), "ok");
SRC
	'signed-constant-overflow' => [ 'signed overflow in a constant expression is ill formed', <<'SRC' ],
static_assert(9223372036854775807LL + 1, "overflow");
SRC
	'scoped-enum-does-not-compare-with-an-integer' => [ 'a scoped enumeration does not convert to an integer (7.2/9)', <<'SRC' ],
enum class FY { X, Y = 3 };
static_assert(FY::Y == 3, "ok");
SRC
	'scoped-enum-compares-with-itself' => [ 'a scoped enumeration compares with the same enumeration', <<'SRC' ],
enum class FY { X, Y = 3 };
using GY = FY;
static_assert(GY::Y == FY::Y, "ok");
SRC
	'opaque-unscoped-enum-is-rejected' => [ 'an opaque unscoped enumeration has no underlying type to infer', <<'SRC' ],
enum FY;
SRC
	'opaque-enum-underlying-must-agree' => [ 'an enumeration has one underlying type (7.2/2)', <<'SRC' ],
enum E : int;
enum E : long;
SRC
	'non-enclosing-qualified-definition' => [ 'a qualified definition must be in an enclosing namespace (7.3.1.2/2)', <<'SRC' ],
namespace A { extern int x; }
namespace B { int A::x; }
SRC
	'enclosing-qualified-definition' => [ 'the global namespace encloses every namespace (7.3.1.2/2)', <<'SRC' ],
namespace A { extern int x; }
int A::x = 1;
SRC
	'namespace-alias-cannot-be-reopened' => [ 'an alias is not a namespace that can be extended (7.3.2/3)', <<'SRC' ],
namespace A { typedef int T; }
namespace B = A;
namespace B { }
SRC
	'alias-of-a-class-is-a-qualifier' => [ 'an alias names the type it denotes, including its members', <<'SRC' ],
struct C { typedef int Y; };
using A = C;
A::Y x;
SRC
	'void-object-is-rejected' => [ 'void is an incomplete type no object may have (3.9.1/9)', <<'SRC' ],
void x;
SRC
	'uninitialized-reference-is-rejected' => [ 'a reference needs an initializer except in four places (8.3.2/5)', <<'SRC' ],
int& r;
SRC
	'extern-reference-is-allowed' => [ 'an explicit extern is one of the four places', <<'SRC' ],
extern int& r;
SRC
	'pointer-to-reference-is-rejected' => [ 'there are no pointers to references (8.3.2/5)', <<'SRC' ],
typedef int& R;
R* g;
SRC
	'decltype-parenthesized-lvalue' => [ 'a parenthesized lvalue names a reference (7.1.6.2/4)', <<'SRC' ],
int x;
decltype(x) y;
decltype((x)) z;
SRC
	'decltype-of-an-enumerator-is-not-a-reference' => [ 'an enumerator is a prvalue', <<'SRC' ],
enum class E { value };
decltype((E::value)) object;
SRC
	'inline-namespace-is-searched' => [ 'an inline namespace is a transparent member (7.3.1/8)', <<'SRC' ],
namespace I0 {
  inline namespace I1 { typedef int TI; namespace Deep { typedef char DC; } }
}
I0::TI a;
I0::Deep::DC d;
SRC
	'namespace-scope-anonymous-union-must-be-static' => [ 'an unnamed union at namespace scope has no linkage without static (9.5/2)', <<'SRC' ],
union { int t; };
SRC
	'array-bound-must-be-positive' => [ 'a bound must convert to a positive size', <<'SRC' ],
void f() { int values[0]; }
SRC
	'class-layout-decides-sizeof' => [ 'members are laid out in declaration order at their own alignment (9.2)', <<'SRC' ],
struct C { char a[3]; short s; int i; };
int laid_out[sizeof(C)];
int aligned[alignof(C)];
SRC
	'empty-class-still-occupies-one-byte' => [ 'a class with no members has size one (5.3.3/2)', <<'SRC' ],
struct C {};
int laid_out[sizeof(C)];
SRC
	'sizeof-incomplete-class' => [ 'sizeof of an incomplete type is ill formed', <<'SRC' ],
struct C;
int a[sizeof(C)];
SRC
	'cross-unit-constant-is-not-visible' => [ 'a translation unit is analysed on its own', <<'SRC' ],
extern const int bound;
int values[bound];
SRC
	'union-is-as-large-as-its-largest-member' => [ 'every member of a union is at offset zero (9.5/1)', <<'SRC' ],
union U { int a; char c[8]; };
int wide[sizeof(U)];
int aligned[alignof(U)];
union V { char c; double d; };
int padded[sizeof(V)];
SRC
	'reference-member-occupies-a-pointer' => [ 'a reference data member is a pointer-sized object (3.9/8)', <<'SRC' ],
struct S { char c; int &r; double d; };
int laid_out[sizeof(S)];
int aligned[alignof(S)];
SRC
	'static-member-is-not-part-of-the-object' => [ 'a static data member is not part of the object (9.4.2/1)', <<'SRC' ],
struct S { static int s; int a; };
int laid_out[sizeof(S)];
struct T { static const int t = 3; };
int empty[sizeof(T)];
int empty_aligned[alignof(T)];
SRC
	'anonymous-union-is-one-member' => [ 'an anonymous union is one member of the class that contains it (9.5/1)', <<'SRC' ],
struct S { char c; union { int a; char b[8]; }; int z; };
int laid_out[sizeof(S)];
SRC
	'reference-size-is-its-referent' => [ 'sizeof applied to a reference gives the referenced type (5.3.3/2)', <<'SRC' ],
typedef int& R;
int bound[sizeof(R)];
int aligned[alignof(R)];
SRC
	'array-of-void-is-rejected' => [ 'the element type of an array shall not be void (8.3.4/1)', <<'SRC' ],
typedef void V;
V a[3];
SRC
	'array-of-reference-is-rejected' => [ 'there are no arrays of references (8.3.4/1)', <<'SRC' ],
typedef int& R;
R a[3];
SRC
	'array-of-function-is-rejected' => [ 'there are no arrays of functions (8.3.4/1)', <<'SRC' ],
typedef int F(int);
F a[3];
SRC
	'cv-qualified-void-array-is-accepted' => [ 'a cv-qualified void is a different type from void', <<'SRC' ],
typedef const void CV;
CV a[3];
SRC
	'ref-qualified-member-functions' => [ 'a ref-qualifier is part of a member function type (8.3.5/6)', <<'SRC' ],
struct S {
  void a() &;
  void b() &&;
  void c() const &;
  void d() const &&;
  void e() volatile &;
};
SRC
	'ref-qualifier-needs-a-member-function' => [ 'a ref-qualifier makes a function a member function (8.3.5/6)', <<'SRC' ],
void f() &;
SRC
	'ref-qualifier-on-a-free-function-type-is-accepted' => [ 'the rule is about declaring a function, not about the type', <<'SRC' ],
typedef void F() &;
SRC
	'conflicting-function-return-type' => [ 'one signature with two return types is ill formed (13.1/3)', <<'SRC' ],
int f(int);
long f(int);
SRC
	'overloads-on-qualification-are-accepted' => [ 'a cv-qualifier is part of a member function signature', <<'SRC' ],
struct S { void f(); void f() const; void f() volatile; };
SRC
	'ref-qualified-overloads-are-accepted' => [ 'two ref-qualified members with one parameter list overload', <<'SRC' ],
struct S { void f() &; void f() &&; void f() const &; void f() const &&; };
SRC
	'mixed-ref-qualified-overload-set-is-rejected' => [ 'one parameter list cannot mix ref-qualified members with the rest (13.1/2)', <<'SRC' ],
struct S { void f(); void f() &; };
SRC
	'parameter-adjustment-in-signature-matching' => [ 'an array and a pointer parameter are one signature (8.3.5/5)', <<'SRC' ],
int f(int*);
int f(int[]);
int g(const int);
int g(int);
SRC
	'duplicate-function-definition' => [ 'a function is defined once in a translation unit (3.2/1)', <<'SRC' ],
int f() { return 1; }
int f() { return 2; }
SRC
	'declaration-then-definition-is-not-a-duplicate' => [ 'a declaration is not a definition', <<'SRC' ],
int f();
int f() { return 1; }
SRC
	'anonymous-union-with-a-declarator-injects-nothing' => [ '`union { ... } u;` is not an anonymous union (9.5/1)', <<'SRC' ],
union { int a; char b; } u;
SRC
	'union-with-a-declarator-needs-no-static' => [ 'the static requirement is for an anonymous union (9.5/2)', <<'SRC' ],
union { int a; char b; } u;
int sized[sizeof(u)];
SRC
	'unnamed-enum-in-a-class-keeps-its-name' => [ "a class member's declarator names the object, not the type", <<'SRC' ],
struct S { enum { A } e; };
S::e member;
SRC
	'unnamed-class-in-a-class-keeps-its-name' => [ "a class member's declarator names the object, not the type", <<'SRC' ],
struct S { struct { int a; } x; };
int sized[sizeof(S)];
SRC
	'comma-operator-is-a-constant' => [ 'a comma expression is its right operand (5.18/1)', <<'SRC' ],
int bound[(1, 2)];
static_assert((1, 1) == 1, "ok");
SRC
	'comma-operator-still-evaluates-its-left-operand' => [ 'the discarded operand is evaluated all the same', <<'SRC' ],
int bound[(1 / 0, 2)];
SRC
	'zero-with-a-suffix-is-decimal' => [ 'only a digit after the leading zero makes a literal octal (2.14.2)', <<'SRC' ],
int bound[0u ? 1 : 2];
int other[1 + 0u];
int octal[010];
SRC
	'opaque-unscoped-enum-redeclaration-is-rejected' => [ 'a later opaque declaration is ill formed for the same reason (7.2/3)', <<'SRC' ],
enum E { a };
enum E;
SRC
	'opaque-unscoped-enum-under-a-base-is-rejected' => [ 'an enum-base does not license a later opaque declaration (7.2/3)', <<'SRC' ],
enum E : int;
enum E;
SRC
	'qualified-elaborated-enum-is-the-enumeration' => [ 'a specifier that only uses the name defines nothing', <<'SRC' ],
struct S { enum E { a } e; };
enum S::E *p;
SRC
	'decltype-looking-type-name' => [ 'a type whose name begins with `decltype` is an ordinary name', <<'SRC' ],
typedef int decltype_x;
decltype_x value;
SRC
	'statement-substatements-have-scopes' => [ 'a selection or iteration statement owns a scope (6.4/3)', <<'SRC' ],
int f() {
  for(int i = 0; i < 3; ++i) { int j; }
  if(1) int k;
  while(1) int l;
  return 0;
}
SRC
	'for-init-declaration-belongs-to-the-loop' => [ 'a for-init declaration is scoped to the loop (6.5.3/1)', <<'SRC' ],
int f() {
  int i;
  for(int i = 0;;) { }
  return i;
}
SRC
	'friend-declares-in-the-enclosing-namespace' => [ 'a friend function is a member of the nearest namespace (11.3/6)', <<'SRC' ],
struct S { friend void f(); };
void f() { }
SRC
	'class-name-is-bound-before-its-body' => [ 'the class name is declared at the class-head (9.2/2)', <<'SRC' ],
struct S { S *p; };
struct T { struct U { }; U u; };
SRC
);

sub run_tool
{
	my ($tool, $source) = @_;
	my $in = "/tmp/types_differential.in.$$";
	my $out = "/tmp/types_differential.out.$$";
	open(my $fh, '>', $in) or die "cannot write $in: $!";
	print $fh $source;
	close($fh);
	my $status = system("$tool --emit-types -o $out $in 2>/dev/null");
	my $exit = $status >> 8;
	open(my $rf, '<', $out) or die "cannot read $out: $!";
	local $/;
	my $data = <$rf>;
	close($rf);
	unlink($in, $out);
	$data = '' if !defined($data);
	return ($exit, $data);
}

my %known_differences = ();

my $checked = 0;
my $differences = 0;
for my $name (sort keys %cases)
{
	my ($question, $source) = @{$cases{$name}};
	my ($mine_exit, $mine_dump) = run_tool($mine, $source);
	my ($ref_exit, $ref_dump) = run_tool($reference, $source);
	++$checked;
	if($mine_exit != $ref_exit)
	{
		print "DIFFERENT exit status: $name ($question)\n";
		print "  reference=$ref_exit compiler=$mine_exit\n";
		++$differences;
		next;
	}
	if($ref_exit == 0 && $mine_dump ne $ref_dump)
	{
		print "DIFFERENT dump: $name ($question)\n";
		++$differences;
	}
}

for my $name (sort keys %known_differences)
{
	my $source = $known_differences{$name};
	my ($mine_exit, $mine_dump) = run_tool($mine, $source);
	my ($ref_exit, $ref_dump) = run_tool($reference, $source);
	my $same = $mine_exit == $ref_exit && ($ref_exit != 0 || $mine_dump eq $ref_dump);
	if($same)
	{
		print "KNOWN DIFFERENCE is gone: $name now agrees with the reference\n";
		++$differences;
	}
}

print "$checked cases checked, $differences differences\n";
