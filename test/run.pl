#!/usr/bin/perl
# Runs every test/*.pl with both the system perl and build/picoperl and compares
# stdout+stderr+exit code. Usage: perl test/run.pl [pattern]
use strict;
use warnings;

my $bin = $ENV{PICOPERL} || 'build/picoperl';
my $perl = $ENV{PICOPERL_TEST_PERL} || 'perl';   # e.g. /root/.cache/perl-5.44/bin/perl
my $pat = shift;
my @tests = sort glob('test/t_*.pl');
@tests = grep { /$pat/ } @tests if defined $pat;
my ($pass, $fail) = (0, 0);
my $pver = 0 + `$perl -e 'print \$]'`;
for my $t (@tests) {
    if (open my $fh, '<', $t) {
        my $l1 = <$fh> // '';
        close $fh;
        if ($l1 =~ /needs-perl\s+([\d.]+)/ && $pver < $1) { print "skip $t (needs perl >= $1)\n"; next; }
    }
    my $exp = `$perl $t 2>&1`; my $exp_rc = $? >> 8;
    my $got = `$bin $t 2>&1`; my $got_rc = $? >> 8;
    if ($exp eq $got && $exp_rc == $got_rc) { $pass++; print "ok   $t\n"; next; }
    $fail++;
    print "FAIL $t (perl rc=$exp_rc, picoperl rc=$got_rc)\n";
    my @e = split /\n/, $exp; my @g = split /\n/, $got;
    for my $i (0 .. ($#e > $#g ? $#e : $#g)) {
        my ($a, $b) = ($e[$i] // '<none>', $g[$i] // '<none>');
        next if $a eq $b;
        print "  line ", $i + 1, ":\n    perl:     $a\n    picoperl: $b\n";
        last;
    }
}
# compile-error tests: both interpreters must fail, and the first line of the message must match
for my $t (sort glob('test/e_*.pl')) {
    my $exp = `$perl $t 2>&1`; my $exp_rc = $? >> 8;
    my $got = `$bin $t 2>&1`; my $got_rc = $? >> 8;
    my ($e1) = split /\n/, $exp; my ($g1) = split /\n/, $got;
    if ($exp_rc && $got_rc && $e1 eq $g1) { $pass++; print "ok   $t\n"; next; }
    $fail++;
    print "FAIL $t (perl rc=$exp_rc, picoperl rc=$got_rc)\n    perl:     $e1\n    picoperl: $g1\n";
}
print "\n$pass passed, $fail failed\n";
exit($fail ? 1 : 0);
