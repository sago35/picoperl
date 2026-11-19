use strict; use warnings;
$| = 1;
my $u;
my $n = $u + 1;
print "n=$n\n";
my $s = "abc" + 1;
print "s=$s\n";
my $t = "5 apples" * 2;
print "t=$t\n";
print "u=$u\n";
print "join: ", join(",", 1, $u, 3), "\n";
my %h = (1, 2, 3);
print scalar(keys %h), "\n";
my $r = { a => 1, 'b' };
print scalar(keys %$r), "\n";
my $c = $u . "x";
print "c=$c\n";
my $eq = ($u == 0) ? "zero" : "nonzero";
print "$eq\n";
my $f = sprintf("%d-%s", $u, "x");
print "$f\n";
{
    no warnings;
    my $q = $u + 1;
    print "quiet q=$q\n";
}
my $ok = "0 but true" + 5;
print "ok=$ok\n";
my $z; $z .= "a"; $z += 1 if 0; print "z=$z\n";
my $w = "12" + 3; print "w=$w\n";
print "done\n";
# 比較や照合の「偽」は、警告なしで数値の 0 になる。書いた "" は警告が出る
{
    my $false = (1 == 2); my $true = (1 == 1);
    my $n = $false + 5; my $m = ("a" =~ /b/) + 1; my $not = !1;
    print "false in arithmetic: $n $m ", $not + 0, " [$false][$true] ", length($false), " ", ($false eq "" ? "empty" : "other"), "\n";
    my @hits = map { $_ == 2 } 1 .. 3; my $sum = 0; $sum += $_ for @hits;
    my %h = (k => (3 < 2)); my $copy = $false; $copy .= "x";
    print "false copies: $sum ", $h{k} + 0, " $copy [$false]\n";
    my $empty = ""; my $w = $empty + 1; print "written empty string: $w\n";
}
