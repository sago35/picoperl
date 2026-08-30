package Ws2812;
# WS2812B / SK6812 RGB LEDs (driven by a PIO state machine in the firmware).
#
#   use Ws2812;
#   my $ws = Ws2812->new(16);                                  # data pin
#   $ws->put_color({ R => 0x20, G => 0x20, B => 0x20 });       # one LED (color.RGBA style)
#   $ws->write_raw(0xFFFFFFFF, 0xFF0000FF);                    # one 32-bit 0xGGRRBBxx value per LED
#   $ws->write([255, 0, 0], [0, 255, 0]);                      # [R, G, B] per LED
use strict;

sub new {
    my ($class, $pin, %opt) = @_;
    die "Ws2812->new(pin): pin is required\n" unless defined $pin;
    my $h = Ws2812::_init($pin);
    return bless { pin => $pin, handle => $h }, $class;
}
sub pin       { $_[0]->{pin} }
sub put_color { my ($self, $c) = @_; return Ws2812::_put_color($self->{handle}, $c) }
sub write_raw { my $self = shift; return Ws2812::_write_raw($self->{handle}, @_) }
sub write     { my $self = shift; return Ws2812::_write_raw($self->{handle}, map { rgb(@$_) } @_) }
sub off       { my ($self, $n) = @_; return Ws2812::_write_raw($self->{handle}, (0) x ($n // 1)) }

# rgb($r, $g, $b) -> raw 0xGGRRBB00 value
sub rgb { my ($r, $g, $b) = @_; return (($g & 0xFF) << 24) | (($r & 0xFF) << 16) | (($b & 0xFF) << 8) }

1;
