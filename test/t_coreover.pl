use strict; no warnings;
# main が組み込み関数と同じ名前のサブを取り込んでも、ほかのパッケージと CORE:: は組み込みを使う
BEGIN { package Wrap; sub my_unlink { "wrapped(@_)" } sub my_mkdir { "wrapped mkdir" } no strict 'refs'; *{"main::unlink"} = \&my_unlink; *{"main::mkdir"} = \&my_mkdir; }
{ package Other; sub try { my $n = unlink "/no/such/file/here"; my $m = mkdir "/no/such/dir/x/y"; "other: unlink=$n mkdir=" . ($m ? 1 : 0) } }
print unlink("a", "b"), " | ", Other::try(), " | core: ", CORE::unlink("/no/such/file/here"), "\n";
my $old = umask; my $prev = umask(027); my $now = umask; umask($old); printf "umask: %03o %03o\n", $prev == $old ? 1 : 0, $now;

# $( と $) (グループ ID。ここでは常に "0 0")、名前のない引数 ($) との共存
{ use feature qw(signatures); my @g = split /\s+/, $); my @r = split " ", $(; sub anon_last ($x, $) { $x } print scalar(@g) >= 1 ? "egid" : "none", " ", scalar(@r) >= 1 ? "gid" : "none", " ", anon_last(7, 8), "\n"; }
