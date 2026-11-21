use strict; use warnings;
# クロージャを自分のスコープの変数に入れても、必要な間は生きていて、要らなくなれば解放される
{ package Probe; my $live = 0; sub new { $live++; bless { n => $_[1] }, $_[0] } sub DESTROY { $live-- } sub live { $live } }
sub local_cb { my $p = Probe->new(1); my $cb = sub { $p->{n} + 1 }; $cb->() }
my $lv = local_cb(); print "local: $lv live=", Probe::live(), "\n";
sub returned { my $p = Probe->new(2); my $c = sub { $p->{n} * 10 }; return $c }
{ my $f = returned(); print "returned: ", $f->(), " live=", Probe::live(), "\n"; my $g = $f; undef $f; print "copy: ", $g->(), " live=", Probe::live(), "\n"; }
print "after drop: live=", Probe::live(), "\n";
sub counter { my $p = Probe->new(0); my %h = (inc => sub { ++$p->{n} }, get => sub { $p->{n} }); return \%h }
{ my $o = counter(); $o->{inc}->() for 1 .. 3; my $get = $o->{get}; undef $o; print "hash: ", $get->(), " live=", Probe::live(), "\n"; }
print "after hash: live=", Probe::live(), "\n";
sub pair { my $p = Probe->new(5); my $inc = sub { ++$p->{n} }; my $get = sub { $p->{n} }; return ($inc, $get) }
{ my ($i, $g) = pair(); $i->(); undef $i; print "pair: ", $g->(), " live=", Probe::live(), "\n"; }
print "after pair: live=", Probe::live(), "\n";
my @keep;
for my $i (1 .. 3) { my $p = Probe->new($i); my $cb = sub { $p->{n} * $i }; push @keep, $cb if $i != 2; }
print "loop: ", join(",", map { $_->() } @keep), " live=", Probe::live(), "\n";
@keep = (); print "after loop: live=", Probe::live(), "\n";
sub nested { my $p = Probe->new(7); my $outer = sub { my $k = shift; my $inner = sub { $p->{n} + $k }; $inner->() }; return $outer }
{ my $f = nested(); print "nested: ", $f->(1), " ", $f->(2), " live=", Probe::live(), "\n"; }
print "after nested: live=", Probe::live(), "\n";
sub shared { my @data = (1, 2, 3); my $sum = sub { my $t = 0; $t += $_ for @data; $t }; return (\@data, $sum->()) }
{ my ($d, $s) = shared(); push @$d, 4; print "shared data survives: @$d sum=$s\n"; }
print "end: live=", Probe::live(), "\n";
