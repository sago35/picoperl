package Hid;
# USB HID: boot keyboard + consumer control (media keys).
#
#   use Hid;
#   Hid::keyboard_report(0x02, 0x04);      # modifier byte, up to 6 keycodes (Shift + a)
#   Hid::keyboard_report(0);               # release everything
#   Hid::consumer_report(0xE2);            # mute (usage page 0x0C); 0 = release
#   Hid::ready(), Hid::consumer_ready()    # the USB stack can take a report
#   Hid::leds()                            # bit0 NumLock, bit1 CapsLock, bit2 ScrollLock
use strict;
use Exporter 'import';
our @EXPORT_OK = qw(keyboard_report consumer_report ready consumer_ready leds);

sub ready           { return Hid::_ready() }
sub leds            { return Hid::_leds() }
sub keyboard_report { return Hid::_keyboard_report(@_) }
sub consumer_ready  { return defined &Hid::_consumer_ready ? Hid::_consumer_ready() : 0 }
sub consumer_report { return defined &Hid::_consumer_report ? Hid::_consumer_report(@_) : 0 }
sub has_consumer    { return defined &Hid::_consumer_report ? 1 : 0 }

1;
