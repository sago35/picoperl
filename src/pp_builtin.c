/* Built-in functions for picoperl. */
#include "pp_internal.h"
#include "pp_regex.h"
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
    B_OPEN, B_CLOSE, B_EOF, B_SEEK, B_TELL, B_ALARM, B_ANY, B_ALL, B_PROTOTYPE, B_SETPROTO, B_SUBINFO, B_SYSOPEN, B_ABS_END
};

/* prototype("CORE::name"): the prototypes of perl's own functions, as " name=proto name=proto ".
   Functions that perl gives none (print, system, sort ...) are not listed: undef. Fatal and
   autodie ask to learn whether, and how, a builtin can be wrapped. */
static const char core_protos[] =
    " abs=_ accept=** alarm=_ all=\\&@ any=\\&@ bind=*$ binmode=*;$ bless=$;$ break= caller=;$ chdir=;$"
    " chmod=@ chown=@ chr=_ chroot=_ close=;* closedir=* connect=*$ continue= cos=_ crypt=$$ dbmclose=\\%"
    " dbmopen=\\%$$ die=@ dump= each=\\[%@] endgrent= endhostent= endnetent= endprotoent= endpwent="
    " endservent= eof=;* evalbytes=_ exit=;$ exp=_ fc=_ fcntl=*$$ fileno=* flock=*$ fork= formline=$@"
    " getc=;* getgrent= getgrgid=$ getgrnam=$ gethostbyaddr=$$ gethostbyname=$ gethostent= getlogin="
    " getnetbyaddr=$$ getnetbyname=$ getnetent= getpeername=* getpgrp=;$ getppid= getpriority=$$"
    " getprotobyname=$ getprotobynumber=$; getprotoent= getpwent= getpwnam=$ getpwuid=$ getservbyname=$$"
    " getservbyport=$$ getservent= getsockname=* getsockopt=*$$ glob=_; gmtime=;$ hex=_ index=$$;$ int=_"
    " ioctl=*$$ join=$@ keys=\\[%@] kill=@ lc=_ lcfirst=_ length=_ link=$$ listen=*$ localtime=;$"
    " lock=\\[$@%&*] log=_ lstat=;* method= mkdir=_;$ msgctl=$$$ msgget=$$ msgrcv=$$$$$ msgsnd=$$$ oct=_"
    " open=*;$@ opendir=*$ ord=_ pack=$@ pipe=** pop=;\\@ pos=;\\[$*] prototype=_ push=\\@@ quotemeta=_"
    " rand=;$ read=*\\$$;$ readdir=* readline=;* readlink=_ readpipe=_ recv=*\\$$$ ref=_ rename=$$ reset=;$"
    " reverse=@ rewinddir=* rindex=$$;$ rmdir=_ scalar=$ seek=*$$ seekdir=*$ semctl=$$$$ semget=$$$"
    " semop=$$ send=*$$;$ setgrent= sethostent=$ setnetent=$ setpgrp=;$$ setpriority=$$$ setprotoent=$"
    " setpwent= setservent=$ setsockopt=*$$$ shift=;\\@ shmctl=$$$ shmget=$$$ shmread=$$$$ shmwrite=$$$$"
    " shutdown=*$ sin=_ sleep=;$ socket=*$$$ socketpair=**$$$ splice=\\@;$$@ sprintf=$@ sqrt=_ srand=;$"
    " stat=;* study=_ substr=$$;$$ symlink=$$ syscall=$@ sysopen=*$$;$ sysread=*\\$$;$ sysseek=*$$"
    " syswrite=*$;$$ tell=;* telldir=* tie=\\[$@%*]$@ tied=\\[$@%*] time= times= truncate=$$ uc=_ ucfirst=_"
    " umask=;$ undef=;\\[$@%&*] unlink=@ unpack=$_ unshift=\\@@ untie=\\[$@%*] utime=@ values=\\[%@] vec=$$$"
    " wait= waitpid=$$ wantarray= warn=@ write=;* ";

int pp_builtin_id(const char *name, int len, int *kind);
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
    { "open", B_OPEN, BK_LIST }, { "sysopen", B_SYSOPEN, BK_LIST }, { "close", B_CLOSE, BK_UNARY }, { "eof", B_EOF, BK_UNARY },
    { "seek", B_SEEK, BK_LIST }, { "tell", B_TELL, BK_UNARY }, { "alarm", B_ALARM, BK_UNARY },
    { "select", B_SELECT, BK_LIST }, { "bless", B_BLESS, BK_LIST },
    { "prototype", B_PROTOTYPE, BK_UNARY }, { "__setproto", B_SETPROTO, BK_LIST }, { "__subinfo", B_SUBINFO, BK_UNARY },
    { NULL, 0, 0 }
};

/* is this the name of a perl function (CORE::name)? One of the builtins here, or one that
   perl has a prototype for (caller, stat, wantarray ...) */
