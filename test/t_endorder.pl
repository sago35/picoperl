use strict; use warnings;
# END ブロックはコンパイルされた順の逆に実行する (モジュールの END も含めて)
use lib 'test/lib';
my @list;
END { print "first END (runs last): @list\n" }
END { foreach (@list) { print "item $_\n" } }
use TestEndMod;
push @list, 1, 2;
END { print "third END (runs first)\n" }
print "body\n";
