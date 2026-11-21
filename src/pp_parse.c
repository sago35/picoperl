/* Lexer + recursive-descent parser for the picoperl Perl subset. */
#include "pp_internal.h"
#include "pp_regex.h"
#include <ctype.h>
#include <setjmp.h>

/* ------------------------------------------------------------------ */
/* Lexer                                                               */
/* ------------------------------------------------------------------ */

enum { T_EOF = 0, T_NUM, T_STR, T_ISTR, T_IDENT, T_VAR, T_CAST, T_OP, T_QW, T_PAT, T_READLINE };

typedef struct {
    int    type;
    int    line;
    const char *s;      /* STR/ISTR/QW: copied body; IDENT/VAR: pointer into src */
    int    len;
    pp_val num;
    char   sigil;       /* T_VAR: '$','@','%','&','#' ($#name) */
    char   op[4];       /* T_OP / T_CAST */
    int    space_before;
    /* T_PAT: m// s/// tr/// qr// */
    const char *s2;     /* replacement body (s, tr) */
    int    len2;
    char   kind;        /* 'm', 's', 't' (tr), 'q' (qr) */
    char   delim;       /* opening delimiter of the pattern */
    char   flags[10];   /* trailing modifiers */
} pp_tok;

typedef struct {
    const char *src;
    int   pos, len;
    int   line;
    int   prev_term;    /* previous token ends a term (so '/', '%', '@' are operators) */
    int   prev_brace;   /* previous token was '}' (block end: sigils are more likely than operators) */
    int   prev_split;   /* previous token was the identifier 'split' (so // is a pattern) */
    int   prev_arrow;   /* previous token was '->' (so s, y, m, tr are method names) */
    int   prev_op;      /* previous token was an operator (so '/' must start a pattern) */
    int   prev_bind;    /* previous token was =~ or !~ (so `s,a,b,` is a substitution, not `s` and a comma) */
    /* pending heredoc bodies: when lexing crosses `from`, jump to `to` (past the
       terminator line) and set the line counter to `line_after` */
    struct { int from, to, line_after; } hd[4];
    int   nhd;
    char **strs; int nstrs, capstrs;
} pp_lex;

/* Compile-time lexical scopes: every block/sub/unit/loop-variable/condition scope gets an id.
   Variables resolve to (depth, slot); scopes that end up declaring nothing are "collapsed"
   (no run-time environment is created for them) and skipped when depths are computed. */
enum { SCOPE_BLOCK = 0, SCOPE_SUB, SCOPE_UNIT, SCOPE_LOOPVAR };
typedef struct {
    int id, kind, n, cap;
    pp_sym **vars;
    unsigned char strict_vars, strict_subs, strict_refs, warnings;
    unsigned char integer;       /* use integer: arithmetic in this scope is on integers */
} pscope;
typedef struct {
    pp_arena *arena;                             /* AST storage of the file being parsed */
    int heap_depth;                              /* >0: nodes are malloc'd (subtree runs at parse time) */
    pscope *st; int n, cap;                      /* open scopes (stack) */
    unsigned char *collapsed; int *parent_of; int nids, capids;
    struct { pp_node *node; int own, target; } *recs; int nrecs, caprecs;   /* lexical references to finalize */
    int has_goto;                                /* the file uses goto LABEL */
    pp_node **made; int nmade, capmade; int track;   /* eval STRING: every arena node, to clean up after a syntax error */
    pp_sym **ours; int nours, capours;           /* `our` names allowed under strict vars */
} pscopes;

typedef struct pp_parser pp_parser;
struct pp_parser {
    pp_interp *I;
    int state_n;                          /* `state` variables numbered so far in the enclosing sub */
    pp_lex L;
    uint8_t is_main;             /* this is the main script (not a required file, not an eval) */
    pp_tok cur, nxt;
    int has_nxt;
    pscopes *sc;                          /* shared with sub-parsers (string interpolation) */
    int owns_sc;
    const char *file;
    jmp_buf jb;
    char errmsg[256];
    int err_has_loc;                     /* errmsg already ends with "at FILE line N." */
    pp_sym **decl; int ndecl, capdecl;   /* subs declared so far (for bareword parsing) */
    unsigned char *declnoargs;           /* 1 if declared with an empty prototype */
    pp_sym *pending_label;               /* LABEL: before a loop statement */
    char pkg[96];                        /* current package */
    int pkg_stmt;                        /* a `package X;` statement was seen in the block being parsed */
    char **our_names; pp_sym **our_syms; int nours, capours;   /* `our` vars declared in non-main packages */
    int in_sub;
    int proto_mask;                              /* '$' positions of the prototype skip_prototype just read */
    unsigned char *declmask;                     /* per declared sub (decl[]): that mask */
    int argw;                                    /* the sub being parsed writes to $_[N] (or loops over @_) */
};

static char *lex_strdup(pp_lex *L, const char *s, int len) {
    char *p = pp_xmalloc(len + 1);
    memcpy(p, s, len); p[len] = 0;
    if (L->nstrs == L->capstrs) { L->capstrs = L->capstrs ? L->capstrs * 2 : 16; L->strs = pp_xrealloc(L->strs, L->capstrs * sizeof(char *)); }
    L->strs[L->nstrs++] = p;
    return p;
}

static void perr(pp_parser *P, const char *fmt, ...) __attribute__((noreturn));
static void perr(pp_parser *P, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(P->errmsg, sizeof P->errmsg, fmt, ap);
    va_end(ap);
    longjmp(P->jb, 1);
}

static pscope *scope_top(pp_parser *P) { return P->sc->n ? &P->sc->st[P->sc->n - 1] : NULL; }

static int scope_push(pp_parser *P, int kind) {
    pscopes *S = P->sc;
    if (S->n == S->cap) { S->cap = S->cap ? S->cap * 2 : 8; S->st = pp_xrealloc(S->st, S->cap * sizeof(pscope)); }
    if (S->nids == S->capids) {
        S->capids = S->capids ? S->capids * 2 : 32;
        S->collapsed = pp_xrealloc(S->collapsed, S->capids);
        S->parent_of = pp_xrealloc(S->parent_of, S->capids * sizeof(int));
    }
    pscope *top = scope_top(P);
    pscope sc;
    sc.id = S->nids; sc.kind = kind; sc.n = 0; sc.cap = 0; sc.vars = NULL;
    sc.strict_vars = top ? top->strict_vars : 0;
    sc.strict_subs = top ? top->strict_subs : 0;
    sc.strict_refs = top ? top->strict_refs : 0;
    sc.warnings = top ? top->warnings : 0;
    sc.integer = top ? top->integer : 0;
    S->parent_of[S->nids] = top ? top->id : -1;
    S->collapsed[S->nids] = 0;
    S->nids++;
    S->st[S->n++] = sc;
    return sc.id;
}
/* closes the scope; returns the number of slots it needs at run time */
static int scope_pop(pp_parser *P) {
    pscopes *S = P->sc;
    pscope *sc = &S->st[--S->n];
    int n = sc->n;
    S->collapsed[sc->id] = (n == 0 && sc->kind == SCOPE_BLOCK);
    free(sc->vars);
    return n;
}
static int scope_declare(pp_parser *P, pp_sym *sym) {
    pscope *sc = scope_top(P);
    if (sc->n == sc->cap) { sc->cap = sc->cap ? sc->cap * 2 : 4; sc->vars = pp_xrealloc(sc->vars, sc->cap * sizeof(pp_sym *)); }
    if (sc->n >= 255) perr(P, "too many lexical variables in one scope (max 255)");
    sc->vars[sc->n] = sym;
    return sc->n++;
}
static void scope_record(pp_parser *P, pp_node *node, int own, int target) {
    pscopes *S = P->sc;
    if (S->nrecs == S->caprecs) { S->caprecs = S->caprecs ? S->caprecs * 2 : 64; S->recs = pp_xrealloc(S->recs, S->caprecs * sizeof *S->recs); }
    S->recs[S->nrecs].node = node; S->recs[S->nrecs].own = own; S->recs[S->nrecs].target = target;
    S->nrecs++;
}
/* compute depths for records [from, nrecs); targets below min_target (outside a BEGIN/use subtree
   that runs now) become globals. The records are then dropped. */
static void scope_finalize(pp_parser *P, int from, int min_target) {
    pscopes *S = P->sc;
    for (int i = from; i < S->nrecs; i++) {
        pp_node *node = S->recs[i].node;
        int own = S->recs[i].own, target = S->recs[i].target;
        if (target < min_target) {
            /* a variable from outside a BEGIN / use subtree that runs now. A `my` of the file
               scope is reachable: the subtree runs in that scope (pending_env), directly
               below its own scopes, whatever blocks the source nests it in. Variables of
               enclosing blocks and subs do not exist yet and become globals. */
            if (target == S->st[0].id && node->sym != P->I->sym_args) {
                int depth = 0;
                for (int id = own; id >= min_target; id = S->parent_of[id]) if (!S->collapsed[id]) depth++;
                node->aux = (uint8_t)depth;
            }
            else node->flags &= ~PP_VAR_LEX;
            continue;
        }
        int depth = 0, id = own;
        while (id != target && id >= 0) { if (!S->collapsed[id]) depth++; id = S->parent_of[id]; }
        if (id < 0 || depth > 250) perr(P, "internal: lexical scope chain broken");
        node->aux = (uint8_t)depth;
    }
    S->nrecs = from;
}
static int is_ident_start(int c);
/* after "-e" (L->pos is at the '-'): does a path follow? Only a scalar or a string literal
   counts, so that -s => 1, -e, and -x(...) keep their old meaning. */
static int is_ident_char(int c);
static int lex_filetest_operand(pp_lex *L) {
    int i = L->pos + 2;
    while (i < L->len && (L->src[i] == ' ' || L->src[i] == '\t')) i++;
    if (i >= L->len) return 0;
    char c = L->src[i];
    if (c == '$') return i + 1 < L->len && (is_ident_start((unsigned char)L->src[i + 1]) || L->src[i + 1] == '{' || L->src[i + 1] == '$');
    if (c == '_' && !(i + 1 < L->len && (isalnum((unsigned char)L->src[i + 1]) || L->src[i + 1] == '_' || L->src[i + 1] == ':'))) return 1;   /* -d _ */
    if (L->src[L->pos + 1] == 't' && c >= 'A' && c <= 'Z') {   /* -t STDIN: a handle name (capitals only) */
        while (i < L->len && (isupper((unsigned char)L->src[i]) || isdigit((unsigned char)L->src[i]) || L->src[i] == '_')) i++;
        return i >= L->len || !(is_ident_start((unsigned char)L->src[i]) || L->src[i] == ':' || L->src[i] == '(');
    }
    if (c == '(') return 1;                                   /* -e ($path) */
    if (is_ident_start((unsigned char)c)) {                   /* -e uc $path, -f File::Spec->catfile(...), -e __FILE__ */
        while (i < L->len && (is_ident_char((unsigned char)L->src[i]) || L->src[i] == ':')) i++;
        while (i < L->len && (L->src[i] == ' ' || L->src[i] == '\t')) i++;
        return !(i + 1 < L->len && L->src[i] == '=' && L->src[i + 1] == '>');   /* (-e word => 1) stays a string */
    }
    return c == '"' || c == '\'';
}
/* is a '*' here a typeglob sigil rather than multiplication? Yes where a term is expected.
   After '}' the previous token may have ended a block or a subscript ($h{a} *$x), so there it
   only counts when the '*' starts its line (a statement after a block). */
static int lex_glob_position(pp_lex *L) {
    if (L->pos > 0 && strchr("@$%#*", L->src[L->pos - 1])) return 0;   /* $r->@*  $r->$#*  ** */
    if (!L->prev_term) return 1;
    if (!L->prev_brace) return 0;
    int i = L->pos - 1;
    while (i >= 0 && (L->src[i] == ' ' || L->src[i] == '\t')) i--;
    return i < 0 || L->src[i] == '\n';
}
static int is_special_var(char sigil, const char *name, int len) {
    if (len == 1) {
        char c = name[0];
        if (sigil == '$' && (c == '_' || c == 'a' || c == 'b' || c == '0' || c == '@' || c == '!' || c == ',' || c == '/' || c == '\\' || c == '"' || c == ';' || c == '&' || c == '$' || c == '.' || c == '<' || c == '>' || c == '|' || c == '^' || c == ']' || c == '?')) return 1;
        if (sigil == '@' && (c == '_' || c == '-' || c == '+')) return 1;
        if (sigil == '%' && c == '+') return 1;
        if (sigil == '$' && (c == '+' || c == '-')) return 1;
    }
    if (sigil == '$' && len >= 1 && isdigit((unsigned char)name[0])) return 1;   /* $1 .. */
    if (len >= 2 && name[0] == '^') return 1;                                     /* $^O ${^TAINT} %^H .. */
    if (sigil == '%' && len == 3 && !memcmp(name, "SIG", 3)) return 1;
    if (sigil == '$' && ((len == 6 && !memcmp(name, "STDOUT", 6)) || (len == 6 && !memcmp(name, "STDERR", 6)) || (len == 5 && !memcmp(name, "STDIN", 5)))) return 1;
    if (sigil == '$' && len > 0 && name[0] >= '1' && name[0] <= '9') return 1;
    if (sigil == '@' && ((len == 4 && !memcmp(name, "ARGV", 4)) || (len == 3 && !memcmp(name, "INC", 3)))) return 1;
    if (sigil == '%' && ((len == 3 && !memcmp(name, "ENV", 3)) || (len == 3 && !memcmp(name, "INC", 3)))) return 1;
    for (int i = 0; i + 1 < len; i++) if (name[i] == ':' && name[i+1] == ':') return 1;   /* qualified */
    return 0;
}
static void ours_add(pp_parser *P, pp_sym *sym) {
    pscopes *S = P->sc;
    if (S->nours == S->capours) { S->capours = S->capours ? S->capours * 2 : 8; S->ours = pp_xrealloc(S->ours, S->capours * sizeof(pp_sym *)); }
    S->ours[S->nours++] = sym;
}
static int ours_has(pp_parser *P, pp_sym *sym) {
    for (int i = 0; i < P->sc->nours; i++) if (P->sc->ours[i] == sym) return 1;
    return 0;
}
/* resolve a variable use: lexical (depth/slot) or global; enforces strict vars */
static void resolve_var(pp_parser *P, pp_node *node, const char *name, int len) {
    pscopes *S = P->sc;
    for (int i = S->n - 1; i >= 0; i--) {
        pscope *sc = &S->st[i];
        for (int k = sc->n - 1; k >= 0; k--) {
            if (sc->vars[k] == node->sym) {
                node->flags |= PP_VAR_LEX;
                node->op = (uint8_t)k;          /* slot */
                scope_record(P, node, S->st[S->n - 1].id, sc->id);
                return;
            }
        }
    }
    node->flags &= ~PP_VAR_LEX;
    if (strcmp(P->pkg, "main") != 0 && !strstr(node->sym->name, "::") && !is_special_var(node->sym->name[0], name, len)) {
        /* a plain global is main's, except when this package already has a variable of that
           name: one imported by `use Module qw($x %h)` (or set as $Pkg::x earlier) */
        char buf[256];
        int n = pp_snprintf(buf, sizeof buf, "%c%s::%.*s", node->sym->name[0], P->pkg, len, name);
        pp_sym *q = n < (int)sizeof buf ? pp_sym_find(P->I, buf, n) : NULL;
        if (q && q->gcell) node->sym = q;
    }
    pscope *top = scope_top(P);
    if (top && top->strict_vars && !is_special_var(node->sym->name[0], name, len) && !ours_has(P, node->sym) && !node->sym->gcell)
        perr(P, "Global symbol \"%s\" requires explicit package name (did you forget to declare \"my %s\"?)", node->sym->name, node->sym->name);
}

static int peekc(pp_lex *L, int off) { return L->pos + off < L->len ? (unsigned char)L->src[L->pos + off] : 0; }
static int is_ident_start(int c) { return isalpha(c) || c == '_'; }
static int is_ident_char(int c) { return isalnum(c) || c == '_'; }

static void skip_pod(pp_lex *L) {
    for (;;) {
        if (L->pos + 4 <= L->len && strncmp(L->src + L->pos, "=cut", 4) == 0) {
            while (L->pos < L->len && L->src[L->pos] != '\n') L->pos++;
            return;
        }
        while (L->pos < L->len && L->src[L->pos] != '\n') L->pos++;
        if (L->pos >= L->len) return;
        L->pos++; L->line++;
    }
}

static void hd_check(pp_lex *L) {
    while (L->nhd && L->pos >= L->hd[0].from) {
        L->pos = L->hd[0].to;
        L->line = L->hd[0].line_after;
        L->nhd--;
        for (int i = 0; i < L->nhd; i++) L->hd[i] = L->hd[i + 1];
    }
}

static void skip_ws(pp_lex *L) {
    if (L->pos == 0 && peekc(L, 0) == '=' && is_ident_start(peekc(L, 1))) skip_pod(L);
    for (;;) {
        hd_check(L);
        int c = peekc(L, 0);
        if (c == '\n') {
            L->line++; L->pos++;
            if (peekc(L, 0) == '=' && is_ident_start(peekc(L, 1))) skip_pod(L);
        }
        else if (c == ' ' || c == '\t' || c == '\r' || c == '\f') L->pos++;
        else if (c == '#') { while (L->pos < L->len && L->src[L->pos] != '\n') L->pos++; }
        else break;
    }
}

static int tok_is_ident_raw(pp_tok *t, const char *id) {
    return t->type == T_IDENT && (int)strlen(id) == t->len && memcmp(t->s, id, t->len) == 0;
}
static int closing_delim(int c) {
    switch (c) { case '(': return ')'; case '[': return ']'; case '{': return '}'; case '<': return '>'; default: return c; }
}

/* read a delimited body starting after the opening delimiter */
static const char *lex_delimited(pp_parser *P, int open, int *outlen) {
    pp_lex *L = &P->L;
    int close = closing_delim(open);
    int depth = 1;
    int start = L->pos;
    int startline = L->line;
    for (;;) {
        if (L->pos >= L->len) { L->line = startline; perr(P, "Can't find string terminator '%c' anywhere before EOF", close); }
        int c = L->src[L->pos];
        if (c == '\\' && L->pos + 1 < L->len) { if (L->src[L->pos+1] == '\n') L->line++; L->pos += 2; continue; }
        if (c == '\n') L->line++;
        if (open != close && c == open) depth++;
        else if (c == close) { if (--depth == 0) break; }
        L->pos++;
    }
    *outlen = L->pos - start;
    const char *body = lex_strdup(L, L->src + start, *outlen);
    L->pos++;
    return body;
}

/* m/PAT/flags, s/PAT/REPL/flags, tr/A/B/flags, qr/PAT/flags: fills a T_PAT token.
   The lexer is at the opening delimiter. */
static void lex_pattern(pp_parser *P, pp_tok *t, char kind) {
    pp_lex *L = &P->L;
    int d = peekc(L, 0);
    L->pos++;
    t->type = T_PAT; t->kind = kind; t->delim = (char)d;
    int blen;
    t->s = lex_delimited(P, d, &blen); t->len = blen;
    if (kind == 's' || kind == 't') {
        int d2 = d;
        if (closing_delim(d) != d) {      /* s{...}{...}: whitespace, then any bracket pair or delimiter */
            skip_ws(L);
            d2 = peekc(L, 0);
            if (!d2 || is_ident_char(d2) || d2 == '#') perr(P, "Substitution replacement not terminated");
            L->pos++;
        }
        t->s2 = lex_delimited(P, d2, &blen); t->len2 = blen;
    }
    int nf = 0;
    while (isalpha(peekc(L, 0))) { if (nf < 9) t->flags[nf++] = (char)peekc(L, 0); L->pos++; }
    t->flags[nf] = 0;
}

static const char *three_char_ops[] = { "<=>", "**=", "||=", "&&=", "//=", "...", "<<=", ">>=", "^^=", NULL };
static const char *two_char_ops[] = { "=>", "->", "++", "--", "**", "=~", "!~", "==", "!=", "<=", ">=", "&&", "||", "//", "..", "::",
    "+=", "-=", "*=", "/=", ".=", "%=", "|=", "&=", "^=", "^^", "<<", ">>", NULL };

static void lex_number(pp_parser *P, pp_tok *t) {
    pp_lex *L = &P->L;
    t->type = T_NUM;
    if (peekc(L, 0) == '0' && (peekc(L, 1) == 'x' || peekc(L, 1) == 'X')) {
        L->pos += 2; int64_t v = 0;
        while (isxdigit(peekc(L, 0)) || peekc(L, 0) == '_') { int c = peekc(L, 0); if (c != '_') v = v * 16 + (isdigit(c) ? c - '0' : (tolower(c) - 'a' + 10)); L->pos++; }
        t->num = pp_int(v); return;
    }
    if (peekc(L, 0) == '0' && (peekc(L, 1) == 'b' || peekc(L, 1) == 'B')) {
        L->pos += 2; int64_t v = 0;
        while (peekc(L, 0) == '0' || peekc(L, 0) == '1' || peekc(L, 0) == '_') { int c = peekc(L, 0); if (c != '_') v = v * 2 + (c - '0'); L->pos++; }
        t->num = pp_int(v); return;
    }
    if (peekc(L, 0) == '0' && (isdigit(peekc(L, 1)) || peekc(L, 1) == 'o' || peekc(L, 1) == 'O')) {
        L->pos += isdigit(peekc(L, 1)) ? 1 : 2; int64_t v = 0;
        while (isdigit(peekc(L, 0)) || peekc(L, 0) == '_') { int c = peekc(L, 0); if (c != '_') v = v * 8 + (c - '0'); L->pos++; }
        t->num = pp_int(v); return;
    }
    char buf[80]; int n = 0; int isfloat = 0;
    while (isdigit(peekc(L, 0)) || peekc(L, 0) == '_') { if (peekc(L, 0) != '_' && n < 78) buf[n++] = L->src[L->pos]; L->pos++; }
    if (peekc(L, 0) == '.' && peekc(L, 1) != '.') {
        isfloat = 1; if (n < 78) buf[n++] = '.'; L->pos++;
        while (isdigit(peekc(L, 0)) || peekc(L, 0) == '_') { if (peekc(L, 0) != '_' && n < 78) buf[n++] = L->src[L->pos]; L->pos++; }
    }
    if ((peekc(L, 0) == 'e' || peekc(L, 0) == 'E') && (isdigit(peekc(L, 1)) || ((peekc(L, 1) == '-' || peekc(L, 1) == '+') && isdigit(peekc(L, 2))))) {
        isfloat = 1; if (n < 78) buf[n++] = 'e'; L->pos++;
        if (peekc(L, 0) == '-' || peekc(L, 0) == '+') { if (n < 78) buf[n++] = L->src[L->pos]; L->pos++; }
        while (isdigit(peekc(L, 0))) { if (n < 78) buf[n++] = L->src[L->pos]; L->pos++; }
    }
    buf[n] = 0;
    if (isfloat) t->num = pp_num(strtod(buf, NULL));
    else t->num = pp_str_to_num(buf, n);
}

static int lex_name(pp_lex *L) {
    /* scan identifier with optional :: parts starting at pos; returns length */
    int start = L->pos;
    for (;;) {
        while (is_ident_char(peekc(L, 0))) L->pos++;
        if (peekc(L, 0) == ':' && peekc(L, 1) == ':' && is_ident_start(peekc(L, 2))) { L->pos += 2; continue; }
        break;
    }
    return L->pos - start;
}

/* The name of a variable at pos: like lex_name, plus the two ways a package's name table is
   written -- a trailing :: ($Pkg::{name}, %Pkg::) and a bare :: for main ($::{name}, $::x).
   Sets the token's s/len. */
static void lex_var_name(pp_lex *L, pp_tok *t) {
    int start = L->pos;
    if (peekc(L, 0) == ':' && peekc(L, 1) == ':') L->pos += 2;
    lex_name(L);
    if (peekc(L, 0) == ':' && peekc(L, 1) == ':' && !is_ident_start(peekc(L, 2)) && L->pos > start) L->pos += 2;
    t->s = L->src + start; t->len = L->pos - start;
    if (t->len == 2 && t->s[0] == ':') { t->s = "main::"; t->len = 6; }
}
/* does a variable name start here? (an identifier, or :: followed by one or by a subscript) */
static int var_name_start(pp_lex *L, int off) {
    if (is_ident_start(peekc(L, off))) return 1;
    return peekc(L, off) == ':' && peekc(L, off + 1) == ':' && (is_ident_start(peekc(L, off + 2)) || peekc(L, off + 2) == '{');
}

/* <$fh> in term position: one token (t->s/len = the variable name) */
static int lex_try_readline(pp_lex *L, pp_tok *t) {
    int save = L->pos;
    L->pos += 2;
    int start = L->pos;
    int len = lex_name(L);
    if (len > 0 && peekc(L, 0) == '>') {
        L->pos++;
        t->type = T_READLINE; t->s = L->src + start; t->len = len;
        return 1;
    }
    L->pos = save;
    return 0;
}

/* <<EOF, <<"EOF", <<'EOF', <<~EOF (dedented): the body is the source text after the
   current physical line (stacked after earlier heredocs begun on the same line), up
   to a line holding just the delimiter. The token is the body; an hd[] entry later
   makes the lexer jump over it. */
