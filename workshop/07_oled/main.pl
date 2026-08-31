# tinygo-keeb/workshop 07_oled in picoperl:
# fill the SSD1306 OLED (I2C0, SDA GPIO12, SCL GPIO13, address 0x3C) with 2x2 blocks,
# white then black, refreshing the screen after every block like the Go version.
#
# Go:  display := ssd1306.NewI2C(machine.I2C0); display.Configure(...); display.SetPixel(x, y, c); display.Display()
# pl:  my $display = Ssd1306->new(...);                          $display->set_pixel($x, $y, $c); $display->display
use strict;
use Ssd1306;

my $display = Ssd1306->new(i2c => 0, sda => 12, scl => 13, address => 0x3C, width => 128, height => 64);
$display->clear_display;
sleep 0.05;

my $white = { R => 0xFF, G => 0xFF, B => 0xFF, A => 0xFF };
my $black = { R => 0x00, G => 0x00, B => 0x00, A => 0xFF };

my $cnt = 0;
while (1) {
    my $c = $cnt == 1 ? $black : $white;
    for (my $x = 0; $x < 128; $x += 2) {
        for (my $y = 0; $y < 64; $y += 2) {
            $display->set_pixel($x + 0, $y + 0, $c);
            $display->set_pixel($x + 0, $y + 1, $c);
            $display->set_pixel($x + 1, $y + 0, $c);
            $display->set_pixel($x + 1, $y + 1, $c);
            $display->display;
        }
    }
    $cnt++;
}
