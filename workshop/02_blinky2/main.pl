# tinygo-keeb/workshop 02_blinky2 in picoperl:
# light the 12 key LEDs (SK6812MINI-E on GPIO1) one after another.
#
# write_raw() takes one 32-bit value per LED: Green / Red / Blue from the most
# significant byte (0xGGRRBBxx), like WriteRaw() in the Go version.
#   0xFFFFFFFF white, 0xFF0000FF green, 0x00FF00FF red, 0x0000FFFF blue
use strict;
use Ws2812;

my $ws = Ws2812->new(1);

my @colors = (
    [
        0xFFFFFFFF, 0xFF0000FF, 0x00FF00FF, 0x0000FFFF,
        0xFFFFFFFF, 0xFF0000FF, 0x00FF00FF, 0x0000FFFF,
        0xFFFFFFFF, 0xFF0000FF, 0x00FF00FF, 0x0000FFFF,
    ],
    [
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000000, 0x00000000,
    ],
);

while (1) {
    for my $i (0 .. $#{ $colors[0] }) {
        sleep 0.1;
        $ws->write_raw(@{ $colors[0] }[0 .. $i]);   # colors[0][:i+1]
    }
    sleep 0.5;
    $ws->write_raw(@{ $colors[1] });
}
