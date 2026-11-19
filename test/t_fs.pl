# mkdir / rmdir / unlink / rename / opendir / readdir と、ホスト版だけの stat / chmod など
use strict; use warnings;
my $base = "test/fs_tmp";
mkdir $base;
sub show { my ($n, @v) = @_; print "$n: ", join("|", map { defined $_ ? $_ : "undef" } @v), "\n" }
my $d = "$base/work";
show("mkdir", (mkdir($d) ? 1 : 0), (mkdir($d) ? 1 : 0), ($! ? "err set" : "no err"), (-d $d ? "dir" : "nodir"), (mkdir("$d/sub", 0755) ? 1 : 0), (mkdir("$d/no/such/deep") ? 1 : 0));
for my $n (qw(b.txt a.txt c.dat)) { open(my $fh, ">", "$d/$n") or die "open: $!"; print $fh "data $n\n"; close($fh); }
opendir(my $dh, $d) or die "opendir: $!";
my @all = sort readdir($dh); closedir($dh);
show("readdir list", @all);
opendir($dh, $d) or die; my @one; while (defined(my $e = readdir($dh))) { push @one, $e } closedir $dh;
show("readdir scalar", scalar(@one), join(",", sort @one) eq join(",", @all) ? "same" : "differs");
opendir(DH, $d) or die; my @txt = sort grep { /\.txt$/ } readdir(DH); rewinddir(DH); my $n = () = readdir(DH); my @rest = readdir(DH); closedir(DH);
show("bareword handle", @txt, $n, scalar(@rest));
show("opendir fails", (opendir(my $no, "$d/nope") ? 1 : 0), ($! ? "err set" : "no err"), (opendir(DH, "$d/a.txt") ? 1 : 0));
show("rename", (rename("$d/a.txt", "$d/z.txt") ? 1 : 0), (-e "$d/a.txt" ? "old there" : "old gone"), (-e "$d/z.txt" ? "new there" : "new missing"), (rename("$d/nope", "$d/x") ? 1 : 0));
show("unlink", unlink("$d/z.txt"), unlink("$d/b.txt", "$d/c.dat", "$d/nope"), unlink("$d/nope"), (-e "$d/b.txt" ? "there" : "gone"));
{ open(my $fh, ">", "$d/u.txt") or die; close $fh; local $_ = "$d/u.txt"; show("unlink \$_", unlink, (-e "$d/u.txt" ? "there" : "gone")); }
show("rmdir", (rmdir($d) ? 1 : 0), (rmdir("$d/sub") ? 1 : 0), (rmdir($d) ? 1 : 0), (-d $d ? "dir" : "gone"), (rmdir($d) ? 1 : 0));
# 小さなツリーを作って再帰的に消す
sub rmtree_ { my $p = shift; if (-d $p) { opendir(my $h, $p) or return 0; my @e = grep { $_ ne "." && $_ ne ".." } readdir($h); closedir($h); rmtree_("$p/$_") for @e; return rmdir($p) } return unlink($p) }
mkdir "$base/t"; mkdir "$base/t/x"; mkdir "$base/t/x/y"; for my $f ("$base/t/1", "$base/t/x/2", "$base/t/x/y/3") { open(my $fh, ">", $f) or die; close $fh }
show("tree", rmtree_("$base/t") ? 1 : 0, (-e "$base/t" ? "there" : "gone"));
# _ : 直前のファイルテストや stat が見たファイル
{
    mkdir "$base/u"; open(my $fh, ">", "$base/u/f") or die; print $fh "abc"; close $fh;
    my @r;
    if (-e "$base/u") { push @r, (-d _ ? "dir" : "not dir"), (-f _ ? "file" : "not file") }
    push @r, (-f "$base/u/f" && -r _ && !-d _ ? "plain readable file" : "no"), ((-s _) == 3 ? "3 bytes" : "other size");
    show("underscore", @r);
    unlink "$base/u/f"; rmdir "$base/u";
}
# ここからはホスト版だけ (ボードの FAT には権限やリンクが無い)
my $f = "$base/s.txt"; open(my $fh, ">", $f) or die; print $fh "12345"; close $fh;
my @st = stat($f); my @ls = lstat($f);
show("stat", scalar(@st), $st[7], ($st[2] & 0170000) == 0100000 ? "regular" : "other", scalar(@ls), scalar(my @none = stat("$base/nope")), (stat($f) ? "true" : "false"), (stat("$base/nope") ? "true" : "false"), (stat $f)[7]);
show("chmod", chmod(0600, $f), sprintf("%o", (stat $f)[2] & 07777), chmod(0644, $f, "$base/nope"), sprintf("%o", (stat $f)[2] & 07777));
show("utime", utime(1000000000, 1100000000, $f), (stat $f)[8], (stat $f)[9]);
show("chown", chown(-1, -1, $f), chown(-1, -1, "$base/nope"));
show("symlink", (symlink("s.txt", "$base/l") ? 1 : 0), readlink("$base/l"), (-l "$base/l" ? "link" : "nolink"), (lstat("$base/l"))[2] != (stat("$base/l"))[2] ? "lstat differs" : "same", readlink($f) // "undef");
# ハードリンクは無い環境がある (wasm のメモリ上のファイルシステム)。作れたときだけリンク数を確かめる
{ my $made = link($f, "$base/h"); show("link", (!$made || (stat $f)[3] == 2 ? "consistent" : "link count is wrong")); }
unlink("$base/l", "$base/h", $f);
{ open(my $fh, ">", "$base/st") or die; print $fh "12345"; close $fh; my @st = stat("$base/st"); my @again = stat(_); show("stat(_)", scalar(@again), ($st[7] == $again[7] ? "same size" : "differs"), ((lstat $base) && -d _ ? "dir" : "?")); unlink "$base/st"; }
show("cleanup", (rmdir($base) ? 1 : 0), (-e $base ? "there" : "gone"));
print "done\n";
