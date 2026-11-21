use strict; use warnings;
# caller は eval { } を 1 段として数える
sub show { my $n = shift; my @c = caller($n); @c ? "$c[3]\@$c[2]" : "none" }
sub inner { join(" ", map { show($_ + 1) } 0 .. 3) }
sub outer { my $r = eval { inner() }; $r }
sub deep { eval { eval { inner() } } }
#line 100
print outer(), "\n";
print deep(), "\n";
sub in_eval { my @c = eval { caller(0) }; "$c[3] line $c[2]" }
print in_eval(), "\n";
sub wa { my $x = eval { (caller(0))[5] }; my @y = eval { (caller(0))[5] }; (defined $x ? "s:[$x]" : "s:undef") . " l:[$y[0]]" } print wa(), "\n";
sub sh { my $self = shift; eval { my $a = shift; $_[0] = "changed:$a"; 1 }; } my $v = "orig"; sh("self", "arg", $v); print "$v\n";
print "top: ", (eval { join(",", map { defined $_ ? $_ : "u" } (caller(0))[0, 2, 3]) }), "\n";
sub w { my $s = eval q{ join(",", map { (caller($_))[3] // "-" } 0 .. 2) }; $s } print w(), "\n"; print eval(q{ (caller(0))[3] }), " ", (eval q{ eval { (caller(1))[3] } }), "\n";
