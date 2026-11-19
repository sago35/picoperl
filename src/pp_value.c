#include "pp_internal.h"
#include "pp_regex.h"
#include <math.h>
#include <ctype.h>

long pp_stat_nodes, pp_stat_strs, pp_stat_objs;
void *pp_xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) { pp_host_fatal("picoperl: out of memory\n"); abort(); }
    return p;
}
void *pp_xrealloc(void *p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (!p) { pp_host_fatal("picoperl: out of memory\n"); abort(); }
    return p;
}

/* ---------------- arena ---------------- */

#define ARENA_CHUNK 2048

pp_arena *pp_arena_new(void) {
    pp_arena *a = pp_xmalloc(sizeof *a);
    a->chunks = NULL; a->nnodes = 0;
    return a;
}
void *pp_arena_alloc(pp_arena *a, size_t n) {
    n = (n + 7) & ~(size_t)7;
    pp_arena_chunk *c = a->chunks;
    if (!c || c->used + n > c->cap) {
        size_t cap = n > ARENA_CHUNK ? n : ARENA_CHUNK;
        c = pp_xmalloc(sizeof *c + cap);
        c->next = a->chunks; c->used = 0; c->cap = cap;
        a->chunks = c;
    }
    void *p = (char *)(c + 1) + c->used;
    c->used += n;
    return p;
}
void pp_arena_free(pp_arena *a) {
    if (!a) return;
    pp_stat_nodes -= a->nnodes;
    for (pp_arena_chunk *c = a->chunks; c; ) { pp_arena_chunk *nx = c->next; free(c); c = nx; }
    free(a);
}

int pp_snprintf(char *buf, size_t cap, const char *fmt, ...) {
    if (!cap) return 0;
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    if (n < 0) { buf[0] = 0; return 0; }
    return (size_t)n >= cap ? (int)cap - 1 : n;
}

/* The false value of comparisons, matches and `!`: an empty string that, unlike a written "",
   is also the number 0 without an "isn't numeric" warning (perl's false is such a value).
   One shared string that is never freed; copies are recognized by its address. */
static pp_str pp_false_str = { 1 << 28, 0, 0, { 0 } };
pp_val pp_false(void) {
    pp_false_str.refcnt++;
    pp_val v; v.t = PP_STR; v.u.s = &pp_false_str;
    return v;
}
int pp_is_false(pp_val v) { return v.t == PP_STR && v.u.s == &pp_false_str; }

void pp_arena_absorb(pp_arena *dst, pp_arena *src) {
    if (src->chunks) {
        pp_arena_chunk *tail = src->chunks;
        while (tail->next) tail = tail->next;
        if (dst->chunks) { tail->next = dst->chunks->next; dst->chunks->next = src->chunks; }   /* dst keeps filling its own head chunk */
        else dst->chunks = src->chunks;
    }
    dst->nnodes += src->nnodes;
    free(src);
}

/* ---------------- strings ---------------- */

pp_str *pp_str_new(const char *s, int len) {
    int cap = len < 15 ? 15 : len;
    pp_str *r = pp_xmalloc(sizeof(pp_str) + cap);
    pp_stat_strs++;
    r->refcnt = 1; r->len = len; r->cap = cap;
    if (s && len) memcpy(r->d, s, len);
    r->d[len] = 0;
    return r;
}
void pp_str_decref(pp_str *s) {
    if (s && --s->refcnt == 0) { free(s); pp_stat_strs--; }
}
pp_str *pp_str_reserve(pp_str *s, int extra) {
    if (s->refcnt > 1) {
        pp_str *n = pp_str_new(s->d, s->len);
        pp_str_decref(s);
        s = n;
    }
    if (s->len + extra > s->cap) {
        int cap = s->cap * 2;
        if (cap < s->len + extra) cap = s->len + extra;
        s = pp_xrealloc(s, sizeof(pp_str) + cap);
        s->cap = cap;
    }
    return s;
}
pp_val pp_str_val(pp_str *s) { pp_val v; v.t = PP_STR; v.u.s = s; return v; }

/* ---------------- values ---------------- */

