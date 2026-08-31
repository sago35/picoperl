/*
 * picoperl - a small Perl-subset interpreter for microcontrollers.
 *
 * Portable C99. No dependencies beyond libc. The host (firmware or a
 * desktop CLI) provides the functions declared in "host hooks" below.
 */
#ifndef PICOPERL_H
#define PICOPERL_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Values                                                              */
/* ------------------------------------------------------------------ */

typedef struct pp_interp pp_interp;
typedef struct pp_val    pp_val;
typedef struct pp_obj    pp_obj;
typedef struct pp_str    pp_str;
typedef struct pp_list   pp_list;
typedef struct pp_node   pp_node;
typedef struct pp_env    pp_env;
typedef struct pp_sym    pp_sym;

enum { PP_UNDEF = 0, PP_INT, PP_NUM, PP_STR, PP_REF };

struct pp_val {
    uint8_t t;
    union {
        int64_t i;
        double  n;
        pp_str *s;
        pp_obj *o;
    } u;
};

struct pp_str {
    int  refcnt;
    int  len, cap;
    char d[1];              /* NUL terminated, cap bytes available */
};

struct pp_list {
    pp_val *v;
    int n, cap;
};

enum { PP_OSCALAR = 0, PP_OARRAY, PP_OHASH, PP_OCODE };

typedef struct pp_hent {
    char    *key;
    int      klen;          /* -1 == deleted */
    unsigned h;
    pp_val   v;
    int      next;
} pp_hent;

typedef pp_val (*pp_native)(pp_interp *I, pp_list *args, int ctx, pp_list *out);

struct pp_obj {
    int     refcnt;
    uint8_t kind;
    pp_sym *class;          /* package name when blessed, else NULL */
    union {
        pp_val scalar;
        struct { pp_val *v; int n, cap; } arr;
        struct { pp_hent *e; int n, cap, live; int *bk; int nb; } hash;
        struct {
            pp_node   *body;
            pp_env    *env;
            const char *name;
            pp_native  native;
            pp_val     constval;   /* for `use constant` */
            int        is_const;
        } code;
    } u;
};

/* symbols: interned, sigil included in name ("$x", "@x", "%x", "&x") */
struct pp_sym {
    pp_sym  *next;
    unsigned h;
    pp_obj  *sub;       /* for "&name": the sub (borrowed from globals) */
    pp_sym  *fallback;  /* for "&Pkg::name": the "&name" symbol in main (lazily set) */
    pp_obj  *gcell;     /* global variable cell (borrowed from globals env) */
    char name[1];
};

/* Lexical scope at run time: a fixed number of slots decided by the parser.
 * Cells are created on first use. Variables are addressed by (depth, slot). */
struct pp_env {
    int     refcnt;
    pp_env *parent;
    int     n, cap;         /* cap is only used for the globals list */
    uint8_t is_sub;         /* sub scope: slot 0 is @_ */
    pp_obj **cells;         /* SCALAR/ARRAY/HASH objects, NULL until used */
};

/* evaluation contexts */
enum { CTX_VOID = 0, CTX_SCALAR, CTX_LIST };

/* control flow */
enum { CTL_NONE = 0, CTL_RETURN, CTL_LAST, CTL_NEXT, CTL_DIE, CTL_EXIT };

/* ------------------------------------------------------------------ */
/* AST                                                                 */
/* ------------------------------------------------------------------ */

