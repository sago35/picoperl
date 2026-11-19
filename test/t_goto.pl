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
print "done\n";
