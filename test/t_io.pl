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
# ファイルテスト
my $ft_file = 'test/t_io_data.txt';
my $ft_none = 'test/no_such_file.txt';
sub ft { defined $_[0] ? ($_[0] eq '' ? "''" : $_[0] > 1 ? "size" : $_[0]) : "undef" }
print "filetest file: ", join(" ", map { ft($_) } -e $ft_file, -f $ft_file, -d $ft_file, -s $ft_file, -z $ft_file, -r $ft_file, -l $ft_file), "\n";
print "filetest dir: ", join(" ", map { ft($_) } -e "test", -f "test", -d "test", -d 'test/'), "\n";
print "filetest none: ", join(" ", map { ft($_) } -e $ft_none, -f $ft_none, -d $ft_none, -s $ft_none), "\n";
print "filetest size: ", ((-s $ft_file) == length(do { open(my $f, '<', $ft_file) or die; local $/; my $c = join("", <$f>); $c }) ? "match" : "differ"), "\n";
print "filetest expr: ", (-e $ft_file ? "yes" : "no"), " ", (-e $ft_none ? "yes" : "no"), " ", (-f $ft_file && -s $ft_file > 0 ? "nonempty" : "empty"), " ", ((not -f $ft_none or -d $ft_none) ? "ok" : "ng"), " ", (-d "te" . "st" ? "concat" : "no"), "\n";
my %ft_h = (-s => 1, -e => 2, -x => 3); my $ft_n = 5; my $ft_e = 2;
print "filetest not: $ft_h{-s} $ft_h{-e} $ft_h{-x} ", $ft_n -$ft_e, " ", $ft_n - $ft_e, " ", -$ft_e, "\n";
unless (-e $ft_none) { print "filetest unless\n" }
# print STDERR (LIST) / print STDOUT (LIST)
print STDOUT ("paren ", "list", "\n");
print STDOUT (join("-", 1, 2), "\n");
printf STDOUT ("%s-%d\n", "fmt", 7);
print STDERR ("to stderr ", 1 + 1, "\n");
