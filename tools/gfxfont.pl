#!/usr/bin/perl
# Converts bitmap fonts into src/pp_fonts.c for picoperl's Gfx engine.
#
#   perl tools/gfxfont.pl -o src/pp_fonts.c \
#        classic=glcdfont.c freemono_bold_9pt=FreeMonoBold9pt7b.h gophers_32pt=gophers32pt.go ...
#
# Accepted inputs (name=file):
#   *.c   the Adafruit "classic" 5x7 font (glcdfont.c)          -> license: BSD
#   *.h   Adafruit GFX font header (FreeMonoBold9pt7b.h, ...)     -> license: see the header (FreeFont GPL w/ exception)
#   *.go  tinygo.org/x/tinyfont font source (gophers32pt.go, ...) -> license: see the package
# All become the same glyph format (Adafruit GFX): bitmaps are rows of `width` bits,
# MSB first, packed continuously; (x, y) of a drawn string is the baseline.
use strict;
use warnings;

my ($out, @inputs);
while (@ARGV) {
    my $a = shift @ARGV;
    if ($a eq '-o') { $out = shift @ARGV; next }
    push @inputs, $a;
}
die "usage: gfxfont.pl -o pp_fonts.c name=font-file ...\n" unless $out && @inputs;

sub slurp { my $f = shift; open my $h, '<', $f or die "cannot read $f: $!"; local $/; my $s = <$h>; close $h; $s }
sub nums  { my $s = shift; return map { /^0x/i ? hex($_) : $_ + 0 } ($s =~ /(-?0x[0-9a-fA-F]+|-?\d+)/g) }

my @fonts;   # { name, bitmap => [..], glyphs => [ [off,w,h,adv,xo,yo] ... ], first, last, yadv }

for my $in (@inputs) {
    my ($name, $file) = split /=/, $in, 2;
    die "bad input '$in' (expected name=file)\n" unless defined $file;
    my $src = slurp($file);
    my %f = (name => $name, bitmap => [], glyphs => []);

    if ($file =~ /\.go$/ && $src =~ /const1bit\.Font/) {
        # tinyfont const1bit (e.g. shnm): OffsetMap = 6-byte records (rune 3BE + offset 3BE),
        # Data = glyph records (w h adv xo yo bitmap...). Kept verbatim as a sparse font.
        my ($mname) = $src =~ /OffsetMap:\s*(\w+)/ or die "$file: no OffsetMap\n";
        my ($dname) = $src =~ /Data:\s*(\w+)/     or die "$file: no Data\n";
        my ($yadv)  = $src =~ /YAdvance:\s*(\d+)/;
        for my $pair ([$mname, 'map'], [$dname, 'data']) {
            my ($cname, $key) = @$pair;
            my ($body) = $src =~ /const \Q$cname\E = ""(.*?)(?=\nconst |\nvar |\z)/s;
            die "$file: cannot find $cname\n" unless defined $body;
            $body =~ s{\s*//[^\n]*}{}g;   # the per-glyph comments may contain a bare quote (the '"' glyph)
            my $bytes = '';
            while ($body =~ /"((?:\\.|[^"\\])*)"/g) {
                my $part = $1;
                $part =~ s/\\x([0-9a-fA-F]{2})|\\([\\"'nrt0])/
                    defined $1 ? chr(hex $1)
                    : $2 eq 'n' ? "\n" : $2 eq 'r' ? "\r" : $2 eq 't' ? "\t" : $2 eq '0' ? "\0" : $2/gex;
                $bytes .= $part;
            }
            $f{$key} = [map { ord } split //, $bytes];
        }
        die "$file: OffsetMap not a multiple of 6\n" if @{ $f{map} } % 6;
        $f{nmap} = @{ $f{map} } / 6;
        $f{yadv} = $yadv // 13;
        $f{first} = $f{last} = 0;
        push @fonts, \%f;
        printf STDERR "%-22s %4d glyphs (sparse), %6d data bytes, yadv %d\n", $name, $f{nmap}, scalar @{ $f{data} }, $f{yadv};
        next;
    }
    if ($file =~ /\.go$/) {
        # tinyfont: Glyph{Rune: 33, Width: 0x8, Height: 0x15, XAdvance: 0xd, XOffset: 1, YOffset: -21, Bitmaps: []uint8{...}}
        my @g;
        while ($src =~ /Glyph\{Rune:\s*(\d+),\s*Width:\s*([^,]+),\s*Height:\s*([^,]+),\s*XAdvance:\s*([^,]+),\s*XOffset:\s*([^,]+),\s*YOffset:\s*([^,]+),\s*Bitmaps:\s*\[\]uint8\{([^}]*)\}/g) {
            my ($rune, $w, $h, $adv, $xo, $yo, $bits) = ($1, nums($2), nums($3), nums($4), nums($5), nums($6), $7);
            push @g, { rune => $rune, w => $w, h => $h, adv => $adv, xo => $xo, yo => $yo, bits => [nums($bits)] };
        }
        die "$file: no glyphs found\n" unless @g;
        @g = sort { $a->{rune} <=> $b->{rune} } @g;
        $f{first} = $g[0]{rune}; $f{last} = $g[-1]{rune};
        my %by = map { $_->{rune} => $_ } @g;
        my ($yadv) = $src =~ /YAdvance:\s*([^,\s}]+)/;
        my @bbox = $src =~ /BBox:\s*\[4\]int8\{([^}]*)\}/ ? nums($1) : ();
        # tinyfont fonts carry no line advance; use the bounding-box height (BBox = w, h, x, y)
        $f{yadv} = defined $yadv ? (nums($yadv))[0] : (@bbox ? $bbox[1] + 2 : 8);
        for my $r ($f{first} .. $f{last}) {
            my $gl = $by{$r} || { w => 0, h => 0, adv => $by{$f{first}}{adv}, xo => 0, yo => 0, bits => [] };
            push @{ $f{glyphs} }, [ scalar @{ $f{bitmap} }, $gl->{w}, $gl->{h}, $gl->{adv}, $gl->{xo}, $gl->{yo} ];
            push @{ $f{bitmap} }, @{ $gl->{bits} };
        }
    }
    elsif ($file =~ /\.h$/) {
        # Adafruit GFX: Bitmaps[] then Glyphs[] {off,w,h,adv,xo,yo} then GFXfont { bitmaps, glyphs, first, last, yAdvance }
        my ($bm) = $src =~ /Bitmaps\[\]\s*PROGMEM\s*=\s*\{([^}]*)\}/s or die "$file: no Bitmaps[]\n";
        my ($gs) = $src =~ /Glyphs\[\]\s*PROGMEM\s*=\s*\{(.*?)\};/s or die "$file: no Glyphs[]\n";
        my ($hdr) = $src =~ /GFXfont\s+\w+\s*PROGMEM\s*=\s*\{(.*?)\};/s or die "$file: no GFXfont\n";
        $f{bitmap} = [nums($bm)];
        while ($gs =~ /\{([^}]*)\}/g) { my @v = nums($1); push @{ $f{glyphs} }, [ @v[0 .. 5] ] if @v >= 6 }
        my @h = nums($hdr);          # (pointers are not numbers) first, last, yAdvance
        ($f{first}, $f{last}, $f{yadv}) = @h[-3, -2, -1];
        die "$file: glyph count mismatch\n" unless @{ $f{glyphs} } == $f{last} - $f{first} + 1;
    }
    elsif ($file =~ /\.c$/) {
        # classic 5x7: 256 chars x 5 column bytes, bit n of column = row n. Convert to row bitmaps.
        my ($tbl) = $src =~ /font\[\]\s*PROGMEM\s*=\s*\{(.*?)\};/s or die "$file: no font[]\n";
        my @b = nums($tbl);
        die "$file: expected 1280 bytes, got " . scalar(@b) . "\n" unless @b >= 1280;
        $f{first} = 32; $f{last} = 126; $f{yadv} = 8;
        for my $c (32 .. 126) {
            my @col = @b[$c * 5 .. $c * 5 + 4];
            my @rows;
            for my $y (0 .. 6) { my $r = 0; for my $x (0 .. 4) { $r |= (1 << (7 - $x)) if ($col[$x] >> $y) & 1 } push @rows, $r }
            # 5 wide x 7 high, packed continuously (35 bits -> 5 bytes)
            my $bits = '';
            for my $r (@rows) { $bits .= substr(unpack('B8', chr($r)), 0, 5) }
            $bits .= '0' x ((8 - length($bits) % 8) % 8);
            my @bytes = map { ord } split //, pack('B*', $bits);
            push @{ $f{glyphs} }, [ scalar @{ $f{bitmap} }, 5, 7, 6, 0, -7 ];
            push @{ $f{bitmap} }, @bytes;
        }
    }
    else { die "$file: unknown font format\n" }
    push @fonts, \%f;
    printf STDERR "%-22s %4d glyphs, %6d bitmap bytes, %d..%d, yadv %d\n", $name, scalar @{ $f{glyphs} }, scalar @{ $f{bitmap} }, $f{first}, $f{last}, $f{yadv};
}

