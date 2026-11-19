package TestInit;
# INIT / CHECK ブロックを持つモジュール (t_end.pl 用)
INIT  { print "module INIT\n" }
CHECK { print "module CHECK\n" }
print "module body\n";
1;
