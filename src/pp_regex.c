/* Regular expressions: parser -> small AST -> bytecode -> backtracking VM.
 * See pp_regex.h for the supported syntax. */
#include "pp_regex.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/* AST                                                                 */
/* ------------------------------------------------------------------ */

enum {
    R_EMPTY, R_CHAR, R_ANY, R_CLASS, R_BOL, R_EOL, R_BOS, R_EOS, R_EOSNL, R_WORDB, R_NWORDB,
    R_GROUP, R_CAT, R_ALT, R_REPEAT, R_BACKREF, R_LOOK,
};

typedef struct rnode {
    uint8_t type, flags, greedy, neg;
    int16_t cap;                 /* R_GROUP: group number, -1 = non-capturing */
    int     c;                   /* char / class index / backref number */
    int     min, max;            /* R_REPEAT (max -1 = unbounded) */
    struct rnode *child, *next;  /* children (linked through next) */
    struct rnode *all;           /* allocation chain */
} rnode;

/* bytecode */
enum {
    I_CHAR = 1, I_CHARI, I_ANY, I_ANYNL, I_CLASS,
    I_BOL, I_MBOL, I_EOL, I_MEOL, I_BOS, I_EOS, I_EOSNL, I_WORDB, I_NWORDB,
    I_SPLIT, I_JMP, I_SAVE, I_MATCH, I_BREF, I_BREFI,
    I_REP,          /* kind arg min max greedy : one single-byte atom repeated */
    I_MARK, I_CHECK,/* loop progress guard */
    I_LOOK,         /* neg end : sub-program follows, ends with I_MATCH */
    I_LOOKB,        /* neg end min max : lookbehind; the sub-program must end where we stand */
};

typedef struct {
    const char *p, *end;
    int   flags;
    int   ncap;
    int   nmarks;
    rnode *all;
    const char *base;                            /* start of the pattern (for name offsets) */
    struct { int off, len, cap; } names[16]; int nnames;
    uint8_t *classes; int nclasses, capclasses;
    char  err[96];
    int   failed;
} rparser;

/* recursive helpers stay one copy each: inlined or cloned per caller they cost kilobytes of ROM */
#if defined(__GNUC__) && !defined(__clang__)
#define RE_FN __attribute__((noinline, noclone))
#else
#define RE_FN __attribute__((noinline))          /* clang (the wasm build) has no noclone */
#endif
static rnode *rnew(rparser *P, int type);
/* the fewest and the most bytes a node can match; 0 when there is no upper bound */
RE_FN static int width(const struct rnode *n, int *lo, int *hi) {
    int a = 0, b = 0;
    *lo = *hi = 0;
    switch (n->type) {
    case R_CHAR: case R_ANY: case R_CLASS: *lo = *hi = 1; return 1;
    case R_BACKREF: return 0;
    case R_LOOK: return n->neg == 2 ? width(n->child, lo, hi) : 1;   /* an atomic group has a width, a lookaround none */
    case R_GROUP: return width(n->child, lo, hi);
    case R_CAT:
        for (const struct rnode *c = n->child; c; c = c->next) { if (!width(c, &a, &b)) return 0; *lo += a; *hi += b; if (*hi > 100000) return 0; }
        return 1;
    case R_ALT:
        *lo = -1;
        for (const struct rnode *c = n->child; c; c = c->next) { if (!width(c, &a, &b)) return 0; if (*lo < 0 || a < *lo) *lo = a; if (b > *hi) *hi = b; }
        if (*lo < 0) *lo = 0;
        return 1;
    case R_REPEAT:
        if (!width(n->child, &a, &b)) return 0;
        if (n->max < 0) { if (b > 0) return 0; return 1; }
        if (b > 100000 || n->max > 100000) return 0;
        *lo = a * n->min; *hi = b * n->max;
        return *hi <= 100000;
    default: return 1;   /* anchors and the empty node */
    }
}

static rnode *rnew(rparser *P, int type) {
    rnode *n = calloc(1, sizeof *n);
    if (!n) { P->failed = 1; snprintf(P->err, sizeof P->err, "out of memory"); return NULL; }
    n->type = (uint8_t)type; n->flags = (uint8_t)P->flags; n->cap = -1; n->greedy = 1; n->max = -1;
    n->all = P->all; P->all = n;
    return n;
}
static void rfail(rparser *P, const char *msg) {
    if (!P->failed) { P->failed = 1; snprintf(P->err, sizeof P->err, "%s", msg); }
}
static int new_class(rparser *P) {
    if (P->nclasses == P->capclasses) {
        P->capclasses = P->capclasses ? P->capclasses * 2 : 4;
        P->classes = realloc(P->classes, (size_t)P->capclasses * 32);
    }
    memset(P->classes + P->nclasses * 32, 0, 32);
    return P->nclasses++;
}
static void class_set(uint8_t *cls, int c) { cls[(c & 255) >> 3] |= (uint8_t)(1 << (c & 7)); }
static int  class_has(const uint8_t *cls, int c) { return (cls[(c & 255) >> 3] >> (c & 7)) & 1; }
static void class_range(uint8_t *cls, int lo, int hi) { for (int c = lo; c <= hi; c++) class_set(cls, c); }
/* Unicode properties, for the bytes of ASCII: \pL \p{Lower} \PM \p{^Space} ...
   Strings are bytes here, so only ASCII characters have properties; a byte >= 0x80 (part of
   a UTF-8 character) matches none of them. */
