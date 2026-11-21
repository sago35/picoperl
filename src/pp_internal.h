#ifndef PP_INTERNAL_H
#define PP_INTERNAL_H
#include "picoperl.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void  *pp_xmalloc(size_t n);
void  *pp_xrealloc(void *p, size_t n);

/* strings */
pp_str *pp_str_new(const char *s, int len);
pp_str *pp_str_reserve(pp_str *s, int extra);     /* returns (possibly moved) unique str */
void    pp_str_decref(pp_str *s);
pp_val  pp_str_val(pp_str *s);                    /* takes ownership */

/* objects */
pp_obj *pp_obj_new(int kind);
void    pp_obj_incref(pp_obj *o);
void    pp_obj_decref(pp_obj *o);
void    pp_env_leave(pp_env *e);                         /* a scope ends: pp_env_decref, after freeing closures that only it holds */
pp_val  pp_ref(pp_obj *o);                        /* incref, wrap */

/* arrays */
void    pp_array_reserve(pp_obj *a, int n);
void    pp_array_set(pp_obj *a, int i, pp_val v); /* takes ownership, extends */
pp_val *pp_array_slot(pp_obj *a, int i);          /* extends, vivifies */
pp_val  pp_array_get(pp_obj *a, int i);           /* new reference (undef if OOB) */
void    pp_array_clear(pp_obj *a);
pp_val  pp_array_shift(pp_obj *a);
pp_val  pp_array_pop(pp_obj *a);
void    pp_array_unshift(pp_obj *a, pp_val v);

/* hashes */
pp_val *pp_hash_find(pp_obj *h, const char *k, int klen);
pp_val *pp_hash_slot(pp_obj *h, const char *k, int klen);  /* insert if missing */
pp_val  pp_hash_get(pp_obj *h, const char *k, int klen);   /* new reference */
void    pp_hash_set(pp_obj *h, const char *k, int klen, pp_val v);  /* takes ownership */
int     pp_hash_delete(pp_obj *h, const char *k, int klen, pp_val *out);
void    pp_hash_clear(pp_obj *h);

/* tie: which object a variable (or glob) is tied to. The variable carries only the `tied`
   flag; the object lives in a table, so untied variables pay nothing for it. */
int     pp_core_fn_known(const char *name, int len);     /* a perl function name (for CORE::name) */
int     pp_core_const(const char *name, pp_val *out);
pp_native pp_core_native(const char *name, const char **stored);   /* a native made on first use (builtin::, MIME::, POSIX:: ...) */   /* "Pkg::NAME" of a constant kept in ROM (Fcntl) */
void    pp_tie_set(pp_obj *var, pp_val obj);       /* takes ownership of obj; replaces an old tie */
void pp_elemref_sync(pp_obj *cell, int store);   /* the scalar behind \$a[i] / \$h{k} <-> its element */
pp_val *pp_tie_find(pp_obj *var);                  /* borrowed, NULL when not tied */
void    pp_tie_remove(pp_obj *var);
void    pp_tie_flush(pp_interp *I);                /* run the STOREs that are still owed */
void    pp_tie_writeback(pp_interp *I, pp_obj *o);    /* a tied array / hash was rewritten: CLEAR + STORE */
void    pp_tie_sync(pp_interp *I, pp_obj *o);         /* fill a tied array / hash from its object */
pp_obj *pp_get_array_raw(pp_interp *I, pp_node *n, int vivify);   /* like pp_get_array, without filling a tied one */
pp_obj *pp_get_hash_raw(pp_interp *I, pp_node *n, int vivify);
extern int pp_tie_count;                           /* how many ties exist (0: nothing to look up) */
/* call METHOD on a tie object (or a class name) with args; must: die when there is no such method */
pp_obj *pp_find_method(pp_interp *I, const char *cls, const char *name);   /* borrowed; NULL: the class cannot do it */
int     pp_isa(pp_interp *I, const char *cls, const char *want);
pp_val  pp_tie_call(pp_interp *I, pp_val obj, const char *method, pp_list *args, int ctx, pp_list *out, int must);

/* the text after __DATA__ (or the main script's __END__) becomes the handle DATA of package pkg */
void    pp_set_data_handle(pp_interp *I, const char *pkg, const char *text, int len);

