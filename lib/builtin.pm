package builtin;
# The perl 5.36+ builtin:: functions (the stable subset).
#
#   use builtin qw(true false ceil floor trim blessed reftype refaddr);
#
# ceil/floor/blessed/reftype/refaddr are C natives registered by the
# interpreter; true/false/trim are plain perl. Not provided: is_bool
# (picoperl has no boolean flag on values), weaken/unweaken (reference
# counting only), the created_as_* checks, and export_lexically.
use strict;
use Exporter 'import';
our @EXPORT_OK = qw(true false ceil floor trim blessed reftype refaddr indexed);

sub true ()  { 1 == 1 }
sub false () { 1 == 0 }

sub trim {
    my ($s) = @_;
    return undef unless defined $s;
    $s =~ s/\A\s+//;
    $s =~ s/\s+\z//;
    return $s;
}

# indexed(LIST) -> (0, $x0, 1, $x1, ...)
sub indexed {
    my @r;
    for my $i (0 .. $#_) { push @r, $i, $_[$i] }
    return @r;
}

1;
