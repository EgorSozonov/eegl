//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## mark.c: functions for setting marks (`ma`) and jumping to them (`'a`)

#include "eegl.h"

//{{{marks

// This file contains routines to maintain and manipulate marks.

//If a named file mark's lnum is non-zero, it is valid.
//If a named file mark's fnum is non-zero, it is for an existing book,
//otherwise it is from .eeglinfo and namedfm[n].fname is the file name.
//There are marks 'A - 'Z (set by user) and '0 to '9 (set when writing eeglinfo).
private FileMarkExt namedfm[NMARKS + EXTRA_MARKS];      // marks with file nr

private void fname2fnum(FileMarkExt *fm);
private void fmarks_check_one(FileMarkExt *fm, Byte *name, Book *book);
private Byte *mark_line(Pos *mp, int lead_len);
private void show_one_mark(int, Byte *, Pos *, Byte *, int current);
private void mark_adjust_internal(
   LineNr line1, LineNr line2, long amount, long amount_after, int adjust_folds
);

//Set named mark "c" at current cursor position. Return OK on success, FAIL if bad name given.
int
setmark(int c) {
   return setmark_pos(c, &curPor->cursor, curBook->fiNum);
}

//Set named mark "c" to position "pos". When "c" is upper case use file "fnum".
//Return OK on success, FAIL if bad name given.
int
setmark_pos(int c, Pos *pos, int fnum) {
   int      i;

   // Check for a special key (may cause islower() to crash).
   if (c < 0)
      return FAIL;

   if (c == '\'' || c == '`') {
      if (pos == &curPor->cursor) {
         setpcmark();
         // keep it even when the cursor doesn't move
         curPor->prevPrevContextMark = curPor->prevContextMark;
      } else
          curPor->prevContextMark = *pos;
      return OK;
   }

   Book* book = bookFindFileByBookNr(fnum);
   if (!book)
      return FAIL;

   if (c == '"') {
      book->lastCursor = *pos;
      return OK;
   }

   // Allow setting '[ and '] for an autocommand that simulates reading a file.
   if (c == '[') {
      book->opStart = *pos;
      return OK;
   }
   if (c == ']') {
      book->opEnd = *pos;
      return OK;
   }

   if (c == '<' || c == '>') {
      if (c == '<')
         book->visual.vi_start = *pos;
      else
         book->visual.vi_end = *pos;
      if (book->visual.vi_mode == ZERO)
         // Visual_mode has not yet been set, use a sane default.
         book->visual.vi_mode = 'v';
      return OK;
   }

   if (ASCII_ISLOWER(c)) {
      i = c - 'a';
      book->namedMarks[i] = *pos;
      return OK;
   }
   if (ASCII_ISUPPER(c) || EE_ISDIGIT(c)) {
      if (EE_ISDIGIT(c))
         i = c - '0' + NMARKS;
      else
         i = c - 'A';
      namedfm[i].fmark.mark = *pos;
      namedfm[i].fmark.fnum = fnum;
      EE_CLEAR(namedfm[i].fname);
      namedfm[i].time_set = eeTime();
      return OK;
   }
   return FAIL;
}

//Delete every entry referring to file 'fnum' from both the jumplist and the tag stack.
void
mark_forget_file(Portal *wp, int fnum) {

   for (int i = wp->jumpListLen - 1; i >= 0; --i) {
      if (wp->jumpList[i].fmark.fnum == fnum) {
          eeglFree(wp->jumpList[i].fname);
          if (wp->jumpListInd > i)
         --wp->jumpListInd;
          --wp->jumpListLen;
          mch_memmove(&wp->jumpList[i], &wp->jumpList[i + 1],
            (wp->jumpListLen - i) * sizeof(wp->jumpList[i]));
      }
   } 

   for (Unt i = wp->tagStackLen - 1; i < wp->tagStackLen; --i) {
      if (wp->tagStack[i].fmark.fnum == fnum) {
         tagstack_clear_entry(&wp->tagStack[i]);
         if (wp->tagStackInd > i)
            --wp->tagStackInd;
         --wp->tagStackLen;
         mch_memmove(&wp->tagStack[i], &wp->tagStack[i + 1],
            (wp->tagStackLen - i) * sizeof(wp->tagStack[i]));
      }
   } 
}

// Set the previous context mark to the current position and add it to the jump list.
void
setpcmark(void) {
   int      i;
   FileMarkExt   *fm;

   // for :global the mark is set only once
   if (global_busy || listcmd_busy || (commModifierG.cmod_flags & CMOD_KEEPJUMPS))
      return;

   curPor->prevPrevContextMark = curPor->prevContextMark;
   curPor->prevContextMark = curPor->cursor;

   //If we're somewhere in the middle of the jumplist, discard everything after the current index.
   if (curPor->jumpListInd < curPor->jumpListLen - 1)
       //Discard the rest of the jumplist by cutting the length down to
       //contain nothing beyond the current index.
       curPor->jumpListLen = curPor->jumpListInd + 1;

   // If jumplist is full: remove oldest entry
   if (++curPor->jumpListLen > JUMPLISTSIZE) {
      curPor->jumpListLen = JUMPLISTSIZE;
      eeglFree(curPor->jumpList[0].fname);
      for (i = 1; i < JUMPLISTSIZE; ++i)
         curPor->jumpList[i - 1] = curPor->jumpList[i];
   }
   curPor->jumpListInd = curPor->jumpListLen;
   fm = &curPor->jumpList[curPor->jumpListLen - 1];

   fm->fmark.mark = curPor->prevContextMark;
   fm->fmark.fnum = curBook->fiNum;
   fm->fname = NULL;
   fm->time_set = eeTime();
}

//To change context, call setpcmark(), then move the current position to
//where ever, then call checkpcmark().  This ensures that the previous
//context will only be changed if the cursor moved to a different line.
//If pcmark was deleted (with "dG") the previous mark is restored.
void
checkpcmark(void) {
   if (curPor->prevPrevContextMark.lnum != 0
          && (EQUAL_POS(curPor->prevContextMark, curPor->cursor)
         || curPor->prevContextMark.lnum == 0))
      curPor->prevContextMark = curPor->prevPrevContextMark;
   curPor->prevPrevContextMark.lnum = 0;      // it has been checked
}

// move "count" positions in the jump list (count may be negative)
Pos *
movemark(int count) {
   Pos   *pos;
   FileMarkExt   *jmp;

   cleanup_jumplist(curPor, TRUE);

   if (curPor->jumpListLen == 0)       // nothing to jump to
      return (Pos *)NULL;

   for (;;) {
      if (curPor->jumpListInd + count < 0
            || curPor->jumpListInd + count >= curPor->jumpListLen)
         return (Pos *)NULL;

      //if first CTRL-O or CTRL-I command after a jump, add cursor position
      //to list.  Careful: If there are duplicates (CTRL-O immediately after
      //starting Eegl on a file), another entry may have been removed.
      if (curPor->jumpListInd == curPor->jumpListLen) {
         setpcmark();
         --curPor->jumpListInd;   // skip the new entry
         if (curPor->jumpListInd + count < 0)
            return (Pos *)NULL;
      }

      curPor->jumpListInd += count;

      jmp = curPor->jumpList + curPor->jumpListInd;
      if (jmp->fmark.fnum == 0)
         fname2fnum(jmp);
      if (jmp->fmark.fnum != curBook->fiNum) {
         // Make a copy, an autocommand may make "jmp" invalid.
         FileMark fmark = jmp->fmark;

         // jump to the file with the mark
         if (bookFindFileByBookNr(fmark.fnum) == NULL) {                    // Skip this one ..
            count += count < 0 ? -1 : 1;
            continue;
         }
         if (buflist_getfile(fmark.fnum, fmark.mark.lnum, 0, FALSE) == FAIL)
            return (Pos *)NULL;
         // Set lnum again, autocommands my have changed it
         curPor->cursor = fmark.mark;
         pos = (Pos *)-1;
      } else
         pos = &(jmp->fmark.mark);
      return pos;
   }
}

// Move "count" positions in the changelist (count may be negative).
Pos *
movechangelist(int count) {

   if (curBook->changeListLen == 0)       // nothing to jump to
      return (Pos *)NULL;

   int n = curPor->changeListInd;
   if (n + count < 0) {
      if (n == 0)
          return (Pos *)NULL;
      n = 0;
   } ei (n + count >= (int)curBook->changeListLen) {
      if (n == (int)curBook->changeListLen - 1)
          return (Pos *)NULL;
      n = curBook->changeListLen - 1;
   } else
      n += count;
   curPor->changeListInd = n;
   return curBook->changeList + n;
}

//Find mark "c" in book pointed to by "book".
//If "changefile" is TRUE it's allowed to edit another file for '0, 'A, etc.
//If "fnum" is not NULL store the fnum there for '0, 'A etc., don't edit another file.
//Return:
//- pointer to Pos if found.  lnum is 0 when mark not set, -1 when mark is
//  in another file which can't be gotten. (caller needs to check lnum!)
//- NULL if there is no mark called 'c'.
//- -1 if mark is in other file and jumped there (only if changefile is TRUE)
Pos *
markGetBook(Book* book, int c, int changefile) {
   return markGetBookFnum(book, c, changefile, NULL);
}

Pos *
getmark(int c, int changefile) {
   return markGetBookFnum(curBook, c, changefile, NULL);
}

Pos *
markGetBookFnum(
   Book* book,
   int c,
   int changefile,
   int* fnum
) {
   Pos      *posp;
   Pos      *startp, *endp;
   static Pos   pos_copy;

   posp = NULL;

   // Check for special key, can't be a mark name and might cause islower() to crash.
   if (c < 0)
      return posp;
   if (c > '~')         // check for islower()/isupper()
      ;
   ei (c == '\'' || c == '`') {  // previous context mark
      pos_copy = curPor->prevContextMark;   // need to make a copy because
      posp = &pos_copy;      //   prevContextMark may be changed soon
   } ei (c == '"')         // to pos when leaving buffer
      posp = &(book->lastCursor);
   ei (c == '^')         // to where Insert mode stopped
      posp = &(book->lastInsert);
   ei (c == '.')         // to where last change was made
      posp = &(book->lastChange);
   ei (c == '[')         // to start of previous operator
      posp = &(book->opStart);
   ei (c == ']')         // to end of previous operator
      posp = &(book->opEnd);
   ei (c == '{' || c == '}') {  // to previous/next paragraph
      Pos   pos;
      Operator   oa;
      int   slcb = listcmd_busy;

      pos = curPor->cursor;
      listcmd_busy = TRUE;       // avoid that '' is changed
      if (findpar(&oa.inclusive,
                   c == '}' ? FORWARD : BACKWARD, 1L, ZERO, FALSE))
      {
          pos_copy = curPor->cursor;
          posp = &pos_copy;
      }
      curPor->cursor = pos;
      listcmd_busy = slcb;
   } ei (c == '(' || c == ')') {  // to previous/next sentence
      int   slcb = listcmd_busy;

      Pos pos = curPor->cursor;
      listcmd_busy = TRUE;       // avoid that '' is changed
      if (findsent(c == ')' ? FORWARD : BACKWARD, 1L)) {
         pos_copy = curPor->cursor;
         posp = &pos_copy;
      }
      curPor->cursor = pos;
      listcmd_busy = slcb;
   } ei (c == '<' || c == '>') {  // start/end of visual area
      startp = &book->visual.vi_start;
      endp = &book->visual.vi_end;
      if (((c == '<') == LT_POS(*startp, *endp) || endp->lnum == 0)
                          && startp->lnum != 0)
          posp = startp;
      else
          posp = endp;
      // For Visual line mode, set mark at begin or end of line
      if (book->visual.vi_mode == 'V') {
         pos_copy = *posp;
         posp = &pos_copy;
         if (c == '<')
            pos_copy.col = 0;
         else
            pos_copy.col = MAXCOL;
         pos_copy.coladd = 0;
      }
   } ei (ASCII_ISLOWER(c))  {    // normal named mark
      posp = &(book->namedMarks[c - 'a']);
   } ei (ASCII_ISUPPER(c) || EE_ISDIGIT(c)) {  // named file mark
      if (EE_ISDIGIT(c))
         c = c - '0' + NMARKS;
      else
         c -= 'A';
      posp = &(namedfm[c].fmark.mark);

      if (namedfm[c].fmark.fnum == 0)
         fname2fnum(&namedfm[c]);

      if (fnum)
         *fnum = namedfm[c].fmark.fnum;
      ei (namedfm[c].fmark.fnum != book->fiNum) {
         // mark is in another file
         posp = &pos_copy;

         if (namedfm[c].fmark.mark.lnum != 0 && changefile && namedfm[c].fmark.fnum) {
            if (buflist_getfile(namedfm[c].fmark.fnum, (LineNr)1, GETF_SETMARK, FALSE) == OK) {
               // Set the lnum now, autocommands could have changed it
               curPor->cursor = namedfm[c].fmark.mark;
               return (Pos *)-1;
            }
            pos_copy.lnum = -1;   // can't get file
         } else
            pos_copy.lnum = 0;   // mark exists, but is not valid in current buffer
      }
   }

   return posp;
}

