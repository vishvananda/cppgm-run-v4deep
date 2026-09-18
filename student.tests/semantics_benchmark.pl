#!/usr/bin/perl
#
# Compiler benchmark for the pa7 resolved-semantics stage.
#
# Generates one fixed, reproducible call-and-expression-heavy translation unit,
# checks that the student compiler and the reference wrapper agree on the
# semantics dump byte for byte, and reports paired ABBA wall-time and peak-RSS
# blocks for both.  A second A/A arm measures the reference against itself, so
# the noise floor of the schedule is visible beside the effect it is used to
# judge.
#
# The corpus is the shape this stage is actually paid for: overload sets the
# call layer ranks, conversions of every supported kind, pointer and reference
# parameters, local declarations and control flow, and qualified and
# unqualified calls into nested namespaces.  The work tracks the expressions
# the walk resolves and the candidates each call ranks, so the corpus is sized
# by group count and the dump's line count is reported as the size counter the
# work produces.
#
# The protocol is the one the specification asks for: fixed binaries, flags and
# input; wall-time ABBA blocks; an A/A arm that calibrates the noise floor;
# paired per-block differences as well as per-label medians and spread; the
# produced text's size beside latency and peak RSS; every observation kept in a
# TSV next to the generated corpus.
#
# The noise floor is the median absolute deviation of the A/A paired
# differences, not their min..max range: one delayed run moves the range by
# hundreds of milliseconds and would let a single outlier either manufacture or
# mask a result.  The range is still printed so an excursion larger than the
# measured effect stays visible.
#
# `cppgm++ --emit-semantics` has no executable output, so only compiler
# latency, peak RSS and the size of the dump are reported; there is no
# generated-program runtime or text size at this stage, and no telemetry
# surface is invented to report one.
#
# This is a personal benchmark: it is not part of the course contract and is not
# discovered by `make test`.
#
#   perl student.tests/semantics_benchmark.pl [groups] [blocks]

use strict;
use warnings;
use File::Path qw(make_path);
use Time::HiRes qw(time);

my $root = $0;
$root =~ s{/student\.tests/[^/]+\z}{};
$root = '.' if $root eq $0;

my $groups = shift @ARGV // 3000;
my $blocks = shift @ARGV // 5;
my $outdir = '/tmp/pa7_semantics_benchmark';
my $corpus = "$outdir/semantics_bench.t";
my $tsv = "$outdir/semantics_benchmark.tsv";

my $mine = "$root/dev/cppgm++";
my $ref = "$root/pa7/cppgm++-ref";

make_path($outdir);

sub generate
{
	my ($path, $count) = @_;
	open(my $fh, '>', $path) or die "cannot write $path: $!";
	print $fh <<'HEAD';
namespace bench {

typedef unsigned long size_t;

struct point { int x; int y; };

int take(int value) { return value; }
long take(long value) { return value; }
int take(const void* value) { return value != 0; }
int take(point* value) { return value->x; }

namespace inner {
  int scale(int value) { return value * 2; }
  long scale(long value) { return value * 3; }
  namespace deeper {
    int offset(const char* text) { return text[0]; }
  }
}

int classify(int value) {
  int total = 0;
  for (int index = 0; index < 4; index = index + 1) {
    if (value > index) total += value;
    else total -= index;
  }
  return total;
}

}

HEAD
	for my $i (1 .. $count)
	{
		my $n = 1 + ($i % 7);
		print $fh "int g_$i(int a, long b, const char* c) {\n";
		print $fh "  bench::point p;\n";
		print $fh "  p.x = a; p.y = static_cast<int>(b);\n";
		print $fh "  int local = bench::take(a);\n";
		print $fh "  local = local + bench::take(b);\n";
		print $fh "  local += bench::inner::scale(a);\n";
		print $fh "  local -= bench::inner::deeper::offset(c);\n";
		print $fh "  bool flag = c != 0 && local > a;\n";
		print $fh "  int chosen = flag ? local : a;\n";
		print $fh "  while (chosen > $n) chosen = chosen - 1;\n";
		print $fh "  int (&ref)(int) = bench::take;\n";
		print $fh "  return ref(chosen) + p.x + p.y + local;\n";
		print $fh "}\n";
	}
	close($fh);
	return -s $path;
}