pp_val pp_undef(void) { pp_val v; v.t = PP_UNDEF; v.u.i = 0; return v; }
pp_val pp_int(int64_t i) { pp_val v; v.t = PP_INT; v.u.i = i; return v; }
pp_val pp_num(double n) { pp_val v; v.t = PP_NUM; v.u.n = n; return v; }
pp_val pp_strn(const char *s, int len) { return pp_str_val(pp_str_new(s, len)); }
pp_val pp_cstr(const char *s) { return pp_strn(s, (int)strlen(s)); }
pp_val pp_copy(pp_val v) {
    if (v.t == PP_STR) v.u.s->refcnt++;
    else if (v.t == PP_REF) v.u.o->refcnt++;
    return v;
}
void pp_release(pp_val v) {
    if (v.t == PP_STR) pp_str_decref(v.u.s);
    else if (v.t == PP_REF) pp_obj_decref(v.u.o);
}
pp_val pp_ref(pp_obj *o) { pp_val v; v.t = PP_REF; o->refcnt++; v.u.o = o; return v; }

int pp_fmt_num(char *buf, int cap, pp_val v) {
    if (v.t == PP_INT) return pp_snprintf(buf, cap, "%lld", (long long)v.u.i);
    if (v.t == PP_NUM) {
        double d = v.u.n;
        if (isnan(d)) return pp_snprintf(buf, cap, "NaN");
        if (isinf(d)) return pp_snprintf(buf, cap, d < 0 ? "-Inf" : "Inf");
        if (d == 0) return pp_snprintf(buf, cap, "0");
        if (d == floor(d) && fabs(d) < 1e15) return pp_snprintf(buf, cap, "%lld", (long long)d);
        return pp_snprintf(buf, cap, "%.15g", d);
    }
    buf[0] = 0;
    return 0;
}

static const char *ref_name(pp_obj *o) {
    switch (o->kind) {
    case PP_OARRAY: return "ARRAY";
    case PP_OHASH:  return "HASH";
    case PP_OCODE:  return "CODE";
    case PP_OREGEX: return "Regexp";
    case PP_OFILE:  return "GLOB";
    case PP_OGLOB:  return "GLOB";
    default:
        if (o->u.scalar.t == PP_REF) return "REF";
        return "SCALAR";
    }
}

int (*pp_ovl_hook)(pp_val v, int kind, pp_val *out);
pp_str *pp_to_str(pp_val v) {
    char buf[64];
    switch (v.t) {
    case PP_STR: v.u.s->refcnt++; return v.u.s;
    case PP_INT: case PP_NUM: {
        int n = pp_fmt_num(buf, sizeof buf, v);
        return pp_str_new(buf, n);
    }
    case PP_REF: {
        if (v.u.o->kind == PP_OREGEX) {
            /* qr// stringifies like perl: (?^i:pattern) */
            const pp_regex *re = v.u.o->u.re;
            pp_str *r = pp_str_new(NULL, 0);
            r = pp_str_reserve(r, re->srclen + 8);
            int n = 0;
            r->d[n++] = '('; r->d[n++] = '?'; r->d[n++] = '^';
            if (re->flags & PP_RE_M) r->d[n++] = 'm';
            if (re->flags & PP_RE_S) r->d[n++] = 's';
            if (re->flags & PP_RE_I) r->d[n++] = 'i';
            if (re->flags & PP_RE_X) r->d[n++] = 'x';
            r->d[n++] = ':';
            memcpy(r->d + n, re->source, (size_t)re->srclen); n += re->srclen;
            r->d[n++] = ')'; r->d[n] = 0; r->len = n;
            return r;
        }
        if (v.u.o->class) {   /* a blessed reference: Class=HASH(0x...) */
            pp_val ov;
            if (pp_ovl_hook && pp_ovl_hook(v, 's', &ov)) { pp_str *r = pp_to_str(ov); pp_release(ov); return r; }
            char big[192];
            int n = pp_snprintf(big, sizeof big, "%s=%s(0x%lx)", v.u.o->class->name, ref_name(v.u.o), (unsigned long)(uintptr_t)v.u.o);
            return pp_str_new(big, n >= (int)sizeof big ? (int)sizeof big - 1 : n);
        }
        int n = pp_snprintf(buf, sizeof buf, "%s(0x%lx)", ref_name(v.u.o), (unsigned long)(uintptr_t)v.u.o);
        return pp_str_new(buf, n);
    }
    default: return pp_str_new("", 0);
    }
}

