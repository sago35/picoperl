package Scalar::Util;
# Scalar::Util の picoperl 版 (本家は XS なので読み込めない)。
#
#   use Scalar::Util qw(blessed reftype refaddr looks_like_number weaken);
#
# blessed / reftype / refaddr / looks_like_number は C のネイティブ関数。
# 本家との違い:
#   weaken / unweaken  何もしない (picoperl は参照カウントだけで弱参照が無い。循環参照は解放されない)
#   isweak / readonly / tainted / isvstring / isdual  常に偽
#   dualvar            数値と文字列を別々に持つ値を作れないので die する
use strict;
use Exporter 'import';
our $VERSION = '1.63';
our @EXPORT_OK = qw(blessed refaddr reftype weaken unweaken isweak dualvar isdual isvstring
                    looks_like_number openhandle readonly set_prototype tainted);

sub blessed { builtin::blessed($_[0]) }
sub reftype { builtin::reftype($_[0]) }
sub refaddr { builtin::refaddr($_[0]) }
# looks_like_number はインタプリタが Scalar::Util::looks_like_number として登録している

sub weaken    { return }
sub unweaken  { return }
sub isweak    { '' }
sub readonly  { '' }
sub tainted   { '' }
sub isvstring { '' }
sub isdual    { '' }
# 開いているハンドルならそのまま返し、そうでなければ undef。グロブ (*STDERR)、その参照、open(my $fh) の
# ハンドル、IO::Handle のオブジェクトを受け付ける
sub openhandle {
    my $fh = shift;
    return undef unless defined $fh;
    my $rt = builtin::reftype($fh);
    if (defined $rt) { return undef unless $rt eq 'GLOB' || $rt eq 'IO' }
    else { return undef unless "$fh" =~ /^\*/ }                 # グロブそのもの (参照でない)
    return defined(tied *$fh) || defined(fileno($fh)) ? $fh : undef;
}
sub set_prototype { __setproto($_[0], $_[1]) }
sub dualvar { die "Scalar::Util::dualvar is not supported by picoperl\n" }

1;
