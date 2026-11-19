/* Regular-expression operators: m//, s///, tr///, qr//, split /re/, pos(), $1 $& */
#include "pp_internal.h"
#include "pp_regex.h"

static pp_val scalar_or_push(int ctx, pp_list *out, pp_val v) {
    if (ctx == CTX_LIST && out) { pp_list_push(out, v); return pp_undef(); }
    return v;
}
static void str_append(pp_str **r, const char *p, int n) {
    if (n <= 0) return;
    *r = pp_str_reserve(*r, n);
    memcpy((*r)->d + (*r)->len, p, (size_t)n);
    (*r)->len += n; (*r)->d[(*r)->len] = 0;
}

/* ---------------- compiled patterns ---------------- */

pp_obj *pp_regex_from_val(pp_interp *I, pp_val v, int cflags) {
    if (v.t == PP_REF && v.u.o->kind == PP_OREGEX) { pp_obj_incref(v.u.o); return v.u.o; }
    pp_str *s = pp_to_str(v);
    for (int i = 0; i < 4; i++) {
        pp_re_ent *e = &I->re_cache[i];
        if (e->src && e->flags == cflags && e->src->len == s->len && memcmp(e->src->d, s->d, (size_t)s->len) == 0) {
            pp_str_decref(s);
            pp_obj_incref(e->re);
            return e->re;
        }
    }
    char err[96];
    pp_regex *re = pp_re_compile(s->d, s->len, cflags, err, sizeof err);
    if (!re) {
        pp_die(I, "%s in regex m/%.*s/", err, s->len > 60 ? 60 : s->len, s->d);
        pp_str_decref(s);
        return NULL;
    }
    pp_obj *o = pp_obj_new(PP_OREGEX);
    o->u.re = re;
    pp_re_ent *e = &I->re_cache[I->re_next];
    I->re_next = (I->re_next + 1) & 3;
    if (e->src) pp_str_decref(e->src);
    if (e->re) pp_obj_decref(e->re);
    e->src = s; e->flags = cflags; e->re = o;
    pp_obj_incref(o);
    return o;
}

pp_obj *pp_regex_of(pp_interp *I, pp_node *pat, int cflags) {
    if (pat->type == N_CONST && pat->val.t == PP_REF && pat->val.u.o->kind == PP_OREGEX) { pp_obj_incref(pat->val.u.o); return pat->val.u.o; }
    pp_val v = pp_eval(I, pat, CTX_SCALAR, NULL);
    if (I->ctl) { pp_release(v); return NULL; }
    if (I->warn_on && v.t == PP_UNDEF) pp_warn_uninit(I, "regexp compilation", pp_var_name_of(pat));
    pp_obj *o = pp_regex_from_val(I, v, cflags);
    pp_release(v);
    return o;
}

void pp_regex_cleanup(pp_interp *I) {
    for (int i = 0; i < 4; i++) {
        if (I->re_cache[i].src) pp_str_decref(I->re_cache[i].src);
        if (I->re_cache[i].re) pp_obj_decref(I->re_cache[i].re);
        I->re_cache[i].src = NULL; I->re_cache[i].re = NULL;
        if (I->pos_tab[i].s) pp_str_decref(I->pos_tab[i].s);
        I->pos_tab[i].s = NULL; I->pos_tab[i].cell = NULL;
    }
}

/* ---------------- $& $1 .. ---------------- */

static pp_obj *cap_cell(pp_interp *I, int i) {
    if (i < 10) {
        if (!I->sym_cap[i]) { char nm[3] = { '$', (char)(i ? '0' + i : '&'), 0 }; I->sym_cap[i] = pp_intern(I, nm, 2); }
        return pp_global(I, I->sym_cap[i]);
    }
    char nm[16]; int n = pp_snprintf(nm, sizeof nm, "$%d", i);
    return pp_global(I, pp_intern(I, nm, n));
}

