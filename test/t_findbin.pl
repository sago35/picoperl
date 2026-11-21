use strict; use warnings;
# FindBin: スクリプトのあるディレクトリとファイル名
use FindBin qw($Bin $Script $RealBin $RealScript);
print "bin: ", ($Bin =~ m{^/.*/test$} ? "ok" : "ng: $Bin"), "\n";
print "script: $Script $RealScript\n";
print "real: ", ($RealBin eq $Bin ? "same" : "diff"), " pkg: ", ($FindBin::Bin eq $Bin ? "same" : "diff"), "\n";
print "data: ", (-f "$Bin/t_io_data.txt" ? "found" : "missing"), "\n";
