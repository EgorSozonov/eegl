//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## juggle.c: low-level operations and operators for changing text

#include "eegl.h"

#define TABSIZE_MAX 16

//{{{forward decls

private int coladvance2(Pos *pos, int addspaces, int finetune, ColNr wcol);
private void fixthisline(int (*get_the_indent)(void));
private int get_indent_str(CS ptr, int ts);
private Boole cin_is_cinword(CS line);

//}}}
//{{{changes to text

//If the file is readonly, give a warning message with the first change. Don't do this for 
//autocommands. Don't use emsg() because it flushes the macro buffer.
//If we have undone all changes, "wasModified" will be false, but "didWarnReadonly" will be TRUE.
//"col" is the column for the message; non-zero when in insert mode and 'showmode' is on.
//Careful: may trigger autocommands that reload the book.
void
change_warning(int col) {
   static CS w_readonly = S"W10: Warning: Changing a readonly file";

   if (curBook->didWarnReadonly
          || doWasCurBookChanged()
          || autocmd_busy
          || curBook->o.modifiable)
      return;

   ++curBookLock;
   applyAutocomms(EVENT_FILECHANGEDRO, NULL, NULL, false, curBook);
   --curBookLock;
   if (curBook->o.modifiable)
      return;

   // Do what msg() does, but with a column offset if the warning should
   // be after the mode message.
   msg_start();
   if (msgRowG == visibleRowsG - 1)
      msgColG = col;
   msg_source(getDecoFlags(HLF_W));
   msgPutsDeco(_(w_readonly), getDecoFlags(HLF_W) | MSG_HIST);
   set_EeglVar_string(VV_WARNINGMSG, (CS)_(w_readonly), -1);
   msg_clr_eos();
   (void)msg_end();
   if (msg_silent == 0 && !silentModeG
    && time_for_testing != 1
   ) {
       out_flush();
       ui_delay(1002L, TRUE); // give the user time to think about it
   }
   curBook->didWarnReadonly = TRUE;
   redrawCommlineG = FALSE;   // don't redraw and erase the message
   if (msgRowG < visibleRowsG - 1)
      showmode();
}

//Call this function when something in the current book is changed.
//
//Most often called through changed_bytes() and changed_lines(), which also
//mark the area of the display to be redrawn.
//
//Careful: may trigger autocommands that reload the book.
void
changed(void) {
   if (!curBook->wasModified) {
      int   save_msg_scroll = msg_scroll;

      // Give a warning about changing a read-only file. This may also
      // check-out the file, thus change "curBook"!
      change_warning(0);

      // Create a swap file if that is wanted.
      // Don't do this for "nofile" and "nowrite" book types.
      if (curBook->maySwap && curBook->currFileName && !bookDontWrite(curBook)) {
         int save_need_wait_return = need_wait_return;

         need_wait_return = FALSE;
         memOpenSwapFile(curBook);

         // The memOpenSwapFile() can cause an ATTENTION message.
         // Wait two seconds, to make sure the user reads this unexpected
         // message.  Since we could be anywhere, call wait_return() now,
         // and don't let the emsg() set msg_scroll.
         if (need_wait_return && emsg_silent == 0 && !in_assert_fails) {
            out_flush();
            ui_delay(2002L, TRUE);
            wait_return(TRUE);
            msg_scroll = save_msg_scroll;
         } else
            need_wait_return = save_need_wait_return;
      }
      changed_internal();
   }
   ++CHANGEDTICK(curBook);

   // If a pattern is highlighted, the position may now be invalid.
   highlight_match = FALSE;
}

//check_status: called when the status bars for the book 'book'
//       need to be updated
private void
check_status(Book* book) {
   Portal   *po;
   FOR_ALL_PORTALS(po) {
      if (po->book == book && po->statusHeight) {
         po->statusLineNeedsRedraw = true;
         set_must_redraw(UPD_VALID);
      }
   } 
}

//Internal part of changed(), no user interaction. Also used for recovery.
void
changed_internal(void) {
   curBook->wasModified = true;
   ml_setflags(curBook);
   check_status(curBook);
   needRedrawTabpanelG = TRUE;
}

private long next_listener_id = 0;

//Check if the change at "lnum" is above or overlaps with an existing
//change. If above then flush changes and invoke listeners.
private void
checkRecordedChanges(
   Book* book,
   LineNr lnum,
   LineNr lnume,
   long xtra
) {
   if (book->recordedChanges == NULL || xtra == 0)
      return;

   ListItem *li;
   LineNr    prev_lnum;
   LineNr    prev_lnume;

   FOR_ALL_LIST_ITEMS(book->recordedChanges, li) {
      prev_lnum = (LineNr)bagGetNumber( li->c.bag, tConst("lnum"));
      prev_lnume = (LineNr)bagGetNumber( li->c.bag, tConst("end"));
      if (prev_lnum >= lnum || prev_lnum > lnume || prev_lnume >= lnum) {
          // the current change is going to make the line number in
          // the older change invalid, flush now
          invoke_listeners(curBook);
          break;
      }
   }
}

//Record a change for listeners added with listener_add(). Always for the current book.
private void
may_record_change(
    LineNr   lnum,
    ColNr   col,
    LineNr   lnume,
    long   xtra
) {
   Bag   *dict;

   if (curBook->listener == NULL)
      return;

   // If the new change is going to change the line numbers in already listed
   // changes, then flush.
   checkRecordedChanges(curBook, lnum, lnume, xtra);

   if (curBook->recordedChanges == NULL) {
      curBook->recordedChanges = list_alloc();
      ++curBook->recordedChanges->refcount;
      curBook->recordedChanges->lock = VAR_FIXED;
   }

   dict = allocBag();
   bagAddNumber(dict, S"lnum", (Long)lnum);
   bagAddNumber(dict, S"end", (Long)lnume);
   bagAddNumber(dict, S"added", (Long)xtra);
   bagAddNumber(dict, S"col", (Long)col + 1);

   listAppendBag(curBook->recordedChanges, dict);
}

// Return something that fits into an int.
int
trim_to_int(Long x) {
   return x > INT_MAX ? INT_MAX : x < INT_MIN ? INT_MIN : x;
}


//listener_add() function
void
f_listener_add(Arr(Var) argVars, OUT Var* returnVar) {
   Listener* lnr;
   Book* book = curBook;

   Callback callback = get_callback(&argVars[0]);
   if (callback.name == NULL)
      return;

   if (argVars[1].tag != VAR_UNKNOWN) {
      book = evGetBookArg(&argVars[1]);
      if (book) {
          evFreeCallback(&callback);
          return;
      }
   }

   lnr = ALLOC_CLEAR_ONE(Listener);
   if (!lnr) {
      evFreeCallback(&callback);
      return;
   }
   lnr->next = book->listener;
   book->listener = lnr;

   set_callback(&lnr->callback, &callback);
   if (callback.needsFreeing)
      eeglFree(callback.name);

   lnr->id = ++next_listener_id;
   returnVar->number = lnr->id;
}

void
f_listener_flush(Arr(Var) argVars, OUT Var* returnVar UNUSED) {
   Book* book = curBook;

   if (argVars[0].tag != VAR_UNKNOWN) {
      book = evGetBookArg(&argVars[0]);
      if (book == NULL)
         return;
   }
   invoke_listeners(book);
}


private void
remove_listener(Book* book, Listener *lnr, Listener *prev) {
   if (prev)
      prev->next = lnr->next;
   else
      book->listener = lnr->next;
   evFreeCallback(&lnr->callback);
   eeglFree(lnr);
}

void
f_listener_remove(Arr(Var) argVars, OUT Var* returnVar) {
   Listener* lnr;
   Listener* next;
   Listener* prev;
   Book* book;

   int id = tv_get_number(argVars);
   FOR_ALL_BOOKS(book) {
      prev = NULL;
      for (lnr = book->listener; lnr != NULL; lnr = next) {
         next = lnr->next;
         if (lnr->id == id) {
            if (textlock > 0) {
               // in invoke_listeners(), clear ID and delete later
               lnr->id = 0;
               return;
            }
            remove_listener(book, lnr, prev);
            returnVar->number = 1;
            return;
         }
         prev = lnr;
      }
   }
}

//Called before inserting a line above "lnum"/"lnum3" or deleting line "lnum" to "lnume".
void
may_invoke_listeners(Book* book, LineNr lnum, LineNr lnume, int added) {
   checkRecordedChanges(book, lnum, lnume, added);
}

//Called when a sequence of changes is done: invoke listeners added with listener_add().
void
invoke_listeners(Book* book) {
   Listener   *lnr;
   Var   returnVar;
   Var   argv[6];
   ListItem   *li;
   LineNr   start = MAXLNUM;
   LineNr   end = 0;
   LineNr   added = 0;
   int      save_updating_screen = updating_screen;
   static int   recursive = FALSE;
   Listener   *next;
   Listener   *prev;

   if (book->recordedChanges == NULL  // nothing changed
          || book->listener == NULL   // no listeners
          || recursive)       // already busy
      return;
   recursive = TRUE;

   // Block messages on channels from being handled, so that they don't make
   // text changes here.
   ++updating_screen;

   argv[0].tag = VAR_NUMBER;
   argv[0].number = book->fiNum; // a:bufnr

   FOR_ALL_LIST_ITEMS(book->recordedChanges, li) {
      Long lnum = bagGetNumber(li->c.bag, tConst("lnum"));
      if (start > lnum)
         start = lnum;
      lnum = bagGetNumber(li->c.bag, tConst("end"));
      if (end < lnum)
         end = lnum;
      added += bagGetNumber(li->c.bag, tConst("added"));
   }
   argv[1].tag = VAR_NUMBER;
   argv[1].number = start;
   argv[2].tag = VAR_NUMBER;
   argv[2].number = end;
   argv[3].tag = VAR_NUMBER;
   argv[3].number = added;

   argv[4].tag = VAR_LIST;
   argv[4].list = book->recordedChanges;
   ++textlock;

   for (lnr = book->listener; lnr != NULL; lnr = lnr->next) {
      call_callback(&lnr->callback, -1, &returnVar, 5, argv);
      clearVar(&returnVar);
   }

   // If f_listener_remove() was called may have to remove a listener now.
   prev = NULL;
   for (lnr = book->listener; lnr != NULL; lnr = next) {
      next = lnr->next;
      if (lnr->id == 0)
         remove_listener(book, lnr, prev);
      else
         prev = lnr;
   }

    --textlock;
    list_unref(book->recordedChanges);
    book->recordedChanges = NULL;

   if (save_updating_screen)
      updating_screen = TRUE;
   else
      after_updating_screen(TRUE);
   recursive = FALSE;
}

//Remove all listeners associated with "book".
void
remove_listeners(Book* book) {
   Listener* next;
   for (Listener* lnr = book->listener; lnr != NULL; lnr = next) {
      next = lnr->next;
      evFreeCallback(&lnr->callback);
      eeglFree(lnr);
   }
   book->listener = NULL;
}

//Common code for when a change was made. See changed_lines() for the arguments.
//Careful: may trigger autocommands that reload the book.
private void
changed_common(
   LineNr   lnum,
   ColNr   col,
   LineNr   lnume,
   long   xtra
){
   Portal   *po;
   Tab   *tp;
   int      i;
   int      cols;
   Pos   *p;
   int      add;

   // mark the book as modified
   changed();

   may_record_change(lnum, col, lnume, xtra);
   if (curPor->o.diff && diff_internal()) {
      curtab->diff_update = TRUE;
      diff_update_line(lnum);
   }

   // set the '. mark
   if ((commModifierG.cmod_flags & CMOD_KEEPJUMPS) == 0) {
      curBook->lastChange.lnum = lnum;
      curBook->lastChange.col = col;

      // Create a new entry if a new undo-able change was started or we
      // don't have an entry yet.
      if (curBook->newChange || curBook->changeListLen == 0) {
         if (curBook->changeListLen == 0)
            add = TRUE;
         else {
            // Don't create a new entry when the line number is the same
            // as the last one and the column is not too far away.  Avoids
            // creating many entries for typing "xxxxx".
            p = &curBook->changeList[curBook->changeListLen - 1];
            if (p->lnum != lnum)
               add = TRUE;
            else {
               cols = comp_textwidth(FALSE);
               if (cols == 0)
                  cols = 79;
               add = (p->col + cols < col || col + cols < p->col);
            }
         }
         if (add) {
            // This is the first of a new sequence of undo-able changes and it's at some distance 
            // of the last change. Use a new position in the changelist.
            curBook->newChange = false;

            if (curBook->changeListLen == JUMPLISTSIZE) {
               // changelist is full: remove oldest entry
               curBook->changeListLen = JUMPLISTSIZE - 1;
               mch_memmove(curBook->changeList, curBook->changeList + 1,
                       sizeof(Pos) * (JUMPLISTSIZE - 1));
               FOR_ALL_TAB_PORTALS(tp, po) {
                  // Correct position in changelist for other portals into this book.
                  if (po->book == curBook && po->changeListInd > 0)
                      --po->changeListInd;
               }
            }
            FOR_ALL_TAB_PORTALS(tp, po) {
                // For other portals, if the position in the changelist is
                // at the end it stays at the end.
                if (po->book == curBook && po->changeListInd == (int)curBook->changeListLen)
               ++po->changeListInd;
            }
            ++curBook->changeListLen;
         }
      }
      curBook->changeList[curBook->changeListLen - 1] = curBook->lastChange;
      //The current portal is always after the last change, so that "g," takes you back to it.
      curPor->changeListInd = curBook->changeListLen;
   }

   if (VIsual_active)
      check_visual_pos();

   FOR_ALL_TAB_PORTALS(tp, po) {
      if (po->book == curBook) {
         LineNr last = lnume + xtra - 1;  // last line after the change

         // Mark this portal to be redrawn later.
         if (!redraw_not_allowed && po->redrawType < UPD_VALID)
            po->redrawType = UPD_VALID;

         // When inserting/deleting lines and the portal has specific lines
         // to be redrawn, redrawTop and redrawBott may now be invalid,
         // so just redraw everything.
         if (xtra != 0 && po->redrawTop != 0)
            redrawPortLater(po, UPD_NOT_VALID);

         // Reset "skipCol" if the topline length has become smaller to
         // such a degree that nothing will be visible anymore, accounting
         // for 'smoothscroll' <<< or 'listchars' "precedes" marker.
         if (po->skipCol > 0
            && (last < po->topLine
                || (po->topLine >= lnum
                      && po->topLine < lnume
                      && linetabsize_eol(po, po->topLine) <= po->skipCol + sms_marker_overlap(po, -1)
                   )
               )
         ) {
            po->skipCol = 0;
         } 

         //Check if a change in the book has invalidated the cached values for the cursor.
         //Update the folds for this portal.  Can't postpone this, because
         //a following operator might work on the whole fold: ">>dd".
         foldUpdate(po, lnum, last);

         //The change may cause lines above or below the change to become
         //included in a fold.  Set lnum/lnume to the first/last line that
         //might be displayed differently.
         //Set isCursorLineFolded here as an efficient way to update it when
         //inserting lines just above a closed fold.
         i = getFoldsPortal(po, lnum, &lnum, NULL, FALSE, NULL);
         if (po->cursor.lnum == lnum) {
            po->isCursorLineFolded = i;
         }
         i = getFoldsPortal(po, last, NULL, &last, FALSE, NULL);
         if (po->cursor.lnum == last) {
            po->isCursorLineFolded = i;
         }

         // If the changed line is in a range of previously folded lines,
         // compare with the first line in that range.
         if (po->cursor.lnum <= lnum) {
            i = find_wl_entry(po, lnum);
            if (i >= 0 && po->cursor.lnum > po->lines[i].bookLnum) {
                changed_line_abv_curs_win(po);
            }
         }
         if (po->cursor.lnum > lnum)
            changed_line_abv_curs_win(po);
         ei (po->cursor.lnum == lnum && po->cursor.col >= col)
            changed_cline_bef_curs_win(po);
         if (po->bottomLine >= lnum) {
            if (xtra < 0) {
               invalidate_botline_win(po);
            } else {
                // Assume that botline doesn't change (inserted lines make
                // other lines scroll down below botline).
                approximate_botline_win(po);
            }
         }

         // Check if any lines[] entries have become invalid.
         // For entries below the change: Correct the lnums for
         // inserted/deleted lines.  Makes it possible to stop displaying
         // after the change.
         for (i = 0; i < po->validLines; ++i) {
            if (po->lines[i].isValid) {
                if (po->lines[i].bookLnum >= lnum) {
               // Do not change bookLnum at index zero, it is used to
               // compare with topLine.  Invalidate it instead.
               if (po->lines[i].bookLnum < lnume || i == 0) {
                   // line included in change
                   po->lines[i].isValid = FALSE;
               } ei (xtra != 0) {
                   // line below change
                   po->lines[i].bookLnum += xtra;
                   po->lines[i].lastBookLnum += xtra;
               }
                } ei (po->lines[i].lastBookLnum >= lnum) {
               // change somewhere inside this range of folded lines,
               // may need to be redrawn
               po->lines[i].isValid = FALSE;
               }
            }
         }
         // Take care of side effects for setting topLine when folds have
         // changed.  Esp. when the book was changed in another portal.
         if (hasAnyFolding(po)) {
            set_topline(po, po->topLine);
         }
         // If lines have been added or removed, relative numbering always
         // requires an update even if cursor didn't move.
         if (po->o.relativeNumber && xtra != 0) {
            po->lastCursorLnumRnu = 0;
         }

         if (po->o.cursorLine && po->lastCursorLine >= lnum) {
            if (po->lastCursorLine < lnume)
               // If 'cursorline' was inside the change, it has already
               // been invalidated in lines[] by the loop above.
               po->lastCursorLine = 0;
            else
               // If 'cursorline' was below the change, adjust its lnum.
               po->lastCursorLine += xtra;
         }
      }
      if (po == curPor && xtra != 0 && searchLastLnumG >= lnum)
         searchLastLnumG += xtra;
   }

   // Call drawUpdateScreen() later, which checks out what needs to be redrawn,
   // since it notices needsRedraw and then uses b_mod_*.
   set_must_redraw(UPD_VALID);

   // when the cursor line is changed always trigger CursorMoved
   if (lnum <= curPor->cursor.lnum && lnume + (xtra < 0 ? -xtra : xtra) > curPor->cursor.lnum)
      last_cursormoved.lnum = 0;
}

private void
changedOneline(Book* book, LineNr lnum) {
   if (book->needsRedraw) {
      // find the maximum area that must be redisplayed
      if (lnum < book->needsRedrawTop)
          book->needsRedrawTop = lnum;
      ei (lnum >= book->needsRedrawBott)
          book->needsRedrawBott = lnum + 1;
   } else {
      // set the area that must be redisplayed to one line
      book->needsRedraw = TRUE;
      book->needsRedrawTop = lnum;
      book->needsRedrawBott = lnum + 1;
      book->lineCountDiff = 0;
   }
}

//Changed bytes within a single line for the current book.
//- mark the portals into this book to be redisplayed
//- mark the book changed by calling changed()
//- invalidates cached values
//Careful: may trigger autocommands that reload the book.
void
changed_bytes(LineNr lnum, ColNr col) {
   changedOneline(curBook, lnum);
   changed_common(lnum, col, lnum + 1, 0L);

   // Diff hiliting in other diff portals may need to be updated too.
   if (curPor->o.diff) {
      Portal* po;
      FOR_ALL_PORTALS(po) {
         if (po->o.diff && po != curPor) {
            redrawPortLater(po, UPD_VALID);
            LineNr poLnum = diff_lnum_win(lnum, po);
            if (poLnum > 0)
               changedOneline(po->book, poLnum);
         }
      } 
   }
}

//Like changed_bytes() but also adjust text properties for "added" bytes.
//When "added" is negative text was deleted.
void
inserted_bytes(LineNr lnum, ColNr col, int added UNUSED) {
   if (curBook->hasTextprop && added != 0)
      adjustPropColumns(lnum, col, added, 0);

   changed_bytes(lnum, col);
}

//Appended "count" lines below line "lnum" in the current book.
//Must be called AFTER the change and after mark_adjust().
//Takes care of marking the book to be redrawn and sets the changed flag.
void
appended_lines(LineNr lnum, long count) {
   changed_lines(lnum + 1, 0, lnum + 1, count);
}

//Like appended_lines(), but adjust marks first.
void
appended_lines_mark(LineNr lnum, long count) {
   mark_adjust(lnum + 1, (LineNr)MAXLNUM, count, 0L);
   changed_lines(lnum + 1, 0, lnum + 1, count);
}

//Deleted "count" lines at line "lnum" in the current book.
//Must be called AFTER the change and after mark_adjust().
//Take care of marking the book to be redrawn and sets the changed flag.
void
deleted_lines(LineNr lnum, long count) {
   changed_lines(lnum, 0, lnum + count, -count);
}

//Like deleted_lines(), but adjust marks first.
//Make sure the cursor is on a valid line before calling, a GUI callback may
//be triggered to display the cursor.
void
deleted_lines_mark(LineNr lnum, long count) {
   mark_adjust(lnum, (LineNr)(lnum + count - 1), (long)MAXLNUM, -count);
   changed_lines(lnum, 0, lnum + count, -count);
}

//Mark the area to be redrawn after a change.
//Consider also calling normInvalidateDisplayOfChangedBookLine().
void
changed_lines_buf(
   Book* book,
   LineNr lnum,       // first line with change
   LineNr lnume,       // line below last changed line
   long xtra       // number of extra lines (negative when deleting)
){
   if (book->needsRedraw) {
      // find the maximum area that must be redisplayed
      if (lnum < book->needsRedrawTop)
         book->needsRedrawTop = lnum;
      if (lnum < book->needsRedrawBott) {
         // adjust old bot position for xtra lines
         book->needsRedrawBott += xtra;
         if (book->needsRedrawBott < lnum)
            book->needsRedrawBott = lnum;
      }
      if (lnume + xtra > book->needsRedrawBott)
         book->needsRedrawBott = lnume + xtra;
      book->lineCountDiff += xtra;
   } else {
      // set the area that must be redisplayed
      book->needsRedraw = TRUE;
      book->needsRedrawTop = lnum;
      book->needsRedrawBott = lnume + xtra;
      book->lineCountDiff = xtra;
   }
}

//Changed lines for the current book.
//Must be called AFTER the change and after mark_adjust().
//- mark the book changed by calling changed()
//- mark the portals on this book to be redisplayed
//- invalidate cached values
//"lnum" is the first line that needs displaying, "lnume" the first line below the changed lines 
//(BEFORE the change). When only inserting lines, "lnum" and "lnume" are equal.
//Takes care of calling changed() and updating b_mod_*.
//Careful: may trigger autocommands that reload the book.
void
changed_lines(
   LineNr lnum,    // first line with change
   ColNr col,      // column in first line with change
   LineNr lnume,   // line below last changed line
   long xtra       // number of extra lines (negative when deleting)
){
   changed_lines_buf(curBook, lnum, lnume, xtra);

   if (xtra == 0 && curPor->o.diff && !diff_internal()) {
      // When the number of lines doesn't change then mark_adjust() isn't
      // called and other diff books still need to be marked for displaying.
      Portal       *po;
      FOR_ALL_PORTALS(po) {
         if (po->o.diff && po != curPor) {
            redrawPortLater(po, UPD_VALID);
            LineNr poLnum = diff_lnum_win(lnum, po);
            if (poLnum > 0)
               changed_lines_buf(po->book, poLnum, lnume - lnum + poLnum, 0L);
         }
      } 
   }

   changed_common(lnum, col, lnume, xtra);
}

// Called when the changed flag must be reset for book "book". When "always_inc_changedtick" is 
// TRUE b:changedtick is incremented also when the changed flag was off.
void
unchanged(Book* book, int always_inc_changedtick) {
   if (book->wasModified) {
      book->wasModified = false;
      ml_setflags(book);
      check_status(book);
      needRedrawTabpanelG = TRUE;
      ++CHANGEDTICK(book);
   } ei (always_inc_changedtick)
      ++CHANGEDTICK(book);
}

//Insert string "p" at the cursor position.  Stops at a ZERO.
//Handles Replace mode and multi-byte characters.
void
ins_bytes(CS p) {
   ins_bytes_len(p, (int)STRLEN(p));
}

//Insert string "p" with length "len" at the cursor position.
//Handle Replace mode and multi-byte characters.
void
ins_bytes_len(CS p, int len) {
   int n;
   for (int i = 0; i < len; i += n) {
      // avoid reading past p[len]
      n = utfCharLen_len(p + i, len - i);
      opInsertCharBytes(p + i, n, false);
   }
}

private void
insertOrReplaceChar(Unt c, Boole replace) {
   Byte buf[MB_MAXBYTES + 1];
   int charLen = mb_char2bytes(c, buf);
   // When "c" is 0x100, 0x200, etc. we don't want to insert a ZERO. Happens for CTRL-Vu9900.
   if (buf[0] == 0)
      buf[0] = '\n';

   opInsertCharBytes(buf, charLen, replace);
}

//Insert a single character at the cursor position.
//Caller must have prepared for undo.
//For multi-byte characters we get the whole character, the caller must convert bytes to character
void
insertChar(Unt c) {
   insertOrReplaceChar(c, false);
}

//Replace a single character at the cursor position. Caller must have prepared for undo.
//For multi-byte characters we get the whole character, the caller must convert bytes to character
void
replaceChar(Unt c) {
   insertOrReplaceChar(c, true);
}

void
opInsertCharBytes(CS targetLine, int charlen, Boole replace) {
   LineNr   lnum = curPor->cursor.lnum;

   // Break tabs if needed.
   if (virtual_active() && curPor->cursor.coladd > 0)
      coladvance_force(getviscol());

   ColNr col = curPor->cursor.col;
   CS oldp = ml_get(lnum);
   int oldLineLen = (int)ml_get_len(lnum) + 1;// length of old line including ZERO

   // The lengths default to the values for when not replacing.
   int oldCharLen = replace ? utfCharLen(oldp + col) : 0; // nr of bytes deleted (0 when not replacing)
   int newCharLen = charlen; // nr of bytes inserted

   CS newp = alloc(oldLineLen + newCharLen - oldCharLen);

   // Copy bytes before the cursor.
   if (col > 0)
      mch_memmove(newp, oldp, (Unt)col);

   // Copy bytes after the changed character(s).
   CS p = newp + col;
   if (oldLineLen > col + oldCharLen)
      mch_memmove(p + newCharLen, oldp + col + oldCharLen, (Unt)(oldLineLen - col - oldCharLen));

   // Insert or overwrite the new character.
   mch_memmove(p, targetLine, charlen);
   int i = charlen;

   // Fill with spaces when necessary.
   while (i < newCharLen) {
      p[i++] = ' ';
   }

   // Replace the line in the book.
   ml_replace(lnum, newp, FALSE);

   // mark the book as changed and prepare for displaying
   changed_bytes(lnum, col);
   if (curBook->hasTextprop && newCharLen != oldCharLen) {
      adjustPropColumns(lnum, col, newCharLen - oldCharLen, 0);
   }

   // Normal insert: move cursor right
   curPor->cursor.col += charlen;

   // TODO: should try to update w_row here, to avoid recomputing it later.
}

//Insert a string at the cursor position. Note: Does NOT handle Replace mode.
//Caller must have prepared for undo.
void
ins_str(CS s, Unt slen) {
   LineNr lnum = curPor->cursor.lnum;

   if (virtual_active() && curPor->cursor.coladd > 0)
      coladvance_force(getviscol());

   ColNr col = curPor->cursor.col;
   CS oldp = ml_get(lnum);
   int oldlen = (int)ml_get_len(lnum);

   CS newp = alloc(oldlen + slen + 1);
   if (col > 0)
      mch_memmove(newp, oldp, (Unt)col);
   mch_memmove(newp + col, s, slen);
   mch_memmove(newp + col + slen, oldp + col, (Unt)(oldlen - col + 1));
   ml_replace(lnum, newp, FALSE);
   inserted_bytes(lnum, col, (int)slen);
   curPor->cursor.col += (ColNr)slen;
}

//Delete one character under the cursor.
//If "fixpos" is TRUE, don't leave the cursor on the ZERO after the line.
//Caller must have prepared for undo.
//
//return FAIL for failure, OK otherwise
int
del_char(Boole fixpos) {
   // Make sure the cursor is at the start of a character.
   mb_adjust_cursor();
   if (*ml_get_cursor() == ZERO)
      return FAIL;
   return del_chars(1L, fixpos);
}

//Like del_bytes(), but delete characters instead of bytes.
int
del_chars(long count, Boole fixpos) {
   long   bytes = 0;
   CS p = ml_get_cursor();
   for (int i = 0; i < count && *p != ZERO; ++i)     {
      int l = utfCharLen(p);
      bytes += l;
      p += l;
   }
   return del_bytes(bytes, fixpos, TRUE);
}

//Delete "count" bytes under the cursor.
//If "fixpos" is TRUE, don't leave the cursor on the ZERO after the line.
//Caller must have prepared for undo.
//
//Return FAIL for failure, OK otherwise.
int
del_bytes(
   long   count,
   Boole      fixpos_arg,
   int      use_delcombine UNUSED)       // 'delcombine' option applies
{
   ColNr   oldlen;
   ColNr   newlen;
   LineNr   lnum = curPor->cursor.lnum;
   ColNr   col = curPor->cursor.col;
   int      alloc_newp;
   long   movelen;
   int      fixpos = fixpos_arg;

   CS oldp = ml_get(lnum);
   oldlen = (int)ml_get_len(lnum);

   // Can't do anything when the cursor is on the ZERO after the line.
   if (col >= oldlen)
      return FAIL;

   // If "count" is zero there is nothing to do.
   if (count == 0)
      return OK;

   // If "count" is negative the caller must be doing something wrong.
   if (count < 1) {
      internalErrFmtMsg(e_invalid_count_for_del_bytes_nr, count);
      return FAIL;
   }

   // If @delcombine is set and deleting (less than) one character, only
   // delete the last combining character.
   if (p_delcomb && use_delcombine && utfCharLen(oldp + col) >= count) {
      int   cc[MAX_COMBINED_SYMBOLS];
      int   n;

      (void)utfc_ptr2char(oldp + col, cc);
      if (cc[0] != ZERO) {
         // Find the last composing char, there can be several.
         n = col;
         do {
            col = n;
            count = utf_ptr2len(oldp + n);
            n += count;
         } while (UTF_COMPOSINGLIKE(oldp + col, oldp + n));
         fixpos = 0;
      }
   }

   // When count is too big, reduce it.
   movelen = (long)oldlen - (long)col - count + 1; // includes trailing ZERO
   if (movelen <= 1) {
      // If we just took off the last character of a non-blank line, and
      // fixpos is TRUE, we don't want to end up positioned at the ZERO,
      // unless "restart_edit" is set
      if (col > 0 && fixpos && restart_edit == 0) {
         --curPor->cursor.col;
         curPor->cursor.coladd = 0;
         curPor->cursor.col -= (*mb_head_off)(oldp, oldp + curPor->cursor.col);
      }
      count = oldlen - col;
      movelen = 1;
   }
   newlen = oldlen - count;

   // If the old line has been allocated the deletion can be done in the
   // existing line. Otherwise a new line has to be allocated
   alloc_newp = !ml_line_alloced();    // check if oldp was allocated
   CS newp;
   if (!alloc_newp)
      newp = oldp;             // use same allocated memory
   else {                   // need to allocate a new line
      newp = alloc(newlen + 1);
      mch_memmove(newp, oldp, (Unt)col);
   }
   mch_memmove(newp + col, oldp + col + count, (Unt)movelen);
   if (alloc_newp)
      ml_replace(lnum, newp, FALSE);
   else {
      // Also move any following text properties.
      if (oldlen + 1 < curBook->mem.lineLen)
         mch_memmove(newp + newlen + 1, oldp + oldlen + 1, (Unt)curBook->mem.lineLen - oldlen - 1);
      curBook->mem.lineLen -= count;
      curBook->mem.lineTextLen = 0;
   }

   // mark the book as changed and prepare for displaying
   inserted_bytes(lnum, col, -count);

   return OK;
}

