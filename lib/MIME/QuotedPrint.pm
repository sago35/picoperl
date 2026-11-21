package MIME::QuotedPrint;
# quoted-printable の符号化と復号。
#
#   use MIME::QuotedPrint;                 # encode_qp decode_qp
#   my $text = encode_qp($bytes);          # encode_qp($bytes, $eol, $binmode)
#   my $bytes = decode_qp($text);
#
# 2 つの関数はインタプリタが C で持っています (perl 本体の XS 版と同じ手順)。
use strict;
use Exporter 'import';
our @EXPORT = qw(encode_qp decode_qp);
our $VERSION = '3.16';

*encode = \&encode_qp;
*decode = \&decode_qp;

1;
