package TestVars;
# 変数をエクスポートするモジュール (t_modules.pl 用)
use strict;
use Exporter 'import';
our @EXPORT    = qw(%Table $scalar);
our @EXPORT_OK = qw(@list);
our %Table = (k => "v"); our $scalar = "s"; our @list = (1, 2);
1;
