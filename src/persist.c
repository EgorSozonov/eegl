//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## persist.c: session related functions. Saving and restoring IDE state to files

#include "eegl.h"
#ifndef PROTO
#include <sys/stat.h> // for stat, fstat, S_ISDIR
#endif

//{{{users

private CS username = null; // cached result of mch_get_user_name()
// All user names (for ~user completion as done by shell).
private ArrayList   ga_users;


//Add a user name to the list of users in ga_users. Do nothing if user name is NULL or empty.
private void
add_user(Byte *user, int need_copy) {
   CS user_copy = (user && need_copy) ? copyStr(user) : user;

   if (!user_copy || *user_copy == ZERO || ga_grow(&ga_users, 1) == FAIL) {
      if (need_copy)
         eeglFree(user_copy);
      return;
   }
   ((Byte **)(ga_users.c))[ga_users.len++] = user_copy;
}

//Find all user names for user completion. Done only once and then cached.
private void
init_users(void) {
   static int   lazy_init_done = false;

   if (lazy_init_done)
      return;

   lazy_init_done = true;
   ga_init2(&ga_users, sizeof(CS), 20);

   {
   struct passwd*   pw;

   setpwent();
   while ((pw = getpwent()) != NULL)
      add_user((CS)pw->pw_name, true);
   endpwent();
   }
   CS user_env = mch_getenv(S"USER");

   //The $USER environment variable may be a valid remote user name (NIS, LDAP) not already listed 
   //by getpwent(), as getpwent() only lists local user names.  If $USER is not already listed, 
   //check whether it is a valid remote user name using getpwnam() and if it is, add it to
   //the list of user names.

   if (user_env && *user_env != ZERO) {
      Unt   i;
      for (i = 0; i < (Unt)ga_users.len; i++) {
         Byte   *local_user = ((Byte **)ga_users.c)[i];

         if (STRCMP(local_user, user_env) == 0)
             break;
      }

      if (i == (Unt)ga_users.len) {
         struct passwd *pw = getpwnam((char *)user_env);
         if (pw)
            add_user((CS)pw->pw_name, true);
      }
   }
}

//Function given to expandGeneric() to obtain user names.
CS
get_users(Expand *xp UNUSED, int idx) {
   init_users();
   if (idx < ga_users.len)
      return ((Byte **)ga_users.c)[idx];
   return NULL;
}

//Check whether name matches a user name. Return:
//0 if name does not match any user name.
//1 if name partially matches the beginning of a user name.
//2 is name fully matches a user name.
int
match_user(CS name) {
   int i;
   int n = (int)STRLEN(name);
   int result = 0;

   init_users();
   for (i = 0; i < ga_users.len; i++) {
      if (STRCMP(((Byte **)ga_users.c)[i], name) == 0)
         return 2; // full match
      if (STRNCMP(((Byte **)ga_users.c)[i], name, n) == 0)
         result = 1; // partial match
   }
   return result;
}



#if defined(EXITFREE) || defined(PROTO)

void
free_homedir(void) {
   eeglFree(homedir);
}

void
free_users(void) {
   ga_clear_strings(&ga_users);
}

#endif

//Get user name from machine-specific function. Return the user name in "buf[len]".
//Return OK or FAIL.
int
get_user_name(CS builder, int len) {
   if (!username) {
      if (mch_get_uname(getuid(), builder, len) == FAIL)
         return FAIL;
      username = copyStr(builder);
   } else
      copySubstrToAllocation(builder, (Text){username, len - 1});
   return OK;
}

//}}}
//{{{sessions

// Variable flavor
typedef enum {
   VAR_FLAVOR_DEFAULT,   // doesn't start with uppercase
   VAR_FLAVOR_SESSION,   // starts with uppercase, some lower
   VAR_FLAVOR_EEGLINFO      // all uppercase
} VarFlavor;

private Boole did_lcd;   // whether ":lcd" was produced for a session

//Write a file name to the session file.
//Takes care of the "slash" option in 'sessionoptions' and escapes special characters.
//Return FAIL if writing fails.
private int
ses_put_fname(FILE *fd, CS name) {
   CS sname = home_replace_save(NULL, name);

   int retval = OK;
   // escape special characters
   CS p = copyStr_fnameescape(sname, VSE_NONE);
   eeglFree(sname);

   // write the result
   if (FPUTS(p, fd) < 0)
      retval = FAIL;

   eeglFree(p);
   return retval;
}

//Write a book name to the session file.
//Also end the line, if "add_eol" is true. Return FAIL if writing fails.
private int
ses_fname(FILE* fd, Book* book, int add_eol) {
   CS name = book->shortFileName;
   if (ses_put_fname(fd, name) == FAIL || (add_eol && put_eol(fd) == FAIL))
      return FAIL;
   return OK;
}

// Write an argument list to the session file. Return FAIL if writing fails.
private int
ses_arglist(FILE* fd, CS cmd, ArrayList* gap, int fullname) {   // true: use full path name
   if (FPUTS(cmd, fd) < 0 || put_eol(fd) == FAIL)
      return FAIL;
   if (put_line(fd, S"%argdel") == FAIL)
      return FAIL;
   for (int i = 0; i < gap->len; ++i) {
      // NULL file names are skipped (only happens when out of memory).
      CS s = alist_name(&((ArgFileEntry *)gap->c)[i]);
      if (!s) {
         continue;
      }
      
      Byte buf[MAXPATHL];
      if (fullname) {
         (void)eeFullFileName(s, buf, MAXPATHL, false);
         s = buf;
      }
      if (fputs("$argadd ", fd) < 0 
            || ses_put_fname(fd, s) == FAIL 
            || put_eol(fd) == FAIL
      ){
         return FAIL;
      }
   }
   return OK;
}

// Return non-zero if portal "po" is to be stored in the Session.
private Boole
portNeedsToBeSaved(Portal* po) {
   if (bt_terminal(po->book)) {
      return !term_is_finished(po->book) && term_should_restore(po->book);
   } 
   if (!po->book->currFileName || bt_nofilename(po->book))
      // When 'buftype' is "nofile", can't restore the portal contents.
      return false;
   return true;
}

// Return true if frame "fr" has a window somewhere that we want to save in the Session
private Boole
ses_do_frame(Frame* fr) {
   if (fr->layout == FR_LEAF)
      return portNeedsToBeSaved(fr->port);
      
   Frame   *frc;
   FOR_ALL_FRAMES(frc, fr->child) {
      if (ses_do_frame(frc))
          return true;
   } 
   return false;
}

// Skip frames that don't contain portals we want to save in the Session. Return NULL when none
private Frame*
ses_skipframe(Frame* fr) {
   Frame* frc;
   FOR_ALL_FRAMES(frc, fr) {
      if (ses_do_frame(frc))
          return frc;
   } 
   return null;
}

// Write commands to "fd" to recursively create portals for frame "fr", horizontally and vertically
// split. After the commands the last portal in the frame is the current portal. Return FAIL when 
// writing the commands to "fd" fails.
private int
createPortals(FILE* fd, Frame* fr) {
   if (fr->layout == FR_LEAF)
      return OK;

   // Find first frame that's not skipped and then create a window for
   // each following one (first frame is already there).
   Frame* frc = ses_skipframe(fr->child);
   int count = 0;
   if (frc) {
      while ((frc = ses_skipframe(frc->next)) != NULL) {
         // Make window as big as possible so that we have lots of room to split.
         if (put_line(fd, S"wincmd _ | wincmd |") == FAIL
               || put_line(fd, fr->layout == FR_COL ? S"split" : S"vsplit") == FAIL
         )
            return FAIL;
         ++count;
      }
   } 

   // Go back to the first window.
   if (count > 0 && (fprintf(fd, fr->layout == FR_COL
          ? "%dwincmd k" : "%dwincmd h", count) < 0
         || put_eol(fd) == FAIL))
      return FAIL;

   // Recursively create frames/windows in each window of this column or row.
   frc = ses_skipframe(fr->child);
   while (frc) {
      createPortals(fd, frc);
      frc = ses_skipframe(frc->next);
      // Go to next window.
      if (frc && put_line(fd, S"wincmd w") == FAIL)
          return FAIL;
   }

   return OK;
}

private int
portalSizes(FILE* fd, int restore_size, Portal* tab_firstPor) {
   int      n = 0;
   Portal* wp;

   if (restore_size) {
      for (wp = tab_firstPor; wp != NULL; wp = wp->next) {
         if (!portNeedsToBeSaved(wp))
            continue;
         ++n;

         // restore height when not full height
         if (wp->height + wp->statusHeight < topframeG->width
                && (fprintf(fd,
                 "exe '%dresize ' . ((&lines * %ld + %ld) / %ld)",
                   n, (long)wp->height, visibleRowsG / 2, visibleRowsG) < 0
                          || put_eol(fd) == FAIL))
            return FAIL;

          // restore width when not full width
          if (wp->width < visibleColsG && (fprintf(fd,
            "exe 'vert %dresize ' . ((&columns * %ld + %ld) / %ld)",
                n, (long)wp->width, visibleColsG / 2, visibleColsG) < 0
                       || put_eol(fd) == FAIL))
         return FAIL;
      }
   } else {
      // Just equalise window sizes
      if (put_line(fd, S"wincmd =") == FAIL)
         return FAIL;
   }
   return OK;
}

private int
put_view_curpos(FILE *fd, Portal *wp, char *spaces) {
   int r;
   if (wp->cursWant == MAXCOL)
      r = fprintf(fd, "%snormal! $", spaces);
   else
      r = fprintf(fd, "%snormal! 0%d|", spaces, wp->virtCol + 1);
   return r < 0 || put_eol(fd) == FAIL ? false : OK;
}

//Write commands to "fd" to restore the view of a window.
//Caller must make sure 'scrolloff' is zero.
private int
put_view(
   FILE* fd,
   Portal* wp,
   Tab* tp,
   int add_edit,        // add ":edit" command to view
   int current_arg_idx,     // current argument index of the portal, use -1 if unknown
   EeSet* terminal_bufs // already encountered terminal books, can be NULL
){
   Portal   *save_curPor;
   int      f;
   int      did_next = false;

   // Always restore cursor position for ":mksession".
   Boole do_cursor = true;

   // Local argument list.
   if (wp->argList == &argListG) {
      if (put_line(fd, S"argglobal") == FAIL)
         return FAIL;
   } else {
      if (ses_arglist(fd, S"arglocal", &wp->argList->al_ga,
            tp->localdir != NULL
            || wp->localDir != NULL) == FAIL)
         return FAIL;
   }

   //Only when part of a session: restore the argument index.  Some
   //arguments may have been deleted, check if the index is valid.
   if (wp->argListInd != current_arg_idx && wp->argListInd < WARGCOUNT(wp)) {
      if (fprintf(fd, "%ldargu", (long)wp->argListInd + 1) < 0 || put_eol(fd) == FAIL)
         return FAIL;
      did_next = true;
   }

   // Edit the file.  Skip this when ":next" already did it.
   if (add_edit && (!did_next || wp->isNotValid)) {
      if (bookIsHelp(wp->book)) {
         CS curtag = S"";

         // A help book needs some options to be set.
         // First, create a new empty book with "buftype=help".
         // Then ":help" will re-use both the book and the portal and set the options, even when
         // "options" is not in 'sessionoptions'.
         if (0 < wp->tagStackInd && wp->tagStackInd <= wp->tagStackLen)
            curtag = wp->tagStack[wp->tagStackInd - 1].tagname;

         if (put_line(fd, S"enew | setl bt=help") == FAIL
                || fprintf(fd, "help %s", curtag) < 0
                || put_eol(fd) == FAIL)
            return FAIL;
      } ei (bt_terminal(wp->book)) {
         if (term_write_session(fd, wp, terminal_bufs) == FAIL)
            return FAIL;
      }
      // Load the file.
      ei (wp->book->fullFileName != NULL && !bt_nofilename(wp->book)) {
          // Editing a file in this book: use ":edit file".
          // This may have side effects! (e.g., compressed or network file).
          //
          // Note, if a book for that file already exists, use :badd to
          // edit that book, to not lose folding information (:edit resets
          // folds in other books)
          if (fputs("if bufexists(fnamemodify(\"", fd) < 0
             || ses_fname(fd, wp->book, false) == FAIL
             || fputs("\", \":p\")) | buffer ", fd) < 0
             || ses_fname(fd, wp->book, false) == FAIL
             || fputs(" | else | edit ", fd) < 0
             || ses_fname(fd, wp->book, false) == FAIL
             || fputs(" | endif", fd) < 0
             || put_eol(fd) == FAIL)
         return FAIL;
      } else {
         //No file in this book, just make it empty.
         if (put_line(fd, S"enew") == FAIL)
            return FAIL;
         if (wp->book->fullFileName != NULL) {
            //The book does have a name, but it's not a file name.
            if (fputs("file ", fd) < 0 || ses_fname(fd, wp->book, true) == FAIL)
               return FAIL;
         }
         do_cursor = false;
      }
   }

   if (wp->altFnum) {
      Book *alt = bookFindFileByBookNr(wp->altFnum);

      // Set the alternate file if the book is listed.
      if (     alt
            && alt->currFileName != NULL
            && *alt->currFileName != ZERO
            && alt->o.bookListed
            && (fputs("balt ", fd) < 0 || ses_fname(fd, alt, true) == FAIL))
         return FAIL;
   }

   // Local mappings and abbreviations.
   if (makemap(fd, wp->book) == FAIL)
      return FAIL;

   //Local options. Need to go to the portal temporarily.
   //Store only local values when ":mksession" is
   //used and 'sessionoptions' doesn't include "options".
   //Some folding options are always stored when "folds" is included,
   //otherwise the folds would not be restored correctly.
   save_curPor = curPor;
   curPor = wp;
   curBook = curPor->book;
   f = writeOptionsAsSet(fd);
   curPor = save_curPor;
   curBook = curPor->book;
   if (f == FAIL)
      return FAIL;

   // Set the cursor after creating folds, since that moves the cursor.
   if (do_cursor) {

      // Restore the cursor line in the file and relatively in the
      // portal.  Don't use "G", it changes the jumplist.
      if (wp->height <= 0) {
         if (fprintf(fd, "let s:l = %ld", (long)wp->cursor.lnum) < 0)
            return FAIL;
      } ei (fprintf(fd,
             "let s:l = %ld - ((%ld * winheight(0) + %ld) / %ld)",
             (long)wp->cursor.lnum,
             (long)(wp->cursor.lnum - wp->topLine),
             (long)wp->height / 2, (long)wp->height) < 0)
          return FAIL;

      if (put_eol(fd) == FAIL
            || put_line(fd, S"if s:l < 1 | let s:l = 1 | endif") == FAIL
            || put_line(fd, S"keepjumps exe s:l") == FAIL
            || put_line(fd, S"normal! zt") == FAIL
            || fprintf(fd, "keepjumps %ld", (long)wp->cursor.lnum) < 0
            || put_eol(fd) == FAIL)
          return FAIL;
      // Restore the cursor column and left offset when not wrapping.
      if (wp->cursor.col == 0) {
         if (put_line(fd, S"normal! 0") == FAIL)
            return FAIL;
      } else {
         if (!wp->o.wrap && wp->leftCol > 0 && wp->width > 0) {
            if (fprintf(fd,
                 "let s:c = %ld - ((%ld * winwidth(0) + %ld) / %ld)",
                   (long)wp->virtCol + 1,
                   (long)(wp->virtCol - wp->leftCol),
                   (long)wp->width / 2, (long)wp->width) < 0
               || put_eol(fd) == FAIL
               || put_line(fd, S"if s:c > 0") == FAIL
               || fprintf(fd,
                   "  exe 'normal! ' . s:c . '|zs' . %ld . '|'",
                   (long)wp->virtCol + 1) < 0
               || put_eol(fd) == FAIL
               || put_line(fd, S"else") == FAIL
               || put_view_curpos(fd, wp, "  ") == FAIL
               || put_line(fd, S"endif") == FAIL)
                return FAIL;
         } ei (put_view_curpos(fd, wp, "") == FAIL)
            return FAIL;
      }
   }

   // Local directory, if the current flag is not view options or the "curdir"
   // option is included.
   if (wp->localDir) {
      if (fputs("lcd ", fd) < 0
            || ses_put_fname(fd, wp->localDir) == FAIL
            || put_eol(fd) == FAIL)
         return FAIL;
      did_lcd = true;
   }

   return OK;
}

