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
# 変数のエクスポート: main が取り込んでいなくても、取り込んだパッケージの中で見える
{ package VarUser; use strict; use TestVars; use TestVars qw(@list);
  sub show { "$Table{k} $scalar @list" } sub change { $scalar = "changed"; push @list, 3 } }
print "var import: ", VarUser::show(), "\n";
VarUser::change();
print "var import shares storage: ", VarUser::show(), " / $TestVars::scalar @TestVars::list\n";
# require VERSION: 最低バージョンの宣言 (ファイルの読み込みではない)
require 5.006; require 5.006_001; require v5.10.1; require 5;
BEGIN { require 5.008; }
print "require version: ", (require 5.010 ? "true" : "false"), "\n";
eval { require 7.001 };  print "too new: ", ($@ =~ /^Perl v7\.1\.0 required--this is only v5\.\d+\.\d+, stopped at / ? "dies" : "no: $@"), "\n";
eval { require v9.2.3 }; print "too new vstring: ", ($@ =~ /^Perl v9\.2\.3 required/ ? "dies" : "no: $@"), "\n";
print "done modules\n";
