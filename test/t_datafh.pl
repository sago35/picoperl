# __END__ / __DATA__ の後ろを <DATA> で読む
use strict;
use lib 'test/lib';
use TestDataMod;
my @lines; while (<DATA>) { chomp; push @lines, $_ } print scalar(@lines), ": @lines\n";
print "eof: ", (eof(DATA) ? 1 : 0), " again: ", (defined(scalar <DATA>) ? "more" : "end"), "\n";
print "module __DATA__: ", join("|", TestDataMod::lines()), "\n";
my @a = (1, 2); my $n = 5; print "less-than still works: ", (scalar @a < 3 ? "lt" : "ge"), " ", (length "abc" < $n ? 1 : 0), "\n";
__END__
first line
second, with __END__ inside
  third

last
