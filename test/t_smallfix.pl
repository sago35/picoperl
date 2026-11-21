use strict; use warnings;
# $obj->isa('ARRAY'): bless されていても、中身の型の名前には真を返す
{ my $o = bless [], "SfFoo"; my $h = bless {}, "SfBar";
  print "isa: ", join(",", map { $_ ? 1 : 0 } $o->isa("ARRAY"), $o->isa("HASH"), $h->isa("HASH"), UNIVERSAL::isa([], "ARRAY"), UNIVERSAL::isa($o, "SfFoo"), UNIVERSAL::isa("SfFoo", "ARRAY"), UNIVERSAL::isa($o, "ARRAY"), SfFoo->isa("ARRAY")), "\n"; }
# binmode: 開いていないハンドルには undef
{ no warnings; my $r = binmode(SF_NOSUCH); print "binmode: ", (defined $r ? "def" : "undef"), " ", (binmode(STDOUT) ? "ok" : "no"), (binmode(STDIN) ? "ok" : "no"), "\n";
  open(my $fh, "<", $0) or die; print "  open: ", (binmode($fh) ? 1 : 0); close $fh; my $r2 = binmode($fh); print " closed: ", (defined $r2 ? "def" : "undef"), "\n"; }
# version オブジェクトは計算できない
{ require version; my $v = version->new("1.2"); my $res = "";
  for my $c (q{$v + 1}, q{$v - 1}, q{$v * 2}, q{$v / 2}, q{abs($v)}, q{$v += 1}, q{$v *= 1}) { eval $c; $res .= $@ =~ /operation not supported with version object/ ? "n" : "?"; }
  print "version math: $res ", ($v > version->new("1.1") ? "cmp ok" : "cmp bad"), " $v\n"; }
# warnings::enabled / warnif は、use warnings の下で使える
{ print "warnings::enabled: ", (warnings::enabled("deprecated") ? "on" : "off"), "\n";
  my $w = ""; local $SIG{__WARN__} = sub { $w .= shift }; warnings::warnif("deprecated", "old thing"); print "warnif: [", $w =~ s/ at .*//sr, "]\n"; }
