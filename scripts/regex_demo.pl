# Regex demo for the PICOPERL board.
#
# At boot it runs a small self-test (each line ends with "ok" when the result
# matches the expectation), then reads lines from the USB serial console and
# shows what m//, s///, split and tr/// do with them.
use strict;
use warnings;
use Board;

print "picoperl regex demo, heap free ", Board::heap_free() // "?", "\n";

my @tests = (
    ["match",   sub { "zero-kb02" =~ /kb(\d+)/ ? $1 : "no" },                 "02"],
    ["ci",      sub { "Hello" =~ /hello/i ? "yes" : "no" },                   "yes"],
    ["capture", sub { my ($y, $m, $d) = "2026-08-31" =~ /^(\d+)-(\d+)-(\d+)$/; "$y/$m/$d" }, "2026/08/31"],
    ["global",  sub { join(",", "a1b22c333" =~ /(\d+)/g) },                   "1,22,333"],
    ["subst",   sub { (my $s = "hello world") =~ s/o/0/g; $s },               "hell0 w0rld"],
    ["eval",    sub { (my $s = "3+4") =~ s/(\d+)\+(\d+)/$1 + $2/e; $s },      "7"],
    ["split",   sub { join("|", split /\s*,\s*/, "a, b ,c") },                "a|b|c"],
    ["tr",      sub { (my $s = "GATTACA") =~ tr/ACGT/TGCA/; $s },             "CTAATGT"],
    ["anchor",  sub { "foo.pl" =~ /\.pl$/ ? "yes" : "no" },                   "yes"],
    ["qr",      sub { my $re = qr/(\w+)@(\w+)/; "user\@host" =~ $re ? "$1:$2" : "no" }, "user:host"],
);
for my $t (@tests) {
    my ($name, $code, $want) = @$t;
    my $got = $code->();
    printf "%-8s %-12s %s\n", $name, $got, $got eq $want ? "ok" : "NG (want $want)";
}
print "self-test done, heap free ", Board::heap_free() // "?", "\n";
print "type a line and I will pick it apart:\n";

while (1) {
    my $line = <STDIN>;
    last unless defined $line;
    chomp $line;
    next unless length $line;

    if (my @nums = $line =~ /(-?\d+(?:\.\d+)?)/g) {
        print "  numbers: @nums\n";
    } else {
        print "  numbers: (none)\n";
    }
    my @words = $line =~ /([[:alpha:]]+)/g;
    print "  words:   ", scalar(@words), " (@words)\n";
    if ($line =~ /(\w+)@([\w.]+)/) {
        print "  email?   user=$1 host=$2\n";
    }
    (my $up = $line) =~ tr/a-z/A-Z/;
    print "  upper:   $up\n";
    (my $masked = $line) =~ s/\d/#/g;
    print "  masked:  $masked\n";
    my $vowels = ($line =~ tr/aeiouAEIOU//);
    print "  vowels:  $vowels\n";
    print "type another line:\n";
}
