#!/usr/bin/perl
#
# Compiler benchmark for the pa3 ppexpr stage.
#
# Generates a fixed, reproducible file of controlling expressions - one per
# logical line, which is the assignment's unit of work - checks that the student
# compiler and the reference wrapper agree on its output byte for byte, then
# reports paired ABBA wall-time and peak-RSS blocks for both.
#
# The protocol is the one the specification asks for: fixed binaries, flags and
# input; wall-time ABBA blocks; an A/A arm that measures the reference against
# itself to calibrate the noise floor; paired per-block differences as well as
# per-label medians and spread; every observation kept in a TSV next to the
# generated corpus.  PA3 has no executable output, so only compiler latency and
# peak RSS are reported - there is no generated-program runtime or text size at
# this stage, and no telemetry surface is invented to report them.
#
# The noise floor is the median absolute deviation of the A/A paired
# differences, not their min..max range: one delayed run moves the range by
# hundreds of milliseconds and would let a single outlier either manufacture or
# mask a result.  The range is still printed, so an excursion larger than the
# measured effect is visible rather than averaged away.
#
# This is a personal benchmark: it is not part of the course contract and is not
# discovered by `make test`.
#
#   perl student.tests/ppexpr_benchmark.pl [blocks] [abba-blocks]

use strict;
use warnings;

use Cwd qw(abs_path);
use File::Basename qw(dirname);
use Time::HiRes qw(time);

my $lines = shift // 40000;
my $blocks = shift // 5;

my $root = dirname(dirname(abs_path($0)));
my $mine = "$root/dev/ppexpr";
my $reference = "$root/reference-binaries/ppexpr";
my $corpus = "/tmp/ppexpr_benchmark.txt";
my $samples_path = "/tmp/ppexpr_benchmark.samples.tsv";

for my $tool ($mine, $reference)
{
	die "$tool is missing; build it first\n" if !-x $tool;
}