static const struct { const char *name; char kind; } uprops[] = {
    { "L", 'a' }, { "Letter", 'a' }, { "Alpha", 'a' }, { "Alphabetic", 'a' }, { "IsAlpha", 'a' }, { "XPosixAlpha", 'a' }, { "PosixAlpha", 'a' },
    { "Lu", 'u' }, { "Upper", 'u' }, { "Uppercase", 'u' }, { "IsUpper", 'u' }, { "Uppercase_Letter", 'u' }, { "XPosixUpper", 'u' },
    { "Ll", 'l' }, { "Lower", 'l' }, { "Lowercase", 'l' }, { "IsLower", 'l' }, { "Lowercase_Letter", 'l' }, { "XPosixLower", 'l' },
    { "N", 'd' }, { "Nd", 'd' }, { "Digit", 'd' }, { "Number", 'd' }, { "IsDigit", 'd' }, { "XPosixDigit", 'd' }, { "Decimal_Number", 'd' },
    { "M", 'm' }, { "Mn", 'm' }, { "Mc", 'm' }, { "Me", 'm' }, { "Mark", 'm' },
    /* categories with no ASCII character: they match nothing here */
    { "Zl", 'm' }, { "Zp", 'm' }, { "Line_Separator", 'm' }, { "Paragraph_Separator", 'm' },
    { "Lt", 'm' }, { "Lm", 'm' }, { "Lo", 'm' }, { "Nl", 'm' }, { "No", 'm' }, { "Cf", 'm' }, { "Cs", 'm' }, { "Co", 'm' }, { "Cn", 'm' },
    { "P", 'p' }, { "Punct", 'p' }, { "Punctuation", 'p' }, { "IsPunct", 'p' },
    { "S", 'y' }, { "Symbol", 'y' },
    { "Z", 'z' }, { "Zs", 'z' }, { "Space_Separator", 'z' },
    { "Space", 's' }, { "IsSpace", 's' }, { "White_Space", 's' }, { "XPosixSpace", 's' }, { "SpacePerl", 's' }, { "XPerlSpace", 's' },
    { "Word", 'w' }, { "IsWord", 'w' }, { "W", 'w' }, { "XPosixWord", 'w' }, { "PerlWord", 'w' },
    { "Alnum", 'n' }, { "IsAlnum", 'n' }, { "XPosixAlnum", 'n' },
    { "XDigit", 'x' }, { "IsXDigit", 'x' }, { "Hex", 'x' }, { "AHex", 'x' }, { "Hex_Digit", 'x' }, { "ASCII_Hex_Digit", 'x' },
    { "C", 'c' }, { "Cc", 'c' }, { "Cntrl", 'c' }, { "Control", 'c' }, { "IsCntrl", 'c' },
    { "ASCII", 'A' }, { "IsASCII", 'A' }, { "Any", '*' }, { "All", '*' },
    { "Print", 'P' }, { "IsPrint", 'P' }, { "Graph", 'g' }, { "IsGraph", 'g' }, { "Blank", 'b' }, { "HorizSpace", 'b' },
    { NULL, 0 }
};
static void class_property(uint8_t *cls, int kind, int negate) {
    uint8_t tmp[32]; memset(tmp, 0, 32);
    for (int c = 0; c < 128; c++) {
        int in;
        switch (kind) {
        case 'a': in = isalpha(c); break;
        case 'u': in = isupper(c); break;
        case 'l': in = islower(c); break;
        case 'd': in = isdigit(c); break;
        case 'm': in = 0; break;
        case 'y': in = c && strchr("$+<=>^`|~", c) != NULL; break;
        case 'p': in = ispunct(c) && !strchr("$+<=>^`|~", c); break;
        case 'z': in = c == ' '; break;
        case 's': in = isspace(c); break;
        case 'w': in = isalnum(c) || c == '_'; break;
        case 'n': in = isalnum(c); break;
        case 'x': in = isxdigit(c); break;
        case 'c': in = iscntrl(c); break;
        case 'P': in = isprint(c); break;
        case 'g': in = isgraph(c); break;
        case 'b': in = c == ' ' || c == '\t'; break;
        default:  in = 1; break;             /* 'A' ASCII, '*' Any (bytes >= 0x80 added below) */
        }
        if (in) class_set(tmp, c);
    }
    if (kind == '*') for (int c = 128; c < 256; c++) class_set(tmp, c);
    if (negate) for (int i = 0; i < 32; i++) cls[i] |= (uint8_t)~tmp[i];
    else for (int i = 0; i < 32; i++) cls[i] |= tmp[i];
}
static void class_named(uint8_t *cls, int letter) {
    /* \d \w \s and their negations; a property from parse_escape: 0x100 | negate << 7 | kind */
    if (letter & 0x100) { class_property(cls, letter & 0x7F, (letter & 0x80) != 0); return; }
    uint8_t tmp[32]; memset(tmp, 0, 32);
    switch (tolower(letter)) {
    case 'd': class_range(tmp, '0', '9'); break;
    case 'w': class_range(tmp, '0', '9'); class_range(tmp, 'a', 'z'); class_range(tmp, 'A', 'Z'); class_set(tmp, '_'); break;
    case 's': class_set(tmp, ' '); class_set(tmp, '\t'); class_set(tmp, '\n'); class_set(tmp, '\r'); class_set(tmp, '\f'); class_set(tmp, '\v'); break;
    }
    if (isupper(letter)) for (int i = 0; i < 32; i++) cls[i] |= (uint8_t)~tmp[i];
    else for (int i = 0; i < 32; i++) cls[i] |= tmp[i];
}
static int class_posix(uint8_t *cls, const char *name, int len) {
    int (*fn)(int) = NULL;
    if (len == 5 && !memcmp(name, "alpha", 5)) fn = isalpha;
    else if (len == 5 && !memcmp(name, "digit", 5)) fn = isdigit;
    else if (len == 5 && !memcmp(name, "alnum", 5)) fn = isalnum;
    else if (len == 5 && !memcmp(name, "space", 5)) fn = isspace;
    else if (len == 5 && !memcmp(name, "upper", 5)) fn = isupper;
    else if (len == 5 && !memcmp(name, "lower", 5)) fn = islower;
    else if (len == 5 && !memcmp(name, "punct", 5)) fn = ispunct;
    else if (len == 5 && !memcmp(name, "print", 5)) fn = isprint;
    else if (len == 5 && !memcmp(name, "graph", 5)) fn = isgraph;
    else if (len == 5 && !memcmp(name, "cntrl", 5)) fn = iscntrl;
    else if (len == 5 && !memcmp(name, "blank", 5)) fn = isblank;
    else if (len == 6 && !memcmp(name, "xdigit", 6)) fn = isxdigit;
    else if (len == 4 && !memcmp(name, "word", 4)) { class_named(cls, 'w'); return 1; }
    else if (len == 5 && !memcmp(name, "ascii", 5)) { class_range(cls, 0, 127); return 1; }
    if (!fn) return 0;
    for (int c = 0; c < 128; c++) if (fn(c)) class_set(cls, c);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Parser                                                              */
/* ------------------------------------------------------------------ */

static rnode *parse_alt(rparser *P, int depth);

static int hexv(int c) { return isdigit(c) ? c - '0' : tolower(c) - 'a' + 10; }

/* skip whitespace and comments under /x */
static void skip_x(rparser *P) {
    if (!(P->flags & PP_RE_X)) return;
    for (;;) {
        while (P->p < P->end && isspace((unsigned char)*P->p)) P->p++;
        if (P->p < P->end && *P->p == '#') { while (P->p < P->end && *P->p != '\n') P->p++; continue; }
        break;
    }
}

/* parse an escape after the backslash; returns a char code, or -1 with *cls set to a class letter */
static int parse_escape(rparser *P, int *cls) {
    *cls = 0;
    if (P->p >= P->end) { rfail(P, "trailing \\ in regex"); return 0; }
    int c = (unsigned char)*P->p++;
    switch (c) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case 'f': return '\f';
    case 'e': return 27;
    case 'a': return 7;
    case '0': { int v = 0, k = 0; while (k < 2 && P->p < P->end && *P->p >= '0' && *P->p <= '7') { v = v * 8 + (*P->p - '0'); P->p++; k++; } return v; }
    case 'x': {
        int v = 0;
        if (P->p < P->end && *P->p == '{') { P->p++; while (P->p < P->end && *P->p != '}') { v = v * 16 + hexv((unsigned char)*P->p); P->p++; } if (P->p < P->end) P->p++; }
        else { int k = 0; while (k < 2 && P->p < P->end && isxdigit((unsigned char)*P->p)) { v = v * 16 + hexv((unsigned char)*P->p); P->p++; k++; } }
        return v & 255;
    }
    case 'd': case 'D': case 'w': case 'W': case 's': case 'S': *cls = c; return -1;
    case 'p': case 'P': {
        const char *name; int len, negate = c == 'P';
        if (P->p < P->end && *P->p == '{') {
            name = ++P->p;
            while (P->p < P->end && *P->p != '}') P->p++;
            if (P->p >= P->end) { rfail(P, "missing } in \\p{...}"); return 0; }
            len = (int)(P->p - name);
            P->p++;
            if (len && *name == '^') { negate = !negate; name++; len--; }
        } else {
            if (P->p >= P->end) { rfail(P, "empty \\p in regex"); return 0; }
            name = P->p++; len = 1;
        }
        for (int i = 0; uprops[i].name; i++)
            if ((int)strlen(uprops[i].name) == len && !memcmp(uprops[i].name, name, (size_t)len)) { *cls = 0x100 | (negate ? 0x80 : 0) | uprops[i].kind; return -1; }
        rfail(P, "Unicode property not supported in \\p{...}");
        return 0;
    }
    default: return c;    /* \. \\ \/ \$ ... : the character itself */
    }
}

