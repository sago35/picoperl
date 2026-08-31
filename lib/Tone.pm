package Tone;
# Square-wave tones on a piezo buzzer via PWM (tinygo.org/x/drivers/tone).
#
#   use Tone;
#   my $speaker = Tone->new(14);          # tone.New(pwm, pin)
#   $speaker->set_note(Tone::C5);         # MIDI note number: A4 = 69 = 440 Hz
#   $speaker->set_frequency(523.25);      # Hz
#   $speaker->set_period(1_911_000);      # nanoseconds (tone.SetPeriod)
#   $speaker->stop;
#   Tone::frequency(Tone::A4)             # 440
#   Tone::note("CS4")                     # 61 (name -> note number, also under real perl)
# Note constants Tone::A0 .. Tone::G9 (S = sharp: Tone::CS4) come from the firmware
# and cost nothing until used.
use strict;
use Pwm;
use Exporter 'import';
our @EXPORT_OK = ('note', 'frequency', map { "$_*" } 'A' .. 'G');

my %semitone = (C => 0, D => 2, E => 4, F => 5, G => 7, A => 9, B => 11);

sub note {
    my $name = shift;
    my $l = substr($name, 0, 1);
    my $s = length($name) == 3 && substr($name, 1, 1) eq 'S' ? 1 : 0;
    my $o = substr($name, 1 + $s);
    die "Tone::note: unknown note '$name'\n"
        unless exists $semitone{$l} && length($o) == 1 && index('0123456789', $o) >= 0;
    return $semitone{$l} + $s + 12 * ($o + 1);
}
sub frequency { my $n = shift; return 440 * 2 ** (($n - 69) / 12) }

sub new {
    my ($class, $pin) = @_;
    die "Tone->new(pin): pin is required\n" unless defined $pin;
    return bless { pwm => Pwm->new($pin), playing => 0 }, $class;
}
sub set_frequency {
    my ($self, $hz) = @_;
    return $self->stop unless $hz && $hz > 0;
    $self->{pwm}->frequency($hz);
    $self->{pwm}->duty(0.5) unless $self->{pwm}->duty == 0.5;   # square wave
    $self->{playing} = $hz;
    return $self;
}
sub set_period { my ($self, $ns) = @_; return $self->set_frequency($ns ? 1e9 / $ns : 0) }
sub set_note   { my ($self, $n) = @_; return $self->set_frequency($n ? frequency($n) : 0) }
sub stop       { my $self = shift; $self->{pwm}->stop; $self->{playing} = 0; return $self }
sub playing    { $_[0]->{playing} }

1;
