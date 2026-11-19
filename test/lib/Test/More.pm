package Test::More;
# picoperl 用の小さな Test::More。
#
# 本家 (Test::Builder / Test2) は依存が深くて picoperl では読み込めないので、
# よく使う関数だけを、出力 (TAP と失敗時の診断) が本家と同じになるように書き直したもの。
#
#   use Test::More;                   # 最後に done_testing
#   use Test::More tests => 3;
#   use Test::More skip_all => "理由";
#
#   ok is isnt like unlike cmp_ok is_deeply pass fail
#   can_ok isa_ok new_ok use_ok require_ok
#   diag note explain plan done_testing skip todo_skip subtest BAIL_OUT  / $TODO
#
# 本家との違い: use_ok は @EXPORT の名前だけを取り込む。explain は簡易表示。
# Test::Builder のオブジェクト (Test::More->builder) は無い。
use strict;

our $VERSION = '0.01';
our $TODO;

my @EXPORT = qw(ok is isnt like unlike cmp_ok is_deeply pass fail
                can_ok isa_ok new_ok use_ok require_ok
                diag note explain plan done_testing skip todo_skip subtest BAIL_OUT);

my $num     = 0;        # 実行したテストの数
my $failed  = 0;        # 失敗した数 (TODO は数えない)
my $planned;            # 宣言したテスト数 (undef = 未宣言)
my $done    = 0;        # done_testing を呼んだ
my $no_plan = 0;
my $indent  = "";       # subtest の中では 4 桁ずつ下げる
my $bailed  = 0;

sub import {
    my ($class, @args) = @_;
    my $pkg = caller;
    no strict 'refs';
    *{"${pkg}::$_"} = \&{$_} for @EXPORT;
    *{"${pkg}::TODO"} = \$TODO;
    plan(@args) if @args;
}

sub _out { print STDOUT map { "$indent$_\n" } map { split /\n/, $_, -1 } @_ }
sub _err { print STDERR map { "$indent$_\n" } map { split /\n/, $_, -1 } @_ }

# メッセージの各行の頭に "# " を付けて出す。TODO 中の失敗の診断は標準出力へ
sub _diag {
    my ($to_stdout, @msg) = @_;
    my $text = join "", map { defined $_ ? $_ : "undef" } @msg;
    return unless length $text;
    $text =~ s/\n$//;
    my @lines = map { "# $_" } split /\n/, $text, -1;
    $to_stdout ? _out(@lines) : _err(@lines);
}
sub diag { _diag(0, @_); return 0 }
sub note { _diag(1, @_); return 0 }

sub plan {
    my ($kind, $arg) = @_;
    if ($kind eq 'tests') {
        die "Number of tests must be a positive integer.  You gave it '" . (defined $arg ? $arg : '') . "'\n"
            unless defined $arg && $arg =~ /^\d+$/ && $arg > 0;
        $planned = $arg;
        _out("1..$arg");
    }
    elsif ($kind eq 'skip_all') {
        _out("1..0 # SKIP" . (defined $arg && length $arg ? " $arg" : ""));
        $planned = 0; $done = 1;
        exit 0;
    }
    elsif ($kind eq 'no_plan') { $no_plan = 1 }
    else { die "plan() doesn't understand $kind" . (defined $arg ? " $arg" : "") . "\n" }
    return 1;
}

sub done_testing {
    my ($n) = @_;
    unless (defined $planned) {      # 計画を宣言済みなら 1..N はもう出ている
        $planned = defined $n ? $n : $num;
        _out("1..$planned");
    }
    $done = 1;
    _summary();
    return 1;
}

my $summarized = 0;
sub _summary {
    return if $summarized++;
    if (defined $planned && $planned != $num) {
        my $s = $planned == 1 ? "" : "s";
        _err("# Looks like you planned $planned test$s but ran $num.");
    }
    if ($failed) {
        my $s = $failed == 1 ? "" : "s";
        _err("# Looks like you failed $failed test$s of $num.");
    }
}

# テストを呼んだ場所 (Test::More の外で最初のフレーム)
sub _where {
    my $i = 0;
    my @c;
    while (@c = caller($i)) { last if $c[0] ne 'Test::More'; $i++ }
    return @c ? ($c[1], $c[2]) : ("-", 0);
}

