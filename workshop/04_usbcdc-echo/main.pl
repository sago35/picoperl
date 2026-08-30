# tinygo-keeb/workshop 04_usbcdc-echo in picoperl:
# read lines from the USB serial console and echo them back.
# <STDIN> blocks until Enter (CR, LF or CR LF); typed characters are echoed.
use strict;

while (defined(my $line = <STDIN>)) {
    chomp $line;
    print "you typed : $line\n";
}
