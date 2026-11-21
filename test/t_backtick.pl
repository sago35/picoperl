use strict; use warnings;
# バッククォートと qx を含むコードはコンパイルできる (実行すると picoperl では空の結果になるので、ここでは呼ばない)
my $dir = "/tmp"; my %h = (k => "v");
sub never { my $out = `ls $dir 2>&1`; my @l = qx{echo $h{k}}; my $raw = qx'echo $dir'; my $n = `nm $dir` =~ /\b_?boot_/; return scalar(@l) }
print "compiled: ", (defined &never ? "sub is there" : "no sub"), "\n";
my $t = "a`b"; my $q = 'x`y'; print "in strings: $t $q ", length("`"), "\n";
print "regex: ", ("a`b" =~ /`/ ? "match" : "no"), " ", join("|", split /`/, "p`q"), "\n";
my %tick = ('`' => 1); my $qx = { qx => 2 }; print "keys: ", $tick{'`'}, $qx->{qx}, "\n";
print <<"EOT";
heredoc with `ticks` and $dir
EOT
