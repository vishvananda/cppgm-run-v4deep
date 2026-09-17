#!/usr/bin/perl
#
# Differential check for pa2 posttoken.
#
# Runs the student compiler and the reference wrapper over generated inputs and
# reports any difference in stdout or exit status.  This is a personal test: it
# is not part of the course contract and is not discovered by `make test`.
#
#   perl student.tests/posttoken_differential.pl [iterations] [seed]

use strict;
use warnings;

my $iterations = shift // 2000;
my $seed = shift // 1;

use Cwd qw(abs_path);
use File::Basename qw(dirname);

my $root = dirname(dirname(abs_path($0)));
my $mine = "$root/dev/posttoken";
my $reference = "$root/reference-binaries/posttoken";

for my $tool ($mine, $reference)
{
	die "$tool is missing; build it first\n" if !-x $tool;
}

# The atoms lean on the parts of PA2 that the fixtures pin thinly: the
# pp-number grammar's four productions and their boundaries, integer type
# selection, escape-sequence code units, the encoding-prefix and ud-suffix
# unions of phase 6, and raw strings.
my @atoms = (
	# simple tokens and identifiers
	'a', 'foo', '_x', 'auto', 'and', 'or_eq', 'not', 'new', 'nullptr',
	'delete', 'true', 'false', 'operator', '+', '-', '+=', '--', '->*',
	'->', '<<=', '>>=', '<<', '>>', '<=', '>=', '<:', ':>', '<%', '%>',
	'%:', '%:%:', '##', '#', '::', '...', '..', '.', '.*', '?', '(', ')',
	'[', ']', '{', '}', ';', ',', ':', '=', '!', '~', '^', '&', '|',
	'/', '*', '%', '@', '$', '`', '\\',
	# pp-numbers
	'0', '1', '42', '00', '07', '09', '0x0', '0X', '0x', '0b1',
	'2147483647', '2147483648', '4294967295', '4294967296',
	'9223372036854775807', '9223372036854775808',
	'18446744073709551615', '18446744073709551616',
	'01777777777777777777777', '02000000000000000000000',
	'0x7FFFFFFFFFFFFFFF', '0x8000000000000000', '0xFFFFFFFFFFFFFFFF',
	'0x10000000000000000',
	'l', 'L', 'll', 'LL', 'lL', 'Ll', 'u', 'U', 'ul', 'lu', 'ull', 'llu',
	'uL', 'Lu', 'uLL', 'LLu', 'ulL', 'lLu', 'W', 'X',
	'1.0', '.5', '1.', '0.', '1e0', '1E+0', '1e-0', '.5e2', '1.e2',
	'1..2', '1.2.3', '1e', '1e+', '1e-', '1e+_x', '1e_x', '2e_3',
	'0x1p3', '0X1P-2', '0x1.8p3', '0x.8p3', '0x1p', '0x1.8', '0x.p3',
	'1f', '1.0f', '1.0F', '1.0l', '1.0L', '1e5f', '1e5L', '1.0ff',
	'0x1p3f', '0x1p3l',
	# user-defined numeric literals
	'1_x', '1e5_x', '1.0_x', '0x1p3_x', '0x12_p3', '123_e3', '07_e9',
	'1_p', '1_e+', '1_e+1', '1.0_foo.bar', '1_.', '1_x.', '0x_ud', '0_x',
	'123l_ud', '123ll_ud', '1u_ud', '1.0f_ud', '2.0_ud_f', '0_ud1lL',
	'02777000000000000000000_id1', '3.14e+20000_id2', '2.7e-20000_id2',
	'123ud', '1.5ud', '0x1ud',
	# binary literals: the course frontend takes the C++14 form, which
	# pa29's host-binary-integer-ud-literal fixture requires
	'0b0', '0b1', '0b1010', '0B11110000', '0b', '0b2', '0b1u', '0b1l',
	'0b1ull', '0b1_ud', '0b1.0', '010b1', '0b11111111111111111111111111111111',
	'0b111111111111111111111111111111111', '0b1111111111111111111111111111111111111111111111111111111111111111',
	# character literals
	q{'a'}, q{'\''}, q{'\"'}, q{'\?'}, q{'\\'}, q{'\a'}, q{'\b'},
	q{'\f'}, q{'\n'}, q{'\r'}, q{'\t'}, q{'\v'}, q{'\0'}, q{'\101'},
	q{'\777'}, q{'\400'}, q{'\x41'}, q{'\xFF'}, q{'\x100'}, q{'\xD800'},
	q{'\xE000'}, q{'\x10FFFF'}, q{'\x110000'}, q{'\u0041'}, q{'\U0001D11E'},
	q{''}, q{'ab'}, q{'\x41\x42'}, q{'\n\r'}, "'\xC3\xA9'", "'\xCF\x80'",
	q{'\xF'}, q{'\x'}, q{'\8'}, q{'\u00'},
	q{u'a'}, q{U'a'}, q{L'a'}, q{u'\x10000'}, q{u'\xFFFF'}, q{U'\xFFFFFFFF'},
	q{U'\x100000000'}, q{L'\x100000000'}, "u'\xC3\xA9'", q{u8'a'},
	# character literals with suffixes
	q{'a'_x}, q{'a'_}, q{'a'x}, q{'a'1}, q{u'a'_u1}, q{L'a'_L1}, q{'\xFF'_q},
	# string literals
	q{""}, q{"a"}, q{"abc"}, q{"\n"}, q{"\x41"}, q{"\xFF"}, q{"\x100"},
	q{"\101"}, q{"\400"}, q{"\777"}, q{"\0"}, q{"\u0041"}, q{"\U0001D11E"},
	'"\xC3\xA9"', '"\xCF\x80"', '"\xF0\x9D\x84\x9E"',
	q{"\'"}, q{"\""}, q{"\\"}, q{"\?"}, q{"\a"}, q{"\b"}, q{"\f"},
	q{"\r"}, q{"\t"}, q{"\v"}, q{"\8"}, q{"\x"}, q{"\x110000"},
	q{"a\x100b"},
	q{u"a"}, q{U"a"}, q{L"a"}, q{u8"a"}, q{u"\x10000"}, q{u"\xD800"},
	q{U"\x110000"}, q{u"\xFFFF"}, q{u8"\xFF"}, q{L"\x100000000"},
	'"\xEF\xBB\xBF"', 'u"\xCF\x80"', 'U"\xF0\x9D\x84\x9E"',
	# string literals with suffixes
	q{"a"_x}, q{"a"x}, q{"a"_}, q{"a"_x1}, q{u"a"_x}, q{"a"yyy},
	# raw strings
	q{R"(a)"}, q{R"()"}, q{R"x(a)x"}, q{R"x(a)xx"}, q{R"(a\nb)"},
	q{R"(\x41)"}, q{R"(\u0041)"}, q{R"(a"b)"}, "R\"(a\nb)\"",
	"R\"(a\\\nb)\"", q{u8R"(a)"}, q{uR"(a)"}, q{UR"(a)"}, q{LR"(a)"},
	q{R"(a)"_x}, q{R"(a)"x}, q{R"delim(x)delim"}, q{R"a(x)a"},
	# whitespace, comments and new-lines (all invisible to phase 6)
	' ', '  ', "\t", "\n", "\n\n", '//c', '/*c*/', '/**/', '/*\n*/',
	'\\' . "\n",
);

