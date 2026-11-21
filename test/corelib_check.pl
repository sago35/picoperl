#!/usr/bin/perl
# perl 同梱の標準モジュール (.pm) を 1 つずつ picoperl で読み込み、どこまで通るかを集計する。
#
#   perl test/corelib_check.pl [-v] [-o report.tsv] PICOPERL PERL
#
#   PICOPERL  ホスト版 picoperl のパス (build/picoperl)
#   PERL      比較対象の perl (この perl の privlib / archlib にあるモジュールを調べる)
#
# 各モジュールのファイルをスクリプトとして実行する (= コンパイルしてトップレベルを実行)。
# 依存モジュールは PICOPERL_LIB 経由で同じ perl のライブラリから読み込まれる。
# 結果の分類:
#   ok       エラーなく読み込めた
#   xs       XS (.so) を必要とするモジュール。picoperl は XS 非対応なので別枠
#   fail     picoperl がエラーを出した (最初のエラーを根本原因として、原因別・場所別に集計)
#   timeout  10 秒で終わらなかった
#   crash    シグナルで落ちた
use strict;
use warnings;
use File::Find;
use File::Temp qw(tempdir);
use Cwd qw(abs_path);

my ($verbose, $out);
while (@ARGV && $ARGV[0] =~ /^-/) {
    my $o = shift;
    if    ($o eq '-v') { $verbose = 1 }
    elsif ($o eq '-o') { $out = shift }
    else               { die "unknown option $o\n" }
}
my ($pico, $perl) = @ARGV;
die "usage: $0 [-v] [-o report.tsv] PICOPERL PERL\n" unless $pico && $perl;
$pico = abs_path($pico);

chomp(my $privlib = `$perl -MConfig -e 'print \$Config{privlibexp}'`);
chomp(my $archlib = `$perl -MConfig -e 'print \$Config{archlibexp}'`);
$_ = abs_path($_) // $_ for $privlib, $archlib;
chomp(my $version = `$perl -e 'print \$^V'`);

