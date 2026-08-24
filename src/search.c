//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov
 
//## search.c: code for normal mode searching commands and hiliting matches

#include "eegl.h"

private typedef struct searchstat {
   int cur;        // current position of found words
   int cnt;        // total count of found words
   int exact_match;// true if matched exactly on specified position
   int incomplete; // 0: search was fully completed
                   // 1: recomputing was timed out
                   // 2: max count exceeded
   int last_maxcount;  // the max count of the last search
} SearchFileStat;

//{{{forward declarations

private void set_vv_searchforward(void);
private int first_submatch(RegMultilineMatch* rp);
private CS get_line_and_copy(LineNr lnum, CS buf);
private void show_pat_in_path(CS, int, int, int, FILE *, LineNr *, long);
private void save_incsearch_state(void);
private void restore_incsearch_state(void);
private int check_prevcol(CS linep, int col, int ch, int *prevcol);
private int find_rawstring_end(CS linep, Pos *startpos, Pos *endpos);
private void find_mps_values(OUT Unt* initc, OUT Unt* findc, OUT int *backwards, int switchit);
private int is_zero_width(Text pattern, Boole move, Pos* cur, Unt direction);
private void cmdline_search_stat(
   int dirc, Pos *pos, Pos *cursor_pos, int show_top_bot_msg, CS msgbuf, Unt msgbuflen, 
   int recompute, int maxcount, long timeout
);
private void update_search_stat(
   int dirc, Pos *pos, Pos *cursor_pos, SearchFileStat *stat, int recompute, int maxcount, 
   long timeout
);

//}}}
//{{{searches

#define SEARCH_STAT_DEF_TIMEOUT 40L
// 'W ':  2 +
// '[>9999/>9999]': 13 + 1 (ZERO)
#define SEARCH_STAT_BUF_LEN 16

//This file contains various searching-related routines. These fall into 3 groups:
//0. string searches (for /, ?, n, and N)
//1. character searches within a single line (for f, F, t, T, etc)
//2. "other" kinds of searches like the '%' command, and 'word' searches.

//String searches
//
//The string search functions are divided into two levels:
//lowest:  searchit(); uses an Pos for starting position and found match.
//Highest: do_search(); uses curPor->cursor; calls searchit().
//
//The last search pattern is remembered for repeating the same search.
//This pattern is shared between the :g, :s, ? and / commands.
//This is in search_regcomp().
//
//The actual string matching is done using a heavily modified version of
//Henry Spencer's regular expression library.  See regexp.c.

//Two search patterns are remembered: One for the :substitute command and
//one for other searches.  last_idx points to the one that was used the last time.
private SearchPattern prevSearchPatternsP[2] = {
    {(Text){NULL, 0}, true, false, {'/', 0, 0, 0L}},   // last used search pat
    {(Text){NULL, 0}, true, false, {'/', 0, 0, 0L}}   // last used substitute pat
};

// copy of prevSearchPatternsP[], for keeping the search patterns while executing autocmds
private SearchPattern saved_spats[2];

private int last_idx = 0;   // index in prevSearchPatternsP[] for RE_LAST

private Byte lastc[2] = {ZERO, ZERO};   // last character searched for
private int lastcdir = FORWARD;      // last direction of character search
private int last_t_cmd = true;      // last search t_cmd
private Byte lastc_bytes[MB_MAXBYTES + 1];
private int lastc_bytelen = 1;   // >1 for multi-byte char

private Text mrPatternSaved = (Text){NULL, 0};
private int saved_spats_last_idx = 0;
private Boole saved_spatsHlsearch = true;

// allocated copy of pattern used by search_regcomp()
private Text mrPatternP = (Text){.c = null, .len = 0};

// Type used by find_pattern_in_path() to remember which included files have been searched already
private typedef struct {
   FILE* fp;     //File pointer
   CS name;      //Full name of file
   LineNr lnum;  //Line we were up to in file
   int matched;  //Found a match in this file
} SearchedFile;

//translate search pattern for compileRegexp()
//pat_save == RE_SEARCH: save pat in prevSearchPatternsP[RE_SEARCH].pat (normal search cmd)
//pat_save == RE_SUBST: save pat in prevSearchPatternsP[RE_SUBST].pat (:substitute command)
//pat_save == RE_BOTH: save pat in both patterns (:global command)
//pat_use  == RE_SEARCH: use previous search pattern if "pat" is NULL
//pat_use  == RE_SUBST: use previous substitute pattern if "pat" is NULL
//pat_use  == RE_LAST: use last used pattern if "pat" is NULL
//options & SEARCH_HIS: put search string in history
//options & SEARCH_KEEP: keep previous search pattern
//return FAIL if failed, OK otherwise.
pub int
search_regcomp(
   Text pat,
   Arr(CS) used_pat,
   int pat_save,
   int pat_use,
   int options,
   OUT RegMultilineMatch* regmatch   // return: pattern and ignore-case flag
){
   int magic;
   anyRegexEmsgG = false;

   // If no pattern given, use a previously defined pattern.
   if (pat.len == 0) {
      int i = (pat_use == RE_LAST) ? last_idx : pat_use;
      if (prevSearchPatternsP[i].pat.c == 0) {   // pattern was never defined
         if (pat_use == RE_SUBST)
            emsg(_(e_no_previous_substitute_regular_expression));
         else
            emsg(_(e_no_previous_regular_expression));
         anyRegexEmsgG = true;
         return FAIL;
      }
      pat = prevSearchPatternsP[i].pat;
      magic = prevSearchPatternsP[i].magic;
      no_smartcase = prevSearchPatternsP[i].no_scs;
   } ei (options & SEARCH_HIS)   // put new pattern in history
      scrAddToHistory(HIST_SEARCH, pat, true, ZERO);

   if (used_pat)
      *used_pat = pat.c;

   eeglFree(mrPatternP.c);
   mrPatternP = copyText(pat);

   //Save the currently used pattern in the appropriate place,
   //unless the pattern should not be remembered.
   if (!(options & SEARCH_KEEP) && (commModifierG.cmod_flags & CMOD_KEEPPATTERNS) == 0) {
      // search or global command
      if (pat_save == RE_SEARCH || pat_save == RE_BOTH)
          save_re_pat(RE_SEARCH, pat, magic);
      // substitute or global command
      if (pat_save == RE_SUBST || pat_save == RE_BOTH)
          save_re_pat(RE_SUBST, pat, magic);
   }

   regmatch->rmm_ic = ignorecase(pat.c);
   regmatch->rmm_maxcol = 0;
   regmatch->regprog = compileRegexp(pat.c, magic ? RE_MAGIC : 0);
   if (regmatch->regprog == NULL)
      return FAIL;
   return OK;
}

// Get search pattern used by search_regcomp().
pub CS
get_search_pat(void) {
   return mrPatternP.c;
}

pub void
save_re_pat(int idx, Text pat, int magic) {
   if (prevSearchPatternsP[idx].pat.c == pat.c)
      return;

   eeglFree(prevSearchPatternsP[idx].pat.c);
   prevSearchPatternsP[idx].pat = copyText(pat);
   prevSearchPatternsP[idx].magic = magic;
   prevSearchPatternsP[idx].no_scs = no_smartcase;
   last_idx = idx;
   //If @hlsearch set and search pat changed: need redraw.
   if (p_hls && idx == RE_SEARCH)
      redraw_all_later(UPD_SOME_VALID);
   setHlsearch(true);
}

//Save the search patterns, so they can be restored later.
//Used before/after executing autocommands and user functions.
private int saveLevelS = 0;

pub void
save_search_patterns(void) {
   if (saveLevelS++ != 0)
      return;

   for (int i = 0; i < (int)ARRAY_LENGTH(prevSearchPatternsP); ++i) {
      saved_spats[i] = prevSearchPatternsP[i];
      if (prevSearchPatternsP[i].pat.len != 0) {
         saved_spats[i].pat = copyText(prevSearchPatternsP[i].pat);
      }
   }
   if (mrPatternP.len == 0)
      mrPatternSaved = (Text){NULL, 0};
   else
      mrPatternSaved = copyText(mrPatternP);
   saved_spats_last_idx = last_idx;
   saved_spatsHlsearch = hiliteSearchG;
}

pub void
restore_search_patterns(void) {
   if (--saveLevelS != 0)
      return;

   for (Unt i = 0; i < ARRAY_LENGTH(prevSearchPatternsP); ++i) {
      eeglFree(prevSearchPatternsP[i].pat.c);
      prevSearchPatternsP[i] = saved_spats[i];
   }
   set_vv_searchforward();
   eeglFree(mrPatternP.c);
   mrPatternP = mrPatternSaved;
   last_idx = saved_spats_last_idx;
   setHlsearch(saved_spatsHlsearch);
}

#if defined(EXITFREE) || defined(PROTO)
pub void
free_search_patterns(void) {
   for (int i = 0; i < (int)ARRAY_LENGTH(prevSearchPatternsP); ++i) {
      EE_CLEAR(prevSearchPatternsP[i].pat);
      prevSearchPatternsP[i].patlen = 0;
   }
   EE_CLEAR(mrPatternP.c);
   mrPatternLen = 0;
}
#endif

// copy of prevSearchPatternsP[RE_SEARCH], for keeping the search patterns while incremental
// searching
private SearchPattern saved_last_search_spat;
private int did_save_last_search_spat = 0;
private int saved_last_idx = 0;
private Boole savedHlsearch = true;
private int saved_search_match_endcol;
private int saved_search_match_lines;

//Save and restore the search pattern for incremental highlight search feature.
//
//It's similar to but different from save_search_patterns() and
//restore_search_patterns(), because the search pattern must be restored when
//canceling incremental searching even if it's called inside user functions.
pub void
save_last_search_pattern(void) {
   if (++did_save_last_search_spat != 1)
      // nested call, nothing to do
      return;

   saved_last_search_spat = prevSearchPatternsP[RE_SEARCH];
   if (prevSearchPatternsP[RE_SEARCH].pat.len != 0) {
      saved_last_search_spat.pat = copyText(prevSearchPatternsP[RE_SEARCH].pat);
   }
   saved_last_idx = last_idx;
   savedHlsearch = hiliteSearchG;
}

pub void
restore_last_search_pattern(void) {
   if (--did_save_last_search_spat > 0)
      // nested call, nothing to do
      return;
   if (did_save_last_search_spat != 0) {
      internalErrMsg(S"restore_last_search_pattern() called more often than save_last_search_pattern()");
      return;
   }

   eeglFree(prevSearchPatternsP[RE_SEARCH].pat.c);
   prevSearchPatternsP[RE_SEARCH] = saved_last_search_spat;
   saved_last_search_spat.pat = (Text){NULL, 0};
   set_vv_searchforward();
   last_idx = saved_last_idx;
   setHlsearch(savedHlsearch);
}

//Save and restore the incsearch hiliting variables.
//This is required so that calling searchcount() at does not invalidate the incsearch hiliting.
private void
save_incsearch_state(void) {
   saved_search_match_endcol = search_match_endcol;
   saved_search_match_lines  = search_match_lines;
}

private void
restore_incsearch_state(void) {
   search_match_endcol = saved_search_match_endcol;
   search_match_lines  = saved_search_match_lines;
}

pub Text
last_search_pattern(void) {
   return prevSearchPatternsP[RE_SEARCH].pat;
}

//Return true when case should be ignored for search pattern "pat".
//Use the 'ignorecase' and 'smartcase' options.
pub int
ignorecase(CS pat) {
   return ignorecase_opt(pat, p_ic, p_scs);
}

//As ignorecase() but pass the "ic" and "scs" flags.
pub int
ignorecase_opt(CS pat, int ic_in, int scs) {
   int      ic = ic_in;

   if (ic && !no_smartcase && scs && !(ctrl_x_mode_not_default() && curBook->o.inferCase))
      ic = !pat_has_uppercase(pat);
   no_smartcase = false;
   return ic;
}

// Return true if pattern "pat" has an uppercase character.
pub int
pat_has_uppercase(CS pat) {
   CS p = pat;
   Magic magic_val = MAGIC_ON;

   // get the magicness of the pattern
   (void)skip_regexp_ex(pat, ZERO, true, NULL, NULL, &magic_val);

   while (*p != ZERO) {
      int      l;

      if ((l = utfCharLen(p)) > 1) {
         if (utf_isupper(mb_ptr2char(p)))
            return true;
         p += l;
      } ei (*p == '\\' && magic_val <= MAGIC_ON) {
         if (p[1] == '_' && p[2] != ZERO)  // skip "\_X"
            p += 3;
         ei (p[1] == '%' && p[2] != ZERO)  // skip "\%X"
            p += 3;
         ei (p[1] != ZERO)  // skip "\X"
            p += 2;
         else
            p += 1;
      } ei ((*p == '%' || *p == '_') && magic_val == MAGIC_ALL) {
         if (p[1] != ZERO)  // skip "_X" and %X
            p += 2;
         else
            p++;
      } ei (MB_ISUPPER(*p))
         return true;
      else
         ++p;
   }
   return false;
}

pub CS
last_csearch(void) {
   return lastc_bytes;
}

pub int
last_csearch_forward(void) {
   return lastcdir == FORWARD;
}

pub int
last_csearch_until(void) {
   return last_t_cmd == true;
}

pub void
set_last_csearch(int c, CS s, int len) {
   *lastc = c;
   lastc_bytelen = len;
   if (len)
      memcpy(lastc_bytes, s, len);
   else
      CLEAR_FIELD(lastc_bytes);
}

pub void
set_csearch_direction(int cdir) {
   lastcdir = cdir;
}

pub void
set_csearch_until(int t_cmd) {
   last_t_cmd = t_cmd;
}

pub Text
last_search_pat(void) {
   return prevSearchPatternsP[last_idx].pat;
}

// Reset search direction to forward.  For "gd" and "gD" commands.
pub void
reset_search_dir(void) {
   prevSearchPatternsP[0].off.dir = '/';
   set_vv_searchforward();
}

//Set the last search pattern.  For ":let @/ =" and eeglinfo.
//Also set the saved search pattern, so that this works in an autocommand.
pub void
set_last_search_pat(
   CS s,
   int idx,
   int magic,
   int setlast
) {
   eeglFree(prevSearchPatternsP[idx].pat.c);
   // An empty string means that nothing should be matched.
   if (*s == ZERO)
      prevSearchPatternsP[idx].pat.len = 0;
   else {
      prevSearchPatternsP[idx].pat = 
         (Text){copySubstr(s, prevSearchPatternsP[idx].pat.len), STRLEN(s)};
   }
   prevSearchPatternsP[idx].magic = magic;
   prevSearchPatternsP[idx].no_scs = false;
   prevSearchPatternsP[idx].off.dir = '/';
   set_vv_searchforward();
   prevSearchPatternsP[idx].off.line = false;
   prevSearchPatternsP[idx].off.end = false;
   prevSearchPatternsP[idx].off.off = 0;
   if (setlast)
      last_idx = idx;
   if (saveLevelS) {
      eeglFree(saved_spats[idx].pat.c);
      saved_spats[idx] = prevSearchPatternsP[0];
      if (prevSearchPatternsP[idx].pat.len == 0)
         saved_spats[idx].pat.len = 0;
      else
         saved_spats[idx].pat = copyText(prevSearchPatternsP[idx].pat);
      saved_spats_last_idx = last_idx;
   }
   // If @hlsearch set and search pat changed: need redraw.
   if (p_hls && idx == RE_SEARCH && hiliteSearchG)
      redraw_all_later(UPD_SOME_VALID);
}

//Get a regexp program for the last used search pattern. This is used for hiliting all matches 
//in a portal. Values returned in regmatch->regprog and regmatch->rmm_ic.
pub void
last_pat_prog(RegMultilineMatch* regmatch) {
   if (prevSearchPatternsP[RE_SEARCH].pat.len == 0) {
      regmatch->regprog = NULL;
      return;
   }
   ++emsg_off;      // So it doesn't beep if bad expr
   (void)search_regcomp((Text){null, 0}, NULL, 0, RE_SEARCH, SEARCH_KEEP, OUT regmatch);
   --emsg_off;
}

