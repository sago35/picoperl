package TestExpOnly;
# use Exporter 'import' の書き方: import は Exporter から取り込む (継承しない)
use strict;
use Exporter 'import';
our @EXPORT_OK = qw(eo_one eo_two);
our %EXPORT_TAGS = (all => [qw(eo_one eo_two)]);
sub eo_one { 1 }
sub eo_two { 2 }
1;