const char *pp_to_cstr(pp_val v, pp_str **tmp) {
    if (v.t == PP_STR) { *tmp = NULL; return v.u.s->d; }
    *tmp = pp_to_str(v);
    return (*tmp)->d;
}

int pp_looks_like_number(const char *s, int len) {
    int i = 0;
    while (i < len && isspace((unsigned char)s[i])) i++;
    if (i < len && (s[i] == '+' || s[i] == '-')) i++;
    int digits = 0;
    while (i < len && isdigit((unsigned char)s[i])) { i++; digits++; }
    if (i < len && s[i] == '.') { i++; while (i < len && isdigit((unsigned char)s[i])) { i++; digits++; } }
    if (!digits) return 0;
    if (i < len && (s[i] == 'e' || s[i] == 'E')) {
        int j = i + 1;
        if (j < len && (s[j] == '+' || s[j] == '-')) j++;
        if (j < len && isdigit((unsigned char)s[j])) { while (j < len && isdigit((unsigned char)s[j])) j++; i = j; }
    }
    while (i < len && isspace((unsigned char)s[i])) i++;
    return i == len;
}

pp_val pp_str_to_num(const char *s, int len) {
    int i = 0;
    while (i < len && isspace((unsigned char)s[i])) i++;
    int start = i;
    int neg = 0;
    if (i < len && (s[i] == '+' || s[i] == '-')) { neg = s[i] == '-'; i++; }
    int intstart = i;
    int64_t iv = 0; int overflow = 0;
    while (i < len && isdigit((unsigned char)s[i])) {
        int d = s[i] - '0';
        if (iv > (INT64_MAX - d) / 10) overflow = 1;
        else iv = iv * 10 + d;
        i++;
    }
    int isfloat = 0;
    if (i < len && s[i] == '.' ) {
        int j = i + 1;
        if (j < len && isdigit((unsigned char)s[j])) { isfloat = 1; while (j < len && isdigit((unsigned char)s[j])) j++; i = j; }
        else if (i > intstart) { isfloat = 1; i = j; }
    }
    if (i > intstart && i < len && (s[i] == 'e' || s[i] == 'E')) {
        int j = i + 1;
        if (j < len && (s[j] == '+' || s[j] == '-')) j++;
        if (j < len && isdigit((unsigned char)s[j])) { isfloat = 1; while (j < len && isdigit((unsigned char)s[j])) j++; i = j; }
    }
    if (i == intstart) {
        /* Inf / NaN */
        if (len - i >= 3 && (strncasecmp(s + i, "inf", 3) == 0)) return pp_num(neg ? -INFINITY : INFINITY);
        if (len - i >= 3 && (strncasecmp(s + i, "nan", 3) == 0)) return pp_num(NAN);
        return pp_int(0);
    }
    if (isfloat || overflow) {
        char tmp[64];
        int n = i - start; if (n > 63) n = 63;
        memcpy(tmp, s + start, n); tmp[n] = 0;
        return pp_num(strtod(tmp, NULL));
    }
    return pp_int(neg ? -iv : iv);
}

pp_val pp_numify(pp_val v) {
    switch (v.t) {
    case PP_INT: case PP_NUM: return v;
    case PP_STR: return pp_str_to_num(v.u.s->d, v.u.s->len);
    case PP_REF: {
        pp_val ov;
        if (pp_ovl_hook && v.u.o->class && pp_ovl_hook(v, 'n', &ov)) { pp_val r = pp_numify(ov); pp_release(ov); return r; }
        return pp_int((int64_t)(uintptr_t)v.u.o);
    }
    default: return pp_int(0);
    }
}
int64_t pp_to_int(pp_val v) {
    v = pp_numify(v);
    if (v.t == PP_INT) return v.u.i;
    if (isnan(v.u.n)) return 0;
    if (v.u.n >= 9.2233720368547758e18) return INT64_MAX;
    if (v.u.n <= -9.2233720368547758e18) return INT64_MIN;
    return (int64_t)v.u.n;
}
double pp_to_num(pp_val v) {
    v = pp_numify(v);
    return v.t == PP_INT ? (double)v.u.i : v.u.n;
}
int pp_truth(pp_val v) {
    switch (v.t) {
    case PP_UNDEF: return 0;
    case PP_INT: return v.u.i != 0;
    case PP_NUM: return v.u.n != 0;
    case PP_STR: return !(v.u.s->len == 0 || (v.u.s->len == 1 && v.u.s->d[0] == '0'));
    case PP_REF: {
        pp_val ov;
        if (pp_ovl_hook && v.u.o->class && pp_ovl_hook(v, 'b', &ov)) { int t = pp_truth(ov); pp_release(ov); return t; }
        return 1;
    }
    default: return 1;
    }
}
int pp_str_cmp(pp_val a, pp_val b) {
    pp_str *ta, *tb;
    const char *sa = pp_to_cstr(a, &ta), *sb = pp_to_cstr(b, &tb);
    int la = ta ? ta->len : a.u.s->len, lb = tb ? tb->len : b.u.s->len;
    int m = la < lb ? la : lb;
    int r = memcmp(sa, sb, m);
    if (r == 0) r = la < lb ? -1 : la > lb ? 1 : 0;
    else r = r < 0 ? -1 : 1;
    if (ta) pp_str_decref(ta);
    if (tb) pp_str_decref(tb);
    return r;
}
int pp_val_is_array_ref(pp_val v) { return v.t == PP_REF && v.u.o->kind == PP_OARRAY; }

