/* Tree-walking evaluator for picoperl. */
#include "pp_internal.h"
#include <math.h>
#include <ctype.h>

/* $^O: the host OS on a PC build (so scripts see what perl would report there),
   "picoperl" on the board and in the browser */
#ifndef PP_OSNAME
#if defined(__linux__)
#define PP_OSNAME "linux"
#elif defined(__APPLE__)
#define PP_OSNAME "darwin"
#elif defined(_WIN32)
#define PP_OSNAME "MSWin32"
#else
#define PP_OSNAME "picoperl"
#endif
#endif

#define NOINLINE __attribute__((noinline))

/* ------------------------------------------------------------------ */
/* errors                                                              */
/* ------------------------------------------------------------------ */

static pp_obj *sig_handler(pp_interp *I, const char *name);
static int class_isa(pp_interp *I, const char *cls, const char *want, int depth);

pp_val pp_die_val(pp_interp *I, pp_val msg) {
    /* takes ownership of msg */
    pp_val v;
    if (msg.t == PP_REF) v = msg;   /* die with an object */
    else {
        pp_str *s = pp_to_str(msg);
        pp_release(msg);
        if (s->len == 0 || s->d[s->len - 1] != '\n') {
            char tail[160];
            int n = pp_snprintf(tail, sizeof tail, " at %s line %d.\n", I->file ? I->file : "-", I->line);
            s = pp_str_reserve(s, n);
            memcpy(s->d + s->len, tail, n); s->len += n; s->d[s->len] = 0;
        }
        v = pp_str_val(s);
    }
    if (!(I->hook_busy & 2)) {
        pp_obj *h = sig_handler(I, "__DIE__");
        if (h) {   /* like perl: the hook sees the final message; if it dies itself, its error wins */
            I->hook_busy |= 2;
            pp_list args = {0};
            pp_list_push(&args, pp_copy(v));
            pp_val r = pp_call(I, h, &args, CTX_VOID, NULL);
            pp_release(r); pp_list_free(&args);
            I->hook_busy &= (uint8_t)~2;
            if (I->ctl == CTL_DIE) { pp_release(v); return pp_undef(); }
        }
    }
    pp_release(I->err);
    I->err = v;
    I->ctl = CTL_DIE;
    return pp_undef();
}

pp_val pp_die(pp_interp *I, const char *fmt, ...) {
    char buf[256];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return pp_die_val(I, pp_cstr(buf));
}

/* ------------------------------------------------------------------ */
/* variables                                                           */
/* ------------------------------------------------------------------ */

/* the cell of an N_VAR node: lexical (depth/slot) or global */
static inline pp_obj *var_cell(pp_interp *I, pp_node *n) {
    if (n->flags & PP_VAR_LEX) {
        pp_env *e = I->env;
        for (int d = n->aux; d > 0; d--) e = e->parent;
        return pp_env_cell(e, n->op, n->sym->name[0]);
    }
    return n->sym->gcell ? n->sym->gcell : pp_global(I, n->sym);
}
pp_obj *pp_var_cell(pp_interp *I, pp_node *n) { return var_cell(I, n); }
/* @_ of the innermost sub, or NULL at file level */
static pp_obj *current_args(pp_interp *I) {
    for (pp_env *e = I->env; e; e = e->parent)
        if (e->is_sub) return pp_env_cell(e, 0, '@');
    return NULL;
}

void pp_warn_uninit_args(pp_interp *I, pp_node *n, pp_list *args, const char *what) {
    /* warn for undef arguments of print/say/join...; names only when args map 1:1 to nodes */
    for (int i = 0; i < args->n; i++) {
        if (args->v[i].t != PP_UNDEF) continue;
        const char *var = (args->n == n->n && n->list[i]->type == N_VAR && n->list[i]->sym->name[0] == '$') ? n->list[i]->sym->name : NULL;
        if (var) pp_warn(I, "Use of uninitialized value %s in %s", var, what);
        else pp_warn(I, "Use of uninitialized value in %s", what);
    }
}

/* the %SIG{name} handler coderef, or NULL */
static pp_obj *sig_handler(pp_interp *I, const char *name) {
    if (!I->sym_sig) I->sym_sig = pp_intern(I, "%SIG", 4);
    pp_obj *h = I->sym_sig->gcell;
    if (!h || h->kind != PP_OHASH) return NULL;
    pp_val *v = pp_hash_find(h, name, (int)strlen(name));
    if (!v || v->t != PP_REF || v->u.o->kind != PP_OCODE) return NULL;
    return v->u.o;
}

/* final warning text: goes to $SIG{__WARN__} when set (not recursively), else stderr */
void pp_emit_warning(pp_interp *I, const char *msg, int len) {
    if (!(I->hook_busy & 1)) {
        pp_obj *h = sig_handler(I, "__WARN__");
        if (h) {
            I->hook_busy |= 1;
            pp_list args = {0};
            pp_list_push(&args, pp_strn(msg, len));
            pp_val r = pp_call(I, h, &args, CTX_VOID, NULL);
            pp_release(r); pp_list_free(&args);
            I->hook_busy &= (uint8_t)~1;
            return;
        }
    }
    pp_host_write(2, msg, len);
}

void pp_warn(pp_interp *I, const char *fmt, ...) {
    char buf[300];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf - 60, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof buf - 61) n = (int)sizeof buf - 61;      /* the message was cut to fit */
    n += pp_snprintf(buf + n, sizeof buf - n, " at %s line %d.\n", I->file ? I->file : "-", I->line);
    pp_emit_warning(I, buf, n);
}

/* Resolve "&Pkg::name" (or "&name" in main). Perl rules: only the package's own subs
 * and what was imported into it. Two extensions: constants the host or the interpreter
 * provides lazily (pp_host_const, e.g. PicoPerl::Keycodes::KC_A; pp_core_const, Fcntl's)
 * and wildcard exports recorded by
 * `use` (an @EXPORT entry like "KC_*"). */
static pp_obj *resolve_depth(pp_interp *I, pp_sym *sym, int depth) {
    if (sym->sub) return sym->sub;
    if (sym->name[0] != '&' || depth > 8) return NULL;
    pp_val v;
    if (pp_host_const(sym->name + 1, &v) || pp_core_const(sym->name + 1, &v)) {
        pp_obj *code = pp_obj_new(PP_OCODE);
        code->u.code.is_const = 1; code->u.code.constval = v; code->u.code.name = sym->name + 1;
        pp_define_sub(I, sym, code);
        return sym->sub;
    }
    if (I->nlazy) {
        const char *full = sym->name + 1;
        const char *last = NULL;
        for (const char *p = full; *p; p++) if (p[0] == ':' && p[1] == ':') last = p;
        const char *base = last ? last + 2 : full;
        char pkg[128];
        if (last) snprintf(pkg, sizeof pkg, "%.*s", (int)(last - full), full); else strcpy(pkg, "main");
        for (int i = 0; i < I->nlazy; i++) {
            if (strcmp(I->lazy[i].dst, pkg) != 0) continue;
            if (strncmp(base, I->lazy[i].prefix, strlen(I->lazy[i].prefix)) != 0) continue;
            char buf[256];
            int n = pp_snprintf(buf, sizeof buf, "&%s::%s", I->lazy[i].src, base);
            pp_obj *code = resolve_depth(I, pp_intern(I, buf, n), depth + 1);
            if (code) { code->refcnt++; pp_define_sub(I, sym, code); return sym->sub; }
        }
    }
    return NULL;
}
pp_obj *pp_resolve_sub(pp_interp *I, pp_sym *sym) { return resolve_depth(I, sym, 0); }

static void add_lazy_export(pp_interp *I, const char *dst, const char *src, const char *prefix) {
    for (int i = 0; i < I->nlazy; i++)
        if (!strcmp(I->lazy[i].dst, dst) && !strcmp(I->lazy[i].src, src) && !strcmp(I->lazy[i].prefix, prefix)) return;
    I->lazy = pp_xrealloc(I->lazy, (I->nlazy + 1) * sizeof *I->lazy);
    I->lazy[I->nlazy].dst = pp_xmalloc(strlen(dst) + 1); strcpy(I->lazy[I->nlazy].dst, dst);
    I->lazy[I->nlazy].src = pp_xmalloc(strlen(src) + 1); strcpy(I->lazy[I->nlazy].src, src);
    I->lazy[I->nlazy].prefix = pp_xmalloc(strlen(prefix) + 1); strcpy(I->lazy[I->nlazy].prefix, prefix);
    I->nlazy++;
}
/* A sub remembers the file it was written in (for die / warn / caller while it runs): the
   name is kept as a symbol in its body block, which has no other use for that field. */
void pp_note_sub_file(pp_interp *I, pp_node *body, const char *file) {
    if (!body || body->type != N_BLOCK || body->sym || !file) return;
    char buf[300];
    int n = pp_snprintf(buf, sizeof buf, "\001%s", file);
    body->sym = pp_intern(I, buf, n);
}
pp_obj *pp_lookup_sub(pp_interp *I, pp_sym *sym) { return pp_resolve_sub(I, sym); }

/* the symbol for `sigil` + a name written the way scripts spell it: "x", "main::x", "::x",
   "Pkg::x" or "*main::x" (a stringified glob). Unqualified names belong to `pkg` (NULL = main). */
static pp_sym *named_sym(pp_interp *I, char sigil, const char *pkg, const char *name) {
    if (*name == '*') name++;
    if (!strcmp(name, "main::") || !strcmp(name, "::")) return pp_intern(I, sigil == '%' ? "%main::" : sigil == '*' ? "*main::" : "$main::", 7);   /* main's name table itself */
    if (!strncmp(name, "main::", 6)) { name += 6; pkg = NULL; }   /* explicitly main: do not re-qualify */
    else if (!strncmp(name, "::", 2)) { name += 2; pkg = NULL; }
    char buf[256]; int n;
    if (!pkg || !strcmp(pkg, "main") || strstr(name, "::")) n = pp_snprintf(buf, sizeof buf, "%c%s", sigil, name);
    else n = pp_snprintf(buf, sizeof buf, "%c%s::%s", sigil, pkg, name);
    if (n >= (int)sizeof buf) n = (int)sizeof buf - 1;
    return pp_intern(I, buf, n);
}
/* a name, or a reference to a glob (which stands for its name: @$globref is @name) */
static int is_globref(pp_val v) { return v.t == PP_REF && v.u.o->kind == PP_OGLOB; }
pp_sym *pp_glob_named(pp_interp *I, const char *name) { return named_sym(I, '*', NULL, name); }
/* *$fh: a handle from open(my $fh ...) becomes a glob the first time it is used as one */
static void glob_operand(pp_interp *I, pp_val v) {
    if (v.t == PP_REF && v.u.o->kind == PP_OFILE) pp_glob_promote(I, v.u.o);
}
static int is_symname(pp_val v) { return v.t == PP_STR || v.t == PP_INT || v.t == PP_NUM || is_globref(v); }
static pp_str *symname_str(pp_val v) {
    if (is_globref(v)) { pp_str *s = v.u.o->u.glob.name.u.s; s->refcnt++; return s; }
    return pp_to_str(v);
}

pp_sym *pp_symref(pp_interp *I, pp_node *n, pp_val v, char sigil, int strict) {
    pp_str *s = symname_str(v);
    pp_sym *sym = NULL;
    /* a glob ("*main::x", what *$ref gives) is not a symbolic reference: strict refs lets it through */
    if (strict && (n->flags & PP_NODE_STRICTREFS) && s->d[0] != '*') {
        const char *what = sigil == '@' ? "an ARRAY" : sigil == '%' ? "a HASH" : sigil == '&' ? "a subroutine" : "a SCALAR";
        pp_die(I, "Can't use string (\"%.32s\"%s) as %s ref while \"strict refs\" in use", s->d, s->len > 32 ? "..." : "", what);
    }
    else sym = named_sym(I, sigil, n->b && n->b->type == N_CONST && n->b->val.t == PP_STR ? n->b->val.u.s->d : NULL, s->d);
    pp_str_decref(s);
    return sym;
}
/* the sub a string names (&{"name"}, \&$name); dies if there is none */
static pp_val *lvalue_or_null(pp_interp *I, pp_node *n);


static pp_obj *symref_code(pp_interp *I, pp_node *n, pp_val v) {
    pp_sym *sym = pp_symref(I, n, v, '&', 1);
    if (!sym) return NULL;
    pp_obj *code = pp_resolve_sub(I, sym);
    if (!code) pp_die(I, "Undefined subroutine &%s%s called", strstr(sym->name, "::") ? "" : "main::", sym->name + 1);
    return code;
}

static pp_val scalar_or_push(int ctx, pp_list *out, pp_val v);
/* *name = VALUE: a typeglob has no value of its own here. Assigning a reference replaces the
   slot of that kind (&name, $name, @name, %name); assigning another glob (or its name)
   aliases all four. */
static NOINLINE pp_val eval_glob_assign(pp_interp *I, pp_node *n) {
    pp_node *g = n->a;
    pp_val nv = pp_eval(I, g->a, CTX_SCALAR, NULL);
    if (I->ctl) { pp_release(nv); return pp_undef(); }
    glob_operand(I, nv);
    if (!is_symname(nv)) { pp_release(nv); return pp_die(I, "Not a GLOB reference"); }
    pp_str *name = symname_str(nv); pp_release(nv);
    const char *pkg = g->b && g->b->type == N_CONST ? g->b->val.u.s->d : NULL;
    pp_val v = pp_eval(I, n->b, CTX_SCALAR, NULL);
    if (I->ctl) { pp_str_decref(name); pp_release(v); return pp_undef(); }
    if (v.t == PP_REF && !is_globref(v)) {
        pp_obj *o = v.u.o;
        if (o->kind == PP_OCODE) { o->refcnt++; pp_define_sub(I, named_sym(I, '&', pkg, name->d), o); }
        else if (o->kind == PP_OSCALAR || o->kind == PP_OARRAY || o->kind == PP_OHASH) {
            pp_sym *dst = named_sym(I, o->kind == PP_OARRAY ? '@' : o->kind == PP_OHASH ? '%' : '$', pkg, name->d);
            if (dst->gcell != o) pp_global_alias(I, dst, o);
        }
    }
    else if (is_symname(v)) {   /* *a = *b */
        pp_str *src = symname_str(v);
        static const char sigils[] = "$@%";
        for (int i = 0; sigils[i]; i++) {
            pp_sym *d = named_sym(I, sigils[i], pkg, name->d), *s = named_sym(I, sigils[i], pkg, src->d);
            if (d != s && d->gcell != pp_global(I, s)) pp_global_alias(I, d, pp_global(I, s));
        }
        pp_sym *d = named_sym(I, '&', pkg, name->d), *s = named_sym(I, '&', pkg, src->d);
        pp_obj *code = d != s ? pp_resolve_sub(I, s) : NULL;
        if (code) { code->refcnt++; pp_define_sub(I, d, code); }
        pp_str_decref(src);
    }
    pp_str_decref(name);
    return v;
}
/* -e $path and friends: 1 / "" like perl, undef when the path does not exist; -s gives the size */
static NOINLINE pp_val eval_filetest(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_val v = pp_eval(I, n->a, CTX_SCALAR, NULL);
    if (I->ctl) { pp_release(v); return pp_undef(); }
    pp_val r = pp_undef();
    if (v.t != PP_UNDEF && v.t != PP_REF) {
        pp_str *s = pp_to_str(v);
        long long size = 0;
        int m = (int)strlen(s->d) == s->len ? pp_host_filetest(s->d, &size) : -1;
        {   /* remember the path for a following -X _ / stat(_) */
            pp_obj *last = pp_global(I, pp_intern(I, "$\002stat", 6));
            if (!(last->u.scalar.t == PP_STR && last->u.scalar.u.s == s)) { pp_release(last->u.scalar); s->refcnt++; last->u.scalar = pp_str_val(s); }
        }
        pp_str_decref(s);
        if (m >= 0) {
            int t;
            switch (n->op) {
            case 'e': t = 1; break;
            case 'f': t = m & PP_FT_FILE; break;
            case 'd': t = m & PP_FT_DIR; break;
            case 'r': t = m & PP_FT_READ; break;
            case 'w': t = m & PP_FT_WRITE; break;
            case 'x': case 'X': t = m & PP_FT_EXEC; break;
            case 'l': t = m & PP_FT_LINK; break;
            case 'R': t = m & PP_FT_READ; break;
            case 'W': t = m & PP_FT_WRITE; break;
            case 'o': case 'O': t = 1; break;                 /* there is one user here: the owner */
            case 'p': case 'S': case 'b': case 'c': case 'u': case 'g': case 'k': t = 0; break;   /* no pipes, sockets, devices, set-id bits */
            case 'T': case 'B': {
                /* text or binary, guessed from the first block like perl: a NUL byte or many
                   odd bytes make it binary; an empty file is both */
                t = 0;
                if (m & PP_FT_DIR) { t = n->op == 'B'; break; }   /* perl calls a directory binary */
                pp_str *path = pp_to_str(pp_global(I, pp_intern(I, "$\002stat", 6))->u.scalar);
                void *fp = pp_host_fopen(path->d, "r");
                pp_str_decref(path);
                if (!fp) break;
                unsigned char blk[512];
                int got = (int)pp_host_fread(fp, (char *)blk, sizeof blk), odd = 0, nul = 0;
                pp_host_fclose(fp);
                for (int k = 0; k < got; k++) {
                    if (!blk[k]) nul = 1;
                    else if (blk[k] < 32 && !strchr("\n\r\t\f\b\033", blk[k])) odd++;
                }
                int binary = got > 0 && (nul || odd * 3 > got);
                t = got == 0 ? 1 : n->op == 'B' ? binary : !binary;
                break;
            }
            case 'z': t = size == 0; break;
            default:  t = size > 0; break;   /* 's' */
            }
            r = !t ? pp_strn("", 0) : n->op == 's' ? pp_int(size) : pp_int(1);
        }
    }
    pp_release(v);
    return scalar_or_push(ctx, out, r);
}
/* the package a sub belongs to, from its qualified name ("Pkg::name"; plain names are main) */
static pp_val code_pkg(pp_obj *code) {
    const char *name = code->u.code.name, *last = NULL;
    if (name) for (const char *p = name; *p; p++) if (p[0] == ':' && p[1] == ':') last = p;
    return last ? pp_strn(name, (int)(last - name)) : pp_strn("main", 4);
}
/* caller: (package, file, line) of the code that called the current sub; caller(N) looks N
   calls further up and adds (sub name, hasargs, wantarray). Empty list / undef at file level. */
static NOINLINE pp_val eval_caller(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    int64_t up = 0;
    if (n->a) { pp_val v = pp_eval(I, n->a, CTX_SCALAR, NULL); if (I->ctl) { pp_release(v); return pp_undef(); } up = pp_to_int(v); pp_release(v); }
    struct pp_frame *f = I->frame;
    while (f && up-- > 0) f = f->up;
    if (!f) return pp_undef();
    pp_val pkg = f->caller_code ? code_pkg(f->caller_code) : f->caller_pkg ? pp_cstr(f->caller_pkg) : pp_strn("main", 4);
    if (!(ctx == CTX_LIST && out)) return pkg;
    pp_list_push(out, pkg);
    pp_list_push(out, pp_cstr(f->file ? f->file : "-"));
    pp_list_push(out, pp_int(f->line));
    if (n->a) {
        const char *name = !f->code ? "(eval)" : f->code->u.code.name ? f->code->u.code.name : "__ANON__";   /* no code: a require */
        char buf[200];
        int len = pp_snprintf(buf, sizeof buf, "%s%s", !f->code || strstr(name, "::") ? "" : "main::", name);
        pp_list_push(out, pp_strn(buf, len >= (int)sizeof buf ? (int)sizeof buf - 1 : len));
        pp_list_push(out, pp_int(1));
        pp_list_push(out, f->ctx == CTX_LIST ? pp_int(1) : f->ctx == CTX_SCALAR ? pp_strn("", 0) : pp_undef());
        if (I->file_pkg && I->file_pkg[0] == 'D' && !strcmp(I->file_pkg, "DB")) {
            /* called from package DB: @DB::args gets the arguments of that call (what is left
               of its @_; perl also shows the ones it has shifted off). Carp prints them. */
            pp_obj *da = pp_global(I, pp_intern(I, "@DB::args", 9));
            pp_array_clear(da);
            if (f->argv) for (int i = 0; i < f->argv->u.arr.n; i++) pp_array_push(da, pp_copy(f->argv->u.arr.v[i]));
        }
    }
    return pp_undef();
}
/* substr($s, OFF, LEN) =~ s/a/b/ (or tr///, or .= and the like): OFF and LEN are evaluated
   once, the part goes into a hidden variable, the operation runs on that, and the result is
   put back with the 4-argument substr */
static NOINLINE pp_val eval_substr_lv(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_node *call = n->a;
    pp_node c[3]; pp_node *list[4]; pp_node sub = *call;
    memset(c, 0, sizeof c);
    int na = call->n - 1;                             /* OFF, or OFF and LEN */
    list[0] = call->list[0];
    for (int i = 0; i < na; i++) {
        c[i].type = N_CONST; c[i].line = call->line;
        c[i].val = pp_eval(I, call->list[i + 1], CTX_SCALAR, NULL);
        list[i + 1] = &c[i];
        if (I->ctl) { for (int k = 0; k <= i; k++) pp_release(c[k].val); return pp_undef(); }
    }
    if (na == 1) { c[1].type = N_CONST; c[1].line = call->line; c[1].val = pp_int(0x7fffffff); list[2] = &c[1]; }
    sub.list = list; sub.n = (uint16_t)(na + 1);
    pp_val part = pp_eval(I, &sub, CTX_SCALAR, NULL);
    pp_val res = pp_undef();
    if (!I->ctl) {
        pp_obj *cell = pp_global(I, pp_intern(I, "$\x13", 2));
        pp_val saved = cell->u.scalar;                /* (a substr target inside the operation nests) */
        cell->u.scalar = part; part = pp_undef();
        res = pp_eval(I, n->b, ctx == CTX_LIST ? CTX_SCALAR : ctx, NULL);
        c[2].type = N_CONST; c[2].line = call->line; c[2].val = cell->u.scalar;
        cell->u.scalar = saved;
        if (!I->ctl) {
            list[3] = &c[2]; sub.n = 4;
            pp_release(pp_eval(I, &sub, CTX_VOID, NULL));
        }
    }
    pp_release(part);
    for (int k = 0; k < 3; k++) pp_release(c[k].val);
    if (I->ctl) { pp_release(res); return pp_undef(); }
    return scalar_or_push(ctx, out, res);
}
/* a glob used as a value is its name: "*main::name" (enough for *a = *b and for printing) */
static NOINLINE pp_val eval_glob(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_val nv = pp_eval(I, n->a, CTX_SCALAR, NULL);
    if (I->ctl) { pp_release(nv); return pp_undef(); }
    glob_operand(I, nv);
    if (!is_symname(nv)) { pp_release(nv); return pp_die(I, "Not a GLOB reference"); }
    pp_str *name = symname_str(nv); pp_release(nv);
    pp_sym *s = named_sym(I, '*', n->b && n->b->type == N_CONST ? n->b->val.u.s->d : NULL, name->d);
    pp_str_decref(name);
    char buf[264];
    int len = pp_snprintf(buf, sizeof buf, "*%s%s", strstr(s->name, "::") ? "" : "main::", s->name + 1);
    return scalar_or_push(ctx, out, pp_strn(buf, len));
}
/* the interned "*name" an N_GLOB node names (NULL after a die) */
static pp_sym *glob_sym(pp_interp *I, pp_node *n) {
    pp_val nv = pp_eval(I, n->a, CTX_SCALAR, NULL);
    if (I->ctl) { pp_release(nv); return NULL; }
    glob_operand(I, nv);
    if (!is_symname(nv)) { pp_release(nv); pp_die(I, "Not a GLOB reference"); return NULL; }
    pp_str *name = symname_str(nv); pp_release(nv);
    pp_sym *s = named_sym(I, '*', n->b && n->b->type == N_CONST ? n->b->val.u.s->d : NULL, name->d);
    pp_str_decref(name);
    return s;
}
/* the same name with another sigil: "*Pkg::x" -> "@Pkg::x" */
static pp_sym *glob_slot_sym(pp_interp *I, pp_sym *gs, char sigil) {
    char buf[264];
    int n = pp_snprintf(buf, sizeof buf, "%c%s", sigil, gs->name + 1);
    return pp_intern(I, buf, n);
}
/* does the glob name a standard handle? 0 STDIN, 1 STDOUT, 2 STDERR, else -1 */
static int glob_std_fd(pp_sym *gs) {
    return !strcmp(gs->name, "*STDIN") ? 0 : !strcmp(gs->name, "*STDOUT") ? 1 : !strcmp(gs->name, "*STDERR") ? 2 : -1;
}
pp_obj *pp_stash_of(pp_interp *I, pp_sym *hs);
/* *name{SLOT}: a reference to that part of the glob, undef if there is none */
static NOINLINE pp_val eval_globslot(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_sym *gs = glob_sym(I, n->a);
    if (!gs) return pp_undef();
    pp_val kv = pp_eval(I, n->b, CTX_SCALAR, NULL);
    if (I->ctl) { pp_release(kv); return pp_undef(); }
    pp_str *k = pp_to_str(kv); pp_release(kv);
    pp_val r = pp_undef();
    if (!strcmp(k->d, "SCALAR")) r = pp_ref(pp_global(I, glob_slot_sym(I, gs, '$')));
    else if (!strcmp(k->d, "ARRAY") || !strcmp(k->d, "HASH")) {
        pp_sym *s = glob_slot_sym(I, gs, k->d[0] == 'A' ? '@' : '%');
        size_t sl = strlen(s->name);
        if (k->d[0] == 'H' && sl >= 3 && s->name[sl - 1] == ':' && s->name[sl - 2] == ':') {   /* *Pkg::{HASH}: the name table of Pkg */
            pp_obj *st = pp_stash_of(I, s);
            if (st) r = pp_ref(st);
        }
        else if (s->gcell) r = pp_ref(s->gcell);
    }
    else if (!strcmp(k->d, "CODE")) { pp_obj *code = pp_resolve_sub(I, glob_slot_sym(I, gs, '&')); if (code) r = pp_ref(code); }
    else if (!strcmp(k->d, "GLOB")) r = pp_ref(pp_glob_obj(I, gs));
    else if (!strcmp(k->d, "IO") || !strcmp(k->d, "FILEHANDLE")) {
        /* the open file itself, so that it outlives a `local *FH` around the open; the
           standard handles have no file object, they are the glob */
        if (gs->gcell && gs->gcell->kind == PP_OGLOB && gs->gcell->u.glob.io) r = pp_ref(gs->gcell->u.glob.io);
        else if (glob_std_fd(gs) >= 0) r = pp_ref(pp_glob_obj(I, gs));
    }
    else if (!strcmp(k->d, "NAME") || !strcmp(k->d, "PACKAGE")) {
        const char *name = gs->name + 1, *last = NULL;
        for (const char *p = name; *p; p++) if (p[0] == ':' && p[1] == ':') last = p;
        if (k->d[0] == 'N') r = pp_cstr(last ? last + 2 : name);
        else r = last ? pp_strn(name, (int)(last - name)) : pp_strn("main", 4);
    }
    pp_str_decref(k);
    return scalar_or_push(ctx, out, r);
}

