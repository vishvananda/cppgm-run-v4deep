#!/usr/bin/perl
#
# Compiler benchmark for the pa4 preproc stage.
#
# Generates two fixed, reproducible translation units and checks that the
# student compiler and the reference wrapper agree on the dump byte for byte,
# then reports paired ABBA wall-time and peak-RSS blocks for both.
#
# The two shapes are the two the preprocessor is actually paid for.  The first
# is a translation unit: a guarded header that defines a few thousand object-
# like, function-like, paste and stringize macros, followed by a great many use
# sites, so the run is dominated by reading tokens and expanding them once.  The
# second is macro-expansion-shaped: sites whose every token goes through a
# helper chain, a paste and a stringize, which is what makes the recursion and
# nesting machinery's cost visible rather than amortised away.
#
# The protocol is the one the specification asks for: fixed binaries, flags and
# input; wall-time ABBA blocks; an A/A arm that measures the reference against
# itself to calibrate the noise floor; paired per-block differences as well as
# per-label medians and spread; the produced text's size beside latency and peak
# RSS; every observation kept in a TSV next to the generated corpora.
#
# The noise floor is the median absolute deviation of the A/A paired
# differences, not their min..max range: one delayed run moves the range by
# hundreds of milliseconds and would let a single outlier either manufacture or
# mask a result.  The range is still printed, so an excursion larger than the
# measured effect is visible rather than averaged away.
#
# `preproc` has no executable output, so only compiler latency, peak RSS and the
# size of the dump are reported - there is no generated-program runtime at this
# stage, and no telemetry surface is invented to report one.
#
# This is a personal benchmark: it is not part of the course contract and is not
# discovered by `make test`.
#
#   perl student.tests/preproc_benchmark.pl [sites] [blocks]

use strict;
use warnings;

use Cwd qw(abs_path);
use File::Basename qw(dirname);
use Time::HiRes qw(time);

my $sites = shift // 40000;
my $blocks = shift // 5;

my $root = dirname(dirname(abs_path($0)));
my $mine = "$root/dev/preproc";
my $reference = "$root/reference-binaries/preproc";

for my $tool ($mine, $reference)
{
	die "$tool is missing; build it first\n" if !-x $tool;
}

# A guarded header of every macro shape the assignment has, then use sites that
# exercise all of them.  Deterministic, so the corpus is the same bytes on every
# run.
sub generate_translation_unit
{
	my ($path, $count) = @_;
	open(my $fh, '>', $path) or die "cannot write $path: $!";
	print $fh "#ifndef BENCH_TU_H\n#define BENCH_TU_H\n";
	for my $i (1 .. 8000) { print $fh "#define OBJ_$i ((long)($i) + 1)\n"; }
	for my $i (1 .. 2000) { print $fh "#define FN_$i(a, b) ((a) * OBJ_$i + (b))\n"; }
	for my $i (1 .. 1000) { print $fh "#define PA_$i(a, b) a ## b\n"; }
	for my $i (1 .. 1000) { print $fh "#define ST_$i(x) #x\n"; }
	print $fh "#endif\n";
	for my $i (1 .. $count)
	{
		my $m = 1 + ($i % 2000);
		print $fh "long v_$i = FN_$m(OBJ_$m, PA_$m(u, $i)) + ST_$m(let us count $i) [0];\n";
	}
	close($fh);
	return -s $path;
}

# Every token of every site goes through a helper chain, a paste and a
# stringize, which is where the nesting rule's bookkeeping is spent.
sub generate_expansion_chain
{
	my ($path, $count, $chain) = @_;
	open(my $fh, '>', $path) or die "cannot write $path: $!";
	print $fh "#define CAT(a,b) a ## b\n#define STR(x) #x\n#define H0(x) (x)\n";
	for my $i (1 .. $chain) { print $fh "#define H$i(x) H" . ($i - 1) . "(x)\n"; }
	for my $i (1 .. $count)
	{
		print $fh "long CAT(v_,$i) = H$chain($i) + CAT(u_,$i);\n";
		print $fh "const char* CAT(s_,$i) = STR(a b $i);\n";
	}
	close($fh);
	return -s $path;
}

# Wall time for one run, timed around the child directly so the reported
# latency is not quantised by the 10 ms ceiling of `/usr/bin/time`'s %e field.
sub measure_latency
{
	my ($tool, $input, $output) = @_;
	my $start = time();
	system("$tool -o $output $input > /dev/null 2>&1");
	my $elapsed = time() - $start;
	return ($? >> 8) == 0 ? $elapsed : undef;
}