# 1 件分の結果を出す。@diag は失敗時に続けて出す診断
sub _ok {
    my ($pass, $name, @diag) = @_;
    $num++;
    my $todo = defined $TODO && length $TODO;
    my $line = ($pass ? "ok" : "not ok") . " $num";
    if (defined $name && length $name) {
        (my $n = $name) =~ s/#/\\#/g;
        $line .= " - $n";
    }
    $line .= " # TODO $TODO" if $todo;
    _out($line);
    unless ($pass) {
        $failed++ unless $todo;
        my ($file, $ln) = _where();
        my $what = $todo ? "Failed (TODO) test" : "Failed test";
        if (defined $name && length $name) { _diag($todo, "  $what '$name'\n  at $file line $ln.") }
        else                               { _diag($todo, "  $what at $file line $ln.") }
        _diag($todo, $_) for @diag;
    }
    return $pass ? 1 : 0;
}

sub ok   { my ($test, $name) = @_; _ok($test ? 1 : 0, $name) }
sub pass { _ok(1, $_[0]) }
sub fail { _ok(0, $_[0]) }

sub _q { defined $_[0] ? "'$_[0]'" : "undef" }

sub is {
    my ($got, $exp, $name) = @_;
    my $same = defined $got && defined $exp ? $got eq $exp : !defined $got && !defined $exp;
    _ok($same, $name, sprintf("         got: %s\n    expected: %s", _q($got), _q($exp)));
}
sub isnt {
    my ($got, $exp, $name) = @_;
    my $same = defined $got && defined $exp ? $got eq $exp : !defined $got && !defined $exp;
    _ok(!$same, $name, sprintf("         got: %s\n    expected: anything else", _q($got)));
}

sub like {
    my ($got, $re, $name) = @_;
    my $m = defined $got && $got =~ $re;
    _ok($m, $name, sprintf("                  %s\n    doesn't match '%s'", _q($got), $re));
}
sub unlike {
    my ($got, $re, $name) = @_;
    my $m = defined $got && $got =~ $re;
    _ok(!$m, $name, sprintf("                  %s\n          matches '%s'", _q($got), $re));
}

my %CMP = (
    '==' => sub { $_[0] == $_[1] }, '!=' => sub { $_[0] != $_[1] },
    '<'  => sub { $_[0] <  $_[1] }, '<=' => sub { $_[0] <= $_[1] },
    '>'  => sub { $_[0] >  $_[1] }, '>=' => sub { $_[0] >= $_[1] },
    'eq' => sub { $_[0] eq $_[1] }, 'ne' => sub { $_[0] ne $_[1] },
    'lt' => sub { $_[0] lt $_[1] }, 'le' => sub { $_[0] le $_[1] },
    'gt' => sub { $_[0] gt $_[1] }, 'ge' => sub { $_[0] ge $_[1] },
    '=~' => sub { $_[0] =~ $_[1] }, '!~' => sub { $_[0] !~ $_[1] },
    '&&' => sub { $_[0] && $_[1] }, '||' => sub { $_[0] || $_[1] },
    'and' => sub { $_[0] && $_[1] }, 'or' => sub { $_[0] || $_[1] },
    '<=>' => sub { $_[0] <=> $_[1] }, 'cmp' => sub { $_[0] cmp $_[1] },
);
sub cmp_ok {
    my ($got, $op, $exp, $name) = @_;
    my $f = $CMP{$op};
    return _ok(0, $name, "    unsupported operator '$op' in cmp_ok (picoperl Test::More)") unless $f;
    my $r;
    { no warnings; $r = $f->($got, $exp) }
    my $diag;
    if    ($op eq '==') { $diag = sprintf("         got: %s\n    expected: %s", map { defined $_ ? $_ : "undef" } $got, $exp) }
    elsif ($op eq 'eq') { $diag = sprintf("         got: %s\n    expected: %s", _q($got), _q($exp)) }
    elsif ($op eq '!=') { $diag = sprintf("         got: %s\n    expected: anything else", defined $got ? $got : "undef") }
    elsif ($op eq 'ne') { $diag = sprintf("         got: %s\n    expected: anything else", _q($got)) }
    else                { $diag = sprintf("    %s\n        %s\n    %s", _q($got), $op, _q($exp)) }
    _ok($r, $name, $diag);
}

