package File::Spec;
# File::Spec の picoperl 版。本家は OS ごとの実装に切り替えるが、picoperl 版は
# Unix 形式 (File::Spec::Unix) だけを持つ。ボードのドライブのパスも "/" 区切り。
#
#   use File::Spec;
#   my $path = File::Spec->catfile("lib", "Foo", "Bar.pm");     # lib/Foo/Bar.pm
#   my ($vol, $dir, $file) = File::Spec->splitpath($path);
use strict;
use File::Spec::Unix;
our $VERSION = '3.94';
our @ISA = ('File::Spec::Unix');

1;
