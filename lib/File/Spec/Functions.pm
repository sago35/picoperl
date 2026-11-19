package File::Spec::Functions;
# File::Spec のメソッドを関数として使う (catfile("a", "b") など)。
use strict;
use File::Spec;
use Exporter 'import';
our $VERSION = '3.94';
our @EXPORT    = qw(canonpath catdir catfile curdir rootdir updir no_upwards file_name_is_absolute path);
our @EXPORT_OK = qw(devnull tmpdir splitpath splitdir catpath abs2rel rel2abs case_tolerant);
our %EXPORT_TAGS = (ALL => [@EXPORT, @EXPORT_OK]);

sub canonpath  { File::Spec->canonpath(@_) }
sub catdir     { File::Spec->catdir(@_) }
sub catfile    { File::Spec->catfile(@_) }
sub curdir     { File::Spec->curdir }
sub rootdir    { File::Spec->rootdir }
sub updir      { File::Spec->updir }
sub no_upwards { File::Spec->no_upwards(@_) }
sub file_name_is_absolute { File::Spec->file_name_is_absolute(@_) }
sub path       { File::Spec->path }
sub devnull    { File::Spec->devnull }
sub tmpdir     { File::Spec->tmpdir }
sub splitpath  { File::Spec->splitpath(@_) }
sub splitdir   { File::Spec->splitdir(@_) }
sub catpath    { File::Spec->catpath(@_) }
sub abs2rel    { File::Spec->abs2rel(@_) }
sub rel2abs    { File::Spec->rel2abs(@_) }
sub case_tolerant { File::Spec->case_tolerant }

1;
