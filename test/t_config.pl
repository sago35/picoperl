# lib/Config.pm (picoperl 版): 値は環境で変わるので、本家と同じになるはずの性質だけを確かめる
use strict;
use warnings;
use Config;

my ($r, $v, $s) = $] =~ /^(\d+)\.(\d{3})(\d{3})/;
print "version: ", ($Config{version} eq join(".", $r + 0, $v + 0, $s + 0) ? "matches \$]" : "differs: $Config{version}"), "\n";
print "osname: ", ($Config{osname} eq $^O ? "matches \$^O" : "differs"), "\n";
print "path_sep: [$Config{path_sep}] exe_ext: [$Config{exe_ext}] ivsize: $Config{ivsize} nvsize: $Config{nvsize}\n";
print "revision: $Config{PERL_REVISION} ", ($Config{PERL_VERSION} == $v + 0 ? "version ok" : "version ng"), "\n";
print "defined: ", join("", map { defined $Config{$_} ? "y" : "n" } qw(archname perlpath startperl byteorder privlibexp sig_name cc so)), "\n";
print "missing: ", join("", map { defined $Config{$_} ? "y" : "n" } qw(no_such_key picoperl_never_set)), " ", (exists $Config{no_such_key} ? "exists" : "absent"), "\n";
print "keys: ", (scalar(keys %Config) > 20 ? "many" : "few"), "\n";
my @path = split /$Config{path_sep}/, "/a:/b";
print "split by path_sep: ", scalar(@path), "\n";
{ package CfgUser; use Config; sub os { $Config{osname} } sub qualified { $Config::Config{osname} } }
{ package CfgOnly; use strict; use Config qw(%Config); sub sep { $Config{path_sep} } }
print "explicit import in package: [", CfgOnly::sep(), "]\n";
print "in package: ", (CfgUser::os() eq $^O ? "ok" : "ng"), " ", (CfgUser::qualified() eq $^O ? "ok" : "ng"), "\n";
print "myconfig: ", (Config::myconfig() =~ /osname=\Q$^O\E/ ? "mentions osname" : "no osname"), "\n";
print "sig_name has ALRM: ", ($Config{sig_name} =~ /\bALRM\b/ ? "yes" : "no"), "\n";
print "done\n";