static void set_captures(pp_interp *I, const pp_regex *re, const char *s, const int *caps, int ncap) {
    for (int i = 0; i <= ncap; i++) {
        pp_obj *c = cap_cell(I, i);
        pp_release(c->u.scalar);
        c->u.scalar = caps[2 * i] >= 0 ? pp_strn(s + caps[2 * i], caps[2 * i + 1] - caps[2 * i]) : pp_undef();
    }
    for (int i = ncap + 1; i <= I->m_nset; i++) { pp_obj *c = cap_cell(I, i); pp_release(c->u.scalar); c->u.scalar = pp_undef(); }
    I->m_nset = ncap;
    /* $+ : the text of the last group that matched */
    {
        pp_obj *c = pp_global(I, pp_intern(I, "$+", 2));
        int last = 0;
        for (int i = 1; i <= ncap; i++) if (caps[2 * i] >= 0) last = i;
        pp_release(c->u.scalar);
        c->u.scalar = last ? pp_strn(s + caps[2 * last], caps[2 * last + 1] - caps[2 * last]) : pp_undef();
    }
    /* @- and @+ : start/end offsets per group (like perl, but global, not scoped) */
    {
        pp_obj *mi = pp_global(I, pp_intern(I, "@-", 2));
        pp_obj *pl = pp_global(I, pp_intern(I, "@+", 2));
        pp_array_clear(mi); pp_array_clear(pl);
        for (int i = 0; i <= ncap; i++) {
            pp_array_push(mi, caps[2 * i] >= 0 ? pp_int(caps[2 * i]) : pp_undef());
            pp_array_push(pl, caps[2 * i] >= 0 ? pp_int(caps[2 * i + 1]) : pp_undef());
        }
    }
    /* %+ : named groups of this match */
    {
        pp_obj *h = pp_global(I, pp_intern(I, "%+", 2));
        pp_hash_clear(h);
        if (re) {
            for (int i = 0; i < re->nnames; i++) {
                int g = re->names[i].cap;
                if (g <= ncap && caps[2 * g] >= 0)
                    pp_hash_set(h, re->source + re->names[i].off, re->names[i].len,
                                pp_strn(s + caps[2 * g], caps[2 * g + 1] - caps[2 * g]));
            }
        }
    }
}

/* ---------------- pos() ---------------- */

static int same_text(pp_str *a, pp_str *b) { return a == b || (a && b && a->len == b->len && memcmp(a->d, b->d, (size_t)a->len) == 0); }

static pp_pos_ent *pos_entry(pp_interp *I, pp_obj *cell, pp_str *s, int create) {
    for (int i = 0; i < 4; i++) {
        pp_pos_ent *e = &I->pos_tab[i];
        if (e->cell != cell) continue;
        if (!same_text(e->s, s)) {           /* the variable was assigned since: pos resets */
            if (!create) return NULL;
            pp_str_decref(e->s); e->s = s; s->refcnt++; e->pos = 0; e->empty = 0;
        }
        return e;
    }
    if (!create) return NULL;
    pp_pos_ent *e = &I->pos_tab[I->pos_next];
    I->pos_next = (I->pos_next + 1) & 3;
    if (e->s) pp_str_decref(e->s);
    e->cell = cell; e->s = s; s->refcnt++; e->pos = 0; e->empty = 0;
    return e;
}
static void pos_reset(pp_pos_ent *e) {
    if (!e) return;
    if (e->s) pp_str_decref(e->s);
    e->s = NULL; e->cell = NULL; e->pos = 0; e->empty = 0;
}

pp_val pp_pos_of(pp_interp *I, pp_obj *cell) {
    pp_str *s = pp_to_str(cell->u.scalar);
    pp_pos_ent *e = pos_entry(I, cell, s, 0);
    pp_str_decref(s);
    return e ? pp_int(e->pos) : pp_undef();
}

/* ---------------- m// ---------------- */

