//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## book.c: functions for dealing with the Book structure (i.e. open files)

//The book list is a double linked list of all books.
//Each book can be in one of these states:
//never loaded: BF_NEVERLOADED is set, only the file name is valid
//  not loaded: mem.mfile == NULL, no memfile allocated
//      hidden: countPortals == 0, loaded but not displayed in a portal
//      normal: loaded and displayed in a portal
//
//Instead of storing file names all over the place, each file name is
//stored in the book list. It can be referenced by a number.
//
//The current implementation remembers all file names ever used.

#include "eegl.h"
#include <sys/stat.h> // for stat

private int text_prop_type_valid(Book* book, TextProp* prop);
//{{{builtins. Book related builtin functions

//Mark references in functions of books.
Boole
setRefInBooks(int copyID) {
   Boole abort = false;
   Book* bp;
   FOR_ALL_BOOKS(bp) {
      for (Listener* lnr = bp->listener; !abort && lnr; lnr = lnr->next)
         abort = abort || memSetRefInCallback(&lnr->callback, copyID);
      if (abort)
         return TRUE;
         
      abort = abort 
         || memSetRefInCallback(&bp->promptCallback, copyID)
         || memSetRefInCallback(&bp->promptInterrupt, copyID)
         || memSetRefInCallback(bp->o.completeFn, copyID)
         || memSetRefInCallback(bp->o.omniFn, copyID)
         || memSetRefInCallback(bp->o.thesaurusFn, copyID);
      if (!abort && bp->o.completeFn)
         abort = abort || memSetRefInCallback(bp->o.completeFn, copyID);
      if (!abort)
         abort = abort || memSetRefInCallback(bp->o.tagFn, copyID)
                       || memSetRefInCallback(bp->o.findFn, copyID);
      if (abort)
         return true;
   }
   return false;
}

Book*
bookFindByName(CS name, Boole curtab_only) {
   return bookFindFileByBookNr(
      booklistFindPattern(name, name + STRLEN(name), true, false, curtab_only)
   );
}

// Find a book by number or exact name.
Book*
findBook(Var* avar){
   Book* book = NULL;

   if (avar->tag == VAR_NUMBER)
      book = bookFindFileByBookNr((int)avar->number);
   ei (avar->tag == VAR_STRING && avar->string) {
      book = booklistFindByNameExpandingLinks(avar->string);
      if (!book) {
         //No full path name match, try a match with a URL or a "nofile"
         //book, these don't use the full path.
         FOR_ALL_BOOKS(book) {
            if (book->currFileName
                  && (path_with_url(book->currFileName) || bt_nofilename(book))
                  && STRCMP(book->currFileName, avar->string) == 0)
               break;
         } 
      }
   }
   return book;
}

// If there is a portal for "curBook", make it the current portal.
private void
findPortalIntoCurBook(void) {
   //The portInfos list should have the portals that recently showed the book, going over this is
   //faster than going over all the portals. Do check the book is still there.
   PortInfo* poInfo;
   FOR_ALL_BOOK_PORTINFOS(curBook, poInfo) {
      if (poInfo->portal && poInfo->portal->book == curBook) {
         curPor = poInfo->portal;
         break;
      }
   }
}

typedef struct {
   Portal* curPorSave;
   AutocommSave autocommSave;
   Boole usingAco;
   int visualActiveSaved;
} ChangeOtherBook;

//Used before making a change in "book", which is not the current one: Make
//"book" the current book and find a portal into this book, so that side
//effects are done correctly (e.g., adjusting marks).
//
//Information is saved in "cob" and MUST be restored by calling restoreChangeInOtherBook().
//If this fails then "curBook" will not be equal to "book".
private void
prepareChangeInOtherBook(ChangeOtherBook *cob, Book* book) {
   CLEAR_POINTER(cob);

   // Set "curBook" to the book being changed.  Then make sure there is a
   // portal for it to handle any side effects.
   cob->visualActiveSaved = VIsual_active;
   VIsual_active = FALSE;
   cob->curPorSave = curPor;
   curBook = book;
   findPortalIntoCurBook();  // simplest: find existing portal into "book"

   if (curPor->book != book) {
      //No existing portal into this book. It is dangerous to have
      //curPor->book differ from "curBook", use the autocmd portal.
      curBook = curPor->book;
      auCommPrepareBook(&cob->autocommSave, book);
      if (curBook == book)
         cob->usingAco = true;
   }
}

private void
restoreChangeInOtherBook(ChangeOtherBook* cob) {
   if (cob->usingAco) {
      auCommRestoreBook(&cob->autocommSave);
   } else {
      curPor = cob->curPorSave;
      curBook = curPor->book;
   }
   VIsual_active = cob->visualActiveSaved;
}

//Set line or list of 'em in book "book" to "lines". Any type is allowed and converted to a string
private void
updateLinesFromVars(
   Book* book,
   LineNr lnum_arg,
   Boole append,
   Arr(Var) lines,
   Var* returnVar
) {
   LineNr    lnum = lnum_arg + (append ? 1 : 0);
   CS line = NULL;
   List* l = NULL;
   ListItem* li = NULL;
   long   added = 0;
   LineNr   appendLnum;

   // When using the current book mfile will be set if needed.  Useful when
   // setline() is used on startup.  For other books the book must be loaded.
   Boole isCurBook = book == curBook;
   if (!book || (!isCurBook && book->mem.mfile == NULL) || lnum < 1) {
      returnVar->number = FAIL;
      return;
   }

   // After this don't use "return", goto "cleanup"!
   ChangeOtherBook cob;
   if (!isCurBook)
      // set "curBook" to "book" and find a portal for this book
      prepareChangeInOtherBook(&cob, book);

   if (append)
      // appendbufline() uses the line number below which we insert
      appendLnum = lnum - 1;
   else
      // setbufline() uses the line number above which we insert, we only
      // append if it's below the last line
      appendLnum = curBook->mem.lineCount;

   if (lines->tag == VAR_LIST) {
      l = lines->list;
      if (!l || list_len(l) == 0) {
         // not appending anything always succeeds
         goto done;
      }
      CHECK_LIST_MATERIALIZE(l);
      li = l->first;
   } else
      line = typval_tostring(lines, FALSE);

   // default result is zero == OK
   for (;;) {
      if (l) {
         // list argument, get next string
         if (!li)
            break;
         eeglFree(line);
         line = typval_tostring(&li->c, FALSE);
         li = li->next;
      }

      returnVar->number = FAIL;
      if (!line || lnum > curBook->mem.lineCount + 1)
         break;

      // When coming here from Insert mode, sync undo, so that this can be
      // undone separately from what was previously inserted.
      if (u_sync_once == 2) {
          u_sync_once = 1; // notify that u_sync() was called
          u_sync(TRUE);
      }

      if (!append && lnum <= curBook->mem.lineCount) {
         // Existing line, replace it. Removes any existing text properties.
         if (u_savesub(lnum) == OK && ml_replace_len(
               lnum, line, (ColNr)STRLEN(line) + 1, TRUE, TRUE) == OK
         ) {
            changed_bytes(lnum, 0);
            if (isCurBook && lnum == curPor->cursor.lnum)
               check_cursor_col();
            returnVar->number = OK;
         }
      } ei (added > 0 || u_save(lnum - 1, lnum) == OK) {
         // append the line
         ++added;
         if (ml_append(lnum - 1, line, (ColNr)0, FALSE) == OK)
            returnVar->number = OK;
      }

      if (!l)         // only one string argument
         break;
      ++lnum;
   }
   eeglFree(line);

   if (added > 0) {
      Portal       *po;
      Tab   *t;

      appended_lines_mark(appendLnum, added);

      // Only adjust the cursor for books other than the current, unless it
      // is the current portal.  For curBook and other portals it has been
      // done in mark_adjust_internal().
      FOR_ALL_TAB_PORTALS(t, po) {
         if (po->book == book
                && (po->book != curBook || po == curPor)
                && po->cursor.lnum > appendLnum)
            po->cursor.lnum += added;
      } 
      check_cursor_col();

      //Only update the portal view if book matches curBook, otherwise
      //the computations will be wrong.
      if (curPor->book == curBook)
         update_topline();
   }

done:
   if (!isCurBook)
      restoreChangeInOtherBook(&cob);
}

// "append(lnum, string/list)" function
void
f_append(Var *argvars, Var *returnVar) {
   int      anyEmsgSaved = anyEmsgG;
   LineNr lnum = tv_get_lnum(&argvars[0]);
   if (anyEmsgG == anyEmsgSaved)
      updateLinesFromVars(curBook, lnum, true, argvars + 1, returnVar);
}

// Set or append lines to a book.
private void
setOrAppendLines(Arr(Var) argvars, Var *returnVar, Boole append) {
   int anyEmsgSaved = anyEmsgG;

   Book* book = daGetBook(&argvars[0], false);
   if (!book)
      returnVar->number = FAIL;
   else {
      LineNr lnum = daGetLnumFromBookOrVar(&argvars[1], book);
      if (anyEmsgG == anyEmsgSaved)
         updateLinesFromVars(book, lnum, append, argvars + 2, returnVar);
   }
}

// "appendbufline(book, lnum, string/list)" function
void
f_appendbufline(Var *argvars, Var *returnVar) {
   setOrAppendLines(argvars, returnVar, true);
}

// "bufadd(expr)" function
void
f_bufadd(Var *argvars, Var *returnVar) {
   CS name = tv_get_string(&argvars[0]);
   returnVar->number = bookOpen(*name == ZERO ? NULL : name, 0);
}

void
f_bufexists(Var *argvars, Var *returnVar) {
   returnVar->number = (findBook(&argvars[0]) != NULL);
}

void
f_buflisted(Var *argvars, Var *returnVar) {
   Book* book = findBook(&argvars[0]);
   returnVar->number = (book && book->o.bookListed);
}

void
f_bufload(Arr(Var) argvars, Var *returnVar UNUSED) {
   Book* book = evGetBookArg(argvars);
   if (book)
      bookEnsureLoaded(book);
}

void
f_bufloaded(Var *argvars, Var *returnVar) {
   Book* book = findBook(&argvars[0]);
   returnVar->number = (book && book->mem.mfile);
}

void
f_bufname(Var *argvars, Var *returnVar) {
   Var* tv = &argvars[0];
   Book* book = (tv->tag == VAR_UNKNOWN) ? curBook : daGetBookFromArg(tv);
   returnVar->tag = VAR_STRING;
   returnVar->string = (book && book->currFileName) ? copyStr(book->currFileName) : null;
}

void
f_bufnr(Var *argvars, Var *returnVar) {
   Boole error = false;
   CS name;

   Book* book = (argvars[0].tag == VAR_UNKNOWN) ? curBook : daGetBookFromArg(&argvars[0]);

   // If the book isn't found and the second argument is not 0, create a new book.
   if (!book
        && argvars[1].tag != VAR_UNKNOWN
        && varGetNumberChk(&argvars[1], OUT &error) != 0
        && !error
        && (name = convertVarToStringSingleUse(&argvars[0])) != NULL
        && !error
   )
      book = bookNew(name, NULL, (LineNr)1, 0);

   if (book)
      returnVar->number = book->fiNum;
   else
      returnVar->number = -1;
}

private void
bufPortalCommon(Var *argvars, Var *returnVar, int get_nr) {
   Portal   *po;
   int      winnr = 0;
   Book* book = daGetBookFromArg(&argvars[0]);
   FOR_ALL_PORTALS(po) {
      ++winnr;
      if (po->book == book)
         break;
   }
   returnVar->number = (po ? (get_nr ? winnr : po->id) : -1);
}

// "bufwinid(nr)" function
void
f_bufwinid(Var *argvars, Var *returnVar) {
   bufPortalCommon(argvars, returnVar, FALSE);
}

// "bufwinnr(nr)" function
void
f_bufwinnr(Var *argvars, Var *returnVar){
   bufPortalCommon(argvars, returnVar, TRUE);
}

void
f_deletebufline(Var *argvars, Var *returnVar) {
   LineNr   last;
   LineNr   lnum;
   long   count;
   Tab   *tp;
   Portal   *po;
   int      anyEmsgSaved = anyEmsgG;

   returnVar->number = 1;   // FAIL by default

   Book* book = daGetBook(&argvars[0], FALSE);
   if (!book)
      return;

   LineNr first = daGetLnumFromBookOrVar(&argvars[1], book);
   if (anyEmsgG > anyEmsgSaved)
      return;
   if (argvars[2].tag != VAR_UNKNOWN)
      last = daGetLnumFromBookOrVar(&argvars[2], book);
   else
      last = first;

   if (book->mem.mfile == NULL || first < 1 || first > book->mem.lineCount || last < first)
      return;

   // After this don't use "return", goto "cleanup"!
   Boole isCurBook = book == curBook;
   ChangeOtherBook cob;
   if (!isCurBook)
      // set "curBook" to "book" and find a portal into this book
      prepareChangeInOtherBook(&cob, book);

   if (last > curBook->mem.lineCount)
     last = curBook->mem.lineCount;
   count = last - first + 1;

   // When coming here from Insert mode, sync undo, so that this can be
   // undone separately from what was previously inserted.
   if (u_sync_once == 2) {
      u_sync_once = 1; // notify that u_sync() was called
      u_sync(TRUE);
   }

   if (u_save(first - 1, last + 1) == FAIL)
      goto cleanup;

   for (lnum = first; lnum <= last; ++lnum)
      ml_delete_flags(first, ML_DEL_MESSAGE);

   FOR_ALL_TAB_PORTALS(tp, po) {
      if (po->book == book) {
         if (po->cursor.lnum > last)
            po->cursor.lnum -= count;
         ei (po->cursor.lnum > first)
            po->cursor.lnum = first;
         if (po->cursor.lnum > po->book->mem.lineCount)
            po->cursor.lnum = po->book->mem.lineCount;
         po->cacheState = 0;
         if (po->cursor.lnum <= po->topLine)
            po->topLine = 1;
      }
   } 
   check_cursor_col();
   deleted_lines_mark(first, count);
   returnVar->number = 0; // OK

cleanup:
  if (!isCurBook)
     restoreChangeInOtherBook(&cob);
}

// Find the lnum for the book 'book' for the current portal.
private LineNr
findLnum(Book* book) {
   return bookFindFpos(book)->lnum;
}

// Return book options, variables and other attributes in a dictionary.
private Bag *
getBookInfo(Book* book) {
   Tab   *tp;
   Portal   *po;

   Bag* bag = allocBag();
   bagAddNumber(bag, S"bufnr", book->fiNum);
   bagAddString(bag, S"name", book->fullFileName);
   bagAddNumber(bag, S"lnum", book == curBook ? curPor->cursor.lnum : findLnum(book));
   bagAddNumber(bag, S"linecount", book->mem.lineCount);
   bagAddNumber(bag, S"loaded", book->mem.mfile != null);
   bagAddNumber(bag, S"listed", book->o.bookListed);
   bagAddNumber(bag, S"changed", doWasBookChanged(book));
   bagAddNumber(bag, S"changedtick", CHANGEDTICK(book));
   bagAddNumber(bag, S"hidden", book->mem.mfile && book->countPortals == 0);
   bagAddNumber(bag, S"command", book == commPortBookG);

   // Get a reference to book variables
   bagAddBag(bag, S"variables", book->bVars);

   // List of portals displaying this book
   List* portals = list_alloc();
   FOR_ALL_TAB_PORTALS(tp, po) {
      if (po->book == book)
         list_append_number(portals, (Long)po->id);
   } 
   bagAddList(bag, S"portals", portals);

   // List of popup portals displaying this book
   portals = list_alloc();
   FOR_ALL_POPUPPORTS(po) {
      if (po->book == book)
         list_append_number(portals, (Long)po->id);
   } 
   FOR_ALL_TABS(tp) {
      FOR_ALL_POPUPPORTS_IN_TAB(tp, po) {
         if (po->book == book)
            list_append_number(portals, (Long)po->id);
      } 
   } 

   bagAddList(bag, S"popups", portals);

   if (book->signList) {
      // List of signs placed in this book
      List* signs = list_alloc();
      markGetBookSigns(book, signs);
      bagAddList(bag, S"signs", signs);
   }

   bagAddNumber(bag, S"lastused", book->lastUsed);

   return bag;
}

void
f_getbufinfo(Var *argvars, Var *returnVar) {
   Book   *book = NULL;
   Book   *argbuf = NULL;
   int      filtered = FALSE;
   int      sel_buflisted = FALSE;
   int      sel_bufloaded = FALSE;
   int      sel_bufmodified = FALSE;

   allocReturnList(returnVar);

   // List of all the books or selected books
   if (argvars[0].tag == VAR_BAG) {
      Bag* selB = argvars[0].bag;

      if (selB) {
         filtered = TRUE;
         sel_buflisted = bagGetBool(selB, tConst("buflisted"), false);
         sel_bufloaded = bagGetBool(selB, tConst("bufloaded"), false);
         sel_bufmodified = bagGetBool(selB, tConst("bufmodified"), false);
      }
   } ei (argvars[0].tag != VAR_UNKNOWN) {
      //Information about one book. Argument specifies the book
      argbuf = daGetBookFromArg(&argvars[0]);
      if (!argbuf)
         return;
   }

   //Return information about all the books or a specified book
   FOR_ALL_BOOKS(book) {
      if (argbuf && argbuf != book)
         continue;
      if (filtered && ((sel_bufloaded && book->mem.mfile == NULL)
            || (sel_buflisted && !book->o.bookListed)
            || (sel_bufmodified && !book->wasModified)))
         continue;

      Bag* b = getBookInfo(book);
      if (b)
         listAppendBag(returnVar->list, b);
      if (argbuf)
         return;
   }
}

//Get line or list of lines from book "book" into "returnVar".
//Return a range (from start to end) of lines in returnVar from the specified book.
//If 'retlist' is TRUE, then the lines are returned as an Eegl List.
private void
getLinesIntoVar(
   Book   *book,
   LineNr   start,
   LineNr   end,
   int      retlist,
   OUT Var   *returnVar
) {
   CS p;

   if (retlist) {
      allocReturnList(returnVar);
   } else {
      returnVar->tag = VAR_STRING;
      returnVar->string = NULL;
   }

   if (!book || book->mem.mfile == NULL || start < 0)
      return;
      
   if (!retlist) {
      p = (start >= 1 && start <= book->mem.lineCount) ? memGetLine(book, start, FALSE) : E;
      returnVar->string = copyStr(p);
   } else {
      if (end < start)
         return;

      if (start < 1)
         start = 1;
      if (end > book->mem.lineCount)
         end = book->mem.lineCount;
      while (start <= end)
         if (list_append_string(returnVar->list, memGetLine(book, start++, FALSE), -1) == FAIL)
            break;
   }
}

//"retlist" TRUE: "getBookLineIntoVar()" function
//"retlist" FALSE: "getbufoneline()" function
private void
getBookLineIntoVar(Var* argvars, Var* returnVar, int retlist) {
   LineNr   lnum = 1;
   LineNr   end = 1;
   int      anyEmsgSaved = anyEmsgG;

   Book* book = daGetBookFromArg(&argvars[0]);
   if (book) {
      lnum = daGetLnumFromBookOrVar(&argvars[1], book);
      if (anyEmsgG > anyEmsgSaved)
         return;
      if (argvars[2].tag == VAR_UNKNOWN)
         end = lnum;
      else
         end = daGetLnumFromBookOrVar(&argvars[2], book);
    }

    getLinesIntoVar(book, lnum, end, retlist, returnVar);
}

void
f_getbufline(Var *argvars, Var *returnVar) {
   getBookLineIntoVar(argvars, returnVar, TRUE);
}

void
f_getbufoneline(Var *argvars, Var *returnVar) {
   getBookLineIntoVar(argvars, returnVar, FALSE);
}

//"getline(lnum, [end])" function
void
f_getline(Var *argvars, Var *returnVar) {
   LineNr   end;
   int      retlist;
   LineNr lnum = tv_get_lnum(argvars);
   if (argvars[1].tag == VAR_UNKNOWN) {
      end = 0;
      retlist = FALSE;
   } else {
      end = tv_get_lnum(&argvars[1]);
      retlist = TRUE;
   }

   getLinesIntoVar(curBook, lnum, end, retlist, returnVar);
}

void
f_setbufline(Var *argvars, Var *returnVar) {
   setOrAppendLines(argvars, returnVar, false);
}

void
f_setline(Var *argvars, Var *returnVar) {
   int      anyEmsgSaved = anyEmsgG;

   LineNr lnum = tv_get_lnum(&argvars[0]);
   if (anyEmsgG == anyEmsgSaved)
      updateLinesFromVars(curBook, lnum, false, argvars + 1, returnVar);
}

//}}}
//{{{charset

private int parseAnIsOption(CS var, Book* book, Boole only_check);
private int win_nolbr_chartabsize(CharTableSize *cts, int *headp);

private Boole chartab_initialized = false;

// charsForKeywords[] is an array of 32 bytes, each bit representing one of the
// characters 0-255.
#define SET_CHARTAB(book, c) (book)->charsForKeywords[(unsigned)(c) >> 3] |= (1 << ((c) & 0x7))
#define RESET_CHARTAB(book, c) (book)->charsForKeywords[(unsigned)(c) >> 3] &= ~(1 << ((c) & 0x7))
#define GET_CHARTAB(book, c) ((book)->charsForKeywords[(unsigned)(c) >> 3] & (1 << ((c) & 0x7)))

// table used below, see bookInitCharsForKeywordsForCurbook() for an explanation
private Byte g_chartab[256];

// Flags for g_chartab[].
#define CT_CELL_MASK   0x07   // mask: nr of display cells (1, 2 or 4)
#define CT_PRINT_CHAR  0x10   // flag: set for printable chars
#define CT_ID_CHAR     0x20   // flag: set for ID chars
#define CT_FNAME_CHAR  0x40   // flag: set for file name chars

private int inPortalBorder(Portal *po, ColNr vcol);

//Fill g_chartab[]. Also fill curBook->charsForKeywords[] with flags for keyword
//characters for current book.
//
//Depend on the option settings 'iskeyword', 'isident', 'isfname', and 'isprint'.
//
//- For UTF-8 index with the character (when first byte is up to 0x80 it is
//  the same as the character, if the first byte is 0x80 and above it depends on further bytes).
//
//The contents of g_chartab[]:
//- The lower two bits, masked by CT_CELL_MASK, give the number of display
//  cells the character occupies (1 or 2).  Not valid for UTF-8 above 0x80.
//- CT_PRINT_CHAR bit is set when the character is printable (no need to
//  translate the character before displaying it).  Note that only DBCS
//  characters can have 2 display cells and still be printable.
//- CT_FNAME_CHAR bit is set when the character can be in a file name.
//- CT_ID_CHAR bit is set when the character can be in an identifier.
//
//Return FAIL if @iskeyword, @isident, @isfname or @isprint option has an error, OK otherwise.
private int
bookInitCharsForKeywords(Book* book) {
   //Init word char flags all to FALSE
   CLEAR_FIELD(book->charsForKeywords);

   // Walk through the 'isident', 'iskeyword', 'isfname' and 'isprint' options.
   for (int i = 0; i < 3; ++i) {
      CS p;
      if (i == 0)
         p = p_isi;      // first round: 'isident'
      ei (i == 1)
         p = p_isf;      // third round: 'isfname'
      else   // i == 2
         p = book->o.isKeyword;   // fourth round: 'iskeyword'
         
      if (p && parseAnIsOption(p, book, false) == FAIL)
         return FAIL;
   }

   chartab_initialized = true;
   return OK;
}

int
bookInitCharsForKeywordsForCurbook(void) {
   return curBook ? bookInitCharsForKeywords(curBook) : OK;
}

void
bookInitGlobalCharsForKeywords() {
   //Set the default size for printable characters:
   //From <Space> to '~' is 1 (printable), others are 2 (not printable).
   //This also inits all 'isident' and 'isfname' flags to FALSE.
   Unt c = 0;
   while (c < ' ')
      g_chartab[c++] = 2;
   while (c <= '~')
      g_chartab[c++] = 1 + CT_PRINT_CHAR;
   while (c < 256) {
      // UTF-8: bytes 0xa0 - 0xff are printable (latin1)
      if (c >= 0xa0)
         g_chartab[c++] = CT_PRINT_CHAR + 1;
    else
      // the rest is unprintable by default
      g_chartab[c++] = 2;
   }

   // Assume that every multi-byte char is a filename character.
   for (c = 1; c < 256; ++c) {
      if (c >= 0xa0)
         g_chartab[c] |= CT_FNAME_CHAR;
   } 
}

//Strict version of bookIsCharPrintable(c), don't return TRUE if "c" is the head byte of a double-byte 
//character
int
bookIsCharPrintable_strict(int c) {
   if (c >= 0x100)
      return utf_printable(c);
   return (c >= 0x100 || (c > 0 && (g_chartab[c] & CT_PRINT_CHAR)));
}

//Parse an "is" option: @iskeyword, @isident, @isfname, @isprint. Return OK/FAIL.
private int
parseAnIsOption(CS var, Book* book, Boole only_check) {  // FALSE: refill g_chartab[]
   CS p = var;
   long c;
   int tilde;

   //Parse the 'isident', 'iskeyword', 'isfname' and 'isprint' options. Each option is a list of 
   //characters, character numbers or ranges, separated by commas, e.g.: "200-210,x,#-178,-"
   while (*p != ZERO) {
      tilde = FALSE;
      Boole do_isalpha = false;
      if (*p == '^' && p[1] != ZERO) {
         tilde = TRUE;
         ++p;
      }
      if (EE_ISDIGIT(*p))
         c = parseLong(&p);
      else
         c = inpAdvanceMultibyte(&p);
         
      Unt c2 = UNT;
      if (*p == '-' && p[1] != ZERO) {
         ++p;
         if (EE_ISDIGIT(*p))
            c2 = parseLong(&p);
         else
            c2 = inpAdvanceMultibyte(&p);
         c2 = *p++;
      }
      if (c <= 0 || c >= 256 || (c2 < c && c2 != UNT) || c2 >= 256 || !(*p == ZERO || *p == ','))
         return FAIL;

      Boole trail_comma = *p == ',';
      p = skip_to_option_part(p);
      if (trail_comma && *p == ZERO)
         // Trailing comma is not allowed.
         return FAIL;

      if (only_check)
         continue;

      if (c2 == UNT) {   // not a range
         //A single '@' (not "@-@"):
         //Decide on letters being ID/printable/keyword chars with
         //standard function isalpha(). This takes care of locale for single-byte characters).
         if (c == '@') {
            do_isalpha = true;
            c = 1;
            c2 = 255;
         } else
            c2 = c;
      }

      while (c <= c2) {
         if (!do_isalpha || MB_ISLOWER(c) || MB_ISUPPER(c)) {
            if (var == p_isi) {         // (re)set ID flag
               if (tilde)
                  g_chartab[c] &= ~CT_ID_CHAR;
               else
                  g_chartab[c] |= CT_ID_CHAR;
            } ei (var == p_isf) {        // (re)set fname flag
               if (tilde)
                  g_chartab[c] &= ~CT_FNAME_CHAR;
               else
                  g_chartab[c] |= CT_FNAME_CHAR;
            } else {// var == book->o.isKeyword (re)set keyword flag
               if (tilde)
                  RESET_CHARTAB(book, c);
               else
                  SET_CHARTAB(book, c);
            }
         }
         ++c;
      }
   }

   return OK;
}

//Catch 22: g_chartab[] can't be initialized before the options are initialized, and initializing 
//options may cause transchar() to be called!
//When chartab_initialized == FALSE don't use g_chartab[].
//Does NOT work for multi-byte characters, c must be <= 255.
//Also doesn't work for the first byte of a multi-byte, "c" must be a character!
private Byte translateScratch[7];

CS
transchar(Unt c) {
   return transchar_buf(c);
}

CS
transchar_buf(Unt c) {
   int i = 0;
   if (IS_SPECIAL(c)) {      // special key code, display as ~@ char
      translateScratch[0] = '~';
      translateScratch[1] = '@';
      i = 2;
      c = K_SECOND(c);
   }

   if ((!chartab_initialized && ((c >= ' ' && c <= '~'))) || (c < 256 && bookIsCharPrintable_strict(c))) {
      // printable character
      translateScratch[i] = c;
      translateScratch[i + 1] = ZERO;
   } else
      transchar_nonprint(translateScratch + i, c);
   return translateScratch;
}

// Like transchar_buf(), but called with a byte instead of a character.  Checks
// for an illegal UTF-8 byte.
CS
transchar_byte(Unt c) {
   if (c >= 0x80) {
      transchar_nonprint(translateScratch, c);
      return translateScratch;
   }
   return transchar_buf(c);
}

//Convert non-printable character to two or more printable characters in
//"charbuf[]".  "charbuf" needs to be able to hold five bytes.
//Does NOT work for multi-byte characters, c must be <= 255.
void
transchar_nonprint(CS charbuf, int c) {
   if (c == NL)
      c = ZERO;      // we use newline in place of a ZERO

   ei (c <= 0x7f) {       // 0x00 - 0x1f and 0x7f
      charbuf[0] = '^';
      charbuf[1] = c ^ 0x40;      // DEL displayed as ^?
      charbuf[2] = ZERO;
   } else {
      transchar_hex(charbuf, c);
   }
}


//Return number of display cells occupied by byte "b". Caller must make sure 0 <= b <= 255.
//For multi-byte mode "b" must be the first byte of a character.
//A TAB is counted as two cells: "^I".
//Will return 0 for bytes >= 0x80, because the number of cells depends on further bytes.
int
byte2cells(Unt b) {
   if (b >= 0x80)
      return 0;
   return (g_chartab[b] & CT_CELL_MASK);
}

//Return number of display cells occupied by character "c".
//"c" can be a special key (negative number) in which case 3 or 4 is returned.
//A TAB is counted as two cells: "^I" or four: "<09>".
int
char2cells(Unt c) {
   if (IS_SPECIAL(c))
      return char2cells(K_SECOND(c)) + 2;
   if (c >= 0x80) {
      // UTF-8: above 0x80 need to check the value
      return mb_char2cells(c);
   } else {
      return (g_chartab[c & 0xff] & CT_CELL_MASK);
   }
}

//Return number of display cells occupied by character at "*p".
//A TAB is counted as two cells: "^I" or four: "<09>".
int
ptr2cells(CS p) {
   // For UTF-8 we need to look at more bytes if the first byte is >= 0x80.
   if (*p >= 0x80)
      return mb_ptr2cells(p);
   // For DBCS we can tell the cell count from the first byte.
   return (g_chartab[*p] & CT_CELL_MASK);
}

//Return the number of characters 'c' will take on the screen, taking into account the size of a tab
//Use a define to make it fast, this is used very often!!! Also see getvcol() below.
# define RET_PORT_BOOK_CHARSIZE(po, book, p, col) \
   if (*(p) == TAB && (!(po)->o.list || listCharsG.tab1)) { \
      int ts; \
      ts = (book)->o.shiftWidth; \
      return (int)(ts - (col % ts)); \
   } else \
      return ptr2cells(p);

int
chartabsize(CS p, ColNr col) {
   RET_PORT_BOOK_CHARSIZE(curPor, curBook, p, col)
}

int
win_chartabsize(Portal *po, CS p, ColNr col) {
   RET_PORT_BOOK_CHARSIZE(po, po->book, p, col)
}

// Return the number of characters the string "s" will take on the screen, taking into account the 
// size of a tab. Does not handle text properties, since "s" is not a book line.
Unt
linetabsize_str(CS s) {
   return linetabsize_col(0, s);
}

// Like linetabsize_str(), but "s" starts at column "startcol".
int
linetabsize_col(int startcol, CS s) {
   CharTableSize cts;

   bookInitCharsForKeywordsSizeArg(&cts, curPor, 0, startcol, s, s);
   Long vcol = cts.cts_vcol;

   while (*cts.cts_ptr != ZERO) {
      vcol += lbr_chartabsize_adv(&cts);
      if (vcol > MAXCOL) {
         cts.cts_vcol = MAXCOL;
         break;
      } else
         cts.cts_vcol = (int)vcol;
   }
   clear_chartabsize_arg(&cts);
   return (int)cts.cts_vcol;
}

//Like linetabsize_str(), but for a given portal instead of the current one.
//Doesn't count the size of @listchars "eol".
Unt
drawLineOnScreentabsize(Portal* po, LineNr lnum, CS line, ColNr len) {
   CharTableSize cts;
   bookInitCharsForKeywordsSizeArg(&cts, po, lnum, 0, line, line);
   drawLineOnScreentabsize_cts(&cts, len);
   clear_chartabsize_arg(&cts);
   return (Unt)cts.cts_vcol;
}

//Return the number of cells line "lnum" of portal "po" will take on the screen, taking into 
//account the size of a tab and text properties. Doesn't count the size of @listchars "eol".
int
linetabsize(Portal *po, LineNr lnum) {
   return drawLineOnScreentabsize(po, lnum, memGetLine(po->book, lnum, FALSE), (ColNr)MAXCOL);
}


// Like linetabsize(), but counts the size of 'listchars' "eol".
int
linetabsize_eol(Portal *po, LineNr lnum) {
   return linetabsize(po, lnum) + ((po->o.list && listCharsG.eol != ZERO) ? 1 : 0);
}

//Like linetabsize(), but excludes 'above'/'after'/'right'/'below' aligned
//virtual text, while keeping inline virtual text.
int
linetabsize_no_outer(Portal *po, LineNr lnum) {
   CharTableSize cts;
   CS line = memGetLine(po->book, lnum, FALSE);

   bookInitCharsForKeywordsSizeArg(&cts, po, lnum, 0, line, line);

   if (cts.cts_text_prop_count) {
      int write_idx = 0;
      for (int read_idx = 0; read_idx < cts.cts_text_prop_count; read_idx++) {
         TextProp *tp = &cts.cts_text_props[read_idx];
         if (tp->col != MAXCOL) {
            if (read_idx != write_idx)
               cts.cts_text_props[write_idx] = *tp;
            write_idx++;
         }
      }
      cts.cts_text_prop_count = write_idx;
      if (cts.cts_text_prop_count == 0)
         EE_CLEAR(cts.cts_text_props);
    }

    drawLineOnScreentabsize_cts(&cts, (ColNr)MAXCOL);
    clear_chartabsize_arg(&cts);
    return (int)cts.cts_vcol;
}

void
drawLineOnScreentabsize_cts(CharTableSize *cts, ColNr len) {
   Long vcol = cts->cts_vcol;
   cts->cts_with_trailing = len == MAXCOL;
   for ( ; *cts->cts_ptr != ZERO && (len == MAXCOL || cts->cts_ptr < cts->cts_line + len);
         MB_PTR_ADV(cts->cts_ptr)
   ){
      vcol += win_lbr_chartabsize(cts, NULL);
      if (vcol > MAXCOL) {
         cts->cts_vcol = MAXCOL;
         break;
      } else
         cts->cts_vcol = (int)vcol;
   }
   // check for a virtual text at the end of a line or on an empty line
   if (len == MAXCOL && cts->cts_has_prop_with_text && *cts->cts_ptr == ZERO) {
      (void)win_lbr_chartabsize(cts, NULL);
      vcol += cts->cts_cur_text_width;
      // when properties are above or below the empty line must also be counted
      if (cts->cts_ptr == cts->cts_line && cts->cts_prop_lines > 0)
          ++vcol;
      cts->cts_vcol = vcol > MAXCOL ? MAXCOL : (int)vcol;
   }
}

//Return TRUE if 'c' is a normal identifier character: Letters and chars from the 'isident' option
int
eeIsIdentifierChar(int c) {
   return (c > 0 && c < 0x100 && (g_chartab[c] & CT_ID_CHAR));
}

// Like eeIsIdentifierChar() but not using the 'isident' option: letters, numbers and underscore
int
eeIsNormalIdentifierChar(int c) {
   return ASCII_ISALNUM(c) || c == '_';
}

int
eeIsWordc_buf(Unt c, Book* book) {
   if (c >= 0x100) {
      return utf_class_buf(c, book) >= 2;
   }
   return (c < UNT_NEG && GET_CHARTAB(book, c) != 0);
}

//return TRUE if 'c' is a keyword character: Letters and characters from @iskeyword option for the 
//current book. For multi-byte characters mb_get_class() is used (builtin rules).
int
eeIsWordc(Unt c) {
   return eeIsWordc_buf(c, curBook);
}

int
eeIsWordPtr_buf(CS p, Book* book) {
   Unt c = *p;
   if (utf8CharLens[c] > 1)
      c = mb_ptr2char(p);
   return eeIsWordc_buf(c, book);
}

// Just like eeIsWordc() but uses a pointer to the (multi-byte) character.
int
eeIsWordPtr(CS p) {
   return eeIsWordPtr_buf(p, curBook);
}

// Return TRUE if 'c' is a valid file-name character as specified with the 'isfname' option.
// Assume characters above 0x100 are valid (multi-byte). To be used for commands like "gf".
int
eeIsFnameChar(Unt c) {
   return (c >= 0x100 || (c < UNT_NEG && (g_chartab[c] & CT_FNAME_CHAR)));
}

//Return TRUE if 'c' is a valid file-name character, including characters left
//out of 'isfname' to make "gf" work, such as comma, space, '@', etc.
Boole
eeIsFnameCharForGf(Unt c) {
   return eeIsFnameChar(c) || c == ',' || c == ' ' || c == '@';
}

//Return TRUE if 'c' is a printable character.
//Assume characters above 0x100 are printable (multi-byte), except for Unicode.
Boole
bookIsCharPrintable(Unt c) {
   if (c > 0xFF)
      return utf_printable(c);
   return (c > 0 && (g_chartab[c] & CT_PRINT_CHAR) != 0);
}

//Prepare the structure passed to chartabsize functions.
//"line" is the start of the line, "ptr" is the first relevant character.
//When "lnum" is zero do not use text properties that insert text.
void
bookInitCharsForKeywordsSizeArg(
   OUT CharTableSize* cts,
   Portal* po,
   LineNr lnum,
   ColNr col,
   CS line,
   CS ptr
) {
   CLEAR_POINTER(cts);
   cts->cts_win = po;
   cts->cts_vcol = col;
   cts->cts_line = line;
   cts->cts_ptr = ptr;
   cts->cts_bri_size = -1;
   if (lnum > 0 && !ignore_text_props) {
      CS propStart;
      int count = get_text_props(OUT &propStart, po->book, lnum, false);
      cts->cts_text_prop_count = count;
      if (count > 0) {
         // Make a copy of the properties, so that they are properly
         // aligned.  Make it twice as long for the sorting below.
         cts->cts_text_props = ALLOC_MULT(TextProp, count * 2);
         mch_memmove(cts->cts_text_props + count, propStart, count * sizeof(TextProp));
         for (int i = 0; i < count; ++i) {
            TextProp *tp = cts->cts_text_props + i + count;
            if (tp->id < 0 && text_prop_type_valid(po->book, tp)) {
               cts->cts_has_prop_with_text = TRUE;
               break;
            }
         }
         if (!cts->cts_has_prop_with_text) {
             // won't use the text properties, free them
             EE_CLEAR(cts->cts_text_props);
             cts->cts_text_prop_count = 0;
         } else {
            // Need to sort the array to get any truncation right. Do the sorting in the second
            // part of the array, then move the sorted props to the first part of the array.
            Arr(int) text_prop_idxs = ALLOC_MULT(int, count);
            for (int i = 0; i < count; ++i)
               text_prop_idxs[i] = i + count;
            sort_text_props(curBook, cts->cts_text_props, text_prop_idxs, count);
            // Here we want the reverse order.
            for (int i = 0; i < count; ++i)
                cts->cts_text_props[count - i - 1] = cts->cts_text_props[text_prop_idxs[i]];
            eeglFree(text_prop_idxs);
         }
      }
   }
}

// Free any allocated item in "cts".
void
clear_chartabsize_arg(OUT CharTableSize* cts) {
   if (cts->cts_text_prop_count > 0) {
      EE_CLEAR(cts->cts_text_props);
      cts->cts_text_prop_count = 0;
   }
}

// Like chartabsize(), but also check for line breaks on the screen and text properties that insert
int
lbr_chartabsize(CharTableSize* cts) {
   if (!p_sbr && !curPor->o.breakIndent && !cts->cts_has_prop_with_text) {
      if (curPor->o.wrap)
         return win_nolbr_chartabsize(cts, NULL);
      RET_PORT_BOOK_CHARSIZE(curPor, curBook, cts->cts_ptr, cts->cts_vcol)
   }
   return win_lbr_chartabsize(cts, NULL);
}

// Call lbr_chartabsize() and advance the pointer.
int
lbr_chartabsize_adv(CharTableSize *cts) {
   int retval = lbr_chartabsize(cts);
   MB_PTR_ADV(cts->cts_ptr);
   return retval;
}


//Return the screen size of the character indicated by "cts".
//"cts->cts_cur_text_width" is set to the extra size for a text property that inserts text.
//This function is used very often, keep it fast!!!!
//
//If "headp" not NULL, set "*headp" to the size of 'showbreak'/'breakindent'
//included in the return value.
//When "cts->cts_max_head_vcol" is positive, only count in "*headp" the size
//of 'showbreak'/'breakindent' before "cts->cts_max_head_vcol".
//When "cts->cts_max_head_vcol" is negative, only count in "*headp" the size
//of 'showbreak'/'breakindent' before where cursor should be placed.
//
//Warning: "*headp" may not be set if it's 0, init to 0 before calling.
int
win_lbr_chartabsize(CharTableSize* cts, int* headp){
   Portal* po = cts->cts_win;
   CS line = cts->cts_line; // start of the line
   CS s = cts->cts_ptr;
   ColNr vcol = cts->cts_vcol;
   int mb_added = 0;
   int n;
   Boole no_sbr = false;

   cts->cts_cur_text_width = 0;
   cts->cts_first_char = 0;

   // No @showbreak, @breakindent and text properties that insert text: finish quickly
   if (!po->o.breakIndent && !p_sbr && !cts->cts_has_prop_with_text) {
      if (po->o.wrap)
         return win_nolbr_chartabsize(cts, headp);
      RET_PORT_BOOK_CHARSIZE(po, po->book, s, vcol)
   }

   int has_lcs_eol = po->o.list && listCharsG.eol != ZERO;

   //First get the normal size, without text properties
   int size = win_chartabsize(po, s, vcol);
   if (*s == ZERO) {
      // 1 cell for EOL list char (if present), as opposed to the two cell ^@
      // for a ZERO character in the text.
      size = has_lcs_eol ? 1 : 0;
   }
   int is_doublewidth = size == 2 && utf8CharLens[*s] > 1;

   if (cts->cts_has_prop_with_text) {
      int tab_size = size;
      int charlen = *s == ZERO ? 1 : utfCharLen(s);
      int i;
      int col = (int)(s - line);
      ArrayList    *gap = &po->book->textPropText;

      // The "$" for 'list' mode will go between the EOL and the text prop, account for that.
      if (has_lcs_eol) {
         ++vcol;
         --size;
      }

      for (i = 0; i < cts->cts_text_prop_count; ++i) {
         TextProp* tp = cts->cts_text_props + i;
         int col_off = normalPortalColumnOffset(po);

         //Watch out for the text being deleted.  "cts_text_props" is a
         //copy, the text prop may actually have been removed from the line.
         if (tp->id < 0
               && ((tp->col - 1 >= col && tp->col - 1 < col + charlen)
                     || (tp->col == MAXCOL 
                           && ((tp->flags & TEXT_PROP_ALIGN_ABOVE) 
                              ? col == 0 
                              : (s[0] == ZERO && cts->cts_with_trailing))
                        )
                  )
               && -tp->id - 1 < gap->len
         ) {
            CS p = ((Byte **)gap->c)[-tp->id - 1];

            if (p) {
               int cells;

               if (tp->col == MAXCOL) {
                  int n_extra = (int)STRLEN(p);

                  cells = text_prop_position(
                     po, tp, vcol, (vcol + size) % (po->width - col_off) + col_off, &n_extra, &p, 
                     NULL, NULL, FALSE
                  );
                  no_sbr = true;  // don't use @showbreak now
               } else
                  cells = eeglStrSize(p);
               cts->cts_cur_text_width += cells;
               if (tp->flags & TEXT_PROP_ALIGN_ABOVE)
                  cts->cts_first_char += cells;
               else
                  size += cells;
               cts->cts_start_incl = tp->flags & TEXT_PROP_START_INCL;
               if (*s == TAB) {
                  // tab size changes because of the inserted text
                  size -= tab_size;
                  tab_size = win_chartabsize(po, s, vcol + size);
                  size += tab_size;
               }
               if (tp->col == MAXCOL 
                     && (tp->flags & (TEXT_PROP_ALIGN_ABOVE | TEXT_PROP_ALIGN_BELOW))
               )  // count extra line for property above/below
                  ++cts->cts_prop_lines;
            }
         }
         if (tp->col != MAXCOL && tp->col - 1 > col)
            break;
      }
      if (has_lcs_eol) {
         --vcol;
         ++size;
      }
   }

   if (is_doublewidth && po->o.wrap && inPortalBorder(po, vcol + size - 2)) {
      ++size;      // Count the ">" in the last column.
      mb_added = 1;
   }

   // May have to add something for 'breakindent' and/or 'showbreak'
   // string at the start of a screen line.
   int head = mb_added;
   CS sbr = no_sbr || !p_sbr ? Em : p_sbr;
   // When "size" is 0, no new screen line is started.
   if (size > 0 && po->o.wrap && (*sbr != ZERO || po->o.breakIndent)) {
      int col_off_prev = normalPortalColumnOffset(po);
      int width2 = po->width - col_off_prev;
      ColNr wcol = vcol + col_off_prev;
      wcol -= po->virtColFirstChar;
      ColNr max_head_vcol = cts->cts_max_head_vcol;
      int added = 0;

      // cells taken by 'showbreak'/'breakindent' before current char
      int   head_prev = 0;
      if (wcol >= (int)po->width) {
         wcol -= po->width;
         col_off_prev = po->width - width2;
         if (wcol >= width2 && width2 > 0)
            wcol %= width2;
         if (*sbr != ZERO)
            head_prev += eeglStrSize(sbr);
         if (po->o.breakIndent) {
            if (cts->cts_bri_size < 0)
               cts->cts_bri_size = getBreakindentForPort(po, line);
            head_prev += cts->cts_bri_size;
         }
         if (wcol < head_prev) {
            head_prev -= wcol;
            wcol += head_prev;
            added += head_prev;
            if (max_head_vcol <= 0 || vcol < max_head_vcol)
               head += head_prev;
         } else
            head_prev = 0;
         wcol += col_off_prev;
      }

      if (wcol + size > (int)po->width) {
         // cells taken by 'showbreak'/'breakindent' halfway current char
         int   head_mid = 0;
         if (*sbr != ZERO)
            head_mid += eeglStrSize(sbr);
         if (po->o.breakIndent) {
            if (cts->cts_bri_size < 0)
               cts->cts_bri_size = getBreakindentForPort(po, line);
            head_mid += cts->cts_bri_size;
         }
         if (head_mid > 0) {
            // Calculate effective portal width.
            int prev_rem = po->width - wcol;
            int width = width2 - head_mid;

            if (width <= 0)
               width = 1;
            // Divide "size - prev_rem" by "width", rounding up.
            int cnt = (size - prev_rem + width - 1) / width;
            added += cnt * head_mid;

            if (max_head_vcol == 0 || vcol + size + added < max_head_vcol)
                head += cnt * head_mid;
            ei (max_head_vcol > vcol + head_prev + prev_rem)
                head += (max_head_vcol - (vcol + head_prev + prev_rem)
                          + width2 - 1) / width2 * head_mid;
            ei (max_head_vcol < 0) {
               int off = mb_added;
               if (*s != ZERO && ((stateG & MODE_NORMAL) || cts->cts_start_incl))
                  off += cts->cts_cur_text_width;
               if (off >= prev_rem)
                  head += (1 + (off - prev_rem) / width) * head_mid;
            }
         }
      }

      size += added;
   }

   if (headp)
      *headp = head;

   Boole need_lbr = false;
   if (need_lbr) {
      // Count all characters from first non-blank after a blank up to next non-blank after a blank.
      int numberextra = normalPortalColumnOffset(po);
      ColNr col_adj = size - 1;
      ColNr colmax = (ColNr)(po->width - numberextra - col_adj);
      if (vcol >= colmax) {
         colmax += col_adj;
         n = colmax;
         if (n > 0)
            colmax += (((vcol - colmax) / n) + 1) * n - col_adj;
      }

      ColNr vcol2 = vcol;
      for (;;) {
         CS ps = s;
         MB_PTR_ADV(s);
         int c = *s;
         if (!(c != ZERO
                && (EE_ISBREAK(c)
                  || (!EE_ISBREAK(c) && (vcol2 == vcol || !EE_ISBREAK((int)*ps))))))
            break;

         vcol2 += win_chartabsize(po, s, vcol2);
         if (vcol2 >= colmax) {     // doesn't fit
            size = colmax - vcol + col_adj;
            break;
         }
      }
   }

   size += cts->cts_first_char;
   return size;
}

//Like win_lbr_chartabsize(), except that we know 'linebreak' is off, 'wrap'
//is on and there are no properties that insert text.  This means we need to
//check for a double-byte character that doesn't fit at the end of the screen line.
//Only uses "cts_win", "cts_ptr" and "cts_vcol" from "cts".
private int
win_nolbr_chartabsize(CharTableSize* cts, int* headp){
   Portal* po = cts->cts_win;
   CS s = cts->cts_ptr;
   ColNr col = cts->cts_vcol;
   int n;

   if (*s == TAB && (!po->o.list || listCharsG.tab1)) {
      n = po->book->o.shiftWidth;
      return (int)(n - (col % n));
   }
   n = ptr2cells(s);
   // Add one cell for a double-width character in the last column of the
   // portal, displayed with a ">".
   if (n == 2 && utf8CharLens[*s] > 1 && inPortalBorder(po, col)) {
      if (headp)
         *headp = 1;
      return 3;
   }
   return n;
}

// Return TRUE if virtual column "vcol" is in the rightmost column of portal "po".
private int                                                                    
inPortalBorder(Portal *po, ColNr vcol) {
   if (po->width == 0)   // there is no border
      return FALSE;
   int width1 = po->width - normalPortalColumnOffset(po); //width of first line (after line number)
   if ((int)vcol < width1 - 1)
      return FALSE;
   if ((int)vcol == width1 - 1)
      return TRUE;
   if (width1 <= 0)
      return FALSE;
   return ((vcol - width1) % width1 == width1 - 1);
}


//Get virtual column number of pos.
// start: on the first position of this character (TAB, ctrl)
//cursor: where the cursor is on this character (first char, except for TAB)
//   end: on the last position of this character (TAB, ctrl)
//
//This is used very often, keep it fast!
void
getvcol(
   Portal* po,
   Pos* pos,
   ColNr* start,
   ColNr* cursor,
   ColNr* end
) {
   int incr;
   int head;
   int ts = po->book->o.shiftWidth;
   CharTableSize cts;
   int      on_ZERO = FALSE;

   ColNr vcol = 0;
   CS ptr = memGetLine(po->book, pos->lnum, FALSE);  // points to current char
   CS line = ptr;  // start of the line

   bookInitCharsForKeywordsSizeArg(&cts, po, pos->lnum, 0, line, line);
   cts.cts_max_head_vcol = -1;

   //This function is used very often, do some speed optimizations.
   //When 'list', 'linebreak', 'showbreak' and 'breakindent' are not set
   //and there are no text properties with "text" use a simple loop.
   //Also use this when 'list' is set but tabs take their normal size.
   
   Unt c;
   if ((!po->o.list || listCharsG.tab1 != ZERO)
       && !p_sbr && !po->o.breakIndent
       && !cts.cts_has_prop_with_text
   ) {
      for (;;) {
         head = 0;
         c = *ptr;
         // make sure we don't go past the end of the line
         if (c == ZERO) {
            incr = 1;   // ZERO at end of line only takes one column
            break;
         }
         // A tab gets expanded, depending on the current column
         if (c == TAB)
            incr = ts - (vcol % ts);
         else {
            //If the byte is >= 0x80, need to look at further bytes to find the cell width.
            if (c >= 0x80)
               incr = mb_ptr2cells(ptr);
            else
               incr = g_chartab[c] & CT_CELL_MASK;

            //If a double-cell char doesn't fit at the end of a line
            //it wraps to the next line, it's like this char is three cells wide.
            if (incr == 2 && po->o.wrap && utf8CharLens[*ptr] > 1 && inPortalBorder(po, vcol)
            ) {
               ++incr;
               head = 1;
            }
         }

         CS next_ptr = ptr + utfCharLen(ptr);
         if (next_ptr - line > pos->col) // character at pos->col
            break;

          vcol += incr;
          ptr = next_ptr;
      }
   } else {
      for (;;) {
         //A tab gets expanded, depending on the current column. Other things also take up space.
         head = 0;
         incr = win_lbr_chartabsize(&cts, &head);
         // make sure we don't go past the end of the line
         if (*cts.cts_ptr == ZERO) {
            incr = 1;   // ZERO at end of line only takes one column
            if (cts.cts_cur_text_width > 0)
                incr = cts.cts_cur_text_width;
            on_ZERO = TRUE;
            break;
         }
         if (cursor == &po->virtCol && cts.cts_ptr == cts.cts_line)
            // do not count the virtual text above for cursWant
            po->virtColFirstChar = cts.cts_first_char;

         CS next_ptr = cts.cts_ptr + utfCharLen(cts.cts_ptr);
         if (next_ptr - line > pos->col) // character at pos->col
            break;

         cts.cts_vcol += incr;
         cts.cts_ptr = next_ptr;
      }
      vcol = cts.cts_vcol;
      ptr = cts.cts_ptr;
   }
   clear_chartabsize_arg(&cts);

   if (*ptr == ZERO && pos->col < MAXCOL && pos->col > ptr - line)
      pos->col = ptr - line;

   if (start)
      *start = vcol + head;
   if (end)
      *end = vcol + incr - 1;
   if (cursor) {
      if (*ptr == TAB
         && (stateG & MODE_NORMAL)
         && !po->o.list
         && !virtual_active()
         && !(VIsual_active
               && LTOREQ_POS(*pos, VIsual))
         )
          *cursor = vcol + incr - 1;       // cursor at end
      else {
         // in Insert mode, if "start_incl" is true the text gets inserted
         // after the virtual text, thus add its width
         if (((stateG & MODE_INSERT) == 0 || cts.cts_start_incl) && !on_ZERO)
            // cursor is after inserted text, unless on the ZERO
            vcol += cts.cts_cur_text_width;
         else
            // insertion also happens after the "above" virtual text
            vcol += cts.cts_first_char;
         *cursor = vcol + head;       // cursor at start
      }
   }
}

// Get virtual cursor column in the current portal, pretending 'list' is off.
ColNr
getvcol_nolist(Pos* posp) {
   int   list_save = curPor->o.list;
   ColNr vcol;

   curPor->o.list = FALSE;
   if (posp->coladd)
      bookGetVirtualColInVirtualMode(curPor, posp, NULL, &vcol, NULL);
   else
      getvcol(curPor, posp, NULL, &vcol, NULL);
   curPor->o.list = list_save;
   return vcol;
}

// Get virtual column in virtual mode.
void
bookGetVirtualColInVirtualMode(
   Portal* po,
   Pos* pos,
   OUT ColNr* start,
   OUT ColNr* cursor,
   OUT ColNr* end
) {
   ColNr   col;
   ColNr   coladd;
   ColNr   endadd;

   if (virtual_active()) {
      // For virtual mode, only want one value
      getvcol(po, pos, &col, NULL, NULL);

      coladd = pos->coladd;
      endadd = 0;
      // Cannot put the cursor on part of a wide character.
      CS ptr = memGetLine(po->book, pos->lnum, FALSE);
      if (pos->col < memGetBookLen(po->book, pos->lnum)) {
         Unt c = mb_ptr2char(ptr + pos->col);

         if (c != TAB && bookIsCharPrintable(c)) {
            endadd = (ColNr)(char2cells(c) - 1);
            if (coladd > endadd)   // past end of line
               endadd = 0;
            else
               coladd = 0;
         }
      }
      col += coladd;
      if (start)
         *start = col;
      if (cursor)
         *cursor = col;
      if (end)
          *end = col + endadd;
  } else
     getvcol(po, pos, start, cursor, end);
}

//Get the leftmost and rightmost virtual column of pos1 and pos2. Used for Visual block mode.
void
getvcols(
   Portal* po,
   Pos* pos1,
   Pos* pos2,
   ColNr* left,
   ColNr* right
) {
   ColNr   from1, from2, to1, to2;

   if (LT_POSP(pos1, pos2)) {
      bookGetVirtualColInVirtualMode(po, pos1, &from1, NULL, &to1);
      bookGetVirtualColInVirtualMode(po, pos2, &from2, NULL, &to2);
   } else {
      bookGetVirtualColInVirtualMode(po, pos2, &from1, NULL, &to1);
      bookGetVirtualColInVirtualMode(po, pos1, &from2, NULL, &to2);
   }
   if (from2 < from1)
      *left = from2;
   else
      *left = from1;
   if (to2 > to1) {
      *right = to2;
   } else
      *right = to1;
}


//}}}

// Determines how deeply nested %{} blocks will be evaluated in statusline.
# define MAX_STL_EVAL_DEPTH 100
// Values for flags argument of bookDo()
#define DOBOOK_FORCEIT   1   // :cmd!
#define DOBOOK_NOPOPUP   2   // skip popup portal books
#define DOBOOK_SKIPHELP  4   // skip or keep help books depending on kind of the starting book

//{{{forward declarations

private void   enterBook(Book* book);
private void   getLastKnownLineNumber(void);
private CS checkFilenameMatch(RegMatch *rmp, Book* book);
private CS fname_match(RegMatch *rmp, Byte *name);
private Book* booklistFindName_stat(CS fullFName, FileStat *st);
private int   areSameInode(Book* book, FileStat *stp);
private int   append_arg_number(Portal *po, CS buf, Unt buflen, Boole add_file);
private void   freeBook(Book *);
private void   freeAttachedData(Book* book, int free_options);
private int   bt_nofileread(Book* book);
private void   no_write_message_buf(Book* book);
private int   runBookCommand(int action, int start, int dir, int count, int flags);
private void clearPropTypes(Book* book);

//}}}

typedef dev_t Device;

#define FOR_ALL_BOOKS_FROM_LAST(book) \
    for ((book) = lastBook; (book); (book) = (book)->prev)

private CS msg_loclist = S"[Location List]";
private CS msg_qflist = S"[Quickfix List]";

// Number of times freeBook() was called.
private int freeCallCountS = 0;

private int   top_file_num = 1;   // highest file number
private ArrayList recycledFileNumberS = GA_EMPTY;   // file numbers to recycle

private void
trigger_undo_ftplugin(Book* book, Portal* port) {
   portalLayout_lock();
   book->locked++;
   port->locked = TRUE;
   // b:undo_ftplugin may be set, undo it
   executeCommLine(S"if exists('b:undo_ftplugin') | :legacy :exe \
       b:undo_ftplugin | endif");
   book->locked--;
   port->locked = FALSE;
   portalLayout_unlock();
}

// Calculate the percentage that `part` is of the `whole`.
private int
calc_percentage(long part, long whole) {
   // With 32 bit longs and more than 21,474,836 lines multiplying by 100
   // causes an overflow, thus for large numbers divide instead.
   return (part > 1000000L) ? (int)(part / (whole / 100L)) : (int)((part * 100L) / whole);
}

// The highest possible book number.
int get_highest_fnum(void) {
   return top_file_num - 1;
}

//Convert the specified character index of line 'lnum' in book to a byte index. Works only 
//for loaded books. Return -1 on failure. The index of the first byte and the first character is 0
int
bookCharidxToByteidx(Book* book, int lnum, int charidx) {
   if (!book || book->mem.mfile == NULL)
      return -1;

   if (lnum > book->mem.lineCount)
      lnum = book->mem.lineCount;

   CS str = memGetLine(book, lnum, FALSE);
   if (str == NULL)
      return -1;

   // Convert the character offset to a byte offset
   CS t = str;
   while (*t != ZERO && --charidx > 0)
      t += utfCharLen(t);

    return t - str;
}

// Read data from book for retrying.
private int
readBook(
   int read_stdin,       // read file from stdin, otherwise fifo
   Invocation* invo,          // for forced 'ff' or NULL
   Unt      flags          // extra flags for readfile()
){
   int retval = OK;
   LineNr   line_count;

   // Read from the book which the text is already filled in and append at the end. 
   line_count = curBook->mem.lineCount;
   retval = readfile(
       read_stdin ? NULL : curBook->fullFileName,
       read_stdin ? NULL : curBook->currFileName,
       line_count, (LineNr)0, (LineNr)MAXLNUM, invo,
       flags | READ_BOOK);
   if (retval == OK) {
      // Delete the binary lines.
      while (--line_count >= 0)
         ml_delete((LineNr)1);
   } else {
      // Delete the converted lines.
      while (curBook->mem.lineCount > line_count)
         ml_delete(line_count);
   }
   // Put the cursor on the first line.
   curPor->cursor.lnum = 1;
   curPor->cursor.col = 0;

   if (read_stdin) {
      //Set or reset 'modified' before executing autocommands, so that it can be changed there.
      if (!CURBOOK_EMPTY())
         changed();
      ei (retval == OK)
         unchanged(curBook, TRUE);
   }
   return retval;
}

// Ensure book is loaded. Do not trigger the swap-exists action.
void
bookEnsureLoaded(Book* book) {
   if (book->mem.mfile)
      return;

   // Make sure there is a portal into the book. Otherwise skip it.
   AutocommSave   aco;
   auCommPrepareBook(&aco, book);
   if (curBook == book) {
      if (swap_exists_action != SEA_READONLY)
          swap_exists_action = SEA_NONE; 
      bookOpenFromInvo(false, NULL, 0);
      auCommRestoreBook(&aco);
   }
}

//Open current book, that is: open the memfile and read the file into memory. Return OK/FAIL
Unt
bookOpenFromInvo(
   Boole read_stdin,     //read file from stdin
   Invocation* invo, //for forced 'ff' or NULL
   Unt flags        //extra flags for readfile()
){
   int      retval = OK;
   BookRef   oldCurBook;
   int      read_fifo = FALSE;

   if (optImmutableMode() && curBook->fullFileName)
      curBook->o.modifiable = false;

   if (ml_open(curBook) == FAIL) {
      // There MUST be a memfile, otherwise we can't do anything
      // If we can't create one for the current book, take another book
      closeBook(NULL, curBook, 0, FALSE, FALSE);
      FOR_ALL_BOOKS(curBook) {
         if (curBook->mem.mfile)
            break;
      } 
      // If there is no memfile at all, exit. This is OK, since there are no changes to lose.
      if (!curBook) {
         emsg(_(e_cannot_allocate_any_buffer_exiting));
         // Don't try to do any saving, with "curBook" NULL almost nothing will work.
         v_dying = 2;
         exitEegl(2);
      }

      emsg(_(e_cannot_allocate_book_using_other_one));
      enterBook(curBook);
      return FAIL;
   }

   // Do not sync this book yet, may first want to read the file.
   if (curBook->mem.mfile)
      curBook->mem.mfile->mf_dirty = MF_DIRTY_YES_NOSYNC;

   //The autocommands in readfile() may change the book, but only AFTER reading the file.
   bookStoreInRef(OUT &oldCurBook, curBook);
   curBook->modifiedWasSet = FALSE;

   // mark cursor position as being invalid
   curPor->cacheState = 0;

   // A book without an actual file should not use the book name to read a file.
   if (bt_nofileread(curBook))
      flags |= READ_NOFILE;

   // Read the file if there is one.
   if (curBook->fullFileName) {
      int old_msg_silent = msg_silent;
      Boole save_bin = curBook->o.binary;
      int perm;
      perm = mch_getperm(curBook->fullFileName);
      if (perm >= 0 && (S_ISFIFO(perm)
               || S_ISSOCK(perm)
# ifdef OPEN_CHR_FILES
               || (S_ISCHR(perm) && is_dev_fd_file(curBook->fullFileName))
# endif
             ))
         read_fifo = TRUE;
      if (read_fifo)
         curBook->o.binary = true;
      if (shortmess(SHM_FILEINFO))
         msg_silent = 1;
      retval = readfile(
         curBook->fullFileName, curBook->currFileName,
         (LineNr)0, (LineNr)0, (LineNr)MAXLNUM, invo,
         flags | READ_NEW | (read_fifo ? READ_FIFO : 0)
      );
      if (read_fifo) {
         curBook->o.binary = save_bin;
         if (retval == OK)
            // don't add READ_FIFO here, otherwise we won't be able to detect the encoding
            retval = readBook(FALSE, invo, flags);
      }
      msg_silent = old_msg_silent;
      // Help book is filtered.
      if (bookIsHelp(curBook))
         searchFixHelpBook();
   } ei (read_stdin) {
      Boole save_bin = curBook->o.binary;

      //First read the text in binary mode into the book.
      //Then read from that same book and append at the end.
      curBook->o.binary = true;
      retval = readfile(
         NULL, NULL, (LineNr)0, (LineNr)0, (LineNr)MAXLNUM, NULL, flags | (READ_NEW + READ_STDIN)
      );
      curBook->o.binary = save_bin;
      if (retval == OK)
         retval = readBook(TRUE, invo, flags);
   }

   // Can now sync this book in ml_sync_all().
   if (curBook->mem.mfile && curBook->mem.mfile->mf_dirty == MF_DIRTY_YES_NOSYNC) {
      curBook->mem.mfile->mf_dirty = MF_DIRTY_YES;
   }

   // Set/reset the Changed flag first, autocmds may change the book.
   // Apply the automatic commands.
   //
   // When reading stdin, the book contents always needs writing, so set
   // the changed flag.  Unless in readonly mode: "ls | gview -".
   if (curBook->modifiedWasSet) {   // autocmd did ":set modified"
      changed();
   } ei (retval == OK && !read_stdin && !read_fifo) {
      unchanged(curBook, TRUE);
   }

   // Set last_changedtick to avoid triggering a TextChanged autocommand right
   // after it was added.
   curBook->lastChangeTick = CHANGEDTICK(curBook);
   curBook->lastChangeTickInsert = CHANGEDTICK(curBook);
   curBook->lastChangeTickPum = CHANGEDTICK(curBook);

   // require "!" to overwrite the file, because it wasn't read completely
   if (aborting())
      curBook->flags |= BF_READERR;

   // Need to update automatic folding.  Do this before the autocommands,
   // they may use the fold info.
   foldUpdateAll(curPor);

   // need to set topLine, unless some autocommand already did that.
   if (!(curPor->cacheState & VALID_TOPLINE)) {
      curPor->topLine = 1;
      curPor->topFill = 0;
   }
   applyAutocommsRetval(EVENT_BUFENTER, NULL, NULL, FALSE, curBook, &retval);

   if (retval != OK)
      return retval;

   // The autocommands may have changed the current book.
   if (bookRefValid(&oldCurBook) && oldCurBook.c->mem.mfile) {

      //Go to the book that was opened, make sure there is a portal into it.
      //If not then skip it.
      AutocommSave   aco;
      auCommPrepareBook(OUT &aco, oldCurBook.c);
      if (curBook == oldCurBook.c) {
         curBook->flags &= ~(BF_CHECK_RO | BF_NEVERLOADED);

         if ((flags & READ_NOWINENTER) == 0)
            applyAutocommsRetval(EVENT_BUFWINENTER, NULL, NULL, FALSE, curBook, &retval);

         // restore curPor/curBook and a few other things
         auCommRestoreBook(&aco);
      }
   }

   return retval;
}

// Store "book" in "bookRef" and set the free count.
void
bookStoreInRef(OUT BookRef *bookRef, Book* book){
   bookRef->c = book;
   bookRef->fnum = book == NULL ? 0 : book->fiNum;
   bookRef->freeCount = freeCallCountS;
}

//Return TRUE if "bookRef->c" points to the same book as when bookStoreInRef() was called and it 
//is a valid book. Only goes through the book list if freeCallCountS changed.
//Also checks if fiNum is still the same, a :bwipe followed by :new might get
//the same allocated memory, but it's a different book.
Boole
bookRefValid(BookRef* bookRef){
   return bookRef->freeCount == freeCallCountS
      ? TRUE : (bookIsValid(bookRef->c) && bookRef->fnum == bookRef->c->fiNum);
}

//Return TRUE if "book" points to a valid book (in the book list).
//This can be slow if there are many books, prefer using bookRefValid().
Boole
bookIsValid(Book* book){
   // Assume that we more often have a recent book, start with the last one.
   Book* bp;
   FOR_ALL_BOOKS_FROM_LAST(bp) {
      if (bp == book)
         return true;
   } 
   return false;
}

// A hash table used to quickly lookup a book by its number.
private EeSet buf_hashtab;

private void
addBookToHashtable(Book* book){
   eeSnprintf(book->keyContainer, sizeof(book->keyContainer - 1), "%x", book->fiNum);
   if (book->key.len == 0) {
      book->key = text(book->keyContainer);
   }
   if (hash_add(&buf_hashtab, book->key, S"create book") == FAIL)
      emsg(_(e_book_cannot_be_registered));
}

private void
removeBookFromHashtable(Book* book) {
   EeSetItem* hi = hash_find(&buf_hashtab, book->key);

   if (!HASHITEM_EMPTY(hi))
      hash_remove(&buf_hashtab, hi, S"close book");
}

//Return TRUE when book "book" can be unloaded.
//Give an error message and return FALSE when the book is locked or the
//screen is being redrawn and the book is in a portal.
private Boole
canUnloadBook(Book* book) {
   Boole canUnload = !book->locked;

   if (canUnload && updating_screen) {
      Portal* po;
      FOR_ALL_PORTALS(po)
         if (po->book == book) {
            canUnload = false;
            break;
         }
   }
   if (!canUnload) {
      CS fname = book->currFileName ? book->currFileName : book->fullFileName;

      showErrFmtMsg(_(e_attempt_to_delete_buffer_that_is_in_use_str), fname ? fname : S"[No Name]");
   }
   return canUnload;
}

//Close the link to a book.
//"action" is used when there is no longer a portal into the book. It can be:
//0                   book becomes hidden
//DOBOOK_UNLOAD       book is unloaded
//DOBOOK_DEL          book is unloaded and removed from book list
//DOBOOK_WIPE         book is unloaded and really deleted
//DOBOOK_WIPE_REUSE   idem, and add to recycledFileNumberS list
//When doing all but the first one on the current book, the caller should
//get a new book very soon!
//
//When "abort_if_last" is TRUE then do not close the book if autocommands
//cause there to be only one portal into this book.  e.g. when ":quit" is
//supposed to close the portal but autocommands close all other portals.
//
//When "ignore_abort" is TRUE don't abort even when aborting() returns TRUE.
//
//Return TRUE when we got to the end and countPortals was decremented.
int
closeBook(
   Portal* port,      // if not NULL, set lastCursor
   Book* book,
   int action,
   int abort_if_last,
   int ignore_abort
){
   int nPortals;
   BookRef bookRef;
   Boole isCurPor = (curPor && curPor->book == book);
   Portal* theCurPor = curPor;
   Tab* theCurtab = curtab;
   int unload_buf = (action != 0);
   int wipe_buf = (action == DOBOOK_WIPE || action == DOBOOK_WIPE_REUSE);
   int del_buf = (action == DOBOOK_DEL || wipe_buf);

   CHECK_CURBOOK;

   //The caller must take care of NOT deleting/freeing (otherwise we could never free or delete 
   //a book).
   //depending on how we get here countPortals may already be zero
   if (bt_terminal(book) && (book->countPortals <= 1 || del_buf)) {
      CHECK_CURBOOK;
      if (term_job_running(book->term)) {
         if (wipe_buf || unload_buf) {
            if (!canUnloadBook(book))
               return FALSE;

            // Wiping out or unloading a terminal book kills the job.
            free_terminal(book);

            // A terminal book is wiped out when job has finished.
            del_buf = TRUE;
            unload_buf = TRUE;
            wipe_buf = TRUE;
         } else {
            // The job keeps running, hide the book.
            del_buf = FALSE;
            unload_buf = FALSE;
         }
      } ei (!del_buf) {
          // Hide a terminal book.
          unload_buf = FALSE;
      } else {
         if (del_buf || unload_buf) {
            // A terminal book is wiped out if the job has finished.
            // We only do this when there's an intention to unload the
            // book. This way, :hide and other similar commands won't
            // wipe the book.
            del_buf = TRUE;
            unload_buf = TRUE;
            wipe_buf = TRUE;
         }
      }
      CHECK_CURBOOK;
    }

   //Disallow deleting the book when it is locked (already being closed or
   //halfway a command that relies on it). Unloading is allowed.
   if ((del_buf || wipe_buf) && !canUnloadBook(book))
      return FALSE;

   //check no autocommands closed the portal
   if (port && doesPortalExistInAnyTab(port)) {
      //Set lastCursor when closing the last portal into the book.
      //Remember the last cursor position and portal options of the book.
      //This used to be only for the current portal, but then options like
      //@foldmethod may be lost with a ":only" command.
      if (book->countPortals == 1)
         set_last_cursor(port);
      bookSetPosInPort(book, port,
             port->cursor.lnum == 1 ? 0 : port->cursor.lnum,
             port->cursor.col, TRUE
      );
   }

   bookStoreInRef(OUT &bookRef, book);

   // When the book is no longer in a portal, trigger BufWinLeave
   if (book->countPortals == 1) {
      ++book->locked;
      ++book->lockedSplit;
      if (applyAutocomms(EVENT_BUFWINLEAVE, book->currFileName, book->currFileName, false, book)
         && !bookRefValid(&bookRef)
      ) {
         // Autocommands deleted the book.
   aucmd_abort:
         emsg(_(e_autocommands_caused_command_to_abort));
         return FALSE;
      }
      --book->locked;
      --book->lockedSplit;
      if (abort_if_last && onePortal())
          // Autocommands made this the only portal.
          goto aucmd_abort;

      // When the book becomes hidden, but is not unloaded, trigger BufHidden
      if (!unload_buf) {
         ++book->locked;
         ++book->lockedSplit;
         if (applyAutocomms(EVENT_BUFHIDDEN, book->currFileName, book->currFileName, false, book)
                && !bookRefValid(&bookRef))
            // Autocommands deleted the book.
            goto aucmd_abort;
         --book->locked;
         --book->lockedSplit;
         if (abort_if_last && onePortal())
            // Autocommands made this the only portal.
            goto aucmd_abort;
      }
      // autocmds may abort script processing
      if (!ignore_abort && aborting())
         return FALSE;
   }

   // If the book was in curPor and the portal has changed, go back to that
   // portal, if it still exists.  This avoids that ":edit x" triggering a
   // "tabnext" BufUnload autocmd leaves a portal behind without a book.
   if (isCurPor && curPor != theCurPor &&  doesPortalExistInAnyTab(theCurPor)) {
      block_autocmds();
      goto_tab_port(theCurtab, theCurPor);
      unblock_autocmds();
   }

   nPortals = book->countPortals;

   // decrease the link count from portals (unless not in any portal)
   if (book->countPortals > 0)
      --book->countPortals;

   if (diffopt_hiddenoff() && !unload_buf && book->countPortals == 0)
      diffDeleteBook(book);   // Clear 'diff' for hidden book.

   // Return when a portal is displaying the book or when it's not unloaded.
   if (book->countPortals > 0 || !unload_buf)
      return FALSE;

   // Always remove the book when there is no file name.
   if (!book->fullFileName)
      del_buf = TRUE;

   //When closing the current book stop Visual mode before freeing anything.
   if (book == curBook && VIsual_active
#if defined(EXITFREE)
       && !entered_free_all_mem
#endif
   )
      end_visual_mode();

   //Free all things allocated for this book.
   // Also calls the "BufDelete" autocommands when del_buf is TRUE.
   //
   // Remember if we are closing the current book.  Restore the number of
   // portals, so that autocommands in bookFreeAll() don't get confused.
   Boole isCurBook = (book == curBook);
   book->countPortals = nPortals;

   bookFreeAll(book, (del_buf ? BFA_DEL : 0)
         + (wipe_buf ? BFA_WIPE : 0)
         + (ignore_abort ? BFA_IGNORE_ABORT : 0));

   // Autocommands may have deleted the book.
   if (!bookRefValid(&bookRef))
      return FALSE;
   // autocmds may abort script processing
   if (!ignore_abort && aborting())
      return FALSE;

   //It's possible that autocommands change curBook to the one being deleted. This might cause the 
   //previous curBook to be deleted unexpectedly. But in some cases it's OK to delete the curBook, 
   //because a new one is obtained anyway. Therefore only return if curBook changed to the deleted 
   //book.
   if (book == curBook && !isCurBook)
      return false;

   if (doesPortalExistInAnyTab(port) && port->book == book)
      port->book = NULL;  // make sure we don't use the book now

   //Autocommands may have opened or closed portals into this book.
   //Decrement the count for the close we do here.
   if (book->countPortals > 0)
      --book->countPortals;

   //Remove the book from the list.
   if (wipe_buf) {
      Tab   *tp;
      Portal      *po;

      // Do not wipe out the book if it is open in a portal.
      if (book->countPortals > 0)
         return FALSE;

      FOR_ALL_TAB_PORTALS(tp, po)
         mark_forget_file(po, book->fiNum);

      if (action == DOBOOK_WIPE_REUSE) {
         // we can re-use this book number, store it
         if (recycledFileNumberS.ga_itemsize == 0)
            ga_init2(&recycledFileNumberS, sizeof(int), 50);
         if (ga_grow(&recycledFileNumberS, 1) == OK)
            ((int *)recycledFileNumberS.c)[recycledFileNumberS.len++] = book->fiNum;
      }
      if (book->shortFileName != book->fullFileName)
          EE_CLEAR(book->shortFileName);
      else
          book->shortFileName = NULL;
          
      EE_CLEAR(book->fullFileName);
      if (book->prev == NULL)
         firstBook = book->next;
      else
         book->prev->next = book->next;
      if (!book->next)
         lastBook = book->prev;
      else
         book->next->prev = book->prev;
      freeBook(book);
   } else {
      if (del_buf) {
         // Make it look like a new book.
         book->flags = BF_CHECK_RO | BF_NEVERLOADED;

         // Init the options when loaded again.
         book->o.initialized = false;
      }
      buf_clear_file(book);
      if (del_buf)
          book->o.bookListed = false;
   }
   //NOTE: at this point "curBook" may be invalid!
   return TRUE;
}

// Make book not contain a file.
void
buf_clear_file(Book* book){
   book->mem.lineCount = 1;
   unchanged(book, TRUE);
   book->startEof = FALSE;
   book->startEol = TRUE;
   book->mem.mfile = NULL;
   book->mem.flags = ML_EMPTY;      // empty book
}

//bookFreeAll() - free all things allocated for a book that are related to
//the file.  Careful: get here with "curPor" NULL when exiting.
//flags:
//BFA_DEL        book is going to be deleted
//BFA_WIPE        book is going to be wiped out
//BFA_KEEP_UNDO     do not free undo information
//BFA_IGNORE_ABORT  don't abort even when aborting() returns TRUE
void
bookFreeAll(Book* book, Unt flags){
   Boole isCurBook = (book == curBook);
   Boole isCurPor = (curPor && curPor->book == book);
   Portal* theCurPor = curPor;
   Tab* theCurtab = curtab;

   // Make sure the book isn't closed by autocommands.
   ++book->locked;
   ++book->lockedSplit;
   BookRef   bookRef;
   bookStoreInRef(OUT &bookRef, book);
   if (book->mem.mfile) {
      if (applyAutocomms(EVENT_BUFUNLOAD, book->currFileName, book->currFileName,
                             false, book) && !bookRefValid(&bookRef))
         // autocommands deleted the book
         return;
   }
   if ((flags & BFA_DEL) && book->o.bookListed) {
      if (applyAutocomms(EVENT_BUFDELETE, book->currFileName, book->currFileName, false, book)
            && !bookRefValid(&bookRef))
        // autocommands deleted the book
        return;
    }
   if (flags & BFA_WIPE) {
      if (applyAutocomms(EVENT_BUFWIPEOUT, book->currFileName, book->currFileName, false, book)
            && !bookRefValid(&bookRef))
          // autocommands deleted the book
          return;
   }
   --book->locked;
   --book->lockedSplit;

   // If the book was in curPor and the portal has changed, go back to that
   // portal, if it still exists.  This avoids that ":edit x" triggering a
   // "tabnext" BufUnload autocmd leaves a portal behind without a book.
   if (isCurPor && curPor != theCurPor && doesPortalExistInAnyTab(theCurPor)) {
      block_autocmds();
      goto_tab_port(theCurtab, theCurPor);
      unblock_autocmds();
   }

   // autocmds may abort script processing
   if ((flags & BFA_IGNORE_ABORT) == 0 && aborting())
      return;

   // It's possible that autocommands change curBook to the one being deleted.
   // This might cause curBook to be deleted unexpectedly.  But in some cases
   // it's OK to delete the curBook, because a new one is obtained anyway.
   // Therefore only return if curBook changed to the deleted book.
   if (book == curBook && !isCurBook)
      return;
   diffDeleteBook(book);       // Can't use 'diff' for unloaded book.
   // Remove any ownsyntax, unless exiting.
   if (curPor && curPor->book == book)
      reset_synblock(curPor);

   // No folds in an empty book.
   Portal* port;
   Tab* tp;
   FOR_ALL_TAB_PORTALS(tp, port) {
      if (port->book == book)
         clearFolding(port);
   } 

   ml_close(book, TRUE);       // close and delete the memline/memfile
   book->mem.lineCount = 0;    // no lines in book
   if ((flags & BFA_KEEP_UNDO) == 0)
      // free the memory allocated for undo and reset all undo information
      invalidateUndoBufferAndFreeBlocks(book);
   syntax_clear(&book->syntax);       // reset syntax info
   clearPropTypes(book);
   book->flags &= ~BF_READERR;    // a read error is no longer relevant
}

//Free a book structure and the things it contains related to the book
//itself (not the file, that must have been done already).
private void
freeBook(Book* book){
   ++freeCallCountS;
   freeAttachedData(book, TRUE);
   // b:changedtick uses an item in Book, remove it now
   dictitem_remove(book->bVars, (DictItem *)&book->changedTick, S"free book");
   unref_var_dict(book->bVars);
   remove_listeners(book);
   chaFreeBook(book);
   free_terminal(book);
   eeglFree(book->promptText);
   evFreeCallback(&book->promptCallback);
   evFreeCallback(&book->promptInterrupt);

   removeBookFromHashtable(book);

   scrRemoveAutocommsFromBook(book);

   if (autocmd_busy) {
      // Do not free the book structure while autocommands are executing,
      // it's still needed. Free it when autocmd_busy is reset.
      book->next = auPendingFreeBooksG;
      auPendingFreeBooksG = book;
   } else {
      eeglFree(book);
      if (curBook == book)
         curBook = NULL;  // make clear it's not to be used
   }
}

// Initializes b:changedtick.
private void
init_changedtick(Book* book){
   DictItem *di = (DictItem *)&book->changedTick;
   di->len = STRLEN(di->key);
   di->flags = DI_FLAGS_FIX | DI_FLAGS_RO;
   di->c.tag = VAR_NUMBER;
   di->c.lock = VAR_FIXED;
   di->c.number = 0;

   STRCPY(book->changedTick.key, "changedtick");
   book->changedTick.len = 11;
   (void)bagAdd(book->bVars, di);
}

// Free the portInfos list for book
private void
clearPortInfo(Book* book){
   while (book->portInfos) {
      PortInfo* poInfo = book->portInfos;
      book->portInfos = poInfo->next;
      free_wininfo(poInfo);
   }
}

// Free stuff in the book for ":bdel" or when wiping out the book.
private void
freeAttachedData(Book* book, int free_options) {     // free options as well
   if (free_options) {
      clearPortInfo(book);      // including portal-local options
      optFreeBookCallbacks(book);
      ga_clear(&book->syntax.b_langp);
   }
   {
      Long tick = CHANGEDTICK(book);

      vars_clear(&book->bVars->hashTable); // free all book variables
      hash_init(&book->bVars->hashTable);
      init_changedtick(book);
      CHANGEDTICK(book) = tick;
      remove_listeners(book);
   }
   uc_clear(&book->userCommands);      // clear local user commands
   markDeleteSigns(book, S"*");   // delete any signs
   ga_clear_strings(&book->textPropText);
   mapClearAllMappingsInMode(book, MAP_ALL_MODES, TRUE, FALSE);  // clear local mappings
   mapClearAllMappingsInMode(book, MAP_ALL_MODES, TRUE, TRUE);   // clear local abbrevs
}

// Free one PortInfo.
void
free_wininfo(PortInfo *poInfo) {
   if (poInfo->isOptChanged) {
      optClearPortOptions(&poInfo->opt);
      deleteFoldRecurse(&poInfo->folds);
   }
   eeglFree(poInfo);
}

// Go to another book. Handles the result of the ATTENTION dialog.
void
bookGoto(Invocation* invo, int start, int dir, int count){
   BookRef   oldCurBook;
   int      save_sea = swap_exists_action;
   int      skipHelpAndQuickfix;

   switch (invo->id) {
   case C_bnext:
   case C_sbnext:
   case C_bNext:
   case C_bprevious:
   case C_sbNext:
   case C_sbprevious:
      skipHelpAndQuickfix = TRUE;
      break;
   default:
      skipHelpAndQuickfix = FALSE;
      break;
   }

   bookStoreInRef(OUT &oldCurBook, curBook);

   if (swap_exists_action == SEA_NONE)
      swap_exists_action = SEA_DIALOG;
   (void)runBookCommand(
      *invo->comm == 's' ? DOBOOK_SPLIT : DOBOOK_GOTO, start, dir, count,
      (invo->forceit ? DOBOOK_FORCEIT : 0) | (skipHelpAndQuickfix ? DOBOOK_SKIPHELP : 0)
   );
   if (swap_exists_action == SEA_QUIT && *invo->comm == 's') {
      Cleanup   cs;

      // Reset the error/interrupt/exception state here so that
      // aborting() returns FALSE when closing a portal.
      enter_cleanup(&cs);

      // Quitting means closing the split portal, nothing else.
      closePortal(curPor, TRUE);
      swap_exists_action = save_sea;
      swap_exists_did_quit = TRUE;

      // Restore the error/interrupt/exception state if not discarded by a
      // new aborting error, interrupt, or uncaught exception.
      leave_cleanup(&cs);
   } else
      handle_swap_exists(&oldCurBook);
}

//Handle the situation of swap_exists_action being set.
//It is allowed for "oldCurBook" to be NULL or invalid.
void
handle_swap_exists(BookRef *oldCurBook) {
   Cleanup   cs;
   Book   *book;

   if (swap_exists_action == SEA_QUIT) {
      // Reset the error/interrupt/exception state here so that
      // aborting() returns FALSE when closing a book.
      enter_cleanup(&cs);

      // User selected Quit at ATTENTION prompt.  Go back to previous
      // book. If that book is gone or the same as the current one, open a new, empty book.
      swap_exists_action = SEA_NONE;   // don't want it again
      swap_exists_did_quit = TRUE;
      closeBook(curPor, curBook, DOBOOK_UNLOAD, FALSE, FALSE);
      if (oldCurBook == NULL || !bookRefValid(oldCurBook)
                        || oldCurBook->c == curBook) {
          // Block autocommands here because curPor->book is NULL.
          block_autocmds();
          book = bookNew(NULL, NULL, 1L, BLN_CURBOOK | BLN_LISTED);
          unblock_autocmds();
      } else
          book = oldCurBook->c;
      if (book) {
         int old_msg_silent = msg_silent;

         if (shortmess(SHM_FILEINFO))
            msg_silent = 1;  // prevent fileinfo message
         enterBook(book);
         // restore msg_silent, so that the command line will be shown
         msg_silent = old_msg_silent;
      }
      // If "oldCurBook" is NULL we are in big trouble here...

      // Restore the error/interrupt/exception state if not discarded by a
      // new aborting error, interrupt, or uncaught exception.
      leave_cleanup(&cs);
   } ei (swap_exists_action == SEA_RECOVER) {
      // Reset the error/interrupt/exception state here so that
      // aborting() returns FALSE when closing a book.
      enter_cleanup(&cs);

      // User selected Recover at ATTENTION prompt.
      msg_scroll = TRUE;
      ml_recover(FALSE);
      msg_puts(S"\n");   // don't overwrite the last message
      commlineRowG = msgRowG;

      // Restore the error/interrupt/exception state if not discarded by a
      // new aborting error, interrupt, or uncaught exception.
      leave_cleanup(&cs);
    }
swap_exists_action = SEA_NONE;
}

// Make the current book empty. Used when it is wiped out and it's the last book.
private int
emptyCurBook(
   int close_others,
   int forceit,
   int action) {
   int       retval;
   Book   *book = curBook;
   BookRef bookRef;

   if (action == DOBOOK_UNLOAD) {
      emsg(_(e_cannot_unload_last_buffer));
      return FAIL;
   }

   bookStoreInRef(OUT &bookRef, book);
   if (close_others)
      // Close any other portals into this book, then make it empty.
      closePortalsInto(book, TRUE);

   setpcmark();
   retval = startEditingFile(0, NULL, NULL, NULL, ECMD_ONE, forceit ? ECMD_FORCEIT : 0, curPor);

   // startEditingFile() may create a new book, then we have to delete the old one. But 
   // startEditingFile() may have done that already, check if the book still exists.
   if (book != curBook && bookRefValid(&bookRef) && book->countPortals == 0)
      closeBook(NULL, book, action, FALSE, FALSE);
   if (!close_others)
      need_fileinfo = FALSE;
   return retval;
}

//Implementation of the commands for the book list.
//
//action == DOBOOK_GOTO       go to specified book
//action == DOBOOK_SPLIT    split portal and go to specified book
//action == DOBOOK_UNLOAD   unload specified book(s)
//action == DOBOOK_DEL       delete specified book(s) from book list
//action == DOBOOK_WIPE       delete specified book(s) really
//action == DOBOOK_WIPE_REUSE idem, and add number to "recycledFileNumberS"
//
//start == DOBOOK_CURRENT   go to "count" book from current book
//start == DOBOOK_FIRST       go to "count" book from first book
//start == DOBOOK_LAST       go to "count" book from last book
//start == DOBOOK_MOD       go to "count" modified book from current book
//
//Return FAIL or OK.
private int
runBookCommand(
   int      action,
   int      start,
   int      dir,      // FORWARD or BACKWARD
   int      count,      // book number or number of books
   int      flags      // DOBOOK_FORCEIT etc.
){
   Book   *book;
   Book   *bp;
   int unload = (action == DOBOOK_UNLOAD || action == DOBOOK_DEL
            || action == DOBOOK_WIPE || action == DOBOOK_WIPE_REUSE);
   switch (start) {
   case DOBOOK_FIRST:   book = firstBook; break;
   case DOBOOK_LAST:    book = lastBook;  break;
   default:       book = curBook;   break;
   }
   
   if (start == DOBOOK_MOD) {      // find next modified book
      while (count-- > 0) {
         do {
            book = book->next;
            if (book == NULL)
               book = firstBook;
         }
         while (book != curBook && !doWasBookChanged(book));
      }
      if (!doWasBookChanged(book)) {
          emsg(_(e_no_modified_buffer_found));
          return FAIL;
      }
   } ei (start == DOBOOK_FIRST && count) { // find specified book number
      while (book && book->fiNum != count)
          book = book->next;
   } else {
      int helpOnly = (flags & DOBOOK_SKIPHELP) != 0 && book->kind == BOOK_HELP;

      bp = NULL;
      while (count > 0 
            || (bp != book && !unload && !(helpOnly ? book->kind == BOOK_HELP : book->o.bookListed))
      ) {
         // remember the book where we start, we return there when all books are unlisted
         if (bp == NULL)
            bp = book;
         if (dir == FORWARD) {
            book = book->next;
            if (book == NULL)
               book = firstBook;
         } else {
            book = book->prev;
            if (book == NULL)
               book = lastBook;
         }
         // Avoid non-help books if the starting point was a help book and vice-versa.
         // Don't count unlisted books.
         if (unload
                || (helpOnly
                  ? book->kind == BOOK_HELP
                  : (book->o.bookListed 
                     && ((flags & DOBOOK_SKIPHELP) == 0 
                        || (book->kind != BOOK_HELP && book->kind != BOOK_LOCATION) 
                        )
                  )
               )
         ) {
            --count;
            bp = NULL; // use this book as new starting point
         }
         if (bp == book) {
            // back where we started, didn't find anything.
            emsg(_(e_there_is_no_listed_buffer));
            return FAIL;
         }
      }
   }

   if (book == NULL) {    // could not find it
      if (start == DOBOOK_FIRST) {
         // don't warn when deleting
         if (!unload)
            showErrFmtMsg(_(e_book_nr_does_not_exist), count);
      } ei (dir == FORWARD)
         emsg(_(e_cannot_go_beyond_last_buffer));
      else
         emsg(_(e_cannot_go_before_first_buffer));
      return FAIL;
   }
   if ((flags & DOBOOK_NOPOPUP) && bt_popup(book) && !bt_terminal(book))
      return OK;
   if (action == DOBOOK_GOTO && book != curBook) {
      if (!portCheckCanSetCurBookForceIt((flags & DOBOOK_FORCEIT) != 0))
         // disallow navigating to another book when 'portfixbuf' is applied
         return FAIL;
      if (book->lockedSplit) {
         // disallow navigating to a closing book, which like splitting,
         // can result in more portals displaying it
         emsg(_(e_cannot_switch_to_a_closing_buffer));
         return FAIL;
      }
   }

   if ((action == DOBOOK_GOTO || action == DOBOOK_SPLIT) && (book->flags & BF_DUMMY)) {
      // disallow navigating to the dummy book
      showErrFmtMsg(_(e_book_nr_does_not_exist), count);
      return FAIL;
   }

   // delete "book" from memory and/or the list
   if (unload) {
      int   forward;
      BookRef bookRef;

      if (!canUnloadBook(book))
         return FAIL;

      bookStoreInRef(OUT &bookRef, book);

      // When unloading or deleting a book that's already unloaded and
      // unlisted: fail silently.
      if (action != DOBOOK_WIPE && action != DOBOOK_WIPE_REUSE
                  && book->mem.mfile == NULL && !book->o.bookListed)
          return FAIL;

      if ((flags & DOBOOK_FORCEIT) == 0 && doWasBookChanged(book)) {
         if (p_confirm || (commModifierG.cmod_flags & CMOD_CONFIRM)) {
            if (term_job_running(book->term)) {
               if (term_confirm_stop(book) == FAIL)
                  return FAIL;
            } else {
               dialog_changed(book, FALSE);
               if (!bookRefValid(&bookRef))
                  // Autocommand deleted book, oops!  It's not changed now.
                  return FAIL;
               // If it's still changed fail silently, the dialog already mentioned why it fails.
               if (doWasBookChanged(book))
                  return FAIL;
            }
         } else {
            no_write_message_buf(book);
            return FAIL;
         }
      }

      // When closing the current book stop Visual mode.
      if (book == curBook && VIsual_active)
         end_visual_mode();

      // If deleting the last (listed) book, make it empty.
      // The last (listed) book cannot be unloaded.
      FOR_ALL_BOOKS(bp) {
         if (bp->o.bookListed && bp != book)
            break;
      } 
      if (bp == NULL && book == curBook)
         return emptyCurBook(TRUE, (flags & DOBOOK_FORCEIT), action);

      // If the deleted book is the current one, close the current portal (unless it's the only 
      // portal). Repeat this so long as we end up in a portal with this book.
      while (book == curBook
            && !(portalLocked(curPor) || curPor->book->locked > 0)
            && (!ONLY_ONE_PORTAL || firstTabG->next)) {
         if (closePortal(curPor, FALSE) == FAIL)
            break;
      }

      // If the book to be deleted is not the current one, delete it here.
      if (book != curBook) {
          closePortalsInto(book, FALSE);
          if (book != curBook && bookRefValid(&bookRef) && book->countPortals <= 0)
             closeBook(NULL, book, action, FALSE, FALSE);
          return OK;
      }

      //Deleting the current book: Need to find another book to go to. There should be another, 
      //otherwise it would have been handled above. However, autocommands may have deleted all 
      //books. First use auNewCurBook.c, if it is valid. Then prefer the book we most recently 
      //visited. Else try to find one that is loaded, after the current book, then before the 
      //current book. Finally use any book.
      book = NULL;  // selected book
      bp = NULL;   // used when no loaded book found
      if (auNewCurBookG.c && bookRefValid(&auNewCurBookG))
          book = auNewCurBookG.c;
      ei (curPor->jumpListLen > 0) {
          int     jumpidx;

         jumpidx = curPor->jumpListInd - 1;
         if (jumpidx < 0)
            jumpidx = curPor->jumpListLen - 1;

         forward = jumpidx;
         while (jumpidx != curPor->jumpListInd) {
            book = bookFindFileByBookNr(curPor->jumpList[jumpidx].fmark.fnum);
            if (book) {
               //Skip current and unlisted bufs. Also skip a location book, it might be deleted soon
               if (book == curBook || !book->o.bookListed || isLocationListBook(book))
                  book = NULL;
               ei (book->mem.mfile == NULL) {
                  // skip unloaded book, but may keep it for later
                  if (bp == NULL)
                      bp = book;
                  book = NULL;
               }
            }
            if (book)   // found a valid book: stop searching
               break;
            // advance to older entry in jump list
            if (!jumpidx && curPor->jumpListInd == curPor->jumpListLen)
               break;
            if (--jumpidx < 0)
               jumpidx = curPor->jumpListLen - 1;
            if (jumpidx == forward)      // List exhausted for sure
               break;
         }
      }

      if (book == NULL) { // No previous book, Try 2'nd approach
         forward = TRUE;
         book = curBook->next;
         for (;;) {
            if (book == NULL) {
               if (!forward)   // tried both directions
                  break;
               book = curBook->prev;
               forward = FALSE;
               continue;
            }
            // in non-help book, try to skip help books, and vv
            if ((book->kind == BOOK_HELP) == (curBook->kind == BOOK_HELP) && book->o.bookListed
                   && !isLocationListBook(book)
            ){
               if (book->mem.mfile)   // found loaded book
                  break;
               if (!bp)   // remember unloaded book for later
                  bp = book;
            }
            if (forward)
               book = book->next;
            else
               book = book->prev;
         }
      }
      if (!book)   // No loaded book, use unloaded one
          book = bp;
      if (!book) {  // No loaded book, find listed one
         FOR_ALL_BOOKS(book) {
            if (book->o.bookListed && book != curBook && !isLocationListBook(book))
               break;
         } 
      }
      if (!book) {  // Still no book, just take one
         if (curBook->next)
            book = curBook->next;
         else
            book = curBook->prev;
         if (isLocationListBook(book))
            book = NULL;
      }
   }

   if (!book) {
      // Autocommands must have wiped out all other books. Only option
      // now is to make the current book empty.
      return emptyCurBook(FALSE, (flags & DOBOOK_FORCEIT), action);
   }

   // make "book" the current book
   if (action == DOBOOK_SPLIT) {     // split portal first
      // If 'switchbook' is set jump to the portal containing "book".
      if (switchBufGotoPortalIntoBuf(book) != NULL)
         return OK;

      if (splitPortal(0, 0) == FAIL)
         return FAIL;
   }

   // go to current book - nothing to do
   if (book == curBook)
      return OK;

   // Go to the other book.
   bookSetCurBook(book, action);

   if (action == DOBOOK_SPLIT)
      RESET_BINDING(curPor);   // reset 'scrollbind' and 'cursorbind'

   if (aborting())       // autocmds may abort script processing
      return FAIL;

   return OK;
}

int
bookDo(
   int      action,
   int      start,
   int      dir,      // FORWARD or BACKWARD
   int      count,      // book number
   int      forceit)   // TRUE when using !
{
   return runBookCommand(action, start, dir, count, forceit ? DOBOOK_FORCEIT : 0);
}

//do_bufdel() - delete or unload book(s)
//
//addr_count == 0: ":bdel" - delete current book
//addr_count == 1: ":N bdel" or ":bdel N [N ..]" - first delete
//         book "end_bnr", then any other arguments.
//addr_count == 2: ":N,N bdel" - delete books in range
//
//command can be DOBOOK_UNLOAD (":bunload"), DOBOOK_WIPE (":bwipeout") or DOBOOK_DEL (":bdel")
//
//Return error message or NULL
CS
do_bufdel(
   int command,
   CS arg,      // pointer to extra arguments
   int addr_count,
   int start_bnr,   // first book number in a range
   int end_bnr,   // book nr or last book nr in a range
   int forceit
) {
   int      do_current = 0;   // delete current book?
   int      deleted = 0;   // number of books deleted
   CS errormsg = NULL; // return value
   int      bnr;      // book number
   CS p;

   if (addr_count == 0) {
      (void)bookDo(command, DOBOOK_CURRENT, FORWARD, 0, forceit);
   } else {
      if (addr_count == 2) {
         if (*arg)      // both range and argument is not allowed
            return ex_errmsg(e_trailing_characters_str, arg);
         bnr = start_bnr;
      } else   // addr_count == 1
          bnr = end_bnr;

      for ( ;!gotInterruptG; ui_breakcheck()) {
         // Delete the current book last, otherwise when the
         // current book is deleted, the next book becomes
         // the current one and will be loaded, which may then
         // also be deleted, etc.
         if (bnr == curBook->fiNum)
            do_current = bnr;
         ei (runBookCommand(command, DOBOOK_FIRST, FORWARD, bnr,
            DOBOOK_NOPOPUP | (forceit ? DOBOOK_FORCEIT : 0)) == OK)
         ++deleted;

         // find next book number to delete/unload
         if (addr_count == 2) {
            if (++bnr > end_bnr)
               break;
         } else  {   // addr_count == 1
            arg = skipwhite(arg);
            if (*arg == ZERO)
               break;
            if (!EE_ISDIGIT(*arg)) {
               p = skiptowhite_esc(arg);
               bnr = booklistFindPattern(
                      arg, p, command == DOBOOK_WIPE || command == DOBOOK_WIPE_REUSE, FALSE, FALSE
               );
               if (bnr < 0)       // failed
                  break;
               arg = p;
            } else
               bnr = parseLong(&arg);
         }
      }
      if (!gotInterruptG && do_current && bookDo(command, DOBOOK_FIRST,
                    FORWARD, do_current, forceit) == OK)
          ++deleted;

      if (deleted == 0) {
         if (command == DOBOOK_UNLOAD)
            STRCPY(IObuff, _(e_no_buffers_were_unloaded));
         ei (command == DOBOOK_DEL)
            STRCPY(IObuff, _(e_no_buffers_were_deleted));
         else
            STRCPY(IObuff, _(e_no_buffers_were_wiped_out));
         errormsg = IObuff;
      } else {
         if (command == DOBOOK_UNLOAD)
            smsg(NGETTEXT("%d book unloaded", "%d books unloaded", deleted), deleted);
         ei (command == DOBOOK_DEL)
            smsg(NGETTEXT("%d book deleted", "%d books deleted", deleted), deleted);
         else
            smsg(NGETTEXT("%d book wiped out", "%d books wiped out", deleted), deleted);
      }
   }

   return errormsg;
}

//Set current book to "book". Execute autocommands and close current book.  
//"action" tells how to close the current book:
//DOBOOK_GOTO       free or hide it
//DOBOOK_SPLIT      nothing
//DOBOOK_UNLOAD     unload it
//DOBOOK_DEL        delete it
//DOBOOK_WIPE       wipe it out
//DOBOOK_WIPE_REUSE wipe it out and add to "recycledFileNumberS"
void
bookSetCurBook(Book* book, int action) {
   int unload = (action == DOBOOK_UNLOAD || action == DOBOOK_DEL
         || action == DOBOOK_WIPE || action == DOBOOK_WIPE_REUSE);
   BookRef   newbufref;
   BookRef   prevbufref;
   int      valid;

   setpcmark();
   if ((commModifierG.cmod_flags & CMOD_KEEPALT) == 0)
      curPor->altFnum = curBook->fiNum; // remember alternate file
      
   bookSetPosInPort(curBook, curPor, curPor->cursor.lnum, curPor->cursor.col, TRUE);

   // Don't restart Select mode after switching to another book.
   VIsual_reselect = FALSE;

   // closePortalsInto() or applyAutocomms() may change curBook and wipe out "book"
   Book* prevbuf = curBook;
   bookStoreInRef(OUT &prevbufref, prevbuf);
   bookStoreInRef(OUT &newbufref, book);

   // Autocommands may delete the current book and/or the book we want to
   // go to.  In those cases don't close the book.
   if (!applyAutocomms(EVENT_BUFLEAVE, NULL, NULL, false, curBook)
       || (bookRefValid(&prevbufref)
            && bookRefValid(&newbufref)
            && !aborting()
          )
   ) {
      if (prevbuf == curPor->book)
         reset_synblock(curPor);
      // autocommands may have opened a new portal with prevbuf, grr
      if (unload)
         closePortalsInto(prevbuf, FALSE);
      if (bookRefValid(&prevbufref) && !aborting()) {
         Portal  *previouswin = curPor;

         // Do not sync when in Insert mode and there is another portal into the book, might 
         // be a timer doing something in another portal.
         if (prevbuf == curBook
                && ((stateG & MODE_INSERT) == 0 || curBook->countPortals <= 1)
         )
            u_sync(FALSE);
         closeBook(
            prevbuf == curPor->book ? curPor : NULL, prevbuf,
            unload ? action : 0,
            FALSE, FALSE
         );
         if (curPor != previouswin && portalIsValid(previouswin))
            // autocommands changed curPor, Grr!
            curPor = previouswin;
      }
   }
   // An autocommand may have deleted "book", already entered it (e.g., when
   // it did ":bunload") or aborted the script processing.
   // If curPor->book is null, enterBook() will make it valid again
   valid = bookIsValid(book);
   if ((valid && book != curBook && !aborting()) || curPor->book == NULL) {
      // autocommands changed curBook and we will move to another
      // book soon, so decrement curBook->countPortals
      if (curBook && prevbuf != curBook)
         curBook->countPortals--;
      // If the book is not valid but curPor->book is NULL we must
      // enter some book.  Using the last one is hopefully OK.
      if (!valid) {
         enterBook(lastBook);
      } else {
         enterBook(book);
      } 
   }
}

//Enter a new current book. Old curBook must have been abandoned already! This also means 
//"curBook" may be pointing to freed memory.
private void
enterBook(Book* book){
   //when closing the current book, stop Visual mode
   if (VIsual_active
#if defined(EXITFREE)
       && !entered_free_all_mem
#endif
   )
      end_visual_mode();

   //Get the book in the current portal.
   curPor->book = book;
   curBook = book;
   ++curBook->countPortals;

   //Copy book and portal local option values. Not for a help book.
   optsCopyToBook(book, BCO_ENTER);
   if (book->kind != BOOK_HELP)
      get_winopts(book);
   else
      // Remove all folds in the portal.
      clearFolding(curPor);
   foldUpdateAll(curPor);   // update folds (later).

   if (curPor->o.diff)
      diffAddBook(curBook);

   curPor->ownSyntax = &(curBook->syntax);

   // Cursor on first line by default.
   curPor->cursor.lnum = 1;
   curPor->cursor.col = 0;
   curPor->cursor.coladd = 0;
   curPor->setCursWant = TRUE;
   curPor->wasTopLineSet = FALSE;

   // mark cursor position as being invalid
   curPor->cacheState = 0;

   // Make sure the book is loaded.
   if (curBook->mem.mfile == NULL) {  // need to load the file
      //If there is no filetype, allow for detecting one.  Esp. useful for ":ball" used in an 
      //autocommand. If there already is a filetype we might prefer to keep it.
      if (*curBook->fileType == ZERO)
         curBook->didFiletype = FALSE;

      bookOpenFromInvo(false, NULL, 0);
   } else {
      if (!msg_silent && !shortmess(SHM_FILEINFO))
         need_fileinfo = TRUE;   // display file info after redraw

      // check if file changed
      (void)fiCheckBookTimestamp(curBook);

      curPor->topLine = 1;
      curPor->topFill = 0;
      applyAutocomms(EVENT_BUFENTER, NULL, NULL, false, curBook);
      applyAutocomms(EVENT_BUFWINENTER, NULL, NULL, false, curBook);
    }

   //If autocommands did not change the cursor position, restore cursor lnum
   //and possibly cursor col.
   if (curPor->cursor.lnum == 1 && inindent(0))
      getLastKnownLineNumber();

   check_arg_idx(curPor);      // check for valid arg_idx
   // when autocmds didn't change it
   if (curPor->topLine == 1 && !curPor->wasTopLineSet)
      scroll_cursor_halfway(FALSE, FALSE);   // redisplay at correct position

   // Change directories when the 'acd' option is set.
   DO_AUTOCHDIR;

   curBook->lastUsed = eeTime();
   redraw_later(UPD_NOT_VALID);
}

private void
no_write_message_buf(Book* book) {
   if (term_job_running(book->term))
      emsg(_(e_job_still_running_add_bang_to_end_the_job));
   else
      showErrFmtMsg(_(e_no_write_since_last_change_for_buffer_nr_add_bang_to_override), book->fiNum);
}

void
no_write_message(void) {
   if (term_job_running(curBook->term))
      emsg(_(e_job_still_running_add_bang_to_end_the_job));
   else
      emsg(_(e_no_write_since_last_change_add_bang_to_override));
}

void
no_write_message_nobang(Book* book) {
   if (term_job_running(book->term))
      emsg(_(e_job_still_running));
   else
      emsg(_(e_no_write_since_last_change));
}

// functions for dealing with the book list

// Return TRUE if the current book is empty, unnamed, unmodified and used in
// only one portal. That means it can be re-used.
private Boole
isCurBookReusable(void) {
   return (curBook
      && !curBook->fullFileName
      && curBook->countPortals <= 1
      && (!curBook->mem.mfile || CURBOOK_EMPTY())
      && !isLocationListBook(curBook)
      && !doWasCurBookChanged()
   );
}

//Add a file name to the book list. Return a pointer to the book.
//If the same file name already exists, return a pointer to that book.
//If it does not exist, or if fname == NULL, a new entry is created.
//If (flags & BLN_CURBOOK) is TRUE, may use current book.
//If (flags & BLN_LISTED) is TRUE, add new book to book list.
//If (flags & BLN_DUMMY) is TRUE, don't count it as a real book.
//If (flags & BLN_NEW) is TRUE, don't use an existing book.
//If (flags & BLN_NOOPT) is TRUE, don't copy options from the current book
//               if the book already exists.
//If (flags & BLN_REUSE) is TRUE, may use book number from "recycledFileNumberS".
//This is the ONLY way to create a new book.
Book*
bookNew(
   CS ffname_arg, // full path of fname or relative
   CS sfname_arg, // short fname or NULL
   LineNr lnum,      // preferred cursor line
   Unt flags
) {                    // BLN_ defines
   CS fullFName = ffname_arg;
   CS sfname = sfname_arg;
   FileStat   st;

   if (top_file_num == 1)
      hash_init(&buf_hashtab);

   fname_expand(&fullFName, &sfname);   // will allocate fullFName

   // If the file name already exists in the list, update the entry. On Unix we can use inode 
   // numbers when the file exists. Works better for hard links.
   if (!sfname || stat((char *)sfname, &st) < 0)
      st.st_dev = (Device)-1;
      
   Book* book;
   if (fullFName && (flags & (BLN_DUMMY | BLN_NEW)) == 0 
         && (book = booklistFindName_stat(fullFName, &st)) != NULL
   ) { // found existing book with this file
      eeglFree(fullFName);
      if (lnum != 0)
         bookSetPosInPort(book, (flags & BLN_NOCURWIN) ? NULL : curPor, lnum, (ColNr)0, FALSE);

      if ((flags & BLN_NOOPT) == 0)
         optsCopyToBook(book, 0);

      if ((flags & BLN_LISTED) && !book->o.bookListed) {
         BookRef bookRef;
         book->o.bookListed = true;
         bookStoreInRef(OUT &bookRef, book);
         if (!(flags & BLN_DUMMY)) {
            if (applyAutocomms(EVENT_BUFADD, NULL, NULL, false, book) && !bookRefValid(&bookRef))
               return NULL;
         }
      }
      return book;
   }

   //If the current book has no name and no contents, use it.
   //Otherwise: Need to allocate a new book structure.
   //
   //This is the ONLY place where a new book structure is allocated!
   //(A spell file book is allocated in spell.c, but that's not a normal book)
   book = NULL;
   if ((flags & BLN_CURBOOK) != 0 && isCurBookReusable()) {
      BookRef bookRef;
      book = curBook;
      
      bookStoreInRef(OUT &bookRef, book);
      trigger_undo_ftplugin(book, curPor);
      // It's like this book is deleted.  Watch out for autocommands that
      // change curBook! If that happens, allocate a new book anyway.
      bookFreeAll(book, BFA_WIPE | BFA_DEL);
      if (aborting()) {    // autocmds may abort script processing
         eeglFree(fullFName);
         return NULL;
      }
      if (!bookRefValid(&bookRef))
         book = NULL;      // book was deleted; allocate a new book
   }
   if (book != curBook || curBook == NULL) {
      book = ALLOC_CLEAR_ONE(Book);
      
      // init b: variables
      book->bVars = allocBag_id(aid_newbuf_bvars);
      if (!book->bVars) {
         eeglFree(fullFName);
         eeglFree(book);
         return NULL;
      }
      init_var_dict(book->bVars, &book->bookVar, VAR_SCOPE);
      init_changedtick(book);
   }

   if (fullFName) {
      book->fullFileName = fullFName;
      book->shortFileName = copyStr(sfname);
   }
        
   clearPortInfo(book);
   book->portInfos = ALLOC_CLEAR_ONE(PortInfo);

   if (fullFName && (!book->fullFileName || !book->shortFileName)) {
      if (book->shortFileName != book->fullFileName)
         EE_CLEAR(book->shortFileName);
      else
         book->shortFileName = NULL;
      EE_CLEAR(book->fullFileName);
      if (book != curBook)
         freeBook(book);
      return NULL;
   }

   if (book == curBook) {
      freeAttachedData(book, FALSE);   // delete local variables et al.

      // Init the options.
      book->o.initialized = false;
      book->o.modifiable = (flags & BLN_MODIFIABLE);
      optsCopyToBook(book, BCO_ENTER);
   } else {
      // put the new book at the end of the book list
      book->next = NULL;
      
      if (firstBook == NULL) {     // book list is empty
         book->prev = NULL;
         firstBook = book;
      } else {        // append new book at end of list
         lastBook->next = book;
         book->prev = lastBook;
      }
      lastBook = book;

      if ((flags & BLN_REUSE) != 0 && recycledFileNumberS.len > 0) {
         // Recycle a previously used book number. Used for books which
         // are normally hidden, e.g. in a popup portal. Avoids that the book number grows rapidly
         --recycledFileNumberS.len;
         book->fiNum = ((int *)recycledFileNumberS.c)[recycledFileNumberS.len];

         // Move book to the right place in the book list.
         while (book->prev && book->fiNum < book->prev->fiNum) {
            Book   *prev = book->prev;

            prev->next = book->next;
            if (prev->next)
               prev->next->prev = prev;
            book->next = prev;
            book->prev = prev->prev;
            if (book->prev)
               book->prev->next = book;
            prev->prev = book;
            if (lastBook == book)
               lastBook = prev;
            if (firstBook == prev)
               firstBook = book;
         }
      } else
         book->fiNum = top_file_num++;
      if (top_file_num < 0) {     // wrap around (may cause duplicates)
         emsg(_("W14: Warning: List of file names overflow"));
         if (emsg_silent == 0 && !in_assert_fails) {
            out_flush();
            ui_delay(3001L, TRUE);   // make sure it is noticed
         }
         top_file_num = 1;
      }
      addBookToHashtable(book);

      // Always copy the options from the current book.
      book->o.modifiable = flags & BLN_MODIFIABLE;
      optsCopyToBook(book, BCO_ALWAYS);
   }

   book->portInfos->wi_fpos.lnum = lnum;
   book->portInfos->portal = curPor;

   hash_init(&book->syntax.keywords);
   hash_init(&book->syntax.keywordsIgnoreCase);

   book->currFileName = book->shortFileName;
   if (st.st_dev == (Device) - 1)
      book->isDevNumValid = FALSE;
   else {
      book->isDevNumValid = TRUE;
      book->devNum = st.st_dev;
      book->inode = st.st_ino;
   }
   book->undo.synced = TRUE;
   book->flags = BF_CHECK_RO | BF_NEVERLOADED;
   if (flags & BLN_DUMMY)
      book->flags |= BF_DUMMY;
   buf_clear_file(book);
   clrallmarks(book);         // clear marks
   
   fmarks_check_names(book);      // check file marks for this file
   book->o.bookListed = (flags & BLN_LISTED) ? true : false;   // init 'buflisted'
   if (!(flags & BLN_DUMMY)) {
      BookRef bookRef;

      // Tricky: these autocommands may change the book list. They could also split the portal
      // with re-using the one empty book. This may result in unexpectedly losing the empty book
      bookStoreInRef(OUT &bookRef, book);
      if (applyAutocomms(EVENT_BUFNEW, NULL, NULL, false, book) && !bookRefValid(&bookRef))
         return NULL;
      if (flags & BLN_LISTED) {
         if (applyAutocomms(EVENT_BUFADD, NULL, NULL, false, book) && !bookRefValid(&bookRef))
            return NULL;
      }
      if (aborting())      // autocmds may abort script processing
          return NULL;
   }
      
   return book;
}

//Get alternate file "n".
//Set linenr to "lnum" or altfpos.lnum if "lnum" == 0.
//  Also set cursor column to altfpos.col if 'startofline' is not set.
//if (options & GETF_SETMARK) call setpcmark()
//if (options & GETF_ALT) we are jumping to an alternate file.
//if (options & GETF_SWITCH) respect 'switchbook' settings when jumping
//
//Return FAIL for failure, OK for success.
int
booklistGetFile(
   int n,
   LineNr   lnum,
   int options,
   int forceit
){
   Portal   *po = NULL;
   Pos   *fpos;
   ColNr   col;

   Book* book = bookFindFileByBookNr(n);
   if (!book) {
      if ((options & GETF_ALT) && n == 0)
         emsg(_(e_no_alternate_file));
      else
         showErrFmtMsg(_(e_book_nr_not_found), n);
      return FAIL;
   }

   // if alternate file is the current book, nothing to do
   if (book == curBook)
      return OK;

   if (text_or_buf_locked())
      return FAIL;

   // altfpos may be changed by getfile(), get it now
   if (lnum == 0) {
      fpos = bookFindFpos(book);
      lnum = fpos->lnum;
      col = fpos->col;
   } else
      col = 0;

   if ((options & GETF_SWITCH) != 0) {
      // If @switchbook is set, jump to the portal containing "book".
      po = switchBufGotoPortalIntoBuf(book);

      //If @switchbook contains "split", "vsplit" or "newtab" and the
      //current book isn't empty: open new tab or portal
      if (!po && (p_swb & (SWB_VSPLIT | SWB_SPLIT | SWB_NEWTAB)) != 0 && !CURBOOK_EMPTY()) {
         if ((p_swb & SWB_NEWTAB) != 0)
            tabNew();
         ei (splitPortal(0, (p_swb & SWB_VSPLIT) ? WSP_VERT : 0) == FAIL)
            return FAIL;
         RESET_BINDING(curPor);
      }
   }

   ++isRedrawingDisabledG;
   int retval = FAIL;
   if (GETFILE_SUCCESS(getfile(book->fiNum, NULL, NULL, (options & GETF_SETMARK), lnum, forceit))) {
      // cursor is at to BOL and cursor.lnum is checked due to getfile()
      if (!p_sol && col != 0) {
         curPor->cursor.col = col;
         check_cursor_col();
         curPor->cursor.coladd = 0;
         curPor->setCursWant = TRUE;
      }
      retval = OK;
   }

   if (isRedrawingDisabledG > 0)
      --isRedrawingDisabledG;
   return retval;
}

// Go to the last known line number for the current book
private void
getLastKnownLineNumber(void) {
   Pos* fpos = bookFindFpos(curBook);

   curPor->cursor.lnum = fpos->lnum;
   check_cursor_lnum();

   if (p_sol)
      curPor->cursor.col = 0;
   else {
      curPor->cursor.col = fpos->col;
      check_cursor_col();
      curPor->cursor.coladd = 0;
      curPor->setCursWant = true;
   }
}

// Find file in book list by name. Return NULL if not found.
Book *
booklistFindByNameExpandingLinks(CS fname) {
   // First make the name into a full path name
   CS fullFName = fiExpandAndCopy(fname, TRUE);      // force expansion, get rid of symbolic links
   Book* book = NULL;
   if (fullFName) {
      book = booklistFindName(fullFName);
      eeglFree(fullFName);
   }
   return book;
}

//Find file in book list by name. "fullFName" must have a full path.
//Skip dummy books. Return NULL if not found.
Book*
booklistFindName(CS fullFName){
   FileStat st;
   if (stat((char *)fullFName, &st) < 0)
      st.st_dev = (Device)-1;
   return booklistFindName_stat(fullFName, &st);
}

private Boole
sameFileInBook(Book* book, CS fullFName, FileStat* stp) {
   // no name is different
   if (fullFName == NULL || *fullFName == ZERO || book->fullFileName == NULL)
      return false;
   if (fnamecmp(fullFName, book->fullFileName) == 0)
      return true;

   FileStat st;
   // If no FileStat given, get it now
   if (stp == NULL) {
      if (!book->isDevNumValid || stat((char *)fullFName, &st) < 0)
         st.st_dev = (Device)-1;
      stp = &st;
   }
   // Use dev/ino to check if the files are the same, even when the names are different (possible 
   // with links).  Still need to compare the name above, for when the file doesn't exist yet.
   // Problem: The dev/ino changes when a file is deleted (and created again) and remains the same 
   // when renamed/moved.  We don't want to stat() each book each time, that would be too 
   // slow.  Get the dev/ino again when they appear to match, but not when they appear to be 
   // different: Could skip a book when it's actually the same file.
   if (areSameInode(book, stp)) {
      buf_setino(book);
      if (areSameInode(book, stp))
         return true;
   }
   return false;
}


// Find file in book list by name, but pass the stat structure to avoid getting it twice for 
// the same file. Return NULL if not found.
private Book*
booklistFindName_stat(CS fullFName, FileStat* stp) {
   // Start at the last book, expect to find a match sooner.
   Book* book;
   FOR_ALL_BOOKS_FROM_LAST(book) {
      if ((book->flags & BF_DUMMY) == 0 && sameFileInBook(book, fullFName, stp))
         return book;
   } 
   return NULL;
}

//Find file in book list by a regexp pattern.
//Return fnum of the found book. Return < 0 for error.
int
booklistFindPattern(
   CS pattern,
   CS pattern_end,   // pointer to first char after pattern
   int unlisted,   // find unlisted books
   int diffmode UNUSED, // find diff-mode books only
   int curtab_only  // find books in current tab only
){
   Book* book;
   int      match = -1;
   int      find_listed;
   CS p;

   // "%" is current file, "%%" or "#" is alternate file
   if ((pattern_end == pattern + 1 && (*pattern == '%' || *pattern == '#'))) {
      if (*pattern == '#' || pattern_end == pattern + 2)
         match = curPor->altFnum;
      else
         match = curBook->fiNum;
      if (diffmode && !diffIsBookInDiffMode(bookFindFileByBookNr(match)))
         match = -1;
   }

   //Try four ways of matching a listed book:
   //attempt == 0: without '^' or '$' (at any position)
   //attempt == 1: with '^' at start (only at position 0)
   //attempt == 2: with '$' at end (only match at end)
   //attempt == 3: with '^' at start and '$' at end (only full match)
   //Repeat this for finding an unlisted book if there was no matching listed book.
   else {
      CS pat = file_pat_to_reg_pat(pattern, pattern_end, NULL);
      CS patend = pat + STRLEN(pat) - 1;
      Boole toggledollar = (patend > pat && *patend == '$');

      // First try finding a listed book.  If not found and "unlisted" is TRUE, try finding an 
      // unlisted one.
      find_listed = TRUE;
      for (;;) {
         for (int attempt = 0; attempt <= 3; ++attempt) {
            RegMatch   regmatch;

            // may add '^' and '$'
            if (toggledollar)
               *patend = (attempt < 2) ? ZERO : '$'; // add/remove '$'
            p = pat;
            if (*p == '^' && !(attempt & 1))    // add/remove '^'
               ++p;
            regmatch.regprog = compileRegexp(p, RE_MAGIC);

            FOR_ALL_BOOKS_FROM_LAST(book) {
               if (regmatch.regprog == NULL) {
                  // invalid pattern, possibly after switching engine
                  eeglFree(pat);
                  return -1;
               }
               if (book->o.bookListed == find_listed
                      && (!diffmode || diffIsBookInDiffMode(book))
                      && checkFilenameMatch(&regmatch, book) != NULL) {
                  if (curtab_only) {
                     // Ignore the match if the book is not open in the current tab.
                     Portal* po;
                     FOR_ALL_PORTALS(po) {
                        if (po->book == book)
                           break;
                     } 
                     if (!po)
                        continue;
                  }
                  if (match >= 0) {     // already found a match
                     match = -2;
                     break;
                  }
                  match = book->fiNum;   // remember first match
               }
            }

            eeRegFree(regmatch.regprog);
            if (match >= 0)         // found one match
               break;
         }

         // Only search for unlisted books if there was no match with a listed book.
         if (!unlisted || !find_listed || match != -1)
            break;
         find_listed = FALSE;
      }

      eeglFree(pat);
   }

   if (match == -2)
      showErrFmtMsg(_(e_more_than_one_match_for_str), pattern);
   ei (match < 0)
      showErrFmtMsg(_(e_no_matching_buffer_for_str), pattern);
   return match;
}

typedef struct {
   Book* book;
   CS match;
} BufMatch;

LIST_TY(BufMatch)
private LIST_CREATE(BufMatch)
#define ADD_LIST_TY BufMatch
#include "generic.h"

//Find all book names that match. For command line expansion of ":book" and ":sbook".
//Return OK if matches found, FAIL otherwise.
int
bufExpandBufnames(
   CS pat,
   Unt options,
   OUT ExpandMatch* matches
){
   CS p;
   CS patSaved = NULL;
   LBufMatch* bufMatches = createLBufMatch(2, matches->a);
   Fuzzy fuzzy = {};
   fuzzy.a = matches->a;
   RegMatch regmatch;
   int score = 0;
   Boole to_free = false;

   if ((options & BOOK_DIFF_FILTER) != 0 && !curPor->o.diff)
      return FAIL;

   Boole doFuzzy = scrIsCommlineFuzzyCompletable(pat);

   // Make a copy of "pat" and change "^" to "\(^\|[\/]\)" (if doing regular
   // expression matching)
   if (!doFuzzy) {
      if (*pat == '^' && pat[1] != ZERO) {
         int len = (int)STRLEN(pat);
         patSaved = alloc(len);
         STRNCPY(patSaved, pat + 1, len - 1);
         patSaved[len - 1] = ZERO;
         to_free = true;
      } ei (*pat == '^')
         patSaved = Em;
      else
         patSaved = pat;
      regmatch.regprog = compileRegexp(patSaved, RE_MAGIC);
   }

   Book* book;
   FOR_ALL_BOOKS(book) {
      if (!book->o.bookListed)
         continue;
      if ((options & BOOK_DIFF_FILTER) != 0 && (book == curBook || !diffIsBookInDiffMode(book)))
         // Skip books not suitable for :diffget or :diffput completion.
         continue;

      if (doFuzzy) {
         p = NULL;
         // first try matching with the short file name
         if ((score = fuzzyMatchStr(book->shortFileName, pat)) != FUZZY_SCORE_NONE)
            p = book->shortFileName;
         // next try matching with the full path file name
         if (!p && (score = fuzzyMatchStr(book->fullFileName, pat)) != FUZZY_SCORE_NONE)
            p = book->fullFileName;
      } else {
         if (!regmatch.regprog) {
            // invalid pattern, possibly after recompiling
            if (to_free)
               eeglFree(patSaved);
            return FAIL;
         }
         p = checkFilenameMatch(&regmatch, book);
      }

      if (!p)
         continue;

      if (options & WILD_HOME_REPLACE)
         p = home_replace_save(book, p);
      else
         p = copyStr(p);

      if (doFuzzy) {
         addFuzzyMatch((FuzzyMatch){.str = p, .score = score}, OUT &fuzzy);
      } else {
         add(((BufMatch){book, p}), bufMatches);
      }
   }
   if (!doFuzzy) {
      eeRegFree(regmatch.regprog);
      if (to_free)
         eeglFree(patSaved);
   }

   if (doFuzzy) {
       if (defuzz(OUT matches, fuzzy, false) == FAIL)
          return FAIL;
   } else {  
      if (bufMatches->len > 1)
         qsort(bufMatches->c, bufMatches->len, sizeof(BufMatch), bookCompare);
      // if the current book is first in the list, place it at the end
      if (bufMatches->c[0].book == curBook) {
         for (Unt i = 1; i < bufMatches->len; i++)
            addExpandMatch(bufMatches->c[i].match, OUT matches);
         addExpandMatch(bufMatches->c[bufMatches->len - 1].match, OUT matches);
      } else {
         for (Unt i = 0; i < bufMatches->len; i++)
            addExpandMatch(bufMatches->c[i].match, OUT matches);
      }
   }

   return (matches->len > 0 ? OK : FAIL);
}

// Check for a match on the file name for book "book" with regprog "prog".
private CS
checkFilenameMatch(RegMatch* rmp, Book* book) {
   // First try the short file name, then the long file name.
   CS match = fname_match(rmp, book->shortFileName);
   if (match == NULL && rmp->regprog)
      match = fname_match(rmp, book->fullFileName);

   return match;
}

//Try matching the regexp in "rmp->regprog" with file name "name".
//Note that rmp->regprog may become NULL when switching regexp engine.
//Return "name" when there is a match, NULL when not.
private CS
fname_match(RegMatch* rmp, CS name){
   // extra check for valid arguments
   if (!name || !rmp->regprog)
      return NULL;

   // Ignore case when 'fileignorecase' or the argument is set.
   rmp->rm_ic = FALSE;
   CS match = null;
   if (eeRegexec(rmp, name, (ColNr)0))
      match = name;
   ei (rmp->regprog) {
      // Replace $(HOME) with '~' and try matching again.
      CS p = home_replace_save(NULL, name);
      if (p && eeRegexec(rmp, p, (ColNr)0))
         match = name;
      eeglFree(p);
    }

    return match;
}

// Find a file in the book list by book number.
Book*
bookFindFileByBookNr(int nr){
   Byte key[EE_SIZEOF_INT * 2 + 1];

   if (nr == 0)
      nr = curPor->altFnum;
   eeSnprintf(key, sizeof(key), "%x", nr);
   
   EeSetItem* hi = hash_find(&buf_hashtab, (Text){.c = key, .len = sizeof(key)});

   if (!HASHITEM_EMPTY(hi))
      return (Book *)(hi->hi_key - ((unsigned)(curBook->keyContainer - (CS)curBook)));
   return NULL;
}

//Get name of file 'n' in the book list. When the file has no name an empty string is returned.
//home_replace() is used to shorten the file name (used for marks).
//Return a pointer to allocated memory, of NULL when failed.
CS
bookGetNameByBookNr(
   int      n,
   int      fullname,
   int      helptail   // for help books return tail only
){
   Book* book = bookFindFileByBookNr(n);
   if (!book)
      return NULL;
   return home_replace_save(helptail ? book : NULL,
                 fullname ? book->fullFileName : book->currFileName);
}

//Set the "lnum" and "col" for book "book" and a portal into it.
//When "copy_options" is TRUE save the local portal option values.
//When "lnum" is 0 only do the options.
void
bookSetPosInPort(
   Book* book,
   Portal* port,      // may be NULL when using :badd
   LineNr lnum,
   ColNr col,
   Boole copy_options
) {
   PortInfo* poInfo;

   FOR_ALL_BOOK_PORTINFOS(book, poInfo) {
      if (poInfo->portal == port)
         break;
   } 
   if (!poInfo) {
      // allocate a new entry
      poInfo = ALLOC_CLEAR_ONE(PortInfo);
      poInfo->portal = port;
      if (lnum == 0)      // set lnum even when it's 0
         lnum = 1;
   } else {
      // remove the entry from the list
      if (poInfo->prev)
         poInfo->prev->next = poInfo->next;
      else
         book->portInfos = poInfo->next;
      if (poInfo->next)
         poInfo->next->prev = poInfo->prev;
      if (copy_options && poInfo->isOptChanged) {
         optClearPortOptions(&poInfo->opt);
         deleteFoldRecurse(&poInfo->folds);
      }
   }
   if (lnum != 0) {
      poInfo->wi_fpos.lnum = lnum;
      poInfo->wi_fpos.col = col;
   }
   if (port)
      poInfo->wi_changelistidx = port->changeListInd;
   if (copy_options && port) {
      // Save the portal-specific option values.
      copyPortOpt(&poInfo->opt, &port->o);
      poInfo->foldManual = port->foldManual;
      cloneFoldArrayList(&port->folds, &poInfo->folds);
      poInfo->isOptChanged = true;
   }

   // insert the entry in front of the list
   poInfo->next = book->portInfos;
   book->portInfos = poInfo;
   poInfo->prev = NULL;
   if (poInfo->next)
      poInfo->next->prev = poInfo;
}

// Return TRUE when "poInfo" has 'diff' set and the diff is only for another tab.  
// That's because a diff is local to a tab.
private int
wininfo_other_tab_diff(PortInfo* poInfo){
   if (!poInfo->opt.diff)
      return FALSE;

   Portal* po;
   FOR_ALL_PORTALS(po) {
      // return FALSE when it's a portal into current tab, thus the book was in diff mode here
      if (poInfo->portal == po)
         return FALSE;
   } 
   return TRUE;
}

//Find info for the current portal in book "book".
//If not found, return the info for the most recently used portal.
//When "need_options" is TRUE skip entries where isOptChanged is false.
//When "skipDiffBook" is TRUE avoid portals with 'diff' set that is in another tab.
//Return NULL when there isn't any info.
private PortInfo*
find_wininfo(Book* book, int need_options, Boole skipDiffBook){
   PortInfo* poInfo;
   FOR_ALL_BOOK_PORTINFOS(book, poInfo) {
      if (poInfo->portal == curPor
            && (!skipDiffBook || !wininfo_other_tab_diff(poInfo))
            && (!need_options || poInfo->isOptChanged)
      )
         break;
   } 

   if (poInfo)
      return poInfo;

   //If no wininfo for curPor, use the first in the list (that doesn't have
   //'diff' set and is in another tab).
   //If "need_options" is TRUE skip entries that don't have options set,
   //unless the portal is editing "book", so we can copy from the portal itself.
   if (skipDiffBook) {
      FOR_ALL_BOOK_PORTINFOS(book, poInfo)
         if (!wininfo_other_tab_diff(poInfo)
             && (!need_options || poInfo->isOptChanged || (poInfo->portal && poInfo->portal->book == book))
         )
            break;
   } else
      poInfo = book->portInfos;
   return poInfo;
}

//Reset the local portal options to the values last used in this portal. If the book wasn't used 
//in this portal before, use the values from the most recently used portal. If the values were 
//never set, use the global values for the portal.
void
get_winopts(Book* book) {
   optClearPortOptions(&curPor->o);
   clearFolding(curPor);

   PortInfo* poInfo = find_wininfo(book, TRUE, TRUE);
   if (poInfo && poInfo->portal && poInfo->portal != curPor && poInfo->portal->book == book) {
      // The book is currently displayed in the portal: use the actual
      // option values instead of the saved (possibly outdated) values.
      Portal *po = poInfo->portal;

      copyPortOpt(&curPor->o, &po->o);
      curPor->foldManual = po->foldManual;
      curPor->foldNeedsRecomputation = true;
      cloneFoldArrayList(&po->folds, &curPor->folds);
   } ei (poInfo && poInfo->isOptChanged) {
      // the book was displayed in the current portal earlier
      copyPortOpt(&curPor->o, &poInfo->opt);
      curPor->foldManual = poInfo->foldManual;
      curPor->foldNeedsRecomputation = true;
      cloneFoldArrayList(&poInfo->folds, &curPor->folds);
   }
   if (poInfo)
      curPor->changeListInd = poInfo->wi_changelistidx;

   // Set 'foldlevel' to 'foldlevelstart' if it's not negative.
   if (foldLevelStart >= 0)
      curPor->o.foldLevel = foldLevelStart;
   afterCopyPortOpt(curPor);
}

//Find the position (lnum and col) for the book 'book' for the current portal.
//Return a pointer to no_position if no position is found.
Pos *
bookFindFpos(Book* book){
   static Pos no_position = {1, 0, 0};
   PortInfo* poInfo = find_wininfo(book, FALSE, FALSE);
   return poInfo ? &(poInfo->wi_fpos) : &no_position;
}

// List all known file names (for :files and :books command).
void
bookListFiles(Invocation* invo) {
   Book* book = firstBook;
   int i;
   int ro_char;
   int changed_char;
   int job_running;
   int job_none_open;

   ArrayList buflist;
   Book** booklistData = NULL;

   if (firstOccurrence(invo->arg, 't')) {
      ga_init2(&buflist, sizeof(Book *), 50);
      FOR_ALL_BOOKS(book) {
         if (ga_grow(&buflist, 1) == OK)
            ((Book **)buflist.c)[buflist.len++] = book;
      }

      qsort(buflist.c, (Unt)buflist.len, sizeof(Book *), bookCompare);

      booklistData = (Book **)buflist.c;
      book = *booklistData;
   }
   Book** p = booklistData;

   int len;
   for (; book && !gotInterruptG; book = booklistData
       ? (++p < booklistData + buflist.len ? *p : NULL)
       : book->next
   ){
      job_running = term_job_running(book->term);
      job_none_open = term_none_open(book->term);
      // skip unlisted books, unless ! was used
      if ((!book->o.bookListed && !invo->forceit && !firstOccurrence(invo->arg, 'u'))
            || (firstOccurrence(invo->arg, 'u') && book->o.bookListed)
            || (firstOccurrence(invo->arg, '+') 
                  && ((book->flags & BF_READERR) || !doWasBookChanged(book))
               )
            || (firstOccurrence(invo->arg, 'a') 
                  && (book->mem.mfile == NULL || book->countPortals == 0)
               )
            || (firstOccurrence(invo->arg, 'h') 
                  && (book->mem.mfile == NULL || book->countPortals != 0)
               )
            || (firstOccurrence(invo->arg, 'R') && (!job_running || (job_running && job_none_open)))
            || (firstOccurrence(invo->arg, '?') 
                  && (!job_running || (job_running && !job_none_open))
               )
            || (firstOccurrence(invo->arg, 'F') && (job_running || book->term == NULL))
            || (firstOccurrence(invo->arg, '-') && book->o.modifiable)
            || (firstOccurrence(invo->arg, '=') && book->o.modifiable)
            || (firstOccurrence(invo->arg, 'x') && !(book->flags & BF_READERR))
            || (firstOccurrence(invo->arg, '%') && book != curBook)
            || (firstOccurrence(invo->arg, '#') 
                  && (book == curBook || curPor->altFnum != book->fiNum))
               )
         continue;
      CS name = bookSpName(book);
      if (name)
         copySubstrToAllocation(OUT NameBuff, (Text){name, MAXPATHL - 1});
      else
         home_replace(book, book->currFileName, NameBuff, MAXPATHL, TRUE);
      if (message_filtered(NameBuff))
         continue;

      changed_char = (book->flags & BF_READERR) ? 'x' : (doWasBookChanged(book) ? '+' : ' ');
      if (job_running) {
         if (job_none_open)
            ro_char = '?';
         else
            ro_char = 'R';
         changed_char = ' ';  // doWasBookChanged() returns TRUE to avoid
                // closing, but it's not actually changed.
      } ei (book->term)
         ro_char = 'F';
      else
         ro_char = !book->o.modifiable ? '-' : '=';

      msg_putchar('\n');
      len = (int)eeSnprintfSafelen(IObuff, IOSIZE - 20, "%3d%c%c%c%c%c \"%s\"",
         book->fiNum,
         book->o.bookListed ? ' ' : 'u',
         book == curBook ? '%' : (curPor->altFnum == book->fiNum ? '#' : ' '),
         book->mem.mfile == NULL ? ' ' : (book->countPortals == 0 ? 'h' : 'a'),
         ro_char,
         changed_char,
         NameBuff
      );

      // put "line 999" in column 40 or after the file name
      i = 40 - eeglStrSize(IObuff);
      do
          IObuff[len++] = ' ';
      while (--i > 0 && len < IOSIZE - 18);
      if (firstOccurrence(invo->arg, 't') && book->lastUsed)
         add_time(IObuff + len, (Unt)(IOSIZE - len), book->lastUsed);
      else
         eeSnprintf(IObuff + len, (Unt)(IOSIZE - len),
             _("line %ld"), book == curBook ? curPor->cursor.lnum : (long)findLnum(book));
      msg_outtrans(IObuff);
      out_flush();       // output one line at a time
      ui_breakcheck();
   }

   if (booklistData)
      ga_clear(&buflist);
}

//Get file name and line number for file 'fnum'. Used by DoOneCmd() for translating '%' and '#'.
//Used by insert_reg() and cmdline_paste() for '#' register. Return FAIL if not found, or OK.
int
bookGetFnameByFileId(
   int fnum,
   OUT CS* fname,
   OUT LineNr* lnum
){
   Book* book = bookFindFileByBookNr(fnum);
   if (!book || book->currFileName == NULL)
      return FAIL;

   *fname = book->currFileName;
   *lnum = findLnum(book);

   return OK;
}

//Set the file name for "book"' to "ffname_arg", short file name to "sfname_arg".
//The file name with the full path is also remembered, for when :cd is used.
//Return FAIL for failure (file name already in use by other book) OK otherwise.
int
setfname(
   Book* book,
   CS ffname_arg,
   CS sfname_arg,
   int message   // give message when book already exists
){
   CS fullFName = ffname_arg;
   CS sfname = sfname_arg;
   Book* obook = NULL;
   FileStat   st;

   if (fullFName == NULL || *fullFName == ZERO) {
      // Removing the name.
      if (book->shortFileName != book->fullFileName)
         EE_CLEAR(book->shortFileName);
      else
         book->shortFileName = NULL;
      EE_CLEAR(book->fullFileName);
      st.st_dev = (Device)-1;
   } else {
      fname_expand(&fullFName, &sfname); // will allocate fullFName
      if (fullFName == NULL)          // out of memory
         return FAIL;

      //If the file name is already used in another book:
      //- if the book is loaded, fail
      //- if the book is not loaded, delete it from the list
      if (stat((char *)fullFName, &st) < 0)
         st.st_dev = (Device)-1;
      if (!(book->flags & BF_DUMMY))
         obook = booklistFindName_stat(fullFName, &st);
      if (obook && obook != book) {
         Portal   *port;
         Tab   *tab;
         Boole in_use = false;

         // during startup a portal may use a book that is not loaded yet
         FOR_ALL_TAB_PORTALS(tab, port) {
            if (port->book == obook)
               in_use = true;
         } 

         // it's loaded or used in a portal, fail
         if (obook->mem.mfile || in_use) {
            if (message)
               emsg(_(e_buffer_with_this_name_already_exists));
            eeglFree(fullFName);
            return FAIL;
         }
         // delete from the list
         closeBook(NULL, obook, DOBOOK_WIPE, FALSE, FALSE);
      }
      sfname = copyStr(sfname);
      if (!fullFName || !sfname) {
         eeglFree(sfname);
         eeglFree(fullFName);
         return FAIL;
      }
      if (book->shortFileName != book->fullFileName)
         eeglFree(book->shortFileName);
      eeglFree(book->fullFileName);
      book->fullFileName = fullFName;
      book->shortFileName = sfname;
   }
   book->currFileName = book->shortFileName;
   if (st.st_dev == (Device)-1)
      book->isDevNumValid = FALSE;
   else {
      book->isDevNumValid = TRUE;
      book->devNum = st.st_dev;
      book->inode = st.st_ino;
   }

   bookHandleNameChange(book);
   return OK;
}

// Crude way of changing the name of a book.  Use with care!
// The name should be relative to the current directory.
void
bookSetName(int fnum, CS name) {
   Book* book = bookFindFileByBookNr(fnum);
   if (!book)
      return;

   if (book->shortFileName != book->fullFileName)
      eeglFree(book->shortFileName);
   eeglFree(book->fullFileName);
   book->fullFileName = copyStr(name);
   book->shortFileName = NULL;
   // Allocate fullFName and expand into full path.
   fname_expand(&book->fullFileName, &book->shortFileName);
   book->currFileName = book->shortFileName;
}

// Take care of what needs to be done when the name of book has changed.
void
bookHandleNameChange(Book* book) {
   // If the file name changed, also change the name of the swapfile
   if (book->mem.mfile)
      ml_setname(book);

   if (book->term != NULL)
      term_clear_status_text(book->term);

   if (curPor->book == book)
      check_arg_idx(curPor);   // check file name for arg list
   status_redraw_all();   // status lines need to be redrawn
   fmarks_check_names(book);   // check named file marks
   ml_timestamp(book);      // reset timestamp
}

//set alternate file name for current portal
//
//Used by do_one_cmd(), do_write() and startEditingFile(). Return the book.
Book *
setaltfname(
   CS fullFName,
   CS sfname,
   LineNr lnum
){
   // Create a book.  'buflisted' is not set if it's a new book
   Book* book = bookNew(fullFName, sfname, lnum, 0);
   if (book && (commModifierG.cmod_flags & CMOD_KEEPALT) == 0)
      curPor->altFnum = book->fiNum;
   return book;
}

// Get alternate file name for current portal.
// Return NULL if there isn't any, and give error message if requested.
CS
getaltfname(int errmsg) {      // give error message
   CS fname;
   LineNr dummy;
   if (bookGetFnameByFileId(0, OUT &fname, OUT &dummy) == FAIL) {
      if (errmsg)
         emsg(_(e_no_alternate_file));
      return NULL;
   }
   return fname;
}

//Open a file and add it to the booklist. Return its number or 0 if failed.
//Use same flags as bookNew(), except BLN_DUMMY. Used by qfInit(), main() and doarglist()
int
bookOpen(CS fname, Unt flags){
   Book* book = bookNew(fname, NULL, (LineNr)0, flags);
   if (book)
      return book->fiNum;
   return 0;
}

// Return TRUE if 'fullFName' is not the same file as current file.
// Fname must have a full path (expanded by mch_FullName()).
Boole
fNameMatchesCurBook(CS fullFName){
   return sameFileInBook(curBook, fullFName, NULL);
}

// Set inode and device number for a book. Must always be called when currFileName is changed!.
void
buf_setino(Book* book) {
   FileStat   st;
   if (book->currFileName && STAT(book->currFileName, &st) >= 0) {
      book->isDevNumValid = TRUE;
      book->devNum = st.st_dev;
      book->inode = st.st_ino;
   } else
      book->isDevNumValid = FALSE;
}

// Return TRUE if dev/ino in book "book" matches with "stp".
private int
areSameInode(Book* book, FileStat* stp){
   return (book->isDevNumValid && stp->st_dev == book->devNum && stp->st_ino == book->inode);
}

// Print info about the current book.
void
fileinfo(
   int fullname,       // when true, print full path; whan > 1, include book number
   Boole shorthelp,
   Boole   dont_truncate
){
   Unt   bufLen = 0;
   CS buf = alloc(IOSIZE);

   if (fullname > 1)       // 2 CTRL-G: include book number
      bufLen = eeSnprintfSafelen(buf, IOSIZE, "buf %d: ", curBook->fiNum);

   buf[bufLen++] = '"';

   CS name = bookSpName(curBook);
   if (name)
      bufLen += eeSnprintfSafelen(buf + bufLen, IOSIZE - bufLen, "%s", name);
   else {
      if (!fullname && curBook->currFileName)
         name = curBook->currFileName;
      else
         name = curBook->fullFileName;
      home_replace(
         shorthelp ? curBook : NULL, name, (CS)buf + bufLen, IOSIZE - (int)bufLen, TRUE
      );
      bufLen += STRLEN(buf + bufLen);
   }

   bufLen += eeSnprintfSafelen(
      buf + bufLen,
      IOSIZE - bufLen,
      "\"%s%s%s%s%s%s",
      doWasCurBookChanged() ? (shortmess(SHM_MOD) ? (CS)" [+]" : _(" [Modified]")) : S" ",
      (curBook->flags & BF_NOTEDITED) && !bt_dontwrite(curBook) ? _("[Not edited]") : Em,
      (curBook->flags & BF_NEW) && !bt_dontwrite(curBook) ? new_file_message() : Em,
      (curBook->flags & BF_READERR) ? _("[Read errors]") : E, 
      curBook->o.modifiable ? Em : S"[-]",
      (doWasCurBookChanged() || (curBook->flags & BF_WRITE_MASK) || !curBook->o.modifiable) ? S" " : Em
   );

   if (curBook->mem.flags & ML_EMPTY) {
      bufLen += eeSnprintfSafelen(
         buf + bufLen, IOSIZE - bufLen, "%s", _(no_lines_msg)
      );
   } else {
      // Current line and column are already on the screen -- webb
      bufLen += eeSnprintfSafelen(
          buf + bufLen,
          IOSIZE - bufLen,
          NGETTEXT("%ld line --%d%%--", "%ld lines --%d%%--", curBook->mem.lineCount),
          (long)curBook->mem.lineCount,
          calc_percentage(curPor->cursor.lnum, curBook->mem.lineCount)
      );
   } 

   (void)append_arg_number(curPor, buf + bufLen, IOSIZE - bufLen, !shortmess(SHM_FILE));

   if (dont_truncate) {
      // Temporarily set msg_scroll to avoid the message being truncated.
      // First call msg_start() to get the message in the right place.
      msg_start();
      int n = msg_scroll;
      msg_scroll = TRUE;
      msg(buf);
      msg_scroll = n;
   } else {
      CS p = msgTruncDeco(buf, FALSE, 0);
      if (restart_edit != 0 || (msg_scrolled && !need_wait_return))
          // Need to repeat the message after redrawing when:
          // - When restart_edit is set (otherwise there will be a delay before redrawing).
          // - When the screen was scrolled but there is no wait-return prompt.
          set_keep_msg((CS)p, 0);
   }

   eeglFree(buf);
}

int
col_print(CS buf, Unt  buflen, int col, int vcol){
   if (col == vcol)
      return (int)eeSnprintfSafelen(buf, buflen, "%d", col);
   return (int)eeSnprintfSafelen(buf, buflen, "%d-%d", col, vcol);
}

// Used for building in the status line.
typedef struct {
   CS stl_start;
   int      stl_minwid;
   int      stl_maxwid;
   enum {
      Normal,
      Empty,
      Group,
      Separate,
      Highlight,
      TabPage,
      Trunc
   } StatusTag;
} StatusItem;

private Unt      countStatusItems = 20; // Initial value, grows as needed.
private Arr(StatusItem) stl_items = NULL;
private int* stl_groupitem = NULL;
private StatusLineHilite* statusHilitesS = NULL;
private StatusLineHilite* stl_tabtab = NULL;
private int      *stl_separator_locations = NULL;

// Build a string from the status line items in "fmt".
// Return length of string in screen cells.
//
// Items are drawn interspersed with the text that surrounds it
// Specials: %-<wid>(xxx%) => group, %= => separation marker, %< => truncation
// Item: %-<minwid>.<maxwid><itemch> All but <itemch> are optional
//
// If maxwidth is not zero, the string will be filled at any middle marker
// or truncated if too long, fillchar is used for all whitespace.
int
bookRenderStatusLine(
   Portal* po,
   CS out,      // string book to write into != NameBuff
   Unt outlen,      // length of out[]
   CS fmt,
   Byte oname,      // one of STATLINE_* constants
   int opt_scope,   // scope for "oname"
   int fillchar,
   int maxwidth,
   OUT StatusLineHilite** hltab,   // return: hilite decorations (can be NULL)
   OUT StatusLineHilite** labels   // return: tab numbers (can be NULL)
){
   LineNr   lnum;
   ColNr   len;
   Unt   outputlen;   // length of out[] used (excluding the ZERO)
   CS p;
   CS s;
   int byteval;
   int save_VIsual_active;
   int empty_line;
   long l;
   long n;
   int prevchar_isflag;
   int prevchar_isitem;
   int itemisflag;
   int fillable;
   CS str;
   long   num;
   int      width;
   int      itemcnt;
   int      curitem;
   int      groupdepth;
   int      evaldepth;
   int      minwid;
   int      maxwid;
   int      zeropad;
   Byte   base;
   Byte   opt;
#define TMPLEN 70
   Byte   buf_tmp[TMPLEN];
   CS usefmt = fmt;
   StatusLineHilite *sp;
   int save_redraw_not_allowed = redraw_not_allowed;
   int save_KeyTyped = KeyTyped;
   // TODO: find out why using called_emsg_before makes tests fail, does it matter?
   // int   called_emsg_before = called_emsg;
   int      anyEmsgSaved = anyEmsgG;

   // When inside drawUpdateScreen() we do not want redrawing a statusline,
   // ruler, title, etc. to trigger another redraw, it may cause an endless loop.
   if (updating_screen)
      redraw_not_allowed = TRUE;

   if (!stl_items) {
      stl_items = ALLOC_MULT(StatusItem, countStatusItems);
      stl_groupitem = ALLOC_MULT(int, countStatusItems);

      // Allocate one more, because the last element is used to indicate the end of the list
      statusHilitesS  = ALLOC_MULT(StatusLineHilite, countStatusItems + 1);
      stl_tabtab = ALLOC_MULT(StatusLineHilite, countStatusItems + 1);

      stl_separator_locations = ALLOC_MULT(int, countStatusItems);
   }

   // When the format starts with "%!" then evaluate it as an expression and
   // use the result as the actual format string.
   if (fmt[0] == '%' && fmt[1] == '!') {
      Var   tv;

      tv.tag = VAR_NUMBER;
      tv.number = po->id;
      set_var(tConst("g:statusline_winid"), &tv, FALSE);

      usefmt = eval_to_string_safe(fmt + 2, false);
      if (usefmt == NULL)
         usefmt = fmt;

      unletImpl(S"g:statusline_winid", true);
   }

   if (fillchar == 0)
      fillchar = ' ';

   // The cursor in portals other than the current one isn't always
   // up-to-date, esp. because of autocommands and timers.
   lnum = po->cursor.lnum;
   if (lnum > po->book->mem.lineCount) {
      lnum = po->book->mem.lineCount;
      po->cursor.lnum = lnum;
   }

   // Get line & check if empty (cursorpos will show "0-1").  Note that
   // p will become invalid when getting another book line.
   p = memGetLine(po->book, lnum, FALSE);
   empty_line = (*p == ZERO);

   // Get the byte value now, in case we need it below. This is more efficient
   // than making a copy of the line.
   len = memGetBookLen(po->book, lnum);
   if (po->cursor.col > len) {
      // Line may have changed since checking the cursor column, or the lnum was adjusted above
      po->cursor.col = len;
      po->cursor.coladd = 0;
      byteval = 0;
   } else
      byteval = (*mb_ptr2char)(p + po->cursor.col);

   groupdepth = 0;
   evaldepth = 0;
   p = out;
   curitem = 0;
   prevchar_isflag = TRUE;
   prevchar_isitem = FALSE;
   for (s = usefmt; *s != ZERO; ) {
      if (curitem == (int)countStatusItems) {
         Unt   newLen = countStatusItems * 3 / 2;

         StatusItem* new_items = eeRealloc(stl_items, sizeof(StatusItem) * newLen);
         stl_items = new_items;

         int *new_groupitem = eeRealloc(stl_groupitem, sizeof(int) * newLen);
         stl_groupitem = new_groupitem;

         StatusLineHilite* new_hlrec = 
             eeRealloc(statusHilitesS, sizeof(StatusLineHilite) * (newLen + 1));
         statusHilitesS = new_hlrec;
         new_hlrec = eeRealloc(stl_tabtab, sizeof(StatusLineHilite) * (newLen + 1));
         stl_tabtab = new_hlrec;

         int *new_separator_locs = eeRealloc(stl_separator_locations, sizeof(int) * newLen);
         stl_separator_locations = new_separator_locs;
         countStatusItems = newLen;
      }

      if (*s != '%')
          prevchar_isflag = prevchar_isitem = FALSE;

      // Handle up to the next '%' or the end.
      while (*s != ZERO && *s != '%' && p + 1 < out + outlen)
          *p++ = *s++;
      if (*s == ZERO || p + 1 >= out + outlen)
          break;

      // Handle one '%' item.
      s++;
      if (*s == ZERO)  // ignore trailing %
          break;
      if (*s == '%') {
         if (p + 1 >= out + outlen)
            break;
         *p++ = *s++;
         prevchar_isflag = prevchar_isitem = FALSE;
         continue;
      }
      // STL_SEPARATE: Separation between items, filled with white space.
      if (*s == STL_SEPARATE) {
         s++;
         if (groupdepth > 0)
            continue;
         stl_items[curitem].StatusTag = Separate;
         stl_items[curitem++].stl_start = p;
         continue;
      }
      if (*s == STL_TRUNCMARK) {
         s++;
         stl_items[curitem].StatusTag = Trunc;
         stl_items[curitem++].stl_start = p;
         continue;
      }
      if (*s == ')') {
         s++;
         if (groupdepth < 1)
            continue;
         groupdepth--;

         CS t = stl_items[stl_groupitem[groupdepth]].stl_start;
         *p = ZERO;
         l = eeglStrSize(t);
         if (curitem > stl_groupitem[groupdepth] + 1 
               && stl_items[stl_groupitem[groupdepth]].stl_minwid == 0
         ) {
            Short groupStartUserId = 0;
            Short groupEndHiId = 0;

            // remove group if all items are empty and hilite group doesn't change
            for (n = stl_groupitem[groupdepth] - 1; n >= 0; n--) {
               if (stl_items[n].StatusTag == Highlight) {
                  groupStartUserId = groupEndHiId = stl_items[n].stl_minwid;
                  break;
               }
            }
            for (n = stl_groupitem[groupdepth] + 1; n < curitem; n++) {
               if (stl_items[n].StatusTag == Normal)
                  break;
               if (stl_items[n].StatusTag == Highlight)
                  groupEndHiId = stl_items[n].stl_minwid;
            }
            if (n == curitem && groupStartUserId == groupEndHiId) {
               // empty group
               p = t;
               l = 0;
               for (n = stl_groupitem[groupdepth] + 1; n < curitem; n++) {
                  // do not use the hiliting from the removed group
                  if (stl_items[n].StatusTag == Highlight)
                      stl_items[n].StatusTag = Empty;
                  // adjust the start position of TabPage to the next item position
                  if (stl_items[n].StatusTag == TabPage)
                      stl_items[n].stl_start = p;
               }
            }
         }
         if (l > stl_items[stl_groupitem[groupdepth]].stl_maxwid) {
            // truncate, remove n bytes of text at the start
            // Find the first character that should be included.
            n = 0;
            while (l >= stl_items[stl_groupitem[groupdepth]].stl_maxwid) {
               l -= ptr2cells(t + n);
               n += utfCharLen(t + n);
            }

            *t = '<';
            mch_memmove(t + 1, t + n, (Unt)(p - (t + n)));
            p = p - n + 1;

            // Fill up space left over by half a double-wide char.
            while (++l < stl_items[stl_groupitem[groupdepth]].stl_minwid)
                MB_CHAR2BYTES(fillchar, p);

            // correct the start of the items for the truncation
            for (l = stl_groupitem[groupdepth] + 1; l < curitem; l++) {
               // Minus one for the leading '<' added above.
               stl_items[l].stl_start -= n - 1;
               if (stl_items[l].stl_start < t)
                  stl_items[l].stl_start = t;
            }
         } ei (abs(stl_items[stl_groupitem[groupdepth]].stl_minwid) > l) {
            // fill
            n = stl_items[stl_groupitem[groupdepth]].stl_minwid;
            if (n < 0) {
               // fill by appending characters
               n = 0 - n;
               while (l++ < n && p + 1 < out + outlen)
                  MB_CHAR2BYTES(fillchar, p);
            } else {
               // fill by inserting characters
               l = (n - l) * MB_CHAR2LEN(fillchar);
               mch_memmove(t + l, t, (Unt)(p - t));
               if (p + l >= out + outlen)
                  l = (long)((out + outlen) - p - 1);
               p += l;
               for (n = stl_groupitem[groupdepth] + 1; n < curitem; n++)
                  stl_items[n].stl_start += l;
               for ( ; l > 0; l--)
                  MB_CHAR2BYTES(fillchar, t);
            }
         }
         continue;
      }
      minwid = 0;
      maxwid = 9999;
      zeropad = FALSE;
      l = 1;
      if (*s == '0') {
         s++;
         zeropad = TRUE;
      }
      if (*s == '-') {
         s++;
         l = -1;
      }
      if (EE_ISDIGIT(*s)) {
         minwid = (int)parseLong(&s);
         if (minwid < 0)   // overflow
            minwid = 0;
      }
      if (*s == STL_USER_HL) {
         stl_items[curitem].StatusTag = Highlight;
         stl_items[curitem].stl_start = p;
         stl_items[curitem].stl_minwid = minwid > 9 ? 1 : minwid;
         s++;
         curitem++;
         continue;
      }
      if (*s == STL_TABPAGENR || *s == STL_TABCLOSENR) {
         if (*s == STL_TABCLOSENR) {
            if (minwid == 0) {
               // %X ends the close label, go back to the previously define tab label nr.
               for (n = curitem - 1; n >= 0; --n) {
                  if (stl_items[n].StatusTag == TabPage && stl_items[n].stl_minwid >= 0) {
                      minwid = stl_items[n].stl_minwid;
                      break;
                  }
               } 
            } else
               // close nrs are stored as negative values
               minwid = - minwid;
         }
         stl_items[curitem].StatusTag = TabPage;
         stl_items[curitem].stl_start = p;
         stl_items[curitem].stl_minwid = minwid;
         s++;
         curitem++;
         continue;
      }
      if (*s == '.') {
         s++;
         if (EE_ISDIGIT(*s)) {
            maxwid = (int)parseLong(&s);
         if (maxwid <= 0)   // overflow
             maxwid = 50;
         }
      }
      minwid = (minwid > 50 ? 50 : minwid) * l;
      if (*s == '(') {
         stl_groupitem[groupdepth++] = curitem;
         stl_items[curitem].StatusTag = Group;
         stl_items[curitem].stl_start = p;
         stl_items[curitem].stl_minwid = minwid;
         stl_items[curitem].stl_maxwid = maxwid;
         s++;
         curitem++;
         continue;
      }
      // Denotes end of expanded %{} block
      if (*s == '}' && evaldepth > 0) {
          s++;
          evaldepth--;
          continue;
      }
      if (firstOccurrence(STL_ALL, *s) == NULL) {
         if (*s == ZERO)  // can happen with "%0"
            break;
         s++;
         continue;
      }
      opt = *s++;

      // OK - now for the real work
      base = 'D';
      itemisflag = FALSE;
      fillable = TRUE;
      num = -1;
      str = NULL;
      switch (opt) {
      case STL_FILEPATH:
      case STL_FULLPATH:
      case STL_FILENAME: {
         fillable = FALSE;   // don't change ' ' to fillchar
         CS name = bookSpName(po->book);
         if (name)
            copySubstrToAllocation(OUT NameBuff, (Text){name, MAXPATHL - 1});
         else {
            CS t = (opt == STL_FULLPATH) ? po->book->fullFileName : po->book->currFileName;
            home_replace(po->book, t, NameBuff, MAXPATHL, TRUE);
         }
         trans_characters(NameBuff, MAXPATHL);
         if (opt != STL_FILENAME)
            str = NameBuff;
         else
            str = fiGetShortFiName(NameBuff);
         break;
      }

      case STL_EE_EXPR: { // opening curly brace
         CS block_start = s - 1;
         int       reevaluate = (*s == '%');

         if (reevaluate)
            s++;
         itemisflag = TRUE;
         CS t = p;
         while ((*s != '}' || (reevaluate && s[-1] != '%'))
                    && *s != ZERO && p + 1 < out + outlen)
         *p++ = *s++;
         if (*s != '}')   // missing '}' or out of space
            break;
         s++;
         if (reevaluate)
            p[-1] = ZERO; // remove the % at the end of %{% expr %}
         else
            *p = ZERO;
         p = t;
         eeSnprintf(buf_tmp, sizeof(buf_tmp), "%d", curBook->fiNum);
         set_internal_string_var(S"g:actual_curbuf", buf_tmp);
         eeSnprintf(buf_tmp, sizeof(buf_tmp), "%d", curPor->id);
         set_internal_string_var(S"g:actual_curPor", buf_tmp);

         Book* curBookSaved = curBook;
         Portal* save_curPor = curPor;
         save_VIsual_active = VIsual_active;
         curPor = po;
         curBook = po->book;
         // Visual mode is only valid in the current portal.
         if (curPor != save_curPor)
            VIsual_active = FALSE;

         str = eval_to_string_safe(p, false);

         curPor = save_curPor;
         curBook = curBookSaved;
         VIsual_active = save_VIsual_active;
         unletImpl(S"g:actual_curbuf", true);
         unletImpl(S"g:actual_curPor", true);

         if (str && *str != ZERO) {
            if (*skipdigits(str) == ZERO) {
                num = atoi((char *)str);
                EE_CLEAR(str);
                itemisflag = FALSE;
            }
         }

          // If the output of the expression needs to be evaluated
          // replace the %{} block with the result of evaluation
          if (reevaluate && str && *str != ZERO
             && strchr((const char *)str, '%') != NULL
             && evaldepth < MAX_STL_EVAL_DEPTH
         ) {
            Unt parsed_usefmt = (Unt)(block_start - usefmt);
            Unt str_length = strlen((const char *)str);
            Unt fmt_length = strlen((const char *)s);
            Unt new_fmt_len = parsed_usefmt + str_length + fmt_length + 3;
            CS new_fmt = (CS)alloc(new_fmt_len * sizeof(Byte));

            CS new_fmt_p = new_fmt;

            new_fmt_p = (CS)memcpy(new_fmt_p, usefmt, parsed_usefmt) + parsed_usefmt;
            new_fmt_p = (CS)memcpy(new_fmt_p , str, str_length) + str_length;
            new_fmt_p = (CS)memcpy(new_fmt_p, "%}", 2) + 2;
            new_fmt_p = (CS)memcpy(new_fmt_p , s, fmt_length) + fmt_length;
            *new_fmt_p = 0;
            new_fmt_p = NULL;

            if (usefmt != fmt)
               eeglFree(usefmt);
            EE_CLEAR(str);
            usefmt = new_fmt;
            s = usefmt + parsed_usefmt;
            evaldepth++;
            continue;
         }
         break;
      }
      case STL_LINE:
          num = (po->book->mem.flags & ML_EMPTY) ? 0L : (long)(po->cursor.lnum);
          break;

      case STL_NUMLINES:
          num = po->book->mem.lineCount;
          break;

      case STL_COLUMN:
          num = (stateG & MODE_INSERT) == 0 && empty_line
                         ? 0 : (int)po->cursor.col + 1;
          break;

      case STL_VIRTCOL:
      case STL_VIRTCOL_ALT: {
         ColNr virtcol = po->virtCol + 1;

         // Don't display %V if it's the same as %c.
         if (opt == STL_VIRTCOL_ALT
             && (virtcol == (ColNr)((stateG & MODE_INSERT) == 0
                   && empty_line ? 0 : (int)po->cursor.col + 1))
         )
            break;
         num = (long)virtcol;
         break;
      }

      case STL_PERCENTAGE:
         num = calc_percentage((long)po->cursor.lnum, (long)po->book->mem.lineCount);
         break;

      case STL_ALTPERCENT:
         str = buf_tmp;
         (void)get_rel_pos(po, str, TMPLEN);
         break;

      case STL_SHOWCMD:
         if (p_sloc == oname)
            str = showcmd_buf;
         break;

      case STL_ARGLISTSTAT:
         fillable = FALSE;
         buf_tmp[0] = ZERO;
         if (append_arg_number(po, buf_tmp, sizeof(buf_tmp), false) > 0)
            str = buf_tmp;
         break;

      case STL_KEYMAP:
         fillable = FALSE;
         if (get_keymap_str(po, S"<%s>", buf_tmp, TMPLEN) > 0)
            str = buf_tmp;
         break;
      case STL_PAGENUM:
         num = 0;
         break;

      case STL_BUFNO:
         num = po->book->fiNum;
         break;

      case STL_OFFSET_X:
         base = 'X';
         // FALLTHROUGH
      case STL_OFFSET:
         l = ml_find_line_or_offset(po->book, po->cursor.lnum, NULL);
         num = (po->book->mem.flags & ML_EMPTY) || l < 0
                ? 0L : l + 1 + ((stateG & MODE_INSERT) == 0 && empty_line
               ? 0 : (int)po->cursor.col);
         break;

      case STL_BYTEVAL_X:
         base = 'X';
         // FALLTHROUGH
      case STL_BYTEVAL:
         num = byteval;
         if (num == NL)
            num = 0;
         break;

      case STL_ROFLAG:
      case STL_ROFLAG_ALT:
         itemisflag = TRUE;
         if (!po->book->o.modifiable)
            str = (CS)((opt == STL_ROFLAG_ALT) ? S",RO" : _("[RO]"));
         break;

      case STL_HELPFLAG:
      case STL_HELPFLAG_ALT:
         itemisflag = TRUE;
         if (po->book->kind == BOOK_HELP)
            str = (CS)((opt == STL_HELPFLAG_ALT) ? S",HLP" : _("[Help]"));
         break;

      case STL_FILETYPE:
         if (*po->book->fileType != ZERO && STRLEN(po->book->fileType) < TMPLEN - 3) {
            eeSnprintf(buf_tmp, sizeof(buf_tmp), "[%s]", po->book->fileType);
            str = buf_tmp;
         }
         break;

      case STL_FILETYPE_ALT:
         itemisflag = TRUE;
         if (*po->book->fileType != ZERO && STRLEN(po->book->fileType) < TMPLEN - 2) {
            eeSnprintf(buf_tmp, sizeof(buf_tmp), ",%s", po->book->fileType);
            for (CS t = buf_tmp; *t != 0; t++)
               *t = TOUPPER_LOC(*t);
            str = buf_tmp;
         }
         break;

      case STL_PREVIEWFLAG:
      case STL_PREVIEWFLAG_ALT:
         itemisflag = TRUE;
         if (po->isPreview)
            str = (CS)((opt == STL_PREVIEWFLAG_ALT) ? S",PRV" : _("[Preview]"));
         break;

      case STL_QUICKFIX:
         if (isLocationListBook(po->book)) {
            str = (CS)(po->locationStackRef ? _(msg_loclist) : _(msg_qflist));
         } 
         break;

      case STL_MODIFIED:
      case STL_MODIFIED_ALT:
         itemisflag = TRUE;
         switch (
               (opt == STL_MODIFIED_ALT) + doWasBookChanged(po->book) * 2 
                + (!po->book->o.modifiable) * 4
         ) {
         case 2: str = S"[+]"; break;
         case 3: str = S",+"; break;
         case 4: str = S"[-]"; break;
         case 5: str = S",-"; break;
         case 6: str = S"[+-]"; break;
         case 7: str = S",+-"; break;
         }
         break;

      case STL_HIGHLIGHT: {
         CS t = s;

         while (*s != '#' && *s != ZERO)
            ++s;
         if (*s == '#') {
            stl_items[curitem].StatusTag = Highlight;
            stl_items[curitem].stl_start = p;
            stl_items[curitem].stl_minwid = -syntaxClusterByName((Text){.c = t, .len = s - t});
            curitem++;
         }
         if (*s != ZERO)
            ++s;
         continue;
      }
      }

      stl_items[curitem].stl_start = p;
      stl_items[curitem].StatusTag = Normal;
      if (str && *str) {
         CS t = str;

         if (itemisflag) {
            if ((t[0] && t[1])
                  && ((!prevchar_isitem && *t == ',') || (prevchar_isflag && *t == ' '))
            )
               t++;
            prevchar_isflag = TRUE;
         }
         l = eeglStrSize(t);
         if (l > 0)
            prevchar_isitem = TRUE;
         if (l > maxwid) {
            while (l >= maxwid)
               l -= ptr2cells(t);
            t += utfCharLen(t);
            if (p + 1 >= out + outlen)
                break;
            *p++ = '<';
         }
         if (minwid > 0) {
            for (; l < minwid && p + 1 < out + outlen; l++) {
               // Don't put a "-" in front of a digit.
               if (l + 1 == minwid && fillchar == '-' && EE_ISDIGIT(*t))
                  *p++ = ' ';
               else
                  MB_CHAR2BYTES(fillchar, p);
            }
            minwid = 0;
         } else
            minwid *= -1;
         for (; *t && p + 1 < out + outlen; t++) {
            // Change a space by fillchar, unless fillchar is '-' and a digit follows.
            if (fillable && *t == ' ' && (!EE_ISDIGIT(*(t + 1)) || fillchar != '-'))
               MB_CHAR2BYTES(fillchar, p);
            else
               *p++ = *t;
         }
         for (; l < minwid && p + 1 < out + outlen; l++)
            MB_CHAR2BYTES(fillchar, p);
      } ei (num >= 0) {
         int nbase = (base == 'D' ? 10 : (base == 'O' ? 8 : 16));
         Byte nstr[20];
         CS t = nstr;

         if (p + 20 >= out + outlen)
            break;      // not sufficient space
         prevchar_isitem = TRUE;
         if (opt == STL_VIRTCOL_ALT) {
            *t++ = '-';
            minwid--;
         }
         *t++ = '%';
         if (zeropad)
            *t++ = '0';
         *t++ = '*';
         *t++ = nbase == 16 ? base : (Byte)(nbase == 8 ? 'o' : 'd');
         *t = ZERO;

         for (n = num, l = 1; n >= nbase; n /= nbase)
            l++;
         if (opt == STL_VIRTCOL_ALT)
            l++;

         if (l > maxwid) {
            l += 2;
            n = l - maxwid;
            while (l-- > maxwid)
               num /= nbase;
            *t++ = '>';
            *t++ = '%';
            *t = t[-3];
            *++t = ZERO;
            p += eeSnprintfSafelen(p, outlen - (p - out), (char *)nstr, 0, num, n);
         }
         else
            p += eeSnprintfSafelen(p, outlen - (p - out), (char *)nstr, minwid, num);
      } else
         stl_items[curitem].StatusTag = Empty;

      if (num >= 0 || (!itemisflag && str && *str != ZERO))
         prevchar_isflag = FALSE;       // Item not NULL, but not a flag
      if (opt == STL_EE_EXPR)
         eeglFree(str);
      curitem++;
   }
   *p = ZERO;
   outputlen = (Unt)(p - out);
   itemcnt = curitem;

   if (usefmt != fmt)
      eeglFree(usefmt);

   width = eeglStrSize(out);
   if (maxwidth > 0 && width > maxwidth) {
      //Result is too long, must truncate somewhere.
      l = 0;
      if (itemcnt == 0)
         s = out;
      else {
         for ( ; l < itemcnt; l++) {
            if (stl_items[l].StatusTag == Trunc) {
               //Truncate at %< item.
               s = stl_items[l].stl_start;
               break;
            }
         } 
         if (l == itemcnt) {
            //No %< item, truncate first item.
            s = stl_items[0].stl_start;
            l = 0;
         }
      }

      if (width - eeglStrSize(s) >= maxwidth) {
         //Truncation mark is beyond max length
         s = out;
         width = 0;
         for (;;) {
            width += ptr2cells(s);
            if (width >= maxwidth)
               break;
            s += utfCharLen(s);
         }
         // Fill up for half a double-wide character.
         while (++width < maxwidth)
            MB_CHAR2BYTES(fillchar, s);
         for (l = 0; l < itemcnt; l++) {
            if (stl_items[l].stl_start > s) {
               break;
            } 
         } 
         itemcnt = l;
         *s++ = '>';
         *s = ZERO;
      } else {
         CS end = out + outputlen;

         n = 0;
         while (width >= maxwidth) {
            width -= ptr2cells(s + n);
            n += utfCharLen(s + n);
         }
         p = s + n;
         mch_memmove(s + 1, p, (Unt)(end - p) + 1);   // +1 for ZERO
         end -= (Unt)(p - (s + 1));
         *s = '<';

         --n;   // count the '<'
         for (; l < itemcnt; l++) {
            if (stl_items[l].stl_start - n >= s)
               stl_items[l].stl_start -= n;
            else
               stl_items[l].stl_start = s;
         }

         // Fill up for half a double-wide character.
         while (++width < maxwidth) {
            s = end;
            MB_CHAR2BYTES(fillchar, s);
            *s = ZERO;
            end = s;
         }
      }
      width = maxwidth;
   } ei (width < maxwidth && outputlen + maxwidth - width + 1 < outlen) {
      //Find how many separators there are, which we will use when
      //figuring out how many groups there are.
      int num_separators = 0;

      for (l = 0; l < itemcnt; l++) {
         if (stl_items[l].StatusTag == Separate) {
            // Create an array of the start location for each separator mark.
            stl_separator_locations[num_separators] = l;
            num_separators++;
         }
      }

      // If we have separated groups, then we deal with it now
      if (num_separators) {
         int standard_spaces = (maxwidth - width) / num_separators;
         int final_spaces = (maxwidth - width) - standard_spaces * (num_separators - 1);
         for (l = 0; l < num_separators; l++) {
            int dislocation = (l == (num_separators - 1)) ? final_spaces : standard_spaces;
            dislocation *= MB_CHAR2LEN(fillchar);
            CS start = stl_items[stl_separator_locations[l]].stl_start;
            CS seploc = start + dislocation;
            STRMOVE(seploc, start);
            for (s = start; s < seploc;)
               MB_CHAR2BYTES(fillchar, s);

            for (int i = stl_separator_locations[l] + 1; i < itemcnt; i++)
               stl_items[i].stl_start += dislocation;
          }

          width = maxwidth;
      }
   }

   // Store the info about hiliting.
   if (hltab) {
      *hltab = statusHilitesS;
      sp = statusHilitesS;
      for (l = 0; l < itemcnt; l++) {
         if (stl_items[l].StatusTag == Highlight) {
            sp->start = stl_items[l].stl_start;
            sp->hiId = stl_items[l].stl_minwid;
            sp++;
         }
      }
      sp->start = NULL;
      sp->hiId = 0;
   }

   // Store the info about tab labels.
   if (labels) {
      *labels = stl_tabtab;
      sp = stl_tabtab;
      for (l = 0; l < itemcnt; l++) {
         if (stl_items[l].StatusTag == TabPage) {
            sp->start = stl_items[l].stl_start;
            sp->hiId = stl_items[l].stl_minwid;
            sp++;
         }
      }
      sp->start = NULL;
      sp->hiId = 0;
   }

   // A user function may reset vars, restore them
   redraw_not_allowed = save_redraw_not_allowed;
   KeyTyped = save_KeyTyped;

   //Check for an error.  If there is one the display will be messed up and
   //might loop redrawing.  Avoid that by making the corresponding option empty.
   //TODO: find out why using called_emsg_before makes tests fail, does it matter?
   //if (called_emsg > called_emsg_before)
   if (anyEmsgG > anyEmsgSaved) {
      CS optionName = oname == STATLINE_TABPANEL 
         ? S"tabpanel"
         : (oname == STATLINE_STATUSLINE)
            ? S"statusline"
            : S"rulerformat";
      optChangeStringOptionDirect(optionName, S"", opt_scope, SID_ERROR);
   } 

   return width;
}

// Get relative cursor position in portal into "buf[]", in the localized
// percentage form like %99, 99%; using "Top", "Bot" or "All" when appropriate.
int
get_rel_pos(Portal* po, CS buf, int buflen){
   long   above; // number of lines above portal
   long   below; // number of lines below portal

   if (buflen < 3) // need at least 3 chars for writing
      return 0;
   above = po->topLine - 1;
   above += diff_check_fill(po, po->topLine) - po->topFill;
   if (po->topLine == 1 && po->topFill >= 1)
      above = 0; // All book lines are displayed and there is an
                 // indication of filler lines, that can be considered seeing all lines.
   below = po->book->mem.lineCount - po->bottomLine + 1;
   if (below <= 0)
      return (int)eeSnprintfSafelen(buf, buflen,
          "%s", (above == 0) ? _("All") : _("Bot"));

   if (above <= 0)
      return (int)eeSnprintfSafelen(buf, buflen,
          "%s", _("Top"));

   int perc = calc_percentage(above, above + below);
   Byte tmp[8];
   // localized percentage value
   eeSnprintf(tmp, sizeof(tmp), _("%d%%"), perc);
   return (int)eeSnprintfSafelen(buf, buflen, _("%2s"), tmp);
}

// Append (file 2 of 8) to "buf[]", if editing more than one file. Return the number of appended 
// characters
private int
append_arg_number(
   Portal* po,
   CS buf,
   Unt buflen,
   Boole add_file   // Add "file" before the arg number
){
   if (ARGCOUNT <= 1)      // nothing to do
      return 0;

   CS msg;
   switch ((po->isNotValid ? 1 : 0) + (add_file ? 2 : 0)) {
   case 0: msg = _(" (%d of %d)"); break;
   case 1: msg = _(" ((%d) of %d)"); break;
   case 2: msg = _(" (file %d of %d)"); break;
   case 3: msg = _(" (file (%d) of %d)"); break;
   }

   return (int)eeSnprintfSafelen(buf, buflen, msg, po->argListInd + 1, ARGCOUNT);
}

// Make "*fullFName" a full file name, set "*sfname" to "*fullFName" if not NULL.
// "*fullFName" becomes a pointer to allocated memory (or NULL).
// When resolving a link, both "*sfname" and "*fullFName" will point to the same
// allocated memory.
// The "*fullFName" and "*sfname" pointer values on call will not be freed.
// Note that the resulting "*fullFName" pointer should be considered not allocated.
void
fname_expand(CS* fullFName, CS* sfname){
   if (*fullFName == NULL)       // no file name given, nothing to do
      return;
   if (*sfname == NULL)       // no short file name given, use fullFName
      *sfname = *fullFName;
   *fullFName = fiExpandAndCopy(*fullFName, true);   // expand to full path
}

// Open a portal for a number of books.
void
c_bookAll(Invocation* invo) {
   Book* book;
   Portal *po, *wpnext;
   int split_ret = OK;
   int p_ea_save;
   int open_wins = 0;
   int count;      // Maximum number of portals to open.
   int all;      // When TRUE also load inactive books.
   int had_tab = commModifierG.cmod_tab;
   Tab* tNext;

   if (invo->addr_count == 0)   // make as many portals as possible
      count = 9999;
   else
      count = invo->line2;   // make as many portals as specified
   if (invo->id == C_unhide || invo->id == C_sunhide)
      all = FALSE;
   else
      all = TRUE;

   //Stop Visual mode, the cursor and "VIsual" may very well be invalid after
   //switching to another book.
   reset_VIsual_and_resel();

   setpcmark();

   // Close superfluous portals (two portals into the same book).
   // Also close portals that are not full-width.
   if (had_tab > 0)
      gotoTab(firstTabG, TRUE, TRUE);
   for (;;) {
      tNext = curtab->next;
      for (po = firstPor; po; po = wpnext) {
         wpnext = po->next;
         if ((po->book->countPortals > 1
            || ((commModifierG.cmod_split & WSP_VERT)
                ? (po->height + po->statusHeight < visibleRowsG - commlineHeightG )
                : po->width != visibleColsG)
            || (had_tab > 0 && po != firstPor))
             && !ONLY_ONE_PORTAL
             && !(portalLocked(po) || po->book->locked > 0)
             && !portUnlisted(po)
         ){
            if (closePortal(po, FALSE) == FAIL)
               break;
            // Just in case an autocommand does something strange with portals: start all over...
            wpnext = firstPor;
            tNext = firstTabG;
            open_wins = 0;
         } else
            ++open_wins;
      }

      // Without the ":tab" modifier only do the current tab
      if (had_tab == 0 || tNext == NULL)
         break;
      gotoTab(tNext, TRUE, TRUE);
    }

   //Go through the book list.  When a book doesn't have a portal yet,
   //open one.  Otherwise move the portal to the right position.
   //Watch out for autocommands that delete books or portals!
   //Don't execute Win/Buf Enter/Leave autocommands here.
   ++autocmd_no_enter;
   enterPortal(lastPor, FALSE);
   ++autocmd_no_leave;
   for (book = firstBook; book && open_wins < count; book = book->next) {
      // Check if this book needs a portal
      if ((!all && book->mem.mfile == NULL) || !book->o.bookListed)
         continue;

      if (had_tab != 0) {
         // With the ":tab" modifier don't move the portal.
         if (book->countPortals > 0)
            po = lastPor;       // book has a portal, skip it
         else
            po = NULL;
      } else {
         // Check if this book already has a portal
         FOR_ALL_PORTALS(po) {
            if (po->book == book)
               break;
         } 
         // If the book already has a portal, move it
         if (po)
            portMoveAfter(po, curPor);
      }

      if (!po && split_ret == OK) {
         BookRef bookRef;

         bookStoreInRef(OUT &bookRef, book);

         // Split the portal and put the book in it
         p_ea_save = p_ea;
         p_ea = TRUE;      // use space from all portals
         split_ret = splitPortal(0, WSP_ROOM | WSP_BELOW);
         ++open_wins;
         p_ea = p_ea_save;
         if (split_ret == FAIL)
            continue;

         // Open this portal into the book
         swap_exists_action = SEA_DIALOG;
         bookSetCurBook(book, DOBOOK_GOTO);
         if (!bookRefValid(&bookRef)) {
            // autocommands deleted the book!!!
            swap_exists_action = SEA_NONE;
            break;
         }
         if (swap_exists_action == SEA_QUIT) {
            Cleanup   cs;

            //Reset the error/interrupt/exception state here so that
            //aborting() returns FALSE when closing a portal.
            enter_cleanup(&cs);

            // User selected Quit at ATTENTION prompt; close this portal.
            closePortal(curPor, TRUE);
            --open_wins;
            swap_exists_action = SEA_NONE;
            swap_exists_did_quit = TRUE;

            // Restore the error/interrupt/exception state if not
            // discarded by a new aborting error, interrupt, or uncaught
            // exception.
            leave_cleanup(&cs);
         } else
            handle_swap_exists(NULL);
      }

      ui_breakcheck();
      if (gotInterruptG) {
         (void)vgetc();   // only break the file loading, not the rest
         break;
      }
      //Autocommands deleted the book or aborted script processing!!!
      if (aborting())
         break;
      //When ":tab" was used open a new tab for a new portal repeatedly.
      if (had_tab > 0)
         commModifierG.cmod_tab = 9999;
   }
   --autocmd_no_enter;
   enterPortal(firstPor, FALSE);      // back to first portal
   --autocmd_no_leave;

   // Close superfluous portals.
   for (po = lastPor; open_wins > count; ) {
      if (!portalIsValid(po)) {
         // BufWrite Autocommands made the portal invalid, start over
         po = lastPor;
      } else {
         closePortal(po, FALSE);
         --open_wins;
         po = lastPor;
      }
   }
}

// Return TRUE if "book" is a normal book
int
bt_normal(Book* book) {
   return book && book->kind == BOOK_NORMAL;
}

// Return TRUE if "book" is the location list book.
Boole
isLocationListBook(Book* book) {
   return book && bookIsValid(book) && book->kind == BOOK_LOCATION;
}

// Return TRUE if "book" is a terminal book.
int
bt_terminal(Book* book) {
   return book && book->kind == BOOK_TERMINAL;
}

// Return TRUE if "book" is a help book.
int
bookIsHelp(Book* book) {
   return book && book->kind == BOOK_HELP;
}

// Return TRUE if "book" is a prompt book.
int
bt_prompt(Book* book) {
   return book && book->kind == BOOK_PROMPT;
}

// Return TRUE if "book" is a book for a popup portal.
int
bt_popup(Book* book) {
   return book && book->kind == BOOK_POPUP;
}

//Return TRUE if "book" is a "nofile", "acwrite", "terminal" or "prompt"
//book. This means the book name may not be a file name, at least not for writing the book.
int
bt_nofilename(Book* book) {
   return book && (book->kind == BOOK_NOFILE
       || book->kind == BOOK_ACWRITE
       || book->kind == BOOK_TERMINAL
       || book->kind == BOOK_PROMPT
       );
}

// Return TRUE if "book" is a "nofile", "quickfix", "terminal" or "prompt"
// book. This means the book is not to be read from a file.
private int
bt_nofileread(Book* book) {
   return book && (book->kind == BOOK_NOFILE 
       || book->kind == BOOK_TERMINAL
       || book->kind == BOOK_LOCATION
       || book->kind == BOOK_PROMPT
       );
}

// Return TRUE if "book" has 'buftype' set to "nofile".
int
bt_nofile(Book* book) {
   return book && book->kind == BOOK_NOFILE;
}

// Return TRUE if "book" is a "nowrite", "nofile", "terminal", "prompt", or "popup" book.
int
bt_dontwrite(Book* book) {
    return book && (book->kind == BOOK_NOWRITE
       || book->kind == BOOK_NOFILE
       || book->kind == BOOK_TERMINAL
       || book->kind == BOOK_PROMPT
       || book->kind == BOOK_POPUP
       );
}

int
bt_dontwrite_msg(Book* book) {
   if (bt_dontwrite(book)) {
      emsg(_(e_cannot_write_buftype_option_is_set));
      return TRUE;
   }
   return FALSE;
}

// Return special book name. Returns NULL when the book has a normal file name.
CS
bookSpName(Book* book) {
   if (isLocationListBook(book)) {
      // Differentiate between the quickfix and location list books using
      // the book number stored in the global quickfix stack.
      if (book->fiNum == qf_stack_get_bufnr())
          return (CS)_(msg_qflist);
      else
          return (CS)_(msg_loclist);
   }

   // There is no _file_ when 'buftype' is "nofile", shortFileName
   // contains the name as specified by the user.
   if (bt_nofilename(book)) {
      if (book->term)
          return term_get_status_text(book->term);
      if (book->currFileName)
          return book->currFileName;
      if (book == commPortBookG)
          return (CS)_("[Command Line]");
      if (bt_prompt(book))
          return (CS)_("[Prompt]");
      if (bt_popup(book))
          return (CS)_("[Popup]");
      return (CS)_("[Scratch]");
   }

   if (book->currFileName == NULL)
      return bookGetFname(book);
   return NULL;
}

//Get "book->currFileName", use "[No Name]" if it is NULL.
CS
bookGetFname(Book* book) {
   if (book->currFileName == NULL)
      return (CS)_("[No Name]");
   return book->currFileName;
}

//Set 'buflisted' for curBook to "on" and trigger autocommands if it changed.
void
bookSetBooklisted(Boole on) {
   if (on == curBook->o.bookListed)
      return;
   curBook->o.bookListed = on;
   if (on)
      applyAutocomms(EVENT_BUFADD, NULL, NULL, false, curBook);
   else
      applyAutocomms(EVENT_BUFDELETE, NULL, NULL, false, curBook);
}

//Read the file for "book" again and check if the contents changed.
//Return TRUE if it changed or this could not be checked.
int
buf_contents_changed(Book* book){
   Boole differ = true;
   LineNr lnum;
   AutocommSave aco;
   Invocation invo;

   // Allocate a book without putting it in the book list.
   Book* new = bookNew(NULL, NULL, (LineNr)1, BLN_DUMMY);
   if (!new)
      return TRUE;

   // Force the 'binary' option to be equal.
   if (prep_exarg(&invo, book) == FAIL) {
      bookWipe(new, false);
      return true;
   }

   // Set curPor/curBook to book and save a few things.
   auCommPrepareBook(&aco, new);
   if (curBook != new) {
      // Failed to find a portal for "new".
      bookWipe(new, FALSE);
      return true;
   }

   //We don't want to trigger autocommands now, they may have nasty side-effects like wiping books
   block_autocmds();
   if (ml_open(curBook) == OK
       && readfile(
             book->fullFileName, book->currFileName, (LineNr)0, (LineNr)0, (LineNr)MAXLNUM,
             &invo, READ_NEW | READ_DUMMY
          ) == OK
   ) {
      // compare the two files line by line
      if (book->mem.lineCount == curBook->mem.lineCount) {
         differ = FALSE;
         for (lnum = 1; lnum <= curBook->mem.lineCount; ++lnum) {
            if (STRCMP(memGetLine(book, lnum, FALSE), ml_get(lnum)) != 0) {
                differ = TRUE;
                break;
            }
         } 
      }
   }
   eeglFree(invo.comm);

   // restore curPor/curBook and a few other things
   auCommRestoreBook(&aco);

   if (curBook != new)   // safety check
      bookWipe(new, FALSE);

   unblock_autocmds();

   return differ;
}

// Wipe out a book and decrement the last book number if it was used for
// this book.  Call this to wipe out a temp book that does not contain any marks.
void
bookWipe(Book* book, int aucmd) { // When TRUE, trigger autocommands.
   if (book->fiNum == top_file_num - 1)
      --top_file_num;

   if (!aucmd)          // Don't trigger BufDelete autocommands here.
      block_autocmds();

   closeBook(NULL, book, DOBOOK_WIPE, FALSE, TRUE);

   if (!aucmd)
      unblock_autocmds();
}

//Output a string for the version message.  If it's going to wrap, output a
//newline, unless the message is too long to fit on the screen anyway.
void
printMsgWithWrap(CS s) {
   int len = eeglStrSize(s);

   if (!gotInterruptG && len < (int)visibleColsG 
         && msgColG + len >= (int)visibleColsG && *s != '\n'
   )
      msg_putchar('\n');
   if (!gotInterruptG) {
      msg_puts(s);
   }
}

//List string items nicely aligned in columns. When "size" is < 0 then the last entry is marked 
//with NULL. The entry with index "current" is inclosed in [].
void
listInColumns(Arr(CS) items, int size, int current, Boole useHilite) {
   Unt cur_row = 1;
   Unt itemCount = 0;
   int width = 0;

   // Find the length of the longest item, use that + 1 as the column width.
   for (int i = 0; size < 0 ? items[i] != NULL : i < size; ++i) {
      int l = eeglStrSize(items[i]) + (i == current ? 2 : 0);

      if (l > width)
         width = l;
      ++itemCount;
   }
   width += 1;

   if (visibleColsG < width) {
      // Not enough screen columns - show one per line
      for (Unt i = 0; i < itemCount; ++i) {
         printMsgWithWrap(items[i]);
         if (msgColG > 0 && i + 1 < itemCount)
            msg_putchar('\n');
      }
      return;
   }

   //The rightmost column doesn't need a separator.
   //Sacrifice it to fit in one more column if possible.
   Unt ncol = (Unt) (visibleColsG + 1) / width;
   Unt nrow = itemCount / ncol + ((itemCount % ncol) ? 1 : 0);

   //"i" counts columns then rows. "idx" counts rows then columns.
   for (Unt i = 0; !gotInterruptG && i < nrow * ncol; ++i) {
      Unt idx = (i / ncol) + (i % ncol) * nrow;
      if (idx < itemCount) {
         int last_col = (i + 1) % ncol == 0;

         if (idx == (Unt)current)
            msg_putchar('[');
         if (useHilite && items[idx][0] == '-')
            msgPutsDeco(items[idx], getDecoFlags(HLF_W));
         else
            msg_puts(items[idx]);
         if (idx == (Unt)current)
            msg_putchar(']');
         if (last_col) {
            if (msgColG > 0 && cur_row < nrow)
               msg_putchar('\n');
            ++cur_row;
         } else {
            while (msgColG % width)
               msg_putchar(' ');
         }
      } else {
         // this row is out of items, thus at the end of the row
         if (msgColG > 0) {
            if (cur_row < nrow)
               msg_putchar('\n');
            ++cur_row;
         }
      }
   }
}

//Compare functions for qsort() below, that compares lastUsed.
int
bookCompare(const void* s0, const void* s1) {
   Book *book0 = *(Book **)s0;
   Book *book1 = *(Book **)s1;

   if (book0->lastUsed == book1->lastUsed)
      return 0;
   return book0->lastUsed > book1->lastUsed ? -1 : 1;
}


//{{{bookwrite: functions for writing a book

#include <fcntl.h>      // Definition of AT_* constants for utimensat()
#include <sys/stat.h>   // for utimensat(), modification time changin'

#define SMALLBUFSIZE   256   // size of emergency write book

// Structure to pass arguments from bookWrite() to writeBytes().
typedef struct {
   int      fd;      // file descriptor
   CS bw_buf;   // buffer with data to be written
   int      bw_len;      // length of data
   Book   *tgt;   // book being written
   int      bw_first;   // first write call
   LineNr   bw_start_lnum;   // line number at start of book
} BwInfo;

//Call write() to write a number of bytes to the file.
//Return FAIL for failure, OK otherwise.
private int
writeBytes(BwInfo* ip) {
   CS buf = ip->bw_buf;   // data to write
   int      len = ip->bw_len;   // length of data

   if (ip->fd < 0)
      // Only checking conversion, which is OK if we get here.
      return OK;

   int wlen = write_eintr(ip->fd, buf, len);
   return (wlen < len) ? FAIL : OK;
}

//Check modification time of file, before writing to it. The size isn't checked, because using 
//a tool like "gzip" takes care of using the same timestamp but can't set the size.
private int
check_mtime(Book* book, FileStat *st) {
   if (book->readTime != 0
        && time_differs(st, book->readTime, book->readTimeNs)
   ) {
      msg_scroll = TRUE;       // don't overwrite messages here
      msg_silent = 0;          // must give this prompt
      // don't use emsg() here, don't want to flush the books
      msgDeco(_("WARNING: The file has been changed since reading it!!!"),
                            getDecoFlags(HLF_E)
      );
      if (ask_yesno((CS)_("Do you really want to write to it"), TRUE) == 'n')
         return FAIL;
      msg_scroll = FALSE;       // always overwrite the file message now
    }
    return OK;
}

private void
set_file_time(
   CS fname,
   Tyme  atime,       // access time
   Tyme  mtime)       // modification time
{
   ProfTime newTimes[2];
   newTimes[0] = (ProfTime){.tv_sec = atime, .tv_nsec = 0};
   newTimes[1] = (ProfTime){.tv_sec = mtime, .tv_nsec = 0};

   // first arg is 0 because the path is absolute. Last arg is just absence of flags
   utimensat(0, (char*)fname, newTimes, 0);
}

CS
new_file_message(void) {
   return shortmess(SHM_NEW) ? _("[New]") : _("[New File]");
}

//bookWrite() - write to file "fname" lines "start" through "end"
//
//We do our own buffering here because fwrite() is so slow.
//
//If "forceit" is true, we don't care for errors when attempting backups.
//In case of an error everything possible is done to restore the original
//file.  But when "forceit" is TRUE, we risk losing it.
//
//When "reset_changed" is TRUE and "append" == FALSE and "start" == 1 and
//"end" == curBook->mem.lineCount, reset curBook->wasModified.
//
//This function must NOT use NameBuff (because it's called by autowrite()).
//
//return FAIL for failure, OK otherwise
int
bookWrite(
   Book* book,
   CS fname,
   CS sfname,
   LineNr start,
   LineNr end,
   Invocation* invo,      // for forced 'ff', can be NULL!
   int append,      // append to the file
   int forceit,
   int reset_changed,
   int filtering
) {
   int fd;
   CS backup = NULL;
   int  backup_copy = FALSE; // copy the original file?
   CS s;
   CS ptr;
   Byte c;
   int len;
   LineNr lnum;
   long nchars;
   CS errmsg = NULL;
   int          errmsg_allocated = FALSE;
   CS errnum = NULL;
   CS buffer;
   Byte smallbuf[SMALLBUFSIZE];
   CS backup_ext;
   int bufsize;
   long perm;          // file permissions
   int retval = OK;
   int newfile = FALSE;       // TRUE if file doesn't exist yet
   int msg_save = msg_scroll;
   int overwriting;       // TRUE if writing over original
   int no_eol = FALSE;       // no end-of-line written
   int device = FALSE;       // writing to a device
   int          prev_gotInterruptG = gotInterruptG;
   int          file_readonly = FALSE;  // overwritten file is read-only
   int          made_writable = FALSE;  // 'w' bit has been set
               // writing everything
   Boole whole = (start == 1 && end == book->mem.lineCount);
   LineNr       old_line_count = book->mem.lineCount;
   char flags; // decoration flags
   int          write_bin;
   ContextSha256 sha_ctx;
   Unt bkc = book->o.backupCopy;
   Pos       orig_start = book->opStart;
   Pos       orig_end = book->opEnd;

   if (!fname || *fname == ZERO)   // safety check
      return FAIL;
   if (!book->mem.mfile) {
      // This can happen during startup when there is a stray "w" in the vimrc file.
      emsg(_(e_empty_buffer));
      return FAIL;
   }

   // Avoid a crash for a long name.
   if (STRLEN(fname) >= MAXPATHL) {
      emsg(_(e_name_too_long));
      return FAIL;
   }

   BwInfo writeInfo = (BwInfo) { // for writeBytes()
      .tgt = book
   }; 

   // After writing a file changedtick changes but we don't want to display the line.
   ex_no_reprint = TRUE;

   // If there is no file name yet, use the one for the written file.
   // BF_NOTEDITED is set to reflect this (in case the write fails).
   // Don't do this when the write is for a filter command.
   // Don't do this when appending.
   if (book->fullFileName == NULL
       && reset_changed
       && whole
       && book == curBook
       && !bt_nofilename(book)
       && !filtering
       && !append
   ){
      if (set_rw_fname(fname, sfname) == FAIL)
         return FAIL;
      book = curBook;       // just in case autocmds made "book" invalid
    }

   if (!sfname)
      sfname = fname;
   // Use the short file name whenever possible.
   // Avoids problems with networks and when directory names are changed.
   CS fullFName = fname;             // remember full fname
   fname = sfname;

   if (book->fullFileName && fnamecmp(fullFName, book->fullFileName) == 0)
      overwriting = TRUE;
   else
      overwriting = FALSE;

   if (exiting)
      termSetMode(TMODE_COOK);       // when exiting allow typeahead now

   ++no_wait_return;          // don't wait for return yet

   // Set '[ and '] marks to the lines to be written.
   book->opStart.lnum = start;
   book->opStart.col = 0;
   book->opEnd.lnum = end;
   book->opEnd.col = 0;

   {
   AutocommSave   aco;
   int      buf_ffname = FALSE;
   int      buf_sfname = FALSE;
   int      buf_fname_f = FALSE;
   int      buf_fname_s = FALSE;
   int      did_cmd = FALSE;
   Boole nofile_err = false;
   int      empty_memline = (book->mem.mfile == NULL);
   BookRef   bookRef;

   // Apply PRE autocommands. Set curBook to the book to be written.
   // Careful: The autocommands may call bookWrite() recursively!
   if (fullFName == book->fullFileName)
      buf_ffname = TRUE;
   if (sfname == book->shortFileName)
      buf_sfname = TRUE;
   if (fname == book->fullFileName)
      buf_fname_f = TRUE;
   if (fname == book->shortFileName)
      buf_fname_s = TRUE;

   // Set curPor/curBook to book and save a few things.
   auCommPrepareBook(&aco, book);
   if (curBook != book) {
      // Could not find a portal for "book".  Doing more might cause problems, better bail out.
      return FAIL;
   }

   bookStoreInRef(OUT &bookRef, book);

   if (append) {
      if (!(did_cmd = 
               auCommApplyWithInvo(EVENT_FILEAPPENDCMD, sfname, sfname, FALSE, curBook, invo))) {
         if (overwriting && bt_nofilename(curBook))
            nofile_err = true;
         else
            auCommApplyWithInvo(EVENT_FILEAPPENDPRE, sfname, sfname, FALSE, curBook, invo);
      }
   } ei (filtering) {
       auCommApplyWithInvo(EVENT_FILTERWRITEPRE, NULL, sfname, FALSE, curBook, invo);
   } ei (reset_changed && whole) {
      int was_changed = doWasCurBookChanged();

      did_cmd = auCommApplyWithInvo(EVENT_BUFWRITECMD, sfname, sfname, FALSE, curBook, invo);
      if (did_cmd) {
         if (was_changed && !doWasCurBookChanged()) {
             // Written everything correctly and BufWriteCmd has reset
             // 'modified': Correct the undo information so that an
             // undo now sets 'modified'.
             u_unchanged(curBook);
             u_update_save_nr(curBook);
         }
      } else {
         if (overwriting && bt_nofilename(curBook))
            nofile_err = true;
         else
            auCommApplyWithInvo(EVENT_BUFWRITEPRE, sfname, sfname, FALSE, curBook, invo);
      }
   } else {
      if (!(did_cmd = 
                auCommApplyWithInvo(EVENT_FILEWRITECMD, sfname, sfname, FALSE, curBook, invo))
      ) {
         if (overwriting && bt_nofilename(curBook))
            nofile_err = true;
         else
            auCommApplyWithInvo(EVENT_FILEWRITEPRE, sfname, sfname, FALSE, curBook, invo);
      }
   }

   // restore curPor/curBook and a few other things
   auCommRestoreBook(&aco);

   // In three situations we return here and don't write the file:
   // 1. the autocommands deleted or unloaded the book.
   // 2. The autocommands abort script processing.
   // 3. If one of the "Cmd" autocommands was executed.
   if (!bookRefValid(&bookRef))
      book = NULL;
   if (!book || (!book->mem.mfile && !empty_memline)
            || did_cmd || nofile_err
            || aborting()
   ){
      if (book && (commModifierG.cmod_flags & CMOD_LOCKMARKS)) {
         // restore the original '[ and '] positions
         book->opStart = orig_start;
         book->opEnd = orig_end;
      }

      --no_wait_return;
      msg_scroll = msg_save;
      if (nofile_err) {
         showErrFmtMsg(
            _(e_no_matching_autocommands_for_buftype_str_buffer), p_buftype_values[book->kind]
         );
      } 

      if (nofile_err || aborting() )
         // An aborting error, interrupt or exception in the autocommands.
         return FAIL;
      if (did_cmd) {
         if (!book)
            // The book was deleted.  We assume it was written (can't retry anyway).
            return OK;
         if (overwriting) {
            // Assume the book was written, update the timestamp.
            ml_timestamp(book);
            if (append)
               book->flags &= ~BF_NEW;
            else
               book->flags &= ~BF_WRITE_MASK;
         }
         if (reset_changed && book->wasModified && !append && overwriting)
            // Book still changed, the autocommands didn't work properly.
            return FAIL;
         return OK;
      }
      if (!aborting())
         emsg(_(e_autocommands_deleted_or_unloaded_buffer_to_be_written));
      return FAIL;
   }

   // The autocommands may have changed the number of lines in the file.
   // When writing the whole file, adjust the end.
   // When writing part of the file, assume that the autocommands only
   // changed the number of lines that are to be written (tricky!).
   if (book->mem.lineCount != old_line_count) {
      if (whole)                  // write all
         end = book->mem.lineCount;
      ei (book->mem.lineCount > old_line_count)   // more lines
         end += book->mem.lineCount - old_line_count;
      else {                // less lines
         end -= old_line_count - book->mem.lineCount;
         if (end < start) {
             --no_wait_return;
             msg_scroll = msg_save;
             emsg(_(e_autocommands_changed_number_of_lines_in_unexpected_way));
             return FAIL;
         }
      }
   }

   // The autocommands may have changed the name of the book, which may
   // be kept in fname, fullFName and sfname.
   if (buf_ffname)
      fullFName = book->fullFileName;
   if (buf_sfname)
      sfname = book->shortFileName;
   if (buf_fname_f)
      fname = book->fullFileName;
   if (buf_fname_s)
      fname = book->shortFileName;
   }

   if (commModifierG.cmod_flags & CMOD_LOCKMARKS) {
      // restore the original '[ and '] positions
      book->opStart = orig_start;
      book->opEnd = orig_end;
   }

   if (shortmess(SHM_OVER) && !exiting)
      msg_scroll = FALSE;       // overwrite previous file message
   else
      msg_scroll = TRUE;       // don't overwrite previous file message
   if (!filtering)
      filemess(book, fname, Em, 0);   // show that we are busy
   msg_scroll = FALSE;          // always overwrite the file message now

   buffer = tryBigAlloc(WRITEBUFSIZE);
   if (buffer == NULL) { // can't allocate big buffer, use small
                         // one (to be able to write when out of memory)
      buffer = smallbuf;
      bufsize = SMALLBUFSIZE;
   } else
      bufsize = WRITEBUFSIZE;

   // Get information about original file (if there is one).
   FileStat     stOld;
   stOld.st_dev = 0;
   stOld.st_ino = 0;
   perm = -1;
   if (stat((char *)fname, &stOld) < 0)
      newfile = TRUE;
   else {
      perm = stOld.st_mode;
      if (!S_ISREG(stOld.st_mode)) {    // not a file
         if (S_ISDIR(stOld.st_mode)) {
            errnum = (CS)"E502: ";
            errmsg = (CS)_(e_is_a_directory);
            goto fail;
         }
         if (mch_nodetype(fname) != NODE_WRITABLE) {
            errnum = (CS)"E503: ";
            errmsg = (CS)_(e_is_not_file_or_writable_device);
            goto fail;
         }
         //It's a device of some kind (or a fifo) which we can write to
         //but for which we can't make a backup.
         device = TRUE;
         newfile = TRUE;
         perm = -1;
      }
   }

   if (!device && !newfile) {
      // Check if the file is really writable (when renaming the file to
      // make a backup we won't discover it later).
      file_readonly = check_file_readonly(fname, (int)perm);

      if (!forceit && file_readonly) {
          errnum = (CS)"E505: ";
          errmsg = (CS)_(e_is_read_only_add_bang_to_override);
          goto fail;
      }

      // Check if the timestamp hasn't changed since reading the file.
      if (overwriting) {
         retval = check_mtime(book, &stOld);
         if (retval == FAIL)
            goto fail;
      }
   }

   // If 'backupskip' is not empty, don't make a backup for some files.
   Boole dobackup = !(p_bsk && match_file_list(p_bsk, sfname, fullFName));

   // Save the value of gotInterruptG and reset it.  We don't want a previous
   // interruption cancel writing, only hitting CTRL-C while writing should abort it.
   prev_gotInterruptG = gotInterruptG;
   gotInterruptG = FALSE;

   // Mark the book as 'being saved' to prevent changed buffer warnings
   book->isBeingSaved = true;

   //{{{backup
   // If we are not appending or filtering, the file exists, and the
   // 'writebackup', 'backup' or 'patchmode' option is set, need a backup.
   // When 'patchmode' is set also make a backup when appending.
   //
   // Do not make any backup, if 'writebackup' and 'backup' are both switched
   // off.  This helps when editing large files on almost-full disks.
   if (!(append) && !filtering && perm >= 0 && dobackup) {
      FileStat       st;

      if ((bkc & BKC_YES) || append)   // "yes"
          backup_copy = TRUE;
      ei ((bkc & BKC_AUTO)) {   // "auto"
          int i;

          // Don't rename the file when:
          // - it's a hard link
          // - it's a symbolic link
          // - we don't have write permission in the directory
          // - we can't set the owner/group of the new file
          if (stOld.st_nlink > 1
                || lstat((char *)fname, &st) < 0
                || st.st_dev != stOld.st_dev
                || st.st_ino != stOld.st_ino
          ) {
             backup_copy = TRUE;
          } else {
            // Check if we can create a file and set the owner/group to
            // the ones from the original file.
            // First find a file name that doesn't exist yet (use some
            // arbitrary numbers).
            STRCPY(IObuff, fname);
            fd = -1;
            for (i = 4913; ; i += 123) {
               sprintf((char *)fiGetShortFiName(IObuff), "%d", i);
               if (lstat((char *)IObuff, &st) < 0) {
                  fd = open((char *)IObuff, O_CREAT|O_WRONLY|O_EXCL|O_NOFOLLOW, perm);
                  if (fd < 0 && errno == EEXIST)
                      // If the same file name is created by another
                      // process between lstat() and open(), find another
                      // name.
                      continue;
                  break;
               }
            }
            if (fd < 0) { // can't write in directory
                backup_copy = TRUE;
            } else {
               (void)fchown(fd, stOld.st_uid, stOld.st_gid);
               if (stat((char *)IObuff, &st) < 0
                   || st.st_uid != stOld.st_uid
                   || st.st_gid != stOld.st_gid
                   || (long)st.st_mode != perm
               ) {
                  backup_copy = TRUE;
               } 
               // Close the file before removing it
               close(fd);
               mch_remove(IObuff);
            }
         }
      }

      // Break symlinks and/or hardlinks if we've been asked to.
      if ((bkc & BKC_BREAKSYMLINK) || (bkc & BKC_BREAKHARDLINK)) {
         int lstat_res;

         lstat_res = lstat((char *)fname, &st);

         // Symlinks.
         if ((bkc & BKC_BREAKSYMLINK) && lstat_res == 0 && st.st_ino != stOld.st_ino)
            backup_copy = FALSE;

         // Hardlinks.
         if ((bkc & BKC_BREAKHARDLINK)
                && stOld.st_nlink > 1
                && (lstat_res != 0 || st.st_ino == stOld.st_ino)) {
            backup_copy = FALSE;
         } 
      }

      // make sure we have a valid backup extension to use
      backup_ext = p_bex ? p_bex : S".bak";

      if (backup_copy && (fd = open((char *)fname, O_RDONLY | O_EXTRA, 0)) >= 0) {
         int bfd;
         CS po;
         int some_error = FALSE;
         FileStat stNew;
         CS rootname;
         CS p;
         mode_t umask_save;

         CS copybuf = tryBigAlloc(WRITEBUFSIZE + 1);
         if (!copybuf) {
            some_error = TRUE;       // out of memory
            goto nobackup;
         }

         // Try to make the backup in each directory in the 'bdir' option.
         //
         // We may have a writable file, that cannot be recreated with a simple 
         // open(..., O_CREAT, ) e.g:
         //  - the directory is not writable,
         //  - the file may be a symbolic link,
         //  - the file may belong to another user/group, etc.
         //
         // For these reasons, the existing writable file must be truncated
         // and reused. Creation of a backup COPY will be attempted.
         if (!p_bdir)
            goto nobackup;
            
         CS dirp = p_bdir;
         while (*dirp) {
            stNew.st_ino = 0;
            stNew.st_dev = 0;
            stNew.st_gid = 0;

            // Isolate one directory name, using an entry in 'bdir'.
            (void)copy_option_part(&dirp, copybuf, WRITEBUFSIZE, ",");

            p = copybuf + STRLEN(copybuf);
            if (after_pathsep(copybuf, p) && p[-1] == p[-2]
                  // Ends with '//', use full path
                  && (p = make_percent_swname(copybuf, p, fname)) != NULL
            ) {
               backup = fiAppendFileExtension(p, backup_ext, FALSE);
               eeglFree(p);
            }
            rootname = get_file_in_dir(fname, copybuf);
            if (rootname == NULL) {
               some_error = TRUE;       // out of memory
               goto nobackup;
            }

            // Make the backup file name.
            if (!backup)
               backup = fiAppendFileExtension(rootname, backup_ext, FALSE);
            if (!backup) {
               eeglFree(rootname);
               some_error = TRUE;      // out of memory
               goto nobackup;
            }

            // Check if backup file already exists.
            if (stat((char *)backup, &stNew) >= 0) {
               //Check if backup file is same as original file. May happen when 
               //fiAppendFileExtension() gave the same file back. E.g. silly link, or file 
               //name-length reached. If we don't check here, we either ruin the file when copying 
               //or erase it after writing. jw.
               if (stNew.st_dev == stOld.st_dev && stNew.st_ino == stOld.st_ino) {
                  EE_CLEAR(backup);   // no backup file to delete
                  goto endOfName;
               }

               //If we are not going to keep the backup file, don't delete an existing one, 
               //try to use another name. Change one character, just before the extension.
               if (!p_bk) {
                  po = backup + STRLEN(backup) - 1 - STRLEN(backup_ext);
                  if (po < backup)   // empty file name ???
                     po = backup;
                  *po = 'z';
                  while (*po > 'a' && stat((char *)backup, &stNew) >= 0)
                     --*po;
                  // They all exist??? Must be something wrong.
                  if (*po == 'a')
                     EE_CLEAR(backup);
               }
            }
endOfName: 
            eeglFree(rootname);

            // Try to create the backup file
            if (backup) {
               //remove old backup, if present
               mch_remove(backup);
               //Open with O_EXCL to avoid the file being created while
               //we were sleeping (symlink hacker attack?). Reset umask
               //if possible to avoid mch_setperm() below.
               umask_save = umask(0);
               bfd = open((char *)backup, O_WRONLY|O_CREAT|O_EXTRA|O_EXCL|O_NOFOLLOW, perm & 0777);
               (void)umask(umask_save);
               if (bfd < 0)
                  EE_CLEAR(backup);
               else {
                  //Set file protection same as original file, but strip s-bit. Only needed 
                  //if umask() wasn't used above. Try to set the group of the backup same as the
                  //original file. If this fails, set the protection bits for the group same as 
                  //the protection bits for others.
                  if (stNew.st_gid != stOld.st_gid && fchown(bfd, (uid_t)-1, stOld.st_gid) != 0)
                      mch_setperm(backup, (perm & 0707) | ((perm & 07) << 3));
                  mch_copy_xattr(fname, backup);

                  // copy the file.
                  writeInfo.fd = bfd;
                  writeInfo.bw_buf = copybuf;
                  while ((writeInfo.bw_len = read_eintr(fd, copybuf, WRITEBUFSIZE)) > 0) {
                     if (writeBytes(&writeInfo) == FAIL) {
                        errmsg = (CS)_(e_cant_write_to_backup_file_add_bang_to_override);
                        break;
                     }
                     ui_breakcheck();
                     if (gotInterruptG) {
                        errmsg = (CS)_(e_interrupted);
                        break;
                     }
                  }

                  if (close(bfd) < 0 && errmsg == NULL)
                     errmsg = (CS)_(e_close_error_for_backup_file_add_bang_to_write_anyway);
                  if (writeInfo.bw_len < 0)
                     errmsg = (CS)_(e_cant_read_file_for_backup_add_bang_to_write_anyway);
                  set_file_time(backup, stOld.st_atime, stOld.st_mtime);
                  mch_copy_xattr(fname, backup);
                  break;
               }
            }
         }
         
      nobackup:
         close(fd);      // ignore errors for closing read file
         eeglFree(copybuf);

         if (backup == NULL && errmsg == NULL)
            errmsg = (CS)_(e_cannot_create_backup_file_add_bang_to_write_anyway);
         // ignore errors when forceit is TRUE
         if ((some_error || errmsg) && !forceit) {
            retval = FAIL;
            goto fail;
         }
         errmsg = NULL;
      } else {
         CS p;
         CS rootname;

         // Make a backup by renaming the original file.

         // Form the backup file name - change path/fo.o.h to
         // path/fo.o.h.bak Try all directories in 'backupdir', first one that works is used.
         if (!p_bdir)
            goto finishedParsing;
            
         CS dirp = p_bdir;
         while (*dirp) {
            // Isolate one directory name and make the backup file name.
            (void)copy_option_part(&dirp, IObuff, IOSIZE, ",");

            p = IObuff + STRLEN(IObuff);
            if (after_pathsep(IObuff, p) && p[-1] == p[-2]) {
               // path ends with '//', use full path
               if ((p = make_percent_swname(IObuff, p, fname)) != NULL) {
                  backup = fiAppendFileExtension(p, backup_ext, FALSE);
                  eeglFree(p);
               }
            } 
            if (backup == NULL) {
               rootname = get_file_in_dir(fname, IObuff);
               if (rootname == NULL)
                  backup = NULL;
               else {
                  backup = fiAppendFileExtension(rootname, backup_ext, FALSE);
                  eeglFree(rootname);
               }
            }

            if (backup) {
               // If we are not going to keep the backup file, don't delete an existing one, 
               // try to use another name. Change one character, just before the extension.
               if (!p_bk && mch_getperm(backup) >= 0) {
                  p = backup + STRLEN(backup) - 1 - STRLEN(backup_ext);
                  if (p < backup)   // empty file name ???
                     p = backup;
                  *p = 'z';
                  while (*p > 'a' && mch_getperm(backup) >= 0)
                     --*p;
                  // They all exist??? Must be something wrong!
                  if (*p == 'a')
                     EE_CLEAR(backup);
               }
            }
            if (backup) {
               // Delete any existing backup and move the current version to the backup. For safety,
               // we don't remove the backup until the write has finished successfully. And if the
               // 'backup' option is set, leave it around.

               // If the renaming of the original file to the backup file works, quit here.
               if (eeRename(fname, backup) == 0)
                  break;

               EE_CLEAR(backup);   // don't do the rename below
            }
         }
      finishedParsing: 
         if (backup == NULL && !forceit) {
            errmsg = (CS)_(e_cant_make_backup_file_add_bang_to_write_anyway);
            goto fail;
         }
      }
   } //}}}

   if (end > book->mem.lineCount)
      end = book->mem.lineCount;
   if (book->mem.flags & ML_EMPTY)
      start = end + 1;

   // If the original file is being overwritten, there is a small chance that
   // we crash in the middle of writing. Therefore the file is preserved now.
   // This makes all block numbers positive so that recovery does not need the original file.
   // Don't do this if there is a backup file and we are exiting.
   if (reset_changed && !newfile && overwriting && !(exiting && backup)) {
      ml_preserve(book, FALSE);
      if (gotInterruptG) {
         errmsg = (CS)_(e_interrupted);
         goto fail;
      }
   }

   // Default: write the file directly.  May write to a temp file for multi-byte conversion.
   CS wfname = fname;   // name of file to write to
   
   
#define TRUNC_ON_OPEN 0
   // Open the file "wfname" for writing.
   // We may try to open the file twice: If we can't write to the file
   // and forceit is TRUE we delete the existing file and try to
   // create a new one. If this still fails we may have lost the
   // original file!  (this may happen when the user reached his
   // quotum for number of files).
   // Appending will fail if the file does not exist and forceit is FALSE.
   while ((fd = open((char *)wfname, O_WRONLY | O_EXTRA | (append
            ? (forceit ? (O_APPEND | O_CREAT) : O_APPEND)
            : (O_CREAT | TRUNC_ON_OPEN)), perm < 0 ? 0666 : (perm & 0777))) < 0
   ){
      // A forced write will try to create a new file if the old one
      // is still readonly. This may also happen when the directory
      // is read-only. In that case the mch_remove() will fail.
      if (errmsg == NULL) {
          FileStat   st;

         // Don't delete the file when it's a hard or symbolic link.
         if ((!newfile && stOld.st_nlink > 1) 
               || (lstat((char *)fname, &st) == 0 && (st.st_dev != stOld.st_dev
                || st.st_ino != stOld.st_ino))
         ) {
            errmsg = (CS)_(e_cant_open_linked_file_for_writing);
         } else {
            errmsg = (CS)_(e_cant_open_file_for_writing);
            if (forceit && perm >= 0) {
               // we write to the file, thus it should be marked writable after all
               if (!(perm & 0200))
                  made_writable = TRUE;
               perm |= 0200;
               if (stOld.st_uid != getuid() || stOld.st_gid != getgid())
                  perm &= 0777;
               if (!append)  // don't remove when appending
                  mch_remove(wfname);
               continue;
           }
        }
     }

      {
         FileStat   st;

         // If we failed to open the file, we don't need a backup. Throw it away.  If we moved or 
         // removed the original file try to put the backup in its place.
         if (backup && wfname == fname) {
            if (backup_copy) {
               // There is a small chance that we removed the original, so try to move the copy 
               // in its place. This may not work if the eeRename() fails. In that case we leave
               // the copy around.
               if (stat((char *)fname, &st) < 0)
                  eeRename(backup, fname);
               // if original file does exist throw away the copy
               if (stat((char *)fname, &st) >= 0)
                  mch_remove(backup);
            } else {
                // try to put the original file back
                eeRename(backup, fname);
            }
         }

         // if original file no longer exists give an extra warning
         if (!newfile && stat((char *)fname, &st) < 0)
            end = 0;
      }

      if (wfname != fname)
         eeglFree(wfname);
      goto fail;
   }
   writeInfo.fd = fd;

   {
   FileStat   st;

   // Double check we are writing the intended file before making any changes.
   if (overwriting
         && (!dobackup || backup_copy)
         && fname == wfname
         && perm >= 0
         && fstat(fd, &st) == 0
         && st.st_ino != stOld.st_ino
   ){
       close(fd);
       errmsg = (CS)_(e_file_changed_while_writing);
       goto fail;
   }
   }
   if (!append)
      (void)ftruncate(fd, (off_t)0);

   errmsg = NULL;

   writeInfo.bw_buf = buffer;
   nchars = 0;

   // use "++bin", "++nobin" or 'binary'
   if (invo && invo->force_bin != 0)
      write_bin = (invo->force_bin == FORCE_BIN);
   else
      write_bin = book->o.binary;

   writeInfo.bw_start_lnum = start;
   
   Boole write_undo_file = (book->o.undoFile
             && overwriting
             && !append
             && !filtering
             && reset_changed);

   if (write_undo_file)
       // Prepare for computing the hash value of the text.
       sha256_start(&sha_ctx);

   writeInfo.bw_len = bufsize;
   s = buffer;
   
   len = 0; // main loop of writing data
   for (lnum = start; lnum <= end; ++lnum) {
      // The next while loop is done once for each character written. Keep it fast!
      ptr = memGetLine(book, lnum, FALSE) - 1;
      if (write_undo_file)
         sha256_update(&sha_ctx, ptr + 1, (Unt)(STRLEN(ptr + 1) + 1));
      while ((c = *++ptr) != ZERO) {
         if (c == NL)
            *s = ZERO;      // replace newlines with NULs
         else
            *s = c;
         ++s;
         if (++len != bufsize)
            continue;
         if (writeBytes(&writeInfo) == FAIL) {
            end = 0;      // write error: break loop
            break;
         }
         nchars += bufsize;
         s = buffer;
         len = 0;
         writeInfo.bw_start_lnum = lnum;
      }
      // write failed or last line has no EOL: stop here
      if (end == 0
          || (lnum == end
               && ((write_bin && lnum == book->noEolLnum) || (lnum == book->mem.lineCount)))
      ) {
         ++lnum;         // written the line, count it
         no_eol = TRUE;
         break;
      }
      *s++ = NL;
      if (++len == bufsize && end) {
         if (writeBytes(&writeInfo) == FAIL) {
            end = 0;      // write error: break loop
            break;
         }
         nchars += bufsize;
         s = buffer;
         len = 0;

         ui_breakcheck();
         if (gotInterruptG) {
            end = 0;      // Interrupted, break loop
            break;
         }
      }
   }

   if (len > 0 && end > 0) {
      writeInfo.bw_len = len;
      if (writeBytes(&writeInfo) == FAIL)
         end = 0;          // write error
      nchars += len;
   }

   //If we started writing, finish writing. Also when an error was encountered.
   //On many journaling file systems there is a bug that causes both the original and the 
   //backup file to be lost when halting the system right after writing the file. That's 
   //because only the meta-data is journalled. Syncing the file slows down the system, but 
   //assures it has been written to disk and we don't lose it. For a device do try the fsync() 
   //but don't complain if it does not work (could be a pipe). If the @fsync option is off, 
   //don't fsync(). Useful for laptops.
   if (p_fs && eeFsync(fd) != 0 && !device) {
      errmsg = (CS)_(e_fsync_failed);
      end = 0;
   }

   //Probably need to set the security context.
   if (!backup_copy) {
       mch_copy_xattr(backup, wfname);
   }

   //When creating a new file, set its owner/group to that of the
   //original file. Get the new device and inode number.
   if (backup && !backup_copy) {
      FileStat   st;

      //Don't change the owner when it's already OK, some systems remove permission or ACL stuff.
      if (stat((char *)wfname, &st) < 0
          || st.st_uid != stOld.st_uid
          || st.st_gid != stOld.st_gid
      ) {
         //changing owner might not be possible
         (void)fchown(fd, stOld.st_uid, -1);
         //if changing group fails clear the group permissions
         if (fchown(fd, -1, stOld.st_gid) == -1 && perm > 0)
            perm &= ~070;
      }
      buf_setino(book);
   } ei (!book->isDevNumValid) {
      //Set the inode when creating a new file.
      buf_setino(book);
   } 

   if (made_writable)
      perm &= ~0200;   // reset 'w' bit for security reasons
   // set permission of new file same as old file
   if (perm >= 0)
      (void)mch_fsetperm(fd, perm);
   if (close(fd) != 0) {
      errmsg = (CS)_(e_close_failed);
      end = 0;
   }

   if (wfname != fname) {
       mch_remove(wfname);
       eeglFree(wfname);
   }

   if (end == 0) { // Error encountered.
      if (errmsg == NULL) {
         if (gotInterruptG)
            errmsg = (CS)_(e_interrupted);
         else
            errmsg = (CS)_(e_write_error_file_system_full);
      }

      //If we have a backup file, try to put it in place of the new file, because the new file is 
      //probably corrupt. This avoids losing the original file when trying to make a backup when 
      //writing the file a second time. When "backup_copy" is set we need to copy the backup over 
      //the new file. Otherwise rename the backup file.
      //If this is OK, don't give the extra warning message.
      if (backup) {
         if (backup_copy) {
            //This may take a while, if we were interrupted let the user know we got the message
            if (gotInterruptG) {
               msg(_(e_interrupted));
               out_flush();
            }
            if ((fd = open((char *)backup, O_RDONLY | O_EXTRA, 0)) >= 0) {
               if ((writeInfo.fd 
                     = open((char *)fname,
                      O_WRONLY | O_CREAT | O_TRUNC | O_EXTRA, perm & 0777)
                  ) >= 0
               ) {
                  // copy the file.
                  writeInfo.bw_buf = smallbuf;
                  while ((writeInfo.bw_len = read_eintr(fd, smallbuf, SMALLBUFSIZE)) > 0) {
                     if (writeBytes(&writeInfo) == FAIL)
                        break;
                  } 

                  if (close(writeInfo.fd) >= 0 && writeInfo.bw_len == 0)
                     end = 1;      // success
               }
               close(fd);   // ignore errors for closing read file
            }
         } else {
            if (eeRename(backup, fname) == 0)
               end = 1;
         }
      }
      goto fail;
   }

   lnum -= start;     // compute number of written lines
   --no_wait_return;  // may wait for return now

   if (!filtering) {
      msg_add_fname(book, fname);   // put fname in IObuff with quotes
      c = FALSE;
      if (device) {
         STRCAT(IObuff, _("[Device]"));
         c = TRUE;
      } ei (newfile) {
         STRCAT(IObuff, new_file_message());
         c = TRUE;
      }
      if (no_eol) {
         msg_add_eol();
         c = TRUE;
      }
      msg_add_lines(c, (long)lnum, nchars);   // add line/char count
      if (!shortmess(SHM_WRITE)) {
         if (append)
            STRCAT(IObuff, shortmess(SHM_WRI) ? _(" [a]") : _(" appended"));
         else
            STRCAT(IObuff, shortmess(SHM_WRI) ? _(" [w]") : _(" written"));
      }

      set_keep_msg(msgTruncDeco(IObuff, FALSE, 0), 0);
   }

   //When written everything correctly: reset 'modified'.  Unless not
   //writing to the original file and '+' is not in 'cpoptions'.
   if (reset_changed && whole && !append && overwriting) {
      unchanged(book, FALSE);
      //b:changedtick may be incremented in unchanged() but that should not
      //trigger a TextChanged event.
      if (book->lastChangeTick + 1 == CHANGEDTICK(book))
         book->lastChangeTick = CHANGEDTICK(book);
      u_unchanged(book);
      u_update_save_nr(book);
   }

   // If written to the current file, update the timestamp of the swap file
   // and reset the BF_WRITE_MASK flags. Also sets book->modifiedTime.
   if (overwriting) {
      ml_timestamp(book);
      if (append)
         book->flags &= ~BF_NEW;
      else
         book->flags &= ~BF_WRITE_MASK;
   }

   // Remove the backup unless 'backup' option is set
   if (!p_bk && backup && mch_remove(backup) != 0)
      emsg(_(e_cant_delete_backup_file));

   goto nofail;

   // Finish up.  We get here either after failure or success.
fail:
   --no_wait_return;      // may wait for return now
nofail:

   // Done saving, we accept changed book warnings again
   book->isBeingSaved = FALSE;

   eeglFree(backup);
   if (buffer != smallbuf)
      eeglFree(buffer);

   if (errmsg) {
      int numlen = errnum ? (int)STRLEN(errnum) : 0;

      flags = getDecoFlags(HLF_E);   // set highlight for error messages
      msg_add_fname(book, fname);      // put file name in IObuff with quotes
      if (STRLEN(IObuff) + STRLEN(errmsg) + numlen >= IOSIZE)
          IObuff[IOSIZE - STRLEN(errmsg) - numlen - 1] = ZERO;
      // If the error message has the form "is ...", put the error number in
      // front of the file name.
      if (errnum) {
          STRMOVE(IObuff + numlen, IObuff);
          mch_memmove(IObuff, errnum, (Unt)numlen);
      }
      STRCAT(IObuff, errmsg);
      emsg(IObuff);
      if (errmsg_allocated)
         eeglFree(errmsg);

      retval = FAIL;
      if (end == 0) {
         msgPutsDeco(
             _("\nWARNING: Original file may be lost or damaged\n"), flags | MSG_HIST
         );
         msgPutsDeco(
            _("don't quit the editor until the file is successfully written!"), flags | MSG_HIST
         );

         // Update the timestamp to avoid an "overwrite changed file"
         // prompt when writing again.
         if (stat((char *)fname, &stOld) >= 0) {
            buf_store_time(book, &stOld, fname);
            book->readTime = book->modifiedTime;
            book->readTimeNs = book->modifiedTimeNs;
         }
      }
   }
   msg_scroll = msg_save;

   // When writing the whole file and 'undofile' is set, also write the undo file.
   if (retval == OK && write_undo_file) {
      Byte hash[UNDO_HASH_SIZE];

      sha256_finish(&sha_ctx, hash);
      u_write_undo(NULL, FALSE, book, hash);
   }

   if (!should_abort(retval)) {
   AutocommSave   aco;

   curBook->noEolLnum = 0;  // in case it was set by the previous read

   // Apply POST autocommands.
   // Careful: The autocommands may call bookWrite() recursively!
   // Only do this when a portal was found for "book".
   auCommPrepareBook(&aco, book);
   if (curBook == book) {
       if (append)
      auCommApplyWithInvo(EVENT_FILEAPPENDPOST, fname, fname,
                        FALSE, curBook, invo);
       ei (filtering)
      auCommApplyWithInvo(EVENT_FILTERWRITEPOST, NULL, fname,
                        FALSE, curBook, invo);
       ei (reset_changed && whole)
      auCommApplyWithInvo(EVENT_BUFWRITEPOST, fname, fname,
                        FALSE, curBook, invo);
       else
      auCommApplyWithInvo(EVENT_FILEWRITEPOST, fname, fname,
                        FALSE, curBook, invo);

       // restore curPor/curBook and a few other things
       auCommRestoreBook(&aco);
   }

   if (aborting())       // autocmds may abort script processing
      retval = FALSE;
   }

   // Make sure marks will be written out to the eeglinfo file later, even when
   // the file is new.
   curBook->haveReadEeglinfoMarks = TRUE;

   gotInterruptG |= prev_gotInterruptG;

   return retval;
}

//}}}
//{{{argument lists (lists of open files)

#define AL_SET   1
#define AL_ADD   2
#define AL_DEL   3

// This flag is set whenever the argument list is being changed and calling a
// function that might trigger an autocommand.
private int arglist_locked = FALSE;

private int
check_arglist_locked(void) {
   if (arglist_locked) {
      emsg(_(e_cannot_change_arglist_recursively));
      return FAIL;
   }
   return OK;
}

// Clear an argument list: free all file names and reset it to zero entries.
void
alist_clear(EeArgList* al) {
   if (check_arglist_locked() == FAIL)
      return;
   while (--al->al_ga.len >= 0)
      eeglFree(AARGLIST(al)[al->al_ga.len].fname);
   ga_clear(&al->al_ga);
}

// Init an argument list.
void
alist_init(EeArgList *al) {
   ga_init2(&al->al_ga, sizeof(ArgFileEntry), 5);
}

// Remove a reference from an argument list.
// Ignored when the argument list is the global one.
// If the argument list is no longer used by any portal, free it.
void
alist_unlink(EeArgList *al) {
   if (al != &argListG && --al->al_refcount <= 0) {
      alist_clear(al);
      eeglFree(al);
   }
}

// Create a new argument list and use it for the current portal.
void
alist_new(void) {
   curPor->argList = ALLOC_ONE(EeArgList);
   if (curPor->argList == NULL) {
      curPor->argList = &argListG;
      ++argListG.al_refcount;
   } else {
      curPor->argList->al_refcount = 1;
      curPor->argList->id = ++max_alist_id;
      alist_init(curPor->argList);
   }
}


// Set the argument list for the current portal.
// Takes over the allocated files[] and the allocated fnames in it.
private void
alist_set(
   EeArgList* al,
   Boole useCurBook,
   Arr(int) fnum_list,
   Unt fnum_len,
   OUT ExpandMatch* files
){
   if (check_arglist_locked() == FAIL)
      return;

   alist_clear(al);
   if (GA_GROW_OK(&al->al_ga, (int)files->len)) {
      for (Unt i = 0; i < files->len; ++i) {
         if (gotInterruptG) {
            // When adding many books this can take a long time. Allow interrupting here.
            while (i < files->len) {
               eeglFree(files->c[i]);
               i++;
            }
            break;
         }

         // May set book name of a book previously used for the
         // argument list, so that it's re-used by arglistIngest.
         if (fnum_list && i < fnum_len) {
            arglist_locked = TRUE;
            bookSetName(fnum_list[i], files->c[i]);
            arglist_locked = FALSE;
         }

         arglistIngest(al, files->c[i], useCurBook ? 2 : 1);
         ui_breakcheck();
      }
      eeglFree(files);
   }
   if (al == &argListG)
      arg_had_last = FALSE;
}

// Add file "fname" to argument list "al".
// "fname" must have been allocated and "al" must have been checked for room.
// May trigger Buf* autocommands
void
arglistIngest(
    EeArgList* al,
    CS fname,
    int set_fnum   // 1: set book number; 2: re-use curBook
){
   if (fname == NULL)      // don't add NULL file names
      return;
   if (check_arglist_locked() == FAIL)
      return;
   arglist_locked = TRUE;
   curPor->locked = TRUE;

   AARGLIST(al)[al->al_ga.len].fname = fname;
   if (set_fnum > 0) {
      AARGLIST(al)[al->al_ga.len].fnum =
          bookOpen(fname, BLN_LISTED | (set_fnum == 2 ? BLN_CURBOOK : 0));
   } 
   ++al->al_ga.len;

   arglist_locked = FALSE;
   curPor->locked = FALSE;
}

//Isolate one argument, taking backticks. Change the argument in-place, puts a ZERO after it.
//Backticks remain. Return a pointer to the start of the next argument.
private CS
do_one_arg(CS str) {
   CS p;
   Boole inbacktick = false;
   for (p = str; *str; ++str) {
      // When the backslash is used for escaping the special meaning of a
      // character, we need to keep it until wildcard expansion.
      if (rem_backslash(str)) {
          *p++ = *str++;
          *p++ = *str;
      } else {
         // An item ends at a space not in backticks
         if (!inbacktick && isSpace(*str))
            break;
         if (*str == '`')
            inbacktick = !inbacktick;
         *p++ = *str;
      }
   }
   str = skipwhite(str);
   *p = ZERO;

   return str;
}

// Separate the arguments in "str" and return a list of pointers in the growarray "gap".
private int
get_arglist(ArrayList *gap, CS str, Boole escaped) {
   ga_init2(gap, sizeof(CS), 20);
   while (*str != ZERO) {
      if (ga_grow(gap, 1) == FAIL) {
         ga_clear(gap);
         return FAIL;
      }
      ((Byte **)gap->c)[gap->len++] = str;

      // If str is escaped, don't handle backslashes or spaces
      if (!escaped)
         return OK;

      // Isolate one argument, change it in-place, put a ZERO after it.
      str = do_one_arg(str);
   }
   return OK;
}

//Parse a list of arguments (file names), expand them and return in "fnames[fcountp]".  
//When "wildignore" is TRUE, removes files matching 'wildignore'. Return FAIL or OK.
int
bookParseAndExpandFnames(CS str, Boole omitWildignore, OUT ExpandMatch* matches){
   ArrayList   ga;

   if (get_arglist(&ga, str, false) == FAIL)
      return FAIL;
   int i;
   if (omitWildignore == true)
      i = expand_wildcards(
            ga.len, (Byte **)ga.c, EW_FILE|EW_NOTFOUND|EW_NOTWILD, OUT matches
      );
   else
      i = gen_expand_wildcards(
            ga.len, (Byte **)ga.c, EW_FILE|EW_NOTFOUND|EW_NOTWILD, OUT matches
      );

   ga_clear(&ga);
   return i;
}

// Check the validity of the arg_idx for each other portal.
private void
alist_check_arg_idx(void) {
   Portal   *port;
   Tab   *tp;

   FOR_ALL_TAB_PORTALS(tp, port) {
      if (port->argList == curPor->argList)
         check_arg_idx(port);
   } 
}

//Add files to the arglist of the current portal after arg "after".
//The file names in files[count] must have been allocated and are taken over.
//Files[] itself is not taken over.
private void
alist_add_list(
   ExpandMatch files,
   int after,       // where to add: 0 = before first one
   Boole will_edit  // will edit adding argument
){
   int old_argcount = ARGCOUNT;
   if (check_arglist_locked() != FAIL && GA_GROW_OK(&(curPor->argList)->al_ga, (int)files.len)) {
      if (after < 0)
         after = 0;
      if (after > ARGCOUNT)
         after = ARGCOUNT;
      if (after < ARGCOUNT)
         mch_memmove(
            ARGLIST + after + files.len, ARGLIST + after, 
            (ARGCOUNT - after) * sizeof(ArgFileEntry)
         );
      arglist_locked = TRUE;
      curPor->locked = TRUE;
      for (Unt i = 0; i < files.len; ++i) {
         int flags = BLN_LISTED | (will_edit ? BLN_CURBOOK : 0);

         ARGLIST[after + i].fname = files.c[i];
         ARGLIST[after + i].fnum = bookOpen(files.c[i], flags);
      }
      arglist_locked = FALSE;
      curPor->locked = FALSE;
      curPor->argList->al_ga.len += files.len;
      if (old_argcount > 0 && curPor->argListInd >= after)
          curPor->argListInd += files.len;
      return;
   }
}

// Delete the file names in 'alist_ga' from the argument list.
private void
arglist_del_files(ArrayList *alist_ga) {
   RegMatch   regmatch;
   int didone;
   int i;
   CS p;
   int match;

   // Delete the items: use each item as a regexp and find a match in the argument list.
   regmatch.rm_ic = FALSE;   // ignore case when 'fileignorecase' is set
   for (i = 0; i < alist_ga->len && !gotInterruptG; ++i) {
      p = ((Byte **)alist_ga->c)[i];
      p = file_pat_to_reg_pat(p, NULL, NULL);
      regmatch.regprog = compileRegexp(p, RE_MAGIC);
      if (regmatch.regprog == NULL) {
         eeglFree(p);
         break;
      }

      didone = FALSE;
      for (match = 0; match < ARGCOUNT; ++match) {
         if (eeRegexec(&regmatch, alist_name(&ARGLIST[match]), (ColNr)0)) {
            didone = TRUE;
            eeglFree(ARGLIST[match].fname);
            mch_memmove(ARGLIST + match, ARGLIST + match + 1,
               (ARGCOUNT - match - 1) * sizeof(ArgFileEntry));
            --curPor->argList->al_ga.len;
            if (curPor->argListInd > match)
                --curPor->argListInd;
            --match;
         }
      }
      
      eeRegFree(regmatch.regprog);
      eeglFree(p);
      if (!didone)
         showErrFmtMsg(_(e_no_match_str_2), ((Byte **)alist_ga->c)[i]);
   }
   ga_clear(alist_ga);
}

// "what" == AL_SET: Redefine the argument list to 'str'.
// "what" == AL_ADD: add files in 'str' to the argument list after "after".
// "what" == AL_DEL: remove files in 'str' from the argument list.
//
// Return FAIL for failure, OK otherwise.
private int
do_arglist(
   CS str,
   int what,
   int after UNUSED,   // 0 means before first one
   Boole will_edit   // will edit added argument
){
   ArrayList   new_ga;
   int      i;
   int      arg_escaped = TRUE;

   if (check_arglist_locked() == FAIL)
      return FAIL;

   // Set default argument for ":argadd" command.
   if (what == AL_ADD && *str == ZERO) {
      if (!curBook->fullFileName)
         return FAIL;
      str = curBook->currFileName;
      arg_escaped = FALSE;
   }

   // Collect all file name arguments in "new_ga".
   if (get_arglist(&new_ga, str, arg_escaped) == FAIL)
      return FAIL;

   ExpandMatch files;
   files.a = createArena();
   int retval = FAIL;
   
   if (what == AL_DEL)
      arglist_del_files(&new_ga);
   else {
      i = expand_wildcards(
            new_ga.len, (Byte **)new_ga.c, EW_DIR|EW_FILE|EW_ADDSLASH|EW_NOTFOUND, OUT &files
      );
      ga_clear(&new_ga);
      if (i == FAIL || files.len == 0) {
         emsg(_(e_no_match));
         goto cleanup;
      }

      if (what == AL_ADD) {
         alist_add_list(files, after, will_edit);
      } else // what == AL_SET
         alist_set(curPor->argList, will_edit, NULL, 0, OUT &files);
   }

   alist_check_arg_idx();
   retval = OK;
   
cleanup:
   deleteArena(files.a);
   return retval;
}

// Redefine the argument list.
void
set_arglist(CS str) {
    do_arglist(str, AL_SET, 0, true);
}

// Return TRUE if portal "port" is editing the file at the current argument index.
int
editing_arg_idx(Portal *port) {
    return !(port->argListInd >= WARGCOUNT(port)
      || (port->book->fiNum != WARGLIST(port)[port->argListInd].fnum
          && (port->book->fullFileName == NULL
             || !(fullpathcmp(
                alist_name(&WARGLIST(port)[port->argListInd]),
           port->book->fullFileName, TRUE, TRUE) & FPC_SAME))));
}

// Check if portal "port" is editing the argListInd file in its argument list.
void
check_arg_idx(Portal* port) {
   if (WARGCOUNT(port) > 1 && !editing_arg_idx(port)) {
      // We are not editing the current entry in the argument list.
      // Set "arg_had_last" if we are editing the last one.
      port->isNotValid = true;
      if (port->argListInd != WARGCOUNT(port) - 1
         && arg_had_last == FALSE
         && port->argList == &argListG
         && GARGCOUNT > 0
         && port->argListInd < GARGCOUNT
         && (port->book->fiNum == GARGLIST[GARGCOUNT - 1].fnum
             || (port->book->fullFileName != NULL
            && (fullpathcmp(alist_name(&GARGLIST[GARGCOUNT - 1]),
              port->book->fullFileName, TRUE, TRUE) & FPC_SAME)))
      )
          arg_had_last = TRUE;
   } else {
      // We are editing the current entry in the argument list.
      // Set "arg_had_last" if it's also the last one
      port->isNotValid = false;
      if (port->argListInd == WARGCOUNT(port) - 1 && port->argList == &argListG)
          arg_had_last = TRUE;
   }
}

// ":args", ":arglocal" and ":argglobal".
void
c_args(Invocation* invo) {
   int      i;

   if (invo->id != C_args) {
      if (check_arglist_locked() == FAIL)
         return;
      alist_unlink(curPor->argList);
      if (invo->id == C_argglobal)
         curPor->argList = &argListG;
      else // invo->id == C_arglocal
         alist_new();
    }

   // ":args file ..": define new argument list, handle like ":next"
   // Also for ":argslocal file .." and ":argsglobal file ..".
   if (*invo->arg != ZERO) {
      if (check_arglist_locked() == FAIL)
         return;
      c_next(invo);
      return;
   }

   // ":args": list arguments.
   if (invo->id == C_args) {
      if (ARGCOUNT <= 0)
         return;      // empty argument list

      Arr(CS) items = ALLOC_MULT(CS, ARGCOUNT);

      // Overwrite the command, for a short list there is no scrolling
      // required and no wait_return().
      gotoCommline(TRUE);

      for (i = 0; i < ARGCOUNT; ++i)
         items[i] = alist_name(&ARGLIST[i]);
      listInColumns(items, ARGCOUNT, curPor->argListInd, false);
      eeglFree(items);

      return;
   }

   // ":argslocal": make a local copy of the global argument list.
   if (invo->id == C_arglocal) {
      ArrayList   *gap = &curPor->argList->al_ga;

      if (GA_GROW_FAILS(gap, GARGCOUNT))
         return;

      for (i = 0; i < GARGCOUNT; ++i) {
         if (GARGLIST[i].fname != NULL) {
            AARGLIST(curPor->argList)[gap->len].fname =
                copyStr(GARGLIST[i].fname);
            AARGLIST(curPor->argList)[gap->len].fnum =
                GARGLIST[i].fnum;
            ++gap->len;
         }
      } 
   }
}

// ":previous", ":sprevious", ":Next" and ":sNext".
void
c_previous(Invocation* invo){
   // If past the last one already, go to the last one.
   if (curPor->argListInd - (int)invo->line2 >= ARGCOUNT)
      do_argfile(invo, ARGCOUNT - 1);
   else
      do_argfile(invo, curPor->argListInd - (int)invo->line2);
}

// ":rewind", ":first", ":sfirst" and ":srewind".
void
c_rewind(Invocation* invo){
   do_argfile(invo, 0);
}

// ":last" and ":slast".
void
c_last(Invocation* invo){
   do_argfile(invo, ARGCOUNT - 1);
}

// ":argument" and ":sargument".
void
c_argument(Invocation* invo){
   int      i;
   if (invo->addr_count > 0)
      i = invo->line2 - 1;
   else
      i = curPor->argListInd;
   do_argfile(invo, i);
}

// Edit file "argn" of the argument lists.
void
do_argfile(Invocation* invo, int argn){
   CS p;
   int      old_arg_idx = curPor->argListInd;
   Boole isSplitCommand = *invo->comm == 's';

   if (portErrorIfPopup(true))
      return;
   if (argn < 0 || argn >= ARGCOUNT) {
      if (ARGCOUNT <= 1)
         emsg(_(e_there_is_only_one_file_to_edit));
      ei (argn < 0)
         emsg(_(e_cannot_go_before_first_file));
      else
         emsg(_(e_cannot_go_beyond_last_file));

      return;
   }

   if (!isSplitCommand
          && (&ARGLIST[argn])->fnum != curBook->fiNum
          && !portCheckCanSetCurBookForceIt(invo->forceit))
      return;

   setpcmark();

   // split portal or create new tab first
   if (isSplitCommand || commModifierG.cmod_tab != 0) {
      if (splitPortal(0, 0) == FAIL)
         return;
      RESET_BINDING(curPor);
   } else {
      // if 'hidden' set, only check for changed file when re-editing the same book
      Boole sameFile = false;
      p = fiExpandAndCopy(alist_name(&ARGLIST[argn]), true);
      sameFile = fNameMatchesCurBook(p);
      eeglFree(p);
      if (sameFile
         && check_changed(curBook, CCGD_AW
             | (sameFile ? CCGD_MULTWIN : 0)
             | (invo->forceit ? CCGD_FORCEIT : 0)
             | CCGD_EXCMD)
      )
          return;
   }

   curPor->argListInd = argn;
   if (argn == ARGCOUNT - 1 && curPor->argList == &argListG)
      arg_had_last = TRUE;

   // Edit the file; always use the last known line number.
   // When it fails (e.g. Abort for already edited file) restore the argument index.
   if (startEditingFile(0, alist_name(&ARGLIST[curPor->argListInd]), NULL,
         invo, ECMD_LAST,
         (ECMD_HIDE) + (invo->forceit ? ECMD_FORCEIT : 0), curPor) == FAIL
   )
      curPor->argListInd = old_arg_idx;
   // like Vi: set the mark where the cursor is in the file.
   ei (invo->id != C_argdo)
      setmark('\'');
}

// ":next", and commands that behave like it.
void
c_next(Invocation* invo){
   // check for changed book now, if this fails the argument list is not redefined.
   int i;
   if (*invo->arg != ZERO) {    // redefine file list
      if (do_arglist(invo->arg, AL_SET, 0, TRUE) == FAIL)
         return;
      i = 0;
   } else
      i = curPor->argListInd + (int)invo->line2;
   do_argfile(invo, i);
}

// ":argdedupe"
void
c_argdedupe(Invocation* invo UNUSED){
   for (int i = 0; i < ARGCOUNT; ++i) {
      // Expand each argument to a full path to catch different paths leading to the same file
      CS firstFullname = fiExpandAndCopy(ARGLIST[i].fname, FALSE);
      if (!firstFullname)
          return;

      for (int j = i + 1; j < ARGCOUNT; ++j) {
         CS secondFullname = fiExpandAndCopy(ARGLIST[j].fname, FALSE);
         if (secondFullname == NULL)
            break;  // out of memory
         int areNamesDuplicate = fnamecmp(firstFullname, secondFullname) == 0;
         eeglFree(secondFullname);

         if (areNamesDuplicate) {
            // remove one duplicate argument
            eeglFree(ARGLIST[j].fname);
            mch_memmove(ARGLIST + j, ARGLIST + j + 1, (ARGCOUNT - j - 1) * sizeof(ArgFileEntry));
            --ARGCOUNT;

            if (curPor->argListInd == j)
                curPor->argListInd = i;
            ei (curPor->argListInd > j)
                --curPor->argListInd;

            --j;
         }
      }

      eeglFree(firstFullname);
   }
}

void
c_argedit(Invocation* invo) {
   int i = invo->addr_count ? (int)invo->line2 : curPor->argListInd + 1;
   // Whether curBook will be reused, curBook->fullFileName will be set.
   Boole isReusable = isCurBookReusable();

   if (do_arglist(invo->arg, AL_ADD, i, true) == FAIL)
      return;

   if (curPor->argListInd == 0
          && (curBook->mem.flags & ML_EMPTY)
          && (curBook->fullFileName == NULL || isReusable))
      i = 0;
   // Edit the argument.
   if (i < ARGCOUNT)
      do_argfile(invo, i);
}

void
c_argadd(Invocation* invo) {
   do_arglist(invo->arg, AL_ADD,
          invo->addr_count > 0 ? (int)invo->line2 : curPor->argListInd + 1,
          false);
}

void
c_argdelete(Invocation* invo) {
   int      i;
   int      n;

   if (check_arglist_locked() == FAIL)
      return;

   if (invo->addr_count > 0 || *invo->arg == ZERO) {
   // ":argdel" works like ":.argdel"
   if (invo->addr_count == 0) {
       if (curPor->argListInd >= ARGCOUNT) {
      emsg(_(e_no_argument_to_delete));
      return;
       }
       invo->line1 = invo->line2 = curPor->argListInd + 1;
   } ei (invo->line2 > ARGCOUNT)
       // ":1,4argdel": Delete all arguments in the range.
       invo->line2 = ARGCOUNT;
   n = invo->line2 - invo->line1 + 1;
   if (*invo->arg != ZERO)
       // Can't have both a range and an argument.
       emsg(_(e_invalid_argument));
   ei (n <= 0) {
       // Don't give an error for ":%argdel" if the list is empty.
       if (invo->line1 != 1 || invo->line2 != 0)
      emsg(_(e_invalid_range));
   } else {
       for (i = invo->line1; i <= invo->line2; ++i)
      eeglFree(ARGLIST[i - 1].fname);
       mch_memmove(ARGLIST + invo->line1 - 1, ARGLIST + invo->line2,
         (Unt)((ARGCOUNT - invo->line2) * sizeof(ArgFileEntry)));
       curPor->argList->al_ga.len -= n;
       if (curPor->argListInd >= invo->line2)
      curPor->argListInd -= n;
       ei (curPor->argListInd > invo->line1)
      curPor->argListInd = invo->line1;
       if (ARGCOUNT == 0)
      curPor->argListInd = 0;
       ei (curPor->argListInd >= ARGCOUNT)
      curPor->argListInd = ARGCOUNT - 1;
   }
   } else
      do_arglist(invo->arg, AL_DEL, 0, false);
}

// Function given to expandGeneric() to obtain the possible arguments of the argedit and argdelete 
// commands.
CS
get_arglist_name(Expand *xp UNUSED, int idx) {
   return (idx >= ARGCOUNT) ? E : alist_name(&ARGLIST[idx]);
}

// Get the file name for an argument list entry.
CS
alist_name(ArgFileEntry *afe) {
   // Use the name from the associated book if it exists.
   Book* b = bookFindFileByBookNr(afe->fnum);
   if (!b || b->currFileName == NULL)
      return afe->fname;
   return b->currFileName;
}

// State used by the :all command to open all the files in the argument list in separate portals
typedef struct {
   EeArgList* alist;      // argument list to be used
   int   had_tab;
   int   keep_tabs;
   int   forceit;

   int      use_firstPor;   // use first portal for arglist
   Arr(Byte) opened;   // Array of weight for which args are open:
           //  0: not opened
           //  1: opened in other tab
           //  2: opened in curtab
           //  3: opened in curtab and curPor
   int opened_len;   // length of opened[]
   Portal* new_curPor;
   Tab* new_curtab;
} ArgAllState;

// Close all the portals containing files which are not in the argument list.
// Used by the ":all" command.
private void
argAllCloseUnusedPortals(ArgAllState *aall) {
   Portal* po;
   Portal* wpnext;
   Tab* tNext;
   Book* book;
   int i;
   Portal* old_curPor;
   Tab* old_curtab;

   old_curPor = curPor;
   old_curtab = curtab;

   if (aall->had_tab > 0)
      gotoTab(firstTabG, TRUE, TRUE);

   // moving tabs around in an autocommand may cause an endless loop
   movingTabsForbiddenG++;
   for (;;) {
      tNext = curtab->next;
      for (po = firstPor; po != NULL; po = wpnext) {
         wpnext = po->next;
         book = po->book;
         if (!book->fullFileName
                || (!aall->keep_tabs && (book->countPortals > 1 || po->width != visibleColsG))
         )
            i = aall->opened_len;
         else {
            // check if the book in this portal is in the arglist
            for (i = 0; i < aall->opened_len; ++i) {
               if (i < aall->alist->al_ga.len
                   && (AARGLIST(aall->alist)[i].fnum == book->fiNum
                     || fullpathcmp(alist_name( &AARGLIST(aall->alist)[i]),
                        book->fullFileName, TRUE, TRUE) & FPC_SAME)
               ) {
               int weight = 1;

               if (old_curtab == curtab) {
                  ++weight;
                  if (old_curPor == po)
                     ++weight;
               }

               if (weight > (int)aall->opened[i]) {
                  aall->opened[i] = (Byte)weight;
                  if (i == 0) {
                     if (aall->new_curPor != NULL)
                        aall->new_curPor->argListInd = aall->opened_len;
                     aall->new_curPor = po;
                     aall->new_curtab = curtab;
                  }
               } ei (aall->keep_tabs)
                  i = aall->opened_len;

               if (po->argList != aall->alist) {
                   // Use the current argument list for all portals
                   // containing a file from it.
                   alist_unlink(po->argList);
                   po->argList = aall->alist;
                   ++po->argList->al_refcount;
               }
               break;
               }
            }
         }
         po->argListInd = i;

         if (i == aall->opened_len && !aall->keep_tabs) {// close this portal
            // If the book was changed, and we would like to hide it, try autowriting.
            // don't close last portal
            if (ONLY_ONE_PORTAL && (firstTabG->next == NULL || !aall->had_tab))
               aall->use_firstPor = TRUE;
            else {
               closePortal(po, FALSE);

               // check if autocommands removed the next portal
               if (!portalIsValid(wpnext))
                  wpnext = firstPor;   // start all over...
            }
         }
      }

      // Without the ":tab" modifier only do the current tab.
      if (aall->had_tab == 0 || tNext == NULL)
         break;

      // check if autocommands removed the next tab
      if (!isTabValid(tNext))
         tNext = firstTabG;   // start all over...

      gotoTab(tNext, TRUE, TRUE);
   }
   movingTabsForbiddenG--;
}

// Open upto "count" portals for the files in the argument list 'aall->alist'.
private void
openPortalsIntoFiles(ArgAllState *aall, int count) {
   Portal   *po;
   Boole tabDropEmptyPortal = false;
   int      i;
   int      split_ret = OK;
   int      p_ea_save;

   // ":tab drop file" should re-use an empty portal to avoid "--remote-tab"
   // leaving an empty tab when executed locally.
   if (aall->keep_tabs && CURBOOK_EMPTY() && curBook->countPortals == 1
             && curBook->fullFileName == NULL && !curBook->wasModified
   ) {
      aall->use_firstPor = TRUE;
      tabDropEmptyPortal = true;
   }

   for (i = 0; i < count && !gotInterruptG; ++i) {
      if (aall->alist == &argListG && i == argListG.al_ga.len - 1)
         arg_had_last = TRUE;
      if (aall->opened[i] > 0) {
         // Move the already present portal to below the current portal
         if (curPor->argListInd != i) {
            FOR_ALL_PORTALS(po) {
               if (po->argListInd == i) {
                  if (aall->keep_tabs) {
                      aall->new_curPor = po;
                      aall->new_curtab = curtab;
                  } ei (po->frame->parent   != curPor->frame->parent) {
                      emsg(_(e_portal_layout_changed_unexpectedly));
                      i = count;
                      break;
                  } else
                     portMoveAfter(po, curPor);
                  break;
               }
            }
         }
      } ei (split_ret == OK) {
         // trigger events for tab drop
         if (tabDropEmptyPortal && i == count - 1)
            --autocmd_no_enter;
         if (!aall->use_firstPor) { // split current portal
            p_ea_save = p_ea;
            p_ea = TRUE;      // use space from all portals
            split_ret = splitPortal(0, WSP_ROOM | WSP_BELOW);
            p_ea = p_ea_save;
            if (split_ret == FAIL)
               continue;
         } else    // first portal: do autocomm for leaving this book
            --autocmd_no_leave;

         // edit file "i"
         curPor->argListInd = i;
         if (i == 0) {
            aall->new_curPor = curPor;
            aall->new_curtab = curtab;
         }
         (void)startEditingFile(
            0, alist_name(&AARGLIST(aall->alist)[i]), NULL, NULL, ECMD_ONE, ECMD_HIDE + ECMD_OLDBUF,
            curPor
         );
         if (tabDropEmptyPortal && i == count - 1)
            ++autocmd_no_enter;
         if (aall->use_firstPor)
            ++autocmd_no_leave;
         aall->use_firstPor = FALSE;
      }
      ui_breakcheck();

      // When ":tab" was used open a new tab for a new portal repeatedly.
      if (aall->had_tab > 0)
         commModifierG.cmod_tab = 9999;
   }
}

// openAllArgs(): Open up to "count" portals, one for each argument.
private void
openAllArgs(
    int   count,
    int   forceit,      // hide books in current portals
    int keep_tabs      // keep current tabs, for ":tab drop file"
){
    ArgAllState   aall;
    Portal      *last_curPor;
    Tab      *last_curtab;
    int         prev_arglist_locked = arglist_locked;

   if (commPortTypeG != 0) {
      emsg(_(e_invalid_in_commline_portal));
      return;
   }
   if (ARGCOUNT <= 0) {
      // Don't give an error message. We don't want it when the ":all" command is in the .vimrc.
      return;
   }
   setpcmark();

   aall.use_firstPor = FALSE;
   aall.had_tab = commModifierG.cmod_tab;
   aall.new_curPor = NULL;
   aall.new_curtab = NULL;
   aall.forceit = forceit;
   aall.keep_tabs = keep_tabs;
   aall.opened_len = ARGCOUNT;
   aall.opened = allocZeroed(aall.opened_len);

   // Autocommands may do anything to the argument list.  Make sure it's not
   // freed while we are working here by "locking" it.  We still have to
   // watch out for its size being changed.
   aall.alist = curPor->argList;
   ++aall.alist->al_refcount;
   arglist_locked = TRUE;

   Tab *new_lu_tp = curtab;

   // Stop Visual mode, the cursor and "VIsual" may very well be invalid after
   // switching to another book.
   reset_VIsual_and_resel();

   //Try closing all portals that are not in the argument list. Also close portals that are not 
   //full width; When 'hidden' or "forceit" set the book becomes hidden. Portals that have a 
   //changed book and can't be hidden won't be closed. When the ":tab" modifier was used do 
   //this for all tabs.
   argAllCloseUnusedPortals(&aall);

   // Open a portal into files in the argument list that don't have one.
   // ARGCOUNT may change while doing this, because of autocommands.
   if (count > aall.opened_len || count <= 0)
      count = aall.opened_len;

   // Don't execute Win/Buf Enter/Leave autocommands here.
   ++autocmd_no_enter;
   ++autocmd_no_leave;
   last_curPor = curPor;
   last_curtab = curtab;
   enterPortal(lastPor, FALSE);

   // Open up to "count" portals.
   openPortalsIntoFiles(&aall, count);

   // Remove the "lock" on the argument list.
   alist_unlink(aall.alist);
   arglist_locked = prev_arglist_locked;

   --autocmd_no_enter;

   // restore last referenced tab's curPor
   if (last_curtab != aall.new_curtab) {
      if (isTabValid(last_curtab))
          gotoTab(last_curtab, TRUE, TRUE);
      if (portalIsValid(last_curPor))
          enterPortal(last_curPor, FALSE);
   }
   // to portal with first arg
   if (isTabValid(aall.new_curtab))
      gotoTab(aall.new_curtab, TRUE, TRUE);

   // Now set the last used tabpage to where we started.
   if (isTabValid(new_lu_tp))
      lastUsedTabG = new_lu_tp;

   if (portalIsValid(aall.new_curPor))
      enterPortal(aall.new_curPor, FALSE);

   --autocmd_no_leave;
   eeglFree(aall.opened);
}

// ":all" and ":sall". Also used for ":tab drop file ..." after setting the argument list.
void
c_all(Invocation* invo) {
   if (invo->addr_count == 0)
      invo->line2 = 9999;
   openAllArgs((int)invo->line2, invo->forceit, invo->id == C_drop);
}

//Concatenate all files in the argument list, separated by spaces, and return it in one allocated 
//string. Spaces and backslashes in the file names are escaped with a backslash.
//Return NULL when out of memory.
CS
arg_all(void) {
   int      len;
   int      idx;
   CS retval = NULL;
   CS p;

   // Do this loop twice: first time: compute the total length second time: concatenate the names
   for (;;) {
      len = 0;
      for (idx = 0; idx < ARGCOUNT; ++idx) {
         p = alist_name(&ARGLIST[idx]);
         if (p == NULL)
            continue;
         if (len > 0) {
            // insert a space in between names
            if (retval != NULL)
                retval[len] = ' ';
            ++len;
         }
         for ( ; *p != ZERO; ++p) {
            if (*p == ' '
               || *p == '\\'
               || *p == '`'
            ) {
               // insert a backslash
               if (retval != NULL)
                  retval[len] = '\\';
               ++len;
            }
            if (retval != NULL)
                retval[len] = *p;
            ++len;
         }
      }

      // second time: break here
      if (retval != NULL) {
         retval[len] = ZERO;
         break;
      }

      // allocate memory
      retval = alloc(len + 1);
   }

   return retval;
}

// "argc([portal id])" function
void
f_argc(Var* argvars, Var* returnVar) {
   Portal* po;

   if (argvars[0].tag == VAR_UNKNOWN)
      // use the current portal
      returnVar->number = ARGCOUNT;
   ei (argvars[0].tag == VAR_NUMBER && tv_get_number(&argvars[0]) == -1)
      // use the global argument list
      returnVar->number = GARGCOUNT;
   else {
      // use the argument list of the specified portal
      po = portFindByNrOrId(&argvars[0]);
      if (po != NULL)
         returnVar->number = WARGCOUNT(po);
      else
         returnVar->number = -1;
   }
}

void
f_argidx(Var *argvars UNUSED, Var *returnVar) {
   returnVar->number = curPor->argListInd;
}

void
f_arglistid(Var *argvars, Var *returnVar) {
   Portal   *po;

   returnVar->number = -1;
   po = find_tabwin(&argvars[0], &argvars[1], NULL);
   if (po != NULL)
      returnVar->number = po->argList->id;
}

// Get the argument list for a given portal
private void
get_arglist_as_returnVar(ArgFileEntry *arglist, Unt argcount, Var *returnVar) {
   allocReturnList(returnVar);
   if (arglist) {
      for (Unt idx = 0; idx < argcount; ++idx)
         list_append_string(returnVar->list, alist_name(&arglist[idx]), -1);
   }
}

// "argv(nr)" function
void
f_argv(Var *argvars, Var *returnVar) {
   ArgFileEntry   *arglist = NULL;
   Unt argcount = 0;

   if (argvars[0].tag == VAR_UNKNOWN) {
      get_arglist_as_returnVar(ARGLIST, ARGCOUNT, returnVar);
      return;
   }

   if (argvars[1].tag == VAR_UNKNOWN) {
      arglist = ARGLIST;
      argcount = ARGCOUNT;
   } ei (argvars[1].tag == VAR_NUMBER && tv_get_number(&argvars[1]) == -1) {
      arglist = GARGLIST;
      argcount = GARGCOUNT;
   } else {
      Portal* po = portFindByNrOrId(&argvars[1]);
      if (po) {
         // Use the argument list of the specified portal
         arglist = WARGLIST(po);
         argcount = WARGCOUNT(po);
      }
   }

   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;
   int idx = varGetNumberChk(&argvars[0], NULL);
   if (arglist && idx >= 0 && idx < (int)argcount)
      returnVar->string = copyStr(alist_name(&arglist[idx]));
   ei (idx == -1)
      get_arglist_as_returnVar(arglist, argcount, returnVar);
}

//}}}
//{{{text properties. See ":help text-properties".

//In a hashtable item "hi_key" points to "name" in a PropType.
//This avoids adding a pointer to the hashtable item.
//PT2HIKEY() converts a proptype pointer to a hashitem key pointer.
//HIKEY2PT() converts a hashitem key pointer to a proptype pointer.
//HI2PT() converts a hashitem pointer to a proptype pointer.
#define PT2HIKEY(p)  (mbText((p)->name))
#define HIKEY2PT(p)   ((PropType *)((p) - offsetof(PropType, name)))
#define HI2PT(hi)      HIKEY2PT((hi)->hi_key)

// The global text property types.
// property types for compilation errors and warning, used for example after :make
//private PropType compilationTypesS[] = {
//   (PropType) {.id = 0, .ty = 0, .hilite = 123, .priority = 0, .flags = PT_FLAG_COMBINE},
//   (PropType) {.id = 1, .ty = 0, .hilite = 123, .priority = 0, .flags = PT_FLAG_COMBINE}
//};
private EeSet *global_proptypes = NULL;
private PropType **global_proparray = NULL;

// The last used text property type ID.
private int proptype_id = 0;

// Find a property type by name, return the hashitem.
// Returns NULL if the item can't be found.
private EeSetItem *
findPropTypeHash(Text name, Book* book) {
   if (name.len == 0)
      return NULL;
      
   EeSet* ht = book ? book->propTypes : global_proptypes;
   if (!ht)
      return NULL;
      
   EeSetItem* hi = hash_find(ht, name);
   if (HASHITEM_EMPTY(hi))
      return NULL;
   return hi;
}

// Like findPropTypeHash() but return the property type.
private PropType *
findPropTypeByName(Text name, Book* book) {
   EeSetItem* hi = findPropTypeHash(name, book);
   return hi ?  HI2PT(hi) : null;
}

// Get the prop type ID of "name". When not found return zero.
int
findPropTypeIdByName(Text name, Book* book) {
   PropType *pt = findPropTypeByName(name, book);

   if (pt == NULL)
      return 0;
   return pt->id;
}

// Lookup a property type by name.  First in "book" and when not found in the global types.
// When not found gives an error message and returns NULL.
private PropType *
lookup_prop_type(Text name, Book* book) {
   PropType *type = findPropTypeByName(name, book);

   if (type == NULL)
      type = findPropTypeByName(name, NULL);
   if (type == NULL)
      showErrFmtMsg(_(e_property_type_str_does_not_exist), name);
   return type;
}

//Get an optional "bufnr" item from the dict in "arg".
//When the argument is not used or "bufnr" is not present then "book" is unchanged.
//If "bufnr" is valid or not present return OK.
//When "arg" is not a dict or "bufnr" is invalid return FAIL.
private Unt
getBookNrFromArg(Var *arg, Book** book) {
   if (arg->tag != VAR_BAG) {
      emsg(_(e_dictionary_required));
      return FAIL;
   }
   if (!arg->bag)
      return OK;  // NULL dict is like an empty dict
      
   DictItem* di = bagFind(arg->bag, tConst("bufnr"));
   if (di && (di->c.tag != VAR_NUMBER || di->c.number != 0)) {
      *book = evGetBookArg(&di->c);
      if (*book == NULL)
          return FAIL;
   }
   return OK;
}

//prop_add({lnum}, {col}, {props})
void
f_prop_add(Var *argvars, Var *returnVar) {
   LineNr start_lnum = tv_get_number(&argvars[0]);
   ColNr start_col = tv_get_number(&argvars[1]);
   if (check_for_dict_arg(argvars, 2) == FAIL)
      return;

   returnVar->number = prop_add_common(start_lnum, start_col,
             argvars[2].bag, curBook, &argvars[2]);
}

typedef struct {
   CS tyName;
   int      id;
   NULLABLE CS text; // if non-empty, the text to display above or before the line
   int      textPaddingLeft;
   int      textFlags;
   LineNr   startLnum;
   LineNr   endLnum;
   ColNr      startCol;
   ColNr      endCol;
} Prop;

//Attach a text property 'type_name' to the text starting at [start_lnum, start_col] and ending at
//[end_lnum, end_col] in the book "book" and assign identifier "id".
//When "text" is not NULL add it to book->textPropText[-id - 1].
private int
addProp(OUT Book* book, Prop prop) {
   LineNr   lnum;
   int      proplen;
   CS newprops;
   Unt textlen;
   CS newtext;
   int i;
   TextProp tmpProp;
   NULLABLE CS text = prop.text;
   int res = FAIL;

   PropType* type = lookup_prop_type(mbText(prop.tyName), book);
   if (!type)
      goto theend;

   if (prop.startLnum < 1 || prop.startLnum > book->mem.lineCount) {
      showErrFmtMsg(_(e_invalid_line_number_nr), (long)prop.startLnum);
      goto theend;
   }
   if (prop.endLnum < prop.startLnum || prop.endLnum > book->mem.lineCount) {
      showErrFmtMsg(_(e_invalid_line_number_nr), (long)prop.endLnum);
      goto theend;
   }

   if (book->mem.mfile == NULL) {
      emsg(_(e_cannot_add_text_property_to_unloaded_buffer));
      goto theend;
   }

   if (prop.text) {
      ArrayList* gap = &book->textPropText;
      CS p;

      // double check we got the right ID
      if (-prop.id - 1 != gap->len)
         internalErrMsg(S"text prop ID mismatch");
      if (gap->ga_growsize == 0)
         ga_init2(gap, sizeof(char *), 50);
      if (ga_grow(gap, 1) == FAIL)
         goto theend;
      ((Byte **)gap->c)[gap->len] = text;
      gap->len++; 

      // change any control character (Tab, Newline, etc.) to a Space to make
      // it simpler to compute the size
      for (p = prop.text; *p != ZERO; MB_PTR_ADV(p)) {
         if (*p < ' ')
            *p = ' ';
      } 
      text = NULL;
   }

   for (lnum = prop.startLnum; lnum <= prop.endLnum; ++lnum) {
      ColNr sort_col;   // column where it appears
      long   length;       // in bytes

      // Fetch the line to get the lineLen field updated.
      CS props;
      proplen = get_text_props(OUT &props, book, lnum, true);
      textlen = book->mem.lineLen - proplen * sizeof(TextProp);

      ColNr col;       // start column use in col
      if (lnum == prop.startLnum)
         col = prop.startCol;
      else
         col = 1;
      if (col - 1 > (ColNr)textlen && !(col == 0 && prop.text != NULL)) {
         showErrFmtMsg(_(e_invalid_column_number_nr), (long)prop.startCol);
         goto theend;
      }
      sort_col = col;

      if (lnum == prop.endLnum)
         length = prop.endCol - col;
      else
         length = (int)textlen - col + 1;
      if (length > (long)textlen)
         length = (int)textlen;   // can include the end-of-line
      if (length < 0)
         length = 0;      // zero-width property

      if (prop.text != NULL) {
         length = 1;      // text is placed on one character
         if (col == 0) {
            col = MAXCOL;   // before or after the line
            if ((prop.textFlags & TEXT_PROP_ALIGN_ABOVE) == 0)
               sort_col = MAXCOL;
            length += prop.textPaddingLeft;
         }
      }

      // Allocate the new line with space for the new property.
      newtext = alloc(book->mem.lineLen + sizeof(TextProp));
      // Copy the text, including terminating ZERO.
      mch_memmove(newtext, book->mem.cachedLine, textlen);

      // Find the index where to insert the new property.
      // Since the text properties are not aligned properly when stored with
      // the text, we need to copy them as bytes before using it as a struct.
      for (i = 0; i < proplen; ++i) {
         mch_memmove(&tmpProp, props + i * sizeof(TextProp), sizeof(TextProp));
         // col is MAXCOL when the text goes above or after the line, when
         // above we should use column zero for sorting
         ColNr propCol = (tmpProp.flags & TEXT_PROP_ALIGN_ABOVE) ? 0 : tmpProp.col;
         if (propCol >= sort_col)
            break;
      }
      newprops = newtext + textlen;
      if (i > 0)
         mch_memmove(newprops, props, sizeof(TextProp) * i);

      tmpProp.col = col;
      tmpProp.len = length;
      tmpProp.id = prop.id;
      tmpProp.type = type->id;
      tmpProp.flags = prop.textFlags
                | (lnum > prop.startLnum ? TEXT_PROP_CONT_PREV : 0)
                | (lnum < prop.endLnum ? TEXT_PROP_CONT_NEXT : 0)
                | ((type->flags & PT_FLAG_INS_START_INCL) ? TEXT_PROP_START_INCL : 0);
      tmpProp.leftPad = prop.textPaddingLeft;
      mch_memmove(newprops + i * sizeof(TextProp), &tmpProp, sizeof(TextProp));

      if (i < proplen) {
         mch_memmove(
            newprops + (i + 1) * sizeof(TextProp), props + i * sizeof(TextProp), 
            sizeof(TextProp) * (proplen - i)
         );
      } 

      if ((book->mem.flags & ML_LINE_DIRTY) != 0)
         eeglFree(book->mem.cachedLine);
      book->mem.cachedLine = newtext;
      book->mem.lineLen += sizeof(TextProp);
      book->mem.flags |= ML_LINE_DIRTY;
   }

   normInvalidateDisplayOfChangedBookLine(book);
   changed_lines_buf(book, prop.startLnum, prop.endLnum + 1, 0);
   res = OK;

theend:
   eeglFree(text);
   return res;
}

//prop_add_list()
//First argument specifies the text property:
//  {'type': <str>, 'id': <num>, 'bufnr': <num>}
//Second argument is a List where each item is a List with the following
//entries: [lnum, start_col, end_col]
void
f_prop_add_list(Var *argvars, Var *returnVar UNUSED) {
   Book   *book = curBook;
   int      id = 0;
   ListItem   *li;
   Boole error = false;
   int      prev_anyEmsgG = anyEmsgG;
   Prop prop;

   if (check_for_dict_arg(argvars, 0) == FAIL || confirmVarIsList(argvars, 1) == FAIL)
      return;

   if (confirmVarIsNonnullList(argvars, 1) == FAIL)
      return;

   Bag* b = argvars[0].bag;
   if (!b || !bagHasKey(b, tConst("type"))) {
      emsg(_(e_missing_property_type_name));
      return;
   }

   if (bagHasKey(b, tConst("id"))) {
      Long x;
      x = bagGetNumber(b, tConst("id"));
      if (x > INT_MAX || x  <= INT_MIN) {
         showErrFmtMsg(_(e_val_too_large), bagGetString(b, tConst("id"), false));
         return;
      }
      id = (int)x;
   }

   if (getBookNrFromArg(&argvars[0], &book) == FAIL)
      return;

   // This must be done _before_ we start adding properties because property changes trigger book
   // (memline) reorganisation, which needs this flag to be correctly set.
   book->hasTextprop = TRUE;  // this is never reset
   FOR_ALL_LIST_ITEMS(argvars[1].list, li) {
      if (li->c.tag != VAR_LIST || li->c.list == NULL) {
         emsg(_(e_list_required));
         return;
      }

      List* posList = li->c.list;
      prop.startLnum = list_find_nr(posList, 0L, OUT &error);
      if (!error)
         prop.startCol = list_find_nr(posList, 1L, OUT &error);
      if (!error)
         prop.endLnum = list_find_nr(posList, 2L, OUT &error);
      if (!error)
         prop.endCol = list_find_nr(posList, 3L, OUT &error);
      prop.id = id;
      if (!error && posList->len > 4)
          prop.id = list_find_nr(posList, 4L, OUT &error);
      if (error || prop.startLnum <= 0 || prop.startCol <= 0 
            || prop.endLnum <= 0 || prop.endCol <= 0
      ) {
         if (prev_anyEmsgG == anyEmsgG)
            emsg(_(e_invalid_argument));
         return;
      }
      if (addProp(OUT book, prop))
//            type_name, thisId, NULL, 0, 0, start_lnum, end_lnum, start_col, end_col) == FAIL)
         return;
   }

   drawBookLater(book, UPD_VALID);
}

// Get the next ID to use for a textprop with text in book.
private int
get_textprop_id(Book* book) {
   // TODO: recycle deleted entries
   return -(book->textPropText.len + 1);
}

// Flag that is set when a negative ID isused for a normal text property.
// It is then impossible to use virtual text properties.
private int didUseNegativePropIdS = FALSE;

//Shared between prop_add() and createPopup().
//"dict_arg" is the function argument of a dict containing "bufnr".
//it is NULL for createPopup(). Return the "id" used for "text" or zero.
int
prop_add_common(
   LineNr startLnum,
   ColNr startCol,
   Bag* dict,
   Book* defaultBook,
   Var* dictArg
){
   Book* book = defaultBook;
   int      id = 0;
   CS text = NULL;

   if (!dict || !bagHasKey(dict, tConst("type"))) {
      emsg(_(e_missing_property_type_name));
      goto theend;
   }
   Prop prop = {};
   prop.startLnum = startLnum;
   prop.startCol = startCol;

   if (bagHasKey(dict, tConst("end_lnum"))) {
      prop.endLnum = bagGetNumber(dict, tConst("end_lnum"));
      if (prop.endLnum < startLnum) {
         showErrFmtMsg(_(e_invalid_value_for_argument_str), S"end_lnum");
         goto theend;
      }
   } else
      prop.endLnum = startLnum;

   if (bagHasKey(dict, tConst("length"))) {
      long length = bagGetNumber(dict, tConst("length"));

      if (length < 0 || prop.endLnum > prop.startLnum) {
         showErrFmtMsg(_(e_invalid_value_for_argument_str), "length");
         goto theend;
      }
      prop.endCol = prop.startCol + length;
   } ei (bagHasKey(dict, tConst("end_col"))) {
      prop.endCol = bagGetNumber(dict, tConst("end_col"));
      if (prop.endCol <= 0) {
         showErrFmtMsg(_(e_invalid_value_for_argument_str), "end_col");
         goto theend;
      }
   } ei (prop.startLnum == prop.endLnum)
      prop.endCol = prop.startCol;
   else
      prop.endCol = 1;

   if (bagHasKey(dict, tConst("id"))) {
      Long x;
      x = bagGetNumber(dict, tConst("id"));
      if (x > INT_MAX || x  <= INT_MIN) {
         showErrFmtMsg(_(e_val_too_large), bagGetString(dict, tConst("id"), false));
         goto theend;
      }
      prop.id = (int)x;
   }

   if (bagHasKey(dict, tConst("text"))) {
      if (bagHasKey(dict, tConst("length")) 
            || bagHasKey(dict, tConst("end_col")) 
            || bagHasKey(dict, tConst("end_lnum"))
      ){
         emsg(_(e_cannot_use_length_endcol_and_endlnum_with_text));
         goto theend;
      }

      prop.text = bagGetString(dict, tConst("text"), true);
      if (prop.text == NULL)
         goto theend;
      // use a default length of 1 to make multiple props show up
      prop.endCol = startCol + 1;

      if (bagHasKey(dict, tConst("text_align"))) {
         CS p = bagGetString(dict, tConst("text_align"), false);
         if (!p)
            goto theend;
            
         if (prop.startCol != 0) {
            emsg(_(e_can_only_use_text_align_when_column_is_zero));
            goto theend;
         }
         if (STRCMP(p, "right") == 0)
            prop.textFlags |= TEXT_PROP_ALIGN_RIGHT;
         ei (STRCMP(p, "above") == 0)
            prop.textFlags |= TEXT_PROP_ALIGN_ABOVE;
         ei (STRCMP(p, "below") == 0)
            prop.textFlags |= TEXT_PROP_ALIGN_BELOW;
         ei (STRCMP(p, "after") != 0) {
            showErrFmtMsg(_(e_invalid_value_for_argument_str_str), "text_align", p);
            goto theend;
         }
      }

      if (bagHasKey(dict, tConst("text_padding_left"))) {
         int textPaddingLeft = bagGetNumber(dict, tConst("text_padding_left"));
         if (textPaddingLeft < 0) {
            showErrFmtMsg(_(e_argument_must_be_positive_str), "text_padding_left");
            goto theend;
         }
      }

      if (bagHasKey(dict, tConst("text_wrap"))) {
         CS p = bagGetString(dict, tConst("text_wrap"), false);
         if (!p)
            goto theend;
         if (STRCMP(p, "wrap") == 0)
            prop.textFlags |= TEXT_PROP_WRAP;
         ei (STRCMP(p, "truncate") != 0) {
            showErrFmtMsg(_(e_invalid_value_for_argument_str_str), "text_wrap", p);
            goto theend;
         }
      }
   }

   // Column must be 1 or more for a normal text property; when "text" is
   // present zero means it goes after the line.
   if (startCol < (text == NULL ? 1 : 0)) {
      showErrFmtMsg(_(e_invalid_column_number_nr), (long)startCol);
      goto theend;
   }
   if (startCol > 0 && prop.textPaddingLeft > 0) {
      emsg(_(e_can_only_use_left_padding_when_column_is_zero));
      goto theend;
   }

   if (dictArg && getBookNrFromArg(dictArg, &book) == FAIL)
      goto theend;

   if (id < 0) {
      if (book->textPropText.len > 0) {
         emsg(_(e_cannot_use_negative_id_after_adding_textprop_with_text));
         goto theend;
      }
      didUseNegativePropIdS = TRUE;
   }

   if (text) {
      if (didUseNegativePropIdS) {
         emsg(_(e_cannot_add_textprop_with_text_after_using_textprop_with_negative_id));
         goto theend;
      }
      id = get_textprop_id(book);
   }

   // This must be done _before_ we add the property because property changes
   // trigger book (memline) reorganization, which needs this flag to be correctly set.
   book->hasTextprop = TRUE;  // this is never reset

   addProp(OUT book, prop);
   text = NULL;

   drawBookLater(book, UPD_VALID);

theend:
    eeglFree(text);
    return id;
}

//Fetch the text properties for line "lnum" in book "book".
//Return the number of text properties and, when non-zero, a pointer to the
//first one in "props" (note that it is not aligned, therefore the Byte pointer).
int
get_text_props(OUT CS* props, Book* book, LineNr lnum, Boole will_change) {
   // Be quick when no text property types have been defined for the book,
   // unless we are adding one.
   if ((!book->hasTextprop && !will_change) || book->mem.mfile == NULL)
      return 0;

   // Fetch the line to get the lineLen field updated.
   CS text = memGetLine(book, lnum, will_change);
   Unt textlen = memGetBookLen(book, lnum) + 1;
   Unt proplen = book->mem.lineLen - textlen;
   if (proplen == 0)
      return 0;
   if (proplen % sizeof(TextProp) != 0) {
      internalErrMsg(e_text_property_info_corrupted);
      return 0;
   }
   *props = text + textlen;
   return (int)(proplen / sizeof(TextProp));
}

//Return the number of text properties with "above" or "below" alignment in
//line "lnum".  A "right" aligned property also goes below after a "below" or
//other "right" aligned property.
int
prop_count_above_below(Book* book, LineNr lnum) {
   CS props;
   int count = get_text_props(OUT &props, book, lnum, false);
   int result = 0;
   TextProp   prop;
   int      i;
   int      next_right_goes_below = FALSE;

   if (count == 0)
      return 0;
   for (i = 0; i < count; ++i) {
      mch_memmove(&prop, props + i * sizeof(prop), sizeof(prop));
      if (prop.col == MAXCOL && text_prop_type_valid(book, &prop)) {
         if ((prop.flags & (TEXT_PROP_ALIGN_ABOVE | TEXT_PROP_ALIGN_BELOW))
             || (next_right_goes_below && (prop.flags & TEXT_PROP_ALIGN_RIGHT))
         ){
            ++result;
         } ei (prop.flags & TEXT_PROP_ALIGN_RIGHT)
            next_right_goes_below = TRUE;
      }
   }
   return result;
}

//Return the number of text properties on line "lnum" in the current book.
//When "only_starting" is true only text properties starting in this line will be considered.
//When "last_line" is FALSE then text properties after the line are not counted.
int
count_props(LineNr lnum, int only_starting, int last_line) {
   CS props;
   int      proplen = get_text_props(OUT &props, curBook, lnum, false);
   int      result = proplen;
   int      i;
   TextProp   prop;

   for (i = 0; i < proplen; ++i) {
      mch_memmove(&prop, props + i * sizeof(prop), sizeof(prop));
      // A prop is dropped when in the first line and it continues from the
      // previous line, or when not in the last line and it is virtual text after the line.
      if ((only_starting && (prop.flags & TEXT_PROP_CONT_PREV))
         || (!last_line && prop.col == MAXCOL))
          --result;
   }
   return result;
}

private TextProp   *text_prop_compare_props;
private Book      *text_prop_compare_buf;

//Score for sorting on position of the text property: 0: above,
//1: after (default), 2: right, 3: below (comes last)
private int
text_prop_order(int flags) {
   if (flags & TEXT_PROP_ALIGN_ABOVE)
      return 0;
   if (flags & TEXT_PROP_ALIGN_RIGHT)
      return 2;
   if (flags & TEXT_PROP_ALIGN_BELOW)
      return 3;
   return 1;
}

//Function passed to qsort() to sort text properties.
//Return 1 if "s1" has priority over "s2", -1 if the other way around, zero if
//both have the same priority.
private int
text_prop_compare(const void *s0, const void *s1) {
   int  idx0, idx1;
   TextProp   *tp0, *tp1;
   PropType  *pt0, *pt1;
   ColNr col0, col1;

   idx0 = *(int *)s0;
   idx1 = *(int *)s1;
   tp0 = &text_prop_compare_props[idx0];
   tp1 = &text_prop_compare_props[idx1];
   col0 = tp0->col;
   col1 = tp1->col;

   // property that inserts text has priority over one that doesn't
   if ((tp0->id < 0) != (tp1->id < 0))
      return tp0->id < 0 ? 1 : -1;

   if (col0 == MAXCOL || col1 == MAXCOL) {
   int order0 = text_prop_order(tp0->flags);
   int order1 = text_prop_order(tp1->flags);

   if (order0 != order1)
       return order0 < order1 ? 1 : -1;
   }

   // check highest priority, defined by the type
   pt0 = text_prop_type_by_id(text_prop_compare_buf, tp1->type);
   pt1 = text_prop_type_by_id(text_prop_compare_buf, tp1->type);
   if (pt0 != pt1) {
      if (pt0 == NULL)
          return -1;
      if (pt1 == NULL)
          return 1;
      if (pt0->priority != pt1->priority)
          return pt0->priority > pt1->priority ? 1 : -1;
    }

   // same priority, one that starts first wins
   if (col0 != col1)
      return col0 < col1 ? 1 : -1;

   // for a property with text the id can be used as tie breaker
   if (tp0->id < 0)
      return tp0->id > tp1->id ? 1 : -1;

   return 0;
}

//Sort "count" text properties using an array if indexes "idxs" into the list
//of text props "props" for book "book".
void
sort_text_props(
   Book* book,
   TextProp* props,
   int* idxs,
   int count
) {
   text_prop_compare_buf = book;
   text_prop_compare_props = props;
   qsort((void *)idxs, (Unt)count, sizeof(int), text_prop_compare);
}

//Find text property "type_id" in the visible lines of portal "wp".
//Match "id" when it is > 0. Return FAIL when not found.
int
find_visible_prop(
   Portal       *wp,
   int       type_id,
   int       id,
   TextProp  *prop,
   LineNr    *found_lnum
) {
   // return when "type_id" no longer exists
   if (text_prop_type_by_id(wp->book, type_id) == NULL)
      return FAIL;

   // bottomLine may not have been updated yet.
   validate_botline_win(wp);
   for (LineNr lnum = wp->topLine; lnum < wp->bottomLine; ++lnum) {
      CS props;
      int   count = get_text_props(OUT &props, wp->book, lnum, false);
      for (int i = 0; i < count; ++i) {
         mch_memmove(prop, props + i * sizeof(TextProp), sizeof(TextProp));
         if (prop->type == type_id && (id <= 0 || prop->id == id)) {
            *found_lnum = lnum;
            return OK;
         }
      }
   }
   return FAIL;
}

//Set the text properties for line "lnum" to "props" with length "len".
//If "len" is zero text properties are removed, "props" is not used.
//Any existing text properties are dropped. Only work for the current book.
private void
set_text_props(LineNr lnum, CS props, int len) {
   CS text = ml_get(lnum);
   int textlen = ml_get_len(lnum) + 1;
   CS newtext = alloc(textlen + len);
   mch_memmove(newtext, text, textlen);
   if (len > 0)
      mch_memmove(newtext + textlen, props, len);
   if ((curBook->mem.flags & ML_LINE_DIRTY) != 0)
      eeglFree(curBook->mem.cachedLine);
   curBook->mem.cachedLine = newtext;
   curBook->mem.lineLen = textlen + len;
   curBook->mem.flags |= ML_LINE_DIRTY;
}

//Add "text_props" with "text_prop_count" text properties to line "lnum".
void
add_text_props(LineNr lnum, TextProp *text_props, int text_prop_count) {
   int       proplen = text_prop_count * (int)sizeof(TextProp);

   CS text = ml_get(lnum);
   CS newtext = alloc(curBook->mem.lineLen + proplen);
   mch_memmove(newtext, text, curBook->mem.lineLen);
   mch_memmove(newtext + curBook->mem.lineLen, text_props, proplen);
   if ((curBook->mem.flags & ML_LINE_DIRTY) != 0)
      eeglFree(curBook->mem.cachedLine);
   curBook->mem.cachedLine = newtext;
   curBook->mem.lineLen += proplen;
   curBook->mem.flags |= ML_LINE_DIRTY;
}

//Function passed to qsort() for sorting PropType on id.
private int
compare_pt(void const * s0, void const* s1) {
   PropType* tp0 = *(PropType **)s0;
   PropType* tp1 = *(PropType **)s1;

   return tp0->id == tp1->id ? 0 : tp0->id < tp1->id ? -1 : 1;
}

private PropType*
find_type_by_id(EeSet* ht, PropType*** array, int id) {
   if (!ht || ht->count == 0)
      return NULL;

   int low = 0;
   int high;
   // Make the lookup faster by creating an array with pointers to
   // hashtable entries, sorted on id.
   if (*array == NULL) {
      EeSetItem  *hi;
      int       i = 0;

      *array = ALLOC_MULT(PropType *, ht->count);
      long todo = (long)ht->count;
      FOR_ALL_HASHTAB_ITEMS(ht, hi, todo) {
         if (!HASHITEM_EMPTY(hi)) {
            (*array)[i++] = HI2PT(hi);
            --todo;
         }
      }
      qsort((void *)*array, ht->count, sizeof(PropType *), compare_pt);
   }

   // binary search in the sorted array
   high = ht->count;
   while (high > low) {
      int m = (high + low) / 2;

      if ((*array)[m]->id == id)
         return (*array)[m];
      if ((*array)[m]->id > id)
         high = m;
      else
         low = m + 1;
    }
    return NULL;
}

// Fill 'dict' with text properties in 'prop'.
private void
prop_fill_dict(Bag* dict, TextProp* prop, Book* book) {
   PropType *pt;
   int buflocal = TRUE;
   int virtualtext_prop = prop->id < 0;

   bagAddNumber(dict, S"col", (prop->col == MAXCOL) ? 0 : prop->col);
   if (!virtualtext_prop) {
      bagAddNumber(dict, S"length", prop->len);
      bagAddNumber(dict, S"id", prop->id);
   }
   bagAddNumber(dict, S"start", !(prop->flags & TEXT_PROP_CONT_PREV));
   bagAddNumber(dict, S"end", !(prop->flags & TEXT_PROP_CONT_NEXT));

   pt = find_type_by_id(book->propTypes, &book->propArray, prop->type);
   if (!pt) {
      pt = find_type_by_id(global_proptypes, &global_proparray, prop->type);
      buflocal = FALSE;
   }
   if (pt)
      bagAddString(dict, S"type", pt->name);

   if (buflocal)
      bagAddNumber(dict, S"type_bufnr", book->fiNum);
   else
      bagAddNumber(dict, S"type_bufnr", 0);
   if (virtualtext_prop) {
      // virtual text property
      ArrayList    *gap = &book->textPropText;

      // negate the property id to get the string index
      CS text = ((Byte **)gap->c)[-prop->id - 1];
      bagAddString(dict, S"text", text);

      // text_align
      CS text_align = NULL;
      if (prop->flags & TEXT_PROP_ALIGN_RIGHT)
          text_align = S"right";
      ei (prop->flags & TEXT_PROP_ALIGN_ABOVE)
          text_align = S"above";
      ei (prop->flags & TEXT_PROP_ALIGN_BELOW)
          text_align = S"below";
      if (text_align != NULL)
          bagAddString(dict, S"text_align", text_align);

      // text_wrap
      if (prop->flags & TEXT_PROP_WRAP)
          bagAddString(dict, S"text_wrap", S"wrap");
      if (prop->leftPad != 0)
          bagAddNumber(dict, S"text_padding_left", prop->leftPad);
   }
}

// Find a property type by ID in "book" or globally. Returns NULL if not found.
PropType *
text_prop_type_by_id(Book* book, int id) {
   PropType* ty = find_type_by_id(book->propTypes, &book->propArray, id);
   if (!ty)
      ty = find_type_by_id(global_proptypes, &global_proparray, id);
   return ty;
}

// Return TRUE if "prop" is a valid text property type.
private int
text_prop_type_valid(Book* book, TextProp *prop) {
   return text_prop_type_by_id(book, prop->type) != NULL;
}

//prop_clear({lnum} [, {lnum_end} [, {bufnr}]])
void
f_prop_clear(Var *argvars, Var *returnVar UNUSED) {
   Book    *book = curBook;
   int       did_clear = FALSE;

   LineNr start = tv_get_number(&argvars[0]);
   LineNr end = start;
   if (argvars[1].tag != VAR_UNKNOWN) {
      end = tv_get_number(&argvars[1]);
      if (argvars[2].tag != VAR_UNKNOWN && getBookNrFromArg(&argvars[2], &book) == FAIL)
         return;
   }
   if (start < 1 || end < 1) {
      emsg(_(e_invalid_range));
      return;
   }

   for (LineNr lnum = start; lnum <= end; ++lnum) {
      if (lnum > book->mem.lineCount)
         break;
      CS text = memGetLine(book, lnum, FALSE);
      Unt len = memGetBookLen(book, lnum) + 1;
      if ((Unt)book->mem.lineLen > len) {
         did_clear = TRUE;
         if (!(book->mem.flags & ML_LINE_DIRTY)) {
            CS newtext = copyStr(text);

            book->mem.cachedLine = newtext;
            book->mem.flags |= ML_LINE_DIRTY;
         }
         book->mem.lineLen = (int)len;
      }
   }
   if (did_clear)
      drawBookLater(book, UPD_NOT_VALID);
}

//prop_find({props} [, {direction}])
void
f_prop_find(Var *argvars, Var *returnVar) {
   Pos       *cursor = &curPor->cursor;
   Book       *book = curBook;
   int      start_pos_has_prop = 0;
   int      seen_end = FALSE;
   int      id = 0;
   int      id_found = FALSE;
   int      type_id = -1;
   int      lnum = -1;
   int      col = -1;
   Unt      dir = FORWARD;    // FORWARD == 1, BACKWARD == -1
   int      both;

   if (check_for_nonnull_dict_arg(argvars, 0) == FAIL)
      return;
   Bag* b = argvars[0].bag;

   if (getBookNrFromArg(&argvars[0], &book) == FAIL)
      return;
   if (book->mem.mfile == NULL)
      return;

   if (argvars[1].tag != VAR_UNKNOWN) {
      CS dir_s = tv_get_string(&argvars[1]);

      if (*dir_s == 'b')
         dir = BACKWARD;
      ei (*dir_s != 'f') {
         emsg(_(e_invalid_argument));
         return;
      }
   }

   DictItem* di = bagFind(b, tConst("lnum"));
   if (di)
      lnum = tv_get_number(&di->c);

   di = bagFind(b, tConst("col"));
   if (di != NULL)
      col = tv_get_number(&di->c);

   if (lnum == -1) {
      lnum = cursor->lnum;
      col = cursor->col + 1;
   } ei (col == -1)
      col = 1;

   if (lnum < 1 || lnum > book->mem.lineCount) {
      emsg(_(e_invalid_range));
      return;
   }

   Boole skipstart = bagGetBool(b, tConst("skipstart"), false);

   if (bagHasKey(b, tConst("id"))) {
      id = bagGetNumber(b, tConst("id"));
      id_found = TRUE;
   }
   if (bagHasKey(b, tConst("type"))) {
      CS name = bagGetString(b, tConst("type"), false);
      PropType* type = lookup_prop_type(mbText(name), book);

      if (type == NULL)
         return;
      type_id = type->id;
   }
   both = bagGetBool(b, tConst("both"), false);
   if (!id_found && type_id == -1) {
      emsg(_(e_need_at_least_one_of_id_or_type));
      return;
   }
   if (both && (!id_found || type_id == -1)) {
      emsg(_(e_need_id_and_type_or_types_with_both));
      return;
   }

   int lnum_start = lnum;

   allocReturnDict(returnVar);

   while (1) {
      CS text = memGetLine(book, lnum, FALSE);
      Unt   textlen = memGetBookLen(book, lnum) + 1;
      int   count = (int)((book->mem.lineLen - textlen) / sizeof(TextProp));
      int       i;
      TextProp  prop;
      int       prop_start;
      int       prop_end;

      for (i = dir == BACKWARD ? count - 1 : 0; i >= 0 && i < count; i += dir) {
          mch_memmove(&prop, text + textlen + i * sizeof(TextProp),
                           sizeof(TextProp));

         // For the very first line try to find the first property before or
         // after `col`, depending on the search direction.
         if (lnum == lnum_start) {
            if (dir == BACKWARD) {
               if (prop.col > col)
                  continue;
            } ei (prop.col + prop.len - (prop.len != 0) < col)
               continue;
         }
         if (both ? prop.id == id && prop.type == type_id
              : (id_found && prop.id == id) || prop.type == type_id
         ){
         // Check if the starting position has text props.
         if (lnum_start == lnum
               && col >= prop.col
               && (col <= prop.col + prop.len - (prop.len != 0))
         )
             start_pos_has_prop = 1;

         // The property was not continued from last line, it starts on
         // this line.
         prop_start = !(prop.flags & TEXT_PROP_CONT_PREV);
         // The property does not continue on the next line, it ends on
         // this line.
         prop_end = !(prop.flags & TEXT_PROP_CONT_NEXT);
         if (!prop_start && prop_end && dir == FORWARD)
             seen_end = 1;

         // Skip lines without the start flag.
         if (!prop_start) {
            // Always search backwards for start when search started
            // on a prop and we're not skipping.
            if (start_pos_has_prop && !skipstart)
               dir = BACKWARD;
            continue;
         }

         // If skipstart is true, skip the prop at start pos (even if
         // continued from another line).
         if (start_pos_has_prop && skipstart && !seen_end) {
             start_pos_has_prop = 0;
             continue;
         }

         prop_fill_dict(returnVar->bag, &prop, book);
         bagAddNumber(returnVar->bag, S"lnum", lnum);

         return;
          }
      }

      if (dir > 0) {
         if (lnum >= book->mem.lineCount)
            break;
         lnum++;
      } else {
         if (lnum <= 1)
            break;
         lnum--;
      }
   }
}

//Return TRUE if 'type_or_id' is in the 'types_or_ids' list.
private int
prop_type_or_id_in_list(int *types_or_ids, int len, int type_or_id) {
   for (int i = 0; i < len; i++) {
      if (types_or_ids[i] == type_or_id)
          return TRUE;
   } 
   return FALSE;
}

//Return all the text properties in line 'lnum' in book 'book' in 'retlist'.
//If 'prop_types' is not NULL, then return only the text properties with
//matching property type in the 'prop_types' array.
//If 'prop_ids' is not NULL, then return only the text properties with
//an identifier in the 'props_ids' array.
//If 'add_lnum' is TRUE, then add the line number also to the text property dictionary.
private void
get_props_in_line(
   Book      *book,
   LineNr   lnum,
   int      *prop_types,
   int      prop_types_len,
   int      *prop_ids,
   int      prop_ids_len,
   List      *retlist,
   int      add_lnum)
{
   CS text = memGetLine(book, lnum, FALSE);
   Unt   textlen = memGetBookLen(book, lnum) + 1;
   int      i;
   TextProp   prop;

   int count = (int)((book->mem.lineLen - textlen) / sizeof(TextProp));
   for (i = 0; i < count; ++i) {
      mch_memmove(&prop, text + textlen + i * sizeof(TextProp),
         sizeof(TextProp));
      if ((prop_types == NULL
             || prop_type_or_id_in_list(prop_types, prop_types_len, prop.type))
         && (prop_ids == NULL || prop_type_or_id_in_list(prop_ids, prop_ids_len, prop.id))
      ){
         Bag *b = allocBag();
         prop_fill_dict(b, &prop, book);
         if (add_lnum)
            bagAddNumber(b, S"lnum", lnum);
         listAppendBag(retlist, b);
      }
   }
}

//Convert a List of property type names into an array of property type
//identifiers. Returns a pointer to the allocated array. Returns NULL on
//error. 'num_types' is set to the number of returned property types.
private int *
get_prop_types_from_names(List *l, Book* book, OUT int *num_types) {
   *num_types = 0;

   Arr(int) prop_types = ALLOC_MULT(int, list_len(l));

   int i = 0;
   ListItem   *li;
   FOR_ALL_LIST_ITEMS(l, li) {
      if (li->c.tag != VAR_STRING) {
          emsg(_(e_string_required));
          goto errret;
      }
      CS name = li->c.string;
      if (!name)
          goto errret;

      PropType* type = lookup_prop_type(text(name), book);
      if (!type)
          goto errret;
      prop_types[i++] = type->id;
   }

   *num_types = i;
   return prop_types;

errret:
   EE_CLEAR(prop_types);
   return NULL;
}

//Convert a List of property identifiers into an array of property
//identifiers.  Returns a pointer to the allocated array. Returns NULL on
//error. 'num_ids' is set to the number of returned property identifiers.
private int*
get_prop_ids_from_list(List *l, OUT int* countIds) {
   ListItem   *li;
   int      i = 0;
   int      id;
   Boole error = false;

   *countIds = 0;

   Arr(int) prop_ids = ALLOC_MULT(int, list_len(l));

   CHECK_LIST_MATERIALIZE(l);
   FOR_ALL_LIST_ITEMS(l, li) {
      error = FALSE;
      id = varGetNumberChk(&li->c, OUT &error);
      if (error)
         goto errret;

      prop_ids[i++] = id;
   }

   *countIds = i;
   return prop_ids;

errret:
   EE_CLEAR(prop_ids);
   return NULL;
}

//prop_list({lnum} [, {bufnr}])
void
f_prop_list(Var *argvars, Var *returnVar) {
   LineNr   lnum;
   Book* book = curBook;
   int      add_lnum = FALSE;
   int      *prop_types = NULL;
   int      prop_types_len = 0;
   int      *prop_ids = NULL;
   int      prop_ids_len = 0;
   List   *l;
   DictItem   *di;

   allocReturnList(returnVar);

   // default: get text properties on current line
   LineNr start_lnum = tv_get_number(&argvars[0]);
   LineNr end_lnum = start_lnum;
   if (argvars[1].tag != VAR_UNKNOWN) {

      if (check_for_dict_arg(argvars, 1) == FAIL)
         return;
      Bag* d = argvars[1].bag;

      if (getBookNrFromArg(&argvars[1], &book) == FAIL)
         return;

      if (d && (di = bagFind(d, tConst("end_lnum"))) != NULL) {
         if (di->c.tag != VAR_NUMBER) {
            emsg(_(e_number_required));
            return;
         }
         end_lnum = tv_get_number(&di->c);
         if (end_lnum < 0)
            // negative end_lnum is used as an offset from the last book line
            end_lnum = book->mem.lineCount + end_lnum + 1;
         ei (end_lnum > book->mem.lineCount)
            end_lnum = book->mem.lineCount;
         add_lnum = TRUE;
      }
      if (d != NULL && (di = bagFind(d, tConst("types"))) != NULL) {
         if (di->c.tag != VAR_LIST) {
            emsg(_(e_list_required));
            return;
         }

         l = di->c.list;
         if (l != NULL && list_len(l) > 0) {
            prop_types = get_prop_types_from_names(l, book, &prop_types_len);
            if (prop_types == NULL)
                return;
         }
      }
      if (d != NULL && (di = bagFind(d, tConst("ids"))) != NULL) {
         if (di->c.tag != VAR_LIST) {
            emsg(_(e_list_required));
            goto errret;
         }

         l = di->c.list;
         if (l && list_len(l) > 0) {
            prop_ids = get_prop_ids_from_list(l, OUT &prop_ids_len);
            if (!prop_ids)
               goto errret;
         }
      }
   }
   if (start_lnum < 1 || start_lnum > book->mem.lineCount
      || end_lnum < 1 || end_lnum < start_lnum)
      emsg(_(e_invalid_range));
   else {
      for (lnum = start_lnum; lnum <= end_lnum; lnum++)
          get_props_in_line(book, lnum, prop_types, prop_types_len,
             prop_ids, prop_ids_len,
             returnVar->list, add_lnum);
   } 

errret:
   EE_CLEAR(prop_types);
   EE_CLEAR(prop_ids);
}

// prop_remove({props} [, {lnum} [, {lnum_end}]])
void
f_prop_remove(Var *argvars, Var *returnVar) {
   LineNr   start = 1;
   LineNr   end = 0;
   LineNr   lnum;
   LineNr   first_changed = 0;
   LineNr   last_changed = 0;
   Book   *book = curBook;
   int      do_all;
   int      id = -MAXCOL;
   int      type_id = -1;       // for a single "type"
   int      *typeIds = NULL;   // array, for a list of "types", allocated
   int      num_typeIds = 0;   // number of elements in "typeIds"
   int      both;
   int      did_remove_text = FALSE;

   returnVar->number = 0;

   if (check_for_nonnull_dict_arg(argvars, 0) == FAIL)
      return;

   if (argvars[1].tag != VAR_UNKNOWN) {
      start = tv_get_number(&argvars[1]);
      end = start;
      if (argvars[2].tag != VAR_UNKNOWN)
          end = tv_get_number(&argvars[2]);
      if (start < 1 || end < 1) {
         emsg(_(e_invalid_range));
         return;
      }
   }

   Bag* dict = argvars[0].bag;
   if (getBookNrFromArg(&argvars[0], &book) == FAIL || book->mem.mfile == NULL)
      return;

   do_all = bagGetBool(dict, tConst("all"), false);

   if (bagHasKey(dict, tConst("id")))
      id = bagGetNumber(dict, tConst("id"));

   // if a specific type was supplied "type": check that (and ignore "types".
   // Otherwise check against the list of "types".
   if (bagHasKey(dict, tConst("type"))) {
      CS name = bagGetString(dict, tConst("type"), false);
      PropType  *type = lookup_prop_type(mbText(name), book);

      if (!type)
         return;
      type_id = type->id;
   }
   if (bagHasKey(dict, tConst("types"))) {
      Var types;
      ListItem *li = NULL;

      bagGetVar(dict, tConst("types"), &types);
      if (types.tag == VAR_LIST && types.list->len > 0) {
         typeIds = alloc( sizeof(int) * types.list->len );
         FOR_ALL_LIST_ITEMS(types.list, li) {
            PropType *prop_type;

            if (li->c.tag != VAR_STRING)
              continue;

            prop_type = lookup_prop_type(mbText(li->c.string), book);

            if (!prop_type)
               goto cleanup_prop_remove;

            typeIds[num_typeIds++] = prop_type->id;
         }
      }
   }
   both = bagGetBool(dict, tConst("both"), false);

   if (id == -MAXCOL && (type_id == -1 && num_typeIds == 0)) {
      emsg(_(e_need_at_least_one_of_id_or_type));
      goto cleanup_prop_remove;
   }
   if (both && (id == -MAXCOL || (type_id == -1 && num_typeIds == 0))) {
      emsg(_(e_need_id_and_type_or_types_with_both));
      goto cleanup_prop_remove;
   }
   if (type_id != -1 && num_typeIds > 0) {
      emsg(_(e_cannot_specify_both_type_and_types));
      goto cleanup_prop_remove;
   }

   if (end == 0)
      end = book->mem.lineCount;
   for (lnum = start; lnum <= end; ++lnum) {
      Unt len;

      if (lnum > book->mem.lineCount)
         break;
      len = memGetBookLen(book, lnum) + 1;
      if ((Unt)book->mem.lineLen > len) {
         static TextProp   textprop;  // static because of alignment
         unsigned      idx;

         for (idx = 0; idx < (book->mem.lineLen - len) / sizeof(TextProp); ++idx) {
            CS cur_prop = book->mem.cachedLine + len + idx * sizeof(TextProp);
            Unt   taillen;
            int matches_id = 0;
            int matchty = 0;

            mch_memmove(&textprop, cur_prop, sizeof(TextProp));

            matches_id = textprop.id == id;
            if (num_typeIds > 0) {
               for (int idx2 = 0; !matchty && idx2 < num_typeIds; ++idx2)
                  matchty = textprop.type == typeIds[idx2];
            } else {
               matchty = textprop.type == type_id;
            }

            if (both ? matches_id && matchty : matches_id || matchty) {
               if (!(book->mem.flags & ML_LINE_DIRTY)) {
                  // need to allocate the line to be able to change it
                  CS newptr = alloc(book->mem.lineLen);
                  mch_memmove(newptr, book->mem.cachedLine, book->mem.lineLen);
                  book->mem.cachedLine = newptr;
                  book->mem.flags |= ML_LINE_DIRTY;

                  cur_prop = book->mem.cachedLine + len + idx * sizeof(TextProp);
               }

               taillen = book->mem.lineLen - len - (idx + 1) * sizeof(TextProp);
               if (taillen > 0)
                  mch_memmove(cur_prop, cur_prop + sizeof(TextProp), taillen);
               book->mem.lineLen -= sizeof(TextProp);
               --idx;

               if (textprop.id < 0) {
                  ArrayList    *gap = &book->textPropText;
                  int       ii = -textprop.id - 1;

                  // negative ID: property with text - free the text
                  if (ii < gap->len) {
                     Byte **p = ((Byte **)gap->c) + ii;
                     EE_CLEAR(*p);
                     did_remove_text = TRUE;
                  }
               }

               if (first_changed == 0)
                  first_changed = lnum;
               last_changed = lnum;
               ++returnVar->number;
               if (!do_all)
                  break;
            }
         }
      }
   }

   if (first_changed > 0) {
      normInvalidateDisplayOfChangedBookLine(book);
      changed_lines_buf(book, first_changed, last_changed + 1, 0);
      drawBookLater(book, UPD_VALID);
   }

   if (did_remove_text) {
      ArrayList* lst = &book->textPropText;

      // Reduce the arraylist size for NULL pointers at the end.
      while (lst->len > 0 && ((Byte **)lst->c)[lst->len - 1] == NULL)
          --lst->len;
   }

cleanup_prop_remove:
    eeglFree(typeIds);
}

//Common for f_prop_type_add() and f_prop_type_change().
private void
prop_type_set(Var *argvars, int add) {
   CS name;
   Book   *book = NULL;
   DictItem  *di;
   name = tv_get_string(&argvars[0]);
   if (*name == ZERO) {
      showErrFmtMsg(_(e_invalid_argument_str), "\"\"");
      return;
   }

   if (getBookNrFromArg(&argvars[1], &book) == FAIL)
      return;
   Bag* dict = argvars[1].bag;

   PropType* prop = findPropTypeByName(text(name), book);
   if (add) {
      EeSet **htp;

      if (prop) {
         showErrFmtMsg(_(e_property_type_str_already_defined), name);
         return;
      }
      prop = allocZeroed(offsetof(PropType, name) + STRLEN(name) + 1);
      STRCPY(prop->name, name);
      prop->id = ++proptype_id;
      prop->flags = PT_FLAG_COMBINE;
      if (book == NULL) {
         htp = &global_proptypes;
         EE_CLEAR(global_proparray);
      } else {
         htp = &book->propTypes;
         EE_CLEAR(book->propArray);
      }
      if (*htp == NULL) {
         *htp = ALLOC_ONE(EeSet);
         hash_init(*htp);
      }
      hash_add(*htp, PT2HIKEY(prop), S"prop type");
   } else {
      if (prop == NULL) {
         showErrFmtMsg(_(e_property_type_str_does_not_exist), name);
         return;
      }
   }

   if (dict) {
      di = bagFind(dict, tConst("highlight"));
      if (di) {
         Short hiId = SHORT;

         CS hiliteName = bagGetString(dict, tConst("highlight"), false);
         if (hiliteName && *hiliteName != ZERO)
            hiId = hiliteGroupByName(mbText(hiliteName));
         if (hiId == SHORT) {
            showErrFmtMsg(_(e_unknown_highlight_group_name_str), hiliteName ? hiliteName : E);
            return;
         }
         prop->hilite = hiId;
      }

      di = bagFind(dict, tConst("combine"));
      if (di) {
         if (tv_get_bool(&di->c))
            prop->flags |= PT_FLAG_COMBINE;
         else
            prop->flags &= ~PT_FLAG_COMBINE;
      }

      di = bagFind(dict, tConst("override"));
      if (di) {
         if (tv_get_bool(&di->c))
            prop->flags |= PT_FLAG_OVERRIDE;
         else
            prop->flags &= ~PT_FLAG_OVERRIDE;
      }

      di = bagFind(dict, tConst("priority"));
      if (di)
         prop->priority = tv_get_number(&di->c);

      di = bagFind(dict, tConst("start_incl"));
      if (di) {
         if (tv_get_bool(&di->c))
            prop->flags |= PT_FLAG_INS_START_INCL;
         else
            prop->flags &= ~PT_FLAG_INS_START_INCL;
      }

      di = bagFind(dict, tConst("end_incl"));
      if (di) {
         if (tv_get_bool(&di->c))
            prop->flags |= PT_FLAG_INS_END_INCL;
         else
            prop->flags &= ~PT_FLAG_INS_END_INCL;
      }
   }
}

//prop_type_add({name}, {props})
void
f_prop_type_add(Var *argvars, Var *returnVar UNUSED) {
   prop_type_set(argvars, TRUE);
}

//prop_type_change({name}, {props})
void
f_prop_type_change(Var *argvars, Var *returnVar UNUSED) {
   prop_type_set(argvars, FALSE);
}

//prop_type_delete({name} [, {bufnr}])
void
f_prop_type_delete(Var *argvars, Var *returnVar UNUSED) {
   Book   *book = NULL;

   CS name = tv_get_string(&argvars[0]);
   if (*name == ZERO) {
      showErrFmtMsg(_(e_invalid_argument_str), "\"\"");
      return;
   }

   if (argvars[1].tag != VAR_UNKNOWN && getBookNrFromArg(&argvars[1], &book) == FAIL)
      return;

   EeSetItem* hi = findPropTypeHash(text(name), book);
   if (!hi)
      return;

   EeSet* ht;
   PropType   *prop = HI2PT(hi);

   if (!book) {
      ht = global_proptypes;
      EE_CLEAR(global_proparray);
   } else {
      ht = book->propTypes;
      EE_CLEAR(book->propArray);
   }
   hash_remove(ht, hi, S"prop type delete");
   eeglFree(prop);

   // currently visible text properties will disappear
   redraw_all_later(UPD_CLEAR);
   didChangePortalSettingBuf(book ? book : curBook);
}

//prop_type_get({name} [, {props}])
void
f_prop_type_get(Var *argvars, Var *returnVar) {
   CS name = tv_get_string(&argvars[0]);
   if (*name == ZERO) {
      showErrFmtMsg(_(e_invalid_argument_str), "\"\"");
      return;
   }

   allocReturnDict(returnVar);

   Book* book = NULL;
   if (argvars[1].tag != VAR_UNKNOWN && getBookNrFromArg(&argvars[1], OUT &book) == FAIL)
      return;

   PropType* prop = findPropTypeByName(text(name), book);
   if (!prop)
      return;

   Bag *d = returnVar->bag;

   if (prop->hilite != SHORT)
      bagAddString(d, S"highlight", syn_id2name(prop->hilite));
   bagAddNumber(d, S"priority", prop->priority);
   bagAddNumber(d, S"combine", (prop->flags & PT_FLAG_COMBINE) ? 1 : 0);
   bagAddNumber(d, S"start_incl", (prop->flags & PT_FLAG_INS_START_INCL) ? 1 : 0);
   bagAddNumber(d, S"end_incl", (prop->flags & PT_FLAG_INS_END_INCL) ? 1 : 0);
   if (book)
      bagAddNumber(d, S"bufnr", book->fiNum);
}

private void
list_types(EeSet *ht, List *l) {
   long todo = (long)ht->count;
   EeSetItem   *hi;
   FOR_ALL_HASHTAB_ITEMS(ht, hi, todo) {
     if (!HASHITEM_EMPTY(hi)) {
        PropType *prop = HI2PT(hi);

        list_append_string(l, prop->name, -1);
        --todo;
      }
   }
}

// prop_type_list([{bufnr}])
void
f_prop_type_list(Var *argvars, Var *returnVar) {
   allocReturnList(returnVar);

   Book* book = NULL;
   if (argvars[0].tag != VAR_UNKNOWN) {
      if (getBookNrFromArg(&argvars[0], &book) == FAIL)
          return;
   }
   if (!book) {
      if (global_proptypes != NULL)
         list_types(global_proptypes, returnVar->list);
   } ei (book->propTypes != NULL)
      list_types(book->propTypes, returnVar->list);
}

// Free all property types in "ht".
private void
clear_ht_prop_types(EeSet *ht) {
    long   todo;
    EeSetItem   *hi;

   if (!ht)
      return;

   todo = (long)ht->count;
   FOR_ALL_HASHTAB_ITEMS(ht, hi, todo) {
      if (!HASHITEM_EMPTY(hi)) {
          PropType *prop = HI2PT(hi);

          eeglFree(prop);
          --todo;
      }
   }

   hash_clear(ht);
   eeglFree(ht);
}

#if defined(EXITFREE) || defined(PROTO)
// Free all global property types.
void
clear_global_prop_types(void) {
   clear_ht_prop_types(global_proptypes);
   global_proptypes = NULL;
   EE_CLEAR(global_proparray);
}
#endif

// Free all property types for "book".
private void
clearPropTypes(Book* book) {
   clear_ht_prop_types(book->propTypes);
   book->propTypes = NULL;
   EE_CLEAR(book->propArray);
}

// Struct used to return two values from adjust().
typedef struct {
   int dirty;      // if the property was changed
   int mayDrop;   // whether after this change, the prop may be removed
} AdjustRes;

//Adjust the property for "added" bytes (can be negative) inserted at "col".
//
//Note that "col" is zero-based, while col is one-based. Only for the current book.
//"flags" can have:
//APC_SUBSTITUTE:   Text is replaced, not inserted.
//APC_INDENT:      Text is inserted before virtual text prop
private AdjustRes
adjust(
   TextProp  *prop,
   ColNr       col,
   int       added,
   int       flags
) {
   PropType   *pt;
   int      start_incl;
   int      end_incl;
   int      droppable;
   AdjustRes res = {TRUE, FALSE};

   // prop after end of the line doesn't move
   if (prop->col == MAXCOL) {
      res.dirty = FALSE;
      return res;
   }

   pt = text_prop_type_by_id(curBook, prop->type);
   start_incl = (pt != NULL && (pt->flags & PT_FLAG_INS_START_INCL))
            || (flags & APC_SUBSTITUTE)
            || (prop->flags & TEXT_PROP_CONT_PREV);
   if (prop->id < 0 && (flags & APC_INDENT))
      // when inserting indent just before a character with virtual text
      // shift the text property
      start_incl = FALSE;
   end_incl = (pt != NULL && (pt->flags & PT_FLAG_INS_END_INCL))
            || (prop->flags & TEXT_PROP_CONT_NEXT);
   // do not drop zero-width props if they later can increase in size
   droppable = !(start_incl || end_incl);

   if (added > 0) {
      if (col + 1 <= prop->col - (start_incl || (prop->len == 0 && end_incl)))
         // Change is entirely before the text property: Only shift
         prop->col += added;
      ei (col + 1 < prop->col + prop->len + end_incl)
         // Insertion was inside text property
         prop->len += added;
   } ei (prop->col > col + 1) {
      if (prop->col + added < col + 1) {
         prop->len += (prop->col - 1 - col) + added;
         prop->col = col + 1;
         if (prop->len <= 0) {
            prop->len = 0;
            res.mayDrop = droppable;
         }
      }
      else
          prop->col += added;
   } ei (prop->len > 0 && prop->col + prop->len > col
       && prop->id >= 0  // don't change length for virtual text
   ) {
      int after = col - added - (prop->col - 1 + prop->len);

      prop->len += after > 0 ? added + after : added;
      res.mayDrop = prop->len <= 0 && droppable;
   } else
      res.dirty = FALSE;

   return res;
}

//Adjust the columns of text properties in line "lnum" after position "col" to shift by 
//"bytes_added" (can be negative). Note that "col" is zero-based, while col is one-based.
//Only for the current book. "flags" can have:
//APC_SAVE_FOR_UNDO:   Call u_savesub() before making changes to the line.
//APC_SUBSTITUTE:   Text is replaced, not inserted.
//APC_INDENT:      Text is inserted before virtual text prop
//Caller is expected to check hasTextprop and "bytes_added" being non-zero.
//Return TRUE when props were changed.
Boole
adjustPropColumns(LineNr lnum, ColNr col, int bytes_added, Unt flags) {
   if (textPropFrozenG > 0)
      return false;

   CS props;
   int proplen = get_text_props(OUT &props, curBook, lnum, true);
   if (proplen == 0)
      return false;
   Unt textlen = curBook->mem.lineLen - proplen * sizeof(TextProp);

   int wi = 0; // write index
   Boole dirty = false;
   for (int ri = 0; ri < proplen; ++ri) {
      TextProp   prop;
      AdjustRes   res;
      mch_memmove(&prop, props + ri * sizeof(prop), sizeof(prop));
      res = adjust(&prop, col, bytes_added, flags);
      if (res.dirty) {
         // Save for undo if requested and not done yet.
         if ((flags & APC_SAVE_FOR_UNDO) && !dirty && u_savesub(lnum) == FAIL)
            return false;
         dirty = true;

         // u_savesub() may have updated curBook->mem, fetch it again
         if (curBook->mem.ml_line_lnum != lnum)
            proplen = get_text_props(OUT &props, curBook, lnum, true);
      }
      if (res.mayDrop)
         continue; // Drop this text property
      mch_memmove(props + wi * sizeof(TextProp), &prop, sizeof(TextProp));
      ++wi;
   }
   if (dirty) {
      ColNr newlen = (int)textlen + wi * (ColNr)sizeof(TextProp);
      if ((curBook->mem.flags & ML_LINE_DIRTY) == 0) {
         CS p = eeMemsave(curBook->mem.cachedLine, newlen);
         curBook->mem.cachedLine = p;
      }
      curBook->mem.flags |= ML_LINE_DIRTY;
      curBook->mem.lineLen = newlen;
   }
   return dirty;
}

//Adjust text properties for a line that was split in two.
//"lnumProps" is the line that has the properties from before the split.
//"lnumTop" is the top line.
//"kept" is the number of bytes kept in the first line, while
//"deleted" is the number of bytes deleted.
//"atEol" is true if the split is after the end of the line.
void
adjustPropsForSplit(
   LineNr    lnumProps,
   LineNr    lnumTop,
   int       kept,
   int       deleted,
   int       atEol
){
   int skipped = kept + deleted;
   if (!curBook->hasTextprop)
      return;

   // Get the text properties from "lnumProps".
   CS props;
   int count = get_text_props(OUT &props, curBook, lnumProps, false);
   ArrayList prevProp;
   ArrayList nextProp;
   ga_init2(&prevProp, sizeof(TextProp), 10);
   ga_init2(&nextProp, sizeof(TextProp), 10);

   // Keep the relevant ones in the first line, reducing the length if needed.
   // Copy the ones that include the split to the second line.
   // Move the ones after the split to the second line.
   for (int i = 0; i < count; ++i) {
      // copy the prop to an aligned structure
      TextProp  prop;
      mch_memmove(&prop, props + i * sizeof(TextProp), sizeof(TextProp));

      PropType* proTy = text_prop_type_by_id(curBook, prop.type);
      Boole startIncl = (proTy && (proTy->flags & PT_FLAG_INS_START_INCL));
      Boole endIncl = (proTy && (proTy->flags & PT_FLAG_INS_END_INCL));

      // a text prop "above" behaves like it is on the first text column
      int propCol = (prop.flags & TEXT_PROP_ALIGN_ABOVE) ? 1 : prop.col;

      Boole contPrev, contNext;
      if (propCol == MAXCOL) {
         contPrev = atEol;
         contNext = !atEol;
      } else {
         contPrev = propCol + (startIncl ? 0 : 1) <= kept;
         contNext = skipped <= propCol + prop.len - !endIncl;
      }
      // when a prop has text it is never copied
      if (prop.id < 0 && contNext)
         contPrev = FALSE;

      if (contPrev && ga_grow(&prevProp, 1) == OK) {
         TextProp* tProp = ((TextProp *)prevProp.c) + prevProp.len;

         *tProp = prop;
         ++prevProp.len;
         if (tProp->col != MAXCOL && tProp->col + tProp->len >= kept)
            tProp->len = kept - tProp->col;
         if (contNext)
            tProp->flags |= TEXT_PROP_CONT_NEXT;
      }

      // Only add the property to the next line if the length is positive
      if (contNext && ga_grow(&nextProp, 1) == OK) {
         TextProp* tProp = ((TextProp *)nextProp.c) + nextProp.len;

         *tProp = prop;
         ++nextProp.len;
         if (tProp->col != MAXCOL) {
            if (tProp->col > skipped)
               tProp->col -= skipped - 1;
            else {
               tProp->len -= skipped - tProp->col;
               tProp->col = 1;
            }
         }
         if (contPrev)
            tProp->flags |= TEXT_PROP_CONT_PREV;
      }
    }

    set_text_props(lnumTop, prevProp.c, prevProp.len * sizeof(TextProp));
    ga_clear(&prevProp);
    set_text_props(lnumTop + 1, nextProp.c, nextProp.len * sizeof(TextProp));
    ga_clear(&nextProp);
}

// Prepend properties of joined line "lnum" to "new_props".
void
prepend_joined_props(
   CS new_props,
   int       propcount,
   OUT int* props_remaining,
   LineNr    lnum,
   int       last_line,
   long       col,
   int       removed
) {
   CS props;
   int proplen = get_text_props(OUT &props, curBook, lnum, false);
   for (int i = proplen; i-- > 0; ) {
      TextProp  prop;

      mch_memmove(&prop, props + i * sizeof(prop), sizeof(prop));
      if (prop.col == MAXCOL && !last_line)
          continue;  // drop property with text after the line
      int end = !(prop.flags & TEXT_PROP_CONT_NEXT);

      adjust(&prop, 0, -removed, 0); // Remove leading spaces
      adjust(&prop, -1, col, 0); // Make line start at its final column

      if (last_line || end)
         mch_memmove(new_props + --(*props_remaining) * sizeof(prop), &prop, sizeof(prop));
      else {
         Boole found = false;

         // Search for continuing prop.
         for (int j = *props_remaining; j < propcount; ++j) {
            TextProp op;

            mch_memmove(&op, new_props + j * sizeof(op), sizeof(op));
            if ((op.flags & TEXT_PROP_CONT_PREV)
               && op.id == prop.id && op.type == prop.type
            ){
               found = true;
               op.len += op.col - prop.col;
               op.col = prop.col;
               // Start/end is taken care of when deleting joined lines
               op.flags = prop.flags;
               mch_memmove(new_props + j * sizeof(op), &op, sizeof(op));
               break;
            }
         }
         if (!found)
            internal_error(S"text property above joined line not found");
      }
   }
}

//}}}
