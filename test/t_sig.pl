use strict;
use warnings;
use feature 'signatures';

sub add($x, $y) { return $x + $y }
print add(2, 3), "\n";

sub greet($name, $greeting = "hello", $punct = "!") { return "$greeting, $name$punct" }
print greet("perl"), "\n";
print greet("perl", "hi"), "\n";
print greet("perl", "hi", "?"), "\n";

sub dflt($x, $y = $x * 10) { return "$x/$y" }      # a default can use earlier parameters
print dflt(3), " ", dflt(3, 4), "\n";

sub dor($x, $y //= 42) { return $y }
print dor(1, undef), " ", dor(1, 0), " ", dor(1), "\n";

sub lor2($x, $y ||= 7) { return $y }
print lor2(1, 0), " ", lor2(1, 5), "\n";

sub rest($first, @others) { return "$first [@others]" }
print rest(1, 2, 3, 4), "\n";
print rest(1), "\n";

sub opts($n, %o) { return join(",", map { "$_=$o{$_}" } sort keys %o) . ":$n" }
print opts(9, b => 2, a => 1), "\n";

my $mul = sub ($m, $k) { $m * $k };
print $mul->(6, 7), "\n";

package Counter;
sub new($class, $start = 0) { return bless { n => $start }, $class }
sub add($self, $d = 1) { $self->{n} += $d; return $self->{n} }
package main;
my $c = Counter->new(10);
print $c->add, " ", $c->add(5), "\n";

# arity errors: the messages match perl's
eval { add(1) };         print $@;
eval { add(1, 2, 3) };   print $@;
eval { greet() };        print $@;
eval { $mul->(1) };      print $@;
eval { opts(1, "odd") }; print $@;

# closures over signature parameters
sub make_adder($n) { return sub ($x) { return $x + $n } }
my $a5 = make_adder(5);
print $a5->(37), "\n";
