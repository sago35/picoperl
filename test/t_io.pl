# file I/O: 3-arg open with lexical handles, <$fh>, print/close, eof/seek/tell
use strict; use warnings;
$| = 1;

open(my $fh, '<', 'test/t_io_data.txt') or die "open: $!";
my $l1 = <$fh>;
print "1: $l1";
chomp(my $l2 = <$fh>);
print "2: [$l2]\n";
print "3: tell=", tell($fh), "\n";
my @rest = <$fh>;
print "4: ", scalar(@rest), " lines left, last=", $rest[-1];
print "5: eof=", (eof($fh) ? 1 : 0), "\n";
seek($fh, 0, 0) or die "seek: $!";
print "6: tell=", tell($fh), " eof=", (eof($fh) ? 1 : 0), "\n";
my ($n, $hits) = (0, 0);
while (<$fh>) { $n++; $hits++ if /line/ }   # <$fh> reads into $_ like perl
print "7: $n lines, $hits contain 'line'\n";
seek($fh, 11, 0);
my $mid = <$fh>;
print "8: $mid";
print "9: ", (close($fh) ? "closed" : "close failed"), "\n";

my $ok = open(my $nf, '<', 'test/no_such_file.txt');
print "10: ", (defined $ok ? "opened?!" : "failed"), " (\$!=$!) handle=", (defined $nf ? "def" : "undef"), "\n";

open(my $out, '>', 'test/io_tmp.txt') or die "write open: $!";
print $out "alpha\n";
print {$out} "beta gamma\n";
printf $out "%03d\n", 7;
say $out "delta";
print "11: ", (close($out) ? "ok" : "ng"), "\n";

open(my $in, '<', 'test/io_tmp.txt') or die $!;
my @all = <$in>;
print "12: ", scalar(@all), " lines: ", join("", @all);
close($in);

