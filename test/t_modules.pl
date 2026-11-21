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
# サブの中から require したファイルでも、caller はそのファイルのパッケージを返す
{ package LoaderPkg; sub load { require TestCallerUser; TestCallerUser::get() } }
print "caller while loading: ", LoaderPkg::load(), " / body: $TestCallerUser::body_caller\n";
# use Module (); は読み込むだけで、何も取り込まない
{ package EmptyImp; use TestImpExp (); sub has { __PACKAGE__->can('exp_default') ? "imported" : "not imported" } sub direct { TestImpExp::exp_default() } }
{ package EmptyImp2; use TestImpExp qw(); sub has { __PACKAGE__->can('exp_default') ? "imported" : "not imported" } }
{ package EmptyImp3; use TestImpChild (); }
print "empty import list: ", EmptyImp::has(), " ", EmptyImp::direct(), " ", EmptyImp2::has(), " ", (grep({ /EmptyImp3/ } @TestImpBase::log) ? "import called" : "import not called"), "\n";
print "builtins are not methods: ", (main->can('chdir') ? "can" : "cannot"), "\n";
# use lib / @INC / %INC
use lib 'test/lib/inc_a';
use IncMod; use Deep::Mod;
print "use lib: ", IncMod::hi(), " ", Deep::Mod::hi(), " ", $INC{"IncMod.pm"}, " ", $INC{"Deep/Mod.pm"}, " [$INC[0]]\n";
unshift @INC, "test/lib/inc_b"; require IncOther;
print "unshift \@INC: ", IncOther::hi(), " $INC{'IncOther.pm'}\n";
eval { require No::Such::Thing::Here }; print "not in \@INC: ", ($@ =~ /^Can't locate No\/Such\/Thing\/Here\.pm in \@INC/ ? "dies" : $@), "\n";
# 別のファイルのサブの中で起きたエラーは、そのファイルの名前と行で報告される
use TestWhere;
sub tw_short { my $m = shift; $m =~ s{ at \S*?(\w+\.p[lm]) line}{ at $1 line}; $m }
{
    local $SIG{__WARN__} = sub { print "warn in a module: ", tw_short($_[0]) };
    TestWhere::careful();
}
eval { TestWhere::boom() }; print "die in a module: ", tw_short($@);
eval { TestWhere::bad_ref() }; print "runtime error in a module: ", tw_short($@);
{ my $cb = \&TestWhere::boom; eval { $cb->() }; print "through a code ref: ", tw_short($@); }
eval { TestWhere::via(sub { die "callback" }) }; print "callback defined here: ", tw_short($@);
eval { die "here" }; print "back in this file: ", tw_short($@);
{ my @w = TestWhere::where(); s{.*/}{} for @w; print "__FILE__ / caller: @w\n"; }
print "done modules\n";
# require されたファイルの先頭での caller (unless (caller) { ... } の慣用句が正しく働く)
print "caller at file level: ", (caller() ? "has caller" : "no caller"), "\n";
require TestCaller;
print "caller in a required file: $TestCaller::top / $TestCaller::modulino / $TestCaller::sub0\n";
# Class->import(LIST) を自分で呼ぶ形 (use Exporter 'import' のモジュール) と、取り込んだ関数への can
{
    require TestExpOnly;
    package ExpA; TestExpOnly->import('eo_one');
    print "explicit import: ", (ExpA->can('eo_one') ? 1 : 0), (__PACKAGE__->can('eo_one') ? 1 : 0), (ExpA->can('eo_two') ? 1 : 0), " call ", eo_one(), "\n";
    package main;
    my $epkg = "ExpDyn$$";
    print "use in eval: ", (eval "package $epkg; use TestExpOnly ':all'; sub own { 1 } 1" ? "ok" : "failed: $@"), " ", ($epkg->can('eo_one') ? 1 : 0), ($epkg->can('eo_two') ? 1 : 0), ($epkg->can('own') ? 1 : 0), (main->can('eo_two') ? 1 : 0), "\n";
    print "import on a class without one: ", (eval { ExpA->import; ExpA->unimport; 1 } ? "quiet" : "died"), "\n";
}

# require の後ろが式 (Class->method(...) や連結)
{
    require File::Spec;
    my $r1 = require File::Spec->catfile("TestMod.pm");
    my $r2 = require "Test" . "Mod.pm";
    print "require expr: $r1 $r2\n";
}

# 失敗した require は「読み込み済み」にならない。%INC には undef が残り、次の require も失敗する
{
    my $try = sub { my $f = shift; my $ok = eval { require $f; 1 }; (my $e = $@) =~ s/\n.*//s; $e =~ s/ at .*//; ($ok ? "loaded" : "failed: $e") . " inc=" . (exists $INC{$f} ? (defined $INC{$f} ? "set" : "undef") : "none") };
    print "bad 1: ", $try->("TestBadRun.pm"), "\n";
    print "bad 2: ", $try->("TestBadRun.pm"), "\n";
    delete $INC{"TestBadRun.pm"};
    print "bad 3: ", $try->("TestBadRun.pm"), "\n";
    print "good: ", $try->("TestMod.pm"), "\n";
}