enum {
    N_CONST = 1,    /* val */
    N_INTERP,       /* list: parts concatenated */
    N_VAR,          /* sym (with sigil). flags&1: lexical -> op = slot, aux = depth; else global via sym->gcell */
    N_MY,           /* list: N_VAR nodes to declare (op: 'm' my, 'o' our, 'l' local) */
    N_AELEM,        /* a: array node, b: index */
    N_HELEM,        /* a: hash node, b: key */
    N_ASLICE,       /* a: array node, b: index list */
    N_HSLICE,       /* a: hash node, b: key list */
    N_LSLICE,       /* a: list, b: index list */
    N_RV2AV,        /* a: expr -> array obj */
    N_RV2HV,        /* a: expr -> hash obj */
    N_RV2SV,        /* a: expr -> scalar */
    N_RV2CV,        /* a: expr -> code (used by CALLREF) */
    N_LASTIDX,      /* a: array node */
    N_ANONARRAY,    /* list */
    N_ANONHASH,     /* list */
    N_ANONSUB,      /* a: body block */
    N_REF,          /* a: expr (\) */
    N_LIST,         /* list  (parenthesised / comma) */
    N_RANGE,        /* a .. b */
    N_ASSIGN,       /* a = b ; op: 0 or operator code */
    N_BINOP,        /* op, a, b */
    N_LOGAND, N_LOGOR, N_DEFOR,  /* a, b */
    N_NOT,          /* a */
    N_NEG,          /* a */
    N_TERNARY,      /* a ? b : c */
    N_PREINC, N_PREDEC, N_POSTINC, N_POSTDEC,   /* a */
    N_REPEAT,       /* a x b  (op: 1 if list repeat) */
    N_CALL,         /* sym (&name), list: args, op: 1 if bareword (no parens, no args) */
    N_CALLREF,      /* a: code expr, list: args */
    N_BUILTIN,      /* op: builtin id, list: args, a: block (sort/map/grep) */
    N_STRINGIFY,    /* a  (used for hash keys / concat) */
    N_BLOCK,        /* list: stmts, flags&1: has my */
    N_IF,           /* a: cond, b: then block, c: else (block or N_IF), op: 1 for unless */
    N_WHILE,        /* a: cond, b: body, op: 1 for until, flags&2: postfix (do-while) */
    N_FOR,          /* a: init, b: cond, c: step, d: body */
    N_FOREACH,      /* a: var (N_VAR or N_MY or NULL for $_), b: list, c: body */
    N_SUBDEF,       /* sym, a: body */
    N_RETURN,       /* a: expr or NULL */
    N_LAST, N_NEXT,
    N_NOP,
    N_DOBLOCK,      /* a: block */
    N_EVALBLOCK,    /* a: block */
    N_CONSTDEF,     /* a: list expr (name, value, ...) */
    N_REQUIRE,      /* a: expr */
    N_WANTARRAY,
    N_FILEHANDLE,   /* op: 1 stdout, 2 stderr */
    N_USE,          /* a: module name (const), b: package to import into, list: import list; op 1: use parent/base */
    N_METHOD,       /* a: invocant, sym: method name (plain) or b: dynamic method expr, c: current package (const), list: args */
};

/* binary operator codes */
enum {
    OP_NONE = 0,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW, OP_CONCAT,
    OP_SHL, OP_SHR, OP_BAND, OP_BOR, OP_BXOR,
    OP_NUM_EQ, OP_NUM_NE, OP_NUM_LT, OP_NUM_GT, OP_NUM_LE, OP_NUM_GE, OP_NUM_CMP,
    OP_STR_EQ, OP_STR_NE, OP_STR_LT, OP_STR_GT, OP_STR_LE, OP_STR_GE, OP_STR_CMP,
    OP_LOGAND, OP_LOGOR, OP_DEFOR, OP_REPEAT,
};

/* 32 bytes on 32-bit targets. N_CONST uses `val`; every other node type may use
 * c, d, sym and list, which overlay it. Nodes live in a per-file arena unless
 * flags & PP_NODE_HEAP (individually malloc'd, e.g. subtrees run at parse time). */
struct pp_node {
    uint8_t  type;
    uint8_t  op;
    uint8_t  flags;
    uint8_t  aux;            /* blocks / cond scopes: number of lexical slots; N_VAR: depth */
    uint16_t line;
    uint16_t n;              /* number of entries in list */
    pp_node *a, *b;
    union {
        pp_val val;
        struct { pp_node *c, *d; pp_sym *sym; pp_node **list; };
    };
};
#define PP_NODE_HEAP 0x80

/* bump allocator for the AST of one file */
typedef struct pp_arena_chunk { struct pp_arena_chunk *next; size_t used, cap; } pp_arena_chunk;
typedef struct pp_arena { pp_arena_chunk *chunks; long nnodes; } pp_arena;
pp_arena *pp_arena_new(void);
void     *pp_arena_alloc(pp_arena *a, size_t n);
void      pp_arena_free(pp_arena *a);

/* ------------------------------------------------------------------ */
/* Interpreter                                                         */
/* ------------------------------------------------------------------ */

#define PP_SYM_BUCKETS 128
#ifndef PP_DEFAULT_STACK_LIMIT
#define PP_DEFAULT_STACK_LIMIT (1024 * 1024)
#endif

struct pp_interp {
    pp_sym  *syms[PP_SYM_BUCKETS];
    pp_env  *globals;
    pp_env  *env;                /* current lexical env */
    int      ctl;
    pp_sym  *ctl_label;          /* label for last/next, or NULL */
    pp_val   err;                /* die message */
    pp_list  ret;                /* return values */
    int      call_ctx;           /* context of the currently executing sub */
    int      depth;              /* perl call depth */
    int      max_depth;
    char    *stack_base;         /* C stack address at pp_run() entry */
    size_t   stack_limit;        /* max C stack bytes the interpreter may use (0 = default) */
    int      exit_code;
    const char *file;
    int      line;
    unsigned stmt_counter;
    pp_obj  *loaded;             /* hash of required files */
    pp_obj  *g_underscore;       /* $_ cell (SCALAR obj) */
    pp_obj  *g_a, *g_b;          /* $a, $b for sort */
    pp_obj  *g_at;               /* $@ */
    pp_obj  *g_argv;             /* @ARGV */
    pp_node **subdefs; int nsubdefs;   /* named subs collected while parsing */
    pp_node **units; char **unit_files; int nunits;   /* parsed files, freed by pp_free */
    pp_arena *arena;             /* AST storage for every file loaded by this interpreter */
    pp_node **all_subdefs; int nall_subdefs;          /* every named sub node (freed by pp_free) */
    struct { char *dst; char *src; char *prefix; } *lazy; int nlazy;   /* wildcard exports (e.g. KC_*) */
    int      in_sub;
    int      warn_on;            /* `use warnings` in effect for the running statement */
    pp_obj  *lv_hold;            /* keeps a container alive while an lvalue slot is in use */
    struct { pp_obj *cell; pp_val saved; } *locals; int nlocals, caplocals;  /* `local` save stack */
    pp_obj  *unit_env_holder;
    /* symbols frequently needed */
    pp_sym  *sym_args;           /* @_ */
    pp_sym  *sym_underscore;     /* $_ */
    pp_sym  *sym_a, *sym_b;
};

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