/* a sub that has been referred to (\&name) but not defined yet */
static int is_sub_stub(const pp_obj *o) {
    return o->kind == PP_OCODE && !o->u.code.body && !o->u.code.native && !o->u.code.is_const;
}
static pp_obj *find_method(pp_interp *I, const char *cls, const char *name, int depth);
void pp_set_data_handle(pp_interp *I, const char *pkg, const char *text, int len) {
    char name[256];
    if (!strcmp(pkg, "main")) pp_snprintf(name, sizeof name, "DATA"); else pp_snprintf(name, sizeof name, "%s::DATA", pkg);
    pp_sym *gs = pp_glob_named(I, name);
    if (!gs) return;
    pp_obj *glob = pp_glob_obj(I, gs);
    pp_obj *cell = pp_obj_new(PP_OSCALAR);
    cell->u.scalar = pp_strn(text, len);
    pp_obj *f = pp_obj_new(PP_OFILE);        /* an in-memory file on that text */
    f->u.file.mem = cell;                    /* (takes the reference to the cell) */
    f->u.file.fp = cell;
    if (glob->u.glob.io) pp_obj_decref(glob->u.glob.io);
    glob->u.glob.io = f;
}
/* run the DESTROY methods that are waiting; called between statements */
void pp_destroy_run(pp_interp *I) {
    /* A DESTROY that is running may drop more objects: those are destroyed right away (the
       nested call), ahead of the ones that were already waiting when it started (floor). */
    static int floor;
    if (I->ctl) return;                  /* not while a die is travelling */
    pp_obj *o;
    while (!I->ctl && (o = pp_destroy_next(floor)) != NULL) {
        pp_obj *code = o->class ? find_method(I, o->class->name, "DESTROY", 0) : NULL;
        if (code) {
            pp_list args = {0};
            pp_list_push(&args, pp_ref(o));
            code->refcnt++;
            int saved_line = I->line, saved_floor = floor;
            floor = pp_destroy_n;
            pp_release(pp_call(I, code, &args, CTX_VOID, NULL));
            floor = saved_floor;
            pp_list_free(&args);
            pp_obj_decref(code);
            I->line = saved_line;
            if (I->ctl == CTL_DIE) {         /* a die in DESTROY is only a warning, like perl */
                pp_str *m = pp_to_str(I->err);
                I->ctl = CTL_NONE;
                if (I->warn_on) pp_warn(I, "\t(in cleanup) %.*s", m->len > 0 && m->d[m->len - 1] == '\n' ? m->len - 1 : m->len, m->d);
                pp_str_decref(m);
            }
        }
        if (o->refcnt > 1) o->destroyed = 0;     /* DESTROY stored it somewhere: it lives on, and is destroyed again later */
        pp_obj_decref(o);
    }
}
void pp_define_sub(pp_interp *I, pp_sym *sym, pp_obj *code) {
    /* takes ownership of code */
    {   /* a DESTROY anywhere turns the DESTROY machinery on */
        size_t nl = strlen(sym->name);
        if (nl >= 8 && !strcmp(sym->name + nl - 7, "DESTROY") && (nl == 8 || sym->name[nl - 8] == ':')) pp_destroy_on = 1;
    }
    if (sym->sub && sym->sub != code && sym->sub->u.code.native && !code->u.code.native && sym->name[0] == '&' && !strstr(sym->name, "::")) {
        /* main redefines a perl function that is a native here (autodie wraps chown, mkdir ...):
           the native stays reachable as CORE::name, for CORE::name(...) and for the other
           packages, which still mean the builtin */
        char buf[80];
        int n = pp_snprintf(buf, sizeof buf, "&CORE::%s", sym->name + 1);
        pp_sym *core = n < (int)sizeof buf ? pp_intern(I, buf, n) : NULL;
        if (core && !core->sub) { pp_obj *nat = sym->sub; nat->refcnt++; pp_define_sub(I, core, nat); }
    }
    pp_obj *cell = pp_global(I, sym);   /* SCALAR cell holding a CODE ref keeps it alive */
    if (cell->u.scalar.t == PP_REF && cell->u.scalar.u.o != code && is_sub_stub(cell->u.scalar.u.o)
        && code->refcnt == 1 && code->u.code.body && !code->u.code.is_const) {
        /* \&name was taken before the sub existed (*alias = \&name in a BEGIN block): the
           definition moves into that same object, so everything holding it now has the sub */
        pp_obj *stub = cell->u.scalar.u.o;
        stub->u.code = code->u.code;
        memset(&code->u.code, 0, sizeof code->u.code);
        pp_obj_decref(code);
        sym->sub = stub;
        return;
    }
    pp_release(cell->u.scalar);
    cell->u.scalar = pp_ref(code);
    pp_obj_decref(code);
    sym->sub = code;
}

static void hold(pp_interp *I, pp_obj *o) {
    /* keep o alive until the next hold (o may be NULL) */
    if (o) o->refcnt++;
    if (I->lv_hold) pp_obj_decref(I->lv_hold);
    I->lv_hold = o;
}

/* ------------------------------------------------------------------ */
/* local                                                               */
/* ------------------------------------------------------------------ */

static void local_push(pp_interp *I, pp_obj *cell) {
    if (I->nlocals == I->caplocals) { I->caplocals = I->caplocals ? I->caplocals * 2 : 8; I->locals = pp_xrealloc(I->locals, I->caplocals * sizeof *I->locals); }
    I->locals[I->nlocals].cell = cell; cell->refcnt++;
    I->locals[I->nlocals].saved = cell->u.scalar;
    I->locals[I->nlocals].key = NULL; I->locals[I->nlocals].kind = 0;
    cell->u.scalar = pp_undef();
    I->nlocals++;
}
/* local @a / local %h: the variable is empty until the scope ends. Its contents move into a
   spare object (kind 3 on the save stack) and are swapped back on the way out. */
static void local_push_aggregate(pp_interp *I, pp_obj *cell) {
    if (I->nlocals == I->caplocals) { I->caplocals = I->caplocals ? I->caplocals * 2 : 8; I->locals = pp_xrealloc(I->locals, I->caplocals * sizeof *I->locals); }
    pp_obj *spare = pp_obj_new(cell->kind);
    __typeof__(cell->u) tmp = cell->u; cell->u = spare->u; spare->u = tmp;
    I->locals[I->nlocals].cell = cell; cell->refcnt++;
    I->locals[I->nlocals].saved = pp_ref(spare); pp_obj_decref(spare);
    I->locals[I->nlocals].key = NULL; I->locals[I->nlocals].kind = 3;
    I->nlocals++;
}
/* local *name: $name, @name, %name, &name and the file handle are all set aside until the
   scope ends. Kinds on the save stack: 4 = which cell the name stands for (an alias made in
   the scope goes away), 5 = the sub, 6 = the handle; the contents of $ @ % are saved the way
   local $x / local @a do it. */
static void local_grow(pp_interp *I) {
    if (I->nlocals == I->caplocals) { I->caplocals = I->caplocals ? I->caplocals * 2 : 8; I->locals = pp_xrealloc(I->locals, I->caplocals * sizeof *I->locals); }
    memset(&I->locals[I->nlocals], 0, sizeof *I->locals);
}
static void local_push_glob(pp_interp *I, pp_sym *gs, int clear) {
    static const char sigils[] = "$@%";
    char buf[264];
    for (int i = 0; sigils[i]; i++) {
        pp_sym *s = pp_intern(I, buf, pp_snprintf(buf, sizeof buf, "%c%s", sigils[i], gs->name + 1));
        local_grow(I);
        I->locals[I->nlocals].kind = 4; I->locals[I->nlocals].sym = s; I->locals[I->nlocals].cell = s->gcell;
        if (s->gcell) s->gcell->refcnt++;
        I->nlocals++;
        if (!s->gcell || !clear) continue;
        if (sigils[i] == '$') local_push(I, s->gcell); else local_push_aggregate(I, s->gcell);
    }
    pp_sym *cs = pp_intern(I, buf, pp_snprintf(buf, sizeof buf, "&%s", gs->name + 1));
    local_grow(I);
    I->locals[I->nlocals].kind = 5; I->locals[I->nlocals].sym = cs;
    pp_obj *oldsub = pp_resolve_sub(I, cs);
    if (oldsub) {
        I->locals[I->nlocals].saved = pp_ref(oldsub);
        if (clear) {
            pp_obj *cell = pp_global(I, cs);
            pp_release(cell->u.scalar); cell->u.scalar = pp_undef();
            cs->sub = NULL;
        }
    }
    I->nlocals++;
    pp_obj *g = pp_glob_obj(I, gs);
    local_grow(I);
    I->locals[I->nlocals].kind = 6; I->locals[I->nlocals].cell = g; g->refcnt++;
    if (g->u.glob.io) {
        I->locals[I->nlocals].saved = pp_ref(g->u.glob.io);
        if (clear) { pp_obj_decref(g->u.glob.io); g->u.glob.io = NULL; }
    }
    I->nlocals++;
}
/* local $h{key} (key != NULL) or local $a[idx]: the element becomes undef until the scope ends */
static void local_push_elem(pp_interp *I, pp_obj *box, pp_str *key, int idx) {
    if (I->nlocals == I->caplocals) { I->caplocals = I->caplocals ? I->caplocals * 2 : 8; I->locals = pp_xrealloc(I->locals, I->caplocals * sizeof *I->locals); }
    int existed = 1;
    pp_val *slot;
    if (key) {
        existed = pp_hash_find(box, key->d, key->len) != NULL;
        slot = pp_hash_slot(box, key->d, key->len);
        key->refcnt++;
    } else slot = pp_array_slot(box, idx);
    if (!slot) return;
    I->locals[I->nlocals].cell = box; box->refcnt++;
    I->locals[I->nlocals].saved = *slot; *slot = pp_undef();
    I->locals[I->nlocals].key = key; I->locals[I->nlocals].idx = idx;
    I->locals[I->nlocals].kind = key ? 1 : 2; I->locals[I->nlocals].existed = (uint8_t)existed;
    I->nlocals++;
}
static void local_unwind(pp_interp *I, int height) {
    while (I->nlocals > height) {
        I->nlocals--;
        pp_obj *cell = I->locals[I->nlocals].cell;
        if (I->locals[I->nlocals].kind == 4) {               /* local *name: the name stands for its old cell again */
            I->locals[I->nlocals].sym->gcell = cell;
            if (!cell) continue;
        } else if (I->locals[I->nlocals].kind == 5) {        /* ... and names its old sub (or none) */
            pp_sym *cs = I->locals[I->nlocals].sym;
            pp_val saved = I->locals[I->nlocals].saved;
            if (saved.t == PP_REF) pp_define_sub(I, cs, saved.u.o);
            else { pp_obj *c = pp_global(I, cs); pp_release(c->u.scalar); c->u.scalar = pp_undef(); cs->sub = NULL; }
            continue;
        } else if (I->locals[I->nlocals].kind == 6) {        /* ... and its old file handle */
            pp_val saved = I->locals[I->nlocals].saved;
            if (cell->u.glob.io) pp_obj_decref(cell->u.glob.io);
            cell->u.glob.io = saved.t == PP_REF ? saved.u.o : NULL;
        } else
        if (I->locals[I->nlocals].kind == 0) {
            pp_release(cell->u.scalar);
            cell->u.scalar = I->locals[I->nlocals].saved;
        } else if (I->locals[I->nlocals].kind == 3) {       /* local @a / %h: swap the contents back */
            pp_val saved = I->locals[I->nlocals].saved;
            pp_obj *spare = saved.u.o;
            __typeof__(cell->u) tmp = cell->u; cell->u = spare->u; spare->u = tmp;
            pp_release(saved);                               /* frees what the scope left in the variable */
        } else {
            pp_str *key = I->locals[I->nlocals].key;
            pp_val saved = I->locals[I->nlocals].saved;
            if (key && !I->locals[I->nlocals].existed) {      /* the key was not there before */
                pp_val gone; if (pp_hash_delete(cell, key->d, key->len, &gone)) pp_release(gone);
                pp_release(saved);
            } else {
                pp_val *slot = key ? pp_hash_slot(cell, key->d, key->len) : pp_array_slot(cell, I->locals[I->nlocals].idx);
                if (slot) { pp_release(*slot); *slot = saved; } else pp_release(saved);
            }
            if (key) pp_str_decref(key);
        }
        pp_obj_decref(cell);
    }
}

/* ------------------------------------------------------------------ */
/* numeric helpers                                                     */
/* ------------------------------------------------------------------ */

static pp_val truth_val(int b) { return b ? pp_int(1) : pp_false(); }

static int is_magic_inc_str(pp_str *s) {
    if (s->len == 0) return 0;
    int i = 0;
    while (i < s->len && isalpha((unsigned char)s->d[i])) i++;
    while (i < s->len && isdigit((unsigned char)s->d[i])) i++;
    return i == s->len;
}
static pp_val magic_inc(pp_str *s) {
    pp_str *r = pp_str_new(s->d, s->len);
    int i = r->len - 1;
    for (;;) {
        if (i < 0) {
            char first = r->d[0];
            char add = isdigit((unsigned char)first) ? '1' : (isupper((unsigned char)first) ? 'A' : 'a');
            r = pp_str_reserve(r, 1);
            memmove(r->d + 1, r->d, r->len + 1);
            r->d[0] = add; r->len++;
            break;
        }
        char c = r->d[i];
        if (c == 'z') { r->d[i] = 'a'; i--; }
        else if (c == 'Z') { r->d[i] = 'A'; i--; }
        else if (c == '9') { r->d[i] = '0'; i--; }
        else { r->d[i] = c + 1; break; }
    }
    return pp_str_val(r);
}

static pp_val incdec(pp_val v, int delta) {
    if (delta > 0 && v.t == PP_STR && !pp_looks_like_number(v.u.s->d, v.u.s->len) && is_magic_inc_str(v.u.s)) return magic_inc(v.u.s);
    if (v.t == PP_UNDEF) return pp_int(delta);
    pp_val n = pp_numify(v);
    if (n.t == PP_INT) {
        int64_t r;
        if (!__builtin_add_overflow(n.u.i, (int64_t)delta, &r)) return pp_int(r);
        return pp_num((double)n.u.i + delta);
    }
    return pp_num(n.u.n + delta);
}

static const char *binop_name(int op) {
    switch (op) {
    case OP_ADD: return "addition (+)"; case OP_SUB: return "subtraction (-)"; case OP_MUL: return "multiplication (*)";
    case OP_DIV: return "division (/)"; case OP_MOD: return "modulus (%)"; case OP_POW: return "exponentiation (**)";
    case OP_CONCAT: return "concatenation (.) or string"; case OP_SHL: return "left bitshift (<<)"; case OP_SHR: return "right bitshift (>>)";
    case OP_BAND: return "bitwise and (&)"; case OP_BOR: return "bitwise or (|)"; case OP_BXOR: return "bitwise xor (^)";
    case OP_NUM_EQ: return "numeric eq (==)"; case OP_NUM_NE: return "numeric ne (!=)"; case OP_NUM_LT: return "numeric lt (<)";
    case OP_NUM_GT: return "numeric gt (>)"; case OP_NUM_LE: return "numeric le (<=)"; case OP_NUM_GE: return "numeric ge (>=)";
    case OP_NUM_CMP: return "numeric comparison (<=>)"; case OP_STR_EQ: return "string eq"; case OP_STR_NE: return "string ne";
    case OP_STR_LT: return "string lt"; case OP_STR_GT: return "string gt"; case OP_STR_LE: return "string le"; case OP_STR_GE: return "string ge";
    case OP_STR_CMP: return "string comparison (cmp)"; case OP_REPEAT: return "repeat (x)";
    case OP_LOGXOR: return "logical xor (^^)"; case OP_ISA: return "isa";
    default: return "operation";
    }
}
static pp_val do_binop(pp_interp *I, int op, pp_val a, pp_val b);
/* a binary operator under `use integer`: the operands are truncated to (signed) integers and
   the result is an integer; / drops the fraction, % has the sign of the left side (as in C),
   >> keeps the sign, ~0 is -1. Anything else (strings, objects) goes the normal way. */
static NOINLINE pp_val int_binop(pp_interp *I, int op, pp_val a, pp_val b) {
    if (a.t == PP_REF || b.t == PP_REF) return do_binop(I, op, a, b);
    switch (op) {
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: case OP_SHL: case OP_SHR: case OP_BAND: case OP_BOR: case OP_BXOR:
    case OP_NUM_EQ: case OP_NUM_NE: case OP_NUM_LT: case OP_NUM_GT: case OP_NUM_LE: case OP_NUM_GE: case OP_NUM_CMP: break;
    default: return do_binop(I, op, a, b);
    }
    int64_t x = pp_to_int(a), y = pp_to_int(b);
    uint64_t ux = (uint64_t)x, uy = (uint64_t)y;
    switch (op) {
    case OP_ADD: return pp_int((int64_t)(ux + uy));
    case OP_SUB: return pp_int((int64_t)(ux - uy));
    case OP_MUL: return pp_int((int64_t)(ux * uy));
    case OP_DIV:
        if (y == 0) return pp_die(I, "Illegal division by zero");
        return pp_int(y == -1 ? (int64_t)(0 - ux) : x / y);
    case OP_MOD:
        if (y == 0) return pp_die(I, "Illegal modulus zero");
        return pp_int(y == -1 ? 0 : x % y);
    case OP_SHL: return pp_int((int64_t)(ux << (y & 63)));
    case OP_SHR: return pp_int(x < 0 ? (int64_t)~(~ux >> (y & 63)) : (int64_t)(ux >> (y & 63)));
    case OP_BAND: return pp_int((int64_t)(ux & uy));
    case OP_BOR: return pp_int((int64_t)(ux | uy));
    case OP_BXOR: return pp_int((int64_t)(ux ^ uy));
    case OP_NUM_EQ: return truth_val(x == y);
    case OP_NUM_NE: return truth_val(x != y);
    case OP_NUM_LT: return truth_val(x < y);
    case OP_NUM_GT: return truth_val(x > y);
    case OP_NUM_LE: return truth_val(x <= y);
    case OP_NUM_GE: return truth_val(x >= y);
    default: return pp_int(x < y ? -1 : x > y ? 1 : 0);
    }
}
/* "$x" for a plain scalar variable node, else NULL (used in warning messages like perl's) */
static const char *var_name_of(pp_node *n) {
    if (n && n->type == N_VAR && n->sym->name[0] == '$') return n->sym->name;
    return NULL;
}
static void warn_uninit(pp_interp *I, const char *what, const char *var) {
    if (var) pp_warn(I, "Use of uninitialized value %s in %s", var, what);
    else pp_warn(I, "Use of uninitialized value in %s", what);
}
void pp_warn_uninit(pp_interp *I, const char *what, const char *var) { warn_uninit(I, what, var); }
const char *pp_var_name_of(pp_node *n) { return var_name_of(n); }
static void warn_operand(pp_interp *I, int op, pp_val v, const char *var) {
    if (op == OP_LOGXOR || op == OP_ISA) return;   /* like &&/||, no uninitialized warning */
    if (v.t == PP_UNDEF) { warn_uninit(I, binop_name(op), var); return; }
    int numeric = op != OP_CONCAT && op != OP_REPEAT && !(op >= OP_STR_EQ && op <= OP_STR_CMP);
    if (numeric && v.t == PP_STR && !pp_is_false(v) && !pp_looks_like_number(v.u.s->d, v.u.s->len) && strcmp(v.u.s->d, "0 but true") != 0)
        pp_warn(I, "Argument \"%s\" isn't numeric in %s", v.u.s->d, binop_name(op));
}

/* ------------------------------------------------------------------ */
/* operator overloading                                                */
/* ------------------------------------------------------------------ */
/* `use overload '+' => CODE` defines the sub "(+" in the class (lib/overload.pm), so a handler
   is found like any method, inheritance included. All of this is reached only for blessed
   references, and only once some `use overload` has run. */
static pp_obj *find_method(pp_interp *I, const char *cls, const char *name, int depth);
static pp_interp *ovl_I;
static uint8_t ovl_assign;       /* the operation in progress is OP= (try "(OP=" first) */

static pp_obj *ovl_find(pp_interp *I, pp_val v, const char *op) {
    if (v.t != PP_REF || !v.u.o->class) return NULL;
    char name[16];
    pp_snprintf(name, sizeof name, "(%s", op);
    return find_method(I, v.u.o->class->name, name, 0);
}
/* handler(x, y, swapped) in scalar context */
static pp_val ovl_call(pp_interp *I, pp_obj *code, pp_val x, pp_val y, int swapped) {
    pp_list args = {0};
    pp_list_push(&args, pp_copy(x)); pp_list_push(&args, pp_copy(y)); pp_list_push(&args, swapped ? pp_int(1) : pp_strn("", 0));
    code->refcnt++;
    pp_val r = pp_call(I, code, &args, CTX_SCALAR, NULL);
    pp_list_free(&args);
    pp_obj_decref(code);
    return r;
}
static int ovl_conv(pp_val v, int kind, pp_val *out) {
    pp_interp *I = ovl_I;
    if (!I || I->ctl) return 0;
    /* each conversion stands in for the others, in perl's order */
    static const char *const order[3][3] = { { "\"\"", "0+", "bool" }, { "0+", "\"\"", "bool" }, { "bool", "0+", "\"\"" } };
    const char *const *try = order[kind == 's' ? 0 : kind == 'n' ? 1 : 2];
    for (int i = 0; i < 3; i++) {
        pp_obj *code = ovl_find(I, v, try[i]);
        if (!code) continue;
        *out = ovl_call(I, code, v, pp_undef(), 0);
        if (out->t == PP_REF && out->u.o == v.u.o) { pp_release(*out); return 0; }   /* returned itself: the plain conversion */
        return 1;
    }
    return 0;
}
void pp_ovl_enable(pp_interp *I) { ovl_I = I; pp_ovl_hook = ovl_conv; }
int pp_ovl_unary(pp_interp *I, pp_val v, const char *name, pp_val *out) {
    if (!pp_ovl_hook || I->ctl) return 0;
    pp_obj *code = ovl_find(I, v, name);
    if (!code) return 0;
    *out = ovl_call(I, code, v, pp_undef(), 0);
    return 1;
}
static const char *ovl_opname(int op) {
    static const char *const names[] = {
        [OP_ADD] = "+", [OP_SUB] = "-", [OP_MUL] = "*", [OP_DIV] = "/", [OP_MOD] = "%", [OP_POW] = "**", [OP_CONCAT] = ".",
        [OP_SHL] = "<<", [OP_SHR] = ">>", [OP_BAND] = "&", [OP_BOR] = "|", [OP_BXOR] = "^",
        [OP_NUM_EQ] = "==", [OP_NUM_NE] = "!=", [OP_NUM_LT] = "<", [OP_NUM_GT] = ">", [OP_NUM_LE] = "<=", [OP_NUM_GE] = ">=", [OP_NUM_CMP] = "<=>",
        [OP_STR_EQ] = "eq", [OP_STR_NE] = "ne", [OP_STR_LT] = "lt", [OP_STR_GT] = "gt", [OP_STR_LE] = "le", [OP_STR_GE] = "ge", [OP_STR_CMP] = "cmp",
        [OP_REPEAT] = "x",
    };
    return op > 0 && op < (int)(sizeof names / sizeof *names) ? names[op] : NULL;
}
/* a op b where one side is a blessed reference: 1 with the result in *out when a handler ran */
static NOINLINE int ovl_binop(pp_interp *I, int op, pp_val a, pp_val b, pp_val *out) {
    int assign = ovl_assign; ovl_assign = 0;
    const char *name = ovl_opname(op);
    if (!name || I->ctl) return 0;
    pp_obj *code;
    if (assign) {   /* $a += $b: "(+=" when there is one */
        char an[8]; pp_snprintf(an, sizeof an, "%s=", name);
        if ((code = ovl_find(I, a, an)) != NULL) { *out = ovl_call(I, code, a, b, 0); return 1; }
    }
    if ((code = ovl_find(I, a, name)) != NULL) { *out = ovl_call(I, code, a, b, 0); return 1; }
    if ((code = ovl_find(I, b, name)) != NULL) { *out = ovl_call(I, code, b, a, 1); return 1; }
    /* comparisons come from <=> / cmp when only that one is given */
    int viacmp = op >= OP_NUM_EQ && op <= OP_NUM_GE ? OP_NUM_CMP : op >= OP_STR_EQ && op <= OP_STR_GE ? OP_STR_CMP : 0;
    if (viacmp) {
        pp_val c;
        if (!ovl_binop(I, viacmp, a, b, &c)) return 0;
        int64_t k = I->ctl ? 0 : pp_to_int(c);
        pp_release(c);
        int base = viacmp == OP_NUM_CMP ? OP_NUM_EQ : OP_STR_EQ, t;
        switch (op - base) {
        case 0: t = k == 0; break;
        case 1: t = k != 0; break;
        case 2: t = k < 0; break;
        case 3: t = k > 0; break;
        case 4: t = k <= 0; break;
        default: t = k >= 0; break;
        }
        *out = t ? pp_int(1) : pp_strn("", 0);
        return 1;
    }
    return 0;
}

static pp_val do_binop(pp_interp *I, int op, pp_val a, pp_val b) {
    if (__builtin_expect((a.t == PP_REF) | (b.t == PP_REF), 0) && pp_ovl_hook) { pp_val r; if (ovl_binop(I, op, a, b, &r)) return r; }
    switch (op) {
    case OP_CONCAT: {
        pp_str *sa = pp_to_str(a);
        pp_str *tb; const char *pb = pp_to_cstr(b, &tb);
        int lb = tb ? tb->len : b.u.s->len;
        sa = pp_str_reserve(sa, lb);
        memcpy(sa->d + sa->len, pb, lb); sa->len += lb; sa->d[sa->len] = 0;
        if (tb) pp_str_decref(tb);
        return pp_str_val(sa);
    }
    case OP_LOGXOR: return truth_val(pp_truth(a) != pp_truth(b));
    case OP_ISA: {
        int r = 0;
        if (a.t == PP_REF && a.u.o->class) {
            pp_str *tb; const char *want = pp_to_cstr(b, &tb);
            r = class_isa(I, a.u.o->class->name, want, 0);
            if (tb) pp_str_decref(tb);
        }
        return truth_val(r);
    }
    case OP_STR_EQ: return truth_val(pp_str_cmp(a, b) == 0);
    case OP_STR_NE: return truth_val(pp_str_cmp(a, b) != 0);
    case OP_STR_LT: return truth_val(pp_str_cmp(a, b) < 0);
    case OP_STR_GT: return truth_val(pp_str_cmp(a, b) > 0);
    case OP_STR_LE: return truth_val(pp_str_cmp(a, b) <= 0);
    case OP_STR_GE: return truth_val(pp_str_cmp(a, b) >= 0);
    case OP_STR_CMP: return pp_int(pp_str_cmp(a, b));
    case OP_REPEAT: {
        int64_t n = pp_to_int(b);
        pp_str *sa = pp_to_str(a);
        if (n <= 0) { pp_str_decref(sa); return pp_strn("", 0); }
        pp_str *r = pp_str_new(NULL, 0);
        r = pp_str_reserve(r, (int)(sa->len * n));
        for (int64_t i = 0; i < n; i++) { memcpy(r->d + r->len, sa->d, sa->len); r->len += sa->len; }
        r->d[r->len] = 0;
        pp_str_decref(sa);
        return pp_str_val(r);
    }
    default: break;
    }
    pp_val na = pp_numify(a), nb = pp_numify(b);
    int both_int = na.t == PP_INT && nb.t == PP_INT;
    double da = na.t == PP_INT ? (double)na.u.i : na.u.n;
    double db = nb.t == PP_INT ? (double)nb.u.i : nb.u.n;
    switch (op) {
    case OP_ADD: if (both_int) { int64_t r; if (!__builtin_add_overflow(na.u.i, nb.u.i, &r)) return pp_int(r); } return pp_num(da + db);
    case OP_SUB: if (both_int) { int64_t r; if (!__builtin_sub_overflow(na.u.i, nb.u.i, &r)) return pp_int(r); } return pp_num(da - db);
    case OP_MUL: if (both_int) { int64_t r; if (!__builtin_mul_overflow(na.u.i, nb.u.i, &r)) return pp_int(r); } return pp_num(da * db);
    case OP_DIV:
        if (db == 0) return pp_die(I, "Illegal division by zero");
        if (both_int && nb.u.i != 0 && na.u.i % nb.u.i == 0 && !(na.u.i == INT64_MIN && nb.u.i == -1)) return pp_int(na.u.i / nb.u.i);
        return pp_num(da / db);
    case OP_MOD: {
        if (!both_int && da >= 9223372036854775808.0 && db >= 1 && da < 3.7e19) {
            /* a value beyond the signed range (~0 % 10): the remainder of the unsigned integer */
            uint64_t ub = pp_to_bits(nb);
            if (ub) return pp_bits_val(pp_to_bits(na) % ub);
        }
        int64_t ia = both_int ? na.u.i : (int64_t)da, ib = both_int ? nb.u.i : (int64_t)db;
        if (ib == 0) return pp_die(I, "Illegal modulus zero");
        if (ib == -1) return pp_int(0);
        int64_t r = ia % ib;
        if (r != 0 && ((r < 0) != (ib < 0))) r += ib;
        return pp_int(r);
    }
    case OP_POW:
        /* like perl: pow() is computed in floating point (integral results still print as integers) */
        return pp_num(pow(da, db));
    case OP_SHL: return pp_bits_val(pp_to_bits(na) << (pp_to_int(nb) & 63));
    case OP_SHR: return pp_bits_val(pp_to_bits(na) >> (pp_to_int(nb) & 63));
    case OP_BAND: return pp_bits_val(pp_to_bits(na) & pp_to_bits(nb));
    case OP_BOR: return pp_bits_val(pp_to_bits(na) | pp_to_bits(nb));
    case OP_BXOR: return pp_bits_val(pp_to_bits(na) ^ pp_to_bits(nb));
    case OP_NUM_EQ: return truth_val(both_int ? na.u.i == nb.u.i : da == db);
    case OP_NUM_NE: return truth_val(both_int ? na.u.i != nb.u.i : da != db);
    case OP_NUM_LT: return truth_val(both_int ? na.u.i < nb.u.i : da < db);
    case OP_NUM_GT: return truth_val(both_int ? na.u.i > nb.u.i : da > db);
    case OP_NUM_LE: return truth_val(both_int ? na.u.i <= nb.u.i : da <= db);
    case OP_NUM_GE: return truth_val(both_int ? na.u.i >= nb.u.i : da >= db);
    case OP_NUM_CMP:
        if (both_int) return pp_int(na.u.i < nb.u.i ? -1 : na.u.i > nb.u.i ? 1 : 0);
        if (isnan(da) || isnan(db)) return pp_undef();
        return pp_int(da < db ? -1 : da > db ? 1 : 0);
    }
    return pp_die(I, "internal: bad binop %d", op);
}

