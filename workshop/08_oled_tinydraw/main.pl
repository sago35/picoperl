# tinygo-keeb/workshop 08_oled_tinydraw in picoperl:
# draw a rectangle, a filled circle and a triangle on the OLED, then clear, forever.
#
# Go:  tinydraw.Rectangle(display, 10, 20, 30, 40, white); display.Display()
# pl:  Tinydraw::Rectangle($display, 10, 20, 30, 40, $white); $display->display;
use strict;
use Ssd1306;
use Tinydraw;

my $display = Ssd1306->new(i2c => 0, sda => 12, scl => 13, address => 0x3C, width => 128, height => 64);
$display->clear_display;
sleep 0.05;

my $white = { R => 0xFF, G => 0xFF, B => 0xFF, A => 0xFF };

while (1) {
    Tinydraw::Rectangle($display, 10, 20, 30, 40, $white);
    $display->display;
    sleep 0.5;
    Tinydraw::FilledCircle($display, 60, 50, 10, $white);
    $display->display;
    sleep 0.5;
    Tinydraw::Triangle($display, 100, 10, 80, 40, 60, 10, $white);
    $display->display;
    sleep 0.5;
    $display->clear_display;
    sleep 0.5;
}
