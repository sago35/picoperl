# tinygo-keeb/workshop 01_blinky1 in picoperl:
# blink the RGB LED on the RP2040-Zero (WS2812B on GPIO16).
#
# Go:  ws := NewWS2812B(machine.GPIO16); ws.PutColor(white); time.Sleep(500 * time.Millisecond)
# pl:  my $ws = Ws2812->new(16);         $ws->put_color($white); sleep 0.5;
use strict;
use Ws2812;

my $white = { R => 0x20, G => 0x20, B => 0x20, A => 0x00 };
my $black = { R => 0x00, G => 0x00, B => 0x00, A => 0x00 };

my $ws = Ws2812->new(16);
$ws->put_color($white);

while (1) {
    sleep 0.5;
    $ws->put_color($black);
    sleep 0.5;
    $ws->put_color($white);
}
