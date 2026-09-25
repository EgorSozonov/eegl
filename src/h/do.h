void do_ascii(Invocation*);
void c_align(Invocation* invo);
void c_sort(Invocation* invo);
void c_uniq(Invocation* invo);
int do_move(LineNr line1, LineNr line2, LineNr dest);
void free_prev_shellcmd(void);
void do_bang(
   int addr_count,
   Invocation* invo,
   Boole forceit,
   Boole do_in,
   Boole do_out
);
void do_shell(CS cmd, Unt flags);
int prompt_for_number(int *mouse_used);
CS make_filter_cmd(CS cmd, NULLABLE CS inputFName, NULLABLE CS outputFName);
void do_fixdel(Invocation*);
void c_file(Invocation* invo);
void c_update(Invocation* invo);
void c_write(Invocation* invo);
int do_write(Invocation* invo);
void c_wnext(Invocation* invo);
void do_wqall(Invocation* invo);
int getfile(
   int fnum,
   CS ffname_arg,
   CS sfname_arg,
   int setpm,
   LineNr lnum,
   Boole forceit
);
void c_append(Invocation* invo);
void c_change(Invocation* invo);
void c_z(Invocation* invo);
CS skip_substitute(CS start, int delimiter);
void c_substitute(Invocation* invo);
int do_sub_msg(int       count_only);
CS get_old_sub(void);
void set_old_sub(CS val);
void free_old_sub(void);
void c_global(Invocation* invo);
void global_exe(CS cmd);
int prepare_tagpreview(
   int      undo_sync,       // sync undo when leaving the portal
   int      use_previewpopup,   // use popup if 'previewpopup' set
   UsePopup   use_popup       // use other popup portal
);
void c_smile(Invocation*);
void c_drop(Invocation* invo);
CS skipEeglGrepPat(CS p, Byte **s, Unt *flags);
void c_oldfiles(Invocation* invo);
void c_listDo(Invocation* invo);
void c_compiler(Invocation* invo);
void c_checktime(Invocation* invo);
int autowrite(Book *book, int forceit);
void doFlushAllBooks(void);
int check_changed(Book *book, int flags);
void dialog_changed(Book* book, int checkall);
int check_changed_any(Boole checkOnlyHidden, Boole unload);
int check_fname(void);
int bookWrite_all(Book* book, Boole forceit);
int executeCommLine(CS cmd);
void handle_did_throw(void);
CS getline_peek(
   LineGetter fgetline,
   void* cookie      // argument for fgetline()
);
int doCommand(
   CS commline,
   LineGetter fgetline,
   void* cookie,      // argument for fgetline()
   Unt flags
);
CS ex_errmsg(CS msg, CS arg);
CS ex_range_without_command(Invocation* invo);
int checkforcmd(
   OUT CS* pp,      // start of command
   CS cmd,      // name of command
   int      len
);
int checkforcmd_noparen(
    OUT CS* pp,      // start of command
    CS cmd,      // name of command
    int len      // required length
);
int parse_command_modifiers(
   Invocation* invo,
   OUT CS* errorMsg,
   CommandModifier* cmod,
   int skip_only
);
void applyCommModifiers(CommandModifier* cmod);
void undoCommModifier(CommandModifier *cmod);
int parse_cmd_address(Invocation* invo, CS* errorMsg, int silent);
CS skip_option_env_lead(CS start);
int number_method(CS cmd);
CS findCommand(Invocation* invo, int* full, int (*lookup)(CS, Unt, int cmd));
int modifier_len(CS cmd);
int cmd_exists(CS name);
void f_fullcommand(Var *argvars, Var *returnVar);
CommIndex commandGetInd(CS cmd, int len);
long commandGetFlags(CommIndex idx);
CS skip_range(
   CS cmd_start,
   int skip_star,   // skip "*" used for Visual range
   Unt* ctx)      // pointer to context or NULL
