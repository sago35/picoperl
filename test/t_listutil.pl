# lib/List/Util.pm と lib/Scalar/Util.pm (picoperl 版) を本家と突き合わせる
use strict;
use warnings;
use List::Util qw(sum sum0 product min max minstr maxstr first any all none notall reduce reductions
                  shuffle uniq uniqnum uniqint uniqstr pairs unpairs pairkeys pairvalues pairmap pairgrep pairfirst
                  head tail zip mesh);
use Scalar::Util qw(blessed reftype refaddr looks_like_number weaken readonly);

sub show { join(",", map { defined $_ ? (ref $_ eq 'ARRAY' ? "[" . show(@$_) . "]" : $_) : "undef" } @_) }

my @n = (3, 1, 4, 1, 5, 9, 2, 6);
print "sum: ", sum(@n), " ", show(sum()), " ", sum0(), " ", sum(1.5, 2), "\n";
print "product: ", product(@n), " ", product(), "\n";
print "min/max: ", min(@n), " ", max(@n), " ", show(min()), " ", min(-1, -5), " ", max(2.5, 2), "\n";
print "minstr/maxstr: ", minstr(qw(pear apple fig)), " ", maxstr(qw(pear apple fig)), "\n";

print "first: ", first { $_ > 3 } @n; print " ", show(first { $_ > 100 } @n), " ", show(first { 1 } ()), "\n";
print "any/all: ", (any { $_ == 9 } @n) ? "y" : "n", (any { $_ == 7 } @n) ? "y" : "n", (all { $_ > 0 } @n) ? "y" : "n", (all { $_ > 1 } @n) ? "y" : "n", "\n";
print "none/notall: ", (none { $_ > 9 } @n) ? "y" : "n", (none { $_ > 8 } @n) ? "y" : "n", (notall { $_ > 1 } @n) ? "y" : "n", (notall { $_ > 0 } @n) ? "y" : "n", "\n";
print "empty lists: ", (any { 1 } ()) ? "y" : "n", (all { 0 } ()) ? "y" : "n", (none { 1 } ()) ? "y" : "n", (notall { 1 } ()) ? "y" : "n", "\n";
my $limit = 4; my @big = grep { defined } map { first { $_ > $limit } $_, $_ + 3 } 1, 2;
print "first in map, closure: @big\n";

print "reduce: ", (reduce { $a + $b } @n), " ", (reduce { $a * $b } 1 .. 5), " ", (reduce { $a . $b } qw(a b c)), " ", show(reduce { $a + $b } ()), " ", (reduce { $a + $b } 7), "\n";
print "reduce max: ", (reduce { $a > $b ? $a : $b } @n), "\n";
print "reductions: ", show(reductions { $a + $b } 1 .. 5), "\n";
{ package Other; use List::Util qw(reduce first); sub total { reduce { $a + $b } @_ } sub pick { first { $_ eq "b" } @_ } }
print "other package: ", Other::total(1, 2, 3), " ", Other::pick(qw(a b c)), "\n";
print "qualified: ", List::Util::sum(1, 2), " ", List::Util::max(4, 8), " ", (List::Util::first { $_ } 0, 5), "\n";

my @sh = shuffle(1 .. 20);
print "shuffle: ", scalar(@sh), " ", join(",", sort { $a <=> $b } @sh) eq join(",", 1 .. 20) ? "same elements" : "lost", "\n";
print "uniq: ", show(uniq(qw(a b a c b), undef, undef)), " ", scalar(uniq(1, 1, 2)), "\n";
print "uniqnum/int/str: ", show(uniqnum(1, 1.0, "1", 2, 3.0, 3)), " ", show(uniqint(1.5, 1, 2.9, 3)), " ", show(uniqstr("a", "a", "b")), "\n";

my @p = pairs(a => 1, b => 2);
print "pairs: ", show(map { [@$_] } @p), " ", ("$p[0]" =~ /^List::Util::_Pair=ARRAY\(0x[0-9a-f]+\)$/ ? "stringified" : "odd: $p[0]"), " ", $p[1]->key, "=", $p[1]->value, " ", ref($p[0]), "\n";
print "unpairs/keys/values: ", show(unpairs([x => 1], [y => 2])), " ", show(pairkeys(a => 1, b => 2)), " ", show(pairvalues(a => 1, b => 2)), "\n";
print "pairmap: ", show(pairmap { "$a-$b" } a => 1, b => 2), " ", show(pairgrep { $b > 1 } a => 1, b => 2, c => 3), " ", show(pairfirst { $b == 2 } a => 1, b => 2), "\n";
print "head/tail: ", show(head(2, @n)), " ", show(head(-2, @n)), " ", show(tail(2, @n)), " ", show(tail(-6, @n)), " ", show(head(99, 1, 2)), " ", show(tail(0, 1, 2)), "\n";
print "zip/mesh: ", show(zip([1, 2, 3], ["a", "b"])), " ", show(mesh([1, 2], ["a", "b"])), "\n";

# Scalar::Util
{ package SuObj; sub new { bless {}, shift } }
my $o = SuObj->new;
print "blessed: ", show(blessed($o), blessed({}), blessed("str"), blessed(undef)), "\n";
print "reftype: ", show(reftype($o), reftype([]), reftype(\1), reftype(sub {}), reftype("x")), "\n";
print "refaddr: ", (refaddr($o) == refaddr($o) ? "same" : "differ"), " ", (refaddr([]) != refaddr($o) ? "distinct" : "equal"), " ", show(refaddr("x")), "\n";
print "looks_like_number: ", join("", map { looks_like_number($_) ? "y" : "n" } 1, "2.5", "1e3", " 7", "abc", "", "0x10", "-3", ".5", undef), "\n";
my $strong = [1]; my $w = $strong; weaken($w); print "weaken: ", show(@$w), " ", (readonly($w) ? "ro" : "rw"), "\n";

# ブロックを取るユーザー定義のサブ
sub apply(&@) { my $f = shift; map { $f->($_) } @_ }
sub times_do(&$) { my ($f, $c) = @_; join "", map { $f->($_) } 1 .. $c }
sub wrap(&) { "<" . $_[0]->() . ">" }
sub takes_hash { ref $_[0] }
print "block subs: ", show(apply { $_[0] * 2 } 1, 2, 3), " ", (times_do { "x$_[0]" } 3), " ", (wrap { "in" }), " ", wrap(sub { "ref" }), " ", takes_hash({ a => 1 }), "\n";
# mro
use mro;
{ package MA; our @ISA = (); package MB; our @ISA = ('MA'); package MC; our @ISA = ('MA'); package MD; our @ISA = ('MB', 'MC'); }
print "mro: ", join(" ", @{ mro::get_linear_isa('MD') }), " / ", join(" ", @{ mro::get_linear_isa('MA') }), " / ", mro::get_mro('MD'), " ", (mro::is_universal('UNIVERSAL') ? "u" : "-"), (mro::is_universal('MA') ? "u" : "-"), "\n";
print "done\n";
