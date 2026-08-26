int findsent(int dir, long count);
int normFindNextParagraf(
   OUT Boole* pincl,       // Return: true if last char is to be included
   int dir,
   long count,
   int what,
   int both
);
int startPS(LineNr lnum, int para, int both);
int fwd_word(
   long   count,
   int      bigword,    // "W", "E" or "B"
   int      eol)
;
int bck_word(long count, int bigword, int stop);
int end_word(long count, int bigword, int stop, int      empty);
int bckend_word(
   long   count,
   int      bigword,    // true for "B"
   int      eol)       // true: stop at end of line.
;
int current_word(
   Operator* oper,
   long   count,
   int      include,   // true: include word and white space
   int      bigword)   // false == word, true == WORD
;
int current_sent(Operator *oper, long count, int include);
int current_tagblock(Operator* oper, long count_arg, Boole includeWhiteSpace);
int current_par(
   Operator   *oper,
   long   count,
   int      include,   // true == include white space
   int      type      // 'p' for paragraph, 'S' for section
);
int current_quote(
   Operator* oper,
   long count,
   int include,   // true == include quote char
   int quotechar   // Quote character
);
int virtual_active(void);
Bag * get_v_event(SaveVEvent *sve);
void restore_v_event(Bag* v_event, SaveVEvent* sve);
void get_mode(CS buf);
void may_trigger_modechanged(void);
int get_real_state(void);
int check_text_or_curbuf_locked(Operator *oper);
void normalAction(Operator* oper, Boole toplevel);
void check_visual_highlight(void);
void end_visual_mode(void);
void end_visual_mode_keep_button(void);
void reset_VIsual_and_resel(void);
void reset_VIsual(void);
void restore_visual_mode(void);
int find_ident_under_cursor(OUT CS* text, int find_type);
int find_ident_at_pos(
   Portal* po,
   LineNr lnum,
   ColNr startcol,
   OUT CS* text,
   int* textcol,   // column where "text" starts, can be NULL
   int find_type
);
void prep_redo(
   int regname,
   long num,
   int cmd1,
   int cmd2,
   int cmd3,
   int cmd4,
   int cmd5
);
void prep_redo_num2(
    int       regname,
    long    num1,
    int       cmd1,
    int       cmd2,
    long    num2,
    int       cmd3,
    int       cmd4,
    int       cmd5)
