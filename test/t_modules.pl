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
# 継承した import: use は親クラスの import を呼ぶ
use TestImpChild tests => 5;
print "inherited import: ", join(" | ", @TestImpBase::log), " / ", imp_marker(), "\n";
{ package ImpUser; use TestImpChild qw(a b); }
print "inherited import in a package: $TestImpBase::log[-1] / ", ImpUser::imp_marker(), "\n";
use TestImpExp;
use TestImpExp qw(exp_optional);
print "Exporter subclass: ", exp_default(), " ", exp_optional(), "\n";
# import を自分で呼ぶ / export_to_level / タグ
{ package ImpManual; sub take { TestImpExp->import('exp_optional'); exp_optional() } }
print "manual import: ", ImpManual::take(), "\n";
{ package ImpTags; require Exporter; our @ISA = ('Exporter'); our @EXPORT_OK = qw(t_one t_two $t_var); our %EXPORT_TAGS = (both => [qw(t_one t_two)]);
  our $t_var = "tv"; sub t_one { 1 } sub t_two { 2 } sub push_up { ImpTags->export_to_level(1, undef, 't_two') } }
{ package ImpTagUser; ImpTags->import(':both', '$t_var'); our $t_var; sub sum { t_one() + t_two() . $t_var } }
print "tags: ", ImpTagUser::sum(), "\n";
{ package ImpLevel; sub go { ImpTags::push_up(); t_two() } }
print "export_to_level: ", ImpLevel::go(), "\n";
eval { ImpTags->import('no_such_name') };
print "unknown name: ", ($@ =~ /^"no_such_name" is not exported by the ImpTags module/ ? "dies" : "no: $@"), "\n";
print "done modules\n";
