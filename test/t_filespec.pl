# lib/File/Spec.pm (picoperl 版) を本家と突き合わせる
use strict;
use warnings;
use File::Spec;
use File::Spec::Functions qw(catfile catdir canonpath splitdir file_name_is_absolute);

sub show { join("|", map { defined $_ ? "[$_]" : "undef" } @_) }

print "constants: ", show(File::Spec->curdir, File::Spec->updir, File::Spec->rootdir, File::Spec->devnull, File::Spec->case_tolerant), "\n";
print "canonpath: ", show(map { File::Spec->canonpath($_) } "a/b", "a//b///c", "a/./b/.", "./a", "./", ".", "/", "//", "a/", "/a/", "/../a", "/../..", "a/../b", "/./", "", "a/b/..", "../a", "/a/./b//c/"), "\n";
print "canonpath undef: ", show(File::Spec->canonpath(undef)), "\n";
print "catdir: ", show(File::Spec->catdir("a", "b"), File::Spec->catdir("/a", "b/"), File::Spec->catdir("", "a"), File::Spec->catdir(), File::Spec->catdir("a"), File::Spec->catdir("a", "", "b"), File::Spec->catdir("a", ".", "b"), File::Spec->catdir("/")), "\n";
print "catfile: ", show(File::Spec->catfile("a", "b", "c.txt"), File::Spec->catfile("c.txt"), File::Spec->catfile("/", "c.txt"), File::Spec->catfile("a/", "/c.txt"), File::Spec->catfile("", "c.txt"), File::Spec->catfile("a", "b/"), File::Spec->catfile(), File::Spec->catfile("lib", "Foo", "Bar.pm")), "\n";
print "join: ", show(File::Spec->join("a", "b")), "\n";
print "absolute: ", show(map { File::Spec->file_name_is_absolute($_) ? 1 : 0 } "/a", "a", "./a", "", "/"), "\n";
print "no_upwards: ", show(File::Spec->no_upwards(".", "..", "a", ".b", "...", "c")), "\n";
for my $p ("/a/b/c.txt", "c.txt", "a/b/", "/", "a/b/..", "a/.", "", "/c.txt", "a/b/c", "..") {
    print "splitpath $p: ", show(File::Spec->splitpath($p)), " nofile: ", show(File::Spec->splitpath($p, 1)), "\n";
}
print "splitdir: ", show(File::Spec->splitdir("a/b/c")), " ", show(File::Spec->splitdir("/a/b/")), " ", show(File::Spec->splitdir("")), " ", show(File::Spec->splitdir("a//b")), "\n";
print "catpath: ", show(File::Spec->catpath("", "a/b", "c"), File::Spec->catpath("", "a/b/", "c"), File::Spec->catpath("", "", "c"), File::Spec->catpath("", "a", ""), File::Spec->catpath("", "/", "c")), "\n";
print "roundtrip: ", show(map { my @p = File::Spec->splitpath($_); File::Spec->catpath(@p) } "/a/b/c.txt", "c.txt", "a/b/"), "\n";
print "rel2abs: ", show(File::Spec->rel2abs("a/b", "/base"), File::Spec->rel2abs("/abs/x", "/base"), File::Spec->rel2abs("../x", "/base/dir"), File::Spec->rel2abs("./a", "/base/"), File::Spec->rel2abs("a", "/")), "\n";
print "abs2rel: ", show(File::Spec->abs2rel("/a/b/c", "/a"), File::Spec->abs2rel("/a/b", "/a/b"), File::Spec->abs2rel("/a/x", "/a/b/c"), File::Spec->abs2rel("/x", "/a/b"), File::Spec->abs2rel("/a/b/c", "/"), File::Spec->abs2rel("/", "/a"), File::Spec->abs2rel("/a/b/", "/a/")), "\n";
require Cwd;
my $cwd = Cwd::getcwd();
print "cwd based: ", (File::Spec->rel2abs("x/y") eq "$cwd/x/y" ? "rel2abs ok" : "rel2abs differs"), " ", show(File::Spec->abs2rel("$cwd/test/t.pl"), File::Spec->abs2rel("test/t.pl", "test"), File::Spec->abs2rel("a", "b")), "\n";
my $tmp = File::Spec->tmpdir;
print "tmpdir: ", (-d $tmp ? "a directory" : "missing"), " ", (File::Spec->tmpdir eq File::Spec->tmpdir ? "stable" : "changes"), "\n";
print "path: ", (scalar(File::Spec->path) == scalar(my @p = split /:/, $ENV{PATH}, -1) ? "same count as PATH" : "differs"), "\n";
print "functions: ", show(catfile("a", "b.txt"), catdir("a", "b"), canonpath("a//b/"), splitdir("x/y"), file_name_is_absolute("/x") ? 1 : 0), "\n";
print "isa: ", (File::Spec->isa("File::Spec::Unix") ? "Unix" : "other"), " ", show(File::Spec::Unix->catfile("u", "v")), "\n";
print "done\n";