//Search for the next named mark in the current file.
//
//Return pointer to Pos of the next mark or NULL if no mark is found.
Pos *
getnextmark(
   Pos   *startpos,   // where to start
   int      dir,   // direction for search
   int      begin_line
) {
   int      i;
   Pos   *result = NULL;

   Pos pos = *startpos;

   //When searching backward and leaving the cursor on the first non-blank,
   //position must be in a previous line.
   //When searching forward and leaving the cursor on the first non-blank,
   //position must be in a next line.
   if (dir == BACKWARD && begin_line)
      pos.col = 0;
   ei (dir == FORWARD && begin_line)
      pos.col = MAXCOL;

   for (i = 0; i < NMARKS; i++) {
      if (curBook->namedMarks[i].lnum > 0) {
         if (dir == FORWARD) {
         if ((result == NULL || LT_POS(curBook->namedMarks[i], *result))
               && LT_POS(pos, curBook->namedMarks[i]))
            result = &curBook->namedMarks[i];
         } else {
            if ((result == NULL || LT_POS(*result, curBook->namedMarks[i]))
                  && LT_POS(curBook->namedMarks[i], pos))
               result = &curBook->namedMarks[i];
         }
      }
   }

   return result;
}

//For an xtended filemark: set the fnum from the fname.
//This is used for marks obtained from the .eeglinfo file.  It's postponed
//until the mark is used to avoid a long startup delay.
private void
fname2fnum(FileMarkExt *fm) {
   if (!fm->fname)
      return;

   //First expand "~/" in the file name to the home directory.
   //Don't expand the whole name since it may contain other '~' chars.
   if (fm->fname[0] == '~' && (fm->fname[1] == '/')) {
      Unt len = doExpandEnv(OUT filenameBuilder, S"~/");
      copySubstrToAllocation(NameBuff + len, (Text){fm->fname + 2, MAXPATHL - len - 1});
   } else
      copySubstrToAllocation(NameBuff, (Text){fm->fname, MAXPATHL - 1});

   // Try to shorten the file name.
   mch_dirname(IObuff, IOSIZE);
   CS p = shorten_fname(NameBuff, IObuff);

   // bookNew() will call fmarks_check_names()
   (void)bookNew(NameBuff, p, (LineNr)1, 0);
}

//Check all file marks for a name that matches the file name in book. May replace the name with an 
//fnum. Used for marks that come from the .eeglinfo file.
void
fmarks_check_names(Book *book) {
   if (book->fullFileName == NULL)
      return;

   CS name = home_replace_save(book, book->fullFileName);
   if (!name)
      return;

   for (int i = 0; i < NMARKS + EXTRA_MARKS; ++i)
      fmarks_check_one(&namedfm[i], name, book);

   Portal   *wp;
   FOR_ALL_PORTALS(wp) {
      for (int i = 0; i < wp->jumpListLen; ++i)
         fmarks_check_one(&wp->jumpList[i], name, book);
   }

   eeglFree(name);
}

private void
fmarks_check_one(FileMarkExt *fm, Byte *name, Book *book) {
   if (fm->fmark.fnum == 0 && fm->fname != NULL && fnamecmp(name, fm->fname) == 0) {
      fm->fmark.fnum = book->fiNum;
      EE_CLEAR(fm->fname);
   }
}

//Check a if a position from a mark is valid.
//Give and error message and return FAIL if not.
int
check_mark(Pos *pos) {
   if (!pos) {
      emsg(_(e_unknown_mark));
      return FAIL;
   }
   if (pos->lnum <= 0) {
      // lnum is negative if mark is in another file can can't get that
      // file, error message already give then.
      if (pos->lnum == 0)
         emsg(_(e_mark_not_set));
      return FAIL;
   }
   if (pos->lnum > curBook->mem.lineCount) {
      emsg(_(e_mark_has_invalid_line_number));
      return FAIL;
   }
   return OK;
}

//clrallmarks() - clear all marks in the book 'book'
//
//Used mainly when trashing the entire book during ":e" type commands
void
clrallmarks(Book* book) {
   static int i = -1;

   if (i == -1) {   // first call ever: initialize
      for (i = 0; i < NMARKS + 1; i++) {
         namedfm[i].fmark.mark.lnum = 0;
         namedfm[i].fname = NULL;
         namedfm[i].time_set = 0;
      }
   } 

   for (i = 0; i < NMARKS; i++)
      book->namedMarks[i].lnum = 0;
   book->opStart.lnum = 0;      // start/end op mark cleared
   book->opEnd.lnum = 0;
   book->lastCursor.lnum = 1;   // '" mark cleared
   book->lastCursor.col = 0;
   book->lastCursor.coladd = 0;
   book->lastInsert.lnum = 0;   // '^ mark cleared
   book->lastChange.lnum = 0;   // '. mark cleared
   book->changeListLen = 0;
}

//Get name of file from a filemark.
//When it's in the current buffer, return the text at the mark. Returns an allocated string.
CS
fm_getname(FileMark* fmark, int lead_len) {
   if (fmark->fnum == curBook->fiNum)          // current buffer
      return mark_line(&(fmark->mark), lead_len);
   return bookGetNameByBookNr(fmark->fnum, FALSE, TRUE);
}

// Return the line at mark "mp". Truncate to fit in portal. The returned string has been allocated
private CS
mark_line(Pos* mp, int lead_len) {
   if (mp->lnum == 0 || mp->lnum > curBook->mem.lineCount)
      return copyStr(S"-invalid-");
   // Allow for up to 5 bytes per character.
   CS s = copySubstr(skipwhite(ml_get(mp->lnum)), visibleColsG * 5);
   
   // Truncate the line to fit it in the portal.
   int len = 0;
   CS p;
   for (p = s; *p != ZERO; MB_PTR_ADV(p)) {
      len += ptr2cells(p);
      if (len >= visibleColsG - lead_len)
         break;
   }
   *p = ZERO;
   return s;
}

// print the marks
void
c_marks(Invocation *invo) {
   CS arg = invo->arg;
   int i;
   CS name;
   Pos   *posp, *startp, *endp;

   if (arg && *arg == ZERO)
      arg = NULL;

   show_one_mark('\'', arg, &curPor->prevContextMark, NULL, TRUE);
   for (i = 0; i < NMARKS; ++i)
      show_one_mark(i + 'a', arg, &curBook->namedMarks[i], NULL, TRUE);
   for (i = 0; i < NMARKS + EXTRA_MARKS; ++i) {
      if (namedfm[i].fmark.fnum != 0)
         name = fm_getname(&namedfm[i].fmark, 15);
      else
         name = namedfm[i].fname;
      if (name != NULL) {
         show_one_mark(i >= NMARKS ? i - NMARKS + '0' : i + 'A',
             arg, &namedfm[i].fmark.mark, name,
             namedfm[i].fmark.fnum == curBook->fiNum
         );
         if (namedfm[i].fmark.fnum != 0)
            eeglFree(name);
      }
   }
   show_one_mark('"', arg, &curBook->lastCursor, NULL, TRUE);
   show_one_mark('[', arg, &curBook->opStart, NULL, TRUE);
   show_one_mark(']', arg, &curBook->opEnd, NULL, TRUE);
   show_one_mark('^', arg, &curBook->lastInsert, NULL, TRUE);
   show_one_mark('.', arg, &curBook->lastChange, NULL, TRUE);

   // Show the marks as where they will jump to.
   startp = &curBook->visual.vi_start;
   endp = &curBook->visual.vi_end;
   if ((LT_POS(*startp, *endp) || endp->lnum == 0) && startp->lnum != 0)
      posp = startp;
   else
      posp = endp;
   show_one_mark('<', arg, posp, NULL, TRUE);
   show_one_mark('>', arg, posp == startp ? endp : startp, NULL, TRUE);

   show_one_mark(-1, arg, NULL, NULL, FALSE);
}

private void
show_one_mark(
   int      c,
   CS arg,
   Pos* p,
   CS name_arg,
   int      current   // in current file
){
   static int   did_title = FALSE;
   int      mustfree = FALSE;
   CS name = name_arg;

   if (c == -1) {            // finish up
      if (did_title)
         did_title = FALSE;
      else {
         if (arg == NULL)
            msg(_("No marks set"));
         else
            showErrFmtMsg(_(e_no_marks_matching_str), arg);
      }
   }
   // don't output anything if 'q' typed at --more-- prompt
   ei (!gotInterruptG && (!arg || firstOccurrence(arg, c) != NULL) && p->lnum != 0) {
      if (!name && current) {
         name = mark_line(p, 15);
         if (!name) {
            emsg(_(e_out_of_memory));
            return;
         }
         mustfree = TRUE;
      }
      if (!message_filtered(name)) {
         if (!did_title) {
            // Highlight title
            msg_puts_title(_("\nmark line  col file/text"));
            did_title = TRUE;
         }
         msg_putchar('\n');
         if (!gotInterruptG) {
            sprintf((char *)IObuff, " %c %6ld %4d ", c, p->lnum, p->col);
            msg_outtrans(IObuff);
            if (name) {
               msgOuttransDeco(name, current ? getDecoFlags(HLF_D) : 0);
            }
         }
         out_flush();          // show one line at a time
      }
      if (mustfree)
         eeglFree(name);
   }
}

// ":delmarks[!] [marks]"
void
c_delmarks(Invocation *invo) {
   Byte   *p;
   int      from, to;
   int      i;
   int      lower;
   int      digit;
   int      n;

   if (*invo->arg == ZERO && invo->forceit)
      // clear all marks
      clrallmarks(curBook);
   ei (invo->forceit)
      emsg(_(e_invalid_argument));
   ei (*invo->arg == ZERO)
      emsg(_(e_argument_required));
   else {
      // clear specified marks only
      for (p = invo->arg; *p != ZERO; ++p) {
         lower = ASCII_ISLOWER(*p);
         digit = EE_ISDIGIT(*p);
         if (lower || digit || ASCII_ISUPPER(*p)) {
            if (p[1] == '-') {
               // clear range of marks
               from = *p;
               to = p[2];
               if (!(lower ? ASCII_ISLOWER(p[2])
                  : (digit ? EE_ISDIGIT(p[2]) : ASCII_ISUPPER(p[2])))
                   || to < from
               ){
                  showErrFmtMsg(_(e_invalid_argument_str), p);
                  return;
               }
               p += 2;
            } else
               // clear one lower case mark
               from = to = *p;

            for (i = from; i <= to; ++i) {
               if (lower)
                  curBook->namedMarks[i - 'a'].lnum = 0;
               else {
                  if (digit)
                     n = i - '0' + NMARKS;
                  else
                     n = i - 'A';
                  namedfm[n].fmark.mark.lnum = 0;
                  namedfm[n].fmark.fnum = 0;
                  EE_CLEAR(namedfm[n].fname);
                  namedfm[n].time_set = digit ? 0 : eeTime();
               }
            }
         } else {
            switch (*p) {
            case '"': curBook->lastCursor.lnum = 0; break;
            case '^': curBook->lastInsert.lnum = 0; break;
            case '.': curBook->lastChange.lnum = 0; break;
            case '[': curBook->opStart.lnum    = 0; break;
            case ']': curBook->opEnd.lnum      = 0; break;
            case '<': curBook->visual.vi_start.lnum = 0; break;
            case '>': curBook->visual.vi_end.lnum   = 0; break;
            case ' ': break;
            default:  showErrFmtMsg(_(e_invalid_argument_str), p);
                 return;
            }
         } 
      }
   }
}

// print the jumplist
void
c_jumps(Invocation *invo UNUSED) {
   CS name;

   cleanup_jumplist(curPor, TRUE);

   // Highlight title
   msg_puts_title(_("\n jump line  col file/text"));
   for (int i = 0; i < curPor->jumpListLen && !gotInterruptG; ++i) {
      if (curPor->jumpList[i].fmark.mark.lnum != 0) {
         name = fm_getname(&curPor->jumpList[i].fmark, 16);

         // Make sure to output the current indicator, even when on a wiped
         // out book.  ":filter" may still skip it.
         if (name == NULL && i == curPor->jumpListInd)
            name = copyStr((CS)"-invalid-");
         // apply :filter /pat/ or file name not available
         if (name == NULL || message_filtered(name)) {
            eeglFree(name);
            continue;
         }

         msg_putchar('\n');
         if (gotInterruptG) {
            eeglFree(name);
            break;
         }
         sprintf(
            (char *)IObuff, "%c %2d %5ld %4d ",
            i == curPor->jumpListInd ? '>' : ' ',
            i > curPor->jumpListInd ? i - curPor->jumpListInd
                       : curPor->jumpListInd - i,
            curPor->jumpList[i].fmark.mark.lnum,
            curPor->jumpList[i].fmark.mark.col
         );
         msg_outtrans(IObuff);
         msgOuttransDeco(
            name, curPor->jumpList[i].fmark.fnum == curBook->fiNum ? getDecoFlags(HLF_D) : 0
         );
         eeglFree(name);
         ui_breakcheck();
      }
      out_flush();
   }
   if (curPor->jumpListInd == curPor->jumpListLen)
      msg_puts(S"\n>");
}

void
c_clearjumps(Invocation *invo UNUSED) {
   free_jumplist(curPor);
   curPor->jumpListLen = 0;
   curPor->jumpListInd = 0;
}

// print the changelist
void
c_changes(Invocation *invo UNUSED) {
   CS name;

   // Highlight title
   msg_puts_title(_("\nchange line  col text"));

   for (Unt i = 0; i < curBook->changeListLen && !gotInterruptG; ++i) {
      if (curBook->changeList[i].lnum != 0) {
         msg_putchar('\n');
         if (gotInterruptG)
            break;
         sprintf((char *)IObuff, "%c %3d %5ld %4d ",
             i == (Unt)curPor->changeListInd ? '>' : ' ',
             i > (Unt)curPor->changeListInd ? i - curPor->changeListInd
                     : curPor->changeListInd - i,
             (long)curBook->changeList[i].lnum,
             curBook->changeList[i].col);
         msg_outtrans(IObuff);
         name = mark_line(&curBook->changeList[i], 17);
         if (!name)
            break;
         msgOuttransDeco(name, getDecoFlags(HLF_D));
         eeglFree(name);
         ui_breakcheck();
      }
      out_flush();
   }
   if (curPor->changeListInd == (int)curBook->changeListLen)
      msg_puts(S"\n>");
}

