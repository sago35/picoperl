use strict; use warnings;
use POSIX; use POSIX qw(round trunc log2 isnan isinf);
print "math: ", join(" ", floor(2.7), ceil(2.1), floor(-2.5), ceil(-2.5), fmod(10, 3), fmod(-7.5, 2), pow(2, 10), round(2.5), round(-2.5), trunc(-2.7), fabs(-3)), "\n";
printf "trig: %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.3f %.3f\n", tan(1), asin(0.5), acos(0.5), atan(1), sinh(1), cosh(1), tanh(1), log10(1000), log2(8);
print "nan: ", (isnan(9**9**9 - 9**9**9) ? 1 : 0), (isnan(1) ? 1 : 0), (isinf(9**9**9) ? 1 : 0), (isinf(1) ? 1 : 0), "\n";
print "strftime: ", strftime("%Y-%m-%d %H:%M:%S", 5, 4, 3, 2, 0, 124), " | ", strftime("%a %A %b %B %e %j %u %w %y %C %I %p %F %T %R %D %%", 59, 59, 23, 29, 1, 100), " | ",
      strftime("%Y-%m-%d %a", 0, 0, 0, 31, 11, 99), " | ", strftime("%d/%m/%Y %j", 0, 0, 12, 1, 2, 125), " | ", strftime("%H:%M %p %I", 0, 5, 0, 1, 0, 70), " | ", strftime("%Y-%m-%d", 0, 0, 0, 32, 0, 124), "\n";
my @t = gmtime(86400 * 365 * 30 + 12345); print "gmtime: ", strftime("%Y-%m-%d %H:%M:%S %a %j", @t), "\n";
print "limits: ", join(" ", INT_MAX, INT_MIN, UINT_MAX, SHRT_MAX, CHAR_BIT, LONG_MAX, DBL_DIG, EXIT_SUCCESS, EXIT_FAILURE, (DBL_MAX > 1e300 ? "big" : "small"), (DBL_EPSILON < 1e-10 ? "eps" : "?")), "\n";
print "consts: ", join(" ", SIGINT, SIGTERM, SIGKILL, SIGALRM, WNOHANG, LC_ALL, LC_CTYPE, SEEK_SET, SEEK_END, O_RDONLY, O_CREAT, EAGAIN, ENOENT, EWOULDBLOCK, R_OK, W_OK, F_OK), "\n";
print "wait: ", join(" ", WEXITSTATUS(256 * 3), (WIFEXITED(256 * 3) ? 1 : 0), WTERMSIG(9), (WIFSIGNALED(9) ? 1 : 0), (WIFSIGNALED(0) ? 1 : 0)), "\n";
my ($n, $left) = strtol("0x1fz", 16); my ($d, $dl) = strtod("3.5abc"); my $one = strtol("42");
print "strto: $n $left $d $dl $one ", scalar(strtol("777", 8)), "\n";
print "locale: ", setlocale(LC_ALL, "C"), " ", (defined setlocale(LC_ALL) ? "def" : "undef"), " ", localeconv()->{decimal_point}, " pid=", (getpid() == $$ ? "same" : "diff"), "\n";
printf "pi: %.6f %.6f\n", POSIX::M_PI(), POSIX::floor(POSIX::M_E() * 100) / 100;
print "qualified: ", POSIX::floor(1.9), POSIX::ceil(1.1), " ", POSIX::strftime("%Y", 0, 0, 0, 1, 0, 99), " ", POSIX::INT_MAX(), "\n";
# 範囲外の日時は繰り上がる (月が 0..11 を外れる場合と秒の 60 は perl 5.40 と 5.42 以降で結果が違うので、ここでは試さない。picoperl は 5.42 以降と同じ)
print "roll: ", join(" | ", strftime("%F %T", 75, 59, 23, 31, 11, 99), strftime("%F", 0, 0, 0, 0, 0, 100), strftime("%F %a", 0, 0, 0, 29, 1, 101), strftime("%F %T", 0, 0, 25, 31, 0, 124), strftime("%F %T %j", 0, 90, 0, 1, 2, 124), strftime("%F", 0, 0, 0, 366, 0, 124), strftime("%F %T", -1, 0, 0, 1, 0, 100)), "\n";
print "access: ", join(" ", (access($0, R_OK) ? "r" : "-"), (access($0, F_OK) ? "f" : "-"), (defined(access("/no/such/file/here", F_OK)) ? "found" : "undef"), (access($0, R_OK) eq "0 but true" ? "0bt" : "other")), "\n";
