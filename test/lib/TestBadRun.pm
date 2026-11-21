package TestBadRun;
# t_modules.pl: 実行中に die するモジュール (require の失敗の扱いを確かめる)
sub defined_before_die { 1 }
die "runtime failure in TestBadRun\n";
1;