#define one_adjust(add) \
    { \
   lp = add; \
   if (*lp >= line1 && *lp <= line2) \
   { \
       if (amount == MAXLNUM) \
      *lp = 0; \
       else \
      *lp += amount; \
   } \
   ei (amount_after && *lp > line2) \
       *lp += amount_after; \
    }

// don't delete the line, just put at first deleted line
#define one_adjust_nodel(add) \
    { \
   lp = add; \
   if (*lp >= line1 && *lp <= line2) \
   { \
       if (amount == MAXLNUM) \
      *lp = line1; \
       else \
      *lp += amount; \
   } \
   ei (amount_after && *lp > line2) \
       *lp += amount_after; \
    }

//Adjust marks between "line1" and "line2" (inclusive) to move "amount" lines.
//Must be called before changed_*(), appended_lines() or deleted_lines().
//May be called before or after changing the text.
//When deleting lines "line1" to "line2", use an "amount" of MAXLNUM: The
//marks within this range are made invalid.
//If "amount_after" is non-zero adjust marks after "line2".
//Example: Delete lines 34 and 35: mark_adjust(34, 35, MAXLNUM, -2);
//Example: Insert two lines below 55: mark_adjust(56, MAXLNUM, 2, 0);
//              or: mark_adjust(56, 55, MAXLNUM, 2);
void
mark_adjust(
   LineNr   line1,
   LineNr   line2,
   long   amount,
   long   amount_after
) {
   mark_adjust_internal(line1, line2, amount, amount_after, TRUE);
}

void
mark_adjust_nofold(
   LineNr   line1,
   LineNr   line2,
   long   amount,
   long   amount_after) {
   mark_adjust_internal(line1, line2, amount, amount_after, FALSE);
}

private void
mark_adjust_internal(
   LineNr   line1,
   LineNr   line2,
   long   amount,
   long   amount_after,
   int      adjust_folds UNUSED)
{
   int      i;
   int      fnum = curBook->fiNum;
   LineNr   *lp;
   static Pos initpos = {1, 0, 0};

   if (line2 < line1 && amount_after == 0L)       // nothing to do
      return;

   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
      // named marks, lower case and upper case
      for (i = 0; i < NMARKS; i++) {
         one_adjust(&(curBook->namedMarks[i].lnum));
         if (namedfm[i].fmark.fnum == fnum)
            one_adjust_nodel(&(namedfm[i].fmark.mark.lnum));
      }
      for (i = NMARKS; i < NMARKS + EXTRA_MARKS; i++) {
         if (namedfm[i].fmark.fnum == fnum)
            one_adjust_nodel(&(namedfm[i].fmark.mark.lnum));
      }

      // last Insert position
      one_adjust(&(curBook->lastInsert.lnum));

      // last change position
      one_adjust(&(curBook->lastChange.lnum));

      // last cursor position, if it was set
      if (!EQUAL_POS(curBook->lastCursor, initpos))
         one_adjust(&(curBook->lastCursor.lnum));

      // list of change positions
      for (Unt i = 0; i < curBook->changeListLen; ++i)
          one_adjust_nodel(&(curBook->changeList[i].lnum));

      // Visual area
      one_adjust_nodel(&(curBook->visual.vi_start.lnum));
      one_adjust_nodel(&(curBook->visual.vi_end.lnum));

      // quickfix marks
      llAdjustEntries(line1, line2, amount, amount_after);
      sign_mark_adjust(line1, line2, amount, amount_after);
   }

   // previous context mark
   one_adjust(&(curPor->prevContextMark.lnum));

   // previous pcmark
   one_adjust(&(curPor->prevPrevContextMark.lnum));

   // saved cursor for formatting
   if (saved_cursor.lnum != 0)
      one_adjust_nodel(&(saved_cursor.lnum));

   // Adjust items in all portals into the current buffer
   
   Portal* port;
   Tab* tab;
   FOR_ALL_TAB_PORTALS(tab, port) {
      if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0)
         // Marks in the jumplist.  When deleting lines, this may create
         // duplicate marks in the jumplist, they will be removed later.
         for (i = 0; i < port->jumpListLen; ++i) {
            if (port->jumpList[i].fmark.fnum == fnum)
                one_adjust_nodel(&(port->jumpList[i].fmark.mark.lnum));
         } 

      if (port->book == curBook) {
         if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
            // marks in the tag stack
            for (Unt i = 0; i < port->tagStackLen; i++)
               if (port->tagStack[i].fmark.fnum == fnum)
                  one_adjust_nodel(&(port->tagStack[i].fmark.mark.lnum));
         } 

         // the displayed Visual area
         if (port->prevVisualEnd != 0) {
            one_adjust_nodel(&(port->prevVisualEnd));
            one_adjust_nodel(&(port->oldVisualLnum));
         }

         // topline and cursor position for portals into the same buffer other than curPor
         if (port != curPor) {
            if (port->topLine >= line1 && port->topLine <= line2) {
               if (amount == MAXLNUM) {    // topline is deleted
                  if (line1 <= 1)
                      port->topLine = 1;
                  else
                      port->topLine = line1 - 1;
               } ei (port->topLine > line1)
                  // keep topline on the same line, unless inserting just
                  // above it (we probably want to see that line then)
                  port->topLine += amount;
               port->topFill = 0;
            } ei (amount_after && port->topLine > line2) {
                port->topLine += amount_after;
                port->topFill = 0;
            }
            if (port->cursor.lnum >= line1 && port->cursor.lnum <= line2) {
               if (amount == MAXLNUM) { // line with cursor is deleted
                  if (line1 <= 1)
                      port->cursor.lnum = 1;
                  else
                      port->cursor.lnum = line1 - 1;
                  port->cursor.col = 0;
               } else      // keep cursor on the same line
                  port->cursor.lnum += amount;
            } ei (amount_after && port->cursor.lnum > line2)
                port->cursor.lnum += amount_after;
         }

         // adjust folds
         if (adjust_folds)
            foldMarkAdjust(port, line1, line2, amount, amount_after);
      }
   }

   // adjust diffs
   diff_mark_adjust(line1, line2, amount, amount_after);
}

// This code is used often, needs to be fast.
#define col_adjust(pp) \
    { \
   posp = pp; \
   if (posp->lnum == lnum && posp->col >= mincol) \
   { \
       posp->lnum += lnum_amount; \
       if (col_amount < 0 && posp->col <= (ColNr)-col_amount) \
      posp->col = 0; \
       ei (posp->col < spaces_removed) \
      posp->col = col_amount + spaces_removed; \
       else \
      posp->col += col_amount; \
   } \
    }

//Adjust marks in line "lnum" at column "mincol" and further: add
//"lnum_amount" to the line number and add "col_amount" to the column position.
//"spaces_removed" is the number of spaces that were removed, matters when the cursor is inside 
//them
void
mark_col_adjust(
   LineNr   lnum,
   ColNr   mincol,
   long   lnum_amount,
   long   col_amount,
   int      spaces_removed)
{
   int      i;
   int      fnum = curBook->fiNum;
   Pos   *posp;

   if ((col_amount == 0L && lnum_amount == 0L) || (commModifierG.cmod_flags & CMOD_LOCKMARKS))
      return; // nothing to do

   // named marks, lower case and upper case
   for (i = 0; i < NMARKS; i++) {
      col_adjust(&(curBook->namedMarks[i]));
      if (namedfm[i].fmark.fnum == fnum)
         col_adjust(&(namedfm[i].fmark.mark));
   }
   for (i = NMARKS; i < NMARKS + EXTRA_MARKS; i++) {
      if (namedfm[i].fmark.fnum == fnum)
         col_adjust(&(namedfm[i].fmark.mark));
   }

   // last Insert position
   col_adjust(&(curBook->lastInsert));

   // last change position
   col_adjust(&(curBook->lastChange));

   // list of change positions
   for (Unt i = 0; i < curBook->changeListLen; ++i)
     col_adjust(&(curBook->changeList[i]));

   // Visual area
   col_adjust(&(curBook->visual.vi_start));
   col_adjust(&(curBook->visual.vi_end));

   // previous context mark
   col_adjust(&(curPor->prevContextMark));

   // previous pcmark
   col_adjust(&(curPor->prevPrevContextMark));

   // saved cursor for formatting
   col_adjust(&saved_cursor);

   // Adjust items in all portals into the current buffer.
   Portal* port;
   FOR_ALL_PORTALS(port) {
      // marks in the jumplist
      for (i = 0; i < port->jumpListLen; ++i)
         if (port->jumpList[i].fmark.fnum == fnum)
            col_adjust(&(port->jumpList[i].fmark.mark));

      if (port->book == curBook) {
         // marks in the tag stack
         for (Unt i = 0; i < port->tagStackLen; i++) {
            if (port->tagStack[i].fmark.fnum == fnum)
                col_adjust(&(port->tagStack[i].fmark.mark));
         } 

         // cursor position for other potals into the same buffer
         if (port != curPor)
            col_adjust(&port->cursor);
      }
   }
}

//When deleting lines, this may create duplicate marks in the jumplist. They will be removed here 
//for the specified ortal. When "loadfiles" is TRUE first ensure entries have the "fnum" field set
//(this may be a bit slow).
void
cleanup_jumplist(Portal *wp, int loadfiles) {

   if (loadfiles) {
      // If specified, load all the files from the jump list. This is
      // needed to properly clean up duplicate entries, but will take some time.
      for (int i = 0; i < wp->jumpListLen; ++i) {
         if ((wp->jumpList[i].fmark.fnum == 0) && (wp->jumpList[i].fmark.mark.lnum != 0))
            fname2fnum(&wp->jumpList[i]);
      }
   }

   int to = 0;
   for (int from = 0; from < wp->jumpListLen; ++from) {
      if (wp->jumpListInd == from)
          wp->jumpListInd = to;
      int i = from + 1;
      for (; i < wp->jumpListLen; ++i) {
         if (wp->jumpList[i].fmark.fnum == wp->jumpList[from].fmark.fnum
                && wp->jumpList[from].fmark.fnum != 0
                && wp->jumpList[i].fmark.mark.lnum == wp->jumpList[from].fmark.mark.lnum)
            break;
      } 
      wp->jumpList[to++] = wp->jumpList[from];
   }
   if (wp->jumpListInd == wp->jumpListLen)
      wp->jumpListInd = to;
   wp->jumpListLen = to;
}

// Copy the jumplist from portal "from" to portal "to".
void
copy_jumplist(Portal *from, Portal *to) {
   for (int i = 0; i < from->jumpListLen; ++i) {
      to->jumpList[i] = from->jumpList[i];
      if (from->jumpList[i].fname != NULL)
          to->jumpList[i].fname = copyStr(from->jumpList[i].fname);
   }
   to->jumpListLen = from->jumpListLen;
   to->jumpListInd = from->jumpListInd;
}

// Free items in the jumplist of portal "wp".
void
free_jumplist(Portal *wp) {
   for (int i = 0; i < wp->jumpListLen; ++i)
      eeglFree(wp->jumpList[i].fname);
}

void
set_last_cursor(Portal *port) {
   if (port->book)
      port->book->lastCursor = port->cursor;
}

#if defined(EXITFREE) || defined(PROTO)
void
free_all_marks(void) {
   int      i;

   for (i = 0; i < NMARKS + EXTRA_MARKS; i++) {
      if (namedfm[i].fmark.mark.lnum != 0)
          eeglFree(namedfm[i].fname);
   } 
}
#endif

// Return a pointer to the named file marks.
FileMarkExt *
get_namedfm(void) {
   return namedfm;
}

// Add information about mark 'mname' to list 'l'
private int
add_mark(List *l, Byte *mname, Pos *pos, int bufnr, Byte *fname) {
   if (pos->lnum <= 0)
      return OK;

   Bag* d = allocBag();
   if (!d)
      return FAIL;

   if (listAppendBag(l, d) == FAIL) {
      bagUnref(d);
      return FAIL;
   }

   List* lpos = list_alloc();

   list_append_number(lpos, bufnr);
   list_append_number(lpos, pos->lnum);
   list_append_number(lpos, pos->col < MAXCOL ? pos->col + 1 : MAXCOL);
   list_append_number(lpos, pos->coladd);

   if (bagAddString(d, S"mark", mname) == FAIL
          || bagAddList(d, S"pos", lpos) == FAIL
          || (fname != NULL && bagAddString(d, S"file", fname) == FAIL))
      return FAIL;

   return OK;
}

// Get information about marks local to a buffer.
private void
get_buf_local_marks(Book *book, List *l) {
   Byte   mname[3] = "' ";
   int      i;

   // Marks 'a' to 'z'
   for (i = 0; i < NMARKS; ++i) {
      mname[1] = 'a' + i;
      add_mark(l, mname, &book->namedMarks[i], book->fiNum, NULL);
   }

    // Mark '' is a portal local mark and not a buffer local mark
    add_mark(l, (CS)"''", &curPor->prevContextMark, curBook->fiNum, NULL);

    add_mark(l, (CS)"'\"", &book->lastCursor, book->fiNum, NULL);
    add_mark(l, (CS)"'[", &book->opStart, book->fiNum, NULL);
    add_mark(l, (CS)"']", &book->opEnd, book->fiNum, NULL);
    add_mark(l, (CS)"'^", &book->lastInsert, book->fiNum, NULL);
    add_mark(l, (CS)"'.", &book->lastChange, book->fiNum, NULL);
    add_mark(l, (CS)"'<", &book->visual.vi_start, book->fiNum, NULL);
    add_mark(l, (CS)"'>", &book->visual.vi_end, book->fiNum, NULL);
}

