#!/usr/bin/perl
#
# Compiler benchmark for the pa5 syntax stage.
#
# Generates one fixed, reproducible template-heavy translation unit, checks that
# the student compiler and the reference wrapper agree on the AST dump byte for
# byte, and reports paired ABBA wall-time and peak-RSS blocks for both.  A second
# A/A arm measures the reference against itself, so the noise floor of the
# schedule is visible beside the effect it is used to judge.
#
# The corpus is the shape the parser is actually paid for at this stage:
# class and function templates with type, non-type and defaulted parameters,
# nested template-ids in declarations and expressions, qualified ids into a
# class member, and class bodies with inline member definitions - the constructs
# whose ambiguity resolution (template-id against relational, type-id against
# expression, declaration against expression statement) is the stage's real
# work.  Parsing is proportional to tokens, so the corpus is sized by its token
# count rather than by a claim about time.
#
# The protocol is the one the specification asks for: fixed binaries, flags and
# input; wall-time ABBA blocks; an A/A arm that calibrates the noise floor;
# paired per-block differences as well as per-label medians and spread; the
# produced text's size beside latency and peak RSS; every observation kept in a
# TSV next to the generated corpus.
#
# The noise floor is the median absolute deviation of the A/A paired
# differences - the median of |difference - median difference| - not their
# min..max range: one delayed run moves the range by hundreds of milliseconds
# and would let a single outlier either manufacture or mask a result.  The range
# is still printed so an excursion larger than the measured effect stays visible.
#
# `cppgm++ --emit-ast` has no executable output, so only compiler latency, peak
# RSS and the size of the dump are reported; there is no generated-program
# runtime at this stage, and no telemetry surface is invented to report one.
# The dump's line count is the tree's node count, which is the one size counter
# this stage's work has.
#
# This is a personal benchmark: it is not part of the course contract and is not
# discovered by `make test`.
#
#   perl student.tests/syntax_benchmark.pl [uses] [blocks]

use strict;
use warnings;
use File::Path qw(make_path);
use Time::HiRes qw(time);

my $root = $0;
$root =~ s{/student\.tests/[^/]+\z}{};
$root = '.' if $root eq $0;

my $uses = shift @ARGV // 6000;
my $blocks = shift @ARGV // 5;
my $outdir = '/tmp/pa5_syntax_benchmark';
my $corpus = "$outdir/syntax_bench.t";
my $tsv = "$outdir/syntax_benchmark.tsv";

my $mine = "$root/dev/cppgm++";
my $ref = "$root/pa5/cppgm++-ref";

make_path($outdir);

sub generate
{
	my ($path, $count) = @_;
	open(my $fh, '>', $path) or die "cannot write $path: $!";
	print $fh <<'HEAD';
namespace bench {

template <class T, int N>
struct holder {
  T value[N];
  static const int size = N;
  T& at(int index);
};

template <class T>
struct link {
  link<T>* next;
  T value;
};

template <class A, class B>
struct pair_like {
  A first;
  B second;
  pair_like() : first(), second() {}
};

template <int N>
struct tag {
  static const int value = N;
};

template <class T>
T& holder<T, 1>::at(int index) {
  return value[index];
}

template <class T, class U>
struct same {
  static const bool value = false;
};

template <class T>
struct same<T, T> {
  static const bool value = true;
};

}  // namespace bench

HEAD
	for my $i (1 .. $count)
	{
		my $n = 1 + ($i % 7);
		my $k = 1 + ($i % 5);
		print $fh "bench::holder<int, $n> h_$i;\n";
		print $fh "bench::holder<char, $k> c_$i;\n";
		print $fh "bench::link<bench::pair_like<int, char> > l_$i;\n";
		print $fh "int v_$i = h_$i.at(0) + bench::tag<$n>::value + c_$i.size;\n";
		print $fh "bool b_$i = bench::same<bench::tag<$n>, bench::tag<$n> >::value;\n";
		print $fh "void f_$i(bench::holder<int, $n>* p) { (void)p; (void)h_$i.at(0); }\n";
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
	system("$tool --emit-ast -o $output $input > /dev/null 2>&1");
	my $elapsed = time() - $start;
	return ($? >> 8) == 0 ? $elapsed : undef;
}

sub measure_rss
{
	my ($tool, $input, $output) = @_;
	my $timing = `{ /usr/bin/time -f '%M' $tool --emit-ast -o $output $input > /dev/null; } 2>&1`;
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

my $bytes = generate($corpus, $uses);
print "corpus: $corpus ($bytes bytes, $uses groups)\n";

if (!-x $mine || !-x $ref)
{
	die "missing benchmark binaries: $mine or $ref\n";
}

system("$mine --emit-ast -o $outdir/mine.txt $corpus > /dev/null 2>&1") == 0
	or die "student compiler failed on the corpus\n";
system("$ref --emit-ast -o $outdir/ref.txt $corpus > /dev/null 2>&1") == 0
	or die "reference compiler failed on the corpus\n";
system("cmp -s $outdir/mine.txt $outdir/ref.txt") == 0
	or die "student and reference dumps differ\n";
my $dump_lines = 0;
open(my $dump, '<', "$outdir/mine.txt") or die "cannot read the dump: $!";
$dump_lines++ while <$dump>;
close($dump);
printf "dump: %d bytes, %d lines (one per syntax node), byte-identical\n",
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
my ($mine_median, $mine_mad) = report_arm(
	"cppgm++ against cppgm++-ref ($blocks ABBA blocks, " . (4 * $blocks) . " timed runs per label)",
	\%mine_samples, [@mine_diffs[0 .. $blocks - 1]]);
my ($noise_median, $noise_mad) = report_arm(
	"noise floor, reference against itself ($blocks ABBA blocks)",
	\%noise_samples, \@noise_diffs);

print "\nobservations: $tsv\n";
printf "effect %+.4f s against a noise floor of %0.4f s\n",
	$mine_median, $noise_mad;
