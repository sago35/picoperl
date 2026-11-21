use strict; use warnings;
# continue ブロック: while / until / foreach / 素のブロック。next のあとにも実行され、last では実行されない
my $i = 0; my @o;
while ($i < 5) { next if $i == 2; push @o, $i } continue { $i++ } print "while: @o\n";
@o = (); foreach my $x (1 .. 5) { next if $x == 2; last if $x == 5; push @o, $x } continue { push @o, "c$x" } print "foreach: @o\n";
@o = (); for (1 .. 3) { push @o, $_ } continue { push @o, "-" } print "for \$_: @o\n";
@o = (); $i = 0; until ($i >= 3) { push @o, $i } continue { $i++ } print "until: @o\n";
@o = (); { push @o, "b"; } continue { push @o, "cb" } print "block: @o\n";
@o = (); { push @o, "b"; last; } continue { push @o, "not run" } print "block last: @o\n";
@o = (); my @list = (1, 2, 3); for my $v (@list) { $v *= 2 } continue { push @o, "after" } print "alias: @list @o\n";
@o = (); OUTER: for my $a1 (1 .. 3) { for my $b1 (1 .. 3) { next OUTER if $b1 == 2; push @o, "$a1$b1" } continue { push @o, "in" } } continue { push @o, "out$a1" } print "nested: @o\n";
@o = (); my $n = 0; for my $x (1 .. 4) { push @o, $x } continue { last if ++$n == 2 } print "last in continue: @o\n";
sub f { my @r; for my $x (1 .. 3) { push @r, $x } continue { return "returned at $x" if $x == 2 } "end" } print f(), "\n";
# sort $サブ のあと、リストが次の行から始まってもよい
{ my %hh = (b => 1, a => 2, c => 3); my $rev = sub { $b cmp $a };
  my @j = map  { "$_=$hh{$_}" }
          sort $rev
          keys %hh;
  my @k = sort $rev
          @j;
  my $n = 5; my @s = sort $rev ("x", "y"); my $z = $n
    + 1;
  print "sort over lines: @j | @k | @s | $z\n"; }
