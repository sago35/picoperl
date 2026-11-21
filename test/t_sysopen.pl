use strict; use warnings;
use Fcntl;
# sysopen: O_* のフラグで開く
my $p = "test/io_tmp_sysopen.txt"; unlink $p;
print "missing: ", (sysopen(my $f0, $p, O_RDONLY) ? "opened" : "failed"), " ", ($! =~ /No such file/ ? "ENOENT" : "other: $!"), "\n";
sysopen(my $w, $p, O_WRONLY | O_CREAT | O_EXCL, 0600) or die "create: $!"; print $w "line1\nline2\n"; close $w;
print "excl again: ", (sysopen(my $x, $p, O_RDWR | O_CREAT | O_EXCL, 0600) ? "opened" : "failed"), " ", ($! =~ /File exists/ ? "EEXIST" : "other: $!"), "\n";
sysopen(my $r, $p, O_RDONLY) or die "read: $!"; my @l = <$r>; close $r; print "read: ", scalar(@l), " $l[1]";
sysopen(my $a, $p, O_WRONLY | O_APPEND) or die "append: $!"; print $a "line3\n"; close $a;
sysopen(my $rw, $p, O_RDWR) or die "rdwr: $!"; my $first = <$rw>; close $rw; print "rdwr: $first";
open(my $c, '<', $p) or die; my @all = <$c>; close $c; print "lines: ", scalar(@all), "\n";
sysopen(my $t, $p, O_WRONLY | O_TRUNC | O_CREAT) or die "trunc: $!"; print $t "new\n"; close $t;
open($c, '<', $p) or die; @all = <$c>; close $c; print "after trunc: @all";
sysopen(FH, $p, O_RDONLY) or die; print "bareword: ", scalar(<FH>); close FH;
unlink $p;
# パーミッションの指定 (作るときだけ効く。umask を引く)
{
    my $q = "test/io_tmp_sysopen2.txt"; unlink $q;
    my $old = umask(022);
    sysopen(my $h, $q, O_RDWR | O_CREAT | O_EXCL, 0400) or die "perm: $!"; print $h "x\n"; close $h;
    printf "0400: %04o\n", (stat($q))[2] & 0777; chmod 0600, $q; unlink $q;
    sysopen($h, $q, O_WRONLY | O_CREAT, 0666) or die; close $h; printf "0666 less umask: %04o\n", (stat($q))[2] & 0777;
    sysopen($h, $q, O_WRONLY, 0600) or die; close $h; printf "existing file keeps: %04o\n", (stat($q))[2] & 0777;
    umask($old); unlink $q;
}
# syswrite: 長さと位置を指定して書く。戻り値は書いたバイト数
{
    my $q = "test/io_tmp_syswrite.txt"; unlink $q;
    open(my $h, '>', $q) or die; my $buf = "0123456789";
    my $n1 = syswrite($h, $buf); my $n2 = syswrite($h, $buf, 3); my $n3 = syswrite($h, $buf, 4, 6); my $n4 = syswrite($h, $buf, 100, 8); my $n5 = syswrite($h, $buf, 2, -3);
    close $h;
    open($h, '<', $q) or die; my $all = <$h>; close $h;
    print "syswrite: $n1 $n2 $n3 $n4 $n5 [$all]\n";
    open($h, '<', $q) or die; { no warnings; my $r = syswrite($h, "x"); print "read-only: ", defined $r ? "wrote" : "undef", "\n"; } close $h;
    unlink $q;
}
