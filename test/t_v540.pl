use v5.40;
# use v5.40: builtin の関数 (blessed、trim、true など) が素の名前で使える。有効範囲はブロック単位
my $o = bless [], "K";
print blessed($o), " ", (blessed([]) // "undef"), " ", reftype(bless {}, "K"), " ", (refaddr([]) ? "addr" : "no"), "\n";
print ceil(1.2), floor(-1.2), " [", trim("  a b  "), "] ", (true ? "t" : "f"), (false ? "t" : "f"), " ", true + 1, " [", false, "] ", true(), "\n";
my @a = qw(a b); for my ($i, $v) (indexed @a) { print "$i=$v " } print is_tainted("x") ? "T" : "nt", "\n";
my %h = (true => 1, trim => 2, blessed => 3); print $h{true}, $h{trim}, $h{blessed}, "\n";
{ package V540Other; sub t { blessed(bless [], "W") . trim(" x ") } } print V540Other::t(), "\n";
print builtin::trim(" direct "), builtin::ceil(0.5), (builtin::true() ? "T" : "F"), "\n";
my $r = [1]; my $w = $r; weaken($w); print "weaken: $$w[0]\n";