;
LineNr doGetCommandAddress(
   Invocation* invo,
   OUT CS* ptr,
   CommandAddress   addressKind,
   int skip,      // only skip the address, don't use it
   int silent,      // no errors or side effects
   int to_other_file,  // flag: may jump to other file
   int address_count // 1 for first address, >1 after comma
);
void c_ni(Invocation* invo);
int expand_filename(Invocation* invo, OUT CS* commline, OUT CS* errorMsg);
void separateNextCommand(Invocation* invo, int keep_backslash);
CS skip_cmd_arg(CS p, int rembs);
int get_bad_opt(CS p, Invocation* invo);
int expand_argopt(
   CS pat,
   Expand* xp,
   RegMatch* rmp,
   OUT ExpandMatch* matches
);
void c_autocmd(Invocation* invo);
void c_doautocmd(Invocation* invo);
void c_bunload(Invocation* invo);
void c_book(Invocation* invo);
void c_bmodified(Invocation* invo);
void c_bnext(Invocation* invo);
void c_bprevious(Invocation* invo);
void c_brewind(Invocation* invo);
void c_blast(Invocation* invo);
CS find_nextcmd(CS p);
CS get_command_name(Expand *, int idx);
void c_hilite(Invocation* invo);
void not_exiting(void);
void c_quit(Invocation* invo);
void c_cquit(Invocation* invo);
int before_quit_all(Invocation* invo);
void c_quit_all(Invocation* invo);
void c_close(Invocation* invo);
void c_pclose(Invocation*);
void c_tabclose(Invocation* invo);
void c_tabonly(Invocation* invo);
void tabClose();
void tabCloseOther(Tab *t);
void c_only(Invocation* invo);
void c_hide(Invocation* invo);
void c_exit(Invocation* invo);
void c_print(Invocation* invo);
void c_goto(Invocation* invo);
void c_shell(Invocation*);
void c_preserve(Invocation*);
void c_recover(Invocation* invo);
void c_wrongmodifier(Invocation* invo);
int expand_findfunc(CS pat, OUT ExpandMatch* matches);
CS setFindFn(OptionChange* cha);
void doFreeFindFnOption(void);
int set_ref_in_findfunc(int copyID);
void c_splitview(Invocation* invo);
void tabNew(void);
void c_tabnext(Invocation* invo);
void c_tabmove(Invocation* invo);
void c_tabs(Invocation*);
void c_mode(Invocation* invo);
void c_resize(Invocation* invo);
void c_find(Invocation* invo);
void c_open(Invocation* invo);
void c_edit(Invocation* invo);
void do_exedit(Invocation* invo, Portal* old_curPor);
void c_syncbind(Invocation*);
void c_read(Invocation* invo);
void free_cd_dir(void);
void post_chdir(CdScopeKind scope);
void trigger_DirChangedPre(CS acmd_fname, CS new_dir);
int changedir_func(CS new_dir, CdScopeKind scope);
void c_cd(Invocation* invo);
void c_pwd(Invocation*);
void c_equal(Invocation* invo);
void c_sleep(Invocation* invo);
void do_sleep(long msec, int hide_cursor);
void c_wincmd(Invocation* invo);
void c_portPos(Invocation* invo);
void c_operators(Invocation* invo);
void c_put(Invocation* invo);
void c_iput(Invocation* invo);
void c_copymove(Invocation* invo);
void c_join(Invocation* invo);
void c_at(Invocation* invo);
void c_bang(Invocation* invo);
void c_undo(Invocation* invo);
void c_wundo(Invocation* invo);
void c_rundo(Invocation* invo);
void c_redo(Invocation*);
void c_later(Invocation* invo);
void c_redir(Invocation* invo);
void c_redraw(Invocation* invo);
void redraw_cmd(int clear);
void c_redrawstatus(Invocation* invo);
void c_redrawtabpanel(Invocation*);
int eeMkdir_emsg(CS name, int prot);
FILE * doOpenCommandsFile(CS fname, int forceit, CS mode);
void c_mark(Invocation* invo);
void update_topline_cursor(void);
void c_normal(Invocation* invo);
void c_startinsert(Invocation* invo);
void c_stopinsert(Invocation*);
void exec_normal_cmd(CS cmd, int remap, int silent);
void exec_normal(int was_typed, int use_vpeekc, int may_use_terminal_loop);
void c_checkpath(Invocation* invo);
void c_psearch(Invocation* invo);
void c_findpat(Invocation* invo);
void c_ptag(Invocation* invo);
void c_pedit(Invocation* invo);
void c_pbuffer(Invocation* invo);
void c_stag(Invocation* invo);
void c_tag(Invocation* invo);
int find_commline_var(CS src, Unt *usedlen);
CS evalVars(
   OUT LineNr* lnump,      // line number for :e command, or NULL
   OUT CS* errorMsg,   // pointer to error message
   CS src,      // pointer into commandline
   CS srcstart,   // beginning of valid memory for src
   Unt* usedlen,   // characters after src that are used
   int* escaped,   // return value has escaped white space (can be NULL)
   int empty_is_error   // empty result is considered an error
);
CS expand_sfile(CS arg);
void dialog_msg(CS buff, CS format, CS fname);
void c_filetype(Invocation* invo);
void setHlsearch(Boole flag);
void c_nohlsearch(Invocation*);
void c_fold(Invocation* invo);
void c_foldopen(Invocation* invo);
void c_folddo(Invocation* invo);
int get_pressedreturn(void);
void set_pressedreturn(int val);
int commandFlagNoSpacesInExtra();
int commandFlagExpandWildcards();
int ask_yesno(CS str, int direct);
CS doExpandEnvInMultiplePaths(CS src);
CS doExpandEnvInFilePaths(CS src, Boole singleFileName);
Unt doExpandEnv(
   OUT Text dst, // where to put the result
   NULLABLE CS src  // input string e.g. "$HOME/eegl.hlp"
);
Unt doExpandEnvVarsWithEscaped(
   OUT Text dst, //where to put the result. Length must be sufficient!
   CS srcArg,    //input string e.g. "$HOME/eegl.help"
   Boole one,    //"srcp" is one file name
   CS startstr   //start again after this (can be NULL)
);
NULLABLE CS eeglGetEnv(CS name);
void eeUnsetenv(CS var);
void eeSetenv_ext(CS name, CS val);
void eeSetenv(CS name, CS val);
void line_breakcheck(void);
void fast_breakcheck(void);
void veryfast_breakcheck(void);
int u_save_cursor(void);
int u_save(LineNr top, LineNr bot);
int u_savesub(LineNr lnum);
int u_savedel(LineNr lnum, long nlines);
int undo_allowed(void);
int u_savecommon(LineNr top, LineNr bot, LineNr newbot, int reload);
void u_compute_hash(OUT Byte hash[UNDO_HASH_SIZE]);
void u_write_undo(CS name, Boole forceit, Book* book, Arr(Byte) hash);
void u_read_undo(CS name, Arr(Byte) hash, CS orig_name);
void u_undo(int count);
void u_redo(int count);
void undo_time(long step, int sec, int file, int absolute);
void u_sync(int force);
void c_undolist(Invocation*);
void c_undojoin(Invocation*);
void u_unchanged(Book* book);
void u_find_first_changed(void);
void u_update_save_nr(Book* book);
void invalidateUndoBufferAndFreeBlocks(Book* book);
void u_clearline(void);
void u_undoline(void);
void f_undofile(Var* argvars, Var* returnVar);
void u_undofile_reset_and_delete(Book* book);
void f_undotree(Var* argvars, Var* returnVar);
void change_warning(int col);
void changed(void);
void doOnChangeToText(void);
int trim_to_int(Long x);
void f_listener_add(Arr(Var) argVars, OUT Var* returnVar);
void f_listener_flush(Arr(Var) argVars, OUT Var*);
void f_listener_remove(Arr(Var) argVars, OUT Var* returnVar);
void may_doInvokeListenersOnChangedText(Book* book, LineNr lnum, LineNr lnume, int added);
void doInvokeListenersOnChangedText(Book* book);
void remove_listeners(Book* book);
void changed_bytes(LineNr lnum, ColNr col);
void inserted_bytes(LineNr lnum, ColNr col, int added);
void appended_lines(LineNr lnum, long count);
void appended_lines_mark(LineNr lnum, long count);
void deleted_lines(LineNr lnum, long count);
void deleted_lines_mark(LineNr lnum, long count);
void doChangedLinesBook(
   Book* book,
   LineNr lnum,       // first line with change
   LineNr lnume,       // line below last changed line
   long xtra       // number of extra lines (negative when deleting)
);
void doChangedLines(
   LineNr lnum,    // first line with change
   ColNr col,      // column in first line with change
   LineNr lnume,   // line below last changed line
   long xtra       // number of extra lines (negative when deleting)
);
void unchanged(Book* book, int always_inc_changedtick);
void ins_bytes(CS p);
void ins_bytes_len(CS p, int len);
void insertChar(Unt c);
void replaceChar(Unt c);
void opInsertCharBytes(CS targetLine, int charlen, Boole replace);
void ins_str(CS s, Unt slen);
int del_char(Boole fixpos);
int del_chars(long count, Boole fixpos);
int del_bytes(Long   count, Boole fixpos_arg, int      use_delcombine);
int insertLine(Unt      dir);
int get_leader_len(CS line, Byte** flags, int backward, int include_space);
int openLine(
   Unt flags,
   int second_line_indent
);
int truncate_line(int fixpos);
void del_lines(long nlines,   int undo);
Unt get_op_type(Unt char1, Unt char2);
Boole op_is_change(int op);
int get_op_char(int optype);
int get_extra_op_char(int optype);
void op_shift(Operator *oper, int curs_top, int amount);
void shift_line(
   int   left,         // true if shift is to the left
   int   round,         // true if new indent is to be to a tabstop
   int   amount,         // Number of shifts
   Boole   call_changed_bytes)   // call changed_bytes()
