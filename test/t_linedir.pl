use strict; use warnings;
# #line 指定: 次の行をその行番号として数える
sub where { (caller)[2] }
print "normal: ", __LINE__, "\n";
#line 100
print "after directive: ", __LINE__, " ", where(), "\n";
print "next: ", __LINE__, "\n";
# line 200
print "spaced form: ", __LINE__, "\n";
  #line 300
print "indented is a plain comment: ", __LINE__, "\n";
#line 400 "other_name.pl"
eval { die "oops\n" }; eval { die "msg" }; print "die line: ", ($@ =~ /line (\d+)/)[0], "\n";
#linefoo 500
#line abc
print "not directives: ", __LINE__, "\n";
my $w = ""; { local $SIG{__WARN__} = sub { $w = $_[0] };
#line 600
warn "careful"; } print "warn line: ", ($w =~ /line (\d+)/)[0], "\n";
