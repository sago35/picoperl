use strict; use warnings;
use Scalar::Util qw(set_prototype);
# prototype: サブのプロトタイプ文字列を返す
sub p_none { 1 }
sub p_two($$) { "$_[0]$_[1]" }
sub p_opt($;$@) { 1 }
sub p_blk(&@) { 1 }
sub p_empty() { 42 }
sub p_ref(\@\%) { 1 }
sub p_sp( $ $ ; $ ) { 1 }
sub p_attr :prototype($;$) { 1 }
sub show { my $p = shift; defined $p ? "[$p]" : "undef" }
print join(" ", map { show(prototype($_)) } \&p_none, \&p_two, \&p_opt, \&p_blk, \&p_empty, \&p_ref, \&p_sp, \&p_attr), "\n";
print show(prototype("p_two")), " ", show(prototype("main::p_opt")), " ", show(prototype('&p_blk')), " ", show(prototype \&p_two), "\n";
my $anon = sub ($$) { 1 }; my $plain = sub { 1 }; print show(prototype $anon), " ", show(prototype $plain), "\n";
{ package Other; sub o_one($) { 1 } sub chk { main::show(prototype("o_one")) . main::show(prototype("Other::o_one")) } }
print Other::chk(), " ", show(prototype("Other::o_one")), "\n";
# set_prototype: そのコード参照だけに付く
my $w1 = set_prototype(sub { "w1" }, '$$'); my $w2 = set_prototype(sub { "w2" }, prototype \&p_blk);
print show(prototype $w1), " ", show(prototype $w2), " ", $w1->(), $w2->(), "\n";
my @made = map { my $n = $_; set_prototype(sub { $n }, '$' x $n) } 1 .. 3; print join(" ", map { show(prototype $_) . $_->() } @made), "\n";
set_prototype(\&p_none, '@'); print show(prototype \&p_none), " ", show(prototype "p_none"), "\n";
&set_prototype($w1, undef); print show(prototype $w1), "\n";
print "has block proto: ", (grep { my $p = prototype(main->can($_)); $p && $p =~ /&/ } qw(p_two p_blk p_none)), "\n";
# 組み込み関数のプロトタイプ
for (qw(open close push shift time print system sort mkdir each binmode)) { my $p = prototype("CORE::$_"); print "$_=", defined $p ? "[$p]" : "undef", " " } print "\n";