//Lowest level search function.
//Search for 'count'th occurrence of pattern "pat" in direction "dir".
//Start at position "pos" and return the found position in "pos".
//
//if (options & SEARCH_MSG) == 0 don't give any messages
//if (options & SEARCH_MSG) == SEARCH_NFMSG don't give 'notfound' messages
//if (options & SEARCH_MSG) == SEARCH_MSG give all messages
//if (options & SEARCH_HIS) put search pattern in history
//if (options & SEARCH_END) return position at end of match
//if (options & SEARCH_START) accept match at pos itself
//if (options & SEARCH_KEEP) keep previous search pattern
//if (options & SEARCH_FOLD) match only once in a closed fold
//if (options & SEARCH_PEEK) check for typed char, cancel search
//if (options & SEARCH_COL) start at pos->col instead of zero
//
//Return FAIL (zero) for failure, non-zero for success.
//Return the index of the first matching subpattern plus one; one if there was none.
pub int
searchit(
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
){
   int      found;
   LineNr   lnum;      // no init to shut up Apollo cc
   ColNr   col;
   RegMultilineMatch   regmatch;
   CS ptr;
   ColNr   matchcol;
   PosNoVirt   endpos;
   PosNoVirt   matchpos;
   int loop;
   Pos   start_pos;
   int at_first_line;
   int extra_col;
   int start_char_len;
   int match_ok;
   long nmatched;
   int submatch = 0;
   int first_match = true;
   int called_emsg_before = called_emsg;
   int break_loop = false;
   LineNr   stop_lnum = 0;   // stop after this line number when != 0
   int      unused_timeout_flag = false;
   int      *timed_out = &unused_timeout_flag;  // set when timed out.

   if (search_regcomp(pat, NULL, RE_SEARCH, pat_use,
         (options & (SEARCH_HIS + SEARCH_KEEP)), OUT &regmatch) == FAIL
   ){
      if ((options & SEARCH_MSG) && !anyRegexEmsgG)
         showErrFmtMsg(_(e_invalid_search_string_str), mrPatternP.c);
      return FAIL;
   }

   if (extra_arg) {
      stop_lnum = extra_arg->sa_stop_lnum;
      if (extra_arg->sa_tm > 0)
         init_regexp_timeout(extra_arg->sa_tm);
      // Also set the pointer when sa_tm is zero, the caller may have set the
      // timeout.
      timed_out = &extra_arg->sa_timed_out;
   }

   // find the string
   do {  // loop for count
         // When not accepting a match at the start position set "extra_col" to
         // a non-zero value.  Don't do that when starting at MAXCOL, since MAXCOL + 1 is zero.
         if (pos->col == MAXCOL)
             start_char_len = 0;
         // Watch out for the "col" being MAXCOL - 2, used in a closed fold.
         ei (pos->lnum >= 1 && pos->lnum <= book->mem.lineCount && pos->col < MAXCOL - 2){
            ptr = memGetLine(book, pos->lnum, false);
            if (memGetBookLen(book, pos->lnum) <= pos->col)
               start_char_len = 1;
            else
               start_char_len = utfCharLen(ptr + pos->col);
         } else
             start_char_len = 1;
         if (dir == FORWARD) {
            if (options & SEARCH_START)
               extra_col = 0;
            else
               extra_col = start_char_len;
         } else {
            if (options & SEARCH_START)
               extra_col = start_char_len;
            else
               extra_col = 0;
      }

      start_pos = *pos;   // remember start pos for detecting no match
      found = 0;      // default: not found
      at_first_line = true;   // default: start in first line
      if (pos->lnum == 0) {   // correct lnum for when starting in line 0
         pos->lnum = 1;
         pos->col = 0;
         at_first_line = false;  // not in first line now
      }

      //Start searching in current line, unless searching backwards and we're in column 0.
      //If we are searching backwards, in column 0, and not including the
      //current position, gain some efficiency by skipping back a line.
      //Otherwise begin the search in the current line.
      if (dir == BACKWARD && start_pos.col == 0 && (options & SEARCH_START) == 0) {
         lnum = pos->lnum - 1;
         at_first_line = false;
      } else
         lnum = pos->lnum;

      for (loop = 0; loop <= 1; ++loop) {  // loop twice if 'wrapscan' set
         for ( ; lnum > 0 && lnum <= book->mem.lineCount; lnum += dir, at_first_line = false) {
            // Stop after checking "stop_lnum", if it's set.
            if (stop_lnum != 0 && (dir == FORWARD ? lnum > stop_lnum : lnum < stop_lnum))
               break;
            // Stop after passing the time limit.
            if (*timed_out)
               break;

            // Look for a match somewhere in line "lnum".
            col = at_first_line && (options & SEARCH_COL) ? pos->col : (ColNr)0;
            nmatched = eeRegexec_multi(&regmatch, port, book, lnum, col, timed_out);
            // eeRegexec_multi() may clear "regprog"
            if (regmatch.regprog == NULL)
               break;
            // Abort searching on an error (e.g., out of stack).
            if (called_emsg > called_emsg_before || *timed_out)
                break;
            if (nmatched > 0) {
               // match may actually be in another line when using \zs
               matchpos = regmatch.startpos[0];
               endpos = regmatch.endpos[0];
               submatch = first_submatch(&regmatch);
               // "lnum" may be past end of buffer for "\n\zs".
               if (lnum + matchpos.lnum > book->mem.lineCount)
                  ptr = (CS)"";
               else
                  ptr = memGetLine(book, lnum + matchpos.lnum, false);

               //Forward search in the first line: match should be after the start position. If 
               //not, continue at the end of the match (this is vi compatible) or on the next char.
               if (dir == FORWARD && at_first_line) {
               match_ok = true;

               //When the match starts in a next line it's certainly past the start position.
               //When match lands on a ZERO the cursor will be put
               //one back afterwards, compare with that position,
               //otherwise "/$" will get stuck on end of line.
               while (matchpos.lnum == 0
                  && ((options & SEARCH_END) && first_match
                      ?  (nmatched == 1
                     && (int)endpos.col - 1
                          < (int)start_pos.col + extra_col)
                      : ((int)matchpos.col
                          - (ptr[matchpos.col] == ZERO)
                         < (int)start_pos.col + extra_col)))
               {
                  //otherwise continue one position forward.
                  if (nmatched > 1) {
                      // end is in next line, thus no match in this line
                      match_ok = false;
                      break;
                  }
                  matchcol = endpos.col;
                  // for empty match: advance one char
                  if (matchcol == matchpos.col && ptr[matchcol] != ZERO) {
                      matchcol += utfCharLen(ptr + matchcol);
                  }
                  if (matchcol == 0 && (options & SEARCH_START))
                     break;
                  if (ptr[matchcol] == ZERO
                       || (nmatched = eeRegexec_multi(&regmatch,
                            port, book, lnum + matchpos.lnum,
                            matchcol, timed_out)) == 0
                  ) {
                     match_ok = false;
                     break;
                  }
                  // eeRegexec_multi() may clear "regprog"
                  if (regmatch.regprog == NULL)
                     break;
                  matchpos = regmatch.startpos[0];
                  endpos = regmatch.endpos[0];
                  submatch = first_submatch(&regmatch);

                  //Need to get the line pointer again, a multi-line search may have invalidated it
                  ptr = memGetLine(book, lnum + matchpos.lnum, false);
               }
               if (!match_ok)
                  continue;
               }
               if (dir == BACKWARD) {
                  //Now, if there are multiple matches on this line, we have to get the last one. 
                  //Or the last one before the cursor, if we're on that line.
                  //When putting the new cursor at the end, compare relative to the end of the match
                  match_ok = false;
                  for (;;) {
                      // Remember a position that is before the start
                      // position, we use it if it's the last match in
                      // the line.  Always accept a position after wrapping around.
                      if (loop
                           || ((options & SEARCH_END)
                               ? (lnum + regmatch.endpos[0].lnum < start_pos.lnum
                                    || (lnum + regmatch.endpos[0].lnum == start_pos.lnum
                                         && (int)regmatch.endpos[0].col - 1
                                             < (int)start_pos.col + extra_col))
                               : (lnum + regmatch.startpos[0].lnum < start_pos.lnum
                                    || (lnum + regmatch.startpos[0].lnum == start_pos.lnum
                                         && (int)regmatch.startpos[0].col 
                                            < (int)start_pos.col + extra_col)
                                 )
                           )
                     ) {
                        match_ok = true;
                        matchpos = regmatch.startpos[0];
                        endpos = regmatch.endpos[0];
                        submatch = first_submatch(&regmatch);
                     } else
                        break;

                     //We found a valid match, now check if there is
                     //another one after it. continue one position forward.
                     if (nmatched > 1)
                        break;
                     matchcol = endpos.col;
                     // for empty match: advance one char
                     if (matchcol == matchpos.col && ptr[matchcol] != ZERO) {
                        matchcol += utfCharLen(ptr + matchcol);
                     }
                     if (ptr[matchcol] == ZERO
                         || (nmatched = eeRegexec_multi(&regmatch,
                              port, book, lnum + matchpos.lnum,
                              matchcol, timed_out)) == 0
                     ) {
                        // If the search timed out, we did find a match
                        // but it might be the wrong one, so that's not OK.
                        if (*timed_out)
                           match_ok = false;
                        break;
                     }
                     // eeRegexec_multi() may clear "regprog"
                     if (regmatch.regprog == NULL)
                        break;

                      //Need to get the line pointer again, a multi-line search may have 
                      //invalidated it
                      ptr = memGetLine(book, lnum + matchpos.lnum, false);
                  }

                  // If there is only a match after the cursor, skip this match.
                  if (!match_ok)
                     continue;
                }

               //With the SEARCH_END option move to the last character of the match. Don't do it 
               //for an empty match, end should be same as start then.
               if ((options & SEARCH_END) && !(options & SEARCH_NOOF)
                   && !(matchpos.lnum == endpos.lnum && matchpos.col == endpos.col)
               ) {
                  // For a match in the first column, set the position
                  // on the ZERO in the previous line.
                  pos->lnum = lnum + endpos.lnum;
                  pos->col = endpos.col;
                  if (endpos.col == 0) {
                     if (pos->lnum > 1) { // just in case
                        --pos->lnum;
                        pos->col = memGetBookLen(book, pos->lnum);
                     }
                  } else {
                      --pos->col;
                      if (pos->lnum <= book->mem.lineCount) {
                        ptr = memGetLine(book, pos->lnum, false);
                        pos->col -= (*mb_head_off)(ptr, ptr + pos->col);
                      }
                  }
                  if (end_pos) {
                      end_pos->lnum = lnum + matchpos.lnum;
                      end_pos->col = matchpos.col;
                  }
               } else {
                  pos->lnum = lnum + matchpos.lnum;
                  pos->col = matchpos.col;
                  if (end_pos) {
                      end_pos->lnum = lnum + endpos.lnum;
                      end_pos->col = endpos.col;
                  }
               }
               pos->coladd = 0;
               if (end_pos)
                  end_pos->coladd = 0;
               found = 1;
               first_match = false;

                // Set variables used for 'incsearch' hiliting.
                search_match_lines = endpos.lnum - matchpos.lnum;
                search_match_endcol = endpos.col;
                break;
            }
            line_breakcheck();   // stop if ctrl-C typed
            if (gotInterruptG)
                break;

            //Cancel searching if a character was typed.  Used for
            //'incsearch'.  Don't check too often, that would slowdown searching too much.
            if ((options & SEARCH_PEEK) && ((lnum - pos->lnum) & 0x3f) == 0 && char_avail()) {
                break_loop = true;
                break;
            }

            if (loop && lnum == start_pos.lnum)
                break;       // if second loop, stop where started
         }
         at_first_line = false;

         // eeRegexec_multi() may clear "regprog"
         if (regmatch.regprog == NULL)
            break;

         //Stop the search if wrapscan isn't set, "stop_lnum" is
         //specified, after an interrupt, after a match and after looping twice.
         if (wrapSearchG || stop_lnum != 0 || gotInterruptG
                  || called_emsg > called_emsg_before || *timed_out
                  || break_loop
                  || found || loop)
            break;

         //If 'wrapscan' is set we continue at the other end of the file.
         //If 'shortmess' does not contain 's', we give a message, but
         //only, if we won't show the search stat later anyhow,
         //(so SEARCH_COUNT must be absent).
         //This message is also remembered in msgAfterRedrawG for when the screen is redrawn. 
         //The msgAfterRedrawG is cleared whenever another message is written.
         if (dir == BACKWARD)    // start second loop at the other end
            lnum = book->mem.lineCount;
         else
            lnum = 1;
         if (extra_arg != NULL)
            extra_arg->sa_wrapped = true;
      }
      if (gotInterruptG || called_emsg > called_emsg_before || *timed_out || break_loop)
         break;
   } while (--count > 0 && found);   // stop after count matches or no match

   if (extra_arg && extra_arg->sa_tm > 0)
      disable_regexp_timeout();
   eeRegFree(regmatch.regprog);

   if (!found) {         // did not find it
      if (gotInterruptG)
         emsg(_(e_interrupted));
      ei ((options & SEARCH_MSG) == SEARCH_MSG) {
         if (wrapSearchG)
            showErrFmtMsg(_(e_pattern_not_found_str), mrPatternP.c);
      }
      return FAIL;
   }

   // A pattern like "\n\zs" may go past the last line.
   if (pos->lnum > book->mem.lineCount) {
      pos->lnum = book->mem.lineCount;
      pos->col = memGetBookLen(book, pos->lnum);
      if (pos->col > 0)
         --pos->col;
   }

   return submatch + 1;
}

pub void
set_search_direction(int cdir) {
   prevSearchPatternsP[0].off.dir = cdir;
}

private void
set_vv_searchforward(void) {
   set_EeglVar_nr(VV_SEARCHFORWARD, (long)(prevSearchPatternsP[0].off.dir == '/'));
}

//Return the number of the first subpat that matched. Return zero if none of them matched.
private int
first_submatch(RegMultilineMatch *rp) {
   int      submatch;

   for (submatch = 1; ; ++submatch) {
      if (rp->startpos[submatch].lnum >= 0)
          break;
      if (submatch == 9) {
          submatch = 0;
          break;
      }
   }
   return submatch;
}

//Highest level string search function.
//Search for the 'count'th occurrence of pattern 'pat' in direction 'dirc'
//       If 'dirc' is 0: use previous dir.
//   If 'pat' is NULL or empty : use previous string.
//   If 'options & SEARCH_REV' : go in reverse of previous dir.
//   If 'options & SEARCH_ECHO': echo the search command and handle options
//   If 'options & SEARCH_MSG' : may give error message
//   If 'options & SEARCH_OPT' : interpret optional flags
//   If 'options & SEARCH_HIS' : put search pattern in history
//   If 'options & SEARCH_NOOF': don't add offset to position
//   If 'options & SEARCH_MARK': set previous context mark
//   If 'options & SEARCH_KEEP': keep previous search pattern
//   If 'options & SEARCH_START': accept match at curpos itself
//   If 'options & SEARCH_PEEK': check for typed char, cancel search
//
//Careful: If prevSearchPatternsP[0].off.line == true and prevSearchPatternsP[0].off.off == 0 this
//makes the movement linewise without moving the match position.
//
//Return 0 for failure, 1 for found, 2 for found and line offset added.
pub int
do_search(
   Operator* oap,   // can be NULL
   int dirc,   // '/' or '?'
   int search_delim, // the delimiter for the search, e.g. '%' in s%regex%replacement%
   Text pat,
   long count,
   int options,
   SearchitArg* sia   // optional arguments or NULL
){
   Text searchstr;
   SearchOffset       old_off;
   int retval;   // Return value
   CS p;
   long c;
   CS dircp;
   CS strcopy = NULL;
   CS ps;
   Boole showSearchStats;
   CS msgbuf = NULL;
   Unt msgbuflen = 0;
   int has_offset = false;

   //Save the values for when (options & SEARCH_KEEP) is used.
   //(there is no "if ()" around this because gcc wants them initialized)
   old_off = prevSearchPatternsP[0].off;
   // position of the last match
   Pos pos = curPor->cursor;   // start searching at the cursor position

   //Find out the direction of the search.
   if (dirc == 0)
      dirc = prevSearchPatternsP[0].off.dir;
   else {
      prevSearchPatternsP[0].off.dir = dirc;
      set_vv_searchforward();
   }
   if (options & SEARCH_REV) {
      if (dirc == '/')
         dirc = '?';
      else
         dirc = '/';
   }

   // If the cursor is in a closed fold, don't find another match in the same fold.
   if (dirc == '/') {
      if (getFolds(pos.lnum, NULL, OUT &pos.lnum))
         pos.col = MAXCOL - 2;   // avoid overflow when adding 1
   } ei (getFolds(pos.lnum, OUT &pos.lnum, NULL))
      pos.col = 0;

   //Turn @hlsearch hiliting back on.
   if (!hiliteSearchG && !(options & SEARCH_KEEP)) {
      redraw_all_later(UPD_SOME_VALID);
      setHlsearch(true);
   }

   //Repeat the search when pattern followed by ';', e.g. "/foo/;?bar".
   for (;;) {
      int show_top_bot_msg = false;

      searchstr = pat;

      dircp = NULL;
                      // use previous pattern
      if (pat.len == 0 || pat.c[0] == search_delim) {
          if (prevSearchPatternsP[RE_SEARCH].pat.len == 0) {      // no previous pattern
            if (prevSearchPatternsP[RE_SUBST].pat.len == 0) {
               emsg(_(e_no_previous_regular_expression));
               retval = 0;
               goto end_do_search;
            }
            searchstr = prevSearchPatternsP[RE_SUBST].pat;
         } else {
            // make search_regcomp() use prevSearchPatternsP[RE_SEARCH].pat
            searchstr = (Text){null, 0};
         }
      }

      if (pat.len > 0) {  // look for (new) offset
         // Find end of regular expression. If there is a matching '/' or '?', toss it.
         ps = strcopy;
         p = skip_regexp_ex(pat.c, search_delim, true, &strcopy, NULL, NULL);
         if (strcopy != ps) {
            // made a copy of "pat" to change "\?" to "?"
            pat = text(strcopy);
            searchstr = (Text){strcopy, pat.len};
         }
         if (*p == search_delim) {
            searchstr.len = p - pat.c;
            dircp = p;   // remember where we put the ZERO
            *p++ = ZERO;
         }
         prevSearchPatternsP[0].off.line = false;
         prevSearchPatternsP[0].off.end = false;
         prevSearchPatternsP[0].off.off = 0;
         //Check for a line offset or a character offset.
         //For doGetCommandAddress (echo off) we don't check for a character
         //offset, because it is meaningless and the 's' could be a substitute command.
         if (*p == '+' || *p == '-' || EE_ISDIGIT(*p))
            prevSearchPatternsP[0].off.line = true;
         ei ((options & SEARCH_OPT) && (*p == 'e' || *p == 's' || *p == 'b')) {
            if (*p == 'e')      // end
                prevSearchPatternsP[0].off.end = SEARCH_END;
            ++p;
         }
         if (EE_ISDIGIT(*p) || *p == '+' || *p == '-') { // got an offset
                         // 'nr' or '+nr' or '-nr'
            if (EE_ISDIGIT(*p) || EE_ISDIGIT(*(p + 1)))
               prevSearchPatternsP[0].off.off = atol((char *)p);
            ei (*p == '-')       // single '-'
               prevSearchPatternsP[0].off.off = -1;
            else             // single '+'
               prevSearchPatternsP[0].off.off = 1;
            ++p;
            while (EE_ISDIGIT(*p))       // skip number
               ++p;
          }

          pat.len -= p - pat.c;
          pat.c = p;             // put pat after search command
      }

      showSearchStats = false;
      if ((options & SEARCH_ECHO) && messaging() && !msg_silent && (!cmd_silent)) {
         Byte off_buf[40];
         Unt off_len = 0;
         Unt plen;
         Unt msgbufsize;

         // Compute msgRowG early.
         msg_start();

         // Get the offset, so we know how long it is.
         if (!cmd_silent &&
             (prevSearchPatternsP[0].off.line 
              || prevSearchPatternsP[0].off.end 
              || prevSearchPatternsP[0].off.off)
         ) {
            off_buf[off_len++] = dirc;
            if (prevSearchPatternsP[0].off.end)
                off_buf[off_len++] = 'e';
            ei (!prevSearchPatternsP[0].off.line)
                off_buf[off_len++] = 's';
            off_buf[off_len] = ZERO;
            if (prevSearchPatternsP[0].off.off != 0 || prevSearchPatternsP[0].off.line)
                off_len += eeSnprintf(off_buf + off_len,
                  sizeof(off_buf) - off_len, "%+ld", prevSearchPatternsP[0].off.off);
         }

         if (searchstr.len == ZERO) {
            p = prevSearchPatternsP[0].pat.c;
            plen = prevSearchPatternsP[0].pat.len;
         } else {
            p = searchstr.c;
            plen = searchstr.len;
         }

         if (cmd_silent) {
            // Reserve enough space for the search pattern + offset +
            // search stat.  Use all the space available, so that the
            // search state is right aligned.  If there is not enough space
            // msg_strtrunc() will shorten in the middle.
            if (msg_scrolled != 0 && !cmd_silent)
                // Use all the columns.
                msgbufsize = (int)(visibleRowsG - msgRowG) * visibleColsG - 1;
            else
                // Use up to 'showcmd' column.
                msgbufsize = (int)(visibleRowsG - msgRowG - 1) * visibleColsG + shownCommandColG - 1;
            if (msgbufsize < plen + off_len + SEARCH_STAT_BUF_LEN + 3)
                msgbufsize = plen + off_len + SEARCH_STAT_BUF_LEN + 3;
         } else
            // Reserve enough space for the search pattern + offset.
            msgbufsize = plen + off_len + 3;

         eeglFree(msgbuf);
         msgbuf = alloc(msgbufsize);
         memset(msgbuf, ' ', msgbufsize);
         msgbuflen = msgbufsize - 1;
         msgbuf[msgbuflen] = ZERO;
         // do not fill the msgbuf buffer, if cmd_silent is set, leave it
         // empty for the search_stat feature.
         if (!cmd_silent) {
            CS trunc;

            msgbuf[0] = dirc;

            if (utf_iscomposing(mb_ptr2char(p))) {
               // Use a space to draw the composing char on.
               msgbuf[1] = ' ';
               MEMMOVE(msgbuf + 2, p, plen);
            } else
               MEMMOVE(msgbuf + 1, p, plen);
            if (off_len > 0)
               MEMMOVE(msgbuf + plen + 1, off_buf, off_len);

            trunc = msg_strtrunc(msgbuf, true);
            if (trunc != NULL) {
               eeglFree(msgbuf);
               msgbuf = trunc;
               msgbuflen = STRLEN(msgbuf);
            }

             msg_outtrans(msgbuf);
             msg_clr_eos();
             msg_check();

             gotoCommline(false);
             out_flush();
             msg_nowait = true;       // don't wait for this message
         }

         showSearchStats = true;
      }

      //If there is a character offset, subtract it from the current
      //position, so we don't get stuck at "?pat?e+2" or "/pat/s-2".
      //Skip this if pos.col is near MAXCOL (closed fold).
      //This is not done for a line offset, because then we would not be vi compatible.
      if (!prevSearchPatternsP[0].off.line && prevSearchPatternsP[0].off.off && pos.col < MAXCOL - 2) {
         if (prevSearchPatternsP[0].off.off > 0) {
            for (c = prevSearchPatternsP[0].off.off; c; --c)
               if (decl(&pos) == -1)
                  break;
            if (c) {        // at start of buffer
               pos.lnum = 0;   // allow lnum == 0 here
               pos.col = MAXCOL;
            }
         } else {
            for (c = prevSearchPatternsP[0].off.off; c; ++c)
               if (incl(&pos) == -1)
                  break;
            if (c) {        // at end of buffer
               pos.lnum = curBook->mem.lineCount + 1;
               pos.col = 0;
            }
         }
      }

      //The actual search.
      c = searchit(
         curPor, curBook, &pos, NULL, dirc == '/' ? FORWARD : BACKWARD,
         searchstr, count, 
         prevSearchPatternsP[0].off.end 
            + (options & (SEARCH_KEEP + SEARCH_PEEK + SEARCH_HIS + SEARCH_MSG 
               + SEARCH_START + ((pat.len != 0 && pat.c[0] == ';') ? 0 : SEARCH_NOOF))
            ),
         RE_LAST, sia
      );

      if (dircp)
         *dircp = search_delim; // restore second '/' or '?' for normal_cmd()


      if (c == FAIL) {
         retval = 0;
         goto end_do_search;
      }
      if (prevSearchPatternsP[0].off.end && oap != NULL)
         oap->inclusive = true;  // 'e' includes last character

      retval = 1;          // pattern found

      //Add character and/or line offset
      if ((options & SEARCH_NOOF) == 0 || (pat.len != 0 && pat.c[0] == ';')) {
         Pos org_pos = pos;

         if (prevSearchPatternsP[0].off.line){   // Add the offset to the line number.
            c = pos.lnum + prevSearchPatternsP[0].off.off;
            if (c < 1)
               pos.lnum = 1;
            ei (c > curBook->mem.lineCount)
               pos.lnum = curBook->mem.lineCount;
            else
               pos.lnum = c;
            pos.col = 0;

            retval = 2;       // pattern found, line offset added
         } ei (pos.col < MAXCOL - 2) {  // just in case
            // to the right, check for end of file
            c = prevSearchPatternsP[0].off.off;
            if (c > 0) {
               while (c-- > 0) {
                  if (incl(&pos) == -1)
                      break;
               } 
            } else {// to the left, check for start of file
               while (c++ < 0) {
                  if (decl(&pos) == -1)
                     break;
               } 
            }
         }
         if (!EQUAL_POS(pos, org_pos))
            has_offset = true;
      }

      // Show [1/15] if 'S' is not in 'shortmess'.
      if (showSearchStats) {
         cmdline_search_stat(
            dirc, &pos, &curPor->cursor, show_top_bot_msg, msgbuf, msgbuflen,
            (count != 1 || has_offset
                || (!(p_fdo & FDO_SEARCH) && getFolds(curPor->cursor.lnum, NULL, NULL))
            ),
            p_msc, SEARCH_STAT_DEF_TIMEOUT
         );
      } 

      //The search command can be followed by a ';' to do another search.
      //For example: "/pat/;/foo/+3;?bar"
      //This is like doing another search command, except:
      //- The remembered direction '/' or '?' is from the first search.
      //- When an error happens the cursor isn't moved at all.
      //Don't do this when called by doGetCommandAddress() (it handles ';' itself).
      if ((options & SEARCH_OPT) == 0 || pat.len == 0 || pat.c[0] != ';')
         break;

      dirc = pat.c[1];
      pat.c++;
      pat.len--;
      search_delim = dirc;
      if (dirc != '?' && dirc != '/') {
         retval = 0;
         emsg(_(e_expected_question_or_slash_after_semicolon));
         goto end_do_search;
      }
      pat.c++;
      pat.len--;
   }

   if (options & SEARCH_MARK)
      setpcmark();
   curPor->cursor = pos;
   curPor->setCursWant = true;

end_do_search:
   if ((options & SEARCH_KEEP) || (commModifierG.cmod_flags & CMOD_KEEPPATTERNS))
      prevSearchPatternsP[0].off = old_off;
   eeglFree(strcopy);
   eeglFree(msgbuf);

   return retval;
}

