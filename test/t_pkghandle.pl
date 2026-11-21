# use strict の無いパッケージでも、open(FH, ...) と print FH / <FH> は同じハンドルを指す
package PhPk;
my $file = "test/io_tmp_pkghandle.txt";
open(LOG, ">", $file) or die; print LOG "x\n"; printf LOG "%s\n", "y"; close LOG;
open(RD, "<", $file) or die; my @l = <RD>; print eof(RD) ? "eof " : "more "; close RD; print scalar(@l), " $l[1]";
opendir(DH, "test") or die; my @e = readdir(DH); closedir(DH); print @e > 1 ? "dir ok\n" : "dir bad\n";
pipe(R, W); print W "p\n"; close W; print scalar(<R>); close R;
unlink $file;
