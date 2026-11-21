package TestProtoMod;
# 空のプロトタイプのサブと定数をエクスポートするモジュール (t_lexmisc.pl 用)
use Exporter "import"; our @EXPORT = qw(tau one);
sub tau () { 6 }
use constant one => 1;
1;