/* ------------------------------------------------------------------ */
/* containers                                                          */
/* ------------------------------------------------------------------ */

static int  declare_my(pp_interp *I, pp_node *m);

/* Returns an owned reference to the array designated by node n. */
NOINLINE pp_obj *pp_get_array_raw(pp_interp *I, pp_node *n, int vivify) {
    switch (n->type) {
    case N_VAR: {
        if (n->sym->name[0] != '@') break;
        pp_obj *c = var_cell(I, n);
        c->refcnt++;
        return c;
    }
    case N_MY: {
        declare_my(I, n);
        if (n->n == 1 && n->list[0]->type == N_VAR) return pp_get_array(I, n->list[0], vivify, NULL);
        break;
    }
    case N_ANONARRAY: {
        pp_val v = pp_eval(I, n, CTX_SCALAR, NULL);
        if (v.t == PP_REF && v.u.o->kind == PP_OARRAY) return v.u.o;   /* ownership transferred */
        pp_release(v);
        break;
    }
    case N_RV2AV: {
        if (vivify) {
            pp_val *slot = lvalue_or_null(I, n->a);
            if (I->ctl) return NULL;
            if (slot) {
                if (slot->t == PP_UNDEF) { pp_obj *a = pp_obj_new(PP_OARRAY); *slot = pp_ref(a); return a; }
                if (slot->t == PP_REF && slot->u.o->kind == PP_OARRAY) { slot->u.o->refcnt++; return slot->u.o; }
                if (is_symname(*slot)) { pp_sym *s = pp_symref(I, n, *slot, '@', 1); if (!s) return NULL; pp_obj *c = pp_global(I, s); c->refcnt++; return c; }
                pp_die(I, "Not an ARRAY reference");
                return NULL;
            }
        }
        pp_val v = pp_eval(I, n->a, CTX_SCALAR, NULL);
        if (I->ctl) { pp_release(v); return NULL; }
        if (v.t == PP_REF && v.u.o->kind == PP_OARRAY) return v.u.o;
        if (v.t == PP_UNDEF) { pp_release(v); return pp_obj_new(PP_OARRAY); }   /* rvalue deref of undef: empty */
        if (is_symname(v)) { pp_sym *s = pp_symref(I, n, v, '@', 1); pp_release(v); if (!s) return NULL; pp_obj *c = pp_global(I, s); c->refcnt++; return c; }
        pp_release(v);
        pp_die(I, "Not an ARRAY reference");
        return NULL;
    }
    default: break;
    }
    if (!I->ctl) pp_die(I, "Can't use expression as an ARRAY");
    return NULL;
}

/* %Pkg:: / %main:: -- a package's name table. picoperl keeps no such table (symbols are
   looked up by their full name), so the hash is filled in from the symbols each time it is
   used: one key per name ("name" => "*Pkg::name") and one per nested package ("Sub::").
   It can be read; storing into it or deleting from it does not change the symbols. */
static pp_obj *stash_fill(pp_interp *I, pp_sym *hs) {
    pp_obj *h = pp_global(I, hs);
    size_t hl = strlen(hs->name);
    if (hl < 3 || hs->name[hl - 1] != ':' || hs->name[hl - 2] != ':') return h;
    const char *pkg = hs->name + 1;                    /* "Pkg::" */
    int main_pkg = !strcmp(pkg, "main::");
    size_t pl = main_pkg ? 0 : strlen(pkg);
    pp_hash_clear(h);
    for (int b = 0; b < PP_SYM_BUCKETS; b++)
        for (pp_sym *s = I->syms[b]; s; s = s->next) {
            if (!strchr("$@%&*", s->name[0]) || !(isalpha((unsigned char)s->name[1]) || s->name[1] == '_')) continue;
            if (s->name[0] == '&' ? !s->sub : !s->gcell) continue;
            const char *name = s->name + 1;
            if (pl && strncmp(name, pkg, pl)) continue;
            const char *rest = name + pl, *sep = strstr(rest, "::");
            if (!*rest) continue;
            int kl = sep ? (int)(sep - rest) + 2 : (int)strlen(rest);
            if (pp_hash_find(h, rest, kl)) continue;
            char buf[264];
            int n = pp_snprintf(buf, sizeof buf, "*%s%.*s", main_pkg ? "main::" : pkg, kl, rest);
            pp_hash_set(h, rest, kl, pp_strn(buf, n));
        }
    return h;
}
pp_obj *pp_stash_of(pp_interp *I, pp_sym *hs) { return stash_fill(I, hs); }
static int is_stash_name(const char *name) { size_t l = strlen(name); return l >= 3 && name[l - 1] == ':' && name[l - 2] == ':'; }

/* the array an expression names. A tied one is first filled from its object (FETCHSIZE /
   FETCH), so that everything reading the whole array works on plain data; element access
   uses pp_get_array_raw and calls FETCH / STORE for the one element. */
pp_obj *pp_get_array(pp_interp *I, pp_node *n, int vivify, pp_obj **unused) {
    (void)unused;
    pp_obj *o = pp_get_array_raw(I, n, vivify);
    if (o && o->tied) pp_tie_sync(I, o);
    return o;
}
NOINLINE pp_obj *pp_get_hash_raw(pp_interp *I, pp_node *n, int vivify) {
    switch (n->type) {
    case N_VAR: {
        if (n->sym->name[0] != '%') break;
        pp_obj *c = var_cell(I, n);
        if (!(n->flags & PP_VAR_LEX) && is_stash_name(n->sym->name)) stash_fill(I, n->sym);
        c->refcnt++;
        return c;
    }
    case N_MY: {
        declare_my(I, n);
        if (n->n == 1 && n->list[0]->type == N_VAR) return pp_get_hash(I, n->list[0], vivify, NULL);
        break;
    }
    case N_ANONHASH: {
        pp_val v = pp_eval(I, n, CTX_SCALAR, NULL);
        if (v.t == PP_REF && v.u.o->kind == PP_OHASH) return v.u.o;
        pp_release(v);
        break;
    }
    case N_RV2HV: {
        if (vivify) {
            pp_val *slot = lvalue_or_null(I, n->a);
            if (I->ctl) return NULL;
            if (slot) {
                if (slot->t == PP_UNDEF) { pp_obj *h = pp_obj_new(PP_OHASH); *slot = pp_ref(h); return h; }
                if (slot->t == PP_REF && slot->u.o->kind == PP_OHASH) { slot->u.o->refcnt++; return slot->u.o; }
                if (is_symname(*slot)) { pp_sym *s = pp_symref(I, n, *slot, '%', 1); if (!s) return NULL; pp_obj *c = is_stash_name(s->name) ? stash_fill(I, s) : pp_global(I, s); c->refcnt++; return c; }
                pp_die(I, "Not a HASH reference");
                return NULL;
            }
        }
        pp_val v = pp_eval(I, n->a, CTX_SCALAR, NULL);
        if (I->ctl) { pp_release(v); return NULL; }
        if (v.t == PP_REF && v.u.o->kind == PP_OHASH) return v.u.o;
        if (v.t == PP_UNDEF) { pp_release(v); return pp_obj_new(PP_OHASH); }
        if (is_symname(v)) { pp_sym *s = pp_symref(I, n, v, '%', 1); pp_release(v); if (!s) return NULL; pp_obj *c = is_stash_name(s->name) ? stash_fill(I, s) : pp_global(I, s); c->refcnt++; return c; }
        pp_release(v);
        pp_die(I, "Not a HASH reference");
        return NULL;
    }
    default: break;
    }
    if (!I->ctl) pp_die(I, "Can't use expression as a HASH");
    return NULL;
}

static pp_str *eval_key(pp_interp *I, pp_node *n) {
    pp_val k = pp_eval(I, n, CTX_SCALAR, NULL);
    if (I->ctl) { pp_release(k); return NULL; }
    pp_str *s = pp_to_str(k);
    pp_release(k);
    return s;
}

/* Returns a pointer to a scalar slot, or NULL (with ctl set on error). */
/* the slot of a reference-holding expression, or NULL when the expression cannot be assigned to
   (@{"name"}, @{ $pkg . "::ISA" }, @{ f() }): those are evaluated as plain values instead */
/* ------------------------------------------------------------------ */
/* tied variables                                                      */
/* ------------------------------------------------------------------ */
/* Reads call FETCH on the spot. Writes go through pp_lvalue(), whose callers store into the
   slot they get; for a tied variable that slot is remembered here and STORE is called once
   the write has happened: after the assignment, at the end of the statement at the latest,
   and before the next FETCH. */
struct pp_tiepend { struct pp_tiepend *next; pp_obj *var; pp_val key, val; uint8_t keyed; };

static pp_val tie_var_call(pp_interp *I, pp_obj *var, const char *m, pp_list *args, int ctx, pp_list *out) {
    pp_val *t = pp_tie_find(var);
    if (!t) { if (args) pp_list_free(args); return pp_undef(); }
    pp_val o = pp_copy(*t);
    /* while its own method runs, the variable is plain: STORE may assign to it (as in perl) */
    uint8_t was = var->tied;
    var->tied = 2; var->refcnt++;
    pp_val r = pp_tie_call(I, o, m, args, ctx, out, 1);
    if (var->tied == 2) var->tied = was;
    pp_obj_decref(var);
    pp_release(o);
    return r;
}
void pp_tie_flush(pp_interp *I) {
    struct pp_tiepend *p = I->tie_pend, *rev = NULL;
    I->tie_pend = NULL;
    while (p) { struct pp_tiepend *nx = p->next; p->next = rev; rev = p; p = nx; }   /* oldest first */
    while (rev) {
        p = rev; rev = p->next;
        if (!I->ctl && p->var->tied == 1) {
            pp_list a = {0};
            if (p->keyed) pp_list_push(&a, pp_copy(p->key));
            pp_list_push(&a, pp_copy(p->keyed ? p->val : p->var->u.scalar));
            pp_release(tie_var_call(I, p->var, "STORE", &a, CTX_SCALAR, NULL));
        }
        pp_release(p->key); pp_release(p->val);
        pp_obj_decref(p->var);
        free(p);
    }
}
static int tie_key_eq(pp_val a, pp_val b) {
    if (a.t != b.t) return 0;
    if (a.t == PP_INT) return a.u.i == b.u.i;
    return a.t == PP_STR && a.u.s->len == b.u.s->len && !memcmp(a.u.s->d, b.u.s->d, (size_t)a.u.s->len);
}
/* forget the newest owed STORE (the lvalue was looked at but not written) */
static void tie_unowe(pp_interp *I) {
    struct pp_tiepend *p = I->tie_pend;
    I->tie_pend = p->next;
    pp_release(p->key); pp_release(p->val);
    pp_obj_decref(p->var);
    free(p);
}
/* the slot to write for tied `var` (keyed: its element `key`, which is consumed) */
static NOINLINE pp_val *tie_lv(pp_interp *I, pp_obj *var, pp_val key, int keyed) {
    int noread = I->tie_noread;
    I->tie_noread = 0;
    for (struct pp_tiepend *p = I->tie_pend; p; p = p->next)
        if (p->var == var && p->keyed == keyed && (!keyed || tie_key_eq(p->key, key))) { pp_release(key); return keyed ? &p->val : &var->u.scalar; }
    pp_val cur = pp_undef();
    if (!noread) {   /* .= ++ s/// and friends start from the current value */
        pp_list a = {0};
        if (keyed) pp_list_push(&a, pp_copy(key));
        cur = tie_var_call(I, var, "FETCH", &a, CTX_SCALAR, NULL);
        if (I->ctl) { pp_release(cur); pp_release(key); return NULL; }
    }
    struct pp_tiepend *p = pp_xmalloc(sizeof *p);
    p->var = var; var->refcnt++;
    p->key = key; p->keyed = (uint8_t)keyed; p->val = pp_undef();
    if (keyed) p->val = cur;
    else if (!noread) { pp_release(var->u.scalar); var->u.scalar = cur; }
    p->next = I->tie_pend; I->tie_pend = p;
    return keyed ? &p->val : &var->u.scalar;
}
/* read a tied scalar: FETCH (the value is also left in the cell) */
static NOINLINE pp_val tie_fetch_scalar(pp_interp *I, pp_obj *c) {
    if (I->tie_pend) pp_tie_flush(I);
    pp_val v = tie_var_call(I, c, "FETCH", NULL, CTX_SCALAR, NULL);
    if (I->ctl) { pp_release(v); return pp_undef(); }
    pp_release(c->u.scalar); c->u.scalar = pp_copy(v);
    return v;
}

pp_obj *pp_get_hash(pp_interp *I, pp_node *n, int vivify, pp_obj **unused) {
    (void)unused;
    pp_obj *o = pp_get_hash_raw(I, n, vivify);
    if (o && o->tied) pp_tie_sync(I, o);
    return o;
}
/* fill a tied array / hash from its object */
NOINLINE void pp_tie_sync(pp_interp *I, pp_obj *o) {
    if (o->tied != 1 || I->ctl) return;
    if (I->tie_pend) pp_tie_flush(I);
    o->tied = 2;                        /* being filled: a nested read uses what is there */
    o->refcnt++;
    if (o->kind == PP_OARRAY) {
        pp_val nv = tie_var_call(I, o, "FETCHSIZE", NULL, CTX_SCALAR, NULL);
        int64_t n = I->ctl ? 0 : pp_to_int(nv); pp_release(nv);
        if (!I->ctl) pp_array_clear(o);
        for (int64_t i = 0; i < n && !I->ctl; i++) {
            pp_list a = {0}; pp_list_push(&a, pp_int(i));
            pp_val v = tie_var_call(I, o, "FETCH", &a, CTX_SCALAR, NULL);
            if (I->ctl) { pp_release(v); break; }
            pp_array_push(o, v);
        }
    } else if (o->kind == PP_OHASH) {
        pp_val k = tie_var_call(I, o, "FIRSTKEY", NULL, CTX_SCALAR, NULL);
        if (!I->ctl) pp_hash_clear(o);
        while (!I->ctl && k.t != PP_UNDEF) {
            pp_str *ks = pp_to_str(k);
            pp_list a = {0}; pp_list_push(&a, pp_copy(k));
            pp_val v = tie_var_call(I, o, "FETCH", &a, CTX_SCALAR, NULL);
            if (!I->ctl) pp_hash_set(o, ks->d, ks->len, v); else pp_release(v);
            pp_str_decref(ks);
            if (I->ctl) break;
            pp_list b = {0}; pp_list_push(&b, k);
            k = tie_var_call(I, o, "NEXTKEY", &b, CTX_SCALAR, NULL);
        }
        pp_release(k);
    }
    if (o->tied == 2) o->tied = 1;
    pp_obj_decref(o);
}
/* a tied array / hash was rewritten as a whole (list assignment, push, splice ...): hand the
   new contents to its object with CLEAR and STORE */
void pp_tie_writeback(pp_interp *I, pp_obj *o) {
    if (o->tied != 1 || I->ctl) return;
    o->tied = 2;
    o->refcnt++;
    pp_release(tie_var_call(I, o, "CLEAR", NULL, CTX_SCALAR, NULL));
    if (o->kind == PP_OARRAY) {
        for (int i = 0; i < o->u.arr.n && !I->ctl; i++) {
            pp_list a = {0}; pp_list_push(&a, pp_int(i)); pp_list_push(&a, pp_copy(o->u.arr.v[i]));
            pp_release(tie_var_call(I, o, "STORE", &a, CTX_SCALAR, NULL));
        }
    } else {
        for (int i = 0; i < o->u.hash.n && !I->ctl; i++) if (o->u.hash.e[i].klen >= 0) {
            pp_list a = {0}; pp_list_push(&a, pp_strn(o->u.hash.e[i].key, o->u.hash.e[i].klen)); pp_list_push(&a, pp_copy(o->u.hash.e[i].v));
            pp_release(tie_var_call(I, o, "STORE", &a, CTX_SCALAR, NULL));
        }
    }
    if (o->tied == 2) o->tied = 1;
    pp_obj_decref(o);
}
/* one element of a tied array / hash: FETCH */
static NOINLINE pp_val tie_fetch_elem(pp_interp *I, pp_obj *o, pp_val key) {
    if (I->tie_pend) pp_tie_flush(I);
    pp_list a = {0}; pp_list_push(&a, key);
    return tie_var_call(I, o, "FETCH", &a, CTX_SCALAR, NULL);
}

/* one element of a tied array / hash: STORE (key and value are consumed) */
static void tie_store_elem(pp_interp *I, pp_obj *o, pp_val key, pp_val val) {
    pp_list a = {0}; pp_list_push(&a, key); pp_list_push(&a, val);
    pp_release(tie_var_call(I, o, "STORE", &a, CTX_SCALAR, NULL));
}

static pp_val *lvalue_or_null(pp_interp *I, pp_node *n) {
    switch (n->type) {
    case N_VAR: case N_MY: case N_AELEM: case N_HELEM: case N_RV2SV: case N_TERNARY: case N_ASSIGN:
    case N_LOGOR: case N_LOGAND: case N_DEFOR: case N_PREINC: case N_PREDEC:
        return pp_lvalue(I, n);
    default: return NULL;
    }
}
pp_val *pp_lvalue(pp_interp *I, pp_node *n) {
    switch (n->type) {
    case N_VAR:
        if (n->sym->name[0] == '$') { pp_obj *c = var_cell(I, n); return c->tied == 1 ? tie_lv(I, c, pp_undef(), 0) : &c->u.scalar; }
        break;
    case N_MY:
        declare_my(I, n);
        if (n->n == 1 && n->list[0]->type == N_VAR) return pp_lvalue(I, n->list[0]);
        break;
    case N_LIST:
        if (n->n == 1) return pp_lvalue(I, n->list[0]);
        break;
    case N_AELEM: {
        pp_val iv = pp_eval(I, n->b, CTX_SCALAR, NULL);
        if (I->ctl) { pp_release(iv); return NULL; }
        int64_t idx = pp_to_int(iv); pp_release(iv);
        pp_obj *arr = pp_get_array_raw(I, n->a, 1);
        if (!arr) return NULL;
        hold(I, arr); pp_obj_decref(arr);
        if (arr->tied == 1) return tie_lv(I, arr, pp_int(idx), 1);
        pp_val *slot = pp_array_slot(arr, (int)idx);
        if (!slot) { pp_die(I, "Modification of non-creatable array value attempted, subscript %lld", (long long)idx); return NULL; }
        return slot;
    }
    case N_HELEM: {
        pp_str *k = eval_key(I, n->b);
        if (!k) return NULL;
        pp_obj *h = pp_get_hash_raw(I, n->a, 1);
        if (!h) { pp_str_decref(k); return NULL; }
        hold(I, h); pp_obj_decref(h);
        if (h->tied == 1) return tie_lv(I, h, (pp_val){ .t = PP_STR, .u.s = k }, 1);
        pp_val *slot = pp_hash_slot(h, k->d, k->len);
        pp_str_decref(k);
        return slot;
    }
    case N_RV2SV: {
        pp_val *inner = lvalue_or_null(I, n->a);
        if (I->ctl) return NULL;
        if (inner) {
            if (inner->t == PP_UNDEF) { pp_obj *s = pp_obj_new(PP_OSCALAR); *inner = pp_ref(s); pp_obj_decref(s); return &s->u.scalar; }
            if (inner->t == PP_REF && inner->u.o->kind == PP_OSCALAR) return inner->u.o->tied == 1 ? tie_lv(I, inner->u.o, pp_undef(), 0) : &inner->u.o->u.scalar;
            if (is_symname(*inner)) { pp_sym *s = pp_symref(I, n, *inner, '$', 1); return s ? &pp_global(I, s)->u.scalar : NULL; }
            pp_die(I, "Not a SCALAR reference");
            return NULL;
        }
        pp_val v = pp_eval(I, n->a, CTX_SCALAR, NULL);
        if (I->ctl) { pp_release(v); return NULL; }
        if (v.t == PP_REF && v.u.o->kind == PP_OSCALAR) { hold(I, v.u.o); pp_release(v); return I->lv_hold->tied == 1 ? tie_lv(I, I->lv_hold, pp_undef(), 0) : &I->lv_hold->u.scalar; }
        if (is_symname(v)) { pp_sym *s = pp_symref(I, n, v, '$', 1); pp_release(v); return s ? &pp_global(I, s)->u.scalar : NULL; }
        pp_release(v);
        pp_die(I, "Not a SCALAR reference");
        return NULL;
    }
    case N_TERNARY: {
        pp_val c = pp_eval(I, n->a, CTX_SCALAR, NULL);
        if (I->ctl) { pp_release(c); return NULL; }
        int t = pp_truth(c); pp_release(c);
        return pp_lvalue(I, t ? n->b : n->c);
    }
    case N_ASSIGN: {
        pp_val v = pp_eval(I, n, CTX_VOID, NULL);
        pp_release(v);
        if (I->ctl) return NULL;
        pp_node *t = n->a;
        if (t->type == N_MY && t->n == 1 && t->list[0]->type == N_VAR) t = t->list[0];   /* (my $x = ...) =~ s///: already declared */
        return pp_lvalue(I, t);
    }
    case N_LOGOR: case N_LOGAND: case N_DEFOR: {
        pp_val v = pp_eval(I, n->a, CTX_SCALAR, NULL);
        if (I->ctl) { pp_release(v); return NULL; }
        int pick_a = n->type == N_LOGOR ? pp_truth(v) : n->type == N_LOGAND ? !pp_truth(v) : v.t != PP_UNDEF;
        pp_release(v);
        return pp_lvalue(I, pick_a ? n->a : n->b);
    }
    case N_PREINC: case N_PREDEC: {
        pp_val v = pp_eval(I, n, CTX_VOID, NULL); pp_release(v);
        if (I->ctl) return NULL;
        return pp_lvalue(I, n->a);
    }
    default: break;
    }
    if (!I->ctl) pp_die(I, "Can't modify non-lvalue expression");
    return NULL;
}

/* declares the variables of an N_MY node; returns 1 only for `state` vars that
 * were all initialized on an earlier call (the caller then skips the initializer) */
static int declare_my(pp_interp *I, pp_node *m) {
    int existed = m->op == 's' && m->n > 0;
    for (int i = 0; i < m->n; i++) {
        pp_node *v = m->list[i];
        if (v->type != N_VAR) continue;
        if (m->op == 'm' || (m->op == 's' && !I->cur_code)) {   /* file-level state: the file runs once */
            if (!(v->flags & PP_VAR_LEX)) continue;
            pp_env *e = I->env;
            for (int d = v->aux; d > 0; d--) e = e->parent;
            /* a fresh variable each time `my` runs. Not in the file scope, where it runs once
               and a BEGIN block may have given the variable its value already. */
            if (e->parent) pp_env_reset(e, v->op, v->sym->name[0]);
            existed = 0;
        }
        else if (m->op == 's') {
            /* state: the cell lives on the code object (per closure instance) in the
             * otherwise unused constval slot, as a PP_OARRAY of refs indexed by m->aux+i */
            pp_obj *code = I->cur_code;
            if (code->u.code.constval.t != PP_REF) {
                pp_obj *arr = pp_obj_new(PP_OARRAY);
                code->u.code.constval = pp_ref(arr);
                pp_obj_decref(arr);
            }
            pp_val *slot = pp_array_slot(code->u.code.constval.u.o, m->aux + i);
            pp_obj *cell;
            if (slot->t == PP_REF) cell = slot->u.o;
            else {
                char sg = v->sym->name[0];
                cell = pp_obj_new(sg == '@' ? PP_OARRAY : sg == '%' ? PP_OHASH : PP_OSCALAR);
                *slot = pp_ref(cell);
                pp_obj_decref(cell);
                existed = 0;
            }
            if (!(v->flags & PP_VAR_LEX)) continue;
            pp_env *e = I->env;
            for (int d = v->aux; d > 0; d--) e = e->parent;
            if (e->cells[v->op] != cell) {
                if (e->cells[v->op]) pp_obj_decref(e->cells[v->op]);
                e->cells[v->op] = cell;
                pp_obj_incref(cell);
            }
        }
        else if (m->op == 'o') pp_global(I, v->sym);
        else if (m->op == 'l') {
            if (v->sym->name[0] == '$') local_push(I, var_cell(I, v));
            else local_push_aggregate(I, var_cell(I, v));
        }
    }
    return existed;
}

/* ------------------------------------------------------------------ */
/* assignment                                                          */
/* ------------------------------------------------------------------ */

static int is_list_lhs(pp_node *a) {
    switch (a->type) {
    case N_VAR: return a->sym->name[0] == '@' || a->sym->name[0] == '%';
    case N_MY: return (a->flags & 1) || (a->n == 1 && a->list[0]->type == N_VAR && a->list[0]->sym->name[0] != '$');
    case N_LIST: return 1;
    case N_LOCALELEM: return a->a->type == N_HSLICE || a->a->type == N_ASLICE;
    case N_RV2AV: case N_RV2HV: case N_ASLICE: case N_HSLICE: return 1;
    default: return 0;
    }
}

static void assign_array(pp_interp *I, pp_obj *arr, pp_list *src, int from) {
    pp_array_clear(arr);
    for (int i = from; i < src->n; i++) pp_array_push(arr, pp_copy(src->v[i]));
    if (arr->tied) pp_tie_writeback(I, arr);
}
static void assign_hash(pp_interp *I, pp_obj *h, pp_list *src, int from) {
    if (I->warn_on && ((src->n - from) & 1)) pp_warn(I, "Odd number of elements in hash assignment");
    pp_hash_clear(h);
    for (int i = from; i < src->n; i += 2) {
        pp_str *k = pp_to_str(src->v[i]);
        pp_val v = i + 1 < src->n ? pp_copy(src->v[i + 1]) : pp_undef();
        pp_hash_set(h, k->d, k->len, v);
        pp_str_decref(k);
    }
    if (h->tied) pp_tie_writeback(I, h);
}

/* assign values (owned by src) to the list lvalue a */
static void assign_list(pp_interp *I, pp_node *a, pp_list *src) {
    int pos = 0;
    pp_node **targets; int nt;
    pp_node *single[1];
    if (a->type == N_LIST || (a->type == N_MY && (a->flags & 1))) { targets = a->list; nt = a->n; }
    else { single[0] = a; targets = single; nt = 1; }
    if (a->type == N_MY) declare_my(I, a);
    for (int i = 0; i < nt && !I->ctl; i++) {
        pp_node *t = targets[i];
        if (t->type == N_MY && !(t->flags & 1)) { declare_my(I, t); t = t->list[0]; }
        if (t->type == N_NOP) { pos++; continue; }
        if (t->type == N_VAR && t->sym->name[0] == '@') { assign_array(I, var_cell(I, t), src, pos); pos = src->n; continue; }
        if (t->type == N_VAR && t->sym->name[0] == '%') { assign_hash(I, var_cell(I, t), src, pos); pos = src->n; continue; }
        if (t->type == N_RV2AV) { pp_obj *arr = pp_get_array(I, t, 1, NULL); if (arr) { assign_array(I, arr, src, pos); pp_obj_decref(arr); } pos = src->n; continue; }
        if (t->type == N_RV2HV) { pp_obj *h = pp_get_hash(I, t, 1, NULL); if (h) { assign_hash(I, h, src, pos); pp_obj_decref(h); } pos = src->n; continue; }
        if (t->type == N_ASLICE || t->type == N_HSLICE) {
            pp_list idx = {0};
            pp_eval(I, t->b, CTX_LIST, &idx);
            if (I->ctl) { pp_list_free(&idx); return; }
            if (t->type == N_ASLICE) {
                pp_obj *arr = pp_get_array(I, t->a, 1, NULL);
                if (arr) {
                    for (int k = 0; k < idx.n; k++) {
                        pp_val nv = pos < src->n ? pp_copy(src->v[pos++]) : pp_undef();
                        if (arr->tied == 1) tie_store_elem(I, arr, pp_int(pp_to_int(idx.v[k])), nv);
                        else pp_array_set(arr, (int)pp_to_int(idx.v[k]), nv);
                    }
                    pp_obj_decref(arr);
                }
            } else {
                pp_obj *h = pp_get_hash(I, t->a, 1, NULL);
                if (h) {
                    for (int k = 0; k < idx.n; k++) {
                        pp_str *ks = pp_to_str(idx.v[k]);
                        pp_val nv = pos < src->n ? pp_copy(src->v[pos++]) : pp_undef();
                        if (h->tied == 1) { ks->refcnt++; tie_store_elem(I, h, (pp_val){ .t = PP_STR, .u.s = ks }, nv); }
                        else pp_hash_set(h, ks->d, ks->len, nv);
                        pp_str_decref(ks);
                    }
                    pp_obj_decref(h);
                }
            }
            pp_list_free(&idx);
            continue;
        }
        pp_val v = pos < src->n ? pp_copy(src->v[pos]) : pp_undef();
        pos++;
        I->tie_noread = 1;
        pp_val *slot = pp_lvalue(I, t);
        I->tie_noread = 0;
        if (!slot) { pp_release(v); return; }
        pp_release(*slot);
        *slot = v;
    }
}