static rnode *parse_class(rparser *P) {
    rnode *n = rnew(P, R_CLASS);
    if (!n) return NULL;
    int idx = new_class(P);
    uint8_t *cls = P->classes + idx * 32;
    int neg = 0;
    if (P->p < P->end && *P->p == '^') { neg = 1; P->p++; }
    int first = 1;
    int prev = -1;    /* last single char, for ranges */
    while (P->p < P->end && (*P->p != ']' || first)) {
        first = 0;
        int c = (unsigned char)*P->p++;
        int is_single = 1;
        if (c == '[' && P->p < P->end && *P->p == ':') {
            const char *q = P->p + 1; const char *e = memchr(q, ':', (size_t)(P->end - q));
            if (e && e + 1 < P->end && e[1] == ']') {
                int pneg = 0; if (*q == '^') { pneg = 1; q++; }
                uint8_t tmp[32]; memset(tmp, 0, 32);
                if (!class_posix(tmp, q, (int)(e - q))) { rfail(P, "unknown POSIX class in regex"); return NULL; }
                for (int i = 0; i < 32; i++) cls[i] |= pneg ? (uint8_t)~tmp[i] : tmp[i];
                P->p = e + 2; prev = -1; continue;
            }
        }
        if (c == '\\') {
            int letter; c = parse_escape(P, &letter);
            if (P->failed) return NULL;
            if (c < 0) { class_named(cls, letter); is_single = 0; }
            else if (letter == 0 && c == 'b') c = '\b';
        }
        if (!is_single) { prev = -1; continue; }
        /* range a-z ? */
        if (P->p + 1 < P->end && *P->p == '-' && P->p[1] != ']') {
            P->p++;
            int hi = (unsigned char)*P->p++;
            if (hi == '\\') { int letter; hi = parse_escape(P, &letter); if (P->failed) return NULL; if (hi < 0) { rfail(P, "invalid range in character class"); return NULL; } }
            if (hi < c) { rfail(P, "Invalid [] range in regex"); return NULL; }
            class_range(cls, c, hi);
            prev = -1;
            continue;
        }
        class_set(cls, c);
        prev = c;
    }
    (void)prev;
    if (P->p >= P->end) { rfail(P, "Unmatched [ in regex"); return NULL; }
    P->p++;   /* ] */
    if (P->flags & PP_RE_I) for (int ch = 'a'; ch <= 'z'; ch++) if (class_has(cls, ch) || class_has(cls, toupper(ch))) { class_set(cls, ch); class_set(cls, toupper(ch)); }
    if (neg) for (int i = 0; i < 32; i++) cls[i] = (uint8_t)~cls[i];
    n->c = idx;
    return n;
}