private VarFlavor
getVarFlavor(CS varname) {
   CS p = varname;

   if (ASCII_ISUPPER(*p)) {
      while (*(++p)) {
         if (ASCII_ISLOWER(*p))
            return VAR_FLAVOR_SESSION;
      } 
      return VAR_FLAVOR_EEGLINFO;
   } else
      return VAR_FLAVOR_DEFAULT;
}

private int
store_session_globals(FILE *fd) {
   EeSet   *gvht = get_globvar_ht();
   EeSetItem   *hi;
   DictItem   *this_var;
   int      todo;
   Byte   *p, *t;

   todo = (int)gvht->count;
   FOR_ALL_HASHTAB_ITEMS(gvht, hi, todo) {
      if (!HASHITEM_EMPTY(hi)) {
         --todo;
         this_var = HI2DI(hi);
         if ((this_var->c.tag == VAR_NUMBER || this_var->c.tag == VAR_STRING)
                && getVarFlavor(this_var->key) == VAR_FLAVOR_SESSION
         ){
            // Escape special characters with a backslash. Turn a LF and CR into \n and \r.
            p = copyStr_escaped(tv_get_string(&this_var->c), (CS)"\\\"\n\r");
            for (t = p; *t != ZERO; ++t)
               if (*t == '\n')
                  *t = 'n';
               ei (*t == '\r')
                  *t = 'r';
            if ((fprintf(fd, "let %s = %c%s%c",
                  this_var->key,
                  (this_var->c.tag == VAR_STRING) ? '"' : ' ',
                  p,
                  (this_var->c.tag == VAR_STRING) ? '"' : ' ') < 0)
                  || put_eol(fd) == FAIL
            ){
               eeglFree(p);
               return FAIL;
            }
            eeglFree(p);
         } ei (this_var->c.tag == VAR_FLOAT && getVarFlavor(this_var->key) == VAR_FLAVOR_SESSION) {
            double f = this_var->c.floatt;
            int sign = ' ';

            if (f < 0) {
               f = -f;
               sign = '-';
            }
            if ((fprintf(fd, "let %s = %c%f", this_var->key, sign, f) < 0) || put_eol(fd) == FAIL)
               return FAIL;
         }
      }
   }
   return OK;
}

//Write openfile commands for the current books to an .exrc file.
//Return FAIL on error, OK otherwise.
private int
makeopens(FILE   *fd, Byte   *currDir) {  // Current directory name
   Book   *book;
   int      nr;
   int      restore_size = true;
   int      restore_height_width = false;
   Portal   *wp;
   Byte   *sname;
   Portal   *edited_win = NULL;
   int      restore_stal = false;
   Portal   *tab_firstPor;
   Frame   *tab_topframe;
   int      cur_arg_idx = 0;
   int      next_arg_idx = 0;
   int      ret = FAIL;
   Tab   *tp;
   EeSet   terminal_bufs;

   hash_init(&terminal_bufs);


   // Begin by setting the this_session variable, and then other
   // sessionable variables.
   if (put_line(fd, S"let v:this_session=expand(\"<sfile>:p\")") == FAIL
        || store_session_globals(fd) == FAIL)
      goto fail;

   // Close all portals and tabs but one.
   if (put_line(fd, S"silent only") == FAIL)
      goto fail;
   if (put_line(fd, S"silent tabonly") == FAIL)
      goto fail;

   // Now a :cd command to the current directory
   sname = home_replace_save(NULL, globaldir != NULL ? globaldir : currDir);
   if (  fputs("cd ", fd) < 0
      || ses_put_fname(fd, sname) == FAIL
      || put_eol(fd) == FAIL
   ){
      eeglFree(sname);
      goto fail;
   }
   eeglFree(sname);

   // If there is an empty, unnamed book we will wipe it out later.
   // Remember the book number.
   if (put_line(fd, S"if expand('%') == '' && !&modified && line('$') <= 1 && getline(1) == ''") 
         == FAIL
   )
      goto fail;
   if (put_line(fd, S"  let s:wipebuf = bufnr('%')") == FAIL)
      goto fail;
   if (put_line(fd, S"endif") == FAIL)
      goto fail;

   // Set 'shortmess' for the following.
   if (put_line(fd, S"set shortmess+=aoO") == FAIL)
      goto fail;

   // Now save the current files, current book first.
   // Put all books into the book list.
   // Do it very early to preserve book order after loading session (which
   // can be disrupted by prior `edit` or `tabedit` calls).
   FOR_ALL_BOOKS(book) {
      if (fprintf(fd, "badd +%ld ", book->portInfos == NULL ? 1L
                  : book->portInfos->wi_fpos.lnum) < 0
             || ses_fname(fd, book, true) == FAIL)
         goto fail;
   }

   // the global argument list
   if (ses_arglist(fd, S"argglobal", &argListG.al_ga, false) 
         == FAIL
   )
      goto fail;

   // Note: after the restore we still check it worked!
   if (fprintf(fd, "set lines=%ld columns=%ld" , visibleRowsG, visibleColsG) < 0 
         || put_eol(fd) == FAIL)
      goto fail;

   // "tabs" is in 'sessionoptions': Similar to createPortals() below, populate the tabs first 
   // so later local options won't be copied to the new tabs.
   FOR_ALL_TABS(tp) {
      // Use `bufhidden=wipe` to remove empty "placeholder" books once they are not needed. 
      // This prevents creating extra books (see cause of patch 8.1.0829)
      if (tp->next != NULL && put_line(fd, S"tabnew +setlocal\\ bufhidden=wipe") == FAIL)
         goto fail;
   } 
   if (firstTabG->next != NULL && put_line(fd, S"tabrewind") == FAIL)
       goto fail;

   // Assume "tabs" is in 'sessionoptions'. If not then we only do "curtab" and bail out of the loop
   FOR_ALL_TABS(tp) {
      int   need_tabnext = false;
      int   cnr = 1;

      // May repeat putting Portals for each tab, when "tabs" is in 'sessionoptions'.
      // Don't use goto_tabpage(), it may change directory and trigger autocommands.
      if (tp == curtab) {
         tab_firstPor = firstPor;
         tab_topframe = topframeG;
      } else {
         tab_firstPor = tp->firstPor;
         tab_topframe = tp->topframe;
      }
      if (tp != firstTabG)
         need_tabnext = true;

      // Before creating the window layout, try loading one file.  If this
      // is aborted we don't end up with a number of useless windows.
      // This may have side effects! (e.g., compressed or network file).
      for (wp = tab_firstPor; wp != NULL; wp = wp->next) {
          if (portNeedsToBeSaved(wp)
             && wp->book->fullFileName != NULL
             && !bookIsHelp(wp->book)
             && !bt_nofilename(wp->book)
         ){
            if (need_tabnext && put_line(fd, S"tabnext") == FAIL)
               goto fail;
            need_tabnext = false;

            if (fputs("edit ", fd) < 0 || ses_fname(fd, wp->book, true) == FAIL)
               goto fail;
            if (!wp->isNotValid)
               edited_win = wp;
            break;
         }
      }

      // If no file got edited create an empty tab
      if (need_tabnext && put_line(fd, S"tabnext") == FAIL)
         goto fail;

      if (tab_topframe->layout != FR_LEAF) {
          // Save current window layout.
          if (put_line(fd, S"let s:save_splitbelow = &splitbelow") == FAIL
                || put_line(fd, S"let s:save_splitright = &splitright") == FAIL)
            goto fail;
         if (put_line(fd, S"set splitbelow splitright") == FAIL)
            goto fail;
         if (createPortals(fd, tab_topframe) == FAIL)
            goto fail;
         if (put_line(fd, S"let &splitbelow = s:save_splitbelow") == FAIL
                || put_line(fd, S"let &splitright = s:save_splitright") == FAIL)
            goto fail;
      }

      // Check if window sizes can be restored (no windows omitted).
      // Remember the window number of the current window after restoring.
      nr = 0;
      for (wp = tab_firstPor; wp != NULL; wp = wp->next) {
         if (portNeedsToBeSaved(wp))
            ++nr;
         else
            restore_size = false;
         if (curPor == wp)
            cnr = nr;
      }

      if (tab_firstPor->next) {
         // Go to the first portal.
         if (put_line(fd, S"wincmd t") == FAIL)
            goto fail;

          // If more than one window, see if sizes can be restored.
          // First set 'winheight' and 'winwidth' to 1 to avoid the windows
          // being resized when moving between windows.
          // Do this before restoring the view, so that the topline and the
          // cursor can be set.  This is done again below.
          // winminheight and winminwidth need to be set to avoid an error if
          // the user has set winheight or winwidth.
          if (put_line(fd, S"let s:save_winminheight = &winminheight") == FAIL
             || put_line(fd, S"let s:save_winminwidth = &winminwidth")
                                  == FAIL)
         goto fail;
          if (put_line(fd, S"set winminheight=0") == FAIL
                || put_line(fd, S"set winheight=1") == FAIL
                || put_line(fd, S"set winminwidth=0") == FAIL
                || put_line(fd, S"set winwidth=1") == FAIL)
            goto fail;
         restore_height_width = true;
      }
      if (nr > 1 && portalSizes(fd, restore_size, tab_firstPor) == FAIL)
         goto fail;

      // Restore the tab-local working directory if specified
      // Do this before the windows, so that the window-local directory can
      // override the tab-local directory.
      if (tp->localdir != NULL) {
         if (fputs("tcd ", fd) < 0
              || ses_put_fname(fd, tp->localdir) == FAIL
              || put_eol(fd) == FAIL
         )
            goto fail;
         did_lcd = true;
      }

      // Restore the view of the window (options, file, cursor, etc.).
      for (wp = tab_firstPor; wp != NULL; wp = wp->next) {
         if (!portNeedsToBeSaved(wp))
            continue;
          if (put_view(fd, wp, tp, wp != edited_win, 
                         cur_arg_idx,
                         &terminal_bufs
          ) == FAIL)
         goto fail;
          if (nr > 1 && put_line(fd, S"wincmd w") == FAIL)
         goto fail;
          next_arg_idx = wp->argListInd;
      }

      // The argument index in the first tab is zero, need to set it in each portal. For further 
      // tabs it's the portal where we do "tabedit".
      cur_arg_idx = next_arg_idx;

      // Restore cursor to the current window if it's not the first one.
      if (cnr > 1 && (fprintf(fd, "%dwincmd w", cnr) < 0 || put_eol(fd) == FAIL))
         goto fail;

      //Restore window sizes again after jumping around in windows, because
      //the current window has a minimum size while others may not.
      if (nr > 1 && portalSizes(fd, restore_size, tab_firstPor) == FAIL)
         goto fail;
   }

   if (fprintf(fd, "tabnext %d", indexOfTab(curtab)) < 0 || put_eol(fd) == FAIL)
      goto fail;
   if (restore_stal && put_line(fd, S"set stal=1") == FAIL)
      goto fail;

   // Wipe out an empty unnamed book we started in.
   if (put_line(fd, S"if exists('s:wipebuf') && len(win_findbuf(s:wipebuf)) == 0") == FAIL)
      goto fail;
   if (put_line(fd, S"  silent exe 'bwipe ' . s:wipebuf") == FAIL)
      goto fail;
   if (put_line(fd, S"endif") == FAIL)
      goto fail;
   if (put_line(fd, S"unlet! s:wipebuf") == FAIL)
      goto fail;

   // Re-apply 'winheight' and 'winwidth'.
   if (fprintf(fd, "set winheight=%ld winwidth=%ld", p_wh, p_wiw) < 0 || put_eol(fd) == FAIL)
      goto fail;

   if (restore_height_width // Restore 'winminheight' and 'winminwidth'.
       && (put_line(fd, S"let &winminheight = s:save_winminheight") == FAIL
         || put_line(fd, S"let &winminwidth = s:save_winminwidth") == FAIL)
   ) {
      goto fail;
   }

   // Lastly, execute the x.vim file if it exists.
   if (put_line(fd, S"let s:sx = expand(\"<sfile>:p:r\").\"x.vim\"") == FAIL
          || put_line(fd, S"if filereadable(s:sx)") == FAIL
          || put_line(fd, S"  exe \"source \" . fnameescape(s:sx)") == FAIL
          || put_line(fd, S"endif") == FAIL)
      goto fail;

   ret = OK;
fail:
   hash_clear_all(&terminal_bufs, 0);
   return ret;
}

# if (defined(EXPERIMENTAL_GUI_CMD)) || defined(PROTO)
//Generate a script that can be used to restore the current editing session.
//Save the value of v:this_session before running :mksession in order to make
//automagic session save fully transparent.  Return true on success.
int
write_session_file(CS filename) {
   // Build a command line to create a script that restores the current
   // session if executed.  Escape the filename to avoid nasty surprises.
   CS escaped_filename = copyStr_escaped(filename, escape_chars);
   CS mksession_cmdline = alloc(10 + (int)STRLEN(escaped_filename) + 1);
   strcpy(mksession_cmdline, "mksession ");
   STRCAT(mksession_cmdline, escaped_filename);
   eeglFree(escaped_filename);

   //Use a reasonable hardcoded set of 'sessionoptions' flags to avoid unpredictable effects 
   //when the session is saved automatically.

   executeCommLine(S"let Save_VV_this_session = v:this_session");
   int failed = (executeCommLine((CS)mksession_cmdline) == FAIL);
   executeCommLine(S"let v:this_session = Save_VV_this_session");
   unletImpl(S"Save_VV_this_session", true);

   eeglFree(mksession_cmdline);

   //Reopen the file and append a command to restore v:this_session,
   //as if this save never happened.   This is to avoid conflicts with
   //the user's own sessions.  FIXME: It's probably less hackish to add
   //a "stealth" flag to 'sessionoptions' -- gotta ask Bram.
   if (!failed) {
      FILE* fd = doOpenCommandsFile(filename, true, APPENDBIN);
      failed = (fd == NULL
             || put_line(fd, S"let v:this_session = Save_VV_this_session") == FAIL
             || put_line(fd, S"unlet Save_VV_this_session") == FAIL);

      if (fd != NULL && fclose(fd) != 0)
         failed = true;

      if (failed)
         mch_remove(filename);
   }

   return !failed;
}
# endif


