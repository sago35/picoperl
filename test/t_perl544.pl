# needs-perl 5.043  (^^=, any/all, named signature parameters are 5.42/5.44 features)
use v5.40;
no warnings;
use feature 'keyword_any', 'keyword_all';

# --- ^^= ---
my ($t, $u, $w) = (5, 5, 0);
$t ^^= 0; $u ^^= 2; $w ^^= 0;
print "$t [$u] [$w]\n";

# --- any / all ---
my @n = (3, 14, 15, 9, 26);
print((any { $_ > 20 } @n) ? "y" : "n", (any { $_ > 30 } @n) ? "y" : "n",
      (all { $_ > 2 } @n) ? "y" : "n", (all { $_ > 5 } @n) ? "y" : "n", "\n");
print((any { $_ } ()) ? "y" : "n", (all { $_ } ()) ? "y" : "n", "\n");   # empty list

# --- named signature parameters ---
sub mk($kind, :$width = 10, :$height = 5, :$label) {
    return "$kind ${width}x$height '$label'";
}
print mk("box", label => "A"), "\n";
print mk("box", height => 7, label => "B", width => 3), "\n";
eval { mk("box") };
print $@;
eval { mk("box", label => "C", colour => "red") };
print $@;
eval { mk("box", label => "D", "stray") };
print $@;

sub opts(:$level = 1, %rest) {
    return "level=$level rest=" . join(",", map { "$_=$rest{$_}" } sort keys %rest);
}
print opts(level => 3, a => 1, b => 2), "\n";
print opts(x => 9), "\n";

sub dflt($base, :$scale = $base * 2) { return $scale }
print dflt(21), " ", dflt(21, scale => 5), "\n";