// insertLine - simply insert a line below or above the current line. Applies autoindent
// Return OK for success, FAIL for failure
int
insertLine(
   int      dir // FORWARD or BACKWARD
){
   Pos oldCursor = curPor->cursor;
   // count white space on current line
   int newIndent = get_indent_lnum(curPor->cursor.lnum);
   if (dir == BACKWARD)
      --curPor->cursor.lnum;
   if (ml_append(curPor->cursor.lnum, NULL, (ColNr)0, FALSE) == FAIL) {
      return FAIL;
   }
   ++curPor->cursor.lnum;
    
   (void)set_indent(newIndent, SIN_INSERT);
    
   // Postpone calling changed_lines(), because it would mess up folding with markers.
   mark_adjust(curPor->cursor.lnum + 1, (LineNr)MAXLNUM, 1L, 0L);
    
   changed_lines(curPor->cursor.lnum, curPor->cursor.col, curPor->cursor.lnum + 1, 1L);
   curPor->cursor.lnum = oldCursor.lnum + 1;
   return OK;
}

//get_leader_len() returns the length in bytes of the prefix of the given string which introduces 
//a comment. If this string is not a comment then 0 is returned. When "flags" is not NULL, it is 
//set to point to the flags of the recognized comment leader.
//"backward" must be true for the "O" command.
//If "include_space" is set, include trailing whitespace while calculating the length.
int
get_leader_len(CS line, Byte** flags, int backward, int include_space) {
   if (!curBook->o.comments) {
      return 0;
   }
   
   int j;
   int got_com = FALSE;
   Boole foundOne;
   Byte   part_buf[COM_MAX_LEN];   // buffer for one option part
   CS string;      // pointer to comment string
   CS list;
   int middle_match_len = 0;
   CS preList;
   CS saved_flags = NULL;

   int i = 0;
   int result = 0;
   while (SPACE_OR_TAB(line[i]))    // leading white space is ignored
      ++i;

   //Repeat to match several nested comment strings.
   while (line[i] != ZERO) {
      //scan through the 'comments' option for a match
      foundOne = false;
      for (list = curBook->o.comments; *list != ZERO; ) {
         // Get one option part into part_buf[].  Advance "list" to next
         // one.  Put "string" at start of string.
         if (!got_com && flags)
            *flags = list;       // remember where flags started
         preList = list;
         (void)doCutPathFromListOfPaths(&list, part_buf, COM_MAX_LEN, ",");
         string = firstOccurrence(part_buf, ':');
         if (string == NULL)       // missing ':', ignore this part
            continue;
         *string++ = ZERO;       // isolate flags from string

         // If we found a middle match previously, use that match when this is not a middle or end.
         if (middle_match_len != 0
                && firstOccurrence(part_buf, COM_MIDDLE) == NULL
                && firstOccurrence(part_buf, COM_END) == NULL)
            break;

         // When we already found a nested comment, only accept further nested comments.
         if (got_com && firstOccurrence(part_buf, COM_NEST) == NULL)
            continue;

         // When 'O' flag present and using "O" command skip this one.
         if (backward && firstOccurrence(part_buf, COM_NOBACK) != NULL)
            continue;

         // Line contents and string must match.
         // When string starts with white space, must have some white space
         // (but the amount does not need to match, there might be a mix of TABs and spaces).
         if (SPACE_OR_TAB(string[0])) {
            if (i == 0 || !SPACE_OR_TAB(line[i - 1]))
               continue;  // missing white space
            while (SPACE_OR_TAB(string[0]))
               ++string;
         }
         for (j = 0; string[j] != ZERO && string[j] == line[i + j]; ++j)
            {}
         if (string[j] != ZERO)
            continue;  // string doesn't match

         // When 'b' flag set, there must be white space or an end-of-line after the string in the line
         if (firstOccurrence(part_buf, COM_BLANK) != NULL
               && !SPACE_OR_TAB(line[i + j]) && line[i + j] != ZERO)
            continue;

         // We have found a match, stop searching unless this is a middle comment. The middle comment 
         // can be a substring of the end comment in which case it's better to return the length of the
         // end comment and its flags.  Thus we keep searching with middle and end matches and use an 
         // end match if it matches better.
         if (firstOccurrence(part_buf, COM_MIDDLE) != NULL) {
            if (middle_match_len == 0) {
               middle_match_len = j;
               saved_flags = preList;
            }
            continue;
         }
         if (middle_match_len != 0 && j > middle_match_len)
            // Use this match instead of the middle match, since it's a longer thus better match.
            middle_match_len = 0;

         if (middle_match_len == 0)
            i += j;
         foundOne = true;
         break;
      }

      if (middle_match_len != 0) {
         // Use the previously found middle match after failing to find a match with an end.
         if (!got_com && flags != NULL)
            *flags = saved_flags;
         i += middle_match_len;
         foundOne = true;
      }

      // No match found, stop scanning.
      if (!foundOne)
         break;

      result = i;

      // Include any trailing white space.
      while (SPACE_OR_TAB(line[i]))
         ++i;

      if (include_space)
         result = i;

      // If this comment doesn't nest, stop here.
      got_com = TRUE;
      if (firstOccurrence(part_buf, COM_NEST) == NULL)
         break;
   }
   return result;
}


//openLine: Open a new line below the current line with an "Enter" in insert mode
//
//For MODE_VREPLACE state, we only add a new line when we get to the end of
//the file, otherwise we just start replacing the next line.
//
//Caller must take care of undo.  Since MODE_VREPLACE may affect any number of lines however, 
//it may call u_save_cursor() again when starting to change a new line.
//"flags": OPENLINE_DELSPACES   delete spaces after cursor
//      OPENLINE_DO_COM   format comments
//      OPENLINE_KEEPTRAIL   keep trailing spaces
//      OPENLINE_MARKFIX   adjust mark positions after the line break
//      OPENLINE_COM_LIST   format comments with list or 2nd line indent
//      OPENLINE_FORCE_INDENT  set indent from second_line_indent, ignore 'autoindent'
//
//"second_line_indent": indent for after ^^D in Insert mode or if flag OPENLINE_COM_LIST
//"did_do_comment" is set to TRUE when intentionally putting the comment leader in front of the 
// new line.
//
//Return OK for success, FAIL for failure
int
openLine(
   Unt flags,
   int second_line_indent
){
   CS savedLine;      // copy of the original line
   CS nextLine = NULL;   // copy of the next line
   CS transferText = NULL;   // what goes to next line
   int transferLen = 0;   // length of transferText string
   int fewerCols = 0;      // fewer columns for mark in new line
   int fewerColsOff = 0;   // columns to skip for mark and textprop adjustment
   Pos old_cursor;      // old cursor position
   int newcol = 0;      // new cursor column
   int newindent = 0;      // auto-indent of the new line
   int n;
   int shouldTruncateLine = FALSE;
   int retval = FAIL;      // return value
   int lead_len;      // length of comment leader
   int comment_start = 0;   // start index of the comment leader
   CS lead_flags;   // position in 'comments' for comment leader
   CS leader = NULL;      // copy of comment leader
   CS allocated = NULL;   // allocated memory
   CS p;
   int      saved_char = ZERO;   // init for GCC
   Pos* pos;
   int do_si = may_do_si();
   int no_si = FALSE;      // reset didSindentG afterwards
   int first_char = ZERO;   // init for GCC
   int didAppend;      // appended a new line
   int at_eol;         // cursor after last character

   //make a copy of the current line so we can mess with it
   savedLine = copySubstr(ml_get_curline(), ml_get_curline_len());

   at_eol = curPor->cursor.col >= (int)ml_get_curline_len();

   if (stateG & MODE_INSERT) {
      transferText = savedLine + curPor->cursor.col;
      if (do_si) { // need first char after new line break
          p = skipwhite(transferText);
          first_char = *p;
      }
      transferLen = (int)STRLEN(transferText);
      
      saved_char = *transferText;
      *transferText = ZERO;
   }

   u_clearline();      // cannot do "U" command when adding lines
   didSindentG = false;
   ai_col = 0;

   //If we just did an auto-indent, then we didn't type anything on
   //the prior line, and it should be truncated.  Do this even if 'ai' is not
   //set because automatically inserting a comment leader also sets didAindentG.
   if (didAindentG)
      shouldTruncateLine = TRUE;

   if ((flags & OPENLINE_FORCE_INDENT) != 0 && second_line_indent >= 0) {
      newindent = second_line_indent;
      //If 'autoindent' and/or 'smartindent' is set, try to figure out what
      //indent to use for the new line.
   } ei (curBook->o.autoIndent || do_si) {
      //count white space on current line
      newindent = get_indent_str(savedLine, (int)curBook->o.shiftWidth);
      if (newindent == 0 && (flags & OPENLINE_COM_LIST) == 0) {
          newindent = second_line_indent; // for ^^D command in insert mode
      }

      //Do smart indenting. In insert/replace mode we may move 
      //some text to the next line. If it starts with '{' don't add an indent. Fixes inserting 
      //a NL before '{' in line
      //   `if (condition) {`
      if (!shouldTruncateLine && do_si && *savedLine != ZERO 
            && (transferText == NULL || first_char != '{')) {
         Byte  last_char;

         old_cursor = curPor->cursor;
         CS ptr = savedLine;
         if ((flags & OPENLINE_DO_COM) != 0) {
             lead_len = get_leader_len(ptr, NULL, FALSE, TRUE);
         } else {
             lead_len = 0;
         }
         // Skip preprocessor directives, unless they are recognized as comments.
         if ( lead_len == 0 && ptr[0] == '#') {
            while (ptr[0] == '#' && curPor->cursor.lnum > 1)
                ptr = ml_get(--curPor->cursor.lnum);
            newindent = get_indent();
         }
         if ((flags & OPENLINE_DO_COM) != 0)
            lead_len = get_leader_len(ptr, NULL, FALSE, TRUE);
         else
            lead_len = 0;
         if (lead_len > 0) {
            // This case gets the following right:
            //       /*
            //        * A comment (read '\' as '/').
            //        */
            // #define IN_THE_WAY
            //       This should line up here;
            p = skipwhite(ptr);
            if (p[0] == '/' && p[1] == '*')
               p++;
            if (p[0] == '*') {
               for (p++; *p; p++) {
                  if (p[0] == '/' && p[-1] == '*') {
                     // End of C comment, indent should line up with the line containing 
                     // the start of the comment.
                     curPor->cursor.col = (ColNr)(p - ptr);
                     if ((pos = findmatch(NULL, ZERO)) != NULL) {
                        curPor->cursor.lnum = pos->lnum;
                        newindent = get_indent();
                        break;
                     }
                     // this may make "ptr" invalid, get it again
                     ptr = ml_get(curPor->cursor.lnum);
                     p = ptr + curPor->cursor.col;
                  }
               }
            }
         } else { // Not a comment line
            //Find last non-blank in line
            p = ptr + STRLEN(ptr) - 1;
            while (p > ptr && SPACE_OR_TAB(*p))
                --p;
            last_char = *p;

            //find the character just before the '{' or ';'
            if (last_char == '{' || last_char == ';') {
                if (p > ptr)
                  --p;
                while (p > ptr && SPACE_OR_TAB(*p)) {
                   --p;
                } 
            }
            // Try to catch lines that are split over multiple lines. eg:
            //       if (condition &&
            //         condition) {
            //      Should line up here!
            //       }
            if (*p == ')') {
               curPor->cursor.col = (ColNr)(p - ptr);
               if ((pos = findmatch(NULL, '(')) != NULL) {
                  curPor->cursor.lnum = pos->lnum;
                  newindent = get_indent();
                  ptr = ml_get_curline();
               }
            }
            //If last character is '{' do indent, without checking for "if" and the like.
            if (last_char == '{') {
               didSindentG = true;   // do indent
               no_si = TRUE;   // don't delete it when '{' typed
               //Look for "if" and the like, use 'cinwords'.
               //Don't do this if the previous line ended in ';' or '}'.
            } ei (last_char != ';' && last_char != '}' && cin_is_cinword(ptr)) {
                didSindentG = true;
            }
         }
         curPor->cursor = old_cursor;
      }
      if (do_si)
         can_si = TRUE;

      didAindentG = true;
   }

   // Find out if the current line starts with a comment leader like ` * `.
   // This may then be inserted in front of the new line.
   end_comment_pending = ZERO;
   if ((flags & OPENLINE_DO_COM) != 0) {
      lead_len = get_leader_len(savedLine, &lead_flags, FALSE, TRUE);
   } else
      lead_len = 0;
   if (lead_len > 0) {
      CS lead_repl = NULL;       // replaces comment leader
      int lead_repl_len = 0;       // length of *lead_repl
      Byte lead_middle[COM_MAX_LEN];   // middle-comment string
      Byte lead_end[COM_MAX_LEN];       // end-comment string
      CS comment_end = NULL;       // where lead_end has been found
      int extra_space = FALSE;       // append extra space
      int current_flag;
      int require_blank = FALSE;       // requires blank after middle
      CS p2;

      // If the comment leader has the start, middle or end flag, it may not
      // be used or may be replaced with the middle leader.
      for (p = lead_flags; *p && *p != ':'; ++p) {
         if (*p == COM_BLANK) {
            require_blank = TRUE;
            continue;
         }
         if (*p == COM_START || *p == COM_MIDDLE) {
            current_flag = *p;
            if (*p == COM_START) {
                // find start of middle part
                (void)doCutPathFromListOfPaths(&p, lead_middle, COM_MAX_LEN, ",");
                require_blank = FALSE;
            }

            // Isolate the strings of the middle and end leader.
            while (*p && p[-1] != ':') { // find end of middle flags
               if (*p == COM_BLANK)
                  require_blank = TRUE;
               ++p;
            }
            (void)doCutPathFromListOfPaths(&p, lead_middle, COM_MAX_LEN, ",");

            while (*p && p[-1] != ':') {// find end of end flags
               // Check whether we allow automatic ending of comments
               if (*p == COM_AUTO_END)
                  end_comment_pending = UNT; // means we want to set it
               ++p;
            }
            n = doCutPathFromListOfPaths(&p, lead_end, COM_MAX_LEN, ",");

            if (end_comment_pending == UNT)   // we can set it now
               end_comment_pending = lead_end[n - 1];

            // If the end of the comment is in the same line, don't use the comment leader.
            for (p = savedLine + lead_len; *p; ++p) {
               if (STRNCMP(p, lead_end, n) == 0) {
                  comment_end = p;
                  lead_len = 0;
                  break;
               }
            }

            // Doing "o" on a start of comment inserts the middle leader.
            if (lead_len > 0) {
                if (current_flag == COM_START) {
                  lead_repl = lead_middle;
                  lead_repl_len = (int)STRLEN(lead_middle);
                }

                // If we have hit RETURN immediately after the start comment leader, then put 
                // a space after the middle comment leader on the next line.
                if (!SPACE_OR_TAB(savedLine[lead_len - 1])
                   && ((transferText != NULL && (int)curPor->cursor.col == lead_len)
                  || (transferText == NULL && savedLine[lead_len] == ZERO)
                  || require_blank))
               extra_space = TRUE;
            }
            break;
         }
         if (*p == COM_END) {
            //Doing "o" on the end of a comment does not insert leader. Remember where the end is,
            //might want to use it to find the start (for C-comments).
            comment_end = skipwhite(savedLine);
            lead_len = 0;
            break;

            // Doing "O" on the end of a comment inserts the middle leader.
            // Find the string for the middle leader, searching backwards.
            while (p > curBook->o.comments && *p != ',')
                --p;
            for (
               lead_repl = p; lead_repl > curBook->o.comments && lead_repl[-1] != ':'; --lead_repl
            ) {} 
            lead_repl_len = (int)(p - lead_repl);

            // We can probably always add an extra space when doing "O" on the comment-end
            extra_space = TRUE;

            // Check whether we allow automatic ending of comments
            for (p2 = p; *p2 && *p2 != ':'; p2++) {
                if (*p2 == COM_AUTO_END)
               end_comment_pending = UNT; // means we want to set it
            }
            if (end_comment_pending == UNT) {
               // Find last character in end-comment string
               while (*p2 && *p2 != ',')
                  p2++;
               end_comment_pending = p2[-1];
            }
            break;
         }
         if (*p == COM_FIRST) {
            // Comment leader for first line only:   Don't repeat leader
            // when using "O", blank out leader when using "o".
            lead_repl = (CS)"";
            lead_repl_len = 0;
            break;
         }
      }
      if (lead_len) {
         // allocate buffer (may concatenate transferText later)
         leader = alloc(lead_len + lead_repl_len + extra_space + transferLen
              + (second_line_indent > 0 ? second_line_indent : 0) + 1);
         allocated = leader;          // remember to free it later

         if (!leader)
            lead_len = 0;
         else {
            copySubstrToAllocation(leader, (Text){savedLine, lead_len});

            // TODO: handle multi-byte and double width chars
            for (int li = 0; li < comment_start; ++li) {
               if (!SPACE_OR_TAB(leader[li]))
                  leader[li] = ' ';
            } 

            // Replace leader with lead_repl, right or left adjusted
            if (lead_repl != NULL) {
               int      c = 0;
               int      off = 0;

               for (p = lead_flags; *p != ZERO && *p != ':'; ) {
                  if (*p == COM_RIGHT || *p == COM_LEFT)
                     c = *p++;
                  ei (EE_ISDIGIT(*p) || *p == '-')
                     off = parseLong(&p);
                  else
                     ++p;
               }
               if (c == COM_RIGHT) {   // right adjusted leader
                  // find last non-white in the leader to line up with
                  for (p = leader + lead_len - 1; p > leader && SPACE_OR_TAB(*p); --p)
                      {} 
                  ++p;

                  // Compute the length of the replaced characters in
                  // screen characters, not bytes.
                  {
                     int       repl_size = eeglStrNsize(lead_repl, lead_repl_len);
                     int       old_size = 0;
                     Byte  *endp = p;
                     int       l;

                     while (old_size < repl_size && p > leader) {
                        MB_PTR_BACK(leader, p);
                        old_size += ptr2cells(p);
                     }
                     l = lead_repl_len - (int)(endp - p);
                     if (l != 0)
                        mch_memmove(endp + l, endp, (Unt)((leader + lead_len) - endp));
                     lead_len += l;
                  }
                  mch_memmove(p, lead_repl, (Unt)lead_repl_len);
                  if (p + lead_repl_len > leader + lead_len)
                     p[lead_repl_len] = ZERO;

                  // blank-out any other chars from the old leader.
                  while (--p >= leader) {
                     int l = mb_head_off(leader, p);

                     if (l > 1) {
                        p -= l;
                        if (ptr2cells(p) > 1) {
                           p[1] = ' ';
                           --l;
                        }
                        mch_memmove(p + 1, p + l + 1, (Unt)((leader + lead_len) - (p + l + 1)));
                        lead_len -= l;
                        *p = ' ';
                     } ei (!SPACE_OR_TAB(*p))
                        *p = ' ';
                  }
               } else { // left adjusted leader
                  p = skipwhite(leader);

                  // Compute the length of the replaced characters in
                  // screen characters, not bytes. Move the part that is not to be overwritten.
                  {
                  int       repl_size = eeglStrNsize(lead_repl,
                       lead_repl_len);
                  int       i;
                  int       l;

                  for (i = 0; i < lead_len && p[i] != ZERO; i += l) {
                     l = utfCharLen(p + i);
                     if (eeglStrNsize(p, i + l) > repl_size)
                        break;
                  }
                  if (i != lead_repl_len) {
                     mch_memmove(
                        p + lead_repl_len, p + i, (Unt)(lead_len - i - (p - leader))
                     );
                     lead_len += lead_repl_len - i;
                  }
                  }
                  mch_memmove(p, lead_repl, (Unt)lead_repl_len);

                  // Replace any remaining non-white chars in the old leader by spaces. 
                  // Keep Tabs, the indent must remain the same.
                  for (p += lead_repl_len; p < leader + lead_len; ++p) {
                     if (!SPACE_OR_TAB(*p)) {
                        // Don't put a space before a TAB.
                        if (p + 1 < leader + lead_len && p[1] == TAB) {
                           --lead_len;
                           mch_memmove(p, p + 1, (leader + lead_len) - p);
                       } else {
                           int l = utfCharLen(p);

                           if (l > 1) {
                              if (ptr2cells(p) > 1) {
                                // Replace a double-wide char with
                                // two spaces
                                --l;
                                *p++ = ' ';
                             }
                             mch_memmove(p + 1, p + l, (leader + lead_len) - p);
                             lead_len -= l - 1;
                           }
                           *p = ' ';
                        }
                     }
                   }
                   *p = ZERO;
                }

                // Recompute the indent, it may have changed.
                if (curBook->o.autoIndent || do_si)
                   newindent = get_indent_str(leader, (int)curBook->o.shiftWidth);

                // Add the indent offset
                if (newindent + off < 0) {
                   off = -newindent;
                   newindent = 0;
                } else {
                   newindent += off;
                }

                // Correct trailing spaces for the shift, so that alignment remains equal
                while (off > 0 && lead_len > 0 && leader[lead_len - 1] == ' ') {
                   // Don't do it when there is a tab before the space
                   if (firstOccurrence(skipwhite(leader), '\t') != NULL)
                      break;
                    --lead_len;
                    --off;
                }

                // If the leader ends in white space, don't add an extra space
                if (lead_len > 0 && SPACE_OR_TAB(leader[lead_len - 1]))
                   extra_space = FALSE;
                leader[lead_len] = ZERO;
            }

            if (extra_space) {
               leader[lead_len++] = ' ';
               leader[lead_len] = ZERO;
            }

            newcol = lead_len;

            // if a new indent will be set below, remove the indent in the comment leader
            if (newindent || didSindentG) {
               while (lead_len && SPACE_OR_TAB(*leader)) {
                  --lead_len;
                  --newcol;
                  ++leader;
               }
            }
         }
         didSindentG = can_si = false;
      } ei (comment_end) {
         //We have finished a comment, so we don't use the leader. If this was a C-comment 
         //and 'ai' or 'si' is set do a normal indent to align with the line containing the 
         //start of the comment.
         if (comment_end[0] == '*' && comment_end[1] == '/' && (curBook->o.autoIndent || do_si)) {
            old_cursor = curPor->cursor;
            curPor->cursor.col = (ColNr)(comment_end - savedLine);
            if ((pos = findmatch(NULL, ZERO)) != NULL) {
                curPor->cursor.lnum = pos->lnum;
                newindent = get_indent();
            }
            curPor->cursor = old_cursor;
         }
      }
   }

   // (stateG == MODE_INSERT)
   if (transferText)     {
      *transferText = saved_char;      // restore char that ZERO replaced

      // When 'ai' set or "flags" has OPENLINE_DELSPACES, skip to the first non-blank.
      if (curBook->o.autoIndent || (flags & OPENLINE_DELSPACES)) {
         while ((*transferText == ' ' || *transferText == '\t')
             && (!utf_iscomposing(mb_ptr2char(transferText + 1)))
         ){
            ++transferText;
            ++fewerColsOff;
         }
      }

      // columns for marks adjusted for removed columns
      fewerCols = (int)(transferText - savedLine);
   } else {
      transferText = S"";          // append empty line
   }
    
   // concatenate leader and transferText, if there is a leader
   if (lead_len) {
      if ((flags & OPENLINE_COM_LIST) != 0 && second_line_indent > 0) {
         int i;
         int padding = second_line_indent  - (newindent + (int)STRLEN(leader));

         // Here whitespace is inserted after the comment char.
         // Below, set_indent(newindent, SIN_INSERT) will insert the
         // whitespace needed before the comment char.
         for (i = 0; i < padding; i++) {
            STRCAT(leader, " ");
            fewerCols--;
            newcol++;
         }
      }
      STRCAT(leader, transferText);
      transferText = leader;
      didAindentG = true;       // So truncating blanks works with comments
      fewerCols -= lead_len;
   } else
      end_comment_pending = ZERO;  // turns out there was no leader

   old_cursor = curPor->cursor;
   if (ml_append(curPor->cursor.lnum, transferText, (ColNr)transferLen, FALSE) == FAIL)
      goto theend;
   // Postpone calling changed_lines(), because it would mess up folding with markers.
   mark_adjust(curPor->cursor.lnum + 1, (LineNr)MAXLNUM, 1L, 0L);
   didAppend = TRUE;
   if (stateG & MODE_INSERT) {
      // Properties after the split move to the next line.
      adjustPropsForSplit(curPor->cursor.lnum, curPor->cursor.lnum,
          curPor->cursor.col + 1, 0, at_eol);
   }

   if (newindent || didSindentG) {
      ++curPor->cursor.lnum;
      if (didSindentG) {
         int sw = (int)get_sw_value(curBook);

         newindent -= newindent % sw;
         newindent += sw;
      }
      // Copy the indent
      (void)set_indent(newindent, SIN_INSERT);
      fewerCols -= curPor->cursor.col;

      ai_col = curPor->cursor.col;

      newcol += curPor->cursor.col;
     if (no_si)
          didSindentG = false;
   }

   curPor->cursor = old_cursor;

   if (shouldTruncateLine || (stateG & MODE_INSERT)) {
      // truncate current line at cursor
      savedLine[curPor->cursor.col] = ZERO;
      // Remove trailing white space, unless OPENLINE_KEEPTRAIL used.
      if (shouldTruncateLine && (flags & OPENLINE_KEEPTRAIL) == 0) {
         truncate_spaces(savedLine, curPor->cursor.col);
      }
      ml_replace(curPor->cursor.lnum, savedLine, FALSE);
      savedLine = NULL;
      if (didAppend) {
          changed_lines(
             curPor->cursor.lnum, curPor->cursor.col, curPor->cursor.lnum + 1, 1L
          );
          didAppend = FALSE;

          // Move marks after the line break to the new line.
          if ((flags & OPENLINE_MARKFIX) != 0) {
             mark_col_adjust(curPor->cursor.lnum, curPor->cursor.col + fewerColsOff,
                1L, (long)-fewerCols, 0
             );
          }
          // Keep into account the deleted blanks on the new line.
          if (curBook->hasTextprop && fewerColsOff != 0) {
             adjustPropColumns(curPor->cursor.lnum + 1, 0, -fewerColsOff, 0);
          }
      } else {
            changed_bytes(curPor->cursor.lnum, curPor->cursor.col);
         }
      }

      // Put the cursor on the new line.  Careful: the scrollup() above may
      // have moved cursor, we must use old_cursor.
      curPor->cursor.lnum = old_cursor.lnum + 1;
   if (didAppend)
      changed_lines(curPor->cursor.lnum, 0, curPor->cursor.lnum, 1L);

   curPor->cursor.col = newcol;
   curPor->cursor.coladd = 0;

   retval = OK;      // success!
theend:
   eeglFree(savedLine);
   eeglFree(nextLine);
   eeglFree(allocated);
   return retval;
}

//Delete from cursor to end of line. Caller must have prepared for undo.
//If "fixpos" is TRUE fix the cursor position when done.
//
//Return FAIL for failure, OK otherwise.
int
truncate_line(int fixpos) {
   LineNr   lnum = curPor->cursor.lnum;
   ColNr   col = curPor->cursor.col;

   CS old_line = ml_get(lnum);
   CS newp = (col == 0) ? copyStr(Em) : copySubstr(old_line, col);
   int deleted = (int)ml_get_len(lnum) - col;
   ml_replace(lnum, newp, FALSE);

   // mark the book as changed and prepare for displaying
   inserted_bytes(lnum, curPor->cursor.col, -deleted);

   // If "fixpos" is TRUE we don't want to end up positioned at the ZERO.
   if (fixpos && curPor->cursor.col > 0)
      --curPor->cursor.col;

   return OK;
}

//Delete "nlines" lines at the cursor. Saves the lines for undo first if "undo" is TRUE.
void
del_lines(long nlines,   int undo) {
   long   n;
   LineNr   first = curPor->cursor.lnum;

   if (nlines <= 0)
      return;

   // save the deleted lines for undo
   if (undo && u_savedel(first, nlines) == FAIL)
      return;

   for (n = 0; n < nlines; ) {
      if (curBook->mem.flags & ML_EMPTY)       // nothing to delete
         break;

      ml_delete_flags(first, ML_DEL_MESSAGE);
      ++n;

      // If we delete the last line in the file, stop
      if (first > curBook->mem.lineCount)
          break;
   }

   // Correct the cursor position before calling deleted_lines_mark(), it may
   // trigger a callback to display the cursor.
   curPor->cursor.col = 0;
   check_cursor_lnum();

   // adjust marks, mark the book as changed and prepare for displaying
   deleted_lines_mark(first, n);
}

//}}}
//{{{operators

// implementation of various operators: op_shift, op_delete, op_tilde, op_change, op_yank, jugJoinLinesUnderCursor

private void shift_block(Operator *oper, int amount);
private void   mb_adjust_opend(Operator *oper);
private int   do_addsub(int opTy, Pos *pos, int length, LineNr prenum1);
private void   pbyte(Pos lp, int c);
#define PBYTE(lp, c) pbyte(lp, c)


// Flags for third item in "opchars".
#define OPF_LINES  1   // operator always works on lines
#define OPF_CHANGE 2   // operator changes text

// The names of operators.
// IMPORTANT: Index must correspond with defines in eegl.h!!! The third field holds OPF_ flags.
private Byte opchars[][3] = {
   {ZERO, ZERO, 0},              // OP_NOP
   {'d', ZERO, OPF_CHANGE},      // OP_DELETE
   {'y', ZERO, 0},               // OP_YANK
   {'c', ZERO, OPF_CHANGE},      // OP_CHANGE
   {'<', ZERO, OPF_LINES | OPF_CHANGE},   // OP_LSHIFT
   {'>', ZERO, OPF_LINES | OPF_CHANGE},   // OP_RSHIFT
   {'!', ZERO, OPF_LINES | OPF_CHANGE},   // OP_FILTER
   {'g', '~', OPF_CHANGE},      // OP_TILDE
   {'=', ZERO, OPF_LINES | OPF_CHANGE},   // OP_INDENT
   {'g', 'q', OPF_LINES | OPF_CHANGE},   // OP_FORMAT
   {':', ZERO, OPF_LINES},      // OP_COLON
   {'g', 'U', OPF_CHANGE},      // OP_UPPER
   {'g', 'u', OPF_CHANGE},      // OP_LOWER
   {'J', ZERO, OPF_LINES | OPF_CHANGE},   // DO_JOIN
   {'g', 'J', OPF_LINES | OPF_CHANGE},   // DO_JOIN_NS
   {'g', '?', OPF_CHANGE},       // OP_ROT13
   {'r', ZERO, OPF_CHANGE},      // OP_REPLACE
   {'I', ZERO, OPF_CHANGE},      // OP_INSERT
   {'A', ZERO, OPF_CHANGE},      // OP_APPEND
   {'z', 'f', OPF_LINES},        // OP_FOLD
   {'z', 'o', OPF_LINES},        // OP_FOLDOPEN
   {'z', 'O', OPF_LINES},        // OP_FOLDOPENREC
   {'z', 'c', OPF_LINES},        // OP_FOLDCLOSE
   {'z', 'C', OPF_LINES},        // OP_FOLDCLOSEREC
   {'z', 'd', OPF_LINES},        // OP_FOLDDEL
   {'z', 'D', OPF_LINES},        // OP_FOLDDELREC
   {'g', 'w', OPF_LINES | OPF_CHANGE},   // OP_FORMAT2
   {'g', '@', OPF_CHANGE},       // OP_FUNCTION
   {Ctrl_A, ZERO, OPF_CHANGE},   // OP_ADD
   {Ctrl_X, ZERO, OPF_CHANGE}    // OP_SUB
};