int pp_core_fn_known(const char *name, int len) {
    if (pp_builtin_id(name, len, NULL) > 0) return 1;
    char key[48];
    if (len <= 0 || len > 40) return 0;
    int kl = pp_snprintf(key, sizeof key, " %.*s=", len, name);
    (void)kl;
    return strstr(core_protos, key) != NULL;
}
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
        /* %vd: the numbers of the string's characters joined with dots (sprintf("%vd", "1.22.333") is itself) */
        if (i + 1 < flen && f[i] == 'v' && f[i + 1] == 'd') {
            i += 2;
            pp_str *vs = ai < args->n ? pp_to_str(args->v[ai]) : pp_str_new("", 0);
            if (ai < args->n) ai++;
            const unsigned char *p = (const unsigned char *)vs->d;
            for (int k = 0; k < vs->len; ) {
                /* one character: a UTF-8 sequence when it is well formed, else the byte */
                long cp = p[k]; int nb = 1;
                if (p[k] >= 0xC2 && p[k] < 0xE0 && k + 1 < vs->len && (p[k + 1] & 0xC0) == 0x80) { cp = ((p[k] & 0x1F) << 6) | (p[k + 1] & 0x3F); nb = 2; }
                else if (p[k] >= 0xE0 && p[k] < 0xF0 && k + 2 < vs->len && (p[k + 1] & 0xC0) == 0x80 && (p[k + 2] & 0xC0) == 0x80) { cp = ((p[k] & 0x0F) << 12) | ((p[k + 1] & 0x3F) << 6) | (p[k + 2] & 0x3F); nb = 3; }
                else if (p[k] >= 0xF0 && p[k] < 0xF5 && k + 3 < vs->len && (p[k + 1] & 0xC0) == 0x80 && (p[k + 2] & 0xC0) == 0x80 && (p[k + 3] & 0xC0) == 0x80) { cp = ((long)(p[k] & 0x07) << 18) | ((p[k + 1] & 0x3F) << 12) | ((p[k + 2] & 0x3F) << 6) | (p[k + 3] & 0x3F); nb = 4; }
                char nbuf[16]; int nn = pp_snprintf(nbuf, sizeof nbuf, "%s%ld", k ? "." : "", cp);
                str_append(&r, nbuf, nn);
                k += nb;
            }
            pp_str_decref(vs);
            continue;
        }
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
            uint64_t v = pp_to_bits(arg);
            snprintf(spec + sn, sizeof spec - sn, "llu");
            bn = pp_snprintf(buf, sizeof buf, spec, (unsigned long long)v);
            break;
        }
        case 'x': case 'X': case 'o': {
            uint64_t v = pp_to_bits(arg);
            spec[sn] = 'l'; spec[sn + 1] = 'l'; spec[sn + 2] = conv; spec[sn + 3] = 0;
            bn = pp_snprintf(buf, sizeof buf, spec, (unsigned long long)v);
            break;
        }
        case 'b': case 'B': {
            uint64_t v = pp_to_bits(arg);
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
/* the object a handle value (\*FH, *FH, "FH") is tied to, or NULL (borrowed) */
static pp_val *tied_handle(pp_interp *I, pp_val v) {
    if (!pp_tie_count) return NULL;
    pp_obj *g = NULL;
    if (v.t == PP_REF) g = v.u.o->kind == PP_OGLOB ? v.u.o : NULL;
    else if (v.t == PP_INT && (v.u.i == 1 || v.u.i == 2)) { pp_sym *gs = pp_glob_named(I, v.u.i == 1 ? "STDOUT" : "STDERR"); g = gs ? gs->gcell : NULL; if (g && g->kind != PP_OGLOB) g = NULL; }
    else if (v.t == PP_STR && v.u.s->len > 0) { pp_sym *gs = pp_glob_named(I, v.u.s->d); g = gs && gs->gcell && gs->gcell->kind == PP_OGLOB ? gs->gcell : NULL; }
    return g ? pp_tie_find(g) : NULL;
}
/* HANDLE is tied: call METHOD on its object with the handle value consumed; *done = 1 */
static pp_val tied_handle_call(pp_interp *I, pp_val hv, const char *method, pp_list *args, int ctx, pp_list *out, int *done) {
    pp_val *t = tied_handle(I, hv);
    *done = t != NULL;
    if (!t) return pp_undef();
    pp_val obj = pp_copy(*t);
    pp_release(hv);
    pp_val r = pp_tie_call(I, obj, method, args, ctx, out, 1);
    pp_release(obj);
    return r;
}
/* EXISTS / DELETE on a tied hash: key is consumed; returns the truth of the result, the result
   itself in *res when asked for */
static int tied_elem_call(pp_interp *I, pp_obj *h, const char *m, pp_val key, pp_val *res) {
    pp_val *t = pp_tie_find(h);
    if (!t) { pp_release(key); return 0; }
    pp_tie_flush(I);
    pp_val o = pp_copy(*t);
    pp_list a = {0}; pp_list_push(&a, key);
    pp_val r = pp_tie_call(I, o, m, &a, CTX_SCALAR, NULL, 1);
    pp_release(o);
    int truth = !I->ctl && pp_truth(r);
    if (res && !I->ctl) *res = r; else pp_release(r);
    return truth;
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

/* $. : the number of lines read from the handle that was read last */
static void set_line_no(pp_interp *I, void *handle, int lines) {
    I->last_read = handle;
    pp_obj *c = pp_global(I, pp_intern(I, "$.", 2));
    pp_release(c->u.scalar);
    c->u.scalar = pp_int(lines);
}
static pp_val read_stdin_line(pp_interp *I) {
    char *line = pp_host_read_line();
    if (line) I->stdin_lines++;
    set_line_no(I, I, I->stdin_lines);
    if (!line) return pp_undef();
    pp_val r = pp_cstr(line);
    free(line);
    return r;
}
/* file I/O that also serves in-memory files (open($fh, MODE, \$buf)): those read from and
   append to the scalar they were opened on */
static pp_str *mem_text(pp_obj *f) {
    pp_val v = f->u.file.mem->u.scalar;
    return v.t == PP_STR ? v.u.s : NULL;
}
static int f_read1(pp_obj *f, char *c) {
    if (!f->u.file.mem) return (int)pp_host_fread(f->u.file.fp, c, 1) == 1;
    pp_str *t = mem_text(f);
    if (!t || f->u.file.mpos >= t->len) return 0;
    *c = t->d[f->u.file.mpos++];
    return 1;
}
static int f_write(pp_obj *f, const char *d, int len) {
    if (!f->u.file.mem) return (int)pp_host_fwrite(f->u.file.fp, d, len) == len;
    pp_obj *cell = f->u.file.mem;
    if (cell->tied) pp_elemref_sync(cell, 0);        /* opened on \$h{k}: another handle may have written to the element */
    pp_str *old = pp_to_str(cell->u.scalar);
    pp_str *n = pp_str_new(old->d, old->len);
    pp_str_decref(old);
    str_append(&n, d, len);
    pp_release(cell->u.scalar);
    cell->u.scalar = pp_str_val(n);
    if (cell->tied) pp_elemref_sync(cell, 1);        /* opened on \$h{k}: the text goes to the element */
    return 1;
}
static int f_eof(pp_obj *f) {
    if (!f->u.file.fp) return 1;
    if (!f->u.file.mem) return pp_host_feof(f->u.file.fp);
    pp_str *t = mem_text(f);
    return !t || f->u.file.mpos >= t->len;
}
static int f_close(pp_obj *f) {
    if (!f->u.file.fp) return 0;
    int ok = 1;
    if (f->u.file.mem) { pp_obj_decref(f->u.file.mem); f->u.file.mem = NULL; }
    else ok = pp_host_fclose(f->u.file.fp) == 0;
    f->u.file.fp = NULL;
    return ok;
}
/* one record from a file: up to and including $/ ("\n" unless changed; undef reads the rest
   of the file, "" reads a paragraph) */
static pp_val read_fline(pp_interp *I, pp_obj *f) {
    if (!f->u.file.fp) return pp_undef();
    pp_sym *isym = pp_intern(I, "$/", 2);
    pp_val irs = isym->gcell ? isym->gcell->u.scalar : pp_undef();
    const char *sep = "\n"; int seplen = 1, slurp = irs.t == PP_UNDEF;
    pp_str *sepstr = NULL;
    if (!slurp && irs.t != PP_STR) { sepstr = pp_to_str(irs); sep = sepstr->d; seplen = sepstr->len; }
    else if (!slurp) { sep = irs.u.s->d; seplen = irs.u.s->len; }
    if (!slurp && seplen == 0) { sep = "\n\n"; seplen = 2; }
    pp_str *s = pp_str_new(NULL, 0);
    char c;
    while (f_read1(f, &c)) {
        s = pp_str_reserve(s, 1);
        s->d[s->len++] = c;
        if (!slurp && c == sep[seplen - 1] && s->len >= seplen && !memcmp(s->d + s->len - seplen, sep, (size_t)seplen)) break;
    }
    if (sepstr) pp_str_decref(sepstr);
    if (s->len == 0) { pp_str_decref(s); return pp_undef(); }
    s->d[s->len] = 0;
    f->u.file.lines++;
    return pp_str_val(s);
}

/* $/ applies to files only: reading STDIN always ends lines at \n; say so once */
static void check_irs(pp_interp *I) {
    if (I->compat_warn & 32) return;
    pp_sym *sym = pp_intern(I, "$/", 2);
    if (!sym->gcell) return;
    pp_val v = sym->gcell->u.scalar;
    if (v.t == PP_UNDEF || (v.t == PP_STR && v.u.s->len == 1 && v.u.s->d[0] == '\n')) return;
    I->compat_warn |= 32;
    pp_warn(I, "picoperl: $/ is ignored for STDIN, which always splits on \"\\n\"");
}

static int umask_value = 022;       /* what umask() remembers; applied to the PERMS of sysopen */
/* <> : a line from the files named in @ARGV, one after the other ($ARGV is the current one),
   or from STDIN when @ARGV was empty to begin with. undef after the last file; the next <>
   starts over. */
static pp_val argv_line(pp_interp *I) {
    pp_obj *glob = pp_glob_obj(I, pp_glob_named(I, "ARGV"));
    pp_obj *st = pp_global(I, pp_intern(I, "$ ARGVSTATE", 11));   /* 0: not started, 1: files, 2: STDIN */
    pp_obj *args = pp_global(I, pp_intern(I, "@ARGV", 5));
    for (;;) {
        int state = st->u.scalar.t == PP_INT ? (int)st->u.scalar.u.i : 0;
        if (state == 2) {
            check_irs(I);
            pp_val l = read_stdin_line(I);
            if (l.t == PP_UNDEF) st->u.scalar = pp_int(0);
            return l;
        }
        if (glob->u.glob.io) {
            pp_obj *f = glob->u.glob.io;
            pp_val l = f->u.file.fp || f->u.file.mem ? read_fline(I, f) : pp_undef();
            if (l.t != PP_UNDEF) { set_line_no(I, f, f->u.file.lines); return l; }
            glob->u.glob.io = NULL;
            pp_obj_decref(f);
        }
        if (args->kind != PP_OARRAY || args->u.arr.n == 0) {
            if (state == 0) { st->u.scalar = pp_int(2); continue; }
            st->u.scalar = pp_int(0);
            return pp_undef();
        }
        st->u.scalar = pp_int(1);
        pp_val name = pp_array_shift(args);
        pp_str *ns = pp_to_str(name); pp_release(name);
        pp_obj *cur = pp_global(I, pp_intern(I, "$ARGV", 5));
        pp_release(cur->u.scalar); ns->refcnt++; cur->u.scalar = pp_str_val(ns);
        void *fp = pp_host_fopen(ns->d, "r");
        if (!fp) pp_warn(I, "Can't open %s: %s", ns->d, pp_host_io_error());
        else { pp_obj *o = pp_obj_new(PP_OFILE); o->u.file.fp = fp; glob->u.glob.io = o; }
        pp_str_decref(ns);
    }
}
static pp_val do_open(pp_interp *I, pp_node *n, int ctx, pp_list *out) {
    int new_perms = -1;                 /* sysopen creating a file: the permissions to give it */
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
    if (n->op == B_SYSOPEN) {
        /* sysopen(FH, PATH, FLAGS, PERMS): the O_* flags (Fcntl's values) become the mode of a
           3-argument open. O_CREAT | O_EXCL fails when the file is there, opening without
           O_CREAT fails when it is not. PERMS (less the umask) is given to a file that is
           created, where the host has a chmod. */
        if (args.n < 2) { pp_list_free(&args); return pp_die(I, "Not enough arguments for sysopen"); }
        pp_str *ps = pp_to_str(args.v[0]);
        int64_t fl = pp_to_int(args.v[1]);
        long long sz = 0;
        int there = pp_host_filetest(ps->d, &sz) >= 0;
        int acc = (int)(fl & 3), creat = (fl & 64) != 0, excl = (fl & 128) != 0, trunc = (fl & 512) != 0, append = (fl & 1024) != 0;
        const char *why = creat && excl && there ? "File exists" : !creat && !there ? "No such file or directory" : NULL;
        if (why) { pp_str_decref(ps); pp_list_free(&args); set_errno_var(I, why); return ret(ctx, out, pp_undef()); }
        if (creat && !there && args.n > 2 && args.v[2].t != PP_UNDEF) new_perms = (int)pp_to_int(args.v[2]) & 07777 & ~umask_value;
        const char *mode = acc == 0 ? "<" : acc == 1 ? (append ? ">>" : trunc || !there ? ">" : "+<") : (trunc || !there ? "+>" : "+<");
        pp_val path = pp_str_val(ps);
        pp_list_free(&args);
        memset(&args, 0, sizeof args);
        pp_list_push(&args, pp_cstr(mode));
        pp_list_push(&args, path);
    }
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
    if (args.n == 1) {
        /* open(FH, "<$path") / open(FH, $path): the mode is the front of the string (none: read).
           Split it into the 3-argument form. Pipes ("cmd |", "| cmd") are not available. */
        pp_str *ws = pp_to_str(args.v[0]);
        const char *m = ws->d, *e = ws->d + ws->len;
        while (m < e && isspace((unsigned char)*m)) m++;
        while (e > m && isspace((unsigned char)e[-1])) e--;
        const char *q = m;
        while (q < e && (*q == '+' || *q == '<' || *q == '>')) q++;
        int piped = (m < e && *m == '|') || (e > m && e[-1] == '|');
        pp_val mode = q > m ? pp_strn(m, (int)(q - m)) : pp_strn("<", 1);
        while (q < e && isspace((unsigned char)*q)) q++;
        pp_val path = pp_strn(q, (int)(e - q));
        pp_str_decref(ws);
        pp_release(args.v[0]); args.v[0] = mode;
        pp_list_push(&args, path);
        if (piped) args.n = 0;
    }
    if (args.n < 2) {
        set_errno_var(I, args.n ? "Invalid argument" : "pipes are not supported");
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
    if (args.v[1].t == PP_REF && args.v[1].u.o->kind == PP_OSCALAR) {
        /* open($fh, MODE, \$buf): an in-memory file on that scalar */
        pp_obj *cell = args.v[1].u.o;
        pp_obj *o = pp_obj_new(PP_OFILE);
        o->u.file.mem = cell; cell->refcnt++;
        o->u.file.fp = cell;                          /* non-NULL: open */
        o->u.file.writable = (uint8_t)(base[0] != 'r' || base[1] == '+');
        if (cell->tied) pp_elemref_sync(cell, 0);     /* opened on \$h{k}: start from the element's text */
        if (base[0] == 'w') { pp_release(cell->u.scalar); cell->u.scalar = pp_strn("", 0); if (cell->tied) pp_elemref_sync(cell, 1); }
        pp_str_decref(ms); pp_list_free(&args);
        if (glob) {
            if (glob->u.glob.io) pp_obj_decref(glob->u.glob.io);
            glob->u.glob.io = o;
            return ret(ctx, out, pp_int(1));
        }
        slot = pp_lvalue(I, n->list[0]);
        if (!slot) { pp_obj_decref(o); return pp_undef(); }
        pp_release(*slot);
        *slot = pp_ref(o);
        pp_obj_decref(o);
        return ret(ctx, out, pp_int(1));
    }
    pp_str *path = pp_to_str(args.v[1]);
    void *fp = pp_host_fopen(path->d, base);
    if (fp && new_perms >= 0) {
        pp_sym *cs = pp_sym_find(I, "&chmod", 6);
        if (cs && cs->sub && cs->sub->u.code.native) {
            pp_list ca = {0};
            pp_list_push(&ca, pp_int(new_perms));
            path->refcnt++; pp_list_push(&ca, pp_str_val(path));
            pp_release(cs->sub->u.code.native(I, &ca, CTX_SCALAR, NULL));
            pp_list_free(&ca);
        }
    }
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
    pp_val f = pp_undef();
    if (n->a) { f = pp_eval(I, n->a, CTX_SCALAR, NULL); if (I->ctl) { pp_release(f); return pp_undef(); } }
    if (pp_tie_count) {   /* print {$tied} LIST is $obj->PRINT(LIST); say adds the newline. No handle: STDOUT */
        pp_val probe = n->a ? pp_copy(f) : pp_int(1);
        if (tied_handle(I, probe)) {
            pp_release(f);
            pp_list targs = {0};
            if (n->n == 0 && id != B_PRINTF) pp_list_push(&targs, pp_copy(I->g_underscore->u.scalar));
            else if (!eval_args(I, n, 0, &targs)) { pp_list_free(&targs); pp_release(probe); return pp_undef(); }
            if (id == B_SAY) pp_list_push(&targs, pp_strn("\n", 1));
            int done;
            return tied_handle_call(I, probe, id == B_PRINTF ? "PRINTF" : "PRINT", &targs, CTX_SCALAR, NULL, &done);
        }
        pp_release(probe);
    }
    if (n->a) {
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
        if (fh) wr_ok = f_write(fh, t->d, t->len);
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
        if (fh) wr_ok = f_write(fh, buf->d, buf->len);
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
        if (arr->tied) pp_tie_writeback(I, arr);
        pp_obj_decref(arr);
        return ret(ctx, out, pp_int(len));
    }
    case B_POP: case B_SHIFT: {
        pp_obj *arr;
        if (n->n == 0) { arr = default_array(I); arr->refcnt++; }
        else arr = arg_container(I, n->list[0], 0);
        if (!arr) return pp_undef();
        if (arr->kind != PP_OARRAY) { pp_obj_decref(arr); return pp_die(I, "Not an ARRAY reference"); }
        if (arr->tied) pp_tie_sync(I, arr);
        if (n->op == B_SHIFT && I->frame && arr->u.arr.n) {
            struct pp_frame *sf = I->frame;
            while (sf && !sf->code) sf = sf->up;      /* eval { } frames belong to the sub around them */
            if (sf && sf->argv == arr) sf->nshift++;
        }
        pp_val v = n->op == B_POP ? pp_array_pop(arr) : pp_array_shift(arr);
        if (arr->tied) pp_tie_writeback(I, arr);
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
        if (len - off - cnt > 0) memmove(arr->u.arr.v + off + nins, arr->u.arr.v + off + cnt, (len - off - cnt) * sizeof(pp_val));   /* an empty array has no storage yet */
        for (int i = 0; i < nins; i++) arr->u.arr.v[off + i] = pp_copy(vals.v[2 + i]);
        arr->u.arr.n = newlen;
        if (arr->tied) pp_tie_writeback(I, arr);
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
        pp_val pv = pp_pos_of_node(I, v);
        if (I->ctl) return pp_undef();
        return ret(ctx, out, pv);
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
        if (n->op == B_DIE) return pp_die_val(I, pp_str_val(r));
        /* (the tail may move r: take the value only afterwards) */
        if (r->d[r->len - 1] != '\n') { char tail[160]; int tn = pp_snprintf(tail, sizeof tail, " at %s line %d.\n", I->file ? I->file : "-", I->line); str_append(&r, tail, tn); }
        pp_emit_warning(I, r->d, r->len);
        pp_str_decref(r);
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
        if (v.t == PP_REF && pp_ovl_hook) {   /* abs($obj) and friends: the class's own version */
            pp_val ov;
            const char *fn = n->op == B_INT ? "int" : n->op == B_ABS ? "abs" : n->op == B_SQRT ? "sqrt" : n->op == B_SIN ? "sin" : n->op == B_COS ? "cos" : n->op == B_EXP ? "exp" : "log";
            if (pp_ovl_unary(I, v, fn, &ov)) { pp_release(v); return I->ctl ? (pp_release(ov), pp_undef()) : ret(ctx, out, ov); }
        }
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
        if (a->type == N_CALL && a->n == 0 && (a->flags & 2)) d = pp_resolve_sub(I, a->sym) != NULL;   /* defined &name; defined name() calls it */
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
    case B_PROTOTYPE: {
        /* prototype(\&f) / prototype("name"): the text of the sub's prototype, undef without
           one. It is read from where the parser kept it (the body), unless set_prototype gave
           this very code ref another one (kept in the tie table, which code refs do not use
           otherwise). Builtins ("CORE::push") have none here. */
        pp_val v = unary_arg(I, n);
        if (I->ctl) return pp_undef();
        pp_obj *code = NULL;
        if (v.t == PP_REF && v.u.o->kind == PP_OCODE) code = v.u.o;
        else if (v.t == PP_STR && !strncmp(v.u.s->d, "CORE::", 6) && v.u.s->len < 40) {
            char key[48];
            int kl = pp_snprintf(key, sizeof key, " %s=", v.u.s->d + 6);
            const char *hit = strstr(core_protos, key);
            pp_release(v);
            if (!hit) return ret(ctx, out, pp_undef());
            const char *e = strchr(hit + kl, ' ');
            return ret(ctx, out, pp_strn(hit + kl, (int)(e - (hit + kl))));
        }
        else if (v.t == PP_STR && v.u.s->len > 0 && v.u.s->len < 200) {
            char buf[280];
            const char *nm = v.u.s->d;
            const char *pkg = n->n > 1 && n->list[1]->type == N_CONST && n->list[1]->val.t == PP_STR ? n->list[1]->val.u.s->d : "main";
            int bn = strstr(nm, "::") ? pp_snprintf(buf, sizeof buf, "&%s", !strncmp(nm, "main::", 6) ? nm + 6 : nm)
                   : strcmp(pkg, "main") ? pp_snprintf(buf, sizeof buf, "&%s::%s", pkg, nm) : pp_snprintf(buf, sizeof buf, "&%s", nm);
            code = pp_resolve_sub(I, pp_intern(I, buf, bn));
        }
        pp_val r = pp_undef();
        if (code) {
            pp_val *set = code->tied ? pp_tie_find(code) : NULL;
            if (set) r = pp_copy(*set);
            else if (code->u.code.body && code->u.code.body->b && code->u.code.body->b->type == N_CONST) r = pp_copy(code->u.code.body->b->val);
        }
        pp_release(v);
        return ret(ctx, out, r);
    }
    case B_SUBINFO: {
        /* __subinfo(\&code): (package, name, file, first line, last line) of a sub, for lib/B.pm
           (B::svref_2object). The lines are those of the first and the last statement of the
           body; a native or a constant has no file and no lines. An empty list for no code ref. */
        pp_val v = unary_arg(I, n);
        if (I->ctl) return pp_undef();
        if (!(v.t == PP_REF && v.u.o->kind == PP_OCODE) || !(ctx == CTX_LIST && out)) { pp_release(v); return ctx == CTX_LIST && out ? pp_no_value() : pp_undef(); }
        pp_obj *code = v.u.o;
        const char *full = code->u.code.name ? code->u.code.name : "__ANON__";
        const char *last = NULL;
        for (const char *p = full; *p; p++) if (p[0] == ':' && p[1] == ':') last = p;
        pp_list_push(out, last ? pp_strn(full, (int)(last - full)) : pp_strn("main", 4));
        pp_list_push(out, pp_cstr(last ? last + 2 : full));
        pp_node *b = code->u.code.body;
        const char *file = b && b->type == N_BLOCK && b->sym ? b->sym->name + 1 : "";
        int l1 = 0, l2 = 0;
        if (b && b->type == N_BLOCK) {
            for (int i = 0; i < b->n; i++) { pp_node *st = b->list[i]; if (!st || !st->line) continue; if (!l1) l1 = st->line; l2 = st->line; }
            if (!l1) l1 = l2 = b->line;
        }
        pp_list_push(out, pp_cstr(file));
        pp_list_push(out, pp_int(l1));
        pp_list_push(out, pp_int(l2));
        pp_release(v);
        return pp_undef();
    }
    case B_SETPROTO: {   /* __setproto(\&code, PROTO): Scalar::Util::set_prototype; undef removes it */
        pp_list a = {0};
        if (!eval_args(I, n, 0, &a)) { pp_list_free(&a); return pp_undef(); }
        if (a.n >= 2 && a.v[0].t == PP_REF && a.v[0].u.o->kind == PP_OCODE) {
            pp_val p = a.v[1].t == PP_UNDEF ? pp_undef() : pp_str_val(pp_to_str(a.v[1]));
            pp_tie_set(a.v[0].u.o, p);
        }
        pp_val r = a.n ? pp_copy(a.v[0]) : pp_undef();
        pp_list_free(&a);
        return ret(ctx, out, r);
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
            default: {
                /* a glob held in a scalar (what $Pkg::{name} gives) is the string "*Pkg::name"
                   here; a reference to it is a GLOB reference in perl */
                pp_val in = v.u.o->u.scalar;
                r = in.t == PP_REF ? "REF" : "SCALAR";
                if (in.t == PP_STR && in.u.s->len > 3 && in.u.s->d[0] == '*' && strstr(in.u.s->d, "::")) {
                    const char *p = in.u.s->d + 1;
                    while (*p && (isalnum((unsigned char)*p) || *p == '_' || *p == ':')) p++;
                    if (!*p) r = "GLOB";
                }
                break;
            }
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
            pp_obj *h = pp_get_hash_raw(I, a->a, 0);
            if (h && h->tied == 1) e = tied_elem_call(I, h, "EXISTS", (pp_val){ .t = PP_STR, .u.s = ks }, NULL), ks = NULL;
            else if (h) e = pp_hash_find(h, ks->d, ks->len) != NULL;
            if (h) pp_obj_decref(h);
            if (ks) pp_str_decref(ks);
        } else if (a->type == N_AELEM) {
            pp_val k = pp_eval(I, a->b, CTX_SCALAR, NULL);
            if (I->ctl) return pp_undef();
            int64_t idx = pp_to_int(k); pp_release(k);
            pp_obj *arr = pp_get_array(I, a->a, 0, NULL);
            if (arr) { if (idx < 0) idx += arr->u.arr.n; e = idx >= 0 && idx < arr->u.arr.n; pp_obj_decref(arr); }
        } else if (a->type == N_CALL) {
            e = pp_resolve_sub(I, a->sym) != NULL;
        } else if (a->type == N_CALLREF && (a->flags & 2)) {   /* exists &$code / exists &{"name"}: no call, and fine under strict refs */
            pp_val cv = pp_eval(I, a->a, CTX_SCALAR, NULL);
            if (I->ctl) { pp_release(cv); return pp_undef(); }
            if (cv.t == PP_REF) e = cv.u.o->kind == PP_OCODE;
            else if (cv.t != PP_UNDEF) { pp_sym *s = pp_symref(I, a, cv, '&', 0); e = s && pp_resolve_sub(I, s) != NULL; }
            pp_release(cv);
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
            pp_obj *h = pp_get_hash_raw(I, a->a, 0);
            pp_val r = pp_undef();
            if (h && h->tied == 1) tied_elem_call(I, h, "DELETE", (pp_val){ .t = PP_STR, .u.s = ks }, &r), ks = NULL;
            else if (h) pp_hash_delete(h, ks->d, ks->len, &r);
            if (h) pp_obj_decref(h);
            if (ks) pp_str_decref(ks);
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
        if (n->n == 0 || (n->flags & 4)) {           /* <STDIN>, <> */
            int argv = n->n != 0;
            if (!argv) check_irs(I);
            if (ctx == CTX_LIST && out) {
                for (;;) { pp_val l = argv ? argv_line(I) : read_stdin_line(I); if (l.t == PP_UNDEF || I->ctl) { pp_release(l); break; } pp_list_push(out, l); }
                return pp_undef();
            }
            return ret(ctx, out, argv ? argv_line(I) : read_stdin_line(I));
        }
        pp_val hv = pp_eval(I, n->list[0], CTX_SCALAR, NULL);
        if (I->ctl) { pp_release(hv); return pp_undef(); }
        { int done; pp_val r = tied_handle_call(I, hv, "READLINE", NULL, ctx == CTX_LIST && out ? CTX_LIST : CTX_SCALAR, out, &done);
          if (done) return I->ctl || (ctx == CTX_LIST && out) ? r : ret(ctx, out, r); }
        int hfd;
        pp_obj *f = handle_of(I, hv, &hfd);
        if (!f && hfd == 0) {              /* <STDIN> through a glob: readline(*STDIN), <$in> */
            check_irs(I);
            pp_release(hv);
            if (ctx == CTX_LIST && out) {
                for (;;) { pp_val l = read_stdin_line(I); if (l.t == PP_UNDEF) break; pp_list_push(out, l); }
                return pp_undef();
            }
            return ret(ctx, out, read_stdin_line(I));
        }
        if (!f) { pp_release(hv); return pp_die(I, "readline: not a filehandle (expected a handle from open)"); }
        if (ctx == CTX_LIST && out) {
            for (;;) { pp_val l = read_fline(I, f); if (l.t == PP_UNDEF) break; pp_list_push(out, l); }
            set_line_no(I, f, f->u.file.lines);
            pp_release(hv);
            return pp_undef();
        }
        pp_val r = read_fline(I, f);
        set_line_no(I, f, f->u.file.lines);
        pp_release(hv);
        return ret(ctx, out, r);
    }
    case B_CLOSE: {
        pp_val hv = unary_arg(I, n);
        if (I->ctl) { pp_release(hv); return pp_undef(); }
        { int done; pp_val r = tied_handle_call(I, hv, "CLOSE", NULL, CTX_SCALAR, NULL, &done); if (done) return I->ctl ? r : ret(ctx, out, r); }
        int hfd;
        pp_obj *f = handle_of(I, hv, &hfd);
        if (!f && hfd >= 0) { pp_release(hv); return ret(ctx, out, pp_int(1)); }   /* close(STDOUT): stays open here */
        if (!f) { pp_release(hv); return pp_die(I, "close: not a filehandle (expected a handle from open)"); }
        int r = f_close(f);
        f->u.file.lines = 0;                       /* close starts the count again */
        if (I->last_read == f) set_line_no(I, f, 0);
        pp_release(hv);
        return ret(ctx, out, r ? pp_int(1) : pp_strn("", 0));
    }
    case B_EOF: {
        pp_val hv = unary_arg(I, n);
        if (I->ctl) { pp_release(hv); return pp_undef(); }
        { int done; pp_val r = tied_handle_call(I, hv, "EOF", NULL, CTX_SCALAR, NULL, &done); if (done) return I->ctl ? r : ret(ctx, out, r); }
        pp_obj *f = file_of(I, hv, "eof");
        if (!f) { pp_release(hv); return pp_undef(); }
        int e = f_eof(f);
        pp_release(hv);
        return ret(ctx, out, e ? pp_int(1) : pp_strn("", 0));
    }
    case B_TELL: {
        pp_val hv = unary_arg(I, n);
        if (I->ctl) { pp_release(hv); return pp_undef(); }
        pp_obj *f = file_of(I, hv, "tell");
        if (!f) { pp_release(hv); return pp_undef(); }
        long pos = !f->u.file.fp ? -1 : f->u.file.mem ? (f->u.file.writable ? (mem_text(f) ? mem_text(f)->len : 0) : f->u.file.mpos) : pp_host_ftell(f->u.file.fp);
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
        long r = -1;
        if (f->u.file.fp && f->u.file.mem) {            /* an in-memory file: only the read position moves */
            int len = mem_text(f) ? mem_text(f)->len : 0;
            long np = whence == 0 ? off : whence == 1 ? f->u.file.mpos + off : len + off;
            if (np >= 0) { f->u.file.mpos = np > len ? len : (int)np; r = 0; }
        }
        else if (f->u.file.fp) r = pp_host_fseek(f->u.file.fp, off, whence);
        pp_list_free(&args);
        return ret(ctx, out, r >= 0 ? pp_int(1) : pp_strn("", 0));
    }
    case B_OPEN: case B_SYSOPEN: return do_open(I, n, ctx, out);
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
        if (n->op == B_BINMODE && args.n > 0 && tied_handle(I, args.v[0])) {   /* $obj->BINMODE(LAYER) */
            pp_val hv = args.v[0];
            for (int i = 1; i < args.n; i++) args.v[i - 1] = args.v[i];
            args.n--;
            int done;
            pp_val r = tied_handle_call(I, hv, "BINMODE", &args, CTX_SCALAR, NULL, &done);
            return I->ctl ? r : ret(ctx, out, r);
        }
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
/* trim: without the white space at both ends (undef stays undef) */
static pp_val n_bi_trim(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    if (!a->n || a->v[0].t == PP_UNDEF) return pp_undef();
    pp_str *s = pp_to_str(a->v[0]);
    int b = 0, e = s->len;
    while (b < e && isspace((unsigned char)s->d[b])) b++;
    while (e > b && isspace((unsigned char)s->d[e - 1])) e--;
    pp_val r = pp_strn(s->d + b, e - b);
    pp_str_decref(s);
    return r;
}
/* indexed(LIST): (0, $x0, 1, $x1, ...); the number of values in scalar context */
static pp_val n_bi_indexed(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I;
    if (ctx == CTX_LIST && out) {
        for (int i = 0; i < a->n; i++) { pp_list_push(out, pp_int(i)); pp_list_push(out, pp_copy(a->v[i])); }
        return pp_no_value();
    }
    return pp_int(2 * a->n);
}
/* true / false; weaken, unweaken, is_weak, is_tainted: there are no weak references and no
   taint mode here, so these do nothing / say no */
static pp_val n_bi_true(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return pp_int(1); }
static pp_val n_bi_false(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return pp_strn("", 0); }
static pp_val n_bi_nothing(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)a; (void)ctx; (void)out; return pp_undef(); }
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
    if (a->v[0].t == PP_REF && a->v[0].u.o->class && pp_ovl_hook) {
        /* an object with overloading (Math::BigInt): what it turns into as a string decides */
        pp_str *t = pp_to_str(a->v[0]);
        int ok = !(t->len > 2 && memchr(t->d, '=', (size_t)t->len) && t->d[t->len - 1] == ')') && pp_looks_like_number(t->d, t->len);
        pp_str_decref(t);
        return ok ? pp_int(1) : pp_strn("", 0);
    }
    if (a->v[0].t != PP_STR) return pp_strn("", 0);
    return pp_looks_like_number(a->v[0].u.s->d, a->v[0].u.s->len) ? pp_int(1) : pp_strn("", 0);
}

/* localtime / gmtime (optional argument: seconds since the epoch, default now).
   List context: (sec, min, hour, mday, mon, year - 1900, wday, yday, isdst); scalar context:
   "Thu Nov 19 12:34:56 2026". The host says how far local time is from UTC
   (pp_host_utc_offset; a host without one, like the boards, gets UTC). */
__attribute__((weak)) int pp_host_utc_offset(int64_t t, int *isdst) { (void)t; *isdst = 0; return 0; }
static pp_val time_parts(pp_list *a, int ctx, pp_list *out, int local) {
    int64_t t = a->n > 0 && a->v[0].t != PP_UNDEF ? pp_to_int(a->v[0]) : pp_host_time();
    int isdst = 0;
    if (local) t += pp_host_utc_offset(t, &isdst);
    /* the date is worked out here (days -> civil date), not by the C library: its time zone
       code alone would add 40 KB to the firmware */
    int64_t days = t / 86400, rem = t % 86400;
    if (rem < 0) { rem += 86400; days--; }
    int wday = (int)(((days % 7) + 11) % 7);                     /* 1970-01-01 was a Thursday */
    int64_t z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;   /* year from March */
    int mday = (int)(doy - (153 * mp + 2) / 5 + 1), mon = (int)(mp < 10 ? mp + 2 : mp - 10);
    int64_t year = yoe + era * 400 + (mon <= 1);
    int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    static const short before[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    int yday = before[mon] + mday - 1 + (leap && mon > 1);
    int hour = (int)(rem / 3600), min = (int)(rem / 60 % 60), sec = (int)(rem % 60);
    if (ctx == CTX_LIST && out) {
        int64_t v[8] = { sec, min, hour, mday, mon, year - 1900, wday, yday };
        for (int i = 0; i < 8; i++) pp_list_push(out, pp_int(v[i]));
        return pp_int(isdst);                      /* the caller appends the last element */
    }
    static const char dnames[] = "SunMonTueWedThuFriSat", mnames[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char buf[64];
    int n = pp_snprintf(buf, sizeof buf, "%.3s %.3s %2d %02d:%02d:%02d %lld", dnames + 3 * wday, mnames + 3 * mon,
                        mday, hour, min, sec, (long long)year);
    return pp_strn(buf, n);
}
static pp_val n_localtime(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; return time_parts(a, ctx, out, 1); }
static pp_val n_gmtime(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; return time_parts(a, ctx, out, 0); }
/* ------------------------------------------------------------------ */
/* vec / pack / unpack                                                 */
/* ------------------------------------------------------------------ */

/* vec(STR, OFFSET, BITS): element OFFSET of STR seen as a row of BITS-wide unsigned numbers
   (BITS: 1 2 4 8 16 32 64; below 8 the low bits of a byte come first, from 8 up big-endian) */
static int vec_bits_ok(pp_interp *I, int64_t bits) {
    if (bits == 1 || bits == 2 || bits == 4 || bits == 8 || bits == 16 || bits == 32 || bits == 64) return 1;
    pp_die(I, "Illegal number of bits in vec");
    return 0;
}
static pp_val n_vec(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    if (a->n < 3) return pp_die(I, "Not enough arguments for vec");
    int64_t off = pp_to_int(a->v[1]), bits = pp_to_int(a->v[2]);
    if (!vec_bits_ok(I, bits)) return pp_undef();
    if (off < 0) return pp_die(I, "Negative offset to vec in lvalue context");
    pp_str *s = pp_to_str(a->v[0]);
    uint64_t r = 0;
    if (bits < 8) {
        int64_t bit = off * bits;
        if (bit / 8 < s->len) r = ((unsigned char)s->d[bit / 8] >> (bit % 8)) & ((1u << bits) - 1);
    } else {
        int64_t nb = bits / 8, start = off * nb;
        /* an element that reaches past the end reads as if the string went on with zero bytes */
        if (start < s->len) for (int64_t i = 0; i < nb; i++) r = (r << 8) | (start + i < s->len ? (unsigned char)s->d[start + i] : 0);
    }
    pp_str_decref(s);
    return bits == 64 && r > (uint64_t)INT64_MAX ? pp_num((double)r) : pp_int((int64_t)r);
}
static uint64_t pack_arg_uint(pp_val v);
/* vec(STR, OFFSET, BITS) = VALUE is parsed as STR = __vec_set(STR, OFFSET, BITS, VALUE) */
static pp_val n_vec_set(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    if (a->n < 4) return pp_die(I, "Not enough arguments for vec");
    int64_t off = pp_to_int(a->v[1]), bits = pp_to_int(a->v[2]);
    uint64_t val = pack_arg_uint(a->v[3]);
    if (!vec_bits_ok(I, bits)) return pp_undef();
    if (off < 0 || off > 0x3fffffff / bits) return pp_die(I, "Negative offset to vec in lvalue context");
    pp_str *src = a->v[0].t == PP_UNDEF ? pp_str_new(NULL, 0) : pp_to_str(a->v[0]);
    pp_str *s = pp_str_new(src->d, src->len);
    pp_str_decref(src);
    int need = (int)((off * bits + bits + 7) / 8);
    if (need > s->len) {
        s = pp_str_reserve(s, need - s->len);
        memset(s->d + s->len, 0, (size_t)(need - s->len));
        s->len = need; s->d[need] = 0;
    }
    if (bits < 8) {
        int64_t bit = off * bits;
        unsigned mask = ((1u << bits) - 1) << (bit % 8);
        s->d[bit / 8] = (char)(((unsigned char)s->d[bit / 8] & ~mask) | (((unsigned)val << (bit % 8)) & mask));
    } else {
        int64_t nb = bits / 8, start = off * nb;
        for (int64_t i = nb - 1; i >= 0; i--) { s->d[start + i] = (char)(val & 0xff); val >>= 8; }
    }
    return pp_str_val(s);
}

/* (kept out of line and uncloned: left alone, the compiler unrolls the recursion for groups
   into itself and the two functions grow to 30 KB of firmware) */
#if defined(__GNUC__) && !defined(__clang__)
#define PACK_FN __attribute__((noinline, noclone))
#else
#define PACK_FN __attribute__((noinline))          /* clang (the wasm build) has no noclone */
#endif
/* one item of a pack template: a letter (or a group), its modifiers and its repeat count */
typedef struct {
    char type;                 /* the letter; '(' for a group */
    const char *sub; int sublen;   /* group body */
    int big, little, bang;     /* > < ! */
    int star, has_count;       /* '*', or an explicit count */
    int64_t count;
    int slash;                 /* followed by '/': this item packs the length of the next one */
} pack_item;

/* reads the next item at *pos; 0 at the end of the template, -1 after a die */
PACK_FN static int pack_next(pp_interp *I, const char *t, int tl, int *pos, pack_item *it) {
    int p = *pos;
    for (;;) {
        while (p < tl && isspace((unsigned char)t[p])) p++;
        if (p < tl && t[p] == '#') { while (p < tl && t[p] != '\n') p++; continue; }
        break;
    }
    if (p >= tl) { *pos = p; return 0; }
    memset(it, 0, sizeof *it);
    it->type = t[p++];
    it->count = 1;
    if (it->type == '(') {
        int depth = 1, start = p;
        while (p < tl && depth) { if (t[p] == '(') depth++; else if (t[p] == ')') depth--; p++; }
        if (depth) { pp_die(I, "No group ending character ')' found in template"); return -1; }
        it->sub = t + start; it->sublen = p - 1 - start;
    }
    for (;; p++) {
        if (p < tl && t[p] == '<') it->little = 1;
        else if (p < tl && t[p] == '>') it->big = 1;
        else if (p < tl && t[p] == '!') it->bang = 1;
        else break;
    }
    if (p < tl && t[p] == '*') { it->star = 1; p++; }
    else if (p < tl && (isdigit((unsigned char)t[p]) || t[p] == '[')) {
        int br = t[p] == '[';
        if (br) p++;
        int64_t c = 0;
        while (p < tl && isdigit((unsigned char)t[p]) && c < 0x7ffffff) c = c * 10 + (t[p++] - '0');
        if (br) { if (p >= tl || t[p] != ']') { pp_die(I, "No group ending character ']' found in template"); return -1; } p++; }
        it->count = c; it->has_count = 1;
    }
    int q = p;
    while (q < tl && isspace((unsigned char)t[q])) q++;
    if (q < tl && t[q] == '/') { it->slash = 1; p = q + 1; }
    *pos = p;
    return 1;
}
/* size in bytes of an integer letter (0: not one), whether it is signed, and its byte order */
PACK_FN static int pack_int_size(const pack_item *it, int *is_signed, int *big) {
    *big = it->big;
    *is_signed = 0;
    switch (it->type) {
    case 'c': *is_signed = 1; return 1;
    case 'C': return 1;
    case 's': *is_signed = 1; return 2;
    case 'S': return 2;
    case 'l': *is_signed = 1; return it->bang ? (int)sizeof(long) : 4;
    case 'L': return it->bang ? (int)sizeof(long) : 4;
    case 'i': *is_signed = 1; return 4;
    case 'I': return 4;
    case 'q': case 'j': *is_signed = 1; return 8;
    case 'Q': case 'J': return 8;
    case 'n': *big = 1; *is_signed = it->bang; return 2;
    case 'N': *big = 1; *is_signed = it->bang; return 4;
    case 'v': *big = 0; *is_signed = it->bang; return 2;
    case 'V': *big = 0; *is_signed = it->bang; return 4;
    default: return 0;
    }
}
PACK_FN static void pack_put_int(pp_str **buf, uint64_t v, int size, int big) {
    char b[8];
    for (int i = 0; i < size; i++) b[big ? size - 1 - i : i] = (char)((v >> (8 * i)) & 0xff);
    str_append(buf, b, size);
}
PACK_FN static void pack_put_utf8(pp_str **buf, uint64_t cp) {
    char b[4]; int n;
    if (cp < 0x80) { b[0] = (char)cp; n = 1; }
    else if (cp < 0x800) { b[0] = (char)(0xc0 | (cp >> 6)); b[1] = (char)(0x80 | (cp & 0x3f)); n = 2; }
    else if (cp < 0x10000) { b[0] = (char)(0xe0 | (cp >> 12)); b[1] = (char)(0x80 | ((cp >> 6) & 0x3f)); b[2] = (char)(0x80 | (cp & 0x3f)); n = 3; }
    else { b[0] = (char)(0xf0 | ((cp >> 18) & 7)); b[1] = (char)(0x80 | ((cp >> 12) & 0x3f)); b[2] = (char)(0x80 | ((cp >> 6) & 0x3f)); b[3] = (char)(0x80 | (cp & 0x3f)); n = 4; }
    str_append(buf, b, n);
}
static uint64_t pack_arg_uint(pp_val v) {
    if (v.t == PP_NUM && v.u.n >= 18446744073709551615.0) return UINT64_MAX;
    if (v.t == PP_NUM && v.u.n >= 9223372036854775808.0) return (uint64_t)v.u.n;
    return (uint64_t)pp_to_int(v);
}
#define PACK_MAX (1 << 24)     /* no packed string or repeat count beyond this */

/* packs the items of t into *buf, taking arguments from args[*ai...]; 0 after a die */
PACK_FN static int pack_run(pp_interp *I, const char *t, int tl, pp_list *args, int *ai, pp_str **buf, int base, int depth) {
    int pos = 0;
    pack_item it;
    if (depth > 20) { pp_die(I, "Too deeply nested ()-groups in pack"); return 0; }
    for (;;) {
        int r = pack_next(I, t, tl, &pos, &it);
        if (r <= 0) return r == 0;
        if (it.count > PACK_MAX || (*buf)->len > PACK_MAX) { pp_die(I, "pack: result too large"); return 0; }
        int is_signed, big;
        int isz = pack_int_size(&it, &is_signed, &big);
        if (it.slash) {     /* LENGTH/STRING: the length of the string that follows, then the string */
            pack_item si;
            int sr = pack_next(I, t, tl, &pos, &si);
            if (sr < 0) return 0;
            if (sr == 0) { pp_die(I, "Code missing after '/' in pack"); return 0; }
            if (!strchr("aAZ", si.type) || !isz) { pp_die(I, "pack: '/' needs a number before it and a string type (a, A, Z) after it"); return 0; }
            pp_val v = *ai < args->n ? args->v[(*ai)++] : pp_undef();
            pp_str *s = v.t == PP_UNDEF ? pp_str_new(NULL, 0) : pp_to_str(v);
            int64_t len = s->len;
            if (si.has_count && si.count < len) len = si.count;
            int nul = si.type == 'Z' && !si.has_count;
            pack_put_int(buf, (uint64_t)(len + nul), isz, big);
            str_append(buf, s->d, (int)len);
            if (nul) str_append(buf, "", 1);
            pp_str_decref(s);
            continue;
        }
        switch (it.type) {
        case '(': {
            int gbase = (*buf)->len;
            for (int64_t k = 0; it.star ? *ai < args->n : k < it.count; k++) {
                int before = *ai;
                if (!pack_run(I, it.sub, it.sublen, args, ai, buf, gbase, depth + 1)) return 0;
                if (it.star && *ai == before) break;      /* a group that takes no argument would never end */
                if ((*buf)->len > PACK_MAX) { pp_die(I, "pack: result too large"); return 0; }
            }
            break;
        }
        case 'a': case 'A': case 'Z': {
            pp_val v = *ai < args->n ? args->v[(*ai)++] : pp_undef();
            pp_str *s = v.t == PP_UNDEF ? pp_str_new(NULL, 0) : pp_to_str(v);
            int64_t want = it.star ? s->len + (it.type == 'Z') : it.count;
            int64_t take = s->len < want ? s->len : want;
            if (it.type == 'Z' && !it.star && take == want && want > 0) take = want - 1;   /* always room for the NUL */
            str_append(buf, s->d, (int)take);
            for (int64_t k = take; k < want; k++) str_append(buf, it.type == 'A' ? " " : "", 1);
            pp_str_decref(s);
            break;
        }
        case 'b': case 'B': case 'h': case 'H': {
            pp_val v = *ai < args->n ? args->v[(*ai)++] : pp_undef();
            pp_str *s = v.t == PP_UNDEF ? pp_str_new(NULL, 0) : pp_to_str(v);
            int per = it.type == 'b' || it.type == 'B' ? 8 : 2;      /* digits per byte */
            int64_t digits = it.star ? s->len : it.count;
            for (int64_t d = 0; d < digits; d += per) {
                unsigned byte = 0;
                for (int k = 0; k < per && d + k < digits; k++) {
                    int ch = d + k < s->len ? (unsigned char)s->d[d + k] : '0';
                    unsigned val = per == 8 ? (unsigned)(ch & 1) : isalpha(ch) ? (unsigned)((ch + 9) & 0xf) : (unsigned)(ch & 0xf);
                    if (it.type == 'b') byte |= val << k;
                    else if (it.type == 'B') byte |= val << (7 - k);
                    else if (it.type == 'h') byte |= val << (4 * k);
                    else byte |= val << (4 * (1 - k));
                }
                char c = (char)byte;
                str_append(buf, &c, 1);
            }
            pp_str_decref(s);
            break;
        }
        case 'x':
            if (it.bang) { int64_t al = it.count; while (al > 0 && ((*buf)->len - base) % al) str_append(buf, "", 1); break; }
            for (int64_t k = 0; k < (it.star ? 0 : it.count); k++) str_append(buf, "", 1);
            break;
        case 'X': {
            int64_t back = it.star ? 0 : it.count;
            if (back > (*buf)->len) { pp_die(I, "'X' outside of string in pack"); return 0; }
            (*buf)->len -= (int)back; (*buf)->d[(*buf)->len] = 0;
            break;
        }
        case '@': {
            int64_t to = base + (it.star ? 0 : it.count);
            while ((*buf)->len < to) str_append(buf, "", 1);
            (*buf)->len = (int)to; (*buf)->d[to] = 0;
            break;
        }
        default: {
            int isfloat = it.type == 'f' || it.type == 'd' || it.type == 'F';
            if (!isz && !isfloat && it.type != 'U' && it.type != 'W' && it.type != 'w') { pp_die(I, "Invalid type '%c' in pack", it.type); return 0; }
            for (int64_t k = 0; it.star ? *ai < args->n : k < it.count; k++) {
                pp_val v = *ai < args->n ? args->v[(*ai)++] : pp_undef();
                if (isfloat) {
                    char b[8]; int n;
                    if (it.type == 'f') { float f = (float)pp_to_num(v); memcpy(b, &f, 4); n = 4; }
                    else { double d = pp_to_num(v); memcpy(b, &d, 8); n = 8; }
                    if (it.big) for (int x = 0; x < n / 2; x++) { char tmp = b[x]; b[x] = b[n - 1 - x]; b[n - 1 - x] = tmp; }
                    str_append(buf, b, n);
                }
                else if (it.type == 'U') pack_put_utf8(buf, pack_arg_uint(v));
                else if (it.type == 'W') { uint64_t c = pack_arg_uint(v); if (c < 256) pack_put_int(buf, c, 1, 0); else pack_put_utf8(buf, c); }
                else if (it.type == 'w') {      /* BER compressed integer: 7 bits per byte, high bit on all but the last */
                    uint64_t u = pack_arg_uint(v);
                    char b[10]; int n = 0;
                    do { b[n++] = (char)(u & 0x7f); u >>= 7; } while (u);
                    while (n--) { char c = (char)(b[n] | (n ? 0x80 : 0)); str_append(buf, &c, 1); }
                }
                else pack_put_int(buf, pack_arg_uint(v), isz, big);
            }
            break;
        }
        }
    }
}
static pp_val n_pack(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    if (a->n < 1) return pp_die(I, "Not enough arguments for pack");
    pp_str *t = pp_to_str(a->v[0]);
    pp_str *buf = pp_str_new(NULL, 0);
    int ai = 1;
    int ok = pack_run(I, t->d, t->len, a, &ai, &buf, 0, 0);
    pp_str_decref(t);
    if (!ok) { pp_str_decref(buf); return pp_undef(); }
    return pp_str_val(buf);
}

/* unpacks d[*dp...] by the items of t, pushing the values; 0 after a die */
PACK_FN static int unpack_run(pp_interp *I, const char *t, int tl, const unsigned char *d, int dl, int *dp, pp_list *res, int base, int depth) {
    int pos = 0;
    pack_item it;
    int64_t forced = -1;        /* count taken from a LENGTH/ item, for the next item */
    int cks = -1, cks_from = 0; /* a %<bits> in front of the current item: sum its values */
    if (depth > 20) { pp_die(I, "Too deeply nested ()-groups in unpack"); return 0; }
    for (;;) {
        int r = pack_next(I, t, tl, &pos, &it);
        if (r <= 0) return r == 0;
        if (res->n > PACK_MAX) { pp_die(I, "unpack: too many values"); return 0; }
        if (forced >= 0) { it.count = forced; it.star = 0; it.has_count = 1; forced = -1; }
        if (it.type == '%') {   /* %<bits>ITEM: the sum of what ITEM unpacks, kept to that many bits (16 without a number) */
            cks = it.has_count ? (int)it.count : 16;
            cks_from = res->n;
            continue;
        }
        int is_signed, big;
        int isz = pack_int_size(&it, &is_signed, &big);
        int left = dl - *dp;
        switch (it.type) {
        case '(': {
            for (int64_t k = 0; it.star ? *dp < dl : k < it.count; k++) {
                int before = *dp;
                if (!unpack_run(I, it.sub, it.sublen, d, dl, dp, res, *dp, depth + 1)) return 0;
                if (it.star && *dp == before) break;      /* a group that reads nothing would never end */
            }
            break;
        }
        case 'a': case 'A': case 'Z': {
            int64_t n = it.star ? left : it.count;
            if (n > left) n = left;
            int64_t keep = n;
            if (it.type == 'Z') { const void *z = memchr(d + *dp, 0, (size_t)n); if (z) { keep = (const unsigned char *)z - (d + *dp); if (it.star) n = keep + 1; } }
            else if (it.type == 'A') while (keep > 0 && (d[*dp + keep - 1] == ' ' || d[*dp + keep - 1] == 0 || d[*dp + keep - 1] == '\n' || d[*dp + keep - 1] == '\t' || d[*dp + keep - 1] == '\r')) keep--;
            pp_list_push(res, pp_strn((const char *)d + *dp, (int)keep));
            *dp += (int)n;
            break;
        }
        case 'b': case 'B': case 'h': case 'H': {
            int per = it.type == 'b' || it.type == 'B' ? 8 : 2;
            int64_t digits = it.star ? (int64_t)left * per : it.count;
            if (digits > (int64_t)left * per) digits = (int64_t)left * per;
            pp_str *s = pp_str_new(NULL, 0);
            for (int64_t k = 0; k < digits; k++) {
                unsigned byte = d[*dp + k / per];
                unsigned val;
                if (it.type == 'b') val = (byte >> (k % 8)) & 1;
                else if (it.type == 'B') val = (byte >> (7 - k % 8)) & 1;
                else if (it.type == 'h') val = (byte >> (4 * (k % 2))) & 0xf;
                else val = (byte >> (4 * (1 - k % 2))) & 0xf;
                char c = "0123456789abcdef"[val];
                str_append(&s, &c, 1);
            }
            *dp += (int)((digits + per - 1) / per);
            pp_list_push(res, pp_str_val(s));
            break;
        }
        case 'x':
            if (it.bang) { int64_t al = it.count; while (al > 0 && (*dp - base) % al && *dp < dl) (*dp)++; break; }
            if (!it.star && it.count > left) { pp_die(I, "'x' outside of string in unpack"); return 0; }
            *dp += it.star ? left : (int)it.count;
            break;
        case 'X':
            if (!it.star && it.count > *dp) { pp_die(I, "'X' outside of string in unpack"); return 0; }
            *dp -= it.star ? 0 : (int)it.count;
            break;
        case '@': {
            int64_t to = base + (it.star ? 0 : it.count);
            if (to > dl) { pp_die(I, "'@' outside of string in unpack"); return 0; }
            *dp = (int)to;
            break;
        }
        default: {
            int isfloat = it.type == 'f' || it.type == 'd' || it.type == 'F';
            if (!isz && !isfloat && it.type != 'U' && it.type != 'W' && it.type != 'w') { pp_die(I, "Invalid type '%c' in unpack", it.type); return 0; }
            int64_t last = 0;
            for (int64_t k = 0; it.star ? *dp < dl : k < it.count; k++) {
                left = dl - *dp;
                if (isfloat) {
                    int n = it.type == 'f' ? 4 : 8;
                    if (left < n) break;
                    char b[8];
                    for (int x = 0; x < n; x++) b[x] = (char)d[*dp + (it.big ? n - 1 - x : x)];
                    if (n == 4) { float f; memcpy(&f, b, 4); pp_list_push(res, pp_num(f)); }
                    else { double v; memcpy(&v, b, 8); pp_list_push(res, pp_num(v)); }
                    *dp += n;
                }
                else if (it.type == 'U' || (it.type == 'W' && 0)) {
                    if (left < 1) break;
                    unsigned c = d[*dp]; int n = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 1;
                    if (n > left) n = 1;
                    uint64_t cp = n == 1 ? c : c & (0xff >> (n + 1));
                    for (int x = 1; x < n; x++) cp = (cp << 6) | (d[*dp + x] & 0x3f);
                    pp_list_push(res, pp_int((int64_t)cp));
                    *dp += n;
                }
                else if (it.type == 'W') { if (left < 1) break; pp_list_push(res, pp_int(d[(*dp)++])); }
                else if (it.type == 'w') {
                    if (left < 1) break;
                    uint64_t u = 0; int more = 1;
                    while (more && *dp < dl) { unsigned c = d[(*dp)++]; u = (u << 7) | (c & 0x7f); more = c & 0x80; }
                    pp_list_push(res, pp_int((int64_t)u));
                }
                else {
                    if (left < isz) break;
                    uint64_t u = 0;
                    for (int x = 0; x < isz; x++) u |= (uint64_t)d[*dp + (big ? isz - 1 - x : x)] << (8 * x);
                    *dp += isz;
                    if (is_signed && isz < 8 && (u >> (8 * isz - 1))) u |= ~(uint64_t)0 << (8 * isz);
                    last = (int64_t)u;
                    if (it.slash) continue;                 /* a length: used below, not returned */
                    if (!is_signed && isz == 8 && u > (uint64_t)INT64_MAX) pp_list_push(res, pp_num((double)u));
                    else pp_list_push(res, pp_int((int64_t)u));
                }
            }
            if (it.slash) forced = last < 0 ? 0 : last;
            break;
        }
        }
        if (cks >= 0) {
            uint64_t sum = 0; double fsum = 0; int isf = 0;
            for (int k = cks_from; k < res->n; k++) {
                pp_val v = res->v[k];
                if (v.t == PP_STR && (it.type == 'b' || it.type == 'B')) { for (int x = 0; x < v.u.s->len; x++) sum += v.u.s->d[x] == '1'; }   /* bit strings: the number of set bits */
                else if (v.t == PP_NUM) { isf = 1; fsum += v.u.n; }
                else sum += (uint64_t)pp_to_int(v);
                pp_release(v);
            }
            res->n = cks_from;
            if (isf) { fsum += (double)(int64_t)sum; if (cks < 64) { double m = (double)((uint64_t)1 << cks); fsum = fmod(fsum, m); if (fsum < 0) fsum += m; } pp_list_push(res, pp_num(fsum)); }
            else {
                if (cks < 64) sum &= ((uint64_t)1 << cks) - 1;
                if (sum > (uint64_t)INT64_MAX) pp_list_push(res, pp_num((double)sum)); else pp_list_push(res, pp_int((int64_t)sum));
            }
            cks = -1;
        }
    }
}
static pp_val n_unpack(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    if (a->n < 1) return pp_die(I, "Not enough arguments for unpack");
    pp_str *t = pp_to_str(a->v[0]);
    pp_str *d = a->n > 1 ? pp_to_str(a->v[1]) : pp_to_str(I->g_underscore->u.scalar);
    pp_list res = {0};
    int dp = 0;
    int ok = unpack_run(I, t->d, t->len, (const unsigned char *)d->d, d->len, &dp, &res, 0, 0);
    pp_str_decref(t); pp_str_decref(d);
    if (!ok) { pp_list_free(&res); return pp_undef(); }
    if (ctx == CTX_LIST && out) {
        for (int i = 0; i < res.n; i++) { pp_list_push(out, res.v[i]); res.v[i] = pp_undef(); }
        pp_list_free(&res);
        return pp_no_value();
    }
    pp_val first = res.n ? res.v[0] : pp_undef();      /* scalar context: the first value */
    if (res.n) res.v[0] = pp_undef();
    pp_list_free(&res);
    return first;
}

/* ------------------------------------------------------------------ */
/* directories, unlink, rename                                         */
/* ------------------------------------------------------------------ */

static const char *no_fs_msg = NULL;     /* set while a weak default below was the one that ran */
__attribute__((weak)) int pp_host_mkdir(const char *path, int mode) { (void)path; (void)mode; no_fs_msg = "Function not implemented"; return -1; }
__attribute__((weak)) int pp_host_remove(const char *path, int is_dir) { (void)path; (void)is_dir; no_fs_msg = "Function not implemented"; return -1; }
__attribute__((weak)) int pp_host_rename(const char *from, const char *to) { (void)from; (void)to; no_fs_msg = "Function not implemented"; return -1; }
__attribute__((weak)) int pp_host_listdir(const char *path, void (*add)(void *, const char *), void *ctx) { (void)path; (void)add; (void)ctx; no_fs_msg = "Function not implemented"; return -1; }
/* after a failed host call: $! */
static pp_val fs_fail(pp_interp *I) {
    set_errno_var(I, no_fs_msg ? no_fs_msg : pp_host_io_error());
    no_fs_msg = NULL;
    return pp_strn("", 0);
}
/* the path argument: a->v[i], or $_ when there is none */
static pp_str *fs_path(pp_interp *I, pp_list *a, int i) {
    return i < a->n ? pp_to_str(a->v[i]) : pp_to_str(I->g_underscore->u.scalar);
}
static pp_val n_mkdir(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    pp_str *p = fs_path(I, a, 0);
    int r = pp_host_mkdir(p->d, a->n > 1 ? (int)pp_to_int(a->v[1]) : 0777);
    pp_str_decref(p);
    return r == 0 ? pp_int(1) : fs_fail(I);
}
static pp_val n_rmdir(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    pp_str *p = fs_path(I, a, 0);
    int r = pp_host_remove(p->d, 1);
    pp_str_decref(p);
    return r == 0 ? pp_int(1) : fs_fail(I);
}
/* unlink LIST: the number of files removed */
static pp_val n_unlink(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    int done = 0, failed = 0;
    for (int i = 0; i < (a->n ? a->n : 1); i++) {
        pp_str *p = fs_path(I, a, i);
        if (pp_host_remove(p->d, 0) == 0) done++; else failed = 1;
        pp_str_decref(p);
    }
    if (failed) { pp_val f = fs_fail(I); pp_release(f); }
    return pp_int(done);
}
static pp_val n_rename(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    if (a->n < 2) return pp_die(I, "Not enough arguments for rename");
    pp_str *from = pp_to_str(a->v[0]), *to = pp_to_str(a->v[1]);
    int r = pp_host_rename(from->d, to->d);
    pp_str_decref(from); pp_str_decref(to);
    return r == 0 ? pp_int(1) : fs_fail(I);
}
/* A directory handle is an array: [position, name, name, ...], read in full when the directory
   is opened. opendir(my $dh, $path) is parsed as $dh = __opendir($path); a bareword handle
   (opendir(DH, $path)) keeps the array in a hidden global, see dir_handle. */
static void dir_add(void *ctx, const char *name) { pp_array_push((pp_obj *)ctx, pp_cstr(name)); }
static pp_obj *dir_read(pp_interp *I, pp_val pathv) {
    pp_str *p = pp_to_str(pathv);
    pp_obj *arr = pp_obj_new(PP_OARRAY);
    pp_array_push(arr, pp_int(1));
    int r = pp_host_listdir(p->d, dir_add, arr);
    pp_str_decref(p);
    if (r != 0) { pp_obj_decref(arr); pp_val f = fs_fail(I); pp_release(f); return NULL; }
    return arr;
}
static pp_sym *dir_named_sym(pp_interp *I, pp_val name) {
    pp_str *s = pp_to_str(name);
    char buf[264];
    int n = pp_snprintf(buf, sizeof buf, "@\002dir%s", pp_glob_named(I, s->d)->name);
    pp_str_decref(s);
    return pp_intern(I, buf, n);
}
static pp_val n_opendir(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    if (a->n < 1) return pp_die(I, "Not enough arguments for opendir");
    pp_obj *arr = dir_read(I, a->v[0]);
    if (!arr) return pp_undef();
    pp_val r = pp_ref(arr);
    pp_obj_decref(arr);
    return r;
}
static pp_val n_opendir_named(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    if (a->n < 2) return pp_die(I, "Not enough arguments for opendir");
    pp_obj *arr = dir_read(I, a->v[1]);
    if (!arr) return pp_strn("", 0);
    pp_obj *g = pp_global(I, dir_named_sym(I, a->v[0]));
    pp_array_clear(g);
    for (int i = 0; i < arr->u.arr.n; i++) pp_array_push(g, pp_copy(arr->u.arr.v[i]));
    pp_obj_decref(arr);
    return pp_int(1);
}
/* the array behind a directory handle (borrowed), NULL + die if the value is none */
static pp_obj *dir_handle(pp_interp *I, pp_list *a, const char *what) {
    pp_obj *arr = NULL;
    if (a->n > 0 && a->v[0].t == PP_REF && a->v[0].u.o->kind == PP_OARRAY) arr = a->v[0].u.o;
    else if (a->n > 0 && (a->v[0].t == PP_STR || (a->v[0].t == PP_REF && a->v[0].u.o->kind == PP_OGLOB))) {
        pp_val name = a->v[0].t == PP_REF ? a->v[0].u.o->u.glob.name : a->v[0];
        pp_sym *s = dir_named_sym(I, name);
        if (s->gcell) arr = s->gcell;
    }
    if (!arr || arr->u.arr.n < 1 || arr->u.arr.v[0].t != PP_INT) { pp_die(I, "%s() attempted on invalid dirhandle", what); return NULL; }
    return arr;
}
static pp_val n_readdir(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    pp_obj *d = dir_handle(I, a, "readdir");
    if (!d) return pp_undef();
    int64_t pos = d->u.arr.v[0].u.i;
    if (pos < 1) pos = 1;
    if (ctx == CTX_LIST && out) {
        for (; pos < d->u.arr.n; pos++) pp_list_push(out, pp_copy(d->u.arr.v[pos]));
        d->u.arr.v[0] = pp_int(pos);
        return pp_no_value();
    }
    if (pos >= d->u.arr.n) return pp_undef();
    d->u.arr.v[0] = pp_int(pos + 1);
    return pp_copy(d->u.arr.v[pos]);
}
static pp_val n_rewinddir(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    pp_obj *d = dir_handle(I, a, "rewinddir");
    if (!d) return pp_undef();
    d->u.arr.v[0] = pp_int(1);
    return pp_int(1);
}
static pp_val n_closedir(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    pp_obj *d = dir_handle(I, a, "closedir");
    if (!d) return pp_undef();
    while (d->u.arr.n > 0) { pp_val v = d->u.arr.v[--d->u.arr.n]; pp_release(v); }     /* no longer a handle */
    return pp_int(1);
}

/* fileno(HANDLE): 0 / 1 / 2 for the standard handles, 3 for any other open file, undef otherwise */
static pp_val n_fileno(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    if (a->n < 1) return pp_undef();
    int fd;
    if (tied_handle(I, a->v[0])) { int done; return tied_handle_call(I, pp_copy(a->v[0]), "FILENO", NULL, CTX_SCALAR, NULL, &done); }
    pp_obj *f = handle_of(I, a->v[0], &fd);
    if (f) return f->u.file.fp ? pp_int(3) : pp_undef();
    return fd >= 0 ? pp_int(fd) : pp_undef();
}
/* -t HANDLE: the standard handles are a terminal when the host says so; a file never is */
__attribute__((weak)) int pp_host_isatty(int fd) { (void)fd; return 1; }   /* a board: the USB serial port */
static pp_val n_isatty(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    int fd = -1;
    if (a->n < 1 || handle_of(I, a->v[0], &fd) || fd < 0) return fd < 0 && !(a->n && a->v[0].t == PP_REF && a->v[0].u.o->kind == PP_OFILE) ? pp_undef() : pp_strn("", 0);
    return pp_host_isatty(fd) ? pp_int(1) : pp_strn("", 0);
}
/* tie VAR, CLASS, LIST / tied VAR / untie VAR: the parser passes \VAR (a glob for a handle) */
static pp_val n_tie(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    if (a->n < 2 || a->v[0].t != PP_REF) return pp_die(I, "Can't modify constant item in tie");
    pp_obj *var = a->v[0].u.o;
    const char *m = var->kind == PP_OARRAY ? "TIEARRAY" : var->kind == PP_OHASH ? "TIEHASH" : var->kind == PP_OGLOB ? "TIEHANDLE" : "TIESCALAR";
    pp_list rest = {0};
    for (int i = 2; i < a->n; i++) pp_list_push(&rest, pp_copy(a->v[i]));
    var->refcnt++;                        /* the constructor may drop the last reference to it */
    pp_val obj = pp_tie_call(I, a->v[1], m, &rest, CTX_SCALAR, NULL, 1);
    if (I->ctl) { pp_obj_decref(var); pp_release(obj); return pp_undef(); }
    pp_tie_set(var, pp_copy(obj));
    pp_obj_decref(var);
    return obj;
}
static pp_val n_tied(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    pp_val *t = a->n && a->v[0].t == PP_REF ? pp_tie_find(a->v[0].u.o) : NULL;
    return t ? pp_copy(*t) : pp_undef();
}
static pp_val n_untie(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    if (a->n && a->v[0].t == PP_REF && a->v[0].u.o->tied) pp_tie_remove(a->v[0].u.o);
    return pp_int(1);
}
/* overload::_enable(): some class overloads operators from now on.
   overload::StrVal($x): the plain Class=HASH(0x...) form, whatever the class overloads */
static pp_val n_ovl_enable(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)a; (void)ctx; (void)out;
    pp_ovl_enable(I);
    return pp_int(1);
}
static pp_val n_ovl_strval(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    if (a->n < 1) return pp_undef();
    int (*saved)(pp_val, int, pp_val *) = pp_ovl_hook;
    pp_ovl_hook = NULL;
    pp_str *s = pp_to_str(a->v[0]);
    pp_ovl_hook = saved;
    return pp_str_val(s);
}
/* re::is_regexp($x): is it a qr// object (blessed or not)?
   re::regexp_pattern($qr): (pattern, flags) in list context, "(?^flags:pattern)" in scalar context */
static pp_val n_re_is_regexp(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    return a->n && a->v[0].t == PP_REF && a->v[0].u.o->kind == PP_OREGEX ? pp_int(1) : pp_strn("", 0);
}
static pp_val n_re_pattern(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I;
    if (!a->n || a->v[0].t != PP_REF || a->v[0].u.o->kind != PP_OREGEX) return ctx == CTX_LIST && out ? pp_no_value() : pp_strn("", 0);
    const pp_regex *re = a->v[0].u.o->u.re;
    if (!(ctx == CTX_LIST && out)) {
        int (*saved)(pp_val, int, pp_val *) = pp_ovl_hook;
        pp_ovl_hook = NULL;
        pp_str *s = pp_to_str(a->v[0]);
        pp_ovl_hook = saved;
        return pp_str_val(s);
    }
    char fl[8]; int n = 0;
    if (re->flags & PP_RE_M) fl[n++] = 'm';
    if (re->flags & PP_RE_S) fl[n++] = 's';
    if (re->flags & PP_RE_I) fl[n++] = 'i';
    if (re->flags & PP_RE_X) fl[n++] = 'x';
    pp_list_push(out, pp_strn(re->source, re->srclen));
    return pp_strn(fl, n);                 /* the caller appends the last element */
}
/* Constants that the interpreter itself provides, created the first time one is used (see
   resolve_depth). For now the ones of Fcntl (lib/Fcntl.pm; the real module is XS): about a
   hundred names, of which a script uses a handful, so they are kept here in ROM instead of
   being defined by the module. The values are those of Linux. */
static const struct { const char *name; uint32_t val; } fcntl_consts[] = {
    {"FD_CLOEXEC", 1u}, {"F_DUPFD", 0u}, {"F_EXLCK", 4u}, {"F_GETFD", 1u}, {"F_GETFL", 3u}, {"F_GETLK", 5u},
    {"F_GETLK64", 5u}, {"F_GETOWN", 9u}, {"F_RDLCK", 0u}, {"F_SETFD", 2u}, {"F_SETFL", 4u}, {"F_SETLK", 6u},
    {"F_SETLK64", 6u}, {"F_SETLKW", 7u}, {"F_SETLKW64", 7u}, {"F_SETOWN", 8u}, {"F_SHLCK", 8u},
    {"F_UNLCK", 2u}, {"F_WRLCK", 1u}, {"O_ACCMODE", 3u}, {"O_APPEND", 1024u}, {"O_ASYNC", 8192u},
    {"O_BINARY", 0u}, {"O_CREAT", 64u}, {"O_DIRECT", 16384u}, {"O_DIRECTORY", 65536u}, {"O_DSYNC", 4096u},
    {"O_EXCL", 128u}, {"O_LARGEFILE", 0u}, {"O_NDELAY", 2048u}, {"O_NOCTTY", 256u}, {"O_NOFOLLOW", 131072u},
    {"O_NONBLOCK", 2048u}, {"O_RDONLY", 0u}, {"O_RDWR", 2u}, {"O_RSYNC", 1052672u}, {"O_SYNC", 1052672u},
    {"O_TEXT", 0u}, {"O_TRUNC", 512u}, {"O_WRONLY", 1u}, {"DN_ACCESS", 1u}, {"DN_ATTRIB", 32u},
    {"DN_CREATE", 4u}, {"DN_DELETE", 8u}, {"DN_MODIFY", 2u}, {"DN_MULTISHOT", 2147483648u},
    {"DN_RENAME", 16u}, {"F_ADD_SEALS", 1033u}, {"F_GETLEASE", 1025u}, {"F_GETPIPE_SZ", 1032u},
    {"F_GET_SEALS", 1034u}, {"F_GETSIG", 11u}, {"F_NOTIFY", 1026u}, {"F_SEAL_FUTURE_WRITE", 16u},
    {"F_SEAL_GROW", 4u}, {"F_SEAL_SEAL", 1u}, {"F_SEAL_SHRINK", 2u}, {"F_SEAL_WRITE", 8u},
    {"F_SETLEASE", 1024u}, {"F_SETPIPE_SZ", 1031u}, {"F_SETSIG", 10u}, {"LOCK_MAND", 32u}, {"LOCK_READ", 64u},
    {"LOCK_RW", 192u}, {"LOCK_WRITE", 128u}, {"O_NOATIME", 262144u}, {"O_TMPFILE", 4259840u},
    {"FAPPEND", 1024u}, {"FASYNC", 8192u}, {"FNDELAY", 2048u}, {"FNONBLOCK", 2048u}, {"S_ISUID", 2048u},
    {"S_ISGID", 1024u}, {"S_ISVTX", 512u}, {"_S_IFMT", 61440u}, {"S_IFREG", 32768u}, {"S_IFDIR", 16384u},
    {"S_IFLNK", 40960u}, {"S_IFSOCK", 49152u}, {"S_IFBLK", 24576u}, {"S_IFCHR", 8192u}, {"S_IFIFO", 4096u},
    {"S_IRUSR", 256u}, {"S_IWUSR", 128u}, {"S_IXUSR", 64u}, {"S_IRWXU", 448u}, {"S_IRGRP", 32u},
    {"S_IWGRP", 16u}, {"S_IXGRP", 8u}, {"S_IRWXG", 56u}, {"S_IROTH", 4u}, {"S_IWOTH", 2u}, {"S_IXOTH", 1u},
    {"S_IRWXO", 7u}, {"S_IREAD", 256u}, {"S_IWRITE", 128u}, {"S_IEXEC", 64u}, {"LOCK_SH", 1u},
    {"LOCK_EX", 2u}, {"LOCK_NB", 4u}, {"LOCK_UN", 8u}, {"SEEK_SET", 0u}, {"SEEK_CUR", 1u}, {"SEEK_END", 2u},
};
/* ... and the error numbers of Errno (lib/Errno.pm): ENOENT, EEXIST, ... as on Linux, which
   are also the numbers $! takes here. "Errno::_NAMES" is all the names in one string, for
   keys %!. */
static const struct { const char *name; uint8_t val; } errno_consts[] = {
    {"EPERM", 1}, {"ENOENT", 2}, {"ESRCH", 3}, {"EINTR", 4}, {"EIO", 5}, {"ENXIO", 6}, {"E2BIG", 7},
    {"ENOEXEC", 8}, {"EBADF", 9}, {"ECHILD", 10}, {"EAGAIN", 11}, {"EWOULDBLOCK", 11}, {"ENOMEM", 12},
    {"EACCES", 13}, {"EFAULT", 14}, {"ENOTBLK", 15}, {"EBUSY", 16}, {"EEXIST", 17}, {"EXDEV", 18},
    {"ENODEV", 19}, {"ENOTDIR", 20}, {"EISDIR", 21}, {"EINVAL", 22}, {"ENFILE", 23}, {"EMFILE", 24},
    {"ENOTTY", 25}, {"ETXTBSY", 26}, {"EFBIG", 27}, {"ENOSPC", 28}, {"ESPIPE", 29}, {"EROFS", 30},
    {"EMLINK", 31}, {"EPIPE", 32}, {"EDOM", 33}, {"ERANGE", 34}, {"EDEADLK", 35}, {"EDEADLOCK", 35},
    {"ENAMETOOLONG", 36}, {"ENOLCK", 37}, {"ENOSYS", 38}, {"ENOTEMPTY", 39}, {"ELOOP", 40}, {"ENOMSG", 42},
    {"EIDRM", 43}, {"ECHRNG", 44}, {"EL2NSYNC", 45}, {"EL3HLT", 46}, {"EL3RST", 47}, {"ELNRNG", 48},
    {"EUNATCH", 49}, {"ENOCSI", 50}, {"EL2HLT", 51}, {"EBADE", 52}, {"EBADR", 53}, {"EXFULL", 54},
    {"ENOANO", 55}, {"EBADRQC", 56}, {"EBADSLT", 57}, {"EBFONT", 59}, {"ENOSTR", 60}, {"ENODATA", 61},
    {"ETIME", 62}, {"ENOSR", 63}, {"ENONET", 64}, {"ENOPKG", 65}, {"EREMOTE", 66}, {"ENOLINK", 67},
    {"EADV", 68}, {"ESRMNT", 69}, {"ECOMM", 70}, {"EPROTO", 71}, {"EMULTIHOP", 72}, {"EDOTDOT", 73},
    {"EBADMSG", 74}, {"EOVERFLOW", 75}, {"ENOTUNIQ", 76}, {"EBADFD", 77}, {"EREMCHG", 78}, {"ELIBACC", 79},
    {"ELIBBAD", 80}, {"ELIBSCN", 81}, {"ELIBMAX", 82}, {"ELIBEXEC", 83}, {"EILSEQ", 84}, {"ERESTART", 85},
    {"ESTRPIPE", 86}, {"EUSERS", 87}, {"ENOTSOCK", 88}, {"EDESTADDRREQ", 89}, {"EMSGSIZE", 90},
    {"EPROTOTYPE", 91}, {"ENOPROTOOPT", 92}, {"EPROTONOSUPPORT", 93}, {"ESOCKTNOSUPPORT", 94},
    {"ENOTSUP", 95}, {"EOPNOTSUPP", 95}, {"EPFNOSUPPORT", 96}, {"EAFNOSUPPORT", 97}, {"EADDRINUSE", 98},
    {"EADDRNOTAVAIL", 99}, {"ENETDOWN", 100}, {"ENETUNREACH", 101}, {"ENETRESET", 102}, {"ECONNABORTED", 103},
    {"ECONNRESET", 104}, {"ENOBUFS", 105}, {"EISCONN", 106}, {"ENOTCONN", 107}, {"ESHUTDOWN", 108},
    {"ETOOMANYREFS", 109}, {"ETIMEDOUT", 110}, {"ECONNREFUSED", 111}, {"EHOSTDOWN", 112},
    {"EHOSTUNREACH", 113}, {"EALREADY", 114}, {"EINPROGRESS", 115}, {"ESTALE", 116}, {"EUCLEAN", 117},
    {"ENOTNAM", 118}, {"ENAVAIL", 119}, {"EISNAM", 120}, {"EREMOTEIO", 121}, {"EDQUOT", 122},
    {"ENOMEDIUM", 123}, {"EMEDIUMTYPE", 124}, {"ECANCELED", 125}, {"ENOKEY", 126}, {"EKEYEXPIRED", 127},
    {"EKEYREVOKED", 128}, {"EKEYREJECTED", 129}, {"EOWNERDEAD", 130}, {"ENOTRECOVERABLE", 131},
    {"ERFKILL", 132}, {"EHWPOISON", 133},
};

/* ---------------- POSIX (the part that makes sense here) ----------------
   lib/POSIX.pm exports these by name on first use; none of it costs memory until then. */
#define POSIX_MATH1(fname, expr) \
    static pp_val fname(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; double x = a->n ? pp_to_num(a->v[0]) : 0; return pp_num(expr); }
POSIX_MATH1(n_px_round, round(x)) POSIX_MATH1(n_px_trunc, trunc(x)) POSIX_MATH1(n_px_tan, tan(x))
POSIX_MATH1(n_px_asin, asin(x)) POSIX_MATH1(n_px_acos, acos(x)) POSIX_MATH1(n_px_atan, atan(x))
POSIX_MATH1(n_px_sinh, sinh(x)) POSIX_MATH1(n_px_cosh, cosh(x)) POSIX_MATH1(n_px_tanh, tanh(x))
POSIX_MATH1(n_px_log10, log10(x)) POSIX_MATH1(n_px_log2, log2(x)) POSIX_MATH1(n_px_fabs, fabs(x))
static pp_val n_px_fmod(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; return pp_num(fmod(a->n ? pp_to_num(a->v[0]) : 0, a->n > 1 ? pp_to_num(a->v[1]) : 0)); }
static pp_val n_px_pow(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; return pp_num(pow(a->n ? pp_to_num(a->v[0]) : 0, a->n > 1 ? pp_to_num(a->v[1]) : 0)); }
static pp_val n_px_isnan(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; return pp_int(a->n && isnan(pp_to_num(a->v[0]))); }
static pp_val n_px_isinf(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; return pp_int(a->n && isinf(pp_to_num(a->v[0]))); }
/* the wait status macros (the status is what $? holds) */
static pp_val n_px_wexitstatus(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; return pp_int(((a->n ? pp_to_int(a->v[0]) : 0) >> 8) & 255); }
static pp_val n_px_wifexited(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; return pp_int(((a->n ? pp_to_int(a->v[0]) : 0) & 127) == 0); }
static pp_val n_px_wtermsig(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; return pp_int((a->n ? pp_to_int(a->v[0]) : 0) & 127); }
static pp_val n_px_wifsignaled(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; int64_t s = (a->n ? pp_to_int(a->v[0]) : 0) & 127; return pp_int(s != 0 && s != 127); }
static pp_val n_px_exit(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)ctx; (void)out; pp_request_exit(I, a->n ? (int)pp_to_int(a->v[0]) : 0); return pp_undef(); }
static pp_val n_px_getpid(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)a; (void)ctx; (void)out; return pp_copy(pp_global(I, pp_intern(I, "$$", 2))->u.scalar); }
/* access(PATH, MODE): "0 but true" when the file is there and allows it (R_OK | W_OK | X_OK), else undef */
static pp_val n_px_access(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    pp_str *p = a->n ? pp_to_str(a->v[0]) : pp_str_new(NULL, 0);
    int mode = a->n > 1 ? (int)pp_to_int(a->v[1]) : 0;
    long long size = 0;
    int m = pp_host_filetest(p->d, &size);
    pp_str_decref(p);
    int ok = m >= 0 && (!(mode & 4) || (m & PP_FT_READ)) && (!(mode & 2) || (m & PP_FT_WRITE)) && (!(mode & 1) || (m & PP_FT_EXEC));
    if (!ok) { set_errno_var(I, m < 0 ? "No such file or directory" : "Permission denied"); return pp_undef(); }
    return pp_strn("0 but true", 10);
}
/* tzset / tzname: there is one time zone */
static pp_val n_px_tzname(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)a;
    if (ctx == CTX_LIST && out) { pp_list_push(out, pp_strn("UTC", 3)); pp_list_push(out, pp_strn("UTC", 3)); return pp_no_value(); }
    return pp_strn("UTC", 3);
}
/* setlocale: there is the "C" locale and nothing else */
static pp_val n_px_setlocale(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    if (a->n > 1 && a->v[1].t != PP_UNDEF) {
        pp_str *l = pp_to_str(a->v[1]);
        int ok = l->len == 0 || !strcmp(l->d, "C") || !strcmp(l->d, "POSIX");
        pp_str_decref(l);
        if (!ok) return pp_undef();
    }
    return pp_strn("C", 1);
}
static pp_val n_px_localeconv(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)a; (void)ctx; (void)out;
    pp_obj *h = pp_obj_new(PP_OHASH);
    pp_hash_set(h, "decimal_point", 13, pp_strn(".", 1));
    pp_val r = pp_ref(h); pp_obj_decref(h);
    return r;
}
/* strtol(STR, BASE = 10): the number, and in list context how many characters were left over */
static pp_val n_px_strtol(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I;
    pp_str *s = a->n ? pp_to_str(a->v[0]) : pp_str_new(NULL, 0);
    int base = a->n > 1 ? (int)pp_to_int(a->v[1]) : 10;
    char *end = s->d;
    long long v = base == 0 || (base >= 2 && base <= 36) ? strtoll(s->d, &end, base) : 0;
    int left = s->len - (int)(end - s->d);
    pp_str_decref(s);
    if (ctx == CTX_LIST && out) { pp_list_push(out, pp_int(v)); pp_list_push(out, pp_int(left)); return pp_no_value(); }
    return pp_int(v);
}
static pp_val n_px_strtod(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I;
    pp_str *s = a->n ? pp_to_str(a->v[0]) : pp_str_new(NULL, 0);
    char *end = s->d;
    double v = strtod(s->d, &end);
    int left = s->len - (int)(end - s->d);
    pp_str_decref(s);
    if (ctx == CTX_LIST && out) { pp_list_push(out, pp_num(v)); pp_list_push(out, pp_int(left)); return pp_no_value(); }
    return pp_num(v);
}
/* strftime(FMT, SEC, MIN, HOUR, MDAY, MON, YEAR, ...): the fields are taken as they are (the
   week day and the day of the year are worked out from the date, as perl does); English
   names, and UTC for %Z / %z (there are no time zones here) */
