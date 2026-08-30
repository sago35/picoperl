# Diagnostic: one switch on GPIO2 (pull-up, pressed = low) sends KC_A.
# Copy this file to the PICOPERL drive as main.pl and watch the serial console.
use strict;
use Gpio;
use Hid;
use Usb;
use Time::HiRes qw(usleep time);
use Board;
use PicoPerl::Keycodes;

my $btn = Gpio->new(2, 'input_pullup');

print "diag: GPIO2 -> KC_A (", KC_A, ")  usb_mounted=", Usb::mounted(), " hid_ready=", Hid::ready(),
      " heap_free=", Board::heap_free() // "?", "\n";

my $prev = -1;
my $loops = 0;
my $t0 = time();
while (1) {
    my $v = $btn->read;
    if ($v != $prev) {
        $prev = $v;
        my $pressed = $v ? 0 : 1;
        my $ready = Hid::ready();
        my $sent = $pressed ? Hid::keyboard_report(0, KC_A) : Hid::keyboard_report(0);
        print "GPIO2=$v pressed=$pressed hid_ready=$ready sent=$sent\n";
    }
    $loops++;
    if (time() - $t0 >= 5) {
        printf("alive: %d loops in 5s, GPIO2=%d, hid_ready=%d, usb_mounted=%d, heap_free=%s\n",
               $loops, $btn->read, Hid::ready(), Usb::mounted(), Board::heap_free() // "?");
        $loops = 0; $t0 = time();
    }
    usleep(1000);
}