/* ---------------- lists ---------------- */

void pp_list_push(pp_list *l, pp_val v) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->v = pp_xrealloc(l->v, l->cap * sizeof(pp_val));
    }
    l->v[l->n++] = v;
}
void pp_list_free(pp_list *l) {
    for (int i = 0; i < l->n; i++) pp_release(l->v[i]);
    free(l->v);
    l->v = NULL; l->n = l->cap = 0;
}
/* same type and same value/identity (strings and refs compare by pointer) */
int pp_val_same(pp_val a, pp_val b) {
    if (a.t != b.t) return 0;
    switch (a.t) {
    case PP_INT: return a.u.i == b.u.i;
    case PP_NUM: return a.u.n == b.u.n;
    case PP_STR: return a.u.s == b.u.s;
    case PP_REF: return a.u.o == b.u.o;
    default: return 1;
    }
}

int pp_list_arg_int(pp_list *args, int i, int64_t dflt) {
    if (i >= args->n) return (int)dflt;
    return (int)pp_to_int(args->v[i]);
}

/* ---------------- objects ---------------- */

pp_obj *pp_obj_new(int kind) {
    pp_obj *o = pp_xmalloc(sizeof(pp_obj));
    pp_stat_objs++;
    memset(o, 0, sizeof *o);
    o->refcnt = 1; o->kind = (uint8_t)kind;
    return o;
}
void pp_obj_incref(pp_obj *o) { o->refcnt++; }
/* the tie table: a handful of entries at most, searched only for variables with the flag set */
static struct tie_ent { pp_obj *var; pp_val obj; } *tie_tab;
static int tie_cap;
int pp_tie_count;
#define tie_n pp_tie_count
pp_val *pp_tie_find(pp_obj *var) {
    if (!var->tied) return NULL;
    for (int i = 0; i < tie_n; i++) if (tie_tab[i].var == var) return &tie_tab[i].obj;
    return NULL;
}
void pp_tie_remove(pp_obj *var) {
    var->tied = 0;
    for (int i = 0; i < tie_n; i++) if (tie_tab[i].var == var) {
        pp_val old = tie_tab[i].obj;
        tie_tab[i] = tie_tab[--tie_n];
        if (!tie_n) { free(tie_tab); tie_tab = NULL; tie_cap = 0; }
        pp_release(old);
        return;
    }
}
void pp_tie_set(pp_obj *var, pp_val obj) {
    pp_tie_remove(var);
    if (tie_n == tie_cap) { tie_cap = tie_cap ? tie_cap * 2 : 4; tie_tab = pp_xrealloc(tie_tab, tie_cap * sizeof *tie_tab); }
    tie_tab[tie_n].var = var; tie_tab[tie_n].obj = obj; tie_n++;
    var->tied = 1;
}
static void obj_free(pp_obj *o) {
    if (o->tied) pp_tie_remove(o);
    switch (o->kind) {
    case PP_OSCALAR: pp_release(o->u.scalar); break;
    case PP_OARRAY:
        for (int i = 0; i < o->u.arr.n; i++) pp_release(o->u.arr.v[i]);
        free(o->u.arr.v);
        break;
    case PP_OHASH:
        for (int i = 0; i < o->u.hash.n; i++) if (o->u.hash.e[i].klen >= 0) { free(o->u.hash.e[i].key); pp_release(o->u.hash.e[i].v); }
        free(o->u.hash.e); free(o->u.hash.bk);
        break;
    case PP_OCODE:
        if (o->u.code.env) pp_env_decref(o->u.code.env);
        pp_release(o->u.code.constval);
        break;
    case PP_OREGEX:
        pp_re_free(o->u.re);
        break;
    case PP_OFILE:
        if (o->u.file.fp) pp_host_fclose(o->u.file.fp);
        break;
    case PP_OGLOB:
        pp_release(o->u.glob.name);
        if (o->u.glob.io) pp_obj_decref(o->u.glob.io);
        break;
    }
    free(o);
    pp_stat_objs--;
}

