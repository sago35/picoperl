use strict; use warnings;
# List::Util / Scalar::Util の代用品: 本家 (XS 版) と同じ結果になること
use List::Util qw(shuffle sample uniqstr uniq);
use Scalar::Util qw(looks_like_number openhandle);
print "lln: ", join(",", map { looks_like_number($_) ? 1 : 0 } "Inf", "Infinity", "NaN", "-inf", "+INF", " nan ", "infx", "in", "1e5", "0x10", " 12 ", "abc", "", "infinit"), "\n";
{ my $x = "Inf" + 1; my $y = "-Infinity" * 2; my $z = "nan" + 0; print "inf: $x $y $z\n"; }
# 乱数源を差し替えると、shuffle と sample は本家と同じ並びになる
for my $v (0.4, 0, 0.99, 0.5) {
    local $List::Util::RAND = sub { $v };
    print "rand $v: ", join(",", shuffle(1 .. 10)), " | ", join(",", sample(3, 1 .. 10)), " | ", join(",", sample(20, 1 .. 4)), " | ", join(",", sample(1, 7)), "|", scalar(my @e = sample(2)), "\n";
}
{ my $i = 0; local $List::Util::RAND = sub { ($i++ % 7) / 7 }; print "seq: ", join(",", shuffle("a" .. "h")), " ", join(",", sample(5, "a" .. "h")), "\n"; }
{ my @s = sample 2, 1 .. 5; my @all = sort { $a <=> $b } shuffle(1 .. 6); my %seen; $seen{$_}++ for sample(4, 1 .. 9);
  print "plain: ", scalar(@s), " @all ", scalar(keys %seen), " ", (grep { $_ > 1 } values %seen) ? "dup" : "nodup", "\n"; }
{ no warnings; my @u = uniqstr("a", undef, "", "a", undef); my @v = uniq("a", undef, "", undef);
  print "uniq: ", scalar(@u), "[", join("|", map { defined $_ ? "<$_>" : "undef" } @u), "] ", scalar(@v), "[", join("|", map { defined $_ ? "<$_>" : "undef" } @v), "]\n"; }
open(my $fh, "<", $0) or die; my @r; our $NOSUCH; $NOSUCH = 1;
push @r, defined(openhandle(*STDERR)) ? "glob" : "undef", defined(openhandle(\*STDIN)) ? "ref" : "undef", defined(openhandle("STDIN")) ? "str" : "undef",
         defined(openhandle(\*NOSUCH)) ? "open" : "undef", defined(openhandle([])) ? "arr" : "undef", defined(openhandle({})) ? "hash" : "undef",
         defined(openhandle(undef)) ? "u" : "undef", defined(openhandle($fh)) ? "lex" : "undef", (openhandle($fh) == $fh ? "same" : "diff"),
         defined(openhandle(42)) ? "num" : "undef";
close $fh; push @r, defined(openhandle($fh)) ? "still" : "closed";
open(my $m, "<", \"mem") or die; push @r, defined(openhandle($m)) ? "mem" : "undef"; close $m; push @r, defined(openhandle($m)) ? "still" : "closed";
print "openhandle: @r ", fileno(openhandle(*STDERR)), "\n";
# 演算子オーバーロードを持つオブジェクトは、文字列にした結果で判定する
{ package UfNum; use overload '""' => sub { ${$_[0]} }, '0+' => sub { ${$_[0]} }, fallback => 1; sub new { my $v = $_[1]; bless \$v, $_[0] } }
print "overloaded: ", join(",", map { looks_like_number($_) ? 1 : 0 } UfNum->new("12.5"), UfNum->new("abc"), bless({}, "UfPlain"), [], \1), "\n";