static rnode *mkchar(rparser *P, int c) {
    rnode *n = rnew(P, R_CHAR);
    if (n) n->c = c & 255;
    return n;
}

static rnode *parse_atom(rparser *P, int depth) {
    int c = (unsigned char)*P->p++;
    rnode *n;
    switch (c) {
    case '.': return rnew(P, R_ANY);
    case '^': return rnew(P, R_BOL);
    case '$': return rnew(P, R_EOL);
    case '[': return parse_class(P);
    case '(': {
        int cap = -1, look = 0, neg = 0;
        int saved_flags = P->flags;
        if (P->p < P->end && *P->p == '?') {
            P->p++;
            if (P->p >= P->end) { rfail(P, "Sequence (? incomplete"); return NULL; }
            int k = (unsigned char)*P->p++;
            if (k == ':') ;
            else if (k == '=') look = 1;
            else if (k == '!') { look = 1; neg = 1; }
            else if (k == '>') { look = 1; neg = 2; }   /* (?>...): atomic, no backtracking into it */
            else if (k == '#') { while (P->p < P->end && *P->p != ')') P->p++; if (P->p < P->end) P->p++; return rnew(P, R_EMPTY); }
            else if (k == '<' && P->p < P->end && (*P->p == '=' || *P->p == '!')) { look = 1; neg = *P->p++ == '=' ? 3 : 4; }   /* lookbehind */
            else if ((k == '<' || k == '\'') && P->p < P->end && (isalpha((unsigned char)*P->p) || *P->p == '_')) {
                /* (?<name>...) / (?'name'...): a numbered group that also fills $+{name} */
                char close = k == '<' ? '>' : '\'';
                const char *ns = P->p;
                while (P->p < P->end && (isalnum((unsigned char)*P->p) || *P->p == '_')) P->p++;
                if (P->p >= P->end || *P->p != close) { rfail(P, "syntax error in (?<...>) group name"); return NULL; }
                if (P->nnames >= 16) { rfail(P, "too many named groups"); return NULL; }
                cap = ++P->ncap;
                P->names[P->nnames].off = (int)(ns - P->base);
                P->names[P->nnames].len = (int)(P->p - ns);
                P->names[P->nnames].cap = cap;
                P->nnames++;
                P->p++;                          /* '>' or the closing quote */
            }
            else {
                /* inline flags: (?i) (?^i:...) (?i-m:...) */
                P->p--;
                int on = 1, fl = P->flags;
                if (*P->p == '^') { fl = 0; P->p++; }
                while (P->p < P->end && *P->p != ')' && *P->p != ':') {
                    int f = *P->p++;
                    int bit = f == 'i' ? PP_RE_I : f == 'm' ? PP_RE_M : f == 's' ? PP_RE_S : f == 'x' ? PP_RE_X : (f == '-' || f == 'a' || f == 'u' || f == 'l' || f == 'd' || f == 'p') ? 0 : -1;   /* charset flags and /p change nothing here */
                    if (bit < 0) { rfail(P, "unknown flag in (?...)"); return NULL; }
                    if (f == '-') on = 0; else if (on) fl |= bit; else fl &= ~bit;
                }
                if (P->p >= P->end) { rfail(P, "Sequence (? incomplete"); return NULL; }
                if (*P->p == ')') { P->p++; P->flags = fl; return rnew(P, R_EMPTY); }   /* applies to the rest of the group */
                P->p++;   /* ':' */
                P->flags = fl;
            }
        } else cap = ++P->ncap;
        rnode *body = parse_alt(P, depth + 1);
        if (!body) return NULL;
        if (P->p >= P->end || *P->p != ')') { rfail(P, "Unmatched ( in regex"); return NULL; }
        P->p++;
        P->flags = saved_flags;
        n = rnew(P, look ? R_LOOK : R_GROUP);
        if (!n) return NULL;
        n->child = body; n->cap = (int16_t)cap; n->neg = (uint8_t)neg;
        if (neg >= 3) {   /* what a lookbehind matches must have a bounded length, as in perl */
            int lo = 0, hi = 0;
            if (!width(body, &lo, &hi) || hi > 255) { rfail(P, "Lookbehind longer than 255 not implemented"); return NULL; }
            n->min = lo; n->max = hi;
        }
        return n;
    }
    case '\\': {
        if (P->p >= P->end) { rfail(P, "trailing \\ in regex"); return NULL; }
        int d = (unsigned char)*P->p;
        if (d == 'b') { P->p++; return rnew(P, R_WORDB); }
        if (d == 'B') { P->p++; return rnew(P, R_NWORDB); }
        if (d == 'A') { P->p++; return rnew(P, R_BOS); }
        if (d == 'z') { P->p++; return rnew(P, R_EOS); }
        if (d == 'Z') { P->p++; return rnew(P, R_EOSNL); }
        if (d == 'G') { P->p++; return rnew(P, R_EMPTY); }
        if (d >= '1' && d <= '9') {
            int v = 0; while (P->p < P->end && isdigit((unsigned char)*P->p)) { v = v * 10 + (*P->p - '0'); P->p++; }
            n = rnew(P, R_BACKREF); if (n) n->c = v; return n;
        }
        int letter; int ch = parse_escape(P, &letter);
        if (P->failed) return NULL;
        if (ch < 0) { n = rnew(P, R_CLASS); if (!n) return NULL; int idx = new_class(P); class_named(P->classes + idx * 32, letter); n->c = idx; return n; }
        return mkchar(P, ch);
    }
    case '*': case '+': case '?': rfail(P, "Quantifier follows nothing in regex"); return NULL;
    case ')': rfail(P, "Unmatched ) in regex"); return NULL;
    default: return mkchar(P, c);
    }
}

