# tinygo-keeb/workshop 03_usbcdc-serial in picoperl:
# print "hello world!" on the USB serial console once a second.
# (print/printf go to the CDC port; open it with any terminal at any baud rate)
use strict;

while (1) {
    print "hello world!\n";
    sleep 1;
}
