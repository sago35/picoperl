package IO;
# IO の picoperl 版。本物は XS で、IO::Handle / IO::File / IO::Seekable が使う関数のうち
# C で書かれた部分を提供している。ここではそれを Perl で用意する (バッファや非ブロッキングの
# 設定は何もしない)。IO::Handle などの本体は同梱していないので、perl のライブラリのものを使う。
use strict;
our $VERSION = "1.56";

sub import {
    shift;
    for my $m (@_ ? @_ : qw(Handle Seekable File)) { my ($n) = $m =~ /(\w+)/; require "IO/$n.pm" }
}

package IO::Handle;
sub blocking { 1 }                       # いつもブロッキング
sub error    { 0 }
sub clearerr { 0 }
sub untaint  { 0 }
sub flush    { "0 but true" }            # 出力はバッファしていない
sub sync     { "0 but true" }
sub setbuf   { return }
sub setvbuf  { return }
sub ungetc   { die "IO::Handle::ungetc is not available in picoperl\n" }
sub getline  { @_ == 1 or die 'usage: $io->getline()' . "\n"; my $fh = shift; scalar <$fh> }
sub gets     { @_ == 1 or die 'usage: $io->gets()' . "\n"; my $fh = shift; scalar <$fh> }
sub getlines { @_ == 1 or die 'usage: $io->getlines()' . "\n"; wantarray or die 'Can\'t call $io->getlines in a scalar context, use $io->getline' . "\n"; my $fh = shift; <$fh> }

package IO::Seekable;
sub getpos { tell($_[0]) }
sub setpos { seek($_[0], $_[1], 0) }

package IO::File;
sub new_tmpfile { die "IO::File::new_tmpfile is not available in picoperl\n" }

1;
