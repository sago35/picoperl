/* Musical note constants for the Tone module, computed from the name instead of
 * stored: "Tone::C5" -> 72 (MIDI note number, A4 = 69 = 440 Hz). Names follow
 * tinygo.org/x/drivers/tone: letter A..G, optional S for sharp, octave 0..9. */
#include <string.h>
#include "picoperl.h"

int pp_tone_const(const char *name, pp_val *out) {
    static const signed char semitone[] = { 9, 11, 0, 2, 4, 5, 7 };   /* A B C D E F G */
    const char *p;
    if (strncmp(name, "Tone::", 6) == 0) p = name + 6;
    else if (strncmp(name, "Midi::", 6) == 0) p = name + 6;   /* Midi::C4 = 60, like tinygo's midi.C4 */
    else return 0;
    if (*p < 'A' || *p > 'G') return 0;
    int n = semitone[*p++ - 'A'];
    if (*p == 'S') { n++; p++; }
    if (*p < '0' || *p > '9' || p[1]) return 0;
    n += 12 * (*p - '0' + 1);
    if (n > 127) return 0;
    *out = pp_int(n);
    return 1;
}
