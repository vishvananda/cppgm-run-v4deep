#!/usr/bin/perl
#
# Differential sweep for the pa6 scope/type stage.
#
# Builds small translation units by crossing the declaration shapes this stage
# owns - a type alias, a class with a member type, an unscoped and a scoped
# enumeration, a namespace that is reopened, a using-declaration and a
# using-directive, an array whose bound is a constant, and a qualified name
# into each of them - and compares the student compiler against the reference
# wrapper in exit status and in the scope dump.  Where the differential harness
# curates the corners by hand, the sweep covers the combinations, including the
# ones that are ill formed, because a rejection is as much a reading as an
# acceptance.
#
# This is a personal test: it is not part of the course contract and is not
# discovered by `make test`.
#
#   perl student.tests/types_sweep.pl [limit]

use strict;
use warnings;

use Cwd qw(abs_path);
use File::Basename qw(dirname);

my $root = dirname(dirname(abs_path($0)));
my $mine = "$root/dev/cppgm++";
my $reference = "$root/reference-binaries/cppgm++";
my $limit = shift @ARGV // 0;

# Each entry is a declaration a case may carry, and the name it introduces.
my @declarations = (
	[ 'typedef int Y;', 'Y' ],
	[ 'using A = const Y;', 'A' ],
	[ 'struct C { typedef int M; };', 'C' ],
	[ 'struct C { char c; int i; };', 'C' ],
	[ 'struct C { char a[3]; short s; };', 'C' ],
	[ 'struct C {};', 'C' ],
	[ 'struct C { struct D { char c; } d; int i; };', 'C' ],
	[ 'struct C;', 'C' ],
	[ 'class C {};', 'C' ],
	[ 'enum E { V = 3 };', 'E' ],
	[ 'enum class S : char { W = 2 };', 'S' ],
	[ 'enum class S;', 'S' ],
	[ 'namespace N { typedef Y T; }', 'N' ],
	[ 'namespace N { }', 'N' ],
	[ 'namespace N = M;', 'N' ],
	[ 'using N::T;', 'T' ],
	[ 'using namespace N;', 'N' ],
	[ 'int arr[V];', 'arr' ],
	[ 'int cond[1 ? 2 : 3];', '1 ? 2 : 3' ],
	[ 'extern int incomplete[];', 'incomplete' ],
);

# Each entry is a use the case may make of a name the declarations introduced.
my @uses = (
	[ 'Y y;', 'Y' ],
	[ 'A a;', 'A' ],
	[ 'C c;', 'C' ],
	[ 'C::M m;', 'C::M' ],
	[ 'E e;', 'E' ],
	[ 'S s;', 'S' ],
	[ 'S::W w;', 'S::W' ],
	[ 'N::T t;', 'N::T' ],
	[ 'int bound[V];', 'V' ],
	[ 'int wide[sizeof(S)];', 'sizeof(S)' ],
	[ 'int aligned[alignof(Y)];', 'alignof(Y)' ],
	[ 'int laid_out[sizeof(C)];', 'sizeof(C)' ],
	[ 'int laid_out_align[alignof(C)];', 'alignof(C)' ],
	[ 'C::D nested;', 'C::D' ],
	[ 'int object_size[sizeof(arr)];', 'sizeof(arr)' ],
	[ 'int wide_conditional[1 ? 2 : 3];', '1 ? 2 : 3' ],
	[ 'decltype(V) d;', 'decltype(V)' ],
	[ 'static_assert(V == 3, "v");', 'V' ],
	[ 'static_assert(sizeof(S) == 1, "s");', 'sizeof(S)' ],
);

my @cases;
for my $d (@declarations)
{
	for my $u (@uses)
	{
		push @cases, "$d->[0]\n$u->[0]\n";
	}
}
for my $d (@declarations)
{
	for my $other (@declarations)
	{
		next if $d == $other;
		push @cases, "$d->[0]\n$other->[0]\n";
	}
}
@cases = @cases[0 .. $limit - 1] if $limit > 0;

sub run_tool
{
	my ($tool, $source) = @_;
	my $in = "/tmp/types_sweep.in.$$";
	my $out = "/tmp/types_sweep.out.$$";
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

my $checked = 0;
my $differences = 0;
my %seen;
for my $source (@cases)
{
	next if $seen{$source}++;
	++$checked;
	my ($mine_exit, $mine_dump) = run_tool($mine, $source);
	my ($ref_exit, $ref_dump) = run_tool($reference, $source);
	my $same = $mine_exit == $ref_exit && ($ref_exit != 0 || $mine_dump eq $ref_dump);
	next if $same;
	++$differences;
	print "DIFFERENT ($mine_exit against $ref_exit):\n";
	print "  ", join("\n  ", split(/\n/, $source)), "\n";
	if($ref_exit == 0 && $mine_exit == 0)
	{
		my @mine = split(/\n/, $mine_dump);
		my @ref = split(/\n/, $ref_dump);
		for my $index (0 .. $#ref)
		{
			last if $index > $#mine;
			if($ref[$index] ne $mine[$index])
			{
				print "  ref:  $ref[$index]\n  mine: $mine[$index]\n";
				last;
			}
		}
	}
}

print "$checked cases checked, $differences differences\n";
