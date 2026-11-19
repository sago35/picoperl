package File::Spec::Unix;
# File::Spec の picoperl 版の本体 (Unix 形式のパス)。本家の File::Spec::Unix は読み込めないので
# 書き直してある。ボードのドライブも "/" 区切りなので、そのまま使える。
#
# 本家と同じく、パスを文字列として整えるだけでファイルシステムは見ない
# (tmpdir と、基準を省いた rel2abs / abs2rel だけが環境を見る)。
use strict;
our $VERSION = '3.94';

sub curdir  { '.' }
sub updir   { '..' }
sub rootdir { '/' }
sub devnull { '/dev/null' }
sub case_tolerant { 0 }

sub canonpath {
    my ($self, $path) = @_;
    return undef unless defined $path;
    $path =~ s{/{2,}}{/}g;                              # a//b   -> a/b
    $path =~ s{(?:/\.)+(?:/|\z)}{/}g;                   # a/./b  -> a/b
    $path =~ s{^(?:\./)+}{}s unless $path eq "./";      # ./a    -> a
    $path =~ s{^/(?:\.\./)*\.\.\z}{/};                  # /../.. -> /
    $path =~ s{^/(?:\.\./)+}{/};                        # /../a  -> /a
    $path =~ s{/\z}{} unless $path eq "/";              # a/     -> a
    return $path;
}

sub catdir {
    my $self = shift;
    return $self->canonpath(join('/', @_, ''));
}
sub catfile {
    my $self = shift;
    my $file = $self->canonpath(pop @_);
    return $file unless @_;
    my $dir = $self->catdir(@_);
    $dir .= "/" unless substr($dir, -1) eq "/";
    return $dir . $file;
}
sub join { my $self = shift; return $self->catfile(@_) }

sub file_name_is_absolute {
    my ($self, $file) = @_;
    return scalar($file =~ m{^/}s);
}
sub no_upwards {
    my $self = shift;
    return grep { !/^\.{1,2}\z/s } @_;
}
sub path {
    return () unless exists $ENV{PATH};
    my @path = split /:/, $ENV{PATH}, -1;
    for (my $i = 0; $i < @path; $i++) { $path[$i] = '.' if $path[$i] eq '' }
    return @path;
}

my $tmpdir;
sub tmpdir {
    my $self = shift;
    return $tmpdir if defined $tmpdir;
    for my $dir ($ENV{TMPDIR}, "/tmp") {
        next unless defined $dir && length $dir;
        if (-d $dir && -w $dir) { $tmpdir = $dir; last }
    }
    $tmpdir = $self->curdir unless defined $tmpdir;
    return $tmpdir = $self->canonpath($tmpdir);
}

sub splitpath {
    my ($self, $path, $nofile) = @_;
    my ($directory, $file) = ('', '');
    if ($nofile) { $directory = $path }
    else {
        $path =~ m{^ ( (?: .* / (?: \.\.?\z )? )? ) ([^/]*) }xs;
        ($directory, $file) = ($1, $2);
    }
    return ('', $directory, $file);
}
sub splitdir {
    my ($self, $dirs) = @_;
    return split m{/}, $dirs, -1;
}
sub catpath {
    my ($self, $volume, $directory, $file) = @_;
    if ($directory ne '' && $file ne '' && substr($directory, -1) ne '/' && substr($file, 0, 1) ne '/') {
        return $directory . "/$file";
    }
    return $directory . $file;
}

sub _cwd { require Cwd; return Cwd::getcwd() }

sub rel2abs {
    my ($self, $path, $base) = @_;
    if (!$self->file_name_is_absolute($path)) {
        if    (!defined $base || $base eq '')           { $base = $self->_cwd }
        elsif (!$self->file_name_is_absolute($base))    { $base = $self->rel2abs($base) }
        else                                            { $base = $self->canonpath($base) }
        $path = $self->catdir($base, $path);
    }
    return $self->canonpath($path);
}

sub abs2rel {
    my ($self, $path, $base) = @_;
    $base = $self->_cwd unless defined $base && length $base;
    $path = $self->canonpath($path);
    $base = $self->canonpath($base);
    if (!($self->file_name_is_absolute($path) && $self->file_name_is_absolute($base))) {
        $path = $self->rel2abs($path);
        $base = $self->rel2abs($base);
    }
    return $self->curdir if $path eq $base;
    my @path = grep { length } $self->splitdir($path);
    my @base = grep { length } $self->splitdir($base);
    while (@path && @base && $path[0] eq $base[0]) { shift @path; shift @base }
    my @up = ($self->updir) x @base;
    return $self->curdir unless @up || @path;
    return $self->catdir(@up, @path);
}

1;
