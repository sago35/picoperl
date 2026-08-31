package PicoPerl::Event;
# Deferred board callbacks, perl "safe signals" style: the firmware's interrupt
# handlers only queue events; the interpreter calls _dispatch between
# statements (so handlers never run in interrupt context, and a die inside a
# handler propagates like a die in the surrounding code).
#
# Normally used through the on_* options of Gpio and Encoder:
#   my $btn = Gpio->new(2, 'input_pullup', on_falling => sub { ... });
#   my $enc = Encoder->new(3, 4, on_turn => sub { my ($clicks) = @_; ... });
# Timers use the real perl idiom instead: alarm(1) + $SIG{ALRM}.
use strict;

my %gpio;   # pin => { falling => cb, rising => cb, change => cb }
my %enc;    # index => { cb, precision, acc }

sub on_gpio {
    my ($pin, $edge, $cb) = @_;
    die "PicoPerl::Event::on_gpio: edge must be falling/rising/change\n"
        unless $edge eq 'falling' || $edge eq 'rising' || $edge eq 'change';
    $gpio{$pin}{$edge} = $cb;
    return;
}

sub on_encoder {
    my ($cb, $precision) = @_;
    $enc{0} = { cb => $cb, precision => $precision // 4, acc => 0 };
    return;
}

# called by the interpreter with ("gpio", $pin, $level) or ("encoder", $index, $steps)
sub _dispatch {
    my ($kind, $a, $b) = @_;
    if ($kind eq 'gpio') {
        my $h = $gpio{$a} or return;
        # the edge woke us; the level must still agree when the handler runs
        # (a real press stays low for tens of ms, a crosstalk glitch from e.g.
        # the encoder pins is back high within microseconds - drop those)
        return if Gpio::_get($a) != ($b ? 1 : 0);
        $h->{change}->($b)  if $h->{change};
        $h->{rising}->()    if $b && $h->{rising};
        $h->{falling}->()   if !$b && $h->{falling};
    }
    elsif ($kind eq 'encoder') {
        my $e = $enc{0} or return;
        $e->{acc} += $b;
        my $clicks = int($e->{acc} / $e->{precision});
        if ($clicks) {
            $e->{acc} -= $clicks * $e->{precision};
            $e->{cb}->($clicks);
        }
    }
    return;
}

1;
