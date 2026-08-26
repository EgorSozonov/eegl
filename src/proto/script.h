void estack_init(void);
Estack * estack_push(CallFrame type, CS name, long lnum);
Estack* estack_push_ufunc(UserFunc *ufunc, long lnum);
int estack_top_is_ufunc(UserFunc *ufunc, long lnum);
Estack* estack_pop(void);
CS estack_sfile(EstackArg which UNUSED);
List * stacktrace_create(void);
void f_getstacktrace(Arr(Var) argvars UNUSED, Var* returnVar);
void c_runtime(Invocation* invo);
void set_context_in_runtime_cmd(Expand *xp, CS arg);
int find_script_by_name(Byte *name);
int get_new_scriptitem_for_fname(int* error, CS fname);
void check_script_symlink(int sid);
int doInPath(
   CS path,
   CS prefix,
   CS name,
   int    flags,
   void   (*callback)(Byte *fname, void *ck),
   void   *cookie
);
int do_in_runtimepath(
   Byte   *name,
   int      flags,
   void   (*callback)(Byte *fname, void *ck),
   void   *cookie
);
int source_runtime(Byte *name, int flags);
int source_in_path(CS path, CS name, int flags, OUT int* ret_sid);
int find_script_in_rtp(Byte *name);
void load_start_packages(void);
void c_packloadall(Invocation* invo);
void c_packadd(Invocation* invo);
void remove_duplicates(OUT ExpandMatch* matches);
int expandRuntimeDir(
   CS pat,
   Unt flags,
   Arr(CS) dirnames,
   OUT ExpandMatch* matches
);
void c_source(Invocation* invo);
void c_options(Invocation   *invo UNUSED);
LineNr * source_breakpoint(void *cookie);
int * source_dbg_tick(void *cookie);
int source_level(void *cookie);
CS source_nextline(void *cookie);
int may_load_script(int sid, int *loaded);
int scriptRunFile(CS fname, OUT int* retSid);
void c_scriptnames(Invocation* invo);
CS get_scriptname(ScriptId id);
void free_scriptnames(void);
void free_autoload_scriptnames(void);
LineNr get_sourced_lnum(LineGetter fgetline, void *cookie);
void f_getscriptinfo(Arr(Var) argvars, Var* returnVar);
CS scrGetSourceLine(
   Unt c UNUSED,
   void* cookie,
   int indent UNUSED,
   GetlineAlgo options
);
int sourcing_a_script(Invocation* invo);
CS get_autoload_prefix(ScriptItem *si);
CS autoload_name(Byte *name);
int scriautoload(CS name, int reload);
TypeSpec * alloc_type(TypeSpec *type);
void free_type(TypeSpec *type);
CS toNameEnd(Byte *arg, int use_namespace);
Byte * to_name_const_end(Byte *arg);
int assignment_len(Byte *p, int *heredoc);
void import_check_sourced_sid(int *sid);
Svar * find_typval_in_script(Var *dest, ScriptId sid, int must_find);
int has_watchexpr(void);
void do_debug(Byte *comm);
void c_debug(Invocation* invo);
void dbg_check_breakpoint(Invocation* invo);
int dbg_check_skipped(Invocation* invo);
void c_breakadd(Invocation* invo);
void c_debuggreedy(Invocation* invo);
int debug_has_expr_breakpoint(void);
void c_breakdel(Invocation* invo);
void c_breaklist(Invocation* invo UNUSED);
LineNr dbg_find_breakpoint(
   int      file,       // true for a file, false for a function
   Byte   *fname,       // file or function name
   LineNr   after       // after this line number
);
void dbg_breakpoint(Byte *name, LineNr lnum);
Boole scrIsCommlineFuzzyCompletable(CS fuzzystr);
void expandEscape(
   OUT Expand* xp,
   CS str,
   int options,
   OUT ExpandMatch* files
);
void cmdline_pum_display(void);
int cmdline_pum_active(void);
void cmdline_pum_remove(CommlineInfo *cclp UNUSED, int defer_redraw);
void cmdline_pum_cleanup(CommlineInfo *cclp);
int cmdline_compl_startcol(void);
CS cmdline_compl_pattern(void);
int cmdline_compl_is_fuzzy(void);
CS expandWildcard(
   OUT Expand* xp,
   CS str,
   CS orig,       // allocated copy of original of expanded string
   Unt      options,
   int      mode
);
void expandInit(OUT Expand* xp);
void scrExpandCleanup(OUT Expand* xp);
void clear_commlineSaved(void);
int showmatches(Expand *xp, int wildmenu, int noselect);
CS addstar(Text fname, Unt context);
void set_expand_context(Expand *xp);
void setCompletionContextForCommand(
   OUT Expand* xp,
   Text searchPattern,
   int col,        // position of cursor
   Boole use_ccline // use ccline for info
);
int expandCommline(
   Expand* xp,
   CS str,      // start of command line
   int col,      // position of cursor
   OUT ExpandMatch* matches
);
int expandFromContext(
   Expand   *xp,
   CS pat,
   Unt options, // WILD_ flags
   OUT ExpandMatch* matches
);
int expandGeneric(
   CS pat,
   Expand* xp,
   RegMatch* regmatch,
   CS (*fn)(Expand *, int), // return a string from the list
   int      escaped,
   OUT ExpandMatch* matches
);
int wildmenu_process_key(CommlineInfo *cclp, Unt key, Expand *xp);
void wildmenu_cleanup(CommlineInfo *cclp UNUSED);
void f_getcompletion(Arr(Var) argvars, Var* returnVar);
void f_getcompletiontype(Arr(Var) argvars, Var* returnVar);
void f_cmdcomplete_info(Arr(Var) argvars UNUSED, Var* returnVar);
int getHistLen(void);
Arr(HistoryEntry) get_histentry(int hist_type);
void set_histentry(int hist_type, HistoryEntry* entry);
int * get_hisidx(int hist_type);
int * get_hisnum(int hist_type);
int hist_char2type(int c);
CS get_history_arg(Expand *xp UNUSED, int idx);
void init_history(void);
void clear_hist_entry(HistoryEntry *hisptr);
int in_history(
    int       type,
    Byte  *str,
    int       move_to_front,   // Move the entry to the front if it exists
    int       sep,
    int       writing)      // ignore entries read from eeglinfo
