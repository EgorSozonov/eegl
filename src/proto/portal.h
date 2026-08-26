void portalLayout_lock(void);
void portalLayout_unlock(void);
Boole portalLayout_locked(CommIndex cmd);
Boole portCheckCanSetCurBookDisabled(void);
Boole portCheckCanSetCurBookForceIt(Boole forceit);
Portal* prevPor_curPor(void);
Portal* switchBufGotoPortalIntoBuf(Book* book);
void doPortal(int nchar, long prenum, Unt xchar);
void getPortCommAddressType(CS arg, Invocation* invo);
int splitPortal(int size, Unt flags);
int splitPortal_ins(
   int size,
   Unt flags,
   Portal* newPort,
   int dir,
   Frame* to_flatten
);
int portalIsValid(Portal* port);
Portal * portFindById(int id);
int doesPortalExistInAnyTab(Portal* port);
Unt portCount(void);
Unt portMakePortals(Unt count, Boole vertical);
int splitPortalmove(Portal* po, int size, Unt flags);
void portMoveAfter(Portal* port0, Portal* port1);
void portEqualizeHeight(
   Portal* next_curPor,   // pointer to current portal to be or NULL
   int current,   // do only frame with current portal
   Byte dir   // EAD_* constants, or 0 for using p_ead
);
void leavingPortal(Portal* port);
void enteringPortal(Portal* port);
void curPor_init(void);
void closePortalsInto(Book* book, Boole keep_curPor) ;
int lastPortal(void);
int onePortal(void);
Unt closePortal(Portal* port, int free_buf);
void trigger_tabclosedpre(Tab* t, int directly);
void portSnapshotScrollSizes(void);
void may_make_initial_scroll_size_snapshot(void);
void may_trigger_win_scrolled_resized(void);
void closePortal_othertab(Portal* port, int free_buf, Tab *t);
void portFreeAll(void);
Portal * portRemoveFrame(
   Portal* port,
   OUT Byte* dirp,  // set to EAD_VERTICAL or EAD_HORIZONTAL for direction if @equalalways
   Tab* t,      // tab "port" is in, NULL for current
   OUT Frame** unflat_altfr // if not NULL, set to pointer of frame that got the space, and it 
                            // is not flattened
);
void portCloseOthers(Boole message);
void unloadTab(Tab *t);
void loadTab(Tab* t);
int portAllocFirst(void);
Portal * portAllocPopup(void);
void initPopupPortal(Portal* po, Book* book);
void portalInitSize(void);
void freeTab(Tab *t);
int portNewTab(int after);
int portMakeTabs(Unt maxcount);
int isTabValid(Tab *tpc);
int areTabAndPortalValid(Tab *tpc);
void closeTab(Tab *tab);
Tab * getTab(int n);
Unt indexOfTab(Tab* needle);
void gotoTabById(int n);
void gotoTab(Tab* t, Boole trigger_enter_autocmds, Boole trigger_leave_autocmds);
int goto_tabpage_lastused(void);
void goto_tab_port(Tab *t, Portal* po);
void moveTab(Unt nr);
void gotoPortal(Portal* po);
void enterPortal(Portal* po, int undo_sync);
void portFixCurrentDir(void);
Portal * portTryFindOpenBook(Book* book);
Portal * buf_jump_open_tab(Book* book);
int portUnlisted(Portal* po);
void portFreePopup(Portal* port);
void removePortal(Portal* port, Tab* t);
void allocLinesPortal(Portal* po);
void freePortalLsizes(Portal* po);
void shell_new_rows(void);
void shell_new_columns(void);
void portalSaveSizes(OUT ArrayList *gap);
void portRestoreSize(ArrayList *gap);
void computePosPortal(void);
void portEnsureSize(void);
void portSetHeight(int height, Portal* port);
void portSetWidth(int width, Portal* po);
void portDragStatusLine(Portal* dragwin, int offset);
void portDragVsepLine(Portal *dragwin, int offset);
void set_fraction(Portal* po);
void check_cursor_lnum(void);
void check_cursor_col(void);
int set_leftcol(ColNr leftcol);
void check_cursor_col_win(Portal* po);
void check_cursor(void);
void check_visual_pos(void);
void adjust_cursor_col(void);
int plines(LineNr lnum);
int plines_win(
   Portal   *wp,
   LineNr   lnum,
   int      limit_winheight)   // when true limit to portal height
;
int plines_nofill(LineNr lnum);
int plines_win_nofill(
    Portal   *wp,
    LineNr   lnum,
    int      limit_winheight)   // when true limit to portal height
