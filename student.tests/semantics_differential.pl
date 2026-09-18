#!/usr/bin/perl
#
# Differential check for the pa7 resolved-semantics stage.
#
# Runs the student compiler and the reference wrapper over a curated set of
# reduced reproducers and reports any difference in exit status or in the
# semantics dump.  This is a personal test: it is not part of the course
# contract and is not discovered by `make test`.
#
#   perl student.tests/semantics_differential.pl
#
# The curated list holds the reduced reproducers for the corners the checked-in
# fixtures reach only in passing: which conversion sequence the call layer
# ranks over which, what a reference binding does with a temporary and with a
# base subobject, how a member's cv-qualifiers reach the object it is reached
# through, what the value category of each operator form is, and which of the
# call and control-flow violations the handout requires PA7 to reject.  Each
# case says what the two readings of it are, because the point of the case is
# the choice.
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
	'integral-promotion-beats-integral-conversion' => [ '4.5/1 promotes before 4.7 converts (13.3.3.2/3)', <<'SRC' ],
int pick(int value) { return value; }
long pick(long value) { return value; }
int f() { short value = 0; return pick(value); }
SRC
	'ellipsis-is-the-worst-match' => [ '13.3.3.2/3 ranks an ellipsis slot below every conversion', <<'SRC' ],
int pick(int* value) { return 1; }
int pick(...) { return 2; }
int f() { return pick(0); }
SRC
	'ranked-prefix-decides-an-ellipsis-list' => [ 'the ranked prefix is compared, not the ellipsis slot', <<'SRC' ],
int pick(int value, ...) { return 1; }
long pick(long value, ...) { return 2; }
int f() { return pick(1, 2); }
SRC
	'null-pointer-conversion-beats-boolean' => [ '4.10/3 converts a derived pointer to a base pointer', <<'SRC' ],
int pick(bool value) { return 1; }
int pick(const void* value) { return 2; }
int f() { int x = 0; return pick(&x); }
SRC
	'less-qualified-pointer-target-wins' => [ '13.3.3.2/3 ranks two pointer conversions by cv-qualification', <<'SRC' ],
struct base { int value; };
struct derived : base { int extra; };
int pick(base*) { return 1; }
int pick(const base*) { return 2; }
int f() { derived d; return pick(&d); }
SRC
	'rvalue-reference-binds-a-converted-temporary' => [ '13.3.3.1.4 forms the sequence to the referred type first', <<'SRC' ],
int pick(const long long&) { return 1; }
int pick(const long long&&) { return 2; }
int f() { unsigned value = 0; return pick(value); }
SRC
	'direct-binding-beats-a-temporary' => [ '13.3.3.2/3 prefers the binding that needs no temporary', <<'SRC' ],
int pick(volatile int&) { return 1; }
int pick(const int&) { return 2; }
int f() { volatile int value = 0; return pick(value); }
SRC
	'derived-lvalue-binds-a-base-reference' => [ '13.3.3.1.4/3 binds by a derived-to-base conversion', <<'SRC' ],
struct base { int value; };
struct derived : base { int extra; };
int pick(const base&) { return 1; }
int f() { derived d; return pick(d); }
SRC
	'pointer-qualification-is-recursive' => [ '4.4/4 needs the intermediate const', <<'SRC' ],
int take(const int* const*) { return 1; }
int f() { int** value = 0; return take(value); }
SRC
	'deep-pointer-qualification-hole-is-rejected' => [ '4.4/4 rejects the missing intermediate const', <<'SRC' ],
int take(const int**) { return 1; }
int f() { int** value = 0; return take(value); }
SRC
	'array-decays-before-the-parameter-is-matched' => [ '4.2 array-to-pointer is an lvalue transformation', <<'SRC' ],
int take(int* value) { return 1; }
int f() { int values[3]; return take(values); }
SRC
	'function-decays-before-the-parameter-is-matched' => [ '4.3 function-to-pointer is an lvalue transformation', <<'SRC' ],
int callback(int value) { return value; }
int take(int (*value)(int)) { return 1; }
int f() { return take(callback); }
SRC
	'overload-resolves-against-a-function-pointer-target' => [ '13.4 picks the overload the target type names', <<'SRC' ],
int pick(int value) { return value; }
long pick(long value) { return value; }
int f() { int (*chosen)(int) = pick; return chosen(0); }
SRC
	'address-of-a-member-function-is-a-pointer-to-member' => [ '5.3.1/3 with 8.3.3 forms a member pointer', <<'SRC' ],
struct C { int read() const; };
typedef int (C::*reader)() const;
reader f() { return &C::read; }
SRC
	'target-type-chooses-between-cv-member-overloads' => [ '13.4 tells two cv-qualified members apart', <<'SRC' ],
struct C { int read(); int read() const; };
typedef int (C::*reader)() const;
reader f() { return &C::read; }
SRC
	'member-of-a-base-is-a-member-of-the-derived' => [ '10/1 searches the base classes for an unqualified member', <<'SRC' ],
