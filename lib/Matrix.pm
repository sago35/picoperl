package Matrix;
# Key matrix scanning (one pass in C).
#   use Matrix;
#   my $keys = Matrix::scan(\@drive_pins, \@read_pins, $active_high, $settle_us);
#   # -> array ref of 0/1, one per (drive, read) pair in drive-major order
use strict;
sub scan { return Matrix::_scan(@_) }
sub available { return defined &Matrix::_scan ? 1 : 0 }
1;
