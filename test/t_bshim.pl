use strict; use warnings; use B ();
sub named {
    my $x = 1;
    return $x;
}
{ package Foo::Bar; sub meth { 1 } }
my $an = sub { 2 };
for my $c (\&named, \&Foo::Bar::meth, $an) {
    my $cv = B::svref_2object($c);
    my @lines; my $op = $cv->START; while ($op) { push @lines, $op->line if $op->can('line'); last unless $op->can('next'); $op = $op->next }
    @lines = sort { $a <=> $b } @lines;
    print join(" ", ref($cv), $cv->GV->NAME, $cv->GV->STASH->NAME, ($cv->FILE =~ m{([^/]+)$})[0], "$lines[0]-$lines[-1]", ($cv->isa('B::CV') ? "isa" : "not"), ($cv->GV->isa('B::SPECIAL') ? "special" : "gv")), "\n";
}
print B::perlstring('a"b$c@d\\'), " ", B::perlstring("x\ny"), " ", B::cstring("q\"\n"), "\n";