/* Freeing an object releases its children, which can free more objects: for a
 * deeply nested structure ($r = [$r] x 100000) the naive recursion overflows
 * the C stack. The outermost call drains nested frees from a worklist instead,
 * so destruction runs at a fixed stack depth. */
static pp_obj **free_work; static int free_nwork, free_capwork, free_active;

void pp_obj_decref(pp_obj *o) {
    if (!o || --o->refcnt > 0) return;
    if (free_active) {
        if (free_nwork == free_capwork) {
            free_capwork = free_capwork ? free_capwork * 2 : 32;
            free_work = pp_xrealloc(free_work, free_capwork * sizeof *free_work);
        }
        free_work[free_nwork++] = o;
        return;
    }
    free_active = 1;
    obj_free(o);
    while (free_nwork) obj_free(free_work[--free_nwork]);
    free_active = 0;
}

pp_obj *pp_new_array(void) { return pp_obj_new(PP_OARRAY); }

void pp_array_reserve(pp_obj *a, int n) {
    if (n <= a->u.arr.cap) return;
    int cap = a->u.arr.cap ? a->u.arr.cap * 2 : 8;
    if (cap < n) cap = n;
    a->u.arr.v = pp_xrealloc(a->u.arr.v, cap * sizeof(pp_val));
    a->u.arr.cap = cap;
}
void pp_array_push(pp_obj *a, pp_val v) {
    pp_array_reserve(a, a->u.arr.n + 1);
    a->u.arr.v[a->u.arr.n++] = v;
}
pp_val *pp_array_slot(pp_obj *a, int i) {
    if (i < 0) i += a->u.arr.n;
    if (i < 0) return NULL;
    if (i >= a->u.arr.n) {
        pp_array_reserve(a, i + 1);
        for (int k = a->u.arr.n; k <= i; k++) a->u.arr.v[k] = pp_undef();
        a->u.arr.n = i + 1;
    }
    return &a->u.arr.v[i];
}
void pp_array_set(pp_obj *a, int i, pp_val v) {
    pp_val *s = pp_array_slot(a, i);
    if (!s) { pp_release(v); return; }
    pp_release(*s);
    *s = v;
}
pp_val pp_array_get(pp_obj *a, int i) {
    if (i < 0) i += a->u.arr.n;
    if (i < 0 || i >= a->u.arr.n) return pp_undef();
    return pp_copy(a->u.arr.v[i]);
}
void pp_array_clear(pp_obj *a) {
    for (int i = 0; i < a->u.arr.n; i++) pp_release(a->u.arr.v[i]);
    a->u.arr.n = 0;
}
pp_val pp_array_shift(pp_obj *a) {
    if (a->u.arr.n == 0) return pp_undef();
    pp_val v = a->u.arr.v[0];
    memmove(a->u.arr.v, a->u.arr.v + 1, (a->u.arr.n - 1) * sizeof(pp_val));
    a->u.arr.n--;
    return v;
}
pp_val pp_array_pop(pp_obj *a) {
    if (a->u.arr.n == 0) return pp_undef();
    return a->u.arr.v[--a->u.arr.n];
}
void pp_array_unshift(pp_obj *a, pp_val v) {
    pp_array_reserve(a, a->u.arr.n + 1);
    memmove(a->u.arr.v + 1, a->u.arr.v, a->u.arr.n * sizeof(pp_val));
    a->u.arr.v[0] = v;
    a->u.arr.n++;
}

/* ---------------- hashes ---------------- */