static int px_days_before(int y, int m) {     /* days from 1970-01-01 to the first of month m (0-based) of year y */
    static const int cum[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    y += m / 12; m %= 12; if (m < 0) { m += 12; y--; }
    long yy = y - 1;
    long days = 365L * (y - 1970) + (yy / 4 - yy / 100 + yy / 400) - (1969 / 4 - 1969 / 100 + 1969 / 400) + cum[m];
    if (m > 1 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) days++;
    return (int)days;
}
static pp_val n_px_strftime(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    static const char *const wd[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
    static const char *const mn[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
    if (a->n < 7) return pp_die(I, "Usage: POSIX::strftime(fmt, sec, min, hour, mday, mon, year, wday = -1, yday = -1, isdst = -1)");
    pp_str *f = pp_to_str(a->v[0]);
    int sec = (int)pp_to_int(a->v[1]), min = (int)pp_to_int(a->v[2]), hour = (int)pp_to_int(a->v[3]);
    int mday = (int)pp_to_int(a->v[4]), mon = (int)pp_to_int(a->v[5]), year = (int)pp_to_int(a->v[6]) + 1900;
    year += mon / 12; mon %= 12; if (mon < 0) { mon += 12; year--; }
    int days = px_days_before(year, mon) + mday - 1;
    {   /* fields out of range roll over, as with mktime (and as perl 5.42 and later do):
           January 32nd is February 1st, second 60 is the next minute */
        long long t = (long long)days * 86400 + (long long)hour * 3600 + (long long)min * 60 + sec;
        long long d = t / 86400, rem = t % 86400;
        if (rem < 0) { rem += 86400; d--; }
        days = (int)d; hour = (int)(rem / 3600); min = (int)(rem % 3600 / 60); sec = (int)(rem % 60);
        long long z = d + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
        long long doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
        mday = (int)(doy - (153 * mp + 2) / 5 + 1);
        mon = (int)(mp < 10 ? mp + 2 : mp - 10);
        year = (int)(yoe + era * 400 + (mon <= 1));
    }
    int wday = ((days % 7) + 11) % 7;                      /* 1970-01-01 was a Thursday */
    int yday = days - px_days_before(year, 0);
    pp_str *r = pp_str_new(NULL, 0);
    char b[48];
    for (int i = 0; i < f->len; i++) {
        if (f->d[i] != '%' || i + 1 >= f->len) { str_append(&r, f->d + i, 1); continue; }
        char c = f->d[++i];
        int n = 0, h12 = hour % 12 ? hour % 12 : 12;
        switch (c) {
        case 'Y': n = pp_snprintf(b, sizeof b, "%d", year); break;
        case 'C': n = pp_snprintf(b, sizeof b, "%02d", year / 100); break;
        case 'y': n = pp_snprintf(b, sizeof b, "%02d", ((year % 100) + 100) % 100); break;
        case 'm': n = pp_snprintf(b, sizeof b, "%02d", mon + 1); break;
        case 'd': n = pp_snprintf(b, sizeof b, "%02d", mday); break;
        case 'e': n = pp_snprintf(b, sizeof b, "%2d", mday); break;
        case 'H': n = pp_snprintf(b, sizeof b, "%02d", hour); break;
        case 'I': n = pp_snprintf(b, sizeof b, "%02d", h12); break;
        case 'M': n = pp_snprintf(b, sizeof b, "%02d", min); break;
        case 'S': n = pp_snprintf(b, sizeof b, "%02d", sec); break;
        case 'j': n = pp_snprintf(b, sizeof b, "%03d", yday + 1); break;
        case 'u': n = pp_snprintf(b, sizeof b, "%d", wday ? wday : 7); break;
        case 'w': n = pp_snprintf(b, sizeof b, "%d", wday); break;
        case 'a': n = pp_snprintf(b, sizeof b, "%.3s", wd[wday]); break;
        case 'A': n = pp_snprintf(b, sizeof b, "%s", wd[wday]); break;
        case 'b': case 'h': n = pp_snprintf(b, sizeof b, "%.3s", mn[mon]); break;
        case 'B': n = pp_snprintf(b, sizeof b, "%s", mn[mon]); break;
        case 'p': n = pp_snprintf(b, sizeof b, "%s", hour < 12 ? "AM" : "PM"); break;
        case 'F': n = pp_snprintf(b, sizeof b, "%d-%02d-%02d", year, mon + 1, mday); break;
        case 'T': n = pp_snprintf(b, sizeof b, "%02d:%02d:%02d", hour, min, sec); break;
        case 'R': n = pp_snprintf(b, sizeof b, "%02d:%02d", hour, min); break;
        case 'D': case 'x': n = pp_snprintf(b, sizeof b, "%02d/%02d/%02d", mon + 1, mday, ((year % 100) + 100) % 100); break;
        case 'X': n = pp_snprintf(b, sizeof b, "%02d:%02d:%02d", hour, min, sec); break;
        case 'r': n = pp_snprintf(b, sizeof b, "%02d:%02d:%02d %s", h12, min, sec, hour < 12 ? "AM" : "PM"); break;
        case 'c': n = pp_snprintf(b, sizeof b, "%.3s %.3s %2d %02d:%02d:%02d %d", wd[wday], mn[mon], mday, hour, min, sec, year); break;
        case 's': n = pp_snprintf(b, sizeof b, "%lld", (long long)days * 86400 + hour * 3600 + min * 60 + sec); break;
        case 'Z': n = pp_snprintf(b, sizeof b, "UTC"); break;
        case 'z': n = pp_snprintf(b, sizeof b, "+0000"); break;
        case 'n': b[0] = '\n'; n = 1; break;
        case 't': b[0] = '\t'; n = 1; break;
        case '%': b[0] = '%'; n = 1; break;
        default:  b[0] = '%'; b[1] = c; n = 2; break;
        }
        str_append(&r, b, n);
    }
    pp_str_decref(f);
    return pp_str_val(r);
}
static const struct { const char *name; int64_t val; } posix_consts[] = {
    {"SIGHUP", 1}, {"SIGINT", 2}, {"SIGQUIT", 3}, {"SIGILL", 4}, {"SIGTRAP", 5}, {"SIGABRT", 6}, {"SIGBUS", 7}, {"SIGFPE", 8},
    {"SIGKILL", 9}, {"SIGUSR1", 10}, {"SIGSEGV", 11}, {"SIGUSR2", 12}, {"SIGPIPE", 13}, {"SIGALRM", 14}, {"SIGTERM", 15},
    {"SIGCHLD", 17}, {"SIGCONT", 18}, {"SIGSTOP", 19}, {"SIGTSTP", 20}, {"SIGTTIN", 21}, {"SIGTTOU", 22},
    {"SIG_BLOCK", 0}, {"SIG_UNBLOCK", 1}, {"SIG_SETMASK", 2},
    {"WNOHANG", 1}, {"WUNTRACED", 2},
    {"LC_CTYPE", 0}, {"LC_NUMERIC", 1}, {"LC_TIME", 2}, {"LC_COLLATE", 3}, {"LC_MONETARY", 4}, {"LC_MESSAGES", 5}, {"LC_ALL", 6},
    {"EXIT_SUCCESS", 0}, {"EXIT_FAILURE", 1},
    {"R_OK", 4}, {"W_OK", 2}, {"X_OK", 1}, {"F_OK", 0},
    {"STDIN_FILENO", 0}, {"STDOUT_FILENO", 1}, {"STDERR_FILENO", 2},
    {"CHAR_BIT", 8}, {"CHAR_MAX", 127}, {"CHAR_MIN", -128}, {"SCHAR_MAX", 127}, {"SCHAR_MIN", -128}, {"UCHAR_MAX", 255},
    {"SHRT_MAX", 32767}, {"SHRT_MIN", -32768}, {"USHRT_MAX", 65535},
    {"INT_MAX", 2147483647}, {"INT_MIN", -2147483647 - 1}, {"UINT_MAX", 4294967295LL},
    {"LONG_MAX", INT64_MAX}, {"LONG_MIN", INT64_MIN},
    {"PATH_MAX", 4096}, {"NAME_MAX", 255}, {"BUFSIZ", 8192}, {"EOF", -1}, {"RAND_MAX", 2147483647}, {"CLOCKS_PER_SEC", 1000000},
    {"DBL_DIG", 15}, {"DBL_MANT_DIG", 53}, {"FLT_DIG", 6}, {"FLT_MANT_DIG", 24}, {"FLT_RADIX", 2},
};
static const struct { const char *name; double val; } posix_fconsts[] = {
    {"DBL_MAX", 1.7976931348623157e308}, {"DBL_MIN", 2.2250738585072014e-308}, {"DBL_EPSILON", 2.2204460492503131e-16},
    {"FLT_MAX", 3.40282346638528859812e38}, {"FLT_MIN", 1.17549435082228750797e-38}, {"FLT_EPSILON", 1.1920928955078125e-07},
    {"ULONG_MAX", 18446744073709551615.0},
    {"M_PI", 3.14159265358979323846}, {"M_E", 2.7182818284590452354}, {"M_SQRT2", 1.41421356237309504880},
    {"M_LN2", 0.69314718055994530942}, {"M_LN10", 2.30258509299404568402},
};
int pp_core_const(const char *name, pp_val *out) {
    int posix = !strncmp(name, "POSIX::", 7);
    if (posix) {
        name += 7;
        for (size_t i = 0; i < sizeof posix_consts / sizeof posix_consts[0]; i++)
            if (!strcmp(posix_consts[i].name, name)) { *out = pp_int(posix_consts[i].val); return 1; }
        for (size_t i = 0; i < sizeof posix_fconsts / sizeof posix_fconsts[0]; i++)
            if (!strcmp(posix_fconsts[i].name, name)) { *out = pp_num(posix_fconsts[i].val); return 1; }
        if (!strcmp(name, "HUGE_VAL") || !strcmp(name, "INFINITY")) { *out = pp_num(INFINITY); return 1; }
        if (!strcmp(name, "NAN")) { *out = pp_num(NAN); return 1; }
        /* POSIX has the fcntl.h and errno.h constants too (O_RDONLY, SEEK_SET, EAGAIN ...) */
        for (size_t i = 0; i < sizeof fcntl_consts / sizeof fcntl_consts[0]; i++)
            if (!strcmp(fcntl_consts[i].name, name)) { *out = pp_int((int64_t)fcntl_consts[i].val); return 1; }
        if (name[0] == 'E')
            for (size_t i = 0; i < sizeof errno_consts / sizeof errno_consts[0]; i++)
                if (!strcmp(errno_consts[i].name, name)) { *out = pp_int(errno_consts[i].val); return 1; }
        return 0;
    }
    if (!strncmp(name, "Fcntl::", 7)) {
        name += 7;
        for (size_t i = 0; i < sizeof fcntl_consts / sizeof fcntl_consts[0]; i++)
            if (!strcmp(fcntl_consts[i].name, name)) { *out = pp_int((int64_t)fcntl_consts[i].val); return 1; }
        return 0;
    }
    if (!strncmp(name, "Errno::", 7)) {
        name += 7;
        if (name[0] != 'E' && strcmp(name, "_NAMES")) return 0;
        size_t n = sizeof errno_consts / sizeof errno_consts[0];
        if (name[0] == '_') {
            pp_str *all = pp_str_new(NULL, 0);
            for (size_t i = 0; i < n; i++) { if (i) str_append(&all, " ", 1); str_append(&all, errno_consts[i].name, (int)strlen(errno_consts[i].name)); }
            *out = pp_str_val(all);
            return 1;
        }
        for (size_t i = 0; i < n; i++)
            if (!strcmp(errno_consts[i].name, name)) { *out = pp_int(errno_consts[i].val); return 1; }
    }
    return 0;
}

/* umask: there are no file permissions to mask here; the value is only remembered */
static pp_val n_umask(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    int old = umask_value;
    if (a->n > 0 && a->v[0].t != PP_UNDEF) umask_value = (int)pp_to_int(a->v[0]) & 0777;
    return pp_int(old);
}

/* times: (user, system, child user, child system) CPU seconds. There are no processes to
   tell apart here: the time since start counts as user time, the rest is 0. */
static pp_val n_times(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)a;
    double up = (double)pp_host_millis() / 1000.0;
    if (ctx == CTX_LIST && out) {
        pp_list_push(out, pp_num(up)); pp_list_push(out, pp_num(0)); pp_list_push(out, pp_num(0)); pp_list_push(out, pp_num(0));
        return pp_no_value();
    }
    return pp_num(up);
}

/* ---------------- MIME::Base64 / MIME::QuotedPrint ----------------
   The functions the XS part of MIME::Base64 provides, written after its algorithms so that
   line breaks and the odd cases come out the same. lib/MIME/Base64.pm and QuotedPrint.pm
   only export them. */
static const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
/* the optional EOL argument (argument `at`): "\n" when left out or undef */
static pp_str *mime_eol(pp_list *a, int at) {
    if (a->n > at && a->v[at].t != PP_UNDEF) return pp_to_str(a->v[at]);
    return pp_str_new("\n", 1);
}
static pp_val n_b64_encode(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    pp_str *s = a->n ? pp_to_str(a->v[0]) : pp_str_new(NULL, 0);
    pp_str *eol = mime_eol(a, 1);
    pp_str *r = pp_str_new(NULL, 0);
    const unsigned char *p = (const unsigned char *)s->d;
    int len = s->len, chunk = 0;
    for (; len > 0; len -= 3, chunk++) {
        if (chunk == 19) { str_append(&r, eol->d, eol->len); chunk = 0; }       /* 76 characters a line */
        unsigned c1 = *p++, c2 = len > 1 ? *p++ : 0, c3 = len > 2 ? *p++ : 0;
        char q[4] = { b64_chars[c1 >> 2], b64_chars[((c1 & 3) << 4) | (c2 >> 4)],
                      len > 1 ? b64_chars[((c2 & 15) << 2) | (c3 >> 6)] : '=', len > 2 ? b64_chars[c3 & 63] : '=' };
        str_append(&r, q, 4);
    }
    if (r->len) str_append(&r, eol->d, eol->len);
    pp_str_decref(s); pp_str_decref(eol);
    return pp_str_val(r);
}
/* 0..63 for a character of the alphabet, 64 for '=', -1 for anything else (skipped) */
static int b64_value(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    return c == '+' ? 62 : c == '/' ? 63 : c == '=' ? 64 : -1;
}
static pp_val n_b64_decode(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    pp_str *s = a->n ? pp_to_str(a->v[0]) : pp_str_new(NULL, 0);
    pp_str *r = pp_str_new(NULL, 0);
    const unsigned char *p = (const unsigned char *)s->d, *end = p + s->len;
    while (p < end) {
        int c[4], i = 0, stop = 0;
        do {
            int v = b64_value(*p++);
            if (v >= 0) c[i++] = v;
            if (p == end) {
                if (i < 4) { if (i < 2) { stop = 1; break; } if (i == 2) c[2] = 64; c[3] = 64; }
                break;
            }
        } while (i < 4);
        if (stop || c[0] == 64 || c[1] == 64) break;
        char o = (char)((c[0] << 2) | ((c[1] & 0x30) >> 4)); str_append(&r, &o, 1);
        if (c[2] == 64) break;
        o = (char)(((c[1] & 0x0F) << 4) | ((c[2] & 0x3C) >> 2)); str_append(&r, &o, 1);
        if (c[3] == 64) break;
        o = (char)(((c[2] & 0x03) << 6) | c[3]); str_append(&r, &o, 1);
    }
    pp_str_decref(s);
    return pp_str_val(r);
}
static pp_val n_b64_enclen(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    pp_str *s = a->n ? pp_to_str(a->v[0]) : pp_str_new(NULL, 0);
    pp_str *eol = mime_eol(a, 1);
    int64_t len = ((int64_t)s->len + 2) / 3 * 4;
    if (len) len += ((len - 1) / 76 + 1) * eol->len;
    pp_str_decref(s); pp_str_decref(eol);
    return pp_int(len);
}
static pp_val n_b64_declen(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    pp_val d = n_b64_decode(I, a, ctx, out);
    int64_t n = d.u.s->len;
    pp_release(d);
    return pp_int(n);
}
/* encode_base64url / decode_base64url: - and _ for + and /, no padding, no line breaks */
static pp_val n_b64url_encode(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    pp_list b = {0};
    pp_list_push(&b, a->n ? pp_copy(a->v[0]) : pp_undef());
    pp_list_push(&b, pp_strn("", 0));
    pp_val e = n_b64_encode(I, &b, ctx, out);
    pp_list_free(&b);
    pp_str *r = pp_str_new(e.u.s->d, e.u.s->len);
    pp_release(e);
    while (r->len && r->d[r->len - 1] == '=') r->d[--r->len] = 0;
    for (int i = 0; i < r->len; i++) r->d[i] = r->d[i] == '+' ? '-' : r->d[i] == '/' ? '_' : r->d[i];
    return pp_str_val(r);
}
static pp_val n_b64url_decode(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    pp_str *s = a->n ? pp_to_str(a->v[0]) : pp_str_new(NULL, 0);
    pp_str *t = pp_str_new(s->d, s->len);
    pp_str_decref(s);
    for (int i = 0; i < t->len; i++) t->d[i] = t->d[i] == '-' ? '+' : t->d[i] == '_' ? '/' : t->d[i];
    while (t->len % 4) str_append(&t, "=", 1);
    pp_list b = {0};
    pp_list_push(&b, pp_str_val(t));
    pp_val r = n_b64_decode(I, &b, ctx, out);
    pp_list_free(&b);
    return r;
}
static int qp_plain(unsigned char c) { return c == '\t' || (c >= ' ' && c <= '~' && c != '='); }
/* encode_qp(STR, EOL = "\n", BINMODE = 0) */
static pp_val n_qp_encode(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    pp_str *s = a->n ? pp_to_str(a->v[0]) : pp_str_new(NULL, 0);
    pp_str *eol = mime_eol(a, 1);
    int binary = a->n > 2 && pp_truth(a->v[2]);
    pp_str *r = pp_str_new(NULL, 0);
    const char *p = s->d, *end = s->d + s->len;
    int linelen = 0;
    for (;;) {
        const char *beg = p;
        while (p < end && qp_plain((unsigned char)*p)) p++;
        if (p == end || *p == '\n') while (p > beg && (p[-1] == '\t' || p[-1] == ' ')) p--;   /* white space at the end of a line is encoded */
        int n = (int)(p - beg);
        if (n) {
            if (eol->len) {
                while (n > 76 - 1 - linelen) {
                    int k = 76 - 1 - linelen;
                    str_append(&r, beg, k); beg += k; n -= k;
                    str_append(&r, "=", 1); str_append(&r, eol->d, eol->len);
                    linelen = 0;
                }
            }
            if (n) { str_append(&r, beg, n); linelen += n; }
        }
        if (p == end) break;
        if (*p == '\n' && eol->len && !binary) {
            if (linelen == 1 && r->len > eol->len + 1 && r->d[r->len - eol->len - 2] == '=') {
                r->d[r->len - eol->len - 2] = r->d[r->len - 1];      /* a soft line break that was not needed */
                r->len--; r->d[r->len] = 0;
            } else str_append(&r, eol->d, eol->len);
            p++; linelen = 0;
        } else {
            if (eol->len && linelen > 76 - 4 && !(linelen == 76 - 3 && p + 1 < end && p[1] == '\n' && !binary)) {
                str_append(&r, "=", 1); str_append(&r, eol->d, eol->len);
                linelen = 0;
            }
            char q[4]; pp_snprintf(q, sizeof q, "=%02X", (unsigned char)*p);
            str_append(&r, q, 3);
            p++; linelen += 3;
        }
    }
    if (r->len && eol->len && linelen) { str_append(&r, "=", 1); str_append(&r, eol->d, eol->len); }
    pp_str_decref(s); pp_str_decref(eol);
    return pp_str_val(r);
}
static pp_val n_qp_decode(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)I; (void)ctx; (void)out;
    pp_str *s = a->n ? pp_to_str(a->v[0]) : pp_str_new(NULL, 0);
    pp_str *r = pp_str_new(NULL, 0);
    const char *p = s->d, *end = s->d + s->len, *ws = NULL;
    while (p < end) {
        if (*p == ' ' || *p == '\t') { if (!ws) ws = p; p++; }       /* kept unless the line ends here */
        else if (*p == '\r' && p + 1 < end && p[1] == '\n') p++;
        else if (*p == '\n') { ws = NULL; str_append(&r, p++, 1); }
        else {
            if (ws) { str_append(&r, ws, (int)(p - ws)); ws = NULL; }
            if (*p == '=') {
                if (p + 2 < end && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
                    char h[3] = { p[1], p[2], 0 }, c = (char)strtol(h, NULL, 16);
                    str_append(&r, &c, 1); p += 3;
                } else {                                               /* a soft line break? */
                    const char *q = p + 1;
                    while (q < end && (*q == ' ' || *q == '\t')) q++;
                    if (q < end && *q == '\n') p = q + 1;
                    else if (q + 1 < end && *q == '\r' && q[1] == '\n') p = q + 2;
                    else str_append(&r, p++, 1);
                }
            } else str_append(&r, p++, 1);
        }
    }
    if (ws) str_append(&r, ws, (int)(p - ws));
    pp_str_decref(s);
    return pp_str_val(r);
}

/* syswrite(FH, SCALAR, LEN, OFFSET): LEN bytes of SCALAR from OFFSET (all of it without LEN);
   the number of bytes written, undef when it fails. There is no unbuffered writing here: it
   goes the way print does. */
static pp_val n_syswrite(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    if (a->n < 2) return pp_die(I, "Not enough arguments for syswrite");
    int fd;
    pp_obj *f = handle_of(I, a->v[0], &fd);
    pp_str *s = pp_to_str(a->v[1]);
    int64_t off = a->n > 3 ? pp_to_int(a->v[3]) : 0;
    if (off < 0) off += s->len;
    if (off < 0 || off > s->len) { pp_str_decref(s); return pp_die(I, "Offset outside string"); }
    int64_t len = a->n > 2 && a->v[2].t != PP_UNDEF ? pp_to_int(a->v[2]) : s->len - off;
    if (len < 0) { pp_str_decref(s); return pp_die(I, "Negative length"); }
    if (len > s->len - off) len = s->len - off;
    int ok = 1;
    if (f) { if (!f->u.file.writable || !(f->u.file.fp || f->u.file.mem)) ok = 0; else ok = f_write(f, s->d + off, (int)len); }
    else if (fd == 1 || fd == 2) pp_host_write(fd, s->d + off, (int)len);
    else ok = 0;
    pp_str_decref(s);
    if (!ok) { set_errno_var(I, "Bad file descriptor"); return pp_undef(); }
    return pp_int(len);
}

/* read(FH, $buf, LEN, OFFSET): the parser passes \$buf. Returns the number of bytes read (0 at
   the end of the file), undef for something that is no open file. getc(FH): one byte or undef. */
static pp_val n_read(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    if (a->n < 3 || a->v[1].t != PP_REF || a->v[1].u.o->kind != PP_OSCALAR) return pp_die(I, "Can't modify non-lvalue expression in read");
    int fd;
    pp_obj *f = handle_of(I, a->v[0], &fd);
    if (!f || !f->u.file.fp) return pp_undef();
    int64_t want = pp_to_int(a->v[2]), off = a->n > 3 ? pp_to_int(a->v[3]) : 0;
    if (want < 0) return pp_die(I, "Negative length");
    pp_obj *cell = a->v[1].u.o;
    pp_str *old = a->n > 3 ? pp_to_str(cell->u.scalar) : pp_str_new("", 0);
    if (off < 0) off += old->len;
    if (off < 0) { pp_str_decref(old); return pp_die(I, "Offset outside string"); }
    pp_str *s = pp_str_new(old->d, off < old->len ? (int)off : old->len);
    pp_str_decref(old);
    while (s->len < off) str_append(&s, "\0", 1);       /* an offset past the end pads with NULs */
    int64_t got = 0;
    char c;
    while (got < want && f_read1(f, &c)) { str_append(&s, &c, 1); got++; }
    pp_release(cell->u.scalar);
    cell->u.scalar = pp_str_val(s);
    if (cell->tied) pp_elemref_sync(cell, 1);        /* read(FH, $h{buf}, N): the buffer is an element */
    return pp_int(got);
}
static pp_val n_getc(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    int fd;
    pp_obj *f = a->n ? handle_of(I, a->v[0], &fd) : NULL;
    char c;
    if (!f || !f->u.file.fp || !f_read1(f, &c)) return pp_undef();
    return pp_strn(&c, 1);
}
/* pos($s) = N and $#a = N: the parser passes \$s / \@a and the new value, which is returned */
static pp_val n_pos_set(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    if (a->n < 2 || a->v[0].t != PP_REF || a->v[0].u.o->kind != PP_OSCALAR) return pp_die(I, "Can't modify non-lvalue expression");
    pp_pos_set(I, a->v[0].u.o, a->v[1]);
    return pp_copy(a->v[1]);
}
static pp_val n_lastidx_set(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    if (a->n < 2 || a->v[0].t != PP_REF || a->v[0].u.o->kind != PP_OARRAY) return pp_die(I, "Can't modify non-lvalue expression");
    pp_obj *arr = a->v[0].u.o;
    if (arr->tied) pp_tie_sync(I, arr);
    int64_t want = pp_to_int(a->v[1]) + 1;
    if (want < 0) want = 0;
    if (want > 50000000) return pp_die(I, "Out of memory during array extend");
    while (arr->u.arr.n > want) pp_release(pp_array_pop(arr));
    while (arr->u.arr.n < want) pp_array_push(arr, pp_undef());
    if (arr->tied) pp_tie_writeback(I, arr);
    return pp_copy(a->v[1]);
}

/* ---------------- version objects ----------------
   What perl itself implements in C for the `version` class: parse / new, declare / qv,
   numify, normal, stringify, comparison, is_alpha, is_qv, is_lax, is_strict, _VERSION.
   An object is a blessed hash like perl's: { version => [1, 2, 3], original => "1.2.3",
   qv => 1, alpha => 1 }. lib/version.pm only turns overloading on and has import(). */
static int ver_is(pp_val v) {
    return v.t == PP_REF && v.u.o->kind == PP_OHASH && v.u.o->class && pp_hash_find(v.u.o, "version", 7) != NULL;
}
static pp_obj *ver_parts(pp_interp *I, pp_val v) {
    pp_val *p = ver_is(v) ? pp_hash_find(v.u.o, "version", 7) : NULL;
    if (!p || p->t != PP_REF || p->u.o->kind != PP_OARRAY) { pp_die(I, "Invalid version object"); return NULL; }
    return p->u.o;
}
static pp_val ver_bad(pp_interp *I, const char *why) { return pp_die(I, "Invalid version format (%s)", why); }
static int ver_dg(char c) { return isdigit((unsigned char)c) || c == '_'; }
/* the text of a version: a v-string (v1.2.3 is chr(1) chr(2) chr(3)) is spelled out again */
static pp_str *ver_text(pp_val v) {
    pp_str *s = pp_to_str(v);
    int odd = 0, punct = 0, printable = 1;
    for (int i = 0; i < s->len; i++) {
        unsigned char c = (unsigned char)s->d[i];
        if (c == ',' || c == '.' || c == '_') punct = 1;
        if (!isalnum(c) && !isspace(c)) odd = 1;
        if (!(c >= 0x21 && c <= 0x7e) && !isspace(c)) printable = 0;
    }
    if (!s->len || punct || !odd || printable) return s;
    pp_str *r = pp_str_new("v", 1);
    char b[16];
    for (int i = 0; i < s->len; ) {                    /* (a number above 127 is there as UTF-8) */
        unsigned c = (unsigned char)s->d[i];
        int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0, first = i == 0;
        if (more && i + more < s->len + 0 && ((unsigned char)s->d[i + 1] & 0xC0) == 0x80) {
            c &= 0x3F >> more;
            for (int k = 1; k <= more; k++) c = (c << 6) | ((unsigned char)s->d[i + k] & 0x3F);
            i += more + 1;
        } else i++;
        int n = pp_snprintf(b, sizeof b, "%s%u", first ? "" : ".", c);
        str_append(&r, b, n);
    }
    pp_str_decref(s);
    return r;
}
static pp_val ver_make(pp_interp *I, const char *cls, pp_obj *parts, const char *orig, int olen, int qv, int alpha) {
    pp_obj *h = pp_obj_new(PP_OHASH);
    h->class = pp_intern(I, cls, (int)strlen(cls));
    pp_val pr = pp_ref(parts); pp_obj_decref(parts);
    pp_hash_set(h, "version", 7, pr);
    pp_hash_set(h, "original", 8, pp_strn(orig, olen));
    if (qv) pp_hash_set(h, "qv", 2, pp_int(1));
    if (alpha) pp_hash_set(h, "alpha", 5, pp_int(1));
    pp_val r = pp_ref(h); pp_obj_decref(h);
    return r;
}
/* read a version out of text; force: declare / qv (always dotted) */
static pp_val ver_scan(pp_interp *I, const char *cls, const char *value, int force) {
    const char *s = value;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '-') return ver_bad(I, "negative version number");
    int qv = force, n = 0;
    if (s[0] == 'v' && isdigit((unsigned char)s[1])) {                    /* v1, v1.2.3 */
        n = 1; while (ver_dg(s[n])) n++;
        while (s[n] == '.' && ver_dg(s[n + 1])) { n++; while (ver_dg(s[n])) n++; }
        qv = 1;
    } else {
        int i = 0, groups = 0;
        while (isdigit((unsigned char)s[i])) i++;
        int j = i;
        while (s[j] == '.' && ver_dg(s[j + 1])) { j++; while (ver_dg(s[j])) j++; groups++; }
        if (groups >= 2) { n = j; qv = 1; }                               /* 1.2.3 */
        else if (isdigit((unsigned char)s[0])) { n = i; if (s[n] == '.') { n++; while (ver_dg(s[n])) n++; } }   /* 1, 1., 1.23 */
        else if (s[0] == '.' && isdigit((unsigned char)s[1])) { n = 2; while (ver_dg(s[n])) n++; }               /* .5 */
        else return ver_bad(I, s[0] == 'v' ? "dotted-decimal versions require at least three parts" : "non-numeric data");
    }
    const char *rest = s + n;
    int dots = 0, unders = 0, first_dot = -1, last_under = -1, dot_under = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] == '.') { dots++; if (first_dot < 0) first_dot = i; if (i + 1 < n && s[i + 1] == '_') dot_under = 1; }
        if (s[i] == '_') { unders++; last_under = i; }
    }
    if (*rest == '_' && !dots) return ver_bad(I, "alpha without decimal");
    if (dot_under || (n && s[n - 1] == '.' && *rest == '_')) return ver_bad(I, "fractional part required");
    if (*rest && !strchr("; \t\n\r{}", *rest)) return ver_bad(I, "non-numeric data");
    if (unders) {
        if (unders > 1) return ver_bad(I, "multiple underscores");
        if (!dots) return ver_bad(I, "alpha without decimal");
        if (!qv) { for (int i = 0; i < first_dot; i++) if (s[i] == '_') return ver_bad(I, "underscores before decimal"); }
        if (last_under == n - 1) return ver_bad(I, "misplaced underscore");
    }
    pp_obj *parts = pp_obj_new(PP_OARRAY);
    if (qv) {
        int i = s[0] == 'v' ? 1 : 0;
        for (;;) {
            int64_t v = 0;
            while (i < n && s[i] != '.') { if (s[i] != '_') v = v * 10 + (s[i] - '0'); i++; }
            pp_array_push(parts, pp_int(v));
            if (i >= n) break;
            i++;
        }
        while (parts->u.arr.n < 3) pp_array_push(parts, pp_int(0));
    } else {
        int i = 0; int64_t v = 0;
        while (i < n && s[i] != '.') { if (s[i] != '_') v = v * 10 + (s[i] - '0'); i++; }
        pp_array_push(parts, pp_int(v));
        if (i < n) i++;
        int k = 0, part = 0;                                               /* the fraction, three digits at a time */
        for (; i < n; i++) {
            if (s[i] == '_') continue;
            part = part * 10 + (s[i] - '0');
            if (++k == 3) { pp_array_push(parts, pp_int(part)); k = 0; part = 0; }
        }
        if (k) { while (k++ < 3) part *= 10; pp_array_push(parts, pp_int(part)); }
    }
    char obuf[160];
    int ol = pp_snprintf(obuf, sizeof obuf, "%s%.*s", qv && s[0] != 'v' && dots == 1 ? "v" : "", n > 150 ? 150 : n, s);
    pp_val r = ver_make(I, cls, parts, obuf, ol, qv, unders);
    if (*rest) pp_warn(I, "Version string '%s' contains invalid data; ignoring: '%s'", value, rest);
    return r;
}
static pp_val ver_copy(pp_interp *I, const char *cls, pp_val src, int qv) {
    pp_obj *from = ver_parts(I, src);
    if (!from) return pp_undef();
    pp_obj *parts = pp_obj_new(PP_OARRAY);
    for (int i = 0; i < from->u.arr.n; i++) pp_array_push(parts, pp_copy(from->u.arr.v[i]));
    pp_val *o = pp_hash_find(src.u.o, "original", 8);
    pp_str *os = o ? pp_to_str(*o) : pp_str_new("0", 1);
    pp_val r = ver_make(I, cls, parts, os->d, os->len, qv || pp_hash_find(src.u.o, "qv", 2) != NULL, pp_hash_find(src.u.o, "alpha", 5) != NULL);
    pp_str_decref(os);
    return r;
}
/* a version from any value: an object is copied; undef is version 0 */
static pp_val ver_from(pp_interp *I, const char *cls, pp_val v, int force) {
    if (ver_is(v)) return ver_copy(I, cls, v, force);
    if (v.t == PP_UNDEF) { if (force) return ver_bad(I, "version required"); pp_obj *parts = pp_obj_new(PP_OARRAY); pp_array_push(parts, pp_int(0)); return ver_make(I, cls, parts, "0", 1, 0, 0); }
    pp_str *t = ver_text(v);
    pp_val r;
    if (!t->len) r = ver_bad(I, "version required");
    else if (!force && !strcmp(t->d, "undef")) { pp_obj *parts = pp_obj_new(PP_OARRAY); pp_array_push(parts, pp_int(0)); r = ver_make(I, cls, parts, "0", 1, 0, 0); }
    else r = ver_scan(I, cls, t->d, force);
    pp_str_decref(t);
    return r;
}
static const char *ver_class_of(pp_val v, pp_str **tmp) {
    *tmp = NULL;
    if (v.t == PP_REF && v.u.o->class) return v.u.o->class->name;
    *tmp = pp_to_str(v);
    return (*tmp)->d;
}
/* version->new(VALUE), version->parse(VALUE), $obj->new */
static pp_val n_ver_new(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    pp_str *tmp;
    const char *cls = a->n ? ver_class_of(a->v[0], &tmp) : (tmp = NULL, "version");
    pp_val r;
    if (a->n == 1 && ver_is(a->v[0])) r = ver_copy(I, cls, a->v[0], 0);
    else r = ver_from(I, cls, a->n > 1 ? a->v[a->n > 2 ? 2 : 1] : pp_undef(), a->n > 2);
    if (tmp) pp_str_decref(tmp);
    return r;
}
/* version->declare(VALUE), qv(VALUE): always read as dotted */
static pp_val n_ver_declare(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    pp_str *tmp = NULL;
    const char *cls = a->n > 1 ? ver_class_of(a->v[0], &tmp) : "version";
    pp_val r = ver_from(I, cls, a->n > 1 ? a->v[1] : a->n ? a->v[0] : pp_undef(), 1);
    if (tmp) pp_str_decref(tmp);
    return r;
}
static pp_val ver_numify(pp_interp *I, pp_val v) {
    pp_obj *p = ver_parts(I, v);
    if (!p) return pp_undef();
    pp_str *r = pp_str_new(NULL, 0);
    char b[32];
    for (int i = 0; i < p->u.arr.n; i++) { int n = pp_snprintf(b, sizeof b, i ? "%03lld" : "%lld.", (long long)pp_to_int(p->u.arr.v[i])); str_append(&r, b, n); }
    if (p->u.arr.n <= 1) str_append(&r, "000", 3);
    return pp_str_val(r);
}
static pp_val ver_normal(pp_interp *I, pp_val v) {
    pp_obj *p = ver_parts(I, v);
    if (!p) return pp_undef();
    pp_str *r = pp_str_new("v", 1);
    char b[32];
    int n = p->u.arr.n < 3 ? 3 : p->u.arr.n;
    for (int i = 0; i < n; i++) { int k = pp_snprintf(b, sizeof b, "%s%lld", i ? "." : "", i < p->u.arr.n ? (long long)pp_to_int(p->u.arr.v[i]) : 0LL); str_append(&r, b, k); }
    return pp_str_val(r);
}
static pp_val n_ver_numify(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)ctx; (void)out; return ver_numify(I, a->n ? a->v[0] : pp_undef()); }
static pp_val n_ver_normal(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)ctx; (void)out; return ver_normal(I, a->n ? a->v[0] : pp_undef()); }
static pp_val n_ver_stringify(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    pp_val v = a->n ? a->v[0] : pp_undef();
    if (!ver_parts(I, v)) return pp_undef();
    pp_val *o = pp_hash_find(v.u.o, "original", 8);
    if (o) return pp_copy(*o);
    return pp_hash_find(v.u.o, "qv", 2) ? ver_normal(I, v) : ver_numify(I, v);
}
static pp_val n_ver_is_alpha(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; return a->n && ver_is(a->v[0]) && pp_hash_find(a->v[0].u.o, "alpha", 5) ? pp_int(1) : pp_strn("", 0); }
static pp_val n_ver_is_qv(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; return a->n && ver_is(a->v[0]) && pp_hash_find(a->v[0].u.o, "qv", 2) ? pp_int(1) : pp_strn("", 0); }
/* -1 / 0 / 1; *ok = 0 after a die (the other side was no version) */
static int ver_cmp(pp_interp *I, pp_val l, pp_val r, int *ok) {
    *ok = 0;
    pp_obj *lp = ver_parts(I, l);
    if (!lp) return 0;
    pp_val made = pp_undef();
    if (!ver_is(r)) { made = ver_from(I, l.u.o->class->name, r, 0); if (I->ctl) { pp_release(made); return 0; } r = made; }
    pp_obj *rp = ver_parts(I, r);
    int c = 0;
    if (rp) {
        int n = lp->u.arr.n > rp->u.arr.n ? lp->u.arr.n : rp->u.arr.n;
        for (int i = 0; i < n && !c; i++) {
            int64_t x = i < lp->u.arr.n ? pp_to_int(lp->u.arr.v[i]) : 0, y = i < rp->u.arr.n ? pp_to_int(rp->u.arr.v[i]) : 0;
            c = x < y ? -1 : x > y ? 1 : 0;
        }
        *ok = 1;
    }
    pp_release(made);
    return c;
}
static pp_val n_ver_vcmp(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    int ok, c = ver_cmp(I, a->n ? a->v[0] : pp_undef(), a->n > 1 ? a->v[1] : pp_undef(), &ok);
    if (!ok) return pp_undef();
    return pp_int(a->n > 2 && pp_truth(a->v[2]) ? -c : c);
}
static pp_val n_ver_bool(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    pp_obj *p = ver_parts(I, a->n ? a->v[0] : pp_undef());
    if (!p) return pp_undef();
    for (int i = 0; i < p->u.arr.n; i++) if (pp_to_int(p->u.arr.v[i])) return pp_int(1);
    return pp_strn("", 0);
}
static pp_val n_ver_noop(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)a; (void)ctx; (void)out; return pp_die(I, "operation not supported with version object"); }
/* is_strict / is_lax: the two grammars of version::regex, written out */
static int ver_digits(const char *s, int i) { int k = i; while (isdigit((unsigned char)s[k])) k++; return k - i; }
static int ver_strict(const char *s) {
    int i = 0;
    if (s[0] == 'v') {                                    /* v1.2.3: at least three parts, up to 3 digits each */
        i = 1;
        int d = ver_digits(s, i);
        if (!d || (d > 1 && s[i] == '0')) return -1;
        i += d;
        int groups = 0;
        while (s[i] == '.') { d = ver_digits(s, i + 1); if (d < 1 || d > 3) return -1; i += 1 + d; groups++; }
        return groups >= 2 ? i : -1;
    }
    int d = ver_digits(s, 0);
    if (!d || (d > 1 && s[0] == '0')) return -1;
    i = d;
    if (s[i] == '.') { d = ver_digits(s, i + 1); if (!d) return -1; i += 1 + d; }
    return i;
}
static int ver_alpha_tail(const char *s, int i) { if (s[i] == '_') { int d = ver_digits(s, i + 1); if (d) return i + 1 + d; } return i; }
static int ver_lax(const char *s) {
    if (!strncmp(s, "undef", 5)) return 5;
    int best = -1, i, d;
    if (s[0] == 'v' && (d = ver_digits(s, 1))) {          /* v1, v1.2, v1.2_3 */
        i = 1 + d;
        int groups = 0;
        while (s[i] == '.' && (d = ver_digits(s, i + 1))) { i += 1 + d; groups++; }
        if (groups) i = ver_alpha_tail(s, i);
        if (i > best) best = i;
    }
    d = ver_digits(s, 0); i = d;                          /* 1.2.3, .1.2 (two or more dotted parts) */
    {
        int j = i, groups = 0, e;
        while (s[j] == '.' && (e = ver_digits(s, j + 1))) { j += 1 + e; groups++; }
        if (groups >= 2) { j = ver_alpha_tail(s, j); if (j > best) best = j; }
    }
    if (d) {                                              /* 1, 1., 1.23, 1.02_03 */
        int j = d;
        if (s[j] == '.') { int e = ver_digits(s, j + 1); j = e ? ver_alpha_tail(s, j + 1 + e) : j + 1; }
        if (j > best) best = j;
    } else if (s[0] == '.' && (d = ver_digits(s, 1))) {   /* .5 */
        int j = ver_alpha_tail(s, 1 + d);
        if (j > best) best = j;
    }
    return best;
}
static pp_val ver_check(pp_list *a, int strict) {
    if (!a->n || a->v[0].t == PP_UNDEF) return pp_strn("", 0);
    pp_str *t = pp_to_str(a->v[0]);
    const char *s = t->d;
    while (isspace((unsigned char)*s)) s++;
    int n = strict ? ver_strict(s) : ver_lax(s);
    int ok = n >= 0;
    if (ok) { s += n; while (isspace((unsigned char)*s)) s++; ok = !*s; }
    pp_str_decref(t);
    return ok ? pp_int(1) : pp_strn("", 0);
}
static pp_val n_ver_is_strict(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; return ver_check(a, 1); }
static pp_val n_ver_is_lax(pp_interp *I, pp_list *a, int ctx, pp_list *out) { (void)I; (void)ctx; (void)out; return ver_check(a, 0); }
/* version::_VERSION(CLASS or OBJECT, REQUIRED): what UNIVERSAL::VERSION does in perl */
static pp_val n_ver_VERSION(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    if (!a->n) return pp_undef();
    pp_str *tmp;
    const char *cls = ver_class_of(a->v[0], &tmp);
    char nm[160];
    int nl = pp_snprintf(nm, sizeof nm, "$%s::VERSION", cls);
    pp_sym *vs = nl < (int)sizeof nm ? pp_sym_find(I, nm, nl) : NULL;
    pp_val have = pp_undef(), res = pp_undef();
    if (vs && vs->gcell && vs->gcell->kind == PP_OSCALAR && vs->gcell->u.scalar.t != PP_UNDEF) have = ver_from(I, "version", vs->gcell->u.scalar, 0);
    if (!I->ctl && a->n > 1 && a->v[1].t != PP_UNDEF) {
        if (have.t == PP_UNDEF) pp_die(I, "%s does not define $%s::VERSION--version check failed", cls, cls);
        else {
            pp_val req = ver_from(I, "version", a->v[1], 0);
            int ok = 0, c = I->ctl ? 0 : ver_cmp(I, req, have, &ok);
            if (ok && c > 0) {
                int qv = pp_hash_find(req.u.o, "qv", 2) != NULL;
                pp_list one = {0};
                pp_list_push(&one, pp_copy(req));
                pp_val rs = qv ? ver_normal(I, req) : n_ver_stringify(I, &one, CTX_SCALAR, NULL);
                pp_release(one.v[0]); one.v[0] = pp_copy(have);
                pp_val hs = qv ? ver_normal(I, have) : n_ver_stringify(I, &one, CTX_SCALAR, NULL);
                pp_list_free(&one);
                pp_str *r1 = pp_to_str(rs), *h1 = pp_to_str(hs);
                pp_die(I, "%s version %s required--this is only version %s", cls, r1->d, h1->d);
                pp_str_decref(r1); pp_str_decref(h1); pp_release(rs); pp_release(hs);
            }
            pp_release(req);
        }
    }
    if (!I->ctl && have.t != PP_UNDEF) { pp_list one = {0}; pp_list_push(&one, pp_copy(have)); res = n_ver_stringify(I, &one, CTX_SCALAR, NULL); pp_list_free(&one); }
    pp_release(have);
    if (tmp) pp_str_decref(tmp);
    return res;
}

