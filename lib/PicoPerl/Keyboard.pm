package PicoPerl::Keyboard;
# Matrix keyboard support for picoperl.
#
#   use PicoPerl::Keycodes;
#   use PicoPerl::Keyboard;
#   keyboard_run(
#       rows   => [2, 3],           # GPIOs of the rows
#       cols   => [4, 5, 6],        # GPIOs of the columns
#       keymap => [ [ [KC_A, KC_B, MO(1)], [KC_LSFT, KC_C, KC_ENT] ],   # layer 0
#                   [ [KC_1, KC_2, KC_TRNS], [KC_TRNS, KC_3, KC_ESC] ] ], # layer 1
#   );
#
# Options:
#   diode  => 'row2col' (default: rows are driven, columns are read)
#             'col2row' (columns are driven, rows are read)
#   active => 'low'  (default: driven pin goes low, inputs use pull-ups, pressed reads 0)
#             'high' (driven pin goes high, inputs use pull-downs, pressed reads 1)
#   direct_keys => { GPIO => keycode, ... }   extra switches wired straight to a pin
#                  (pull-up, active low), e.g. an encoder's push button
#   encoder => { a => GPIO, b => GPIO, cw => keycode, ccw => keycode, precision => 4 }
#              rotary encoder: taps cw/ccw once per click
#   debounce_ms, settle_us, scan_interval_ms, caps_led
#   on_report => sub { my ($mod, @keys) = @_ }, on_press => sub { my ($row, $col, $kc) = @_ },
#   on_release => sub { my ($row, $col) = @_ }, on_encoder => sub { my ($dir) = @_ }, on_idle => sub { }
#
# Instead of keyboard_run() you can call keyboard_init(%opts) once and then
# keyboard_step() from your own loop; keyboard_step() returns 1 when the
# debounced key state changed.
use strict;
use PicoPerl::Keycodes;
use Gpio;
use Hid;
use Matrix;
use Encoder;
use Board;
use Time::HiRes qw(usleep sleep time);
use Exporter 'import';

our @EXPORT = qw(keyboard_init keyboard_step keyboard_run keyboard_state keyboard_layer keyboard_tap);

my (@outs, @ins, @keymap, $ncols, $nrows, $nkeys, $swapped, $active_high);
my ($debounce_ms, $settle_us);
my (@raw, @since, @state, @prev_state);
my (@direct_pins, @direct_codes);
my ($enc, $enc_cw, $enc_ccw);
my $toggled_layers = 0;
my $last_report = "";           # keyboard report last accepted by the USB stack
my ($want_report, @want);       # report we want to send (retried until Hid::ready)
my ($want_consumer, $last_consumer) = (0, 0);   # consumer control usage (0 = none)
my %opts;

sub _now_ms { return int(time() * 1000) }