static rnode *parse_cat(rparser *P, int depth) {
    rnode *head = NULL, *tail = NULL;
    for (;;) {
        skip_x(P);
        if (P->p >= P->end || *P->p == '|' || *P->p == ')') break;
        rnode *a = parse_atom(P, depth);
        if (!a) return NULL;
        skip_x(P);
        /* quantifier */
        if (P->p < P->end && (*P->p == '*' || *P->p == '+' || *P->p == '?' || *P->p == '{')) {
            int min = 0, max = -1, ok = 1;
            const char *q = P->p;
            if (*q == '*') { q++; }
            else if (*q == '+') { min = 1; q++; }
            else if (*q == '?') { max = 1; q++; }
            else {
                q++;
                if (q < P->end && isdigit((unsigned char)*q)) {
                    min = 0; while (q < P->end && isdigit((unsigned char)*q)) min = min * 10 + (*q++ - '0');
                    if (q < P->end && *q == ',') { q++; if (q < P->end && isdigit((unsigned char)*q)) { max = 0; while (q < P->end && isdigit((unsigned char)*q)) max = max * 10 + (*q++ - '0'); } else max = -1; }
                    else max = min;
                    if (q >= P->end || *q != '}') ok = 0; else q++;
                } else ok = 0;     /* a literal '{' */
            }
            if (ok) {
                if (a->type == R_BOL || a->type == R_EOL || a->type == R_BOS || a->type == R_EOS || a->type == R_WORDB || a->type == R_NWORDB || (a->type == R_LOOK && a->neg != 2)) { rfail(P, "Quantifier on an anchor or lookahead"); return NULL; }
                if (min > 1000 || max > 1000) { rfail(P, "Quantifier in {,} bigger than 1000 in regex"); return NULL; }
                if (max >= 0 && max < min) { rfail(P, "Can't do {n,m} with n > m in regex"); return NULL; }
                P->p = q;
                rnode *r = rnew(P, R_REPEAT);
                if (!r) return NULL;
                r->child = a; r->min = min; r->max = max;
                if (P->p < P->end && *P->p == '?') { r->greedy = 0; P->p++; }
                else if (P->p < P->end && *P->p == '+') { P->p++; }   /* possessive: treated as greedy */
                a = r;
            }
        }
        if (tail) tail->next = a; else head = a;
        tail = a;
    }
    if (!head) return rnew(P, R_EMPTY);
    if (!head->next) return head;
    rnode *cat = rnew(P, R_CAT);
    if (cat) cat->child = head;
    return cat;
}

static rnode *parse_alt(rparser *P, int depth) {
    if (depth > 64) { rfail(P, "regex nested too deeply"); return NULL; }
    rnode *first = parse_cat(P, depth);
    if (!first) return NULL;
    if (P->p >= P->end || *P->p != '|') return first;
    rnode *alt = rnew(P, R_ALT);
    if (!alt) return NULL;
    alt->child = first;
    rnode *tail = first;
    while (P->p < P->end && *P->p == '|') {
        P->p++;
        rnode *b = parse_cat(P, depth);
        if (!b) return NULL;
        tail->next = b; tail = b;
    }
    return alt;
}

/* ------------------------------------------------------------------ */
/* Compiler                                                            */
/* ------------------------------------------------------------------ */

typedef struct { int32_t *prog; int n, cap; int nmarks; int failed; } emitter;

static int emit(emitter *E, int32_t v) {
    if (E->n == E->cap) {
        E->cap = E->cap ? E->cap * 2 : 64;
        int32_t *np = realloc(E->prog, (size_t)E->cap * sizeof *np);
        if (!np) { E->failed = 1; return 0; }
        E->prog = np;
    }
    E->prog[E->n++] = v;
    return E->n - 1;
}

static int nullable(const rnode *n) {
    switch (n->type) {
    case R_CHAR: case R_ANY: case R_CLASS: return 0;
    case R_BACKREF: return 1;
    case R_GROUP: return nullable(n->child);
    case R_CAT: for (const rnode *c = n->child; c; c = c->next) if (!nullable(c)) return 0; return 1;
    case R_ALT: for (const rnode *c = n->child; c; c = c->next) if (nullable(c)) return 1; return 0;
    case R_REPEAT: return n->min == 0 || nullable(n->child);
    default: return 1;   /* anchors, lookahead, empty */
    }
}
static int single_atom(const rnode *n) { return n->type == R_CHAR || n->type == R_ANY || n->type == R_CLASS; }

static void gen(emitter *E, const rnode *n);

static void gen_atom_op(emitter *E, const rnode *n) {
    switch (n->type) {
    case R_CHAR: if (n->flags & PP_RE_I) { emit(E, I_CHARI); emit(E, tolower(n->c)); } else { emit(E, I_CHAR); emit(E, n->c); } break;
    case R_ANY: emit(E, (n->flags & PP_RE_S) ? I_ANYNL : I_ANY); emit(E, 0); break;
    case R_CLASS: emit(E, I_CLASS); emit(E, n->c); break;
    }
}