// Get information about global marks ('A' to 'Z' and '0' to '9')
private void
get_global_marks(List *l) {
   Byte   mname[3] = "' ";
   Byte   *name;

   // Marks 'A' to 'Z' and '0' to '9'
   for (int i = 0; i < NMARKS + EXTRA_MARKS; ++i) {
      if (namedfm[i].fmark.fnum != 0)
         name = bookGetNameByBookNr(namedfm[i].fmark.fnum, TRUE, TRUE);
      else
         name = namedfm[i].fname;
      if (name) {
         mname[1] = i >= NMARKS ? i - NMARKS + '0' : i + 'A';
         add_mark(l, mname, &namedfm[i].fmark.mark, namedfm[i].fmark.fnum, name);
         if (namedfm[i].fmark.fnum != 0)
            eeglFree(name);
      }
   }
}

void
f_getmarklist(Var *argvars, Var *returnVar) {
   allocReturnList(returnVar);

   if (argvars[0].tag == VAR_UNKNOWN) {
      get_global_marks(returnVar->list);
      return;
   }

   Book* book = daGetBook(&argvars[0], FALSE);
   if (!book)
      return;

   get_buf_local_marks(book, returnVar->list);
}

//}}}
//{{{signs (little chars shown in the leftmost column, like changed line markings)

// Iterate through all the signs placed in a book
#define FOR_ALL_SIGNS_IN_BOOK(book, sign) \
    for ((sign) = (book)->signList; (sign) != NULL; (sign) = (sign)->next)


// Struct to hold the sign properties.
typedef struct Sign Sign;

struct Sign {
   Sign *next; // next sign in list
   int typeNr; // type number of sign
   CS name; // name of sign
   CS text; // text used instead of pixmap
   Short lineHiId; // hilite ID for line
   Short textHiId; // hilite ID for text
   Short cursorLineHiId; // hilite ID for text on current line when 'cursorline' is set
   Short lineNumHiId; // hilite ID for line number
   int priority; // default priority of this sign, -1 means SIGN_DEF_PRIO
};

private Sign *first_sign = NULL;
private int next_sign_typenr = 1;

private void sign_list_defined(Sign *sp);
private void sign_undefine(Sign *sp, Sign *sp_prev);

private char *cmds[] = { "define",
# define SIGNCMD_DEFINE 0
                        "undefine",
# define SIGNCMD_UNDEFINE 1
                        "list",
# define SIGNCMD_LIST 2
                        "place",
# define SIGNCMD_PLACE 3
                        "unplace",
# define SIGNCMD_UNPLACE 4
                        "jump",
# define SIGNCMD_JUMP 5
                        NULL
# define SIGNCMD_LAST 6
};

# define FOR_ALL_SIGNS(sp) \
     for ((sp) = first_sign; (sp); (sp) = (sp)->next)

private EeSet signGroups; // sign group (SignGroup) hashtable
private int next_sign_id = 1; // next sign id in the global group

// Initialize data needed for managing signs
void
init_signs(void) {
   hash_init(&signGroups); // sign group hash table
}

//A new sign in group 'groupname' is added. If the group is not present,
//create it. Otherwise reference the group.
private SignGroup *
sign_group_ref(CS groupname) {
   Text groupName = mbText(groupname);
   Hash hash = calcHash(groupName);
   EeSetItem *hi = hash_lookup(&signGroups, groupName, hash);
   SignGroup* group = NULL;

   if (HASHITEM_EMPTY(hi)) {
      // new group
      group = alloc(offsetof(SignGroup, sg_name) + STRLEN(groupname) + 1);

      STRCPY(group->sg_name, groupname);
      group->sg_refcount = 1;
      group->sg_next_sign_id = 1;
      group->isPopupOnly = STRNCMP("PopUp", groupname, 5) == 0;
      hash_add_item(&signGroups, hi, mbText(group->sg_name), hash);
   } else {
      // existing group
      group = HI2SG(hi);
      group->sg_refcount++;
   }

   return group;
}

//A sign in group 'groupname' is removed. If all the signs in this group are
//removed, then remove the group.
private void
sign_group_unref(CS groupname) {
   EeSetItem *hi = hash_find(&signGroups, mbText(groupname));
   if (HASHITEM_EMPTY(hi))
       return;

   SignGroup* group = HI2SG(hi);
   group->sg_refcount--;
   if (group->sg_refcount == 0) {
       // All the signs in this group are removed
       hash_remove(&signGroups, hi, S"sign remove");
       eeglFree(group);
   }
}

//Return TRUE if 'sign' is in 'group'.
//A sign can either be in the global group (sign->group == NULL)
//or in a named group. If 'group' is '*', then the sign is part of the group.
private int
sign_in_group(SignEntry *sign, CS group) {
   return ((group && STRCMP(group, "*") == 0) 
          || (!group && !sign->group) 
          || (group && sign->group && STRCMP(group, sign->group->sg_name) == 0)
   );
}

//Return TRUE if "sign" is to be displayed in window "wp".
//If the group name starts with "PopUp" it only shows in a popup portal.
private int
sign_group_for_window(SignEntry *sign, Portal *wp) {
   int for_popup = sign->group && sign->group->isPopupOnly;
   return PORTAL_IS_POPUP(wp) ? for_popup : !for_popup;
}

// Get the next free sign identifier in the specified group
private int
sign_group_get_next_signid(Book *book, CS groupname) {
   int id = 1;
   SignGroup *group = NULL;
   SignEntry *sign = NULL;

   if (groupname) {
      EeSetItem *hi = hash_find(&signGroups, text(groupname));
      if (HASHITEM_EMPTY(hi))
         return id;
      group = HI2SG(hi);
   }

   // Search for the next usable sign identifier
   Boole found = false;
   while (!found) {
      if (group) {
         id = group->sg_next_sign_id;
         group->sg_next_sign_id++;
      } else {
         id = next_sign_id; // global group
         next_sign_id++;
      } 

      // Check whether this sign is already placed in the buffer
      found = found;
      FOR_ALL_SIGNS_IN_BOOK(book, sign) {
         if (id == sign->id && sign_in_group(sign, groupname)) {
            found = false; // sign identifier is in use
            break;
         }
      }
   }

   return id;
}

// Insert a new sign into the signlist for buffer 'book' between the 'prev' and 'next' signs.
private void
insert_sign(
   Book *book, // buffer to store sign in
   SignEntry* prev, // previous sign entry
   SignEntry* next, // next sign entry
   int id, // sign ID
   CS group, // sign group; NULL for global group
   int prio, // sign priority
   LineNr lnum, // line number which gets the mark
   int typenr
) {// typenr of sign we are adding
   SignEntry *newsign = lalloc_id(sizeof(SignEntry), FALSE, aid_insert_sign);
   if (!newsign)
      return;

   newsign->id = id;
   newsign->lnum = lnum;
   newsign->typeNr = typenr;

   if (group) {
      newsign->group = sign_group_ref(group);
      if (!newsign->group) {
         eeglFree(newsign);
         return;
      }
   } else {
      newsign->group = NULL;
   }

   newsign->priority = prio;
   newsign->next = next;
   newsign->prev = prev;
   if (next)
      next->prev = newsign;

   if (!prev) {
      // When adding first sign need to redraw the windows to create the column for signs.
      if (!book->signList) {
         drawBookLater(book, UPD_NOT_VALID);
         changed_line_abv_curs();
      }

      // first sign in signlist
      book->signList = newsign;
   } else {
      prev->next = newsign;
   }
}

// Insert a new sign sorted by line number and sign priority.
private void
insert_sign_by_lnum_prio(
   Book *book, // buffer to store sign in
   SignEntry *prev, // previous sign entry
   int id, // sign ID
   Byte *group, // sign group; NULL for global group
   int prio, // sign priority
   LineNr lnum, // line number which gets the mark
   int typenr
) {   // typenr of sign we are adding
      // keep signs sorted by lnum and by priority: insert new sign at
      // the proper position in the list for this lnum.
      while (prev && prev->lnum == lnum && prev->priority <= prio)
         prev = prev->prev;

      SignEntry *sign = (!prev) ? book->signList : prev->next;

      insert_sign(book, prev, sign, id, group, prio, lnum, typenr);
}

// Lookup a sign by typenr. Returns NULL if sign is not found.
private Sign *
find_sign_by_typenr(int typenr) {
   Sign *sp = NULL;
   FOR_ALL_SIGNS(sp) {
      if (sp->typeNr == typenr)
         return sp;
   } 
   return NULL;
}

// Get the name of a sign by its typenr.
private Byte *
sign_typenr2name(int typenr) {
    Sign *sp = NULL;
    FOR_ALL_SIGNS(sp) {
       if (sp->typeNr == typenr)
          return sp->name;
    } 
    return (CS)_("[Deleted]");
}

// Return information about a sign in a Bag
private Bag*
sign_get_info(SignEntry *sign) {
   Bag *b = allocBag_id(aid_sign_getinfo);
   if (!b)
      return NULL;

   bagAddNumber(b, S"id", sign->id);
   bagAddString(b, S"group", (!sign->group) ? E : sign->group->sg_name);
   bagAddNumber(b, S"lnum", sign->lnum);
   bagAddString(b, S"name", sign_typenr2name(sign->typeNr));
   bagAddNumber(b, S"priority", sign->priority);

   return b;
}

//Sort the signs placed on the same line as "sign" by priority.  Invoked after
//changing the priority of an already placed sign.  Assumes the signs in the
//buffer are sorted by line number and priority.
private void
sign_sort_by_prio_on_line(Book *book, SignEntry *sign) {
    // If there is only one sign in the buffer or only one sign on the line or
    // the sign is already sorted by priority, then return.
    if ((sign->prev == NULL || sign->prev->lnum != sign->lnum ||
         sign->prev->priority > sign->priority) &&
        (sign->next == NULL || sign->next->lnum != sign->lnum ||
         sign->next->priority < sign->priority))
        return;

    // One or more signs on the same line as 'sign'
    // Find a sign after which 'sign' should be inserted

    // First search backward for a sign with higher priority on the same line
    SignEntry *p = sign;
    while (p->prev && p->prev->lnum == sign->lnum &&
           p->prev->priority <= sign->priority)
       p = p->prev;

    if (p == sign) {
        // Sign not found. Search forward for a sign with priority just before
        // 'sign'.
        p = sign->next;
        while (p->next != NULL && p->next->lnum == sign->lnum &&
               p->next->priority > sign->priority)
           p = p->next;
    }

    // Remove 'sign' from the list
    if (book->signList == sign)
        book->signList = sign->next;

    if (sign->prev)
        sign->prev->next = sign->next;

    if (sign->next != NULL)
        sign->next->prev = sign->prev;

    sign->prev = NULL;
    sign->next = NULL;

    // Re-insert 'sign' at the right place
    if (p->priority <= sign->priority) {
        // 'sign' has a higher priority and should be inserted before 'p'
        sign->prev = p->prev;
        sign->next = p;
        p->prev = sign;
        if (sign->prev)
            sign->prev->next = sign;

        if (book->signList == p)
            book->signList = sign;
    } else {
        // 'sign' has a lower priority and should be inserted after 'p'
        sign->prev = p;
        sign->next = p->next;
        p->next = sign;
        if (sign->next)
            sign->next->prev = sign;
    }
}

// Add the sign into the signlist. Find the right spot to do it though.
private void
buf_addsign(
   Book *book, // book to store sign in
   int id, // sign ID
   Byte *groupname, // sign group
   int prio, // sign priority
   LineNr lnum, // line number which gets the mark
   int typenr) // typenr of sign we are adding
{
    SignEntry *sign = NULL; // a sign in the signlist
    SignEntry *prev = NULL; // the previous sign
    FOR_ALL_SIGNS_IN_BOOK(book, sign) {
        if (lnum == sign->lnum && id == sign->id && sign_in_group(sign, groupname)) {
            // Update an existing sign
            sign->typeNr = typenr;
            sign->priority = prio;
            sign_sort_by_prio_on_line(book, sign);
            return;
        } ei (lnum < sign->lnum) {
            insert_sign_by_lnum_prio(book, prev, id, groupname, prio, lnum, typenr);
            return;
        }
        prev = sign;
    }

    insert_sign_by_lnum_prio(book, prev, id, groupname, prio, lnum, typenr);
}

//For an existing, placed sign "markId" change the type to "typenr".
//Return the line number of the sign, or zero if the sign is not found.
private LineNr
buf_change_sign_type(
   Book *book, // book to store sign in
   int markId, // sign ID
   Byte *group, // sign group
   int typenr, // typenr of sign we are adding
   int prio // sign priority
){
    SignEntry *sign = NULL; // a sign in the signlist
    FOR_ALL_SIGNS_IN_BOOK(book, sign) {
        if (sign->id == markId && sign_in_group(sign, group)) {
            sign->typeNr = typenr;
            sign->priority = prio;
            sign_sort_by_prio_on_line(book, sign);
            return sign->lnum;
        }
    }

    return (LineNr)0;
}

