use strict; use warnings;
# require したモジュールがコンパイルできないとき、理由は $@ に入る (eval で捕まえれば何も表示されない)
use lib 'test/lib';
sub lines { my @l = split /\n/, shift; s{ at \S*?(\w+\.p[ml]) line}{ at $1 line} for @l; join(" / ", @l) }
my $ok = eval { require TestBeginDie; 1 };
print "begin die: ", ($ok ? "loaded" : lines($@)), "\n";
$ok = eval { require TestUseDie; 1 };
print "nested: ", ($ok ? "loaded" : lines($@)), "\n";
$ok = eval { require TestSynErr; 1 };
print "syntax: ", ($ok ? "loaded" : ($@ =~ /syntax error/ ? "syntax error" : "?") . ($@ =~ /Compilation failed in require at \S+ line 9\./ ? " + require line" : " no chain")), "\n";
$ok = eval q{ BEGIN { die "direct\n" } 1 };
print "eval BEGIN: ", ($ok ? "ran" : lines($@)), "\n";
$ok = eval { require TestBeginDie; 1 };
print "again: ", ($ok ? "loaded" : ($@ =~ /^Attempt to reload TestBeginDie\.pm aborted/ ? "not tried again" : lines($@))), "\n";
$ok = eval { require No::Such::Module::Here; 1 };
print "missing: ", ($@ =~ /^Can't locate No\/Such\/Module\/Here\.pm in \@INC/ ? "can't locate" : $@), "\n";
print "alive\n";
