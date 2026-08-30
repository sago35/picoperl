package TestMod;
# exercised by t_modules.pl under both perl and picoperl
use strict;
use Exporter 'import';

our @EXPORT = qw(greet counter $shared @list);
our @EXPORT_OK = qw(secret double);
our $shared = "shared";
our @list = (1, 2, 3);
our $VERSION = "1.0";
my $count = 0;

use constant PI => 3;
use constant { ONE => 1, TWO => 2 };

sub greet { my $who = shift; return "hello, $who (" . __PACKAGE__ . ")" }
sub counter { return ++$count }
sub secret { return "s3cret" }
sub double { return $_[0] * TWO }
sub uses_pi { return PI + ONE }

package TestMod::Inner;
sub name { return __PACKAGE__ }
sub call_outer { return TestMod::greet("inner") }

package TestMod;
sub inner_name { return TestMod::Inner::name() }

1;
