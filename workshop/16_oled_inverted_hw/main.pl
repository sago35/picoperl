# tinygo-keeb/workshop 16_oled_inverted_hw in picoperl:
# same picture as 09, but with the display turned by 180 degrees for the zero-kb02.
#
# The Go version calls display.SetRotation(drivers.Rotation180) after Configure; the
# driver then flips the panel in hardware (SEGREMAP / COMSCANDEC). picoperl rotates in
# the frame buffer instead, which gives the same picture: $display->set_rotation(180).
use strict;
use Ssd1306;
use Tinyfont;

my $display = Ssd1306->new(i2c => 0, sda => 12, scl => 13, address => 0x3C, width => 128, height => 64);
$display->set_rotation(180);
$display->clear_display;
sleep 0.05;

my $white = { R => 0xFF, G => 0xFF, B => 0xFF, A => 0xFF };

Tinyfont::WriteLine($display, Tinyfont::FREEMONO_BOLD_9PT, 5, 10, "hello", $white);
Tinyfont::WriteLine($display, Tinyfont::GOPHERS_32PT, 5, 50, "ABCEF", $white);
$display->display;