static void lex_heredoc(pp_parser *P, pp_tok *t) {
    pp_lex *L = &P->L;
    L->pos += 2;
    int dedent = 0, interp = 1;
    if (peekc(L, 0) == '~') { dedent = 1; L->pos++; }
    char q = 0;
    if (peekc(L, 0) == '"' || peekc(L, 0) == '\'') { q = (char)peekc(L, 0); L->pos++; if (q == '\'') interp = 0; }
    int ds = L->pos;
    int dlen;
    if (q) {   /* a quoted delimiter is any text up to the closing quote (<<"    EOT", <<'-- end --') */
        while (L->pos < L->len && L->src[L->pos] != q && L->src[L->pos] != '\n') L->pos++;
        dlen = L->pos - ds;
    } else dlen = lex_name(L);
    if (!dlen) perr(P, "syntax error in heredoc delimiter");
    const char *delim = L->src + ds;
    if (q) { if (peekc(L, 0) != q) perr(P, "unterminated heredoc delimiter"); L->pos++; }
    if (L->nhd >= 4) perr(P, "too many heredocs on one line");
    /* the body starts after the current line, or after the previous heredoc's terminator */
    int ls;
    if (L->nhd) ls = L->hd[L->nhd - 1].to;
    else { ls = L->pos; while (ls < L->len && L->src[ls] != '\n') ls++; if (ls < L->len) ls++; }
    int body_start = ls, body_end = -1, term_end = L->len, nlines = 0, tind = 0;
    for (int p = ls; p <= L->len; ) {
        int le = p;
        while (le < L->len && L->src[le] != '\n') le++;
        int q0 = p;
        if (dedent) while (q0 < le && (L->src[q0] == ' ' || L->src[q0] == '\t')) q0++;
        int ll = le - q0;
        if (ll > 0 && L->src[q0 + ll - 1] == '\r') ll--;
        if (ll == dlen && memcmp(L->src + q0, delim, dlen) == 0) {
            body_end = p; term_end = le < L->len ? le + 1 : L->len; tind = q0 - p;
            break;
        }
        nlines++;
        if (le >= L->len) break;
        p = le + 1;
    }
    if (body_end < 0) perr(P, "Can't find string terminator \"%.*s\" anywhere before EOF", dlen, delim);
    t->type = interp ? T_ISTR : T_STR;
    if (!interp) t->sigil = 'H';                 /* raw body: not even \' \\ are special */
    if (dedent && tind) {
        /* <<~: strip up to the terminator's indentation from every line */
        char *buf = pp_xmalloc((size_t)(body_end - body_start) + 1);
        int n = 0;
        for (int p = body_start; p < body_end; ) {
            int k = 0;
            while (k < tind && p < body_end && (L->src[p] == ' ' || L->src[p] == '\t')) { p++; k++; }
            while (p < body_end) { char ch = L->src[p++]; buf[n++] = ch; if (ch == '\n') break; }
        }
        t->s = lex_strdup(L, buf, n); t->len = n;
        free(buf);
    } else {
        t->s = L->src + body_start; t->len = body_end - body_start;
    }
    int base_line = L->nhd ? L->hd[L->nhd - 1].line_after : L->line + 1;   /* first body line */
    L->hd[L->nhd].from = body_start;
    L->hd[L->nhd].to = term_end;
    L->hd[L->nhd].line_after = base_line + nlines + 1;
    L->nhd++;
}

static void lex_next(pp_parser *P, pp_tok *t) {
    pp_lex *L = &P->L;
    int before = L->pos;
    skip_ws(L);
    memset(t, 0, sizeof *t);
    t->space_before = L->pos != before;
    t->line = L->line;
    int term = 0;
    if (L->pos >= L->len) { t->type = T_EOF; return; }
    int c = peekc(L, 0);

    if (isdigit(c) || (c == '.' && isdigit(peekc(L, 1)) && !L->prev_term)) {
        lex_number(P, t); term = 1;
    }
    else if (is_ident_start(c)) {
        int start = L->pos;
        int len = lex_name(L);
        const char *id = L->src + start;
        if (peekc(L, 0) == ':' && peekc(L, 1) == ':') {   /* Foo::Bar:: is the string "Foo::Bar" (Class::->method) */
            L->pos += 2;
            t->type = T_STR; t->s = id; t->len = len;
            term = 1;
            goto lexed;
        }
        if (id[0] == 'v' && len >= 2) {          /* v5.36: keep the version string as one token */
            int alld = 1;
            for (int i = 1; i < len; i++) if (!isdigit((unsigned char)id[i])) alld = 0;
            if (alld && peekc(L, 0) == '.' && isdigit((unsigned char)peekc(L, 1))) {
                while (peekc(L, 0) == '.' || isdigit((unsigned char)peekc(L, 0))) L->pos++;
                len = (int)(L->src + L->pos - id);
            }
        }
        if ((len == 1 && id[0] == 'q') || (len == 2 && (memcmp(id, "qq", 2) == 0 || memcmp(id, "qw", 2) == 0))) {
            int save = L->pos, saveline = L->line;
            skip_ws(L);
            int d = peekc(L, 0);
            if (d && !is_ident_char(d) && d != ',' && d != ';' && d != ')' && d != '=' && d != '}') {
                L->pos++;
                int blen; const char *body = lex_delimited(P, d, &blen);
                t->s = body; t->len = blen;
                if (len == 1) t->type = T_STR;
                else if (id[1] == 'q') t->type = T_ISTR;
                else t->type = T_QW;
                if (d == '\'' && t->type == T_ISTR) t->type = T_STR;
                L->prev_term = 1;
                return;
            }
            L->pos = save; L->line = saveline;
        }
        char pk = 0;
        if (len == 1 && (id[0] == 'm' || id[0] == 's' || id[0] == 'y')) pk = id[0] == 'y' ? 't' : id[0];
        else if (len == 2 && id[0] == 'q' && id[1] == 'r') pk = 'q';
        else if (len == 2 && id[0] == 't' && id[1] == 'r') pk = 't';
        if (pk && !L->prev_arrow) {
            int save = L->pos, saveline = L->line;
            int spaced = 0;
            if (isspace(peekc(L, 0))) { skip_ws(L); spaced = 1; }
            int d = peekc(L, 0);
            if (d && !is_ident_char(d) && (d != ',' || L->prev_bind) && d != ';' && d != ')' && d != '=' && d != '}' && d != ']' && !(spaced && d == '#')
                && !(d == '-' && peekc(L, 1) == '>')) {
                lex_pattern(P, t, pk);
                L->prev_term = 1; L->prev_brace = 0; L->prev_split = 0; L->prev_arrow = 0;
                return;
            }
            L->pos = save; L->line = saveline;
        }
        if ((len == 7 && memcmp(id, "__END__", 7) == 0) || (len == 8 && memcmp(id, "__DATA__", 8) == 0)) {
            /* the end of the code. What follows is read through <DATA>: after __DATA__ in any
               file (the DATA of its package), after __END__ in the main script only. */
            if (len == 8 || P->is_main) {
                int i = L->pos;
                while (i < L->len && L->src[i] != '\n') i++;
                if (i < L->len) i++;
                pp_set_data_handle(P->I, len == 8 ? P->pkg : "main", L->src + i, L->len - i);
            }
            L->pos = L->len; t->type = T_EOF; return;
        }
        t->type = T_IDENT; t->s = id; t->len = len;
        term = 0;
        if ((len == 8 && memcmp(id, "__LINE__", 8) == 0) || (len == 8 && memcmp(id, "__FILE__", 8) == 0)) term = 1;
    }
    else if (c == '"') { L->pos++; int blen; t->s = lex_delimited(P, '"', &blen); t->len = blen; t->type = T_ISTR; term = 1; }
    else if (c == '\'') { L->pos++; int blen; t->s = lex_delimited(P, '\'', &blen); t->len = blen; t->type = T_STR; term = 1; }
    else if (c == '`') { perr(P, "backticks are not supported"); }
    else if (c == '/' && !L->prev_term && (L->prev_split || L->prev_op || (peekc(L, 1) != '/' && peekc(L, 1) != ' ' && peekc(L, 1) != '='))) {
        /* /PATTERN/flags in term position */
        lex_pattern(P, t, 'm');
        term = 1;
    }
    else if (c == '<' && peekc(L, 1) == '<' && peekc(L, 2) != '=' && peekc(L, 2) != '<' &&
             (peekc(L, 2) == '~' || peekc(L, 2) == '"' || peekc(L, 2) == '\'' || is_ident_start(peekc(L, 2)))) {
        lex_heredoc(P, t);
        term = 1;
    }
    else if (c == '<' && !L->prev_term && peekc(L, 1) == '$' && is_ident_start(peekc(L, 2)) && lex_try_readline(L, t)) { term = 1; }
    else if (c == '$') {
        int c1 = peekc(L, 1);
        if (c1 == '#') {
            int c2 = peekc(L, 2);
            if (c2 == '{' || c2 == '$' || (c2 == '*' && L->prev_arrow)) { L->pos += 2; t->type = T_CAST; strcpy(t->op, "$#"); term = 0; }
            else if (is_ident_start(c2)) {
                L->pos += 2; int start = L->pos; int len = lex_name(L);
                t->type = T_VAR; t->sigil = '#'; t->s = L->src + start; t->len = len; term = 1;
            }
            else perr(P, "unexpected '$#'");
        }
        else if (var_name_start(L, 1)) {
            L->pos++; lex_var_name(L, t);
            t->type = T_VAR; t->sigil = '$'; term = 1;
        }
        else if (isdigit(c1)) {
            L->pos++; int start = L->pos;
            while (isdigit(peekc(L, 0))) L->pos++;
            t->type = T_VAR; t->sigil = '$'; t->s = L->src + start; t->len = L->pos - start; term = 1;
        }
        else if (c1 == '{' && peekc(L, 2) == '^' && isupper(peekc(L, 3))) {   /* ${^NAME}: the name is "^NAME" */
            int start = L->pos + 2, e = start + 1;
            while (e < L->len && is_ident_char((unsigned char)L->src[e])) e++;
            if (e >= L->len || L->src[e] != '}') perr(P, "syntax error in ${^NAME}");
            t->type = T_VAR; t->sigil = '$'; t->s = L->src + start; t->len = e - start; term = 1;
            L->pos = e + 1;
        }
        else if (c1 == '{' || c1 == '$') {
            if (c1 == '$' && !(is_ident_start(peekc(L, 2)) || peekc(L, 2) == '{' || peekc(L, 2) == '$')) {
                L->pos += 2; t->type = T_VAR; t->sigil = '$'; t->s = "$"; t->len = 1; term = 1;
            } else {
                L->pos++; t->type = T_CAST; strcpy(t->op, "$"); term = 0;
            }
        }
        else if (c1 == '^' && isupper(peekc(L, 2))) {   /* $^O, $^W, ...: the name is "^O" */
            L->pos += 3; t->type = T_VAR; t->sigil = '$'; t->s = L->src + L->pos - 2; t->len = 2; term = 1;
        }
        else if (c1 == '?' || c1 == ']' || c1 == '@' || c1 == '!' || c1 == ',' || c1 == '/' || c1 == '\\' || c1 == '"' || c1 == '&' || c1 == ';' || c1 == '|' || c1 == '^' || c1 == '+' || c1 == '-' || c1 == '.' || c1 == '<' || c1 == '>') {
            L->pos += 2; t->type = T_VAR; t->sigil = '$'; t->s = L->src + L->pos - 1; t->len = 1; term = 1;
        }
        else if (c1 == '*' && L->prev_arrow) { L->pos++; t->type = T_CAST; strcpy(t->op, "$"); term = 0; }   /* $r->$* */
        else if (c1 == ')') { L->pos++; t->type = T_CAST; strcpy(t->op, "$"); term = 0; }                    /* sub f ($x, $): a nameless parameter */
        else perr(P, "unexpected character after '$'");
    }
    else if (c == '@' && (!L->prev_term || L->prev_brace)) {
        int c1 = peekc(L, 1);
        if (var_name_start(L, 1)) {
            L->pos++; lex_var_name(L, t);
            t->type = T_VAR; t->sigil = '@'; term = 1;
        }
        else if (c1 == '{' || c1 == '$') { L->pos++; t->type = T_CAST; strcpy(t->op, "@"); term = 0; }
        else if (c1 == '*' && L->prev_arrow) { L->pos++; t->type = T_OP; strcpy(t->op, "@"); term = 0; }   /* $r->@* */
        else if (c1 == '-' || c1 == '+') {   /* @- / @+ : match offsets */
            L->pos += 2; t->type = T_VAR; t->sigil = '@'; t->s = L->src + L->pos - 1; t->len = 1; term = 1;
        }
        else perr(P, "unexpected '@'");
    }
    else if (c == '%' && !L->prev_term && peekc(L, 1) == '^' && isupper(peekc(L, 2))) {   /* %^H */
        L->pos += 3; t->type = T_VAR; t->sigil = '%'; t->s = L->src + L->pos - 2; t->len = 2; term = 1;
    }
    else if (c == '%' && (!L->prev_term || L->prev_brace) && peekc(L, 1) == '+') {   /* %+ : named captures */
        L->pos += 2; t->type = T_VAR; t->sigil = '%'; t->s = L->src + L->pos - 1; t->len = 1; term = 1;
    }
    else if (c == '%' && (!L->prev_term || L->prev_brace) && (is_ident_start(peekc(L, 1)) || peekc(L, 1) == '{' || peekc(L, 1) == '$')) {
        if (is_ident_start(peekc(L, 1))) {
            L->pos++; lex_var_name(L, t);
            t->type = T_VAR; t->sigil = '%'; term = 1;
        } else { L->pos++; t->type = T_CAST; strcpy(t->op, "%"); term = 0; }
    }
    else if (c == ':' && peekc(L, 1) == ':' && !L->prev_term && is_ident_start(peekc(L, 2))) {
        int start = L->pos; L->pos += 2; lex_name(L);            /* ::name is main::name */
        t->type = T_IDENT; t->s = L->src + start; t->len = L->pos - start; term = 0;
    }
    else if (c == '&' && (!L->prev_term || L->prev_brace) && (is_ident_start(peekc(L, 1)) || peekc(L, 1) == '{' || peekc(L, 1) == '$'
                          || (peekc(L, 1) == ':' && peekc(L, 2) == ':' && is_ident_start(peekc(L, 3))))) {
        if (is_ident_start(peekc(L, 1)) || peekc(L, 1) == ':') {
            L->pos++; int start = L->pos; if (peekc(L, 0) == ':') L->pos += 2;
            lex_name(L); int len = L->pos - start;
            t->type = T_VAR; t->sigil = '&'; t->s = L->src + start; t->len = len; term = 1;
        } else { L->pos++; t->type = T_CAST; strcpy(t->op, "&"); term = 0; }
    }
    else if (c == '-' && (!L->prev_term || L->prev_brace) && peekc(L, 1) && strchr("edfsrwxzltTBoORWXpSbcugk", peekc(L, 1))
             && !is_ident_char(peekc(L, 2)) && lex_filetest_operand(L)) {
        /* file test: -e $path, -d "dir" ... (one token "-e"; the letter must not reach the
           lexer on its own, `s $x` would start a substitution) */
        t->type = T_OP; t->op[0] = '-'; t->op[1] = (char)peekc(L, 1); t->op[2] = 0;
        L->pos += 2; term = 0;
    }
    else if (c == '*' && (is_ident_start(peekc(L, 1)) || peekc(L, 1) == '{' || peekc(L, 1) == '$')
             && lex_glob_position(L)) {
        /* typeglob: *name / *Pkg::name (a T_VAR with sigil '*'), *{expr} / *$name (a cast) */
        if (is_ident_start(peekc(L, 1))) {
            L->pos++; int start = L->pos; int len = lex_name(L);
            t->type = T_VAR; t->sigil = '*'; t->s = L->src + start; t->len = len; term = 1;
        } else { L->pos++; t->type = T_CAST; strcpy(t->op, "*"); term = 0; }
    }
    else {
        t->type = T_OP;
        for (int i = 0; three_char_ops[i]; i++)
            if (L->pos + 3 <= L->len && memcmp(L->src + L->pos, three_char_ops[i], 3) == 0) { strcpy(t->op, three_char_ops[i]); L->pos += 3; goto done_op; }
        for (int i = 0; two_char_ops[i]; i++)
            if (L->pos + 2 <= L->len && memcmp(L->src + L->pos, two_char_ops[i], 2) == 0) { strcpy(t->op, two_char_ops[i]); L->pos += 2; goto done_op; }
        t->op[0] = (char)c; t->op[1] = 0; L->pos++;
    done_op:
        term = (c == ')' || c == ']' || c == '}');
    }
lexed:
    L->prev_term = term;
    L->prev_brace = t->type == T_OP && t->op[0] == '}' && t->op[1] == 0;
    L->prev_split = tok_is_ident_raw(t, "split") || (L->prev_split && t->type == T_OP && t->op[0] == '(' && t->op[1] == 0);
    L->prev_arrow = t->type == T_OP && t->op[0] == '-' && t->op[1] == '>';
    L->prev_op = t->type == T_OP && !term;
    L->prev_bind = t->type == T_OP && (!strcmp(t->op, "=~") || !strcmp(t->op, "!~"));
}

/* ------------------------------------------------------------------ */
/* Parser helpers                                                      */
/* ------------------------------------------------------------------ */

static pp_node *const_str(pp_parser *P, const char *s, int len);
static pp_node *node(pp_parser *P, int type) {
    pp_node *n;
    if (P->sc->heap_depth) { n = pp_xmalloc(sizeof(pp_node)); memset(n, 0, sizeof *n); n->flags = PP_NODE_HEAP; }
    else {
        n = pp_arena_alloc(P->sc->arena, sizeof(pp_node)); memset(n, 0, sizeof *n); P->sc->arena->nnodes++;
        if (P->sc->track) {
            pscopes *S = P->sc;
            if (S->nmade == S->capmade) { S->capmade = S->capmade ? S->capmade * 2 : 64; S->made = pp_xrealloc(S->made, S->capmade * sizeof(pp_node *)); }
            S->made[S->nmade++] = n;
        }
    }
    pp_stat_nodes++;
    n->type = (uint8_t)type;
    n->line = (uint16_t)P->cur.line;
    if (type == N_BINOP || type == N_ASSIGN) { pscope *top = scope_top(P); if (top && top->integer) n->aux = 1; }   /* use integer */
    if (type == N_RV2AV || type == N_RV2HV || type == N_RV2SV || type == N_CALLREF || type == N_GLOB) {
        /* a string used as a reference (symbolic reference): dies under strict refs, otherwise
           the name is looked up in the current package (kept in b; main needs none) */
        pscope *top = scope_top(P);
        if (type != N_GLOB && top && top->strict_refs) n->flags |= PP_NODE_STRICTREFS;
        else if (strcmp(P->pkg, "main") != 0) n->b = const_str(P, P->pkg, (int)strlen(P->pkg));
    }
    return n;
}
/* temporary node (list shells etc.): always malloc'd so it can be dropped */
static pp_node *node_tmp(pp_parser *P, int type) {
    if (P->sc->track && !P->sc->heap_depth) return node(P, type);   /* eval STRING: everything is in its arena */
    pp_node *n = pp_xmalloc(sizeof(pp_node));
    memset(n, 0, sizeof *n);
    pp_stat_nodes++;
    n->flags = PP_NODE_HEAP;
    n->type = (uint8_t)type;
    n->line = (uint16_t)P->cur.line;
    return n;
}
/* a temporary that stays in the tree: move it into the arena (unless the subtree is heap anyway) */
static pp_node *adopt(pp_parser *P, pp_node *n) {
    if (!(n->flags & PP_NODE_HEAP) || P->sc->heap_depth) return n;
    pp_node *a = pp_arena_alloc(P->sc->arena, sizeof(pp_node));
    P->sc->arena->nnodes++;
    *a = *n;
    a->flags &= ~PP_NODE_HEAP;
    free(n);
    return a;
}
static void drop_shell(pp_node *l) {
    free(l->list);
    if (l->flags & PP_NODE_HEAP) { free(l); pp_stat_nodes--; }
    else { l->list = NULL; l->n = 0; }        /* an arena node stays behind, empty */
}
static void node_add(pp_node *n, pp_node *child) {
    if ((n->n & (n->n - 1)) == 0) {
        int cap = n->n ? n->n * 2 : 1;
        n->list = pp_xrealloc(n->list, cap * sizeof(pp_node *));
    }
    n->list[n->n++] = child;
}

/* Run a `use`, `use constant` or BEGIN block now, like perl does at compile time, so that
   imported subs are known to the parser (they may override builtins such as time/sleep). */
static int keeps_code_p(pp_node *n) {
    if (!n) return 0;
    if (n->type == N_ANONSUB) return 1;
    if (n->type == N_CONST) return 0;
    if (keeps_code_p(n->a) || keeps_code_p(n->b) || keeps_code_p(n->c) || keeps_code_p(n->d)) return 1;
    for (int i = 0; i < n->n; i++) if (keeps_code_p(n->list[i])) return 1;
    return 0;
}
/* Code that runs while the file is still being parsed (BEGIN, use, use constant) may call the
   named subs defined above it, as in perl. Define the ones collected so far now, in the scope
   the file will run in (pending_env), instead of waiting for pp_exec_unit. */
static void predefine_subs(pp_parser *P) {
    pp_interp *I = P->I;
    pscopes *S = P->sc;
    int todo = 0;
    for (int i = 0; i < I->nsubdefs; i++) if (!(I->subdefs[i]->flags & PP_SUB_EARLY)) todo = 1;
    if (!todo) return;
    /* their bodies need the depth of each lexical now; the pass at the end of the file
       computes the same values again */
    for (int i = 0; i < S->nrecs; i++) {
        int depth = 0, id = S->recs[i].own;
        while (id != S->recs[i].target && id >= 0) { if (!S->collapsed[id]) depth++; id = S->parent_of[id]; }
        if (id >= 0 && depth <= 250) S->recs[i].node->aux = (uint8_t)depth;
    }
    if (!I->pending_env) I->pending_env = pp_env_new(NULL, 0);
    pp_env_grow(I->pending_env, S->st[0].n);
    for (int i = 0; i < I->nsubdefs; i++) {
        pp_node *d = I->subdefs[i];
        if (d->flags & PP_SUB_EARLY) continue;
        d->flags |= PP_SUB_EARLY;
        pp_obj *code = pp_obj_new(PP_OCODE);
        code->u.code.body = d->a;
        pp_note_sub_file(I, d->a, P->file);
        code->u.code.env = I->pending_env; pp_env_incref(I->pending_env);
        code->u.code.name = d->sym->name + 1;
        code->u.code.block_proto = d->op == 1; code->u.code.pscalar = d->aux;
        pp_define_sub(I, d->sym, code);
    }
}
static void exec_at_parse(pp_parser *P, pp_node *n, int rec_start, int id_start) {
    pp_interp *I = P->I;
    scope_finalize(P, rec_start, id_start);
    predefine_subs(P);
    /* it runs in the file scope, so that it can use the file's `my` variables */
    pp_env *saved = I->env;
    if (!I->pending_env) I->pending_env = pp_env_new(NULL, 0);
    pp_env_grow(I->pending_env, P->sc->st[0].n);
    I->env = I->pending_env;
    const char *sf = I->file; int sl = I->line;
    pp_obj *scode = I->cur_code;        /* ... and are file-level code, not part of the sub that asked */
    I->cur_code = NULL;                 /*     for this file (caller must not report that sub's package) */
    const char *sp = I->file_pkg;       /* BEGIN / use run in the package being parsed */
    I->file_pkg = strcmp(P->pkg, "main") ? pp_intern(I, P->pkg, (int)strlen(P->pkg))->name : NULL;
    I->file = P->file; I->line = n->line;
    pp_val v = pp_eval(I, n, CTX_VOID, NULL);
    pp_release(v);
    I->file = sf; I->line = sl; I->file_pkg = sp;
    I->cur_code = scode;
    I->env = saved;
    if (I->ctl == CTL_DIE) {
        pp_str *s = pp_to_str(I->err);
        int len = s->len; while (len > 0 && s->d[len - 1] == '\n') len--;
        snprintf(P->errmsg, sizeof P->errmsg, "%.*s", len, s->d);
        pp_str_decref(s);
        pp_release(I->err); I->err = pp_undef();
        I->ctl = CTL_NONE;
        P->err_has_loc = 1;
        longjmp(P->jb, 1);
    }
    if (I->ctl) I->ctl = CTL_NONE;
    if (keeps_code_p(n)) {   /* a closure may still point into it: keep until pp_free */
        I->all_subdefs = pp_xrealloc(I->all_subdefs, (I->nall_subdefs + 1) * sizeof(pp_node *));
        I->all_subdefs[I->nall_subdefs++] = n;
    } else pp_free_ast(n);
}

static void advance(pp_parser *P) {
    if (P->has_nxt) { P->cur = P->nxt; P->has_nxt = 0; }
    else lex_next(P, &P->cur);
}
static pp_tok *peek(pp_parser *P) {
    if (!P->has_nxt) { lex_next(P, &P->nxt); P->has_nxt = 1; }
    return &P->nxt;
}
static int is_op(pp_tok *t, const char *op) { return t->type == T_OP && strcmp(t->op, op) == 0; }
static int cur_is_op(pp_parser *P, const char *op) { return is_op(&P->cur, op); }
static int tok_is_ident(pp_tok *t, const char *id) {
    return t->type == T_IDENT && (int)strlen(id) == t->len && memcmp(t->s, id, t->len) == 0;
}
static int cur_is_ident(pp_parser *P, const char *id) { return tok_is_ident(&P->cur, id); }