// Return the decorations of the first sign placed on line 'lnum' in buffer 'buf'. Used when 
// refreshing the screen. Returns TRUE if a sign is found on 'lnum', FALSE otherwise.
int
markGetSignDecorations(Portal *wp, LineNr lnum, OUT SignHilite* signHilites) {
   CLEAR_POINTER(signHilites);

   Book* buf = wp->book;
   SignEntry* sign = NULL;
   FOR_ALL_SIGNS_IN_BOOK(buf, sign) {
      // Signs are sorted by line number in the buffer. No need to check
      // for signs after the specified line number 'lnum'.
      if (sign->lnum > lnum)
          break;

      if (sign->lnum == lnum && sign_group_for_window(sign, wp)) {
         signHilites->typeNr = sign->typeNr;
         Sign *sp = find_sign_by_typenr(sign->typeNr);
         if (!sp)
            return FALSE;

         signHilites->text = sp->text;

         if (signHilites->text != NULL && sp->textHiId > 0)
            signHilites->textHiId = decorationByHiliteId(sp->textHiId);

         if (sp->lineHiId < SHORT)
            signHilites->lineHiId = decorationByHiliteId(sp->lineHiId);

         if (sp->cursorLineHiId < SHORT)
            signHilites->cursorLineHiId = sp->cursorLineHiId;

         if (sp->lineNumHiId < SHORT)
            signHilites->lineNumHiId = sp->lineNumHiId;

         signHilites->priority = sign->priority;

         // If there is another sign next with the same priority, may
         // combine the text and the line highlighting.
         if (sign->next != NULL &&
              sign->next->priority == sign->priority &&
              sign->next->lnum == sign->lnum
         ) {
            Sign *next_sp = find_sign_by_typenr(sign->next->typeNr);
            if (!next_sp)
               return FALSE;

            if (!signHilites->icon && !signHilites->text) {
               signHilites->text = next_sp->text;
            }

            if (sp->textHiId == SHORT && next_sp->textHiId < SHORT)
               signHilites->textHiId = next_sp->textHiId;

            if (sp->lineHiId == SHORT && next_sp->lineHiId < SHORT)
               signHilites->lineHiId = next_sp->lineHiId;

            if (sp->cursorLineHiId == SHORT && next_sp->cursorLineHiId < SHORT)
               signHilites->cursorLineHiId = next_sp->cursorLineHiId;

            if (sp->lineNumHiId == SHORT && next_sp->lineNumHiId < SHORT)
               signHilites->lineNumHiId = next_sp->lineNumHiId;
         }
         return TRUE;
      }
   }
   return FALSE;
}

//Delete sign 'id' in group 'group' from buffer 'buf'.
//If 'id' is zero, then delete all the signs in group 'group'. Otherwise
//delete only the specified sign.
//If 'group' is '*', then delete the sign in all the groups. If 'group' is
//NULL, then delete the sign in the global group. Otherwise delete the sign in
//the specified group.
//Return the line number of the deleted sign. If multiple signs are deleted,
//then returns the line number of the last sign deleted.
private LineNr
delsign(Book* book, // buffer sign is stored in
            LineNr atlnum, // sign at this line, 0 - at any line
            int id, // sign id
            Byte *group) // sign group
{
   // pointer to pointer to current sign
   SignEntry **lastp = &book->signList;
   SignEntry *next = NULL; // the next sign in a signList
   LineNr lnum = 0; // line number whose sign was deleted

   for (SignEntry *sign = book->signList; sign != NULL; sign = next) {
      next = sign->next;

      if ((id == 0 || sign->id == id) &&
            (atlnum == 0 || sign->lnum == atlnum) && sign_in_group(sign, group)
      ) {
          *lastp = next;
          if (next)
             next->prev = sign->prev;

          lnum = sign->lnum;

          if (sign->group)
             sign_group_unref(sign->group->sg_name);

          eeglFree(sign);
          drawBookLineLater(book, lnum);

            // Check whether only one sign needs to be deleted
            // If deleting a sign with a specific identifier in a particular
            // group or deleting any sign at a particular line number, delete
            // only one sign.
            if (!group || (*group != '*' && id != 0) ||
                (*group == '*' && atlnum != 0))
                break;
        } else {
            lastp = &sign->next;
        }
    }

    // When deleting the last sign the cursor position may change, because the
    // sign columns no longer shows.  And the 'signcolumn' may be hidden.
    if (book->signList == NULL) {
        drawBookLater(book, UPD_NOT_VALID);
        changed_line_abv_curs();
    }

    return lnum;
}

//Find the line number of the sign with the requested id in group 'group'. If
//the sign does not exist, return 0 as the line number. This will still let
//the correct file get loaded.
int
buf_findsign(Book *book, // buffer to store sign in
             int id, // sign ID
             Byte *group) // sign group
{
    SignEntry *sign = NULL; // a sign in the signlist
    FOR_ALL_SIGNS_IN_BOOK(book, sign) {
       if (sign->id == id && sign_in_group(sign, group))
          return sign->lnum;
    } 

    return 0;
}

//Return the sign at line 'lnum' in book. Return NULL if a sign is
//not found at the line. If 'groupname' is NULL, search in the global group.
private SignEntry *
getsignAtLine(Book* book, // book whose sign we are searching for
              LineNr lnum, // line number of sign
              CS groupname // sign group name
){
   SignEntry *sign = NULL; // a sign in the signlist
   FOR_ALL_SIGNS_IN_BOOK(book, sign) {
       // Signs are sorted by line number in the book. No need to check
       // for signs after the specified line number 'lnum'.
       if (sign->lnum > lnum)
          break;

       if (sign->lnum == lnum && sign_in_group(sign, groupname))
          return sign;
   }

   return NULL;
}

//Return the identifier of the sign at line number 'lnum' in book.
private int
findsign_id(Book* book, // book whose sign we are searching for
            LineNr lnum, // line number of sign
            CS groupname // sign group name
){
    // a sign in the signlist
    SignEntry *sign = getsignAtLine(book, lnum, groupname);
    if (sign != NULL)
        return sign->id;

    return 0;
}

# if defined(PROTO)
//See if a given type of sign exists on a specific line.
int
buf_findsigntype_id(Book* book, // buffer whose sign we are searching for
                    LineNr lnum, // line number of sign
                    int typenr) // sign type number
{
   SignEntry *sign = NULL; // a sign in the signlist
   FOR_ALL_SIGNS_IN_BOOK(book, sign) {
      // Signs are sorted by line number in the book. No need to check
      // for signs after the specified line number 'lnum'.
      if (sign->lnum > lnum)
         break;

      if (sign->lnum == lnum && sign->typeNr == typenr)
         return sign->id;
   }

   return 0;
}

# endif // FEAT_PROTO

//Delete signs in group 'group' in book. If 'group' is '*', then delete all the signs.
void
markDeleteSigns(Book* book, CS group) {
    // When deleting the last sign need to redraw the windows to remove the
    // sign column. Not when curPor is NULL (this means we're exiting).
    if (book->signList && curPor) {
        drawBookLater(book, UPD_NOT_VALID);
        changed_line_abv_curs();
    }

    // pointer to pointer to current sign
    SignEntry **lastp = &book->signList;
    SignEntry *next = NULL;

    for (SignEntry *sign = book->signList; sign != NULL; sign = next) {
        next = sign->next;
        if (sign_in_group(sign, group)) {
            *lastp = next;

            if (next)
               next->prev = sign->prev;

            if (sign->group)
               sign_group_unref(sign->group->sg_name);

            eeglFree(sign);
        } else {
           lastp = &sign->next;
        }
    }
}

// List placed signs for "rbook".  If "rbook" is NULL do it for all books.
private void
sign_list_placed(Book* rbook, Byte *sign_group) {
   Byte lbuf[MSG_BUF_LEN];
   Byte group[MSG_BUF_LEN];

   msg_puts_title(_("\n--- Signs ---"));
   msg_putchar('\n');

   Book* book = (!rbook) ? firstBook : rbook;
   while (book && !gotInterruptG) {
     if (book->signList != NULL) {
         eeSnprintf(lbuf, MSG_BUF_LEN, _("Signs for %s:"), book->currFileName);
         msgPutsDeco(lbuf, getDecoFlags(HLF_D));
         msg_putchar('\n');
      }

      SignEntry *sign = NULL;
      FOR_ALL_SIGNS_IN_BOOK(book, sign) {
         if (gotInterruptG)
            break;

         if (!sign_in_group(sign, sign_group))
            continue;

         if (sign->group)
            eeSnprintf(group, MSG_BUF_LEN, _("  group=%s"), sign->group->sg_name);
         else
            group[0] = '\0';

         eeSnprintf(lbuf, MSG_BUF_LEN,
                      _("    line=%ld  id=%d%s  name=%s  priority=%d"),
                      (long)sign->lnum, sign->id, group,
                      sign_typenr2name(sign->typeNr), sign->priority);

         msg_puts(lbuf);
         msg_putchar('\n');
      }

      if (rbook)
         break;

      book = book->next;
   }
}

// Adjust a placed sign for inserted/deleted lines.
void
sign_mark_adjust(
    LineNr    line1,
    LineNr    line2,
    long        amount,
    long        amount_after
) {
    SignEntry *sign = NULL; // a sign in a signList
    FOR_ALL_SIGNS_IN_BOOK(curBook, sign) {
        // Ignore changes to lines after the sign
        if (sign->lnum < line1)
            continue;

        LineNr new_lnum = sign->lnum;

        if (sign->lnum <= line2) {
            if (amount != MAXLNUM)
                new_lnum += amount;
        } ei (sign->lnum > line2) {
            // Lines inserted or deleted before the sign
            new_lnum += amount_after;
        }

        // If the new sign line number is past the last line in the book,
        // then don't adjust the line number. Otherwise, it will always be past
        // the last line and will not be visible.
        if (new_lnum <= curBook->mem.lineCount)
            sign->lnum = new_lnum;
    }
}

//Find index of a ":sign" subcmd from its name. "*end_cmd" must be writable.
private int
sign_cmd_idx(CS begin_cmd, // begin of sign subcmd
             CS end_cmd // just after sign subcmd
){
    int idx = 0;
    char save = *end_cmd;
    *end_cmd = ZERO;

    while (cmds[idx] != NULL && STRCMP(begin_cmd, cmds[idx]) != 0)
       ++idx;

    *end_cmd = save;
    return idx;
}

// Find a sign by name. Also returns pointer to the previous sign.
private Sign*
sign_find(CS name, Sign** sp_prev) {
    if (sp_prev)
       *sp_prev = NULL;

    Sign *sp = NULL;
    FOR_ALL_SIGNS(sp) {
       if (STRCMP(sp->name, name) == 0)
          break;

       if (sp_prev != NULL)
          *sp_prev = sp;
    }

    return sp;
}

// Allocate a new sign
private Sign *
alloc_new_sign(CS name) {
    int start = next_sign_typenr;

    // Allocate a new sign.
    Sign *sp = allocZeroed_id(sizeof(Sign), aid_sign_define_by_name);

    // Check that next_sign_typenr is not already being used.
    // This only happens after wrapping around.  Hopefully
    // another one got deleted and we can use its number.
    Sign *lp = first_sign;
    while (lp != NULL) {
        if (lp->typeNr == next_sign_typenr) {
            ++next_sign_typenr;

            if (next_sign_typenr == MAX_TYPENR)
               next_sign_typenr = 1;

            if (next_sign_typenr == start) {
               eeglFree(sp);
               emsg(_(e_too_many_signs_defined));
               return NULL;
            }

            lp = first_sign; // start all over
            continue;
        }
        lp = lp->next;
    }

    sp->typeNr = next_sign_typenr;

    if (++next_sign_typenr == MAX_TYPENR)
        next_sign_typenr = 1; // wrap around

    sp->name = copyStr(name);

    return sp;
}

// Initialize the text for a new sign
private int
sign_define_init_text(Sign *sp, Byte *text) {
    Byte *s = NULL;
    Byte *endp = text + (int)STRLEN(text);
    int cells = 0;

    // Remove backslashes so that it is possible to use a space.
    for (s = text; s + 1 < endp; ++s) {
       if (*s == '\\') {
           STRMOVE(s, s + 1);
           --endp;
       }
    }

    // Count cells and check for non-printable chars
    for (s = text; s < endp; s += utfCharLen(s)) {
            if (!eeIsPrintable((*mb_ptr2char)(s)))
                break;

            cells += (*mb_ptr2cells)(s);
    }

    // Currently sign text must be one or two display cells
    if (s != endp || cells < 1 || cells > 2) {
       showErrFmtMsg(_(e_invalid_sign_text_str), text);
       return FAIL;
    }

    eeglFree(sp->text);
    // Allocate one byte more if we need to pad up with a space.
    int len = (int)(endp - text + ((cells == 1) ? 1 : 0));
    sp->text = copySubstr(text, len);

    // For single character sign text, pad with a space.
    if (sp->text != NULL && cells == 1)
        STRCPY(sp->text + len - 1, " ");

    return OK;
}

