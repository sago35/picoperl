package PicoPerl::Keycodes;
# Keycode names for keymaps.
#
#   use PicoPerl::Keycodes;
#   my @keymap = ([KC_A, KC_B, MO(1)], ...);
#
# The KC_* / PP_* constants (USB HID usage codes, QMK names, plus consumer control
# media keys such as KC_MUTE) come from a table built into the firmware. They are
# created on first use, so `use PicoPerl::Keycodes` exports them lazily ("KC_*").
# Under a normal perl the same table is provided by PicoPerl::KeycodesTable.
use strict;
use Exporter 'import';

BEGIN {
    require PicoPerl::KeycodesTable unless defined &PicoPerl::Keycodes::_value;
}

our @EXPORT = (
    qw(MO TG KC_BOOTSEL is_modifier modifier_bit is_layer_key is_consumer consumer_usage keycode_value keycode_name),
    (defined &PicoPerl::Keycodes::_value ? ('KC_*', 'PP_*') : sort keys %PicoPerl::Keycodes::TABLE),
);

sub keycode_value { return defined &PicoPerl::Keycodes::_value ? PicoPerl::Keycodes::_value($_[0]) : $PicoPerl::Keycodes::TABLE{$_[0]} }
sub keycode_name  {
    my $c = shift;
    return PicoPerl::Keycodes::_name($c) if defined &PicoPerl::Keycodes::_name;
    for my $n (sort keys %PicoPerl::Keycodes::TABLE) { return $n if $PicoPerl::Keycodes::TABLE{$n} == $c }
    return undef;
}

# picoperl pseudo keycodes (never sent to the host); also in the firmware table
sub MO { return PP_LAYER_MO() | $_[0] }     # MO(n): momentary layer n while held
sub TG { return PP_LAYER_TG() | $_[0] }     # TG(n): toggle layer n
sub KC_BOOTSEL() { return PP_BOOTSEL() }    # reboot into the UF2 bootloader

sub is_modifier  { my $kc = shift; return $kc >= 0xE0 && $kc <= 0xE7 }
sub modifier_bit { my $kc = shift; return 1 << ($kc - 0xE0) }
sub is_layer_key { my $kc = shift; my $t = $kc & 0xF000; return $t == PP_LAYER_MO() || $t == PP_LAYER_TG() }
# consumer control (media) keys such as KC_MUTE / KC_VOLU: sent on a separate HID interface
sub is_consumer    { my $kc = shift; return ($kc & 0xF000) == PP_CONSUMER() }
sub consumer_usage { my $kc = shift; return $kc & 0x0FFF }

1;
