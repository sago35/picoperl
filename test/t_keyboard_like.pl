use strict; no warnings;
# Mimics what a keyboard script does, without hardware.
use constant { KC_NO => 0, KC_A => 4, KC_B => 5, KC_LSHIFT => 0xE1, KC_LCTRL => 0xE0 };
my @rows = (2, 3); my @cols = (4, 5, 6);
my @keymap = (
  [ KC_A,      KC_B,   KC_LSHIFT ],
  [ KC_LCTRL,  KC_NO,  KC_A ],
);
my %pressed;   # "r,c" => 1
sub report {
    my $mod = 0; my @keys;
    for my $rc (sort keys %pressed) {
        my ($r, $c) = split /,/, $rc;
        my $kc = $keymap[$r][$c];
        next if $kc == KC_NO;
        if ($kc >= 0xE0 && $kc <= 0xE7) { $mod |= 1 << ($kc - 0xE0) }
        else { push @keys, $kc if @keys < 6 }
    }
    return sprintf("mod=%02x keys=%s", $mod, join(",", map { sprintf "%02x", $_ } @keys));
}
$pressed{"0,0"} = 1; print report(), "\n";
$pressed{"0,2"} = 1; print report(), "\n";
$pressed{"1,0"} = 1; delete $pressed{"0,0"}; print report(), "\n";
%pressed = (); print report(), "\n";
# debounce simulation
my @hist = (1, 1, 0, 0, 0, 1, 0, 0, 0, 0);
my $state = 0; my $stable = 0; my @events;
for my $i (0..$#hist) {
    if ($hist[$i] == $state) { $stable++ } else { $stable = 0; $state = $hist[$i]; push @events, "$i:$state" }
}
print "@events\n";
my $t0 = 1000; my $t1 = 1015; print "elapsed ", $t1 - $t0, "ms\n";
my @layers = ([KC_A, KC_B], [KC_B, KC_A]); my $layer = 1; print $layers[$layer][0], "\n";