/* DESTROY. Once some class defines a DESTROY (pp_destroy_on), a blessed object whose last
   reference goes away is kept and queued instead of freed; pp_destroy_run() calls the methods
   at the next safe point (the end of a statement) and then lets the objects go. */
extern uint8_t pp_destroy_on;
extern int     pp_destroy_n;                      /* objects waiting for their DESTROY */
pp_obj *pp_destroy_next(int from);                /* take the oldest one at or after index `from` (refcnt 1), or NULL */
void    pp_destroy_run(pp_interp *I);

/* operator overloading. The conversions (pp_to_str, pp_numify, pp_truth) ask this hook about a
   blessed reference: kind 's' string, 'n' number, 'b' truth; 1 with the result in *out when
   the class has a conversion. NULL until `use overload` runs, so nothing else pays for it. */
extern int (*pp_ovl_hook)(pp_val v, int kind, pp_val *out);
void    pp_ovl_enable(pp_interp *I);
/* a unary operator or function ("neg", "abs", "sqrt" ...) on v: 1 with *out when overloaded */
int     pp_ovl_unary(pp_interp *I, pp_val v, const char *name, pp_val *out);

/* symbols / env */
pp_sym *pp_intern(pp_interp *I, const char *name, int len);
pp_env *pp_env_new(pp_env *parent, int nslots);
void    pp_env_incref(pp_env *e);
void    pp_env_decref(pp_env *e);
pp_obj *pp_env_cell(pp_env *e, int slot, char sigil);    /* create on first use; borrowed */
pp_obj *pp_env_reset(pp_env *e, int slot, char sigil);   /* fresh cell (re-executed `my`); borrowed */
pp_obj *pp_global(pp_interp *I, pp_sym *s);        /* find/create global; borrowed */
#define PP_VAR_LEX  1      /* N_VAR flag: lexical (depth in op, slot in n) */
/* $! holds an error number; reading it gives the message, or the number when the variable is
   an operand of arithmetic or a numeric comparison ($! + 0, $! == 2) */
#define PP_VAR_ERRNO  0x10 /* N_VAR flag: this is $! */
#define PP_VAR_ERRNUM 0x20 /* ... read as a number */
#define PP_STMT_WARN 0x40  /* statement flag: `use warnings` in effect */
void    pp_global_alias(pp_interp *I, pp_sym *s, pp_obj *cell);   /* make s refer to an existing cell */

/* conversions */
/* Bit operations work on unsigned 64-bit values, like perl. Integers here are signed, so a
   result keeps its exact bits as a (possibly negative) integer, except all ones: ~0, the
   largest unsigned integer, is the number 18446744073709551615 (a double, shown exactly). */
uint64_t pp_to_bits(pp_val v);
pp_val   pp_bits_val(uint64_t r);
int     pp_fmt_num(char *buf, int cap, pp_val v);  /* int/num -> string, returns len */
pp_val  pp_str_to_num(const char *s, int len);     /* returns INT or NUM */
int     pp_looks_like_number(const char *s, int len);
pp_val  pp_numify(pp_val v);                       /* INT or NUM value */
int     pp_str_cmp(pp_val a, pp_val b);
int     pp_val_is_array_ref(pp_val v);
int     pp_val_same(pp_val a, pp_val b);   /* same type and value (strings/refs by identity) */

/* parser */
pp_node *pp_parse(pp_interp *I, const char *filename, const char *src, int *err);
pp_node *pp_parse_required(pp_interp *I, const char *filename, const char *src, int *err, char *errbuf, int errcap);
/* parse the code of an eval STRING at node `site` (N_EVALSTR); NULL with the message in errbuf */
pp_node *pp_parse_eval(pp_interp *I, const char *filename, const char *src, pp_node *site, char *errbuf, int errcap, pp_arena **arena);
pp_node *pp_parse_expr_string(pp_interp *I, const char *src, int len, int line, const char *file, int *err);

