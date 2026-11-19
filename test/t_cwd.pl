# lib/Cwd.pm (picoperl 版) を本家と突き合わせる (make test は リポジトリの先頭で実行される)
use strict;
use warnings;
use Cwd;
use Cwd qw(abs_path realpath);

my $here = getcwd();
print "getcwd: ", ($here =~ m{^/} ? "absolute" : "relative: $here"), " ", (-d $here ? "dir" : "not a dir"), "\n";
print "cwd: ", (cwd() eq $here ? "same" : "differs"), " ", (Cwd::getcwd() eq $here ? "same" : "differs"), "\n";
print "has test dir: ", (-d "$here/test" ? "yes" : "no"), "\n";
my $real = abs_path(".");
print "abs_path dot: ", ($real =~ m{^/} ? "absolute" : "relative"), " ", (abs_path() eq $real ? "default is dot" : "differs"), "\n";
sub rel { my $p = shift; index($p, $real) == 0 ? "<here>" . substr($p, length $real) : $p }
print "abs_path: ", join(" ", map { rel(abs_path($_)) } "test", "test/", "test/../test/./t_cwd.pl", "./test/lib/..", "test//lib"), "\n";
print "realpath: ", rel(realpath("test/t_cwd.pl")), " ", rel(Cwd::realpath("test")), "\n";
print "absolute input: ", abs_path("/"), " ", (abs_path($real) eq $real ? "idempotent" : "changed"), " ", rel(abs_path("$real/test/..")), "\n";
print "missing last part: ", rel(abs_path("test/no_such_file_here")), "\n";
{ package CwdUser; use Cwd; sub where { cwd() } }
print "in package: ", (CwdUser::where() eq $here ? "same" : "differs"), "\n";
print "done\n";
# Cwd::chdir: 移動して $ENV{PWD} を合わせる。失敗したら偽
{
    my $start = Cwd::getcwd();
    print "chdir: ", (Cwd::chdir("test") ? "ok" : "ng"), " ", (Cwd::getcwd() eq "$start/test" ? "moved" : "not moved"), " ", ($ENV{PWD} eq Cwd::getcwd() ? "PWD set" : "PWD stale"), "\n";
    print "chdir back: ", (Cwd::chdir("..") ? "ok" : "ng"), " ", (Cwd::getcwd() eq $start ? "back" : "lost"), "\n";
    print "chdir missing: ", (Cwd::chdir("test/no_such_dir") ? "ok" : "ng"), " ", (Cwd::getcwd() eq $start ? "stayed" : "moved"), "\n";
}
