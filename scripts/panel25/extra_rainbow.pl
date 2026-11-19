# panel25 LT デモ 番外編: 10x10 の LED に、斜めに流れる虹を出す。
# (プレイグラウンド wasm/index.html の「panel25 (10x10 LED)」と同じ内容)
use strict;
use Ws2812;
use Time::HiRes qw(usleep);

# panel25: 10x10 の LED パネル (GPIO26)。LED は上半分の 5 行を左の列から縦に 0..49、
# 続けて下半分の 5 行を同じ順に 50..99 と並んでいる:
#    0  5 .. 45
#    1  6 .. 46
#    :  :     :
#    4  9 .. 49
#   50 55 .. 95
#    :  :     :
#   54 59 .. 99
my $ws = Ws2812->new(26);

# 座標 (左上が 0,0) -> LED の番号
sub led { my ($x, $y) = @_; return $y < 5 ? $x * 5 + $y : $x * 5 + ($y - 5) + 50 }

# 毎フレーム計算すると実機では遅いので、先に表を作っておく:
#   @color  色相環を 60 等分した色
#   @shift  LED ごとの色のずれ (斜めに x + y が大きいほど先の色)
my @color = map { hue($_ * 6) } 0 .. 59;
my @shift;
for my $y (0 .. 9) { for my $x (0 .. 9) { $shift[led($x, $y)] = ($x + $y) * 3 } }

my $t = 0;
while (1) {
    $ws->write_raw(map { $color[($t + $_) % 60] } @shift);    # 斜めに流れる虹
    $t = ($t + 2) % 60;
    usleep(60_000);
}

# 色相 (0..359) -> 0xGGRRBBxx。明るさ控えめの r -> g -> b -> r の色相環
sub hue {
    my $h = int(shift) % 360;
    my $up = int(44 * ($h % 120) / 120);
    my ($r, $g, $b) = $h < 120 ? (44 - $up, $up, 0)
                    : $h < 240 ? (0, 44 - $up, $up)
                    :            ($up, 0, 44 - $up);
    return Ws2812::rgb($r, $g, $b);
}