/* local on an element or a slice: save the elements and leave them undef */
static NOINLINE void eval_local_elem(pp_interp *I, pp_node *n) {
    pp_node *t = n->a;
    int is_hash = t->type == N_HELEM || t->type == N_HSLICE;
    pp_obj *box = is_hash ? pp_get_hash(I, t->a, 1, NULL) : pp_get_array(I, t->a, 1, NULL);
    if (!box || I->ctl) { if (box) pp_obj_decref(box); return; }
    pp_list keys = {0};
    if (t->type == N_HELEM) { pp_str *k = eval_key(I, t->b); if (k) pp_list_push(&keys, (pp_val){ .t = PP_STR, .u.s = k }); }
    else if (t->type == N_AELEM) pp_list_push(&keys, pp_eval(I, t->b, CTX_SCALAR, NULL));
    else pp_eval(I, t->b, CTX_LIST, &keys);
    for (int i = 0; i < keys.n && !I->ctl; i++) {
        if (is_hash) { pp_str *k = pp_to_str(keys.v[i]); local_push_elem(I, box, k, 0); pp_str_decref(k); }
        else {
            int64_t idx = pp_to_int(keys.v[i]);
            if (idx < 0) idx += box->u.arr.n;
            if (idx >= 0) local_push_elem(I, box, NULL, (int)idx);
        }
    }
    pp_list_free(&keys);
    pp_obj_decref(box);
}

static NOINLINE pp_val eval_assign(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_node *a = n->a, *b = n->b;
    if (a->type == N_GLOB && n->op == OP_NONE) return eval_glob_assign(I, n);
    if (a->type == N_LIST && n->op == OP_NONE && a->n <= 32) {
        /* local($x, $h{k}) = LIST: localize the elements, then assign to the list with the
           elements themselves in their places */
        int has = 0;
        for (int i = 0; i < a->n; i++) if (a->list[i]->type == N_LOCALELEM) has = 1;
        if (has) {
            /* the right side is evaluated before anything is localized: local($h{k}) = $h{k} */
            pp_list src = {0};
            pp_eval(I, b, CTX_LIST, &src);
            if (I->ctl) { pp_list_free(&src); return pp_undef(); }
            int count = src.n;
            pp_node *items[32];
            for (int i = 0; i < a->n; i++) {
                pp_node *c = a->list[i];
                if (c->type == N_LOCALELEM) { eval_local_elem(I, c); if (I->ctl) { pp_list_free(&src); return pp_undef(); } c = c->a; }
                items[i] = c;
            }
            pp_node tl = *a; tl.list = items; tl.flags &= ~PP_NODE_HEAP;
            assign_list(I, &tl, &src);
            if (ctx == CTX_LIST && out && !I->ctl) pp_eval(I, &tl, CTX_LIST, out);
            pp_list_free(&src);
            return pp_int(count);
        }
    }
    if (a->type == N_LOCALGLOB) {   /* local *name = v */
        pp_sym *gs = glob_sym(I, a->a);
        if (!gs) return pp_undef();
        /* local *name = \&code replaces one slot and keeps the others visible; local *name = *other
           (like a plain local *name) starts from an empty glob */
        pp_val v = pp_eval(I, b, CTX_SCALAR, NULL);
        if (I->ctl) { pp_release(v); return pp_undef(); }
        local_push_glob(I, gs, !(v.t == PP_REF && !is_globref(v)));
        pp_node cb; memset(&cb, 0, sizeof cb); cb.type = N_CONST; cb.val = v; cb.line = n->line;
        pp_node tmp = *n; tmp.a = a->a; tmp.b = &cb; tmp.flags &= ~PP_NODE_HEAP;
        pp_val r = eval_assign(I, &tmp, ctx, out);
        pp_release(v);
        return r;
    }
    if (a->type == N_LOCALELEM) {   /* local $h{k} = v: evaluate v, localize, then assign to the element itself */
        if (n->op == OP_NONE) {
            int lst = is_list_lhs(a);
            pp_list src = {0};
            if (lst) pp_eval(I, b, CTX_LIST, &src); else pp_list_push(&src, pp_eval(I, b, CTX_SCALAR, NULL));
            if (I->ctl) { pp_list_free(&src); return pp_undef(); }
            int count = src.n;
            eval_local_elem(I, a);
            if (I->ctl) { pp_list_free(&src); return pp_undef(); }
            if (lst) {
                assign_list(I, a->a, &src);
                if (ctx == CTX_LIST && out && !I->ctl) pp_eval(I, a->a, CTX_LIST, out);
                pp_list_free(&src);
                return pp_int(count);
            }
            pp_node kn; memset(&kn, 0, sizeof kn);
            kn.type = N_CONST; kn.val = src.v[0]; kn.line = n->line;
            pp_node tmp = *n; tmp.a = a->a; tmp.b = &kn; tmp.flags &= ~PP_NODE_HEAP;
            pp_val r = eval_assign(I, &tmp, ctx, out);
            pp_list_free(&src);
            return r;
        }
        eval_local_elem(I, a);
        if (I->ctl) return pp_undef();
        pp_node tmp = *n; tmp.a = a->a; tmp.flags &= ~PP_NODE_HEAP;
        return eval_assign(I, &tmp, ctx, out);
    }
    if (a->type == N_MY && a->op == 's' && n->op == OP_NONE && declare_my(I, a)) {
        /* state vars already initialized on an earlier call: skip the initializer */
        if (ctx == CTX_LIST && out) { pp_eval(I, a, CTX_LIST, out); return pp_undef(); }
        if (ctx == CTX_SCALAR && a->n == 1 && a->list[0]->type == N_VAR) return pp_eval(I, a->list[0], CTX_SCALAR, NULL);
        return pp_undef();
    }
    if (n->op == OP_NONE && is_list_lhs(a)) {
        pp_list src = {0};
        pp_eval(I, b, CTX_LIST, &src);
        if (I->ctl) { pp_list_free(&src); return pp_undef(); }
        int count = src.n;
        assign_list(I, a, &src);
        if (ctx == CTX_LIST && out && !I->ctl) {
            if (a->type == N_MY) {   /* (my @l = LIST) as a list: the variables just assigned, not a new declaration */
                pp_node tl = *a; tl.type = N_LIST; tl.op = 0; tl.flags = 0;
                pp_eval(I, &tl, CTX_LIST, out);
            }
            else pp_eval(I, a, CTX_LIST, out);
        }
        pp_list_free(&src);
        return pp_int(count);
    }
    if (n->op == OP_LOGOR || n->op == OP_LOGAND || n->op == OP_DEFOR) {
        struct pp_tiepend *owed = I->tie_pend;
        pp_val *slot = pp_lvalue(I, a);
        if (!slot) return pp_undef();
        int keep = n->op == OP_LOGOR ? pp_truth(*slot) : n->op == OP_LOGAND ? !pp_truth(*slot) : slot->t != PP_UNDEF;
        if (keep) {
            pp_val kept = ctx == CTX_VOID ? pp_undef() : pp_copy(*slot);
            if (I->tie_pend != owed) tie_unowe(I);   /* nothing was written: a tied variable gets no STORE */
            return kept;
        }
        pp_val v = pp_eval(I, b, CTX_SCALAR, NULL);
        if (I->ctl) { pp_release(v); return pp_undef(); }
        slot = pp_lvalue(I, a);
        if (!slot) { pp_release(v); return pp_undef(); }
        pp_release(*slot); *slot = v;
        return ctx == CTX_VOID ? pp_undef() : pp_copy(v);
    }
    if (n->op == OP_REPEAT && a->type == N_VAR && a->sym->name[0] == '@') {
        /* @a x= n */
        pp_val cnt = pp_eval(I, b, CTX_SCALAR, NULL);
        if (I->ctl) return pp_undef();
        int64_t k = pp_to_int(cnt); pp_release(cnt);
        pp_obj *arr = var_cell(I, a);
        int len = arr->u.arr.n;
        if (k <= 0) pp_array_clear(arr);
        else for (int64_t r = 1; r < k; r++) for (int i = 0; i < len; i++) pp_array_push(arr, pp_copy(arr->u.arr.v[i]));
        return pp_int(arr->u.arr.n);
    }
    if (n->op != OP_NONE && is_list_lhs(a)) return pp_die(I, "Can't modify array/hash in compound assignment");
    pp_val v = pp_eval(I, b, CTX_SCALAR, NULL);
    if (I->ctl) { pp_release(v); return pp_undef(); }
    I->tie_noread = n->op == OP_NONE;
    pp_val *slot = pp_lvalue(I, a);
    I->tie_noread = 0;
    if (!slot) { pp_release(v); return pp_undef(); }
    if (n->op != OP_NONE) {
        if (I->warn_on) {
            /* like perl, .= and += on undef do not warn about the left side */
            if (n->op != OP_CONCAT && n->op != OP_ADD && n->op != OP_SUB && n->op != OP_BOR && n->op != OP_BXOR) warn_operand(I, n->op, *slot, var_name_of(a));
            else if (slot->t == PP_STR && n->op != OP_CONCAT) warn_operand(I, n->op, *slot, var_name_of(a));
            warn_operand(I, n->op, v, var_name_of(b));
        }
        if (slot->t == PP_REF) ovl_assign = pp_ovl_hook != NULL;   /* ovl_binop takes the note and clears it */
        pp_val r = n->aux ? int_binop(I, n->op, *slot, v) : do_binop(I, n->op, *slot, v);
        pp_release(v);
        if (I->ctl) { pp_release(r); return pp_undef(); }
        v = r;
        slot = pp_lvalue(I, a);
        if (!slot) { pp_release(v); return pp_undef(); }
    }
    pp_release(*slot);
    *slot = v;
    if (I->tie_pend) {
        /* a tied target: STORE now; the value of the assignment is what FETCH then gives */
        v = pp_copy(v);
        pp_tie_flush(I);
        if (ctx == CTX_VOID || I->ctl) { pp_release(v); return pp_undef(); }
        if (a->type == N_VAR && a->sym->name[0] == '$' && var_cell(I, a)->tied == 1) { pp_release(v); return pp_eval(I, a, CTX_SCALAR, NULL); }
        return v;
    }
    if (ctx == CTX_VOID) return pp_undef();
    return pp_copy(v);
}

/* ------------------------------------------------------------------ */
/* calls                                                               */
/* ------------------------------------------------------------------ */

static pp_val exec_block(pp_interp *I, pp_node *blk, int ctx, pp_list *out);

/* AUTOLOAD: the sub that stands in for an undefined one of its package. full is the name that
   was called ("Pkg::name", main's subs without a package); on a hit $Pkg::AUTOLOAD is set to
   the full name, as the sub expects. NULL when the package has no AUTOLOAD. */
static NOINLINE pp_obj *autoload_sub(pp_interp *I, const char *full) {
    const char *last = NULL;
    for (const char *p = full; *p; p++) if (p[0] == ':' && p[1] == ':') last = p;
    const char *base = last ? last + 2 : full;
    if (!strcmp(base, "AUTOLOAD") || !strcmp(base, "DESTROY")) return NULL;
    char buf[280];
    int plen = last ? (int)(last - full) : 0;
    if (plen > 200) return NULL;
    int n = plen && !(plen == 4 && !memcmp(full, "main", 4)) ? pp_snprintf(buf, sizeof buf, "&%.*s::AUTOLOAD", plen, full) : pp_snprintf(buf, sizeof buf, "&AUTOLOAD");
    pp_obj *code = pp_resolve_sub(I, pp_intern(I, buf, n));
    if (!code || (!code->u.code.body && !code->u.code.native)) return NULL;
    buf[0] = '$';
    pp_obj *var = pp_global(I, pp_intern(I, buf, n));
    char name[280];
    int nl = pp_snprintf(name, sizeof name, "%s%s", last ? "" : "main::", full);
    pp_release(var->u.scalar);
    var->u.scalar = pp_strn(name, nl);
    return code;
}
/* the same for a method: AUTOLOAD is looked up like a method of cls (through @ISA), and
   $AUTOLOAD of the package it lives in is set to "cls::method" */
static pp_obj *find_method(pp_interp *I, const char *cls, const char *name, int depth);
static NOINLINE pp_obj *autoload_method(pp_interp *I, const char *cls, const char *mname) {
    if (!strcmp(mname, "AUTOLOAD") || !strcmp(mname, "DESTROY") || strstr(mname, "::")) return NULL;
    pp_obj *code = find_method(I, cls, "AUTOLOAD", 0);
    if (!code || !code->u.code.name || (!code->u.code.body && !code->u.code.native)) return NULL;
    char buf[280];
    int n = strstr(code->u.code.name, "::") ? pp_snprintf(buf, sizeof buf, "$%s", code->u.code.name) : pp_snprintf(buf, sizeof buf, "$AUTOLOAD");
    pp_obj *var = pp_global(I, pp_intern(I, buf, n));
    char name[280];
    int nl = pp_snprintf(name, sizeof name, "%s::%s", cls, mname);
    pp_release(var->u.scalar);
    var->u.scalar = pp_strn(name, nl);
    return code;
}

/* A sub that writes to its arguments ($_[0] = ..., s/// on $_[1], for (@_) ...) changes the
   caller's variables in perl, where @_ holds aliases. Here @_ holds copies, so the changes are
   copied back after the call: the sub leaves what it changed in I->wb (args_changed), and the
   calling expression writes those values to the variables its arguments came from
   (args_writeback). Only subs the parser marked (PP_BLOCK_WARGS) take part.
   Not covered: changes after unshift / splice / pop / an assignment to @_, a sub that hands
   @_ on to the one that writes, and argument expressions other than the ones below. */
static NOINLINE void args_changed(pp_interp *I, pp_obj *code, pp_obj *argv, pp_list *orig, int nshift) {
    pp_list_free(&I->wb); I->wb_code = NULL;
    if (nshift + argv->u.arr.n == orig->n) {         /* only shifted: element i was argument i + nshift */
        for (int i = 0; i < argv->u.arr.n; i++) {
            if (pp_val_same(argv->u.arr.v[i], orig->v[i + nshift])) continue;
            pp_list_push(&I->wb, pp_int(i + nshift));
            pp_list_push(&I->wb, pp_copy(argv->u.arr.v[i]));
        }
        if (I->wb.n) I->wb_code = code;
    }
    pp_list_free(orig);
}
/* can this argument be evaluated again as an lvalue without doing anything else? */
static int arg_pure_lv(pp_node *n) {
    switch (n->type) {
    case N_VAR: return 1;
    case N_AELEM: case N_HELEM: return arg_pure_lv(n->a) && (n->b->type == N_CONST || n->b->type == N_VAR);
    case N_RV2AV: case N_RV2HV: case N_RV2SV: return arg_pure_lv(n->a);
    default: return 0;
    }
}
static void arg_store(pp_interp *I, pp_node *n, int k, pp_val v) {
    if (n->type == N_VAR && n->sym->name[0] == '@') {
        pp_obj *arr = pp_get_array(I, n, 0, NULL);
        if (!arr) return;
        if (k < arr->u.arr.n) { pp_release(arr->u.arr.v[k]); arr->u.arr.v[k] = pp_copy(v); if (arr->tied) pp_tie_writeback(I, arr); }
        pp_obj_decref(arr);
        return;
    }
    pp_val *slot = pp_lvalue(I, n);
    if (slot && !I->ctl) { pp_release(*slot); *slot = pp_copy(v); }
}
/* nodes[0..nn): the argument expressions; first: the invocant of a method call (argument 0) */
static int list_writeback(pp_interp *I, pp_node *first, pp_node **nodes, int nn, pp_list wb);
static NOINLINE void args_writeback(pp_interp *I, pp_node *first, pp_node **nodes, int nn) {
    pp_list wb = I->wb;                              /* detach: storing may call subs (tie) */
    memset(&I->wb, 0, sizeof I->wb); I->wb_code = NULL;
    list_writeback(I, first, nodes, nn, wb);
}
/* wb: (index, value) pairs, by index; the values go to the variables that the list expressions
   nodes[] name (foreach uses this too: its loop variable is a copy of the element as well).
   Consumes wb; returns how many values found their variable. */
static int list_writeback(pp_interp *I, pp_node *first, pp_node **nodes, int nn, pp_list wb) {
    int stored = 0;
    int saved_ctl = I->ctl; pp_sym *saved_label = I->ctl_label;   /* also after die: perl's aliases were written before it */
    I->ctl = CTL_NONE;
    int pos = 0, w = 0;                              /* pos: index of the first value of the current expression */
    for (int i = first ? -1 : 0; i < nn && w < wb.n && !I->ctl; i++) {
        pp_node *a = i < 0 ? first : nodes[i];
        int cnt;
        if (i < 0) cnt = 1;                          /* the invocant is one value, whatever it is */
        else if (a->type == N_VAR && a->sym->name[0] == '@') {
            pp_obj *arr = pp_get_array(I, a, 0, NULL);
            if (!arr) break;
            cnt = arr->u.arr.n; pp_obj_decref(arr);
        } else if ((a->type == N_VAR && a->sym->name[0] == '$') || a->type == N_AELEM || a->type == N_HELEM || a->type == N_RV2SV
                   || a->type == N_CONST || a->type == N_INTERP || a->type == N_REF || a->type == N_ANONARRAY || a->type == N_ANONHASH || a->type == N_ANONSUB) cnt = 1;
        else break;                                  /* a call, a hash, a list ...: the count is unknown from here on */
        for (; w < wb.n && wb.v[w].u.i < pos + cnt; w += 2) {
            if (wb.v[w].u.i < pos || !arg_pure_lv(a)) continue;
            arg_store(I, a, (int)(wb.v[w].u.i - pos), wb.v[w + 1]);
            stored++;
        }
        pos += cnt;
    }
    if (!I->ctl) { I->ctl = saved_ctl; I->ctl_label = saved_label; }
    pp_list_free(&wb);
    return stored;
}

pp_val pp_call(pp_interp *I, pp_obj *code, pp_list *args, int ctx, pp_list *out) {
    /* args: owned by caller; contents are moved into @_ */
    if (code->u.code.is_const) {
        if (code->u.code.is_const == 2 && code->u.code.constval.t == PP_REF) {
            pp_obj *arr = code->u.code.constval.u.o;
            if (ctx == CTX_LIST && out) { for (int i = 0; i < arr->u.arr.n; i++) pp_list_push(out, pp_copy(arr->u.arr.v[i])); return pp_undef(); }
            return arr->u.arr.n ? pp_copy(arr->u.arr.v[arr->u.arr.n - 1]) : pp_undef();
        }
        if (ctx == CTX_LIST && out) { pp_list_push(out, pp_copy(code->u.code.constval)); return pp_undef(); }
        return pp_copy(code->u.code.constval);
    }
    if (code->u.code.native) {
        I->goto_call = 0;
        /* natives return one scalar; in list context it becomes a one-element list */
        pp_val r = code->u.code.native(I, args, ctx, out);
        if (r.t == PP_UNDEF && r.u.i == PP_NO_VALUE) return pp_undef();          /* an empty list */
        if (ctx == CTX_LIST && out && !I->ctl) { pp_list_push(out, r); return pp_undef(); }
        if (ctx == CTX_VOID) { pp_release(r); return pp_undef(); }
        return r;
    }
    if (I->depth >= I->max_depth) return pp_die(I, "Deep recursion limit exceeded");
    if (!code->u.code.body) {   /* declared (sub name;) or referenced (\&name) but never defined */
        pp_obj *al = code->u.code.name ? autoload_sub(I, code->u.code.name) : NULL;
        if (al && al != code) return pp_call(I, al, args, ctx, out);
        return pp_die(I, "Undefined subroutine &%s%s called", strstr(code->u.code.name ? code->u.code.name : "", "::") ? "" : "main::", code->u.code.name ? code->u.code.name : "");
    }
    pp_env *env = pp_env_new(code->u.code.env, 1);
    env->is_sub = 1;
    pp_obj *argv = pp_env_cell(env, 0, '@');
    int wargs = code->u.code.body->flags & PP_BLOCK_WARGS;
    pp_list orig = {0};                  /* a sub that writes to $_[N]: what it was given, to tell what it changed */
    if (wargs) for (int i = 0; i < args->n; i++) pp_list_push(&orig, pp_copy(args->v[i]));
    for (int i = 0; i < args->n; i++) pp_array_push(argv, args->v[i]);
    args->n = 0;
    pp_env *saved_env = I->env;
    pp_obj *saved_code = I->cur_code;
    int saved_ctx = I->call_ctx, saved_line = I->line, saved_warn = I->warn_on;
    I->env = env;
    I->call_ctx = ctx;
    I->cur_code = code;                  /* for `state` variables */
    I->depth++;
    code->refcnt++;
    struct pp_frame fr = { I->frame, saved_code, I->file_pkg, I->file, code, saved_line, ctx, argv, 0 };
    struct pp_frame *prev_frame = I->frame;
    const char *prev_pkg = I->file_pkg;
    if (I->goto_call) {
        /* goto &sub: for caller(), this call replaces the sub that is running (its frame stays
           on the C stack, but is skipped) */
        I->goto_call = 0;
        if (prev_frame) { fr.up = prev_frame->up; fr.caller_code = prev_frame->caller_code; fr.caller_pkg = prev_frame->caller_pkg; fr.file = prev_frame->file; fr.line = prev_frame->line; }
    }
    I->frame = &fr;
    const char *saved_file = I->file;
    if (code->u.code.body->type == N_BLOCK && code->u.code.body->sym) I->file = code->u.code.body->sym->name + 1;
    void *msave = NULL, **prev_slot = I->m_slot;
    I->m_slot = &msave;
    pp_val r = exec_block(I, code->u.code.body, ctx, out);
    I->m_slot = prev_slot;
    I->file = saved_file;
    I->frame = prev_frame;
    I->file_pkg = prev_pkg;              /* a `package` statement inside the sub ends with it */
    I->depth--;
    I->cur_code = saved_code;
    if (I->ctl == CTL_RETURN) {
        I->ctl = CTL_NONE;
        pp_release(r);
        pp_list rv = I->ret;              /* detach: `out` may alias I->ret */
        memset(&I->ret, 0, sizeof I->ret);
        if (ctx == CTX_LIST && out) {
            for (int i = 0; i < rv.n; i++) pp_list_push(out, rv.v[i]);
            rv.n = 0;
            r = pp_undef();
        } else {
            r = rv.n ? rv.v[rv.n - 1] : pp_undef();
            if (rv.n) rv.n--;
        }
        pp_list_free(&rv);
    } else if ((I->ctl == CTL_LAST || I->ctl == CTL_NEXT || I->ctl == CTL_REDO) && !I->ctl_label) {
        I->ctl = CTL_NONE;   /* exiting subroutine via last/next: ignore */
    }
    I->env = saved_env;
    I->call_ctx = saved_ctx;
    I->line = saved_line;
    I->warn_on = saved_warn;
    if (wargs) args_changed(I, code, argv, &orig, fr.nshift);
    if (msave) pp_match_restore(I, msave);
    pp_env_decref(env);
    pp_obj_decref(code);
    return r;
}

static NOINLINE pp_val call_named(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_obj *code = pp_resolve_sub(I, n->sym);
    if (!code) {
        if (n->flags & 1) {   /* bareword */
            const char *name = n->sym->name + 1;
            if (n->flags & 8) {   /* FH, STDIN ...: a file handle, that is the glob of that name */
                char buf[264];
                const char *base = name;
                for (const char *p = name; *p; p++) if (p[0] == ':' && p[1] == ':') base = p + 2;
                if (!strcmp(base, "STDIN") || !strcmp(base, "STDOUT") || !strcmp(base, "STDERR")) name = base;   /* always main's */
                int len = pp_snprintf(buf, sizeof buf, "*%s%s", strstr(name, "::") ? "" : "main::", name);
                return scalar_or_push(ctx, out, pp_strn(buf, len));
            }
            if (n->flags & 4) { for (const char *p = name; *p; p++) if (p[0] == ':' && p[1] == ':') name = p + 2; }
            if (ctx == CTX_LIST && out) { pp_list_push(out, pp_cstr(name)); return pp_undef(); }
            return pp_cstr(name);
        }
        code = autoload_sub(I, n->sym->name + 1);
        if (!code) return pp_die(I, "Undefined subroutine &%s%s called", strstr(n->sym->name, "::") ? "" : "main::", n->sym->name + 1);
    }
    pp_list args = {0};
    if (n->flags & 2) {
        pp_obj *cur = current_args(I);
        if (cur) for (int i = 0; i < cur->u.arr.n; i++) pp_list_push(&args, pp_copy(cur->u.arr.v[i]));
    } else {
        for (int i = 0; i < n->n; i++) {
            pp_eval(I, n->list[i], CTX_LIST, &args);
            if (I->ctl) { pp_list_free(&args); return pp_undef(); }
        }
    }
    I->line = n->line;
    I->goto_call = (n->flags & PP_CALL_GOTO) != 0;
    pp_val r = pp_call(I, code, &args, ctx, out);
    I->goto_call = 0;
    if (I->wb_code == code && !(n->flags & 2)) args_writeback(I, NULL, n->list, n->n);
    pp_list_free(&args);
    return r;
}

static NOINLINE pp_val eval_callref(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_val cv = pp_eval(I, n->a, CTX_SCALAR, NULL);
    if (I->ctl) return pp_undef();
    if (is_symname(cv)) {   /* &{"name"}(...) / &$name */
        pp_obj *code = symref_code(I, n, cv);
        pp_release(cv);
        if (!code) return pp_undef();
        cv = pp_ref(code);
    }
    if (cv.t != PP_REF || cv.u.o->kind != PP_OCODE) { pp_release(cv); return pp_die(I, "Not a CODE reference"); }
    pp_list args = {0};
    if (n->flags & 2) {
        pp_obj *cur = current_args(I);
        if (cur) for (int i = 0; i < cur->u.arr.n; i++) pp_list_push(&args, pp_copy(cur->u.arr.v[i]));
    } else {
        for (int i = 0; i < n->n; i++) { pp_eval(I, n->list[i], CTX_LIST, &args); if (I->ctl) { pp_list_free(&args); pp_release(cv); return pp_undef(); } }
    }
    I->line = n->line;
    I->goto_call = (n->flags & PP_CALL_GOTO) != 0;
    pp_val r = pp_call(I, cv.u.o, &args, ctx, out);
    I->goto_call = 0;
    if (I->wb_code == cv.u.o && !(n->flags & 2)) args_writeback(I, NULL, n->list, n->n);
    pp_list_free(&args);
    pp_release(cv);
    return r;
}

/* ------------------------------------------------------------------ */
/* blocks & statements                                                 */
/* ------------------------------------------------------------------ */

/* goto LABEL reached this statement list: the index of the statement carrying the label, or -1 */
static int find_label(pp_node *blk, pp_sym *label) {
    for (int i = 0; i < blk->n; i++) {
        pp_node *s = blk->list[i];
        if (!s || s->sym != label) continue;
        if (s->type == N_LABEL || s->type == N_WHILE || s->type == N_FOR || s->type == N_FOREACH) return i;
    }
    return -1;
}
static pp_val exec_stmts(pp_interp *I, pp_node *blk, int ctx, pp_list *out) {
    pp_val last = pp_undef();
    struct pp_stmts here = { I->stmts, blk };
    I->stmts = &here;
    for (int i = 0; i < blk->n; i++) {
        pp_node *s = blk->list[i];
        if (s->line) I->line = s->line;   /* line 0: signature check, keep the caller's line */
        I->warn_on = (s->flags & PP_STMT_WARN) != 0;
        if ((++I->stmt_counter & 0xFF) == 0) pp_host_poll();
        if (!I->in_dispatch && (I->ev_head != I->ev_tail || I->alarm_at)) pp_events_poll(I);
        int is_last = i == blk->n - 1;
        pp_val v = pp_eval(I, s, is_last ? ctx : CTX_VOID, is_last ? out : NULL);
        if (is_last) last = v; else pp_release(v);
        if (I->tie_pend) pp_tie_flush(I);
        if (pp_destroy_n) pp_destroy_run(I);
        if (I->ctl == CTL_GOTO) {
            int j = find_label(blk, I->ctl_label);
            if (j >= 0) {                        /* the label is here: go on from it */
                I->ctl = CTL_NONE; I->ctl_label = NULL;
                pp_release(last); last = pp_undef();
                i = j - 1;
                continue;
            }
        }
        if (I->ctl) break;
    }
    I->stmts = here.up;
    return last;
}