;
int plines_win_nofold(Portal *wp, LineNr lnum);
int plines_m_win(Portal *wp, LineNr first, LineNr last, int max);
void scroll_to_fraction(Portal* po, int prevHeight);
void portComputeScroll(Portal* po);
void command_height(void);
void last_status();
int last_stl_height(int morePorts);
int min_rows(void);
Unt minRowsForAllTabs(void);
Boole onlyOnePortal(void);
void check_lnums(int do_curPor);
void check_lnums_nested(int do_curPor);
void reset_lnums(void);
int make_snapshot(int idx);
void restore_snapshot(
   int      idx,
   int      close_curPor)       // closing current portal
;
int getLastPortId(void);
int portalLocked(Portal* po);
Portal * getPortalById(int id);
Portal * getPortAndTab(int id, OUT Tab** result);
Portal * portFindByNr(Var* vp, Tab* t);
Portal * portFindByNrOrId(Var *vp);
Portal * find_tabwin(
    Var* needle,   // VAR_UNKNOWN for current portal
    Var* tvp,   // VAR_UNKNOWN for current tab
    Tab** ptp
);
void f_gettabinfo(Arr(Var) argvars, Var* returnVar);
void f_getwininfo(Arr(Var) argvars, Var* returnVar);
void f_getwinpos(Arr(Var) argvars UNUSED, Var* returnVar);
void f_getwinposx(Arr(Var) argvars UNUSED, Var* returnVar);
void f_getwinposy(Arr(Var) argvars UNUSED, OUT Var* returnVar);
void f_tabpagenr(Arr(Var) argvars UNUSED, Var* returnVar);
void f_tabpagewinnr(Arr(Var) argvars UNUSED, Var* returnVar);
void f_win_execute(Arr(Var) argvars, Var* returnVar);
void f_win_findbuf(Arr(Var) argvars, Var* returnVar);
void f_win_getid(Arr(Var) argvars, Var* returnVar);
void f_gotoPortalid(Arr(Var) argvars, Var* returnVar);
void f_win_id2tabwin(Arr(Var) argvars, Var* returnVar);
void f_win_id2win(Arr(Var) argvars, Var* returnVar);
void f_win_move_separator(Arr(Var) argvars, Var* returnVar);
void f_win_move_statusline(Arr(Var) argvars, Var* returnVar);
void f_win_screenpos(Arr(Var) argvars, Var* returnVar);
void f_splitPortalmove(Arr(Var) argvars, Var* returnVar);
void f_win_gettype(Arr(Var) argvars, Var* returnVar);
void f_getcmdwintype(Arr(Var) argvars UNUSED, Var* returnVar);
void f_winbufnr(Arr(Var) argvars, Var* returnVar);
void f_wincol(Arr(Var) argvars UNUSED, Var* returnVar);
void f_winheight(Arr(Var) argvars, Var* returnVar);
void f_winlayout(Arr(Var) argvars, Var* returnVar);
void f_winline(Arr(Var) argvars UNUSED, Var* returnVar);
void f_winnr(Arr(Var) argvars UNUSED, Var* returnVar);
void f_winrestcmd(Arr(Var) argvars UNUSED, Var* returnVar);
void f_winrestview(Arr(Var) argvars, Var* returnVar UNUSED);
void f_winsaveview(Arr(Var) argvars UNUSED, Var* returnVar);
void f_winwidth(Arr(Var) argvars, Var* returnVar);
int portSwitch(SwitchPort *switchPort, Portal* port, Tab* t, int no_display);
int portSwitchNoblock(
   SwitchPort *switchPort,
   Portal       *port,
   Tab   *t,
   int       no_display)
;
void portRestore(SwitchPort* switchPort, int no_display);
void portRestoreNoblock(
   SwitchPort* switchPort,
   int       no_display)
