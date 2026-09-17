#!/usr/bin/perl
#
# Systematic differential sweep for pa3 ppexpr.
#
# Where ppexpr_differential.pl samples the input space at random, this walks it:
# every operator spelling crossed with every operand pair and every
# parenthesisation shape, every unary spelling crossed with every operand, the
# `defined` forms, the conditional's branch combinations, and the rejected
# shapes at each operand position of a few skeletons.
#
#   perl student.tests/ppexpr_sweep.pl
#
# Well-formed candidates are packed one per line into chunks and the two tools
# are compared over the whole chunk at once, so a sweep of this size costs a few
# hundred process pairs rather than one per candidate.  A chunk that differs is
# reported and then reduced to its first differing line by re-running that line
# alone.  Candidates that can break translation phases 1-3 - an unterminated
# literal, a line splice, a raw string whose body contains a newline - cannot be
# packed, so they are run one per file.

use strict;
use warnings;

use Cwd qw(abs_path);
use File::Basename qw(dirname);

my $root = dirname(dirname(abs_path($0)));
my $mine = "$root/dev/ppexpr";
my $reference = "$root/reference-binaries/ppexpr";

for my $tool ($mine, $reference)
{
	die "$tool is missing; build it first\n" if !-x $tool;
}

my $chunk_size = 400;

# Operands chosen around the type and value boundaries: the two decimal
# boundaries, the unsigned/signed split of every base, character literals of
# each prefix, and the `identifier_or_keyword` forms.
my @operands = (
	'0', '1', '2', '3', '63', '64', '65', '97', '124u', '424', '-132',
	'0x2', '03', '0x2u', '03u', '1u', '4294967295u', '4294967296',
	'9223372036854775807', '9223372036854775807ll', '0x7fffffffffffffff',
	'0x8000000000000000', '0xffffffffffffffff', '18446744073709551615u',
	"'a'", "'\\xff'", "'\\u00aa'", "u'a'", "U'a'", "L'a'", "u'z'", "U'z'",
	'true', 'false', 'ident', '_x', 'auto', 'sizeof', 'defined', 'operator',
);

# Every binary operator of the grammar in both its punctuator and its
# alternative spelling, so `200-ops-alts`'s table is swept as well.
my @binary = (
	['*', '*'], ['/', '/'], ['%', '%'], ['+', '+'], ['-', '-'],
	['<<', '<<'], ['>>', '>>'], ['<', '<'], ['>', '>'], ['<=', '<='],
	['>=', '>='], ['==', '=='], ['!=', '!='],
	['&', 'bitand'], ['^', 'xor'], ['|', 'bitor'],
	['&&', 'and'], ['||', 'or'],
);

my @unary = ('+', '-', '!', '~', 'not', 'compl');

# The token kinds the handout rejects wherever they stand on the line.
my @rejected = (
	'"foo"', "u8\"x\"", "L\"x\"", '3.2', '1e10', '0x1p3', '"a"_x', '1.0_x',
	"'a'_x", '5uu', '08', '@', '\\', '::', '...', '[', ']', '{', '}', ';',
	',', '=', '++', '--', '->*', '.*', 'R"(a)"', '0x',
);

# Skeleton positions a rejected token is planted in.
my @skeletons = ('%s', '1 + %s', '%s + 1', '1 ? %s : 2', '1 ? 2 : %s',
                 'defined %s', '(%s)', '!%s', '1 && %s', '0 || %s');

# The `defined` operator's operands, including keywords and the operator itself.
my @defined_operands = (
	'a', 'b', 'aha', 'foo', 'bar', 'car', '_x', 'AB', 'BCD', 'Z',
	'defined', 'true', 'false', 'auto', 'sizeof', 'operator', 'int',
	"\xC3\xA9", "\xCF\x80", 'a1', '1a',
);

# Separators between tokens.  Every one of them is white space or nothing, so
# the token sequence is the same and only the phase 3 state differs.
my @separators = (' ', '', "\t", '  ', '/*c*/', ' /*c*/ ', '//c');