struct base { int value; };
struct derived : base { int extra; };
int f(derived& object) { return object.value; }
SRC
	'qualified-member-access-names-the-base' => [ '5.2.5/5 searches the scope the qualifier names', <<'SRC' ],
struct base { int value; };
struct derived : base { int value(); };
int f(derived& object) { return object.base::value; }
SRC
	'member-of-a-const-object-is-const' => [ '9.3.2/2 qualifies a member reached through a const object', <<'SRC' ],
struct S { int value; };
int f(const S& object) { return object.value; }
SRC
	'conditional-of-two-lvalues-is-an-lvalue' => [ '5.16/4 keeps the common type and category', <<'SRC' ],
int f(bool sign, int left, int right) { return sign ? left : right; }
SRC
	'conditional-of-a-mixed-pair-is-a-prvalue' => [ '5.16/5 makes a mixed pair a prvalue', <<'SRC' ],
bool f(bool sign, bool value) { return sign ? false : value; }
SRC
	'comma-yields-its-right-operand' => [ '5.18/1 discards the left operand', <<'SRC' ],
int f() { int left = 1; int right = 2; return (left++, right); }
SRC
	'subscript-commutes' => [ '5.2.1/1 reads `1[a]` as `a[1]`', <<'SRC' ],
int f() { int values[3] = {1, 2, 3}; return 1[values]; }
SRC
	'string-literal-is-an-array-lvalue' => [ '2.14.5/8 gives a string literal array type', <<'SRC' ],
int f() { return "abc"[1]; }
SRC
	'sizeof-of-an-expression-is-a-size' => [ '5.3.3/1 types `sizeof` as `size_t`', <<'SRC' ],
int f() { int values[3]; return sizeof(values) + sizeof(int); }
SRC
	'value-initialised-scalar-is-a-zero' => [ '8.5/7 value-initialises a scalar to zero', <<'SRC' ],
typedef decltype(sizeof(0)) size_t;
size_t f() { return size_t(); }
SRC
	'call-of-a-function-reference-parameter' => [ '5.2.2/1 calls through a reference to a function', <<'SRC' ],
int callback(int value) { return value; }
int f(int (&reference)(int)) { return reference(0); }
SRC
	'call-of-a-function-pointer-through-a-deref' => [ '5.3.1/2 with 5.2.2 calls a pointer through `*`', <<'SRC' ],
int callback(int value) { return value; }
int f() { int (*pointer)(int) = callback; return (*pointer)(0); }
SRC
	'condition-declaration-is-scoped-to-the-statement' => [ '6.4/3 scopes a condition declaration to its statement', <<'SRC' ],
int f() { if (int value = 1) return value; return 0; }
SRC
	'unbraced-substatement-is-its-own-scope' => [ '6.4/1 gives each substatement position a scope', <<'SRC' ],
int f(bool sign) { if (sign) int value; else int value; return 0; }
SRC
	'for-init-declaration-belongs-to-the-loop' => [ '6.5.3/1 scopes a for-init declaration to the loop', <<'SRC' ],
int f() { for (int index = 0; index < 3; index = index + 1) { } return 0; }
SRC
	'void-return-of-a-value-is-rejected' => [ '6.6.3/2 rejects a value returned from a void function', <<'SRC' ],
void f() { return 1; }
SRC
	'break-outside-a-loop-is-rejected' => [ '6.6.1/1 requires an enclosing loop or switch', <<'SRC' ],
void f() { break; }
SRC
	'continue-outside-a-loop-is-rejected' => [ '6.6.2/1 requires an enclosing loop', <<'SRC' ],
void f() { continue; }
SRC
	'default-outside-a-switch-is-rejected' => [ '6.4.2/1 requires an enclosing switch', <<'SRC' ],
void f() { default: return; }
SRC
	'nonconstant-case-label-is-rejected' => [ '6.4.2/1 requires an integral constant expression', <<'SRC' ],
int f(int value) { switch (value) { case value: return 1; } return 0; }
SRC
	'scoped-enum-condition-is-rejected' => [ '6.4/4 with 7.2/9 rejects a scoped enumeration condition', <<'SRC' ],
enum class state { ready };
int f(state value) { if (value) return 1; return 0; }
SRC
	'mismatched-indirect-call-arity-is-rejected' => [ '5.2.2/1 fixes an indirect call’s arity', <<'SRC' ],
int f(int (*pointer)(int)) { return pointer(); }
SRC
	'pointer-and-integer-equality-is-rejected' => [ '5.10/1 needs comparable operands', <<'SRC' ],
bool f(int* value) { return value == 1; }
SRC
	'pointer-multiplication-is-rejected' => [ '5.17/7 applies the operator to the operands', <<'SRC' ],
void f(int* value) { value *= 2; }
SRC
	'enumerator-is-not-a-null-pointer-constant' => [ '4.10/1 admits only an integer literal zero', <<'SRC' ],
enum { zero = 0 };
int take(int* value) { return 1; }
int f() { return take(zero); }
SRC
	'ambiguous-overload-is-rejected' => [ '13.3.3/2 rejects a call with no unique best match', <<'SRC' ],