open(my $ap, '>>', 'test/io_tmp.txt') or die $!;
print $ap "tail\n";
close($ap);
open($in, '<', 'test/io_tmp.txt') or die $!;
seek($in, -5, 2) or die "seek end: $!";
my $t = <$in>;
print "13: $t";
while (my $line = <$in>) { print "14: unreachable\n" }
print "15: eof=", (eof($in) ? 1 : 0), "\n";
close($in);
# ファイルテスト
my $ft_file = 'test/t_io_data.txt';
my $ft_none = 'test/no_such_file.txt';
sub ft { defined $_[0] ? ($_[0] eq '' ? "''" : $_[0] > 1 ? "size" : $_[0]) : "undef" }
print "filetest file: ", join(" ", map { ft($_) } -e $ft_file, -f $ft_file, -d $ft_file, -s $ft_file, -z $ft_file, -r $ft_file, -l $ft_file), "\n";
print "filetest dir: ", join(" ", map { ft($_) } -e "test", -f "test", -d "test", -d 'test/'), "\n";
print "filetest none: ", join(" ", map { ft($_) } -e $ft_none, -f $ft_none, -d $ft_none, -s $ft_none), "\n";
print "filetest size: ", ((-s $ft_file) == length(do { open(my $f, '<', $ft_file) or die; local $/; my $c = join("", <$f>); $c }) ? "match" : "differ"), "\n";
print "filetest expr: ", (-e $ft_file ? "yes" : "no"), " ", (-e $ft_none ? "yes" : "no"), " ", (-f $ft_file && -s $ft_file > 0 ? "nonempty" : "empty"), " ", ((not -f $ft_none or -d $ft_none) ? "ok" : "ng"), " ", (-d "te" . "st" ? "concat" : "no"), "\n";
my %ft_h = (-s => 1, -e => 2, -x => 3); my $ft_n = 5; my $ft_e = 2;
print "filetest not: $ft_h{-s} $ft_h{-e} $ft_h{-x} ", $ft_n -$ft_e, " ", $ft_n - $ft_e, " ", -$ft_e, "\n";
unless (-e $ft_none) { print "filetest unless\n" }
# print STDERR (LIST) / print STDOUT (LIST)
print STDOUT ("paren ", "list", "\n");
print STDOUT (join("-", 1, 2), "\n");
printf STDOUT ("%s-%d\n", "fmt", 7);
print STDERR ("to stderr ", 1 + 1, "\n");
# chdir
require Cwd;
my $cd_start = Cwd::getcwd();
print "chdir: ", (chdir("test") ? "ok" : "failed"), " ", (Cwd::getcwd() eq "$cd_start/test" ? "moved" : "not moved"), " ", (-e "t_io.pl" ? "sees t_io.pl" : "no file"), "\n";
print "chdir back: ", (chdir "..") ? "ok" : "failed", " ", (Cwd::getcwd() eq $cd_start ? "back" : "elsewhere"), "\n";
print "chdir missing: ", (chdir("no_such_dir_here") ? "ok" : "failed"), "\n";
{ package ChdirPkg; sub go { chdir "." } } print "chdir in package: ", (ChdirPkg::go() ? "ok" : "failed"), "\n";
# $! は文字列としてはメッセージ、数値としてはエラー番号
$! = 0;
print "errno clear: [", $! + 0, "] [$!] ", ($! ? "true" : "false"), "\n";
open(my $en_fh, '<', 'test/no_such_file_here') or print "errno after open: $! (", $! + 0, ") ", ($! == 2 ? "ENOENT" : "other"), " ", ($! ? "true" : "false"), "\n";
$! = 13; print "errno set: $! / ", 0 + $!, " / ", -$!, " / ", ($! > 12 ? "gt" : "le"), ($! != 0 ? " set" : " clear"), "\n";
my $en_saved = do { open(my $f, '<', 'test/no_such_file_here'); $! };
$! = 0; $! = $en_saved; print "errno restored from a copy: ", $! + 0, " $!\n";
sub en_local { local $!; open(my $f, '<', 'test/no_such_file_here'); $! + 0 }
$! = 1; my $en_in = en_local(); print "errno local: inside $en_in, outside ", $! + 0, "\n";
my $en_copy = $!; print "errno copy is the message: $en_copy / " . $! . "\n";
# getpwnam / getpwuid / getgrnam / getgrgid (ホスト版)
my @pw_none = getpwnam("no_such_user_xyz_123"); my $pw_none = getpwnam("no_such_user_xyz_123");
my @gr_none = getgrnam("no_such_group_xyz_123");
print "passwd: no such user: ", scalar(@pw_none), " ", (defined $pw_none ? "def" : "undef"), " / no such group: ", scalar(@gr_none), "\n";
# uid 0 / gid 0 の登録が無い環境がある (wasm)。あるときだけ、名前と番号が行き来できることを確かめる
{
    my @pw = getpwuid(0); my @gr = getgrgid(0);
    my $pw_ok = !@pw || (@pw == 9 && scalar(getpwuid(0)) eq $pw[0] && getpwnam($pw[0]) == 0 && $pw[2] == 0);
    my $gr_ok = !@gr || (@gr == 4 && scalar(getgrgid(0)) eq $gr[0] && getgrnam($gr[0]) == 0 && $gr[2] == 0);
    print "passwd / group lookups: ", ($pw_ok ? "consistent" : "passwd differs"), " ", ($gr_ok ? "consistent" : "group differs"), "\n";
}
# bareword のファイルハンドル
open(BW, '>', 'test/io_tmp.txt') or die "open: $!";
print BW "a\n"; printf BW "%s\n", "b"; print {*BW} "c\n"; print {\*BW} "d\n";
{ my $g = \*BW; print $g "e\n"; print {$g} "f\n"; }
close(BW);
open(BW, '<', 'test/io_tmp.txt') or die "open: $!";
{ my $first = <BW>; chomp $first; my @rest = <BW>; print "bareword handle: $first ", scalar(@rest), " ", (eof(BW) ? "eof" : "more"), "\n"; }
close BW;
open(BW, '<', 'test/io_tmp.txt') or die;
{ my $all = ""; while (<BW>) { chomp; $all .= "[$_]" } print "while (<BW>): $all\n"; }
close(BW);
# ハンドルを値として渡す: \*FH、*FH、*FH{IO}、文字列に入ったグロブ
sub io_line { my $fh = shift; my $l = <$fh>; chomp $l; $l }
open(BW, '<', 'test/io_tmp.txt') or die;
print "passing a handle: ", io_line(\*BW), io_line(*BW), io_line(*BW{IO}), " ", scalar(readline(BW)) =~ /^d/ ? "readline(BW)" : "?", "\n";
{ local *BW; open(BW, '<', 'test/io_tmp.txt') or die; print "local *BW: ", io_line(\*BW), "\n"; close BW; }
print "after the scope: ", io_line(\*BW), " ", (defined *BW{IO} ? "io" : "no io"), " ", (defined *BW_NEVER{IO} || defined *BW_NEVER{IO} ? "io" : "no io"), "\n";
close BW;
{ no strict 'refs'; my $sym = \*{"IoGen::H1"}; open($sym, '<', 'test/io_tmp.txt') or die; print "glob ref as handle: ", ref($sym), " ", io_line($sym), "\n"; close($sym); }
# 標準ハンドル
{
    my $o = \*STDOUT; my $s = *STDOUT; my %h = (out => \*STDOUT);
    print $o "std: ref "; print {$o} "block "; print $s "glob "; print {*STDOUT} "star "; print {*STDOUT{IO}} "io "; print { $h{out} } "hash\n";
    print "fileno: ", fileno(STDIN), fileno(STDOUT), fileno(STDERR), fileno($o), " ", (defined fileno(BW) ? "open" : "closed"), "\n";
    $| = 1;      # perl の複製は別のバッファを持つので、順番が決まるように毎回書き出す
    open(my $dup, ">&", \*STDOUT) or die "dup: $!"; print $dup "dup of STDOUT\n"; close($dup);
    open(my $dup2, ">&" . fileno(STDOUT)) or die "dup: $!"; print {$dup2} "dup by number\n"; close($dup2);
    print "still open after close(\$dup)\n";
}
{ package IoPkg; print "std handles belong to main: ", *STDOUT, " ", *STDERR{PACKAGE}, "\n"; }
# $. : 最後に読んだハンドルの行番号
{
    open(my $ln, '<', 'test/t_io_data.txt') or die "open: $!";
    my $first = <$ln>; my $n1 = $.;
    my $second = <$ln>; print "line numbers: $n1 $. at line $.\n";
    my $w = ""; while (<$ln>) { $w .= "$. " } print "in while: $w/ after eof: $.\n";
    open(my $other, '<', 'test/t_io_data.txt') or die; my $o = <$other>; print "another handle: $.\n";
    close($other); print "after close: $. ", $. + 1, "\n";
    close($ln);
    open($ln, '<', 'test/t_io_data.txt') or die; my @all = <$ln>; print "list read: ", ($. == @all ? "counts every line" : "differs: $."), "\n"; close($ln);
    print "not in a regex: ", ("ab" =~ /a$./ ? "interpolated" : "anchor"), "\n";
}

# -t ハンドル: ファイルは端末ではない (偽だが定義済み)。標準ハンドルは環境しだいなので定義済みかだけ見る
{
    open(my $tfh, '<', $0) or die;
    my $tdup; open($tdup, '<&', $tfh) or die;
    print "-t: ", join(" ", (-t $tfh) ? "tty" : "file", defined(-t $tfh) ? "defined" : "undef", (-t $tdup) ? "tty" : "file",
        defined(-t STDIN) ? "defined" : "undef", defined(-t STDERR) ? "defined" : "undef"), "\n";
    close $tfh;
}
