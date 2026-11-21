use strict; use warnings;
# *$fh: open(my $fh) のハンドルをグロブとして使う (${*$fh}、%{*$fh}、@{*$fh})
my $p = "test/io_tmp_globslots.txt";
open(my $w, '>', $p) or die; print $w "line1\nline2\nline3\n"; close $w;
open(my $fh, '<', $p) or die;
my $copy = $fh;
my $first = <$fh>;
${*$fh} = "name"; ${*$fh}{key} = "v"; %{*$fh} = (%{*$fh}, k2 => 2); @{*$fh} = (1, 2, 3); push @{*$fh}, 4;
print "slots: ${*$fh} ", join(",", map { "$_=${*$fh}{$_}" } sort keys %{*$fh}), " ", scalar(@{*$fh}), " ${*$fh}[3]\n";
*$copy->{arrow} = "ar"; *$copy->[4] = 5; print "copy sees: ${*$copy} ", *$copy->{key}, " ", *$fh->{arrow}, " ", *$fh->[4], " ", *$fh->[0] + 1, "\n"; delete *$fh->{arrow}; pop @{*$fh};
print "read goes on: $first", scalar(<$fh>), scalar(<$copy>);
print "ref: ", ref($fh), " ", ref(\*$fh), " eof=", (eof($fh) ? 1 : 0), " line=$.\n";
close($fh) or die; print "closed: ", (defined fileno($fh) ? "open" : "closed"), "\n";
open($fh, '<', $p) or die; print "reopened: ", scalar(<$fh>); print "kept: ${*$fh}\n"; close $fh;
# 別のハンドルは別の枠
open(my $a, '<', $p) or die; open(my $b, '<', $p) or die;
${*$a}{n} = "a"; ${*$b}{n} = "b"; print "separate: ${*$a}{n} ${*$b}{n}\n"; close $a; close $b;
# bless したハンドル、スコープを抜けると DESTROY と自動クローズ、枠は次のハンドルに残らない
{ package TmpH; sub new { my ($c, $path) = @_; open(my $h, '<', $path) or die; ${*$h}{path} = $path; ${*$h} = "obj"; bless $h, $c } sub path { ${*{$_[0]}}{path} } sub DESTROY { print "DESTROY ", ${*{$_[0]}}, "\n" } }
for my $i (1 .. 3) { my $o = TmpH->new($p); print ref($o), " ", $o->path eq $p ? "path" : "nopath", " ", scalar(<$o>); }
open(my $n, '<', $p) or die; print "fresh: ", (defined ${*$n} ? "stale" : "undef"), " ", scalar(keys %{*$n}), "\n"; close $n;
# 書き込み側
open(my $o, '>', $p) or die; ${*$o}{count} = 0; print {$o} "x\n"; ${*$o}{count}++; print $o "y\n"; close $o;
open($n, '<', $p) or die; my @l = <$n>; close $n; print "written: ", scalar(@l), " ${*$o}{count}\n";
unlink $p;
print "done\n";