// Translate an action name into an operator type. Must only be called with a valid operator name!
Unt
get_op_type(Unt char1, Unt char2) {
   if (char1 == 'r')      // ignore second character
      return OP_REPLACE;
   if (char1 == '~')      // when tilde is an operator
      return OP_TILDE;
   if (char1 == 'g' && char2 == Ctrl_A)   // add
      return OP_ADD;
   if (char1 == 'g' && char2 == Ctrl_X)   // subtract
      return OP_SUB;
   if (char1 == 'z' && char2 == 'y')   // OP_YANK
      return OP_YANK;
      
   Unt i;
   for (i = 0; ; ++i) {
      if (opchars[i][0] == char1 && opchars[i][1] == char2)
         break;
      if (i == (int)ARRAY_LENGTH(opchars) - 1) {
         internal_error(S"get_op_type()");
         break;
      }
   }
   return i;
}

// Return TRUE if operator "op" always works on whole lines.
private int
op_on_lines(int op) {
   return opchars[op][2] & OPF_LINES;
}

// Return TRUE if operator "op" changes text.
int
op_is_change(int op) {
   return opchars[op][2] & OPF_CHANGE;
}

// Get first operator command character. Returns 'g' or 'z' if there is another command character.
int
get_op_char(int optype) {
   return opchars[optype][0];
}

// Get second operator command character.
int
get_extra_op_char(int optype) {
   return opchars[optype][1];
}

// op_shift - handle a shift operation
void
op_shift(Operator *oper, int curs_top, int amount) {
   if (u_save((LineNr)(oper->start.lnum - 1), (LineNr)(oper->end.lnum + 1)) == FAIL)
      return;

   int block_col = 0;
   if (oper->block_mode)
      block_col = curPor->cursor.col;

   for (long i = oper->line_count; --i >= 0; ) {
      int first_char = *ml_get_curline();
      if (first_char == ZERO)            // empty line
         curPor->cursor.col = 0;
      ei (oper->block_mode)
         shift_block(oper, amount);
      ei (first_char != '#' || !preprocs_left())
         // Move the line right if it doesn't start with '#', 'smartindent'
         // isn't set or 'cindent' isn't set or '#' isn't in 'cino'.
         shift_line(oper->opTy == OP_LSHIFT, TRUE, amount, FALSE);
      ++curPor->cursor.lnum;
   }

   changed_lines(oper->start.lnum, 0, oper->end.lnum + 1, 0L);
   if (oper->block_mode) {
      curPor->cursor.lnum = oper->start.lnum;
      curPor->cursor.col = block_col;
   } ei (curs_top) {      // put cursor on first line, for ">>"
      curPor->cursor.lnum = oper->start.lnum;
      beginline(BL_SOL | BL_FIX);   // shift_line() may have set cursor.col
   } else
      --curPor->cursor.lnum;   // put cursor on last line, for ":>"

   // The cursor line is not in a closed fold
   foldOpenCursor();

   CS op = (oper->opTy == OP_RSHIFT) ? S">" : S"<";
   CS msg_line_single = NGETTEXT("%ld line %sed %d time", "%ld line %sed %d times", amount);
   CS msg_line_plural = NGETTEXT("%ld lines %sed %d time", "%ld lines %sed %d times", amount);
   eeSnprintf(IObuff, IOSIZE,
      NGETTEXT(msg_line_single, msg_line_plural, oper->line_count),
      oper->line_count, op, amount);
   msgAndKeep(IObuff, 0, TRUE);

   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
      // Set "'[" and "']" marks.
      curBook->opStart = oper->start;
      curBook->opEnd.lnum = oper->end.lnum;
      curBook->opEnd.col = ml_get_len(oper->end.lnum);
      if (curBook->opEnd.col > 0)
         --curBook->opEnd.col;
   }
}


private Long
get_new_sw_indent(
   int      left,      // TRUE if shift is to the left
   int      round,      // TRUE if new indent is to be to a tabstop
   Long   amount,      // Number of shifts
   Long   sw_val)
{
   Long   count = get_indent();
   Long   i, j;

   if (round) {        // round off indent
      i = trim_to_int(count) / sw_val;   // number of 'shiftwidth' rounded down
      j = trim_to_int(count) % sw_val;   // extra spaces
      if (j && left)      // first remove extra spaces
          --amount;
      if (left) {
         i -= amount;
         if (i < 0)
            i = 0;
      } else
         i += amount;
      count = i * sw_val;
   } else {        // original vi indent
      if (left) {
         count -= sw_val * amount;
         if (count < 0)
            count = 0;
      } else
         count += sw_val * amount;
   }

   return count;
}

//Shift the current line 'amount' shiftwidth(s) left (if 'left' is TRUE) or right.
//
//The rules for choosing a shiftwidth are: If 'shiftwidth' is non-zero, use 'shiftwidth'; else if 
//'vartabstop' is not empty, use 'vartabstop'; else use 'tabstop'. The Eegl documentation says 
//nothing about 'softtabstop' or 'varsofttabstop' affecting the shiftwidth, and neither affects the
//shiftwidth in current versions of Eegl, so they are not considered here.
void
shift_line(
   int   left,         // TRUE if shift is to the left
   int   round,         // TRUE if new indent is to be to a tabstop
   int   amount,         // Number of shifts
   Boole   call_changed_bytes)   // call changed_bytes()
{
   Long   count;
   long   sw_val = curBook->o.shiftWidth;

   count = get_new_sw_indent(left, round, amount, sw_val);

   // Set new indent
   (void)set_indent(trim_to_int(count), call_changed_bytes ? SIN_CHANGED : 0);
}

//Shift one line of the current block one shiftwidth right or left.
//Leave cursor on first character in block.
private void
shift_block(Operator *oper, int amount) {
   int         left = (oper->opTy == OP_LSHIFT);
   int         oldstate = stateG;
   int         total;
   CS      newp;
   Unt      newlen, oldlen;
   int         oldcol = curPor->cursor.col;
   int         sw_val = (int)get_sw_value_indent(curBook, left);
   BlockDef   bd;
   int         incr;
   ColNr      ws_vcol;
   int         added;
   Unt      new_line_len;   // the length of the line after the block shift

   stateG = MODE_INSERT; 
   block_prep(oper, OUT &bd, curPor->cursor.lnum, true);
   if (bd.is_short)
      return;

   // total is number of screen columns to be inserted/removed
   total = (int)((unsigned)amount * (unsigned)sw_val);
   if ((total / sw_val) != amount)
      return; // multiplication overflow

    CS oldp = ml_get_curline();
    oldlen = ml_get_curline_len();

   if (!left) {
      int tabs = 0, spaces = 0;
      CharTableSize   cts;

      //1. Get start vcol
      //2. Total ws vcols
      //3. Divvy into TABs & spp
      //4. Construct new string
      total += bd.pre_whitesp; // all virtual WS up to & incl a split TAB
      ws_vcol = bd.start_vcol - bd.pre_whitesp;
      if (bd.startspaces) {
         if (utfCharLen(bd.textstart) == 1)
            ++bd.textstart;
         else {
            ws_vcol = 0;
            bd.startspaces = 0;
         }
      }

      // TODO: is passing bd.textstart for start of the line OK?
      bookInitCharsForKeywordsSizeArg(
         &cts, curPor, curPor->cursor.lnum, bd.start_vcol, bd.textstart, bd.textstart
      );
      for ( ; SPACE_OR_TAB(*cts.cts_ptr); ) {
          incr = lbr_chartabsize_adv(&cts);
          total += incr;
          cts.cts_vcol += incr;
      }
      bd.textstart = cts.cts_ptr;
      bd.start_vcol = cts.cts_vcol;
      clear_chartabsize_arg(&cts);

      // OK, now total=all the VWS reqd, and textstart points at the 1st non-ws char in the block.
      if (!curBook->o.expandTab)
         tabs = ((ws_vcol % sw_val) + total) / sw_val; // number of tabs
      if (tabs > 0)
         spaces = ((ws_vcol % sw_val) + total) % sw_val; // number of spp
      else
         spaces = total;
      // if we're splitting a TAB, allow for it
      bd.textcol -= bd.pre_whitesp_c - (bd.startspaces != 0);

      new_line_len = bd.textcol + tabs + spaces + (oldlen - (bd.textstart - oldp));
      newp = alloc(new_line_len + 1);
      mch_memmove(newp, oldp, (Unt)bd.textcol);
      newlen = bd.textcol;
      memset(newp + newlen, TAB, (Unt)tabs);
      newlen += tabs;
      memset(newp + newlen, ' ', (Unt)spaces);
      STRCPY(newp + newlen + spaces, bd.textstart);
   } else {// left
      ColNr       destination_col;   // column to which text in block will
                  // be shifted
      Byte       *verbatim_copy_end;   // end of the part of the line which is
                  // copied verbatim
      ColNr       verbatim_copy_width;// the (displayed) width of this part
                  // of line
      Unt       fill;      // nr of spaces that replace a TAB
      Unt       block_space_width;
      Unt       shift_amount;
      Byte       *non_white = bd.textstart;
      ColNr       non_white_col;
      Unt       fixedlen;      // length of string left of the shift
                  // position (ie the string not being shifted)
      CharTableSize cts;

      /*
       * Firstly, let's find the first non-whitespace character that is
       * displayed after the block's start column and the character's column
       * number. Also, let's calculate the width of all the whitespace
       * characters that are displayed in the block and precede the searched
       * non-whitespace character.
       */

      // If "bd.startspaces" is set, "bd.textstart" points to the character,
      // the part of which is displayed at the block's beginning. Let's start
      // searching from the next character.
      if (bd.startspaces)
          MB_PTR_ADV(non_white);

      // The character's column is in "bd.start_vcol".
      non_white_col = bd.start_vcol;

      bookInitCharsForKeywordsSizeArg(&cts, curPor, curPor->cursor.lnum,
                  non_white_col, bd.textstart, non_white);
      while (SPACE_OR_TAB(*cts.cts_ptr)) {
         incr = lbr_chartabsize_adv(&cts);
         cts.cts_vcol += incr;
      }
      non_white_col = cts.cts_vcol;
      non_white = cts.cts_ptr;
      clear_chartabsize_arg(&cts);

      block_space_width = non_white_col - oper->start_vcol;
      // We will shift by "total" or "block_space_width", whichever is less.
      shift_amount = (block_space_width < (Unt)total ? block_space_width : (Unt)total);

      // The column to which we will shift the text.
      destination_col = (ColNr)(non_white_col - shift_amount);

      // Now let's find out how much of the beginning of the line we can
      // reuse without modification.
      verbatim_copy_end = bd.textstart;
      verbatim_copy_width = bd.start_vcol;

      // If "bd.startspaces" is set, "bd.textstart" points to the character
      // preceding the block. We have to subtract its width to obtain its column number.
      if (bd.startspaces)
          verbatim_copy_width -= bd.start_char_vcols;
      bookInitCharsForKeywordsSizeArg(&cts, curPor, 0, verbatim_copy_width, bd.textstart, verbatim_copy_end);
      while (cts.cts_vcol < destination_col) {
         incr = lbr_chartabsize(&cts);
         if (cts.cts_vcol + incr > destination_col)
            break;
         cts.cts_vcol += incr;
         MB_PTR_ADV(cts.cts_ptr);
      }
      verbatim_copy_width = cts.cts_vcol;
      verbatim_copy_end = cts.cts_ptr;
      clear_chartabsize_arg(&cts);

      // If "destination_col" is different from the width of the initial
      // part of the line that will be copied, it means we encountered a tab
      // character, which we will have to partly replace with spaces.
      fill = destination_col - verbatim_copy_width;

      // The replacement line will consist of:
      // - the beginning of the original line up to "verbatim_copy_end",
      // - "fill" number of spaces,
      // - the rest of the line, pointed to by non_white.
      fixedlen = verbatim_copy_end - oldp;
      new_line_len = fixedlen + fill + (oldlen - (non_white - oldp));

      newp = alloc(new_line_len + 1);
      mch_memmove(newp, oldp, fixedlen);
      newlen = fixedlen;
      memset(newp + newlen, ' ', (Unt)fill);
      STRCPY(newp + newlen + fill, non_white);
   }
   // replace the line
   ml_replace(curPor->cursor.lnum, newp, FALSE);

   //compute the number of bytes added or subtracted. note new_line_len and oldlen are unsigned 
   //so we have to be careful about how we calculate this.
   if (new_line_len >= oldlen)
      added = (int)(new_line_len - oldlen);
   else
      added = 0 - (int)(oldlen - new_line_len);
   inserted_bytes(curPor->cursor.lnum, bd.textcol, added);
   stateG = oldstate;
   curPor->cursor.col = oldcol;
}

//Insert string "s" (b_insert ? before : after) block :AKelly Caller must prepare for undo.
private void
block_insert(
   Operator* oper,
   CS s,
   Unt slen,
   int b_insert,
   OUT BlockDef* bdp)
{
   int      count = 0;   // extra spaces to replace a cut TAB
   int      spaces = 0;   // non-zero if cutting a TAB
   ColNr   offset;      // pointer along new line
   ColNr   startcol;   // column where insert starts
   Byte   *newp, *oldp;   // new, old lines
   LineNr   lnum;      // loop var
   int      oldstate = stateG;
   int sw_val = curBook->o.shiftWidth;

   stateG = MODE_INSERT;

   for (lnum = oper->start.lnum + 1; lnum <= oper->end.lnum; lnum++) {
      block_prep(oper, OUT bdp, lnum, true);
      if (bdp->is_short && b_insert)
         continue;   // OP_INSERT, line ends before block start

      oldp = ml_get(lnum);

      if (b_insert) {
         spaces = bdp->startspaces;
         if (spaces != 0)
            count = sw_val - 1; // we're cutting a TAB
         offset = bdp->textcol;
      } else { // append
         if (!bdp->is_short) { // spaces = padding after block
            spaces = (bdp->endspaces ? sw_val - bdp->endspaces : 0);
            if (spaces != 0)
                count = sw_val - 1; // we're cutting a TAB
            offset = bdp->textcol + bdp->textlen - (spaces != 0);
         } else { // spaces = padding to block edge
            // if $ used, just append to EOL (ie spaces==0)
            if (!bdp->is_MAX)
                spaces = (oper->end_vcol - bdp->end_vcol) + 1;
            count = spaces;
            offset = bdp->textcol + bdp->textlen;
         }
      }

      if (spaces > 0)
         // avoid copying part of a multi-byte character
         offset -= (*mb_head_off)(oldp, oldp + offset);

      if (spaces < 0)  // can happen when the cursor was moved
          spaces = 0;

      // Make sure the allocated size matches what is actually copied below.
      newp = alloc(ml_get_len(lnum) + spaces + slen
             + (spaces > 0 && !bdp->is_short ? sw_val - spaces : 0)
                             + count + 1);

      // copy up to shifted part
      mch_memmove(newp, oldp, (Unt)offset);
      oldp += offset;

      // insert pre-padding
      memset(newp + offset, ' ', (Unt)spaces);
      startcol = offset + spaces;

      // copy the new text
      mch_memmove(newp + startcol, s, slen);
      offset += (int)slen;

      if (spaces > 0 && !bdp->is_short) {
         if (*oldp == TAB) {
            // insert post-padding
            memset(newp + offset + spaces, ' ', (Unt)(sw_val - spaces));
            // we're splitting a TAB, don't copy it
            oldp++;
            // We allowed for that TAB, remember this now
            count++;
         } else
            // Not a TAB, no extra spaces
            count = spaces;
      }

      if (spaces > 0)
         offset += count;
      STRCPY(newp + offset, oldp);

      ml_replace(lnum, newp, FALSE);

      if (b_insert)
         // correct any text properties
         inserted_bytes(lnum, startcol, (int)slen);

      if (lnum == oper->end.lnum) {
         // Set "']" mark to the end of the block instead of the end of
         // the insert in the first line.
         curBook->opEnd.lnum = oper->end.lnum;
         curBook->opEnd.col = offset;
      }
   } // for all lnum

   changed_lines(oper->start.lnum + 1, 0, oper->end.lnum + 1, 0L);

   stateG = oldstate;
}

// Get the screen position of character col with a coladd in the cursor line.
private int
getviscol2(ColNr col, ColNr coladd) {
   Pos   pos;
   pos.lnum = curPor->cursor.lnum;
   pos.col = col;
   pos.coladd = coladd;
   
   ColNr   x;
   bookGetVirtualColInVirtualMode(curPor, &pos, OUT &x, NULL, NULL);
   return (int)x;
}

Unt
gchar_pos(Pos *pos) {
   // When searching columns is sometimes put at the end of a line.
   if (pos->col == MAXCOL)
      return ZERO;
   int ptrlen = ml_get_len(pos->lnum);
   CS ptr = ml_get_pos(pos);
   if (pos->col > ptrlen)
      return ZERO;
   return mb_ptr2char(ptr);
}

Unt
gchar_cursor(void) {
   return mb_ptr2char(ml_get_cursor());
}


// Handle a delete operation. Return FAIL if undo failed, OK otherwise.
int
op_delete(Operator* oper) {
   int         n;
   LineNr      lnum;
   Byte      *ptr;
   BlockDef   bd;
   LineNr      old_lcount = curBook->mem.lineCount;
   int         did_yank = FALSE;

   if (curBook->mem.flags & ML_EMPTY)       // nothing to do
      return OK;

   // Nothing to delete, return here.   Do prepare undo, for op_change().
   if (oper->empty)
      return u_save_cursor();

   if (!curBook->o.modifiable) {
      emsg(_(e_cannot_make_changes_modifiable_is_off));
      return FAIL;
   }

   adjust_clip_reg(&oper->regname);

   mb_adjust_opend(oper);

   //Imitate the strange Vi behaviour: If the delete spans more than one
   //line and motion_type == MCHAR and the result is a blank line, make the
   //delete linewise.  Don't do this for the change command or Visual mode.
   if (   oper->motion_type == MCHAR
       && !oper->is_VIsual
       && !oper->block_mode
       && oper->line_count > 1
       && oper->motion_force == ZERO
       && oper->opTy == OP_DELETE
   ) {
      ptr = ml_get(oper->end.lnum) + oper->end.col;
      if (*ptr != ZERO)
         ptr += oper->inclusive;
      ptr = skipwhite(ptr);
      if (*ptr == ZERO && inindent(0))
         oper->motion_type = MLINE;
   }

   // Check for trying to delete (e.g. "D") in an empty line. Note: For the change operator it is ok
   if (   oper->motion_type == MCHAR
       && oper->line_count == 1
       && oper->opTy == OP_DELETE
       && *ml_get(oper->start.lnum) == ZERO
   ){
      // It's an error to operate on an empty region
      if (virtual_op)
         // Virtual editing: Nothing gets deleted, but we set the '[ and '] marks as if it happened
         goto setmarks;
      return OK;
   }

   // Copy whatever we're about to delete to the register. If a yank register was specified, put 
   // the deleted text into that register. For the black hole register '_' don't yank anything.
   if (oper->regname != '_') {
      if (oper->regname != 0) {
         // check for read-only register
         if (!valid_yank_reg(oper->regname, TRUE)) {
            beep_flush();
            return OK;
         }
         get_yank_register(oper->regname, TRUE); // yank into specif'd reg.
         if (op_yank(oper, TRUE, FALSE) == OK)   // yank without message
            did_yank = TRUE;
      } else
         reset_y_append(); // not appending to unnamed register

      // Put deleted text into register 1 and shift number registers if the delete contains a line 
      // break, or when using a specific operator (Vi compatible)
      if (oper->motion_type == MLINE || oper->line_count > 1 || oper->use_reg_one) {
         shift_delete_registers();
         if (op_yank(oper, TRUE, FALSE) == OK)
            did_yank = TRUE;
      }

      //Yank into small delete register when no named register specified
      //and the delete is within one line.
      if ((oper->regname == '*' || oper->regname == '+' || oper->regname == 0) 
         && oper->motion_type != MLINE && oper->line_count == 1
      ){
         oper->regname = '-';
         get_yank_register(oper->regname, TRUE);
         if (op_yank(oper, TRUE, FALSE) == OK)
            did_yank = TRUE;
         oper->regname = 0;
      }

      //If there's too much stuff to fit in the yank register, then get a
      //confirmation before doing the delete. This is crude, but simple.
      //And it avoids doing a delete of something we can't put back if we want.
      if (!did_yank) {
         int msg_silent_save = msg_silent;

         msg_silent = 0;   // must display the prompt
         n = ask_yesno((CS)_("cannot yank; delete anyway"), TRUE);
         msg_silent = msg_silent_save;
         if (n != 'y') {
            emsg(_(e_command_aborted));
            return FAIL;
         }
      }

      if (did_yank && has_textyankpost())
         yank_do_autocmd(oper, get_y_current());
   }

   // block mode delete
   if (oper->block_mode) {
      if (u_save((LineNr)(oper->start.lnum - 1), (LineNr)(oper->end.lnum + 1)) == FAIL)
         return FAIL;

      for (lnum = curPor->cursor.lnum; lnum <= oper->end.lnum; ++lnum) {
         block_prep(oper, OUT &bd, lnum, true);
         if (bd.textlen == 0)   // nothing to delete
            continue;

         // Adjust cursor position for tab replaced by spaces and 'lbr'.
         if (lnum == curPor->cursor.lnum) {
            curPor->cursor.col = bd.textcol + bd.startspaces;
            curPor->cursor.coladd = 0;
         }

         // "n" == number of chars deleted
         // If we delete a TAB, it may be replaced by several characters.
         // Thus the number of characters may increase!
         n = bd.textlen - bd.startspaces - bd.endspaces;
         CS oldp = ml_get(lnum);
         CS newp = alloc(ml_get_len(lnum) + 1 - n);
         // copy up to deleted part
         mch_memmove(newp, oldp, (Unt)bd.textcol);
         // insert spaces
         memset(newp + bd.textcol, ' ', (Unt)(bd.startspaces + bd.endspaces));
         // copy the part after the deleted part
         STRCPY(newp + bd.textcol + bd.startspaces + bd.endspaces, oldp + bd.textcol + bd.textlen);
         // replace the line
         ml_replace(lnum, newp, FALSE);

         if (curBook->hasTextprop && n != 0)
            adjustPropColumns(lnum, bd.textcol, -n, 0);
      }

      check_cursor_col();
      changed_lines(curPor->cursor.lnum, curPor->cursor.col, oper->end.lnum + 1, 0L);
      oper->line_count = 0;       // no lines deleted
   } ei (oper->motion_type == MLINE) {
      if (oper->opTy == OP_CHANGE) {
         //Delete the lines except the first one.  Temporarily move the
         //cursor to the next line.  Save the current line number, if the
         //last line is deleted it may be changed.
         if (oper->line_count > 1) {
            lnum = curPor->cursor.lnum;
            ++curPor->cursor.lnum;
            del_lines((long)(oper->line_count - 1), TRUE);
            curPor->cursor.lnum = lnum;
         }
         if (u_save_cursor() == FAIL)
            return FAIL;
         if (curBook->o.autoIndent)  {        // don't delete indent
            beginline(BL_WHITE);       // cursor on first non-white
            didAindentG = true;          // delete the indent when ESC hit
            ai_col = curPor->cursor.col;
         } else
            beginline(0);       // cursor in column 0
         truncate_line(FALSE);  //delete the rest of the line, leaving cursor past last char in line
         if (oper->line_count > 1)
            u_clearline();      // "U" command not possible after "2cc"
      } else {
         del_lines(oper->line_count, TRUE);
         beginline(BL_WHITE | BL_FIX);
         u_clearline();   // "U" command not possible after "dd"
      }
   } else {
      if (virtual_op) {
         int      endcol = 0;

         // For virtualedit: break the tabs that are partly included.
         if (gchar_pos(&oper->start) == '\t') {
            if (u_save_cursor() == FAIL)   // save first line for undo
               return FAIL;
            if (oper->line_count == 1)
               endcol = getviscol2(oper->end.col, oper->end.coladd);
            coladvance_force(getviscol2(oper->start.col, oper->start.coladd));
            oper->start = curPor->cursor;
            if (oper->line_count == 1) {
               coladvance(endcol);
               oper->end.col = curPor->cursor.col;
               oper->end.coladd = curPor->cursor.coladd;
               curPor->cursor = oper->start;
            }
         }

         // Break a tab only when it's included in the area.
         if (gchar_pos(&oper->end) == '\t' && (int)oper->end.coladd < oper->inclusive) {
            // save last line for undo
            if (u_save((LineNr)(oper->end.lnum - 1), (LineNr)(oper->end.lnum + 1)) == FAIL)
               return FAIL;
            curPor->cursor = oper->end;
            coladvance_force(getviscol2(oper->end.col, oper->end.coladd));
            oper->end = curPor->cursor;
            curPor->cursor = oper->start;
          }
         mb_adjust_opend(oper);
      }

      if (oper->line_count == 1) {  // delete characters within one line
         if (u_save_cursor() == FAIL)   // save line for undo
            return FAIL;

         n = oper->end.col - oper->start.col + 1 - !oper->inclusive;

         if (virtual_op) {
            //fix up things for virtualedit-delete:
            //break the tabs which are going to get in our way
            int len = ml_get_curline_len();

            if (oper->end.coladd != 0
                  && (int)oper->end.col >= len - 1
                  && !(oper->start.coladd && (int)oper->end.col >= len - 1)
            )
               n++;
            //Delete at least one char (e.g, when on a control char).
            if (n == 0 && oper->start.coladd != oper->end.coladd)
               n = 1;

            //When deleted a char in the line, reset coladd.
            if (gchar_cursor() != ZERO)
               curPor->cursor.coladd = 0;
         }
         (void)del_bytes((long)n, !virtual_op,  oper->opTy == OP_DELETE && !oper->is_VIsual);
      } else {          // delete characters between lines
         Pos   curpos;

         // save deleted and changed lines for undo
         if (u_save((LineNr)(curPor->cursor.lnum - 1),
               (LineNr)(curPor->cursor.lnum + oper->line_count)) == FAIL)
            return FAIL;

          truncate_line(TRUE);   // delete from cursor to end of line

          curpos = curPor->cursor;   // remember curPor->cursor
          ++curPor->cursor.lnum;
          del_lines((long)(oper->line_count - 2), FALSE);

          // delete from start of line until op_end
          n = (oper->end.col + 1 - !oper->inclusive);
          curPor->cursor.col = 0;
          (void)del_bytes((long)n, !virtual_op,
                oper->opTy == OP_DELETE && !oper->is_VIsual);
          curPor->cursor = curpos;   // restore curPor->cursor
          (void)jugJoinLinesUnderCursor(2, FALSE, FALSE, FALSE, FALSE);
      }
      if (oper->opTy == OP_DELETE)
          auto_format(FALSE, TRUE);
    }

    msgmore(curBook->mem.lineCount - old_lcount);

setmarks:
   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
      if (oper->block_mode) {
         curBook->opEnd.lnum = oper->end.lnum;
         curBook->opEnd.col = oper->start.col;
      } else
         curBook->opEnd = oper->start;
      curBook->opStart = oper->start;
   }

   return OK;
}

//Adjust end of operating area for ending on a multi-byte character. Used for deletion.
private void
mb_adjust_opend(Operator *oper) {
   if (!oper->inclusive)
      return;

   CS line = ml_get(oper->end.lnum);
   CS ptr = line + oper->end.col;
   if (*ptr != ZERO) {
      ptr -= (*mb_head_off)(line, ptr);
      ptr += utfCharLen(ptr) - 1;
      oper->end.col = ptr - line;
   }
}

//Replace the character under the cursor with "c". This takes care of multi-byte characters.
private void
replaceAndMoveBack(Unt c) {
   replaceChar(c);
   // Backup to the replaced character.
   dec_cursor();
}

//Replace a whole area with one character.
private int
op_replace(Operator *oper, Unt c) {
   int         n, numc;
   int         num_chars;
   Byte      *newp, *oldp;
   Unt      newlen, oldlen;
   BlockDef   bd;
   Byte      *after_p = NULL;
   int         had_ctrl_v_cr = FALSE;

   if ((curBook->mem.flags & ML_EMPTY ) || oper->empty)
      return OK;       // nothing to do

   if (c == REPLACE_CR_NCHAR) {
      had_ctrl_v_cr = TRUE;
      c = ENTER;
   } ei (c == REPLACE_NL_NCHAR) {
      had_ctrl_v_cr = TRUE;
      c = NL;
   }

   mb_adjust_opend(oper);

   if (u_save((LineNr)(oper->start.lnum - 1), (LineNr)(oper->end.lnum + 1)) == FAIL)
      return FAIL;

   //block mode replace
   if (oper->block_mode) {
      bd.is_MAX = (curPor->cursWant == MAXCOL);
      for ( ; curPor->cursor.lnum <= oper->end.lnum; ++curPor->cursor.lnum) {
         curPor->cursor.col = 0;  // make sure cursor position is valid
         block_prep(oper, OUT &bd, curPor->cursor.lnum, true);
         if (bd.textlen == 0 && (!virtual_op || bd.is_MAX))
            continue;       // nothing to replace

         // n == number of extra chars required
         // If we split a TAB, it may be replaced by several characters.
         // Thus the number of characters may increase!
         // If the range starts in virtual space, count the initial
         // coladd offset as part of "startspaces"
         if (virtual_op && bd.is_short && *bd.textstart == ZERO) {
            Pos vpos;

         vpos.lnum = curPor->cursor.lnum;
         getvpos(&vpos, oper->start_vcol);
         bd.startspaces += vpos.coladd;
         n = bd.startspaces;
         } else
            // allow for pre spaces
            n = (bd.startspaces ? bd.start_char_vcols - 1 : 0);

         // allow for post spp
         n += (bd.endspaces
             && !bd.is_oneChar
             && bd.end_char_vcols > 0) ? bd.end_char_vcols - 1 : 0;
         // Figure out how many characters to replace.
         numc = oper->end_vcol - oper->start_vcol + 1;
         if (bd.is_short && (!virtual_op || bd.is_MAX))
            numc -= (oper->end_vcol - bd.end_vcol) + 1;

         // A double-wide character can be replaced only up to half the
         // times.
         if (mb_char2cells(c) > 1) {
            if ((numc & 1) && !bd.is_short) {
                ++bd.endspaces;
                ++n;
            }
            numc = numc / 2;
         }

         // Compute bytes needed, move character count to num_chars.
         num_chars = numc;
         numc *= mb_char2len(c);
         // oldlen includes textlen, so don't double count
         n += numc - bd.textlen;

         oldp = ml_get_curline();
         oldlen = ml_get_curline_len();
         newp = alloc(oldlen + 1 + n);
         memset(newp, ZERO, (Unt)(oldlen + 1 + n));
         // copy up to deleted part
         mch_memmove(newp, oldp, (Unt)bd.textcol);
         newlen = bd.textcol;
         // insert pre-spaces
         memset(newp + newlen, ' ', (Unt)bd.startspaces);
         newlen += bd.startspaces;
         //insert replacement chars CHECK FOR ALLOCATED SPACE
         //REPLACE_CR_NCHAR/REPLACE_NL_NCHAR is used for entering CR literally.
         if (had_ctrl_v_cr || (c != '\r' && c != '\n')) {
            while (--num_chars >= 0)
               newlen += mb_char2bytes(c, newp + newlen);
            if (!bd.is_short) {
                // insert post-spaces
                memset(newp + newlen, ' ', (Unt)bd.endspaces);
                // copy the part after the changed part
                STRCPY(newp + newlen + bd.endspaces,
                  oldp + bd.textcol + bd.textlen);
            }
         } else {
            // Replacing with \r or \n means splitting the line.
            after_p = alloc(oldlen + 1 + n - newlen);
            STRCPY(after_p, oldp + bd.textcol + bd.textlen);
         }

         //replace the line
         ml_replace(curPor->cursor.lnum, newp, FALSE);
         if (after_p != NULL) {
            ml_append(curPor->cursor.lnum++, after_p, 0, FALSE);
            appended_lines_mark(curPor->cursor.lnum, 1L);
            oper->end.lnum++;
            eeglFree(after_p);
         }
      }
   } else {
      //MCHAR and MLINE motion replace.
      if (oper->motion_type == MLINE) {
         oper->start.col = 0;
         curPor->cursor.col = 0;
         oper->end.col = ml_get_len(oper->end.lnum);
         if (oper->end.col)
            --oper->end.col;
      } ei (!oper->inclusive)
         dec(&(oper->end));

      while (LTOREQ_POS(curPor->cursor, oper->end)) {
         int done = FALSE;

         n = gchar_cursor();
         if (n != ZERO) {
            int new_byte_len = mb_char2len(c);
            int old_byte_len = utfCharLen(ml_get_cursor());

            if (new_byte_len > 1 || old_byte_len > 1) {
               // This is slow, but it handles replacing a single-byte
               // with a multi-byte and the other way around.
               if (curPor->cursor.lnum == oper->end.lnum)
                  oper->end.col += new_byte_len - old_byte_len;
               replaceAndMoveBack(c);
               done = TRUE;
            } else {
               if (n == TAB) {
                  int end_vcol = 0;

                  if (curPor->cursor.lnum == oper->end.lnum) {
                      // oper->end has to be recalculated when
                      // the tab breaks
                      end_vcol = getviscol2(oper->end.col,
                                   oper->end.coladd);
                  }
                  coladvance_force(getviscol());
                  if (curPor->cursor.lnum == oper->end.lnum)
                      getvpos(&oper->end, end_vcol);
               }
               // with "coladd" set may move to just after a TAB
               if (gchar_cursor() != ZERO) {
                  PBYTE(curPor->cursor, c);
                  done = TRUE;
               }
            }
          }
         if (!done && virtual_op && curPor->cursor.lnum == oper->end.lnum) {
            int virtcols = oper->end.coladd;

            if (curPor->cursor.lnum == oper->start.lnum
               && oper->start.col == oper->end.col && oper->start.coladd)
                virtcols -= oper->start.coladd;

            // oper->end has been trimmed so it's effectively inclusive;
            // as a result an extra +1 must be counted so we don't trample the ZERO
            coladvance_force(getviscol2(oper->end.col, oper->end.coladd) + 1);
            curPor->cursor.col -= (virtcols + 1);
            for (; virtcols >= 0; virtcols--) {
               if (mb_char2len(c) > 1)
                  replaceAndMoveBack(c);
               else
                  PBYTE(curPor->cursor, c);
               if (inc(&curPor->cursor) == -1)
                  break;
            }
         }

         // Advance to next character, stop at the end of the file.
         if (inc_cursor() == -1)
            break;
      }
   }

   curPor->cursor = oper->start;
   check_cursor();
   changed_lines(oper->start.lnum, oper->start.col, oper->end.lnum + 1, 0L);

   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
      // Set "'[" and "']" marks.
      curBook->opStart = oper->start;
      curBook->opEnd = oper->end;
   }

   return OK;
}

