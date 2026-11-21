package MIME::Base64;
# Base64 の符号化と復号。
#
#   use MIME::Base64;                      # encode_base64 decode_base64
#   my $text = encode_base64($bytes);      # 76 文字ごとに改行。encode_base64($bytes, "") で改行なし
#   my $bytes = decode_base64($text);
#   use MIME::Base64 qw(encode_base64url decode_base64url);   # URL 用 (+/ の代わりに -_、= なし)
#
# 関数はすべてインタプリタが C で持っていて、このファイルはエクスポートするだけです
# (perl 本体の XS 版と同じ結果になるように、同じ手順で書いてあります)。
use strict;
use Exporter 'import';
our @EXPORT = qw(encode_base64 decode_base64);
our @EXPORT_OK = qw(encode_base64url decode_base64url encoded_base64_length decoded_base64_length);
our $VERSION = '3.16';

*encode = \&encode_base64;
*decode = \&decode_base64;

1;
