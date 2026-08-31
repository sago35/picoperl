# Subroutine-signature demo for the PICOPERL board.
#
# At boot it runs a small self-test (each line ends with "ok" when the result
# matches the expectation), then reads lines from the USB serial console and
# greets with defaultable parameters.
use strict;
use warnings;
use feature 'signatures';
use Board;

print "picoperl signature demo, heap free ", Board::heap_free() // "?", "\n";

sub add($x, $y) { return $x + $y }
sub greet($name, $greeting = "hello", $punct = "!") { return "$greeting, $name$punct" }
sub scaled($x, $factor = $x) { return $x * $factor }              # default uses an earlier parameter
sub pick($x, $fallback //= "(none)") { return $fallback }
sub stats($label, @values) { return "$label: " . scalar(@values) . " values" }
sub conf($name, %opt) { return "$name=" . ($opt{level} // 0) }
sub arity_err($code) { eval { $code->() }; my ($m) = $@ =~ /^(.*?) \(/; return $m }

my @tests = (
    ["basic",   sub { add(2, 3) },                                 "5"],
    ["default", sub { greet("pico") },                             "hello, pico!"],
    ["partial", sub { greet("pico", "hi") },                       "hi, pico!"],
    ["earlier", sub { scaled(9) },                                 "81"],
    ["defor",   sub { pick(1, undef) },                            "(none)"],
    ["slurpy",  sub { stats("t", 1, 2, 3) },                       "t: 3 values"],
    ["opthash", sub { conf("x", level => 7) },                     "x=7"],
    ["method",  sub { Acc->new(40)->add(2)->total },               "42"],
    ["toofew",  sub { arity_err(sub { add(1) }) },                 "Too few arguments for subroutine 'main::add'"],
    ["toomany", sub { arity_err(sub { add(1, 2, 3) }) },           "Too many arguments for subroutine 'main::add'"],
);
for my $t (@tests) {
    my ($name, $code, $want) = @$t;
    my $got = $code->();
    printf "%-8s %-24s %s\n", $name, $got, $got eq $want ? "ok" : "NG (want $want)";
}
print "self-test done, heap free ", Board::heap_free() // "?", "\n";
print "type: NAME [GREETING] [PUNCT]\n";

while (1) {
    my $line = <STDIN>;
    last unless defined $line;
    chomp $line;
    next unless length $line;
    my @f = split ' ', $line;
    print "  ", greet(@f[0 .. (@f > 3 ? 2 : $#f)]), "\n";
    print "type another line:\n";
}

package Acc;
sub new($class, $start = 0) { return bless { n => $start }, $class }
sub add($self, $d = 1) { $self->{n} += $d; return $self }
sub total($self) { return $self->{n} }