# A candidate that could break a translation phase, or that carries a line
# boundary, cannot share a file with the rest of the sweep: a phase 1-3 failure
# ends the run and truncates its output, and a splice joins two of the packed
# lines.  Those go to the one-per-file list.
sub push_candidate
{
	my ($packed, $solo, $text) = @_;
	if ($text =~ /\\\z/ || $text =~ /[\x00-\x08\x0a-\x1f]/)
	{
		push @{$solo}, $text;
		return;
	}
	push @{$packed}, $text;
}

my (@packed, @solo);

for my $pair (@binary)
{
	for my $a (@operands)
	{
		for my $b (@operands)
		{
			push_candidate(\@packed, \@solo, "$a $pair->[0] $b");
			push_candidate(\@packed, \@solo, "($a) $pair->[1] $b");
			push_candidate(\@packed, \@solo, "$a $pair->[0] ($b)");
			push_candidate(\@packed, \@solo, "($a) $pair->[0] ($b)");
		}
	}
	for my $unit (@unary)
	{
		for my $a (@operands)
		{
			push_candidate(\@packed, \@solo, "$unit $a");
			push_candidate(\@packed, \@solo, "$unit($a)");
			push_candidate(\@packed, \@solo, "1 $pair->[0] $unit $a");
			push_candidate(\@packed, \@solo, "$unit $a $pair->[1] 1");
		}
	}
}

# The conditional: both arms' types, and the short-circuit that decides which
# of them is evaluated.
my @conditions = ('0', '1', 'true', 'false', '2', '0u', '1u', 'defined a', 'ident');
for my $c (@conditions)
{
	for my $a (@operands)
	{
		for my $b (@operands)
		{
			push_candidate(\@packed, \@solo, "$c ? $a : $b");
			push_candidate(\@packed, \@solo, "$c ? ($a) : ($b)");
		}
	}
	for my $a (@operands)
	{
		for my $b (@operands)
		{
			push_candidate(\@packed, \@solo, "$c && $a || $b");
			push_candidate(\@packed, \@solo, "$c || $a && $b");
		}
	}
}

for my $operand (@defined_operands)
{
	push_candidate(\@packed, \@solo, "defined $operand");
	push_candidate(\@packed, \@solo, "defined($operand)");
	push_candidate(\@packed, \@solo, "defined ( $operand )");
	push_candidate(\@packed, \@solo, "defined($operand) ? 1 : 2");
	push_candidate(\@packed, \@solo, "1 ? defined ($operand) : 2");
	push_candidate(\@packed, \@solo, "!defined($operand)");
}

