#!/usr/bin/perl
#
# Differential check for the pa5 syntax stage.
#
# Runs the student compiler and the reference wrapper over a curated set of
# reduced reproducers and reports any difference in exit status or in the AST
# dump.  This is a personal test: it is not part of the course contract and is
# not discovered by `make test`.
#
#   perl student.tests/syntax_differential.pl
#
# The curated list holds the reduced reproducers for the corners the checked-in
# fixtures reach only in passing: the name category that decides a
# parenthesized parameter against a function type, a template-id against a
# relational expression in a template argument and in a base clause, whether a
# template declaration's name is a name of the enclosing scope, and what a
# failed speculative alternative leaves behind in the name table and in the
# delimiter count.  Each case says what the two readings of it are, because the
# point of the case is the choice, not the spelling.
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
	'parameter-name-against-function-type' => [ 'a parenthesized name is a parameter name', <<'SRC' ],
int named(int(value));
SRC
	'parameter-typedef-is-a-function-type' => [ 'a type name is a simple-type-specifier (8.2/7)', <<'SRC' ],
typedef int value_type;
int callback(int(value_type));
SRC
	'parameter-pack-is-not-a-declarator' => [ '`( ... )` is a parameter clause with a pack', <<'SRC' ],
int variadic(int(...));
SRC
	'parameter-keyword-type-in-parens' => [ 'a keyword type is not a declarator-id', <<'SRC' ],
int keyword(int(int));
SRC
	'parameter-pointer-in-parens' => [ 'a pointer belongs to the clause, not the name', <<'SRC' ],
class C { };
int pointed(int(C*));
SRC
	'parameter-array-suffix-in-parens' => [ 'a suffixed name stays a declarator', <<'SRC' ],
class C { };
int arrayed(int(C[10]));
SRC
	'base-clause-relational-argument' => [ 'a qualified name keeps the `>` that closes the list', <<'SRC' ],
template<class T, T V> struct ic {};
template<class R1> struct B : ic<bool, R1::num < 2> {};
SRC
	'base-clause-logical-argument' => [ 'a logical operator inside a base clause argument', <<'SRC' ],
template<bool> struct ic {};
template<class T> struct P { static const bool value = true; };
template<class T> struct B : ic<P<T>::value && (!P<T>::value)> {};
SRC
	'unknown-qualified-name-in-arguments' => [ 'an unknown qualified name is no template-id', <<'SRC' ],
template<class T> struct A {};
A<N::B<2>, int> v;
SRC
	'known-template-in-arguments' => [ 'a known member template is one', <<'SRC' ],
template<class T> struct A {};
struct N { template<class T> struct B {}; };
A<N::B<2>, int> v;
SRC
	'qualified-template-outside-arguments' => [ 'an expression keeps the template-id reading', <<'SRC' ],
int g() { return N::num<2>(); }
SRC
	'dependent-qualified-template-call' => [ 'a dependent name keeps it too', <<'SRC' ],
template<class R1> int f() { return R1::num<2>(); }
SRC
	'argument-unknown-unqualified-template-id' => [ 'a bare name is read speculatively', <<'SRC' ],
template<class T> struct A {};
A<x<2>> y;
SRC
	'template-name-of-the-enclosing-scope' => [ 'a class template names a type outside', <<'SRC' ],
template<class T> struct B {};
void f() { B * p; }
SRC
	'template-name-in-a-block' => [ 'and a template-id call reads as one', <<'SRC' ],
template<class T> void validate();
int main() { validate<int>(); }
SRC
	'qualified-call-is-not-a-declaration' => [ 'a function template is not a type', <<'SRC' ],
namespace ns { template<class T> void destroy_at(T*); }
struct box { void clear(box& other) { ns::destroy_at(&other); } };
SRC
	'rollback-keeps-an-earlier-declaration' => [ 'a failed alternative unbinds only its own', <<'SRC' ],
typedef int value_type;
int g1(int(a));
void f() { value_type * p; }
SRC
	'rollback-keeps-a-typedef-for-a-later-parameter' => [ 'and a later parameter still reads it', <<'SRC' ],
typedef int value_type;
int g1(int(a));
int g2(int(value_type));
SRC
	'nested-qualified-template-id-arguments' => [ 'a namespace member template in arguments', <<'SRC' ],
namespace ns { template<class T> struct box {}; template<class A, class B> struct andx {}; }
template<typename T> using left_alias = ns::andx<ns::box<T>, int>;
SRC
	'explicit-instantiation-name' => [ 'an explicit instantiation names its template', <<'SRC' ],
template<class T> void f(T);
extern template void f<int>(int);
SRC
);

# The reference reads `(x::C)` in a parameter's parenthesized position as a
# parameter clause over the qualified name, and this compiler reads it as a
# declarator; the two agree until an earlier parameter has declared `x` as a
# value, which is when the qualified name stops reading as a type.  What that
# shape means is not a rule this stage's documents state - a parameter name may
# not be qualified - so it is recorded rather than matched.
my %known_differences = (
	'qualified-name-in-a-parameter-parens-after-a-value' => <<'SRC',
class C { };
int q8(int(C(x)));
int q6(int(x::C));
SRC
);

sub run_tool
{
	my ($tool, $source) = @_;
	my $in = "/tmp/syntax_differential.in.$$";
	my $out = "/tmp/syntax_differential.out.$$";
	open(my $fh, '>', $in) or die "cannot write $in: $!";
	print $fh $source;
	close($fh);
	my $status = system("$tool --emit-ast -o $out $in 2>/dev/null");
	my $exit = $status >> 8;
	open(my $rf, '<', $out) or die "cannot read $out: $!";
	local $/;
	my $data = <$rf>;
	close($rf);
	unlink($in, $out);
	$data = '' if !defined($data);
	return ($exit, $data);
}

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
exit($differences == 0 ? 0 : 1);