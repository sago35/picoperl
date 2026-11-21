use strict; use warnings;
use Errno qw(ENOENT EACCES EEXIST :POSIX);
open(my $f, '<', '/no/such/file/here') or print "open failed: ", ($! == ENOENT ? "ENOENT" : "other"), " ", ($!{ENOENT} ? "hash true" : "hash false"), " ", ($!{EACCES} ? "eacces" : "not eacces"), "\n";
print ENOENT, " ", EACCES, " ", EEXIST, " ", Errno::EINTR(), " ", EAGAIN, " ", EWOULDBLOCK, " ", Errno::ENOSYS, "\n";
print "exists: ", (exists $!{EPERM} ? 1 : 0), (exists $!{NOT_AN_ERROR} ? 1 : 0), " unknown: [", $!{NOT_AN_ERROR}, "]\n";
$! = EEXIST; print "set: ", ($!{EEXIST} ? "yes" : "no"), " ", 0 + $!, "\n";
my @k = keys %!; print "keys: ", (@k > 100 ? "many" : scalar(@k)), " ", (grep({ $_ eq "ENOTDIR" } @k) ? "has ENOTDIR" : "missing"), "\n";
print eval { $!{EPERM} = 1; 1 } ? "stored?" : "read only", "\n";
my @t = times; print "times: ", scalar(@t), " ", ($t[0] >= 0 ? "ok" : "neg"), "\n";
