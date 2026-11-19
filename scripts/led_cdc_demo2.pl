# led_cdc_demo.pl に try/catch を足した版。
# "red" "green" "blue" を含む行を入力するとその色に光る。
# どれも含まない行は die し、catch でエラーを表示して赤く 2 回フラッシュする。
use v5.40;
use Ws2812;

my $ws = Ws2812->new(1);

my %COLORS = (
    red   => [32,  0,  0],
    green => [ 0, 32,  0],
    blue  => [ 0,  0, 32],
    black => [ 0,  0,  0],
);

# 12 個の LED を全部 $name の色にする
sub put_color($name) {
    my $rgb = $COLORS{$name} // die "unknown color '$name'\n";
    $ws->write(($rgb) x 12);
}

put_color('black');

while (<STDIN>) {
    try {
        if (/red/i) {
            put_color('red');
        } elsif (/green/i) {
            put_color('green');
        } elsif (/blue/i) {
            put_color('blue');
        } else {
            die "no color in: $_";
        }
    }
    catch ($e) {
        print "error: $e";
        for (1 .. 2) {              # 赤く 2 回フラッシュして消灯
            put_color('red');
            sleep 0.15;
            put_color('black');
            sleep 0.15;
        }
    }
}