pp_interp *pp_new(void);
void       pp_free(pp_interp *I);

/* Parse & run source. Returns 0 on success, non-zero on error
 * (parse error, uncaught die, or exit with non-zero code). */
int        pp_run(pp_interp *I, const char *filename, const char *src);

/* Register a native function callable from Perl as name(...) */
void       pp_register(pp_interp *I, const char *name, pp_native fn);
/* Define a constant sub (like `use constant NAME => value`); takes ownership of v */
void       pp_define_const(pp_interp *I, const char *name, pp_val v);
/* Resolve a sub symbol, falling back from Pkg::name to main's name (natives, main subs) */
pp_obj    *pp_resolve_sub(pp_interp *I, pp_sym *sym);
/* Ask the running script to stop (like exit(code)); safe to call from a host hook.
 * pp_run() returns code as soon as the current statement finishes. */
void       pp_request_exit(pp_interp *I, int code);
/* Free an AST (used internally; units are freed by pp_free) */
void       pp_free_ast(pp_node *n);

/* allocation statistics (live AST nodes / strings / objects), for memory tuning */
extern long pp_stat_nodes, pp_stat_strs, pp_stat_objs;
/* Value helpers for natives */
pp_val     pp_undef(void);
pp_val     pp_int(int64_t i);
pp_val     pp_num(double n);
pp_val     pp_strn(const char *s, int len);
pp_val     pp_cstr(const char *s);
pp_val     pp_copy(pp_val v);              /* incref */
void       pp_release(pp_val v);           /* decref */
int64_t    pp_to_int(pp_val v);
double     pp_to_num(pp_val v);
int        pp_truth(pp_val v);
pp_str    *pp_to_str(pp_val v);            /* new reference */
const char *pp_to_cstr(pp_val v, pp_str **tmp); /* tmp must be released if non-NULL */
void       pp_list_push(pp_list *l, pp_val v);   /* takes ownership */
void       pp_list_free(pp_list *l);
pp_val     pp_die(pp_interp *I, const char *fmt, ...);
void       pp_warn(pp_interp *I, const char *fmt, ...);   /* prints "... at FILE line N." when warnings are on */
int        pp_list_arg_int(pp_list *args, int i, int64_t dflt);

/* Lower-level access used by the pico bindings */
pp_obj    *pp_new_array(void);
void       pp_array_push(pp_obj *a, pp_val v);

/* ------------------------------------------------------------------ */
/* Host hooks: must be provided by the embedding program               */
/* ------------------------------------------------------------------ */

/* write to stdout (fd 1) or stderr (fd 2) */
void        pp_host_write(int fd, const char *buf, int len);
/* read a file for require; return malloc'd NUL-terminated string or NULL */
char       *pp_host_read_file(const char *name);
/* called periodically from the interpreter loop (e.g. tud_task) */
void        pp_host_poll(void);
/* milliseconds since start */
uint32_t    pp_host_millis(void);
/* seconds since epoch (0 if unknown) */
int64_t     pp_host_time(void);
/* sleep */
void        pp_host_sleep_ms(uint32_t ms);
/* read one line from stdin (<STDIN>); malloc'd, includes newline; NULL at EOF */
char       *pp_host_read_line(void);
/* register host-specific natives (gpio_*, hid_*, ...) */
void        pp_host_register(pp_interp *I);
/* unrecoverable error (out of memory): must not return */
void        pp_host_fatal(const char *msg);
/* lazily provided constants: return 1 and set *out when the fully qualified sub name
 * (e.g. "PicoPerl::Keycodes::KC_A") is known */
int         pp_host_const(const char *name, pp_val *out);
/* keycode table (src/pp_keycodes.c) */
void        pp_keycodes_register(pp_interp *I);
int         pp_keycodes_const(const char *name, pp_val *out);
/* note constants (src/pp_tone.c): "Tone::C5" -> MIDI note number */
int         pp_tone_const(const char *name, pp_val *out);
int         pp_keycode_value(const char *name);
const char *pp_keycode_name(int code);

#ifdef __cplusplus
}
#endif
#endif
