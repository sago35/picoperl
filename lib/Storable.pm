package Storable;
# Storable の picoperl 版 (本物は XS)。データ構造をバイト列やファイルに保存して、読み戻す。
#   use Storable qw(store retrieve freeze thaw dclone);
#   store($data, "/data.sto");   my $d = retrieve("/data.sto");      # ファイルへ
#   my $bytes = freeze($data);   my $d = thaw($bytes);               # バイト列へ
#   my $copy = dclone($data);                                        # 深いコピー
# バイト列は本物の Storable と同じ形式 (nstore / nfreeze はネットワークバイトオーダー) なので、
# PC の perl で作ったファイルをボードで読めるし、その逆もできる。
# ハッシュ・配列・スカラーへの参照、bless されたもの、同じものを 2 か所から指している構造や循環参照を扱える。
# 処理はインタプリタが C で持っていて、このファイルは名前を用意するだけ。
#
# クラスのフックも呼ぶ: ($frozen, @refs) = $obj->STORABLE_freeze($cloning) で保存し、
# $obj->STORABLE_thaw($cloning, $frozen, @refs) か Class->STORABLE_attach($cloning, $frozen) で読み戻す。
#
# 本家との違い: 読み戻すときにクラスのモジュールを自動で require しない (先に use しておく)。
# コード、ファイルハンドル、正規表現、tie された変数は保存できない (dclone は正規表現などを共有する)。
# lock_store などはロックせずに読み書きする。真偽値 (!!1) は 1 と "" として保存する。
use strict;
our $VERSION = '3.37';
our @EXPORT    = qw(store retrieve);
our @EXPORT_OK = qw(dclone nstore freeze nfreeze thaw lock_store lock_nstore lock_retrieve
                    store_fd nstore_fd fd_retrieve file_magic read_magic);
our ($canonical, $forgive_me, $Deparse, $Eval, $recursion_limit, $recursion_limit_hash);

sub CAN_FLOCK { 0 }
sub dclone  { _thaw(_freeze($_[0], 0, 0, 1), 1) }
sub freeze  { _freeze($_[0], 0, $canonical) }
sub nfreeze { _freeze($_[0], 1, $canonical) }
sub thaw    { _thaw($_[0]) }

sub _store {
    my ($data, $file, $net) = @_;
    my $bytes = _freeze($data, $net, $canonical);
    open(my $fh, '>', $file) or die "can't create $file: $!\n";
    binmode $fh;
    print $fh "pst0", $bytes;
    close($fh) or die "can't write to $file: $!\n";
    1;
}
sub store       { _store($_[0], $_[1], 0) }
sub nstore      { _store($_[0], $_[1], 1) }
sub lock_store  { _store($_[0], $_[1], 0) }
sub lock_nstore { _store($_[0], $_[1], 1) }
sub store_fd  { my $fh = $_[1]; print $fh "pst0", _freeze($_[0], 0, $canonical); 1 }
sub nstore_fd { my $fh = $_[1]; print $fh "pst0", _freeze($_[0], 1, $canonical); 1 }

sub retrieve {
    my ($file) = @_;
    open(my $fh, '<', $file) or die "can't open $file: $!\n";
    binmode $fh;
    local $/;
    my $bytes = <$fh>;
    close $fh;
    die "Magic number checking on storable file failed\n" unless defined $bytes && substr($bytes, 0, 4) eq 'pst0';
    _thaw(substr($bytes, 4));
}
sub lock_retrieve { retrieve($_[0]) }
sub fd_retrieve {                                    # ハンドルから 1 つ分だけ読む (残りは次の fd_retrieve が読む)
    my $fh = $_[0];
    local $/;
    my $bytes = <$fh>;
    die "Magic number checking on storable file failed\n" unless defined $bytes && substr($bytes, 0, 4) eq 'pst0';
    my $r = _thaw(substr($bytes, 4), 0, \my $used);
    seek($fh, $used + 4 - length($bytes), 1) if $used + 4 < length($bytes);
    $r;
}

1;
