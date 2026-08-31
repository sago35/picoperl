# tinygo-keeb/workshop 20_rotary_gopher in picoperl:
# the alphabet in the 32pt gophers font scrolls across the OLED as the rotary
# encoder (GPIO3 / GPIO4) turns.
#
# The text is longer than the display, so it is drawn three times, one string
# width apart, and the offset wraps with `%` so it scrolls forever. The Go
# version hard-codes the width (598) after measuring it; here LineWidth measures
# it. The OLED is mounted upside down on the zero-kb02, hence rotation 180.
use strict;
use Ssd1306;
use Encoder;
use Tinyfont;
use Time::HiRes qw(usleep);

my $alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
my $alphabet_width = Tinyfont::LineWidth(Tinyfont::GOPHERS_32PT, $alphabet);   # 598 in Go

my $enc = Encoder->new(3, 4, precision => 4);

my $display = Ssd1306->new(i2c => 0, sda => 12, scl => 13, address => 0x3C,
                           width => 128, height => 64, rotation => 180);
$display->clear_display;
sleep 0.05;

my $white = { R => 0xFF, G => 0xFF, B => 0xFF, A => 0xFF };
Tinyfont::WriteLine($display, Tinyfont::GOPHERS_32PT, 0, 50, $alphabet, $white);
$display->display;

my $old = 0;
while (1) {
    my $new = $enc->position % $alphabet_width;   # Perl's % is never negative, so 0 .. width-1
    if ($new != $old) {
        $display->clear_buffer;
        $old = $new;

        print "value:  $new\n";

        Tinyfont::WriteLine($display, Tinyfont::GOPHERS_32PT, $old - $alphabet_width, 50, $alphabet, $white);
        Tinyfont::WriteLine($display, Tinyfont::GOPHERS_32PT, $old,                   50, $alphabet, $white);
        Tinyfont::WriteLine($display, Tinyfont::GOPHERS_32PT, $old + $alphabet_width, 50, $alphabet, $white);
        $display->display;
    }
    usleep(10_000);
}