# White space and comments around and inside an expression.  A `//` separator
# never leads a candidate: it would swallow the whole line and the line would
# produce no output at all, which the packed chunk's line count cannot tell
# apart from a truncated file.
for my $separator (@separators)
{
	for my $body ('1+2', '1 + 2', '-  3', '1??', 'defined a', '2 * ~1', '(1)')
	{
		push_candidate(\@packed, \@solo, "1${separator}+${separator}2");
		push_candidate(\@packed, \@solo, "${separator}$body")
			if $separator !~ m{^//};
		push_candidate(\@packed, \@solo, "$body$separator");
	}
}

for my $token (@rejected)
{
	for my $skeleton (@skeletons)
	{
		my $line = $skeleton;
		$line =~ s/%s/$token/;
		# A comment or a newline inside the skeleton would split the line.
		next if $line =~ /\n/;
		push_candidate(\@packed, \@solo, $line);
	}
}

# Candidates that break translation phases 1-3, or split a logical line, so
# they cannot share a file with anything else.
push @solo,
	'"unterminated', "'unterminated", 'R"(a', 'R"d(a)d', 'a\\', '1 + \\',
	"1 + \\\n2", "defined \\\na", "\"a\\\nb\"", "R\"(a\nb)\"", "/*unterminated",
	"\xC3", "\x80", "\\u0041", "%:", "??/", "??=", "1 ??' 2";

my $candidates = scalar(@packed) + scalar(@solo);
my $mismatches = 0;

# A chunk that differs is reduced to the line that made it differ.  The chunk
# is only accepted after every one of its lines has been accounted for, so a
# rejection cannot hide a divergence in a later line of the same file.
for (my $start = 0; $start < scalar(@packed); $start += $chunk_size)
{
	my $end = $start + $chunk_size - 1;
	$end = scalar(@packed) - 1 if $end > scalar(@packed) - 1;
	my @chunk = @packed[$start .. $end];
	my $text = join("\n", @chunk) . "\n";
	my ($mine_exit, $mine_out) = run_tool_file($mine, $text);
	my ($ref_exit, $ref_out) = run_tool_file($reference, $text);

	# The packing assumption: no candidate in the chunk may fail a translation
	# phase (which would truncate the file's output and hide the lines after
	# it), and every line of the chunk must have produced its result.
	if ($mine_exit != 0 || $ref_exit != 0)
	{
		die "chunk $start ended with a phase 1-3 failure (mine=$mine_exit ref=$ref_exit)\n";
	}
	# The trailing `eof` line is not counted: the comparison strips it with the
	# output's trailing newline.
	my $mine_lines = () = $mine_out =~ /\n/g;
	my $ref_lines = () = $ref_out =~ /\n/g;
	if ($mine_lines != scalar(@chunk) || $ref_lines != scalar(@chunk))
	{
		die "chunk $start produced $mine_lines/$ref_lines lines for " .
			scalar(@chunk) . " candidates\n";
	}

	next if $mine_exit == $ref_exit && $mine_out eq $ref_out;

	# The whole file's exit status is a single oracle for every line, so a
	# failing file is reduced line by line.
	my $reduced = 0;
	for my $line (@chunk)
	{
		my ($me, $mo) = run_tool_file($mine, "$line\n");
		my ($re, $ro) = run_tool_file($reference, "$line\n");
		next if $me == $re && ($re != 0 || $mo eq $ro);
		++$reduced;
		++$mismatches;
		print "MISMATCH: $line\n";
		print "  exit status: mine=$me ref=$re\n" if $me != $re;
		print "  mine:\n$mo\n  ref:\n$ro\n";
	}
	# Every line agreeing on its own while the chunk disagrees would mean a
	# candidate depends on the lines before it; that is a divergence too.
	if ($reduced == 0)
	{
		++$mismatches;
		print "MISMATCH in a packed chunk of ", scalar(@chunk),
			" lines that no single line reproduces (chunk $start)\n";
	}
}

for my $line (@solo)
{
	my ($me, $mo) = run_tool_file($mine, "$line\n");
	my ($re, $ro) = run_tool_file($reference, "$line\n");
	next if $me == $re && ($re != 0 || $mo eq $ro);
	++$mismatches;
	print "MISMATCH: ", unpack('H*', $line), "\n";
	print "  exit status: mine=$me ref=$re\n" if $me != $re;
	print "  mine:\n$mo\n  ref:\n$ro\n";
}

print $mismatches == 0
	? "sweep passed: $candidates candidates\n"
	: "sweep failed: $mismatches mismatches out of $candidates candidates\n";
exit($mismatches == 0 ? 0 : 1);

sub run_tool_file
{
	my ($tool, $input) = @_;
	my $in = "/tmp/ppexpr_sweep.in.$$";
	my $out = "/tmp/ppexpr_sweep.out.$$";
	open(my $fh, '>', $in) or die "cannot write $in: $!";
	binmode($fh);
	print $fh $input;
	close($fh);
	my $status = system("$tool < $in > $out 2>/dev/null");
	my $exit = $status >> 8;
	open(my $rf, '<', $out) or die "cannot read $out: $!";
	local $/;
	my $data = <$rf>;
	close($rf);
	unlink($in, $out);
	$data = '' if !defined($data);
	$data =~ s/\s+$//;
	return ($exit, $data);
}
