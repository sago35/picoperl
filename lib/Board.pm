package Board;
# The board itself.
#   use Board;
#   Board::led(1);            # on-board LED (no-op on boards without one, e.g. RP2040-Zero)
#   Board::reboot_bootsel();  # restart into the UF2 bootloader
#   Board::reboot();          # plain restart
#   Board::NAME, Board::LED_PIN
use strict;
sub led            { return Board::_led($_[0] ? 1 : 0) }
sub reboot_bootsel { return Board::_reboot_bootsel() }
sub reboot         { return defined &Board::_reboot ? Board::_reboot() : Board::_reboot_bootsel() }
sub heap_free      { return defined &Board::_heap_free ? Board::_heap_free() : undef }   # bytes of RAM still free
1;
