# panel25 LT デモ 段階 3: 色が見つからない行は die し、try/catch で受けて赤く 2 回フラッシュ。
use v5.40;
use Panel25;

while (<STDIN>) {
    try {
        if (/red/i) {
            put_color('red');
        } elsif (/blue/i) {
            put_color('blue');
        } else {
            die "no color in: $_";
        }
    }
    catch ($e) {
        print "error: $e";
        for (1 .. 2) {
            put_color('red');
            sleep 0.15;
            put_color('black');
            sleep 0.15;
        }
    }
}
