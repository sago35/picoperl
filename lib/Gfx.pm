package Gfx;
# Monochrome frame buffer with drawing primitives (implemented in C). Ssd1306 inherits from it.
#   my $g = Gfx->new(width => 128, height => 64);
#   $g->set_pixel($x, $y, $color); $g->get_pixel($x, $y); $g->clear_buffer; ($w, $h) = $g->size;
#   $g->set_rotation(0|90|180|270); $g->line/rectangle/filled_rectangle/circle/filled_circle/triangle/filled_triangle
#   $g->write_line($font, $x, $y, $text, $color, $scale); Gfx::text_width($font, $text); Gfx::fonts()
use strict;

sub new {
    my ($class, %o) = @_;
    my $h = Gfx::_new($o{width} // 128, $o{height} // 64);
    return bless { handle => $h, width => $o{width} // 128, height => $o{height} // 64 }, $class;
}
# colors are tinygo-style {R,G,B,A} hashes or plain 0/1
sub on { my $c = shift; return 1 unless defined $c; return ref $c eq 'HASH' ? (($c->{R} || $c->{G} || $c->{B}) ? 1 : 0) : ($c ? 1 : 0) }

sub size          { my $self = shift; return (Gfx::_width($self->{handle}), Gfx::_height($self->{handle})) }
sub set_rotation  { my ($self, $deg) = @_; Gfx::_rotation($self->{handle}, int(($deg // 0) / 90) & 3); return $self }
sub rotation      { return Gfx::_rotation($_[0]->{handle}) * 90 }
sub clear_buffer  { Gfx::_clear($_[0]->{handle}); return $_[0] }
sub set_pixel     { my ($s, $x, $y, $c) = @_; Gfx::_pixel($s->{handle}, $x, $y, on($c)); return $s }
sub get_pixel     { my ($s, $x, $y) = @_; return Gfx::_get_pixel($s->{handle}, $x, $y) }
sub line          { my ($s, $x0, $y0, $x1, $y1, $c) = @_; Gfx::_line($s->{handle}, $x0, $y0, $x1, $y1, on($c)); return $s }
sub rectangle     { my ($s, $x, $y, $w, $h, $c) = @_; Gfx::_rect($s->{handle}, $x, $y, $w, $h, on($c)); return $s }
sub filled_rectangle { my ($s, $x, $y, $w, $h, $c) = @_; Gfx::_fill_rect($s->{handle}, $x, $y, $w, $h, on($c)); return $s }
sub circle        { my ($s, $x, $y, $r, $c) = @_; Gfx::_circle($s->{handle}, $x, $y, $r, on($c)); return $s }
sub filled_circle { my ($s, $x, $y, $r, $c) = @_; Gfx::_fill_circle($s->{handle}, $x, $y, $r, on($c)); return $s }
sub triangle      { my ($s, $x0, $y0, $x1, $y1, $x2, $y2, $c) = @_; Gfx::_triangle($s->{handle}, $x0, $y0, $x1, $y1, $x2, $y2, on($c)); return $s }
sub filled_triangle { my ($s, $x0, $y0, $x1, $y1, $x2, $y2, $c) = @_; Gfx::_fill_triangle($s->{handle}, $x0, $y0, $x1, $y1, $x2, $y2, on($c)); return $s }
sub write_line    { my ($s, $font, $x, $y, $text, $c, $scale) = @_; return Gfx::_text($s->{handle}, $font // '', $x, $y, $text, on($c), $scale // 1) }
sub text_width    { my ($font, $text, $scale) = @_; return Gfx::_text_width($font // '', $text, $scale // 1) }
sub font_height   { return Gfx::_font_height($_[0] // '') }
sub fonts         { return split /,/, Gfx::_fonts() }

1;