// ":mkvimrc",  and ":mksession".
void
c_mkrc(Invocation* invo) {
   int failed = false;
   int using_vdir = false;   // using 'viewdir'?
   CS viewFile = NULL;

   Boole sessionFile = (invo->id == C_mksession);

   // Use the short file name until ":lcd" is used.  We also don't use the
   // short file name when 'acd' is set, that is checked later.
   did_lcd = false;
   
   CS fname;
   if (*invo->arg != ZERO)
      fname = invo->arg;
   ei (invo->id == C_mkvimrc)
      fname = S"~/.config/eegl/init.vim";
   ei (invo->id == C_mksession)
      fname = (CS)SESSION_FILE;

   FILE* fd = doOpenCommandsFile(fname, invo->forceit, (CS)WRITEBIN);
   if (!fd) {
      goto theEnd;
   }

   // Write the version command for :mkvimrc
   if (invo->id == C_mkvimrc)
       (void)put_line(fd, S"version 6.0");

   if (invo->id == C_mksession) {
       if (put_line(fd, S"let SessionLoad = 1") == FAIL)
      failed = true;
   }

   (void)put_line(fd, S"if &cp | set nocp | endif");

   if (!sessionFile || invo->id == C_mksession) {
      failed |= (makemap(fd, NULL) == FAIL || writeOptionsAsSet(fd) == FAIL);
   }

   if (!failed && sessionFile) {
      if (put_line(fd, S"let s:so_save = &g:so | let s:siso_save = &g:siso | "
         "setg so=0 siso=0 | setl so=-1 siso=-1") == FAIL)
      failed = true;
      if (invo->id == C_mksession) {
         Byte currDir[MAXPATHL];    // current directory
         // Change to session file's dir.
         if (mch_dirname(currDir, MAXPATHL) == FAIL || mch_chdir((char *)currDir) != 0)
            *currDir = ZERO;
         if (*currDir != ZERO) {
            if (eeChdirfile(fname, NULL) == OK)
               shorten_fnames(true);
         } ei (*currDir != ZERO && globaldir) {
            if (mch_chdir((char *)globaldir) == 0)
               shorten_fnames(true);
         }

         failed |= (makeopens(fd, currDir) == FAIL);

         // restore original dir
         if (*currDir != ZERO && globaldir) {
            if (mch_chdir((char *)currDir) != 0)
               emsg(_(e_cannot_go_back_to_previous_directory));
            shorten_fnames(true);
         }
      } else {
         failed |= (put_view(fd, curPor, curtab, !using_vdir, -1, NULL) == FAIL);
      }
      if (put_line(fd, S"let &g:so = s:so_save | let &g:siso = s:siso_save") == FAIL)
         failed = true;
      if (!hiliteSearchG && put_line(fd, S"hlsearch") == FAIL)
         failed = true;
      if (put_line(fd, S"doautoall SessionLoadPost") == FAIL)
         failed = true;
      if (invo->id == C_mksession) {
         if (put_line(fd, S"unlet SessionLoad") == FAIL)
            failed = true;
      }
   }
   if (put_line(fd, S"\" vim: set ft=vim :") == FAIL)
      failed = true;

   failed |= fclose(fd);

   if (failed)
      emsg(_(e_error_while_writing));
   ei (invo->id == C_mksession) {
      // successful session write - set this_session var
      Byte tbuf[MAXPATHL];
      if (eeFullFileName(fname, tbuf, MAXPATHL, false) == OK)
         set_EeglVar_string(VV_THIS_SESSION, tbuf, -1);
   }
theEnd:

   eeglFree(viewFile);

   applyAutocomms(EVENT_SESSIONWRITEPOST, NULL, NULL, false, curBook);
}

//Write end-of-line character(s) for ":mkexrc", ":mkvimrc" and ":mksession".
//Return FAIL for a write error.
int
put_eol(FILE *fd) {
   if (putc('\n', fd) < 0)
      return FAIL;
   return OK;
}

//Write a line to "fd". Return FAIL for a write error.
int
put_line(FILE *fd, CS s) {
   if (FPUTS(s, fd) < 0 || put_eol(fd) == FAIL)
      return FAIL;
   return OK;
}

//}}}
//{{{eeglinfo files - serialization of state like marks and search history to files

#define EEGLINFO_VERSION                4
#define EEGLINFO_VERSION_WITH_HISTORY   2
#define EEGLINFO_VERSION_WITH_REGISTERS 3
#define EEGLINFO_VERSION_WITH_MARKS     4

// The type numbers are fixed for backwards compatibility.
#define BARTYPE_VERSION  1
#define BARTYPE_HISTORY  2
#define BARTYPE_REGISTER 3
#define BARTYPE_MARK     4

// Structure used for reading from the eeglinfo file.
typedef struct {
   CS line;   // text of the current line
   FILE* vir_fd;   // file descriptor
   int vir_version;   // eeglinfo version detected or -1
   ArrayList vir_barlines;   // lines starting with |
} Vir;

typedef enum {
   BVAL_NR,
   BVAL_STRING,
   BVAL_EMPTY
} BValKind;

typedef struct {
   BValKind   btag;
   long   bv_nr;
   Byte   *bv_string;
   Byte   *bv_tofree;   // free later when not NULL
   int      bv_len;      // length of bv_string
   int      bv_allocated;   // bv_string was allocated
} BVal;


private int  eeglinfo_errcnt;

//Find the parameter represented by the given character (eg ''', ':', '"', or
//'/') in the @eeglinfo option and return a pointer to the string after it.
//Return NULL if the parameter is not specified in the string.
private CS
find_eeglinfo_parameter(int type) {
   if (!p_eeglinfo)
      return null;
   for (CS p = p_eeglinfo; *p; ++p) {
      if (*p == type)
         return p + 1;
      if (*p == 'n')          // 'n' is always the last one
         break;
      p = firstOccurrence(p, ',');       // skip until next ','
      if (!p)          // hit the end without finding parameter
         break;
   }
   return NULL;
}

//Find the parameter represented by the given character (eg ', :, ", or /), and return its 
//associated value in the 'eeglinfo' string. Only works for number parameters, not for 'r' or 'n'.
//If the parameter is not specified in the string or there is no following number, return -1.
int
get_eeglinfo_parameter(int type) {
   CS p = find_eeglinfo_parameter(type);
   if (p && EE_ISDIGIT(*p))
      return atoi((char *)p);
   return -1;
}

//Get the eeglinfo file name to use. If "file" is given and not empty, use it (has already been 
//expanded by cmdline functions).
//Otherwise use "-i file_name", value from 'eeglinfo' or the default, and expand environment 
//variables. Return an allocated string.
private CS
eeglinfo_filename(CS file) {
   if (!file || *file == ZERO) {
      if (p_eeglinfofile)
         file = p_eeglinfofile;
      ei ((file = find_eeglinfo_parameter('n')) == NULL || *file == ZERO) {
#ifdef EEGLINFO_FILE2
         if (mch_getenv((CS)"HOME") == NULL) {
            // don't use $EEGL when not available.
            doExpandEnv(OUT nameBuffTextG, S"$EEGL");
            if (STRCMP("$EEGL", nameBuffG) != 0)  // $EEGL was expanded
               file = (CS)EEGLINFO_FILE2;
            else
               file = (CS)EEGLINFO_FILE;
         } else
#endif
         file = (CS)EEGLINFO_FILE;
      }
      Unt len = doExpandEnv(OUT nameBuffTextG, file);
      file = nameBuffG;

      return copySubstr(file, len);
   }

   return copyStr(file);
}

//write string to eeglinfo file
//- replace CTRL-V with CTRL-V CTRL-V
//- replace '\n'   with CTRL-V 'n'
//- add a '\n' at the end
//
//For a long line:
//- write " CTRL-V <length> \n " in first line
//- write " < <string> \n "     in second line
private void
eeglinfo_writestring(FILE* fd, CS p) {
   int c;
   CS s;
   int len = 0;

   for (s = p; *s != ZERO; ++s) {
      if (*s == Ctrl_V || *s == '\n')
          ++len;
      ++len;
   }

   //If the string will be too long, write its length and put it in the next line. Take into 
   //account that some room is needed for what comes before the string (e.g., variable name). 
   //Add something to the length for the '<', NL and trailing ZERO.
   if (len > LSIZE / 2)
      fprintf(fd, "\026%d\n<", len + 3);

   while ((c = *p++) != ZERO) {
      if (c == Ctrl_V || c == '\n') {
         putc(Ctrl_V, fd);
         if (c == '\n')
            c = 'n';
      }
      putc(c, fd);
   }
   putc('\n', fd);
}

//Write a string in quotes that barline_parse() can read back. Break the line in less than LSIZE 
//pieces when needed. Return remaining characters in the line.
private int
barline_writestring(FILE *fd, CS s, int remaining_start) {
   Byte *p;
   int       remaining = remaining_start;
   int       len = 2;

   // Count the number of characters produced, including quotes.
   for (p = s; *p != ZERO; ++p) {
      if (*p == NL)
          len += 2;
      ei (*p == '"' || *p == '\\')
          len += 2;
      else
          ++len;
   }
   if (len > remaining - 2) {
      fprintf(fd, ">%d\n|<", len);
      remaining = LSIZE - 20;
   }

   putc('"', fd);
   for (p = s; *p != ZERO; ++p) {
      if (*p == NL) {
          putc('\\', fd);
          putc('n', fd);
          --remaining;
      } ei (*p == '"' || *p == '\\') {
          putc('\\', fd);
          putc(*p, fd);
          --remaining;
      }
      else
          putc(*p, fd);
      --remaining;

      if (remaining < 3) {
          putc('\n', fd);
          putc('|', fd);
          putc('<', fd);
          // Leave enough space for another continuation.
          remaining = LSIZE - 20;
      }
   }
   putc('"', fd);
   return remaining - 2;
}

//Check string read from eeglinfo file.
//Remove '\n' at the end of the line.
//- replace CTRL-V CTRL-V with CTRL-V
//- replace CTRL-V 'n'    with '\n'
//
//Check for a long line as written by eeglinfo_writestring().
//
//Return the string in allocated memory (NULL when out of memory).
private CS
eeglinfo_readstring(Vir* virp, int off) {          // offset for virp->line
   CS retval = NULL;
   CS s;
   long len;

   if (virp->line[off] == Ctrl_V && eeIsDigit(virp->line[off + 1])) {
      len = atol((char *)virp->line + off + 1);
      if (len > 0 && len < 1000000)
         retval = lalloc(len, true);
      else {
         // Invalid length, line too long?  Skip next line.
         (void)eeFgets(virp->line, 10, virp->vir_fd);
         return NULL;
      }
      (void)eeFgets(retval, (int)len, virp->vir_fd);
      s = retval + 1;       // Skip the leading '<'
   } else {
      retval = copyStr(virp->line + off);
      s = retval;
   }

   // Change CTRL-V CTRL-V to CTRL-V and CTRL-V n to \n in-place.
   CS d = retval;
   while (*s != ZERO && *s != '\n') {
      if (s[0] == Ctrl_V && s[1] != ZERO) {
         if (s[1] == 'n')
            *d++ = '\n';
         else
            *d++ = Ctrl_V;
         s += 2;
      } else
         *d++ = *s++;
   }
   *d = ZERO;
   return retval;
}

// Read a line from the eeglinfo file. Return true for end-of-file;
private int
eeglinfo_readline(Vir* virp) {
   return eeFgets(virp->line, LSIZE, virp->vir_fd);
}

private int
readEeglinfoBookList(Vir* virp, int writing) {
   CS tab;
   LineNr   lnum;
   ColNr   col;
   Book* book;
   CS sfname;

   // Handle long line and escaped characters.
   CS xline = eeglinfo_readstring(virp, 1);

   // don't read in if there are files on the command-line or if writing:
   if (xline && !writing && ARGCOUNT == 0 && find_eeglinfo_parameter('%') != NULL) {
      // Format is: <fname> Tab <lnum> Tab <col>.
      // Watch out for a Tab in the file name, work from the end.
      lnum = 0;
      col = 0;
      tab = lastOccurrence(xline, '\t');
      if (tab != NULL) {
         *tab++ = '\0';
         col = (ColNr)atoi((char *)tab);
         tab = lastOccurrence(xline, '\t');
         if (tab) {
            *tab++ = '\0';
            lnum = atol((char *)tab);
         }
      }

      // Expand "~/" in the file name at "line + 1" to a full path.
      // Then try shortening it by comparing with the current directory
      doExpandEnv(OUT nameBuffTextG, xline);
      sfname = shorten_fname1(nameBuffG);

      book = bookNew(nameBuffG, sfname, (LineNr)0, BLN_LISTED);
      if (book != NULL) {  // just in case...
         book->lastCursor.lnum = lnum;
         book->lastCursor.col = col;
         bookSetPosInPort(book, curPor, lnum, col, false);
      }
   }
   eeglFree(xline);

   return eeglinfo_readline(virp);
}

// Return true if "name" is on removable media (depending on @eeglinfo).
private Boole
removable(CS name) {
   if (!p_eeglinfo)
      return false;
      
   Byte part[51];
   Boole retval = false;
   Unt  n;
   name = home_replace_save(NULL, name);
   for (CS p = p_eeglinfo; *p; ) {
      doCutPathFromListOfPaths(OUT &p, OUT part, 51, S", ");
      if (part[0] == 'r') {
         n = STRLEN(part + 1);
         if (MB_STRNICMP(part + 1, name, n) == 0) {
            retval = true;
            break;
         }
      }
   }
   eeglFree(name);
   return retval;
}

private void
writeEeglInfoBookList(FILE* fp) {
   if (find_eeglinfo_parameter('%') == NULL)
      return;

   // Without a number -1 is returned: do all books.
   int max_buffers = get_eeglinfo_parameter('%');

   // Allocate room for the file name, lnum and col.
#define LINE_BUF_LEN (MAXPATHL + 40)
   Byte line[LINE_BUF_LEN];

   Tab* tp;
   Portal* port;
   FOR_ALL_TAB_PORTALS(tp, port) {
      set_last_cursor(port);
   } 

   FPUTS(_("\n# Book list:\n"), fp);
   Book* book;
   FOR_ALL_BOOKS(book) {
      if (book->currFileName == NULL
            || !book->o.bookListed
            || isLocationListBook(book)
            || bt_terminal(book)
            || removable(book->fullFileName))
         continue;

      if (max_buffers-- == 0)
         break;
      putc('%', fp);
      home_replace(book->fullFileName, line, MAXPATHL, true);
      eeSnprintfAdd(line, LINE_BUF_LEN, "\t%ld\t%d",
            (long)book->lastCursor.lnum,
            book->lastCursor.col);
      eeglinfo_writestring(fp, line);
   }
}

// Buffers for history read from a eeglinfo file.  Only valid while reading.
private HistoryEntry *eeglinfo_history[HIST_COUNT] = {NULL, NULL, NULL, NULL, NULL};
private int   eeglinfo_hisidx[HIST_COUNT] = {0, 0, 0, 0, 0};
private int   eeglinfo_hislen[HIST_COUNT] = {0, 0, 0, 0, 0};
private int   eeglinfo_add_at_front = false;

// Translate a history type number to the associated character.
private int
hist_type2char(int type, int use_question) {      // use '?' instead of '/'
   if (type == HIST_CMD)
      return ':';
   if (type == HIST_SEARCH) {
      if (use_question)
         return '?';
      else
         return '/';
   }
   if (type == HIST_EXPR)
      return '=';
   return '@';
}

