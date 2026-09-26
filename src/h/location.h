int llInitFromFile(
   OUT LocationStack* st,
   CS errorFName,
   NULLABLE CS errorformat,
   Boole newlist,      //true: start a new error list
   CS title
);
int qf_stack_get_bufnr(void);
void check_qfBusynessG(void);
void llInitStacksOnce(void);
LocationStack* getLocationStack(int ind);
void llJump(LocationStack* stack, Unt dir, int errornr, Boole forceit);
void c_list(Invocation* invo);
void c_llAge(Invocation* invo);
void qf_history(Invocation* invo);
void llViewLocation(int split);
void c_cPortal(Invocation* invo);
void c_lClose(Invocation* invo);
void c_lOpen(Invocation* invo);
void c_lBottom(Invocation* invo);
LineNr llCurrentEntry(Portal* po);
CS setQuickfixtextfunc(OptionChange* cha);
int grepIsActuallyInternal(CommIndex id);
void c_elgrep(Invocation* invo);
void c_grep(Invocation* invo);
void initInProgressLl();
void c_make(Invocation*);
int llGetSize(Invocation* invo);
int llGetValidSize(Invocation* invo);
int llGetCurrIndex(Invocation* invo);
int llGetCurrValidIndex(Invocation* invo);
void c_lMove(Invocation* invo);
void c_lNext(Invocation* invo);
void c_lBelow(Invocation* invo);
void c_lFile(Invocation* invo);
void c_vimgrep(Invocation* invo);
int setLocationList(
   OUT LocationStack* stack,
   List* newContent,
   LocListAction action,
   CS title,
   Bag* specific
);
Boole llSetRef(int copyId);
void c_lbook(Invocation* invo);
CS cexpr_get_auname(CommIndex id);
int trigger_cexpr_autocmd(int id);
int cexpr_core(Invocation* invo, Var *tv);
void c_lExpr(Invocation* invo);
void c_helpgrep(Invocation* invo);
void free_quickfix(void);
void f_getloclist(Arr(Var) argvars, OUT Var* returnVar);
void f_setloclist(Var* argvars, Var* returnVar);
int setmark(int c);
int setmark_pos(int c, Pos *pos, int fnum);
void mark_forget_file(Portal *wp, int fnum);
void setpcmark(void);
void checkpcmark(void);
Pos * movemark(int count);
Pos * movechangelist(int count);
Pos * markGetBook(Book* book, int c, int changefile);
Pos * getmark(int c, int changefile);
Pos * markGetBookFnum(Book* book, int c, int changefile, int* fnum);
Pos * getnextmark(Pos* startpos, Unt dir, int begin_line);
void fmarks_check_names(Book* book);
int check_mark(Pos* pos);
void clrallmarks(Book* book);
CS fm_getname(FileMark* fmark, int lead_len);
void c_marks(Invocation *invo);
void c_delmarks(Invocation* invo);
void c_jumps(Invocation*);
void c_clearjumps(Invocation*);
void c_changes(Invocation*);
void markAdjust(
   LineNr line1,
   LineNr line2,
   long amount,
   long amount_after,
   Boole adjust_folds
);
void mark_col_adjust(
   LineNr lnum,
   ColNr mincol,
   long lnum_amount,
   long col_amount,
   int spaces_removed
);
void cleanup_jumplist(Portal* wp, int loadfiles);
void copy_jumplist(Portal* from, Portal* to);
void free_jumplist(Portal *wp);
void set_last_cursor(Portal *port);
void free_all_marks(void);
FileMarkExt * get_namedfm(void);
void f_getmarklist(Arr(Var) argvars, Var* returnVar);
void init_signs(void);
int markGetSignDecorations(Portal *wp, LineNr lnum, OUT SignHilite* signHilites);
void llDeleteSigns(Book* book, CS group);
int sign_define_by_name(
   CS name,
   CS linehl,
   CS textt,
   CS texthl,
   CS culhl,
   CS numhl,
   int prio
);
int sign_exists_by_name(CS name);
int sign_undefine_by_name(CS name, Boole give_error);
int sign_place(
   int *sign_id,
   CS sign_group,
   CS sign_name,
   Book* book,
   LineNr lnum,
   int prio
);
void c_sign(Invocation* invo);
void llGetBookSigns(Book *book, List *l);
void free_signs(void);
CS get_sign_name(Expand *, int idx);
void set_context_in_sign_cmd(Expand* xp, CS arg);
void f_sign_define(Arr(Var) argvars, Var* returnVar);
void f_sign_getdefined(Arr(Var) argvars, Var* returnVar);
void f_sign_getplaced(Arr(Var) argvars, Var* returnVar);
void f_sign_jump(Arr(Var) argvars, Var* returnVar);
void f_sign_place(Arr(Var) argvars, Var* returnVar);
void f_sign_placelist(Arr(Var) argvars, Var* returnVar);
void f_sign_undefine(Arr(Var) argvars, Var* returnVar);
Boole isSigncolumnOn(Portal* po);
void f_sign_unplace(Arr(Var) argvars, Var* returnVar);
void f_sign_unplacelist(Arr(Var) argvars, Var* returnVar);
int search_regcomp(
   Text pat,
   Arr(CS) used_pat,
   int pat_save,
   int pat_use,
   int options,
   OUT RegMultilineMatch* regmatch   //return: pattern and ignore-case flag
);
CS get_search_pat(void);
void save_re_pat(int idx, Text pat, int magic);
void save_search_patterns(void);
void restore_search_patterns(void);
void free_search_patterns(void);
void save_last_search_pattern(void);
void restore_last_search_pattern(void);
Text last_search_pattern(void);
int ignorecase(CS pat);
int ignorecase_opt(CS pat, int ic_in, int scs);
int pat_has_uppercase(CS pat);
CS last_csearch(void);
int last_csearch_forward(void);
int last_csearch_until(void);
void set_last_csearch(int c, CS s, int len);
void set_csearch_direction(int cdir);
void set_csearch_until(int t_cmd);
Text last_search_pat(void);
void reset_search_dir(void);
void set_last_search_pat(
   CS s,
   int idx,
   int magic,
   int setlast
);
void last_pat_prog(RegMultilineMatch* regmatch);
int searchit(
   Portal* port, //portal to search in; can be NULL for a buffer without a portal!
   Book* book,
   Pos* pos,
   OUT Pos* end_pos,   //set to end of the match, unless NULL
   Unt dir,    //forward or backward
   Text pat,
   long count,
   Unt options,
   int pat_use,   //which pattern to use when "pat" is empty
   SearchitArg* extra_arg   //optional extra arguments, can be NULL
);
void set_search_direction(int cdir);
int do_search(
   Operator* oap,   //can be NULL
   int dirc,   //'/' or '?'
   int search_delim, //the delimiter for the search, e.g. '%' in s%regex%replacement%
   Text pat,
   long count,
   int options,
   SearchitArg* sia   //optional arguments or NULL
);
int search_for_exact_line(
   Book* book,
   Pos* pos,
   int dir,
   CS pat
);
int searchc(ActionArg* cap, int t_cmd);
Pos* findmatch(Operator *oap, int initc);
Pos* findmatchlimit(
   Operator* oap,
   Unt initc,
   int flags,
   int maxtravel
);
int check_linecomment(CS line);
int current_search(long   count, Boole forward);
int linewhite(LineNr lnum);
void find_pattern_in_path(
   CS ptr,      //pointer to search pattern
   Unt dir,   //direction of expansion
   int len,      //length of search pattern
   int whole,      //match whole words only
   int skip_comments,   //don't match inside comments
   int type,      //Type of search; are we looking for a type? a macro?
   Long count,
   int action,      //What to do when we find it
   LineNr start_lnum,   //first line to start searching
   LineNr end_lnum,   //last line for searching
   int forceit,   //If true, always switch to the found path
   int silent      //Do not print messages when ACTION_EXPAND
);
SearchPattern * getPrevSearchPattern(int idx);
int getPrevSearchOrSubstPattern(void);
void f_searchcount(Arr(Var) argvars, Var* returnVar);
void clear_matches(Portal* po);
void searchInitHilite(Portal* po, Match* search_hl);
void prepare_search_hl(Portal* po, Match* search_hl, LineNr lnum);
Boole searchPrepareHiliteLine(
   Portal* po,
   LineNr lnum,
   ColNr mincol,
   OUT CS* line,
   Match* search_hl,
   OUT Short* searchHiId
);
Short update_search_hl(
   Portal* po,
   LineNr lnum,
   ColNr col,
   OUT CS* line,
   Match* search_hl,
   int didLineDecorations,
   int lcs_eol_one,
   OUT Boole* onLastCol
);
int get_prevcol_hl_flag(Portal* po, Match* search_hl, long curcol);
void get_search_match_hl(Portal* po, Match* search_hl, long col, OUT Short* charHiId);
void f_clearmatches(Var* argvars, Var*);
void f_getmatches(Arr(Var) argvars, Var* returnVar);
void f_setmatches(Arr(Var) argvars, Var* returnVar);
void f_matchadd(Arr(Var) argvars, Var* returnVar);
void f_matchaddpos(Arr(Var) argvars, Var* returnVar);
void f_matcharg(Var* argvars, Var* returnVar);
void f_matchdelete(Arr(Var) argvars, Var* returnVar);
void c_match(Invocation* invo);
void c_help(Invocation* invo);
void c_helpclose(Invocation*);
CS check_help_lang(CS arg);
int help_heuristic(
   CS matched_string,
   int offset,         //offset for match
   int wrong_case      //no matching case
);
int find_help_tags(
   CS arg,
   int keep_lang,
   OUT ExpandMatch* matches
);
void cleanup_help_tags(OUT ExpandMatch* matches);
void prepare_help_buffer(void);
void searchFixHelpBook(void);
void c_exusage(Invocation*);
void c_usage(Invocation*);
void c_helptags(Invocation* invo);
