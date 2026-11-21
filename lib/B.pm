package B;
# B の picoperl 版 (本物は perl の内部を覗く XS)。サブについて調べる部分だけを用意している:
#   my $cv = B::svref_2object(\&func);
#   $cv->GV->NAME            サブの名前 (無名なら __ANON__)
#   $cv->GV->STASH->NAME     定義されたパッケージ
#   $cv->FILE、$cv->START->line  定義されたファイルと行
# Test2 や autodie がサブの素性を知るのに使う。構文木 (op) をたどる機能はありません。
use strict;
our $VERSION = '1.89';
our @EXPORT_OK = qw(svref_2object perlstring cstring class sub_generation);
our @specialsv_name = qw(Nullsv &PL_sv_undef &PL_sv_yes &PL_sv_no (SV*)pWARN_ALL (SV*)pWARN_NONE (SV*)pWARN_STD &PL_sv_zero);

sub svref_2object {
    my $ref = shift;
    my $type = ref($ref) ? builtin::reftype($ref) : '';
    if ($type eq 'CODE') {
        my ($pkg, $name, $file, $first, $last) = __subinfo($ref);
        return bless { ref => $ref, pkg => $pkg, name => $name, file => $file, first => $first, last => $last }, 'B::CV';
    }
    return bless { ref => $ref }, $type eq 'GLOB' ? 'B::GV' : $type eq 'HASH' ? 'B::HV' : $type eq 'ARRAY' ? 'B::AV' : 'B::SV';
}
sub class { my $c = ref shift; $c =~ s/^.*:://; $c }
sub sub_generation { 1 }
# 文字列を、同じ値になる perl / C の二重引用符の書き方にする
sub perlstring { my $s = shift; return '0' unless defined $s; $s =~ s/([\\"\$\@])/\\$1/g; $s =~ s/\n/\\n/g; $s =~ s/\t/\\t/g; $s =~ s/([^ -~])/sprintf '\\x{%x}', ord $1/ge; qq("$s") }
sub cstring    { my $s = shift; return '0' unless defined $s; $s =~ s/([\\"])/\\$1/g; $s =~ s/\n/\\n/g; $s =~ s/([^ -~])/sprintf '\\%03o', ord $1/ge; qq("$s") }

package B::OBJECT;
package B::SV;      our @ISA = ('B::OBJECT');
sub object_2svref { $_[0]{ref} }
sub REFCNT { 1 }
sub FLAGS  { 0 }
sub SV     { $_[0] }
package B::AV;      our @ISA = ('B::SV');
package B::SPECIAL; our @ISA = ('B::OBJECT');
package B::HV;      our @ISA = ('B::SV');
sub NAME { $_[0]{name} }
package B::GV;      our @ISA = ('B::SV');
sub NAME     { $_[0]{name} }
sub SAFENAME { $_[0]{name} }
sub STASH    { bless { name => $_[0]{pkg} }, 'B::HV' }
sub FILE     { $_[0]{file} }
sub LINE     { $_[0]{line} }
package B::COP;     our @ISA = ('B::OBJECT');      # 1 つの文: 行番号だけを持つ
sub line { $_[0]{line} }
sub file { $_[0]{file} }
sub next { $_[0]{next} }
sub name { 'nextstate' }
package B::CV;      our @ISA = ('B::SV');
sub GV     { my $s = shift; bless { pkg => $s->{pkg}, name => $s->{name}, file => $s->{file}, line => $s->{first} }, 'B::GV' }
sub STASH  { bless { name => $_[0]{pkg} }, 'B::HV' }
sub FILE   { $_[0]{file} }
sub DEPTH  { 0 }
sub XSUB   { $_[0]{file} eq '' ? 1 : 0 }           # ファイルが無い = インタプリタ組み込みの関数
sub CvFLAGS { 0 }
sub const_sv { bless {}, 'B::SPECIAL' }
sub ROOT   { $_[0]->START }
sub START  {                                       # 最初の文と最後の文だけの、行番号の列
    my $s = shift;
    return bless {}, 'B::SPECIAL' unless $s->{first};
    my $end = bless { line => $s->{last}, file => $s->{file} }, 'B::COP';
    return $s->{first} == $s->{last} ? $end : bless { line => $s->{first}, file => $s->{file}, next => $end }, 'B::COP';
}
1;
