# tinygo-keeb/workshop 05_rotary in picoperl:
# print the position of the rotary encoder (GPIO3 / GPIO4) whenever it changes.
#
# Encoder->new decodes the quadrature signal in the GPIO interrupt (like
# NewQuadratureViaInterrupt); precision 4 = one click of the knob per 4 steps,
# and position() counts clicks like Position() in the Go version.
use strict;
use Encoder;
use Time::HiRes qw(usleep);

my $enc = Encoder->new(3, 4, precision => 4);

my $old = 0;
while (1) {
    my $new = $enc->position;
    if ($new != $old) {
        print "value:  $new\n";
        $old = $new;
    }
    usleep(1000);
}