// Define a new sign or update an existing sign
int
sign_define_by_name(
   Byte *name,
   Byte *linehl,
   Byte *textt,
   Byte *texthl,
   Byte *culhl,
   Byte *numhl,
   int prio
){
    Sign *sp_prev = NULL;
    Sign *sp = sign_find(name, &sp_prev);
    if (!sp) {
        sp = alloc_new_sign(name);
        if (!sp)
           return FAIL;

        // add the new sign to the list of signs
        if (!sp_prev)
           first_sign = sp;
        else
           sp_prev->next = sp;
    } else {
        Portal *wp = NULL;
        // Signs may already exist, a redraw is needed in windows with a
        // non-empty sign list.
        FOR_ALL_PORTALS(wp) {
           if (wp->book->signList != NULL)
               drawBookLater(wp->book, UPD_NOT_VALID);
        }
    }

    // set values for a defined sign.

    if (textt && (sign_define_init_text(sp, textt) == FAIL))
        return FAIL;

    sp->priority = prio;

    if (linehl) {
       if (*linehl == ZERO)
          sp->lineHiId = 0;
       else
          sp->lineHiId = hiliteGroupByName(text(linehl));
    }

    if (texthl) {
       if (*texthl == ZERO)
          sp->textHiId = 0;
       else
          sp->textHiId = hiliteGroupByName(text(texthl));
    }

    if (culhl) {
       if (*culhl == ZERO)
          sp->cursorLineHiId = 0;
       else
          sp->cursorLineHiId = hiliteGroupByName(text(culhl));
    }

    if (numhl) {
       if (*numhl == ZERO)
          sp->lineNumHiId = 0;
       else
          sp->lineNumHiId = hiliteGroupByName(text(numhl));
    }

    return OK;
}

// Return TRUE if sign "name" exists.
int
sign_exists_by_name(Byte *name) {
   return sign_find(name, NULL) != NULL;
}

// Free the sign specified by 'name'.
int
sign_undefine_by_name(Byte *name, Boole give_error) {
   Sign *sp_prev = NULL;
   Sign *sp = sign_find(name, &sp_prev);
   if (!sp) {
      if (give_error)
         showErrFmtMsg(_(e_unknown_sign_str), name);
      return FAIL;
   }
   sign_undefine(sp, sp_prev);

   return OK;
}

// List the signs matching 'name'
private void
sign_list_by_name(Byte *name) {
   Sign *sp = sign_find(name, NULL);
   if (sp != NULL)
      sign_list_defined(sp);
   else
      showErrFmtMsg(_(e_unknown_sign_str), name);
}

private void
may_force_numberwidth_recompute(Book* book, int unplace) {
   Tab *t;
   Portal *wp;
   FOR_ALL_TAB_PORTALS(t, wp) {
      if (wp->book == book && (unplace || wp->lineCountSaved < 2) && wp->bookOpts.signColumn)
         wp->lineCountSaved = 0;
   }
}

// Place a sign at the specified file location or update a sign.
int
sign_place(
   int *sign_id,
   CS sign_group,
   CS sign_name,
   Book* book,
   LineNr lnum,
   int prio
) {
   //Check for reserved character '*' in group name
   if (sign_group != NULL && (*sign_group == '*' || *sign_group == '\0'))
      return FAIL;

   Sign *sp = NULL;
   FOR_ALL_SIGNS(sp) {
      if (STRCMP(sp->name, sign_name) == 0)
         break;
   }

   if (!sp) {
      showErrFmtMsg(_(e_unknown_sign_str), sign_name);
      return FAIL;
   }

   if (*sign_id == 0)
      *sign_id = sign_group_get_next_signid(book, sign_group);

   //Use the default priority value for this sign.
   if (prio == -1)
      prio = (sp->priority != -1) ? sp->priority : SIGN_DEF_PRIO;

   if (lnum > 0) {
      //":sign place {id} line={lnum} name={name} file={fname}": place a sign
      buf_addsign(book, *sign_id, sign_group, prio, lnum, sp->typeNr);
   } else {
      //":sign place {id} file={fname}": change sign type and/or priority
      lnum = buf_change_sign_type(book, *sign_id, sign_group, sp->typeNr, prio);
   }

    if (lnum > 0) {
       drawBookLineLater(book, lnum);

       //When displaying signs in the 'number' column, if the width of the
       //number column is less than 2, then force recomputing the width.
       may_force_numberwidth_recompute(book, FALSE);
    } else {
       showErrFmtMsg(_(e_not_possible_to_change_sign_str), sign_name);
       return FAIL;
    }

    return OK;
}

// Unplace the specified sign
private int
sign_unplace(int sign_id, Byte *sign_group, Book* book, LineNr atlnum) {
   if (!book->signList) // No signs in the book
      return OK;

   if (sign_id == 0) {
      //Delete all the signs in the specified book
      drawBookLater(book, UPD_NOT_VALID);
      markDeleteSigns(book, sign_group);
   } else {
      //Delete only the specified signs
      LineNr lnum = delsign(book, atlnum, sign_id, sign_group);
      if (lnum == 0)
         return FAIL;
   }

   //When all the signs in a book are removed, force recomputing the number column width 
   //(if enabled) in all the portals into the book if @signcolumn is set to 'number' in that portal
   if (book->signList == NULL)
      may_force_numberwidth_recompute(book, TRUE);

   return OK;
}

// Unplace the sign at the current cursor line.
private void
sign_unplace_at_cursor(CS groupname) {
    int id = findsign_id(curPor->book, curPor->cursor.lnum, groupname);

    if (id > 0)
       sign_unplace(id, groupname, curPor->book, curPor->cursor.lnum);
    else
       emsg(_(e_missing_sign_number));
}

//  Jump to a sign.
private LineNr
sign_jump(int sign_id, Byte *sign_group, Book* book) {
    LineNr lnum = buf_findsign(book, sign_id, sign_group);
    if (lnum <= 0) {
       showErrFmtMsg(_(e_invalid_sign_id_nr), sign_id);
       return -1;
    }

    // goto a sign ...
    if (portTryFindOpenBook(book) != NULL) { // ... in a current portal
       curPor->cursor.lnum = lnum;
       check_cursor_lnum();
       beginline(BL_WHITE);
    } else { // ... not currently in a portal
        if (book->currFileName == NULL) {
           emsg(_(e_cannot_jump_to_buffer_that_does_not_have_name));
           return -1;
        }
        Byte *cmd = alloc(STRLEN(book->currFileName) + 25);

        sprintf((char *)cmd, "e +%ld %s", (long)lnum, book->currFileName);
        executeCommLine(cmd);
        eeglFree(cmd);
    }
    foldOpenCursor();

    return lnum;
}

// ":sign define {name} ..." command
private void
sign_define_cmd(Byte *sign_name, Byte *cmdline) {
   CS arg = NULL;
   CS p = cmdline;
   CS text = NULL;
   CS linehl = NULL;
   CS texthl = NULL;
   CS culhl = NULL;
   CS numhl = NULL;
   int prio = -1;
   int failed = FALSE;

   // set values for a defined sign.
   while (true) {
      arg = skipwhite(p);
      if (*arg == ZERO)
          break;

      p = skiptowhite_esc(arg);
      if (STRNCMP(arg, "text=", 5) == 0) {
          arg += 5;
          text = copySubstr(arg, p - arg);
      } ei (STRNCMP(arg, "linehl=", 7) == 0) {
          arg += 7;
          linehl = copySubstr(arg, p - arg);
      } ei (STRNCMP(arg, "texthl=", 7) == 0) {
          arg += 7;
          texthl = copySubstr(arg, p - arg);
      } ei (STRNCMP(arg, "culhl=", 6) == 0) {
          arg += 6;
          culhl = copySubstr(arg, p - arg);
      } ei (STRNCMP(arg, "numhl=", 6) == 0) {
          arg += 6;
          numhl = copySubstr(arg, p - arg);
      } ei (STRNCMP(arg, "priority=", 9) == 0) {
          arg += 9;
          prio = atoi((char *)arg);
      } else {
          showErrFmtMsg(_(e_invalid_argument_str), arg);
          failed = TRUE;
          break;
      }
   }

   if (!failed)
      sign_define_by_name(sign_name, linehl, text, texthl, culhl, numhl, prio);

   eeglFree(text);
   eeglFree(linehl);
   eeglFree(texthl);
   eeglFree(culhl);
   eeglFree(numhl);
}

// ":sign place" command
private void
sign_place_cmd(
      Book* book,
      LineNr lnum,
      CS sign_name,
      int id,
      CS group,
      int prio
) {
    if (id <= 0) {
        // List signs placed in a file/buffer
        //   :sign place file={fname}
        //   :sign place group={group} file={fname}
        //   :sign place group=* file={fname}
        //   :sign place buffer={nr}
        //   :sign place group={group} buffer={nr}
        //   :sign place group=* buffer={nr}
        //   :sign place
        //   :sign place group={group}
        //   :sign place group=*
        if (lnum >= 0 || sign_name != NULL || (group != NULL && *group == '\0'))
            emsg(_(e_invalid_argument));
        else
            sign_list_placed(book, group);
    } else {
        // Place a new sign
        if (sign_name == NULL || !book || (group && *group == '\0')) {
            emsg(_(e_invalid_argument));
            return;
        }

        sign_place(&id, group, sign_name, book, lnum, prio);
    }
}

// ":sign unplace" command
private void
sign_unplace_cmd(Book* book, LineNr lnum, Byte *sign_name, int id, Byte *group) {
   if (lnum >= 0 || sign_name != NULL || (group != NULL && *group == '\0')) {
       emsg(_(e_invalid_argument));
       return;
   }

   if (id == -2) {
       if (book) {
            // :sign unplace * file={fname}
            // :sign unplace * group={group} file={fname}
            // :sign unplace * group=* file={fname}
            // :sign unplace * buffer={nr}
            // :sign unplace * group={group} buffer={nr}
            // :sign unplace * group=* buffer={nr}
            sign_unplace(0, group, book, 0);
        } else {
            // :sign unplace *
            // :sign unplace * group={group}
            // :sign unplace * group=*
            FOR_ALL_BOOKS(book) {
                if (book->signList != NULL)
                    markDeleteSigns(book, group);
            }
        }
    } else {
        if (book) {
            // :sign unplace {id} file={fname}
            // :sign unplace {id} group={group} file={fname}
            // :sign unplace {id} group=* file={fname}
            // :sign unplace {id} buffer={nr}
            // :sign unplace {id} group={group} buffer={nr}
            // :sign unplace {id} group=* buffer={nr}
            sign_unplace(id, group, book, 0);
        } else {
            if (id == -1) {
                // :sign unplace group={group}
                // :sign unplace group=*
                sign_unplace_at_cursor(group);
            } else {
                // :sign unplace {id}
                // :sign unplace {id} group={group}
                // :sign unplace {id} group=*
                FOR_ALL_BOOKS(book)
                    sign_unplace(id, group, book, 0);
            }
        }
    }
}

//Jump to a placed sign commands:
//  :sign jump {id} file={fname}
//  :sign jump {id} buffer={nr}
//  :sign jump {id} group={group} file={fname}
//  :sign jump {id} group={group} buffer={nr}
private void
sign_jump_cmd(
   Book* book,
   LineNr lnum,
   CS sign_name,
   int id,
   CS group
) {
    if (!sign_name && !group && id == -1) {
        emsg(_(e_argument_required));
        return;
    }

    if (!book || (group && *group == ZERO) || lnum >= 0 || sign_name) {
        // File or book is not specified or an empty group is used
        // or a line number or a sign name is specified.
        emsg(_(e_invalid_argument));
        return;
    }

    (void)sign_jump(id, group, book);
}

//Parse the command line arguments for the ":sign place", ":sign unplace" and
//":sign jump" commands.
//The supported arguments are: line={lnum} name={name} group={group}
//priority={prio} and file={fname} or buffer={nr}.
private int
parse_sign_cmd_args(
   int cmd,
   CS arg,
   OUT CS* sign_name,
   int* signid,
   Byte** group,
   int *prio,
   Book** book,
   LineNr* lnum
) {
    Byte *arg1 = arg;
    Byte *filename = NULL;
    int lnum_arg = FALSE;

    // first arg could be placed sign id
    if (EE_ISDIGIT(*arg)) {
        *signid = getdigits(&arg);
        if (!SPACE_OR_TAB(*arg) && *arg != ZERO) {
            *signid = -1;
            arg = arg1;
        } else {
            arg = skipwhite(arg);
        }
    }

    while (*arg != ZERO) {
        if (STRNCMP(arg, "line=", 5) == 0) {
            arg += 5;
            *lnum = atoi((char *)arg);
            arg = skiptowhite(arg);
            lnum_arg = TRUE;
        } ei (STRNCMP(arg, "*", 1) == 0 && cmd == SIGNCMD_UNPLACE) {
            if (*signid != -1) {
                emsg(_(e_invalid_argument));
                return FAIL;
            }
            *signid = -2;
            arg = skiptowhite(arg + 1);
        } ei (STRNCMP(arg, "name=", 5) == 0) {
            arg += 5;
            Byte *name = arg;
            arg = skiptowhite(arg);
            if (*arg != ZERO)
                *arg++ = ZERO;

            while (name[0] == '0' && name[1] != ZERO)
                ++name;

            *sign_name = name;
        } ei (STRNCMP(arg, "group=", 6) == 0) {
            arg += 6;
            *group = arg;
            arg = skiptowhite(arg);
            if (*arg != ZERO)
                *arg++ = ZERO;
        } ei (STRNCMP(arg, "priority=", 9) == 0) {
            arg += 9;
            *prio = atoi((char *)arg);
            arg = skiptowhite(arg);
        } ei (STRNCMP(arg, "file=", 5) == 0) {
            arg += 5;
            filename = arg;
            *book = booklistFindByNameExpandingLinks(arg);
            break;
        } ei (STRNCMP(arg, "buffer=", 7) == 0) {
            arg += 7;
            filename = arg;
            *book = bookFindFileByBookNr((int)getdigits(&arg));

            if (*skipwhite(arg) != ZERO)
                showErrFmtMsg(_(e_trailing_characters_str), arg);

            break;
        } else {
            emsg(_(e_invalid_argument));
            return FAIL;
        }

        arg = skipwhite(arg);
    }

    if (filename != NULL && *book == NULL) {
       showErrFmtMsg(_(e_invalid_buffer_name_str), filename);
       return FAIL;
    }

    // If the filename is not supplied for the sign place or the sign jump
    // command, then use the current book.
    if (filename == NULL &&
       ((cmd == SIGNCMD_PLACE && lnum_arg) || cmd == SIGNCMD_JUMP))
       *book = curPor->book;

    return OK;
}

