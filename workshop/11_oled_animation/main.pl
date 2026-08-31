# tinygo-keeb/workshop 11_oled_animation in picoperl:
# rotate the letters "ABCEF" through the 32pt gophers font five times a second.
# The display is configured upside down (rotation => 180) because of how the OLED
# is mounted on the zero-kb02.
#
# Go:  display.Configure(ssd1306.Config{..., Rotation: drivers.Rotation180})
# pl:  Ssd1306->new(..., rotation => 180)
use strict;
use Ssd1306;
use Tinyfont;

my $display = Ssd1306->new(i2c => 0, sda => 12, scl => 13, address => 0x3C, width => 128, height => 64, rotation => 180);
$display->clear_display;
sleep 0.05;

my $white = { R => 0xFF, G => 0xFF, B => 0xFF, A => 0xFF };
my @data = split //, "ABCEF";

while (1) {
    $display->clear_buffer;
    push @data, shift @data;                    # data[0..4] = data[1..4], data[0]
    Tinyfont::WriteLine($display, Tinyfont::GOPHERS_32PT, 5, 45, join("", @data), $white);
    $display->display;
    sleep 0.2;
}