# Boundaries the fixture suite does not pin, each a place where two readings of
# the grammar disagree.  Always checked, before the random inputs.
my @curated = (
	# the maximal string-literal sequence crosses new-lines, comments and
	# whitespace, and every element must agree on prefix and ud-suffix
	"\"a\" \"b\"\n", "\"a\"\n\"b\"\n", "\"a\"/*c*/\"b\"\n",
	"u8\"a\" \"b\"\n", "\"a\" u8\"b\"\n", "u\"a\" \"b\"\n",
	"L\"a\" U\"b\"\n", "u8\"a\" u\"b\"\n", "\"a\"_x \"b\"\n",
	"\"a\"_x \"b\"_x\n", "\"a\"_x \"b\"_y\n", "\"a\"_x \"b\"_x \"c\"\n",
	"\"a\"x \"b\"\n", "u8\"a\"_x \"b\"_y\n", "\"a\" \"b\"x\n",
	"R\"(a)\" \"b\"\n", "\"a\" R\"(b)\"\n", "R\"(a)\" LR\"(b)\"\n",
	"\"a\" u8R\"(b)\"\n", "\"a\";\"b\"\n", "\"a\" 1 \"b\"\n",
	# the numeric escape of a concatenated sequence keeps the *sequence's*
	# element type, not the element literal's own
	"\"\\xFF\" u8\"a\"\n", "u\"a\" \"\\xFFFF\"\n", "u\"a\" \"\\x10000\"\n",
	"\"a\" \"\\x100\"\n", "u8\"\\xFF\" \"a\"\n",
	# integer type selection boundaries
	"2147483647 2147483648 4294967295 4294967296\n",
	"2147483648l 4294967296l 9223372036854775807l\n",
	"9223372036854775808l 18446744073709551615l\n",
	"9223372036854775808u 18446744073709551615u\n",
	"18446744073709551616u 18446744073709551616ul\n",
	"18446744073709551615ull 18446744073709551615llu\n",
	"0xFFFFFFFF 0xFFFFFFFFFFFFFFFF 0x10000000000000000\n",
	"0xFFFFFFFFl 0xFFFFFFFFFFFFFFFFl 0xFFFFFFFFFFFFFFFFu\n",
	"0xFFFFFFFFll 0xFFFFFFFFFFFFFFFFll\n",
	"017777777777 020000000000 01777777777777777777777\n",
	"017777777777l 02000000000000000000000l\n",
	"0 00 000 0x0 0X0 0u 0l 0ll 0ul 0ull\n",
	"0lL 0Ll 0ulL 0lLu 0uLl 0uLL 0LLu 0llu\n",
	# pp-number boundaries
	"0x1p3 0x1P-2 0x.8p3 0x1.8p3 0x1p 0x1.8 0x.p3\n",
	"1.0e0 .5 1. 0. 1e+0 1e-0 1..2 1.2.3\n",
	"1e 1e+ 1e- 1e_x 1_e+ 1_e+1 1.0_foo.bar\n",
	"1_x 1e5_x 1.0_x 0x1p3_x 0x12_p3 123_e3 07_e9\n",
	"1_p 0_x 0x_ud 1_. 1_x. 2.0_ud_f 0_ud1lL\n",
	"123ud 1.5ud 0x1ud 123l_ud 123ll_ud 1u_ud 1.0f_ud\n",
	"123_e3 123_ae3 123_xp3 0x12_p3 07_e9\n",
	# character literal boundaries
	"'\\777' '\\400' '\\x100' '\\xFF' '\\xD800' '\\xE000' '\\x110000'\n",
	"'\\x41\\x42' '' 'ab' '\\0' '\\101'\n",
	"u'\\xFFFF' u'\\x10000' U'\\xFFFFFFFF' U'\\x100000000'\n",
	"L'\\xFFFFFFFF' L'\\x100000000' u'\\xD800' U'\\xD800'\n",
	"'a'_x 'a'x 'a'_ u'a'_u1 '\xC3\xA9'_y\n",
	# string literal boundaries
	"\"\\xFF\" \"\\x100\" \"\\xD800\" u\"\\xD800\" u\"\\x10000\"\n",
	"U\"\\x110000\" \"\\x110000\" \"a\\x100b\" u\"A\\xD800\" u\"B\"\n",
	"\"\\u03C0\" \"\\U0001D11E\" \"\\x3C0\" \"\\x1D11E\" \"\\101\"\n",
	# the `operator "" identifier` split
	"operator\"\"s\n", "operator\"\"sv\n", "operator\"\"_x\n",
	"operator \"\"s\n", "operator\"\"\n", "operator\"a\"s\n",
	# `#`, `##`, `%:` and `%:%:` have no post token
	"# ## %: %:%:\n", "#x\n", "a ## b\n",
	# non-whitespace characters
	"@ \$ ` \\\\\n", "\x7F\n", "\x01\n",
	# a header-name is only a header-name in an include directive, and PA2
	# makes it invalid either way
	"#include <a>\n", "#include \"a\"\n", "<a>\n", "\"a\"\n",
	# an empty translation unit still ends with eof
	"", "\n", " ", "//c", "/*c*/",
);

