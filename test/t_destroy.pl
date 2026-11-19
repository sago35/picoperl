# DESTROY: 最後の参照が消えたオブジェクトのメソッドが呼ばれるタイミングを本物の perl と突き合わせる
use strict;
package Guard; my $count = 0;
sub new { my ($c, $n) = @_; print "new $n\n"; bless { n => $n }, $c }
sub DESTROY { my $s = shift; $count++; print "DESTROY $s->{n}\n" }
package Child; our @ISA = ('Guard');
package Plain; sub new { bless {}, shift }
package main;
{ my $a = Guard->new("scoped"); print "in scope\n"; }
print "after scope\n";
my $b = Guard->new("undef'd"); undef $b; print "after undef\n";
my $c = Guard->new("reassigned"); $c = Guard->new("second"); print "after reassign\n";
sub make { my $t = Guard->new("in sub"); return 5 } my $r = make(); print "after sub $r\n";
my $d = Guard->new("two refs"); my $d2 = $d; undef $d; print "still alive\n"; undef $d2; print "now gone\n";
my @list = map { Guard->new("item$_") } 1 .. 2; @list = (); print "list cleared\n";
my %h = (k => Child->new("inherited")); delete $h{k}; print "deleted\n";
{ my $p = Plain->new; } print "plain ok\n";
my $holder = { inner => Guard->new("nested") }; undef $holder; print "nested done\n";
sub tmp { Guard->new("temporary")->{n} } print "got ", tmp(), "\n";
{ package Dies; sub new { bless {}, shift } sub DESTROY { die "oops in destroy\n" } }
{ local $SIG{__WARN__} = sub { print "warned: $_[0]" }; my $x = Dies->new; undef $x; print "survived\n"; }
my $last = Guard->new("at exit");
print "count so far $count\n";
END { print "END block\n" }
# DESTROY の中で自分をどこかに残す (復活)、DESTROY の中でほかのオブジェクトを捨てる
{ package Keep; our @saved; sub new { bless { n => $_[1] }, $_[0] } sub DESTROY { push @saved, $_[0] if @saved < 1; print "Keep::DESTROY $_[0]{n}\n" } }
{ my $k = Keep->new("kept"); } print "saved: ", scalar(@Keep::saved), " $Keep::saved[0]{n}\n"; @Keep::saved = (); print "dropped again\n";
{ package Outer; sub new { bless { inner => Guard->new("inner of outer") }, shift } sub DESTROY { print "Outer::DESTROY\n"; delete $_[0]{inner}; print "Outer done\n" } }
{ my $o = Outer->new; } print "outer gone\n";
