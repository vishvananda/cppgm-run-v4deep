#!/usr/bin/perl
#
# Differential check for pa3 ppexpr.
#
# Runs the student compiler and the reference wrapper over generated controlling
# expressions and reports any difference in stdout or exit status.  This is a
# personal test: it is not part of the course contract and is not discovered by
# `make test`.
#
#   perl student.tests/ppexpr_differential.pl [iterations] [seed]
#
# Each logical line is generated one of three ways - a random expression from
# the controlling-expression grammar, a random concatenation of source atoms
# (which mostly produces rejected lines), or an empty/comment-only line - so the
# accepted and the rejected paths are both exercised.  The curated list below
# holds the reduced reproducers for the classes the fixtures do not reach.

use strict;
use warnings;

my $iterations = shift // 2000;
my $seed = shift // 1;

use Cwd qw(abs_path);
use File::Basename qw(dirname);

my $root = dirname(dirname(abs_path($0)));
my $mine = "$root/dev/ppexpr";
my $reference = "$root/reference-binaries/ppexpr";

for my $tool ($mine, $reference)
{
	die "$tool is missing; build it first\n" if !-x $tool;
}

# Operands whose evaluation the grammar accepts.  The signed/unsigned split and
# the widths are deliberate: `char` is signed, `char16_t` and `char32_t` are
# unsigned, and the two decimal boundaries are the ones 2.14.2 sends to
# different types.
my @operands = (
	'0', '1', '2', '3', '7', '64', '65', '521', '124u', '424', '-132',
	'0x2', '03', '0x2u', '03u', '0u', '1u', '4294967295u', '4294967296',
	'9223372036854775807', '9223372036854775807l', '9223372036854775807ll',
	'9223372036854775808', '9223372036854775808u', '0x7fffffffffffffff',
	'0x8000000000000000', '0xffffffffffffffff', '0xffffffffffffffffu',
	'18446744073709551615u', '18446744073709551615ul',
	'18446744073709551616u', '18446744073709551616',
	"'a'", "'\\0'", "'\\xff'", "'\\377'", "'\\u00aa'", "u'a'", "U'a'",
	"L'a'", "u'z'", "U'\\U00102222'", "u'\\U00102222'", "L'\\u03d0'",
	'true', 'false', 'ident', '_x', 'auto', 'sizeof', 'int', 'defined_word',
	'defined a', 'defined b', 'defined (a)', 'defined ( b )',
	'defined (defined)', 'defined true', 'defined auto', 'defined _x',
);

# Everything the phase 3 surface can put on a line, including the tokens the
# controlling-expression grammar has no production for.
my @atoms = (
	@operands,
	'+', '-', '!', '~', 'not', 'compl',
	'*', '/', '%', '+', '-', '<<', '>>', '<', '>', '<=', '>=', '==', '!=',
	'&', '^', '|', '&&', '||', '?', ':', '(', ')',
	'bitand', 'bitor', 'xor', 'and', 'or', 'and_eq', 'or_eq', 'xor_eq',
	'not_eq', '<=>', '++', '--', ',', ';', '=', '::', '.', '.*', '->*',
	'[', ']', '{', '}', '#', '##', '@', '$', '`', '\\',
	'"foo"', '""', 'u8"x"', "u\"x\"", "U\"x\"", "L\"x\"", '"foo"_x',
	'3.2', '3.2f', '1e10', '1e400', "1.0_x", '0x1p3', '1..e',
	"'abc'", "''", "u8'a'", "'a'_x", 'R"(a)"',
	'defined', 'defined(', 'defined(', 'true?', '5uu', '5 7', '==',
	'//comment' . "\n", '/*c*/', '/*', '*/', "\n", "\n", ' ', "\t", "\f",
	"\xC3\xA9", "\xCF\x80", "\x80", "\xA0",
);

# The tool's command-line surface: it takes no options of its own, and the
# harness's worker flag is stripped before a real invocation.  The binary here
# is built with the test runner, whose `main` consumes `--batch-stdin` when
# `WRAPPED_BATCH_STDIN` is set and otherwise falls through to the tool.  The
# reference binary is the one exception in the tree - it answers an unknown
# argument with `ERROR: invalid usage` rather than ignoring it - so the flag is
# compared against this tool's own plain run rather than against the reference.
my ($plain_exit, $plain_out) = run_tool($mine, "1 + 1\n");
my ($flag_exit, $flag_out) = run_tool($mine, "1 + 1\n", '--batch-stdin');
if ($plain_exit != 0 || $flag_exit != $plain_exit || $flag_out ne $plain_out)
{
	print "command-line surface check failed: plain exit=$plain_exit flag exit=$flag_exit\n";
	exit(1);
}

