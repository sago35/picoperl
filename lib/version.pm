package version;
# バージョン番号のオブジェクト。
#
#   use version;
#   my $v = version->parse("1.2.3");          # version->new(...) も同じ
#   print "newer\n" if $v > version->parse("1.2");
#   print $v->normal, " ", $v->numify, "\n";  # v1.2.3 1.002003
#   my $q = version->declare("1.2");          # 必ずドット区切りとして読む (v1.2.0)。qv("1.2") も同じ
#
# 小数の形 ("1.23"、1.02_03) とドット区切りの形 ("1.2.3"、"v1.2"、v 文字列) の両方を読み、
# 文字列化、numify / normal、比較 (<=> cmp と、文字列や数との比較)、is_alpha / is_qv、
# is_lax / is_strict、version::_VERSION に対応します。処理はインタプリタが C で持っていて (perl 本体も
# この部分は C)、このファイルは演算子のオーバーロードを有効にして import を用意するだけです。
#
# 本家との違い: `use version;` しても UNIVERSAL::VERSION は置き換えません (Module->VERSION(要求) は
# picoperl の組み込みの比較のまま)。$version::LAX / $version::STRICT (正規表現) はありません。
# 印字できる文字だけの v 文字列 (v65.66 は "AB") は、ただの文字列と区別できません。
use strict;
our $VERSION = '0.9930';
our $CLASS = 'version';
overload::_enable();

sub import {
    my $class = shift;
    no strict 'refs';
    unless ($class eq $CLASS) {                                # 派生クラス
        *{"${class}::declare"} = \&declare;
        *{"${class}::qv"} = \&qv;
    }
    my %args = @_ ? map { $_ => 1 } @_ : (qv => 1);
    my $callpkg = caller();
    for my $name (qw(declare qv)) {
        next unless $args{$name} && !defined &{"${callpkg}::$name"};
        *{"${callpkg}::$name"} = sub { $class->$name(shift) };
    }
    *{"${callpkg}::VERSION"} = \&_VERSION if $args{VERSION};
    for my $name (qw(is_strict is_lax)) {
        *{"${callpkg}::$name"} = \&{$name} if $args{$name} && !defined &{"${callpkg}::$name"};
    }
}

1;
