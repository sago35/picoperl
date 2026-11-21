package Exporter;
# Exporter の picoperl 版。
#
# `use Module;` のエクスポートはインタプリタが直接行う (@EXPORT / @EXPORT_OK を見る) ので、
# 普段このファイルは読み込まれない。次の書き方のために置いてある:
#   require Exporter; our @ISA = ('Exporter');     # 昔ながらの継承
#   Module->import(qw(name));                      # import を自分で呼ぶ
#   Module->export_to_level(1, @names);
#   Exporter::export($pkg, $into, @names);
use strict;
our $VERSION = '5.79';
our $ExportLevel = 0;

sub import {
    my $pkg = shift;
    my $into = caller($ExportLevel);
    if ($pkg eq 'Exporter') {                       # use Exporter 'import';
        no strict 'refs';
        *{"${into}::import"} = \&import if grep { $_ eq 'import' } @_;
        return;
    }
    _export($pkg, $into, @_);
}
sub export_to_level {
    my ($pkg, $level, $ignored, @names) = @_;
    _export($pkg, scalar caller($level), @names);
}
# Exporter::export($pkg, $into, @names): 取り込み先のパッケージを直接指定する形 (FileHandle が使う)
sub export { _export(@_) }

sub _export {
    my ($pkg, $into, @names) = @_;
    no strict 'refs';
    my @default = @{"${pkg}::EXPORT"};
    my %tags    = %{"${pkg}::EXPORT_TAGS"};
    my %known   = map { $_ => 1 } @default, @{"${pkg}::EXPORT_OK"}, map { @$_ } values %tags;
    @names = @default unless @names;
    my @todo;
    for my $n (@names) {
        if ($n =~ /^:(.+)/) {                       # :tag / :DEFAULT
            if ($1 eq 'DEFAULT') { push @todo, @default }
            elsif ($tags{$1})    { push @todo, @{ $tags{$1} } }
            else { die qq("$1" is not defined in \%${pkg}::EXPORT_TAGS\n) }
        }
        else { push @todo, $n }
    }
    for my $n (@todo) {
        (my $plain = $n) =~ s/^&//;
        die qq("$n" is not exported by the $pkg module\n) unless $known{$n} || $known{$plain} || $known{"&$plain"};
        if    ($n =~ /^\$(.+)/) { *{"${into}::$1"} = \${"${pkg}::$1"} }
        elsif ($n =~ /^\@(.+)/) { *{"${into}::$1"} = \@{"${pkg}::$1"} }
        elsif ($n =~ /^%(.+)/)  { *{"${into}::$1"} = \%{"${pkg}::$1"} }
        elsif (defined &{"${pkg}::$plain"}) { *{"${into}::$plain"} = \&{"${pkg}::$plain"} }
        # 名前だけ @EXPORT にあって定義のないサブ (Test::More の todo など) は、本家と同じく黙って飛ばす
    }
}

# @EXPORT_OK / @EXPORT にタグの中身を足す
sub export_ok_tags { my $pkg = caller; _add_tags($pkg, 'EXPORT_OK', @_) }
sub export_tags    { my $pkg = caller; _add_tags($pkg, 'EXPORT', @_) }
sub _add_tags {
    my ($pkg, $list, @tags) = @_;
    no strict 'refs';
    my %tags = %{"${pkg}::EXPORT_TAGS"};
    push @{"${pkg}::$list"}, map { @{ $tags{$_} || [] } } (@tags ? @tags : sort keys %tags);
}
sub require_version { my ($pkg, $want) = @_; no strict 'refs'; return ${"${pkg}::VERSION"} }

1;
