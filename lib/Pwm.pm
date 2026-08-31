package Pwm;
# Hardware PWM output on a GPIO (RP2040: 8 slices, GPIO n and n+16 share slice (n/2) % 8;
# the two pins of a pair, e.g. GPIO14 / GPIO15, share one period).
#
#   use Pwm;
#   my $p = Pwm->new(14, frequency => 1000, duty => 0.5);
#   $p->frequency(440);      # Hz; returns the frequency actually programmed
#   $p->duty(0.25);          # 0 .. 1 (1 = always high)
#   $p->stop;                # output low
#   $p->top;                 # counter wrap value of the current period
use strict;

sub new {
    my ($class, $pin, %opt) = @_;
    die "Pwm->new(pin): pin is required\n" unless defined $pin;
    my $self = bless { pin => $pin, frequency => 0, duty => 0 }, $class;
    $self->{slice} = Pwm::_init($pin);
    $self->frequency($opt{frequency}) if $opt{frequency};
    $self->duty($opt{duty} // 0);
    return $self;
}
sub pin       { $_[0]->{pin} }
sub frequency {
    my ($self, $hz) = @_;
    return $self->{frequency} unless defined $hz;
    $self->{frequency} = Pwm::_set_freq($self->{pin}, $hz);
    $self->duty($self->{duty}) if $self->{duty};   # the level is relative to the new period
    return $self->{frequency};
}
sub duty {
    my ($self, $d) = @_;
    return $self->{duty} unless defined $d;
    $self->{duty} = $d;
    Pwm::_set_duty($self->{pin}, $d);
    return $self;
}
sub top     { Pwm::_top($_[0]->{pin}) }
sub stop    { my $self = shift; $self->{duty} = 0; Pwm::_stop($self->{pin}); $self }
sub enable  { Pwm::_enable($_[0]->{pin}, 1); $_[0] }
sub disable { Pwm::_enable($_[0]->{pin}, 0); $_[0] }

1;
