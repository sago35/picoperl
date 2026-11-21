package Fcntl;
# Fcntl の picoperl 版 (本物は XS)。定数の値は Linux のもの。
#
# 定数 (O_RDONLY、LOCK_EX、SEEK_SET、S_IFMT など約 100 個) はインタプリタが表を持っていて、
# 使われたときに初めて作られる。ここでは名前の先頭だけを書いて (F* O_* ...) 取り込む側に知らせる。
# `use Fcntl;` でも `use Fcntl qw(:seek)` でも、RAM を使うのは実際に書いた定数の分だけです。
# ボードのファイルシステムにはロックもパーミッションも無いので、番号として使えるだけです
# (seek の SEEK_SET / SEEK_CUR / SEEK_END は実際に効きます)。
use strict;
our $VERSION = "1.20";
our @EXPORT    = qw(F* O_*);                        # FD_CLOEXEC, F_GETFL ..., O_RDONLY ...
our @EXPORT_OK = qw(DN_* LOCK_* SEEK_* S_* _S_*);   # :flock :seek :mode、名前を指定したとき

# ファイルの種類を調べる関数。S_IFMT は引数なしならマスク、引数ありなら種類のビットを返す
sub S_IFMT  { @_ ? $_[0] & 0170000 : 0170000 }
sub S_IMODE { $_[0] & 07777 }
{
    my %type = (REG => 0100000, DIR => 0040000, LNK => 0120000, SOCK => 0140000, BLK => 0060000, CHR => 0020000, FIFO => 0010000);
    no strict 'refs';
    for my $n (keys %type) { my $t = $type{$n}; *{"Fcntl::S_IS$n"} = sub { ($_[0] & 0170000) == $t } }
}
1;
