use strict; use warnings;
# "ab"x3 (x と回数がくっついた形)、定数のあとの /、/a などの修飾子、-M -A -C、-f $0
my $x = "z"; my %h = (x1 => 1, x => 2);
my $y = $x x2; my @l = (1) x3;
print "a"x3, "-", "ab" x2, "-", "@l", "-$y-", $h{x1}, $h{x}, "\n";
sub x5 { "sub" } print x5(), " ", "q" x 2, " ", "w"x(1+1), "\n";
use constant pi => 4; sub half () { pi/2 } sub two () { 2 }
print half, " ", pi/two, " ", two / two, " ", join("|", split /,/, "a,b"), " ", pi /2, "\n";
(my $s = "a\x01b") =~ s/([[:^print:]])/sprintf("&#x%x;", ord($1))/aeg;
print "$s ", ("Ab" =~ /ab/ia ? 1 : 0), ("x" =~ /x/u ? 1 : 0), ("x" =~ /x/l ? 1 : 0), ("x" =~ /x/p ? 1 : 0), "\n";
my @f = sort { -M $a <=> -M $b } ($0, $0);
print scalar(@f), " ", (-M $0 >= 0 && -M $0 < 1 ? "fresh" : "old"), " ", (-A $0 < 1 ? "a" : "A"), " ", (-C $0 < 1 ? "c" : "C"), " ",
      (defined(-M "/no/such/file") ? "def" : "undef"), " ", ($^T > 1000000 ? "T" : "noT"), " ", (-f $0 && -r $0 ? "f" : "nof"), "\n";
stat($0); print -M _ < 1 ? "underscore\n" : "no\n";
# モジュールから取り込んだ空プロトタイプのサブも引数を取らない (tau + 1、tau/2)
use lib 'test/lib'; use TestProtoMod;
sub half_tau () { tau/2 } sub inv_tau () { 360/tau }
print tau + 1, " ", tau/2, " ", one/2, " ", half_tau, " ", inv_tau, " ", tau * 2, " ", (tau - 1), "\n";