int pick(long, long) { return 1; }
int pick(unsigned long, unsigned long) { return 2; }
int f() { short value = 0; return pick(value, value); }
SRC
	'name-denotes-one-entity' => [ '3.4/1 rejects an object and a function of one spelling', <<'SRC' ],
int conflict;
void conflict();
SRC
	'object-of-an-incomplete-class-is-rejected' => [ '3.9/5 rejects an object of an incomplete type', <<'SRC' ],
struct S;
S object;
SRC
	'duplicate-definition-is-rejected' => [ '3.2/1 defines a function once per unit', <<'SRC' ],
int duplicate() { return 1; }
int duplicate() { return 2; }
SRC
	'function-template-id-denotes-a-specialization' => [ '14.2 substitutes the argument for the parameter', <<'SRC' ],
template<class T> void take(T);
template<class T> void hello(T);
struct stream {};
void use() { take(static_cast<void(*)(stream)>(&hello<stream>)); }
SRC
	'target-type-chooses-between-template-parameter-lists' => [ '13.4 picks the specialization the target names', <<'SRC' ],
template<class T> void take(T);
template<class T> void hello(T);
template<class T> void hello(T, int);
struct stream {};
void use() { take(static_cast<void(*)(stream)>(&hello<stream>)); }
SRC
	'deduced-call-instantiates-from-the-argument-type' => [ '14.8.2 deduces the parameter a call does not write', <<'SRC' ],
template<class T> void take(T);
void use() { take(1); take(2); }
SRC
	'two-demands-of-one-specialization-are-one-function' => [ '14.7.1 instantiates a specialization once', <<'SRC' ],
template<class T> void take(T);
void use() { take(1); take(1); }
SRC
	'deduction-binds-each-parameter-from-its-own-argument' => [ '14.8.2.1 matches every parameter in turn', <<'SRC' ],
template<class T> void take(T, T);
void use() { take(1, 2); }
SRC
	'deduction-through-a-pointer-parameter' => [ '14.8.2.1 deduces through the pointer former', <<'SRC' ],
template<class T> void take(T*);
void use() { int value = 0; take(&value); }
SRC
	'deduction-decays-an-array-argument' => [ '14.8.2.1 decays the argument before the pointer match', <<'SRC' ],
template<class T> void take(T*);
void use() { int values[3]; take(values); }
SRC
	'deduction-through-a-reference-parameter' => [ '14.8.2.1 deduces against the referred type', <<'SRC' ],
template<class T> void take(const T&);
void use() { take(1); }
SRC
	'explicit-template-argument-list-names-a-specialization' => [ '14.3/1 writes the arguments instead of deducing them', <<'SRC' ],
template<class T> void take(T);
void use() { take<int>(1); }
SRC
	'explicit-template-argument-list-is-ranked-by-the-call' => [ '13.3 ranks the specializations a template-id gives', <<'SRC' ],
template<class T> void take(T, int);
template<class T> void take(T);
void use() { take<int>(1); }
SRC
	'qualified-template-name-is-looked-up' => [ '14.2 resolves the qualifier before the template name', <<'SRC' ],
namespace n { template<class T> void take(T); }
void use() { n::take(1); }
SRC
	'a-void-argument-is-not-a-value' => [ '5.2.2/4 rejects an argument of type void', <<'SRC' ],
template<class T> void take(T);
template<class U> void other(U);
void use() { take(other(1)); }
SRC
	'a-parameter-the-call-does-not-deduce-is-not-a-candidate' => [ '14.8.2/5 leaves an undeduced parameter without a type', <<'SRC' ],
template<class T> void take(T, T);
void use() { take(1, 2L); }
SRC
	'an-explicit-argument-list-must-match-the-parameter-list' => [ '14.3/1 rejects a wrong argument count', <<'SRC' ],
template<class T> void take(T);
void use() { take<int, int>(1); }
SRC
	'a-template-id-in-an-initializer-is-not-an-unknown-name' => [ '14.2 names a function, not a constant', <<'SRC' ],
template<class T> void hello(T);
void (*p)(int) = &hello<int>;
SRC
	'an-unevaluated-operand-demands-no-instantiation' => [ '5.3.3/1 leaves the operand of sizeof unevaluated', <<'SRC' ],
template<class T> void hello(T);
void use() { unsigned long n = sizeof(&hello<int>); (void)n; }
SRC
	'a-later-demand-instantiates-what-sizeof-skipped' => [ '14.7.1 instantiates on the use that evaluates it', <<'SRC' ],
template<class T> void hello(T);
template<class T> void take(T);
void use() { unsigned long n = sizeof(&hello<int>); (void)n; take(&hello<int>); }
SRC
);

sub run_tool
{
	my ($tool, $source) = @_;
	my $in = "/tmp/pa7_differential_in.$$.t";
	my $out = "/tmp/pa7_differential_out.$$.txt";
	open(my $fh, '>', $in) or die "cannot write $in: $!";
	print $fh $source;
	close($fh);
	my $status = system("$tool --emit-semantics -o $out $in 2>/dev/null");
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