static void tok_desc(pp_tok *t, char *buf, int cap) {
    switch (t->type) {
    case T_EOF: pp_snprintf(buf, cap, "end of file"); break;
    case T_NUM: pp_snprintf(buf, cap, "number"); break;
    case T_STR: case T_ISTR: case T_QW: pp_snprintf(buf, cap, "string"); break;
    case T_PAT: pp_snprintf(buf, cap, "pattern"); break;
    case T_READLINE: pp_snprintf(buf, cap, "<$fh>"); break;
    case T_IDENT: pp_snprintf(buf, cap, "'%.*s'", t->len, t->s); break;
    case T_VAR: pp_snprintf(buf, cap, "'%c%.*s'", t->sigil == '#' ? '$' : t->sigil, t->len, t->s); break;
    default: pp_snprintf(buf, cap, "'%s'", t->op); break;
    }
}
static void expect_op(pp_parser *P, const char *op) {
    if (!cur_is_op(P, op)) {
        char d[64]; tok_desc(&P->cur, d, sizeof d);
        perr(P, "syntax error: expected '%s' near %s", op, d);
    }
    advance(P);
}
static int accept_op(pp_parser *P, const char *op) {
    if (cur_is_op(P, op)) { advance(P); return 1; }
    return 0;
}

static int has_pkg_sep(const char *name, int len) {
    for (int i = 0; i + 1 < len; i++) if (name[i] == ':' && name[i+1] == ':') return 1;
    return 0;
}

/* symbol for sigil+name, qualified with the current package where perl would do so */
static pp_sym *sym_sigil(pp_parser *P, char sigil, const char *name, int len) {
    char buf[256];
    if (len > 200) perr(P, "identifier too long");
    if (has_pkg_sep(name, len)) {
        if (len > 6 && memcmp(name, "main::", 6) == 0) { name += 6; len -= 6; }
        else if (len > 2 && name[0] == ':' && name[1] == ':') { name += 2; len -= 2; }
        else {
            buf[0] = sigil; memcpy(buf + 1, name, len); buf[len + 1] = 0;
            return pp_intern(P->I, buf, len + 1);
        }
    }
    else if (strcmp(P->pkg, "main") != 0) {
        if (sigil == '&') {
            int n = pp_snprintf(buf, sizeof buf, "&%s::%.*s", P->pkg, len, name);
            return pp_intern(P->I, buf, n);
        }
        for (int i = P->nours - 1; i >= 0; i--)   /* newest first: a later `our` of the same name in another package wins */
            if (P->our_names[i][0] == sigil && (int)strlen(P->our_names[i]) == len + 1 && memcmp(P->our_names[i] + 1, name, len) == 0)
                return P->our_syms[i];
    }
    buf[0] = sigil; memcpy(buf + 1, name, len); buf[len + 1] = 0;
    return pp_intern(P->I, buf, len + 1);
}

static pp_node *node(pp_parser *P, int type);
#define PARSE_AS_BLOCK 0x40000000   /* flag in the `line` argument of parse_expr_shared */
static pp_node *parse_expr_shared(pp_parser *parent, const char *src, int len, int line, int *err);
/* N_VAR for a variable use */
static pp_node *mkvar(pp_parser *P, char sigil, const char *name, int len) {
    pp_node *v = node(P, N_VAR);
    v->sym = sym_sigil(P, sigil, name, len);
    resolve_var(P, v, name, len);
    if (sigil == '$' && len == 1 && name[0] == '!') v->flags |= PP_VAR_ERRNO;
    return v;
}
/* N_VAR for a `my` declaration in the current scope */
static pp_node *mkvar_decl(pp_parser *P, char sigil, const char *name, int len) {
    pp_node *v = node(P, N_VAR);
    char buf[256];
    if (len > 250) perr(P, "identifier too long");
    buf[0] = sigil; memcpy(buf + 1, name, len); buf[len + 1] = 0;
    v->sym = pp_intern(P->I, buf, len + 1);
    v->flags |= PP_VAR_LEX;
    v->op = (uint8_t)scope_declare(P, v->sym);
    scope_record(P, v, scope_top(P)->id, scope_top(P)->id);
    return v;
}

/* `our $x` inside package P: later references to $x in this file mean $P::x */
static pp_sym *declare_our(pp_parser *P, char sigil, const char *name, int len) {
    if (strcmp(P->pkg, "main") == 0 || has_pkg_sep(name, len)) { pp_sym *q = sym_sigil(P, sigil, name, len); ours_add(P, q); return q; }
    char buf[256];
    int n = pp_snprintf(buf, sizeof buf, "%c%s::%.*s", sigil, P->pkg, len, name);
    pp_sym *q = pp_intern(P->I, buf, n);
    if (P->nours == P->capours) {
        P->capours = P->capours ? P->capours * 2 : 8;
        P->our_names = pp_xrealloc(P->our_names, P->capours * sizeof(char *));
        P->our_syms = pp_xrealloc(P->our_syms, P->capours * sizeof(pp_sym *));
    }
    char *un = pp_xmalloc(len + 2); un[0] = sigil; memcpy(un + 1, name, len); un[len + 1] = 0;
    P->our_names[P->nours] = un; P->our_syms[P->nours] = q; P->nours++;
    ours_add(P, q);
    return q;
}

static void declare_sub(pp_parser *P, pp_sym *s, int noargs) {
    for (int i = 0; i < P->ndecl; i++) if (P->decl[i] == s) { P->declnoargs[i] |= (unsigned char)noargs; return; }
    if (P->ndecl == P->capdecl) {
        P->capdecl = P->capdecl ? P->capdecl * 2 : 16;
        P->decl = pp_xrealloc(P->decl, P->capdecl * sizeof(pp_sym *));
        P->declnoargs = pp_xrealloc(P->declnoargs, P->capdecl);
        P->declmask = pp_xrealloc(P->declmask, P->capdecl);
    }
    P->declmask[P->ndecl] = 0;
    P->declnoargs[P->ndecl] = (unsigned char)noargs;
    P->decl[P->ndecl++] = s;
}
/* was the sub declared with a (&...) prototype? Known from this file's declarations (bit 4 of
   declnoargs) or from the sub itself when it is already defined (imported by `use`). */
static int is_block_proto(pp_parser *P, pp_sym *s) {
    for (int i = 0; i < P->ndecl; i++) if (P->decl[i] == s) return (P->declnoargs[i] & 4) != 0;
    pp_obj *c = pp_lookup_sub(P->I, s);
    return c && c->u.code.block_proto;
}
/* A '$' in a prototype gives its argument scalar context: is(grep(...), 1) compares a count,
   ok($s =~ /re/, $name) passes one value even when the match fails. Arguments that are one
   value anyway are left alone. */
static void apply_proto(pp_parser *P, pp_node *n) {
    int mask = -1;
    for (int i = 0; i < P->ndecl; i++) if (P->decl[i] == n->sym) mask = P->declmask[i];
    if (mask < 0) { pp_obj *c = pp_lookup_sub(P->I, n->sym); mask = c ? c->u.code.pscalar : 0; }
    for (int i = 0; i < n->n && i < 8 && mask; i++) {
        pp_node *a = n->list[i];
        if (!(mask & (1 << i))) continue;
        int listy = (a->type == N_VAR && a->sym->name[0] != '$') || a->type == N_BUILTIN || a->type == N_CALL || a->type == N_CALLREF
                    || a->type == N_METHOD || a->type == N_RV2AV || a->type == N_RV2HV || a->type == N_MATCH;
        if (!listy) continue;
        pp_node *sc = node(P, N_BUILTIN); sc->op = (uint8_t)pp_builtin_id("scalar", 6, NULL); sc->line = a->line;
        node_add(sc, a);
        n->list[i] = sc;
    }
}
/* 0: unknown, 1: declared (list operator), 2: declared with no args */
static int is_declared(pp_parser *P, pp_sym *s) {
    for (int i = 0; i < P->ndecl; i++) if (P->decl[i] == s) return (P->declnoargs[i] & 1) ? 2 : 1;
    pp_obj *c = pp_lookup_sub(P->I, s);
    if (!c) return 0;
    return (c->u.code.is_const || (c->u.code.native == NULL && c->u.code.body == NULL)) ? 2 : 1;
}

static pp_node *parse_block(pp_parser *P);
static void parse_block_stmts(pp_parser *P, pp_node *b);
static int  parse_sub_params(pp_parser *P, pp_node *b, const char *subname, int *noargs);
static pp_node *parse_expr(pp_parser *P);
static pp_node *parse_assign(pp_parser *P);
static void note_argw(pp_parser *P, pp_node *t);
static pp_node *parse_comma_list(pp_parser *P);
static pp_node *parse_binary(pp_parser *P, int minprec);
static pp_node *parse_unary(pp_parser *P);
static int is_handle_name(const char *id, int len);
static pp_node *handle_const(pp_parser *P, const char *id, int len);
static pp_node *parse_term(pp_parser *P);
static pp_node *parse_statement(pp_parser *P);
static pp_node *parse_ident_term(pp_parser *P);

static pp_node *unwrap_single(pp_node *l);
static pp_node *const_str(pp_parser *P, const char *s, int len) {
    pp_node *n = node(P, N_CONST);
    n->val = pp_strn(s, len);
    return n;
}
static pp_node *const_int(pp_parser *P, int64_t i) {
    pp_node *n = node(P, N_CONST);
    n->val = pp_int(i);
    return n;
}
static pp_node *wrap_list(pp_parser *P, pp_node *n) {
    if (n->type == N_LIST) return n;
    pp_node *l = node_tmp(P, N_LIST);
    node_add(l, n);
    return l;
}
/* move the children of temporary list node l into dst and free l's shell */
static void take_children(pp_node *dst, pp_node *l) {
    for (int i = 0; i < l->n; i++) node_add(dst, l->list[i]);
    drop_shell(l);
}
static pp_node *unwrap_single(pp_node *l) {
    pp_node *only = l->list[0];
    drop_shell(l);
    return only;
}

/* ------------------------------------------------------------------ */
/* String literals & interpolation                                     */
/* ------------------------------------------------------------------ */

static int hexval(int c) { return isdigit(c) ? c - '0' : tolower(c) - 'a' + 10; }

static pp_node *single_quoted(pp_parser *P, const char *s, int len) {
    char *buf = pp_xmalloc(len + 1); int n = 0;
    for (int i = 0; i < len; i++) {
        if (s[i] == '\\' && i + 1 < len && (s[i+1] == '\\' || s[i+1] == '\'')) buf[n++] = s[++i];
        else buf[n++] = s[i];
    }
    pp_node *r = const_str(P, buf, n);
    free(buf);
    return r;
}

static void append_char(char **buf, int *n, int *cap, int c) {
    if (*n + 1 >= *cap) { *cap = *cap ? *cap * 2 : 32; *buf = pp_xrealloc(*buf, *cap); }
    (*buf)[(*n)++] = (char)c;
}
static void append_utf8(char **buf, int *n, int *cap, int v) {
    if (v < 0x80) append_char(buf, n, cap, v);
    else if (v < 0x800) { append_char(buf, n, cap, 0xC0 | (v >> 6)); append_char(buf, n, cap, 0x80 | (v & 0x3F)); }
    else if (v < 0x10000) { append_char(buf, n, cap, 0xE0 | (v >> 12)); append_char(buf, n, cap, 0x80 | ((v >> 6) & 0x3F)); append_char(buf, n, cap, 0x80 | (v & 0x3F)); }
    else { append_char(buf, n, cap, 0xF0 | (v >> 18)); append_char(buf, n, cap, 0x80 | ((v >> 12) & 0x3F)); append_char(buf, n, cap, 0x80 | ((v >> 6) & 0x3F)); append_char(buf, n, cap, 0x80 | (v & 0x3F)); }
}

static int match_bracket(const char *s, int len, int i) {
    int open = s[i], close = open == '[' ? ']' : '}';
    int depth = 0;
    for (; i < len; i++) {
        if (s[i] == open) depth++;
        else if (s[i] == close) { if (--depth == 0) return i + 1; }
        else if (s[i] == '\\') i++;
    }
    return -1;
}

/* extent of an interpolated expression starting at s[i] ('$' or '@'); 0 if none */
static int interp_extent(const char *s, int len, int i) {
    int j = i + 1;
    if (j >= len) return 0;
    if (s[i] == '$' && s[j] == '#' && j + 1 < len && (is_ident_start((unsigned char)s[j+1]) || s[j+1] == '{' || s[j+1] == '$')) {
        if (s[j+1] == '{') { int e = match_bracket(s, len, j + 1); return e < 0 ? 0 : e; }
        if (s[j+1] == '$') { int e = interp_extent(s, len, j + 1); return e ? e : 0; }
        j += 2;
        while (j < len && is_ident_char((unsigned char)s[j])) j++;
        return j;
    }
    if (s[j] == '{') {
        int e = match_bracket(s, len, j);
        if (e < 0) return 0;
        j = e;
    }
    else if (is_ident_start((unsigned char)s[j]) || (s[j] == ':' && j + 2 < len && s[j+1] == ':' && is_ident_start((unsigned char)s[j+2]))) {
        if (s[j] == ':') j += 2;                       /* "$::name" is main's */
        while (j < len && (is_ident_char((unsigned char)s[j]) || (s[j] == ':' && j + 2 < len && s[j+1] == ':' && is_ident_start((unsigned char)s[j+2])))) j += (s[j] == ':') ? 2 : 1;
        if (j + 2 < len && s[j] == ':' && s[j+1] == ':' && s[j+2] == '{') j += 2;   /* "$Pkg::{name}": the name table */
    }
    else if (s[i] == '$' && s[j] == '$' && j + 1 < len && (is_ident_start((unsigned char)s[j+1]) || s[j+1] == '{')) {
        return interp_extent(s, len, j);
    }
    else if (s[i] == '$' && s[j] == '$') return j + 1;   /* "$$": the process id */
    else if (s[i] == '@' && s[j] == '$' && j + 1 < len && is_ident_start((unsigned char)s[j+1])) {   /* "@$ref" */
        j++;
        while (j < len && is_ident_char((unsigned char)s[j])) j++;
    }
    else if (s[i] == '$' && s[j] == '^' && j + 1 < len && isupper((unsigned char)s[j+1])) j += 2;   /* $^O */
    else if (s[i] == '$' && (s[j] == ']' || s[j] == '.')) return j + 1;                                /* $] and $. */
    else if (s[i] == '$' && (isdigit((unsigned char)s[j]) || s[j] == '@' || s[j] == '!' || s[j] == '&' || s[j] == '+' || s[j] == '-' || s[j] == '?')) {
        j++;
        while (isdigit((unsigned char)s[j-1]) && j < len && isdigit((unsigned char)s[j])) j++;
    }
    else return 0;
    for (;;) {
        if (j < len && s[j] == '[') {
            int e = match_bracket(s, len, j);
            if (e < 0) break;
            int k = j + 1; int ok = 0;
            while (k < e - 1 && isspace((unsigned char)s[k])) k++;
            if (k < e - 1 && (s[k] == '$' || s[k] == '-' || isdigit((unsigned char)s[k]))) ok = 1;
            if (!ok) break;
            j = e;
        }
        else if (j < len && s[j] == '{') {
            int e = match_bracket(s, len, j);
            if (e < 0) break;
            j = e;
        }
        else if (j + 2 < len && s[j] == '-' && s[j+1] == '>' && (s[j+2] == '[' || s[j+2] == '{')) {
            int e = match_bracket(s, len, j + 2);
            if (e < 0) break;
            j = e;
        }
        else break;
    }
    return j;
}

static pp_node *parse_interp_expr(pp_parser *P, const char *s, int len, int line) {
    char *tmp = NULL;
    if (len >= 3 && s[1] == '{') {   /* ${name} -> $name */
        int k = 2; while (k < len - 1 && isspace((unsigned char)s[k])) k++;
        int e = k; while (e < len - 1 && is_ident_char((unsigned char)s[e])) e++;
        int e2 = e; while (e2 < len - 1 && isspace((unsigned char)s[e2])) e2++;
        if (e > k && e2 == len - 1) {
            tmp = pp_xmalloc(len);
            tmp[0] = s[0]; memcpy(tmp + 1, s + k, e - k); tmp[1 + e - k] = 0;
            s = tmp; len = 1 + e - k;
        }
    }
    int err = 0;
    pp_node *n = parse_expr_shared(P, s, len, line, &err);
    free(tmp);
    if (err || !n) longjmp(P->jb, 1);   /* errmsg was set by the sub-parser */
    return n;
}

static pp_node *double_quoted(pp_parser *P, const char *s, int len) {
    pp_node *r = node_tmp(P, N_INTERP);
    char *buf = NULL; int n = 0, cap = 0;
    int line = P->cur.line;
    /* \U \L \Q change the text up to \E (or the end), \u \l only the next character.
       Literal text is converted here; an interpolated value is wrapped in uc() etc. */
    int mode = 0, first = 0;
    for (int i = 0; i < len; ) {
        int c = (unsigned char)s[i];
        if (c == '\\' && i + 1 < len) {
            int d = (unsigned char)s[i+1]; i += 2;
            switch (d) {
            case 'n': append_char(&buf, &n, &cap, '\n'); break;
            case 't': append_char(&buf, &n, &cap, '\t'); break;
            case 'r': append_char(&buf, &n, &cap, '\r'); break;
            case 'f': append_char(&buf, &n, &cap, '\f'); break;
            case 'a': append_char(&buf, &n, &cap, '\a'); break;
            case 'e': append_char(&buf, &n, &cap, 27); break;
            case 'b': append_char(&buf, &n, &cap, 8); break;
            case 'c':   /* \cA is chr(1): the control character of the letter */
                if (i < len) { append_char(&buf, &n, &cap, toupper((unsigned char)s[i]) ^ 64); i++; }
                break;
            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
                int v = d - '0', k = 1;
                while (k < 3 && i < len && s[i] >= '0' && s[i] <= '7') { v = v * 8 + (s[i] - '0'); i++; k++; }
                append_char(&buf, &n, &cap, v); break;
            }
            case 'x': {
                int v = 0;
                if (i < len && s[i] == '{') { i++; while (i < len && s[i] != '}') { v = v * 16 + hexval(s[i]); i++; } i++; append_utf8(&buf, &n, &cap, v); }
                else { int k = 0; while (k < 2 && i < len && isxdigit((unsigned char)s[i])) { v = v * 16 + hexval(s[i]); i++; k++; } append_char(&buf, &n, &cap, v); }
                break;
            }
            case '\n': line++; break;
            case 'U': case 'L': case 'Q': mode = d; break;
            case 'E': mode = 0; break;
            case 'u': case 'l': first = d; break;
            default: append_char(&buf, &n, &cap, d); break;
            }
            continue;
        }
        if (c == '$' || c == '@') {
            int e = interp_extent(s, len, i);
            if (e > i + 1) {
                if (n) { node_add(r, const_str(P, buf, n)); n = 0; }
                pp_node *ex = parse_interp_expr(P, s + i, e - i, line);
                if (c == '@') {
                    pp_node *j = node(P, N_BUILTIN);
                    j->op = (uint8_t)pp_builtin_id("join", 4, NULL);
                    node_add(j, const_str(P, " ", 1));
                    node_add(j, ex);
                    ex = j;
                }
                if (mode) {
                    pp_node *w = node(P, N_BUILTIN);
                    w->op = (uint8_t)(mode == 'U' ? pp_builtin_id("uc", 2, NULL) : mode == 'L' ? pp_builtin_id("lc", 2, NULL) : pp_builtin_id("quotemeta", 9, NULL));
                    node_add(w, ex); ex = w;
                }
                if (first) {
                    pp_node *w = node(P, N_BUILTIN);
                    w->op = (uint8_t)(first == 'u' ? pp_builtin_id("ucfirst", 7, NULL) : pp_builtin_id("lcfirst", 7, NULL));
                    node_add(w, ex); ex = w;
                    first = 0;
                }
                node_add(r, ex);
                i = e;
                continue;
            }
        }
        if (c == '\n') line++;
        if (mode == 'U') c = toupper(c);
        else if (mode == 'L') c = tolower(c);
        else if (mode == 'Q' && !isalnum(c) && c != '_' && c < 0x80) append_char(&buf, &n, &cap, '\\');
        if (first) { c = first == 'u' ? toupper(c) : tolower(c); first = 0; }
        append_char(&buf, &n, &cap, c);
        i++;
    }
    if (n || r->n == 0) node_add(r, const_str(P, buf ? buf : "", n));
    free(buf);
    if (r->n == 1 && r->list[0]->type == N_CONST) return unwrap_single(r);
    return adopt(P, r);
}

/* ---- regular expression literals ---- */

/* extent of an interpolated variable in a pattern starting at s[i] ('$' or '@'); 0 if none.
   Like interp_extent, but a trailing {n,m} is a quantifier and [..] is only a subscript when
   it holds a plain index. */
static int re_interp_extent(const char *s, int len, int i) {
    int j = i + 1;
    if (j >= len) return 0;
    int nx = (unsigned char)s[j];
    if (s[i] == '$' && nx == '$') { if (j + 1 < len && (is_ident_start((unsigned char)s[j+1]) || s[j+1] == '{')) return re_interp_extent(s, len, j); return 0; }
    if (nx == '{') { int e = match_bracket(s, len, j); if (e < 0) return 0; j = e; }
    else if (is_ident_start(nx)) {
        while (j < len && (is_ident_char((unsigned char)s[j]) || (s[j] == ':' && j + 2 < len && s[j+1] == ':' && is_ident_start((unsigned char)s[j+2])))) j += (s[j] == ':') ? 2 : 1;
    }
    else if (s[i] == '$' && isdigit(nx)) { while (j < len && isdigit((unsigned char)s[j])) j++; return j; }
    else if (s[i] == '$' && nx == '&') return j + 1;
    else if (s[i] == '$' && nx == '^' && j + 1 < len && isupper((unsigned char)s[j+1])) return j + 2;   /* $^O */
    else return 0;
    for (;;) {
        int arrow = (j + 2 < len && s[j] == '-' && s[j+1] == '>' && (s[j+2] == '[' || s[j+2] == '{')) ? 2 : 0;
        int k = j + arrow;
        if (k < len && s[k] == '[') {
            int e = match_bracket(s, len, k);
            if (e < 0) break;
            int a = k + 1, b = e - 1;      /* content s[a..b) */
            int ok = 0;
            if (a < b && (s[a] == '-' || isdigit((unsigned char)s[a]))) { ok = 1; for (int q = a + (s[a] == '-'); q < b; q++) if (!isdigit((unsigned char)s[q])) ok = 0; }
            else if (a < b && s[a] == '$') { ok = 1; for (int q = a + 1; q < b; q++) if (!is_ident_char((unsigned char)s[q])) ok = 0; }
            if (!ok) break;
            j = e;
        }
        else if (k < len && s[k] == '{') {
            int e = match_bracket(s, len, k);
            if (e < 0) break;
            if (!arrow) {           /* {2} / {2,} / {2,5} is a quantifier */
                int q = k + 1, digits = 0;
                while (q < e - 1 && isdigit((unsigned char)s[q])) { q++; digits++; }
                if (q < e - 1 && s[q] == ',') { q++; while (q < e - 1 && isdigit((unsigned char)s[q])) q++; }
                if (digits && q == e - 1) break;
            }
            j = e;
        }
        else break;
    }
    return j;
}

static pp_node *builtin_call1(pp_parser *P, const char *name, pp_node *arg) {
    pp_node *b = node(P, N_BUILTIN);
    b->op = (uint8_t)pp_builtin_id(name, (int)strlen(name), NULL);
    node_add(b, arg);
    return b;
}

/* pattern body -> N_CONST holding a compiled regex (constant pattern) or an N_INTERP whose
   parts are joined and compiled at run time. Backslashes are kept for the regex parser;
   $var / @var interpolate as in strings; \Q...\E quote what they enclose. */
