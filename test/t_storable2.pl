# Storable の freeze / thaw / store / retrieve: バイト列が本物と同じで、読み戻すと同じ形になる
use strict; use warnings;
use Storable qw(freeze nfreeze thaw store nstore retrieve dclone);
sub hx { join " ", map { sprintf "%02x", ord } split //, shift }
$Storable::canonical = 1;
my $o = bless { n => 1 }, "Cls";
my @c = ([1, "ab", undef, 300, -5, 1.5, 70000, 5000000000, -129, 3.0, "x" x 300], { a => 1, bb => [2], "" => undef }, \"str", [ $o, $o, bless([], "Cls"), bless([], "Other") ], \\7, bless([], "A"), bless(\(my $s = "sc"), "SC"), [[], {}]);
print "freeze  $_: ", hx(freeze($c[$_])), "\n" for 0 .. $#c;
print "nfreeze $_: ", hx(nfreeze($c[$_])), "\n" for 0 .. $#c;
my $cyc = [1]; push @$cyc, $cyc; print "cyc: ", hx(nfreeze($cyc)), "\n";
my $sh = [1,2]; print "shared: ", hx(nfreeze([$sh, $sh])), "\n";
# 読み戻し
sub show { my ($d, $seen) = (@_, {}); return defined $d ? "'$d'" : "undef" unless ref $d;
  my $cls = ref $d; my $t = "$d"; return "SEEN" if $seen->{$t}++; my $r;
  if ($t =~ /ARRAY/) { $r = "[" . join(",", map { show($_, $seen) } @$d) . "]" } elsif ($t =~ /HASH/) { $r = "{" . join(",", map { "$_=>" . show($d->{$_}, $seen) } sort keys %$d) . "}" } else { $r = "\\" . show($$d, $seen) }
  delete $seen->{$t}; ($cls =~ /^(ARRAY|HASH|SCALAR|REF)$/ ? "" : "$cls=") . $r }
for my $f (\&freeze, \&nfreeze) { print "back $_: ", show(thaw($f->($c[$_]))), "\n" for 0 .. $#c; }
my $t = thaw(freeze([$o, $o])); print "shared kept: ", ($t->[0] == $t->[1] ? 1 : 0), " ", ref($t->[0]), "\n";
my $t2 = thaw(nfreeze($cyc)); print "cycle kept: ", ($t2->[1] == $t2 ? 1 : 0), "\n";
my $x = 5; my $t3 = thaw(freeze([\$x, \$x])); ${$t3->[0]}++; print "shared scalar: ${$t3->[1]}\n";
my $file = "test/io_tmp_storable2.bin";
store($c[1], $file); print "store/retrieve: ", show(retrieve($file)), "\n";
nstore($c[3], $file); print "nstore/retrieve: ", show(retrieve($file)), "\n"; unlink $file;
print "bad: ", (eval { freeze([sub { 1 }]); 1 } ? "ok" : $@ =~ /Can't store CODE items/ ? "CODE refused" : $@), "\n";
print "notref: ", (eval { freeze("x"); 1 } ? "ok" : $@ =~ /not a reference/ ? "refused" : $@), "\n";
my $d = dclone({ a => [1, 2], o => $o }); print "dclone: ", show($d), " ", ($d->{o} == $o ? "same" : "copy"), "\n";
# 途中で切れたデータは undef、壊れたデータは die (異常終了しない)
{ my $img = nfreeze([1, { a => "bc" }, \"s"]); my ($ok, $bad) = (0, 0);
  for my $n (2 .. length($img) - 1) { defined(eval { thaw(substr($img, 0, $n)) }) ? $ok++ : $bad++ }
  print "truncated: $ok read, $bad undef\n";
  print "garbage: ", (eval { thaw("\x05\x0b\x7f"); 1 } ? "read" : "refused"), "\n"; }
print "not a storable: ", (eval { open my $f, ">", $file; print $f "hello"; close $f; retrieve($file); 1 } ? "read" : $@ =~ /Magic number checking on storable file failed/ ? "refused" : $@), "\n"; unlink $file;
# ハンドルへ続けて書いたものを、1 つずつ読み戻す
{ use Storable qw(store_fd nstore_fd fd_retrieve);
  open my $o, ">", $file or die; store_fd([1, 2], $o); nstore_fd({ k => "v" }, $o); store_fd(\"last", $o); close $o;
  open my $i, "<", $file or die; my @r = map { fd_retrieve($i) } 1 .. 3; close $i; unlink $file;
  print "fd: @{$r[0]} $r[1]{k} ${$r[2]} net=", (Storable::last_op_in_netorder() ? 1 : 0), "\n";
  print "too new: ", (eval { thaw("\xFF\xFF"); 1 } ? "read" : $@ =~ /v127\.255 more recent than I am/ ? "refused" : $@), "\n"; }
