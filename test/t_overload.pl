# 演算子オーバーロード (use overload) を本物の perl と突き合わせる
use strict;
package Vec2;
use overload
    '+' => \&add, '-' => sub { my ($a, $b, $sw) = @_; $b = Vec2->new($b, $b) unless ref $b; ($a, $b) = ($b, $a) if $sw; Vec2->new($a->{x} - $b->{x}, $a->{y} - $b->{y}) },
    '*' => sub { my ($a, $b) = @_; ref $b ? $a->{x} * $b->{x} + $a->{y} * $b->{y} : Vec2->new($a->{x} * $b, $a->{y} * $b) },
    '==' => sub { my ($a, $b) = @_; $a->{x} == $b->{x} && $a->{y} == $b->{y} },
    '""' => sub { my $s = shift; "($s->{x}, $s->{y})" },
    'bool' => sub { my $s = shift; $s->{x} || $s->{y} },
    'neg' => sub { Vec2->new(-$_[0]{x}, -$_[0]{y}) },
    'abs' => sub { sqrt($_[0]{x} ** 2 + $_[0]{y} ** 2) },
    '+=' => sub { $_[0]{x} += $_[1]{x}; $_[0]{y} += $_[1]{y}; $_[0] };
sub new { my ($c, $x, $y) = @_; bless { x => $x, y => $y }, $c }
sub add { my ($a, $b) = @_; Vec2->new($a->{x} + $b->{x}, $a->{y} + $b->{y}) }
package Num;   # <=> と 0+ と "" だけ: 残りは自動生成
use overload fallback => 1, '<=>' => sub { my ($a, $b, $sw) = @_; my $r = $a->{n} <=> (ref $b ? $b->{n} : $b); $sw ? -$r : $r }, '0+' => sub { $_[0]{n} }, '""' => sub { "N$_[0]{n}" }, 'cmp' => sub { my ($a, $b, $sw) = @_; my $r = "$a" cmp "$b"; $sw ? -$r : $r };
sub new { bless { n => $_[1] }, $_[0] }
package Named; use overload fallback => 1, '""' => 'as_string', 'eq' => sub { "$_[0]" eq "$_[1]" }; sub new { bless { name => $_[1] }, $_[0] } sub as_string { "<" . $_[0]{name} . ">" }
package Child; our @ISA = ('Named'); sub as_string { "child:" . $_[0]{name} }
package Plain; sub new { bless {}, shift }
package Counter; use overload fallback => 1, '++' => sub { $_[0]{c}++; $_[0] }, '--' => sub { $_[0]{c}--; $_[0] }, '""' => sub { "C$_[0]{c}" }, '+' => sub { Counter->new($_[0]{c} + $_[1]) }; sub new { bless { c => $_[1] }, $_[0] }
package main;
my ($va, $vb) = (Vec2->new(1, 2), Vec2->new(3, 4));
print "add: ", $va + $vb, " sub: ", $vb - $va, " swapped: ", 10 - $va, " scale: ", $va * 3, " dot: ", $va * $vb, "\n";
print "eq: ", ($va == Vec2->new(1, 2) ? "yes" : "no"), ($va == $vb ? "yes" : "no"), " neg: ", -$va, " abs: ", abs(Vec2->new(3, 4)), "\n";
print "bool: ", (Vec2->new(0, 0) ? "true" : "false"), ($va ? "true" : "false"), (!$va ? "T" : "F"), "\n";
my $c = Vec2->new(1, 1); $c += $va; $c += $va; print "+=: $c\n"; my $d = $va; $d = $d + $vb; $d -= $va; print "-= via -: $d, a still $va\n";
print "interp: [$va] join: ", join(";", $va, $vb), " concat: " . $va . " len ", length($va), "\n";
my %h = ($va => 1); print "key: ", join(",", keys %h), "\n";
my @n = map { Num->new($_) } 3, 1, 2;
print "sort: @{[sort { $a <=> $b } @n]} | cmp: ", join(" ", map { $_ ? 1 : 0 } $n[0] > $n[1], $n[0] < 2, $n[1] == 1, 5 <= $n[0], $n[2] != 2, $n[0] >= 3), "\n";
print "numeric: ", $n[0] + 10, " ", $n[0] * $n[2], " ", 2 ** $n[0], " ", sqrt(Num->new(16)), " ", int(Num->new(7)) + 0, " str: $n[0] ", ($n[0] lt $n[1] ? "lt" : "ge"), ($n[1] eq "N1" ? " eq" : " ne"), "\n";
my ($p, $q) = (Named->new("x"), Child->new("y"));
print "method name: $p inherited: $q eq: ", ($p eq "<x>" ? 1 : 0), ($q eq "child:y" ? 1 : 0), "\n";
my $pl = Plain->new; my $pl2 = $pl;
print "plain: ", ("$pl" =~ /^Plain=HASH\(0x[0-9a-f]+\)$/ ? "addr" : "other"), " ", ($pl == $pl2 ? "same" : "diff"), " ", ($pl == Plain->new ? "same" : "diff"), " ", ($pl ? "true" : "false"), "\n";
print "StrVal: ", (overload::StrVal($va) =~ /^Vec2=HASH\(0x[0-9a-f]+\)$/ ? "plain" : "other"), " Overloaded: ", (overload::Overloaded($va) ? 1 : 0), (overload::Overloaded($pl) ? 1 : 0), (overload::Overloaded('Child') ? 1 : 0), " Method: ", (ref overload::Method($va, '+')), (defined overload::Method($va, '/') ? "def" : "undef"), "\n";
my $k = Counter->new(5); $k++; ++$k; $k--; my $k2 = $k + 10; print "counter: $k $k2\n";
print "ref/blessed unaffected: ", ref($va), " ", ($va->isa('Vec2') ? 1 : 0), "\n";
