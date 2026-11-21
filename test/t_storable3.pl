# Storable のフック (STORABLE_freeze / STORABLE_thaw / STORABLE_attach): バイト列も読み戻しも本物と同じ
use strict; use warnings;
use Storable qw(freeze nfreeze thaw dclone);
sub hx { join " ", map { sprintf "%02x", ord } split //, shift }
$Storable::canonical = 1;
# 文字列だけを返すフック
package Pt; sub new { my ($c, @xy) = @_; bless { x => $xy[0], y => $xy[1], cache => "tmp" }, $c }
sub STORABLE_freeze { my ($s, $cl) = @_; "$s->{x},$s->{y};$cl" }
sub STORABLE_thaw { my ($s, $cl, $str) = @_; my ($xy, $c) = split /;/, $str; @$s{qw(x y)} = split /,/, $xy; $s->{cl} = "$cl$c"; }
# 参照も返すフック (配列のオブジェクト)
package Bag; sub new { my $c = shift; bless [@_], $c }
sub STORABLE_freeze { my ($s, $cl) = @_; ("n=" . scalar(@$s), [@$s], { tag => "t" }) }
sub STORABLE_thaw { my ($s, $cl, $str, $list, $h) = @_; @$s = (@$list, $str, $h->{tag}); }
# 空のリストを返す (ふつうに保存される)
package Plain; sub new { bless { v => $_[1] }, $_[0] } sub STORABLE_freeze { $main::busy .= (Storable::is_storing() ? "S" : "-") . (Storable::is_retrieving() ? "R" : "-"); () }
# STORABLE_attach
package Single; my %inst; sub get { my ($c, $n) = @_; $inst{$n} ||= bless { name => $n }, $c }
sub STORABLE_freeze { $_[0]{name} } sub STORABLE_attach { my ($c, $cl, $str) = @_; $c->get($str) }
# スカラーのオブジェクト、自分自身を指す参照を返すフック
package Sc; sub new { my $v = $_[1]; bless \$v, $_[0] } sub STORABLE_freeze { "sc:${$_[0]}" } sub STORABLE_thaw { ${$_[0]} = substr($_[2], 3) }
package Node; sub new { bless { name => $_[1], kids => [] }, $_[0] }
sub STORABLE_freeze { my $s = shift; ($s->{name}, $s->{kids}, $s->{up} ? ($s->{up}) : ()) }
sub STORABLE_thaw { my ($s, $cl, $n, $kids, $up) = @_; $s->{name} = $n; $s->{kids} = $kids; $s->{up} = $up if $up; }
package main;
my $p = Pt->new(3, 4);
my @c = ($p, [$p, $p, Pt->new(5, 6)], Bag->new(1, 2, 3), [Bag->new("a"), Bag->new("b")], Plain->new(9), { a => Sc->new("val"), b => Pt->new(1, 2) }, [Single->get("one")]);
print "freeze  $_: ", hx(freeze($c[$_])), "\n" for 0 .. $#c;
print "nfreeze $_: ", hx(nfreeze($c[$_])), "\n" for 0 .. $#c;
my $t = thaw(freeze($p)); print "pt: ", ref($t), " $t->{x} $t->{y} cl=$t->{cl} cache=", (exists $t->{cache} ? "kept" : "gone"), "\n";
$t = thaw(nfreeze($c[1])); print "shared: ", ($t->[0] == $t->[1] ? 1 : 0), " $t->[2]{x}\n";
$t = thaw(freeze($c[2])); print "bag: ", ref($t), " @$t\n";
$t = thaw(freeze($c[3])); print "bags: @{$t->[0]} | @{$t->[1]}\n";
$t = thaw(freeze($c[4])); print "plain: ", ref($t), " $t->{v}\n";
$t = thaw(freeze($c[5])); print "sc: ", ref($t->{a}), " ${$t->{a}} $t->{b}{y}\n";
$t = thaw(freeze($c[6])); print "attach: ", ($t->[0] == Single->get("one") ? "same" : "other"), "\n";
my $d = dclone($p); print "dclone: $d->{x} cl=$d->{cl}\n";
my $root = Node->new("root"); my $kid = Node->new("kid"); $kid->{up} = $root; push @{$root->{kids}}, $kid;
print "tree: ", hx(nfreeze($root)), "\n";
$t = thaw(freeze($root)); print "tree back: $t->{name} $t->{kids}[0]{name} up=", ($t->{kids}[0]{up} == $t ? "root" : "other"), "\n";
{ package Dies; sub STORABLE_freeze { die "hook failed\n" } } print "dies: ", (eval { freeze([bless {}, "Dies"]); 1 } ? "ok" : $@ =~ /^hook failed/ ? "died in the hook" : $@), "\n";
{ package BadRef; sub STORABLE_freeze { ("s", "notref") } } print "bad: ", (eval { freeze(bless {}, "BadRef"); 1 } ? "ok" : $@ =~ /Item #1 returned by STORABLE_freeze for BadRef is not a reference/ ? "refused" : $@), "\n";
print "busy: $main::busy\n";
