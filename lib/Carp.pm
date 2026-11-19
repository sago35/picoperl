package Carp;
# Carp の picoperl 版 (本家は eval STRING などを使うので読み込めない)。
#
#   use Carp;                 # croak carp confess
#   use Carp qw(cluck);
#   croak "bad argument";     # 呼び出し元の行で die する
#
# croak / carp は本家と同じく「croak を呼んだパッケージの外で最初の呼び出し元」の位置を付ける。
# 本家との違い: confess / cluck / longmess の呼び出し履歴に引数は出ない
# (main::f() called at ... の形)。@CARP_NOT と、@ISA による信頼関係は見ない。
# ファイル名は実行中のファイルになる (picoperl の caller の制限)。
use strict;
use Exporter 'import';
our $VERSION = '1.54';
our @EXPORT    = qw(confess croak carp);
our @EXPORT_OK = qw(cluck verbose longmess shortmess);
our $Verbose   = 0;     # 真なら croak / carp も呼び出し履歴を出す
our $CarpLevel = 0;     # 位置を何段上にずらすか

# メッセージを作る。$skip は、この関数から数えて Carp の関数を呼んだ場所までの段数
sub _short {
    my ($skip, @msg) = @_;
    my $msg = join "", @msg;
    my @at = caller($skip);                 # Carp の関数を呼んだ場所
    return "$msg at - line 0.\n" unless @at;
    my $i = $skip + 1;
    my $outside = 0;
    while (my @c = caller($i)) {            # そのパッケージの外に出るまでさかのぼる
        if ($c[0] ne $at[0]) { @at = @c; $outside = 1; last }
        $i++;
    }
    # 外の呼び出し元が無い (同じパッケージの中だけで呼ばれた) ときは、本家と同じく履歴付きで出す
    return _long($skip + 1, @msg) unless $outside;
    if ($CarpLevel > 0) { my @c = caller($i + $CarpLevel); @at = @c if @c }
    return "$msg at $at[1] line $at[2].\n";
}
sub _long {
    my ($skip, @msg) = @_;
    my $msg = join "", @msg;
    $skip += $CarpLevel if $CarpLevel > 0;
    my @at = caller($skip);
    return "$msg at - line 0.\n" unless @at;
    my $out = "$msg at $at[1] line $at[2].\n";
    my $i = $skip + 1;                      # Carp の関数を呼んだサブから外側へ
    while (my @c = caller($i)) {
        $out .= "\t$c[3]() called at $c[1] line $c[2]\n";
        $i++;
    }
    return $out;
}

sub shortmess { $Verbose ? _long(1, @_) : _short(1, @_) }
sub longmess  { _long(1, @_) }

sub croak   { die $_[0] if @_ == 1 && ref $_[0]; die($Verbose ? _long(1, @_) : _short(1, @_)) }
sub confess { die $_[0] if @_ == 1 && ref $_[0]; die _long(1, @_) }
sub carp    { warn($Verbose ? _long(1, @_) : _short(1, @_)) }
sub cluck   { warn _long(1, @_) }
sub verbose { $Verbose = 1 }

1;