//search_for_exact_line(book, pos, dir, pat)
//
//Search for a line starting with the given pattern (ignoring leading
//white-space), starting from pos and going in direction "dir". "pos" will
//contain the position of the match found.    Blank lines match only if
//ADDING is set.  If p_ic is set then the pattern must be in lowercase.
//Return OK for success, or FAIL if no line found.
pub int
search_for_exact_line(
   Book* book,
   Pos* pos,
   int dir,
   CS pat
) {
   LineNr   start = 0;
   CS ptr;
   CS p;

   if (book->mem.lineCount == 0)
      return FAIL;
   for (;;) {
      pos->lnum += dir;
      if (pos->lnum < 1) {
         if (wrapSearchG) {
            pos->lnum = book->mem.lineCount;
         } else { 
            pos->lnum = 1;
            break;
         } 
      } ei (pos->lnum > book->mem.lineCount) {
         pos->lnum = 1;
         if (!wrapSearchG) {
            break;
         } 
      }
      if (pos->lnum == start)
         break;
      if (start == 0)
         start = pos->lnum;
      ptr = memGetLine(book, pos->lnum, false);
      p = skipwhite(ptr);
      pos->col = (ColNr) (p - ptr);

      // when adding lines the matching line may be empty but it is not
      // ignored because we are interested in the next line -- Acevedo
      if (compl_status_adding() && !compl_status_sol()) {
         if ((p_ic ? caseInsensitiveCompareMaxCol(p, pat) : STRCMP(p, pat)) == 0)
            return OK;
      } ei (*p != ZERO) {  // ignore empty lines
         // expanding lines or words
         if ((p_ic ? caseInsensitiveCompareNChars(p, pat, ins_compl_len())
                  : STRNCMP(p, pat, ins_compl_len())) == 0)
         return OK;
      }
   }
   return FAIL;
}

//Character Searches

//Search for a character in a line.  If "t_cmd" is false, move to the
//position of the character, otherwise move to just before the char.
//Do this "cap->count1" times. Return FAIL or OK.
pub int
searchc(ActionArg* cap, int t_cmd) {
   int c = cap->nchar;   // char to search for
   int dir = cap->arg;   // true for searching forward
   long count = cap->count1;   // repeat count
   int stop = true;

   if (c != ZERO) {  // normal search: remember args for repeat
      if (!keyWasStuffedG) {   // don't remember when redoing
         *lastc = c;
         set_csearch_direction(dir);
         set_csearch_until(t_cmd);
         lastc_bytelen = mb_char2bytes(c, lastc_bytes);
         if (cap->ncharC1 != 0) {
            lastc_bytelen += mb_char2bytes(cap->ncharC1, lastc_bytes + lastc_bytelen);
            if (cap->ncharC2 != 0)
               lastc_bytelen += mb_char2bytes(cap->ncharC2, lastc_bytes + lastc_bytelen);
          }
      }
   } else {     // repeat previous search
      if (*lastc == ZERO && lastc_bytelen <= 1)
         return FAIL;
      if (dir)   // repeat in opposite direction
         dir = -lastcdir;
      else
         dir = lastcdir;
      t_cmd = last_t_cmd;
      c = *lastc;
      // For multi-byte re-use last lastc_bytes[] and lastc_bytelen.

      // Force a move of at least one char, so ";" and "," will move the
      // cursor, even if the cursor is right in front of char we are looking at.
      if (count == 1 && t_cmd)
         stop = false;
   }

   if (dir == BACKWARD)
      cap->oper->inclusive = false;
   else
      cap->oper->inclusive = true;

   CS p = ml_get_curline();
   int col = curPor->cursor.col;
   int len = ml_get_curline_len();

   while (count--) {
      for (;;) {
         if (dir > 0) {
            col += utfCharLen(p + col);
            if (col >= len)
               return FAIL;
         } else {
            if (col == 0)
               return FAIL;
            col -= (*mb_head_off)(p, p + col - 1) + 1;
         }
         if (lastc_bytelen <= 1) {
            if (p[col] == c && stop)
            break;
         } ei (STRNCMP(p + col, lastc_bytes, lastc_bytelen) == 0 && stop)
            break;
         stop = true;
      }
   }

   if (t_cmd) {
      // backup to before the character (possibly double-byte)
      col -= dir;
      if (dir < 0)
         // Landed on the search char which is lastc_bytelen long
         col += lastc_bytelen - 1;
      else
         // To previous char, which may be multi-byte.
         col -= (*mb_head_off)(p, p + col);
   }
   curPor->cursor.col = col;

   return OK;
}

//"Other" Searches


//findmatch - find the matching paren or brace
pub Pos*
findmatch(Operator *oap, int initc) {
   return findmatchlimit(oap, initc, 0, 0);
}

//Return true if the character before "linep[col]" equals "ch".
//Return false if "col" is zero.
//Update "*prevcol" to the column of the previous character, unless "prevcol" is NULL.
//Handle multibyte string correctly.
private int
check_prevcol(
   CS linep,
   int      col,
   int      ch,
   int      *prevcol
) {
   --col;
   if (col > 0)
      col -= (*mb_head_off)(linep, linep + col);
   if (prevcol)
      *prevcol = col;
   return (col >= 0 && linep[col] == ch) ? true : false;
}

//Raw string start is found at linep[startpos.col - 1].
//Return true if the matching end can be found between startpos and endpos.
private int
find_rawstring_end(CS linep, Pos* startpos, Pos* endpos) {
   CS p;
   CS delim_copy;
   Unt delim_len;
   LineNr   lnum;
   int found = false;

   for (p = linep + startpos->col + 1; *p && *p != '('; ++p)
      {} 
   delim_len = (p - linep) - startpos->col - 1;
   delim_copy = copySubstr(linep + startpos->col + 1, delim_len);
   if (!delim_copy)
      return false;
   for (lnum = startpos->lnum; lnum <= endpos->lnum; ++lnum) {
      CS line = ml_get(lnum);

      for (p = line + (lnum == startpos->lnum ? startpos->col + 1 : 0); *p; ++p) {
         if (lnum == endpos->lnum && (ColNr)(p - line) >= endpos->col)
            break;
         if (*p == ')' && STRNCMP(delim_copy, p + 1, delim_len) == 0 && p[delim_len + 1] == '"') {
            found = true;
            break;
         }
      }
      if (found)
         break;
   }
   eeglFree(delim_copy);
   return found;
}

//Check matchpairs option for "*initc".
//If there is a match set "*initc" to the matching character and "*findc" to
//the opposite character.  Set "*backwards" to the direction.
//When "switchit" is true swap the direction.
private void
find_mps_values(
   OUT Unt* initc,
   OUT Unt* findc,
   OUT int* backwards,
   int switchit
) {
   if (!curBook->o.matchPairs)
      return;
      
   CS ptr = curBook->o.matchPairs;
   while (*ptr != ZERO) {
      CS prev;

      if (mb_ptr2char(ptr) == *initc) {
         if (switchit) {
             *findc = *initc;
             *initc = mb_ptr2char(ptr + utfCharLen(ptr) + 1);
             *backwards = true;
         } else {
             *findc = mb_ptr2char(ptr + utfCharLen(ptr) + 1);
             *backwards = false;
         }
         return;
      }
      prev = ptr;
      ptr += utfCharLen(ptr) + 1;
      if (mb_ptr2char(ptr) == *initc) {
         if (switchit) {
            *findc = *initc;
            *initc = mb_ptr2char(prev);
            *backwards = false;
         } else {
            *findc = mb_ptr2char(prev);
            *backwards = true;
         }
         return;
      }
      ptr += utfCharLen(ptr);
      if (*ptr == ',')
          ++ptr;
   }
}

//findmatchlimit -- find the matching paren or brace, if it exists within
//maxtravel lines of the cursor.  A maxtravel of 0 means search until falling
//off the edge of the file.
//
//"initc" is the character to find a match for.  ZERO means to find the
//character at or after the cursor. Special values:
//'*'  look for C-style comment / *
//'/'  look for C-style comment / *, ignoring comment-end
//'#'  look for preprocessor directives
//'R'  look for raw string start: R"delim(text)delim" (only backwards)
//
//flags: FM_BACKWARD   search backwards (when initc is '/', '*' or '#')
//    FM_FORWARD   search forwards (when initc is '/', '*' or '#')
//    FM_BLOCKSTOP   stop at start/end of block ({ or } in column 0)
//    FM_SKIPCOMM   skip comments (not implemented yet!)
//
//"oap" is only used to set oap->motion_type for a linewise motion, it can be NULL
pub Pos*
findmatchlimit(
   Operator* oap,
   Unt initc,
   int flags,
   int maxtravel
) {
   Unt findc = 0;      // matching brace
   Unt c;
   int count = 0;      // cumulative number of braces
   int backwards = false;   // init for gcc
   int raw_string = false;   // search for raw string
   int inquote = false;   // true when inside quotes
   CS ptr;
   int do_quotes;      // check for quotes in current line
   int at_start;      // do_quotes value at start position
   int hash_dir = 0;      // Direction searched for # things
   int comment_dir = 0;   // Direction searched for comments
   Pos match_pos;      // Where last slash-star was found
   int start_in_quotes;   // start position is in quotes
   int traveled = 0;      // how far we've searched so far
   int ignore_cend = false;    // ignore comment end
   int match_escaped = 0;   // search for escaped match
   int dir;         // Direction to search
   int comment_col = MAXCOL;   // start of / / comment
   static Pos pos;
   pos = curPor->cursor;         // current search position
   pos.coladd = 0;
   CS linep = ml_get(pos.lnum);// pointer to current line

   // Direction to search when initc is '/', '*' or '#'
   if (flags & FM_BACKWARD)
      dir = BACKWARD;
   ei (flags & FM_FORWARD)
      dir = FORWARD;
   else
      dir = 0;

   //if initc given, look in the table for the matching character
   //'/' and '*' are special cases: look for start or end of comment.
   //When '/' is used, we ignore running backwards into an star-slash, for
   //"[*" command, we just want to find any comment.
   if (initc == '/' || initc == '*' || initc == 'R') {
      comment_dir = dir;
      if (initc == '/')
         ignore_cend = true;
      backwards = (dir == FORWARD) ? false : true;
      raw_string = (initc == 'R');
      initc = ZERO;
   } ei (initc != '#' && initc != ZERO) {
      find_mps_values(OUT &initc, OUT &findc, OUT &backwards, true);
      if (dir)
         backwards = (dir == FORWARD) ? false : true;
      if (findc == ZERO)
         return NULL;
   } else {
      // Either initc is '#', or no initc was given and we need to look under the cursor.
      if (initc == '#') {
         hash_dir = dir;
      } else {
         //initc was not given, must look for something to match under or near the cursor.
         //Only check for special things when 'cpo' doesn't have '%'.
         // Are we before or at #if, #else etc.?
         ptr = skipwhite(linep);
         if (*ptr == '#' && pos.col <= (ColNr)(ptr - linep)) {
             ptr = skipwhite(ptr + 1);
             if (   STRNCMP(ptr, "if", 2) == 0
            || STRNCMP(ptr, "endif", 5) == 0
            || STRNCMP(ptr, "el", 2) == 0)
            hash_dir = 1;
         }

         // Are we on a comment?
         ei (linep[pos.col] == '/') {
            if (linep[pos.col + 1] == '*') {
               comment_dir = FORWARD;
               backwards = false;
               pos.col++;
            } ei (pos.col > 0 && linep[pos.col - 1] == '*') {
               comment_dir = BACKWARD;
               backwards = true;
               pos.col--;
            }
         } ei (linep[pos.col] == '*') {
            if (linep[pos.col + 1] == '/') {
               comment_dir = BACKWARD;
               backwards = true;
            } ei (pos.col > 0 && linep[pos.col - 1] == '/') {
               comment_dir = FORWARD;
               backwards = false;
            }
         }

         //If we are not on a comment or the # at the start of a line, then
         //look for brace anywhere on this line after the cursor.
         if (!hash_dir && !comment_dir) {
            //Find the brace under or after the cursor.
            //If beyond the end of the line, use the last character in the line.
            if (linep[pos.col] == ZERO && pos.col)
                --pos.col;
            for (;;) {
               initc = mb_ptr2char(linep + pos.col);
               if (initc == ZERO)
                  break;

               find_mps_values(&initc, &findc, &backwards, false);
               if (findc)
                  break;
               pos.col += utfCharLen(linep + pos.col);
            }
            if (!findc) {
               // no brace in the line, maybe use "  #if" then
               if (*skipwhite(linep) == '#')
                  hash_dir = 1;
               else
                  return NULL;
            } else {
               int col, bslcnt = 0;

               // Set "match_escaped" if there are an odd number of backslashes.
               for (col = pos.col; check_prevcol(linep, col, '\\', &col);)
                  bslcnt++;
               match_escaped = (bslcnt & 1);
            }
         }
      }
      if (hash_dir) {
         //Look for matching #if, #else, #elif, or #endif
         if (oap)
            oap->motion_type = MLINE;   // Linewise for this case only
         if (initc != '#') {
            ptr = skipwhite(skipwhite(linep) + 1);
            if (STRNCMP(ptr, "if", 2) == 0 || STRNCMP(ptr, "el", 2) == 0)
                hash_dir = 1;
            ei (STRNCMP(ptr, "endif", 5) == 0)
                hash_dir = -1;
            else
                return NULL;
         }
         pos.col = 0;
         while (!gotInterruptG) {
            if (hash_dir > 0) {
                if (pos.lnum == curBook->mem.lineCount)
               break;
            }
            ei (pos.lnum == 1)
                break;
            pos.lnum += hash_dir;
            linep = ml_get(pos.lnum);
            line_breakcheck();   // check for CTRL-C typed
            ptr = skipwhite(linep);
            if (*ptr != '#')
                continue;
            pos.col = (ColNr) (ptr - linep);
            ptr = skipwhite(ptr + 1);
            if (hash_dir > 0) {
               if (STRNCMP(ptr, "if", 2) == 0)
                  count++;
               ei (STRNCMP(ptr, "el", 2) == 0) {
                  if (count == 0)
                     return &pos;
               } ei (STRNCMP(ptr, "endif", 5) == 0) {
                  if (count == 0)
                     return &pos;
                  count--;
               }
            } else {
               if (STRNCMP(ptr, "if", 2) == 0) {
                  if (count == 0)
                      return &pos;
                  count--;
               } ei (initc == '#' && STRNCMP(ptr, "el", 2) == 0) {
                  if (count == 0)
                      return &pos;
               } ei (STRNCMP(ptr, "endif", 5) == 0)
                  count++;
            }
         }
         return NULL;
      }
   }

   do_quotes = -1;
   start_in_quotes = MAYBE;
   CLEAR_POS(&match_pos);

   // backward search: Check if this line contains a single-line comment
   if ((backwards && comment_dir))
      comment_col = check_linecomment(linep);

   while (!gotInterruptG) {
      //Go to the next position, forward or backward. We could use
      //inc() and dec() here, but that is much slower
      if (backwards) {
         // char to match is inside of comment, don't search outside
         if (pos.col == 0) {      // at start of line, go to prev. one
            if (pos.lnum == 1)   // start of file
               break;
            --pos.lnum;

            if (maxtravel > 0 && ++traveled > maxtravel)
               break;

            linep = ml_get(pos.lnum);
            pos.col = ml_get_len(pos.lnum); // pos.col on trailing ZERO
            do_quotes = -1;
            line_breakcheck();

            // Check if this line contains a single-line comment
            if (comment_dir)
                comment_col = check_linecomment(linep);
         } else {
            --pos.col;
            pos.col -= (*mb_head_off)(linep, linep + pos.col);
         } 
      } else { // forward search
          if (linep[pos.col] == ZERO
             // at end of line, go to next one
          ){
            if (pos.lnum == curBook->mem.lineCount)  // end of file
               // line is exhausted and comment with it,
               // don't search for match in code
               break;
            ++pos.lnum;

            if (maxtravel && traveled++ > maxtravel)
                break;

            linep = ml_get(pos.lnum);
            pos.col = 0;
            do_quotes = -1;
            line_breakcheck();
         } else {
            pos.col += utfCharLen(linep + pos.col);
         }
      }

      //If FM_BLOCKSTOP given, stop at a '{' or '}' in column 0.
      if (pos.col == 0 && (flags & FM_BLOCKSTOP)
                      && (linep[0] == '{' || linep[0] == '}')) {
         if (linep[0] == findc && count == 0)   // match!
            return &pos;
         break;               // out of scope
      }

      if (comment_dir) {
         // Note: comments do not nest, and we ignore quotes in them
         // TODO: ignore comment brackets inside strings
         if (comment_dir == FORWARD) {
            if (linep[pos.col] == '*' && linep[pos.col + 1] == '/') {
               pos.col++;
               return &pos;
            }
         } else  {// Searching backwards
            //A comment may contain / * or / /, it may also start or end
            //with / * /.   Ignore a / * after / / and after *.
            if (pos.col == 0)
               continue;
            ei (raw_string) {
               if (linep[pos.col - 1] == 'R'
                  && linep[pos.col] == '"'
                  && firstOccurrence(linep + pos.col + 1, '(') != NULL
               ) {
                  //Possible start of raw string. Now that we have the
                  //delimiter we can check if it ends before where we
                  //started searching, or before the previously found raw string start.
                  if (!find_rawstring_end(linep, &pos, count > 0 ? &match_pos : &curPor->cursor)) {
                     count++;
                     match_pos = pos;
                     match_pos.col--;
                  }
                  linep = ml_get(pos.lnum); // may have been released
               }
            } ei (   linep[pos.col - 1] == '/'
                  && linep[pos.col] == '*'
                  && (pos.col == 1 || linep[pos.col - 2] != '*')
                  && (int)pos.col < comment_col
            ) {
               count++;
               match_pos = pos;
               match_pos.col--;
            } ei (linep[pos.col - 1] == '*' && linep[pos.col] == '/') {
               if (count > 0)
                  pos = match_pos;
               ei (pos.col > 1 && linep[pos.col - 2] == '/' && (int)pos.col <= comment_col)
                  pos.col -= 2;
               ei (ignore_cend)
                  continue;
               else
                  return NULL;
               return &pos;
            }
         }
         continue;
      }

      //Braces inside of quotes are ignored, but only if there is an even number of quotes in the line
      if (do_quotes == -1) {
         //Count the number of quotes in the line, skipping \" and '"'. Watch out for "\\".
         at_start = do_quotes;
         for (ptr = linep; *ptr; ++ptr) {
            if (ptr == linep + pos.col + backwards)
               at_start = (do_quotes & 1);
            if (*ptr == '"' && (ptr == linep || ptr[-1] != '\'' || ptr[1] != '\''))
               ++do_quotes;
            if (*ptr == '\\' && ptr[1] != ZERO)
               ++ptr;
         }
         do_quotes &= 1;       // result is 1 with even number of quotes

         //If we find an uneven count, check current line and previous one for a '\' at the end.
         if (!do_quotes) {
            inquote = false;
            if (ptr[-1] == '\\') {
               do_quotes = 1;
               if (start_in_quotes == MAYBE) {
                  // Do we need to use at_start here?
                  inquote = true;
                  start_in_quotes = true;
               } ei (backwards)
                  inquote = true;
            }
            if (pos.lnum > 1) {
               ptr = ml_get(pos.lnum - 1);
               if (*ptr && *(ptr + ml_get_len(pos.lnum - 1) - 1) == '\\') {
                  do_quotes = 1;
                  if (start_in_quotes == MAYBE) {
                     inquote = at_start;
                     if (inquote)
                        start_in_quotes = true;
                  } ei (!backwards)
                      inquote = true;
               }

               // ml_get() only keeps one line, need to get linep again
               linep = ml_get(pos.lnum);
            }
         }
      }
      if (start_in_quotes == MAYBE)
         start_in_quotes = false;

      //If 'smartmatch' is set:
      //Things inside quotes are ignored by setting 'inquote'. If we find a quote without a 
      //preceding '\' invert 'inquote'. At the end of a line not ending in '\' we reset 'inquote'.
      //
      //In lines with an uneven number of quotes (without preceding '\') we do not know which part
      //to ignore. Therefore we only set inquote if the number of quotes in a line is even, unless 
      //this line or the previous one ends in a '\'.  Complicated, isn't it?
      c = mb_ptr2char(linep + pos.col);
      switch (c) {
      case ZERO:
         // at end of line without trailing backslash, reset inquote
         if (pos.col == 0 || linep[pos.col - 1] != '\\') {
            inquote = false;
            start_in_quotes = false;
         }
         break;

      case '"':
         // a quote that is preceded with an odd number of backslashes is ignored
         if (do_quotes) {
            int col;

            for (col = pos.col - 1; col >= 0; --col) {
               if (linep[col] != '\\')
                  break;
            } 
            if ((((int)pos.col - 1 - col) & 1) == 0) {
                inquote = !inquote;
                start_in_quotes = false;
            }
         }
         break;

      //If smart matching ('cpoptions' does not contain '%'):
      //  Skip things in single quotes: 'x' or '\x'.  Be careful for single
      //  single quotes, eg jon's.  Things like '\233' or '\x3f' are not
      //  skipped, there is never a brace in them.
      //  Ignore this when finding matches for `'.
      case '\'':
         if (initc != '\'' && findc != '\'') {
            if (backwards) {
               if (pos.col > 1) {
                  if (linep[pos.col - 2] == '\'') {
                     pos.col -= 2;
                     break;
                  } ei (linep[pos.col - 2] == '\\' && pos.col > 2 && linep[pos.col - 3] == '\'') {
                     pos.col -= 3;
                     break;
                  }
               }
            } ei (linep[pos.col + 1]) {  // forward search
               if (linep[pos.col + 1] == '\\' && linep[pos.col + 2] && linep[pos.col + 3] == '\'') {
                  pos.col += 3;
                  break;
               } ei (linep[pos.col + 2] == '\'') {
                  pos.col += 2;
                  break;
               }
            }
         }
         // FALLTHROUGH

      default:
         //Check for match outside of quotes, and inside of
         //quotes when the start is also inside of quotes.
         if ((!inquote || start_in_quotes == true) && (c == initc || c == findc)) {
            int   col, bslcnt = 0;

            for (col = pos.col; check_prevcol(linep, col, '\\', &col);) {
               bslcnt++;
            }
            if ((bslcnt & 1) == match_escaped) {
               if (c == initc)
                  count++;
               else {
                  if (count == 0)
                     return &pos;
                  count--;
               }
            }
         }
      }
   }

   if (comment_dir == BACKWARD && count > 0) {
      pos = match_pos;
      return &pos;
   }
   return (Pos *)NULL;   // never found it
}

