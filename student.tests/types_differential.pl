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
	'sizeof-incomplete-class' => [ 'sizeof of an incomplete type is ill formed', <<'SRC' ],
struct C;
int a[sizeof(C)];
SRC
	'cross-unit-constant-is-not-visible' => [ 'a translation unit is analysed on its own', <<'SRC' ],
extern const int bound;
int values[bound];
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