sub make_input
{
	my $rng = shift;
	my $length = 1 + int(rand(20));
	my $text = '';
	for (my $i = 0; $i < $length; ++$i)
	{
		$text .= $atoms[int(rand(scalar(@atoms)))];
	}
	$text .= "\n" if rand() < 0.9;
	return $text;
}

sub run_tool
{
	my ($tool, $input) = @_;
	my $in = "/tmp/posttoken_differential.in.$$";
	my $out = "/tmp/posttoken_differential.out.$$";
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

# The tool's command-line surface: it takes no options, so the harness worker
# flag must be inert.  The skeleton answers `--batch-stdin` with a per-line
# "not implemented" record until the tool is written; an implemented tool that
# keeps that stub silently reports failure for every request, so pin the flag
# here against the reference before the fuzz starts.
my $cli_failures = 0;
for my $args ('', '--batch-stdin', '-o /tmp/posttoken_differential.out')
{
	my ($mine_exit, $mine_out) = run_tool("$mine $args", "int a;\n");
	my ($ref_exit, $ref_out) = run_tool("$reference $args", "int a;\n");
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

srand($seed);
my $mismatches = 0;
my @inputs = (@curated, map { make_input($_) } (1 .. $iterations));
for my $iteration (1 .. scalar(@inputs))
{
	my $input = $inputs[$iteration - 1];
	my ($mine_exit, $mine_out) = run_tool($mine, $input);
	my ($ref_exit, $ref_out) = run_tool($reference, $input);
	# The exit status is an oracle for every input.  Only the stdout of a
	# rejected input is informational, so that case compares nothing further.
	next if $mine_exit == $ref_exit && $ref_exit != 0;
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