private Boole swapchars(Unt opTy, Pos* pos, int length);

//Handle the (non-standard vi) tilde operator.  Also for "gu", "gU" and "g?".
private void
op_tilde(Operator* oper) {
   BlockDef   bd;
   Boole didChange = false;

   if (u_save((LineNr)(oper->start.lnum - 1), (LineNr)(oper->end.lnum + 1)) == FAIL)
      return;

   Pos pos = oper->start;
   if (oper->block_mode) {         // Visual block mode
      for (; pos.lnum <= oper->end.lnum; ++pos.lnum) {
         int one_change;

         block_prep(oper, OUT &bd, pos.lnum, false);
         pos.col = bd.textcol;
         one_change = swapchars(oper->opTy, &pos, bd.textlen);
         didChange = didChange || one_change;
      }
      if (didChange)
         changed_lines(oper->start.lnum, 0, oper->end.lnum + 1, 0L);
   } else {               // not block mode
      if (oper->motion_type == MLINE) {
          oper->start.col = 0;
          pos.col = 0;
          oper->end.col = ml_get_len(oper->end.lnum);
          if (oper->end.col)
         --oper->end.col;
      } ei (!oper->inclusive)
          dec(&(oper->end));

      if (pos.lnum == oper->end.lnum)
          didChange = swapchars(oper->opTy, &pos, oper->end.col - pos.col + 1);
      else
         for (;;) {
            didChange = didChange || swapchars(
               oper->opTy, &pos, 
               pos.lnum == oper->end.lnum ? oper->end.col + 1 : ml_get_pos_len(&pos)
            );
            if (LTOREQ_POS(oper->end, pos) || inc(&pos) == -1)
                break;
         }
      if (didChange) {
          changed_lines(oper->start.lnum, oper->start.col, oper->end.lnum + 1, 0L);
      }
   }

   if (!didChange && oper->is_VIsual)
      // No change: need to remove the Visual selection
      drawCurBookLater(UPD_INVERTED);

   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
      // Set '[ and '] marks.
      curBook->opStart = oper->start;
      curBook->opEnd = oper->end;
   }

   smsg(NGETTEXT("%ld line changed", "%ld lines changed", oper->line_count), oper->line_count);
}

//Invoke swapchar() on "length" bytes at position "pos". "pos" is advanced to just after the 
//changed characters. "length" is rounded up to include the whole last multi-byte character.
//Also work correctly when the number of bytes changes. Return TRUE if some character was changed.
private Boole
swapchars(Unt opTy, Pos* pos, int length) {
   int todo;
   Boole didChange = false;

   for (todo = length; todo > 0; --todo) {
      int len = utfCharLen(ml_get_pos(pos));

      // we're counting bytes, not characters
      if (len > 0)
         todo -= len - 1;
      didChange = didChange || swapchar(opTy, pos);
      if (inc(pos) == -1)    // at end of file
         break;
   }
   return didChange;
}

//If opTy == OP_UPPER: make uppercase,
//if opTy == OP_LOWER: make lowercase,
//if opTy == OP_ROT13: do rot13 encoding, else swap case of character at 'pos'.
//Return TRUE when something actually changed.
Boole
swapchar(Unt opTy, Pos* pos) {
   Unt c = gchar_pos(pos);

   // Only do rot13 encoding for ASCII characters.
   if (c >= 0x80 && opTy == OP_ROT13)
      return FALSE;

   Unt nc = c;
   if (MB_ISLOWER(c)) {
      if (opTy == OP_ROT13)
          nc = ROT13(c, 'a');
      ei (opTy != OP_LOWER)
          nc = MB_TOUPPER(c);
   } ei (MB_ISUPPER(c)) {
      if (opTy == OP_ROT13)
         nc = ROT13(c, 'A');
      ei (opTy != OP_UPPER)
         nc = MB_TOLOWER(c);
   }
   if (nc != c) {
      if (c >= 0x80 || nc >= 0x80) {
         Pos   sp = curPor->cursor;

         curPor->cursor = *pos;
         // don't use del_char(), it also removes composing chars
         del_bytes(utf_ptr2len(ml_get_cursor()), FALSE, FALSE);
         insertChar(nc);
         curPor->cursor = sp;
      } else
         PBYTE(*pos, nc);
      return TRUE;
   }
   return FALSE;
}

// op_insert - Insert and append operators for Visual mode.
void
op_insert(Operator *oper, long count1) {
   long      pre_textlen = 0;
   ColNr      ind_pre_col = 0, ind_post_col;
   int         ind_pre_vcol = 0, ind_post_vcol = 0;
   BlockDef   bd;
   int         i;
   Pos      t1;
   Pos      start_insert;

   // edit() changes this - record it for OP_APPEND
   bd.is_MAX = (curPor->cursWant == MAXCOL);

   // vis block is still marked. Get rid of it now.
   curPor->cursor.lnum = oper->start.lnum;
   drawUpdateScreen(UPD_INVERTED);

   if (oper->block_mode) {
      //When 'virtualedit' is used, need to insert the extra spaces before
      //doing block_prep().  When only "block" is used, virtual edit is
      //already disabled, but still need it when calling coladvance_force().
      if (curPor->cursor.coladd > 0) {
         if (u_save_cursor() == FAIL)
            return;

         coladvance_force(oper->opTy == OP_APPEND ? oper->end_vcol + 1 : getviscol());
         if (oper->opTy == OP_APPEND)
            --curPor->cursor.col;
      }
      // Get the info about the block before entering the text
      block_prep(oper, OUT &bd, oper->start.lnum, true);
      // Get indent information
      ind_pre_col = (ColNr)getwhitecols_curline();
      ind_pre_vcol = get_indent();
      pre_textlen = ml_get_len(oper->start.lnum) - bd.textcol;
      if (oper->opTy == OP_APPEND)
         pre_textlen -= bd.textlen;
   }

   if (oper->opTy == OP_APPEND) {
      if (oper->block_mode && curPor->cursor.coladd == 0) {
          // Move the cursor to the character right of the block.
          curPor->setCursWant = true;
          while (*ml_get_cursor() != ZERO
             && (curPor->cursor.col < bd.textcol + bd.textlen))
         ++curPor->cursor.col;
          if (bd.is_short && !bd.is_MAX) {
            // First line was too short, make it longer and adjust the
            // values in "bd".
            if (u_save_cursor() == FAIL)
                return;
            for (i = 0; i < bd.endspaces; ++i)
               insertChar(' ');
            bd.textlen += bd.endspaces;
         }
      } else {
          curPor->cursor = oper->end;
          check_cursor_col();

          // Works just like an 'i'nsert on the next character.
          if (!LINEEMPTY(curPor->cursor.lnum)
             && oper->start_vcol != oper->end_vcol)
         inc_cursor();
      }
   }

   t1 = oper->start;
   start_insert = curPor->cursor;
   (void)edit(ZERO, FALSE, (LineNr)count1);

   // When a tab was inserted, and the characters in front of the tab
   // have been converted to a tab as well, the column of the cursor
   // might have actually been reduced, so need to adjust here.
   if (t1.lnum == curBook->opStartOrig.lnum
       && LT_POS(curBook->opStartOrig, t1))
   oper->start = curBook->opStartOrig;

   // If user has moved off this line, we don't know what to do, so do
   // nothing.
   // Also don't repeat the insert when Insert mode ended with CTRL-C.
   if (curPor->cursor.lnum != oper->start.lnum || gotInterruptG)
      return;

   if (oper->block_mode) {
      int         ins_len;
      Byte         *firstline, *ins_text;
      BlockDef   bd2;
      int         did_indent = FALSE;
      Unt         len;
      Unt         add;
      // offset when cursor was moved in insert mode
      int         offset = 0;

      // If indent kicked in, the firstline might have changed
      // but only do that, if the indent actually increased.
      ind_post_col = (ColNr)getwhitecols_curline();
      if (curBook->opStart.col > ind_pre_col && ind_post_col > ind_pre_col) {
          bd.textcol += ind_post_col - ind_pre_col;
          ind_post_vcol = get_indent();
          bd.start_vcol += ind_post_vcol - ind_pre_vcol;
          did_indent = TRUE;
      }

      // The user may have moved the cursor before inserting something, try
      // to adjust the block for that.  But only do it, if the difference
      // does not come from indent kicking in.
      if (oper->start.lnum == curBook->opStartOrig.lnum && !bd.is_MAX && !did_indent) {
          int t = getviscol2(curBook->opStartOrig.col, curBook->opStartOrig.coladd);

          if (oper->opTy == OP_INSERT
             && oper->start.col + oper->start.coladd
                != curBook->opStartOrig.col + curBook->opStartOrig.coladd)
          {
         oper->start.col = curBook->opStartOrig.col;
         pre_textlen -= t - oper->start_vcol;
         oper->start_vcol = t;
          } ei (oper->opTy == OP_APPEND
             && oper->start.col + oper->start.coladd
                >= curBook->opStartOrig.col
                        + curBook->opStartOrig.coladd
         ) {
            oper->start.col = curBook->opStartOrig.col;
            // reset pre_textlen to the value of OP_INSERT
            pre_textlen += bd.textlen;
            pre_textlen -= t - oper->start_vcol;
            oper->start_vcol = t;
            oper->opTy = OP_INSERT;
         }
      }

      // Spaces and tabs in the indent may have changed to other spaces and
      // tabs.  Get the starting column again and correct the length.
      // Don't do this when "$" used, end-of-line will have changed.
      //
      // if indent was added and the inserted text was after the indent,
      // correct the selection for the new indent.
      if (did_indent && bd.textcol - ind_post_col > 0) {
          oper->start.col += ind_post_col - ind_pre_col;
          oper->start_vcol += ind_post_vcol - ind_pre_vcol;
          oper->end.col += ind_post_col - ind_pre_col;
          oper->end_vcol += ind_post_vcol - ind_pre_vcol;
      }
      block_prep(oper, OUT &bd2, oper->start.lnum, true);
      if (did_indent && bd.textcol - ind_post_col > 0) {
          // undo for where "oper" is used below
          oper->start.col -= ind_post_col - ind_pre_col;
          oper->start_vcol -= ind_post_vcol - ind_pre_vcol;
          oper->end.col -= ind_post_col - ind_pre_col;
          oper->end_vcol -= ind_post_vcol - ind_pre_vcol;
      }
      if (!bd.is_MAX || bd2.textlen < bd.textlen) {
          if (oper->opTy == OP_APPEND) {
         pre_textlen += bd2.textlen - bd.textlen;
         if (bd2.endspaces)
             --bd2.textlen;
          }
          bd.textcol = bd2.textcol;
          bd.textlen = bd2.textlen;
      }

      /*
       * Subsequent calls to ml_get() flush the firstline data - take a
       * copy of the required string.
       */
      firstline = ml_get(oper->start.lnum);
      len = ml_get_len(oper->start.lnum);
      add = bd.textcol;
      if (oper->opTy == OP_APPEND) {
          add += bd.textlen;
          // account for pressing cursor in insert mode when '$' was used
         if (bd.is_MAX
            && (start_insert.lnum == insertStartG.lnum && start_insert.col > insertStartG.col)
         ) {
            offset = (start_insert.col - insertStartG.col);
            add -= offset;
            if (oper->end_vcol > offset)
                oper->end_vcol -= (offset + 1);
            else
                // moved outside of the visual block, what to do?
                return;
         }
      }
      if (add > len)
          add = len;  // short line, point to the ZERO
      firstline += add;
      len -= add;
      if (pre_textlen >= 0 && (ins_len = (int)len - pre_textlen - offset) > 0) {
         ins_text = copySubstr(firstline, ins_len);
         // block handled here
         if (u_save(oper->start.lnum,
                   (LineNr)(oper->end.lnum + 1)) == OK)
            block_insert(oper, ins_text, ins_len, (oper->opTy == OP_INSERT), &bd);

         curPor->cursor.col = oper->start.col;
         check_cursor();
         eeglFree(ins_text);
      }
   }
}

//op_change - handle a change operation
//return TRUE if edit() returns because of a CTRL-O command
int
op_change(Operator *oper) {
   ColNr      l;
   int         retval;
   LineNr      linenr;
   long      pre_textlen = 0;
   long      pre_indent = 0;
   Byte      *firstline;
   Byte      *ins_text, *newp, *oldp;
   BlockDef   bd;

   l = oper->start.col;
   if (oper->motion_type == MLINE) {
      l = 0;
      can_si = may_do_si();   // Like opening a new line, do smart indent
   }

   // First delete the text in the region. In an empty book only need to save for undo
   if (curBook->mem.flags & ML_EMPTY) {
      if (u_save_cursor() == FAIL)
         return FALSE;
   } ei (op_delete(oper) == FAIL)
      return FALSE;

   if ((l > curPor->cursor.col) && !LINEEMPTY(curPor->cursor.lnum) && !virtual_op)
      inc_cursor();

   // check for still on same line (<CR> in inserted text meaningless) skip blank lines too
   if (oper->block_mode) {
      // Add spaces before getting the current line length.
      if (virtual_op && (curPor->cursor.coladd > 0 || gchar_cursor() == ZERO))
          coladvance_force(getviscol());
      firstline = ml_get(oper->start.lnum);
      pre_textlen = ml_get_len(oper->start.lnum);
      pre_indent = (long)getwhitecols(firstline);
      bd.textcol = curPor->cursor.col;
   }

   if (oper->motion_type == MLINE)
      fix_indent();

    // Reset finish_op now, don't want it set inside edit().
    int save_finish_op = finish_op;
    finish_op = FALSE;

    retval = edit(ZERO, FALSE, (LineNr)1);

    finish_op = save_finish_op;

   //In Visual block mode, handle copying the new text to all lines of the block.
   //Don't repeat the insert when Insert mode ended with CTRL-C.
   if (oper->block_mode && oper->start.lnum != oper->end.lnum && !gotInterruptG) {
      int   ins_len;

      // Auto-indenting may have changed the indent.  If the cursor was past
      // the indent, exclude that indent change from the inserted text.
      firstline = ml_get(oper->start.lnum);
      if (bd.textcol > (ColNr)pre_indent) {
         long new_indent = (long)getwhitecols(firstline);

         pre_textlen += new_indent - pre_indent;
         bd.textcol += new_indent - pre_indent;
      }

      ins_len = (int)ml_get_len(oper->start.lnum) - pre_textlen;
      if (ins_len > 0) {
         // Subsequent calls to ml_get() flush the firstline data - take a
         // copy of the inserted text.
         if ((ins_text = alloc(ins_len + 1)) != NULL) {
            copySubstrToAllocation(ins_text, (Text){firstline + bd.textcol, ins_len});
            for (linenr = oper->start.lnum + 1; linenr <= oper->end.lnum; linenr++) {
               block_prep(oper, OUT &bd, linenr, true);
               if (!bd.is_short || virtual_op) {
                  Pos vpos;
                  Unt newlen;

                  // If the block starts in virtual space, count the
                  // initial coladd offset as part of "startspaces"
                  if (bd.is_short) {
                     vpos.lnum = linenr;
                     (void)getvpos(&vpos, oper->start_vcol);
                  } else
                     vpos.coladd = 0;
                  oldp = ml_get(linenr);
                  newp = alloc(ml_get_len(linenr) + vpos.coladd + ins_len + 1);
                  // copy up to block start
                  mch_memmove(newp, oldp, (Unt)bd.textcol);
                  newlen = bd.textcol;
                  memset(newp + newlen, ' ', (Unt)vpos.coladd);
                  newlen += vpos.coladd;
                  mch_memmove(newp + newlen, ins_text, ins_len);
                  STRCPY(newp + newlen + ins_len, oldp + bd.textcol);
                  ml_replace(linenr, newp, FALSE);
                  // Shift the properties for linenr as edit() would do.
                  if (curBook->hasTextprop)
                     adjustPropColumns(linenr, bd.textcol, vpos.coladd + (int)ins_len, 0);
               }
            }
            check_cursor();

            changed_lines(oper->start.lnum + 1, 0, oper->end.lnum + 1, 0L);
         }
         eeglFree(ins_text);
      }
   }
   auto_format(FALSE, TRUE);

   return retval;
}

//When the cursor is on the ZERO past the end of the line and it should not be
//there, move it left.
void
adjust_cursor_eol(void) {
   int adj_cursor = (curPor->cursor.col > 0
            && gchar_cursor() == ZERO
            && !(restart_edit || (stateG & MODE_INSERT)));
   if (!adj_cursor)
      return;

    // Put the cursor on the last character in the line.
    dec_cursor();
}

//Return the offset at which the last comment in line starts. If there is no
//comment in the whole line, -1 is returned.
//
//When "flags" is not null, it is set to point to the flags describing the
//recognized comment leader.
private int
get_last_leader_offset(CS line, Byte **flags) {
   if (!curBook->o.comments) {
      return -1;
   }
   int result = -1;
   int i, j;
   int lower_check_bound = 0;
   CS string;
   CS com_leader;
   CS com_flags;
   CS list;
   Boole foundOne;
   Byte part_buf[COM_MAX_LEN];   // buffer for one option part

   // Repeat to match several nested comment strings.
   i = (int)STRLEN(line);
   while (--i >= lower_check_bound) {
      // scan through the @comments option for a match
      foundOne = false;
      for (list = curBook->o.comments; *list; ) {
         CS flags_save = list;

         //Get one option part into part_buf[].  Advance list to next one.
         //put string at start of string.
         (void)doCutPathFromListOfPaths(&list, part_buf, COM_MAX_LEN, ",");
         string = firstOccurrence(part_buf, ':');
         if (!string)   // If everything is fine, this cannot actually happen.
            continue;
         *string++ = ZERO;   // Isolate flags from string.
         com_leader = string;

         //Line contents and string must match.
         //When string starts with white space, must have some white space
         //(but the amount does not need to match, there might be a mix of TABs and spaces).
         if (SPACE_OR_TAB(string[0])) {
            if (i == 0 || !SPACE_OR_TAB(line[i - 1]))
                continue;
            while (SPACE_OR_TAB(*string))
                ++string;
         }
         for (j = 0; string[j] != ZERO && string[j] == line[i + j]; ++j)
            {}
         if (string[j] != ZERO)
            continue;

         //When 'b' flag used, there must be white space or an
         //end-of-line after the string in the line.
         if (firstOccurrence(part_buf, COM_BLANK) != NULL
             && !SPACE_OR_TAB(line[i + j]) && line[i + j] != ZERO
         )
            continue;

         if (firstOccurrence(part_buf, COM_MIDDLE) != NULL) {
            //For a middlepart comment, only consider it to match if everything before the 
            //current position in the line is whitespace.  Otherwise we would think we are 
            //inside a comment if the middle part appears somewhere in the middle
            //of the line. E.g. for C the "*" appears often.
            for (j = 0; SPACE_OR_TAB(line[j]) && j <= i; j++)
                ;
            if (j < i)
                continue;
         }

         //We have found a match, stop searching.
         foundOne = true;

         if (flags != 0)
            *flags = flags_save;
         com_flags = flags_save;

         break;
      }

      if (foundOne) {
         Byte part_buf2[COM_MAX_LEN];   // buffer for one option part
         int len1, len2, off;

         result = i;
         //If this comment nests, continue searching.
         if (firstOccurrence(part_buf, COM_NEST) != NULL)
            continue;

         lower_check_bound = i;

         // Let's verify whether the comment leader found is a substring
         // of other comment leaders. If it is, let's adjust the
         // lower_check_bound so that we make sure that we have determined
         // the comment leader correctly.

         while (SPACE_OR_TAB(*com_leader))
            ++com_leader;
         len1 = (int)STRLEN(com_leader);

         for (list = curBook->o.comments; *list; ) {
            CS flags_save = list;

            (void)doCutPathFromListOfPaths(&list, part_buf2, COM_MAX_LEN, ",");
            if (flags_save == com_flags)
               continue;
            string = firstOccurrence(part_buf2, ':');
            ++string;
            while (SPACE_OR_TAB(*string))
               ++string;
            len2 = (int)STRLEN(string);
            if (len2 == 0)
               continue;

            // Now we have to verify whether string ends with a substring
            // beginning the com_leader.
            for (off = (len2 > i ? i : len2); off > 0 && off + len1 > len2;) {
                --off;
                if (!STRNCMP(string + off, com_leader, len2 - off))
                {
               if (i - off < lower_check_bound)
                   lower_check_bound = i - off;
                }
            }
         }
      }
   }
   return result;
}


//If "process" is TRUE and the line begins with a comment leader (possibly
//after some white space), return a pointer to the text after it. Put a boolean
//value indicating whether the line ends with an unclosed comment in "is_comment".
//line - line to be processed,
//process - if FALSE, will only check whether the line ends with an unclosed comment,
//include_space - whether to also skip space following the comment leader,
//is_comment - will indicate whether the current line ends with an unclosed comment.
CS
skip_comment(
   Byte   *line,
   int      process,
   int        include_space,
   int      *is_comment
) {
   CS comment_flags = NULL;
   int    lead_len;
   int    leader_offset = get_last_leader_offset(line, &comment_flags);

   *is_comment = FALSE;
   if (leader_offset != -1) {
      // Let's check whether the line ends with an unclosed comment.
      // If the last comment leader has COM_END in flags, there's no comment.
      while (*comment_flags) {
         if (*comment_flags == COM_END || *comment_flags == ':')
            break;
         ++comment_flags;
      }
      if (*comment_flags != COM_END)
         *is_comment = TRUE;
   }

   if (process == FALSE)
      return line;

   lead_len = get_leader_len(line, &comment_flags, FALSE, include_space);

   if (lead_len == 0)
      return line;

   // Find:
   // - COM_END,
   // - colon,
   // whichever comes first.
   while (*comment_flags) {
      if (*comment_flags == COM_END || *comment_flags == ':')
         break;
      ++comment_flags;
   }

   // If we found a colon, it means that we are not processing a line
   // starting with a closing part of a three-part comment. That's good,
   // because we don't want to remove those as this would be annoying.
   if (*comment_flags == ':' || *comment_flags == ZERO)
      line += lead_len;

   return line;
}