static pp_node *pattern_node(pp_parser *P, const char *s, int len, int delim, int cflags) {
    pp_node *r = node_tmp(P, N_INTERP);
    char *buf = NULL; int n = 0, cap = 0;
    int quoting = 0, dyn = 0, incls = 0;
    int line = P->cur.line;
    for (int i = 0; i < len; ) {
        int c = (unsigned char)s[i];
        if ((cflags & PP_RE_X) && !quoting) {
            /* under /x a comment runs to the end of the line and nothing in it interpolates
               ("# $quote" names no variable); a '#' inside [...] is an ordinary character */
            if (c == '[' && !incls) { incls = 1; append_char(&buf, &n, &cap, c); i++; if (i < len && s[i] == '^') append_char(&buf, &n, &cap, s[i++]); if (i < len && s[i] == ']') append_char(&buf, &n, &cap, s[i++]); continue; }
            if (c == ']') incls = 0;
            if (c == '#' && !incls) {
                while (i < len && s[i] != '\n') append_char(&buf, &n, &cap, s[i++]);
                continue;
            }
        }
        if (c == '\\' && i + 1 < len) {
            int d = (unsigned char)s[i+1];
            if (delim != '\'' && d == 'Q') { quoting = 1; i += 2; continue; }
            if (delim != '\'' && d == 'E') { quoting = 0; i += 2; continue; }
            if (quoting) { append_char(&buf, &n, &cap, '\\'); append_char(&buf, &n, &cap, '\\'); if (!isalnum(d) && d != '_') append_char(&buf, &n, &cap, '\\'); append_char(&buf, &n, &cap, d); }
            else if (d == delim && !isalnum(delim)) append_char(&buf, &n, &cap, d);   /* \/ in m/.../ is just / */
            else { append_char(&buf, &n, &cap, c); append_char(&buf, &n, &cap, d); }
            i += 2; continue;
        }
        if (delim != '\'' && (c == '$' || c == '@')) {
            int e = re_interp_extent(s, len, i);
            if (e > i + 1) {
                if (n) { node_add(r, const_str(P, buf, n)); n = 0; }
                pp_node *ex = parse_interp_expr(P, s + i, e - i, line);
                if (c == '@') { pp_node *j = node(P, N_BUILTIN); j->op = (uint8_t)pp_builtin_id("join", 4, NULL); node_add(j, const_str(P, " ", 1)); node_add(j, ex); ex = j; }
                if (quoting) ex = builtin_call1(P, "quotemeta", ex);
                node_add(r, ex);
                dyn = 1; i = e; continue;
            }
        }
        if (c == '\n') line++;
        if (quoting && !isalnum(c) && c != '_') append_char(&buf, &n, &cap, '\\');
        append_char(&buf, &n, &cap, c);
        i++;
    }
    if (!dyn) {
        char err[96];
        pp_regex *re = pp_re_compile(buf ? buf : "", n, cflags, err, sizeof err);
        if (!re) { char pat[64]; snprintf(pat, sizeof pat, "%.*s", n > 60 ? 60 : n, buf ? buf : ""); free(buf); drop_shell(r); perr(P, "%s in m/%s/", err, pat); }
        free(buf);
        drop_shell(r);
        pp_obj *o = pp_obj_new(PP_OREGEX);
        o->u.re = re;
        pp_node *k = node(P, N_CONST);
        k->val = pp_ref(o);
        pp_obj_decref(o);
        return k;
    }
    if (n) node_add(r, const_str(P, buf, n));
    free(buf);
    return adopt(P, r);
}

/* tr/// list: expand escapes and a-z ranges into out (up to 256 bytes) */
/* one character of a tr list at s[*i], escapes included (\n \t \\ \- \012 \x41 \x{41}) */
static int tr_char(const char *s, int len, int *i) {
    int c = (unsigned char)s[(*i)++];
    if (c != '\\' || *i >= len) return c;
    int d = (unsigned char)s[(*i)++];
    switch (d) {
    case 'n': return '\n'; case 't': return '\t'; case 'r': return '\r'; case 'f': return '\f'; case 'e': return 27; case 'a': return 7;
    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
        int v = d - '0', k = 1;
        while (k < 3 && *i < len && s[*i] >= '0' && s[*i] <= '7') { v = v * 8 + (s[*i] - '0'); (*i)++; k++; }
        return v & 255;
    }
    case 'x': {
        int v = 0;
        if (*i < len && s[*i] == '{') { (*i)++; while (*i < len && s[*i] != '}') { v = v * 16 + hexval(s[*i]); (*i)++; } if (*i < len) (*i)++; }
        else { int k = 0; while (k < 2 && *i < len && isxdigit((unsigned char)s[*i])) { v = v * 16 + hexval(s[*i]); (*i)++; k++; } }
        return v > 255 ? 255 : v;
    }
    default: return d;
    }
}
static int tr_expand(const char *s, int len, unsigned char *out) {
    int n = 0;
    for (int i = 0; i < len && n < 256; ) {
        int c = tr_char(s, len, &i);
        if (i + 1 < len && s[i] == '-') {
            i++;
            int hi = tr_char(s, len, &i);
            for (int k = c; k <= hi && n < 256; k++) out[n++] = (unsigned char)k;
        } else out[n++] = (unsigned char)c;
    }
    return n;
}

static pp_node *parse_pattern_term(pp_parser *P) {
    pp_tok t = P->cur;
    int line = t.line;
    advance(P);
    int cflags = 0, op = 0, trop = 0;
    for (const char *f = t.flags; *f; f++) {
        switch (*f) {
        case 'i': cflags |= PP_RE_I; break;
        case 'm': cflags |= PP_RE_M; break;
        case 'x': cflags |= PP_RE_X; break;
        case 'o': break;
        case 's': if (t.kind == 't') trop |= PP_TR_S; else cflags |= PP_RE_S; break;
        case 'g': op |= PP_M_G; break;
        case 'e': op |= PP_M_E; break;
        case 'r': op |= PP_M_R; trop |= PP_TR_R; break;
        case 'c': trop |= PP_TR_C; break;
        case 'd': trop |= PP_TR_D; break;
        default: perr(P, "Unknown regexp modifier \"/%c\"", *f);
        }
    }
    if (t.kind == 't') {
        unsigned char a[256], b[256];
        int na = tr_expand(t.s, t.len, a), nb = tr_expand(t.s2, t.len2, b);
        pp_node *n = node(P, N_TRANS); n->line = (uint16_t)line;
        n->op = (uint8_t)trop;
        n->b = const_str(P, (const char *)a, na);
        n->c = const_str(P, (const char *)b, nb);
        return n;
    }
    pp_node *pat = pattern_node(P, t.s, t.len, t.delim, cflags);
    if (t.kind == 'q') {
        if (pat->type == N_CONST) return pat;
        pp_node *n = node(P, N_MATCH); n->line = (uint16_t)line; n->b = pat; n->op = PP_M_QR; n->aux = (uint8_t)cflags;
        return n;
    }
    if (t.kind == 'm') {
        pp_node *n = node(P, N_MATCH); n->line = (uint16_t)line; n->b = pat; n->op = (uint8_t)op; n->aux = (uint8_t)cflags;
        return n;
    }
    /* s/// */
    pp_node *n = node(P, N_SUBST); n->line = (uint16_t)line; n->b = pat; n->op = (uint8_t)op; n->aux = (uint8_t)cflags;
    if (op & PP_M_E) {
        int err = 0;
        /* the replacement is code: one expression, or several statements (the last one's value) */
        pp_node *code = parse_expr_shared(P, t.s2, t.len2, line | (memchr(t.s2, ';', (size_t)t.len2) ? PARSE_AS_BLOCK : 0), &err);
        if (err || !code) longjmp(P->jb, 1);
        n->c = code;
    }
    else if (t.delim == '\'') n->c = single_quoted(P, t.s2, t.len2);
    else n->c = double_quoted(P, t.s2, t.len2);
    return n;
}

static pp_node *qw_list(pp_parser *P, const char *s, int len) {
    pp_node *r = node(P, N_LIST);
    int i = 0;
    while (i < len) {
        while (i < len && isspace((unsigned char)s[i])) i++;
        int st = i;
        while (i < len && !isspace((unsigned char)s[i])) i++;
        if (i > st) node_add(r, const_str(P, s + st, i - st));
    }
    return r;
}

/* ------------------------------------------------------------------ */
/* Expressions                                                         */
/* ------------------------------------------------------------------ */

/* perl's while-condition magic: `while (<$fh>)` reads into $_ and tests defined;
   an assignment from readline also gets the implicit defined(). */
static pp_node *while_cond_magic(pp_parser *P, pp_node *c) {
    int rl = pp_builtin_id("readline", 8, NULL);
    pp_node *inner = c;
    if (inner->type == N_LIST && inner->n == 1) inner = inner->list[0];
    if (inner->type == N_BUILTIN && inner->op == rl) {
        pp_node *a = node(P, N_ASSIGN);
        a->a = mkvar(P, '$', "_", 1);
        a->b = inner;
        inner = a;
    }
    else if (!(inner->type == N_ASSIGN && inner->op == OP_NONE && inner->b && inner->b->type == N_BUILTIN && inner->b->op == rl))
        return c;
    pp_node *d = node(P, N_BUILTIN);
    d->op = (uint8_t)pp_builtin_id("defined", 7, NULL);
    node_add(d, inner);
    return d;
}

#define OP_MATCH  100
#define OP_NMATCH 101

static int binop_of(pp_tok *t, int *prec, int *rassoc) {
    *rassoc = 0;
    if (t->type == T_OP) {
        const char *o = t->op;
        if (!strcmp(o, "**")) { *prec = 15; *rassoc = 1; return OP_POW; }
        if (!strcmp(o, "*")) { *prec = 13; return OP_MUL; }
        if (!strcmp(o, "/")) { *prec = 13; return OP_DIV; }
        if (!strcmp(o, "%")) { *prec = 13; return OP_MOD; }
        if (!strcmp(o, "+")) { *prec = 12; return OP_ADD; }
        if (!strcmp(o, "-")) { *prec = 12; return OP_SUB; }
        if (!strcmp(o, ".")) { *prec = 12; return OP_CONCAT; }
        if (!strcmp(o, "<<")) { *prec = 11; return OP_SHL; }
        if (!strcmp(o, ">>")) { *prec = 11; return OP_SHR; }
        if (!strcmp(o, "<")) { *prec = 9; return OP_NUM_LT; }
        if (!strcmp(o, ">")) { *prec = 9; return OP_NUM_GT; }
        if (!strcmp(o, "<=")) { *prec = 9; return OP_NUM_LE; }
        if (!strcmp(o, ">=")) { *prec = 9; return OP_NUM_GE; }
        if (!strcmp(o, "==")) { *prec = 8; return OP_NUM_EQ; }
        if (!strcmp(o, "!=")) { *prec = 8; return OP_NUM_NE; }
        if (!strcmp(o, "<=>")) { *prec = 8; return OP_NUM_CMP; }
        if (!strcmp(o, "&")) { *prec = 7; return OP_BAND; }
        if (!strcmp(o, "|")) { *prec = 6; return OP_BOR; }
        if (!strcmp(o, "^")) { *prec = 6; return OP_BXOR; }
        if (!strcmp(o, "&&")) { *prec = 5; return OP_LOGAND; }
        if (!strcmp(o, "||")) { *prec = 4; return OP_LOGOR; }
        if (!strcmp(o, "//")) { *prec = 4; return OP_DEFOR; }
        if (!strcmp(o, "^^")) { *prec = 4; return OP_LOGXOR; }
        if (!strcmp(o, "=~")) { *prec = 14; return OP_MATCH; }
        if (!strcmp(o, "!~")) { *prec = 14; return OP_NMATCH; }
        return 0;
    }
    if (t->type == T_IDENT) {
        if (tok_is_ident(t, "x")) { *prec = 13; return OP_REPEAT; }
        if (tok_is_ident(t, "lt")) { *prec = 9; return OP_STR_LT; }
        if (tok_is_ident(t, "gt")) { *prec = 9; return OP_STR_GT; }
        if (tok_is_ident(t, "le")) { *prec = 9; return OP_STR_LE; }
        if (tok_is_ident(t, "ge")) { *prec = 9; return OP_STR_GE; }
        if (tok_is_ident(t, "eq")) { *prec = 8; return OP_STR_EQ; }
        if (tok_is_ident(t, "ne")) { *prec = 8; return OP_STR_NE; }
        if (tok_is_ident(t, "cmp")) { *prec = 8; return OP_STR_CMP; }
        if (tok_is_ident(t, "isa")) { *prec = 10; return OP_ISA; }
    }
    return 0;
}

#define PREC_NAMED_UNARY 10

/* comparison operators are non-associative: `$a < $b < $c` is a syntax error in perl */
static int cmp_prec_of(int op) {
    switch (op) {
    case OP_NUM_LT: case OP_NUM_GT: case OP_NUM_LE: case OP_NUM_GE:
    case OP_STR_LT: case OP_STR_GT: case OP_STR_LE: case OP_STR_GE: return 9;
    case OP_NUM_EQ: case OP_NUM_NE: case OP_NUM_CMP:
    case OP_STR_EQ: case OP_STR_NE: case OP_STR_CMP: return 8;
    default: return 0;
    }
}

static pp_node *parse_binary(pp_parser *P, int minprec) {
    pp_node *lhs = parse_unary(P);
    for (;;) {
        int prec, rassoc;
        int op = binop_of(&P->cur, &prec, &rassoc);
        if (!op || prec < minprec) break;
        if ((prec == 8 || prec == 9) && lhs->type == N_BINOP && cmp_prec_of(lhs->op) == prec) {
            if (P->cur.type == T_OP) perr(P, "syntax error near \"%s\" (comparison operators are non-associative)", P->cur.op);
            perr(P, "syntax error near \"%.*s\" (comparison operators are non-associative)", P->cur.len, P->cur.s);
        }
        /* `x=` assignment */
        if (op == OP_REPEAT && peek(P)->type == T_OP && !strcmp(peek(P)->op, "=") && !peek(P)->space_before) break;
        advance(P);
        pp_node *rhs;
        if (op == OP_ISA && P->cur.type == T_IDENT) {   /* $x isa Foo::Bar: bareword class name */
            rhs = const_str(P, P->cur.s, P->cur.len);
            advance(P);
        }
        else rhs = parse_binary(P, rassoc ? prec : prec + 1);
        pp_node *n;
        if (op == OP_MATCH || op == OP_NMATCH) {
            if ((rhs->type == N_MATCH || rhs->type == N_SUBST || rhs->type == N_TRANS) && !rhs->a && !(rhs->type == N_MATCH && (rhs->op & PP_M_QR))) { rhs->a = lhs; n = rhs; if (n->type != N_MATCH && lhs->type == N_AELEM) note_argw(P, lhs); }
            else { n = node(P, N_MATCH); n->a = lhs; n->b = rhs; }
            if (op == OP_NMATCH) {
                if (n->type == N_TRANS || (n->type == N_SUBST && (n->op & PP_M_R))) perr(P, "Using !~ with %s doesn't make sense", n->type == N_TRANS ? "tr///" : "s///r");
                n->op |= PP_M_NEG;
            }
            lhs = n;
            continue;
        }
        if (op == OP_LOGAND) n = node(P, N_LOGAND);
        else if (op == OP_LOGOR) n = node(P, N_LOGOR);
        else if (op == OP_DEFOR) n = node(P, N_DEFOR);
        else if (op == OP_REPEAT) { n = node(P, N_REPEAT); n->op = (lhs->type == N_LIST && (lhs->flags & 1)) ? 1 : 0; }
        else {
            n = node(P, N_BINOP); n->op = (uint8_t)op;
            if (op == OP_ADD || op == OP_SUB || op == OP_MUL || op == OP_DIV || op == OP_MOD || op == OP_POW
                || (op >= OP_NUM_EQ && op <= OP_NUM_CMP)) {       /* $! in arithmetic is the error number */
                if (lhs->type == N_VAR && (lhs->flags & PP_VAR_ERRNO)) lhs->flags |= PP_VAR_ERRNUM;
                if (rhs->type == N_VAR && (rhs->flags & PP_VAR_ERRNO)) rhs->flags |= PP_VAR_ERRNUM;
            }
        }
        n->a = lhs; n->b = rhs;
        lhs = n;
    }
    return lhs;
}

static pp_node *parse_range(pp_parser *P) {
    pp_node *lhs = parse_binary(P, 0);
    if (cur_is_op(P, "..") || cur_is_op(P, "...")) {
        advance(P);
        pp_node *rhs = parse_binary(P, 0);
        pp_node *n = node(P, N_RANGE); n->a = lhs; n->b = rhs;
        return n;
    }
    return lhs;
}

static pp_node *parse_ternary(pp_parser *P) {
    pp_node *c = parse_range(P);
    if (cur_is_op(P, "?")) {
        advance(P);
        pp_node *n = node(P, N_TERNARY);
        n->a = c;
        n->b = parse_assign(P);
        expect_op(P, ":");
        n->c = parse_assign(P);
        return n;
    }
    return c;
}

static int assign_op_of(pp_parser *P) {
    pp_tok *t = &P->cur;
    if (t->type == T_IDENT && tok_is_ident(t, "x") && peek(P)->type == T_OP && !strcmp(peek(P)->op, "=") && !peek(P)->space_before) return OP_REPEAT;
    if (t->type != T_OP) return -1;
    const char *o = t->op;
    if (!strcmp(o, "=")) return OP_NONE;
    if (!strcmp(o, "+=")) return OP_ADD;
    if (!strcmp(o, "-=")) return OP_SUB;
    if (!strcmp(o, "*=")) return OP_MUL;
    if (!strcmp(o, "/=")) return OP_DIV;
    if (!strcmp(o, "%=")) return OP_MOD;
    if (!strcmp(o, "**=")) return OP_POW;
    if (!strcmp(o, ".=")) return OP_CONCAT;
    if (!strcmp(o, "|=")) return OP_BOR;
    if (!strcmp(o, "&=")) return OP_BAND;
    if (!strcmp(o, "^=")) return OP_BXOR;
    if (!strcmp(o, "<<=")) return OP_SHL;
    if (!strcmp(o, ">>=")) return OP_SHR;
    if (!strcmp(o, "||=")) return OP_LOGOR;
    if (!strcmp(o, "&&=")) return OP_LOGAND;
    if (!strcmp(o, "//=")) return OP_DEFOR;
    if (!strcmp(o, "^^=")) return OP_LOGXOR;
    return -1;
}

/* note a write to an argument: $_[N] = ..., $_[N] =~ s///, $_[N]++, chomp($_[N]), for (@_).
   picoperl passes copies; the caller of such a sub copies the changed values back. */
static void note_argw(pp_parser *P, pp_node *t) {
    if (!t) return;
    if (t->type == N_LIST) { for (int i = 0; i < t->n; i++) note_argw(P, t->list[i]); return; }
    if (t->type == N_AELEM) t = t->a;
    if (t->type == N_VAR && t->sym == P->I->sym_args) P->argw = 1;
}
static pp_node *parse_assign(pp_parser *P) {
    pp_node *lhs = parse_ternary(P);
    int op = assign_op_of(P);
    if (op == OP_NONE && lhs->type == N_BUILTIN && (lhs->n == 2 || lhs->n == 3)
        && lhs->op == (uint8_t)pp_builtin_id("substr", 6, NULL)) {
        /* substr($s, OFF, LEN) = VALUE is the 4-argument substr($s, OFF, LEN, VALUE);
           without LEN it replaces to the end of the string */
        advance(P);
        if (lhs->n == 2) node_add(lhs, const_int(P, 0x7fffffff));
        node_add(lhs, parse_assign(P));
        return lhs;
    }
    if (op == OP_NONE && lhs->type == N_LIST) {
        /* (undef, $x) = LIST: an undef on the left skips one value */
        for (int i = 0; i < lhs->n; i++) {
            pp_node *c = lhs->list[i];
            if (c->type == N_BUILTIN && c->n == 0 && c->op == (uint8_t)pp_builtin_id("undef", 5, NULL)) c->type = N_NOP;
        }
    }
    if (op == OP_NONE && ((lhs->type == N_BUILTIN && lhs->n <= 1 && lhs->op == (uint8_t)pp_builtin_id("pos", 3, NULL)) || lhs->type == N_LASTIDX)) {
        /* pos($s) = N is __pos_set(\$s, N); $#a = N is __lastidx_set(\@a, N) */
        int is_pos = lhs->type == N_BUILTIN;
        advance(P);
        pp_node *c = node(P, N_CALL); c->line = lhs->line;
        c->sym = is_pos ? pp_intern(P->I, "&__pos_set", 10) : pp_intern(P->I, "&__lastidx_set", 14);
        pp_node *r = node(P, N_REF);
        r->a = !is_pos ? lhs->a : lhs->n ? lhs->list[0] : mkvar(P, '$', "_", 1);
        node_add(c, r);
        node_add(c, parse_assign(P));
        return c;
    }
    if (op == OP_NONE && lhs->type == N_CALL && lhs->n == 3 && lhs->sym && !strcmp(lhs->sym->name, "&vec")) {
        /* vec($s, OFF, BITS) = VALUE is $s = __vec_set($s, OFF, BITS, VALUE) */
        advance(P);
        pp_node *n = node(P, N_ASSIGN);
        n->op = OP_NONE;
        n->a = lhs->list[0];
        lhs->sym = pp_intern(P->I, "&__vec_set", 10);
        node_add(lhs, parse_assign(P));
        n->b = lhs;
        return n;
    }
    if (op >= 0) {
        if (op == OP_REPEAT) advance(P);
        advance(P);
        pp_node *n = node(P, N_ASSIGN);
        n->op = (uint8_t)op;
        n->a = lhs;
        if (lhs->type == N_AELEM || lhs->type == N_LIST) note_argw(P, lhs);
        n->b = parse_assign(P);
        return n;
    }
    return lhs;
}

static int at_list_end(pp_parser *P) {
    pp_tok *t = &P->cur;
    if (t->type == T_EOF) return 1;
    if (t->type == T_OP) {
        const char *o = t->op;
        return !strcmp(o, ")") || !strcmp(o, "]") || !strcmp(o, "}") || !strcmp(o, ";") || !strcmp(o, ":") || !strcmp(o, "?") || !strcmp(o, "=");
    }
    if (t->type == T_IDENT) {
        pp_tok *nx = peek(P);
        if (nx->type == T_OP && !strcmp(nx->op, "=>")) return 0;
        return tok_is_ident(t, "if") || tok_is_ident(t, "unless") || tok_is_ident(t, "while") || tok_is_ident(t, "until")
            || tok_is_ident(t, "for") || tok_is_ident(t, "foreach") || tok_is_ident(t, "or") || tok_is_ident(t, "and")
            || tok_is_ident(t, "xor") || tok_is_ident(t, "x") || tok_is_ident(t, "eq") || tok_is_ident(t, "ne")
            || tok_is_ident(t, "lt") || tok_is_ident(t, "gt") || tok_is_ident(t, "le") || tok_is_ident(t, "ge") || tok_is_ident(t, "cmp");
    }
    return 0;
}

static pp_node *parse_comma_list(pp_parser *P) {
    pp_node *l = node_tmp(P, N_LIST);
    while (!at_list_end(P)) {
        node_add(l, parse_assign(P));
        if (cur_is_op(P, ",") || cur_is_op(P, "=>")) { advance(P); continue; }
        break;
    }
    return l;
}

static pp_node *parse_not(pp_parser *P) {
    if (cur_is_ident(P, "not") && !is_op(peek(P), "=>") && !is_op(peek(P), "(")) {   /* not(...) is a term: (not(1), 2) has two elements */
        advance(P);
        pp_node *n = node(P, N_NOT);
        n->a = parse_not(P);
        return n;
    }
    pp_node *l = parse_comma_list(P);
    if (l->n == 1 && !(l->flags & 1)) return unwrap_single(l);
    return adopt(P, l);
}
static pp_node *parse_and(pp_parser *P) {
    pp_node *lhs = parse_not(P);
    while (cur_is_ident(P, "and")) {
        advance(P);
        pp_node *n = node(P, N_LOGAND); n->a = lhs; n->b = parse_not(P); lhs = n;
    }
    return lhs;
}
static pp_node *parse_expr(pp_parser *P) {
    pp_node *lhs = parse_and(P);
    while (cur_is_ident(P, "or") || cur_is_ident(P, "xor")) {
        int is_xor = cur_is_ident(P, "xor");
        advance(P);
        pp_node *rhs = parse_and(P);
        pp_node *n;
        if (is_xor) { n = node(P, N_BINOP); n->op = OP_NUM_NE; pp_node *na = node(P, N_NOT); na->a = lhs; pp_node *nb = node(P, N_NOT); nb->a = rhs; n->a = na; n->b = nb; lhs = n; continue; }
        n = node(P, N_LOGOR);
        n->a = lhs; n->b = rhs; lhs = n;
    }
    return lhs;
}

