use strict; use warnings;
# substr(...) を s///、tr///、代入演算子の対象にする
my $path = "tmpXXXXXX.dat"; my $n = 0;
substr($path, 0, -4) =~ s/X(?=X*\z)/chr(ord("a") + $n++)/ge; print "$path\n";
my $s = "hello world";
my $cnt = (substr($s, 0, 5) =~ tr/a-z/A-Z/); print "$s $cnt\n";
substr($s, 6) =~ s/o/0/; print "$s\n";
substr($s, 0, 1) .= "++"; print "$s\n";
substr($s, -5, 1) x= 3; print "$s\n";
my %h = (k => "abcdef"); my @a = ("12345");
substr($h{k}, 1, 2) =~ s/(.)(.)/$2$1/; substr($a[0], 1, 3) += 1000; print "$h{k} $a[0]\n";
my $i = 0; my $t = "aaaa"; substr($t, $i++, 1) =~ s/a/b/; print "$t $i\n";
my $r = (substr($t, 2) =~ s/a/c/g); print "$t $r\n";
print "nomatch\n" unless substr($t, 0, 1) =~ s/z/y/; print "$t\n";
substr($t, 0, 2) ||= "zz"; substr($t, 0, 0) .= ">"; print "$t\n";
my $u = "x-y"; substr($u, 0, 1) =~ s/x/substr($u, 2, 1) =~ tr|y|Y|; "long"/e; print "$u\n";
my $v = "abc"; my $plain = substr($v, 0, 2) =~ /ab/ ? "m" : "n"; my $copy = substr($v, 1) =~ s/b/B/r; print "$plain $copy $v\n";
