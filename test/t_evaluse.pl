use strict; use warnings;
# use / no は、セミコロンなしでコードやブロックの終わりに来てもよい (eval "use strict")
my @r;
push @r, (eval "use strict; 1" ? "a" : "A:$@");
eval "use strict"; push @r, ($@ eq "" ? "b" : "B:$@");
eval "package EvUse; use vars qw(\$x \@y)"; push @r, ($@ eq "" ? "c" : "C:$@");
eval "no warnings"; push @r, ($@ eq "" ? "d" : "D:$@");
eval "use constant EVUSE_K => 5"; push @r, ($@ eq "" ? "e" . main->can("EVUSE_K")->() : "E:$@");
eval "use 5.006"; push @r, ($@ eq "" ? "f" : "F:$@");
eval "use lib 'test/lib'; use TestMod"; push @r, ($@ eq "" ? "g" : "G:$@");
eval "use NoSuchModuleHere"; push @r, ($@ =~ /^Can't locate NoSuchModuleHere/ ? "h" : "H:$@");
{ use integer } { no strict 'refs' }
sub blk { use integer; 7 / 2 } push @r, "i" . blk();
eval "{ use strict }"; push @r, ($@ eq "" ? "j" : "J:$@");
{ no strict 'vars'; $EvUse::x = 3; push @r, "k$EvUse::x"; }
print "@r\n";