static pp_node *parse_unary(pp_parser *P) {
    pp_tok *t = &P->cur;
    if (t->type == T_OP) {
        if (!strcmp(t->op, "!")) { advance(P); pp_node *n = node(P, N_NOT); n->a = parse_unary(P); return n; }
        if (t->op[0] == '-' && isalpha((unsigned char)t->op[1]) && !t->op[2]) {   /* file test: a named unary operator */
            if (t->op[1] == 't') {   /* -t HANDLE: is it a terminal? (a call to the native __isatty) */
                pp_node *c = node(P, N_CALL); c->sym = pp_intern(P->I, "&__isatty", 9);
                advance(P);
                if (P->cur.type == T_IDENT && is_handle_name(P->cur.s, P->cur.len)) { node_add(c, handle_const(P, P->cur.s, P->cur.len)); advance(P); }
                else node_add(c, parse_binary(P, PREC_NAMED_UNARY));
                return c;
            }
            pp_node *n = node(P, N_FILETEST); n->op = (uint8_t)t->op[1];
            advance(P);
            n->a = parse_binary(P, PREC_NAMED_UNARY);
            return n;
        }
        if (!strcmp(t->op, "-")) {
            advance(P);
            if (P->cur.type == T_IDENT && !P->cur.space_before) {
                pp_tok *nx = peek(P);
                int kind;
                if (!(nx->type == T_OP && !strcmp(nx->op, "(")) && pp_builtin_id(P->cur.s, P->cur.len, &kind) <= 0 && !is_declared(P, sym_sigil(P, '&', P->cur.s, P->cur.len))) {
                    char buf[128]; int len = P->cur.len; if (len > 126) len = 126;
                    buf[0] = '-'; memcpy(buf + 1, P->cur.s, len);
                    pp_node *n = const_str(P, buf, len + 1);
                    advance(P);
                    return n;
                }
            }
            pp_node *n = node(P, N_NEG); n->a = parse_binary(P, 15);
            if (n->a->type == N_VAR && (n->a->flags & PP_VAR_ERRNO)) n->a->flags |= PP_VAR_ERRNUM;
            if (n->a->type == N_CONST && n->a->val.t == PP_INT) { n->a->val.u.i = -n->a->val.u.i; return n->a; }
            if (n->a->type == N_CONST && n->a->val.t == PP_NUM) { n->a->val.u.n = -n->a->val.u.n; return n->a; }
            return n;
        }
        if (!strcmp(t->op, "+")) { advance(P); return parse_unary(P); }
        if (!strcmp(t->op, "~")) { advance(P); pp_node *n = node(P, N_BINOP); n->op = OP_BXOR; n->a = const_int(P, -1); n->b = parse_unary(P); return n; }
        if (!strcmp(t->op, "\\")) { advance(P); pp_node *n = node(P, N_REF); n->a = parse_unary(P); return n; }
        if (!strcmp(t->op, "++")) { advance(P); pp_node *n = node(P, N_PREINC); n->a = parse_unary(P); if (n->a->type == N_AELEM) note_argw(P, n->a); return n; }
        if (!strcmp(t->op, "--")) { advance(P); pp_node *n = node(P, N_PREDEC); n->a = parse_unary(P); if (n->a->type == N_AELEM) note_argw(P, n->a); return n; }
    }
    pp_node *n = parse_term(P);
    if (cur_is_op(P, "**")) {
        /* ** binds tighter than unary minus: handled here so -2**2 == -4 */
        advance(P);
        pp_node *m = node(P, N_BINOP); m->op = OP_POW; m->a = n; m->b = parse_unary(P);
        n = m;
    }
    if (cur_is_op(P, "++")) { advance(P); pp_node *m = node(P, N_POSTINC); m->a = n; if (n->type == N_AELEM) note_argw(P, n); return m; }
    if (cur_is_op(P, "--")) { advance(P); pp_node *m = node(P, N_POSTDEC); m->a = n; if (n->type == N_AELEM) note_argw(P, n); return m; }
    return n;
}

static pp_node *parse_hash_key(pp_parser *P) {
    if (P->cur.type == T_IDENT) {
        pp_tok *nx = peek(P);
        if (nx->type == T_OP && !strcmp(nx->op, "}")) {
            pp_node *k = const_str(P, P->cur.s, P->cur.len);
            advance(P);
            return k;
        }
    }
    if (cur_is_op(P, "-") && peek(P)->type == T_IDENT) {
        advance(P);
        char buf[128]; int len = P->cur.len; if (len > 126) len = 126;
        buf[0] = '-'; memcpy(buf + 1, P->cur.s, len);
        pp_tok *nx = peek(P);
        if (nx->type == T_OP && !strcmp(nx->op, "}")) { pp_node *k = const_str(P, buf, len + 1); advance(P); return k; }
        perr(P, "syntax error in hash subscript");
    }
    return parse_expr(P);
}

static pp_node *parse_subscripts(pp_parser *P, pp_node *base, int allow_implicit) {
    for (;;) {
        if (allow_implicit && cur_is_op(P, "(") && (base->type == N_AELEM || base->type == N_HELEM || base->type == N_CALLREF)) {
            /* $h->{code}(args), $a[0](args), $f->(1)(2): the arrow between subscripts is optional before ( too */
            advance(P);
            pp_node *n = node(P, N_CALLREF); n->a = base;
            pp_node *args = parse_comma_list(P);
            expect_op(P, ")");
            take_children(n, args);
            base = n; continue;
        }
        if (cur_is_op(P, "->")) {
            pp_tok *nx = peek(P);
            if (nx->type == T_OP && !strcmp(nx->op, "[")) {
                advance(P); advance(P);
                pp_node *av = node(P, N_RV2AV); av->a = base;
                pp_node *n = node(P, N_AELEM); n->a = av; n->b = parse_expr(P);
                expect_op(P, "]");
                base = n; allow_implicit = 1; continue;
            }
            if (nx->type == T_OP && !strcmp(nx->op, "{")) {
                advance(P); advance(P);
                pp_node *hv = node(P, N_RV2HV); hv->a = base;
                pp_node *n = node(P, N_HELEM); n->a = hv; n->b = parse_hash_key(P);
                expect_op(P, "}");
                base = n; allow_implicit = 1; continue;
            }
            if (nx->type == T_OP && !strcmp(nx->op, "(")) {
                advance(P); advance(P);
                pp_node *n = node(P, N_CALLREF); n->a = base;
                pp_node *args = parse_comma_list(P);
                expect_op(P, ")");
                take_children(n, args);
                base = n; allow_implicit = 1; continue;
            }
            if (nx->type == T_OP && !strcmp(nx->op, "@")) {
                advance(P); advance(P); expect_op(P, "*");
                pp_node *av = node(P, N_RV2AV); av->a = base; base = av; allow_implicit = 0; continue;
            }
            if (nx->type == T_OP && !strcmp(nx->op, "%")) {
                advance(P); advance(P); expect_op(P, "*");
                pp_node *hv = node(P, N_RV2HV); hv->a = base; base = hv; allow_implicit = 0; continue;
            }
            if (nx->type == T_CAST) {
                advance(P);
                if (!strcmp(P->cur.op, "$")) { advance(P); expect_op(P, "*"); pp_node *sv = node(P, N_RV2SV); sv->a = base; base = sv; allow_implicit = 0; continue; }
                if (!strcmp(P->cur.op, "$#")) { advance(P); expect_op(P, "*"); pp_node *av = node(P, N_RV2AV); av->a = base; pp_node *li = node(P, N_LASTIDX); li->a = av; base = li; allow_implicit = 0; continue; }
                perr(P, "syntax error after '->'");
            }
            if (nx->type == T_IDENT || (nx->type == T_VAR && nx->sigil == '$')) {
                /* $obj->method(args) / Class->method / $obj->$name(args) */
                advance(P);
                pp_node *n = node(P, N_METHOD); n->a = base;
                if (P->cur.type == T_IDENT) n->sym = pp_intern(P->I, P->cur.s, P->cur.len);
                else n->b = mkvar(P, '$', P->cur.s, P->cur.len);
                n->c = const_str(P, P->pkg, (int)strlen(P->pkg));
                advance(P);
                if (cur_is_op(P, "(")) { advance(P); pp_node *args = parse_comma_list(P); expect_op(P, ")"); take_children(n, args); }
                base = n; allow_implicit = 0; continue;
            }
            perr(P, "syntax error after '->'");
        }
        if (allow_implicit && cur_is_op(P, "[")) {
            advance(P);
            pp_node *av = node(P, N_RV2AV); av->a = base;
            pp_node *n = node(P, N_AELEM); n->a = av; n->b = parse_expr(P);
            expect_op(P, "]");
            base = n; continue;
        }
        if (allow_implicit && cur_is_op(P, "{")) {
            advance(P);
            pp_node *hv = node(P, N_RV2HV); hv->a = base;
            pp_node *n = node(P, N_HELEM); n->a = hv; n->b = parse_hash_key(P);
            expect_op(P, "}");
            base = n; continue;
        }
        return base;
    }
}

static pp_node *parse_postfix(pp_parser *P, pp_node *n) { return parse_subscripts(P, n, 0); }

static pp_node *parse_scalar_var_subscript(pp_parser *P, const char *name, int len) {
    if (cur_is_op(P, "[")) {
        advance(P);
        pp_node *av = mkvar(P, '@', name, len);
        pp_node *n = node(P, N_AELEM); n->a = av; n->b = parse_expr(P);
        expect_op(P, "]");
        return parse_subscripts(P, n, 1);
    }
    if (cur_is_op(P, "{")) {
        advance(P);
        pp_node *hv = mkvar(P, '%', name, len);
        pp_node *n = node(P, N_HELEM); n->a = hv; n->b = parse_hash_key(P);
        expect_op(P, "}");
        return parse_subscripts(P, n, 1);
    }
    pp_node *v = mkvar(P, '$', name, len);
    return parse_subscripts(P, v, 0);
}

static pp_node *parse_cast_target(pp_parser *P) {
    if (cur_is_op(P, "{")) {
        advance(P);
        pp_node *e = parse_expr(P);
        expect_op(P, "}");
        return e;
    }
    if (P->cur.type == T_VAR && P->cur.sigil == '$') {
        pp_node *v = mkvar(P, '$', P->cur.s, P->cur.len);
        advance(P);
        return v;
    }
    if (P->cur.type == T_CAST && !strcmp(P->cur.op, "$")) {
        advance(P);
        pp_node *inner = parse_cast_target(P);
        pp_node *sv = node(P, N_RV2SV); sv->a = inner;
        return sv;
    }
    perr(P, "syntax error after sigil");
    return NULL;
}

/* FH, STDERR, MY_LOG2: the way file handles are written as barewords */
static int is_handle_name(const char *id, int len) {
    if (len < 1 || !isupper((unsigned char)id[0])) return 0;
    for (int i = 0; i < len; i++) if (!isupper((unsigned char)id[i]) && !isdigit((unsigned char)id[i]) && id[i] != '_') return 0;
    return 1;
}
/* the glob name of a bareword handle as a constant: "*Pkg::FH" */
static pp_node *handle_const(pp_parser *P, const char *id, int len) {
    char buf[264];
    int std = (len == 5 && !memcmp(id, "STDIN", 5)) || (len == 6 && (!memcmp(id, "STDOUT", 6) || !memcmp(id, "STDERR", 6)));
    int n = pp_snprintf(buf, sizeof buf, "*%s::%.*s", std ? "main" : P->pkg, len, id);
    return const_str(P, buf, n);
}
/* after *name: a {SLOT} subscript makes it *name{SLOT} */
static pp_node *glob_slot(pp_parser *P, pp_node *g) {
    if (!cur_is_op(P, "{") || P->cur.space_before) return g;
    advance(P);
    pp_node *n = node(P, N_GLOBSLOT); n->a = g; n->b = parse_hash_key(P);
    expect_op(P, "}");
    return parse_postfix(P, n);
}

static pp_node *decl_var(pp_parser *P, int kind, char sigil, const char *name, int len) {
    if (kind == 'm' || kind == 's') return mkvar_decl(P, sigil, name, len);
    if (kind == 'o') { pp_node *v = node(P, N_VAR); v->sym = declare_our(P, sigil, name, len); return v; }
    pp_node *v = mkvar(P, sigil, name, len);          /* local: an existing global */
    if (v->flags & PP_VAR_LEX) perr(P, "Can't localize lexical variable %s", v->sym->name);
    return v;
}
/* number the variables of a `state` declaration within the enclosing sub */
static void state_assign(pp_parser *P, pp_node *m) {
    if (P->state_n + m->n > 255) perr(P, "too many state variables in one sub");
    m->aux = (uint8_t)P->state_n;
    P->state_n += m->n;
}

static pp_node *parse_var_decl_list(pp_parser *P, int kind) {
    pp_node *m = node(P, N_MY); m->op = (uint8_t)kind;
    if (cur_is_op(P, "(")) {
        advance(P);
        /* local($x, $h{k}, @a[1, 2]): once an element or slice shows up, the list is kept in
           source order, every plain variable in a `local` node of its own, so that a list
           assigned to it lines up */
        pp_node *mixed = NULL;
        while (!cur_is_op(P, ")")) {
            if (P->cur.type != T_VAR) {
                if (cur_is_ident(P, "undef")) { advance(P); node_add(mixed ? mixed : m, node(P, N_NOP)); }
                else perr(P, "syntax error in variable declaration");
            } else {
                pp_tok *nx = peek(P);
                if (kind == 'l' && (P->cur.sigil == '$' || P->cur.sigil == '@') && nx->type == T_OP && !nx->space_before
                    && (!strcmp(nx->op, "{") || !strcmp(nx->op, "[") || !strcmp(nx->op, "->"))) {
                    if (!mixed) {
                        mixed = node(P, N_LIST);
                        for (int i = 0; i < m->n; i++) {         /* the variables seen so far */
                            if (m->list[i]->type != N_VAR) { node_add(mixed, m->list[i]); continue; }
                            pp_node *one = node(P, N_MY); one->op = 'l'; node_add(one, m->list[i]);
                            node_add(mixed, one);
                        }
                        m->n = 0;
                    }
                    pp_node *e = parse_term(P);
                    if (e->type != N_HELEM && e->type != N_AELEM && e->type != N_HSLICE && e->type != N_ASLICE)
                        perr(P, "Can't localize this kind of expression");
                    pp_node *le = node(P, N_LOCALELEM); le->a = e;
                    node_add(mixed, le);
                } else {
                    pp_node *v = decl_var(P, kind, P->cur.sigil, P->cur.s, P->cur.len);
                    advance(P);
                    if (mixed) { pp_node *one = node(P, N_MY); one->op = 'l'; node_add(one, v); node_add(mixed, one); }
                    else node_add(m, v);
                }
            }
            if (!accept_op(P, ",")) break;
        }
        expect_op(P, ")");
        if (mixed) return mixed;
        m->flags |= 1;
        if (kind == 's') state_assign(P, m);
        return m;
    }
    if (P->cur.type != T_VAR) perr(P, "syntax error in variable declaration");
    char sigil = P->cur.sigil; const char *name = P->cur.s; int len = P->cur.len;
    advance(P);
    if (kind == 'l' && sigil == '$') { node_add(m, parse_scalar_var_subscript(P, name, len)); return m; }
    node_add(m, decl_var(P, kind, sigil, name, len));
    if (kind == 's') state_assign(P, m);
    return m;
}

/* The (...) after `sub`, with the parser at the first token past '('.
   Named parameters make it a perl signature: `($x, $y = default, @rest)` becomes
   an arity check plus `my` bindings from @_, prepended to the body block b
   (whose scope must already be open). Anything else is a prototype and is
   ignored, as before. Returns the number of statements added to b. */
/* at the '(' after `sub NAME`: if the parentheses hold a prototype (only sigils and the like:
   ($$) (&$) (\@\@) (;$) (_)), skip to the ')' in the source, because prototype text does not
   lex as perl code. Returns 1 if it did. An empty () is left to parse_sub_params. */
/* sub NAME : lvalue : method(...) { ... }: attributes are read and ignored */
static void skip_attributes(pp_parser *P) {
    while (cur_is_op(P, ":")) {
        advance(P);
        while (P->cur.type == T_IDENT) {
            advance(P);
            if (cur_is_op(P, "(") && !P->has_nxt) {
                pp_lex *L = &P->L;
                while (L->pos < L->len && L->src[L->pos] != ')') L->pos++;
                advance(P);              /* the ')' */
                advance(P);
            }
        }
    }
}
static int skip_prototype(pp_parser *P) {
    P->proto_mask = 0;
    if (P->has_nxt) return 0;
    pp_lex *L = &P->L;
    int i = L->pos, seen = 0;
    while (i < L->len && L->src[i] != ')') {
        if (!strchr("$@%&*;+\\[]_ \t", L->src[i])) return 0;
        if (L->src[i] != ' ' && L->src[i] != '\t') seen = 1;
        i++;
    }
    if (i >= L->len || !seen) return 0;
    int first = L->pos;
    P->proto_mask = 0;
    for (int j = L->pos, k = 0; j < i && k < 8; j++) {
        char c = L->src[j];
        if (c == ' ' || c == '\t' || c == ';') continue;
        if (c == '@' || c == '%') break;                       /* the rest is a list */
        if (c == '\\') { j++; if (j < i && L->src[j] == '[') while (j < i && L->src[j] != ']') j++; }
        else if (c == '$' || c == '_') P->proto_mask |= 1 << k;
        k++;
    }
    while (L->src[first] == ' ' || L->src[first] == '\t') first++;
    L->pos = i;
    return L->src[first] == '&' ? 2 : 1;   /* 2: the first argument is a block */
}
static int parse_sub_params(pp_parser *P, pp_node *b, const char *subname, int *noargs) {
    if (noargs) *noargs = cur_is_op(P, ")");
    /* a nameless '$' parameter reaches us as the variable "$," or "$)" (the comma or the
       closing parenthesis swallowed by the lexer) */
#define ANON_PARAM(P) (((P)->cur.type == T_VAR && (P)->cur.sigil == '$' && (P)->cur.len == 1 && ((P)->cur.s[0] == ',' || (P)->cur.s[0] == ')')) \
                       || ((P)->cur.type == T_CAST && !strcmp((P)->cur.op, "$")))
    if (!(P->cur.type == T_VAR && P->cur.len > 0 && is_ident_start((unsigned char)P->cur.s[0])) && !cur_is_op(P, ":") && !ANON_PARAM(P)) {
        while (!cur_is_op(P, ")") && P->cur.type != T_EOF) {   /* prototype: ignored */
            if (P->cur.type == T_VAR && P->cur.len > 0 && is_ident_start((unsigned char)P->cur.s[0]))
                perr(P, "unsupported subroutine signature");
            advance(P);
        }
        expect_op(P, ")");
        return 0;
    }
    /* arity check runs first; line 0 keeps the caller's line in messages, like perl */
    pp_node *chk = node(P, N_SIGCHECK);
    chk->line = 0;
    char full[160];
    if (strstr(subname, "::")) snprintf(full, sizeof full, "%s", subname);
    else snprintf(full, sizeof full, "%s::%s", P->pkg[0] ? P->pkg : "main", subname);
    chk->a = const_str(P, full, (int)strlen(full));
    node_add(b, chk);
    int idx = 0, min = -1, slurpy = 0, nstmt = 1, closed = 0;
    pp_node *named = NULL;
    while (!cur_is_op(P, ")")) {
        if (ANON_PARAM(P)) {                         /* ($, $x): an argument that is taken but not named */
            int bare = P->cur.type == T_CAST;        /* a lone '$': the ',' or ')' is still to come */
            closed = !bare && P->cur.s[0] == ')';
            idx++;
            if (!P->has_nxt) P->L.prev_term = 0;     /* what follows starts a term ("@rest") */
            advance(P);
            if (closed) break;
            if (bare && !accept_op(P, ",")) break;
            continue;
        }
        if (cur_is_op(P, ":")) {                     /* :$name / :$name = default (5.44) */
            if (!named) {
                named = node(P, N_NAMEDBIND);
                named->line = 0;                     /* messages report the caller's line */
                named->a = const_str(P, chk->a->val.u.s->d, chk->a->val.u.s->len);
                named->op = (uint8_t)idx;            /* pairs start after the positionals */
            }
            advance(P);
            if (P->cur.type != T_VAR || P->cur.sigil != '$' || !is_ident_start((unsigned char)P->cur.s[0]))
                perr(P, "syntax error in subroutine signature");
            pp_node *m = node(P, N_MY); m->op = 'm';
            node_add(m, decl_var(P, 'm', '$', P->cur.s, P->cur.len));
            advance(P);
            pp_node *ent = node(P, N_ASSIGN); ent->a = m; ent->b = NULL;
            if (accept_op(P, "=")) ent->b = parse_assign(P);
            node_add(named, ent);
            if (!accept_op(P, ",")) break;
            continue;
        }
        if (named) {                                 /* only a slurpy hash may follow named params */
            if (P->cur.type == T_VAR && P->cur.sigil == '%' && is_ident_start((unsigned char)P->cur.s[0])) {
                pp_node *m = node(P, N_MY); m->op = 'm';
                node_add(m, decl_var(P, 'm', '%', P->cur.s, P->cur.len));
                advance(P);
                named->b = m;
                accept_op(P, ",");
                break;
            }
            perr(P, "syntax error in subroutine signature (named parameters must come last)");
        }
        if (P->cur.type != T_VAR || P->cur.len == 0 || !is_ident_start((unsigned char)P->cur.s[0]))
            perr(P, "syntax error in subroutine signature");
        if (slurpy) perr(P, "Slurpy parameter not last");
        char sigil = P->cur.sigil;
        const char *name = P->cur.s; int len = P->cur.len;
        advance(P);
        pp_node *m = node(P, N_MY); m->op = 'm';
        node_add(m, decl_var(P, 'm', sigil, name, len));
        pp_node *as = node(P, N_ASSIGN); as->a = m;
        if (sigil == '@' || sigil == '%') {
            slurpy = 1;                                       /* my @rest = @_[idx .. $#_] */
            pp_node *sl = node(P, N_ASLICE);
            sl->a = mkvar(P, '@', "_", 1);
            pp_node *r = node(P, N_RANGE);
            r->a = const_int(P, idx);
            pp_node *li = node(P, N_LASTIDX); li->a = mkvar(P, '@', "_", 1);
            r->b = li;
            sl->b = r;
            as->b = sl;
            if (sigil == '%') chk->flags |= 1;                /* odd name/value check */
        } else if (sigil != '$') {
            perr(P, "syntax error in subroutine signature");
        } else {
            pp_node *ae = node(P, N_AELEM);                   /* $_[idx] */
            ae->a = mkvar(P, '@', "_", 1);
            ae->b = const_int(P, idx);
            int k = cur_is_op(P, "=") ? '=' : cur_is_op(P, "//=") ? '/' : cur_is_op(P, "||=") ? '|' : 0;
            if (k) {
                advance(P);
                if (min < 0) min = idx;
                pp_node *def = parse_assign(P);               /* may use earlier parameters */
                if (k == '=') {                               /* $#_ >= idx ? $_[idx] : default */
                    pp_node *tn = node(P, N_TERNARY);
                    pp_node *ge = node(P, N_BINOP); ge->op = OP_NUM_GE;
                    pp_node *li = node(P, N_LASTIDX); li->a = mkvar(P, '@', "_", 1);
                    ge->a = li; ge->b = const_int(P, idx);
                    tn->a = ge; tn->b = ae; tn->c = def;
                    as->b = tn;
                } else {                                      /* $_[idx] // default, $_[idx] || default */
                    pp_node *d = node(P, k == '/' ? N_DEFOR : N_LOGOR);
                    d->a = ae; d->b = def;
                    as->b = d;
                }
            } else {
                if (min >= 0) perr(P, "Mandatory parameter follows optional parameter");
                as->b = ae;
            }
            idx++;
        }
        if (scope_top(P)->warnings) as->flags |= PP_STMT_WARN;
        as->line = 0;                    /* keep the caller's line for signature errors */
        node_add(b, as); nstmt++;
        if (!accept_op(P, ",")) break;
    }
    if (!closed) expect_op(P, ")");
    chk->op = (uint8_t)(min >= 0 ? min : idx);
    chk->aux = (uint8_t)((slurpy || named) ? 255 : idx);
    chk->b = const_int(P, idx);
    if (named) { node_add(b, named); nstmt++; }
    return nstmt;
}

/* deeply nested source (parens, brackets, blocks) recurses in the parser;
   check the C stack watermark like pp_eval does and fail with a message
   instead of overflowing (on the board that would trample the heap) */
static void check_parse_stack(pp_parser *P) {
#ifdef __SANITIZE_ADDRESS__
    (void)P;      /* ASan's fake stack breaks the address arithmetic; ASan detects overflows itself */
#else
    if (!P->I->stack_base) return;
    char here;
    size_t used = (size_t)(P->I->stack_base > &here ? P->I->stack_base - &here : &here - P->I->stack_base);
    if (used > P->I->stack_limit) perr(P, "Out of stack space (source nesting too deep)");
#endif
}