//Prepare for reading the history from the eeglinfo file.
//This allocates history arrays to store the read history lines.
private void
prepare_eeglinfo_history(int asklen, int writing) {
   init_history();
   int hislen = getHistLen();
   eeglinfo_add_at_front = (asklen != 0 && !writing);
   if (asklen > hislen)
      asklen = hislen;

   for (int type = 0; type < HIST_COUNT; ++type) {
      HistoryEntry *histentry = get_histentry(type);

      //Count the number of empty spaces in the history list.  Entries read from eeglinfo previously
      //are also considered empty. If there are more spaces available than we request, then fill 
      //them up.
      int num;
      int i;
      for (i = 0, num = 0; i < hislen; i++)
         if (histentry[i].hisstr == NULL || histentry[i].eeglinfo)
            num++;
      int len = asklen;
      if (num > len)
         len = num;
      if (len <= 0)
         eeglinfo_history[type] = NULL;
      else
         eeglinfo_history[type] = LALLOC_MULT(HistoryEntry, len);
      if (eeglinfo_history[type] == NULL)
         len = 0;
      eeglinfo_hislen[type] = len;
      eeglinfo_hisidx[type] = 0;
   }
}

//Accept a line from the eeglinfo, store it in the history array when it's new.
private int
read_eeglinfo_history(Vir* virp, int writing) {
   int type = hist_char2type(virp->line[0]);
   if (eeglinfo_hisidx[type] >= eeglinfo_hislen[type])
      goto done;

   CS val = eeglinfo_readstring(virp, 1);
   if (!val || *val == ZERO)
      goto done;

   int sep = (*val == ' ' ? ZERO : *val);

   if (in_history(type, val + (type == HIST_SEARCH), eeglinfo_add_at_front, sep, writing))
      goto done;

   // Need to re-allocate to append the separator byte.
   Ulong len = STRLEN(val);
   CS p;
   if (type == HIST_SEARCH) {
      p = alloc((Unt)len + 1); // +1 for the ZERO. val already includes the separator.

      // Search entry: Move the separator from the first column to after the ZERO.
      MEMMOVE(p, val + 1, (Unt)len);
      p[len] = sep;
      --len;                // take into account the shortened string
   } else {
      p = alloc((Unt)len + 2);       // +1 for ZERO and +1 for separator

      // Not a search entry: No separator in the eeglinfo file, add a ZERO separator.
      MEMMOVE(p, val, (Unt)len + 1);   // +1 to include the ZERO
      p[len + 1] = ZERO;         // put the separator *after* the string's ZERO
   }
   eeglinfo_history[type][eeglinfo_hisidx[type]].hisstr = p;
   eeglinfo_history[type][eeglinfo_hisidx[type]].hisstrlen = (Unt)len;
   eeglinfo_history[type][eeglinfo_hisidx[type]].time_set = 0;
   eeglinfo_history[type][eeglinfo_hisidx[type]].eeglinfo = true;
   eeglinfo_history[type][eeglinfo_hisidx[type]].hisnum = 0;
   eeglinfo_hisidx[type]++;

done:
   eeglFree(val);
   return eeglinfo_readline(virp);
}

//Accept a new style history line from the eeglinfo, store it in the history array when it's new.
private void
handle_eeglinfo_history(ArrayList* values, int writing) {
   BVal* vp = (BVal *)values->c;

   // Check the format:
   // |{bartype},{histtype},{timestamp},{separator},"text"
   if (values->len < 4
        || vp[0].btag != BVAL_NR
        || vp[1].btag != BVAL_NR
        || (vp[2].btag != BVAL_NR && vp[2].btag != BVAL_EMPTY)
        || vp[3].btag != BVAL_STRING)
      return;

   int type = vp[0].bv_nr;
   if (type >= HIST_COUNT)
      return;

   if (eeglinfo_hisidx[type] >= eeglinfo_hislen[type])
      return;

   CS val = vp[3].bv_string;
   if (!val || *val == ZERO)
      return;

   int sep = type == HIST_SEARCH && vp[2].btag == BVAL_NR ? vp[2].bv_nr : ZERO;
   int idx;
   int overwrite = false;

   if (in_history(type, val, eeglinfo_add_at_front, sep, writing))
      return;

   Ulong len;
   CS p;
   
   // If lines were written by an older Eegl, we need to avoid getting duplicates. See if the 
   // entry already exists.
   for (idx = 0; idx < eeglinfo_hisidx[type]; ++idx) {
      p = eeglinfo_history[type][idx].hisstr;
      len = eeglinfo_history[type][idx].hisstrlen;
      if (STRCMP(val, p) == 0 && (type != HIST_SEARCH || sep == p[len + 1])) {
          overwrite = true;
          break;
      }
   }

   if (!overwrite) {
      // Need to re-allocate to append the separator byte.
      len = vp[3].bv_len;
      p = alloc(len + 2);
   } else
      len = 0; // for picky compilers
   if (p) {
      eeglinfo_history[type][idx].time_set = vp[1].bv_nr;
      if (!overwrite) {
          MEMMOVE(p, val, (Unt)len + 1);
          // Put the separator after the ZERO.
          p[len + 1] = sep;
          eeglinfo_history[type][idx].hisstr = p;
          eeglinfo_history[type][idx].hisstrlen = (Unt)len;
          eeglinfo_history[type][idx].hisnum = 0;
          eeglinfo_history[type][idx].eeglinfo = true;
          eeglinfo_hisidx[type]++;
      }
   }
}

//Concatenate history lines from eeglinfo after the lines typed in this Eegl.
private void
concat_history(int type) {
   int i;
   int hislen = getHistLen();
   HistoryEntry *histentry = get_histentry(type);
   int* hisidx = get_hisidx(type);
   int* hisnum = get_hisnum(type);

   int idx = *hisidx + eeglinfo_hisidx[type];
   if (idx >= hislen)
      idx -= hislen;
   ei (idx < 0)
      idx = hislen - 1;
   if (eeglinfo_add_at_front)
      *hisidx = idx;
   else {
      if (*hisidx == -1)
          *hisidx = hislen - 1;
      do {
         if (histentry[idx].hisstr != NULL || histentry[idx].eeglinfo)
            break;
         if (++idx == hislen)
            idx = 0;
      } while (idx != *hisidx);
      if (idx != *hisidx && --idx < 0)
         idx = hislen - 1;
   }
   for (i = 0; i < eeglinfo_hisidx[type]; i++) {
      eeglFree(histentry[idx].hisstr);
      histentry[idx].hisstr = eeglinfo_history[type][i].hisstr;
      histentry[idx].hisstrlen = eeglinfo_history[type][i].hisstrlen;
      histentry[idx].eeglinfo = true;
      histentry[idx].time_set = eeglinfo_history[type][i].time_set;
      if (--idx < 0)
         idx = hislen - 1;
   }
   idx += 1;
   idx %= hislen;
   for (i = 0; i < eeglinfo_hisidx[type]; i++) {
      histentry[idx++].hisnum = ++*hisnum;
      idx %= hislen;
   }
}

private int
sort_hist(const void *s1, const void *s2) {
   HistoryEntry* p1 = *(HistoryEntry **)s1;
   HistoryEntry* p2 = *(HistoryEntry **)s2;

   if (p1->time_set < p2->time_set) return -1;
   if (p1->time_set > p2->time_set) return 1;
   return 0;
}

//Merge history lines from eeglinfo and lines typed in this Eegl based on the timestamp;
private void
merge_history(int type) {
   HistoryEntry **tot_hist;
   HistoryEntry *new_hist;
   int hislen = getHistLen();
   HistoryEntry *histentry = get_histentry(type);
   int* hisidx = get_hisidx(type);
   int* hisnum = get_hisnum(type);

   // Make one long list with all entries.
   int max_len = hislen + eeglinfo_hisidx[type];
   tot_hist = ALLOC_MULT(HistoryEntry *, max_len);
   new_hist = ALLOC_MULT(HistoryEntry, hislen);
   if (tot_hist == NULL || new_hist == NULL) {
      eeglFree(tot_hist);
      eeglFree(new_hist);
      return;
   }
   int i;
   for (i = 0; i < eeglinfo_hisidx[type]; i++)
      tot_hist[i] = &eeglinfo_history[type][i];
   int len = i;
   for (i = 0; i < hislen; i++) {
      if (histentry[i].hisstr != NULL)
         tot_hist[len++] = &histentry[i];
   } 

   // Sort the list on timestamp.
   qsort((void *)tot_hist, (Unt)len, sizeof(HistoryEntry *), sort_hist);

   // Keep the newest ones.
   for (i = 0; i < hislen; i++) {
      if (i < len) {
          new_hist[i] = *tot_hist[i];
          tot_hist[i]->hisstr = NULL;
          tot_hist[i]->hisstrlen = 0;
          if (new_hist[i].hisnum == 0)
         new_hist[i].hisnum = ++*hisnum;
      } else
         clear_hist_entry(&new_hist[i]);
   }
   *hisidx = (i < len ? i : len) - 1;

   // Free what is not kept.
   for (i = 0; i < eeglinfo_hisidx[type]; i++) {
      eeglFree(eeglinfo_history[type][i].hisstr);
      eeglinfo_history[type][i].hisstrlen = 0;
   }
   for (i = 0; i < hislen; i++) {
      eeglFree(histentry[i].hisstr);
      histentry[i].hisstrlen = 0;
   }
   eeglFree(histentry);
   set_histentry(type, new_hist);
   eeglFree(tot_hist);
}

// Finish reading history lines from eeglinfo.  Not used when writing eeglinfo.
private void
finish_eeglinfo_history(Vir *virp) {
   int type;
   int merge = virp->vir_version >= EEGLINFO_VERSION_WITH_HISTORY;

   for (type = 0; type < HIST_COUNT; ++type) {
      if (get_histentry(type) == NULL)
         continue;

      if (merge)
         merge_history(type);
      else
         concat_history(type);

      EE_CLEAR(eeglinfo_history[type]);
      eeglinfo_hisidx[type] = 0;
   }
}

//Write history to eeglinfo file in "fp".
//When "merge" is true merge history lines with a previously read eeglinfo
//file, data is in eeglinfo_history[].
//When "merge" is false just write all history lines.  Used for ":weeglinfo!".
private void
write_eeglinfo_history(FILE *fp, int merge) {
   int i;
   int type;
   int num_saved;
   int round;

   init_history();
   int hislen = getHistLen();
   if (hislen == 0)
      return;
   for (type = 0; type < HIST_COUNT; ++type) {
      HistoryEntry *histentry = get_histentry(type);
      int       *hisidx = get_hisidx(type);

      num_saved = get_eeglinfo_parameter(hist_type2char(type, false));
      if (num_saved == 0)
          continue;
      if (num_saved < 0)  // Use default
          num_saved = hislen;
      fprintf(fp, (char*)_("\n# %s History (newest to oldest):\n"),
                type == HIST_CMD ? _("Command Line") :
                type == HIST_SEARCH ? _("Search String") :
                type == HIST_EXPR ? _("Expression") :
                type == HIST_INPUT ? _("Input Line") :
                  _("Debug Line"));
      if (num_saved > hislen)
          num_saved = hislen;

      // Merge typed and eeglinfo history:
      // round 1: history of typed commands.
      // round 2: history from recently read eeglinfo.
      for (round = 1; round <= 2; ++round) {
         if (round == 1)
            // start at newest entry, somewhere in the list
            i = *hisidx;
         ei (eeglinfo_hisidx[type] > 0)
            // start at newest entry, first in the list
            i = 0;
         else
            // empty list
            i = -1;
         if (i >= 0) {
            while (num_saved > 0 && !(round == 2 && i >= eeglinfo_hisidx[type])) {
               CS p;
               Unt plen;
               Tyme timestamp;
               int c = ZERO;

               if (round == 1) {
                  p = histentry[i].hisstr;
                  plen = histentry[i].hisstrlen;
                  timestamp = histentry[i].time_set;
               } else {
                  if (eeglinfo_history[type] == NULL) {
                     p = NULL;
                     plen = 0;
                     timestamp = 0;
                  } else {
                     p = eeglinfo_history[type][i].hisstr;
                     plen = eeglinfo_history[type][i].hisstrlen;
                     timestamp = eeglinfo_history[type][i].time_set;
                  }
               }

               if (p != NULL && (round == 2 || !merge || !histentry[i].eeglinfo)) {
                  --num_saved;
                  fputc(hist_type2char(type, true), fp);
                  // For the search history: put the separator in the
                  // second column; use a space if there isn't one.
                  if (type == HIST_SEARCH) {
                      c = p[plen + 1];
                      putc(c == ZERO ? ' ' : c, fp);
                  }
                  eeglinfo_writestring(fp, p);

                  {
                     char    cbuf[NUMBUFLEN];

                     // New style history with a bar line. Format:
                     // |{bartype},{histtype},{timestamp},{separator},"text"
                     if (c == ZERO)
                        cbuf[0] = ZERO;
                     else
                        sprintf(cbuf, "%d", c);
                     fprintf(fp, "|%d,%d,%ld,%s,", BARTYPE_HISTORY, type, (long)timestamp, cbuf);
                     barline_writestring(fp, p, LSIZE - 20);
                     putc('\n', fp);
                  }
               }
               if (round == 1) {
                  // Decrement index, loop around and stop when back at the start.
                  if (--i < 0)
                     i = hislen - 1;
                  if (i == *hisidx)
                     break;
               } else {
                  // Increment index. Stop at the end in the while.
                  ++i;
               }
            }
         } 
      }
      for (i = 0; i < eeglinfo_hisidx[type]; ++i) {
         if (eeglinfo_history[type] != NULL) {
            eeglFree(eeglinfo_history[type][i].hisstr);
            eeglinfo_history[type][i].hisstrlen = 0;
         }
      } 
      EE_CLEAR(eeglinfo_history[type]);
      eeglinfo_hisidx[type] = 0;
   }
}

private void
write_eeglinfo_barlines(Vir *virp, FILE *fp_out) {
   int i;
   ArrayList* gap = &virp->vir_barlines;
   int seen_useful = false;
   char* line;

   if (gap->len <= 0)
      return;

   FPUTS(_("\n# Bar lines, copied verbatim:\n"), fp_out);

   // Skip over continuation lines until seeing a useful line.
   for (i = 0; i < gap->len; ++i) {
      line = ((char **)(gap->c))[i];
      if (seen_useful || line[1] != '<') {
         fputs(line, fp_out);
         seen_useful = true;
      }
   }
}

