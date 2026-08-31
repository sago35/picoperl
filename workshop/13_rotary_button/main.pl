# tinygo-keeb/workshop 13_rotary_button in picoperl:
# the push switch of the rotary encoder is on GPIO2 (pull-up, pressed = low).
# Print "pressed" when it goes down and "released" when it comes back up.
use strict;
use Gpio;
use Time::HiRes qw(usleep);

my $btn = Gpio->new(2, 'input_pullup');

while (1) {
    if (!$btn->read) {
        print "pressed\n";
        usleep(10_000);

        # wait for the release
        while (!$btn->read) { usleep(1000) }
        print "released\n";
        usleep(10_000);
    }
    usleep(1000);
}
