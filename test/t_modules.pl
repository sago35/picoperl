use strict; no warnings;
use lib 'lib'; use lib 'test/lib';
use TestMod;
use TestMod qw(secret);
use PicoPerl::Keycodes;

print greet("world"), "\n";
print counter(), counter(), counter(), "\n";
print "$shared @list\n";
print secret(), " ", TestMod::double(21), " ", TestMod::uses_pi(), " ", TestMod::PI, "\n";
print $TestMod::VERSION, " ", scalar(@TestMod::EXPORT), " ", join(",", @TestMod::EXPORT_OK), "\n";
print TestMod::Inner::name(), " ", TestMod::Inner::call_outer(), " ", TestMod::inner_name(), "\n";
print __PACKAGE__, "\n";
print defined &greet ? "def" : "undef", " ", defined &double ? "def" : "undef", " ", defined &TestMod::double ? "def" : "undef", "\n";
my $ref = \&TestMod::secret; print $ref->(), "\n";

# keycodes module
print KC_A, " ", KC_LSFT, " ", MO(1), " ", TG(2), " ", KC_BOOTSEL, " ", KC_INT3, "\n";
print is_modifier(KC_LCTL) ? 1 : 0, is_modifier(KC_A) ? 1 : 0, " ", modifier_bit(KC_RGUI), " ", is_layer_key(MO(1)) ? 1 : 0, is_layer_key(KC_A) ? 1 : 0, "\n";
my @km = ([KC_A, KC_B, MO(1)], [KC_LSFT, KC_C, KC_ENT]);
print join(",", map { join("/", @$_) } @km), "\n";
print keycode_value("KC_Z"), " ", keycode_name(0x1D), " ", defined keycode_value("KC_NOPE") ? "def" : "undef", "\n";
print "done\n";
