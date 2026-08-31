# tinygo-keeb/workshop 09_oled_tinyfont in picoperl:
# write "hello" in FreeMono Bold 9pt and "ABCEF" in the 32pt gophers font.
#
# Go:  tinyfont.WriteLine(display, &freemono.Bold9pt7b, 5, 10, "hello", white)
# pl:  Tinyfont::WriteLine($display, Tinyfont::FREEMONO_BOLD_9PT, 5, 10, "hello", $white);
# (x, y) is the baseline of the text, like tinyfont.
use strict;
use Ssd1306;
use Tinyfont;

my $display = Ssd1306->new(i2c => 0, sda => 12, scl => 13, address => 0x3C, width => 128, height => 64);
$display->clear_display;
sleep 0.05;

my $white = { R => 0xFF, G => 0xFF, B => 0xFF, A => 0xFF };

Tinyfont::WriteLine($display, Tinyfont::FREEMONO_BOLD_9PT, 5, 10, "hello", $white);
Tinyfont::WriteLine($display, Tinyfont::GOPHERS_32PT, 5, 50, "ABCEF", $white);
$display->display;
