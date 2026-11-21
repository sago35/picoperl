use strict; use warnings;
use version;
# version オブジェクト (lib/version.pm): 本物の perl と同じ結果になること
sub show { my $s = shift; my $v = eval { version->parse($s) }; return "ERR(" . ($@ =~ /^(Invalid version format \([^)]*\))/ ? $1 : substr($@, 0, 40)) . ")" unless $v; join("/", "$v", do { no warnings; $v->numify }, $v->normal, ($v->is_qv ? "qv" : "dec"), ($v->is_alpha ? "alpha" : "-")) }
my @in = ("1.23", "1.2", "1", "0", "1.2.3", "v1.2", "v1.2.3", "v1", "1.02_03", "1.230", "1.2345", "1.002003", "0.000001", "1.2.3.4", "v1.2_3", "1.2.3_4", ".5", "00.1", "1.", "10.20.30", "1.0", "v0.0.1", "1.23456789", "3.14159", "5.036", "5.36.0", "v5.36", "1e3", "1.5e-2", "undef", "");
print "$_ => ", show($_), "\n" for @in;
{ no warnings; local $SIG{__WARN__} = sub {}; print "junk: ", join(" | ", map { "$_ => " . show($_) } "junk", "-1", "1_2", "1.2_3_4", "1.2x", "v", "1._2", "1.2_"), "\n"; }
my @cmp = (["1.2", "1.10"], ["1.2.3", "1.2.4"], ["1.2", "v1.200"], ["1.0", "1"], ["1.2.3", "1.002003"], ["v1.2", "1.2"], ["1.10", "1.9"], ["v1.10", "v1.9"], ["2", "10"], ["1.2.0", "1.2"], ["0.1", "0.10"], ["1.02_03", "1.0203"]);
print "cmp: ", join(" ", map { my ($a, $b) = @$_; my $c = version->parse($a) <=> version->parse($b); "$a" . ($c < 0 ? "<" : $c > 0 ? ">" : "=") . "$b" } @cmp), "\n";
my $v = version->parse("1.2.3");
print "ops: ", join(" ", ($v > "1.2.2" ? 1 : 0), ($v < "1.3" ? 1 : 0), ($v == "v1.2.3" ? 1 : 0), ("1.2.4" > $v ? 1 : 0), ($v eq "1.2.3" ? 1 : 0), ($v ne "v1.2.3" ? 1 : 0), ($v ? "true" : "false"), (version->parse("0") ? "true" : "false"), (version->parse("0.0") ? "true" : "false")), "\n";
print "sort: ", join(" ", sort { version->parse($a) <=> version->parse($b) } qw(1.10 1.9 1.2.3 v1.2 0.5 2)), "\n";
print "declare: ", join(" ", map { my $q = version->declare($_); "$q/" . $q->normal . "/" . $q->numify } "1.2", "1.2.3", "v1.2", "1", "1.02"), " | qv: ", qv("1.2"), " ", qv("1.2.3")->numify, "\n";
print "vstring: ", version->new(v1.2.3), " ", version->declare(v1.22.333)->normal, "\n";
print "copy: ", version->new(version->new("1.5")), " ", ref(version->new("1.5")), " ", (version->new("1.5")->isa("version") ? "isa" : "no"), "\n";
print "lax: ", join("", map { version::is_lax($_) ? 1 : 0 } "1.2", "v1.2.3", "1.2.3", "1.02_03", "x", "1.2x", "v1", ".5", "undef", ""), " strict: ", join("", map { version::is_strict($_) ? 1 : 0 } "1.2", "v1.2.3", "1.2.3", "1.02_03", "v1.2", "01.2", "1", "0.5"), "\n";
{ package VtMod; our $VERSION = "1.25"; }
print "_VERSION: ", version::_VERSION("VtMod"), " ", (eval { version::_VERSION("VtMod", "1.2"); 1 } ? "ok" : "died"), " ", (eval { version::_VERSION("VtMod", "1.3"); 1 } ? "ok" : $@ =~ /^(VtMod version 1.3 required--this is only version 1.25)/ ? $1 : $@), "\n";
print "noop: ", (eval { my $x = $v + 1; 1 } ? "allowed" : $@ =~ /^(operation not supported with version object)/ ? $1 : $@), "\n";
