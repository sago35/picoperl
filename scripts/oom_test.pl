# メモリ不足の確認用: メモリを使い切るまで配列を伸ばす
use strict; use warnings;
print "oom test: press Enter to eat all memory\n";
my $l = <STDIN>;
my @a; my $n = 0;
while (1) { push @a, "x" x 1000; $n++; print "allocated $n KB, heap free ", Board::_heap_free(), "\n" if $n % 20 == 0; }