static pp_val exec_block(pp_interp *I, pp_node *blk, int ctx, pp_list *out) {
    if (blk->type != N_BLOCK) return pp_eval(I, blk, ctx, out);
    int lh = I->nlocals;
    const char *saved_pkg = I->file_pkg;
    pp_val r;
    if (blk->flags & 1) {
        pp_env *env = pp_env_new(I->env, blk->aux);
        pp_env *saved = I->env;
        I->env = env;
        r = exec_stmts(I, blk, ctx, out);
        I->env = saved;
        pp_env_decref(env);
    } else {
        r = exec_stmts(I, blk, ctx, out);
    }
    if (I->nlocals > lh) local_unwind(I, lh);
    if (blk->flags & PP_BLOCK_PKG) I->file_pkg = saved_pkg;
    return r;
}

/* handle last/next/redo after a loop body; returns 1 to break the loop, 2 to run the body again */
static int loop_ctl(pp_interp *I, pp_sym *label) {
    if (I->ctl == CTL_NEXT || I->ctl == CTL_LAST || I->ctl == CTL_REDO) {
        if (I->ctl_label && I->ctl_label != label) return 1;   /* propagate to an outer loop */
        int r = I->ctl == CTL_LAST ? 1 : I->ctl == CTL_REDO ? 2 : 0;
        I->ctl = CTL_NONE; I->ctl_label = NULL;
        if (r == 2 && (++I->stmt_counter & 0xFF) == 0) pp_host_poll();   /* a loop made of redo alone can still be interrupted */
        return r;
    }
    return I->ctl != CTL_NONE;
}
/* the body of a loop: a block (expr = 0) or the statement in front of a modifier */
static int loop_body_x(pp_interp *I, pp_node *body, pp_sym *label, int expr) {
    int r;
    do {
        pp_val v = expr ? pp_eval(I, body, CTX_VOID, NULL) : exec_block(I, body, CTX_VOID, NULL);
        pp_release(v);
        r = loop_ctl(I, label);
    } while (r == 2);
    return r;
}
#define loop_body(I, body, label) loop_body_x(I, body, label, 0)

static int cond_true(pp_interp *I, pp_node *c) {
    if (!c) return 1;
    pp_val v = pp_eval(I, c, CTX_SCALAR, NULL);
    int t = pp_truth(v);
    pp_release(v);
    if (I->ctl) return 0;
    return t;
}

static NOINLINE pp_val eval_while(pp_interp *I, pp_node *n) {
    if (n->flags & 16) {   /* bare block */
        loop_body(I, n->b, n->sym);
        return pp_undef();
    }
    pp_env *saved = I->env;
    int until = n->op == 1;
    if (n->flags & 8) {    /* do {} while */
        do {
            if ((++I->stmt_counter & 0xFF) == 0) pp_host_poll();
            if (!I->in_dispatch && (I->ev_head != I->ev_tail || I->alarm_at)) pp_events_poll(I);
            if (loop_body(I, n->b, n->sym)) break;
        } while (cond_true(I, n->a) != until && !I->ctl);
    } else {
        for (;;) {
            if ((++I->stmt_counter & 0xFF) == 0) pp_host_poll();
            if (!I->in_dispatch && (I->ev_head != I->ev_tail || I->alarm_at)) pp_events_poll(I);
            /* `while (my $x = ...)`: a fresh scope per iteration, so closures capture their own $x */
            pp_env *it = NULL;
            if (n->flags & 4) { it = pp_env_new(saved, n->aux); I->env = it; }
            int brk = 0;
            int t = cond_true(I, n->a);
            if (I->ctl || t == until) brk = 1;
            else brk = loop_body_x(I, n->b, n->sym, n->flags & 2);
            if (!brk && n->c) { pp_val v = exec_block(I, n->c, CTX_VOID, NULL); pp_release(v); if (I->ctl) brk = 1; }
            if (it) { I->env = saved; pp_env_decref(it); }
            if (brk) break;
        }
    }
    return pp_undef();
}

static NOINLINE pp_val eval_for(pp_interp *I, pp_node *n) {
    pp_env *env = NULL, *saved = I->env;
    if (n->flags & 4) { env = pp_env_new(I->env, n->aux); I->env = env; }
    if (n->a) { pp_val v = pp_eval(I, n->a, CTX_VOID, NULL); pp_release(v); }
    while (!I->ctl) {
        if ((++I->stmt_counter & 0xFF) == 0) pp_host_poll();
        if (!I->in_dispatch && (I->ev_head != I->ev_tail || I->alarm_at)) pp_events_poll(I);
        if (!cond_true(I, n->b)) break;
        if (loop_body(I, n->d, n->sym)) break;
        if (n->c) { pp_val v = pp_eval(I, n->c, CTX_VOID, NULL); pp_release(v); }
    }
    if (env) { I->env = saved; pp_env_decref(env); }
    return pp_undef();
}

static NOINLINE pp_val eval_foreach(pp_interp *I, pp_node *n) {
    pp_env *env = NULL, *saved = I->env;
    pp_obj *cell;
    pp_val saved_val = pp_undef();
    int restore = 0;
    /* the list is evaluated in the enclosing scope (before the loop variable's scope exists) */
    pp_obj *src_arr = NULL;     /* aliasing emulation: write modified loop variables back into a single array */
    pp_node *lb = n->b;
    if (lb->type == N_LIST && lb->n == 1) lb = lb->list[0];
    if ((lb->type == N_VAR && lb->sym->name[0] == '@') || lb->type == N_RV2AV) {
        src_arr = pp_get_array(I, lb, 0, NULL);
        if (I->ctl) src_arr = NULL;
    }
    pp_list items = {0};
    if (src_arr) { for (int i = 0; i < src_arr->u.arr.n; i++) pp_list_push(&items, pp_copy(src_arr->u.arr.v[i])); }
    else pp_eval(I, n->b, CTX_LIST, &items);
    int nv = 1;                          /* for my ($k, $v) (LIST): stride > 1 */
    pp_obj *cells[16];
    if (n->a && n->a->type == N_MY) {
        nv = n->a->n > 0 ? n->a->n : 1;
        env = pp_env_new(I->env, nv); I->env = env;
        for (int k = 0; k < nv; k++) cells[k] = pp_env_cell(env, k, '$');
        cell = cells[0];
    } else if (n->a) {
        cell = var_cell(I, n->a);
        saved_val = cell->u.scalar; cell->u.scalar = pp_undef(); restore = 1;
    } else {
        cell = I->g_underscore;
        saved_val = cell->u.scalar; cell->u.scalar = pp_undef(); restore = 1;
    }
    if (!I->ctl) {
        for (int i = 0; i < items.n; i += nv) {
            if ((++I->stmt_counter & 0xFF) == 0) pp_host_poll();
            if (!I->in_dispatch && (I->ev_head != I->ev_tail || I->alarm_at)) pp_events_poll(I);
            if (env && env->refcnt > 1) {
                /* a closure made in the previous iteration captured `my $var`: it keeps that
                   scope, and this iteration gets a fresh one (each closure sees its own value) */
                I->env = saved; pp_env_decref(env);
                env = pp_env_new(I->env, nv); I->env = env;
                for (int k = 0; k < nv; k++) cells[k] = pp_env_cell(env, k, '$');
                cell = cells[0];
            }
            if (nv == 1) {
                pp_release(cell->u.scalar);
                cell->u.scalar = pp_copy(items.v[i]);
            } else {
                for (int k = 0; k < nv; k++) {   /* missing tail elements become undef, like perl */
                    pp_release(cells[k]->u.scalar);
                    cells[k]->u.scalar = i + k < items.n ? pp_copy(items.v[i + k]) : pp_undef();
                }
            }
            int brk = loop_body_x(I, n->c, n->sym, n->flags & 2);
            if (nv == 1 && src_arr && i < src_arr->u.arr.n) {
                pp_val *slot = &src_arr->u.arr.v[i];
                if (!pp_val_same(*slot, cell->u.scalar)) { pp_release(*slot); *slot = pp_copy(cell->u.scalar); }
            }
            else if (nv == 1 && !src_arr && !pp_val_same(cell->u.scalar, items.v[i])) {
                /* perl aliases the loop variable to the list element; here the changed value is
                   written back to the variable it came from: for ($x, @list, $h{k}) { s/..// } */
                pp_list wb = {0};
                pp_list_push(&wb, pp_int(i)); pp_list_push(&wb, pp_copy(cell->u.scalar));
                int direct = n->b->type != N_LIST;
                pp_env *loop_env = I->env;
                I->env = saved;                      /* the list was written in the enclosing scope */
                int stored = list_writeback(I, NULL, direct ? &n->b : n->b->list, direct ? 1 : n->b->n, wb);
                I->env = loop_env;
                /* nothing to write back when the list holds no variables (split(...), a call):
                   the loop then changes a temporary, as in perl */
                int has_var = 0;
                for (int q = 0; q < (direct ? 1 : n->b->n) && !has_var; q++) {
                    pp_node *e = direct ? n->b : n->b->list[q];
                    has_var = e->type == N_VAR || e->type == N_AELEM || e->type == N_HELEM || e->type == N_RV2AV || e->type == N_RV2HV || e->type == N_RV2SV || e->type == N_MY;
                }
                if (!stored && has_var && !(I->compat_warn & 4) && !I->ctl) {
                    I->compat_warn |= 4;
                    pp_warn(I, "picoperl: modifying the foreach variable only changes the source when iterating variables");
                }
            }
            if (brk) break;
            if (env && env->refcnt > 1 && i + nv < items.n) {
                /* a closure captured this iteration's variables: the next iteration gets fresh ones */
                I->env = saved; pp_env_decref(env);
                env = pp_env_new(I->env, nv); I->env = env;
                for (int k = 0; k < nv; k++) cells[k] = pp_env_cell(env, k, '$');
                cell = cells[0];
            }
        }
    }
    pp_list_free(&items);
    if (src_arr) pp_obj_decref(src_arr);
    if (restore) { pp_release(cell->u.scalar); cell->u.scalar = saved_val; }
    if (env) { I->env = saved; pp_env_decref(env); }
    return pp_undef();
}

/* a loop with a label: note it while it runs, so that last/next LABEL can tell whether the
   label exists */
static NOINLINE pp_val eval_labeled_loop(pp_interp *I, pp_node *n) {
    struct pp_looplabel here = { I->loop_labels, n->sym };
    I->loop_labels = &here;
    pp_val r = n->type == N_WHILE ? eval_while(I, n) : n->type == N_FOR ? eval_for(I, n) : eval_foreach(I, n);
    I->loop_labels = here.up;
    return r;
}

static NOINLINE pp_val eval_if(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_env *env = NULL, *saved = I->env;
    if (n->flags & 4) { env = pp_env_new(I->env, n->aux); I->env = env; }
    pp_val r = pp_undef();
    int t = cond_true(I, n->a);
    if (!I->ctl) {
        if (n->op) t = !t;
        if (t) r = (n->flags & 2) ? pp_eval(I, n->b, ctx, out) : exec_block(I, n->b, ctx, out);
        else if (n->c) r = n->c->type == N_IF ? eval_if(I, n->c, ctx, out) : exec_block(I, n->c, ctx, out);
    }
    if (env) { I->env = saved; pp_env_decref(env); }
    return r;
}

/* ------------------------------------------------------------------ */
/* expression helpers (kept out of pp_eval to keep its stack frame small) */
/* ------------------------------------------------------------------ */

static pp_val scalar_or_push(int ctx, pp_list *out, pp_val v) {
    if (ctx == CTX_LIST && out) { pp_list_push(out, v); return pp_undef(); }
    if (ctx == CTX_VOID) { pp_release(v); return pp_undef(); }
    return v;
}

static NOINLINE pp_val make_ref(pp_interp *I, pp_node *a, int ctx, pp_list *out) {
    switch (a->type) {
    case N_VAR: return pp_ref(var_cell(I, a));
    case N_MY:
        declare_my(I, a);
        if (a->n == 1) return make_ref(I, a->list[0], ctx, out);
        break;
    case N_RV2AV: { pp_obj *o = pp_get_array_raw(I, a, 1); if (!o) return pp_undef(); pp_val v = pp_ref(o); pp_obj_decref(o); return v; }
    case N_RV2HV: { pp_obj *o = pp_get_hash_raw(I, a, 1); if (!o) return pp_undef(); pp_val v = pp_ref(o); pp_obj_decref(o); return v; }
    case N_CALL:
        if (a->n == 0 && (a->flags & 2)) {   /* \&name: the sub itself; \name() and \(CONST) are references to the result */
            pp_obj *code = pp_resolve_sub(I, a->sym);
            if (!code) {
                /* not defined yet: a placeholder that the definition will fill in. It lives in
                   the name's cell only, so the name itself still counts as undefined. */
                pp_obj *cell = pp_global(I, a->sym);
                if (cell->u.scalar.t == PP_REF && is_sub_stub(cell->u.scalar.u.o)) return pp_copy(cell->u.scalar);
                code = pp_obj_new(PP_OCODE);
                code->u.code.name = a->sym->name + 1;
                pp_release(cell->u.scalar);
                cell->u.scalar = pp_ref(code);
                pp_obj_decref(code);
                return pp_copy(cell->u.scalar);
            }
            return pp_ref(code);
        }
        break;
    case N_ANONSUB: return pp_eval(I, a, CTX_SCALAR, NULL);
    case N_GLOB: { pp_sym *gs = glob_sym(I, a); return gs ? pp_ref(pp_glob_obj(I, gs)) : pp_undef(); }
    case N_CALLREF:
        if (a->flags & 2) {   /* \&$code / \&{"name"}: the sub itself, not a call */
            pp_val cv = pp_eval(I, a->a, CTX_SCALAR, NULL);
            if (I->ctl) { pp_release(cv); return pp_undef(); }
            if (cv.t == PP_REF && cv.u.o->kind == PP_OCODE) return cv;
            if (!is_symname(cv)) { pp_release(cv); return pp_die(I, "Not a CODE reference"); }
            /* \&{"name"} is allowed under strict refs (it only names the sub) */
            pp_sym *cs = pp_symref(I, a, cv, '&', 0);
            pp_obj *code = cs ? pp_resolve_sub(I, cs) : NULL;
            if (cs && !code) pp_die(I, "Undefined subroutine &%s%s called", strstr(cs->name, "::") ? "" : "main::", cs->name + 1);
            pp_release(cv);
            return code ? pp_ref(code) : pp_undef();
        }
        break;
    case N_LIST:
        if (a->n == 1) return make_ref(I, a->list[0], ctx, out);
        if (ctx == CTX_LIST && out) {
            for (int i = 0; i < a->n; i++) { pp_val v = make_ref(I, a->list[i], CTX_SCALAR, NULL); if (I->ctl) return pp_undef(); pp_list_push(out, v); }
            return pp_undef();
        }
        break;
    case N_AELEM: case N_HELEM: {
        /* \$a[i], \$h{k}: elements are plain values inside their container, there is no cell
           to point at. The reference goes to a scalar that is tied (natively) to that element:
           reading it reads the element, writing it writes the element. */
        pp_obj *box = a->type == N_AELEM ? pp_get_array_raw(I, a->a, 1) : pp_get_hash_raw(I, a->a, 1);
        if (!box || I->ctl) { if (box) pp_obj_decref(box); return pp_undef(); }
        if (box->tied || box->kind != (a->type == N_AELEM ? PP_OARRAY : PP_OHASH)) { pp_obj_decref(box); break; }   /* a tied container: the copy below */
        pp_val key = pp_eval(I, a->b, CTX_SCALAR, NULL);
        if (I->ctl) { pp_release(key); pp_obj_decref(box); return pp_undef(); }
        pp_val cur;
        if (a->type == N_AELEM) {
            int64_t i = pp_to_int(key);
            if (i < 0) i += box->u.arr.n;
            if (i < 0 || i > 0x3fffffff) { pp_release(key); pp_obj_decref(box); break; }
            pp_release(key); key = pp_int(i);
            cur = pp_copy(*pp_array_slot(box, (int)i));          /* the element exists from now on, like in perl */
        } else {
            pp_str *ks = pp_to_str(key);
            pp_release(key); key = pp_str_val(ks);
            cur = pp_copy(*pp_hash_slot(box, ks->d, ks->len));
        }
        pp_obj *spec = pp_obj_new(PP_OARRAY);                    /* the tie object: [container, key] */
        pp_array_push(spec, pp_ref(box)); pp_obj_decref(box);
        pp_array_push(spec, key);
        spec->class = pp_intern(I, "PicoPerl::ElemRef", 17);
        pp_obj *cell = pp_obj_new(PP_OSCALAR);
        cell->u.scalar = cur;
        pp_val sv = pp_ref(spec); pp_obj_decref(spec);
        pp_tie_set(cell, sv);
        pp_val r = pp_ref(cell); pp_obj_decref(cell);
        return r;
    }
    default: break;
    }
    /* reference to a copy of the value (e.g. \"str"; \$a[0] is approximated the same way) */
    pp_val v = pp_eval(I, a, CTX_SCALAR, NULL);
    if (I->ctl) { pp_release(v); return pp_undef(); }
    pp_obj *s = pp_obj_new(PP_OSCALAR);
    s->u.scalar = v;
    pp_val r = pp_ref(s); pp_obj_decref(s);
    return r;
}

static NOINLINE pp_val eval_range(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_val a = pp_eval(I, n->a, CTX_SCALAR, NULL);
    if (I->ctl) return pp_undef();
    pp_val b = pp_eval(I, n->b, CTX_SCALAR, NULL);
    if (I->ctl) { pp_release(a); return pp_undef(); }
    if (ctx != CTX_LIST || !out) { pp_release(a); pp_release(b); return pp_die(I, "Range operator in scalar context is not supported"); }
    if (a.t == PP_STR && !pp_looks_like_number(a.u.s->d, a.u.s->len) && is_magic_inc_str(a.u.s)) {
        pp_str *bs = pp_to_str(b);
        pp_val cur = pp_copy(a);
        int guard = 0;
        while (cur.u.s->len <= bs->len && guard++ < 100000) {
            pp_list_push(out, pp_copy(cur));
            if (cur.u.s->len == bs->len && memcmp(cur.u.s->d, bs->d, bs->len) == 0) break;
            pp_val nx = magic_inc(cur.u.s);
            pp_release(cur); cur = nx;
        }
        pp_release(cur); pp_str_decref(bs);
    } else {
        int64_t lo = pp_to_int(a), hi = pp_to_int(b);
        for (int64_t i = lo; i <= hi; i++) pp_list_push(out, pp_int(i));
    }
    pp_release(a); pp_release(b);
    return pp_undef();
}

static NOINLINE pp_val eval_interp(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_str *r = pp_str_new(NULL, 0);
    for (int i = 0; i < n->n; i++) {
        pp_val v = pp_eval(I, n->list[i], CTX_SCALAR, NULL);
        if (I->ctl) { pp_release(v); pp_str_decref(r); return pp_undef(); }
        if (I->warn_on && v.t == PP_UNDEF) warn_uninit(I, "concatenation (.) or string", var_name_of(n->list[i]));
        pp_str *t; const char *p = pp_to_cstr(v, &t);
        int l = t ? t->len : v.u.s->len;
        r = pp_str_reserve(r, l);
        memcpy(r->d + r->len, p, l); r->len += l; r->d[r->len] = 0;
        if (t) pp_str_decref(t);
        pp_release(v);
    }
    return scalar_or_push(ctx, out, pp_str_val(r));
}

static void track_unit(pp_interp *I, pp_node *unit, char *fname) {
    I->units = pp_xrealloc(I->units, (I->nunits + 1) * sizeof(pp_node *));
    I->unit_files = pp_xrealloc(I->unit_files, (I->nunits + 1) * sizeof(char *));
    I->units[I->nunits] = unit; I->unit_files[I->nunits] = fname; I->nunits++;
}

/* release what a subtree owns (constant values, list arrays); nodes themselves are freed
   only when malloc'd, arena nodes go away with their arena */
void pp_free_ast(pp_node *n) {
    if (!n) return;
    pp_free_ast(n->a); pp_free_ast(n->b);
    if (n->type == N_CONST) { pp_release(n->val); n->val = pp_undef(); }
    else {
        pp_free_ast(n->c); pp_free_ast(n->d);
        for (int i = 0; i < n->n; i++) pp_free_ast(n->list[i]);
        if (n->flags & PP_NODE_HEAP) free(n->list);   /* arena nodes: the list lives in the arena */
        n->list = NULL; n->n = 0;
    }
    if (n->flags & PP_NODE_HEAP) { free(n); pp_stat_nodes--; }
}

/* A file is parsed straight into the interpreter's arena. Until the parse has succeeded its
   part of the arena holds nodes only, each still owning a malloc'd list or a constant; after
   a syntax error (or an exit in a BEGIN block) this walks that part and releases them.
   What lies between `from` and `to` but is not ours is stepped over: the nodes and lists of a
   file that a `use` loaded meanwhile (an N_SKIPRANGE node in front says where it ends) and
   the chunks taken over from an eval string (foreign).
   rewind: also give the memory back, when nothing else got in between. */
void pp_free_node_range(pp_arena *a, pp_arena_pos from, pp_arena_pos to, int rewind) {
    int n = 0, plain = 1;
    for (pp_arena_chunk *c = a->chunks; c; c = c->next) { n++; if (c == from.chunk) break; }
    if (!n) return;
    pp_arena_chunk **cs = pp_xmalloc((size_t)n * sizeof *cs);       /* oldest first */
    { int i = n; for (pp_arena_chunk *c = a->chunks; c && i > 0; c = c->next) cs[--i] = c; }
    size_t off = cs[0] == from.chunk ? from.off : 0;
    for (int i = 0; i < n; ) {
        pp_arena_chunk *c = cs[i];
        size_t limit = c == to.chunk ? to.off : c->used;
        if (c->foreign) { plain = 0; i++; off = 0; continue; }
        if (off + sizeof(pp_node) > limit) { if (c == to.chunk) break; i++; off = 0; continue; }
        pp_node *nd = (pp_node *)((char *)(c + 1) + off);
        if (nd->type == N_SKIPRANGE) {
            plain = 0;
            pp_arena_chunk *ec = (pp_arena_chunk *)nd->a;
            int j = i;
            while (j < n && cs[j] != ec) j++;
            if (j >= n) break;                                       /* cannot happen: the range ends inside ours */
            i = j; off = (size_t)(uintptr_t)nd->b;
            continue;
        }
        if (nd->type == N_CONST) { pp_release(nd->val); nd->val = pp_undef(); }
        else { free(nd->list); nd->list = NULL; nd->n = 0; }
        off += sizeof(pp_node);
    }
    if (rewind && plain && to.chunk == a->chunks) {
        while (a->chunks != from.chunk && a->chunks) { pp_arena_chunk *c = a->chunks; a->chunks = c->next; free(c); }
        if (a->chunks) a->chunks->used = from.off;
    }
    free(cs);
}

/* does the subtree contain code that may still be referenced later (anonymous subs)? */
static int keeps_code(pp_node *n) {
    if (!n) return 0;
    if (n->type == N_ANONSUB) return 1;
    if (n->type == N_CONST) return 0;
    if (keeps_code(n->a) || keeps_code(n->b) || keeps_code(n->c) || keeps_code(n->d)) return 1;
    for (int i = 0; i < n->n; i++) if (keeps_code(n->list[i])) return 1;
    return 0;
}

void pp_request_exit(pp_interp *I, int code) {
    I->exit_code = code;
    I->ctl = CTL_EXIT;
}

/* load and run a file once (force: `do FILE` semantics) */
static pp_val do_require(pp_interp *I, const char *fn, int force) {
    int fnlen = (int)strlen(fn);
    if (!force && pp_hash_find(I->loaded, fn, fnlen)) return pp_int(1);
    if (!force) {   /* it did not compile before (%INC keeps the name with undef): not tried again */
        pp_val *prev = pp_hash_find(pp_global(I, pp_intern(I, "%INC", 4)), fn, fnlen);
        if (prev && prev->t == PP_UNDEF) return pp_die(I, "Attempt to reload %s aborted.\nCompilation failed in require", fn);
    }
    /* the directories in @INC (use lib 'dir'; unshift @INC, ...) come first, then the places
       the host looks in by itself. A path that says where it is (/abs, ./rel) is taken as is. */
    char *src = NULL;
    char found[512];
    pp_snprintf(found, sizeof found, "%s", fn);
    if (fn[0] != '/' && !(fn[0] == '.' && (fn[1] == '/' || (fn[1] == '.' && fn[2] == '/')))) {
        pp_obj *inc = pp_global(I, pp_intern(I, "@INC", 4));
        for (int i = 0; i < inc->u.arr.n && !src; i++) {
            if (inc->u.arr.v[i].t != PP_STR || inc->u.arr.v[i].u.s->len == 0) continue;
            pp_snprintf(found, sizeof found, "%s/%s", inc->u.arr.v[i].u.s->d, fn);
            src = pp_host_read_file(found);
        }
        if (!src) pp_snprintf(found, sizeof found, "%s", fn);
    }
    if (!src) src = pp_host_read_file(fn);
    if (!src) return pp_die(I, "Can't locate %s in @INC (you may need to install the module)", fn);
    {   /* %INC: what was loaded, and from where */
        pp_val *slot = pp_hash_slot(pp_global(I, pp_intern(I, "%INC", 4)), fn, fnlen);
        pp_release(*slot); *slot = pp_cstr(found);
    }
    pp_hash_set(I->loaded, fn, fnlen, pp_int(1));
    int err = 0;
    char *fname = pp_xmalloc(fnlen + 1); memcpy(fname, fn, fnlen + 1);   /* outlives the AST */
    /* the file that asked for this one (a `use` in a file still being parsed) keeps its own
       list of subs waiting to be defined, and its own file scope */
    pp_node **outer_subs = I->subdefs; int outer_nsubs = I->nsubdefs;
    pp_env *outer_pending = I->pending_env;
    I->subdefs = NULL; I->nsubdefs = 0; I->pending_env = NULL;
    pp_node *unit = pp_parse(I, fname, src, &err);
    free(src);
    if (err) {
        free(fname);
        if (I->pending_env) pp_env_decref(I->pending_env);
        free(I->subdefs);
        I->subdefs = outer_subs; I->nsubdefs = outer_nsubs; I->pending_env = outer_pending;
        if (I->ctl != CTL_EXIT) {   /* not loaded after all; %INC remembers the failure, as in perl */
            pp_hash_delete(I->loaded, fn, fnlen, NULL);
            pp_val *slot = pp_hash_slot(pp_global(I, pp_intern(I, "%INC", 4)), fn, fnlen);
            pp_release(*slot); *slot = pp_undef();
        }
        return pp_die(I, "Compilation failed in require");
    }
    track_unit(I, unit, fname);
    const char *saved_file = I->file; int saved_line = I->line;
    pp_env *saved_env = I->env;
    I->file = fname;
    /* the file runs as if called from the require: caller() at its top level names the
       requiring code (so `unless (caller) { self-test }` stays quiet), like perl */
    struct pp_frame fr = { I->frame, I->cur_code, I->file_pkg, saved_file, NULL, saved_line, CTX_SCALAR, NULL, 0 };
    I->frame = &fr;
    pp_val r = pp_exec_unit(I, unit, CTX_SCALAR, NULL);
    I->frame = fr.up;
    if (I->ctl == CTL_DIE) {   /* it died while running: not loaded, and remembered as failed like a compile error */
        pp_hash_delete(I->loaded, fn, fnlen, NULL);
        pp_val *slot = pp_hash_slot(pp_global(I, pp_intern(I, "%INC", 4)), fn, fnlen);
        pp_release(*slot); *slot = pp_undef();
    }
    free(I->subdefs);
    I->subdefs = outer_subs; I->nsubdefs = outer_nsubs; I->pending_env = outer_pending;
    I->env = saved_env;
    I->file = saved_file; I->line = saved_line;
    return r;
}

static NOINLINE pp_val eval_require(pp_interp *I, pp_node *n) {
    if (!n->a) return pp_int(1);
    pp_val fv = pp_eval(I, n->a, CTX_SCALAR, NULL);
    if (I->ctl) return pp_undef();
    pp_str *fn = pp_to_str(fv); pp_release(fv);
    pp_val r = do_require(I, fn->d, n->op == 1);
    pp_str_decref(fn);
    return r;
}

