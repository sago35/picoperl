use strict; no warnings;
use constant PI => 3.14159;
use constant { E => 2.718, NAME => "const" };
use constant LIST => (1, 2, 3);
print PI, " ", E, " ", NAME, " ", scalar(my @l = LIST), " ", (LIST)[1], "\n";
print "pi=" . PI . "\n";
my $r = eval { die "boom\n"; 1 }; print defined $r ? "def" : "undef", " err=$@";
$r = eval { 42 }; print "$r [", $@, "]\n";
eval { die { code => 7 } }; print "obj: $@->{code}\n";
eval { my $x = 1 / 0; }; print "div: $@";
eval { die "no newline" }; print "msg: $@";
eval { eval { die "inner\n" }; print "in: $@"; die "outer\n" }; print "out: $@";
sub risky { die "in sub\n" } eval { risky() }; print "sub: $@";
my @w; local $SIG{__WARN__} = sub { push @w, $_[0] } if 0;
print "sort num: ", join(",", sort { $a <=> $b } (10, 9, 100, 1)), "\n";
print "sort str: ", join(",", sort (10, 9, 100, 1)), "\n";
print "sort rev: ", join(",", reverse sort { $a <=> $b } (3, 1, 2)), "\n";
sub by_num { $a <=> $b } print "sort sub: ", join(",", sort by_num (3, 1, 2)), "\n";
print "map hash: ", join(",", map { "$_->[0]:$_->[1]" } sort { $a->[0] cmp $b->[0] } map { [$_, length $_] } qw(bb a ccc)), "\n";
my %seen; my @uniq = grep { !$seen{$_}++ } qw(a b a c b); print "@uniq\n";
print "grep count: ", scalar(grep { $_ > 2 } 1..5), "\n";
print "map expr: ", join(",", map $_ * 2, 1..3), " grep expr: ", join(",", grep $_ % 2, 1..5), "\n";
print "map pairs: ", join(",", map { ($_, $_ * 2) } 1..2), "\n";
my %hm = map { $_ => 1 } qw(x y); print join(",", sort keys %hm), "\n";
print "nested map: ", join(";", map { my $x = $_; join(",", map { $x * $_ } 1..2) } 1..2), "\n";
print "wantarray: ", (sub { wantarray ? "list" : defined(wantarray) ? "scalar" : "void" }->()), "\n";
print "chained ternary: ", (5 > 3 ? 5 > 4 ? "a" : "b" : "c"), "\n";
print "unless/else: "; unless (0) { print "u" } else { print "e" } print "\n";
print "complex lvalue: "; my @arr; $arr[$_] = $_ ** 2 for 0..3; print "@arr\n";
my %h; $h{$_ % 3} .= $_ for 1..6; print join(",", map { "$_=$h{$_}" } sort keys %h), "\n";
print "string mult: ", "3" * "4", " ", "3" x "4", "\n";
print "numeric string cmp: ", (10 == 10.0 ? "t" : "f"), ("10" eq "10.0" ? "t" : "f"), "\n";
print "list assign count: ", scalar(my ($a1, $b1) = (1, 2, 3)), "\n";
print "swap: "; my ($p, $q) = (1, 2); ($p, $q) = ($q, $p); print "$p$q\n";
print "local: "; our $g = "global"; sub show { print $g } sub test { local $g = "local"; show() } test(); show(); print "\n";
print "string repetition list: ", join(",", ("a", "b") x 2), "\n";
print "range chars: ", join("", "a".."e"), " ", join(",", "aa".."ad"), "\n";
print "exists sub: ", (exists &show ? 1 : 0), (defined &nothere ? 1 : 0), "\n";
print "int: ", int(3.7), int(-3.7), " ", int("42abc"), " abs: ", abs(-5), abs(5), " sqrt: ", sqrt(16), "\n";
printf("%.4f %.4f %.4f %.4f\n", sin(1), cos(1), exp(1), log(10));
printf("%.4f\n", atan2(1, 1) * 4);
print "hex: ", sprintf("%x", 255), " ", hex("ff"), " oct: ", oct("0755"), " ", sprintf("%o", 493), "\n";
my $line = "abc\n"; print "chop/chomp ret: ", chomp($line), " $line\n";
print "lc/uc in interp: @{[ uc 'x' ]}\n";
print "join empty: [", join(",", ()), "]\n";
print "nested data: ", scalar(@{[ 1, 2, 3 ]}), "\n";
my @stack = (1..5); print "splice neg: ", join(",", splice(@stack, -2)), " left: @stack\n";
print "last in bare: "; { print "a"; last; print "b" } print "\n";
my $i = 0; { $i++; redo if 0; } print "redo skipped $i\n";
print "undef in list: ", join(",", map { defined $_ ? $_ : "undef" } (1, undef, 3)), "\n";
print "numeric contexts: ", "3 apples" * 2, " ", "0 but true" + 5, " ", "" + 0, " ", undef // 0, "\n";
print "negative modulo: ", -7 % 3, " ", 7 % -3, " ", -7 % -3, "\n";
print "big ints: ", 2 ** 62, " ", 2 ** 63, " ", 9007199254740993, " ", -2 ** 63, "\n";
print "float fmt: ", 1e100, " ", 1.23456789012345678, " ", 100 * 1.1, " ", 3.0, " ", 2.50, "\n";
print "inc float: ", do { my $f = 1.5; $f++; $f }, "\n";
# $] / $^O / use vars
print "version var: ", ($] >= 5.010 ? "new" : "old"), " ", ($] =~ /^5\.\d{6}$/ ? "fmt ok" : "fmt ng"), " ", ($] lt "5.008" ? "lt" : "ge"), " ", ("v[$]]" eq "v[" . $] . "]" ? "interp ok" : "interp ng"), "\n";
print "osname: ", (length($^O) > 0 ? "set" : "empty"), " ", ("$^O" eq $^O ? "interp ok" : "interp ng"), " ", ($^O eq 'VMS' ? "vms" : "not vms"), "\n";
{
    package VarsDemo;
    use strict;
    use vars qw($VERSION @ISA %opt);
    $VERSION = '1.02';
    @ISA = ();
    %opt = (a => 1);
    sub version { $VERSION }
}
print "use vars: $VarsDemo::VERSION ", VarsDemo::version(), " ", scalar(@VarsDemo::ISA), " $VarsDemo::opt{a}\n";
# %ENV と ${^NAME}
print "env: ", (defined $ENV{PATH} && length $ENV{PATH} ? "path" : "nopath"), " ", (exists $ENV{PICOPERL_NO_SUCH_VAR} ? "exists" : "missing"), " ", (scalar(keys %ENV) > 1 ? "many" : "few"), "\n";
{ package EnvPkg; sub path_len { length $ENV{PATH} } }
print "env in package: ", (EnvPkg::path_len() == length($ENV{PATH}) ? "same" : "differ"), "\n";
sub env_local { local $ENV{PATH} = "/x"; local @ENV{qw(HOME PICOPERL_TMP)} = ("/h", "t"); "$ENV{PATH} $ENV{HOME} $ENV{PICOPERL_TMP}" }
my $env_before = $ENV{PATH};
print "env local: ", env_local(), " ", ($ENV{PATH} eq $env_before ? "restored" : "changed"), " ", (exists $ENV{PICOPERL_TMP} ? "left" : "gone"), "\n";
print "caret names: taint=${^TAINT} phase=${^GLOBAL_PHASE} ", (defined ${^WARNING_BITS} ? "bits" : "nobits"), " ", ${^UNICODE} + 0, "\n";
{ no strict; ${^PICO_TEST} = 41; print "caret user: ", ${^PICO_TEST} + 1, " ", "interp ${^PICO_TEST}", "\n"; }
$^H{pico_hint} = "h"; print "hint hash: $^H{pico_hint} ", scalar(grep { $_ eq "pico_hint" } keys %^H), "\n";
# local を要素とスライスに
our %lh = (a => 1, b => 2); our @la = (1, 2, 3); my $lr = { k => "v", list => [1, 2] };
sub lh_show { join(",", map { "$_=" . ($lh{$_} // "undef") } sort keys %lh) . " [@la] $lr->{k} @{$lr->{list}}" }
sub l_elem  { local $lh{a} = 9; local $lh{new} = 5; local $la[0] = 7; local $lr->{k} = "w"; local $lr->{list}[1] = 8; lh_show() }
sub l_slice { local @lh{qw(a c)} = (8, 9); local @la[1, 2] = (5, 6); lh_show() }
sub l_bare  { local @lh{qw(a b)}; local $la[-1]; join(",", map { defined $lh{$_} ? "def" : "undef" } qw(a b)) . (defined $la[2] ? " def" : " undef") }
sub l_die   { local $lh{a} = "tmp"; die "x\n" }
print "local elem: ", l_elem(), "\n";
print "local slice: ", l_slice(), "\n";
print "local bare: ", l_bare(), "\n";
eval { l_die() };
print "local after: ", lh_show(), "\n";
# utf8:: の関数 (use utf8 なしで呼べる)
my $u8 = "abc"; my $u8r = utf8::encode($u8);
print "utf8 functions: ", chr(utf8::unicode_to_native(65)), utf8::native_to_unicode(66), " ", (utf8::is_utf8("abc") ? "flagged" : "bytes"), " ", (utf8::valid("abc") ? "valid" : "invalid"), " $u8\n";
# local のリストに要素とスライスを書く
our %ll = (a => 1, b => 2); our ($ls, @lr) = ("g", 1, 2, 3);
sub ll_show { "$ls " . join(",", map { "$_=" . ($ll{$_} // "undef") } sort keys %ll) . " [@lr]" }
sub ll_plain  { local($ls, $ll{a}, @lr[0, 1]); $ls = "L"; $ll{a} = 9; ll_show() }
sub ll_assign { local($ll{a}, $ls, $ll{new}) = ("la", "ls", "ln"); ll_show() }
sub ll_mixed  { local($ls, $lr[0], @lr[1, 2]) = (6, 7, 8, 9); ll_show() }
print "local list: ", ll_plain(), " / ", ll_show(), "\n";
print "local list assign: ", ll_assign(), " / ", ll_show(), "\n";
print "local list mixed: ", ll_mixed(), " / ", ll_show(), "\n";
sub ll_guard { local($@, $!, $SIG{__DIE__}); eval { die "inner\n" }; "inside: $@" }
$@ = "outer"; (my $ll_r = ll_guard()) =~ s/\n//; print "local \$\@ in a list: $ll_r / after: $@\n";
# $, と $\ は print に効く
{ local($,, $\) = ("-", "!\n"); print 1, 2, 3; }
{ local $, = ","; print "a", "b"; print "\n"; }
print "plain", " print", "\n";
# local を配列とハッシュに
our %lah = (k => 1); our @laa = (1, 2);
sub la_copy  { local %lah = %lah; $lah{$_[0]} = 1; local @laa = (@laa, 9); push @laa, 5; scalar(keys %lah) . ":" . scalar(@laa) }
sub la_empty { local %lah; local @laa; $lah{x} = 1; push @laa, 1; scalar(keys %lah) . scalar(@laa) }
sub la_die   { local %lah = (); local @laa; die "x\n" }
sub la_rec   { my $n = shift; local @laa = (@laa, $n); return $n ? la_rec($n - 1) : "@laa" }
sub la_list  { local (@laa, %lah); scalar(@laa) . scalar(keys %lah) }
print "local aggregates: ", la_copy("a"), " ", la_copy("b"), " / ", la_empty(), " / ", la_rec(2), " / ", la_list(), "\n";
eval { la_die() };
print "restored: ", join(",", sort keys %lah), " [@laa]\n";
sub la_argv { local @ARGV = (7, 8); shift(@ARGV) . scalar(@ARGV) }
print "local \@ARGV: ", la_argv(), " ", scalar(@ARGV), "\n";
print "done\n";
