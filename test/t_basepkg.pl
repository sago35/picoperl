use strict; use warnings;
# use base は、既にあるクラス (同じファイルで定義したもの、読み込み済みのもの) をファイルから探さない
{ package BpBase; sub hi { "hi from " . ref(shift) } }
{ package BpOnlyIsa; our @ISA = (); }
{ package BpKid; use base "BpBase"; sub new { bless {}, shift } }
{ package BpKid2; use base qw(BpOnlyIsa BpBase); }
{ package BpLoaded; use base "Carp"; }
print BpKid->new->hi, " ", (BpKid->isa("BpBase") ? 1 : 0), (BpKid2->isa("BpOnlyIsa") ? 1 : 0), (BpKid2->can("hi") ? 1 : 0),
      (BpLoaded->can("croak") ? 1 : 0), "\n";
{ package BpMissing; my $ok = eval q{ use base "Bp::No::Such::Class"; 1 }; print $ok ? "loaded?\n" : "missing class dies\n"; }
{ package BpParent; my $ok = eval q{ use parent "BpBase"; 1 }; print $ok ? "parent loaded?\n" : "use parent still wants the file\n";
  eval q{ use parent -norequire, "BpBase"; 1 } or die $@; print BpParent->can("hi") ? "norequire ok\n" : "no\n"; }
# 宣言済みのサブを括弧なしで呼ぶとき、マイナスは引数の符号 (head -1, @list)
sub f { "f(@_)" } sub c0 () { 10 }
my $x = 5; my @l = (f -2, 3);
print f(-1), "|", (f -1), "|", (f - 1), "|", (f -$x, 2), "|", scalar(@l), "|", c0 - 1, "|", c0 -1, "|", 7 - f(1) =~ /1/, "\n";
my $y = f -1; my $z = f +1, 2; print "$y $z\n";
# 下位のパッケージ (BpTop::Sub) があれば、perl と同じく上位のクラス (BpTop) も「空でない」と見なす
{ package BpTop::Sub; sub x { 1 } }
{ package BpUser; my $ok = eval q{ use base "BpTop"; 1 }; print $ok ? "a package below it counts\n" : "died\n"; }
