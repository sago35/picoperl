use strict;
use warnings;

# $SIG{__WARN__}: receives warn() and runtime warnings, with the location appended
my @w;
$SIG{__WARN__} = sub { push @w, $_[0] };
warn "plain\n";
my $u;
my $v = $u + 1;
print scalar(@w), " warnings\n";
print $w[0];
print $w[1] =~ /^Use of uninitialized value \$u in addition/ ? "uninit hooked\n" : "NG: $w[1]";
delete $SIG{__WARN__};

# $SIG{__DIE__}: observes the die (also inside eval); dying in the hook replaces the error
my @seen;
$SIG{__DIE__} = sub { push @seen, $_[0] };
eval { die "boom\n" };
print "caught: $@";
print "hook saw: $seen[0]";
$SIG{__DIE__} = sub { die "replaced\n" };
eval { die "orig\n" };
print "caught2: $@";
delete $SIG{__DIE__};

# alarm + $SIG{ALRM}: fires while a loop is running; alarm(0) reports what is left
my $fired = 0;
$SIG{ALRM} = sub { $fired = 1 };
alarm 1;
my $spin = 0;
while (!$fired) { $spin++ }
print "alarm fired\n";
print alarm(0), "\n";
alarm 100;
my $rest = alarm 0;
print $rest >= 99 ? "remaining ok\n" : "NG: $rest\n";