//Check if line[] contains a / / comment. Return MAXCOL if not, otherwise return the column.
pub int
check_linecomment(CS line) {
   CS p = line;
   while ((p = firstOccurrence(p, '/')) != NULL) {
      //Accept a double /, unless it's preceded with * and followed by
      //*, because * / / * is an end and start of a C comment.  Only
      //accept the position if it is not inside a string.
      if (p[1] == '/' && (p == line || p[-1] != '*' || p[2] != '*')
                && !is_pos_in_string(line, (ColNr)(p - line))
      )
         break;
      ++p;
   }

   if (!p)
      return MAXCOL;
   return (int)(p - line);
}

//Check if the pattern is zero-width.
//If move is true, check from the beginning of the buffer, else from position "cur".
//"direction" is FORWARD or BACKWARD. Return true, false or -1 for failure.
private int
is_zero_width(
   Text pattern,
   Boole move,
   Pos* cur,
   Unt direction
) {
   RegMultilineMatch   regmatch;
   int nmatched = 0;
   int result = -1;
   Pos pos;
   int called_emsg_before = called_emsg;
   int flag = 0;

   if (pattern.len == 0) {
      pattern = prevSearchPatternsP[last_idx].pat;
   }

   if (search_regcomp(pattern, NULL, RE_SEARCH, RE_SEARCH, SEARCH_KEEP, OUT &regmatch) == FAIL)
      return -1;

   // init startcol correctly
   regmatch.startpos[0].col = -1;
   // move to match
   if (move) {
      CLEAR_POS(&pos);
   } else {
      pos = *cur;
      // accept a match at the cursor position
      flag = SEARCH_START;
   }

   if (searchit(curPor, curBook, &pos, NULL, direction, pattern, 1,
              SEARCH_KEEP + flag, RE_SEARCH, NULL) != FAIL
   ) {
      // Zero-width pattern should match somewhere, then we can check if
      // start and end are in the same position.
      do {
          regmatch.startpos[0].col++;
          nmatched = eeRegexec_multi(&regmatch, curPor, curBook,
                   pos.lnum, regmatch.startpos[0].col, NULL);
          if (nmatched != 0)
         break;
      } while (regmatch.regprog != NULL
         && direction == FORWARD ? regmatch.startpos[0].col < pos.col
                     : regmatch.startpos[0].col > pos.col);

      if (called_emsg == called_emsg_before) {
          result = (nmatched != 0
         && regmatch.startpos[0].lnum == regmatch.endpos[0].lnum
         && regmatch.startpos[0].col == regmatch.endpos[0].col);
      }
   }

   eeRegFree(regmatch.regprog);
   return result;
}

//Find next search match under cursor, cursor at end.
//Used while an operator is pending, and in Visual mode.
pub int
current_search(long   count, Boole forward) {  // true for forward, false for backward
   Pos start_pos;   // start position of the pattern match
   Pos end_pos;   // end position of the pattern match
   Pos pos;      // position after the pattern
   int i;
   int dir;
   int result;      // result of various function calls
   int flags = 0;
   Pos   save_VIsual = VIsual;

   // When searching forward and the cursor is at the start of the Visual
   // area, skip the first search backward, otherwise it doesn't move.
   int skip_first_backward = forward && VIsual_active && LT_POS(curPor->cursor, VIsual);

   Pos orig_pos = pos = curPor->cursor;   //position of the cursor at beginning
   if (VIsual_active) {
      if (forward)
         incl(&pos);
      else
         decl(&pos);
   }

   // Is the pattern is zero-width?, this time, don't care about the direction
   int zero_width = is_zero_width(prevSearchPatternsP[last_idx].pat, true, &curPor->cursor, FORWARD);
   if (zero_width == -1)
      return FAIL;  // pattern not found

   //The trick is to first search backwards and then search forward again, so that a match at the 
   //current cursor position will be correctly captured. When "forward" is false do it the other 
   //way around.
   for (i = 0; i < 2; i++) {
      if (forward) {
         if (i == 0 && skip_first_backward)
            continue;
         dir = i;
      } else
         dir = !i;

      flags = 0;
      if (!dir && !zero_width)
         flags = SEARCH_END;
      end_pos = pos;

      // wrapping should not occur in the first round
      if (i == 0)
         wrapSearchG = false;

      result = searchit(curPor, curBook, &pos, &end_pos,
         (dir ? FORWARD : BACKWARD),
         prevSearchPatternsP[last_idx].pat, (long) (i ? count : 1),
         SEARCH_KEEP | flags, RE_SEARCH, NULL);

      wrapSearchG = true;

      // First search may fail, but then start searching from the
      // beginning of the file (cursor might be on the search match)
      // except when Visual mode is active, so that extending the visual
      // selection works.
      if (i == 1 && !result){ // not found, abort
          curPor->cursor = orig_pos;
          if (VIsual_active)
         VIsual = save_VIsual;
          return FAIL;
      } ei (i == 0 && !result) {
          if (forward) {
            // try again from start of buffer
            CLEAR_POS(&pos);
         }  else {
            // try again from end of buffer
            // searching backwards, so set pos to last line and col
            pos.lnum = curPor->book->mem.lineCount;
            pos.col  = ml_get_len(curPor->book->mem.lineCount);
          }
      }
   }

   start_pos = pos;

   if (!VIsual_active)
      VIsual = start_pos;

   // put the cursor after the match
   curPor->cursor = end_pos;
   if (LT_POS(VIsual, end_pos) && forward) {
      if (skip_first_backward)
         // put the cursor on the start of the match
         curPor->cursor = pos;
      else
         // put the cursor on last character of match
         dec_cursor();
   } ei (VIsual_active && LT_POS(curPor->cursor, VIsual) && forward)
      curPor->cursor = pos;   // put the cursor on the start of the match
   VIsual_active = true;
   VIsual_mode = 'v';

   if (p_fdo & FDO_SEARCH && keyWasTypedG)
      foldOpenCursor();

   setmouse();
   // Make sure the clipboard gets updated.  Needed because start and
   // end are still the same, and the selection needs to be owned
   clipboard.vmode = ZERO;
   drawCurBookLater(UPD_INVERTED);
   showmode();

   return OK;
}

// return true if line 'lnum' is empty or has white chars only.
pub int
linewhite(LineNr lnum) {
   CS p = skipwhite(ml_get(lnum));
   return (*p == ZERO);
}

//Add the search count "[3/19]" to "msgbuf". See update_search_stat() for other arguments.
private void
cmdline_search_stat(
   int dirc,
   Pos* pos,
   Pos* cursor_pos,
   int show_top_bot_msg,
   CS msgbuf,
   Unt msgbuflen,
   int recompute,
   int maxcount,
   long timeout
) {
   SearchFileStat stat;

   update_search_stat(dirc, pos, cursor_pos, &stat, recompute, maxcount, timeout);
   if (stat.cur <= 0)
      return;

   Byte t[SEARCH_STAT_BUF_LEN];
   Unt   len;

   if (stat.incomplete == 1)
      len = eeSnprintf(t, SEARCH_STAT_BUF_LEN, "[?/??]");
   ei (stat.cnt > maxcount && stat.cur > maxcount)
      len = eeSnprintf(t, SEARCH_STAT_BUF_LEN, "[>%d/>%d]", maxcount, maxcount);
   ei (stat.cnt > maxcount)
      len = eeSnprintf(t, SEARCH_STAT_BUF_LEN, "[%d/>%d]", stat.cur, maxcount);
   else
      len = eeSnprintf(t, SEARCH_STAT_BUF_LEN, "[%d/%d]", stat.cur, stat.cnt);

   if (show_top_bot_msg && len + 2 < SEARCH_STAT_BUF_LEN) {
      MEMMOVE(t + 2, t, len);
      t[0] = 'W';
      t[1] = ' ';
      len += 2;
   }

   if (len > msgbuflen)
      len = msgbuflen;
   MEMMOVE(msgbuf + msgbuflen - len, t, len);

   if (dirc == '?' && stat.cur == maxcount + 1)
      stat.cur = -1;

   // keep the message even after redraw, but don't put in history
   msg_hist_off = true;
   give_warning(msgbuf, false);
   msg_hist_off = false;
}

//Add the search count information to "stat". "stat" must not be NULL.
//When "recompute" is true always recompute the numbers.
//dirc == 0: don't find the next/previous match (only set the result to "stat")
//dirc == '/': find the next match
//dirc == '?': find the previous match
private void
update_search_stat(
   int dirc,
   Pos* pos,
   Pos* cursor_pos,
   SearchFileStat* stat,
   int recompute,
   int maxcount,
   long timeout
) {
   Pos p = (*pos);
   static Pos lastpos = {0, 0, 0};
   static int cur = 0;
   static int cnt = 0;
   static int exact_match = false;
   static int incomplete = 0;
   static int last_maxcount = 0;
   static int chgtick = 0;
   static CS lastpat = NULL;
   static Unt lastpatlen = 0;
   static Book* lBook = NULL;
   ProfTime  start;

   CLEAR_POINTER(stat);

   if (dirc == 0 && !recompute && !EMPTY_POS(lastpos)) {
      stat->cur = cur;
      stat->cnt = cnt;
      stat->exact_match = exact_match;
      stat->incomplete = incomplete;
      stat->last_maxcount = p_msc;
      return;
   }
   last_maxcount = maxcount;

   Boole wraparound = ((dirc == '?' && LT_POS(lastpos, p)) || (dirc == '/' && LT_POS(p, lastpos)));

   // If anything relevant changed the count has to be recomputed.
   if (!(chgtick == CHANGEDTICK(curBook)
         && (lastpat
             && STRNCMP(lastpat, prevSearchPatternsP[last_idx].pat.c, lastpatlen) == 0
             && lastpatlen == prevSearchPatternsP[last_idx].pat.len
         )
         && EQUAL_POS(lastpos, *cursor_pos)
         && lBook == curBook) 
      || wraparound || cur < 0
      || (maxcount > 0 && cur > maxcount) || recompute
   ) {
      cur = 0;
      cnt = 0;
      exact_match = false;
      incomplete = 0;
      CLEAR_POS(&lastpos);
      lBook = curBook;
   }

   // when searching backwards and having jumped to the first occurrence,
   // cur must remain greater than 1
   if (EQUAL_POS(lastpos, *cursor_pos) && !wraparound
         && (dirc == 0 || dirc == '/' ? cur < cnt : cur > 1))
      cur += dirc == 0 ? 0 : dirc == '/' ? 1 : -1;
   else {
      int done_search = false;
      Pos endpos = {0, 0, 0};

      wrapSearchG = false;
      if (timeout > 0)
         profile_setlimit(timeout, &start);
      while (!gotInterruptG 
            && searchit(
                  curPor, curBook, &lastpos, &endpos, FORWARD, (Text){null, 0}, 1, SEARCH_KEEP, RE_LAST, NULL
               ) != FAIL
      ) {
         done_search = true;
         // Stop after passing the time limit.
         if (timeout > 0 && profile_passed_limit(&start)) {
            incomplete = 1;
            break;
         }
         cnt++;
         if (LTOREQ_POS(lastpos, p)) {
            cur = cnt;
            if (LT_POS(p, endpos))
                exact_match = true;
         }
         fast_breakcheck();
         if (maxcount > 0 && cnt > maxcount) {
            incomplete = 2;    // max count exceeded
            break;
         }
      }
      if (gotInterruptG)
         cur = -1; // abort
      if (done_search) {
         eeglFree(lastpat);
         lastpat = 
            copySubstr(prevSearchPatternsP[last_idx].pat.c, prevSearchPatternsP[last_idx].pat.len);
         lastpatlen = prevSearchPatternsP[last_idx].pat.len;
         chgtick = CHANGEDTICK(curBook);
         lBook = curBook;
         lastpos = p;
      }
   }
   stat->cur = cur;
   stat->cnt = cnt;
   stat->exact_match = exact_match;
   stat->incomplete = incomplete;
   stat->last_maxcount = last_maxcount;
   wrapSearchG = true;
}

//Get line "lnum" and copy it into "buf[LSIZE]".
//The copy is made because the regexp may make the line invalid when using a mark.
private CS
get_line_and_copy(LineNr lnum, CS buf) {
   CS line = ml_get(lnum);
   copySubstrToAllocation(buf, (Text){line, LSIZE - 1});
   return buf;
}

