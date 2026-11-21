use strict; use warnings;
# pipe: 書いたものを読み出せる (プロセスが無いので、picoperl では読む側は待たない。
# 本物の perl でも同じ結果になるよう、書く側を閉じるか syswrite してから読む)
pipe(my $r, my $w) or die "pipe: $!";
print $w "line1\nline2\n"; print {$w} "tail"; close $w;
my @l = <$r>; print "lexical: ", scalar(@l), " [", join("|", map { s/\n/\\n/r } @l), "]\n"; close $r;
pipe(R, W) or die; print W "from bareword\n"; close W; print "bareword: ", scalar(<R>); print "eof: ", (eof(R) ? 1 : 0), "\n"; close R;
pipe my $r2, my $w2; syswrite($w2, "abcdef", 3, 1); syswrite($w2, "X\n"); my $got = <$r2>; print "no parens: $got"; close $w2; close $r2;
sub xpipe { pipe $_[0], $_[1] or die "pipe failed: $!"; }
xpipe(my $r3, my $w3); syswrite($w3, "via \@_\n"); print "sub: ", scalar(<$r3>); close $w3;
our ($GR, $GW); my ($gr, $gw) = (\*GR, \*GW); pipe($gr, $gw) or die; syswrite($gw, "globref\n"); print "glob: ", scalar(<$gr>); close $gw; close $gr;
my %h = (pipe => 1); print "key: $h{pipe} ", (defined fileno($r3) ? "fileno" : "nofileno"), "\n";
pipe(my $r4, my $w4); close $w4; my $none = <$r4>; print "empty: ", (defined $none ? "data" : "eof"), "\n";
# 呼び出しの引数で宣言した変数や、\$_[0] 経由の書き込みも呼び出し元に届く
sub setit { $_[0] = 5; $_[1] = 6 } setit(my $sv, my $sw); print "decl in call: $sv $sw\n";
sub viaref { my $ref = \$_[0]; $$ref = 7 } my $vx; viaref($vx); print "via ref: $vx\n";
# CORE::pipe は、pipe という名前のサブの中からでも組み込みを呼ぶ
{ package PiWrap; sub pipe { my ($pr, $pw); CORE::pipe($pr, $pw) or return; ($pr, $pw) } }
{ my ($pr, $pw) = PiWrap::pipe(); syswrite($pw, "wrapped\n"); print "CORE::pipe: ", scalar(<$pr>); close $pw; close $pr; }
# strict の下でも、defined &$name / \&$name / exists &$name は自分のパッケージのサブを見つける
{ package PiNames; use strict; sub mine { "m" } sub pipe { 1 }
  my @n = grep { !defined(&$_) } qw(pipe mine other); my $r = \&{"mine"}; my $nm = "mine";
  print "by name: @n ", $r->(), " ", (exists(&$nm) ? "ex" : "nex"), " ", (defined(&{"PiNames::mine"}) ? 1 : 0), (defined(&{"main::mine"}) ? 1 : 0), "\n"; }
