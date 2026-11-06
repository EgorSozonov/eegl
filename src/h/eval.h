#ifndef AVOID_DUPLICATION
#define MAX_FUNC_NAME_LEN   200
#endif

int checkIfNameReserved(S name, int is_objm_access);
Long num_divide(Long n1, Long n2, OUT Boole* failed);
Long num_modulus(Long n1, Long n2, OUT Boole* failed);
void evalInitGlobals(void);
void eval_clear(void);
void fillEvalArgFromInvo(OUT EvalCtx *evalarg, Invocation* invo, int skip);
int eval_to_bool(
	S arg,
	OUT Boole* error,
	Invocation* invo,
	Boole skip,       //only parse, don't execute
	Boole use_simple_function
);
Boole eval_expr_valid_arg(Var *tv);
int eval_expr_typval(
	Var    *expr,
	int       want_func,
	Var    *argv,
	int       argc,
	Var    *returnVar)
;
S eval_to_string_skip(
	S arg,
	Invocation* invo,
	int      skip)       //only parse, don't execute
;
void init_evalarg(EvalCtx *evalarg);
void clear_evalarg(EvalCtx* evalarg, Invocation* invo);
int skip_expr(OUT S* pp, EvalCtx* evalarg);
int skip_expr_concatenate(Byte** arg, Byte** start, Byte** end, EvalCtx* evalarg);
S typval2string(Var *tv, int join_list);
S evalToStringWithInvo(S arg, Boole join_list, Invocation* invo, Boole use_simple_function);
S eval_to_string(S arg, Boole join_list, Boole use_simple_function);
S eval_to_string_safe(S arg, Boole use_simple_function);
Long eval_to_number(S expr, int use_simple_function);
Var* eval_expr(S arg, Invocation* invo);
void * call_func_retstr(Byte* func, int argc, Var* argv);
void * call_func_retlist(Byte* func, int argc, Var* argv);
S getLval( OUT Lval* retVal, GetLval arg);
void clear_lval(OUT Lval* lp);
Boole letImpl(
	OUT Lval* lval,
	Var* returnVar,
	Boole copy,
	Unt flags, //ASSIGN_CONST, ASSIGN_NO_DECL
	NULLABLE S op
);
Boole evalLetVarSimple(S name, Var* newValue);
int tv_op(Var *tv1, Var *tv2, S op);
int op_pending(void);
void set_context_for_expression(Expand   *xp, S arg, CommIndex   id);
int pattern_match(S pat, S text, int ic);
S skipwhite_and_linebreak(S arg, EvalCtx *evalarg);
int eval0(S arg, Var* returnVar, EvalCtx* evalarg);
int may_call_simple_func(S arg, OUT Var* returnVar);
int eval0_simple_funccal(S arg, OUT Var* returnVar, EvalCtx* evalarg);
int eval1(OUT S* arg, Var* returnVar, OUT EvalCtx* evalarg);
void eval_addblob(Var *tv1, Var *tv2);
int eval_addlist(Var *tv1, Var *tv2);
int eval_leader(Byte **arg);
int handle_predefined(S s, int len, Var* returnVar);
S partial_name(PartiallyApplied* pt);
void partial_unref(PartiallyApplied *pt);
void f_test_option_not_set(Arr(Var) argvars, Var*);
S echo_string_core(
	Var* tv,
	Byte** tofree,
	Byte* numbuf,
	Unt copyID,
	int echo_style,
	int restore_copyID,
	int composite_val
);
S echo_string(Var* tv, Byte** tofree, Byte* numbuf, Unt copyID);
Pos* var2fpos(
	Var* varp,
	int dollar_lnum,   //true when $ is last line
	int* fnum,      //set to fnum for '0, 'A, etc.
	int charcol   //return character column
);
int list2fpos(
	Var* arg,
	Pos* posp,
	int* fnump,
	ColNr* curswantp,
	int charcol
);
int readEnvNameAndGetItsLen(OUT S* arg);
int get_id_len(OUT S* arg);
int get_name_len(Byte** arg, Byte** alias, int evaluate, int verbose);
Text findNameEnd(Text const arg, OUT Text* expr, Unt flags);
int handle_subscript(
	OUT S* arg,
	Var* returnVar,
	EvalCtx* evalarg,
	Boole verbose   //give error messages
);
int evCopyItem(Var* from, Var* to, int deep, int top, Unt copyID);
int mch_get_random(OUT S buf, int len);
void echo_one(Var* returnVar, int with_space, int *atstart, int *needclr);
void c_echo(Invocation* invo);
int get_echo_attr(void);
void c_execute(Invocation* invo);
S find_option_end(OUT S* arg, OUT int *scope);
void lastSetMsg(ScriptPos script_ctx);
S do_string_sub(
	S str,
	Unt len,
	S pat,
	S sub,
	Var* expr,
	Byte* flags,
	Unt* ret_len      //length of returned buffer
);
void evalvars_clear(void);
void set_internal_string_var(S name, S value);
void eval_diff(S, S, S);
void eval_patch(S, S, S);
List * eval_spell_expr(S , S expr);
int is_scoped_variable(S name);
S eval_one_expr_in_str(S p, ArrayList *gap, int evaluate);
List * heredoc_get(Invocation* invo, S cmd, int script_get);
void c_let(Invocation* invo);
S skip_var_list(S arg, OUT int* var_count, OUT int* semicolon, int silent);
void list_hashtable_vars(EeSet* ht, S prefix, int empty, int* first);
void c_unlet(Invocation* invo);
void c_lockvar(Invocation* invo);
void list_unlet_range(
	List* l,
	ListItem* li_first,
	long n1_arg,
	int has_n2,
	long n2
);
int unletVarFromHashTable(S name, S nameWithPrefix, EeSet* ht, Boole forceit);
int unletImpl(S nameWithPrefix, Boole forceit);
void item_lock(Var *tv, int deep, int lock, int check_refcount);
void del_menutrans_vars(void);
S cat_prefix_varname(int prefix, S name);
S get_user_var_name(Expand *xp, int idx);
Bag* get_globvar_dict(void);
EeSet * get_globvar_ht(void);
void reset_reg_var(void);
int eval_variable_import(S name, Var* returnVar);
DictItem * findVar(S name, Boole noAutoload);
DictItem * findVar_also_in_script(S name, OUT EeSet** htp, Boole no_autoload);
DictItem * findVar_in_ht(
	EeSet* ht,
	VarLevel level,
	Text varname,
	Boole no_autoload
);
EeSet * get_script_local_ht(void);
int lookup_scriptitem(Text name, int cmd);
EeSet* findVarHashTable(Text name, OUT S* varname);
S get_var_value(S name);
void new_script_vars(ScriptId id);
void init_var_dict(Bag* bag, DictItem* dict_var, int scope);
void unref_var_dict(Bag* dict);
void vars_clear(EeSet* ht);
void vars_clear_ext(EeSet* ht, int free_val);
void delete_var(EeSet* ht, EeSetItem *hi);
void set_var(Text name, Var* newValue, Boole copy);
int var_check_permission(DictItem* di, S name);
Boole var_check_ro(int flags, Text name, Boole use_gettext);
int var_check_lock(Unt flags, Text name, Boole use_gettext);
Boole var_check_fixed(int flags, Text name, Boole use_gettext);
Boole value_check_lock(int lock, Text name, Boole use_gettext);
Boole valid_varname(Text varname, Boole autoload);
int var_exists(S var);
int alloc_redirLvalS(void);
void clear_redirLvalS(void);
void init_redirArrayList(void);
int var_redir_start(S name, int append);
void var_redir_str(S value, int value_len);
void var_redir_stop(void);
S get_clear_redirArrayList(void);
void f_mode(Var* argvars, Var* returnVar);
void f_state(Var* argvars, Var* returnVar);
void f_gettabvar(Var* argvars, Var* returnVar);
void f_gettabwinvar(Arr(Var) argvars, Var* returnVar);
void f_getwinvar(Arr(Var) argvars, Var* returnVar);
void f_getbufvar(Arr(Var) argvars, Var* returnVar);
void f_settabvar(Var* argvars, Var*);
void f_settabwinvar(Arr(Var) argvars, Var*);
void f_setwinvar(Arr(Var) argvars, Var*);
void f_setbufvar(Var* argvars, Var*);
Callback get_callback(Var* arg);
void putCallback(OUT Var* tv, Callback* cb);
void set_callback(Callback* dest, Callback* src);
void evCopyCallback(OUT Callback* dest, Callback* src);
void evFreeCallback(Callback* callback);
S get_function_name(Expand *xp, int idx);
S get_expr_name(Expand* xp, int idx);
Unt find_internal_func(S name);
int has_internal_func(S name);
void internal_func_get_argcount(int idx, OUT int *argcount, OUT int *min_argcount);
int internal_func_is_map(int idx);
int check_internal_func(int idx, int argcount);
FnError call_internal_func(S name, int argcount, Var* argvars, Var* returnVar);
void call_internal_func_by_idx(int idx, Var* argvars, Var* returnVar);
FnError call_internal_method(
	S name,
	int argcount,
	Var* argvars,
	Var* returnVar,
	Var* basetv
);
int non_zero_arg(Var* argvars);
Book * evGetBookArg(Var* arg);
Portal* getOptionalPortal(Arr(Var) argvars, int idx);
void f_cursor(Var* argvars, Var* returnVar);
void f_deepcopy(Var* argvars, Var* returnVar);
void execute_redir_str(S value, int value_len);
void execute_cmds_from_string(S str);
S get_list_line(Unt, void* cookie, int, GetlineAlgo);
void execute_common(Arr(Var) argvars, Var* returnVar, int arg_off);
void f_exists(Var* argvars, Var* returnVar);
int dynamic_feature(S feature);
void f_hostname(Arr(Var), Var* returnVar);
void f_id(Arr(Var) argvars, Var* returnVar);
void f_len(Arr(Var) argvars, Var* returnVar);
void range_list_materialize(List *list);
long do_searchpair(
	S spat,       //start pattern
	S mpat,       //middle pattern
	S epat,       //end pattern
	Unt dir,       //BACKWARD or FORWARD
	Var* skip,       //skip expression
	Unt flags,       //SP_SETPCMARK and other SP_ values
	Pos* match_pos,
	LineNr lnum_stop,  //stop at this line if not zero
	long time_limit //stop after this many msec
);
int aborting(void);
void update_force_abort(void);
int should_abort(int retcode);
int aborted_in_try(void);
void free_global_msglist(void);
void c_eval(Invocation* invo);
void enter_cleanup(Cleanup *csp);
void leave_cleanup(Cleanup* csp);
void c_endfunction(Invocation* invo);