# Nesting: the parser is iterative, so the depth an expression can carry is a
# property of the input rather than of the process stack, and the reference -
# which is the only bound the assignment has - is matched at every depth.  The
# three recursions a recursive-descent parser of this grammar cannot carry -
# parentheses, a prefix chain and a conditional chain - are each checked past
# the depth a C stack of ordinary size could reach, and an unbalanced deep
# input is checked to be the same `error` here as there.
my $nesting_failures = 0;
my @deep = (
	('(' x 1024) . '1' . (')' x 1024) . "\n",
	('(' x 5000) . '1' . (')' x 5000) . "\n",
	('(' x 50000) . '1' . (')' x 50000) . "\n",
	('(' x 200000) . '1' . (')' x 200000) . "\n",
	('!' x 100000) . '1' . "\n",
	('1 ? ' x 20000) . '2' . (': 3' x 20000) . "\n",
	('-' x 100000) . '1' . "\n",
	('(' x 5000) . "1\n",
	('(' x 5000) . "\n",
);
for my $index (0 .. scalar(@deep) - 1)
{
	my ($me, $mo) = run_tool($mine, $deep[$index]);
	my ($re, $ro) = run_tool($reference, $deep[$index]);
	# The exit status is an oracle for every input, and stdout has to agree
	# too: a crash would leave the output truncated and the status non-zero.
	if ($me != $re || $mo ne $ro)
	{
		++$nesting_failures;
		print "NESTING MISMATCH on deep input $index: mine exit=$me ref exit=$re\n";
		print "  mine:\n$mo\n  ref:\n$ro\n";
	}
}
if ($nesting_failures != 0)
{
	print "nesting check failed: $nesting_failures mismatches\n";
	exit(1);
}

