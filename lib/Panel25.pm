package Panel25;
# panel25 (10x10 の SK6812MINI-E、データピン GPIO26) を 1 色で塗るための小さなモジュール。
#
#   use Panel25;
#   put_color('green');    # 100 個すべてを緑に
#   put_color('black');    # 消灯
#
# 色名: red green blue yellow cyan magenta white black
# 知らない色名を渡すと die する。
use strict;
use Ws2812;
use Exporter 'import';
our @EXPORT = qw(put_color);

my $PIN = 26;     # LED のデータピン
my $NUM = 100;    # LED の数

# 明るさの上限 (0-255)。100 個を白で全灯しても USB 給電で足りる程度に絞っている
our $BRIGHT = 16;

my %COLORS = (
    red     => [1, 0, 0],
    green   => [0, 1, 0],
    blue    => [0, 0, 1],
    yellow  => [1, 1, 0],
    cyan    => [0, 1, 1],
    magenta => [1, 0, 1],
    white   => [1, 1, 1],
    black   => [0, 0, 0],
);

my $ws;

# 全部の LED を $name の色にする
sub put_color {
    my ($name) = @_;
    my $rgb = $COLORS{$name} // die "unknown color '$name'\n";
    $ws //= Ws2812->new($PIN);
    my $raw = Ws2812::rgb(map { $_ * $BRIGHT } @$rgb);
    $ws->write_raw(($raw) x $NUM);
}

1;