;
int popup_on_border(Portal* po, int row, int col);
int popup_close_if_on_X(Portal* po, int row, int col);
void popup_start_drag(Portal* po, int row, int col);
void popup_drag(Portal* po);
void popup_set_firstline(Portal* po);
int popup_is_in_scrollbar(Portal* po, int row, int col);
void popup_handle_scrollbar_click(Portal* po, int row, int col);
int popup_top_extra(Portal* po);
int popup_left_extra(Portal* po);
int popup_height(Portal* po);
int popup_width(Portal* po);
int popup_extra_width(Portal* po);
int parse_previewpopup(Portal* po);
int parse_completepopup(Portal* po);
void popup_set_wantpos_cursor(Portal* po, int width, Bag *d);
void popup_set_wantpos_rowcol(Portal* po, int row, int col);
void popup_redraw_all(void);
Portal* createPopup(Arr(Var) argvars, OUT Var* returnVar, PopupKind kind);
void f_popup_clear(Arr(Var) argvars, Var* returnVar UNUSED);
void f_createPopup(Arr(Var) argvars, OUT Var* returnVar);
void f_popup_atcursor(Arr(Var) argvars, OUT Var* returnVar);
void f_popup_beval(Arr(Var) argvars, OUT Var* returnVar);
void popup_close_with_retval(Portal* po, int retval);
void popup_close_for_mouse_click(Portal* po);
void popup_handle_mouse_moved(void);
void f_popup_filter_menu(Arr(Var) argvars, Var* returnVar);
void f_popup_filter_yesno(Arr(Var) argvars, Var* returnVar);
void f_popup_dialog(Arr(Var) argvars, Var* returnVar);
void f_popup_menu(Arr(Var) argvars, Var* returnVar);
void f_popup_notification(Arr(Var) argvars, Var* returnVar);
void f_popup_close(Arr(Var) argvars, Var* returnVar UNUSED);
void popup_hide(Portal* po);
void f_popup_hide(Arr(Var) argvars, Var* returnVar UNUSED);
void popup_show(Portal* po);
void f_popup_show(Arr(Var) argvars, Var* returnVar UNUSED);
void f_popup_settext(Arr(Var) argvars, Var* returnVar UNUSED);
void f_popup_setbuf(Arr(Var) argvars, Var* returnVar UNUSED);
Boole portErrorIfPopup(Boole also_with_term);
int popup_close(int id, int force);
int popupCloseTab(Tab* tab, int id, int force);
void close_all_popups(int force);
void f_popup_move(Arr(Var) argvars, Var* returnVar UNUSED);
void f_popup_setoptions(Arr(Var) argvars, Var* returnVar UNUSED);
void f_popup_getpos(Arr(Var) argvars, Var* returnVar);
void f_popup_list(Arr(Var) argvars UNUSED, OUT Var* returnVar);
void f_popup_locate(Arr(Var) argvars, Var* returnVar);
void f_popup_getoptions(Arr(Var) argvars, OUT Var* returnVar);
Boole portErrorIfTermPopup(void);
void popup_reset_handled(int handled_flag);
Portal * find_next_popup(int lowest, int handled_flag);
int popup_do_filter(Unt c);
int popup_no_mapping(void);
void popup_check_cursor_pos(void);
void may_update_popup_mask(int type);
void may_update_popup_position(void);
void update_popups(void (*portUpdate)(Portal* po));
int set_ref_in_popups(int copyID);
int portalIsPopup(Portal* po);
Portal * popupFindPreviewPortal(void);
Portal * popupFindInfoPortal(void);
void f_popup_findecho(Arr(Var) argvars UNUSED, OUT Var* returnVar);
void f_popup_findinfo(Arr(Var) argvars UNUSED, OUT Var* returnVar);
void f_popup_findpreview(Arr(Var) argvars UNUSED, OUT Var* returnVar);
int portalCreatePreviewPortal(int info);
void popup_close_preview(void);
void popup_close_info(void);
int popup_overlaps_cmdline(void);
Portal* popup_get_messagePort(void);
void popup_show_messagePort(void);
int popup_message_win_visible(void);
void popup_hide_messagePort(void);
void start_echowindow(int time_sec);
void end_echowindow(void);
void setPopupTitle(Portal* po);
void popup_update_preview_title(void);
void showNotification(CS text);
Boole canStartDrag(Portal* po, int row, int col);
Boole isInfoPopup(Portal* po);
void pum_display(PopupItem* array, Unt size, int selected);
void pum_callUpdateScreen(void);
int pum_under_menu(int row, int col, int only_redrawing);
void pum_redraw(void);
void pum_undisplay(void);
void pum_clear(void);
int pum_visible(void);
int pum_redraw_in_same_position(void);
void pum_may_redraw(void);
int pum_get_height(void);
void pum_set_event_info(Bag *bag);
Unt balloonSplitMessage(CS mesg, OUT Arr(PopupItem)* array);
void ui_remove_balloon(void);
void ui_post_balloon(CS mesg, List* list);
void ui_may_remove_balloon(void);
int find_word_under_cursor(
   int mouserow,
   int mousecol,
   int getword,
   Unt flags,   // flags for find_ident_at_pos()
   Portal** winp,   // may be NULL
   LineNr* lnump,   // may be NULL
   OUT CS* textp,
   int* colp,   // column where mouse hovers, can be NULL
   int* startcolp // column where text starts, can be NULL
);
int get_beval_info(
   BalloonEval* beval,
   int getword,
   Portal**winp,
   LineNr* lnump,
   OUT CS* textp,
   int* colp
);
void post_balloon(BalloonEval* beval UNUSED, CS mesg, List* list);
void general_beval_cb(BalloonEval* beval, int state UNUSED);