# The expression shapes a real `#if` carries: operator precedence chains, the
# usual arithmetic conversions between signed and unsigned operands, `defined`,
# the conditional and its short-circuit, character literals of every prefix, and
# a few lines the course definition rejects.  Deterministic, so the corpus is
# the same bytes on every run.
sub generate
{
	my ($path, $count) = @_;
	open(my $fh, '>', $path) or die "cannot write $path: $!";
	binmode($fh);
	for my $i (1 .. $count)
	{
		print $fh "($i * 3 + 5) % 7 << 2 < 1000 ? $i : -$i\n";
		print $fh "defined f$i && !defined g$i || ${i}u == 1\n" if $i % 2 == 0;
		print $fh "~($i) & 0xff | $i << 3 ^ $i\n";
		print $fh "'a' + $i > 200 ? (defined a ? 1 : 2) : 3u\n" if $i % 3 == 0;
		print $fh "$i && ($i / 3) || ($i % 5)\n";
		print $fh "1 + 2 * 3 - 4 / 2 % 5 << 1 >> 1 < 2 == 1 & 3 ^ 4 | 5 && 6 || 7 ? 8 : 9\n";
		print $fh "-2147483647 - 1 + $i < 0 ? u'a' : U'\\U0010ffff'\n" if $i % 4 == 0;
		print $fh "L'\\u03d0' * 2 - $i > 0 && 0x7fffffffffffffff % ($i + 1)\n" if $i % 5 == 0;
		print $fh "0xffffffffffffffffu / ($i | 1) + $i % 7\n" if $i % 6 == 0;
		print $fh "5 / 0 + $i\n" if $i % 50 == 0;
		print $fh "1 << 64 + $i\n" if $i % 50 == 25;
		print $fh "true ? $i : ($i * 9223372036854775807)\n" if $i % 11 == 0;
		print $fh "\n" if $i % 13 == 0;
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

# A robust spread for a paired-difference sample.  One delayed run - a
# descheduled child, a page-cache miss on the corpus, another process on the
# machine - moves min..max by hundreds of milliseconds while leaving the body of
# the sample alone, so the noise floor is taken from the median absolute
# deviation.  The extremes are still reported, because a single excursion past
# the measured effect is exactly what an A/A arm exists to expose.
sub robust_spread
{
	my ($values) = @_;
	my @sorted = sort { $a <=> $b } map { abs($_) } @{$values};
	my $count = scalar(@sorted);
	my $mad = $count % 2 ? $sorted[($count - 1) / 2]
		: ($sorted[$count / 2 - 1] + $sorted[$count / 2]) / 2;
	my ($median, $min, $max) = summarize($values);
	my $negative = scalar(grep { $_ < 0 } @{$values});
	return ($median, $min, $max, $mad, $negative, $count);
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

if ((system("$reference < $corpus > /tmp/ppexpr_benchmark.ref") >> 8) != 0)
{
	die "reference wrapper failed on the corpus\n";
}
if ((system("$mine < $corpus > /tmp/ppexpr_benchmark.my") >> 8) != 0)
{
	die "student compiler failed on the corpus\n";
}
my $identical = system("cmp -s /tmp/ppexpr_benchmark.ref /tmp/ppexpr_benchmark.my") == 0;
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
	my ($title, $samples, $diffs, $label_name) = @_;
	print "$title ($blocks ABBA blocks, ", 4 * $blocks, " timed runs per label)\n";
	for my $label ('ref', 'mine')
	{
		my ($time, $time_min, $time_max) = summarize($samples->{$label}{time});
		my ($rss, $rss_min, $rss_max) = summarize($samples->{$label}{rss});
		printf "  %-5s latency %0.4f s [%0.4f..%0.4f]   peak RSS %0.1f MB [%0.1f..%0.1f]\n",
			$label, $time, $time_min, $time_max,
			$rss / 1024, $rss_min / 1024, $rss_max / 1024;
	}
	my ($median, $min, $max, $mad, $negative, $count) = robust_spread($diffs);
	printf "  paired per-block difference (%s) median %+0.4f s [%+0.4f..%+0.4f]\n",
		$label_name, $median, $min, $max;
	printf "  median absolute difference %0.4f s; %d of %d blocks negative\n",
		$mad, $negative, $count;
	return ($median, $min, $max, $mad, $negative, $count);
}

print "\n";
my ($ab_median, $ab_min, $ab_max, $ab_mad, $ab_negative, $ab_count) =
	report_arm("A/B", \%ab, \@ab_diff, "mine - ref");
print "\n";
my ($aa_median, $aa_min, $aa_max, $aa_mad, $aa_negative, $aa_count) =
	report_arm("A/A noise calibration", \%aa, \@aa_diff, "ref - ref");
my $aa_worst = abs($aa_min) > abs($aa_max) ? abs($aa_min) : abs($aa_max);
printf "\nnoise floor: A/A median absolute difference %0.4f s, worst observed excursion %0.4f s\n",
	$aa_mad, $aa_worst;
printf "measured A/B difference: %+0.4f s with %d of %d blocks negative\n",
	$ab_median, $ab_negative, $ab_count;
# The direct question: did the reference differ from itself, under the same
# ABBA schedule, by as much as the effect the A/B arm claims?  A noise sample
# that reaches the effect means the effect is not separable from the schedule.
my $aa_reaching = scalar(grep { abs($_) >= abs($ab_median) } @aa_diff);
printf "A/A blocks whose |difference| reached the measured A/B effect: %d of %d\n",
	$aa_reaching, $aa_count;
if (abs($ab_median) > $aa_worst && $ab_negative == $ab_count && $aa_reaching == 0 && $ab_count > 1)
{
	print "verdict: every A/B block moved the same way, by more than the largest\n";
	print "         excursion the reference produced against itself.\n";
}
else
{
	print "verdict: the A/B difference is not separable from the schedule noise;\n";
	print "         treat it as unresolved.\n";
}
print "all observations: $samples_path\n";
