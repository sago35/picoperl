package mro;
# mro の picoperl 版 (本家は XS)。picoperl のメソッド探索は常に深さ優先 (dfs) なので、
# `use mro 'c3'` は受け付けるだけで探索順は変わらない。next::method は無い。
use strict;
our $VERSION = '1.29';

sub import { 1 }

# クラスとその親を、メソッドを探す順 (深さ優先、重複は最初の 1 回だけ) に並べた配列リファレンス
sub get_linear_isa {
    my ($class) = @_;
    my (@out, %seen);
    my @todo = ($class);
    while (@todo) {
        my $c = shift @todo;
        next if $seen{$c}++;
        push @out, $c;
        no strict 'refs';
        unshift @todo, @{"${c}::ISA"};
    }
    return \@out;
}
sub get_mro { 'dfs' }
sub set_mro { return }
sub is_universal { $_[0] eq 'UNIVERSAL' }
sub get_pkg_gen { 1 }
sub method_changed_in { return }
sub invalidate_all_method_caches { return }

1;
