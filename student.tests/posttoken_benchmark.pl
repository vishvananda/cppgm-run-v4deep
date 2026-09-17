#!/usr/bin/perl
#
# Compiler benchmark for the pa2 posttoken stage.
#
# Generates a fixed, reproducible translation unit that exercises the phase 4-7
# token mix (keywords, punctuators, the four pp-number productions, integer
# type selection, character and string literals with escapes, phase 6
# concatenation and raw strings), checks that the student compiler and the
# reference wrapper agree on its output byte for byte, then reports paired ABBA
# wall-time and peak-RSS blocks for both.
#
# The protocol is the one the specification asks for: fixed binaries, flags and
# input; wall-time ABBA blocks; an A/A arm that measures the reference against
# itself to calibrate the noise floor; paired per-block differences as well as
# per-label medians and spread; every observation kept in a TSV next to the
# generated corpus.  PA2 has no executable output, so only compiler latency and
# peak RSS are reported.
#
# This is a personal benchmark: it is not part of the course contract and is not
# discovered by `make test`.
#
#   perl student.tests/posttoken_benchmark.pl [lines] [blocks]

use strict;
use warnings;

use Cwd qw(abs_path);
use File::Basename qw(dirname);
use Time::HiRes qw(time);

my $lines = shift // 40000;
my $blocks = shift // 6;

my $root = dirname(dirname(abs_path($0)));
my $mine = "$root/dev/posttoken";
my $reference = "$root/reference-binaries/posttoken";
my $corpus = "/tmp/posttoken_benchmark.txt";
my $samples_path = "/tmp/posttoken_benchmark.samples.tsv";

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
		# The declaration shape the later stages parse, so the token mix is the
		# one a real translation unit produces.
		print $fh "int f$i(int a, int b) { return a + b * $i; }\n";
		print $fh "unsigned long long n$i = ${i}ull;\n";
		print $fh "long m$i = 0x${i}a; double d$i = ${i}.5e-3;\n";
		print $fh "char c$i = '\\x41'; wchar_t w$i = L'\\u0024';\n" if $i % 4 == 0;
		print $fh "const char* s$i = \"a\\tb\" \"c\\\\d\"_tag;\n" if $i % 5 == 0;
		print $fh "const char* r$i = R\"d$i(raw $i \\n text)d$i\";\n" if $i % 7 == 0;
		print $fh "auto u$i = 0b1$i > 1.0f && 0b101u;\n" if $i % 9 == 0;
		print $fh "float g$i = 0x1.8p3f; double h$i = 0x1p-2;\n" if $i % 11 == 0;
		print $fh "u16string t$i = u\"wide\" \"joined\";\n" if $i % 13 == 0;
		print $fh "// comment $i\n#include <header$i.h>\n" if $i % 20 == 0;
	}
	close($fh);
	return -s $path;
}

# Wall time for one run of the tool over the corpus.  Timed around the child
# directly, so the reported latency is not quantised by the 10 ms ceiling of
# /usr/bin/time's %e field.
sub measure_latency
{
	my ($tool, $input) = @_;
	my $start = time();
	system("$tool < $input > /dev/null 2>/dev/null");
	my $elapsed = time() - $start;
	return ($? >> 8) == 0 ? $elapsed : undef;
}

# Peak RSS of one run, in kilobytes.
sub measure_rss
{
	my ($tool, $input) = @_;
	my $timing = `{ /usr/bin/time -f '%M' $tool < $input > /dev/null; } 2>&1`;
	chomp($timing);
	my ($rss) = $timing =~ /(\d+)\s*\z/;
	return defined($rss) ? $rss + 0 : undef;
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

sub run_arm
{
	my ($label, $tool, $input, $samples, $arm) = @_;
	my $latency = measure_latency($tool, $input);
	my $rss = measure_rss($tool, $input);
	push @{$samples->{$label}{time}}, $latency;
	push @{$samples->{$label}{rss}}, $rss;
	push @{$samples->{observations}}, [$arm, $label, $latency, $rss];
	return $latency;
}

# One ABBA block.  $a is the binary measured under the `mine` label and $b the
# one measured under the `ref` label; the A/A arm passes the reference for both,
# so the difference it reports is the harness noise floor.
my @abba = (['ref', 'r'], ['mine', 'm'], ['mine', 'm'], ['ref', 'r']);

sub run_block
{
	my ($arm, $a, $b, $input, $samples) = @_;
	my %latency;
	for my $step (@abba)
	{
		my ($label, $which) = @{$step};
		my $tool = $which eq 'm' ? $a : $b;
		$latency{$label} = run_arm($label, $tool, $input, $samples, $arm);
	}
	return ($latency{mine} // 0) - ($latency{ref} // 0);
}

my $size = generate($corpus, $lines);
print "corpus: $corpus ($size bytes, $lines generated blocks)\n";

if ((system("$reference < $corpus > /tmp/posttoken_benchmark.ref") >> 8) != 0)
{
	die "reference wrapper failed on the corpus\n";
}
if ((system("$mine < $corpus > /tmp/posttoken_benchmark.my") >> 8) != 0)
{
	die "student compiler failed on the corpus\n";
}
my $identical = system("cmp -s /tmp/posttoken_benchmark.ref /tmp/posttoken_benchmark.my") == 0;
print "output identical to reference: ", ($identical ? "yes" : "NO"), "\n";
exit(1) if !$identical;

my %ab;
my %aa;
my @ab_diff;
my @aa_diff;
for my $block (1 .. $blocks)
{
	push @ab_diff, run_block('A/B', $mine, $reference, $corpus, \%ab);
	push @aa_diff, run_block('A/A', $reference, $reference, $corpus, \%aa);
}

open(my $sf, '>', $samples_path) or die "cannot write $samples_path: $!";
print $sf join("\t", qw(arm label latency_s rss_kb)), "\n";
for my $arm (\%ab, \%aa)
{
	for my $observation (@{$arm->{observations}})
	{
		my ($a, $label, $latency, $rss) = @{$observation};
		next if !defined $latency;
		printf $sf "%s\t%s\t%.6f\t%s\n", $a, $label, $latency, $rss;
	}
}
close($sf);

sub report_arm
{
	my ($title, $samples, $diffs) = @_;
	print "$title ($blocks ABBA blocks, ", 4 * $blocks, " timed runs per label)\n";
	for my $label ('ref', 'mine')
	{
		my ($time, $time_min, $time_max) = summarize($samples->{$label}{time});
		my ($rss, $rss_min, $rss_max) = summarize($samples->{$label}{rss});
		printf "  %-5s latency %0.4f s [%0.4f..%0.4f]   peak RSS %0.1f MB [%0.1f..%0.1f]\n",
			$label, $time, $time_min, $time_max,
			$rss / 1024, $rss_min / 1024, $rss_max / 1024;
	}
	my ($median, $min, $max) = summarize($diffs);
	printf "  paired per-block difference (mine - ref) median %+0.4f s [%+0.4f..%+0.4f]\n",
		$median, $min, $max;
	return ($median, $min, $max);
}

print "\n";
my ($ab_median, $ab_min, $ab_max) = report_arm("A/B", \%ab, \@ab_diff);
print "\n";
my ($aa_median, $aa_min, $aa_max) = report_arm("A/A noise calibration", \%aa, \@aa_diff);
printf "\nnoise floor (A/A spread): %0.4f s; measured A/B difference: %+0.4f s\n",
	$aa_max - $aa_min, $ab_median;
print "all observations: $samples_path\n";
