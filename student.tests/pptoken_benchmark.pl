#!/usr/bin/perl
#
# Compiler benchmark for the pa1 frontend.
#
# Generates a fixed, reproducible translation unit that exercises the phase 1-3
# mix (identifiers, pp-numbers, punctuation, comments, escapes, raw strings,
# header-names and line splices), checks that the student compiler and the
# reference wrapper agree on its output, and then reports paired ABBA wall-time
# and peak-RSS blocks for both.
#
# This is a personal benchmark: it is not part of the course contract and is not
# discovered by `make test`.
#
#   perl student.tests/pptoken_benchmark.pl [lines] [blocks]

use strict;
use warnings;

use Cwd qw(abs_path);
use File::Basename qw(dirname);

my $lines = shift // 40000;
my $blocks = shift // 6;

my $root = dirname(dirname(abs_path($0)));
my $mine = "$root/dev/pptoken";
my $reference = "$root/reference-binaries/pptoken";
my $corpus = "/tmp/pptoken_benchmark.cpp";

for my $tool ($mine, $reference)
{
	die "$tool is missing; build it first\n" if !-x $tool;
}

sub generate
{
	my ($path, $count) = @_;
	open(my $fh, '>', $path) or die "cannot write $path: $!";
	binmode($fh);
	for my $i (1 .. $count)
	{
		print $fh "int f$i(int a, int b) { /* c */ return a + b * $i; } // note $i\n";
		print $fh "#include <header$i.h>\n" if $i % 500 == 0;
		print $fh "const char* s$i = R\"d$i(raw string)d$i\";\n" if $i % 1000 == 0;
		print $fh "double d$i = 1.0e$i + .5 + 0x1p3;\n" if $i % 700 == 0;
		print $fh "auto u$i = u8\"prefix\"_suffix;\n" if $i % 900 == 0;
		print $fh "char c$i = '\\x41';\n" if $i % 300 == 0;
		print $fh "wchar_t w$i = L'\\u0024';\n" if $i % 1100 == 0;
		print $fh "// multi-line \\\ncontinued comment\n" if $i % 1500 == 0;
		print $fh "int t$i = a<::b>c;\n" if $i % 1300 == 0;
	}
	close($fh);
	return -s $path;
}

sub run_tool
{
	my ($tool, $input, $output) = @_;
	my $timing = `{ /usr/bin/time -f '%e %M' $tool < $input > $output; } 2>&1`;
	chomp($timing);
	my ($seconds, $rss) = split(/\s+/, $timing);
	return (defined($seconds) ? $seconds + 0 : undef, defined($rss) ? $rss + 0 : undef);
}

my $size = generate($corpus, $lines);
print "corpus: $corpus ($size bytes, $lines generated blocks)\n";

run_tool($reference, $corpus, "/tmp/pptoken_benchmark.ref");
run_tool($mine, $corpus, "/tmp/pptoken_benchmark.my");
my $identical = system("cmp -s /tmp/pptoken_benchmark.ref /tmp/pptoken_benchmark.my") == 0;
print "output identical to reference: ", ($identical ? "yes" : "NO"), "\n";
exit(1) if !$identical;

my (%observed, %samples);
for my $block (1 .. $blocks)
{
	for my $order (['ref', $reference], ['mine', $mine], ['mine', $mine], ['ref', $reference])
	{
		my ($label, $tool) = @{$order};
		my ($seconds, $rss) = run_tool($tool, $corpus, "/dev/null");
		push @{$samples{$label}{time}}, $seconds;
		push @{$samples{$label}{rss}}, $rss;
	}
}

sub summarize
{
	my ($values) = @_;
	my @sorted = sort { $a <=> $b } @{$values};
	my $count = scalar(@sorted);
	my $median = $count % 2 ? $sorted[($count - 1) / 2]
		: ($sorted[$count / 2 - 1] + $sorted[$count / 2]) / 2;
	return ($median, $sorted[0], $sorted[$count - 1]);
}

for my $label ('ref', 'mine')
{
	my ($time, $time_min, $time_max) = summarize($samples{$label}{time});
	my ($rss, $rss_min, $rss_max) = summarize($samples{$label}{rss});
	printf "%-5s latency %0.3f s [%0.3f..%0.3f]   peak RSS %0.1f MB [%0.1f..%0.1f]\n",
		$label, $time, $time_min, $time_max,
		$rss / 1024, $rss_min / 1024, $rss_max / 1024;
}
