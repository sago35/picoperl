# tinygo-keeb/workshop 10_oled_rotated in picoperl:
# use the 128x64 OLED as a 64x128 portrait display.
#
# The Go version wraps the display in a RotatedDisplay whose SetPixel(x, y) calls
# SetPixel(y, height - x) on the real display; that is a 270 degree rotation, which the
# Gfx frame buffer does for us: $display->set_rotation(270) makes size() return (64, 128).
use strict;
use Ssd1306;
use Tinyfont;

my $display = Ssd1306->new(i2c => 0, sda => 12, scl => 13, address => 0x3C, width => 128, height => 64);
$display->clear_display;
sleep 0.05;

$display->set_rotation(270);
my ($w, $h) = $display->size;
print "rotated display: ${w}x${h}\n";

my $white = { R => 0xFF, G => 0xFF, B => 0xFF, A => 0xFF };

Tinyfont::WriteLine($display, Tinyfont::FREEMONO_BOLD_9PT, 5, 10, "hello", $white);
Tinyfont::WriteLine($display, Tinyfont::GOPHERS_58PT, 10, 70, "B", $white);
Tinyfont::WriteLine($display, Tinyfont::GOPHERS_58PT, 10, 110, "H", $white);
$display->display;