//Find identifiers or defines in included files.
//If p_ic && compl_status_sol() then ptr must be in lowercase.
pub void
find_pattern_in_path(
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
){
   SearchedFile* bigger;      // When we need more space
   int      max_path_depth = 50;
   long   match_count = 1;

   CS pat;
   CS new_fname;
   CS curr_fname = curBook->currFileName;
   CS prev_fname = NULL;
   int      depth;
   int      depth_displayed;   // For type==CHECK_PATH
   int      old_files;
   int      already_searched;
   CS line;
   CS p;
   Byte   save_char;
   int      define_matched;
   RegMatch   regmatch;
   RegMatch   incl_regmatch;
   RegMatch   def_regmatch;
   int      matched = false;
   int      did_show = false;
   Boole      found = false;
   int      i;
   CS already = NULL;
   CS startp = NULL;
   CS inc_opt = NULL;
   Portal   *curPor_save = NULL;

   regmatch.regprog = NULL;
   incl_regmatch.regprog = NULL;
   def_regmatch.regprog = NULL;

   CS file_line = alloc(LSIZE);

   if (type != CHECK_PATH && type != FIND_DEFINE
       // when CONT_SOL is set compare "ptr" with the beginning of the
       // line is faster than quote_meta/regcomp/regexec "ptr" -- Acevedo
       && !compl_status_sol()
   ) {
      pat = alloc(len + 5);
      eeSnprintf(pat, len + 5, whole ? "\\<%.*s\\>" : "%.*s", len, ptr);
      // ignore case according to p_ic, p_scs and pat
      regmatch.rm_ic = ignorecase(pat);
      regmatch.regprog = compileRegexp(pat, RE_MAGIC);
      eeglFree(pat);
      if (regmatch.regprog == NULL)
          goto fpip_end;
   }
   inc_opt = curBook->o.includer;
   if (inc_opt) {
      incl_regmatch.regprog = compileRegexp(inc_opt, RE_MAGIC);
      if (incl_regmatch.regprog == NULL)
         goto fpip_end;
      incl_regmatch.rm_ic = false;   // don't ignore case in incl. pat.
   }
   if (type == FIND_DEFINE && curBook->o.definer) {
      def_regmatch.regprog = compileRegexp(curBook->o.definer, RE_MAGIC);
      if (!def_regmatch.regprog)
         goto fpip_end;
      def_regmatch.rm_ic = false;   // don't ignore case in define pat.
   }
   // Stack of included files
   Arr(SearchedFile) files = lallocZeroed(max_path_depth * sizeof(SearchedFile), true);
   old_files = max_path_depth;
   depth = depth_displayed = -1;

   LineNr lnum = start_lnum;
   if (end_lnum > curBook->mem.lineCount)
      end_lnum = curBook->mem.lineCount;
   if (lnum > end_lnum)      // do at least one line
      lnum = end_lnum;
   line = get_line_and_copy(lnum, file_line);

   for (;;) {
      if (incl_regmatch.regprog != NULL && eeRegexec(&incl_regmatch, line, (ColNr)0)){
         CS p_fname = (curr_fname == curBook->currFileName)
                        ? curBook->fullFileName : curr_fname;

         if (inc_opt != NULL && strstr((char *)inc_opt, "\\zs") != NULL)
            // Use text from '\zs' to '\ze' (or end) of 'include'.
            new_fname = find_file_name_in_path(incl_regmatch.startp[0],
                   (int)(incl_regmatch.endp[0] - incl_regmatch.startp[0]),
                   FNAME_EXP|FNAME_INCL|FNAME_REL, 1L, p_fname);
         else
            // Use text after match with 'include'.
            new_fname = file_name_in_line(incl_regmatch.endp[0], 0,
                    FNAME_EXP|FNAME_INCL|FNAME_REL, 1L, p_fname, NULL);
         already_searched = false;
         if (new_fname) {
            // Check whether we have already searched in this file
            for (i = 0;; i++) {
               if (i == depth + 1)
                  i = old_files;
               if (i == max_path_depth)
                  break;
               if (fullpathcmp(new_fname, files[i].name, true, true) & FPC_SAME) {
                  if (type != CHECK_PATH && action == ACTION_SHOW_ALL && files[i].matched) {
                     msg_putchar('\n');       // cursor below last one
                     if (!gotInterruptG)       // don't display if 'q'
                               // typed at "--more--" message
                      {
                        msg_home_replace_hl(new_fname);
                        msg_puts(_(" (includes previously listed match)"));
                        prev_fname = NULL;
                     }
                  }
                  EE_CLEAR(new_fname);
                  already_searched = true;
                  break;
               }
            }
         }

         if (type == CHECK_PATH 
               && (action == ACTION_SHOW_ALL || (new_fname == NULL && !already_searched))
         ) {
            if (did_show)
                msg_putchar('\n');       // cursor below last one
            else {
               gotoCommline(true);       // cursor at status line
               msg_puts_title(_("--- Included files "));
               if (action != ACTION_SHOW_ALL)
                  msg_puts_title(_("not found "));
               msg_puts_title(_("in path ---\n"));
            }
            did_show = true;
            while (depth_displayed < depth && !gotInterruptG) {
               ++depth_displayed;
               for (i = 0; i < depth_displayed; i++)
                  msg_puts(S"  ");
               msg_home_replace(files[depth_displayed].name);
               msg_puts(S" -->\n");
            }
            if (!gotInterruptG) { // don't display if 'q' typed for "--more--" message
               for (i = 0; i <= depth_displayed; i++)
                  msg_puts(S"  ");
               if (new_fname) {
                  // using "new_fname" is more reliable, e.g., when @includeexpr is set.
                  msgOuttransDeco(new_fname, getDecoFlags(HLF_D));
               } else {
                  //Isolate the file name. Include the surrounding "" or <> if present.
                  if (inc_opt != NULL && strstr((char *)inc_opt, "\\zs") != NULL) {
                     // pattern contains \zs, use the match
                     p = incl_regmatch.startp[0];
                     i = (int)(incl_regmatch.endp[0] - incl_regmatch.startp[0]);
                  } else {
                     // find the file name after the end of the match
                     for (p = incl_regmatch.endp[0]; *p && !eeIsFnameChar(*p); p++)
                        {}
                     for (i = 0; eeIsFnameChar(p[i]); i++)
                        {}
                  }

                  if (i == 0) {
                      // Nothing found, use the rest of the line.
                      p = incl_regmatch.endp[0];
                      i = (int)STRLEN(p);
                  }
                  //Avoid checking before the start of the line, can
                  //happen if \zs appears in the regexp.
                  ei (p > line) {
                     if (p[-1] == '"' || p[-1] == '<') {
                        --p;
                        ++i;
                     }
                     if (p[i] == '"' || p[i] == '>')
                        ++i;
                  }
                  save_char = p[i];
                  p[i] = ZERO;
                  msgOuttransDeco(p, getDecoFlags(HLF_D));
                  p[i] = save_char;
               }

               if (new_fname == NULL && action == ACTION_SHOW_ALL) {
                  if (already_searched)
                     msg_puts(_("  (Already listed)"));
                  else
                     msg_puts(_("  NOT FOUND"));
               }
            }
            out_flush();       // output each line directly
         }

         if (new_fname) {
            // Push the new file onto the file stack
            if (depth + 1 == old_files) {
               bigger = ALLOC_MULT(SearchedFile, max_path_depth * 2);
               for (i = 0; i <= depth; i++)
                   bigger[i] = files[i];
               for (i = depth + 1; i < old_files + max_path_depth; i++) {
                  bigger[i].fp = NULL;
                  bigger[i].name = NULL;
                  bigger[i].lnum = 0;
                  bigger[i].matched = false;
               }
               for (i = old_files; i < max_path_depth; i++)
                  bigger[i + max_path_depth] = files[i];
               old_files += max_path_depth;
               max_path_depth *= 2;
               eeglFree(files);
               files = bigger;
            }
            if ((files[depth + 1].fp = fopen((char *)new_fname, "r")) == NULL)
               eeglFree(new_fname);
            else {
               if (++depth == old_files) {
                  //lalloc() for 'bigger' must have failed above. We
                  //will forget one of our already visited files now.
                  eeglFree(files[old_files].name);
                  ++old_files;
               }
               files[depth].name = curr_fname = new_fname;
               files[depth].lnum = 0;
               files[depth].matched = false;
               if (action == ACTION_EXPAND && !silent) {
                  msg_hist_off = true;   // reset in msgTruncDeco()
                  eeSnprintf(IObuff, IOSIZE, _("Scanning included file: %s"), new_fname);
                  msgTruncDeco(IObuff, getDecoFlags(HLF_R));
               } ei (p_verbose >= 5) {
                  verbose_enter();
                  smsg(_("Searching included file %s"), (char *)new_fname);
                  verbose_leave();
               }

            }
         }
      } else {
         //Check if the line is a define (type == FIND_DEFINE)
         p = line;
   search_line:
         define_matched = false;
         if (def_regmatch.regprog != NULL && eeRegexec(&def_regmatch, line, (ColNr)0)) {
            //Pattern must be first identifier after 'define', so skip
            //to that position before checking for match of pattern.  Also
            //don't let it match beyond the end of this identifier.
            p = def_regmatch.endp[0];
            while (*p && !eeIsWordc(*p))
               p++;
            define_matched = true;
         }

         //Look for a match. Don't do this if we are looking for a
         //define and this line didn't match define_prog above.
         if (def_regmatch.regprog == NULL || define_matched) {
            if (define_matched || compl_status_sol()) {
               //compare the first "len" chars from "ptr"
               startp = skipwhite(p);
               if (p_ic)
                  matched = !caseInsensitiveCompareNChars(startp, ptr, len);
               else
                  matched = !STRNCMP(startp, ptr, len);
               if (matched && define_matched && whole && eeIsWordc(startp[len]))
                  matched = false;
            }
            ei (regmatch.regprog != NULL && eeRegexec(&regmatch, line, (ColNr)(p - line))) {
               matched = true;
               startp = regmatch.startp[0];
               //Check if the line is not a comment line (unless we are
               //looking for a define).  A line starting with "# define"
               //is not considered to be a comment line.
               if (!define_matched && skip_comments) {
                  if ((*line != '#' ||
                     STRNCMP(skipwhite(line + 1), "define", 6) != 0)
                     && get_leader_len(line, NULL, false, true))
                      matched = false;

                  //Also check for a "/ *" or "/ /" before the match.
                  //Skips lines like "int backwards;  / * normal index
                  //* /" when looking for "normal".
                  //Note: Doesn't skip "/ *" in comments.
                  p = skipwhite(line);
                  if (matched || (p[0] == '/' && p[1] == '*') || p[0] == '*') {
                     for (p = line; *p && p < startp; ++p) {
                        if (matched
                           && p[0] == '/'
                           && (p[1] == '*' || p[1] == '/')
                        ) {
                           matched = false;
                           // After "//" all text is comment
                           if (p[1] == '/')
                              break;
                            ++p;
                        } ei (!matched && p[0] == '*' && p[1] == '/') {
                            // Can find match after "* /".
                            matched = true;
                            ++p;
                        }
                     }
                  } 
               }
            }
         }
      }
      if (matched) {
         if (action == ACTION_EXPAND) {
            int   cont_s_ipos = false;
            int   add_r;

            if (depth == -1 && lnum == curPor->cursor.lnum)
               break;
            found = true;
            p = startp; 
            CS aux = p;
            if (compl_status_adding()) {
               p += ins_compl_len();
               if (eeIsWordPtr(p))
                  goto exit_matched;
               p = findWordStart(p);
            }
            p = find_word_end(p);
            i = (int)(p - aux);

            if (compl_status_adding() && i == ins_compl_len()) {
               // IOSIZE > compl_length, so the STRNCPY works
               STRNCPY(IObuff, aux, i);

               //Get the next line: when "depth" < 0  from the current buffer, otherwise from the 
               //included file. Jump to exit_matched when past the last line.
               if (depth < 0) {
                  if (lnum >= end_lnum)
                     goto exit_matched;
                  line = get_line_and_copy(++lnum, file_line);
               } ei (eeFgets(line = file_line, LSIZE, files[depth].fp))
                  goto exit_matched;

               //we read a line, set "already" to check this "line" later if depth >= 0 we'll 
               //increase files[depth].lnum far below  -- Acevedo
               already = aux = p = skipwhite(line);
               p = findWordStart(p);
               p = find_word_end(p);
               if (p > aux) {
                  if (*aux != ')' && IObuff[i-1] != TAB) {
                      if (IObuff[i-1] != ' ')
                         IObuff[i++] = ' ';
                      //IObuf =~ "\(\k\|\i\).* ", thus i >= 2
                  }
                  //copy as much as possible of the new word
                  if (p - aux >= IOSIZE - i)
                     p = aux + IOSIZE - i - 1;
                  STRNCPY(IObuff + i, aux, p - aux);
                  i += (int)(p - aux);
                  cont_s_ipos = true;
               }
               IObuff[i] = ZERO;
               aux = IObuff;

               if (i == ins_compl_len())
                  goto exit_matched;
            }

            add_r = ins_compl_add_infercase(
               aux, i, p_ic, curr_fname == curBook->currFileName ? NULL : curr_fname, dir, 
               cont_s_ipos, 0
            );
            if (add_r == OK)
               // if dir was BACKWARD then honor it just once
               dir = FORWARD;
            ei (add_r == FAIL)
               break;
          } ei (action == ACTION_SHOW_ALL) {
            found = true;
            if (!did_show)
               gotoCommline(true);      // cursor at status line
            if (curr_fname != prev_fname) {
               if (did_show)
                  msg_putchar('\n');   // cursor below last one
               if (!gotInterruptG)      // don't display if 'q' typed at "--more--" message
                  msg_home_replace_hl(curr_fname);
               prev_fname = curr_fname;
            }
            did_show = true;
            if (!gotInterruptG)
                show_pat_in_path(line, type, true, action,
                   (depth == -1) ? NULL : files[depth].fp,
                   (depth == -1) ? &lnum : &files[depth].lnum,
                   match_count++);

            // Set matched flag for this file and all the ones that
            // include it
            for (i = 0; i <= depth; ++i)
                files[i].matched = true;
         } ei (--count <= 0) {
            found = true;
            if (depth == -1 && lnum == curPor->cursor.lnum && g_do_tagpreview == 0)
               emsg(_(e_match_is_on_current_line));
            ei (action == ACTION_SHOW) {
               show_pat_in_path(
                  line, type, did_show, action, (depth == -1) ? NULL : files[depth].fp,
                   (depth == -1) ? &lnum : &files[depth].lnum, 1L
               );
               did_show = true;
            } else {
               // ":psearch" uses the preview portal
               if (g_do_tagpreview != 0) {
                  curPor_save = curPor;
                  prepare_tagpreview(true, true, false);
               }
               if (action == ACTION_SPLIT) {
                  if (splitPortal(0, 0) == FAIL)
                     break;
                  curPor->o.diff = false;
               }
               if (depth == -1) {
                  // match in current file
                  if (g_do_tagpreview != 0) {
                     if (!portalIsValid(curPor_save))
                        break;
                     if (!GETFILE_SUCCESS(getfile(
                             curPor_save->book->fiNum, NULL, NULL, true, lnum, forceit
                          ))
                     )
                        break;   // failed to jump to file
                  } else
                     setpcmark();
                  curPor->cursor.lnum = lnum;
                  check_cursor();
                }
               else {
                  if (!GETFILE_SUCCESS(getfile(
                           0, files[depth].name, NULL, true, files[depth].lnum, forceit
                         )
                      )
                  )
                     break;   // failed to jump to file
                  // autocommands may have changed the lnum, we don't want that here
                  curPor->cursor.lnum = files[depth].lnum;
               }
            }
            if (action != ACTION_SHOW) {
               curPor->cursor.col = (ColNr)(startp - line);
               curPor->setCursWant = true;
            }

            if (g_do_tagpreview != 0 && curPor != curPor_save && portalIsValid(curPor_save)) {
               //Return cursor to where we were
               validate_cursor();
               redraw_later(UPD_VALID);
               enterPortal(curPor_save, true);
            } ei (PORTAL_IS_POPUP(curPor))
               //can't keep focus in popup portal
               enterPortal(firstPor, true);
            break;
         }
   exit_matched:
         matched = false;
         //look for other matches in the rest of the line if we are not at the end of it already
         if (def_regmatch.regprog == NULL
                && action == ACTION_EXPAND
                && !compl_status_sol()
                && *startp != ZERO
                && *(startp + utfCharLen(startp)) != ZERO
         )
            goto search_line;
      }
      line_breakcheck();
      if (action == ACTION_EXPAND)
         ins_compl_check_keys(30, false);
      if (gotInterruptG || ins_compl_interrupted())
         break;

      //Read the next line.  When reading an included file and encountering
      //end-of-file, close the file and continue in the file that included it.
      while (depth >= 0 && !already && eeFgets(line = file_line, LSIZE, files[depth].fp)) {
         fclose(files[depth].fp);
         --old_files;
         files[old_files].name = files[depth].name;
         files[old_files].matched = files[depth].matched;
         --depth;
         curr_fname = (depth == -1) ? curBook->currFileName : files[depth].name;
         if (depth < depth_displayed)
            depth_displayed = depth;
      }
      if (depth >= 0) {     // we could read the line
          files[depth].lnum++;
          // Remove any CR and LF from the line.
          i = (int)STRLEN(line);
          if (i > 0 && line[i - 1] == '\n')
         line[--i] = ZERO;
          if (i > 0 && line[i - 1] == '\r')
         line[--i] = ZERO;
      } ei (!already) {
          if (++lnum > end_lnum)
         break;
          line = get_line_and_copy(lnum, file_line);
      }
      already = NULL;
   }
   // End of big for (;;) loop.

   // Close any files that are still open.
   for (i = 0; i <= depth; i++) {
      fclose(files[i].fp);
      eeglFree(files[i].name);
   }
   for (i = old_files; i < max_path_depth; i++)
      eeglFree(files[i].name);
   eeglFree(files);

   if (type == CHECK_PATH) {
      if (!did_show) {
         if (action != ACTION_SHOW_ALL)
            msg(_("All included files were found"));
         else
            msg(_("No included files"));
      }
   } ei (!found && action != ACTION_EXPAND && !silent) {
      if (gotInterruptG || ins_compl_interrupted())
          emsg(_(e_interrupted));
      ei (type == FIND_DEFINE)
          emsg(_(e_couldnt_find_definition));
      else
          emsg(_(e_couldnt_find_pattern));
   }
   if (action == ACTION_SHOW || action == ACTION_SHOW_ALL)
      msg_end();

fpip_end:
   eeglFree(file_line);
   eeRegFree(regmatch.regprog);
   eeRegFree(incl_regmatch.regprog);
   eeRegFree(def_regmatch.regprog);
}

private void
show_pat_in_path(
   CS  line,
   int       type,
   int       did_show,
   int       action,
   FILE* fp,
   LineNr* lnum,
   long    count
) {
   CS p;
   Unt linelen;

   if (did_show)
      msg_putchar('\n');   // cursor below last one
   ei (!msg_silent)
      gotoCommline(true);   // cursor at status line
   if (gotInterruptG)      // 'q' typed at "--more--" message
      return;
   linelen = STRLEN(line);
   for (;;) {
      p = line + linelen - 1;
      if (fp != NULL) {
          // We used fgets(), so get rid of newline at end
          if (p >= line && *p == '\n')
         --p;
          if (p >= line && *p == '\r')
         --p;
          *(p + 1) = ZERO;
      }
      if (action == ACTION_SHOW_ALL) {
          SPRINTF(IObuff, "%3ld: ", count);   // show match nr
          msg_puts(IObuff);
          SPRINTF(IObuff, FMT_UNT, *lnum);   // show line nr
                     // Highlight line numbers
          msgPutsDeco(IObuff, getDecoFlags(HLF_N));
          msg_puts(S" ");
      }
      msg_prt_line(line, false);
      out_flush();         // show one line at a time

      // Definition continues until line that doesn't end with '\'
      if (gotInterruptG || type != FIND_DEFINE || p < line || *p != '\\')
          break;

      if (fp) {
         if (eeFgets(line, LSIZE, fp)) // end of file
            break;
         linelen = STRLEN(line);
         ++*lnum;
      } else {
         if (++*lnum > curBook->mem.lineCount)
            break;
         line = ml_get(*lnum);
         linelen = ml_get_len(*lnum);
      }
      msg_putchar('\n');
   }
}

// Return the last used search pattern at "idx".
pub SearchPattern *
getPrevSearchPattern(int idx) {
   return &prevSearchPatternsP[idx];
}

//Return the last used search pattern index.
pub int
getPrevSearchOrSubstPattern(void) {
   return last_idx;
}

// "searchcount()" function
pub void
f_searchcount(Var *argvars, Var* returnVar) {
   Pos pos = curPor->cursor;
   CS pattern = NULL;
   int         maxcount = p_msc;
   long      timeout = SEARCH_STAT_DEF_TIMEOUT;
   int         recompute = true;
   SearchFileStat   stat;

   allocReturnDict(returnVar);

   if (argvars[0].tag != VAR_UNKNOWN) {
      ListItem   *li;
      Boole error = false;

      if (check_for_nonnull_dict_arg(argvars, 0) == FAIL)
         return;
      Bag* dict = argvars[0].bag;
      DictItem* di = bagFind(dict, tConst("timeout"));
      if (di) {
         timeout = (long)varGetNumberChk(&di->c, OUT &error);
         if (error)
            return;
      }
      di = bagFind(dict, tConst("maxcount"));
      if (di) {
         maxcount = (int)varGetNumberChk(&di->c, OUT &error);
         if (error)
            return;
      }
      recompute = bagGetBool(dict, tConst("recompute"), recompute);
      di = bagFind(dict, tConst("pattern"));
      if (di) {
         pattern = convertVarToStringSingleUse(&di->c);
         if (pattern == NULL)
            return;
      }
      di = bagFind(dict, tConst(S"pos"));
      if (di) {
         if (di->c.tag != VAR_LIST) {
            showErrFmtMsg(_(e_invalid_argument_str), "pos");
            return;
         }
         if (list_len(di->c.list) != 3) {
            showErrFmtMsg(_(e_invalid_argument_str), "List format should be [lnum, col, off]");
            return;
         }
         li = list_find(di->c.list, 0L);
         if (li) {
            pos.lnum = varGetNumberChk(&li->c, OUT &error);
            if (error)
                return;
         }
         li = list_find(di->c.list, 1L);
         if (li) {
            pos.col = varGetNumberChk(&li->c, OUT &error) - 1;
            if (error)
                return;
         }
         li = list_find(di->c.list, 2L);
         if (li) {
            pos.coladd = varGetNumberChk(&li->c, OUT &error);
            if (error)
               return;
         }
      }
   }

   save_last_search_pattern();
   save_incsearch_state();
   if (pattern) {
      if (*pattern == ZERO)
         goto the_end;
      eeglFree(prevSearchPatternsP[last_idx].pat.c);
      prevSearchPatternsP[last_idx].pat = pattern 
         ? (Text){copyStr(pattern), STRLEN(pattern)} : (Text){null, 0};
   }
   if (prevSearchPatternsP[last_idx].pat.len == 0)
      goto the_end;   // the previous pattern was never defined

   update_search_stat(0, &pos, &pos, &stat, recompute, maxcount, timeout);

   bagAddNumber(returnVar->bag, S"current", stat.cur);
   bagAddNumber(returnVar->bag, S"total", stat.cnt);
   bagAddNumber(returnVar->bag, S"exact_match", stat.exact_match);
   bagAddNumber(returnVar->bag, S"incomplete", stat.incomplete);
   bagAddNumber(returnVar->bag, S"maxcount", stat.last_maxcount);

the_end:
   restore_last_search_pattern();
   restore_incsearch_state();
}

//}}}
//{{{ match hilitin'

# define SEARCH_HL_PRIORITY 0

//Add match to the match list of portal "po".
//If "pat" is not NULL the pattern will be hilited with the group "grp" with priority "prio".
//If "pos_list" is not NULL, the list of posisions defines the hilites. Optionally, a desired 
//ID "id" can be specified (greater than or equal to 1). If no particular ID is desired, -1 must
//be specified for "id". Return ID of added match, -1 on failure.
private int
match_add(
   Portal* po,
   CS grp,
   CS pat,
   int prio,
   int id,
   List* pos_list
) {
   MatchItem* cur;
   RegProg* regprog = NULL;

   if (*grp == ZERO || (pat && *pat == ZERO))
      return -1;
   if (id < -1 || id == 0) {
      showErrFmtMsg(_(e_invalid_id_nr_must_be_greater_than_or_equal_to_one_1), id);
      return -1;
   }
   
   Unt rtype = UPD_SOME_VALID;
   if (id == -1) {
      // use the next available match ID
      id = po->nextMatchId++;
   } else {
      // check the given ID is not already in use
      for (cur = po->firstMatch; cur; cur = cur->next) {
         if (cur->id == id) {
            showErrFmtMsg(_(e_id_already_taken_nr), id);
            return -1;
         }
      } 

      // Make sure the next match ID is always higher than the highest manually selected ID. Add 
      // some extra in case a few more IDs are added soon.
      if (po->nextMatchId < id + 100)
         po->nextMatchId = id + 100;
   }

   Short hiId;
   if ((hiId = syntaxClusterByName((Text){.c = grp, .len = STRLEN(grp)})) == 0) {
      showErrFmtMsg(_(e_no_such_highlight_group_name_str), grp);
      return -1;
   }
   if (pat && (regprog = compileRegexp(pat, RE_MAGIC)) == NULL) {
      showErrFmtMsg(_(e_invalid_argument_str), pat);
      return -1;
   }

   // Build new match.
   MatchItem* m = ALLOC_CLEAR_ONE(MatchItem);
   if (pos_list && pos_list->len > 0) {
      m->pos = ALLOC_CLEAR_MULT(PosNoVirtLen, pos_list->len);
      m->posLen = pos_list->len;
   }
   m->id = id;
   m->priority = prio;
   m->pattern = pat ? copyStr(pat) : null;
   m->hiId = hiId;
   m->match.regprog = regprog;
   m->match.rmm_ic = false;
   m->match.rmm_maxcol = 0;

   // Set up position matches
   if (pos_list) {
      LineNr toplnum = 0;
      LineNr botlnum = 0;
      ListItem* li;
      CHECK_LIST_MATERIALIZE(pos_list);
      int i;
      for (i = 0, li = pos_list->first; li; i++, li = li->next) {
         LineNr lnum = 0;
         ColNr col = 0;
         int len = 1;
         List* subl;
         ListItem* subli;
         Boole error = false;

         if (li->c.tag == VAR_LIST) {
            subl = li->c.list;
            if (!subl)
               goto fail;
            subli = subl->first;
            if (!subli)
               goto fail;
            lnum = varGetNumberChk(&subli->c, OUT &error);
            if (error == true)
               goto fail;
            if (lnum == 0) {
               --i;
               continue;
            }
            m->pos[i].lnum = lnum;
            subli = subli->next;
            if (subli) {
               col = varGetNumberChk(&subli->c, OUT &error);
               if (error == true)
                  goto fail;
               subli = subli->next;
               if (subli) {
                  len = varGetNumberChk(&subli->c, OUT &error);
                  if (error == true)
                     goto fail;
               }
            }
            m->pos[i].col = col;
            m->pos[i].len = len;
         } ei (li->c.tag == VAR_NUMBER) {
            if (li->c.number == 0) {
                --i;
                continue;
            }
            m->pos[i].lnum = li->c.number;
            m->pos[i].col = 0;
            m->pos[i].len = 0;
         } else {
            emsg(_(e_list_or_number_required));
            goto fail;
         }
         if (toplnum == 0 || lnum < toplnum)
            toplnum = lnum;
         if (botlnum == 0 || lnum >= botlnum)
            botlnum = lnum + 1;
      }

      // Calculate top and bottom lines for redrawing area
      if (toplnum != 0) {
         redrawPortRangeLater(po, toplnum, botlnum);
         m->topLnum = toplnum;
         m->bottLnum = botlnum;
         rtype = UPD_VALID;
      }
   }

   // Insert new match.  The match list is in ascending order with regard to the match priorities.
   cur = po->firstMatch;
   MatchItem* prev = cur;
   while (cur && prio >= cur->priority) {
      prev = cur;
      cur = cur->next;
   }
   if (cur == prev)
      po->firstMatch = m;
   else
      prev->next = m;
   m->next = cur;

   redrawPortLater(po, rtype);
   return id;

fail:
   eeglFree(m->pattern);
   eeglFree(m->pos);
   eeglFree(m);
   return -1;
}

