package POSIX;
# POSIX の picoperl 版 (本家は XS)。ボードで意味のある部分だけです。
#
#   use POSIX;                          # 下の名前がすべて使える
#   use POSIX qw(floor ceil strftime);  # 名前を指定してもよい
#   print strftime("%Y-%m-%d %H:%M:%S", localtime), "\n";
#
# 関数: floor ceil fmod pow round trunc fabs tan asin acos atan sinh cosh tanh log10 log2 isnan isinf
#       strftime strtol strtod setlocale localeconv getpid _exit access tzset tzname
#       WEXITSTATUS WIFEXITED WTERMSIG WIFSIGNALED
# 定数: SIG*、WNOHANG、LC_*、EXIT_*、INT_MAX / DBL_MAX などの上限、M_PI など、R_OK / W_OK ...、
#       Fcntl の定数 (O_RDONLY、SEEK_SET ...) と Errno の定数 (EAGAIN ...)
#
# どれもインタプリタが C の表で持っていて、使われたときに初めて作られます (`use POSIX;` だけなら RAM は
# ほとんど増えません)。本家との違い: strftime の曜日・月の名前は英語、%Z は "UTC"、%z は "+0000"。
# setlocale は "C" だけ。シグナル (sigaction)、端末 (Termios)、dup2、fork まわりなど、ここに無い関数は
# 呼ぶと Undefined subroutine になります。
use strict;
our $VERSION = "2.20";
our @EXPORT = qw(*);        # どの名前も、最初に使われたときに POSIX:: の同じ名前を探す
1;