;
void scrAddToHistory(
   int histype,
   Text newEntry,
   int in_map,      // consider maptick when inside a mapping
   int sep      // separator character used (search hist)
);
void f_histadd(Arr(Var) argvars UNUSED, Var* returnVar);
void f_histdel(Arr(Var) argvars UNUSED, Var* returnVar UNUSED);
void f_histget(Arr(Var) argvars UNUSED, Var* returnVar);
void f_histnr(Arr(Var) argvars UNUSED, Var* returnVar);
void remove_key_from_history(void);
void c_history(Invocation* invo);
int parse_pattern_and_range(
   OUT Pos* incsearch_start,
   OUT Unt* searchDelim,
   OUT int* skiplen,
   OUT int* patlen
);
void cmdline_init(void);
CS getCommline(
   Unt firstc,
   long count,   // only used for incremental search
   int indent,   // indent for inside conditionals
   GetlineAlgo do_concat UNUSED
);
Arr(Byte) getcmdline_prompt(
   Unt      firstc,
   CS prompt,   // command line prompt
   char      deco,      // decorations for prompt
   int      context,   // type of expansion
   CS completionFn)   // user-defined expansion argument
;
int check_opt_wim(void);
int text_locked(void);
void text_locked_msg(void);
CS get_text_locked_msg(void);
int text_or_buf_locked(void);
int curBookLocked(void);
int allbuf_locked(void);
CS scrGetTypedCommand(
   Unt  c,      // normally ':', NUL for ":append"
   void* cookie UNUSED,
   int indent,      // indent for inside conditionals
   GetlineAlgo options
);
int cmdline_overstrike(void);
int cmdline_at_end(void);
ColNr cmdline_getvcol_cursor(void);
int reallocateCommBuf(int len);
void free_arshape_buf(void);
void putcmdline(int c, int shift);
void unputcmdline(void);
int put_on_cmdline(Byte *str, int len, int redraw);
void cmdline_paste_str(CS s, int literally);
void redrawCommline(void);
void redrawCommlineEx(int do_compute_cmdrow);
void redrawcmd(void);
void compute_cmdrow(void);
void cursorcmd(void);
void gotoCommline(int clr);
CS copyStr_fnameescape(CS fname, Unt what);
void escape_fname(Byte **pp);
void tilde_replace(CS orig_pat, ExpandMatch* files);
CommlineInfo * getCommlineInfo(void);
void f_getcmdcomplpat(Arr(Var) argvars UNUSED, Var* returnVar);
void f_getcmdcompltype(Arr(Var) argvars UNUSED, Var* returnVar);
void f_getCommline(Arr(Var) argvars UNUSED, Var* returnVar);
void f_getcmdpos(Arr(Var) argvars UNUSED, Var* returnVar);
void f_getcmdprompt(Arr(Var) argvars UNUSED, Var* returnVar);
void f_getcmdscreenpos(Arr(Var) argvars UNUSED, Var* returnVar);
void f_getcmdtype(Arr(Var) argvars UNUSED, Var* returnVar);
void f_setcmdline(Arr(Var) argvars, Var* returnVar);
void f_setcmdpos(Arr(Var) argvars, Var* returnVar);
int get_cmdline_firstc(void);
int get_list_range(Byte **str, int *num1, int *num2);
int inCommPort(void);
CS script_get(Invocation* invo, Byte *comm UNUSED);
void get_user_input(
    Var   *argvars,
    Var   *returnVar,
    int      inputdialog,
    int      secret
);
void f_wildtrigger(Arr(Var) argvars UNUSED, Var* returnVar UNUSED);
CS find_ucmd(
   Invocation   *invo,
   Byte   *p,    // end of the command (possibly including count)
   int      *full,    // set to true for a full match
   Expand   *xp,    // used for completion, NULL otherwise
   OUT Unt* context // completion flags or NULL
);
CS set_context_in_user_cmd(Expand *xp, CS arg_in);
CS set_context_in_user_cmdarg(
   CS comm UNUSED,
   CS arg,
   long argFlags,
   Unt context,
   Expand* xp,
   Boole forceit
);
CS expand_user_command_name(int idx);
CS get_user_commands(Expand* xp UNUSED, int idx);
CS get_user_command_name(int idx, int id);
CS get_user_cmd_addr_type(Expand *xp UNUSED, int idx);
CS get_user_cmd_flags(Expand *xp UNUSED, int idx);
CS get_user_cmd_nargs(Expand *xp UNUSED, int idx);
CS get_user_cmd_complete(Expand *xp UNUSED, int idx);
CS cmdcomplete_type_to_str(int expand, CS compl_arg);
int cmdcomplete_str_to_type(Byte *complete_str);
CS uc_fun_cmd(void);
int parse_compl_arg(
   CS value,
   int vallen,
   int* context,
   long* argFlags,
   OUT CS* compl_arg
);
void c_command(Invocation* invo);
void c_comclear(Invocation* invo UNUSED);
void uc_clear(ArrayList *gap);
void c_delcommand(Invocation* invo);
Unt add_win_cmd_modifiers(CS builder, CommandModifier* cmod, int* multi_mods);
void do_ucmd(Invocation* invo);
void func_init(void);
EeSet * func_tbl_get(void);
CS make_ufunc_name_readable(Byte *name, Byte* builder, Unt bufsize);
Text get_lambda_name(void);
Byte * register_cfunc(cfunc_T cb, cfunc_free_T cb_free, void *state);
int get_lambda_tv(
   Byte       **arg,
   Var    *returnVar,
   EvalCtx   *evalarg
);
Text deref_func_name(
   Text name,
   OUT PartiallyApplied** partialp,
   TypeSpec** type,
   Boole no_autoload,
   OUT Boole* found_var)
