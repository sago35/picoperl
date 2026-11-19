# ブロックの中で定義した名前付きサブから、ファイル直下の my 変数を使う
use strict;
my $w = "file"; my @log;
{ my %c = (k => 1); sub a1 { "a1:$w" } push @log, a1(); }
for my $i (1 .. 2) { my $t = $i; sub a2 { my $x = shift; { my $y = "$w$x"; return sub { "$y!" } } } push @log, a2($t)->(); }
{ my $n = 5; sub a3 { ++$n . $w } push @log, a3(), a3(); }
sub outer { my $v = shift; my $in = sub { "$v$w" }; { my $z = 1; return $in->() . $z } } push @log, outer("o");
if (1) { my $q = 2; { my $r = 3; sub a4 { join "", map { "$_$w" } @_ } push @log, a4($q, $r); } }
print "$_\n" for @log; print a1(), "\n";

# プロトタイプの $ は引数をスカラーコンテキストにする
sub is2 ($$;$) { my ($got, $exp, $name) = @_; print "is2: ", (defined $got ? $got : "undef"), " / ", (defined $exp ? $exp : "undef"), " / ", (defined $name ? $name : "-"), " (", scalar(@_), ")\n" }
sub ok2 ($;$) { print "ok2: ", ($_[0] ? "true" : "false"), " n=", scalar(@_), " name=", (defined $_[1] ? $_[1] : "-"), "\n" }
sub lst (@) { print "lst: ", scalar(@_), "\n" }
sub mix ($@) { print "mix: $_[0] rest=", scalar(@_) - 1, "\n" }
sub ctx ($) { print "ctx: $_[0]\n" } sub want { wantarray ? "list" : "scalar" }
my %h = (squaa => 1, blorm => 2, vliff => 3); my @a = (5, 6, 7);
is2 grep(/a/, keys %h), 2;
is2 grep(/zzz/, keys %h), 0, "none";
is2(@a, 3, "count");
ok2 "abc" =~ /x/, "no match";
ok2("abc" =~ /(b)/, "match");
lst @a, @a; mix @a, @a; ctx want(); ctx(want());
my $s = "v"; is2 $s, $a[0]; is2 $h{blorm}, "x" . "y";
