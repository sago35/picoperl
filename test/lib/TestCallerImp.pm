package TestCallerImp;
# import が caller のパッケージに定数を作るモジュール (t_modules.pl 用)
sub import { my $into = caller; no strict 'refs'; *{"${into}::CALLER_MARK"} = sub () { "mark in $into" }; }
1;
