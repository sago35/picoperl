use strict; use warnings;
use Storable qw(dclone);
sub dump1 { my $d = shift; ref $d eq 'HASH' ? "{" . join(",", map { "$_=" . dump1($d->{$_}) } sort keys %$d) . "}" : ref $d eq 'ARRAY' ? "[" . join(",", map { dump1($_) } @$d) . "]" : ref $d eq 'SCALAR' ? "\\" . dump1($$d) : ref $d eq 'REF' ? "\\" . dump1($$d) : ref $d ? ref($d) . dump1(UNIVERSAL::isa($d, 'HASH') ? { %$d } : [ @$d ]) : defined $d ? $d : "undef" }
my $data = { name => "x", list => [1, 2, [3, 4]], h => { a => undef, b => \"str" }, n => 3.5 };
my $copy = dclone($data);
print dump1($copy), "\n", (dump1($copy) eq dump1($data) ? "equal" : "differ"), "\n";
$copy->{list}[2][0] = "changed"; $copy->{h}{a} = 1; push @{ $copy->{list} }, 9;
print "original untouched: ", dump1($data), "\n";
# bless されたもの
{ package Pt; sub new { bless { x => $_[1], kids => [] }, $_[0] } sub x { $_[0]{x} } }
my $o = Pt->new(5); push @{ $o->{kids} }, Pt->new(6); my $oc = dclone($o);
print ref($oc), " ", $oc->x, " ", ref($oc->{kids}[0]), " ", $oc->{kids}[0]->x, " ", ($oc != $o ? "new object" : "same"), "\n";
my $ba = dclone(bless [1, 2], 'Arr'); my $bs = dclone(bless \(my $s = "v"), 'Scl'); print ref($ba), " @$ba ", ref($bs), " $$bs\n";
# 共有と循環
my $shared = [1]; my $two = dclone({ a => $shared, b => $shared }); push @{ $two->{a} }, 2; print "shared kept: @{ $two->{b} }", " ", ($two->{a} == $two->{b} ? "same ref" : "split"), " ", ($two->{a} != $shared ? "copied" : "aliased"), "\n";
my $loop = { name => "loop" }; $loop->{self} = $loop; my $lc = dclone($loop); print "cycle: ", ($lc->{self} == $lc ? "closed" : "open"), " ", ($lc != $loop ? "copied" : "same"), " $lc->{self}{self}{name}\n";
my $rr = dclone(\\"deep"); print "ref of ref: $$$rr\n";
print "plain: ", dclone([])->[0] // "empty", " ", scalar(@{ dclone([1, 2, 3]) }), "\n";
print eval { dclone({ code => sub { 1 } }); 1 } ? "code copied?" : "code refused: " . ($@ =~ /^(Can't store CODE items)/)[0], "\n";