void
c_sign(Invocation *invo) {
    CS arg = invo->arg;

    // Parse the subcommand.
    CS p = skiptowhite(arg);
    int idx = sign_cmd_idx(arg, p);
    if (idx == SIGNCMD_LAST) {
       showErrFmtMsg(_(e_unknown_sign_command_str), arg);
       return;
    }
    arg = skipwhite(p);

    if (idx > SIGNCMD_LIST) {
       int id = -1;
       CS group = NULL;
       int prio = -1;
       Book* book = NULL;
       LineNr lnum = -1;

       // Parse command line arguments
       CS sign_name = NULL;
       if (parse_sign_cmd_args(idx, arg, OUT &sign_name, &id, &group, &prio, &book, &lnum) == FAIL)
          return;

       if (idx == SIGNCMD_PLACE)
          sign_place_cmd(book, lnum, sign_name, id, group, prio);
       ei (idx == SIGNCMD_UNPLACE)
          sign_unplace_cmd(book, lnum, sign_name, id, group);
       ei (idx == SIGNCMD_JUMP)
          sign_jump_cmd(book, lnum, sign_name, id, group);

       return;
   }

   // Define, undefine or list signs.
   if (idx == SIGNCMD_LIST && *arg == ZERO) {
        // ":sign list": list all defined signs
        for (Sign *sp = first_sign; sp && !gotInterruptG; sp = sp->next)
            sign_list_defined(sp);
   } ei (*arg == ZERO) {
        emsg(_(e_missing_sign_name));
   } else {

        // Isolate the sign name.  If it's a number skip leading zeroes,
        // so that "099" and "99" are the same sign.  But keep "0".
        p = skiptowhite(arg);
        if (*p != ZERO)
           *p++ = ZERO;

        while (arg[0] == '0' && arg[1] != ZERO)
           ++arg;

        CS name = copyStr(arg);

        if (idx == SIGNCMD_DEFINE)
            sign_define_cmd(name, p);
        ei (idx == SIGNCMD_LIST)
            // ":sign list {name}"
            sign_list_by_name(name);
        else
            // ":sign undefine {name}"
            sign_undefine_by_name(name, true);

        eeglFree(name);
   }
}

// Return information about a specified sign
private void
sign_getinfo(Sign *sp, Bag *retBag) {
    bagAddString(retBag, S"name", sp->name);

    if (sp->text)
       bagAddString(retBag, S"text", sp->text);

    if (sp->priority > 0)
       bagAddNumber(retBag, S"priority", sp->priority);

    if (sp->lineHiId > 0) {
       Text p = getHiliteGroupName(NULL, sp->lineHiId);
       if (p.len == 0)
          p = text(S"NONE");
       bagAddString(retBag, S"linehl", p.c);
    }

    if (sp->textHiId > 0) {
        Text p = getHiliteGroupName(NULL, sp->textHiId);
        if (p.len == 0)
           p = text(S"NONE");
        bagAddString(retBag, S"texthl", p.c);
    }

    if (sp->cursorLineHiId > 0) {
        Text p = getHiliteGroupName(NULL, sp->cursorLineHiId);
        if (p.len == 0)
           p = text(S"NONE");
        bagAddString(retBag, S"culhl", p.c);
    }

    if (sp->lineNumHiId > 0) {
        Text p = getHiliteGroupName(NULL, sp->lineNumHiId);
        if (p.len == 0)
           p = text(S"NONE");
        bagAddString(retBag, S"numhl", p.c);
    }
}

//If 'name' is NULL, return a list of all the defined signs.
//Otherwise, return information about the specified sign.
private void
sign_getlist(CS name, List* retlist) {
    Sign* sp = first_sign;

    if (name) {
       sp = sign_find(name, NULL);
       if (!sp)
          return;
    }

    for (; sp && !gotInterruptG; sp = sp->next) {
        Bag *dict = allocBag_id(aid_sign_getlist);
        if (!dict)
           return;

        if (listAppendBag(retlist, dict) == FAIL)
           return;

        sign_getinfo(sp, dict);

        if (name) // handle only the specified sign
           break;
    }
}

// Returns information about signs placed in a book as list of dicts.
void
markGetBookSigns(Book *book, List *l){
    SignEntry *sign = NULL;
    FOR_ALL_SIGNS_IN_BOOK(book, sign) {
        Bag *d = sign_get_info(sign);
        if (d)
            listAppendBag(l, d);
    }
}

// Return information about all the signs placed in a book
private void
getSignsInBook(
   Book* book,
   LineNr lnum,
   int sign_id,
   CS sign_group,
   List *retlist
) {
   Bag *b = allocBag_id(aid_sign_getplaced_dict);
   if (!b)
      return;

   listAppendBag(retlist, b);

   bagAddNumber(b, S"bufnr", (long)book->fiNum);

   List *l = list_alloc_id(aid_sign_getplaced_list);
   if (!l)
      return;

   bagAddList(b, S"signs", l);

   SignEntry *sign = NULL;
   FOR_ALL_SIGNS_IN_BOOK(book, sign) {
      if (!sign_in_group(sign, sign_group))
          continue;

      if ((lnum == 0 && sign_id == 0) ||
            (sign_id == 0 && lnum == sign->lnum) ||
            (lnum == 0 && sign_id == sign->id) ||
            (lnum == sign->lnum && sign_id == sign->id)
      ) {
         Bag *sdict = sign_get_info(sign);
         if (sdict)
            listAppendBag(l, sdict);
      }
   }
}

//Get a list of signs placed in book. If 'num' is non-zero, return the
//sign placed at the line number. If 'lnum' is zero, return all the signs
//placed in 'book'. If 'book' is NULL, return signs placed in all the books.
private void
sign_get_placed(
   Book* book,
   LineNr lnum,
   int sign_id,
   Byte *sign_group,
   List *retlist
) {
   if (book) {
      getSignsInBook(book, lnum, sign_id, sign_group, retlist);
   } else {
      FOR_ALL_BOOKS(book) {
         if (book->signList)
            getSignsInBook(book, 0, sign_id, sign_group, retlist);
      }
   }
}

// List one sign.
private void
sign_list_defined(Sign* sp) {
   Byte lbuf[MSG_BUF_LEN];

   smsg("sign %s", sp->name);
   if (sp->text) {
      msg_puts(S" text=");
      msg_outtrans(sp->text);
   }

   if (sp->priority > 0) {
      eeSnprintf(lbuf, MSG_BUF_LEN, " priority=%d", sp->priority);
      msg_puts(lbuf);
   }

   if (sp->lineHiId > 0) {
      msg_puts(S" linehl=");
      Text p = getHiliteGroupName(NULL, sp->lineHiId);
      if (p.len == 0)
         msg_puts(S"NONE");
      else
         msg_puts(p.c);
   }

   if (sp->textHiId > 0) {
       msg_puts(S" texthl=");

       Text p = getHiliteGroupName(NULL, sp->textHiId);
       if (p.len == 0)
          msg_puts(S"NONE");
       else
          msg_puts(p.c);
    }

    if (sp->cursorLineHiId > 0) {
       msg_puts(S" culhl=");

       Text p = getHiliteGroupName(NULL, sp->cursorLineHiId);
       if (p.len == 0)
          msg_puts(S"NONE");
       else
          msg_puts(p.c);
    }

    if (sp->lineNumHiId > 0) {
       msg_puts(S" numhl=");

       Text p = getHiliteGroupName(NULL, sp->lineNumHiId);
       if (p.len == 0)
          msg_puts(S"NONE");
       else
          msg_puts(p.c);
    }
}

// Undefine a sign and free its memory.
private void
sign_undefine(Sign *sp, Sign *sp_prev) {
   eeglFree(sp->name);
   eeglFree(sp->text);

   if (!sp_prev)
      first_sign = sp->next;
   else
      sp_prev->next = sp->next;

   eeglFree(sp);
}

// Undefine/free all signs.
void
free_signs(void) {
   while (first_sign)
      sign_undefine(first_sign, NULL);
}

private enum {
   EXP_SUBCMD, // expand :sign sub-commands
   EXP_DEFINE, // expand :sign define {name} args
   EXP_PLACE, // expand :sign place {id} args
   EXP_LIST, // expand :sign place args
   EXP_UNPLACE, // expand :sign unplace"
   EXP_SIGN_NAMES, // expand with name of placed signs
   EXP_SIGN_GROUPS // expand with name of placed sign groups
} expandWhatS;

// Return the n'th sign name (used for command line completion)
private Byte *
get_nth_sign_name(int idx) {
   int current_idx = 0;
   Sign* sp = NULL;

   // Complete with name of signs already defined
   FOR_ALL_SIGNS(sp) {
      if (current_idx++ == idx)
         return sp->name;
   }
   return NULL;
}

// Return the n'th sign group name (used for command line completion)
private Byte *
get_nth_sign_group_name(int idx) {
   int current_idx = 0;
   int todo = (int)signGroups.count;
   EeSetItem *hi = NULL;

   // Complete with name of sign groups already defined
   FOR_ALL_HASHTAB_ITEMS(&signGroups, hi, todo) {
      if (!HASHITEM_EMPTY(hi)) {
         --todo;
         if (current_idx++ == idx) {
             SignGroup *group = HI2SG(hi);
             return group->sg_name;
         }
      }
   }
   return NULL;
}

// Function given to expandGeneric() to obtain the sign command expansion.
Byte *
get_sign_name(Expand *xp UNUSED, int idx) {
    switch (expandWhatS) {
    case EXP_SUBCMD:
       return (CS)cmds[idx];
    case EXP_DEFINE: {
       char *define_arg[] = { 
           "culhl=", "icon=",   "linehl=",   "numhl=", "text=",  "texthl=", "priority=", NULL 
       };
       return (CS)define_arg[idx];
    }
    case EXP_PLACE: {
       char *place_arg[] = { 
          "line=", "name=", "group=", "priority=", "file=", "buffer=", NULL 
       };
       return (CS)place_arg[idx];
    }
    case EXP_LIST: {
       char *list_arg[] = { "group=", "file=", "buffer=", NULL };
       return (CS)list_arg[idx];
    }
    case EXP_UNPLACE: {
       char *unplace_arg[] = { "group=", "file=", "buffer=", NULL };
       return (CS)unplace_arg[idx];
    }
    case EXP_SIGN_NAMES:
       return get_nth_sign_name(idx);
    case EXP_SIGN_GROUPS:
       return get_nth_sign_group_name(idx);
    default:
       return NULL;
    }
}

// Handle command line completion for :sign command.
void
set_context_in_sign_cmd(Expand* xp, CS arg) {
   CS p;
   CS last;

   // Default: expand subcommands.
   xp->context = EXPAND_SIGN;
   expandWhatS = EXP_SUBCMD;
   xp->input = mbText(arg);

   CS end_subcmd = skiptowhite(arg);
   // expand subcmd name
   // :sign {subcmd}<CTRL-D>
   if (*end_subcmd == ZERO)
      return;

   int cmd_idx = sign_cmd_idx(arg, end_subcmd);

   // :sign {subcmd} {subcmd_args}
   //                |
   //                begin_subcmd_args
   CS begin_subcmd_args = skipwhite(end_subcmd);

   // expand last argument of subcmd

   // :sign define {name} {args}...
   //              |
   //              p

   // Loop until reaching last argument.
   p = begin_subcmd_args;
   do {
      p = skipwhite(p);
      last = p;
      p = skiptowhite(p);
   } while (*p != ZERO);

   p = firstOccurrence(last, '=');

   // :sign define {name} {args}... {last}=
   //                               |     |
   //                            last     p
   if (p == NULL) {
       // Expand last argument name (before equal sign).
       xp->input = mbText(last);
       switch (cmd_idx) {
       case SIGNCMD_DEFINE:
          expandWhatS = EXP_DEFINE;
          break;
       case SIGNCMD_PLACE:
          // List placed signs
          if (EE_ISDIGIT(*begin_subcmd_args))
             //   :sign place {id} {args}...
             expandWhatS = EXP_PLACE;
          else
             //   :sign place {args}...
             expandWhatS = EXP_LIST;
          break;
       case SIGNCMD_LIST:
       case SIGNCMD_UNDEFINE:
          // :sign list <CTRL-D>
          // :sign undefine <CTRL-D>
          expandWhatS = EXP_SIGN_NAMES;
          break;
       case SIGNCMD_JUMP:
       case SIGNCMD_UNPLACE:
          expandWhatS = EXP_UNPLACE;
          break;
       default:
          xp->context = EXPAND_NOTHING;
       }
   } else {
      // Expand last argument value (after equal sign).
      xp->input = mbText(p + 1);
      switch (cmd_idx) {
      case SIGNCMD_DEFINE:
         if (STRNCMP(last, "texthl", 6) == 0
            || STRNCMP(last, "linehl", 6) == 0
            || STRNCMP(last, "culhl", 5) == 0
            || STRNCMP(last, "numhl", 5) == 0
         )
            xp->context = EXPAND_HILITE_GROUP;
         ei (STRNCMP(last, "icon", 4) == 0)
            xp->context = EXPAND_FILES;
         else
            xp->context = EXPAND_NOTHING;
         break;
      case SIGNCMD_PLACE:
         if (STRNCMP(last, "name", 4) == 0)
            expandWhatS = EXP_SIGN_NAMES;
         ei (STRNCMP(last, "group", 5) == 0)
            expandWhatS = EXP_SIGN_GROUPS;
         ei (STRNCMP(last, "file", 4) == 0)
            xp->context = EXPAND_BUFFERS;
         else
            xp->context = EXPAND_NOTHING;
         break;
      case SIGNCMD_UNPLACE:
      case SIGNCMD_JUMP:
         if (STRNCMP(last, "group", 5) == 0)
            expandWhatS = EXP_SIGN_GROUPS;
         ei (STRNCMP(last, "file", 4) == 0)
            xp->context = EXPAND_BUFFERS;
         else
            xp->context = EXPAND_NOTHING;
         break;
      default:
         xp->context = EXPAND_NOTHING;
      }
   }
}