static unsigned hash_bytes(const char *k, int len) {
    unsigned h = 2166136261u;
    for (int i = 0; i < len; i++) { h ^= (unsigned char)k[i]; h *= 16777619u; }
    return h;
}
static void hash_rebuild(pp_obj *h, int nb) {
    /* compact entries and rebuild buckets */
    int j = 0;
    for (int i = 0; i < h->u.hash.n; i++) {
        if (h->u.hash.e[i].klen >= 0) {
            if (j != i) h->u.hash.e[j] = h->u.hash.e[i];
            j++;
        }
    }
    h->u.hash.n = j;
    h->u.hash.live = j;
    free(h->u.hash.bk);
    h->u.hash.nb = nb;
    h->u.hash.bk = pp_xmalloc(nb * sizeof(int));
    for (int i = 0; i < nb; i++) h->u.hash.bk[i] = -1;
    for (int i = 0; i < j; i++) {
        int b = h->u.hash.e[i].h & (nb - 1);
        h->u.hash.e[i].next = h->u.hash.bk[b];
        h->u.hash.bk[b] = i;
    }
}
pp_val *pp_hash_find(pp_obj *h, const char *k, int klen) {
    if (h->u.hash.nb == 0) return NULL;
    unsigned hv = hash_bytes(k, klen);
    int i = h->u.hash.bk[hv & (h->u.hash.nb - 1)];
    while (i >= 0) {
        pp_hent *e = &h->u.hash.e[i];
        if (e->h == hv && e->klen == klen && memcmp(e->key, k, klen) == 0) return &e->v;
        i = e->next;
    }
    return NULL;
}
pp_val *pp_hash_slot(pp_obj *h, const char *k, int klen) {
    pp_val *v = pp_hash_find(h, k, klen);
    if (v) return v;
    if (h->u.hash.nb == 0 || h->u.hash.live + 1 > h->u.hash.nb) {
        hash_rebuild(h, h->u.hash.nb ? h->u.hash.nb * 2 : 8);
    }
    if (h->u.hash.n == h->u.hash.cap) {
        h->u.hash.cap = h->u.hash.cap ? h->u.hash.cap * 2 : 8;
        h->u.hash.e = pp_xrealloc(h->u.hash.e, h->u.hash.cap * sizeof(pp_hent));
    }
    pp_hent *e = &h->u.hash.e[h->u.hash.n];
    e->key = pp_xmalloc(klen + 1); memcpy(e->key, k, klen); e->key[klen] = 0;
    e->klen = klen; e->h = hash_bytes(k, klen); e->v = pp_undef();
    int b = e->h & (h->u.hash.nb - 1);
    e->next = h->u.hash.bk[b];
    h->u.hash.bk[b] = h->u.hash.n;
    h->u.hash.n++; h->u.hash.live++;
    return &e->v;
}
pp_val pp_hash_get(pp_obj *h, const char *k, int klen) {
    pp_val *v = pp_hash_find(h, k, klen);
    return v ? pp_copy(*v) : pp_undef();
}
void pp_hash_set(pp_obj *h, const char *k, int klen, pp_val v) {
    pp_val *s = pp_hash_slot(h, k, klen);
    pp_release(*s);
    *s = v;
}
int pp_hash_delete(pp_obj *h, const char *k, int klen, pp_val *out) {
    pp_val *v = pp_hash_find(h, k, klen);
    if (!v) { if (out) *out = pp_undef(); return 0; }
    pp_hent *e = (pp_hent *)((char *)v - offsetof(pp_hent, v));
    if (out) *out = e->v; else pp_release(e->v);
    e->v = pp_undef();
    free(e->key); e->key = NULL; e->klen = -1;
    h->u.hash.live--;
    if (h->u.hash.n > 16 && h->u.hash.live < h->u.hash.n / 2) hash_rebuild(h, h->u.hash.nb);
    return 1;
}
void pp_hash_clear(pp_obj *h) {
    for (int i = 0; i < h->u.hash.n; i++) if (h->u.hash.e[i].klen >= 0) { free(h->u.hash.e[i].key); pp_release(h->u.hash.e[i].v); }
    h->u.hash.n = h->u.hash.live = 0;
    for (int i = 0; i < h->u.hash.nb; i++) h->u.hash.bk[i] = -1;
}

/* ---------------- symbols & environments ---------------- */

