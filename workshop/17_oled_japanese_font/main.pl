# 17_oled_japanese_font: OLED に日本語 (東雲フォント 12px) と gophers フォントを表示
use strict;
use warnings;
use Ssd1306;
use Tinyfont;
use Time::HiRes qw(usleep);

my $display = Ssd1306->new(sda => 12, scl => 13, address => 0x3C,
                           width => 128, height => 64, rotation => 180);
$display->clear_display;
usleep(50_000);

Tinyfont::WriteLine($display, Tinyfont::SHNMK12, 5, 10, "こんにちは世界", 1);
Tinyfont::WriteLine($display, Tinyfont::GOPHERS_32PT, 5, 50, "ABCEF", 1);
$display->display;

while (1) { sleep 1 }
