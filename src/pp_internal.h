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

/* symbols / env */
pp_sym *pp_intern(pp_interp *I, const char *name, int len);
pp_env *pp_env_new(pp_env *parent, int nslots);
void    pp_env_incref(pp_env *e);
void    pp_env_decref(pp_env *e);
pp_obj *pp_env_cell(pp_env *e, int slot, char sigil);    /* create on first use; borrowed */
pp_obj *pp_env_reset(pp_env *e, int slot, char sigil);   /* fresh cell (re-executed `my`); borrowed */
pp_obj *pp_global(pp_interp *I, pp_sym *s);        /* find/create global; borrowed */
#define PP_VAR_LEX  1      /* N_VAR flag: lexical (depth in op, slot in n) */
#define PP_STMT_WARN 0x40  /* statement flag: `use warnings` in effect */
void    pp_global_alias(pp_interp *I, pp_sym *s, pp_obj *cell);   /* make s refer to an existing cell */

/* conversions */
int     pp_fmt_num(char *buf, int cap, pp_val v);  /* int/num -> string, returns len */
pp_val  pp_str_to_num(const char *s, int len);     /* returns INT or NUM */
int     pp_looks_like_number(const char *s, int len);
pp_val  pp_numify(pp_val v);                       /* INT or NUM value */
int     pp_str_cmp(pp_val a, pp_val b);
int     pp_val_is_array_ref(pp_val v);

/* parser */
pp_node *pp_parse(pp_interp *I, const char *filename, const char *src, int *err);
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
void     pp_define_sub(pp_interp *I, pp_sym *sym, pp_obj *code);
pp_val   pp_die_val(pp_interp *I, pp_val msg);
int      pp_builtin_id(const char *name, int len, int *kind);
pp_obj  *pp_lookup_sub(pp_interp *I, pp_sym *sym);

/* regular expressions (pp_match.c) */
pp_val   pp_eval_match(pp_interp *I, pp_node *n, int ctx, pp_list *out);
pp_val   pp_eval_subst(pp_interp *I, pp_node *n, int ctx, pp_list *out);
pp_val   pp_eval_trans(pp_interp *I, pp_node *n, int ctx, pp_list *out);
pp_obj  *pp_regex_of(pp_interp *I, pp_node *pat, int cflags);      /* new reference to a PP_OREGEX object; NULL after die */
pp_obj  *pp_regex_from_val(pp_interp *I, pp_val v, int cflags);    /* qr object or pattern text -> new reference */
int      pp_split_regex(pp_interp *I, pp_obj *ro, const char *s, int len, int limit, pp_list *res);   /* 0 after die */
pp_val   pp_pos_of(pp_interp *I, pp_obj *cell);                     /* pos() of a scalar cell */
void     pp_regex_cleanup(pp_interp *I);
pp_obj  *pp_var_cell(pp_interp *I, pp_node *n);
void     pp_warn_uninit(pp_interp *I, const char *what, const char *var);
const char *pp_var_name_of(pp_node *n);

/* builtin arg-parsing kinds */
enum { BK_LIST = 0, BK_UNARY, BK_NOARG, BK_SPECIAL };

#define PP_CHK(I) do { if ((I)->ctl) goto bail; } while (0)

#endif
