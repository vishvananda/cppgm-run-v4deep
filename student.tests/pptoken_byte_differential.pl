#!/usr/bin/perl
#
# Byte-level differential check for the pa1 frontend.
#
# The atom-based harness (pptoken_differential.pl) composes inputs from a
# hand-written token alphabet, so it only ever feeds well-formed UTF-8.  This
# harness drives the frontend with arbitrary byte sequences - including bytes
# that cannot begin a UTF-8 sequence - and compares stdout and exit status with
# the reference wrapper.
#
#   perl student.tests/pptoken_byte_differential.pl [iterations] [seed]
#
# This is a personal test: it is not part of the course contract and is not
# discovered by `make test`.

use strict;
use warnings;

use Cwd qw(abs_path);
use File::Basename qw(dirname);
use POSIX qw(_exit);

my $iterations = shift // 4000;
my $seed = shift // 1;

my $root = dirname(dirname(abs_path($0)));
my $mine = "$root/dev/pptoken";
my $reference = "$root/reference-binaries/pptoken";

for my $tool ($mine, $reference)
{
	die "$tool is missing; build it first\n" if !-x $tool;
}

# The corpus is the whole byte range.  Bytes that cannot begin a UTF-8 sequence
# are deliberately over-represented: they are where two readings of the
# physical-to-source-character mapping disagree.
my @atoms = (
	('a', 'b', '_z', '"', "'", '\\', '/', '*', '<', '>', '(', ')', '#', '.',
	 '0', 'e', 'E', 'p', 'R', 'u', 'U', 'L', ' ', "\t", "\n", "\r"),
	(map { chr($_) } (0x80 .. 0xBF)),
	(map { chr($_) } (0xC0, 0xC2, 0xDF, 0xE0, 0xE2, 0xEF, 0xF0, 0xF4, 0xF5, 0xF8, 0xFF)),
	("\xC2\xA0", "\xCF\x80", "\xF0\x9D\x84\x9E", "\xEF\xBB\xBF"),
);

sub make_input
{
	my $length = 1 + int(rand(24));
	my $text = '';
	for (my $index = 0; $index < $length; ++$index)
	{
		$text .= $atoms[int(rand(scalar(@atoms)))];
	}
	$text .= "\n" if rand() < 0.7;
	return $text;
}

sub write_file
{
	my ($path, $data) = @_;
	open(my $fh, '>', $path) or die "cannot write $path: $!";
	binmode($fh);
	print $fh $data;
	close($fh);
}

sub read_file
{
	my ($path) = @_;
	open(my $fh, '<', $path) or die "cannot read $path: $!";
	binmode($fh);
	local $/;
	my $data = <$fh>;
	close($fh);
	return defined($data) ? $data : '';
}

# One child per case; the parent collects only the divergences, so the child's
# exit code is the report.
sub compare_case
{
	my ($index, $input) = @_;
	my $tag = "/tmp/pptoken_byte_differential.$$.$index";
	my $in = "$tag.in";
	write_file($in, $input);
	my $mine_out = "$tag.mine";
	my $ref_out = "$tag.ref";
	my $mine_exit = system("$mine < $in > $mine_out 2>/dev/null") >> 8;
	my $ref_exit = system("$reference < $in > $ref_out 2>/dev/null") >> 8;
	my $diverge = 0;
	my $reason = '';
	if ($mine_exit != $ref_exit)
	{
		$diverge = 1;
		$reason = "exit status mine=$mine_exit ref=$ref_exit";
	}
	elsif ($ref_exit == 0 && read_file($mine_out) ne read_file($ref_out))
	{
		$diverge = 1;
		$reason = "stdout differs";
	}
	unlink($in, $mine_out, $ref_out);
	return ($diverge, $reason);
}

srand($seed);
my @inputs = map { make_input() } (1 .. $iterations);

my $jobs = $ENV{PPTOKEN_BYTE_JOBS} || 8;
my $next = 0;
my %reports;
my $divergences = 0;

while ($next < scalar(@inputs) || keys(%reports))
{
	while (keys(%reports) < $jobs && $next < scalar(@inputs))
	{
		my $index = $next++;
		my $pid = fork();
		die "fork failed: $!" if !defined $pid;
		if ($pid == 0)
		{
			my ($diverge, $reason) = compare_case($index, $inputs[$index]);
			_exit($diverge ? 1 : 0);
		}
		$reports{$pid} = $index;
	}
	my $pid = wait();
	last if $pid < 0;
	if (exists $reports{$pid})
	{
		my $index = delete $reports{$pid};
		if ($? >> 8)
		{
			++$divergences;
			if ($divergences <= 10)
			{
				my (undef, $reason) = compare_case($index, $inputs[$index]);
				print "DIVERGENCE on case $index ($reason)\n";
				print "  input: ", unpack('H*', $inputs[$index]), "\n";
			}
		}
	}
}

print $divergences == 0
	? "byte differential passed: $iterations inputs\n"
	: "byte differential failed: $divergences divergences\n";
exit($divergences == 0 ? 0 : 1);