;
void emsg_funcname(CS ermsg, Byte *name);
int get_func_arguments(
   Byte** arg,
   OUT EvalCtx* evalarg,
   int partial_argc,
   Var* argvars,
   int* argcount
);
int get_func_tv(
   CS name,      // name of the function
   int len,      // length of "name" or -1 to use strlen()
   Var* returnVar,
   Byte   **arg,      // argument, pointing to the '('
   EvalCtx* evalarg,   // for line continuation
   FnExe* funcexe)   // various values
;
Byte * fname_trans_sid(
   Byte       *name,
   Byte       *fname_buf,
   Byte       **tofree,
   FnError *error)
;
void func_name_with_sid(CS name, int sid, CS builder);
UserFunc * find_func_even_dead(CS name, int flags);
UserFunc * find_func(CS name, Boole is_global);
int func_is_global(UserFunc *ufunc);
int func_requires_g_prefix(UserFunc *ufunc);
int func_name_refcount(Byte *name);
void func_clear_free(UserFunc *fp, int force);
int funcdepth_increment(void);
void funcdepth_decrement(void);
int funcdepth_get(void);
void funcdepth_restore(int depth);
FnCall * create_funccal(UserFunc *fp, Var* returnVar);
void remove_funccal(void);
FnError check_user_func_argcount(UserFunc *fp, int argcount);
FnError call_user_func_check(
   UserFunc       *fp,
   int       argcount,
   Var    *argvars,
   Var    *returnVar,
   FnExe   *funcexe,
   Bag       *selfdict
);
void save_funccal(FnCallEntry *entry);
void restore_funccal(void);
FnCall * get_current_funccal(void);
int at_script_level(void);
void delete_scrifntions(int sid);
void free_all_functions(void);
int func_call(
   Byte   *name,
   Var   *args,
   PartiallyApplied   *partial,
   Bag   *selfdict,
   Var   *returnVar)
