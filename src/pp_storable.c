/* Storable for picoperl: freeze / thaw (natives made on first use, see pp_core_native) */
#include "pp_internal.h"
#include <stdint.h>

static void str_append(pp_str **s, const char *p, int len) {
    *s = pp_str_reserve(*s, len);
    memcpy((*s)->d + (*s)->len, p, len);
    (*s)->len += len;
    (*s)->d[(*s)->len] = 0;
}

/* ---- Storable: freeze / thaw in the format of perl's Storable (major 2) -------------------
   Storable::_freeze(REF, NET, CANONICAL, CLONE) gives the image without the file magic "pst0";
   Storable::_thaw(IMAGE) reads one (native 64-bit little-endian, or network order).
   Every stored value gets a number ("tag") in the order it is written; a value met again is
   written as SX_OBJECT + its tag, which is how shared and circular structures keep their shape.
   CLONE (dclone): things that cannot be stored (qr//, code, handles) are passed by pointer. */
enum { SX_OBJECT = 0, SX_LSCALAR, SX_ARRAY, SX_HASH, SX_REF, SX_UNDEF, SX_INTEGER, SX_DOUBLE, SX_BYTE, SX_NETINT, SX_SCALAR,
       SX_SV_UNDEF = 14, SX_SV_YES, SX_SV_NO, SX_BLESS, SX_IX_BLESS, SX_HOOK, SX_OVERLOAD, SX_UTF8STR = 23, SX_LUTF8STR, SX_FLAG_HASH,
       SX_CODE, SX_WEAKREF, SX_WEAKOVERLOAD, SX_VSTRING, SX_LVSTRING, SX_SVUNDEF_ELEM, SX_REGEXP, SX_LOBJECT, SX_BOOLEAN_TRUE, SX_BOOLEAN_FALSE,
       SX_PP_SAME = 0xfe };
typedef struct { pp_interp *I; pp_str *out; pp_obj *seen, *classes; int tag, nclass, net, canon, clone; const char *err; pp_obj *nohook; pp_list keep; char errbuf[160]; } st_wr;
static const char st_died[] = "";                 /* a hook died: its message stands */
static void st_op(st_wr *w, int op) { char c = (char)op; str_append(&w->out, &c, 1); }
static void st_u32(st_wr *w, uint32_t n, int net) {
    unsigned char b[4];
    for (int i = 0; i < 4; i++) b[i] = (unsigned char)(n >> (net ? 24 - 8 * i : 8 * i));
    str_append(&w->out, (char *)b, 4);
}
static void st_wobj(st_wr *w, pp_obj *o);
static void st_wbody(st_wr *w, pp_val v) {
    if (w->err) return;
    if (v.t == PP_NUM && v.u.n == (double)(int64_t)v.u.n && v.u.n > -9e18 && v.u.n < 9e18) v = pp_int((int64_t)v.u.n);
    switch (v.t) {
    case PP_UNDEF: st_op(w, SX_UNDEF); break;
    case PP_INT:
        if (v.u.i >= -128 && v.u.i <= 127) { st_op(w, SX_BYTE); st_op(w, (int)v.u.i + 128); }
        else if (!w->net) { st_op(w, SX_INTEGER); str_append(&w->out, (char *)&v.u.i, 8); }
        else if (v.u.i >= INT32_MIN && v.u.i <= INT32_MAX) { st_op(w, SX_NETINT); st_u32(w, (uint32_t)v.u.i, 1); }
        else goto text;
        break;
    case PP_NUM:
        if (w->net) goto text;
        st_op(w, SX_DOUBLE); str_append(&w->out, (char *)&v.u.n, 8);
        break;
    case PP_REF: st_op(w, SX_REF); st_wobj(w, v.u.o); break;
    default: text: {
        pp_str *s = pp_to_str(v);
        if (s->len <= 255) { st_op(w, SX_SCALAR); st_op(w, s->len); }
        else { st_op(w, SX_LSCALAR); st_u32(w, (uint32_t)s->len, w->net); }
        str_append(&w->out, s->d, s->len);
        pp_str_decref(s);
    }
    }
}
static void st_wobj(st_wr *w, pp_obj *o);
static int st_keycmp(const void *a, const void *b) {
    const pp_hent *x = *(const pp_hent *const *)a, *y = *(const pp_hent *const *)b;
    int c = memcmp(x->key, y->key, x->klen < y->klen ? x->klen : y->klen);
    return c ? c : x->klen - y->klen;
}
/* A class with STORABLE_freeze: ($frozen, @refs) = $obj->STORABLE_freeze($cloning) is stored
   in place of the object:
     SX_HOOK <flags> [<object> <flags>]... <class> <len> <frozen> [<count> <tag>...]
   An <object> is there for each of @refs not stored yet (flags then has SHF_NEED_RECURSE).
   Returns 0 when the class has no hook, or its hook returned nothing (stored as usual). */
