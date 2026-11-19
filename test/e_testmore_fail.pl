# test/lib/Test/More.pm: 失敗するテストの診断と終了コードを本家と突き合わせる
use strict;
use warnings;
use Test::More tests => 12;

ok(0, "ok fails");
ok(0);
is("got", "want", "is fails");
is(undef, "x", "is undef fails");
isnt("same", "same", "isnt fails");
like("hello", qr/xyz/i, "like fails");
unlike("hello", qr/ell/, "unlike fails");
cmp_ok(5, '<', 3, "cmp_ok fails");
cmp_ok(1, '==', 2, "cmp_ok == fails");
is_deeply([1, { a => [2, 3] }], [1, { a => [2, 4] }], "is_deeply fails");
is_deeply({ a => 1 }, { a => 1, b => 2 }, "is_deeply missing key");
diag("a diag\nin two lines");
pass("the last one passes");