//Parse a eeglinfo line starting with '|'. Add each decoded value to "values".
//Return true if the next line is to be read after using the parsed values.
private int
barline_parse(Vir* virp, CS text, ArrayList* values) {
   CS p = text;
   CS nextp = NULL;
   CS buf = NULL;
   BVal  *value;
   int i;
   int allocated = false;
   int eof;
   int converted;

   while (*p == ',') {
      ++p;
      if (ga_grow(values, 1) == FAIL)
         break;
      value = (BVal *)(values->c) + values->len;

      if (*p == '>') {
         // Need to read a continuation line.  Put strings in allocated
         // memory, because virp->line is overwritten.
         if (!allocated) {
            for (i = 0; i < values->len; ++i) {
               BVal  *vp = (BVal *)(values->c) + i;

               if (vp->btag == BVAL_STRING && !vp->bv_allocated) {
                  vp->bv_string = copySubstr(vp->bv_string, vp->bv_len);
                  vp->bv_allocated = true;
               }
            }
            allocated = true;
         }

         if (eeIsDigit(p[1])) {
            Unt len;
            Unt todo;
            Unt n;

            // String value was split into lines that are each shorter
            // than LSIZE:
            //     |{bartype},>{length of "{text}{text2}"}
            //     |<"{text1}
            //     |<{text2}",{value}
            // Length includes the quotes.
            ++p;
            len = parseLong(&p);
            buf = alloc((int)(len + 1));
            p = buf;
            for (todo = len; todo > 0; todo -= n) {
               eof = eeglinfo_readline(virp);
               if (eof || virp->line[0] != '|' || virp->line[1] != '<') {
                  // File was truncated or garbled. Read another line if this one starts with '|'.
                  eeglFree(buf);
                  return eof || virp->line[0] == '|';
               }
               // Get length of text, excluding |< and NL chars.
               n = STRLEN(virp->line);
               while (n > 0 && (virp->line[n - 1] == NL || virp->line[n - 1] == ENTER))
                  --n;
               n -= 2;
               if (n > todo) {
                  // more values follow after the string
                  nextp = virp->line + 2 + todo;
                  n = todo;
               }
                MEMMOVE(p, virp->line + 2, n);
                p += n;
            }
            *p = ZERO;
            p = buf;
          } else {
            // Line ending in ">" continues in the next line:
            //     |{bartype},{lots of values},>
            //     |<{value},{value}
            eof = eeglinfo_readline(virp);
            if (eof || virp->line[0] != '|' || virp->line[1] != '<')
               // File was truncated or garbled. Read another line if
               // this one starts with '|'.
               return eof || virp->line[0] == '|';
            p = virp->line + 2;
         }
      }

      if (SAFE_isdigit(*p)) {
          value->btag = BVAL_NR;
          value->bv_nr = parseLong(&p);
          ++values->len;
      } ei (*p == '"') {
         int len = 0;
         CS s = p;

         // Unescape special characters in-place.
         ++p;
         while (*p != '"') {
            if (*p == NL || *p == ZERO)
               return true;  // syntax error, drop the value
            if (*p == '\\') {
               ++p;
               if (*p == 'n')
                  s[len++] = '\n';
               else
                  s[len++] = *p;
               ++p;
            } else
               s[len++] = *p++;
         }
         ++p;
         s[len] = ZERO;

         converted = false;
         value->bv_tofree = NULL;

         // Need to copy in allocated memory if the string wasn't allocated
         // above and we did allocate before, thus line may change.
         if (s != buf && allocated && !converted)
            s = copySubstr(s, len);
         value->bv_string = s;
         value->btag = BVAL_STRING;
         value->bv_len = len;
         value->bv_allocated = allocated || converted;
         ++values->len;
         if (nextp) {
            // values following a long string
            p = nextp;
            nextp = NULL;
         }
      } ei (*p == ',') {
         value->btag = BVAL_EMPTY;
         ++values->len;
      } else
         break;
   }
   return true;
}

private void
write_eeglinfo_version(FILE* fp_out) {
   fprintf(fp_out, "# Eeglinfo version\n|%d,%d\n\n", BARTYPE_VERSION, EEGLINFO_VERSION);
}

private int
no_eeglinfo(void) {
   // "vim -i NONE" does not read or write a eeglinfo file
   return !p_eeglinfofile || STRCMP(p_eeglinfofile, "NONE") == 0;
}

//Report an error for reading a eeglinfo file.
//Count the number of errors.   When there are more than 10, return true.
private int
eeglinfo_error(CS errnum, CS message, Byte *line) {
   eeSnprintf(IObuff, IOSIZE, _("%seeglinfo: %s in line: "), errnum, message);
   STRNCAT(IObuff, line, IOSIZE - STRLEN(IObuff) - 1);
   if (IObuff[STRLEN(IObuff) - 1] == '\n')
      IObuff[STRLEN(IObuff) - 1] = ZERO;
   emsg(IObuff);
   if (++eeglinfo_errcnt >= 10) {
      emsg(_(e_eeglinfo_too_many_errors_skipping_rest_of_file));
      return true;
   }
   return false;
}

//Restore global vars that start with a capital from the eeglinfo file
private int
read_eeglinfo_varlist(Vir* virp, int writing) {
   int type = VAR_NUMBER;
   Var tv;
   FnCallEntry funccal_entry;

   if (!writing && (find_eeglinfo_parameter('!') != NULL)) {
      CS tab = firstOccurrence(virp->line + 1, '\t');
      if (tab) {
         *tab++ = '\0';   // isolate the variable name
         switch (*tab) {
         case 'S': type = VAR_STRING; break;
         case 'F': type = VAR_FLOAT; break;
         case 'D': type = VAR_BAG; break;
         case 'L': type = VAR_LIST; break;
         case 'B': type = VAR_BLOB; break;
         case 'X': type = VAR_SPECIAL; break;
         }

         tab = firstOccurrence(tab, '\t');
         if (tab) {
            tv.tag = type;
            if (type == VAR_STRING || type == VAR_BAG || type == VAR_LIST || type == VAR_BLOB)
               tv.string = eeglinfo_readstring(virp, (int)(tab - virp->line + 1));
            ei (type == VAR_FLOAT)
               (void)string2float(tab + 1, OUT &tv.floatt, false);
            else {
               tv.number = atol((char *)tab + 1);
               if (type == VAR_SPECIAL && (tv.number == VVAL_FALSE || tv.number == VVAL_TRUE))
                  tv.tag = VAR_BOOL;
            }
            if (type == VAR_BAG || type == VAR_LIST) {
               Var *etv = eval_expr(tv.string, NULL);

               if (etv == NULL)
                  // Failed to parse back the dict or list, use it as a string.
                  tv.tag = VAR_STRING;
               else {
                  eeglFree(tv.string);
                  tv = *etv;
                  eeglFree(etv);
                }
            } ei (type == VAR_BLOB) {
               Blob *blob = string2blob(tv.string);

               if (blob == NULL)
                  // Failed to parse back the blob, use it as a string.
                  tv.tag = VAR_STRING;
               else {
                  eeglFree(tv.string);
                  tv.tag = VAR_BLOB;
                  tv.blob = blob;
               }
            }

            // when in a function use global variables
            save_funccal(&funccal_entry);
            set_var(mbText(virp->line + 1), &tv, false);
            restore_funccal();

            if (tv.tag == VAR_STRING)
               eeglFree(tv.string);
            ei (tv.tag == VAR_BAG || tv.tag == VAR_LIST || tv.tag == VAR_BLOB)
               clearVar(&tv);
         }
      }
   }

   return eeglinfo_readline(virp);
}

// Write global vars that start with a capital to the eeglinfo file
private void
write_eeglinfo_varlist(FILE* fp) {
   EeSet   *gvht = get_globvar_ht();
   EeSetItem   *hi;
   CS s = E;
   CS p;
   CS tofree;
   Byte   numbuf[NUMBUFLEN];

   if (find_eeglinfo_parameter('!') == NULL)
      return;

   FPUTS(_("\n# global variables:\n"), fp);

   int todo = (int)gvht->count;
   FOR_ALL_HASHTAB_ITEMS(gvht, hi, todo) {
      if (!HASHITEM_EMPTY(hi)) {
         --todo;
         DictItem* this_var = HI2DI(hi);
         if (getVarFlavor(this_var->key) == VAR_FLAVOR_EEGLINFO) {
            switch (this_var->c.tag) {
            case VAR_STRING:  s = S"STR"; break;
            case VAR_NUMBER:  s = S"NUM"; break;
            case VAR_FLOAT:   s = S"FLO"; break;
            case VAR_BAG: {
               Bag   *di = this_var->c.bag;
               int   copyID = get_copyID();

               s = S"DIC";
               if (di && !setRefInSet(&di->hashTable, copyID, NULL) && di->copyId == copyID)
                  // has a circular reference, can't turn the value into a string
                  continue;
               break;
            }
            case VAR_LIST: {
               List   *l = this_var->c.list;
               int   copyID = get_copyID();

               s = S"LIS";
               if (l && !set_ref_in_list_items(l, copyID, NULL) && l->copyId == copyID)
                  // has a circular reference, can't turn the value into a string
                  continue;
               break;
            }
            case VAR_BLOB:    s = S"BLO"; break;
            case VAR_BOOL:    s = S"XPL"; break;  // backwards compat.
            case VAR_SPECIAL: s = S"XPL"; break;

            case VAR_UNKNOWN:
            case VAR_ANY:
            case VAR_VOID:
            case VAR_FUNC:
            case VAR_PARTIAL:
            case VAR_JOB:
            case VAR_CHANNEL:
               continue;
            }
            fprintf(fp, "!%s\t%s\t", this_var->key, s);
            if (this_var->c.tag == VAR_BOOL || this_var->c.tag == VAR_SPECIAL) {
               // do not use "v:true" but "1"
               sprintf((char *)numbuf, "%ld", (long)this_var->c.number);
               p = numbuf;
               tofree = NULL;
            } else
               p = echo_string(&this_var->c, &tofree, numbuf, 0);
            if (p)
               eeglinfo_writestring(fp, p);
            eeglFree(tofree);
         }
      }
   }
}

private int
read_eeglinfo_sub_string(Vir* virp, int force) {
   if (force || get_old_sub() == NULL)
      set_old_sub(eeglinfo_readstring(virp, 1));
   return eeglinfo_readline(virp);
}

private void
write_eeglinfo_sub_string(FILE *fp) {
   CS old_sub = get_old_sub();

   if (get_eeglinfo_parameter('/') == 0 || old_sub == NULL)
      return;

   FPUTS(_("\n# Last Substitute String:\n$"), fp);
   eeglinfo_writestring(fp, old_sub);
}

//Functions relating to reading/writing the search pattern from eeglinfo

private int
read_eeglinfo_search_pattern(Vir* virp, Boole force) {
   int idx = -1;
   int magic = false;
   int no_scs = false;
   int off_line = false;
   int off_end = 0;
   long off = 0;
   int setlast = false;
   static Boole   hlsearch_on = false;
   Byte* val;
   SearchPattern* spat;

   // Old line types:
   // "/pat", "&pat": search/subst. pat
   // "~/pat", "~&pat": last used search/subst. pat
   // New line types:
   // "~h", "~H": hlsearch hiliting off/on
   // "~<magic><smartcase><line><end><off><last><which>pat"
   // <magic>: 'm' off, 'M' on
   // <smartcase>: 's' off, 'S' on
   // <line>: 'L' line offset, 'l' char offset
   // <end>: 'E' from end, 'e' from start
   // <off>: decimal, offset
   // <last>: '~' last used pattern
   // <which>: '/' search pat, '&' subst. pat
   CS lp = virp->line;
   if (lp[0] == '~' && (lp[1] == 'm' || lp[1] == 'M')) {  // new line type
      if (lp[1] == 'M')      // magic on
         magic = true;
      if (lp[2] == 's')
         no_scs = true;
      if (lp[3] == 'L')
         off_line = true;
      if (lp[4] == 'E')
         off_end = SEARCH_END;
      lp += 5;
      off = parseLong(&lp);
   }
   if (lp[0] == '~') {     // use this pattern for last-used pattern
      setlast = true;
      lp++;
   }
   if (lp[0] == '/')
      idx = RE_SEARCH;
   ei (lp[0] == '&')
      idx = RE_SUBST;
   ei (lp[0] == 'h')   // ~h: 'hlsearch' hiliting off
      hlsearch_on = false;
   ei (lp[0] == 'H')   // ~H: 'hlsearch' hiliting on
      hlsearch_on = true;
   if (idx >= 0) {
      spat = getPrevSearchPattern(idx);
      if (force || spat->pat.len == 0) {
         val = eeglinfo_readstring(virp, (int)(lp - virp->line + 1));
         if (val) {
            set_last_search_pat(val, idx, magic, setlast);
            eeglFree(val);
            spat->no_scs = no_scs;
            spat->off.line = off_line;
            spat->off.end = off_end;
            spat->off.off = off;
            if (setlast)
               setHlsearch(hlsearch_on);
         }
      }
   }
   return eeglinfo_readline(virp);
}

private void
wvsp_one(
   FILE* fp,   // file to write to
   int idx,   // spats[] index
   CS s,   // search pat
   int sc   // dir char
){
   SearchPattern* spat = getPrevSearchPattern(idx);
   if (spat->pat.len == 0)
      return;

   fprintf(fp, (char*)_("\n# Last %sSearch Pattern:\n~"), s);
   // off.dir is not stored, it's reset to forward
   fprintf(
      fp, "%c%c%c%c%ld%s%c",
      spat->magic    ? 'M' : 'm',   // magic
      spat->no_scs   ? 's' : 'S',   // smartcase
      spat->off.line ? 'L' : 'l',   // line offset
      spat->off.end  ? 'E' : 'e',   // offset from end
      spat->off.off,         // offset
      getPrevSearchOrSubstPattern() == idx ? "~" : "",   // last used pat
      sc
   );
   eeglinfo_writestring(fp, spat->pat.c);
}

private void
write_eeglinfo_search_pattern(FILE* fp) {
   if (get_eeglinfo_parameter('/') == 0)
      return;

   fprintf(fp, "\n# hlsearch on (H) or off (h):\n~%c",
       (!hiliteSearchG || find_eeglinfo_parameter('h') != NULL) ? 'h' : 'H');
   wvsp_one(fp, RE_SEARCH, E, '/');
   wvsp_one(fp, RE_SUBST, _("Substitute "), '&');
}

// Functions relating to reading/writing registers from eeglinfo

private YankReg *y_read_regs = NULL;

#define REG_PREVIOUS 1
#define REG_EXEC 2

// Prepare for reading eeglinfo registers when writing eeglinfo later.
private void
prepare_eeglinfo_registers(void) {
   y_read_regs = ALLOC_CLEAR_MULT(YankReg, NUM_REGISTERS);
}

private void
finish_eeglinfo_registers(void) {
   if (!y_read_regs)
      return;

   for (Unt i = 0; i < NUM_REGISTERS; ++i) {
      if (y_read_regs[i].y_array != NULL) {
         for (Unt j = 0; j < y_read_regs[i].y_size; j++)
            eeglFree(y_read_regs[i].y_array[j].c);
         eeglFree(y_read_regs[i].y_array);
      }
   } 
   EE_CLEAR(y_read_regs);
}