open my $o, '>', $out or die "cannot write $out: $!";
print $o "/* generated by tools/gfxfont.pl - do not edit. Fonts: ", join(", ", map { $_->{name} } @fonts), " */\n";
print $o "#include \"pp_gfx.h\"\n\n";
for my $f (@fonts) {
    if ($f->{map}) {
        for my $pair ([map => 'map'], [data => 'dt']) {
            my ($key, $pfx) = @$pair;
            print $o "static const uint8_t ${pfx}_$f->{name}\[\] = {";
            my $a = $f->{$key};
            for my $i (0 .. $#$a) { print $o "\n   " if $i % 16 == 0; printf $o " 0x%02X,", $a->[$i] }
            print $o "\n};\n";
        }
        print $o "\n";
        next;
    }
    print $o "static const uint8_t bm_$f->{name}\[\] = {";
    for my $i (0 .. $#{ $f->{bitmap} }) { print $o "\n   " if $i % 16 == 0; printf $o " 0x%02X,", $f->{bitmap}[$i] }
    print $o "\n};\n";
    print $o "static const pp_glyph gl_$f->{name}\[\] = {\n";
    for my $g (@{ $f->{glyphs} }) { printf $o "    { %5d, %3d, %3d, %3d, %4d, %4d },\n", @$g }
    print $o "};\n\n";
}
print $o "const pp_font pp_fonts[] = {\n";
for my $f (@fonts) {
    if ($f->{map}) { printf $o "    { \"%s\", NULL, NULL, 0, 0, %d, map_%s, dt_%s, %d },\n", $f->{name}, $f->{yadv}, $f->{name}, $f->{name}, $f->{nmap} }
    else { printf $o "    { \"%s\", bm_%s, gl_%s, %d, %d, %d, NULL, NULL, 0 },\n", $f->{name}, $f->{name}, $f->{name}, $f->{first}, $f->{last}, $f->{yadv} }
}
print $o "};\nconst int pp_nfonts = ", scalar(@fonts), ";\n";
close $o;
print STDERR "wrote $out\n";