static pp_node *parse_term(pp_parser *P) {
    check_parse_stack(P);
    pp_tok *t = &P->cur;
    if (t->type == T_CAST && !P->has_nxt && (((t->op[0] == '$' || t->op[0] == '@' || t->op[0] == '%') && !t->op[1]) || !strcmp(t->op, "$#"))) {
        /* ${name} @{name} %{name}: a name alone in the braces is the variable itself, so the
           token becomes $name and everything after it (subscripts included) reads the same */
        pp_lex *L = &P->L;
        int i = L->pos;
        if (i < L->len && L->src[i] == '{') {
            i++;
            while (i < L->len && (L->src[i] == ' ' || L->src[i] == '\t')) i++;
            int start = i;
            if (i < L->len && is_ident_start((unsigned char)L->src[i])) {
                while (i < L->len && (is_ident_char((unsigned char)L->src[i]) || (L->src[i] == ':' && i + 2 < L->len && L->src[i + 1] == ':' && is_ident_start((unsigned char)L->src[i + 2])))) i += L->src[i] == ':' ? 2 : 1;
                int end = i;
                while (i < L->len && (L->src[i] == ' ' || L->src[i] == '\t')) i++;
                if (i < L->len && L->src[i] == '}') {
                    t->type = T_VAR; t->sigil = t->op[1] ? '#' : t->op[0]; t->s = L->src + start; t->len = end - start;
                    L->pos = i + 1; L->prev_term = 1; L->prev_brace = 0;
                }
            }
        }
    }
    switch (t->type) {
    case T_NUM: { pp_node *n = node(P, N_CONST); n->val = t->num; advance(P); return n; }
    case T_STR: {
        if (t->sigil == 'H') { pp_node *n = const_str(P, t->s, t->len); advance(P); return n; }   /* <<'EOF': raw */
        pp_node *n = single_quoted(P, t->s, t->len); advance(P);
        return cur_is_op(P, "->") ? parse_postfix(P, n) : n;      /* 'Class'->method(...) */
    }
    case T_PAT: return parse_pattern_term(P);
    case T_READLINE: {
        pp_node *n = node(P, N_BUILTIN); n->op = (uint8_t)pp_builtin_id("readline", 8, NULL);
        node_add(n, mkvar(P, '$', t->s, t->len));
        advance(P);
        return n;
    }
    case T_ISTR: { pp_node *n = double_quoted(P, t->s, t->len); advance(P); return cur_is_op(P, "->") ? parse_postfix(P, n) : n; }
    case T_QW: {
        pp_node *n = qw_list(P, t->s, t->len); n->flags |= 1; advance(P);
        if (cur_is_op(P, "[")) { advance(P); pp_node *s = node(P, N_LSLICE); s->a = n; s->b = parse_expr(P); expect_op(P, "]"); return s; }
        return n;
    }
    case T_VAR: {
        char sigil = t->sigil; const char *name = t->s; int len = t->len;
        advance(P);
        if (sigil == '*') {   /* *name: the name is qualified now, so the node needs no package */
            pp_node *g = node(P, N_GLOB);
            char buf[256]; int bl;
            if (len > 6 && !memcmp(name, "main::", 6)) { name += 6; len -= 6; }
            int std = (len == 5 && !memcmp(name, "STDIN", 5)) || (len == 6 && (!memcmp(name, "STDOUT", 6) || !memcmp(name, "STDERR", 6)));
            if (std || has_pkg_sep(name, len) || !strcmp(P->pkg, "main")) bl = pp_snprintf(buf, sizeof buf, "%.*s", len, name);
            else bl = pp_snprintf(buf, sizeof buf, "%s::%.*s", P->pkg, len, name);
            g->b = NULL;
            g->a = const_str(P, buf, bl);
            return glob_slot(P, g);
        }
        if (sigil == '$') return parse_scalar_var_subscript(P, name, len);
        if (sigil == '@') {
            if (cur_is_op(P, "{")) { advance(P); pp_node *hv = mkvar(P, '%', name, len);
                pp_node *s = node(P, N_HSLICE); s->a = hv; s->b = parse_expr(P); expect_op(P, "}"); return s; }
            pp_node *v = mkvar(P, '@', name, len);
            if (cur_is_op(P, "[")) { advance(P); pp_node *s = node(P, N_ASLICE); s->a = v; s->b = parse_expr(P); expect_op(P, "]"); return s; }
            return v;
        }
        if (sigil == '%') { return mkvar(P, '%', name, len); }
        if (sigil == '#') { pp_node *v = mkvar(P, '@', name, len); pp_node *n = node(P, N_LASTIDX); n->a = v; return n; }
        if (sigil == '&') {
            pp_node *n = node(P, N_CALL); n->sym = sym_sigil(P, '&', name, len);
            if (cur_is_op(P, "(")) { advance(P); pp_node *args = parse_comma_list(P); expect_op(P, ")"); take_children(n, args); }
            else n->flags |= 2;
            return parse_postfix(P, n);
        }
        perr(P, "bad variable");
    }
    case T_CAST: {
        char op[4]; strcpy(op, t->op);
        advance(P);
        if (!strcmp(op, "$")) {
            pp_node *inner = parse_cast_target(P);
            if (cur_is_op(P, "[")) { advance(P); pp_node *av = node(P, N_RV2AV); av->a = inner; pp_node *n = node(P, N_AELEM); n->a = av; n->b = parse_expr(P); expect_op(P, "]"); return parse_subscripts(P, n, 1); }
            if (cur_is_op(P, "{")) { advance(P); pp_node *hv = node(P, N_RV2HV); hv->a = inner; pp_node *n = node(P, N_HELEM); n->a = hv; n->b = parse_hash_key(P); expect_op(P, "}"); return parse_subscripts(P, n, 1); }
            pp_node *sv = node(P, N_RV2SV); sv->a = inner;
            return parse_subscripts(P, sv, 0);
        }
        if (!strcmp(op, "@")) {
            pp_node *inner = parse_cast_target(P);
            pp_node *av = node(P, N_RV2AV); av->a = inner;
            if (cur_is_op(P, "[")) { advance(P); pp_node *s = node(P, N_ASLICE); s->a = av; s->b = parse_expr(P); expect_op(P, "]"); return s; }
            if (cur_is_op(P, "{")) { advance(P); pp_node *hv = node(P, N_RV2HV); hv->a = inner; pp_node *s = node(P, N_HSLICE); s->a = hv; s->b = parse_expr(P); expect_op(P, "}"); return s; }
            return av;
        }
        if (!strcmp(op, "%")) { pp_node *inner = parse_cast_target(P); pp_node *hv = node(P, N_RV2HV); hv->a = inner; return hv; }
        if (!strcmp(op, "*")) { pp_node *inner = parse_cast_target(P); pp_node *g = node(P, N_GLOB); g->a = inner; return glob_slot(P, g); }
        if (!strcmp(op, "$#")) { pp_node *inner = parse_cast_target(P); pp_node *av = node(P, N_RV2AV); av->a = inner; pp_node *n = node(P, N_LASTIDX); n->a = av; return n; }
        if (!strcmp(op, "&")) {
            pp_node *inner = parse_cast_target(P);
            pp_node *n = node(P, N_CALLREF); n->a = inner;
            if (cur_is_op(P, "(")) { advance(P); pp_node *args = parse_comma_list(P); expect_op(P, ")"); take_children(n, args); }
            else n->flags |= 2;
            return parse_postfix(P, n);
        }
        perr(P, "bad cast");
    }
    case T_OP: {
        if (!strcmp(t->op, "(")) {
            advance(P);
            pp_node *l;
            if (cur_is_op(P, ")")) l = node(P, N_LIST);
            else l = adopt(P, wrap_list(P, parse_expr(P)));
            expect_op(P, ")");
            l->flags |= 1;
            if (cur_is_op(P, "[")) {
                advance(P); pp_node *s = node(P, N_LSLICE); s->a = l; s->b = parse_expr(P); expect_op(P, "]");
                return cur_is_op(P, "->") ? parse_postfix(P, s) : s;      /* (LIST)[0]->method */
            }
            if (cur_is_op(P, "->")) return parse_postfix(P, l);           /* (sub { ... })->(...), ($x || $y)->{k} */
            return l;
        }
        if (!strcmp(t->op, "[")) {
            advance(P);
            pp_node *n = node(P, N_ANONARRAY);
            if (!cur_is_op(P, "]")) take_children(n, wrap_list(P, parse_expr(P)));
            expect_op(P, "]");
            return parse_postfix(P, n);
        }
        if (!strcmp(t->op, "{")) {
            advance(P);
            pp_node *n = node(P, N_ANONHASH);
            if (!cur_is_op(P, "}")) take_children(n, wrap_list(P, parse_expr(P)));
            expect_op(P, "}");
            return parse_postfix(P, n);
        }
        if (!strcmp(t->op, "<")) {
            advance(P);
            if (cur_is_ident(P, "STDIN")) { advance(P); expect_op(P, ">"); pp_node *n = node(P, N_BUILTIN); n->op = (uint8_t)pp_builtin_id("readline", 8, NULL); return n; }
            if (P->cur.type == T_IDENT && is_handle_name(P->cur.s, P->cur.len) && is_op(peek(P), ">")) {   /* <FH> */
                pp_node *n = node(P, N_BUILTIN); n->op = (uint8_t)pp_builtin_id("readline", 8, NULL);
                node_add(n, handle_const(P, P->cur.s, P->cur.len));
                advance(P); expect_op(P, ">");
                return n;
            }
            perr(P, "syntax error near '<'");
        }
        if (!strcmp(t->op, "-") || !strcmp(t->op, "!") || !strcmp(t->op, "\\") || !strcmp(t->op, "+") || !strcmp(t->op, "~") || !strcmp(t->op, "++") || !strcmp(t->op, "--")) return parse_unary(P);
        perr(P, "syntax error near '%s'", t->op);
    }
    case T_IDENT: return parse_ident_term(P);
    case T_EOF: perr(P, "syntax error: unexpected end of file");
    }
    perr(P, "syntax error");
    return NULL;
}

static void parse_call_args(pp_parser *P, pp_node *n) {
    if (cur_is_op(P, "(")) {
        advance(P);
        if (!cur_is_op(P, ")")) take_children(n, wrap_list(P, parse_expr(P)));
        expect_op(P, ")");
        return;
    }
    take_children(n, parse_comma_list(P));
}

/* sort/map/grep: optional block or expression, then list */
static pp_node *parse_block_or_expr_arg(pp_parser *P, pp_node *n, int is_sort) {
    int parens = accept_op(P, "(");
    if (cur_is_op(P, "{")) {
        n->a = parse_block(P);
        accept_op(P, ",");
        take_children(n, parse_comma_list(P));
        if (parens) expect_op(P, ")");
        return n;
    }
    if (is_sort && P->cur.type == T_IDENT && !at_list_end(P)) {
        int kind;
        pp_tok *nx = peek(P);
        int nx_ends = nx->type == T_OP && (!strcmp(nx->op, ",") || !strcmp(nx->op, "=>") || !strcmp(nx->op, "->") || !strcmp(nx->op, ")") || !strcmp(nx->op, ";"));
        {   /* sort readdir($dh): a built-in function is the start of the list, not a comparison sub */
            pp_sym *fs = sym_sigil(P, '&', P->cur.s, P->cur.len);
            if (fs->sub && fs->sub->u.code.native) nx_ends = 1;
        }
        if (pp_builtin_id(P->cur.s, P->cur.len, &kind) <= 0 && !cur_is_ident(P, "reverse") && !cur_is_ident(P, "map") && !cur_is_ident(P, "grep") && !cur_is_ident(P, "keys") && !cur_is_ident(P, "values") && !nx_ends && (nx->type != T_OP || !strcmp(nx->op, "(") || !strcmp(nx->op, "@"))) {
            pp_node *call = node(P, N_CALL); call->sym = sym_sigil(P, '&', P->cur.s, P->cur.len);
            advance(P);
            pp_node *blk = node(P, N_BLOCK); node_add(blk, call); n->a = blk;
            pp_node *l2 = parse_comma_list(P);
            if (parens) expect_op(P, ")");
            take_children(n, l2);
            return n;
        }
    }
    if (is_sort && P->cur.type == T_VAR && P->cur.sigil == '$' && !P->has_nxt) {
        /* sort $subref LIST: a plain scalar directly followed by the start of a list. Decided
           from the source text, because after a term the lexer would read "@list" as an operator. */
        pp_lex *L = &P->L;
        int i = L->pos, sp = 0;
        while (i < L->len && (L->src[i] == ' ' || L->src[i] == '\t')) { i++; sp = 1; }
        int c = i < L->len ? (unsigned char)L->src[i] : 0;
        if (sp && (c == '@' || c == '(' || (is_ident_start(c) && !(c == 'x' && !is_ident_char(i + 1 < L->len ? (unsigned char)L->src[i + 1] : 0))
                                             && strncmp(L->src + i, "if ", 3) && strncmp(L->src + i, "unless ", 7) && strncmp(L->src + i, "for ", 4) && strncmp(L->src + i, "foreach ", 8)
                                             && strncmp(L->src + i, "or ", 3) && strncmp(L->src + i, "and ", 4) && strncmp(L->src + i, "eq ", 3) && strncmp(L->src + i, "ne ", 3)
                                             && strncmp(L->src + i, "cmp ", 4) && strncmp(L->src + i, "lt ", 3) && strncmp(L->src + i, "gt ", 3) && strncmp(L->src + i, "le ", 3) && strncmp(L->src + i, "ge ", 3)
                                             && strncmp(L->src + i, "while ", 6) && strncmp(L->src + i, "until ", 6) && strncmp(L->src + i, "xor ", 4)))) {
            pp_node *call = node(P, N_CALLREF);
            L->prev_term = 0;                 /* what follows starts a term */
            call->a = parse_unary(P);
            pp_node *blk = node(P, N_BLOCK); node_add(blk, call); n->a = blk;
            pp_node *l2 = parse_comma_list(P);
            if (parens) expect_op(P, ")");
            take_children(n, l2);
            return n;
        }
    }
    pp_node *l = parse_comma_list(P);
    if (parens) expect_op(P, ")");
    if (l->n >= 1 && !is_sort) {
        /* map EXPR, LIST */
        pp_node *blk = node(P, N_BLOCK);
        node_add(blk, l->list[0]);
        n->a = blk;
        for (int i = 1; i < l->n; i++) node_add(n, l->list[i]);
        drop_shell(l);
        return n;
    }
    if (l->n >= 2 && is_sort && l->list[0]->type == N_CALL && (l->list[0]->flags & 1)) {
        /* sort subname LIST */
        pp_node *blk = node(P, N_BLOCK);
        pp_node *call = l->list[0]; call->flags &= ~1;
        node_add(blk, call);
        n->a = blk;
        for (int i = 1; i < l->n; i++) node_add(n, l->list[i]);
        drop_shell(l);
        return n;
    }
    take_children(n, l);
    return n;
}

static int starts_term(pp_parser *P) {
    pp_tok *t = &P->cur;
    switch (t->type) {
    case T_NUM: case T_STR: case T_ISTR: case T_VAR: case T_CAST: case T_QW: case T_PAT: case T_READLINE: return 1;
    case T_IDENT: return !at_list_end(P);
    case T_OP: return !strcmp(t->op, "(") || !strcmp(t->op, "[") || !strcmp(t->op, "{") || !strcmp(t->op, "\\") || !strcmp(t->op, "!")
        || (t->op[0] == '-' && isalpha((unsigned char)t->op[1]) && !t->op[2])
        || !strcmp(t->op, "-") || !strcmp(t->op, "+") || !strcmp(t->op, "++") || !strcmp(t->op, "--") || !strcmp(t->op, "~") || !strcmp(t->op, "<");
    default: return 0;
    }
}

