# tinygo-keeb/workshop 22_buzzer in picoperl:
# play a C major scale up and down on a piezo buzzer.
#
# Please connect a piezo buzzer to the 3V3 and EX01 pins on the back terminal.
#
# | EX01 | EX03 | 3V3 | SDA0 | 3V3 | 3V3 |     |        GROVE            |
# | EX02 | EX04 | GND | SCL0 | GND | GND | - - | GND | 3V3 | SDA0 | SCL0 |
#
# EX01 = GPIO14, EX02 = GPIO15 (PWM slice 7); EX03 = GPIO26, EX04 = GPIO27 (slice 5).
# Go picks the PWM slice by hand (pinToPWM); Pwm->new finds it from the pin.
#
# Go:  speaker, _ := tone.New(pwm, bzrPin); speaker.SetNote(tone.C5)
# pl:  my $speaker = Tone->new($bzr_pin);   $speaker->set_note(Tone::C5);
use strict;
use Tone;

my $bzr_pin = 14;
my $speaker = Tone->new($bzr_pin);

my @song = (
    Tone::C5,
    Tone::D5,
    Tone::E5,
    Tone::F5,
    Tone::G5,
    Tone::A5,
    Tone::B5,
    Tone::C6,
    Tone::C6,
    Tone::B5,
    Tone::A5,
    Tone::G5,
    Tone::F5,
    Tone::E5,
    Tone::D5,
    Tone::C5,
);

while (1) {
    for my $val (@song) {
        $speaker->set_note($val);
        sleep 0.5;
    }
}
