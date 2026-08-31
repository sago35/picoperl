package Tinydraw;
# tinygo.org/x/drivers/tinydraw-style functions on a Gfx / Ssd1306 display.
#   use Tinydraw;
#   Tinydraw::Rectangle($display, $x, $y, $w, $h, $color);   Tinydraw::FilledRectangle(...)
#   Tinydraw::Circle($display, $x, $y, $r, $color);          Tinydraw::FilledCircle(...)
#   Tinydraw::Triangle($display, $x0,$y0, $x1,$y1, $x2,$y2, $color);  Tinydraw::FilledTriangle(...)
#   Tinydraw::Line($display, $x0, $y0, $x1, $y1, $color)
use strict;
use Exporter 'import';
our @EXPORT_OK = qw(Line Rectangle FilledRectangle Circle FilledCircle Triangle FilledTriangle);

sub Line            { my $d = shift; $d->line(@_) }
sub Rectangle       { my $d = shift; $d->rectangle(@_) }
sub FilledRectangle { my $d = shift; $d->filled_rectangle(@_) }
sub Circle          { my $d = shift; $d->circle(@_) }
sub FilledCircle    { my $d = shift; $d->filled_circle(@_) }
sub Triangle        { my $d = shift; $d->triangle(@_) }
sub FilledTriangle  { my $d = shift; $d->filled_triangle(@_) }

1;
