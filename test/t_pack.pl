# vec / pack / unpack
use strict; use warnings;
sub hx { join " ", map { sprintf "%02x", ord } split //, $_[0] }
sub show { my ($n, @v) = @_; print "$n: ", join("|", map { defined $_ ? $_ : "undef" } @v), "\n" }
# vec
my $v = "";
vec($v, 0, 1) = 1; vec($v, 3, 1) = 1; vec($v, 9, 1) = 1; show("vec1", hx($v), vec($v, 0, 1), vec($v, 1, 1), vec($v, 9, 1), vec($v, 100, 1), length $v);
my $w = ""; vec($w, 0, 4) = 0xa; vec($w, 1, 4) = 0x5; vec($w, 3, 2) = 3; vec($w, 2, 8) = 0x41; vec($w, 2, 16) = 0x1234; vec($w, 2, 32) = 0xdeadbeef;
show("vec2", hx($w), vec($w, 0, 4), vec($w, 1, 4), vec($w, 0, 8), vec($w, 2, 16), vec($w, 2, 32), vec($w, 1, 32), vec("abc", 1, 8), vec("abc", 0, 16), vec("abc", 1, 16));
my %h; vec($h{bits}, 5, 1) = 1; my @a; vec($a[1], 0, 8) = 65; show("vec3", hx($h{bits}), $a[1]);
my $mask = ""; vec($mask, $_, 1) = 1 for 1, 2, 12; show("vec4", unpack("b*", $mask), unpack("%32b*", $mask) // "n/a") if 0; show("vec4", unpack("b*", $mask));
# pack: strings
show("a", hx(pack("a", "xyz")), hx(pack("a5", "xy")), hx(pack("A5", "xy")), hx(pack("Z5", "xy")), hx(pack("Z5", "abcdefg")), hx(pack("a*", "hey")), hx(pack("Z*", "hey")), hx(pack("A2 a2", "abcd", "efgh")), hx(pack("a0", "x")));
show("bits", hx(pack("b8", "10110000")), hx(pack("B8", "10110000")), hx(pack("b*", "1011000011")), hx(pack("B*", "1011000011")), hx(pack("h4", "1a2b")), hx(pack("H4", "1a2b")), hx(pack("H*", "deadBEEF0")), hx(pack("H3", "abc")), hx(pack("h", "f")));
show("ints", hx(pack("c C", -1, 255)), hx(pack("s S", -2, 65534)), hx(pack("l L", -3, 4000000000)), hx(pack("q Q", -4, 5)), hx(pack("n N", 258, 16909060)), hx(pack("v V", 258, 16909060)), hx(pack("s> l< L>", 1, 2, 3)), hx(pack("j J", -1, 1)), hx(pack("i I", -5, 6)));
show("rep", hx(pack("C3", 1, 2, 3)), hx(pack("C*", 65 .. 70)), hx(pack("n*", 1, 2)), hx(pack("(C n)2", 1, 2, 3, 4)), hx(pack("(a2 C)*", "ab", 1, "cd", 2)), hx(pack("C x2 C", 1, 2)), hx(pack("C3 X C", 1, 2, 3, 9)), hx(pack('C @4 C', 1, 2)), hx(pack("C x!4 C", 1, 2)), hx(pack("C[2] a[3]", 7, 8, "zz")));
show("float", hx(pack("f", 1.5)), hx(pack("d", -2.25)), hx(pack("d>", 1)), hx(pack("F", 0.5)), unpack("f", pack("f", 1.5)), unpack("d", pack("d", 3.141592653589793)), join(",", unpack("f2 d", pack("f2 d", 0.25, -8, 1e100))));
show("uni", hx(pack("W", 200)), hx(pack("w", 300)), hx(pack("w*", 0, 127, 128, 16384)), join(",", unpack("w*", pack("w*", 0, 127, 128, 16384, 4000000000))));
show("slash", hx(pack("n/a*", "hello")), hx(pack("C/a* C/a*", "ab", "cde")), hx(pack("N/A*", "x")), hx(pack("C/Z*", "zz")), join(",", unpack("n/a* C", pack("n/a* C", "hello", 7))), join(",", unpack("(C/a*)*", pack("(C/a*)*", "a", "bcd", ""))));
# unpack
show("ua", unpack("a3 A3 Z3", "abcdef   x\0y"), unpack("A*", "trail  \0 "), unpack("Z*", "zed\0junk"), hx(unpack("a*", "al\0l")), unpack("a2 x2 a2", "abXXcd"), unpack("a2 X2 a3", "hello"), unpack("\@2 a2", "abcdef"));
show("ub", unpack("b8", "\x0d"), unpack("B8", "\x0d"), unpack("b*", "\x01\x80"), unpack("B10", "\xff\xc0"), unpack("h*", "\x1a\x2b"), unpack("H*", "\x1a\x2b"), unpack("H3", "\xab\xcd"));
show("ui", unpack("c C", "\xff\xff"), unpack("s S", "\xfe\xff\xfe\xff"), unpack("l L", "\xfd\xff\xff\xff\x00\x28\x6b\xee"), unpack("n N v V", "\x01\x02\x01\x02\x03\x04\x02\x01\x04\x03\x02\x01"), unpack("q", pack("q", -4)), unpack("Q", pack("Q", 5)), unpack("s> l<", "\x00\x01\x02\x00\x00\x00"), unpack("n!", "\xff\xfe"));
show("ur", unpack("C*", "ABC"), unpack("n*", "\0\1\0\2\0"), unpack("(C n)2", "\1\0\2\3\0\4"), unpack("(a1 C)*", "a\1b\2"), unpack("C3", "\1\2"), unpack("W*", "\xc8A"));
my ($first) = unpack("N", "\0\0\1\0"); my $sc = unpack("n n", "\0\5\0\6"); my @none = unpack("N", "ab"); show("ctx", $first, $sc, scalar(@none), scalar(unpack("a2", "xyz")));
{ local $_ = "\x41\x42"; my @d = unpack("C*"); show("dflt", @d); }
show("round", join(",", unpack("A3 n C a*", pack("A3 n C a*", "ab", 513, 9, "rest"))), hx(pack("J", 9223372036854775807)), unpack("J", pack("J", 18446744073709551615)) == 18446744073709551615 ? "u64 ok" : "u64 bad", hx(pack("N", -1)), hx(pack("C", 300 & 255)));
show("ws", hx(pack(" C  # one byte\n n ", 1, 2)), length(pack("x5")), hx(pack("a", "")));
for my $bad ("y", "(C") { my $r = eval { my $x = pack($bad, 1, 2); 1 }; show("err $bad", $r ? "ok" : ($@ =~ /^(.*?) at /)[0]); }
{ my $r = eval { my $x = pack("C/", 1); 1 }; show("err slash", ($@ =~ /^(.*?) at /)[0]); }
# U は文字コード。picoperl の文字列は UTF-8 のバイト列なので、文字列として扱う perl と同じ結果になる
{
    my $a = "\xe3\x81\x82"; utf8::decode($a);                      # "あ"
    my $mix = "A\xc3\xa9\xf0\x9f\x98\x80"; utf8::decode($mix);     # "Aé😀"
    show("U", (pack("U", 0x3042) eq $a ? "pack U" : "differs"), join(",", unpack("U*", $a . "A")), (pack("U*", 65, 0xe9, 0x1f600) eq $mix ? "pack U*" : "differs"));
}
print "done\n";
