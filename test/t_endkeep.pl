use strict; use warnings;
# END ブロックが使うファイル直下の変数は、DESTROY があっても END が終わるまで中身を保つ
my @list; my %h; my $kept = bless { n => "kept" }, "Obj"; my $other = bless { n => "other" }, "Obj"; my $count = 0;
END { print "END: ", scalar(@list), " ", join(",", sort keys %h), " ", ref($kept), " count=$count\n" }
sub Obj::DESTROY { print "DESTROY $_[0]{n}\n" }
push @list, 1, 2; $h{a} = 1; $count = 5;
{ my $inner = "in"; END { print "inner END: $inner ", scalar(@list), "\n" } }
print "body\n";
