package Storable;
# Storable の picoperl 版 (本物は XS)。dclone (データ構造の深いコピー) だけを用意している。
#   use Storable qw(dclone);   my $copy = dclone($data);
# ハッシュ・配列・スカラーへの参照をたどって複製する。bless されたものは同じクラスになり、
# 同じものを 2 か所から指している構造や循環参照は、複製の中でも同じ形になる。
# ファイルやバイト列への保存 (store / retrieve / freeze / thaw) はありません。
use strict;
our $VERSION = '3.37';
our @EXPORT    = qw(store retrieve);
our @EXPORT_OK = qw(dclone nstore freeze nfreeze thaw lock_store lock_nstore lock_retrieve);
our ($canonical, $forgive_me, $Deparse, $Eval, $recursion_limit, $recursion_limit_hash);

sub dclone { _clone($_[0], {}) }

sub _clone {
    my ($d, $seen) = @_;
    return $d unless ref $d;
    my $addr = builtin::refaddr($d);
    return $seen->{$addr} if exists $seen->{$addr};         # すでに複製したもの: 同じ複製を指す
    my $type  = builtin::reftype($d);
    my $class = builtin::blessed($d);
    my $c;
    if ($type eq 'HASH') {
        $c = $seen->{$addr} = defined $class ? bless({}, $class) : {};
        $c->{$_} = _clone($d->{$_}, $seen) for keys %$d;
    }
    elsif ($type eq 'ARRAY') {
        $c = $seen->{$addr} = defined $class ? bless([], $class) : [];
        push @$c, _clone($_, $seen) for @$d;
    }
    elsif ($type eq 'SCALAR' || $type eq 'REF') {
        my $v;
        $c = $seen->{$addr} = defined $class ? bless(\$v, $class) : \$v;
        $v = _clone($$d, $seen);
    }
    elsif ($type eq 'CODE') { die "Can't store CODE items\n" }
    elsif ($type eq 'GLOB') { die "Can't store GLOB items\n" }
    else { $c = $seen->{$addr} = $d }                       # 正規表現など: そのまま共有する
    return $c;
}

for my $name (qw(store retrieve nstore freeze nfreeze thaw lock_store lock_nstore lock_retrieve)) {
    no strict 'refs';
    *{"Storable::$name"} = sub { die "Storable::$name is not available in picoperl (only dclone is)\n" };
}
1;
