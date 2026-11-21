# Storable / pipe などの実機確認
#
# シリアルで Enter を送るたびに一通り試して、結果 (ok / NG) と空きメモリを表示します。
# ドライブには storable_test.sto と storable_run.sto の 2 つを書きます (どちらも数十バイト)。
#
# 見るところ:
#   - 各行が ok で、最後が "N ok, 0 NG" であること
#   - Enter を何度送っても "heap free" の before の値が変わらないこと
#   - "run #N" の N が Enter のたびに 1 ずつ増えること (ファイルに保存して読み戻している)。
#     main.pl を保存し直して再読み込みさせたあとも、N が続きから増えること
#     (電源を入れ直すとドライブが初期状態に戻るので、N は 1 からになります)
use strict;
use warnings;
use Storable qw(store nstore retrieve freeze nfreeze thaw dclone);

my ($ok, $ng);
sub check {
    my ($name, $got, $want) = @_;
    if ($got eq $want) { $ok++; print "ok   $name\n" }
    else { $ng++; print "NG   $name: got [$got], want [$want]\n" }
}
sub hx { join "", map { sprintf "%02x", ord } split //, shift }
sub unhx { join "", map { chr hex } $_[0] =~ /../g }
sub heap { defined &Board::_heap_free ? Board::_heap_free() : 0 }

package Pt;         # フック付きのクラス: x と y だけを保存する
sub new { bless { x => $_[1], y => $_[2], cache => "tmp" }, $_[0] }
sub STORABLE_freeze { "$_[0]{x},$_[0]{y}" }
sub STORABLE_thaw { @{$_[0]}{qw(x y)} = split /,/, $_[2] }

package Names;      # strict の下で、名前で指したサブを自分のパッケージから探す
sub mine { "m" }
sub find { my $n = shift; no warnings; defined(&$n) ? 1 : 0 }

package main;

sub setit  { $_[0] = 5; $_[1] = 6 }
sub viaref { my $r = \$_[0]; $$r = 7 }

sub run {
    ($ok, $ng) = (0, 0);
    my $before = heap();
    local $Storable::canonical = 1;
    my $data = { a => 1, bb => [2] };

    # バイト列が PC の perl と同じであること (下の 16 進は perl 5.44 の出力)
    check("nfreeze bytes", hx(nfreeze($data)), "050b0300000002088100000001610402000000010882000000026262");
    check("freeze bytes",  hx(freeze($data)),  "040b083132333435363738040808080302000000088101000000610402010000000882020000006262");
    # PC の perl が作ったバイト列を読む (整数、小数、長い整数、bless、共有)
    my $t = thaw(unhx("050b0200000005090000012c0a03312e350a0a35303030303030303030041103436c7303000000010881000000016e040000000005"));
    check("thaw a perl image", "$t->[0] $t->[1] $t->[2] " . ref($t->[3]) . " $t->[3]{n} " . ($t->[3] == $t->[4] ? "shared" : "copied"), "300 1.5 5000000000 Cls 1 shared");

    # ファイルへ保存して読み戻す
    my $obj = bless { name => "obj", list => [1, 2, 3] }, "My::Class";
    my $big = { int => 42, neg => -7, num => 3.25, str => "hello", undef => undef, obj => $obj, again => $obj, nest => [[1, [2, [3]]]] };
    check("store", (store($big, "storable_test.sto") ? 1 : 0), 1);
    my $d = retrieve("storable_test.sto");
    check("retrieve", "$d->{int} $d->{neg} $d->{num} $d->{str} " . (defined $d->{undef} ? "def" : "undef") . " $d->{nest}[0][1][1][0]", "42 -7 3.25 hello undef 3");
    check("  object", ref($d->{obj}) . " $d->{obj}{name} @{$d->{obj}{list}} " . ($d->{obj} == $d->{again} ? "shared" : "copied"), "My::Class obj 1 2 3 shared");
    check("nstore / retrieve", (nstore([1, "two", [3]], "storable_test.sto") ? join(",", map { ref $_ ? @$_ : $_ } @{ retrieve("storable_test.sto") }) : "failed"), "1,two,3");
    check("-s of the file", (-s "storable_test.sto"), 26);

    # 循環参照、フック、dclone
    my $cyc = [1]; push @$cyc, $cyc;
    my $c2 = thaw(freeze($cyc));
    check("cycle", ($c2->[1] == $c2 ? "kept" : "lost"), "kept");
    @$cyc = (); @$c2 = ();                           # 輪を切る (切らないと解放されない)
    my $p = thaw(freeze([Pt->new(3, 4), Pt->new(5, 6)]));
    check("hooks", ref($p->[0]) . " $p->[0]{x} $p->[0]{y} $p->[1]{x} " . (exists $p->[0]{cache} ? "cache" : "nocache"), "Pt 3 4 5 nocache");
    check("hook bytes", hx(nfreeze(Pt->new(3, 4))), "050b130202507403332c34");
    my $dc = dclone($big);
    check("dclone", "$dc->{obj}{name} " . ($dc->{obj} == $obj ? "same" : "copy"), "obj copy");
    check("truncated image", (defined(eval { thaw(substr(nfreeze($data), 0, 9)) }) ? "read" : "undef"), "undef");
    check("broken image", (eval { thaw("\x05\x0b\x7f"); 1 } ? "read" : "died"), "died");

    # pipe (メモリ上のバッファ)
    pipe(my $r, my $w) or die "pipe: $!";
    print $w "line1\nline2\n"; close $w;
    my @l = <$r>; close $r;
    check("pipe", scalar(@l) . " $l[1]", "2 line2\n");
    pipe(R, W); print W "bare\n"; close W; my $bl = <R>; close R;
    check("pipe(R, W)", $bl, "bare\n");

    # この日に直した本体の動作
    setit(my $v, my $v2);
    check("f(my \$x) writes back", "$v $v2", "5 6");
    my $x; viaref($x);
    check("\\\$_[0] writes back", $x, 7);
    check("defined &\$name in a package", Names::find("mine") . Names::find("other"), "10");

    # 実行回数をファイルに持つ
    my $run = -e "storable_run.sto" ? retrieve("storable_run.sto") : { n => 0 };
    $run->{n}++;
    store($run, "storable_run.sto");
    my $after = heap();
    print "$ok ok, $ng NG   run #$run->{n}\n";
    print "heap free: $before before, $after after\n" if $before;
    print "press Enter to run again\n";
}

print "storable test: press Enter (heap free ", heap(), ")\n";
while (defined(my $l = <STDIN>)) { run() }
