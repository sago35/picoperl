use v5.36;   # enables strict + warnings here and in perl; state/for_list are available

# --- heredocs ---
my $who = "world";
print <<EOF;
hello, $who
second line
EOF
print <<"DQ";
interp: @{[ 1 + 2 ]}\t(tab)
DQ
print <<'SQ';
raw: $who \n \\ stays
SQ
print <<~IND;
    dedented $who
      kept deeper indent
    IND
my ($a2, $b2) = (<<A, <<B);
first body
A
second body
B
print "stacked: [$a2][$b2]\n";

# --- each ---
my %h = (one => 1, two => 2, three => 3);
my @pairs;
while (my ($k, $v) = each %h) { push @pairs, "$k=$v" }
print join(",", sort @pairs), "\n";
my $n = 0;
$n++ while defined each %h;
print "second sweep: $n\n";
keys %h;                       # resets the iterator
my ($k1) = each %h;
print "reset: ", (defined $k1 ? "yes" : "no"), "\n";

# --- state ---
sub counter { state $c = 0; return ++$c }
counter(); counter();
print "counter: ", counter(), "\n";
sub make_id {
    my ($prefix) = @_;
    return sub { state $i = 0; return $prefix . ++$i };
}
my $ida = make_id("a");
my $idb = make_id("b");
$ida->(); $ida->();
print $ida->(), " ", $idb->(), "\n";
sub tick { state $left; $left //= 3; return $left-- }
tick(); tick();
print "tick: ", tick(), "\n";

# --- for my ($k, $v) (LIST) ---
my @out;
for my ($k, $v) (map { $_ => $h{$_} } sort keys %h) { push @out, "$k:$v" }
print "@out\n";
for my ($x, $y, $z) (1 .. 7) {
    print defined $z ? "($x $y $z)" : defined $y ? "($x $y -)" : "($x - -)";
}
print "\n";

# --- ^^ and ^^= ---
print( (1 ^^ 0 ? "t" : "f"), (0 ^^ 1 ? "t" : "f"), (1 ^^ 1 ? "t" : "f"), (0 ^^ 0 ? "t" : "f"), "\n" );
