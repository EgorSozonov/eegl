#ifndef AVOID_DUPLICATION
int stat(const char* restrict path, struct stat* restrict buf);
#define VGR_GLOBAL  1
#define VGR_NOJUMP  2
#define VGR_FUZZY   4
#define CCGD_AW       1   //do autowrite if book was changed
#define CCGD_MULTWIN  2   //check also when several wins for the buf
#define CCGD_FORCEIT  4   //! used
#define CCGD_ALLBOOKS 8   //may write all books
#define CCGD_EXCMD   16   //may suggest using !
#define OPENLINE_DELSPACES    0x01 //delete spaces after cursor
#define OPENLINE_DO_COM       0x02 //format comments
#define OPENLINE_KEEPTRAIL    0x04 //keep trailing spaces
#define OPENLINE_MARKFIX      0x08 //fix mark positions
#define OPENLINE_COM_LIST     0x10 //format comments with list/2nd line indent
#define OPENLINE_FORMAT       0x20 //formatting Long comment
#define OPENLINE_FORCE_INDENT 0x40 //use second_line_indent without indent logic
#endif

void do_ascii(Invocation*);
void c_align(Invocation* invo);
void c_sort(Invocation* invo);
void c_uniq(Invocation* invo);
int do_move(LineNr line1, LineNr line2, LineNr dest);
void free_prev_shellcmd(void);
void do_bang(
	int addrCount,
	Invocation* invo,
	Boole forceit,
	Boole do_in,
	Boole do_out
);
void do_shell(S cmd, Unt flags);
int prompt_for_number(int *mouse_used);
S make_filter_cmd(S cmd, NULLABLE S inputFName, NULLABLE S outputFName);
void do_fixdel(Invocation*);
void c_file(Invocation* invo);
void c_update(Invocation* invo);
void c_write(Invocation* invo);
int do_write(Invocation* invo);
void c_wnext(Invocation* invo);
void do_wqall(Invocation* invo);
int getfile(
	int fnum,
	S ffname_arg,
	S sfname_arg,
	int setpm,
	LineNr lnum,
	Boole forceit
);
void c_append(Invocation* invo);
void c_change(Invocation* invo);
void c_z(Invocation* invo);
S skip_substitute(S start, Unt delimiter);
void c_substitute(Invocation* invo);
int do_sub_msg(int       count_only);
S get_old_sub(void);
void set_old_sub(S val);
void free_old_sub(void);
void c_global(Invocation* invo);
void global_exe(S cmd);
int prepare_tagpreview(
	int      undo_sync,       //sync undo when leaving the portal
	int      use_previewpopup,   //use popup if 'previewpopup' set
	UsePopup   use_popup       //use other popup portal
);
void c_smile(Invocation*);
void c_drop(Invocation* invo);
S skipEeglGrepPat(S p, Byte **s, Unt *flags);
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
int executeCommLine(S cmd);
S getline_peek(
	LineGetter fgetline,
	void* cookie      //argument for fgetline()
);
int doCommand(
	S commline,
	LineGetter fgetline,
	void* cookie,      //argument for fgetline()
	Unt flags
);
S ex_errmsg(S msg, S arg);
S ex_range_without_command(Invocation* invo);
int checkforcmd(
	OUT S* pp,  //start of command
	S cmd,      //name of command
	int len //required length
);
int doCheckForCommNoParen(
	 OUT S* pp,  //start of command
	 S cmd,      //name of command
	 int len      //required length
);
int doParseCommandModifiers(
	Invocation* invo,
	OUT S* errorMsg,
	CommandModifier* cmod,
	int skip_only
);
void applyCommModifiers(CommandModifier* cmod);
void undoCommModifier(CommandModifier *cmod);
int parse_cmd_address(OUT Invocation* invo, OUT S* errorMsg, Boole silent);
S skip_option_env_lead(S start);
int number_method(S cmd);
S findCommand(Invocation* invo, int* full, int (*lookup)(S, Unt, int cmd));
int modifier_len(S cmd);
int cmd_exists(S name);
void f_fullcommand(Var *argvars, Var *returnVar);
CommIndex commandGetInd(S cmd, int len);
Long commandGetFlags(CommIndex idx);
S skip_range(
	S cmd_start,
	int skip_star,   //skip "*" used for Visual range
	Unt* ctx)      //pointer to context or NULL