;
int get_callback_depth(void);
int call_callback(
   Callback   *callback,
   int      len,      // length of "name" or -1 to use strlen()
   Var   *returnVar,      // return value goes here
   int      argcount,   // number of "argvars"
   Var   *argvars)   // vars for arguments, must have "argcount" PLUS ONE elements!
;
Long call_callback_retnr(
   Callback   *callback,
   int      argcount,   // number of "argvars"
   Var   *argvars)   // vars for arguments, must have "argcount" PLUS ONE elements!
;
void user_func_error(FnError error, Byte *name, int found_var);
int call_func(
   Arr(Byte) funcname,   // name of the function
   int      len,      // length of "name" or -1 to use strlen()
   Var   *returnVar,      // return value goes here
   int      argcount_in,   // number of "argvars"
   Var   *argvars_in,   // vars for arguments, must have "argcount" PLUS ONE elements!
   FnExe   *funcexe)   // more arguments
;
int call_simple_func(
   CS funcname,   // name of the function
   Unt len,      // length of "name"
   OUT Var* returnVar      // return value goes here
);
CS printable_func_name(UserFunc *fp);
CS trans_function_name(
   OUT CS* name,
   OUT Boole* is_global,
   Boole skip,      // only find the end, don't evaluate
   Unt flags
);
CS get_scriptlocal_funcname(CS funcname);
CS alloc_printable_func_name(Byte *fname);
CS save_function_name(
   OUT CS* name,
   OUT Boole* is_global,
   Boole skip,
   Unt flags,
   OUT FuncDict* fudi
);
void list_functions(RegMatch *regmatch);
int get_func_arity(CS name, int *required, int *optional, int *varargs);
int scriptCheckScriptPrefix(CS p);
int translated_function_exists(CS name, Boole is_global);
int has_varargs(UserFunc *ufunc);
int function_exists(CS name, int no_deref);
CS get_user_func_name(Expand *xp, int idx);
void c_delfunction(Invocation* invo);
void func_unref(CS name);
void func_ptr_unref(UserFunc* fp);
void func_ref(CS name);
void func_ptr_ref(UserFunc *fp);
void c_return(Invocation* invo);
int can_add_defer(void);
int add_defer(CS name, int argcount_arg, Arr(Var) argvars);
void invoke_all_defer(void);
void c_call(Invocation* invo);
void discard_pending_return(void *returnVar);
CS get_return_cmd(void* returnVar);
int func_has_ended(void* cookie);
int func_has_abort(void* cookie);
Bag * make_partial(Bag* selfdict_in, Var* returnVar);
CS func_name(void* cookie);
LineNr * func_breakpoint(void *cookie);
int * func_dbg_tick(void *cookie);
int current_func_returned(void);
int free_unref_funccal(int copyID, int testing);
EeSet * get_funccal_local_ht(void);
DictItem * get_funccal_local_var(void);
EeSet * get_funccal_args_ht(void);
DictItem * get_funccal_args_var(void);
void list_func_vars(int *first);
Bag * get_current_funccal_dict(EeSet *ht);
EeSetItem* find_hi_in_scoped_ht(CS name, EeSet** pht);
DictItem * findVar_in_scoped_ht(Text name, Boole no_autoload);
int set_ref_in_previous_funccal(int copyID);
int set_ref_in_call_stack(int copyID);
int set_ref_in_functions(int copyID);
int set_ref_in_func_args(int copyID);
int set_ref_in_func(CS name, UserFunc* fp_in, int copyID);
UserFunc * define_function(Invocation* invo, ArrayList* lines_to_free);
void c_function(Invocation* invo);
int var_wrong_func_name(
   Text name,    // points to start of variable name
   int    new_var)  // true when creating the variable
