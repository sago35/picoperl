use strict; use warnings;
# MIME::Base64 / MIME::QuotedPrint: perl 本体 (XS 版) と同じ結果になること
use MIME::Base64 qw(encode_base64 decode_base64 encode_base64url decode_base64url encoded_base64_length decoded_base64_length);
use MIME::QuotedPrint;
sub show { my $s = shift; $s =~ s/([^\x20-\x7e])/sprintf("\\x%02x", ord($1))/ge; $s }
my @in = ("", "a", "ab", "abc", "abcd", "Hello, World!", "\x00\xff\x10\x80", join("", map { chr($_ % 256) } 0 .. 299), "x" x 57, "y" x 58, "z" x 114);
for my $s (@in) {
    my $e = encode_base64($s); my $d = decode_base64($e);
    print length($s), ": ", show(substr($e, 0, 40)), (length($e) > 40 ? "..." : ""), " len=", length($e), " ", ($d eq $s ? "roundtrip" : "BROKEN"),
          " ", encoded_base64_length($s), "/", decoded_base64_length($e), " noeol=", length(encode_base64($s, "")), " crlf=", length(encode_base64($s, "\r\n")), "\n";
}
print "url: ", encode_base64url("\xfb\xff\xfe?>"), " ", show(decode_base64url("-__-Pz4")), " ", encode_base64url("ab"), "\n";
print "decode odd: ", join("|", map { show(decode_base64($_)) } "YWJj", "YWJj\n", "YW Jj\r\nZGVm", "YWI=", "YQ==", "YQ", "Y", "=YWJj", "YW=Jj", "!!YWJj!!", "YWJjZA", "YWJj====", ""), "\n";
print "class methods: ", MIME::Base64::encode("hi", ""), " ", MIME::Base64::decode("aGk="), "\n";
my @qp = ("", "plain text", "caf\xc3\xa9 = 5", "tab\there", "trailing space \nnext line\t\n", "a" x 80, ("b" x 74) . "=", ("c" x 75) . "\n" . ("d" x 76) . "\n", "line1\r\nline2",
          "=" x 30, "no newline at end", ("e" x 73) . "\xff\n", ("f" x 72) . "\xff\xfe", "x\n" x 3, "\x00\x01\x7f\x80", ("g" x 75) . " ", "sp at end of long " . ("h" x 60) . " \n");
for my $s (@qp) {
    my $e = encode_qp($s); my $d = decode_qp($e);
    print "qp ", length($s), ": ", show($e), " ", ($d eq $s ? "roundtrip" : "differs:" . show($d)), "\n";
}
print "qp eol: ", show(encode_qp("a\nb" . ("i" x 80), "\r\n")), " | none: ", show(encode_qp("a\nb" . ("j" x 80), "")), " | bin: ", show(encode_qp("a\nb\r\n", "\n", 1)), "\n";
print "qp decode: ", join("|", map { show(decode_qp($_)) } "a=3Db", "soft=\nbreak", "soft=  \r\nbreak", "trail  \nx", "bad=ZZ", "end=", "=4a=4A", "keep  ", "a=\n", "cr\r\nlf"), "\n";