/* eval */
pp_val   pp_eval(pp_interp *I, pp_node *n, int ctx, pp_list *out);
pp_val   pp_exec_unit(pp_interp *I, pp_node *unit, int ctx, pp_list *out);
pp_val   pp_call(pp_interp *I, pp_obj *code, pp_list *args, int ctx, pp_list *out);
pp_obj  *pp_get_array(pp_interp *I, pp_node *n, int vivify, pp_obj **tmp);
pp_obj  *pp_get_hash(pp_interp *I, pp_node *n, int vivify, pp_obj **tmp);
pp_val  *pp_lvalue(pp_interp *I, pp_node *n);
pp_val   pp_builtin(pp_interp *I, pp_node *n, int ctx, pp_list *out);
pp_val   pp_sprintf(pp_interp *I, pp_list *args, int start);
void     pp_warn_uninit_args(pp_interp *I, pp_node *n, pp_list *args, const char *what);
void     pp_emit_warning(pp_interp *I, const char *msg, int len);   /* honours $SIG{__WARN__} */
void     pp_define_sub(pp_interp *I, pp_sym *sym, pp_obj *code);
pp_val   pp_die_val(pp_interp *I, pp_val msg);
int      pp_builtin_id(const char *name, int len, int *kind);
void     pp_core_natives_register(pp_interp *I);   /* builtin:: functions */
void     pp_env_grow(pp_env *e, int nslots);        /* make room for at least nslots cells */
/* the object behind \*name (gsym: the interned "*name"); created on first use */
pp_obj *pp_glob_obj(pp_interp *I, pp_sym *gsym);
/* the interned "*name" for a glob written as a string: "FH", "main::FH", "*main::FH" */
pp_sym *pp_glob_named(pp_interp *I, const char *name);
void pp_note_sub_file(pp_interp *I, pp_node *body, const char *file);
/* snprintf that returns the number of characters actually stored (at most cap - 1), so the
   result can be used as a length even when the text was cut */
int      pp_snprintf(char *buf, size_t cap, const char *fmt, ...);
pp_val   pp_false(void);                            /* "" that is also 0 without a warning */
int      pp_is_false(pp_val v);
void    pp_glob_promote(pp_interp *I, pp_obj *file);     /* an open(my $fh) handle becomes a glob without a name (*$fh) */
extern pp_interp *pp_anon_I;                             /* the interpreter that has such globs (NULL: none) */
void    pp_match_restore(pp_interp *I, void *saved);     /* a sub returns: $1 ... are the caller's again */
pp_sym  *pp_sym_find(pp_interp *I, const char *name, int len);   /* NULL if not interned */
pp_obj  *pp_lookup_sub(pp_interp *I, pp_sym *sym);
/* symbolic reference: the symbol a plain string names when it is used as a reference by node n
   (N_RV2xV / N_CALLREF). NULL after die when n was parsed under strict refs (unless !strict). */
pp_sym  *pp_symref(pp_interp *I, pp_node *n, pp_val v, char sigil, int strict);

/* regular expressions (pp_match.c) */
pp_val   pp_eval_match(pp_interp *I, pp_node *n, int ctx, pp_list *out);
pp_val   pp_eval_subst(pp_interp *I, pp_node *n, int ctx, pp_list *out);
pp_val   pp_eval_trans(pp_interp *I, pp_node *n, int ctx, pp_list *out);
pp_obj  *pp_regex_of(pp_interp *I, pp_node *pat, int cflags);      /* new reference to a PP_OREGEX object; NULL after die */
pp_obj  *pp_regex_from_val(pp_interp *I, pp_val v, int cflags);    /* qr object or pattern text -> new reference */
int      pp_split_regex(pp_interp *I, pp_obj *ro, const char *s, int len, int limit, pp_list *res);   /* 0 after die */
void     pp_pos_set(pp_interp *I, pp_obj *cell, pp_val v);          /* pos($x) = v (undef clears it) */
pp_val   pp_pos_of_node(pp_interp *I, pp_node *target);             /* pos(EXPR); NULL: $_ */
pp_val   pp_pos_of(pp_interp *I, pp_obj *cell);                     /* pos() of a scalar cell */
void     pp_regex_cleanup(pp_interp *I);
pp_obj  *pp_var_cell(pp_interp *I, pp_node *n);
void     pp_warn_uninit(pp_interp *I, const char *what, const char *var);
const char *pp_var_name_of(pp_node *n);

/* builtin arg-parsing kinds */
enum { BK_LIST = 0, BK_UNARY, BK_NOARG, BK_SPECIAL };

#define PP_CHK(I) do { if ((I)->ctl) goto bail; } while (0)

#endif