;
void clearop(Operator *oper);
void clearopbeep(Operator *oper);
void may_clear_cmdline(void);
void clear_showcmd(void);
int add_to_showcmd(Unt c);
void add_to_showcmd_c(Unt c);
void push_showcmd(void);
void pop_showcmd(void);
void normPostProcessScrollbind(int check);
void check_scrollbind(LineNr topline_diff, long leftcol_diff);
int find_decl(
   CS ptr,
   int len,
   int locally,
   int thisblock,
   Unt flags_arg   // flags passed to searchit()
);
int get_visual_text(
   ActionArg* aArg,
   OUT CS* pp,       // return: start of selected text
   OUT int* lenp       // return: length of selected text
);
void start_selection(void);
int unadjust_for_sel_inner(Pos *pp);
void set_cursor_for_append_to_line(void);
int nv_screengo(Operator* oper, Unt dir, long dist);
void nv_scroll_line(ActionArg* aArg);
void do_nv_ident(int c1, int c2);
void nv_g_home_m_cmd(ActionArg* aArg);
void a_linewiseOperator(ActionArg* aArg);
int adjust_plines_for_skipcol(Portal *po);
int sms_marker_overlap(Portal* po, int extra2);
void update_topline_redraw(void);
void update_topline(void);
void update_curswant(void);
void check_cursor_moved(Portal *po);
void didChangePortalSettingCurPor(void);
void didChangePortalSetting(Portal *po);
void didChangePortalSettingBuf(Book *book);
void didChangePortalSettingAll(void);
void set_topline(Portal* po, LineNr lnum);
void changed_cline_bef_curs(void);
void changed_cline_bef_curs_win(Portal *po);
void changed_line_abv_curs(void);
void changed_line_abv_curs_win(Portal *po);
void normInvalidateDisplayOfChangedBookLine(Book* book);
void validate_botline(void);
void validate_botline_win(Portal *po);
void invalidate_botline(void);
void invalidate_botline_win(Portal *po);
void approximate_botline_win( Portal   *po);
int cursor_valid(void);
void validate_cursor(void);
void validate_virtcol(void);
void validate_virtcol_win(Portal* po);
void validate_cheight(void);
void validate_cursor_col(void);
int normalPortalColumnOffset(Portal *po);
void curs_columns(int may_scroll);
void textpos2screenpos(
   Portal* po,
   Pos* pos,
   int* rowp,   // screen row
   int* scolp,   // start screen column
   int* ccolp,   // cursor screen column
   int* ecolp   // end screen column
);
void f_screenpos(Var* argvars, Var* returnVar);
void f_virtcol2col(Var* argvars, Var* returnVar);
void scroll_redraw(int up, long count);
void scrolldown(long line_count, int byfold);
void scrollup(long line_count, int byfold);
void adjust_skipcol(void);
void check_topfill(Portal* po, int down);
void scrolldown_clamp(void);
void scrollup_clamp(void);
void normSetEmptyRowCount(Portal* po, int used);
void scroll_cursor_bot(int min_scroll, int set_topbot);
void scroll_cursor_halfway(int atend, int prefer_above);
void cursor_correct(void);
int pagescroll(int dir, long count, int half);
void do_check_cursorbind(void);
MapBlock* getMappingTableList(int state, int c);
MapBlock * getBufMappingTableList(int state, int c);
int isMappingTableValid(void);
int do_map(int maptype, CS arg, Unt mode, int abbrev);
void mapClearAllMappingsInMode(
   Book* book,      // book for local mappings
   int      modeClearFrom,      // mode in which to delete
   Boole      localOnly,      // true for buffer-local mappings
   Boole      abbr      // true for abbreviations
);
int mode_str2flags(CS modechars);
int map_to_exists(CS str, CS modechars, int abbr);
Boole map_to_exists_mode(CS rhs, int mode, int abbr);
CS set_context_in_map_cmd(
   Expand* xp,
   CS cmd,
   CS arg,
   Boole forceit,   // true if '!' given
   Boole isabbrev,   // true if abbreviation
   Boole isunmap,   // true if unmap/unabbrev command
   CommIndex   id
);
int expandMappings(
   CS pat,
   RegMatch* regmatch,
   OUT ExpandMatch* matches
);
Boole check_abbr(Unt c, CS ptr, int col, int mincol);
CS eval_map_expr(MapBlock   *mp, int c);
CS copyStr_escape_csi(CS p);
void eeUnescapeCsi(CS p);
int makemap(FILE* fd, NULLABLE Book* book);
int put_escstr(FILE* fd, CS strstart, int what);
void check_map_keycodes(void);
CS norCheckMapping(
   CS keys,
   int mode,
   int exact,      // require exact match
   int ign_mod,   // ignore preceding modifier
   int abbr,      // do abbreviations
   OUT MapBlock** mp_ptr,   // return: pointer to mapblock or NULL
   OUT int* local_ptr   // return: buffer-local mapping or NULL
);
void f_hasmapto(Var* argvars, Var* returnVar);
void f_maplist(Arr(Var) argvars, Var* returnVar);
void f_maparg(Var* argvars, Var* returnVar);
void f_mapcheck(Var *argvars, Var* returnVar);
void f_mapset(Var *argvars, Var* returnVar UNUSED);
void add_map(CS map, int mode, int nore);
int langmap_adjust_mb(int c);
void langmap_init(void);
CS setLangmap(OptionChange* args);
void c_abbreviate(Invocation* invo);
void c_map(Invocation* invo);
void c_unmap(Invocation* invo);
void c_mapclear(Invocation* invo);
void c_abclear(Invocation* invo);
void copyFoldingState(Portal* wp_from, Portal* wp_to);
int hasAnyFolding(Portal* po);
Boole getFolds(LineNr lnum, OUT LineNr *firstp, OUT LineNr *lastp);
Boole getFoldsPortal(
   Portal* po,
   LineNr lnum,
   LineNr* firstp,
   LineNr* lastp,
   int      cache,      // when true: use cached values of portal
   OUT FoldInfo* infop      // where to store fold info
);
int lineFolded(Portal *po, LineNr lnum);
long foldedCount(Portal* po, LineNr lnum, OUT FoldInfo* infop);
void closeFold(LineNr lnum, Long count);
void opFoldRange(
   LineNr first,
   LineNr last,
   Boole opening,   // true to open, false to close
   Boole recurse,   // true to do it recursively
   Boole had_visual   // true when Visual selection used
);
void openFold(LineNr lnum, Long count);
void foldOpenCursor(void);
void newFoldLevel(void);
void foldCheckClose(void);
int foldManualAllowed(int create);
void foldCreate(LineNr start, LineNr end);
void deleteFold(LineNr start, LineNr end, int recursive, int had_visual);
void clearFolding(Portal* po);
void foldUpdate(Portal* po, LineNr top, LineNr bot);
void foldUpdateAll(Portal* po);
void normInitFoldForPortal(Portal* newPort);
int find_wl_entry(Portal* po, LineNr lnum);
void foldAdjustVisual(void);
void cloneFoldArrayList(ArrayList* from, ArrayList* to);
void deleteFoldRecurse(ArrayList *gap);
void foldMarkAdjust(
    Portal* po,
    LineNr line1,
    LineNr line2,
    long amount,
    long amount_after
);
CS get_foldtext(
   Portal* po,
   LineNr lnum,
   LineNr lnume,
   FoldInfo* foldinfo,
   CS buffer
);
void foldMoveRange(ArrayList* gap, LineNr line1, LineNr line2, LineNr dest);
int put_folds(FILE* fd, Portal* po);
void f_foldclosed(Var *argvars, Var* returnVar);
void f_foldclosedend(Var *argvars, Var* returnVar);
void f_foldlevel(Var *argvars UNUSED, Var* returnVar);
void f_foldtext(Var *argvars UNUSED, Var* returnVar);
void f_foldtextresult(Var *argvars UNUSED, Var* returnVar);