# Wall time for one run, timed around the child at high resolution so a
# sub-second run is not quantised away.
sub measure_latency
{
	my ($tool, $input, $output) = @_;
	my $start = time();
	system("$tool --emit-semantics -o $output $input > /dev/null 2>&1");
	my $elapsed = time() - $start;
	return ($? >> 8) == 0 ? $elapsed : undef;
}

sub measure_rss
{
	my ($tool, $input, $output) = @_;
	my $timing = `{ /usr/bin/time -f '%M' $tool --emit-semantics -o $output $input > /dev/null; } 2>&1`;
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
	return ($median, $sorted[0], $sorted[-1]);
}

sub robust_spread
{
	my ($values) = @_;
	my ($median, $min, $max) = summarize($values);
	my @sorted = sort { $a <=> $b } map { abs($_ - $median) } @{$values};
	my $count = scalar(@sorted);
	my $mad = $count % 2 ? $sorted[($count - 1) / 2]
		: ($sorted[$count / 2 - 1] + $sorted[$count / 2]) / 2;
	my $negative = scalar(grep { $_ < 0 } @{$values});
	return ($median, $min, $max, $mad, $negative, $count);
}

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
	my ($title, $samples, $diffs) = @_;
	print "$title\n";
	for my $label ('ref', 'mine')
	{
		my ($t, $tmin, $tmax) = summarize($samples->{$label}{time});
		my ($r, $rmin, $rmax) = summarize($samples->{$label}{rss});
		printf "  %-5s latency %0.3f s [%0.3f..%0.3f]   peak RSS %0.1f MB [%0.1f..%0.1f]\n",
			$label, $t, $tmin, $tmax, $r / 1024, $rmin / 1024, $rmax / 1024;
	}
	my ($median, $min, $max, $mad, $negative, $count) = robust_spread($diffs);
	printf "  paired difference (mine - ref): median %+.4f s, %d of %d negative," .
		" MAD %0.4f s, range [%+.4f..%+.4f]\n",
		$median, $negative, $count, $mad, $min, $max;
	return ($median, $mad);
}

my $bytes = generate($corpus, $groups);
print "corpus: $corpus ($bytes bytes, $groups groups)\n";

if (!-x $mine || !-x $ref)
{
	die "missing benchmark binaries: $mine or $ref\n";
}

system("$mine --emit-semantics -o $outdir/mine.txt $corpus > /dev/null 2>&1") == 0
	or die "student compiler failed on the corpus\n";
system("$ref --emit-semantics -o $outdir/ref.txt $corpus > /dev/null 2>&1") == 0
	or die "reference compiler failed on the corpus\n";
system("cmp -s $outdir/mine.txt $outdir/ref.txt") == 0
	or die "student and reference dumps differ\n";
my $dump_lines = 0;
open(my $dump, '<', "$outdir/mine.txt") or die "cannot read the dump: $!";
$dump_lines++ while <$dump>;
close($dump);
printf "dump: %d bytes, %d lines (one per resolved node), byte-identical\n",
	-s "$outdir/mine.txt", $dump_lines;

open(my $log, '>', $tsv) or die "cannot write $tsv: $!";
print $log "arm\tlabel\tlatency_s\tpeak_rss_kb\n";

my %mine_samples = (time => [], rss => [], observations => []);
my %noise_samples = (time => [], rss => [], observations => []);
my @mine_diffs;
for my $block (1 .. $blocks)
{
	push @mine_diffs, run_block('mine', $mine, $ref, $corpus, $outdir, \%mine_samples);
}
my @noise_diffs;
for my $block (1 .. $blocks)
{
	push @noise_diffs, run_block('noise', $ref, $ref, $corpus, $outdir, \%noise_samples);
}
for my $row (@{$mine_samples{observations}}, @{$noise_samples{observations}})
{
	printf $log "%s\t%s\t%s\t%s\n", @{$row};
}
close($log);

print "\n";
my ($effect, $effect_mad) = report_arm('student against the reference', \%mine_samples, \@mine_diffs);
print "\n";
my ($noise, $noise_mad) = report_arm('reference against itself (A/A noise floor)', \%noise_samples, \@noise_diffs);
printf "\neffect %+.4f s against a noise floor of %0.4f s (A/A MAD); every observation in %s\n",
	$effect, $noise_mad, $tsv;
