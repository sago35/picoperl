package Time::HiRes;
# Fractional sleeps and time, like the core Time::HiRes.
#   use Time::HiRes qw(usleep sleep time);
#   usleep(100_000);   # microseconds (USB keeps running)
#   sleep(0.5);        # seconds, fractional
#   time();            # seconds since boot as a float
# The builtin `sleep` also accepts fractional seconds in picoperl.
use strict;
use Exporter 'import';
our @EXPORT_OK = qw(usleep sleep time);

sub usleep { my $us = shift; return $us >= 1000 ? Time::HiRes::_sleep_ms(int($us / 1000)) + Time::HiRes::_usleep($us % 1000) : Time::HiRes::_usleep($us) }
sub sleep  { my $s = shift; return Time::HiRes::_sleep_ms(int($s * 1000)) }
sub time   { return Time::HiRes::_time_ms() / 1000 }

1;
