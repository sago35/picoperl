# Interrupt-callback demo for the PICOPERL board (zero-kb02: button GPIO2,
# encoder GPIO3/4).
#
# Edge callbacks are dispatched like perl's "safe signals": the GPIO interrupt
# only queues an event, the handler runs between statements of the main loop.
# Timers use the real perl idiom: alarm() + $SIG{ALRM}.
use strict;
use warnings;
use Gpio;
use Encoder;
use Time::HiRes qw(time);
use Board;

print "picoperl irq demo, heap free ", Board::heap_free() // "?", "\n";

my ($count, $pos, $last_edge) = (0, 0, 0);
my $btn = Gpio->new(2, 'input_pullup',
    on_falling => sub {
        my $now = time();
        return if $now - $last_edge < 0.05;    # a mechanical switch bounces
        $last_edge = $now;
        $count++;
        print "button pressed ($count)\n";
    });
my $enc = Encoder->new(3, 4, on_turn => sub {
    $pos += $_[0];
    print "encoder: $pos\n";
});

$SIG{ALRM} = sub { print "-- alive, ", int(time()), "s --\n"; alarm 10 };
alarm 10;

print "press the button (GPIO2) or turn the encoder (GPIO3/4)\n";
while (1) { sleep 0.1 }
