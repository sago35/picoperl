use strict; use warnings;
# プロトタイプ: ($) のサブは単項演算子 (f $x, $y は f($x), $y)、引数の数が合わない呼び出しはコンパイルできない
sub one($) { "one($_[0])" } sub opt1(;$) { "opt(@_)" } sub us(_) { "us(@_)" } sub lst { "lst(@_)" } sub two($$) { scalar @_ }
sub opt2($;$) { scalar @_ } sub rest($@) { scalar @_ } sub c0() { 5 }
$_ = "d";
my @r = (one 1, 2); print scalar(@r), " @r\n";
print join("|", (one "a", "b"), (opt1 5, 6), (us 7, 8), (lst 9, 10), (one 1 + 2, 3), (one 2 * 3 + 1), (opt1), us), "\n";
print "cmp: ", (one 5 eq "one(5)" ? "y" : "n"), " ", (one(5) eq "one(5)" ? "y" : "n"), " ", opt2(1), opt2(1, 2), rest(1), rest(1, 2, 3), two(1, 2), c0, "\n";
for my $c ('two(1)', 'two(1, 2, 3)', 'rest()', 'c0(1)', 'us(1, 2)', 'one()', 'opt2(1, 2, 3)', 'my @a = (1, 2); two(@a)', 'two(1, 2)', 'rest(1)', '&two(1)', 'main->can("two")->(1)', 'opt1()') {
    my $ok = eval "$c; 1";
    print "$c: ", ($ok ? "ok" : $@ =~ /^((?:Not enough|Too many) arguments for [\w:]+)/ ? $1 : "other: $@"), "\n";
}
use List::Util qw(head tail);
my @h = head -1, (4, 5, 6); my @t = tail 2, (4, 5, 6); print "@h|@t\n";
print eval('my @x = head; 1') ? "compiled\n" : ($@ =~ /^(Not enough arguments for List::Util::head)/ ? "$1\n" : $@);