static void gen_repeat(emitter *E, const rnode *n) {
    const rnode *body = n->child;
    if (single_atom(body)) {
        /* I_REP kind arg min max greedy */
        emit(E, I_REP);
        int at = E->n;
        gen_atom_op(E, body);       /* two words: kind, arg */
        (void)at;
        emit(E, n->min); emit(E, n->max); emit(E, n->greedy);
        return;
    }
    int guard = nullable(body);
    for (int i = 0; i < n->min; i++) gen(E, body);
    if (n->max == -1) {
        /* L1: SPLIT L2, L3 ; L2: [MARK k] body [CHECK k] ; JMP L1 ; L3: */
        int mark = guard ? E->nmarks++ : -1;
        int l1 = emit(E, I_SPLIT); emit(E, 0); emit(E, 0);
        int l2 = E->n;
        if (guard) { emit(E, I_MARK); emit(E, mark); }
        gen(E, body);
        if (guard) { emit(E, I_CHECK); emit(E, mark); }
        emit(E, I_JMP); emit(E, l1);
        int l3 = E->n;
        if (E->failed) return;
        if (n->greedy) { E->prog[l1 + 1] = l2; E->prog[l1 + 2] = l3; }
        else { E->prog[l1 + 1] = l3; E->prog[l1 + 2] = l2; }
        return;
    }
    /* optional copies: SPLIT L1, L2 ; L1: body ; (next optional) ; L2: */
    int count = n->max - n->min;
    int *fix = count ? malloc((size_t)count * sizeof(int)) : NULL;
    for (int i = 0; i < count; i++) {
        fix[i] = emit(E, I_SPLIT); emit(E, 0); emit(E, 0);
        gen(E, body);
    }
    int end = E->n;
    if (!E->failed) for (int i = 0; i < count; i++) {
        int l1 = fix[i] + 3;
        if (n->greedy) { E->prog[fix[i] + 1] = l1; E->prog[fix[i] + 2] = end; }
        else { E->prog[fix[i] + 1] = end; E->prog[fix[i] + 2] = l1; }
    }
    free(fix);
}

static void gen(emitter *E, const rnode *n) {
    if (E->failed) return;
    switch (n->type) {
    case R_EMPTY: break;
    case R_CHAR: case R_ANY: case R_CLASS: gen_atom_op(E, n); break;
    case R_BOL: emit(E, (n->flags & PP_RE_M) ? I_MBOL : I_BOL); emit(E, 0); break;
    case R_EOL: emit(E, (n->flags & PP_RE_M) ? I_MEOL : I_EOL); emit(E, 0); break;
    case R_BOS: emit(E, I_BOS); emit(E, 0); break;
    case R_EOS: emit(E, I_EOS); emit(E, 0); break;
    case R_EOSNL: emit(E, I_EOSNL); emit(E, 0); break;
    case R_WORDB: emit(E, I_WORDB); emit(E, 0); break;
    case R_NWORDB: emit(E, I_NWORDB); emit(E, 0); break;
    case R_BACKREF: emit(E, (n->flags & PP_RE_I) ? I_BREFI : I_BREF); emit(E, n->c); break;
    case R_GROUP:
        if (n->cap >= 0) { emit(E, I_SAVE); emit(E, 2 * n->cap); }
        gen(E, n->child);
        if (n->cap >= 0) { emit(E, I_SAVE); emit(E, 2 * n->cap + 1); }
        break;
    case R_CAT: for (const rnode *c = n->child; c; c = c->next) gen(E, c); break;
    case R_ALT: {
        int nalt = 0; for (const rnode *c = n->child; c; c = c->next) nalt++;
        int *jmps = malloc((size_t)nalt * sizeof(int)); int nj = 0;
        for (const rnode *c = n->child; c; c = c->next) {
            if (c->next) {
                int sp = emit(E, I_SPLIT); emit(E, sp + 3); emit(E, 0);
                gen(E, c);
                jmps[nj++] = emit(E, I_JMP); emit(E, 0);
                if (!E->failed) E->prog[sp + 2] = E->n;
            } else gen(E, c);
        }
        if (!E->failed) for (int i = 0; i < nj; i++) E->prog[jmps[i] + 1] = E->n;
        free(jmps);
        break;
    }
    case R_REPEAT: gen_repeat(E, n); break;
    case R_LOOK: {
        if (n->neg >= 3) {   /* lookbehind: neg end min max, then the sub-program */
            int at = emit(E, I_LOOKB); emit(E, n->neg == 4); emit(E, 0); emit(E, n->min); emit(E, n->max);
            gen(E, n->child);
            emit(E, I_MATCH); emit(E, 0);
            if (!E->failed) E->prog[at + 2] = E->n;
            break;
        }
        int at = emit(E, I_LOOK); emit(E, n->neg); emit(E, 0);
        gen(E, n->child);
        emit(E, I_MATCH); emit(E, 0);
        if (!E->failed) E->prog[at + 2] = E->n;
        break;
    }
    }
}

pp_regex *pp_re_compile(const char *pat, int len, int flags, char *err, int errcap) {
    rparser P; memset(&P, 0, sizeof P);
    P.p = pat; P.end = pat + len; P.base = pat;
    P.flags = flags & (PP_RE_I | PP_RE_M | PP_RE_S | PP_RE_X);
    rnode *root = parse_alt(&P, 0);
    if (root && !P.failed && P.p < P.end) rfail(&P, *P.p == ')' ? "Unmatched ) in regex" : "syntax error in regex");
    emitter E; memset(&E, 0, sizeof E);
    if (root && !P.failed) { gen(&E, root); emit(&E, I_MATCH); emit(&E, 0); }
    for (rnode *n = P.all; n; ) { rnode *nx = n->all; free(n); n = nx; }
    if (P.failed || E.failed) {
        if (err) snprintf(err, (size_t)errcap, "%s", P.failed ? P.err : "out of memory");
        free(E.prog); free(P.classes);
        return NULL;
    }
    pp_regex *re = calloc(1, sizeof *re);
    re->prog = E.prog; re->nprog = E.n;
    re->classes = P.classes; re->nclasses = P.nclasses;
    re->ncap = P.ncap; re->nmarks = E.nmarks; re->flags = flags;
    re->source = malloc((size_t)len + 1); memcpy(re->source, pat, (size_t)len); re->source[len] = 0; re->srclen = len;
    if (P.nnames) {
        re->names = calloc((size_t)P.nnames, sizeof *re->names);
        for (int i = 0; i < P.nnames; i++) {
            re->names[i].off = (uint16_t)P.names[i].off;
            re->names[i].len = (uint16_t)P.names[i].len;
            re->names[i].cap = (uint16_t)P.names[i].cap;
        }
        re->nnames = P.nnames;
    }
    /* cheap optimisations: anchored start, literal first byte */
    re->first = -1;
    int pc = 0;
    while (re->prog[pc] == I_SAVE) pc += 2;
    if (re->prog[pc] == I_BOL || re->prog[pc] == I_BOS) re->anchored = 1;
    else if (re->prog[pc] == I_CHAR) re->first = re->prog[pc + 1];
    else if (re->prog[pc] == I_REP && re->prog[pc + 1] == I_CHAR && re->prog[pc + 3] >= 1) re->first = re->prog[pc + 2];
    return re;
}

