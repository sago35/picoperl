package FindBin;
# FindBin の picoperl 版: 実行中のスクリプトがあるディレクトリ ($Bin) とファイル名 ($Script)。
# シンボリックリンクはたどらないので、$RealBin / $RealScript は $Bin / $Script と同じです。
use strict;
use Cwd ();
our $VERSION = '1.54';
our ($Bin, $Script, $RealBin, $RealScript, $Dir, $RealDir);
our @EXPORT_OK = qw($Bin $Script $RealBin $RealScript $Dir $RealDir);
our %EXPORT_TAGS = (ALL => [@EXPORT_OK]);

sub init {
    my $path = $0;
    my ($dir, $name) = $path =~ m{^(.*)/([^/]*)$} ? ($1, $2) : ('.', $path);
    $dir = '/' if $dir eq '';
    ($dir, $name) = ('.', $path) if $path eq '-e' || $path eq '-';
    my $abs = Cwd::abs_path($dir);
    $abs = $dir unless defined $abs && length $abs;
    $Bin = $RealBin = $Dir = $RealDir = $abs;
    $Script = $RealScript = $name;
}
*again = \&init;
init();
1;
