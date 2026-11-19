package TestImpChild;
# import を定義せず、TestImpBase から継承する
use strict;
require TestImpBase;
our @ISA = ('TestImpBase');
1;
