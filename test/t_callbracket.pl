use strict; use warnings;
# 宣言済みのサブを括弧なしで呼ぶとき、最初の引数が [ ... ] や { ... } でもよい
sub show_hash { my $h = shift; "{" . join(",", map { "$_=$h->{$_}" } sort keys %$h) . "}" }
sub show { join(" ", map { ref($_) eq 'ARRAY' ? "[@$_]" : ref($_) eq 'HASH' ? show_hash($_) : $_ } @_) }
sub first_of { $_[0] }
my @got = (3, 1, 2);
my $line = show [sort @got], [1, 2, 3], 'name'; print "$line\n";
$line = show { a => 1, b => 2 }, "after"; print "$line\n";
$line = show [ first_of [7, 8] ]->[0], "x"; print "$line\n";
my $r = first_of { k => [1, 2] }; print ref($r), " $r->{k}[1]\n";
my $e = first_of []; my $h = first_of {}; print ref($e), " ", ref($h), "\n";
if (first_of [1]) { print "in if\n" }
print "list: ", scalar(my @l = (show [1], show [2])), "\n";
my %tab = (show => 5); print "key: $tab{show}\n";
sub noargs () { 42 } my @arr = (noargs, noargs); print "const: $arr[0] ", noargs + 1, "\n";
print show({ x => 1 }), " ", show([9]), "\n";