//Delete match with ID 'id' in the match list of portal 'po'.
//Print error messages if 'perr' is true.
private int
match_delete(Portal* po, int id, int perr) {
   if (id < 1) {
      if (perr == true)
         showErrFmtMsg(_(e_invalid_id_nr_must_be_greater_than_or_equal_to_one_2), id);
      return -1;
   }
   MatchItem* cur = po->firstMatch;
   MatchItem* prev = cur;
   for (; cur && cur->id != id; prev = cur, cur = cur->next) {
   }
   if (!cur) {
      if (perr == true)
         showErrFmtMsg(_(e_id_not_found_nr), id);
      return -1;
   }
   if (cur == prev)
      po->firstMatch = cur->next;
   else
      prev->next = cur->next;
   eeRegFree(cur->match.regprog);
   eeglFree(cur->pattern);
   
   Unt rtype = UPD_SOME_VALID;
   if (cur->topLnum != 0) {
      redrawPortRangeLater(po, cur->topLnum, cur->bottLnum);
      rtype = UPD_VALID;
   }
   eeglFree(cur->pos);
   eeglFree(cur);
   redrawPortLater(po, rtype);
   return 0;
}

// Delete all matches in the match list of portal 'po'.
pub void
clear_matches(Portal* po) {
   while (po->firstMatch) {
      MatchItem* m = po->firstMatch->next;
      eeRegFree(po->firstMatch->match.regprog);
      eeglFree(po->firstMatch->pattern);
      eeglFree(po->firstMatch->pos);
      eeglFree(po->firstMatch);
      po->firstMatch = m;
   }
   redrawPortLater(po, UPD_SOME_VALID);
}

// Get match from ID 'id' in portal 'po'. Return NULL if match not found.
private MatchItem*
get_match(Portal* po, int id) {
   MatchItem *cur;
   for (cur = po->firstMatch; cur && cur->id != id; cur = cur->next)
      {}
   return cur;
}

// Init for calling prepare_search_hl().
pub void
searchInitHilite(Portal* po, Match* search_hl) {
   // Setup for match and @hlsearch hiliting.  Disable any previous match
   MatchItem* cur = po->firstMatch;
   while (cur) {
      cur->mit_hl.rm = cur->match;
      if (cur->hiId == SHORT)
         cur->mit_hl.currHiId = SHORT;
      else
         cur->mit_hl.currHiId = cur->hiId;
      cur->mit_hl.book = po->book;
      cur->mit_hl.lnum = 0;
      cur->mit_hl.first_lnum = 0;
      cur = cur->next;
   }
   search_hl->book = po->book;
   search_hl->lnum = 0;
   search_hl->first_lnum = 0;
   // time limit is set at the toplevel, for all portals
}

// If there is a match fill "match" and return one. Return zero otherwise.
private int
next_search_hl_pos(
   OUT Match* match,   // points to a match
   LineNr lnum,
   MatchItem* matchItem,   // match item with positions
   ColNr mincol   // minimal column for a match
){
   int found = -1;
   for (int i = matchItem->currPos; i < matchItem->posLen; i++) {
      PosNoVirtLen* pos = &matchItem->pos[i];

      if (pos->lnum == 0)
         break;
      if (pos->len == 0 && pos->col < mincol)
         continue;
      if (pos->lnum == lnum) {
         if (found >= 0) {
            // if this match comes before the one at "found" then swap them
            if (pos->col < matchItem->pos[found].col) {
               PosNoVirtLen tmp = *pos;
               *pos = matchItem->pos[found];
               matchItem->pos[found] = tmp;
            }
         } else
            found = i;
      }
   }
   matchItem->currPos = 0;
   if (found >= 0) {
      ColNr start = matchItem->pos[found].col == 0 ? 0 : matchItem->pos[found].col - 1;
      ColNr end = matchItem->pos[found].col == 0 ? MAXCOL : start + matchItem->pos[found].len;

      match->lnum = lnum;
      match->rm.startpos[0].lnum = 0;
      match->rm.startpos[0].col = start;
      match->rm.endpos[0].lnum = 0;
      match->rm.endpos[0].col = end;
      match->is_addpos = true;
      match->has_cursor = false;
      matchItem->currPos = found + 1;
      return 1;
   }
   return 0;
}

//Search for a next 'hlsearch' or match.
//Uses match->buf.
//Sets match->lnum and match->rm contents.
//Note: Assumes a previous match is always before "lnum", unless match->lnum is zero.
//Careful: Any pointers for buffer lines will become invalid.
private void
next_search_hl(
   Portal* port,
   Match* search_hl,
   Match* match,   // points to search_hl or a match
   LineNr lnum,
   ColNr mincol,   // minimal column for a match
   MatchItem* cur   // to retrieve match positions if any
){
   ColNr matchcol;
   long nmatched;
   int called_emsg_before = called_emsg;
   int timed_out = false;

   // for :{range}s/pat only highlight inside the range
   if ((lnum < search_first_line || lnum > search_last_line) && cur == NULL) {
      match->lnum = 0;
      return;
   }

   if (match->lnum != 0) {
      //Check for three situations:
      //1. If the "lnum" is below a previous match, start a new search.
      //2. If the previous match includes "mincol", use it.
      //3. Continue after the previous match.
      LineNr l = match->lnum + match->rm.endpos[0].lnum - match->rm.startpos[0].lnum;
      if (lnum > l)
         match->lnum = 0;
      ei (lnum < l || match->rm.endpos[0].col > mincol)
         return;
   }

   // Repeat searching for a match until one is found that includes "mincol"
   // or none is found in this line.
   for (;;) {
      // Three situations:
      // 1. No useful previous match: search from start of line.
      // 2. Empty match: continue at next character.
      //    Break the loop if this is beyond the end of the line.
      if (match->lnum == 0)
         matchcol = 0;
      ei (match->rm.endpos[0].lnum == 0 && match->rm.endpos[0].col <= match->rm.startpos[0].col) {
         matchcol = match->rm.startpos[0].col;
         CS ml = memGetLine(match->book, lnum, false) + matchcol;
         if (*ml == ZERO) {
            ++matchcol;
            match->lnum = 0;
            break;
         }
         matchcol += utfCharLen(ml);
      } else
         matchcol = match->rm.endpos[0].col;

      match->lnum = lnum;
      if (match->rm.regprog != NULL) {
         // Remember whether match->rm is using a copy of the regprog in cur->match.
         int regprog_is_copy = (match != search_hl 
               && cur && match == &cur->mit_hl && cur->match.regprog == cur->mit_hl.rm.regprog);

         nmatched = eeRegexec_multi(&match->rm, port, match->book, lnum, matchcol, &timed_out);
         // Copy the regprog, in case it got freed and recompiled.
         if (regprog_is_copy)
            cur->match.regprog = cur->mit_hl.rm.regprog;

         if (called_emsg > called_emsg_before || gotInterruptG || timed_out) {
            // Error while handling regexp: stop using this regexp.
            if (match == search_hl) {
               // don't free regprog in the match list, it's a copy
               eeRegFree(match->rm.regprog);
               setHlsearch(false);
            }
            match->rm.regprog = NULL;
            match->lnum = 0;
            gotInterruptG = false;  // avoid the "Type :quit to exit Vim" message
            break;
         }
      } ei (cur)
         nmatched = next_search_hl_pos(match, lnum, cur, matchcol);
      else
         nmatched = 0;
      if (nmatched == 0) {
         match->lnum = 0;      // no match found
         break;
      }
      if (match->rm.startpos[0].lnum > 0
         || match->rm.startpos[0].col >= mincol
         || nmatched > 1
         || match->rm.endpos[0].col > mincol
      ) {
         match->lnum += match->rm.startpos[0].lnum;
         break;         // useful match found
      }
   }
}

// Advance to the match in portal "po" line "lnum" or past it.
pub void
prepare_search_hl(Portal* po, Match* search_hl, LineNr lnum) {
   Match* match;      // points to search_hl or a match
   Boole pos_inprogress;   // marks that position match search is in progress
   int n;

   // When using a multi-line pattern, start searching at the top
   // of the portal or just after a closed fold.
   // Do this both for search_hl and the match list.
   MatchItem* cur = po->firstMatch; //points to the match list
   Boole didHiliteSearch = PORTAL_IS_POPUP(po);  // skip search_hl in a popup portal
   while (cur || didHiliteSearch == false) {
      if (didHiliteSearch == false) {
         match = search_hl;
         didHiliteSearch = true;
      } else
         match = &cur->mit_hl;
      if (match->rm.regprog && match->lnum == 0 && re_multiline(match->rm.regprog)) {
         if (match->first_lnum == 0) {
            for (match->first_lnum = lnum;
                 match->first_lnum > po->topLine; --match->first_lnum
            )
               if (getFoldsPortal(po, match->first_lnum - 1, NULL, NULL, true, NULL))
                  break;
         }
         if (cur)
            cur->currPos = 0;
         pos_inprogress = true;
         n = 0;
         while (match->first_lnum < lnum && (match->rm.regprog || (cur && pos_inprogress))) {
            next_search_hl(po, search_hl, match, match->first_lnum, (ColNr)n,
                            match == search_hl ? NULL : cur);
            pos_inprogress = !(!cur || cur->currPos == 0);
            if (match->lnum != 0) {
                match->first_lnum = match->lnum
                      + match->rm.endpos[0].lnum
                      - match->rm.startpos[0].lnum;
                n = match->rm.endpos[0].col;
            } else {
                ++match->first_lnum;
                n = 0;
            }
         }
      }
      if (match != search_hl && cur)
          cur = cur->next;
   }
}

// Update "match->has_cursor" based on the match in "match" and the cursor position.
private void
check_cur_search_hl(Portal* po, Match* match) {
   LineNr linecount = match->rm.endpos[0].lnum - match->rm.startpos[0].lnum;

   if (po->cursor.lnum >= match->lnum
         && po->cursor.lnum <= match->lnum + linecount
         && (po->cursor.lnum > match->lnum || po->cursor.col >= match->rm.startpos[0].col)
         && (po->cursor.lnum < match->lnum + linecount || po->cursor.col < match->rm.endpos[0].col)
   )
      match->has_cursor = true;
   else
      match->has_cursor = false;
}

//Prepare for 'hlsearch' and match hiliting in one portal line.
//Return true if there is such hiliting and set "searchHiId" to the current hilite decoration.
pub Boole
searchPrepareHiliteLine(
   Portal* po,
   LineNr lnum,
   ColNr mincol,
   OUT CS* line,
   Match* search_hl,
   OUT Short* searchHiId
){
   Boole areaHiliting = false;

   //Handle hiliting the last used search pattern and matches.
   //Do this for both search_hl and the match list. Do not use search_hl in a popup portal.
   MatchItem* cur = po->firstMatch; //points to the match list
   Boole didHiliteSearch = PORTAL_IS_POPUP(po); //whether search_hl has been processed or not
   Match* match; //search_hl or a match
   while (cur || didHiliteSearch == false) {
      if (didHiliteSearch == false) {
         match = search_hl;
         didHiliteSearch = true;
      } else
         match = &cur->mit_hl;
      match->startcol = MAXCOL;
      match->endcol = MAXCOL;
      match->currHiId = SHORT;
      match->is_addpos = false;
      match->has_cursor = false;
      if (cur)
         cur->currPos = 0;
      next_search_hl(po, search_hl, match, lnum, mincol, match == search_hl ? NULL : cur);

      // Need to get the line again, a multi-line regexp may have made it invalid.
      *line = memGetLine(po->book, lnum, false);

      if (match->lnum != 0 && match->lnum <= lnum) {
         if (match->lnum == lnum)
            match->startcol = match->rm.startpos[0].col;
         else
            match->startcol = 0;
         if (lnum == match->lnum + match->rm.endpos[0].lnum - match->rm.startpos[0].lnum)
            match->endcol = match->rm.endpos[0].col;
         else
            match->endcol = MAXCOL;

         // check if the cursor is in the match before changing the columns
         if (match == search_hl)
            check_cur_search_hl(po, match);

         // Highlight one character for an empty match.
         if (match->startcol == match->endcol) {
            if ((*line)[match->endcol] != ZERO)
               match->endcol += utfCharLen((*line) + match->endcol);
            else
               match->endcol++;
         }
         if ((long)match->startcol < mincol) { // match at leftcol
            match->currHiId = match->currHiId;
            *searchHiId = match->currHiId;
         }
         areaHiliting = true;
      }
      if (match != search_hl && cur)
         cur = cur->next;
   }
   return areaHiliting;
}

// For a position in a line: Check for start/end of 'hlsearch' and other matches. After end, check 
// for start/end of next match. When another match, have to check for start again. Watch out for 
// matching an empty string! "onLastCol" is set to true with non-zero searchDeco and the next 
// column is endcol. Return the updated searchDeco.
pub Short
update_search_hl(
   Portal* po,
   LineNr lnum,
   ColNr col,
   OUT CS* line,
   Match* search_hl,
   int didLineDecorations,
   int lcs_eol_one,
   OUT Boole* onLastCol
) {
   Match* match;          // points to search_hl or a match
   Boole pos_inprogress;       // marks that position match search is in progress
   Short searchHiId = 0;

   // Do this for 'search_hl' and the match list (ordered by priority).
   MatchItem* cur = po->firstMatch; //the match list
   Boole didHiliteSearch = PORTAL_IS_POPUP(po); //whether search_hl has been processed or not
   while (cur || didHiliteSearch == false) {
      if (didHiliteSearch == false && (!cur || cur->priority > SEARCH_HL_PRIORITY)) {
         match = search_hl;
         didHiliteSearch = true;
      } else
         match = &cur->mit_hl;
      if (cur)
         cur->currPos = 0;
      pos_inprogress = true;
      while (match->rm.regprog != NULL || (cur && pos_inprogress)) {
         if (match->startcol != MAXCOL && col >= match->startcol && col < match->endcol) {
            int next_col = col + utfCharLen(*line + col);

            if (match->endcol < next_col)
               match->endcol = next_col;
            match->currHiId = match->currHiId;
            // Hilite the match were the cursor is using the CurSearch group.
            if (match == search_hl && match->has_cursor) {
               match->extra = OVERLAY_DECO_INVERT;
            }
         } ei (col == match->endcol) {
            match->currHiId = SHORT;
            next_search_hl(po, search_hl, match, lnum, col, match == search_hl ? NULL : cur);
            pos_inprogress = (cur && cur->currPos != 0);

            // Need to get the line again, a multi-line regexp may have made it invalid.
            *line = memGetLine(po->book, lnum, false);

            if (match->lnum == lnum) {
               match->startcol = match->rm.startpos[0].col;
               if (match->rm.endpos[0].lnum == 0)
                  match->endcol = match->rm.endpos[0].col;
               else
                  match->endcol = MAXCOL;

               //check if the cursor is in the match
               if (match == search_hl)
                  check_cur_search_hl(po, match);

               if (match->startcol == match->endcol) {
                  //hilite empty match, try again after it
                  CS p = *line + match->endcol;

                  if (*p == ZERO)
                     // consistent with non-mbyte
                     match->endcol++;
                  else
                     match->endcol += utfCharLen(p);
               }

                // Loop to check if the match starts at the
                // current position
                continue;
            }
         }
         break;
      }
      if (match != search_hl && cur)
          cur = cur->next;
   }

   // Use decorations from match with highest priority among 'search_hl' and the match list.
   cur = po->firstMatch;
   didHiliteSearch = PORTAL_IS_POPUP(po);
   while (cur || didHiliteSearch == false) {
      if (didHiliteSearch == false && (cur == NULL || cur->priority > SEARCH_HL_PRIORITY)){
         match = search_hl;
         didHiliteSearch = true;
      } else
         match = &cur->mit_hl;
      if (match->currHiId != SHORT) {
         searchHiId = match->currHiId;
         *onLastCol = col + 1 >= match->endcol;
      }
      if (match != search_hl && cur)
         cur = cur->next;
   }
   // Only highlight one character after the last column.
   if (*(*line + col) == ZERO && (didLineDecorations >= 1 || (po->o.list && lcs_eol_one == -1)))
      searchHiId = SHORT;
   return searchHiId;
}

pub int
get_prevcol_hl_flag(Portal* po, Match* search_hl, long curcol) {
   long prevcol = curcol;
   Boole prevcol_hl_flag = false;
   MatchItem* cur;         // points to the match list

   // don't do this in a popup portal
   if (portalIsPopup(po))
      return false;

   // we're not really at that column when skipping some text
   if ((long)(po->o.wrap ? po->skipCol : po->leftCol) > prevcol)
      ++prevcol;

   // Highlight a character after the end of the line if the match started
   // at the end of the line or when the match continues in the next line
   // (match includes the line break).
   if (!search_hl->is_addpos && (prevcol == (long)search_hl->startcol
      || (prevcol > (long)search_hl->startcol && search_hl->endcol == MAXCOL))
   )
      prevcol_hl_flag = true;
   else {
      cur = po->firstMatch;
      while (cur) {
         if (!cur->mit_hl.is_addpos && (prevcol == (long)cur->mit_hl.startcol
            || (prevcol > (long)cur->mit_hl.startcol && cur->mit_hl.endcol == MAXCOL))
         ){
            prevcol_hl_flag = true;
            break;
         }
         cur = cur->next;
      }
   }
   return prevcol_hl_flag;
}

// Get hiliting for the char after the text in "char_attr" from 'hlsearch' or match hiliting
pub void
get_search_match_hl(Portal* po, Match* search_hl, long col, OUT Short* charHiId) {
   MatchItem* cur = po->firstMatch;         // points to the match list
   Boole isPopup = PORTAL_IS_POPUP(po);  // flag to indicate whether search_hl has been processed or not
   Match* match; // points to search_hl or a match        
   while (cur || isPopup == false) {
      if (isPopup == false && ((cur && cur->priority > SEARCH_HL_PRIORITY) || !cur)){
         match = search_hl;
         isPopup = true;
      } else
         match = &cur->mit_hl;
      if (col - 1 == (long)match->startcol && (match == search_hl || !match->is_addpos))
         *charHiId = match->currHiId;
      if (match != search_hl && cur)
         cur = cur->next;
   }
}

private int
matchadd_dict_arg(Var* tv, OUT Portal** port) {
   DictItem* di;

   if (tv->tag != VAR_BAG) {
      emsg(_(e_dictionary_required));
      return FAIL;
   }


   if ((di = bagFind(tv->bag, tConst("window"))) == NULL)
      return OK;

   *port = portFindByNrOrId(&di->c);
   if (*port == NULL) {
      emsg(_(e_invalid_portal_number));
      return FAIL;
   }

   return OK;
}

pub void
f_clearmatches(Var* argvars, Var* returnVar UNUSED) {
   Portal* port = getOptionalPortal(argvars, 0);
   if (port)
      clear_matches(port);
}

pub void
f_getmatches(Var *argvars, Var* returnVar UNUSED) {
   Portal* port = getOptionalPortal(argvars, 0);
   if (!port)
      return;
      
   allocReturnList(returnVar);
   MatchItem* cur = port->firstMatch;
   while (cur) {
      Bag* bag = allocBag();
      if (!cur->match.regprog) {
         // match added with matchaddpos()
         for (int i = 0; i < cur->posLen; ++i) {
            Byte buf[30];  // use 30 to avoid compiler warning

            PosNoVirtLen* llpos = &cur->pos[i];
            if (llpos->lnum == 0)
               break;
            List* l = list_alloc();
            list_append_number(l, (Long)llpos->lnum);
            if (llpos->col > 0) {
               list_append_number(l, (Long)llpos->col);
               list_append_number(l, (Long)llpos->len);
            }
            SPRINTF(buf, S"pos%d", i + 1);
            bagAddList(bag, buf, l);
         }
      } else {
         bagAddString(bag, S"pattern", cur->pattern);
      }
      bagAddString(bag, S"group", syn_id2name(cur->hiId));
      bagAddNumber(bag, S"priority", (long)cur->priority);
      bagAddNumber(bag, S"id", (long)cur->id);
      listAppendBag(returnVar->list, bag);
      cur = cur->next;
   }
}