static int st_whook(st_wr *w, pp_obj *o) {
    pp_interp *I = w->I;
    const char *cn = o->class->name; int cl = (int)strlen(cn);
    if ((w->nohook && pp_hash_find(w->nohook, cn, cl)) || !pp_find_method(I, cn, "STORABLE_freeze")) return 0;
    pp_list args = {0}, res = {0};
    pp_list_push(&args, pp_int(w->clone ? 4 : 0));  /* ($cloning is 4 in perl's Storable) */
    pp_val self = pp_ref(o);
    pp_release(pp_tie_call(I, self, "STORABLE_freeze", &args, CTX_LIST, &res, 1));
    pp_release(self);
    if (I->ctl) { pp_list_free(&res); w->err = st_died; return 1; }
    if (!res.n) {                                    /* the class does without its hook */
        if (!w->nohook) w->nohook = pp_obj_new(PP_OHASH);
        pp_hash_set(w->nohook, cn, cl, pp_int(1));
        pp_list_free(&res);
        return 0;
    }
    int type = o->kind == PP_OSCALAR ? 0 : o->kind == PP_OARRAY ? 1 : 2, recursed = 0;
    if (res.n > 1 && pp_find_method(I, cn, "STORABLE_attach")) {
        snprintf(w->errbuf, sizeof w->errbuf, "Freeze cannot return references if %.80s class is using STORABLE_attach", cn);
        w->err = w->errbuf;
    }
    for (int i = 1; i < res.n && !w->err; i++) {
        if (res.v[i].t != PP_REF) {
            snprintf(w->errbuf, sizeof w->errbuf, "Item #%d returned by STORABLE_freeze for %.80s is not a reference", i, cn);
            w->err = w->errbuf;
            break;
        }
        pp_obj *x = res.v[i].u.o;
        if (pp_hash_find(w->seen, (char *)&x, sizeof x)) continue;
        if (!recursed++) st_op(w, SX_HOOK);
        st_op(w, 0x40 | type);
        pp_list_push(&w->keep, pp_copy(res.v[i]));   /* alive until the end: its address stands for it in `seen` */
        st_wobj(w, x);
    }
    if (w->err) { pp_list_free(&res); return 1; }
    pp_val *ix = pp_hash_find(w->classes, cn, cl);
    int cnum = ix ? (int)ix->u.i : cl;
    pp_str *fz = pp_to_str(res.v[0]);
    int flags = type | (cnum > 255 ? 0x04 : 0) | (ix ? 0x20 : 0) | (fz->len > 255 ? 0x08 : 0) | (res.n > 1 ? 0x80 : 0) | (res.n > 256 ? 0x10 : 0);
    if (!recursed) st_op(w, SX_HOOK);
    st_op(w, flags);
    if (flags & 0x04) st_u32(w, (uint32_t)cnum, w->net); else st_op(w, cnum);
    if (!ix) { str_append(&w->out, cn, cl); pp_hash_set(w->classes, cn, cl, pp_int(w->nclass++)); }
    if (flags & 0x08) st_u32(w, (uint32_t)fz->len, w->net); else st_op(w, fz->len);
    str_append(&w->out, fz->d, fz->len);
    pp_str_decref(fz);
    if (flags & 0x80) {
        if (flags & 0x10) st_u32(w, (uint32_t)(res.n - 1), w->net); else st_op(w, res.n - 1);
        for (int i = 1; i < res.n; i++) {
            pp_obj *x = res.v[i].u.o;
            st_u32(w, (uint32_t)pp_hash_find(w->seen, (char *)&x, sizeof x)->u.i, 1);
        }
    }
    pp_list_free(&res);
    return 1;
}
static void st_wobj(st_wr *w, pp_obj *o) {
    if (w->err) return;
    pp_val *t = pp_hash_find(w->seen, (char *)&o, sizeof o);
    if (t) { st_op(w, SX_OBJECT); st_u32(w, (uint32_t)t->u.i, 1); return; }
    pp_hash_set(w->seen, (char *)&o, sizeof o, pp_int(w->tag++));
    if (o->kind != PP_OSCALAR && o->kind != PP_OARRAY && o->kind != PP_OHASH) {
        if (w->clone && o->kind != PP_OCODE && o->kind != PP_OGLOB && o->kind != PP_OFILE) { st_op(w, SX_PP_SAME); str_append(&w->out, (char *)&o, sizeof o); }
        else w->err = o->kind == PP_OCODE ? "Can't store CODE items" : o->kind == PP_OREGEX ? "Can't store REGEXP items" : "Can't store GLOB items";
        return;
    }
    if (o->class && st_whook(w, o)) return;
    if (o->class) {
        const char *cn = o->class->name; int cl = (int)strlen(cn);
        pp_val *ix = pp_hash_find(w->classes, cn, cl);
        int n = ix ? (int)ix->u.i : cl;
        st_op(w, ix ? SX_IX_BLESS : SX_BLESS);
        if (n <= 127) st_op(w, n); else { st_op(w, 0x80); st_u32(w, (uint32_t)n, w->net); }
        if (!ix) { str_append(&w->out, cn, cl); pp_hash_set(w->classes, cn, cl, pp_int(w->nclass++)); }
    }
    if (o->kind == PP_OSCALAR) st_wbody(w, o->u.scalar);
    else if (o->kind == PP_OARRAY) {
        st_op(w, SX_ARRAY); st_u32(w, (uint32_t)o->u.arr.n, w->net);
        for (int i = 0; i < o->u.arr.n && !w->err; i++) { w->tag++; st_wbody(w, o->u.arr.v[i]); }
    } else {
        int n = o->u.hash.live, k = 0;
        st_op(w, SX_HASH); st_u32(w, (uint32_t)n, w->net);
        pp_hent **es = pp_xmalloc(sizeof *es * (size_t)(n + 1));
        for (int i = 0; i < o->u.hash.n && k < n; i++) if (o->u.hash.e[i].klen >= 0) es[k++] = &o->u.hash.e[i];
        if (w->canon) qsort(es, (size_t)k, sizeof *es, st_keycmp);
        for (int i = 0; i < k && !w->err; i++) {
            w->tag++; st_wbody(w, es[i]->v);
            st_u32(w, (uint32_t)es[i]->klen, w->net); str_append(&w->out, es[i]->key, es[i]->klen);
        }
        free(es);
    }
}
static int st_last_net;                              /* Storable::last_op_in_netorder() */
static int st_busy[2];                               /* inside freeze / thaw (a hook is running): Storable::is_storing() / is_retrieving() */
pp_val pp_st_storing(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return st_busy[0] ? pp_int(1) : pp_strn("", 0); }
pp_val pp_st_retrieving(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return st_busy[1] ? pp_int(1) : pp_strn("", 0); }
pp_val pp_st_lastnet(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return st_last_net ? pp_int(1) : pp_strn("", 0); }
pp_val pp_st_freeze(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    if (a->n < 1 || a->v[0].t != PP_REF) return pp_die(I, "not a reference");
    st_wr w = { I, pp_str_new(NULL, 0), pp_obj_new(PP_OHASH), pp_obj_new(PP_OHASH), 0, 0,
                a->n > 1 && pp_truth(a->v[1]), a->n > 2 && pp_truth(a->v[2]), a->n > 3 && pp_truth(a->v[3]), NULL, NULL, {0}, "" };
    st_last_net = w.net;
    if (w.net) str_append(&w.out, "\x05\x0b", 2);
    else str_append(&w.out, "\x04\x0b\x08" "12345678" "\x04\x08\x08\x08", 15);
    st_busy[0]++;
    st_wobj(&w, a->v[0].u.o);
    st_busy[0]--;
    pp_obj_decref(w.seen); pp_obj_decref(w.classes);
    if (w.nohook) pp_obj_decref(w.nohook);
    pp_list_free(&w.keep);
    if (w.err) { pp_str_decref(w.out); return w.err == st_died ? pp_undef() : pp_die(I, "%s", w.err); }
    return pp_str_val(w.out);
}