private int
read_eeglinfo_register(Vir* virp, Boole force) {
   int eof;
   int do_it = true;
   int set_prev = false;
   Arr(Text) array = NULL;
   int      new_type = MCHAR; // init to shut up compiler
   ColNr   new_width = 0; // init to shut up compiler
   YankReg   *y_current_p;

   // We only get here (hopefully) if line[0] == '"'
   CS str = virp->line + 1;

   // If the line starts with "" this is the y_previous register.
   if (*str == '"') {
      set_prev = true;
      str++;
   }

   if (!ASCII_ISALNUM(*str) && *str != '-') {
      if (eeglinfo_error(S"E577: ", _(e_illegal_register_name), virp->line))
          return true;   // too many errors, pretend end-of-file
      do_it = false;
   }
   get_yank_register(*str++, false);
   y_current_p = get_y_current();
   if (!force && y_current_p->y_array != NULL)
      do_it = false;

   if (*str == '@') {
      // "x@: register x used for @@
      if (force || get_execreg_lastc() == ZERO)
          set_execreg_lastc(str[-1]);
   }

   int size = 0;
   int limit = 100;   // Optimized for registers containing <= 100 lines
   if (do_it) {
      // Build the new register in array[].
      // y_array is kept as-is until done.
      // The "do_it" flag is reset when something is wrong, in which case
      // array[] needs to be freed.
      if (set_prev)
         set_y_previous(y_current_p);
      array = ALLOC_MULT(Text, limit);
      str = skipwhite(skiptowhite(str));
      if (STRNCMP(str, "CHAR", 4) == 0)
         new_type = MCHAR;
      ei (STRNCMP(str, "BLOCK", 5) == 0)
         new_type = MBLOCK;
      else
         new_type = MLINE;
      // get the block width; if it's missing we get a zero, which is OK
      str = skipwhite(skiptowhite(str));
      new_width = parseLong(&str);
   }

   while (!(eof = eeglinfo_readline(virp))
          && (virp->line[0] == TAB || virp->line[0] == '<')
   ) {
      if (do_it) {
         if (size == limit) {
            Arr(Text) new_array = (Text *)alloc(limit * 2 * sizeof(Text));
            if (!new_array) {
               do_it = false;
               break;
            }
            for (int i = 0; i < limit; i++)
               new_array[i] = array[i];
            eeglFree(array);
            array = new_array;
            limit *= 2;
         }
         str = eeglinfo_readstring(virp, 1);
         if (str) {
            array[size].c = str;
            array[size].len = STRLEN(str);
            ++size;
         } else
            // error, don't store the result
            do_it = false;
      }
   }

   if (do_it) {
      // free y_array[]
      for (int i = 0; i < y_current_p->y_size; i++)
         eeglFree(y_current_p->y_array[i].c);
      eeglFree(y_current_p->y_array);

      y_current_p->y_type = new_type;
      y_current_p->y_width = new_width;
      y_current_p->y_size = size;
      y_current_p->y_time_set = 0;
      if (size == 0) {
         y_current_p->y_array = NULL;
      } else {
         // Move the lines from array[] to y_array[].
         y_current_p->y_array = ALLOC_MULT(Text, size);
         for (int i = 0; i < size; i++) {
            if (y_current_p->y_array == NULL) {
               EE_CLEAR_STRING(array[i]);
            } else {
               y_current_p->y_array[i] = array[i];
            }
         }
      }
    } else {
      // Free array[] if it was filled.
      for (int i = 0; i < size; i++)
         eeglFree(array[i].c);
   }
   eeglFree(array);

   return eof;
}

//Accept a new style register line from the eeglinfo, store it when it's new.
private void
handle_eeglinfo_register(ArrayList *values, int force) {
   BVal   *vp = (BVal *)values->c;
   time_t   timestamp;
   YankReg   *y_ptr;
   YankReg   *y_regs_p = get_y_regs();
   int      i;

   // Check the format:
   // |{bartype},{flags},{name},{type},
   //      {linecount},{width},{timestamp},"line1","line2"
   if (values->len < 6
       || vp[0].btag != BVAL_NR
       || vp[1].btag != BVAL_NR
       || vp[2].btag != BVAL_NR
       || vp[3].btag != BVAL_NR
       || vp[4].btag != BVAL_NR
       || vp[5].btag != BVAL_NR
   )
      return;
   int flags = vp[0].bv_nr;
   int name = vp[1].bv_nr;
   if (name < 0 || name >= NUM_REGISTERS)
      return;
   int type = vp[2].bv_nr;
   if (type != MCHAR && type != MLINE && type != MBLOCK)
      return;
   int linecount = vp[3].bv_nr;
   if (values->len < 6 + linecount)
      return;
   int width = vp[4].bv_nr;
   if (width < 0)
      return;

   if (y_read_regs)
      //Reading eeglinfo for merging and writing.  Store the register
      //content, don't update the current registers.
      y_ptr = &y_read_regs[name];
   else
      y_ptr = &y_regs_p[name];

   //Do not overwrite unless forced or the timestamp is newer.
   timestamp = (time_t)vp[5].bv_nr;
   if (y_ptr->y_array && !force && (timestamp == 0 || y_ptr->y_time_set > timestamp))
      return;

   if (y_ptr->y_array) {
      for (i = 0; i < y_ptr->y_size; i++)
         eeglFree(y_ptr->y_array[i].c);
   } 
   eeglFree(y_ptr->y_array);

   if (!y_read_regs) {
      if (flags & REG_PREVIOUS)
          set_y_previous(y_ptr);
      if ((flags & REG_EXEC) && (force || get_execreg_lastc() == ZERO))
          set_execreg_lastc(get_register_name(name));
   }
   y_ptr->y_type = type;
   y_ptr->y_width = width;
   y_ptr->y_size = linecount;
   y_ptr->y_time_set = timestamp;
   if (linecount == 0) {
      y_ptr->y_array = NULL;
      return;
   }
   y_ptr->y_array = ALLOC_MULT(Text, linecount);
   if (y_ptr->y_array == NULL) {
      y_ptr->y_size = 0; // ensure object state is consistent
      return;
   }
   for (i = 0; i < linecount; i++) {
      if (vp[i + 6].bv_allocated) {
         y_ptr->y_array[i].c = vp[i + 6].bv_string;
         y_ptr->y_array[i].len = vp[i + 6].bv_len;
         vp[i + 6].bv_string = NULL;
      } ei (vp[i + 6].btag != BVAL_STRING) {
         free(y_ptr->y_array);
         y_ptr->y_array = NULL;
      } else {
         y_ptr->y_array[i].c = copySubstr(vp[i + 6].bv_string, vp[i + 6].bv_len);
         y_ptr->y_array[i].len = vp[i + 6].bv_len;
      }
    }
}

private void
write_eeglinfo_registers(FILE* fp) {
   int      i, j;
   Byte   *type;
   Byte c;
   int num_lines;
   long len;
   YankReg   *y_ptr;
   YankReg   *y_regs_p = get_y_regs();;

   FPUTS(_("\n# Registers:\n"), fp);

   // Get '<' value, use old '"' value if '<' is not found.
   int max_num_lines = get_eeglinfo_parameter('<');
   if (max_num_lines < 0)
      max_num_lines = get_eeglinfo_parameter('"');
   if (max_num_lines == 0)
      return;
   int max_kbyte = get_eeglinfo_parameter('s');
   if (max_kbyte == 0)
      return;

   for (i = 0; i < NUM_REGISTERS; i++) {
      // Skip '*'/'+' register, we don't want them back next time
      if (i == STAR_REGISTER || i == PLUS_REGISTER)
          continue;
      // Neither do we want the '~' register
      if (i == TILDE_REGISTER)
          continue;
      // When reading eeglinfo for merging and writing: Use the register from
      // eeglinfo if it's newer.
      if (y_read_regs
         && y_read_regs[i].y_array != NULL
         && (y_regs_p[i].y_array == NULL || y_read_regs[i].y_time_set > y_regs_p[i].y_time_set)
      )
          y_ptr = &y_read_regs[i];
      ei (y_regs_p[i].y_array == NULL)
         continue;
      else
         y_ptr = &y_regs_p[i];

      // Skip empty registers.
      num_lines = y_ptr->y_size;
      if (num_lines == 0
         || (num_lines == 1 && y_ptr->y_type == MCHAR
                  && *y_ptr->y_array[0].c == ZERO))
          continue;

      if (max_kbyte > 0) {
          // Skip register if there is more text than the maximum size.
          len = 0;
          for (j = 0; j < num_lines; j++)
         len += (long)y_ptr->y_array[j].len + 1L;
          if (len > (long)max_kbyte * 1024L)
         continue;
      }

      switch (y_ptr->y_type) {
      case MLINE:
         type = (CS)"LINE";
         break;
      case MCHAR:
         type = (CS)"CHAR";
         break;
      case MBLOCK:
         type = (CS)"BLOCK";
         break;
      default:
         showErrFmtMsg(_(e_unknown_register_type_nr), y_ptr->y_type);
         type = (CS)"LINE";
         break;
      }
      if (get_y_previous() == &y_regs_p[i])
         fprintf(fp, "\"");
      c = get_register_name(i);
      fprintf(fp, "\"%c", c);
      if (c == get_execreg_lastc())
         fprintf(fp, "@");
      fprintf(fp, "\t%s\t%d\n", type, (int)y_ptr->y_width);

      // If max_num_lines < 0, then we save ALL the lines in the register
      if (max_num_lines > 0 && num_lines > max_num_lines)
         num_lines = max_num_lines;
      for (j = 0; j < num_lines; j++) {
         putc('\t', fp);
         eeglinfo_writestring(fp, y_ptr->y_array[j].c);
      }

      {
         Unt flags = 0;

         // New style with a bar line. Format:
         // |{bartype},{flags},{name},{type},
         //      {linecount},{width},{timestamp},"line1","line2"
         // flags: REG_PREVIOUS - register is y_previous
         //         REG_EXEC - used for @@
         if (get_y_previous() == &y_regs_p[i])
            flags |= REG_PREVIOUS;
         if (c == get_execreg_lastc())
            flags |= REG_EXEC;
         fprintf(fp, "|%d,%d,%d,%d,%d,%d,%ld", BARTYPE_REGISTER, flags,
             i, y_ptr->y_type, num_lines, (int)y_ptr->y_width,
             (long)y_ptr->y_time_set);
         // 11 chars for type/flags/name/type, 3 * 20 for numbers
         int remaining = LSIZE - 71;
         for (j = 0; j < num_lines; j++) {
            putc(',', fp);
            --remaining;
            remaining = barline_writestring(fp, y_ptr->y_array[j].c, remaining);
         }
         putc('\n', fp);
      }
   }
}

// Functions relating to reading/writing marks from eeglinfo

private FileMarkExt *vi_namedfm = NULL;
private FileMarkExt *vi_jumplist = NULL;
private int vi_jumplist_len = 0;

private void
write_one_mark(FILE* fp_out, int c, Pos* pos) {
   if (pos->lnum != 0)
      fprintf(fp_out, "\t%c\t%ld\t%d\n", c, (long)pos->lnum, (int)pos->col);
}

private void
writeBookMarks(Book* book, FILE* fp_out) {
   home_replace(book->fullFileName, IObuff, IOSIZE, true);
   fprintf(fp_out, "\n> ");
   eeglinfo_writestring(fp_out, IObuff);

   // Write the last used timestamp as the lnum of the non-existing mark '*'.
   // Older Eegls will ignore it and/or copy it.
   Pos pos;
   pos.lnum = (LineNr)book->lastUsed;
   pos.col = 0;
   write_one_mark(fp_out, '*', &pos);

   write_one_mark(fp_out, '"', &book->lastCursor);
   write_one_mark(fp_out, '^', &book->lastInsert);
   write_one_mark(fp_out, '.', &book->lastChange);
   // changelist positions are stored oldest first
   for (Unt i = 0; i < book->changeListLen; ++i) {
      // skip duplicates
      if (i == 0 || !EQUAL_POS(book->changeList[i - 1], book->changeList[i]))
          write_one_mark(fp_out, '+', &book->changeList[i]);
   }
   for (Unt i = 0; i < NMARKS; i++)
      write_one_mark(fp_out, 'a' + i, &book->namedMarks[i]);
}

// Return true if marks for "book" should not be written.
private int
skip_for_eeglinfo(Book *book) {
    return bt_terminal(book) || removable(book->fullFileName);
}

//Write all the named marks for all books.
//When "buflist" is not NULL fill it with the books for which marks are to be written.
private void
write_eeglinfo_marks(FILE* fp_out, ArrayList* buflist) {
   int is_mark_set;
   int i;

   // Set lastCursor for all books that have a portal.
   Portal   *port;
   Tab   *t;
   FOR_ALL_TAB_PORTALS(t, port)
      set_last_cursor(port);

   FPUTS(_("\n# History of marks within files (newest to oldest):\n"), fp_out);
   Book   *book;
   FOR_ALL_BOOKS(book) {
      // Only write something if book has been loaded and at least one mark is set.
      if (book->haveReadEeglinfoMarks) {
         if (book->lastCursor.lnum != 0)
            is_mark_set = true;
         else {
            is_mark_set = false;
            for (i = 0; i < NMARKS; i++)
               if (book->namedMarks[i].lnum != 0) {
                  is_mark_set = true;
                  break;
               }
         }
         if (is_mark_set && book->fullFileName && book->fullFileName[0] != ZERO
               && !skip_for_eeglinfo(book))
          {
            if (!buflist)
                writeBookMarks(book, fp_out);
            ei (ga_grow(buflist, 1) == OK)
                ((Book **)buflist->c)[buflist->len++] = book;
          }
      }
   }
}

private void
write_one_filemark(FILE* fp, FileMarkExt* fm, int c1, int c2) {
   if (fm->fmark.mark.lnum == 0)   // not set
      return;

   CS name;
   if (fm->fmark.fnum != 0)      // there is a book
      name = bookGetNameByBookNr(fm->fmark.fnum, true, false);
   else
      name = fm->fname;      // use name from .eeglinfo
   if (name && *name != ZERO) {
      fprintf(fp, "%c%c  %ld  %ld  ", c1, c2, (long)fm->fmark.mark.lnum,
                         (long)fm->fmark.mark.col);
      eeglinfo_writestring(fp, name);

      // Barline: |{bartype},{name},{lnum},{col},{timestamp},{filename}
      // size up to filename: 8 + 3 * 20
      fprintf(fp, "|%d,%d,%ld,%ld,%ld,", BARTYPE_MARK, c2,
         (long)fm->fmark.mark.lnum, (long)fm->fmark.mark.col,
         (long)fm->time_set);
      barline_writestring(fp, name, LSIZE - 70);
      putc('\n', fp);
   }

   if (fm->fmark.fnum != 0)
      eeglFree(name);
}

private void
write_eeglinfo_filemarks(FILE* fp) {
   int i;
   CS name;
   Book* book;
   FileMarkExt* namedfm_p = get_namedfm();
   FileMarkExt* fm;
   int vi_idx;
   int idx;

   if (get_eeglinfo_parameter('f') == 0)
      return;

   FPUTS(_("\n# File marks:\n"), fp);

   // Write the filemarks 'A - 'Z
   for (i = 0; i < NMARKS; i++) {
      if (vi_namedfm != NULL && (vi_namedfm[i].time_set > namedfm_p[i].time_set))
         fm = &vi_namedfm[i];
      else
         fm = &namedfm_p[i];
      write_one_filemark(fp, fm, '\'', i + 'A');
   }

   // Find a mark that is the same file and position as the cursor.
   // That one, or else the last one is deleted.
   // Move '0 to '1, '1 to '2, etc. until the matching one or '9
   // Set the '0 mark to current cursor position.
   if (curBook->fullFileName != NULL && !skip_for_eeglinfo(curBook)) {
      name = bookGetNameByBookNr(curBook->fiNum, true, false);
      for (i = NMARKS; i < NMARKS + EXTRA_MARKS - 1; ++i)
          if (namedfm_p[i].fmark.mark.lnum == curPor->cursor.lnum
             && (namedfm_p[i].fname == NULL
                ? namedfm_p[i].fmark.fnum == curBook->fiNum
                : (name != NULL
                   && STRCMP(name, namedfm_p[i].fname) == 0)))
         break;
      eeglFree(name);

      eeglFree(namedfm_p[i].fname);
      for ( ; i > NMARKS; --i)
         namedfm_p[i] = namedfm_p[i - 1];
      namedfm_p[NMARKS].fmark.mark = curPor->cursor;
      namedfm_p[NMARKS].fmark.fnum = curBook->fiNum;
      namedfm_p[NMARKS].fname = NULL;
      namedfm_p[NMARKS].time_set = eeTime();
   }

   // Write the filemarks '0 - '9.  Newest (highest timestamp) first.
   vi_idx = NMARKS;
   idx = NMARKS;
   for (i = NMARKS; i < NMARKS + EXTRA_MARKS; i++) {
      FileMarkExt *vi_fm = vi_namedfm != NULL ? &vi_namedfm[vi_idx] : NULL;

      if (vi_fm
         && vi_fm->fmark.mark.lnum != 0
         && (vi_fm->time_set > namedfm_p[idx].time_set || namedfm_p[idx].fmark.mark.lnum == 0)
      ){
         fm = vi_fm;
         ++vi_idx;
      } else {
         fm = &namedfm_p[idx++];
         if (vi_fm
              && vi_fm->fmark.mark.lnum == fm->fmark.mark.lnum
              && vi_fm->time_set == fm->time_set
              && ((vi_fm->fmark.fnum != 0
                 && vi_fm->fmark.fnum == fm->fmark.fnum)
                  || (vi_fm->fname && fm->fname && STRCMP(vi_fm->fname, fm->fname) == 0))
         )
            ++vi_idx;  // skip duplicate
      }
      write_one_filemark(fp, fm, '\'', i - NMARKS + '0');
   }

   // Write the jumplist with -'
   FPUTS(_("\n# Jumplist (newest first):\n"), fp);
   setpcmark();   // add current cursor position
   cleanup_jumplist(curPor, false);
   vi_idx = 0;
   idx = curPor->jumpListLen - 1;
   for (i = 0; i < JUMPLISTSIZE; ++i) {
      fm = idx >= 0 ? &curPor->jumpList[idx] : NULL;
      FileMarkExt* vi_fm = (vi_jumplist != NULL && vi_idx < vi_jumplist_len)
                  ? &vi_jumplist[vi_idx] : NULL;
      if (fm == NULL && vi_fm == NULL)
         break;
      if (fm == NULL || (vi_fm != NULL && fm->time_set < vi_fm->time_set)) {
         fm = vi_fm;
         ++vi_idx;
      } else
         --idx;
      if (fm->fmark.fnum == 0
            || ((book = bookFindFileByBookNr(fm->fmark.fnum)) != NULL && !skip_for_eeglinfo(book)))
          write_one_filemark(fp, fm, '-', '\'');
   }
}