static int exec_or_die(pp_interp *I, const pp_regex *re, const char *s, int len, int start, int notempty_at, int *caps) {
    int r = pp_re_exec(re, s, len, start, notempty_at, caps);
    if (r < 0) { pp_die(I, "Regexp backtracking limit exceeded"); return 0; }
    return r;
}

/* subject of a match: the cell when the target is a plain scalar variable (needed for pos) */
static pp_val match_subject(pp_interp *I, pp_node *target, pp_obj **cell, const char *what) {
    pp_val subj;
    *cell = NULL;
    if (!target) { *cell = I->g_underscore; subj = pp_copy((*cell)->u.scalar); }
    else if (target->type == N_VAR && target->sym->name[0] == '$') { *cell = pp_var_cell(I, target); subj = pp_copy((*cell)->u.scalar); }
    else { subj = pp_eval(I, target, CTX_SCALAR, NULL); if (I->ctl) { pp_release(subj); return pp_undef(); } }
    if (I->warn_on && subj.t == PP_UNDEF) pp_warn_uninit(I, what, target ? pp_var_name_of(target) : "$_");
    return subj;
}

static void push_captures(pp_list *out, const char *s, const int *caps, int ncap) {
    if (ncap == 0) { pp_list_push(out, pp_strn(s + caps[0], caps[1] - caps[0])); return; }
    for (int k = 1; k <= ncap; k++) pp_list_push(out, caps[2 * k] >= 0 ? pp_strn(s + caps[2 * k], caps[2 * k + 1] - caps[2 * k]) : pp_undef());
}

pp_val pp_eval_match(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    int flags = n->op;
    if (flags & PP_M_QR) {
        pp_obj *o = pp_regex_of(I, n->b, n->aux);
        if (!o) return pp_undef();
        pp_val r = pp_ref(o); pp_obj_decref(o);
        return scalar_or_push(ctx, out, r);
    }
    pp_obj *cell;
    pp_val subj = match_subject(I, n->a, &cell, "pattern match (m//)");
    if (I->ctl) return pp_undef();
    pp_obj *ro = pp_regex_of(I, n->b, n->aux);
    if (!ro) { pp_release(subj); return pp_undef(); }
    const pp_regex *re = ro->u.re;
    pp_str *s = pp_to_str(subj); pp_release(subj);
    int nslots = pp_re_nslots(re);
    int stackcaps[24];
    int *caps = nslots <= 24 ? stackcaps : pp_xmalloc((size_t)nslots * sizeof(int));
    int matched = 0;
    int want_list = ctx == CTX_LIST && out && !(flags & PP_M_NEG);
    if (!(flags & PP_M_G)) {
        if (exec_or_die(I, re, s->d, s->len, 0, -1, caps)) {
            matched = 1;
            set_captures(I, re, s->d, caps, re->ncap);
            if (want_list) { if (re->ncap == 0) pp_list_push(out, pp_int(1)); else push_captures(out, s->d, caps, re->ncap); }
        }
    }
    else if (want_list) {
        /* all matches; starts at pos() and clears it afterwards */
        pp_pos_ent *pe = cell ? pos_entry(I, cell, s, 0) : NULL;
        int pos = pe ? pe->pos : 0, empty = pe ? pe->empty : 0;
        int *last = pp_xmalloc((size_t)nslots * sizeof(int));
        while (!I->ctl && pos <= s->len && exec_or_die(I, re, s->d, s->len, pos, empty ? pos : -1, caps)) {
            matched++;
            memcpy(last, caps, (size_t)nslots * sizeof(int));
            push_captures(out, s->d, caps, re->ncap);
            pos = caps[1]; empty = caps[0] == caps[1];
        }
        if (matched) set_captures(I, re, s->d, last, re->ncap);
        free(last);
        if (pe) pos_reset(pe);
    }
    else {
        /* scalar /g: continue from pos() */
        pp_pos_ent *pe = cell ? pos_entry(I, cell, s, 1) : NULL;
        int pos = pe ? pe->pos : 0, empty = pe ? pe->empty : 0;
        if (pos <= s->len && exec_or_die(I, re, s->d, s->len, pos, empty ? pos : -1, caps)) {
            matched = 1;
            set_captures(I, re, s->d, caps, re->ncap);
            if (pe) { pe->pos = caps[1]; pe->empty = caps[0] == caps[1]; }
        }
        else if (pe) pos_reset(pe);
    }
    if (caps != stackcaps) free(caps);
    pp_str_decref(s);
    pp_obj_decref(ro);
    if (I->ctl) return pp_undef();
    if (flags & PP_M_NEG) matched = !matched;
    if (want_list) return pp_undef();
    return scalar_or_push(ctx, out, matched ? pp_int(1) : pp_strn("", 0));
}

