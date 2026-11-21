use v5.36;
# 属性とシグネチャの併用 (sub f :prototype(_) ($arg))、_ プロトタイプの既定値 ($_)
sub f :prototype(_) ($arg) { "got $arg" }
$_ = "dflt"; say f("x"), " ", f, " ", f(), " ", prototype(\&f); for ("loop") { say f }
sub g :prototype($$) ($a, $b) { "$a+$b" } say g 1, 2; my @l = (7, 8); say g(@l, 5);
sub h :lvalue ($x) { $x } say h(3);
sub m2 :method ($self, $v = 2) { $v } say m2(1);
my $c = sub :prototype($) ($v) { $v * 2 }; say $c->(4);
sub blk :prototype(&@) ($code, @rest) { join ",", map { $code->($_) } @rest } say blk { $_[0] + 1 } 1, 2;
sub z :prototype() { 5 } say z + 1;
{ no feature 'signatures'; no warnings;
  sub two($_) { "@_" } sub one(_) { "[@_]" } sub k :prototype($;$) { "@_" } }
$_ = "u"; say two("a"), "|", two("a", "b"), "|", one, one(), one("z"), "|", k(1), "|", prototype("k"), prototype("one");
my @m = map { one } qw(p q); say "@m";
# ほかのパッケージからの組み込みは、main が同じ名前のサブを取り込んだあとも組み込みのまま
package AsOther { sub size_of { my @s = stat($_[0]); scalar @s } sub core_size { my @s = CORE::stat($_[0]); scalar @s } }
BEGIN { no strict 'refs'; no warnings; *{"main::stat"} = sub { "wrapped" }; }
say AsOther::size_of($0) > 5 ? "builtin" : "wrapped", " ", AsOther::core_size($0) > 5 ? "builtin" : "wrapped";