//Handle marks in the eeglinfo file:
//fp_out != NULL: copy marks, in time order with books in "booklist".
//fp_out == NULL && (flags & EIF_WANT_MARKS): read marks for curBook
//fp_out == NULL && (flags & EIF_ONLY_CURBOOK): bail out after curBook marks
//fp_out == NULL && (flags & EIF_GET_OLDFILES | EIF_FORCEIT): fill v:oldfiles
private void
copy_eeglinfo_marks(
   Vir* virp,
   FILE* fp_out,
   ArrayList* buflist,
   int eof,
   int flags
){
   CS line = virp->line;
   Book* book;
   int num_marked_files;
   int load_marks;
   int copy_marks_out;
   CS str;
   int i;
   Byte   *p;
   Pos   pos;
   List   *list = NULL;
   int      count = 0;
   int      buflist_used = 0;
   Book* buflist_buf = NULL;

   CS name_buf = alloc(LSIZE);
   *name_buf = ZERO;

   if (fp_out && buflist->len > 0) {
      // Sort the list of books on lastUsed.
      qsort(buflist->c, (Unt)buflist->len, sizeof(Book *), bookCompare);
      buflist_buf = ((Book **)buflist->c)[0];
   }

   if (fp_out == NULL && (flags & (EIF_GET_OLDFILES | EIF_FORCEIT))) {
      list = list_alloc();
      set_EeglVar_list(VV_OLDFILES, list);
   }

   num_marked_files = get_eeglinfo_parameter('\'');
   while (!eof && (count < num_marked_files || fp_out == NULL)) {
      if (line[0] != '>') {
         if (line[0] != '\n' && line[0] != '\r' && line[0] != '#'
            && eeglinfo_error(S"E576: ", _(e_nonr_missing_gt), line)
         )
            break;   // too many errors, return now
         eof = eeFgets(line, LSIZE, virp->vir_fd);
         continue;      // Skip this dud line
      }

      // Handle long line and translate escaped characters.
      // Find file name, set str to start. Ignore leading and trailing white space.
      str = skipwhite(line + 1);
      str = eeglinfo_readstring(virp, (int)(str - virp->line));
      if (str == NULL)
         continue;
      p = str + STRLEN(str);
      while (p != str && (*p == ZERO || isSpace(*p)))
         p--;
      if (*p)
         p++;
      *p = ZERO;

      if (list)
         list_append_string(list, str, -1);

      // If fp_out == NULL, load marks for current book.
      // If fp_out != NULL, copy marks for books not in booklist.
      load_marks = copy_marks_out = false;
      if (fp_out == NULL) {
         if ((flags & EIF_WANT_MARKS) && curBook->fullFileName != NULL) {
            if (*name_buf == ZERO)       // only need to do this once
               home_replace(curBook->fullFileName, name_buf, LSIZE, true);
            if (fnamecmp(str, name_buf) == 0)
               load_marks = true;
         }
      } else { // fp_out != NULL
         // This is slow if there are many books!!
         FOR_ALL_BOOKS(book) {
            if (book->fullFileName) {
               home_replace(book->fullFileName, name_buf, LSIZE, true);
               if (fnamecmp(str, name_buf) == 0)
                  break;
            }
         } 

         // Copy marks if the book has not been loaded.
         if (book == NULL || !book->haveReadEeglinfoMarks) {
            int   did_read_line = false;

            if (buflist_buf) {
               // Read the next line.  If it has the "*" mark compare the
               // time stamps.  Write entries from "buflist" that are newer.
               if (!eeglinfo_readline(virp) && line[0] == TAB) {
                  did_read_line = true;
                  if (line[1] == '*') {
                     long   ltime;
                     sscanf((char *)line + 2, "%ld ", OUT &ltime);
                     while ((Tyme)ltime < buflist_buf->lastUsed) {
                        writeBookMarks(buflist_buf, fp_out);
                        if (++count >= num_marked_files)
                           break;
                        if (++buflist_used == buflist->len) {
                           buflist_buf = NULL;
                           break;
                        }
                        buflist_buf = ((Book **)buflist->c)[buflist_used];
                     }
                  } else {
                     // No timestamp, must be written by an older Eegl.
                     // Assume all remaining books are older than ours.
                     while (count < num_marked_files && buflist_used < buflist->len) {
                        buflist_buf = ((Book **)buflist->c)[buflist_used++];
                        writeBookMarks(buflist_buf, fp_out);
                        ++count;
                     }
                     buflist_buf = NULL;
                  }

                  if (count >= num_marked_files) {
                      eeglFree(str);
                      break;
                  }
               }
            }

            fputs("\n> ", fp_out);
            eeglinfo_writestring(fp_out, str);
            if (did_read_line)
               FPUTS(line, fp_out);

            count++;
            copy_marks_out = true;
          }
      }
      eeglFree(str);

      pos.coladd = 0;
      while (!(eof = eeglinfo_readline(virp)) && line[0] == TAB) {
         if (load_marks) {
            if (line[1] != ZERO) {
               unsigned u;

               sscanf((char *)line + 2, "%ld %u", &pos.lnum, &u);
               pos.col = u;
               switch (line[1]) {
               case '"': curBook->lastCursor = pos; break;
               case '^': curBook->lastInsert = pos; break;
               case '.': curBook->lastChange = pos; break;
               case '+':
                    // changelist positions are stored oldest
                    // first
                    if (curBook->changeListLen == JUMPLISTSIZE)
                        // list is full, remove oldest entry
                        MEMMOVE(curBook->changeList,
                         curBook->changeList + 1,
                         sizeof(Pos) * (JUMPLISTSIZE - 1));
                    else
                        ++curBook->changeListLen;
                    curBook->changeList[curBook->changeListLen - 1] = pos;
                    break;

                    // Using the line number for the last-used timestamp.
               case '*': curBook->lastUsed = pos.lnum; break;

               default:  
                  if ((i = line[1] - 'a') >= 0 && i < NMARKS)
                     curBook->namedMarks[i] = pos;
               }
            }
         } ei (copy_marks_out)
            FPUTS(line, fp_out);
      }

      if (load_marks) {
         Portal   *wp;
         FOR_ALL_PORTALS(wp) {
            if (wp->book == curBook)
                wp->changeListInd = curBook->changeListLen;
         }
         if (flags & EIF_ONLY_CURBOOK)
            break;
      }
   }

   if (fp_out) {
      // Write any remaining entries from buflist.
      while (count < num_marked_files && buflist_used < buflist->len) {
          buflist_buf = ((Book **)buflist->c)[buflist_used++];
          writeBookMarks(buflist_buf, fp_out);
          ++count;
      }
   } 

   eeglFree(name_buf);
}

//Read marks for the current book from the eeglinfo file, when we support
//book marks and the book has a name.
void
check_marks_read(void) {
   if (!curBook->haveReadEeglinfoMarks && get_eeglinfo_parameter('\'') > 0 && curBook->fullFileName)
      read_eeglinfo(NULL, EIF_WANT_MARKS | EIF_ONLY_CURBOOK);

   // Always set haveReadEeglinfoMarks; needed when 'eeglinfo' is changed to include
   // the ' parameter after opening a book.
   curBook->haveReadEeglinfoMarks = true;
}

private int
read_eeglinfo_filemark(Vir *virp, int force) {
   FileMarkExt* namedfm_p = get_namedfm();
   FileMarkExt* fm;
   int i;

   // We only get here if line[0] == '\'' or '-'.
   // Illegal mark names are ignored (for future expansion).
   CS str = virp->line + 1;
   if (*str <= 127
       && ((*virp->line == '\'' && (EE_ISDIGIT(*str) || SAFE_isupper(*str)))
        || (*virp->line == '-' && *str == '\''))
   ){
      if (*str == '\'') {
         // If the jumplist isn't full insert fmark as oldest entry
         if (curPor->jumpListLen == JUMPLISTSIZE)
            fm = NULL;
         else {
            for (i = curPor->jumpListLen; i > 0; --i)
                curPor->jumpList[i] = curPor->jumpList[i - 1];
            ++curPor->jumpListInd;
            ++curPor->jumpListLen;
            fm = &curPor->jumpList[0];
            fm->fmark.mark.lnum = 0;
            fm->fname = NULL;
         }
      } ei (EE_ISDIGIT(*str))
         fm = &namedfm_p[*str - '0' + NMARKS];
      else
         fm = &namedfm_p[*str - 'A'];
      if (fm && (fm->fmark.mark.lnum == 0 || force)) {
         str = skipwhite(str + 1);
         fm->fmark.mark.lnum = parseLong(&str);
         str = skipwhite(str);
         fm->fmark.mark.col = parseLong(&str);
         fm->fmark.mark.coladd = 0;
         fm->fmark.fnum = 0;
         str = skipwhite(str);
         eeglFree(fm->fname);
         fm->fname = eeglinfo_readstring(virp, (int)(str - virp->line));
         fm->time_set = 0;
      }
   }
   return eeFgets(virp->line, LSIZE, virp->vir_fd);
}

// Prepare for reading eeglinfo marks when writing eeglinfo later.
private void
prepare_eeglinfo_marks(void) {
   vi_namedfm = ALLOC_CLEAR_MULT(FileMarkExt, NMARKS + EXTRA_MARKS);
   vi_jumplist = ALLOC_CLEAR_MULT(FileMarkExt, JUMPLISTSIZE);
   vi_jumplist_len = 0;
}

private void
finish_eeglinfo_marks(void) {
   if (vi_namedfm) {
      for (int i = 0; i < NMARKS + EXTRA_MARKS; ++i)
         eeglFree(vi_namedfm[i].fname);
      EE_CLEAR(vi_namedfm);
   }
   if (vi_jumplist != NULL) {
      for (int i = 0; i < vi_jumplist_len; ++i)
         eeglFree(vi_jumplist[i].fname);
      EE_CLEAR(vi_jumplist);
   }
}

// Accept a new style mark line from the eeglinfo, store it when it's new.
private void
handle_eeglinfo_mark(ArrayList *values, int force) {
   BVal* vp = (BVal *)values->c;

   // Check the format:
   // |{bartype},{name},{lnum},{col},{timestamp},{filename}
   if (values->len < 5
         || vp[0].btag != BVAL_NR
         || vp[1].btag != BVAL_NR
         || vp[2].btag != BVAL_NR
         || vp[3].btag != BVAL_NR
         || vp[4].btag != BVAL_STRING)
      return;

   int name = vp[0].bv_nr;
   if (name != '\'' && !EE_ISDIGIT(name) && !ASCII_ISUPPER(name))
      return;
   LineNr lnum = vp[1].bv_nr;
   ColNr col = vp[2].bv_nr;
   if (lnum <= 0 || col < 0)
      return;
   Tyme timestamp = (time_t)vp[3].bv_nr;

   FileMarkExt* fm = NULL;
   if (name == '\'') {
      if (vi_jumplist) {
         if (vi_jumplist_len < JUMPLISTSIZE)
            fm = &vi_jumplist[vi_jumplist_len++];
      } else {
         int idx;
         int i;

         // If we have a timestamp insert it in the right place.
         if (timestamp != 0) {
            for (idx = curPor->jumpListLen - 1; idx >= 0; --idx)
               if (curPor->jumpList[idx].time_set < timestamp) {
                  ++idx;
                  break;
               }
            // idx cannot be zero now
            if (idx < 0 && curPor->jumpListLen < JUMPLISTSIZE)
               // insert as the oldest entry
               idx = 0;
         } ei (curPor->jumpListLen < JUMPLISTSIZE)
            // insert as oldest entry
            idx = 0;
         else
            idx = -1;

         if (idx >= 0) {
            if (curPor->jumpListLen == JUMPLISTSIZE) {
               // Drop the oldest entry.
               --idx;
               eeglFree(curPor->jumpList[0].fname);
               for (i = 0; i < idx; ++i)
                  curPor->jumpList[i] = curPor->jumpList[i + 1];
            } else {
               // Move newer entries forward.
               for (i = curPor->jumpListLen; i > idx; --i)
                  curPor->jumpList[i] = curPor->jumpList[i - 1];
               ++curPor->jumpListInd;
               ++curPor->jumpListLen;
            }
            fm = &curPor->jumpList[idx];
            fm->fmark.mark.lnum = 0;
            fm->fname = NULL;
            fm->time_set = 0;
         }
      }
   } else {
      int      idx;
      FileMarkExt* namedfm_p = get_namedfm();

      if (EE_ISDIGIT(name)) {
         if (vi_namedfm)
            idx = name - '0' + NMARKS;
         else {
            int i;

            // Do not use the name from the eeglinfo file, insert in time
            // order.
            for (idx = NMARKS; idx < NMARKS + EXTRA_MARKS; ++idx)
                if (namedfm_p[idx].time_set < timestamp)
               break;
            if (idx == NMARKS + EXTRA_MARKS)
                // All existing entries are newer.
                return;
            i = NMARKS + EXTRA_MARKS - 1;

            eeglFree(namedfm_p[i].fname);
            for ( ; i > idx; --i)
                namedfm_p[i] = namedfm_p[i - 1];
            namedfm_p[idx].fname = NULL;
         }
      } else
         idx = name - 'A';
      if (vi_namedfm != NULL)
         fm = &vi_namedfm[idx];
      else
         fm = &namedfm_p[idx];
   }

   if (fm) {
      if (vi_namedfm != NULL || fm->fmark.mark.lnum == 0 || fm->time_set < timestamp || force) {
         fm->fmark.mark.lnum = lnum;
         fm->fmark.mark.col = col;
         fm->fmark.mark.coladd = 0;
         fm->fmark.fnum = 0;
         eeglFree(fm->fname);
         if (vp[4].bv_allocated) {
            fm->fname = vp[4].bv_string;
            vp[4].bv_string = NULL;
         } else
            fm->fname = copyStr(vp[4].bv_string);
         fm->time_set = timestamp;
      }
   }
}

