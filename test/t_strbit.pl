use strict; use warnings;
# & | ^ は、両方が文字列ならバイト単位の演算 (数が混じれば数値の演算)
print "ab" | "  ", "|", "AB" ^ "  ", "|", "abc" & "a", "|", length("abcd" | "x"), "|", "12" & 3, "|", 12 & "3", "|", 6 | 9, "|", unpack("H*", "a" ^ "b"), "|", unpack("H*", "\xf0\x0f" & "\x3c\x3c"), "\n";
my $x = "AB"; $x |= "  "; my $y = "abc"; $y &= "ab"; my $z = "ab"; $z ^= "ab"; my $w = 12; $w &= 10; my $v = "12"; $v |= 1;
print "$x $y ", unpack("H*", $z), " $w $v\n";
# この性質で「数として作られた値か、文字列か」を見分けられる (JSON::PP の数値判定)
{ no warnings;
  my $n = 5; my $u = "$n"; my $len = length("abc"); my $div = 10 / 4;
  print join(",", map { length((my $d = "") & $_) ? "num" : "str" } $n, "5", $u, 1.5, "x", 0, "0", 3 + 0, "1" . "2", $len, $div, -7, "-7"), "\n"; }
# マスクとして使う例
my $mask = "\xff\x00\xff"; print unpack("H*", "abc" & $mask), " ", unpack("H*", "\x01\x02" | "\x10\x20\x30"), "\n";
{ use v5.28; no warnings;       # bitwise 機能の下では、文字列どうしでも数値の演算
  print "feature: ", "12" & "10", " ", 6 ^ "3", "\n"; }
