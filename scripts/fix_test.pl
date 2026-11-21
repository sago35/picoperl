# 本体の修正の実機確認 (select、文頭の {、*_、プロトタイプの引数、裸のハンドル)
#
# シリアルで Enter を送るたびに一通り試して、結果 (ok / NG) と空きメモリを表示します。
# ドライブには fix_test.txt を書いて、最後に消します。
#
# 見るところ:
#   - 各行が ok で、最後が "N ok, 0 NG" であること
#   - Enter を何度送っても "heap free" の before の値が変わらないこと
use strict;
use warnings;

my ($ok, $ng);
sub check {
    my ($name, $got, $want) = @_;
    if ($got eq $want) { $ok++; print "ok   $name\n" }
    else { $ng++; print "NG   $name: got [$got], want [$want]\n" }
}
sub heap  { defined &Board::_heap_free ? Board::_heap_free() : 0 }
sub slurp { open(my $in, "<", $_[0]) or return "open failed: $!"; local $/; my $t = <$in>; close $in; $t =~ s/\n/|/g; $t }
sub two ($$;$) { scalar(@_) . (defined $_[0] ? "[$_[0]]" : "[undef]") }
sub none  { return }

package Loose;      # use strict の無いパッケージ: open(FH, ...) と print FH / <FH> が同じハンドルを指す
no strict;
sub write_read {
    my $f = shift;
    open(LOG, ">", $f) or return "open failed: $!";
    print LOG "from a package\n"; printf LOG "%d\n", 42; close LOG;
    open(RD, "<", $f) or return "open failed: $!";
    my @l = <RD>; my $at_end = eof(RD) ? "eof" : "more"; close RD;
    chomp @l; return "@l $at_end";
}
sub strip { local *_ = \join("", @_); s/^\s+//; s/a/X/; return $_ }      # パッケージの中の *_ も main のもの

package main;

sub run {
    ($ok, $ng) = (0, 0);
    my $before = heap();
    my $file = "fix_test.txt";

    # select(FH): ハンドルなしの print / printf の出力先
    {   open(my $out, ">", $file) or die "open: $!";
        my $old = select($out); print "one\n"; printf "%s\n", "two"; select($old); close $out;
        check("select(\$fh)", slurp($file) . " $old", "one|two| main::STDOUT"); }
    {   open(FOO, ">", $file) or die "open: $!";
        my $old = select(FOO); print "bare\n"; my $cur = select(STDOUT); close FOO;
        check("select(FOO)", slurp($file) . " $cur", "bare| main::FOO"); }
    print "     (this line must be visible: output is back on the serial port)\n";

    # strict の無いパッケージでの裸のハンドル
    check("bareword handles in a package", Loose::write_read($file), "from a package 42 eof");

    # local *_ = \$x
    {   local $_ = "main";
        check("*_ in a package", Loose::strip("  a b") . " $_", "X b main");
        my $x = "abc"; { local *_ = \$x; tr/a-c/A-C/; }
        check("local *_ = \\\$x", "$x $_", "ABC main"); }

    # 文の先頭の { : 無名ハッシュかブロックか
    {   no warnings;
        my @r = map { eval $_ } ("[]", "{}", "{ a => 1 }", '{ "k", 2 }');
        check("{ at the start of a statement", join(",", map { ref } @r) . " " . (eval "{ 5 }") . " " . (eval "{ my \$v = 3; \$v + 1 }"), "ARRAY,HASH,HASH,HASH 5 4");
        my $d = eval "{ name => 'x', list => [1, 2] }";
        check("  data read back", "$d->{name} @{$d->{list}}", "x 1 2"); }

    # プロトタイプ ($$;$) の引数
    {   no warnings;
        my @f = (7, 8, 9);
        check("eval { } as a \$ argument", join(" ", two(eval { die "x\n" }, undef, "name"), two(eval { (1, 2) }, 3), two(do { (4, 5) }, 1), two((@f), 1), two(none(), 1)), "3[undef] 2[2] 2[5] 2[3] 2[undef]"); }

    unlink $file;
    check("file removed", (-e $file ? "there" : "gone"), "gone");
    my $after = heap();
    print "$ok ok, $ng NG\n";
    print "heap free: $before before, $after after\n" if $before;
    print "press Enter to run again\n";
}

print "fix test: press Enter (heap free ", heap(), ")\n";
while (defined(my $l = <STDIN>)) { run() }
