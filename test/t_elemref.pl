use strict; use warnings;

# 要素への参照: \$h{k} / \$a[i] は要素そのものを読み書きする
{
    my %h = (k => "v", n => 1); my @a = (10, 20, 30);
    my $r = \$h{k}; $$r = "changed"; print "$h{k} $$r\n";
    $h{k} = "direct"; print "sees: $$r\n";
    my $q = \$a[1]; $$q += 5; $$q .= "x"; print "@a\n"; $a[1] = 7; print "sees: $$q\n";
    my $rn = \$h{n}; $$rn++; ${$rn} *= 10; print "n=$h{n}\n";
    my $new = \$h{fresh}; print "exists: ", (exists $h{fresh} ? 1 : 0), "\n"; $$new = "born"; print "$h{fresh}\n";
    my $deep = { list => [1, 2], inner => { x => "y" } }; my $d1 = \$deep->{list}[1]; my $d2 = \$deep->{inner}{x}; $$d1 = "two"; $$d2 = uc $$d2; print "@{$deep->{list}} $deep->{inner}{x}\n";
    my $neg = \$a[-1]; $$neg = "last"; print "@a\n";
    sub setit { ${$_[0]} = $_[1] } setit(\$h{k}, "via sub"); setit(\$a[0], "zero"); print "$h{k} $a[0]\n";
    my @refs = map { \$a[$_] } 0 .. 2; ${$refs[2]} = "third"; print "@a ", ref($refs[0]), "\n";
    my ($x, $y) = (\$h{k}, \$h{k}); $$x = "one"; print "both: $$y\n";
    open(my $out, '>>', \$h{log}) or die "open: $!"; print $out "line1\n"; print $out "line2\n"; close $out; print $h{log};
    open(my $in, '<', \$h{log}) or die; my @l = <$in>; close $in; print scalar(@l), "\n";
    my %opt; my @spec = (name => \$opt{name}, count => \$opt{count}); ${$spec[1]} = "abc"; ${$spec[3]} = 3; print join(",", map { "$_=$opt{$_}" } sort keys %opt), "\n";
}
{ my %h; open(my $f, "<", $0) or die; read($f, $h{buf}, 5); my @a; read($f, $a[2], 3); close $f; print "read into element: [$h{buf}][$a[2]]\n"; }

# 同じ要素に 2 つのハンドルで追記する
{
    my %o = (all => '', a => '', b => '');
    open(my $a1, '>>', \$o{all}) or die; open(my $a2, '>>', \$o{a}) or die; open(my $b1, '>>', \$o{all}) or die; open(my $b2, '>>', \$o{b}) or die;
    print $a1 "one\n"; print $a2 "one\n"; print $b1 "two\n"; print $b2 "two\n"; print $a1 "three\n";
    $o{all} .= "direct\n"; print $b1 "four\n";
    print "all=[", join("|", split /\n/, $o{all}), "] a=[", join("|", split /\n/, $o{a}), "] b=[", join("|", split /\n/, $o{b}), "]\n";
    my $s = ''; open(my $p, '>>', \$s) or die; open(my $q, '>>', \$s) or die; print $p "x"; print $q "y"; print $p "z"; print "plain=[$s]\n";
}
