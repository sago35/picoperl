use strict; no warnings;
# 組み込み関数と同じ名前のサブ: 自分のパッケージで定義したものは組み込みを上書きしない
{
    package OwnNames;
    sub new { bless { log => [] }, shift }
    sub close { my $self = shift; push @{ $self->{log} }, "close"; 1 }
    sub push_log { my $self = shift; push @{ $self->{log} }, @_; scalar @{ $self->{log} } }
    sub open { my ($self, $path) = @_; open(my $fh, '<', $path) or return; my $l = <$fh>; close($fh); $self->{first} = $l; 1 }
    sub length { length($_[0]{first}) }
    sub join { my $self = shift; join("+", @{ $self->{log} }) }
    sub fileno { my $self = shift; defined fileno(STDOUT) ? "has fileno" : "none" }
    package main;
    my $o = OwnNames->new; $o->open($0); $o->close; $o->push_log("x");
    print $o->length > 5 ? "long" : "short", " ", $o->join, " ", $o->fileno, "\n";
}
# local(*FH) と書式用の特殊変数
{
    sub with_glob { local(*TMPFH); open(TMPFH, '<', $0) or die; my $l = <TMPFH>; close(TMPFH); length($l) > 3 ? "read" : "empty" }
    print with_glob(), "\n";
    my $old = $=; $= = 42; my $n = $=; $= = $old; $~ = "MYFMT"; print "$n ", $~, "\n";
}
