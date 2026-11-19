# panel25 LT デモ 番外編: 10x10 の LED に、文字を緑で右から左へ流す。
# (プレイグラウンド wasm/index.html の「panel25 (文字スクロール)」と同じ内容)
use strict;
use Gfx;
use Tinyfont;
use Ws2812;
use Time::HiRes qw(usleep);

my $text  = "hello picoperl";    # 日本語も書けます ("こんにちは" など)
my $green = Ws2812::rgb(0, 24, 0);     # 文字の色 (赤, 緑, 青)
my $ws    = Ws2812->new(26);

# 座標 (左上が 0,0) -> LED の番号
sub led { my ($x, $y) = @_; return $y < 5 ? $x * 5 + $y : $x * 5 + ($y - 5) + 50 }

# 文字は最初に 1 回だけ、画面の無いバッファに東雲フォント (12px) で描く。
# 文字の高さは 11 ドットあるので、いちばん上の 1 行を落として 10 行に収める
my $w = Gfx::text_width(Tinyfont::SHNMK12, $text);
my $g = Gfx->new(width => $w, height => 16);
$g->write_line(Tinyfont::SHNMK12, 0, 12, $text, 1);

# @col: 文字列の 1 列ぶんの点を 10 ビットの数にしたもの (y 行目がビット y)。前後に空白を 10 列ずつ
my @col = ((0) x 10);
for my $x (0 .. $w - 1) {
    my $bits = 0;
    $bits |= $g->get_pixel($x, $_ + 3) << $_ for 0 .. 9;
    push @col, $bits;
}
push @col, (0) x 10;

# LED の番号ごとの「何列目か」と「その行のビット」
my (@x, @bit);
for my $y (0 .. 9) { for my $x (0 .. 9) { my $i = led($x, $y); $x[$i] = $x; $bit[$i] = 1 << $y } }

my $t = 0;      # 左端に見えている列
while (1) {
    $ws->write_raw(map { $col[$t + $x[$_]] & $bit[$_] ? $green : 0 } 0 .. 99);
    $t = ($t + 1) % (@col - 9);
    usleep(50_000);
}
