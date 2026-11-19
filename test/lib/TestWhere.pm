package TestWhere;
# die / warn / caller の中で報告されるファイル名のテスト用
sub boom { die "boom" }
sub careful { warn "careful"; 1 }
sub bad_ref { my %h; my $r = \%h; push @$r, 1; 1 }
sub where { return (__FILE__, __LINE__, (caller(0))[1]) }
sub via { my $cb = shift; $cb->() }
1;
