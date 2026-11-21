use strict; use warnings;
my $file = "test/io_tmp_smallfix2.txt";
# select(FH): ハンドルなしの print / printf / say の出力先が変わる。戻り値は前の出力先
{ open(my $out, ">", $file) or die; my $old = select $out; print "A\n"; printf "%s\n", "A2"; my $mid = select $old; print "back to $old\n"; close $out;
  open(my $in, "<", $file) or die; my @l = <$in>; close $in; chomp @l; print "select \$fh: [@l]\n"; }
{ open(FOO, ">", $file) or die; my $old = select(FOO); print "D\n"; my $cur = select; select(STDOUT); close FOO;
  open(IN, "<", $file) or die; my $l = <IN>; close IN; chomp $l; print "select(FOO): [$l] $old $cur ", select(), "\n"; }
{ open(my $out, ">", $file) or die; my $old = select *$out; print "G\n"; select $old; close $out; open(IN, "<", $file) or die; my $l = <IN>; close IN; print "select *\$fh: $l"; }
# プロトタイプ ($$;$) の引数に書いた eval { } / do { } / ( ) は 1 つの値になる (失敗しても引数は減らない)
{ no warnings; sub is2 ($$;$) { scalar(@_) . "[" . (defined $_[0] ? $_[0] : "undef") . "]" } sub lst { return } my @e; my @f = (7, 8, 9);
  print "proto: ", join(" ", is2(eval { die "x\n" }, undef, "name"), is2(eval { (1, 2) }, 3), is2(do { (4, 5) }, 1), is2(eval q{ (6, 7) }, 1), is2((@e ? @e : lst()), 1), is2((@f), 1)), "\n"; }
# local *_ = \$x: 暗黙の $_ (s///、tr、length) もその変数になる。パッケージの中の *_ も main のもの
{ package SfPk; sub o { local *_ = \join("", @_) if @_; s/a/X/; return "[$_]"; }
  package main; $_ = "main"; print "glob _: ", SfPk::o("a b"), " $_\n";
  my $x = "abc"; { local *_ = \$x; tr/a-c/A-C/; print "alias: ", length, " $_\n"; } print "after: $x $_\n"; }
# 文の先頭の {: } がすぐ来るか、単語・文字列のあとに , か => が来れば無名ハッシュ
{ no warnings; my @r = map { eval $_ } ("[]", "{}", "{ a => 1 }", q{{ "k", 2 }}, "{ 1 => 2 }", "{ a => { b => [1] } }"); print "brace: ", join(",", map { ref } @r), "\n";
  print "block: ", (defined(eval "{ ; }") ? "def" : "undef"), " ", (eval "{ 5 }"), " ", (eval "{ my \$v = 3; \$v + 1 }"), "\n";
  my $n = 0; { $n++ } { ; } { local $_ = 5; $n += $_; } print "bare blocks: $n\n"; }
unlink $file;