/* "&name" in package pkg ("main" -> "&name") */
static pp_sym *pkg_sym(pp_interp *I, char sigil, const char *pkg, const char *name) {
    char buf[256]; int n;
    if (strcmp(pkg, "main") == 0 || strstr(name, "::")) n = pp_snprintf(buf, sizeof buf, "%c%s", sigil, name);
    else n = pp_snprintf(buf, sizeof buf, "%c%s::%s", sigil, pkg, name);
    return pp_intern(I, buf, n);
}

static void builtin_import(pp_interp *I, const char *mod, const char *pkg, pp_list *args, int from);
static pp_obj *find_method(pp_interp *I, const char *cls, const char *name, int depth);
/* export one symbol from module `mod` into package `pkg`; returns 0 if it does not exist */
static int export_symbol(pp_interp *I, const char *mod, const char *pkg, const char *name) {
    char sigil = '&';
    if (*name == '&' || *name == '$' || *name == '@' || *name == '%') sigil = *name++;
    size_t nl = strlen(name);
    if (nl && name[nl - 1] == '*') {          /* "KC_*": names resolved on first use */
        char prefix[64]; snprintf(prefix, sizeof prefix, "%.*s", (int)(nl - 1), name);
        add_lazy_export(I, pkg, mod, prefix);
        return 1;
    }
    pp_sym *src = pkg_sym(I, sigil, mod, name);
    pp_sym *dst = pkg_sym(I, sigil, pkg, name);
    if (sigil == '&') {
        pp_obj *code = pp_resolve_sub(I, src);   /* also finds main/native subs listed in @EXPORT */
        if (!code) return 0;
        if (dst == src) return 1;
        code->refcnt++;
        pp_define_sub(I, dst, code);
        return 1;
    }
    if (dst != src) pp_global_alias(I, dst, pp_global(I, src));
    return 1;
}

static void export_array(pp_interp *I, const char *mod, const char *pkg, const char *arrname, pp_list *only) {
    pp_sym *s = pkg_sym(I, '@', mod, arrname);
    if (!s->gcell) return;
    pp_obj *arr = s->gcell;
    for (int i = 0; i < arr->u.arr.n; i++) {
        pp_str *nm = pp_to_str(arr->u.arr.v[i]);
        int want = 1;
        if (only) { want = 0; for (int k = 0; k < only->n; k++) { pp_str *o = pp_to_str(only->v[k]); if (strcmp(o->d, nm->d) == 0) want = 1; pp_str_decref(o); } }
        if (want && !export_symbol(I, mod, pkg, nm->d)) pp_die(I, "\"%s\" is not exported by the %s module", nm->d, mod);
        pp_str_decref(nm);
        if (I->ctl) return;
    }
}

/* use Module LIST: require Module.pm, then import (user import() or Exporter emulation) */
/* what Exporter's import does, without Exporter.pm: export @EXPORT of `mod` into `pkg`, or
   the names in args (from, the first one to look at); a :tag takes @EXPORT and @EXPORT_OK */
static void builtin_import(pp_interp *I, const char *mod, const char *pkg, pp_list *args, int from) {
    if (args->n <= from) { export_array(I, mod, pkg, "EXPORT", NULL); return; }
    pp_list names = {0}; int all = 0;
    for (int i = from; i < args->n; i++) {
        pp_str *a = pp_to_str(args->v[i]);
        if (a->d[0] == ':') all = 1;                 /* :all / :DEFAULT tags: take everything */
        else pp_list_push(&names, pp_strn(a->d, a->len));
        pp_str_decref(a);
    }
    if (all) { export_array(I, mod, pkg, "EXPORT", NULL); if (!I->ctl) export_array(I, mod, pkg, "EXPORT_OK", NULL); }
    for (int i = 0; i < names.n && !I->ctl; i++) {
        pp_str *nm = pp_to_str(names.v[i]);
        if (!export_symbol(I, mod, pkg, nm->d)) pp_die(I, "\"%s\" is not exported by the %s module", nm->d, mod);
        pp_str_decref(nm);
    }
    pp_list_free(&names);
}

static NOINLINE pp_val eval_use(pp_interp *I, pp_node *n) {
    pp_str *mod = pp_to_str(n->a->val);
    pp_str *pkg = pp_to_str(n->b->val);
    char file[256]; int fl = 0;
    for (int i = 0; i < mod->len && fl < 240; i++) {
        if (mod->d[i] == ':' && i + 1 < mod->len && mod->d[i+1] == ':') { file[fl++] = '/'; i++; }
        else file[fl++] = mod->d[i];
    }
    memcpy(file + fl, ".pm", 4);
    if (n->op == 1) {
        /* use parent/base LIST: require each class (unless -norequire) and push it onto @ISA */
        pp_list args = {0};
        for (int i = 0; i < n->n; i++) { pp_eval(I, n->list[i], CTX_LIST, &args); if (I->ctl) { pp_list_free(&args); goto out; } }
        int norequire = 0;
        char isa[256]; snprintf(isa, sizeof isa, "@%s::ISA", pkg->d);
        pp_obj *isav = pp_global(I, pp_intern(I, isa, (int)strlen(isa)));
        for (int i = 0; i < args.n && !I->ctl; i++) {
            pp_str *c = pp_to_str(args.v[i]);
            if (strcmp(c->d, "-norequire") == 0) { norequire = 1; pp_str_decref(c); continue; }
            if (!norequire) {
                char f[256]; int fl = 0;
                for (int k = 0; k < c->len && fl < 240; k++) { if (c->d[k] == ':' && c->d[k+1] == ':') { f[fl++] = '/'; k++; } else f[fl++] = c->d[k]; }
                memcpy(f + fl, ".pm", 4);
                pp_release(do_require(I, f, 0));
            }
            if (!I->ctl) pp_array_push(isav, pp_strn(c->d, c->len));
            pp_str_decref(c);
        }
        pp_list_free(&args);
        goto out;
    }
    pp_val r = do_require(I, file, 0);
    pp_release(r);
    if (I->ctl) goto out;
    if (n->flags & 2) goto out;                  /* use Module (); */
    pp_list args = {0};
    for (int i = 0; i < n->n; i++) { pp_eval(I, n->list[i], CTX_LIST, &args); if (I->ctl) { pp_list_free(&args); goto out; } }
    /* Module->import(LIST): the module's own import, or one it inherits (use Test::Simple
       tests => 5 runs Test::Builder::Module::import). An inherited Exporter::import is left
       to the built-in export code below, which does not need Exporter.pm. */
    pp_sym *imp = pkg_sym(I, '&', mod->d, "import");
    pp_obj *impcode = imp->sub ? imp->sub : find_method(I, mod->d, "import", 0);
    if (impcode && impcode->u.code.name && !strcmp(impcode->u.code.name, "Exporter::import")) impcode = NULL;
    if (impcode) {
        pp_list cargs = {0};
        pp_list_push(&cargs, pp_strn(mod->d, mod->len));
        for (int i = 0; i < args.n; i++) pp_list_push(&cargs, args.v[i]);
        args.n = 0;
        pp_val v = pp_call(I, impcode, &cargs, CTX_VOID, NULL);
        pp_release(v);
        pp_list_free(&cargs);
    } else builtin_import(I, mod->d, pkg->d, &args, 0);
    pp_list_free(&args);
out:
    pp_str_decref(mod); pp_str_decref(pkg);
    return pp_undef();
}

static void define_const_sym(pp_interp *I, const char *name, pp_obj *code) {
    char buf[128]; snprintf(buf, sizeof buf, "&%s", name);
    pp_define_sub(I, pp_intern(I, buf, (int)strlen(buf)), code);
}

static void define_const_in(pp_interp *I, const char *pkg, const char *name, pp_val v) {
    pp_obj *code = pp_obj_new(PP_OCODE);
    code->u.code.is_const = 1; code->u.code.constval = v;
    pp_define_sub(I, pkg_sym(I, '&', pkg, name), code);
}

static NOINLINE pp_val eval_constdef(pp_interp *I, pp_node *n) {
    pp_list vals = {0};
    pp_node *e = n->a;
    pp_str *pkg = pp_to_str(n->b->val);
    if (e->type == N_ANONHASH) {
        for (int i = 0; i < e->n; i++) { pp_eval(I, e->list[i], CTX_LIST, &vals); if (I->ctl) goto out; }
        for (int i = 0; i + 1 < vals.n; i += 2) {
            pp_str *name = pp_to_str(vals.v[i]);
            define_const_in(I, pkg->d, name->d, pp_copy(vals.v[i + 1]));
            pp_str_decref(name);
        }
    } else {
        pp_eval(I, e, CTX_LIST, &vals);
        if (I->ctl) goto out;
        if (vals.n == 1 && vals.v[0].t == PP_REF && vals.v[0].u.o->kind == PP_OHASH) {
            /* use constant \%hash */
            pp_obj *h = vals.v[0].u.o;
            for (int i = 0; i < h->u.hash.n; i++)
                if (h->u.hash.e[i].klen >= 0) define_const_in(I, pkg->d, h->u.hash.e[i].key, pp_copy(h->u.hash.e[i].v));
        } else if (vals.n >= 1) {
            pp_str *name = pp_to_str(vals.v[0]);
            if (vals.n == 2) define_const_in(I, pkg->d, name->d, pp_copy(vals.v[1]));
            else {
                pp_obj *arr = pp_obj_new(PP_OARRAY);
                for (int i = 1; i < vals.n; i++) pp_array_push(arr, pp_copy(vals.v[i]));
                pp_obj *code = pp_obj_new(PP_OCODE);
                code->u.code.is_const = 2; code->u.code.constval = pp_ref(arr); pp_obj_decref(arr);
                pp_define_sub(I, pkg_sym(I, '&', pkg->d, name->d), code);
            }
            pp_str_decref(name);
        }
    }
out:
    pp_list_free(&vals);
    pp_str_decref(pkg);
    return pp_undef();
}

static NOINLINE pp_val eval_slice(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_list idx = {0}, src = {0};
    pp_eval(I, n->b, CTX_LIST, &idx);
    if (I->ctl) { pp_list_free(&idx); return pp_undef(); }
    pp_list tmp = {0};
    pp_val last = pp_undef();
    if (n->type == N_ASLICE) {
        pp_obj *arr = pp_get_array(I, n->a, 0, NULL);
        if (!arr) { pp_list_free(&idx); return pp_undef(); }
        for (int i = 0; i < idx.n; i++) pp_list_push(&tmp, pp_array_get(arr, (int)pp_to_int(idx.v[i])));
        pp_obj_decref(arr);
    } else if (n->type == N_HSLICE) {
        pp_obj *h = pp_get_hash(I, n->a, 0, NULL);
        if (!h) { pp_list_free(&idx); return pp_undef(); }
        for (int i = 0; i < idx.n; i++) { pp_str *k = pp_to_str(idx.v[i]); pp_list_push(&tmp, pp_hash_get(h, k->d, k->len)); pp_str_decref(k); }
        pp_obj_decref(h);
    } else {
        pp_eval(I, n->a, CTX_LIST, &src);
        if (I->ctl) { pp_list_free(&idx); pp_list_free(&src); return pp_undef(); }
        for (int i = 0; i < idx.n; i++) { int64_t k = pp_to_int(idx.v[i]); if (k < 0) k += src.n; pp_list_push(&tmp, k >= 0 && k < src.n ? pp_copy(src.v[k]) : pp_undef()); }
        pp_list_free(&src);
    }
    pp_list_free(&idx);
    if (ctx == CTX_LIST && out) { for (int i = 0; i < tmp.n; i++) pp_list_push(out, tmp.v[i]); tmp.n = 0; }
    else if (tmp.n) { last = tmp.v[tmp.n - 1]; tmp.n--; }
    pp_list_free(&tmp);
    return last;
}

static NOINLINE pp_val eval_anon(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_list tmp = {0};
    for (int i = 0; i < n->n; i++) { pp_eval(I, n->list[i], CTX_LIST, &tmp); if (I->ctl) { pp_list_free(&tmp); return pp_undef(); } }
    pp_obj *o;
    if (n->type == N_ANONARRAY) {
        o = pp_obj_new(PP_OARRAY);
        for (int i = 0; i < tmp.n; i++) pp_array_push(o, tmp.v[i]);
        tmp.n = 0;
    } else {
        o = pp_obj_new(PP_OHASH);
        if (I->warn_on && (tmp.n & 1)) pp_warn(I, "Odd number of elements in anonymous hash");
        for (int i = 0; i < tmp.n; i += 2) {
            pp_str *k = pp_to_str(tmp.v[i]);
            pp_hash_set(o, k->d, k->len, i + 1 < tmp.n ? pp_copy(tmp.v[i + 1]) : pp_undef());
            pp_str_decref(k);
        }
    }
    pp_list_free(&tmp);
    pp_val r = pp_ref(o); pp_obj_decref(o);
    return scalar_or_push(ctx, out, r);
}

static NOINLINE pp_val eval_anonsub(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_obj *code = pp_obj_new(PP_OCODE);
    code->u.code.body = n->a;
    pp_note_sub_file(I, n->a, I->file);
    code->u.code.env = I->env; if (I->env) pp_env_incref(I->env);
    code->u.code.name = n->sym ? n->sym->name : "__ANON__";   /* "Pkg::__ANON__" outside main */
    code->u.code.block_proto = n->op == 1; code->u.code.pscalar = n->aux;
    pp_val r = pp_ref(code); pp_obj_decref(code);
    return scalar_or_push(ctx, out, r);
}

static NOINLINE pp_val eval_neg(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_val a = pp_eval(I, n->a, CTX_SCALAR, NULL);
    if (I->ctl) return pp_undef();
    pp_val r;
    if (a.t == PP_REF && pp_ovl_hook) {   /* -$obj: "neg", or 0 - $obj through the "-" handler */
        if (pp_ovl_unary(I, a, "neg", &r)) { pp_release(a); return I->ctl ? (pp_release(r), pp_undef()) : scalar_or_push(ctx, out, r); }
        pp_obj *sub = ovl_find(I, a, "-");
        if (sub) { r = ovl_call(I, sub, a, pp_int(0), 1); pp_release(a); return I->ctl ? (pp_release(r), pp_undef()) : scalar_or_push(ctx, out, r); }
    }
    if (a.t == PP_STR && !pp_looks_like_number(a.u.s->d, a.u.s->len) && a.u.s->len > 0) {
        if (a.u.s->d[0] == '-') { r = pp_strn(a.u.s->d, a.u.s->len); r.u.s->d[0] = '+'; }
        else if (a.u.s->d[0] == '+') { r = pp_strn(a.u.s->d, a.u.s->len); r.u.s->d[0] = '-'; }
        else { r = pp_strn("-", 1); r.u.s = pp_str_reserve(r.u.s, a.u.s->len); memcpy(r.u.s->d + 1, a.u.s->d, a.u.s->len); r.u.s->len += a.u.s->len; r.u.s->d[r.u.s->len] = 0; }
    } else {
        pp_val nv = pp_numify(a);
        r = nv.t == PP_INT && nv.u.i != INT64_MIN ? pp_int(-nv.u.i) : pp_num(-(nv.t == PP_INT ? (double)nv.u.i : nv.u.n));
    }
    pp_release(a);
    return scalar_or_push(ctx, out, r);
}

static NOINLINE pp_val eval_not(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_val a = pp_eval(I, n->a, CTX_SCALAR, NULL);
    if (I->ctl) return pp_undef();
    if (a.t == PP_REF && pp_ovl_hook) {   /* !$obj: "!" when there is one, else from its truth */
        pp_val r;
        if (pp_ovl_unary(I, a, "!", &r)) { pp_release(a); return I->ctl ? (pp_release(r), pp_undef()) : scalar_or_push(ctx, out, r); }
    }
    int t = pp_truth(a); pp_release(a);
    return scalar_or_push(ctx, out, truth_val(!t));
}

static NOINLINE pp_val eval_logop(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_val a = pp_eval(I, n->a, CTX_SCALAR, NULL);
    if (I->ctl) return pp_undef();
    int take_a = n->type == N_LOGAND ? !pp_truth(a) : n->type == N_LOGOR ? pp_truth(a) : a.t != PP_UNDEF;
    if (take_a) return scalar_or_push(ctx, out, a);
    pp_release(a);
    return pp_eval(I, n->b, ctx, out);
}

static NOINLINE pp_val eval_incdec(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_val *slot = pp_lvalue(I, n->a);
    if (!slot) return pp_undef();
    int delta = (n->type == N_PREINC || n->type == N_POSTINC) ? 1 : -1;
    pp_val old = *slot;
    pp_val nv;
    if (old.t == PP_REF && pp_ovl_hook && old.u.o->class) {
        /* $obj++: the "++" handler changes the object in place; without one it is $obj + 1 */
        int post = n->type == N_POSTINC || n->type == N_POSTDEC;
        pp_val keep = pp_copy(old), r;
        if (pp_ovl_unary(I, keep, delta > 0 ? "++" : "--", &r)) { pp_release(r); if (!post) return scalar_or_push(ctx, out, keep); pp_release(keep); return scalar_or_push(ctx, out, pp_undef()); }
        if (ovl_binop(I, delta > 0 ? OP_ADD : OP_SUB, keep, pp_int(1), &r)) {
            if (I->ctl) { pp_release(r); pp_release(keep); return pp_undef(); }
            slot = pp_lvalue(I, n->a);
            if (!slot) { pp_release(r); pp_release(keep); return pp_undef(); }
            pp_release(*slot); *slot = r;
            if (post) return scalar_or_push(ctx, out, keep);
            pp_release(keep);
            return scalar_or_push(ctx, out, pp_copy(r));
        }
        pp_release(keep);
    }
    nv = incdec(old, delta);
    *slot = nv;
    if (n->type == N_POSTINC || n->type == N_POSTDEC) {
        if (old.t == PP_UNDEF) { pp_release(old); old = pp_int(0); }
        return scalar_or_push(ctx, out, old);
    }
    pp_release(old);
    return scalar_or_push(ctx, out, pp_copy(nv));
}

static NOINLINE pp_val eval_repeat(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    if (n->op == 1 && ctx == CTX_LIST && out) {
        pp_list items = {0};
        pp_eval(I, n->a, CTX_LIST, &items);
        if (I->ctl) { pp_list_free(&items); return pp_undef(); }
        pp_val cv = pp_eval(I, n->b, CTX_SCALAR, NULL);
        if (I->ctl) { pp_list_free(&items); return pp_undef(); }
        int64_t k = pp_to_int(cv); pp_release(cv);
        for (int64_t r = 0; r < k; r++) for (int i = 0; i < items.n; i++) pp_list_push(out, pp_copy(items.v[i]));
        pp_list_free(&items);
        return pp_undef();
    }
    pp_val a = pp_eval(I, n->a, CTX_SCALAR, NULL);
    if (I->ctl) return pp_undef();
    pp_val b = pp_eval(I, n->b, CTX_SCALAR, NULL);
    if (I->ctl) { pp_release(a); return pp_undef(); }
    pp_val r = do_binop(I, OP_REPEAT, a, b);
    pp_release(a); pp_release(b);
    return scalar_or_push(ctx, out, r);
}

static NOINLINE pp_val eval_evalblock(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_env *saved_env = I->env; int saved_depth = I->depth, saved_ctx = I->call_ctx;
    int lh = I->nlocals;
    int outn = out ? out->n : 0;
    I->call_ctx = ctx;                           /* return and wantarray inside see the eval's context */
    /* eval { } is a frame of its own for caller(), as in perl: "(eval)", called at the line of
       the eval. Carp and Test::Builder count frames with that in mind. */
    struct pp_frame fr = { I->frame, I->cur_code, I->file_pkg, I->file, NULL, I->line, ctx, NULL, 0 };
    I->frame = &fr;
    pp_val r = exec_block(I, n->a, ctx, out);
    I->frame = fr.up;
    I->call_ctx = saved_ctx;
    if (I->ctl == CTL_RETURN) {
        /* `return` inside eval { } leaves the eval, not the sub around it: the returned
           values are the value of the eval */
        I->ctl = CTL_NONE;
        pp_release(r);
        pp_list rv = I->ret;
        memset(&I->ret, 0, sizeof I->ret);
        if (ctx == CTX_LIST && out) {
            for (int i = 0; i < rv.n; i++) pp_list_push(out, rv.v[i]);
            rv.n = 0;
            r = pp_undef();
        } else {
            r = rv.n ? rv.v[rv.n - 1] : pp_undef();
            if (rv.n) rv.n--;
        }
        pp_list_free(&rv);
    }
    if (I->ctl == CTL_DIE) {
        I->ctl = CTL_NONE;
        pp_release(r); r = pp_undef();
        if (out) { for (int i = outn; i < out->n; i++) pp_release(out->v[i]); out->n = outn; }
        pp_release(I->g_at->u.scalar);
        I->g_at->u.scalar = I->err; I->err = pp_undef();
        I->env = saved_env; I->depth = saved_depth; I->call_ctx = saved_ctx;
        local_unwind(I, lh);
    } else {
        pp_release(I->g_at->u.scalar);
        I->g_at->u.scalar = pp_strn("", 0);
    }
    return r;
}

/* eval STRING: parse the string now and run it like a small file. Its file scope starts with
   the `my` variables that were visible at the eval (the same cells, so assignments are seen
   outside). Errors, including syntax errors, end up in $@. */
static NOINLINE pp_val eval_evalstr(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_val sv = n->a ? pp_eval(I, n->a, CTX_SCALAR, NULL) : pp_copy(I->g_underscore->u.scalar);
    if (I->ctl) { pp_release(sv); return pp_undef(); }
    pp_str *src = pp_to_str(sv);
    pp_release(sv);
    char namebuf[32];
    int nl = pp_snprintf(namebuf, sizeof namebuf, "(eval %d)", ++I->eval_seq);
    char *fname = pp_xmalloc(nl + 1); memcpy(fname, namebuf, nl + 1);
    /* like a required file: the code being run keeps its own list of subs to define */
    pp_node **outer_subs = I->subdefs; int outer_nsubs = I->nsubdefs;
    pp_env *outer_pending = I->pending_env;
    I->subdefs = NULL; I->nsubdefs = 0; I->pending_env = NULL;
    char errbuf[400];
    pp_arena *arena = NULL;                      /* the code's AST lives here */
    int subs_before = I->nall_subdefs;
    {   /* the variables the string can see, in place before it is parsed: a `use` or BEGIN in
           the string runs while parsing and may read them (use Module @{$args[0]}) */
        int nlex = 0;
        for (int i = 0; i < n->n; i++) if (n->list[i]->flags & PP_VAR_LEX) nlex++;
        if (nlex) {
            pp_env *e = pp_env_new(NULL, nlex);
            int slot = 0;
            for (int i = 0; i < n->n; i++) {
                if (!(n->list[i]->flags & PP_VAR_LEX)) continue;
                pp_obj *cell = var_cell(I, n->list[i]);
                cell->refcnt++;
                if (e->cells[slot]) pp_obj_decref(e->cells[slot]);
                e->cells[slot++] = cell;
            }
            I->pending_env = e;
        }
    }
    pp_node *unit = pp_parse_eval(I, fname, src->d, n, errbuf, sizeof errbuf, &arena);
    pp_str_decref(src);
    pp_val r = pp_undef();
    if (!unit) {
        if (I->pending_env) pp_env_decref(I->pending_env);
        pp_release(I->g_at->u.scalar);
        I->g_at->u.scalar = pp_cstr(errbuf);
        free(fname);
    } else {
        pp_env *e = I->pending_env ? I->pending_env : pp_env_new(NULL, unit->aux);
        pp_env_grow(e, unit->aux);
        int slot = 0;
        for (int i = 0; i < n->n; i++) {
            if (!(n->list[i]->flags & PP_VAR_LEX)) continue;
            pp_obj *cell = var_cell(I, n->list[i]);
            cell->refcnt++;
            if (e->cells[slot]) pp_obj_decref(e->cells[slot]);
            e->cells[slot++] = cell;
        }
        I->pending_env = e;                      /* pp_exec_unit runs the code in it */
        pp_env *saved_env = I->env; int saved_depth = I->depth, saved_ctx = I->call_ctx;
        const char *saved_file = I->file; int saved_line = I->line;
        int lh = I->nlocals, outn = out ? out->n : 0;
        I->file = fname;
        I->call_ctx = ctx;                       /* return and wantarray inside see the eval's context */
        struct pp_frame efr = { I->frame, I->cur_code, I->file_pkg, saved_file, NULL, saved_line, ctx, NULL, 0 };   /* a frame for caller(), like eval { } */
        I->frame = &efr;
        r = pp_exec_unit(I, unit, ctx, out);
        I->frame = efr.up;
        I->file = saved_file; I->line = saved_line; I->call_ctx = saved_ctx;
        if (I->ctl == CTL_DIE) {
            I->ctl = CTL_NONE;
            pp_release(r); r = pp_undef();
            if (out) { for (int i = outn; i < out->n; i++) pp_release(out->v[i]); out->n = outn; }
            pp_release(I->g_at->u.scalar);
            I->g_at->u.scalar = I->err; I->err = pp_undef();
            I->env = saved_env; I->depth = saved_depth; I->call_ctx = saved_ctx;
            local_unwind(I, lh);
        } else {
            if (I->ctl == CTL_RETURN) {          /* return inside the string ends the eval */
                I->ctl = CTL_NONE;
                pp_release(r);
                pp_list rv = I->ret;
                memset(&I->ret, 0, sizeof I->ret);
                if (ctx == CTX_LIST && out) { for (int i = 0; i < rv.n; i++) pp_list_push(out, rv.v[i]); rv.n = 0; r = pp_undef(); }
                else { r = rv.n ? rv.v[rv.n - 1] : pp_undef(); if (rv.n) rv.n--; }
                pp_list_free(&rv);
            }
            pp_release(I->g_at->u.scalar);
            I->g_at->u.scalar = pp_strn("", 0);
        }
        /* statements that made no sub or closure were released as they ran. If nothing made
           one, the whole AST goes away now (an eval in a loop must not pile up memory);
           otherwise it stays with the interpreter. */
        int kept = I->nall_subdefs != subs_before;
        for (int i = 0; i < unit->n; i++) if (unit->list[i]) kept = 1;
        if (kept) { pp_arena_absorb(I->arena, arena); track_unit(I, unit, fname); }
        else { pp_free_ast(unit); pp_arena_free(arena); free(fname); }
    }
    free(I->subdefs);
    I->subdefs = outer_subs; I->nsubdefs = outer_nsubs; I->pending_env = outer_pending;
    return r;
}

/* try/catch/finally: like eval {}, but return/last/next escape through the try
 * block, $@ stays empty inside catch (the error is in the catch variable), and
 * finally always runs, preserving whatever control flow is pending. */
static NOINLINE pp_val eval_try(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    pp_env *saved_env = I->env; int saved_depth = I->depth, saved_ctx = I->call_ctx;
    int lh = I->nlocals;
    pp_val r = exec_block(I, n->a, CTX_VOID, NULL);
    pp_release(r);
    if (I->ctl == CTL_DIE) {
        I->ctl = CTL_NONE;
        pp_release(I->try_err);
        I->try_err = I->err; I->err = pp_undef();
        pp_release(I->g_at->u.scalar);
        I->g_at->u.scalar = pp_strn("", 0);       /* like perl: $@ is empty inside catch */
        I->env = saved_env; I->depth = saved_depth; I->call_ctx = saved_ctx;
        local_unwind(I, lh);
        pp_val c = exec_block(I, n->b, CTX_VOID, NULL);
        pp_release(c);
        pp_release(I->try_err);
        I->try_err = pp_undef();
    }
    if (n->c) {                                   /* finally */
        int ctl = I->ctl; I->ctl = CTL_NONE;
        pp_val err = I->err; I->err = pp_undef();
        pp_list ret = I->ret; I->ret.v = NULL; I->ret.n = I->ret.cap = 0;
        pp_val f = exec_block(I, n->c, CTX_VOID, NULL);
        pp_release(f);
        if (I->ctl) { pp_release(err); pp_list_free(&ret); }   /* die/return in finally wins */
        else { I->ctl = ctl; pp_release(I->err); I->err = err; pp_list_free(&I->ret); I->ret = ret; }
    }
    return pp_undef();
}

static NOINLINE pp_val eval_return(pp_interp *I, pp_node *n) {
    pp_list rv = {0};
    if (n->a) {
        if (I->call_ctx == CTX_LIST) pp_eval(I, n->a, CTX_LIST, &rv);
        else { pp_val v = pp_eval(I, n->a, CTX_SCALAR, NULL); if (!I->ctl) pp_list_push(&rv, v); else pp_release(v); }
        if (I->ctl) { pp_list_free(&rv); return pp_undef(); }
    }
    pp_list_free(&I->ret);
    I->ret = rv;
    I->ctl = CTL_RETURN;
    return pp_undef();
}

/* $! as a number: the cell holds the number itself, a numeric string, or a message (set by a
   failed I/O call, or by `$! = $saved` where $saved was read as a string) that is looked up */
