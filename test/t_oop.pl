use strict; no warnings;
use lib 'lib'; use lib 'test/lib';

package Animal;
sub new {
    my ($class, %args) = @_;
    my $self = bless { name => $args{name} || 'noname', legs => 4 }, $class;
    return $self;
}
sub name  { my $self = shift; return $self->{name} }
sub legs  { $_[0]->{legs} }
sub speak { my $self = shift; return $self->name . " makes a sound" }
sub describe { my $self = shift; return ref($self) . ":" . $self->name . "/" . $self->legs }

package Dog;
our @ISA = ('Animal');
sub new {
    my ($class, %args) = @_;
    my $self = Animal::new($class, %args);
    $self->{tricks} = [];
    return $self;
}
sub speak { my $self = shift; return $self->name . " barks" }
sub add_trick { my ($self, $t) = @_; push @{ $self->{tricks} }, $t; return $self }
sub tricks { @{ $_[0]->{tricks} } }

package Puppy;
use parent -norequire, 'Dog';
sub speak { my $self = shift; return "small " . $self->SUPER::speak() }

package main;

my $a = Animal->new(name => 'generic');
my $d = Dog->new(name => 'Rex');
my $p = Puppy->new(name => 'Bit');
print $a->speak, "\n";
print $d->speak, "\n";
print $p->speak, "\n";
print $d->describe, " ", $p->describe, "\n";
print ref($a), " ", ref($d), " ", ref($p), " ", ref([]), " ", ref({}), "\n";
$d->add_trick('sit')->add_trick('roll');
print join(",", $d->tricks), " ", scalar($d->tricks), "\n";
print $p->isa('Dog') ? 1 : 0, $p->isa('Animal') ? 1 : 0, $a->isa('Dog') ? 1 : 0, Puppy->isa('Animal') ? 1 : 0, "\n";
print $d->can('add_trick') ? "can" : "cannot", " ", $a->can('add_trick') ? "can" : "cannot", "\n";
my $m = "speak"; print $d->$m, " | ", $d->$m(), "\n";
my $cref = $d->can('name'); print $d->$cref, "\n";
my $class = "Dog"; my $d2 = $class->new(name => 'Dyn'); print $d2->name, "\n";
print Animal->new->name, "\n";
my $h = { x => 1 }; bless $h, 'Animal'; print ref($h), " ", $h->{x}, "\n";
my $obj = bless [], 'Animal'; print ref($obj), "\n";
eval { $h->nosuch }; print "err: ", substr($@, 0, 40), "\n";
eval { my $u = {}; $u->name }; print "unblessed: ", substr($@, 0, 20), "\n";
print __PACKAGE__, "\n";
package Counter;
sub new { my $c = shift; return bless { n => 0 }, $c }
sub inc { $_[0]{n}++; return $_[0] }
sub value { $_[0]{n} }
package main;
print Counter->new->inc->inc->inc->value, "\n";
print "done\n";
# パッケージ名を付けたメソッド呼び出し
{ package FqBase; sub new { bless {}, shift } sub who { "base who(" . ref($_[0]) . ")" } sub only_base { "only base" } }
{ package FqKid; our @ISA = ('FqBase'); sub who { "kid who" } sub via { my $s = shift; $s->FqBase::who() . " / " . $s->FqKid::who() . " / " . $s->FqKid::only_base() } }
{ package FqOther; sub helper { "other helper for " . ref($_[0]) } }
my $fq = FqKid->new;
print "qualified method: ", $fq->who, " / ", $fq->FqBase::who, " / ", $fq->via, "\n";
print "unrelated package: ", $fq->FqOther::helper(), " / ", FqKid->FqBase::only_base(), "\n";
eval { $fq->FqBase::no_such_method() };
print "missing qualified method: ", ($@ =~ /^Can't locate object method "no_such_method" via package "FqBase"/ ? "dies" : "no: $@"), "\n";
# UNIVERSAL::isa / UNIVERSAL::can を関数として呼ぶ
{ package UvBase; sub new { bless [], shift } sub hello { "hello" } }
{ package UvKid; our @ISA = ('UvBase'); }
my $uv = UvKid->new;
print "UNIVERSAL::isa: ", join("", map { $_ ? 1 : 0 } UNIVERSAL::isa($uv, 'UvKid'), UNIVERSAL::isa($uv, 'UvBase'), UNIVERSAL::isa($uv, 'ARRAY'), UNIVERSAL::isa($uv, 'HASH'), UNIVERSAL::isa($uv, 'Other')), "\n";
print "isa on plain refs: ", join("", map { $_ ? 1 : 0 } UNIVERSAL::isa([], 'ARRAY'), UNIVERSAL::isa({}, 'HASH'), UNIVERSAL::isa({}, 'ARRAY'), UNIVERSAL::isa(sub {}, 'CODE'), UNIVERSAL::isa(\1, 'SCALAR'), UNIVERSAL::isa(\\1, 'REF'), UNIVERSAL::isa(qr/x/, 'Regexp')), "\n";
print "isa on names and non-refs: ", join("", map { $_ ? 1 : 0 } UNIVERSAL::isa('UvKid', 'UvBase'), UNIVERSAL::isa('UvBase', 'UvKid'), UNIVERSAL::isa('NoSuchClass', 'UvBase'), UNIVERSAL::isa(undef, 'ARRAY'), UNIVERSAL::isa("string", 'ARRAY'), UNIVERSAL::isa(42, 'SCALAR')), "\n";
my $uv_can = UNIVERSAL::can($uv, 'hello');
print "UNIVERSAL::can: ", ref($uv_can), " ", $uv_can->($uv), " ", (UNIVERSAL::can('UvKid', 'new') ? "class" : "no"), " ", (defined UNIVERSAL::can($uv, 'nope') ? "found" : "undef"), " ", (defined UNIVERSAL::can([], 'hello') ? "found" : "undef"), " ", (defined UNIVERSAL::can(undef, 'hello') ? "found" : "undef"), "\n";
# クラス名 main と、文字列リテラルをクラス名にしたメソッド呼び出し
sub mn_hello { "hi from main" }
my $mn_cls = "main";
print "class main: ", ($mn_cls->can("mn_hello") ? "can" : "cannot"), " ", (main->can("mn_hello") ? "can" : "cannot"), " ", $mn_cls->mn_hello, " / ", main->mn_hello, " ", (UNIVERSAL::can("main", "mn_hello") ? "can" : "cannot"), " ", (main->can("mn_nope") ? "y" : "n"), "\n";
{ package StrCls; sub new { bless { v => $_[1] }, $_[0] } sub get { $_[0]{v} } sub name { "StrCls" } }
print "string as class: ", "StrCls"->new(5)->get, " ", 'StrCls'->name, " ", ("StrCls"->can("get") ? "can" : "cannot"), " ", "main"->mn_hello, "\n";
# Class::->method (末尾の :: はクラス名の文字列)
{ package Tc::Deep; sub new { bless { v => $_[1] }, $_[0] } sub get { $_[0]{v} } sub name { "name of $_[0]" } }
my $tc_name = Tc::Deep::; my %tc_h = (StrCls:: => 1);
print "trailing :: ", Tc::Deep::->new(3)->get, " ", Tc::Deep::->name, " ", StrCls::->name, " $tc_name ", join(",", keys %tc_h), " ", (1 ? StrCls::->name : 0), "\n";
{ my ($p, $q) = (StrCls->new(1), StrCls->new(2)); print "paren invocant: ", ($p || $q)->get, (0 || $q)->get, ((StrCls->new(5)))->get, "\n"; }

