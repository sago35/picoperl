# panel25 LT デモ 段階 1: sleep で L チカ。
use v5.40;
use Panel25;

while (1) {
    put_color('red');
    sleep 0.5;
    put_color('black');
    sleep 0.5;
}
