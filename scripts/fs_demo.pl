# ファイル操作の実機確認: mkdir / opendir / readdir / rename / unlink / rmdir
#
# シリアルで Enter を送るたびに、ドライブ上の fs_test/ で一通り試して結果を表示します。
# 各行が "ok" なら期待どおり、"NG" なら食い違いです。最後に fs_test/ を消すので、
# 何度実行してもドライブには何も残りません。
use strict;
use warnings;

my ($ok, $ng);
sub check {
    my ($name, $got, $want) = @_;
    if ($got eq $want) { $ok++; print "ok   $name\n" }
    else { $ng++; print "NG   $name: got [$got], want [$want]\n" }
}
sub names { my $d = shift; opendir(my $dh, $d) or return "opendir failed: $!"; my @n = sort readdir($dh); closedir($dh); return "@n" }
sub put { my ($path, $text) = @_; open(my $fh, '>', $path) or return 0; print $fh $text; close($fh); return 1 }

sub run {
    ($ok, $ng) = (0, 0);
    my $d = "fs_test";
    print "-- start --\n";
    check("mkdir",                 (mkdir($d) ? 1 : 0), 1);
    check("mkdir again fails",     (mkdir($d) ? 1 : 0), 0);
    check("  \$! says why",         "$!", "File exists");
    check("-d on the new dir",     (-d $d ? 1 : 0), 1);
    check("mkdir of a subdir",     (mkdir("$d/sub") ? 1 : 0), 1);
    check("mkdir without parent",  (mkdir("$d/no/deep") ? 1 : 0), 0);
    check("write 3 files",         put("$d/b.txt", "bbb\n") + put("$d/a.txt", "a\n") + put("$d/c.dat", "cccc\n"), 3);
    check("readdir (list)",        names($d), ". .. a.txt b.txt c.dat sub");

    opendir(my $dh, $d) or print "NG   opendir: $!\n";
    my $n = 0; $n++ while defined(my $e = readdir($dh));
    check("readdir (one by one)",  $n, 6);
    rewinddir($dh); my @again = readdir($dh); closedir($dh);
    check("rewinddir",             scalar(@again), 6);

    opendir(DH, $d) or print "NG   opendir(DH): $!\n";
    my @txt = sort grep { /\.txt$/ } readdir(DH); closedir(DH);
    check("bareword handle + grep", "@txt", "a.txt b.txt");
    check("opendir of a missing dir", (opendir(my $no, "$d/nope") ? 1 : 0), 0);
    check("opendir of a file",     (opendir(my $nf, "$d/a.txt") ? 1 : 0), 0);

    check("rename",                (rename("$d/a.txt", "$d/z.txt") ? 1 : 0), 1);
    check("  old name is gone",    (-e "$d/a.txt" ? 1 : 0), 0);
    check("  new name has the data", (-s "$d/z.txt") || 0, 2);
    check("rename onto a file",    (rename("$d/z.txt", "$d/b.txt") ? 1 : 0), 1);
    check("  it was replaced",     (-s "$d/b.txt") || 0, 2);
    check("rename of a missing file", (rename("$d/nope", "$d/x") ? 1 : 0), 0);
    check("-f \$path && -s _",      (-f "$d/c.dat" && (-s _) == 5 ? 1 : 0), 1);

    check("unlink on a dir fails", unlink("$d/sub"), 0);
    check("rmdir on a file fails", (rmdir("$d/c.dat") ? 1 : 0), 0);
    check("rmdir of a full dir fails", (rmdir($d) ? 1 : 0), 0);
    check("unlink (2 of 3 exist)", unlink("$d/b.txt", "$d/c.dat", "$d/nope"), 2);
    check("rmdir of the subdir",   (rmdir("$d/sub") ? 1 : 0), 1);
    check("the dir is empty now",  names($d), ". ..");
    check("rmdir",                 (rmdir($d) ? 1 : 0), 1);
    check("it is gone",            (-e $d ? 1 : 0), 0);
    check("root still lists main.pl", (scalar(grep { lc($_) eq "main.pl" } split / /, names("."))), 1);

    print "-- done: $ok ok, $ng NG --\n";
}

print "fs demo: press Enter to run\n";
while (<STDIN>) { run(); print "press Enter to run again\n" }