pub void
f_setmatches(Var *argvars, Var* returnVar) {
   List   *l;
   ListItem   *li;
   Bag   *d;
   List   *s = NULL;
   returnVar->number = -1;

   if (confirmVarIsList(argvars, 0) == FAIL)
      return;
   Portal* port = getOptionalPortal(argvars, 1);
   if (!port)
      return;

   if ((l = argvars[0].list) != NULL) {
      // To some extent make sure that we are dealing with a list from "getmatches()".
      li = l->first;
      while (li) {
         if (li->c.tag != VAR_BAG || (d = li->c.bag) == NULL) {
            emsg(_(e_invalid_argument));
            return;
         }
         if (!(bagHasKey(d, tConst("group"))
            && (bagHasKey(d, tConst("pattern")) || bagHasKey(d, tConst("pos1")))
            && bagHasKey(d, tConst("priority"))
            && bagHasKey(d, tConst("id")))
         ) {
            emsg(_(e_invalid_argument));
            return;
         }
         li = li->next;
      }

      clear_matches(port);
      li = l->first;
      while (li) {
         int i = 0;
         Byte buf[30];  // use 30 to avoid compiler warning
         DictItem  *di;
         CS group;
         int priority;
         int id;

         d = li->c.bag;
         if (!bagHasKey(d, tConst("pattern"))) {
            if (!s) {
               s = list_alloc();
            }

            // match from matchaddpos()
            for (i = 1; i < 9; i++) {
               sprintf((char *)buf, (char *)"pos%d", i);
               if ((di = bagFind(d, mbText(buf))) != NULL) {
                  if (di->c.tag != VAR_LIST)
                      return;

                  list_append_tv(s, &di->c);
                  s->refCount++;
               } else
                  break;
            }
         }

         group = bagGetString(d, tConst("group"), true);
         priority = (int)bagGetNumber(d, tConst("priority"));
         id = (int)bagGetNumber(d, tConst("id"));
         if (i == 0) {
            match_add(port, group, bagGetString(d, tConst("pattern"), false), priority, id, NULL);
         } else {
            match_add(port, group, NULL, priority, id, s);
            list_unref(s);
            s = NULL;
         }
         eeglFree(group);

         li = li->next;
      }
      returnVar->number = 0;
   }
}

pub void
f_matchadd(Var *argvars, Var* returnVar) {
   Byte buf[NUMBUFLEN];
   int prio = 10;   // default priority
   int id = -1;
   Boole error = false;
   Portal* port = curPor;

   returnVar->number = -1;

   CS grp = convertVarToString(&argvars[0], buf);   // group
   CS pat = convertVarToString(&argvars[1], buf);   // pattern
   if (grp == NULL || pat == NULL)
      return;
   if (argvars[2].tag != VAR_UNKNOWN) {
      prio = (int)varGetNumberChk(argvars + 2, OUT &error);
      if (argvars[3].tag != VAR_UNKNOWN) {
         id = (int)varGetNumberChk(argvars + 3, OUT &error);
         if (argvars[4].tag != VAR_UNKNOWN
               && matchadd_dict_arg(&argvars[4], &port) == FAIL)
            return;
      }
   }
   if (error == true)
      return;
   if (id >= 1 && id <= 3) {
      showErrFmtMsg(_(e_id_is_reserved_for_match_nr), id);
      return;
   }

   returnVar->number = match_add(port, grp, pat, prio, id, NULL);
}

// "matchaddpos()" function
pub void
f_matchaddpos(Var *argvars, Var* returnVar) {
   Byte buf[NUMBUFLEN];
   int prio = 10;
   int id = -1;
   Boole error = false;
   Portal* port = curPor;
   returnVar->number = -1;

   CS group = convertVarToString(&argvars[0], buf);
   if (group == NULL)
      return;

   if (argvars[1].tag != VAR_LIST) {
      showErrFmtMsg(_(e_argument_of_str_must_be_list), "matchaddpos()");
      return;
   }
   List* l = argvars[1].list;
   if (!l || l->len == 0)
      return;

   if (argvars[2].tag != VAR_UNKNOWN) {
      prio = (int)varGetNumberChk(argvars + 2, OUT &error);
      if (argvars[3].tag != VAR_UNKNOWN) {
         id = (int)varGetNumberChk(argvars + 3, OUT &error);

         if (argvars[4].tag != VAR_UNKNOWN && matchadd_dict_arg(&argvars[4], &port) == FAIL)
            return;
      }
   }
   if (error == true)
      return;

   // id == 3 is ok because matchaddpos() is supposed to substitute :3match
   if (id == 1 || id == 2) {
      showErrFmtMsg(_(e_id_is_reserved_for_match_nr), id);
      return;
   }

   returnVar->number = match_add(port, group, NULL, prio, id, l);
}

pub void
f_matcharg(Var* argvars, Var* returnVar) {
   allocReturnList(returnVar);

   MatchItem *m;

   int id = (int)tv_get_number(&argvars[0]);
   if (id >= 1 && id <= 3) {
      if ((m = get_match(curPor, id)) != NULL) {
         list_append_string(returnVar->list, syn_id2name(m->hiId), -1);
         list_append_string(returnVar->list, m->pattern, -1);
      } else {
         list_append_string(returnVar->list, NULL, -1);
         list_append_string(returnVar->list, NULL, -1);
      }
   }
}

pub void
f_matchdelete(Var *argvars UNUSED, Var* returnVar UNUSED) {
   Portal* port = getOptionalPortal(argvars, 1);
   if (!port)
      returnVar->number = -1;
   else
      returnVar->number = match_delete(port, (int)tv_get_number(&argvars[0]), true);
}

//":[N]match {group} {pattern}"
//called when skipping commands to find the next command.
pub void
c_match(Invocation* invo) {
   CS g = NULL;
   int c;
   int id;

   if (invo->line2 <= 3)
      id = invo->line2;
   else {
      emsg(_(e_invalid_command));
      return;
   }

   // First clear any old pattern.
   if (!invo->skip)
      match_delete(curPor, id, false);

   CS end;
   if (endsComm(invo->arg))
      end = invo->arg;
   ei ((STRNICMP(invo->arg, "none", 4) == 0
         && (SPACE_OR_TAB(invo->arg[4]) || endsComm(invo->arg + 4))))
      end = invo->arg + 4;
   else {
      CS p = skiptowhite(invo->arg);
      if (!invo->skip)
         g = copySubstr(invo->arg, p - invo->arg);
      p = skipwhite(p);
      if (*p == ZERO) {
         // There must be two arguments.
         eeglFree(g);
         showErrFmtMsg(_(e_invalid_argument_str), invo->arg);
         return;
      }
      end = skip_regexp(p + 1, *p, true);
      if (!invo->skip) {
         if (*end != ZERO && !endsComm(skipwhite(end + 1))) {
            eeglFree(g);
            invo->errmsg = ex_errmsg(e_trailing_characters_str, end);
            return;
         }
         if (*end != *p) {
            eeglFree(g);
            showErrFmtMsg(_(e_invalid_argument_str), p);
            return;
         }

         c = *end;
         *end = ZERO;
         match_add(curPor, g, p + 1, 10, id, NULL);
         eeglFree(g);
         *end = c;
      }
   }
}

//}}}
//{{{help file searchin'

// ":help": open a read-only portal on a help file
pub void
c_help(Invocation* invo) {
   CS arg;
   int      n;
   int      empty_fnum = 0;
   int      alt_fnum = 0;
   int len;
   int old_keyWasTypedG = keyWasTypedG;

   if (portErrorIfPopup(true))
      return;

   if (invo) {
      // A ":help" command ends at the first LF
      for (arg = invo->arg; *arg; ++arg) {
          if (*arg == '\n' || *arg == '\r') {
            *arg++ = ZERO;
            break;
         }
      }
      arg = invo->arg;

      if (invo->forceit && *arg == ZERO && curBook->kind != BOOK_HELP) {
          emsg(_(e_dont_panic));
          return;
      }

      if (invo->skip)       // not executing commands
          return;
   } else
      arg = S"";

   // remove trailing blanks
   CS p = arg + STRLEN(arg) - 1;
   while (p > arg && SPACE_OR_TAB(*p) && p[-1] != '\\') {
      *p-- = ZERO;
   }

   // Check for a specified language
   CS lang = check_help_lang(arg);

   // When no argument given go to the index.
   if (*arg == ZERO)
      arg = S"help.txt";

   // Check if there is a match for the argument.
   ExpandMatch matches = {};
   matches.a = createArena();
   n = find_help_tags(arg, invo && invo->forceit, OUT &matches);

   Unt i = 0;
   if (n != FAIL && lang != NULL) {
      // Find first item with the requested language.
      for (i = 0; i < matches.len; ++i) {
         len = (int)STRLEN(matches.c[i]);
         if (len > 3 
           && matches.c[i][len - 3] == '@'
           && caseInsensitiveCompare(matches.c[i] + len - 2, lang) == 0
         ) {
            break;
         }
      }
   } 
   if (i >= matches.len || n == FAIL) {
      if (lang)
         showErrFmtMsg(_(e_sorry_no_str_help_for_str), lang, arg);
      else
         showErrFmtMsg(_(e_sorry_no_help_for_str), arg);
      deleteArena(matches.a);
      return;
   }

   // The first match (in the requested language) is the best match.
   CS tag = copyStr(matches.c[i]);

   // Re-use an existing help portal or open a new one.
   // Always open a new one for ":tab help".
   if (!bookIsHelp(curPor->book) || commModifierG.cmod_tab != 0) {
   
      Portal   *po;
      if (commModifierG.cmod_tab != 0) {
         po = null;
      } else {
         FOR_ALL_PORTALS(po) {
            if (bookIsHelp(po->book)) {
               break;
            }
         } 
      }
      if (po && po->book->countPortals > 0)
          enterPortal(po, true);
      else {
         // There is no help portal yet. Try to open the file specified by the "helpfile" option.
         FILE* helpfd;   // file descriptor of help file
         if ((helpfd = fopen(MAIN_HELPFILE, READBIN)) == NULL) {
            smsg(_("Sorry, help file \"%s\" not found"), MAIN_HELPFILE);
            goto erret;
         }
         fclose(helpfd);

         // Split off help portal; put it at far top if no position
         // specified, the current portal is vertically split and narrow.
         n = WSP_HELP;
         if (commModifierG.cmod_split == 0 && curPor->width != visibleColsG && curPor->width < 80)
            n |= p_sb ? WSP_BOT : WSP_TOP;
         if (splitPortal(0, n) == FAIL)
            goto erret;

         if (curPor->height < p_hh)
            portSetHeight((int)p_hh, curPor);

         // Open help file (startEditingFile() will set kind = BOOK_HELP, readfile() will
         // set readonly flag). Set the alternate file to the previously edited file.
         alt_fnum = curBook->fiNum;
         (void)startEditingFile(0, NULL, NULL, NULL, ECMD_LASTL,
              ECMD_HIDE + ECMD_SET_HELP,
              NULL);  // buffer is still open, don't store info
         if ((commModifierG.cmod_flags & CMOD_KEEPALT) == 0)
            curPor->altFnum = alt_fnum;
         empty_fnum = curBook->fiNum;
      }
   }

   restart_edit = 0;       // don't want insert mode in help file

   // Restore keyWasTypedG, setting 'filetype=help' may reset it.
   // It is needed for do_tag top open folds under the cursor.
   keyWasTypedG = old_keyWasTypedG;

   if (tag != NULL)
      do_tag(tag, DT_HELP, 1, false, true);

   //Delete the empty book if we're not using it.  Careful: autocommands
   //may have jumped to another portal, check that the book is not in a portal.
   if (empty_fnum != 0 && curBook->fiNum != empty_fnum) {
      Book* book = bookFindFileByBookNr(empty_fnum);
      if (book && book->countPortals == 0)
          bookWipe(book, true);
   }

   // keep the previous alternate file
   if (alt_fnum != 0 && curPor->altFnum == empty_fnum && (commModifierG.cmod_flags & CMOD_KEEPALT) == 0)
      curPor->altFnum = alt_fnum;

erret:
   deleteArena(matches.a);
   eeglFree(tag);
}

// ":helpclose": Close one help portal
pub void
c_helpclose(Invocation* invo UNUSED) {
   Portal *port;

   FOR_ALL_PORTALS(port) {
      if (bookIsHelp(port->book)) {
         closePortal(port, false);
         return;
      }
   }
}

//In an argument search for a language specifiers in the form "@xx".
//Change the "@" to ZERO if found, and return a pointer to "xx". NULL if not found.
pub CS
check_help_lang(CS arg) {
   int len = (int)STRLEN(arg);

   if (len >= 3 && arg[len - 3] == '@' 
       && ASCII_ISALPHA(arg[len - 2]) && ASCII_ISALPHA(arg[len - 1])
   ){
      arg[len - 3] = ZERO;      // remove the '@'
      return arg + len - 2;
   }
   return NULL;
}

//Return a heuristic indicating how well the given string matches. The
//smaller the number, the better the match. This is the order of priorities,
//from best match to worst match:
//  - Match with least alphanumeric characters is better.
//  - Match with least total characters is better.
//  - Match towards the start is better.
//  - Match starting with "+" is worse (feature instead of command)
//Assumption is made that the matched_string passed has already been found to
//match some string for which help is requested.  webb.
pub int
help_heuristic(
   CS matched_string,
   int offset,         // offset for match
   int wrong_case      // no matching case
){
   int num_letters = 0;
   CS p;
   for (p = matched_string; *p; p++) {
      if (ASCII_ISALNUM(*p))
          num_letters++;
   } 

   //Multiply the number of letters by 100 to give it a much bigger
   //weighting than the number of characters.
   //If there only is a match while ignoring case, add 5000.
   //If the match starts in the middle of a word, add 10000 to put it somewhere in the last half.
   //If the match is more than 2 chars from the start, multiply by 200 to
   //put it after matches at the start.
   if (ASCII_ISALNUM(matched_string[offset]) && offset > 0
             && ASCII_ISALNUM(matched_string[offset - 1])
   )
      offset += 10000;
   ei (offset > 2)
      offset *= 200;
   if (wrong_case)
      offset += 5000;
   //Features are less interesting than the subjects themselves, but "+" alone is not a feature.
   if (matched_string[0] == '+' && matched_string[1] != ZERO)
      offset += 100;
   return (int)(100 * num_letters + STRLEN(matched_string) + offset);
}

//Compare functions for qsort() below, that checks the help heuristics number
//that has been put after the tagname by find_tags().
private int
helpCompare(const void *s1, const void *s2) {
   CS p1 = *(Byte **)s1 + strlen(*(char**)s1) + 1;
   CS p2 = *(Byte **)s2 + strlen(*(char**)s2) + 1;

   // Compare by help heuristic number first.
   int cmp = STRCMP(p1, p2);
   if (cmp != 0)
      return cmp;

   // Compare by strings as tie-breaker when same heuristic number.
   return strcmp(*(char **)s1, *(char **)s2);
}

//Find all help tags matching "arg", sort them and return in matches[], with
//the number of matches in num_matches.
//The matches will be sorted with a "best" match algorithm.
//When "keep_lang" is true try keeping the language of the current buffer.
pub int
find_help_tags(
   CS arg,
   int keep_lang,
   OUT ExpandMatch* matches
) {
   Byte   *s, *d;
   int      i;
   // Specific tags that either have a specific replacement or won't go
   // through the generic rules.
   static char *(except_tbl[][2]) = {
      {"*",      "star"},
      {"g*",      "gstar"},
      {"[*",      "[star"},
      {"]*",      "]star"},
      {":*",      ":star"},
      {"/*",      "/star"},
      {"/\\*",   "/\\\\star"},
      {"\"*",      "quotestar"},
      {"**",      "starstar"},
      {"cpo-*",   "cpo-star"},
      {"/\\(\\)",   "/\\\\(\\\\)"},
      {"/\\%(\\)",   "/\\\\%(\\\\)"},
      {"?",      "?"},
      {"??",      "??"},
      {":?",      ":?"},
      {"?<CR>",   "?<CR>"},
      {"g?",      "g?"},
      {"g?g?",   "g?g?"},
      {"g??",      "g??"},
      {"-?",      "-?"},
      {"q?",      "q?"},
      {"v_g?",   "v_g?"},
      {"/\\?",   "/\\\\?"},
      {"/\\z(\\)",   "/\\\\z(\\\\)"},
      {"\\=",      "\\\\="},
      {":s\\=",   ":s\\\\="},
      {"[count]",   "\\[count]"},
      {"[quotex]",   "\\[quotex]"},
      {"[range]",   "\\[range]"},
      {":[range]",   ":\\[range]"},
      {"[pattern]",   "\\[pattern]"},
      {"\\|",      "\\\\bar"},
      {"\\%$",   "/\\\\%\\$"},
      {"s/\\~",   "s/\\\\\\~"},
      {"s/\\U",   "s/\\\\U"},
      {"s/\\L",   "s/\\\\L"},
      {"s/\\1",   "s/\\\\1"},
      {"s/\\2",   "s/\\\\2"},
      {"s/\\3",   "s/\\\\3"},
      {"s/\\9",   "s/\\\\9"},
      {NULL, NULL}
   };
   static char *(expr_table[]) = {"!=?", "!~?", "<=?", "<?", "==?", "=~?",
               ">=?", ">?", "is?", "isnot?"};
   int flags;

   d = IObuff;          // assume IObuff is long enough!
   d[0] = ZERO;

   if (STRNICMP(arg, "expr-", 5) == 0) {
      //When the string starting with "expr-" and containing '?' and matches
      //the table, it is taken literally (but ~ is escaped). Otherwise '?'
      //is recognized as a wildcard.
      for (i = (int)ARRAY_LENGTH(expr_table); --i >= 0; ) {
         if (STRCMP(arg + 5, expr_table[i]) == 0) {
            int si = 0, di = 0;

            for (;;) {
                if (arg[si] == '~')
               d[di++] = '\\';
                d[di++] = arg[si];
                if (arg[si] == ZERO)
               break;
                ++si;
            }
            break;
          }
       }
   } else {
      // Recognize a few exceptions to the rule.  Some strings that contain
      // '*'are changed to "star", otherwise '*' is recognized as a wildcard.
      for (i = 0; except_tbl[i][0] != NULL; ++i) {
         if (STRCMP(arg, except_tbl[i][0]) == 0) {
            STRCPY(d, except_tbl[i][1]);
            break;
         }
      }
   }
    
   if (d[0] == ZERO) {// no match in table
      //Replace "\S" with "/\\S", etc.  Otherwise every tag is matched.
      //Also replace "\%^" and "\%(", they match every tag too.
      //Also "\zs", "\z1", etc.
      //Also "\@<", "\@=", "\@<=", etc.
      //And also "\_$" and "\_^".
      if (arg[0] == '\\'
         && ((arg[1] != ZERO && arg[2] == ZERO)
             || (firstOccurrence((CS)"%_z@", arg[1]) != NULL
                           && arg[2] != ZERO))
      ) {
         eeSnprintf(d, IOSIZE, "/\\\\%s", arg + 1);
         //Check for "/\\_$", should be "/\\_\$"
         if (d[3] == '_' && d[4] == '$')
            STRCPY(d + 4, "\\$");
      } else {
         //Replace:
         //"[:...:]" with "\[:...:]"
         //"[++...]" with "\[++...]"
         //"\{" with "\\{"         -- matching "} \}"
         if ((arg[0] == '[' && (arg[1] == ':'
             || (arg[1] == '+' && arg[2] == '+')))
             || (arg[0] == '\\' && arg[1] == '{'))
            *d++ = '\\';

         // If tag starts with "('", skip the "(". Fixes CTRL-] on ('option'.
         if (*arg == '(' && arg[1] == '\'')
            arg++;
         for (s = arg; *s; ++s)  {
            //Replace "|" with "bar" and '"' with "quote" to match the name of
            //the tags for these commands.
            //Replace "*" with ".*" and "?" with "." to match command line completion.
            //Insert a backslash before '~', '$' and '.' to avoid their special meaning.
            if (d - IObuff > IOSIZE - 10)   // getting too long!?
               break;
               
            switch (*s) {
            case '|':   STRCPY(d, "bar");
               d += 3;
               continue;
            case '"':   STRCPY(d, "quote");
               d += 5;
               continue;
            case '*':   *d++ = '.';
               break;
            case '?':   *d++ = '.';
               continue;
            case '$':
            case '.':
            case '~':   *d++ = '\\';
               break;
            }

            // Replace "^x" by "CTRL-X". Don't do this for "^_" to make
            // ":help i_^_CTRL-D" work.
            // Insert '-' before and after "CTRL-X" when applicable.
            if (*s < ' ' || (*s == '^' && s[1] && (ASCII_ISALPHA(s[1])
                  || firstOccurrence((CS)"?@[\\]^", s[1]) != NULL))
            ){
               if (d > IObuff && d[-1] != '_' && d[-1] != '\\')
                  *d++ = '_';      // prepend a '_' to make x_CTRL-x
               STRCPY(d, "CTRL-");
               d += 5;
               if (*s < ' ') {
                  *d++ = *s + '@';
                  if (d[-1] == '\\')
                     *d++ = '\\';   // double a backslash
               } else
                  *d++ = *++s;
               if (s[1] != ZERO && s[1] != '_')
                  *d++ = '_';      // append a '_'
               continue;
            } ei (*s == '^')      // "^" or "CTRL-^" or "^_"
               *d++ = '\\';

            //Insert a backslash before a backslash after a slash, for search
            //pattern tags: "/\|" --> "/\\|".
            ei (s[0] == '\\' && s[1] != '\\' && *arg == '/' && s == arg + 1)
               *d++ = '\\';

            // "CTRL-\_" -> "CTRL-\\_" to avoid the special meaning of "\_" in "CTRL-\_CTRL-N"
            if (STRNICMP(s, "CTRL-\\_", 7) == 0) {
               STRCPY(d, "CTRL-\\\\");
               d += 7;
               s += 6;
            }

            *d++ = *s;

            //If tag contains "({" or "([", tag terminates at the "(".
            //This is for help on functions, e.g.: abs({expr}).
            if (*s == '(' && (s[1] == '{' || s[1] =='['))
               break;

            //If tag starts with ', toss everything after a second '. Fixes
            //CTRL-] on 'option'. (would include the trailing '.').
            if (*s == '\'' && s > arg && *arg == '\'')
               break;
            //Also '{' and '}'.
            if (*s == '}' && s > arg && *arg == '{')
               break;
         }
         *d = ZERO;

         if (*IObuff == '`') {
            if (d > IObuff + 2 && d[-1] == '`') {
               // remove the backticks from `command`
               MEMMOVE(IObuff, IObuff + 1, STRLEN(IObuff));
               d[-2] = ZERO;
            } ei (d > IObuff + 3 && d[-2] == '`' && d[-1] == ',') {
               // remove the backticks and comma from `command`,
               MEMMOVE(IObuff, IObuff + 1, STRLEN(IObuff));
               d[-3] = ZERO;
            } ei (d > IObuff + 4 && d[-3] == '`' && d[-2] == '\\' && d[-1] == '.') {
               // remove the backticks and dot from `command`\.
               MEMMOVE(IObuff, IObuff + 1, STRLEN(IObuff));
               d[-4] = ZERO;
            }
         }
      }
   }

   *matches = (ExpandMatch){};
   flags = TAG_HELP | TAG_REGEXP | TAG_NAMES | TAG_VERBOSE | TAG_NO_TAGFUNC;
   if (keep_lang)
      flags |= TAG_KEEP_LANG;
   if (find_tags(IObuff, flags, (int)MAXCOL, NULL, OUT matches) == OK
       && matches->len > 0) {
      // Sort the matches found on the heuristic number that is after the tag name.
      qsort((void *)matches->c, (Unt)matches->len, sizeof(CS), helpCompare);
      // Delete more than TAG_MANY to reduce the size of the listing.
      while (matches->len > TAG_MANY) {
         --matches->len;
         eeglFree(matches->c[matches->len]);
      } 
   }
   return OK;
}

