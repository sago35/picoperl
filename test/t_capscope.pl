use strict; use warnings;
# サブの中の正規表現は、呼び出し側の $1 $2 $& などを変えない
sub inner { my $s = shift; $s =~ /(x)/; "abc" =~ /(b)/ or die; return "in:$1" }
sub nomatch { "abc" =~ /(z)/; 1 }
"hello world" =~ /(\w+) (\w+)/ or die;
my $r = inner("y");
print "after call: $1 $2 [$&] $r $-[2] $+[2]\n";
nomatch(); print "after failed match in sub: $1 $2\n";
my @l = map { inner($_) } 1 .. 2; print "after map: $1 $2\n";
"k=v" =~ /(?<key>\w)=(?<val>\w)/; inner("q"); print "named: $+{key} $+{val}\n";
for my $w ("ab", "cd") { $w =~ /(.)(.)/; inner($w); print "loop: $1$2\n"; }
my $t = "a1b2"; my $out = ""; while ($t =~ /([a-z])(\d)/g) { $out .= inner($1) . $2 } print "$out\n";
(my $c = "x-y") =~ s/(\w)-(\w)/inner($1) . $2 . $1/e; print "$c\n";