/* `command` / qx{command}: there is nothing to run a command with. It gives undef (an empty
   list), with $? = -1 and $! set as after a failed exec, and says so once per run. */
static pp_val n_backtick(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)a;
    if (!(I->compat_warn & 64)) {
        I->compat_warn |= 64;
        pp_warn(I, "picoperl: backticks do not run commands here, the result is empty");
    }
    pp_obj *st = pp_global(I, pp_intern(I, "$?", 2));
    pp_release(st->u.scalar); st->u.scalar = pp_int(-1);
    set_errno_var(I, "No such file or directory");
    if (ctx == CTX_LIST && out) return pp_no_value();
    return pp_undef();
}

/* pipe(READ, WRITE): two handles on one buffer in memory. What is written to WRITE can be read
   from READ; there are no processes here, so nothing ever blocks: an empty pipe reads as the
   end of the file. Each argument is a glob name ("*main::R"), or a reference to the variable
   that gets the handle (or holds a glob, as with Symbol::gensym). */
static pp_val n_pipe(pp_interp *I, pp_list *a, int ctx, pp_list *out) {
    (void)ctx; (void)out;
    if (a->n < 2) return pp_die(I, "Not enough arguments for pipe");
    pp_obj *buf = pp_obj_new(PP_OSCALAR);
    buf->u.scalar = pp_strn("", 0);
    for (int k = 0; k < 2; k++) {
        pp_obj *f = pp_obj_new(PP_OFILE);
        f->u.file.mem = buf; buf->refcnt++;
        f->u.file.fp = buf;
        f->u.file.writable = (uint8_t)k;
        pp_val v = a->v[k];
        pp_obj *cell = v.t == PP_REF && v.u.o->kind == PP_OSCALAR ? v.u.o : NULL;
        if (cell && cell->tied) pp_elemref_sync(cell, 0);
        pp_obj *glob = glob_of(I, cell ? cell->u.scalar : v);
        if (glob) { if (glob->u.glob.io) pp_obj_decref(glob->u.glob.io); glob->u.glob.io = f; }
        else if (cell) {
            pp_release(cell->u.scalar); cell->u.scalar = pp_ref(f); pp_obj_decref(f);
            if (cell->tied) pp_elemref_sync(cell, 1);
        }
        else { pp_obj_decref(f); pp_obj_decref(buf); return pp_die(I, "Bad filehandle in pipe"); }
    }
    pp_obj_decref(buf);
    return pp_int(1);
}

