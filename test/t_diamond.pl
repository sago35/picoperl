use strict; use warnings;
# <> (@ARGV のファイルを順に読む)、<Pkg::FH>、ブロックのあとの <$fh>
my @files = ("test/io_tmp_dia_a.txt", "test/io_tmp_dia_b.txt");
open(my $w, '>', $files[0]) or die; print $w "a1\na2\n"; close $w;
open($w, '>', $files[1]) or die; print $w "b1\n"; close $w;
@ARGV = @files;
while (<>) { chomp; print "[$ARGV] $_\n" }
print "left: ", scalar(@ARGV), "\n";
@ARGV = @files; my @all = <>; print "list: ", scalar(@all), " $all[2]";
@ARGV = @files; my $first = <>; my @rest = <>; print "first: $first", "rest: ", scalar(@rest), "\n";
@ARGV = ($files[1]); print "mod: $_" while <>;
@ARGV = ($files[0]); print "paren: $_" while (<>);
@ARGV = ($files[1], $files[0]); my $n = 0; while (my $l = <>) { $n++ } print "count: $n\n";
open(my $in, '<', $files[0]) or die;
my @m = map  { uc }
        grep { /2/ }
        <$in>;
print "after block: @m";
open($in, '<', $files[0]) or die; print {*STDOUT} <$in>;
{ package DiaPkg; print "pkg data: $_" while <DiaPkg::DATA>; }
my ($x, $y) = (5, 3); my %h = (k => 2);
print "still a comparison\n" if $h{k} <$x and $y> 1;
unlink @files;
package DiaPkg;
__DATA__
d1
d2
