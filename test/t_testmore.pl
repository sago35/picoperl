# test/lib/Test/More.pm (picoperl 用の小さな Test::More) を本家と突き合わせる: 成功する場合の出力
use strict;
use warnings;
use Test::More;

ok(1, "ok with name");
ok(1);
is("abc", "abc", "is string");
is(undef, undef, "is undef");
isnt("a", "b", "isnt");
like("hello world", qr/wor/, "like");
unlike("hello", qr/xyz/, "unlike");
cmp_ok(3, '<', 5, "cmp_ok <");
cmp_ok("a", 'eq', "a", "cmp_ok eq");
cmp_ok(2, '==', 2.0, "cmp_ok ==");
is_deeply([1, { a => [2, 3] }, "x"], [1, { a => [2, 3] }, "x"], "is_deeply nested");
is_deeply({}, {}, "is_deeply empty");
is_deeply("s", "s", "is_deeply scalars");
pass("pass");
note("a note\nin two lines");
{ package TmObj; sub new { bless { v => $_[1] }, $_[0] } sub v { $_[0]{v} } }
{ package TmSub; our @ISA = ('TmObj'); }
can_ok("TmObj", "new");
can_ok("TmObj", "new", "v");
isa_ok(TmSub->new(1), "TmObj");
isa_ok([], "ARRAY");
isa_ok("TmSub", "TmObj");
my $obj = new_ok("TmObj", [5]);
is($obj->v, 5, "new_ok returns the object");
is("has # hash", "has # hash", "name with # mark");

SKIP: {
    skip "not today", 2 if 1;
    fail("never reached");
    fail("never reached either");
}
TODO: {
    local $TODO = "known to fail";
    ok(0, "todo test");
    is("a", "b", "todo is");
}
ok(!defined $TODO, "TODO is restored");

subtest "a subtest" => sub {
    plan tests => 2;
    ok(1, "inner one");
    is(2, 2, "inner two");
};
subtest "subtest without plan" => sub {
    ok(1, "inner");
    subtest "nested" => sub { pass("deep"); done_testing };
};
my @ex = explain("plain", [1, { k => "v" }]);
is(scalar(@ex), 2, "explain returns a list");
is($ex[0], "plain", "explain keeps plain scalars");
done_testing;