static pp_node *parse_ident_term(pp_parser *P) {
    pp_tok *t = &P->cur;
    const char *id = t->s; int len = t->len;
    int line = t->line;

    pp_tok *nx = peek(P);
    if (nx->type == T_OP && !strcmp(nx->op, "=>")) { pp_node *n = const_str(P, id, len); advance(P); return n; }
    if (len >= 2 && id[0] == 'v' && isdigit((unsigned char)id[1]) && !is_declared(P, sym_sigil(P, '&', id, len))) {
        /* v1.22.333: a v-string, the characters with those numbers (v65.66 is "AB") */
        int ok = 1;
        for (int i = 1; i < len; i++) if (!isdigit((unsigned char)id[i]) && !(id[i] == '.' && i + 1 < len && isdigit((unsigned char)id[i + 1]) && isdigit((unsigned char)id[i - 1]))) ok = 0;
        if (ok) {
            char *buf = NULL; int n = 0, cap = 0;
            for (int i = 1; i < len; ) {
                long v = 0;
                while (i < len && isdigit((unsigned char)id[i])) { if (v < 0x110000) v = v * 10 + (id[i] - '0'); i++; }
                if (v > 0x10FFFF) v = 0xFFFD;
                if (v < 128) append_char(&buf, &n, &cap, (int)v); else append_utf8(&buf, &n, &cap, (int)v);
                if (i < len) i++;                /* the dot */
            }
            pp_node *k = const_str(P, buf ? buf : "", n);
            free(buf);
            advance(P);
            return k;
        }
    }
    int forced_core = 0;
    if (len > 6 && !memcmp(id, "CORE::", 6) && pp_builtin_id(id + 6, len - 6, NULL) > 0) {
        /* CORE::length: the builtin itself, even where a sub of that name exists */
        t->s += 6; t->len -= 6; id = t->s; len = t->len; forced_core = 1;
    }

    if (len == 8 && !memcmp(id, "__LINE__", 8)) { advance(P); return const_int(P, line); }
    if (len == 8 && !memcmp(id, "__FILE__", 8)) { advance(P); return const_str(P, P->file, (int)strlen(P->file)); }
    if (len == 11 && !memcmp(id, "__PACKAGE__", 11)) { advance(P); return parse_postfix(P, const_str(P, P->pkg, (int)strlen(P->pkg))); }
    if (len == 2 && !memcmp(id, "my", 2)) {
        advance(P);
        if (P->cur.type == T_IDENT && (peek(P)->type == T_VAR || is_op(peek(P), "("))) advance(P);   /* my Class $obj: the class name is only a note */
        return parse_var_decl_list(P, 'm');
    }
    if (len == 5 && !memcmp(id, "state", 5)) {
        pp_tok *nx1 = peek(P);
        if (nx1->type == T_VAR || (nx1->type == T_OP && !strcmp(nx1->op, "("))) {
            advance(P);
            return parse_var_decl_list(P, 's');
        }
    }
    if (len == 3 && !memcmp(id, "our", 3)) { advance(P); return parse_var_decl_list(P, 'o'); }
    if (len == 5 && !memcmp(id, "local", 5)) {
        advance(P);
        if (cur_is_ident(P, "our")) {   /* local our ($x, @y): declare the package variables, then localize them */
            advance(P);
            pp_node *m = parse_var_decl_list(P, 'o');
            m->op = 'l';
            return m;
        }
        if (P->cur.type == T_VAR && (P->cur.sigil == '$' || P->cur.sigil == '@')) {
            pp_tok *nx = peek(P);
            if (nx->type == T_OP && !nx->space_before && (!strcmp(nx->op, "{") || !strcmp(nx->op, "[") || !strcmp(nx->op, "->"))) {
                /* local $h{k}, local $a[i], local @h{...}, local $r->{k}: an element or a slice */
                pp_node *e = parse_term(P);
                if (e->type != N_HELEM && e->type != N_AELEM && e->type != N_HSLICE && e->type != N_ASLICE)
                    perr(P, "Can't localize this kind of expression");
                pp_node *n = node(P, N_LOCALELEM); n->a = e;
                return n;
            }
        }
        if ((P->cur.type == T_VAR && P->cur.sigil == '*') || (P->cur.type == T_CAST && !strcmp(P->cur.op, "*"))) {
            pp_node *g = parse_term(P);              /* local *name / local *{"name"} / local *$ref */
            if (g->type != N_GLOB) perr(P, "Can't localize this kind of expression");
            pp_node *n = node(P, N_LOCALGLOB); n->a = g;
            return n;
        }
        return parse_var_decl_list(P, 'l');
    }
    if (len == 3 && !memcmp(id, "sub", 3)) {
        advance(P);
        pp_node *n = node(P, N_ANONSUB);
        if (strcmp(P->pkg, "main") != 0) {   /* its name, for caller: "Pkg::__ANON__" */
            char an[128]; int al = pp_snprintf(an, sizeof an, "%s::__ANON__", P->pkg);
            n->sym = pp_intern(P->I, an, al);
        }
        int save = P->in_sub, save_state = P->state_n, save_argw = P->argw;
        P->in_sub = 1; P->state_n = 0; P->argw = 0;
        scope_push(P, SCOPE_SUB); scope_declare(P, P->I->sym_args);
        pp_node *b = node(P, N_BLOCK);
        scope_push(P, SCOPE_BLOCK);
        int nsig = 0;
        if (cur_is_op(P, "(")) { if (skip_prototype(P) == 2) n->op = 1; n->aux = (uint8_t)P->proto_mask; advance(P); nsig = parse_sub_params(P, b, "__ANON__", NULL); }
        skip_attributes(P);
        parse_block_stmts(P, b);
        if (b->n == nsig) node_add(b, node(P, N_NOP));   /* empty body: do not return the last binding */
        int nv = scope_pop(P);
        if (nv) { b->flags |= 1; b->aux = (uint8_t)nv; }
        n->a = b;
        scope_pop(P);
        if (P->argw) b->flags |= PP_BLOCK_WARGS;
        P->in_sub = save; P->state_n = save_state; P->argw = save_argw;
        return parse_postfix(P, n);
    }
    if (len == 2 && !memcmp(id, "do", 2)) {
        advance(P);
        if (cur_is_op(P, "{")) {
            pp_node *n = node(P, N_DOBLOCK); n->a = parse_block(P);
            return cur_is_op(P, "->") ? parse_postfix(P, n) : n;
        }
        pp_node *n = node(P, N_REQUIRE); n->op = 1; n->a = parse_unary(P); return n;
    }
    if (len == 4 && !memcmp(id, "eval", 4)) {
        advance(P);
        if (cur_is_op(P, "{")) { pp_node *n = node(P, N_EVALBLOCK); n->a = parse_block(P); return n; }
        /* eval STRING: the code is parsed when it runs. Note which `my` variables it can see
           (innermost first, so shadowed ones are skipped): the run-time parser declares the
           same names, and their cells are shared with the eval. */
        pp_node *n = node(P, N_EVALSTR);
        if (cur_is_op(P, "(")) { advance(P); if (!cur_is_op(P, ")")) n->a = parse_expr(P); expect_op(P, ")"); }   /* eval(...) + 1 */
        else if (starts_term(P)) n->a = parse_binary(P, PREC_NAMED_UNARY);
        pscopes *S = P->sc;
        for (int i = S->n - 1; i >= 0 && n->n < 200; i--) {
            pscope *sc = &S->st[i];
            for (int k = sc->n - 1; k >= 0 && n->n < 200; k--) {
                int dup = 0;
                for (int j = 0; j < n->n; j++) if (n->list[j]->sym == sc->vars[k]) { dup = 1; break; }
                if (dup) continue;
                pp_node *v = node(P, N_VAR);
                v->sym = sc->vars[k]; v->flags |= PP_VAR_LEX; v->op = (uint8_t)k;
                scope_record(P, v, S->st[S->n - 1].id, sc->id);
                node_add(n, v);
            }
        }
        if (strcmp(P->pkg, "main") != 0) n->b = const_str(P, P->pkg, (int)strlen(P->pkg));
        pscope *top = scope_top(P);
        n->op = (uint8_t)((top->strict_vars ? 1 : 0) | (top->strict_subs ? 2 : 0) | (top->strict_refs ? 4 : 0) | (top->warnings ? 8 : 0));
        return n;
    }
    if (len == 3 && !memcmp(id, "not", 3) && !(nx->type == T_OP && (!strcmp(nx->op, "(") || !strcmp(nx->op, "=>")))) {
        /* `not` where a term is expected ($x = not IS_VMS): negates the expression after it */
        advance(P);
        pp_node *n = node(P, N_NOT); n->line = line;
        n->a = parse_assign(P);
        return n;
    }
    if (len == 3 && !memcmp(id, "not", 3) && nx->type == T_OP && !strcmp(nx->op, "(")) {
        advance(P); advance(P);
        pp_node *n = node(P, N_NOT);
        n->a = cur_is_op(P, ")") ? const_int(P, 0) : parse_expr(P);
        expect_op(P, ")");
        return n;
    }
    if (len == 6 && !memcmp(id, "caller", 6)) {   /* caller, caller(), caller(N), caller N */
        advance(P);
        pp_node *n = node(P, N_CALLER);
        if (accept_op(P, "(")) { if (!cur_is_op(P, ")")) n->a = parse_expr(P); expect_op(P, ")"); }
        else if (P->cur.type == T_NUM || (P->cur.type == T_VAR && P->cur.sigil == '$')) n->a = parse_unary(P);
        return parse_postfix(P, n);
    }
    if (len == 9 && !memcmp(id, "wantarray", 9)) { advance(P); if (accept_op(P, "(")) expect_op(P, ")"); return node(P, N_WANTARRAY); }
    if (len == 6 && (!memcmp(id, "STDOUT", 6) || !memcmp(id, "STDERR", 6))) {
        pp_node *n = node(P, N_FILEHANDLE); n->op = id[3] == 'O' ? 1 : 2; advance(P); return n;
    }
    if (len == 1 && id[0] == '_') {
        /* _ : the file of the last file test or stat. There is no saved stat buffer here; it
           stands for the path that was looked at last, which is looked at again. */
        advance(P);
        pp_node *v = node(P, N_VAR); v->sym = pp_intern(P->I, "$\002stat", 6); v->line = line;
        return v;
    }
    if (((len == 3 && !memcmp(id, "tie", 3)) || (len == 4 && !memcmp(id, "tied", 4)) || (len == 5 && !memcmp(id, "untie", 5)))
        && !(nx->type == T_OP && !strcmp(nx->op, "=>")) && !is_declared(P, sym_sigil(P, '&', id, len))) {
        /* tie VAR, CLASS, LIST / tied VAR / untie VAR: calls __tie(\VAR, CLASS, LIST) etc., so
           the function gets the variable itself (a glob for a handle) */
        advance(P);
        pp_node *c = node(P, N_CALL); c->line = line;
        c->sym = pp_intern(P->I, len == 3 ? "&__tie" : len == 4 ? "&__tied" : "&__untie", len + 3);
        if (len == 3) parse_call_args(P, c);
        else if (accept_op(P, "(")) { node_add(c, parse_assign(P)); expect_op(P, ")"); }
        else node_add(c, parse_unary(P));
        if (c->n < 1) perr(P, "Not enough arguments for %.*s", len, id);
        pp_node *r = node(P, N_REF); r->a = c->list[0]; r->line = line;
        c->list[0] = r;
        return len == 3 ? c : parse_postfix(P, c);
    }
    if (((len == 4 && !memcmp(id, "read", 4)) || (len == 7 && !memcmp(id, "sysread", 7)) || (len == 4 && !memcmp(id, "getc", 4)))
        && !(nx->type == T_OP && (!strcmp(nx->op, "=>") || !strcmp(nx->op, "->") || !strcmp(nx->op, "::"))) && !is_declared(P, sym_sigil(P, '&', id, len))) {
        /* read(FH, $buf, LEN, OFFSET) is __read(FH, \$buf, LEN, OFFSET); getc(FH) is __getc(FH) */
        int is_getc = id[0] == 'g';
        advance(P);
        int parens = accept_op(P, "(");
        pp_node *c = node(P, N_CALL); c->line = line;
        c->sym = is_getc ? pp_intern(P->I, "&__getc", 7) : pp_intern(P->I, "&__read", 7);
        if (is_getc && (parens ? cur_is_op(P, ")") : !starts_term(P))) node_add(c, handle_const(P, "STDIN", 5));
        else if (P->cur.type == T_IDENT && is_handle_name(P->cur.s, P->cur.len) && (is_getc || is_op(peek(P), ","))) { node_add(c, handle_const(P, P->cur.s, P->cur.len)); advance(P); }
        else node_add(c, parse_assign(P));
        if (!is_getc) {
            expect_op(P, ",");
            pp_node *r = node(P, N_REF); r->a = parse_assign(P); r->line = line;
            node_add(c, r);
            expect_op(P, ",");
            node_add(c, parse_assign(P));
            if (accept_op(P, ",")) node_add(c, parse_assign(P));
        }
        if (parens) expect_op(P, ")");
        return c;
    }
    if (len == 7 && !memcmp(id, "opendir", 7) && !is_declared(P, sym_sigil(P, '&', id, len))) {
        /* opendir(my $dh, $path) is $dh = __opendir($path) (the handle, or undef);
           opendir(DH, $path) is __opendir_named("*Pkg::DH", $path) */
        advance(P);
        int parens = accept_op(P, "(");
        pp_node *h;
        if (P->cur.type == T_IDENT && is_handle_name(P->cur.s, P->cur.len) && is_op(peek(P), ",")) { h = handle_const(P, P->cur.s, P->cur.len); advance(P); }
        else h = parse_assign(P);
        expect_op(P, ",");
        pp_node *path = parse_assign(P);
        if (parens) expect_op(P, ")");
        pp_node *c = node(P, N_CALL); c->line = line;
        if (h->type == N_CONST || h->type == N_GLOB || h->type == N_REF) {
            c->sym = pp_intern(P->I, "&__opendir_named", 16);
            node_add(c, h); node_add(c, path);
            return c;
        }
        c->sym = pp_intern(P->I, "&__opendir", 10);
        node_add(c, path);
        pp_node *as = node(P, N_ASSIGN); as->op = OP_NONE; as->a = h; as->b = c; as->line = line;
        return as;
    }
    if (len == 7 && !memcmp(id, "require", 7)) {
        advance(P);
        /* require 5.006; / require v5.10.1; -- a minimum perl version, not a file. picoperl
           follows perl 5.44: anything up to that is fine, a newer one dies like perl does. */
        int isver = P->cur.type == T_NUM;
        if (P->cur.type == T_IDENT && P->cur.len >= 2 && P->cur.s[0] == 'v' && isdigit((unsigned char)P->cur.s[1])) {
            isver = 1;
            for (int i = 1; i < P->cur.len; i++) if (!isdigit((unsigned char)P->cur.s[i]) && P->cur.s[i] != '.') isver = 0;
        }
        if (isver) {
            long part[3] = {0, 0, 0};
            if (P->cur.type == T_NUM) {              /* 5.006001 = 5, 6, 1 */
                double ver = P->cur.num.t == PP_INT ? (double)P->cur.num.u.i : P->cur.num.u.n;
                long long micro = (long long)(ver * 1000000.0 + 0.5);
                part[0] = (long)(micro / 1000000); part[1] = (long)(micro / 1000 % 1000); part[2] = (long)(micro % 1000);
            } else {
                int k = 0;
                for (int i = 1; i < P->cur.len && k < 3; i++) {
                    if (P->cur.s[i] == '.') k++;
                    else part[k] = part[k] * 10 + (P->cur.s[i] - '0');
                }
            }
            advance(P);
            if (part[0] < 5 || (part[0] == 5 && (part[1] < 44 || (part[1] == 44 && part[2] == 0)))) return const_int(P, 1);
            char msg[96];
            int ml = pp_snprintf(msg, sizeof msg, "Perl v%ld.%ld.%ld required--this is only v5.44.0, stopped", part[0], part[1], part[2]);
            pp_node *d = node(P, N_BUILTIN); d->op = (uint8_t)pp_builtin_id("die", 3, NULL);
            node_add(d, const_str(P, msg, ml));
            return d;
        }
        pp_node *n = node(P, N_REQUIRE);
        pp_tok *rnx = P->cur.type == T_IDENT ? peek(P) : NULL;
        /* require Class->method(...): the file name comes from a call (an expression, as below) */
        if (rnx && !(rnx->type == T_OP && !strcmp(rnx->op, "->"))) {   /* require Module::Name; -> "Module/Name.pm" */
            char buf[256]; int bn = 0;
            for (int i = 0; i < P->cur.len && bn < 240; i++) {
                if (P->cur.s[i] == ':' && i + 1 < P->cur.len && P->cur.s[i+1] == ':') { buf[bn++] = '/'; i++; }
                else buf[bn++] = P->cur.s[i];
            }
            memcpy(buf + bn, ".pm", 4); bn += 3;
            n->a = const_str(P, buf, bn);
            advance(P);
            return n;
        }
        n->a = parse_binary(P, PREC_NAMED_UNARY);   /* require $dir . "/x.pl": binds like a named unary operator */
        return n;
    }
    if (len == 6 && !memcmp(id, "return", 6)) {
        advance(P);
        pp_node *n = node(P, N_RETURN);
        if (!at_list_end(P)) n->a = adopt(P, parse_comma_list(P));
        return n;
    }
    if (len == 4 && !memcmp(id, "goto", 4)) {
        /* goto &sub / goto &$code / goto \&sub: leave this sub by calling the other one with the
           current @_. Done as `return &sub;`, so the caller's context is passed on but the
           frame of this sub stays on the stack until the other one returns. */
        advance(P);
        if (P->cur.type == T_IDENT && !has_pkg_sep(P->cur.s, P->cur.len)) {   /* goto LABEL */
            pp_node *g = node(P, N_GOTO);
            g->sym = pp_intern(P->I, P->cur.s, P->cur.len);
            advance(P);
            P->sc->has_goto = 1;
            return g;
        }
        pp_node *e = adopt(P, parse_assign(P));
        if (!((e->type == N_CALL || e->type == N_CALLREF) && (e->flags & 2))) {   /* an expression giving a code ref */
            pp_node *c = node(P, N_CALLREF); c->a = e; c->flags |= 2; e = c;
        }
        pp_node *n = node(P, N_RETURN);
        n->a = e;
        return n;
    }
    if (len == 4 && (!memcmp(id, "last", 4) || !memcmp(id, "next", 4) || !memcmp(id, "redo", 4))) {
        pp_node *n = node(P, id[0] == 'l' ? N_LAST : id[0] == 'n' ? N_NEXT : N_REDO);
        advance(P);
        if (P->cur.type == T_IDENT && !at_list_end(P)) { n->sym = pp_intern(P->I, P->cur.s, P->cur.len); advance(P); }
        return n;
    }

    int kind;
    int bid = pp_builtin_id(id, len, &kind);
    if (bid > 0) {
        /* a sub imported into (or defined in) this package overrides an overridable builtin (Time::HiRes::time) */
        static const char *core_only[] = { "print", "printf", "say", "defined", "undef", "scalar", "exists", "delete", "bless", "die", "warn", "sort", "map", "grep", "push", "pop", "shift", "unshift", "keys", "values", "each", "split", "join", "ref", NULL };
        int core = 0;
        for (int i = 0; core_only[i]; i++) if ((int)strlen(core_only[i]) == len && !memcmp(core_only[i], id, len)) core = 1;
        if (!core && !forced_core && is_declared(P, sym_sigil(P, '&', id, len))) bid = 0;
    }
    if (bid > 0) {
        advance(P);
        pp_node *n = node(P, N_BUILTIN); n->op = (uint8_t)bid; n->line = line;
        int is_sort = len == 4 && !memcmp(id, "sort", 4);
        int is_mapgrep = (len == 4 && !memcmp(id, "grep", 4)) || (len == 3 && !memcmp(id, "map", 3))
                       || (len == 3 && (!memcmp(id, "any", 3) || !memcmp(id, "all", 3)));
        if (is_sort || is_mapgrep) return parse_block_or_expr_arg(P, n, is_sort);
        int is_print = (len == 5 && !memcmp(id, "print", 5)) || (len == 6 && !memcmp(id, "printf", 6)) || (len == 3 && !memcmp(id, "say", 3));
        if (is_print) {
            int parens = accept_op(P, "(");
            if (cur_is_ident(P, "STDERR") || cur_is_ident(P, "STDOUT")) {
                pp_tok *nx2 = peek(P);
                /* print STDERR LIST and print STDERR (LIST); not `print STDERR, ...` or STDERR->method */
                if (!(nx2->type == T_OP && (!strcmp(nx2->op, ",") || !strcmp(nx2->op, "->")))) {
                    pp_node *fh = node(P, N_FILEHANDLE); fh->op = P->cur.s[3] == 'O' ? 1 : 2; n->a = fh; advance(P);
                }
            }
            else if (P->cur.type == T_IDENT && is_handle_name(P->cur.s, P->cur.len)
                     && !is_declared(P, sym_sigil(P, '&', P->cur.s, P->cur.len))) {
                /* print FH LIST / print FH; -- not `print FH, ...` (a list) or FH->method */
                pp_tok *nx2 = peek(P);
                int binop = nx2->type == T_OP && strcmp(nx2->op, "(") && strcmp(nx2->op, ";") && strcmp(nx2->op, ")") && strcmp(nx2->op, "\\")
                            && strcmp(nx2->op, "-") && strcmp(nx2->op, "[") && strcmp(nx2->op, "<") && strcmp(nx2->op, "!") && strcmp(nx2->op, "}");
                if (!binop) { n->a = handle_const(P, P->cur.s, P->cur.len); advance(P); }
            }
            else if (cur_is_op(P, "{")) { advance(P); n->a = parse_expr(P); expect_op(P, "}"); }
            else if (P->cur.type == T_VAR && P->cur.sigil == '$') {
                /* `print $fh LIST` (no comma): the handle is a plain scalar followed by a value */
                int at_list = 0;
                if (!P->has_nxt) {
                    /* print $fh @list: seen in the source text, because after a term the lexer
                       would read "@list" as an operator */
                    pp_lex *L = &P->L;
                    int i = L->pos, sp = 0;
                    while (i < L->len && (L->src[i] == ' ' || L->src[i] == '\t')) { i++; sp = 1; }
                    if (sp && i + 1 < L->len && L->src[i] == '@' && (is_ident_start((unsigned char)L->src[i + 1]) || L->src[i + 1] == '{' || L->src[i + 1] == '$' || L->src[i + 1] == ':')) at_list = 1;
                }
                if (at_list) {
                    n->a = mkvar(P, '$', P->cur.s, P->cur.len);
                    P->L.prev_term = 0;                 /* what follows starts a term */
                    advance(P);
                } else {
                pp_tok *nx2 = peek(P);
                static const char *const opwords[] = { "x", "eq", "ne", "lt", "gt", "le", "ge", "cmp", "and", "or", "xor", "if", "unless", "for", "foreach", "while", "until", "isa", NULL };
                int word = nx2->type == T_IDENT;
                for (int i = 0; word && opwords[i]; i++) if (tok_is_ident(nx2, opwords[i])) word = 0;
                if (nx2->type == T_STR || nx2->type == T_ISTR || nx2->type == T_NUM || nx2->type == T_QW ||
                    nx2->type == T_VAR || nx2->type == T_READLINE || word) {
                    n->a = mkvar(P, '$', P->cur.s, P->cur.len);
                    advance(P);
                }
                }
            }
            take_children(n, parse_comma_list(P));
            if (parens) expect_op(P, ")");
            return n;
        }
        if (kind == BK_NOARG) {
            if (accept_op(P, "(")) {
                if (len == 8 && !memcmp(id, "readline", 8) && !cur_is_op(P, ")")) node_add(n, parse_expr(P));   /* readline($fh) */
                expect_op(P, ")");
            }
            return parse_postfix(P, n);
        }
        if (kind == BK_UNARY) {
            int chomps = (len == 5 && !memcmp(id, "chomp", 5)) || (len == 4 && !memcmp(id, "chop", 4));
            if (cur_is_op(P, "(")) { advance(P); if (!cur_is_op(P, ")")) node_add(n, parse_expr(P)); expect_op(P, ")"); if (chomps && n->n) note_argw(P, n->list[0]); return parse_postfix(P, n); }
            /* a '<' is the start of an argument only as <STDIN> / <FH> (scalar <DATA>), else it is "less than" */
            int lt_read = 0;
            if (cur_is_op(P, "<")) { pp_tok *nx2 = peek(P); lt_read = nx2->type == T_IDENT && is_handle_name(nx2->s, nx2->len) && P->L.pos < P->L.len && P->L.src[P->L.pos] == '>'; }
            if (starts_term(P) && !(cur_is_op(P, "<") && !lt_read)) node_add(n, parse_binary(P, PREC_NAMED_UNARY));
            if (chomps && n->n) note_argw(P, n->list[0]);
            return n->n ? n : parse_postfix(P, n);   /* shift->[0], shift->method(...) */
        }
        parse_call_args(P, n);
        if (bid == pp_builtin_id("bless", 5, NULL) && n->n == 1) node_add(n, const_str(P, P->pkg, (int)strlen(P->pkg)));
        return parse_postfix(P, n);
    }

    pp_sym *s = sym_sigil(P, '&', id, len);
    if (strcmp(P->pkg, "main") != 0 && !has_pkg_sep(id, len) && !pp_lookup_sub(P->I, s)) {
        /* a native registered without a package (chdir) works like a builtin: every package
           calls the one in main, unless it defines a sub of that name itself */
        char buf[128];
        int bn = pp_snprintf(buf, sizeof buf, "&%.*s", len, id);
        pp_sym *m = bn < (int)sizeof buf ? pp_sym_find(P->I, buf, bn) : NULL;
        int local_decl = 0;
        for (int i = 0; i < P->ndecl; i++) if (P->decl[i] == s) local_decl = 1;
        if (m && m->sub && m->sub->u.code.native && !local_decl) s = m;
    }
    advance(P);
    pp_node *n = node(P, N_CALL); n->sym = s; n->line = line;
    if (cur_is_op(P, "(")) { parse_call_args(P, n); apply_proto(P, n); return parse_postfix(P, n); }
    if (cur_is_op(P, "{") && !P->has_nxt && is_block_proto(P, s)) {
        /* name { ... } LIST for a sub with a (&...) prototype: the block is `sub { ... }`.
           Put a `sub` token in front of the '{' and let the anonymous-sub parser read it. */
        P->nxt = P->cur; P->has_nxt = 1;
        P->cur.type = T_IDENT; P->cur.s = "sub"; P->cur.len = 3;
        node_add(n, parse_ident_term(P));
        accept_op(P, ",");
        if (starts_term(P)) take_children(n, parse_comma_list(P));
        return n;
    }
    int decl = is_declared(P, s);
    if (decl) {
        if (decl == 2) return parse_postfix(P, n);
        if (starts_term(P) && !cur_is_op(P, "-") && !cur_is_op(P, "<") && !cur_is_op(P, "{") && !cur_is_op(P, "[") && !cur_is_op(P, "?")) { parse_call_args(P, n); apply_proto(P, n); }
        return parse_postfix(P, n);
    }
    n->flags |= 1;   /* bareword: call sub if it exists at runtime, else string */
    if (!has_pkg_sep(id, len)) n->flags |= 4;   /* written without a package: the string is the bare name */
    /* FH, STDIN, DATA ...: a bareword in capitals is a file handle (the glob *FH), unless it is
       a class name (FOO->new) or a hash key (FOO => 1) */
    /* (without strict subs it stays the string "FH", which names the same handle) */
    if (scope_top(P)->strict_subs && is_handle_name(id, len) && !cur_is_op(P, "->") && !cur_is_op(P, "=>")) { n->flags |= 8; return n; }
    if (scope_top(P)->strict_subs && !cur_is_op(P, "->") && !has_pkg_sep(id, len))
        perr(P, "Bareword \"%.*s\" not allowed while \"strict subs\" in use", len, id);
    return parse_postfix(P, n);
}

/* ------------------------------------------------------------------ */
/* Statements                                                          */
/* ------------------------------------------------------------------ */

static void parse_block_stmts(pp_parser *P, pp_node *b) {
    char saved_pkg[sizeof P->pkg]; strcpy(saved_pkg, P->pkg);   /* `package X;` lasts to the end of its block */
    int saved_stmt = P->pkg_stmt; P->pkg_stmt = 0;
    expect_op(P, "{");
    while (!cur_is_op(P, "}")) {
        if (P->cur.type == T_EOF) perr(P, "Missing right curly bracket");
        pp_node *s = parse_statement(P);
        if (s) node_add(b, s);
    }
    strcpy(P->pkg, saved_pkg);
    if (P->pkg_stmt) b->flags |= PP_BLOCK_PKG;   /* and at run time (for caller) */
    P->pkg_stmt = saved_stmt;
    expect_op(P, "}");
}
static pp_node *parse_block(pp_parser *P) {
    pp_node *b = node(P, N_BLOCK);
    scope_push(P, SCOPE_BLOCK);
    parse_block_stmts(P, b);
    int nv = scope_pop(P);
    if (nv) { b->flags |= 1; b->aux = (uint8_t)nv; }
    return b;
}

/* if (COND) BLOCK elsif ... else ...: a scope around the whole chain for `my` in conditions */
static pp_node *parse_cond_block(pp_parser *P, int is_unless) {
    pp_node *n = node(P, N_IF); n->op = (uint8_t)is_unless;
    scope_push(P, SCOPE_BLOCK);
    expect_op(P, "(");
    n->a = parse_expr(P);
    expect_op(P, ")");
    n->b = parse_block(P);
    if (cur_is_ident(P, "elsif")) { advance(P); n->c = parse_cond_block(P, 0); }
    else if (cur_is_ident(P, "else")) { advance(P); n->c = parse_block(P); }
    int nv = scope_pop(P);
    if (nv) { n->flags |= 4; n->aux = (uint8_t)nv; }
    return n;
}

static const char *ignored_modules[] = {
    "utf8", "feature", "Exporter", "bytes",
    "diagnostics", "experimental", "FindBin", "English", NULL
};

static pp_node *parse_use(pp_parser *P) {
    int is_no = cur_is_ident(P, "no");
    advance(P);
    /* use v5.36; / use 5.036; — minimum-version declaration. Every picoperl feature
       is always available; like perl, v5.12+ turns on strict and v5.36+ also warnings. */
    int isv = P->cur.type == T_IDENT && P->cur.len >= 2 && P->cur.s[0] == 'v';
    if (isv) {
        int digits = 0;
        for (int i = 1; i < P->cur.len; i++) {
            if (isdigit((unsigned char)P->cur.s[i])) digits = 1;
            else if (P->cur.s[i] != '.') { isv = 0; break; }
        }
        if (!digits) isv = 0;
    }
    if (!is_no && (isv || P->cur.type == T_NUM)) {
        int minor = 0;
        if (P->cur.type == T_NUM) {
            double ver = P->cur.num.t == PP_INT ? (double)P->cur.num.u.i : P->cur.num.u.n;
            minor = (int)((ver - 5.0) * 1000.0 + 0.5);
        } else {
            const char *s = P->cur.s; int l = P->cur.len, i = 1;
            while (i < l && isdigit((unsigned char)s[i])) i++;                 /* major */
            if (i < l && s[i] == '.') { i++; while (i < l && isdigit((unsigned char)s[i])) minor = minor * 10 + (s[i++] - '0'); }
        }
        advance(P);
        pscope *top = scope_top(P);
        if (minor >= 11) top->strict_vars = top->strict_subs = top->strict_refs = 1;
        if (minor >= 35) top->warnings = 1;
        expect_op(P, ";");
        return NULL;
    }
    if (cur_is_ident(P, "integer")) {   /* use integer / no integer: for the rest of the enclosing block */
        advance(P);
        scope_top(P)->integer = !is_no;
        expect_op(P, ";");
        return NULL;
    }
    if (cur_is_ident(P, "strict") || cur_is_ident(P, "warnings")) {
        int is_strict = cur_is_ident(P, "strict");
        pscope *top = scope_top(P);
        int vars = 1, subs = 1, refs = 1;
        advance(P);
        if (!cur_is_op(P, ";")) {   /* use strict 'vars'; use strict qw(subs refs); */
            vars = subs = refs = 0;
            pp_node *l = wrap_list(P, parse_expr(P));
            for (int i = 0; i < l->n; i++) {
                if (l->list[i]->type == N_CONST && l->list[i]->val.t == PP_STR) {
                    const char *w = l->list[i]->val.u.s->d;
                    if (!strcmp(w, "vars")) vars = 1;
                    if (!strcmp(w, "subs")) subs = 1;
                    if (!strcmp(w, "refs")) refs = 1;
                    if (!strcmp(w, "all")) vars = subs = refs = 1;
                }
            }
            pp_free_ast(l);
            if (!is_strict) vars = subs = refs = 1;
        }
        if (is_strict) {
            if (vars) top->strict_vars = !is_no;
            if (subs) top->strict_subs = !is_no;
            if (refs) top->strict_refs = !is_no;
        } else top->warnings = !is_no;
        expect_op(P, ";");
        return NULL;
    }
    if (cur_is_ident(P, "lib")) {   /* use lib LIST: unshift @INC, LIST (runs now, like every use) */
        advance(P);
        if (is_no || cur_is_op(P, ";")) { while (!cur_is_op(P, ";") && P->cur.type != T_EOF) advance(P); accept_op(P, ";"); return NULL; }
        pp_node *u = node(P, N_BUILTIN);
        u->op = (uint8_t)pp_builtin_id("unshift", 7, NULL);
        pp_node *inc = node(P, N_VAR);
        inc->sym = pp_intern(P->I, "@INC", 4);
        node_add(u, inc);
        take_children(u, wrap_list(P, parse_expr(P)));
        expect_op(P, ";");
        return u;
    }
    if (cur_is_ident(P, "vars") && !is_no) {   /* use vars qw($x @y %z): package globals, like `our` */
        advance(P);
        if (!cur_is_op(P, ";")) {
            pp_node *l = wrap_list(P, parse_expr(P));
            for (int i = 0; i < l->n; i++) {
                if (l->list[i]->type != N_CONST || l->list[i]->val.t != PP_STR) continue;
                const char *w = l->list[i]->val.u.s->d; int wl = (int)l->list[i]->val.u.s->len;
                if (wl >= 2 && (w[0] == '$' || w[0] == '@' || w[0] == '%')) declare_our(P, w[0], w + 1, wl - 1);
            }
            pp_free_ast(l);
        }
        expect_op(P, ";");
        return NULL;
    }
    if (cur_is_ident(P, "constant") && !is_no) {
        advance(P);
        pp_node *n = node(P, N_CONSTDEF);
        n->b = const_str(P, P->pkg, (int)strlen(P->pkg));
        pp_node *e = adopt(P, parse_expr(P));
        n->a = e;
        if (e->type == N_LIST && e->n >= 1 && e->list[0]->type == N_CONST && e->list[0]->val.t == PP_STR)
            declare_sub(P, sym_sigil(P, '&', e->list[0]->val.u.s->d, e->list[0]->val.u.s->len), 1);
        else if (e->type == N_ANONHASH) {
            for (int i = 0; i + 1 < e->n; i += 2)
                if (e->list[i]->type == N_CONST && e->list[i]->val.t == PP_STR)
                    declare_sub(P, sym_sigil(P, '&', e->list[i]->val.u.s->d, e->list[i]->val.u.s->len), 1);
        }
        expect_op(P, ";");
        return n;
    }
    if (P->cur.type == T_IDENT && !is_no) {
        int ignored = 0;
        for (int i = 0; ignored_modules[i]; i++) if (cur_is_ident(P, ignored_modules[i])) ignored = 1;
        if (cur_is_ident(P, "utf8") && !(P->I->compat_warn & 1)) {
            P->I->compat_warn |= 1;
            char msg[200];
            int mn = pp_snprintf(msg, sizeof msg, "picoperl: use utf8 is ignored, strings stay bytes at %s line %d.\n", P->file, P->cur.line);
            pp_host_write(2, msg, mn);
        }
        if (!ignored) {
            pp_node *n = node(P, N_USE);
            if (cur_is_ident(P, "parent") || cur_is_ident(P, "base")) n->op = 1;
            n->a = const_str(P, P->cur.s, P->cur.len);
            n->b = const_str(P, P->pkg, (int)strlen(P->pkg));
            advance(P);
            if (P->cur.type == T_NUM) advance(P);   /* version */
            /* use Module (); / use Module qw(); -- an empty list written out: load, import nothing */
            if ((cur_is_op(P, "(") && is_op(peek(P), ")")) || (P->cur.type == T_QW && P->cur.len == 0)) n->flags |= 2;
            if (!cur_is_op(P, ";")) take_children(n, wrap_list(P, parse_expr(P)));
            expect_op(P, ";");
            return n;
        }
    }
    /* an ignored `use Module ARGS;`: skip to the ';' that ends it (not one inside { ... },
       as in use overload '+' => sub { ...; }) */
    for (int depth = 0; P->cur.type != T_EOF && !(depth == 0 && cur_is_op(P, ";")); advance(P)) {
        if (cur_is_op(P, "{")) depth++;
        else if (cur_is_op(P, "}") && depth > 0) depth--;
    }
    accept_op(P, ";");
    return NULL;
}

