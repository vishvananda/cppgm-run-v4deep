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
	"R\"(a\\\nb)\"", "R\"d(x)d\"", 'u8R"(x)y', "\\\nR\"(x)\"",
	'??=', '????=', '??/', '?', '??', '???',
	"\xC3\xA9", "\xCF\x80", "\xF0\x9F\x98\x80", "\xEF\xBB\xBF",
	"\377", "\xC0\x80", "\xE2\x82",
	"\x80", "\x85", "\x91", "\x9F", "\xA0", "\xAD", "\xBF", "\x81",
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
	my ($tool, $input, $args) = @_;
	my $in = "/tmp/pptoken_differential.in.$$";
	my $out = "/tmp/pptoken_differential.out.$$";
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

# The tool's command-line surface: it takes no options, so the harness worker
# flag must be inert.  The skeleton answers `--batch-stdin` with a per-line
# "not implemented" record until the tool is written; an implemented tool that
# keeps that stub silently reports failure for every request, so pin the flag
# here against the reference before the fuzz starts.
my $cli_failures = 0;
for my $args ('', '--batch-stdin')
{
	my ($mine_exit, $mine_out) = run_tool($mine, "int a;\n", $args);
	my ($ref_exit, $ref_out) = run_tool($reference, "int a;\n", $args);
	if ($mine_exit != $ref_exit || $mine_out ne $ref_out)
	{
		++$cli_failures;
		print "CLI MISMATCH with args '$args': mine exit=$mine_exit ref exit=$ref_exit\n";
		print "  mine:\n$mine_out\n  ref:\n$ref_out\n";
	}
}
if ($cli_failures != 0)
{
	print "command-line surface check failed\n";
	exit(1);
}

# Boundaries that the fixture suite does not pin, each one a place where two
# readings of the grammar disagree.  They are always checked, before the random
# inputs, so a regression is reported even when the seed changes.
my @curated = (
	"#include\"\"\n", "#include <>\n", "#include\"a\"\n", "#include < >\n",
	"#include \"foo\n", "#include <foo\n", "#include\"\"x\n",
	"\\u0041\n", "\\\\u0041\n", "\\\\\\u0041\n", "\\\\uD800\n",
	"\"\\\\u0041\"\n", "\"\\uZZZZ\"\n", "'\\u0024'\n", "\\u0024\n",
	"R\"a(x)aaa\"xxx)aa\"\n", "R\"(a\\\nb)\"\n", "R\"()\"\n", "R\"()\"_x\n",
	"R\"abc\"\n", "R\"1234567890123456()\"\n", "R\"12345678901234567()\"\n",
	"<::", "<::>", "<:::", "<::\n", "%:include <c>\n", "%:%:include <d>\n",
	"a\\\n", "a\\\\\nb\n", "???=\n", "????=\n", "??/u0040\n", "??/\n",
	"/*c*/\n", "/*\n*/a\n", "//x", "a", "", "\n", "\xff", "\xc0\x80",
	"s<::t\n", "x<:3:>y\n", "\"a\"b\n", "\"a\"_w\n", "'a'_w\n",
	"u8'x'\n", "u8\"x\"\n", "u8R\"(x)\"\n", "LR\"(x)\"\n", "uR\"(x)\"\n",
	# A byte in 0x80-0xBF cannot begin a UTF-8 sequence.  The reference reads it
	# as one Windows-1252 code point rather than rejecting it; these are the
	# reduced reproducers for that rule (0x80 -> U+20AC, 0x82 -> U+201A,
	# 0xA0 -> U+00A0) and for the bytes around it, which stay errors.
	"a\x80b\n", "\x80\n", "\x82\n", "\xA0\n", "\xBF\n", "\x81\n",
	"/*\x80*/\n", "\"\x80\"\n", "R\"(\x80)\"\n", "#include <\x80>\n",
	"R\"\x80(a)\x80\"\n", "\xA0\xBF\x80\x81\n", "a\x80\xA0b\n",
	"\xC0\x80\n", "\xC1\x81\n", "\xF5\x80\x80\x80\n", "\xF8\n", "\xFF\n",
	# `#include_next` is the GNU extension the library headers are written in
	# terms of, and it takes a header-name exactly like `#include` does.
	"#include_next <a>\n", "#include_next \"a\"\n", "%:include_next <c>\n",
	"#include_next <>\n", "#include_next\n<a>\n", "#including <a>\n",
	"#includeX <a>\n", "#includefoo <a>\n", "x #include_next <a>\n",
	"#if 1\n#include_next <a>\n#endif\n",
	# A literal-operator-id is `operator "" identifier`, so after `operator` the
	# empty string literal and its suffix must arrive as separate tokens.
	"operator\"\"s(int);\n", "operator \"\"s\n", "operator\"\"_x\n",
	"operator\"\"not\n", "operator\"a\"s\n", "operator L\"\"s\n",
	"operator''s\n", "(operator\"\"s)\n", "not\"\"s\n", "operator\"\"8x\n",
	"operator\"\"\n", "operator\n\n\"\"s\n",
	# A binary exponent sign belongs to a pp-number only when it began with the
	# hexadecimal prefix; a decimal exponent sign belongs to every pp-number.
	"0x1P+1\n", "0x1.5bf0a8b145769P+1\n", "0x1.5bf0a8b145769p-1\n",
	"0xP+P\n", "0x1uP+1\n", "0x1P+\n", "0x1P++1\n", "0xx1P+1\n",
	"1P+3\n", "1p+3\n", "10x1P+1\n", "00x1P+1\n", ".0x1P+1\n",
	"0x1P+1P+1\n", "0x1E-1\n", "0e+1\n",
);

srand($seed);
my $mismatches = 0;
my @inputs = (@curated, map { make_input($_) } (1 .. $iterations));
for my $iteration (1 .. scalar(@inputs))
{
	my $input = $inputs[$iteration - 1];
	my ($mine_exit, $mine_out) = run_tool($mine, $input);
	my ($ref_exit, $ref_out) = run_tool($reference, $input);
	# The exit status is an oracle for every input: a disagreement is a
	# divergence whether or not the input is well-formed.  Only the stdout of a
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