static int errno_number(pp_val v) {
    if (v.t == PP_INT || v.t == PP_NUM) return (int)pp_to_int(v);
    if (v.t != PP_STR || v.u.s->len == 0) return 0;
    if (pp_looks_like_number(v.u.s->d, v.u.s->len)) return (int)pp_to_int(v);
    for (int e = 1; e < 200; e++) {
        const char *m = pp_host_strerror(e);
        if (m && !strcmp(m, v.u.s->d)) return e;
    }
    return 0;
}
static NOINLINE pp_val eval_errno(pp_node *n, pp_val v) {
    if (n->flags & PP_VAR_ERRNUM) return pp_int(errno_number(v));
    if (v.t == PP_STR && !pp_looks_like_number(v.u.s->d, v.u.s->len)) return pp_copy(v);   /* already a message */
    int e = errno_number(v);
    const char *m = e ? pp_host_strerror(e) : "";
    if (m) return pp_cstr(m);
    char buf[32];
    int len = pp_snprintf(buf, sizeof buf, "Unknown error %d", e);
    return pp_strn(buf, len);
}
static NOINLINE pp_val eval_var(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_obj *c = var_cell(I, n);
    switch (n->sym->name[0]) {
    case '$':
        if (c->tied == 1) return scalar_or_push(ctx, out, tie_fetch_scalar(I, c));
        if (n->flags & PP_VAR_ERRNO) return scalar_or_push(ctx, out, eval_errno(n, c->u.scalar));
        return scalar_or_push(ctx, out, pp_copy(c->u.scalar));
    case '@':
        if (c->tied) pp_tie_sync(I, c);
        if (ctx == CTX_LIST && out) { for (int i = 0; i < c->u.arr.n; i++) pp_list_push(out, pp_copy(c->u.arr.v[i])); return pp_undef(); }
        return pp_int(c->u.arr.n);
    case '%':
        if (c->tied) pp_tie_sync(I, c);
        if (ctx == CTX_LIST && out) {
            for (int i = 0; i < c->u.hash.n; i++) if (c->u.hash.e[i].klen >= 0) { pp_list_push(out, pp_strn(c->u.hash.e[i].key, c->u.hash.e[i].klen)); pp_list_push(out, pp_copy(c->u.hash.e[i].v)); }
            return pp_undef();
        }
        return pp_int(c->u.hash.live);
    }
    return pp_undef();
}

static NOINLINE pp_val eval_my(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    declare_my(I, n);
    if (n->n == 1 && n->list[0]->type == N_VAR && ctx != CTX_VOID) return pp_eval(I, n->list[0], ctx, out);
    if (ctx == CTX_LIST && out) for (int i = 0; i < n->n; i++) if (n->list[i]->type == N_VAR) pp_eval(I, n->list[i], ctx, out);
    return pp_undef();
}

static NOINLINE pp_val eval_elem(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    if (n->type == N_AELEM) {
        pp_val iv = pp_eval(I, n->b, CTX_SCALAR, NULL);
        if (I->ctl) return pp_undef();
        int64_t idx = pp_to_int(iv); pp_release(iv);
        pp_obj *arr = pp_get_array_raw(I, n->a, 0);
        if (!arr) return pp_undef();
        pp_val v = arr->tied == 1 ? tie_fetch_elem(I, arr, pp_int(idx)) : pp_array_get(arr, (int)idx);
        pp_obj_decref(arr);
        return scalar_or_push(ctx, out, v);
    }
    pp_str *k = eval_key(I, n->b);
    if (!k) return pp_undef();
    pp_obj *h = pp_get_hash_raw(I, n->a, 0);
    if (!h) { pp_str_decref(k); return pp_undef(); }
    pp_val v;
    if (h->tied == 1) { k->refcnt++; v = tie_fetch_elem(I, h, (pp_val){ .t = PP_STR, .u.s = k }); }
    else v = pp_hash_get(h, k->d, k->len);
    pp_obj_decref(h); pp_str_decref(k);
    return scalar_or_push(ctx, out, v);
}

static NOINLINE pp_val eval_rv2(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    if (n->type == N_RV2AV) {
        pp_obj *arr = pp_get_array(I, n, 0, NULL);
        if (!arr) return pp_undef();
        pp_val r;
        if (ctx == CTX_LIST && out) { for (int i = 0; i < arr->u.arr.n; i++) pp_list_push(out, pp_copy(arr->u.arr.v[i])); r = pp_undef(); }
        else r = pp_int(arr->u.arr.n);
        pp_obj_decref(arr);
        return r;
    }
    if (n->type == N_RV2HV) {
        pp_obj *h = pp_get_hash(I, n, 0, NULL);
        if (!h) return pp_undef();
        pp_val r;
        if (ctx == CTX_LIST && out) {
            for (int i = 0; i < h->u.hash.n; i++) if (h->u.hash.e[i].klen >= 0) { pp_list_push(out, pp_strn(h->u.hash.e[i].key, h->u.hash.e[i].klen)); pp_list_push(out, pp_copy(h->u.hash.e[i].v)); }
            r = pp_undef();
        } else r = pp_int(h->u.hash.live);
        pp_obj_decref(h);
        return r;
    }
    pp_val v = pp_eval(I, n->a, CTX_SCALAR, NULL);
    if (I->ctl) return pp_undef();
    if (v.t == PP_REF && v.u.o->kind == PP_OSCALAR) { pp_val r = v.u.o->tied == 1 ? tie_fetch_scalar(I, v.u.o) : pp_copy(v.u.o->u.scalar); pp_release(v); return scalar_or_push(ctx, out, r); }
    if (is_globref(v)) { pp_val r = pp_copy(v.u.o->u.glob.name); pp_release(v); return scalar_or_push(ctx, out, r); }   /* $$globref is the glob */
    if (is_symname(v)) {
        pp_sym *s = pp_symref(I, n, v, '$', 1); pp_release(v);
        if (!s) return pp_undef();
        return scalar_or_push(ctx, out, pp_copy(pp_global(I, s)->u.scalar));
    }
    pp_release(v);
    return pp_die(I, "Not a SCALAR reference");
}

static NOINLINE pp_val eval_lastidx(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_obj *arr = pp_get_array(I, n->a, 0, NULL);
    if (!arr) return pp_undef();
    pp_val r = pp_int(arr->u.arr.n - 1);
    pp_obj_decref(arr);
    return scalar_or_push(ctx, out, r);
}

static NOINLINE pp_val eval_binop_node(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_val a = pp_eval(I, n->a, CTX_SCALAR, NULL);
    if (I->ctl) return pp_undef();
    pp_val b = pp_eval(I, n->b, CTX_SCALAR, NULL);
    if (I->ctl) { pp_release(a); return pp_undef(); }
    if (I->warn_on) {
        if (n->op == OP_REPEAT) { warn_operand(I, n->op, a, var_name_of(n->a)); if (b.t == PP_UNDEF) warn_uninit(I, "repeat (x)", var_name_of(n->b)); }
        else { warn_operand(I, n->op, a, var_name_of(n->a)); warn_operand(I, n->op, b, var_name_of(n->b)); }
    }
    pp_val r = n->aux ? int_binop(I, n->op, a, b) : do_binop(I, n->op, a, b);
    pp_release(a); pp_release(b);
    if (I->ctl) { pp_release(r); return pp_undef(); }
    return scalar_or_push(ctx, out, r);
}

static NOINLINE pp_val eval_list_node(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    if (ctx == CTX_LIST && out) {
        for (int i = 0; i < n->n; i++) { pp_eval(I, n->list[i], CTX_LIST, out); if (I->ctl) return pp_undef(); }
        return pp_undef();
    }
    pp_val last = pp_undef();
    for (int i = 0; i < n->n; i++) {
        pp_release(last);
        last = pp_eval(I, n->list[i], i == n->n - 1 ? CTX_SCALAR : CTX_VOID, NULL);
        if (I->ctl) { pp_release(last); return pp_undef(); }
    }
    if (ctx == CTX_VOID) { pp_release(last); return pp_undef(); }
    return last;
}

static NOINLINE pp_val eval_assign_node(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_val r = eval_assign(I, n, ctx, out);
    if (I->ctl) { pp_release(r); return pp_undef(); }
    if (ctx == CTX_LIST && out) {
        if (!is_list_lhs(n->a) || n->op != OP_NONE) pp_list_push(out, r);
        else pp_release(r);
        return pp_undef();
    }
    return r;
}

static NOINLINE pp_val eval_subdef(pp_interp *I, pp_node *n) {
    pp_obj *code = pp_obj_new(PP_OCODE);
    code->u.code.body = n->a;
    pp_note_sub_file(I, n->a, I->file);
    code->u.code.env = I->env; if (I->env) pp_env_incref(I->env);
    code->u.code.name = n->sym->name + 1;
    code->u.code.block_proto = n->op == 1; code->u.code.pscalar = n->aux;
    pp_define_sub(I, n->sym, code);
    return pp_undef();
}

/* find &Class::name following @Class::ISA depth-first */
static pp_obj *find_method(pp_interp *I, const char *cls, const char *name, int depth) {
    char buf[256];
    int is_main = !strcmp(cls, "main");          /* main's subs and @ISA have no package prefix */
    int n = is_main ? pp_snprintf(buf, sizeof buf, "&%s", name) : pp_snprintf(buf, sizeof buf, "&%s::%s", cls, name);
    pp_obj *code = pp_resolve_sub(I, pp_intern(I, buf, n));
    if (is_main && code && code->u.code.native) code = NULL;   /* chdir and friends are builtins, not methods */
    if (code || depth > 16) return code;
    n = is_main ? pp_snprintf(buf, sizeof buf, "@ISA") : pp_snprintf(buf, sizeof buf, "@%s::ISA", cls);
    pp_sym *isa = pp_intern(I, buf, n);
    if (!isa->gcell) return NULL;
    pp_obj *arr = isa->gcell;
    for (int i = 0; i < arr->u.arr.n; i++) {
        pp_str *parent = pp_to_str(arr->u.arr.v[i]);
        code = find_method(I, parent->d, name, depth + 1);
        pp_str_decref(parent);
        if (code) return code;
    }
    return NULL;
}
static int class_isa(pp_interp *I, const char *cls, const char *want, int depth) {
    if (strcmp(cls, want) == 0) return 1;
    if (depth > 16) return 0;
    char buf[256]; int n = !strcmp(cls, "main") ? pp_snprintf(buf, sizeof buf, "@ISA") : pp_snprintf(buf, sizeof buf, "@%s::ISA", cls);
    pp_sym *isa = pp_intern(I, buf, n);
    if (!isa->gcell) return 0;
    pp_obj *arr = isa->gcell;
    for (int i = 0; i < arr->u.arr.n; i++) {
        pp_str *parent = pp_to_str(arr->u.arr.v[i]);
        int r = class_isa(I, parent->d, want, depth + 1);
        pp_str_decref(parent);
        if (r) return 1;
    }
    return 0;
}

/* tie support: obj->METHOD(args) where obj is a tie object, or CLASS->METHOD(args) for a
   class name. The args are consumed. Without the method: an error when `must`, else undef
   with *args left alone (the caller falls back to its own default). */
pp_val pp_tie_call(pp_interp *I, pp_val obj, const char *method, pp_list *args, int ctx, pp_list *out, int must) {
    pp_str *cs = (obj.t == PP_REF && obj.u.o->class) ? NULL : pp_to_str(obj);
    const char *cls = cs ? cs->d : obj.u.o->class->name;
    pp_obj *code = find_method(I, cls, method, 0);
    if (!code) {
        pp_val e = must ? pp_die(I, "Can't locate object method \"%s\" via package \"%s\"", method, cls) : pp_undef();
        if (cs) pp_str_decref(cs);
        if (must && args) pp_list_free(args);
        return e;
    }
    if (cs) pp_str_decref(cs);
    pp_list full = {0};
    pp_list_push(&full, pp_copy(obj));
    if (args) { for (int i = 0; i < args->n; i++) pp_list_push(&full, args->v[i]); args->n = 0; pp_list_free(args); }
    code->refcnt++;
    pp_val r = pp_call(I, code, &full, ctx, out);
    pp_list_free(&full);
    pp_obj_decref(code);
    return r;
}

/* a version string as numbers: "v1.2.3" and "1.2.3" by their parts, a decimal "1.25" as
   1.250 (three digits per part, as perl reads it); underscores are ignored */
static void version_parts(const char *s, long *part, int max) {
    for (int i = 0; i < max; i++) part[i] = 0;
    while (*s == ' ') s++;
    int dotted = *s == 'v', dots = 0;
    if (dotted) s++;
    for (const char *p = s; *p; p++) if (*p == '.') dots++;
    if (dots >= 2) dotted = 1;
    int k = 0;
    while (*s && k < max) {
        long v = 0; int digits = 0;
        while ((isdigit((unsigned char)*s) || *s == '_') && (dotted || k == 0 || digits < 3)) { if (*s != '_') { v = v * 10 + (*s - '0'); digits++; } s++; }
        if (!dotted && k > 0) while (digits < 3) { v *= 10; digits++; }   /* .25 is .250 */
        part[k++] = v;
        if (*s == '.') s++;
        else if (!isdigit((unsigned char)*s)) break;
    }
}
static int version_cmp(const char *a, const char *b) {
    long pa[6], pb[6];
    version_parts(a, pa, 6); version_parts(b, pb, 6);
    for (int i = 0; i < 6; i++) if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    return 0;
}

/* methods on file handles come from IO::File: make sure it is loaded. 0 when it is not
   available (the board without IO/File.pm on the drive): handles then have no methods. */
static pp_val do_require(pp_interp *I, const char *fn, int force);
static NOINLINE int handle_class_ready(pp_interp *I) {
    if (pp_resolve_sub(I, pp_intern(I, "&IO::Handle::new", 16))) return 1;
    if (I->ctl) return 0;
    pp_val saved_at = pp_copy(I->g_at->u.scalar);
    pp_val r = do_require(I, "IO/File.pm", 0);
    pp_release(r);
    int ok = !I->ctl;
    if (I->ctl == CTL_DIE) { I->ctl = CTL_NONE; pp_release(I->err); I->err = pp_undef(); }
    pp_release(I->g_at->u.scalar); I->g_at->u.scalar = saved_at;
    return ok;
}
static NOINLINE pp_val eval_method(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    pp_val inv = pp_eval(I, n->a, CTX_SCALAR, NULL);
    if (I->ctl) return pp_undef();
    /* class name of the invocant */
    char cls[128];
    if (inv.t == PP_REF) {
        if (inv.u.o->kind == PP_OCODE && !inv.u.o->class) { /* $coderef->method not supported */ }
        if (!inv.u.o->class) {
            /* a file handle ($fh->print, $fh->autoflush): its methods are those of IO::File,
               loaded on first use as perl does. Without IO::File there are none. */
            if ((inv.u.o->kind == PP_OFILE || inv.u.o->kind == PP_OGLOB) && handle_class_ready(I)) snprintf(cls, sizeof cls, "IO::File");
            else { pp_release(inv); return pp_die(I, "Can't call method \"%s\" on unblessed reference", n->sym ? n->sym->name : "?"); }
        }
        else snprintf(cls, sizeof cls, "%s", inv.u.o->class->name);
    } else {
        pp_str *s = pp_to_str(inv);
        if (s->len == 0) { pp_str_decref(s); pp_release(inv); return pp_die(I, "Can't call method \"%s\" on an undefined value", n->sym ? n->sym->name : "?"); }
        snprintf(cls, sizeof cls, "%s", s->d);
        {   /* STDOUT->flush, FH->close: a bareword that names an open handle */
            const char *hn = s->d[0] == '*' ? s->d + 1 : s->d;
            if (!strncmp(hn, "main::", 6)) hn += 6;
            int std = !strcmp(hn, "STDIN") || !strcmp(hn, "STDOUT") || !strcmp(hn, "STDERR");
            if (!std && s->d[0] != '*' && s->len < 100) {
                char gb[128]; int gl = pp_snprintf(gb, sizeof gb, "*%s", s->d);
                pp_sym *gs = pp_sym_find(I, gb, gl);
                std = gs && gs->gcell && gs->gcell->kind == PP_OGLOB && gs->gcell->u.glob.io;
            }
            if (std && handle_class_ready(I)) snprintf(cls, sizeof cls, "IO::File");
        }
        pp_str_decref(s);
    }
    /* method name: static, or from $obj->$name (string or code ref) */
    const char *name = n->sym ? n->sym->name : NULL;
    pp_str *dyn = NULL; pp_obj *code = NULL;
    if (!name) {
        pp_val m = pp_eval(I, n->b, CTX_SCALAR, NULL);
        if (I->ctl) { pp_release(inv); return pp_undef(); }
        if (m.t == PP_REF && m.u.o->kind == PP_OCODE) { code = m.u.o; code->refcnt++; }
        else dyn = pp_to_str(m);
        pp_release(m);
        if (dyn) name = dyn->d;
    }
    pp_list args = {0};
    pp_list_push(&args, inv);
    for (int i = 0; i < n->n; i++) { pp_eval(I, n->list[i], CTX_LIST, &args); if (I->ctl) { pp_list_free(&args); if (dyn) pp_str_decref(dyn); if (code) pp_obj_decref(code); return pp_undef(); } }
    pp_val r = pp_undef();
    if (!code) {
        const char *start = cls;
        const char *mname = name;
        if (strncmp(name, "SUPER::", 7) == 0) {
            /* SUPER:: searches the parents of the package the call was written in */
            mname = name + 7;
            pp_str *cur = pp_to_str(n->c->val);
            char isa[256]; int in = pp_snprintf(isa, sizeof isa, "@%s::ISA", cur->d);
            pp_sym *isas = pp_intern(I, isa, in);
            if (isas->gcell) for (int i = 0; i < isas->gcell->u.arr.n && !code; i++) {
                pp_str *parent = pp_to_str(isas->gcell->u.arr.v[i]);
                code = find_method(I, parent->d, mname, 0);
                pp_str_decref(parent);
            }
            pp_str_decref(cur);
            start = NULL;
        } else if (strstr(name, "::")) {
            /* $obj->Pkg::method(...): start the search in Pkg, whatever the object's class is */
            const char *last = NULL;
            for (const char *p = name; *p; p++) if (p[0] == ':' && p[1] == ':') last = p;
            char pkgbuf[200];
            snprintf(pkgbuf, sizeof pkgbuf, "%.*s", (int)(last - name), name);
            code = find_method(I, pkgbuf, last + 2, 0);
        } else code = find_method(I, start, mname, 0);
        if (code) code->refcnt++;
        else if (strcmp(mname, "isa") == 0) {
            pp_str *want = args.n > 1 ? pp_to_str(args.v[1]) : pp_str_new("", 0);
            r = class_isa(I, cls, want->d, 0) ? pp_int(1) : pp_strn("", 0);
            pp_str_decref(want);
            pp_list_free(&args); if (dyn) pp_str_decref(dyn);
            return scalar_or_push(ctx, out, r);
        } else if (strcmp(mname, "VERSION") == 0) {
            /* Class->VERSION: $Class::VERSION; Class->VERSION(N) also dies when it is older than N */
            char vb[256];
            int vn = !strcmp(cls, "main") ? pp_snprintf(vb, sizeof vb, "$VERSION") : pp_snprintf(vb, sizeof vb, "$%s::VERSION", cls);
            pp_sym *vs = pp_intern(I, vb, vn);
            pp_val have = vs->gcell ? pp_copy(vs->gcell->u.scalar) : pp_undef();
            pp_val e = pp_undef();
            if (args.n > 1 && args.v[1].t != PP_UNDEF) {
                pp_str *want = pp_to_str(args.v[1]);
                if (have.t == PP_UNDEF) e = pp_die(I, "%s does not define $%s::VERSION--version check failed", cls, cls);
                else {
                    pp_str *hs = pp_to_str(have);
                    if (version_cmp(hs->d, want->d) < 0) e = pp_die(I, "%s version %s required--this is only version %s", cls, want->d, hs->d);
                    pp_str_decref(hs);
                }
                pp_str_decref(want);
            }
            pp_list_free(&args); if (dyn) pp_str_decref(dyn);
            if (I->ctl) { pp_release(have); return e; }
            return scalar_or_push(ctx, out, have);
        } else if (strcmp(mname, "import") == 0 || strcmp(mname, "unimport") == 0) {
            /* Class->import(LIST) for a class without an import of its own (use Exporter 'import'
               is not loaded here): the built-in export into the calling package. With nothing
               to export it does nothing, like perl's missing import. */
            if (mname[0] == 'i' && n->c && n->c->type == N_CONST && n->c->val.t == PP_STR) builtin_import(I, cls, n->c->val.u.s->d, &args, 1);
            pp_list_free(&args); if (dyn) pp_str_decref(dyn);
            return pp_undef();
        } else if (strcmp(mname, "can") == 0) {
            pp_str *want = args.n > 1 ? pp_to_str(args.v[1]) : pp_str_new("", 0);
            pp_obj *m = find_method(I, cls, want->d, 0);
            r = m ? pp_ref(m) : pp_undef();
            pp_str_decref(want);
            pp_list_free(&args); if (dyn) pp_str_decref(dyn);
            return scalar_or_push(ctx, out, r);
        } else if ((code = autoload_method(I, cls, mname)) != NULL) {
            code->refcnt++;                          /* the class (or a parent) has an AUTOLOAD: it gets the call */
        } else {
            pp_list_free(&args);
            const char *q = NULL;                    /* $obj->Pkg::method: the search was in Pkg */
            if (strncmp(mname, "SUPER::", 7)) for (const char *p = mname; *p; p++) if (p[0] == ':' && p[1] == ':') q = p;
            pp_val e = q ? pp_die(I, "Can't locate object method \"%s\" via package \"%.*s\"", q + 2, (int)(q - mname), mname)
                         : pp_die(I, "Can't locate object method \"%s\" via package \"%s\"", mname, cls);
            if (dyn) pp_str_decref(dyn);
            return e;
        }
    }
    I->line = n->line;
    r = pp_call(I, code, &args, ctx, out);
    if (I->wb_code == code) args_writeback(I, n->a, n->list, n->n);
    pp_list_free(&args);
    pp_obj_decref(code);
    if (dyn) pp_str_decref(dyn);
    return r;
}

static NOINLINE pp_val stack_overflow(pp_interp *I) {
    I->stack_base = NULL;   /* disable the check while reporting */
    return pp_die(I, "Out of stack space (expression or call nesting too deep)");
}

/* ------------------------------------------------------------------ */
/* main eval                                                           */
/* ------------------------------------------------------------------ */

pp_val pp_eval(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    if (I->ctl) return pp_undef();
#ifndef __SANITIZE_ADDRESS__    /* ASan's fake stack breaks the address arithmetic; ASan detects overflows itself */
    if (I->stack_base) {
        char here;
        size_t used = (size_t)(I->stack_base > &here ? I->stack_base - &here : &here - I->stack_base);
        if (used > I->stack_limit) return stack_overflow(I);
    }
#endif
    switch (n->type) {
    case N_CONST: return scalar_or_push(ctx, out, pp_copy(n->val));
    case N_INTERP: return eval_interp(I, n, ctx, out);
    case N_VAR: return eval_var(I, n, ctx, out);
    case N_MY: return eval_my(I, n, ctx, out);
    case N_AELEM: case N_HELEM: return eval_elem(I, n, ctx, out);
    case N_ASLICE: case N_HSLICE: case N_LSLICE: return eval_slice(I, n, ctx, out);
    case N_RV2AV: case N_RV2HV: case N_RV2SV: return eval_rv2(I, n, ctx, out);
    case N_LASTIDX: return eval_lastidx(I, n, ctx, out);
    case N_ANONARRAY: case N_ANONHASH: return eval_anon(I, n, ctx, out);
    case N_ANONSUB: return eval_anonsub(I, n, ctx, out);
    case N_REF: {
        pp_val r = make_ref(I, n->a, ctx, out);
        if (I->ctl) { pp_release(r); return pp_undef(); }
        if (r.t == PP_UNDEF) return r;
        return scalar_or_push(ctx, out, r);
    }
    case N_LIST: return eval_list_node(I, n, ctx, out);
    case N_RANGE: return eval_range(I, n, ctx, out);
    case N_ASSIGN: return eval_assign_node(I, n, ctx, out);
    case N_BINOP: return eval_binop_node(I, n, ctx, out);
    case N_LOGAND: case N_LOGOR: case N_DEFOR: return eval_logop(I, n, ctx, out);
    case N_NOT: return eval_not(I, n, ctx, out);
    case N_NEG: return eval_neg(I, n, ctx, out);
    case N_TERNARY: {
        int t = cond_true(I, n->a);
        if (I->ctl) return pp_undef();
        return pp_eval(I, t ? n->b : n->c, ctx, out);
    }
    case N_PREINC: case N_PREDEC: case N_POSTINC: case N_POSTDEC: return eval_incdec(I, n, ctx, out);
    case N_REPEAT: return eval_repeat(I, n, ctx, out);
    case N_CALL: return call_named(I, n, ctx, out);
    case N_CALLREF: return eval_callref(I, n, ctx, out);
    case N_BUILTIN: I->line = n->line; return pp_builtin(I, n, ctx, out);
    case N_BLOCK: return exec_block(I, n, ctx, out);
    case N_IF: return eval_if(I, n, ctx, out);
    case N_WHILE:
        if (n->sym) return eval_labeled_loop(I, n);
        if (n->flags & 16) {   /* a bare block: as the last statement, its value is the value of its last statement */
            for (;;) {
                int outn = out ? out->n : 0;
                pp_val v = exec_block(I, n->b, ctx, out);
                if (loop_ctl(I, NULL) != 2) return v;            /* not redo */
                pp_release(v);
                if (out) { for (int i = outn; i < out->n; i++) pp_release(out->v[i]); out->n = outn; }
            }
        }
        return eval_while(I, n);
    case N_FOR: if (n->sym) return eval_labeled_loop(I, n); return eval_for(I, n);
    case N_FOREACH: if (n->sym) return eval_labeled_loop(I, n); return eval_foreach(I, n);
    case N_SUBDEF: return eval_subdef(I, n);
    case N_RETURN: return eval_return(I, n);
    case N_GOTO: {
        /* the label must be in a statement list that is running now (this block, an enclosing
           one, or one of a caller); otherwise the error is raised here, as in perl */
        struct pp_stmts *f = I->stmts;
        while (f && find_label(f->blk, n->sym) < 0) f = f->up;
        if (!f) return pp_die(I, "Can't find label %s", n->sym->name);
        I->ctl = CTL_GOTO; I->ctl_label = n->sym;
        return pp_undef();
    }
    case N_LABEL: return n->a ? pp_eval(I, n->a, ctx, out) : pp_undef();
    case N_LAST: case N_NEXT: case N_REDO:
        if (n->sym) {   /* the label must belong to a loop that is running (here or in a caller) */
            struct pp_looplabel *l = I->loop_labels;
            while (l && l->label != n->sym) l = l->up;
            if (!l) return pp_die(I, "Label not found for \"%s %s\"", n->type == N_LAST ? "last" : n->type == N_NEXT ? "next" : "redo", n->sym->name);
        }
        I->ctl = n->type == N_LAST ? CTL_LAST : n->type == N_NEXT ? CTL_NEXT : CTL_REDO; I->ctl_label = n->sym;
        return pp_undef();
    case N_NOP: return pp_undef();
    case N_DOBLOCK: return exec_block(I, n->a, ctx, out);
    case N_EVALBLOCK: return eval_evalblock(I, n, ctx, out);
    case N_EVALSTR: return eval_evalstr(I, n, ctx, out);
    case N_CONSTDEF: return eval_constdef(I, n);
    case N_REQUIRE: return scalar_or_push(ctx, out, eval_require(I, n));
    case N_USE: return eval_use(I, n);
    case N_METHOD: return eval_method(I, n, ctx, out);
    case N_WANTARRAY:
        return scalar_or_push(ctx, out, I->call_ctx == CTX_LIST ? pp_int(1) : I->call_ctx == CTX_SCALAR ? pp_int(0) : pp_undef());
    case N_FILEHANDLE: return scalar_or_push(ctx, out, pp_int(n->op));
    case N_GLOB: return eval_glob(I, n, ctx, out);
    case N_CALLER: return eval_caller(I, n, ctx, out);
    case N_FILETEST: return eval_filetest(I, n, ctx, out);
    case N_LOCALELEM: eval_local_elem(I, n); return pp_undef();
    case N_LOCALGLOB: { pp_sym *gs = glob_sym(I, n->a); if (gs) local_push_glob(I, gs, 1); return pp_undef(); }
    case N_GLOBSLOT: return eval_globslot(I, n, ctx, out);
    case N_SUBCLOSE:
        for (int i = I->nall_subdefs - 1; i >= 0; i--)
            if (I->all_subdefs[i]->sym == n->sym && (I->all_subdefs[i]->flags & PP_SUB_INPLACE)) return eval_subdef(I, I->all_subdefs[i]);
        return pp_undef();
    case N_INIT: return pp_undef();   /* reached inside a block or sub: too late to run it */
    case N_END: {   /* queue the block; pp_run calls the queue in reverse order */
        pp_val code = pp_eval(I, n->a, CTX_SCALAR, NULL);
        if (I->ctl) { pp_release(code); return pp_undef(); }
        pp_obj *q = pp_global(I, pp_intern(I, "@ END", 5));   /* pairs: the block, its place in compile order */
        pp_array_push(q, code);
        pp_array_push(q, pp_copy(n->b->val));
        for (int i = 0; i < n->n; i++) {              /* the outer lexicals the block uses (see pp_run) */
            pp_val r = pp_eval(I, n->list[i], CTX_SCALAR, NULL);
            if (I->ctl) { pp_release(r); return pp_undef(); }
            pp_array_push(pp_global(I, pp_intern(I, "@ ENDVARS", 9)), r);
        }
        return pp_undef();
    }
    case N_PACKAGE: I->file_pkg = n->sym ? n->sym->name : NULL; return pp_undef();
    case N_NAMEDBIND: {
        pp_obj *args = current_args(I);
        int got = args ? args->u.arr.n : 0;
        int np = n->op;
        const char *subnm = n->a->val.u.s->d;
        if (got > np && ((got - np) & 1))
            return pp_die(I, "Odd name/value argument for subroutine '%s'", subnm);
        for (int i = 0; i < n->n && !I->ctl; i++) {
            pp_node *ent = n->list[i];               /* N_ASSIGN{a: N_MY[$var], b: default|NULL} */
            pp_node *var = ent->a->list[0];
            const char *pname = var->sym->name + 1;
            size_t plen = strlen(pname);
            declare_my(I, ent->a);
            int found = -1;
            for (int k = np; k + 1 < got; k += 2) {  /* the last duplicate wins, like perl */
                pp_str *t; const char *ks = pp_to_cstr(args->u.arr.v[k], &t);
                size_t kl = t ? (size_t)t->len : strlen(ks);
                if (kl == plen && memcmp(ks, pname, plen) == 0) found = k;
                if (t) pp_str_decref(t);
            }
            pp_obj *cell = var_cell(I, var);
            pp_release(cell->u.scalar);
            cell->u.scalar = pp_undef();
            if (found >= 0) cell->u.scalar = pp_copy(args->u.arr.v[found + 1]);
            else if (ent->b) {
                pp_val d = pp_eval(I, ent->b, CTX_SCALAR, NULL);
                if (I->ctl) { pp_release(d); return pp_undef(); }
                cell->u.scalar = d;
            }
            else return pp_die(I, "Missing required named parameter '%s' to subroutine '%s'", pname, subnm);
        }
        /* leftover pairs: collected by a trailing %hash, otherwise an error */
        pp_obj *rest = NULL;
        if (n->b) { declare_my(I, n->b); rest = var_cell(I, n->b->list[0]); pp_hash_clear(rest); }
        for (int k = np; k + 1 < got && !I->ctl; k += 2) {
            pp_str *t; const char *ks = pp_to_cstr(args->u.arr.v[k], &t);
            size_t kl = t ? (size_t)t->len : strlen(ks);
            int known = 0;
            for (int i = 0; i < n->n; i++) {
                const char *pn = n->list[i]->a->list[0]->sym->name + 1;
                if (strlen(pn) == kl && memcmp(pn, ks, kl) == 0) { known = 1; break; }
            }
            if (!known) {
                if (rest) pp_hash_set(rest, ks, (int)kl, pp_copy(args->u.arr.v[k + 1]));
                else {
                    char nm[64]; snprintf(nm, sizeof nm, "%.*s", (int)(kl > 60 ? 60 : kl), ks);
                    if (t) pp_str_decref(t);
                    return pp_die(I, "Unrecognized named parameter '%s' to subroutine '%s'", nm, subnm);
                }
            }
            if (t) pp_str_decref(t);
        }
        return pp_undef();
    }
    case N_TRY: return eval_try(I, n, ctx, out);
    case N_TRYERR: return scalar_or_push(ctx, out, pp_copy(I->try_err));
    case N_SIGCHECK: {
        pp_obj *args = current_args(I);
        int got = args ? args->u.arr.n : 0;
        const char *nm = n->a->val.u.s->d;
        int min = n->op, max = n->aux;      /* max 255 = slurpy, no upper limit */
        if (got < min)
            return pp_die(I, "Too few arguments for subroutine '%s' (got %d; expected %s%d)", nm, got, max != min ? "at least " : "", min);
        if (max != 255 && got > max)
            return pp_die(I, "Too many arguments for subroutine '%s' (got %d; expected %s%d)", nm, got, max != min ? "at most " : "", max);
        if (n->flags & 1) {
            int ns = (int)n->b->val.u.i;
            if (got > ns && ((got - ns) & 1))
                return pp_die(I, "Odd name/value argument for subroutine '%s'", nm);
        }
        return pp_undef();
    }
    case N_MATCH: I->line = n->line; return pp_eval_match(I, n, ctx, out);
    case N_SUBST: I->line = n->line; return pp_eval_subst(I, n, ctx, out);
    case N_TRANS: I->line = n->line; return pp_eval_trans(I, n, ctx, out);
    case N_SUBSTRLV: return eval_substr_lv(I, n, ctx, out);
    default: break;
    }
    return pp_die(I, "internal: unknown node type %d", n->type);
}