# privlib と archlib を 1 つのディレクトリにまとめる (PICOPERL_LIB は 1 つしか指定できない)
my $merged = tempdir(CLEANUP => 1) . "/lib";
mkdir $merged;
my (%mod, %xs);
for my $root ($privlib, $archlib) {
    next unless -d $root;
    find({ no_chdir => 1, wanted => sub {
        return if $File::Find::name eq $root;
        my $rel = substr($File::Find::name, length($root) + 1);
        # archlib が privlib の下にある配置では二重に数えない
        if ($root eq $privlib && index($archlib, "$privlib/") == 0
            && index($File::Find::name, $archlib) == 0) { $File::Find::prune = 1; return }
        if (-d $File::Find::name) { mkdir "$merged/$rel"; return }
        if ($rel =~ m{^auto/(.+)/[^/]+\.so$}) { (my $m = $1) =~ s{/}{::}g; $xs{$m} = 1; return }
        return unless $rel =~ /\.(pm|pl|ph|ix|al)$/ || $rel =~ m{^unicore/};
        symlink $File::Find::name, "$merged/$rel";
        if ($rel =~ /\.pm$/) { (my $m = $rel) =~ s{/}{::}g; $m =~ s/\.pm$//; $mod{$m} = $rel }
    } }, $root);
}

# picoperl 自身の lib/ にあるモジュール (builtin、Time::HiRes、List::Util など) は、実機と同じく
# perl 本体のものより優先する
(my $pico_lib = abs_path($0)) =~ s{/test/[^/]+$}{/lib};
my %shim;
if (-d $pico_lib) {
    find({ no_chdir => 1, wanted => sub {
        return unless -f $File::Find::name && $File::Find::name =~ /\.pm$/;
        my $rel = substr($File::Find::name, length($pico_lib) + 1);
        my $dir = $rel =~ m{(.*)/} ? $1 : '';
        my $path = $merged;
        for my $part (split m{/}, $dir) { $path .= "/$part"; mkdir $path }
        (my $m = $rel) =~ s{/}{::}g; $m =~ s/\.pm$//;
        return unless exists $mod{$m};      # 標準モジュールと同名のものだけが対象
        unlink "$merged/$rel";
        symlink $File::Find::name, "$merged/$rel";
        $shim{$m} = 1;
    } }, $pico_lib);
}

sub normalize {
    my ($msg) = @_;
    $msg =~ s/ at \S+ line \d+.*//s;
    $msg =~ s/\s+$//;
    return $msg;
}

my (%count, %why, %dep, @rows);
my $own = 0;
for my $m (sort keys %mod) {
    my $file = "$merged/$mod{$m}";
    my $pid = open(my $ph, '-|') // die "fork: $!";
    if (!$pid) {
        open STDERR, '>&', \*STDOUT;
        open STDIN, '<', '/dev/null';
        $ENV{PICOPERL_LIB} = $merged;
        $ENV{PICOPERL_SIM_QUIET} = 1;
        exec 'sh', '-c', 'ulimit -v 2000000; exec timeout 10 "$0" "$1"', $pico, $file;
        exit 127;
    }
    my $text = do { local $/; <$ph> } // '';
    close $ph;
    my ($sig, $code) = ($? & 127, $? >> 8);
    my @lines = grep { !/^picoperl: / } split /\n/, $text;
    # 最初のエラー行が根本原因 (後ろは "Compilation failed in require" の連鎖)
    my $first = (grep { / at \S+ line \d+/ } @lines)[0] // $lines[0] // '';
    my $where = $first =~ / at (\S+) line \d+/ ? $1 : '';
    $where =~ s{^\Q$merged\E/}{};
    my $last = $first;
    my ($status, $reason) = ('ok', '');
    if    ($code == 124)       { $status = 'timeout' }
    elsif ($sig || $code > 128 && $code != 255) { $status = 'crash'; $reason = "signal " . ($sig || $code - 128) }
    elsif ($code != 0)         { $status = 'fail'; $reason = normalize($last) }
    # "Can't load module X, dynamic loading not available ..." は 3 行に分かれ、" at FILE line N" は
    # 最後の行にだけ付く。理由が空になったら最初の行を使う (XS モジュールへの依存が見えるように)
    $reason = normalize($lines[0] // '') if $status eq 'fail' && $reason eq '';
    # XS モジュールは読み込めなくても picoperl の Perl 対応度とは別の話なので別枠にする
    $status = 'xs' if $status ne 'ok' && $xs{$m} && !$shim{$m};
    $count{$status}++;
    if ($status eq 'fail') {
        $why{$reason}++;
        # 自分自身のファイルで失敗したのか、依存モジュールで失敗したのか
        if ($where eq $mod{$m}) { $own++ } else { $dep{$where}++ }
    }
    push @rows, [$m, $status, $reason, $where, $first];
    print "$status\t$m\t$reason\n" if $verbose;
}

if ($out) {
    open my $fh, '>', $out or die "$out: $!";
    print $fh join("\t", @$_), "\n" for @rows;
    close $fh;
}

my $total = keys %mod;
print "perl $version の標準モジュール: $total 個\n";
print "  (picoperl の lib/ で置き換えたもの: ", join(" ", sort keys %shim), ")\n" if %shim;
printf "  %-8s %4d (%.1f%%)\n", $_, $count{$_} // 0, 100 * ($count{$_} // 0) / $total
    for qw(ok fail xs timeout crash);
print "\n失敗の原因 (多い順):\n";
my @top = sort { $why{$b} <=> $why{$a} || $a cmp $b } keys %why;
printf "  %4d  %s\n", $why{$_}, $_ for @top;
print "\n失敗した場所: モジュール自身 $own 個、依存モジュール ", ($count{fail} // 0) - $own, " 個\n";
print "足を引っ張っている依存モジュール (多い順):\n";
printf "  %4d  %s\n", $dep{$_}, $_ for sort { $dep{$b} <=> $dep{$a} || $a cmp $b } keys %dep;
