# USB CDC から入力した文字列で zero-kb02 の WS2812B x 12 (GPIO1) を光らせるデモ。
# "red" を含む行を入力すると赤く光り、それ以外は消える。
use v5.40;
use Ws2812;

my $ws = Ws2812->new(1);

my %COLORS = (
    red   => [32, 0, 0],
    black => [ 0, 0, 0],
);

# 12 個の LED を全部 $name の色にする
sub put_color($name) {
    my $rgb = $COLORS{$name} // die "unknown color '$name'\n";
    $ws->write(($rgb) x 12);
}

put_color('black');

while (<STDIN>) {
    if (/red/i) {
        put_color('red');
    } else {
        put_color('black');
    }
}
