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
my ($nt, $pass, $skip) = (0, 0, 0);
for my $d (sort keys %dist) {
    my @t = $d =~ /\.t$/ ? ($d) : do { my @x; find({ no_chdir => 1, wanted => sub { push @x, $_ if /\.t$/ } }, grep { -d } "$d/t"); sort @x };
    for my $t (@t) {
        my $dir = $d =~ /\.t$/ ? $root : "$root/$d";
        (my $tt = $t) =~ s{^\Q$d\E/}{} unless $d =~ /\.t$/;
        my $out = `cd '$dir' && PICOPERL_LIB='$lib' timeout 10 '$pico' '$tt' 2>&1 </dev/null`;
        my $code = $? >> 8; $nt++;
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
        my @err = grep { / at \S+ line \d+/ && !/^Use of uninitialized|isn't numeric|^Useless use|redefined at|^picoperl: |^#/ } split /\n/, $out;
        my $first = $err[0] // (grep { /^not ok/ } split /\n/, $out)[0] // "exit $code (no message)";
        my ($loc) = $first =~ / at (\S+ line \d+)/; $loc //= "";
        $first =~ s/ at \S+ line \d+.*//;
        if ($first =~ /^not ok/) { $first = "not ok (テストが失敗)"; print "NOTOK $d|$tt\n"; }
        print "WHY\t$d|$tt\t$first\t$loc\n"; $why{$first}++; $where{"$first\t$loc"}++ if $first =~ /^(syntax error|unexpected|Bareword|Global symbol|Can't modify|Undefined sub|Can't locate object)/;
    }
}
printf "配布物: %d、テストファイル: %d、成功: %d、スキップ: %d\n", scalar(keys %dist), $nt, $pass, $skip;
printf "  %4d  %s\n", $why{$_}, substr($_, 0, 90) for grep { defined } (sort { $why{$b} <=> $why{$a} } keys %why)[0 .. 24];
print "--- 場所別\n";
printf "  %4d  %s\n", $where{$_}, $_ for grep { defined } (sort { $where{$b} <=> $where{$a} } keys %where)[0 .. 39];
print "$_: $count{$_}\n" for keys %count;