/* ------------------------------------------------------------------ */
/* units                                                               */
/* ------------------------------------------------------------------ */

pp_val pp_exec_unit(pp_interp *I, pp_node *unit, int ctx, pp_list *out) {
    pp_env *saved = I->env;
    const char *saved_pkg = I->file_pkg;
    I->file_pkg = NULL;                  /* every file starts in package main */
    pp_obj *saved_code = I->cur_code;    /* the file's own statements are not part of the sub that */
    I->cur_code = NULL;                  /* required it: caller() sees the file's package */
    /* the file scope: the one subs defined during parsing already closed over, if any */
    pp_env *env = I->pending_env ? I->pending_env : pp_env_new(NULL, unit->aux);
    I->pending_env = NULL;
    pp_env_grow(env, unit->aux);
    if (unit == I->main_unit && !I->main_env) { I->main_env = env; pp_env_incref(env); }
    I->env = env;
    /* named subs are defined before the unit body runs; they close over the unit scope */
    for (int i = 0; i < I->nsubdefs; i++) {
        pp_node *d = I->subdefs[i];
        if (d->flags & PP_SUB_EARLY) continue;       /* a BEGIN block needed it: defined already */
        pp_obj *code = pp_obj_new(PP_OCODE);
        code->u.code.body = d->a;
        pp_note_sub_file(I, d->a, I->file);
        code->u.code.env = env; pp_env_incref(env);
        code->u.code.name = d->sym->name + 1;
        code->u.code.block_proto = d->op == 1; code->u.code.pscalar = d->aux;
        pp_define_sub(I, d->sym, code);
    }
    I->nsubdefs = 0;
    int lh = I->nlocals;
    struct pp_stmts here = { I->stmts, unit };
    I->stmts = &here;
    /* UNITCHECK blocks of this file run before its first statement */
    for (int i = 0; i < unit->n && !I->ctl; i++) {
        pp_node *s = unit->list[i];
        if (!s || s->type != N_INIT || s->op == 9) continue;
        pp_val code = pp_eval(I, s->a, CTX_SCALAR, NULL);
        if (I->ctl) { pp_release(code); break; }
        pp_list args = {0};
        if (code.t == PP_REF && code.u.o->kind == PP_OCODE) { pp_val v = pp_call(I, code.u.o, &args, CTX_VOID, NULL); pp_release(v); }
        pp_list_free(&args);
        pp_release(code);
    }
    /* The main program is about to run: first the CHECK blocks (last compiled first), then
       the INIT blocks (in compile order), of the program and of every module it loaded
       with `use`. After that it is too late for more, as in perl. */
    if (unit == I->main_unit && !I->init_ran) {
        I->init_ran = 1;
        for (int pass = 0; pass < 2; pass++) {
            pp_obj *q = pp_global(I, pp_intern(I, pass ? "@ INIT" : "@ CHECK", pass ? 6 : 7));
            for (int k = 0; k < q->u.arr.n && !I->ctl; k++) {
                pp_val code = q->u.arr.v[pass ? k : q->u.arr.n - 1 - k];
                pp_list args = {0};
                if (code.t == PP_REF && code.u.o->kind == PP_OCODE) { pp_val v = pp_call(I, code.u.o, &args, CTX_VOID, NULL); pp_release(v); }
                pp_list_free(&args);
            }
            for (int k = 0; k < q->u.arr.n; k++) pp_release(q->u.arr.v[k]);
            q->u.arr.n = 0;
        }
    }
    /* like exec_stmts, but each top-level statement's AST is freed once it has run
       (saves the memory of big literals such as constant tables) */
    pp_val last = pp_undef();
    for (int i = 0; i < unit->n; i++) {
        pp_node *s = unit->list[i];
        if (!s || s->type == N_INIT) continue;       /* INIT blocks were handled above */
        if (I->ctl) break;
        I->line = s->line;
        I->warn_on = (s->flags & PP_STMT_WARN) != 0;
        if ((++I->stmt_counter & 0xFF) == 0) pp_host_poll();
        if (!I->in_dispatch && (I->ev_head != I->ev_tail || I->alarm_at)) pp_events_poll(I);
        int is_last = i == unit->n - 1;
        pp_val v = pp_eval(I, s, is_last ? ctx : CTX_VOID, is_last ? out : NULL);
        if (is_last) last = v; else pp_release(v);
        if (I->tie_pend) pp_tie_flush(I);
        if (pp_destroy_n) pp_destroy_run(I);
        if (I->ctl == CTL_GOTO) {
            int j = find_label(unit, I->ctl_label);
            if (j >= 0) {
                I->ctl = CTL_NONE; I->ctl_label = NULL;
                pp_release(last); last = pp_undef();
                i = j - 1;
                continue;
            }
            pp_sym *label = I->ctl_label;        /* nowhere left to look */
            I->ctl = CTL_NONE; I->ctl_label = NULL;
            pp_release(pp_die(I, "Can't find label %s", label ? label->name : "?"));
        }
        /* statements that ran are freed, unless a goto may come back to them */
        if (!(unit->flags & PP_UNIT_GOTO) && !keeps_code(s)) { pp_free_ast(s); unit->list[i] = NULL; }
        if (I->ctl) break;
    }
    I->stmts = here.up;
    local_unwind(I, lh);
    I->env = saved;
    I->file_pkg = saved_pkg;
    I->cur_code = saved_code;
    pp_env_decref(env);
    return last;
}

/* ------------------------------------------------------------------ */
/* interpreter lifecycle                                               */
/* ------------------------------------------------------------------ */

/* FETCH / STORE of the scalar behind \$a[i] and \$h{k} (see make_ref): self is [container, key] */
static pp_val n_elemref(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    if (a->n < 1 || a->v[0].t != PP_REF || a->v[0].u.o->kind != PP_OARRAY || a->v[0].u.o->u.arr.n < 2) return pp_undef();
    pp_val *sp = a->v[0].u.o->u.arr.v;
    if (sp[0].t != PP_REF) return pp_undef();
    pp_obj *box = sp[0].u.o;
    pp_val *slot;
    if (box->kind == PP_OARRAY) {
        int64_t i = pp_to_int(sp[1]);
        if (a->n < 2 && i >= box->u.arr.n) return pp_undef();
        slot = pp_array_slot(box, (int)i);
    } else if (box->kind == PP_OHASH) {
        if (sp[1].t != PP_STR) return pp_undef();
        if (a->n < 2 && !pp_hash_find(box, sp[1].u.s->d, sp[1].u.s->len)) return pp_undef();
        slot = pp_hash_slot(box, sp[1].u.s->d, sp[1].u.s->len);
    } else return pp_undef();
    if (a->n >= 2) { pp_release(*slot); *slot = pp_copy(a->v[1]); }      /* STORE */
    return pp_copy(*slot);
}
/* cell is the scalar behind \$a[i] / \$h{k}: copy its value to the element (store) or the
   element's value to it. For code that works on the cell directly (an in-memory file). */
void pp_elemref_sync(pp_obj *cell, int store) {
    pp_val *tv = cell->tied == 1 ? pp_tie_find(cell) : NULL;
    if (!tv || tv->t != PP_REF || tv->u.o->kind != PP_OARRAY || !tv->u.o->class || strcmp(tv->u.o->class->name, "PicoPerl::ElemRef")) return;
    pp_list a = {0};
    pp_list_push(&a, pp_copy(*tv));
    if (store) pp_list_push(&a, pp_copy(cell->u.scalar));
    pp_val r = n_elemref(NULL, &a, CTX_SCALAR, NULL);
    if (!store) { pp_release(cell->u.scalar); cell->u.scalar = r; } else pp_release(r);
    pp_list_free(&a);
}
/* UNIVERSAL::isa($thing, TYPE) and UNIVERSAL::can($thing, METHOD) called as functions.
   $thing may be an object, a class name, or (for isa) a plain reference whose type is asked. */
static const char *univ_reftype(pp_obj *o) {
    switch (o->kind) {
    case PP_OARRAY: return "ARRAY";
    case PP_OHASH:  return "HASH";
    case PP_OCODE:  return "CODE";
    case PP_OREGEX: return "Regexp";
    case PP_OFILE: case PP_OGLOB: return "GLOB";
    default:        return o->u.scalar.t == PP_REF ? "REF" : "SCALAR";
    }
}
static pp_val n_univ_isa(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    if (a->n < 2 || a->v[0].t == PP_UNDEF) return pp_strn("", 0);
    pp_str *want = pp_to_str(a->v[1]);
    int yes = 0;
    if (a->v[0].t == PP_REF) {
        pp_obj *o = a->v[0].u.o;
        if (o->class && class_isa(I, o->class->name, want->d, 0)) yes = 1;
        else yes = !strcmp(univ_reftype(o), want->d);
    } else {
        pp_str *cls = pp_to_str(a->v[0]);
        yes = cls->len > 0 && class_isa(I, cls->d, want->d, 0);
        pp_str_decref(cls);
    }
    pp_str_decref(want);
    return yes ? pp_int(1) : pp_strn("", 0);
}
static pp_val n_univ_can(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    if (a->n < 2 || a->v[0].t == PP_UNDEF) return pp_undef();
    pp_str *name = pp_to_str(a->v[1]);
    pp_obj *code = NULL;
    if (a->v[0].t == PP_REF) { if (a->v[0].u.o->class) code = find_method(I, a->v[0].u.o->class->name, name->d, 0); }
    else {
        pp_str *cls = pp_to_str(a->v[0]);
        if (cls->len > 0) code = find_method(I, cls->d, name->d, 0);
        pp_str_decref(cls);
    }
    pp_str_decref(name);
    return code ? pp_ref(code) : pp_undef();
}

/* state kept outside the interpreter (the overload hook, the DESTROY queue) must not carry
   over from one interpreter to the next: a board runs a fresh one on every reload */
static void reset_static_state(void) {
    pp_ovl_hook = NULL; ovl_I = NULL; ovl_assign = 0;
    pp_anon_I = NULL;
    pp_destroy_on = 0;
    pp_obj *o;
    while ((o = pp_destroy_next(0)) != NULL) pp_obj_decref(o);   /* freed without their DESTROY */
}
pp_interp *pp_new(void) {
    reset_static_state();
    pp_interp *I = pp_xmalloc(sizeof *I);
    memset(I, 0, sizeof *I);
    I->globals = pp_env_new(NULL, 0);
    I->arena = pp_arena_new();
    I->env = NULL;
    I->max_depth = 100;
    I->loaded = pp_obj_new(PP_OHASH);
    I->sym_args = pp_intern(I, "@_", 2);
    I->sym_underscore = pp_intern(I, "$_", 2);
    I->sym_a = pp_intern(I, "$a", 2);
    I->sym_b = pp_intern(I, "$b", 2);
    I->g_underscore = pp_global(I, I->sym_underscore);
    I->g_a = pp_global(I, I->sym_a);
    I->g_b = pp_global(I, I->sym_b);
    I->g_at = pp_global(I, pp_intern(I, "$@", 2));
    I->g_at->u.scalar = pp_strn("", 0);
    I->g_argv = pp_global(I, pp_intern(I, "@ARGV", 5));
    pp_global(I, pp_intern(I, "$!", 2))->u.scalar = pp_int(0);       /* no error yet */
    pp_global(I, pp_intern(I, "$$", 2))->u.scalar = pp_int(1);       /* process id: the host sets the real one */
    pp_global(I, pp_intern(I, "$<", 2))->u.scalar = pp_int(0);       /* user ids: the host sets the real ones */
    pp_global(I, pp_intern(I, "$>", 2))->u.scalar = pp_int(0);
    pp_global(I, pp_intern(I, "@INC", 4));                           /* directories `require` looks in first */
    pp_global(I, pp_intern(I, "%INC", 4));
    I->g_ofs = pp_global(I, pp_intern(I, "$,", 2));
    I->g_ors = pp_global(I, pp_intern(I, "$\\", 2));
    {   /* $/ starts as "\n"; undef means "read the whole file" */
        pp_obj *irs = pp_global(I, pp_intern(I, "$/", 2));
        if (irs->u.scalar.t == PP_UNDEF) irs->u.scalar = pp_strn("\n", 1);
    }
    {   /* $^H (compile-time hints) is a number; nothing here reads it, pragmas written in perl set bits in it */
        pp_obj *hints = pp_global(I, pp_intern(I, "$^H", 3));
        if (hints->u.scalar.t == PP_UNDEF) hints->u.scalar = pp_int(0);
    }
    pp_obj *dq = pp_global(I, pp_intern(I, "$\"", 2)); dq->u.scalar = pp_strn(" ", 1);
    /* ${^NAME} variables scripts commonly read: no taint mode, no -C, and the main program is running */
    pp_global(I, pp_intern(I, "$^TAINT", 7))->u.scalar = pp_int(0);
    pp_global(I, pp_intern(I, "$^UNICODE", 9))->u.scalar = pp_int(0);
    pp_global(I, pp_intern(I, "$^GLOBAL_PHASE", 14))->u.scalar = pp_strn("RUN", 3);
    /* $]: the perl version picoperl follows (a string, like perl's "5.044000") */
    pp_global(I, pp_intern(I, "$]", 2))->u.scalar = pp_strn("5.044000", 8);
    pp_global(I, pp_intern(I, "$^O", 3))->u.scalar = pp_strn(PP_OSNAME, (int)strlen(PP_OSNAME));
    pp_core_natives_register(I);
    pp_register(I, "PicoPerl::ElemRef::FETCH", n_elemref);
    pp_register(I, "PicoPerl::ElemRef::STORE", n_elemref);
    pp_register(I, "UNIVERSAL::isa", n_univ_isa);
    pp_register(I, "UNIVERSAL::can", n_univ_can);
    pp_host_register(I);
    return I;
}

void pp_free(pp_interp *I) {
    reset_static_state();
    if (!I) return;
    if (I->pending_env) { pp_env_decref(I->pending_env); I->pending_env = NULL; }
    hold(I, NULL);
    local_unwind(I, 0);
    free(I->locals);
    pp_list_free(&I->wb);
    pp_list_free(&I->ret);
    pp_release(I->err);
    pp_release(I->try_err);
    pp_obj_decref(I->loaded);
    pp_regex_cleanup(I);
    pp_env_decref(I->globals);
    for (int i = 0; i < PP_SYM_BUCKETS; i++) { pp_sym *s = I->syms[i]; while (s) { pp_sym *n = s->next; free(s); s = n; } }
    free(I->subdefs);
    for (int i = 0; i < I->nall_subdefs; i++) pp_free_ast(I->all_subdefs[i]);
    free(I->all_subdefs);
    for (int i = 0; i < I->nunits; i++) { pp_free_ast(I->units[i]); free(I->unit_files[i]); }
    free(I->units); free(I->unit_files);
    for (int i = 0; i + 1 < I->nfailed_ranges; i += 2) pp_free_node_range(I->arena, I->failed_ranges[i], I->failed_ranges[i + 1], 0);
    free(I->failed_ranges);
    pp_arena_free(I->arena);
    for (int i = 0; i < I->nlazy; i++) { free(I->lazy[i].dst); free(I->lazy[i].src); free(I->lazy[i].prefix); }
    free(I->lazy);
    free(I);
}

void pp_register(pp_interp *I, const char *name, pp_native fn) {
    pp_obj *code = pp_obj_new(PP_OCODE);
    code->u.code.native = fn;
    code->u.code.name = name;
    define_const_sym(I, name, code);
}

void pp_define_const(pp_interp *I, const char *name, pp_val v) {
    pp_obj *code = pp_obj_new(PP_OCODE);
    code->u.code.is_const = 1;
    code->u.code.constval = v;
    code->u.code.name = name;
    define_const_sym(I, name, code);
}

/* ------------------------------------------------------------------ */
/* deferred events (perl "safe signals" style): the host queues them,  */
/* handlers run between statements                                     */
/* ------------------------------------------------------------------ */

void pp_event_push(pp_interp *I, int kind, int a, int b) {
    int next = (I->ev_head + 1) & 15;
    if (next == I->ev_tail) return;              /* queue full: drop the event */
    I->evq[I->ev_head].kind = (uint8_t)kind;
    I->evq[I->ev_head].a = (int16_t)a;
    I->evq[I->ev_head].b = b;
    I->ev_head = next;
}

int pp_events_pending(pp_interp *I) {
    if (I->in_dispatch) return 0;
    if (I->ev_head != I->ev_tail) return 1;
    return I->alarm_at && (int32_t)(pp_host_millis() - I->alarm_at) >= 0;
}

static void call_handler(pp_interp *I, pp_obj *code, pp_list *args) {
    pp_val r = pp_call(I, code, args, CTX_VOID, NULL);
    pp_release(r);
    pp_list_free(args);
}

void pp_events_poll(pp_interp *I) {
    if (I->in_dispatch || I->ctl) return;
    I->in_dispatch = 1;
    while (I->ev_tail != I->ev_head && !I->ctl) {
        int kind = I->evq[I->ev_tail].kind, a = I->evq[I->ev_tail].a, b = I->evq[I->ev_tail].b;
        I->ev_tail = (I->ev_tail + 1) & 15;
        if (!I->sym_dispatch) I->sym_dispatch = pp_intern(I, "&PicoPerl::Event::_dispatch", 27);
        pp_obj *code = pp_lookup_sub(I, I->sym_dispatch);
        if (!code) continue;                     /* PicoPerl::Event not loaded: drop */
        pp_list args = {0};
        pp_list_push(&args, pp_cstr(kind == PP_EV_ENCODER ? "encoder" : "gpio"));
        pp_list_push(&args, pp_int(a));
        pp_list_push(&args, pp_int(b));
        call_handler(I, code, &args);
    }
    if (!I->ctl && I->alarm_at && (int32_t)(pp_host_millis() - I->alarm_at) >= 0) {
        I->alarm_at = 0;
        pp_obj *h = sig_handler(I, "ALRM");
        if (h) {
            pp_list args = {0};
            pp_list_push(&args, pp_cstr("ALRM"));
            call_handler(I, h, &args);
        }
        else pp_die(I, "Alarm clock");           /* perl is killed by SIGALRM; picoperl dies */
    }
    I->in_dispatch = 0;
}

static void report_die(pp_interp *I) {
    pp_str *s = pp_to_str(I->err);
    pp_host_write(2, s->d, s->len);
    pp_str_decref(s);
}
int pp_run(pp_interp *I, const char *filename, const char *src) {
    int err = 0;
    I->file = filename;
    char base;
    I->stack_base = &base;                /* the parser checks the watermark too (deep nesting) */
    if (!I->stack_limit) I->stack_limit = PP_DEFAULT_STACK_LIMIT;
    pp_node *unit = pp_parse(I, filename, src, &err);
    if (err) {
        if (I->pending_env) { pp_env_decref(I->pending_env); I->pending_env = NULL; }
        I->stack_base = NULL;
        return 255;
    }
    char *fname = pp_xmalloc(strlen(filename) + 1); strcpy(fname, filename);
    track_unit(I, unit, fname);
    I->main_unit = unit;
    if (I->ctl != CTL_EXIT) {                 /* (exit in a BEGIN block or a `use`: nothing is run, END blocks still are) */
        I->ctl = CTL_NONE;
        pp_val r = pp_exec_unit(I, unit, CTX_VOID, NULL);
        pp_release(r);
    }
    int rc = 0;
    if (I->ctl == CTL_DIE) { report_die(I); rc = 255; }
    else if (I->ctl == CTL_EXIT) rc = I->exit_code;
    I->ctl = CTL_NONE;
    if (I->main_env) {
        /* the end of the program (before the END blocks, as perl does for file-level lexicals): objects still held by the script's file-level variables get
           their DESTROY now, newest variable first (package variables are not visited) */
        pp_env *me = I->main_env;
        pp_obj *keep = pp_global(I, pp_intern(I, "@ ENDVARS", 9));
        if (pp_destroy_on) {
            if (pp_destroy_n) pp_destroy_run(I);
            for (int pass = 0; pass < 2; pass++) {
                for (int i = me->n - 1; i >= 0; i--) {
                    pp_obj *c = me->cells[i];
                    if (!c) continue;
                    int kept = 0;                      /* used by an END block: stays as it is */
                    for (int k = 0; k < keep->u.arr.n && !kept; k++) kept = keep->u.arr.v[k].t == PP_REF && keep->u.arr.v[k].u.o == c;
                    if (kept) continue;
                    if (pass == 0 && c->kind == PP_OSCALAR && c->u.scalar.t == PP_REF) { pp_val v = c->u.scalar; c->u.scalar = pp_undef(); pp_release(v); }
                    else if (pass == 1 && c->kind == PP_OARRAY) pp_array_clear(c);
                    else if (pass == 1 && c->kind == PP_OHASH) pp_hash_clear(c);
                    if (pp_destroy_n) pp_destroy_run(I);
                }
            }
        }
        I->main_env = NULL;
        pp_env_decref(me);
    }
    /* END blocks: last queued first, also after die and exit. They see the exit code in $?
       and may change it. */
    pp_obj *ends = pp_global(I, pp_intern(I, "@ END", 5));
    if (ends->u.arr.n > 0) {
        pp_obj *phase = pp_global(I, pp_intern(I, "$^GLOBAL_PHASE", 14));
        pp_release(phase->u.scalar); phase->u.scalar = pp_strn("END", 3);
        pp_obj *status = pp_global(I, pp_intern(I, "$?", 2));
        pp_release(status->u.scalar); status->u.scalar = pp_int(rc);
        while (ends->u.arr.n > 0) {
            int at = 0;                                /* the one compiled last (a module's END is
                                                          queued when it is loaded, the main file's when they are reached) */
            for (int k = 2; k < ends->u.arr.n; k += 2) if (ends->u.arr.v[k + 1].u.i >= ends->u.arr.v[at + 1].u.i) at = k;
            pp_val code = ends->u.arr.v[at];
            for (int k = at; k + 2 < ends->u.arr.n; k++) ends->u.arr.v[k] = ends->u.arr.v[k + 2];
            ends->u.arr.n -= 2;
            pp_list args = {0};
            if (code.t == PP_REF && code.u.o->kind == PP_OCODE) { pp_val v = pp_call(I, code.u.o, &args, CTX_VOID, NULL); pp_release(v); }
            pp_list_free(&args);
            pp_release(code);
            if (I->ctl == CTL_DIE) report_die(I);
            else if (I->ctl == CTL_EXIT) { pp_release(status->u.scalar); status->u.scalar = pp_int(I->exit_code); }
            I->ctl = CTL_NONE;
        }
        pp_obj *keep = pp_global(I, pp_intern(I, "@ ENDVARS", 9));   /* what the END blocks kept goes now */
        if (pp_destroy_on) {
            for (int k = keep->u.arr.n - 1; k >= 0; k--) {
                pp_obj *c = keep->u.arr.v[k].t == PP_REF ? keep->u.arr.v[k].u.o : NULL;
                if (!c) continue;
                if (c->kind == PP_OSCALAR) { pp_val v = c->u.scalar; c->u.scalar = pp_undef(); pp_release(v); }
                else if (c->kind == PP_OARRAY) pp_array_clear(c);
                else if (c->kind == PP_OHASH) pp_hash_clear(c);
                if (pp_destroy_n) pp_destroy_run(I);
            }
            I->ctl = CTL_NONE;
        }
        pp_array_clear(keep);
        rc = (int)pp_to_int(status->u.scalar) & 255;
    }
    I->stack_base = NULL;
    return rc;
}