typedef struct { pp_val v; uint8_t cont; } st_tag;
typedef struct { pp_interp *I; const unsigned char *p, *end; int net, clone; st_tag *tags; int ntags, captags; pp_sym **cls; int ncls, capcls; const char *err; char errbuf[160]; } st_rd;
static const char st_short[] = "Storable image is truncated";
static uint32_t st_r32(st_rd *r, int net) {
    if (r->end - r->p < 4) { r->err = st_short; r->p = r->end; return 0; }
    uint32_t n = 0;
    for (int i = 0; i < 4; i++) n |= (uint32_t)r->p[i] << (net ? 24 - 8 * i : 8 * i);
    r->p += 4;
    return n;
}
static int st_r8(st_rd *r) { if (r->p >= r->end) { r->err = st_short; return 0; } return *r->p++; }
static const char *st_bytes(st_rd *r, uint32_t n) {
    if ((uint32_t)(r->end - r->p) < n) { r->err = st_short; r->p = r->end; return NULL; }
    const char *s = (const char *)r->p; r->p += n;
    return s;
}
static void st_settag(st_rd *r, int i, pp_val v, int cont) { pp_release(r->tags[i].v); r->tags[i].v = pp_copy(v); r->tags[i].cont = (uint8_t)cont; }
/* Reads one value. *cont: the value is the thing itself (an array, a hash, a scalar that
   already has its own cell), held by the reference that is returned; otherwise a plain scalar
   value. *tagp: its tag. bless: the class the thing is blessed into. */
