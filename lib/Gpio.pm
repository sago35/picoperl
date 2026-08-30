package Gpio;
# GPIO pins.
#
#   use Gpio;
#   my $led = Gpio->new(25, 'output');          # 'input', 'input_pullup', 'input_pulldown', 'output'
#   $led->high; $led->low; $led->put(1); $led->toggle;
#   my $btn = Gpio->new(2, 'input_pullup');
#   if (!$btn->get) { ... }                      # pressed (active low)
#
# Class functions work on bare pin numbers: Gpio::configure(2, 'input_pullup'); Gpio::get(2);
use strict;

sub configure {
    my ($pin, $mode) = @_;
    $mode //= 'input';
    Gpio::_init($pin);
    if ($mode eq 'output') { Gpio::_set_dir($pin, 1) }
    elsif ($mode eq 'input') { Gpio::_set_dir($pin, 0); Gpio::_disable_pulls($pin) if defined &Gpio::_disable_pulls }
    elsif ($mode eq 'input_pullup') { Gpio::_set_dir($pin, 0); Gpio::_pull_up($pin) }
    elsif ($mode eq 'input_pulldown') { Gpio::_set_dir($pin, 0); Gpio::_pull_down($pin) }
    else { die "Gpio: unknown mode '$mode'\n" }
    return $pin;
}
sub get { return Gpio::_get($_[0]) }
sub put { return Gpio::_put($_[0], $_[1] ? 1 : 0) }

sub new {
    my ($class, $pin, $mode) = @_;
    die "Gpio->new(pin, mode): pin is required\n" unless defined $pin;
    configure($pin, $mode);
    my $self = bless { pin => $pin, mode => $mode // 'input', state => 0 }, $class;
    return $self;
}
sub pin       { $_[0]->{pin} }
sub read      { Gpio::_get($_[0]->{pin}) }
sub write     { my ($self, $v) = @_; $self->{state} = $v ? 1 : 0; Gpio::_put($self->{pin}, $self->{state}) }
sub high      { $_[0]->write(1) }
sub low       { $_[0]->write(0) }
sub toggle    { my $self = shift; $self->write(!$self->{state}) }
sub pull_up   { Gpio::_pull_up($_[0]->{pin}); $_[0] }
sub pull_down { Gpio::_pull_down($_[0]->{pin}); $_[0] }
sub set_dir   { my ($self, $out) = @_; Gpio::_set_dir($self->{pin}, $out ? 1 : 0); $self }

1;