pp_sym *pp_intern(pp_interp *I, const char *name, int len) {
    unsigned h = hash_bytes(name, len);
    pp_sym *s = I->syms[h % PP_SYM_BUCKETS];
    for (; s; s = s->next)
        if (s->h == h && strlen(s->name) == (size_t)len && memcmp(s->name, name, len) == 0) return s;
    s = pp_xmalloc(sizeof(pp_sym) + len);
    s->h = h; s->sub = NULL; s->gcell = NULL; s->fallback = NULL; memcpy(s->name, name, len); s->name[len] = 0;
    s->next = I->syms[h % PP_SYM_BUCKETS];
    I->syms[h % PP_SYM_BUCKETS] = s;
    return s;
}

void pp_env_grow(pp_env *e, int nslots) {
    if (nslots <= e->n) return;
    e->cells = pp_xrealloc(e->cells, nslots * sizeof(pp_obj *));
    for (int i = e->n; i < nslots; i++) e->cells[i] = NULL;
    e->n = e->cap = nslots;
}

/* like pp_intern, but NULL when the name was never interned (does not create a symbol) */
pp_sym *pp_sym_find(pp_interp *I, const char *name, int len) {
    unsigned h = hash_bytes(name, len);
    for (pp_sym *s = I->syms[h % PP_SYM_BUCKETS]; s; s = s->next)
        if (s->h == h && strlen(s->name) == (size_t)len && memcmp(s->name, name, len) == 0) return s;
    return NULL;
}

pp_env *pp_env_new(pp_env *parent, int nslots) {
    pp_env *e = pp_xmalloc(sizeof(pp_env));
    e->refcnt = 1; e->parent = parent; e->n = nslots; e->cap = nslots; e->is_sub = 0;
    e->cells = nslots ? pp_xmalloc(nslots * sizeof(pp_obj *)) : NULL;
    for (int i = 0; i < nslots; i++) e->cells[i] = NULL;
    if (parent) parent->refcnt++;
    return e;
}
void pp_env_incref(pp_env *e) { e->refcnt++; }
void pp_env_decref(pp_env *e) {
    while (e && --e->refcnt == 0) {
        pp_env *p = e->parent;
        for (int i = 0; i < e->n; i++) if (e->cells[i]) pp_obj_decref(e->cells[i]);
        free(e->cells); free(e);
        e = p;
    }
}
static pp_obj *cell_for_sigil(char sigil) {
    switch (sigil) {
    case '@': return pp_obj_new(PP_OARRAY);
    case '%': return pp_obj_new(PP_OHASH);
    default:  return pp_obj_new(PP_OSCALAR);
    }
}
pp_obj *pp_env_cell(pp_env *e, int slot, char sigil) {
    if (!e->cells[slot]) e->cells[slot] = cell_for_sigil(sigil);
    return e->cells[slot];
}
pp_obj *pp_env_reset(pp_env *e, int slot, char sigil) {
    /* closures that captured the old cell keep it alive through their own reference */
    if (e->cells[slot]) pp_obj_decref(e->cells[slot]);
    e->cells[slot] = cell_for_sigil(sigil);
    return e->cells[slot];
}
static void globals_push(pp_env *g, pp_obj *cell) {
    if (g->n == g->cap) {
        g->cap = g->cap ? g->cap * 2 : 16;
        g->cells = pp_xrealloc(g->cells, g->cap * sizeof(pp_obj *));
    }
    g->cells[g->n++] = cell;
}
pp_obj *pp_global(pp_interp *I, pp_sym *s) {
    if (s->gcell) return s->gcell;
    s->gcell = cell_for_sigil(s->name[0]);
    globals_push(I->globals, s->gcell);
    return s->gcell;
}
pp_obj *pp_glob_obj(pp_interp *I, pp_sym *gsym) {
    if (gsym->gcell) return gsym->gcell;
    pp_obj *g = pp_obj_new(PP_OGLOB);
    char buf[264];
    int n = pp_snprintf(buf, sizeof buf, "*%s%s", strstr(gsym->name, "::") ? "" : "main::", gsym->name + 1);
    g->u.glob.name = pp_strn(buf, n);
    gsym->gcell = g;
    globals_push(I->globals, g);
    return g;
}
void pp_global_alias(pp_interp *I, pp_sym *s, pp_obj *cell) {
    cell->refcnt++;
    globals_push(I->globals, cell);
    s->gcell = cell;
}
