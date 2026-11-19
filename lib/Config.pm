package Config;
# Config の picoperl 版。本家は perl をビルドしたときの設定を tie したハッシュで見せるが、
# picoperl には tie が無いので、スクリプトがよく調べるキーだけを普通のハッシュで持つ。
#
#   use Config;
#   print "$Config{osname} $Config{version}\n";
#   my @path = split /$Config{path_sep}/, $ENV{PATH};
#
# picoperl に無いもの (C コンパイラ、動的ロード、fork、スレッド) のキーは入れていないので、
# $Config{d_fork} や $Config{useithreads} は本家の「無い」と同じ undef になる。
use strict;
use Exporter 'import';
our $VERSION = '5.044000';
our @EXPORT    = qw(%Config);
our @EXPORT_OK = qw(myconfig config_sh config_vars);

my ($rev, $ver, $sub) = map { $_ + 0 } $] =~ /^(\d+)\.(\d{3})(\d{3})/;
my $win = $^O eq 'MSWin32';

our %Config = (
    osname            => $^O,
    archname          => 'picoperl',
    version           => "$rev.$ver.$sub",
    api_versionstring => "$rev.$ver.0",
    PERL_REVISION     => $rev,
    PERL_VERSION      => $ver,
    PERL_SUBVERSION   => $sub,
    PERL_API_REVISION => $rev,
    PERL_API_VERSION  => $ver,
    PERL_API_SUBVERSION => 0,
    perl_patchlevel   => '',
    path_sep          => $win ? ';' : ':',
    exe_ext           => $win ? '.exe' : '',
    _exe              => $win ? '.exe' : '',
    perlpath          => 'picoperl',
    startperl         => '#!picoperl',
    sharpbang         => '#!',
    ivsize            => 8,         # 整数は 64 ビット、小数は倍精度
    uvsize            => 8,
    nvsize            => 8,
    ivtype            => 'long long',
    nvtype            => 'double',
    byteorder         => '12345678',
    useperlio         => 'define',
    privlibexp        => 'lib',
    archlibexp        => 'lib',
    sitelibexp        => 'lib',
    installprivlib    => 'lib',
    installarchlib    => 'lib',
    sig_name          => 'ZERO ALRM',     # %SIG で配送されるのは ALRM だけ
    sig_num           => '0 14',
    cc                => '',
    ccflags           => '',
    ld                => '',
    make              => '',
    so                => '',
    dlext             => '',
    lib_ext           => '',
    obj_ext           => '',
    libs              => '',
    perllibs          => '',
    libpth            => '',
);

sub myconfig {
    return "Summary of my picoperl (perl $Config{version} compatible) configuration:\n"
         . "  osname=$Config{osname}, archname=$Config{archname}\n"
         . "  ivsize=$Config{ivsize}, nvsize=$Config{nvsize}\n";
}
sub config_sh {
    return join "", map { "$_='" . (defined $Config{$_} ? $Config{$_} : '') . "'\n" } sort keys %Config;
}
sub config_vars {
    for my $name (@_) {
        print exists $Config{$name} ? "$name='$Config{$name}';\n" : "$name='UNKNOWN';\n";
    }
}
sub bincompat_options     { () }
sub non_bincompat_options { () }
sub compile_date          { '' }
sub local_patches         { () }

1;
