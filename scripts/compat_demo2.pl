# 互換性まわりの実機確認 その 2: 引数の別名、要素への参照、eval と caller、行番号、正規表現の追加分など
#
# シリアルで Enter を送るたびに一通り試して結果を表示します。
# 各行が "ok" なら期待どおり、"NG" なら食い違いです。ドライブには何も書きません。
# (compat_demo.pl と 1 本にまとめるとボードの RAM に収まらないので分けてあります)
use strict;
use warnings;

my ($ok, $ng);
sub check {
    my ($name, $got, $want) = @_;
    if ($got eq $want) { $ok++; print "ok   $name\n" }
    else { $ng++; print "NG   $name: got [$got], want [$want]\n" }
}

package Ind;        # 間接オブジェクト構文 (new Ind(...)) と goto &sub 用
sub new { my $c = shift; bless { args => [@_] }, $c }
sub count { scalar @{ $_[0]{args} } }
sub real_caller { my @c = caller; $c[0] }
sub via_goto { goto &real_caller }

package main;
use constant IDX_ONE => 1;
sub cnt2($$) { "$_[0]/$_[1]" }                    # プロトタイプの $ はスカラーコンテキスト
sub trim_args { for (@_) { s/^\s+//; s/\s+$// } }  # @_ 経由で呼び出し元を書き換える
sub set_second { my $self = shift; $_[0] = "set" }
sub x { "<@_>" }                                  # x という名前のサブ
sub line_of { (caller)[2] }
sub ev_ret { my $v = eval { return "inner"; 1 }; "after:$v" }
sub ev_depth { my @c = eval { caller(0) }; $c[3] }
sub bare_val { { "bare" } }

sub run {
    ($ok, $ng) = (0, 0);
    print "-- start --\n";
    my @la = (1, 2);
    # ループと構文
    my ($rt, @ro) = (0); for my $i (1 .. 2) { $rt++; if ($i == 2 && $rt < 4) { push @ro, "r"; redo } push @ro, $i }
    check("redo",                           "@ro", "1 r r 2");
    my $io = new Ind(1, 2, 3); check("indirect object: new Ind(...)", ref($io) . $io->count, "Ind3");
    my @mh = map {{ n => $_ }} 1 .. 2; check("map {{ ... }} makes hashes",  ref($mh[0]) . $mh[1]{n}, "HASH2");
    check("bare block returns its value",   bare_val(), "bare");
    my @la2 = (my @inner = (4, 5, 6)); check("(my \@l = LIST) as a list", scalar(@la2), 3);
    my @cm = (1,, 2, => 3); check("empty elements between commas", scalar(@cm), 3);
    check("x(...) calls sub x, x repeats",  x("a") . ("-" x 2), "<a>--");
    check("prototype \$ gives scalar context", cnt2(@la, @cm), "2/3");
    my @row = ("zero", "one"); check('"$row[CONST]" in a string',     "$row[IDX_ONE]", "one");

    # 引数と参照
    my ($ta, $tb) = ("  a ", " b"); trim_args($ta, $tb); check("\@_ aliases the caller's variables", "[$ta][$tb]", "[a][b]");
    my $sv = "old"; main->set_second($sv); check("\$_[0] after shift",               $sv, "set");
    my ($fa, @fl) = (" p ", " q "); for ($fa, @fl) { s/\s//g } check("foreach changes its variables", "$fa@fl", "pq");
    my %eh = (k => 1); my @ea = (1, 2); my ($er, $eq) = (\$eh{k}, \$ea[1]); $$er += 10; $$eq .= "x";
    check("\\\$h{k} and \\\$a[i] point at the element", "$eh{k} @ea", "11 1 2x");
    open(my $efh, '>>', \$eh{log}) or die; print $efh ("a", "b"); close $efh; check("in-memory file on \\\$h{k}", $eh{log}, "ab");
    my $rc = \(IDX_ONE); check("\\(CONST) is a reference to the value", ref($rc) . $$rc, "SCALAR1");

    # eval、caller、行番号
    check("return inside eval { } leaves the eval", ev_ret(), "after:inner");
    check("caller counts eval { } as a frame",      ev_depth(), "(eval)");
    check("goto &sub: caller sees the first caller", Ind::via_goto(), "main");
    my @ln = (line_of(),
              line_of()); check("calls report the line the statement starts on", $ln[0] == $ln[1] ? "same" : "differ", "same");
#line 5000
    check("#line sets the line number",     __LINE__, 5000);

    # 正規表現と pack
    my ($lx2, @tk) = ("a1 b"); for (1 .. 9) { if ($lx2 =~ /\G\s+/gc) { } elsif ($lx2 =~ /\G([a-z])/gc) { push @tk, "w$1" } elsif ($lx2 =~ /\G(\d)/gc) { push @tk, "d$1" } else { last } }
    check("\\G and /gc: a lexer loop",       "@tk", "wa d1 wb");
    (my $ind = "a\nb\n") =~ s/^/> /mg; check("/m ^ does not match after the last newline", length($ind), 8);
    check("\\cX in a regex",                 ("a\tb" =~ /a\cIb/ ? 1 : 0), 1);
    my $dl = "k=v"; check("m;...; after =~",              ($dl =~ m;^(\w)=(\w)$; ? "$1$2" : "no"), "kv");
    check("unpack checksum %32C*",          unpack("%32C*", "abc"), 294);
    my $h0 = defined &Board::_heap_free ? Board::_heap_free() : 0;      # ボード上だけ: Fcntl の読み込みで減る空きを見る
    my $fc = eval { require Fcntl; Fcntl::SEEK_END() . Fcntl::O_RDWR() }; check("Fcntl constants", defined $fc ? $fc : "err: $@", "22");
    my $h1 = defined &Board::_heap_free ? Board::_heap_free() : 0;
    print "     heap free: $h0 before Fcntl, $h1 after (", $h0 - $h1, " bytes used; next to nothing on a second run)\n" if $h0;

    print "-- done: $ok ok, $ng NG --\n\n";
}

print "compat demo 2: press Enter to run the checks\n";
while (defined(my $l = <STDIN>)) { run() }
