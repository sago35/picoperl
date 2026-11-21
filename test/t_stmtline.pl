# 複数行にまたがる文: 呼び出しは文の開始行を報告する
use strict; use warnings;
sub ln { (caller)[2] } sub show { print "@_\n" }
show("A", ln(),
     ln());
show("B",
     ln()
);
my @r = (ln(),
         ln());  show("C", @r);
my $x = ln(
);  show("D", $x);
$x = ln(
);  show("E", $x);
eval { die "F",
       "\n" if 0; die "F2 ",
       "x" }; print $@ =~ /line (\d+)/ ? "F $1\n" : "?\n";
show("G", sub {
    ln() }->(), ln()
);
if (ln() ==
    0) { } else { show("H", ln(
    )) }
warn "W",
     "\n" if 0; local $SIG{__WARN__} = sub { print "W ", $_[0] =~ /line (\d+)/, "\n" }; warn "w1",
     " w2";
my %h = (a =>
     ln()); show("I", $h{a});
print "J ", ln(
), "\n";
return_it(ln(),
   2);
sub return_it { show("K", $_[0], (caller)[2]) }
