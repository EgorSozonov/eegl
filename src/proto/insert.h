int edit(Unt commChar, int startln, long count);
void edit_putchar(int c, Boole needDoHilite);
void set_insstart(LineNr lnum, int col);
void edit_unputchar(void);
void truncate_spaces(CS line, Unt len);
void backspace_until_column(int col);
int get_literal(int noReduceKeys);
void insertchar0(
   Unt c,         // character to insert or ZERO
   Unt flags,         // INSCHAR_FORMAT, etc.
   int second_indent      // indent for second line if >= 0
);
void start_arrow(Pos* end_insert_pos);
int stop_arrow(void);
void set_last_insert(Unt c);
void free_last_insert(void);
CS add_char2buf(Unt c, CS s);
void beginline(Unt flags);
int oneright(void);
int oneleft(void);
void cursor_up_inner(Portal* po, long n);
int cursor_up(long   n, Boole upd_topline);
void cursor_down_inner(Portal* wp, long n);
int cursor_down(long n, int upd_topline);
int stuff_inserted(
   Unt c,      // Command character to be inserted
   Long count,   // Repeat this many times
   int no_esc   // Don't add an ESC at the end
);
Text get_last_insert(void);
CS get_last_insert_save(void);
int bracketed_paste(PasteMode mode, int drop, ArrayList *gap);
int ins_copychar(LineNr lnum);
ColNr get_nolist_virtcol(void);
void set_can_cindent(int val);
int ins_applyAutocomms(AutoEvent event);
void ins_ctrl_x(void);
int ctrl_x_mode_whole_line(void)    ;
int ctrl_x_mode_not_default(void);
int ctrl_x_mode_not_defined_yet(void);
int compl_status_adding(void);
int compl_status_sol(void);
int compl_status_local(void);
int eeIsCtrlXKey(Unt c);
Unt ins_compl_add_infercase(
   CS str_arg,
   int len,
   int icase,
   CS fname,
   Unt dir,
   int cont_s_ipos,  // next ^X<> will set initial_pos
   int score
);
Decoration getDecorationIfColumnIsWithinCompletion(LineNr lnum, int col);
int ins_compl_lnum_in_range(LineNr lnum);
void ins_compl_show_pum(void);
CS ins_compl_leader(void);
int ins_compl_active(void);
int ins_compl_win_active(Portal *wp);
int ins_compl_interrupted(void);
int ins_compl_len(void);
CS did_set_thesaurusfunc(OptionChange* cha);
int set_ref_in_insexpand_funcs(int copyID);
void f_complete(Arr(Var) argvars, Var* returnVar UNUSED);
void f_complete_add(Arr(Var) argvars, Var* returnVar);
void f_complete_check(Arr(Var) argvars UNUSED, Var* returnVar);
void f_complete_match(Arr(Var) argvars, Var* returnVar);
void f_complete_info(Arr(Var) argvars, Var* returnVar);
void ins_compl_check_keys(int frequency, Boole in_compl_func);
void free_insexpand_stuff(void);
CS get_disassemble_argument(Expand* xp, int idx);
CS setCompletefunc(OptionChange* cha);
void inSetOmniCbForBook(Book* book);
void inSetTagCbForBook(Book* book);
void inSetCustomCompletionCbForBook(Book* book);
CS setOmnifunc(OptionChange* cha);
Unt setCompletionCallbacks(OptionChange *cha);
Boole has_format_option(int x);
void internal_format(
   int textwidth,
   int second_indent,
   int flags,
   int format_only,
   Unt c // character to be inserted (can be ZERO)
);
int fmt_check_par(LineNr lnum, OUT int* leader_len, OUT CS* leader_flags, int doComments);
void auto_format(
    int trailblank,   // when true also format with trailing blank
    int prev_line   // may start in previous line
);
void check_auto_format(int end_insert);
int comp_textwidth(int ff);
