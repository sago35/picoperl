package TestCallerUser;
# サブの中から require されても、use した先の import は TestCallerUser を caller として見る
use strict;
use TestCallerImp;
sub get { CALLER_MARK }
my $who = sub { scalar caller };
our $body_caller = $who->();     # ファイルの文から呼んだサブの caller
1;
