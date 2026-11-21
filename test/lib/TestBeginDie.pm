package TestBeginDie;
# BEGIN ブロックの中で die するモジュール (t_requireerr.pl 用)
BEGIN { die "died in BEGIN\n" }
1;
