/* Small backtracking regular-expression engine for picoperl.
 *
 * Supports the everyday Perl syntax: literals, ., classes ([...], \d \w \s and
 * POSIX [:alpha:]), anchors (^ $ \A \z \Z \b \B), groups ( ) (?: ) (?= ) (?! ),
 * inline flags (?i) (?^i: ), alternation, quantifiers * + ? {n,m} (greedy and
 * lazy), backreferences \1.., and the i/m/s/x modifiers. Strings are bytes.
 *
 * Matching runs on an explicit heap-allocated backtrack stack, never on the C
 * stack (except one level per nested lookahead), so it is safe on the MCU. */
#ifndef PP_REGEX_H
#define PP_REGEX_H
#include <stdint.h>

enum { PP_RE_I = 1, PP_RE_M = 2, PP_RE_S = 4, PP_RE_X = 8 };

typedef struct pp_regex {
    int32_t *prog;      /* bytecode */
    int      nprog;
    uint8_t *classes;   /* 32-byte bitmaps */
    int      nclasses;
    int      ncap;      /* capture groups, not counting group 0 */
    struct { uint16_t off, len, cap; } *names;   /* (?<name>...): name is source[off..off+len) */
    int      nnames;
    int      nmarks;    /* loop-progress slots after the capture slots */
    int      flags;     /* PP_RE_* the pattern was compiled with */
    int      anchored;  /* 1: only position 0 can match; 2: starts with \G, only the start position can */
    int      first;     /* literal first byte, or -1 */
    char    *source;    /* pattern text, for stringification */
    int      srclen;
} pp_regex;

/* Compile; returns NULL and fills err (errcap bytes) on a syntax error. */
pp_regex *pp_re_compile(const char *pat, int len, int flags, char *err, int errcap);
void      pp_re_free(pp_regex *re);
/* Number of int slots the caller must provide for caps: offsets of group k are
 * caps[2k], caps[2k+1] (-1 when the group did not take part). */
int       pp_re_nslots(const pp_regex *re);
/* Search for a match starting at or after `start`. When notempty_at >= 0, an
 * empty match at that position is rejected (used to advance /g loops).
 * Returns 1 (match, caps filled), 0 (no match), -1 (backtrack limit exceeded). */
int       pp_re_exec(const pp_regex *re, const char *s, int len, int start, int notempty_at, int *caps);

#ifndef PP_RE_MAX_STACK
#define PP_RE_MAX_STACK 100000       /* backtrack entries (16 bytes each); the MCU build lowers this */
#endif
#ifndef PP_RE_MAX_STEPS
#define PP_RE_MAX_STEPS 5000000      /* VM steps per match attempt before giving up (catastrophic backtracking) */
#endif
#endif
