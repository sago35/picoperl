package Midi;
# USB MIDI (tinygo の machine/usb/adc/midi 風)。
#
#   use Midi;
#   my $m = Midi->new;                              # midi.Port()
#   $m->note_on(0, 1, Midi::C4, 0x40);              # (cable, channel 1..16, note, velocity)
#   $m->note_off(0, 1, Midi::C4, 0x40);
#   $m->control_change(0, 1, Midi::CC_MODULATION_WHEEL, $v);
#   $m->pitch_bend(0, 1, $v14);                     # 14 bit、0x2000 = センター
#   $m->program_change(0, 1, $program);             # 正しく 2 バイトで送る
#   $m->write(0, @bytes);                           # 生のメッセージ
#   Midi::ready()                                   # ホストにマウントされているか
#
# 音名定数 Midi::A0 .. Midi::G9 (S = シャープ: Midi::CS4) は MIDI ノート番号
# (A4 = 69 = 440 Hz) で、ファームウェアの表から使われたものだけ生成される。
use strict;

use constant CC_MODULATION_WHEEL => 1;
use constant CC_VOLUME           => 7;
use constant CC_PAN              => 10;
use constant CC_EXPRESSION       => 11;

sub new   { my ($class) = @_; return bless {}, $class }
sub ready { return defined &Midi::_ready ? Midi::_ready() : 0 }

sub note_on        { my ($self, $cable, $ch, $note, $vel) = @_; return Midi::_write($cable, 0x90 | (($ch - 1) & 0x0F), $note & 0x7F, ($vel // 0x40) & 0x7F) }
sub note_off       { my ($self, $cable, $ch, $note, $vel) = @_; return Midi::_write($cable, 0x80 | (($ch - 1) & 0x0F), $note & 0x7F, ($vel // 0x40) & 0x7F) }
sub control_change { my ($self, $cable, $ch, $cc, $v) = @_;     return Midi::_write($cable, 0xB0 | (($ch - 1) & 0x0F), $cc & 0x7F, $v & 0x7F) }
sub pitch_bend     { my ($self, $cable, $ch, $v) = @_;          return Midi::_write($cable, 0xE0 | (($ch - 1) & 0x0F), $v & 0x7F, ($v >> 7) & 0x7F) }
sub program_change { my ($self, $cable, $ch, $p) = @_;          return Midi::_write($cable, 0xC0 | (($ch - 1) & 0x0F), $p & 0x7F) }
sub write          { my ($self, $cable, @b) = @_;               return Midi::_write($cable, @b) }

1;
