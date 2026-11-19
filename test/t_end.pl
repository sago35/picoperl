# END ブロックと $?
use strict;
use warnings;
my $n = 0;
END { print "end 1: n=$n status=$?\n"; $? = 3 if $n == 2; }
END { print "end 2 runs first\n"; $n++ }
{ package EndPkg; END { print "end in package ", __PACKAGE__, "\n" } }
# INIT / CHECK / UNITCHECK: 最初の文より前に、CHECK は逆順、INIT はコンパイルした順で実行される
INIT { print "INIT 1\n" }
BEGIN { print "BEGIN\n" }
use lib 'test/lib'; use TestInit;
my $init_sees = "assigned";
INIT { print "INIT 2 sees ", (defined $init_sees ? "a value" : "undef"), "\n" }
CHECK { print "CHECK 1\n" }
CHECK { print "CHECK 2\n" }
UNITCHECK { print "UNITCHECK\n" }
print "body\n";
$n++;
sub leave { print "leaving\n"; exit 7 }
leave();
print "not reached\n";
# 注意: exit より後ろに書いた END は、perl では実行されるが picoperl では登録されない
