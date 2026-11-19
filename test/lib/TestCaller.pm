package TestCaller;
# require されたファイルの先頭での caller: 読み込んだ側のパッケージ、ファイル、行
my @c = caller;
our $top = join("|", $c[0], ($c[1] =~ /t_modules\.pl$/ ? "script" : $c[1]), ($c[2] > 0 ? "line" : "no line"));
our $modulino = caller() ? "loaded as a module" : "run as a script";
my @c0 = caller(0); our $sub0 = $c0[3];
1;
