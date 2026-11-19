# 型グロブへの代入とシンボリックリファレンス
use strict;
use warnings;

# --- グロブへの代入: サブルーチン ---
sub hello { "hello @_" }
*greet = \&hello;
print "alias sub: ", greet("a"), "\n";
*twice = sub { $_[0] * 2 };
print "anon sub: ", twice(21), "\n";
print "defined: ", (defined &greet ? 1 : 0), (defined &nothing ? 1 : 0), "\n";

# --- グロブへの代入: 変数の別名 ---
our (@list, %map, $one);
my @src = (1, 2, 3);
*list = \@src;
push @list, 4;
print "alias array: @src / @list\n";
my %h = (a => 1);
*map = \%h;
$map{b} = 2;
print "alias hash: ", join(",", map { "$_=$h{$_}" } sort keys %h), "\n";
my $v = 10;
*one = \$v;
$one++;
print "alias scalar: $v $one\n";

# --- グロブ同士 ---
our (@orig, $orig, @copy, $copy);
@orig = (7, 8); $orig = "o";
sub orig { "orig sub" }
*copy = *orig;
push @copy, 9;
print "glob copy: @orig / $copy / ", copy(), "\n";
print "glob name: ", *orig, " ", *main::orig, " ", *Maker::make, "\n";

# --- 乗算との読み分け ---
my ($x, $y) = (6, 7);
my %p = (w => 3, h => 4);
my @n = (2, 5);
print "mul: ", $x * $y, " ", $x*$y, " ", 2 *$x, " ", $p{w}*$p{h}, " ", $p{w} *$y, " ", $n[0]*$n[1], " ", ($x) * ($y), " ", 2 ** 3, "\n";
my $r = [1, 2, 3];
print "postfix: ", scalar($r->@*), " ", $r->$#*, " ", join("", map { $_ * 2 } $r->@*), "\n";
$x *= 2; print "mul assign: $x\n";

# --- シンボリックリファレンス ---
{
    no strict 'refs';
    my $name = "hello";
    print "sym call: ", &$name("b"), " ", &{"hello"}("c"), " ", &{\&hello}("d"), "\n";
    my $cr = \&{"hello"};
    print "sym coderef: ", ref($cr), " ", $cr->("e"), "\n";
    print "sym defined: ", (defined &{"hello"} ? 1 : 0), (defined &{"nope"} ? 1 : 0), (defined &$name ? 1 : 0), "\n";
    our @data = (1, 2, 3);
    our %conf = (k => "v");
    our $single = "s";
    print "sym vars: @{'data'} ", scalar(@{"main::data"}), " ${'single'} ", ${"main::single"}, " ${'conf'}{k} ", join(",", keys %{"conf"}), "\n";
    push @{"data"}, 4;
    ${"single"} .= "!";
    ${"conf"}{n} = 1;
    print "sym write: @data $single ", scalar(keys %conf), "\n";
    my $elem = "data";
    print "sym elem: $$elem[1] ${$elem}[2] $#{$elem}\n";

    # 動的な名前のグロブ
    for my $f (qw(red green)) {
        *{"color_$f"} = sub { "<$f>" };
        *{"main::up_$f"} = sub { uc $f };
    }
    print "dyn glob: ", color_red(), color_green(), up_red(), "\n";
    my $g = "dynamic";
    *$g = sub { "via scalar" };
    print "glob scalar: ", dynamic(), "\n";
    *{"dyn_arr"} = [5, 6];
    print "dyn arr: @{'dyn_arr'}\n";
}

# --- パッケージをまたぐ ---
{
    package Maker;
    sub make {
        my ($class, @names) = @_;
        no strict 'refs';
        for my $n (@names) {
            *{"${class}::$n"} = sub { my $self = shift; $self->{$n} = shift if @_; $self->{$n} };
        }
    }
    sub local_name { "in Maker" }
    sub by_name { no strict 'refs'; my $n = "local_name"; &$n() . " / " . &{"Maker::local_name"}() }
    our @ISA_LIKE = ("a");
    sub count { no strict 'refs'; scalar @{"ISA_LIKE"} }
}
{
    package Point;
    Maker::make(__PACKAGE__, qw(x y));
    sub new { bless {}, shift }
}
my $pt = Point->new; $pt->x(3); $pt->y(4);
print "accessors: ", $pt->x, ",", $pt->y, " ", Maker::by_name(), " ", Maker::count(), "\n";
{
    package Imp;
    sub import_into { my $to = shift; no strict 'refs'; *{$to . '::' . $_} = \&{$_} for qw(f1 f2); }
    sub f1 { "f1" } sub f2 { "f2" }
}
Imp::import_into("main");
print "export: ", f1(), f2(), "\n";

# --- BEGIN での定義は定数として使える ---
BEGIN { *DEBUG = sub () { 0 } unless defined &DEBUG }
print "const: ", (DEBUG ? "on" : "off"), "\n";

# --- strict refs のエラー ---
for my $code (
    sub { my $n = "data"; my @a = @$n; },
    sub { my $n = "single"; my $s = $$n; },
    sub { my $n = "conf"; my %c = %$n; },
    sub { my $n = "hello"; &$n(); },
    sub { my $n = "a_very_long_name_that_goes_past_thirty_two_characters"; push @$n, 1; },
) {
    eval { $code->() };
    (my $e = $@) =~ s/ at .*//s;
    print "strict: $e\n";
}
{
    no strict 'refs';
    eval { &{"no_such_sub"}() };
    (my $e = $@) =~ s/ at .*//s;
    print "undef sub: $e\n";
}

# --- 繰り返し代入してもメモリが増え続けない ---
for (1 .. 2000) { *list = \@src; *greet = \&hello; }
print "repeat ok: @list\n";

# --- 関連する修正: ループ変数を捕まえるクロージャ、ブロック内の package ---
my @subs;
for my $f (qw(a b c)) { push @subs, sub { $f } }
for my ($k, $val) (x => 1, y => 2) { push @subs, sub { "$k$val" } }
print "loop closures: ", join(",", map { $_->() } @subs), "\n";
{ package Inner; sub where { __PACKAGE__ } }
sub outer_where { package Other; __PACKAGE__ }
print "package scope: ", __PACKAGE__, " ", Inner::where(), " ", outer_where(), " ", __PACKAGE__, "\n";
print "done\n";