static pp_val st_read(st_rd *r, int *cont, int *tagp, pp_sym *bless) {
    *cont = 0; *tagp = -1;
    if (r->err) return pp_undef();
    int op = st_r8(r);
    if (r->err) return pp_undef();
    if (op == SX_OBJECT) {
        uint32_t t = st_r32(r, 1);
        if (r->err || t >= (uint32_t)r->ntags) { r->err = "Object # out of range in Storable image"; return pp_undef(); }
        *cont = r->tags[t].cont; *tagp = (int)t;
        return pp_copy(r->tags[t].v);
    }
    if (op == SX_BLESS || op == SX_IX_BLESS) {
        uint32_t n = (uint32_t)st_r8(r);
        if (n & 0x80) n = st_r32(r, r->net);
        pp_sym *c = NULL;
        if (op == SX_BLESS) {
            const char *s = st_bytes(r, n);
            if (!s) return pp_undef();
            c = pp_intern(r->I, s, (int)n);
            if (r->ncls == r->capcls) { r->capcls = r->capcls ? r->capcls * 2 : 8; r->cls = pp_xrealloc(r->cls, sizeof *r->cls * (size_t)r->capcls); }
            r->cls[r->ncls++] = c;
        } else if (n < (uint32_t)r->ncls) c = r->cls[n];
        else { r->err = "Class # out of range in Storable image"; return pp_undef(); }
        return st_read(r, cont, tagp, c);
    }
    if (r->ntags == r->captags) { r->captags = r->captags ? r->captags * 2 : 16; r->tags = pp_xrealloc(r->tags, sizeof *r->tags * (size_t)r->captags); }
    int my = r->ntags++;
    r->tags[my].v = pp_undef(); r->tags[my].cont = 0;
    pp_val v = pp_undef();
    switch (op) {
    case SX_UNDEF: case SX_SV_UNDEF: case SX_SVUNDEF_ELEM: break;
    case SX_SV_YES: case SX_BOOLEAN_TRUE: v = pp_int(1); break;
    case SX_SV_NO: case SX_BOOLEAN_FALSE: v = pp_strn("", 0); break;
    case SX_BYTE: v = pp_int(st_r8(r) - 128); break;
    case SX_NETINT: v = pp_int((int32_t)st_r32(r, 1)); break;
    case SX_INTEGER: { const char *s = st_bytes(r, 8); int64_t i = 0; if (s) memcpy(&i, s, 8); v = pp_int(i); break; }
    case SX_DOUBLE: { const char *s = st_bytes(r, 8); double d = 0; if (s) memcpy(&d, s, 8); v = pp_num(d); break; }
    case SX_VSTRING: case SX_LVSTRING: {             /* the v-string magic, then the scalar itself */
        st_bytes(r, op == SX_VSTRING ? (uint32_t)st_r8(r) : st_r32(r, r->net));
        op = st_r8(r);
        if (op != SX_SCALAR && op != SX_LSCALAR && op != SX_UTF8STR && op != SX_LUTF8STR) { r->err = "Storable image is corrupt"; break; }
    } /* fall through */
    case SX_SCALAR: case SX_UTF8STR: case SX_LSCALAR: case SX_LUTF8STR: {
        uint32_t n = op == SX_SCALAR || op == SX_UTF8STR ? (uint32_t)st_r8(r) : st_r32(r, r->net);
        const char *s = st_bytes(r, n);
        if (s) v = pp_strn(s, (int)n);
        break;
    }
    case SX_REF: case SX_OVERLOAD: case SX_WEAKREF: case SX_WEAKOVERLOAD: {
        int c, ct;
        pp_val child = st_read(r, &c, &ct, NULL);
        if (c) v = child;
        else {                                       /* a reference to a scalar: the scalar gets a cell, shared from now on */
            pp_obj *box = pp_obj_new(PP_OSCALAR);
            box->u.scalar = child;
            v = pp_ref(box); pp_obj_decref(box);
            if (ct >= 0) st_settag(r, ct, v, 1);
        }
        break;
    }
    case SX_ARRAY: {
        uint32_t n = st_r32(r, r->net);
        pp_obj *o = pp_new_array();
        o->class = bless;
        v = pp_ref(o); pp_obj_decref(o);
        st_settag(r, my, v, 1);
        for (uint32_t i = 0; i < n && !r->err; i++) { int c, ct; pp_array_push(o, st_read(r, &c, &ct, NULL)); }
        *cont = 1; *tagp = my;
        return v;
    }
    case SX_HASH: case SX_FLAG_HASH: {
        if (op == SX_FLAG_HASH) st_r8(r);
        uint32_t n = st_r32(r, r->net);
        pp_obj *o = pp_obj_new(PP_OHASH);
        o->class = bless;
        v = pp_ref(o); pp_obj_decref(o);
        st_settag(r, my, v, 1);
        for (uint32_t i = 0; i < n && !r->err; i++) {
            int c, ct;
            pp_val e = st_read(r, &c, &ct, NULL);
            if (op == SX_FLAG_HASH && (st_r8(r) & 0x10)) r->err = "Storable image: this kind of hash key is not supported";
            uint32_t kl = st_r32(r, r->net);
            const char *k = st_bytes(r, kl);
            if (k && !r->err) pp_hash_set(o, k, (int)kl, e); else pp_release(e);
        }
        *cont = 1; *tagp = my;
        return v;
    }
    case SX_PP_SAME: {                               /* dclone: the same object (a pointer: never taken from a stored image) */
        pp_obj *o = NULL; const char *s = r->clone ? st_bytes(r, sizeof o) : NULL;
        if (!s) { r->err = "Storable image is corrupt"; break; }
        memcpy(&o, s, sizeof o);
        v = pp_ref(o);
        st_settag(r, my, v, 1);
        *cont = 1; *tagp = my;
        return v;
    }
    case SX_HOOK: {                                  /* see st_whook */
        pp_interp *I = r->I;
        int flags = st_r8(r);
        if ((flags & 3) == 3) { r->err = "Storable image holds a tied variable: not supported in picoperl"; break; }
        pp_obj *o = pp_obj_new((flags & 3) == 0 ? PP_OSCALAR : (flags & 3) == 1 ? PP_OARRAY : PP_OHASH);
        v = pp_ref(o); pp_obj_decref(o);
        st_settag(r, my, v, 1);
        *cont = 1; *tagp = my;
        while ((flags & 0x40) && !r->err) { int c, ct; pp_release(st_read(r, &c, &ct, NULL)); flags = st_r8(r); }
        uint32_t n = flags & 0x04 ? st_r32(r, r->net) : (uint32_t)st_r8(r);
        pp_sym *c = NULL;
        if (r->err) return v;
        if (flags & 0x20) {
            if (n >= (uint32_t)r->ncls) { r->err = "Class # out of range in Storable image"; return v; }
            c = r->cls[n];
        } else {
            const char *s = st_bytes(r, n);
            if (!s) return v;
            c = pp_intern(I, s, (int)n);
            if (r->ncls == r->capcls) { r->capcls = r->capcls ? r->capcls * 2 : 8; r->cls = pp_xrealloc(r->cls, sizeof *r->cls * (size_t)r->capcls); }
            r->cls[r->ncls++] = c;
        }
        n = flags & 0x08 ? st_r32(r, r->net) : (uint32_t)st_r8(r);
        const char *fz = st_bytes(r, n);
        if (!fz) return v;
        pp_list args = {0};
        pp_list_push(&args, pp_int(r->clone ? 4 : 0));
        pp_list_push(&args, pp_strn(fz, (int)n));
        uint32_t cnt = !(flags & 0x80) ? 0 : flags & 0x10 ? st_r32(r, r->net) : (uint32_t)st_r8(r);
        for (uint32_t i = 0; i < cnt && !r->err; i++) {
            uint32_t t = st_r32(r, 1);
            if (r->err) break;
            if (t >= (uint32_t)r->ntags) { r->err = "Object # out of range in Storable image"; break; }
            if (!r->tags[t].cont) {                  /* a plain scalar: it gets a cell, as under SX_REF */
                pp_obj *box = pp_obj_new(PP_OSCALAR);
                box->u.scalar = pp_copy(r->tags[t].v);
                pp_val bv = pp_ref(box); pp_obj_decref(box);
                st_settag(r, (int)t, bv, 1);
                pp_release(bv);
            }
            pp_list_push(&args, pp_copy(r->tags[t].v));
        }
        if (r->err) { pp_list_free(&args); return v; }
        if (pp_find_method(I, c->name, "STORABLE_attach")) {       /* Class->STORABLE_attach($cloning, $frozen) makes the object */
            if (cnt) { pp_list_free(&args); r->err = "STORABLE_attach called with unexpected references"; return v; }
            pp_val cv = pp_cstr(c->name);
            pp_val got = pp_tie_call(I, cv, "STORABLE_attach", &args, CTX_SCALAR, NULL, 1);
            pp_release(cv);
            if (I->ctl) { pp_release(got); r->err = st_died; return v; }
            if (got.t != PP_REF || !got.u.o->class || !pp_isa(I, got.u.o->class->name, c->name)) {
                pp_release(got);
                snprintf(r->errbuf, sizeof r->errbuf, "STORABLE_attach did not return a %.80s object", c->name);
                r->err = r->errbuf;
                return v;
            }
            pp_release(v);
            st_settag(r, my, got, 1);
            return got;
        }
        o->class = c;
        if (!pp_find_method(I, c->name, "STORABLE_thaw")) {
            pp_list_free(&args);
            snprintf(r->errbuf, sizeof r->errbuf, "No STORABLE_thaw defined for objects of class %.60s (even after a \"require %.60s;\")", c->name, c->name);
            r->err = r->errbuf;
            return v;
        }
        pp_release(pp_tie_call(I, v, "STORABLE_thaw", &args, CTX_SCALAR, NULL, 1));
        if (I->ctl) r->err = st_died;
        return v;
    }
    case 11: case 12: case 13: case 21: case 22: r->err = "Storable image holds a tied variable: not supported in picoperl"; break;
    case SX_CODE: r->err = "Can't retrieve code references"; break;
    case SX_REGEXP: r->err = "Storable image holds a regexp: not supported in picoperl"; break;
    default: r->err = "Storable image is corrupt"; break;
    }
    if (bless && !r->err) {                          /* a blessed scalar: it needs a cell of its own */
        pp_obj *box = pp_obj_new(PP_OSCALAR);
        box->u.scalar = v; box->class = bless;
        v = pp_ref(box); pp_obj_decref(box);
        *cont = 1;
    }
    st_settag(r, my, v, *cont);
    *tagp = my;
    return v;
}
pp_val pp_st_thaw(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    pp_str *s = a->n ? pp_to_str(a->v[0]) : pp_str_new(NULL, 0);
    st_rd r = { I, (const unsigned char *)s->d, (const unsigned char *)s->d + s->len, 0, a->n > 1 && pp_truth(a->v[1]), NULL, 0, 0, NULL, 0, 0, NULL, "" };
    pp_val v = pp_undef();
    int b0 = st_r8(&r), minor = st_r8(&r);
    r.net = b0 & 1;
    if (r.err) r.err = NULL;                         /* (an empty image: undef, like perl) */
    else if (b0 >> 1 > 2) { pp_str_decref(s); return pp_die(I, "Storable binary image v%d.%d more recent than I am (v2.12)", b0 >> 1, minor); }
    else if (b0 >> 1 != 2) r.err = "Storable binary image is too old (or not a Storable image)";
    else {
        if (!r.net) {
            uint32_t n = (uint32_t)st_r8(&r);
            const char *bo = st_bytes(&r, n);
            const char *sz = st_bytes(&r, minor >= 2 ? 4 : 3);
            if (!r.err && (n != 8 || memcmp(bo, "12345678", 8) != 0)) r.err = "Byte order is not compatible";
            else if (!r.err && minor >= 2 && sz[3] != 8) r.err = "Double size is not compatible";
        }
        int c, ct;
        st_busy[1]++;
        v = st_read(&r, &c, &ct, NULL);
        st_busy[1]--;
        if (!c && !r.err) { pp_obj *box = pp_obj_new(PP_OSCALAR); box->u.scalar = v; v = pp_ref(box); pp_obj_decref(box); }
    }
    for (int i = 0; i < r.ntags; i++) pp_release(r.tags[i].v);
    free(r.tags); free(r.cls);
    st_last_net = r.net;
    if (a->n > 2 && a->v[2].t == PP_REF && a->v[2].u.o->kind == PP_OSCALAR) {      /* \$n: how many bytes the image took (fd_retrieve) */
        pp_release(a->v[2].u.o->u.scalar); a->v[2].u.o->u.scalar = pp_int((int64_t)((const char *)r.p - s->d));
    }
    pp_str_decref(s);
    if (r.err) { pp_release(v); return r.err == st_short || r.err == st_died ? pp_undef() : pp_die(I, "%s", r.err); }   /* (cut short: undef, like perl) */
    return v;
}
