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
# caller
sub c_who { my @c = caller; "$c[0] line $c[2]" }
sub c_scalar { my $p = caller; $p }
sub c_paren { my ($p, $f, $l) = caller(); "$p:" . ($f =~ /t_subs/ ? "file" : "?") . ":$l" }
sub c_frame { my @c = caller(0); "$c[3] " . ($c[5] ? "list" : defined $c[5] ? "scalar" : "void") }
sub c_outer { c_inner() } sub c_inner { my @c = caller(1); my @d = caller 0; "$c[3] > $d[3]" }
sub c_idx { (caller)[0] . "/" . (caller(0))[3] }
sub c_none { my @c = caller(5); scalar(@c) }
print "caller: ", c_who(), " / ", c_scalar(), " / ", c_paren(), "\n";
my @cf = c_frame(); my $cs = c_frame();
print "caller frame: $cf[0] / $cs / ", c_outer(), " / ", c_idx(), " / ", c_none(), "\n";
print "caller top: ", (defined(caller) ? "def" : "undef"), " ", scalar(my @ct = caller), "\n";
{
    package CPkg;
    sub who { my $p = caller; $p }
    sub call_main { main::c_scalar() }
    sub via_anon { my $c = sub { (caller(0))[3] . " from " . caller }; $c->() }
    sub import_like { my $target = caller; no strict 'refs'; *{"${target}::imported"} = sub { "imported into $target" }; }
    our $at_file_level = main::c_scalar();
}
{ package CUser; CPkg::import_like(); our $r = CPkg::who(); }
print "caller pkg: ", CPkg::who(), " ", CPkg::call_main(), " ", CPkg::via_anon(), " $CPkg::at_file_level $CUser::r ", CUser::imported(), " ", c_scalar(), "\n";
package CStmt;
sub probe { my $p = caller; $p }
my $c_stmt = main::c_scalar();
package main;
print "caller stmt: $c_stmt ", CStmt::probe(), " ", c_scalar(), "\n";
# s,,, など区切り文字がカンマの置換とマッチ
my $cm = "a/b/c"; (my $cm2 = $cm) =~ s,/,-,g; my $cm3 = $cm; $cm3 =~ s,/[^/]+$,,;
print "comma delim: $cm2 $cm3 ", ($cm =~ m,^a/, ? "m" : "-"), " ", ($cm !~ m,x, ? "nm" : "-"), " ", join("|", (my $cm4 = $cm) =~ tr,/,,), "\n";
my %ch = (s => 1, m => 2, y => 3); my @cl = ($ch{s}, $ch{m}, $ch{y});
print "comma idents: @cl ", join(",", map { $ch{$_} } qw(s m y)), "\n";
# shift-> / pop->
sub sh_elem { shift->[1] } sub sh_key { shift->{k} } sub sh_meth { shift->name(@_) } sub sh_code { shift->(2) } sub sh_pop { pop->[0] }
{ package ShObj; sub new { bless { n => $_[1] }, $_[0] } sub name { "name:$_[0]{n}:" . scalar(@_) } }
print "shift arrow: ", sh_elem([7, 8]), " ", sh_key({ k => "v" }), " ", sh_meth(ShObj->new("o"), 1, 2), " ", sh_code(sub { $_[0] * 21 }), " ", sh_pop(1, [9]), "\n";
# ブロックの my 変数を使う名前付きサブ
{ my $bc_n = 0; my @bc_list = (1, 2, 3); sub bc_next { ++$bc_n } sub bc_peek { $bc_n } sub bc_sum { my $s = 0; foreach my $x (@bc_list) { $s += $x } $s } }
BEGIN { my %bc_tab = (a => "A"); sub bc_lookup { $bc_tab{$_[0]} // "?" } }
my $bc_top = "top"; sub bc_file { $bc_top }
{ package BcPkg; sub early { "early" } }
sub bc_outer { my $v = shift; sub bc_inner { defined $v ? "def" : "undef" } bc_inner() }
print "block subs: ", bc_next(), bc_next(), bc_peek(), " ", bc_sum(), " ", bc_lookup("a"), bc_lookup("z"), " ", bc_file(), " ", BcPkg::early(), " ", bc_outer(1), "\n";
# BEGIN / use constant から、その上で定義したサブを呼ぶ
my $pd_file_var = "fv";
sub pd_helper { "helped(@_)" }
sub pd_file_var { defined $pd_file_var ? $pd_file_var : "undef" }
our ($pd_from_begin, $pd_seen, $pd_later_known, $pd_unknown);
BEGIN { $pd_from_begin = pd_helper(1); $pd_seen = pd_file_var(); }
use constant PD_CONST => pd_helper("const");
sub pd_later { "later" }
BEGIN { *pd_alias = \&pd_helper; $pd_later_known = defined &pd_later ? "known" : "unknown"; $pd_unknown = defined &pd_not_yet ? "known" : "unknown"; }
sub pd_not_yet { 1 }
print "predefined subs: $pd_from_begin / $pd_seen / ", PD_CONST, " / ", pd_alias(2), " / ", pd_file_var(), " / $pd_later_known $pd_unknown\n";
# use の行より前に定義したサブが、ファイルの my 変数を使う
my $pu_var = 5;
sub pu_before { "before:$pu_var" }
use List::Util qw(sum);
sub pu_after { "after:$pu_var" }
print "subs around use: ", pu_before(), " ", pu_after(), " ", sum(1, 2), "\n";
# BEGIN ブロックからファイルの my 変数を使う
my ($bl_scalar, @bl_list, %bl_hash, $bl_code);
BEGIN { $bl_scalar = 5; @bl_list = (1, 2); $bl_hash{k} = "v"; my $inner = 3; $bl_code = sub { $inner * 2 } }
my $bl_after = $bl_scalar + @bl_list;
print "begin sets file lexicals: $bl_scalar @bl_list $bl_hash{k} ", $bl_code->(), " $bl_after\n";
my $bl_init = "run";
BEGIN { print "begin sees: ", (defined $bl_init ? "a value" : "undef"), "\n"; $bl_init = "begin" }
print "initializer wins: $bl_init\n";
my %bl_reg; BEGIN { $bl_reg{a} = 1 } use constant BL_KEY => "k"; BEGIN { $bl_reg{+BL_KEY} = 2 }
print "begin and use constant: ", join(",", map { "$_=$bl_reg{$_}" } sort keys %bl_reg), "\n";
my @bl_order; { my $blk = 2; BEGIN { push @bl_order, "begin" } push @bl_order, $blk } for my $i (1 .. 2) { my $t = $i; push @bl_order, $t }
print "begin inside a block: @bl_order\n";
# 括弧の式に続く -> : (sub { ... })->(...)、(LIST)[i]->()、do { }->()
sub pa_mk { sub { "mk@_" } } sub pa_f { "f@_" }
my @pa_f = (sub { 1 }, sub { 2 }); my %pa_h = (k => sub { "k@_" });
print "paren arrow: ", (sub { "a@_" })->(1), " ", (sub { $_[0] * 2 })->(21), " ", (@pa_f)[1]->(), " ", ($pa_h{k})->(5), " ",
    ($pa_h{none} || $pa_h{k})->(7), " ", ([1, 2, 3])->[1], " ", ({ a => 5 })->{a}, " ", (1 ? \&pa_f : \&pa_mk)->(3), " ",
    (pa_mk())->(2), " ", do { sub { 9 } }->(), "\n";
my ($pa_a, $pa_b) = (4, 5, 6)[0, 1]; print "list slice still works: $pa_a$pa_b ", (4, 5, 6)[1], "\n";
# ::name は main::name
sub cc_f { "mainf@_" }
{ package CcPkg; sub cc_f { "pkgf" } print "leading ::  ", ::cc_f(1), " ", &::cc_f(2), " ", cc_f(), " ", (\&::cc_f == \&main::cc_f ? "same" : "differs"), "\n"; }
print "done\n";
# try という名前のサブを自分で定義したら、try { ... }; はその呼び出し (組み込みの try/catch ではない)
{
    package TrySub;
    sub try(&) { my $code = shift; my $ok = eval { $code->(); 1 }; return ($ok, $@) }
    my ($ok) = try { die "x\n" };
    my @got;
    try { push @got, "ran" };
    try {
        push @got, "second";
        die "boom\n";
    };
    print "own try: ", ($ok ? "ok" : "caught"), " @got\n";
}
# まだ定義されていないサブへの参照 (\&name): 後から定義されると、先に取った参照や別名からも呼べる
{
    package StubP;
    BEGIN { no strict 'refs'; *is_pos = \&is_positive; *alias2 = \&is_positive; }
    my $early = \&later; my $r1 = \&later;
    print "stub: same ", ($early == $r1 ? 1 : 0), " defined ", (defined &later ? 1 : 0), (defined &is_positive ? 1 : 0), "\n";
    sub is_positive { $_[0] > 0 ? "pos" : "notpos" }
    print "stub: ", is_pos(3), " ", alias2(-1), " ", StubP->can('is_pos')->(5), "\n";
    eval { $early->() }; print "stub early call: ", ($@ =~ /Undefined subroutine &StubP::later called/ ? "undefined" : "other: $@"), "\n";
    eval 'sub later { "now " . shift }'; print "stub: ", $early->("defined"), " ", (defined &later ? 1 : 0), "\n";
    my $never = \&never_defined; eval { $never->() }; print "stub never: ", ($@ =~ /Undefined subroutine &StubP::never_defined called/ ? "undefined" : "other: $@"), "\n";
}
# CORE::name は、同じ名前のサブがあっても組み込みの方
{
    package CoreP; sub length { "mine" } sub uc { "MINE" }
    print "CORE: ", CORE::length("abcd"), " ", CORE::uc("x"), " ", CoreP::length("abcd"), " ", CORE::join("-", 1, 2), "\n";
}
# sort $subref LIST: スカラーに入れた比較関数
{
    my $by_num = sub { $a <=> $b }; my $rev = sub { $b cmp $a }; my %sh = (b => 2, a => 1, c => 3); my @sl = (10, 9, 100, 1);
    print "sort subref: ", join(",", sort $by_num @sl), " | ", join(",", sort $rev (keys %sh)), " | ", join(",", sort $by_num (3, 1, 2)), " | ", join(",", sort $rev keys %sh), "\n";
    my $keysort; print "sort subref undef: ", join(",", defined $keysort ? (sort $keysort (keys %sh)) : sort keys %sh), "\n";
    my ($sx, $sy) = ("b", "a"); my @sw = ("q", "p");
    print "sort scalars: ", join(",", sort $sx, $sy), " | ", join(",", sort($sx, $sy)), " | ", join(",", sort $sx, @sw), " | ", join(",", sort $sx x 2, "a"), " | ", join(",", sort $sx eq "b" ? @sw : ()), "\n";
}
# サブの属性 (: lvalue など) は読み飛ばす
{
    sub attr_f : lvalue { 5 } sub attr_g :method { $_[0] + $_[1] } my $attr_h = sub : lvalue { 7 };
    print "attributes: ", attr_f(), attr_g(1, 2), $attr_h->(), "\n";
}
# defined(name()) はサブを呼んで戻り値を調べる。defined &name は定義の有無
{
    my $dcalls = 0;
    sub def_u { $dcalls++; undef } sub def_d { $dcalls++; 1 } sub def_none { $dcalls++; return }
    print "defined call: ", join("", map { $_ ? 1 : 0 } defined(def_u()), defined(def_d()), defined &def_u, defined &def_nosuch, defined(def_none()), defined def_u(), defined(&def_d)), " calls $dcalls\n";
    my ($dtext, $dat) = ("abc", 0); my $dch;
    sub def_next { return $dch = undef if $dat >= 3; $dch = substr($dtext, $dat++, 1) }
    my $dn = 0; while (defined(def_next())) { $dn++ } print "defined in while: $dn\n";
}
# my sub (レキシカルサブ)、local our (...)、my Class $obj
{
    use feature qw(lexical_subs signatures); no warnings;
my sub ls_add ($x, $y) { $x + $y } my sub ls_twice { 2 * shift } print ls_add(2, 3), " ", ls_twice(4), "\n";
{ package LS; my sub helper { "helper:@_" } sub pub { helper(shift) } } print LS::pub("x"), "\n";
    package VdFoo; sub new { bless { v => 5 }, shift } package main;
    my VdFoo $vobj = VdFoo->new; my VdFoo ($vo1, $vo2) = (VdFoo->new, 2); print "typed my: $vobj->{v} $vo1->{v} $vo2\n";
    our ($vtopdir, $vtopdev) = ("orig", 1);
    sub vshow { "$vtopdir/$vtopdev" }
    sub vinner { local our ($vtopdir, $vtopdev) = ("local", 2); vshow() }
    print "local our: ", vinner(), " ", vshow(), "\n";
    { local our $vsingle = "s"; local our @varr = (1, 2); print "local our single: $vsingle @varr $main::vsingle\n"; } print "after: ", (defined $main::vsingle ? "still" : "restored"), " ", scalar(@main::varr), "\n";
}
# 名前のない引数を含むシグネチャ ($, @rest)
{
    use feature "signatures"; no warnings;
sub sg_imp ($, @ops) { "ops=@ops" } sub sg_mid ($a, $, $c) { "$a$c" } sub sg_last ($x, $) { "x=$x" } sub sg_two ($, $) { "two" }
print sg_imp("Class", 1, 2), " ", sg_mid(1, 2, 3), " ", sg_last(7, 8), " ", sg_two(1, 2), "\n";
print eval { sg_mid(1, 2); 1 } ? "no error" : "too few: " . ($@ =~ /Too few arguments/ ? "ok" : $@), "\n";
print eval { sg_last(1, 2, 3); 1 } ? "no error" : "too many: " . ($@ =~ /Too many arguments/ ? "ok" : $@), "\n";
}