# Boundaries the fixture suite does not pin.  They are always checked, before
# the random inputs, so a regression is reported even when the seed changes.
my @curated = (
	# The dead-branch rules: only the arm `?:` did not choose, and the operand
	# a short-circuit skipped, may carry a course-defined value error; both are
	# still parsed and typed.
	"false?5u:-5\n", "false?5/0u:-5\n", "true?5:5/0\n", "true?5/0:5\n",
	"true?5:5<<64\n", "true?5<<64:5\n", "true?5:5<<-1\n", "true?5<<-1:5\n",
	"5||(5%0)\n", "0||(5%0)\n", "0&&(5/0)\n", "5&&(5/0)\n",
	"5||(5<<64)\n", "0||(5<<64)\n", "0&&(5<<64)\n", "5&&(5<<64)\n",
	"5||(5<<-1)\n", "0||(5<<-1)\n", "0&&(5<<-1)\n", "5&&(5<<-1)\n",
	"0?defined(a):1\n", "1?1:0?2:3\n", "0?1:0?2:3\n", "1&&0?4:5\n",
	# A rejection anywhere on the line rejects the line, even where evaluation
	# would never reach it.
	"true?5:3.2\n", "true?5:\"x\"\n", "true?5:1ff\n", "true?5:5uu\n",
	"0&&3.2\n", "1||3.2\n", "true?5:'a'_x\n", "true?5:1.5_x\n",
	# Division by zero and the one signed quotient that has no representation.
	"(1 << 63)/-1\n", "(1 << 63)%-1\n", "(1u << 63)/-1\n", "(1u << 63)%-1\n",
	"5/0\n", "5%0\n", "5u/0\n", "5u%0\n", "5/-0\n", "(-1u)/0u\n",
	"-9223372036854775807-1\n", "(-9223372036854775807-1)/-1\n",
	"0x8000000000000000/-1\n", "0x8000000000000001/2\n",
	# Shifts: the course-defined bound is on the promoted right operand, and
	# the result's type is the promoted left operand's.
	"1 << 63\n", "1 << 64\n", "1u << 63u\n", "1u << 64u\n", "1 << 64u\n",
	"1u << 64\n", "1 >> 63\n", "(-2) >> 1\n", "(-2u) >> 1\n", "1 << 0\n",
	"1 << 6-6\n", "1 << (7-7)\n", "1 << 1 ? 3 : 4\n", "1 << (1 ? 3 : 4)\n",
	# The usual arithmetic conversions, and the comparison result's type.
	"-5 < 5u\n", "-5 < 5\n", "-5u < 5\n", "1u == 1\n", "1u != 1\n",
	"0xffffffffffffffffu > -1\n", "-1 > 0xffffffffffffffffu\n",
	"1 < 2 == 1\n", "1 == 1 == 1\n", "1 & 3u\n", "~0u\n", "~0\n",
	"!0u\n", "!!5\n", "-0u\n", "~u'z'\n", "~'\\xff'\n", "~'\\u00aa'\n",
	"u'\\u03d0' + 1\n", "U'\\U00102222' - 1\n", "L'\\u03d0' * 2\n",
	# `identifier_or_keyword`: keywords are operands and evaluate as 0, and the
	# mock `defined` reads the first code unit's parity.
	"auto\n", "true\n", "false\n", "defined\n", "defined defined\n",
	"defined()\n", "defined ()\n", "defined (a\n", "defined (a + b)\n",
	"defined ()\n", "defined 5\n", "defined 'a'\n", "defined \"a\"\n",
	"defined(a)\n", "defined (a)\n", "defined ((a))\n", "defined ( a )\n",
	"defined a b\n", "defined\tb\n", "defined aha\n", "defined bar\n",
	# Grammar boundaries: an unconsumed token, a missing operand, a stray
	# punctuator, and the empty line that produces no output at all.
	"2 + == 4\n", "5 7\n", "==\n", "1+2+\n", "(123\n", "()\n", "(123)\n",
	"1 ? 2\n", "1 ? : 3\n", "1 : 2\n", "?1:2\n", "1 ? 2 : 3 : 4\n",
	"1 ? 2 : 3 ? 4 : 5\n", "1 ? 2 : (3 ? 4 : 5)\n", "(1) ? (2) : (3)\n",
	"2--2--7\n", "2- -2- -7\n", "2--2\n", "2- -2\n", "\n", "   \n", "\t\n",
	"/* blank */\n", "// blank\n", "\n\n\n", " \n\t\n \n",
	# Phase 1-3 material: a splice inside a logical line, a comment inside an
	# expression, and the phase 3 failure that must exit non-zero.
	"1 + \\\n2\n", "1 /*c*/ + 2\n", "1 +\n2\n", "1\\\n+2\n",
	"\"unterminated\n", "'unterminated\n", "R\"(a\n", "0x\n",
	# The result type of a `?:` arm comes from both arms, and a rejected token
	# anywhere on the line rejects the line even in a dead arm.  The first two
	# are the reducers for the arm-typing defect: a comparison's result is
	# `bool`, so it is signed `intmax_t` however unsigned its operands are, and
	# that has to hold for an arm that is not evaluated.
	"0 ? 1u == 1u : 1\n", "0 ? 1u < 2u : 1\n", "0 ? 1u != 1u : 1\n",
	"0 ? 1u == 1u : -1\n", "1 ? 1 : 1u == 1u\n", "0 ? 2 : 1u == 1u\n",
	"0 ? 1u == 1u : (1u == 1u)\n", "0 ? 1u && 1u : 1\n",
	# A `+`, `-`, `*` or unary `-` on the signed promoted type whose value is
	# not representable is an error, and an unsigned one wraps.  Shifts are
	# exempt: `1 << 63` is the most negative value rather than an error.
	"9223372036854775807 + 1\n", "9223372036854775807 + 0\n",
	"-9223372036854775807 - 1\n", "-9223372036854775807 - 2\n",
	"9223372036854775807 - -1\n", "2 * 9223372036854775807\n",
	"-2 * -9223372036854775807\n", "4611686018427387904 * 2\n",
	"4611686018427387904 * 4\n", "-4611686018427387904 * 2\n",
	"-(-9223372036854775807 - 1)\n", "+(-9223372036854775807 - 1)\n",
	"!(-9223372036854775807 - 1)\n", "~(-9223372036854775807 - 1)\n",
	"(-9223372036854775807 - 1) < 0\n", "true?5:9223372036854775807+1\n",
	"false?9223372036854775807+1:5\n", "1||(9223372036854775807+1)\n",
	"0&&(9223372036854775807+1)\n", "18446744073709551615u + 1\n",
	"1u + 9223372036854775807\n", "0x8000000000000000 * 2\n",
	"1 << 63\n", "2 << 62\n", "9223372036854775807 * 1\n",
	# Deep nesting is compared against the reference too, in the block above the
	# curated list: the parser holds its own stacks on the heap, so an
	# expression nested past what a C stack could carry is the same value here
	# as there.
	# Integer-literal boundaries across the bases and suffixes.
	"01777777777777777777777\n", "0x7fffffffffffffff\n",
	"0b1010\n", "0B1010\n", "08\n", "1'000\n", "1e1\n", "0e1\n",
);

