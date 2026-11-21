package Errno;
# Errno の picoperl 版。エラー番号の定数 (ENOENT、EEXIST など約 130 個、Linux の値) と %! を提供する。
#
# 定数はインタプリタが表を持っていて、使われたときに初めて作られる (lib/Fcntl.pm と同じ方式)。
#   use Errno qw(ENOENT EACCES);    if ($! == ENOENT) { ... }
#   use Errno;                      if ($!{ENOENT}) { ... }     # %! は use Errno したあとで使える
use strict;
our $VERSION = "1.39";
our @EXPORT_OK   = qw(E*);
our %EXPORT_TAGS = (POSIX => ['E*']);

# %! : $!{NAME} は、$! がそのエラーのとき番号 (真)、違えば 0、知らない名前なら ""
sub _num { my $n = shift; no strict 'refs'; $n =~ /^E[A-Z0-9]+$/ && defined &{"Errno::$n"} ? &{"Errno::$n"}() : undef }
sub TIEHASH  { bless [] }
sub FETCH    { my $v = _num($_[1]); defined $v ? ($v == $! ? $v : 0) : "" }
sub EXISTS   { defined _num($_[1]) }
sub STORE    { die "ERRNO hash is read only!\n" }
sub FIRSTKEY { @{$_[0]} = split ' ', Errno::_NAMES(); shift @{$_[0]} }
sub NEXTKEY  { shift @{$_[0]} }
*CLEAR = *DELETE = \&STORE;
tie %!, __PACKAGE__;
1;
