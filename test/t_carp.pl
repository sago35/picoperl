# lib/Carp.pm (picoperl 版) を本家と突き合わせる
use strict;
use warnings;
use Carp;

sub show { my $e = shift; $e =~ s/\n$//; $e =~ s/\n/\\n/g; $e }

{ package Lib; use Carp;
  sub check { my $v = shift; croak "bad value '$v'" unless $v > 0; "ok $v" }
  sub outer { inner(@_) }                      # 同じパッケージの中を通っても、位置はパッケージの外の呼び出し元
  sub inner { croak "from inner" }
  sub warns { carp "careful" }
  sub obj   { croak({ code => 42 }) }
  sub multi { croak "a", "b", 3 }
  sub newline { croak "ends with newline\n" } }
{ package Mid; sub call { Lib::check(@_) } sub deep { Lib::outer() } }

print "croak ok: ", Lib::check(3), "\n";
eval { Lib::check(-1) };      print "croak: ", show($@), "\n";
eval { Mid::call(0) };        print "via other package: ", show($@), "\n";
eval { Lib::outer() };        print "same package frames: ", show($@), "\n";
eval { Mid::deep() };         print "deep: ", show($@), "\n";
eval { Lib::multi() };        print "multiple args: ", show($@), "\n";
eval { Lib::newline() };      print "newline: ", show($@), "\n";
eval { Lib::obj() };          print "object: ", ref($@), " $@->{code}\n";
# 同じパッケージの中だけで呼ぶと履歴付きになる (本家は eval の段も出すので、1 行目だけ比べる)
sub first_line { (split /\n/, $_[0])[0] }
eval { croak "at top level" }; print "top level: ", first_line($@), "\n";
sub in_main { croak "in main" }
eval { in_main() };           print "main sub: ", first_line($@), " / ", ($@ =~ /\n\tmain::in_main\(\) called at \S+ line \d+\n/ ? "trace" : "no trace"), "\n";

{ local $SIG{__WARN__} = sub { print "carp: ", show($_[0]), "\n" }; Lib::warns(); carp "direct"; }

eval { Carp::croak("qualified") }; print "qualified: ", first_line($@), "\n";
{ package NoImport; sub f { require Carp; Carp::croak("required") } }
eval { NoImport::f() };       print "require: ", show($@), "\n";

sub conf_inner { confess "full trace" } sub conf_outer { conf_inner() }
eval { conf_outer() };
my @lines = split /\n/, $@;
print "confess: ", $lines[0], " / ", scalar(@lines) >= 3 ? "has trace" : "no trace", " / ", ($lines[1] =~ /^\tmain::conf_inner\(.*\) called at / ? "frame ok" : "frame: $lines[1]"), "\n";
print "shortmess: ", show(Carp::shortmess("sm")), "\n";
print "done\n";
