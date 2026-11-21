# クロージャの解放の実機確認
#
# シリアルで Enter を送るたびに、クロージャを作るパターンを 300 回ずつ回して、
# 結果 (ok / NG) と空きメモリを表示します。ドライブには何も書きません。
#
# 見るところ:
#   - 各行が ok であること
#   - Enter を何度送っても "heap free" の before の値が変わらないこと
#     (after は run() の中で測るので、まだ生きている変数のぶん数百バイト少なく出ます。
#      実機では before が 131080 で一定、after との差は 400 バイト前後でした)
# 直す前のファームウェアでは、1 回の Enter で 10 万バイト以上減ってメモリ不足になります。
#
# 構文エラーからの復帰も確かめられます: main.pl の適当な行を壊して (括弧を 1 つ消すなど) 保存すると
# エラーが表示され、直して保存し直すと最初の行 "closure test: ... heap free N" がまた出ます。
# 壊す、直すを繰り返しても、この N が減っていかないことを見てください。
use strict;
use warnings;

my ($ok, $ng);
sub check {
    my ($name, $got, $want) = @_;
    if ($got eq $want) { $ok++; print "ok   $name\n" }
    else { $ng++; print "NG   $name: got [$got], want [$want]\n" }
}

package Probe;      # 生きているオブジェクトの数を数える
my $live = 0;
sub new { $live++; bless { n => $_[1] }, $_[0] }
sub DESTROY { $live-- }
sub live { $live }

package main;

sub local_cb { my $n = shift; my $cb = sub { $n + 1 }; $cb->() }           # 変数に入れて呼ぶだけ
sub returned { my $n = shift; my $c = sub { $n * 2 }; return $c }          # 変数に入れてから返す
sub counter  { my $n = shift; my %h = (inc => sub { ++$n }, get => sub { $n }); return \%h }
sub pair     { my $n = shift; my $inc = sub { ++$n }; my $get = sub { $n }; return ($inc, $get) }
sub nested   { my $n = shift; return sub { my $k = shift; my $in = sub { $n + $k }; $in->() } }
sub recur    { my $n = shift; my $f; $f = sub { my $k = shift; $k ? $k + $f->($k - 1) : 0 }; $f->($n) }
sub holder   { my $p = Probe->new(shift); my $cb = sub { $p->{n} }; $cb->() }
sub kept     { my $p = Probe->new(shift); my $c = sub { $p->{n} }; return $c }

{ my $calls = 0; sub count_call { ++$calls } }                             # ブロックの変数を使う名前付きサブ

sub heap { defined &Board::_heap_free ? Board::_heap_free() : 0 }

sub run {
    ($ok, $ng) = (0, 0);
    my $before = heap();
    my $n = 300;
    my $t = 0;
    $t += local_cb($_) for 1 .. $n;
    check("local closure", $t, $n * ($n + 1) / 2 + $n);
    $t = 0; for my $i (1 .. $n) { my $f = returned($i); $t += $f->(); }
    check("returned closure", $t, $n * ($n + 1));
    $t = 0; for my $i (1 .. $n) { my $o = counter($i); $o->{inc}->(); $t += $o->{get}->(); }
    check("closures in a hash", $t, $n * ($n + 1) / 2 + $n);
    $t = 0; for my $i (1 .. $n) { my ($inc, $get) = pair($i); $inc->(); $inc->(); $t += $get->(); }
    check("two closures, one variable", $t, $n * ($n + 1) / 2 + 2 * $n);
    $t = 0; for my $i (1 .. $n) { my $f = nested($i); $t += $f->(1); }
    check("nested closure", $t, $n * ($n + 1) / 2 + $n);
    $t = 0; $t += recur(5) for 1 .. $n;
    check("recursive closure", $t, 15 * $n);
    $t = 0; for my $i (1 .. $n) { my $cb = sub { $i * 3 }; $t += $cb->(); }
    check("closure in a loop body", $t, 3 * $n * ($n + 1) / 2);
    $t = 0; $t += holder($_) for 1 .. $n;
    check("object freed with its closure", "$t live=" . Probe::live(), $n * ($n + 1) / 2 . " live=0");
    my @keep = map { kept($_) } 1 .. 5;
    check("kept closures stay alive", join(",", map { $_->() } @keep) . " live=" . Probe::live(), "1,2,3,4,5 live=5");
    @keep = ();
    check("... and go when dropped", "live=" . Probe::live(), "live=0");
    my $c0 = count_call(); count_call() for 1 .. 4;
    check("named sub in a block", count_call() - $c0, 5);
    my $after = heap();
    print "$ok ok, $ng NG\n";
    print "heap free: $before before, $after after (", $before - $after, " bytes lost)\n" if $before;
    print "press Enter to run again\n";
}

print "closure test: press Enter (heap free ", heap(), ")\n";
while (defined(my $l = <STDIN>)) { run() }