static pp_node *parse_statement_inner(pp_parser *P) {
    pp_tok *t = &P->cur;
    pp_sym *label = P->pending_label;
    P->pending_label = NULL;
    if (t->type == T_OP) {
        if (!strcmp(t->op, ";")) { advance(P); return NULL; }
        if (!strcmp(t->op, "{")) {
            pp_node *n = node(P, N_WHILE); n->flags |= 16; n->sym = label; n->b = parse_block(P);   /* bare block: loop once */
            return n;
        }
    }
    if (t->type == T_IDENT) {
        pp_tok *nx = peek(P);
        if (nx->type == T_OP && !strcmp(nx->op, ":") && t->len > 0 && !has_pkg_sep(t->s, t->len)) {
            pp_sym *lab = pp_intern(P->I, t->s, t->len);
            advance(P); advance(P);
            P->pending_label = lab;
            pp_node *st = cur_is_op(P, "}") || P->cur.type == T_EOF ? NULL : parse_statement(P);
            P->pending_label = NULL;
            if (st && (st->type == N_WHILE || st->type == N_FOR || st->type == N_FOREACH) && st->sym == lab) return st;
            pp_node *w = node(P, N_LABEL);       /* a label on a plain statement: a goto target */
            w->sym = lab; w->a = st;
            return w;
        }

        if ((tok_is_ident(t, "my") || tok_is_ident(t, "our") || tok_is_ident(t, "state")) && tok_is_ident(nx, "sub")) {
            /* my sub name { ... }: a lexical sub; here it is an ordinary sub of the package */
            advance(P);
            nx = peek(P);
        }
        if (tok_is_ident(t, "sub") && nx->type == T_IDENT) {
            advance(P);
            pp_node *n = node(P, N_SUBDEF);
            n->sym = sym_sigil(P, '&', P->cur.s, P->cur.len);
            advance(P);
            int save = P->in_sub, save_state = P->state_n, save_argw = P->argw;
            P->in_sub = 1; P->state_n = 0; P->argw = 0;
            int first_rec = P->sc->nrecs;
            scope_push(P, SCOPE_SUB); scope_declare(P, P->I->sym_args);
            int sub_scope = scope_top(P)->id;
            pp_node *b = node(P, N_BLOCK);
            scope_push(P, SCOPE_BLOCK);
            int noargs = 0, nsig = 0;
            if (cur_is_op(P, "(")) {
                int proto = skip_prototype(P);
                n->aux = (uint8_t)P->proto_mask;
                advance(P); nsig = parse_sub_params(P, b, n->sym->name + 1, &noargs);
                if (proto) noargs = 0;   /* a non-empty prototype takes arguments */
                if (proto == 2) { n->op = 1; noargs |= 4; }
            }
            skip_attributes(P);
            declare_sub(P, n->sym, noargs);
            for (int i = 0; i < P->ndecl; i++) if (P->decl[i] == n->sym) P->declmask[i] = n->aux;
            if (cur_is_op(P, ";")) {             /* forward declaration; a signature here is discarded */
                advance(P);
                scope_pop(P); scope_pop(P);
                P->in_sub = save; P->state_n = save_state; P->argw = save_argw;
                return NULL;
            }
            parse_block_stmts(P, b);
            if (b->n == nsig) node_add(b, node(P, N_NOP));   /* empty body: do not return the last binding */
            int nv = scope_pop(P);
            if (nv) { b->flags |= 1; b->aux = (uint8_t)nv; }
            n->a = b;
            scope_pop(P);
            if (P->argw) b->flags |= PP_BLOCK_WARGS;
            P->in_sub = save; P->state_n = save_state; P->argw = save_argw;
            pp_interp *I = P->I;
            /* Named subs are normally defined before the file runs, closing over the file scope.
               One that uses `my` variables of an enclosing block ({ my $n; sub next_id { ++$n } })
               needs that block's scope instead, so it is defined when the block runs. */
            int in_place = 0;
            for (int i = first_rec; i < P->sc->nrecs && !in_place; i++)
                in_place = P->sc->recs[i].target < sub_scope && P->sc->recs[i].target != P->sc->st[0].id;
            I->all_subdefs = pp_xrealloc(I->all_subdefs, (I->nall_subdefs + 1) * sizeof(pp_node *));
            I->all_subdefs[I->nall_subdefs++] = n;
            if (in_place) {
                n->flags |= PP_SUB_INPLACE;
                pp_node *c = node(P, N_SUBCLOSE); c->sym = n->sym;
                return c;
            }
            /* defined up front, it runs directly below the file scope: the blocks the source
               nests it in are not between its variables and the file's `my` variables */
            P->sc->parent_of[sub_scope] = P->sc->st[0].id;
            I->subdefs = pp_xrealloc(I->subdefs, (I->nsubdefs + 1) * sizeof(pp_node *));
            I->subdefs[I->nsubdefs++] = n;
            return NULL;
        }
        if (tok_is_ident(t, "try") && nx->type == T_OP && !strcmp(nx->op, "{")
            && !is_declared(P, sym_sigil(P, '&', "try", 3))) {   /* a sub try(&) of the script's own wins */
            advance(P);
            pp_node *n = node(P, N_TRY);
            n->a = parse_block(P);
            if (!cur_is_ident(P, "catch")) perr(P, "try block without catch is not allowed");
            advance(P);
            expect_op(P, "(");
            if (P->cur.type != T_VAR || P->cur.sigil != '$') perr(P, "syntax error in catch");
            const char *en = P->cur.s; int el = P->cur.len;
            advance(P);
            expect_op(P, ")");
            pp_node *b = node(P, N_BLOCK);        /* catch block, with `my $e = <caught>` first */
            scope_push(P, SCOPE_BLOCK);
            pp_node *m = node(P, N_MY); m->op = 'm';
            node_add(m, mkvar_decl(P, '$', en, el));
            pp_node *as = node(P, N_ASSIGN); as->a = m; as->b = node(P, N_TRYERR);
            node_add(b, as);
            parse_block_stmts(P, b);
            int nv = scope_pop(P);
            if (nv) { b->flags |= 1; b->aux = (uint8_t)nv; }
            n->b = b;
            if (cur_is_ident(P, "finally")) { advance(P); n->c = parse_block(P); }
            return n;
        }
        if (tok_is_ident(t, "if")) { advance(P); return parse_cond_block(P, 0); }
        if (tok_is_ident(t, "unless")) { advance(P); return parse_cond_block(P, 1); }
        if (tok_is_ident(t, "while") || tok_is_ident(t, "until")) {
            int until = t->s[0] == 'u';
            advance(P);
            pp_node *n = node(P, N_WHILE); n->op = (uint8_t)until; n->sym = label;
            scope_push(P, SCOPE_BLOCK);
            expect_op(P, "(");
            n->a = cur_is_op(P, ")") ? const_int(P, 1) : while_cond_magic(P, parse_expr(P));
            expect_op(P, ")");
            n->b = parse_block(P);
            if (cur_is_ident(P, "continue")) { advance(P); n->c = parse_block(P); }
            int nv = scope_pop(P);
            if (nv) { n->flags |= 4; n->aux = (uint8_t)nv; }
            return n;
        }
        if (tok_is_ident(t, "for") || tok_is_ident(t, "foreach")) {
            advance(P);
            int is_my = 0; const char *vname = NULL; int vlen = 0;
            if (cur_is_ident(P, "my") || cur_is_ident(P, "our")) {
                advance(P);
                if (cur_is_op(P, "(")) {          /* for my ($k, $v) (LIST): iterate n at a time */
                    advance(P);
                    const char *names[16]; int lens[16]; int nvars = 0;
                    while (!cur_is_op(P, ")")) {
                        if (P->cur.type != T_VAR || P->cur.sigil != '$' || nvars >= 16)
                            perr(P, "syntax error in foreach");
                        names[nvars] = P->cur.s; lens[nvars] = P->cur.len; nvars++;
                        advance(P);
                        if (!accept_op(P, ",")) break;
                    }
                    if (!nvars) perr(P, "syntax error in foreach");
                    expect_op(P, ")");
                    expect_op(P, "(");
                    pp_node *n = node(P, N_FOREACH); n->sym = label;
                    n->b = cur_is_op(P, ")") ? node(P, N_LIST) : parse_expr(P);
                    note_argw(P, n->b);
                    expect_op(P, ")");
                    scope_push(P, SCOPE_LOOPVAR);
                    pp_node *var = node(P, N_MY); var->op = 'm'; var->flags |= 1;
                    for (int i = 0; i < nvars; i++) node_add(var, mkvar_decl(P, '$', names[i], lens[i]));
                    n->a = var;
                    n->c = parse_block(P);
                    scope_pop(P);
                    return n;
                }
                if (P->cur.type != T_VAR || P->cur.sigil != '$') perr(P, "syntax error in foreach");
                is_my = 1; vname = P->cur.s; vlen = P->cur.len;
                advance(P);
            } else if (P->cur.type == T_VAR && P->cur.sigil == '$') {
                vname = P->cur.s; vlen = P->cur.len;
                advance(P);
            }
            expect_op(P, "(");
            if (!vname) {
                scope_push(P, SCOPE_BLOCK);   /* for (my $i = 0; ...) */
                pp_node *init = cur_is_op(P, ";") ? NULL : parse_expr(P);
                if (cur_is_op(P, ";")) {
                    advance(P);
                    pp_node *n = node(P, N_FOR); n->sym = label;
                    n->a = init;
                    n->b = cur_is_op(P, ";") ? NULL : parse_expr(P);
                    expect_op(P, ";");
                    n->c = cur_is_op(P, ")") ? NULL : parse_expr(P);
                    expect_op(P, ")");
                    n->d = parse_block(P);
                    int nv = scope_pop(P);
                    if (nv) { n->flags |= 4; n->aux = (uint8_t)nv; }
                    return n;
                }
                expect_op(P, ")");
                pp_node *n = node(P, N_FOREACH); n->sym = label; n->a = NULL; n->b = init ? init : node(P, N_LIST); note_argw(P, n->b); n->c = parse_block(P);
                int nv = scope_pop(P);
                if (nv) perr(P, "internal: unexpected declaration in foreach list");
                return n;
            }
            pp_node *n = node(P, N_FOREACH); n->sym = label;
            /* the list is parsed before the loop variable comes into scope */
            n->b = cur_is_op(P, ")") ? node(P, N_LIST) : parse_expr(P);
            note_argw(P, n->b);
            expect_op(P, ")");
            if (is_my) {
                scope_push(P, SCOPE_LOOPVAR);
                pp_node *var = node(P, N_MY); var->op = 'm';
                node_add(var, mkvar_decl(P, '$', vname, vlen));
                n->a = var;
                n->c = parse_block(P);
                scope_pop(P);
            } else {
                n->a = mkvar(P, '$', vname, vlen);
                n->c = parse_block(P);
            }
            return n;
        }
        if (tok_is_ident(t, "use") || tok_is_ident(t, "no")) {
            int rs = P->sc->nrecs, is = P->sc->nids;
            P->sc->heap_depth++;                     /* this subtree runs now and is freed afterwards */
            pp_node *u = parse_use(P);
            P->sc->heap_depth--;
            if (u) exec_at_parse(P, u, rs, is);
            else P->sc->nrecs = rs;                  /* handled inside parse_use: its nodes are gone, forget their variables */
            return NULL;
        }
        if (tok_is_ident(t, "package")) {
            advance(P);
            if (P->cur.type != T_IDENT) perr(P, "syntax error after package");
            char saved[96]; strcpy(saved, P->pkg);
            snprintf(P->pkg, sizeof P->pkg, "%.*s", P->cur.len, P->cur.s);
            advance(P);
            if (P->cur.type == T_NUM) advance(P);   /* version */
            pp_node *pn = node(P, N_PACKAGE);   /* file-level code tells caller() its package */
            if (strcmp(P->pkg, "main") != 0) pn->sym = pp_intern(P->I, P->pkg, (int)strlen(P->pkg));
            if (cur_is_op(P, "{")) {            /* package X { ... } */
                pp_node *b = parse_block(P); strcpy(P->pkg, saved);
                pp_node *w = node(P, N_BLOCK); w->flags |= PP_BLOCK_PKG;
                node_add(w, pn); node_add(w, b);
                return w;
            }
            accept_op(P, ";");
            P->pkg_stmt = 1;
            return pn;
        }
        if (tok_is_ident(t, "BEGIN")) {
            advance(P);
            int rs = P->sc->nrecs, is = P->sc->nids;
            P->sc->heap_depth++;
            pp_node *b = parse_block(P);
            P->sc->heap_depth--;
            exec_at_parse(P, b, rs, is);
            return NULL;
        }
        if ((tok_is_ident(t, "INIT") || tok_is_ident(t, "CHECK") || tok_is_ident(t, "UNITCHECK")) && is_op(nx, "{")) {
            int kind = t->s[0] == 'I' ? 0 : t->s[0] == 'C' ? 1 : 2;
            pp_node *n = node(P, N_INIT);
            int file_level = P->sc->n == 1 && !P->sc->heap_depth && !P->sc->track;
            P->cur.s = "sub"; P->cur.len = 3;
            n->a = parse_ident_term(P);
            accept_op(P, ";");
            if (kind == 2) return n;                 /* UNITCHECK: when this file starts to run */
            /* INIT / CHECK: queue the block now, in the order the blocks are compiled; the main
               program runs the queues before its first statement. The block closes over the
               file scope, which is made early for it. Too late once the program runs, and not
               done for blocks inside other blocks or an eval STRING. */
            pp_interp *I = P->I;
            if (file_level && !I->init_ran && n->a->type == N_ANONSUB) {
                if (!I->pending_env) I->pending_env = pp_env_new(NULL, 0);
                pp_env *saved = I->env;
                I->env = I->pending_env;
                pp_val code = pp_eval(I, n->a, CTX_SCALAR, NULL);
                I->env = saved;
                pp_array_push(pp_global(I, pp_intern(I, kind ? "@ CHECK" : "@ INIT", kind ? 7 : 6)), code);
                pp_node *keep = node(P, N_NOP);      /* the block's AST must outlive this statement */
                keep->a = n->a; n->a = NULL;
                keep->type = N_INIT; keep->op = 9;   /* op 9: already queued, pp_exec_unit skips it */
                return keep;
            }
            return NULL;
        }
        if (tok_is_ident(t, "END") && is_op(nx, "{")) {
            /* END { ... } is an anonymous sub that runs at exit: parse it as `sub { ... }` */
            pp_node *n = node(P, N_END);
            P->cur.s = "sub"; P->cur.len = 3;
            n->a = parse_ident_term(P);
            accept_op(P, ";");
            return n;
        }
    }

    pp_node *e = parse_expr(P);
    for (;;) {
        if (cur_is_ident(P, "if") || cur_is_ident(P, "unless")) {
            int un = P->cur.s[0] == 'u'; advance(P);
            pp_node *n = node(P, N_IF); n->op = (uint8_t)un; n->a = parse_expr(P); n->b = e; n->flags |= 2;
            e = n; continue;
        }
        if (cur_is_ident(P, "while") || cur_is_ident(P, "until")) {
            int un = P->cur.s[0] == 'u'; advance(P);
            pp_node *n = node(P, N_WHILE); n->op = (uint8_t)un; n->a = while_cond_magic(P, parse_expr(P)); n->b = e; n->flags |= 2;
            if (e->type == N_DOBLOCK) { n->flags |= 8; n->b = e->a; }
            e = n; continue;
        }
        if (cur_is_ident(P, "for") || cur_is_ident(P, "foreach")) {
            advance(P);
            pp_node *n = node(P, N_FOREACH); n->a = NULL; n->b = parse_expr(P); n->c = e; n->flags |= 2; note_argw(P, n->b);
            e = n; continue;
        }
        break;
    }
    if (!cur_is_op(P, "}") && P->cur.type != T_EOF) expect_op(P, ";");
    return e;
}
static pp_node *parse_statement(pp_parser *P) {
    check_parse_stack(P);       /* nested blocks recurse without going through parse_term */
    pp_node *st = parse_statement_inner(P);
    if (st && scope_top(P)->warnings) st->flags |= PP_STMT_WARN;
    return st;
}

/* ------------------------------------------------------------------ */
/* Entry points                                                        */
/* ------------------------------------------------------------------ */

static void parser_init(pp_parser *P, pp_interp *I, const char *file, const char *src, int len, int line, pp_parser *parent) {
    memset(P, 0, sizeof *P);
    P->I = I; P->file = file;
    strcpy(P->pkg, parent ? parent->pkg : "main");
    P->L.src = src; P->L.len = len; P->L.pos = 0; P->L.line = line;
    if (parent) { P->sc = parent->sc; P->decl = parent->decl; P->ndecl = parent->ndecl; P->declnoargs = parent->declnoargs; P->declmask = parent->declmask; P->our_names = parent->our_names; P->our_syms = parent->our_syms; P->nours = parent->nours; }
    else { P->sc = pp_xmalloc(sizeof(pscopes)); memset(P->sc, 0, sizeof(pscopes)); P->sc->arena = I->arena; P->owns_sc = 1; }
}
static void parser_free(pp_parser *P) {
    for (int i = 0; i < P->L.nstrs; i++) free(P->L.strs[i]);
    free(P->L.strs);
    if (P->owns_sc) {
        free(P->decl);
        free(P->declnoargs);
        free(P->declmask);
        for (int i = 0; i < P->nours; i++) free(P->our_names[i]);
        free(P->our_names); free(P->our_syms);
        pscopes *S = P->sc;
        while (S->n) scope_pop(P);
        free(S->st); free(S->collapsed); free(S->parent_of); free(S->recs); free(S->ours); free(S->made);
        free(S);
    }
}

/* list arrays grow by doubling while parsing; once the tree is final they are copied
   into the arena (exact size, no malloc header). Heap nodes keep malloc'd lists. */
static void shrink_ast(pp_arena *arena, pp_node *n) {
    if (!n) return;
    shrink_ast(arena, n->a); shrink_ast(arena, n->b);
    if (n->type == N_CONST) return;
    shrink_ast(arena, n->c); shrink_ast(arena, n->d);
    for (int i = 0; i < n->n; i++) shrink_ast(arena, n->list[i]);
    if (!n->list) return;
    if (n->flags & PP_NODE_HEAP) { if (n->n) n->list = pp_xrealloc(n->list, n->n * sizeof(pp_node *)); return; }
    pp_node **nl = NULL;
    if (n->n) { nl = pp_arena_alloc(arena, n->n * sizeof(pp_node *)); memcpy(nl, n->list, n->n * sizeof(pp_node *)); }
    free(n->list);
    n->list = nl;
}

/* Parse a file, or (site != NULL) the code of an eval STRING. The eval code starts with the
   package, pragmas and `my` variables of the place it is evaluated at, its nodes go into an
   arena of their own (*arena, freed by the caller once the code has run), and a syntax error
   goes to errbuf instead of stderr. */
/* the first file parsed (not an eval string) is the main script: its __END__ text is DATA */
static __attribute__((noinline)) void note_main_script(pp_parser *P, int is_file) {
    P->is_main = (uint8_t)(is_file && !P->I->main_seen);
    if (is_file) P->I->main_seen = 1;
}
static pp_node *parse_unit(pp_interp *I, const char *filename, const char *src, int *err, pp_node *site, char *errbuf, int errcap, pp_arena **arena) {
    pp_parser P;
    parser_init(&P, I, filename, src, (int)strlen(src), 1, NULL);
    note_main_script(&P, site == NULL);
    *err = 0;
    int subs_before = I->nall_subdefs;
    if (site) { P.sc->arena = pp_arena_new(); P.sc->track = 1; *arena = P.sc->arena; }
    if (setjmp(P.jb)) {
        char buf[512];
        int n = P.err_has_loc ? pp_snprintf(buf, sizeof buf, "%s\n", P.errmsg)
                              : pp_snprintf(buf, sizeof buf, "%s at %s line %d.\n", P.errmsg, filename, P.cur.line);
        if (errbuf) snprintf(errbuf, errcap, "%s", buf);
        else pp_host_write(2, buf, n);
        *err = 1;
        if (site) {
            pp_arena *ea = P.sc->arena;
            int defined_early = 0;               /* a BEGIN block in the string already defined a sub from it */
            for (int i = subs_before; i < I->nall_subdefs; i++) if (I->all_subdefs[i]->flags & PP_SUB_EARLY) defined_early = 1;
            if (defined_early) pp_arena_absorb(I->arena, ea);
            else {
                I->nall_subdefs = subs_before;   /* the string did not compile: its subs are not defined */
                /* nothing refers to the partial tree: release what its nodes own, then the arena */
                for (int i = 0; i < P.sc->nmade; i++) {
                    pp_node *nd = P.sc->made[i];
                    if (nd->type == N_CONST) { pp_release(nd->val); nd->val = pp_undef(); }
                    else { free(nd->list); nd->list = NULL; }
                }
                pp_arena_free(ea);
            }
            *arena = NULL;
        }
        parser_free(&P);      /* a file's partial tree stays in the arena until pp_free */
        return NULL;
    }
    advance(&P);
    pp_node *unit = node(&P, N_BLOCK);
    scope_push(&P, SCOPE_UNIT);
    if (site) {
        pscope *top = scope_top(&P);
        top->strict_vars = site->op & 1; top->strict_subs = (site->op & 2) != 0;
        top->strict_refs = (site->op & 4) != 0; top->warnings = (site->op & 8) != 0;
        for (int i = 0; i < site->n; i++)            /* slots 0..k-1: the caller fills them with the cells */
            if (site->list[i]->flags & PP_VAR_LEX) scope_declare(&P, site->list[i]->sym);
        if (site->b) {
            snprintf(P.pkg, sizeof P.pkg, "%s", site->b->val.u.s->d);
            pp_node *pn = node(&P, N_PACKAGE);
            pn->sym = pp_intern(I, P.pkg, (int)strlen(P.pkg));
            node_add(unit, pn);
        }
    }
    while (P.cur.type != T_EOF) {
        if (cur_is_op(&P, "}")) perr(&P, "Unmatched right curly bracket");   /* would loop forever: nothing consumes it */
        pp_node *s = parse_statement(&P);
        if (s) node_add(unit, s);
    }
    int nv = scope_pop(&P);
    unit->flags |= 1;
    if (P.sc->has_goto) unit->flags |= PP_UNIT_GOTO;
    unit->aux = (uint8_t)nv;
    scope_finalize(&P, 0, -1);
    shrink_ast(P.sc->arena, unit);
    for (int i = 0; i < I->nsubdefs; i++) shrink_ast(P.sc->arena, I->subdefs[i]);
    parser_free(&P);
    return unit;
}
pp_node *pp_parse(pp_interp *I, const char *filename, const char *src, int *err) {
    return parse_unit(I, filename, src, err, NULL, NULL, 0, NULL);
}
pp_node *pp_parse_eval(pp_interp *I, const char *filename, const char *src, pp_node *site, char *errbuf, int errcap, pp_arena **arena) {
    int err = 0;
    return parse_unit(I, filename, src, &err, site, errbuf, errcap, arena);
}

/* parse an interpolated expression with the scopes of the enclosing parser; with
   PARSE_AS_BLOCK or'ed into the line, a sequence of statements as a block of its own */
static pp_node *parse_expr_shared(pp_parser *parent, const char *src, int len, int line, int *err) {
    pp_parser P;
    int as_block = (line & PARSE_AS_BLOCK) != 0;
    line &= ~PARSE_AS_BLOCK;
    parser_init(&P, parent->I, parent->file, src, len, line, parent);
    *err = 0;
    if (setjmp(P.jb)) {
        snprintf(parent->errmsg, sizeof parent->errmsg, "%s", P.errmsg);
        parent->err_has_loc = P.err_has_loc;
        *err = 1; parser_free(&P); return NULL;
    }
    advance(&P);
    pp_node *e;
    if (as_block) {
        e = node(&P, N_BLOCK);
        scope_push(&P, SCOPE_BLOCK);
        while (P.cur.type != T_EOF) { pp_node *st = parse_statement(&P); if (st) node_add(e, st); }
        int nv = scope_pop(&P);
        if (nv) { e->flags |= 1; e->aux = (uint8_t)nv; }
    }
    else e = parse_expr(&P);
    if (P.cur.type != T_EOF) { snprintf(parent->errmsg, sizeof parent->errmsg, "syntax error in interpolated string"); *err = 1; parser_free(&P); return NULL; }
    parser_free(&P);
    return e;
}
pp_node *pp_parse_expr_string(pp_interp *I, const char *src, int len, int line, const char *file, int *err) {
    (void)I; (void)src; (void)len; (void)line; (void)file;
    *err = 1;
    return NULL;
}
