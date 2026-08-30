package Fs;
# Files on the PICOPERL drive.
#   use Fs;  my $s = Fs::read("/notes.txt");  Fs::write("/notes.txt", $s);  Fs::mounted()
use strict;
sub read    { return defined &Fs::_read ? Fs::_read($_[0]) : undef }
sub write   { return defined &Fs::_write ? Fs::_write($_[0], $_[1]) : 0 }
sub mounted { return defined &Fs::_mounted ? Fs::_mounted() : 0 }
1;
