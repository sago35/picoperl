use strict; use warnings;
# AUTOLOAD: 未定義のサブ・メソッドの呼び出しを受け取る
{
    package AL::Fn; our $AUTOLOAD;
    sub AUTOLOAD { my $n = $AUTOLOAD; $n =~ s/.*:://; return if $n eq 'DESTROY'; "fn:$n(" . join(",", @_) . ")" }
    sub real { "real" }
    package AL::Obj; our $AUTOLOAD;
    sub new { bless { v => $_[1] }, $_[0] }
    sub AUTOLOAD { my $self = shift; (my $n = $AUTOLOAD) =~ s/.*:://; return if $n eq 'DESTROY'; die "no method $n\n" unless $n =~ /^get_(\w+)$/; ref($self) ? $self->{$1} : "class:$1" }
    package AL::Child; our @ISA = ('AL::Obj');
    package AL::Lazy; our $AUTOLOAD; my %T = (ONE => 1, TWO => 2); our $made = 0;
    sub AUTOLOAD { (my $n = $AUTOLOAD) =~ s/.*:://; die "no constant $n\n" unless exists $T{$n}; my $v = $T{$n}; $made++; no strict 'refs'; *{"AL::Lazy::$n"} = sub { $v }; goto &{"AL::Lazy::$n"} }
    package AL::None; sub here { 1 }
}
print AL::Fn::anything(1, 2), " ", AL::Fn::real(), " ", AL::Fn::other(), "\n";
my $o = AL::Obj->new(42); print $o->get_v, " ", AL::Obj->get_x, " ", AL::Child->new(7)->get_v, "\n";
print eval { $o->nothing; 1 } ? "?" : "err: $@";
print AL::Lazy::ONE() + AL::Lazy::TWO() + AL::Lazy::ONE(), " made=$AL::Lazy::made\n";
print eval { AL::Lazy::THREE(); 1 } ? "?" : "err: $@";
print eval { AL::None::missing(); 1 } ? "?" : "err: " . ($@ =~ /^(Undefined subroutine &AL::None::missing called)/)[0] . "\n";
print eval { AL::None->missing; 1 } ? "?" : "err: " . ($@ =~ /^(Can't locate object method "missing" via package "AL::None")/)[0] . "\n";
my $cr = \&AL::Fn::via_ref; print $cr->("r"), " ", (defined &AL::Fn::via_ref ? "defined" : "not defined"), " ", (AL::Fn->can('nope') ? "can" : "cannot"), "\n";
sub AUTOLOAD { our $AUTOLOAD; "main:$AUTOLOAD" } print undefined_in_main(), "\n";
