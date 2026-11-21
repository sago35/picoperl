use strict; use warnings;
# \${"name"} と \$$ref は変数そのものを指す (tie ${"Pkg::name"} が効く)
our ($FOO, $BAR, $T1) = ("f", "b");
{ no strict 'refs';
  my $r = \${"main::FOO"}; $$r = 5; print "symbolic: $FOO ", ($r == \$FOO ? "same" : "different"), "\n";
  my $n = "BAR"; my $q = \$$n; $$q .= "!"; print "by name: $BAR\n";
  my $u = \${"SrPkg::made"}; $$u = "new"; print "created: $SrPkg::made ", defined $SrPkg::made ? 1 : 0, "\n"; }
my $x = 1; my $rx = \$x; my $rr = \$$rx; $$rr = 2; print "ref of deref: $x ", ($rr == $rx ? "same" : "different"), "\n";
my $g = \${*FOO}; $$g = "glob"; print "glob: $FOO\n";
{ package SrTie; sub TIESCALAR { my $k = $_[1]; bless \$k } sub FETCH { "<" . ($ENV{${$_[0]}} // "undef") . ">" } sub STORE { $ENV{${$_[0]}} = $_[1] } }
$ENV{PP_SR_TEST} = "foo";
{ no strict 'refs'; tie ${"main::T1"}, 'SrTie', "PP_SR_TEST"; }
print "tied: $T1\n"; $T1 = "bar"; print "stored: $ENV{PP_SR_TEST} $T1\n";
{ no strict 'refs'; print "tied(): ", (ref tied(${"main::T1"})), "\n"; untie ${"main::T1"}; } print "untied: ", defined $T1 ? "def" : "undef", "\n";
my $e = eval { my $bad = \${"NoStrictHere"}; 1 } ? "allowed" : "strict refs";
print "$e\n";
