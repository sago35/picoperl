# tie / tied / untie を本物の perl と突き合わせる
use strict;
# --- tie の基本: コンストラクタ、戻り値、tied、untie
{
    package TCnt; sub TIESCALAR { my ($c, @a) = @_; print "TIESCALAR @a\n"; bless { v => $a[0] }, $c } sub hello { "hello from " . ref(shift) }
    sub FETCH { $_[0]{v} } sub STORE { $_[0]{v} = $_[1] }
    package TH; sub TIEHASH { print "TIEHASH\n"; bless {}, shift } package TA; sub TIEARRAY { print "TIEARRAY $_[1]\n"; bless [], shift }
    package TFH; sub TIEHANDLE { print "TIEHANDLE\n"; bless [], shift }
    package main;
    my ($s, %h, @a); our %tg;
    my $o = tie $s, 'TCnt', 5, 6; print ref($o), " ", (tied($s) == $o ? "same" : "diff"), " ", tied($s)->hello, "\n";
    tie %h, 'TH'; tie @a, 'TA', 3; tie *TFOO, 'TFH'; my $hr = \%tg; tie %$hr, 'TH';
    print join(" ", map { ref($_) || 'none' } tied %h, tied @a, tied *TFOO, tied %tg, tied %$hr), "\n";
    my %plain; print defined(tied %plain) ? "tied" : "not tied", " ", (not tied %plain) ? "ok" : "ng", " ", ref tied %h, "\n";
    undef $o; untie %h; untie $s; print defined(tied %h) ? "tied" : "untied", " ", defined(tied $s) ? "tied" : "untied", "\n";
    my %t = (tie => 1, tied => 2, untie => 3); print join(",", map { "$_=$t{$_}" } sort keys %t), "\n";
    eval { tie my %x, 'NoSuch' }; print $@ =~ /Can't locate object method "TIEHASH" via package "NoSuch"/ ? "err ok\n" : "err: $@";
}
# --- ハンドル
{
    package TRec;
    sub TIEHANDLE { my ($c, $name) = @_; bless { name => $name, out => [], in => ["l1\n", "l2\n", "l3\n"] }, $c }
    sub PRINT { my $s = shift; push @{$s->{out}}, join("|", @_); 1 }
    sub PRINTF { my $s = shift; my $f = shift; push @{$s->{out}}, "F:" . sprintf($f, @_); 1 }
    sub READLINE { my $s = shift; return wantarray ? splice(@{$s->{in}}) : shift @{$s->{in}} }
    sub CLOSE { my $s = shift; push @{$s->{out}}, "closed"; "closeval" }
    sub EOF { my $s = shift; !@{$s->{in}} }
    sub BINMODE { my $s = shift; push @{$s->{out}}, "binmode(@_)"; 1 }
    sub FILENO { 42 }
    package main;
    my $obj = tie *TFH, 'TRec', 'fh';
    print TFH "a", "b"; printf TFH "%03d-%s", 7, "x"; print {*TFH} "c"; my $r = \*TFH; print $r "d", 1; print {$r} "e";
    binmode(TFH, ":utf8"); binmode $r;
    my $first = <TFH>; print "eof? ", (eof(TFH) ? 1 : 0), "\n"; my @rest = <$r>; print "eof? ", (eof($r) ? 1 : 0), "\n";
    chomp($first, @rest); print "read: $first / @rest\n";
    print "fileno: ", fileno(TFH), " ", fileno($r), "\n";
    my $cl = close(TFH); print "close: $cl\n";
    print "out: ", join(", ", @{$obj->{out}}), "\n";
    # 名前で作ったグロブ (Symbol::gensym と同じ形) と STDOUT / STDERR の tie
    my $sym = do { no strict 'refs'; \*{"TGen::G1"} };
    my $o2 = tie *$sym, 'TRec', 'gen'; print $sym "x"; print {$sym} "y", "z"; print "gen: @{$o2->{out}}\n";
    { my $o3 = tie *STDOUT, 'TRec', 'so'; print "captured"; print STDOUT "too"; printf("%d!", 5); my @got = @{$o3->{out}}; undef $o3; untie *STDOUT; print "stdout: @got\n"; }
    { my $o4 = tie *STDERR, 'TRec', 'se'; print STDERR "err"; my @got = @{$o4->{out}}; undef $o4; untie *STDERR; print "stderr: @got\n"; }
    print "after untie: ", (defined(tied *STDOUT) ? "tied" : "untied"), "\n";
    while (my $l = <$sym>) { chomp $l; print "loop $l\n" }
}
# --- スカラー: FETCH / STORE が呼ばれる順番まで本物と同じ
{
package Log; our @log;
sub TIESCALAR { my ($c, $v) = @_; bless { v => $v }, $c }
sub FETCH { my $s = shift; push @log, "FETCH"; $s->{v} }
sub STORE { my ($s, $v) = @_; push @log, "STORE($v)"; $s->{v} = $v }
package Upper; sub TIESCALAR { bless { v => '' }, shift } sub FETCH { uc $_[0]{v} } sub STORE { $_[0]{v} = "<$_[1]>" }
package main;
sub show { my $t = shift; print "$t: @Log::log\n"; @Log::log = () }
my $x; tie $x, 'Log', 10;
my $a = $x; show("read");
$x = 5; show("assign");
$x .= "z"; show("concat");
$x = 3; $x++; ++$x; $x += 10; show("incr");
print "value $x\n"; show("interp");
my $r = \$x; $$r = 7; my $b = $$r + 1; show("ref");
$x =~ s/7/seven/; show("subst");
my ($p, $q) = ($x, $x); show("list read");
($x, my $z) = (1, 2); show("list assign");
$x ||= 99; $x = 0; $x ||= 42; $x //= 1; show("logical");
chomp(my $c = "$x\n"); $x = "line\n"; chomp $x; show("chomp");
sub mod { $_[0] } my $got = mod($x); show("arg");
for my $i (1 .. 2) { $x = $i } show("loop");
my $len = length $x; my $def = defined $x ? 1 : 0; show("builtins");
our $g; tie $g, 'Upper'; $g = "abc"; print "global: $g ", length($g), "\n"; $g .= "d"; print "append: $g\n";
{ my $t; tie $t, 'Upper'; $t = "in"; print "lexical: $t\n"; my $f = sub { $t = "clo"; $t }; print "closure: ", $f->(), "\n"; }
my $v = ($x = 8); show("value of assign"); print "v=$v\n";
untie $x; $x = 1; my $y = $x; show("untied");
}
# --- ハッシュと配列
{
package LH; our @log;
sub TIEHASH { bless { d => {} }, shift }
sub FETCH { push @log, "FETCH($_[1])"; $_[0]{d}{$_[1]} }
sub STORE { push @log, "STORE($_[1],$_[2])"; $_[0]{d}{$_[1]} = $_[2] }
sub EXISTS { push @log, "EXISTS($_[1])"; exists $_[0]{d}{$_[1]} }
sub DELETE { push @log, "DELETE($_[1])"; delete $_[0]{d}{$_[1]} }
sub CLEAR { push @log, "CLEAR"; %{$_[0]{d}} = () }
sub FIRSTKEY { my @k = sort keys %{$_[0]{d}}; $k[0] }
sub NEXTKEY { my @k = sort keys %{$_[0]{d}}; for my $i (0 .. $#k) { return $k[$i + 1] if $k[$i] eq $_[1] } undef }
package LA; our @log;
sub TIEARRAY { bless { a => [] }, shift }
sub FETCH { push @log, "FETCH($_[1])"; $_[0]{a}[$_[1]] }
sub STORE { push @log, "STORE($_[1],$_[2])"; $_[0]{a}[$_[1]] = $_[2] }
sub FETCHSIZE { scalar @{$_[0]{a}} }
sub STORESIZE { $#{$_[0]{a}} = $_[1] - 1 }
sub CLEAR { @{$_[0]{a}} = () }
sub EXTEND { }
sub PUSH { my $s = shift; push @{$s->{a}}, @_ } sub POP { pop @{$_[0]{a}} } sub SHIFT { shift @{$_[0]{a}} }
sub UNSHIFT { my $s = shift; unshift @{$s->{a}}, @_ } sub SPLICE { my $s = shift; my $o = shift; my $l = shift; splice @{$s->{a}}, $o, $l, @_ }
package main;
sub hlog { my $t = shift; print "$t: @LH::log\n"; @LH::log = () }
sub alog { my $t = shift; print "$t: @LA::log\n"; @LA::log = () }
my %h; my $ho = tie %h, 'LH';
$h{a} = 1; hlog("store");
my $v = $h{a}; hlog("fetch");
$h{a} .= "x"; $h{n}++; $h{n} += 5; hlog("modify");
print "exists: ", (exists $h{a} ? 1 : 0), (exists $h{zz} ? 1 : 0), "\n"; hlog("exists");
my $d = delete $h{a}; print "deleted $d\n"; hlog("delete");
$h{b} = 2; $h{c} = 3; @LH::log = ();
print "keys: ", join(",", sort keys %h), " values: ", join(",", sort values %h), " count: ", scalar(keys %h), "\n";
my %copy = %h; print "copy: ", join(",", map { "$_=$copy{$_}" } sort keys %copy), "\n";
my @pairs; while (my ($k, $val) = each %h) { push @pairs, "$k=$val" } print "each: @{[sort @pairs]}\n";
print "inner: ", join(",", map { "$_=$ho->{d}{$_}" } sort keys %{$ho->{d}}), "\n";
my $hr = \%h; $hr->{r} = 9; print "via ref: $hr->{r} $h{r} ", (exists $hr->{r} ? 1 : 0), "\n";
@h{qw(s1 s2)} = (7, 8); print "slice: @h{qw(s1 s2)} inner $ho->{d}{s1}$ho->{d}{s2}\n";
%h = (x => 1, y => 2); print "assigned: ", join(",", map { "$_=$ho->{d}{$_}" } sort keys %{$ho->{d}}), "\n";
%h = (); print "cleared: ", scalar(keys %{$ho->{d}}), " ", (%h ? "nonempty" : "empty"), "\n";
for my $k (qw(p q)) { $h{$k} = uc $k } print "loop: ", join(",", map { "$_=$h{$_}" } sort keys %h), "\n";
undef $ho; untie %h; print "untied: ", (defined tied %h ? "tied" : "plain"), "\n";

my @a; my $ao = tie @a, 'LA';
$a[0] = "z"; $a[2] = "y"; alog("store");
my $e = $a[2]; alog("fetch");
$a[1] = "m"; $a[1] .= "!"; @LA::log = ();
print "size: ", scalar(@a), " last: $#a list: @a inner: @{$ao->{a}}\n";
push @a, "p1", "p2"; print "push: @{$ao->{a}}\n";
my $pp = pop @a; my $sh = shift @a; print "pop $pp shift $sh: @{$ao->{a}}\n";
unshift @a, "u"; print "unshift: @{$ao->{a}}\n";
my @rem = splice(@a, 1, 2, "S1", "S2", "S3"); print "splice (@rem): @{$ao->{a}}\n";
@a = (1, 2, 3); print "assign: @{$ao->{a}} ", scalar(@a), "\n";
my $sum = 0; $sum += $_ for @a; print "sum $sum, join ", join("-", @a), ", rev @{[reverse @a]}, map @{[map { $_ * 2 } @a]}\n";
my $ar = \@a; $ar->[1] = 20; print "via ref: $ar->[1] $a[1] @$ar\n";
@a = (); print "empty: ", scalar(@{$ao->{a}}), "\n";
}
