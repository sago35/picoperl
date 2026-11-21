# 名前表で取り込んだ名前が見える、(&) だけのプロトタイプ
use strict; use warnings;
use List::Util qw(sum max);
sub local_one { 1 }
our $pv = 5; our @pa = (1);
no strict 'refs';
my $stash = \%{"main::"};
for my $n (qw(sum max local_one nope pv pa)) {
    my $in = exists $stash->{$n} ? 1 : 0;
    my $glob = \*{"main::$n"};
    my $code = defined(*{$glob}{CODE}) ? "code" : "nocode";
    print "$n: stash=$in $code\n";
}
sub dies(&) { my $c = shift; eval { $c->(); 1 } ? undef : $@ }
sub chk { scalar(@_) . ":" . join("|", map { defined $_ ? (ref $_ || $_) : "undef" } @_) }
print chk(dies { die "boom\n" }, qr/boom/, "name"), "\n";
print chk((dies { 1 }), "after"), "\n";
