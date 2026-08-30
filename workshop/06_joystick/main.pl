# tinygo-keeb/workshop 06_joystick in picoperl:
# print the joystick position (X = GPIO29, Y = GPIO28) and its button (GPIO0).
#
# Go:  ax := machine.ADC{Pin: machine.GPIO29}; ax.Configure(...); ax.Get()
# pl:  my $ax = Adc->new(29);                                      $ax->get
# get() returns 0..0xFFFF like TinyGo (the RP2040 sample is 12 bits, shifted left by 4);
# the stick rests near 0x8000 on both axes.
use strict;
use Adc;
use Gpio;

my $ax  = Adc->new(29);
my $ay  = Adc->new(28);
my $btn = Gpio->new(0, 'input_pullup');

while (1) {
    my $x = $ax->get;
    my $y = $ay->get;
    my $pressed = !$btn->read;
    printf("%04X %04X %s\n", $x, $y, $pressed ? "true" : "false");
    sleep 0.2;
}