srand($seed);
my $mismatches = 0;
my @inputs = (@curated, map { make_input() } (1 .. $iterations));
for my $iteration (1 .. scalar(@inputs))
{
	my $input = $inputs[$iteration - 1];
	my ($mine_exit, $mine_out) = run_tool($mine, $input);
	my ($ref_exit, $ref_out) = run_tool($reference, $input);
	# The exit status is an oracle for every input.  Only the stdout of a
	# rejected input is informational, so that case compares nothing further.
	if ($mine_exit == $ref_exit && $ref_exit != 0)
	{
		next;
	}
	next if $mine_exit == $ref_exit && $mine_out eq $ref_out;

	++$mismatches;
	print "MISMATCH on iteration $iteration\n";
	print "input: ", unpack('H*', $input), "\n";
	print "  exit status: mine=$mine_exit ref=$ref_exit\n" if $mine_exit != $ref_exit;
	print "  mine:\n", $mine_out, "\n";
	print "  ref:\n", $ref_out, "\n";
	last if $mismatches >= 10;
}

print $mismatches == 0
	? "differential check passed: $iterations inputs\n"
	: "differential check failed: $mismatches mismatches\n";
exit($mismatches == 0 ? 0 : 1);

sub make_input
{
	# Every few lines is a whole file, so a run exercises several logical lines
	# and the phase 3 state between them.
	my $line_count = 1 + int(rand(4));
	my $text = '';
	for (my $line = 0; $line < $line_count; ++$line)
	{
		my $roll = rand();
		if ($roll < 0.60)
		{
			$text .= make_expression() . "\n";
		}
		elsif ($roll < 0.90)
		{
			$text .= make_soup() . "\n";
		}
		else
		{
			$text .= $atoms[int(rand(scalar(@atoms)))] . "\n";
		}
	}
	return $text;
}

sub make_expression
{
	my $depth = shift // 0;
	my $roll = rand();
	if ($depth >= 5 || $roll < 0.30)
	{
		return $operands[int(rand(scalar(@operands)))];
	}
	if ($roll < 0.42)
	{
		my @unary = ('+', '-', '!', '~');
		return $unary[int(rand(scalar(@unary)))] . make_expression($depth + 1);
	}
	if ($roll < 0.62)
	{
		return '(' . make_expression($depth + 1) . ')';
	}
	if ($roll < 0.78)
	{
		my @short = ('&&', '||');
		return make_expression($depth + 1) . ' ' .
			$short[int(rand(scalar(@short)))] . ' ' . make_expression($depth + 1);
	}
	if ($roll < 0.88)
	{
		return make_expression($depth + 1) . ' ? ' .
			make_expression($depth + 1) . ' : ' . make_expression($depth + 1);
	}
	my @binary = ('*', '/', '%', '+', '-', '<<', '>>', '<', '>', '<=', '>=',
		'==', '!=', '&', '^', '|');
	return make_expression($depth + 1) . ' ' .
		$binary[int(rand(scalar(@binary)))] . ' ' . make_expression($depth + 1);
}

sub make_soup
{
	my $length = 1 + int(rand(7));
	my $text = '';
	for (my $i = 0; $i < $length; ++$i)
	{
		$text .= $atoms[int(rand(scalar(@atoms)))];
	}
	return $text;
}

sub run_tool
{
	my ($tool, $input, $args) = @_;
	my $in = "/tmp/ppexpr_differential.in.$$";
	my $out = "/tmp/ppexpr_differential.out.$$";
	open(my $fh, '>', $in) or die "cannot write $in: $!";
	binmode($fh);
	print $fh $input;
	close($fh);
	$args = '' if !defined $args;
	my $status = system("$tool $args < $in > $out 2>/dev/null");
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
