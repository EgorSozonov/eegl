#ifndef AVOID_DUPLICATION
#define FSK_KEYCODE     0x01   //prefer key code, e.g. K_DEL instead of DEL
#define FSK_KEEP_X_KEY  0x02   //don't translate xHome to Home key
#define FSK_IN_STRING   0x04   //true in string, double quote is escaped
#define FSK_SIMPLIFY    0x08   //simplify <C-H> and <A-x>
#define FSK_FROM_PART   0x10   //left-hand-side of mapping
extern char *UP, *BC, PC;
char *ptsname(int);
int grantpt(int);
#endif

void termInitProps(Boole all);
void f_terminalprops(Var*, Var* returnVar);
int set_termname(S termName);
void free_cur_term(void);
void getlinecol(Arr(long) cols, Arr(long) rows);
int add_termcap_entry(S name, int force);
void termInitTerminfo(S name);
void termOutFlush(void);
void out_char(unsigned c);
void out_str_nf(S s);
void out_str(S s);
void termMoveCursor(int row, int col);
void term_cursor_right(int i);
void term_append_lines(int line_count);
void term_delete_lines(int line_count);
void term_enable_mouse(int enable);
void term_set_winpos(int x, int y);
int term_get_winpos(int* x, int* y, Long timeout);
void term_set_winsize(int height, int width);
void termApplyFgColor(Byte n);
void termApplyBgColor(Byte n);
void termApplyUnderColor(Byte n);
void ttest(int pairs);
void add_long_to_buf(Ulong val, S dst);
int get_bytes_from_buf(S buffer, S bytes, int num_bytes);
void check_shellsize(void);
void clampScreenSize(void);
void win_new_shellsize(void);
void shell_resized(void);
void shell_resized_check(void);
void set_shellsize(int width, int height, int mustset);
void out_str_t_TE(void);
void out_str_t_TI(void);
void out_str_t_BE(void);
void may_send_t_RK(void);
void termSetMode(TermInputMode tmode);
void starttermcap(void);
void termStopTerminfo(void);
void check_terminal_behavior(void);
int termIsScreenBeingSwapped(void);
void scroll_start(void);
void cursor_on_force(void);
void cursor_on(void);
void cursor_off(void);
void cursor_sleep(void);
void cursor_unsleep(void);
void term_cursor_mode(int forced);
void term_cursor_color(S color);
int blink_state_is_inverted(void);
void termSetCursorShape(int shape, int blink);
void scroll_region_set(Portal* wp, int off);
void scroll_region_reset(void);
void clear_termcodes(void);
void termAddRecognizedTermcode(S name, S string, Boole isAtcFromTerm);
S find_termcode(S name);
void del_termcode(S name);
void set_mouse_topline(Portal* po);
int is_mouse_topline(Portal* po);
int termPutStrIntoTypeBuf(int offset, int slen, Text newText);
Unt decode_modifiers(int n);
int termTryParseTermcode(int max_offset, NULLABLE OUT Text buffer, OUT int* bufLen);
void get_stty(void);
int get_tty_info(int fd, TtyInfo* info);
void mch_termSetMode(TermInputMode tmode);
void check_mouse_termcode(void);
S get_key_name(int i);
S replace_termcodes(
	S from,
	S* bufP,
	ScriptId sid_arg,   //script ID to use for <SID>, or 0 to use scriptPosG
	Unt flags,
	OUT Boole* didSimplify,
	Boole recognizeRawKeycodes
);
Unt simplify_key(Unt key, Unt* modifiers);
void show_termcodes(Unt flags);
S get_special_key_name(Unt c, int modifiers);
int get_special_key_code(S name);
int show_one_termcode(S name, S code, int printit);
void ansi_color2rgb(int nr, OUT Byte *r, OUT Byte *g, OUT Byte *b, OUT Byte *ansi_idx);
void cterm_color2rgb(int nr, Byte* r, Byte* g, Byte* b, Byte* ansi_idx);
int term_replace_keycodes(S ta_buf, int ta_len, int len_arg);
int termFindSpecialKey(
	OUT Byte** srcp,
	OUT Unt* modp,
	Unt flags,      //FSK_ values
	OUT Boole* didSimplify //found <C-H> or <A-x>
);
int termFindSpecialKey_in_table(int c);
int special_to_buf(Unt key, Unt modifiers, int escape_ks, OUT S dst);
int trans_special(
	OUT Byte** srcp,
	S dst,
	Unt flags,      //FSK_ values
	int escape_ks,   //escape K_SPECIAL bytes in the character
	OUT Boole* didSimplify  //FSK_SIMPLIFY and found <C-H> or <A-x>
);
int setup_slavepty(int fd);
int openpty(char **ttyn);
int mch_isatty(int fd);
