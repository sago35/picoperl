use strict; no warnings;
my @a = (1..5);
for my $x (@a) { print $x; } print "\n";
foreach (@a) { print $_ * 2, " "; } print "\n";
for (my $i = 0; $i < 3; $i++) { print "i$i"; } print "\n";
my $j = 0; while ($j < 3) { print "w$j"; $j++; } print "\n";
$j = 0; until ($j >= 3) { print "u$j"; $j++; } print "\n";
$j = 0; do { print "d$j"; $j++ } while ($j < 3); print "\n";
$j = 0; do { print "e$j"; $j++ } until $j >= 2; print "\n";
for my $x (1..10) { next if $x % 2; last if $x > 8; print $x; } print "\n";
my $k = 0; $k++ while $k < 5; print "$k\n";
print "if\n" if $k == 5; print "unless\n" unless $k != 5;
if ($k == 1) { print "one\n" } elsif ($k == 5) { print "five\n" } else { print "other\n" }
unless ($k == 1) { print "not one\n" } else { print "one\n" }
my @stack; push @stack, $_ for 1..3; print "@stack\n";
{ print "bare block\n"; }
OUTER: for my $x (1..3) { for my $y (1..3) { next OUTER if $y == 2; print "$x$y "; } } print "\n";
my $t = 1 ? "a" : "b"; print "$t\n";
print(($k > 3 and $k < 10) ? "range\n" : "out\n");
my $r = 0 || "default"; print "$r\n";
$r = 0 // "def"; print "$r\n";
$r = undef // "def"; print "$r\n";
my $x = 5; $x > 3 and print "and\n"; $x < 3 or print "or\n";
my @e = (); print "empty\n" unless @e; print "count=", scalar(@e), "\n";
for my $i (reverse 1..3) { print $i } print "\n";
my $cnt = 0; for (1..3) { for (1..3) { $cnt++ } } print "$cnt\n";
my @aa = (1,2,3); $_ *= 10 for @aa; print "@aa\n";
my $w = 0; while (1) { last if ++$w > 3 } print "$w\n";
my ($q, $rr) = (10, 20); ($q, $rr) = ($rr, $q); print "$q $rr\n";
my $lastval = do { 1; 2; 3 }; print "$lastval\n";

# redo
{
# redo: 条件を調べ直さずに本体をやり直す
my @out; my $tries = 0;
for my $i (1 .. 3) { $tries++; if ($i == 2 && $tries < 4) { push @out, "retry$i"; redo; } push @out, "i$i"; }
print "@out ($tries)\n";
my $n = 0; my $c = 0;
while ($n < 3) { $n++; $c++; redo if $n == 2 && $c < 5 && $n--  > 0; }
print "while n=$n c=$c\n";
my @in = ("a\\", "b", "c\\", "d\\", "e", "f"); my @lines;
while (defined(my $l = shift @in)) { if ($l =~ s/\\$//) { $l .= shift @in; unshift @in, $l; next } push @lines, $l }
print "@lines\n";
@in = ("a\\", "b", "c\\", "d\\", "e", "f"); @lines = ();
for (my $l = shift @in; defined $l; $l = shift @in) { if ($l =~ s/\\$//) { $l .= shift @in; redo } push @lines, $l }
print "@lines\n";
# ラベル付き
my $k = 0; my @t;
OUTER: for my $x (1, 2) { for my $y (1, 2) { $k++; push @t, "$x$y"; redo OUTER if $k == 2; } }
print "@t\n";
# 素のブロック
{ my $z = 0; { $z++; redo if $z < 3; } print "bare $z\n"; }
# 後置
my $m = 0; my @u; do { $m++; push @u, $_ } for 1 .. 2; print "@u $m\n";
my $q = 0; foreach (1 .. 2) { $q++; $_ == 1 && $q < 3 and redo; } print "q=$q\n";
{ no warnings "exiting"; eval { for (1) { redo NOPE } } }; print "err: $@";
}

# foreach の変数を書き換えると、元の変数に反映される (スカラー変数、要素、配列の混在)
{
my $s = " <a b> "; for ($s) { s/[<>]//g; s/^\s+//; s/\s+$//; } print "[$s]\n";
my ($p, $q) = ("a", "b"); for ($p, $q) { $_ .= "!" } print "$p $q\n";
my %h = (k => "v"); my @arr = (1, 2); for ($h{k}, $arr[1], $p) { $_ .= "+" } print "$h{k} @arr $p\n";
for my $e ($p, @arr, $q) { $e x= 2 } print "$p @arr $q\n";
}
