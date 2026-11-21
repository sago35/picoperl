# BEGIN の中の exit でプログラムが終わる (本体は実行されない)
use strict; use warnings;
print "not reached 1\n";
BEGIN { print "in begin\n"; }
BEGIN { print "leaving\n"; exit 0; }
print "not reached 2\n";
BEGIN { print "not reached 3\n" }
