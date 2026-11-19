package TestImpExp;
# Exporter を継承する昔ながらの書き方: 組み込みのエクスポート処理で扱う
use strict;
require Exporter;
our @ISA = ('Exporter');
our @EXPORT = ('exp_default');
our @EXPORT_OK = ('exp_optional');
sub exp_default  { "default" }
sub exp_optional { "optional" }
1;
