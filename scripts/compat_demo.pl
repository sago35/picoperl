# 互換性まわりの実機確認: 正規表現 (後読み、アトミックグループ)、tie、演算子オーバーロードなど
#
# シリアルで Enter を送るたびに一通り試して結果を表示します。
# 各行が "ok" なら期待どおり、"NG" なら食い違いです。ドライブには何も書きません。
use strict;
use warnings;

my ($ok, $ng);
sub check {
    my ($name, $got, $want) = @_;
    if ($got eq $want) { $ok++; print "ok   $name\n" }
    else { $ng++; print "NG   $name: got [$got], want [$want]\n" }
}

# --- tie 用のクラス
package Upper;      # 代入した値を大文字にして返すスカラー
sub TIESCALAR { bless { v => '' }, shift }
sub FETCH { uc $_[0]{v} }
sub STORE { $_[0]{v} = $_[1] }

package CountHash;  # 読み書きの回数を数えるハッシュ
sub TIEHASH  { bless { d => {}, n => 0 }, shift }
sub FETCH    { $_[0]{n}++; $_[0]{d}{$_[1]} }
sub STORE    { $_[0]{n}++; $_[0]{d}{$_[1]} = $_[2] }
sub EXISTS   { exists $_[0]{d}{$_[1]} }
sub DELETE   { delete $_[0]{d}{$_[1]} }
sub CLEAR    { %{$_[0]{d}} = () }
sub FIRSTKEY { my @k = sort keys %{$_[0]{d}}; $k[0] }
sub NEXTKEY  { my @k = sort keys %{$_[0]{d}}; for my $i (0 .. $#k) { return $k[$i + 1] if $k[$i] eq $_[1] } undef }

package Ring;       # 3 要素しか持たない配列 (添字は 3 で割った余り)
sub TIEARRAY  { bless { a => [0, 0, 0] }, shift }
sub FETCH     { $_[0]{a}[$_[1] % 3] }
sub STORE     { $_[0]{a}[$_[1] % 3] = $_[2] }
sub FETCHSIZE { 3 }
sub CLEAR     { @{$_[0]{a}} = (0, 0, 0) }

package Capture;    # print された内容をためるハンドル
sub TIEHANDLE { bless { out => [] }, shift }
sub PRINT     { my $s = shift; push @{$s->{out}}, join("", @_); 1 }
sub PRINTF    { my $s = shift; my $f = shift; push @{$s->{out}}, sprintf($f, @_); 1 }
sub READLINE  { "line from tie\n" }

# --- オーバーロード用のクラス
package Vec2;
use overload fallback => 1,
    '+'  => sub { Vec2->new($_[0]{x} + $_[1]{x}, $_[0]{y} + $_[1]{y}) },
    '-'  => sub { my ($a, $b, $sw) = @_; $b = Vec2->new($b, $b) unless ref $b; ($a, $b) = ($b, $a) if $sw; Vec2->new($a->{x} - $b->{x}, $a->{y} - $b->{y}) },
    '*'  => sub { ref $_[1] ? $_[0]{x} * $_[1]{x} + $_[0]{y} * $_[1]{y} : Vec2->new($_[0]{x} * $_[1], $_[0]{y} * $_[1]) },
    '==' => sub { $_[0]{x} == $_[1]{x} && $_[0]{y} == $_[1]{y} },
    '""' => sub { "($_[0]{x}, $_[0]{y})" },
    'bool' => sub { $_[0]{x} || $_[0]{y} },
    'neg'  => sub { Vec2->new(-$_[0]{x}, -$_[0]{y}) },
    'abs'  => sub { sqrt($_[0]{x} ** 2 + $_[0]{y} ** 2) };
sub new { my ($c, $x, $y) = @_; bless { x => $x, y => $y }, $c }

package Ver;        # <=> だけ: 残りの比較はそこから作られる
use overload fallback => 1, '<=>' => sub { my ($a, $b, $sw) = @_; my $r = $a->{n} <=> (ref $b ? $b->{n} : $b); $sw ? -$r : $r }, '""' => sub { "v$_[0]{n}" };
sub new { bless { n => $_[1] }, $_[0] }

package StubPkg;
BEGIN { no strict 'refs'; *is_pos = \&is_positive }     # 定義より前に別名を付ける
sub is_positive { $_[0] > 0 ? "pos" : "not" }
sub length { "mine" }

package OurA; our @list = ("a");
package OurB; our @list; push @list, "b";

package main;

sub run {
    ($ok, $ng) = (0, 0);
    print "-- start --\n";

    # 正規表現
    check("lookbehind (?<=)",      join(",", "price: \$42 and 17" =~ /(?<=\$)(\d+)/g), "42");
    check("lookbehind (?<!)",      join(",", "price: \$42 and 17" =~ /(?<!\$)\b(\d+)/g), "17");
    check("lookbehind, 2 lengths", join(",", "ab12cd" =~ /(?<=a|\d\d)(\w)/g), "b,c");
    check("lookbehind in s///",    do { (my $t = "aXbXXc") =~ s/(?<=X)(?!X)/_/g; $t }, "aX_bXX_c");
    check("atomic group fails",    ("aaab" =~ /^(?>a+)ab/ ? 1 : 0), 0);
    check("atomic group matches",  ('"x\"y" z' =~ /^"((?>[^\\"]*(?:\\.[^\\"]*)*))"/ ? $1 : "-"), 'x\"y');
    check("/x comment, no interp", ("ab" =~ /^ a   # $nosuch is only a comment
                                              b /x ? 1 : 0), 1);
    check("[[:ascii:]]",           ("abc" =~ /^[[:ascii:]]+$/ ? 1 : 0) . ("a\xC3\xA9" =~ /[[:^ascii:]]/ ? 1 : 0), "11");

    # tie: スカラー
    my $s; tie $s, 'Upper';
    $s = "hello"; check("tie scalar",           $s, "HELLO");
    $s .= " pico"; check("tie scalar .=",        $s, "HELLO PICO");
    check("tied returns the object",            ref(tied $s), "Upper");
    untie $s; check("untie",                    (defined(tied $s) ? "tied" : "untied"), "untied");

    # tie: ハッシュ
    my %h; my $ho = tie %h, 'CountHash';
    $h{b} = 2; $h{a} = 1; $h{a} += 10;
    check("tie hash store/fetch",  "$h{a} $h{b}", "11 2");
    check("tie hash keys",         join(",", sort keys %h), "a,b");
    check("tie hash exists",       (exists $h{a} ? 1 : 0) . (exists $h{z} ? 1 : 0), "10");
    check("tie hash delete",       delete($h{b}) . " left " . join(",", keys %h), "2 left a");
    check("tie hash counted",      ($ho->{n} > 0 ? "counted" : "not"), "counted");
    %h = (x => 1, y => 2); check("tie hash assign", join(",", map { "$_=$ho->{d}{$_}" } sort keys %{$ho->{d}}), "x=1,y=2");

    # tie: 配列
    my @r; tie @r, 'Ring';
    $r[0] = "a"; $r[4] = "b"; $r[8] = "c";
    check("tie array",             "@r", "a b c");
    check("tie array size",        scalar(@r), 3);
    check("tie array wraps",       $r[3] . $r[7], "ab");

    # tie: ハンドル
    my $cap = tie *CAP, 'Capture';
    print CAP "one", "two"; printf CAP "%03d", 7; my $glob = \*CAP; print $glob "three";
    check("tie handle print",      join("|", @{$cap->{out}}), "onetwo|007|three");
    my $line = <CAP>; chomp $line; check("tie handle readline", $line, "line from tie");
    { my $o = tie *STDOUT, 'Capture'; print "captured"; my @got = @{$o->{out}}; undef $o; untie *STDOUT; check("tie STDOUT", "@got", "captured"); }

    # オーバーロード
    my ($va, $vb) = (Vec2->new(1, 2), Vec2->new(3, 4));
    check("overload +",            $va + $vb, "(4, 6)");
    check("overload - swapped",    10 - $va, "(9, 8)");
    check("overload * (two kinds)", ($va * 3) . " " . ($va * $vb), "(3, 6) 11");
    check("overload ==",           ($va == Vec2->new(1, 2) ? 1 : 0) . ($va == $vb ? 1 : 0), "10");
    check("overload neg, abs",     (-$va) . " " . abs(Vec2->new(3, 4)), "(-1, -2) 5");
    check("overload bool",         (Vec2->new(0, 0) ? "t" : "f") . ($va ? "t" : "f"), "ft");
    my $acc = Vec2->new(0, 0); $acc += $va for 1 .. 3; check("overload += via +", $acc, "(3, 6)");
    check("overload in string",    "v=$va " . join(";", $va, $vb), "v=(1, 2) (1, 2);(3, 4)");
    my %byname = ($va => "first"); check("overload as hash key", join(",", keys %byname), "(1, 2)");
    my @vers = map { Ver->new($_) } 3, 1, 2;
    check("overload sort by <=>",  join(" ", sort { $a <=> $b } @vers), "v1 v2 v3");
    check("overload < > from <=>", join("", map { $_ ? 1 : 0 } $vers[0] > $vers[1], $vers[1] < 2, 5 <= $vers[0], $vers[2] == 2), "1101");
    check("overload::StrVal",      (overload::StrVal($va) =~ /^Vec2=HASH\(0x[0-9a-f]+\)$/ ? "plain" : "other"), "plain");

    # その他
    check("sub ref before its definition", StubPkg::is_pos(3) . " " . StubPkg::is_pos(-1), "pos not");
    check("CORE::length",                  StubPkg::length("abcd") . " " . CORE::length("abcd"), "mine 4");
    check("our again in another package",  "@OurA::list|@OurB::list", "a|b");
    my $aref = [1, 2, 3]; check('"@$ref" in a string', "<@$aref>", "<1 2 3>");
    my $flag = not 0; check("not as a term", ($flag ? 1 : 0), 1);
    check("-t on STDIN is defined",        (defined(-t STDIN) ? 1 : 0), 1);

    print "-- done: $ok ok, $ng NG --\n\n";
}

print "compat demo: press Enter to run the checks\n";
while (defined(my $l = <STDIN>)) { run() }
