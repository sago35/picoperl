# file I/O: 3-arg open with lexical handles, <$fh>, print/close, eof/seek/tell
use strict; use warnings;
$| = 1;

open(my $fh, '<', 'test/t_io_data.txt') or die "open: $!";
my $l1 = <$fh>;
print "1: $l1";
chomp(my $l2 = <$fh>);
print "2: [$l2]\n";
print "3: tell=", tell($fh), "\n";
my @rest = <$fh>;
print "4: ", scalar(@rest), " lines left, last=", $rest[-1];
print "5: eof=", (eof($fh) ? 1 : 0), "\n";
seek($fh, 0, 0) or die "seek: $!";
print "6: tell=", tell($fh), " eof=", (eof($fh) ? 1 : 0), "\n";
my ($n, $hits) = (0, 0);
while (<$fh>) { $n++; $hits++ if /line/ }   # <$fh> reads into $_ like perl
print "7: $n lines, $hits contain 'line'\n";
seek($fh, 11, 0);
my $mid = <$fh>;
print "8: $mid";
print "9: ", (close($fh) ? "closed" : "close failed"), "\n";

my $ok = open(my $nf, '<', 'test/no_such_file.txt');
print "10: ", (defined $ok ? "opened?!" : "failed"), " (\$!=$!) handle=", (defined $nf ? "def" : "undef"), "\n";

open(my $out, '>', 'test/io_tmp.txt') or die "write open: $!";
print $out "alpha\n";
print {$out} "beta gamma\n";
printf $out "%03d\n", 7;
say $out "delta";
print "11: ", (close($out) ? "ok" : "ng"), "\n";

open(my $in, '<', 'test/io_tmp.txt') or die $!;
my @all = <$in>;
print "12: ", scalar(@all), " lines: ", join("", @all);
close($in);

open(my $ap, '>>', 'test/io_tmp.txt') or die $!;
print $ap "tail\n";
close($ap);
open($in, '<', 'test/io_tmp.txt') or die $!;
seek($in, -5, 2) or die "seek end: $!";
my $t = <$in>;
print "13: $t";
while (my $line = <$in>) { print "14: unreachable\n" }
print "15: eof=", (eof($in) ? 1 : 0), "\n";
close($in);
