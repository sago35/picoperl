# goto LABEL
use strict;
use warnings;

# 前方へ
print "forward: ";
goto skip;
print "not printed ";
skip:
print "after skip\n";

# 後方へ (回数を数えるループ)
my $n = 0;
again:
$n++;
goto again if $n < 3;
print "backward: $n\n";

# サブの中: 入れ子のブロックやループから、外側の文の並びにあるラベルへ
sub classify {
    my ($v) = @_;
    my @log;
    if ($v < 0) { push @log, "neg"; goto done; }
    for my $i (1 .. 5) {
        if ($i == $v) { push @log, "found $i"; goto done; }
        while (1) { goto too_big if $v > 100; last }
    }
    push @log, "not found";
    goto done;
  too_big:
    push @log, "too big";
  done:
    push @log, "end";
    return join(",", @log);
}
print "sub: ", join(" / ", map { classify($_) } -1, 3, 9, 500), "\n";

# 状態機械
sub machine {
    my @out;
    my $i = 0;
  state_a:
    push @out, "a$i";
    goto state_b if ++$i < 3;
    goto finish;
  state_b:
    push @out, "b$i";
    goto state_a;
  finish:
    return "@out";
}
print "machine: ", machine(), "\n";

# ループに付けたラベルは last / next と goto の両方で使える
my @seen;
my $pass = 0;
OUTER: for my $x (1 .. 3) {
    for my $y (1 .. 3) {
        next OUTER if $y == 2;
        push @seen, "$x$y";
    }
}
$pass++;
goto OUTER if $pass < 2;
print "loop label: @seen\n";

# 小文字のラベル、文のないラベル、ラベルを付けたブロック
sub tail_label { my $r = "start"; goto end_of_sub; $r = "skipped"; end_of_sub: return $r }
print "lowercase label: ", tail_label(), "\n";
sub empty_label { my $r = 1; goto the_end if $r; $r = 2; the_end: ; return $r }
print "empty statement: ", empty_label(), "\n";
my $blocks = "";
goto second;
first: { $blocks .= "first "; }
second: { $blocks .= "second"; }
print "labelled blocks: $blocks\n";

# 見つからないラベル
eval { goto no_such_label_here; };
print "missing label: ", ($@ =~ /^Can't find label no_such_label_here at / ? "dies" : "no: $@"), "\n";
sub inner_goto { goto outer_label }
eval { inner_goto(); outer_label: 1 };
print "label in the caller: ", ($@ =~ /^Can't find label outer_label/ ? "dies" : "no error"), "\n";

# eval STRING の中
print "in eval string: ", eval('my $k = 0; top: $k++; goto top if $k < 4; $k'), "\n";

# 三項演算子やハッシュのコロンはラベルではない
my %h = (a => 1); my $t = $n ? "yes" : "no";
print "not labels: $t $h{a} ", ($n > 1 ? "big" : "small"), "\n";
# last / next で存在しないラベルを指定すると die する (eval で受けられる)
no warnings;    # perl は "Exiting eval via last" などの警告を出す
sub lb_term { eval { last NO_SUCH_LOOP_LABEL; }; (my $e = $@) =~ s/ at .*//s; $e }
print "missing loop label: [", lb_term(), "]\n";
eval { next ALSO_MISSING }; print "missing next label: ", ($@ =~ /^Label not found for "next ALSO_MISSING"/ ? "dies" : "no: $@"), "\n";
my @lb; LB_OUT: for my $i (1 .. 3) { for my $j (1 .. 3) { next LB_OUT if $j == 2; push @lb, "$i$j" } }
sub lb_inner { no warnings; last LB_CALLER } my $lb_n = 0; LB_CALLER: for (1 .. 3) { $lb_n++; lb_inner(); $lb_n += 10 }
print "existing labels still work: @lb / $lb_n\n";
print "done\n";

# goto &sub: 移った先のサブから見た caller は、元のサブの呼び出し元
{
    package GotoLib;
    sub real { my @c = caller; my @c0 = caller(0); my @c1 = caller(1); "pkg=$c[0] line=$c[2] sub=$c0[3] up=" . (defined $c1[3] ? $c1[3] : "none") . " args=@_" }
    sub front { unshift @_, "added"; goto &real }
    sub viaref { my $code = \&real; goto &$code }
    sub chain { goto &front }
    sub normal { real(@_) }
    package main;
    sub wrap { GotoLib::front("x") }
    print GotoLib::front("a", "b"), "\n";
    print GotoLib::viaref("c"), "\n";
    print GotoLib::chain("d"), "\n";
    print wrap(), "\n";
    print GotoLib::normal("e"), "\n";
    print GotoLib::front("after"), "\n";
}
