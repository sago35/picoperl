package Adc;
# Analog inputs (RP2040: GPIO26-29 = channels 0-3, plus the on-chip temperature sensor).
#
#   use Adc;
#   my $ax = Adc->new(29);
#   my $v = $ax->get;          # 0..0xFFFF, like machine.ADC.Get() in TinyGo (12-bit sample << 4)
#   my $raw = $ax->read_raw;   # 0..4095
#   my $volts = $ax->voltage;  # 0..3.3
#   Adc->new(Adc::TEMPERATURE)->temperature   # degrees C (rough)
use strict;

use constant TEMPERATURE => 4;   # pseudo pin for the temperature sensor

sub new {
    my ($class, $pin) = @_;
    die "Adc->new(pin): pin is required\n" unless defined $pin;
    defined Adc::_configure($pin) or die "Adc: GPIO$pin has no ADC (use GPIO26-29)\n";
    return bless { pin => $pin }, $class;
}
sub pin       { $_[0]->{pin} }
sub read_raw  { return Adc::_read($_[0]->{pin}) // 0 }
sub get       { return read_raw($_[0]) << 4 }
sub voltage   { return read_raw($_[0]) * 3.3 / 4095 }
sub temperature { my $v = voltage($_[0]); return 27 - ($v - 0.706) / 0.001721 }

# class functions on bare pin numbers
sub configure { return Adc::_configure($_[0]) }
sub read      { return Adc::_read($_[0]) // 0 }

1;
