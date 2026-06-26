//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## do.c: functions for executing Commands

#include "eegl.h"
#include <float.h>
int stat(const char* restrict path, struct stat* restrict buf);
int mkdir(const char* pathname, mode_t mode);

private Boole anySyntaxEmsgS; // anyEmsgG set because of a syntax error

//{{{forward decls

private int linelen(int *has_tab);
private void 
do_filter(LineNr line1, LineNr line2, Invocation* invo, CS cmd, Boole do_in, Boole do_out);
private Boole isWritingForbidden(void);
private Boole check_readonly(OUT Boole* forceit, Book* book);
private void delbuf_msg(CS name);
private int u_inssub(LineNr lnum);
private Boole wasBookChangedNotTerm(Book *book);
private void mayPrint(Invocation* invo);
private void closePortalInternal(Portal* port, Tab* t);

//}}}
//{{{sortin' and filterin'

// ":ascii" and "ga".
void
do_ascii(Invocation* invo UNUSED){
   int cval;
   Byte buf1[20];
   Byte buf2[20];
   Byte buf3[7];
   int cc[MAX_COMBINED_SYMBOLS];
   int ci = 0;
   int len;

   int c = utfc_ptr2char(ml_get_cursor(), cc);
   if (c == ZERO) {
      msg((CS)"ZERO");
      return;
   }

   IObuff[0] = ZERO;
   if (c < 0x80) {
      if (c == NL)       // ZERO is stored as NL
         c = ZERO;
      else
         cval = c;
      if (bookIsCharPrintable_strict(c) && (c < ' ' || c > '~')) {
         transchar_nonprint(buf3, c);
         eeSnprintf(buf1, sizeof(buf1), "  <%s>", (char *)buf3);
      } else
         buf1[0] = ZERO;
      if (c >= 0x80) {
         eeSnprintf(buf2, sizeof(buf2), "  <M-%s>", (char *)transchar(c & 0x7f));
      } else {
         buf2[0] = ZERO;
      } 
      eeSnprintf(
         IObuff, IOSIZE, _("<%s>%s%s  %d,  Hex %02x"), transchar(c), buf1, buf2, cval, cval
      );
      c = cc[ci++];
   }

   // Repeat for combining characters.
   while (c >= 0x100) {
      len = (int)STRLEN(IObuff);
      // This assumes every multi-byte char is printable...
      if (len > 0)
         IObuff[len++] = ' ';
      IObuff[len++] = '<';
      if (utf_iscomposing(c))
         IObuff[len++] = ' '; // draw composing char on top of a space
      len += mb_char2bytes(c, IObuff + len);
          eeSnprintf(IObuff + len, IOSIZE - len,
             c < 0x10000 ? _("> %d, Hex %04x")
                    : _("> %d, Hex %08x"),
                    c, c);
      if (ci == MAX_COMBINED_SYMBOLS)
         break;
      c = cc[ci++];
   }

   msg(IObuff);
}

//":left", ":center" and ":right": align text.
void
c_align(Invocation* invo) {
   int      len;
   int      indent = 0;
   int width = atoi((char *)invo->arg);
   Pos save_curpos = curPor->cursor;
   if (invo->id == C_left) {   // width is used for new indent
      if (width >= 0)
         indent = width;
   } else {
   //if 'textwidth' set, use it
   //ei 'wrapmargin' set, use it
   //if invalid value, use 80
   if (width <= 0)
       width = curBook->o.textWidth;
   if (width == 0 && curBook->o.wrapMargin > 0)
       width = curPor->width - curBook->o.wrapMargin;
   if (width <= 0)
       width = 80;
   }

   if (u_save((LineNr)(invo->line1 - 1), (LineNr)(invo->line2 + 1)) == FAIL)
      return;

   int new_indent;
   for (curPor->cursor.lnum = invo->line1; curPor->cursor.lnum <= invo->line2; 
         ++curPor->cursor.lnum
   ) {
      if (invo->id == C_left)      // left align
         new_indent = indent;
      else {
         int has_tab = false;   // avoid uninit warnings
         len = linelen(invo->id == C_right ? &has_tab : NULL) - get_indent();

         if (len <= 0)         // skip blank lines
            continue;

         if (invo->id == C_center)
            new_indent = (width - len) / 2;
         else {
            new_indent = width - len;   // right align

            //Make sure that embedded TABs don't make the text go too far to the right.
            if (has_tab) {
               while (new_indent > 0) {
                  (void)set_indent(new_indent, 0);
                  if (linelen(NULL) <= width) {
                     //Now try to move the line as much as possible to the right. Stop when it moves too far.
                     do
                        (void)set_indent(++new_indent, 0);
                     while (linelen(NULL) <= width);
                     --new_indent;
                     break;
                  }
                  --new_indent;
               }
            } 
         }
      }
      if (new_indent < 0)
         new_indent = 0;
      (void)set_indent(new_indent, 0);      // set indent
   }
   changed_lines(invo->line1, 0, invo->line2 + 1, 0L);
   curPor->cursor = save_curpos;
   beginline(BL_WHITE | BL_FIX);
}

//Get the length of the current line, excluding trailing white space.
private int
linelen(OUT int* has_tab) {
   // Get the line.  If it's empty bail out early (could be the empty string for an unloaded book)
   CS line = ml_get_curline();
   if (*line == ZERO)
      return 0;

   // find the first non-blank character
   CS first = skipwhite(line);

   // find the character after the last non-blank character
   CS last;
   for (last = first + STRLEN(first); last > first && SPACE_OR_TAB(last[-1]); --last)
      {}
   int save = *last;
   *last = ZERO;
   int len = linetabsize_str(line);   // get line length on screen
   if (has_tab)      // check for embedded TAB
      *has_tab = (firstOccurrence(first, TAB) != NULL);
   *last = save;

   return len;
}

// Book for two lines used during sorting.  They are allocated to
// contain the longest line being sorted.
private CS sortbuf1;
private CS sortbuf2;

private int   sort_lc;   // sort using locale
private int   sort_ic;   // ignore case
private int   sort_nr;   // sort on number
private int   sort_rx;   // sort on regex instead of skipping it
private int   sort_flt;   // sort on floating number

private int   sort_abort;   // flag to indicate if sorting has been interrupted

// Struct to store info to be sorted.
typedef struct {
   LineNr   lnum;         // line number
   union {
      struct {
         Long   start_col_nr;   // starting column number
         Long   end_col_nr;   // ending column number
      } line;
      struct {
         Long   value;      // value if sorting by integer
         int is_number;      // true when line contains a number
      } num;
      double value_flt;      // value if sorting by float
   } st_u;
} SortingInfo;

private int
string_compare(const void *s1, const void *s2) {
   if (sort_lc)
      return strcoll((char *)s1, (char *)s2);
   return sort_ic ? caseInsensitiveCompare(s1, s2) : STRCMP(s1, s2);
}

private int
sort_compare(const void *s1, const void *s2) {
    SortingInfo   l1 = *(SortingInfo *)s1;
    SortingInfo   l2 = *(SortingInfo *)s2;
    int      result = 0;

    // If the user interrupts, there's no way to stop qsort() immediately, but
    // if we return 0 every time, qsort will assume it's done sorting and
    // exit.
   if (sort_abort)
   return 0;
    fast_breakcheck();
   if (gotInterruptG)
   sort_abort = true;

   if (sort_nr) {
   if (l1.st_u.num.is_number != l2.st_u.num.is_number)
       result = l1.st_u.num.is_number > l2.st_u.num.is_number ? 1 : -1;
   else
       result = l1.st_u.num.value == l2.st_u.num.value ? 0
              : l1.st_u.num.value > l2.st_u.num.value ? 1 : -1;
    } ei (sort_flt)
   result = l1.st_u.value_flt == l2.st_u.value_flt ? 0
              : l1.st_u.value_flt > l2.st_u.value_flt ? 1 : -1;
    else {
   // We need to copy one line into "sortbuf1", because there is no
   // guarantee that the first pointer becomes invalid when obtaining the
   // second one.
   STRNCPY(sortbuf1, ml_get(l1.lnum) + l1.st_u.line.start_col_nr,
           l1.st_u.line.end_col_nr - l1.st_u.line.start_col_nr + 1);
   sortbuf1[l1.st_u.line.end_col_nr - l1.st_u.line.start_col_nr] = 0;
   STRNCPY(sortbuf2, ml_get(l2.lnum) + l2.st_u.line.start_col_nr,
           l2.st_u.line.end_col_nr - l2.st_u.line.start_col_nr + 1);
   sortbuf2[l2.st_u.line.end_col_nr - l2.st_u.line.start_col_nr] = 0;

   result = string_compare(sortbuf1, sortbuf2);
    }

    // If two lines have the same value, preserve the original line order.
   if (result == 0)
   return (int)(l1.lnum - l2.lnum);
    return result;
}

// ":sort".
void
c_sort(Invocation* invo) {
   RegMatch   regmatch;
   int      len;
   LineNr   lnum;
   long   maxlen = 0;
   SortingInfo   *nrs;
   Unt   count = (Unt)(invo->line2 - invo->line1 + 1);
   Unt   i;
   CS p;
   CS s;
   CS s2;
   Byte   c;         // temporary character storage
   int      unique = false;
   long   deleted;
   ColNr   start_col;
   ColNr   end_col;
   int      sort_what = 0;
   int      format_found = 0;
   int      change_occurred = false; // Book contents changed.

   // Sorting one line is really quick!
   if (count <= 1)
      return;

   if (u_save((LineNr)(invo->line1 - 1), (LineNr)(invo->line2 + 1)) == FAIL)
      return;
   sortbuf1 = NULL;
   sortbuf2 = NULL;
   regmatch.regprog = NULL;
   nrs = ALLOC_MULT(SortingInfo, count);

   sort_abort = sort_ic = sort_lc = sort_rx = sort_nr = 0;
   sort_flt = 0;

   for (p = invo->arg; *p != ZERO; ++p) {
      if (SPACE_OR_TAB(*p))
          ;
      ei (*p == 'i')
          sort_ic = true;
      ei (*p == 'l')
          sort_lc = true;
      ei (*p == 'r')
          sort_rx = true;
      ei (*p == 'n') {
          sort_nr = 1;
          ++format_found;
      } ei (*p == 'f') {
          sort_flt = 1;
          ++format_found;
      } ei (*p == 'b') {
          sort_what = STR2NR_BIN + STR2NR_FORCE;
          ++format_found;
      } ei (*p == 'x') {
          sort_what = STR2NR_HEX + STR2NR_FORCE;
          ++format_found;
      }
      ei (*p == 'u')
          unique = true;
      ei (isComment(p))
          break;
      ei (invo->nextComm == NULL && check_nextcmd(p) != NULL) {
          invo->nextComm = check_nextcmd(p);
          break;
      } ei (!ASCII_ISALPHA(*p) && regmatch.regprog == NULL) {
          s = skip_regexp_err(p + 1, *p, true);
          if (s == NULL)
         goto sortend;
          *s = ZERO;
          // Use last search pattern if sort pattern is empty.
          if (s == p + 1) {
         if (last_search_pat() == NULL) {
             emsg(_(e_no_previous_regular_expression));
             goto sortend;
         }
         regmatch.regprog = compileRegexp(last_search_pat(), RE_MAGIC);
          } else
            regmatch.regprog = compileRegexp(p + 1, RE_MAGIC);
         if (regmatch.regprog == NULL)
            goto sortend;
         p = s;      // continue after the regexp
         regmatch.rm_ic = p_ic;
      } else {
         showErrFmtMsg(_(e_invalid_argument_str), p);
         goto sortend;
      }
    }

    // Can only have one of 'n', 'b', 'o' and 'x'.
   if (format_found > 1) {
      emsg(_(e_invalid_argument));
      goto sortend;
   }

   // From here on "sort_nr" is used as a flag for any integer number sorting.
   sort_nr += sort_what;

   //Make an array with all line numbers.  This avoids having to copy all
   //the lines into allocated memory.
   //When sorting on strings "start_col_nr" is the offset in the line, for
   //numbers sorting it's the number to sort on.  This means the pattern
   //matching and number conversion only has to be done once per line.
   //Also get the longest line length for allocating "sortbuf".
   for (lnum = invo->line1; lnum <= invo->line2; ++lnum) {
      s = ml_get(lnum);
      len = ml_get_len(lnum);
      if (maxlen < len)
         maxlen = len;

      start_col = 0;
      end_col = len;
      if (regmatch.regprog && eeRegexec(&regmatch, s, 0)) {
         if (sort_rx) {
            start_col = (ColNr)(regmatch.startp[0] - s);
            end_col = (ColNr)(regmatch.endp[0] - s);
         } else
            start_col = (ColNr)(regmatch.endp[0] - s);
      } else
         if (regmatch.regprog != NULL)
            end_col = 0;

      if (sort_nr || sort_flt) {
         // Make sure readLongNumber() doesn't read any digits past the end
         // of the match, by temporarily terminating the string there
         s2 = s + end_col;
         c = *s2;
         *s2 = ZERO;
         // Sorting on number: Store the number itself.
         p = s + start_col;
         if (sort_nr) {
            if (sort_what & STR2NR_HEX)
                s = skiptohex(p);
            ei (sort_what & STR2NR_BIN)
                s = skiptobin(p);
            else
                s = skiptodigit(p);
            if (s > p && s[-1] == '-')
                --s;  // include preceding negative sign
            if (*s == ZERO) {
                // line without number should sort before any number
                nrs[lnum - invo->line1].st_u.num.is_number = false;
                nrs[lnum - invo->line1].st_u.num.value = 0;
            } else {
               nrs[lnum - invo->line1].st_u.num.is_number = true;
               readLongNumber(
                  s, NULL, NULL, sort_what,
                  &nrs[lnum - invo->line1].st_u.num.value,
                  NULL, 0, false, NULL
               );
            }
         } else {
         s = skipwhite(p);
         if (*s == '+')
             s = skipwhite(s + 1);

         if (*s == ZERO)
            // empty line should sort before any number
            nrs[lnum - invo->line1].st_u.value_flt = -DBL_MAX;
         else
            nrs[lnum - invo->line1].st_u.value_flt = strtod((char *)s, NULL);
         }
         *s2 = c;
      } else {
          // Store the column to sort at.
          nrs[lnum - invo->line1].st_u.line.start_col_nr = start_col;
          nrs[lnum - invo->line1].st_u.line.end_col_nr = end_col;
      }

      nrs[lnum - invo->line1].lnum = lnum;

      if (regmatch.regprog)
          fast_breakcheck();
      if (gotInterruptG)
          goto sortend;
    }

   // Allocate a buffer that can hold the longest line.
   sortbuf1 = alloc(maxlen + 1);
   sortbuf2 = alloc(maxlen + 1);

   // Sort the array of line numbers.  Note: can't be interrupted!
   qsort((void *)nrs, count, sizeof(SortingInfo), sort_compare);

   if (sort_abort)
      goto sortend;

   // Insert the lines in the sorted order below the last one.
   lnum = invo->line2;
   for (i = 0; i < count; ++i) {
      LineNr get_lnum = nrs[invo->forceit ? count - i - 1 : i].lnum;

      // If the original line number of the line being placed is not the same
      // as "lnum" (accounting for offset), we know that the buffer changed.
      if (get_lnum + ((LineNr)count - 1) != lnum)
          change_occurred = true;

      s = ml_get(get_lnum);
      if (!unique || i == 0 || string_compare(s, sortbuf1) != 0) {
          // Copy the line into a buffer, it may become invalid in
          // ml_append(). And it's needed for "unique".
          STRCPY(sortbuf1, s);
          if (ml_append(lnum++, sortbuf1, (ColNr)0, false) == FAIL)
         break;
      }
      fast_breakcheck();
      if (gotInterruptG)
          goto sortend;
    }

    // delete the original lines if appending worked
   if (i == count) {
      for (i = 0; i < count; ++i)
          ml_delete(invo->line1);
   } else
      count = 0;

   // Adjust marks for deleted (or added) lines and prepare for displaying.
   deleted = (long)(count - (lnum - invo->line2));
   if (deleted > 0) {
      markAdjust(invo->line2 - deleted, invo->line2, (long)MAXLNUM, -deleted, true);
      msgmore(-deleted);
   } ei (deleted < 0)
      markAdjust(invo->line2, MAXLNUM, -deleted, 0L, true);

   if (change_occurred || deleted != 0)
      changed_lines(invo->line1, 0, invo->line2 + 1, -deleted);

   curPor->cursor.lnum = invo->line1;
   beginline(BL_WHITE | BL_FIX);

sortend:
   eeglFree(nrs);
   eeglFree(sortbuf1);
   eeglFree(sortbuf2);
   eeRegFree(regmatch.regprog);
   if (gotInterruptG)
      emsg(_(e_interrupted));
}

// ":uniq".
void
c_uniq(Invocation* invo) {
   RegMatch   regmatch;
   int      len;
   LineNr   lnum;
   long   maxlen = 0;
   LineNr   count = invo->line2 - invo->line1 + 1;
   CS p;
   CS s;
   Byte save_c = 0;      // temporary character storage
   int keep_only_unique = false;
   int keep_only_not_unique = invo->forceit ? true : false;
   long deleted = 0;
   ColNr start_col;
   ColNr end_col;
   int change_occurred = false; // Book contents changed.

   // Uniq one line is really quick!
   if (count <= 1)
      return;

   if (u_save((LineNr)(invo->line1 - 1), (LineNr)(invo->line2 + 1)) == FAIL)
      return;
   sortbuf1 = NULL;
   regmatch.regprog = NULL;

   sort_abort = sort_ic = sort_lc = sort_rx = sort_nr = 0;
   sort_flt = 0;

   for (p = invo->arg; *p != ZERO; ++p) {
      if (SPACE_OR_TAB(*p))
          ;
      ei (*p == 'i')
          sort_ic = true;
      ei (*p == 'l')
          sort_lc = true;
      ei (*p == 'r')
          sort_rx = true;
      ei (*p == 'u') {
          // 'u' is only valid when '!' is not given.
          if (!keep_only_not_unique)
         keep_only_unique = true;
      } ei (isComment(p))   // comment start
         break;
      ei (invo->nextComm == NULL && check_nextcmd(p) != NULL) {
         invo->nextComm = check_nextcmd(p);
         break;
      } ei (!ASCII_ISALPHA(*p) && regmatch.regprog == NULL) {
         s = skip_regexp_err(p + 1, *p, true);
         if (s == NULL)
            goto uniqend;
         *s = ZERO;
         // Use last search pattern if uniq pattern is empty.
         if (s == p + 1) {
            if (last_search_pat() == NULL) {
                emsg(_(e_no_previous_regular_expression));
                goto uniqend;
            }
            regmatch.regprog = compileRegexp(last_search_pat(), RE_MAGIC);
         } else
            regmatch.regprog = compileRegexp(p + 1, RE_MAGIC);
         if (regmatch.regprog == NULL)
            goto uniqend;
         p = s;      // continue after the regexp
         regmatch.rm_ic = p_ic;
      } else {
          showErrFmtMsg(_(e_invalid_argument_str), p);
          goto uniqend;
      }
   }

   // Find the length of the longest line.
   for (lnum = invo->line1; lnum <= invo->line2; ++lnum) {
      len = ml_get_len(lnum);
      if (maxlen < len)
         maxlen = len;

      if (gotInterruptG)
         goto uniqend;
   }

   // Allocate a buffer that can hold the longest line.
   sortbuf1 = alloc(maxlen + 1);

   // Delete lines according to options.
   int match_continue = false;
   int next_is_unmatch = false;
   int is_match;
   LineNr done_lnum = invo->line1 - 1;
   LineNr delete_lnum = 0;
   for (LineNr i = 0; i < count; ++i) {
      LineNr get_lnum = invo->line1 + i;

      s = ml_get(get_lnum);
      len = ml_get_len(get_lnum);

      start_col = 0;
      end_col = len;
      if (regmatch.regprog && eeRegexec(&regmatch, s, 0)) {
          if (sort_rx) {
         start_col = (ColNr)(regmatch.startp[0] - s);
         end_col = (ColNr)(regmatch.endp[0] - s);
          }
          else
         start_col = (ColNr)(regmatch.endp[0] - s);
      } ei(regmatch.regprog)
         end_col = 0;
      if (end_col > 0) {
         save_c = s[end_col];
         s[end_col] = ZERO;
      }

      is_match = i > 0 ? !string_compare(&s[start_col], sortbuf1) : false;
      delete_lnum = 0;
      if (next_is_unmatch) {
         is_match = false;
         next_is_unmatch = false;
      }

      if (!keep_only_unique && !keep_only_not_unique) {
          if (is_match)
         delete_lnum = get_lnum;
          else
         STRCPY(sortbuf1, &s[start_col]);
      } ei (keep_only_not_unique) {
          if (is_match) {
         done_lnum = get_lnum - 1;
         delete_lnum = get_lnum;
         match_continue = true;
          } else {
         if (i > 0 && !match_continue && get_lnum - 1 > done_lnum) {
             delete_lnum = get_lnum - 1;
             next_is_unmatch = true;
         }
         ei (i >= count - 1)
             delete_lnum = get_lnum;
         match_continue = false;
         STRCPY(sortbuf1, &s[start_col]);
          }
      } else // keep_only_unique
      {
          if (is_match) {
         if (!match_continue)
             delete_lnum = get_lnum - 1;
         else
             delete_lnum = get_lnum;
         match_continue = true;
          } else {
         if (i == 0 && match_continue)
             delete_lnum = get_lnum;
         match_continue = false;
         STRCPY(sortbuf1, &s[start_col]);
          }
      }

      if (end_col > 0)
          s[end_col] = save_c;

      if (delete_lnum > 0) {
          ml_delete(delete_lnum);
          i -= get_lnum - delete_lnum + 1;
          count--;
          deleted++;
          change_occurred = true;
      }

      fast_breakcheck();
      if (gotInterruptG)
          goto uniqend;
   }

   // Adjust marks for deleted lines and prepare for displaying.
   markAdjust(invo->line2 - deleted, invo->line2, (long)MAXLNUM, -deleted, true);
   msgmore(-deleted);

   if (change_occurred)
      changed_lines(invo->line1, 0, invo->line2 + 1, -deleted);

   curPor->cursor.lnum = invo->line1;
   beginline(BL_WHITE | BL_FIX);

uniqend:
   eeglFree(sortbuf1);
   eeRegFree(regmatch.regprog);
   if (gotInterruptG)
      emsg(_(e_interrupted));
}

// :move command - move lines line1-line2 to line dest
// return FAIL for failure, OK otherwise
int
do_move(LineNr line1, LineNr line2, LineNr dest) {
   CS str;
   LineNr l;
   LineNr extra;       // Num lines added before line1
   LineNr num_lines;  // Num lines moved
   LineNr last_line;  // Last line in file after adding new text
   Tab* t;

   if (dest >= line1 && dest < line2) {
      emsg(_(e_cannot_move_range_of_lines_into_itself));
      return FAIL;
   }

   // Do nothing if we are not actually moving any lines.  This will prevent
   // the 'modified' flag from being set without cause.
   if (dest == line1 - 1 || dest == line2) {
      // Move the cursor as if lines were moved (see below) to be backwards
      // compatible.
      if (dest >= line1)
         curPor->cursor.lnum = dest;
      else
         curPor->cursor.lnum = dest + (line2 - line1) + 1;

      return OK;
   }

    num_lines = line2 - line1 + 1;

    /*
     * First we copy the old text to its new location -- webb
     * Also copy the flag that ":global" command uses.
     */
   if (u_save(dest, dest + 1) == FAIL)
   return FAIL;
   for (extra = 0, l = line1; l <= line2; l++) {
      str = copySubstr(ml_get(l + extra), ml_get_len(l + extra));
      if (str) {
         ml_append(dest + l - line1, str, (ColNr)0, false);
         eeglFree(str);
         if (dest < line1)
            extra++;
      }
   }

   //Now we must be careful adjusting our marks so that we don't overlap our markAdjust() calls.
   //
   //We adjust the marks within the old text so that they refer to the
   //last lines of the file (temporarily), because we know no other marks
   //will be set there since these line numbers did not exist until we added our new lines.
   //
   //Then we adjust the marks on lines between the old and new text positions
   //(either forwards or backwards).
   //
   //And Finally we adjust the marks we put at the end of the file back to
   //their final destination at the new text position -- webb
   last_line = curBook->mem.lineCount;
   markAdjust(line1, line2, last_line - line2, 0L, false);
   Portal* po;
   if (dest >= line2) {
      markAdjust(line2 + 1, dest, -num_lines, 0L, false);
      FOR_ALL_TAB_PORTALS(t, po) {
         if (po->book == curBook)
            foldMoveRange(&po->folds, line1, line2, dest);
      }
      if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
          curBook->opStart.lnum = dest - num_lines + 1;
          curBook->opEnd.lnum = dest;
      }
   } else {
      markAdjust(dest + 1, line1 - 1, num_lines, 0L, false);
      FOR_ALL_TAB_PORTALS(t, po) {
         if (po->book == curBook)
            foldMoveRange(&po->folds, dest + 1, line1 - 1, line2);
      }
      if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
          curBook->opStart.lnum = dest + 1;
          curBook->opEnd.lnum = dest + num_lines;
      }
   }
   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0)
      curBook->opStart.col = curBook->opEnd.col = 0;
   markAdjust(last_line - num_lines + 1, last_line, -(last_line - dest - extra), 0L, false);

   // Now we delete the original text -- webb
   if (u_save(line1 + extra - 1, line2 + extra + 1) == FAIL)
      return FAIL;

   for (l = line1; l <= line2; l++)
      ml_delete_flags(line1 + extra, ML_DEL_MESSAGE);

   if (!global_busy) {
      smsg(NGETTEXT("%ld line moved", "%ld lines moved", num_lines),
            (long)num_lines);
   } 

   //Leave the cursor on the last of the moved lines.
   if (dest >= line1)
      curPor->cursor.lnum = dest;
   else
      curPor->cursor.lnum = dest + (line2 - line1) + 1;

   if (line1 < dest) {
      dest += num_lines + 1;
      last_line = curBook->mem.lineCount;
      if (dest > last_line + 1)
          dest = last_line + 1;
      changed_lines(line1, 0, dest, 0L);
   } else
      changed_lines(dest + 1, 0, line1 + num_lines, 0L);

   return OK;
}

// ":copy"
private void
doCopy(LineNr line1, LineNr line2, LineNr n) {
   CS p;

   LineNr count = line2 - line1 + 1;
   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
      curBook->opStart.lnum = n + 1;
      curBook->opEnd.lnum = n + count;
      curBook->opStart.col = curBook->opEnd.col = 0;
   }

   //there are three situations:
   //1. destination is above line1
   //2. destination is between line1 and line2
   //3. destination is below line2
   //
   //n = destination (when starting)
   //curPor->cursor.lnum = destination (while copying)
   //line1 = start of source (while copying)
   //line2 = end of source (while copying)
   if (u_save(n, n + 1) == FAIL)
      return;

   curPor->cursor.lnum = n;
   while (line1 <= line2) {
      // need to make a copy because the line will be unlocked within ml_append()
      p = copySubstr(ml_get(line1), ml_get_len(line1));
      if (p) {
         ml_append(curPor->cursor.lnum, p, (ColNr)0, false);
         eeglFree(p);
      }
      // situation 2: skip already copied lines
      if (line1 == n)
          line1 = curPor->cursor.lnum;
      ++line1;
      if (curPor->cursor.lnum < line1)
          ++line1;
      if (curPor->cursor.lnum < line2)
          ++line2;
      ++curPor->cursor.lnum;
    }

    appended_lines_mark(n, count);
   if (VIsual_active)
   check_pos(curBook, &VIsual);

    msgmore((long)count);
}

private CS prevcmd = NULL;   // the previous command

#if defined(EXITFREE) || defined(PROTO)
void
free_prev_shellcmd(void) {
    eeglFree(prevcmd);
}
#endif

//Check that "prevcmd" is not NULL. If it is NULL, then give an error message and return false.
private int
prevcmd_is_set(void) {
   if (prevcmd == NULL) {
   emsg(_(e_no_previous_command));
   return false;
    }
    return true;
}

//Handle the ":!cmd" command.   Also for ":r !cmd" and ":w !cmd"
//Bangs in the argument are replaced with the previously entered command.
//Remember the argument.
void
do_bang(
   int addr_count,
   Invocation* invo,
   Boole forceit,
   Boole do_in,
   Boole do_out
) {
   CS arg = invo->arg;   // command
   LineNr      line1 = invo->line1;   // start of range
   LineNr      line2 = invo->line2;   // end of range
   CS newcmd = NULL;      // the new command
   Boole free_newcmd = false;    // need to free() newcmd
   CS t;
   CS p;
   int         len;
   int         scroll_save = msg_scroll;

   if (addr_count == 0) {     // :!
      msg_scroll = false;       // don't scroll here
      doFlushAllBooks();
      msg_scroll = scroll_save;
   }

   //Try to find an embedded bang, like in ":!<cmd> ! [args]"
   //":!!" is indicated by the 'forceit' variable.
   int ins_prevcmd = forceit;

   //Skip leading white space to avoid a strange error with some shells.
   CS trailarg = skipwhite(arg);
    do {
      len = (int)STRLEN(trailarg) + 1;
      if (newcmd)
          len += (int)STRLEN(newcmd);
      if (ins_prevcmd) {
          if (!prevcmd_is_set()) {
         eeglFree(newcmd);
         return;
          }
          len += (int)STRLEN(prevcmd);
      }
      t = alloc(len);
      *t = ZERO;
      if (newcmd)
          STRCAT(t, newcmd);
      if (ins_prevcmd)
          STRCAT(t, prevcmd);
      p = t + STRLEN(t);
      STRCAT(t, trailarg);
      eeglFree(newcmd);
      newcmd = t;

      /*
       * Scan the rest of the argument for '!', which is replaced by the
       * previous command.  "\!" is replaced by "!" (this is vi compatible).
       */
      trailarg = NULL;
      while (*p) {
          if (*p == '!') {
         if (p > newcmd && p[-1] == '\\')
             STRMOVE(p - 1, p);
         else {
             trailarg = p;
             *trailarg++ = ZERO;
             ins_prevcmd = true;
             break;
         }
          }
          ++p;
      }
   } while (trailarg);

   // Only set "prevcmd" if there is a command to run, otherwise keep te one
   // we have.
   if (STRLEN(newcmd) > 0) {
      eeglFree(prevcmd);
      prevcmd = newcmd;
    } else
      free_newcmd = true;

   if (bangredo) {     // put cmd in redo buffer for ! command
      if (!prevcmd_is_set())
          goto theend;

      //If % or # appears in the command, it must have been escaped.
      //Reescape them, so that redoing them does not substitute them by the buffername.
      CS cmd = copyStr_escaped(prevcmd, S"%#");

      AppendToRedobuffLit(cmd, -1);
      eeglFree(cmd);
      AppendToRedobuff((CS)"\n");
      bangredo = false;
   }
   if (addr_count == 0) {     // :!
      // echo the command
      msg_start();
      msg_putchar(':');
      msg_putchar('!');
      msg_outtrans(newcmd);
      msg_clr_eos();
      windgoto(msgRowG, msgColG);

      do_shell(newcmd, 0);
   } else { // :range!
      // Careful: This may recursively call do_bang() again! (because of autocommands)
      do_filter(line1, line2, invo, newcmd, do_in, do_out);
      applyAutocomms(EVENT_SHELLFILTERPOST, NULL, NULL, false, curBook);
   }

theend:
   if (free_newcmd)
      eeglFree(newcmd);
}

//do_filter: filter lines through a command given by the user
//
//We mostly use temp files and the call_shell() routine here. This would
//normally be done using pipes on a Unix machine, but this is more portable
//to non-unix machines. The call_shell() routine needs to be able
//to deal with redirection somehow, and should handle things like looking
//at the PATH env. variable, and adding reasonable extensions to the
//command name given by the user. All reasonable versions of call_shell() do this.
//Alternatively, if on Unix and redirecting input or output, but not both,
//and the @shelltemp option isn't set, use pipes.
//We use input redirection if do_in is true.
//We use output redirection if do_out is true.
private void
do_filter(
   LineNr   line1,
   LineNr   line2,
   Invocation* invo,      // for forced 'ff' and 'fenc'
   CS cmd,
   Boole do_in,
   Boole do_out
) {
   CS itmp = NULL;
   CS otmp = NULL;
   LineNr   linecount;
   LineNr   read_linecount;
   Pos   cursor_save;
   Book* curBookSaved = curBook;
   int      shell_flags = 0;
   Pos   orig_start = curBook->opStart;
   Pos   orig_end = curBook->opEnd;
   int      save_cmod_flags = commModifierG.cmod_flags;
   int      stmp = p_stmp;

   if (*cmd == ZERO)       // no filter command
      return;

   // Temporarily disable lockmarks since that's needed to propagate changed
   // regions of the book for foldUpdate(), linecount, etc.
   commModifierG.cmod_flags &= ~CMOD_LOCKMARKS;

   cursor_save = curPor->cursor;
   linecount = line2 - line1 + 1;
   curPor->cursor.lnum = line1;
   curPor->cursor.col = 0;
   changed_line_abv_curs();
   invalidate_botline();

   //When using temp files:
   //1. * Form temp file names
   //2. * Write the lines to a temp file
   //3.   Run the filter command on the temp file
   //4. * Read the output of the command into the buffer
   //5. * Delete the original lines to be filtered
   //6. * Remove the temp files
   //
   //When writing the input with a pipe or when catching the output with a
   //pipe only need to do 3.

   if (do_out)
      shell_flags |= SHELL_DOOUT;


   if (!do_in && do_out && !stmp) {
      // Use a pipe to fetch stdout of the command, do not use a temp file.
      shell_flags |= SHELL_READ;
      curPor->cursor.lnum = line2;
   } ei (do_in && !do_out && !stmp) {
      // Use a pipe to write stdin of the command, do not use a temp file.
      shell_flags |= SHELL_WRITE;
      curBook->opStart.lnum = line1;
      curBook->opEnd.lnum = line2;
   } ei (do_in && do_out && !stmp) {
      //Use a pipe to write stdin and fetch stdout of the command, do not use a temp file.
      shell_flags |= SHELL_READ|SHELL_WRITE;
      curBook->opStart.lnum = line1;
      curBook->opEnd.lnum = line2;
      curPor->cursor.lnum = line2;
    } else
   if ((do_in && (itmp = eeTempName('i', false)) == NULL)
      || (do_out && (otmp = eeTempName('o', false)) == NULL))
   {
       emsg(_(e_cant_get_temp_file_name));
       goto filterend;
   }

   //The writing and reading of temp files will not be shown.
   ++no_wait_return;      // don't call wait_return() while busy
   if (itmp && bookWrite(curBook, itmp, NULL, line1, line2, invo,
                  false, false, false, true) == FAIL
   ) {
      msg_putchar('\n');      // keep message from bookWrite()
      --no_wait_return;
      if (!aborting())
         // will call wait_return()
         (void)showErrFmtMsg(_(e_cant_create_file_str), itmp);
      goto filterend;
   }
   if (curBook != curBookSaved)
      goto filterend;

   if (!do_out)
      msg_putchar('\n');

   // Create the shell command in allocated memory.
   CS cmd_buf = make_filter_cmd(cmd, itmp, otmp);
   if (!cmd_buf)
      goto filterend;

   windgoto((int)visibleRowsG - 1, 0);
   cursor_on();

   //When not redirecting the output the command can write anything to the
   //screen. If 'shellredir' is equal to ">", screen may be messed up by
   //stderr output of external command. Clear the screen later.
   //If do_in is false, this could be something like ":r !cat", which may
   //also mess up the screen, clear it later.
   if (!do_out || (p_srr && STRCMP(p_srr, ">") == 0) || !do_in)
      redraw_later_clear();

   if (do_out) {
      if (u_save(line2, (LineNr)(line2 + 1)) == FAIL) {
          eeglFree(cmd_buf);
          goto error;
      }
      drawCurBookLater(UPD_VALID);
   }
   read_linecount = curBook->mem.lineCount;

   //When call_shell() fails wait_return() is called to give the user a
   //chance to read the error messages. Otherwise errors are ignored, so you
   //can see the error messages from the command that appear on stdout; use
   //'u' to fix the text
   //Switch to cooked mode when not redirecting stdin, avoids that something
   //like ":r !cat" hangs.
   //Pass on the SHELL_DOOUT flag when the output is being redirected.
   if (call_shell(cmd_buf, null, SHELL_FILTER | SHELL_COOKED | shell_flags)) {
      redraw_later_clear();
      wait_return(false);
   }
   eeglFree(cmd_buf);

   did_check_timestamps = false;
   need_check_timestamps = true;

   // When interrupting the shell command, it may still have produced some
   // useful output.  Reset gotInterruptG here, so that readfile() won't cancel reading.
   ui_breakcheck();
   gotInterruptG = false;

   if (do_out) {
      if (otmp) {
         if (readfile(otmp, NULL, line2, (LineNr)0, (LineNr)MAXLNUM, invo, READ_FILTER) != OK) {
            if (!aborting()) {
               msg_putchar('\n');
               showErrFmtMsg(_(e_cant_read_file_str), otmp);
            }
            goto error;
         }
         if (curBook != curBookSaved)
            goto filterend;
      }

      read_linecount = curBook->mem.lineCount - read_linecount;

      if (shell_flags & SHELL_READ) {
         curBook->opStart.lnum = line2 + 1;
         curBook->opEnd.lnum = curPor->cursor.lnum;
         appended_lines_mark(line2, read_linecount);
      }

      if (do_in) {
         if (read_linecount >= linecount)
            // move all marks from old lines to new lines
            markAdjust(line1, line2, linecount, 0L, true);
         ei (save_cmod_flags & CMOD_LOCKMARKS) {
            // Move marks from the lines below the new lines down by the number of lines lost.
            // Move marks from the lines that will be deleted to the new lines and below.
            markAdjust(line2 + 1, (LineNr)MAXLNUM, linecount - read_linecount, 0L, true);
            markAdjust(line1, line2, linecount, 0L, true);
         } else {
            // move marks from old lines to new lines, delete marks that are in deleted lines
            markAdjust(line1, line1 + read_linecount - 1, linecount, 0L, true);
            markAdjust(line1 + read_linecount, line2, MAXLNUM, 0L, true);
         }

         //Put cursor on first filtered line for ":range!cmd".
         //Adjust '[ and '] (set by bookWrite()).
         curPor->cursor.lnum = line1;
         del_lines(linecount, true);
         curBook->opStart.lnum -= linecount;   // adjust '[
         curBook->opEnd.lnum -= linecount;      // adjust ']
         write_lnum_adjust(-linecount);      // adjust last line for next write
         foldUpdate(curPor, curBook->opStart.lnum, curBook->opEnd.lnum);
      } else {
         //Put cursor on last new line for ":r !cmd".
         linecount = curBook->opEnd.lnum - curBook->opStart.lnum + 1;
         curPor->cursor.lnum = curBook->opEnd.lnum;
      }

      beginline(BL_WHITE | BL_FIX);       // cursor on first non-blank
      --no_wait_return;

      if (do_in) {
         eeSnprintf(msg_buf, sizeof(msg_buf), _("%ld lines filtered"), (long)linecount);
         if (msg(msg_buf) && !msg_scroll)
            // save message to display it after redraw
            set_keep_msg((CS)msg_buf, 0);
      } else
         msgmore((long)linecount);
   } else {
error:
      // put cursor back in same position for ":w !cmd"
      curPor->cursor = cursor_save;
      --no_wait_return;
      wait_return(false);
   }

filterend:

   commModifierG.cmod_flags = save_cmod_flags;
   if (curBook != curBookSaved) {
      --no_wait_return;
      emsg(_(e_filter_autocommands_must_not_change_current_buffer));
   } ei (commModifierG.cmod_flags & CMOD_LOCKMARKS) {
      curBook->opStart = orig_start;
      curBook->opEnd = orig_end;
   }

   if (itmp)
      mch_remove(itmp);
   if (otmp)
      mch_remove(otmp);
   eeglFree(itmp);
   eeglFree(otmp);
}

//Call a shell to execute a command. When "cmd" is NULL, start an interactive shell.
void
do_shell(CS cmd, Unt flags) {   // may be SHELL_DOOUT when output is redirected
   int keep_termcap = !termcap_active;

   //For autocommands we want to get the output on the current screen, to avoid having to type 
   //return below.
   msg_putchar('\r');         // put cursor at start of line
   if (!autocmd_busy && !keep_termcap)
      termStopTerminfo();
   msg_putchar('\n');      // may shift screen one line up

   // warning message before calling the shell
   if (!autocmd_busy && msg_silent == 0) {
      Book* book;
      FOR_ALL_BOOKS(book) {
         if (wasBookChangedNotTerm(book)) {
            msg_puts(_("[No write since last change]\n"));
            break;
         }
      }
   } 
   // This windgoto is required for when the '\n' resulted in a "delete line
   // 1" command to the terminal.
   if (!termIsScreenBeingSwapped())
      windgoto(msgRowG, msgColG);
   cursor_on();
   
   (void)call_shell(cmd, null, SHELL_COOKED | flags);
   did_check_timestamps = false;
   need_check_timestamps = true;

   //put the message cursor at the end of the screen, avoids wait_return()
   //to overwrite the text that the external command showed
   if (!termIsScreenBeingSwapped()) {
      msgRowG = visibleRowsG - 1;
      msgColG = 0;
   }

   if (autocmd_busy) {
      if (msg_silent == 0)
          redraw_later_clear();
   } else {
      //For ":sh" there is no need to call wait_return(), just redraw.
      //Otherwise there is probably text on the screen that the user wants
      //to read before redrawing, so call wait_return().

      if (!keep_termcap)   // if keep_termcap is true didn't stop termcap
         starttermcap();   // start termcap if not done by wait_return()
   }

   display_errors();

   applyAutocomms(EVENT_SHELLCMDPOST, NULL, NULL, false, curBook);
}

//Ask the user to enter a number.
//When "mouse_used" is not NULL allow using the mouse and in that case return the line number.
int
prompt_for_number(int *mouse_used) {
   int      save_commlineRowG;
   int      save_State;

   // When using ":silent" assume that <CR> was entered.
   if (mouse_used)
      msg_puts(_("Type number and <Enter> or click with the mouse (q or empty cancels): "));
   else
      msg_puts(_("Type number and <Enter> (q or empty cancels): "));

   // Set the state such that text can be selected/copied/pasted and we still
   // get mouse events. redraw_after_callback() will not redraw if commlineRowG
   // is zero.
   save_commlineRowG = commlineRowG;
   commlineRowG = 0;
   save_State = stateG;
   stateG = MODE_COMMLINE;
   // May show different mouse shape.
   setmouse();

   int i = get_number(true, mouse_used);
   if (keyWasTypedG) {
      // don't call wait_return() now
      if (msgRowG > 0)
         commlineRowG = msgRowG - 1;
      need_wait_return = false;
      msg_didany = false;
      msg_didout = false;
   } else
      commlineRowG = save_commlineRowG;
   stateG = save_State;
   // May need to restore mouse shape.
   setmouse();

   return i;
}

//Create a shell command from a command string, input redirection file and
//output redirection file.
//Return an allocated string with the shell command, or NULL for failure.
CS
make_filter_cmd(
   CS cmd,      // command
   CS inputFName,      // NULL or name of input file
   CS outputFName      // NULL or name of output file
){

   Ulong len = (Ulong)STRLEN(cmd) + 3;      // "()" + ZERO

   if (inputFName) {
      len += (Ulong)STRLEN(inputFName) + 9;   // " { < " + " } "
   }
   if (outputFName)
      len += (Ulong)STRLEN(outputFName) + (Ulong)(p_srr ? STRLEN(p_srr) : 0) + 2; // "  "

   CS stringBuild = alloc(len);

   //Put braces around the command (for concatenated commands) when
   //redirecting input and/or output.
   if (inputFName || outputFName) {
      eeSnprintf(stringBuild, len, "(%s)", (char *)cmd);
   } else
      STRCPY(stringBuild, cmd);
   if (inputFName) {
      STRCAT(stringBuild, " < ");
      STRCAT(stringBuild, inputFName);
   }
    
   if (outputFName)
      doAppendRedir(stringBuild, (int)len, p_srr, outputFName);

   return stringBuild;
}

//Append output redirection for file "fname" to the end of string buffer "buf[buflen]"
//Works with the @shellredir and @shellpipe options.
//The caller must make sure that there is enough room:
//  STRLEN(opt) + STRLEN(fname) + 3
void
doAppendRedir(CS buf, int buflen, NULLABLE CS opt, CS fname) {
   if (!opt)
      return;
   CS p;

   CS end = buf + STRLEN(buf);
   // find "%s"
   for (p = opt; (p = firstOccurrence(p, '%')) != NULL; ++p) {
      if (p[1] == 's') // found %s
         break;
      if (p[1] == '%') // skip %%
         ++p;
   }
   if (p) {
      eeSnprintf(end, (Unt)(buflen - (end - buf)), (char *)opt, (char *)fname);
   } else
      eeSnprintf(end, (Unt)(buflen - (end - buf)), " %s %s", (char *)opt, (char *)fname);
}

//Implementation of ":fixdel", also used by get_stty().
// <BS>    resulting <Del>
//  ^?      ^H
//not ^?   ^?
void
do_fixdel(Invocation* invo UNUSED) {
    CS p = find_termcode(S"kb");
    add_termcode(S"kD", p && *p == DEL ? (CS)CTRL_H_STR : DEL_STR, false);
}

private void
print_line_no_prefix(LineNr lnum, int list) {
   Byte numbuf[30];
   eeSnprintf(numbuf, sizeof(numbuf), "%*ld ", number_width(curPor), (long)lnum);
   msgPutsDeco(numbuf, getDecoFlags(HLF_N));   // Highlight line nrs
   msg_prt_line(ml_get(lnum), list);
}

//Print a text line.  Also in silent mode ("ex -s").
private void
print_line(LineNr lnum, int list) {
   Boole save_silent = silentModeG;

   // apply :filter /pat/
   if (message_filtered(ml_get(lnum)))
      return;

   msg_start();
   silentModeG = false;
   info_message = true;   // use mch_msg(), not mch_errmsg()
   print_line_no_prefix(lnum, list);
   if (save_silent) {
      msg_putchar('\n');
      cursor_on();      // msg_start() switches it off
      out_flush();
      silentModeG = save_silent;
   }
   info_message = false;
}

private int
renameBook(CS new_fname) {
   Book* book = curBook;
   applyAutocomms(EVENT_BUFFILEPRE, NULL, NULL, false, curBook);
   // book changed, don't change name now
   if (book != curBook)
      return FAIL;
   if (aborting())       // autocmds may abort script processing
      return FAIL;
   //The name of the current book will be changed.
   //A new (unlisted) book entry needs to be made to hold the old file
   //name, which will become the alternate file name.
   //But don't set the alternate file name if the book didn't have a name.
   CS fname = curBook->fullFileName;
   CS sfname = curBook->shortFileName;
   CS xfname = curBook->currFileName;
   curBook->fullFileName = NULL;
   curBook->shortFileName = NULL;
   if (setfname(curBook, new_fname, NULL, true) == FAIL) {
      curBook->fullFileName = fname;
      curBook->shortFileName = sfname;
      return FAIL;
   }
   
   curBook->flags |= BF_NOTEDITED;
   if (xfname && *xfname != ZERO) {
      book = bookNew(fname, xfname, curPor->cursor.lnum, 0);
      if (book && (commModifierG.cmod_flags & CMOD_KEEPALT) == 0)
          curPor->altFnum = book->fiNum;
   }
   eeglFree(fname);
   eeglFree(sfname);
   applyAutocomms(EVENT_BUFFILEPOST, NULL, NULL, false, curBook);

   // Change directories when the 'acd' option is set.
   DO_AUTOCHDIR;
   return OK;
}

//}}}
//{{{writin' to files

// ":file[!] [fname]".
void
c_file(Invocation* invo) {
   // ":0file" removes the file name.  Check for illegal uses ":3file", "0file name", etc.
   if (invo->addr_count > 0 && (*invo->arg != ZERO || invo->line2 > 0 || invo->addr_count > 1)) {
      emsg(_(e_invalid_argument));
      return;
   }

   if (*invo->arg != ZERO || invo->addr_count == 1) {
      if (renameBook(invo->arg) == FAIL)
          return;
      needRedrawTabpanelG = true;
   }

   // print file name if no argument or 'F' is not in 'shortmess'
   fileinfo(false, false, invo->forceit);
}

// ":update".
void
c_update(Invocation* invo) {
   if (doWasCurBookChanged())
      (void)do_write(invo);
}

// ":write" and ":saveas".
void
c_write(Invocation* invo) {
   if (invo->id == C_saveas) {
      // :saveas does not take a range, uses all lines.
      invo->line1 = 1;
      invo->line2 = curBook->mem.lineCount;
   }

   if (invo->usefilter)      // input lines to shell command
      do_bang(1, invo, false, true, false);
   else
      (void)do_write(invo);
}

private int
check_writable(CS fname) {
   if (mch_nodetype(fname) == NODE_OTHER) {
      showErrFmtMsg(_(e_str_is_not_file_or_writable_device), fname);
      return FAIL;
   }
   return OK;
}

//Check if it is allowed to overwrite a file.  If flags has BF_NOTEDITED, BF_NEW or BF_READERR, 
//check for overwriting current file. May set invo->forceit if a dialog says it's OK to overwrite.
//Return OK if it's OK, FAIL if it is not.
private int
check_overwrite(
   Invocation* invo,
   Book* book,
   CS fname,       // file name to be used (can differ from book->fullFName)
   CS fullFName,    // full path version of fname
   Boole other)       // writing under other name
{
   //Write to another file or flags set or not writing the whole file: overwriting only allowed 
   //with '!'. If "other" is false and bt_nofilename(book) is true, this must be
   //writing an "acwrite" book to the same file as its fullFileName, and bookWrite() will only 
   //allow writing with BufWriteCmd autocommands, so there is no need for an overwrite check.
   if (       (other
      || (!bt_nofilename(book)
          && ((book->flags & BF_NOTEDITED)
         || (book->flags & BF_NEW)
         || (book->flags & BF_READERR))))
       && !p_wa
       && eeFexists(fullFName)
   ) {
      if (!invo->forceit && !invo->append) {
         if (mch_isdir(fullFName)) {
            showErrFmtMsg(_(e_str_is_directory), fullFName);
            return FAIL;
         }
         if (p_confirm || (commModifierG.cmod_flags & CMOD_CONFIRM)) {
            Byte buff[DIALOG_MSG_SIZE];
            dialog_msg(buff, _("Overwrite existing file \"%s\"?"), fname);
            if (eeDialog_yesno(EE_QUESTION, NULL, buff, 2) != EE_YES)
                return FAIL;
            invo->forceit = true;
         } else {
            emsg(_(e_file_exists));
            return FAIL;
         }
      }

      // For ":w! filename" check that no swap file exists for "filename".
      if (other && !emsg_silent) {
         CS swapname = fiBuildSwapOrUndoFname(fullFName, false);
         int r = eeFexists(swapname);
         if (r) {
            if (p_confirm || (commModifierG.cmod_flags & CMOD_CONFIRM)) {
               Byte buff[DIALOG_MSG_SIZE];
               dialog_msg(buff, _("Swap file \"%s\" exists, overwrite anyway?"), swapname);
               if (eeDialog_yesno(EE_QUESTION, NULL, buff, 2) != EE_YES) {
                  eeglFree(swapname);
                  return FAIL;
               }
               invo->forceit = true;
            } else {
               showErrFmtMsg(_(e_swap_file_exists_str_silent_overrides), swapname);
               eeglFree(swapname);
               return FAIL;
            }
         }
         eeglFree(swapname);
      }
   }
   return OK;
}


//Write the current book to file "invo->arg".
//If "invo->append" is true, append to the file.
//
//If "*invo->arg == ZERO" write to current file.
//
//Return FAIL for failure, OK otherwise.
int
do_write(Invocation* invo) {
   CS fname = NULL;      // init to shut up gcc
   int retval = FAIL;
   CS free_fname = NULL;
   Book* altBook = NULL;
   int name_was_missing;

   if (isWritingForbidden())      // check 'write' option
      return FAIL;

   CS fullFName = invo->arg;
   Boole sameFile;
   if (*fullFName == ZERO) {
      if (invo->id == C_saveas) {
         emsg(_(e_argument_required));
         goto theend;
      }
      sameFile = true;
   } else {
      fname = fullFName;
      free_fname = fiExpandAndCopy(fullFName, true);
      //When out-of-memory, keep unexpanded file name, because we MUST be
      //able to write the file in this situation.
      if (free_fname)
         fullFName = free_fname;
      sameFile = fNameMatchesCurBook(fullFName);
   }

   // If we have a new file, put its name in the list of alternate file names.
   if (!sameFile) {
      altBook = setaltfname(fullFName, fname, (LineNr)1);
      if (altBook && altBook->mem.mfile) {
         // Overwriting a file that is loaded in another book is not a good idea.
         emsg(_(e_file_is_loaded_in_another_buffer));
         goto theend;
      }
   }

   //A file name is required. "nofile" and "nowrite" books cannot be written implicitly either.
   if (sameFile && (bookDontWrite_msg(curBook) || check_fname() == FAIL
         || check_writable(curBook->fullFileName) == FAIL
         || check_readonly(OUT &invo->forceit, curBook))
   )
      goto theend;

   if (sameFile) {
      fullFName = curBook->fullFileName;
      fname = curBook->currFileName;
      //Not writing the whole file is only allowed with '!'.
      if ((invo->line1 != 1 || invo->line2 != curBook->mem.lineCount)
            && !invo->forceit
            && !invo->append
            && !p_wa
      ){
         if (p_confirm || (commModifierG.cmod_flags & CMOD_CONFIRM)) {
            if (eeDialog_yesno(EE_QUESTION, NULL, (CS)_("Write partial file?"), 2) != EE_YES)
               goto theend;
            invo->forceit = true;
         } else {
            emsg(_(e_use_bang_to_write_partial_buffer));
            goto theend;
         }
      }
   }

   if (check_overwrite(invo, curBook, fname, fullFName, !sameFile) == OK) {
      if (invo->id == C_saveas && altBook) {
         Book* was_curbuf = curBook;

         applyAutocomms(EVENT_BUFFILEPRE, NULL, NULL, false, curBook);
         applyAutocomms(EVENT_BUFFILEPRE, NULL, NULL, false, altBook);
         if (curBook != was_curbuf || aborting()) {
            // book changed, don't change name now
            retval = FAIL;
            goto theend;
         }
         //Exchange the file names for the current and the alternate book. This makes it look 
         //like we are now editing the book under the new name. Must be done before bookWrite(), 
         //because if there is no file name and 'cpo' contains 'F', it will set the file name.
         fname = altBook->currFileName;
         altBook->currFileName = curBook->currFileName;
         curBook->currFileName = fname;
         fname = altBook->fullFileName;
         altBook->fullFileName = curBook->fullFileName;
         curBook->fullFileName = fname;
         fname = altBook->shortFileName;
         altBook->shortFileName = curBook->shortFileName;
         curBook->shortFileName = fname;
         bookHandleNameChange(curBook);

         applyAutocomms(EVENT_BUFFILEPOST, NULL, NULL, false, curBook);
         applyAutocomms(EVENT_BUFFILEPOST, NULL, NULL, false, altBook);
         if (!altBook->o.bookListed) {
            altBook->o.bookListed = true;
            applyAutocomms(EVENT_BUFADD, NULL, NULL, false, altBook);
         }
         if (curBook != was_curbuf || aborting()) {
            // book changed, don't write the file
            retval = FAIL;
            goto theend;
         }

         //If 'filetype' was empty try detecting it now.
         if (!curBook->fileType) {
            if (auGroupExists(S"filetypedetect"))
                (void)do_doautocmd(S"filetypedetect BufRead", true, NULL);
         }

         //Autocommands may have changed book names, esp. when 'autochdir' is set.
         fname = curBook->shortFileName;
      }

      name_was_missing = curBook->fullFileName == NULL;

      retval = bookWrite(curBook, fullFName, fname, invo->line1, invo->line2,
                invo, invo->append, invo->forceit, true, false);

      // After ":saveas fname" reset 'readonly'.
      if (invo->id == C_saveas) {
         if (retval == OK) {
            curBook->o.modifiable = true;
            needRedrawTabpanelG = true;
         }
      }

      // Change directories when the 'acd' option is set and the file name got changed or set.
      if (invo->id == C_saveas || name_was_missing)
          DO_AUTOCHDIR;
   }

theend:
   eeglFree(free_fname);
   return retval;
}

//Isolate one part of a string option where parts are separated with "sep_chars".
//The part is copied into "buf[maxlen]". "*option" is advanced to the next part.
//The length is returned.
int
doCutPathFromListOfPaths(OUT CS* option, OUT CS buf, int maxlen, CS sep_chars){
   int len = 0;
   CS p = *option;

   // skip '.' at start of option part, for 'suffixes'
   if (*p == '.') {
      buf[len] = *p;
      len++;
      p++;
   } 
   while (*p != ZERO && firstOccurrence((CS)sep_chars, *p) == NULL) {
      //Skip backslash before a separator character and space.
      if (p[0] == '\\' && firstOccurrence((CS)sep_chars, p[1]) != NULL)
         ++p;
      if (len < maxlen - 1) {
         buf[len] = *p;
         len++;
      } 
      ++p;
   }
   buf[len] = ZERO;

   if (*p != ZERO && *p != ',')   // skip non-standard separator
      ++p;
   p = skip_to_option_part(p);   // p points to next file name

   *option = p;
   return len;
}


// Handle ":wnext", ":wNext" and ":wprevious" commands.
void
c_wnext(Invocation* invo){
   int      i;
   if (invo->comm[1] == 'n')
      i = curPor->argListInd + (int)invo->line2;
   else
      i = curPor->argListInd - (int)invo->line2;
   invo->line1 = 1;
   invo->line2 = curBook->mem.lineCount;
   if (do_write(invo) != FAIL)
      do_argfile(invo, i);
}

//}}}
//{{{editing files

// ":wall", ":wqall" and ":xall": Write all changed files (and exit).
void
do_wqall(Invocation* invo){
   int error = 0;
   int save_forceit = invo->forceit;

   if (invo->id == C_xall || invo->id == C_wqall) {
      if (before_quit_all(invo) == FAIL)
          return;
      isExitingG = true;
   }

   Book* book;
   FOR_ALL_BOOKS(book) {
      if (isExitingG && term_job_running(book->term)) {
          no_write_message_nobang(book);
          ++error;
      } ei (doWasBookChanged(book) && !bookDontWrite(book)) {
         //Check if there is a reason the book cannot be written:
         //1. if the book is not modifiable
         //2. if there is no file name (even after browsing)
         //3. if the 'readonly' is set (even after a dialog)
         //4. if overwriting is allowed (even after a dialog)
         if (isWritingForbidden()) {
            ++error;
            break;
         }
         if (book->fullFileName == NULL) {
            showErrFmtMsg(_(e_no_file_name_for_buffer_nr), (long)book->fiNum);
            ++error;
         } ei (check_readonly(OUT &invo->forceit, book)
             || check_overwrite(invo, book, book->currFileName, book->fullFileName, false) == FAIL
         ) {
            ++error;
         } else {
            BookRef bufref;

            bookStoreInRef(OUT &bufref, book);
            if (bookWrite_all(book, invo->forceit) == FAIL)
               ++error;
            // an autocommand may have deleted the book
            if (!bookRefValid(&bufref))
               book = firstBook;
         }
         invo->forceit = save_forceit;    // check_overwrite() may set it
      }
   }
   if (isExitingG) {
      if (!error)
         exitEegl(0);
      not_exiting();
   }
}

//Check the @modifiable option. Return true and give a message when it's not set.
private Boole
isWritingForbidden(void) {
   if (curBook->o.modifiable)
      return false;
   emsg(_(e_file_not_written_writing_is_disabled_by_write_option));
   return true;
}

// Check if a book is read-only (either 'modifiable' option is not set or file is
// read-only). Ask for overruling in a dialog. Return true and give an error
// message when the book is readonly.
private Boole
check_readonly(OUT Boole* forceit, Book* book) {
   FileStat   st;

   // Handle a file being readonly when the 'readonly' option is set or when
   // the file exists and permissions are read-only.
   // We will send 0777 to check_file_readonly(), as the "perm" variable is
   // important for device checks but not here.
   if (!*forceit && (!book->o.modifiable
         || (stat((char *)book->fullFileName, &st) >= 0 
               && check_file_readonly(book->fullFileName, 0777)))
   ) {
      if ((p_confirm || (commModifierG.cmod_flags & CMOD_CONFIRM)) && book->currFileName) {
         Byte buff[DIALOG_MSG_SIZE];

         if (!book->o.modifiable)
            dialog_msg(buff, _("'readonly' option is set for \"%s\".\nDo you wish to write anyway?"),
                book->currFileName);
         else {
            dialog_msg(buff, _("File permissions of \"%s\" are read-only.\n"
                     "It may still be possible to write it.\nDo you wish to try?"),
                book->currFileName);
         } 

         if (eeDialog_yesno(EE_QUESTION, NULL, buff, 2) == EE_YES) {
            //Set forceit, to force the writing of a readonly file
            *forceit = true;
            return false;
         } else
            return true;
      } ei (!book->o.modifiable)
         emsg(_(e_readonly_option_is_set_add_bang_to_override));
      else
         showErrFmtMsg(_(e_str_is_read_only_add_bang_to_override), book->currFileName);
      return true;
   }

   return false;
}

//Try to abandon the current file and edit a new or existing file.
//"fnum" is the number of the file, if zero use "ffname_arg"/"sfname_arg".
//"lnum" is the line number for the cursor in the new file (if non-zero).
//
//Return:
//GETFILE_ERROR for "normal" error,
//GETFILE_NOT_WRITTEN for "not written" error,
//GETFILE_SAME_FILE for success
//GETFILE_OPEN_OTHER for successfully opening another file.
int
getfile(
   int fnum,
   CS ffname_arg,
   CS sfname_arg,
   int setpm,
   LineNr lnum,
   Boole forceit
) {
   CS fullFName = ffname_arg;
   CS sfname = sfname_arg;
   int      retval;
   CS free_me = NULL;

   if (!portCheckCanSetCurBookForceIt(forceit))
      return GETFILE_ERROR;

   if (text_locked())
      return GETFILE_ERROR;
   if (curBookLocked())
      return GETFILE_ERROR;

   Boole sameFile;
   if (fnum == 0) {
      // make fullFName full path, set sfname
      fname_expand(&fullFName, &sfname);
      sameFile = fNameMatchesCurBook(fullFName);
      free_me = fullFName;      // has been allocated, free() later
   } else
      sameFile = (fnum == curBook->fiNum);

   if (!sameFile)
      ++no_wait_return;       // don't wait for autowrite message
   if (!sameFile)
      --no_wait_return;
   if (setpm)
      setpcmark();
   if (sameFile) {
      if (lnum != 0)
         curPor->cursor.lnum = lnum;
      check_cursor_lnum();
      beginline(BL_SOL | BL_FIX);
      retval = GETFILE_SAME_FILE;   // it's in the same file
   } ei (startEditingFile(fnum, fullFName, sfname, NULL, lnum,
           ECMD_HIDE + (forceit ? ECMD_FORCEIT : 0),
         curPor) == OK) {
      retval = GETFILE_OPEN_OTHER;   // opened another file
   } else
      retval = GETFILE_ERROR;      // error encountered

   eeglFree(free_me);
   return retval;
}

//start editing a new file
//
//  fnum: file number; if zero use fullFName/sfname
//  fullFName: the file name
//     - full path if sfname used,
//     - any file name if sfname is NULL
//     - empty string to re-edit with the same file name (but may be
//         in a different directory)
//     - NULL to start an empty book
//  sfname: the short file name (or NULL)
//  invo: contains the command to be executed after loading the file and
//       forced 'ff' and 'fenc'
//  newlnum: if > 0: put cursor on this line number (if possible)
//       if ECMD_LASTL: use last position in loaded file
//       if ECMD_LAST: use last position in all files
//       if ECMD_ONE: use first line
//   flags:
//  ECMD_HIDE: if true don't free the current book
//  ECMD_SET_HELP: set kind = BOOK_HELP for (new) book before opening file
//  ECMD_OLDBUF: use existing book if it exists
//  ECMD_FORCEIT: ! used for a command
//  ECMD_ADDBUF: don't edit, just add to book list
//  ECMD_ALTBUF: like ECMD_ADDBUF and also set the alternate file
//  ECMD_NOWINENTER: Do not trigger BufWinEnter
//  ECMD_MODIFIABLE: set the @modifiable flag for the new book
//  oldPort: Should be "curPor" when editing a new book in the current
//       portal, NULL when splitting the portal first.  When not NULL info
//       of the previous book for "oldPort" is stored.
//
//return FAIL for failure, OK otherwise
int
startEditingFile(
   int fnum,
   CS fullFName,
   CS sfname,
   Invocation* invo,         // can be NULL!
   LineNr newlnum,
   Unt flags,
   Portal* oldPort
) {
   if (portErrorIfTermPopup())
      return FAIL;
      
   int oldbuf;           // true if using existing book
   int auto_buf = false; // true if autocommands brought us into the book unexpectedly
   CS new_name = NULL;
   int did_set_swapcommand = false;
   Book* book;
   BookRef bufref;
   BookRef curBookSaved;
   CS free_fname = NULL;
   int retval = FAIL;
   long n;
   Pos orig_pos;
   LineNr topline = 0;
   int newcol = -1;
   int solcol = -1;
   Pos* pos;
   CS command = NULL;
   Unt readfile_flags = 0;
   int did_inc_redrawing_disabled = false;
   long* so_ptr = &curPor->o.scrollOff;
      
   Unt modifiable = flags & ECMD_MODIFIABLE; 

   if (invo)
      command = invo->higherOrderComm;
   bookStoreInRef(OUT &curBookSaved, curBook);

   Boole sameFile;      // true if editing another file
   if (fnum != 0) {
      if (fnum == curBook->fiNum)   // file is already being edited
         return OK;         // nothing to do
      sameFile = false;
   } else {
      // if no short name given, use fullFName for short name
      if (!sfname)
         sfname = fullFName;

      if ((flags & (ECMD_ADDBUF | ECMD_ALTBUF)) && (fullFName == NULL || *fullFName == ZERO))
         goto theend;

      if (fullFName == NULL)
         sameFile = false; // there is no file name
      ei (*fullFName == ZERO && curBook->fullFileName == NULL)
         sameFile = true;
      else {
         if (*fullFName == ZERO)  {        // re-edit with same file name
            fullFName = curBook->fullFileName;
            sfname = curBook->currFileName;
         }
         free_fname = fiExpandAndCopy(fullFName, true); // may expand to full path name
         if (free_fname)
            fullFName = free_fname;
         sameFile = fNameMatchesCurBook(fullFName);
      }
   }

   //If the file was changed we may not be allowed to abandon it:
   //- if we are going to re-edit the same file
   //- or if we are the only portal into this file and if ECMD_HIDE is false
   if (((sameFile && !(flags & ECMD_OLDBUF))
             || (curBook->countPortals == 1
            && !(flags & (ECMD_HIDE | ECMD_ADDBUF | ECMD_ALTBUF)))
       )
       && check_changed(
             curBook, (p_awa ? CCGD_AW : 0)
             | (sameFile ? CCGD_MULTWIN : 0)
             | ((flags & ECMD_FORCEIT) ? CCGD_FORCEIT : 0)
             | (invo == NULL ? 0 : CCGD_EXCMD)
          )
   ) {
      if (fnum == 0 && !sameFile && fullFName)
         (void)setaltfname(fullFName, sfname, newlnum < 0 ? 0 : newlnum);
      goto theend;
   }

   //End Visual mode before switching to another book, so the text can be copied into the 
   //selection book. Careful: may trigger ModeChanged() autocommand
   //Should we block autocommands here?
   reset_VIsual();

   //autocommands freed portal :(
   if (oldPort && !portalIsValid(oldPort))
      oldPort = NULL;

   if ((command || newlnum > (LineNr)0) && *get_EeglVar_str(VV_SWAPCOMMAND) == ZERO) {
      // Set v:swapcommand for the SwapExists autocommands.
      Unt len = command ? STRLEN(command) + 3 : 30;
      CS p = alloc(len);
      if (command)
         eeSnprintf(p, len, ":%s\r", command);
      else
         eeSnprintf(p, len, "%ldG", (long)newlnum);
      set_EeglVar_string(VV_SWAPCOMMAND, p, -1);
      did_set_swapcommand = true;
      eeglFree(p);
   }

   //If we are starting to edit another file, open a (new) book.
   //Otherwise we re-use the current book.
   if (!sameFile) {
      int prev_alt_fnum = curPor->altFnum;

      if (!(flags & (ECMD_ADDBUF | ECMD_ALTBUF))) {
         if ((commModifierG.cmod_flags & CMOD_KEEPALT) == 0)
            curPor->altFnum = curBook->fiNum;
         if (oldPort)
            bookSetPosInPort(curBook, oldPort, oldPort->cursor.lnum, oldPort->cursor.col, true);
      }

      if (fnum) {
         book = bookFindFileByBookNr(fnum);
      } else {
         if (flags & (ECMD_ADDBUF | ECMD_ALTBUF)) {
            //Default the line number to zero to avoid that a wininfo item
            //is added for the current portal.
            LineNr tlnum = 0;

            if (command) {
               tlnum = atol((char *)command);
               if (tlnum <= 0)
                  tlnum = 1L;
            }
            //Add BLN_NOCURWIN to avoid a new wininfo items are associated
            //with the current portal.
            Book* newbuf = bookNew(
               fullFName, sfname, tlnum, modifiable | BLN_LISTED | BLN_NOCURWIN
            );
            if (newbuf) {
               if (flags & ECMD_ALTBUF)
                  curPor->altFnum = newbuf->fiNum;
               if (tlnum > 0)
                  newbuf->lastCursor.lnum = tlnum;
            }
            goto theend;
         }
         book = bookNew(
            fullFName, sfname, 0L, 
            modifiable | BLN_CURBOOK | ((flags & ECMD_SET_HELP) ? 0 : BLN_LISTED)
         );
             
         // autocommands may change curPor and curBook
         if (oldPort)
            oldPort = curPor;
         bookStoreInRef(OUT &curBookSaved, curBook);
      }
      if (!book)
         goto theend;
      // autocommands try to edit a closing book, which like splitting, can
      // result in more portal displaying it; abort
      if (book->lockedSplit) {
         // portal was split, but not editing the new book, reset countPortals again
         if (oldPort == NULL && curPor->book != NULL && curPor->book->countPortals > 1)
            --curPor->book->countPortals;
         emsg(_(e_cannot_switch_to_a_closing_buffer));
         goto theend;
      }
      if (curPor->altFnum == book->fiNum && prev_alt_fnum != 0)
         // reusing the book, keep the old alternate file
         curPor->altFnum = prev_alt_fnum;

      if (book->mem.mfile == NULL) {    // no memfile yet
         oldbuf = false;
      } else {              // existing memfile
         oldbuf = true;
         bookStoreInRef(OUT &bufref, book);
         (void)fiCheckBookTimestamp(book);
         // Check if autocommands made the book invalid or changed the current book.
         if (!bookRefValid(&bufref) || curBook != curBookSaved.c)
            goto theend;
         if (aborting())       // autocmds may abort script processing
            goto theend;
      }

      // May jump to last used line number for a loaded book or when asked for explicitly
      if ((oldbuf && newlnum == ECMD_LASTL) || newlnum == ECMD_LAST) {
         pos = bookFindFpos(book);
         newlnum = pos->lnum;
         solcol = pos->col;
      }

      //Make the (new) book the one used by the current portal.
      //If the old book becomes unused, free it if ECMD_HIDE is false.
      //If the current buffer was empty and has no file name, curBook
      //is returned by bookNew(), nothing to do here.
      if (book != curBook) {
         BookRef save_auNewCurBuf;
         int save_commPortTypeG = commPortTypeG;
         Portal* save_commPortPortG = commPortPortG;

         // Should only be possible to get here if the commporta is closed, or
         // if it's opening and its buffer hasn't been set yet (the new buffer is for it).
         assert(commPortBookG == NULL);

         // BufLeave applies to the old buffer.
         commPortTypeG = 0;
         commPortPortG = NULL;

         //Be careful: The autocommands may delete any buffer and change the current buffer.
         //- If the buffer we are going to edit is deleted, give up.
         //- If the current buffer is deleted, prefer to load the new buffer when loading a 
         //  buffer is required. This avoids loading another buffer which then must be closed again.
         //- If we ended up in the new buffer already, need to skip a few things, set auto_buf.
         if (book->currFileName != NULL)
            new_name = copyStr(book->currFileName);
         save_auNewCurBuf = auNewCurBookG;
         bookStoreInRef(OUT &auNewCurBookG, book);
         applyAutocomms(EVENT_BUFLEAVE, NULL, NULL, false, curBook);

         commPortTypeG = save_commPortTypeG;
         commPortPortG = save_commPortPortG;

         if (!bookRefValid(&auNewCurBookG)) {
            // new buffer has been deleted
            delbuf_msg(new_name);   // frees new_name
            auNewCurBookG = save_auNewCurBuf;
            goto theend;
         }
         if (aborting()) {     // autocmds may abort script processing
            eeglFree(new_name);
            auNewCurBookG = save_auNewCurBuf;
            goto theend;
         }
         if (book == curBook)      // already in new buffer
            auto_buf = true;
         else {
            Portal* the_curPor = curPor;
            int did_decrement;
            Book* was_curbuf = curBook;

            //Set the locked flag to avoid that autocommands close the
            //portal. And set locked for the same reason.
            the_curPor->locked = true;
            ++book->locked;

            if (curBook == curBookSaved.c)
               optsCopyToBook(book, BCO_ENTER);

            // Close the link to the current buffer. This will set oldPort->buffer to NULL.
            u_sync(false);
            did_decrement = closeBook(oldPort, curBook,
                (flags & ECMD_HIDE) ? 0 : DOBOOK_UNLOAD, false, false);

            // Autocommands may have closed the portal.
            if (portalIsValid(the_curPor))
               the_curPor->locked = false;
            --book->locked;

            // autocmds may abort script processing
            if (aborting() && curPor->book != NULL) {
               eeglFree(new_name);
               auNewCurBookG = save_auNewCurBuf;
               goto theend;
            }
            // Be careful again, like above.
            if (!bookRefValid(&auNewCurBookG)) {
               // new buffer has been deleted
               delbuf_msg(new_name);   // frees new_name
               auNewCurBookG = save_auNewCurBuf;
               goto theend;
            }
            if (book == curBook) {    // already in new buffer
               // closeBook() has decremented the portal count,
               // increment it again here and restore buffer.
               if (did_decrement && bookIsValid(was_curbuf))
                  ++was_curbuf->countPortals;
               if (doesPortalExistInAnyTab(oldPort) && !oldPort->book)
                  oldPort->book = was_curbuf;
               auto_buf = true;
            } else {
               // <VN> We could instead free the synblock and re-attach to buffer, perhaps.
               if (!curPor->book || curPor->ownSyntax == &(curPor->book->syntax))
                  curPor->ownSyntax = &(book->syntax);
               curPor->book = book;
               curBook = book;
               ++curBook->countPortals;

               // Set 'binary' when forced.
               if (!oldbuf && invo != NULL) {
                  set_file_options(invo);
               }
            }

            //May get the portal options from the last time this buffer was in this portal (or 
            //another portal). If not used before, reset the local window options to the global
            //values. Also restores old folding stuff.
            get_winopts(curBook);
         }
         eeglFree(new_name);
         auNewCurBookG = save_auNewCurBuf;
      }

      curPor->prevContextMark.lnum = 1;
      curPor->prevContextMark.col = 0;
   } else { // sameFile
      if ((flags & (ECMD_ADDBUF | ECMD_ALTBUF)) != 0 || check_fname() == FAIL)
         goto theend;
      oldbuf = (flags & ECMD_OLDBUF);
   }

   // Don't redraw until the cursor is in the right line, otherwise
   // autocommands may cause ml_get errors.
   ++isRedrawingDisabledG;
   did_inc_redrawing_disabled = true;

   book = curBook;
   if ((flags & ECMD_SET_HELP) || keep_help_flag) {
      prepare_help_buffer();
   } else {
      // Don't make a buffer listed if it's a help buffer.  Useful when
      // using CTRL-O to go back to a help file.
      if (curBook->kind != BOOK_HELP)
         bookSetBooklisted(true);
   }

   // If autocommands change buffers under our fingers, forget about editing the file.
   if (book != curBook)
      goto theend;
   if (aborting())       // autocmds may abort script processing
      goto theend;

   //Since we are starting to edit a file, consider the filetype to be
   //unset.  Helps for when an autocommand changes files and expects syntax
   //highlighting to work in the other file.
   curBook->didFiletype = false;

   //sameFile   oldbuf
   // true    false      re-edit same file, buffer is re-used
   // true    true       re-edit same file, nothing changes
   // false   false      start editing new file, new buffer
   // false   true       start editing in existing buffer (nothing to do)
   if (sameFile && !oldbuf) {    //re-use the buffer
      set_last_cursor(curPor);   //may set lastCursor
      if (newlnum == ECMD_LAST || newlnum == ECMD_LASTL) {
         newlnum = curPor->cursor.lnum;
         solcol = curPor->cursor.col;
      }
      book = curBook;
      if (book->currFileName)
         new_name = copyStr(book->currFileName);
      else
         new_name = NULL;
      bookStoreInRef(OUT &bufref, book);

      // If the buffer was used before, store the current contents so that
      // the reload can be undone.  Do not do this if the (empty) buffer is
      // being re-used for another file.
      if (!(curBook->flags & BF_NEVERLOADED) && (p_ur < 0 || curBook->mem.lineCount <= p_ur)) {
         // Sync first so that this is a separate undo-able action.
         u_sync(false);
         if (u_savecommon(0, curBook->mem.lineCount + 1, 0, true) == FAIL) {
            eeglFree(new_name);
            goto theend;
         }
         u_unchanged(curBook);
         bookFreeAll(curBook, BFA_KEEP_UNDO);

         // tell readfile() not to clear or reload undo info
         readfile_flags = READ_KEEP_UNDO;
      } else
         bookFreeAll(curBook, 0);   // free all things for buffer

      // If autocommands deleted the buffer we were going to re-edit, give
      // up and jump to the end.
      if (!bookRefValid(&bufref)) {
         delbuf_msg(new_name);   // frees new_name
         goto theend;
      }
      eeglFree(new_name);

      // If autocommands change buffers under our fingers, forget about
      // re-editing the file.  Should do the buf_clear_file(), but perhaps
      // the autocommands changed the buffer...
      if (book != curBook)
         goto theend;
      if (aborting())       // autocmds may abort script processing
         goto theend;
      buf_clear_file(curBook);
      curBook->opStart.lnum = 0;   // clear '[ and '] marks
      curBook->opEnd.lnum = 0;
    }

   // If we got here we are sure to start editing Assume success now
   retval = OK;

   // If the file name was changed, reset the not-edit flag so that ":write" works.
   if (sameFile)
      curBook->flags &= ~BF_NOTEDITED;

   //Check if we are editing the argListInd file in the argument list.
   check_arg_idx(curPor);

   if (!auto_buf) {
      //Set cursor and init portal before reading the file and executing
      //autocommands.  This allows for the autocommands to position the cursor.
      curPor_init();

      // It's possible that all lines in the buffer changed.  Need to update
      // automatic folding for all windows where it's used.
      {
         Portal* port;
         Tab* t;
         FOR_ALL_TAB_PORTALS(t, port) {
            if (port->book == curBook)
               foldUpdateAll(port);
         } 
      }

      // Change directories when the 'acd' option is set.
      DO_AUTOCHDIR;

      //Careful: bookOpenFromInvo() and applyAutocomms() may change the current buffer and portal
      orig_pos = curPor->cursor;
      topline = curPor->topLine;
      if (!oldbuf) {    // need to read the file
         // Don't use the swap-exists dialog for a popup window, can't edit the buffer.
         if (PORTAL_IS_POPUP(curPor))
            curBook->flags |= BF_NO_SEA;
         swap_exists_action = SEA_DIALOG;
         curBook->flags |= BF_CHECK_RO; // set/reset 'ro' flag

         //Open the buffer and read the file.
         if (flags & ECMD_NOWINENTER)
            readfile_flags |= READ_NOWINENTER;
         if (should_abort(bookOpenFromInvo(false, invo, readfile_flags)))
            retval = FAIL;

         curBook->flags &= ~BF_NO_SEA;
         if (swap_exists_action == SEA_QUIT)
            retval = FAIL;
         handle_swap_exists(&curBookSaved);
      } else {
         applyAutocommsRetval(EVENT_BUFENTER, NULL, NULL, false, curBook, &retval);
         if ((flags & ECMD_NOWINENTER) == 0)
            applyAutocommsRetval(EVENT_BUFWINENTER, NULL, NULL, false, curBook, &retval);
      }
      check_arg_idx(curPor);

      // If autocommands change the cursor position or topline, we should
      // keep it.  Also when it moves within a line. But not when it moves to the first non-blank.
      if (!EQUAL_POS(curPor->cursor, orig_pos)) {
         CS text = ml_get_curline();

         if (curPor->cursor.lnum != orig_pos.lnum
              || curPor->cursor.col != (int)(skipwhite(text) - text)) {
            newlnum = curPor->cursor.lnum;
            newcol = curPor->cursor.col;
         }
      }
      if (curPor->topLine == topline)
          topline = 0;

      // Even when cursor didn't move we need to recompute topline.
      changed_line_abv_curs();

      if (PORTAL_IS_POPUP(curPor) && curPor->isPreview && retval != FAIL)
         setPopupTitle(curPor);
   }

   // Tell the diff stuff that this buffer is new and/or needs updating. Also needed when 
   // re-editing the same buffer, because unloading will have removed it as a diff buffer.
   if (curPor->o.diff) {
      diffAddBook(curBook);
      diff_invalidate(curBook);
   }

   if (command == NULL) {
      if (newcol >= 0) {  // position set by autocommands
          curPor->cursor.lnum = newlnum;
          curPor->cursor.col = newcol;
          check_cursor();
      } ei (newlnum > 0) {  // line number from caller or old position
          curPor->cursor.lnum = newlnum;
          check_cursor_lnum();
          if (solcol >= 0 && !p_sol) {
            // 'sol' is off: Use last known column.
            curPor->cursor.col = solcol;
            check_cursor_col();
            curPor->cursor.coladd = 0;
            curPor->setCursWant = true;
         }
         else
            beginline(BL_SOL | BL_FIX);
      } else {        // no line number
         beginline(BL_WHITE | BL_FIX);
      }
   }

   // Check if cursors in other portals into the same buffer are still valid
   check_lnums(false);

   //Did not read the file, need to show some info about the file. Do this after setting the cursor
   if (oldbuf && !auto_buf) {
      int msg_scroll_save = msg_scroll;

      // Obey the 'O' flag in 'cpoptions': overwrite any previous file
      // message.
      if (!isExitingG && p_verbose == 0)
         msg_scroll = false;
      if (!msg_scroll)   // wait a bit when overwriting an error msg
         check_for_delay(false);
      msg_start();
      msg_scroll = msg_scroll_save;
      msg_scrolled_ign = true;

      fileinfo(false, true, false);

      msg_scrolled_ign = false;
    }

    curBook->lastUsed = eeTime();

   if (command)
      doCommand(command, NULL, NULL, DOCMD_VERBOSE);

   if (isRedrawingDisabledG > 0)
      --isRedrawingDisabledG;
   did_inc_redrawing_disabled = false;
   if (!skip_redraw) {
      n = *so_ptr;
      if (topline == 0 && !command)
         *so_ptr = 9999;      // force cursor halfway the portal
      update_topline();
      curPor->scbindPos = curPor->topLine;
      *so_ptr = n;
      drawCurBookLater(UPD_NOT_VALID);   // redraw this buffer later
   }

theend:
   if (did_inc_redrawing_disabled && isRedrawingDisabledG > 0)
      --isRedrawingDisabledG;
   if (did_set_swapcommand)
      set_EeglVar_string(VV_SWAPCOMMAND, NULL, -1);
   eeglFree(free_fname);
   return retval;
}

private void
delbuf_msg(CS name) {
   showErrFmtMsg(_(e_autocommands_unexpectedly_deleted_new_buffer_str), name == NULL ? (CS)"" : name);
   eeglFree(name);
   auNewCurBookG.c = NULL;
   auNewCurBookG.freeCount = 0;
}

private int append_indent = 0;       // autoindent for first line

// ":insert" and ":append", also used by ":change"
void
c_append(Invocation* invo) {
   CS theline;
   int did_undo = false;
   LineNr lnum = invo->line2;
   int indent = 0;
   CS p;
   int vcol;
   int empty = (curBook->mem.flags & ML_EMPTY);

   // the ! flag toggles autoindent
   if (invo->forceit)
      curBook->o.autoIndent = !curBook->o.autoIndent;

   // First autoindent comes from the line we start on
   if (invo->id != C_change && curBook->o.autoIndent && lnum > 0)
      append_indent = get_indent_lnum(lnum);

   if (invo->id != C_append)
      --lnum;

   // when the buffer is empty need to delete the dummy line
   if (empty && lnum == 1)
      lnum = 0;

   stateG = MODE_INSERT;          // behave like in Insert mode
   if (curBook->o.b_p_iminsert == B_IMODE_LMAP)
      stateG |= MODE_LANGMAP;

   for (;;) {
      msg_scroll = true;
      need_wait_return = false;
      if (curBook->o.autoIndent) {
         if (append_indent >= 0) {
            indent = append_indent;
            append_indent = -1;
         } ei (lnum > 0)
            indent = get_indent_lnum(lnum);
      }
      if (*invo->arg == '|') {
         //Get the text after the trailing bar.
         theline = copyStr(invo->arg + 1);
         *invo->arg = ZERO;
      } ei (invo->ea_getline == NULL) {
         // No getline() function, use the lines that follow. This ends when there is no more.
         if (invo->nextComm == NULL)
            break;
         p = firstOccurrence(invo->nextComm, NL);
         if (!p)
            p = invo->nextComm + STRLEN(invo->nextComm);
         theline = copySubstr(invo->nextComm, p - invo->nextComm);
         if (*p != ZERO)
            ++p;
         else
            p = NULL;
         invo->nextComm = p;
      } else {
         int save_State = stateG;

         // Set stateG to avoid the cursor shape to be set to MODE_INSERT
         // state when getline() returns.
         stateG = MODE_COMMLINE;
         theline = invo->ea_getline(
             invo->cstack->loopLevel > 0 ? -1 :
             ZERO, invo->cookie, indent, true
         );
         stateG = save_State;
      }
      lines_left = visibleRowsG - 1;
      if (theline == NULL)
          break;


      // Look for the "." after automatic indent.
      vcol = 0;
      for (p = theline; indent > vcol; ++p) {
         if (*p == ' ')
            ++vcol;
         ei (*p == TAB)
            vcol += 8 - vcol % 8;
         else
            break;
      }
      if ((p[0] == '.' && p[1] == ZERO)
         || (!did_undo && u_save(lnum, lnum + 1 + (empty ? 1 : 0)) == FAIL))
      {
         eeglFree(theline);
         break;
      }

      // don't use autoindent if nothing was typed.
      if (p[0] == ZERO)
         theline[0] = ZERO;

      did_undo = true;
      ml_append(lnum, theline, (ColNr)0, false);
      if (empty)
         // there are no marks below the inserted lines
         appended_lines(lnum, 1L);
      else
         appended_lines_mark(lnum, 1L);

      eeglFree(theline);
      ++lnum;

      if (empty) {
         ml_delete(2L);
         empty = false;
      }
   }
   stateG = MODE_NORMAL;

   if (invo->forceit)
      curBook->o.autoIndent = !curBook->o.autoIndent;

   //"start" is set to invo->line2+1 unless that position is invalid (when
   //invo->line2 pointed to the end of the buffer and nothing was appended)
   //"end" is set to lnum when something has been appended, otherwise
   //it is the same as "start"  -- Acevedo
   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
      curBook->opStart.lnum = (invo->line2 < curBook->mem.lineCount) ?
          invo->line2 + 1 : curBook->mem.lineCount;
      if (invo->id != C_append)
          --curBook->opStart.lnum;
      curBook->opEnd.lnum = (invo->line2 < lnum)
                        ? lnum : curBook->opStart.lnum;
      curBook->opStart.col = curBook->opEnd.col = 0;
   }
   curPor->cursor.lnum = lnum;
   check_cursor_lnum();
   beginline(BL_SOL | BL_FIX);

   need_wait_return = false;   // don't use wait_return() now
   ex_no_reprint = true;
}

//":change"
void
c_change(Invocation* invo) {
   if (invo->line2 >= invo->line1 && u_save(invo->line1 - 1, invo->line2 + 1) == FAIL)
      return;

   //the ! flag toggles autoindent
   if (invo->forceit ? !curBook->o.autoIndent : curBook->o.autoIndent)
      append_indent = get_indent_lnum(invo->line1);

   LineNr lnum;
   for (lnum = invo->line2; lnum >= invo->line1; --lnum) {
      if (curBook->mem.flags & ML_EMPTY)       // nothing to delete
         break;
      ml_delete(invo->line1);
   }

   // make sure the cursor is not beyond the end of the file now
   check_cursor_lnum();
   deleted_lines_mark(invo->line1, (long)(invo->line2 - lnum));

   // ":append" on the line above the deleted lines.
   invo->line2 = invo->line1;
   c_append(invo);
}

// "z" family of commands
void
c_z(Invocation* invo) {
   long bigness;
   CS kind;
   int minus = 0;
   LineNr start, end, curs, i;
   int j;
   LineNr lnum = invo->line2;

   // Vi compatible: ":z!" uses display height, without a count uses 'scroll'
   if (invo->forceit)
      bigness = visibleRowsG - 1;
   ei (!ONLY_ONE_PORTAL)
      bigness = curPor->height - 3;
   else
      bigness = curPor->scroll * 2;
   if (bigness < 1)
      bigness = 1;

   CS x = invo->arg;
   kind = x;
   if (*kind == '-' || *kind == '+' || *kind == '=' || *kind == '^' || *kind == '.')
      ++x;
   while (*x == '-' || *x == '+') {
      ++x;
   } 

   if (*x != 0) {
      if (!EE_ISDIGIT(*x)) {
         emsg(_(e_non_numeric_argument_to_z));
         return;
      } else {
         bigness = atol((char *)x);

         // bigness could be < 0 if atol(x) overflows.
         if (bigness > 2 * curBook->mem.lineCount || bigness < 0)
            bigness = 2 * curBook->mem.lineCount;

         if (*kind == '=')
            bigness += 2;
      }
   }

   // the number of '-' and '+' multiplies the distance
   if (*kind == '-' || *kind == '+') {
      for (x = kind + 1; *x == *kind; ++x)
          ;
   }

   switch (*kind) {
   case '-':
      start = lnum - bigness * (LineNr)(x - kind) + 1;
      end = start + bigness - 1;
      curs = end;
      break;
   case '=':
      start = lnum - (bigness + 1) / 2 + 1;
      end = lnum + (bigness + 1) / 2 - 1;
      curs = lnum;
      minus = 1;
      break;
   case '^':
      start = lnum - bigness * 2;
      end = lnum - bigness;
      curs = lnum - bigness;
      break;
   case '.':
      start = lnum - (bigness + 1) / 2 + 1;
      end = lnum + (bigness + 1) / 2 - 1;
      curs = end;
      break;
   default:  // '+'
      start = lnum;
      if (*kind == '+')
         start += bigness * (LineNr)(x - kind - 1) + 1;
      ei (invo->addr_count == 0)
         ++start;
      end = start + bigness - 1;
      curs = end;
      break;
   }

   if (start < 1)
      start = 1;

   if (end > curBook->mem.lineCount)
      end = curBook->mem.lineCount;

   if (curs > curBook->mem.lineCount)
      curs = curBook->mem.lineCount;
   ei (curs < 1)
      curs = 1;

   for (i = start; i <= end; i++) {
      if (minus && i == lnum) {
         msg_putchar('\n');

         for (j = 1; j < visibleColsG; j++)
            msg_putchar('-'); 
      }

      print_line(i, invo->flags & EXFLAG_LIST);

      if (minus && i == lnum) {
         msg_putchar('\n');

         for (j = 1; j < visibleColsG; j++)
            msg_putchar('-');
      }
   }

   if (curPor->cursor.lnum != curs) {
      curPor->cursor.lnum = curs;
      curPor->cursor.col = 0;
   }
   ex_no_reprint = true;
}

//}}}
//{{{substitutions

private CS prevSubstS = NULL;   // previous substitute pattern
private Boole globalNeedBeginlineS = false;   // call beginline() after ":g"

// Flags that are kept between calls to :substitute.
typedef struct {
   Boole do_all;    // do multiple substitutions per line
   Boole do_ask;    // ask for confirmation
   Boole do_count;  // count only
   Boole do_error;  // if false, ignore errors
   Boole do_print;  // print last line with subs.
   Boole do_list;   // list last line with subs.
   Boole do_number; // list last line with line nr
   Boole do_ic;     // ignore case flag
} SubstitutionState;

// Skip over the "sub" part in :s/pat/sub/ where "delimiter" is the separating character.
CS
skip_substitute(CS start, int delimiter) {
   CS p = start;

   while (p[0]) {
      if (p[0] == delimiter) {    // end delimiter found
          *p++ = ZERO;         // replace it with a ZERO
          break;
      }
      if (p[0] == '\\' && p[1] != 0)   // skip escaped characters
          ++p;
      MB_PTR_ADV(p);
   }
   return p;
}

private int
check_regexp_delim(int c) {
   if (SAFE_isalpha(c)) {
      emsg(_(e_regular_expressions_cant_be_delimited_by_letters));
      return FAIL;
   }
   return OK;
}

// Perform a substitution from line invo->line1 to line invo->line2 using the
// command pointed to by invo->arg which should be of the form:
//
// /pattern/substitution/{flags}
//
// The usual escapes are supported as described in the regexp docs.
// :S is the case-sensitive variant
// The & repeats previous substitute command
void
c_substitute(Invocation* invo) {
   LineNr lnum;
   long i = 0;
   RegMultilineMatch regmatch;
   static SubstitutionState subflags = {false, false, false, true, false, false, false, 0};
   SubstitutionState subflags_save;
   int save_do_all;      // remember user specified 'g' flag
   int save_do_ask;      // remember user specified 'c' flag
   Text pat = (Text){null, 0};
   CS sub = null;
   int delimiter;
   int sublen;
   int got_quit = false;
   int got_match = false;
   int which_pat;
   CS p;
   int  save_State;
   LineNr first_line = 0;      // first changed line
   LineNr last_line = 0;      // below last changed line AFTER the change
   LineNr old_line_count = curBook->mem.lineCount;
   LineNr line2;
   long nmatch;         // number of lines in match
   int endcolumn = false;   // cursor in last column when done
   Pos old_cursor = curPor->cursor;
   int keeppatterns = commModifierG.cmod_flags & CMOD_KEEPPATTERNS;
   int save_ma = 0;
   TextProp* text_props = NULL;

   CS cmd = invo->arg;
   if (!global_busy) {
      sub_nsubs = 0;
      sub_nlines = 0;
   }
   int start_nsubs = sub_nsubs;

   if (invo->id == C_tilde)
      which_pat = RE_LAST; // use last used regexp
   else
      which_pat = RE_SUBST; // use last substitute regexp new pattern and substitution
      
   if ((invo->comm[0] == 's' || invo->comm[0] == 'S') && *cmd != ZERO && !SPACE_OR_TAB(*cmd)
      && firstOccurrence(S"0123456789cegriIp|\"", *cmd) == NULL
   ) {
      // don't accept alphanumeric for separator
      if (check_regexp_delim(*cmd) == FAIL)
          return;

      // undocumented vi feature:
      //  "\/sub/" and "\?sub?" use last used search pattern (almost like
      //  //sub/r).  "\&sub&" use last substitute pattern (like //sub/).
      if (*cmd == '\\') {
         ++cmd;
         if (firstOccurrence((CS)"/?&", *cmd) == NULL) {
            emsg(_(e_backslash_should_be_followed_by));
            return;
         }
         if (*cmd != '&')
            which_pat = RE_SEARCH;       // use last '/' pattern
         pat = (Text){null, 0};          // empty search pattern
         delimiter = *cmd++;          // remember delimiter character
      } else {     // find the end of the regexp
         which_pat = RE_LAST;       // use last used regexp
         delimiter = *cmd++;          // remember delimiter character
         pat = mbText(cmd);             // remember start of search pat
         cmd = skip_regexp_ex(cmd, delimiter, true, &invo->arg, NULL, NULL);
         if (cmd[0] == delimiter)       // end delimiter found
            *cmd++ = ZERO;          // replace it with a ZERO
      }

      //Small incompatibility: vi sees '\n' as end of the command, but in
      //Eeegl we want to use '\n' to find/substitute a ZERO.
      p = cmd;       // remember the start of the substitution
      cmd = skip_substitute(cmd, delimiter);
      sub = copyStr(p);

      if (!invo->skip) {
         if (!keeppatterns) {
            eeglFree(prevSubstS);
            prevSubstS = copyStr(sub);
         }
      }
   } ei (!invo->skip) {  // use previous pattern and substitution
      if (!prevSubstS) {  // there is no previous command
         emsg(_(e_no_previous_substitute_regular_expression));
         return;
      }
      pat = (Text){null, 0};      // search_regcomp() will use previous pattern
      sub = copyStr(prevSubstS);

      //Vi compatibility quirk: repeating with ":s" keeps the cursor in the
      //last column after using "$".
      endcolumn = (curPor->cursWant == MAXCOL);
   }

   //Recognize ":%s/\n//" and turn it into a line join, which is much more efficient.
   //TODO: find a generic solution to make line-joining operations more
   //efficient, avoid allocating a string that grows in size.
   if (pat.len > 1 && STRCMP(pat.c, "\\n") == 0
       && *sub == ZERO
       && (*cmd == ZERO || (cmd[1] == ZERO && (*cmd == 'g' || *cmd == 'l'
                    || *cmd == 'p' || *cmd == '#'))))
    {
      if (invo->skip) {
          eeglFree(sub);
          return;
      }
      curPor->cursor.lnum = invo->line1;
      if (*cmd == 'l')
          invo->flags = EXFLAG_LIST;
      ei (*cmd == '#')
          invo->flags = EXFLAG_NR;
      ei (*cmd == 'p')
          invo->flags = EXFLAG_PRINT;

      //The number of lines joined is the number of lines in the range plus
      //one.  One less when the last line is included.
      LineNr joined_lines_count = invo->line2 - invo->line1 + 1;
      if (invo->line2 < curBook->mem.lineCount)
         ++joined_lines_count;
      if (joined_lines_count > 1) {
         (void)jugJoinLinesUnderCursor(joined_lines_count, false, true, false, true);
         sub_nsubs = joined_lines_count - 1;
         sub_nlines = 1;
         (void)do_sub_msg(false);
         mayPrint(invo);
      }

      if (!keeppatterns)
         save_re_pat(RE_SUBST, pat, true);
      // put pattern in history
      scrAddToHistory(HIST_SEARCH, pat, true, ZERO);
      eeglFree(sub);

      return;
   }

   // Find trailing options.  When '&' is used, keep old options.
   if (*cmd == '&')
      ++cmd;
   else {
      subflags.do_all = true; // default is global on
      subflags.do_ask = false;
      subflags.do_error = true;
      subflags.do_print = false;
      subflags.do_list = false;
      subflags.do_count = false;
      subflags.do_number = false;
      subflags.do_ic = 0;
   }
   while (*cmd) {
      //Note that 'g' and 'c' are always inverted, 'r' is never inverted.
      if (*cmd == 'g')
         subflags.do_all = !subflags.do_all;
      ei (*cmd == 'c')
         subflags.do_ask = !subflags.do_ask;
      ei (*cmd == 'n')
         subflags.do_count = true;
      ei (*cmd == 'e')
         subflags.do_error = !subflags.do_error;
      ei (*cmd == 'r')       // use last used regexp
         which_pat = RE_LAST;
      ei (*cmd == 'p')
         subflags.do_print = true;
      ei (*cmd == '#') {
         subflags.do_print = true;
         subflags.do_number = true;
      } ei (*cmd == 'l') {
         subflags.do_print = true;
         subflags.do_list = true;
      } ei (*cmd == 'i')       // ignore case
         subflags.do_ic = 'i';
      ei (*cmd == 'I')       // don't ignore case
         subflags.do_ic = 'I';
      else
         break;
      ++cmd;
   }
   if (subflags.do_count)
      subflags.do_ask = false;

   save_do_all = subflags.do_all;
   save_do_ask = subflags.do_ask;

   // check for a trailing count
   cmd = skipwhite(cmd);
   if (EE_ISDIGIT(*cmd)) {
      i = parseLong(&cmd);
      if (i <= 0 && !invo->skip && subflags.do_error) {
         emsg(_(e_positive_count_required));
         eeglFree(sub);
         return;
      } ei (i >= INT_MAX) {
         Byte buf[20];
         eeSnprintf(buf, sizeof(buf), "%ld", i);
         showErrFmtMsg(_(e_val_too_large), buf);
         eeglFree(sub);
         return;
      }
      invo->line1 = invo->line2;
      invo->line2 += i - 1;
      if (invo->line2 > curBook->mem.lineCount)
         invo->line2 = curBook->mem.lineCount;
   }

   // check for trailing command or garbage
   cmd = skipwhite(cmd);
   if (*cmd && !isComment(cmd)) {      // if not end-of-line or comment
      set_nextcmd(OUT invo, cmd);
      if (invo->nextComm == NULL) {
         showErrFmtMsg(_(e_trailing_characters_str), cmd);
         eeglFree(sub);
         return;
      }
   }

   if (invo->skip) {      // not executing commands, only parsing
      eeglFree(sub);
      return;
   }

   if (!subflags.do_count && !curBook->o.modifiable) {
      // Substitution is not allowed in immutable buffers
      emsg(_(e_cannot_make_changes_modifiable_is_off));
      eeglFree(sub);
      return;
   }

   if (search_regcomp(pat, NULL, RE_SUBST, which_pat, SEARCH_HIS, OUT &regmatch) == FAIL) {
      if (subflags.do_error)
         emsg(_(e_invalid_command));
      eeglFree(sub);
      return;
   }

   // the 'i' or 'I' flag overrules 'ignorecase' and 'smartcase'
   if (invo->comm[0] == 'S') {
      regmatch.rmm_ic = false;
   } ei (subflags.do_ic == 'i') {
      regmatch.rmm_ic = true;
   } ei (subflags.do_ic == 'I') {
      regmatch.rmm_ic = false;
   }

   CS sub_firstline = NULL;// allocated copy of first sub line

   // If the substitute pattern starts with "\=" then it's an expression.
   // Make a copy, a recursive function may free it.
   // Otherwise, '~' in the substitute pattern is replaced with the old
   // pattern.  We do it here once to avoid it to be replaced over and over again.
   if (sub[0] == '\\' && sub[1] == '=') {
      p = copyStr(sub);
      eeglFree(sub);
      sub = p;
   } else {
      p = regtilde(sub);

      if (p != sub) {
          eeglFree(sub);
          sub = p;
      }
   }

   // Check for a match on each line.
   line2 = invo->line2;
   for (lnum = invo->line1; lnum <= line2 && !(got_quit || aborting()); ++lnum) {
      nmatch = eeRegexec_multi(&regmatch, curPor, curBook, lnum, (ColNr)0, NULL);
      if (nmatch) {
         ColNr   copycol;
         ColNr   matchcol;
         ColNr   prev_matchcol = MAXCOL;
         CS new_end;
         CS new_start = null;
         unsigned   new_start_len = 0;
         CS p1;
         int did_sub = false;
         int lastone;
         int len, copy_len, needed_len;
         long   nmatch_tl = 0;   // nr of lines matched below lnum
         int do_again;   // do it again after joining lines
         int skip_match = false;
         LineNr   sub_firstlnum;   // nr of first sub line
         int apc_flags = APC_SAVE_FOR_UNDO | APC_SUBSTITUTE;
         ColNr total_added =  0;
         int text_prop_count = 0;

         // The new text is build up step by step, to avoid too much
         // copying.  There are these pieces:
         // sub_firstline   The old text, unmodified.
         // copycol      Column in the old text where we started
         //         looking for a match; from here old text still
         //         needs to be copied to the new text.
         // matchcol      Column number of the old text where to look
         //         for the next match.  It's just after the
         //         previous match or one further.
         // prev_matchcol   Column just after the previous match (if any).
         //         Mostly equal to matchcol, except for the first
         //         match and after skipping an empty match.
         // regmatch.*pos   Where the pattern matched in the old text.
         // new_start   The new text, all that has been produced so far.
         // new_end      The new text, where to append new text.
         //
         // lnum      The line number where we found the start of
         //         the match.  Can be below the line we searched
         //         when there is a \n before a \zs in the
         //         pattern.
         // sub_firstlnum   The line number in the buffer where to look
         //         for a match.  Can be different from "lnum"
         //         when the pattern or substitute string contains
         //         line breaks.
         //
         // Special situations:
         // - When the substitute string contains a line break, the part up
         //   to the line break is inserted in the text, but the copy of
         //   the original line is kept.  "sub_firstlnum" is adjusted for the inserted lines.
         // - When the matched pattern contains a line break, the old line
         //   is taken from the line at the end of the pattern.  The lines
         //   in the match are deleted later, "sub_firstlnum" is adjusted accordingly.
         //
         // The new text is built up in new_start[].  It has some extra
         // room to avoid using alloc()/free() too often.  new_start_len is
         // the length of the allocated memory at new_start.
         //
         // Make a copy of the old line, so it won't be taken away when
         // updating the screen or handling a multi-line match.  The "old_"
         // pointers point into this copy.
         sub_firstlnum = lnum;
         copycol = 0;
         matchcol = 0;

         // At first match, remember current cursor position.
         if (!got_match) {
            setpcmark();
            got_match = true;
         }

         // Loop until nothing more to replace in this line.
         // 1. Handle match with empty string.
         // 2. If do_ask is set, ask for confirmation.
         // 3. substitute the string.
         // 4. if do_all is set, find next match
         // 5. break if there isn't another match in this line
         for (;;) {
            // Advance "lnum" to the line where the match starts.  The
            // match does not start in the first line when there is a line break before \zs.
            if (regmatch.startpos[0].lnum > 0) {
               lnum += regmatch.startpos[0].lnum;
               sub_firstlnum += regmatch.startpos[0].lnum;
               nmatch -= regmatch.startpos[0].lnum;
               EE_CLEAR(sub_firstline);
            }

            //Match might be after the last line for "\n\zs" matching at the end of the last line.
            if (lnum > curBook->mem.lineCount)
               break;

            if (sub_firstline == NULL) {
               sub_firstline = copySubstr(ml_get(sub_firstlnum), ml_get_len(sub_firstlnum));
               if (sub_firstline == NULL) {
                  eeglFree(new_start);
                  goto outofmem;
               }
            }

            // Save the line number of the last change for the final cursor position
            curPor->cursor.lnum = lnum;
            do_again = false;

            // 1. Match empty string does not count, except for first
            // match. This reproduces the strange vi behaviour. This also catches endless loops.
            if (matchcol == prev_matchcol
               && regmatch.endpos[0].lnum == 0
               && matchcol == regmatch.endpos[0].col
            ){
               if (sub_firstline[matchcol] == ZERO)
                  // We already were at the end of the line.  Don't look
                  // for a match in this line again.
                  skip_match = true;
               else {
                // search for a match at next column
                   matchcol += utfCharLen(sub_firstline + matchcol);
               }
               goto skip;
            }

            // Normally we continue searching for a match just after the previous match.
            matchcol = regmatch.endpos[0].col;
            prev_matchcol = matchcol;

            // 2. If do_count is set only increase the counter.
            //    If do_ask is set, ask for confirmation.
            if (subflags.do_count) {
               // For a multi-line match, put matchcol at the ZERO at the end of the line and 
               // set nmatch to one, so that we continue looking for a match on the next line.
               // Avoids that ":s/\nB\@=//gc" get stuck.
               if (nmatch > 1) {
                  matchcol = (ColNr)STRLEN(sub_firstline);
                  nmatch = 1;
                  skip_match = true;
               }
               sub_nsubs++;
               did_sub = true;
               // Skip the substitution, unless an expression is used
               if (!(sub[0] == '\\' && sub[1] == '='))
                  goto skip;
            }

            if (subflags.do_ask) {
               Unt typed = 0;

               // change stateG to MODE_CONFIRM, so that the mouse works properly
               save_State = stateG;
               stateG = MODE_CONFIRM;
               setmouse();      // disable mouse in xterm
               curPor->cursor.col = regmatch.startpos[0].col;
               if (curPor->o.cursorBind)
                  do_check_cursorbind();


               // Loop until 'y', 'n', 'q', CTRL-E or CTRL-Y typed.
               while (subflags.do_ask) {
                  CS orig_line = NULL;
                  int len_change = 0;
                  int save_p_lz = p_lz;
                  int save_p_fen = curPor->o.foldEnable;

                  curPor->o.foldEnable = false;
                  // Invert the matched string. Remove the inversion afterwards.
                  int save_isRedrawingDisabledG = isRedrawingDisabledG;
                  isRedrawingDisabledG = 0;

                  // avoid calling drawUpdateScreen() in vgetorpeek()
                  p_lz = false;

                  if (new_start) {
                     // There already was a substitution, we would like to show this to the user. 
                     // We cannot really update the line, it would change what matches.  
                     // Temporarily replace the line and change it back afterwards.
                     orig_line = copySubstr(ml_get(lnum), ml_get_len(lnum));
                     if (orig_line) {
                        CS new_line = concat_str(new_start, sub_firstline + copycol);

                        if (new_line == NULL)
                           EE_CLEAR(orig_line);
                        else {
                        // Position the cursor relative to the
                        // end of the line, the previous
                        // substitute may have inserted or
                        // deleted characters before the
                        // cursor.
                        len_change = (int)STRLEN(new_line)
                                - (int)STRLEN(orig_line);
                        curPor->cursor.col += len_change;
                        ml_replace(lnum, new_line, false);
                         }
                     }
                   }

                  search_match_lines = regmatch.endpos[0].lnum - regmatch.startpos[0].lnum;
                  search_match_endcol = regmatch.endpos[0].col + len_change;
                  if (search_match_lines == 0 && search_match_endcol == 0)
                     // highlight at least one character for /^/
                     search_match_endcol = 1;
                  highlight_match = true;

                  update_topline();
                  validate_cursor();
                  drawUpdateScreen(UPD_SOME_VALID);
                  highlight_match = false;
                  redraw_later(UPD_SOME_VALID);

                  curPor->o.foldEnable = save_p_fen;
                  if (msgRowG == visibleRowsG - 1)
                     msg_didout = false;   // avoid a scroll-up
                  msg_starthere();
                  i = msg_scroll;
                  msg_scroll = 0;      // truncate msg when needed
                  msg_no_more = true;
                  // write message same highlighting as for wait_return()
                  smsgDeco(getDecoFlags(HLF_R), _("replace with %s (y/n/a/q/l/^E/^Y)?"), sub);
                  msg_no_more = false;
                  msg_scroll = i;
                  showruler(true);
                  windgoto(msgRowG, msgColG);
                  isRedrawingDisabledG = save_isRedrawingDisabledG;

                  ++no_mapping;   // don't map this key
                  ++allow_keys;   // allow special keys
                  typed = plain_vgetc();
                  --allow_keys;
                  --no_mapping;

                  // clear the question
                  msg_didout = false;   // don't scroll up
                  msgColG = 0;
                  gotoCommline(true);
                  p_lz = save_p_lz;

                  // restore the line
                  if (orig_line)
                     ml_replace(lnum, orig_line, false);

                  need_wait_return = false; // no hit-return prompt
                  if (typed == 'q' || typed == ESC || typed == Ctrl_C 
                        || typed == extraInterruptCharG
                  ){
                     got_quit = true;
                     break;
                  }
                  if (typed == 'n')
                     break;
                  if (typed == 'y')
                     break;
                  if (typed == 'l') {
                     // last: replace and then stop
                     subflags.do_all = false;
                     line2 = lnum;
                     break;
                  }
                  if (typed == 'a') {
                      subflags.do_ask = false;
                      break;
                  }
                  if (typed == Ctrl_E)
                      scrollup_clamp();
                  ei (typed == Ctrl_Y)
                      scrolldown_clamp();
               }
               stateG = save_State;
               setmouse();

               if (typed == 'n') {
                  // For a multi-line match, put matchcol at the ZERO at the end of the line 
                  // and set nmatch to one, so that we continue looking for a match on the next 
                  // line. Avoids that ":%s/\nB\@=//gc" and ":%s/\n/,\r/gc" get stuck when 
                  // pressing 'n'.
                  if (nmatch > 1) {
                     matchcol = (ColNr)STRLEN(sub_firstline);
                     skip_match = true;
                  }
                  goto skip;
               }
               if (got_quit)
                  goto skip;
            }

            // Move the cursor to the start of the match, so that we can use "\=col(".").
            curPor->cursor.col = regmatch.startpos[0].col;

            // 3. substitute the string.
            save_ma = curBook->o.modifiable;
            if (subflags.do_count) {
               // prevent accidentally changing the buffer by a function
               curBook->o.modifiable = false;
            }
            // Save flags for recursion.  They can change for e.g.
            // :s/^/\=execute("s#^##gn")
            subflags_save = subflags;

            // Disallow changing text or switching portal in an expression.
            ++textlock;
            //Get length of substitution part, including the ZERO. When it fails, sublen is 0
            sublen = eeRegsub_multi(&regmatch,
                      sub_firstlnum - regmatch.startpos[0].lnum,
                      sub, sub_firstline, 0,
                      REGSUB_BACKSLASH
                      | (REGSUB_MAGIC));
            --textlock;

            // If getting the substitute string caused an error, don't do the replacement.
            // Don't keep flags set by a recursive call.
            subflags = subflags_save;
            if (sublen == 0 || aborting() || subflags.do_count) {
                curBook->o.modifiable = save_ma;
                goto skip;
            }

            // When the match included the "$" of the last line it may
            // go beyond the last line of the buffer.
            if (nmatch > curBook->mem.lineCount - sub_firstlnum + 1) {
                nmatch = curBook->mem.lineCount - sub_firstlnum + 1;
                skip_match = true;
                // safety check
                if (nmatch < 0)
               goto skip;
            }

            // Need room for:
            // - result so far in new_start (not for first sub in line)
            // - original text up to match
            // - length of substituted part
            // - original text after match
            // Adjust text properties here, since we have all information needed.
            if (nmatch == 1) {
               p1 = sub_firstline;
               if (curBook->hasTextprop) {
                     int bytes_added = 
                        sublen - 1 - (regmatch.endpos[0].col - regmatch.startpos[0].col);

                  // When text properties are changed, need to save for
                  // undo first, unless done already.
                  if (adjustPropColumns(
                           lnum, total_added + regmatch.startpos[0].col, bytes_added, apc_flags
                     )
                  ) {
                      apc_flags &= ~APC_SAVE_FOR_UNDO;
                  } 
                  // Offset for column byte number of the text property
                  // in the resulting buffer afterwards.
                  total_added += bytes_added;
               }
            } else {
               LineNr   lastlnum = sub_firstlnum + nmatch - 1;
               if (curBook->hasTextprop) {

                  // Props in the first line may be shortened or deleted
                  if (adjustPropColumns(
                           lnum, total_added + regmatch.startpos[0].col, -MAXCOL, apc_flags
                           )
                  ) {
                      apc_flags &= ~APC_SAVE_FOR_UNDO;
                  } 
                  total_added -= (ColNr)STRLEN( sub_firstline + regmatch.startpos[0].col);

                  // Props in the last line may be moved or deleted
                  if (adjustPropColumns(lastlnum, 0, -regmatch.endpos[0].col, apc_flags))
                      // When text properties are changed, need to save
                      // for undo first, unless done already.
                      apc_flags &= ~APC_SAVE_FOR_UNDO;

                  // Copy the text props of the last line, they will be
                  // later appended to the changed line.
                  CS propStart;
                  text_prop_count = get_text_props(OUT &propStart, curBook, lastlnum, false);
                  if (text_prop_count > 0) {
                     // TODO: what when we already did this?
                     eeglFree(text_props);
                     text_props = ALLOC_MULT(TextProp, text_prop_count);

                     mch_memmove(text_props, propStart, text_prop_count * sizeof(TextProp));
                     // After joining the text prop columns will increase.
                     for (int pi = 0; pi < text_prop_count; ++pi)
                        text_props[pi].col += regmatch.startpos[0].col + sublen - 1;
                  }
               }
               p1 = ml_get(lastlnum);
               nmatch_tl += nmatch - 1;
               if (curBook->hasTextprop)
                  total_added += (ColNr)STRLEN(p1 + regmatch.endpos[0].col);
            }
            copy_len = regmatch.startpos[0].col - copycol;
            needed_len = copy_len + ((unsigned)STRLEN(p1) - regmatch.endpos[0].col) + sublen + 1;
            if (new_start == NULL) {
               //Get some space for a temporary buffer to do the substitution into (and some 
               //extra space to avoid too many calls to alloc()/free()).
               new_start_len = needed_len + 50;
               new_start = allocZeroed(new_start_len);
               *new_start = ZERO;
               new_end = new_start;
            } else {
               //Check if the temporary buffer is long enough to do the
               //substitution into.  If not, make it larger (with a bit
               //extra to avoid too many calls to alloc()/free()).
               len = (unsigned)STRLEN(new_start);
               needed_len += len;
               if (needed_len > (int)new_start_len) {
                  new_start_len = needed_len + 50;
                  p1 = allocZeroed(new_start_len);
                  mch_memmove(p1, new_start, (Unt)(len + 1));
                  eeglFree(new_start);
                  new_start = p1;
               }
               new_end = new_start + len;
            }

            //copy the text up to the part that matched
            mch_memmove(new_end, sub_firstline + copycol, (Unt)copy_len);
            new_end += copy_len;

            if ((int)new_start_len - copy_len < sublen)
               sublen = new_start_len - copy_len - 1;

            ++textlock;
            (void)eeRegsub_multi(&regmatch,
                      sub_firstlnum - regmatch.startpos[0].lnum,
                        sub, new_end, sublen,
                        REGSUB_COPY | REGSUB_BACKSLASH | (REGSUB_MAGIC));
            --textlock;
            sub_nsubs++;
            did_sub = true;

            // Move the cursor to the start of the line, to avoid that it
            // is beyond the end of the line after the substitution.
            curPor->cursor.col = 0;

            //For a multi-line match, make a copy of the last matched line and continue in that one.
            if (nmatch > 1) {
               sub_firstlnum += nmatch - 1;
               eeglFree(sub_firstline);
               sub_firstline = copySubstr(ml_get(sub_firstlnum), ml_get_len(sub_firstlnum));
               // When going beyond the last line, stop substituting.
               if (sub_firstlnum <= line2)
                  do_again = true;
               else
                  subflags.do_all = false;
            }

            // Remember next character to be copied.
            copycol = regmatch.endpos[0].col;

            if (skip_match) {
               // Already hit end of the buffer, sub_firstlnum is one less than what it ought to be.
               eeglFree(sub_firstline);
               sub_firstline = copyStr((CS)"");
               copycol = 0;
            }

            /*
             * Now the trick is to replace CTRL-M chars with a real line break. This would make
             * it impossible to insert a CTRL-M in the text.  The line break can be avoided by 
             * preceding the CTRL-M with a backslash.  To be able to insert a backslash,
             * they must be doubled in the string and are halved here.
             */
            for (p1 = new_end; *p1; ++p1) {
               if (p1[0] == '\\' && p1[1] != ZERO) { // remove backslash
                  STRMOVE(p1, p1 + 1);
                  if (curBook->hasTextprop) {
                     // When text properties are changed, need to save
                     // for undo first, unless done already.
                     if (adjustPropColumns(lnum, (ColNr)(p1 - new_start), -1, apc_flags))
                        apc_flags &= ~APC_SAVE_FOR_UNDO;
                  }
               } ei (*p1 == ENTER) {
                  if (u_inssub(lnum) == OK) {  // prepare for undo
                     ColNr   plen = (ColNr)(p1 - new_start + 1);

                     *p1 = ZERO;          // truncate up to the CR
                     ml_append(lnum - 1, new_start, plen, false);
                     markAdjust(lnum + 1, (LineNr)MAXLNUM, 1L, 0L, true);
                     if (subflags.do_ask)
                        appended_lines(lnum - 1, 1L);
                     else {
                        if (first_line == 0)
                           first_line = lnum;
                        last_line = lnum + 1;
                     }
                     adjustPropsForSplit(lnum + 1, lnum, plen, 1, false);
                     // all line numbers increase
                     ++sub_firstlnum;
                     ++lnum;
                     ++line2;
                     // move the cursor to the new line, like Vi
                     ++curPor->cursor.lnum;
                     // copy the rest
                     STRMOVE(new_start, p1 + 1);
                     p1 = new_start - 1;
                  }
               } else
                  p1 += utfCharLen(p1) - 1;
            }

            //4. If do_all is set, find next match.
            //Prevent endless loop with patterns that match empty
            //strings, e.g. :s/$/pat/g or :s/[a-z]* /(&)/g.
            //But ":s/\n/#/" is OK.
      skip:
            // We already know that we did the last subst when we are at
            // the end of the line, except that a pattern like
            // "bar\|\nfoo" may match at the ZERO.  "lnum" can be below
            // "line2" when there is a \zs in the pattern after a line break.
            lastone = (skip_match
               || gotInterruptG
               || got_quit
               || lnum > line2
               || !(subflags.do_all || do_again)
               || (sub_firstline[matchcol] == ZERO && nmatch <= 1
                      && !re_multiline(regmatch.regprog)));
            nmatch = -1;

            //Replace the line in the buffer when needed.  This is skipped when there are more 
            //matches. The check for nmatch_tl is needed for when multi-line matching must replace
            //the lines before trying to do another match, otherwise "\@<=" won't work.
            //When the match starts below where we start searching, also need to replace the line 
            //first (using \zs after \n).
            if (lastone
               || nmatch_tl > 0
               || (nmatch = eeRegexec_multi(&regmatch, curPor,
                           curBook, sub_firstlnum,
                            matchcol, NULL)) == 0
               || regmatch.startpos[0].lnum > 0
            ){
                if (new_start) {
                  //Copy the rest of the line, that didn't match. "matchcol" has to be adjusted, 
                  //we use the end of the line as reference, because the substitute may
                  //have changed the number of characters. Same for "prev_matchcol".
                  STRCAT(new_start, sub_firstline + copycol);
                  matchcol = (ColNr)STRLEN(sub_firstline) - matchcol;
                  prev_matchcol = (ColNr)STRLEN(sub_firstline) - prev_matchcol;

                  if (u_savesub(lnum) != OK)
                     break;
                  ml_replace(lnum, new_start, true);
                  if (text_props)
                     add_text_props(lnum, text_props, text_prop_count);
                  if (nmatch_tl > 0) {
                     //Matched lines have now been substituted and are useless, delete them. 
                     //The part after the match has been appended to new_start, we don't need
                     //it in the buffer.
                     ++lnum;
                     if (u_savedel(lnum, nmatch_tl) != OK)
                        break;
                     for (i = 0; i < nmatch_tl; ++i)
                        ml_delete(lnum);
                     markAdjust(lnum, lnum + nmatch_tl - 1, (long)MAXLNUM, -nmatch_tl, true);
                     if (subflags.do_ask)
                        deleted_lines(lnum, nmatch_tl);
                     --lnum;
                     line2 -= nmatch_tl; // nr of lines decreases
                     nmatch_tl = 0;
                  }

                  // When asking, undo is saved each time, must also set
                  // changed flag each time.
                  if (subflags.do_ask)
                     changed_bytes(lnum, 0);
                  else {
                     if (first_line == 0)
                        first_line = lnum;
                     last_line = lnum + 1;
                  }

                  sub_firstlnum = lnum;
                  eeglFree(sub_firstline);    // free the temp buffer
                  sub_firstline = new_start;
                  new_start = NULL;
                  matchcol = (ColNr)STRLEN(sub_firstline) - matchcol;
                  prev_matchcol = (ColNr)STRLEN(sub_firstline) - prev_matchcol;
                  copycol = 0;
               }
               if (nmatch == -1 && !lastone)
                  nmatch = eeRegexec_multi(&regmatch, curPor, curBook, sub_firstlnum, matchcol, NULL);

               //5. break if there is no other match on this line
               if (nmatch <= 0) {
                  // If the match found didn't start where we were searching, do the next search in the 
                  // line where we found the match.
                  if (nmatch == -1)
                     lnum -= regmatch.startpos[0].lnum;
                  break;
               }
            }

            line_breakcheck();
          }

          if (did_sub)
         ++sub_nlines;
          eeglFree(new_start);   // for when substitute was cancelled
          EE_CLEAR(sub_firstline);   // free the copy of the original line
      }

      line_breakcheck();
   }

   if (first_line != 0) {
      // Need to subtract the number of added lines from "last_line" to get
      // the line number before the change (same as adding the number of deleted lines).
      i = curBook->mem.lineCount - old_line_count;
      changed_lines(first_line, 0, last_line - i, i);
   }

outofmem:
   eeglFree(sub_firstline); // may have to free allocated copy of the line

   eeglFree(text_props);

   // ":s/pat//n" doesn't move the cursor
   if (subflags.do_count)
      curPor->cursor = old_cursor;

   if (sub_nsubs > start_nsubs) {
      if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
         // Set the '[ and '] marks.
         curBook->opStart.lnum = invo->line1;
         curBook->opEnd.lnum = line2;
         curBook->opStart.col = curBook->opEnd.col = 0;
      }

      if (!global_busy) {
         // when interactive leave cursor on the match
         if (!subflags.do_ask) {
            if (endcolumn)
                coladvance((ColNr)MAXCOL);
            else
                beginline(BL_WHITE | BL_FIX);
         }
         if (!do_sub_msg(subflags.do_count) && subflags.do_ask)
            msg(E);
      } else
         globalNeedBeginlineS = true;
      if (subflags.do_print)
         print_line(curPor->cursor.lnum, subflags.do_list);
   } ei (!global_busy) {
      if (gotInterruptG)      // interrupted
          emsg(_(e_interrupted));
      ei (got_match)   // did find something but nothing substituted
          msg(E);
      ei (subflags.do_error)   // nothing found
          showErrFmtMsg(_(e_pattern_not_found_str), get_search_pat());
   }

   if (subflags.do_ask && hasAnyFolding(curPor))
      // Cursor position may require updating
      didChangePortalSettingCurPor();

   eeRegFree(regmatch.regprog);
   eeglFree(sub);

   // Restore the flag values, they can be used for ":&&".
   subflags.do_all = save_do_all;
   subflags.do_ask = save_do_ask;
}

// Give message for number of substitutions. Can also be used after a ":global" command.
// Return true if a message was given.
int
do_sub_msg(int       count_only) {    // used 'n' flag for ":s"
   // Only report substitutions when:
   // - command was typed by user, or number of changed lines > 0
   // - giving messages is not disabled by 'lazyredraw'
   if (messaging()) {

      if (gotInterruptG)
         STRCPY(msg_buf, _("(Interrupted) "));
      else
         *msg_buf = ZERO;

      CS msg_single = count_only
          ? NGETTEXT("%ld match on %ld line", "%ld matches on %ld line", sub_nsubs)
          : NGETTEXT("%ld substitution on %ld line", "%ld substitutions on %ld line", sub_nsubs);
      CS msg_plural = count_only
          ? NGETTEXT("%ld match on %ld lines", "%ld matches on %ld lines", sub_nsubs)
          : NGETTEXT("%ld substitution on %ld lines", "%ld substitutions on %ld lines", sub_nsubs);

      eeSnprintfAdd(
            msg_buf, sizeof(msg_buf), NGETTEXT(msg_single, msg_plural, sub_nlines),
            sub_nsubs, (long)sub_nlines
      );

      if (msg(msg_buf))
         // save message to display it after redraw
         set_keep_msg(msg_buf, 0);
      return true;
   }
   if (gotInterruptG) {
      emsg(_(e_interrupted));
      return true;
   }
   return false;
}

// Get the previous substitute pattern.
CS
get_old_sub(void) {
   return prevSubstS;
}

// Set the previous substitute pattern.  "val" must be allocated.
void
set_old_sub(CS val) {
   eeglFree(prevSubstS);
   prevSubstS = val;
}

#if defined(EXITFREE) || defined(PROTO)
void
free_old_sub(void) {
   eeglFree(prevSubstS);
}
#endif

//}}}
//{{{global

private void
global_exe_one(CS cmd, LineNr lnum) {
   curPor->cursor.lnum = lnum;
   curPor->cursor.col = 0;
   if (*cmd == ZERO || *cmd == '\n')
      doCommand(S"p", NULL, NULL, DOCMD_NOWAIT);
   else
      doCommand(cmd, NULL, NULL, DOCMD_NOWAIT);
}

// Execute a global command of the form:
//
// g/pattern/X : execute X on all lines where pattern matches
// v/pattern/X : execute X on all lines where pattern does not match
//
// where 'X' is a Command
//
// The command character (as well as the trailing slash) is optional, and is assumed to be 'p' if 
// missing.
//
// This is implemented in two passes: first we scan the file for the pattern and set a mark for 
// each line that (not) matches. Secondly we execute the command for each line that has a mark. 
// This is required because after deleting lines we do not know where to search for the next match.
void
c_global(Invocation* invo) {
   LineNr lnum;      // line number according to old situation
   int ndone = 0;
   int type;      // first char of cmd: 'v' or 'g'
   CS cmd;      // command argument

   Byte delim;      // delimiter, normally '/'
   Text pat;
   CS used_pat;
   RegMultilineMatch   regmatch;
   int match;
   int which_pat;

   // When nesting the command works on one line.  This allows for
   // ":g/found/v/notfound/command".
   if (global_busy && (invo->line1 != 1 || invo->line2 != curBook->mem.lineCount)) {
      // will increment global_busy to break out of the loop
      emsg(_(e_cannot_do_global_recursive_with_range));
      return;
   }

   if (invo->forceit)          // ":global!" is like ":vglobal"
     type = 'v';
   else
     type = *invo->comm;
   cmd = invo->arg;
   which_pat = RE_LAST;       // default: use last used regexp


   //undocumented feature: 
   //  "\/" and "\?": use previous search pattern.
   //      "\&": use previous substitute pattern.
   if (*cmd == '\\') {
      ++cmd;
      if (firstOccurrence((CS)"/?&", *cmd) == NULL) {
         emsg(_(e_backslash_should_be_followed_by));
         return;
      }
      if (*cmd == '&')
          which_pat = RE_SUBST;   // use previous substitute pattern
      else
          which_pat = RE_SEARCH;   // use previous search pattern
      ++cmd;
      pat = (Text){null, 0};
   } ei (*cmd == ZERO) {
      emsg(_(e_regular_expression_missing_from_global));
      return;
   } ei (check_regexp_delim(*cmd) == FAIL) {
      return;
   } else {
      delim = *cmd;      // get the delimiter
      ++cmd;         // skip delimiter if there is one
      pat = text(cmd);      // remember start of pattern
      cmd = skip_regexp_ex(cmd, delim, true, &invo->arg, NULL, NULL);
      if (cmd[0] == delim)          // end delimiter found
          *cmd++ = ZERO;          // replace it with a ZERO
   }

   if (search_regcomp(pat, &used_pat, RE_BOTH, which_pat, SEARCH_HIS, OUT &regmatch) 
         == FAIL
   ) {
      emsg(_(e_invalid_command));
      return;
   }

   if (global_busy) {
      lnum = curPor->cursor.lnum;
      match = eeRegexec_multi(&regmatch, curPor, curBook, lnum,
                            (ColNr)0, NULL);
      if ((type == 'g' && match) || (type == 'v' && !match))
          global_exe_one(cmd, lnum);
   } else {
      // pass 1: set marks for each (not) matching line
      for (lnum = invo->line1; lnum <= invo->line2 && !gotInterruptG; ++lnum) {
         // a match on this line?
         match = eeRegexec_multi(&regmatch, curPor, curBook, lnum, (ColNr)0, NULL);
         if (regmatch.regprog == NULL)
            break;  // re-compiling regprog failed
         if ((type == 'g' && match) || (type == 'v' && !match)) {
            ml_setmarked(lnum);
            ndone++;
         }
         line_breakcheck();
      }

      // pass 2: execute the command for each line that has been marked
      if (gotInterruptG)
         msg(_(e_interrupted));
      ei (ndone == 0) {
         if (type == 'v') {
            smsg(_("Pattern found in every line: %s"), used_pat);
         } else {
            showErrFmtMsg(_(e_pattern_not_found_str), used_pat);
         }
      } else {
          start_global_changes();
          global_exe(cmd);
          end_global_changes();
      }

      ml_clearmarked();      // clear rest of the marks
   }

   eeRegFree(regmatch.regprog);
}

// Execute "cmd" on lines marked with ml_setmarked().
void
global_exe(CS cmd) {
   LineNr old_lcount;   // mem.lineCount before the command
   Book    *old_buf = curBook;   // remember what buffer we started in
   LineNr lnum;      // line number according to old situation

   // Set current position only once for a global command.
   // If global_busy is set, setpcmark() will not do anything.
   // If there is an error, global_busy will be incremented.
   setpcmark();

   // When the command writes a message, don't overwrite the command.
   msg_didout = true;

   sub_nsubs = 0;
   sub_nlines = 0;
   globalNeedBeginlineS = false;
   global_busy = 1;
   old_lcount = curBook->mem.lineCount;
   while (!gotInterruptG && (lnum = ml_firstmarked()) != 0 && global_busy == 1) {
      global_exe_one(cmd, lnum);
      ui_breakcheck();
   }

   global_busy = 0;
   if (globalNeedBeginlineS)
      beginline(BL_WHITE | BL_FIX);
   else
      check_cursor();   // cursor may be beyond the end of the line

   // the cursor may not have moved in the text but a change in a previous
   // line may move it on the screen
   changed_line_abv_curs();

   // If it looks like no message was written, allow overwriting the
   // command with the report for number of changes.
   if (msgColG == 0 && msg_scrolled == 0)
      msg_didout = false;

   // If substitutes done, report number of substitutes, otherwise report
   // number of extra or deleted lines.
   // Don't report extra or deleted lines in the edge case where the buffer
   // we are in after execution is different from the buffer we started in.
   if (!do_sub_msg(false) && curBook == old_buf)
      msgmore(curBook->mem.lineCount - old_lcount);
}

//}}}
//{{{misc

// Set up for a tagpreview. Make the preview portal the current portal.
// Return true when it was created.
int
prepare_tagpreview(
   int      undo_sync,       // sync undo when leaving the portal
   int      use_previewpopup,   // use popup if 'previewpopup' set
   UsePopup   use_popup)       // use other popup portal
{
   if (curPor->isPreview)
      return false;

   // If there is already a preview portal open, use that one.
   Portal* po;
   if (use_previewpopup) {
      po = popupFindPreviewPortal();
      if (po)
         popup_set_wantpos_cursor(po, po->pup.minWidth, NULL);
   } ei (use_popup != USEPOPUP_NONE) {
      po = popupFindInfoPortal();
      if (po) {
         if (use_popup == USEPOPUP_NORMAL)
            popup_show(po);
         else
            popup_hide(po);
         // When the popup moves or resizes it may reveal part of
         // another portal.  TODO: can this be done more efficiently?
         redraw_all_later(UPD_NOT_VALID);
      }
   } else {
      FOR_ALL_PORTALS(po) {
         if (po->isPreview)
            break;
      } 
   }
   if (po) {
      enterPortal(po, undo_sync);
      return false;
   }

   // There is no preview portal open yet.  Create one.
   if ((use_previewpopup) || use_popup != USEPOPUP_NONE)
      return portalCreatePreviewPortal(use_popup != USEPOPUP_NONE);
   if (splitPortal(g_do_tagpreview > 0 ? g_do_tagpreview : 0, 0) == FAIL)
      return false;
   curPor->isPreview = true;
   curPor->o.portFixHeight = true;
   RESET_BINDING(curPor);       // don't take over 'scrollbind'
               // and 'cursorbind'
   curPor->o.diff = false;       // no 'diff'
   return true;
}


// Make the user happy.
void
c_smile(Invocation* invo UNUSED) {
   static char *code[] = {
   "\34 \4o\14$\4ox\30 \2o\30$\1ox\25 \2o\36$\1o\11 \1o\1$\3 \2$\1 \1o\1$x\5 \1o\1 \1$\1 \2o\10 "
   "\1o\44$\1o\7 \2$\1 \2$\1 \2$\1o\1$x\2 \2o\1 \1$\1 \1$\1 \1\"\1$\6 \1o\11$\4 \15$\4 \11$\1o\7 "
   "\3$\1o\2$\1o\1$x\2 \1\"\6$\1o\1$\5 \1o\11$\6 \13$\6 \12$\1o\4 \10$x\4 \7$\4 \13$\6 \13$\6 "
   "\27$x\4 \27$\4 \15$\4 \16$\2 \3\"\3$x\5 \1\"\3$\4\"\61$\5 \1\"\3$x\6 \3$\3 \1o\62$\5 "
   "\1\"\3$\1ox\5 \1o\2$\1\"\3 \63$\7 \3$\1ox\5 \3$\4 \55$\1\"\1 \1\"\6$",
   "\5o\4$\1ox\4 \1o\3$\4o\5$\2 \45$\3 \1o\21$x\4 \10$\1\"\4$\3 \42$\5 \4$\10\"x\3 \4\"\7 \4$\4 "
   "\1\"\34$\1\"\6 \1o\3$x\16 \1\"\3$\1o\5 \3\"\22$\1\"\2$\1\"\11 \3$x\20 \3$\1o\12 "
   "\1\"\2$\2\"\6$\4\"\13 \1o\3$x\21 \4$\1o\40 \1o\3$\1\"x\22 \1\"\4$\1o\6 \1o\6$\1o\1\"\4$\1o\10 "
   "\1o\4$x\24 \1\"\5$\2o\5 \2\"\4$\1o\5$\1o\3 \1o\4$\2\"x\27 \2\"\5$\4o\2 \1\"\3$\1o\11$\3\"x\32 "
   "\2\"\7$\2o\1 \12$x\42 \4\"\13$x\46 \14$x\47 \12$\1\"x\50 \1\"\3$\4\"x"
   };
   char *p;
   int n;
   int i;

   msg_start();
   msg_putchar('\n');
   for (i = 0; i < 2; ++i) {
      for (p = code[i]; *p != ZERO; ++p) {
         if (*p == 'x')
            msg_putchar('\n');
         else {
            for (n = *p++; n > 0; --n) {
               msg_putchar(*p);
            } 
         } 
      } 
   } 
   msg_clr_eos();
}

// ":drop" Open the first argument in a portal, and the argument list is redefined.
void
c_drop(Invocation* invo) {
   int      split = false;
   Portal   *po;

   if (portErrorIfPopup(false) || portErrorIfTermPopup())
      return;

   // Check if the first argument is already being edited in a portal. If so, jump to that portal.
   // We would actually need to check all arguments, but that's complicated
   // and mostly only one file is dropped.
   // This also ignores wildcards, since it is very unlikely the user is
   // editing a file name with a wildcard character.
   set_arglist(invo->arg);

   // Expanding wildcards may result in an empty argument list.  E.g. when
   // editing "foo.pyc" and ".pyc" is in 'wildignore'.  Assume that we
   // already did an error message for this.
   if (ARGCOUNT == 0)
      return;

   if (commModifierG.cmod_tab) {
      // ":tab drop file ...": open a tab for each argument that isn't
      // edited in a portal yet.  It's like ":tab all" but without closing portals or tabs.
      c_all(invo);
      commModifierG.cmod_tab = 0;
      c_rewind(invo);
      return;
   }

   // ":drop file ...": Edit the first argument.  Jump to an existing portal if possible, edit in 
   // current portal if the current book can be abandoned, otherwise open a new portal.
   Book* book = bookFindFileByBookNr(ARGLIST[0].fnum);

   Tab   *t;
   FOR_ALL_TAB_PORTALS(t, po) {
      if (po->book == book) {
         goto_tab_port(t, po);
         curPor->argListInd = 0;
         if (!doWasBookChanged(curBook)) {
            fiCheckBookTimestamp(curBook);
         }
         if (curBook->mem.flags & ML_EMPTY)
            c_rewind(invo);
         return;
      }
   }

   // Fake a ":sfirst" or ":first" command edit the first argument.
   if (split) {
      invo->id = C_sfirst;
      invo->comm[0] = 's';
   } else
      invo->id = C_first;
   c_rewind(invo);
}

// As skipEeglGrepPat() and store the character overwritten by ZERO in "cp"
// and the pointer to it in "nulp".
private CS
skipEeglGrepPat_ext(CS p, Byte **s, Unt* flags, Byte** nulp, int *cp) {
   int      c;

   if (eeIsIdentifierChar(*p)) {
      // ":vimgrep pattern fname"
      if (s)
         *s = p;
      p = skiptowhite(p);
      if (s && *p != ZERO) {
         if (nulp != NULL) {
            *nulp = p;
            *cp = *p;
         }
         *p++ = ZERO;
      }
   } else {
      // ":vimgrep /pattern/[g][j] fname"
      if (s)
         *s = p + 1;
      c = *p;
      p = skip_regexp(p + 1, c, true);
      if (*p != c)
         return NULL;

      // Truncate the pattern.
      if (s) {
         if (nulp) {
            *nulp = p;
            *cp = *p;
         }
         *p = ZERO;
      }
      ++p;

      // Find the flags
      while (*p == 'g' || *p == 'j' || *p == 'f') {
         if (flags) {
            if (*p == 'g')
                *flags |= VGR_GLOBAL;
            ei (*p == 'j')
                *flags |= VGR_NOJUMP;
            else
                *flags |= VGR_FUZZY;
          }
          ++p;
      }
   }
   return p;
}

// Skip over the pattern argument of ":vimgrep /pat/[g][j]".
// Put the start of the pattern in "*s", unless "s" is NULL.
// If "flags" is not NULL put the flags in it: VGR_GLOBAL, VGR_NOJUMP.
// If "s" is not NULL terminate the pattern with a ZERO.
// Return a pointer to the char just past the pattern plus flags.
CS
skipEeglGrepPat(CS p, Byte **s, Unt *flags) {
   return skipEeglGrepPat_ext(p, s, flags, NULL, NULL);
}

// List v:oldfiles in a nice way.
void
c_oldfiles(Invocation* invo) {
   List* l = get_EeglVar_list(VV_OLDFILES);
   if (!l) {
      msg(_("No old files"));
      return;
   }
   
   int nr = 0;
   // for a single filtered match, remember the number
   // so we can jump directly to it without prompting
   int matches = -1;

   msg_start();
   msg_scroll = true;
   for (ListItem* li = l->first; li && !gotInterruptG; li = li->next) {
      ++nr;
      CS fname = tv_get_string(&li->c);
      if (!message_filtered(fname)) {
         if (matches < 0)
            matches = nr;
         else
            matches = 0;
         msg_outnum((long)nr);
         msg_puts(S": ");
         msg_outtrans(fname);
         msg_clr_eos();
         msg_putchar('\n');
         out_flush();       // output one line at a time
         ui_breakcheck();
      }
   }

   // Assume "gotInterruptG" was set to truncate the listing.
   gotInterruptG = false;

   if (commModifierG.cmod_flags & CMOD_BROWSE) {
      quitMoreG = false;
      // we only need to prompt if there is more than 1 match
      if (matches > 0) {
         nr = matches;
         // msg_putchar above sets needs_wait_return
         need_wait_return = false;
      } else
         nr = prompt_for_number(false);
      msg_starthere();
      if (nr > 0) {
         CS p = list_find_str(get_EeglVar_list(VV_OLDFILES), (long)nr);
         if (p) {
            p = doExpandEnvInMultiplePaths(p);
            invo->arg = p;
            invo->id = C_edit;
            commModifierG.cmod_flags &= ~CMOD_BROWSE;
            do_exedit(invo, NULL);
            eeglFree(p);
         }
      }
   }
}

//":argdo", ":windo", ":bufdo", ":tabdo", ":ldo"
void
c_listDo(Invocation* invo) {
   int i;
   Portal* po;
   Tab* t;
   Book* book = curBook;
   int next_fnum = 0;

   if (curPor->o.portFixBuf) {
      if (portalIsValid(prevPor) && !prevPor->o.portFixBuf)
          //@portfixbuf is set; attempt to change to a portal without it.
          gotoPortal(prevPor);
      if (curPor->o.portFixBuf) {
          //Split the portal, which will have its @portfixbuf off, and set curPor to that
          (void)splitPortal(0, 0);

         if (curPor->o.portFixBuf) {
            //Autocommands set @portfixbuf or sent us to another portal
            //with it set, or we failed to split the portal.  Give up.
            emsg(_(e_portfixbuf_cannot_go_to_buffer));
            return;
         }
      }
   }

   CS save_ei = NULL;

   if (invo->id != C_windo && invo->id != C_tabdo) {
      // Don't do syntax HL autocommands. Skipping the syntax file is a great speed improvement.
      save_ei = au_event_disable(S",Syntax");

      FOR_ALL_BOOKS(book) {
         book->flags &= ~BF_SYN_SET;
      } 
      book = curBook;
   }
   start_global_changes();

   i = 0;
   // start at the invo->line1 argument/portal/book
   po = firstPor;
   t = firstTabG;
   switch (invo->id) {
   case C_windo:
      for ( ; po && i + 1 < invo->line1; po = po->next)
          i++;
      break;
   case C_tabdo:
      for ( ; t && i + 1 < invo->line1; t = t->next)
          i++;
      break;
   case C_argdo:
      i = invo->line1 - 1;
      break;
   default:
      break;
   }
   // set pcmark now
   if (invo->id == C_bufdo) {
      // Advance to the first listed book after "invo->line1".
      for (book = firstBook; 
            book && (book->fiNum < invo->line1 || !book->o.bookListed); 
            book = book->next
      ) {
         if (book->fiNum > invo->line2) {
            book = NULL;
            break;
         }
      } 
      if (book)
         bookGoto(invo, DOBOOK_FIRST, FORWARD, book->fiNum);
   } else
       setpcmark();
   listcmd_busy = true;       // avoids setting pcmark below

   while (!gotInterruptG && book) {
      if (invo->id == C_argdo) {
         // go to argument "i"
         if (i == ARGCOUNT)
             break;
         // Don't call do_argfile() when already there, it will try reloading the file.
         if (curPor->argListInd != i || !editing_arg_idx(curPor)) {
             do_argfile(invo, i);
         }
         if (curPor->argListInd != i)
             break;
      } ei (invo->id == C_windo) {
         // go to portal "po"
         if (!portalIsValid(po))
            break;
         gotoPortal(po);
         if (curPor != po)
            break;  // something must be wrong
         po = curPor->next;
      } ei (invo->id == C_tabdo) {
         // go to portal "t"
         if (!isTabValid(t))
            break;
         gotoTab(t, true, true);
         t = t->next;
      } ei (invo->id == C_bufdo) {
         // Remember the number of the next listed book, in case
         // ":bwipe" is used or autocommands do something strange.
         next_fnum = -1;
         for (book = curBook->next; book; book = book->next) {
            if (book->o.bookListed) {
               next_fnum = book->fiNum;
               break;
            }
         } 
      }

      ++i;

      // execute the command
      doCommand(invo->arg, invo->ea_getline, invo->cookie, DOCMD_VERBOSE + DOCMD_NOWAIT);

      if (invo->id == C_bufdo) {
         // Done?
         if (next_fnum < 0 || next_fnum > invo->line2)
             break;
         // Check if the book still exists.
         FOR_ALL_BOOKS(book) {
            if (book->fiNum == next_fnum)
               break;
         } 
         if (!book)
            break;

         bookGoto(invo, DOBOOK_FIRST, FORWARD, next_fnum);

         // If autocommands took us elsewhere, quit here.
         if (curBook->fiNum != next_fnum)
            break;
      }

      if (invo->id == C_windo) {
         validate_cursor();   // cursor may have moved

         // required when @scrollbind has been set
         if (curPor->o.scrollBind)
            normPostProcessScrollbind(true);
      }

      if (invo->id == C_windo || invo->id == C_tabdo)
         if (i >= invo->line2)
            break;
      if (invo->id == C_argdo && i >= invo->line2)
         break;
   }
   listcmd_busy = false;

   if (save_ei) {
      Book      *bnext;
      AutocommSave   aco;

      au_event_restore(save_ei);

      for (book = firstBook; book; book = bnext) {
         bnext = book->next;
         if (book->countPortals > 0 && (book->flags & BF_SYN_SET)) {
            book->flags &= ~BF_SYN_SET;

            //book was opened while Syntax autocommands were disabled,
            //need to trigger them now.
            if (book == curBook)
               applyAutocomms(
                  EVENT_SYNTAX, curBook->syntaxName, curBook->currFileName, true, curBook
               );
            else {
               auCommPrepareBook(&aco, book);
               if (curBook == book) {
                  applyAutocomms(EVENT_SYNTAX, book->syntaxName, book->currFileName, true, book);
                  auCommRestoreBook(&aco);
               }
            }

            // start over, in case autocommands messed things up.
            bnext = firstBook;
          }
      }
    }
    end_global_changes();
}

// ":compiler[!] {name}"
void
c_compiler(Invocation* invo) {
   CS old_cur_comp = NULL;
   CS p;

   if (*invo->arg == ZERO) {
      // List all compiler scripts.
      executeCommLine((CS)"echo fiGlobpath(&rtp, 'compiler/*.vim')");
                  // ) keep the indenter happy...
      return;
   }

   CS buf = alloc(STRLEN(invo->arg) + 14);

   if (invo->forceit) {
      // ":compiler! {name}" sets global options
      executeCommLine((CS)
         "command -nargs=* -keepscript CompilerSet set <args>");
   } else {
      // ":compiler! {name}" sets local options.
      // To remain backwards compatible "current_compiler" is always
      // used.  A user's compiler plugin may set it, the distributed
      // plugin will then skip the settings.  Afterwards set
      // "b:current_compiler" and restore "current_compiler".
      // Explicitly prepend "g:" to make it work in a function.
      old_cur_comp = get_var_value((CS)"g:current_compiler");
      if (old_cur_comp)
         old_cur_comp = copyStr(old_cur_comp);
      executeCommLine((CS) "command -nargs=* -keepscript CompilerSet setlocal <args>");
   }
   unletImpl(S"g:current_compiler", true);
   unletImpl(S"b:current_compiler", true);

   sprintf((char *)buf, "compiler/%s.vim", invo->arg);
   if (source_runtime(buf, DIP_ALL) == FAIL)
      showErrFmtMsg(_(e_compiler_not_supported_str), invo->arg);
   eeglFree(buf);

   executeCommLine((CS)":delcommand CompilerSet");

   // Set "b:current_compiler" from "current_compiler".
   p = get_var_value((CS)"g:current_compiler");
   if (p)
      set_internal_string_var((CS)"b:current_compiler", p);

   // Restore "current_compiler" for ":compiler {name}".
   if (!invo->forceit) {
      if (old_cur_comp) {
         set_internal_string_var((CS)"g:current_compiler", old_cur_comp);
         eeglFree(old_cur_comp);
      } else
         unletImpl((CS)"g:current_compiler", true);
   }
}

// ":checktime [buffer]"
void
c_checktime(Invocation* invo){
   Book   *book;
   int      save_no_check_timestamps = no_check_timestamps;

   no_check_timestamps = 0;
   if (invo->addr_count == 0)   // default is all books
      check_timestamps(false);
   else {
      book = bookFindFileByBookNr((int)invo->line2);
      if (book)   // cannot happen?
         (void)fiCheckBookTimestamp(book);
   }
   no_check_timestamps = save_no_check_timestamps;
}
//}}}
//{{{writing & flushing

// If 'autowrite' option set, try to write the file. Careful: autocommands may make "book" invalid!
// return FAIL for failure, OK otherwise
int
autowrite(Book *book, int forceit) {
   if (!(p_aw || p_awa) || book->o.modifiable
        // never autowrite a "nofile" or "nowrite" book
        || bookDontWrite(book)
        || (!forceit && !book->o.modifiable) || book->fullFileName == NULL)
      return FAIL;
   BookRef   bufref;
   bookStoreInRef(OUT &bufref, book);
   int r = bookWrite_all(book, forceit);

   // Writing may succeed but the book still changed, e.g., when there is a
   // conversion error.  We do want to return FAIL then.
   if (bookRefValid(&bufref) && doWasBookChanged(book))
      r = FAIL;
   return r;
}

// Flush all books, except the ones that are readonly or are never written.
void
doFlushAllBooks(void) {
   Book   *book;

   if (!(p_aw || p_awa))
      return;
   FOR_ALL_BOOKS(book) {
      if (doWasBookChanged(book) && book->o.modifiable && !bookDontWrite(book)) {
         BookRef   bufref;

         bookStoreInRef(OUT &bufref, book);

         (void)bookWrite_all(book, false);

         // an autocommand may have deleted the buffer
         if (!bookRefValid(&bufref))
            book = firstBook;
      }
   }
}

// Return true if buffer was changed and cannot be abandoned. For flags use the CCGD_ values.
int
check_changed(Book *book, int flags) {
   int      forceit = (flags & CCGD_FORCEIT);
   BookRef   bufref;

   bookStoreInRef(OUT &bufref, book);

   if (       !forceit
       && doWasBookChanged(book)
       && ((flags & CCGD_MULTWIN) || book->countPortals <= 1)
       && (!(flags & CCGD_AW) || autowrite(book, forceit) == FAIL)
    ){
      if ((p_confirm || (commModifierG.cmod_flags & CMOD_CONFIRM)) && book->o.modifiable) {
         if (term_job_running(book->term)) {
            return term_confirm_stop(book) == FAIL;
         }

         Book   *buf2;
         int      count = 0;

         if (flags & CCGD_ALLBOOKS) {
            FOR_ALL_BOOKS(buf2) {
               if (doWasBookChanged(buf2) && (buf2->fullFileName)) {
                  ++count;
               } 
            } 
         }
         if (!bookRefValid(&bufref))
            // Autocommand deleted buffer, oops!  It's not changed now.
            return false;

         dialog_changed(book, count > 1);

         if (!bookRefValid(&bufref))
         // Autocommand deleted buffer, oops!  It's not changed now.
            return false;
         return doWasBookChanged(book);
      }
      if (flags & CCGD_EXCMD)
         no_write_message();
      else
         no_write_message_nobang(curBook);
      return true;
   }
   return false;
}

// Ask the user what to do when abandoning a changed buffer. Must check 'write' option first!
void
dialog_changed(Book* book, int checkall) {  // may abandon all changed buffers
   Byte buff[DIALOG_MSG_SIZE];
   int ret;
   Book* buf2;
   Invocation invo;

   dialog_msg(buff, _("Save changes to \"%s\"?"), book->currFileName);
   if (checkall)
      ret = eeDialog_yesnoallcancel(EE_QUESTION, NULL, buff, 1);
   else
      ret = eeDialog_yesnocancel(EE_QUESTION, NULL, buff, 1);

   // Init invo pseudo-structure, this is needed for the check_overwrite() function.
   CLEAR_FIELD(invo);

   if (ret == EE_YES) {
      int   empty_bufname;

      empty_bufname = book->currFileName == NULL ? true : false;
      if (empty_bufname)
         bookSetName(book->fiNum, S"Untitled");

      if (check_overwrite(&invo, book, book->currFileName, book->fullFileName, false) == OK) {
         // didn't hit Cancel
         if (bookWrite_all(book, false) == OK)
            return;
      }

      // restore to empty when write failed
      if (empty_bufname) {
         book->currFileName = NULL;
         EE_CLEAR(book->fullFileName);
         EE_CLEAR(book->shortFileName);
         unchanged(book, false);
      }
   } ei (ret == EE_NO) {
      unchanged(book, false);
   } ei (ret == EE_ALL) {
      // Write all modified files that can be written.
      // Skip readonly buffers, these need to be confirmed individually.
      FOR_ALL_BOOKS(buf2) {
         if (doWasBookChanged(buf2)
             && buf2->fullFileName
             && !bookDontWrite(buf2)
             && buf2->o.modifiable
         ) {
            BookRef bufref;

            bookStoreInRef(OUT &bufref, buf2);
            if (buf2->currFileName && check_overwrite(&invo, buf2,
                    buf2->currFileName, buf2->fullFileName, false) == OK
            )
               // didn't hit Cancel
               (void)bookWrite_all(buf2, false);

            // an autocommand may have deleted the buffer
            if (!bookRefValid(&bufref))
               buf2 = firstBook;
          }
      }
   } ei (ret == EE_DISCARDALL) {
      FOR_ALL_BOOKS(buf2)
         unchanged(buf2, false);
   }
}

// Add a buffer number to "bufnrs", unless it's already there.
private void
add_bufnum(int *bufnrs, int *bufnump, int nr) {
   for (int i = 0; i < *bufnump; ++i) {
      if (bufnrs[i] == nr)
         return;
   } 
   bufnrs[*bufnump] = nr;
   *bufnump = *bufnump + 1;
}

// true if any buffer was changed and cannot be abandoned. That changed buffer becomes the 
// current buffer. When "unload" is true the current buffer is unloaded instead of making it
// hidden.  This is used for ":q!".
int
check_changed_any(
    int      hidden,      // Only check hidden buffers
    int      unload)
{
   int      ret = false;
   Book   *book;
   int      save;
   int      i;
   int      bufnum = 0;
   int      bufcount = 0;
   int      *bufnrs;
   Tab   *t;
   Portal   *po;

   // Make a list of all buffers, with the most important ones first.
   FOR_ALL_BOOKS(book)
      ++bufcount;

   if (bufcount == 0)
      return false;

   bufnrs = ALLOC_MULT(int, bufcount);

   // curBook
   bufnrs[bufnum++] = curBook->fiNum;

   // buffers in current tab
   FOR_ALL_PORTALS(po) {
      if (po->book != curBook)
         add_bufnum(bufnrs, &bufnum, po->book->fiNum);
   } 

    // buffers in other tabs
   FOR_ALL_TABS(t) {
      if (t != curtab) {
         FOR_ALL_PORTALS_IN_TAB(t, po)
            add_bufnum(bufnrs, &bufnum, po->book->fiNum);
      } 
   } 

   // any other buffer
   FOR_ALL_BOOKS(book)
      add_bufnum(bufnrs, &bufnum, book->fiNum);

   for (i = 0; i < bufnum; ++i) {
      book = bookFindFileByBookNr(bufnrs[i]);
      if (!book)
         continue;
      if ((!hidden || book->countPortals == 0) && doWasBookChanged(book)) {
         BookRef bufref;

         bookStoreInRef(OUT &bufref, book);
         if (term_job_running(book->term)) {
            if (term_try_stop_job(book) == FAIL)
                break;
         } else
            // Try auto-writing the buffer.  If this fails but the buffer no
            // longer exists it's not changed, that's OK.
            if (check_changed(book, (p_awa ? CCGD_AW : 0)
                | CCGD_MULTWIN
                | CCGD_ALLBOOKS) && bookRefValid(&bufref)
            )
               break;       // didn't save - still changes
      }
   }

   if (i >= bufnum)
      goto theend;

   // Get here if "book" cannot be abandoned.
   ret = true;
   isExitingG = false;
   // When ":confirm" used, don't give an error message.
   if (!(p_confirm || (commModifierG.cmod_flags & CMOD_CONFIRM))) {
      // There must be a wait_return() for this message, bookDo()
      // may cause a redraw.  But wait_return() is a no-op when vgetc()
      // is busy (Quit used from window menu), then make sure we don't cause a scroll up.
      if (vgetcBusyG > 0) {
          msgRowG = commlineRowG;
          msgColG = 0;
          msg_didout = false;
      }
      if (
         term_job_running(book->term)
             ? showErrFmtMsg(_(e_job_still_running_in_buffer_str), book->currFileName)
             :
         showErrFmtMsg(_(e_no_write_since_last_change_for_buffer_str),
             bookSpName(book) ? bookSpName(book) : book->currFileName))
      {
          save = no_wait_return;
          no_wait_return = false;
          wait_return(false);
          no_wait_return = save;
      }
   }

   // Try to find a portal into the buffer.
   if (book != curBook) {
      FOR_ALL_TAB_PORTALS(t, po) {
         if (po->book == book) {
            BookRef bufref;

            bookStoreInRef(OUT &bufref, book);

            goto_tab_port(t, po);

            // Paranoia: did autocomm wipe out the buffer with changes?
            if (!bookRefValid(&bufref))
                goto theend;
            goto buf_found;
         }
      } 
   } 
buf_found:

   // Open the changed buffer in the current portal.
   if (book != curBook)
      bookSetCurBook(book, unload ? DOBOOK_UNLOAD : DOBOOK_GOTO);

theend:
    eeglFree(bufnrs);
    return ret;
}

//return FAIL if there is no file name, OK if there is one give error message for FAIL
int
check_fname(void) {
   if (curBook->fullFileName == NULL) {
      emsg(_(e_no_file_name));
      return FAIL;
   }
   return OK;
}

// Flush the contents of a buffer, unless it has no file name.
// Return FAIL for failure, OK otherwise
int
bookWrite_all(Book *book, int forceit) {
   int       retval;
   Book   *curBookSaved = curBook;

   retval = (bookWrite(book, book->fullFileName, book->currFileName,
               (LineNr)1, book->mem.lineCount, NULL,
                    false, forceit, true, false));
   if (curBook != curBookSaved) {
      msg_source(getDecoFlags(HLF_W));
      msg(_("Warning: Entered other buffer unexpectedly (check autocommands)"));
   }
   return retval;
}

//}}}
//{{{command array

private int quitmore = 0;
private int ex_pressedreturn = false;

private CS doOneCommand(CS*, int, CondStack *, LineGetter fgetline, void* cookie);
private void append_command(CS cmd);

private void do_exbuffer(Invocation* invo);
private CS getargcmd(OUT CS*);
private int getargopt(Invocation* invo);

private LineNr default_address(Invocation* invo);
private void address_default_all(Invocation* invo);
private void get_flags(Invocation* invo);
#define HAVE_EX_SCRIPT_NI
private void   ex_script_ni(Invocation* invo);
private CS invalid_range(Invocation* invo);
private void   correct_range(Invocation* invo);
private CS replaceMakeProgramName(Invocation* invo, OUT CS p, OUT CS* commline);
private CS repl_commline(
      Invocation* invo, CS src, Unt srclen, CS repl, OUT CS* commline
);
private void   prepare_preview_window(void);
private void   back_to_current_window(Portal *curPor_save);
# define ex_syntime      c_ni
# define ex_loadkeymap   c_ni
private void   close_redir(void);
#define ex_diffoff       c_ni
#define ex_diffpatch     c_ni
#define ex_diffgetput    c_ni
#define ex_diffsplit     c_ni
#define ex_diffthis      c_ni
#define ex_diffupdate    c_ni


#define ex_profile       c_ni

// Declare the full commands table[].
#define DO_DECLARE_COMMANDS
#include "commands.h"
#undef DO_DECLARE_COMMANDS
#include "indices/commands.h"

private Byte dollar_command[2] = {'$', ZERO};


// Struct for storing a line inside a while/for loop
typedef struct {
   CS line;     // command line
   LineNr lnum;      // sourcing_lnum of the line
} LoopComm;

// Structure used to store info for line position in a while or for loop.
// This is required, because doOneCommand() may invoke ex_function(), which
// reads more lines that may come from the while/for loop.
typedef struct {
   ArrayList   *lines_gap;      // growarray with line info
   int      current_line;      // last read line from growarray
   int      repeating;      // true when looping a second time
   // When "repeating" is false use "getline" and "cookie" to get lines
   LineGetter lc_getline;
   void   *cookie;
} LoopCookie;

private CS get_loop_line(Unt c, void *cookie, int indent, GetlineAlgo options);
private int   store_loop_line(ArrayList *gap, CS line);
private void   free_commlines(ArrayList *gap);

// Struct to save a few things while debugging.  Used in doCommand() only.
typedef struct {
   int trylevel;
   int force_abort;
   Exception* caught_stack;
   CS vv_exception;
   CS vv_throwpoint;
   int anyEmsgG;
   int gotInterruptG;
   int did_throw;
   Boole need_rethrow;
   int check_cstack;
   Exception* current_exception;
} DebugStuff;

private void
saveDbgStuff(DebugStuff* dsp) {
   dsp->trylevel   = trylevel;      trylevel = 0;
   dsp->force_abort   = force_abort;      force_abort = false;
   dsp->caught_stack   = caught_stack;      caught_stack = NULL;
   dsp->vv_exception   = v_exception(NULL);
   dsp->vv_throwpoint   = v_throwpoint(NULL);

   // Necessary for debugging an inactive ":catch", ":finally", ":endtry"
   dsp->anyEmsgG   = anyEmsgG;      anyEmsgG     = false;
   dsp->gotInterruptG   = gotInterruptG;      gotInterruptG        = false;
   dsp->did_throw   = did_throw;      did_throw    = false;
   dsp->need_rethrow   = need_rethrow;      need_rethrow = false;
   dsp->check_cstack   = check_cstack;      
   check_cstack = false;
   dsp->current_exception = current_exception;   current_exception = NULL;
}

private void
restore_DebugStuff(DebugStuff* dsp) {
   suppress_errthrow = false;
   trylevel = dsp->trylevel;
   force_abort = dsp->force_abort;
   caught_stack = dsp->caught_stack;
   (void)v_exception(dsp->vv_exception);
   (void)v_throwpoint(dsp->vv_throwpoint);
   anyEmsgG = dsp->anyEmsgG;
   gotInterruptG = dsp->gotInterruptG;
   did_throw = dsp->did_throw;
   need_rethrow = dsp->need_rethrow;
   check_cstack = dsp->check_cstack;
   current_exception = dsp->current_exception;
}

// Check if files are the same file.
// fnum is a buffer number. 0 == current buffer, 1-or-more must be a valid buffer ID.
// fullFName is a full path to where a buffer lives on-disk or would live on-disk.
private Boole
isSameFile(int fnum, CS fullFName) {
   if (fnum != 0) {
      if (fnum == curBook->fiNum)
         return true;
      return false;
   }

   if (!fullFName)
      return false;

   if (*fullFName == ZERO)
      return true;

   // TODO: Need a reliable way to know whether a buffer is meant to live
   // on-disk !curBook->isDevNumValid is not always available (example: missing
   // on Portals)
   if (curBook->shortFileName && *curBook->shortFileName != ZERO)
      // This occurs with unsaved buffers. In which case `fullFName` actually
      // corresponds to curBook->shortFileName
      return fnamecmp(fullFName, curBook->shortFileName) == 0;

   return fNameMatchesCurBook(fullFName);
}

// Print the executed command for when 'verbose' is set.
// When "lnum" is 0 only print the command.
private void
msg_verbose_cmd(LineNr lnum, CS cmd) {
   ++no_wait_return;
   verbose_enter_scroll();

   if (lnum == 0)
      smsg(_("Executing: %s"), cmd);
   else
      smsg(_("line %ld: %s"), (long)lnum, cmd);
   if (msg_silent == 0)
      msg_puts(S"\n");   // don't overwrite this

   verbose_leave_scroll();
   --no_wait_return;
}

// Execute a simple command line.  Used for translated commands like "*".
int
executeCommLine(CS cmd) {
   return doCommand(cmd, NULL, NULL, DOCMD_VERBOSE|DOCMD_NOWAIT|DOCMD_KEYTYPED);
}

// Execute the "+cmd" argument of "edit +cmd fname" and the like.
// This allows for using a range without ":" in Vim9 script.
private int
do_cmd_argument(CS cmd) {
   return doCommand(cmd, NULL, NULL, DOCMD_VERBOSE|DOCMD_NOWAIT|DOCMD_KEYTYPED);
}

// Handle when "did_throw" is set after executing commands.
void
handle_did_throw(void) {
   CS p = NULL;
   MsgList* messages = NULL;
   ESTACK_CHECK_DECLARATION;

   //If the uncaught exception is a user exception, report it as an error. If it is an error 
   //exception, display the saved error message now.  For an interrupt exception, do nothing; the
   //interrupt message is given elsewhere.
   switch (current_exception->type) {
   case ET_USER:
      eeSnprintf(IObuff, IOSIZE, _(e_exception_not_caught_str), current_exception->value);
      p = copyStr(IObuff);
      break;
   case ET_ERROR:
      messages = current_exception->messages;
      current_exception->messages = NULL;
      break;
   case ET_INTERRUPT:
      break;
   }

   estack_push(ETYPE_EXCEPT, current_exception->throw_name, current_exception->throw_lnum);
   ESTACK_CHECK_SETUP;
   current_exception->throw_name = NULL;

   discard_current_exception();   // uses IObuff if 'verbose'

   // If "silent!" is active the uncaught exception is not fatal.
   if (emsg_silent == 0) {
      suppress_errthrow = true;
      force_abort = true;
   }

   if (messages) {
      do {
         MsgList* next = messages->next;
         emsg(messages->msg);
         eeglFree(messages->msg);
         eeglFree(messages->sfile);
         eeglFree(messages);
         messages = next;
      }
      while (messages);
   } ei (p) {
      emsg(p);
      eeglFree(p);
   }
   eeglFree(SOURCING_NAME);
   ESTACK_CHECK_NOW;
   estack_pop();
}

//Obtain a line when inside a ":while" or ":for" loop.
private CS
get_loop_line(Unt c, void* cookie, int indent, GetlineAlgo options) {
   LoopCookie* cp = (LoopCookie *)cookie;
   CS line;

   if (cp->current_line + 1 >= cp->lines_gap->len) {
      if (cp->repeating)
          return NULL;   // trying to read past ":endwhile"/":endfor"

      // First time inside the ":while"/":for": get line normally.
      if (!cp->lc_getline)
         line = getCommline(c, 0L, indent, 0);
      else
         line = (cp->lc_getline)(c, cp->cookie, indent, options);
      if (line && store_loop_line(cp->lines_gap, line) == OK)
         ++cp->current_line;

      return line;
   }

   keyWasTypedG = false;
   ++cp->current_line;
   LoopComm* wp = (LoopComm *)(cp->lines_gap->c) + cp->current_line;
   SOURCING_LNUM = wp->lnum;
   return copyStr(wp->line);
}

// Store a line in "gap" so that a ":while" loop can execute it again.
private int
store_loop_line(ArrayList *gap, CS line) {
   if (ga_grow(gap, 1) == FAIL)
      return FAIL;
   ((LoopComm *)(gap->c))[gap->len].line = copyStr(line);
   ((LoopComm *)(gap->c))[gap->len].lnum = SOURCING_LNUM;
   ++gap->len;
   return OK;
}

// Free the lines stored for a ":while" or ":for" loop.
private void
free_commlines(ArrayList *gap) {
   while (gap->len > 0) {
      eeglFree(((LoopComm *)(gap->c))[gap->len - 1].line);
      --gap->len;
   }
}

//If "fgetline" is get_loop_line(), return true if the getline it uses equals
//"func". Otherwise return true when "fgetline" equals "func".
int
getline_equal(
   LineGetter fgetline,
   void* cookie,      // argument for fgetline()
   LineGetter func
){
   //When "fgetline" is "get_loop_line()" use the "cookie" to find the 
   //function that's originally used to obtain the lines.  This may be
   //nested several levels.
   LineGetter gp = fgetline;
   LoopCookie* cp = (LoopCookie *)cookie;
   while (gp == get_loop_line) {
      gp = cp->lc_getline;
      cp = cp->cookie;
   }
   return gp == func;
}

//If "fgetline" is get_loop_line(), return the cookie used by the original
//getline function.  Otherwise return "cookie".
void*
getline_cookie(
   LineGetter fgetline,
   void* cookie     // argument for fgetline()
) {
   //When "fgetline" is "get_loop_line()" use the "cookie" to find the
   //cookie that's originally used to obtain the lines. This may be nested several levels.
   LineGetter gp = fgetline;
   LoopCookie* cp = (LoopCookie *)cookie;
   while (gp == get_loop_line) {
      gp = cp->lc_getline;
      cp = cp->cookie;
   }
   return cp;
}

//Get the next line source line without advancing.
CS
getline_peek(
   LineGetter fgetline,
   void* cookie      // argument for fgetline()
){
   LoopComm      *wp;

   // When "fgetline" is "get_loop_line()" use the "cookie" to find the
   // cookie that's originally used to obtain the lines.  This may be nested
   // several levels.
   LineGetter gp = fgetline;
   LoopCookie* cp = (LoopCookie *)cookie;
   while (gp == get_loop_line) {
      if (cp->current_line + 1 < cp->lines_gap->len) {
         // executing lines a second time, use the stored copy
         wp = (LoopComm *)(cp->lines_gap->c) + cp->current_line + 1;
         return wp->line;
      }
      gp = cp->lc_getline;
      cp = cp->cookie;
   }
   if (gp == &getsourceline)
      return source_nextline(cp);
   return NULL;
}


//Helper function to apply an offset for buffer commands, i.e. ":bdelete",
//":bwipeout", etc. Returns the buffer number.
private int
compute_buffer_local_count(int addressKind, int lnum, int offset) {
   Book   *nextbuf;
   int     count = offset;

   Book* book = firstBook;
   while (book->next && book->fiNum < lnum)
      book = book->next;
   while (count != 0) {
      count += (offset < 0) ? 1 : -1;
      nextbuf = (offset < 0) ? book->prev : book->next;
      if (nextbuf == NULL)
         break;
      book = nextbuf;
      if (addressKind == ADDR_LOADED_BUFFERS)
         // skip over unloaded buffers
         while (book->mem.mfile == NULL) {
            nextbuf = (offset < 0) ? book->prev : book->next;
            if (nextbuf == NULL)
                break;
            book = nextbuf;
         }
   }
   // we might have gone too far, last buffer is not loadedd
   if (addressKind == ADDR_LOADED_BUFFERS) {
      while (book->mem.mfile == NULL) {
         nextbuf = (offset >= 0) ? book->prev : book->next;
         if (nextbuf == NULL)
            break;
         book = nextbuf;
      }
   }
   return book->fiNum;
}

//Return the portal number of "portal". When "portal" is NULL, return the number of portal.
private int
getPortNr(Portal* portal) {
   int      nr = 0;
   Portal   *port;
   FOR_ALL_PORTALS(port) {
      ++nr;
      if (port == portal)
         break;
   }
   return nr;
}

private int
current_tab_nr(Tab *tab) {
   int nr = 0;
   Tab   *t;
   FOR_ALL_TABS(t) {
      ++nr;
      if (t == tab)
         break;
   }
   return nr;
}

#define GET_PORT_NR getPortNr(curPor)
#define LAST_WIN_NR getPortNr(NULL)
#define CURRENT_TAB_NR current_tab_nr(curtab)
#define LAST_TAB_NR current_tab_nr(NULL)

//}}}
//{{{command execution

// doCommand(): execute one command line
//
// 1. Execute "commline" when it is not NULL.
//    Otherwise, or if more lines are needed, fgetline() is used.
// 2. Split up in parts separated with '|'.
//
// This function can be called recursively!
//
// flags:
// DOCMD_VERBOSE  - The command will be included in the error message.
// DOCMD_NOWAIT   - Don't call wait_return() and friends.
// DOCMD_REPEAT   - Repeat execution until fgetline() returns NULL.
// DOCMD_KEYTYPED - Don't reset keyWasTypedG.
// DOCMD_EXCRESET - Reset the exception environment (used for debugging).
// DOCMD_KEEPLINE - Store first typed line (for repeating with ".").
//
// return FAIL if commline could not be executed, OK otherwise
int
doCommand(
   CS commline,
   LineGetter fgetline,
   void* cookie,      // argument for fgetline()
   Unt flags
){
   CS commlineCopy = NULL;   // copy of cmd line
   int      used_getline = false;   // used "fgetline" to obtain command
   static int   recursive = 0;      // recursive depth
   int      msg_didout_before_start = 0;
   int      count = 0;      // line number count
   int      did_inc_isRedrawingDisabledG = false;
   int      retval = OK;
   CondStack   cstack;         // conditional stack
   ArrayList   lines_ga;      // keep lines for ":while"/":for"
   int      current_line = 0;   // active line in lines_ga
   int      current_line_before = 0;
   CS fname = NULL;      // function or script name
   LineNr   *breakpoint = NULL;   // ptr to breakpoint field in cookie
   int      *dbg_tick = NULL;   // ptr to dbg_tick field in cookie
   DebugStuff debug_saved;   // saved things for debug mode
   int      initial_trylevel;
   MsgList   **saved_msg_list = NULL;
   MsgList   *private_msg_list = NULL;

   // "fgetline" and "cookie" passed to doOneCommand()
   LineGetter commGetLine;
   void* commCookie;
   LoopCookie commLoopCookie;
   void* real_cookie;
   static int callDepth = 0;      // recursiveness
   // For every pair of doCommand()/doOneCommand() calls, use an extra memory
   // location for storing error messages to be converted to an exception.
   // This ensures that the do_errthrow() call in doOneCommand() does not
   // combine the messages stored by an earlier invocation of doOneCommand()
   // with the command name of the later one.  This would happen when
   // BufWritePost autocommands are executed after a write error.
   saved_msg_list = msg_list;
   msg_list = &private_msg_list;

   // It's possible to create an endless loop with ":execute", catch that
   // here.  The value of 200 allows nested function calls, ":source", etc.
   // Allow 200 or 'maxfuncdepth', whatever is larger.
   if (callDepth >= 200 && callDepth >= p_mfd) {
      emsg(_(e_command_too_recursive));
      // When converting to an exception, we do not include the command name
      // since this is not an error of the specific command.
      do_errthrow((CondStack *)NULL, (CS)NULL);
      msg_list = saved_msg_list;
      return FAIL;
   }
   ++callDepth;

   CLEAR_FIELD(cstack);
   cstack.ind = -1;
   ga_init2(&lines_ga, sizeof(LoopComm), 10);

   real_cookie = getline_cookie(fgetline, cookie);

   // Inside a function use a higher nesting level.
   int isGetlineAFn = getline_equal(fgetline, cookie, &get_func_line);
   if (isGetlineAFn && ex_nesting_level == func_level(real_cookie))
      ++ex_nesting_level;

   // Get the function or script name and the address where the next breakpoint
   // line and the debug tick for a function or script are stored.
   if (isGetlineAFn) {
      fname = func_name(real_cookie);
      breakpoint = func_breakpoint(real_cookie);
      dbg_tick = func_dbg_tick(real_cookie);
   } ei (getline_equal(fgetline, cookie, &getsourceline)) {
      fname = SOURCING_NAME;
      breakpoint = source_breakpoint(real_cookie);
      dbg_tick = source_dbg_tick(real_cookie);
   }

   // Initialize "force_abort"  and "suppress_errthrow" at the top level.
   if (!recursive) {
      force_abort = false;
      suppress_errthrow = false;
   }

   // If requested, store and reset the global values controlling the
   // exception handling (used when debugging).  Otherwise clear it to avoid
   // a bogus compiler warning when the optimizer uses inline functions...
   if (flags & DOCMD_EXCRESET)
      saveDbgStuff(&debug_saved);
   else
      CLEAR_FIELD(debug_saved);

   initial_trylevel = trylevel;

   // "did_throw" will be set to true if an exception will be thrown
   did_throw = false;
   // "anyEmsgG" will be set to true when emsg() is used, in which case we
   // cancel the whole command line, and any if/endif or loop.
   // If force_abort is set, we cancel everything.
   anyEmsgG = false;

   // keyWasTypedG is only set when calling vgetc(). Reset it here when not calling vgetc() (
   // sourced command lines).
   if (!(flags & DOCMD_KEYTYPED) && !getline_equal(fgetline, cookie, getexline))
      keyWasTypedG = false;

   //Continue executing command lines:
   //- when inside an ":if", ":while" or ":for"
   //- for multiple commands on one line, separated with '|'
   //- when repeating until there are no more lines (for ":source")
   CS nextCommline = commline;
   do {
      isGetlineAFn = getline_equal(fgetline, cookie, get_func_line);

      // stop skipping cmds for an error msg after all endif/while/for
      if (!nextCommline && !force_abort && cstack.ind < 0 
            && !(isGetlineAFn && func_has_abort(real_cookie))
      ) {
         anyEmsgG = false;
      }

      //1. If repeating a line in a loop, get a line from lines_ga.
      //2. If no line given: Get an allocated line with fgetline().
      //3. If a line is given: Make a copy, so we can mess with it.
      //4. If repeating, get a previous line from lines_ga.
      if (cstack.loopLevel > 0 && current_line < lines_ga.len) {
         // Each '|' separated command is stored separately in lines_ga, to
         // be able to jump to it.  Don't use nextCommline now.
         EE_CLEAR(commlineCopy);

         // Check if a function has returned or, unless it has an unclosed
         // try conditional, aborted.
         if (isGetlineAFn) {
            if (func_has_ended(real_cookie)) {
               retval = FAIL;
               break;
            }
         }

         // Check if a sourced file hit a ":finish" command.
         if (sourceFileIsFinished(fgetline, cookie)) {
            retval = FAIL;
            break;
         }

         // If breakpoints have been added/deleted need to check for it.
         if (breakpoint && dbg_tick && *dbg_tick != debug_tick) {
            *breakpoint = dbg_find_breakpoint(
                  getline_equal(fgetline, cookie, &getsourceline), fname, SOURCING_LNUM
            );
            *dbg_tick = debug_tick;
         }

         nextCommline = ((LoopComm *)(lines_ga.c))[current_line].line;
         SOURCING_LNUM = ((LoopComm *)(lines_ga.c))[current_line].lnum;

         // Did we encounter a breakpoint?
         if (breakpoint && *breakpoint != 0 && *breakpoint <= SOURCING_LNUM) {
            dbg_breakpoint(fname, SOURCING_LNUM);
            // Find next breakpoint.
            *breakpoint = dbg_find_breakpoint(
                getline_equal(fgetline, cookie, &getsourceline), fname, SOURCING_LNUM
            );
            *dbg_tick = debug_tick;
         }
      }

      // 2. If no line given, get an allocated line with fgetline().
      if (!nextCommline) {
        //Need to set msg_didout for the first line after an ":if",
        //otherwise the ":if" will be overwritten.
        if (count == 1 && getline_equal(fgetline, cookie, getexline))
           msg_didout = true;
        if (fgetline == NULL 
              || (nextCommline = 
                    fgetline(
                       ':', cookie,  cstack.ind < 0 ? 0 : (cstack.ind + 1) * 2, 
                       GETLINE_CONCAT_CONT
                 )) == NULL
        ) {
            // Don't call wait_return() for aborted command line.  The NULL
            // returned for the end of a sourced file or executed function
            // doesn't do this.
            if (keyWasTypedG && !(flags & DOCMD_REPEAT))
                need_wait_return = false;
            retval = FAIL;
            break;
         }
         used_getline = true;

         // Keep the first typed line. Clear it when more lines are typed.
         if (flags & DOCMD_KEEPLINE) {
            eeglFree(repeatCommlineG);
            if (count == 0)
               repeatCommlineG = copyStr(nextCommline);
            else
               repeatCommlineG = NULL;
         }
      }

      // 3. Make a copy of the command so we can mess with it.
      ei (!commlineCopy) {
         nextCommline = copyStr(nextCommline);
      }
      commlineCopy = nextCommline;

      //Inside a while/for loop, and when the command looks like a ":while"
      //or ":for", the line is stored, because we may need it later when looping.
      //
      //When there is a '|' and another command, it is stored separately,
      //because we need to be able to jump back to it from an :endwhile/:endfor.
      //
      //Pass a different "fgetline" function to doOneCommand() below,
      //that it stores lines in or reads them from "lines_ga".  Makes it
      //possible to define a function inside a while/for loop and handles line continuation.
      if ((cstack.loopLevel > 0 || has_loop_cmd(nextCommline))) {
         commGetLine = &get_loop_line;
         commCookie = (void *)&commLoopCookie;
         commLoopCookie.lines_gap = &lines_ga;
         commLoopCookie.current_line = current_line;
         commLoopCookie.lc_getline = fgetline;
         commLoopCookie.cookie = cookie;
         commLoopCookie.repeating = (current_line < lines_ga.len);

         // Save the current line when encountering it the first time.
         if (current_line == lines_ga.len && store_loop_line(&lines_ga, nextCommline) == FAIL) {
            retval = FAIL;
            break;
         }
         current_line_before = current_line;
      } else {
         commGetLine = fgetline;
         commCookie = cookie;
      }

      did_endif = false;

      if (count++ == 0) {
         //All output from the commands is put below each other, without waiting for a return. 
         //Don't do this when executing commands from a script or when being called recursive 
         //(e.g. for ":e +command file").
         if (!(flags & DOCMD_NOWAIT) && !recursive) {
            msg_didout_before_start = msg_didout;
            msg_didany = false; // no output yet
            msg_start();
            msg_scroll = true;  // put messages below each other
            ++no_wait_return;   // don't wait for return until finished
            ++isRedrawingDisabledG;
            did_inc_isRedrawingDisabledG = true;
         }
      }

      if ((p_verbose >= 15 && SOURCING_NAME) || p_verbose >= 16)
         msg_verbose_cmd(SOURCING_LNUM, commlineCopy);

      //2. Execute one '|' separated command.
      //   doOneCommand() will return NULL if there is no trailing '|'.
      //   "commlineCopy" can change, e.g. for '%' and '#' expansion.
      ++recursive;
      nextCommline = doOneCommand(&commlineCopy, flags, &cstack, commGetLine, commCookie);
      --recursive;

      if (commCookie == (void *)&commLoopCookie)
          // Use "current_line" from "commLoopCookie", it may have been
          // incremented when defining a function.
          current_line = commLoopCookie.current_line;

      if (nextCommline == NULL) {
          EE_CLEAR(commlineCopy);

         // If the command was typed, remember it for the ':' register.
         // Do this AFTER executing the command to make :@: work.
         if (getline_equal(fgetline, cookie, getexline) && newLastCommlineG) {
            eeglFreeString(lastCommlineG);
            lastCommlineG = newLastCommlineG;
            newLastCommlineG = NULL;
         }
      } else {
          // need to copy the command after the '|' to commlineCopy, for the next doOneCommand()
          STRMOVE(commlineCopy, nextCommline);
          nextCommline = commlineCopy;
      }


      // reset anyEmsgG for a function that is not aborted by an error
      if (anyEmsgG && !force_abort
               && getline_equal(fgetline, cookie, &get_func_line)
               && !func_has_abort(real_cookie)) {
          anyEmsgG = false;
      }

      if (cstack.loopLevel > 0) {
          ++current_line;

          /*
           * An ":endwhile", ":endfor" and ":continue" is handled here.
           * If we were executing commands, jump back to the ":while" or
           * ":for".
           * If we were not executing commands, decrement loopLevel.
           */
         if (cstack.loopFlags & (CSL_HAD_CONT | CSL_HAD_ENDLOOP)) {
            cstack.loopFlags &= ~(CSL_HAD_CONT | CSL_HAD_ENDLOOP);

            // Jump back to the matching ":while" or ":for".  Be careful
            // not to use a cs_line[] from an entry that isn't a ":while"
            // or ":for": It would make "current_line" invalid and can
            // cause a crash.
            if (!anyEmsgG && !gotInterruptG && !did_throw
               && cstack.ind >= 0
               && (cstack.flags[cstack.ind] & (CSF_WHILE | CSF_FOR))
               && cstack.cs_line[cstack.ind] >= 0
               && (cstack.flags[cstack.ind] & CSF_ACTIVE)
            ){
               current_line = cstack.cs_line[cstack.ind]; // remember we jumped there
               cstack.loopFlags |= CSL_HAD_LOOP;
               line_breakcheck();      // check if CTRL-C typed

               // Check for the next breakpoint at or after the ":while" or ":for".
               if (breakpoint && lines_ga.len > current_line) {
                  *breakpoint = dbg_find_breakpoint(
                     getline_equal(fgetline, cookie, &getsourceline), fname,
                     ((LoopComm *)lines_ga.c)[current_line].lnum-1
                  );
                  *dbg_tick = debug_tick;
               }
            } else {
               // can only get here with ":endwhile" or ":endfor"
               if (cstack.ind >= 0)
                  rewind_conditionals(
                        &cstack, cstack.ind - 1, CSF_WHILE | CSF_FOR, &cstack.loopLevel
                  );
            }
         }
         //For a ":while" or ":for" we need to remember the line number.
         ei (cstack.loopFlags & CSL_HAD_LOOP) {
            cstack.loopFlags &= ~CSL_HAD_LOOP;
            cstack.cs_line[cstack.ind] = current_line_before;
         }
      }

      // Check for the next breakpoint after a watchexpression
      if (breakpoint && has_watchexpr()) {
         *breakpoint = dbg_find_breakpoint(false, fname, SOURCING_LNUM);
         *dbg_tick = debug_tick;
      }

      // When not inside any ":while" loop, clear remembered lines.
      if (cstack.loopLevel == 0) {
         if (lines_ga.len > 0) {
            SOURCING_LNUM = ((LoopComm *)lines_ga.c)[lines_ga.len - 1].lnum;
            free_commlines(&lines_ga);
         }
         current_line = 0;
      }

      // A ":finally" makes anyEmsgG, gotInterruptG, and did_throw pending for being restored at the
      // ":endtry".  Reset them here and set the ACTIVE and FINALLY flags, so that the finally 
      // clause gets executed. This includes the case where a missing ":endif", ":endwhile" or
      // ":endfor" was detected by the ":finally" itself.
      if (cstack.loopFlags & CSL_HAD_FINA) {
          cstack.loopFlags &= ~CSL_HAD_FINA;
          report_make_pending(cstack.pending[cstack.ind]
             & (CSTP_ERROR | CSTP_INTERRUPT | CSTP_THROW),
             did_throw ? (void *)current_exception : NULL);
          anyEmsgG = gotInterruptG = did_throw = false;
          cstack.flags[cstack.ind] |= CSF_ACTIVE | CSF_FINALLY;
      }

      // Update global "trylevel" for recursive calls to doCommand() from within this loop.
      trylevel = initial_trylevel + cstack.tryLevel;

      //If the outermost try conditional (across function calls and sourced
      //files) is aborted because of an error, an interrupt, or an uncaught
      //exception, cancel everything.  If it is left normally, reset
      //force_abort to get the non-EH compatible abortion behavior for the rest of the script.
      if (trylevel == 0 && !anyEmsgG && !gotInterruptG && !did_throw)
         force_abort = false;

      // Convert an interrupt to an exception if appropriate.
      (void)do_intthrow(&cstack);

    }
    //Continue executing command lines when:
    //- no CTRL-C typed, no aborting error, no exception thrown or try
    //  conditionals need to be checked for executing finally clauses or
    //  catching an interrupt exception
    //- didn't get an error message or lines are not typed
    //- there is a command after '|', inside a :if, :while, :for or :try, or
    //  looping for ":source" command or function call.
    while (!((gotInterruptG
               || (anyEmsgG && force_abort)
               || did_throw
            )
             && cstack.tryLevel == 0
           )
       && !(anyEmsgG
           // Keep going when inside try/catch, so that the error can be dealt with, except when it
           // is a syntax error, it may cause the :endtry to be missed.
           && (cstack.tryLevel == 0 || anySyntaxEmsgS)
           && used_getline && getline_equal(fgetline, cookie, getexline)
       )
       && (nextCommline
         || cstack.ind >= 0
         || (flags & DOCMD_REPEAT))
   ); // do while

   eeglFree(commlineCopy);
   anySyntaxEmsgS = false;
   free_commlines(&lines_ga);
   ga_clear(&lines_ga);

   if (cstack.ind >= 0) {
      // If a sourced file or executed function ran to its end, report the unclosed conditional.
      if (!gotInterruptG && !did_throw && !aborting()
         && ((getline_equal(fgetline, cookie, &getsourceline)
            && !sourceFileIsFinished(fgetline, cookie))
             || (getline_equal(fgetline, cookie, &get_func_line)
                      && !func_has_ended(real_cookie)))
      ) {
         if (cstack.flags[cstack.ind] & CSF_TRY)
            emsg(_(e_missing_endtry));
         ei (cstack.flags[cstack.ind] & CSF_WHILE)
            emsg(_(e_missing_endwhile));
         ei (cstack.flags[cstack.ind] & CSF_FOR)
            emsg(_(e_missing_endfor));
         else
            emsg(_(e_missing_endif));
      }

      /*
       * Reset "trylevel" in case of a ":finish" or ":return" or a missing
       * ":endtry" in a sourced file or executed function.  If the try
       * conditional is in its finally clause, ignore anything pending.
       * If it is in a catch clause, finish the caught exception.
       * Also cleanup any "forInfo" structures.
       */
      do {
         int idx = cleanup_conditionals(&cstack, 0, true);

         if (idx >= 0)
            --idx;       // remove try block not in its finally clause
         rewind_conditionals(&cstack, idx, CSF_WHILE | CSF_FOR,
                        &cstack.loopLevel);
      }    while (cstack.ind >= 0);
      trylevel = initial_trylevel;
   }

   // If a missing ":endtry", ":endwhile", ":endfor", or ":endif" or a memory
   // lack was reported above and the error message is to be converted to an
   // exception, do this now after rewinding the cstack.
   do_errthrow(&cstack, getline_equal(fgetline, cookie, &get_func_line)
             ? (CS)"endfunction" : (CS)NULL);

   if (trylevel == 0) {
      // Just in case did_throw got set but current_exception wasn't.
      if (current_exception == NULL)
          did_throw = false;

      /*
       * When an exception is being thrown out of the outermost try
       * conditional, discard the uncaught exception, disable the conversion
       * of interrupts or errors to exceptions, and ensure that no more
       * commands are executed.
       */
      if (did_throw)
         handle_did_throw();
      /*
       * On an interrupt or an aborting error not converted to an exception,
       * disable the conversion of errors to exceptions.  (Interrupts are not
       * converted anymore, here.) This enables also the interrupt message
       * when force_abort is set and anyEmsgG unset in case of an interrupt
       * from a finally clause after an error.
       */
      ei (gotInterruptG || (anyEmsgG && force_abort))
         suppress_errthrow = true;
   }

   //The current cstack will be freed when doCommand() returns. An uncaught exception will have to
   //be rethrown in the previous cstack. If a function has just returned or a script file was just 
   //finished and the previous cstack belongs to the same function or, respectively, script file, 
   //it will have to be checked for finally clauses to be executed due to the ":return" or 
   //":finish".  This is done in doOneCommand().
   if (did_throw)
      need_rethrow = true;
   if ((getline_equal(fgetline, cookie, &getsourceline)
            && ex_nesting_level > source_level(real_cookie))
       || (getline_equal(fgetline, cookie, &get_func_line)
            && ex_nesting_level > func_level(real_cookie) + 1)
   ){
      if (!did_throw)
         check_cstack = true;
   } else {
      // When leaving a function, reduce nesting level.
      if (getline_equal(fgetline, cookie, get_func_line))
          --ex_nesting_level;
      // Go to debug mode when returning from a function in which we are single-stepping.
      if ((getline_equal(fgetline, cookie, &getsourceline)
             || getline_equal(fgetline, cookie, get_func_line))
         && ex_nesting_level + 1 <= debug_break_level)
          do_debug(getline_equal(fgetline, cookie, &getsourceline)
             ? (CS)_("End of sourced file")
             : (CS)_("End of function"));
   }

   // Restore the exception environment (done after returning from the debugger).
   if (flags & DOCMD_EXCRESET)
      restore_DebugStuff(&debug_saved);

   msg_list = saved_msg_list;

   // Cleanup if "cs_emsg_silent_list" remains.
   if (cstack.cs_emsg_silent_list) {
      EMsgList* elem;
      EMsgList* temp;

      for (elem = cstack.cs_emsg_silent_list; elem; elem = temp) {
          temp = elem->next;
          eeglFree(elem);
      }
   }

   /*
    * If there was too much output to fit on the command line, ask the user to
    * hit return before redrawing the screen. With the ":global" command we do
    * this only once after the command is finished.
    */
   if (did_inc_isRedrawingDisabledG) {
      if (isRedrawingDisabledG > 0)
         --isRedrawingDisabledG;
      --no_wait_return;
      msg_scroll = false;

      // When just finished an ":if"-":else" which was typed, no need to
      // wait for hit-return.  Also for an error situation.
      if (retval == FAIL || (did_endif && keyWasTypedG && !anyEmsgG)) {
         need_wait_return = false;
         msg_didany = false;      // don't wait when restarting edit
      } ei (need_wait_return) {
         // The msg_start() above clears msg_didout. The wait_return() we do
         // here should not overwrite the command that may be shown before doing that.
         msg_didout |= msg_didout_before_start;
         wait_return(false);
      }
   }

   did_endif = false;  // in case doCommand used recursively

   --callDepth;
   return retval;
}

// Execute one Command.
//
//If "flags" has DOCMD_VERBOSE, the command will be included in the error message.
//
//1. skip comment lines and leading space
//2. handle command modifiers
//3. find the command
//4. parse range
//5. Parse the command.
//6. parse arguments
//7. switch on command name
//
//Note: "fgetline" can be NULL.
//
//This function may be called recursively!
private CS
doOneCommand(
   OUT CS* commline,
   int flags,
   CondStack* cstack,
   LineGetter fgetline,
   void* cookie      // argument for fgetline()
){
   CS p;
   LineNr   lnum;
   long   n;
   CS   errorMsg = null;
   CS after_modifier = NULL;
   Invocation   invo;         // command arguments
   CommandModifier  saveCommModifier;
   int      save_reg_executing = reg_executing;
   int      save_pending_end_reg_executing = pending_end_reg_executing;
   int      ni;         // set when Not Implemented
   CS cmd;
   int      may_have_range;
   int      did_set_expr_line = false;
   int      sourcing = flags & DOCMD_VERBOSE;
   int      did_append_cmd = false;

   CLEAR_FIELD(invo);
   invo.line1 = 1;
   invo.line2 = 1;
   ++ex_nesting_level;
   // When the last file has not been edited :q has to be typed twice.
   if (quitmore
          // avoid that a function call in 'statusline' does this
          && !getline_equal(fgetline, cookie, get_func_line)
          // avoid that an autocommand, e.g. QuitPre, does this
          && !getline_equal(fgetline, cookie, &getnextac)) {
      --quitmore;
   } 

   //Reset browse, confirm, etc..  They are restored when returning, for recursive calls.
   saveCommModifier = commModifierG;

   // "#!anything" is handled like a comment.
   if ((*commline)[0] == '#' && (*commline)[1] == '!')
      goto doend;
   if (isComment(*commline)) {
      goto doend; 
   }

   //1. Skip comment lines and leading white space and colons.
   //2. Handle command modifiers.
   // The "invo" structure holds the arguments that can be used.
   invo.comm = *commline;
   invo.commline = commline;
   invo.ea_getline = fgetline;
   invo.cookie = cookie;
   invo.cstack = cstack;
   if (parse_command_modifiers(&invo, OUT &errorMsg, &commModifierG, false) == FAIL)
      goto doend;
   applyCommModifiers(&commModifierG);
   after_modifier = invo.comm;

   invo.skip = anyEmsgG || gotInterruptG || did_throw 
      || (cstack->ind >= 0 && !(cstack->flags[cstack->ind] & CSF_ACTIVE));

   //3. Skip over the range to find the command.  Let "p" point to after it.
   //
   //We need the command to know what kind of range it uses.
   cmd = invo.comm;
   
   may_have_range = true;
   if (may_have_range)
      invo.comm = skip_range(invo.comm, true, NULL);

   p = findCommand(&invo, NULL, NULL);

   invo.comm = cmd;

   // May go to debug mode.  If this happens and the ">quit" debug command is
   // used, throw an interrupt exception and skip the next command.
   dbg_check_breakpoint(&invo);
   if (!invo.skip && gotInterruptG) {
      invo.skip = true;
      (void)do_intthrow(cstack);
   }

   //4. parse a range specifier of the form: addr [,addr] [;addr] ..
   //
   //where 'addr' is:
   //
   //%         (entire file)
   //$  [+-NUM]
   //'x [+-NUM] (where x denotes a currently defined mark)
   //.  [+-NUM]
   //[+-NUM]..
   //NUM
   //
   //The invo.comm pointer is updated to point to the first character following the
   //range spec. If an initial address is found, but no second, the upper bound
   //is equal to the lower.

   // invo.addressKind for user commands is set by find_ucmd
   if (!IS_USER_COMMAND(invo.id)) {
      if (invo.id != COUNT_COMMANDS)
         invo.addressKind = commands[(int)invo.id].addressKind;
      else
         invo.addressKind = ADDR_LINES;

      // :wincmd range depends on the argument.
      if (invo.id == C_wincmd && p)
          getPortCommAddressType(skipwhite(p), &invo);
      if (invo.id == C_ll && isLocationListBook(curBook))
         invo.addressKind = ADDR_OTHER;
   }

   if (!may_have_range)
      invo.line1 = invo.line2 = default_address(&invo);
   ei (parse_cmd_address(&invo, OUT &errorMsg, false) == FAIL)
      goto doend;

   // 5. Parse the command.
   //Skip ':' and any white space
   invo.comm = skipwhite(invo.comm);
   while (*invo.comm == ':')
      invo.comm = skipwhite(invo.comm + 1);

   //If we got a line, but no command, then go to the line.
   //If we find a '|' or '\n' we set invo.nextcmd.
   if (*invo.comm == ZERO || isComment(invo.comm)
                || (invo.nextComm = check_nextcmd(invo.comm)) != NULL) {
      //strange vi behaviour:
      //":3"      jumps to line 3
      //":3|..."   prints line 3  (not in Vim9 script)
      //":|"      prints current line  (not in Vim9 script)
      if (invo.skip)       // skip this if inside :if
          goto doend;
      errorMsg = ex_range_without_command(&invo);
      goto doend;
   }

   // If this looks like an undefined user command and there are CmdUndefined
   // autocommands defined, trigger the matching autocommands.
   if (p && invo.id == COUNT_COMMANDS && !invo.skip
       && ASCII_ISUPPER(*invo.comm)
       && has_cmdundefined()
   ) {
      int ret;

      p = invo.comm;
      while (ASCII_ISALNUM(*p))
         ++p;
      p = copySubstr(invo.comm, p - invo.comm);
      ret = applyAutocomms(EVENT_CMDUNDEFINED, p, p, true, NULL);
      eeglFree(p);
      // If the autocommands did something and didn't cause an error, try
      // finding the command again.
      p = (ret && !aborting()) ? findCommand(&invo, NULL, NULL) : invo.comm;
   }

   if (!p) {
      if (!invo.skip)
         errorMsg = _(e_ambiguous_use_of_user_defined_command);
      goto doend;
   }
   // Check for wrong commands.
   if (*p == '!' && invo.comm[1] == 0151 && invo.comm[0] == 78 && !IS_USER_COMMAND(invo.id)) {
      errorMsg = uc_fun_cmd();
      goto doend;
   }

   if (invo.id == COUNT_COMMANDS) {
      if (!invo.skip) {
         STRCPY(IObuff, _(e_not_an_editor_command));
         if (!sourcing) {
            //If the modifier was parsed OK the error must be in the following command
            if (after_modifier)
               append_command(after_modifier);
            else
               append_command(*commline);
            did_append_cmd = true;
         }
         errorMsg = IObuff;
         anySyntaxEmsgS = true;
      }
      goto doend;
   }

   ni = (!IS_USER_COMMAND(invo.id) && (commands[invo.id].fn == c_ni
#ifdef HAVE_EX_SCRIPT_NI
        || commands[invo.id].fn == ex_script_ni
#endif
        ));

   // forced commands
   if (*p == '!' && invo.id != C_substitute) {
      ++p;
      invo.forceit = true;
   } else
      invo.forceit = false;

   // 6. Parse arguments.  Then check for errors.
   if (!IS_USER_COMMAND(invo.id))
      invo.argFlags = (long)commands[(int)invo.id].flags;

   if (!invo.skip) {
      if (!curBook->o.modifiable && (invo.argFlags & MODIFY)) {
          // Command not allowed in immutable buffers
          errorMsg = _(e_cannot_make_changes_modifiable_is_off);
          goto doend;
      }

      if (!IS_USER_COMMAND(invo.id)) {
         if (commPortTypeG != 0 && !(invo.argFlags & COMMPORT)) {
            //Command not allowed in the command line portal
            errorMsg = _(e_invalid_in_commline_portal);
            goto doend;
         }
         if (text_locked() && !(invo.argFlags & LOCK_OK)) {
            //Command not allowed when text is locked
            errorMsg = _(get_text_locked_msg());
            goto doend;
         }
      }

      //Disallow editing another buffer when "curBookLock" is set.
      //Do allow ":checktime" (it is postponed).
      //Do allow ":edit" (check for an argument later).
      //Do allow ":file" with no arguments (check for an argument later).
      if (!(invo.argFlags & (COMMPORT | LOCK_OK))
            && invo.id != C_checktime
            && invo.id != C_edit
            && invo.id != C_file
            && !IS_USER_COMMAND(invo.id)
            && curBookLocked()
      )
         goto doend;

      if (!ni && !(invo.argFlags & RANGE) && invo.addr_count > 0) {
          errorMsg = _(e_no_range_allowed);
          goto doend;
      }
   }

   if (!ni && !(invo.argFlags & BANG) && invo.forceit) {
      errorMsg = _(e_no_bang_allowed);
      goto doend;
   }

   //Don't complain about the range if it is not used
   //(could happen if line_count is accidentally set to 0).
   if (!invo.skip && !ni && (invo.argFlags & RANGE)) {
      //If the range is backwards, ask for confirmation and, if given, swap
      //invo.line1 & invo.line2 so it's forwards again.
      //When global command is busy, don't ask, will fail below.
      if (!global_busy && invo.line1 > invo.line2) {
         if (msg_silent == 0) {
            if (sourcing) {
               errorMsg = _(e_backwards_range_given);
               goto doend;
            }
            if (ask_yesno((CS)_("Backwards range given, OK to swap"), false) != 'y')
               goto doend;
         }
         lnum = invo.line1;
         invo.line1 = invo.line2;
         invo.line2 = lnum;
      }
      if ((errorMsg = invalid_range(&invo)) != NULL)
         goto doend;
   }

   if ((invo.addressKind == ADDR_OTHER) && invo.addr_count == 0)
      // default is 1, not cursor
      invo.line2 = 1;

   correct_range(&invo);

   if (((invo.argFlags & WHOLEFOLD) || invo.addr_count >= 2) && !global_busy
          && invo.addressKind == ADDR_LINES) {
      // Put the first line at the start of a closed fold, put the last line
      // at the end of a closed fold.
      (void)getFolds(invo.line1, OUT &invo.line1, NULL);
      (void)getFolds(invo.line2, NULL, OUT &invo.line2);
   }

   //For the ":make" and ":grep" commands we insert the 'makeprg'/'grepprg'
   //option here, so things like % get expanded.
   p = replaceMakeProgramName(&invo, OUT p, commline);
   if (!p)
      goto doend;

   //Skip to start of argument. Don't do this for the ":!" command, because ":!! -l" needs the space
   if (invo.id == C_bang)
      invo.arg = p;
   else
      invo.arg = skipwhite(p);

   // ":file" cannot be run with an argument when "curBookLock" is set
   if (invo.id == C_file && *invo.arg != ZERO && curBookLocked())
      goto doend;

   //Check for "++opt=val" argument. Must be first, allow ":w ++enc=utf8 !cmd"
   if (invo.argFlags & ARGOPT) {
      while (invo.arg[0] == '+' && invo.arg[1] == '+') {
         if (getargopt(&invo) == FAIL && !ni) {
            errorMsg = _(e_invalid_argument);
            goto doend;
         }
      } 
   } 

   if (invo.id == C_write || invo.id == C_update) {
      if (*invo.arg == '>') {        // append
         if (*++invo.arg != '>') {     // typed wrong
            errorMsg = _(e_use_w_or_w_gt_gt);
            goto doend;
         }
         invo.arg = skipwhite(invo.arg + 1);
         invo.append = true;
      } ei (*invo.arg == '!' && invo.id == C_write) { // :w !filter
         ++invo.arg;
         invo.usefilter = true;
      }
   }

   if (invo.id == C_read) {
      if (invo.forceit) {
         invo.usefilter = true;      // :r! filter if invo.forceit
         invo.forceit = false;
      } ei (*invo.arg == '!') {     // :r !filter
         ++invo.arg;
         invo.usefilter = true;
      }
   }

   if (invo.id == C_lshift || invo.id == C_rshift) {
      invo.amount = 1;
      while (*invo.arg == *invo.comm) {     // count number of '>' or '<'
          ++invo.arg;
          ++invo.amount;
      }
      invo.arg = skipwhite(invo.arg);
   }

   //Check for "+command" argument, before checking for next command.
   //Don't do this for ":read !cmd" and ":write !cmd".
   if ((invo.argFlags & CMDARG) && !invo.usefilter)
      invo.higherOrderComm = getargcmd(OUT &invo.arg);

   //For commands that do not use '|' inside their argument: Check for '|' to
   //separate commands and '//' to start comments.
   //
   //Otherwise: Check for <newline> to end a shell command.
   //Also do this for ":read !cmd", ":write !cmd" and ":global".
   //Also do this inside a { - } block after :command and :autocmd.
   //Any others?
   if ((invo.argFlags & TRLBAR) && !invo.usefilter) {
      separateNextCommand(&invo, false);
   } ei (invo.id == C_bang
       || invo.id == C_terminal
       || invo.id == C_global
       || invo.id == C_vglobal
       || invo.usefilter
       || inside_block(&invo)
    ) {
      for (p = invo.arg; *p; ++p) {
          // Remove one backslash before a newline
         if (*p == '\\' && p[1] == '\n')
            STRMOVE(p, p + 1);
         ei (*p == '\n' && !(invo.argFlags & EXPR_ARG)) {
            invo.nextComm = p + 1;
            *p = ZERO;
            break;
         }
      }
   }

   if ((invo.argFlags & DFLALL) && invo.addr_count == 0)
      address_default_all(&invo);

   // accept numbered register only when no count allowed (:put)
   if ((invo.argFlags & REGSTR)
          && *invo.arg != ZERO
             // Do not allow register = for user commands
          && (!IS_USER_COMMAND(invo.id) || *invo.arg != '=')
          && !((invo.argFlags & COUNT) && EE_ISDIGIT(*invo.arg))
   ) {
      if (valid_yank_reg(*invo.arg, (!IS_USER_COMMAND(invo.id)
                && invo.id != C_put && invo.id != C_iput))) {
         invo.regname = *invo.arg++;
         // for '=' register: accept the rest of the line as an expression
         if (invo.arg[-1] == '=' && invo.arg[0] != ZERO) {
            if (!invo.skip) {
               set_expr_line(copyStr(invo.arg), &invo);
               did_set_expr_line = true;
            }
            invo.arg += STRLEN(invo.arg);
         }
         invo.arg = skipwhite(invo.arg);
      }
   }

   //Check for a count.  When accepting a BUFNAME, don't use "123foo" as a
   //count, it's a buffer name.
   if ((invo.argFlags & COUNT) && EE_ISDIGIT(*invo.arg)
       && (!(invo.argFlags & BUFNAME) || *(p = skipdigits(invo.arg + 1)) == ZERO
                       || SPACE_OR_TAB(*p))) {
      n = parseLong_quoted(&invo.arg);
      invo.arg = skipwhite(invo.arg);
      if (n <= 0 && !ni && (invo.argFlags & ZERO_LINE_OK) == 0) {
          errorMsg = _(e_positive_count_required);
          goto doend;
      }
      if (invo.addressKind != ADDR_LINES) {  // e.g. :buffer 2, :sleep 3
          invo.line2 = n;
          if (invo.addr_count == 0)
         invo.addr_count = 1;
      } else {
         invo.line1 = invo.line2;
         if (invo.line2 >= LONG_MAX - (n - 1))
            invo.line2 = LONG_MAX;  // avoid overflow
         else
            invo.line2 += n - 1;
         ++invo.addr_count;
         if (invo.line2 > curBook->mem.lineCount) {
            showErrFmtMsg(
               e_line_number_out_of_range_nr_past_the_end, invo.line2 - curBook->mem.lineCount
            );
            invo.line2 = curBook->mem.lineCount;
         } 
      }
   }

   // Check for flags: 'l', 'p' and '#'.
   if ((invo.argFlags & FLAGS) != 0)
      get_flags(&invo);
      
   if (!ni && !(invo.argFlags & EXTRA) && *invo.arg != ZERO
        && !isComment(invo.arg) && (*invo.arg != '|' || (invo.argFlags & TRLBAR) == 0)
   ) {
      // no arguments allowed but there is something
      errorMsg = ex_errmsg(e_trailing_characters_str, invo.arg);
      goto doend;
   }

   if (!ni && (invo.argFlags & NEEDARG) && *invo.arg == ZERO) {
      errorMsg = _(e_argument_required);
      goto doend;
   }
//{{{ Skip
   //Skip the command when it's not going to be executed.
   //The commands like :if, :endif, etc. always need to be executed.
   //Also make an exception for commands that handle a trailing command themselves.
   if (invo.skip) {
      switch (invo.id) {
      // commands that need evaluation
      case C_while:
      case C_endwhile:
      case C_for:
      case C_endfor:
      case C_if:
      case C_elseif:
      case C_else:
      case C_endif:
      case C_try:
      case C_catch:
      case C_finally:
      case C_endtry:
           break;

      // Commands that handle '|' themselves.  Check: A command should
      // either have the TRLBAR flag, appear in this list or appear in
      // the list at ":help :bar".
      case C_aboveleft:
      case C_and:
      case C_belowright:
      case C_botright:
      case C_browse:
      case C_call:
      case C_confirm:
      case C_const:
      case C_delfunction:
      case C_djump:
      case C_dlist:
      case C_dsearch:
      case C_dsplit:
      case C_echo:
      case C_echoerr:
      case C_echomsg:
      case C_echon:
      case C_eval:
      case C_execute:
      case C_filter:
      case C_final:
      case C_help:
      case C_hide:
      case C_horizontal:
      case C_ijump:
      case C_ilist:
      case C_isearch:
      case C_isplit:
      case C_keepalt:
      case C_keepjumps:
      case C_keepmarks:
      case C_keeppatterns:
      case C_leftabove:
      case C_let:
      case C_lockmarks:
      case C_lockvar:
      case C_match:
      case C_noautocmd:
      case C_noswapfile:
      case C_psearch:
      case C_return:
      case C_rightbelow:
      case C_silent:
      case C_substitute:
      case C_syntax:
      case C_tab:
      case C_throw:
      case C_tilde:
      case C_topleft:
      case C_unlet:
      case C_unlockvar:
      case C_verbose:
      case C_vertical:
      case C_wincmd:
         break;

      default:      goto doend;
      }
    }
//}}}
   if ((invo.argFlags & XFILE) && expand_filename(&invo, OUT commline, OUT &errorMsg) == FAIL)
      goto doend;

   //Accept book name. Cannot be used at the same time with a book number. Don't do this for 
   //a user command.
   if ((invo.argFlags & BUFNAME) && *invo.arg != ZERO && invo.addr_count == 0
          && !IS_USER_COMMAND(invo.id)) {
      //:bdelete, :bwipeout and :bunload take several arguments, separated
      //by spaces: find next space (skipping over escaped characters).
      //The others take one argument: ignore trailing spaces.
      if (invo.id == C_bdelete || invo.id == C_bwipeout || invo.id == C_bunload)
         p = skiptowhite_esc(invo.arg);
      else {
         p = invo.arg + STRLEN(invo.arg);
         while (p > invo.arg && SPACE_OR_TAB(p[-1]))
            --p;
      }
      invo.line2 = booklistFindPattern(invo.arg, p, (invo.argFlags & BUFUNL) != 0,
                           false, false);
      if (invo.line2 < 0)       // failed
          goto doend;
      invo.addr_count = 1;
      invo.arg = skipwhite(p);
   }

   // The :try command saves the emsg_silent flag, reset it here when
   // ":silent! try" was used, it should only apply to :try itself.
   if (invo.id == C_try && commModifierG.cmod_did_esilent > 0) {
      emsg_silent -= commModifierG.cmod_did_esilent;
      if (emsg_silent < 0)
          emsg_silent = 0;
      commModifierG.cmod_did_esilent = 0;
   }

   //7. Execute the command.
   if (IS_USER_COMMAND(invo.id)) {
      //Execute a user-defined command.
      do_ucmd(&invo);
   } else {
      //Call the function to execute the builtin command.
      (commands[invo.id].fn)(&invo);
      if (invo.errmsg)
         errorMsg = invo.errmsg;
   }

   // Set flag that any command was executed, used by ex_vim9script().
   // Not if this was a command that wasn't executed or :endif.
   if (sourcing_a_script(&invo)
       && scriptPosG.sid > 0
       && invo.id != C_endif
       && (cstack->ind < 0
          || (cstack->flags[cstack->ind] & CSF_ACTIVE))
   )
      SCRIPT_ITEM(scriptPosG.sid)->sn_state = SN_STATE_HAD_COMMAND;

   //If the command just executed called doCommand(), any throw or ":return"
   //or ":finish" encountered there must also check the cstack of the still
   //active doCommand() that called this doOneCommand().  Rethrow an uncaught
   //exception, or reanimate a returned function or finished script file and
   //return or finish it again.
   if (need_rethrow)
      do_throw(cstack);
   ei (check_cstack) {
      if (sourceFileIsFinished(fgetline, cookie))
         do_finish(&invo, true);
      ei (getline_equal(fgetline, cookie, get_func_line)  && current_func_returned())
         do_return(&invo, true, false, NULL);
   }
   need_rethrow = check_cstack = false;

doend:
   if (curPor->cursor.lnum == 0) {  // can happen with zero line number
      curPor->cursor.lnum = 1;
      curPor->cursor.col = 0;
   }

   if (errorMsg && *errorMsg != ZERO && !anyEmsgG) {
      if ((sourcing || !keyWasTypedG) && !did_append_cmd) {
          if (errorMsg != IObuff) {
             STRCPY(IObuff, errorMsg);
             errorMsg = IObuff;
          }
          append_command(*commline);
      }
      emsg(errorMsg);
   }
   do_errthrow(cstack,
       (invo.id != COUNT_COMMANDS && !IS_USER_COMMAND(invo.id))
         ? commands[(int)invo.id].name : (CS)NULL);

   if (did_set_expr_line)
      set_expr_line(NULL, NULL);

   undoCommModifier(&commModifierG);
   commModifierG = saveCommModifier;
   reg_executing = save_reg_executing;
   pending_end_reg_executing = save_pending_end_reg_executing;

   if (invo.nextComm && *invo.nextComm == ZERO)   // not really a next command
      invo.nextComm = NULL;

   --ex_nesting_level;
   eeglFree(invo.commlineToFree);

   return invo.nextComm;
}

//}}}
//{{{command parsin'

private Byte ex_error_buf[MSG_BUF_LEN];

//Return an error message with argument included. Use a static buffer, only the last error will be 
//kept. "msg" will be translated, caller should use N_().
CS
ex_errmsg(CS msg, CS arg) {
   eeSnprintf(ex_error_buf, MSG_BUF_LEN, _(msg), arg);
   return ex_error_buf;
}

//The "+" string used in place of an empty command in Ex mode.
//This string is used in pointer comparison.
private char exmode_plus[] = "+";

// Handle a range without a command. Returns an error message on failure.
CS
ex_range_without_command(Invocation* invo) {
   CS errorMsg = NULL;

   if (*invo->comm == '|') {
      invo->id = C_print;
      invo->argFlags = RANGE+COUNT+TRLBAR;
      if ((errorMsg = invalid_range(invo)) == NULL) {
          correct_range(invo);
          c_print(invo);
      }
   } ei (invo->addr_count != 0) {
      if (invo->line2 > curBook->mem.lineCount) {
          // A line number past the file is put at the end of the file.
         invo->line2 = curBook->mem.lineCount;
      }

      if (invo->line2 < 0)
          errorMsg = _(e_invalid_range);
      else {
          if (invo->line2 == 0)
         curPor->cursor.lnum = 1;
          else
         curPor->cursor.lnum = invo->line2;
          beginline(BL_SOL | BL_FIX);
      }
   }
   return errorMsg;
}

//Check for a command with optional tail.
//If there is a match advance "pp" to the argument and return true.
//If "noparen" is true do not recognize the command followed by "(" or ".".
private int
checkforcmd_opt(
   OUT CS* pp,      // start of command
   CS cmd,      // name of command
   int len,      // required length
   int noparen
) {
   int i;
   for (i = 0; cmd[i] != ZERO; ++i) {
      if (((CS)cmd)[i] != (*pp)[i])
         break;
   } 
   if (i >= len && !ASCII_ISALPHA((*pp)[i]) && (*pp)[i] != '_'
          && (!noparen || ((*pp)[i] != '(' && (*pp)[i] != '.'))) {
      *pp = skipwhite(*pp + i);
      return true;
   }
   return false;
}

//Check for a command with optional tail.
//If there is a match advance "pp" to the argument and return true.
int
checkforcmd(
   OUT CS* pp,      // start of command
   CS cmd,      // name of command
   int      len
) {      // required length
   return checkforcmd_opt(OUT pp, cmd, len, false);
}

//Check for a command with optional tail, not followed by "(" or ".".
//If there is a match advance "pp" to the argument and return true.
int
checkforcmd_noparen(
    OUT CS* pp,      // start of command
    CS cmd,      // name of command
    int len      // required length
){
   return checkforcmd_opt(pp, cmd, len, true);
}

//Parse and skip over command modifiers:
//- update invo->comm
//- store flags in "cmod".
//- Set ex_pressedreturn for an empty command line.
//When "skip_only" is true the global variables are not changed, except for "commModifierG".
//When "skip_only" is false then undoCommModifier() must be called later to free any 
//cmod_filter_regmatch.regprog.
//Call applyCommModifiers() to get the side effects of the modifiers:
//- set p_verbose for ":verbose"
//- set msg_silent for ":silent"
//- set 'eventignore' to "all" for ":noautocmd"
//Return FAIL when the command is not to be executed. May set "errorMsg" to an error message.
int
parse_command_modifiers(
   Invocation* invo,
   OUT CS* errorMsg,
   CommandModifier* cmod,
   int skip_only
){
   CS orig_cmd = invo->comm;
   CS cmd_start = NULL;
   int use_plus_cmd = false;
   int has_visual_range = false;

   CLEAR_POINTER(cmod);
   cmod->cmod_flags = stickyCommandModifiersG;

   if (STRNCMP(invo->comm, "'<,'>", 5) == 0) {
      //The automatically inserted Visual area range is skipped, so that
      //typing ":commModifierG cmd" in Visual mode works without having to move the
      //range to after the modifiers. The command will be "'<,'>commModifierG cmd",
      //parse "commModifierG cmd" and then put back "'<,'>" before "cmd" below.
      invo->comm += 5;
      cmd_start = invo->comm;
      has_visual_range = true;
   }

   //Repeat until no more command modifiers are found.
   for (;;) {
      while (*invo->comm == ' ' || *invo->comm == '\t' || *invo->comm == ':') {
         ++invo->comm;
      }

      //ignore comment and empty lines
      if (isComment(invo->comm)) {
         //a comment ends at a NL
         invo->nextComm = firstOccurrence(invo->comm, '\n');
         if (invo->nextComm)
            ++invo->nextComm;
         return FAIL;
      }
      if (*invo->comm == '\n') {
         invo->nextComm = invo->comm + 1;
         return FAIL;
      }
      if (*invo->comm == ZERO) {
         if (!skip_only) {
            ex_pressedreturn = true;
         }
         return FAIL;
      }

      CS p = skip_range(invo->comm, true, NULL);

      switch (*p) {
      // When adding an entry, also modify modeInfoTable[].
      case 'a':   
         if (!checkforcmd_noparen(&invo->comm, S"aboveleft", 3))
           break;
        cmod->cmod_split |= WSP_ABOVE;
        continue;

      case 'b':
         if (checkforcmd_noparen(&invo->comm, S"belowright", 3)) {
            cmod->cmod_split |= WSP_BELOW;
            continue;
         }
         if (checkforcmd_opt(OUT &invo->comm, S"browse", 3, true)) {
            cmod->cmod_flags |= CMOD_BROWSE;
            continue;
         }
         if (!checkforcmd_noparen(&invo->comm, S"botright", 2))
            break;
         cmod->cmod_split |= WSP_BOT;
         continue;

      case 'c':   
        if (!checkforcmd_opt(OUT &invo->comm, S"confirm", 4, true))
           break;
        cmod->cmod_flags |= CMOD_CONFIRM;
        continue;

      case 'k':   
        if (checkforcmd_noparen(&invo->comm, S"keepmarks", 3)) {
           cmod->cmod_flags |= CMOD_KEEPMARKS;
           continue;
        }
        if (checkforcmd_noparen(&invo->comm, S"keepalt", 5)) {
           cmod->cmod_flags |= CMOD_KEEPALT;
           continue;
        }
        if (checkforcmd_noparen(&invo->comm, S"keeppatterns", 5)) {
           cmod->cmod_flags |= CMOD_KEEPPATTERNS;
           continue;
        }
        if (!checkforcmd_noparen(&invo->comm, S"keepjumps", 5))
           break;
        cmod->cmod_flags |= CMOD_KEEPJUMPS;
        continue;

      case 'f': {   // only accept ":filter {pat} cmd"
         CS reg_pat;
         CS nulp = NULL;
         int       c = 0;

         if (!checkforcmd_noparen(&p, S"filter", 4)
            || *p == ZERO
            || endsComm(p)
         )
            // in ":filter #pat# cmd" # does not start a comment
            break;
         if (*p == '!') {
            cmod->cmod_filter_force = true;
            p = skipwhite(p + 1);
            if (*p == ZERO || endsComm(p))
               break;
         }
         if (skip_only)
            p = skipEeglGrepPat(p, NULL, NULL);
         else
            // NOTE: This puts a ZERO after the pattern.
            p = skipEeglGrepPat_ext(p, &reg_pat, NULL, &nulp, &c);
         if (p == NULL || *p == ZERO)
            break;
         if (!skip_only) {
            cmod->cmod_filter_regmatch.regprog = compileRegexp(reg_pat, RE_MAGIC);
            if (cmod->cmod_filter_regmatch.regprog == NULL)
               break;
            // restore the character overwritten by ZERO
            if (nulp)
               *nulp = c;
         }
         invo->comm = p;
         continue;
      }

      case 'h':   
         if (checkforcmd_noparen(&invo->comm, S"horizontal", 3)) {
            cmod->cmod_split |= WSP_HOR;
            continue;
         }
         // ":hide" and ":hide | cmd" are not modifiers
         if (p != invo->comm || !checkforcmd_noparen(&p, S"hide", 3)
                     || *p == ZERO || endsComm(p))
            break;
         invo->comm = p;
         cmod->cmod_flags |= CMOD_HIDE;
         continue;

      case 'l':   
         if (checkforcmd_noparen(&invo->comm, S"lockmarks", 3)) {
            cmod->cmod_flags |= CMOD_LOCKMARKS;
            continue;
         }

        if (!checkforcmd_noparen(&invo->comm, S"leftabove", 5))
           break;
        cmod->cmod_split |= WSP_ABOVE;
        continue;

      case 'n':   
        if (checkforcmd_noparen(&invo->comm, S"noautocmd", 3)) {
           cmod->cmod_flags |= CMOD_NOAUTOCMD;
           continue;
        }
        if (!checkforcmd_noparen(&invo->comm, S"noswapfile", 3))
           break;
        cmod->cmod_flags |= CMOD_NOSWAPFILE;
        continue;

      case 'r':   
        if (!checkforcmd_noparen(&invo->comm, S"rightbelow", 6))
           break;
        cmod->cmod_split |= WSP_BELOW;
        continue;

      case 's':   
        if (!checkforcmd_noparen(&invo->comm, S"silent", 3))
            break;
        cmod->cmod_flags |= CMOD_SILENT;
        if (*invo->comm == '!' && !SPACE_OR_TAB(invo->comm[-1])) {
            // ":silent!", but not "silent !cmd"
            invo->comm = skipwhite(invo->comm + 1);
            cmod->cmod_flags |= CMOD_ERRSILENT;
        }
        continue;

      case 't':   
         if (checkforcmd_noparen(&p, S"tab", 3)) {
            if (!skip_only) {
               long tabnr = doGetCommandAddress(invo, &invo->comm,
                        ADDR_TABS, invo->skip,
                        skip_only, false, 1);
               if (tabnr == MAXLNUM)
                  cmod->cmod_tab = indexOfTab(curtab) + 1;
               else {
                  if (tabnr < 0 || tabnr > LAST_TAB_NR) {
                     *errorMsg = _(e_invalid_range);
                     return FAIL;
                  }
                  cmod->cmod_tab = tabnr + 1;
              }
            }
            invo->comm = p;
            continue;
         }
         if (!checkforcmd_noparen(&invo->comm, S"topleft", 2))
            break;
         cmod->cmod_split |= WSP_TOP;
         continue;

      case 'u':   
         if (!checkforcmd_noparen(&invo->comm, S"unsilent", 3))
            break;
         cmod->cmod_flags |= CMOD_UNSILENT;
         continue;

      case 'v':   
         if (checkforcmd_noparen(&invo->comm, S"vertical", 4)) {
            cmod->cmod_split |= WSP_VERT;
            continue;
         }
         if (!checkforcmd_noparen(&p, S"verbose", 4))
            break;
         if (eeIsDigit(*invo->comm)) {
            // zero means not set, one is verbose == 0, etc.
            cmod->cmod_verbose = atoi((char *)invo->comm) + 1;
         } else
            cmod->cmod_verbose = 2;  // default: verbose == 1
         invo->comm = p;
         continue;
      }
      break;
   }

   if (has_visual_range) {
      if (invo->comm > cmd_start) {
         // Move the '<,'> range to after the modifiers and insert a colon. Since the modifiers 
         // have been parsed put the colon on top of the space: "'<,'>mod cmd" -> "mod:'<,'>cmd
         // Put invo->comm after the colon.
         if (use_plus_cmd) {
            Unt len = STRLEN(cmd_start);

            // Special case: empty command uses "+":
            //  "'<,'>mods" -> "mods *+
            //  Use "*" instead of "'<,'>" to avoid the command getting
            //  longer, in case it was allocated.
            mch_memmove(orig_cmd, cmd_start, len);
            STRCPY(orig_cmd + len, " *+");
         } else {
            mch_memmove(cmd_start - 5, cmd_start, invo->comm - cmd_start);
            invo->comm -= 5;
            mch_memmove(invo->comm - 1, ":'<,'>", 6);
         }
      } else
         // No modifiers, move the pointer back. Special case: change empty command to "+".
         if (use_plus_cmd)
            invo->comm = (CS)"'<,'>+";
         else
            invo->comm = orig_cmd;
   } ei (use_plus_cmd)
     invo->comm = (CS)exmode_plus;

   return OK;
}

//Apply the command modifiers. Save current state into commModifierG, call undoCommModifier() later
void
applyCommModifiers(CommandModifier* cmod) {
   if (cmod->cmod_verbose > 0) {
      if (cmod->cmod_verbose_save == 0)
          cmod->cmod_verbose_save = p_verbose + 1;
      p_verbose = cmod->cmod_verbose - 1;
   }

   if ((cmod->cmod_flags & (CMOD_SILENT | CMOD_UNSILENT)) && cmod->cmod_save_msg_silent == 0) {
      cmod->cmod_save_msg_silent = msg_silent + 1;
      cmod->cmod_save_msg_scroll = msg_scroll;
   }
   if (cmod->cmod_flags & CMOD_SILENT)
      ++msg_silent;
   if (cmod->cmod_flags & CMOD_UNSILENT)
      msg_silent = 0;

   if (cmod->cmod_flags & CMOD_ERRSILENT) {
      ++emsg_silent;
      ++cmod->cmod_did_esilent;
   }

   if ((cmod->cmod_flags & CMOD_NOAUTOCMD) && cmod->cmod_save_ei == NULL) {
      // Set @eventignore to "all".
      // First save the existing option value for restoring it later.
      cmod->cmod_save_ei = p_ei ? copyStr(p_ei) : null;
      optChangeStringOptionDirect(S"eventignore", S"all", 0, SID_NONE);
   }
}

//Undo and free contents of "cmod".
void
undoCommModifier(CommandModifier *cmod) {
   if (cmod->cmod_verbose_save > 0) {
      p_verbose = cmod->cmod_verbose_save - 1;
      cmod->cmod_verbose_save = 0;
   }

   if (cmod->cmod_save_ei) {
      // Restore 'eventignore' to the value before ":noautocmd".
      optChangeStringOptionDirect(S"eventignore", cmod->cmod_save_ei, 0, SID_NONE);
      cmod->cmod_save_ei = NULL;
   }

   eeRegFree(cmod->cmod_filter_regmatch.regprog);

   if (cmod->cmod_save_msg_silent > 0) {
      // messages could be enabled for a serious error, need to check if the
      // counters don't become negative
      if (!anyEmsgG || msg_silent > cmod->cmod_save_msg_silent - 1)
          msg_silent = cmod->cmod_save_msg_silent - 1;
      emsg_silent -= cmod->cmod_did_esilent;
      if (emsg_silent < 0)
          emsg_silent = 0;
      // Restore msg_scroll, it's set by file I/O commands, even when no
      // message is actually displayed.
      msg_scroll = cmod->cmod_save_msg_scroll;

      // "silent reg" or "silent echo x" inside "redir" leaves msgColG
      // somewhere in the line.  Put it back in the first column.
      if (redirecting())
          msgColG = 0;

      cmod->cmod_save_msg_silent = 0;
      cmod->cmod_did_esilent = 0;
   }
}

//Parse the address range, if any, in "invo". May set the last search pattern, unless "silent" 
//is true. Return FAIL and set "errorMsg" or return OK.
int
parse_cmd_address(Invocation* invo, CS* errorMsg, int silent) {
   int      address_count = 1;
   LineNr   lnum;
   int      need_check_cursor = false;
   int      ret = FAIL;

   // Repeat for all ',' or ';' separated addresses.
   for (;;) {
      invo->line1 = invo->line2;
      invo->line2 = default_address(invo);
      invo->comm = skipwhite(invo->comm);
      lnum = doGetCommandAddress(invo, &invo->comm, invo->addressKind, invo->skip, silent,
                  invo->addr_count == 0, address_count++);
      if (invo->comm == NULL)   // error detected
          goto theend;
      if (lnum == MAXLNUM) {
         if (*invo->comm == '%') {  // '%' - all lines
            ++invo->comm;
            switch (invo->addressKind) {
              case ADDR_LINES:
              case ADDR_OTHER:
                 invo->line1 = 1;
                 invo->line2 = curBook->mem.lineCount;
                 break;
              case ADDR_LOADED_BUFFERS: {
                  Book   *book = firstBook;

                  while (book->next && book->mem.mfile == NULL)
                     book = book->next;
                  invo->line1 = book->fiNum;
                  book = lastBook;
                  while (book->prev && book->mem.mfile == NULL)
                     book = book->prev;
                  invo->line2 = book->fiNum;
                  break;
               }
               case ADDR_BUFFERS:
                  invo->line1 = firstBook->fiNum;
                  invo->line2 = lastBook->fiNum;
                  break;
               case ADDR_PORTALS:
               case ADDR_TABS:
                  if (IS_USER_COMMAND(invo->id)) {
                      invo->line1 = 1;
                      invo->line2 = invo->addressKind == ADDR_PORTALS ? LAST_WIN_NR : LAST_TAB_NR;
                  } else {
                      // there is no Vim command which uses '%' and ADDR_PORTALS or ADDR_TABS
                      *errorMsg = _(e_invalid_range);
                      goto theend;
                  }
                  break;
               case ADDR_TABS_RELATIVE:
                case ADDR_UNSIGNED:
                case ADDR_QUICKFIX:
               *errorMsg = _(e_invalid_range);
               goto theend;
                case ADDR_ARGUMENTS:
               if (ARGCOUNT == 0)
                   invo->line1 = invo->line2 = 0;
               else
               {
                   invo->line1 = 1;
                   invo->line2 = ARGCOUNT;
               }
               break;
                case ADDR_QUICKFIX_VALID:
               invo->line1 = 1;
               invo->line2 = llGetValidSize(invo);
               if (invo->line2 == 0)
                   invo->line2 = 1;
               break;
                case ADDR_NONE:
               // Will give an error later if a range is found.
               break;
            }
            ++invo->addr_count;
          } ei (*invo->comm == '*') {
               Pos       *fp;

               // '*' - visual area
               if (invo->addressKind != ADDR_LINES) {
                   *errorMsg = _(e_invalid_range);
                   goto theend;
               }

               ++invo->comm;
               if (!invo->skip) {
                   fp = getmark('<', false);
                   if (check_mark(fp) == FAIL)
                      goto theend;
                   invo->line1 = fp->lnum;
                   fp = getmark('>', false);
                   if (check_mark(fp) == FAIL)
                      goto theend;
                   invo->line2 = fp->lnum;
                   ++invo->addr_count;
               }
          }
      }
      else
          invo->line2 = lnum;
      invo->addr_count++;

      if (*invo->comm == ';')
      {
          if (!invo->skip)
          {
         curPor->cursor.lnum = invo->line2;

         // Don't leave the cursor on an illegal line or column, but do
         // accept zero as address, so 0;/PATTERN/ works correctly
         // (where zero usually means to use the first line).
         // Check the cursor position before returning.
         if (invo->line2 > 0)
             check_cursor();
         else
             check_cursor_col();
         need_check_cursor = true;
          }
      } ei (*invo->comm != ',')
          break;
      ++invo->comm;
   }

   // One address given: set start and end lines.
   if (invo->addr_count == 1) {
      invo->line1 = invo->line2;
      // ... but only implicit: really no address given
      if (lnum == MAXLNUM)
         invo->addr_count = 0;
   }
   ret = OK;

theend:
   if (need_check_cursor)
      check_cursor();
   return ret;
}

//Append "cmd" to the error message in IObuff.
//Take care of limiting the length and handling 0xa0, which would be invisible otherwise.
private void
append_command(CS cmd) {
   Unt  len = STRLEN(IObuff);
   CS s = cmd;
   CS d;

   if (len > IOSIZE - 100) {
      // Not enough space, truncate and put in "...".
      d = IObuff + IOSIZE - 100;
      d -= mb_head_off(IObuff, d);
      STRCPY(d, "...");
   }
   STRCAT(IObuff, ": ");
   d = IObuff + STRLEN(IObuff);
   while (*s != ZERO && d - IObuff + 5 < IOSIZE) {
      if (s[0] == 0xc2 && s[1] == 0xa0) {
         s += 2;
         STRCPY(d, "<a0>");
         d += 4;
      } ei (d - IObuff + utfCharLen(s) + 1 >= IOSIZE)
         break;
      else
         MB_COPY_CHAR(s, d);
   }
   *d = ZERO;
}

//If "start" points "&opt", "&l:opt", "&g:opt" or "$ENV" return a pointer to
//the name.  Otherwise just return "start".
CS
skip_option_env_lead(CS start) {
   CS name = start;
   if (*start == '&') {
      if ((start[1] == 'l' || start[1] == 'g') && start[2] == ':')
         name += 3;
      else
         name += 1;
   } ei (*start == '$')
      name += 1;
   return name;
}

//Return true and set "*idx" if "p" points to a one letter command.
//- The 'k' command can directly be followed by any character
//      but :keepa[lt] is another command, as are :keepj[umps],
//      :kee[pmarks] and :keepp[atterns].
//- The 's' command can be followed directly by 'c', 'g', 'i', 'I' or 'r'
//      but :sre[wind] is another command, as are :scr[iptnames],
//      :scs[cope], :sim[alt], :sig[ns] and :sil[ent].
private int
oneLetterCommand(CS p, OUT CommIndex *idx) {
   if (p[0] == 'k'  && (p[1] != 'e' || (p[1] == 'e' && p[2] != 'e'))) {
      *idx = C_k;
      return true;
   }
   if (p[0] == 's'
          && ((p[1] == 'c' && (p[2] == ZERO || (p[2] != 's' && p[2] != 'r'
               && (p[3] == ZERO || (p[3] != 'i' && p[4] != 'p')))))
             || p[1] == 'g'
             || (p[1] == 'i' && p[2] != 'm' && p[2] != 'l' && p[2] != 'g')
             || p[1] == 'I'
             || (p[1] == 'r' && p[2] != 'e'))
   ) {
      *idx = C_substitute;
      return true;
   }
   return false;
}

//true if "cmd" starts with "123->", a number followed by a method call.
int
number_method(CS cmd) {
   CS p = skipdigits(cmd);
   return p > cmd && (p = skipwhite(p))[0] == '-' && p[1] == '>';
}

//Find a Command by its name, either built-in or user. Start of the name can be found at 
//invo->comm. Set invo->id and return a pointer to char after the command name.
//"full" is set to true if the whole command name matched.
//
//If "lookup" is not NULL recognize expression without "eval" or "call" and assignment without 
//"let".  Sets invo->id to the command while returning "invo->comm".
//
//Return NULL for an ambiguous user command.
CS
findCommand(Invocation* invo, int* full, int (*lookup)(CS, Unt, int cmd)) {
   int len;
   int i;

   CS p = invo->comm;
   if (lookup) {
      CS pskip = skip_option_env_lead(invo->comm);

      if (firstOccurrence((CS)"{('[\"@&$", *p) != NULL
            || ((p = to_name_const_end(pskip)) > invo->comm && *p != ZERO)
            || (p[0] == '0' && p[1] == 'z')
      ) {
         if (*invo->comm == '&'
             || (invo->comm[0] == '$' && invo->comm[1] != '\'' && !isComment(invo->comm))
             || (invo->comm[0] == '@'
                  && (valid_yank_reg(invo->comm[1], false) || invo->comm[1] == '@'))
         ){
            if (*invo->comm == '&') {
               p = invo->comm + 1;
               if (STRNCMP("l:", p, 2) == 0 || STRNCMP("g:", p, 2) == 0)
                  p += 2;
               p = toNameEnd(p, false);
            } ei (*invo->comm == '$') {
               p = toNameEnd(invo->comm + 1, false);
            } else {
               p = invo->comm + 2;
            }
            if (endsComm(skipwhite(p))) {
               // "&option <NL>", "$ENV <NL>" and "@r <NL>" are the start of an expression.
               invo->id = C_eval;
               return invo->comm;
            }
            // "&option" can be followed by "->" or "=", check below
         }

         CS swp = skipwhite(p);
         if (
            // "(..." is an expression. "funcname(" is always a function call.
            *p == '('
             || (p == invo->comm
            ? (
                // "{..." is a dict expression or block start.
                *invo->comm == '{'
                // "'string'->func()" is an expression.
             || *invo->comm == '\''
                // '"string"->func()' is an expression.
             || isComment(invo->comm)
                // '$"string"->func()' is an expression.
                // "$'string'->func()" is an expression.
             || (invo->comm[0] == '$' && (invo->comm[1] == '\'' || isComment(invo->comm)))
                // '0z1234->func()' is an expression.
             || (invo->comm[0] == '0' && invo->comm[1] == 'z')
                // "g:varname" is an expression.
             || invo->comm[1] == ':')
                // "varname->func()" is an expression.
            : (*swp == '-' && swp[1] == '>'))
         ) {
            if (*invo->comm == '{' && endsComm(skipwhite(invo->comm + 1))) {
               // "{" by itself is the start of a block.
               invo->id = C_block;
               return invo->comm + 1;
            }
            invo->id = C_eval;
            return invo->comm;
         }

         if ((p != invo->comm && (
                  // "varname[]" is an expression.
                  *p == '['
                  // "varname.key" is an expression.
                  || (*p == '.' && (ASCII_ISALPHA(p[1]) || p[1] == '_'))
                )
             )
             // g:[key] is an expression
             || STRNCMP(invo->comm, "g:[", 3) == 0
         ){
            // When followed by "=" or "+=" then it is an assignment. Skip over the whole thing, 
            // which can be:
            //   name.member = val
            //   name[a : b] = val
            //   name[idx] = val
            //   name[idx].member = val
            //   etc.
            invo->id = C_eval;
            return invo->comm;
         }

         // "[...]->Method()" is a list expression, but "[a, b] = Func()" is an assignment.
         // If there is no line break inside the "[...]" then "p" is
         // advanced to after the right bracket by to_name_const_end(): check if a "=" follows.
         // If "[...]" has a line break "p" still points at the left bracket and it
         // can't be an assignment.
         if (*invo->comm == '[') {
            p = to_name_const_end(invo->comm);
            if (p == invo->comm && *p == '[') {
               int count = 0;
               int   semicolon = false;

               p = skip_var_list(invo->comm, &count, &semicolon, true);
            }
            CS eq = p;
            if (eq) {
                eq = skipwhite(eq);
               if (firstOccurrence((CS)"+-*/%.", *eq) != NULL) {
                  if (eq[0] == '.' && eq[1] == '.')
                      ++eq;
                  ++eq;
               }
            }
            if (p == NULL || p == invo->comm || *eq != '=') {
                invo->id = C_eval;
                return invo->comm;
            }
         }
      }

      // 1234->func() is a method call
      if (number_method(invo->comm)) {
         invo->id = C_eval;
         return invo->comm;
      }

      // "g:", "s:" and "l:" are always assumed to be a variable, thus start
      // an expression. A global/substitute/list command needs to use a longer name.
      if (firstOccurrence(S"gsl", *p) != NULL && p[1] == ':') {
          invo->id = C_eval;
          return invo->comm;
      }

      // If it is an ID it might be a variable with an operator on the next
      // line, if the variable exists it can't be a command.
      if (p > invo->comm && endsComm(skipwhite(p))
            && (lookup(invo->comm, p - invo->comm, true) == OK
                || (ASCII_ISALPHA(invo->comm[0]) && invo->comm[1] == ':'))) {
          invo->id = C_eval;
          return invo->comm;
      }
   }

   // Isolate the command and search for it in the command table.
   p = invo->comm;
   if (oneLetterCommand(p, OUT &invo->id)) {
      ++p;
      if (full)
         *full = true;
   } else {
      while (ASCII_ISALPHA(*p))
         ++p;
          
      // check for non-alpha command
      if (p == invo->comm && firstOccurrence((CS)"@*!=><&~#}", *p) != NULL)
         ++p;
      len = (int)(p - invo->comm);
      // The "d" command can directly be followed by 'l' or 'p' flag
      if (*invo->comm == 'd' && (p[-1] == 'l' || p[-1] == 'p')) {
          // Check for ":dl", ":dell", etc. to ":deletel": that's
          // :delete with the 'l' flag.  Same for 'p'.
          for (i = 0; i < len; ++i)
         if (invo->comm[i] != ((CS)"delete")[i])
             break;
         if (i == len - 1) {
            --len;
            if (p[-1] == 'l')
               invo->flags |= EXFLAG_LIST;
            else
               invo->flags |= EXFLAG_PRINT;
         }
      }

      if (ASCII_ISLOWER(invo->comm[0])) {
         int c1 = invo->comm[0];
         int c2 = len == 1 ? ZERO : invo->comm[1];

         if (generatedCommandCount != (int)COUNT_COMMANDS) {
            lo("Generated command count = %d but COUNT_COMMANDS = %d", 
                  generatedCommandCount, (int)COUNT_COMMANDS
            );
            internalErrMsg(e_command_table_needs_to_be_updated_run_make_ids);
            exitEegl(1);
         }

         // Use a precomputed index for fast look-up in commands[]
         // taking into account the first 2 letters of invo->comm.
         invo->id = commandIndices0[c1 - 'a'];
         if (ASCII_ISLOWER(c2))
            invo->id += commandIndices1[c1 - 'a'][c2 - 'a'];
      }
      ei (ASCII_ISUPPER(invo->comm[0]))
          invo->id = C_Next;
      else
          invo->id = C_bang;

      for ( ; (int)invo->id < (int)COUNT_COMMANDS; invo->id = (CommIndex)((int)invo->id + 1)) {
         if (STRNCMP(commands[(int)invo->id].name, (char *)invo->comm, (Unt)len) == 0) {
            if (full && commands[invo->id].name[len] == ZERO)
               *full = true;
            break;
         }
      } 

      // Do not recognize ":*" as the star command 
      if (invo->id == C_star)
         p = invo->comm;

      // Look for a user defined command as a last resort.  Let ":Print" be
      // overruled by a user defined command.
      if ((invo->id == COUNT_COMMANDS || invo->id == C_Print)
            && *invo->comm >= 'A' && *invo->comm <= 'Z') {
         // User defined commands may contain digits.
         while (ASCII_ISALNUM(*p))
            ++p;
         p = find_ucmd(invo, p, full, NULL, NULL);
      }
      if (!p || p == invo->comm)
         invo->id = COUNT_COMMANDS;
   }

   // ":fina" means ":finally"
   if (invo->id == C_final && p - invo->comm == 4)
      invo->id = C_finally;

   return p;
}

typedef struct {
   char   *name;
   int      minlen;
   int      has_count;  // :123verbose  :3tab
} CommModeInfo;

private CommModeInfo modeInfoTable[] = {
   {"aboveleft", 3, false},
   {"belowright", 3, false},
   {"botright", 2, false},
   {"browse", 3, false},
   {"confirm", 4, false},
   {"filter", 4, false},
   {"hide", 3, false},
   {"horizontal", 3, false},
   {"keepalt", 5, false},
   {"keepjumps", 5, false},
   {"keepmarks", 3, false},
   {"keeppatterns", 5, false},
   {"leftabove", 5, false},
   {"legacy", 3, false},
   {"lockmarks", 3, false},
   {"noautocmd", 3, false},
   {"noswapfile", 3, false},
   {"rightbelow", 6, false},
   {"silent", 3, false},
   {"tab", 3, true},
   {"topleft", 2, false},
   {"unsilent", 3, false},
   {"verbose", 4, true},
   {"vertical", 4, false}
};   // modeInfoTable

//Return length of a command modifier (including optional count). Return 0 when it's not a modifier.
int
modifier_len(CS cmd) {
   CS p = cmd;
   if (EE_ISDIGIT(*cmd))
      p = skipwhite(skipdigits(cmd + 1));
   for (int i = 0; i < (int)ARRAY_LENGTH(modeInfoTable); ++i) {
      int j = 0;
      for (; p[j] != ZERO; ++j) {
         if (p[j] != modeInfoTable[i].name[j])
            break;
      } 
      if (!ASCII_ISALPHA(p[j]) && j >= modeInfoTable[i].minlen
                  && (p == cmd || modeInfoTable[i].has_count))
         return j + (int)(p - cmd);
   }
   return 0;
}

//Return > 0 if the command "name" exists.
//Return 2 if there is an exact match.
//Return 3 if there is an ambiguous match.
int
cmd_exists(CS name) {
   int full = false;

   // Check command modifiers.
   for (int i = 0; i < (int)ARRAY_LENGTH(modeInfoTable); ++i) {
      int j = 0;
      for (; name[j] != ZERO; ++j) {
         if (name[j] != modeInfoTable[i].name[j])
            break;
      } 
      if (name[j] == ZERO && j >= modeInfoTable[i].minlen)
         return (modeInfoTable[i].name[j] == ZERO ? 2 : 1);
   }

   // Check built-in commands and user defined commands.
   // For ":2match" and ":3match" we need to skip the number.
   Invocation   invo;
   invo.comm = (*name == '2' || *name == '3') ? name + 1 : name;
   invo.id = (CommIndex)0;
   invo.flags = 0;
   CS p = findCommand(&invo, &full, NULL);
   if (!p)
      return 3;
   if (eeIsDigit(*name) && invo.id != C_match)
      return 0;
   if (*skipwhite(p) != ZERO)
      return 0;   // trailing garbage
   return (invo.id == COUNT_COMMANDS ? 0 : (full ? 2 : 1));
}

void
f_fullcommand(Var *argvars, Var *returnVar) {
   int      save_cmod_flags = commModifierG.cmod_flags;

   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   CS name = tv_get_string(&argvars[0]);
   if (!name)
      return;

   while (*name == ':')
      name++;
   name = skip_range(name, true, NULL);

   Invocation invo;
   invo.comm = (*name == '2' || *name == '3') ? name + 1 : name;
   invo.id = (CommIndex)0;
   invo.addr_count = 0;
   ++emsg_silent;  // don't complain about using "en" in Vim9 script
   CS p = findCommand(&invo, NULL, NULL);
   --emsg_silent;
   if (!p || invo.id == COUNT_COMMANDS)
      goto theend;

   returnVar->string = copyStr(IS_USER_COMMAND(invo.id)
             ? get_user_command_name(invo.useridx, invo.id)
             : commands[invo.id].name);
theend:
   commModifierG.cmod_flags = save_cmod_flags;
}

CommIndex
commandGetInd(CS cmd, int len) {
   CommIndex idx;
   if (!oneLetterCommand(cmd, OUT &idx)) {
      for (idx = (CommIndex)0; (int)idx < (int)COUNT_COMMANDS; idx = (CommIndex)((int)idx + 1)) {
         if (STRNCMP(commands[(int)idx].name, cmd, (Unt)len) == 0)
            break;
      } 
   } 

   return idx;
}

long
commandGetFlags(CommIndex idx) {
   return (long)commands[(int)idx].flags;
}

//Skip a range specifier of the form: addr [,addr] [;addr] ..
//
//Backslashed delimiters after / or ? will be skipped, and commands will
//not be expanded between /'s and ?'s or after "'".
//
//Also skip white space and ":" characters after the range.
//Return the "cmd" pointer advanced to beyond the range.
CS
skip_range(
   CS cmd_start,
   int skip_star,   // skip "*" used for Visual range
   Unt* ctx)      // pointer to context or NULL
{
   CS cmd = cmd_start;
   unsigned   delim;

   while (firstOccurrence((CS)" \t0123456789.$%'/?-+,;\\", *cmd) != NULL) {
      if (*cmd == '\\') {
         if (cmd[1] == '?' || cmd[1] == '/' || cmd[1] == '&')
            ++cmd;
         else
            break;
      } ei (*cmd == '\'') {
         CS p = cmd;

         // a quote is only valid at the start or after a separator
         while (p > cmd_start) {
            --p;
            if (!SPACE_OR_TAB(*p))
               break;
         }
         if (cmd > cmd_start && !SPACE_OR_TAB(*p) && *p != ',' && *p != ';')
            break;
         if (*++cmd == ZERO && ctx != NULL)
            *ctx = EXPAND_NOTHING;
      } ei (*cmd == '/' || *cmd == '?') {
         delim = *cmd++;
         while (*cmd != ZERO && *cmd != delim)
            if (*cmd++ == '\\' && *cmd != ZERO)
               ++cmd;
         if (*cmd == ZERO && ctx)
            *ctx = EXPAND_NOTHING;
      }
      if (*cmd != ZERO)
         ++cmd;
   }

   // Skip ":" and white space.
   while (*cmd == ':')
      cmd = skipwhite(cmd + 1);

    // Skip "*" used for Visual range.
   if (skip_star && *cmd == '*')
       cmd = skipwhite(cmd + 1);

   return cmd;
}

private void
addr_error(CommandAddress addressKind) {
   if (addressKind == ADDR_NONE)
      emsg(_(e_no_range_allowed));
   else
      emsg(_(e_invalid_range));
}

//Return the default address for an address type.
private LineNr
default_address(Invocation* invo) {
   LineNr lnum = 0;

   switch (invo->addressKind) {
   case ADDR_LINES:
   case ADDR_OTHER:
      // Default is the cursor line number.  Avoid using an invalid
      // line number though.
      if (curPor->cursor.lnum > curBook->mem.lineCount)
         lnum = curBook->mem.lineCount;
      else
         lnum = curPor->cursor.lnum;
      break;
   case ADDR_PORTALS:
      lnum = GET_PORT_NR;
      break;
   case ADDR_ARGUMENTS:
      lnum = curPor->argListInd + 1;
      if (lnum > ARGCOUNT)
         lnum = ARGCOUNT;
      break;
   case ADDR_LOADED_BUFFERS:
   case ADDR_BUFFERS:
      lnum = curBook->fiNum;
      break;
   case ADDR_TABS:
      lnum = CURRENT_TAB_NR;
      break;
   case ADDR_TABS_RELATIVE:
   case ADDR_UNSIGNED:
      lnum = 1;
      break;
   case ADDR_QUICKFIX:
      lnum = llGetCurrIndex(invo);
      break;
   case ADDR_QUICKFIX_VALID:
      lnum = llGetCurrValidIndex(invo);
      break;
   case ADDR_NONE:
      // Will give an error later if a range is found.
      break;
   }
   return lnum;
}

//Get a single Command address.
//
//Set ptr to the next character after the part that was interpreted. Set ptr to NULL when an 
//error is encountered. This may set the last used search pattern.
//
//Return MAXLNUM when no address was found.
LineNr
doGetCommandAddress(
   Invocation   *invo UNUSED,
   OUT CS* ptr,
   CommandAddress   addressKind,
   int skip,      // only skip the address, don't use it
   int silent,      // no errors or side effects
   int to_other_file,  // flag: may jump to other file
   int address_count UNUSED // 1 for first address, >1 after comma
){
   int      c;
   int      i;
   long   n;
   Pos   pos;
   Pos   *fp;
   LineNr   lnum;
   Book   *book;

   CS cmd = skipwhite(*ptr);
   lnum = MAXLNUM;
   do {
      switch (*cmd) {
      case '.':             // '.' - Cursor position
         ++cmd;
         switch (addressKind) {
         case ADDR_LINES:
         case ADDR_OTHER:
            lnum = curPor->cursor.lnum;
            break;
         case ADDR_PORTALS:
            lnum = GET_PORT_NR;
            break;
         case ADDR_ARGUMENTS:
            lnum = curPor->argListInd + 1;
            break;
         case ADDR_LOADED_BUFFERS:
         case ADDR_BUFFERS:
            lnum = curBook->fiNum;
            break;
         case ADDR_TABS:
            lnum = CURRENT_TAB_NR;
            break;
         case ADDR_NONE:
         case ADDR_TABS_RELATIVE:
         case ADDR_UNSIGNED:
            addr_error(addressKind);
            cmd = NULL;
            goto error;
            break;
         case ADDR_QUICKFIX:
            lnum = llGetCurrIndex(invo);
            break;
         case ADDR_QUICKFIX_VALID:
            lnum = llGetCurrValidIndex(invo);
            break;
         }
         break;

      case '$':             // '$' - last line
         ++cmd;
         switch (addressKind) {
         case ADDR_LINES:
         case ADDR_OTHER:
            lnum = curBook->mem.lineCount;
            break;
         case ADDR_PORTALS:
            lnum = LAST_WIN_NR;
            break;
         case ADDR_ARGUMENTS:
            lnum = ARGCOUNT;
            break;
         case ADDR_LOADED_BUFFERS:
            book = lastBook;
            while (book->mem.mfile == NULL) {
               if (book->prev == NULL)
                  break;
               book = book->prev;
            }
            lnum = book->fiNum;
            break;
         case ADDR_BUFFERS:
               lnum = lastBook->fiNum;
               break;
         case ADDR_TABS:
               lnum = LAST_TAB_NR;
               break;
         case ADDR_NONE:
         case ADDR_TABS_RELATIVE:
         case ADDR_UNSIGNED:
               addr_error(addressKind);
               cmd = NULL;
               goto error;
               break;
         case ADDR_QUICKFIX:
               lnum = llGetSize(invo);
               if (lnum == 0)
                   lnum = 1;
               break;
         case ADDR_QUICKFIX_VALID:
               lnum = llGetValidSize(invo);
               if (lnum == 0)
                   lnum = 1;
               break;
         }
         break;

      case '\'':             // ''' - mark
         if (*++cmd == ZERO) {
             cmd = NULL;
             goto error;
         }
         if (addressKind != ADDR_LINES) {
             addr_error(addressKind);
             cmd = NULL;
             goto error;
         }
         if (skip)
            ++cmd;
         else {
            // Only accept a mark in another file when it is used by itself: ":'M".
            fp = getmark(*cmd, to_other_file && cmd[1] == ZERO);
            ++cmd;
            if (fp == (Pos *)-1)
               // Jumped to another file.
               lnum = curPor->cursor.lnum;
            else {
               if (check_mark(fp) == FAIL) {
                  cmd = NULL;
                  goto error;
               }
               lnum = fp->lnum;
            }
         }
         break;

      case '/':
      case '?':         // '/' or '?' - search
         c = *cmd++;
         if (addressKind != ADDR_LINES) {
            addr_error(addressKind);
            cmd = NULL;
            goto error;
         }
         if (skip)  { // skip "/pat/"
            cmd = skip_regexp(cmd, c, true);
            if (*cmd == c)
               ++cmd;
         } else {
            pos = curPor->cursor; // save curPor->cursor

            // When '/' or '?' follows another address, start from there.
            if (lnum > 0 && lnum != MAXLNUM)
               curPor->cursor.lnum = lnum > curBook->mem.lineCount ? curBook->mem.lineCount : lnum;

            //Start a forward search at the end of the line (unless before the first line).
            //Start a backward search at the start of the line. This makes sure we never match in 
            //the current line, and can match anywhere in the next/previous line.
            curPor->cursor.col = (c == '/' && curPor->cursor.lnum > 0) ? MAXCOL : 0;
            searchcmdlen = 0;
            Unt flags = silent ? SEARCH_KEEP : SEARCH_HIS | SEARCH_MSG;
            if (!do_search(NULL, c, c, text(cmd), 1L, flags, NULL)) {
               curPor->cursor = pos;
               cmd = NULL;
               goto error;
            }
            lnum = curPor->cursor.lnum;
            curPor->cursor = pos;
            // adjust command string pointer
            cmd += searchcmdlen;
         }
         break;

      case '\\':          // "\?", "\/" or "\&", repeat search
         ++cmd;
         if (addressKind != ADDR_LINES) {
            addr_error(addressKind);
            cmd = NULL;
            goto error;
         }
         if (*cmd == '&')
            i = RE_SUBST;
         ei (*cmd == '?' || *cmd == '/')
            i = RE_SEARCH;
         else {
            emsg(_(e_backslash_should_be_followed_by));
            cmd = NULL;
            goto error;
         }

         if (!skip) {
            //When search follows another address, start from there.
            if (lnum != MAXLNUM)
               pos.lnum = lnum;
            else
               pos.lnum = curPor->cursor.lnum;

            //Start the search just like for the above do_search().
            if (*cmd != '?')
               pos.col = MAXCOL;
            else
               pos.col = 0;
            pos.coladd = 0;
            if (searchit(curPor, curBook, &pos, NULL,
                  *cmd == '?' ? BACKWARD : FORWARD,
                  (Text){null, 0}, 1L, SEARCH_MSG, i, NULL) != FAIL)
               lnum = pos.lnum;
            else {
               cmd = NULL;
               goto error;
            }
         }
         ++cmd;
         break;

      default:
      if (EE_ISDIGIT(*cmd))   // absolute line number
         lnum = parseLong(&cmd);
      }

      for (;;) {
         cmd = skipwhite(cmd);
         if (*cmd != '-' && *cmd != '+' && !EE_ISDIGIT(*cmd))
            break;

         if (lnum == MAXLNUM) {
            switch (addressKind) {
            case ADDR_LINES:
            case ADDR_OTHER:
               // "+1" is same as ".+1"
               lnum = curPor->cursor.lnum;
               break;
            case ADDR_PORTALS:
               lnum = GET_PORT_NR;
               break;
            case ADDR_ARGUMENTS:
               lnum = curPor->argListInd + 1;
               break;
            case ADDR_LOADED_BUFFERS:
            case ADDR_BUFFERS:
               lnum = curBook->fiNum;
               break;
            case ADDR_TABS:
               lnum = CURRENT_TAB_NR;
               break;
            case ADDR_TABS_RELATIVE:
               lnum = 1;
               break;
            case ADDR_QUICKFIX:
               lnum = llGetCurrIndex(invo);
               break;
            case ADDR_QUICKFIX_VALID:
               lnum = llGetCurrValidIndex(invo);
               break;
            case ADDR_NONE:
            case ADDR_UNSIGNED:
               lnum = 0;
               break;
            }
         }

         if (EE_ISDIGIT(*cmd))
            i = '+';      // "number" is same as "+number"
         else
            i = *cmd++;
         if (!EE_ISDIGIT(*cmd))   // '+' is '+1'
            n = 1;
         else {
            // "number", "+number" or "-number"
            n = parseLong(&cmd);
            if (n == MAXLNUM) {
               emsg(_(e_line_number_out_of_range));
               cmd = NULL;
               goto error;
            }
         }

         if (addressKind == ADDR_TABS_RELATIVE) {
            emsg(_(e_invalid_range));
            cmd = NULL;
            goto error;
         } ei (addressKind == ADDR_LOADED_BUFFERS|| addressKind == ADDR_BUFFERS)
            lnum = compute_buffer_local_count( addressKind, lnum, (i == '-') ? -1 * n : n);
         else {
            // Relative line addressing: need to adjust for lines in a
            // closed fold after the first address.
            if (addressKind == ADDR_LINES && (i == '-' || i == '+')
                            && address_count >= 2)
                (void)getFolds(lnum, NULL, OUT &lnum);
            if (i == '-')
                lnum -= n;
            else {
               if (lnum >= 0 && n >= LONG_MAX - lnum) {
                  emsg(_(e_line_number_out_of_range));
                  cmd = NULL;
                  goto error;
               }
               lnum += n;
            }
         }
      }
   } while (*cmd == '/' || *cmd == '?');

error:
   *ptr = cmd;
   return lnum;
}

//Set invo->line1 and invo->line2 to the whole range.
//Used for commands with the DFLALL flag and no range given.
private void
address_default_all(Invocation* invo) {
   invo->line1 = 1;
   switch (invo->addressKind) {
   case ADDR_LINES:
   case ADDR_OTHER:
      invo->line2 = curBook->mem.lineCount;
      break;
   case ADDR_LOADED_BUFFERS: {
      Book *book = firstBook;

      while (book->next && book->mem.mfile == NULL)
         book = book->next;
      invo->line1 = book->fiNum;
      book = lastBook;
      while (book->prev != NULL && book->mem.mfile == NULL)
          book = book->prev;
      invo->line2 = book->fiNum;
      break;
      }
   case ADDR_BUFFERS:
      invo->line1 = firstBook->fiNum;
      invo->line2 = lastBook->fiNum;
      break;
   case ADDR_PORTALS:
      invo->line2 = LAST_WIN_NR;
      break;
   case ADDR_TABS:
      invo->line2 = LAST_TAB_NR;
      break;
   case ADDR_TABS_RELATIVE:
      invo->line2 = 1;
      break;
   case ADDR_ARGUMENTS:
      if (ARGCOUNT == 0)
         invo->line1 = invo->line2 = 0;
      else
         invo->line2 = ARGCOUNT;
      break;
   case ADDR_QUICKFIX_VALID:
      invo->line2 = llGetValidSize(invo);
      if (invo->line2 == 0)
         invo->line2 = 1;
      break;
   case ADDR_NONE:
   case ADDR_UNSIGNED:
   case ADDR_QUICKFIX:
      internalErrMsg(S"Cannot use DFLALL with ADDR_NONE, ADDR_UNSIGNED or ADDR_QUICKFIX");
      break;
   }
}

//Get flags from a command argument.
private void
get_flags(Invocation* invo) {
   while (firstOccurrence((CS)"lp#", *invo->arg) != NULL) {
      if (*invo->arg == 'l')
          invo->flags |= EXFLAG_LIST;
      ei (*invo->arg == 'p')
          invo->flags |= EXFLAG_PRINT;
      else
          invo->flags |= EXFLAG_NR;
      invo->arg = skipwhite(invo->arg + 1);
   }
}

//Function called for command which is Not Implemented.  NI!
void
c_ni(Invocation* invo) {
   if (!invo->skip)
      invo->errmsg = _(e_sorry_command_is_not_available_in_this_version);
}

#ifdef HAVE_EX_SCRIPT_NI
//Function called for script command which is Not Implemented.  NI!
//Skips over ":perl <<EOF" constructs.
private void
ex_script_ni(Invocation* invo) {
   if (!invo->skip)
      c_ni(invo);
   else
      eeglFree(script_get(invo, invo->arg));
}
#endif

//Check range in a command for validity. Return NULL when valid, error message when invalid.
private CS
invalid_range(Invocation* invo) {
   Book   *book;

   if (invo->line1 < 0 || invo->line2 < 0 || invo->line1 > invo->line2)
      return _(e_invalid_range);

   if (invo->argFlags & RANGE) {
      switch (invo->addressKind) {
      case ADDR_LINES:
         if (invo->line2 > curBook->mem.lineCount
                + (invo->id == C_diffget || invo->id == C_diffput)
         )
            return _(e_invalid_range);
         break;
      case ADDR_ARGUMENTS:
         // add 1 if ARGCOUNT is 0
         if (invo->line2 > ARGCOUNT + (!ARGCOUNT))
             return _(e_invalid_range);
         break;
      case ADDR_BUFFERS:
         // Only a boundary check, not whether the buffers actually exist.
         if (invo->line1 < 1 || invo->line2 > get_highest_fnum())
             return _(e_invalid_range);
         break;
      case ADDR_LOADED_BUFFERS:
         book = firstBook;
         while (book->mem.mfile == NULL) {
            if (!book->next)
               return _(e_invalid_range);
            book = book->next;
         }
         if (invo->line1 < book->fiNum)
             return _(e_invalid_range);
         book = lastBook;
         while (book->mem.mfile == NULL) {
            if (!book->prev)
               return _(e_invalid_range);
            book = book->prev;
         }
         if (invo->line2 > book->fiNum)
            return _(e_invalid_range);
         break;
      case ADDR_PORTALS:
         if (invo->line2 > LAST_WIN_NR)
            return _(e_invalid_range);
         break;
      case ADDR_TABS:
         if (invo->line2 > LAST_TAB_NR)
            return _(e_invalid_range);
         break;
      case ADDR_TABS_RELATIVE:
      case ADDR_OTHER:
         // Any range is OK.
         break;
      case ADDR_QUICKFIX:
         // No error for value that is too big, will use the last entry.
         if (invo->line2 <= 0) {
            if (invo->addr_count == 0)
               return _(e_no_entries_in_location_list);
            return _(e_invalid_range);
         }
         break;
      case ADDR_QUICKFIX_VALID:
         if ((invo->line2 != 1 && invo->line2 > llGetValidSize(invo)) || invo->line2 < 0)
            return _(e_invalid_range);
         break;
      case ADDR_UNSIGNED:
      case ADDR_NONE:
         // Will give an error elsewhere.
         break;
      }
   }
   return NULL;
}

// Correct the range for zero line number, if required.
private void
correct_range(Invocation* invo) {
   if (!(invo->argFlags & ZERO_LINE_OK)) { // zero in range not allowed
      if (invo->line1 == 0)
         invo->line1 = 1;
      if (invo->line2 == 0)
         invo->line2 = 1;
   }
}

//For a ":vimgrep" or ":vimgrepadd" command return a pointer past the
//pattern.  Otherwise return invo->arg.
private CS
skip_grep_pat(Invocation* invo) {
   CS p = invo->arg;

   if (*p != ZERO && (invo->id == C_vimgrep
         || invo->id == C_vimgrepadd
         || grepIsActuallyInternal(invo->id))
   ) {
      p = skipEeglGrepPat(p, NULL, NULL);
      if (!p)
         p = invo->arg;
   }
   return p;
}

//For the ":make" and ":grep" commands insert the @makeprog/@grepprog option
//into the command line, so that things like % get expanded.
private CS
replaceMakeProgramName(Invocation* invo, OUT CS p, OUT CS* commline) {
   CS programName;
   CS pos;
   CS ptr;
   int len;
   int i;

   // Don't do it when ":vimgrep" is used for ":grep".
   if ((invo->id == C_make || invo->id == C_grep || invo->id == C_grepadd)
          && !grepIsActuallyInternal(invo->id)
   ) {
      if (invo->id == C_grep || invo->id == C_grepadd) {
         programName = curBook->o.grepProg;
      } else {
         programName = curBook->o.makeProg;
      }
      if (!programName)
         return p;
         
      p = skipwhite(p);

      CS newCommline;
      if ((pos = (CS)STRSTR(programName, "$*")) != NULL) {
         // replace $* by given arguments
         i = 1;
         while ((pos = (CS)STRSTR(pos + 2, "$*")) != NULL)
            ++i;
         len = (int)STRLEN(p);
         newCommline = alloc(STRLEN(programName) + (Unt)i * (len - 2) + 1);
         ptr = newCommline;
         while ((pos = (CS)strstr((char *)programName, "$*")) != NULL) {
            i = (int)(pos - programName);
            STRNCPY(ptr, programName, i);
            STRCPY(ptr += i, p);
            ptr += len;
            programName = pos + 2;
         }
         STRCPY(ptr, programName);
      } else {
         Unt programNameLen = STRLEN(programName);

         newCommline = alloc(programNameLen + STRLEN(p) + 2);
         STRCPY(newCommline, programName);
         STRCPY(newCommline + programNameLen, " ");
         STRCPY(newCommline + programNameLen + 1, p);
      }
      
      msg_make(p);

      // 'invo->comm' is not set here, because it is not used at C_make
      eeglFree(*commline);
      *commline = newCommline;
      p = newCommline;
   }
   return p;
}

//Expand file name in a command argument. When an error is detected, "errorMsg" is set to a 
//non-NULL pointer. Return FAIL for failure, OK otherwise.
int
expand_filename(Invocation* invo, OUT CS* commline, OUT CS* errorMsg){
   CS repl;
   Unt srclen;
   int n;
   int escaped;

   // Skip a regexp pattern for ":vimgrep[add] pat file..."
   CS p = skip_grep_pat(invo);

   //Decide to expand wildcards *before* replacing '%', '#', etc.  If
   //the file name contains a wildcard it should not cause expanding.
   //(it will be expanded anyway if there is a wildcard before replacing).
   int has_wildcards = mch_has_wildcard(p);// need to expand wildcards
   while (*p != ZERO) {
      // Skip over `=expr`, wildcards in it are not expanded.
      if (p[0] == '`' && p[1] == '=') {
         p += 2;
         (void)skip_expr(&p, NULL);
         if (*p == '`')
            ++p;
         continue;
      }
      //Quick check if this cannot be the start of a special string.
      //Also removes backslash before '%', '#' and '<'.
      if (firstOccurrence((CS)"%#<", *p) == NULL) {
         ++p;
         continue;
      }

      //Try to find a match at this position.
      repl = evalVars(
            OUT &(invo->higherOrderLnum), OUT errorMsg, 
            p, invo->arg, &srclen, &escaped, true
      );
      if (*errorMsg)      // error detected
         return FAIL;
      if (repl == NULL) {     // no match found
         p += srclen;
         continue;
      }

      //Wildcards won't be expanded below, the replacement is taken
      //literally. But do expand "~/file", "~user/file" and "$HOME/file".
      if (firstOccurrence(repl, '$') != NULL || firstOccurrence(repl, '~') != NULL) {
         CS l = repl;
         repl = doExpandEnvInMultiplePaths(repl);
         eeglFree(l);
      }

      // Need to escape white space et al. with a backslash. Don't do this for:
      // - replacement that already has been escaped: "##"
      // - shell commands (may have to use quotes instead).
      if (!invo->usefilter
         && !escaped
         && invo->id != C_bang
         && invo->id != C_grep
         && invo->id != C_grepadd
         && invo->id != C_make
         && invo->id != C_terminal
      ) {
         CS l;
# define ESCAPE_CHARS escape_chars

         for (l = repl; *l; ++l) {
            if (firstOccurrence(ESCAPE_CHARS, *l) != NULL) {
               l = copyStr_escaped(repl, ESCAPE_CHARS);
               eeglFree(repl);
               repl = l;
               break;
            }
         } 
      }

      // For a shell command a '!' must be escaped.
      if ((invo->usefilter || invo->id == C_bang || invo->id == C_terminal)
                && eeStrpbrk(repl, S"!") != NULL
      ) {
         CS l = copyStr_escaped(repl, S"!");
         eeglFree(repl);
         repl = l;
      }

      p = repl_commline(invo, p, srclen, repl, OUT commline);
      eeglFree(repl);
      if (!p)
         return FAIL;
   }

   //One file argument: Expand wildcards.
   //Don't do this with ":r !command" or ":w !command".
   if ((invo->argFlags & NOSPC_IN_EXTRA) && !invo->usefilter) {
      //May do this twice:
      //1. Replace environment variables.
      //2. Replace any other wildcards, remove backslashes.
      for (n = 1; n <= 2; ++n) {
         if (n == 2) {
            //Halve the number of backslashes (this is Vi compatible).
            //When wildcards are expanded, this is done by expandWildcard() below.
            if (!has_wildcards)
               backslash_halve(invo->arg);
          }

         if (has_wildcards) {
            if (n == 1) {
               //First loop: May expand environment variables. This can be done much faster with 
               //doExpandEnv() than with something else (e.g., calling a shell).
               //After expanding environment variables, check again if there are still wildcards 
               //present.
               if (firstOccurrence(invo->arg, '$') != NULL 
                     || firstOccurrence(invo->arg, '~') != NULL
               ) {
                  doExpandEnvVarsWithEscaped(
                        OUT (Text){nameBuffG, MAXPATHL}, invo->arg, true, NULL
                  );
                  has_wildcards = mch_has_wildcard(nameBuffG);
                  p = nameBuffG;
               }  else
                  p = NULL;
            } else { // n == 2
               Expand   xpc;
               int options = WILD_LIST_NOTFOUND | WILD_NOERROR | WILD_ADD_SLASH;

               expandInit(&xpc);
               xpc.context = EXPAND_FILES;
               if (p_wic)
                  options += WILD_ICASE;
               p = expandWildcard(&xpc, invo->arg, NULL, options, WILD_EXPAND_FREE);
               if (!p)
                  return FAIL;
            }
            if (p) {
               (void)repl_commline(invo, invo->arg, STRLEN(invo->arg), p, OUT commline);
               if (n == 2)   // p came from expandWildcard()
                  eeglFree(p);
            }
         }
      }
   }
   return OK;
}

//Replace part of the command line, keeping invo->comm, invo->arg and invo->nextComm correct.
//"src" points to the part that is to be replaced, of length "srclen". "repl" is the replacement 
//string. Return a pointer to the character after the replaced string, or null for failure.
private CS
repl_commline(
   Invocation* invo,
   CS src,
   Unt srclen,
   CS repl,
   OUT CS* commline
) {
   //The new command line is build in newCommline[]. First allocate it.
   //Careful: a "+cmd" argument may have been ZERO terminated.
   Unt repllen = STRLEN(repl);
   Unt taillen = STRLEN(src + srclen);
   Unt i = (src - *commline) + repllen + taillen + 3;
   if (invo->nextComm)
      i += STRLEN(invo->nextComm);   // add space for next command
   CS newCommline = alloc(i);

   //Copy the stuff before the expanded part.
   //Copy the expanded stuff.
   //Copy what came after the expanded part.
   //Copy the next commands, if there are any.
   Unt newCommlineLen = src - *commline;   // length of part before replacement
   mch_memmove(newCommline, *commline, newCommlineLen);

   mch_memmove(newCommline + newCommlineLen, repl, repllen);
   newCommlineLen += repllen;      // remember the end of the string
   STRCPY(newCommline + newCommlineLen, src + srclen);
   src = newCommline + newCommlineLen;   // remember where to continue

   if (invo->nextComm) {     // append next command
      newCommlineLen += taillen + 1;
      STRCPY(newCommline + newCommlineLen, invo->nextComm);
      invo->nextComm = newCommline + newCommlineLen;
   }
   invo->comm = newCommline + (invo->comm - *commline);
   invo->arg = newCommline + (invo->arg - *commline);
   if (invo->higherOrderComm && invo->higherOrderComm != dollar_command)
      invo->higherOrderComm = newCommline + (invo->higherOrderComm - *commline);
   eeglFree(*commline);
   *commline = newCommline;

   return src;
}

// Check for '|' to separate commands and '//' to start comments.
// If "keep_backslash" is true do not remove any backslash.
void
separateNextCommand(Invocation* invo, int keep_backslash) {
   for (CS p = skip_grep_pat(invo) ; *p; MB_PTR_ADV(p)) {
      if (*p == Ctrl_V) {
         if ((invo->argFlags & (CTRLV | XFILE)) || keep_backslash)
            ++p;      // skip CTRL-V and next char
         else // remove CTRL-V and skip next char
            STRMOVE(p, p + 1);
         if (*p == ZERO)      // stop at ZERO after CTRL-V
            break;
      }
      // Skip over `=expr` when wildcards are expanded.
      ei (p[0] == '`' && p[1] == '=' && (invo->argFlags & XFILE)) {
         p += 2;
         (void)skip_expr(&p, NULL);
         if (*p == ZERO)      // stop at ZERO after CTRL-V
            break;
      }

      // Check for '//': start of comment or '|': next command
      // :redir @" doesn't either.
      ei ((isComment(p) && !(invo->argFlags & NOTRLCOM))
         || (*p == '|' && invo->id != C_append && invo->id != C_change && invo->id != C_insert)
         || *p == '\n'
      ){
         // We remove the '\' before the '|', unless CTRLV is used AND 'b' is present in 'cpoptions'.
         if (*(p - 1) == '\\') {
            if (!keep_backslash) {
                STRMOVE(p - 1, p);   // remove the '\'
                --p;
            }
         } else {
            invo->nextComm = check_nextcmd(p);
            *p = ZERO;
            break;
         }
      }
   }

   if (!(invo->argFlags & NOTRLCOM))   // remove trailing spaces
      del_trailing_spaces(invo->arg);
}

// get + command from command argument
private CS
getargcmd(OUT CS* argp) {
   CS arg = *argp;
   CS command = NULL;

   if (*arg == '+') {      // +[command]
      ++arg;
      if (isSpace(*arg) || *arg == ZERO)
          command = dollar_command;
      else {
          command = arg;
          arg = skip_cmd_arg(command, true);
          if (*arg != ZERO)
         *arg++ = ZERO;      // terminate command with ZERO
      }

      arg = skipwhite(arg);   // skip over spaces
      *argp = arg;
   }
   return command;
}

//Find end of "+command" argument.  Skip over "\ " and "\\".
CS
skip_cmd_arg(CS p, int rembs) {   // true to halve the number of backslashes
   while (*p && !isSpace(*p)) {
      if (*p == '\\' && p[1] != ZERO) {
         if (rembs)
            STRMOVE(p, p + 1);
         else
            ++p;
      }
      MB_PTR_ADV(p);
   }
   return p;
}

int
get_bad_opt(CS p, Invocation* invo) {
   if (caseInsensitiveCompare(p, "keep") == 0)
      invo->bad_char = BAD_KEEP;
   ei (caseInsensitiveCompare(p, "drop") == 0)
      invo->bad_char = BAD_DROP;
   ei (utf8CharLens[*p] == 1 && p[1] == ZERO)
      invo->bad_char = *p;
   else
      return FAIL;
   return OK;
}

// Function given to expandGeneric() to obtain the list of bad= names.
private CS
get_bad_name(Expand *xp UNUSED, int idx) {
   // Note: Keep this in sync with getargopt.
   static CS p_bad_values[] = {
      S"?",
      S"keep",
      S"drop",
   };

   if (idx < 0 || idx >= (int)ARRAY_LENGTH(p_bad_values))
      return NULL;

   return p_bad_values[idx];
}

// Get "++opt=arg" argument. Return FAIL or OK.
private int
getargopt(Invocation* invo) {
   CS arg = invo->arg + 2;
   int      *pp = NULL;
   int      bad_char_idx;
   // Note: Keep this in sync with get_argoname.

   // ":edit ++[no]bin[ary] file"
   if (STRNCMP(arg, "bin", 3) == 0 || STRNCMP(arg, "nobin", 5) == 0) {
      if (*arg == 'n') {
          arg += 2;
          invo->force_bin = FORCE_NOBIN;
      } else
         invo->force_bin = FORCE_BIN;
      if (!checkforcmd(&arg, S"binary", 3))
         return FAIL;
      invo->arg = skipwhite(arg);
      return OK;
   }

   // ":read ++edit file"
   if (STRNCMP(arg, "edit", 4) == 0) {
      invo->read_edit = true;
      invo->arg = skipwhite(arg + 4);
      return OK;
   }

   if (STRNCMP(arg, "bad", 3) == 0) {
      arg += 3;
      pp = &bad_char_idx;
   }

   if (!pp || *arg != '=')
      return FAIL;

   ++arg;
   *pp = (int)(arg - invo->comm);
   arg = skip_cmd_arg(arg, false);
   invo->arg = skipwhite(arg);
   *arg = ZERO;

   // Check ++bad= argument.  Must be a single-byte character, "keep" or "drop".
   if (get_bad_opt(invo->comm + bad_char_idx, invo) == FAIL)
      return FAIL;

   return OK;
}

// Function given to expandGeneric() to obtain the list of ++opt names.
private CS
get_argoname(Expand* xp UNUSED, int idx) {
   // Note: Keep this in sync with getargopt.
   static CS p_opt_values[] = {SMAP((CS),
      "encoding=",
      "binary",
      "nobinary",
      "bad=",
      "edit"
   )};

   if (idx < 0 || idx >= (int)ARRAY_LENGTH(p_opt_values))
      return NULL;

   return p_opt_values[idx];
}

// Command-line expansion for ++opt=name.
int
expand_argopt(
   CS pat,
   Expand    *xp,
   RegMatch  *rmp,
   OUT ExpandMatch* matches
) {
   if (xp->input.c > xp->fullInput && *(xp->input.c - 1) == '=') {
      Byte *(*cb)(Expand *, int) = NULL;
      CS name_end = xp->input.c - 1;
      if (name_end - xp->fullInput >= 3 && STRNCMP(name_end - 3, "bad", 3) == 0)
         cb = get_bad_name;

      if (cb) {
         return expandGeneric(
             pat, xp, rmp, cb, false, OUT matches
         );
      }
      return FAIL;
   }

   return expandGeneric(
       pat,
       xp,
       rmp,
       get_argoname,
       false,
       OUT matches
   );
}

//}}}
//{{{misc 1

void
c_autocmd(Invocation* invo) {
   //Disallow autocommands from .exrc and .vimrc in current directory for security reasons.
   if (invo->id == C_autocmd)
      do_autocmd(invo, invo->arg, invo->forceit);
   else
      do_augroup(invo->arg, invo->forceit);
}

// ":doautocmd": Apply the automatic commands to the current book.
void
c_doautocmd(Invocation* invo) {
   CS arg = invo->arg;
   Boole did_aucmd;

   (void)do_doautocmd(arg, true, OUT &did_aucmd);
}

//:[N]bunload[!] [N] [bookname] unload book
//:[N]bdelete[!] [N] [bookname] delete book from book list
//:[N]bwipeout[!] [N] [bookname] delete book really
void
c_bunload(Invocation* invo) {
   if (portErrorIfPopup(true))
      return;
   invo->errmsg = do_bufdel(
       invo->id == C_bdelete 
       ? DOBOOK_DEL : (invo->id == C_bwipeout ? DOBOOK_WIPE : DOBOOK_UNLOAD), invo->arg,
       invo->addr_count, (int)invo->line1, (int)invo->line2, invo->forceit);
}

//:[N]book [N]   to book N
//:[N]sbook [N]  to book N
void
c_book(Invocation* invo) {
   if (portErrorIfPopup(true))
      return;
   do_exbuffer(invo);
}

// ":book" command and alike.
private void
do_exbuffer(Invocation* invo) {
   if (*invo->arg)
      invo->errmsg = ex_errmsg(e_trailing_characters_str, invo->arg);
   else {
      if (invo->addr_count == 0)   // default is current book
         bookGoto(invo, DOBOOK_CURRENT, FORWARD, 0);
      else
         bookGoto(invo, DOBOOK_FIRST, FORWARD, (int)invo->line2);
      if (invo->higherOrderComm)
         do_cmd_argument(invo->higherOrderComm);
   }
}

//:[N]bmodified [N]   to next mod. book
//:[N]sbmodified [N]   to next mod. book
void
c_bmodified(Invocation* invo) {
   bookGoto(invo, DOBOOK_MOD, FORWARD, (int)invo->line2);
   if (invo->higherOrderComm)
      do_cmd_argument(invo->higherOrderComm);
}

//:[N]bnext [N]   to next book
//:[N]sbnext [N]   split and to next book
void
c_bnext(Invocation* invo){
   if (portErrorIfPopup(true))
      return;

   bookGoto(invo, DOBOOK_CURRENT, FORWARD, (int)invo->line2);
   if (invo->higherOrderComm)
      do_cmd_argument(invo->higherOrderComm);
}

//:[N]bNext [N]   to previous book
//:[N]bprevious [N]   to previous book
//:[N]sbNext [N]   split and to previous book
//:[N]sbprevious [N]   split and to previous book
void
c_bprevious(Invocation* invo) {
   if (portErrorIfPopup(true))
      return;

   bookGoto(invo, DOBOOK_CURRENT, BACKWARD, (int)invo->line2);
   if (invo->higherOrderComm)
      do_cmd_argument(invo->higherOrderComm);
}

//:brewind      to first book
//:bfirst      to first book
//:sbrewind      split and to first book
//:sbfirst      split and to first book
void
c_brewind(Invocation* invo) {
   if (portErrorIfPopup(true))
      return;

   bookGoto(invo, DOBOOK_FIRST, FORWARD, 0);
   if (invo->higherOrderComm)
      do_cmd_argument(invo->higherOrderComm);
}

//:blast      to last book
//:sblast      split and to last book
void
c_blast(Invocation* invo) {
   if (portErrorIfPopup(true))
      return;

   bookGoto(invo, DOBOOK_LAST, BACKWARD, 0);
   if (invo->higherOrderComm)
      do_cmd_argument(invo->higherOrderComm);
}

// Check if "c" ends a Command.
int
endsComm(CS c) {
   return *c == ZERO || *c == '|' || isComment(c) || *c == '\n';
}

//Return the next command, after the first '|' or '\n'. NULL if not found.
CS
find_nextcmd(CS p) {
   while (*p != '|' && *p != '\n') {
      if (*p == ZERO)
         return NULL;
      ++p;
   }
   return (p + 1);
}

//Check if *p is a separator between commands, skipping over white space.
//Return NULL if it isn't, the following character if it is.
CS
check_nextcmd(CS p) {
   CS s = skipwhite(p);

   if (*s == '|' || *s == '\n')
      return (s + 1);
   else
      return NULL;
}

// If "invo->nextComm" is not set, check for a next command at "arg".
void
set_nextcmd(OUT Invocation* invo, CS arg) {
   CS nextComm = check_nextcmd(arg);

   if (!invo->nextComm)
      invo->nextComm = nextComm;
   ei (nextComm)
      // cannot use "| command" inside a  {} block
      showErrFmtMsg(_(e_cannot_use_bar_to_separate_commands_here_str), arg);
}

// Function given to expandGeneric() to obtain the list of command names.
CS
get_command_name(Expand *xp UNUSED, int idx) {
   if (idx >= (int)COUNT_COMMANDS)
      return expand_user_command_name(idx);
      // the following are not real commands
   if (STRNCMP(commands[idx].name, "{", 1) == 0 || STRNCMP(commands[idx].name, "}", 1) == 0)
      return (CS)"";
   return commands[idx].name;
}

void
c_hilite(Invocation* invo) {
   if (*invo->arg == ZERO && invo->comm[2] == '!')
      msg(_("Greetings, Eegl user!"));
   doHilite(invo->arg, invo->forceit, false);
}


// Call this function if we thought we were going to exit, but we won't
// (because of an error). May need to restore the terminal mode.
void
not_exiting(void) {
   isExitingG = false;
   termSetMode(TMODE_RAW);
}

private int
before_quit_autocmds(Portal *po, int quit_all) {
   applyAutocomms(EVENT_QUITPRE, NULL, NULL, false, po->book);

   // Bail out when autocommands closed the portal. Refuse to quit when the book in the last 
   // portal is being closed (can only happen in autocommands).
   if (!portalIsValid(po)
          || curBookLocked()
          || (po->book->countPortals == 1 && po->book->locked > 0))
      return true;

   if (quit_all) {
      applyAutocomms(EVENT_EXITPRE, NULL, NULL, false, curBook);
      // Refuse to quit when locked or when the portal was closed or the book in the last portal 
      // is being closed (can only happen in autocommands).
      if (!portalIsValid(po) || curBookLocked()
              || (curBook->countPortals == 1 && curBook->locked > 0))
          return true;
   }

   return false;
}

//":quit": quit current portal, quit Eegl if the last portal is closed.
//":{nr}quit": quit portal {nr}
//Also used when closing a terminal portal that's the last one.
void
c_quit(Invocation* invo) {
    Portal   *po;

   if (commPortTypeG != 0) {
      commPortResultG = Ctrl_C;
      return;
   }
   // Don't quit while editing the command line.
   if (text_locked()) {
      text_locked_msg();
      return;
   }
   if (invo->addr_count > 0) {
      int   wnr = invo->line2;

      for (po = firstPor; po->next != NULL; po = po->next)
         if (--wnr <= 0)
            break;
    } else
      po = curPor;

   // Refuse to quit when locked.
   if (curBookLocked())
      return;

   // Trigger QuitPre and maybe ExitPre
   if (before_quit_autocmds(po, false))
      return;

   // If there is only one relevant portal, we will exit.
   if (onlyOnePortal())
      isExitingG = true;
   if (onlyOnePortal() && check_changed_any(invo->forceit, true)) {
      not_exiting();
   } else {
      // quit last portal
      // Note: onlyOnePortal() returns true, even if a help portal is still open. In that case 
      // only quit, if no address has been specified. Example:
      // :h|wincmd w|1q     - don't quit
      // :h|wincmd w|q      - quit
      if (onlyOnePortal() && (ONLY_ONE_PORTAL || invo->addr_count == 0))
         exitEegl(0);
      not_exiting();
      // close portal; may free book
      closePortal(po, invo->forceit);
    }
}

// ":cquit".
void
c_cquit(Invocation* invo UNUSED) {
   // this does not always pass on the exit code to the Manx compiler. why?
   exitEegl(invo->addr_count > 0 ? (int)invo->line2 : EXIT_FAILURE);
}

//Do preparations for "qall" and "wqall". Return FAIL when quitting should be aborted.
int
before_quit_all(Invocation* invo) {
   if (commPortTypeG != 0) {
      if (invo->forceit)
          commPortResultG = K_XF1;   // ex_window() takes care of this
      else
          commPortResultG = K_XF2;
      return FAIL;
   }

    // Don't quit while editing the command line.
   if (text_locked()) {
      text_locked_msg();
      return FAIL;
   }

   if (before_quit_autocmds(curPor, true))
      return FAIL;

   return OK;
}

// ":qall": try to quit all portals
void
c_quit_all(Invocation* invo) {
   if (before_quit_all(invo) == FAIL)
      return;
   isExitingG = true;
   if (invo->forceit || !check_changed_any(false, false))
      exitEegl(0);
   not_exiting();
}

// ":close": close current portal; if it is the last one, close the program
void
c_close(Invocation* invo) {
   if (commPortTypeG != 0)
      commPortResultG = Ctrl_C;
   ei (!text_locked() && !curBookLocked()) {
      if (invo->addr_count == 0)
         closePortalInternal(curPor, NULL);
      else {
         Portal   *port;
         int      portNr = 0;
         FOR_ALL_PORTALS(port) {
            portNr++;
            if (portNr == invo->line2)
               break;
         }
         if (port == NULL)
            port = lastPor;
         closePortalInternal(port, NULL);
      }
   }
}

//}}}
//{{{tabs & portals, closing & openin'

// callback function for 'findfunc'
private Callback findFnCb;


// ":pclose": Close any preview portal.
void
c_pclose(Invocation* invo UNUSED) {
   Portal* port;

   // First close any normal portal.
   FOR_ALL_PORTALS(port) {
      if (port->isPreview) {
         closePortalInternal(port, NULL);
         return;
      }
   }
   // Also when 'previewpopup' is empty, it might have been cleared.
   popup_close_preview();
}


// Close portal "port" and take care of handling closing the last portal into a modified book.
private void
closePortalInternal(Portal* port, Tab* t) {     // NULL or the tab "port" is in
   // Never close the autocommand portal.
   if (is_autoCommPort(port)) {
      emsg(_(e_cannot_close_autocmd_or_popup_portal));
      return;
   }
   if (portalLayout_locked(C_close))
      return;


   // free book when not hiding it or when it's a scratch book
   if (lastPortal())
      exitEegl(0);
   ei (t == NULL)
      closePortal(port, false);
   else
      closePortal_othertab(port, false, t);
}

//Handle the argument for a tab-related command. Return a tab number.
//When an error is encountered then invo->errmsg is set.
private int
getTabRelatedArg(Invocation* invo) {
   int tabId;
   int unaccept_arg0 = (invo->id == C_tabmove) ? 0 : 1;

   if (invo->arg && *invo->arg != ZERO) {
      CS p = invo->arg;
      CS p_save;
      int    relative = 0; // argument +N/-N means: go to N places to the
                 // right/left relative to the current position.

      if (*p == '-') {
         relative = -1;
         p++;
      } ei (*p == '+') {
         relative = 1;
         p++;
      }

      p_save = p;
      tabId = parseLong(&p);

      if (relative == 0) {
         if (STRCMP(p, "$") == 0)
            tabId = LAST_TAB_NR;
         ei (STRCMP(p, "#") == 0) {
            if (isTabValid(lastUsedTabG))
               tabId = indexOfTab(lastUsedTabG);
            else {
               invo->errmsg = ex_errmsg(e_invalid_value_for_argument_str, invo->arg);
               tabId = 0;
               goto theend;
            }
         } ei (p == p_save || *p_save == '-' || *p != ZERO || tabId > LAST_TAB_NR) {
            // No numbers as argument.
            invo->errmsg = ex_errmsg(e_invalid_argument_str, invo->arg);
            goto theend;
         }
      } else {
         if (*p_save == ZERO)
            tabId = 1;
         ei (p == p_save || *p_save == '-' || *p != ZERO || tabId == 0) {
            // No numbers as argument.
            invo->errmsg = ex_errmsg(e_invalid_argument_str, invo->arg);
            goto theend;
         }
         tabId = tabId * relative + indexOfTab(curtab);
         if (!unaccept_arg0 && relative == -1)
            --tabId;
      }
      if (tabId < unaccept_arg0 || tabId > LAST_TAB_NR)
         invo->errmsg = ex_errmsg(e_invalid_argument_str, invo->arg);
    }
   ei (invo->addr_count > 0) {
      if (unaccept_arg0 && invo->line2 == 0) {
         invo->errmsg = _(e_invalid_range);
         tabId = 0;
      } else {
         tabId = invo->line2;
         if (!unaccept_arg0) {
            CS cmdp = invo->comm;

            while (--cmdp > *invo->commline
               && (SPACE_OR_TAB(*cmdp) || EE_ISDIGIT(*cmdp)))
                ;
            if (*cmdp == '-') {
               --tabId;
               if (tabId < unaccept_arg0)
                  invo->errmsg = _(e_invalid_range);
            }
         }
      }
   } else {
      switch (invo->id) {
      case C_tabnext:
         tabId = indexOfTab(curtab) + 1;
         if (tabId > LAST_TAB_NR)
            tabId = 1;
         break;
      case C_tabmove:
         tabId = LAST_TAB_NR;
         break;
      default:
         tabId = indexOfTab(curtab);
      }
   }

theend:
   return tabId;
}

//":tabclose": close current tab, unless it is the last one. ":tabclose N": close tab N.
void
c_tabclose(Invocation* invo) {
   if (commPortTypeG != 0) {
      commPortResultG = K_IGNORE;
      return;
   }

   if (firstTabG->next == NULL) {
      emsg(_(e_cannot_close_last_tab_page));
      return;
   }

   if (portalLayout_locked(C_tabclose))
      return;

   int tabId = getTabRelatedArg(invo);
   if (invo->errmsg != NULL)
      return;

   Tab* t = getTab(tabId);
   if (!t) {
      beep_flush();
      return;
   }
   if (t != curtab) {
      tabCloseOther(t);
      return;
   } ei (!text_locked() && !curBookLocked())
      tabClose();
}

// ":tabonly": close all tabs except the current one
void
c_tabonly(Invocation* invo) {
   if (commPortTypeG != 0) {
      commPortResultG = K_IGNORE;
      return;
   }

   if (firstTabG->next == NULL) {
      msg(_("Already only one tab"));
      return;
   }

   if (portalLayout_locked(C_tabonly))
      return;

   int tabId = getTabRelatedArg(invo);
   if (invo->errmsg)
      return;

   gotoTabById(tabId);
   // Repeat this up to a 1000 times, because autocommands may mess up the lists.
   for (int done = 0; done < 1000; ++done) {
      Tab* t;
      FOR_ALL_TABS(t) {
         if (t->topframe != topframeG) {
            tabCloseOther(t);
            // if we failed to close it quit
            if (isTabValid(t))
               done = 1000;
            // start over, "t" is now invalid
            break;
         }
      } 
      if (firstTabG->next == NULL)
         break;
   }
}

// Close the current tab
void
tabClose() {
   if (portalLayout_locked(C_tabclose))
      return;

   trigger_tabclosedpre(curtab, true);

   // First close all the portals but the current one.  If that worked then
   // close the last portal in this tab, that will close it.
   if (!ONLY_ONE_PORTAL)
      portCloseOthers(true);
   if (ONLY_ONE_PORTAL)
      closePortalInternal(curPor, NULL);
}

//Close tab "p", which is not the current tab. Note that autocommands may make "t" invalid.
void
tabCloseOther(Tab *t) {
   int      done = 0;
   trigger_tabclosedpre(t, true);

   // Limit to 1000 portals, autocommands may add a portal while we close one. OK, so I'm paranoid..
   while (++done < 1000) {
      Portal* po = t->firstPor;
      closePortalInternal(po, t);

      // Autocommands may delete the tab under our fingers and we may
      // fail to close a portal with a modified book.
      if (!isTabValid(t) || t->firstPor == po)
          break;
   }

   applyAutocomms(EVENT_TABCLOSED, NULL, NULL, false, curBook);
}

// ":only".
void
c_only(Invocation* invo) {
   if (portalLayout_locked(C_only))
      return;
   if (invo->addr_count > 0) {
      Portal   *po;
      int   wnr = invo->line2;
      for (po = firstPor; --wnr > 0; ) {
         if (po->next == NULL)
            break;
         else
            po = po->next;
      }
      gotoPortal(po);
   }
   portCloseOthers(true);
}

void
c_hide(Invocation* invo UNUSED) {
   // ":hide" or ":hide | cmd": hide current portal
   if (invo->skip)
      return;

   if (portalLayout_locked(C_hide))
   return;
   if (invo->addr_count == 0)
   closePortal(curPor, false);   // don't free book
    else {
      int winnr = 0;
      Portal* po;

      FOR_ALL_PORTALS(po) {
         winnr++;
         if (winnr == invo->line2)
            break;
      }
      if (!po)
         po = lastPor;
      closePortal(po, false);
   }
}

// ":exit", ":xit" and ":wq": Write file and quit the current portal.
void
c_exit(Invocation* invo) {
   if (commPortTypeG != 0) {
      commPortResultG = Ctrl_C;
      return;
   }
   // Don't quit while editing the command line.
   if (text_locked()) {
      text_locked_msg();
      return;
   }

   // we plan to exit if there is only one relevant portal
   if (onlyOnePortal())
      isExitingG = true;

   // Write the book for ":wq" or when it was changed.
   // Trigger QuitPre and ExitPre.
   // Check if we can exit now, after autocommands have changed things.
   if (((invo->id == C_wq || doWasCurBookChanged()) && do_write(invo) == FAIL)
       || before_quit_autocmds(curPor, false)
       || (onlyOnePortal() && check_changed_any(invo->forceit, false))
   ) {
      not_exiting();
   } else {
      if (onlyOnePortal())       // quit last portal, exit Eegl
         exitEegl(0);
      not_exiting();
      // Quit current portal, may free the book.
      closePortal(curPor, false);
   }
}

// ":print", ":list", ":number".
void
c_print(Invocation* invo) {
   if (curBook->mem.flags & ML_EMPTY)
      emsg(_(e_empty_buffer));
   else {
      for ( ;!gotInterruptG; ui_breakcheck()) {
         print_line(invo->line1, invo->id == C_list || (invo->flags & EXFLAG_LIST));
         if (++invo->line1 > invo->line2)
            break;
         out_flush();       // show one line at a time
      }
      setpcmark();
      // put cursor at last line
      curPor->cursor.lnum = invo->line2;
      beginline(BL_SOL | BL_FIX);
   }

   ex_no_reprint = true;
}

void
c_goto(Invocation* invo) {
   goto_byte(invo->line2);
}

// ":shell".
void
c_shell(Invocation* invo UNUSED) {
   do_shell(NULL, 0);
}

// ":preserve".
void
c_preserve(Invocation* invo UNUSED) {
   curBook->flags |= BF_PRESERVED;
   ml_preserve(curBook, true);
}

// ":recover".
void
c_recover(Invocation* invo) {
   // Set recoveryModeG right away to avoid the ATTENTION prompt.
   recoveryModeG = true;
   if (!check_changed(curBook, (p_awa ? CCGD_AW : 0)
              | CCGD_MULTWIN
              | (invo->forceit ? CCGD_FORCEIT : 0)
              | CCGD_EXCMD)
          && (*invo->arg == ZERO || setfname(curBook, invo->arg, NULL, true) == OK)
   )
      ml_recover(true);
   recoveryModeG = false;
}

// Command modifier used in a wrong way.  Also for other commands that can't appear at the toplevel
void
c_wrongmodifier(Invocation* invo) {
   invo->errmsg = ex_errmsg(e_invalid_command_str, invo->comm);
}

// Call 'findfunc' to obtain a list of file names.
private List *
call_findfunc(CS pat, int cmdcomplete) {
   Var   args[3];
   Var   returnVar;
   ScriptPos   saved_sctx = scriptPosG;
   args[0].tag = VAR_STRING;
   args[0].string = pat;
   args[1].tag = VAR_BOOL;
   args[1].number = cmdcomplete;
   args[2].tag = VAR_UNKNOWN;

   //Lock the text to prevent weird things from happening. Also disallow switching to another 
   //portal, it should not be needed and may end up in Insert mode in another book.
   ++textlock;

   ScriptPos* ctx = optGetScriptPos(S"findfunc");
   if (ctx)
      scriptPosG = *ctx;

   Callback* cb = curBook->o.findFn;
   int retval = call_callback(cb, -1, &returnVar, 2, args);

   scriptPosG = saved_sctx;

   --textlock;

   List *retlist = NULL;

   if (retval == OK) {
      if (returnVar.tag == VAR_LIST)
         retlist = list_copy(returnVar.list, false, false, get_copyID());
      else
         emsg(_(e_invalid_return_type_from_findfunc));

      clearVar(&returnVar);
   }

   return retlist;
}

//Find file names matching "pat" using @findfunc and return it in "files".
//Used for expanding the :find, :sfind and :tabfind command argument.
//Return OK on success and FAIL otherwise.
int
expand_findfunc(CS pat, OUT ExpandMatch* matches) {
   List* l = call_findfunc(pat, VVAL_TRUE);
   if (!l)
      return FAIL;

   int len = list_len(l);
   if (len == 0)       // empty List
      return FAIL;

   // Copy all the List items
   ListItem *li;
   FOR_ALL_LIST_ITEMS(l, li) {
      if (li->c.tag == VAR_STRING) {
         addExpandMatch(copyStr(li->c.string), OUT matches);
      }
   }
   list_free(l);

   return OK;
}

//Use 'findfunc' to find file 'findarg'. The 'count' argument is used to find the n'th matching file
private CS
findFnFindFile(CS findarg, int findarg_len, int count) {
   List* fname_list;
   CS ret_fname = NULL;
   int fname_count;

   Byte cc = findarg[findarg_len];
   findarg[findarg_len] = ZERO;

   fname_list = call_findfunc(findarg, VVAL_FALSE);
   fname_count = list_len(fname_list);

   if (fname_count == 0)
      showErrFmtMsg(_(e_cant_find_file_str_in_path), findarg);
   else {
      if (count > fname_count)
          showErrFmtMsg(_(e_no_more_file_str_found_in_path), findarg);
      else {
         ListItem *li = list_find(fname_list, count - 1);
         if (li && li->c.tag == VAR_STRING)
            ret_fname = copyStr(li->c.string);
      }
   }

   if (fname_list != NULL)
   list_free(fname_list);

    findarg[findarg_len] = cc;

    return ret_fname;
}

//Setter for the @findfunc option. Return NULL on success or an error message on failure.
CS
setFindFn(OptionChange* cha) {
   int   retval;

   if (cha->setScope == SET_LOCAL) {
      // book-local option set
      updateStringRef(cha);
      retval = optSetCallback(OUT curBook->o.findFn, cha->newVal.string);
   } else {
      // global option set
      retval = optSetCallback(OUT &findFnCb, *cha->ref.string);
      // when using :set, free the local callback
      if (cha->setScope == SET_GLOBAL)
         evFreeCallback(curBook->o.findFn);
   }

   if (retval == FAIL)
      return e_invalid_argument;

   //If the option value starts with <SID> or s:, then replace that with the script identifier.
   OptionRef ref = cha->ref;
   CS name = get_scriptlocal_funcname(*ref.string);
   if (name) {
      *ref.string = name;
   }

   return NULL;
}

# if defined(EXITFREE) || defined(PROTO)
void
doFreeFindFnOption(void) {
   evFreeCallback(&findFnCb);
}
# endif

// Mark the global @findfunc callback with "copyID" so that it is not garbage collected.
int
set_ref_in_findfunc(int copyID UNUSED) {
   int abort = memSetRefInCallback(&findFnCb, copyID);
   return abort;
}

//:sview [+command] file   split portal with new file, read-only
//:split [[+command] file]   split portal with current or new file
//:vsplit [[+command] file]   split portal vertically with current or new file
//:new [[+command] file]   split portal with no or new file
//:vnew [[+command] file]   split vertically portal with no or new file
//:sfind [+command] file   split portal with file in 'path'
//
//:tabedit         open new Tab with empty portal
//:tabedit [+command] file   open new Tab and edit "file"
//:tabnew [[+command] file]   just like :tabedit
//:tabfind [+command] file   open new Tab and find "file"
void
c_splitview(Invocation* invo) {
   Portal* old_curPor = curPor;
   CS fname = null;
   Boole use_tab = invo->id == C_tabedit
             || invo->id == C_tabfind
             || invo->id == C_tabnew;

   if (portErrorIfPopup(true))
      return;

   // A ":split" in the location portal works like ":new".  Don't want two
   // location portals.  But it's OK when doing ":tab split".
   if (isLocationListBook(curBook) && commModifierG.cmod_tab == 0) {
      if (invo->id == C_split)
         invo->id = C_new;
      if (invo->id == C_vsplit)
         invo->id = C_vnew;
   }

   if (invo->id == C_sfind || invo->id == C_tabfind) {
      CS file_to_find = NULL;
      FileSearchCtx* search_ctx = NULL;

      if (curBook->o.findFn) {
          fname = findFnFindFile(invo->arg, (int)STRLEN(invo->arg),
                      invo->addr_count > 0 ? invo->line2 : 1);
      } else {
         fname = findFileInPath(
                mbText(invo->arg), FNAME_MESS, true, curBook->fullFileName, 
                OUT &file_to_find, OUT &search_ctx
         );
         eeglFree(file_to_find);
         eeFindFile_cleanup(search_ctx);
      }
      if (!fname)
         goto theend;
      invo->arg = fname;
   }
   // Either open new tab or split the portal
   if (use_tab) {
      if (portNewTab(commModifierG.cmod_tab != 0 ? commModifierG.cmod_tab
             : invo->addr_count == 0 ? 0 : (int)invo->line2 + 1) != FAIL) {
         do_exedit(invo, old_curPor);

         // set the alternate book for the portal we came from
         if (curPor != old_curPor
               && portalIsValid(old_curPor)
               && old_curPor->book != curBook
               && (commModifierG.cmod_flags & CMOD_KEEPALT) == 0)
            old_curPor->altFnum = curBook->fiNum;
      }
   } ei (splitPortal(
             invo->addr_count > 0 ? (int)invo->line2 : 0, *invo->comm == 'v' ? WSP_VERT : 0
          ) != FAIL
   ) {
      // Reset 'scrollbind' when editing another file, but keep it when
      // doing ":split" without arguments.
      if (*invo->arg != ZERO)
          RESET_BINDING(curPor);
      else
          normPostProcessScrollbind(false);
      do_exedit(invo, old_curPor);
   }

theend:
   eeglFree(fname);
}

// Open a new tab.
void
tabNew(void) {
   Invocation   invo;
   CLEAR_FIELD(invo);
   invo.id = C_tabnew;
   invo.comm = (CS)"tabn";
   invo.arg = (CS)"";
   c_splitview(&invo);
}

// :tabnext command
void
c_tabnext(Invocation* invo) {
   int tabId;

   if (portErrorIfPopup(false))
      return;
   switch (invo->id) {
   case C_tabfirst:
   case C_tabrewind:
       gotoTabById(1);
       break;
   case C_tablast:
       gotoTabById(9999);
       break;
   case C_tabprevious:
   case C_tabNext:
      if (invo->arg && *invo->arg != ZERO) {
         CS p = invo->arg;
         CS p_save = p;

         tabId = parseLong(&p);
         if (p == p_save || *p_save == '-' || *p != ZERO || tabId == 0) {
            // No numbers as argument.
            invo->errmsg = ex_errmsg(e_invalid_argument_str, invo->arg);
            return;
         }
      } else {
         if (invo->addr_count == 0)
             tabId = 1;
         else {
             tabId = invo->line2;
             if (tabId < 1) {
            invo->errmsg = _(e_invalid_range);
            return;
             }
         }
      }
      gotoTabById(-tabId);
      break;
   default: // C_tabnext
      tabId = getTabRelatedArg(invo);
      if (invo->errmsg == NULL)
         gotoTabById(tabId);
      break;
   }
}

void
c_tabmove(Invocation* invo) {
   int tabId = getTabRelatedArg(invo);
   if (invo->errmsg == NULL)
      moveTab(tabId);
}

// :tabs command: List tabs and their contents.
void
c_tabs(Invocation* invo UNUSED) {
   Portal* po;
   int tabcount = 1;

   msg_start();
   msg_scroll = true;
   for (Tab* t = firstTabG; t && !gotInterruptG; t = t->next) {
      msg_putchar('\n');
      eeSnprintf(IObuff, IOSIZE, _("Tab %d"), tabcount++);
      msgOuttransDeco(IObuff, getDecoFlags(HLF_T));
      out_flush();       // output one line at a time
      ui_breakcheck();

      if (t  == curtab)
         po = firstPor;
      else
         po = t->firstPor;
      for ( ; po && !gotInterruptG; po = po->next) {
         msg_putchar('\n');
         msg_putchar(po == curPor ? '>' : ' ');
         msg_putchar(' ');
         msg_putchar(doWasBookChanged(po->book) ? '+' : ' ');
         msg_putchar(' ');
         if (bookSpName(po->book) != NULL)
            copySubstrToAllocation(OUT IObuff, (Text){bookSpName(po->book), IOSIZE - 1});
         else
            home_replace(po->book, po->book->currFileName, IObuff, IOSIZE, true);
         msg_outtrans(IObuff);
         out_flush();       // output one line at a time
         ui_breakcheck();
      }
   }
}

//}}}
//{{{misc2

//":mode": Set screen mode. If no argument given, just get the screen size and redraw.
void
c_mode(Invocation* invo) {
   if (*invo->arg == ZERO)
      shell_resized();
   else
      emsg(_(e_screen_mode_setting_not_supported));
}

// ":resize". set, increment or decrement current portal height
void
c_resize(Invocation* invo) {
   int      n;
   Portal   *po = curPor;

   if (invo->addr_count > 0) {
      n = invo->line2;
      for (po = firstPor; po->next && --n > 0; po = po->next)
          ;
   }

   n = atol((char *)invo->arg);
   if (commModifierG.cmod_split & WSP_VERT) {
      if (*invo->arg == '-' || *invo->arg == '+')
          n += po->width;
      ei (n == 0 && invo->arg[0] == ZERO)   // default is very wide
          n = 9999;
      portSetWidth(n, po);
   } else {
      if (*invo->arg == '-' || *invo->arg == '+')
         n += po->height;
      ei (n == 0 && invo->arg[0] == ZERO)   // default is very high
         n = 9999;
      portSetHeight(n, po);
   }
}

// ":find [+command] <file>" command.
void
c_find(Invocation* invo) {
   if (!portCheckCanSetCurBookForceIt(invo->forceit))
      return;

   CS fname = NULL;
   int count;
   CS file_to_find = NULL;
   FileSearchCtx* search_ctx = NULL;

   if (curBook->o.findFn) {
      fname = findFnFindFile(
          invo->arg, (int)STRLEN(invo->arg), invo->addr_count > 0 ? invo->line2 : 1
      );
   } else {
      fname = findFileInPath(
         mbText(invo->arg), FNAME_MESS, true, curBook->fullFileName, 
         OUT &file_to_find, OUT &search_ctx
      );
      
      if (invo->addr_count > 0) {
         //Repeat finding the file "count" times. This matters when it appears
         //several times in the path.
         count = invo->line2;
         while (fname && --count > 0) {
            eeglFree(fname);
            fname = findFileInPath(
               (Text){NULL, 0}, FNAME_MESS, false, curBook->fullFileName, 
               OUT &file_to_find, OUT &search_ctx
            );
         }
      }
      EE_CLEAR(file_to_find);
      eeFindFile_cleanup(search_ctx);
   }

   if (!fname)
      return;

   invo->arg = fname;
   do_exedit(invo, NULL);
   eeglFree(fname);
}

// ":open" simulation: for now works just like ":visual".
void
c_open(Invocation* invo) {
   RegMatch   regmatch;
   CS p;

   curPor->cursor.lnum = invo->line2;
   beginline(BL_SOL | BL_FIX);
   if (*invo->arg == '/') {
      // ":open /pattern/": put cursor in column found with pattern
      ++invo->arg;
      p = skip_regexp(invo->arg, '/', true);
      *p = ZERO;
      regmatch.regprog = compileRegexp(invo->arg, RE_MAGIC);
      if (regmatch.regprog) {
         //make a copy of the line, when searching for a mark it might be flushed
         CS line = copyStr(ml_get_curline());

         regmatch.rm_ic = p_ic;
         if (eeRegexec(&regmatch, line, (ColNr)0))
            curPor->cursor.col = (ColNr)(regmatch.startp[0] - line);
         else
            emsg(_(e_no_match));
         eeRegFree(regmatch.regprog);
         eeglFree(line);
      }
      // Move to the ZERO, ignore any other arguments.
      invo->arg += STRLEN(invo->arg);
   }
   check_cursor();

   invo->id = C_visual;
   do_exedit(invo, NULL);
}

// ":edit", ":badd", ":balt", ":visual".
void
c_edit(Invocation* invo) {
   CS fullFName = invo->id == C_enew ? NULL : invo->arg;

   // Exclude commands which keep the portal's current book
   if ( invo->id != C_badd
          && invo->id != C_balt
          // All other commands must obey 'portfixbuf' / ! rules
          && (!isSameFile(0, fullFName) && !portCheckCanSetCurBookForceIt(invo->forceit))
   )
      return;
      
   if (invo->id == C_edit && STRCHR(invo->arg, ' ') != NULL) { //:e a.txt b.txt
      ArrayList names = splitBySpace(invo->arg);
      for (int i = 0; i < names.len; i++) {
         CS name = ((Arr(CS))names.c)[i];
         Invocation oneNameArg = *invo;
         oneNameArg.arg = name;
         do_exedit(&oneNameArg, NULL);
      }
      return;
   } else {
      do_exedit(invo, NULL);
   }
}

//":edit <file>" command and alike.
void
do_exedit(Invocation* invo, Portal* old_curPor) {      // curPor before doing a split or NULL
   if ((invo->id != C_pedit && portErrorIfPopup(false)) || portErrorIfTermPopup())
      return;

   if ((invo->id == C_new || invo->id == C_tabnew || invo->id == C_tabedit || invo->id == C_vnew)
         && *invo->arg == ZERO
   ) {
      // ":new" or ":tabnew" without argument: edit a new empty book
      setpcmark();
      (void)startEditingFile(
         0, NULL, NULL, invo, ECMD_ONE, ECMD_HIDE + (invo->forceit ? ECMD_FORCEIT : 0),
         old_curPor ? null : curPor
      );
   } ei ((invo->id != C_split && invo->id != C_vsplit) || *invo->arg != ZERO) {
      //Can't edit another file when "textlock" or "curBookLock" is set.
      //Only ":edit" or ":script" can bring us here, others are stopped earlier.
      if (*invo->arg != ZERO && text_or_buf_locked())
         return;
      Boole modifiable = false;

      if (invo->id == C_enew) //immutability doesn't make sense in an empty book
         modifiable = true;
      if (invo->id != C_balt && invo->id != C_badd)
         setpcmark();
      if (startEditingFile(
           0, 
           (invo->id == C_enew ? NULL : invo->arg),
           NULL, invo, invo->higherOrderLnum,
           ECMD_HIDE | (invo->forceit ? ECMD_FORCEIT : 0)
                // after a split, we can use an existing book
                | (old_curPor ? ECMD_OLDBUF : 0)
                | (invo->id == C_badd ? ECMD_ADDBUF : 0)
                | (invo->id == C_balt ? ECMD_ALTBUF : 0)
                | (modifiable ? ECMD_MODIFIABLE : 0),
                old_curPor == NULL ? curPor : NULL
         ) == FAIL
      ){
         //Editing the file failed. If the portal was split, close it.
         if (old_curPor) {
            //Reset the error/interrupt/exception state here so that
            //aborting() returns false when closing a portal.
            Cleanup   cs;
            enter_cleanup(OUT &cs);
            closePortal(curPor, false);

            //Restore the error/interrupt/exception state if not discarded by a new aborting
            //error, interrupt, or uncaught exception.
            leave_cleanup(&cs);
         }
      } ei (curBook->o.modifiable && curBook->countPortals == 1) {
         //When editing an already visited book, @modifiable won't be set but the previous value 
         //is kept. With ":view" and ":sview" we want the file to be readonly, except when 
         //another portal is editing the same book.
         curBook->o.modifiable = false;
      }
   } else  {
      if (invo->higherOrderComm)
         do_cmd_argument(invo->higherOrderComm);
      check_arg_idx(curPor);
   }

   // if ":split file" worked, set alternate file name in old portal to new file
   if (old_curPor
          && *invo->arg != ZERO
          && curPor != old_curPor
          && portalIsValid(old_curPor)
          && old_curPor->book != curBook
          && (commModifierG.cmod_flags & CMOD_KEEPALT) == 0) {
      old_curPor->altFnum = curBook->fiNum;
   } 

   ex_no_reprint = true;
}

void
c_swapname(Invocation* invo UNUSED) {
   if (curBook->mem.mfile == NULL || curBook->mem.mfile->fName == NULL)
      msg(_("No swap file"));
   else
      msg(curBook->mem.mfile->fName);
}

//":syncbind" forces all 'scrollbind' portals to have the same relative offset.
//(1998-11-02 16:21:01  R. Edward Ralston <eralston@computer.org>)
void
c_syncbind(Invocation* invo UNUSED) {
   Portal   *po;
   Portal   *save_curPor = curPor;
   Book   *save_curbuf = curBook;
   long   topline;
   long   y;
   LineNr   old_linenr = curPor->cursor.lnum;

   setpcmark();

   // determine max topline
   if (curPor->o.scrollBind) {
      topline = curPor->topLine;
      FOR_ALL_PORTALS(po) {
         if (po->o.scrollBind && po->book) {
            y = po->book->mem.lineCount - curPor->o.scrollOff;
            if (topline > y)
               topline = y;
          }
      }
      if (topline < 1)
         topline = 1;
   } else {
      topline = 1;
   }


   // Set all scrollbind portals to the same topline.
   FOR_ALL_PORTALS(curPor) {
      if (curPor->o.scrollBind) {
         curBook = curPor->book;
         y = topline - curPor->topLine;
         if (y > 0)
            scrollup(y, true);
         else
            scrolldown(-y, true);
         curPor->scbindPos = topline;
         redraw_later(UPD_VALID);
         cursor_correct();
         curPor->statusLineNeedsRedraw = true;
      }
   }
   curPor = save_curPor;
   curBook = save_curbuf;
   if (curPor->o.scrollBind) {
      did_syncbind = true;
      checkpcmark();
      if (old_linenr != curPor->cursor.lnum) {
         Byte ctrl_o[2] = {Ctrl_O, 0};
         insertIntoTypebuf(ctrl_o, REMAP_NONE, 0, true, false);
      }
   }
}

void
c_read(Invocation* invo) {
   if (invo->usefilter) {        // :r!cmd
      do_bang(1, invo, false, false, true);
      return;
   }

   if (u_save(invo->line2, (LineNr)(invo->line2 + 1)) == FAIL)
      return;

   int      i;
   if (*invo->arg == ZERO) {
       if (check_fname() == FAIL)   // check for no file name
      return;
       i = readfile(curBook->fullFileName, curBook->currFileName,
          invo->line2, (LineNr)0, (LineNr)MAXLNUM, invo, 0);
   } else {
       (void)setaltfname(invo->arg, invo->arg, (LineNr)1);
       i = readfile(invo->arg, NULL, invo->line2, (LineNr)0, (LineNr)MAXLNUM, invo, 0);

   }
   if (i != OK) {
      if (!aborting())
          showErrFmtMsg(_(e_cant_open_file_str), invo->arg);
   } else {
      drawCurBookLater(UPD_VALID);
   }
}

private CS prev_dir = NULL;

#if defined(EXITFREE) || defined(PROTO)
void
free_cd_dir(void) {
   EE_CLEAR(prev_dir);
   EE_CLEAR(globaldir);
}
#endif

// Get the previous directory for the given chdir scope.
private CS
get_prevdir(CdScopeKind scope) {
   if (scope == CDSCOPE_WINDOW)
      return curPor->prevdir;
   ei (scope == CDSCOPE_TABPAGE)
      return curtab->prevdir;
   return prev_dir;
}

//Deal with the side effects of changing the current directory.
//When 'scope' is CDSCOPE_TABPAGE then this was after an ":tcd" command.
//When 'scope' is CDSCOPE_WINDOW then this was after an ":lcd" command.
void
post_chdir(CdScopeKind scope) {
   if (scope != CDSCOPE_WINDOW)
      // Clear tab local directory for both :cd and :tcd
      EE_CLEAR(curtab->localdir);
   EE_CLEAR(curPor->localDir);
   if (scope != CDSCOPE_GLOBAL) {
      CS pdir = get_prevdir(scope);

      // If still in the global directory, need to remember current
      // directory as the global directory.
      if (!globaldir && pdir)
         globaldir = copyStr(pdir);

      // Remember this local directory for the portal.
      if (mch_dirname(nameBuffG, MAXPATHL) == OK) {
         if (scope == CDSCOPE_TABPAGE)
            curtab->localdir = copyStr(nameBuffG);
         else
            curPor->localDir = copyStr(nameBuffG);
      }
   } else {
      // We are now in the global directory, no need to remember its name.
      EE_CLEAR(globaldir);
   }

   shorten_fnames(true);
}

// Trigger DirChangedPre for "acmd_fname" with directory "new_dir".
void
trigger_DirChangedPre(CS acmd_fname, CS new_dir) {
   Bag       *v_event;
   SaveVEvent  save_v_event;

   v_event = get_v_event(&save_v_event);
   (void)bagAddString(v_event, S"directory", new_dir);
   bagSetItemsRo(v_event);
   applyAutocomms(EVENT_DIRCHANGEDPRE, acmd_fname, new_dir, false, curBook);
   restore_v_event(v_event, &save_v_event);
}

//Change directory function used by :cd/:tcd/:lcd commands and the
//chdir() function.
//scope == CDSCOPE_WINDOW: changes the portal-local directory
//scope == CDSCOPE_TABPAGE: changes the tab-local directory
//Otherwise: change the global directory
//Return true if the directory is successfully changed.
int
changedir_func(CS new_dir, CdScopeKind scope){
   CS pdir = NULL;
   int      dir_differs;
   CS acmd_fname = NULL;
   CS* pp;
   Byte   *tofree;

   if (new_dir == NULL || allbuf_locked()) {
       return false;
   }

   // ":cd -": Change to previous directory
   if (STRCMP(new_dir, "-") == 0) {
      pdir = get_prevdir(scope);
      if (pdir == NULL) {
         emsg(_(e_no_previous_directory));
         return false;
      }
      new_dir = pdir;
   }

   // Save current directory for next ":cd -"
   pdir = (mch_dirname(nameBuffG, MAXPATHL) == OK) ? copyStr(nameBuffG) : null;

   // ":cd" means: go to home directory.
   if (*new_dir == ZERO) {
      // use nameBuffG for home directory name
      doExpandEnv(OUT nameBuffTextG, S"$HOME");
      new_dir = nameBuffG;
   }
   dir_differs = pdir == NULL || pathcmp(pdir, new_dir, -1) != 0;
   if (dir_differs) {
      if (scope == CDSCOPE_WINDOW)
         acmd_fname = (CS)"window";
      ei (scope == CDSCOPE_TABPAGE)
         acmd_fname = (CS)"tabpage";
      else
         acmd_fname = (CS)"global";
      trigger_DirChangedPre(acmd_fname, new_dir);

      if (eeChdir(new_dir)) {
          emsg(_(e_command_failed));
          eeglFree(pdir);
          return false;
      }
   }

   if (scope == CDSCOPE_WINDOW)
      pp = &curPor->prevdir;
   ei (scope == CDSCOPE_TABPAGE)
      pp = &curtab->prevdir;
   else
      pp = &prev_dir;
   tofree = *pp;  // new_dir may use this
   *pp = pdir;

   post_chdir(scope);

   if (dir_differs)
      applyAutocomms(EVENT_DIRCHANGED, acmd_fname, new_dir, false, curBook);
   eeglFree(tofree);
   return true;
}

// ":cd", ":tcd", ":lcd", ":chdir" ":tchdir" and ":lchdir".
void
c_cd(Invocation* invo) {
   CS new_dir = invo->arg;
   CdScopeKind   scope = CDSCOPE_GLOBAL;

   if (invo->id == C_lcd || invo->id == C_lchdir)
      scope = CDSCOPE_WINDOW;
   ei (invo->id == C_tcd || invo->id == C_tchdir)
      scope = CDSCOPE_TABPAGE;

   if (changedir_func(new_dir, scope) && (keyWasTypedG || p_verbose >= 5))
      // Echo the new current directory if the command was typed.
      c_pwd(invo);
}

// ":pwd".
void
c_pwd(Invocation* invo UNUSED) {
   if (mch_dirname(nameBuffG, MAXPATHL) == OK) {
      if (p_verbose > 0) {
         CS context = S"global";

         if (curPor->localDir)
            context = S"window";
         ei (curtab->localdir)
            context = S"tabpage";
         smsg("[%s] %s", context, (char *)nameBuffG);
      } else
         msg(nameBuffG);
   } else
      emsg(_(e_directory_unknown));
}

// ":=".
void
c_equal(Invocation* invo) {
   smsg("%ld", (long)invo->line2);
   mayPrint(invo);
}

void
c_sleep(Invocation* invo) {
   int      n;
   long   len;

   if (cursor_valid()) {
      n = curPor->portalRow + curPor->cursorRow - msg_scrolled;
      if (n >= 0)
         windgoto(n, curPor->portalCol + curPor->cursorCol);
   }

   len = invo->line2;
   switch (*invo->arg) {
   case 'm': break;
   case ZERO: len *= 1000L; break;
   default: showErrFmtMsg(_(e_invalid_argument_str), invo->arg); return;
   }

   // Hide the cursor if invoked with !
   do_sleep(len, invo->forceit);
}

// Sleep for "msec" milliseconds, but keep checking for a CTRL-C every second.
// Hide the cursor if "hide_cursor" is true.
void
do_sleep(long msec, int hide_cursor) {
   long   done = 0;
   long   wait_now;
   Elapsed   start_tv;

   // Remember at what time we started, so that we know how much longer we
   // should wait after waiting for a bit.
   ELAPSED_INIT(start_tv);

   if (hide_cursor)
      cursor_sleep();
   else
      cursor_on();

   out_flush();
   while (!gotInterruptG && done < msec) {
      wait_now = msec - done > 1000L ? 1000L : msec - done; {
          long    due_time = check_due_timer();

          if (due_time > 0 && due_time < wait_now)
         wait_now = due_time;
      }
      if (has_any_channel() && wait_now > 20L)
          wait_now = 20L;
      ui_delay(wait_now, true);

      if (has_any_channel())
          ui_breakcheck_force(true);
      else
          ui_breakcheck();
      // Process the clientserver messages that may have been received in the call to ui_breakcheck() 
      // when the GUI is in use. This may occur when running a test case.
      parse_queued_messages();

      // actual time passed
      done = ELAPSED_FUNC(start_tv);
   }

   // If CTRL-C was typed to interrupt the sleep, drop the CTRL-C from the
   // input buffer, otherwise a following call to input() fails.
   if (gotInterruptG)
      (void)vpeekc();

   if (hide_cursor)
      cursor_unsleep();
}

void
c_wincmd(Invocation* invo) {
   int      xchar = ZERO;
   CS p;

   if (*invo->arg == 'g' || *invo->arg == Ctrl_G) {
      // CTRL-W g and CTRL-W CTRL-G  have an extra command character
      if (invo->arg[1] == ZERO) {
         emsg(_(e_invalid_argument));
         return;
      }
      xchar = invo->arg[1];
      p = invo->arg + 2;
   } else
      p = invo->arg + 1;

   set_nextcmd(OUT invo, p);
   p = skipwhite(p);
   if (*p != ZERO && !isComment(p) && invo->nextComm == NULL)
      emsg(_(e_invalid_argument));
   ei (!invo->skip) {
      // Pass flags on for ":vertical wincmd ]".
      postponed_split_flags = commModifierG.cmod_split;
      postponed_split_tab = commModifierG.cmod_tab;
      doPortal(*invo->arg, invo->addr_count > 0 ? invo->line2 : 0L, xchar);
      postponed_split_flags = 0;
      postponed_split_tab = 0;
   }
}

// ":winpos".
void
c_portPos(Invocation* invo) {
   int      x, y;
   CS arg = invo->arg;
   CS p;

   if (*arg == ZERO) {
       emsg(_(e_obtaining_window_position_not_implemented_for_this_platform));
   } else {
      x = parseLong(&arg);
      arg = skipwhite(arg);
      p = arg;
      y = parseLong(&arg);
      if (*p == ZERO || *arg != ZERO) {
         emsg(_(e_winpos_requires_two_number_arguments));
         return;
      }
      if (*termCodesG[KS_CWP])
         term_set_winpos(x, y);
    }
}

// Handle commands that work like operators: ":delete", ":yank", ":>" and ":<"
void
c_operators(Invocation* invo) {
   Operator   oper;

   clear_oparg(&oper);
   oper.regname = invo->regname;
   oper.start.lnum = invo->line1;
   oper.end.lnum = invo->line2;
   oper.line_count = invo->line2 - invo->line1 + 1;
   oper.motion_type = MLINE;
   virtual_op = false;
   if (invo->id != C_yank) {  // position cursor for undo
      setpcmark();
      curPor->cursor.lnum = invo->line1;
      beginline(BL_SOL | BL_FIX);
   }

   if (VIsual_active)
   end_visual_mode();

   switch (invo->id) {
   case C_delete:
      oper.opTy = OP_DELETE;
      op_delete(&oper);
      break;

   case C_yank:
      oper.opTy = OP_YANK;
      (void)op_yank(&oper, false, true);
      break;

   default:    // C_rshift or C_lshift
      if (invo->id == C_rshift)
         oper.opTy = OP_RSHIFT;
      else
         oper.opTy = OP_LSHIFT;
      op_shift(&oper, false, invo->amount);
      break;
   }
   virtual_op = MAYBE;
   mayPrint(invo);
}

// ":put".
void
c_put(Invocation* invo) {
   // ":0put" works like ":1put!".
   if (invo->line2 == 0) {
      invo->line2 = 1;
      invo->forceit = true;
   }
   curPor->cursor.lnum = invo->line2;
   check_cursor_col();
   do_put(invo->regname, NULL, invo->forceit ? BACKWARD : FORWARD, 1L, PUT_LINE|PUT_CURSLINE);
}

// ":iput".
void
c_iput(Invocation* invo) {
   // ":0iput" works like ":1iput!".
   if (invo->line2 == 0) {
      invo->line2 = 1;
      invo->forceit = true;
   }
   curPor->cursor.lnum = invo->line2;
   check_cursor_col();
   do_put(
      invo->regname, NULL, invo->forceit ? BACKWARD : FORWARD, 1L,
      PUT_LINE|PUT_CURSLINE|PUT_FIXINDENT
   );
}

// Handle ":copy" and ":move".
void
c_copymove(Invocation* invo) {
   long n = doGetCommandAddress(invo, &invo->arg, invo->addressKind, false, false, false, 1);
   if (invo->arg == NULL) {      // error detected
      invo->nextComm = NULL;
      return;
   }
   get_flags(invo);

   // move or copy lines from 'invo->line1'-'invo->line2' to below line 'n'
   if (n == MAXLNUM || n < 0 || n > curBook->mem.lineCount) {
      emsg(_(e_invalid_range));
      return;
   }

   if (invo->id == C_move) {
      if (do_move(invo->line1, invo->line2, n) == FAIL)
         return;
   } else
      doCopy(invo->line1, invo->line2, n);
   u_clearline();
   beginline(BL_SOL | BL_FIX);
   mayPrint(invo);
}

// Print the current line if flags were given to the command.
private void
mayPrint(Invocation* invo) {
   if (invo->flags != 0) {
      print_line(curPor->cursor.lnum, (invo->flags & EXFLAG_LIST));
      ex_no_reprint = true;
   }
}

// ":join".
void
c_join(Invocation* invo) {
   curPor->cursor.lnum = invo->line1;
   if (invo->line1 == invo->line2) {
      if (invo->addr_count >= 2)   // :2,2join does nothing
         return;
      if (invo->line2 == curBook->mem.lineCount) {
         beep_flush();
         return;
      }
      ++invo->line2;
   }
   (void)jugJoinLinesUnderCursor(invo->line2 - invo->line1 + 1, !invo->forceit, true, true, true);
   beginline(BL_WHITE | BL_FIX);
   mayPrint(invo);
}

// ":[addr]@r" or ":[addr]*r": execute register
void
c_at(Invocation* invo) {
   int prev_len = typeBufG.validLen;

   curPor->cursor.lnum = invo->line2;
   check_cursor_col();

   // get the register name.  No name means to use the previous one
   int c = *invo->arg;
   if (c == ZERO || (c == '*' && *invo->comm == '*'))
      c = '@';
   // Put the register in the typeahead buffer with the "silent" flag.
   if (do_execreg(c, true, true, true) == FAIL) {
      beep_flush();
      return;
   }

    int   save_efr = executingFromRegG;

   executingFromRegG = true;

   // Execute from the typeahead buffer.
   // Continue until the stuff buffer is empty and all added characters have been consumed.
   while (!stuff_empty() || typeBufG.validLen > prev_len)
      (void)doCommand(NULL, getexline, NULL, DOCMD_NOWAIT|DOCMD_VERBOSE);

   executingFromRegG = save_efr;
}

// ":!".
void
c_bang(Invocation* invo) {
   do_bang(invo->addr_count, invo, invo->forceit, true, true);
}

// ":undo".
void
c_undo(Invocation* invo) {
   if (invo->addr_count == 1)       // :undo 123
      undo_time(invo->line2, false, false, true);
   else
      u_undo(1);
}

void
c_wundo(Invocation* invo) {
   Byte hash[UNDO_HASH_SIZE];

   u_compute_hash(hash);
   u_write_undo(invo->arg, invo->forceit, curBook, hash);
}

void
c_rundo(Invocation* invo) {
   Byte hash[UNDO_HASH_SIZE];

   u_compute_hash(hash);
   u_read_undo(invo->arg, hash, NULL);
}

// ":redo".
void
c_redo(Invocation* invo UNUSED) {
   u_redo(1);
}

// ":earlier" and ":later".
void
c_later(Invocation* invo) {
   long   count = 0;
   int      sec = false;
   int      file = false;
   CS p = invo->arg;

   if (*p == ZERO)
      count = 1;
   ei (SAFE_isdigit(*p)) {
      count = parseLong(&p);
      switch (*p) {
         case 's': ++p; sec = true; break;
         case 'm': ++p; sec = true; count *= 60; break;
         case 'h': ++p; sec = true; count *= 60 * 60; break;
         case 'd': ++p; sec = true; count *= 24 * 60 * 60; break;
         case 'f': ++p; file = true; break;
      }
   }

   if (*p != ZERO)
      showErrFmtMsg(_(e_invalid_argument_str), invo->arg);
   else
      undo_time(invo->id == C_earlier ? -count : count, sec, file, false);
}

// ":redir": start/stop redirection.
void
c_redir(Invocation* invo) {
   CS mode;
   CS fname;
   CS arg = invo->arg;

   if (redir_execute) {
      emsg(_(e_cannot_use_redir_inside_execute));
      return;
   }

   if (caseInsensitiveCompare(invo->arg, "END") == 0)
      close_redir();
   else {
      if (*arg == '>') {
         ++arg;
         if (*arg == '>') {
            ++arg;
            mode = S"a";
         } else
            mode = S"w";
         arg = skipwhite(arg);

         close_redir();

         //Expand environment variables and "~/".
         fname = doExpandEnvInMultiplePaths(arg);
         if (!fname)
            return;

         redir_fd = doOpenCommandsFile(fname, invo->forceit, mode);
         eeglFree(fname);
      } ei (*arg == '@') {
          // redirect to a register a-z (resp. A-Z for appending)
          close_redir();
          ++arg;
          if (ASCII_ISALPHA(*arg)
             || *arg == '*'
             || *arg == '+'
             || *arg == '"'
         ) {
            redir_reg = *arg++;
            if (*arg == '>' && arg[1] == '>')  // append
               arg += 2;
            else {
               // Can use both "@a" and "@a>".
               if (*arg == '>')
                  arg++;
               // Make register empty when not using @A-@Z and the command is valid.
               if (*arg == ZERO && !SAFE_isupper(redir_reg))
                  write_reg_contents(redir_reg, (CS)"", -1, false);
            }
         }
         if (*arg != ZERO) {
            redir_reg = 0;
            showErrFmtMsg(_(e_invalid_argument_str), invo->arg);
         }
      } ei (*arg == '=' && arg[1] == '>') {
         int append;

         // redirect to a variable
         close_redir();
         arg += 2;

         if (*arg == '>') {
            ++arg;
            append = true;
         } else
            append = false;

         if (var_redir_start(skipwhite(arg), append) == OK)
            redir_vname = 1;
      }

      // TODO: redirect to a buffer
      else
         showErrFmtMsg(_(e_invalid_argument_str), invo->arg);
   }

   // Make sure redirection is not off.  Can happen for commline completion
   // that indirectly invokes a command to catch its output.
   if (redir_fd || redir_reg || redir_vname)
      redir_off = false;
}

// ":redraw": force redraw, with clear for ":redraw!".
void
c_redraw(Invocation* invo) {
   redraw_cmd(invo->forceit);
}

// ":redraw": force redraw, with clear if "clear" is true.
void
redraw_cmd(int clear) {
   int save_isRedrawingDisabledG = isRedrawingDisabledG;
   isRedrawingDisabledG = 0;

   int save_p_lz = p_lz;
   p_lz = false;

   validate_cursor();
   update_topline();
   drawUpdateScreen(clear ? UPD_CLEAR : VIsual_active ? UPD_INVERTED : 0);
   isRedrawingDisabledG = save_isRedrawingDisabledG;
   p_lz = save_p_lz;

   // After drawing the statusline screen_attr may still be set.
   drawStopHilite();

   // Reset msg_didout, so that a message that's there is overwritten.
   msg_didout = false;
   msgColG = 0;

   // No need to wait after an intentional redraw.
   need_wait_return = false;

    // When invoked from a callback or autocmd the command line may be active.
   if (stateG & MODE_COMMLINE)
      redrawCommline();

   out_flush();
}

// ":redrawstatus": force redraw of status line(s)
void
c_redrawstatus(Invocation* invo) {
   if (invo->forceit)
      status_redraw_all();
   else
      drawAllStatusLinesOfCurBookLater();
   if (msg_scrolled && (stateG & MODE_COMMLINE))
      return;  // redraw later

   int save_isRedrawingDisabledG = isRedrawingDisabledG;
   isRedrawingDisabledG = 0;

   int save_p_lz = p_lz;
   p_lz = false;

   if (stateG & MODE_COMMLINE)
      redraw_statuslines();
   else
      drawUpdateScreen(VIsual_active ? UPD_INVERTED : 0);
   isRedrawingDisabledG = save_isRedrawingDisabledG;
   p_lz = save_p_lz;
   out_flush();
}

// ":redrawtabpanel": force redraw of the tabpanel
void
c_redrawtabpanel(Invocation* invo UNUSED) {
   int save_isRedrawingDisabledG = isRedrawingDisabledG;
   isRedrawingDisabledG = 0;

   int save_p_lz = p_lz;
   p_lz = false;

   draw_tabpanel();

   isRedrawingDisabledG = save_isRedrawingDisabledG;
   p_lz = save_p_lz;
   out_flush();
}

private void
close_redir(void) {
   if (redir_fd) {
      fclose(redir_fd);
      redir_fd = NULL;
   }
   redir_reg = 0;
   if (redir_vname) {
      var_redir_stop();
      redir_vname = 0;
   }
}

int
eeMkdir_emsg(CS name, int prot UNUSED) {
   if (eeMkdir(name, prot) != 0) {
      showErrFmtMsg(_(e_cannot_create_directory_str), name);
      return FAIL;
   }
   return OK;
}

//Open a file for writing for a command, with some checks. Return file descriptor, NULL on failure
FILE *
doOpenCommandsFile(CS fname, int forceit, CS mode) { //"w" for create new file or "a" for append
   FILE* fd;

   // with Unix it is possible to open a directory
   if (mch_isdir(fname)) {
      showErrFmtMsg(_(e_str_is_directory), fname);
      return NULL;
   }
   if (!forceit && mode[0] != 'a' && eeFexists(fname)) {
      showErrFmtMsg(_(e_str_exists_add_bang_to_override), fname);
      return NULL;
   }

   if ((fd = FOPEN(fname, mode)) == NULL)
      showErrFmtMsg(_(e_cannot_open_str_for_writing_2), fname);

   return fd;
}

// ":mark" and ":k".
void
c_mark(Invocation* invo) {
   if (*invo->arg == ZERO) {     // No argument?
      emsg(_(e_argument_required));
      return;
   }

   if (invo->arg[1] != ZERO) {  // more than one character? showErrFmtMsg(_(e_trailing_characters_str), invo->arg);
      return;
   }

   Pos pos = curPor->cursor;      // save curPor->cursor
   curPor->cursor.lnum = invo->line2;
   beginline(BL_WHITE | BL_FIX);
   if (setmark(*invo->arg) == FAIL)   // set mark
      emsg(_(e_argument_must_be_letter_or_forward_backward_quote));
   curPor->cursor = pos;      // restore curPor->cursor
}

// Update topLine, leftCol and the cursor position.
void
update_topline_cursor(void) {
   check_cursor();      // put cursor on valid line
   update_topline();
   if (!curPor->o.wrap)
      validate_cursor();
   update_curswant();
}

//Save the current stateG and go to Normal mode. Return true if the typeahead could be saved.
int
save_current_state(SaveState* sst) {
   sst->save_msg_scroll = msg_scroll;
   sst->save_restart_edit = restart_edit;
   sst->save_msg_didout = msg_didout;
   sst->save_State = stateG;
   sst->save_finish_op = finish_op;
   sst->save_opcount = opcount;
   sst->save_reg_executing = reg_executing;
   sst->save_pending_end_reg_executing = pending_end_reg_executing;

   msg_scroll = false;          // no msg scrolling in Normal mode
   restart_edit = 0;          // don't go to Insert mode

   // Save the current typeahead.  This is required to allow using ":normal" from an event handler
   // and makes sure we don't hang when the argument ends with half a command.
   save_typeahead(&sst->tabuf);
   return sst->tabuf.typebuf_valid;
}

void
restore_current_state(SaveState* sst) {
   // Restore the previous typeahead.
   restore_typeahead(&sst->tabuf, false);

   msg_scroll = sst->save_msg_scroll;
   restart_edit = sst->save_restart_edit;
   finish_op = sst->save_finish_op;
   opcount = sst->save_opcount;
   reg_executing = sst->save_reg_executing;
   pending_end_reg_executing = sst->save_pending_end_reg_executing;
   msg_didout |= sst->save_msg_didout;   // don't reset msg_didout now

   // Restore the state (needed when called from a function executed for
   // 'indentexpr'). Update the mouse and cursor, they may have changed.
   stateG = sst->save_State;
   ui_cursor_shape();      // may show different cursor shape
}

// ":normal[!] {commands}": Execute normal mode commands.
void
c_normal(Invocation* invo) {
   SaveState saveState;
   int      l;

   if (ex_normal_lock > 0) {
      emsg(_(e_not_allowed_here));
      return;
   }
   if (ex_normal_busy >= MAX_MAPPING_RECURSION) {
      emsg(_(e_recursive_use_of_normal_too_deep));
      return;
   }

   // vgetc() expects a CSI and K_SPECIAL to have been escaped.  Don't do
   // this for the K_SPECIAL leading byte, otherwise special keys will not work.
   int   len = 0;

   // Count the number of characters to be escaped.
   CS arg = NULL;
   for (CS p = invo->arg; *p != ZERO; ++p) {
      for (l = utfCharLen(p) - 1; l > 0; --l) {
         if (*++p == K_SPECIAL)     // trailbyte K_SPECIAL or CSI
            len += 2;
      } 
   }
   if (len > 0) {
      arg = alloc(STRLEN(invo->arg) + len + 1);
      len = 0;
      for (CS p = invo->arg; *p != ZERO; ++p) {
         arg[len++] = *p;
         for (l = utfCharLen(p) - 1; l > 0; --l) {
            arg[len++] = *++p;
            if (*p == K_SPECIAL) {
                arg[len++] = KS_SPECIAL;
                arg[len++] = KE_FILLER;
            }
         }
         arg[len] = ZERO;
      }
   }

   ++ex_normal_busy;
   if (save_current_state(&saveState)) {
      // Repeat the :normal command for each line in the range.  When no
      // range given, execute it just once, without positioning the cursor first.
      do {
         if (invo->addr_count != 0) {
            curPor->cursor.lnum = invo->line1++;
            curPor->cursor.col = 0;
            check_cursor_moved(curPor);
         }

         exec_normal_cmd(arg
              ? arg
              : invo->arg, invo->forceit ? REMAP_NONE : REMAP_YES, false);
      } while (invo->addr_count > 0 && invo->line1 <= invo->line2 && !gotInterruptG);
   }

   // Might not return to the main loop when in an event handler.
   update_topline_cursor();

   restore_current_state(&saveState);
   --ex_normal_busy;
   setmouse();
   ui_cursor_shape();      // may show different cursor shape

   eeglFree(arg);
}

// ":startinsert", ":startreplace" and ":startgreplace"
void
c_startinsert(Invocation* invo) {
   if (invo->forceit) {
      // cursor line can be zero on startup
      if (!curPor->cursor.lnum)
         curPor->cursor.lnum = 1;
      set_cursor_for_append_to_line();
   }
   // Ignore this when running in an active terminal.
   if (term_job_running(curBook->term))
      return;

   // Ignore the command when already in Insert mode.  Inserting an
   // expression register that invokes a function can do this.
   if (stateG & MODE_INSERT)
      return;

   restart_edit = 'a';

   if (!invo->forceit) {
      if (invo->id == C_startinsert)
         restart_edit = 'i';
      curPor->cursWant = 0;       // avoid MAXCOL
   }

   if (VIsual_active)
      showmode();
}

// ":stopinsert"
void
c_stopinsert(Invocation* invo UNUSED) {
   restart_edit = 0;
   stop_insert_mode = true;
   // when called from remote_expr in insert mode, make sure insert mode is
   // ended by adding K_NOP to the typeahead buffer
   if (vgetcBusyG)
      ins_char_typebuf(K_NOP, 0);
   clearmode();
}

// Execute normal mode command "cmd". "remap" can be REMAP_NONE or REMAP_YES.
void
exec_normal_cmd(CS cmd, int remap, int silent) {
   // Stuff the argument into the typeahead buffer.
   insertIntoTypebuf(cmd, remap, 0, true, silent);
   exec_normal(false, false, false);
}

// Execute normalAction() until there is no typeahead left.
// When "use_vpeekc" is true use vpeekc() to check for available chars.
void
exec_normal(int was_typed, int use_vpeekc, int may_use_terminal_loop UNUSED) {
   Operator   oper;
   int      c;

   // When calling vpeekc() from feedkeys() it will return Ctrl_C when there
   // is nothing to get, so also check for Ctrl_C.
   clear_oparg(&oper);
   finish_op = false;
   while ((!stuff_empty()
      || ((was_typed || !typebuf_typed()) && typeBufG.validLen > 0)
      || (use_vpeekc && (c = vpeekc()) != ZERO && c != Ctrl_C)) && !gotInterruptG
   ) {
      update_topline_cursor();
      if (may_use_terminal_loop && term_use_loop()
         && oper.opTy == OP_NOP && oper.regname == ZERO
         && !VIsual_active
      ) {
         // If terminal_loop() returns OK we got a key that is handled
         // in Normal model.  With FAIL we first need to position the
         // cursor and the screen needs to be redrawn.
         if (terminal_loop(true) == OK)
            normalAction(&oper, true);
      } else {
          // execute a Normal mode cmd
          normalAction(&oper, true);
      }
   }
}

void
c_checkpath(Invocation* invo) {
   find_pattern_in_path(NULL, 0, 0, false, false, CHECK_PATH, 1L,
      invo->forceit ? ACTION_SHOW_ALL : ACTION_SHOW,
      (LineNr)1, (LineNr)MAXLNUM, invo->forceit, false);
}

// ":psearch"
void
c_psearch(Invocation* invo) {
   g_do_tagpreview = p_pvh;
   c_findpat(invo);
   g_do_tagpreview = 0;
}

void
c_findpat(Invocation* invo) {
   int      whole = true;
   CS p;
   int      action;

   switch (commands[invo->id].name[2]) {
   case 'e':   // ":psearch", ":isearch" and ":dsearch"
      if (commands[invo->id].name[0] == 'p')
         action = ACTION_GOTO;
      else
         action = ACTION_SHOW;
      break;
   case 'i':   // ":ilist" and ":dlist"
       action = ACTION_SHOW_ALL;
       break;
   case 'u':   // ":ijump" and ":djump"
       action = ACTION_GOTO;
       break;
   default:   // ":isplit" and ":dsplit"
       action = ACTION_SPLIT;
       break;
   }

   long n = 1;
   if (eeIsDigit(*invo->arg))   { // get count
      n = parseLong(&invo->arg);
      invo->arg = skipwhite(invo->arg);
   }
   if (*invo->arg == '/') {  // Match regexp, not just whole words
      whole = false;
      ++invo->arg;
      p = skip_regexp(invo->arg, '/', true);
      if (*p) {
         *p++ = ZERO;
         p = skipwhite(p);

         // Check for trailing illegal characters
         if (!endsComm(invo->arg))
            invo->errmsg = ex_errmsg(e_trailing_characters_str, p);
         else
            set_nextcmd(OUT invo, p);
      }
   }
   if (!invo->skip)
   find_pattern_in_path(invo->arg, 0, (int)STRLEN(invo->arg),
      whole, !invo->forceit,
      *invo->comm == 'd' ? FIND_DEFINE : FIND_ANY, n, action,
      invo->line1, invo->line2, invo->forceit, false);
}

private void
tagCmd(Invocation* invo, CS name) {
   int      cmd;

   switch (name[1]) {
   case 'j': cmd = DT_JUMP;   // ":tjump"
        break;
   case 's': cmd = DT_SELECT;   // ":tselect"
        break;
   case 'p': cmd = DT_PREV;   // ":tprevious"
        break;
   case 'N': cmd = DT_PREV;   // ":tNext"
        break;
   case 'n': cmd = DT_NEXT;   // ":tnext"
        break;
   case 'o': cmd = DT_POP;      // ":pop"
        break;
   case 'f':         // ":tfirst"
   case 'r': cmd = DT_FIRST;   // ":trewind"
        break;
   case 'l': cmd = DT_LAST;   // ":tlast"
        break;
   default:         // ":tag"
      if (p_cst && *invo->arg != ZERO)  {
         c_cstag(invo);
         return;
      }
      cmd = DT_TAG;
      break;
   }

   if (name[0] == 'l') {
      c_ni(invo);
      return;
   }

   do_tag(invo->arg, cmd, invo->addr_count > 0 ? (int)invo->line2 : 1, invo->forceit, true);
}



// ":ptag", ":ptselect", ":ptjump", ":ptnext", etc.
void
c_ptag(Invocation* invo) {
   g_do_tagpreview = p_pvh;  // will be reset to 0 in tagCmd()
   tagCmd(invo, commands[invo->id].name + 1);
}

// ":pedit"
void
c_pedit(Invocation* invo) {
   Portal   *curPor_save = curPor;
   prepare_preview_window();

   // Edit the file.
   do_exedit(invo, NULL);

   back_to_current_window(curPor_save);
}

// ":pbook"
void
c_pbuffer(Invocation* invo) {
   Portal   *curPor_save = curPor;
   prepare_preview_window();

   // Go to the book.
   do_exbuffer(invo);

   back_to_current_window(curPor_save);
}

private void
prepare_preview_window(void) {
   if (portErrorIfPopup(true))
      return;

   // Open the preview portal or popup and make it the current portal.
   g_do_tagpreview = p_pvh;
   prepare_tagpreview(true, true, false);
}

private void
back_to_current_window(Portal *curPor_save) {
   if (curPor != curPor_save && portalIsValid(curPor_save)) {
      // Return cursor to where we were
      validate_cursor();
      redraw_later(UPD_VALID);
      enterPortal(curPor_save, true);
   } ei (PORTAL_IS_POPUP(curPor)) {
      // can't keep focus in popup portal
      enterPortal(firstPor, true);
   }
   g_do_tagpreview = 0;
}

// ":stag", ":stselect" and ":stjump".
void
c_stag(Invocation* invo) {
   postponed_split = -1;
   postponed_split_flags = commModifierG.cmod_split;
   postponed_split_tab = commModifierG.cmod_tab;
   tagCmd(invo, commands[invo->id].name + 1);
   postponed_split_flags = 0;
   postponed_split_tab = 0;
}

// ":tag", ":tselect", ":tjump", ":tnext", etc.
void
c_tag(Invocation* invo) {
   tagCmd(invo, commands[invo->id].name);
}

enum {
   SPEC_PERC = 0,
   SPEC_HASH,
   SPEC_CWORD,       // cursor word
   SPEC_CCWORD,    // cursor WORD
   SPEC_CEXPR,       // expr under cursor
   SPEC_CFILE,       // cursor path name
   SPEC_SFILE,       // ":so" file name
   SPEC_SLNUM,       // ":so" file line number
   SPEC_STACK,       // call stack
   SPEC_SCRIPT,    // script file name
   SPEC_AFILE,       // autocommand file name
   SPEC_ABUF,       // autocommand book number
   SPEC_AMATCH,    // autocommand match name
   SPEC_SFLNUM,    // script file line number
   SPEC_SID       // script ID: <SNR>123_
};

// Check "str" for starting with a special commline variable.
// If found return one of the SPEC_ values and set "*usedlen" to the length of
// the variable.  Otherwise return -1 and "*usedlen" is unchanged.
int
find_commline_var(CS src, Unt *usedlen) {
   // must be sorted by the 'value' field because it is used by bsearch()!
   static Kv spec_str_tab[] = {
      KEYVALUE_ENTRY(SPEC_SID, "SID>"),       // script ID: <SNR>123_
      KEYVALUE_ENTRY(SPEC_ABUF, "abuf>"),       // autocommand book number
      KEYVALUE_ENTRY(SPEC_AFILE, "afile>"),       // autocommand file name
      KEYVALUE_ENTRY(SPEC_AMATCH, "amatch>"),       // autocommand match name
      KEYVALUE_ENTRY(SPEC_CCWORD, "cWORD>"),       // cursor WORD
      KEYVALUE_ENTRY(SPEC_CEXPR, "cexpr>"),       // expr under cursor
      KEYVALUE_ENTRY(SPEC_CFILE, "cfile>"),       // cursor path name
      KEYVALUE_ENTRY(SPEC_CWORD, "cword>"),       // cursor word
      KEYVALUE_ENTRY(SPEC_SCRIPT, "script>"),       // script file name
      KEYVALUE_ENTRY(SPEC_SFILE, "sfile>"),       // ":so" file name
      KEYVALUE_ENTRY(SPEC_SFLNUM, "sflnum>"),       // script file line number
      KEYVALUE_ENTRY(SPEC_SLNUM, "slnum>"),       // ":so" file line number
      KEYVALUE_ENTRY(SPEC_STACK, "stack>")       // call stack
   };
   Kv target;
   Kv *entry;

   switch (*src) {
   case '%':
      *usedlen = 1;
      return SPEC_PERC;

   case '#':
      *usedlen = 1;
      return SPEC_HASH;

   case '<':
      target.key = 0;
      target.value = (Text){src + 1, 0}; // skip '<'. see cmp_keyvalue_value_n()

      entry = (Kv *)bsearch(&target, &spec_str_tab,
      ARRAY_LENGTH(spec_str_tab), sizeof(spec_str_tab[0]),
      cmp_keyvalue_value_n);
   if (!entry)
      return -1;

   *usedlen = entry->value.len + 1;
   return entry->key;

    default:
   break;
   }

   return -1;
}

//}}}
//{{{Evaluate commline variables.
//
// change "%"       to curBook->fullFileName
//     "#"       to curPor->altFnum
//     "%%"       to curPor->altFnum in Vim9 script
//     "<cword>" to word under the cursor
//     "<cWORD>" to WORD under the cursor
//     "<cexpr>" to C-expression under the cursor
//     "<cfile>" to path name under the cursor
//     "<sfile>" to sourced file name
//     "<stack>" to call stack
//     "<script>" to current script name
//     "<slnum>" to sourced file line number
//     "<afile>" to file name for autocommand
//     "<abuf>"  to book number for autocommand
//     "<amatch>" to matching name for autocommand
//
// When an error is detected, "errorMsg" is set to a non-NULL pointer (may be
// "" for error without a message) and NULL is returned.
// Returns an allocated string if a valid match was found.
// Returns NULL if no match was found.   "usedlen" then still contains the
// number of characters to skip.
CS
evalVars(
   OUT LineNr* lnump,      // line number for :e command, or NULL
   OUT CS* errorMsg,   // pointer to error message
   CS src,      // pointer into commandline
   CS srcstart,   // beginning of valid memory for src
   Unt* usedlen,   // characters after src that are used
   int* escaped,   // return value has escaped white space (can be NULL)
   int empty_is_error   // empty result is considered an error
){
   int      i;
   CS s;
   CS result;
   CS resultbuf = NULL;
   Unt   resultlen;
   Book* book;
   int valid = VALID_HEAD + VALID_PATH;    // assume valid result
   int spec_idx;
   int tilde_file = false;
   int skip_mod = false;
   Byte   strbuf[30];

   *errorMsg = NULL;
   if (escaped)
      *escaped = false;

   // Check if there is something to do.
   spec_idx = find_commline_var(src, usedlen);
   if (spec_idx < 0) {// no match
      *usedlen = 1;
      return NULL;
   }

   // Skip when preceded with a backslash "\%" and "\#".
   // Note: In "\\%" the % is also not recognized!
   if (src > srcstart && src[-1] == '\\') {
      *usedlen = 0;
      STRMOVE(src - 1, src);   // remove backslash
      return NULL;
   }

   // word or WORD under cursor
   if (spec_idx == SPEC_CWORD || spec_idx == SPEC_CCWORD || spec_idx == SPEC_CEXPR) {
      resultlen = find_ident_under_cursor(&result,
         spec_idx == SPEC_CWORD ? (FIND_IDENT | FIND_STRING)
            : spec_idx == SPEC_CEXPR ? (FIND_IDENT | FIND_STRING | FIND_EVAL)
            : FIND_STRING);
      if (resultlen == 0) {
         *errorMsg = E;
         return NULL;
      }
   }

   //'#': Alternate file name
   //'%': Current file name
   //      File name under the cursor
   //      File name for autocommand
   //  and following modifiers
   else {
      Unt off = 0;

      switch (spec_idx) {
      case SPEC_PERC:
         // '%': current file
         if (curBook->currFileName == NULL) {
            result = (CS)"";
            valid = 0;       // Must have ":p:h" to be valid
         } else {
            result = curBook->currFileName;
            tilde_file = STRCMP(result, "~") == 0;
         }
         break;
         // "%%" alternate file
         off = 1;
         // FALLTHROUGH
      case SPEC_HASH:      // '#' or "#99": alternate file
         if (off == 0 ? src[1] == '#' : src[2] == '%') {
            // "##" or "%%%": the argument list
            result = arg_all();
            resultbuf = result;
            *usedlen = off + 2;
            if (escaped)
               *escaped = true;
            skip_mod = true;
            break;
         }
         s = src + off + 1;
         if (*s == '<')      // "#<99" uses v:oldfiles
            ++s;
         i = (int)parseLong(&s);
         if (s == src + off + 2 && src[off + 1] == '-')
            // just a minus sign, don't skip over it
            s--;
         *usedlen = (int)(s - src); // length of what we expand

         if (src[off + 1] == '<' && i != 0) {
            if (*usedlen < off + 2) {
               // Should we give an error message for #<text?
               *usedlen = off + 1;
               return NULL;
            }
            result = list_find_str(get_EeglVar_list(VV_OLDFILES), (long)i);
            if (!result) {
               *errorMsg = E;
               return NULL;
            }
         } else {
            if (i == 0 && src[off + 1] == '<' && *usedlen > off + 1)
               *usedlen = off + 1;
            book = bookFindFileByBookNr(i);
            if (!book) {
               *errorMsg = _(e_no_alternate_file_name_to_substitute_for_hash);
               return NULL;
            }
            if (lnump)
               *lnump = ECMD_LAST;
            if (book->currFileName == NULL) {
               result = (CS)"";
               valid = 0;       // Must have ":p:h" to be valid
            } else {
               result = book->currFileName;
               tilde_file = STRCMP(result, "~") == 0;
            }
         }
         break;

      case SPEC_CFILE:   // file name under cursor
         result = file_name_at_cursor(FNAME_MESS|FNAME_HYP, 1L, NULL);
         if (!result) {
            *errorMsg = E;
            return NULL;
         }
         resultbuf = result;       // remember allocated string
         break;

      case SPEC_AFILE:   // file name for autocommand
         result = autocmd_fname;
         if (result && !autocmd_fname_full) {
            // Still need to turn the fname into a full path.  It is
            // postponed to avoid a delay when <afile> is not used.
            autocmd_fname_full = true;
            result = fiExpandAndCopy(autocmd_fname, false);
            eeglFree(autocmd_fname);
            autocmd_fname = result;
         }
         if (!result) {
            *errorMsg = _(e_no_autocommand_file_name_to_substitute_for_afile);
            return NULL;
         }
         result = shorten_fname1(result);
         break;

      case SPEC_ABUF:      // book number for autocommand
         if (autocmd_bufnr <= 0) {
            *errorMsg = _(e_no_autocommand_buffer_number_to_substitute_for_abuf);
            return NULL;
         }
         sprintf((char *)strbuf, "%d", autocmd_bufnr);
         result = strbuf;
         break;

      case SPEC_AMATCH:   // match name for autocommand
         result = autocmd_match;
         if (!result) {
            *errorMsg = _(e_no_autocommand_match_name_to_substitute_for_amatch);
            return NULL;
         }
         break;

      case SPEC_SFILE:   // file name for ":so" command
         result = estack_sfile(ESTACK_SFILE);
         if (!result) {
            *errorMsg = _(e_no_source_file_name_to_substitute_for_sfile);
            return NULL;
         }
         resultbuf = result;       // remember allocated string
         break;
      case SPEC_STACK:   // call stack
         result = estack_sfile(ESTACK_STACK);
         if (!result) {
            *errorMsg = _(e_no_call_stack_to_substitute_for_stack);
            return NULL;
         }
         resultbuf = result;       // remember allocated string
         break;
      case SPEC_SCRIPT:   // script file name
         result = estack_sfile(ESTACK_SCRIPT);
         if (!result) {
            *errorMsg = _(e_no_script_file_name_to_substitute_for_script);
            return NULL;
         }
         resultbuf = result;       // remember allocated string
         break;

      case SPEC_SLNUM:   // line in file for ":so" command
         if (!SOURCING_NAME || SOURCING_LNUM == 0) {
            *errorMsg = _(e_no_line_number_to_use_for_slnum);
            return NULL;
         }
         sprintf((char *)strbuf, "%ld", SOURCING_LNUM);
         result = strbuf;
         break;

      case SPEC_SFLNUM:   // line in script file
         if (scriptPosG.lineNr + SOURCING_LNUM == 0) {
            *errorMsg = _(e_no_line_number_to_use_for_sflnum);
            return NULL;
         }
         sprintf((char *)strbuf, "%ld", (long)(scriptPosG.lineNr + SOURCING_LNUM));
         result = strbuf;
         break;

      case SPEC_SID:
         if (scriptPosG.sid <= 0) {
            *errorMsg = _(e_using_sid_not_in_script_context);
            return NULL;
         }
         sprintf((char *)strbuf, "<SNR>%d_", scriptPosG.sid);
         result = strbuf;
         break;

      default:
         result = S""; // avoid gcc warning
         break;
      }

      resultlen = STRLEN(result);   // length of new string
      if (src[*usedlen] == '<') {  // remove the file name extension
         ++*usedlen;
         if ((s = lastOccurrence(result, '.')) != NULL && s >= fiGetShortFiName(result))
            resultlen = s - result;
      } ei (!skip_mod) {
         valid |= modify_fname(src, tilde_file, usedlen, &result, &resultbuf, &resultlen);
         if (!result) {
            *errorMsg = E;
            return NULL;
         }
      }
   }

   if (resultlen == 0 || valid != VALID_HEAD + VALID_PATH) {
      if (empty_is_error) {
         if (valid != VALID_HEAD + VALID_PATH)
            *errorMsg = _(e_empty_file_name_for_percent_or_hash_only_works_with_ph);
         else
            *errorMsg = _(e_evaluates_to_an_empty_string);
      }
      result = NULL;
   } else
      result = copySubstr(result, resultlen);
   eeglFree(resultbuf);
   return result;
}

// Expand the <sfile> string in "arg".
// Return an allocated string, or NULL for any error.
CS
expand_sfile(CS arg) {
   Unt resultlen = STRLEN(arg);
   CS result = copySubstr(arg, resultlen);
   if (!result)
      return NULL;
      

   for (CS p = result; *p; ) {
      if (STRNCMP(p, "<sfile>", 7) != 0)
         ++p;
      else {
         CS errorMsg;
         CS result;
         Unt   len;
         Unt   srclen;
         // replace "<sfile>" with the sourced file name, and do ":" stuff
         CS repl = evalVars(
               null, OUT &errorMsg,
               p, result, &srclen, NULL, true
         );
         if (errorMsg) {
            if (*errorMsg != ZERO)
               emsg(errorMsg);
            eeglFree(result);
            return NULL;
         }
         if (repl == NULL)  {    // no match (cannot happen)
            p += srclen;
            continue;
         }
         Unt repllen = STRLEN(repl);
         resultlen += (repllen - srclen);
         CS newres = alloc(resultlen + 1);
         len = p - result;
         mch_memmove(newres, result, len);
         STRCPY(newres + len, repl);
         len += repllen;
         STRCPY(newres + len, p + srclen);
         eeglFree(repl);
         eeglFree(result);
         result = newres;
         p = newres + len;      // continue after the match
      }
   }

   return result;
}

//}}}
//{{{dialogs

// Make a dialog message in "buff[DIALOG_MSG_SIZE]". "format" must contain "%s".
void
dialog_msg(CS buff, CS format, CS fname) {
   if (!fname)
      fname = _("Untitled");
   eeSnprintf(buff, DIALOG_MSG_SIZE, format, fname);
}

private int filetype_detect = false;
private int filetype_plugin = false;
private int filetype_indent = false;

// ":filetype [plugin] [indent] {on,off,detect}"
// on: Load the filetype.vim file to install autocommands for file types.
// off: Load the ftoff.vim file to remove all autocommands for file types.
// plugin on: load filetype.vim and ftplugin.vim
// plugin off: load ftplugof.vim
// indent on: load filetype.vim and indent.vim
// indent off: load indoff.vim
void
c_filetype(Invocation* invo) {
   CS arg = invo->arg;
   int plugin = false;
   int indent = false;

   if (*invo->arg == ZERO) {
      // Print current status.
      smsg("filetype detection:%s  plugin:%s  indent:%s",
         filetype_detect ? "ON" : "OFF",
         filetype_plugin ? (filetype_detect ? "ON" : "(on)") : "OFF",
         filetype_indent ? (filetype_detect ? "ON" : "(on)") : "OFF");
      return;
    }

   // Accept "plugin" and "indent" in any order.
   for (;;) {
      if (STRNCMP(arg, "plugin", 6) == 0) {
         plugin = true;
         arg = skipwhite(arg + 6);
         continue;
      }
      if (STRNCMP(arg, "indent", 6) == 0) {
         indent = true;
         arg = skipwhite(arg + 6);
         continue;
      }
      break;
   }
   if (STRCMP(arg, "on") == 0 || STRCMP(arg, "detect") == 0) {
      if (*arg == 'o' || !filetype_detect) {
         source_runtime((CS)FILETYPE_FILE, DIP_ALL);
         filetype_detect = true;
         if (plugin) {
            source_runtime((CS)FTPLUGIN_FILE, DIP_ALL);
            filetype_plugin = true;
          }
         if (indent) {
            source_runtime((CS)INDENT_FILE, DIP_ALL);
            filetype_indent = true;
         }
      }
      if (*arg == 'd') {
          (void)do_doautocmd(S"filetypedetect BufRead", true, NULL);
      }
   } ei (STRCMP(arg, "off") == 0) {
      if (plugin || indent) {
         if (plugin) {
            source_runtime((CS)FTPLUGOF_FILE, DIP_ALL);
            filetype_plugin = false;
         }
         if (indent) {
            source_runtime((CS)INDOFF_FILE, DIP_ALL);
            filetype_indent = false;
         }
      } else {
         source_runtime((CS)FTOFF_FILE, DIP_ALL);
         filetype_detect = false;
      }
   } else
      showErrFmtMsg(_(e_invalid_argument_str), arg);
}

void
setHlsearch(Boole flag) {
   hiliteSearchG = flag;
   set_EeglVar_nr(VV_HLSEARCH, hiliteSearchG && p_hls);
}

// ":nohlsearch"
void
c_nohlsearch(Invocation* invo UNUSED) {
   setHlsearch(false);
   redraw_all_later(UPD_SOME_VALID);
}

void
c_fold(Invocation* invo) {
   if (foldManualAllowed(true))
      foldCreate(invo->line1, invo->line2);
}

void
c_foldopen(Invocation* invo) {
   opFoldRange(invo->line1, invo->line2, invo->id == C_foldopen, invo->forceit, false);
}

void
c_folddo(Invocation* invo) {
   start_global_changes();

   // First set the marks for all lines closed/open.
   for (LineNr lnum = invo->line1; lnum <= invo->line2; ++lnum) {
      if (getFolds(lnum, NULL, NULL) == (invo->id == C_folddoclosed))
          ml_setmarked(lnum);
   } 

   // Execute the command on the marked lines.
   global_exe(invo->arg);
   ml_clearmarked();      // clear rest of the marks
   end_global_changes();
}

int
get_pressedreturn(void) {
   return ex_pressedreturn;
}

void
set_pressedreturn(int val) {
   ex_pressedreturn = val;
}

int
commandFlagNoSpacesInExtra() {
   return NOSPC_IN_EXTRA;
}

int
commandFlagExpandWildcards() {
   return XFILE;
}

//Ask for a reply from the user, a 'y' or a 'n', with prompt "str" (which should have been 
//translated already). No other characters are accepted, the message is repeated until a valid
//reply is entered or CTRL-C is hit. If direct is true, don't use vgetc() but ui_inchar(), don't 
//get characters from any buffers but directly from the user.
//
//return the 'y' or 'n'
int
ask_yesno(CS str, int direct) {
   int r = ' ';
   int save_State = stateG;

   if (isExitingG)      // put terminal in raw mode for this question
      termSetMode(TMODE_RAW);
   ++no_wait_return;
   stateG = MODE_CONFIRM; //mouse behaves like with :confirm
   setmouse();            //disable mouse for xterm
   ++no_mapping;
   ++allow_keys;          // no mapping here, but recognize keys

   while (r != 'y' && r != 'n') {
      //same hiliting as for wait_return()
      smsgDeco(getDecoFlags(HLF_R), "%s (y/n)?", str);
      if (direct)
         r = get_keystroke();
      else
         r = plain_vgetc();
      if (r == Ctrl_C || r == ESC)
         r = 'n';
      msg_putchar(r);       // show what you typed
      out_flush();
   }
   --no_wait_return;
   stateG = save_State;
   setmouse();
   --no_mapping;
   --allow_keys;

   return r;
}

//}}
//}}}
//{{{Environment variables

//Call doExpandEnv() and store the result in an allocated string.
//This is not very memory efficient, this expects the result to be freed again soon.
CS
doExpandEnvInMultiplePaths(CS src) {
   return doExpandEnvInFilePaths(src, false);
}

//Call doExpandEnv() and store the result in an allocated string.
//This is not very memory efficient, this expects the result to be freed again soon.
//When "singleFileName", handle the string as one file name, only expand "~" at the start.
CS
doExpandEnvInFilePaths(CS src, Boole singleFileName) {
   CS p = alloc(MAXPATHL);
   doExpandEnvVarsWithEscaped(OUT (Text){p, MAXPATHL}, src, singleFileName, NULL);
   return p;
}

//Expand environment variable with path name.
//"~/" is also expanded, using $HOME.   For Unix "~user/" is expanded.
//Skip over "\ ", "\~" and "\$".
//If anything fails no expansion is done and dst equals src.
Unt
doExpandEnv(
   OUT Text dst, // where to put the result
   NULLABLE CS src  // input string e.g. "$HOME/eegl.hlp"
){
   if (!src)
      return 0;
   return doExpandEnvVarsWithEscaped(OUT dst, src, false, NULL);
}

//Expand env vars. Return number of bytes written
Unt
doExpandEnvVarsWithEscaped(
   OUT Text dst, // where to put the result. Length must be sufficient!
   CS srcArg,    // input string e.g. "$HOME/eegl.hlp"
   Boole one,    // "srcp" is one file name
   CS startstr   // start again after this (can be NULL)
) {
   CS tail;
   int c;
   CS var;
   Boole copyChar;
   Boole mustfree;   // var was allocated, need to free it later
   int at_start = true; // at start of a name
   int startstr_len = 0;
   CS wr = dst.c;
   CS const sentinel = dst.c + dst.len - 1; // leave one char space for "\,"
   if (startstr)
      startstr_len = (int)STRLEN(startstr);

   CS src = skipwhite(srcArg);
   while (*src != ZERO && wr < sentinel) {
      // Skip over `=expr`.
      if (src[0] == '`' && src[1] == '=') {
         var = src;
         src += 2;
         (void)skip_expr(OUT &src, NULL);
         if (*src == '`')
            ++src;
         int lenSkipped = src - var;
         if (wr + lenSkipped > sentinel)
            lenSkipped = sentinel - wr;
         copySubstrToAllocation(wr, (Text){var, lenSkipped});
         wr += lenSkipped;
         continue;
      }
      copyChar = true;
      if ((*src == '$') || (*src == '~' && at_start)) {
         mustfree = false;

         //The variable name is copied into dst temporarily, because it may
         //be a string in read-only memory and a ZERO needs to be appended.
         if (*src == '$') { // environment var

            // Unix has ${var-name} type environment vars
            tail = src + 1;
            var = wr;
            int spaceLeft = sentinel - wr - 1;
            if (*tail == '{' && !eeIsIdentifierChar('{')) {
               tail++;   // ignore '{'
               while (spaceLeft-- > 0 && *tail != ZERO && *tail != '}')
                  *var++ = *tail++;
            } else {
               while (spaceLeft-- > 0 && *tail != ZERO && (eeIsIdentifierChar(*tail)))
                  *var++ = *tail++;
            }

            if (src[1] == '{' && *tail != '}')
               var = NULL;
            else {
               if (src[1] == '{')
                  ++tail;
               *var = ZERO;
               var = eeglGetEnv(wr);
            }
         } ei ( src[1] == ZERO || src[1] == '/' || firstOccurrence(S" ,\t\n", src[1]) != NULL) { // home directory
            var = homedir;
            tail = src + 1;
         }

         if (var && *var != ZERO) {
            c = (int)STRLEN(var);
            if (wr + c + STRLEN(tail) + 1 < sentinel) {
               STRCPY(wr, var);
               wr += c;
               // if var[] ends in a path separator and tail[] starts with it, skip a character
               if (after_pathsep(wr, wr + c) && *tail == '/')
                  ++tail;
               src = tail;
               copyChar = false;
            }
         }
         if (mustfree)
            eeglFree(var);
      }

      if (copyChar)   { // copy at least one char
         //Recognize the start of a new name, for '~'.
         //Don't do this when "one" is true, to avoid expanding "~" in ":edit foo ~ foo".
         at_start = false;
         if (src[0] == '\\' && src[1] != ZERO) {
            *wr++ = *src++;
         } ei ((src[0] == ' ' || src[0] == ',') && !one)
            at_start = true;
         if (wr < sentinel - 1) {
            *wr++ = *src++;

            if (startstr && src - startstr_len >= srcArg
                  && STRNCMP(src - startstr_len, startstr, startstr_len) == 0
            )
               at_start = true;
         }
      }
   }
   *wr = ZERO;

   return (Unt)(wr - dst.c);
}

//Eegl's version of getenv(). Special handling of $HOME, $EEGL and $EEGLRUNTIME.
//"mustfree" is set to true when the returned string is allocated.  It must be
//initialized to false by the caller.
NULLABLE CS
eeglGetEnv(CS name) {
   CS p = mch_getenv(name);
   if (p && *p == ZERO)       // empty is the same as not set
      p = NULL;
   return p;
}

//Remove environment variable "name" and take care of side effects.
void
eeUnsetenv(CS var) {
   unsetenv((char *)var);
}

//Set environment variable "name" and take care of side effects.
void
eeSetenv_ext(CS name, CS val) {
   eeSetenv(name, val);
   if (caseInsensitiveCompare(name, "HOME") == 0)
      init_homedir();
}

//Our portable version of setenv.
void
eeSetenv(CS name, CS val) {
   mch_setenv(name, val, 1);
   //When setting $EEGLRUNTIME adjust the directory to find message
   //translations to $EEGLRUNTIME/lang.
   if (*val != ZERO && caseInsensitiveCompare(name, "EEGLRUNTIME") == 0) {
      CS buf = concat_str(val, (CS)"/lang");
      BINDTEXTDOMAIN(EEGLPACKAGE, buf);
      eeglFree(buf);
   }
}

//}}}
//{{{break checks

//Check for CTRL-C pressed, but only once in a while.
//Should be used instead of ui_breakcheck() for functions that check for
//each line in the file.  Calling ui_breakcheck() each time takes too much
//time, because it can be a system call.

#ifndef BREAKCHECK_SKIP
# define BREAKCHECK_SKIP 1000
#endif

private int breakcheck_count = 0;

void
line_breakcheck(void) {
   if (++breakcheck_count >= BREAKCHECK_SKIP) {
      breakcheck_count = 0;
      ui_breakcheck();
   }
}

//Like line_breakcheck() but check 10 times less often.
void
fast_breakcheck(void) {
   if (++breakcheck_count >= BREAKCHECK_SKIP * 10) {
      breakcheck_count = 0;
      ui_breakcheck();
   }
}

//Like line_breakcheck() but check 100 times less often.
void
veryfast_breakcheck(void) {
   if (++breakcheck_count >= BREAKCHECK_SKIP * 100) {
      breakcheck_count = 0;
      ui_breakcheck();
   }
}

//}}}
//{{{multi-level undo facility
//
// The saved lines are stored in a list of lists (one for each book):
//
//     oldHead----------------------------------------------------+
//                                                                |
//                                                                V
//            +--------------+      +--------------+        +--------------+
// newHead--->|   u_header   |      | u_header     |        |   u_header   |
//            |      next  ------>  |   next    ------>     |   next     ---->NULL
//     NULL <--------prev    |<---------prev       |<---------  prev       |
//            |   uh_entry   |      |   uh_entry   |        |   uh_entry   |
//            +--------|-----+      +--------|-----+        +--------|-----+
//                     |                     |                       |
//                     V                     V                       V
//            +--------------+      +--------------+       +--------------+
//            |   u_entry    |      |   u_entry    |       |   u_entry    |
//            |   ue_next    |      |   ue_next    |       |   ue_next    |
//            +--------|-----+      +--------|-----+       +--------|-----+
//                     |                     |                      |
//                     V                     V                      V
//            +--------------+              NULL                   NULL
//            |   u_entry    |
//            |   ue_next    |
//            +--------|-----+
//                     |
//                     V
//                    etc.
//
// Each u_entry list contains the information for one undo or redo.
// curBook->undo.currHead points to the header of the last undo (the next redo),
// or is NULL if nothing has been undone (end of the branch).
//
// For keeping alternate undo/redo branches the uh_alt field is used.  Thus at
// each point in the list a branch may appear for an alternate to redo.  The
// uh_seq field is numbered sequentially to be able to find a newer or older
// branch.
//
//          +---------------+   +---------------+
// oldHead->|    u_header   |   |    u_header   |
//          |     altNext  ---->|    altNext  ----> NULL
//    NULL <----- altPrev   |<------ altPrev    |
//          |     prev      |   |    prev       |
//          +-----|---------+   +-----|---------+
//                |                   |
//                V                   V
//          +---------------+   +---------------+
//          | u_header      |   | u_header      |
//          |   altNext     |   |   altNext     |
// newHead->|   altPrev     |   |   altPrev     |
//          |   prev        |   |   prev        |
//          +-----|---------+   +-----|---------+
//                |                   |
//                V                   V
//              NULL            +---------------+    +---------------+
//                              | u_header      |    |    u_header   |
//                              |   altNext   ------>|    altNext    |
//                              |   altPrev     |<------  altPrev    |
//                              |   prev        |    |    prev       |
//                              +-----|---------+    +-----|---------+
//                                    |                    |
//                                   etc.                 etc.
//
//
// All data is allocated and will all be freed when the book is unloaded.

// Uncomment the next line for including the u_check() function.  This warns
// for errors in the debug information.
// #define U_DEBUG 1
#define UH_MAGIC 0x18dade   // value for uh_magic when in use
#define UE_MAGIC 0xabc123   // value for ue_magic when in use

// Size of buffer used for writing.
#define WRITE_BUILDER_SIZE 8192

// Structure passed around between functions.
typedef struct {
   Book* bk;
   FILE* file;
} BufInfo;


private void u_unch_branch(UndoHeader *uhp);
private UndoEntry *u_get_headentry(void);
private void u_getbot(void);
private void u_doit(int count);
private void u_undoredo(Boole undo);
private void u_undo_end(Boole did_undo, Boole absolute);
private void u_freeheader(Book *book, UndoHeader *uhp, UndoHeader **uhpp);
private void freeBranch(Book *book, UndoHeader *uhp, UndoHeader **uhpp);
private void u_freeentries(Book *book, UndoHeader *uhp, UndoHeader **uhpp);
private void freeEntry(UndoEntry *, long);
private int undo_read(BufInfo *bi, CS buffer, Unt size);
private int serialize_uep(BufInfo *bi, UndoEntry *uep);
private UndoEntry *unserialize_uep(BufInfo *bi, int *error, CS file_name);
private void serialize_pos(BufInfo *bi, Pos pos);
private void deserializePos(BufInfo *bi, Pos *pos);
private void serialize_visualinfo(BufInfo *bi, VisualInfo *info);
private void unserialize_visualinfo(BufInfo *bi, VisualInfo *info);
private void u_saveline(LineNr lnum);
private void u_blockfree(Book *book);

#define U_ALLOC_LINE(size) lalloc(size, false)

// used in undo_end() to report number of added and deleted lines
private long   u_newcount, u_oldcount;

//???
private int   undo_undoes = false;

private int   lastmark = 0;

#if defined(U_DEBUG) || defined(PROTO)
//Validate the undo structures. Print a warning when something looks wrong.
private int seen_currHead;
private int seen_newHead;
private int header_count;

private void
u_check_tree(UndoHeader *uhp, UndoHeader *exp_uh_next, UndoHeader *exp_altPrev) {
   UndoEntry *uep;

   if (uhp == NULL)
      return;
   ++header_count;
   if (uhp == curBook->undo.currHead && ++seen_currHead > 1) {
      emsg("currHead found twice (looping?)");
      return;
   }
   if (uhp == curBook->undo.newHead && ++seen_newHead > 1) {
      emsg("newHead found twice (looping?)");
      return;
   }

   if (uhp->uh_magic != UH_MAGIC)
      emsg("uh_magic wrong (may be using freed memory)");
   else {
      // Check pointers back are correct.
      if (uhp->next.ptr != exp_uh_next) {
          emsg("next wrong");
          smsg("expected: 0x%x, actual: 0x%x", exp_uh_next, uhp->next.ptr);
      }
      if (uhp->altPrev.ptr != exp_altPrev) {
          emsg("altPrev wrong");
          smsg("expected: 0x%x, actual: 0x%x", exp_altPrev, uhp->altPrev.ptr);
      }

      // Check the undo tree at this header.
      for (uep = uhp->uh_entry; uep != NULL; uep = uep->ue_next) {
          if (uep->ue_magic != UE_MAGIC) {
         emsg("ue_magic wrong (may be using freed memory)");
         break;
          }
      }

      // Check the next alt tree.
      u_check_tree(uhp->altNext.ptr, uhp->next.ptr, uhp);

      // Check the next header in this branch.
      u_check_tree(uhp->prev.ptr, uhp, NULL);
    }
}

private void
u_check(int newhead_may_be_NULL) {
   seen_newHead = 0;
   seen_currHead = 0;
   header_count = 0;

   u_check_tree(curBook->undo.oldHead, NULL, NULL);

   if (seen_newHead == 0 && curBook->undo.oldHead != NULL
       && !(newhead_may_be_NULL && curBook->undo.newHead == NULL))
      showErrFmtMsg("newHead invalid: 0x%x", curBook->undo.newHead);
   if (curBook->undo.currHead != NULL && seen_currHead == 0)
      showErrFmtMsg("currHead invalid: 0x%x", curBook->undo.currHead);
   if (header_count != curBook->undo.countHeaders) {
      emsg("countHeaders invalid");
      smsg("expected: %ld, actual: %ld",
                   (long)header_count, (long)curBook->countHeaders);
   }
}
#endif

//Save the current line for both the "u" and "U" command. Careful: may trigger autocommands that 
//reload the book. Return OK or FAIL.
int
u_save_cursor(void) {
   return (u_save((LineNr)(curPor->cursor.lnum - 1), (LineNr)(curPor->cursor.lnum + 1)));
}

//Save the lines between "top" and "bot" for both the "u" and "U" command. "top" may be 0 and 
//"bot" may be curBook->mem.lineCount + 1. Careful: may trigger autocommands that reload the 
//book. Return FAIL when lines could not be saved, OK otherwise.
int
u_save(LineNr top, LineNr bot) {
   if (undo_off)
      return OK;

   if (top >= bot || bot > curBook->mem.lineCount + 1)
      return FAIL;   // rely on caller to give an error message

   if (top + 2 == bot)
      u_saveline((LineNr)(top + 1));

   return (u_savecommon(top, bot, (LineNr)0, false));
}

//Save the line "lnum" (used by ":s" and "~" command). The line is replaced, so the new bottom line
//is lnum + 1. Careful: may trigger autocommands that reload the book.
//Return FAIL when lines could not be saved, OK otherwise.
int
u_savesub(LineNr lnum) {
   if (undo_off)
      return OK;

   return (u_savecommon(lnum - 1, lnum + 1, lnum + 1, false));
}

//A new line is inserted before line "lnum" (used by :s command). The line is inserted, so the new 
//bottom line is lnum + 1. Careful: may trigger autocommands that reload the book.
//Return FAIL when lines could not be saved, OK otherwise.
private int
u_inssub(LineNr lnum) {
   if (undo_off)
      return OK;

   return (u_savecommon(lnum - 1, lnum, lnum + 1, false));
}

//Save the lines "lnum" - "lnum" + nlines (used by delete command).
//The lines are deleted, so the new bottom line is lnum, unless the book becomes empty.
//Careful: may trigger autocommands that reload the book.
//Return FAIL when lines could not be saved, OK otherwise.
int
u_savedel(LineNr lnum, long nlines) {
   if (undo_off)
      return OK;

   return (u_savecommon(lnum - 1, lnum + nlines,
           nlines == curBook->mem.lineCount ? 2 : lnum, false));
}

//true when undo is allowed.  Otherwise give an error message and return false.
int
undo_allowed(void) {
   // Don't allow changes when @modifiable is off.
   if (!curBook->o.modifiable) {
      emsg(_(e_cannot_make_changes_modifiable_is_off));
      return false;
   }

   // Don't allow changes in the book while editing the commline. The
   // caller of getCommline() may get confused.
   if (textlock != 0) {
      emsg(_(e_not_allowed_to_change_text_or_change_portal));
      return false;
   }

   return true;
}

//u_save_line(): save an allocated copy of line "lnum" into "ul".
//Return FAIL when out of memory.
private int
u_save_line(UndoLine *ul, LineNr lnum) {
   CS line = ml_get(lnum);
   ul->ul_textlen = ml_get_len(lnum);
   if (curBook->mem.lineLen == 0) {
      ul->ul_len = 1;
      ul->ul_line = copyStr(E);
    } else {
      // This uses the length in the memline, thus text properties are included.
      ul->ul_len = curBook->mem.lineLen;
      ul->ul_line = eeMemsave(line, ul->ul_len);
   }
   return ul->ul_line == NULL ? FAIL : OK;
}

//return true if line "lnum" has text property "flags".
private Boole
has_prop_w_flags(LineNr lnum, int flags) {
   CS props;
   int proplen = get_text_props(OUT &props, curBook, lnum, false);

   for (int i = 0; i < proplen; ++i) {
      TextProp prop;
      mch_memmove(OUT &prop, props + i * sizeof prop, sizeof prop);
      if ((prop.flags & flags) != 0)
         return true;
   }
   return false;
}

//Common code for various ways to save text before a change.
//"top" is the line above the first changed line.
//"bot" is the line below the last changed line.
//"newbot" is the new bottom line.  Use zero when not known.
//"reload" is true when saving for a book reload.
//Careful: may trigger autocommands that reload the book.
//Return FAIL when lines could not be saved, OK otherwise.
int
u_savecommon(LineNr top, LineNr bot, LineNr newbot, int reload) {
   LineNr   lnum;
   long   i;
   UndoHeader   *uhp;
   UndoHeader   *old_curhead;
   UndoEntry   *uep;
   UndoEntry   *prev_uep;
   long   size;

   if (!reload) {
      // When making changes is not allowed return FAIL.  It's a crude way
      // to make all change commands fail.
      if (!undo_allowed())
          return FAIL;

      //A change in a terminal book removes the hiliting.
      uiBeforeLeavingTerminal();

      //Saving text for undo means we are going to make a change.  Give a
      //warning for a read-only file before making the change, so that the
      //FileChangedRO event can replace the book with a read-write version
      //(e.g., obtained from a source control system).
      change_warning(0);
      if (bot > curBook->mem.lineCount + 1) {
          // This happens when the FileChangedRO autocommand changes the
          // file in a way it becomes shorter.
          emsg(_(e_line_count_changed_unexpectedly));
          return FAIL;
      }
   }

#ifdef U_DEBUG
    u_check(false);
#endif

   // Include the line above if a text property continues from it.
   // Include the line below if a text property continues to it.
   if (bot - top > 1) {
      if (top > 0 && has_prop_w_flags(top + 1, TEXT_PROP_CONT_PREV))
          --top;
      if (bot <= curBook->mem.lineCount && has_prop_w_flags(bot - 1, TEXT_PROP_CONT_NEXT)) {
          ++bot;
          if (newbot != 0)
         ++newbot;
      }
   }

   size = bot - top - 1;

   //If curBook->undo.synced == true make a new header.
   if (curBook->undo.synced) {
      // Need to create new entry in changeList.
      curBook->newChange = true;

      if (p_ul >= 0) {
         //Make a new header entry.  Do this first so that we don't mess
         //up the undo info when out of memory.
         uhp = U_ALLOC_LINE(sizeof(UndoHeader));
#ifdef U_DEBUG
         uhp->uh_magic = UH_MAGIC;
#endif
      } else
         uhp = NULL;

      //If we undid more than we redid, move the entry lists before and
      //including curBook->undo.currHead to an alternate branch.
      old_curhead = curBook->undo.currHead;
      if (old_curhead != NULL) {
          curBook->undo.newHead = old_curhead->next.ptr;
          curBook->undo.currHead = NULL;
      }

      //free headers to keep the size right
      while (curBook->undo.countHeaders > p_ul && curBook->undo.oldHead != NULL) {
         UndoHeader       *uhfree = curBook->undo.oldHead;

         if (uhfree == old_curhead)
            // Can't reconnect the branch, delete all of it.
            freeBranch(curBook, uhfree, &old_curhead);
         ei (uhfree->altNext.ptr == NULL)
            // There is no branch, only free one header.
            u_freeheader(curBook, uhfree, &old_curhead);
         else {
            // Free the oldest alternate branch as a whole.
            while (uhfree->altNext.ptr != NULL)
               uhfree = uhfree->altNext.ptr;
            freeBranch(curBook, uhfree, &old_curhead);
         }
#ifdef U_DEBUG
         u_check(true);
#endif
      }

      if (!uhp) {     // no undo at all
         if (old_curhead != NULL)
            freeBranch(curBook, old_curhead, NULL);
         curBook->undo.synced = false;
         return OK;
      }

      uhp->prev.ptr = NULL;
      uhp->next.ptr = curBook->undo.newHead;
      uhp->altNext.ptr = old_curhead;
      if (old_curhead) {
         uhp->altPrev.ptr = old_curhead->altPrev.ptr;
         if (uhp->altPrev.ptr != NULL)
            uhp->altPrev.ptr->altNext.ptr = uhp;
         old_curhead->altPrev.ptr = uhp;
         if (curBook->undo.oldHead == old_curhead)
            curBook->undo.oldHead = uhp;
      } else
         uhp->altPrev.ptr = NULL;
      if (curBook->undo.newHead != NULL)
         curBook->undo.newHead->prev.ptr = uhp;

      uhp->uh_seq = ++curBook->undo.seqLast;
      curBook->undo.seqCurr = uhp->uh_seq;
      uhp->uh_time = eeTime();
      uhp->uh_save_nr = 0;
      curBook->undo.timeCurr = uhp->uh_time + 1;

      uhp->uh_walk = 0;
      uhp->uh_entry = NULL;
      uhp->uh_getbot_entry = NULL;
      uhp->uh_cursor = curPor->cursor;   // save cursor pos. for undo
      if (virtual_active() && curPor->cursor.coladd > 0)
         uhp->uh_cursor_vcol = getviscol();
      else
         uhp->uh_cursor_vcol = -1;

      // save changed and book empty flag for undo
      uhp->uh_flags = (curBook->wasModified ? UH_CHANGED : 0) +
                ((curBook->mem.flags & ML_EMPTY) ? UH_EMPTYBUF : 0);

      // save named marks and Visual marks for undo
      mch_memmove(uhp->uh_namedm, curBook->namedMarks, sizeof(Pos) * NMARKS);
      uhp->uh_visual = curBook->visual;

      curBook->undo.newHead = uhp;
      if (curBook->undo.oldHead == NULL)
         curBook->undo.oldHead = uhp;
      curBook->undo.countHeaders++;
   } else {
      if (p_ul < 0)   // no undo at all
         return OK;

      //When saving a single line, and it has been saved just before, it
      //doesn't make sense saving it again.  Saves a lot of memory when
      //making lots of changes inside the same line.
      //This is only possible if the previous change didn't increase or
      //decrease the number of lines.
      //Check the ten last changes.  More doesn't make sense and takes too long.
      if (size == 1) {
         uep = u_get_headentry();
         prev_uep = NULL;
         for (i = 0; i < 10; ++i) {
            if (!uep)
                break;

            // If lines have been inserted/deleted we give up.
            // Also when the line was included in a multi-line save.
            if ((curBook->undo.newHead->uh_getbot_entry != uep
                   ? (uep->ue_top + uep->ue_size + 1
                  != (uep->ue_bot == 0
                      ? curBook->mem.lineCount + 1
                      : uep->ue_bot))
                   : uep->ue_lcount != curBook->mem.lineCount)
               || (uep->ue_size > 1
                   && top >= uep->ue_top
                   && top + 2 <= uep->ue_top + uep->ue_size + 1))
                break;

            // If it's the same line we can skip saving it again.
            if (uep->ue_size == 1 && uep->ue_top == top) {
               if (i > 0) {
                  // It's not the last entry: get ue_bot for the last
                  // entry now. Following deleted/inserted lines go to the re-used entry.
                  u_getbot();
                  curBook->undo.synced = false;

                  // Move the found entry to become the last entry. The order of undo/redo doesn't 
                  // matter for the entries we move it over, since they don't change the line
                  // count and don't include this line. It does matter for the found entry if the line 
                  // count is changed by the executed command.
                  prev_uep->ue_next = uep->ue_next;
                  uep->ue_next = curBook->undo.newHead->uh_entry;
                  curBook->undo.newHead->uh_entry = uep;
               }

                // The executed command may change the line count.
                if (newbot != 0)
               uep->ue_bot = newbot;
                ei (bot > curBook->mem.lineCount)
               uep->ue_bot = 0;
                else {
               uep->ue_lcount = curBook->mem.lineCount;
               curBook->undo.newHead->uh_getbot_entry = uep;
                }
                return OK;
            }
            prev_uep = uep;
            uep = uep->ue_next;
          }
      }

      // find line number for ue_bot for previous u_save()
      u_getbot();
    }

   //add lines in front of entry list
   uep = U_ALLOC_LINE(sizeof(UndoEntry));
   CLEAR_POINTER(uep);
#ifdef U_DEBUG
   uep->ue_magic = UE_MAGIC;
#endif

   uep->ue_size = size;
   uep->ue_top = top;
   if (newbot != 0)
      uep->ue_bot = newbot;
      //Use 0 for ue_bot if bot is below last line. Otherwise we have to compute ue_bot later.
   ei (bot > curBook->mem.lineCount)
      uep->ue_bot = 0;
   else {
      uep->ue_lcount = curBook->mem.lineCount;
      curBook->undo.newHead->uh_getbot_entry = uep;
   }

   if (size > 0) {
      uep->ue_array = U_ALLOC_LINE(sizeof(UndoLine) * size);
      for (i = 0, lnum = top + 1; i < size; ++i) {
          fast_breakcheck();
          if (gotInterruptG) {
             freeEntry(uep, i);
             return FAIL;
          }
          if (u_save_line(&uep->ue_array[i], lnum++) == FAIL) {
             freeEntry(uep, i);
             goto nomem;
          }
      }
    } else {
       uep->ue_array = NULL;
    }
    uep->ue_next = curBook->undo.newHead->uh_entry;
    curBook->undo.newHead->uh_entry = uep;
    curBook->undo.synced = false;
    undo_undoes = false;

#ifdef U_DEBUG
    u_check(false);
#endif
    return OK;

nomem:
    msg_silent = 0;   // must display the prompt
    if (ask_yesno((CS)_("No undo possible; continue anyway"), true) == 'y') {
       undo_off = true;       // will be reset when character typed
       return OK;
    }
    do_outofmem_msg((Ulong)0);
    return FAIL;
}


# define UF_START_MAGIC       "Vim\237UnDo\345"  // magic at start of undofile
# define UF_START_MAGIC_LEN   9
# define UF_HEADER_MAGIC   0x5fd0   // magic at start of header
# define UF_HEADER_END_MAGIC   0xe7aa   // magic after last header
# define UF_ENTRY_MAGIC      0xf518   // magic at start of entry
# define UF_ENTRY_END_MAGIC   0x3581   // magic after last entry
# define UF_VERSION      2   // 2-byte undofile version number

// extra fields for header
# define UF_LAST_SAVE_NR   1

// extra fields for uhp
# define UHP_SAVE_NR      1

//Compute the hash for the current buffer text into hash[UNDO_HASH_SIZE].
void
u_compute_hash(CS hash) {
   ContextSha256   ctx;
   LineNr      lnum;

   sha256_start(&ctx);
   for (lnum = 1; lnum <= curBook->mem.lineCount; ++lnum)
      sha256_update(&ctx, ml_get(lnum), (Unt)(ml_get_len(lnum) + 1));
   sha256_finish(&ctx, hash);
}

private void
corruption_error(char *mesg, CS file_name) {
   showErrFmtMsg(_(e_corrupted_undo_file_str_str), mesg, file_name);
}

private void
u_free_uhp(UndoHeader *uhp) {
   UndoEntry* uep = uhp->uh_entry;
   while (uep) {
      UndoEntry* nuep = uep->ue_next;
      freeEntry(uep, uep->ue_size);
      uep = nuep;
   }
   eeglFree(uhp);
}

//Write a sequence of bytes to the undo file. Book as needed. Return OK or FAIL.
private int
writeToUndoFile(BufInfo* bi, Arr(Byte) ptr, Unt len) {
   if (fwrite(ptr, len, (Unt)1, bi->file) != 1)
      return FAIL;
   return OK;
}

//Write a number, most significant byte first, in "len" bytes.
//Must match with undo_read_?c() functions.
//Return OK or FAIL.
private int
undo_write_bytes(BufInfo* bi, Ulong nr, int len) {
   Byte  buf[8];
   int i;
   int bufi = 0;

   for (i = len - 1; i >= 0; --i)
      buf[bufi++] = (Byte)(nr >> (i * 8));
   return writeToUndoFile(bi, buf, (Unt)len);
}

//Write the pointer to an undo header.  Instead of writing the pointer itself
//we use the sequence number of the header.  This is converted back to
//pointers when reading.
private void
put_header_ptr(BufInfo *bi, UndoHeader *uhp) {
   undo_write_bytes(bi, (Ulong)(uhp != NULL ? uhp->uh_seq : 0), 4);
}

private int
undo_read_4c(BufInfo *bi) {
   return get4c(bi->file);
}

private int
undo_read_2c(BufInfo *bi) {
   return get2c(bi->file);
}

private int
undo_read_byte(BufInfo *bi) {
   return getc(bi->file);
}

private time_t
undo_read_time(BufInfo *bi) {
   return get8ctime(bi->file);
}

//Read "buffer[size]" from the undo file. Return OK or FAIL.
private int
undo_read(BufInfo *bi, CS buffer, Unt size) {
   int retval = OK;

   if (fread(buffer, size, 1, bi->file) != 1)
      retval = FAIL;

   if (retval == FAIL)
      // Error may be checked for only later. Fill with zeros, so that the reader won't use garbage
      memset(buffer, 0, size);
   return retval;
}

//Read a string of length "len" from "bi->bi_fd". "len" can be zero to allocate an empty line.
//Append a ZERO. Return a pointer to allocated memory or NULL for failure.
private CS
readStringFromFile(BufInfo *bi, Unt len) {
   CS ptr = alloc(len + 1);

   if (len > 0 && undo_read(bi, ptr, len) == FAIL) {
      eeglFree(ptr);
      return NULL;
   }
   // In case there are text properties there already is a ZERO, but
   // checking for that is more expensive than just adding a dummy byte.
   ptr[len] = ZERO;
   return ptr;
}

//Writes the header
private int
serialize_header(BufInfo* bi, Arr(Byte) hash) {
   Book* book = bi->bk;
   FILE* fp = bi->file;
   Byte time_buf[8];

   // Start writing, first the magic marker and undo info version.
   if (fwrite(UF_START_MAGIC, (Unt)UF_START_MAGIC_LEN, (Unt)1, fp) != 1)
      return FAIL;

   undo_write_bytes(bi, (Ulong)UF_VERSION, 2);

   // Write a hash of the buffer text, so that we can verify it is still the
   // same when reading the buffer text.
   if (writeToUndoFile(bi, hash, (Unt)UNDO_HASH_SIZE) == FAIL)
      return FAIL;

   // book-specific data
   undo_write_bytes(bi, (Ulong)book->mem.lineCount, 4);
   undo_write_bytes(bi, (Ulong)book->undo.line.ul_textlen, 4);
   if (book->undo.line.ul_textlen > 0 
         && writeToUndoFile(bi, book->undo.line.ul_line, (Unt)book->undo.line.ul_textlen) == FAIL
   )
      return FAIL;
   undo_write_bytes(bi, (Ulong)book->undo.lineLnum, 4);
   undo_write_bytes(bi, (Ulong)book->undo.lineCol, 4);

   // Undo structures header data
   put_header_ptr(bi, book->undo.oldHead);
   put_header_ptr(bi, book->undo.newHead);
   put_header_ptr(bi, book->undo.currHead);

   undo_write_bytes(bi, (Ulong)book->undo.countHeaders, 4);
   undo_write_bytes(bi, (Ulong)book->undo.seqLast, 4);
   undo_write_bytes(bi, (Ulong)book->undo.seqCurr, 4);
   time_to_bytes(book->undo.timeCurr, time_buf);
   writeToUndoFile(bi, time_buf, 8);

   // Optional fields.
   undo_write_bytes(bi, 4, 1);
   undo_write_bytes(bi, UF_LAST_SAVE_NR, 1);
   undo_write_bytes(bi, (Ulong)book->undo.saveNrLast, 4);

   undo_write_bytes(bi, 0, 1);  // end marker

   return OK;
}

private int
serialize_uhp(BufInfo* bi, UndoHeader* uhp) {
   Byte time_buf[8];

   if (undo_write_bytes(bi, (Ulong)UF_HEADER_MAGIC, 2) == FAIL)
      return FAIL;

   put_header_ptr(bi, uhp->next.ptr);
   put_header_ptr(bi, uhp->prev.ptr);
   put_header_ptr(bi, uhp->altNext.ptr);
   put_header_ptr(bi, uhp->altPrev.ptr);
   undo_write_bytes(bi, uhp->uh_seq, 4);
   serialize_pos(bi, uhp->uh_cursor);
   undo_write_bytes(bi, (Ulong)uhp->uh_cursor_vcol, 4);
   undo_write_bytes(bi, (Ulong)uhp->uh_flags, 2);
   // Assume NMARKS will stay the same.
   for (int i = 0; i < NMARKS; ++i)
      serialize_pos(bi, uhp->uh_namedm[i]);
   serialize_visualinfo(bi, &uhp->uh_visual);
   time_to_bytes(uhp->uh_time, time_buf);
   writeToUndoFile(bi, time_buf, 8);

   // Optional fields.
   undo_write_bytes(bi, 4, 1);
   undo_write_bytes(bi, UHP_SAVE_NR, 1);
   undo_write_bytes(bi, (Ulong)uhp->uh_save_nr, 4);

   undo_write_bytes(bi, 0, 1);  // end marker

   // Write all the entries.
   for (UndoEntry* uep = uhp->uh_entry; uep != NULL; uep = uep->ue_next) {
      undo_write_bytes(bi, (Ulong)UF_ENTRY_MAGIC, 2);
      if (serialize_uep(bi, uep) == FAIL)
         return FAIL;
   }
   undo_write_bytes(bi, (Ulong)UF_ENTRY_END_MAGIC, 2);
   return OK;
}

private UndoHeader *
unserialize_uhp(BufInfo* bi, CS file_name) {
   int i;
   int c;
   int error;

   UndoHeader* uhp = U_ALLOC_LINE(sizeof(UndoHeader));
   CLEAR_POINTER(uhp);
#ifdef U_DEBUG
   uhp->uh_magic = UH_MAGIC;
#endif
   uhp->next.seq = undo_read_4c(bi);
   uhp->prev.seq = undo_read_4c(bi);
   uhp->altNext.seq = undo_read_4c(bi);
   uhp->altPrev.seq = undo_read_4c(bi);
   uhp->uh_seq = undo_read_4c(bi);
   if (uhp->uh_seq <= 0) {
      corruption_error("uh_seq", file_name);
      eeglFree(uhp);
      return NULL;
   }
   deserializePos(bi, &uhp->uh_cursor);
   uhp->uh_cursor_vcol = undo_read_4c(bi);
   uhp->uh_flags = undo_read_2c(bi);
   for (i = 0; i < NMARKS; ++i)
      deserializePos(bi, &uhp->uh_namedm[i]);
   unserialize_visualinfo(bi, &uhp->uh_visual);
   uhp->uh_time = undo_read_time(bi);

   // Optional fields.
   for (;;) {
      int len = undo_read_byte(bi);
      int what;

      if (len == EOF) {
          corruption_error("truncated", file_name);
          u_free_uhp(uhp);
          return NULL;
      }
      if (len == 0)
          break;
      what = undo_read_byte(bi);
      switch (what) {
      case UHP_SAVE_NR:
         uhp->uh_save_nr = undo_read_4c(bi);
         break;
      default:
         // field not supported, skip
         while (--len >= 0)
             (void)undo_read_byte(bi);
      }
   }

   // Unserialize the uep list.
   UndoEntry* last_uep = NULL;
   while ((c = undo_read_2c(bi)) == UF_ENTRY_MAGIC) {
      error = false;
      UndoEntry* uep = unserialize_uep(bi, &error, file_name);
      if (last_uep)
         last_uep->ue_next = uep;
      else
         uhp->uh_entry = uep;
      last_uep = uep;
      if (!uep || error) {
          u_free_uhp(uhp);
          return NULL;
      }
   }
   if (c != UF_ENTRY_END_MAGIC) {
      corruption_error("entry end", file_name);
      u_free_uhp(uhp);
      return NULL;
   }

   return uhp;
}

//Serialize "uep".
private int
serialize_uep(BufInfo* bi, UndoEntry* uep) {
   undo_write_bytes(bi, (Ulong)uep->ue_top, 4);
   undo_write_bytes(bi, (Ulong)uep->ue_bot, 4);
   undo_write_bytes(bi, (Ulong)uep->ue_lcount, 4);
   undo_write_bytes(bi, (Ulong)uep->ue_size, 4);
   for (int i = 0; i < uep->ue_size; ++i) {
   // Text is written without the text properties, since we cannot restore
   // the text property types.
   if (undo_write_bytes(bi, (Ulong)uep->ue_array[i].ul_textlen, 4) == FAIL)
       return FAIL;
   if (uep->ue_array[i].ul_textlen > 0
      && writeToUndoFile(bi, uep->ue_array[i].ul_line, uep->ue_array[i].ul_textlen) == FAIL)
       return FAIL;
   }
   return OK;
}

private UndoEntry *
unserialize_uep(BufInfo *bi, int *error, CS file_name) {
   int      i;
   UndoLine   *array = NULL;
   CS line;

   UndoEntry* uep = U_ALLOC_LINE(sizeof(UndoEntry));
   CLEAR_POINTER(uep);
#ifdef U_DEBUG
   uep->ue_magic = UE_MAGIC;
#endif
   uep->ue_top = undo_read_4c(bi);
   uep->ue_bot = undo_read_4c(bi);
   uep->ue_lcount = undo_read_4c(bi);
   uep->ue_size = undo_read_4c(bi);
   if (uep->ue_size > 0) {
      if (uep->ue_size < LONG_MAX / (int)sizeof(CS))
         array = U_ALLOC_LINE(sizeof(UndoLine) * uep->ue_size);
      if (array == NULL) {
         *error = true;
         return uep;
      }
      memset(array, 0, sizeof(UndoLine) * uep->ue_size);
   }
   uep->ue_array = array;

   for (i = 0; i < uep->ue_size; ++i) {
      int line_len = undo_read_4c(bi);
      if (line_len >= 0)
         line = readStringFromFile(bi, (Unt)line_len);
      else {
         line = NULL;
         corruption_error("line length", file_name);
      }
      if (line == NULL) {
         *error = true;
         return uep;
      }
      array[i].ul_line = line;
      array[i].ul_len = line_len + 1;
      array[i].ul_textlen = line_len;
   }
    return uep;
}

//Serialize "pos".
private void
serialize_pos(BufInfo *bi, Pos pos) {
   undo_write_bytes(bi, (Ulong)pos.lnum, 4);
   undo_write_bytes(bi, (Ulong)pos.col, 4);
   undo_write_bytes(bi, (Ulong)pos.coladd, 4);
}

//Deserialize the Pos at the current position.
private void
deserializePos(BufInfo *bi, Pos *pos) {
   pos->lnum = undo_read_4c(bi);
   if (pos->lnum < 0)
      pos->lnum = 0;
   pos->col = undo_read_4c(bi);
   if (pos->col < 0)
      pos->col = 0;
   pos->coladd = undo_read_4c(bi);
   if (pos->coladd < 0)
      pos->coladd = 0;
}

//Serialize "info".
private void
serialize_visualinfo(BufInfo *bi, VisualInfo *info) {
   serialize_pos(bi, info->vi_start);
   serialize_pos(bi, info->vi_end);
   undo_write_bytes(bi, (Ulong)info->vi_mode, 4);
   undo_write_bytes(bi, (Ulong)info->vi_curswant, 4);
}

//Unserialize the VisualInfo at the current position.
private void
unserialize_visualinfo(BufInfo *bi, VisualInfo *info) {
   deserializePos(bi, &info->vi_start);
   deserializePos(bi, &info->vi_end);
   info->vi_mode = undo_read_4c(bi);
   info->vi_curswant = undo_read_4c(bi);
}

//Write the undo tree into an undo file.
//When "name" is not NULL, use it as the name of the undo file.
//Otherwise use book->fullFileName to generate the undo file name.
//"book" must never be null, book->fullFileName is used to obtain the original file permissions.
//"forceit" is true for ":wundo!", false otherwise.
//"hash[UNDO_HASH_SIZE]" must be the hash value of the buffer text.
void
u_write_undo(CS name, Boole forceit, Book* book, Arr(Byte) hash) {
   UndoHeader* uhp;
   CS file_name;
   int      mark;
#ifdef U_DEBUG
   int headers_written = 0;
#endif
   int fd;
   int write_ok = false;
   int st_old_valid = false;
   FileStat st_old;
   FileStat st_new;
   BufInfo bi;
   CLEAR_FIELD(bi);

   if (!name) {
      file_name = fiBuildSwapOrUndoFname(book->fullFileName, false);
      if (file_name == NULL) {
         if (p_verbose > 0) {
            verbose_enter();
            smsg(_("Cannot write undo file in any directory in 'undodir'"));
            verbose_leave();
         }
         return;
      }
   } else
      file_name = name;

   //Decide about the permission to use for the undo file.  If the book
   //has a name use the permission of the original file.  Otherwise only
   //allow the user to access the undo file.
   Unt perm = 0600;
   if (book->fullFileName) {
      if (stat((char *)book->fullFileName, OUT &st_old) >= 0) {
          perm = st_old.st_mode;
          st_old_valid = true;
      }
   }

   // strip any s-bit and executable bit
   perm = perm & 0666;

   // If the undo file already exists, verify that it actually is an undo file, and delete it.
   if (mch_getperm(file_name) >= 0) {
      if (!name || !forceit) {
         // Check we can read it and it's an undo file.
         fd = open((char *)file_name, O_RDONLY|O_EXTRA, 0);
         if (fd < 0) {
            if (p_verbose > 0)
               verbose_enter();
            if (name || p_verbose > 0)
               smsg( _("Will not overwrite with undo file, cannot read: %s"), file_name);
            if (p_verbose > 0)
               verbose_enter();
            goto theend;
         } else {
            Byte mbuf[UF_START_MAGIC_LEN];
            int len = read_eintr(fd, mbuf, UF_START_MAGIC_LEN);
            close(fd);
            
            if (len < UF_START_MAGIC_LEN || memcmp(mbuf, UF_START_MAGIC, UF_START_MAGIC_LEN) != 0) {
               if (p_verbose > 0)
                  verbose_enter();
               if (name || p_verbose > 0)
                  smsg(_("Will not overwrite, this is not an undo file: %s"), file_name);
               if (p_verbose > 0)
                  verbose_leave();
               goto theend;
            }
         }
      }
      mch_remove(file_name);
   }

   //If there is no undo information at all, quit here after deleting any
   //existing undo file.
   if (book->undo.countHeaders == 0 && book->undo.line.ul_line == NULL) {
      if (p_verbose > 0)
         verb_msg(_("Skipping undo file write, nothing to undo"));
      goto theend;
   }

   fd = open((char *)file_name, O_CREAT|O_EXTRA|O_WRONLY|O_EXCL|O_NOFOLLOW, perm);
   if (fd < 0) {
      showErrFmtMsg(_(e_cannot_open_undo_file_for_writing_str), file_name);
      goto theend;
   }
   (void)mch_setperm(file_name, perm);
   if (p_verbose > 0) {
      verbose_enter();
      smsg(_("Writing undo file: %s"), file_name);
      verbose_leave();
   }

#ifdef U_DEBUG
    //Check there is no problem in undo info before writing.
    u_check(false);
#endif

    //Try to set the group of the undo file same as the original file. If
    //this fails, set the protection bits for the group same as the protection bits for others.
    if (st_old_valid
       && STAT(file_name, OUT &st_new) >= 0
       && st_new.st_gid != st_old.st_gid
       && fchown(fd, (uid_t)-1, st_old.st_gid) != 0
    )
      mch_setperm(file_name, (perm & 0707) | ((perm & 07) << 3));

   FILE* fp = fdopen(fd, "w");
   if (!fp) {
      showErrFmtMsg(_(e_cannot_open_undo_file_for_writing_str), file_name);
      close(fd);
      mch_remove(file_name);
      goto theend;
   }

   // Undo must be synced.
   u_sync(true);

   bi.bk = book;
   bi.file = fp;
   if (serialize_header(&bi, hash) == FAIL)
      goto write_error;

   //Iteratively serialize UHPs and their UEPs from the top down.
   mark = ++lastmark;
   uhp = book->undo.oldHead;
   while (uhp) {
      // Serialize current UHP if we haven't seen it
      if (uhp->uh_walk != mark) {
         uhp->uh_walk = mark;
#ifdef U_DEBUG
         ++headers_written;
#endif
         if (serialize_uhp(&bi, uhp) == FAIL)
            goto write_error;
      }

      // Now walk through the tree - algorithm from undo_time().
      if (uhp->prev.ptr && uhp->prev.ptr->uh_walk != mark)
         uhp = uhp->prev.ptr;
      ei (uhp->altNext.ptr && uhp->altNext.ptr->uh_walk != mark)
         uhp = uhp->altNext.ptr;
      ei (uhp->next.ptr && !uhp->altPrev.ptr && uhp->next.ptr->uh_walk != mark)
         uhp = uhp->next.ptr;
      ei (uhp->altPrev.ptr)
         uhp = uhp->altPrev.ptr;
      else
         uhp = uhp->next.ptr;
   }

   if (undo_write_bytes(&bi, (Ulong)UF_HEADER_END_MAGIC, 2) == OK)
      write_ok = true;
#ifdef U_DEBUG
   if (headers_written != book->undo.countHeaders) {
      showErrFmtMsg("Written %ld headers, ...", headers_written);
      showErrFmtMsg("... but numhead is %ld", book->undo.countHeaders);
   }
#endif

   if (p_fs && fflush(fp) == 0 && eeFsync(fd) != 0)
      write_ok = false;

write_error:
   fclose(fp);
   if (!write_ok)
      showErrFmtMsg(_(e_write_error_in_undo_file_str), file_name);


theend:
   if (file_name != name)
      eeglFree(file_name);
}

//Load the undo tree from an undo file.
//If "name" is not NULL use it as the undo file name. This also means being
//a bit more verbose.
//Otherwise use curBook->fullFileName to generate the undo file name.
//"hash[UNDO_HASH_SIZE]" must be the hash value of the buffer text.
void
u_read_undo(CS name, Arr(Byte) hash, CS orig_name) {
   CS file_name;
   UndoLine line_ptr;
   long  last_save_nr = 0;
   int  old_idx = -1, neidx = -1, cur_idx = -1;
   long  num_read_uhps = 0;
   Tyme seq_time;
   int c;
   UndoHeader* uhp;
   UndoHeader** uhp_table = NULL;
   Byte read_hash[UNDO_HASH_SIZE];
   Byte magic_buf[UF_START_MAGIC_LEN];
#ifdef U_DEBUG
   int* uhp_table_used;
#endif
   FileStat st_orig;
   FileStat st_undo;
   BufInfo bi;

   CLEAR_FIELD(bi);
   line_ptr.ul_len = 0;
   line_ptr.ul_textlen = 0;
   line_ptr.ul_line = NULL;

   if (!name) {
      file_name = fiBuildSwapOrUndoFname(curBook->fullFileName, true);
      if (!file_name)
          return;

      // For safety we only read an undo file if the owner is equal to the
      // owner of the text file or equal to the current user.
      if (stat((char *)orig_name, &st_orig) >= 0
         && stat((char *)file_name, &st_undo) >= 0
         && st_orig.st_uid != st_undo.st_uid
         && st_undo.st_uid != getuid()
      ) {
         if (p_verbose > 0) {
            verbose_enter();
            smsg(_("Not reading undo file, owner differs: %s"), file_name);
            verbose_leave();
         }
         return;
      }
   } else
      file_name = name;

   if (p_verbose > 0) {
      verbose_enter();
      smsg(_("Reading undo file: %s"), file_name);
      verbose_leave();
   }

   FILE* fp = fopen((char *)file_name, "r");
   if (!fp) {
      if (name || p_verbose > 0)
         showErrFmtMsg(_(e_cannot_open_undo_file_for_reading_str), file_name);
      goto error;
   }
   bi.bk = curBook;
   bi.file = fp;

   //Read the undo file header.
   if (fread(magic_buf, UF_START_MAGIC_LEN, 1, fp) != 1
      || memcmp(magic_buf, UF_START_MAGIC, UF_START_MAGIC_LEN) != 0
   ) {
      showErrFmtMsg(_(e_not_an_undo_file_str), file_name);
      goto error;
   }
   int version = get2c(fp);
   if (version != UF_VERSION) {
      showErrFmtMsg(_(e_incompatible_undo_file_str), file_name);
      goto error;
   }

   if (undo_read(&bi, read_hash, (Unt)UNDO_HASH_SIZE) == FAIL) {
      corruption_error("hash", file_name);
      goto error;
   }
   LineNr line_count = (LineNr)undo_read_4c(&bi);
   if (memcmp(hash, read_hash, UNDO_HASH_SIZE) != 0 || line_count != curBook->mem.lineCount) {
      if (p_verbose > 0 || name != NULL) {
         if (!name)
            verbose_enter();
         give_warning((CS) _("File contents changed, cannot use undo info"), true);
         if (name == NULL)
            verbose_leave();
      }
      goto error;
   }

   // Read undo data for "U" command.
   int str_len = undo_read_4c(&bi);
   if (str_len < 0)
      goto error;
   if (str_len > 0) {
      line_ptr.ul_line = readStringFromFile(&bi, (Unt)str_len);
      line_ptr.ul_len = str_len + 1;
      line_ptr.ul_textlen = str_len;
   }
   LineNr line_lnum = (LineNr)undo_read_4c(&bi);
   LineNr line_colnr = (ColNr)undo_read_4c(&bi);
   if (line_lnum < 0 || line_colnr < 0) {
      corruption_error("line lnum/col", file_name);
      goto error;
   }

   // Begin general undo data
   long old_header_seq = undo_read_4c(&bi);
   long new_header_seq = undo_read_4c(&bi);
   long cur_header_seq = undo_read_4c(&bi);
   long num_head = undo_read_4c(&bi);
   long seq_last = undo_read_4c(&bi);
   long seq_cur = undo_read_4c(&bi);
   seq_time = undo_read_time(&bi);

   // Optional header fields.
   for (;;) {
      int len = undo_read_byte(&bi);
      if (len == 0 || len == EOF)
         break;
      int what = undo_read_byte(&bi);
      switch (what) {
      case UF_LAST_SAVE_NR:
         last_save_nr = undo_read_4c(&bi);
         break;
      default:
         // field not supported, skip
         while (--len >= 0)
             (void)undo_read_byte(&bi);
      }
   }

   // uhp_table will store the freshly created undo headers we allocate
   // until we insert them into curBook. The table remains sorted by the
   // sequence numbers of the headers.
   // When there are no headers uhp_table is NULL.
   if (num_head > 0) {
      if (num_head < LONG_MAX / (long)sizeof(UndoHeader *))
         uhp_table = U_ALLOC_LINE(num_head * sizeof(UndoHeader *));
      if (uhp_table == NULL)
         goto error;
   }

   while ((c = undo_read_2c(&bi)) == UF_HEADER_MAGIC) {
      if (num_read_uhps >= num_head) {
         corruption_error("num_head too small", file_name);
         goto error;
      }

      uhp = unserialize_uhp(&bi, file_name);
      if (!uhp)
         goto error;
      uhp_table[num_read_uhps++] = uhp;
   }

   if (num_read_uhps != num_head) {
      corruption_error("num_head", file_name);
      goto error;
   }
   if (c != UF_HEADER_END_MAGIC) {
      corruption_error("end marker", file_name);
      goto error;
   }

#ifdef U_DEBUG
   uhp_table_used = allocZeroed(sizeof(int) * num_head + 1);
# define SET_FLAG(j) ++uhp_table_used[j]
#else
# define SET_FLAG(j)
#endif

   // We have put all of the headers into a table. Now we iterate through the
   // table and swizzle each sequence number we have stored in uh_*_seq into
   // a pointer corresponding to the header with that sequence number.
   for (int i = 0; i < num_head; i++) {
      uhp = uhp_table[i];
      if (!uhp)
          continue;
      for (int j = 0; j < num_head; j++) {
         if (uhp_table[j] && i != j && uhp_table[i]->uh_seq == uhp_table[j]->uh_seq) {
            corruption_error("duplicate uh_seq", file_name);
            goto error;
         }
      } 
      for (int j = 0; j < num_head; j++) {
         if (uhp_table[j] != NULL && uhp_table[j]->uh_seq == uhp->next.seq) {
            uhp->next.ptr = uhp_table[j];
            SET_FLAG(j);
            break;
         }
      } 
      for (int j = 0; j < num_head; j++) {
         if (uhp_table[j] != NULL && uhp_table[j]->uh_seq == uhp->prev.seq) {
            uhp->prev.ptr = uhp_table[j];
            SET_FLAG(j);
            break;
         }
      } 
      for (int j = 0; j < num_head; j++) {
         if (uhp_table[j] != NULL && uhp_table[j]->uh_seq == uhp->altNext.seq) {
            uhp->altNext.ptr = uhp_table[j];
            SET_FLAG(j);
            break;
         }
      } 
      for (int j = 0; j < num_head; j++) {
         if (uhp_table[j] != NULL && uhp_table[j]->uh_seq == uhp->altPrev.seq) {
            uhp->altPrev.ptr = uhp_table[j];
            SET_FLAG(j);
            break;
         }
      } 
      if (old_header_seq > 0 && old_idx < 0 && uhp->uh_seq == old_header_seq) {
         old_idx = i;
         SET_FLAG(i);
      }
      if (new_header_seq > 0 && neidx < 0 && uhp->uh_seq == new_header_seq) {
         neidx = i;
         SET_FLAG(i);
      }
      if (cur_header_seq > 0 && cur_idx < 0 && uhp->uh_seq == cur_header_seq) {
         cur_idx = i;
         SET_FLAG(i);
      }
   }

   //Now that we have read the undo info successfully, free the current undo
   //info and use the info from the file.
   u_blockfree(curBook);
   curBook->undo = (Undo){
       .oldHead = old_idx < 0 ? NULL : uhp_table[old_idx],    
       .newHead = neidx < 0 ? NULL : uhp_table[neidx],    
       .currHead = cur_idx < 0 ? NULL : uhp_table[cur_idx],    
       .line = line_ptr,     .lineLnum = line_lnum,    
       .lineCol = line_colnr, .countHeaders = num_head,    
       .seqLast = seq_last,     .seqCurr = seq_cur,    
       .timeCurr = seq_time,    
       .saveNrLast = last_save_nr,    
       .saveNrCurr = last_save_nr,
       .synced = true
   };

   eeglFree(uhp_table);

#ifdef U_DEBUG
   for (i = 0; i < num_head; ++i) {
      if (uhp_table_used[i] == 0)
         showErrFmtMsg("uhp_table entry %ld not used, leaking memory", i);
   } 
   eeglFree(uhp_table_used);
   u_check(true);
#endif

   if (name)
      smsg(_("Finished reading undo file %s"), file_name);
   goto theend;

error:
   eeglFree(line_ptr.ul_line);
   if (uhp_table) {
      for (int i = 0; i < num_read_uhps; i++) {
         if (uhp_table[i])
            u_free_uhp(uhp_table[i]);
      } 
      eeglFree(uhp_table);
   }

theend:
   if (fp)
      fclose(fp);
   if (file_name != name)
      eeglFree(file_name);
   return;
}

void
u_undo(int count) {
   //If we get an undo command while executing a macro, we behave like the
   //original vi. If this happens twice in one macro the result will not be compatible.
   if (curBook->undo.synced == false) {
      u_sync(true);
      count = 1;
   }

   undo_undoes = true;
   u_doit(count);
}

void
u_redo(int count) {
   undo_undoes = false;
   u_doit(count);
}

//Undo or redo, depending on 'undo_undoes', 'count' times.
private void
u_doit(int startcount) {

   if (!undo_allowed())
       return;

   int count = startcount;
   u_newcount = 0;
   u_oldcount = 0;
   if (curBook->mem.flags & ML_EMPTY)
       u_oldcount = -1;
   while (count--) {
         // Do the change warning now, so that it triggers FileChangedRO when
         // needed.  This may cause the file to be reloaded, that must happen
         // before we do anything, because it may change curBook->undo.currHead and more.
         change_warning(0);

         if (undo_undoes) {
            if (curBook->undo.currHead == NULL) { // first undo
               curBook->undo.currHead = curBook->undo.newHead;
            } ei (p_ul > 0) {// multi level undo
               // get next undo
               curBook->undo.currHead = curBook->undo.currHead->next.ptr;
            }
            // nothing to undo
            if (curBook->undo.countHeaders == 0 || curBook->undo.currHead == NULL) {
               // stick curBook->undo.currHead at end
               curBook->undo.currHead = curBook->undo.oldHead;
               beep_flush();
               if (count == startcount - 1) {
                  msg(_("Already at oldest change"));
                  return;
               }
               break;
            }
            u_undoredo(true);
         } else {
             if (curBook->undo.currHead == NULL || p_ul <= 0) {
                beep_flush();   // nothing to redo
                if (count == startcount - 1) {
                   msg(_("Already at newest change"));
                   return;
                }
                break;
             }

             u_undoredo(false);

             // Advance for next redo. Set "newhead" when at the end of the redoable changes.
             if (curBook->undo.currHead->prev.ptr == NULL)
                curBook->undo.newHead = curBook->undo.currHead;
             curBook->undo.currHead = curBook->undo.currHead->prev.ptr;
         }
    }
    u_undo_end(undo_undoes, false);
}

//Undo or redo over the timeline.
//When "step" is negative go back in time, otherwise goes forward in time.
//When "sec" is false make "step" steps, when "sec" is true use "step" as
//seconds.
//When "file" is true use "step" as a number of file writes.
//When "absolute" is true use "step" as the sequence number to jump to. "sec" must be false then.
void
undo_time(long step, int sec, int file, int absolute) {
   long target;
   long closest;
   long closest_start;
   long closest_seq = 0;
   long val;
   UndoHeader* uhp = NULL;
   UndoHeader* last;
   int mark;
   int nomark = 0;  // shut up compiler
   int round;
   int dosec = sec;
   int dofile = file;
   int above = false;
   int did_undo = true;

   if (text_locked()) {
      text_locked_msg();
      return;
   }

   // First make sure the current undoable change is synced.
   if (curBook->undo.synced == false)
      u_sync(true);

   u_newcount = 0;
   u_oldcount = 0;
   if (curBook->mem.flags & ML_EMPTY)
      u_oldcount = -1;

   // "target" is the node below which we want to be.
   // Init "closest" to a value we can't reach.
   if (absolute) {
      target = step;
      closest = -1;
   } else {
      if (dosec)
          target = (long)(curBook->undo.timeCurr) + step;
      ei (dofile) {
         if (step < 0) {
            // Going back to a previous write. If there were changes after the last write, count that as 
            // moving one file-write, so that ":earlier 1f" undoes all changes since the last save.
            uhp = curBook->undo.currHead;
            if (uhp)
               uhp = uhp->next.ptr;
            else
               uhp = curBook->undo.newHead;
            if (uhp && uhp->uh_save_nr != 0)
               //"uh_save_nr" was set in the last block, that means
               //there were no changes since the last write
               target = curBook->undo.saveNrCurr + step;
            else
               // count the changes since the last write as one step
               target = curBook->undo.saveNrCurr + step + 1;
            if (target <= 0)
               //Go to before first write: before the oldest change. Use the sequence number for that
               dofile = false;
         } else {
            // Moving forward to a newer write.
            target = curBook->undo.saveNrCurr + step;
            if (target > curBook->undo.saveNrLast) {
               // Go to after last write: after the latest change. Use the sequence number for that.
               target = curBook->undo.seqLast + 1;
               dofile = false;
            }
         }
      } else
         target = curBook->undo.seqCurr + step;
      if (step < 0) {
         if (target < 0)
            target = 0;
         closest = -1;
      } else {
         if (dosec)
            closest = (long)(eeTime() + 1);
         ei (dofile)
            closest = curBook->undo.saveNrLast + 2;
         else
            closest = curBook->undo.seqLast + 2;
         if (target >= closest)
            target = closest - 1;
      }
   }
   closest_start = closest;
   closest_seq = curBook->undo.seqCurr;

   // When "target" is 0; Back to origin.
   if (target == 0) {
      mark = lastmark;  // avoid that GCC complains
      goto target_zero;
   }

   //May do this twice:
   //1. Search for "target", update "closest" to the best match found.
   //2. If "target" not found search for "closest".
   //
   //When using the closest time we use the sequence number in the second
   //round, because there may be several entries with the same time.
   for (round = 1; round <= 2; ++round) {
      // Find the path from the current state to where we want to go.  The
      // desired state can be anywhere in the undo tree, need to go all over
      // it.  We put "nomark" in uh_walk where we have been without success,
      // "mark" where it could possibly be.
      mark = ++lastmark;
      nomark = ++lastmark;

      if (curBook->undo.currHead == NULL)   // at leaf of the tree
         uhp = curBook->undo.newHead;
      else
         uhp = curBook->undo.currHead;

      while (uhp) {
         uhp->uh_walk = mark;
         if (dosec)
            val = (long)(uhp->uh_time);
         ei (dofile)
            val = uhp->uh_save_nr;
         else
            val = uhp->uh_seq;

         if (round == 1 && !(dofile && val == 0)) {
            //Remember the header that is closest to the target. It must be at least in the right 
            //direction (checked with "seqCurr").  When the timestamp is equal find the
            //highest/lowest sequence number.
            if ((step < 0 ? uhp->uh_seq <= curBook->undo.seqCurr
                     : uhp->uh_seq > curBook->undo.seqCurr)
               && ((dosec && val == closest)
                   ? (step < 0
                  ? uhp->uh_seq < closest_seq
                  : uhp->uh_seq > closest_seq)
                   : closest == closest_start
                  || (val > target
                      ? (closest > target
                     ? val - target <= closest - target
                     : val - target <= target - closest)
                      : (closest > target
                     ? target - val <= closest - target
                     : target - val <= target - closest)))
            ) {
               closest = val;
               closest_seq = uhp->uh_seq;
            }
         }

         // Quit searching when we found a match.  But when searching for a
         // time we need to continue looking for the best uh_seq.
         if (target == val && !dosec) {
            target = uhp->uh_seq;
            break;
         }

         // go down in the tree if we haven't been there
         if (uhp->prev.ptr != NULL && uhp->prev.ptr->uh_walk != nomark
                   && uhp->prev.ptr->uh_walk != mark)
         uhp = uhp->prev.ptr;

         // go to alternate branch if we haven't been there
         ei (uhp->altNext.ptr
             && uhp->altNext.ptr->uh_walk != nomark
             && uhp->altNext.ptr->uh_walk != mark
         )
            uhp = uhp->altNext.ptr;

         //go up in the tree if we haven't been there and we are at the
         //start of alternate branches
         ei (uhp->next.ptr && !uhp->altPrev.ptr
             && uhp->next.ptr->uh_walk != nomark
             && uhp->next.ptr->uh_walk != mark
         ) {
            // If still at the start we don't go through this change.
            if (uhp == curBook->undo.currHead)
               uhp->uh_walk = nomark;
            uhp = uhp->next.ptr;
         } else {
            // need to backtrack; mark this node as useless
            uhp->uh_walk = nomark;
            if (uhp->altPrev.ptr != NULL)
               uhp = uhp->altPrev.ptr;
            else
               uhp = uhp->next.ptr;
         }
      }

      if (uhp)    // found it
         break;

      if (absolute) {
         showErrFmtMsg(_(e_undo_number_nr_not_found), step);
         return;
      }

      if (closest == closest_start) {
         if (step < 0)
            msg(_("Already at oldest change"));
         else
            msg(_("Already at newest change"));
         return;
      }

      target = closest_seq;
      dosec = false;
      dofile = false;
      if (step < 0)
         above = true;   // stop above the header
   }

target_zero:
   //If we found it: Follow the path to go to where we want to be.
   
   if (!uhp && target != 0) {
      goto theEnd;
   } 
   //First go up the tree as much as needed.
   while (!gotInterruptG) {
      // Do the change warning now, for the same reason as above.
      change_warning(0);

      uhp = curBook->undo.currHead;
      if (!uhp)
         uhp = curBook->undo.newHead;
      else
         uhp = uhp->next.ptr;
      if (!uhp || (target > 0 && uhp->uh_walk != mark) || (uhp->uh_seq == target && !above))
         break;
      curBook->undo.currHead = uhp;
      u_undoredo(true);
      if (target > 0)
         uhp->uh_walk = nomark;   // don't go back down here
   }

   // When back to origin, redo is not needed.
   if (target > 0) {
      //And now go down the tree (redo), branching off where needed.
      while (!gotInterruptG) {
         //Do the change warning now, for the same reason as above.
         change_warning(0);

         uhp = curBook->undo.currHead;
         if (!uhp)
            break;

         // Go back to the first branch with a mark.
         while (uhp->altPrev.ptr != NULL && uhp->altPrev.ptr->uh_walk == mark)
            uhp = uhp->altPrev.ptr;

         // Find the last branch with a mark, that's the one.
         last = uhp;
         while (last->altNext.ptr != NULL && last->altNext.ptr->uh_walk == mark)
            last = last->altNext.ptr;
         if (last != uhp) {
            // Make the used branch the first entry in the list of
            // alternatives to make "u" and CTRL-R take this branch.
            while (uhp->altPrev.ptr != NULL)
               uhp = uhp->altPrev.ptr;
            if (last->altNext.ptr != NULL)
               last->altNext.ptr->altPrev.ptr = last->altPrev.ptr;
            last->altPrev.ptr->altNext.ptr = last->altNext.ptr;
            last->altPrev.ptr = NULL;
            last->altNext.ptr = uhp;
            uhp->altPrev.ptr = last;

            if (curBook->undo.oldHead == uhp)
               curBook->undo.oldHead = last;
            uhp = last;
            if (uhp->next.ptr != NULL)
               uhp->next.ptr->prev.ptr = uhp;
         }
         curBook->undo.currHead = uhp;

         if (uhp->uh_walk != mark)
            break;       // must have reached the target

         //Stop when going backwards in time and didn't find the exact header we were looking for.
         if (uhp->uh_seq == target && above) {
            curBook->undo.seqCurr = target - 1;
            break;
         }

         u_undoredo(false);

         // Advance "curhead" to below the header we last used.  If it
         // becomes NULL then we need to set "newhead" to this leaf.
         if (uhp->prev.ptr == NULL)
            curBook->undo.newHead = uhp;
         curBook->undo.currHead = uhp->prev.ptr;
         did_undo = false;

         if (uhp->uh_seq == target)   // found it!
             break;

         uhp = uhp->prev.ptr;
         if (uhp == NULL || uhp->uh_walk != mark) {
             // Need to redo more but can't find it...
             internal_error((CS)"undo_time()");
             break;
         }
      }
   }
theEnd: 
   u_undo_end(did_undo, absolute);
}

//u_undoredo: common code for undo and redo
//
//The lines in the file are replaced by the lines in the entry list at
//curBook->undo.currHead. The replaced lines in the file are saved in the entry
//list for the next undo/redo.
//
//When "undo" is true we go up in the tree, when false we go down.
private void
u_undoredo(Boole undo) {
   UndoLine* newarray = NULL;
   LineNr oldsize;
   LineNr newsize;
   LineNr top, bot;
   LineNr lnum;
   LineNr newlnum = MAXLNUM;
   Pos new_curpos = curPor->cursor;
   long i;
   UndoEntry *newlist = NULL;
   int old_flags;
   int new_flags;
   Pos namedm[NMARKS];
   VisualInfo visualinfo;
   int empty_buffer;          // buffer became empty
   UndoHeader* curhead = curBook->undo.currHead;

   // Don't want autocommands using the undo structures here, they are invalid till the end.
   block_autocmds();

#ifdef U_DEBUG
   u_check(false);
#endif
   old_flags = curhead->uh_flags;
   new_flags = (curBook->wasModified ? UH_CHANGED : 0) +
         ((curBook->mem.flags & ML_EMPTY) ? UH_EMPTYBUF : 0);
   setpcmark();

   //save marks before undo/redo
   mch_memmove(namedm, curBook->namedMarks, sizeof(Pos) * NMARKS);
   visualinfo = curBook->visual;
   curBook->opStart.lnum = curBook->mem.lineCount;
   curBook->opStart.col = 0;
   curBook->opEnd.lnum = 0;
   curBook->opEnd.col = 0;

   UndoEntry *nuep;
   for (UndoEntry* uep = curhead->uh_entry; uep != NULL; uep = nuep) {
      top = uep->ue_top;
      bot = uep->ue_bot;
      if (bot == 0)
          bot = curBook->mem.lineCount + 1;
      if (top > curBook->mem.lineCount || top >= bot || bot > curBook->mem.lineCount + 1) {
         unblock_autocmds();
         internalErrMsg(e_u_undo_line_numbers_wrong);
         changed();      // don't want UNCHANGED now
         return;
      }

      oldsize = bot - top - 1;    // number of lines before undo
      newsize = uep->ue_size;       // number of lines after undo

      // Decide about the cursor position, depending on what text changed.
      // Don't set it yet, it may be invalid if lines are going to be added.
      if (top < newlnum) {
         // If the saved cursor is somewhere in this undo block, move it to
         // the remembered position.  Makes "gwap" put the cursor back where it was.
         lnum = curhead->uh_cursor.lnum;
         if (lnum >= top && lnum <= top + newsize + 1) {
            new_curpos = curhead->uh_cursor;
            newlnum = new_curpos.lnum - 1;
         } else {
            // Use the first line that actually changed. Avoids that
            // undoing auto-formatting puts the cursor in the previous line.
            for (i = 0; i < newsize && i < oldsize; ++i) {
               CS p = ml_get(top + 1 + i);

               if (curBook->mem.lineLen != uep->ue_array[i].ul_len
                   || memcmp(uep->ue_array[i].ul_line, p, curBook->mem.lineLen) != 0
               )
                  break;
            }
            if (i == newsize && newlnum == MAXLNUM && uep->ue_next == NULL) {
               newlnum = top;
               new_curpos.lnum = newlnum + 1;
            } ei (i < newsize) {
               newlnum = top + i;
               new_curpos.lnum = newlnum + 1;
            }
         }
      }

      empty_buffer = false;

      //Delete the lines between top and bot and save them in newarray.
      if (oldsize > 0) {
         newarray = U_ALLOC_LINE(sizeof(UndoLine) * oldsize);
         // delete backwards, it goes faster in most cases
         for (lnum = bot - 1, i = oldsize; --i >= 0; --lnum) {
            // what can we do when we run out of memory?
            if (u_save_line(&newarray[i], lnum) == FAIL)
               do_outofmem_msg((Ulong)0);
            //remember we deleted the last line in the buffer, and a
            //dummy empty line will be inserted
            if (curBook->mem.lineCount == 1)
               empty_buffer = true;
            ml_delete_flags(lnum, ML_DEL_UNDO);
         }
      }
      else
          newarray = NULL;

      // make sure the cursor is on a valid line after the deletions
      check_cursor_lnum();

      //Insert the lines in u_array between top and bot.
      if (newsize) {
         for (lnum = top, i = 0; i < newsize; ++i, ++lnum) {
            //If the file is empty, there is an empty line 1 that we
            //should get rid of, by replacing it with the new line.
            if (empty_buffer && lnum == 0)
                ml_replace_len((LineNr)1, uep->ue_array[i].ul_line,
                       uep->ue_array[i].ul_len, true, true);
            else
                ml_append_flags(lnum, uep->ue_array[i].ul_line,
                    (ColNr)uep->ue_array[i].ul_len, ML_APPEND_UNDO);
            eeglFree(uep->ue_array[i].ul_line);
         }
         eeglFree((CS)uep->ue_array);
      }

      // adjust marks
      if (oldsize != newsize) {
         markAdjust(top + 1, top + oldsize, (long)MAXLNUM, (long)newsize - (long)oldsize, true);
         if (curBook->opStart.lnum > top + oldsize)
            curBook->opStart.lnum += newsize - oldsize;
         if (curBook->opEnd.lnum > top + oldsize)
            curBook->opEnd.lnum += newsize - oldsize;
      }
      if (oldsize > 0 || newsize > 0) {
         changed_lines(top + 1, 0, bot, newsize - oldsize);
      }

      // Set the '[ mark.
      if (top + 1 < curBook->opStart.lnum)
         curBook->opStart.lnum = top + 1;
      // Set the '] mark.
      if (newsize == 0 && top + 1 > curBook->opEnd.lnum)
         curBook->opEnd.lnum = top + 1;
      ei (top + newsize > curBook->opEnd.lnum)
         curBook->opEnd.lnum = top + newsize;

      u_newcount += newsize;
      u_oldcount += oldsize;
      uep->ue_size = oldsize;
      uep->ue_array = newarray;
      uep->ue_bot = top + newsize + 1;

      // insert this entry in front of the new entry list
      nuep = uep->ue_next;
      uep->ue_next = newlist;
      newlist = uep;
   }

   // Ensure the '[ and '] marks are within bounds.
   if (curBook->opStart.lnum > curBook->mem.lineCount)
      curBook->opStart.lnum = curBook->mem.lineCount;
   if (curBook->opEnd.lnum > curBook->mem.lineCount)
      curBook->opEnd.lnum = curBook->mem.lineCount;

   // Set the cursor to the desired position.  Check that the line is valid.
   curPor->cursor = new_curpos;
   check_cursor_lnum();

   curhead->uh_entry = newlist;
   curhead->uh_flags = new_flags;
   if ((old_flags & UH_EMPTYBUF) && CURBOOK_EMPTY())
      curBook->mem.flags |= ML_EMPTY;
   if (old_flags & UH_CHANGED)
      changed();
   else
      unchanged(curBook, true);

   //restore marks from before undo/redo
   for (i = 0; i < NMARKS; ++i) {
      if (curhead->uh_namedm[i].lnum != 0)
         curBook->namedMarks[i] = curhead->uh_namedm[i];
      if (namedm[i].lnum != 0)
         curhead->uh_namedm[i] = namedm[i];
      else
         curhead->uh_namedm[i].lnum = 0;
   }
   if (curhead->uh_visual.vi_start.lnum != 0) {
      curBook->visual = curhead->uh_visual;
      curhead->uh_visual = visualinfo;
   }

   //If the cursor is only off by one line, put it at the same position as
   //before starting the change (for the "o" command).
   //Otherwise the cursor should go to the first undone line.
   if (curhead->uh_cursor.lnum + 1 == curPor->cursor.lnum && curPor->cursor.lnum > 1)
      --curPor->cursor.lnum;
   if (curPor->cursor.lnum <= curBook->mem.lineCount) {
      if (curhead->uh_cursor.lnum == curPor->cursor.lnum) {
         curPor->cursor.col = curhead->uh_cursor.col;
         if (virtual_active() && curhead->uh_cursor_vcol >= 0)
            coladvance((ColNr)curhead->uh_cursor_vcol);
         else
            curPor->cursor.coladd = 0;
      } else
         beginline(BL_SOL | BL_FIX);
   } else {
      // We get here with the current cursor line being past the end (eg after adding lines at the 
      // end of the file, and then undoing it). check_cursor() will move the cursor to the last 
      // line. Move it to the first column here.
      curPor->cursor.col = 0;
      curPor->cursor.coladd = 0;
   }

   // Make sure the cursor is on an existing line and column.
   check_cursor();

   // Remember where we are for "g-" and ":earlier 10s".
   curBook->undo.seqCurr = curhead->uh_seq;
   if (undo) {
      //We are below the previous undo.  However, to make ":earlier 1s"
      //work we compute this as being just above the just undone change.
      if (curhead->next.ptr != NULL)
         curBook->undo.seqCurr = curhead->next.ptr->uh_seq;
      else
         curBook->undo.seqCurr = 0;
   }

   // Remember where we are for ":earlier 1f" and ":later 1f".
   if (curhead->uh_save_nr != 0) {
      if (undo)
         curBook->undo.saveNrCurr = curhead->uh_save_nr - 1;
      else
         curBook->undo.saveNrCurr = curhead->uh_save_nr;
   }

   //The timestamp can be the same for multiple changes, just use the one of
   //the undone/redone change.
   curBook->undo.timeCurr = curhead->uh_time;

   unblock_autocmds();
#ifdef U_DEBUG
   u_check(false);
#endif
}

//If we deleted or added lines, report the number of less/more lines. Otherwise, report the number 
//of changes (this may be incorrect in some cases, but it's better than nothing).
private void
u_undo_end(
   Boole did_undo,  // just did an undo
   Boole absolute   // used ":undo N"
){
   CS msgstr;
   UndoHeader   *uhp;
   Byte msgbuf[80];

   if ((p_fdo & FDO_UNDO) && keyWasTypedG)
      foldOpenCursor();

   if (global_busy       // no messages now, wait until global is finished
          || !messaging())  // 'lazyredraw' set, don't do messages now
      return;

   if (curBook->mem.flags & ML_EMPTY)
      --u_newcount;

   u_oldcount -= u_newcount;
   if (u_oldcount == -1)
      msgstr = N_("more line");
   ei (u_oldcount < 0)
      msgstr = N_("more lines");
   ei (u_oldcount == 1)
      msgstr = N_("line less");
   ei (u_oldcount > 1)
      msgstr = N_("fewer lines");
   else {
      u_oldcount = u_newcount;
      if (u_newcount == 1)
         msgstr = N_("change");
      else
         msgstr = N_("changes");
   }

   if (curBook->undo.currHead != NULL) {
   // For ":undo N" we prefer a "after #N" message.
   if (absolute && curBook->undo.currHead->next.ptr != NULL) {
      uhp = curBook->undo.currHead->next.ptr;
      did_undo = false;
   } ei (did_undo)
      uhp = curBook->undo.currHead;
   else
      uhp = curBook->undo.currHead->next.ptr;
   } else
      uhp = curBook->undo.newHead;

   if (!uhp)
      *msgbuf = ZERO;
   else
      add_time(msgbuf, sizeof(msgbuf), uhp->uh_time);

   if (VIsual_active)
     check_pos(curBook, &VIsual);

   smsgDecoKeep(
       0, _("%ld %s; %s #%ld  %s"),
       u_oldcount < 0 ? -u_oldcount : u_oldcount,
       _(msgstr),
       did_undo ? _("before") : _("after"),
       uhp == NULL ? 0L : uhp->uh_seq,
       msgbuf
   );
}

// u_sync: stop adding to the current entry list
void
u_sync(int force) {  // Also sync when no_u_sync is set.
   // Skip it when already synced or syncing is disabled.
   if (curBook->undo.synced || (!force && no_u_sync > 0))
      return;
   if (p_ul < 0)
      curBook->undo.synced = true;  // no entries, nothing to do
   else {
      u_getbot();          // compute ue_bot of previous u_save
      curBook->undo.currHead = NULL;
   }
}

//":undolist": List the leaves of the undo tree
void
c_undolist(Invocation* invo UNUSED) {
   ArrayList   ga;
   UndoHeader   *uhp;
   int      mark;
   int      nomark;
   int      changes = 1;
   int      len;

   //1: walk the tree to find all leafs, put the info in "ga".
   //2: sort the lines
   //3: display the list
   mark = ++lastmark;
   nomark = ++lastmark;
   ga_init2(&ga, sizeof(char *), 20);

   uhp = curBook->undo.oldHead;
   while (uhp != NULL) {
      if (uhp->prev.ptr == NULL && uhp->uh_walk != nomark && uhp->uh_walk != mark) {
         if (ga_grow(&ga, 1) == FAIL)
            break;
         len = eeSnprintf(IObuff, IOSIZE, "%6ld %7d  ", uhp->uh_seq, changes);
         add_time(IObuff + len, IOSIZE - len, uhp->uh_time);

         // we have to call STRLEN() here because add_time() does not report
         // the number of characters added.
         len += (int)STRLEN(IObuff + len);
         if (uhp->uh_save_nr > 0) {
            int n = (len >= 33) ? 0 : 33 - len;

            len += eeSnprintf(
                  IObuff + len, IOSIZE - len, "%*.*s  %3ld", n, n, " ", uhp->uh_save_nr
            );
         }
         ((Byte **)(ga.c))[ga.len++] = copySubstr(IObuff, len);
      }

      uhp->uh_walk = mark;

      // go down in the tree if we haven't been there
      if (uhp->prev.ptr != NULL && uhp->prev.ptr->uh_walk != nomark
                   && uhp->prev.ptr->uh_walk != mark)
      {
          uhp = uhp->prev.ptr;
          ++changes;
      }

      // go to alternate branch if we haven't been there
      ei (uhp->altNext.ptr != NULL
         && uhp->altNext.ptr->uh_walk != nomark
         && uhp->altNext.ptr->uh_walk != mark)
          uhp = uhp->altNext.ptr;

      // go up in the tree if we haven't been there and we are at the
      // start of alternate branches
      ei (uhp->next.ptr != NULL && uhp->altPrev.ptr == NULL
         && uhp->next.ptr->uh_walk != nomark
         && uhp->next.ptr->uh_walk != mark)
      {
          uhp = uhp->next.ptr;
          --changes;
      }

      else {
          // need to backtrack; mark this node as done
          uhp->uh_walk = nomark;
          if (uhp->altPrev.ptr != NULL)
         uhp = uhp->altPrev.ptr;
          else
          {
         uhp = uhp->next.ptr;
         --changes;
          }
      }
   }

   if (ga.len == 0)
      msg(_("Nothing to undo"));
   else {
      sortStrings((Byte **)ga.c, ga.len);

      msg_start();
      msgPutsDeco(_("number changes  when               saved"), getDecoFlags(HLF_T));
      for (Unt i = 0; i < (Unt)ga.len && !gotInterruptG; ++i) {
         msg_putchar('\n');
         if (gotInterruptG)
            break;
         msg_puts(((Byte **)ga.c)[i]);
      }
      msg_end();

      ga_clear_strings(&ga);
   }
}

//":undojoin": continue adding to the last entry list
void
c_undojoin(Invocation* invo UNUSED) {
   if (curBook->undo.newHead == NULL)
      return;          // nothing changed before
   if (curBook->undo.currHead != NULL) {
      emsg(_(e_undojoin_is_not_allowed_after_undo));
      return;
   }
   if (!curBook->undo.synced)
      return;          // already unsynced
   if (p_ul < 0)
      return;          // no entries, nothing to do
   else
      // Append next change to the last entry
      curBook->undo.synced = false;
}

//Called after writing or reloading the file and setting wasModified to false.
//Now an undo means that the buffer is modified.
void
u_unchanged(Book* book) {
   u_unch_branch(book->undo.oldHead);
   book->didWarnReadonly = false;
}

//After reloading a buffer which was saved for 'undoreload': Find the first
//line that was changed and set the cursor there.
void
u_find_first_changed(void) {
   UndoHeader   *uhp = curBook->undo.newHead;
   LineNr   lnum;

   if (curBook->undo.currHead != NULL || uhp == NULL)
      return;  // undid something in an autocmd?

   // Check that the last undo block was for the whole file.
   UndoEntry* uep = uhp->uh_entry;
   if (uep->ue_top != 0 || uep->ue_bot != 0)
      return;

   for (lnum = 1; lnum < curBook->mem.lineCount && lnum <= uep->ue_size; ++lnum) {
      CS p = memGetLine(curBook, lnum, false);

      if (uep->ue_array[lnum - 1].ul_len != curBook->mem.lineLen
         || memcmp(p, uep->ue_array[lnum - 1].ul_line, uep->ue_array[lnum - 1].ul_len) != 0
      ) {
         CLEAR_POS(&(uhp->uh_cursor));
         uhp->uh_cursor.lnum = lnum;
         return;
      }
   }
   if (curBook->mem.lineCount != uep->ue_size) {
      // lines added or deleted at the end, put the cursor there
      CLEAR_POS(&(uhp->uh_cursor));
      uhp->uh_cursor.lnum = lnum;
   }
}

//Increase the write count, store it in the last undo header, what would be used for "u".
void
u_update_save_nr(Book* book) {
   ++book->undo.saveNrLast;
   book->undo.saveNrCurr = book->undo.saveNrLast;
   UndoHeader* uhp = book->undo.currHead;
   uhp = uhp ? uhp->next.ptr : book->undo.newHead;
   if (uhp)
      uhp->uh_save_nr = book->undo.saveNrLast;
}

private void
u_unch_branch(UndoHeader* uhp) {
   for (UndoHeader* uh = uhp; uh != NULL; uh = uh->prev.ptr) {
      uh->uh_flags |= UH_CHANGED;
      if (uh->altNext.ptr != NULL)
          u_unch_branch(uh->altNext.ptr);       // recursive
   }
}

//Get pointer to last added entry. If it's not valid, give an error message and return NULL.
private UndoEntry *
u_get_headentry(void) {
   if (curBook->undo.newHead == NULL || curBook->undo.newHead->uh_entry == NULL) {
      internalErrMsg(e_undo_list_corrupt);
      return NULL;
   }
   return curBook->undo.newHead->uh_entry;
}

//u_getbot(): compute the line number of the previous u_save It is called only when synced is false
private void
u_getbot(void) {
   UndoEntry* uep = u_get_headentry();   // check for corrupt undo list
   if (!uep)
      return;

   uep = curBook->undo.newHead->uh_getbot_entry;
   if (uep) {
      //the new ue_bot is computed from the number of lines that has been
      //inserted (0 - deleted) since calling u_save. This is equal to the
      //old line count subtracted from the current line count.
      LineNr extra = curBook->mem.lineCount - uep->ue_lcount;
      uep->ue_bot = uep->ue_top + uep->ue_size + 1 + extra;
      if (uep->ue_bot < 1 || uep->ue_bot > curBook->mem.lineCount) {
          internalErrMsg(e_undo_line_missing);
          uep->ue_bot = uep->ue_top + 1;  // assume all lines deleted, will
                      // get all the old lines back without deleting the current ones
      }

      curBook->undo.newHead->uh_getbot_entry = NULL;
   }

   curBook->undo.synced = true;
}

//Free one header "uhp" and its entry list and adjust the pointers.
private void
u_freeheader(
   Book* book,
   UndoHeader* uhp,
   UndoHeader** uhpp)   // if not NULL reset when freeing this header
{
   UndoHeader* uhap;

   // When there is an alternate redo list free that branch completely,
   // because we can never go there.
   if (uhp->altNext.ptr != NULL)
      freeBranch(book, uhp->altNext.ptr, uhpp);

   if (uhp->altPrev.ptr != NULL)
      uhp->altPrev.ptr->altNext.ptr = NULL;

   // Update the links in the list to remove the header.
   if (uhp->next.ptr == NULL)
      book->undo.oldHead = uhp->prev.ptr;
   else
      uhp->next.ptr->prev.ptr = uhp->prev.ptr;

   if (uhp->prev.ptr == NULL)
      book->undo.newHead = uhp->next.ptr;
   else {
      for (uhap = uhp->prev.ptr; uhap != NULL; uhap = uhap->altNext.ptr)
         uhap->next.ptr = uhp->next.ptr;
   }

   u_freeentries(book, uhp, uhpp);
}

//Free an alternate branch and any following alternate branches.
private void
freeBranch(
   Book* book,
   UndoHeader* uhp,
   UndoHeader** uhpp   // if not NULL reset when freeing this header
){
   // If this is the top branch we may need to use u_freeheader() to update all the pointers.
   if (uhp == book->undo.oldHead) {
      while (book->undo.oldHead)
         u_freeheader(book, book->undo.oldHead, uhpp);
      return;
   }

   if (uhp->altPrev.ptr)
      uhp->altPrev.ptr->altNext.ptr = NULL;

   UndoHeader* next = uhp;
   UndoHeader* tofree;
   while (next) {
      tofree = next;
      if (tofree->altNext.ptr)
         freeBranch(book, tofree->altNext.ptr, uhpp);   // recursive
      next = tofree->prev.ptr;
      u_freeentries(book, tofree, uhpp);
   }
}

//Free all the undo entries for one header and the header itself.
//This means that "uhp" is invalid when returning.
private void
u_freeentries(
   Book       *book,
   UndoHeader       *uhp,
   UndoHeader       **uhpp)   // if not NULL reset when freeing this header
{
   UndoEntry       *uep, *nuep;

   // Check for pointers to the header that become invalid now.
   if (book->undo.currHead == uhp)
      book->undo.currHead = NULL;
   if (book->undo.newHead == uhp)
      book->undo.newHead = NULL;  // freeing the newest entry
   if (uhpp != NULL && uhp == *uhpp)
      *uhpp = NULL;

   for (uep = uhp->uh_entry; uep != NULL; uep = nuep) {
      nuep = uep->ue_next;
      freeEntry(uep, uep->ue_size);
   }

#ifdef U_DEBUG
   uhp->uh_magic = 0;
#endif
   eeglFree((CS)uhp);
   --book->undo.countHeaders;
}

//free entry 'uep' and 'n' lines in uep->ue_array[]
private void
freeEntry(UndoEntry *uep, long n) {
   while (n > 0)
      eeglFree(uep->ue_array[--n].ul_line);
   eeglFree((CS)uep->ue_array);
#ifdef U_DEBUG
   uep->ue_magic = 0;
#endif
   eeglFree((CS)uep);
}

//invalidate the undo buffer; called when storage has already been released
private void
invalidateUndoBuffer(Book *book) {
   book->undo.newHead = book->undo.oldHead = book->undo.currHead = NULL;
   book->undo.synced = true;
   book->undo.countHeaders = 0;
   book->undo.line.ul_line = NULL;
   book->undo.line.ul_len = 0;
   book->undo.line.ul_textlen = 0;
   book->undo.lineLnum = 0;
}

//Free all allocated memory blocks for the 'book'.
private void
u_blockfree(Book* book) {
   while (book->undo.oldHead)
      u_freeheader(book, book->undo.oldHead, NULL);
   eeglFree(book->undo.line.ul_line);
}

//Free all allocated memory blocks for the 'book'. and invalidate the undo buffer
void
invalidateUndoBufferAndFreeBlocks(Book* book) {
   u_blockfree(book);
   invalidateUndoBuffer(book);
}

// Save the line "lnum" for the "U" command.
private void
u_saveline(LineNr lnum) {
   if (lnum == curBook->undo.lineLnum)       // line is already saved
      return;
   if (lnum < 1 || lnum > curBook->mem.lineCount) // should never happen
      return;
   u_clearline();
   curBook->undo.lineLnum = lnum;
   if (curPor->cursor.lnum == lnum)
      curBook->undo.lineCol = curPor->cursor.col;
   else
      curBook->undo.lineCol = 0;
   if (u_save_line(&curBook->undo.line, lnum) == FAIL)
      do_outofmem_msg((Ulong)0);
}

//clear the line saved for the "U" command
//(this is used externally for crossing a line while in insert mode)
void
u_clearline(void) {
   if (curBook->undo.line.ul_line == NULL)
      return;

   EE_CLEAR(curBook->undo.line.ul_line);
   curBook->undo.line.ul_len = 0;
   curBook->undo.line.ul_textlen = 0;
   curBook->undo.lineLnum = 0;
}

//Implementation of the "U" command. We allow the cursor to be in another line.
//Careful: may trigger autocommands that reload the book.
void
u_undoline(void) {
   if (undo_off)
      return;

   if (curBook->undo.line.ul_line == NULL || curBook->undo.lineLnum > curBook->mem.lineCount) {
      beep_flush();
      return;
   }

   // first save the line for the 'u' command
   if (u_savecommon(curBook->undo.lineLnum - 1,
             curBook->undo.lineLnum + 1, (LineNr)0, false) == FAIL)
      return;
      
   UndoLine  oldp;
   if (u_save_line(&oldp, curBook->undo.lineLnum) == FAIL) {
      do_outofmem_msg((Ulong)0);
      return;
   }
   ml_replace_len(curBook->undo.lineLnum, curBook->undo.line.ul_line,
                 curBook->undo.line.ul_len, true, false);
   changed_bytes(curBook->undo.lineLnum, 0);
   curBook->undo.line = oldp;

   ColNr t = curBook->undo.lineCol;
   if (curPor->cursor.lnum == curBook->undo.lineLnum)
      curBook->undo.lineCol = curPor->cursor.col;
   curPor->cursor.col = t;
   curPor->cursor.lnum = curBook->undo.lineLnum;
   check_cursor_col();
}

//Check if the 'modified' flag is set. "nofile" and "scratch" type buffers are 
//considered to always be unchanged. Also considers a buffer changed when a terminal portal 
//contains a running job.
Boole
doWasBookChanged(Book* book) {
   if (term_job_running_not_none(book->term))
      return true;
   return wasBookChangedNotTerm(book);
}

//Return true if any book has changes. Also books that haven't been written.
Boole
doWasAnyBookChanged(void) {
   Book *book;
   FOR_ALL_BOOKS(book) {
      if (doWasBookChanged(book))
         return true;
   } 
   return false;
}

//Like doWasBookChanged() but ignoring a terminal portal.
private Boole
wasBookChangedNotTerm(Book* book) {
   // In a "prompt" book we do respect 'modified', so that we can control
   // closing the portal by setting or resetting that option.
   return (!bookDontWrite(book) || bt_prompt(book)) && book->wasModified;
}

int
doWasCurBookChanged(void) {
   return doWasBookChanged(curBook);
}

//For undotree(): Append the list of undo blocks at "first_uhp" to "list". Recursive.
private void
evalTree(Book* book, UndoHeader* first_uhp, List* list) {
   UndoHeader  *uhp = first_uhp;
   Bag   *dict;

   while (uhp) {
      dict = allocBag();
      if (!dict)
         return;
      bagAddNumber(dict, S"seq", uhp->uh_seq);
      bagAddNumber(dict, S"time", (long)uhp->uh_time);
      if (uhp == book->undo.newHead)
         bagAddNumber(dict, S"newhead", 1);
      if (uhp == book->undo.currHead)
         bagAddNumber(dict, S"curhead", 1);
      if (uhp->uh_save_nr > 0)
         bagAddNumber(dict, S"save", uhp->uh_save_nr);

      if (uhp->altNext.ptr != NULL) {
         List* alt_list = list_alloc();
         // Recursive call to add alternate undo tree.
         evalTree(book, uhp->altNext.ptr, alt_list);
         bagAddList(dict, S"alt", alt_list);
      }

      listAppendBag(list, dict);
      uhp = uhp->prev.ptr;
   } 
}

//"undofile(name)" function
void
f_undofile(Var* argvars, Var* returnVar) {
   returnVar->tag = VAR_STRING;
   CS fname = tv_get_string(&argvars[0]);

   if (*fname == ZERO) {
      // If there is no file name there will be no undo file.
      returnVar->string = NULL;
   } else {
      CS ffname = fiExpandAndCopy(fname, true);

      if (ffname)
         returnVar->string = fiBuildSwapOrUndoFname(ffname, false);
      eeglFree(ffname);
   }
}

//Reset undofile option and delete the undofile
void
u_undofile_reset_and_delete(Book* book) {
   if (!book->o.undoFile)
      return;

   CS file_name = fiBuildSwapOrUndoFname(book->fullFileName, true);
   if (file_name) {
      mch_remove(file_name);
      eeglFree(file_name);
   }

   optChangeAndReportError(
      S"undofile", (OptionValue){.tag = OPTION_BOOLE, .boole = 0L}, SET_LOCAL
   );
}

//"undotree(expr)" function
void
f_undotree(Var* argvars, Var* returnVar) {
   allocReturnDict(returnVar);

   Var* tv = &argvars[0];
   Book* book = tv->tag == VAR_UNKNOWN ? curBook : evGetBookArg(tv);
   if (!book)
      return;

   Bag *bag = returnVar->bag;

   bagAddNumber(bag, S"synced", (long)book->undo.synced);
   bagAddNumber(bag, S"seq_last", book->undo.seqLast);
   bagAddNumber(bag, S"save_last", book->undo.saveNrLast);
   bagAddNumber(bag, S"seq_cur", book->undo.seqCurr);
   bagAddNumber(bag, S"time_cur", (long)book->undo.timeCurr);
   bagAddNumber(bag, S"save_cur", book->undo.saveNrCurr);

   List *list = list_alloc();
   evalTree(book, book->undo.oldHead, list);
   bagAddList(bag, S"entries", list);
}

//}}}