;
LineNr doGetCommandAddress(
	Invocation* invo,
	OUT S* ptr,
	CommandAddress   addressKind,
	int skip,      //only skip the address, don't use it
	int silent,      //no errors or side effects
	int to_other_file,  //flag: may jump to other file
	int address_count //1 for first address, >1 after comma
);
int expand_filename(Invocation* invo, OUT S* commline, OUT S* errorMsg);
void separateNextCommand(Invocation* invo, int keep_backslash);
S skip_cmd_arg(S p, int rembs);
int get_bad_opt(S p, Invocation* invo);
int expand_argopt(
	S pat,
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
S find_nextcmd(S p);
S get_command_name(Expand *, int idx);
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
int expand_findfunc(S pat, OUT ExpandMatch* matches);
S setFindFn(OptionChange* cha);
void doFreeFindFnOption(void);
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
void do_exedit(Invocation* invo, NULLABLE Portal* old_curPor);
void c_syncbind(Invocation*);
void c_read(Invocation* invo);
void free_cd_dir(void);
void post_chdir(CdScopeKind scope);
void trigger_DirChangedPre(S acmd_fname, S new_dir);
int changedir_func(S new_dir, CdScopeKind scope);
void c_cd(Invocation* invo);
void c_pwd(Invocation*);
void c_equal(Invocation* invo);
void c_sleep(Invocation* invo);
void doSleep(Long msec, Boole hide_cursor);
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
int eeMkdir_emsg(S name, int prot);
FILE * doOpenCommandsFile(S fname, int forceit, S mode);
void c_mark(Invocation* invo);
void update_topline_cursor(void);
void c_normal(Invocation* invo);
void c_startinsert(Invocation* invo);
void c_stopinsert(Invocation*);
void exec_normal_cmd(S cmd, int remap, int silent);
void exec_normal(int was_typed, int use_vpeekc, int may_use_terminal_loop);
void c_checkpath(Invocation* invo);
void c_psearch(Invocation* invo);
void c_findpat(Invocation* invo);
void c_ptag(Invocation* invo);
void c_pedit(Invocation* invo);
void c_pbuffer(Invocation* invo);
void c_stag(Invocation* invo);
void c_tag(Invocation* invo);
int find_commline_var(S src, Unt *usedlen);
S evalVars(
	OUT LineNr* lnump,      //line number for :e command, or NULL
	OUT S* errorMsg,   //pointer to error message
	S src,      //pointer into commandline
	S srcstart,   //beginning of valid memory for src
	Unt* usedlen,   //characters after src that are used
	int* escaped,   //return value has escaped white space (can be NULL)
	int empty_is_error   //empty result is considered an error
);
S expand_sfile(S arg);
void dialog_msg(S buff, S format, S fname);
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
Unt ask_yesno(S str, int direct);
S doExpandEnvInMultiplePaths(S src);
S doExpandEnvInFilePaths(S src, Boole singleFileName);
Unt doExpandEnv(
	OUT Text dst, //where to put the result
	NULLABLE S src  //input string e.g. "$HOME/eegl.hlp"
);
Unt doExpandEnvVarsWithEscaped(
	OUT Text dst, //where to put the result. Length must be sufficient!
	S srcArg,    //input string e.g. "$HOME/eegl.help"
	Boole one,    //"srcp" is one file name
	S startstr   //start again after this (can be NULL)
);
NULLABLE S eeglGetEnv(S name);
void eeUnsetenv(S var);
void eeSetenv_ext(S name, S val);
void eeSetenv(S name, S val);
void line_breakcheck(void);
void fast_breakcheck(void);
void veryfast_breakcheck(void);
int u_save_cursor(void);
int u_save(LineNr top, LineNr bot);
int u_savesub(LineNr lnum);
int u_savedel(LineNr lnum, Long nlines);
int undo_allowed(void);
int u_savecommon(LineNr top, LineNr bot, LineNr newbot, int reload);
void u_compute_hash(OUT Byte hash[UNDO_HASH_SIZE]);
void u_write_undo(S name, Boole forceit, Book* book, Arr(Byte) hash);
void u_read_undo(S name, Arr(Byte) hash, S orig_name);
void u_undo(int count);
void u_redo(int count);
void undo_time(Long step, int sec, int file, int absolute);
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
void appended_lines(LineNr lnum, Long count);
void appended_lines_mark(LineNr lnum, Long count);
void deleted_lines(LineNr lnum, Long count);
void deleted_lines_mark(LineNr lnum, Long count);
void doChangedLinesBook(
	Book* book,
	LineNr lnum,    //first line with change
	LineNr lnume,   //line below last changed line
	Long xtra       //number of extra lines (negative when deleting)
);
void doChangedLines(
	LineNr lnum,    //first line with change
	ColNr col,      //column in first line with change
	LineNr lnume,   //line below last changed line
	Long xtra       //number of extra lines (negative when deleting)
);
void doResetChangedFlag(Book* book, int always_inc_changedtick);
void ins_bytes(S p);
void ins_bytes_len(S p, int len);
void insertChar(Unt c);
void replaceChar(Unt c);
void opInsertCharBytes(S targetLine, int charlen, Boole replace);
void ins_str(S s, Unt slen);
int del_char(Boole fixpos);
int del_chars(Long count, Boole fixpos);
int del_bytes(Long count, Boole fixpos_arg, int use_delcombine);
int doInsertLine(Byte dir);
int get_leader_len(S line, Byte** flags, int backward, int include_space);
int openLine(Unt flags, int second_line_indent);
int truncate_line(int fixpos);
void del_lines(Long nlines,   int undo);
Unt get_op_type(Unt char1, Unt char2);
Boole op_is_change(int op);
int get_op_char(int optype);
int get_extra_op_char(int optype);
void op_shift(Operator *oper, int curs_top, int amount);
void shift_line(
	int left,         //true if shift is to the left
	int round,         //true if new indent is to be to a tabstop
	int amount,         //Number of shifts
	Boole call_changed_bytes   //call changed_bytes()
);
Unt gchar_pos(Pos *pos);
Unt gchar_cursor(void);
int op_delete(Operator* oper);
Boole swapchar(Unt opTy, Pos* pos);
void op_insert(Operator* oper, Long count1);
int op_change(Operator *oper);
void doAdjustCursorEol(void);
S skip_comment(S line, Boole process, Boole include_space, OUT Boole* is_comment);
int doJoinLinesUnderCursor(
	Long count,
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
	LineNr prenum1,       //Amount of add/subtract
	int g_cmd          //was g<c-a>/g<c-x>
);
void doClearOpArg(Operator *oper);
void cursor_pos_info(Bag* dict);
S did_set_operatorfunc(OptionChange *cha);
void opsFreeOperatorFnOption(void);
void doExecuteVisualOperator(ActionArg* aArg, int old_col, int clipbYank);
int doGetCursorPos(void);
int doColAdvance_force(ColNr wcol);
int doColAdvance(ColNr wantcol);
int getvpos(Pos *pos, ColNr wantcol);
int inc_cursor(void);
int inc(Pos *lp);
int incl(Pos *lp);
int dec_cursor(void);
int dec(Pos *lp);
int decl(Pos *lp);
void check_pos(Book* book, Pos *pos);
Long get_sw_value(Book *book);
int get_indent(void);
int get_indent_lnum(LineNr lnum);
int get_indent_buf(Book* book, LineNr lnum);
Boole doSetIndent(int size, Unt flags);
int get_number_indent(LineNr lnum);
int inindent(int extra);
void op_reindent(Operator *oper, int (*how)(void));
int preprocs_left(void);
int may_do_si(void);
void doTrySmartIndent(int c);
void opChangeIndent(
	int type,
	int amount,
	int round,
	Boole call_changed_bytes //call changed_bytes()
);
void c_retab(Invocation *eap);
int get_expr_indent(void);
void fix_indent(void);
void f_indent(Arr(Var) argVars, OUT Var* returnVar);
int is_pos_in_string(S line, ColNr col);
Pos* find_start_comment(int ind_maxcomment)  ;
void doIndentExpr(void);