//Join 'count' lines (minimal 2) at the cursor position.
//When "save_undo" is TRUE save lines for undo first.
//Set "use_formatoptions" to FALSE when e.g. processing backspace and comment
//leaders should not be removed.
//When setmark is TRUE, sets the '[ and '] mark, else, the caller is expected
//to set those marks.
//
//return FAIL for failure, OK otherwise
int
jugJoinLinesUnderCursor(
   long count,
   int insert_space,
   int save_undo,
   int use_formatoptions,
   int setmark
) {
   CS curr = NULL;
   CS curr_start = NULL;
   CS cend;
   int endcurr1 = ZERO;
   int endcurr2 = ZERO;
   int currsize = 0;   // size of the current line
   int sumsize = 0;   // size of the long new line
   LineNr t;
   ColNr col = 0;
   int ret = OK;
   int* comments = NULL;
   int remove_comments = (use_formatoptions == TRUE) && has_format_option(FO_REMOVE_COMS);
   int prev_was_comment;
   int propcount = 0;   // number of props over all joined lines
   int props_remaining;

   if (save_undo && u_save((LineNr)(curPor->cursor.lnum - 1),
             (LineNr)(curPor->cursor.lnum + count)) == FAIL)
      return FAIL;

   // Allocate an array to store the number of spaces inserted before each
   // line.  We will use it to pre-compute the length of the new line and the
   // proper placement of each original line in the new one.
   CS spaces = lallocZeroed(count, TRUE);
   if (remove_comments) {
      comments = lallocZeroed(count * sizeof(int), TRUE);
      if (comments == NULL) {
          eeglFree(spaces);
          return FAIL;
      }
    }

    //Don't move anything yet, just compute the final line length
    //and setup the array of space strings lengths. This loops forward over the joined lines.
    for (t = 0; t < count; ++t) {
      curr = curr_start = ml_get((LineNr)(curPor->cursor.lnum + t));
      propcount += count_props((LineNr) (curPor->cursor.lnum + t), t > 0, t + 1 == count);
      if (t == 0 && setmark && (commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
         // Set the '[ mark.
         curPor->book->opStart.lnum = curPor->cursor.lnum;
         curPor->book->opStart.col  = (ColNr)STRLEN(curr);
      }
      if (remove_comments) {
          // We don't want to remove the comment leader if the
          // previous line is not a comment.
          if (t > 0 && prev_was_comment) {
            CS new_curr = skip_comment(curr, TRUE, insert_space, &prev_was_comment);
            comments[t] = (int)(new_curr - curr);
            curr = new_curr;
         } else
            curr = skip_comment(curr, FALSE, insert_space, &prev_was_comment);
      }

      if (insert_space && t > 0) {
          curr = skipwhite(curr);
          if (*curr != ZERO && *curr != ')'
             && sumsize != 0 && endcurr1 != TAB
             && (!has_format_option(FO_MBYTE_JOIN)
                  || (mb_ptr2char(curr) < 0x100 && endcurr1 < 0x100))
             && (!has_format_option(FO_MBYTE_JOIN2)
            || (mb_ptr2char(curr) < 0x100
                && !(utf_eat_space(endcurr1)))
            || (endcurr1 < 0x100
                && !(utf_eat_space(mb_ptr2char(curr)))))
         ) {
            // don't add a space if the line is ending in a space
            if (endcurr1 == ' ')
               endcurr1 = endcurr2;
            else
               ++spaces[t];
         }
      }
      currsize = (int)STRLEN(curr);
      sumsize += currsize + spaces[t];
      endcurr1 = endcurr2 = ZERO;
      if (insert_space && currsize > 0) {
         cend = curr + currsize;
         MB_PTR_BACK(curr, cend);
         endcurr1 = (*mb_ptr2char)(cend);
         if (cend > curr) {
            MB_PTR_BACK(curr, cend);
            endcurr2 = (*mb_ptr2char)(cend);
         }
      }
      line_breakcheck();
      if (gotInterruptG) {
          ret = FAIL;
          goto theend;
      }
   }

   // store the column position before last line
   col = sumsize - currsize - spaces[count - 1];

   // allocate the space for the new line
   Unt newp_len = sumsize + 1;
   newp_len += propcount * sizeof(TextProp);
   CS newp = alloc(newp_len);
   cend = newp + sumsize;
   *cend = 0;

   //Move affected lines to the new long one.
   //This loops backwards over the joined lines, including the original line.
   //
   //Move marks from each deleted line to the joined line, adjusting the
   //column.  This is not Vi compatible, but Vi deletes the marks, thus that
   //should not really be a problem.
   props_remaining = propcount;
   for (t = count - 1; ; --t) {
      int spaces_removed;

      cend -= currsize;
      mch_memmove(cend, curr, (Unt)currsize);

      if (spaces[t] > 0) {
         cend -= spaces[t];
         memset(cend, ' ', (Unt)(spaces[t]));
      }

      // If deleting more spaces than adding, the cursor moves no more than
      // what is added if it is inside these spaces.
      spaces_removed = (curr - curr_start) - spaces[t];

      mark_col_adjust(curPor->cursor.lnum + t, (ColNr)0, -t,
             (long)(cend - newp - spaces_removed), spaces_removed);
      prepend_joined_props(
         newp + sumsize + 1, propcount, &props_remaining,
         curPor->cursor.lnum + t, t == count - 1,
         (long)(cend - newp), spaces_removed
      );
      if (t == 0)
         break;
      curr = curr_start = ml_get((LineNr)(curPor->cursor.lnum + t - 1));
      if (remove_comments)
         curr += comments[t - 1];
      if (insert_space && t > 1)
         curr = skipwhite(curr);
      currsize = (int)STRLEN(curr);
   }

   ml_replace_len(curPor->cursor.lnum, newp, (ColNr)newp_len, TRUE, FALSE);

   if (setmark && (commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
      // Set the '] mark.
      curPor->book->opEnd.lnum = curPor->cursor.lnum;
      curPor->book->opEnd.col  = (ColNr)sumsize;
   }

   // Only report the change in the first line here, del_lines() will report
   // the deleted line.
   changed_lines(curPor->cursor.lnum, currsize, curPor->cursor.lnum + 1, 0L);
   //Delete following lines. To do this we move the cursor there
   //briefly, and then move it back. After del_lines() the cursor may
   //have moved up (last line deleted), so the current lnum is kept in t.
   t = curPor->cursor.lnum;
   ++curPor->cursor.lnum;
   del_lines(count - 1, FALSE);
   curPor->cursor.lnum = t;

   //Set the cursor column: use the column of the last join
   curPor->cursor.col = col;
   check_cursor_col();

   curPor->cursor.coladd = 0;
   curPor->setCursWant = true;

theend:
   eeglFree(spaces);
   if (remove_comments)
      eeglFree(comments);
   return ret;
}

//prepare a few things for block mode yank/delete/tilde
//
//for delete:
//- textlen includes the first/last char to be (partly) deleted
//- start/endspaces is the number of columns that are taken by the
//  first/last deleted char minus the number of columns that have to be deleted.
//for yank and tilde:
//- textlen includes the first/last char to be wholly yanked
//- start/endspaces is the number of columns of the first/last yanked char that are to be yanked.
void
block_prep(
   Operator* oper,
   OUT BlockDef* bdp,
   LineNr lnum,
   Boole is_del
) {
   int incr = 0;
   CS pend;
   CS prev_pend;
   CharTableSize cts;
      // Avoid a problem with unwanted linebreaks in block mode.

   bdp->startspaces = 0;
   bdp->endspaces = 0;
   bdp->textlen = 0;
   bdp->start_vcol = 0;
   bdp->end_vcol = 0;
   bdp->is_short = FALSE;
   bdp->is_oneChar = FALSE;
   bdp->pre_whitesp = 0;
   bdp->pre_whitesp_c = 0;
   bdp->end_char_vcols = 0;
   bdp->start_char_vcols = 0;

   CS line = ml_get(lnum);
   CS prev_pstart = line;
   bookInitCharsForKeywordsSizeArg(&cts, curPor, lnum, bdp->start_vcol, line, line);
   while (cts.cts_vcol < oper->start_vcol && *cts.cts_ptr != ZERO) {
   // Count a tab for what it's worth (if list mode not on)
   incr = lbr_chartabsize(&cts);
   cts.cts_vcol += incr;
   if (SPACE_OR_TAB(*cts.cts_ptr)) {
      bdp->pre_whitesp += incr;
      bdp->pre_whitesp_c++;
   } else {
      bdp->pre_whitesp = 0;
      bdp->pre_whitesp_c = 0;
   }
   prev_pstart = cts.cts_ptr;
   MB_PTR_ADV(cts.cts_ptr);
   }
   bdp->start_vcol = cts.cts_vcol;
   CS pstart = cts.cts_ptr;
   clear_chartabsize_arg(&cts);

   bdp->start_char_vcols = incr;
   if (bdp->start_vcol < oper->start_vcol) {  // line too short
      bdp->end_vcol = bdp->start_vcol;
      bdp->is_short = TRUE;
      if (!is_del || oper->opTy == OP_APPEND)
         bdp->endspaces = oper->end_vcol - oper->start_vcol + 1;
   } else {
      // notice: this converts partly selected Multibyte characters to spaces, too.
      bdp->startspaces = bdp->start_vcol - oper->start_vcol;
      if (is_del && bdp->startspaces)
          bdp->startspaces = bdp->start_char_vcols - bdp->startspaces;
      pend = pstart;
      bdp->end_vcol = bdp->start_vcol;
      if (bdp->end_vcol > oper->end_vcol) {  // it's all in one character
         bdp->is_oneChar = TRUE;
         if (oper->opTy == OP_INSERT)
            bdp->endspaces = bdp->start_char_vcols - bdp->startspaces;
         ei (oper->opTy == OP_APPEND) {
            bdp->startspaces += oper->end_vcol - oper->start_vcol + 1;
            bdp->endspaces = bdp->start_char_vcols - bdp->startspaces;
         } else {
            bdp->startspaces = oper->end_vcol - oper->start_vcol + 1;
            if (is_del && oper->opTy != OP_LSHIFT) {
               // just putting the sum of those two into bdp->startspaces doesn't work for Visual 
               // replace, so we have to split the tab in two
               bdp->startspaces = bdp->start_char_vcols - (bdp->start_vcol - oper->start_vcol);
               bdp->endspaces = bdp->end_vcol - oper->end_vcol - 1;
            }
         }
      } else {
         bookInitCharsForKeywordsSizeArg(&cts, curPor, lnum, bdp->end_vcol, line, pend);
         prev_pend = pend;
         while (cts.cts_vcol <= oper->end_vcol && *cts.cts_ptr != ZERO) {
            // count a tab for what it's worth (if list mode not on)
            prev_pend = cts.cts_ptr;
            incr = lbr_chartabsize_adv(&cts);
            cts.cts_vcol += incr;
         }
         bdp->end_vcol = cts.cts_vcol;
         pend = cts.cts_ptr;
         clear_chartabsize_arg(&cts);

         if (bdp->end_vcol <= oper->end_vcol
             && (!is_del
               || oper->opTy == OP_APPEND
               || oper->opTy == OP_REPLACE) // line too short
         ){
            bdp->is_short = TRUE;
            // Alternative: include spaces to fill up the block. Disadvantage: can lead to 
            // trailing spaces when the line is short where the text is put
            // if (!is_del || oper->opTy == OP_APPEND)
            if (oper->opTy == OP_APPEND || virtual_op)
                bdp->endspaces = oper->end_vcol - bdp->end_vcol
                                + oper->inclusive;
            else
                bdp->endspaces = 0; // replace doesn't add characters
         } ei (bdp->end_vcol > oper->end_vcol) {
            bdp->endspaces = bdp->end_vcol - oper->end_vcol - 1;
            if (!is_del && bdp->endspaces) {
                bdp->endspaces = incr - bdp->endspaces;
                if (pend != pstart)
               pend = prev_pend;
            }
         }
      }
      bdp->end_char_vcols = incr;
      if (is_del && bdp->startspaces)
         pstart = prev_pstart;
      bdp->textlen = (int)(pend - pstart);
   }
   bdp->textcol = (ColNr) (pstart - line);
   bdp->textstart = pstart;
}

//Get block text from "start" to "end"
void
jugCharwiseBlockPrep(
   Pos start,
   Pos end,
   BlockDef* bdp,
   LineNr lnum,
   int inclusive
) {
   ColNr startcol = 0, endcol = MAXCOL;
   ColNr cs, ce;
   int   plen = ml_get_len(lnum);

   CS p = ml_get(lnum);
   bdp->startspaces = 0;
   bdp->endspaces = 0;
   bdp->is_oneChar = FALSE;
   bdp->start_char_vcols = 0;

   if (lnum == start.lnum) {
      startcol = start.col;
      if (virtual_op) {
         getvcol(curPor, &start, &cs, NULL, &ce);
         if (ce != cs && start.coladd > 0) {
            // Part of a tab selected -- but don't double-count it.
            bdp->start_char_vcols = ce - cs + 1;
            bdp->startspaces = bdp->start_char_vcols - start.coladd;
            if (bdp->startspaces < 0)
                bdp->startspaces = 0;
            startcol++;
         }
      }
   }

   if (lnum == end.lnum) {
      endcol = end.col;
      if (virtual_op) {
         getvcol(curPor, &end, &cs, NULL, &ce);
         if (p[endcol] == ZERO || (cs + end.coladd < ce
            // Don't add space for double-wide char; endcol will be on last byte of multi-byte char
            && (*mb_head_off)(p, p + endcol) == 0))
          {
         if (start.lnum == end.lnum && start.col == end.col) {
             // Special case: inside a single char
             bdp->is_oneChar = TRUE;
             bdp->startspaces = end.coladd - start.coladd + inclusive;
             endcol = startcol;
         } else {
             bdp->endspaces = end.coladd + inclusive;
             endcol -= inclusive;
         }
          }
      }
    }
   if (endcol == MAXCOL)
      endcol = ml_get_len(lnum);
   if (startcol > endcol || bdp->is_oneChar)
      bdp->textlen = 0;
   else
      bdp->textlen = endcol - startcol + inclusive;
   bdp->textcol = startcol;
   bdp->textstart = startcol <= plen ? p + startcol : p;
}

//Handle the add/subtract operator.
void
op_addsub(
   Operator* oper,
   LineNr prenum1,       // Amount of add/subtract
   int g_cmd          // was g<c-a>/g<c-x>
){
   Pos pos;
   BlockDef bd;
   int change_cnt = 0;
   LineNr amount = prenum1;

   // do_addsub() might trigger re-evaluation of 'foldexpr' halfway, when the
   // book is not completely updated yet. Postpone updating folds until before
   // the call to changed_lines().
   disable_fold_update++;

   if (!VIsual_active) {
      pos = curPor->cursor;
      if (u_save_cursor() == FAIL) {
         disable_fold_update--;
         return;
      }
      change_cnt = do_addsub(oper->opTy, &pos, 0, amount);
      disable_fold_update--;
      if (change_cnt)
         changed_lines(pos.lnum, 0, pos.lnum + 1, 0L);
   } else {
      int   one_change;
      int   length;
      Pos   startpos;

      if (u_save((LineNr)(oper->start.lnum - 1), (LineNr)(oper->end.lnum + 1)) == FAIL) {
         disable_fold_update--;
         return;
      }

      pos = oper->start;
      for (; pos.lnum <= oper->end.lnum; ++pos.lnum) {
         if (oper->block_mode) {         // Visual block mode
            block_prep(oper, OUT &bd, pos.lnum, false);
            pos.col = bd.textcol;
            length = bd.textlen;
         } ei (oper->motion_type == MLINE) {
            curPor->cursor.col = 0;
            pos.col = 0;
            length = ml_get_len(pos.lnum);
         } else {// oper->motion_type == MCHAR
            if (pos.lnum == oper->start.lnum && !oper->inclusive)
                dec(&(oper->end));
            length = ml_get_len(pos.lnum);
            pos.col = 0;
            if (pos.lnum == oper->start.lnum) {
                pos.col += oper->start.col;
                length -= oper->start.col;
            }
            if (pos.lnum == oper->end.lnum) {
                length = ml_get_len(oper->end.lnum);
                if (oper->end.col >= length)
               oper->end.col = length - 1;
                length = oper->end.col - pos.col + 1;
            }
         }
         one_change = do_addsub(oper->opTy, &pos, length, amount);
         if (one_change) {
            // Remember the start position of the first change.
            if (change_cnt == 0)
               startpos = curBook->opStart;
            ++change_cnt;
         }

         if (g_cmd && one_change)
            amount += prenum1;
      }

      disable_fold_update--;
      if (change_cnt)
         changed_lines(oper->start.lnum, 0, oper->end.lnum + 1, 0L);

      if (!change_cnt && oper->is_VIsual)
         // No change: need to remove the Visual selection
         drawCurBookLater(UPD_INVERTED);

      // Set '[ mark if something changed. Keep the last end
      // position from do_addsub().
      if (change_cnt > 0 && (commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0)
         curBook->opStart = startpos;

      smsg(NGETTEXT("%d line changed", "%d lines changed", change_cnt), change_cnt);
   }
}

//Add or subtract 'prenum1' from a number in a line opTy is OP_ADD or OP_SUB
//Return TRUE if some character was changed.
private int
do_addsub(
   int opTy,
   Pos* pos,
   int length,
   LineNr prenum1
){
   int      col;
   int      pre;      // 'X'/'x': hex; 'B'/'b': bin
   static int   hexupper = FALSE;   // 0xABC
   ULong   n;
   ULong   oldn;
   Byte   *ptr;
   int      linelen;
   int      c;
   int      todel;
   int      firstdigit;
   int      subtract;
   int      negative = FALSE;
   int      was_positive = TRUE;
   int      visual = VIsual_active;
   int      didChange = FALSE;
   Pos   save_cursor = curPor->cursor;
   int      maxlen = 0;
   Pos   startpos;
   Pos   endpos;
   ColNr   save_coladd = 0;

   if (virtual_active()) {
      save_coladd = pos->coladd;
      pos->coladd = 0;
   }

   curPor->cursor = *pos;
   ptr = ml_get(pos->lnum);
   linelen = ml_get_len(pos->lnum);
   col = pos->col;

   if (col + !!save_coladd >= linelen)
      goto theend;

   //First check if we are on a hexadecimal number, after the "0x".
   if (!VIsual_active) {
      while (col > 0 && eeIsXDigit(ptr[col])) {
         --col;
         col -= mb_head_off(ptr, ptr + col);
      }

      if (( col > 0
         && (ptr[col] == 'X' || ptr[col] == 'x')
         && ptr[col - 1] == '0'
         && (!(*mb_head_off)(ptr, ptr + col - 1))
         && eeIsXDigit(ptr[col + 1])) 
      ){
         // Found hexadecimal or binary number, move to its start.
         --col;
         col -= (*mb_head_off)(ptr, ptr + col);
      } else {
         //Search forward and then backward to find the start of number.
         col = pos->col;

         while (ptr[col] != ZERO
                && !eeIsDigit(ptr[col])
                && !(ASCII_ISALPHA(ptr[col])))
            col += utfCharLen(ptr + col);

         while (col > 0
             && eeIsDigit(ptr[col - 1])
             && !(ASCII_ISALPHA(ptr[col]))
         ) {
            --col;
             col -= (*mb_head_off)(ptr, ptr + col);
         }
      }
   }
   if (visual) {
      while (ptr[col] != ZERO && length > 0
               && !eeIsDigit(ptr[col])
               && !(ASCII_ISALPHA(ptr[col]))) {
          int mb_len = utfCharLen(ptr + col);
          col += mb_len;
          length -= mb_len;
      }

      if (length == 0)
         goto theend;

   }

   //If a number was found, and saving for undo works, replace the number.
   firstdigit = ptr[col];
   if (!EE_ISDIGIT(firstdigit) && !(ASCII_ISALPHA(firstdigit))) {
      beep_flush();
      goto theend;
   }

   if (ASCII_ISALPHA(firstdigit)) {
      // decrement or increment alphabetic character
      if (opTy == OP_SUB) {
         if (indexInLatinAlfabet(firstdigit) < prenum1) {
            if (SAFE_isupper(firstdigit))
               firstdigit = 'A';
            else
               firstdigit = 'a';
         } else
            firstdigit -= prenum1;
      } else {
         if (26 - indexInLatinAlfabet(firstdigit) - 1 < prenum1) {
            if (SAFE_isupper(firstdigit))
               firstdigit = 'Z';
            else
               firstdigit = 'z';
          } else
            firstdigit += prenum1;
      }
      curPor->cursor.col = col;
      if (!didChange)
         startpos = curPor->cursor;
      didChange = TRUE;
      (void)del_char(false);
      insertChar(firstdigit);
      endpos = curPor->cursor;
      curPor->cursor.col = col;
   } else {
      Byte   *buf1;
      int   buf1len;
      Byte   buf2[NUMBUFLEN];
      int   buf2len;
      Pos   save_pos;
      int   i;

      // get the number value (unsigned)
      if (visual && VIsual_mode != 'V')
          maxlen = (curBook->visual.vi_curswant == MAXCOL ? linelen - col : length);

      Boole overflow = false;
      readLongNumber(
         ptr + col, &pre, &length, 0 + STR2NR_HEX, NULL, &n, maxlen, false, OUT &overflow
      );

      // ignore leading '-' for hex and bin numbers
      if (pre && negative) {
          ++col;
          --length;
          negative = FALSE;
      }
      // add or subtract
      subtract = FALSE;
      if (opTy == OP_SUB)
          subtract ^= TRUE;
      if (negative)
          subtract ^= TRUE;

      oldn = n;
      if (!overflow) { // if number is too big don't add/subtract
         if (subtract)
            n -= (ULong)prenum1;
         else
            n += (ULong)prenum1;
      }

      // handle wraparound for decimal numbers
      if (!pre) {
         if (subtract) {
            if (n > oldn) {
               n = 1 + (n ^ (ULong)-1);
               negative ^= TRUE;
            }
         } else {
            // add
            if (n < oldn) {
               n = (n ^ (ULong)-1);
               negative ^= TRUE;
            }
         }
         if (n == 0)
            negative = FALSE;
      }

      if (subtract)
         // sticking at zero.
         n = (ULong)0;
      else
         // sticking at 2^64 - 1.
         n = (ULong)(-1);
      negative = FALSE;

      if (visual && !was_positive && !negative && col > 0) {
         // need to remove the '-'
         col--;
         length++;
      }

      // Delete the old number.
      curPor->cursor.col = col;
      if (!didChange)
         startpos = curPor->cursor;
      didChange = TRUE;
      todel = length;
      c = gchar_cursor();
      //Don't include the '-' in the length, only the length of the part after it is kept the same
      if (c == '-')
         --length;

      save_pos = curPor->cursor;
      for (i = 0; i < todel; ++i) {
         if (c < 0x100 && SAFE_isalpha(c)) {
            if (SAFE_isupper(c))
               hexupper = TRUE;
            else
               hexupper = FALSE;
         }
         inc_cursor();
         c = gchar_cursor();
      }
      curPor->cursor = save_pos;

      //Prepare the leading characters in buf1[].
      //When there are many leading zeros it could be very long. Allocate a bit too much.
      buf1 = alloc(length + NUMBUFLEN);
      ptr = buf1;
      if (negative && (!visual || was_positive))
         *ptr++ = '-';
      if (pre) {
         *ptr++ = '0';
         --length;
      }
      if (pre == 'b' || pre == 'B' || pre == 'x' || pre == 'X') {
         *ptr++ = pre;
         --length;
      }

      // Put the number characters in buf2[].
      if (pre == 'b' || pre == 'B') {
         int bit = 0;
         int bits = sizeof(ULong) * 8;

         // leading zeros
         for (bit = bits; bit > 0; bit--) {
            if ((n >> (bit - 1)) & 0x1) 
               break;
         } 

         for (buf2len = 0; bit > 0 && buf2len < (NUMBUFLEN - 1); bit--)
            buf2[buf2len++] = ((n >> (bit - 1)) & 0x1) ? '1' : '0';

         buf2[buf2len] = ZERO;
      }
      ei (pre == 0)
          buf2len = eeSnprintf(buf2, NUMBUFLEN, "%llu", n);
      ei (pre == '0')
          buf2len = eeSnprintf(buf2, NUMBUFLEN, "%llo", n);
      ei (pre && hexupper)
          buf2len = eeSnprintf(buf2, NUMBUFLEN, "%llX", n);
      else
         buf2len = eeSnprintf(buf2, NUMBUFLEN, "%llx", n);
      length -= buf2len;

      //Adjust number of zeros to the new number of digits, so the total length of the number 
      //remains the same.
      if (firstdigit == '0') {
         while (length-- > 0)
            *ptr++ = '0';
      } 
      *ptr = ZERO;
      buf1len = (int)(ptr - buf1);

      STRCPY(buf1 + buf1len, buf2);
      buf1len += buf2len;

      // Insert just after the first character to be removed, so that any
      // text properties will be adjusted.  Then delete the old number afterwards.
      save_pos = curPor->cursor;
      if (todel > 0)
          inc_cursor();
      ins_str(buf1, (Unt)buf1len);      // insert the new number
      eeglFree(buf1);

      // del_char() will also mark line needing displaying
      if (todel > 0) {
          int bytes_after = ml_get_curline_len() - curPor->cursor.col;

          // Delete the one character before the insert.
          curPor->cursor = save_pos;
          (void)del_char(false);
          curPor->cursor.col = ml_get_curline_len() - bytes_after;
          --todel;
      }
      while (todel-- > 0)
          (void)del_char(false);

      endpos = curPor->cursor;
      if (didChange && curPor->cursor.col)
          --curPor->cursor.col;
   }

   if (didChange && (commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
      // set the '[ and '] marks
      curBook->opStart = startpos;
      curBook->opEnd = endpos;
      if (curBook->opEnd.col > 0)
          --curBook->opEnd.col;
   }

theend:
   if (visual)
      curPor->cursor = save_cursor;
   ei (didChange)
      curPor->setCursWant = true;
   ei (virtual_active())
      curPor->cursor.coladd = save_coladd;

   return didChange;
}

void
clear_oparg(Operator *oper) {
    CLEAR_POINTER(oper);
}

//Count the number of bytes, characters and "words" in a line.
//
//"Words" are counted by looking for boundaries between non-space and
//space characters.  (it seems to produce results that match 'wc'.)
//
//Return value is byte count; word count for the line is added to "*wc".
//Char count is added to "*cc".
//
//The function will only examine the first "limit" characters in the line, stopping if it 
//encounters an end-of-line (ZERO byte). In that case, eol_size will be added to the 
//character count to account for the size of the EOL character.
private Long
line_count_info(
    CS line,
    Long* wc,
    Long* cc,
    Long limit,
    int eol_size
) {
   Long i;
   Long words = 0;
   Long charCount = 0;
   Boole is_word = false;

   for (i = 0; i < limit && line[i] != ZERO; ) {
      if (is_word) {
         if (isSpace(line[i])) {
            words++;
            is_word = false;
         }
      } ei (!isSpace(line[i]))
         is_word = true;
      ++charCount;
      i += utfCharLen(line + i);
   }

   if (is_word)
      words++;
   *wc += words;

    // Add eol_size if the end of line was reached before hitting limit.
   if (i < limit && line[i] == ZERO) {
      i += eol_size;
      charCount += eol_size;
   }
   *cc += charCount;
   return i;
}

//Give some info about the position of the cursor (for "g CTRL-G").
//In Visual mode, give some info about the selected region.  (In this case,
//the *_count_cursor variables store running totals for the selection.)
//When "dict" is not NULL store the info there instead of showing it.
void
cursor_pos_info(Bag* dict) {
   CS p;
   Byte buf1[50];
   Byte buf2[40];
   LineNr lnum;
   Long byte_count = 0;
   Long byte_count_cursor = 0;
   Long char_count = 0;
   Long char_count_cursor = 0;
   Long word_count = 0;
   Long word_count_cursor = 0;
   int eol_size;
   Long last_check = 100000L;
   long line_count_selected = 0;
   Pos  min_pos, max_pos;
   Operator oparg;
   BlockDef bd;

   // Compute the length of the file in characters.
   if (curBook->mem.flags & ML_EMPTY) {
      if (!dict) {
         msg(_(no_lines_msg));
         return;
      }
   } else {
      eol_size = 1;

      if (VIsual_active) {
         if (LT_POS(VIsual, curPor->cursor)) {
            min_pos = VIsual;
            max_pos = curPor->cursor;
         } else {
            min_pos = curPor->cursor;
            max_pos = VIsual;
         }

         if (VIsual_mode == Ctrl_V) {
            CS saved_sbr = p_sbr;

            // Make @showbreak empty for a moment to get the correct size.
            p_sbr = null;
            oparg.is_VIsual = 1;
            oparg.block_mode = TRUE;
            oparg.opTy = OP_NOP;
            getvcols(curPor, &min_pos, &max_pos, &oparg.start_vcol, &oparg.end_vcol);
            p_sbr = saved_sbr;
            if (curPor->cursWant == MAXCOL)
                oparg.end_vcol = MAXCOL;
            // Swap the start, end vcol if needed
            if (oparg.end_vcol < oparg.start_vcol) {
                oparg.end_vcol += oparg.start_vcol;
                oparg.start_vcol = oparg.end_vcol - oparg.start_vcol;
                oparg.end_vcol -= oparg.start_vcol;
            }
         }
         line_count_selected = max_pos.lnum - min_pos.lnum + 1;
      }

      for (lnum = 1; lnum <= curBook->mem.lineCount; ++lnum) {
         // Check for a CTRL-C every 100000 characters.
         if (byte_count > last_check) {
            ui_breakcheck();
            if (gotInterruptG)
               return;
            last_check = byte_count + 100000L;
         }

         // Do extra processing for VIsual mode.
         if (VIsual_active && lnum >= min_pos.lnum && lnum <= max_pos.lnum) {
            CS s = NULL;
            long len = 0L;

            switch (VIsual_mode) {
            case Ctrl_V:
               virtual_op = virtual_active();
               block_prep(&oparg, OUT &bd, lnum, false);
               virtual_op = MAYBE;
               s = bd.textstart;
               len = (long)bd.textlen;
               break;
            case 'V':
               s = ml_get(lnum);
               len = MAXCOL;
               break;
            case 'v': {
               ColNr start_col = (lnum == min_pos.lnum) ? min_pos.col : 0;
               ColNr end_col = (lnum == max_pos.lnum) ? max_pos.col - start_col + 1 : MAXCOL;

               s = ml_get(lnum) + start_col;
               len = end_col;
            }
            break;
            }
            if (s) {
               byte_count_cursor += line_count_info(s, &word_count_cursor,
                        &char_count_cursor, len, eol_size);
               if (lnum == curBook->mem.lineCount && (long)STRLEN(s) < len)
                  byte_count_cursor -= eol_size;
            }
         } else {
            // In non-visual mode, check for the line the cursor is on
            if (lnum == curPor->cursor.lnum) {
               word_count_cursor += word_count;
               char_count_cursor += char_count;
               byte_count_cursor = byte_count +
               line_count_info(
                  ml_get(lnum), &word_count_cursor, &char_count_cursor, 
                  (Long)(curPor->cursor.col + 1), eol_size
               );
            }
         }
         // Add to the running totals
         byte_count += line_count_info(ml_get(lnum), &word_count, &char_count, (Long)MAXCOL, eol_size);
      }

      if (!dict) {
         if (VIsual_active) {
            if (VIsual_mode == Ctrl_V && curPor->cursWant < MAXCOL) {
                getvcols(curPor, &min_pos, &max_pos, &min_pos.col, &max_pos.col);
                eeSnprintf(buf1, sizeof(buf1), _("%ld Cols; "),
                   (long)(oparg.end_vcol - oparg.start_vcol + 1));
            } else
               buf1[0] = ZERO;

            if (char_count_cursor == byte_count_cursor && char_count == byte_count)
               eeSnprintf(
                   IObuff, IOSIZE,
                   _("Selected %s%ld of %ld Lines; %ld of %ld Words; %ld of %ld Bytes"),
                   buf1, line_count_selected,
                   (long)curBook->mem.lineCount,
                   word_count_cursor,
                   word_count,
                   byte_count_cursor,
                   byte_count
               );
            else
               eeSnprintf(
                   IObuff, IOSIZE,
                   _("Selected %s%ld of %ld Lines; %ld of %ld Words; %ld of %ld Chars; %ld of %ld Bytes"),
                   buf1, line_count_selected,
                   (long)curBook->mem.lineCount,
                   word_count_cursor,
                   word_count,
                   char_count_cursor,
                   char_count,
                   byte_count_cursor,
                   byte_count
               );
         } else {
            p = ml_get_curline();
            validate_virtcol();
            col_print(buf1, sizeof(buf1), (int)curPor->cursor.col + 1, (int)curPor->virtCol + 1);
            col_print(buf2, sizeof(buf2), ml_get_curline_len(), linetabsize_str(p));

            if (char_count_cursor == byte_count_cursor && char_count == byte_count)
               eeSnprintf(
                  IObuff, IOSIZE,
                  _("Col %s of %s; Line %ld of %ld; Word %ld of %ld; Byte %ld of %ld"),
                  buf1, buf2,
                  (long)curPor->cursor.lnum,
                  (long)curBook->mem.lineCount,
                  word_count_cursor, word_count,
                  byte_count_cursor, byte_count
               );
            else
               eeSnprintf(
                  IObuff, IOSIZE, 
                  _("Col %s of %s; Line %ld of %ld; Word %ld of %ld; Char %ld of %ld; "
                     "Byte %ld of %ld"
                  ),
                  buf1, buf2,
                  (long)curPor->cursor.lnum,
                  (long)curBook->mem.lineCount,
                  word_count_cursor, word_count,
                  char_count_cursor, char_count,
                  byte_count_cursor, byte_count
               );
         }
      }

      if (!dict) {
          msg(IObuff);
      }
   }
   if (dict) {
      bagAddNumber(dict, S"words", word_count);
      bagAddNumber(dict, S"chars", char_count);
      bagAddNumber(dict, S"bytes", byte_count);
      bagAddNumber(dict, VIsual_active ? S"visual_bytes" : S"cursor_bytes", byte_count_cursor);
      bagAddNumber(dict, VIsual_active ? S"visual_chars" : S"cursor_chars", char_count_cursor);
      bagAddNumber(dict, VIsual_active ? S"visual_words" : S"cursor_words", word_count_cursor);
   }
}

//Handle indent and format operators and visual mode ":".
private void
op_colon(Operator *oper) {
   stuffcharReadbuff(':');
   if (oper->is_VIsual)
      stuffReadbuff(S"'<,'>");
   else {
      // Make the range look nice, so it can be repeated.
      if (oper->start.lnum == curPor->cursor.lnum)
         stuffcharReadbuff('.');
      else
         stuffnumReadbuff((long)oper->start.lnum);

      // When using !! on a closed fold the range ".!" works best to operate
      // on, it will be made the whole closed fold later.
      LineNr endOfStartFold = oper->start.lnum;
      (void)getFolds(oper->start.lnum, NULL, &endOfStartFold);
      if (oper->end.lnum != oper->start.lnum && oper->end.lnum != endOfStartFold) {
         // Make it a range with the end line.
         stuffcharReadbuff(',');
         if (oper->end.lnum == curPor->cursor.lnum)
            stuffcharReadbuff('.');
         ei (oper->end.lnum == curBook->mem.lineCount)
            stuffcharReadbuff('$');
         ei (oper->start.lnum == curPor->cursor.lnum
             // do not use ".+number" for a closed fold, it would count folded lines twice
             && !getFolds(oper->end.lnum, NULL, NULL)
         ) {
            stuffReadbuff(S".+");
            stuffnumReadbuff((long)oper->line_count - 1);
         } else
            stuffnumReadbuff((long)oper->end.lnum);
      }
   }
   if (oper->opTy != OP_COLON)
      stuffReadbuff(S"!");
   if (oper->opTy == OP_INDENT) {
      stuffReadbuff(S"indent");
      stuffReadbuff(S"\n");
   } ei (oper->opTy == OP_FORMAT) {
      if (curBook->o.formatProg)
         stuffReadbuff(curBook->o.formatProg);
      ei (p_fp)
         stuffReadbuff(p_fp);
      else
         stuffReadbuff(S"fmt");
      stuffReadbuff((CS)"\n']");
   }

   // doCommand() does the rest
}

// callback function for 'operatorfunc'
private Callback opfunc_cb;

//Process the 'operatorfunc' option value. Return OK or FAIL.
CS
did_set_operatorfunc(OptionChange *cha) {
   if (optSetCallback(OUT &opfunc_cb, cha->newVal.string) == FAIL)
      return e_invalid_argument;

   return NULL;
}

#if defined(EXITFREE) || defined(PROTO)
void
opsFreeOperatorFnOption(void) {
   evFreeCallback(&opfunc_cb);
}
#endif

// Mark the global 'operatorfunc' callback with "copyID" so that it is not garbage collected.
int
set_ref_in_opfunc(int copyID) {
   return memSetRefInCallback(&opfunc_cb, copyID);
}

//Handle the "g@" operator: call 'operatorfunc'.
private void
op_function(Operator *oper UNUSED) {
   Var argv[2];
   Pos orig_start = curBook->opStart;
   Pos orig_end = curBook->opEnd;
   Var returnVar;

   if (!p_opfunc)
      emsg(_(e_operatorfunc_is_empty));
   else {
      // Set '[ and '] marks to text to be operated on.
      curBook->opStart = oper->start;
      curBook->opEnd = oper->end;
      if (oper->motion_type != MLINE && !oper->inclusive)
          // Exclude the end position.
          decl(&curBook->opEnd);

      argv[0].tag = VAR_STRING;
      if (oper->block_mode)
          argv[0].string = (CS)"block";
      ei (oper->motion_type == MLINE)
          argv[0].string = (CS)"line";
      else
          argv[0].string = (CS)"char";
      argv[1].tag = VAR_UNKNOWN;

      // Reset virtual_op so that 'virtualedit' can be changed in the
      // function.
      int save_virtual_op = virtual_op;
      virtual_op = MAYBE;

      // Reset finish_op so that mode() returns the right value.
      int save_finish_op = finish_op;
      finish_op = FALSE;

      if (call_callback(&opfunc_cb, 0, &returnVar, 1, argv) != FAIL)
          clearVar(&returnVar);

      virtual_op = save_virtual_op;
      finish_op = save_finish_op;
      if (commModifierG.cmod_flags & CMOD_LOCKMARKS) {
          curBook->opStart = orig_start;
          curBook->opEnd = orig_end;
      }
   }
}

//Calculate start/end virtual columns for operating in block mode.
private void
get_op_vcol(Operator* oper, ColNr redo_VIsual_vcol, int initial) { //adjust position for selectmode
   ColNr       start, end;

   if (VIsual_mode != Ctrl_V || (!initial && oper->end.col < (int)curPor->width))
      return;

   oper->block_mode = TRUE;

   // prevent from moving onto a trail byte
   mb_adjustpos(curPor->book, &oper->end);

   bookGetVirtualColInVirtualMode(curPor, &(oper->start), &oper->start_vcol, NULL, &oper->end_vcol);

   if (!isRedoVisualBusy) {
      bookGetVirtualColInVirtualMode(curPor, &(oper->end), &start, NULL, &end);

      if (start < oper->start_vcol)
         oper->start_vcol = start;
      if (end > oper->end_vcol) {
         oper->end_vcol = end;
      }
   }

   // if '$' was used, get oper->end_vcol from longest line
   if (curPor->cursWant == MAXCOL) {
      curPor->cursor.col = MAXCOL;
      oper->end_vcol = 0;
      for (
            curPor->cursor.lnum = oper->start.lnum; 
            curPor->cursor.lnum <= oper->end.lnum; 
            ++curPor->cursor.lnum
      ) {
         bookGetVirtualColInVirtualMode(curPor, &curPor->cursor, NULL, NULL, &end);
         if (end > oper->end_vcol)
            oper->end_vcol = end;
      }
   } ei (isRedoVisualBusy)
      oper->end_vcol = oper->start_vcol + redo_VIsual_vcol - 1;
   // Correct oper->end.col and oper->start.col to be the
   // upper-left and lower-right corner of the block area.
   //
   // (Actually, this does convert column positions into character positions)
   curPor->cursor.lnum = oper->end.lnum;
   coladvance(oper->end_vcol);
   oper->end = curPor->cursor;

   curPor->cursor = oper->start;
   coladvance(oper->start_vcol);
   oper->start = curPor->cursor;
}

// Information for redoing the previous Visual selection.
typedef struct {
   int mode;   // 'v', 'V', or Ctrl-V
   LineNr lineCount;   // number of lines
   ColNr vcol;   // number of cols or end column
   long count;   // count for Visual operator
   int extraArg;      // extra argument
} RedoVisual;

private int
is_ex_cmdchar(ActionArg* cap) {
   return cap->cmdchar == ':' || cap->cmdchar == K_COMMAND || cap->cmdchar == K_SCRIPT_COMMAND;
}

//Handle an operator after Visual mode or when the movement is finished.
//"clipbYank" is true when yanking text for the clipboard.
void
visualOperator(ActionArg* cap, int old_col, int clipbYank) {
   Operator* oper = cap->oper;
   Pos old_cursor;
   int restart_edit_save;

   //The visual area is remembered for redo
   static RedoVisual redo_VIsual = {ZERO, 0, 0, 0,0};

   //Yank the visual area into the GUI selection register before we operate
   //on it and lose it forever.
   //Don't do it if a specific register was specified, so that ""x"*P works.
   //This could call visualOperator() recursively, but that's OK
   //because clipbYank will be TRUE for the nested call.
   if (clipboard.available
          && oper->opTy != OP_NOP
          && !clipbYank
          && VIsual_active
          && !isRedoVisualBusy
          && oper->regname == 0
   )
      clip_auto_select();
   old_cursor = curPor->cursor;

   //If an operation is pending, handle it...
   if ((finish_op || VIsual_active) && oper->opTy != OP_NOP) {
      //Avoid a problem with unwanted linebreaks in block mode.
      oper->is_VIsual = VIsual_active;
      if (oper->motion_force == 'V')
          oper->motion_type = MLINE;
      ei (oper->motion_force == 'v') {
         // If the motion was linewise, "inclusive" will not have been set.
         // Use "exclusive" to be consistent.  Makes "dvj" work nice.
         if (oper->motion_type == MLINE)
            oper->inclusive = FALSE;
         // If the motion already was characterwise, toggle "inclusive"
         ei (oper->motion_type == MCHAR)
            oper->inclusive = !oper->inclusive;
         oper->motion_type = MCHAR;
      } ei (oper->motion_force == Ctrl_V) {
         // Change line- or characterwise motion into Visual block mode.
         if (!VIsual_active) {
            VIsual_active = TRUE;
            VIsual = oper->start;
         }
         VIsual_mode = Ctrl_V;
         VIsual_reselect = FALSE;
      }

      //Only redo yank when 'y' flag is in 'cpoptions'. Never redo "zf" (define fold).
      if (oper->opTy != OP_YANK
         && ((!VIsual_active || oper->motion_force)
             //Also redo Operator-pending Visual mode mappings
             || (VIsual_active && is_ex_cmdchar(cap) && oper->opTy != OP_COLON))
         && cap->cmdchar != 'D'
         && oper->opTy != OP_FOLD
         && oper->opTy != OP_FOLDOPEN
         && oper->opTy != OP_FOLDOPENREC
         && oper->opTy != OP_FOLDCLOSE
         && oper->opTy != OP_FOLDCLOSEREC
         && oper->opTy != OP_FOLDDEL
         && oper->opTy != OP_FOLDDELREC
      ) {
         prep_redo(oper->regname, cap->count0,
             get_op_char(oper->opTy), get_extra_op_char(oper->opTy),
             oper->motion_force, cap->cmdchar, cap->nchar);
         if (cap->cmdchar == '/' || cap->cmdchar == '?') {// was a search
            //Insert the search pattern to really repeat the same command.
            AppendToRedobuffLit(cap->searchbuf, -1);
            AppendToRedobuff(NL_STR);
         } ei (is_ex_cmdchar(cap)) {
            //doCommand() has stored the first typed line in "repeatCommlineG". When several lines 
            //are typed repeating won't be possible.
            if (repeatCommlineG == NULL)
                ResetRedobuff();
            else {
                if (cap->cmdchar == ':')
               AppendToRedobuffLit(repeatCommlineG, -1);
                else
               AppendToRedobuffSpec(repeatCommlineG);
                AppendToRedobuff(NL_STR);
                EE_CLEAR(repeatCommlineG);
            }
         }
      }

      if (isRedoVisualBusy) {
         // Redo of an operation on a Visual area. Use the same size from
         // redo_VIsual.lineCount and redo_VIsual.vcol.
         oper->start = curPor->cursor;
         curPor->cursor.lnum += redo_VIsual.lineCount - 1;
         if (curPor->cursor.lnum > curBook->mem.lineCount)
            curPor->cursor.lnum = curBook->mem.lineCount;
         VIsual_mode = redo_VIsual.mode;
         if (redo_VIsual.vcol == MAXCOL || VIsual_mode == 'v') {
            if (VIsual_mode == 'v') {
               if (redo_VIsual.lineCount <= 1) {
                  validate_virtcol();
                  curPor->cursWant = curPor->virtCol + redo_VIsual.vcol - 1;
               } else
                  curPor->cursWant = redo_VIsual.vcol;
            } else {
               curPor->cursWant = MAXCOL;
            }
            coladvance(curPor->cursWant);
         }
         cap->count0 = redo_VIsual.count;
         if (redo_VIsual.count != 0)
            cap->count1 = redo_VIsual.count;
         else
            cap->count1 = 1;
      } ei (VIsual_active) {
         if (!clipbYank) {
            // Save the current VIsual area for '< and '> marks, and "gv"
            curBook->visual.vi_start = VIsual;
            curBook->visual.vi_end = curPor->cursor;
            curBook->visual.vi_mode = VIsual_mode;
            restore_visual_mode();
            curBook->visual.vi_curswant = curPor->cursWant;
            curBook->visual.kind = VIsual_mode;
         }

         oper->start = VIsual;
         if (VIsual_mode == 'V') {
            oper->start.col = 0;
            oper->start.coladd = 0;
         }
      }

      // Set oper->start to the first position of the operated text, oper->end
      // to the end of the operated text. cursor is equal to oper->start.
      if (LT_POS(oper->start, curPor->cursor)) {
         // Include folded lines completely.
         if (!VIsual_active) {
            if (getFolds(oper->start.lnum, &oper->start.lnum, NULL))
               oper->start.col = 0;
            if ((curPor->cursor.col > 0 || oper->inclusive || oper->motion_type == MLINE)
                  && getFolds(curPor->cursor.lnum, NULL, &curPor->cursor.lnum)
            )
               curPor->cursor.col = ml_get_curline_len();
         }
         oper->end = curPor->cursor;
         curPor->cursor = oper->start;

         // virtCol may have been updated; if the cursor goes back to its
         // previous position virtCol becomes invalid and isn't updated automatically.
         curPor->cacheState &= ~VALID_VIRTCOL;
      } else {
         // Include folded lines completely.
         if (!VIsual_active && oper->motion_type == MLINE) {
            if (getFolds(curPor->cursor.lnum, &curPor->cursor.lnum, NULL))
               curPor->cursor.col = 0;
            if (getFolds(oper->start.lnum, NULL, &oper->start.lnum))
               oper->start.col = ml_get_len(oper->start.lnum);
         }
         oper->end = oper->start;
         oper->start = curPor->cursor;
      }

      // Just in case lines were deleted that make the position invalid.
      check_pos(curPor->book, &oper->end);
      oper->line_count = oper->end.lnum - oper->start.lnum + 1;

      // Set "virtual_op" before resetting VIsual_active.
      virtual_op = virtual_active();

      if (VIsual_active || isRedoVisualBusy) {
         get_op_vcol(oper, redo_VIsual.vcol, TRUE);

         if (!isRedoVisualBusy && !clipbYank) {
            // Prepare to reselect and redo Visual: this is based on the size of the Visual text
            resel_VIsual_mode = VIsual_mode;
            if (curPor->cursWant == MAXCOL)
                resel_VIsual_vcol = MAXCOL;
            else {
               if (VIsual_mode != Ctrl_V)
                  bookGetVirtualColInVirtualMode(curPor, &(oper->end), NULL, NULL, &oper->end_vcol);
               if (VIsual_mode == Ctrl_V || oper->line_count <= 1) {
                  if (VIsual_mode != Ctrl_V)
                      bookGetVirtualColInVirtualMode(curPor, &(oper->start),
                           &oper->start_vcol, NULL, NULL);
                  resel_VIsual_vcol = oper->end_vcol - oper->start_vcol + 1;
               } else
                  resel_VIsual_vcol = oper->end_vcol;
            }
            resel_VIsual_line_count = oper->line_count;
         }

         // can't redo yank (unless 'y' is in 'cpoptions') and ":"
         if (oper->opTy != OP_YANK
             && oper->opTy != OP_COLON
             && oper->opTy != OP_FOLD
             && oper->opTy != OP_FOLDOPEN
             && oper->opTy != OP_FOLDOPENREC
             && oper->opTy != OP_FOLDCLOSE
             && oper->opTy != OP_FOLDCLOSEREC
             && oper->opTy != OP_FOLDDEL
             && oper->opTy != OP_FOLDDELREC
             && oper->motion_force == ZERO
         ) {
            // Prepare for redoing.  Only use the nchar field for "r",
            // otherwise it might be the second char of the operator.
            if (cap->cmdchar == 'g' && (cap->nchar == 'n' || cap->nchar == 'N'))
                prep_redo(oper->regname, cap->count0,
                   get_op_char(oper->opTy),
                   get_extra_op_char(oper->opTy),
                   oper->motion_force, cap->cmdchar, cap->nchar);
            ei (!is_ex_cmdchar(cap)) {
               int opchar = get_op_char(oper->opTy);
               int extra_opchar = get_extra_op_char(oper->opTy);
               Unt nchar = oper->opTy == OP_REPLACE ? cap->nchar : ZERO;

               // reverse what nv_replace() did
               if (nchar == REPLACE_CR_NCHAR)
                  nchar = ENTER;
               ei (nchar == REPLACE_NL_NCHAR)
                  nchar = NL;

               if (opchar == 'g' && extra_opchar == '@')
                  // also repeat the count for 'operatorfunc'
                  prep_redo_num2(oper->regname, 0L, ZERO, 'v',
                          cap->count0, opchar, extra_opchar, nchar);
               else
                  prep_redo(oper->regname, 0L, ZERO, 'v', opchar, extra_opchar, nchar);
            }
            if (!isRedoVisualBusy) {
               redo_VIsual.mode = resel_VIsual_mode;
               redo_VIsual.vcol = resel_VIsual_vcol;
               redo_VIsual.lineCount = resel_VIsual_line_count;
               redo_VIsual.count = cap->count0;
               redo_VIsual.extraArg = cap->arg;
            }
         }

         //oper->inclusive defaults to TRUE.
         //If oper->end is on a ZERO (empty line) oper->inclusive becomes
         //FALSE.  This makes "d}P" and "v}dP" work the same.
         if (oper->motion_force == ZERO || oper->motion_type == MLINE)
            oper->inclusive = TRUE;
         if (VIsual_mode == 'V')
            oper->motion_type = MLINE;
         else {
            oper->motion_type = MCHAR;
            if (VIsual_mode != Ctrl_V && *ml_get_pos(&(oper->end)) == ZERO && (!virtual_op)) {
               oper->inclusive = FALSE;
               //Try to include the newline, unless it's an operator that works on lines only.
               if (!op_on_lines(oper->opTy) && oper->end.lnum < curBook->mem.lineCount) {
                  ++oper->end.lnum;
                  oper->end.col = 0;
                  oper->end.coladd = 0;
                  ++oper->line_count;
               }
            }
         }

         isRedoVisualBusy = FALSE;

         //Switch Visual off now, so screen updating does not show inverted text when the screen 
         //is redrawn. With OP_YANK and sometimes with OP_COLON and OP_FILTER there is
         //no screen redraw, so it is done here to remove the inverted part.
         if (!clipbYank) {
            VIsual_active = FALSE;
            setmouse();
            mouseDraggingG = 0;
            may_clear_cmdline();
            if ((oper->opTy == OP_YANK
                   || oper->opTy == OP_COLON
                   || oper->opTy == OP_FUNCTION
                   || oper->opTy == OP_FILTER)
               && oper->motion_force == ZERO
            ){
               drawCurBookLater(UPD_INVERTED);
            }
         }
      }

      // Include the trailing byte of a multi-byte char.
      if (oper->inclusive) {
         int l = utfCharLen(ml_get_pos(&oper->end));
         if (l > 1)
            oper->end.col += l - 1;
      }
      curPor->setCursWant = true;

      //oper->empty is set when start and end are the same.  The inclusive
      //flag affects this too, unless yanking and the end is on a ZERO.
      oper->empty = (oper->motion_type == MCHAR
             && (!oper->inclusive || (oper->opTy == OP_YANK && gchar_pos(&oper->end) == ZERO))
             && EQUAL_POS(oper->start, oper->end)
             && !(virtual_op && oper->start.coladd != oper->end.coladd)
      );

      //Force a redraw when operating on an empty Visual region, when
      //@modifiable is off or creating a fold.
      if (oper->is_VIsual && (oper->empty || !curBook->o.modifiable || oper->opTy == OP_FOLD)) {
          drawCurBookLater(UPD_INVERTED);
      }

      //If the end of an operator is in column one while oper->motion_type is MCHAR and 
      //oper->inclusive is FALSE, we put op_end after the last character in the previous line. If 
      //op_start is on or before the first non-blank in the line, the operator becomes linewise
      //(strange, but that's the way vi does it).
      if (  oper->motion_type == MCHAR
         && oper->inclusive == FALSE
         && !(cap->retval & CA_NO_ADJ_OP_END)
         && oper->end.col == 0
         && (!oper->is_VIsual)
         && !oper->block_mode
         && oper->line_count > 1
      ) {
         oper->end_adjusted = TRUE;       // remember that we did this
         --oper->line_count;
         --oper->end.lnum;
         if (inindent(0))
            oper->motion_type = MLINE;
         else {
            oper->end.col = ml_get_len(oper->end.lnum);
            if (oper->end.col) {
               --oper->end.col;
               oper->inclusive = TRUE;
            }
         }
      } else
         oper->end_adjusted = FALSE;

      switch (oper->opTy) {
      case OP_LSHIFT:
      case OP_RSHIFT:
         op_shift(oper, TRUE, oper->is_VIsual ? (int)cap->count1 : 1);
         auto_format(FALSE, TRUE);
         break;

      case OP_JOIN_NS:
      case OP_JOIN:
         if (oper->line_count < 2)
            oper->line_count = 2;
         if (curPor->cursor.lnum + oper->line_count - 1 > curBook->mem.lineCount)
            beep_flush();
         else {
            (void)jugJoinLinesUnderCursor(oper->line_count, oper->opTy == OP_JOIN, TRUE, TRUE, TRUE);
            auto_format(FALSE, TRUE);
         }
         break;

      case OP_DELETE:
         VIsual_reselect = FALSE;       // don't reselect now
         (void)op_delete(oper);
         // save cursor line for undo if it wasn't saved yet
         if (oper->motion_type == MLINE && has_format_option(FO_AUTO) && u_save_cursor() == OK)
            auto_format(FALSE, TRUE);
         break;

      case OP_YANK:
          oper->excludeTrailingWhitespace = cap->cmdchar == 'z';
          (void)op_yank(oper, FALSE, !clipbYank);
          check_cursor_col();
          break;

      case OP_CHANGE:
         VIsual_reselect = FALSE;       // don't reselect now
         //This is a new edit command, not a restart.  Need to
         //remember it to make 'insertmode' work with mappings for
         //Visual mode.  But do this only once and not when typed and 'insertmode' isn't set.
         if (!KeyTyped)
            restart_edit_save = restart_edit;
         else
            restart_edit_save = 0;
         restart_edit = 0;
         //trigger TextChangedI
         curBook->lastChangeTickInsert = CHANGEDTICK(curBook);

         if (op_change(oper))   // will call edit()
            cap->retval |= CA_COMMAND_BUSY;
         if (restart_edit == 0)
            restart_edit = restart_edit_save;
         break;

      case OP_FILTER:
         bangredo = TRUE;    // do_bang() will put cmd in redo buffer
         // FALLTHROUGH

      case OP_INDENT:
      case OP_COLON:
         if (oper->opTy == OP_INDENT) {
            op_reindent(oper, *curBook->o.indentExpr != ZERO ? &get_expr_indent : null);
            break;
         }

         op_colon(oper);
         break;

      case OP_TILDE:
      case OP_UPPER:
      case OP_LOWER:
      case OP_ROT13:
          op_tilde(oper);
          check_cursor_col();
          break;

      case OP_FORMAT:
         if (curBook->o.formatExpr)
            op_formatexpr(oper);   // use expression
         else {
            if (p_fp || curBook->o.formatProg)
               op_colon(oper);      // use external command
            else
               op_format(oper, FALSE);   // use internal function
         }
         break;
      case OP_FORMAT2:
         op_format(oper, TRUE);   // use internal function
         break;

      case OP_FUNCTION: {
         RedoVisual save_redo_VIsual = redo_VIsual;

         // call 'operatorfunc'
         op_function(oper);

         // Restore the info for redoing Visual mode, the function may
         // invoke another operator and unintentionally change it.
         redo_VIsual = save_redo_VIsual;
         break;
      }

      case OP_INSERT:
      case OP_APPEND:
         VIsual_reselect = FALSE;   // don't reselect now
         // This is a new edit command, not a restart.  Need to
         // remember it to make 'insertmode' work with mappings for
         // Visual mode.  But do this only once.
         restart_edit_save = restart_edit;
         restart_edit = 0;
         // trigger TextChangedI
         curBook->lastChangeTickInsert = CHANGEDTICK(curBook);

         op_insert(oper, cap->count1);

         // TODO: when inserting in several lines, should format all the lines.
         auto_format(FALSE, TRUE);

         if (restart_edit == 0)
            restart_edit = restart_edit_save;
         else
            cap->retval |= CA_COMMAND_BUSY;
         break;

      case OP_REPLACE:
         VIsual_reselect = FALSE;   // don't reselect now
         op_replace(oper, cap->nchar);
         break;

      case OP_FOLD:
         VIsual_reselect = FALSE;   // don't reselect now
         foldCreate(oper->start.lnum, oper->end.lnum);
         break;

      case OP_FOLDOPEN:
      case OP_FOLDOPENREC:
      case OP_FOLDCLOSE:
      case OP_FOLDCLOSEREC:
         VIsual_reselect = FALSE;   // don't reselect now
         opFoldRange(
            oper->start.lnum, oper->end.lnum,
            oper->opTy == OP_FOLDOPEN || oper->opTy == OP_FOLDOPENREC,
            oper->opTy == OP_FOLDOPENREC || oper->opTy == OP_FOLDCLOSEREC,
            oper->is_VIsual
         );
         break;

      case OP_FOLDDEL:
      case OP_FOLDDELREC:
         VIsual_reselect = FALSE;   // don't reselect now
         deleteFold(oper->start.lnum, oper->end.lnum,
                   oper->opTy == OP_FOLDDELREC, oper->is_VIsual);
         break;
      case OP_ADD:
      case OP_SUB:
         VIsual_active = TRUE;
         op_addsub(oper, cap->count1, redo_VIsual.extraArg);
         VIsual_active = FALSE;
         check_cursor_col();
         break;
      default:
         clearopbeep(oper);
      }
      virtual_op = MAYBE;
      if (!clipbYank) {
         // if 'sol' not set, go back to old column for some commands
         if (!p_sol && oper->motion_type == MLINE && !oper->end_adjusted
             && (oper->opTy == OP_LSHIFT || oper->opTy == OP_RSHIFT
                     || oper->opTy == OP_DELETE)
         ) {
             coladvance(curPor->cursWant = old_col);
         }
      } else {
         curPor->cursor = old_cursor;
      }
      oper->block_mode = FALSE;
      clearop(oper);
      motion_force = ZERO;
    }
}

// put byte 'c' at position 'lp', but
// verify, that the position to place is actually safe
private void
pbyte(Pos lp, int c) {
   CS p = memGetLine(curBook, lp.lnum, TRUE);
   int len = curBook->mem.lineLen;

   // safety check
   if (lp.col >= len) {
      lp.col = (len > 1 ? len - 2 : 0);
   } 
   *(p + lp.col) = c;
}

//flush map and typeahead buffers and give a warning for an error
void
beep_flush(void) {
   if (emsg_silent == 0) {
      flush_buffers(FLUSH_MINIMAL);
   }
}

//}}}
//{{{time

#include <time.h>

//Cache of the current timezone name as retrieved from TZ, or an empty string
//where unset, up to 64 octets long including trailing null byte.
private Byte   tz_cache[64];

#define FOR_ALL_TIMERS(t) \
    for ((t) = firstTimerS; (t) != NULL; (t) = (t)->next)
    
typedef struct tm Tm; 

//Call either localtime(3) or localtime_r(3) from POSIX libc time.h, with the
//latter version preferred for reentrancy.
//
//If we use localtime_r(3) and we have tzset(3) available, check to see if the environment variable 
//TZ has changed since the last run, and call tzset(3) to update the global timezone variables if 
//it has.  This is because the POSIX standard doesn't require localtime_r(3) implementations to do 
//that as it does with localtime(3), and we don't want to call tzset(3) every time.
private Tm *
eeLocaltime(
   Tyme const* timep,      // timestamp for local representation
   OUT Tm* result // pointer to caller return buffer
){
   CS tz = mch_getenv(S"TZ");      // pointer for TZ environment var
   if (tz == NULL)
      tz = Em;
   if (STRNCMP(tz_cache, tz, sizeof(tz_cache) - 1) != 0) {
      tzset();
      copySubstrToAllocation((CS)tz_cache, (Text){tz, sizeof(tz_cache) - 1});
   }
   return localtime_r(timep, result);
}

// Return the current time in seconds.  Calls time(), unless test_settime() was used.
Tyme
eeTime(void) {
   return time_for_testing == 0 ? time(NULL) : time_for_testing;
}

//Replacement for ctime(), which is not safe to use.
//Requires strftime(), otherwise returns "(unknown)".
//If "thetime" is invalid returns "(invalid)".  Never returns NULL.
//When "add_newline" is TRUE add a newline like ctime() does. Use a static buffer.
CS
get_ctime(Tyme thetime, int add_newline) {
   static Byte buf[100];  // hopefully enough for every language
   Tm tmval;
   Tm* curtime = eeLocaltime(&thetime, &tmval);
   if (!curtime)
      copySubstrToAllocation(buf, (Text){_("(Invalid)"), sizeof(buf) - 2});
   else {
      // xgettext:no-c-format
      if (STRFTIME(buf, sizeof(buf) - 2, _("%a %b %d %H:%M:%S %Y"), curtime) == 0) {
         // Quoting "man strftime":
         // > If the length of the result string (including the terminating
         // > null byte) would exceed max bytes, then strftime() returns 0,
         // > and the contents of the array are undefined.
         copySubstrToAllocation((CS)buf, (Text){_("(Invalid)"), sizeof(buf) - 2});
      }
   }
   if (add_newline)
      STRCAT(buf, "\n");
   return buf;
}


// "localtime()" function
void
f_localtime(Arr(Var) argVars UNUSED, OUT Var* returnVar) {
   returnVar->number = (Long)time(NULL);
}

// Convert a List to ProfTime. Return FAIL when there is something wrong.
private int
list2proftime(Var *arg, ProfTime *tm) {
   if (arg->tag != VAR_LIST || arg->list == NULL || arg->list->len != 2)
      return FAIL;
      
   Boole error = false;
   long n1 = list_find_nr(arg->list, 0L, &error);
   long n2 = list_find_nr(arg->list, 1L, &error);
   tm->tv_sec = n1;
   tm->tv_fsec = n2;
   return error ? FAIL : OK;
}


// "reltime()" function
void
f_reltime(Arr(Var) argVars, OUT Var* returnVar UNUSED) {
   ProfTime   res;
   ProfTime   start;

   allocReturnList(returnVar);

   if (argVars[0].tag == VAR_UNKNOWN) {
      // No arguments: get current time.
      profile_start(&res);
   } ei (argVars[1].tag == VAR_UNKNOWN) {
      if (list2proftime(&argVars[0], &res) == FAIL) {
         return;
      }
      profile_end(&res);
   } else {
      // Two arguments: compute the difference.
      if (list2proftime(&argVars[0], &start) == FAIL || list2proftime(&argVars[1], &res) == FAIL) {
         return;
      }
      profile_sub(&res, &start);
   }

   long n1 = res.tv_sec;
   long n2 = res.tv_fsec;
   list_append_number(returnVar->list, (Long)n1);
   list_append_number(returnVar->list, (Long)n2);
}

void
f_reltimefloat(Arr(Var) argVars UNUSED, OUT Var* returnVar) {
   ProfTime   tm;

   returnVar->tag = VAR_FLOAT;
   returnVar->floatt = 0;

   if (list2proftime(&argVars[0], &tm) == OK)
      returnVar->floatt = profile_float(&tm);
}

void
f_reltimestr(Arr(Var) argVars, OUT Var* returnVar) {
   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   ProfTime   tm;
   if (list2proftime(&argVars[0], &tm) == OK) {
      static Byte buf[50];
      long usec = tm.tv_fsec / (TV_FSEC_SEC / 1000000);
      eeSnprintf(buf, sizeof(buf), "%3ld.%06ld", (long)tm.tv_sec, usec);
      returnVar->string = copyStr(buf);
   }
}


//"strftime({format}[, {time}])" function
void
f_strftime(Arr(Var) argVars, OUT Var* returnVar) {
   Tm tmval;
   Tyme seconds;

   returnVar->tag = VAR_STRING;

   CS arg = tv_get_string(&argVars[0]);
   if (argVars[1].tag == VAR_UNKNOWN)
      seconds = time(NULL);
   else
      seconds = (Tyme)tv_get_number(&argVars[1]);
   Tm* curtime = eeLocaltime(&seconds, &tmval);
   if (!curtime) {
      returnVar->string = copyStr((CS)_("(Invalid)"));
      return;
   }

   Byte result_buf[256];

   if (!arg || STRFTIME(result_buf, sizeof(result_buf), arg, curtime) == 0)
      result_buf[0] = ZERO;

   returnVar->string = copyStr(result_buf);
}

// "strptime({format}, {timestring})" function
void
f_strptime(Var* argVars, Var* returnVar) {
   Tm tmval;

   CLEAR_FIELD(tmval);
   tmval.tm_isdst = -1;
   Byte* fmt = tv_get_string(&argVars[0]);
   Byte* str = tv_get_string(&argVars[1]);

   if (!fmt
          || strptime((char *)str, (char *)fmt, &tmval) == NULL
          || (returnVar->number = mktime(&tmval)) == -1
   )
      returnVar->number = 0;
}

private Timer* firstTimerS = NULL;
private long lastTimerIdS = 0;

//Return time left, in "msec", until "due".  Negative if past "due".
long
proftime_time_left(ProfTime *due, ProfTime *now) {
   if (now->tv_sec > due->tv_sec)
      return 0;
   return (due->tv_sec - now->tv_sec)*1000 + (due->tv_fsec - now->tv_fsec) / (TV_FSEC_SEC / 1000);
}

//Insert a timer into the list of timers.
private void
insert_timer(Timer* timer) {
   timer->next = firstTimerS;
   timer->prev = NULL;
   if (firstTimerS != NULL)
      firstTimerS->prev = timer;
   firstTimerS = timer;
   did_add_timer = TRUE;
}

//Take a timer out of the list of timers.
private void
remove_timer(Timer* timer) {
   if (!timer->prev)
      firstTimerS = timer->next;
   else
      timer->prev->next = timer->next;
   if (timer->next)
      timer->next->prev = timer->prev;
}

private void
free_timer(Timer* timer) {
   evFreeCallback(&timer->callback);
   eeglFree(timer);
}

// Create a timer and return it. Caller should set the callback.
Timer*
create_timer(long msec, int repeat) {
   Timer* timer = ALLOC_CLEAR_ONE(Timer);
   long   prev_id = lastTimerIdS;

   if (++lastTimerIdS <= prev_id)
      // Overflow!  Might cause duplicates...
      lastTimerIdS = 0;
   timer->id = lastTimerIdS;
   insert_timer(timer);
   if (repeat != 0)
      timer->tr_repeat = repeat - 1;
   timer->tr_interval = msec;

   timer_start(timer);
   return timer;
}

// (Re)start a timer.
void
timer_start(Timer *timer) {
   profile_setlimit(timer->tr_interval, &timer->due);
   timer->tr_paused = FALSE;
}

// Invoke the callback of "timer".
private void
timer_callback(Timer *timer) {
   Var   returnVar;
   Var   argv[2];

   if (ch_log_active()) {
      Callback *cb = &timer->callback;
      lo("invoking timer callback %s", cb->cb_partial != NULL ? cb->cb_partial->name : cb->name);
   }

   argv[0].tag = VAR_NUMBER;
   argv[0].number = (Long)timer->id;
   argv[1].tag = VAR_UNKNOWN;

   returnVar.tag = VAR_UNKNOWN;
   call_callback(&timer->callback, -1, &returnVar, 1, argv);
   clearVar(&returnVar);

   lo("timer callback finished");
}

// Call timers that are due. Return the time in msec until the next timer is due.
// Return -1 if there are no pending timers.
long
check_due_timer(void) {
   Timer* timer_next;
   long this_due;
   long next_due = -1;
   ProfTime now;
   Boole did_one = false;
   Boole need_drawUpdateScreen = false;
   long current_id = lastTimerIdS;

   // Don't run any timers while exiting, dealing with an error or at the debug prompt.
   if (isExitingG || aborting() || debug_mode)
      return next_due;

   profile_start(&now);
   for (Timer* timer = firstTimerS; timer != NULL && !gotInterruptG; timer = timer_next) {
      timer_next = timer->next;

      if (timer->id == -1 || timer->tr_firing || timer->tr_paused)
         continue;
      this_due = proftime_time_left(&timer->due, &now);
      if (this_due <= 1) {
         // Save and restore a lot of flags, because the timer fires while
         // waiting for a character, which might be halfway a command.
         int save_timer_busy = timer_busy;
         int save_vgetcBusyG = vgetcBusyG;
         int save_anyEmsgG = anyEmsgG;
         int prev_uncaught_emsg = uncaught_emsg;
         int save_called_emsg = called_emsg;
         int save_must_redraw = must_redraw;
         int save_ex_pressedreturn = get_pressedreturn();
         int save_may_garbage_collect = may_garbage_collect;
         EeglVarsSave   vvsave;
         ExceptionState estate;

         exception_state_save(&estate);

         // Create a scope for running the timer callback, ignoring most of
         // the current scope, such as being inside a try/catch.
         timer_busy = timer_busy > 0 || vgetcBusyG > 0;
         vgetcBusyG = 0;
         called_emsg = 0;
         anyEmsgG = FALSE;
         must_redraw = 0;
         may_garbage_collect = FALSE;
         exception_state_clear();
         saveEeglVars(&vvsave);

         // Invoke the callback.
         timer->tr_firing = TRUE;
         timer_callback(timer);
         timer->tr_firing = FALSE;

         // Restore stuff.
         timer_next = timer->next;
         did_one = true;
         timer_busy = save_timer_busy;
         vgetcBusyG = save_vgetcBusyG;
         if (uncaught_emsg > prev_uncaught_emsg)
            ++timer->tr_emsg_count;
         anyEmsgG = save_anyEmsgG;
         called_emsg = save_called_emsg;
         exception_state_restore(&estate);
         restoreEeglVars(&vvsave);
         if (must_redraw != 0)
            need_drawUpdateScreen = true;
         must_redraw = must_redraw > save_must_redraw ? must_redraw : save_must_redraw;
         set_pressedreturn(save_ex_pressedreturn);
         may_garbage_collect = save_may_garbage_collect;

         // Only fire the timer again if it repeats and stop_timer() wasn't
         // called while inside the callback (id == -1).
         if (timer->tr_repeat != 0 && timer->id != -1 && timer->tr_emsg_count < 3) {
            profile_setlimit(timer->tr_interval, &timer->due);
            this_due = proftime_time_left(&timer->due, &now);
            if (this_due < 1)
               this_due = 1;
            if (timer->tr_repeat > 0)
               --timer->tr_repeat;
         } else {
            this_due = -1;
            if (timer->tr_keep)
               timer->tr_paused = TRUE;
            else {
               remove_timer(timer);
               free_timer(timer);
            }
         }
      }
      if (this_due > 0 && (next_due == -1 || next_due > this_due))
         next_due = this_due;
   }

   if (did_one)
      redraw_after_callback(need_drawUpdateScreen, false);

   if (bevalexpr_due_set) {
      this_due = proftime_time_left(&bevalexpr_due, &now);
      if (this_due <= 1) {
         bevalexpr_due_set = FALSE;
         if (balloonEval == NULL) {
            balloonEval = ALLOC_CLEAR_ONE(BalloonEval);
            balloonEvalForTerm = TRUE;
         }
         if (balloonEval != NULL) {
            general_beval_cb(balloonEval, 0);
            setcursor();
            out_flush();
         }
      } ei (next_due == -1 || next_due > this_due)
         next_due = this_due;
   }
   // Some terminal portals may need their book updated.
   next_due = term_check_timers(next_due, &now);

   return current_id != lastTimerIdS ? 1 : next_due;
}

// Find a timer by ID.  Returns NULL if not found;
private Timer *
find_timer(long id) {
   Timer *timer;

   if (id < 0)
      return NULL;

   FOR_ALL_TIMERS(timer) {
      if (timer->id == id)
          return timer;
   } 
   return NULL;
}


// Stop a timer and delete it.
void
stop_timer(Timer *timer) {
   if (timer->tr_firing)
      // Free the timer after the callback returns.
      timer->id = -1;
   else {
      remove_timer(timer);
      free_timer(timer);
   }
}

private void
stop_all_timers(void) {
   Timer *timer;
   Timer *timer_next;

   for (timer = firstTimerS; timer != NULL; timer = timer_next) {
      timer_next = timer->next;
      stop_timer(timer);
   }
}

private void
add_timer_info(OUT Var* returnVar, Timer *timer) {
   List   *list = returnVar->list;
   Bag   *dict = allocBag();
   long   remaining;
   ProfTime   now;

   listAppendBag(list, dict);

   bagAddNumber(dict, S"id", timer->id);
   bagAddNumber(dict, S"time", (long)timer->tr_interval);

   profile_start(&now);
   remaining = proftime_time_left(&timer->due, &now);
   bagAddNumber(dict, S"remaining", (long)remaining);

   bagAddNumber(dict, S"repeat",
       (long)(timer->tr_repeat < 0 ? -1
              : timer->tr_repeat + (timer->tr_firing ? 0 : 1)));
   bagAddNumber(dict, S"paused", (long)(timer->tr_paused));

   DictItem* di = dictitem_alloc(tConst("callback"));
   if (bagAdd(dict, di) == FAIL)
      eeglFree(di);
   else
      putCallback(OUT &di->c, &timer->callback);
}

private void
add_timer_info_all(OUT Var* returnVar) {
   Timer *timer;

   FOR_ALL_TIMERS(timer) {
      if (timer->id != -1)
         add_timer_info(returnVar, timer);
   } 
}

// Mark references in partials of timers.
int
set_ref_in_timer(int copyID) {
   int abort = FALSE;
   Var   tv;

   for (Timer* timer = firstTimerS; !abort && timer; timer = timer->next) {
      if (timer->callback.cb_partial) {
         tv.tag = VAR_PARTIAL;
         tv.partial = timer->callback.cb_partial;
      } else {
         tv.tag = VAR_FUNC;
         tv.string = timer->callback.name;
      }
      abort = abort || set_ref_in_item(&tv, copyID, NULL, NULL);
   }
   return abort;
}

//Return TRUE if "timer" exists in the list of timers.
int
timer_valid(Timer *timer) {
   if (!timer)
      return FALSE;

   Timer *t;
   FOR_ALL_TIMERS(t) {
      if (t == timer)
         return TRUE;
   } 
   return FALSE;
}

# if defined(EXITFREE) || defined(PROTO)
void
timer_free_all(void) {
   while (firstTimerS != NULL) {
      Timer *timer = firstTimerS;
      remove_timer(timer);
      free_timer(timer);
   }
}
# endif

// "timer_info([timer])" function
void
f_timer_info(Arr(Var) argVars, OUT Var* returnVar) {
   Timer *timer = NULL;

   allocReturnList(returnVar);

   if (check_for_opt_number_arg(argVars, 0) == FAIL)
      return;

   if (argVars[0].tag != VAR_UNKNOWN) {
      timer = find_timer((int)tv_get_number(&argVars[0]));
      if (timer != NULL)
         add_timer_info(returnVar, timer);
   } else
      add_timer_info_all(returnVar);
}

// "timer_pause(timer, paused)" function
void
f_timer_pause(Arr(Var) argVars, OUT Var* returnVar UNUSED) {
   if (argVars[0].tag != VAR_NUMBER) {
      emsg(_(e_number_expected));
      return;
   }

   int paused = (int)tv_get_bool(&argVars[1]);

   Timer* timer = find_timer((int)tv_get_number(&argVars[0]));
   if (timer != NULL)
      timer->tr_paused = paused;
}

// "timer_start(time, callback [, options])" function
void
f_timer_start(Arr(Var) argVars, OUT Var* returnVar) {
   int repeat = 0;
   Bag* dict;

   returnVar->number = -1;

   long msec = (long)tv_get_number(&argVars[0]);
   if (argVars[2].tag != VAR_UNKNOWN) {
      if (check_for_nonnull_dict_arg(argVars, 2) == FAIL)
         return;

      dict = argVars[2].bag;
      if (bagHasKey(dict, tConst("repeat")))
         repeat = bagGetNumber(dict, tConst("repeat"));
   }

   Callback callback = get_callback(&argVars[1]);
   if (!callback.name)
      return;

   Timer* timer = create_timer(msec, repeat);
   if (!timer) {
      evFreeCallback(&callback);
      return;
   }
   set_callback(&timer->callback, &callback);
   if (callback.needsFreeing)
      eeglFree(callback.name);
   returnVar->number = (Long)timer->id;
}

// "timer_stop(timer)" function
void
f_timer_stop(Arr(Var) argVars, OUT Var* returnVar UNUSED) {
   if (check_for_number_arg(argVars, 0) == FAIL)
      return;

   Timer* timer = find_timer((int)tv_get_number(&argVars[0]));
   if (timer)
      stop_timer(timer);
}

// "timer_stopall()" function
void
f_timer_stopall(Arr(Var) argVars UNUSED, OUT Var* returnVar UNUSED) {
   stop_all_timers();
}

private TimeVal   prev_timeval;

//Save the previous time before doing something that could nest.
//set "*tv_rel" to the time elapsed so far.
void
time_push(void *tv_rel, void *tv_start) {
   *((TimeVal *)tv_rel) = prev_timeval;
   gettimeofday(&prev_timeval, NULL);
   ((TimeVal *)tv_rel)->tv_usec = prev_timeval.tv_usec - ((TimeVal *)tv_rel)->tv_usec;
   ((TimeVal *)tv_rel)->tv_sec = prev_timeval.tv_sec - ((TimeVal *)tv_rel)->tv_sec;
   if (((TimeVal *)tv_rel)->tv_usec < 0) {
      ((TimeVal *)tv_rel)->tv_usec += 1000000;
      --((TimeVal *)tv_rel)->tv_sec;
   }
   *(TimeVal *)tv_start = prev_timeval;
}

//Compute the previous time after doing something that could nest.
//Subtract "*tp" from prev_timeval;
//Note: The arguments are (void *) to avoid trouble with systems that don't have TimeVal.
void
time_pop(void   *tp) {  // actually (TimeVal *)
   prev_timeval.tv_usec -= ((TimeVal *)tp)->tv_usec;
   prev_timeval.tv_sec -= ((TimeVal *)tp)->tv_sec;
   if (prev_timeval.tv_usec < 0) {
      prev_timeval.tv_usec += 1000000;
      --prev_timeval.tv_sec;
   }
}

private void
time_diff(TimeVal *then, TimeVal *now) {
   long usec = now->tv_usec - then->tv_usec;
   long msec = (now->tv_sec - then->tv_sec) * 1000L + usec / 1000L;
   usec = usec % 1000L;
   fprintf(time_fd, "%03ld.%03ld", msec, usec >= 0 ? usec : usec + 1000L);
}

void
time_msg(
   CS mesg,
   void* tv_start  // only for scriptRunFile: start time; actually (TimeVal *)
){
   static TimeVal start;
   TimeVal now;

   if (!time_fd)
      return;

   if (STRSTR(mesg, S"STARTING") != NULL) {
      gettimeofday(&start, NULL);
      prev_timeval = start;
      fprintf(time_fd, "\n\ntimes in msec\n");
      fprintf(time_fd, " clock   self+sourced   self:  sourced script\n");
      fprintf(time_fd, " clock   elapsed:              other lines\n\n");
   }
   gettimeofday(&now, NULL);
   time_diff(&start, &now);
   if (((TimeVal *)tv_start) != NULL) {
      fprintf(time_fd, "  ");
      time_diff(((TimeVal *)tv_start), &now);
   }
   fprintf(time_fd, "  ");
   time_diff(&prev_timeval, &now);
   prev_timeval = now;
   fprintf(time_fd, ": %s\n", mesg);
}

// Read 8 bytes from "fd" and turn them into a Tyme, MSB first. Returns -1 when encountering EOF.
Tyme
get8ctime(FILE *fd) {
   Tyme   n = 0;

   for (int i = 0; i < 8; ++i) {
      int c = getc(fd);
      if (c == EOF) return -1;
      n = (n << 8) + c;
   }
   return n;
}

// Write Tyme to file "fd" in 8 bytes. Returns FAIL when the write failed.
int
put_time(FILE *fd, Tyme the_time) {
   Byte buf[8];

   time_to_bytes(the_time, buf);
   return fwrite(buf, 8, 1, fd) == 1 ? OK : FAIL;
}

// Write Tyme to "buf[8]".
void
time_to_bytes(Tyme the_time, CS buf) {
   int      c;
   int      i;
   int      bi = 0;
   Tyme   wtime = the_time;

   //Tyme can be up to 8 bytes in size, more than Ulong, thus we can't use put_bytes() here.
   //Another problem is that ">>" may do an arithmetic shift that keeps the sign. This happens 
   //for large values of wtime. A cast to Ulong may truncate if Tyme is 8 bytes. So only use a 
   //cast when it is 4 bytes, it's safe to assume that Ulong is 4 bytes or more and when using 8
   //bytes the top bit won't be set.
   for (i = 7; i >= 0; --i) {
      if (i + 1 > (int)sizeof(Tyme))
         // ">>" doesn't work well when shifting more bits than avail
         buf[bi++] = 0;
      else {
         c = (int)(wtime >> (i * 8));
         buf[bi++] = c;
      }
   }
}

// Put timestamp "tt" in "buf[buflen]" in a nice format.
void
add_time(CS buf, Unt buflen, Tyme tt) {
   Tm tmval;
   Tm* curtime;
   Unt   n;

   if (eeTime() - tt >= 100) {
      curtime = eeLocaltime(&tt, &tmval);
      if (eeTime() - tt < (60L * 60L * 12L))
         // within 12 hours
         n = STRFTIME(buf, buflen, "%H:%M:%S", curtime);
      else
         // longer ago
         n = STRFTIME(buf, buflen, "%Y/%m/%d %H:%M:%S", curtime);
      if (n == 0)
         buf[0] = ZERO;
   } else {
      long seconds = (long)(eeTime() - tt);

      eeSnprintf(buf, buflen, NGETTEXT("%ld second ago", "%ld seconds ago", seconds), seconds);
   }
}

// Store the current time in "tm".
void
profile_start(ProfTime *tm){
   PROF_GET_TIME(tm);
}

// Put the time "msec" past now in "tm".
void
profile_setlimit(long msec, ProfTime *tm) {
   if (msec <= 0)   // no limit
      profile_zero(tm);
   else {
      PROF_GET_TIME(tm);
      Long fsec = (Long)tm->tv_fsec + (Long)msec * (Long)(TV_FSEC_SEC / 1000);
      tm->tv_fsec = fsec % (long)TV_FSEC_SEC;
      tm->tv_sec += fsec / (long)TV_FSEC_SEC;
   }
}

// Return TRUE if the current time is past "tm".
int
profile_passed_limit(ProfTime *tm) {
   if (tm->tv_sec == 0)    // timer was not set
      return FALSE;
      
   ProfTime   now;
   PROF_GET_TIME(&now);
   return (now.tv_sec > tm->tv_sec || (now.tv_sec == tm->tv_sec && now.tv_fsec > tm->tv_fsec));
}


// Compute the elapsed time from "tm" till now and store in "tm".
void
profile_end(ProfTime *tm) {
   ProfTime now;

   PROF_GET_TIME(OUT &now);
   tm->tv_fsec = now.tv_fsec - tm->tv_fsec;
   tm->tv_sec = now.tv_sec - tm->tv_sec;
   if (tm->tv_fsec < 0) {
      tm->tv_fsec += TV_FSEC_SEC;
      --tm->tv_sec;
   }
}

// Subtract the time "tm2" from "tm".
void
profile_sub(ProfTime *tm, ProfTime *tm2){
   tm->tv_fsec -= tm2->tv_fsec;
   tm->tv_sec -= tm2->tv_sec;
   if (tm->tv_fsec < 0) {
      tm->tv_fsec += TV_FSEC_SEC;
      --tm->tv_sec;
   }
}

// Return a float that represents the time in "tm".
double
profile_float(ProfTime *tm){
   return (double)tm->tv_sec + (double)tm->tv_fsec / (double)TV_FSEC_SEC;
}

// Set the time in "tm" to zero.
void
profile_zero(ProfTime *tm) {
   tm->tv_fsec = 0;
   tm->tv_sec = 0;
}

//Return a string that represents the time in "tm". Use a static buffer!
CS
profile_msg(ProfTime *tm){
   static Byte buf[50];

   SPRINTF(buf, PROF_TIME_FORMAT, (long)tm->tv_sec, (long)tm->tv_fsec);
   return buf;
}

#ifndef PROTO  // proto is defined in eegl.h
# ifdef ELAPSED_TIMEVAL
// Return time in msec since "start_tv".
long
elapsed(TimeVal *start_tv) {
   TimeVal  now_tv;
   gettimeofday(&now_tv, NULL);
   return (now_tv.tv_sec - start_tv->tv_sec) * 1000L + (now_tv.tv_usec - start_tv->tv_usec) / 1000L;
}
# endif

# ifdef ELAPSED_TICKCOUNT
// Return time in msec since "start_tick".
long
elapsed(DWORD start_tick) {
   DWORD   now = GetTickCount();

   return (long)now - (long)start_tick;
}
# endif
#endif

# if defined(PROF_NSEC) || defined(PROTO)
// Implement timeout with timer_create() and timer_settime().
private volatile sig_atomic_t timeout_flag = FALSE;
private timer_t timer_id;
private int timer_created = FALSE;

// Callback for when the timer expires.
private void
set_flag(union sigval _unused UNUSED) {
   timeout_flag = TRUE;
}

// Stop any active timeout.
void
stop_timeout(void) {
   static struct itimerspec disarm = {{0, 0}, {0, 0}};

   if (timer_created) {
      int ret = timer_settime(timer_id, 0, &disarm, NULL);

      if (ret < 0)
         showErrFmtMsg(_(e_could_not_clear_timeout_str), strerror(errno));
   }

   // Clear the current timeout flag; any previous timeout should be
   // considered _not_ triggered.
   timeout_flag = FALSE;
}

// Start the timeout timer.
//
// The return value is a pointer to a flag that is initialised to FALSE. If the
// timeout expires, the flag is set to TRUE. This will only return pointers to
// static memory; i.e. any pointer returned by this function may always be
// safely dereferenced.
//
// This function is not expected to fail, but if it does it will still return a
// valid flag pointer; the flag will remain stuck as FALSE .
volatile sig_atomic_t *
start_timeout(long msec) {
   struct itimerspec interval = {
       {0, 0},               // Do not repeat.
       {msec / 1000, (msec % 1000) * 1000000}};   // Timeout interval
   int ret;

   // This is really the caller's responsibility, but let's make sure the
   // previous timer has been stopped.
   stop_timeout();

   if (!timer_created) {
      struct sigevent action = {0};

      action.sigev_notify = SIGEV_THREAD;
      action.sigev_notify_function = set_flag;
      ret = timer_create(CLOCK_MONOTONIC, &action, &timer_id);
      if (ret < 0) {
         showErrFmtMsg(_(e_could_not_set_timeout_str), strerror(errno));
         return &timeout_flag;
      }
      timer_created = TRUE;
   }

   lo("setting timeout timer to %d sec %ld nsec",
          (int)interval.it_value.tv_sec, (long)interval.it_value.tv_nsec);
   ret = timer_settime(timer_id, 0, &interval, NULL);
   if (ret < 0)
      showErrFmtMsg(_(e_could_not_set_timeout_str), strerror(errno));

   return &timeout_flag;
}

// To be used before fork/exec: delete any created timer.
void
delete_timer(void) {
   if (!timer_created)
      return;

   timer_delete(timer_id);
   timer_created = FALSE;
}

# else // PROF_NSEC

// Implement timeout with setitimer()
private struct sigaction      prev_sigaction;
private volatile sig_atomic_t   timeout_flag        = FALSE;
private int         timer_active        = FALSE;
private int         timer_handler_active = FALSE;
private volatile sig_atomic_t   alarm_pending        = FALSE;

// Handle SIGALRM for a timeout.
private void
set_flag SIGDEFARG(sigarg) {
   if (alarm_pending)
      alarm_pending = FALSE;
   else
      timeout_flag = TRUE;
}

// Stop any active timeout.
void
stop_timeout(void) {
   static struct itimerval disarm = {{0, 0}, {0, 0}};
   int             ret;

   if (timer_active) {
      timer_active = FALSE;
      ret = setitimer(ITIMER_REAL, &disarm, NULL);
      if (ret < 0)
         // Should only get here as a result of coding errors.
         showErrFmtMsg(_(e_could_not_clear_timeout_str), strerror(errno));
   }

   if (timer_handler_active) {
      timer_handler_active = FALSE;
      ret = sigaction(SIGALRM, &prev_sigaction, NULL);
      if (ret < 0)
         // Should only get here as a result of coding errors.
         showErrFmtMsg(_(e_could_not_reset_handler_for_timeout_str), strerror(errno));
   }
   timeout_flag = FALSE;
}

//Start the timeout timer.
//
//The return value is a pointer to a flag that is initialised to FALSE. If the timeout expires, the
//flag is set to TRUE. This will only return pointers to static memory; i.e. any pointer returned 
//by this function may always be safely dereferenced.
//
//This function is not expected to fail, but if it does it will still return a valid flag pointer;
//the flag will remain stuck as FALSE.
volatile sig_atomic_t*
start_timeout(long msec) {
   struct itimerval   interval = {
       {0, 0},                // Do not repeat.
       {msec / 1000, (msec % 1000) * 1000}};   // Timeout interval
   struct sigaction   handle_alarm;
   int         ret;
   sigset_t      sigs;
   sigset_t      saved_sigs;

   // This is really the caller's responsibility, but let's make sure the
   // previous timer has been stopped.
   stop_timeout();

   // There is a small chance that SIGALRM is pending and so the handler must
   // ignore it on the first call.
   alarm_pending = FALSE;
   ret = sigemptyset(&sigs);
   ret = ret == 0 ? sigaddset(&sigs, SIGALRM) : ret;
   ret = ret == 0 ? sigprocmask(SIG_BLOCK, &sigs, &saved_sigs) : ret;
   timeout_flag = FALSE;
   ret = ret == 0 ? sigpending(&sigs) : ret;
   if (ret == 0) {
      alarm_pending = sigismember(&sigs, SIGALRM);
      ret = sigprocmask(SIG_SETMASK, &saved_sigs, NULL);
   }
   if (unlikely(ret != 0 || alarm_pending < 0)) {
      // Just catching coding errors. Write an error message, but carry on.
      showErrFmtMsg(_(e_could_not_check_for_pending_sigalrm_str), strerror(errno));
      alarm_pending = FALSE;
   }

   // Set up the alarm handler first.
   ret = sigemptyset(&handle_alarm.sa_mask);
   handle_alarm.sa_handler = set_flag;
   
   handle_alarm.sa_flags = 0;
   ret = ret == 0 ?  sigaction(SIGALRM, &handle_alarm, &prev_sigaction) : ret;
   if (ret < 0) {
      // Should only get here as a result of coding errors.
      showErrFmtMsg(_(e_could_not_set_handler_for_timeout_str), strerror(errno));
      return &timeout_flag;
   }
   timer_handler_active = TRUE;

   // Set up the interval timer once the alarm handler is in place.
   ret = setitimer(ITIMER_REAL, &interval, NULL);
   if (ret < 0) {
      // Should only get here as a result of coding errors.
      showErrFmtMsg(_(e_could_not_set_timeout_str), strerror(errno));
      stop_timeout();
      return &timeout_flag;
   }

   timer_active = TRUE;
   return &timeout_flag;
}
# endif // PROF_NSEC


//}}}
//{{{cursor movement

//Get the screen position of the cursor.
int
getviscol(void) {
   ColNr   x;
   bookGetVirtualColInVirtualMode(curPor, &curPor->cursor, OUT &x, NULL, NULL);
   return (int)x;
}

//Go to column "wcol", and add/insert white space as necessary to get the cursor in that column.
//The caller must have saved the cursor line for undo!
int
coladvance_force(ColNr wcol) {
   int rc = coladvance2(&curPor->cursor, TRUE, FALSE, wcol);

   if (wcol == MAXCOL)
      curPor->cacheState &= ~VALID_VIRTCOL;
   else {
      // Virtcol is valid
      curPor->cacheState |= VALID_VIRTCOL;
      curPor->virtCol = wcol;
   }
   return rc;
}

//Try to advance the Cursor to the specified screen column "wantcol". If virtual editing: fine 
//tune the cursor position. Note that all virtual positions off the end of a line should share
//a curPor->cursor.col value (n.b. this is equal to STRLEN(line)), beginning at coladd 0.
//return OK if desired column is reached, FAIL if not
int
coladvance(ColNr wantcol) {
   int rc = getvpos(&curPor->cursor, wantcol);

   if (wantcol == MAXCOL || rc == FAIL)
      curPor->cacheState &= ~VALID_VIRTCOL;
   ei (*ml_get_cursor() != TAB) {
      // Virtcol is valid when not on a TAB
      curPor->cacheState |= VALID_VIRTCOL;
      curPor->virtCol = wantcol;
   }
   return rc;
}

//Return in "pos" the position of the cursor advanced to screen column "wantcol".
//return OK if desired column is reached, FAIL if not
int
getvpos(Pos *pos, ColNr wantcol) {
   return coladvance2(pos, FALSE, virtual_active(), wantcol);
}

private int
coladvance2(
   Pos   *pos,
   int      addspaces,   // change the text to achieve our goal?
   int      finetune,   // change char offset for the exact column
   ColNr   wcol_arg   // column to move to (can be negative)
){
   ColNr   wcol = wcol_arg;
   int      idx;
   ColNr   col = 0;
   int      csize = 0;
   int      head = 0;

   Boole one_more = ((stateG & MODE_INSERT) != 0) || restart_edit != ZERO || (VIsual_active);
   CS line = memGetLine(curBook, pos->lnum, FALSE);
   int linelen = memGetBookLen(curBook, pos->lnum);

   if (wcol >= MAXCOL) {
      idx = linelen - 1 + one_more;
      col = wcol;

      if ((addspaces || finetune) && !VIsual_active) {
         curPor->cursWant = linetabsize(curPor, pos->lnum) + one_more;
         if (curPor->cursWant > 0)
             --curPor->cursWant;
      }
   } else {
      int      width = curPor->width - normalPortalColumnOffset(curPor);
      CharTableSize   cts;

      if (finetune
         && curPor->o.wrap
         && curPor->width != 0
         && wcol >= (ColNr)width
         && width > 0)
      {
         csize = linetabsize_eol(curPor, pos->lnum);
         if (csize > 0)
            csize--;

         if (wcol / width > (ColNr)csize / width
             && ((stateG & MODE_INSERT) == 0 || (int)wcol > csize + 1)) {
            //In case of line wrapping don't move the cursor beyond the right screen edge. In 
            //Insert mode allow going just beyond the last character (like what happens when 
            //typing and reaching the right portal edge).
            wcol = (csize / width + 1) * width - 1;
         }
      }

      bookInitCharsForKeywordsSizeArg(&cts, curPor, pos->lnum, 0, line, line);
      while (cts.cts_vcol <= wcol && *cts.cts_ptr != ZERO) {
         int at_start = cts.cts_ptr == cts.cts_line;
         // Count a tab for what it's worth (if list mode not on)
         csize = win_lbr_chartabsize(&cts, &head);
         MB_PTR_ADV(cts.cts_ptr);
         cts.cts_vcol += csize;
         if (at_start)
            // do not count the columns for virtual text above
            cts.cts_vcol -= cts.cts_first_char;
      }
      col = cts.cts_vcol;
      idx = (int)(cts.cts_ptr - line);
      clear_chartabsize_arg(&cts);

      //Handle all the special cases. The virtual_active() check is needed to ensure that a 
      //virtual position off the end of a line has the correct indexing. The one_more comparison
      //replaces an explicit add of one_more later on.
      if (col > wcol || (!virtual_active() && one_more == 0)) {
          idx -= 1;
          //Don't count the chars from 'showbreak'.
          csize -= head;
          col -= csize;
      }

      if (virtual_active()
         && addspaces
         && wcol >= 0
         && ((col != wcol && col != wcol + 1) || csize > 1)
      ){
         //'virtualedit' is set: The difference between wcol and col is filled with spaces.

         if (line[idx] == ZERO) {
            //Append spaces
            int correct = wcol - col;
            CS newline = alloc(idx + correct + 1);
            int   t;
            for (t = 0; t < idx; ++t)
               newline[t] = line[t];

            for (t = 0; t < correct; ++t)
               newline[t + idx] = ' ';

            newline[idx + correct] = ZERO;

            ml_replace(pos->lnum, newline, FALSE);
            changed_bytes(pos->lnum, (ColNr)idx);
            idx += correct;
            col = wcol;
         } else {
            // Break a tab
            int   correct = wcol - col - csize + 1; // negative!!
            Byte   *newline;
            int   t, s = 0;
            int   v;

            if (-correct > csize)
               return FAIL;

            newline = alloc(linelen + csize);

            for (t = 0; t < linelen; t++) {
               if (t != idx)
                  newline[s++] = line[t];
               else {
                  for (v = 0; v < csize; v++)
                     newline[s++] = ' ';
               } 
            }

            newline[linelen + csize - 1] = ZERO;

            ml_replace(pos->lnum, newline, FALSE);
            changed_bytes(pos->lnum, idx);
            idx += (csize - 1 + correct);
            col += correct;
         }
      }
   }

   if (idx < 0)
      pos->col = 0;
   else
      pos->col = idx;

   pos->coladd = 0;

   if (finetune) {
      if (wcol == MAXCOL) {
         // The width of the last character is used to set coladd.
         if (!one_more) {
            ColNr scol, ecol;
            getvcol(curPor, pos, OUT &scol, NULL, OUT &ecol);
            pos->coladd = ecol - scol;
         }
      } else {
         int b = (int)wcol - (int)col;

         // The difference between wcol and col is used to set coladd.
         if (b > 0 && b < (MAXCOL - 2 * curPor->width))
            pos->coladd = b;

         col += b;
      }
   }

   // prevent from moving onto a trail byte
   mb_adjustpos(curBook, pos);

   return (wcol < 0 || col < wcol) ? FAIL : OK;
}

//Increment the cursor position.  See inc() for return values.
int
inc_cursor(void) {
   return inc(&curPor->cursor);
}

//Increment the line pointer "lp" crossing line boundaries as necessary.
//Return 1 when going to the next line.
//Return 2 when moving forward onto a ZERO at the end of the line).
//Return -1 when at the end of file. 0 otherwise.
int
inc(Pos *lp) {
   // when searching position may be set to end of a line
   if (lp->col != MAXCOL) {
      CS p = ml_get_pos(lp);
      if (*p != ZERO) {// still within line, move to next char (may be ZERO)
         int l = utfCharLen(p);
         lp->col += l;
         return ((p[l] != ZERO) ? 0 : 2);
      }
   }
   if (lp->lnum != curBook->mem.lineCount) {    // there is a next line
      lp->col = 0;
      lp->lnum++;
      lp->coladd = 0;
      return 1;
   }
   return -1;
}

//incl(lp): same as inc(), but skip the ZERO at the end of non-empty lines
int
incl(Pos *lp) {
   int r;
   if ((r = inc(lp)) >= 1 && lp->col)
      r = inc(lp);
   return r;
}

int
dec_cursor(void) {
   return dec(&curPor->cursor);
}

//dec(p)
//
//Decrement the line pointer 'p' crossing line boundaries as necessary.
//Return 1 when crossing a line, -1 when at start of file, 0 otherwise.
int
dec(Pos *lp) {
   CS p;

   lp->coladd = 0;
   if (lp->col == MAXCOL) {
      // past end of line
      p = ml_get(lp->lnum);
      lp->col = ml_get_len(lp->lnum);
      lp->col -= (*mb_head_off)(p, p + lp->col);
      return 0;
   }

   if (lp->col > 0) {
      // still within line
      lp->col--;
      p = ml_get(lp->lnum);
      lp->col -= (*mb_head_off)(p, p + lp->col);
      return 0;
   }

   if (lp->lnum > 1) {
      // there is a prior line
      lp->lnum--;
      p = ml_get(lp->lnum);
      lp->col = ml_get_len(lp->lnum);
      lp->col -= (*mb_head_off)(p, p + lp->col);
      return 1;
   }

   // at start of file
   return -1;
}

//decl(lp): same as dec(), but skip the ZERO at the end of non-empty lines
int
decl(Pos *lp) {
   int r;

   if ((r = dec(lp)) == 1 && lp->col)
      r = dec(lp);
   return r;
}

//Make sure "pos.lnum" and "pos.col" are valid in "buf".
//This allows for the col to be on the ZERO byte.
void
check_pos(Book* book, Pos *pos) {
   if (pos->lnum > book->mem.lineCount)
      pos->lnum = book->mem.lineCount;

   if (pos->col > 0) {
      ColNr len = memGetBookLen(book, pos->lnum);
      if (pos->col > len)
         pos->col = len;
   }
}

//}}}
//{{{indentation-related functions

//Return the effective shiftwidth value for current book
long
get_sw_value(Book *book) {
   return get_sw_value_col(book, 0, FALSE);
}

//Idem, using "pos".
private long
get_sw_value_pos(Book* book, Pos *pos, int left) {
   Pos save_cursor = curPor->cursor;
   curPor->cursor = *pos;
   long sw_value = get_sw_value_col(book, get_nolist_virtcol(), left);
   curPor->cursor = save_cursor;
   return sw_value;
}

//Idem, using the first non-black in the current line.
long
get_sw_value_indent(Book* book, int left) {
   Pos pos = curPor->cursor;

   pos.col = getwhitecols_curline();
   return get_sw_value_pos(book, &pos, left);
}

//Idem, using virtual column "col".
long
get_sw_value_col(Book* book, ColNr col UNUSED, int left UNUSED) {
   return book->o.shiftWidth;
}

//Count the size (in portal cells) of the indent in the current line.
int
get_indent(void) {
   return get_indent_str(ml_get_curline(), (int)curBook->o.shiftWidth);
}

//Count the size (in portal cells) of the indent in line "lnum".
int
get_indent_lnum(LineNr lnum) {
   return get_indent_str(ml_get(lnum), (int)curBook->o.shiftWidth);
}

//Count the size (in portal cells) of the indent in line "lnum" of book "book".
int
get_indent_buf(Book* book, LineNr lnum) {
   return get_indent_str(memGetLine(book, lnum, FALSE), (int)book->o.shiftWidth);
}

//count the size (in portal cells) of the indent in line "ptr", with 'tabstop' at "ts"
private int
get_indent_str(CS ptr, int ts) {// if TRUE, count a tab as ^I
   int count = 0;
   for (; *ptr != ZERO; ++ptr) {
      if (*ptr == TAB) {   // count a tab for what it is worth
         count += ts;
      } ei (*ptr == ' ')
         ++count;      // count a space for one
      else
         break;
   }
   return count;
}

//Set the indent of the current line. Leave the cursor on the first non-blank in the line.
//Caller must take care of undo.
//"flags":
//  SIN_CHANGED:   call changed_bytes() if the line was changed.
//  SIN_INSERT:   insert the indent in front of the line.
//  SIN_UNDO:   save line for undo before changing it.
//Return TRUE if the line was changed.
int
set_indent(
   int      size,          // measured in spaces
   int      flags
){
   Byte   *s;
   int      line_len;       // size of the line (including the ZERO)
   int      doit = FALSE;
   int      retval = FALSE;

   // First check if there is anything to do and compute the number of
   // characters needed for the indent.
   int todo = size;
   int ind_len = 0; // measured in characters
   CS oldline = ml_get_curline();
   CS p = oldline;
   line_len = ml_get_curline_len() + 1;

   // Calculate the buffer size for the new indent, and check to see if it isn't already set

   // if 'expandtab' isn't set: use TABs
   if (!curBook->o.expandTab) {
      // count tabs required for indent
      while (todo >= (int)curBook->o.shiftWidth) {
         if (*p != TAB)
             doit = TRUE;
         else
             ++p;
         todo -= (int)curBook->o.shiftWidth;
         ++ind_len;
       }
   }
   // count spaces required for indent
   while (todo > 0) {
      if (*p != ' ')
         doit = TRUE;
      else
         ++p;
      --todo;
      ++ind_len;
   }

   // Return if the indent is OK already.
   if (!doit && !SPACE_OR_TAB(*p) && !(flags & SIN_INSERT))
      return FALSE;

   // Allocate memory for the new line.
   if ((flags & SIN_INSERT) != 0)
      p = oldline;
   else {
      p = skipwhite(p);
      line_len -= (int)(p - oldline);
   }

   todo = size;
   CS newline = alloc(ind_len + line_len);
   s = newline;

   // Put the characters in the new line.
   // if 'expandtab' isn't set: use TABs
   if (!curBook->o.expandTab) {
      while (todo >= (int)curBook->o.shiftWidth) {
         *s++ = TAB;
         todo -= (int)curBook->o.shiftWidth;
      }
   }
   while (todo > 0) {
       *s++ = ' ';
       --todo;
   }
   mch_memmove(s, p, (Unt)line_len);

   // Replace the line (unless undo fails).
   if (!(flags & SIN_UNDO) || u_savesub(curPor->cursor.lnum) == OK) {
      ColNr old_offset = (ColNr)(p - oldline);
      ColNr new_offset = (ColNr)(s - newline);

      // this may free "newline"
      ml_replace(curPor->cursor.lnum, newline, FALSE);
      if (flags & SIN_CHANGED)
          changed_bytes(curPor->cursor.lnum, 0);

      // Correct saved cursor position if it is in this line.
      if (saved_cursor.lnum == curPor->cursor.lnum) {
         if (saved_cursor.col >= old_offset)
            // cursor was after the indent, adjust for the number of bytes added/removed
            saved_cursor.col += ind_len - old_offset;
         ei (saved_cursor.col >= new_offset)
            // cursor was in the indent, and is now after it, put it back
            // at the start of the indent (replacing spaces with TAB)
            saved_cursor.col = new_offset;
      }
      int added = ind_len - old_offset;

      // When increasing indent this behaves like spaces were inserted at the old indent, when 
      // decreasing indent it behaves like spaces were deleted at the new indent.
      adjustPropColumns(
         curPor->cursor.lnum, added > 0 ? old_offset : (ColNr)ind_len, added, APC_INDENT
      );
      retval = TRUE;
   } else
       eeglFree(newline);

   curPor->cursor.col = ind_len;
   return retval;
}

//Return the indent of the current line after a number.  Return -1 if no
//number was found.  Used for 'n' in 'formatoptions': numbered list.
//Since a pattern is used it can actually handle more than numbers.
int
get_number_indent(LineNr lnum) {
   ColNr   col;
   Pos   pos;

   RegMatch   regmatch;
   int      lead_len = 0;   // length of comment leader

   if (lnum > curBook->mem.lineCount)
      return -1;
   pos.lnum = 0;

   // In format_lines() (i.e. not insert mode), fo+=q is needed too...
   if ((stateG & MODE_INSERT) || has_format_option(FO_Q_COMS))
      lead_len = get_leader_len(ml_get(lnum), NULL, FALSE, TRUE);

   regmatch.regprog = compileRegexp(curBook->o.formatListPattern, RE_MAGIC);
   if (regmatch.regprog) {
      regmatch.rm_ic = FALSE;
      // eeRegexec() expects a pointer to a line.  This lets us
      // start matching for the flp beyond any comment leader...
      if (eeRegexec(&regmatch, ml_get(lnum) + lead_len, (ColNr)0)) {
         pos.lnum = lnum;
         pos.col = (ColNr)(*regmatch.endp - ml_get(lnum));
         pos.coladd = 0;
      }
      eeRegFree(regmatch.regprog);
   }

   if (pos.lnum == 0 || *ml_get_pos(&pos) == ZERO)
       return -1;
   getvcol(curPor, &pos, &col, NULL, NULL);
   return (int)col;
}

//Return appropriate space number for breakindent, taking influencing parameters into account. 
//Portal must be specified, since it is not necessarily always the current one.
int
getBreakindentForPort(Portal* po, CS line) {
   static int       prev_indent = 0;   // cached indent value
   static long       prev_ts     = 0L;   // cached tabstop value
   static int       prev_fnum   = 0;   // cached book number
   static CS prev_line  = NULL;   // cached copy of "line"
   static Long prev_tick = 0;   // changedtick of cached value
   static int preList = 0;   // cached list indent
   static int preListopt = 0;   // cached o.breakIndent_list value
   static int prev_no_ts = FALSE;   // cached no_ts value
   // cached formatlistpat value
   static Byte   *prev_flp = NULL;
   int          bri = 0;
   // portal width minus portal margin space, i.e. what rests for text
   const int       eff_wwidth = po->width  - normalPortalColumnOffset(po);

   // In list mode, if 'listchars' "tab" isn't set, a TAB is displayed as ^I.
   int no_ts = po->o.list && listCharsG.tab1 == ZERO;

   // Used cached indent, unless
   // - book changed, or
   // - book was changed, or
   // - @breakindentopt "list" changed, or
   // - @list or @listchars "tab" changed, or
   // - @formatlistpat changed, or
   // - line changed.
   if (prev_fnum != po->book->fiNum
       || prev_ts != po->book->o.shiftWidth
       || prev_tick != CHANGEDTICK(po->book)
       || preListopt != po->breakIndent.list
       || prev_no_ts != no_ts
       || !prev_flp
       || (po->book->o.formatListPattern && STRCMP(prev_flp, po->book->o.formatListPattern) != 0)
       || !prev_line
       || STRCMP(prev_line, line) != 0
   ) {
      prev_fnum = po->book->fiNum;
      eeglFree(prev_line);
      prev_line = copyStr(line);
      prev_ts = po->book->o.shiftWidth;
      if (po->breakIndent.vcol == 0)
         prev_indent = get_indent_str(line, (int)po->book->o.shiftWidth);
      prev_tick = CHANGEDTICK(po->book);
      preListopt = po->breakIndent.list;
      preList = 0;
      prev_no_ts = no_ts;
      eeglFree(prev_flp);
      prev_flp = po->book->o.formatListPattern ? copyStr(po->book->o.formatListPattern) : null;
      // add additional indent for numbered lists
      if (po->breakIndent.list != 0 && po->breakIndent.vcol == 0) {
         RegMatch regmatch;

         regmatch.regprog = compileRegexp(prev_flp, RE_MAGIC + RE_STRING + RE_AUTO + RE_STRICT);

         if (regmatch.regprog != NULL) {
             regmatch.rm_ic = FALSE;
             if (eeRegexec(&regmatch, line, 0)) {
                if (po->breakIndent.list > 0)
                   preList = po->breakIndent.list;
                else {
                   CS ptr = *regmatch.startp;
                   CS end_ptr = *regmatch.endp;
                   int indent = 0;

                   // Compute the width of the matched text.
                   // Use win_chartabsize() so that TAB size is correct, while wrapping is ignored.
                   while (ptr < end_ptr) {
                       indent += win_chartabsize(po, ptr, indent);
                       MB_PTR_ADV(ptr);
                   }
                   prev_indent = indent;
               }
            }
            eeRegFree(regmatch.regprog);
         }
      }
   }
   if (po->breakIndent.vcol != 0) {
      // column value has priority
      bri = po->breakIndent.vcol;
      preList = 0;
   } else
      bri = prev_indent + po->breakIndent.shift;

   // add additional indent for numbered lists
   if (po->breakIndent.list > 0)
      bri += preList;

   // indent minus the length of the showbreak string
   if (po->breakIndent.showBreak && p_sbr)
      bri -= eeglStrSize(p_sbr);

   // never indent past left portal margin
   if (bri < 0) {
      bri = 0;

      // always leave at least bri_min characters on the left, if text width is sufficient
   } ei (bri > eff_wwidth - po->breakIndent.min) {
      bri = (eff_wwidth - po->breakIndent.min < 0) ? 0 : eff_wwidth - po->breakIndent.min;
   }

   return bri;
}

//When extra == 0: Return TRUE if the cursor is before or on the first non-blank in the line.
//When extra == 1: Return TRUE if the cursor is before the first non-blank in the line.
int
inindent(int extra) {
   Byte   *ptr;
   ColNr   col;

   for (col = 0, ptr = ml_get_curline(); SPACE_OR_TAB(*ptr); ++col)
      ++ptr;
   if (col >= curPor->cursor.col + extra)
      return TRUE;
   else
      return FALSE;
}

//op_reindent - handle reindenting a block of lines.
void
op_reindent(Operator *oper, int (*how)(void)) {
   long   i = 0;
   Byte   *l;
   int amount;
   LineNr first_changed = 0;
   LineNr last_changed = 0;
   LineNr start_lnum = curPor->cursor.lnum;

   //Don't even try when @modifiable is off.
   if (!curBook->o.modifiable) {
      emsg(_(e_cannot_make_changes_modifiable_is_off));
      return;
   }

   // Save for undo.  Do this once for all lines, much faster than doing this
   // for each line separately, especially when undoing.
   if (u_savecommon(
         start_lnum - 1, start_lnum + oper->line_count, start_lnum + oper->line_count, FALSE
       ) == OK
   ) {
      for (i = oper->line_count; --i >= 0 && !gotInterruptG; ) {
         //it's a slow thing to do, so give feedback so there's no worry
         //that the computer's just hung.

         if (i > 1 && (i % 50 == 0 || i == oper->line_count - 1))
            smsg(_("%ld lines to indent... "), i);

         l = skipwhite(ml_get_curline());
         if (*l == ZERO || !how)          // empty or blank line
            amount = 0;
         else
            amount = how();       // get the indent for this line

         if (amount >= 0 && set_indent(amount, 0)) {
            // did change the indent, call changed_lines() later
            if (first_changed == 0)
               first_changed = curPor->cursor.lnum;
            last_changed = curPor->cursor.lnum;
         }
         ++curPor->cursor.lnum;
         curPor->cursor.col = 0;  // make sure it's valid
      }
   } 

   //put cursor on first non-blank of indented line
   curPor->cursor.lnum = start_lnum;
   beginline(BL_SOL | BL_FIX);

   //Mark changed lines so that they will be redrawn.  When Visual
   //highlighting was present, need to continue until the last line.  When
   //there is no change still need to remove the Visual highlighting.
   if (last_changed != 0)
      changed_lines(
         first_changed, 0, oper->is_VIsual ? start_lnum + oper->line_count : last_changed + 1, 0L
      );
   ei (oper->is_VIsual)
      drawCurBookLater(UPD_INVERTED);

   i = oper->line_count - (i + 1);
   smsg(NGETTEXT("%ld line indented ", "%ld lines indented ", i), i);
   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
      // set '[ and '] marks
      curBook->opStart = oper->start;
      curBook->opEnd = oper->end;
   }
}

// TRUE if lines starting with '#' should be left aligned.
int
preprocs_left(void) {
   return curBook->o.smartIndent;
}

// TRUE if the conditions are OK for smart indenting.
int
may_do_si(void) {
   return curBook->o.smartIndent && !curBook->o.indentExpr;
}

// Try to do some very smart auto-indenting. Used when inserting a "normal" character.
void
ins_try_si(int c) {
   Pos   *pos, old_pos;
   CS ptr;
   int i;
   int temp;

   // do some very smart indenting when entering '{' or '}'
   if (((didSindentG || can_si_back) && c == '{') || (can_si && c == '}' && inindent(0))) {
      // for '}' set indent equal to indent of line containing matching '{'
      if (c == '}' && (pos = findmatch(NULL, '{')) != NULL) {
         old_pos = curPor->cursor;
         // If the matching '{' has a ')' immediately before it (ignoring white-space), then line 
         // up with the start of the line containing the matching '(' if there is one.  This 
         // handles the case where an "if (..\n..) {" statement continues over multiple
         // lines -- webb
         ptr = ml_get(pos->lnum);
         i = pos->col;
         if (i > 0) {     // skip blanks before '{'
            while (--i > 0 && SPACE_OR_TAB(ptr[i]))
              {}
         } 
         curPor->cursor.lnum = pos->lnum;
         curPor->cursor.col = i;
         if (ptr[i] == ')' && (pos = findmatch(NULL, '(')) != NULL)
            curPor->cursor = *pos;
         i = get_indent();
         curPor->cursor = old_pos;
         (void)set_indent(i, SIN_CHANGED);
      } ei (curPor->cursor.col > 0) {
          // when inserting '{' after "O" reduce indent, but not
          // more than indent of previous line
          temp = TRUE;
          if (c == '{' && can_si_back && curPor->cursor.lnum > 1) {
             old_pos = curPor->cursor;
             i = get_indent();
             while (curPor->cursor.lnum > 1) {
                 ptr = skipwhite(ml_get(--(curPor->cursor.lnum)));

                  // ignore empty lines and lines starting with '#'.
                  if (*ptr != '#' && *ptr != ZERO)
                 break;
               }
               if (get_indent() >= i)
                   temp = FALSE;
               curPor->cursor = old_pos;
           }
          if (temp)
           shift_line(TRUE, FALSE, 1, TRUE);
          }
   }

   // set indent of '#' always to 0
   if (curPor->cursor.col > 0 && can_si && c == '#' && inindent(0)) {
      // remember current indent for next line
      old_indent = get_indent();
      (void)set_indent(0, SIN_CHANGED);
   }

   // Adjust ai_col, the char at this position can be deleted.
   if (ai_col > curPor->cursor.col)
      ai_col = curPor->cursor.col;
}

// Insert an indent (for <Tab> or CTRL-T) or delete an indent (for CTRL-D).
// Keep the cursor on the same character.
// type == INDENT_INC   increase indent (for CTRL-T or <Tab>)
// type == INDENT_DEC   decrease indent (for CTRL-D)
// type == INDENT_SET   set indent to "amount"
// if round is TRUE, round the indent to 'shiftwidth' (only with _INC and _Dec).
void
opChangeIndent(
   int type,
   int amount,
   int round,
   Boole call_changed_bytes // call changed_bytes()
){
   int last_vcol;
   int i;

   // for the following tricks we don't want list mode
   int save_p_list = curPor->o.list;
   curPor->o.list = FALSE;
   ignore_text_props = TRUE;
   ColNr vc = getvcol_nolist(&curPor->cursor);
   int vcol = vc;

   // determine offset from first non-blank
   int necursor_col = curPor->cursor.col;
   beginline(BL_WHITE);
   necursor_col -= curPor->cursor.col;

   int insstart_less = curPor->cursor.col; // reduction for insertStartG.col

   // If the cursor is in the indent, compute how many screen columns the
   // cursor is to the left of the first non-blank.
   if (necursor_col < 0)
      vcol = get_indent() - vcol;

   // Set the new indent.  The cursor will be put on the first non-blank.
   if (type == INDENT_SET)
      (void)set_indent(amount, call_changed_bytes ? SIN_CHANGED : 0);
   else {
      int save_State = stateG;
      shift_line(type == INDENT_DEC, round, 1, call_changed_bytes);
      stateG = save_State;
   }
   insstart_less -= curPor->cursor.col;

   // Try to put cursor on same character.
   // If the cursor is at or after the first non-blank in the line, compute the cursor column 
   // relative to the column of the first non-blank character.
   // If we are not in insert mode, leave the cursor on the first non-blank.
   // If the cursor is before the first non-blank, position it relative
   // to the first non-blank, counted in screen columns.
   if (necursor_col >= 0) {
      // When changing the indent while the cursor is touching it, reset insertStartG_col to 0.
      if (necursor_col == 0)
         insstart_less = MAXCOL;
      necursor_col += curPor->cursor.col;
   } ei (!(stateG & MODE_INSERT))
      necursor_col = curPor->cursor.col;
   else {
      CharTableSize cts;

      // Compute the screen column where the cursor should be.
      vcol = get_indent() - vcol;
      curPor->virtCol = (ColNr)((vcol < 0) ? 0 : vcol);

      // Advance the cursor until we reach the right screen column.
      last_vcol = 0;
      CS ptr = ml_get_curline();
      bookInitCharsForKeywordsSizeArg(&cts, curPor, 0, 0, ptr, ptr);
      while (cts.cts_vcol <= (int)curPor->virtCol) {
         last_vcol = cts.cts_vcol;
         if (cts.cts_vcol > 0)
             MB_PTR_ADV(cts.cts_ptr);
         if (*cts.cts_ptr == ZERO)
             break;
         cts.cts_vcol += lbr_chartabsize(&cts);
      }
      vcol = last_vcol;
      necursor_col = cts.cts_ptr - cts.cts_line;
      clear_chartabsize_arg(&cts);

      // May need to insert spaces to be able to position the cursor on the right screen column.
      if (vcol != (int)curPor->virtCol) {
         curPor->cursor.col = (ColNr)necursor_col;
         i = (int)curPor->virtCol - vcol;
         ptr = alloc(i + 1);
         Unt ptrlen;
         necursor_col += i;
         ptr[i] = ZERO;
         ptrlen = i;
         while (--i >= 0)
            ptr[i] = ' ';
         ins_str(ptr, ptrlen);
         eeglFree(ptr);
      }

      // When changing the indent while the cursor is in it, reset insertStartG_col to 0.
      insstart_less = MAXCOL;
   }

   curPor->o.list = save_p_list;

   if (necursor_col <= 0)
      curPor->cursor.col = 0;
   else
      curPor->cursor.col = (ColNr)necursor_col;
   curPor->setCursWant = true;
   changed_cline_bef_curs();

   // May have to adjust the start of the insert.
   if (stateG & MODE_INSERT) {
      if (curPor->cursor.lnum == insertStartG.lnum && insertStartG.col != 0) {
         if ((int)insertStartG.col <= insstart_less)
            insertStartG.col = 0;
         else
            insertStartG.col -= insstart_less;
      }
      if ((int)ai_col <= insstart_less)
         ai_col = 0;
      else
         ai_col -= insstart_less;
    }

    ignore_text_props = FALSE;
}

// Give a "resulting text too long" error and maybe set gotInterruptG.
private void
emsg_text_too_long(void) {
   emsg(_(e_resulting_text_too_long));
   // when not inside a try/catch set gotInterruptG to break out of any loop
   if (trylevel == 0)
       gotInterruptG = TRUE;
}

// ":retab".
void
c_retab(Invocation *eap) {
   LineNr   lnum;
   int      got_tab = FALSE;
   long   num_spaces = 0;
   long   num_tabs;
   long   len;
   long   col;
   long   vcol;
   long   start_col = 0;      // For start of white-space string
   long   start_vcol = 0;      // For start of white-space string
   long   old_len;
   long   new_len;
   Byte   *ptr;
   CS new_line = (CS)1; // init to non-NULL
   int      did_undo;      // called u_save for current line
   int      temp;
   int      new_ts = 0;
   int      save_list;
   LineNr   first_line = 0;      // first changed line
   LineNr   last_line = 0;      // last changed line
   int      is_indent_only = 0;   // Only process leading whitespace

   save_list = curPor->o.list;
   curPor->o.list = 0;       // don't want list mode here

   ptr = eap->arg;
   if (STRNCMP(ptr, "-indentonly", 11) == 0 && IS_WHITE_OR_ZERO(ptr[11])) {
      is_indent_only = 1;
      ptr = skipwhite(ptr + 11);
   }

   if (ptr[0] != ZERO && (ptr[0] != '0' || ptr[1] != ZERO)) {
       Byte   *end;

       if (STRTOL(ptr, &end, 10) <= 0) {
          if (ptr != end)
              emsg(_(e_argument_must_be_positive));
          else
              showErrFmtMsg(_(e_invalid_argument_str), ptr);
          return;
       }
       new_ts = parseLong(&ptr);
       if (new_ts < 0 || new_ts > TABSIZE_MAX) {
           showErrFmtMsg(_(e_invalid_argument_str), eap->arg);
           return;
       }
   }
   if (new_ts == 0)
      new_ts = curBook->o.shiftWidth;
   for (lnum = eap->line1; !gotInterruptG && lnum <= eap->line2; ++lnum) {
      ptr = ml_get(lnum);
      old_len = ml_get_len(lnum);
      col = 0;
      vcol = 0;
      did_undo = FALSE;
      for (;;) {
         if (SPACE_OR_TAB(ptr[col])) {
              if (!got_tab && num_spaces == 0) {
                 // First consecutive white-space
                 start_vcol = vcol;
                 start_col = col;
              }
              if (ptr[col] == ' ')
                 num_spaces++;
              else
                 got_tab = TRUE;
          } else {
              if (got_tab || (eap->forceit && num_spaces > 1)) {
                  // Retabulate this string of white-space

                  // len is virtual length of white string
                  len = num_spaces = vcol - start_vcol;
                  num_tabs = 0;
                  if (!curBook->o.expandTab) {
                 temp = new_ts - (start_vcol % new_ts);
                 if (num_spaces >= temp) {
                     num_spaces -= temp;
                     num_tabs++;
                 }
                 num_tabs += num_spaces / new_ts;
                 num_spaces -= (num_spaces / new_ts) * new_ts;
                  }
                  if (curBook->o.expandTab || got_tab || (num_spaces + num_tabs < len)) {
                 if (did_undo == FALSE) {
                    did_undo = TRUE;
                    if (u_save((LineNr)(lnum - 1), (LineNr)(lnum + 1)) == FAIL) {
                       new_line = NULL;   // flag out-of-memory
                       break;
                    }
                 }

                 // len is actual number of white characters used
                 len = num_spaces + num_tabs;
                 new_len = old_len - col + start_col + len + 1;
                 if (new_len <= 0 || new_len >= MAXCOL) {
                    emsg_text_too_long();
                    break;
                 }
                 new_line = alloc(new_len);
                 if (start_col > 0)
                    mch_memmove(new_line, ptr, (Unt)start_col);
                 mch_memmove(new_line + start_col + len, ptr + col, (Unt)(old_len - col + 1));
                 ptr = new_line + start_col;
                 for (col = 0; col < len; col++)
                    ptr[col] = (col < num_tabs) ? '\t' : ' ';
                 if (ml_replace(lnum, new_line, FALSE) == OK)
                    // "new_line" may have been copied
                    new_line = curBook->mem.cachedLine;
                 if (first_line == 0)
                    first_line = lnum;
                 last_line = lnum;
                 ptr = new_line;
                 old_len = new_len - 1;
                 col = start_col + len;
                  }
              }
              got_tab = FALSE;
              num_spaces = 0;

              if (is_indent_only)
                  break;
         }
         if (ptr[col] == ZERO)
            break;
         vcol += chartabsize(ptr + col, (ColNr)vcol);
         if (vcol >= MAXCOL) {
            emsg_text_too_long();
            break;
         }
         col += utfCharLen(ptr + col);
      }
      if (new_line == NULL)          // out of memory
         break;
      line_breakcheck();
   }
   if (gotInterruptG)
      emsg(_(e_interrupted));

   if (curBook->o.shiftWidth != new_ts)
      drawCurBookLater(UPD_NOT_VALID);
   if (first_line != 0)
      changed_lines(first_line, 0, last_line + 1, 0L);

   curPor->o.list = save_list;   // restore 'list'

   curBook->o.shiftWidth = new_ts;
   coladvance(curPor->cursWant);

   u_clearline();
}

// Get indent level from @indentexpr
int
get_expr_indent(void) {
   int      indent = -1;
   ScriptPos save_sctx = scriptPosG;

   //Save and restore cursor position and curswant, in case it was changed via :normal commands
   Pos save_pos = curPor->cursor;
   ColNr save_curswant = curPor->cursWant;
   Boole save_set_curswant = curPor->setCursWant;
   set_EeglVar_nr(VV_LNUM, curPor->cursor.lnum);
   ++textlock;
   scriptPosG = curBook->o.scriptLocs[BOOK_indentExpr];

   //Need to make a copy, the @indentexpr option could be changed while evaluating it.
   CS inde_copy = copyStr(curBook->o.indentExpr);
   if (inde_copy) {
      indent = (int)eval_to_number(inde_copy, TRUE);
      eeglFree(inde_copy);
   }

   --textlock;
   scriptPosG = save_sctx;

   //Restore the cursor position so that @indentexpr doesn't need to.
   //Pretend to be in Insert mode, allow cursor past end of line for "o" command.
   int save_State = stateG;
   stateG = MODE_INSERT;
   curPor->cursor = save_pos;
   curPor->cursWant = save_curswant;
   curPor->setCursWant = save_set_curswant;
   check_cursor();
   stateG = save_State;

   // Reset did_throw, unless 'debug' has "throw" and inside a try/catch.
   if (did_throw && ((p_debug && firstOccurrence(p_debug, 't') == NULL) || trylevel == 0)) {
      handle_did_throw();
      did_throw = FALSE;
   }

   // If there is an error, just keep the current indent.
   if (indent < 0)
      indent = get_indent();

   return indent;
}

// Re-indent the current line, based on the current contents of it and the surrounding lines. 
// Fixing the cursor position seems really easy -- I'm very confused what all the part that 
// handles Control-T is doing that I'm not. "get_the_indent" should be get_c_indent 
// or get_expr_indent
private void
fixthisline(int (*get_the_indent)(void)) {
   int amount = get_the_indent();

   if (amount < 0)
      return;

   opChangeIndent(INDENT_SET, amount, 0, TRUE);
   if (linewhite(curPor->cursor.lnum))
      didAindentG = true;   // delete the indent if the line stays empty
}


// TRUE if current book has expression-based indenting.
Boole
jugIsIndentationExpressionBased(void) {
   return curBook->o.indentExpr != null;
}

// Fix indent for 'expr' indentation
void
fix_indent(void) {
   if (jugIsIndentationExpressionBased())
      do_expr_indent();
}

void
f_indent(Arr(Var) argVars, OUT Var* returnVar) {
   LineNr lnum = tv_get_lnum(argVars);
   if (lnum >= 1 && lnum <= curBook->mem.lineCount)
      returnVar->number = get_indent_lnum(lnum);
   else {
      returnVar->number = -1;
   }
}

// TRUE if the string "line" starts with a word from @cinwords
private Boole
cin_is_cinword(CS line) {
   if (!curBook->o.indentKeywords)
      return false;

   line = skipwhite(line);
   CS p = curBook->o.indentKeywords;
   for (CS comma = skipToComma(p); *p != ZERO; p = comma + 1, comma = skipToComma(p)) {
      int len = comma - p;
      if (STRNCMP(line, p, len) == 0 && !eeIsWordc(line[len])) {
         return true;
      }
   }
   return false;
}

//Skip to the end of a "string" literal and a 'c' character.
//If there is no string or character, return argument unmodified.
private CS
skipStringLiteral(CS p) {
   int       i;

   // We loop because strings may be concatenated: "date""time".
   for ( ; ; ++p) {
      if (p[0] == '\'') {         // 'c' or '\n' or '\000'
         if (p[1] == ZERO)          // ' at end of line
            break;
         i = 2;
         if (p[1] == '\\' && p[2] != ZERO) {   // '\n' or '\000'
            ++i;
         while (eeIsDigit(p[i - 1]))   // '\000'
             ++i;
         }
         if (p[i - 1] != ZERO && p[i] == '\'') {   // check for trailing '
            p += i;
            continue;
         }
      } ei (p[0] == '"') {        // start of string
         for (++p; p[0]; ++p) {
            if (p[0] == '\\' && p[1] != ZERO)
               ++p;
            ei (p[0] == '"')       // end of string
               break;
         }
         if (p[0] == '"')
            continue; // continue for another string
      } ei (p[0] == 'R' && p[1] == '"') {
         // Raw string: R"[delim](...)[delim]"
         CS delim = p + 2;
         CS paren = firstOccurrence(delim, '(');

         if (paren) {
            Unt delim_len = paren - delim;

            for (p += 3; *p; ++p) {
               if (p[0] == ')' && STRNCMP(p + 1, delim, delim_len) == 0 && p[delim_len + 1] == '"') {
                  p += delim_len + 1;
                  break;
               }
            } 
            if (p[0] == '"')
               continue; // continue for another string
         }
      }
      break;                // no string found
   }
   if (!*p)
      --p;                // backup from ZERO
   return p;
}


// TRUE if "line[col]" is inside a C string.
int
is_pos_in_string(CS line, ColNr col) {
   CS p;

   for (p = line; *p && (ColNr)(p - line) < col; ++p)
      p = skipStringLiteral(p);
   return !((ColNr)(p - line) <= col);
}

Pos*
find_start_comment(int ind_maxcomment)   {// XXX
   Pos* pos;
   int cur_maxcomment = ind_maxcomment;

   for (;;) {
      pos = findmatchlimit(NULL, '*', FM_BACKWARD, cur_maxcomment);
      if (!pos)
         break;

      //Check if the comment start we found is inside a string.
      //If it is then restrict the search to below this line and try again.
      if (!is_pos_in_string(ml_get(pos->lnum), pos->col))
          break;
      cur_maxcomment = curPor->cursor.lnum - pos->lnum - 1;
      if (cur_maxcomment <= 0) {
         pos = NULL;
         break;
      }
   }
   return pos;
}

// Do expression indenting on the current line.
void
do_expr_indent(void) {
   if (*curBook->o.indentExpr != ZERO)
      fixthisline(&get_expr_indent);
}

//}}}
