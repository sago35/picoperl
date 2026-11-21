package List::Util;
# List::Util の picoperl 版 (本家は XS なので読み込めない)。すべて perl で書いてある。
#
#   use List::Util qw(sum max min first shuffle uniq reduce);
#   my $total = sum(@values);
#   my $big   = first { $_ > 10 } @values;
#   my $prod  = reduce { $a * $b } @values;
#
# 本家との違い: uniqnum は数値として等しいものを 1 つにまとめるだけ (NaN や巨大な整数の特別扱いは無い)。
# pairmap / pairgrep のブロックの中で $a / $b を書き換えても、元のリストには反映されない (本家は別名)。
use strict;
use Exporter 'import';
our $VERSION = '1.63';
our @EXPORT_OK = qw(sum sum0 product min max minstr maxstr
                    first any all none notall reduce reductions
                    shuffle sample uniq uniqnum uniqint uniqstr
                    pairs unpairs pairkeys pairvalues pairmap pairgrep pairfirst
                    head tail zip zip_longest zip_shortest mesh mesh_longest mesh_shortest);

sub sum     { return undef unless @_; my $s = 0; $s += $_ for @_; $s }
sub sum0    { my $s = 0; $s += $_ for @_; $s }
sub product { my $p = 1; $p *= $_ for @_; $p }
sub min     { return undef unless @_; my $m = shift; for (@_) { $m = $_ if $_ < $m } $m }
sub max     { return undef unless @_; my $m = shift; for (@_) { $m = $_ if $_ > $m } $m }
sub minstr  { return undef unless @_; my $m = shift; for (@_) { $m = $_ if $_ lt $m } $m }
sub maxstr  { return undef unless @_; my $m = shift; for (@_) { $m = $_ if $_ gt $m } $m }

# ブロックは $_ を見る
sub first(&@)  { my $f = shift; for (@_) { return $_ if $f->() } undef }
sub any(&@)    { my $f = shift; for (@_) { return 1 if $f->() } '' }
sub all(&@)    { my $f = shift; for (@_) { return '' unless $f->() } 1 }
sub none(&@)   { my $f = shift; for (@_) { return '' if $f->() } 1 }
sub notall(&@) { my $f = shift; for (@_) { return 1 unless $f->() } '' }

# ブロックが見る $a / $b を設定する。呼び出し元が our ($a, $b) としているパッケージにも入れる
sub _set_ab {
    my ($pkg, $x, $y) = @_;
    no strict 'refs';
    ${"main::a"} = $x; ${"main::b"} = $y;
    if ($pkg ne 'main') { ${"${pkg}::a"} = $x; ${"${pkg}::b"} = $y }
}

sub reduce(&@) {
    my $f = shift;
    return undef unless @_;
    my $pkg = caller;
    my $acc = shift;
    for my $x (@_) { _set_ab($pkg, $acc, $x); $acc = $f->() }
    return $acc;
}
sub reductions(&@) {
    my $f = shift;
    return () unless @_;
    my $pkg = caller;
    my $acc = shift;
    my @out = ($acc);
    for my $x (@_) { _set_ab($pkg, $acc, $x); $acc = $f->(); push @out, $acc }
    return @out;
}

# 乱数源は $List::Util::RAND (0 以上 1 未満を返すサブ) で差し替えられる。手順は本家と同じ
our $RAND;
sub shuffle {
    my @l = @_;
    my $r = $RAND;
    for (my $i = $#l; $i > 0; $i--) {
        my $j = int($r ? $r->() * ($i + 1) : rand($i + 1));
        @l[$i, $j] = @l[$j, $i];
    }
    return @l;
}
# sample($n, LIST): LIST から重複なしに $n 個 (LIST より多ければ全部) を選ぶ
sub sample($@) {
    my $n = shift;
    my @l = @_;
    unshift @l, pop @l if @l;          # 本家と同じ並びから始める (同じ乱数列なら同じ結果になる)
    my $r = $RAND;
    $n = @l if $n > @l;
    for (my $i = 0; $i < $n; $i++) {
        my $j = $i + int($r ? $r->() * (@l - $i) : rand(@l - $i));
        @l[$i, $j] = @l[$j, $i];
    }
    return @l[0 .. $n - 1];
}

