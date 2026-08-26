CS get_recorded(void);
Text get_inserted(void);
Unt get_keystroke(void);
int get_number(Boole allowColonToUpdate, int* mouse_used);
int stuff_empty(void);
int readbuf1_empty(void);
void typeahead_noflush(int c);
typedef enum {
   FLUSH_MINIMAL,
   FLUSH_TYPEAHEAD,   // flush current typebuf contents
   FLUSH_INPUT      // flush typebuf and inchar() input
} FlushBuffers;
void flush_buffers(FlushBuffers flush_typeahead);
void ResetRedobuff(void);
void CancelRedo(void);
void saveRedobuff(SaveRedo* save_redo);
void restoreRedobuff(SaveRedo* save_redo);
void AppendToRedobuff(CS s);
void AppendToRedobuffLit(CS str, int len);
void AppendToRedobuffSpec(CS s);
void AppendCharToRedobuff(Unt c);
void inpAppendNumberToRedoBuff(long n);
void stuffReadbuff(CS s);
void stuffRedoReadbuff(CS s);
void stuffReadbuffLen(CS s, long len);
void stuffReadbuffSpec(CS s);
void stuffcharReadbuff(Unt c);
void stuffnumReadbuff(long n);
void stuffescaped(CS arg, int literally);
int start_redo(long count, int old_redo);
int start_redo_ins(void);
void stop_redo_ins(void);
int noremap_keys(void);
int insertIntoTypebuf(
   CS str,
   Unt noremap,
   int offset,
   Boole nottyped,
   Boole silent
);
int ins_char_typebuf(int c, int modifiers);
int typebuf_changed(int changeCnt);
int typebuf_typed(void);
int typebuf_maplen(void);
void del_typebuf(int len, int offset);
void gotchars_ignore(void);
void ungetchars(int len);
int save_typebuf(void);
void save_typeahead(TypeaheadSave *tp);
void restore_typeahead(TypeaheadSave* tp, Boole overwrite);
void openscript(CS name, Boole directly);
void close_all_scripts(void);
int using_script(void);
void before_blocking(void);
Unt mergeModifierKey(Unt cArg, Unt* modifiers);
Unt vgetc(void);
Unt safe_vgetc(void);
Unt plain_vgetc(void);
Unt vpeekc(void);
int vpeekc_nomap(void);
Unt vpeekc_any(void);
int char_avail(void);
void f_getchar(Arr(Var) argvars, Var* returnVar);
void f_getcharstr(Arr(Var) argvars, Var* returnVar);
void f_getcharmod(Arr(Var) argvars UNUSED, Var* returnVar);
void parse_queued_messages(void);
void vungetc(Unt c);
int input_available(void);
void may_add_last_used_map_to_redobuff(void);
int do_cmdkey_command(Unt key, Unt flags);
void reset_last_used_map(MapBlock* mp);
int eeglStrNsize(CS s, int len);
int eeglStrSize(CS s);
int eeIsFnameChar_or_wc(Unt c);
int ends_in_white(LineNr lnum);
int same_leader(LineNr lnum, int leader1_len, CS leader1_flags, int leader2_len, CS leader2_flags);
int getwhitecols_curline(void);
void format_lines(LineNr   line_count, int avoid_fex);
int mb_ptr2cells_len(CS p, int size);
CS inputInitCharLens(void);
int mb_get_class(CS p);
int inpGetClassForBook(CS p, Book* book);
int utf_composinglike(CS p1, CS p2);
int utfc_ptr2char(CS p, OUT int* pcc);
int utfc_ptr2char_len(
    CS p,
    OUT int* pcc,   // return: composing chars, last one is 0
    int maxlen
);
int utf_class(int c);
void mb_adjust_cursor(void);
void mb_adjustpos(Book* book, Pos *lp);
void f_charclass(Arr(Var) argvars, Var* returnVar UNUSED);
int utf_class_buf(Unt c, Book* book);
int mb_char2cells(int c);
int utf_uint2cells(Unt c);
CS sanitizeStr(CS s);
void show_utf8(void);
void trans_characters(CS buf, int bufsize);
int mb_ptr2cells(CS p);
int mb_ptr2cells_len(CS p, int size);
int mb_string2cells(CS p, int len);
CS findWordStart(CS ptr);
int do_mouse(
   Operator* oper,  // operator argument, can be NULL
   Unt c,           // K_LEFTMOUSE, etc
   Unt dir,         // Direction to 'put' if necessary
   long count,
   int fixindent    // PUT_FIXINDENT if fixing indent necessary
);
void ins_mouse(int c);
void ins_mousescroll(int dir);
Boole is_mouse_key(Unt c);
Unt get_mouse_button(Unt code, OUT Boole* is_click, OUT Boole* is_drag);
void set_mouse_termcode(Unt n, CS s);
void del_mouse_termcode(Unt n);
void setmouse(void);
void mouseResetDragPortal(void);
Unt jump_to_mouse(
   Unt flags,
   OUT Boole* inclusive,   // used for inclusive operator, can be NULL
   Unt which_button   // MOUSE_LEFT, MOUSE_RIGHT, MOUSE_MIDDLE
);
void nv_mousescroll(ActionArg* cap);
void nv_mouse(ActionArg* cap);
void reset_held_button(void);
int termTryParseTermcode_mouse(CS key_name, OUT Unt* modifiers);
int mouse_comp_pos(
   Portal* port,
   OUT int* rowp,
   OUT int* colp,
   LineNr* lnump,
   int* plines_cache
);
Portal* mouseFindPortal(OUT int* rowp, OUT int* colp, MouseFindKind popup UNUSED);
int vcol2col(Portal* po, LineNr lnum, int vcol, ColNr *coladdp);
void f_getmousepos(Arr(Var) argvars UNUSED, Var* returnVar);
void mch_setmouse(Boole on);
void mch_bevalterm_changed(void);
