use strict; no warnings;
my $s = "Hello, World";
print length($s), " ", uc $s, " ", lc($s), " ", ucfirst(lc $s), " ", lcfirst($s), "\n";
print substr($s, 0, 5), "|", substr($s, 7), "|", substr($s, -5), "|", substr($s, -5, 2), "\n";
my $t = $s; substr($t, 0, 5, "Howdy"); print "$t\n";
print index($s, "World"), " ", index($s, "o"), " ", index($s, "o", 5), " ", rindex($s, "o"), " ", index($s, "zzz"), "\n";
print join("|", split(/,/, "a,b,c")), " ", join("|", split(",", "a,b,,c,,")), " ", join("|", split(' ', "  a  b c ")), "\n";
print join("|", split(/\s+/, "a  b\tc")), " ", join("|", split(//, "abc")), " ", join("|", split(",", "a,b,c,d", 2)), "\n";
print join(",", map { ord } split //, "AZ"), " ", chr(65), chr(0x263A) ne "" ? "ok" : "", "\n";
print sprintf("%d %5d %-5d| %05d %+d %x %X %o %b %e %.3f %g %s %5s %-5s| %%", 42, 42, 42, 42, 42, 255, 255, 8, 5, 1234.5, 3.14159, 0.0001, "str", "ab", "ab"), "\n";
print sprintf("%3\$s %1\$s %2\$s", "a", "b", "c") eq "c a b" ? "" : "", sprintf("%.2s|%8.3f|%-8.2e|%c", "abcdef", 3.14159, 1234.5, 65), "\n";
print sprintf("%*d|%-*d|%.*f", 5, 1, 5, 2, 2, 3.14159), "\n";
print sprintf("%s", 1e21), " ", sprintf("%d", "12abc"), " ", sprintf("%d", 3.99), " ", sprintf("%.0f", 2.5), " ", sprintf("%d", -0.5), "\n";
print sprintf("%#x %#o %#b %08.3f %+.2f", 255, 8, 5, 3.14159, 2), "\n";
my $x = "a"; $x .= "b" for 1..3; print "$x\n";
print "repeat: ", "=" x 5, "\n";
print "chained " . "concat " . 1 + 2, "\n";
my $str = "line\n"; chomp $str; print "[$str]"; chomp $str; print "[$str]"; chop $str; print "[$str]\n";
my @lines = ("a\n", "b\n"); chomp @lines; print "@lines\n";
print "interp: @{[ 1 + 2 ]} ${\ 'x'} $s\n";
my @arr = (1, 2, 3); my %h = (k => "v"); my $r = { a => [10, 20] };
print "arr=@arr first=$arr[0] last=$arr[-1] h=$h{k} h2=$h{'k'} r=$r->{a}[1] r2=$$r{a}[0] n=${\ scalar @arr}\n";
my $name = "x"; print "${name}yz $name-1 $name.txt\n";
print "email\@example.com \$notvar \\backslash \"quoted\" tab\tend\n";
print 'single $name @arr \n', "\n";
print "hex \x41\x{42} oct \101 esc\e[0m", "\n";
print q(paren (nested) string), " ", qq{brace $name}, " ", join(",", qw/a b c/), "\n";
print lc("ÀB"), "\n";
print "abc" lt "abd" ? "lt" : "ge", " ", "10" == 10.0 ? "numeq" : "numne", " ", "abc" x -1, "|\n";
print ucfirst("élan"), " ", length("日本語"), "\n";
print join(",", sort { lc($a) cmp lc($b) } qw(b A c)), "\n";
print reverse("abc"), " ", scalar reverse("abc"), "\n";
print "num to str: ", 1.0, " ", 1.50, " ", 100000000000000000000, " ", 0.000001, " ", 1/7, "\n";
print "str to num: ", "1.5e2" + 0, " ", " 42 " + 0, " ", "0x10" + 0, " ", "1_000" + 0, " ", ".5" + 0, " ", "-3" * 2, "\n";
print "cmp: ", (2 <=> 10), " ", ("2" cmp "10"), "\n";
print "join nums: ", join(",", 1.5, 2, "3"), "\n";
print "sprintf list: ", sprintf("%s-%s", split(/,/, "a,b")), "\n";
print "quotemeta: ", quotemeta("a.b*c"), "\n";
print "hex/oct: ", hex("ff"), " ", hex("0xFF"), " ", oct("755"), " ", oct("0x1f"), " ", oct("0b101"), "\n";
print "lc/uc default: "; $_ = "MiXed"; print lc, " ", uc, " ", length, "\n";
print "ord/chr: ", ord("a"), " ", chr(97), " ", ord(""), "\n";
print "x" x 3 . "y", "\n";
print "abc" . 5 + 3, "\n";
print "multi
line\n";
# substr への代入
my $ls = "hello world"; substr($ls, 0, 1) = "J"; substr($ls, 6) = "there"; print "lvalue substr: $ls\n";
my $ls2 = "-details"; substr($ls2, 0, 1) = '' if 1; substr($ls2, -1, 1) = "S!"; print "lvalue substr 2: $ls2\n";
my %lsh = (k => "abcdef"); substr($lsh{k}, 2, 2) = "XY"; my @lsa = ("12345"); substr($lsa[0], 1, 3) = ""; print "lvalue substr 3: $lsh{k} $lsa[0]\n";
# 空白や記号を含む、引用符付きのヒアドキュメントの区切り
my $hd_v = "v";
print <<"    EOT";
indented delimiter $hd_v
    EOT
print <<'-- end --';
symbols in delimiter $hd_v
-- end --
# 空文字列の split は、制限数にかかわらず要素なし
print "split empty: ", join("", map { scalar(@$_) } [split /,/, "", -1], [split /,/, ""], [split ",", "", 5], [split //, ""], [split /,/, "a", -1], [split " ", ""], [split /,/, ",", -1]), "\n";
# \U \L \u \l \Q \E
my $cm = "msWin"; my @cl = ("ab", "cd"); my %ch = (k => "val");
print "case mods: _IS_\U$cm\E_end \LABC Def\E \uabc \lABC \u\L$cm\E! \Ua${cm}b\E \U@cl\E [\Qa.b*$cm\E] \u$cl[0]\n";
print "case mods 2: \Uno end $cm\n";
print "case mods 3: \u$ch{k} \U$ch{k}\E-\l\U$cm\E x\\Uy \$cm\n";
my $cmh = <<"EOT";
heredoc \U$cm\E and \u$ch{k}
EOT
print "case mods heredoc: $cmh";
print "end\n";