sub uniq    { my %s; my $u = 0; my @r = grep { defined $_ ? !$s{$_}++ : !$u++ } @_; wantarray ? @r : scalar @r }
sub uniqstr { my %s; my @r = grep { !$s{$_}++ } map { defined $_ ? "$_" : '' } @_; wantarray ? @r : scalar @r }   # undef は '' として返す
sub uniqnum { my %s; my @r = grep { !$s{ (defined $_ ? $_ : 0) + 0 }++ } @_; wantarray ? @r : scalar @r }
sub uniqint { my %s; my @r = grep { !$s{$_}++ } map { int(defined $_ ? $_ : 0) } @_; wantarray ? @r : scalar @r }

sub pairs {
    my @r;
    for (my $i = 0; $i < @_; $i += 2) { push @r, bless [ $_[$i], $_[$i + 1] ], 'List::Util::_Pair' }
    return @r;
}
sub unpairs    { map { ($_->[0], $_->[1]) } @_ }
sub pairkeys   { my @r; for (my $i = 0; $i < @_; $i += 2) { push @r, $_[$i] } @r }
sub pairvalues { my @r; for (my $i = 0; $i < @_; $i += 2) { push @r, $_[$i + 1] } @r }
sub pairmap(&@) {
    my $f = shift;
    my $pkg = caller;
    my @r;
    for (my $i = 0; $i < @_; $i += 2) { _set_ab($pkg, $_[$i], $_[$i + 1]); push @r, $f->() }
    return wantarray ? @r : scalar @r;
}
sub pairgrep(&@) {
    my $f = shift;
    my $pkg = caller;
    my @r;
    for (my $i = 0; $i < @_; $i += 2) { _set_ab($pkg, $_[$i], $_[$i + 1]); push @r, $_[$i], $_[$i + 1] if $f->() }
    return wantarray ? @r : @r / 2;
}
sub pairfirst(&@) {
    my $f = shift;
    my $pkg = caller;
    for (my $i = 0; $i < @_; $i += 2) {
        _set_ab($pkg, $_[$i], $_[$i + 1]);
        return wantarray ? ($_[$i], $_[$i + 1]) : 1 if $f->();
    }
    return wantarray ? () : '';
}

sub head($@) {
    my $n = shift;
    my $end = $n >= 0 ? $n - 1 : $#_ + $n;
    $end = $#_ if $end > $#_;
    return $end < 0 ? () : @_[0 .. $end];
}
sub tail($@) {
    my $n = shift;
    my $start = $n >= 0 ? @_ - $n : -$n;
    $start = 0 if $start < 0;
    return $start > $#_ ? () : @_[$start .. $#_];
}

# zip / mesh: 配列リファレンスを横に並べる。$short が真なら最短、偽なら最長 (足りない分は undef)
sub _rows {
    my ($short, @lists) = @_;
    return () unless @lists;
    my $n = scalar @{ $lists[0] };
    for my $l (@lists) { my $c = scalar @$l; $n = $c if $short ? $c < $n : $c > $n }
    return map { my $i = $_; [ map { $_->[$i] } @lists ] } 0 .. $n - 1;
}
sub zip           { _rows(0, @_) }
sub zip_longest   { _rows(0, @_) }
sub zip_shortest  { _rows(1, @_) }
sub mesh          { map { @$_ } _rows(0, @_) }
sub mesh_longest  { map { @$_ } _rows(0, @_) }
sub mesh_shortest { map { @$_ } _rows(1, @_) }

package List::Util::_Pair;
sub key   { $_[0][0] }
sub value { $_[0][1] }
sub TO_JSON { [ @{ $_[0] } ] }

1;
