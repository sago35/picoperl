/* Built-in functions for picoperl. */
#include "pp_internal.h"
#include <math.h>
#include <ctype.h>

enum {
    B_NONE = 0,
    B_PRINT, B_PRINTF, B_SAY, B_PUSH, B_UNSHIFT, B_POP, B_SHIFT, B_SPLICE, B_JOIN, B_SPLIT,
    B_SPRINTF, B_REVERSE, B_SORT, B_MAP, B_GREP, B_DIE, B_WARN, B_SUBSTR, B_INDEX, B_RINDEX,
    B_ATAN2, B_LENGTH, B_LC, B_UC, B_LCFIRST, B_UCFIRST, B_CHR, B_ORD, B_INT, B_ABS, B_SQRT,
    B_SIN, B_COS, B_EXP, B_LOG, B_HEX, B_OCT, B_DEFINED, B_REF, B_SCALAR, B_EXISTS, B_DELETE,
    B_KEYS, B_VALUES, B_EACH, B_EXIT, B_UNDEF, B_CHOMP, B_CHOP, B_RAND, B_SRAND, B_TIME, B_SLEEP,
    B_READLINE, B_BINMODE, B_QUOTEMETA, B_LOCAL_IGNORE, B_SELECT, B_BLESS, B_POS,
    B_OPEN, B_CLOSE, B_EOF, B_SEEK, B_TELL, B_ALARM, B_ANY, B_ALL, B_ABS_END
};

static const struct { const char *name; int id; int kind; } builtins[] = {
    { "print", B_PRINT, BK_LIST }, { "printf", B_PRINTF, BK_LIST }, { "say", B_SAY, BK_LIST },
    { "push", B_PUSH, BK_LIST }, { "unshift", B_UNSHIFT, BK_LIST }, { "pop", B_POP, BK_UNARY }, { "shift", B_SHIFT, BK_UNARY },
    { "splice", B_SPLICE, BK_LIST }, { "join", B_JOIN, BK_LIST }, { "split", B_SPLIT, BK_LIST }, { "sprintf", B_SPRINTF, BK_LIST },
    { "reverse", B_REVERSE, BK_LIST }, { "sort", B_SORT, BK_LIST }, { "map", B_MAP, BK_LIST }, { "grep", B_GREP, BK_LIST },
    { "any", B_ANY, BK_LIST }, { "all", B_ALL, BK_LIST },
    { "die", B_DIE, BK_LIST }, { "warn", B_WARN, BK_LIST }, { "substr", B_SUBSTR, BK_LIST }, { "index", B_INDEX, BK_LIST },
    { "rindex", B_RINDEX, BK_LIST }, { "atan2", B_ATAN2, BK_LIST }, { "length", B_LENGTH, BK_UNARY }, { "lc", B_LC, BK_UNARY },
    { "uc", B_UC, BK_UNARY }, { "lcfirst", B_LCFIRST, BK_UNARY }, { "ucfirst", B_UCFIRST, BK_UNARY }, { "chr", B_CHR, BK_UNARY },
    { "ord", B_ORD, BK_UNARY }, { "int", B_INT, BK_UNARY }, { "abs", B_ABS, BK_UNARY }, { "sqrt", B_SQRT, BK_UNARY },
    { "sin", B_SIN, BK_UNARY }, { "cos", B_COS, BK_UNARY }, { "exp", B_EXP, BK_UNARY }, { "log", B_LOG, BK_UNARY },
    { "hex", B_HEX, BK_UNARY }, { "oct", B_OCT, BK_UNARY }, { "defined", B_DEFINED, BK_UNARY }, { "ref", B_REF, BK_UNARY },
    { "scalar", B_SCALAR, BK_UNARY }, { "exists", B_EXISTS, BK_UNARY }, { "delete", B_DELETE, BK_UNARY }, { "keys", B_KEYS, BK_UNARY },
    { "values", B_VALUES, BK_UNARY }, { "each", B_EACH, BK_UNARY }, { "exit", B_EXIT, BK_UNARY }, { "undef", B_UNDEF, BK_UNARY },
    { "chomp", B_CHOMP, BK_UNARY }, { "chop", B_CHOP, BK_UNARY }, { "rand", B_RAND, BK_UNARY }, { "srand", B_SRAND, BK_UNARY },
    { "time", B_TIME, BK_NOARG }, { "sleep", B_SLEEP, BK_UNARY }, { "readline", B_READLINE, BK_NOARG }, { "binmode", B_BINMODE, BK_LIST },
    { "quotemeta", B_QUOTEMETA, BK_UNARY }, { "pos", B_POS, BK_UNARY },
    { "open", B_OPEN, BK_LIST }, { "close", B_CLOSE, BK_UNARY }, { "eof", B_EOF, BK_UNARY },
    { "seek", B_SEEK, BK_LIST }, { "tell", B_TELL, BK_UNARY }, { "alarm", B_ALARM, BK_UNARY },
    { "select", B_SELECT, BK_LIST }, { "bless", B_BLESS, BK_LIST },
    { NULL, 0, 0 }
};

