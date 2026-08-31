package Mouse;
# USB HID mouse, in the style of TinyGo's machine/usb/hid/mouse.
#
#   use Mouse;
#   my $m = Mouse->new;                 # mouse.Port()
#   $m->press(Mouse::LEFT);             # hold a button (LEFT, RIGHT, MIDDLE, BACK, FORWARD)
#   $m->release(Mouse::LEFT);
#   $m->click(Mouse::RIGHT);
#   $m->move($dx, $dy);                 # relative, any size (sent in steps of up to 127)
#   $m->wheel($n); $m->wheel_up; $m->wheel_down;
# Reports go out on the third HID interface; each call waits up to ~10 ms for
# the USB stack to accept the report and returns 1 if it was sent.
use strict;
use Hid;
use Time::HiRes qw(usleep);

use constant {
    LEFT    => 1,
    RIGHT   => 2,
    MIDDLE  => 4,
    BACK    => 8,
    FORWARD => 16,
};

sub new { my $class = shift; return bless { buttons => 0 }, $class }
sub available { return Hid::has_mouse() }

sub _send {
    my ($self, $dx, $dy, $wheel, $pan) = @_;
    for my $try (1 .. 50) {
        return 1 if Hid::mouse_report($self->{buttons}, $dx // 0, $dy // 0, $wheel // 0, $pan // 0);
        usleep(200);
    }
    return 0;
}

sub buttons { return $_[0]->{buttons} }
sub press   { my ($self, $btn) = @_; $self->{buttons} |= $btn; return $self->_send }
sub release { my ($self, $btn) = @_; $self->{buttons} &= ~$btn; return $self->_send }
sub click   { my ($self, $btn) = @_; $self->press($btn) && $self->release($btn) }

sub move {
    my ($self, $dx, $dy) = @_;
    $dx //= 0; $dy //= 0;
    while ($dx != 0 || $dy != 0) {
        my $sx = $dx > 127 ? 127 : $dx < -127 ? -127 : $dx;
        my $sy = $dy > 127 ? 127 : $dy < -127 ? -127 : $dy;
        return 0 unless $self->_send($sx, $sy);
        $dx -= $sx; $dy -= $sy;
    }
    return 1;
}
sub wheel {
    my ($self, $v) = @_;
    while ($v != 0) {
        my $s = $v > 127 ? 127 : $v < -127 ? -127 : $v;
        return 0 unless $self->_send(0, 0, $s);
        $v -= $s;
    }
    return 1;
}
sub wheel_up   { $_[0]->wheel(1) }
sub wheel_down { $_[0]->wheel(-1) }

1;
