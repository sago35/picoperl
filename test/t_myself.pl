use strict; use warnings;
# my $x = EXPR の EXPR からは、宣言したばかりの $x ではなく外側の $x が見える
my $x = 5; { my $x = $x + 1; print "block: $x\n"; } print "outer: $x\n";
sub dbl { my $v = shift; if ($v) { my $v = $v * 2; return $v } 0 } print "sub: ", dbl(4), "\n";
my ($p, $q) = (1, 2); { my ($p, $q) = ($q, $p); print "swap: $p $q\n"; }
my @a = (1, 2); { my @a = (@a, 3); print "array: @a\n"; }
my %h = (k => 1); { my %h = (%h, j => 2); print "hash: ", join(",", sort keys %h), "\n"; }
our $g = "glob"; { my $g = $g . "!"; print "global: $g\n"; }
{ my $x = my $z = $x + 5; print "chain: $x $z\n"; }
my $r = do { my $x = $x + 1; my $m = $x + 1; $m }; print "do: $r\n";
{ my $obj = { value => [3, 4] }; my $got; if (1) { my $obj = [@{ $obj->{value} }]; $got = "@$obj"; } print "copy of a field: $got\n"; }
{ my $x = eval q{ $x * 10 }; print "eval string: $x\n"; }
my $c = 0; while ((my $c2 = $c++) < 2) { print "cond: $c2\n"; } if ((my $v = 3) > 2) { print "if: $v\n" }
print "closure names itself: ", (eval q{ my $rec = sub { $rec }; 1 } ? "compiled" : $@ =~ /Global symbol "\$rec"/ ? "not visible" : $@), "\n";
{ my $rec; $rec = sub { $_[0] ? $_[0] + $rec->($_[0] - 1) : 0 }; print "declared first: ", $rec->(4), "\n"; undef $rec; }
# foreach: ループの中で元の配列に直接書いたものは残る
{ my $v = [3, 4]; my $w = $v; my $j = 0; for my $i (@$w) { $v->[$j] += $i; $j++ } print "same array: @$v\n"; }
{ my @n = (1, 2, 3); for my $i (@n) { $n[2] = 9 if $i == 1; $i *= 2 } print "direct then alias: @n\n"; }
{ my @n = (1, 2); for (@n) { $n[1] = 7 if $_ == 1; } print "later element: @n\n"; }
{ my @n = (1, 2, 3); for (@n) { $_ *= 2 } for my $e (@n) { $e .= "x" } s/x/y/ for @n; print "alias writes: @n\n"; }
# *AUTOLOAD = \&Other::AUTOLOAD: $AUTOLOAD は、サブが書かれたパッケージ (Other) のものに入る
{ package AlMid; our $AUTOLOAD; sub new { bless {}, shift } sub DESTROY {} sub AUTOLOAD { "auto[" . (defined $AUTOLOAD ? $AUTOLOAD : "undef") . "]" }
  package AlKid; our @ISA = ("AlMid"); BEGIN { *AUTOLOAD = \&AlMid::AUTOLOAD; *is_neg = \&is_negative; }
  package main; my $k = AlKid->new; no warnings 'once';
  print "aliased AUTOLOAD: ", $k->is_neg, " | ", AlKid::plainfn(1), " | ", $k->meth, " | ", (defined $AlKid::AUTOLOAD ? "kid set" : "kid unset"), "\n"; }
# f($v = EXPR): サブが @_ 経由で書き換えると、代入先の変数が変わる (元の値は変わらない)
{ sub esc { foreach my $e (@_) { $e =~ s/"/&quot;/g } return } my %src = (k => q{a"b}); my $v; esc($v = $src{k}); print "assign as arg: $v $src{k}\n";
  esc(my $w = q{c"d}); my @arr; esc($arr[1] = q{"}); my %g; esc($g{z} = q{""}, my $t = q{"x}); print "more: $w $arr[1] $g{z} $t\n";
  sub up { $_[0] = uc $_[0]; $_[1] .= "!" } my ($p, $q) = ("a", "b"); up($p .= "x", $q = "new"); print "op assign: $p $q\n"; }
# use base で自分自身を継承しようとすると、警告だけ出て @ISA は変わらない
{ my $w = ""; local $SIG{__WARN__} = sub { $w = shift }; eval q{package SelfBase; use base "SelfBase";}; no warnings 'once'; print "self base: [$w] isa=[", join(",", @SelfBase::ISA), "] err=[$@]\n"; }
