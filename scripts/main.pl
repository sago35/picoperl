# picoperl keyboard: this file is /main.pl on the PICOPERL USB drive.
# Edit it (or the modules under /lib) and save; the board reloads automatically.
#
# Configured for zero-kb02 (TinyGo Keeb Tour, Waveshare RP2040-Zero):
#   12 keys in a 3x4 matrix: columns GPIO5-8 are driven high one at a time,
#   rows GPIO9-11 are read with pull-downs (active high).
#   Rotary encoder on GPIO3/GPIO4, its push button on GPIO2 (pull-up).
#   (The joystick on GPIO28/29 needs ADC, which picoperl does not expose yet.)
#
#   physical layout       key numbers
#   [sw1 ][sw2 ][sw3 ][sw4 ]   row 0
#   [sw5 ][sw6 ][sw7 ][sw8 ]   row 1
#   [sw9 ][sw10][sw11][sw12]   row 2
use strict;
use PicoPerl::Keycodes;
use PicoPerl::Keyboard;

keyboard_run(
    rows   => [9, 10, 11],
    cols   => [5, 6, 7, 8],
    diode  => 'col2row',      # columns are driven, rows are read
    active => 'high',         # driven column goes high, rows use pull-downs

    keymap => [
        # layer 0: numpad-like macropad
        [ [ KC_7, KC_8, KC_9, KC_BSPC  ],
          [ KC_4, KC_5, KC_6, KC_ENTER ],
          [ KC_1, KC_2, KC_3, MO(1)    ] ],
        # layer 1 (while sw12 is held)
        [ [ KC_A,    KC_B,    KC_C,    KC_ESC     ],
          [ KC_LEFT, KC_DOWN, KC_RIGHT, KC_TAB    ],
          [ KC_0,    KC_DOT,  KC_BOOTSEL, KC_TRNS ] ],
    ],

    # rotary encoder: volume up/down, push = mute (consumer control media keys)
    encoder     => { a => 3, b => 4, cw => KC_VOLU, ccw => KC_VOLD, precision => 4 },
    direct_keys => { 2 => KC_MUTE },

    debounce_ms => 5,
    on_report => sub {
        my ($mod, @keys) = @_;
        printf("report mod=%02x keys=%s\n", $mod, join(" ", map { sprintf("%02x", $_) } @keys));
    },
);
