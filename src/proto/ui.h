int fstat(int fd, struct stat* statbuf);
declStruct(VTermLineInfo);
int WCWIDTH_FUNCTION(uint32_t ucs);
int IS_COMBINING_FUNCTION(uint32_t codepoint);
int GET_SPECIAL_PTY_TYPE_FUNCTION(void);
void init_job_options(JobOptions *opt);
void c_terminal(Invocation* invo);
int expand_terminal_opt(CS pat, Expand* xp, RegMatch* rmp, OUT ExpandMatch* matches);
int term_write_session(FILE* fd, Portal* po, EeSet* terminal_bufs);
int term_should_restore(Book* book);
void free_terminal(Book* book);
void free_unused_terminals(void);
void write_to_term(Book *book, CS msg, Channel* channel);
int term_job_running(Terminal *term);
int term_job_running_not_none(Terminal *term);
int term_none_open(Terminal *term);
int term_confirm_stop(Book* book);
int term_try_stop_job(Book* book);
int term_check_timers(int next_due_arg, ProfTime *now);
int term_in_normal_mode(void);
void term_enter_job_mode(void);
void check_no_reduce_keys(void);
int send_keys_to_term(Terminal *term, Unt c, int modmask, int typed);
int terminal_is_active(void);
int term_use_loop(void);
void term_enterPortaled(void);
void term_focus_change(int in_focus);
int terminal_loop(int blocking);
Decoration cellToDecoration(VTermDeco flags, VTermColor fg, VTermColor bg);
int may_close_term_popup(void);
void term_channel_closing(Channel* ch);
void term_channel_closed(Channel* ch);
void term_check_channel_closed_recently(void);
int termDoUpdatePortal(Portal* po);
void termUpdatePortal(Portal* po);
void termDidUpdatePortal(Portal* po);
int term_is_finished(Book* book);
int term_shobuffer(Book* book);
void uiBeforeLeavingTerminal(void);
Decoration uiGetDeco(Portal* po, LineNr lnum, int col);
void term_update_colors_all(void);
CS term_get_status_text(Terminal* term);
void term_clear_status_text(Terminal* term);
int set_ref_in_term(int copyID);
void f_term_dumpwrite(Var* argvars, Var* returnVar UNUSED);
int term_swap_diff(void);
void f_term_dumpdiff(Arr(Var) argvars, Var* returnVar);
void f_term_dumpload(Arr(Var) argvars, Var* returnVar);
void f_term_getaltscreen(Var* argvars, Var* returnVar);
void f_term_getcursor(Var* argvars, Var* returnVar);
void f_term_getjob(Arr(Var) argvars, Var* returnVar);
void f_term_getline(Arr(Var) argvars, Var* returnVar);
void f_term_getscrolled(Arr(Var) argvars, Var* returnVar);
void f_term_getsize(Arr(Var) argvars, Var* returnVar);
void f_term_setsize(Arr(Var) argvars, Var* returnVar UNUSED);
void f_term_getstatus(Arr(Var) argvars, Var* returnVar);
void f_term_gettitle(Arr(Var) argvars, Var* returnVar);
void f_term_gettty(Arr(Var) argvars, Var* returnVar);
void f_term_list(Arr(Var) argvars UNUSED, Var* returnVar);
void f_term_scrape(Arr(Var) argvars, Var* returnVar);
void f_term_sendkeys(Arr(Var) argvars, Var* returnVar UNUSED);
void f_term_setapi(Arr(Var) argvars, Var* returnVar UNUSED);
void f_term_setrestore(Arr(Var) argvars UNUSED, Var* returnVar UNUSED);
void f_term_setkill(Arr(Var) argvars UNUSED, Var* returnVar UNUSED);
void f_term_start(Arr(Var) argvars, Var* returnVar);
void f_term_wait(Arr(Var) argvars, Var* returnVar UNUSED);
void term_send_eof(Channel* ch);
Job* term_getjob(Terminal* term);
void preserve_exit(void);
int uiRealWaitForChar(int fd, Long msec, OUT int* interrupted);
int mch_input_isatty(void);
void uiInit(void);
void ui_write(CS s, int len, int console UNUSED);
void ui_inBytendo(CS s, int len);
int ui_inchar(
   OUT CS buf,
   int maxlen,
   long wtime,       // don't use "time", MIPS cannot handle it
   int changeCnt
);
int inchar_loop(
   OUT CS buf,
   int maxlen,
   long wtime,       // don't use "time", MIPS cannot handle it
   int changeCnt,
   int (*wait_func)(long wtime, int *interrupted, Boole ignore_input),
   int (*resize_func)(int check_only)
);
int waitForChar(long msec, OUT int* interrupted, Boole ignore_input);
int ui_char_avail(void);
void ui_delay(long msec_arg, int ignoreinput);
int ui_get_shellsize(void);
void ui_set_shellsize(int mustset UNUSED);
int uiGetPortPos(int* x, int* y, Long timeout UNUSED);
void ui_breakcheck(void);
void ui_breakcheck_force(Boole force);
int eeIsInputBufFull(void);
int eeIsInputBufEmpty(void);
int eeglFree_in_input_buf(void);
CS get_input_buf(void);
void set_input_buf(CS p, Boole overwrite);
void add_to_input_buf(CS s, int len);
void add_to_input_buf_csi(CS str, int len);
void trash_input_buf(void);
int read_from_input_buf(CS buf, long maxlen);
void fill_input_buf(Boole exit_on_error);
void read_error_exit(void);
void ui_cursor_shape(void);
Unt check_col(Unt col);
Unt check_row(Unt row);
long scroll_line_len(LineNr lnum);
LineNr ui_find_longest_lnum(void);
void ui_focus_change(int in_focus);
int mch_report_winsize(int fd, int rows, int cols);
void mch_set_shellsize(void);
void mch_calc_cell_size(CellSize* cs_out);
int mch_get_shellsize(void);
int uiValidateTabpanelopt(CS new);
int tabpanel_width(void);
int tabpanel_leftcol(void);
void draw_tabpanel(void);
Unt get_tabNr_on_tabpanel(void);
