#ifndef AVOID_DUPLICATION
#define HI_HAS_FG     1
#define HI_HAS_BG     2
#define HI_HAS_DECO   4
#define HI_IS_LINK    8
#define HI_HAS_UNDER 16
#endif

typedef enum {
#define HI(a, b, c, d, e, f) HILITE_##a,
HI(None, HI_HAS_FG|HI_HAS_BG, 7, 0, 0, 0)
HI(NonText, HI_HAS_FG, 4, 0, DECO_BOLD, 0)
HI(NormalFloat, HI_IS_LINK, 0, 0, 0, HILITE_None)
HI(Directories, HI_IS_LINK, 0, 0, 0, HILITE_None)        //directories in CTRL-D listing
HI(ErrorMsg, HI_HAS_FG|HI_HAS_BG, 7, 1, 0, 0)
HI(WarningMsg, HI_HAS_FG|HI_HAS_BG, 9, 7 + 232, 0, 0)    //warning messages
HI(MoreMsg, HI_IS_LINK, 0, 0, 0, HILITE_None)            //"--More--" message
HI(ModeName, HI_HAS_DECO, 0, 0, DECO_BOLD, 0)            //Mode (e.g., "-- INSERT --")
HI(CurrentLineNr, HI_IS_LINK, 0, 0, 0, HILITE_None)      //current line number
HI(CurrentSign, HI_IS_LINK, 0, 0, 0, HILITE_None)        //current line sign column
HI(CurrentFold, HI_IS_LINK, 0, 0, 0, HILITE_None)        //current line fold
HI(YesNoQuestions, HI_IS_LINK, 0, 0, 0, HILITE_None)     //return to continue message
                                                         //and yes/no questions
HI(StatusLine, HI_HAS_DECO, 0, 0, DECO_INVERSE, 0)       //status lines
HI(StatusLinesInactive, HI_HAS_DECO, 0, 0, DECO_INVERSE, 0) //status line of non-current portal
HI(VertSplit, HI_HAS_DECO, 0, 0, DECO_INVERSE, 0) //column to separate vertically split portals
HI(OutputOfAutocmd, HI_IS_LINK, 0, 0, 0, HILITE_None)    //Titles for output from
                                                         //";set all", ";autocmd" etc
HI(VisualMode, HI_HAS_DECO, 0, 0, DECO_BOLD, 0)          //Visual mode
HI(WildcardMenu, HI_IS_LINK, 0, 0, 0, HILITE_None)       //Wildmenu hilite
HI(FoldedLine, HI_IS_LINK, 0, 0, 0, HILITE_None)         //Folded line
HI(DiffTextAdd, HI_HAS_FG, 2, 0, 0, 0)                   //Added diff line
HI(DiffText, HI_HAS_FG, 4, 0, 0, 0)                      //Changed diff line
HI(DiffChangedTextInChanged, HI_IS_LINK, 0, 0, 0, HILITE_None) //Text Changed in changed diff
HI(DiffAddedTextInChanged, HI_IS_LINK, 0, 0, 0, HILITE_None) //Text Added in changed diff line
HI(DiffDeleted, HI_HAS_FG|HI_HAS_BG|HI_HAS_DECO, 4, 14, DECO_BOLD, 0) //HILITE_DiffDeleted
HI(SignColumn, HI_HAS_FG|HI_HAS_BG, 6, 232 + 10, 0, 0)   //Sign column
HI(Pmenu, HI_HAS_FG|HI_HAS_BG, 0, 4, 0, 0)               //popup menu normal item
HI(PmenuSelected, HI_HAS_BG, 0, 4 + 232, 0, 0)           //popup menu selected item
HI(PmenuMatchedText, HI_IS_LINK, 0, 0, 0, HILITE_None)   //popup menu matched text in normal item
HI(PmenuMatchedInSelected, HI_IS_LINK, 0, 0, 0, HILITE_None) //popup menu matched text in
                                                             //selected item
HI(PmenuNormalItem, HI_IS_LINK, 0, 0, 0, HILITE_None)    //popup menu normal item "kind"
HI(PmenuSelectedItem, HI_IS_LINK, 0, 0, 0, HILITE_None)  //popup menu selected item "kind"
HI(PmenuExtraText, HI_IS_LINK, 0, 0, 0, HILITE_None)     //popup menu normal item "menu"
                                                         //(extra text)
HI(PmenuSelectedExtraText, HI_IS_LINK, 0, 0, 0, HILITE_None) //popup menu selected item "menu"
                                                             //(extra text)
HI(PmenuScrollbar, HI_HAS_BG, 0, 12 + 232, 0, 0)         //popup menu scrollbar
HI(PmenuScrollBarThumb, HI_HAS_BG, 0, 7, 0, 0) //40 popup menu scrollbar thumb
HI(Tabpanel, HI_HAS_BG, 0, 4, 0, 0)                      //tabpanel
HI(TabpanelSelected, HI_IS_LINK, 0, 0, 0, HILITE_None)   //tabpanel selected
HI(TabpanelFill, HI_IS_LINK, 0, 0, 0, HILITE_None)       //tabpanel filler
HI(CursorLine, HI_HAS_BG, 0, 8 + 232, 0, 0)              //@cursorline
HI(LocationPortalSelected, HI_IS_LINK, 0, 0, 0, HILITE_PmenuSelectedItem) //location portal line
                                                                          //currently selected
HI(TerminalStatusLine, HI_IS_LINK, 0, 0, 0, HILITE_None)  //status lines of terminal portals
HI(TerminalNoncurrentStatusLine, HI_IS_LINK, 0, 0, 0, HILITE_None) //status lines of
                                                                    //not-current terminal
                                                                    //portals
HI(TerminalRed, HI_HAS_FG, 1, 0, 0, 0)       //status lines of not-current terminal portals
HI(TerminalGreen, HI_HAS_FG, 10, 0, 0, 0)    //status lines of not-current terminal portals
HI(TerminalBlue, HI_HAS_FG, 4, 0, 0, 0)      //status lines of not-current terminal portals
HI(MessageArea, HI_IS_LINK, 0, 0, 0, HILITE_None)        //message area
HI(MetaSpecialKeys, HI_IS_LINK, 0, 0, 0, HILITE_None)    //Meta & special keys listed with
                                                         //";map", text that is
                                                         //displayed different
HI(LineNr, HI_HAS_FG, 3, 0, 0, 0)             //line number for ";number" and ";#" commands
HI(LineNrAbove, HI_IS_LINK, 0, 0, 0, HILITE_LineNr)
HI(LineNrBelow, HI_IS_LINK, 0, 0, 0, HILITE_LineNr)
HI(Directory, HI_HAS_FG, 14, 0, 0, 0)
HI(CursorLineNr, HI_HAS_FG|HI_HAS_DECO, 3, 0, DECO_BOLD, 0)
HI(Question, HI_HAS_FG|HI_HAS_DECO, 10, 0, DECO_BOLD, 0)
HI(SpecialKey, HI_HAS_FG, 12, 0, 0, 0)
HI(Title, HI_HAS_FG|HI_HAS_DECO, 13, 0, DECO_BOLD, 0)
HI(InfoMsg, HI_HAS_FG|HI_HAS_BG, 10, 6 + 232, 0, 0)
HI(ComplMatchIns, HI_HAS_DECO, 0, 0, DECO_INVERSE, 0)
HI(WildMenu, HI_HAS_FG|HI_HAS_BG, 0, 3, 0, 0)
HI(Folded, HI_HAS_FG|HI_HAS_BG, 14, 2 + 232, 0, 0)
HI(Visual, HI_HAS_FG|HI_HAS_BG, 23 + 232, 16 + 232, 0, 0)
HI(ColorColumn, HI_HAS_BG, 0, 1, 0, 0)
HI(MatchParen, HI_HAS_FG, 6, 0, 0, 0)
HI(StatusLineTerm, HI_HAS_FG|HI_HAS_BG|HI_HAS_DECO, 2, 10, DECO_BOLD, 0)
HI(StatusLineTermNC, HI_HAS_FG|HI_HAS_BG, 5, 3, 0, 0)
HI(Search, HI_HAS_FG|HI_HAS_BG, 0, 11, 0, 0)
HI(CurSearch, HI_IS_LINK, 0, 0, 0, HILITE_Search)
HI(LocationLine, HI_IS_LINK, 0, 0, 0, HILITE_Search)
HI(LocationFileName, HI_IS_LINK, 0, 0, 0, HILITE_None)
HI(LocationSeparator, HI_IS_LINK, 0, 0, 0, HILITE_None)
HI(Comment, HI_HAS_FG, 2, 0, 0, 0)
HI(Constant, HI_HAS_FG, 44, 0, 0, 0)
HI(Special, HI_HAS_FG, 54, 0, 0, 0)
HI(Identifier, HI_HAS_FG, 55, 0, 0, 0)
HI(Statement, HI_HAS_FG, 52, 0, 0, 0)
HI(PreProc, HI_HAS_FG, 25, 0, 0, 0)
HI(Type, HI_HAS_FG, 35, 0, 0, 0)
HI(Keyword, HI_HAS_FG, 155, 0, 0, 0)
HI(Ignore, HI_HAS_FG, 7 + 232, 0, 0, 0)
HI(Added, HI_HAS_FG, 252, 0, 0, 0)
HI(Changed, HI_HAS_FG, 225, 0, 0, 0)
HI(Removed, HI_HAS_FG, 1, 0, 0, 0)
HI(Error, HI_HAS_BG, 0, 1, 0, 0)
HI(Todo, HI_HAS_FG|HI_HAS_BG, 4, 3, 0, 0)
HI(Bold, HI_HAS_DECO, 0, 0, DECO_BOLD, 0)
HI(Italic, HI_HAS_DECO, 0, 0, DECO_ITALIC, 0)
HILITE_NULL
#undef HI
} HiliteId;
void initHilite();
void doHilite(CS line, Boole forceit, Boole init);
Short hiliteGroupByName(Text name);
Decoration decosByHiliteName(Text name);
Boole hiliteExists(Text name);
CS syn_id2name(Unt id);
Byte decorationByHiliteId(Short hiId);
Byte syn_id2colors(Short hiId, OUT VTermColor* fgp, OUT VTermColor* bgp);
void setCompletionContextInHiliteCommand(OUT Expand* xp, CS arg);
Text getHiliteGroupName(Expand*, int id);
CS getHiliteGroupNameAsCString(Expand *xp, int id);
int expandHiliteGroup(
   CS pattern,
   Expand* xp,
   RegMatch* rmp,
   OUT ExpandMatch* matches
);
void f_hlget(Arr(Var) argvars, OUT Var* returnVar);
char getDecoFlags(Short hiId);
Decoration getFullDecoration(Short hiId);
Boole decoEq(Decoration a, Decoration b);
void c_echohl(Invocation* invo);
void f_hlID(Arr(Var) argvars, OUT Var* returnVar);
void f_hlexists(Arr(Var) argvars, OUT Var* returnVar);
void syntaxStartLine(Portal *wp, LineNr lnum);
void synFreeBlock(SyntaxBlock *block);
void syn_stack_apply_changes(Book* book);
void syntax_end_parsing(Portal *wp, LineNr lnum);
int syntax_check_changed(LineNr lnum);
Decoration syntGetDeco(
   ColNr col,
   int keep_state   //keep state of char at "col"
);
void syntax_clear(SyntaxBlock *block);
void reset_synblock(Portal* wp);
Short syntaxClusterByName(Text line);
void c_syntax(Invocation* invo);
void c_ownsyntax(Invocation* invo);
int syntax_present(Portal* po);
void reset_expand_highlight(void);
void set_context_in_echohl_cmd(Expand* xp, CS arg);
void set_context_in_syntax_cmd(Expand *xp, CS arg);
CS get_syntax_name(Expand* xp, int idx);
int syn_get_id(
   Portal* wp,
   Long lnum,
   ColNr col,
   int trans,      //remove transparency
   int keep_state  //keep state of char at "col"
);
int syn_get_stack_item(int i);
int syn_get_foldlevel(Portal *po, Long lnum);
void f_synIDtrans(Arr(Var) argvars, Var* returnVar);