/* Natives that are made the first time a script names them (pp_resolve_sub asks here): a
   native registered up front costs RAM in every script, these cost none until they are used. */
static const struct { const char *name; pp_native fn; } lazy_natives[] = {
    {"MIME::Base64::encode_base64", n_b64_encode},
    {"MIME::Base64::decode_base64", n_b64_decode},
    {"MIME::Base64::encode_base64url", n_b64url_encode},
    {"MIME::Base64::decode_base64url", n_b64url_decode},
    {"MIME::Base64::encoded_base64_length", n_b64_enclen},
    {"MIME::Base64::decoded_base64_length", n_b64_declen},
    {"MIME::QuotedPrint::encode_qp", n_qp_encode},
    {"MIME::QuotedPrint::decode_qp", n_qp_decode},
    {"builtin::ceil", n_bi_ceil},
    {"builtin::trim", n_bi_trim},
    {"builtin::indexed", n_bi_indexed},
    {"builtin::true", n_bi_true},
    {"builtin::false", n_bi_false},
    {"builtin::is_weak", n_bi_false},
    {"builtin::is_tainted", n_bi_false},
    {"builtin::weaken", n_bi_nothing},
    {"builtin::unweaken", n_bi_nothing},
    {"builtin::floor", n_bi_floor},
    {"builtin::blessed", n_bi_blessed},
    {"builtin::reftype", n_bi_reftype},
    {"builtin::refaddr", n_bi_refaddr},
    {"Scalar::Util::looks_like_number", n_looks_like_number},
    {"POSIX::floor", n_bi_floor},
    {"POSIX::ceil", n_bi_ceil},
    {"POSIX::fmod", n_px_fmod},
    {"POSIX::pow", n_px_pow},
    {"POSIX::round", n_px_round},
    {"POSIX::trunc", n_px_trunc},
    {"POSIX::tan", n_px_tan},
    {"POSIX::asin", n_px_asin},
    {"POSIX::acos", n_px_acos},
    {"POSIX::atan", n_px_atan},
    {"POSIX::sinh", n_px_sinh},
    {"POSIX::cosh", n_px_cosh},
    {"POSIX::tanh", n_px_tanh},
    {"POSIX::log10", n_px_log10},
    {"POSIX::log2", n_px_log2},
    {"POSIX::fabs", n_px_fabs},
    {"POSIX::isnan", n_px_isnan},
    {"POSIX::isinf", n_px_isinf},
    {"POSIX::strftime", n_px_strftime},
    {"POSIX::access", n_px_access},
    {"POSIX::tzname", n_px_tzname},
    {"POSIX::tzset", n_bi_nothing},
    {"PicoPerl::__backtick", n_backtick},
    {"PicoPerl::__pipe", n_pipe},
    {"version::new", n_ver_new}, {"version::parse", n_ver_new}, {"version::declare", n_ver_declare}, {"version::qv", n_ver_declare},
    {"version::numify", n_ver_numify}, {"version::normal", n_ver_normal}, {"version::stringify", n_ver_stringify},
    {"version::vcmp", n_ver_vcmp}, {"version::is_alpha", n_ver_is_alpha}, {"version::is_qv", n_ver_is_qv},
    {"version::is_strict", n_ver_is_strict}, {"version::is_lax", n_ver_is_lax}, {"version::_VERSION", n_ver_VERSION},
    {"version::(\"\"", n_ver_stringify}, {"version::(0+", n_ver_numify}, {"version::(<=>", n_ver_vcmp}, {"version::(cmp", n_ver_vcmp},
    {"version::(bool", n_ver_bool}, {"version::(+", n_ver_noop}, {"version::(-", n_ver_noop}, {"version::(*", n_ver_noop}, {"version::(/", n_ver_noop},
    {"POSIX::strtol", n_px_strtol},
    {"POSIX::strtod", n_px_strtod},
    {"POSIX::setlocale", n_px_setlocale},
    {"POSIX::localeconv", n_px_localeconv},
    {"POSIX::getpid", n_px_getpid},
    {"POSIX::_exit", n_px_exit},
    {"POSIX::WEXITSTATUS", n_px_wexitstatus},
    {"POSIX::WIFEXITED", n_px_wifexited},
    {"POSIX::WTERMSIG", n_px_wtermsig},
    {"POSIX::WIFSIGNALED", n_px_wifsignaled},
};
pp_native pp_core_native(const char *name, const char **stored) {
    if (!strstr(name, "::")) return NULL;
    for (size_t i = 0; i < sizeof lazy_natives / sizeof lazy_natives[0]; i++)
        if (!strcmp(lazy_natives[i].name, name)) { *stored = lazy_natives[i].name; return lazy_natives[i].fn; }
    return NULL;
}
void pp_core_natives_register(pp_interp *I) {
    pp_register(I, "times", n_times);
    pp_register(I, "umask", n_umask);
    pp_register(I, "__read", n_read);
    pp_register(I, "syswrite", n_syswrite);
    pp_register(I, "__getc", n_getc);
    pp_register(I, "__pos_set", n_pos_set);
    pp_register(I, "__lastidx_set", n_lastidx_set);
    pp_register(I, "re::is_regexp", n_re_is_regexp);
    pp_register(I, "re::regexp_pattern", n_re_pattern);
    pp_register(I, "overload::_enable", n_ovl_enable);
    pp_register(I, "overload::StrVal", n_ovl_strval);
    pp_register(I, "__tie", n_tie);
    pp_register(I, "__tied", n_tied);
    pp_register(I, "__untie", n_untie);
    pp_register(I, "__isatty", n_isatty);
    pp_register(I, "fileno", n_fileno);
    pp_register(I, "mkdir", n_mkdir);
    pp_register(I, "rmdir", n_rmdir);
    pp_register(I, "unlink", n_unlink);
    pp_register(I, "rename", n_rename);
    pp_register(I, "__opendir", n_opendir);
    pp_register(I, "__opendir_named", n_opendir_named);
    pp_register(I, "readdir", n_readdir);
    pp_register(I, "rewinddir", n_rewinddir);
    pp_register(I, "closedir", n_closedir);
    pp_register(I, "vec", n_vec);
    pp_register(I, "__vec_set", n_vec_set);
    pp_register(I, "pack", n_pack);
    pp_register(I, "unpack", n_unpack);
    pp_register(I, "localtime", n_localtime);
    pp_register(I, "gmtime", n_gmtime);
    /* utf8:: functions that need no `use utf8`. Strings are bytes here, so the code point
       conversions are the identity and the encoding ones leave the string alone. */
    pp_register(I, "utf8::unicode_to_native", n_utf8_identity);
    pp_register(I, "utf8::native_to_unicode", n_utf8_identity);
    pp_register(I, "utf8::is_utf8", n_utf8_false);
    pp_register(I, "PerlIO::Layer::find", n_utf8_false);   /* there are no IO layers: every lookup fails */
    pp_register(I, "utf8::valid", n_utf8_true);
    pp_register(I, "utf8::upgrade", n_utf8_true);
    pp_register(I, "utf8::downgrade", n_utf8_true);
    pp_register(I, "utf8::encode", n_utf8_true);
    pp_register(I, "utf8::decode", n_utf8_true);
}
