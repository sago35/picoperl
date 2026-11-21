#!/usr/bin/perl
# 読み込めた標準モジュールが属する配布物の付属テスト (.t) を picoperl で実行し、結果を集計する。
# test/corelib_check.pl の続き: あちらが「読み込めるか」を、こちらが「テストが通るか」を調べる。
#
#   cd PERL_SOURCE && perl /path/to/test/corelib_runtests.pl PICOPERL LIBDIR REPORT.tsv > result.txt
#
#   PERL_SOURCE  perl のソースツリー (cpan/ dist/ ext/ lib/ がある場所)。ここをカレントにして実行する
#   PICOPERL     ホスト版 picoperl の絶対パス (build/picoperl)
#   LIBDIR       perl のライブラリ (privlib と archlib) を 1 つにまとめ、picoperl の lib/*.pm で
#                上書きしたディレクトリの絶対パス (PICOPERL_LIB として渡す)
#   REPORT.tsv   corelib_check.pl -o で作った結果。ok だったモジュールの配布物だけを対象にする
#
# 出力 (1 テスト 1 行):
#   PASS  path        合格
#   SKIP  path        テスト自身が全体をスキップ (1..0)
#   WHY \t 配布物|テスト \t 最初のエラー \t 場所      不合格とその理由
# 最後に、合格数・スキップ数と、理由別・場所別の集計を出す。
#
# 合否は TAP として判定する (下の本体を参照)。1 テスト 10 秒で打ち切る。
# 環境変数 CORELIB_SAVE=DIR を付けると、通らなかったテストの出力を DIR/配布物__テスト.out に残す。
# picoperl の lib/ に代用品があるモジュールは、配布物に同梱の版ではなく代用品で実行する (下の説明を参照)。
use strict; use warnings; use File::Find; use Cwd qw(getcwd);
@ARGV == 3 or die "usage: cd PERL_SOURCE && perl $0 PICOPERL LIBDIR REPORT.tsv\n";
my ($pico, $lib, $tsv) = @ARGV;
my %ok; open my $fh, '<', $tsv or die; while (<$fh>) { my @f = split /\t/; $ok{$f[0]} = 1 if $f[1] eq 'ok' } close $fh;
my (%dist, %why, %count, %where); my $root = getcwd();
for my $m (sort keys %ok) {
    (my $rel = $m) =~ s{::}{/}g;
    my @hit = grep { -f } glob("cpan/*/lib/$rel.pm dist/*/lib/$rel.pm ext/*/lib/$rel.pm cpan/*/$rel.pm dist/*/$rel.pm");
    if (@hit) { my ($d) = $hit[0] =~ m{^(\w+/[^/]+)}; $dist{$d}{$m} = 1 }
    elsif (-f "lib/$rel.t") { $dist{"lib/$rel.t"}{$m} = 1 }
    else { $count{'モジュールにテストが見つからない'}++ }
}
# picoperl が lib/ に代用品を持つモジュール (List::Util、IO、Storable、MIME::Base64 ...) は、配布物に同梱の
# XS 版が先に見つかると「XS が必要」で終わってしまう。実際の使われ方 (代用品が読み込まれる) に合わせるため、
# その配布物のテストを実行するあいだだけ、同梱版のファイルを脇に退けておく (終われば元に戻す)。
(my $ownlib = $0) =~ s{[^/]+$}{../lib};
my @shims; find({ no_chdir => 1, wanted => sub { push @shims, substr($_, length($ownlib) + 1) if /\.pm$/ } }, $ownlib) if -d $ownlib;
my @hidden;
sub unhide { for my $f (@hidden) { rename "$f.pp_hidden", $f } @hidden = () }
$SIG{$_} = sub { unhide(); exit 1 } for qw(INT TERM HUP);
END { unhide() }
find({ no_chdir => 1, wanted => sub { if (/^(.*)\.pp_hidden$/) { rename $_, $1 } } }, grep { -d } qw(cpan dist ext));   # 前回の中断の後始末
my ($nt, $pass, $skip) = (0, 0, 0);
for my $d (sort keys %dist) {
    unhide();
    if ($d !~ /\.t$/) {
        for my $rel (@shims) {
            for my $f ("$d/lib/$rel", "$d/$rel") { if (-f $f && rename $f, "$f.pp_hidden") { push @hidden, $f } }
        }
    }
    my @t = $d =~ /\.t$/ ? ($d) : do { my @x; find({ no_chdir => 1, wanted => sub { push @x, $_ if /\.t$/ } }, grep { -d } "$d/t"); sort @x };
    for my $t (@t) {
        my $dir = $d =~ /\.t$/ ? $root : "$root/$d";
        (my $tt = $t) =~ s{^\Q$d\E/}{} unless $d =~ /\.t$/;
        my $out = `cd '$dir' && PICOPERL_LIB='$lib' timeout 10 '$pico' '$tt' 2>&1 </dev/null`;
        my $code = $? >> 8; $nt++;
        my $save = sub { return unless $ENV{CORELIB_SAVE}; (my $n = "$d|$tt") =~ s{[/|]}{__}g; open my $o, '>', "$ENV{CORELIB_SAVE}/$n.out" or return; print $o $out; close $o };
        # 合否は TAP として判定する: 終了コード 0、TODO でない not ok が無い、項目が 1 つ以上ある、
        # 計画 (1..N) があれば項目数と合う。全項目が TODO の失敗でも合格 (ok の行は無くてよい)。
        # 1..0 (全体をスキップ) は合格とは別に数える。
        my @lines  = grep { /^(?:not )?ok\b/ } split /\n/, $out;
        my $bad    = grep { /^not ok\b/ && !/#\s*TODO\b/i } @lines;
        my ($plan) = $out =~ /^1\.\.(\d+)/m;
        if ($code == 0 && !@lines && defined $plan && $plan == 0) { $skip++; print "SKIP $t\n"; $why{'テスト自身が全体をスキップ (1..0)'}++; next }
        if ($code == 0 && @lines && !$bad && (!defined $plan || $plan == @lines)) { $pass++; print "PASS $t\n"; next }
        if ($code == 0 && @lines && !$bad && defined $plan && $plan != @lines) { $why{'計画した項目数と実行した項目数が合わない'}++; print "WHY\t$d|$tt\tplan mismatch ($plan planned, " . scalar(@lines) . " run)\t\n"; next }
        # 警告を除いた最初のエラー行で分類し、場所 (ファイル:行) も控える
        $save->();                                   # CORELIB_SAVE=DIR: 通らなかったテストの出力を DIR に残す
        my @err = grep { / at \S+ line \d+/ && !/^Use of uninitialized|isn't numeric|^Useless use|redefined at|^picoperl: |^#/ } split /\n/, $out;
        my $first = $err[0] // (grep { /^not ok/ } split /\n/, $out)[0] // "exit $code (no message)";
        my ($loc) = $first =~ / at (\S+ line \d+)/; $loc //= "";
        $first =~ s/ at \S+ line \d+.*//;
        if ($first =~ /^not ok/) { $first = "not ok (テストが失敗)"; print "NOTOK $d|$tt\n"; }
        print "WHY\t$d|$tt\t$first\t$loc\n"; $why{$first}++; $where{"$first\t$loc"}++ if $first =~ /^(syntax error|unexpected|Bareword|Global symbol|Can't modify|Undefined sub|Can't locate object)/;
    }
}
unhide();
printf "配布物: %d、テストファイル: %d、成功: %d、スキップ: %d\n", scalar(keys %dist), $nt, $pass, $skip;
printf "  %4d  %s\n", $why{$_}, substr($_, 0, 90) for grep { defined } (sort { $why{$b} <=> $why{$a} } keys %why)[0 .. 24];
print "--- 場所別\n";
printf "  %4d  %s\n", $where{$_}, $_ for grep { defined } (sort { $where{$b} <=> $where{$a} } keys %where)[0 .. 39];
print "$_: $count{$_}\n" for keys %count;
