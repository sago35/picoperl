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
