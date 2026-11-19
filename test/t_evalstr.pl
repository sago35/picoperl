# eval STRING
use strict;
use warnings;

# 値と文脈
print "value: ", eval "1 + 2", " ", eval("'a' . 'b'"), " ", scalar(eval "my \@t = (4, 5, 6); \@t"), " ", join(",", eval "(4, 5, 6)"), " ", eval("2") + 1, "\n";
my @list = eval "map { \$_ * 2 } 1 .. 3"; print "list: @list\n";
print "empty: ", defined(eval "") ? "def" : "undef", " ", defined(eval "undef") ? "def" : "undef", " [$@]\n";
$_ = "7 * 6"; print "default arg: ", eval, "\n";
print "return: ", eval "return 5; 6", " ", join(",", eval "return (1, 2)"), "\n";

# 外側の my 変数を読み書きする
my $x = 10; my @arr = (1, 2); my %h = (k => "v");
print "read lexicals: ", eval '$x + $arr[1] . $h{k}', "\n";
eval '$x++; push @arr, 3; $h{n} = scalar @arr;';
print "write lexicals: $x @arr $h{n}\n";
sub with_args { my ($p, $q) = @_; my $r = eval '$p * $q + @_'; $r }
print "sub lexicals and \@_: ", with_args(3, 4), "\n";
{ my $x = "inner"; print "shadowing: ", eval('$x'), " "; } print eval('$x'), "\n";
for my $i (1 .. 3) { print "loop var: ", eval('$i * $i'), "\n" if $i == 3 }
my $op = "<"; my ($l, $r2) = (3, 5);
print "built expression: ", (eval "\$l $op \$r2" ? "true" : "false"), " ", (eval "\$l > \$r2" ? "true" : "false"), "\n";

# エラー
my $v = eval "die qq(boom\\n); 1"; print "die: ", defined $v ? "def" : "undef", " [", $@ =~ s/\n//r, "]\n";
$v = eval "1 + ;"; print "syntax error: ", defined $v ? "def" : "undef", " ", ($@ ne "" ? "has message" : "no message"), "\n";
$v = eval "1"; print "cleared: [$@] $v\n";
$v = eval { eval "die { code => 3 }"; $@->{code} }; print "die with ref: $v\n";
eval 'no_such_function_xyz(1)'; print "undefined sub: ", ($@ =~ /Undefined subroutine &main::no_such_function_xyz/ ? "reported" : $@), "\n";
eval 'my $z = $undeclared_var_xyz;'; print "strict inherited: ", ($@ =~ /Global symbol "\$undeclared_var_xyz"/ ? "error" : "no error [$@]"), "\n";
print "nested: ", eval "eval('2 + 3') * 2", " ", eval { eval "die 'inner'"; "after" }, "\n";

# 定義
eval 'sub made_by_eval { "made:" . shift }'; print "sub: ", made_by_eval("x"), "\n";
my $counter = 0; my $inc = eval 'sub { ++$counter }'; $inc->(); $inc->(); print "closure: $counter\n";
eval 'package EvalPkg; our $name = __PACKAGE__; sub hello { "hello from " . __PACKAGE__ }';
{ no warnings 'once'; print "package: $EvalPkg::name ", EvalPkg::hello(), " ", __PACKAGE__, "\n"; }
{ package InPkg; sub where { eval '__PACKAGE__' } sub call { eval 'helper()' } sub helper { "helper in pkg" } }
print "current package: ", InPkg::where(), " ", InPkg::call(), "\n";
our $glob = "g"; print "globals: ", eval('$glob . $main::glob'), "\n";

# require / use
print "require: ", (eval "require List::Util; 1" ? "ok" : "failed: $@"), " ", (eval "require No::Such::Module::Xyz; 1" ? "ok" : "failed"), " ", ($@ =~ /^Can't locate No\/Such\/Module\/Xyz\.pm/ ? "message ok" : $@), "\n";
my $class = "List::Util"; print "require class: ", (eval "require $class" ? "loaded" : "failed"), " ", List::Util::sum(1, 2), "\n";
print "use: ", eval "use List::Util qw(max); max(3, 9, 4)", "\n";
print "version string: ", eval("1.25") + 1, " ", eval('"5.010"') + 0, "\n";

# 繰り返してもメモリが増え続けない
my $sum = 0; $sum += eval "$_ * 2" for 1 .. 2000; print "many evals: $sum\n";
print "done\n";
