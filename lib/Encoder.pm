package Encoder;
# Rotary (quadrature) encoder, decoded in the GPIO interrupt by the firmware.
#
#   use Encoder;
#   my $enc = Encoder->new(3, 4, precision => 4);   # A pin, B pin; 4 steps per click
#   my $steps = $enc->read;                          # clicks since the last read (+/-)
#   my $pos   = $enc->position;                      # absolute clicks since new()
#
# Callback mode (dispatched between statements; read/position are then unused):
#   my $enc = Encoder->new(3, 4, on_turn => sub { my ($clicks) = @_; ... });
use strict;
use PicoPerl::Event;

sub new {
    my ($class, $a, $b, %opt) = @_;
    die "Encoder->new(a, b): two pins are required\n" unless defined $a && defined $b;
    Encoder::_init($a, $b);
    if ($opt{on_turn}) {
        PicoPerl::Event::on_encoder($opt{on_turn}, $opt{precision});
        Encoder::_watch(1) if defined &Encoder::_watch;
    }
    return bless { a => $a, b => $b, precision => $opt{precision} // 4, acc => 0, position => 0 }, $class;
}
# raw steps (4 per click on most encoders) since the last call
sub read_raw { return Encoder::_read() }
# whole clicks since the last call
sub read {
    my $self = shift;
    $self->{acc} += Encoder::_read();
    my $clicks = int($self->{acc} / $self->{precision});
    $self->{acc} -= $clicks * $self->{precision};
    $self->{position} += $clicks;
    return $clicks;
}
sub position { my $self = shift; $self->read; return $self->{position} }

1;
