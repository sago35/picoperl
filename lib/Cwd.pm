package Cwd;
# Cwd の picoperl 版 (本家は XS か、pwd コマンドをバッククォートで呼ぶ)。
#
#   use Cwd;                          # cwd getcwd
#   use Cwd qw(abs_path realpath);
#
# ホスト版では OS に問い合わせる。ボードにはカレントディレクトリの概念が無いので、
# cwd は常に "/" (PICOPERL ドライブの先頭) で、abs_path は "." と ".." を畳むだけになる。
# Cwd::chdir は組み込みの chdir を呼んで $ENV{PWD} を更新する (ボードでは常に失敗)。
# 本家との違い: 存在しないパスの abs_path は undef ではなく、畳んだだけのパスを返す。
use strict;
use Exporter 'import';
our $VERSION = '3.93';
our @EXPORT    = qw(cwd getcwd fastcwd fastgetcwd);
our @EXPORT_OK = qw(chdir abs_path fast_abs_path realpath fast_realpath);

sub getcwd {
    my $d = defined &Cwd::_getcwd ? Cwd::_getcwd() : undef;
    return defined $d ? $d : "/";
}
sub cwd        { getcwd() }

# use Cwd 'chdir' で組み込みの chdir を置き換える版。移動できたら $ENV{PWD} を合わせる
sub chdir {
    my $dir = @_ ? shift : ($ENV{HOME} // $ENV{LOGDIR});
    return 0 unless defined $dir && length $dir && defined &Cwd::_chdir;
    return 0 unless Cwd::_chdir($dir);
    $ENV{PWD} = getcwd();
    return 1;
}
sub fastcwd    { getcwd() }
sub fastgetcwd { getcwd() }

sub abs_path {
    my $path = @_ ? shift : ".";
    $path = "." unless defined $path && length $path;
    if (defined &Cwd::_realpath) {              # シンボリックリンクも解決する
        my $real = Cwd::_realpath($path);
        return $real if defined $real;
    }
    $path = getcwd() . "/" . $path unless $path =~ m{^/};
    my @out;
    for my $part (split m{/+}, $path) {
        next if $part eq "" || $part eq ".";
        if ($part eq "..") { pop @out; next }
        push @out, $part;
    }
    return "/" . join("/", @out);
}
sub realpath      { abs_path(@_) }
sub fast_abs_path { abs_path(@_) }
sub fast_realpath { abs_path(@_) }

1;
