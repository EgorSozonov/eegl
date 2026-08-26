void drawInit();
Decoration getPortcolorDeco(Portal* po);
int screen_get_current_line_off(void);
void resetActiveDeco(void);
void screen_line(
   int row,
   int coloff,
   int endcol,
   int clear_width,
   ColNr last_vcol,
   Unt flags
);
int stl_connected(Portal* po);
int drawGetKeymapStr(Portal* po, OUT Text buf);
void screen_putchar(int c, Unt row, Unt col, char decoFlags);
void screen_getbytes(int row, int col, Byte* bytes, OUT Byte* decoFlags);
void drawText(CS text, Unt row, Unt col, Byte decoFlags);
void drawTextLen(
   CS text,
   int textlen,
   Unt row,
   int col,
   Byte decoFlagsArg
);
void end_search_hl(void);
void drawStopHilite(void);
void reset_cterm_colors(void);
void screen_draw_rectangle(int row, int col, int height, int width, Boole invert);
void fillRowsWithTwoChars(
   Unt start_row,
   Unt end_row,
   Unt start_col,
   Unt end_col,
   Unt c1,
   Unt c2,
   Decoration deco
);
void check_for_delay(int check_msg_scroll);
Boole screen_valid(Boole doclear);
void screenalloc(Boole doclear);
void free_screenlines(void);
int screenclear(void);
void drawLineWasClobbered(int screen_lnum);
int can_clear(CS p);
void screen_start(void);
void windgoto(int row, int col);
void setcursor(void);
void setcursor_mayforce(int force);
int insertLinesIntoPortal(
   Portal   *po,
   int      row,
   int      line_count,
   int      invalid,
   int      mayclear
);
int deleteLinesFromPortal(
   Portal   *po,
   int      row,
   int      line_count,
   int      invalid,
   int      mayclear,
   Unt      clearHiId       // for clearing lines
);
int drawInsertLines(
   int off,
   int row,
   int line_count,
   int end,
   int clearHiId,
   Portal* po       // NULL or portal to use width from
);
int screen_del_lines(
   int      off,
   int      row,
   int      line_count,
   int      end,
   int      force,      // even when line_count > p_ttyscroll
   Int      clearHiId,   // used for clearing lines
   Portal* po      // NULL or portal to use width from
);
int skip_showmode(void);
int showmode(void);
void unshowmode(int force);
void clearmode(void);
void drawGetTranslatedBookName(Book* book);
Unt statusLineNextChar(OUT Decoration* deco, Portal* po);
int redrawing(void);
int messaging(void);
void computeColumnsForRulerAndCommand(void);
int number_width(Portal* po);
int screen_screencol(void);
int screen_screenrow(void);
CS set_chars_option(CS newVal, Boole is_listchars, OUT ErrBuilder* errb);
CS drawSetFillChars(CS newVal, OUT ErrBuilder* errb);
CS drawSetListChars(CS newVal, OUT ErrBuilder* errb);
CS get_fillchars_name(Expand *xp UNUSED, int idx);
CS get_listchars_name(Expand *xp UNUSED, int idx);
CS check_chars_options(CS newVal);
int drawUpdateScreen(Unt type_arg);
void redrawPortalStatusLine(Portal* po, Boole ignore_pum);
void showruler(int always);
void after_updating_screen(int may_resize_shell UNUSED);
void drawUpdateCurBook(Unt type);
int redraw_asap(int type);
void redraw_after_callback(int call_drawUpdateScreen, int do_message);
void redraw_later(int type);
void redrawPortLater(Portal* po, Unt type);
void redraw_later_clear(void);
void redraw_all_later(Unt type);
void redraw_all_portals_later(int type);
void drawSetMustRedraw(Unt type);
void drawCurBookLater(int type);
void drawBookLater(Book* book, int type);
void drawBookLineLater(Book* book, LineNr lnum);
void drawBookAndStatusLater(Book* book, int type);
void status_redraw_all(void);
void drawAllStatusLinesOfCurBookLater(void);
void redraw_statuslines(void);
void redrawAllStatusLinesInFrame(Frame *fr);
void drawPortLineLater(Portal* po, LineNr lnum);
void redrawPortRangeLater(Portal* po, LineNr first, LineNr last);
int text_prop_position(
   Portal* po,
   TextProp* t,
   int vcol,       // current text column
   int scr_col,       // current screen column
   int* countExtraBytes,       // nr of bytes for virtual text
   Byte** extraBytes,       // virtual text
   OUT int* numDecoCells,       // decoration cells, NULL if not used
   int* toSkipBeforeDeco,   // cells to skip deco, NULL if not used
   int do_skip       // skip_cells is not zero
);
int drawLineOnScreen(
   Portal* port,
   LineNr lnum,
   int startrow,
   int endrow,
   int drawingOnlyNumberCol
);
void f_screenattr(Arr(Var) argvars, Var* returnVar);
void f_screenchar(Arr(Var) argvars, Var* returnVar);
void f_screenchars(Arr(Var) argvars, Var* returnVar);
void f_screencol(Arr(Var) argvars UNUSED, Var* returnVar);
void f_screenrow(Arr(Var) argvars UNUSED, Var* returnVar);
void f_screenstring(Arr(Var) argvars, Var* returnVar);
void drawMsgScrollUp(void);
void drawMsgSetCharAtOffset(Unt c, int off, ScreenCell cell);
Byte drawGetLineWrap(int row);
ColNr drawGetScreenCol(int offset);
ColNr drawGetOffset(int row);
Boole drawHasLines();
Byte drawGetLine(int offset);
CS drawGetLinesWithOffset(Unt row);
Unt drawGetScreenComposingChar(int offset, Unt composeInd);
Unt drawGetScreenUnicodeChar(int offset);
