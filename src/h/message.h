#ifndef AVOID_DUPLICATION
int eeSnprintf0(S str, Unt str_m, const char *fmt, ...);
#endif

S msg_strtrunc(S s, int force);
void trunc_string(
	Byte   *s,
	Byte   *builder,
	int      room_in,
	int      buflen)
;
void msg_start(void);
void msg_starthere(void);
void msg_putchar(Unt c);
void msgPutcharDeco(Unt c, char flags);
void msg_outnum(long n);
void msg_home_replace(Byte *fname);
void msg_home_replace_hl(Byte *fname);
int msg_outtrans(Byte* str);
int msgOuttransDeco(S str, Byte flags);
int msgTranslatedSlice(Text slice);
S msgOneChar(S p, char flags);
int msgOuttransLenDeco(Text slice, char flags);
void msg_make(S arg);
int msg_outtrans_special(
	S strstart,
	int from,   //true for lhs of a mapping
	int maxlen  //screen columns, 0 for unlimited
);
S str2special_save(
	Byte  *str,
	int       replace_spaces,   //true to replace " " with "<Space>".
				//used for the lhs of mapping and keytrans().
	int       replace_lt      //true to replace "<" with "<lt>".
);
S str2special(
	Byte** sp,
	int      replace_spaces,   //true to replace " " with "<Space>".
				//used for the lhs of mapping and keytrans().
	int  replace_lt)   //true to replace "<" with "<lt>".
;
void str2specialbuf(Byte *sp, OUT Byte *builder, int len);
void msg_prt_line(S s, int list);
int msg(S s);
int verb_msg(S s);
int msgDeco(S s, char flags);
int msgAndKeep(S s, char flags, int keep);
int smsg0(char const* s, ...);
int smsgDeco0(char flags, const char *s, ...);
int smsgDecoKeep0(char flags, const char *s, ...);
void reset_last_sourcing(void);
S msgTruncDeco(S s, char flags);
S msg_may_trunc(S s);
void msg_source(char flags);
void ignore_error_for_testing(Byte *error);
int emsg(S s);
int showErrFmtMsg0(char const* s, ...);
void internalErrMsg(S s);
void internalErrFmtMsg0(const char *s, ...);
void internal_error(S where);
void internal_error_no_abort(S where);
void emsg_invreg(int name);
void emsg_namelen(S msg, S name, int len);
int delete_first_msg(void);
int messagesopt_changed(S new);
void c_messages(Invocation *invo);
void wait_return(Boole redraw);
void set_keep_msg(Byte *s, char flags);
void msgmore(long n);
void set_keep_msg_from_hist(void);
int do_dialog(
	int,
	Byte*,
	Byte* message,
	Byte* buttons,
	int dfltbutton,
	Byte*,   //ioBuffG for inputdialog(), NULL otherwise
	int ex_cmd       //when true pressing : accepts default and starts a Command
);
int eeDialog_yesno(
	 int      type,
	 Byte   *title,
	 Byte   *message,
	 int      dflt)
;
int eeDialog_yesnocancel(
	 int      type,
	 Byte   *title,
	 Byte   *message,
	 int      dflt)
;
int eeDialog_yesnoallcancel(
	int      type,
	Byte   *title,
	Byte   *message,
	int      dflt
);
void msg_puts(S s);
void msg_puts_title(S s);
void outputShortenedToALine(Text slice, char flags);
void msgPutsDeco(S s, char flags);
int message_filtered(S msg);
int msg_use_printf(void);
void repeat_message(void);
void msg_clr_eos(void);
void msg_clr_eos_force(void);
void msgClearCommline(void);
int msg_end(void);
void msg_check(void);
int redirecting(void);
void verbose_enter(void);
void verbose_leave(void);
void verbose_enter_scroll(void);
void verbose_leave_scroll(void);
void verbose_stop(void);
int verbose_open(void);
void give_warning(Byte *message, int hl);
void give_warning_with_source(Byte *message, int hl, int with_source);
void give_warning2(Byte *message, Byte *a1, int hl);
void msg_advance(int col);
void msg_warn_missing_clipboard(void);
void may_clear_sb_text(void);
void sb_text_start_cmdline(void);
void sb_text_restart_cmdline(void);
void sb_text_end_cmdline(void);
void clear_sb_text(int all);
void show_sb_text(void);
void msg_sb_eol(void);
