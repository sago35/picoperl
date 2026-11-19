# END ブロックと $?
use strict;
use warnings;
my $n = 0;
END { print "end 1: n=$n status=$?\n"; $? = 3 if $n == 2; }
END { print "end 2 runs first\n"; $n++ }
{ package EndPkg; END { print "end in package ", __PACKAGE__, "\n" } }
print "body\n";
$n++;
sub leave { print "leaving\n"; exit 7 }
leave();
print "not reached\n";
# 注意: exit より後ろに書いた END は、perl では実行されるが picoperl では登録されない