// Define a sign using the attributes in 'dict'. Returns 0 on success and -1 on failure.
private int
sign_define_from_dict(CS name_arg, Bag* bag) {
   CS linehl = NULL;
   CS text = NULL;
   CS texthl = NULL;
   CS culhl = NULL;
   CS numhl = NULL;
   int prio = -1;
   int retval = -1;

   if (!name_arg && !bag)
      return retval;

   CS name = name_arg ? copyStr(name_arg) : bagGetString(bag, tConst("name"), true);

   if (!name || name[0] == ZERO)
      goto cleanup;

   if (bag) {
      linehl = bagGetString(bag, tConst("linehl"), true);
      text = bagGetString(bag, tConst("text"), true);
      texthl = bagGetString(bag, tConst("texthl"), true);
      culhl = bagGetString(bag, tConst("culhl"), true);
      numhl = bagGetString(bag, tConst("numhl"), true);
      prio = bagGetNumber_def(bag, tConst("priority"), -1);
   }

   if (sign_define_by_name(name, linehl, text, texthl, culhl, numhl, prio) == OK)
      retval = 0;

cleanup:
   eeglFree(name);
   eeglFree(linehl);
   eeglFree(text);
   eeglFree(texthl);
   eeglFree(culhl);
   eeglFree(numhl);

   return retval;
}

// Define multiple signs using attributes from list 'l' and store the return values in 'retlist'.
private void
sign_define_multiple(List* l, List* retlist) {
   ListItem *li = NULL;
   FOR_ALL_LIST_ITEMS(l, li) {
      int retval = -1;

      if (li->c.tag == VAR_BAG)
         retval = sign_define_from_dict(NULL, li->c.bag);
      else
         emsg(_(e_dictionary_required));

      list_append_number(retlist, retval);
   }
}

void
f_sign_define(Var *argvars, Var *returnVar) {
   if (argvars[0].tag == VAR_LIST && argvars[1].tag == VAR_UNKNOWN) {
      // Define multiple signs
      allocReturnList(returnVar);

      sign_define_multiple(argvars[0].list, returnVar->list);
      return;
   }

   // Define a single sign
   returnVar->number = -1;

   CS name = convertVarToStringSingleUse(&argvars[0]);
   if (!name)
      return;

   if (check_for_oself_arg(argvars, 1) == FAIL)
        return;

   returnVar->number = sign_define_from_dict(
        name, argvars[1].tag == VAR_BAG ? argvars[1].bag : NULL);
}

void
f_sign_getdefined(Var *argvars, Var *returnVar) {
    if (allocReturnList_id(returnVar, aid_sign_getdefined) == FAIL)
       return;

    CS name = NULL;
    if (argvars[0].tag != VAR_UNKNOWN)
        name = tv_get_string(&argvars[0]);

    sign_getlist(name, returnVar->list);
}

void
f_sign_getplaced(Var *argvars, Var *returnVar) {
   Book* book = NULL;
   LineNr lnum = 0;
   int sign_id = 0;
   Byte *group = NULL;

   if (allocReturnList_id(returnVar, aid_sign_getplaced) == FAIL)
       return;

   if (argvars[0].tag != VAR_UNKNOWN) {
      // get signs placed in the specified book
      book = evGetBookArg(&argvars[0]);
      if (!book)
         return;

      if (argvars[1].tag != VAR_UNKNOWN) {
         if (check_for_nonnull_dict_arg(argvars, 1) == FAIL)
            return;

         DictItem *di = NULL;
         Bag *dict = argvars[1].bag;

         if ((di = bagFind(dict, tConst("lnum"))) != NULL) {
            // get signs placed at this line
            Boole notanum = false;
            varGetNumberChk(&di->c, OUT &notanum);
            if (notanum)
               return;

            lnum = tv_get_lnum(&di->c);
         }

         if ((di = bagFind(dict, tConst("id"))) != NULL) {
            // get sign placed with this identifier
            Boole notanum = false;
            sign_id = (int)varGetNumberChk(&di->c, OUT &notanum);
            if (notanum)
               return;
         }

         if ((di = bagFind(dict, tConst("group"))) != NULL) {
            group = convertVarToStringSingleUse(&di->c);
            if (!group)
               return;

            if (*group == '\0') // empty string means global group
               group = NULL;
         }
      }
   }

   sign_get_placed(book, lnum, sign_id, group, returnVar->list);
}

void
f_sign_jump(Var *argvars, Var *returnVar) {
   returnVar->number = -1;

   Boole notanum = false;
   // Sign identifier
   int sign_id = (int)varGetNumberChk(argvars, OUT &notanum);
   if (notanum)
      return;

   if (sign_id <= 0) {
      emsg(_(e_invalid_argument));
      return;
   }

   // Sign group
   CS sign_group = convertVarToStringSingleUse(&argvars[1]);
   if (!sign_group)
       return;

   if (sign_group[0] == '\0') {
      sign_group = NULL; // global sign group
   } else {
      sign_group = copyStr(sign_group);
   }

   // Book to place the sign
   Book* book = evGetBookArg(&argvars[2]);
   if (!book)
       goto cleanup;

   returnVar->number = sign_jump(sign_id, sign_group, book);

cleanup:
   eeglFree(sign_group);
}

//Place a new sign using the values specified in dict 'dict'. Returns the sign
//identifier if successfully placed, otherwise returns 0.
private int
sign_place_from_dict(
   Var* id_tv,
   Var* group_tv,
   Var* name_tv,
   Var* buf_tv,
   Bag* dict
){
   int sign_id = 0;
   CS group = NULL;
   Byte *sign_name = NULL;
   DictItem *di = NULL;
   LineNr lnum = 0;
   int prio = -1;
   int ret_sign_id = -1;

   // sign identifier
   if (!id_tv && (di = bagFind(dict, tConst("id")))) {
      id_tv = &di->c;
   }

   if (!id_tv) {
      sign_id = 0;
   } else {
      Boole notanum = false;
      sign_id = varGetNumberChk(id_tv, OUT &notanum);
      if (notanum)
         return -1;

      if (sign_id < 0) {
         emsg(_(e_invalid_argument));
         return -1;
      }
   }

   // sign group
   if (!group_tv) {
      di = bagFind(dict, tConst("group"));
      if (di)
         group_tv = &di->c;
   }

   if (group_tv == NULL) {
      group = NULL; // global group
   } else {
      group = convertVarToStringSingleUse(group_tv);
      if (!group)
         goto cleanup;

      if (group[0] == '\0') { // global sign group
         group = NULL;
      } else {
         group = copyStr(group);
      }
   }

   // sign name
   if (!name_tv) {
      di = bagFind(dict, tConst("name"));
      if (di)
         name_tv = &di->c;
   }

   if (name_tv == NULL)
      goto cleanup;

   sign_name = convertVarToStringSingleUse(name_tv);
   if (sign_name == NULL)
      goto cleanup;

   // buffer to place the sign
   if (buf_tv == NULL) {
        di = bagFind(dict, tConst("buffer"));
        if (di)
           buf_tv = &di->c;
   }

   if (!buf_tv)
      goto cleanup;

   Book* book = evGetBookArg(buf_tv);
   if (!book)
      goto cleanup;

   // line number of the sign
   di = bagFind(dict, tConst("lnum"));
   if (di) {
      lnum = tv_get_lnum(&di->c);
      if (lnum <= 0) {
            emsg(_(e_invalid_argument));
            goto cleanup;
        }
    }

    // sign priority
    di = bagFind(dict, tConst("priority"));
    if (di) {
        Boole notanum = false;
        prio = (int)varGetNumberChk(&di->c, OUT &notanum);
        if (notanum)
           goto cleanup;
    }

    if (sign_place(&sign_id, group, sign_name, book, lnum, prio) == OK)
        ret_sign_id = sign_id;

cleanup:
    eeglFree(group);

    return ret_sign_id;
}

void
f_sign_place(Var *argvars, Var *returnVar) {
   Bag* bag = NULL;
   returnVar->number = -1;

   if (argvars[4].tag != VAR_UNKNOWN) {
        if (check_for_nonnull_dict_arg(argvars, 4) == FAIL)
            return;
        bag = argvars[4].bag;
   }

   returnVar->number = sign_place_from_dict(&argvars[0], &argvars[1], &argvars[2], &argvars[3], bag);
}

//"sign_placelist()" function.  Place multiple signs.
void
f_sign_placelist(Var *argvars, Var *returnVar) {
   allocReturnList(returnVar);
   if (confirmVarIsList(argvars, 0) == FAIL)
      return;

   // Process the List of sign attributes
   ListItem *li = NULL;
   FOR_ALL_LIST_ITEMS(argvars[0].list, li) {
      int sign_id = -1;

      if (li->c.tag == VAR_BAG)
         sign_id = sign_place_from_dict(NULL, NULL, NULL, NULL, li->c.bag);
      else
         emsg(_(e_dictionary_required));

      list_append_number(returnVar->list, sign_id);
   }
}

// Undefine multiple signs
private void
sign_undefine_multiple(List *l, List *retlist) {
    ListItem *li = NULL;
    FOR_ALL_LIST_ITEMS(l, li)
    {
        int retval = -1;
        Byte *name = convertVarToStringSingleUse(&li->c);
        if (name != NULL && (sign_undefine_by_name(name, true) == OK))
            retval = 0;

        list_append_number(retlist, retval);
    }
}

void
f_sign_undefine(Var *argvars, Var *returnVar) {
   if (argvars[0].tag == VAR_LIST && argvars[1].tag == VAR_UNKNOWN) {
        // Undefine multiple signs
        allocReturnList(returnVar);

        sign_undefine_multiple(argvars[0].list, returnVar->list);
        return;
   }

   returnVar->number = -1;
   if (argvars[0].tag == VAR_UNKNOWN) {
      // Free all the signs
      free_signs();
      returnVar->number = 0;
   } else {
       // Free only the specified sign
       Byte *name = convertVarToStringSingleUse(&argvars[0]);
       if (name == NULL)
          return;

       if (sign_undefine_by_name(name, true) == OK)
           returnVar->number = 0;
   }
}

// Unplace the sign with attributes specified in 'dict'. Returns 0 on success and -1 on failure.
private int
sign_unplace_from_dict(Var *group_tv, Bag *dict) {
   int sign_id = 0;
   Book* book = NULL;
   int retval = -1;

   // sign group
   CS group = group_tv ? tv_get_string(group_tv) : bagGetString(dict, tConst("group"), FALSE);

   if (group) {
      if (group[0] == '\0') { // global sign group
         group = NULL;
      } else {
         group = copyStr(group);
      }
   }

    if (dict) {
      DictItem *di = bagFind(dict, tConst("buffer"));
      if (di) {
         book = evGetBookArg(&di->c);
          if (!book)
              goto cleanup;
      }

      if (bagHasKey(dict, tConst("id"))) {
         sign_id = bagGetNumber(dict, tConst("id"));
         if (sign_id <= 0) {
            emsg(_(e_invalid_argument));
            goto cleanup;
         }
      }
   }

   if (!book) {
      // Delete the sign in all the books
      retval = 0;
      FOR_ALL_BOOKS(book) {
         if (sign_unplace(sign_id, group, book, 0) != OK)
            retval = -1;
      } 
   } ei (sign_unplace(sign_id, group, book, 0) == OK)
      retval = 0;

cleanup:
   eeglFree(group);

   return retval;
}

SignEntry *
get_first_valid_sign(Portal *wp) {
   SignEntry *sign = wp->book->signList;
   while (sign && !sign_group_for_window(sign, wp))
      sign = sign->next;
   return sign;
}

Boole
isSigncolumnOn(Portal *wp) {
   return get_first_valid_sign(wp) != NULL ? wp->bookOpts.signColumn : false;
}

void
f_sign_unplace(Var *argvars, Var *returnVar) {
   Bag *dict = NULL;
   returnVar->number = -1;

   if ((check_for_string_arg(argvars, 0) == FAIL || check_for_oself_arg(argvars, 1) == FAIL))
      return;

   if (argvars[1].tag != VAR_UNKNOWN)
      dict = argvars[1].bag;

   returnVar->number = sign_unplace_from_dict(&argvars[0], dict);
}

void
f_sign_unplacelist(Var *argvars, Var *returnVar) {
   allocReturnList(returnVar);

   if (confirmVarIsList(argvars, 0) == FAIL)
      return;

   ListItem *li = NULL;
   FOR_ALL_LIST_ITEMS(argvars[0].list, li) {
      int retval = -1;

      if (li->c.tag == VAR_BAG)
         retval = sign_unplace_from_dict(NULL, li->c.bag);
      else
         emsg(_(e_dictionary_required));

      list_append_number(returnVar->list, retval);
   }
}

//}}}