# Peak RSS of one run, in kilobytes.
sub measure_rss
{
	my ($tool, $input, $output) = @_;
	my $timing = `{ /usr/bin/time -f '%M' $tool -o $output $input > /dev/null; } 2>&1`;
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

# One ABBA block.  `a` is the binary measured under the `mine` label and `b` the
# one measured under the `ref` label; the A/A arm passes the reference for both,
# so the difference it reports is the harness noise floor.
my @abba = (['ref', 'r'], ['mine', 'm'], ['mine', 'm'], ['ref', 'r']);

sub run_block
{
	my ($arm, $a, $b, $input, $outdir, $samples) = @_;
	my %latency;
	for my $step (@abba)
	{
		my ($label, $which) = @{$step};
		my $tool = $which eq 'm' ? $a : $b;
		my $output = "$outdir/$arm-$label.txt";
		my $latency = measure_latency($tool, $input, $output);
		my $rss = measure_rss($tool, $input, $output);
		push @{$samples->{$label}{time}}, $latency;
		push @{$samples->{$label}{rss}}, $rss;
		push @{$samples->{observations}}, [$arm, $label, $latency, $rss];
		$latency{$label} = $latency;
	}
	return ($latency{mine} // 0) - ($latency{ref} // 0);
}

sub report_arm
{
	my ($blocks, $title, $samples, $diffs) = @_;
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
	printf "  paired per-block difference (mine - ref) median %+0.4f s [%+0.4f..%+0.4f]\n",
		$median, $min, $max;
	printf "  median absolute difference %0.4f s; %d of %d blocks negative\n",
		$mad, $negative, $count;
	return ($median, $min, $max, $mad, $negative, $count);
}

# The whole protocol for one corpus: agreement first, then the two arms.
sub benchmark
{
	my ($name, $corpus, $samples_path, $outdir) = @_;
	my $size = -s $corpus;
	print "\n===== $name: $corpus ($size bytes) =====\n";

	my $ref_dump = "$outdir/ref-dump.txt";
	my $my_dump = "$outdir/my-dump.txt";
	if ((system("$reference -o $ref_dump $corpus > /dev/null 2>&1") >> 8) != 0)
	{
		die "reference wrapper failed on $name\n";
	}
	if ((system("$mine -o $my_dump $corpus > /dev/null 2>&1") >> 8) != 0)
	{
		die "student compiler failed on $name\n";
	}
	my $identical = system("cmp -s $ref_dump $my_dump") == 0;
	my $dump_size = -s $ref_dump;
	printf "dump: %d bytes; identical to reference: %s\n", $dump_size,
		($identical ? "yes" : "NO");
	exit(1) if !$identical;

	my %ab;
	my %aa;
	my @ab_diff;
	my @aa_diff;
	for my $block (1 .. $blocks)
	{
		push @ab_diff, run_block('AB', $mine, $reference, $corpus, $outdir, \%ab);
		push @aa_diff, run_block('AA', $reference, $reference, $corpus, $outdir, \%aa);
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

	my ($ab_median, $ab_min, $ab_max, $ab_mad, $ab_negative, $ab_count) =
		report_arm($blocks, 'A/B', \%ab, \@ab_diff);
	print "\n";
	my ($aa_median, $aa_min, $aa_max, $aa_mad, $aa_negative, $aa_count) =
		report_arm($blocks, 'A/A noise calibration', \%aa, \@aa_diff);
	my $aa_worst = abs($aa_min) > abs($aa_max) ? abs($aa_min) : abs($aa_max);
	printf "\nnoise floor: A/A median absolute difference %0.4f s, worst observed excursion %0.4f s\n",
		$aa_mad, $aa_worst;
	printf "measured A/B difference: %+0.4f s with %d of %d blocks negative\n",
		$ab_median, $ab_negative, $ab_count;
	# The direct question: did the reference differ from itself, under the same
	# ABBA schedule, by as much as the effect the A/B arm claims?  A noise
	# sample that reaches the effect means the effect is not separable from the
	# schedule.
	my $aa_reaching = scalar(grep { abs($_) >= abs($ab_median) } @aa_diff);
	printf "A/A blocks whose |difference| reached the measured A/B effect: %d of %d\n",
		$aa_reaching, $aa_count;
	if (abs($ab_median) > $aa_worst && $ab_negative == $ab_count && $aa_reaching == 0 &&
	    $ab_count > 1)
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
}

my $tu = "/tmp/preproc_benchmark_tu.cpp";
my $chain = "/tmp/preproc_benchmark_chain.cpp";
my $outdir = "/tmp/preproc_benchmark_out";
mkdir($outdir) if !-d $outdir;

generate_translation_unit($tu, $sites);
generate_expansion_chain($chain, $sites, 64);

benchmark("translation unit", $tu, "/tmp/preproc_benchmark_tu.samples.tsv", $outdir);
benchmark("expansion chain", $chain, "/tmp/preproc_benchmark_chain.samples.tsv", $outdir);