void pp_re_free(pp_regex *re) {
    if (!re) return;
    free(re->prog); free(re->classes); free(re->source); free(re->names); free(re);
}

int pp_re_nslots(const pp_regex *re) { return 2 * (re->ncap + 1) + re->nmarks; }

/* ------------------------------------------------------------------ */
/* Matcher                                                             */
/* ------------------------------------------------------------------ */

enum { K_ALT = 0, K_RESTORE, K_REP, K_REPL };
typedef struct { int32_t pc, sp, n; uint8_t kind; } bt_entry;
typedef struct { bt_entry *v; int n, cap; int overflow; } bt_stack;

static int bt_push(bt_stack *st, int kind, int pc, int sp, int n) {
    if (st->n == st->cap) {
        if (st->cap >= PP_RE_MAX_STACK) { st->overflow = 1; return 0; }
        int nc = st->cap ? st->cap * 2 : 32;
        if (nc > PP_RE_MAX_STACK) nc = PP_RE_MAX_STACK;
        bt_entry *nv = realloc(st->v, (size_t)nc * sizeof *nv);
        if (!nv) { st->overflow = 1; return 0; }
        st->v = nv; st->cap = nc;
    }
    bt_entry *e = &st->v[st->n++];
    e->kind = (uint8_t)kind; e->pc = pc; e->sp = sp; e->n = n;
    return 1;
}

static int is_word(int c) { return isalnum(c) || c == '_'; }

static int atom_ok(const pp_regex *re, int kind, int arg, const unsigned char *s, int sp, int len) {
    if (sp >= len) return 0;
    int c = s[sp];
    switch (kind) {
    case I_CHAR: return c == arg;
    case I_CHARI: return tolower(c) == arg;
    case I_ANY: return c != '\n';
    case I_ANYNL: return 1;
    case I_CLASS: return class_has(re->classes + arg * 32, c);
    }
    return 0;
}