sub keyboard_init {
    %opts = @_;
    my $rows = $opts{rows} or die "keyboard_init: rows => [...] is required\n";
    my $cols = $opts{cols} or die "keyboard_init: cols => [...] is required\n";
    @keymap = @{ $opts{keymap} || [] } or die "keyboard_init: keymap => [...] is required\n";
    $swapped = ($opts{diode} || 'row2col') eq 'col2row';
    $active_high = ($opts{active} || 'low') eq 'high';
    # "outs" are driven, "ins" are read; keymap is always indexed [layer][row][col]
    @outs = $swapped ? @$cols : @$rows;
    @ins  = $swapped ? @$rows : @$cols;
    $nrows = @$rows; $ncols = @$cols; $nkeys = $nrows * $ncols;
    $debounce_ms = $opts{debounce_ms} // 5;
    $settle_us   = $opts{settle_us} // 30;

    for my $p (@outs) { Gpio::configure($p, 'output'); Gpio::put($p, $active_high ? 0 : 1); }
    for my $p (@ins)  { Gpio::configure($p, $active_high ? 'input_pulldown' : 'input_pullup'); }

    # direct (non-matrix) keys: pull-up, active low
    my $d = $opts{direct_keys} || {};
    @direct_pins = sort { $a <=> $b } keys %$d;
    @direct_codes = map { $d->{$_} } @direct_pins;
    for my $p (@direct_pins) { Gpio::configure($p, 'input_pullup') }

    # rotary encoder
    my $e = $opts{encoder};
    if ($e) {
        $enc = Encoder->new($e->{a}, $e->{b}, precision => $e->{precision} // 4);
        ($enc_cw, $enc_ccw) = ($e->{cw}, $e->{ccw});
    }

    my $n = $nkeys + @direct_pins;
    @raw = (0) x $n; @since = (0) x $n; @state = (0) x $n; @prev_state = (0) x $n;
    $toggled_layers = 0; $last_report = ""; $want_report = ""; $want_consumer = 0; $last_consumer = 0;
    print "PicoPerl::Keyboard: ${nrows}x${ncols} matrix, ", scalar(@keymap), " layer(s)",
          (@direct_pins ? ", " . scalar(@direct_pins) . " direct key(s)" : ""), ($e ? ", encoder" : ""),
          ", heap free ", Board::heap_free() // "?", "\n";
    return 1;
}

sub _idx { my ($r, $c) = @_; return $r * $ncols + $c }

# read one input pin as pressed/not pressed for the matrix polarity
sub _read_in { my $p = shift; my $v = Gpio::get($p); return $active_high ? $v : ($v ? 0 : 1) }

sub _debounce {
    my ($idx, $v, $now) = @_;
    if ($v != $raw[$idx]) { $raw[$idx] = $v; $since[$idx] = $now; return 0 }
    if ($v != $state[$idx] && $now - $since[$idx] >= $debounce_ms) { $state[$idx] = $v; return 1 }
    return 0;
}

sub _poll_encoder {
    my $clicks = $enc->read;
    while ($clicks > 0) { $clicks--; keyboard_tap($enc_cw);  $opts{on_encoder}->(1)  if $opts{on_encoder}; }
    while ($clicks < 0) { $clicks++; keyboard_tap($enc_ccw); $opts{on_encoder}->(-1) if $opts{on_encoder}; }
}

# send a key press followed by a release (keeps currently held keys/modifiers)
sub keyboard_tap {
    my $kc = shift;
    return unless defined $kc && $kc != KC_NO;
    if (is_consumer($kc)) {
        return unless Hid::has_consumer();
        for my $try (1 .. 20) { last if Hid::consumer_ready(); sleep 0.001; }
        Hid::consumer_report(consumer_usage($kc));
        for my $try (1 .. 20) { last if Hid::consumer_ready(); sleep 0.001; }
        Hid::consumer_report($last_consumer);
        return;
    }
    my ($mod, @keys) = _build_report();
    my @held = grep { $_ != $kc } @keys;
    @held = @held[0 .. 4] if @held > 5;
    for my $try (1 .. 20) { last if Hid::ready(); sleep 0.001; }
    if (is_modifier($kc)) { Hid::keyboard_report($mod | modifier_bit($kc), @keys) }
    else { Hid::keyboard_report($mod, $kc, @held) }
    for my $try (1 .. 20) { last if Hid::ready(); sleep 0.001; }
    Hid::keyboard_report($mod, @keys);
    $last_report = join(",", $mod, @keys);
}

# scan the matrix once; returns 1 when the debounced state changed
sub keyboard_step {
    my $now = _now_ms();
    my $changed = 0;
    if (Matrix::available()) {
        my $s = Matrix::scan(\@outs, \@ins, $active_high ? 1 : 0, $settle_us);
        my $k = 0;
        for my $oi (0 .. $#outs) {
            for my $ii (0 .. $#ins) {
                my $idx = $swapped ? _idx($ii, $oi) : _idx($oi, $ii);
                $changed = 1 if _debounce($idx, $s->[$k++], $now);
            }
        }
    } else {
        for my $oi (0 .. $#outs) {
            Gpio::put($outs[$oi], $active_high ? 1 : 0);
            usleep($settle_us);
            for my $ii (0 .. $#ins) {
                my $idx = $swapped ? _idx($ii, $oi) : _idx($oi, $ii);
                $changed = 1 if _debounce($idx, _read_in($ins[$ii]), $now);
            }
            Gpio::put($outs[$oi], $active_high ? 0 : 1);
        }
    }
    for my $i (0 .. $#direct_pins) {
        $changed = 1 if _debounce($nkeys + $i, Gpio::get($direct_pins[$i]) ? 0 : 1, $now);
    }
    _poll_encoder() if $enc;
    if ($changed) {
        _handle_edges();
        @want = _build_report();
        $want_report = join(",", @want);
        $want_consumer = _build_consumer();
    }
    _flush_report();
    return $changed;
}

# send the wanted reports as soon as the USB stack can take them
sub _flush_report {
    if ($want_report ne $last_report && Hid::ready()) {
        my ($mod, @keys) = @want;
        if (Hid::keyboard_report($mod, @keys)) {
            $last_report = $want_report;
            $opts{on_report}->($mod, @keys) if $opts{on_report};
        }
    }
    if ($want_consumer != $last_consumer) {
        if (!Hid::has_consumer()) { $last_consumer = $want_consumer; print "consumer keys need firmware support\n"; }
        elsif (Hid::consumer_ready() && Hid::consumer_report($want_consumer)) {
            $last_consumer = $want_consumer;
            $opts{on_consumer}->($want_consumer) if $opts{on_consumer};
        }
    }
}

# first pressed consumer (media) key on the current layer, as a usage code
sub _build_consumer {
    my $layer = keyboard_layer();
    for my $idx (0 .. $#state) {
        next unless $state[$idx];
        my $kc = _keycode_of($layer, $idx);
        return consumer_usage($kc) if is_consumer($kc);
    }
    return 0;
}

# run forever
sub keyboard_run {
    keyboard_init(@_) if @_;
    my $led_caps = $opts{caps_led} // 1;
    my $interval = ($opts{scan_interval_ms} // 1) / 1000;
    while (1) {
        keyboard_step();
        Board::led(Hid::leds() & 0x02) if $led_caps;
        $opts{on_idle}->() if $opts{on_idle};
        sleep $interval;
    }
}

sub keyboard_state { return @state }

# resolve a key position on a layer, honouring KC_TRNS
sub _keycode_at {
    my ($layer, $ri, $ci) = @_;
    for (my $l = $layer; $l >= 0; $l--) {
        my $kc = $keymap[$l][$ri][$ci] // KC_NO;
        return $kc if $kc != KC_TRNS;
    }
    return KC_NO;
}

# keycode of key index $idx (matrix position or direct key) on $layer
sub _keycode_of {
    my ($layer, $idx) = @_;
    return $direct_codes[$idx - $nkeys] // KC_NO if $idx >= $nkeys;
    return _keycode_at($layer, int($idx / $ncols), $idx % $ncols);
}

sub keyboard_layer {
    my $layer = 0;
    for my $idx (0 .. $#state) {
        next unless $state[$idx];
        my $kc = _keycode_of(0, $idx);
        $layer = $kc & 0xFF if ($kc & 0xF000) == PP_LAYER_MO;
    }
    for my $l (1 .. $#keymap) { $layer = $l if ($toggled_layers & (1 << $l)) && $l > $layer; }
    return $layer;
}

# press edges: layer toggles, bootsel, user callback
sub _handle_edges {
    for my $idx (0 .. $#state) {
        my ($ri, $ci) = $idx < $nkeys ? (int($idx / $ncols), $idx % $ncols) : (-1, $idx - $nkeys);
        if ($state[$idx] && !$prev_state[$idx]) {
            my $kc = _keycode_of(0, $idx);
            $toggled_layers ^= 1 << ($kc & 0xFF) if ($kc & 0xF000) == PP_LAYER_TG;
            if ($kc == KC_BOOTSEL) { print "rebooting into BOOTSEL\n"; Board::reboot_bootsel(); }
            $opts{on_press}->($ri, $ci, $kc) if $opts{on_press};
        } elsif (!$state[$idx] && $prev_state[$idx]) {
            $opts{on_release}->($ri, $ci) if $opts{on_release};
        }
    }
    @prev_state = @state;
}

sub _build_report {
    my $layer = keyboard_layer();
    my ($mod, @keys) = (0);
    for my $idx (0 .. $#state) {
        next unless $state[$idx];
        my $kc = _keycode_of($layer, $idx);
        next if $kc == KC_NO || is_layer_key($kc) || $kc == KC_BOOTSEL || is_consumer($kc);
        if (is_modifier($kc)) { $mod |= modifier_bit($kc) }
        elsif (@keys < 6)     { push @keys, $kc }
    }
    return ($mod, @keys);
}

1;