/* ---------------- s/// ---------------- */

pp_val pp_eval_subst(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    int flags = n->op;
    pp_val subj;
    if (flags & PP_M_R) {
        pp_obj *cell;
        subj = match_subject(I, n->a, &cell, "substitution (s///)");
        if (I->ctl) return pp_undef();
    } else {
        pp_val *slot = n->a ? pp_lvalue(I, n->a) : &I->g_underscore->u.scalar;
        if (!slot) return pp_undef();
        subj = pp_copy(*slot);
        if (I->warn_on && subj.t == PP_UNDEF) pp_warn_uninit(I, "substitution (s///)", n->a ? pp_var_name_of(n->a) : "$_");
    }
    pp_obj *ro = pp_regex_of(I, n->b, n->aux);
    if (!ro) { pp_release(subj); return pp_undef(); }
    const pp_regex *re = ro->u.re;
    pp_str *s = pp_to_str(subj); pp_release(subj);
    int nslots = pp_re_nslots(re);
    int stackcaps[24];
    int *caps = nslots <= 24 ? stackcaps : pp_xmalloc((size_t)nslots * sizeof(int));
    pp_str *result = pp_str_new(NULL, 0);
    int pos = 0, last = 0, count = 0, empty = 0;
    while (pos <= s->len && exec_or_die(I, re, s->d, s->len, pos, empty ? pos : -1, caps)) {
        count++;
        set_captures(I, re, s->d, caps, re->ncap);
        str_append(&result, s->d + last, caps[0] - last);
        pp_val rv = pp_eval(I, n->c, CTX_SCALAR, NULL);
        if (I->ctl) { pp_release(rv); break; }
        if (I->warn_on && rv.t == PP_UNDEF) pp_warn_uninit(I, "substitution iterator", pp_var_name_of(n->c));
        pp_str *t; const char *p = pp_to_cstr(rv, &t);
        str_append(&result, p, t ? t->len : rv.u.s->len);
        if (t) pp_str_decref(t);
        pp_release(rv);
        last = caps[1]; pos = caps[1]; empty = caps[0] == caps[1];
        if (!(flags & PP_M_G)) break;
    }
    if (caps != stackcaps) free(caps);
    pp_obj_decref(ro);
    if (I->ctl) { pp_str_decref(result); pp_str_decref(s); return pp_undef(); }
    str_append(&result, s->d + last, s->len - last);
    pp_val r;
    if (flags & PP_M_R) { r = pp_str_val(result); }
    else if (count) {
        pp_val *slot = n->a ? pp_lvalue(I, n->a) : &I->g_underscore->u.scalar;
        if (!slot) { pp_str_decref(result); pp_str_decref(s); return pp_undef(); }
        pp_release(*slot);
        *slot = pp_str_val(result);
        r = pp_int(count);
    }
    else { pp_str_decref(result); r = pp_strn("", 0); }
    pp_str_decref(s);
    if (flags & PP_M_NEG) { int t = !pp_truth(r); pp_release(r); r = t ? pp_int(1) : pp_strn("", 0); }
    return scalar_or_push(ctx, out, r);
}

/* ---------------- tr/// ---------------- */

