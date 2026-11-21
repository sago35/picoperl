use strict; use warnings;
# pack / unpack の 'u' (uuencode)
my @in = ("", "a", "ab", "abc", "abcd", "hello world", "x" x 45, "y" x 46, "z" x 100, join("", map { chr } 0 .. 255), "\0\0\0", "line\nbreak\n");
for my $s (@in) {
    my $u = pack("u", $s);
    my $back = unpack("u", $u); $back = "" unless defined $back;      # (空のデータからは undef)
    (my $show = $u) =~ s/\n/\\n/g;
    print length($s), ": ", (length($show) > 70 ? substr($show, 0, 70) . "..." : $show), " ", ($back eq $s ? "roundtrip" : "MISMATCH"), "\n";
}
for my $t ("u", "u*", "u0", "u2", "u3", "u7", "u30", "u63", "u45") { (my $u = pack($t, "abcdefghijklmnopqrstuvwxyz0123456789" x 2)) =~ s/\n/|/g; print "$t: $u\n"; }
{ no warnings; (my $u = pack("u100", "q" x 70)) =~ s/\n/|/g; print "u100: $u\n"; }
print "two: ", join(",", unpack("u a*", pack("u", "first") . "rest")), "\n";
print "with others: ", join(",", unpack("n u", pack("n u", 513, "payload"))), "\n";
print "list: ", join(",", map { length } pack("u u", "one", "two")), " ", pack("A2 u", "hd", "x"), "";
print "space as zero: ", unpack("u", "#   \n") eq "\0\0\0" ? "ok" : "no", "\n";
print "no newline: ", unpack("u", "#86)C"), " cut: ", unpack("H*", unpack("u", "#86")), "\n";      # (切れた分は 0 のビットで埋まる)
print "checksum char: ", unpack("u", "#86)CX\n#9&5F\n"), "\n";
print "undef: [", do { no warnings; pack("u", undef) }, "] empty: ", join(",", map { defined $_ ? "def" : "undef" } unpack("u", "")), " not uu: ", join("|", map { defined $_ ? "[$_]" : "undef" } unpack("u", " x"), unpack("u", "abc"), unpack("u a*", "~~")), "\n";