;
Unt gchar_pos(Pos *pos);
Unt gchar_cursor(void);
int op_delete(Operator* oper);
Boole swapchar(Unt opTy, Pos* pos);
void op_insert(Operator *oper, long count1);
int op_change(Operator *oper);
void adjust_cursor_eol(void);
CS skip_comment(CS line, Boole process, Boole include_space, OUT Boole* is_comment);
int doJoinLinesUnderCursor(
   long count,
   Boole insert_space,
   Boole save_undo,
   Boole use_formatoptions,
   Boole setmark
);
void block_prep(
   Operator* oper,
   OUT BlockDef* bdp,
   LineNr lnum,
   Boole is_del
);
void doCharwiseBlockPrep(
   Pos start,
   Pos end,
   BlockDef* bdp,
   LineNr lnum,
   int inclusive
);
void op_addsub(
   Operator* oper,
   LineNr prenum1,       // Amount of add/subtract
   int g_cmd          // was g<c-a>/g<c-x>
);
void doClearOpArg(Operator *oper);
void cursor_pos_info(Bag* dict);
CS did_set_operatorfunc(OptionChange *cha);
void opsFreeOperatorFnOption(void);
int set_ref_in_opfunc(int copyID);
void doExecuteVisualOperator(ActionArg* cap, int old_col, int clipbYank);
Tyme eeTime(void);
CS get_ctime(Tyme thetime, int add_newline);
void f_localtime(Arr(Var), OUT Var* returnVar);
void f_reltime(Arr(Var) argVars, OUT Var* returnVar);
void f_reltimefloat(Arr(Var) argVars, OUT Var* returnVar);
void f_reltimestr(Arr(Var) argVars, OUT Var* returnVar);
void f_strftime(Arr(Var) argVars, OUT Var* returnVar);
void f_strptime(Var* argVars, Var* returnVar);
long proftime_time_left(ProfTime *due, ProfTime *now);
Timer* create_timer(long msec, int repeat);
void timer_start(Timer *timer);
long check_due_timer(void);
void stop_timer(Timer *timer);
int set_ref_in_timer(int copyID);
int timer_valid(Timer *timer);
void timer_free_all(void);
void f_timer_info(Arr(Var) argVars, OUT Var* returnVar);
void f_timer_pause(Arr(Var) argVars, OUT Var*);
void f_timer_start(Arr(Var) argVars, OUT Var* returnVar);
void f_timer_stop(Arr(Var) argVars, OUT Var*);
void f_timer_stopall(Arr(Var), OUT Var*);
void time_push(void *tv_rel, void *tv_start);
void time_pop(void   *tp);
void time_msg(
   CS mesg,
   void* tv_start  // only for scriptRunFile: start time; actually (TimeVal *)
);
Tyme get8ctime(FILE *fd);
int put_time(FILE *fd, Tyme the_time);
void time_to_bytes(Tyme the_time, CS buf);
void add_time(CS buf, Unt buflen, Tyme tt);
void profile_start(ProfTime *tm);
void profile_setlimit(long msec, ProfTime *tm);
int profile_passed_limit(ProfTime *tm);
void profile_end(ProfTime *tm);
void profile_sub(ProfTime *tm, ProfTime *tm2);
void profile_zero(ProfTime *tm);
CS profile_msg(ProfTime *tm);
long elapsed(TimeVal *start_tv);
void stop_timeout(void);
volatile sig_atomic_t * start_timeout(long msec);
void delete_timer(void);
void stop_timeout(void);
volatile sig_atomic_t* start_timeout(long msec);
int getviscol(void);
int coladvance_force(ColNr wcol);
int coladvance(ColNr wantcol);
int getvpos(Pos *pos, ColNr wantcol);
int inc_cursor(void);
int inc(Pos *lp);
int incl(Pos *lp);
int dec_cursor(void);
int dec(Pos *lp);
int decl(Pos *lp);
void check_pos(Book* book, Pos *pos);
long get_sw_value(Book *book);
int get_indent(void);
int get_indent_lnum(LineNr lnum);
int get_indent_buf(Book* book, LineNr lnum);
int set_indent(
   int      size,          // measured in spaces
   int      flags
);
int get_number_indent(LineNr lnum);
int getBreakindentForPort(Portal* po, CS line);
int inindent(int extra);
void op_reindent(Operator *oper, int (*how)(void));
int preprocs_left(void);
int may_do_si(void);
void doTrySmartIndent(int c);
void opChangeIndent(
   int type,
   int amount,
   int round,
   Boole call_changed_bytes // call changed_bytes()
);
void c_retab(Invocation *eap);
int get_expr_indent(void);
Boole doIsIndentationExpressionBased(void);
void fix_indent(void);
void f_indent(Arr(Var) argVars, OUT Var* returnVar);
int is_pos_in_string(CS line, ColNr col);
Pos* find_start_comment(int ind_maxcomment)  ;
void do_expr_indent(void);
