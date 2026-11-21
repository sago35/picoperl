use strict; use warnings;
use Fcntl; use Fcntl qw(:flock :seek :mode); use Fcntl qw(F_GETFL SEEK_END);
print join(",", O_RDONLY, O_WRONLY, O_RDWR, O_CREAT | O_EXCL, O_APPEND + 1, O_TRUNC, O_NONBLOCK), "\n";
print join(",", LOCK_SH, LOCK_EX | LOCK_NB, LOCK_UN, SEEK_SET, SEEK_CUR, SEEK_END, F_GETFL, F_SETFD, FD_CLOEXEC), "\n";
print join(",", S_IRUSR | S_IWUSR, S_IRWXU, S_IFMT, S_IFMT(0100644), S_IMODE(0100644), S_IFDIR, S_IFREG), "\n";
print join(",", map { $_ ? 1 : 0 } S_ISDIR(040755), S_ISREG(040755), S_ISREG(0100644), S_ISLNK(0120777)), "\n";
print Fcntl::O_CREAT, " ", Fcntl::LOCK_EX(), " ", scalar(@Fcntl::EXPORT) > 30 ? "exports" : "few", "\n";
open(my $fh, '<', $0) or die; seek($fh, -5, SEEK_END); my $tail = <$fh>; seek($fh, 4, SEEK_SET); read($fh, my $b, 6); close $fh; print length($tail), " [$b]\n";
#END
