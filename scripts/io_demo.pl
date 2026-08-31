# File I/O demo (read side): reads /main.pl -- this very file -- from the drive.
#
#   open my $fh, '<', $path    3-argument open, lexical handle
#   <$fh>                      one line (undef at EOF); all lines in list context
#   while (<$fh>)              reads into $_ like perl
#   eof / seek / tell / close
use strict;
use warnings;

open(my $fh, '<', 'main.pl') or die "open main.pl: $!\n";

print "-- first 3 lines of main.pl --\n";
for my $i (1 .. 3) {
    my $l = <$fh>;
    last unless defined $l;
    print "$i: $l";
}
print "-- tell() is now ", tell($fh), "\n";

my ($lines, $uses) = (3, 0);
while (<$fh>) {
    $lines++;
    $uses++ if /^\s*use\s+/;
}
print "-- $lines lines, ", tell($fh), " bytes, $uses 'use' statements, eof=", (eof($fh) ? 1 : 0), "\n";

seek($fh, 0, 0) or die "seek: $!";
my $first = <$fh>;
print "-- after seek(0,0): $first";
close($fh) or die "close: $!";

my $missing = open(my $nope, '<', 'no_such_file.pl');
print "-- open of a missing file: ", (defined $missing ? "?!" : "fails, \$! = $!"), "\n";
print "io demo done\n";
