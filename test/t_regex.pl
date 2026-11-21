# regular expressions: m//, s///, tr///, qr//, split, captures, pos
use strict; use warnings;
$| = 1;
my $s = "Hello World 2026";
print "1: ", ($s =~ /World/ ? "yes" : "no"), "\n";
print "2: ", ($s =~ /world/i ? "yes" : "no"), "\n";
print "3: ", ($s !~ /xyz/ ? "yes" : "no"), "\n";
if ($s =~ /(\w+)\s+(\w+)\s+(\d+)/) { print "4: $1|$2|$3|$&\n"; }
my @m = $s =~ /(\w)(\w)/g;
print "5: @m\n";
my @w = $s =~ /\w+/g;
print "6: @w\n";
my $t = "a1b22c333";
while ($t =~ /(\d+)/g) { print "7: $1 pos=", pos($t), "\n"; }
(my $u = $s) =~ s/o/0/g;
print "8: $u\n";
my $n = ($u = $s) =~ s/l/L/g;
print "9: $n $u\n";
my $v = "abc";
$v =~ s/(\w)/<$1>/g; print "10: $v\n";
my $w = "3 4 5"; $w =~ s/(\d+)/$1*2/ge; print "11: $w\n";
print "12: ", join("|", split(/,\s*/, "a, b,c,,d")), "\n";
print "13: ", join("|", split(//, "abc")), "\n";
print "14: ", join("|", split(/(-)/, "a-b-c")), "\n";
print "15: ", join("|", split(/x*/, "axxbc")), "\n";
print "16: ", join("|", split(' ', "  a  b c ")), "\n";
print "17: ", join("|", split(/,/, "a,b,,,")), "\n";
print "18: ", join("|", split(/,/, "a,b,,,", -1)), "\n";
print "19: ", join("|", split(/,/, "a,b,c,d", 2)), "\n";
my $x = "hello"; (my $y = $x) =~ tr/a-z/A-Z/; print "20: $y ", ($x =~ tr/l//), "\n";
my $z = "aabbcc"; $z =~ tr/a-c//s; print "21: $z\n";
my $q = "hello world"; $q =~ tr/a-zA-Z//cd; print "22: $q\n";
print "23: ", "foo123" =~ /^foo\d+$/ ? 1 : 0, "\n";
my $re = qr/(\d+)-(\d+)/;
print "24: $re\n";
print "25: $1 $2\n" if "10-20" =~ $re;
print "26: $1\n" if "x 10-20 y" =~ /x $re y/;
my $name = "Wor";
print "27: ", ($s =~ /$name/ ? 1 : 0), ($s =~ /^$name/ ? 1 : 0), "\n";
my $meta = "a.c"; print "28: ", ("abc" =~ /^\Q$meta\E$/ ? 1 : 0), ("a.c" =~ /^\Q$meta\E$/ ? 1 : 0), "\n";
print "29: ", join(",", "2026-08-31" =~ /^(\d{4})-(\d{2})-(\d{2})$/), "\n";
print "30: ", ("aaa" =~ /^a{2,3}$/ ? 1 : 0), ("aaaa" =~ /^a{2,3}$/ ? 1 : 0), ("a" =~ /^a{2,}$/ ? 1 : 0), "\n";
print "31: ", ("foobar" =~ /foo(?=bar)/ ? "$&" : "no"), " ", ("foobaz" =~ /foo(?!bar)/ ? "$&" : "no"), "\n";
print "32: ", ("abcabc" =~ /(abc)\1/ ? 1 : 0), " ", ("abcABC" =~ /(abc)\1/i ? 1 : 0), "\n";
print "33: ", ("a\nb" =~ /^b/ ? 1 : 0), ("a\nb" =~ /^b/m ? 1 : 0), ("a\nb" =~ /a.b/ ? 1 : 0), ("a\nb" =~ /a.b/s ? 1 : 0), "\n";
print "34: ", ("x" =~ / x # comment
   /x ? 1 : 0), "\n";
my $str = "key=value; k2=v2";
my %h = $str =~ /(\w+)=(\w+)/g;
print "35: ", join(",", map { "$_=$h{$_}" } sort keys %h), "\n";
print "36: ", join("|", "aXbXc" =~ /([^X]+)/g), "\n";
my $cnt = () = "abcabc" =~ /b/g; print "37: $cnt\n";
$_ = "topic"; print "38: ", (/top/ ? 1 : 0), "\n"; s/top/bot/; print "39: $_\n";
my @list = grep { /^a/ } qw(apple banana avocado); print "40: @list\n";
my @up = map { s/a/A/gr } qw(banana); print "41: @up\n";
print "42: ", ("aaa" =~ /a*?/ ? length($&) : -1), ("aaa" =~ /a+?/ ? length($&) : -1), "\n";
print "43: ", ("The quick" =~ /\bq/ ? 1 : 0), ("The quick" =~ /\Bq/ ? 1 : 0), "\n";
print "44: ", ("ab" =~ /a|b/ ? "$&" : ""), ("cab" =~ /(a|b)+/ ? "$&" : ""), "\n";
print "45: ", ("[x]" =~ /\[(.)\]/ ? "$1" : ""), ("a/b" =~ m{a/(b)} ? "$1" : ""), "\n";
my $p = "x"; $p =~ s{x}{y}; print "46: $p\n";
print "47: ", ("abc" =~ /[[:alpha:]]+/ ? "$&" : ""), ("a1" =~ /[[:digit:]]/ ? "$&" : ""), "\n";
print "48: ", ("" =~ /^$/ ? 1 : 0), ("\n" =~ /^$/ ? 1 : 0), ("a\n" =~ /a$/ ? 1 : 0), ("a\n" =~ /a\z/ ? 1 : 0), "\n";
print "49: ", ("aaa" =~ /^(a*)*$/ ? 1 : 0), ("ab" =~ /^(a?)*b$/ ? 1 : 0), "\n";
my $c = "a,b;c"; my @parts = split /[,;]/, $c; print "50: @parts\n";
# --- part 2 ---
{
my %h = (s => 1, y => 2, m => 3, tr => 4, qr => 5);
print "B1: $h{s} $h{y} $h{m} $h{tr} $h{qr}\n";
my @a = sort { $a <=> $b } (3, 1, 2); print "2: @a\n";
my $x = 10; my $y = 5; print "3: ", $x / $y, " ", $x/$y, " ", ($x) / 2, "\n";
sub half { return $_[0] / 2 } print "4: ", half(8), "\n";
my $str = "a.b.c"; my @p = split /\./, $str; print "5: @p\n";
my @q = split '\.', $str; print "6: @q\n";
my @r = split /\|/, "a|b"; print "7: @r\n";
print "B8: ", ("path/to/file" =~ m'to/(\w+)' ? $1 : ""), "\n";
my $v = "aXbXc"; (my $w = $v) =~ s'X'-'g; print "9: $w\n";
my @arr = ("x", "y"); print "10: ", ("a x y b" =~ /a @arr b/ ? 1 : 0), "\n";
my $t = "2+3"; $t =~ s/(\d)\+(\d)/$1 + $2/e; print "11: $t\n";
sub up { uc $_[0] } my $u = "abc"; $u =~ s/(b)/up($1)/e; print "12: $u\n";
my $k = "abab"; print "13: ", ($k =~ /^(ab)+$/ ? "$1" : ""), ($k =~ /^(?:ab){2}$/ ? 1 : 0), "\n";
print "B14: ", ("a{b" =~ /a\{b/ ? 1 : 0), ("x{2}" =~ /x\{2\}/ ? 1 : 0), "\n";
my $n = 2; print "15: ", ("xx" =~ /^x{$n}$/ ? 1 : 0), ("1" =~ /^$h{s}$/ ? 1 : 0), "\n";
my $obj = bless {}, 'Foo'; sub Foo::s { "method-s" } sub Foo::y { "method-y" } print "16: ", $obj->s, " ", $obj->y, "\n";
my $und; my $c = ($und =~ /x/) ? 1 : 0; print "17: $c\n";
print "B18: ", join("|", "a1b2" =~ /(\d)/g), " ", scalar(my @e = "abc" =~ /\d/g), "\n";
my $g = "aaa"; my $count = 0; $count++ while $g =~ /a/g; print "19: $count\n";
my $line = "  trim me  "; $line =~ s/^\s+|\s+$//g; print "20: [$line]\n";
print "B21: ", ("Hello" =~ /^[A-Z][a-z]+$/ ? 1 : 0), ("hello" =~ /^[^a-c]/ ? 1 : 0), ("-" =~ /^[a\-z]$/ ? 1 : 0), ("]" =~ /^[]a]$/ ? 1 : 0), "\n";
my $re = qr/b+/i; my @all = ("aBbc" =~ /$re/g); print "22: @all ", ref($re), "\n";
my @names = ("x=1", "y=2"); my @vals = map { /=(\d)/ ? $1 : () } @names; print "23: @vals\n";
print "B24: ", ("foo\n" =~ /^foo$/ ? 1 : 0), ("foo\nbar" =~ /^foo$/ ? 1 : 0), ("foo\nbar" =~ /^bar\Z/ ? 1 : 0), "\n";
my $dna = "AACCGGTT"; (my $rc = reverse $dna) =~ tr/ACGT/TGCA/; print "25: $rc\n";
my $s2 = "hello world"; my $nvow = ($s2 =~ tr/aeiou//); print "26: $nvow ", ($s2 =~ tr/a-z/A-Z/r), "\n";
print "B27: ", join(",", map { sprintf "%02d", $_ } "1:2:3" =~ /(\d)/g), "\n";
my $text = "one two  three"; my @f = split ' ', $text; my @f2 = split / /, $text; print "28: ", scalar(@f), " ", scalar(@f2), "\n";
print "B29: ", ("ab" =~ /^(a)(x)?(b)$/ ? (defined $2 ? "def" : "undef") : "no"), " $3\n";
my @caps = "ab" =~ /^(a)(x)?(b)$/; print "30: ", scalar(@caps), " ", defined($caps[1]) ? 1 : 0, "\n";
print "B31: ", "abc" =~ /B/i ? "yes" : "no", "\n";
}
# s///e: 置換式の中の pos() は一致の開始位置、置換式には複数の文を書ける
my $pe = "aXbXc"; $pe =~ s/X/pos($pe)/ge; $_ = "aXbX"; s/X/pos()/ge;
my $pt = "a\tb"; $pt =~ s/(\t)/"[" . pos($pt) . "]"/e;
print "pos in s///e: $pe $_ $pt\n";
$_ = "a\tb\tc"; my $po = 0; s{\t}{ my $p = 8 - ((pos() + $po) % 8); $po += $p - 1; " " x $p }ge;
my $pb = "q.r"; $pb =~ s{\.}{ "{" . 1 . "}" }e; my $pm = "n=5"; $pm =~ s/(\d+)/my $d = $1 * 2; $d + 1/e;
print "statements in s///e: [$_] $pb $pm\n";
# Unicode プロパティ (ASCII の範囲)
print "properties: ", join("", map { $_ ? 1 : 0 } "a1 b" =~ /^\pL/, "a1 b" =~ /\pN/, "a" =~ /\pM/, "a" =~ /\PM/, "ab" =~ /^\p{Lower}+$/, "AB" =~ /^\p{Lu}+$/, " " =~ /\p{Space}/,
    "A-b" =~ /^[\p{Lu}\-\pL]+$/, "x" =~ /\p{^L}/, "x" =~ /\P{L}/, "a.b" =~ /\pP/, "a+b" =~ /\pS/, "a_1" =~ /^\p{Word}+$/, "ff" =~ /^\p{XDigit}+$/), "\n";
my $pcount = () = "ab cd" =~ /\PM/g; my @pwords = "one two3" =~ /(\p{Alpha}+)/g;
print "property counts: $pcount @pwords\n";
my $punknown = "\\p{NoSuchProperty}"; eval { my $r = "x" =~ /$punknown/ }; print "unknown property: ", ($@ ? "dies" : "ok"), "\n";
# (?>...) アトミックグループ: 中に戻ってやり直さない。中で取ったグループは、外で後戻りしたら取り消される
for my $t ('aaab', 'aaa', '"x\"y" z', 'foobar', 'abcabc') {
    my @r;
    push @r, ($t =~ /^(?>a+)ab/ ? 1 : 0), ($t =~ /^(?>a*)b/ ? 1 : 0);
    push @r, ($t =~ /^"((?>[^\\"]*(?:\\.[^\\"]*)*))"/ ? "<$1>" : '-');
    push @r, ($t =~ /(?>foo|foob)ar/ ? 1 : 0), ($t =~ /^(?>(a)bc|x)+$/ ? "<$1>" : '-');
    push @r, ($t =~ /(?:(?>(a))x|a(b))/ ? (defined $1 ? "d" : "u") . ($2 // '') : '-');
    print "atomic $t: @r\n";
}
# /x のコメントの中は変数として読まない (strict でもエラーにならない)。[#] と \# はコメントではない
{
    use strict;
    my $xs = "ab # x"; my $xd = "b";
    print "x comments: ", join("", map { $_ ? 1 : 0 }
        scalar($xs =~ /^ a   # $nosuch @nosuch
                        b \ \# [ #] x # $more
                      /x),
        scalar($xs =~ / a (?-x:$xd)  # $nope
                      /x),
        scalar("a#b" =~ /a [#] b/x), scalar("a]#b" =~ /a []#]+ b # $no
                      /x)), "\n";
}
# (?^aa:...) などの文字セット系フラグ (a u l d) と p は受け付けて無視する
print "charset flags: ", join("", map { $_ ? 1 : 0 } "xAy" =~ /(?^aa:A)/, "xay" =~ /(?^aai:A)/, "xay" =~ /(?^u:A)/, "xAy" =~ /(?a)A/, "xAy" =~ /(?^lp:A)/), "\n";
# 後読み (?<=...) (?<!...): 中身の長さに上限があればよい (選択肢ごとに長さが違ってもよい)
for my $s ('foobar', 'price: $42 and 17', 'a^]b]>c', 'x  >> y > z', 'aXbXXc', "l1\nl2\nl3", 'ab12cd', '') {
    my @r;
    push @r, join(",", $s =~ /(?<=foo)(\w+)/g), join(",", $s =~ /(?<!\$)\b(\d+)/g), join(",", $s =~ /(?<!\^)([\]>]+)/g);
    push @r, ($s =~ /(\s+|(?<=\s\s))(>{2,})/ ? "[$1][$2]" : "-"), scalar(() = $s =~ /(?<=X)/g);
    push @r, join(",", $s =~ /(?<=^|\n)(l\d)/g), join(",", $s =~ /(?<=a|bc|\d\d)(\w)/g);
    push @r, ($s =~ /(?<=(\d))c/ ? "c after $1" : "-"), join(",", $s =~ /(?<![a-z]{2})(\d)/g), ($s =~ /(?<=B)1/i ? "i" : "-");
    (my $t = $s) =~ s/(?<=X)(?!X)/_/g; (my $u = $s) =~ s/(?<!\d)(\d)(?!\d)/<$1>/g;
    push @r, $t, $u, join("+", split /(?<=,)|(?<=\d)(?=[a-z])/, $s);
    push @r, ($s =~ /(?<=(?<!o)o)b/ ? "nest" : "-"), ($s =~ /\b(?<!x)y/ ? "y" : "-");
    print "lookbehind: ", join(" | ", @r), "\n";
}
for my $p ('(?<=a+)b', '(?<=a{1,300})b', '(?<=\1)b', '(?<=a*)b') { my $r = eval { "aab" =~ /$p/ ? "m" : "n" }; print "lookbehind $p: ", (defined $r ? $r : "dies"), "\n"; }
# ASCII に文字が無いカテゴリ (\p{Zl} など) は何にも一致しない
print "empty properties: ", join("", map { $_ ? 1 : 0 } "a \n" =~ /[\p{Zl}\p{Zp}]/, "a \n" =~ /[\s\t\p{Zl}\p{C}\p{Zp}]/, "aZ" =~ /\p{Lo}/, "a" =~ /\P{Zl}/, "12" =~ /\p{No}/), "\n";
print "posix ascii: ", join("", map { $_ ? 1 : 0 } "abc" =~ /^[[:ascii:]]+$/, "a\xC3\xA9" =~ /[[:^ascii:]]/, "abc" =~ /[[:^ascii:]]/, "a\xC3\xA9" =~ /^[[:ascii:]]+$/), "\n";
# tr の中の 8 進数・16 進数のエスケープ
{
    my $tv = "abc\x01\x7f\xff-z";
    my @tr = (scalar($tv =~ tr/\0-\377//c), scalar($tv =~ tr/\0-\377//), scalar($tv =~ tr/\0-\177//c), scalar($tv =~ tr/\x00-\x1f//), scalar($tv =~ tr/\x{61}-\x{63}//), scalar($tv =~ tr/a\-z//), scalar($tv =~ tr/\\//));
    (my $tw = "a\tb\nc") =~ tr/\011\012/TN/; (my $to = "ABC") =~ tr/\101-\103/\141-\143/;
    print "tr escapes: @tr $tw $to\n";
}
# re::is_regexp と re::regexp_pattern
{
    my $rq = qr/a.b/i; my ($rp, $rf) = re::regexp_pattern($rq);
    print "re:: ", (re::is_regexp($rq) ? 1 : 0), (re::is_regexp("x") ? 1 : 0), " $rp $rf ", scalar(re::regexp_pattern($rq)), "\n";
}
# 変数以外 (引数、要素、$$ref) に対する //g と pos
{
    sub pos_scan { my @at; while ($_[0] =~ /(\d)/g) { push @at, pos($_[0]); last if @at > 9 } "@at" }
    my $ps = "a1b2c3"; my @pa = ("x1y2"); my %ph = (k => "p9q8r7");
    my $pn = 0; while ($pa[0] =~ /\d/g) { $pn++; last if $pn > 9 }
    my @pp; while ($ph{k} =~ /(\d)/g) { push @pp, $1 . "@" . pos($ph{k}); last if @pp > 9 }
    my $pr = \$ps; $$pr =~ /b/g;
    print "pos of non-variables: ", pos_scan("a1b2c3"), " | $pn ", (defined pos($pa[0]) ? "def" : "undef"), " | @pp | ", pos($$pr), " ", pos($ps), "\n";
}
# pos は 1 つの文字列の値に付く: 代入し直せば消え、コピーには引き継がれない
{
my $x = "aXbXc"; $x =~ /X/g; print pos($x), " "; $x = "aXbXc"; print defined pos($x) ? "kept" : "reset", " "; my @p; while ($x =~ /X/g) { push @p, pos($x) } print "@p\n";
my $n = 12345; my $c = 0; while ($n =~ /\d/g) { $c++; last if $c > 9 } print "$c\n";
sub mk { my $t = "bjorn-text"; $t =~ /o/g; return } mk() for 1 .. 3; sub chk { my $u = "bjorn" . "-text"; my @all = $u =~ /(.)/g; scalar @all } print chk(), "\n";
my %h = (k => "p9q8"); my @q; while ($h{k} =~ /(\d)/g) { push @q, $1; last if @q > 9 } print "@q\n";
my $s2 = "abab"; $s2 =~ /b/g; my $copy = $s2; print pos($s2), " ", (defined pos($copy) ? "copy has pos" : "copy fresh"), "\n";
}

# \cX: 制御文字 (\cA = 1、\c? = 127)。文字クラスの中でも使える
{
    my $all = join '', map chr, 0 .. 127;
    my @m = map ord, grep { /[\cA-\cC\c?]/ } split //, $all; print "ctrl class: @m\n";
    print "ctrl atom: ", ("a\tb" =~ /a\cIb/ ? "ok" : "ng"), " ", ("\x1b[" =~ /^\c[\[/ ? "ok" : "ng"), "\n";
    (my $s = "x\x01y\x7f") =~ s/([^\n\t\c?[:^cntrl:][:^ascii:]])/sprintf '\\u%04x', ord $1/eg; print length($s), " ", substr($s, 0, 8), "\n";
}

# \G (前回のマッチの続きに固定) と /c (失敗しても pos を保つ)
{
    my $src = "let x = 42 + y1;"; my @tok;
    for (my $guard = 0; $guard < 50; $guard++) {
        if    ($src =~ /\G\s+/gc)         { }
        elsif ($src =~ /\G([a-z]\w*)/gc)  { push @tok, "id:$1" }
        elsif ($src =~ /\G(\d+)/gc)       { push @tok, "num:$1" }
        elsif ($src =~ /\G([=+;])/gc)     { push @tok, "op:$1" }
        else                              { last }
    }
    print "@tok | pos=", pos($src), "\n";
    my $s = "aaXbb"; $s =~ /a+/g; print "after a+: ", pos($s), "\n";
    print "G fail: ", ($s =~ /\Gb/gc ? "hit" : "miss"), " pos=", pos($s), "\n";
    print "G hit: ", ($s =~ /\GX/gc ? "hit" : "miss"), " pos=", pos($s), "\n";
    print "no c: ", ($s =~ /\GZ/g ? "hit" : "miss"), " pos=", (defined pos($s) ? pos($s) : "undef"), "\n";
    my $t = "112233"; my @pairs; push @pairs, $1 while $t =~ /\G(\d\d)/g; print "@pairs\n";
    my $u = "ab12"; $u =~ /[a-z]+/g; print "no g: ", ($u =~ /\G(\d)/ ? $1 : "none"), "\n";
    pos($u) = 3; print "set pos: ", ($u =~ /\G(\d)/g ? $1 : "none"), "\n";
}

# //g のループの中で、ほかの変数に //g を何度も使っても、外側の位置は保たれる
{
    my $outer = "a b c d"; my @seen; my $guard = 0;
    while ($outer =~ /(\w)/g) {
        last if ++$guard > 20;
        push @seen, $1;
        my ($v1, $v2, $v3, $v4, $v5, $v6) = ("x1", "x2", "x3", "x4", "x5", "x6");
        $_ =~ /x/g for $v1, $v2, $v3, $v4, $v5, $v6;      # 1 回ずつマッチしたまま残る
    }
    print "outer: $guard (@seen)\n";
}

# =~ の後ろなら、; や = や , も正規表現の区切り文字になる
{
    my $opt = "name=s"; my %h = (m => 1, s => 2, y => 3);
    print "semi: ", ($opt =~ m;^(\w+)=(\w)$; ? "$1/$2" : "no"), "\n";
    (my $t = "a=b") =~ s=a=X=; print "eq: $t\n";
    (my $u = "a,b") =~ s,\,,;,; print "comma: $u\n";
    my $v = "x.y"; print "paren: ", ($v !~ m)z) ? "nomatch" : "match"), "\n";
    (my $w = "abc") =~ tr;a-c;A-C;; print "tr: $w\n";
    my @k = sort keys %h; print "keys: @k $h{m}$h{s}$h{y}\n"; my ($m, $s) = (4, 5); print "vars: ", $m + $s, "\n";
    if ($opt =~ m;^
          ( \w+ )      # name
          = ( [si] )   # type
        $;x) { print "multi: $1 $2\n" }
}

# /m の ^ は、文字列の最後の改行の後ろには一致しない
{
    my @t = ("a\n", "a\nb\n", "a\n\n", "", "\n", "a");
    for my $s (@t) { (my $c = $s) =~ s/^/> /mg; (my $v = $c) =~ s/\n/\\n/g; my $n = () = $s =~ /^/mg; print "[$v] $n\n" }
    my @l = split /^/m, "x\ny\n"; print scalar(@l), "\n";
    my $e = "a\nb\n"; my $cnt = () = $e =~ /$/mg; (my $d = $e) =~ s/$/!/mg; $d =~ s/\n/\\n/g; print "$cnt [$d]\n";
    print "end: ", ("a\n" =~ /a\n^/m ? "match" : "no"), " ", ("a\nb" =~ /a\n^b/m ? "match" : "no"), "\n";
}

# 8 進のエスケープ: 文字クラスの中 ([\0-\037\177-\377])、\101 (グループが足りなければ文字)
{
    my $s = "a\x01b\x7fc\xffd e"; (my $t = $s) =~ s/([\0-\037\177-\377])/sprintf("\\x%02x", ord($1))/ge; print "octal class: $t\n";
    print "octal: ", join(",", map { /^[\101-\103]$/ ? 1 : 0 } qw(A C D)), " ", ("\0" =~ /[\0]/ ? 1 : 0), ("a\tb" =~ /a[\011]b/ ? 1 : 0), ("x8" =~ /x[\60-\71]/ ? 1 : 0),
          ("\x01" =~ /\001/ ? 1 : 0), ("A" =~ /\101/ ? 1 : 0), ("ab" =~ /(a)\1?b/ ? 1 : 0), ("aa" =~ /(a)\1/ ? 1 : 0),
          ("abcdefghijkk" =~ /(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)\11/ ? 1 : 0), ("a\x08" =~ /(a)\10/ ? 1 : 0), ("z\x53" =~ /z\123/ ? 1 : 0), "\n";
}
