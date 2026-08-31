package Ssd1306;
# SSD1306 128x64 / 128x32 monochrome OLED over I2C, with a frame buffer (Gfx) in the firmware.
#
#   use Ssd1306;
#   my $d = Ssd1306->new(sda => 12, scl => 13);        # zero-kb02: I2C0, address 0x3C, 128x64
#   $d->clear_display;                                  # clear the buffer and the screen
#   $d->set_pixel($x, $y, $white);                      # color: {R,G,B} hash (any non-zero = on) or 0/1
#   $d->display;                                        # send the buffer to the screen
#   ($w, $h) = $d->size;   $d->set_rotation(180);        # 0 / 90 / 180 / 270 (the zero-kb02 OLED is upside down)
#   drawing (see also Tinydraw / Tinyfont for the tinygo-style functions):
#   $d->line($x0,$y0,$x1,$y1,$c)  $d->rectangle($x,$y,$w,$h,$c)  $d->filled_rectangle(...)
#   $d->circle($x,$y,$r,$c)  $d->filled_circle(...)  $d->triangle($x0,$y0,$x1,$y1,$x2,$y2,$c)
#   $d->write_line($font, $x, $y, $text, $c, $scale)   # fonts: Ssd1306::fonts()
use strict;
use Gfx;

our @ISA = ('Gfx');

sub new {
    my ($class, %o) = @_;
    my $self = Gfx::new($class, width => $o{width} // 128, height => $o{height} // 64);
    $self->{bus}     = $o{i2c} // 0;
    $self->{address} = $o{address} // 0x3C;
    $self->{sda}     = $o{sda} // 12;
    $self->{scl}     = $o{scl} // 13;
    $self->{frequency} = $o{frequency} // 1_000_000;
    $self->{disp} = Ssd1306::_init($self->{handle}, $self->{bus}, $self->{sda}, $self->{scl}, $self->{frequency}, $self->{address});
    die "Ssd1306: no display found at I2C address " . sprintf("0x%02X", $self->{address}) . "\n" unless defined $self->{disp};
    $self->set_rotation($o{rotation}) if defined $o{rotation};
    return $self;
}
sub display       { Ssd1306::_display($_[0]->{disp}); return $_[0] }
sub clear_display { my $self = shift; $self->clear_buffer; $self->display; return $self }
sub invert        { Ssd1306::_invert($_[0]->{disp}, $_[1] ? 1 : 0); return $_[0] }
sub contrast      { Ssd1306::_contrast($_[0]->{disp}, $_[1]); return $_[0] }

1;