# --- is_deeply ---
# 最初に違いが見つかった場所を ("->[1]{k}" のような経路, got, expected) で返す。同じなら空リスト
my $MISSING = \"Does not exist";
sub _deep {
    my ($g, $e, $path) = @_;
    return () if !defined $g && !defined $e;
    return ($path, $g, $e) if !defined $g || !defined $e;
    my ($rg, $re) = (ref $g, ref $e);
    return ($path, $g, $e) if $rg ne $re;
    return $g eq $e ? () : ($path, $g, $e) unless $rg;
    my $kind = builtin::reftype($g);
    return ($path, $g, $e) if $kind ne builtin::reftype($e);
    return () if builtin::refaddr($g) == builtin::refaddr($e);
    if ($kind eq 'ARRAY') {
        my $max = @$g > @$e ? scalar @$g : scalar @$e;
        for my $i (0 .. $max - 1) {
            my @d = _deep($i < @$g ? $g->[$i] : $MISSING, $i < @$e ? $e->[$i] : $MISSING, $path . "[$i]");
            return @d if @d;
        }
        return ();
    }
    if ($kind eq 'HASH') {
        my %seen;
        for my $k (sort grep { !$seen{$_}++ } keys %$g, keys %$e) {
            my @d = _deep(exists $g->{$k} ? $g->{$k} : $MISSING, exists $e->{$k} ? $e->{$k} : $MISSING, $path . "{$k}");
            return @d if @d;
        }
        return ();
    }
    if ($kind eq 'SCALAR' || $kind eq 'REF') {
        return ($path, $g, $e) if $g == $MISSING || $e == $MISSING;
        return _deep($$g, $$e, "\${$path}");
    }
    return ($path, $g, $e);     # CODE など: 同じ参照でなければ違う
}
sub _dv {
    my ($v) = @_;
    return "Does not exist" if ref $v && $v == $MISSING;
    return defined $v ? (ref $v ? "$v" : "'$v'") : "undef";
}
sub is_deeply {
    my ($got, $exp, $name) = @_;
    if (!ref $got && !ref $exp) { return is($got, $exp, $name) }
    my @d = _deep($got, $exp, "");
    return _ok(1, $name) unless @d;
    my ($path, $g, $e) = @d;
    my $ga = $path eq "" ? "     \$got" : "     \$got->$path";
    my $ea = $path eq "" ? "\$expected" : "\$expected->$path";
    _ok(0, $name, sprintf("    Structures begin differing at:\n    %s = %s\n    %s = %s", $ga, _dv($g), $ea, _dv($e)));
}

sub can_ok {
    my ($proto, @methods) = @_;
    my $class = ref $proto || $proto;
    my @missing = grep { !$proto->can($_) } @methods;
    my $name = @methods == 1 ? "$class->can('$methods[0]')" : "$class->can(...)";
    _ok(!@missing, $name, map { "    $class->can('$_') failed" } @missing);
}

sub isa_ok {
    my ($thing, $class, $what) = @_;
    my ($ok, $name);
    if (!defined $thing) {
        $what = "undef" unless defined $what;
        $name = "$what isa '$class'"; $ok = 0;
    }
    elsif (builtin::blessed($thing)) {
        $what = "An object of class '" . ref($thing) . "'" unless defined $what;
        $name = "$what isa '$class'"; $ok = $thing->isa($class);
    }
    elsif (ref $thing) {
        $what = "A reference of type '" . ref($thing) . "'" unless defined $what;
        $name = "$what isa '$class'"; $ok = ref($thing) eq $class;
    }
    else {
        $what = "The class (or class-like) '$thing'" unless defined $what;
        $name = "$what isa '$class'"; $ok = $thing->isa($class);
    }
    _ok($ok, $name, $ok ? () : "    $what isn't a '$class'");
}

sub new_ok {
    my ($class, $args, $what) = @_;
    my $obj = eval { $class->new(@{ $args || [] }) };
    if ($@) { my $err = $@; _ok(0, "$class->new() died", "    Error was:  $err"); return }
    isa_ok($obj, $class, $what);
    return $obj;
}