/* run the program from pc0 at position sp0; returns 1/0/-1 (overflow) */
RE_FN static int run(const pp_regex *re, const unsigned char *s, int len, int pc0, int sp0, int start, int notempty_at, int end_at, int *caps, int depth, long *steps) {
    const int32_t *prog = re->prog;
    bt_stack st = {0};
    int result = 0;
    int pc = pc0, sp = sp0;
    for (;;) {
        int fail = 0;
        for (;;) {
            if (++*steps > PP_RE_MAX_STEPS) { result = -1; goto done; }
            switch (prog[pc]) {
            case I_CHAR:  if (sp < len && s[sp] == prog[pc + 1]) { sp++; pc += 2; } else fail = 1; break;
            case I_CHARI: if (sp < len && tolower(s[sp]) == prog[pc + 1]) { sp++; pc += 2; } else fail = 1; break;
            case I_ANY:   if (sp < len && s[sp] != '\n') { sp++; pc += 2; } else fail = 1; break;
            case I_ANYNL: if (sp < len) { sp++; pc += 2; } else fail = 1; break;
            case I_CLASS: if (sp < len && class_has(re->classes + prog[pc + 1] * 32, s[sp])) { sp++; pc += 2; } else fail = 1; break;
            case I_BOL:   if (sp == 0) pc += 2; else fail = 1; break;
            case I_MBOL:  if (sp == 0 || s[sp - 1] == '\n') pc += 2; else fail = 1; break;
            case I_EOL:   if (sp == len || (sp == len - 1 && s[sp] == '\n')) pc += 2; else fail = 1; break;
            case I_MEOL:  if (sp == len || s[sp] == '\n') pc += 2; else fail = 1; break;
            case I_BOS:   if (sp == 0) pc += 2; else fail = 1; break;
            case I_EOS:   if (sp == len) pc += 2; else fail = 1; break;
            case I_EOSNL: if (sp == len || (sp == len - 1 && s[sp] == '\n')) pc += 2; else fail = 1; break;
            case I_WORDB: case I_NWORDB: {
                int a = sp > 0 && is_word(s[sp - 1]), b = sp < len && is_word(s[sp]);
                if ((a != b) == (prog[pc] == I_WORDB)) pc += 2; else fail = 1;
                break;
            }
            case I_SPLIT:
                if (!bt_push(&st, K_ALT, prog[pc + 2], sp, 0)) { result = -1; goto done; }
                pc = prog[pc + 1];
                break;
            case I_JMP: pc = prog[pc + 1]; break;
            case I_SAVE: {
                int slot = prog[pc + 1];
                if (!bt_push(&st, K_RESTORE, slot, caps[slot], 0)) { result = -1; goto done; }
                caps[slot] = sp; pc += 2;
                break;
            }
            case I_MARK: {
                int slot = 2 * (re->ncap + 1) + prog[pc + 1];
                if (!bt_push(&st, K_RESTORE, slot, caps[slot], 0)) { result = -1; goto done; }
                caps[slot] = sp; pc += 2;
                break;
            }
            case I_CHECK: {
                int slot = 2 * (re->ncap + 1) + prog[pc + 1];
                if (caps[slot] == sp) fail = 1; else pc += 2;
                break;
            }
            case I_BREF: case I_BREFI: {
                int g = prog[pc + 1];
                if (g > re->ncap || caps[2 * g] < 0 || caps[2 * g + 1] < 0) { fail = 1; break; }
                int gs = caps[2 * g], gl = caps[2 * g + 1] - gs;
                if (sp + gl > len) { fail = 1; break; }
                int ok = 1;
                if (prog[pc] == I_BREF) ok = memcmp(s + gs, s + sp, (size_t)gl) == 0;
                else for (int i = 0; i < gl; i++) if (tolower(s[gs + i]) != tolower(s[sp + i])) { ok = 0; break; }
                if (ok) { sp += gl; pc += 2; } else fail = 1;
                break;
            }
            case I_REP: {
                int kind = prog[pc + 1], arg = prog[pc + 2], min = prog[pc + 3], max = prog[pc + 4], greedy = prog[pc + 5];
                if (greedy) {
                    int n = 0;
                    while ((max < 0 || n < max) && atom_ok(re, kind, arg, s, sp + n, len)) n++;
                    if (n < min) { fail = 1; break; }
                    if (n > min && !bt_push(&st, K_REP, pc, sp, n)) { result = -1; goto done; }
                    sp += n; pc += 6;
                } else {
                    for (int i = 0; i < min; i++) if (!atom_ok(re, kind, arg, s, sp + i, len)) { fail = 1; break; }
                    if (fail) break;
                    if ((max < 0 || min < max) && !bt_push(&st, K_REPL, pc, sp, min)) { result = -1; goto done; }
                    sp += min; pc += 6;
                }
                break;
            }
            case I_LOOK: {
                if (depth > 20) { result = -1; goto done; }
                if (prog[pc + 1] == 2) {
                    /* atomic group: take the first way the body matches (the sub-run left its end
                       in caps[1]) and go on from there; the choices inside it are gone. Groups it
                       captured are undone if the match later backtracks past this point. */
                    int ns = pp_re_nslots(re);
                    int old[ns];
                    memcpy(old, caps, sizeof old);
                    int r = run(re, s, len, pc + 3, sp, start, -1, -1, caps, depth + 1, steps);
                    if (r < 0) { result = -1; goto done; }
                    if (!r) { fail = 1; break; }
                    for (int i = 2; i < ns; i++)
                        if (caps[i] != old[i] && !bt_push(&st, K_RESTORE, i, old[i], 0)) { result = -1; goto done; }
                    sp = caps[1]; pc = prog[pc + 2];
                    break;
                }
                int r = run(re, s, len, pc + 3, sp, start, -1, -1, caps, depth + 1, steps);
                if (r < 0) { result = -1; goto done; }
                if (r != prog[pc + 1]) pc = prog[pc + 2]; else fail = 1;
                break;
            }
            case I_LOOKB: {
                /* try every length the body can have, shortest first; the sub-run only counts
                   when it ends exactly here (end_at). caps[1] is scratch below depth 0. */
                if (depth > 20) { result = -1; goto done; }
                int found = 0;
                for (int w = prog[pc + 3]; w <= prog[pc + 4] && w <= sp && !found; w++) {
                    int r = run(re, s, len, pc + 5, sp - w, start, -1, sp, caps, depth + 1, steps);
                    if (r < 0) { result = -1; goto done; }
                    found = r;
                }
                if (found != prog[pc + 1]) pc = prog[pc + 2]; else fail = 1;
                break;
            }
            case I_MATCH:
                if (end_at >= 0 && sp != end_at) { fail = 1; break; }
                if (notempty_at >= 0 && sp == notempty_at && start == notempty_at && depth == 0) { fail = 1; break; }
                if (depth == 0) caps[0] = start;
                caps[1] = sp;   /* at depth 0 the end of the match; below, where the sub-program stopped */
                result = 1;
                goto done;
            default:
                result = -1; goto done;
            }
            if (fail) break;
        }
        /* backtrack */
        for (;;) {
            if (st.n == 0) { result = 0; goto done; }
            bt_entry e = st.v[--st.n];
            if (e.kind == K_RESTORE) { caps[e.pc] = e.sp; continue; }
            if (e.kind == K_ALT) { pc = e.pc; sp = e.sp; break; }
            if (e.kind == K_REP) {
                int min = prog[e.pc + 3];
                int n = e.n - 1;
                if (n < min) continue;
                if (n > min && !bt_push(&st, K_REP, e.pc, e.sp, n)) { result = -1; goto done; }
                pc = e.pc + 6; sp = e.sp + n;
                break;
            }
            /* K_REPL: try one more */
            {
                int kind = prog[e.pc + 1], arg = prog[e.pc + 2], max = prog[e.pc + 4];
                int n = e.n + 1;
                if ((max >= 0 && n > max) || !atom_ok(re, kind, arg, s, e.sp + n - 1, len)) continue;
                if ((max < 0 || n < max) && !bt_push(&st, K_REPL, e.pc, e.sp, n)) { result = -1; goto done; }
                pc = e.pc + 6; sp = e.sp + n;
                break;
            }
        }
    }
done:
    free(st.v);
    return result;
}

int pp_re_exec(const pp_regex *re, const char *str, int len, int start, int notempty_at, int *caps) {
    const unsigned char *s = (const unsigned char *)str;
    int nslots = pp_re_nslots(re);
    if (start < 0) start = 0;
    long steps = 0;
    for (int pos = start; pos <= len; pos++) {
        if (re->anchored && pos > 0) return 0;
        if (re->first >= 0) {
            const void *f = pos < len ? memchr(s + pos, re->first, (size_t)(len - pos)) : NULL;
            if (!f) return 0;
            pos = (int)((const unsigned char *)f - s);
        }
        for (int i = 0; i < nslots; i++) caps[i] = -1;
        int r = run(re, s, len, 0, pos, pos, notempty_at, -1, caps, 0, &steps);
        if (r) return r;
    }
    return 0;
}
