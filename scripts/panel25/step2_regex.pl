# panel25 LT デモ 段階 2: USB CDC から入力した行を正規表現で判定して色を変える。
use v5.40;
use Panel25;

while (<STDIN>) {
    if (/red/i) {
        put_color('red');
    } elsif (/blue/i) {
        put_color('blue');
    } else {
        put_color('black');
    }
}
