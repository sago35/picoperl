package overload;
# 演算子オーバーロードの登録 (picoperl 版)。
#
#   use overload '+' => \&add, '""' => sub { ... }, '<=>' => 'compare';
#
# 本家と同じく、演算子ごとに "(+" のような名前のサブを呼び出し元のパッケージに作る。
# 演算のときは C 側がそのサブをメソッド解決 (@ISA の継承込み) で探して handler($a, $b, $swapped) を呼ぶ。
# 対応: 算術、ビット、比較、文字列の各演算子とその代入形、"" 0+ bool、neg ! abs ++ --、
#       abs int sqrt sin cos exp log atan2。
# 未対応: 参照外し (@{} %{} &{} ${})、<>、-X、~~、qr、nomethod、= (コピー)、overload::constant。
our $VERSION = '1.37';

sub import {
    my $class = shift;
    my $pkg = caller;
    overload::_enable();
    no strict 'refs';
    while (@_) {
        my ($op, $h) = (shift, shift);
        if ($op eq 'fallback') { ${"${pkg}::()"} = $h; next }
        if (ref($h) ne 'CODE') { my $m = $h; $h = sub { my $self = shift; $self->$m(@_) } }
        *{"${pkg}::($op"} = $h;
    }
    *{"${pkg}::(("} = sub { } unless defined &{"${pkg}::(("};   # 「このクラスはオーバーロードしている」の印
}
sub unimport {
    my $class = shift;
    my $pkg = caller;
    no strict 'refs';
    for my $op (@_) { undef &{"${pkg}::($op"} if defined &{"${pkg}::($op"} }
}

sub Overloaded { my $p = ref($_[0]) || $_[0]; return $p->can('((') ? 1 : '' }
sub Method     { my $p = ref($_[0]) || $_[0]; return $p->can("($_[1]") }
sub constant        { }
sub remove_constant { }

1;
