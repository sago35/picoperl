package Tinyfont;
# tinygo.org/x/tinyfont-style text on a Gfx / Ssd1306 display. Fonts are built into the
# firmware (Adafruit GFX glyph format): 'classic' (5x7), 'freemono_bold_9pt',
# 'gophers_32pt', 'gophers_58pt' (gophers: CC BY-NC 4.0, tinygo.org).
#   use Tinyfont;
#   Tinyfont::WriteLine($display, Tinyfont::FREEMONO_BOLD_9PT, $x, $y, "hello", $white);   # (x, y) = baseline
#   Tinyfont::LineWidth(Tinyfont::CLASSIC, "hello")
use strict;
use Gfx;
use Exporter 'import';
our @EXPORT_OK = qw(WriteLine LineWidth);

use constant {
    CLASSIC          => 'classic',
    FREEMONO_BOLD_9PT => 'freemono_bold_9pt',
    GOPHERS_32PT     => 'gophers_32pt',
    GOPHERS_58PT     => 'gophers_58pt',
    SHNMK12          => 'shnmk12',       # 東雲 12px (ASCII + かな + JIS 第 1/2 水準漢字)
};

sub WriteLine { my ($d, $font, $x, $y, $text, $color, $scale) = @_; return $d->write_line($font, $x, $y, $text, $color, $scale) }
sub LineWidth { my ($font, $text, $scale) = @_; return Gfx::text_width($font, $text, $scale) }
sub Fonts     { return Gfx::fonts() }

1;
