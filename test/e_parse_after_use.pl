# モジュールを読み込んだあとのコンパイルエラー: エラーを報告して終わる
# (読み込んだモジュールのサブは解放の一覧に残り、作りかけの一時ノードは解放される)
use strict;
use lib 'test/lib';
use TestMod;
{ my $n = 0; sub next_n { ++$n } }
my @list = (1, 2, next_n(), [3, $undeclared_here]);
print "not reached\n";