sub _load {
    my ($module) = @_;
    (my $file = $module) =~ s{::}{/}g;
    return eval { require "$file.pm"; 1 };
}
sub require_ok {
    my ($module) = @_;
    my $ok = _load($module);
    my $err = $@;
    _ok($ok, "require $module;", $ok ? () : "    Tried to require '$module'.\n    Error:  $err");
}
sub use_ok {
    my ($module, @imports) = @_;
    my $pkg = caller;
    my $ok = _load($module);
    my $err = $@;
    if ($ok) {
        no strict 'refs';
        if ($module->can('import')) { eval { $module->import(@imports); 1 } or do { $ok = 0; $err = $@ } }
        else {      # picoperl の Exporter は use の中で働くので、ここでは @EXPORT を手で配る
            for my $n (@imports ? @imports : @{"${module}::EXPORT"}) {
                if    ($n =~ /^\$(.+)/) { *{"${pkg}::$1"} = \${"${module}::$1"} }
                elsif ($n =~ /^\@(.+)/) { *{"${pkg}::$1"} = \@{"${module}::$1"} }
                elsif ($n =~ /^%(.+)/)  { *{"${pkg}::$1"} = \%{"${module}::$1"} }
                else { (my $s = $n) =~ s/^&//; *{"${pkg}::$s"} = \&{"${module}::$s"} if defined &{"${module}::$s"} }
            }
        }
    }
    _ok($ok, "use $module;", $ok ? () : "    Tried to use '$module'.\n    Error:  $err");
}

sub skip {
    my ($why, $count) = @_;
    $count = 1 unless defined $count;
    for (1 .. $count) { $num++; _out("ok $num # skip" . (defined $why && length $why ? " $why" : "")) }
    no warnings;
    last SKIP;
}
sub todo_skip {
    my ($why, $count) = @_;
    $count = 1 unless defined $count;
    for (1 .. $count) { $num++; _out("not ok $num # TODO & SKIP" . (defined $why && length $why ? " $why" : "")) }
    no warnings;
    last TODO;
}

sub subtest {
    my ($name, $code, @args) = @_;
    _out("# Subtest: $name");
    my @saved = ($num, $failed, $planned, $done, $no_plan, $summarized, $indent);
    ($num, $failed, $planned, $done, $no_plan, $summarized) = (0, 0, undef, 0, 0, 0);
    $indent .= "    ";
    my $ok = eval { $code->(@args); 1 };
    my $err = $@;
    if ($ok && !$done) {
        if (defined $planned) { _summary() }
        elsif ($num) { $planned = $num; _out("1..$num"); _summary() }
        else { _out("1..0"); _err("# No tests run!") }
    }
    my $pass = $ok && $num > 0 && !$failed && (!defined $planned || $planned == $num);
    my $ran = $num;
    ($num, $failed, $planned, $done, $no_plan, $summarized, $indent) = @saved;
    die $err unless $ok;
    $ran ? _ok($pass, $name) : _ok(0, "No tests run for subtest \"$name\"");
}

sub explain {
    return map { ref $_ ? _dump($_, 0) . "\n" : $_ } @_;
}
sub _dump {
    my ($v, $depth) = @_;
    return "undef" unless defined $v;
    return "'$v'" unless ref $v;
    my $pad = "  " x ($depth + 1);
    my $end = "  " x $depth;
    my $kind = builtin::reftype($v);
    if ($kind eq 'ARRAY') { return @$v ? "[\n" . join(",\n", map { $pad . _dump($_, $depth + 1) } @$v) . "\n$end]" : "[]" }
    if ($kind eq 'HASH')  { return %$v ? "{\n" . join(",\n", map { "$pad'$_' => " . _dump($v->{$_}, $depth + 1) } sort keys %$v) . "\n$end}" : "{}" }
    if ($kind eq 'SCALAR' || $kind eq 'REF') { return "\\" . _dump($$v, $depth) }
    return "$v";
}

sub BAIL_OUT {
    my ($why) = @_;
    _out("Bail out!" . (defined $why && length $why ? "  $why" : ""));
    $bailed = 1;
    exit 255;
}

# 終了時: 計画との食い違いと失敗数を報告し、終了コードを本家と同じにする
END {
    if (!$bailed && ($num || defined $planned)) {
        if ($? != 0 && !$done) {
            _err("# Looks like your test exited with $? " . ($num ? "just after $num." : "before it could output anything."));
        }
        elsif (!defined $planned && !$done) {
            _err("# Tests were run but no plan was declared and done_testing() was not seen.");
            $? = 254;
        }
        else {
            _summary();
            if    ($failed)            { $? = $failed > 254 ? 254 : $failed }
            elsif ($planned != $num)   { $? = 255 }
        }
    }
}

1;
