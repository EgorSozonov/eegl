int search_regcomp(
   Text pat,
   Arr(CS) used_pat,
   int pat_save,
   int pat_use,
   int options,
   OUT RegMultilineMatch* regmatch   // return: pattern and ignore-case flag
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
   Portal* port, // portal to search in; can be NULL for a buffer without a portal!
   Book* book,
   Pos* pos,
   OUT Pos* end_pos,   // set to end of the match, unless NULL
   Unt dir,    // forward or backward
   Text pat,
   long count,
   Unt options,
   int pat_use,   // which pattern to use when "pat" is empty
   SearchitArg* extra_arg   // optional extra arguments, can be NULL
);
void set_search_direction(int cdir);
int do_search(
   Operator* oap,   // can be NULL
   int dirc,   // '/' or '?'
   int search_delim, // the delimiter for the search, e.g. '%' in s%regex%replacement%
   Text pat,
   long count,
   int options,
   SearchitArg* sia   // optional arguments or NULL
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
   CS ptr,      // pointer to search pattern
   int      dir UNUSED,   // direction of expansion
   int      len,      // length of search pattern
   int      whole,      // match whole words only
   int      skip_comments,   // don't match inside comments
   int      type,      // Type of search; are we looking for a type? a macro?
   long   count,
   int      action,      // What to do when we find it
   LineNr   start_lnum,   // first line to start searching
   LineNr   end_lnum,   // last line for searching
   int      forceit,   // If true, always switch to the found path
   int      silent      // Do not print messages when ACTION_EXPAND
);
SearchPattern * getPrevSearchPattern(int idx);
int getPrevSearchOrSubstPattern(void);
void f_searchcount(Var *argvars, Var* returnVar);
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
void f_clearmatches(Var* argvars, Var* returnVar UNUSED);
void f_getmatches(Var *argvars, Var* returnVar UNUSED);
void f_setmatches(Var *argvars, Var* returnVar);
void f_matchadd(Var *argvars, Var* returnVar);
void f_matchaddpos(Var *argvars, Var* returnVar);
void f_matcharg(Var* argvars, Var* returnVar);
void f_matchdelete(Var *argvars UNUSED, Var* returnVar UNUSED);
void c_match(Invocation* invo);
void c_help(Invocation* invo);
void c_helpclose(Invocation* invo UNUSED);
CS check_help_lang(CS arg);
int help_heuristic(
   CS matched_string,
   int offset,         // offset for match
   int wrong_case      // no matching case
);
int find_help_tags(
   CS arg,
   int keep_lang,
   OUT ExpandMatch* matches
);
void cleanup_help_tags(OUT ExpandMatch* matches);
void prepare_help_buffer(void);
void searchFixHelpBook(void);
void c_exusage(Invocation* invo UNUSED);
void c_usage(Invocation* invo UNUSED);
void c_helptags(Invocation* invo);
