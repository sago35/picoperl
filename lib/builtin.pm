package builtin;
# The perl 5.36+ builtin:: functions (the stable subset).
#
#   use builtin qw(true false ceil floor trim blessed reftype refaddr indexed);
#
# All of them are C natives registered by the interpreter (builtin::trim(...) works without
# loading this file, and `use v5.40;` makes them visible by their plain names); this file
# only exports them. true / false are redefined here with an empty prototype, so that the
# imported names take no arguments. weaken / unweaken do nothing and is_weak / is_tainted
# say no (reference counting only, no taint mode). Not provided: is_bool, the created_as_*
# checks, stringify, inf / nan, export_lexically.
use strict;
use Exporter 'import';
our @EXPORT_OK = qw(true false ceil floor trim blessed reftype refaddr indexed weaken unweaken is_weak is_tainted);

{ no warnings 'redefine';
  sub true ()  { 1 == 1 }
  sub false () { 1 == 0 }
}

1;
