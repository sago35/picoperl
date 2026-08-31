# tinygo-keeb/workshop 15_hid_mouse in picoperl:
# the encoder button (GPIO2, pull-up) works as the left mouse button.
#
# Go:  m := mouse.Port(); m.Press(mouse.Left) / m.Release(mouse.Left)
# pl:  my $m = Mouse->new;  $m->press(Mouse::LEFT) / $m->release(Mouse::LEFT)
# The Go loop sends press or release every turn; a HID report only needs to go
# out when the button state changes, so that is what this does.
use strict;
use Gpio;
use Mouse;
use Time::HiRes qw(usleep);

my $btn = Gpio->new(2, 'input_pullup');
my $m   = Mouse->new;

my $down = 0;
while (1) {
    my $pressed = $btn->read ? 0 : 1;
    if ($pressed != $down) {
        my $sent = $pressed ? $m->press(Mouse::LEFT) : $m->release(Mouse::LEFT);
        $down = $pressed if $sent;
    }
    usleep(1000);
}