pp_val pp_eval_trans(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    int flags = n->op;
    pp_val subj;
    pp_val *slot = NULL;
    if (flags & PP_TR_R) {
        pp_obj *cell;
        subj = match_subject(I, n->a, &cell, "transliteration (tr///)");
        if (I->ctl) return pp_undef();
    } else {
        slot = n->a ? pp_lvalue(I, n->a) : &I->g_underscore->u.scalar;
        if (!slot) return pp_undef();
        subj = pp_copy(*slot);
        if (I->warn_on && subj.t == PP_UNDEF) pp_warn_uninit(I, "transliteration (tr///)", n->a ? pp_var_name_of(n->a) : "$_");
    }
    pp_str *s = pp_to_str(subj); pp_release(subj);
    const pp_str *a = n->b->val.u.s, *b = n->c->val.u.s;
    int map[256];
    for (int i = 0; i < 256; i++) map[i] = -1;
    unsigned char search[256]; int ns = 0;
    if (flags & PP_TR_C) {
        unsigned char in[256]; memset(in, 0, sizeof in);
        for (int i = 0; i < a->len; i++) in[(unsigned char)a->d[i]] = 1;
        for (int c = 0; c < 256; c++) if (!in[c]) search[ns++] = (unsigned char)c;
    } else { ns = a->len; memcpy(search, a->d, (size_t)ns); }
    for (int k = 0; k < ns; k++) {
        int c = search[k];
        if (map[c] != -1) continue;                     /* first occurrence wins */
        if (b->len == 0 && !(flags & PP_TR_D)) map[c] = c;
        else if (k < b->len) map[c] = (unsigned char)b->d[k];
        else if (flags & PP_TR_D) map[c] = -2;
        else map[c] = (unsigned char)b->d[b->len - 1];
    }
    pp_str *result = pp_str_new(NULL, 0);
    result = pp_str_reserve(result, s->len);
    int count = 0, last_out = -1;
    for (int i = 0; i < s->len; i++) {
        int c = (unsigned char)s->d[i], m = map[c];
        if (m == -1) { result->d[result->len++] = (char)c; last_out = -1; continue; }
        count++;
        if (m == -2) continue;
        if ((flags & PP_TR_S) && last_out == m) continue;
        result->d[result->len++] = (char)m; last_out = m;
    }
    result->d[result->len] = 0;
    pp_str_decref(s);
    pp_val r;
    if (flags & PP_TR_R) r = pp_str_val(result);
    else {
        if (count) { pp_release(*slot); *slot = pp_str_val(result); }
        else pp_str_decref(result);
        r = pp_int(count);
    }
    return scalar_or_push(ctx, out, r);
}

/* ---------------- split /re/ ---------------- */

int pp_split_regex(pp_interp *I, pp_obj *ro, const char *s, int len, int limit, pp_list *res) {
    const pp_regex *re = ro->u.re;
    int nslots = pp_re_nslots(re);
    int stackcaps[24];
    int *caps = nslots <= 24 ? stackcaps : pp_xmalloc((size_t)nslots * sizeof(int));
    int field = 0, pos = 0;
    for (;;) {
        if (limit > 0 && res->n >= limit - 1) break;
        if (pos > len) break;
        int r = pp_re_exec(re, s, len, pos, field, caps);   /* an empty match at the field start is skipped */
        if (r < 0) { pp_die(I, "Regexp backtracking limit exceeded"); if (caps != stackcaps) free(caps); return 0; }
        if (!r) break;
        if (caps[1] == 0) { pos = 1; continue; }          /* empty match at the very start: no leading empty field */
        pp_list_push(res, pp_strn(s + field, caps[0] - field));
        for (int k = 1; k <= re->ncap; k++) pp_list_push(res, caps[2 * k] >= 0 ? pp_strn(s + caps[2 * k], caps[2 * k + 1] - caps[2 * k]) : pp_undef());
        field = caps[1];
        pos = caps[1];
    }
    pp_list_push(res, pp_strn(s + field, len - field));
    if (caps != stackcaps) free(caps);
    return 1;
}
