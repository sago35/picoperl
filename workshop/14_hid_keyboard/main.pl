# tinygo-keeb/workshop 14_hid_keyboard in picoperl:
# while the encoder button (GPIO2, pull-up) is held, the board types "a".
#
# Go:  kb.Down(keyboard.KeyA) / kb.Up(keyboard.KeyA)
# pl:  Hid::keyboard_report(0, KC_A) / Hid::keyboard_report(0)
# A report lists the modifier byte and the keys that are down right now, so
# "up" is a report with no keys. Only send when the state changes; sending the
# same report over and over would just flood the USB stack.
use strict;
use Gpio;
use Hid;
use Time::HiRes qw(usleep);
use PicoPerl::Keycodes qw(KC_A);

my $btn = Gpio->new(2, 'input_pullup');

my $down = 0;
while (1) {
    my $pressed = $btn->read ? 0 : 1;
    if ($pressed != $down) {
        if (Hid::ready()) {
            $pressed ? Hid::keyboard_report(0, KC_A) : Hid::keyboard_report(0);
            $down = $pressed;
        }
    }
    usleep(1000);
}