private int
read_eeglinfo_barline(Vir* virp, Boole force, int writing) {
   CS p = virp->line + 1;
   int bartype;
   ArrayList values;
   BVal* vp;
   int i;
   int read_next = true;

   // The format is: |{bartype},{value},...
   // For a very long string:
   //     |{bartype},>{length of "{text}{text2}"}
   //     |<{text1}
   //     |<{text2},{value}
   // For a long line not using a string
   //     |{bartype},{lots of values},>
   //     |<{value},{value}
   if (*p == '<') {
      // Continuation line of an unrecognized item.
      if (writing)
         ga_copy_string(&virp->vir_barlines, virp->line);
   } else {
      ga_init2(&values, sizeof(BVal), 20);
      bartype = parseLong(&p);
      switch (bartype) {
      case BARTYPE_VERSION:
         read_next = barline_parse(virp, p, &values);
         vp = (BVal *)values.c;
         if (values.len > 0 && vp->btag == BVAL_NR)
            virp->vir_version = vp->bv_nr;
         break;

      case BARTYPE_HISTORY:
         read_next = barline_parse(virp, p, &values);
         handle_eeglinfo_history(&values, writing);
         break;

      case BARTYPE_REGISTER:
         read_next = barline_parse(virp, p, &values);
         handle_eeglinfo_register(&values, force);
         break;

      case BARTYPE_MARK:
         read_next = barline_parse(virp, p, &values);
         handle_eeglinfo_mark(&values, force);
         break;

      default:
         // copy unrecognized line (for future use)
         if (writing)
            ga_copy_string(&virp->vir_barlines, virp->line);
      }
      for (i = 0; i < values.len; ++i) {
         vp = (BVal *)values.c + i;
         if (vp->btag == BVAL_STRING && vp->bv_allocated)
            eeglFree(vp->bv_string);
         eeglFree(vp->bv_tofree);
      }
      ga_clear(&values);
   }

   if (read_next)
      return eeglinfo_readline(virp);
   return false;
}

//read_eeglinfo_up_to_marks() -- Only called from do_eeglinfo().  Reads in the
//first part of the eeglinfo file which contains everything but the marks that
//are local to a file.  Return true when end-of-file is reached. -- webb
private int
read_eeglinfo_up_to_marks(Vir* virp, Boole forceit, int writing) {

   prepare_eeglinfo_history(forceit ? 9999 : 0, writing);

   int eof = eeglinfo_readline(virp);
   while (!eof && virp->line[0] != '>') {
      switch (virp->line[0]) {
      // Characters reserved for future expansion, ignored now
      case '+': // "+40 /path/dir file", for running vim without args
      case '^': // to be defined
      case '<': // long line - ignored
      // A comment or empty line.
      case ZERO:
      case '\r':
      case '\n':
      case '#':
         eof = eeglinfo_readline(virp);
         break;
      case '|':
         eof = read_eeglinfo_barline(virp, forceit, writing);
         break;
      case '!': // global variable
         eof = read_eeglinfo_varlist(virp, writing);
         break;
      case '%': // entry for book list
         eof = readEeglinfoBookList(virp, writing);
         break;
      case '"':
         // When registers are in bar lines skip the old style register lines.
         if (virp->vir_version < EEGLINFO_VERSION_WITH_REGISTERS)
            eof = read_eeglinfo_register(virp, forceit);
         else
            do {
               eof = eeglinfo_readline(virp);
            } while (!eof && (virp->line[0] == TAB || virp->line[0] == '<'));
         break;
      case '/':       // Search string
      case '&':       // Substitute search string
      case '~':       // Last search string, followed by '/' or '&'
         eof = read_eeglinfo_search_pattern(virp, forceit);
         break;
      case '$':
         eof = read_eeglinfo_sub_string(virp, forceit);
         break;
      case ':':
      case '?':
      case '=':
      case '@':
         // When history is in bar lines skip the old style history lines.
         if (virp->vir_version < EEGLINFO_VERSION_WITH_HISTORY)
            eof = read_eeglinfo_history(virp, writing);
         else
            eof = eeglinfo_readline(virp);
         break;
      case '-':
      case '\'':
         // When file marks are in bar lines skip the old style lines.
         if (virp->vir_version < EEGLINFO_VERSION_WITH_MARKS)
            eof = read_eeglinfo_filemark(virp, forceit);
         else
            eof = eeglinfo_readline(virp);
         break;
      default:
         if (eeglinfo_error(S"E575: ", _(e_illegal_starting_char), virp->line))
            eof = true;
         else
            eof = eeglinfo_readline(virp);
         break;
      }
   }

   // Finish reading history items.
   if (!writing)
      finish_eeglinfo_history(virp);

   // Change file names to book numbers for fmarks.
   Book* book;
   FOR_ALL_BOOKS(book) {
      fmarks_check_names(book);
   } 

   return eof;
}

// do_eeglinfo() -- Should only be called from read_eeglinfo() & write_eeglinfo().
private void
do_eeglinfo(FILE* fp_in, FILE* fp_out, Unt flags) {
   int eof = false;
   int merge = false;
   int do_copy_marks = false;
   ArrayList   buflist;

   Vir  vir;
   vir.line = alloc(LSIZE);
   vir.vir_fd = fp_in;
   ga_init2(&vir.vir_barlines, sizeof(CS), 100);
   vir.vir_version = -1;

   if (fp_in) {
      if (flags & EIF_WANT_INFO) {
         if (fp_out) {
            //Registers and marks are read and kept separate from what this Eegl is using. 
            //They are merged when writing.
            prepare_eeglinfo_registers();
            prepare_eeglinfo_marks();
         }

         eof = read_eeglinfo_up_to_marks(&vir, flags & EIF_FORCEIT, fp_out != NULL);
         merge = true;
      } ei (flags != 0)
         // Skip info, find start of marks
         while (!(eof = eeglinfo_readline(&vir)) && vir.line[0] != '>')
            {}

      do_copy_marks = (flags & (EIF_WANT_MARKS | EIF_ONLY_CURBOOK | EIF_GET_OLDFILES | EIF_FORCEIT));
   }

   if (fp_out != NULL) {
      // Write the info:
      fprintf(fp_out, (char*)_("# This eeglinfo file was generated by Eegl %s.\n"),
                          EEGL_VERSION_MEDIUM);
      FPUTS(_("# You may edit it if you're careful!\n\n"), fp_out);
      write_eeglinfo_version(fp_out);
      write_eeglinfo_search_pattern(fp_out);
      write_eeglinfo_sub_string(fp_out);
      write_eeglinfo_history(fp_out, merge);
      write_eeglinfo_registers(fp_out);
      finish_eeglinfo_registers();
      write_eeglinfo_varlist(fp_out);
      write_eeglinfo_filemarks(fp_out);
      finish_eeglinfo_marks();
      writeEeglInfoBookList(fp_out);
      write_eeglinfo_barlines(&vir, fp_out);

      if (do_copy_marks)
         ga_init2(&buflist, sizeof(Book *), 50);
      write_eeglinfo_marks(fp_out, do_copy_marks ? &buflist : NULL);
   }

   if (do_copy_marks) {
      copy_eeglinfo_marks(&vir, fp_out, &buflist, eof, flags);
      if (fp_out)
         ga_clear(&buflist);
   }

   eeglFree(vir.line);
   ga_clear_strings(&vir.vir_barlines);
}

//read_eeglinfo() -- Read the eeglinfo file.  Registers etc. which are already
//set are not over-written unless "flags" includes EIF_FORCEIT. -- webb
int
read_eeglinfo(
   CS file,       // file name or NULL to use default name
   Unt flags       // EIF_WANT_INFO et al.
){
   FileStat   st;      // stat() of existing eeglinfo file

   if (no_eeglinfo())
      return FAIL;

   CS fname = eeglinfo_filename(file);   // get file name in allocated book
   if (!fname)
      return FAIL;
   FILE* fp = FOPEN(fname, READBIN);

   if (p_verbose > 0) {
      verbose_enter();
      smsg(_("Reading eeglinfo file \"%s\"%s%s%s%s"),
         fname,
         (flags & EIF_WANT_INFO) ? _(" info") : S"",
         (flags & EIF_WANT_MARKS) ? _(" marks") : S"",
         (flags & EIF_GET_OLDFILES) ? _(" oldfiles") : S"",
         fp == NULL ? _(" FAILED") : S"");
      verbose_leave();
   }

   eeglFree(fname);
   if (fp == NULL)
      return FAIL;
   if (fstat(fileno(fp), &st) < 0 || S_ISDIR(st.st_mode)) {
      fclose(fp);
      return FAIL;
   }

   eeglinfo_errcnt = 0;
   do_eeglinfo(fp, NULL, flags);

   fclose(fp);
   return OK;
}

//Write the eeglinfo file. The old one is read in first so that effectively a
//merge of current info and old info is done. This allows multiple vims to
//run simultaneously, without losing any marks etc. If "forceit" is true, then the old file is not
//read in, and only internal info is written to the file.
void
write_eeglinfo(CS file, Boole forceit) {
   FILE* fp_out = NULL;   // output eeglinfo file
   CS tempname = NULL;   // name of temp eeglinfo file
   FileStat   st_new;      // stat() of potential new file
   FileStat   st_old;      // stat() of existing eeglinfo file
   mode_t   umask_save;

   if (no_eeglinfo())
      return;

   CS fname = eeglinfo_filename(file);   // may set to default if NULL
   if (!fname)
      return;

   FILE* fp_in = fopen((char *)fname, READBIN); // input eeglinfo file, if any
   if (!fp_in) {
      // if it does exist, but we can't read it, don't try writing
      if (stat((char *)fname, &st_new) == 0)
         goto end;

      // Create the new .eeglinfo non-accessible for others, because it may
      // contain text from non-accessible documents. It is up to the user to
      // widen access (e.g. to a group). This may also fail if there is a
      // race condition, then just give up.
      int fd = open((char *)fname, O_CREAT|O_EXTRA|O_EXCL|O_WRONLY|O_NOFOLLOW, 0600);
      if (fd < 0)
         goto end;
      fp_out = fdopen(fd, WRITEBIN);
   } else {
      // There is an existing eeglinfo file.  Create a temporary file to
      // write the new eeglinfo into, in the same directory as the
      // existing eeglinfo file, which will be renamed once all writing is successful.
      if (fstat(fileno(fp_in), &st_old) < 0
         || S_ISDIR(st_old.st_mode)
         //We check the owner of the file. It's not very nice to overwrite a user's eeglinfo file 
         //after a "su root", with a eeglinfo file that the user can't read.
         || (getuid() != ROOT_UID
             && !(st_old.st_uid == getuid()
                ? (st_old.st_mode & 0200)
                : (st_old.st_gid == getgid()
                   ? (st_old.st_mode & 0020)
                   : (st_old.st_mode & 0002))))
      ) {
          int   tt = msg_didany;

          // avoid a wait_return() for this message, it's annoying
          showErrFmtMsg(_(e_eeglinfo_file_is_not_writable_str), fname);
          msg_didany = tt;
          fclose(fp_in);
          goto end;
      }

      // Make tempname, find one that does not exist yet. Beware of a race condition: If someone 
      // logs out and all Eegl instances exit at the same time a temp file might be created between
      // stat() and open(). Use open() with O_EXCL to avoid that.
      for (;;) {
         int next_char = 'z';

         tempname = fiAppendFileExtension(fname, S".tmp", false);
         if (!tempname)      // out of memory
            break;

         // Try a series of names. Change one character, just before the extension. 
         CS wp = tempname + STRLEN(tempname) - 5;
         if (wp < fiGetShortFiName(tempname))       // empty file name?
            wp = fiGetShortFiName(tempname);
         for (;;) {
            // Check if tempfile already exists.  Never overwrite an existing file!
            if (stat((char *)tempname, &st_new) == 0) {
               //Check if tempfile is same as original file. May happen when fiAppendFileExtension() gave the 
               //same file back.  E.g.  silly link, or file name-length reached. 
               if (st_new.st_dev == st_old.st_dev && st_new.st_ino == st_old.st_ino) {
                  EE_CLEAR(tempname);
                  break;
               }
            } else {
               //Try creating the file exclusively. This may fail if another Eegl tries to do it 
               //at the same time.

               //Use open() to be able to use O_NOFOLLOW and set file protection:
               //Unix: same as original file, but strip s-bit. Reset umask to avoid it getting in
               //the way. Others: r&w for user only.
               umask_save = umask(0);
               int fd = open((char *)tempname, O_CREAT|O_EXTRA|O_EXCL|O_WRONLY|O_NOFOLLOW,
                    (int)((st_old.st_mode & 0777) | 0600));
               (void)umask(umask_save);
               if (fd < 0) {
                  fp_out = NULL;
                  // Avoid trying lots of names while the problem is lack
                  // of permission, only retry if the file already exists.
                  if (errno != EEXIST)
                     break;
               } else
                  fp_out = fdopen(fd, WRITEBIN);
               if (fp_out)
                  break;
            }

            // Assume file exists, try again with another name.
            if (next_char == 'a' - 1) {
               // They all exist?  Must be something wrong! Don't write the eeglinfo file then.
               showErrFmtMsg(_(e_too_many_eeglinfo_temp_files_like_str), tempname);
               break;
            }
            *wp = next_char;
            --next_char;
         }

         if (tempname)
            break;
      }

      if (tempname && fp_out) {
         FileStat   tmp_st;

         //Make sure the original owner can read/write the tempfile and
         //otherwise preserve permissions, making sure the group matches.
         if (stat((char *)tempname, &tmp_st) >= 0) {
            if (st_old.st_uid != tmp_st.st_uid)
               //Changing the owner might fail, in which case the
               //file will now be owned by the current user, oh well.
               (void)fchown(fileno(fp_out), st_old.st_uid, -1);
            if (st_old.st_gid != tmp_st.st_gid && fchown(fileno(fp_out), -1, st_old.st_gid) == -1)
               //can't set the group to what it should be, remove group permissions
               (void)mch_setperm(tempname, 0600);
         } else
            //can't stat the file, set conservative permissions
            (void)mch_setperm(tempname, 0600);
      }
   }

   // Check if the new eeglinfo file can be written to.
   if (!fp_out) {
      showErrFmtMsg(_(e_cant_write_eeglinfo_file_str),
                (fp_in == NULL || tempname == NULL) ? fname : tempname);
      if (fp_in)
         fclose(fp_in);
      goto end;
   }

   if (p_verbose > 0) {
      verbose_enter();
      smsg(_("Writing eeglinfo file \"%s\""), fname);
      verbose_leave();
   }

   eeglinfo_errcnt = 0;
   do_eeglinfo(fp_in, fp_out, forceit ? 0 : (EIF_WANT_INFO | EIF_WANT_MARKS));

   if (fclose(fp_out) == EOF)
      ++eeglinfo_errcnt;

   if (fp_in) {
      fclose(fp_in);

      // In case of an error keep the original eeglinfo file.  Otherwise
      // rename the newly written file.  Give an error if that fails.
      if (eeglinfo_errcnt == 0) {
         if (eeRename(tempname, fname) == -1) {
            ++eeglinfo_errcnt;
            showErrFmtMsg(_(e_cant_rename_eeglinfo_file_to_str), fname);
         }
      }
      if (eeglinfo_errcnt > 0)
          mch_remove(tempname);
   }

end:
   eeglFree(fname);
   eeglFree(tempname);
}

//}}}
