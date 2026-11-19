package TestImpBase;
# 自前の import を持つ親クラス (t_modules.pl 用)。子クラスは import を継承する。
use strict;
our @log;
sub import {
    my ($class, @args) = @_;
    my $into = caller;
    push @log, "$class imported into $into with (@args)";
    no strict 'refs';
    *{"${into}::imp_marker"} = sub { "marker from $class" };
}
1;