int pp_builtin_id(const char *name, int len, int *kind) {
    for (int i = 0; builtins[i].name; i++) {
        if ((int)strlen(builtins[i].name) == len && memcmp(builtins[i].name, name, len) == 0) {
            if (kind) *kind = builtins[i].kind;
            return builtins[i].id;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static int eval_args(pp_interp *I, pp_node *n, int from, pp_list *args) {
    for (int i = from; i < n->n; i++) {
        pp_eval(I, n->list[i], CTX_LIST, args);
        if (I->ctl) return 0;
    }
    return 1;
}

static pp_val unary_arg(pp_interp *I, pp_node *n) {
    if (n->n == 0) return pp_copy(I->g_underscore->u.scalar);
    return pp_eval(I, n->list[0], CTX_SCALAR, NULL);
}

static pp_val ret(int ctx, pp_list *out, pp_val v) {
    if (ctx == CTX_LIST && out) { pp_list_push(out, v); return pp_undef(); }
    if (ctx == CTX_VOID) { pp_release(v); return pp_undef(); }
    return v;
}

static pp_val ret_list(int ctx, pp_list *out, pp_list *tmp) {
    /* moves tmp into out (list ctx) or returns last (scalar) */
    pp_val r = pp_undef();
    if (ctx == CTX_LIST && out) { for (int i = 0; i < tmp->n; i++) pp_list_push(out, tmp->v[i]); tmp->n = 0; }
    else if (tmp->n) { r = tmp->v[tmp->n - 1]; tmp->n--; }
    pp_list_free(tmp);
    return r;
}

static void str_append(pp_str **s, const char *p, int len) {
    *s = pp_str_reserve(*s, len);
    memcpy((*s)->d + (*s)->len, p, len);
    (*s)->len += len;
    (*s)->d[(*s)->len] = 0;
}

static pp_obj *default_array(pp_interp *I) {
    for (pp_env *e = I->env; e; e = e->parent)
        if (e->is_sub) return pp_env_cell(e, 0, '@');
    return I->g_argv;
}

/* the container designated by a builtin argument (for keys/values/each/pop/shift) */
static pp_obj *arg_container(pp_interp *I, pp_node *a, int want_hash) {
    if (a->type == N_VAR && a->sym->name[0] == '%') return pp_get_hash(I, a, 0, NULL);
    if (a->type == N_VAR && a->sym->name[0] == '@') return pp_get_array(I, a, 0, NULL);
    if (a->type == N_RV2HV) return pp_get_hash(I, a, 1, NULL);
    if (a->type == N_RV2AV) return pp_get_array(I, a, 1, NULL);
    if (a->type == N_MY) { pp_val v = pp_eval(I, a, CTX_VOID, NULL); pp_release(v); return arg_container(I, a->list[0], want_hash); }
    pp_val v = pp_eval(I, a, CTX_SCALAR, NULL);
    if (I->ctl) return NULL;
    if (v.t == PP_REF && (v.u.o->kind == PP_OHASH || v.u.o->kind == PP_OARRAY)) return v.u.o;
    pp_release(v);
    pp_die(I, want_hash ? "Not a HASH reference" : "Not an ARRAY reference");
    return NULL;
}

/* ------------------------------------------------------------------ */
/* sprintf                                                             */
/* ------------------------------------------------------------------ */

pp_val pp_sprintf(pp_interp *I, pp_list *args, int start) {
    if (args->n <= start) return pp_strn("", 0);
    pp_str *fmt = pp_to_str(args->v[start]);
    pp_str *r = pp_str_new(NULL, 0);
    int ai = start + 1;
    const char *f = fmt->d; int flen = fmt->len;
    for (int i = 0; i < flen; ) {
        if (f[i] != '%') { int j = i; while (j < flen && f[j] != '%') j++; str_append(&r, f + i, j - i); i = j; continue; }
        i++;
        if (i >= flen) { str_append(&r, "%", 1); break; }
        if (f[i] == '%') { str_append(&r, "%", 1); i++; continue; }
        char spec[32]; int sn = 0;
        spec[sn++] = '%';
        /* flags */
        while (i < flen && strchr("-+ 0#", f[i])) { if (sn < 20) spec[sn++] = f[i]; i++; }
        /* vector flag not supported */
        /* width */
        if (i < flen && f[i] == '*') { int w = ai < args->n ? (int)pp_to_int(args->v[ai++]) : 0; if (w < 0) { spec[sn++] = '-'; w = -w; } sn += pp_snprintf(spec + sn, sizeof spec - sn, "%d", w); i++; }
        else while (i < flen && isdigit((unsigned char)f[i])) { if (sn < 20) spec[sn++] = f[i]; i++; }
        /* precision */
        int has_prec = 0, prec = 0;
        if (i < flen && f[i] == '.') {
            has_prec = 1; i++;
            if (i < flen && f[i] == '*') { prec = ai < args->n ? (int)pp_to_int(args->v[ai++]) : 0; i++; }
            else { while (i < flen && isdigit((unsigned char)f[i])) { prec = prec * 10 + (f[i] - '0'); i++; } }
            sn += pp_snprintf(spec + sn, sizeof spec - sn, ".%d", prec);
        }
        /* length modifiers (ignored) */
        while (i < flen && strchr("hlqLV", f[i])) i++;
        if (i >= flen) break;
        char conv = f[i++];
        pp_val arg = ai < args->n ? args->v[ai] : pp_undef();
        if (ai < args->n) ai++;
        char buf[400];
        int bn = 0;
        switch (conv) {
        case 'd': case 'i': {
            int64_t v = pp_to_int(arg);
            snprintf(spec + sn, sizeof spec - sn, "lld");
            bn = pp_snprintf(buf, sizeof buf, spec, (long long)v);
            break;
        }
        case 'u': {
            int64_t v = pp_to_int(arg);
            snprintf(spec + sn, sizeof spec - sn, "llu");
            bn = pp_snprintf(buf, sizeof buf, spec, (unsigned long long)v);
            break;
        }
        case 'x': case 'X': case 'o': {
            int64_t v = pp_to_int(arg);
            spec[sn] = 'l'; spec[sn + 1] = 'l'; spec[sn + 2] = conv; spec[sn + 3] = 0;
            bn = pp_snprintf(buf, sizeof buf, spec, (unsigned long long)v);
            break;
        }
        case 'b': case 'B': {
            uint64_t v = (uint64_t)pp_to_int(arg);
            char bits[70]; int k = 0;
            if (v == 0) bits[k++] = '0';
            while (v) { bits[k++] = '0' + (v & 1); v >>= 1; }
            /* handle width / '-' / '0' flags and '#' */
            int left = 0, zero = 0, alt = 0, width = 0;
            for (int q = 1; q < sn; q++) { if (spec[q] == '-') left = 1; else if (spec[q] == '0' && width == 0) zero = 1; else if (spec[q] == '#') alt = 1; else if (isdigit((unsigned char)spec[q])) width = width * 10 + (spec[q] - '0'); else if (spec[q] == '.') break; }
            char tmp[100]; int tn = 0;
            if (alt && k > 1) { tmp[tn++] = '0'; tmp[tn++] = conv; }
            if (has_prec) while (k < prec && k < 64) bits[k++] = '0';
            for (int q = k - 1; q >= 0; q--) tmp[tn++] = bits[q];
            tmp[tn] = 0;
            if (width > tn && !left) { int pad = width - tn; char padc = zero && !has_prec ? '0' : ' '; for (int q = 0; q < pad; q++) buf[bn++] = padc; }
            memcpy(buf + bn, tmp, tn); bn += tn;
            if (width > tn && left) { int pad = width - tn; for (int q = 0; q < pad; q++) buf[bn++] = ' '; }
            buf[bn] = 0;
            break;
        }
        case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': {
            double v = pp_to_num(arg);
            spec[sn] = conv; spec[sn + 1] = 0;
            bn = pp_snprintf(buf, sizeof buf, spec, v);
            break;
        }
        case 'c': {
            int64_t v = pp_to_int(arg);
            char tmp[8]; int tn = 0;
            if (v < 0x80) tmp[tn++] = (char)v;
            else if (v < 0x800) { tmp[tn++] = (char)(0xC0 | (v >> 6)); tmp[tn++] = (char)(0x80 | (v & 0x3F)); }
            else if (v < 0x10000) { tmp[tn++] = (char)(0xE0 | (v >> 12)); tmp[tn++] = (char)(0x80 | ((v >> 6) & 0x3F)); tmp[tn++] = (char)(0x80 | (v & 0x3F)); }
            else { tmp[tn++] = (char)(0xF0 | (v >> 18)); tmp[tn++] = (char)(0x80 | ((v >> 12) & 0x3F)); tmp[tn++] = (char)(0x80 | ((v >> 6) & 0x3F)); tmp[tn++] = (char)(0x80 | (v & 0x3F)); }
            tmp[tn] = 0;
            spec[sn] = 's'; spec[sn + 1] = 0;
            bn = pp_snprintf(buf, sizeof buf, spec, tmp);
            break;
        }
        case 's': {
            pp_str *s = pp_to_str(arg);
            int left = 0, width = 0;
            for (int q = 1; q < sn; q++) { if (spec[q] == '-') left = 1; else if (isdigit((unsigned char)spec[q]) && !(spec[q] == '0' && width == 0)) width = width * 10 + (spec[q] - '0'); else if (spec[q] == '.') break; }
            int len = s->len;
            if (has_prec && prec < len) len = prec;
            int pad = width > len ? width - len : 0;
            if (!left) for (int q = 0; q < pad; q++) str_append(&r, " ", 1);
            str_append(&r, s->d, len);
            if (left) for (int q = 0; q < pad; q++) str_append(&r, " ", 1);
            pp_str_decref(s);
            continue;
        }
        default:
            spec[sn] = conv; spec[sn + 1] = 0;
            bn = pp_snprintf(buf, sizeof buf, "%s", spec);
            ai--;
            break;
        }
        if (bn > (int)sizeof buf - 1) bn = (int)sizeof buf - 1;
        str_append(&r, buf, bn);
    }
    pp_str_decref(fmt);
    (void)I;
    return pp_str_val(r);
}

/* ------------------------------------------------------------------ */
/* sort                                                                */
/* ------------------------------------------------------------------ */

typedef struct { pp_interp *I; pp_node *blk; int failed; } sort_ctx;

static int sort_cmp(sort_ctx *S, pp_val a, pp_val b) {
    if (S->failed) return 0;
    if (!S->blk) return pp_str_cmp(a, b);
    pp_interp *I = S->I;
    pp_release(I->g_a->u.scalar); I->g_a->u.scalar = pp_copy(a);
    pp_release(I->g_b->u.scalar); I->g_b->u.scalar = pp_copy(b);
    pp_val r = pp_eval(I, S->blk, CTX_SCALAR, NULL);
    if (I->ctl) { S->failed = 1; pp_release(r); return 0; }
    int c = (int)pp_to_int(r);
    pp_release(r);
    return c;
}

static void merge_sort(sort_ctx *S, pp_val *v, pp_val *tmp, int n) {
    if (n < 2) return;
    if (n == 2) { if (sort_cmp(S, v[0], v[1]) > 0) { pp_val t = v[0]; v[0] = v[1]; v[1] = t; } return; }
    int mid = n / 2;
    merge_sort(S, v, tmp, mid);
    merge_sort(S, v + mid, tmp, n - mid);
    int i = 0, j = mid, k = 0;
    while (i < mid && j < n) { if (sort_cmp(S, v[i], v[j]) <= 0) tmp[k++] = v[i++]; else tmp[k++] = v[j++]; }
    while (i < mid) tmp[k++] = v[i++];
    while (j < n) tmp[k++] = v[j++];
    memcpy(v, tmp, n * sizeof(pp_val));
}

static pp_val do_sort(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_list items = {0};
    if (!eval_args(I, n, 0, &items)) { pp_list_free(&items); return pp_undef(); }
    sort_ctx S = { I, n->a, 0 };
    pp_val sa = pp_undef(), sb = pp_undef();
    if (n->a) { sa = I->g_a->u.scalar; sb = I->g_b->u.scalar; I->g_a->u.scalar = pp_undef(); I->g_b->u.scalar = pp_undef(); }
    if (items.n > 1) {
        pp_val *tmp = pp_xmalloc(items.n * sizeof(pp_val));
        merge_sort(&S, items.v, tmp, items.n);
        free(tmp);
    }
    if (n->a) { pp_release(I->g_a->u.scalar); pp_release(I->g_b->u.scalar); I->g_a->u.scalar = sa; I->g_b->u.scalar = sb; }
    if (I->ctl) { pp_list_free(&items); return pp_undef(); }
    if (ctx != CTX_LIST || !out) { int c = items.n; pp_list_free(&items); return pp_int(c); }
    return ret_list(ctx, out, &items);
}

static pp_val do_map_grep(pp_interp *I, pp_node *n, int ctx, pp_list *out, int is_map) {
    pp_list items = {0}, res = {0};
    if (!eval_args(I, n, 0, &items)) { pp_list_free(&items); return pp_undef(); }
    pp_obj *us = I->g_underscore;
    pp_val saved = us->u.scalar; us->u.scalar = pp_undef();
    int count = 0;
    for (int i = 0; i < items.n && !I->ctl; i++) {
        pp_release(us->u.scalar);
        us->u.scalar = pp_copy(items.v[i]);
        if (is_map) {
            pp_val v = pp_eval(I, n->a, CTX_LIST, &res);
            pp_release(v);
        } else {
            pp_val v = pp_eval(I, n->a, CTX_SCALAR, NULL);
            if (!I->ctl && pp_truth(v)) { pp_list_push(&res, pp_copy(us->u.scalar)); }
            pp_release(v);
        }
        /* $_ is a copy here, not an alias like perl: say so once when it would matter */
        if (!(I->compat_warn & 2) && !I->ctl && !pp_val_same(us->u.scalar, items.v[i])) {
            I->compat_warn |= 2;
            pp_warn(I, "picoperl: modifying $_ inside %s does not change the original list", is_map ? "map" : "grep");
        }
    }
    pp_release(us->u.scalar); us->u.scalar = saved;
    pp_list_free(&items);
    if (I->ctl) { pp_list_free(&res); return pp_undef(); }
    count = res.n;
    if (ctx != CTX_LIST || !out) { pp_list_free(&res); return pp_int(count); }
    return ret_list(ctx, out, &res);
}

/* any { } LIST / all { } LIST (perl 5.42): grep that short-circuits to a boolean */
static pp_val do_any_all(pp_interp *I, pp_node *n, int ctx, pp_list *out, int is_all) {
    pp_list items = {0};
    if (!eval_args(I, n, 0, &items)) { pp_list_free(&items); return pp_undef(); }
    pp_obj *us = I->g_underscore;
    pp_val saved = us->u.scalar; us->u.scalar = pp_undef();
    int result = is_all;
    for (int i = 0; i < items.n && !I->ctl; i++) {
        pp_release(us->u.scalar);
        us->u.scalar = pp_copy(items.v[i]);
        pp_val v = pp_eval(I, n->a, CTX_SCALAR, NULL);
        int t = !I->ctl && pp_truth(v);
        pp_release(v);
        if (I->ctl) break;
        if (is_all ? !t : t) { result = !is_all; break; }
    }
    pp_release(us->u.scalar); us->u.scalar = saved;
    pp_list_free(&items);
    if (I->ctl) return pp_undef();
    return ret(ctx, out, result ? pp_int(1) : pp_strn("", 0));
}

/* ------------------------------------------------------------------ */
/* split                                                               */
/* ------------------------------------------------------------------ */

static pp_val do_split(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_list args = {0};
    pp_obj *ro = NULL;
    int from = 0;
    /* split /PATTERN/, ... : the first argument is a pattern, not a match */
    if (n->n > 0 && n->list[0]->type == N_MATCH && !n->list[0]->a && !(n->list[0]->op & PP_M_QR)) {
        ro = pp_regex_of(I, n->list[0]->b, n->list[0]->aux);
        if (!ro) return pp_undef();
        from = 1;
        pp_list_push(&args, pp_undef());
    }
    if (!eval_args(I, n, from, &args)) { if (ro) pp_obj_decref(ro); pp_list_free(&args); return pp_undef(); }
    pp_str *pat = NULL;
    if (!ro) {
        pat = args.n > 0 ? pp_to_str(args.v[0]) : pp_str_new(" ", 1);
        if (!(pat->len == 1 && pat->d[0] == ' ')) {
            ro = pp_regex_from_val(I, args.n > 0 ? args.v[0] : pp_strn("", 0), 0);
            if (!ro) { pp_str_decref(pat); pp_list_free(&args); return pp_undef(); }
        }
    }
    pp_str *s = args.n > 1 ? pp_to_str(args.v[1]) : pp_to_str(I->g_underscore->u.scalar);
    int limit = args.n > 2 ? (int)pp_to_int(args.v[2]) : 0;
    pp_list res = {0};
    const char *p = s->d; int len = s->len;
    if (!ro) {
        /* awk mode: split on whitespace runs, skip leading whitespace */
        int i = 0;
        while (i < len) {
            while (i < len && isspace((unsigned char)p[i])) i++;
            if (i >= len) break;
            if (limit > 0 && res.n == limit - 1) { pp_list_push(&res, pp_strn(p + i, len - i)); i = len; break; }
            int st = i;
            while (i < len && !isspace((unsigned char)p[i])) i++;
            pp_list_push(&res, pp_strn(p + st, i - st));
        }
    } else if (!pp_split_regex(I, ro, p, len, limit, &res)) {
        pp_obj_decref(ro); if (pat) pp_str_decref(pat); pp_str_decref(s); pp_list_free(&args); pp_list_free(&res);
        return pp_undef();
    }
    /* splitting an empty string gives no fields at all, whatever the limit */
    if (len == 0) { for (int i = 0; i < res.n; i++) pp_release(res.v[i]); res.n = 0; }
    /* remove trailing empty fields when limit == 0 */
    if (limit == 0) while (res.n > 0 && (res.v[res.n - 1].t == PP_UNDEF || (res.v[res.n - 1].t == PP_STR && res.v[res.n - 1].u.s->len == 0))) { pp_release(res.v[res.n - 1]); res.n--; }
    if (ro) pp_obj_decref(ro);
    if (pat) pp_str_decref(pat);
    pp_str_decref(s);
    pp_list_free(&args);
    if (ctx != CTX_LIST || !out) { int c = res.n; pp_list_free(&res); return pp_int(c); }
    return ret_list(ctx, out, &res);
}

/* ------------------------------------------------------------------ */
/* print                                                               */
/* ------------------------------------------------------------------ */

/* ---- file handles (open my $fh, '<', $path) ---- */

static void set_errno_var(pp_interp *I, const char *msg) {
    pp_obj *c = pp_global(I, pp_intern(I, "$!", 2));
    pp_release(c->u.scalar);
    c->u.scalar = pp_cstr(msg);
}

/* What a value names as a file handle: a handle from open(my $fh ...), a glob (\*FH, *FH,
   "FH") with a file opened under it, or a standard handle. Returns the file (borrowed) and
   sets *fd to -1; or NULL with *fd = 0 / 1 / 2 for STDIN / STDOUT / STDERR; or NULL and
   *fd = -1 when it is no handle at all. */
static pp_obj *handle_of(pp_interp *I, pp_val v, int *fd) {
    *fd = -1;
    pp_sym *gs = NULL;
    if (v.t == PP_REF && v.u.o->kind == PP_OFILE) return v.u.o;
    if (v.t == PP_REF && v.u.o->kind == PP_OGLOB) gs = pp_glob_named(I, v.u.o->u.glob.name.u.s->d);
    else if (v.t == PP_INT && (v.u.i == 1 || v.u.i == 2)) { *fd = (int)v.u.i; return NULL; }   /* STDOUT / STDERR written out */
    else if (v.t == PP_STR && v.u.s->len > 0) gs = pp_glob_named(I, v.u.s->d);
    if (!gs) return NULL;
    if (gs->gcell && gs->gcell->kind == PP_OGLOB && gs->gcell->u.glob.io) return gs->gcell->u.glob.io;
    if (!strcmp(gs->name, "*STDIN")) *fd = 0;
    else if (!strcmp(gs->name, "*STDOUT")) *fd = 1;
    else if (!strcmp(gs->name, "*STDERR")) *fd = 2;
    return NULL;
}
/* evaluated argument -> the PP_OFILE object (borrowed) or NULL + die */
static pp_obj *file_of(pp_interp *I, pp_val v, const char *what) {
    int fd;
    pp_obj *f = handle_of(I, v, &fd);
    if (f) return f;
    pp_die(I, "%s: not a filehandle (expected a handle from open)", what);
    return NULL;
}
/* the glob a value names, for open(FH, ...) / open($globref, ...): NULL if it is none */
static pp_obj *glob_of(pp_interp *I, pp_val v) {
    if (v.t == PP_REF && v.u.o->kind == PP_OGLOB) return v.u.o;
    if (v.t == PP_STR && v.u.s->len > 1 && v.u.s->d[0] == '*') return pp_glob_obj(I, pp_glob_named(I, v.u.s->d));
    return NULL;
}

static pp_val read_fline(pp_obj *f) {
    if (!f->u.file.fp) return pp_undef();
    pp_str *s = pp_str_new(NULL, 0);
    char c;
    while (pp_host_fread(f->u.file.fp, &c, 1) == 1) {
        s = pp_str_reserve(s, 1);
        s->d[s->len++] = c;
        if (c == '\n') break;
    }
    if (s->len == 0) { pp_str_decref(s); return pp_undef(); }
    s->d[s->len] = 0;
    return pp_str_val(s);
}

/* $/ is not supported: readline always ends lines at \n; say so once */
static void check_irs(pp_interp *I) {
    if (I->compat_warn & 32) return;
    pp_sym *sym = pp_intern(I, "$/", 2);
    if (!sym->gcell) return;
    pp_val v = sym->gcell->u.scalar;
    if (v.t == PP_UNDEF || (v.t == PP_STR && v.u.s->len == 1 && v.u.s->d[0] == '\n')) return;
    I->compat_warn |= 32;
    pp_warn(I, "picoperl: $/ is ignored, readline always splits on \"\\n\"");
}

static pp_val do_open(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    if (n->n < 2) return pp_die(I, "Not enough arguments for open");
    /* open(FH, ...), open(*FH, ...), open(\*FH, ...): the file goes under that name */
    pp_node *hn = n->list[0];
    int named = hn->type == N_CALL || hn->type == N_CONST || hn->type == N_GLOB || hn->type == N_REF || hn->type == N_GLOBSLOT;
    pp_obj *glob = NULL;
    pp_val *slot = NULL;
    if (named) {
        pp_val hv = pp_eval(I, hn, CTX_SCALAR, NULL);
        if (I->ctl) { pp_release(hv); return pp_undef(); }
        glob = glob_of(I, hv);
        if (!glob && hv.t == PP_STR && hv.u.s->len > 0) glob = pp_glob_obj(I, pp_glob_named(I, hv.u.s->d));
        pp_release(hv);
        if (!glob) return pp_die(I, "Bad filehandle in open");
    } else {
        slot = pp_lvalue(I, hn);
        if (!slot) return pp_undef();
        glob = glob_of(I, *slot);          /* open($sym, ...) with a glob (Symbol::gensym) in $sym */
    }
    /* (a glob lives as long as the interpreter, so it needs no reference held here) */
    pp_list args = {0};
    if (!eval_args(I, n, 1, &args)) { pp_list_free(&args); return pp_undef(); }
    {   /* open($dup, ">&", $fh) / open($dup, ">&1") / ">&=", "<&", ">>&": another name for a handle
           that is already open. There is no real dup: both names share one file. */
        pp_str *ms = pp_to_str(args.v[0]);
        const char *m = ms->d;
        while (*m == ' ') m++;
        if (*m == '+') m++;
        if (*m == '>' && m[1] == '>') m++;
        int isdup = (*m == '>' || *m == '<') && m[1] == '&';
        if (isdup) {
            m += 2; if (*m == '=') m++;
            while (*m == ' ') m++;
            pp_val target = args.n > 1 ? pp_copy(args.v[1]) : pp_cstr(m);
            int fd = -1;
            pp_obj *file = NULL;
            if (target.t != PP_REF) {              /* a file number, or a handle's name */
                pp_str *ts = pp_to_str(target);
                if (ts->len == 1 && ts->d[0] >= '0' && ts->d[0] <= '2') fd = ts->d[0] - '0';
                else file = handle_of(I, target, &fd);
                pp_str_decref(ts);
            } else file = handle_of(I, target, &fd);
            pp_val nv = pp_undef();
            if (file) nv = pp_ref(file);
            else if (fd >= 0) nv = pp_ref(pp_glob_obj(I, pp_glob_named(I, fd == 0 ? "STDIN" : fd == 1 ? "STDOUT" : "STDERR")));
            pp_release(target); pp_str_decref(ms); pp_list_free(&args);
            if (nv.t == PP_UNDEF) { set_errno_var(I, "Bad file descriptor"); return ret(ctx, out, pp_undef()); }
            if (glob) {
                if (file) { file->refcnt++; if (glob->u.glob.io) pp_obj_decref(glob->u.glob.io); glob->u.glob.io = file; }
                pp_release(nv);
                return ret(ctx, out, pp_int(1));
            }
            slot = pp_lvalue(I, n->list[0]);
            if (!slot) { pp_release(nv); return pp_undef(); }
            pp_release(*slot); *slot = nv;
            return ret(ctx, out, pp_int(1));
        }
        pp_str_decref(ms);
    }
    if (args.n < 2) {
        if (!(I->compat_warn & 8)) { I->compat_warn |= 8; pp_warn(I, "picoperl: 2-argument open is not supported, use open(my $fh, '<', $path)"); }
        set_errno_var(I, "2-argument open is not supported");
        pp_list_free(&args);
        return ret(ctx, out, pp_undef());
    }
    pp_str *ms = pp_to_str(args.v[0]);
    const char *m = ms->d;
    char base[4] = "";
    const char *layer = strchr(m, ':');
    int mn = layer ? (int)(layer - m) : ms->len;
    while (mn > 0 && m[mn - 1] == ' ') mn--;
    if (mn == 1 && m[0] == '<') strcpy(base, "r");
    else if (mn == 1 && m[0] == '>') strcpy(base, "w");
    else if (mn == 2 && m[0] == '>' && m[1] == '>') strcpy(base, "a");
    else if (mn == 2 && m[0] == '+' && m[1] == '<') strcpy(base, "r+");
    else if (mn == 2 && m[0] == '+' && m[1] == '>') strcpy(base, "w+");
    else {
        pp_die(I, "Unknown open() mode '%s'", m);
        pp_str_decref(ms); pp_list_free(&args);
        return pp_undef();
    }
    if (layer && !(I->compat_warn & 16)) { I->compat_warn |= 16; pp_warn(I, "picoperl: IO layers ('%s') are ignored, files are bytes", layer); }
    pp_str *path = pp_to_str(args.v[1]);
    void *fp = pp_host_fopen(path->d, base);
    pp_str_decref(ms); pp_str_decref(path); pp_list_free(&args);
    if (!fp) set_errno_var(I, pp_host_io_error());
    /* like perl, the handle variable holds a (possibly closed) handle even on failure */
    pp_obj *o = pp_obj_new(PP_OFILE);
    o->u.file.fp = fp;
    o->u.file.writable = (uint8_t)(base[0] != 'r' || base[1] == '+');
    if (glob) {
        if (glob->u.glob.io) pp_obj_decref(glob->u.glob.io);
        glob->u.glob.io = o;
        return ret(ctx, out, fp ? pp_int(1) : pp_undef());
    }
    slot = pp_lvalue(I, n->list[0]);
    if (!slot) { pp_obj_decref(o); return pp_undef(); }
    pp_release(*slot);
    *slot = pp_ref(o);
    pp_obj_decref(o);
    return ret(ctx, out, fp ? pp_int(1) : pp_undef());
}

static pp_val do_print(pp_interp *I, pp_node *n, int id) {
    int fd = 1;
    pp_obj *fh = NULL;
    if (n->a) {
        pp_val f = pp_eval(I, n->a, CTX_SCALAR, NULL);
        if (I->ctl) return pp_undef();
        int hfd;
        pp_obj *hf = handle_of(I, f, &hfd);
        if (hf) {
            fh = hf; pp_obj_incref(fh); pp_release(f);
            if (!fh->u.file.fp) {
                if (I->warn_on) pp_warn(I, "%s() on closed filehandle", id == B_SAY ? "say" : id == B_PRINTF ? "printf" : "print");
                pp_obj_decref(fh);
                return pp_strn("", 0);
            }
        } else {
            pp_release(f);
            fd = hfd == 2 ? 2 : 1;
        }
    }
    pp_list args = {0};
    if (n->n == 0 && id != B_PRINTF) pp_list_push(&args, pp_copy(I->g_underscore->u.scalar));
    else if (!eval_args(I, n, 0, &args)) { pp_list_free(&args); return pp_undef(); }
    if (I->warn_on) pp_warn_uninit_args(I, n, &args, id == B_SAY ? "say" : id == B_PRINTF ? "printf" : "print");
    int wr_ok = 1;
    if (id == B_PRINTF) {
        pp_val s = pp_sprintf(I, &args, 0);
        pp_str *t = pp_to_str(s);
        if (fh) wr_ok = pp_host_fwrite(fh->u.file.fp, t->d, t->len) == t->len;
        else pp_host_write(fd, t->d, t->len);
        pp_str_decref(t); pp_release(s);
    } else {
        pp_str *buf = pp_str_new(NULL, 0);
        pp_val ofs = I->g_ofs->u.scalar, ors = I->g_ors->u.scalar;   /* $, between the arguments, $\ after them */
        for (int i = 0; i < args.n; i++) {
            if (i && ofs.t == PP_STR) str_append(&buf, ofs.u.s->d, ofs.u.s->len);
            pp_str *t; const char *p = pp_to_cstr(args.v[i], &t);
            str_append(&buf, p, t ? t->len : args.v[i].u.s->len);
            if (t) pp_str_decref(t);
        }
        if (id == B_SAY) str_append(&buf, "\n", 1);
        else if (ors.t == PP_STR) str_append(&buf, ors.u.s->d, ors.u.s->len);
        if (fh) wr_ok = pp_host_fwrite(fh->u.file.fp, buf->d, buf->len) == buf->len;
        else pp_host_write(fd, buf->d, buf->len);
        pp_str_decref(buf);
    }
    if (fh) pp_obj_decref(fh);
    pp_list_free(&args);
    if (!wr_ok) { set_errno_var(I, pp_host_io_error()); return pp_strn("", 0); }
    return pp_int(1);
}

/* ------------------------------------------------------------------ */
/* dispatcher                                                          */
/* ------------------------------------------------------------------ */

pp_val pp_builtin(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    switch (n->op) {
    case B_PRINT: case B_PRINTF: case B_SAY: return ret(ctx, out, do_print(I, n, n->op));
    case B_PUSH: case B_UNSHIFT: {
        if (n->n == 0) return pp_die(I, "Not enough arguments for push");
        pp_list vals = {0};
        if (!eval_args(I, n, 1, &vals)) { pp_list_free(&vals); return pp_undef(); }
        pp_obj *arr = pp_get_array(I, n->list[0], 1, NULL);
        if (!arr) { pp_list_free(&vals); return pp_undef(); }
        if (n->op == B_PUSH) { for (int i = 0; i < vals.n; i++) pp_array_push(arr, vals.v[i]); }
        else { for (int i = vals.n - 1; i >= 0; i--) pp_array_unshift(arr, vals.v[i]); }
        vals.n = 0; pp_list_free(&vals);
        int len = arr->u.arr.n;
        pp_obj_decref(arr);
        return ret(ctx, out, pp_int(len));
    }
    case B_POP: case B_SHIFT: {
        pp_obj *arr;
        if (n->n == 0) { arr = default_array(I); arr->refcnt++; }
        else arr = arg_container(I, n->list[0], 0);
        if (!arr) return pp_undef();
        if (arr->kind != PP_OARRAY) { pp_obj_decref(arr); return pp_die(I, "Not an ARRAY reference"); }
        pp_val v = n->op == B_POP ? pp_array_pop(arr) : pp_array_shift(arr);
        pp_obj_decref(arr);
        return ret(ctx, out, v);
    }
    case B_SPLICE: {
        if (n->n == 0) return pp_die(I, "Not enough arguments for splice");
        pp_list vals = {0};
        if (!eval_args(I, n, 1, &vals)) { pp_list_free(&vals); return pp_undef(); }
        pp_obj *arr = pp_get_array(I, n->list[0], 1, NULL);
        if (!arr) { pp_list_free(&vals); return pp_undef(); }
        int len = arr->u.arr.n;
        int off = vals.n > 0 ? (int)pp_to_int(vals.v[0]) : 0;
        if (off < 0) off += len;
        if (off > len) off = len;
        if (off < 0) off = 0;
        int cnt = vals.n > 1 ? (int)pp_to_int(vals.v[1]) : len - off;
        if (cnt < 0) cnt = len - off + cnt;
        if (cnt < 0) cnt = 0;
        if (off + cnt > len) cnt = len - off;
        int nins = vals.n > 2 ? vals.n - 2 : 0;
        pp_list removed = {0};
        for (int i = 0; i < cnt; i++) pp_list_push(&removed, arr->u.arr.v[off + i]);
        int newlen = len - cnt + nins;
        pp_array_reserve(arr, newlen);
        memmove(arr->u.arr.v + off + nins, arr->u.arr.v + off + cnt, (len - off - cnt) * sizeof(pp_val));
        for (int i = 0; i < nins; i++) arr->u.arr.v[off + i] = pp_copy(vals.v[2 + i]);
        arr->u.arr.n = newlen;
        pp_obj_decref(arr);
        pp_list_free(&vals);
        return ret_list(ctx, out, &removed);
    }
    case B_JOIN: {
        pp_list args = {0};
        if (!eval_args(I, n, 0, &args)) { pp_list_free(&args); return pp_undef(); }
        if (I->warn_on) pp_warn_uninit_args(I, n, &args, "join or string");
        pp_str *sep = args.n ? pp_to_str(args.v[0]) : pp_str_new("", 0);
        pp_str *r = pp_str_new(NULL, 0);
        for (int i = 1; i < args.n; i++) {
            if (i > 1) str_append(&r, sep->d, sep->len);
            pp_str *t; const char *p = pp_to_cstr(args.v[i], &t);
            str_append(&r, p, t ? t->len : args.v[i].u.s->len);
            if (t) pp_str_decref(t);
        }
        pp_str_decref(sep); pp_list_free(&args);
        return ret(ctx, out, pp_str_val(r));
    }
    case B_SPLIT: return do_split(I, n, ctx, out);
    case B_POS: {
        pp_node *v = n->n ? n->list[0] : NULL;
        if (v && !(v->type == N_VAR && v->sym->name[0] == '$')) return pp_die(I, "pos() needs a scalar variable");
        return ret(ctx, out, pp_pos_of(I, v ? pp_var_cell(I, v) : I->g_underscore));
    }
    case B_SPRINTF: {
        pp_list args = {0};
        if (!eval_args(I, n, 0, &args)) { pp_list_free(&args); return pp_undef(); }
        if (I->warn_on) pp_warn_uninit_args(I, n, &args, "sprintf");
        pp_val r = pp_sprintf(I, &args, 0);
        pp_list_free(&args);
        return ret(ctx, out, r);
    }
    case B_REVERSE: {
        pp_list args = {0};
        if (!eval_args(I, n, 0, &args)) { pp_list_free(&args); return pp_undef(); }
        if (ctx == CTX_LIST && out) {
            for (int i = args.n - 1; i >= 0; i--) pp_list_push(out, args.v[i]);
            args.n = 0; pp_list_free(&args);
            return pp_undef();
        }
        pp_str *r = pp_str_new(NULL, 0);
        if (args.n == 0) pp_list_push(&args, pp_copy(I->g_underscore->u.scalar));
        for (int i = 0; i < args.n; i++) { pp_str *t; const char *p = pp_to_cstr(args.v[i], &t); str_append(&r, p, t ? t->len : args.v[i].u.s->len); if (t) pp_str_decref(t); }
        for (int i = 0, j = r->len - 1; i < j; i++, j--) { char c = r->d[i]; r->d[i] = r->d[j]; r->d[j] = c; }
        pp_list_free(&args);
        return ret(ctx, out, pp_str_val(r));
    }
    case B_SORT: return do_sort(I, n, ctx, out);
    case B_MAP: return do_map_grep(I, n, ctx, out, 1);
    case B_GREP: return do_map_grep(I, n, ctx, out, 0);
    case B_ANY: return do_any_all(I, n, ctx, out, 0);
    case B_ALL: return do_any_all(I, n, ctx, out, 1);
    case B_DIE: case B_WARN: {
        pp_list args = {0};
        if (!eval_args(I, n, 0, &args)) { pp_list_free(&args); return pp_undef(); }
        pp_val msg;
        if (args.n == 1 && args.v[0].t == PP_REF && n->op == B_DIE) { msg = args.v[0]; args.n = 0; pp_list_free(&args); return pp_die_val(I, msg); }
        pp_str *r = pp_str_new(NULL, 0);
        for (int i = 0; i < args.n; i++) { pp_str *t; const char *p = pp_to_cstr(args.v[i], &t); str_append(&r, p, t ? t->len : args.v[i].u.s->len); if (t) pp_str_decref(t); }
        pp_list_free(&args);
        if (r->len == 0) { pp_str_decref(r); r = pp_str_new(n->op == B_DIE ? "Died" : "Warning: something's wrong", n->op == B_DIE ? 4 : 26); }
        msg = pp_str_val(r);
        if (n->op == B_DIE) return pp_die_val(I, msg);
        if (r->d[r->len - 1] != '\n') { char tail[160]; int tn = pp_snprintf(tail, sizeof tail, " at %s line %d.\n", I->file ? I->file : "-", I->line); str_append(&r, tail, tn); }
        pp_emit_warning(I, r->d, r->len);
        pp_release(msg);
        return ret(ctx, out, pp_int(1));
    }
    case B_SUBSTR: {
        pp_list args = {0};
        if (n->n < 2) return pp_die(I, "Not enough arguments for substr");
        for (int i = 1; i < n->n; i++) { pp_eval(I, n->list[i], CTX_LIST, &args); if (I->ctl) { pp_list_free(&args); return pp_undef(); } }
        pp_val *slot = NULL; pp_val sv;
        if (n->n >= 4) { slot = pp_lvalue(I, n->list[0]); if (!slot) { pp_list_free(&args); return pp_undef(); } sv = pp_copy(*slot); }
        else { sv = pp_eval(I, n->list[0], CTX_SCALAR, NULL); if (I->ctl) { pp_list_free(&args); return pp_undef(); } }
        pp_str *s = pp_to_str(sv); pp_release(sv);
        int len = s->len;
        int64_t off = pp_to_int(args.v[0]);
        int64_t before = 0;                      /* how far a negative offset reaches before the string */
        if (off < 0) off += len;
        if (off < 0) { before = -off; off = 0; }
        if (off > len) {                         /* starts past the end: undef, with a warning */
            if (I->warn_on) pp_warn(I, "substr outside of string");
            pp_str_decref(s); pp_list_free(&args); return ret(ctx, out, pp_undef());
        }
        int64_t cnt = args.n > 1 && args.v[1].t != PP_UNDEF ? pp_to_int(args.v[1]) : len - off;
        if (cnt < 0) cnt = len - off + cnt;
        else if (before && args.n > 1 && args.v[1].t != PP_UNDEF) {
            cnt -= before;                       /* the part before the string is not there */
            if (cnt < 0) {                       /* ... and nothing else was asked for */
                if (I->warn_on) pp_warn(I, "substr outside of string");
                pp_str_decref(s); pp_list_free(&args); return ret(ctx, out, pp_undef());
            }
        }
        if (cnt < 0) cnt = 0;
        if (off + cnt > len) cnt = len - off;
        pp_val r = pp_strn(s->d + off, (int)cnt);
        if (slot && args.n > 2) {
            pp_str *rep = pp_to_str(args.v[2]);
            pp_str *ns = pp_str_new(NULL, 0);
            str_append(&ns, s->d, (int)off);
            str_append(&ns, rep->d, rep->len);
            str_append(&ns, s->d + off + cnt, (int)(len - off - cnt));
            pp_str_decref(rep);
            slot = pp_lvalue(I, n->list[0]);
            if (slot) { pp_release(*slot); *slot = pp_str_val(ns); } else pp_str_decref(ns);
        }
        pp_str_decref(s); pp_list_free(&args);
        return ret(ctx, out, r);
    }
    case B_INDEX: case B_RINDEX: {
        pp_list args = {0};
        if (!eval_args(I, n, 0, &args)) { pp_list_free(&args); return pp_undef(); }
        if (args.n < 2) { pp_list_free(&args); return pp_die(I, "Not enough arguments for index"); }
        pp_str *s = pp_to_str(args.v[0]), *sub = pp_to_str(args.v[1]);
        int64_t r = -1;
        if (n->op == B_INDEX) {
            int64_t pos = args.n > 2 ? pp_to_int(args.v[2]) : 0;
            if (pos < 0) pos = 0;
            for (int64_t i = pos; i + sub->len <= s->len; i++) if (memcmp(s->d + i, sub->d, sub->len) == 0) { r = i; break; }
            if (r < 0 && sub->len == 0) r = pos > s->len ? s->len : pos;
        } else {
            int64_t pos = args.n > 2 ? pp_to_int(args.v[2]) : s->len;
            if (pos > s->len - sub->len) pos = s->len - sub->len;
            for (int64_t i = pos; i >= 0; i--) if (memcmp(s->d + i, sub->d, sub->len) == 0) { r = i; break; }
        }
        pp_str_decref(s); pp_str_decref(sub); pp_list_free(&args);
        return ret(ctx, out, pp_int(r));
    }
    case B_ATAN2: {
        pp_list args = {0};
        if (!eval_args(I, n, 0, &args)) { pp_list_free(&args); return pp_undef(); }
        double y = args.n > 0 ? pp_to_num(args.v[0]) : 0, x = args.n > 1 ? pp_to_num(args.v[1]) : 0;
        pp_list_free(&args);
        return ret(ctx, out, pp_num(atan2(y, x)));
    }
    case B_LENGTH: {
        pp_val v = unary_arg(I, n);
        if (I->ctl) return pp_undef();
        if (v.t == PP_UNDEF) return ret(ctx, out, pp_undef());
        pp_str *s = pp_to_str(v); pp_release(v);
        int len = s->len; pp_str_decref(s);
        return ret(ctx, out, pp_int(len));
    }
    case B_LC: case B_UC: case B_LCFIRST: case B_UCFIRST: case B_QUOTEMETA: {
        pp_val v = unary_arg(I, n);
        if (I->ctl) return pp_undef();
        pp_str *s = pp_to_str(v); pp_release(v);
        if (n->op == B_QUOTEMETA) {
            pp_str *r = pp_str_new(NULL, 0);
            for (int i = 0; i < s->len; i++) { unsigned char c = s->d[i]; if (!(isalnum(c) || c == '_' || c >= 128)) str_append(&r, "\\", 1); str_append(&r, (char *)&c, 1); }
            pp_str_decref(s);
            return ret(ctx, out, pp_str_val(r));
        }
        pp_str *r = pp_str_new(s->d, s->len);
        pp_str_decref(s);
        if (n->op == B_LC) for (int i = 0; i < r->len; i++) r->d[i] = (char)tolower((unsigned char)r->d[i]);
        else if (n->op == B_UC) for (int i = 0; i < r->len; i++) r->d[i] = (char)toupper((unsigned char)r->d[i]);
        else if (r->len) r->d[0] = (char)(n->op == B_LCFIRST ? tolower((unsigned char)r->d[0]) : toupper((unsigned char)r->d[0]));
        return ret(ctx, out, pp_str_val(r));
    }
    case B_CHR: {
        pp_val v = unary_arg(I, n);
        if (I->ctl) return pp_undef();
        int64_t c = pp_to_int(v); pp_release(v);
        char tmp[8]; int tn = 0;
        if (c < 0) c = 0xFFFD;
        if (c < 0x100) tmp[tn++] = (char)c;   /* bytes semantics, like Perl without utf8 */
        else if (c < 0x800) { tmp[tn++] = (char)(0xC0 | (c >> 6)); tmp[tn++] = (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { tmp[tn++] = (char)(0xE0 | (c >> 12)); tmp[tn++] = (char)(0x80 | ((c >> 6) & 0x3F)); tmp[tn++] = (char)(0x80 | (c & 0x3F)); }
        else { tmp[tn++] = (char)(0xF0 | (c >> 18)); tmp[tn++] = (char)(0x80 | ((c >> 12) & 0x3F)); tmp[tn++] = (char)(0x80 | ((c >> 6) & 0x3F)); tmp[tn++] = (char)(0x80 | (c & 0x3F)); }
        return ret(ctx, out, pp_strn(tmp, tn));
    }
    case B_ORD: {
        pp_val v = unary_arg(I, n);
        if (I->ctl) return pp_undef();
        pp_str *s = pp_to_str(v); pp_release(v);
        int c = s->len ? (unsigned char)s->d[0] : 0;
        pp_str_decref(s);
        return ret(ctx, out, pp_int(c));
    }
    case B_INT: case B_ABS: case B_SQRT: case B_SIN: case B_COS: case B_EXP: case B_LOG: {
        pp_val v = unary_arg(I, n);
        if (I->ctl) return pp_undef();
        pp_val num = pp_numify(v); pp_release(v);
        pp_val r;
        switch (n->op) {
        case B_INT:
            if (num.t == PP_INT) r = num;
            else if (isnan(num.u.n) || isinf(num.u.n)) r = num;
            else { double t = trunc(num.u.n); r = fabs(t) < 9.2e18 ? pp_int((int64_t)t) : pp_num(t); }
            break;
        case B_ABS:
            if (num.t == PP_INT) r = num.u.i == INT64_MIN ? pp_num(-(double)num.u.i) : pp_int(num.u.i < 0 ? -num.u.i : num.u.i);
            else r = pp_num(fabs(num.u.n));
            break;
        case B_SQRT: { double d = pp_to_num(num); if (d < 0) return pp_die(I, "Can't take sqrt of %g", d); r = pp_num(sqrt(d)); break; }
        case B_SIN: r = pp_num(sin(pp_to_num(num))); break;
        case B_COS: r = pp_num(cos(pp_to_num(num))); break;
        case B_EXP: r = pp_num(exp(pp_to_num(num))); break;
        default: { double d = pp_to_num(num); if (d <= 0) return pp_die(I, "Can't take log of %g", d); r = pp_num(log(d)); break; }
        }
        return ret(ctx, out, r);
    }
    case B_HEX: case B_OCT: {
        pp_val v = unary_arg(I, n);
        if (I->ctl) return pp_undef();
        pp_str *s = pp_to_str(v); pp_release(v);
        const char *p = s->d; int len = s->len; int i = 0;
        while (i < len && isspace((unsigned char)p[i])) i++;
        int base = n->op == B_HEX ? 16 : 8;
        if (n->op == B_OCT) {
            if (i + 1 < len && p[i] == '0' && (p[i+1] == 'x' || p[i+1] == 'X')) { base = 16; i += 2; }
            else if (i + 1 < len && p[i] == '0' && (p[i+1] == 'b' || p[i+1] == 'B')) { base = 2; i += 2; }
            else if (i + 1 < len && p[i] == '0' && (p[i+1] == 'o' || p[i+1] == 'O')) { base = 8; i += 2; }
            else if (i < len && (p[i] == 'x' || p[i] == 'X')) { base = 16; i++; }
            else if (i < len && (p[i] == 'b' || p[i] == 'B')) { base = 2; i++; }
        } else if (i + 1 < len && p[i] == '0' && (p[i+1] == 'x' || p[i+1] == 'X')) i += 2;
        else if (i < len && (p[i] == 'x' || p[i] == 'X')) i++;
        uint64_t r = 0;
        for (; i < len; i++) {
            int c = (unsigned char)p[i]; int d;
            if (c == '_') continue;
            if (isdigit(c)) d = c - '0'; else if (isalpha(c)) d = tolower(c) - 'a' + 10; else break;
            if (d >= base) break;
            r = r * base + d;
        }
        pp_str_decref(s);
        return ret(ctx, out, pp_int((int64_t)r));
    }
    case B_DEFINED: {
        if (n->n == 0) return ret(ctx, out, pp_int(I->g_underscore->u.scalar.t != PP_UNDEF ? 1 : 0));
        pp_node *a = n->list[0];
        int d;
        if (a->type == N_CALL && a->n == 0) d = pp_resolve_sub(I, a->sym) != NULL;
        else if (a->type == N_CALLREF && (a->flags & 2)) {   /* defined &$code / defined &{"name"}: no call */
            pp_val cv = pp_eval(I, a->a, CTX_SCALAR, NULL);
            if (I->ctl) { pp_release(cv); return pp_undef(); }
            if (cv.t == PP_REF) d = cv.u.o->kind == PP_OCODE;
            else if (cv.t == PP_UNDEF) d = 0;
            else { pp_sym *s = pp_symref(I, a, cv, '&', 0); d = s && pp_resolve_sub(I, s) != NULL; }
            pp_release(cv);
        }
        else if (a->type == N_VAR && a->sym->name[0] == '@') { pp_obj *c = pp_get_array(I, a, 0, NULL); d = c && c->u.arr.n > 0; if (c) pp_obj_decref(c); }
        else if (a->type == N_VAR && a->sym->name[0] == '%') { pp_obj *c = pp_get_hash(I, a, 0, NULL); d = c && c->u.hash.live > 0; if (c) pp_obj_decref(c); }
        else {
            pp_val v = pp_eval(I, a, CTX_SCALAR, NULL);
            if (I->ctl) return pp_undef();
            d = v.t != PP_UNDEF; pp_release(v);
        }
        if (I->ctl) return pp_undef();
        return ret(ctx, out, d ? pp_int(1) : pp_strn("", 0));
    }
    case B_REF: {
        pp_val v = unary_arg(I, n);
        if (I->ctl) return pp_undef();
        const char *r = "";
        if (v.t == PP_REF && v.u.o->class) r = v.u.o->class->name;
        else if (v.t == PP_REF) {
            switch (v.u.o->kind) {
            case PP_OARRAY: r = "ARRAY"; break;
            case PP_OHASH: r = "HASH"; break;
            case PP_OCODE: r = "CODE"; break;
            case PP_OREGEX: r = "Regexp"; break;
            case PP_OFILE: case PP_OGLOB: r = "GLOB"; break;
            default: r = v.u.o->u.scalar.t == PP_REF ? "REF" : "SCALAR"; break;
            }
        }
        pp_release(v);
        return ret(ctx, out, pp_cstr(r));
    }
    case B_SCALAR: {
        if (n->n == 0) return pp_die(I, "Not enough arguments for scalar");
        pp_val v = pp_eval(I, n->list[0], CTX_SCALAR, NULL);
        if (I->ctl) return pp_undef();
        return ret(ctx, out, v);
    }
    case B_EXISTS: {
        if (n->n == 0) return pp_die(I, "Not enough arguments for exists");
        pp_node *a = n->list[0];
        int e = 0;
        if (a->type == N_HELEM) {
            pp_val k = pp_eval(I, a->b, CTX_SCALAR, NULL);
            if (I->ctl) return pp_undef();
            pp_str *ks = pp_to_str(k); pp_release(k);
            pp_obj *h = pp_get_hash(I, a->a, 0, NULL);
            if (h) { e = pp_hash_find(h, ks->d, ks->len) != NULL; pp_obj_decref(h); }
            pp_str_decref(ks);
        } else if (a->type == N_AELEM) {
            pp_val k = pp_eval(I, a->b, CTX_SCALAR, NULL);
            if (I->ctl) return pp_undef();
            int64_t idx = pp_to_int(k); pp_release(k);
            pp_obj *arr = pp_get_array(I, a->a, 0, NULL);
            if (arr) { if (idx < 0) idx += arr->u.arr.n; e = idx >= 0 && idx < arr->u.arr.n; pp_obj_decref(arr); }
        } else if (a->type == N_CALL) {
            e = pp_resolve_sub(I, a->sym) != NULL;
        } else return pp_die(I, "exists argument is not a HASH or ARRAY element or a subroutine");
        if (I->ctl) return pp_undef();
        return ret(ctx, out, e ? pp_int(1) : pp_strn("", 0));
    }
    case B_DELETE: {
        if (n->n == 0) return pp_die(I, "Not enough arguments for delete");
        pp_node *a = n->list[0];
        if (a->type == N_HELEM) {
            pp_val k = pp_eval(I, a->b, CTX_SCALAR, NULL);
            if (I->ctl) return pp_undef();
            pp_str *ks = pp_to_str(k); pp_release(k);
            pp_obj *h = pp_get_hash(I, a->a, 0, NULL);
            pp_val r = pp_undef();
            if (h) { pp_hash_delete(h, ks->d, ks->len, &r); pp_obj_decref(h); }
            pp_str_decref(ks);
            if (I->ctl) { pp_release(r); return pp_undef(); }
            return ret(ctx, out, r);
        }
        if (a->type == N_HSLICE) {
            pp_list idx = {0}, res = {0};
            pp_eval(I, a->b, CTX_LIST, &idx);
            if (I->ctl) { pp_list_free(&idx); return pp_undef(); }
            pp_obj *h = pp_get_hash(I, a->a, 0, NULL);
            if (h) { for (int i = 0; i < idx.n; i++) { pp_str *ks = pp_to_str(idx.v[i]); pp_val r; pp_hash_delete(h, ks->d, ks->len, &r); pp_list_push(&res, r); pp_str_decref(ks); } pp_obj_decref(h); }
            pp_list_free(&idx);
            return ret_list(ctx, out, &res);
        }
        if (a->type == N_AELEM) {
            pp_val k = pp_eval(I, a->b, CTX_SCALAR, NULL);
            if (I->ctl) return pp_undef();
            int64_t idx = pp_to_int(k); pp_release(k);
            pp_obj *arr = pp_get_array(I, a->a, 0, NULL);
            pp_val r = pp_undef();
            if (arr) {
                if (idx < 0) idx += arr->u.arr.n;
                if (idx >= 0 && idx < arr->u.arr.n) {
                    r = arr->u.arr.v[idx]; arr->u.arr.v[idx] = pp_undef();
                    if (idx == arr->u.arr.n - 1) arr->u.arr.n--;
                }
                pp_obj_decref(arr);
            }
            return ret(ctx, out, r);
        }
        return pp_die(I, "delete argument is not a HASH or ARRAY element or slice");
    }
    case B_KEYS: case B_VALUES: {
        if (n->n == 0) return pp_die(I, "Not enough arguments for keys");
        pp_obj *c = arg_container(I, n->list[0], 1);
        if (!c) return pp_undef();
        pp_list res = {0};
        if (c->kind == PP_OHASH) {
            c->u.hash.it = 0;               /* like perl, keys/values reset the each() iterator */
            if (ctx != CTX_LIST || !out) { int cnt = c->u.hash.live; pp_obj_decref(c); return pp_int(cnt); }
            for (int i = 0; i < c->u.hash.n; i++) if (c->u.hash.e[i].klen >= 0) {
                if (n->op == B_KEYS) pp_list_push(&res, pp_strn(c->u.hash.e[i].key, c->u.hash.e[i].klen));
                else pp_list_push(&res, pp_copy(c->u.hash.e[i].v));
            }
        } else {
            if (ctx != CTX_LIST || !out) { int cnt = c->u.arr.n; pp_obj_decref(c); return pp_int(cnt); }
            for (int i = 0; i < c->u.arr.n; i++) pp_list_push(&res, n->op == B_KEYS ? pp_int(i) : pp_copy(c->u.arr.v[i]));
        }
        pp_obj_decref(c);
        return ret_list(ctx, out, &res);
    }
    case B_EACH: {
        if (n->n == 0) return pp_die(I, "Not enough arguments for each");
        pp_obj *c = arg_container(I, n->list[0], 1);
        if (!c) return pp_undef();
        if (c->kind != PP_OHASH) { pp_obj_decref(c); return pp_die(I, "each is only supported on hashes"); }
        int i = c->u.hash.it;
        while (i < c->u.hash.n && c->u.hash.e[i].klen < 0) i++;
        if (i >= c->u.hash.n) {             /* exhausted: reset and return the empty list / undef */
            c->u.hash.it = 0;
            pp_obj_decref(c);
            if (ctx == CTX_LIST && out) return pp_undef();
            return pp_undef();
        }
        c->u.hash.it = i + 1;
        if (ctx == CTX_LIST && out) {
            pp_list_push(out, pp_strn(c->u.hash.e[i].key, c->u.hash.e[i].klen));
            pp_list_push(out, pp_copy(c->u.hash.e[i].v));
            pp_obj_decref(c);
            return pp_undef();
        }
        pp_val k = pp_strn(c->u.hash.e[i].key, c->u.hash.e[i].klen);
        pp_obj_decref(c);
        return ret(ctx, out, k);
    }
    case B_EXIT: {
        int64_t code = 0;
        if (n->n) { pp_val v = pp_eval(I, n->list[0], CTX_SCALAR, NULL); if (I->ctl) return pp_undef(); code = pp_to_int(v); pp_release(v); }
        I->exit_code = (int)code;
        I->ctl = CTL_EXIT;
        return pp_undef();
    }
    case B_UNDEF: {
        if (n->n == 0) return ret(ctx, out, pp_undef());
        pp_node *a = n->list[0];
        if (a->type == N_VAR && a->sym->name[0] == '@') { pp_obj *c = pp_get_array(I, a, 0, NULL); if (c) { pp_array_clear(c); pp_obj_decref(c); } }
        else if (a->type == N_VAR && a->sym->name[0] == '%') { pp_obj *c = pp_get_hash(I, a, 0, NULL); if (c) { pp_hash_clear(c); pp_obj_decref(c); } }
        else if (a->type == N_CALL) { /* undef &foo: ignore */ }
        else { pp_val *slot = pp_lvalue(I, a); if (slot) { pp_release(*slot); *slot = pp_undef(); } }
        return ret(ctx, out, pp_undef());
    }
    case B_CHOMP: case B_CHOP: {
        int count = 0;
        pp_node *targets[1]; pp_node **tl; int nt;
        if (n->n == 0) { targets[0] = NULL; tl = targets; nt = 1; }
        else if (n->list[0]->type == N_LIST) { tl = n->list[0]->list; nt = n->list[0]->n; }
        else { targets[0] = n->list[0]; tl = targets; nt = 1; }
        for (int t = 0; t < nt; t++) {
            pp_node *a = tl[t];
            pp_val *slots[1]; pp_val **sl = slots; int ns = 1;
            pp_obj *arr = NULL;
            if (a && ((a->type == N_VAR && a->sym->name[0] == '@') || a->type == N_RV2AV)) {
                arr = pp_get_array(I, a, 0, NULL);
                if (!arr) return pp_undef();
                for (int i = 0; i < arr->u.arr.n; i++) {
                    pp_val *s = &arr->u.arr.v[i];
                    if (s->t == PP_STR && s->u.s->len > 0 && (n->op == B_CHOP || s->u.s->d[s->u.s->len - 1] == '\n')) {
                        pp_str *ns2 = pp_str_new(s->u.s->d, s->u.s->len - 1); pp_release(*s); *s = pp_str_val(ns2); count++;
                    }
                }
                pp_obj_decref(arr);
                continue;
            }
            if (!a) { slots[0] = &I->g_underscore->u.scalar; }
            else { slots[0] = pp_lvalue(I, a); if (!slots[0]) return pp_undef(); }
            for (int i = 0; i < ns; i++) {
                pp_val *s = sl[i];
                if (s->t != PP_STR) { pp_str *tmp = pp_to_str(*s); pp_release(*s); *s = pp_str_val(tmp); }
                if (s->u.s->len > 0 && (n->op == B_CHOP || s->u.s->d[s->u.s->len - 1] == '\n')) {
                    pp_str *ns2 = pp_str_new(s->u.s->d, s->u.s->len - 1); pp_release(*s); *s = pp_str_val(ns2); count++;
                }
            }
        }
        return ret(ctx, out, pp_int(count));
    }
    case B_RAND: {
        double max = 1;
        if (n->n) { pp_val v = pp_eval(I, n->list[0], CTX_SCALAR, NULL); if (I->ctl) return pp_undef(); max = pp_to_num(v); pp_release(v); if (max == 0) max = 1; }
        return ret(ctx, out, pp_num(max * ((double)rand() / ((double)RAND_MAX + 1.0))));
    }
    case B_SRAND: {
        int64_t seed = (int64_t)pp_host_time();
        if (n->n) { pp_val v = pp_eval(I, n->list[0], CTX_SCALAR, NULL); if (I->ctl) return pp_undef(); seed = pp_to_int(v); pp_release(v); }
        srand((unsigned)seed);
        return ret(ctx, out, pp_int(seed));
    }
    case B_TIME: return ret(ctx, out, pp_int(pp_host_time()));
    case B_SLEEP: {
        pp_val v = unary_arg(I, n);
        if (I->ctl) return pp_undef();
        double s = pp_to_num(v); pp_release(v);
        pp_host_sleep_ms((uint32_t)(s * 1000));
        return ret(ctx, out, pp_int((int64_t)s));
    }
    case B_ALARM: {
        pp_val v = unary_arg(I, n);
        if (I->ctl) { pp_release(v); return pp_undef(); }
        int64_t secs = pp_to_int(v); pp_release(v);
        uint32_t now = pp_host_millis();
        int64_t prev = 0;
        if (I->alarm_at) {
            int32_t d = (int32_t)(I->alarm_at - now);
            prev = d > 0 ? (d + 999) / 1000 : 0;   /* remaining, rounded up like perl */
        }
        if (secs > 0) {
            I->alarm_at = now + (uint32_t)secs * 1000u;
            if (!I->alarm_at) I->alarm_at = 1;     /* 0 means "no alarm armed" */
        } else I->alarm_at = 0;
        return ret(ctx, out, pp_int(prev));
    }
    case B_READLINE: {
        check_irs(I);
        if (n->n == 0) {   /* <STDIN> */
            char *line = pp_host_read_line();
            if (!line) return ret(ctx, out, pp_undef());
            pp_val r = pp_cstr(line);
            free(line);
            return ret(ctx, out, r);
        }
        pp_val hv = pp_eval(I, n->list[0], CTX_SCALAR, NULL);
        if (I->ctl) { pp_release(hv); return pp_undef(); }
        int hfd;
        pp_obj *f = handle_of(I, hv, &hfd);
        if (!f && hfd == 0) {              /* <STDIN> through a glob: readline(*STDIN), <$in> */
            pp_release(hv);
            if (ctx == CTX_LIST && out) {
                for (;;) { char *line = pp_host_read_line(); if (!line) break; pp_list_push(out, pp_cstr(line)); free(line); }
                return pp_undef();
            }
            char *line = pp_host_read_line();
            if (!line) return ret(ctx, out, pp_undef());
            pp_val r = pp_cstr(line);
            free(line);
            return ret(ctx, out, r);
        }
        if (!f) { pp_release(hv); return pp_die(I, "readline: not a filehandle (expected a handle from open)"); }
        if (ctx == CTX_LIST && out) {
            for (;;) { pp_val l = read_fline(f); if (l.t == PP_UNDEF) break; pp_list_push(out, l); }
            pp_release(hv);
            return pp_undef();
        }
        pp_val r = read_fline(f);
        pp_release(hv);
        return ret(ctx, out, r);
    }
    case B_CLOSE: {
        pp_val hv = unary_arg(I, n);
        if (I->ctl) { pp_release(hv); return pp_undef(); }
        int hfd;
        pp_obj *f = handle_of(I, hv, &hfd);
        if (!f && hfd >= 0) { pp_release(hv); return ret(ctx, out, pp_int(1)); }   /* close(STDOUT): stays open here */
        if (!f) { pp_release(hv); return pp_die(I, "close: not a filehandle (expected a handle from open)"); }
        int r = f->u.file.fp ? pp_host_fclose(f->u.file.fp) == 0 : 0;
        f->u.file.fp = NULL;
        pp_release(hv);
        return ret(ctx, out, r ? pp_int(1) : pp_strn("", 0));
    }
    case B_EOF: {
        pp_val hv = unary_arg(I, n);
        if (I->ctl) { pp_release(hv); return pp_undef(); }
        pp_obj *f = file_of(I, hv, "eof");
        if (!f) { pp_release(hv); return pp_undef(); }
        int e = f->u.file.fp ? pp_host_feof(f->u.file.fp) : 1;
        pp_release(hv);
        return ret(ctx, out, e ? pp_int(1) : pp_strn("", 0));
    }
    case B_TELL: {
        pp_val hv = unary_arg(I, n);
        if (I->ctl) { pp_release(hv); return pp_undef(); }
        pp_obj *f = file_of(I, hv, "tell");
        if (!f) { pp_release(hv); return pp_undef(); }
        long pos = f->u.file.fp ? pp_host_ftell(f->u.file.fp) : -1;
        pp_release(hv);
        return ret(ctx, out, pp_int(pos));
    }
    case B_SEEK: {
        pp_list args = {0};
        if (!eval_args(I, n, 0, &args)) { pp_list_free(&args); return pp_undef(); }
        if (args.n < 3) { pp_list_free(&args); return pp_die(I, "Not enough arguments for seek"); }
        pp_obj *f = file_of(I, args.v[0], "seek");
        if (!f) { pp_list_free(&args); return pp_undef(); }
        long off = (long)pp_to_int(args.v[1]);
        int whence = (int)pp_to_int(args.v[2]);
        long r = f->u.file.fp ? pp_host_fseek(f->u.file.fp, off, whence) : -1;
        pp_list_free(&args);
        return ret(ctx, out, r >= 0 ? pp_int(1) : pp_strn("", 0));
    }
    case B_OPEN: return do_open(I, n, ctx, out);
    case B_BLESS: {
        pp_list args = {0};
        if (!eval_args(I, n, 0, &args)) { pp_list_free(&args); return pp_undef(); }
        if (args.n < 1 || args.v[0].t != PP_REF) { pp_list_free(&args); return pp_die(I, "Can't bless non-reference value"); }
        pp_str *cls = args.n > 1 ? pp_to_str(args.v[1]) : pp_str_new("main", 4);
        args.v[0].u.o->class = pp_intern(I, cls->d, cls->len);
        pp_str_decref(cls);
        pp_val r = args.v[0]; args.v[0] = pp_undef();
        pp_list_free(&args);
        return ret(ctx, out, r);
    }
    case B_BINMODE: case B_SELECT: {
        pp_list args = {0};
        eval_args(I, n, 0, &args);
        if (n->op == B_BINMODE && args.n > 1 && !(I->compat_warn & 16)) {
            I->compat_warn |= 16;
            pp_warn(I, "picoperl: IO layers are ignored, files are bytes");
        }
        pp_list_free(&args);
        return ret(ctx, out, pp_int(1));
    }
    default: break;
    }
    return pp_die(I, "internal: unknown builtin %d", n->op);
}

/* ------------------------------------------------------------------ */
/* builtin:: core functions (perl 5.36+); the exportable wrappers and  */
/* the pure-perl ones (true/false/trim) live in lib/builtin.pm         */
/* ------------------------------------------------------------------ */

static pp_val n_bi_ceil(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    double d = a->n ? pp_to_num(a->v[0]) : 0;
    return pp_num(ceil(d));
}
static pp_val n_bi_floor(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    double d = a->n ? pp_to_num(a->v[0]) : 0;
    return pp_num(floor(d));
}
static pp_val n_bi_blessed(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    if (a->n && a->v[0].t == PP_REF && a->v[0].u.o->class) return pp_cstr(a->v[0].u.o->class->name);
    return pp_undef();
}
static pp_val n_bi_reftype(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    if (!a->n || a->v[0].t != PP_REF) return pp_undef();
    switch (a->v[0].u.o->kind) {
    case PP_OARRAY: return pp_cstr("ARRAY");
    case PP_OHASH:  return pp_cstr("HASH");
    case PP_OCODE:  return pp_cstr("CODE");
    case PP_OREGEX: return pp_cstr("REGEXP");
    case PP_OFILE: case PP_OGLOB: return pp_cstr("GLOB");
    default:        return pp_cstr(a->v[0].u.o->u.scalar.t == PP_REF ? "REF" : "SCALAR");
    }
}
static pp_val n_bi_refaddr(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    if (!a->n || a->v[0].t != PP_REF) return pp_undef();
    return pp_int((int64_t)(uintptr_t)a->v[0].u.o);
}

static pp_val n_utf8_identity(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; return a->n ? pp_copy(a->v[0]) : pp_undef(); }
static pp_val n_utf8_true(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return pp_int(1); }
static pp_val n_utf8_false(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return pp_strn("", 0); }

/* Scalar::Util::looks_like_number($x): would perl treat the value as a number without a warning? */
static pp_val n_looks_like_number(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    if (a->n < 1 || a->v[0].t == PP_UNDEF) return pp_strn("", 0);
    if (a->v[0].t == PP_INT || a->v[0].t == PP_NUM) return pp_int(1);
    if (a->v[0].t != PP_STR) return pp_strn("", 0);
    return pp_looks_like_number(a->v[0].u.s->d, a->v[0].u.s->len) ? pp_int(1) : pp_strn("", 0);
}

/* fileno(HANDLE): 0 / 1 / 2 for the standard handles, 3 for any other open file, undef otherwise */
static pp_val n_fileno(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    if (a->n < 1) return pp_undef();
    int fd;
    pp_obj *f = handle_of(I, a->v[0], &fd);
    if (f) return f->u.file.fp ? pp_int(3) : pp_undef();
    return fd >= 0 ? pp_int(fd) : pp_undef();
}
void pp_core_natives_register(pp_interp *I) {
    pp_register(I, "fileno", n_fileno);
    pp_register(I, "builtin::ceil", n_bi_ceil);
    pp_register(I, "builtin::floor", n_bi_floor);
    pp_register(I, "builtin::blessed", n_bi_blessed);
    pp_register(I, "builtin::reftype", n_bi_reftype);
    pp_register(I, "builtin::refaddr", n_bi_refaddr);
    pp_register(I, "Scalar::Util::looks_like_number", n_looks_like_number);
    /* utf8:: functions that need no `use utf8`. Strings are bytes here, so the code point
       conversions are the identity and the encoding ones leave the string alone. */
    pp_register(I, "utf8::unicode_to_native", n_utf8_identity);
    pp_register(I, "utf8::native_to_unicode", n_utf8_identity);
    pp_register(I, "utf8::is_utf8", n_utf8_false);
    pp_register(I, "utf8::valid", n_utf8_true);
    pp_register(I, "utf8::upgrade", n_utf8_true);
    pp_register(I, "utf8::downgrade", n_utf8_true);
    pp_register(I, "utf8::encode", n_utf8_true);
    pp_register(I, "utf8::decode", n_utf8_true);
}
