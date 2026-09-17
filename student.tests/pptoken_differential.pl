#!/usr/bin/perl
#
# Differential check for pa1 pptoken.
#
# Runs the student compiler and the reference wrapper over generated inputs and
# reports any difference in stdout or exit status.  This is a personal test: it
# is not part of the course contract and is not discovered by `make test`.
#
#   perl student.tests/pptoken_differential.pl [iterations] [seed]

use strict;
use warnings;

my $iterations = shift // 2000;
my $seed = shift // 1;

use Cwd qw(abs_path);
use File::Basename qw(dirname);

my $root = dirname(dirname(abs_path($0)));
my $mine = "$root/dev/pptoken";
my $reference = "$root/reference-binaries/pptoken";

for my $tool ($mine, $reference)
{
	die "$tool is missing; build it first\n" if !-x $tool;
}

my @atoms = (
	'a', 'b', 'foo', '_x', 'Z9', 'new', 'delete', 'and', 'or_eq', 'not',
	'0', '42', '1.0e2', '0x1p3', '.5', '1..e', '5e+', '10.0_x',
	'+', '-', '+=', '--', '->*', '->', '<<=', '>>=', '<<', '>>', '<=', '>=',
	'<:', ':>', '<%', '%>', '%:', '%:%:', '##', '#', '<::', '<::>', '<:::',
	'::', ':::', '...', '..', '.', '.*', '?', '???', '??=', '??/', '??-',
	'(', ')', '[', ']', '{', '}', ';', ',', ':', '=', '!', '~', '^', '&', '|',
	'/', '*', '%', '@', '$', '`',
	'"', '""', '"abc"', '"a\\"b"', '"\\\\"', 'u8"x"', 'L"x"', 'U"x"',
	"'", "''", "'a'", "'\\\\''", "L'a'", "u'a'", "U'a'",
	'R"(a)"', 'R"x(a)x"', 'R"()"', 'u8R"(a)"', 'LR"(a)"',
	'"abc"_s', "'a'_s", 'R"(a)"x',
	'\\u0041', '\\u0024', '\\U0001D11E', '\\uD800', '\\uZZZZ', '\\n', '\\',
	'u', 'u8', 'R', 'L', 'U', 'r',
	'\\', "\n", ' ', '  ', "\t", "\r", "\f", "\x0b",
	'//c', '/*c*/', '/*', '*/', '//', '/**/',
	'#include', '<foo>', '"header.h"', '#define', '#ifdef',
	"#include <a>\n", "#include \"b\"\n", "#include\n", "%:include <c>\n",
	"#include <a\n", "#include \"b\n", "a\n#include <d>\n",
	"\n", "\n\n", " \n", "/*c*/\n", "//c\n", "\n//c", "/*c*/", "/**/",
	'"\\u0024"', "'\\u0024'", '"\\x41"', '"\\x"', '"\\8"', '"\\04"',
	'\\u0041\\u0024', '\\u', '\\U', '\\U00110000', '\\u00A0',
	"R\"delim(x)delim\"", 'R"a(b"c)a"', "R\"(a\nb)\"", 'uR"(a)"', 'UR"(a)"',
	'"a"b', "'a'b", '"a"8', '"a"_', 'R"(a)"_',
	'??=', '????=', '??/', '?', '??', '???',
	"\xC3\xA9", "\xCF\x80", "\xF0\x9F\x98\x80", "\xEF\xBB\xBF",
	"\377", "\xC0\x80", "\xE2\x82",
	'??=include', '??=/*c*/', '\\' . "\n",
);

sub make_input
{
	my $rng = shift;
	my $length = 1 + int(rand(28));
	my $text = '';
	for (my $i = 0; $i < $length; ++$i)
	{
		$text .= $atoms[int(rand(scalar(@atoms)))];
	}
	$text .= "\n" if rand() < 0.85;
	return $text;
}

sub run_tool
{
	my ($tool, $input) = @_;
	my $in = "/tmp/pptoken_differential.in.$$";
	my $out = "/tmp/pptoken_differential.out.$$";
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

srand($seed);
my $mismatches = 0;
for my $iteration (1 .. $iterations)
{
	my $input = make_input($iteration);
	my ($mine_exit, $mine_out) = run_tool($mine, $input);
	my ($ref_exit, $ref_out) = run_tool($reference, $input);
	# Failing-case stdout is informational; only the status must agree.
	next if $mine_exit != $ref_exit;
	next if $ref_exit != 0;
	next if $mine_out eq $ref_out;

	++$mismatches;
	print "MISMATCH on iteration $iteration\n";
	print "input: ", unpack('H*', $input), "\n";
	print "  mine:\n", $mine_out, "\n";
	print "  ref:\n", $ref_out, "\n";
	last if $mismatches >= 10;
}

print $mismatches == 0
	? "differential check passed: $iterations inputs\n"
	: "differential check failed: $mismatches mismatches\n";
exit($mismatches == 0 ? 0 : 1);
