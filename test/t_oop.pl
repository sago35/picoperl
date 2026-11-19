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

