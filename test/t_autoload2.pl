use strict; use warnings;
# AUTOLOAD { ... } / DESTROY { ... } (sub を省いた形)、ブロックのあとの ::name(...)
sub ok { print "ok: @_\n" }
package AlPkg;
our $AUTOLOAD;
sub new { bless {}, shift }
AUTOLOAD {
    my $n = $AUTOLOAD; $n =~ s/.*:://;
    return if $n eq 'DESTROY';
    return "auto($n)";
}
package AlD;
sub new { bless { n => $_[1] }, $_[0] }
DESTROY { print "destroy $_[0]{n}\n" }
package main;
print AlPkg->new->hello, " ", AlPkg::other(), "\n";
{ my $d = AlD->new(7); }
if (1) { my $x = 1; }
::ok("after a block");
{
    package AlInner;
    foreach my $t (1 .. 2) { }
    ::ok("in a package", $_) for 1;
    sub helper { 1 }
    ::ok "no parens";
}
my %h = (AUTOLOAD => 1, DESTROY => 2); print "keys: $h{AUTOLOAD}$h{DESTROY}\n";
my $t = 1 ? "a" : "b"; my @l = (1) x 2; print "ternary: $t ", ($t eq "a" ? ::ok("in expr") : 0), "\n";
# strict の下でも、goto &$name と exists &$name / defined &$name は名前で指せる (呼び出しの &$name(...) は不可のまま)
sub Al2::target { "target(@_)" }
sub via_name { my $s = "Al2::target"; goto &$s }
sub via_block { goto &{"Al2::target"} }
my ($s, $none) = ("Al2::target", "Al2::none");
print via_name(1, 2), " ", via_block(3), " ", (defined(&$s) ? "def" : "undef"), " ", (exists(&$s) ? "ex" : "nex"), " ", (exists(&$none) ? "ex" : "nex"), " ", (exists(&{"Al2::target"}) ? "ex" : "nex"), "\n";
my $v = eval { &$s(3) }; print $@ =~ /strict refs/ ? "call still needs no strict\n" : "allowed\n";
