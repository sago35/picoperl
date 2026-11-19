# Data::Dumper の実機確認
#
# Data::Dumper は lib/ に入っていないので、perl 本体の Data/Dumper.pm (POD を除いたもの) を
# lib/Data/Dumper.pm として埋め込んだ uf2 で動かします (dist/picoperl-dumper-demo.uf2)。
# シリアルで Enter を送るたびに、いくつかのデータを Dumper で表示します。
# 読み込みに RAM を多く使うので、前後の空きヒープも表示します。
use strict;
use warnings;
use Board;

print "heap free before loading Data::Dumper: ", Board::heap_free(), "\n";
require Data::Dumper;
Data::Dumper->import;
$Data::Dumper::Sortkeys = 1;
print "heap free after loading:               ", Board::heap_free(), "\n";

sub run {
    my $obj = bless { name => "pico", pins => [26, 27] }, 'My::Device';
    my @cyc = (1); push @cyc, \@cyc;
    print "-- nested --\n",   Data::Dumper::Dumper({ list => [1, "two", undef], hash => { x => \"s" }, num => 1.5, str => "it's" });
    print "-- object --\n",   Data::Dumper::Dumper($obj);
    print "-- cyclic --\n",   Data::Dumper::Dumper(\@cyc);
    { local $Data::Dumper::Indent = 1; local $Data::Dumper::Terse = 1; print "-- Indent=1, Terse --\n", Data::Dumper::Dumper([1, { k => "v" }]); }
    { local $Data::Dumper::Indent = 0; print "-- Indent=0 --\n", Data::Dumper::Dumper({ b => 2, a => 1 }), "\n"; }
    { local $Data::Dumper::Useqq = 1; print "-- Useqq --\n", Data::Dumper::Dumper("tab\there\n"); }
    print "-- named --\n",    Data::Dumper->Dump([[1, 2], { k => 1 }], [qw(list *hash)]);
    print "heap free now: ", Board::heap_free(), "\n-- done --\n\n";
}

print "dumper demo: press Enter to dump\n";
while (defined(my $l = <STDIN>)) { run() }
