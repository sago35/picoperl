use strict; no warnings;
sub add { return $_[0] + $_[1] }
sub greet { my ($name, $greeting) = @_; $greeting //= "Hello"; return "$greeting, $name!" }
sub noargs { "noargs" }
sub multi { return (1, 2, 3) }
sub ctx { return wantarray ? "list" : "scalar" }
sub fact { my $n = shift; return $n <= 1 ? 1 : $n * fact($n - 1) }
sub last_expr { my $x = shift; if ($x) { "yes" } else { "no" } }
print add(2, 3), "\n";
print greet("World"), " ", greet("Perl", "Hi"), "\n";
print noargs(), " ", noargs, "\n";
my @m = multi(); my $m = multi(); print "@m | $m\n";
my @c = ctx(); my $c = ctx(); print "$c[0] $c\n";
print fact(10), "\n";
print last_expr(1), last_expr(0), "\n";
my $anon = sub { my $x = shift; return $x * 2 };
print $anon->(21), " ", &$anon(4), " ", &{$anon}(5), "\n";
my @subs = map { my $n = $_; sub { $n * 10 } } 1..3;
print join(",", map { $_->() } @subs), "\n";
sub make_counter { my $c = 0; return sub { return ++$c } }
my $c1 = make_counter(); my $c2 = make_counter();
$c1->(); $c1->(); print $c1->(), $c2->(), "\n";
sub modify { $_[0] = "x" }   # no aliasing in picoperl; just check no crash
my $v = "y"; my @copy = ($v); modify($copy[0]); print "ok\n";
sub with_hash { my %o = @_; return join(",", map { "$_=$o{$_}" } sort keys %o) }
print with_hash(a => 1, b => 2), "\n";
sub retarr { my @r = (4,5,6); return @r } my $cnt = retarr(); my ($first) = retarr(); print "$cnt $first\n";
sub early { for my $i (1..10) { return $i if $i == 3 } return -1 } print early(), "\n";
sub shifter { my $a = shift; my $b = shift // "def"; "$a-$b" } print shifter(1), " ", shifter(1, 2), "\n";
print &add(1, 1), "\n";
my $ref = \&add; print $ref->(5, 5), " ", ref($ref), "\n";
sub recurse_list { my ($n, @acc) = @_; return @acc if $n == 0; return recurse_list($n - 1, @acc, $n) } print join(" ", recurse_list(4)), "\n";
sub apply { my ($f, @l) = @_; return map { $f->($_) } @l } print join(",", apply(sub { $_[0] ** 2 }, 1..4)), "\n";
print defined &add ? "def" : "undef", " ", defined &nope ? "def" : "undef", " ", exists &add ? 1 : 0, "\n";
sub proto() { 99 } print proto + 1, "\n";
sub nested { my $x = shift; my $inner = sub { $x + 1 }; return $inner->() } print nested(5), "\n";
sub voidret { return } my @vr = voidret(); my $vr = voidret(); print scalar(@vr), defined($vr) ? "def" : "undef", "\n";
sub args_count { scalar @_ } print args_count(), args_count(1), args_count(1, 2, 3), args_count(()), args_count((1, 2), 3), "\n";
sub str_ret { "abc" . shift } print str_ret("d"), "\n";
my @ws; my $wi = 0; while (my $x = ++$wi <= 3 ? $wi : 0) { push @ws, sub { $x } } print join(",", map { $_->() } @ws), "\n";
my @fs; for my $i (1 .. 3) { my $y = $i * 10; push @fs, sub { $y } } print join(",", map { $_->() } @fs), "\n";
# goto &sub
sub g_target { "target(@_) " . (wantarray ? "list" : defined(wantarray) ? "scalar" : "void") }
sub g_named { goto &g_target }
sub g_ref   { my $c = \&g_target; goto &$c }
sub g_block { goto &{ \&g_target } }
sub g_bsref { goto \&g_target }
sub g_shift { shift; unshift @_, "x"; goto &g_target }
sub g_cond  { goto &g_target if $_[0]; "not taken" }
my @gl = g_named(1, 2); my $gs = g_ref(3);
print "goto: $gl[0] / $gs / ", scalar(g_block(4)), " / ", scalar(g_bsref(5)), " / ", scalar(g_shift(6, 7)), " / ", scalar(g_cond(0)), " / ", scalar(g_cond(8)), "\n";
{ no strict 'refs'; no warnings; sub g_sym { my $n = "g_target"; goto &$n } print "goto sym: ", scalar(g_sym(9)), "\n"; }
sub g_fact { my ($n, $acc) = @_; return $acc if $n <= 1; @_ = ($n - 1, $acc * $n); goto &g_fact }
print "goto tail: ", g_fact(10, 1), "\n";
# プロトタイプ (無視される)
sub p_two($$) { "$_[0]+$_[1]" }
sub p_code(&$) { my ($c, $v) = @_; $c->($v) }
sub p_refs(\@\@) { scalar(@_) }
sub p_opt($;$) { scalar(@_) }
sub p_under(_) { "u:$_[0]" }
my $p_anon = sub ($$) { $_[0] * $_[1] };
print "proto: ", p_two(1, 2), " ", p_code(sub { $_[0] * 2 }, 21), " ", p_opt(1), p_opt(1, 2), " ", p_under("v"), " ", $p_anon->(6, 7), "\n";
print "done\n";