// Cleanup matches for help tags: Remove "@ab" if the top of 'helplang' is "ab" and the language 
// of the first tag matches it.  Otherwise remove "@en" if "en" is the only language.
pub void
cleanup_help_tags(OUT ExpandMatch* matches) {
   int len;
   Byte buf[4];
   buf[3] = ZERO;
   CS p = buf;

   if (p_hlg && (p_hlg[0] != 'e' || p_hlg[1] != 'n')) {
      *p++ = '@';
      *p++ = p_hlg[0];
      *p++ = p_hlg[1];
   }

   for (Unt i = 0; i < matches->len; ++i) {
      len = (int)STRLEN(matches->c[i]) - 3;
      if (len <= 0)
         continue;
      if (STRCMP(matches->c[i] + len, "@en") == 0) {
         //Sorting on priority means the same item in another language may
         //be anywhere. Search all items for a match up to the "@en".
         Unt j;
         for (j = 0; j < matches->len; ++j) {
            if (j != i && (int)STRLEN(matches->c[j]) == len + 3
                  && STRNCMP(matches->c[i], matches->c[j], len + 1) == 0
            )
                break;
         } 
         if (j == matches->len)
            //item only exists with @en, remove it
            matches->c[i][len] = ZERO;
      }
   }

   if (*buf != ZERO) {
      for (Unt i = 0; i < matches->len; ++i) {
         len = (int)STRLEN(matches->c[i]) - 3;
         if (len <= 0)
            continue;
         if (STRCMP(matches->c[i] + len, buf) == 0) {
            //remove the default language
            matches->c[i][len] = ZERO;
         }
      }
   } 
}

// Called when starting to edit a book for a help file.
pub void
prepare_help_buffer(void) {
   curBook->kind = BOOK_HELP;
   optSetByName(S"booktype", optEnum(BOOK_HELP), SET_LOCAL);

   //Always set these options after jumping to a help tag, because the
   //user may have an autocommand that gets in the way.
   //When adding an option here, also update the help file helphelp.txt.

   //Accept all ASCII chars for keywords, except ' ', '*', '"', '|', and
   //latin1 word characters (for translated help files).
   CS p = S"!-~,^*,^|,^\",192-255";
   if (curBook->o.isKeyword && STRCMP(curBook->o.isKeyword, p) != 0) {
      optChangeStringOptionDirect(S"iskeyword", p, OPT_LOCAL, 0);
   }

   curBook->o.shiftWidth = 3;      // tab size is 8
   curPor->o.list = false;   // no list mode

   curBook->o.binary = false;   // reset 'bin' before reading file
   curPor->o.relativeNumber = false;   // no relative line numbers
   curPor->o.foldEnable = false;   // No folding in the help portal
   curPor->o.diff = false;   // No 'diff', no scroll or cursor binding

   bookSetBooklisted(false);
}

// After reading a help file: May cleanup a help book when syntax highlighting is not used.
pub void
searchFixHelpBook(void) {
   CS line;
   int in_example = false;
   int len;

   // Set filetype to "help" if still needed.
   if (STRCMP(curBook->fileType, "help") != 0) {
      ++curBookLock;
      curBook->kind = BOOK_HELP;
      --curBookLock;
   }

   if (!syntax_present(curPor)) {
      for (LineNr lnum = 1; lnum <= curBook->mem.lineCount; ++lnum) {
         line = memGetLine(curBook, lnum, false);
         len = memGetBookLen(curBook, lnum);
         if (in_example && len > 0 && !SPACE_OR_TAB(line[0])) {
            // End of example: non-white or '<' in first column.
            if (line[0] == '<') {
               // blank-out a '<' in the first column
               line = memGetLine(curBook, lnum, true);
               line[0] = ' ';
            }
            in_example = false;
         }
         if (!in_example && len > 0) {
            if (line[len - 1] == '>' && (len == 1 || line[len - 2] == ' ')) {
               // blank-out a '>' in the last column (start of example)
               line = memGetLine(curBook, lnum, true);
               line[len - 1] = ' ';
               in_example = true;
            } ei (line[len - 1] == '~') {
               // blank-out a '~' at the end of line (header marker)
               line = memGetLine(curBook, lnum, true);
               line[len - 1] = ' ';
            }
         }
      }
   }

   //In the "help.txt" and "help.abx" file, add the locally added help
   //files. This uses the very first line in the help file.
   CS fname = fiGetShortFiName(curBook->currFileName);
   if (fnamecmp(fname, "help.txt") == 0
      || (STRNCMP(fname, "help.", 5) == 0
          && ASCII_ISALPHA(fname[5])
          && ASCII_ISALPHA(fname[6])
          && TOLOWER_ASC(fname[7]) == 'x'
          && fname[8] == ZERO)
   ){
      for (LineNr lnum = 1; lnum < curBook->mem.lineCount; ++lnum) {
         line = memGetLine(curBook, lnum, false);
         if (strstr((char *)line, "*local-additions*") == NULL)
            continue;

         // Go through all directories in 'runtimepath', skipping $EEGLRUNTIME.
         ExpandMatch files = {};
         files.a = createArena();
         FILE   *fd;
         CS s;
         CS cp;

         // Find all "doc/ *.help" files in this directory.
         STRCAT(nameBuffG, "*.??[help]");
         if (gen_expand_wildcards(1, &nameBuffG, EW_FILE|EW_SILENT, OUT &files) == OK
             && files.len > 0
         ){
            Unt i2;
            Unt i1;
            Byte   *f1, *f2;
            Byte   *t1, *t2;
            Byte   *e1, *e2;

            for (i1 = 0; i1 < files.len; ++i1) {
               f1 = files.c[i1];
               t1 = fiGetShortFiName(f1);
               e1 = lastOccurrence(t1, '.');
               if (e1 == NULL)
                  continue;
               if (fnamecmp(e1, ".help") != 0 && fnamecmp(e1, fname + 4) != 0) {
                  // Not .help, remove it.
                  EE_CLEAR(files.c[i1]);
                  continue;
               }

               for (i2 = i1 + 1; i2 < files.len; ++i2) {
                  f2 = files.c[i2];
                  if (f2 == NULL)
                     continue;
                  t2 = fiGetShortFiName(f2);
                  e2 = lastOccurrence(t2, '.');
                  if (e2 == NULL)
                     continue;
                  if (e1 - f1 != e2 - f2 || STRNCMP(f1, f2, e1 - f1) != 0)
                     continue;
                  if (fnamecmp(e1, ".txt") == 0 && fnamecmp(e2, fname + 4) == 0)
                      EE_CLEAR(files.c[i1]);
               }
            }
            for (Unt fi = 0; fi < files.len; ++fi) {
               if (files.c[fi] == NULL)
                  continue;
               fd = FOPEN(files.c[fi], "r");
               if (fd) {
                  eeFgets(IObuff, IOSIZE, fd);
                  if (IObuff[0] == '*' && (s = firstOccurrence(IObuff + 1, '*')) != NULL) {
                     int   this_utf = MAYBE;

                     // Change tag definition to a reference and remove <CR>/<NL>.
                     IObuff[0] = '|';
                     *s = '|';
                     while (*s != ZERO) {
                        if (*s == '\r' || *s == '\n')
                            *s = ZERO;
                        //The text is utf-8 when a byte above 127 is found and no
                        //illegal byte sequence is found.
                        if (*s >= 0x80 && this_utf != false) {
                           int   l;

                           this_utf = true;
                           l = utf_ptr2len(s);
                           if (l == 1)
                              this_utf = false;
                           s += l - 1;
                        }
                        ++s;
                     }

                     cp = IObuff;
                     ml_append(lnum, cp, (ColNr)0, false);
                     if (cp != IObuff)
                        eeglFree(cp);
                     ++lnum;
                  }
                  fclose(fd);
               }
            }
         }
         deleteArena(files.a);
      }
   }
}

// ":exusage"
pub void
c_exusage(Invocation* invo UNUSED) {
   executeCommLine(S"help ex-cmd-index");
}

// ":usage"
pub void
c_usage(Invocation* invo UNUSED) {
   executeCommLine(S"help normal-index");
}

// Generate tags in one help directory.
private void
generateHelpTagsForDir(
   CS dir,              //doc directory
   CS ext,              // suffix, ".txt", ".itx", ".frx", etc.
   CS tagfname,         //"tags" for English, "tags-fr" for French.
   int add_help_tags,   //add "help-tags" tag
   int ignore_writeerr  //ignore write error
){
   ArrayList   ga;
   CS p1;
   CS p2;
   CS s;
   int i;
   int utf8 = MAYBE;
   int this_utf8;
   int firstline;
   int in_example;
   int len;
   int mix = false;   // detected mixed encodings

   // Find all *.txt files.
   int dirlen = (int)STRLEN(dir);
   STRCPY(nameBuffG, dir);
   STRCAT(nameBuffG, "/**/*");
   STRCAT(nameBuffG, ext);
   
   ExpandMatch files = {};
   files.a = createArena();
   
   int res = gen_expand_wildcards(1, &nameBuffG, EW_FILE|EW_SILENT, OUT &files);
   if (res == FAIL || files.len == 0) {
      if (!gotInterruptG)
          showErrFmtMsg(_(e_no_match_str_1), nameBuffG);
      deleteArena(files.a);
      return;
   }

   //Open the tags file for writing. Do this before scanning through all the files.
   STRCPY(nameBuffG, dir);
   add_pathsep(nameBuffG);
   STRCAT(nameBuffG, tagfname);
   FILE* fd_tags = FOPEN(nameBuffG, "w");
   if (!fd_tags) {
      if (!ignore_writeerr)
         showErrFmtMsg(_(e_cannot_open_str_for_writing_1), nameBuffG);
      deleteArena(files.a);
      return;
   }

   //If using the "++t" argument or generating tags for "docs" add the "help-tags" tag.
   ga_init2(&ga, sizeof(CS), 100);
   if (add_help_tags || fullpathcmp(S"/usr/share/cim/doc", dir, false, true) == FPC_SAME){
      if (ga_grow(&ga, 1) == FAIL)
         gotInterruptG = true;
      else {
         s = alloc(18 + (unsigned)STRLEN(tagfname));
         SPRINTF(s, "help-tags\t%s\t1\n", tagfname);
         ((Byte **)ga.c)[ga.len] = s;
         ++ga.len;
      }
   }

   // Go over all the files and extract the tags.
   for (Unt fi = 0; fi < files.len && !gotInterruptG; ++fi) {
      FILE* fd = fopen((char *)files.c[fi], "r");
      if (!fd) {
         showErrFmtMsg(_(e_unable_to_open_str_for_reading), files.c[fi]);
         continue;
      }
      CS fname = files.c[fi] + dirlen + 1;

      in_example = false;
      firstline = true;
      while (!eeFgets(IObuff, IOSIZE, fd) && !gotInterruptG) {
         if (firstline) {
            // Detect utf-8 file by a non-ASCII char in the first line.
            this_utf8 = MAYBE;
            for (s = IObuff; *s != ZERO; ++s) {
               if (*s >= 0x80) {
                  this_utf8 = true;
                  int l = utf_ptr2len(s);
                  if (l == 1) {
                     // Illegal UTF-8 byte sequence.
                     this_utf8 = false;
                     break;
                  }
                  s += l - 1;
               }
            } 
            if (this_utf8 == MAYBE)       // only ASCII characters found
               this_utf8 = false;
            if (utf8 == MAYBE)       // first file
               utf8 = this_utf8;
            ei (utf8 != this_utf8) {
               showErrFmtMsg(_(e_mix_of_help_file_encodings_within_language_str), files.c[fi]);
               mix = !gotInterruptG;
               gotInterruptG = true;
            }
            firstline = false;
         }
         if (in_example) {
            // skip over example; a non-white in the first column ends it
            if (firstOccurrence((CS)" \t\n\r", IObuff[0]))
               continue;
            in_example = false;
         }
         p1 = firstOccurrence(IObuff, '*');   // find first '*'
         while (p1 != NULL) {
            //TODO Use eeStrbyte() instead of firstOccurrence() so that when
            //'encoding' is dbcs it still works, don't find '*' in the second byte.
            p2 = eeStrbyte(p1 + 1, '*');    // find second '*'
            if (p2 != NULL && p2 > p1 + 1) { // skip "*" and "**"
               for (s = p1 + 1; s < p2; ++s) {
                  if (*s == ' ' || *s == '\t' || *s == '|')
                     break;
               } 

               // Only accept a *tag* when it consists of valid
               // characters, there is white space before it and is
               // followed by a white character or end-of-line.
               if (s == p2
                   && (p1 == IObuff || p1[-1] == ' ' || p1[-1] == '\t')
                   && (firstOccurrence((CS)" \t\n\r", s[1]) != NULL
                  || s[1] == '\0')
               ) {
                  *p2 = '\0';
                  ++p1;
                  if (ga_grow(&ga, 1) == FAIL) {
                     gotInterruptG = true;
                     break;
                  }
                  s = alloc(p2 - p1 + STRLEN(fname) + 2);
                  ((Byte **)ga.c)[ga.len] = s;
                  ++ga.len;
                  sprintf((char *)s, "%s\t%s", p1, fname);

                  // find next '*'
                  p2 = firstOccurrence(p2 + 1, '*');
               }
            }
            p1 = p2;
         }
         len = (int)STRLEN(IObuff);
         if ((len == 2 && STRCMP(&IObuff[len - 2], ">\n") == 0)
                || (len >= 3 && STRCMP(&IObuff[len - 3], " >\n") == 0))
            in_example = true;
         line_breakcheck();
      }

      fclose(fd);
   }

   deleteArena(files.a);

   if (!gotInterruptG) {
      // Sort the tags.
      if (ga.c)
         sortStrings((Byte **)ga.c, ga.len);

      // Check for duplicates.
      for (i = 1; i < ga.len; ++i) {
         p1 = ((Byte **)ga.c)[i - 1];
         p2 = ((Byte **)ga.c)[i];
         while (*p1 == *p2) {
            if (*p2 == '\t') {
               *p2 = ZERO;
               eeSnprintf(nameBuffG, MAXPATHL,
                  _(e_duplicate_tag_str_in_file_str_str),
                      ((Byte **)ga.c)[i], dir, p2 + 1);
               emsg(nameBuffG);
               *p2 = '\t';
               break;
            }
            ++p1;
            ++p2;
         }
      }

      if (utf8 == true)
          fprintf(fd_tags, "!_TAG_FILE_ENCODING\tutf-8\t//\n");

      // Write the tags into the file.
      for (i = 0; i < ga.len; ++i) {
         s = ((Byte **)ga.c)[i];
         if (STRNCMP(s, "help-tags\t", 10) == 0)
            // help-tags entry was added in formatted form
            fputs((char *)s, fd_tags);
         else {
            fprintf(fd_tags, "%s\t/*", s);
            for (p1 = s; *p1 != '\t'; ++p1) {
                // insert backslash before '\\' and '/'
                if (*p1 == '\\' || *p1 == '/')
               putc('\\', fd_tags);
                putc(*p1, fd_tags);
            }
            fprintf(fd_tags, "*\n");
         }
      }
   }
   if (mix)
      gotInterruptG = false;    // continue with other languages

   for (i = 0; i < ga.len; ++i)
      eeglFree(((Byte **)ga.c)[i]);
   ga_clear(&ga);
   fclose(fd_tags);       // there is no check for an error...
}

// Generate tags in one help directory, taking care of translations.
private void
do_helptags(CS dirname, int add_help_tags, int ignore_writeerr) {
   int      len;
   int      j;
   ArrayList   ga;
   Byte lang[2];
   Byte ext[5];
   Byte fname[8];
   ExpandMatch files = {};

   // Get a list of all files in the help directory and in subdirectories.
   STRCPY(nameBuffG, dirname);
   add_pathsep(nameBuffG);
   STRCAT(nameBuffG, "**");
   if (gen_expand_wildcards(1, &nameBuffG, EW_FILE|EW_SILENT, OUT &files) == FAIL
       || files.len == 0
   ) {
      showErrFmtMsg(_(e_no_match_str_1), nameBuffG);
      return;
   }

   // Go over all files in the directory to find out what languages are present.
   ga_init2(&ga, 1, 10);
   for (Unt i = 0; i < files.len; ++i) {
      len = (int)STRLEN(files.c[i]);
      if (len <= 4)
          continue;

      if (caseInsensitiveCompare(files.c[i] + len - 4, ".txt") == 0) {
         // ".txt" -> language "en"
         lang[0] = 'e';
         lang[1] = 'n';
      } else
         continue;

      // Did we find this language already?
      for (j = 0; j < ga.len; j += 2) {
         if (STRNCMP(lang, ((CS)ga.c) + j, 2) == 0)
            break;
      } 
      if (j == ga.len) {
         // New language, add it.
         if (ga_grow(&ga, 2) == FAIL)
            break;
         ((CS)ga.c)[ga.len++] = lang[0];
         ((CS)ga.c)[ga.len++] = lang[1];
      }
   }

   // Loop over the found languages to generate a tags file for each one.
   for (j = 0; j < ga.len; j += 2) {
      STRCPY(fname, "tags-xx");
      fname[5] = ((CS)ga.c)[j];
      fname[6] = ((CS)ga.c)[j + 1];
      if (fname[5] == 'e' && fname[6] == 'n') {
          // English is an exception: use ".txt" and "tags".
          fname[4] = ZERO;
          STRCPY(ext, ".txt");
      } else {
          // Language "ab" uses ".abx" and "tags-ab".
          STRCPY(ext, ".xxx");
          ext[1] = fname[5];
          ext[2] = fname[6];
      }
      generateHelpTagsForDir(dirname, ext, fname, add_help_tags, ignore_writeerr);
   }

   ga_clear(&ga);
   deleteArena(files.a); 
}

private void
helptagsCb(CS fname, void* cookie) {
   do_helptags(fname, *(int *)cookie, true);
}

// ":helptags"
pub void
c_helptags(Invocation* invo) {
   Expand expand;
   CS dirname;
   Boole add_help_tags = false;

   // Check for ":helptags ++t {dir}".
   if (STRNCMP(invo->arg, "++t", 3) == 0 && SPACE_OR_TAB(invo->arg[3])) {
      add_help_tags = true;
      invo->arg = skipwhite(invo->arg + 3);
   }

   if (STRCMP(invo->arg, "ALL") == 0) {
      doInPath(S"/usr/share/doc/", E, S"eegl", DIP_ALL + DIP_DIR, helptagsCb, &add_help_tags);
   } else {
      expandInit(&expand);
      expand.context = EXPAND_DIRECTORIES;
      dirname = expandWildcard(
            OUT &expand, invo->arg, NULL, WILD_LIST_NOTFOUND|WILD_SILENT, WILD_EXPAND_FREE
      );
      if (dirname == NULL || !mch_isdir(dirname))
         showErrFmtMsg(_(e_not_a_directory_str), invo->arg);
      else
         do_helptags(dirname, add_help_tags, false);
      eeglFree(dirname);
   }
}

//}}}