;
declStruct(AutoComm);
declStruct(AutoPat);
void scrRemoveAutocommsFromBook(Book* book);
Boole auGroupExists(CS name);
void do_augroup(CS arg, Boole del_group);
void autocmd_init(void);
void free_all_autocmds(void);
int is_autoCommPort(Portal *port);
Boole event_ignored(AutoEvent event, NULLABLE CS evIgn);
int check_ei(CS evIgn);
CS au_event_disable(CS what);
void au_event_restore(CS old_ei);
void do_autocmd(CS arg_in, int forceit);
int autoEventImpl(AutoEvent event, CS pat, AutoCommCreation creation);
int do_doautocmd(
   CS arg_start,
   Boole do_msg,       // give message for no matching autocmds?
   OUT Boole* didSomething
);
void c_doautoall(Invocation* invo);
void auCommPrepareBook(
   AutocommSave* aco,      // structure to save values in
   Book* book      // new curBook
);
void auCommRestoreBook(AutocommSave* aco) ;
int applyAutocomms(
   AutoEvent   event,
   CS fname,       // NULL or empty means use actual file name
   CS fname_io,  // fname to use for <afile> on cmdline
   Boole force,       // when true, ignore autocmd_busy
   Book* book       // buffer for <abuf>
);
int auCommApplyWithInvo(
   AutoEvent event,
   CS fname,
   CS fname_io,
   Boole force,
   Book* book,
   Invocation* invo
);
int applyAutocommsRetval(
   AutoEvent   event,
   CS fname,      // NULL or empty means use actual file name
   CS fname_io,   // fname to use for <afile> on cmdline
   Boole force,     // when true, ignore autocmd_busy
   Book* book,      // book for <abuf>
   int* retval    // pointer to caller's retval
);
int trigger_cursorhold(void);
int has_winresized(void);
int has_winscrolled(void);
int has_cmdundefined(void);
int has_textyankpost(void);
int has_completechanged(void);
int has_modechanged(void);
void block_autocmds(void);
void unblock_autocmds(void);
int areAutocommsBlocked(void);
CS getnextac(Unt c UNUSED, void* cookie, int indent UNUSED, GetlineAlgo options UNUSED);
int has_autocmd(AutoEvent event, CS sfname, Book* book);
CS get_event_name(Expand* xp UNUSED, int idx);
CS get_event_name_no_group(Expand* xp UNUSED, int idx, int win);
int has_tabclosedpre(void);
int autocmd_supported(CS name);
int au_exists(CS arg);
void f_autocmd_add(Arr(Var) argvars, Var* returnVar);
void f_autocmd_delete(Arr(Var) argvars, Var* returnVar);
void f_autocmd_get(Arr(Var) argvars, Var* returnVar);
