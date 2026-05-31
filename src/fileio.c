//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## fileio.c: read from and write to a file

#include "eegl.h"
#include <sys/stat.h> // for stat, fstat etc
ssize_t listxattr(const char*, char*, size_t); //from sys/xattr.h
ssize_t getxattr(const char*, const char*, void*, size_t);
int setxattr(const char*, const char*, const void*, size_t, int);

#define SHELL_SPECIAL (CS)"\t \"&'$;<>()\\|"

//{{{forward declarations

private int mch_expand_wildcards(int num_pat, Arr(CS) pat, Unt flags, OUT ExpandMatch*);
private int eeCopyfile(CS from, CS to);
private int readdir_core(ArrayList* gap, int withattr UNUSED, int sort);
private void uniquefy_paths(OUT ExpandMatch* matches, CS pattern, CS path_option);

private CS
find_directory_in_path(
   Text fName, Unt options, CS rel_fname, 
   OUT Byte** file_to_find, OUT FileSearchCtx** searchCtx
);
private CS findFileInPathImpl(
   Text fName, Unt options, Boole first, NULLABLE CS path_option, Unt find_what, CS rel_fname,
   CS suffixes, OUT Byte** file_to_find, OUT FileSearchCtx** search_ctx_arg
);
private int expand_in_path(OUT ExpandMatch* matches, CS pattern, Unt flags);
private Boole recursivelyDeleteDir(CS name);
private CS skipInitialSlashes(CS path);

//}}}
//{{{file paths: dealing with file names and paths.

// Flags for the readdirex function, how to sort the result
#define READDIR_SORT_NONE    0  // do not sort
#define READDIR_SORT_BYTE    1  // sort by byte order (strcmp), default
#define READDIR_SORT_IC      2  // sort ignoring case (strcasecmp)
#define READDIR_SORT_COLLATE 3  // sort according to collation (strcoll)


//Adjust a filename, according to a string of modifiers.
//*fnamep must be ZERO terminated when called.  When returning, the length is
//determined by *fnamelen.
//Return VALID_ flags or -1 for failure.
//When there is an error, *fnamep is set to NULL.
int
modify_fname(
   CS src,      // string with modifiers
   int tilde_file,   // "~" is a file name, not $HOME
   Unt* usedlen,   // characters after src that are used
   OUT CS* fnamep,   // file name so far
   OUT CS* bufp,      // buffer for allocated file name or NULL
   Unt* fnamelen   // length of fnamep
){
   int      valid = 0;
   CS s;
   CS p;
   Byte   dirname[MAXPATHL];
   int      c;
   int      has_fullname = 0;
   int      has_homerelative = 0;

repeat:
   // ":p" - full path/file_name
   if (src[*usedlen] == ':' && src[*usedlen + 1] == 'p') {
      has_fullname = 1;

      valid |= VALID_PATH;
      *usedlen += 2;

      // Expand "~/path" for all systems and "~user/path" for Unix
      if ((*fnamep)[0] == '~' && !(tilde_file && (*fnamep)[1] == ZERO)) {
         *fnamep = doExpandEnvInMultiplePaths(*fnamep);
         eeglFree(*bufp);   // free any allocated file name
         *bufp = *fnamep;
         if (*fnamep == NULL)
            return -1;
      }

      // When "/." or "/.." is used: force expansion to get rid of it.
      for (p = *fnamep; *p != ZERO; MB_PTR_ADV(p)) {
         if (*p == '/'
                && p[1] == '.'
                && (p[2] == ZERO
                     || p[2] == '/'
                     || (p[2] == '.' && (p[3] == ZERO || p[3] == '/'))
                   )
         )
            break;
      }

      // fiExpandAndCopy() is slow, don't use it when not needed.
      if (*p != ZERO || !eeIsAbsName(*fnamep)) {
         *fnamep = fiExpandAndCopy(*fnamep, *p != ZERO);
         eeglFree(*bufp);   // free any allocated file name
         *bufp = *fnamep;
         if (*fnamep == NULL)
            return -1;
      }

      // Append a path separator to a directory.
      if (mch_isdir(*fnamep)) {
         // Make room for one or two extra characters.
         *fnamep = copySubstr(*fnamep, STRLEN(*fnamep) + 2);
         eeglFree(*bufp);   // free any allocated file name
         *bufp = *fnamep;
         if (*fnamep == null)
            return -1;
         add_pathsep(*fnamep);
      }
   }

   // ":." - path relative to the current directory
   // ":~" - path relative to the home directory
   while (src[*usedlen] == ':'
        && ((c = src[*usedlen + 1]) == '.' || c == '~')
   ){
      *usedlen += 2;
      if (c == '8') {
         continue;
      }
      CS pbuf = NULL;
      // Need full path first (use doExpandEnv() to remove a "~/")
      if (!has_fullname && !has_homerelative) {
         if (**fnamep == '~')
            p = pbuf = doExpandEnvInMultiplePaths(*fnamep);
         else
            p = pbuf = fiExpandAndCopy(*fnamep, FALSE);
      } else
         p = *fnamep;

      has_fullname = 0;

      if (p) {
         if (c == '.') {
            Unt   namelen;

            mch_dirname(dirname, MAXPATHL);
            if (has_homerelative) {
               s = copyStr(dirname);
               home_replace(NULL, s, dirname, MAXPATHL, TRUE);
               eeglFree(s);
            }
            namelen = STRLEN(dirname);

            // Do not call shorten_fname() here since it removes the prefix
            // even though the path does not have a prefix.
            if (fnamencmp(p, dirname, namelen) == 0) {
               p += namelen;
               if (*p == '/') {
                  while (*p == '/')
                     ++p;
                  *fnamep = p;
                  if (pbuf) {
                      // free any allocated file name
                      eeglFree(*bufp);
                      *bufp = pbuf;
                      pbuf = NULL;
                  }
               }
            }
         } else {
            home_replace(NULL, p, dirname, MAXPATHL, TRUE);
            // Only replace it when it starts with '~'
            if (*dirname == '~') {
               s = copyStr(dirname);
               *fnamep = s;
               eeglFree(*bufp);
               *bufp = s;
               has_homerelative = TRUE;
            }
          }
          eeglFree(pbuf);
      }
   }

   CS tail = fiGetShortFiName(*fnamep);
   *fnamelen = STRLEN(*fnamep);

   // ":h" - head, remove "/file_name", can be repeated
   // Don't remove the first "/" or "c:\"
   while (src[*usedlen] == ':' && src[*usedlen + 1] == 'h') {
      valid |= VALID_HEAD;
      *usedlen += 2;
      s = skipInitialSlashes(*fnamep);
      while (tail > s && after_pathsep(s, tail))
          MB_PTR_BACK(*fnamep, tail);
      *fnamelen = tail - *fnamep;
      if (*fnamelen == 0) {
         // Result is empty.  Turn it into "." to make ":cd %:h" work.
         p = copyStr((CS)".");
         eeglFree(*bufp);
         *bufp = *fnamep = tail = p;
         *fnamelen = 1;
      } else {
         while (tail > s && !after_pathsep(s, tail))
            MB_PTR_BACK(*fnamep, tail);
      }
   }

   // ":t" - tail, just the basename
   if (src[*usedlen] == ':' && src[*usedlen + 1] == 't') {
      *usedlen += 2;
      *fnamelen -= tail - *fnamep;
      *fnamep = tail;
   }

   // ":e" - extension, can be repeated
   // ":r" - root, without extension, can be repeated
   while (src[*usedlen] == ':'
       && (src[*usedlen + 1] == 'e' || src[*usedlen + 1] == 'r')
   ){
      // find a '.' in the tail:
      // - for second :e: before the current fname
      // - otherwise: The last '.'
      if (src[*usedlen + 1] == 'e' && *fnamep > tail)
          s = *fnamep - 2;
      else
          s = *fnamep + *fnamelen - 1;
      for ( ; s > tail; --s) {
         if (s[0] == '.')
            break;
      } 
      if (src[*usedlen + 1] == 'e') {     // :e
         if (s > tail) {
            *fnamelen += (*fnamep - (s + 1));
            *fnamep = s + 1;
         } ei (*fnamep <= tail)
            *fnamelen = 0;
      } else {           // :r
         CS limit = *fnamep;
         if (limit < tail)
            limit = tail;
         if (s > limit)   // remove one extension
            *fnamelen = s - *fnamep;
      }
      *usedlen += 2;
   }

   // ":s?pat?foo?" - substitute
   // ":gs?pat?foo?" - global substitute
   if (src[*usedlen] == ':'
       && (src[*usedlen + 1] == 's'
      || (src[*usedlen + 1] == 'g' && src[*usedlen + 2] == 's'))
   ) {
      CS str;
      CS pat;
      CS sub;
      int sep;
      int didit = FALSE;

      CS flags = S"";
      s = src + *usedlen + 2;
      if (src[*usedlen + 1] == 'g') {
         flags = (CS)"g";
         ++s;
      }

      sep = *s++;
      if (sep) {
         // find end of pattern
         p = firstOccurrence(s, sep);
         if (p) {
            pat = copySubstr(s, p - s);
            if (pat) {
                s = p + 1;
                // find end of substitution
                p = firstOccurrence(s, sep);
                if (p != NULL) {
               sub = copySubstr(s, p - s);
               str = copySubstr(*fnamep, *fnamelen);
               if (sub != NULL && str != NULL) {
                   Unt slen;

                   *usedlen = p + 1 - src;
                   s = do_string_sub(str, *fnamelen, pat, sub, NULL, flags, &slen);
                   if (s != NULL) {
                  *fnamep = s;
                  *fnamelen = slen;
                  eeglFree(*bufp);
                  *bufp = s;
                  didit = TRUE;
                   }
               }
               eeglFree(sub);
               eeglFree(str);
                }
                eeglFree(pat);
            }
          }
          // after using ":s", repeat all the modifiers
          if (didit)
         goto repeat;
      }
   }

   if (src[*usedlen] == ':' && src[*usedlen + 1] == 'S') {
      // copyStr_shellescape() needs a ZERO terminated string.
      c = (*fnamep)[*fnamelen];
      if (c != ZERO)
         (*fnamep)[*fnamelen] = ZERO;
      p = copyStr_shellescape(*fnamep, FALSE, FALSE);
      if (c != ZERO)
         (*fnamep)[*fnamelen] = c;
      eeglFree(*bufp);
      *bufp = *fnamep = p;
      *fnamelen = STRLEN(p);
      *usedlen += 2;
   }

   return valid;
}

//Shorten the path of a file from "~/foo/../.bar/fname" to "~/f/../.b/fname"
//"trim_len" specifies how many characters to keep for each directory.
//Must be 1 or more. It's done in-place.
private void
shorten_dir_len(CS str, int trim_len) {
   int skip = FALSE;
   int dirchunk_len = 0;

   CS tail = fiGetShortFiName(str);
   CS d = str;
   for (CS s = str; ; ++s) {
      if (s >= tail) {        // copy the whole tail
         *d++ = *s;
         if (*s == ZERO)
            break;
      } ei (*s == '/') {     // copy '/' and next char
         *d++ = *s;
         skip = FALSE;
         dirchunk_len = 0;
      } ei (!skip) {
         *d++ = *s;         // copy next char
         if (*s != '~' && *s != '.') { // and leading "~" and "."
            ++dirchunk_len; // only count word chars for the size

            // keep copying chars until we have our preferred length (or
            // until the above if/else branches move us along)
            if (dirchunk_len >= trim_len)
                skip = TRUE;
         }
         int l = utfCharLen(s);

         while (--l > 0)
            *d++ = *++s;
      }
   }
}

//To get the "real" home directory:
//- get value of $HOME
// - go to that directory
// - do mch_dirname() to get the real name of that directory.
// This also works with mounts and links.
void
init_homedir(void) {
   // In case we are called a second time (when 'encoding' changes).
   EE_CLEAR(homedir);

   CS var = mch_getenv("HOME");

   if (var) {
      //Change to the directory and get the actual path. This resolves links. Don't do it when 
      //we can't return.
      if (mch_dirname(nameBuffG, MAXPATHL) == OK && mch_chdir((char *)nameBuffG) == 0) {
         if (!mch_chdir((char *)var) && mch_dirname(IObuff, IOSIZE) == OK)
            var = IObuff;
         if (mch_chdir((char *)nameBuffG) != 0)
            emsg(_(e_cannot_go_back_to_previous_directory));
      }
      homedir = copyStr(var);
   }
}

//Shorten the path of a file from "~/foo/../.bar/fname" to "~/f/../.b/fname" It's done in-place.
void
shorten_dir(CS str){
   shorten_dir_len(str, 1);
}

//Return TRUE if "fname" is a readable file.
int
file_is_readable(CS fname){
   int      fd;

#ifndef O_NONBLOCK
# define O_NONBLOCK 0
#endif
   if (*fname && !mch_isdir(fname) && (fd = open((char *)fname, O_RDONLY | O_NONBLOCK, 0)) >= 0) {
      close(fd);
      return TRUE;
   }
   return FALSE;
}

//"chdir(dir)" function
void
f_chdir(Var* argvars, Var* returnVar) {
   CdScopeKind scope = CDSCOPE_GLOBAL;

   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   if (argvars[0].tag != VAR_STRING) {
      return;
   }

   // Return the current directory
   Byte cwd[MAXPATHL];
   if (mch_dirname(cwd, MAXPATHL) != FAIL) {
      returnVar->string = copyStr(cwd);
   }

   if (argvars[1].tag != VAR_UNKNOWN) {
      CS s = tv_get_string(&argvars[1]);
      if (STRCMP(s, "global") == 0)
         scope = CDSCOPE_GLOBAL;
      ei (STRCMP(s, "tabpage") == 0)
         scope = CDSCOPE_TABPAGE;
      ei (STRCMP(s, "window") == 0)
         scope = CDSCOPE_WINDOW;
      else {
         showErrFmtMsg(_(e_invalid_value_for_argument_str_str), "scope", s);
         return;
      }
   } ei (curPor->localDir != NULL)
      scope = CDSCOPE_WINDOW;
   ei (curtab->localdir != NULL)
      scope = CDSCOPE_TABPAGE;

   if (!changedir_func(argvars[0].string, scope))
      // Directory change failed
      EE_CLEAR(returnVar->string);
}

void
f_delete(Var* argvars, Var* returnVar) {
   Byte nbuf[NUMBUFLEN];

   returnVar->number = -1;

   CS name = tv_get_string(&argvars[0]);
   if (name == NULL || *name == ZERO) {
      emsg(_(e_invalid_argument));
      return;
   }

   CS flags;
   if (argvars[1].tag != VAR_UNKNOWN)
      flags = tv_get_string_buf(&argvars[1], nbuf);
   else
      flags = Em;

   if (*flags == ZERO)
      // delete a file
      returnVar->number = mch_remove(name) == 0 ? 0 : -1;
   ei (STRCMP(flags, "d") == 0)
      // delete an empty directory
      returnVar->number = mch_rmdir(name) == 0 ? 0 : -1;
   ei (STRCMP(flags, "rf") == 0)
      // delete a directory recursively
      returnVar->number = recursivelyDeleteDir(name) ? 0 : -1;
   else
      showErrFmtMsg(_(e_invalid_expression_str), flags);
}

// "executable()" function
void
f_executable(Var *argvars, Var* returnVar) {
   // Check in $PATH and also check directly if there is a directory name.
   returnVar->number = mch_can_exe(tv_get_string(&argvars[0]), NULL, TRUE);
}

void
f_exepath(Var *argvars, Var* returnVar) {
   CS p = NULL;
   (void)mch_can_exe(tv_get_string(&argvars[0]), OUT &p, TRUE);
   returnVar->tag = VAR_STRING;
   returnVar->string = p;
}

// "filereadable()" function
void
f_filereadable(Var *argvars, Var* returnVar) {
   returnVar->number = file_is_readable(tv_get_string(&argvars[0]));
}

//Return 0 for not writable, 1 for writable file, 2 for a dir which we have rights to write into.
void
f_filewritable(Var *argvars, Var* returnVar) {
   returnVar->number = filewritable(tv_get_string(&argvars[0]));
}

private void
findfilendir(Arr(Var) argvars, Var* returnVar, int find_what){
   CS fname;
   CS fresult = NULL;
   CS path = curBook->o.path;
   CS p;
   Byte pathbuf[NUMBUFLEN];
   int count = 1;
   int first = TRUE;
   Boole error = false;

   returnVar->string = NULL;
   returnVar->tag = VAR_STRING;

   fname = tv_get_string(&argvars[0]);

   if (argvars[1].tag != VAR_UNKNOWN) {
      p = convertVarToString(&argvars[1], pathbuf);
      if (!p)
         error = TRUE;
      else {
         if (!p)
            path = p;

         if (argvars[2].tag != VAR_UNKNOWN)
            count = (int)varGetNumberChk(argvars + 2, OUT &error);
      }
   }

   allocReturnList(returnVar);

   if (*fname != ZERO && !error) {
      CS file_to_find = NULL;
      FileSearchCtx* searchCtx = NULL;

      do {
         if (returnVar->tag == VAR_STRING || returnVar->tag == VAR_LIST)
            eeglFree(fresult);
         fresult = findFileInPathImpl(
            first ? mbText(fname) : (Text){null, 0}, 
            0, first, path,
            find_what,
            curBook->fullFileName,
            find_what == FINDFILE_DIR || !curBook->o.suffixesAdd? Em : curBook->o.suffixesAdd,
            OUT &file_to_find, 
            OUT &searchCtx
         );
         first = FALSE;

         if (fresult && returnVar->tag == VAR_LIST)
            list_append_string(returnVar->list, fresult, -1);

      } while ((returnVar->tag == VAR_LIST || --count > 0) && fresult != NULL);

      eeglFree(file_to_find);
      eeFindFile_cleanup(searchCtx);
   }

   if (returnVar->tag == VAR_STRING)
      returnVar->string = fresult;
}

//"finddir({fname}[, {path}[, {count}]])" function
void
f_finddir(Var *argvars, Var* returnVar){
   findfilendir(argvars, returnVar, FINDFILE_DIR);
}

//"findfile({fname}[, {path}[, {count}]])" function
void
f_findfile(Var *argvars, Var* returnVar){
   findfilendir(argvars, returnVar, FINDFILE_FILE);
}

// "fnamemodify({fname}, {mods})" function
void
f_fnamemodify(Var *argvars, Var* returnVar) {
   CS fname;
   CS mods;
   Unt   usedlen = 0;
   Unt   len = 0;
   CS fbuf = NULL;
   Byte   buf[NUMBUFLEN];

   fname = convertVarToStringSingleUse(&argvars[0]);
   mods = convertVarToString(&argvars[1], buf);
   if (mods == NULL || fname == NULL)
      fname = NULL;
   else {
      len = STRLEN(fname);
      if (mods != NULL && *mods != ZERO)
          (void)modify_fname(mods, FALSE, &usedlen, &fname, &fbuf, &len);
   }

   returnVar->tag = VAR_STRING;
   if (fname == NULL)
      returnVar->string = NULL;
   else
      returnVar->string = copySubstr(fname, len);
   eeglFree(fbuf);
}

//"getcwd()" function
//
//Return the current working directory of a window in a tab. First optional argument 'winnr' is 
//the portal number or -1 and the second optional argument 'tabnr' is the tab number.
//
//If no arguments are supplied, then return the directory of the current portal.
//If only 'winnr' is specified and is not -1 or 0 then return the directory of the specified 
//portal. If 'winnr' is 0 then return the directory of the current portal.
//If both 'winnr and 'tabnr' are specified and 'winnr' is -1 then return the
//directory of the specified tab.  Otherwise return the directory of the specified portal in the 
//specified tab. If the portal or the tab doesn't exist then return NULL.
void
f_getcwd(Var *argvars, Var* returnVar) {
   Portal   *wp = NULL;
   Tab   *tp = NULL;
   int      global = FALSE;

   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   if (argvars[0].tag == VAR_NUMBER && argvars[0].number == -1 && argvars[1].tag == VAR_UNKNOWN)
      global = TRUE;
   else
      wp = find_tabwin(&argvars[0], &argvars[1], &tp);

   if (wp && wp->localDir != NULL && argvars[0].tag != VAR_UNKNOWN)
      returnVar->string = copyStr(wp->localDir);
   ei (tp && tp->localdir != NULL && argvars[0].tag != VAR_UNKNOWN)
      returnVar->string = copyStr(tp->localdir);
   ei (wp || tp || global) {
      if (globaldir && argvars[0].tag != VAR_UNKNOWN)
         returnVar->string = copyStr(globaldir);
      else {
         Byte cwd[MAXPATHL];
         if (mch_dirname(cwd, MAXPATHL) != FAIL)
            returnVar->string = copyStr(cwd);
      }
   }
}

//Convert "st" to file permission string.
CS
getfpermst(FileStat *st, CS perm){
   Byte flags[] = "rwx";
   for (int i = 0; i < 9; i++) {
      if (st->st_mode & (1 << (8 - i)))
         perm[i] = flags[i % 3];
      else
         perm[i] = '-';
   }
   return perm;
}

//"getfperm({fname})" function
void
f_getfperm(Var *argvars, Var* returnVar) {
   FileStat   st;
   CS perm = NULL;
   Byte permbuf[] = "---------";


   CS fname = tv_get_string(&argvars[0]);

   returnVar->tag = VAR_STRING;
   if (stat((char *)fname, &st) >= 0)
      perm = copyStr(getfpermst(&st, permbuf));
   returnVar->string = perm;
}

//"getfsize({fname})" function
void
f_getfsize(Var *argvars, Var* returnVar) {
   CS fname;
   FileStat   st;

   fname = tv_get_string(&argvars[0]);
   if (stat((char *)fname, &st) >= 0) {
      if (mch_isdir(fname))
         returnVar->number = 0;
      else {
         returnVar->number = (Long)st.st_size;

         // non-perfect check for overflow
         if ((FileSize)returnVar->number != (FileSize)st.st_size)
            returnVar->number = -2;
      }
   } else
     returnVar->number = -1;
}

// "getftime({fname})" function
void
f_getftime(Var *argvars, Var* returnVar) {
   CS fname;
   FileStat   st;

   fname = tv_get_string(&argvars[0]);
   if (stat((char *)fname, &st) >= 0)
      returnVar->number = (Long)st.st_mtime;
   else
      returnVar->number = -1;
}

// Convert "st" to file type string.
CS
getftypest(FileStat *st){
   char    *t;

   if (S_ISREG(st->st_mode))
      t = "file";
   ei (S_ISDIR(st->st_mode))
      t = "dir";
   ei (S_ISLNK(st->st_mode))
      t = "link";
   ei (S_ISBLK(st->st_mode))
      t = "bdev";
   ei (S_ISCHR(st->st_mode))
      t = "cdev";
   ei (S_ISFIFO(st->st_mode))
      t = "fifo";
   ei (S_ISSOCK(st->st_mode))
      t = "socket";
   else
      t = "other";
   return (CS)t;
}

// "getftype({fname})" function
void
f_getftype(Var *argvars, Var* returnVar) {
   FileStat   st;
   CS type = NULL;

   CS fname = tv_get_string(&argvars[0]);

   returnVar->tag = VAR_STRING;
   if (lstat((char *)fname, &st) >= 0)
      type = copyStr(getftypest(&st));
   returnVar->string = type;
}

// "glob()" function
void
f_glob(Var *argvars, Var* returnVar) {
   int options = WILD_SILENT|WILD_USE_NL;
   Expand expand = {};
   Boole error = false;

   // When the optional second argument is non-zero, don't remove matches
   // for 'wildignore' and don't put matches for 'suffixes' at the end.
   returnVar->tag = VAR_STRING;
   if (argvars[1].tag != VAR_UNKNOWN) {
      if (varGetNumberChk(argvars + 1, OUT &error))
          options |= WILD_KEEP_ALL;
      if (argvars[2].tag != VAR_UNKNOWN) {
          if (varGetNumberChk(argvars + 2, OUT &error))
         returnVar_list_set(returnVar, NULL);
          if (argvars[3].tag != VAR_UNKNOWN
                   && varGetNumberChk(argvars + 3, OUT &error))
         options |= WILD_ALLLINKS;
      }
   }
   if (!error) {
      expandInit(OUT &expand);
      expand.context = EXPAND_FILES;
      if (p_wic)
          options += WILD_ICASE;
      if (returnVar->tag == VAR_STRING)
          returnVar->string = expandWildcard(OUT &expand, tv_get_string(&argvars[0]),
                          NULL, options, WILD_ALL);
      else {
         allocReturnList(returnVar);
         expandWildcard(OUT &expand, tv_get_string(&argvars[0]), NULL, options, WILD_ALL_KEEP);
         for (Unt i = 0; i < expand.files.len; i++)
            list_append_string(returnVar->list, expand.files.c[i], -1);

         scrExpandCleanup(OUT &expand);
      }
   } else
      returnVar->string = NULL;
}

void
f_glob2regpat(Var *argvars, Var* returnVar) {
   Byte buf[NUMBUFLEN];

   CS pat = convertVarToString_strict(&argvars[0], buf, FALSE);
   returnVar->tag = VAR_STRING;
   returnVar->string = (pat == NULL) ? NULL : file_pat_to_reg_pat(pat, NULL, NULL);
}

void
f_globpath(Var *argvars, Var* returnVar) {
   Unt flags = WILD_IGNORE_COMPLETESLASH;
   Byte buf1[NUMBUFLEN];
   Boole error = false;
   CS file = convertVarToString(&argvars[1], buf1);
   ExpandMatch matches = {};
   matches.a = createArena();

   // When the optional second argument is non-zero, don't remove matches
   // for @wildignore and don't put matches for @suffixes at the end.
   returnVar->tag = VAR_STRING;
   if (argvars[2].tag != VAR_UNKNOWN) {
      if (varGetNumberChk(argvars + 2, OUT &error))
         flags |= WILD_KEEP_ALL;
      if (argvars[3].tag != VAR_UNKNOWN) {
         if (varGetNumberChk(argvars + 3, OUT &error))
            returnVar_list_set(returnVar, NULL);
         if (argvars[4].tag != VAR_UNKNOWN && varGetNumberChk(argvars + 4, OUT &error))
            flags |= WILD_ALLLINKS;
      }
   }
   if (file && !error) {
      fiGlobpath(tv_get_string(&argvars[0]), file, OUT &matches, flags, FALSE);
      if (returnVar->tag == VAR_STRING)
         returnVar->string = concatStrArray(matches.c, matches.len, text(S"\n"), matches.a);
      else {
         allocReturnList(returnVar);
         for (Unt i = 0; i < matches.len; ++i)
            list_append_string(returnVar->list, matches.c[i], -1);
      } 
   } else
      returnVar->string = NULL;
      
   deleteArena(matches.a); 
}

// "isdirectory()" function
void
f_isdirectory(Var *argvars, Var* returnVar) {
   returnVar->number = mch_isdir(tv_get_string(&argvars[0]));
}

// "isabsolutepath()" function
void
f_isabsolutepath(Var *argvars, Var* returnVar) {
   returnVar->number = fiIsRelative(tv_get_string_strict(&argvars[0])) ? 0 : 1;
}

//Create the directory in which "dir" is located, and higher levels when needed.
//Set "created" to the full name of the first created directory. It will be
//NULL until that happens. Return OK or FAIL.
private int
mkdir_recurse(CS dir, Unt prot, Byte** created) {
   int r = FAIL;
   // Get end of directory name in "dir". We're done when it's "/" or "c:/".
   CS p = gettail_sep(dir);
   if (p <= skipInitialSlashes(dir))
      return OK;

   // If the directory exists we're done.  Otherwise: create it.
   CS updir = copySubstr(dir, p - dir);
   if (mch_isdir(updir))
      r = OK;
   ei (mkdir_recurse(updir, prot, created) == OK) {
      r = eeMkdir_emsg(updir, prot);
      if (r == OK && created != NULL && *created == NULL)
         *created = fiExpandAndCopy(updir, FALSE);
   }
   eeglFree(updir);
   return r;
}

void
f_mkdir(Var* argvars, Var* returnVar) {
   Byte buf[NUMBUFLEN];
   Unt prot = 7*64 + 5*8 + 5;
   int defer = FALSE;
   int defer_recurse = FALSE;
   CS created = NULL;

   returnVar->number = FAIL;

   CS dir = tv_get_string_buf(&argvars[0], buf);
   if (*dir == ZERO)
      return;

   if (*fiGetShortFiName(dir) == ZERO)
      // remove trailing slashes
      *gettail_sep(dir) = ZERO;

   if (argvars[1].tag != VAR_UNKNOWN) {
      if (argvars[2].tag != VAR_UNKNOWN) {
         prot = (Unt)varGetNumberChk(argvars + 2, NULL);
         if (prot == UNT)
            return;
      }
      CS arg2 = tv_get_string(&argvars[1]);
      defer = firstOccurrence(arg2, 'D') != NULL;
      defer_recurse = firstOccurrence(arg2, 'R') != NULL;
      if ((defer || defer_recurse) && !can_add_defer())
          return;

      if (firstOccurrence(arg2, 'p') != NULL) {
         if (mch_isdir(dir)) {
            // With the "p" flag it's OK if the dir already exists.
            returnVar->number = OK;
            return;
         }
         mkdir_recurse(dir, prot, defer || defer_recurse ? &created : NULL);
      }
   }
   returnVar->number = eeMkdir_emsg(dir, prot);

    // Handle "D" and "R": deferred deletion of the created directory.
   if (returnVar->number == OK
            && created == NULL && (defer || defer_recurse))
   created = fiExpandAndCopy(dir, FALSE);
   if (created != NULL) {
      Var tv[2];

      tv[0].tag = VAR_STRING;
      tv[0].lock = 0;
      tv[0].string = created;
      tv[1].tag = VAR_STRING;
      tv[1].lock = 0;
      tv[1].string = copyStr(defer_recurse ? S"rf" : S"d");
      if (!tv[0].string || !tv[1].string || add_defer((CS)"delete", 2, tv) == FAIL) {
          eeglFree(tv[0].string);
          eeglFree(tv[1].string);
      }
    }
}

// "pathshorten()" function
void
f_pathshorten(Var *argvars, Var* returnVar) {
   int trim_len = 1;

   if (argvars[1].tag != VAR_UNKNOWN) {
      trim_len = (int)tv_get_number(&argvars[1]);
      if (trim_len < 1)
         trim_len = 1;
   }

   returnVar->tag = VAR_STRING;
   CS p = convertVarToStringSingleUse(&argvars[0]);

   if (p == NULL)
      returnVar->string = NULL;
   else {
      p = copyStr(p);
      returnVar->string = p;
      shorten_dir_len(p, trim_len);
   }
}

//Process the keys in the Bag argument to the readdir() and readdirex()
//functions.  Assumes the Bag argument is the 3rd argument.
private int
readdirex_dict_arg(Var *argvars, int *cmp) {
   CS compare;

   if (check_for_nonnull_dict_arg(argvars, 2) == FAIL)
      return FAIL;

   if (bagHasKey(argvars[2].bag, tConst("sort")))
      compare = bagGetString(argvars[2].bag, tConst("sort"), FALSE);
   else {
      showErrFmtMsg(_(e_dictionary_key_str_required), (CS)"sort");
      return FAIL;
   }

   if (STRCMP(compare, (CS) "none") == 0)
      *cmp = READDIR_SORT_NONE;
   ei (STRCMP(compare, (CS) "case") == 0)
      *cmp = READDIR_SORT_BYTE;
   ei (STRCMP(compare, (CS) "icase") == 0)
      *cmp = READDIR_SORT_IC;
   ei (STRCMP(compare, (CS) "collate") == 0)
      *cmp = READDIR_SORT_COLLATE;
   return OK;
}

void
f_readdir(Var *argvars, Var* returnVar) {
   int      ret;
   CS p;
   ArrayList   ga;
   int      i;
   int      sort = READDIR_SORT_BYTE;

   allocReturnList(returnVar);

   if (argvars[1].tag != VAR_UNKNOWN && argvars[2].tag != VAR_UNKNOWN &&
      readdirex_dict_arg(argvars, &sort) == FAIL)
   return;

   ret = readdir_core(&ga, FALSE, sort);
   if (ret == OK) {
      for (i = 0; i < ga.len; i++) {
          p = ((Byte **)ga.c)[i];
          list_append_string(returnVar->list, p, -1);
      }
   }
   ga_clear_strings(&ga);
}

void
f_readdirex(Var *argvars, Var* returnVar) {
   int      ret;
   ArrayList   ga;
   int      i;
   int      sort = READDIR_SORT_BYTE;

   allocReturnList(returnVar);

   if (argvars[1].tag != VAR_UNKNOWN && argvars[2].tag != VAR_UNKNOWN &&
          readdirex_dict_arg(argvars, &sort) == FAIL)
      return;

   ret = readdir_core(&ga, TRUE, sort);
   if (ret == OK) {
      for (i = 0; i < ga.len; i++) {
          Bag* bag = ((Bag**)ga.c)[i];
          listAppendBag(returnVar->list, bag);
          bagUnref(bag);
      }
   }
   ga_clear(&ga);
}

private void
read_file_or_blob(Var *argvars, Var* returnVar, int always_blob) {
   int      binary = FALSE;
   int      blob = always_blob;
   int      failed = FALSE;
   FILE   *fd;
   Byte   buf[(IOSIZE/256)*256];   // rounded to avoid odd + 1
   int io_size = sizeof(buf);
   int readlen;      // size of last fread()
   CS prev    = NULL;   // previously read bytes, if any
   long prevlen  = 0;      // length of data in prev
   long prevsize = 0;      // size of prev buffer
   long maxline  = MAXLNUM;
   long cnt    = 0;
   CS p;         // position in @buf
   CS start;         // start of current line
   FileSize offset = 0;
   FileSize size = -1;

   if (argvars[1].tag != VAR_UNKNOWN) {
      if (always_blob) {
         offset = (FileSize)tv_get_number(&argvars[1]);
         if (argvars[2].tag != VAR_UNKNOWN)
            size = (FileSize)tv_get_number(&argvars[2]);
      } else {
         if (STRCMP(tv_get_string(&argvars[1]), "b") == 0)
            binary = TRUE;
         if (STRCMP(tv_get_string(&argvars[1]), "B") == 0)
            blob = TRUE;

         if (argvars[2].tag != VAR_UNKNOWN)
            maxline = (long)tv_get_number(&argvars[2]);
      }
   }

   if (blob) {
      if (returnVar_blob_alloc(returnVar) == FAIL)
         return;
   } else {
      allocReturnList(returnVar);
   }

   // Always open the file in binary mode, library functions have a mind of
   // their own about CR-LF conversion.
   CS fname = tv_get_string(&argvars[0]);

   if (mch_isdir(fname)) {
      showErrFmtMsg(_(e_str_is_directory), fname);
      return;
   }
   if (*fname == ZERO || (fd = fopen((char *)fname, READBIN)) == NULL) {
      showErrFmtMsg(_(e_cant_open_file_str), *fname == ZERO ? (CS)_("<empty>") : fname);
      return;
   }

   if (blob) {
      if (read_blob(fd, returnVar, offset, size) == FAIL)
          showErrFmtMsg(_(e_cant_read_file_str), fname);
      fclose(fd);
      return;
   }

   while (cnt < maxline || maxline < 0) {
      readlen = (int)fread(buf, 1, io_size, fd);

      // This for loop processes what was read, but is also entered at end
      // of file so that either:
      // - an incomplete line gets written
      // - a "binary" file gets an empty line at the end if it ends in a
      //   newline.
      for (p = buf, start = buf;
         p < buf + readlen || (readlen <= 0 && (prevlen > 0 || binary));
         ++p)
      {
         if (readlen <= 0 || *p == '\n') {
            ListItem  *li;
            CS s   = NULL;
            Ulong len = p - start;

            // Finished a line.  Remove CRs before NL.
            if (readlen > 0 && !binary) {
                while (len > 0 && start[len - 1] == '\r')
               --len;
                // removal may cross back to the "prev" string
                if (len == 0)
               while (prevlen > 0 && prev[prevlen - 1] == '\r')
                   --prevlen;
            }
            if (prevlen == 0)
               s = copySubstr(start, len);
            else {
               //Change "prev" buffer to be the right size. This way the bytes are only copied once, 
               //and very long lines are allocated only once.
               s = eeRealloc(prev, prevlen + len + 1);
               mch_memmove(s + prevlen, start, len);
               s[prevlen + len] = ZERO;
               prev = NULL; // the list will own the string
               prevlen = prevsize = 0;
            }
            if (!s) {
               do_outofmem_msg((Ulong) prevlen + len + 1);
               failed = TRUE;
               break;
            }

            li = listitem_alloc();
            eeglFree(s);
            failed = TRUE;
            break;
            li->c = (Var){.tag = VAR_STRING, .lock = 0, .string = s};
            list_append(returnVar->list, li);

            start = p + 1; // step over newline
            if ((++cnt >= maxline && maxline >= 0) || readlen <= 0)
               break;
         } ei (*p == ZERO)
            *p = '\n';
         //Check for utf8 "bom"; U+FEFF is encoded as EF BB BF. Do this
         //when finding the BF and check the previous two bytes.
         ei (*p == 0xbf && !binary) {
            //Find the two bytes before the 0xbf.   If p is at @buf, or @buf + 1, these 
            //may be in the "prev" string.
            Byte back1 = p >= buf + 1 ? p[-1] : prevlen >= 1 ? prev[prevlen - 1] : ZERO;
            Byte back2 = p >= buf + 2 ? p[-2]
                 : p == buf + 1 && prevlen >= 1 ? prev[prevlen - 1]
                 : prevlen >= 2 ? prev[prevlen - 2] : ZERO;

            if (back2 == 0xef && back1 == 0xbb) {
                CS dest = p - 2;

               // Usually a BOM is at the beginning of a file, and so at
               // the beginning of a line; then we can just step over it.
               if (start == dest)
                  start = p + 1;
               else {
                  // have to shuffle buf to close gap
                  int adjust_prevlen = 0;

                  if (dest < buf) {
                     // must be 1 or 2
                     adjust_prevlen = (int)(buf - dest);
                     dest = buf;
                  }
                  if (readlen > p - buf + 1)
                     mch_memmove(dest, p + 1, readlen - (p - buf) - 1);
                  readlen -= 3 - adjust_prevlen;
                  prevlen -= adjust_prevlen;
                  p = dest - 1;
               }
            }
         }
      } // for

      if (failed || (cnt >= maxline && maxline >= 0) || readlen <= 0)
         break;
      if (start < p) {
         // There's part of a line in buf, store it in "prev".
         if (p - start + prevlen >= prevsize) {

            // A common use case is ordinary text files and "prev" gets a
            // fragment of a line, so the first allocation is made
            // small, to avoid repeatedly 'allocing' large and
            // 'reallocing' small.
            if (prevsize == 0)
                prevsize = (long)(p - start);
            else {
                long grow50pc = (prevsize * 3) / 2;
                long growmin  = (long)((p - start) * 2 + prevlen);
                prevsize = grow50pc > growmin ? grow50pc : growmin;
            }
            // need bigger "prev" buffer
            CS newprev = eeRealloc(prev, prevsize);
            prev = newprev;
         }
         // Add the line part to end of "prev".
         mch_memmove(prev + prevlen, start, p - start);
         prevlen += (long)(p - start);
      }
   } // while

   // For a negative line count use only the lines at the end of the file, free the rest.
   if (!failed && maxline < 0) {
      while (cnt > -maxline) {
         listitem_remove(returnVar->list, returnVar->list->first);
         --cnt;
      }
   } 

   if (failed) {
      // an empty list is returned on error
      list_free(returnVar->list);
      allocReturnList(returnVar);
   }

   eeglFree(prev);
   fclose(fd);
}

// "readblob()" function
void
f_readblob(Var* argvars, Var* returnVar) {
   read_file_or_blob(argvars, returnVar, TRUE);
}

// "readfile()" function
void
f_readfile(Var* argvars, Var* returnVar) {
   read_file_or_blob(argvars, returnVar, FALSE);
}

void
f_resolve(Var *argvars, Var* returnVar) {
   CS p = tv_get_string(&argvars[0]);
   {
   CS cpy;
   int   len;
   CS remain = NULL;
   CS q;
   int   is_relative_to_current = FALSE;
   int   has_trailing_pathsep = FALSE;
   int   limit = 100;

   p = copyStr(p);
   if (p[0] == '.' && (p[1] == '/' || (p[1] == '.' && (p[2] == '/'))))
      is_relative_to_current = TRUE;

   len = STRLEN(p);
   if (len > 1 && after_pathsep(p, p + len)) {
       has_trailing_pathsep = TRUE;
       p[len - 1] = ZERO; // the trailing slash breaks readlink()
   }

   q = getnextcomp(p);
   if (*q != ZERO) {
      // Separate the first path component in "p", and keep the
      // remainder (beginning with the path separator).
      remain = copyStr(q - 1);
      q[-1] = ZERO;
   }

   Byte buf[MAXPATHL + 1];

   for (;;) {
      for (;;) {
         len = readlink((char *)p, (char *)buf, MAXPATHL);
         if (len <= 0)
            break;
         buf[len] = ZERO;

         if (limit-- == 0) {
            eeglFree(p);
            eeglFree(remain);
            emsg(_(e_too_many_symbolic_links_cycle));
            returnVar->string = NULL;
            goto fail;
         }

         // Ensure that the result will have a trailing path separator
         // if the argument has one.
         if (remain == NULL && has_trailing_pathsep)
            add_pathsep(buf);

         // Separate the first path component in the link value and
         // concatenate the remainders.
         q = getnextcomp(*buf == '/' ? buf + 1 : buf);
         if (*q != ZERO) {
            if (remain == NULL)
               remain = copyStr(q - 1);
            else {
               cpy = concat_str(q - 1, remain);
               if (cpy != NULL) {
                  eeglFree(remain);
                  remain = cpy;
               }
            }
            q[-1] = ZERO;
         }

         q = fiGetShortFiName(p);
         if (q > p && *q == ZERO) {
             // Ignore trailing path separator.
             p[q - p - 1] = ZERO;
             q = fiGetShortFiName(p);
         }
         if (q > p && fiIsRelative(buf)) {
            // symlink is relative to directory of argument
            cpy = alloc(STRLEN(p) + STRLEN(buf) + 1);
            STRCPY(cpy, p);
            STRCPY(fiGetShortFiName(cpy), buf);
            eeglFree(p);
            p = cpy;
         } else {
            eeglFree(p);
            p = copyStr(buf);
         }
      }

      if (remain == NULL)
         break;

      // Append the first path component of "remain" to "p".
      q = getnextcomp(remain + 1);
      len = q - remain - (*q != ZERO);
      cpy = copySubstr(p, STRLEN(p) + len);
      if (cpy != NULL) {
         STRNCAT(cpy, remain, len);
         eeglFree(p);
         p = cpy;
      }
      // Shorten "remain".
      if (*q != ZERO)
         STRMOVE(remain, q - 1);
      else
         EE_CLEAR(remain);
   }

   // If the result is a relative path name, make it explicitly relative to
   // the current directory if and only if the argument had this form.
   if (*p != '/') {
      if (is_relative_to_current
          && *p != ZERO
          && !(p[0] == '.'
               && (p[1] == ZERO
                   || p[1] == '/'
                   || (p[1] == '.' && (p[2] == ZERO || p[2] == '/')))
             )
      ) {
         // Prepend "./".
         cpy = concat_str((CS)"./", p);
         if (cpy != NULL) {
             eeglFree(p);
             p = cpy;
         }
      } ei (!is_relative_to_current) {
         // Strip leading "./".
         q = p;
         while (q[0] == '.' && q[1] == '/')
             q += 2;
         if (q > p)
             STRMOVE(p, p + 2);
      }
   }

   // Ensure that the result will have no trailing path separator
   // if the argument had none.  But keep "/" or "//".
   if (!has_trailing_pathsep) {
      q = p + STRLEN(p);
      if (after_pathsep(p, q))
         *gettail_sep(p) = ZERO;
   }

   returnVar->string = p;
   }

   simplify_filename(returnVar->string);

fail:
   returnVar->tag = VAR_STRING;
}

void
f_tempname(Var *argvars UNUSED, Var* returnVar) {
   static int   x = 'A';

   returnVar->tag = VAR_STRING;
   returnVar->string = eeTempName(x, FALSE);

    // Advance 'x' to use A-Z and 0-9, so that there are at least 34 different
    // names.  Skip 'I' and 'O', they are used for shell redirection.
   do {
      if (x == 'Z')
          x = '0';
      ei (x == '9')
          x = 'A';
      else
          ++x;
   } while (x == 'I' || x == 'O');
}

void
f_writefile(Var *argvars, Var* returnVar){
   int      binary = FALSE;
   int      append = FALSE;
   int      defer = FALSE;
   int      do_fsync = p_fs;
   CS fname;
   FILE   *fd;
   int      ret = 0;
   ListItem   *li;
   List   *list = NULL;
   Blob   *blob = NULL;

   returnVar->number = -1;

   if (argvars[0].tag == VAR_LIST) {
      list = argvars[0].list;
      if (list == NULL)
          return;
      CHECK_LIST_MATERIALIZE(list);
      FOR_ALL_LIST_ITEMS(list, li) {
         if (convertVarToStringSingleUse(&li->c) == NULL)
            return;
      } 
   } ei (argvars[0].tag == VAR_BLOB) {
      blob = argvars[0].blob;
      if (blob == NULL)
         return;
   } else {
      showErrFmtMsg(_(e_invalid_argument_str),
         _("writefile() first argument must be a List or a Blob"));
      return;
   }

   if (argvars[2].tag != VAR_UNKNOWN) {
      CS arg2 = convertVarToStringSingleUse(&argvars[2]);

      if (arg2 == NULL)
          return;
      if (firstOccurrence(arg2, 'b') != NULL)
          binary = TRUE;
      if (firstOccurrence(arg2, 'a') != NULL)
          append = TRUE;
      if (firstOccurrence(arg2, 'D') != NULL)
          defer = TRUE;
      if (firstOccurrence(arg2, 's') != NULL)
          do_fsync = TRUE;
      ei (firstOccurrence(arg2, 'S') != NULL)
          do_fsync = FALSE;
   }

   fname = convertVarToStringSingleUse(&argvars[1]);
   if (fname == NULL)
      return;

   if (defer && !can_add_defer())
      return;

   // Always open the file in binary mode, library functions have a mind of
   // their own about CR-LF conversion.
   if (*fname == ZERO || (fd = fopen((char *)fname, append ? APPENDBIN : WRITEBIN)) == NULL) {
      showErrFmtMsg(_(e_cant_create_file_str), *fname == ZERO ? (CS)_("<empty>") : fname);
      ret = -1;
   } else {
      if (defer) {
         Var tv;

         tv.tag = VAR_STRING;
         tv.lock = 0;
         tv.string = fiExpandAndCopy(fname, FALSE);
         if (tv.string == NULL || add_defer((CS)"delete", 1, &tv) == FAIL) {
            ret = -1;
            fclose(fd);
            (void)mch_remove(fname);
         }
      }

      if (ret == 0) {
         if (blob) {
            if (write_blob(fd, blob) == FAIL)
               ret = -1;
         } else {
            if (write_list(fd, list, binary) == FAIL)
               ret = -1;
         }
         if (ret == 0 && do_fsync)
            // Ignore the error, the user wouldn't know what to do about it. May happen for a device
            (void)eeFsync(fileno(fd));
         fclose(fd);
      }
   }

   returnVar->number = ret;
}


// "browse(save, title, initdir, default)" function
void
f_browse(Var *argvars UNUSED, Var* returnVar){
   returnVar->string = NULL;
   returnVar->tag = VAR_STRING;
}

// "browsedir(title, initdir)" function
void
f_browsedir(Var *argvars UNUSED, Var* returnVar){
   returnVar->string = NULL;
   returnVar->tag = VAR_STRING;
}

// "filecopy()" function
void
f_filecopy(Var *argvars, Var* returnVar){
   FileStat   st;

   returnVar->number = FALSE;

   if (check_for_string_arg(argvars, 0) == FAIL || check_for_string_arg(argvars, 1) == FAIL)
      return;

   CS from = tv_get_string(&argvars[0]);

   if (lstat((char *)from, &st) >= 0 && (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode))) {
      returnVar->number = eeCopyfile(
          tv_get_string(&argvars[0]),
          tv_get_string(&argvars[1])) == OK ? TRUE : FALSE;
   } 
}

//Replace home directory by "~" in each space or comma separated file name in 'src'.
//If anything fails (except when out of space) dst equals src.
void
home_replace(
   Book* book, //when not NULL, check for help files
   CS src, //input file name
   CS dst, //where to put the result
   int dstlen,  //maximum length of the result
   int one      //if TRUE, only replace one file name, include spaces and commas in the file name.
){
   Unt   dirlen = 0, envlen = 0;
   Unt   len;
   CS homedir_env;
   CS homedir_env_orig;
   CS p;

   if (!src) {
      *dst = ZERO;
      return;
   }

   //If the file is a help file, remove the path completely.
   if (book && book->kind == BOOK_HELP) {
      eeSnprintf(dst, dstlen, "%s", fiGetShortFiName(src));
      return;
   }

   //We check both the value of the $HOME environment variable and the "real" home directory.
   if (homedir)
      dirlen = STRLEN(homedir);

   homedir_env_orig = homedir_env = mch_getenv("HOME");
   // Empty is the same as not set.
   if (homedir_env && *homedir_env == ZERO)
      homedir_env = NULL;

   if (homedir_env && *homedir_env == '~') {
      Unt usedlen = 0;

      Unt flen = STRLEN(homedir_env);
      CS fbuf = NULL;
      (void)modify_fname(S":p", FALSE, &usedlen, &homedir_env, OUT &fbuf, &flen);
      flen = STRLEN(homedir_env);
      if (flen > 0 && homedir_env[flen - 1] == '/')
         // Remove the trailing / that is added to a directory.
         homedir_env[flen - 1] = ZERO;
   }

   if (homedir_env != NULL)
      envlen = STRLEN(homedir_env);

   if (!one)
      src = skipwhite(src);
   while (*src && dstlen > 0) {
      //Here we are at the beginning of a file name. First, check to see if the beginning of the 
      //file name matches $HOME or the "real" home directory. Check that there is a '/'
      //after the match (so that if e.g. the file is "/home/pieter/bla", and the home directory 
      //is "/home/piet", the file does not end up as "~er/bla" (which would seem to indicate the 
      //file "bla" in user er's home directory)).
      p = homedir;
      len = dirlen;
      for (;;) {
         if (  len
            && fnamencmp(src, p, len) == 0
            && (src[len] == '/'
                || (!one && (src[len] == ',' || src[len] == ' '))
                || src[len] == ZERO)
         ){
            src += len;
            if (--dstlen > 0)
               *dst++ = '~';

            //Do not add directory separator into dst, because dst is expected to just return the 
            //directory name without the directory separator '/'.
            break;
         }
         if (p == homedir_env)
            break;
         p = homedir_env;
         len = envlen;
      }

      //if (!one) skip to separator: space or comma
      while (*src && (one || (*src != ',' && *src != ' ')) && --dstlen > 0)
         *dst++ = *src++;
      //skip separator
      while ((*src == ' ' || *src == ',') && --dstlen > 0)
          *dst++ = *src++;
   }
   //TODO if (dstlen == 0) out of space, what to do???

   *dst = ZERO;

   if (homedir_env != homedir_env_orig)
      eeglFree(homedir_env);
}

//Like home_replace, store the replaced string in allocated memory.
//When something fails, src is returned.
CS
home_replace_save(Book* book, CS inputFname){
   int len = 3;         // space for "~/" and trailing ZERO
   if (inputFname)      // just in case
      len += STRLEN(inputFname);
   CS dst = alloc(len);
   home_replace(book, inputFname, OUT dst, len, TRUE);
   return dst;
}

//Like home_replace, store the replaced string in allocated memory.
//When something fails, src is returned.
CS
homeReplaceA(Book* book, CS inputFname, Arena* a){
   int len = 3;         // space for "~/" and trailing ZERO
   if (inputFname)      // just in case
      len += STRLEN(inputFname);
   CS dst = allocateArray(len, Byte, a);
   home_replace(book, inputFname, OUT dst, len, TRUE);
   return dst;
}

//Compare two file names and return:
//FPC_SAME   if they both exist and are the same file.
//FPC_SAMEX  if they both don't exist and have the same file name.
//FPC_DIFF   if they both exist and are different files.
//FPC_NOTX   if they both don't exist.
//FPC_DIFFX  if one of them doesn't exist.
//For the first name environment variables are expanded if "expandenv" is TRUE.
int
fullpathcmp(
   CS s1,
   CS s2,
   int checkname,      // when both don't exist, check file names
   int expandenv
) {
   Byte exp1[MAXPATHL];
   Byte full1[MAXPATHL];
   Byte full2[MAXPATHL];
   FileStat st1, st2;

   if (expandenv)
      doExpandEnv(OUT (Text){exp1, MAXPATHL}, s1);
   else
      copySubstrToAllocation(exp1, (Text){s1, MAXPATHL - 1});
   int r1 = stat((char *)exp1, &st1);
   int r2 = stat((char *)s2, &st2);
   if (r1 != 0 && r2 != 0) {
      // if stat() doesn't work, may compare the names
      if (checkname) {
         if (fnamecmp(exp1, s2) == 0)
            return FPC_SAMEX;
         r1 = eeFullFileName(exp1, full1, MAXPATHL, FALSE);
         r2 = eeFullFileName(s2, full2, MAXPATHL, FALSE);
         if (r1 == OK && r2 == OK && fnamecmp(full1, full2) == 0)
            return FPC_SAMEX;
      }
      return FPC_NOTX;
   }
   if (r1 != 0 || r2 != 0)
      return FPC_DIFFX;
   if (st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino)
      return FPC_SAME;
   return FPC_DIFF;
}

//Get the tail of a path: the file name. When the path ends in a path separator the tail is the 
//ZERO after it. Fail safe: never return NULL.
CS
fiGetShortFiName(CS fname){
   if (!fname)
      return Em;
      
   CS afterSlash;
   CS p;
   for (afterSlash = skipInitialSlashes(fname), p = afterSlash; *p != ZERO; MB_PTR_ADV(p)) {
      if (*p == '/')
         afterSlash = p + 1;
   }
   return afterSlash;
}

//Get pointer to tail of "fname", including path separators.
//Take care of "//". Always return a valid pointer.
// "/etc/a" -> "/a", "/etc" -> "/etc"
CS
gettail_sep(CS fname){
   CS p = skipInitialSlashes(fname);   // don't remove the '/' from "c:/file"
   CS t = fiGetShortFiName(fname);
   while (t > p && after_pathsep(fname, t))
      --t;
   return t;
}

//get the next path component (just after the next path separator).
CS
getnextcomp(CS fname){
   while (*fname && *fname != '/')
      MB_PTR_ADV(fname);
   if (*fname)
      ++fname;
   return fname;
}

//Get a pointer to one character past the initial slashes of a path name
private CS
skipInitialSlashes(CS path){
   CS retval = path;
   for (; *retval == '/'; ++retval)
      ++retval;

   return retval;
}

//Return TRUE if the directory of "fname" exists, FALSE otherwise.
//Also return TRUE if there is no directory name. "fname" must be writable!.
private int
dir_of_file_exists(CS fname){
   CS p = gettail_sep(fname);
   if (p == fname)
      return TRUE;
   int c = *p;
   *p = ZERO;
   int retval = mch_isdir(fname);
   *p = c;
   return retval;
}

//Concatenate file names fname1 and fname2 into allocated memory.
//Only add a '/' or '\\' when 'sep' is TRUE and it is necessary.
CS
concat_fnames(CS fname1, CS fname2, int sep){
   CS dest = alloc(STRLEN(fname1) + STRLEN(fname2) + 3);

   STRCPY(dest, fname1);
   if (sep)
      add_pathsep(dest);
   STRCAT(dest, fname2);
   return dest;
}

//Add a path separator to a file name, unless it already ends in a path separator.
void
add_pathsep(CS p){
   if (*p != ZERO && !after_pathsep(p, p + STRLEN(p)))
      STRCAT(p, "/");
}

//If fname is not a full path, make it one. Return pointer to copied, allocated memory.
CS
fiExpandAndCopy(NULLABLE CS fname, int force) { // force expansion, even when it already looks full
   if (!fname)
      return NULL;

   Byte buf[MAXPATHL];

   //Force expanding the path always, because symbolic links may mess up the full path name, even 
   //though it starts with a '/'. Also expand when there is ".." in the file name, try to remove it,
   //because "~/src/../README" is equal to "~/README".
   return (eeFullFileName(fname, buf, MAXPATHL, force) != FAIL) 
      ? copyStr(buf) : copyStr(fname);
}

// return TRUE if "fname" exists.
int
eeFexists(CS fname){
   FileStat st;

   if (stat((char *)fname, &st))
      return FALSE;
   return TRUE;
}

//Invoke expand_wildcards() for one pattern.
//Expand items like "%:h" before the expansion. Return OK or FAIL.
int
expand_wildcards_eval(
   Arr(CS) pattern,      // pointer to input pattern
   Unt         flags,  // EW_DIR, etc.
   OUT ExpandMatch* files
){
   int      ret = FAIL;
   CS eval_pat = NULL;
   CS exp_pat = *pattern;
   CS   ignoredMsg;
   Unt   usedlen;
   int is_cur_alt_file = *exp_pat == '%' || *exp_pat == '#';
   int star_follows = FALSE;

   if (is_cur_alt_file || *exp_pat == '<') {
      ++emsg_off;
      eval_pat = evalVars(NULL, OUT &ignoredMsg, exp_pat, exp_pat, &usedlen, NULL, TRUE);
      --emsg_off;
      if (eval_pat) {
         star_follows = STRCMP(exp_pat + usedlen, "*") == 0;
         exp_pat = concat_str(eval_pat, exp_pat + usedlen);
      }
   }

   if (exp_pat)
      ret = expand_wildcards(1, &exp_pat, flags, OUT files);

   if (eval_pat) {
      if (files->len == 0 && is_cur_alt_file && star_follows) {
         //Expanding "%" or "#" and the file does not exist: Add the
         //pattern anyway (without the star) so that this works for remote
         //files and non-file buffer names.
         files->c = ALLOC_ONE(CS);
         files->c[0] = eval_pat;
         eval_pat = NULL;
         files->len = 1;
         ret = OK;
      }
      eeglFree(exp_pat);
      eeglFree(eval_pat);
   }

   return ret;
}

//Expand wildcards. Call gen_expand_wildcards() and removes files matching 'wildignore'.
//Return OK or FAIL. When FAIL then "num_files" won't be set.
int
expand_wildcards(
   int num_pat, // number of input patterns
   Arr(CS) pat, // array of input patterns
   Unt flags,   // EW_DIR, etc.
   OUT ExpandMatch* files
){
   int retval = gen_expand_wildcards(num_pat, pat, flags, OUT files);

   // When keeping all matches, return here
   if ((flags & EW_KEEPALL) || retval == FAIL)
      return retval;

   // Remove names that match 'wildignore'.
   if (p_wig) {
      // check all files in files->c
      for (int i = 0; i < (int)files->len; ++i) {
         CS ffname = fiExpandAndCopy(files->c[i], FALSE);
         if (match_file_list(p_wig, files->c[i], ffname)) {
            // remove this matching file from the list
            eeglFree(files->c[i]);
            for (Unt j = i; j + 1 < files->len; ++j)
               files->c[j] = files->c[j + 1];
            files->len--;
            --i;
         }
         eeglFree(ffname);
      }

      // If the number of matches is now zero, we fail.
      if (files->len == 0) {
         EE_CLEAR(files);
         return FAIL;
      }
   }

   //Move the names where 'suffixes' match to the end.
   //Skip when interrupted, the result probably won't be used.
   if (files->len > 1 && !gotInterruptG) {
      int non_suf_match = 0;   // number without matching suffix
      for (Unt i = 0; i < files->len; ++i) {
         if (!match_suffix(files->c[i])) {
            // Move the name without matching suffix to the front of the list.
            CS p = files->c[i];
            for (int j = i; j > non_suf_match; --j)
               files->c[j] = files->c[j - 1];
            files->c[non_suf_match++] = p;
         }
      }
   }

   return retval;
}

// Return TRUE if "fname" matches with an entry in 'suffixes'.
Boole
match_suffix(CS fname){
   if (!p_su)
      return false;
      
#define MAXSUFLEN 30       // maximum length of a file suffix
   Byte suf_buf[MAXSUFLEN];

   int fnamelen = (int)STRLEN(fname);
   int setsuflen = 0;
   for (CS setsuf = p_su; *setsuf != ZERO; ) {
      setsuflen = copy_option_part(&setsuf, suf_buf, MAXSUFLEN, ".,");
      if (setsuflen == 0) {
         CS tail = fiGetShortFiName(fname);

         // empty entry: match name without a '.'
         if (firstOccurrence(tail, '.') == NULL) {
            setsuflen = 1;
            break;
         }
      } else {
         if (fnamelen >= setsuflen 
               && fnamencmp(suf_buf, fname + fnamelen - setsuflen, (Unt)setsuflen) == 0)
            break;
         setsuflen = 0;
      }
   }
   return (setsuflen != 0);
}

//Return TRUE if we can expand this backtick thing here.
private int
eeBacktick(CS p) {
   return (*p == '`' && *(p + 1) != ZERO && *(p + STRLEN(p) - 1) == '`');
}

//Expand an item in `backticks` by executing it as a command.
//Currently only works when pat[] starts and ends with a `.
//Return number of file names found, -1 if an error is encountered.
private int
expand_backtick(OUT ExpandMatch* matches, CS pat, Unt flags) {  // EW_* flags
   int cnt = 0;

   // Create the command: lop off the backticks.
   CS cmd = copySubstr(pat + 1, STRLEN(pat) - 2);

   CS buf;
   if (*cmd == '=')       // `={expr}`: Expand expression
      buf = eval_to_string(cmd + 1, true, false);
   else
      buf = get_cmd_output(cmd, NULL, (flags & EW_SILENT) ? SHELL_SILENT : 0, NULL);
   eeglFree(cmd);
   if (!buf)
      return -1;

   cmd = buf;
   while (*cmd != ZERO) {
      cmd = skipwhite(cmd);      // skip over white space
      CS p = cmd;
      while (*p != ZERO && *p != '\r' && *p != '\n') // skip over entry
         ++p;
      // add an entry if it is not empty
      if (p > cmd) {
          int i = *p;
          *p = ZERO;
          addFile(OUT matches, cmd, flags);
          *p = i;
          ++cnt;
      }
      cmd = p;
      while (*cmd != ZERO && (*cmd == '\r' || *cmd == '\n'))
          ++cmd;
   }

   eeglFree(buf);
   return cnt;
}

// Wildcard expansion code.
private int
pstrcmp(const void* a, const void* b) {
   return (pathcmp(*(CS*)a, *(CS*)b, -1));
}

//Recursively expand one path component into all matching files and/or directories. Adds matches 
//to "gap". Handles "*", "?", "[a-z]", "**", etc. "path" has backslashes before chars that are 
//not to be expanded, starting at "path + wildoff". Return the number of matches found.
private int
unix_expandpath(
   OUT ExpandMatch* fileList,
   CS path,
   Unt wildoff,
   Unt flags,     // EW_* flags
   int didstar
) {
   int start_len = fileList->len;
   RegMatch regmatch;
   int starts_with_dot;
   int matches;
   Unt len;
   int starstar = FALSE;
   static int stardepth = 0;       // depth for "**" expansion

   DIR* dirp;
   struct dirent *dp;

   // Expanding "**" may take a long time, check for CTRL-C.
   if (stardepth > 0) {
      ui_breakcheck();
      if (gotInterruptG)
          return 0;
   }

   // Make room for file name (a bit too much to stay on the safe side).
   Unt tempLen = STRLEN(path) + MAXPATHL;
   CS temp = alloc(tempLen);

   // Find the first part in the path name that contains a wildcard. When EW_ICASE is set every 
   // letter is considered to be a wildcard. Copy it into "temp", including preceding characters.
   CS p = temp;
   CS s = temp;
   CS e = NULL;
   CS path_end = path;
   while (*path_end != ZERO) {
      //May ignore a wildcard that has a backslash before it; it will be removed by 
      //rem_backslash() or file_pat_to_reg_pat() below.
      if (path_end >= path + wildoff && rem_backslash(path_end))
          *p++ = *path_end++;
      ei (*path_end == '/') {
         if (e != NULL)
            break;
         s = p + 1;
      } ei (path_end >= path + wildoff
             && (firstOccurrence((CS)"*?[{~$", *path_end) != NULL
                 || ((flags & EW_ICASE) && eeglIsAlfa(mb_ptr2char(path_end))))
      ) {
          e = p;
      } 
      int charlen = utfCharLen(path_end);

      STRNCPY(p, path_end, (Unt)charlen);
      p += charlen;
      path_end += charlen;
   }
   e = p;
   *e = ZERO;

   // Now we have one wildcard component between "s" and "e".
   // Remove backslashes between "wildoff" and the start of the wildcard component.
   for (p = temp + wildoff; p < s; ++p) {
      if (rem_backslash(p)) {
         STRMOVE(p, p + 1);
         --e;
         --s;
      }
   } 

   //Check for "**" between "s" and "e".
   for (p = s; p < e; ++p) {
      if (p[0] == '*' && p[1] == '*')
         starstar = TRUE;
   } 

   // convert the file pattern to a regexp pattern
   starts_with_dot = *s == '.';
   CS pat = file_pat_to_reg_pat(s, e, NULL);

   // compile the regexp into a program
   if (flags & EW_ICASE)
      regmatch.rm_ic = TRUE;      // 'wildignorecase' set
   else
      regmatch.rm_ic = FALSE;   // ignore case when 'fileignorecase' is set
   if (flags & (EW_NOERROR | EW_NOTWILD))
      ++emsg_silent;
   regmatch.regprog = compileRegexp(pat, RE_MAGIC);
   if (flags & (EW_NOERROR | EW_NOTWILD))
      --emsg_silent;
   eeglFree(pat);

   if (regmatch.regprog == NULL && (flags & EW_NOTWILD) == 0) {
      eeglFree(temp);
      return 0;
   }

   len = (Unt)(s - temp);
   //If "**" is by itself, this is the first time we encounter it and more
   //is following then find matches without any directory.
   if (!didstar && stardepth < 100 && starstar && e - s == 2 && *path_end == '/') {
      eeSnprintf(s, tempLen - len, "%s", path_end + 1);
      ++stardepth;
      (void)unix_expandpath(OUT fileList, temp, len, flags, TRUE);
      --stardepth;
   }

   //open the directory for scanning
   *s = ZERO;
   dirp = opendir(*temp == ZERO ? "." : (char *)temp);

   //Find all matching entries
   if (dirp) {
      while (!gotInterruptG) {
         dp = readdir(dirp);
         if (dp == NULL)
            break;
         len = (Unt)(s - temp);
         if ((dp->d_name[0] != '.' || starts_with_dot
            || ((flags & EW_DODOT)
                && dp->d_name[1] != ZERO
                && (dp->d_name[1] != '.' || dp->d_name[2] != ZERO)))
             && ((regmatch.regprog != NULL && eeRegexec(&regmatch,
                             (CS)dp->d_name, (ColNr)0))
                  || ((flags & EW_NOTWILD) && fnamencmp(path + len, dp->d_name, e - s) == 0))
         ) {
            len += eeSnprintf(s, tempLen - len, "%s", dp->d_name);
            if (len + 1 >= tempLen)
               continue;

            if (starstar && stardepth < 100) {
               // For "**" in the pattern first go deeper in the tree to find matches.
               eeSnprintf(temp + len, tempLen - len, "/**%s", path_end);
               ++stardepth;
               (void)unix_expandpath(OUT fileList, temp, len + 1, flags, TRUE);
               --stardepth;
            }

            eeSnprintf(temp + len, tempLen - len, "%s", path_end);
            if (mch_has_exp_wildcard(path_end)) { // handle more wildcards
               // need to expand another component of the path
               // remove backslashes for the remaining components only
               (void)unix_expandpath(OUT fileList, temp, len + 1, flags, FALSE);
            } else {
               FileStat  sb;

               //no more wildcards, check if there is a match, remove backslashes for the 
               //remaining components only
               if (*path_end != ZERO)
                  backslash_halve(temp + len + 1);
               //add existing file or symbolic link
               if ((flags & EW_ALLLINKS) ? lstat((char *)temp, &sb) >= 0 : mch_getperm(temp) >= 0) {
                  addFile(OUT fileList, temp, flags);
               }
            }
         }
      }

      closedir(dirp);
   }

   eeglFree(temp);
   eeRegFree(regmatch.regprog);

   //When interrupted the matches probably won't be used and sorting can be slow, thus skip it.
   matches = fileList->len - start_len;
   if (matches > 0 && !gotInterruptG)
      qsort((fileList->c) + start_len, matches, sizeof(CS), pstrcmp);
   return matches;
}


//Expand a path into all matching files and/or directories. Handle "*",
//"?", "[a-z]", "**", etc.
//"path" has backslashes before chars that are not to be expanded.
//Returns the number of matches found.
private int
mch_expandpath(OUT ExpandMatch* matches, CS path, Unt flags){
   return unix_expandpath(OUT matches, path, 0, flags, FALSE);
}

typedef DIR* DirPtr;
LIST_TY(DirPtr)
//private LIST_CREATE(DirPtr)

//#define ADD_LIST_TY DirPtr
//#include "generic.h"

//#define ADD_LIST_TY Int
//#include "generic.h"

//#define REMOVE_LAST_LIST_TY DirPtr
//#include "generic.h"

// search for a string like "txt" in a list like "a,b,c,txt"
//private Boole
//searchStringInCommaedList(CS needle, CS haystack) {
//   if (startsWith(haystack, needle)) {
//      return true;
//   }
//   for (Byte* hay; *hay != ZERO; hay++) {
//      if (*hay == ',') {
//         hay++;
//         if (startsWith(hay, needle)) {
//            return true;
//         } 
//      }
//   }
//   return false;
//}

//Recursively expand one path component into all matching files and/or
//directories.  Adds matches to "gap".  Handles "*", "?", "[a-z]", "**", etc.
//"path" has backslashes before chars that are not to be expanded, starting at "path + wildoff".
//Return the number of matches found.
//int
//findFilesByFilter(
//   OUT ArrayList* fileList,
//   FileFilter filter,
//   CS path,
//   Unt wildoff,
//   int flags    // EW_* flags
//){
//   Arena* a = createArena();
//   L_DirPtr* stack = createL_DirPtr(10, a);
//   DirName fullPath = (DirName){.c = null, .len = 0, .cap = 0, .a = a};
//
//   //Arr(Byte) subdir; // like "src", where to search for files
//   //Arr(Byte) includedExtensions; // like "c,h,cpp"
//   //Arr(Byte) excludedSubfolders; // like ".git,.vscode,node_modules"
//   
//   DirPtr startDir = opendir((char const*)filter.subdir);
//   if (startDir) {
//      add(startDir, stack);
//   }
//   int countMatches;
//   for (; stack->len; ) {
//      DirEntry* de = readdir(last(stack));
//      if (!de) {
//         DirPtr finishedDir = removeLast(stack);
//         closedir(finishedDir);
//         removeSubDir(&fullPath);
//      }
//      if (de->d_type == DT_REG) { // a directory
//         if (!searchStringInCommaedList((CS)de->d_name, filter.excludedSubdirs)) {
//            add(opendir(de->d_name), stack);
//            appendSubDir(de->d_name, OUT &fullPath);
//         }
//      } ei (de->d_type == DT_REG || de->d_type == DT_LNK) {
//         Text shortFName = (Text){.c = (CS)de->d_name, .len = strlen(de->d_name)}; 
//         
//         Arr(Byte) fiExtension = fileExtension(shortFName);
//         if (searchStringInCommaedList(fiExtension, filter.includedExtensions)) {
//            addFile(fileList, toFullFileName(shortFName, &fullPath), flags);
//            ++countMatches;
//         }
//      }
//   }
//   
//   deleteArena(a); 
//   return countMatches;
//}

// Return TRUE if "p" contains what looks like an environment variable. Allowing for escaping.
private int
hasEnvVar(CS p) {
   for ( ; *p; MB_PTR_ADV(p)) {
      if (*p == '\\' && p[1] != ZERO)
         ++p;
      ei (firstOccurrence((CS)"$",  *p) != NULL)
         return TRUE;
   }
   return FALSE;
}

#ifdef SPECIAL_WILDCHAR

// Return TRUE if "p" contains a special wildcard character, one that Eegl cannot expand, 
// requires using a shell.
private int
has_special_wildchar(CS p){
   for ( ; *p; MB_PTR_ADV(p)) {
      // Disallow line break characters.
      if (*p == '\r' || *p == '\n')
          break;
      // Allow for escaping.
      if (*p == '\\' && p[1] != ZERO && p[1] != '\r' && p[1] != '\n')
          ++p;
      ei (firstOccurrence((CS)SPECIAL_WILDCHAR, *p) != NULL) {
         // A { must be followed by a matching }.
         if (*p == '{' && firstOccurrence(p, '}') == NULL)
            continue;
         // A quote and backtick must be followed by another one.
         if ((*p == '`' || *p == '\'') && firstOccurrence(p, *p) == NULL)
            continue;
         return TRUE;
      }
   }
   return FALSE;
}

#endif

//Generic wildcard expansion code.
//
//Characters in "pat" that should not be expanded must be preceded with a backslash. E.g., 
//"/path\ with\ spaces/my\*star*"
//
//Return FAIL when no single file was found. In this case "num_file" is not set, and "file" may 
//contain an error message. Return OK when some files found. "num_file" is set to the number of
//matches, "file" to the array of matches.
int
gen_expand_wildcards(
   int num_pat,   // number of input patterns
   Arr(CS) pat,   // array of input patterns
   Unt flags,      // EW_* flags
   OUT ExpandMatch* matches
){
   ArrayList ga;
   CS p;
   static Boole recursive = false;
   int retval = OK;
   int did_expand_in_path = FALSE;
   CS path_option = curBook->o.path;

   //doExpandEnv() is called to expand things like "~user". If this fails,
   //it calls expandWildcard(), which brings us back here. In this case, always
   //call the machine specific expansion function, if possible.  Otherwise, return FAIL.
   if (recursive)
      return mch_expand_wildcards(num_pat, pat, flags, OUT matches);

   //If there are any special wildcard characters which we cannot handle here, call machine 
   //specific function for all the expansion. This avoids starting the shell for each argument
   //separately. For `=expr` do use the internal function.
   for (int i = 0; i < num_pat; i++) {
      if (has_special_wildchar(pat[i]) && !(eeBacktick(pat[i]) && pat[i][1] == '='))
         return mch_expand_wildcards(num_pat, pat, flags, OUT matches);
   }

   recursive = true;

   for (int i = 0; i < num_pat && !gotInterruptG; ++i) {
      int add_pat = -1;
      p = pat[i];

      if (eeBacktick(p)) {
         add_pat = expand_backtick(OUT matches, p, flags);
         if (add_pat == -1)
            retval = FAIL;
      } else {
         // First expand environment variables, "~/" and "~user/".
         if ((hasEnvVar(p) && !(flags & EW_NOTENV)) || *p == '~') {
            p = doExpandEnvInFilePaths(p, TRUE);
            if (p == NULL)
               p = pat[i];
            //If doExpandEnv() can't expand an environment variable, use the shell to do that. 
            //Discard previously found file names and start all over again.
            ei (hasEnvVar(p) || *p == '~') {
               eeglFree(p);
               ga_clear_strings(&ga);
               i = mch_expand_wildcards(num_pat, pat, flags|EW_KEEPDOLLAR, OUT matches);
               recursive = false;
               return i;
            }
         }

         //If there are wildcards or case-insensitive expansion is required: Expand file names 
         //and add each match to the list. If there is no match, and EW_NOTFOUND is given, add the
         //pattern. Otherwise: Add the file name if it exists or when EW_NOTFOUND is given.
         if (mch_has_exp_wildcard(p) || (flags & EW_ICASE)) {
            if ((flags & (EW_PATH | EW_CDPATH))
               && fiIsRelative(p)
               && !(p[0] == '.' && (p[1] == '/' || (p[1] == '.' && p[2] == '/')))
            ){
               // :find completion where 'path' is used. Recursiveness is OK here.
               recursive = false;
               add_pat = expand_in_path(OUT matches, p, flags);
               recursive = true;
               did_expand_in_path = TRUE;
            } else
               add_pat = mch_expandpath(OUT matches, p, flags);
         }
      }

      if (add_pat == -1 || (add_pat == 0 && (flags & EW_NOTFOUND))) {
         CS t = backslash_halve_save(p);

         // When EW_NOTFOUND is used, always add files and dirs. Makes "vim /" work.
         if (flags & EW_NOTFOUND)
            addFile(OUT matches, t, flags | EW_DIR | EW_FILE);
         else
            addFile(OUT matches, t, flags);

         if (t != p)
            eeglFree(t);
      }

      if (did_expand_in_path && matches->len > 0 && (flags & (EW_PATH | EW_CDPATH)) != 0)
         uniquefy_paths(OUT matches, p, path_option);
      if (p != pat[i])
         eeglFree(p);
   }

   // When returning FAIL the array must be freed here.
   if (retval == FAIL)
      ga_clear_strings(&ga);

   if (matches->len == 0) {
      matches->c[0] = _("no matches");
      if ((flags & EW_EMPTYOK) == 0) {
         recursive = false;
         return FAIL;
      } 
   } 

   recursive = false;
   return retval;
}

//Add a file to a file list.  Accepted flags:
//EW_DIR   add directories
//EW_FILE   add files
//EW_EXEC   add executable files
//EW_NOTFOUND   add even when it doesn't exist
//EW_ADDSLASH   add slash after directory name
//EW_ALLLINKS   add symlink also when the referred file does not exist
void
addFile(OUT ExpandMatch* matches, CS fName, Unt flags){
   FileStat   sb;

   //if the file/dir/link doesn't exist, may not add it
   if ((flags & EW_NOTFOUND) == 0 
         && ((flags & EW_ALLLINKS) != 0 ? LSTAT(fName, &sb) < 0 : mch_getperm(fName) < 0)
   )
      return;

   //if the file/dir contains illegal characters, don't add it
   if (eeStrpbrk(fName, (CS)FNAME_ILLEGAL) != NULL)
      return;

   Boole isdir = mch_isdir(fName);
   if ((isdir && !(flags & EW_DIR)) || (!isdir && !(flags & EW_FILE)))
      return;

   //If the file isn't executable, may not add it.  Do accept directories.
   //When invoked from expand_shellcmd() do not use $PATH.
   if (!isdir && (flags & EW_EXEC) && !mch_can_exe(fName, NULL, !(flags & EW_SHELLCMD)))
      return;

   CS p = allocateArray(STRLEN(fName) + 1 + isdir, Byte, matches->a);

   STRCPY(p, fName);
   //Append a slash or backslash after directory names if none is present.
   if (isdir && (flags & EW_ADDSLASH) != 0)
      add_pathsep(p);
   addExpandMatch(p, OUT matches);
}

//Compare path "p[]" to "q[]". If "maxlen" >= 0 compare "p[maxlen]" to "q[maxlen]"
//Return value like strcmp(p, q), but consider path separators.
int
pathcmp(CS p, CS q, int maxlen) {
   int i, j;
   Unt c1, c2;
   CS s = NULL;

   for (i = 0, j = 0; maxlen < 0 || (i < maxlen && j < maxlen);) {
      c1 = mb_ptr2char((CS)p + i);
      c2 = mb_ptr2char((CS)q + j);

      // End of "p": check if "q" also ends or just has a slash.
      if (c1 == ZERO) {
         if (c2 == ZERO)  // full match
            return 0;
         s = q;
         i = j;
         break;
      }

      // End of "q": check if "p" just has a slash.
      if (c2 == ZERO) {
         s = p;
         break;
      }

      if ( c1 != c2) {
         if (c1 == '/')
            return -1;
         if (c2 == '/')
            return 1;
         return c1 - c2;  // no match
      }

      i += utfCharLen((CS)p + i);
      j += utfCharLen((CS)q + j);
   }
   if (s == NULL) //"i" or "j" ran into "maxlen"
      return 0;

   c1 = mb_ptr2char((CS)s + i);
   c2 = mb_ptr2char((CS)s + i + utfCharLen((CS)s + i));
   //ignore a trailing slash, but not "//" or ":/"
   if (c2 == ZERO
       && i > 0
       && !after_pathsep((CS)s, (CS)s + i)
       && c1 == '/'
   )
      return 0;   //match with trailing slash
   if (s == q)
      return -1;  //no match
   return 1;
}

//TRUE if "name" is a full (absolute) path name or URL.
int
eeIsAbsName(CS name){
   return (path_with_url(name) != 0 || !fiIsRelative(name));
}

//Get absolute file name into "buf[len]". return FAIL for failure, OK for success
private int
mch_FullName(CS fname, OUT CS buf, int len, Boole force) {     // also expand when already absolute path
   int buflen = 0;
   int fd = -1;
   static int dont_fchdir = FALSE;   // TRUE when fchdir() doesn't work
   Byte olddir[MAXPATHL];
   CS p;
   int retval = OK;

   //Expand it if forced or not an absolute path.
   //Do not do it for "/file", the result is always "/".
   if ((force || fiIsRelative(fname)) && ((p = lastOccurrence(fname, '/')) == NULL || p != fname)) {
      if (!p && eq(fname, S".."))
         // Handle ".." without path separators.
         p = fname + 2;
      //If the file name has a path, change to that directory for a moment, and then get the 
      //directory (and get back to where we were).
      //This will get the correct path name with "../" things.
      if (p) {
         if (eq(p, S"/.."))
            //For "/path/dir/.." include the "/..".
            p += 3;

         //Use fchdir() if possible, it's said to be faster and more reliable.  
         if (!dont_fchdir) {
            fd = open(".", O_RDONLY | O_EXTRA, 0);
            if (fd >= 0 && fchdir(fd) < 0) {
               close(fd);
               fd = -1;
               dont_fchdir = TRUE;       // don't try again
            }
         }

         //Only change directory when we are sure we can return to where
         //we are now. After doing "su" chdir(".") might not work.
         if (fd < 0 
               && (mch_dirname(olddir, MAXPATHL) == FAIL || mch_chdir((char *)olddir) != 0)
         ){
            p = NULL;   // can't get current dir: don't chdir
            retval = FAIL;
         } else {
            //The directory is copied into buf[], to be able to remove
            //the file name without changing it (could be a string in read-only memory)
            if (p - fname >= len)
               retval = FAIL;
            else {
               copySubstrToAllocation(buf, (Text){fname, p - fname});
               if (mch_chdir((char *)buf)) {
                  //Path does not exist (yet). For a full path fail, will use the path as-is. 
                  //For a relative path use the current directory and append the file name.
                  if (!fiIsRelative(fname))
                     retval = FAIL;
                  else
                     p = NULL;
               } ei (*p == '/')
                  fname = p + 1;
               else
                  fname = p;
               *buf = ZERO;
            }
         }
      }
      if (mch_dirname(buf, len) == FAIL) {
         retval = FAIL;
         *buf = ZERO;
      }
      if (p) {
         int l;
         if (fd >= 0) {
            if (p_verbose >= 5) {
               verbose_enter();
               msg(S"fchdir() to previous dir");
               verbose_leave();
            }
            l = fchdir(fd);
         } else
            l = mch_chdir((char *)olddir);
         if (l != 0)
            emsg(_(e_cannot_go_back_to_previous_directory));
      }
      if (fd >= 0)
         close(fd);

      buflen = (int)STRLEN(buf);
      if (buflen >= len - 1)
         retval = FAIL; // no space for trailing "/"
      ei (buflen > 0 && buf[buflen - 1] != '/' && *fname != ZERO && STRCMP(fname, ".") != 0) {
         buf[buflen] = '/';
         buflen++;
      }
   }

   if (buflen == 0)
      buflen = (int)STRLEN(buf);

   // Catch file names which are too long.
   if (retval == FAIL || (int)(buflen + STRLEN(fname)) >= len)
      return FAIL;

   // Do not append ".", "/dir/." is equal to "/dir".
   if (STRCMP(fname, ".") != 0)
      STRCPY(buf + buflen, fname);

   return OK;
}


//Get absolute file name into buffer "buf[len]". Urls are copied as is, otherwise env vars expanded
//Return OK/FAIL
int
eeFullFileName(CS fname, OUT CS buf, int len, Boole force) { //force expansion even if absolute
   *buf = ZERO;

   int retval = OK;
   Boole url = path_with_url(fname); //is it of the "asdf://..." form?
   if (!url)
      retval = mch_FullName(fname, OUT buf, len, force);
   if (url || retval == FAIL) {
      // something failed; use the file name (truncate when too long)
      copySubstrToAllocation(OUT buf, (Text){fname, len - 1});
   }
   return retval;
}

//Get name of current directory into buffer "buf" of length "len" bytes.
//"len" must be at least PATH_MAX. Return OK for success, FAIL for failure.
int
mch_dirname(CS buf, int len) {
   if (getcwd((char *)buf, len) == NULL) {
      STRCPY(buf, strerror(errno));
      return FAIL;
   }
   return OK;
}

Boole
fiIsRelative(CS fname) {
   return (*fname != '/' && *fname != '~');
}

//}}}
//{{{ finding files

//File searching functions for 'path', 'tags' and 'cdpath' options.
//External visible functions:
//eeFindFile_init()      creates/initializes the search context
//findfileFreeVisitedList()   free list of visited files/dirs of search context
//eeFindFile()      find a file in the search context
//eeFindFile_cleanup()   cleanup/free search context created by eeFindFile_init()
//
//All private functions and variables start with 'ff_'
//
//In general it works like this:
//First you create yourself a search context by calling eeFindFile_init(). It is possible to give 
//a search context from a previous call to eeFindFile_init(), so it can be reused. After this you 
//call eeFindFile() until you are satisfied with the result or it returns NULL. On every call it
//returns the next file which matches the conditions given to eeFindFile_init(). If it doesn't 
//find a next file it returns NULL.
//
//It is possible to call eeFindFile_init() again to reinitialise your search with some new 
//parameters. Don't forget to pass your old search context to it, so it can reuse it and 
//especially reuse the list of already visited directories. If you want to delete the list of 
//already visited directories simply call findfileFreeVisitedList().
//
//When you are done call eeFindFile_cleanup() to free the search context.
//
//The function eeFindFile_init() has a long comment, which describes the needed parameters.
//
//
//ATTENTION:
//==========
//  Also we use an allocated search context here, these functions are NOT thread-safe!

// type for the directory search stack
declStruct (DirSearchStack);
struct DirSearchStack {
   DirSearchStack* ffs_prev;

   // the fixed part (no wildcards) and the part containing the wildcards of the search path
   Text fixedPathPart;
   Text wildcardPathPart;

   // files/dirs found in the above directory, matched by the first wildcard of wc_part
   ExpandMatch files;
   int ffs_filearray_cur;   // needed for partly handled dirs

   // to store status of partly handled directories
   // 0: we work on this directory for the first time
   // 1: this directory was partly searched in an earlier step
   int stage;

   // How deep are we in the directory tree?
   // Counts backward from value of level parameter to eeFindFile_init
   int depth;

   // Did we already expand '**' to an empty string?
   Boole didExpandStarStar;
};

//type for already visited directories or files.
typedef struct Visited {
   struct Visited* next;

   // Visited directories are different if the wildcard string are
   // different. So we have to save it.
   CS wildcardPath;

   // for unix use inode etc for comparison (needed because of links), else use filename.
   int areDevInoValid;   // deviceId and inodeId were set
   dev_t deviceId;   // device number
   ino_t inodeId;   // inode number
   // The memory for this struct is allocated according to the length of ffv_fname.
   Byte ffv_fname[1];   // actually longer
} Visited;

//We might have to manage several visited lists during a search.
//This is especially needed for the tags option. If tags is set to:
//     "./++/tags,./++/TAGS,++/tags"  (replace + with *)
//So we have to do 3 searches:
//  1) search from the current files directory downward for the file "tags"
//  2) search from the current files directory downward for the file "TAGS"
//  3) search from Eegl's current directory downwards for the file "tags"
//As you can see, the first and the third search are for the same file, so for
//the third search we can use the visited list of the first search. For the
//second search we must start from a empty visited list.
//The struct ff_visited_list_hdr is used to manage a linked list of already visited lists.
declStruct(VisitedList);
struct VisitedList {
   VisitedList* next;

   // the filename the attached visited list is for
   CS filename;
   Visited* ffvl_visited_list;
};


//'**' can be expanded to several directory levels.
//Set the default maximum depth.
#define FF_MAX_STAR_STAR_EXPAND ((Byte)30)

//The search context:
//  stack:   the stack for the dirs to search
//  visitedList: the currently active visited list
//  dirVisitedList: the currently active visited list for search dirs
//  visitedLists: the list of all visited lists
//  allVisitedLists: the list of all visited lists for search dirs
//  needle:     the file to search for
//  startDir:   the starting directory, if search path was relative
//  fixPath:   the fix part of the given path (without wildcards)
//        Needed for upward search.
//  wildcardPath:   the part of the given path containing wildcards
//  maxRecursion:   how many levels of dirs to search downwards
//  stopDirs:   array of stop directories for upward search
//  whatToFind:   FINDFILE_BOTH, FINDFILE_DIR or FINDFILE_FILE
//  tagFile:   searching for tags file, don't use @suffixesadd
typedef struct FileSearchCtx {
   DirSearchStack* stack;
   VisitedList* visitedList;
   VisitedList* dirVisitedList;
   VisitedList* visitedLists;
   VisitedList* allVisitedLists;
   Text needle;
   Text startDir;
   Text fixPath;
   Text wildcardPath;
   int maxRecursion;
   Arr(Text) stopDirs;
   int whatToFind;
   Boole tagFile;
} FileSearchCtx;

// locally needed functions
private int checkFirstTimeVisit(Visited **, Text, CS, Unt);
private void findfileFreeVisitedList(FileSearchCtx* search_ctx_arg);
private void findfileFreeVisitedList_list(OUT VisitedList **listheadp);
private void ff_free_visited_list(Visited *vl);
private VisitedList* ff_get_visited_list(Text, OUT VisitedList **);

private void ff_push(FileSearchCtx *searchCtx, DirSearchStack *stack_ptr);
private DirSearchStack *ff_pop(FileSearchCtx *searchCtx);
private void ff_clear(FileSearchCtx *searchCtx);
private void ff_free_stack_element(DirSearchStack *stack_ptr);
private DirSearchStack*ff_create_stack_element(CS, Unt, CS, Unt, int, Boole);
private int ff_path_in_stoplist(CS, int, Text *);


private Text fileExpansionS = {NULL, 0};       // used for expanding filenames

#if 0
//if someone likes findfirst/findnext, here are the functions NOT TESTED!!

private void *ff_fn_search_context = NULL;

Byte *
eeFindfirst(Byte *path, Byte *filename, int level) {
    ff_fn_search_context =
   eeFindFile_init(path, filename, NULL, level, TRUE, FALSE,
      ff_fn_search_context, rel_fname);
    if (NULL == ff_fn_search_context)
   return NULL;
    else
   return eeFindnext()
}

CS
eeFindnext(void) {
    Byte *ret = eeFindFile(ff_fn_search_context);

    if (NULL == ret) {
   eeFindFile_cleanup(ff_fn_search_context);
   ff_fn_search_context = NULL;
    }
    return ret;
}
#endif

//Initialization routine for eeFindFile().
//
//Return the newly allocated search context or NULL if an error occurred.
//
//Don't forget to clean up by calling eeFindFile_cleanup() if you are done with the search context
//
//Find the file 'filename' in the directory 'path'.
//The parameter 'path' may contain wildcards. If so only search 'level'
//directories deep. The parameter 'level' is the absolute maximum and is
//not related to restricts given to the '**' wildcard. If 'level' is 100
//and you use '**200' eeFindFile() will stop after 100 levels.
//
//'filename' cannot contain wildcards! It is used as-is, no backslashes to
//escape special characters.
//
//If 'stopdirs' is not NULL and nothing is found downward, the search is
//restarted on the next higher directory level. This is repeated until the
//start-directory of a search is contained in 'stopdirs'. 'stopdirs' has the
//format ";*<dirname>*\(;<dirname>\)*;\=$".
//
//If the 'path' is relative, the starting dir for the search is either Eegl's
//current dir or if the path starts with "./" the current files dir.
//If the 'path' is absolute, the starting dir is that part of the path before the first wildcard.
//
//Upward search is only done on the starting dir.
//
//If 'free_visited' is TRUE, the list of already visited files/directories is cleared. Set this to 
//FALSE if you just want to search from another directory, but want to be sure that no directory 
//from a previous search is searched again. This is useful if you search for a file at different 
//places. The list of visited files/dirs can also be cleared with the function
//findfileFreeVisitedList().
//
//Set the parameter 'find_what' to FINDFILE_DIR if you want to search for
//directories only, FINDFILE_FILE for files only, FINDFILE_BOTH for both.
//
//A search context returned by a previous call to eeFindFile_init() can be
//passed in the parameter "search_ctx_arg".  This context is reused and
//reinitialized with the new parameters.  The list of already visited
//directories from this context is only deleted if the parameter
//"free_visited" is true.  Be aware that the passed "search_ctx_arg" is freed
//if the reinitialization fails.
//
//If you don't have a search context from a previous call, "search_ctx_arg" must be NULL.
//
//This function silently ignores a few errors, eeFindFile() will have limited functionality then.
FileSearchCtx*
eeFindFile_init(
   CS path,
   Text filename,
   CS stopdirs,
   int level,
   Boole free_visited,
   Unt find_what, // FINDFILE_DIR, FINDFILE_FILE or FINDFILE_BOTH for both.
   NULLABLE OUT FileSearchCtx* search_ctx_arg,
   Boole tagfile,   // expanding names of tags files
   CS rel_fname   // file name to use for "."
){
   FileSearchCtx* searchCtx;
   int add_sep;

   // If a search context is given by the caller, reuse it, else allocate a new one.
   if (search_ctx_arg)
      searchCtx = search_ctx_arg;
   else {
      searchCtx = ALLOC_CLEAR_ONE(FileSearchCtx);
   }
   searchCtx->whatToFind = find_what;
   searchCtx->tagFile = tagfile;

   // clear the search context, but NOT the visited lists
   ff_clear(searchCtx);

   // clear visited list if wanted
   if (free_visited == TRUE)
      findfileFreeVisitedList(searchCtx);
   else {
      // Reuse old visited lists. Get the visited list for the given
      // filename. If no list for the current filename exists, creates a new one.
      searchCtx->visitedList = ff_get_visited_list(filename, OUT &searchCtx->visitedLists);
      if (!searchCtx->visitedList)
         goto error_return;
      searchCtx->dirVisitedList = ff_get_visited_list(filename, OUT &searchCtx->allVisitedLists);
      if (!searchCtx->dirVisitedList)
         goto error_return;
   }

   if (!fileExpansionS.c) {
      fileExpansionS.len = 0;
      fileExpansionS.c = alloc(MAXPATHL);
   }

   //Store information on starting dir now if path is relative. If absolute, we do that later
   if (path[0] == '.' && (path[1] == '/' || path[1] == ZERO) && rel_fname != NULL) {
      int   len = (int)(fiGetShortFiName(rel_fname) - rel_fname);

      if (!eeIsAbsName(rel_fname) && len + 1 < MAXPATHL) {
         // Make the start dir an absolute path name.
         copySubstrToAllocation(fileExpansionS.c, (Text){rel_fname, len});
         fileExpansionS.len = len;

         searchCtx->startDir.c = fiExpandAndCopy(fileExpansionS.c, FALSE);
         if (searchCtx->startDir.c == NULL)
            goto error_return;
         searchCtx->startDir.len = STRLEN(searchCtx->startDir.c);
      } else {
         searchCtx->startDir.len = len;
         searchCtx->startDir.c = copySubstr(rel_fname, searchCtx->startDir.len);
         if (searchCtx->startDir.c == NULL)
            goto error_return;
      }

      if (*++path != ZERO)
         ++path;
   } ei (*path == ZERO || !eeIsAbsName(path)) {
      if (mch_dirname(fileExpansionS.c, MAXPATHL) == FAIL)
         goto error_return;

      fileExpansionS.len = STRLEN(fileExpansionS.c);

      searchCtx->startDir.len = fileExpansionS.len;
      searchCtx->startDir.c = copySubstr(fileExpansionS.c, searchCtx->startDir.len);
      if (searchCtx->startDir.c == NULL)
         goto error_return;
   }

   //If stopdirs are given, split them into an array of pointers.
   //If this fails (mem allocation), there is no upward search at all or a
   //stop directory is not recognized -> continue silently.
   //If stopdirs just contains a ";" or is empty,
   //searchCtx->stopDirs will only contain a  NULL pointer. This is handled as unlimited upward 
   //search. See function ff_path_in_stoplist() for details.
   if (stopdirs) {
      CS walker = stopdirs;
      while (*walker == ';')
         walker++;

      int dircount = 1;
      searchCtx->stopDirs = ALLOC_ONE(Text);

      Text* tmp;         // for convenience
      do {
         CS helper = walker;
         Arr(Text) ptr = eeRealloc(searchCtx->stopDirs, (dircount + 1) * sizeof(Text));
         searchCtx->stopDirs = ptr;
         walker = firstOccurrence(walker, ';');
         Unt len = walker ? (Unt)(walker - helper) : STRLEN(helper);
         // "" means ascent till top of directory tree.

         if (*helper != ZERO && !eeIsAbsName(helper) && len + 1 < MAXPATHL) {
            // Make the stop dir an absolute path name.
            copySubstrToAllocation(fileExpansionS.c, (Text){helper, len});
            fileExpansionS.len = len;

            tmp = &searchCtx->stopDirs[dircount - 1];
            tmp->c = fiExpandAndCopy(fileExpansionS.c, FALSE);
            tmp->len = tmp->c ? STRLEN(tmp->c) : 0;
         } else {
            tmp = &searchCtx->stopDirs[dircount - 1];
            tmp->len = len;
            tmp->c = copySubstr(helper, tmp->len);
         }
         if (walker)
             walker++;
         dircount++;

       } while (walker != NULL);

       tmp = &searchCtx->stopDirs[dircount - 1];
       tmp->c = NULL;
       tmp->len = 0;
   }

   searchCtx->maxRecursion = level;

   //split into:
   // -fix path
   // -wildcard_stuff (might be NULL)
   CS wc_part = firstOccurrence(path, '*');
   if (wc_part) {
      int   llevel;

      // save the fix part of the path
      searchCtx->fixPath.len = (Unt)(wc_part - path);
      searchCtx->fixPath.c = copySubstr(path, searchCtx->fixPath.len);
      if (searchCtx->fixPath.c == NULL)
          goto error_return;

      //copy wc_path and add restricts to the '**' wildcard.
      //The octet after a '**' is used as a (binary) counter.
      //So '**3' is transposed to '**^C' ('^C' is ASCII value 3)
      //or '**76' is transposed to '**N'( 'N' is ASCII value 76).
      //If no restrict is given after '**' the default is used.
      //Due to this technique the path looks awful if you print it as a string.
      fileExpansionS.len = 0;
      while (*wc_part != ZERO) {
         if (fileExpansionS.len + 5 >= MAXPATHL) {
            emsg(_(e_path_too_long_for_completion));
            break;
         }
         if (STRNCMP(wc_part, "**", 2) == 0) {
            fileExpansionS.c[fileExpansionS.len++] = *wc_part++;
            fileExpansionS.c[fileExpansionS.len++] = *wc_part++;

            CS errpt;
            llevel = STRTOL(wc_part, &errpt, 10);
            if (errpt != wc_part && llevel > 0 && llevel < 255)
                fileExpansionS.c[fileExpansionS.len++] = llevel;
            ei (errpt != wc_part && llevel == 0)
                // restrict is 0 -> remove already added '**'
                fileExpansionS.len -= 2;
            else
                fileExpansionS.c[fileExpansionS.len++] = FF_MAX_STAR_STAR_EXPAND;
            wc_part = errpt;
            if (*wc_part != ZERO && *wc_part != '/') {
               showErrFmtMsg(_(e_invalid_path_number_must_be_at_end_of_path_or_be_followed_by_str), "/");
               goto error_return;
            }
         } else
            fileExpansionS.c[fileExpansionS.len++] = *wc_part++;
      }
      fileExpansionS.c[fileExpansionS.len] = ZERO;

      searchCtx->wildcardPath.len = fileExpansionS.len;
      searchCtx->wildcardPath.c = copySubstr(fileExpansionS.c, searchCtx->wildcardPath.len);
      if (searchCtx->wildcardPath.c == NULL)
          goto error_return;
    } else {
      searchCtx->fixPath.len = STRLEN(path);
      searchCtx->fixPath.c = copySubstr(path, searchCtx->fixPath.len);
   }

   if (searchCtx->startDir.c == NULL) {
      // store the fix part as startdir.
      // This is needed if the parameter path is fully qualified.
      searchCtx->startDir.len = searchCtx->fixPath.len;
      searchCtx->startDir.c = copySubstr(searchCtx->fixPath.c, searchCtx->startDir.len);
      searchCtx->fixPath.c[0] = ZERO;
      searchCtx->fixPath.len = 0;
   }

   // create an absolute path
   if (searchCtx->startDir.len + searchCtx->fixPath.len + 3 >= MAXPATHL) {
      emsg(_(e_path_too_long_for_completion));
      goto error_return;
   }

   add_sep = !after_pathsep(
         searchCtx->startDir.c, searchCtx->startDir.c + searchCtx->startDir.len
   );
   fileExpansionS.len = eeSnprintf(
       fileExpansionS.c,
       MAXPATHL,
       "%s%s",
       searchCtx->startDir.c,
       add_sep ? "/" : "");

   {
   Unt bufsize = fileExpansionS.len + searchCtx->fixPath.len + 1;
   CS buf = alloc(bufsize);

   eeSnprintf(
      buf,
      bufsize,
      "%s%s",
      fileExpansionS.c,
      searchCtx->fixPath.c);
   if (mch_isdir(buf)) {
      if (searchCtx->fixPath.len > 0) {
         add_sep = !after_pathsep(searchCtx->fixPath.c,
             searchCtx->fixPath.c + searchCtx->fixPath.len);
         fileExpansionS.len += eeSnprintf(
            fileExpansionS.c + fileExpansionS.len,
            MAXPATHL - fileExpansionS.len,
            "%s%s",
            searchCtx->fixPath.c,
            add_sep ? "/" : "");
       }
   } else {
      CS p = fiGetShortFiName(searchCtx->fixPath.c);
      int len = (int)searchCtx->fixPath.len;

      if (p > searchCtx->fixPath.c) {
         // do not add '..' to the path and start upwards searching
         len = (int)(p - searchCtx->fixPath.c) - 1;
         if ((len >= 2
            && STRNCMP(searchCtx->fixPath.c, "..", 2) == 0)
            && (len == 2 || searchCtx->fixPath.c[2] == '/'))
         {
             eeglFree(buf);
             goto error_return;
         }

         add_sep = !after_pathsep(searchCtx->fixPath.c,
             searchCtx->fixPath.c + searchCtx->fixPath.len);
         fileExpansionS.len += eeSnprintf(
            fileExpansionS.c + fileExpansionS.len,
            MAXPATHL - fileExpansionS.len,
            "%.*s%s",
            len,
            searchCtx->fixPath.c,
            add_sep ? "/" : "");
       }

      if (searchCtx->wildcardPath.c != NULL) {
         Unt   tempsize = (searchCtx->fixPath.len - len)
               + searchCtx->wildcardPath.len
               + 1;
         CS temp = alloc(tempsize);

         searchCtx->wildcardPath.len = eeSnprintf(
                temp,
                tempsize,
                "%s%s",
                searchCtx->fixPath.c + len,
                searchCtx->wildcardPath.c);
         eeglFree(searchCtx->wildcardPath.c);
         searchCtx->wildcardPath.c = temp;
       }
   }
   eeglFree(buf);
   }

   DirSearchStack* sptr = ff_create_stack_element(fileExpansionS.c,
         fileExpansionS.len,
         searchCtx->wildcardPath.c,
         searchCtx->wildcardPath.len,
         level,
         false 
   );

   if (!sptr)
      goto error_return;

   ff_push(searchCtx, sptr);

   searchCtx->needle.len = filename.len;
   searchCtx->needle.c = copySubstr(filename.c, searchCtx->needle.len);

   return searchCtx;

error_return:
   //We clear the search context now! Even when the caller gave us a (perhaps valid) context, we 
   //free it here, as we might have already destroyed it.
   eeFindFile_cleanup(searchCtx);
   return NULL;
}

//Change directory to "new_dir". Search @cdpath for relative directory names.
int
eeChdir(CS new_dir) {
   CS file_to_find = NULL;
   FileSearchCtx* searchCtx = NULL;

   CS dir_name = find_directory_in_path(
      mbText(new_dir), FNAME_MESS, curBook->fullFileName, OUT &file_to_find, OUT &searchCtx
   );
   eeglFree(file_to_find);
   eeFindFile_cleanup(searchCtx);
   if (!dir_name)
      return -1;
   int r = mch_chdir((char *)dir_name);
   eeglFree(dir_name);
   return r;
}

//Change to a file's directory. Caller must call shorten_fnames()! Return OK or FAIL.
int
eeChdirfile(CS fname, char *trigger_autocmd) {
   Byte old_dir[MAXPATHL];
   Byte new_dir[MAXPATHL];

   if (mch_dirname(old_dir, MAXPATHL) != OK)
      *old_dir = ZERO;

    copySubstrToAllocation(new_dir, (Text){fname, MAXPATHL - 1});
    *gettail_sep(new_dir) = ZERO;

   if (pathcmp(old_dir, new_dir, -1) == 0)
      // nothing to do
      return OK;

   if (trigger_autocmd != NULL)
      trigger_DirChangedPre((CS)trigger_autocmd, new_dir);

   if (mch_chdir((char *)new_dir) != 0)
      return FAIL;

   if (trigger_autocmd != NULL)
      applyAutocomms(EVENT_DIRCHANGED, (CS)trigger_autocmd, new_dir, FALSE, curBook);
   return OK;
}

//Get the stopdir string.  Check that ';' is not escaped.
CS
eeFindFile_stopdir(CS buf) {
   CS r_ptr = buf;
   CS r_ptr_end = NULL;       // points to ZERO at end of string "r_ptr"

   while (*r_ptr != ZERO && *r_ptr != ';') {
      if (r_ptr[0] == '\\' && r_ptr[1] == ';') {
         // Overwrite the escape char,
         // use STRLEN(r_ptr) to move the trailing '\0'.
         if (r_ptr_end == NULL)
            r_ptr_end = r_ptr + STRLEN(r_ptr);
         mch_memmove(r_ptr, r_ptr + 1,
        (Unt)(r_ptr_end - (r_ptr + 1)) + 1);   // +1 for ZERO
         r_ptr++;
         --r_ptr_end;
      }
      r_ptr++;
   }
   if (*r_ptr == ';') {
      *r_ptr = ZERO;
      r_ptr++;
   } ei (*r_ptr == ZERO)
      r_ptr = NULL;
   return r_ptr;
}

//Clean up the given search context. Can handle a NULL pointer.
void
eeFindFile_cleanup(FileSearchCtx* ctx) {
   if (!ctx)
      return;

   findfileFreeVisitedList(ctx);
   ff_clear(ctx);
   eeglFree(ctx);
}

//Find a file in a search context. The search context was created with eeFindFile_init() above.
//Return a pointer to an allocated file name or NULL if nothing found. To get all matching files,
//call this function until you get NULL.
//If the passed search_context is NULL, NULL is returned.
//The search algorithm is depth first. To change this replace the stack with a list (don't forget 
//to leave partly searched directories on the top of the list).
CS
eeFindFile(FileSearchCtx* search_ctx_arg) {
   Text   rest_of_wildcards;
   DirSearchStack   *stackp;

   if (!search_ctx_arg)
      return NULL;

   FileSearchCtx* searchCtx = search_ctx_arg;

   //filepath is used as buffer for various actions and as the storage to return a found filename.
   Byte filePathBuilder[MAXPATHL];
   Text filePath;
   filePath.c = filePathBuilder;

   // store the end of the start dir -- needed for upward search
   
   CS path_end = (searchCtx->startDir.c) ? &searchCtx->startDir.c[searchCtx->startDir.len] : null;

   // upward search loop
   for (;;) {
      // downward search loop
      for (;;) {
          // check if user wants to stop the search
          ui_breakcheck();
          if (gotInterruptG)
         break;

         // get directory to work on from stack
         stackp = ff_pop(searchCtx);
         if (!stackp)
            break;

          //TODO: decide if we leave this test in
          //
          //GOOD: don't search a directory(-tree) twice.
          //BAD:  - check linked list for every new directory entered.
          //      - check for double files also done below
          //
          //Here we check if we already searched this directory.
          //We already searched a directory if:
          //1) The directory is the same.
          //2) We would use the same wildcard string.
          //
          //Good if you have links on same directory via several ways
          // or you have selfreferences in directories (e.g. SuSE Linux 6.3:
          // /etc/rc.d/init.d is linked to /etc/rc.d -> endless loop)
          //
          //This check is only needed for directories we work on for the
          //first time (hence stackp->ff_filearray == NULL)
          if (!(stackp->files.c)
             && checkFirstTimeVisit(&searchCtx->dirVisitedList ->ffvl_visited_list,
                stackp->fixedPathPart,
                stackp->wildcardPathPart.c,
                stackp->wildcardPathPart.len) == FAIL)
          {
#ifdef FF_VERBOSE
         if (p_verbose >= 5) {
             verbose_enter_scroll();
             smsg("Already Searched: %s (%s)", stackp->fixedPathPart.c, stackp->wildcardPathPart.c);
             // don't overwrite this either
             msg_puts("\n");
             verbose_leave_scroll();
         }
#endif
         ff_free_stack_element(stackp);
         continue;
          }
#ifdef FF_VERBOSE
          ei (p_verbose >= 5) {
         verbose_enter_scroll();
         smsg("Searching: %s (%s)", stackp->fixedPathPart.c, stackp->wildcardPathPart.c);
         // don't overwrite this either
         msg_puts("\n");
         verbose_leave_scroll();
          }
#endif

         // check depth
         if (stackp->depth <= 0) {
            ff_free_stack_element(stackp);
            continue;
         }

         filePath.c[0] = ZERO;
         filePath.len = 0;

         //If no filearray till now expand wildcards
         //The function expand_wildcards() can handle an array of paths
         //and all possible expands are returned in one array. We use this
         //to handle the expansion of '**' into an empty string.
         if (!(stackp->files.c)) {
            CS dirptrs[2];

            // we use filepath to build the path expand_wildcards() should
            // expand.
            dirptrs[0] = filePath.c;
            dirptrs[1] = NULL;

            // if we have a start dir copy it in
            if (!eeIsAbsName(stackp->fixedPathPart.c) && searchCtx->startDir.c) {
               if (searchCtx->startDir.len + 1 < MAXPATHL) {
                  int add_sep = !after_pathsep(searchCtx->startDir.c,
                      searchCtx->startDir.c + searchCtx->startDir.len);
                  filePath.len = eeSnprintf(
                      filePath.c,
                      MAXPATHL,
                      "%s%s",
                      searchCtx->startDir.c,
                      add_sep ? "/" : "");
               } else {
                  ff_free_stack_element(stackp);
                  goto fail;
               }
            }

            // append the fix part of the search path
            if (filePath.len + stackp->fixedPathPart.len + 1 < MAXPATHL) {
               int add_sep = !after_pathsep(stackp->fixedPathPart.c,
                  stackp->fixedPathPart.c + stackp->fixedPathPart.len);
               filePath.len += eeSnprintf(
                  filePath.c + filePath.len,
                  MAXPATHL - filePath.len,
                  "%s%s",
                  stackp->fixedPathPart.c,
                  add_sep ? "/" : ""
               );
            } else {
               ff_free_stack_element(stackp);
               goto fail;
            }

            rest_of_wildcards.c = stackp->wildcardPathPart.c;
            rest_of_wildcards.len = stackp->wildcardPathPart.len;
            if (*rest_of_wildcards.c != ZERO) {
                if (STRNCMP(rest_of_wildcards.c, "**", 2) == 0) {
                  // pointer to the restrict byte. The restrict byte is not a character!
                  CS p = rest_of_wildcards.c + 2;

                  if (*p > 0) {
                     (*p)--;
                     if (filePath.len + 1 < MAXPATHL)
                        filePath.c[filePath.len++] = '*';
                     else {
                        ff_free_stack_element(stackp);
                        goto fail;
                     }
                  }

                  if (*p == 0) {
                      // remove '**<numb> from wildcards
                      mch_memmove(rest_of_wildcards.c,
                        rest_of_wildcards.c + 3,
                        (Unt)(rest_of_wildcards.len - 3) + 1);    // +1 for ZERO
                      rest_of_wildcards.len -= 3;
                      stackp->wildcardPathPart.len = rest_of_wildcards.len;
                  } else {
                      rest_of_wildcards.c += 3;
                      rest_of_wildcards.len -= 3;
                  }

                  if (!stackp->didExpandStarStar) {
                      // if not done before, expand '**' to empty
                      stackp->didExpandStarStar = true;
                      dirptrs[1] = stackp->fixedPathPart.c;
                  }
               }

               //Here we copy until the next path separator or the end of
               //the path. If we stop at a path separator, there is
               //still something else left. This is handled below by
               //pushing every directory returned from expand_wildcards()
               //on the stack again for further search.
               while (*rest_of_wildcards.c && *rest_of_wildcards.c != '/') {
                  if (filePath.len + 1 < MAXPATHL) {
                     filePath.c[filePath.len++] = *rest_of_wildcards.c++;
                     --rest_of_wildcards.len;
                  } else {
                      ff_free_stack_element(stackp);
                      goto fail;
                  }
               }

               filePath.c[filePath.len] = ZERO;
               if (*rest_of_wildcards.c == '/') {
                  rest_of_wildcards.c++;
                  rest_of_wildcards.len--;
               }
            }

            //Expand wildcards like "*" and "$VAR". If the path is a URL don't try this.
            if (path_with_url(dirptrs[0])) {
               stackp->files.c = ALLOC_ONE(CS);
               if (stackp->files.c
                     && (stackp->files.c[0] = copySubstr(dirptrs[0], filePath.len)) != NULL)
                  stackp->files.len = 1;
               else
                  stackp->files.len = 0;
            } else
                //Add EW_NOTWILD because the expanded path may contain wildcard characters that are
                //to be taken literally. This is a bit of a hack.
                expand_wildcards((dirptrs[1] == NULL) ? 1 : 2, dirptrs,
                   EW_DIR|EW_ADDSLASH|EW_SILENT|EW_NOTWILD,
                   OUT &stackp->files
               );

            stackp->ffs_filearray_cur = 0;
            stackp->stage = 0;
          } else {
            rest_of_wildcards.c = &stackp->wildcardPathPart.c[stackp->wildcardPathPart.len];
            rest_of_wildcards.len = 0;
          }

         if (stackp->stage == 0) {
            // this is the first time we work on this directory
            if (*rest_of_wildcards.c == ZERO) {
               CS suf;

               //We don't have any wildcards to expand, so we have to check for the final file now
               for (Unt i = stackp->ffs_filearray_cur; i < stackp->files.len; ++i) {
                  if (!path_with_url(stackp->files.c[i]) && !mch_isdir(stackp->files.c[i]))
                      continue;   // not a directory

                  // prepare the filename to be checked for existence below
                  Unt len = STRLEN(stackp->files.c[i]);
                  if (len + 1 + searchCtx->needle.len < MAXPATHL) {
                     int add_sep = !after_pathsep(stackp->files.c[i], stackp->files.c[i] + len);
                     filePath.len = eeSnprintf(
                         filePath.c,
                         MAXPATHL,
                         "%s%s%s",
                         stackp->files.c[i],
                         add_sep ? "/" : "",
                         searchCtx->needle.c
                     );
                  } else {
                      ff_free_stack_element(stackp);
                      goto fail;
                  }

                  //Try without extra suffix and then with suffixes from @suffixesadd.
                  len = filePath.len;
                  if (searchCtx->tagFile || !curBook->o.suffixesAdd)
                     suf = Em;
                  else
                     suf = curBook->o.suffixesAdd;
                  for (;;) {
                      // if file exists and we didn't already find it
                      if ((path_with_url(filePath.c)
                          || (mch_getperm(filePath.c) >= 0
                              && (searchCtx->whatToFind == FINDFILE_BOTH
                                   || ((searchCtx->whatToFind == FINDFILE_DIR)
                                       == mch_isdir(filePath.c))
                                 )
                              )
                          )
#ifndef FF_VERBOSE
                         && (checkFirstTimeVisit(
                            &searchCtx->visitedList ->ffvl_visited_list,
                            filePath,
                            S"", 0) == OK)
#endif
                     ) {
#ifdef FF_VERBOSE
                        if (checkFirstTimeVisit(
                               &searchCtx->visitedList->ffvl_visited_list,
                                 filePath,
                                 (CS)"", 0
                            ) == FAIL
                        ) {
                           if (p_verbose >= 5) {
                              verbose_enter_scroll();
                              smsg("Already: %s", filePath.c);
                              // don't overwrite this either
                              msg_puts("\n");
                              verbose_leave_scroll();
                           }
                           continue;
                        }
#endif

                        // push dir to examine rest of subdirs later
                        stackp->ffs_filearray_cur = i + 1;
                        ff_push(searchCtx, stackp);

                        if (!path_with_url(filePath.c))
                           filePath.len = simplify_filename(filePath.c);

                        if (mch_dirname(fileExpansionS.c, MAXPATHL) == OK) {
                           fileExpansionS.len = STRLEN(fileExpansionS.c);
                           CS p = shorten_fname(filePath.c, fileExpansionS.c);
                           if (p) {
                              mch_memmove(filePath.c, p,
                                  (Unt)((filePath.c + filePath.len) - p) + 1);  // +1 for ZERO
                              filePath.len -= (p - filePath.c);
                           }
                        }
#ifdef FF_VERBOSE
                        if (p_verbose >= 5) {
                           verbose_enter_scroll();
                           smsg("HIT: %s", filePath.c);
                           // don't overwrite this either
                           msg_puts("\n");
                           verbose_leave_scroll();
                        }
#endif
                        return filePath.c;
                     }

                     // Not found or found already, try next suffix.
                     if (*suf == ZERO)
                        break;
                     filePath.len = len 
                        + copy_option_part(&suf, filePath.c + len, (int)(MAXPATHL - len), ",");
                  }
               }
            } else {
               //still wildcards left, push the directories for further search
               for (Unt i = stackp->ffs_filearray_cur; i < stackp->files.len; ++i) { 
                  if (!mch_isdir(stackp->files.c[i]))
                     continue;   // not a directory

                  ff_push(searchCtx,
                     ff_create_stack_element(
                          stackp->files.c[i],
                          STRLEN(stackp->files.c[i]),
                          rest_of_wildcards.c,
                          rest_of_wildcards.len,
                          stackp->depth - 1, false
                     )
                  );
               }
            }
            stackp->ffs_filearray_cur = 0;
            stackp->stage = 1;
         }

         //if wildcards contains '**' we have to descent till we reach the
         //leaves of the directory tree.
         if (STRNCMP(stackp->wildcardPathPart.c, "**", 2) == 0) {
            for (Unt i = stackp->ffs_filearray_cur; i < stackp->files.len; ++i) {
               if (fnamecmp(stackp->files.c[i], stackp->fixedPathPart.c) == 0)
                  continue; // don't repush same directory
               if (!mch_isdir(stackp->files.c[i]))
                  continue;   // not a directory
               ff_push(searchCtx,
                  ff_create_stack_element(
                     stackp->files.c[i],
                     STRLEN(stackp->files.c[i]),
                     stackp->wildcardPathPart.c,
                     stackp->wildcardPathPart.len,
                     stackp->depth - 1, true
                  )
               );
            }
         }

         // we are done with the current directory
         ff_free_stack_element(stackp);
      }

      // If we reached this, we didn't find anything downwards.
      // Let's check if we should do an upward search.
      if (searchCtx->startDir.c && searchCtx->stopDirs != NULL && !gotInterruptG) {
          DirSearchStack  *sptr;
          // path_end may point to the ZERO or the previous path separator
          int plen = (path_end - searchCtx->startDir.c) + (*path_end != ZERO);

          // is the last starting directory in the stop list?
          if (ff_path_in_stoplist(searchCtx->startDir.c, plen, searchCtx->stopDirs) == TRUE)
         break;

         // cut of last dir
         while (path_end > searchCtx->startDir.c && *path_end == '/')
            path_end--;
         while (path_end > searchCtx->startDir.c && path_end[-1] != '/')
            path_end--;
         *path_end = ZERO;

         // we may have shortened searchCtx->startDir, so update it's length
         searchCtx->startDir.len = (Unt)(path_end - searchCtx->startDir.c);
         path_end--;

         if (*searchCtx->startDir.c == ZERO)
            break;

         if (searchCtx->startDir.len + 1 + searchCtx->fixPath.len < MAXPATHL) {
            int add_sep = !after_pathsep(searchCtx->startDir.c,
                   searchCtx->startDir.c + searchCtx->startDir.len);
            filePath.len = eeSnprintf(
               filePath.c,
               MAXPATHL,
               "%s%s%s",
               searchCtx->startDir.c,
               add_sep ? "/" : "",
               searchCtx->fixPath.c);
         } else
            goto fail;

         // create a new stack entry
         sptr = ff_create_stack_element(filePath.c, filePath.len,
             searchCtx->wildcardPath.c, searchCtx->wildcardPath.len,
             searchCtx->maxRecursion, false);
         if (!sptr)
            break;
         ff_push(searchCtx, sptr);
      } else
         break;
   }

fail:
   return NULL;
}

//Free the list of lists of visited files and directories
//Can handle it if the passed search_context is NULL;
private void
findfileFreeVisitedList(FileSearchCtx* search_ctx_arg) {
   if (!search_ctx_arg)
      return;

   FileSearchCtx* searchCtx = search_ctx_arg;
   findfileFreeVisitedList_list(&searchCtx->visitedLists);
   findfileFreeVisitedList_list(&searchCtx->allVisitedLists);
}

private void
findfileFreeVisitedList_list(VisitedList **list_headp) {
   while (*list_headp != NULL) {
      VisitedList* vp = (*list_headp)->next;
      ff_free_visited_list((*list_headp)->ffvl_visited_list);

      eeglFree((*list_headp)->filename);
      eeglFree(*list_headp);
      *list_headp = vp;
   }
   *list_headp = NULL;
}

private void
ff_free_visited_list(Visited* vl) {
   while (vl) {
      Visited* vp = vl->next;
      eeglFree(vl->wildcardPath);
      eeglFree(vl);
      vl = vp;
   }
   vl = NULL;
}

//Return the already visited list for the given filename. If none is found, allocate a new one.
private VisitedList*
ff_get_visited_list(Text filename, OUT VisitedList** listHead) {
   VisitedList  *retptr = NULL;

   // check if a visited list for the given filename exists
   if (*listHead) {
      retptr = *listHead;
      while (retptr) {
          if (fnamecmp(filename.c, retptr->filename) == 0) {
#ifdef FF_VERBOSE
            if (p_verbose >= 5) {
               verbose_enter_scroll();
               smsg("ff_get_visited_list: FOUND list for %s", filename.c);
               // don't overwrite this either
               msg_puts("\n");
               verbose_leave_scroll();
            }
#endif
            return retptr;
         }
         retptr = retptr->next;
      }
   }

#ifdef FF_VERBOSE
   if (p_verbose >= 5) {
      verbose_enter_scroll();
      smsg("ff_get_visited_list: new list for %s", filename.c);
      // don't overwrite this either
      msg_puts("\n");
      verbose_leave_scroll();
   }
#endif

   //if we reach this we didn't find a list and we have to allocate new list
   retptr = ALLOC_ONE(VisitedList);

   retptr->ffvl_visited_list = NULL;
   retptr->filename = copySubstr(filename.c, filename.len);
   retptr->next = *listHead;
   *listHead = retptr;

   return retptr;
}

//check if two wildcard paths are equal. Returns TRUE or FALSE.
//They are equal if:
// - both paths are NULL
// - they have the same length
// - char by char comparison is OK
// - the only differences are in the counters behind a '**', so
//   '**\20' is equal to '**\24'
private int
ff_wc_equal(CS s1, CS s2) {
   int      i, j;
   Unt c1 = ZERO;
   Unt c2 = ZERO;
   int prev1 = ZERO;
   int prev2 = ZERO;

   if (s1 == s2)
      return TRUE;

   if (s1 == NULL || s2 == NULL)
      return FALSE;

   for (i = 0, j = 0; s1[i] != ZERO && s2[j] != ZERO;) {
      c1 = mb_ptr2char(s1 + i);
      c2 = mb_ptr2char(s2 + j);

      if ((c1 != c2) && (prev1 != '*' || prev2 != '*'))
          return FALSE;
      prev2 = prev1;
      prev1 = c1;

      i += utfCharLen(s1 + i);
      j += utfCharLen(s2 + j);
   }
   return s1[i] == s2[j];
}

//maintain the list of already visited files and dirs
//returns FAIL if the given file/dir is already in the list
//returns OK if it is newly added
//
//TODO: What to do on memory allocation problems?
//   -> return TRUE - Better the file is found several times instead of never.
private int
checkFirstTimeVisit(Visited** visited_list, Text fname, CS wc_path, Unt wc_pathlen) {
   FileStat st;
   int url = FALSE;

   // For a URL we only compare the name, otherwise we compare the
   // device/inode (unix) or the full path name (not Unix).
   if (path_with_url(fname.c)) {
      copySubstrToAllocation(fileExpansionS.c, fname);
      fileExpansionS.len = fname.len;
      url = TRUE;
   } else {
      fileExpansionS.c[0] = ZERO;
      fileExpansionS.len = 0;
      if (stat((char *)fname.c, &st) < 0)
          return FAIL;
   }

   // check against list of already visited files
   Visited* vp;
   for (vp = *visited_list; vp != NULL; vp = vp->next) {
      if (
         !url ? (vp->areDevInoValid && vp->deviceId == st.st_dev
                       && vp->inodeId == st.st_ino)
              :
         fnamecmp(vp->ffv_fname, fileExpansionS.c) == 0
         )
      {
          // are the wildcard parts equal
          if (ff_wc_equal(vp->wildcardPath, wc_path) == TRUE)
         // already visited
         return FAIL;
      }
   }

   //New file/dir.  Add it to the list of visited files/dirs.
   vp = alloc( offsetof(Visited, ffv_fname) + fileExpansionS.len + 1);

   if (!url) {
      vp->areDevInoValid = TRUE;
      vp->deviceId = st.st_dev;
      vp->inodeId = st.st_ino;
      vp->ffv_fname[0] = ZERO;
   } else {
      vp->areDevInoValid = FALSE;
      STRCPY(vp->ffv_fname, fileExpansionS.c);
   }
   if (wc_path != NULL)
      vp->wildcardPath = copySubstr(wc_path, wc_pathlen);
   else
      vp->wildcardPath = NULL;

   vp->next = *visited_list;
   *visited_list = vp;

   return OK;
}

//create stack element from given path pieces
private DirSearchStack *
ff_create_stack_element(
   CS fix_part,
   Unt   fix_partlen,
   CS wc_part,
   Unt   wc_partlen,
   int level,
   Boole star_star_empty
) {
   DirSearchStack* new;
   new = ALLOC_ONE(DirSearchStack);
   new->ffs_prev      = NULL;
   new->files = (ExpandMatch){.c = null, .len = 0};
   new->ffs_filearray_cur  = 0;
   new->stage      = 0;
   new->depth      = level;
   new->didExpandStarStar = star_star_empty;

   // the following saves NULL pointer checks in eeFindFile
   if (fix_part == NULL) {
      fix_part = Em;
      fix_partlen = 0;
   }
   new->fixedPathPart.c = copySubstr(fix_part, fix_partlen);
   new->fixedPathPart.len = fix_partlen;

   if (!wc_part) {
      wc_part = Em;
      wc_partlen = 0;
   }
   new->wildcardPathPart.c = copySubstr(wc_part, wc_partlen);
   new->wildcardPathPart.len = wc_partlen;

   if (new->fixedPathPart.c == NULL || new->wildcardPathPart.c == NULL) {
      ff_free_stack_element(new);
      new = NULL;
   }

   return new;
}

//Push a dir onto the directory stack.
private void
ff_push(FileSearchCtx *searchCtx, DirSearchStack *stack_ptr) {
   // check for NULL pointer, not to return an error to the user, but to prevent a crash
   if (!stack_ptr)
      return;

   stack_ptr->ffs_prev = searchCtx->stack;
   searchCtx->stack = stack_ptr;
}

//Pop a dir from the directory stack. Return NULL if stack is empty.
private DirSearchStack *
ff_pop(FileSearchCtx* searchCtx) {

   DirSearchStack* sptr = searchCtx->stack;
   if (searchCtx->stack)
      searchCtx->stack = searchCtx->stack->ffs_prev;

   return sptr;
}

//free the given stack element
private void
ff_free_stack_element(DirSearchStack* stack) {
   // EE_CLEAR_STRING handles possible NULL pointers
   EE_CLEAR_STRING(stack->fixedPathPart);
   EE_CLEAR_STRING(stack->wildcardPathPart);

   deleteArena(stack->files.a);
   eeglFree(stack);
}

//Clear the search context, but NOT the visited list.
private void
ff_clear(FileSearchCtx* searchCtx) {
   DirSearchStack* sptr;

   // clear up stack
   while ((sptr = ff_pop(searchCtx)) != NULL)
      ff_free_stack_element(sptr);

   if (searchCtx->stopDirs != NULL) {
      int  i = 0;

      while (searchCtx->stopDirs[i].c != NULL) {
          eeglFree(searchCtx->stopDirs[i].c);
          i++;
      }
      EE_CLEAR(searchCtx->stopDirs);
   }

   // reset everything
   EE_CLEAR_STRING(searchCtx->needle);
   EE_CLEAR_STRING(searchCtx->startDir);
   EE_CLEAR_STRING(searchCtx->fixPath);
   EE_CLEAR_STRING(searchCtx->wildcardPath);
   searchCtx->maxRecursion = 0;
}

// check if the given path is in the stopdirs returns TRUE if yes else FALSE
private int
ff_path_in_stoplist(CS path, int path_len, Arr(Text) stopdirs_v) {
   int      i = 0;

   // eat up trailing path separators, except the first
   while (path_len > 1 && path[path_len - 1] == '/')
      path_len--;

   // if no path consider it as match
   if (path_len == 0)
      return TRUE;

   for (i = 0; stopdirs_v[i].c != NULL; i++) {
      // match for parent directory. So '/home' also matches
      // '/home/rks'. Check for '/' in stopdirs_v[i], else
      // '/home/r' would also match '/home/rks'
      if (fnamencmp(stopdirs_v[i].c, path, path_len) == 0
         && ((int)stopdirs_v[i].len <= path_len
             || stopdirs_v[i].c[path_len] == '/'))
          return TRUE;
   } 

   return FALSE;
}

//Find the file name "ptr[len]" in the path. Also find directory names.
//
//On the first call set the parameter 'first' to TRUE to initialize
//the search. For repeating calls to FALSE.
//
//Repeating calls will return other files called 'ptr[len]' from the path.
//
//Only on the first call 'ptr' and 'len' are used. For repeating calls they
//don't need valid values.
//
//If nothing found on the first call, the option FNAME_MESS will issue the message:
//      'Can't find file "<file>" in path'
//On repeating calls:
//      'No more file "<file>" found in path'
//
//options:
//FNAME_MESS       give error message when not found
//
//Use nameBuffG[]! Return an allocated string for the file name. NULL for error.
CS
findFileInPath(
   Text fname,
   Unt  options,
   Boole first,      // use count'th matching file name
   CS rel_fname,   // file name searching relative to
   OUT Byte** file_to_find,   // modified copy of file name
   OUT FileSearchCtx** searchCtx   // state of the search
){
   return findFileInPathImpl(
         fname, options, first, curBook->o.path, FINDFILE_BOTH, rel_fname, 
         curBook->o.suffixesAdd ? curBook->o.suffixesAdd : S"",
         OUT file_to_find, OUT searchCtx
   );
}

# if defined(EXITFREE) || defined(PROTO)
void
free_findfile(void){
    EE_CLEAR_STRING(fileExpansionS);
}
# endif

//Find the directory name "ptr[len]" in the path.
//
//options:
//FNAME_MESS       give error message when not found
//FNAME_UNESC       unescape backslashes.
//
//Use nameBuffG[]! Return an allocated string for the file name. NULL for error.
private CS
find_directory_in_path(
   Text fName,
   Unt options,
   CS rel_fname,   // file name searching relative to
   OUT Byte** file_to_find,   // in/out: modified copy of file name
   OUT FileSearchCtx** searchCtx   // in/out: state of the search
){
   return findFileInPathImpl(
         fName, options, true, p_cdpath, FINDFILE_DIR, rel_fname, Em, OUT file_to_find, OUT searchCtx
   );
}

//Find the file name "ptr[len]" in the path. Also find directory names.
//
//On the first call set the parameter 'first' to TRUE to initialize
//the search. For repeating calls to FALSE.
//
//Repeating calls will return other files called 'ptr[len]' from the path.
//
//Only on the first call 'ptr' and 'len' are used. For repeating calls they
//don't need valid values.
//
//If nothing found on the first call, the option FNAME_MESS will issue the message:
//      'Can't find file "<file>" in path'
//On repeating calls:
//      'No more file "<file>" found in path'
//
//options:
//FNAME_MESS       give error message when not found
//
//Use nameBuffG[]! Return an allocated string for the file name. NULL for error.
private CS
findFileInPathImpl(
   Text fName,
   Unt options,
   Boole first,      // use count'th matching file name
   NULLABLE CS path_option,   // path or cdpath
   Unt find_what,   // FINDFILE_FILE, _DIR or _BOTH
   CS rel_fname,   // file name we are looking relative to.
   CS suffixes,   // list of suffixes, 'suffixesadd' option
   OUT Byte** file_to_find,   // modified copy of file name
   OUT FileSearchCtx** search_ctx_arg // state of the search
){
   FileSearchCtx** searchCtx = search_ctx_arg;
   static CS dir;
   static int did_findfile_init = FALSE;
   CS file_name = NULL;
   int rel_to_curdir;
   static Unt file_to_findlen = 0;

   if (first) {
      if (fName.len == 0)
         return NULL;

      // copy file name into nameBuffG, expanding environment variables
      Byte save_char = fName.c[fName.len];
      fName.c[fName.len] = ZERO;
      file_to_findlen = doExpandEnvVarsWithEscaped(
            OUT (Text){nameBuffG, MAXPATHL}, fName.c, true, NULL
      );
      fName.c[fName.len] = save_char;

      eeglFree(*file_to_find);
      *file_to_find = copySubstr(nameBuffG, file_to_findlen);
      if (options & FNAME_UNESC) {
         // Change all "\ " to " ".
         for (CS ptr = *file_to_find; *ptr != ZERO; ++ptr) {
            if (ptr[0] == '\\' && ptr[1] == ' ') {
                mch_memmove(ptr, ptr + 1, (Unt)((*file_to_find + file_to_findlen) - (ptr + 1)) + 1);
                --file_to_findlen;
            }
         } 
      }
   }

   rel_to_curdir = ((*file_to_find)[0] == '.'
          && ((*file_to_find)[1] == ZERO
         || (*file_to_find)[1] == '/'
         || ((*file_to_find)[1] == '.'
             && ((*file_to_find)[2] == ZERO || (*file_to_find)[2] == '/')))
   );
   if (eeIsAbsName(*file_to_find)
       // "..", "../path", "." and "./path": don't use the path_option
       || rel_to_curdir
   ) {
      //Absolute path, no need to use "path_option".
      //If this is not a first call, return NULL.  We already returned a filename on the first call.
      if (first == TRUE) {
         int      l;
         int      nameBuffGlen;
         int      run;
         Unt   rel_fnamelen = 0;
         CS suffix;

         if (path_with_url(*file_to_find)) {
            file_name = copySubstr(*file_to_find, file_to_findlen);
            goto theend;
         }

         if (rel_fname != NULL)
            rel_fnamelen = STRLEN(rel_fname);

         // When FNAME_REL flag given first use the directory of the file.
         // Otherwise or when this fails use the current directory.
         for (run = 1; run <= 2; ++run) {
            l = (int)file_to_findlen;
            if (run == 1
               && rel_to_curdir
               && (options & FNAME_REL)
               && rel_fname != NULL
               && rel_fnamelen + l < MAXPATHL
            ) {
               l = eeSnprintf(
                  nameBuffG,
                  MAXPATHL,
                  "%.*s%s",
                  (int)(fiGetShortFiName(rel_fname) - rel_fname),
                  rel_fname,
                  *file_to_find
               );
            } else {
                STRCPY(nameBuffG, *file_to_find);
                run = 2;
            }

            // When the file doesn't exist, try adding parts of @suffixesadd
            nameBuffGlen = l;
            suffix = suffixes;
            for (;;) {
               if (mch_getperm(nameBuffG) >= 0
                    && (find_what == FINDFILE_BOTH
                   || ((find_what == FINDFILE_DIR) == mch_isdir(nameBuffG)))
               ) {
                  file_name = copySubstr(nameBuffG, nameBuffGlen);
                  goto theend;
               }
               if (*suffix == ZERO)
                  break;
               nameBuffGlen = l + copy_option_part(&suffix, nameBuffG + l, MAXPATHL - l, ",");
            }
         }
      }
   } else {
      //Loop over all paths in the 'path' or 'cdpath' option.
      //When "first" is set, first setup to the start of the option.
      //Otherwise continue to find the next match.
      if (first == TRUE) {
         // findfileFreeVisitedList can handle a possible NULL pointer
         findfileFreeVisitedList(*searchCtx);
         dir = path_option;
         did_findfile_init = FALSE;
      }

      for (;;) {
         if (did_findfile_init) {
            file_name = eeFindFile(*searchCtx);
            if (file_name)
               break;

            did_findfile_init = FALSE;
         } else {
            if (!dir || *dir == ZERO) {
               // We searched all paths of the option, now we can free the search context.
               eeFindFile_cleanup(*searchCtx);
               *searchCtx = NULL;
               break;
            }

            Byte buf[MAXPATHL];
            // copy next path
            buf[0] = ZERO;
            copy_option_part(&dir, buf, MAXPATHL, " ,");

            // get the stopdir string
            CS r_ptr = eeFindFile_stopdir(buf);
            *searchCtx = eeFindFile_init(
               buf, (Text){*file_to_find, file_to_findlen}, r_ptr, 100, FALSE, find_what,
               *searchCtx, FALSE, rel_fname
            );
            if (*searchCtx != NULL)
               did_findfile_init = TRUE;
         }
      }
   }
   if (file_name == NULL && (options & FNAME_MESS)) {
      if (first == TRUE) {
          if (find_what == FINDFILE_DIR)
         showErrFmtMsg(_(e_cant_find_directory_str_in_cdpath), *file_to_find);
          else
         showErrFmtMsg(_(e_cant_find_file_str_in_path), *file_to_find);
      } else {
          if (find_what == FINDFILE_DIR)
         showErrFmtMsg(_(e_no_more_directory_str_found_in_cdpath), *file_to_find);
          else
         showErrFmtMsg(_(e_no_more_file_str_found_in_path), *file_to_find);
      }
    }

theend:
    return file_name;
}

//Get the file name at the cursor.
//If Visual mode is active, use the selected text if it's in one line.
//Return the name in allocated memory, NULL for failure.
CS
grab_file_name(long count, OUT LineNr* file_lnum) {
   Unt options = FNAME_MESS|FNAME_EXP|FNAME_REL|FNAME_UNESC;

   if (VIsual_active) {
      int len;
      CS ptr;
      if (get_visual_text(NULL, OUT &ptr, OUT &len) == FAIL)
         return NULL;
      // Only recognize ":123" here
      if (file_lnum != NULL && ptr[len] == ':' && SAFE_isdigit(ptr[len + 1])) {
         CS p = ptr + len + 1;

         *file_lnum = parseLong(&p);
      }
      return find_file_name_in_path(ptr, len, options, count, curBook->fullFileName);
   }
   return file_name_at_cursor(options | FNAME_HYP, count, OUT file_lnum);
}

//Return the file name under or after the cursor.
//
//The 'path' option is searched if the file name is not absolute.
//The string returned has been alloc'ed and should be freed by the caller.
//NULL is returned if the file name or file is not found.
//
//options:
//FNAME_MESS   give error messages
//FNAME_EXP    expand to path
//FNAME_HYP    check for hypertext link
//FNAME_INCL   apply @includeexpr
CS
file_name_at_cursor(int options, long count, OUT LineNr* file_lnum) {
    return file_name_in_line(ml_get_curline(),
            curPor->cursor.col, options, count, curBook->fullFileName,
            OUT file_lnum);
}

//Return the name of the file under or after ptr[col]. Otherwise like file_name_at_cursor().
CS
file_name_in_line(
   CS line,
   int col,
   int options,
   long count,
   CS rel_fname,   // file we are searching relative to
   OUT LineNr* file_lnum   // line number after the file name
){
   int len;
   int in_type = TRUE;
   int is_url = FALSE;

   //search forward for what could be the start of a file name
   CS ptr = line + col;
   while (*ptr != ZERO && !eeIsFnameChar(*ptr))
      MB_PTR_ADV(ptr);
   if (*ptr == ZERO)   {   // nothing found
      if (options & FNAME_MESS)
          emsg(_(e_no_file_name_under_cursor));
      return S"";
   }

   //Search backward for first char of the file name.
   //Go one char back to ":" before "//" even when ':' is not in 'isfname'.
   while (ptr > line) {
      if ((len = (*mb_head_off)(line, ptr - 1)) > 0)
         ptr -= len + 1;
      ei (eeIsFnameChar(ptr[-1]) || ((options & FNAME_HYP) && path_is_url(ptr - 1)))
         --ptr;
      else
         break;
   }

   //Search forward for the last char of the file name.
   //Also allow "://" when ':' is not in 'isfname'.
   len = 0;
   while (eeIsFnameChar(ptr[len]) || (ptr[len] == '\\' && ptr[len + 1] == ' ')
       || ((options & FNAME_HYP) && path_is_url(ptr + len))
       || (is_url && firstOccurrence((CS)":?&=", ptr[len]) != NULL))
    {
      // After type:// we also include :, ?, & and = as valid characters, so
      // that http://google.com:8080?q=this&that=ok works.
      if ((ptr[len] >= 'A' && ptr[len] <= 'Z') || (ptr[len] >= 'a' && ptr[len] <= 'z')) {
         if (in_type && path_is_url(ptr + len + 1))
            is_url = TRUE;
      } else
         in_type = FALSE;

      if (ptr[len] == '\\')
          // Skip over the "\" in "\ ".
          ++len;
      len += utfCharLen(ptr + len);
   }

   //If there is trailing punctuation, remove it.
   //But don't remove "..", could be a directory name.
   if (len > 2 && firstOccurrence((CS)".,:;!", ptr[len - 1]) != NULL && ptr[len - 2] != '.')
      --len;

   if (file_lnum) {
      CS match_text = S" line ";      // english
      Unt match_textlen = 6;

      // Get the number after the file name and a separator character.
      // Also accept " line 999" with and without the same translation as used in lastSetMsg().
      CS p = ptr + len;
      if (STRNCMP(p, match_text, match_textlen) == 0)
          p += match_textlen;
      else {
         // no match with english, try localized
         match_text = _(line_msg);
         match_textlen = STRLEN(match_text);

         if (STRNCMP(p, match_text, match_textlen) == 0)
            p += match_textlen;
         else
            p = skipwhite(p);
      }
      if (*p != ZERO) {
         if (!SAFE_isdigit(*p))
            ++p;          // skip the separator
         p = skipwhite(p);
         if (SAFE_isdigit(*p))
            *file_lnum = (int)parseLong(&p);
      }
   }

   return find_file_name_in_path(ptr, len, options, count, rel_fname);
}

private CS
eval_includeexpr(CS ptr, int len) {
   if (!curBook->o.includeExpr) {
      return null;
   }
   
   ScriptPos save_sctx = scriptPosG;

   set_EeglVar_string(VV_FNAME, ptr, len);
   scriptPosG = curBook->o.scriptLocs[BOOK_includeExpr];

   CS res = eval_to_string_safe(curBook->o.includeExpr, true);

   set_EeglVar_string(VV_FNAME, NULL, 0);
   scriptPosG = save_sctx;
   return res;
}

//Return the name of the file ptr[len] in 'path'. Otherwise like file_name_at_cursor().
CS
find_file_name_in_path(
   CS ptr,
   int len,
   Unt options,
   long count,
   CS rel_fname   // file we are searching relative to
){
   if (len == 0)
      return Em;

   CS file_name;
   CS tofree = NULL;
   if ((options & FNAME_INCL) && curBook->o.includeExpr) {
      tofree = eval_includeexpr(ptr, len);
      if (tofree) {
          ptr = tofree;
          len = (int)STRLEN(ptr);
      }
   }

   if ((options & FNAME_EXP) != 0) {
      CS file_to_find = NULL;
      FileSearchCtx* searchCtx = NULL;

      file_name = findFileInPath((Text){ ptr, len}, options & ~FNAME_MESS,
                 true, rel_fname, OUT &file_to_find, OUT &searchCtx);

      //If the file could not be found normally, try applying @includeexpr (unless done already).
      if (!file_name && !(options & FNAME_INCL) && curBook->o.includeExpr) {
         tofree = eval_includeexpr(ptr, len);
         if (tofree) {
            ptr = tofree;
            len = (int)STRLEN(ptr);
            file_name = findFileInPath((Text){ptr, len}, options & ~FNAME_MESS,
                    true, rel_fname, &file_to_find, &searchCtx);
         }
      }
      if (!file_name && (options & FNAME_MESS)) {
         Unt c = ptr[len];
         ptr[len] = ZERO;
         showErrFmtMsg(_(e_cant_find_file_str_in_path_2), ptr);
         ptr[len] = c;
      }

      // Repeat finding the file "count" times.  This matters when it
      // appears several times in the path.
      while (file_name && --count > 0) {
         eeglFree(file_name);
         file_name = findFileInPath((Text){ptr, len}, options, false, rel_fname,
                        &file_to_find, &searchCtx);
      }

      eeglFree(file_to_find);
      eeFindFile_cleanup(searchCtx);
   } else
      file_name = copySubstr(ptr, len);

   eeglFree(tofree);

   return file_name;
}

//Return the end of the directory name, on the first path separator:
//"/path/file", "/path/dir/", "/path//dir", "/file"
//      ^             ^             ^        ^
private CS
getLastSlash(CS fname) {
   CS dir_end = fname;
   CS next_dir_end = fname;
   Boole look_for_sep = true;

   for (CS p = fname; *p != ZERO; MB_PTR_ADV(p)) {
      if (*p == '/') {
         if (look_for_sep) {
            next_dir_end = p;
            look_for_sep = false;
         }
      } else {
         if (!look_for_sep)
            dir_end = next_dir_end;
         look_for_sep = true;
      }
   }
   return dir_end;
}

//Move "*psep" back to the previous path separator in "path".
//Return FAIL is "*psep" ends up at the beginning of "path".
private int
find_previous_pathsep(CS path, Byte** psep) {
   // skip the current separator
   if (*psep > path && **psep == '/')
      --*psep;

   // find the previous separator
   while (*psep > path) {
      if (**psep == '/')
         return OK;
      MB_PTR_BACK(path, *psep);
   }

   return FAIL;
}

//Return TRUE if "maybe_unique" is unique wrt other_paths in "matches".
//"maybe_unique" is the end portion of "matches->c[i]".
private Boole
is_unique(CS maybe_unique, ExpandMatch* matches, Unt i) {
   int candidate_len = (int)STRLEN(maybe_unique);

   for (Unt j = 0; j < matches->len; j++) {
      if (j == i)
         continue;  // don't compare it with itself

      int other_path_len = (int)STRLEN(matches->c[j]);
      if (other_path_len < candidate_len)
         continue;  // it's different when it's shorter

      CS rival = matches->c[j] + other_path_len - candidate_len;
      if (fnamecmp(maybe_unique, rival) == 0 && (rival == matches->c[j] || *(rival - 1) == '/'))
         return false;  // match
   }

   return true;  // no match found
}

//Split the 'path' option into an array of strings in ArrayList.  Relative
//paths are expanded to their equivalent fullpath.  This includes the "."
//(relative to current buffer directory) and empty path (relative to current directory) notations.
//
//TODO: handle upward search (;) and path limiter (**N) notations by
//expanding each into their equivalent path(s).
private void
expand_path_option(CS curdir, NULLABLE CS path_option, OUT ExpandMatch* files) {
                              // path or cdpath
   if (!path_option)
      return;
      
   Byte buf[MAXPATHL];
   CS p;
   Unt curdirlen = 0;
   while (*path_option != ZERO) {
      Unt buflen = copy_option_part(&path_option, buf, MAXPATHL, " ,");

      if (buf[0] == '.' && (buf[1] == ZERO || buf[1] == '/')) {

         // Relative to current book:
         // "/path/file" + "." -> "/path/"
         // "/path/file"  + "./subdir" -> "/path/subdir"
         if (curBook->fullFileName == NULL)
            continue;
         p = fiGetShortFiName(curBook->fullFileName);
         Unt plen = (Unt)(p - curBook->fullFileName);
         if (plen + buflen >= MAXPATHL)
            continue;
         if (buf[1] == ZERO)
            buf[plen] = ZERO;
         else
            mch_memmove(buf + plen, buf + 2, (buflen - 2) + 1); // +1 for ZERO
         mch_memmove(buf, curBook->fullFileName, plen);
         buflen = simplify_filename(buf);
      } ei (buf[0] == ZERO) {
         // relative to current directory
         STRCPY(buf, curdir);
         if (curdirlen == 0)
            curdirlen = STRLEN(curdir);
         buflen = curdirlen;
      } ei (path_with_url(buf))
         // URL can't be used here
         continue;
      ei (fiIsRelative(buf)) {
         // Expand relative path to their full path equivalent
         if (curdirlen == 0)
            curdirlen = STRLEN(curdir);
         if (curdirlen + buflen + 3 > MAXPATHL)
            continue;

         mch_memmove(buf + curdirlen + 1, buf, buflen + 1); // +1 for ZERO
         STRCPY(buf, curdir);
         buf[curdirlen] = '/';
         buflen = simplify_filename(buf);
      }
      addExpandMatch(copySubstr(buf, buflen), OUT files);
   }
}

//Return a pointer to the file or directory name in "fname" that matches the
//longest path in "ga"p, or NULL if there is no match. For example:
//
//   path: /foo/bar/baz
//  fname: /foo/bar/baz/quux.txt
//return:       ^this
private CS
get_path_cutoff(CS fname, OUT ExpandMatch* matches) {
   int maxlen = 0;
   CS cutoff = NULL;

   for (Unt i = 0; i < matches->len; i++) {
      int j = 0;

      while ((fname[j] == matches->c[i][j]) && fname[j] != ZERO && matches->c[i][j] != ZERO)
         j++;
      if (j > maxlen) {
         maxlen = j;
         cutoff = &fname[j];
      }
   }

   // skip to the file or directory name
   if (cutoff) {
      while (*cutoff == '/')
         MB_PTR_ADV(cutoff);
   } 

   return cutoff;
}

//Sort, remove duplicates and modify all the fullpath names in "matches" so that they are unique with 
//respect to each other while conserving the part that matches the pattern. Beware, this is at 
//least O(n^2) wrt "matches->len".
private void
uniquefy_paths( OUT ExpandMatch* matches, CS pattern, CS path_option) {   // path or cdpath
   Arr(CS) fnames = matches->c;
   int sort_again = FALSE;
   RegMatch regmatch;
   CS short_name;

   remove_duplicates(OUT matches);
   ExpandMatch files = {};
   files.a = matches->a;

   //We need to prepend a '*' at the beginning of file_pattern so that the
   //regex matches anywhere in the path. FIXME: is this valid for all possible patterns?
   int len = (int)STRLEN(pattern);
   CS file_pattern = alloc(len + 2);
   file_pattern[0] = '*';
   STRCPY(file_pattern + 1, pattern);
   CS pat = file_pat_to_reg_pat(file_pattern, NULL, NULL);
   eeglFree(file_pattern);

   regmatch.rm_ic = TRUE;      // always ignore case
   regmatch.regprog = compileRegexp(pat, RE_MAGIC + RE_STRING);
   eeglFree(pat);
   if (regmatch.regprog == NULL)
      return;
   Byte curdir[MAXPATHL];
   mch_dirname(curdir, MAXPATHL);
   expand_path_option(curdir, path_option, OUT &files);

   Arr(CS) in_curdir = ALLOC_CLEAR_MULT(CS, matches->len);

   for (Unt i = 0; i < matches->len && !gotInterruptG; i++) {
      CS path = fnames[i];
      CS dir_end = getLastSlash(path);

      len = (int)STRLEN(path);
      if (fnamencmp(curdir, path, dir_end - path) == 0 && curdir[dir_end - path] == ZERO)
         in_curdir[i] = copySubstr(path, len);

      // Shorten the filename while maintaining its uniqueness
      CS path_cutoff = get_path_cutoff(path, OUT matches);

      // Don't assume all files can be reached without path when search
      // pattern starts with star star slash, so only remove path_cutoff when possible.
      if (pattern[0] == '*' && pattern[1] == '*'
            && pattern[2] == '/'
            && path_cutoff
            && eeRegexec(&regmatch, path_cutoff, (ColNr)0)
            && is_unique(path_cutoff, matches, i)
      ) {
         sort_again = TRUE;
         mch_memmove(path, path_cutoff, STRLEN(path_cutoff) + 1);
      } else {
         // Here all files can be reached without path, so get shortest
         // unique path.  We start at the end of the path.
         CS pathsep_p = path + len - 1;

         while (find_previous_pathsep(path, &pathsep_p)) {
            if (eeRegexec(&regmatch, pathsep_p + 1, (ColNr)0)
               && is_unique(pathsep_p + 1, matches, i)
               && path_cutoff != NULL && pathsep_p + 1 >= path_cutoff
            ) {
                sort_again = TRUE;
                mch_memmove(path, pathsep_p + 1,
                   (Unt)((path + len) - (pathsep_p + 1)) + 1);  // +1 for ZERO
                break;
            }
         }
      }

      if (!fiIsRelative(path)) {
         //Last resort: shorten relative to curdir if possible. 'possible' means:
         //1. It is under the current directory.
         //2. The result is actually shorter than the original.
         //
         //      Before        curdir   After
         //      /foo/bar/file.txt     /foo/bar   ./file.txt
         //      /file.txt        /      /file.txt
         short_name = shorten_fname(path, curdir);
         if (short_name && short_name > path + 1) {
            eeSnprintf(path, MAXPATHL, ".%s%s", "/", short_name);
         }
      }
      ui_breakcheck();
   }

   // Shorten filenames in /in/current/directory/{filename}
   for (Unt i = 0; i < matches->len && !gotInterruptG; i++) {
      Unt rel_pathsize;
      CS path = in_curdir[i];
      if (!path)
         continue;

      //If the {filename} is not unique, change it to ./{filename}.
      //Else reduce it to {filename}
      short_name = shorten_fname(path, curdir);
      if (!short_name)
         short_name = path;
      if (is_unique(short_name, matches, i)) {
         STRCPY(fnames[i], short_name);
         continue;
      }

      rel_pathsize = 2 + STRLEN(short_name) + 1;
      CS rel_path = alloc(rel_pathsize);

      eeSnprintf(rel_path, rel_pathsize, ".%s%s", "/", short_name);

      eeglFree(fnames[i]);
      fnames[i] = rel_path;
      sort_again = TRUE;
      ui_breakcheck();
   }

   if (in_curdir) {
      for (Unt i = 0; i < matches->len; i++)
         eeglFree(in_curdir[i]);
      eeglFree(in_curdir);
   }
   eeRegFree(regmatch.regprog);

   if (sort_again)
      remove_duplicates(matches);
}

//Call fiGlobpath() with @path values for the given pattern and store the result in "matches".
//Return the total number of matches.
private int
expand_in_path(OUT ExpandMatch* matches, CS pattern, Unt flags) {      // EW_* flags
   Unt gloflags = 0;
   CS path_option = curBook->o.path;

   Byte curdir[MAXPATHL];
   mch_dirname(curdir, MAXPATHL);

   if ((flags & EW_CDPATH) != 0)
      expand_path_option(curdir, p_cdpath, OUT matches);
   else
      expand_path_option(curdir, path_option, OUT matches);
      
   if (matches->len == 0)
      return 0;

   CS paths = concatStrArray(matches->c, matches->len, tConst(","), matches->a);

   if ((flags & EW_ICASE) != 0)
      gloflags |= WILD_ICASE;
   if ((flags & EW_ADDSLASH) != 0)
      gloflags |= WILD_ADD_SLASH;
   fiGlobpath(paths, pattern, OUT matches, gloflags, (flags & EW_CDPATH) != 0);
   eeglFree(paths);

   return matches->len;
}

//Convert a file name into a canonical form. It simplifies a file name into its simplest form by 
//stripping out unneeded components, if any. The resulting file name is simplified in place and 
//will either be the same length as that supplied, or shorter.
Unt
simplify_filename(CS filename) {
   int components = 0;
   CS tail;
   Boole stripping_disabled = false;
   int relative = TRUE;

   CS p = filename;

   if (*p == '/') {
      relative = FALSE;
      do
         ++p;
      while (*p == '/');
   }
   CS start = p;       // remember start after "c:/" or "/" or "///"
   CS p_end = p + STRLEN(p); // point to ZERO at end of string "p"
   // Posix says that "//path" is unchanged but "///path" is "/path".
   if (start > filename + 2) {
      mch_memmove(filename + 1, p, (Unt)(p_end - p) + 1);       // +1 for ZERO
      p_end -= (Unt)(p - (filename + 1));
      start = p = filename + 1;
   }

   do {
      // At this point "p" is pointing to the char following a single "/"
      // or "p" is at the "start" of the (absolute or relative) path name.
      if (*p == '/') {
         mch_memmove(p, p + 1, (Unt)(p_end - (p + 1)) + 1); // remove duplicate "/"
         --p_end;
      } ei (p[0] == '.' && (p[1] == '/' || p[1] == ZERO)) {
         if (p == start && relative)
         p += 1 + (p[1] != ZERO);   // keep single "." or leading "./"
         else {
            // Strip "./" or ".///".  If we are at the end of the file name and there is no 
            // trailing path separator, either strip "/." if we are after "start", or strip "." 
            // if we are at the beginning of an absolute path name .
            tail = p + 1;
            if (p[1] != ZERO) {
               while (*tail == '/')
                  MB_PTR_ADV(tail);
            } ei (p > start)
                --p;      // strip preceding path separator

            mch_memmove(p, tail, (Unt)(p_end - tail) + 1);
            p_end -= (Unt)(tail - p);
         }
      } ei (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == ZERO)) {
         // Skip to after ".." or "../" or "..///".
         tail = p + 2;
         while (*tail == '/')
            MB_PTR_ADV(tail);

         if (components > 0) {     // strip one preceding component
            int do_strip = FALSE;
            Byte saved_char;
            FileStat st;

            // Don't strip for an erroneous file name.
            if (!stripping_disabled) {
               //If the preceding component does not exist in the file
               //system, we strip it.  On Unix, we don't accept a symbolic
               //link that refers to a non-existent file.
               saved_char = p[-1];
               p[-1] = ZERO;
               if (lstat((char *)filename, &st) < 0)
                  do_strip = TRUE;
               p[-1] = saved_char;

               --p;
               // Skip back to after previous '/'.
               while (p > start && !after_pathsep(start, p))
                  MB_PTR_BACK(start, p);

               if (!do_strip) {
                  //If the component exists in the file system, check that stripping it won't 
                  //change the meaning of the file name. First get information about the 
                  //unstripped file name. This may fail if the component to strip is not a 
                  //searchable directory (but a regular file, for instance), since the trailing 
                  //"/.." cannot be applied then. We don't strip it then since we don't want to 
                  //replace an erroneous file name by a valid one, and we disable stripping of 
                  //later components.
                  saved_char = *tail;
                  *tail = ZERO;
                  if (stat((char *)filename, &st) >= 0)
                      do_strip = TRUE;
                  else
                      stripping_disabled = true;
                  *tail = saved_char;
                  if (do_strip) {
                     FileStat new_st;

                     //The check for the unstripped file name above works also for a symbolic link 
                     //pointing to a searchable directory. But then the parent of the directory 
                     //pointed to by the link must be the same as the stripped file name. (The 
                     //latter exists in the file system since it's the component's parent directory)
                     if (p == start && relative)
                        (void)stat(".", OUT &new_st);
                     else {
                        saved_char = *p;
                        *p = ZERO;
                        (void)stat((char *)filename, &new_st);
                        *p = saved_char;
                     }

                     if (new_st.st_ino != st.st_ino || new_st.st_dev != st.st_dev) {
                        do_strip = FALSE;
                        //We don't disable stripping of later
                        //components since the unstripped path name is still valid.
                     }
                  }
               }
            }

            if (!do_strip) {
                //Skip the ".." or "../" and reset the counter for the
                //components that might be stripped later on.
                p = tail;
                components = 0;
            } else {
               //Strip previous component. If the result would get empty and there is no 
               //trailing path separator, leave a single "." instead.  If we are at the end of 
               //the file name and there is no trailing path separator and a preceding
               //component is left after stripping, strip its trailing path separator as well.
               if (p == start && relative && tail[-1] == '.') {
                  *p++ = '.';
                  *p = ZERO;
               } else {
                  if (p > start && tail[-1] == '.')
                     --p;

                  mch_memmove(p, tail, (Unt)(p_end - tail) + 1);   // strip previous component
                  p_end -= (Unt)(tail - p);
               }

               --components;
            }
         } ei (p == start && !relative) {  // leading "/.." or "/../"
            mch_memmove(p, tail, (Unt)(p_end - tail) + 1);      // strip ".." or "../"
            p_end -= (Unt)(tail - p);
         } else {
            if (p == start + 2 && p[-2] == '.') {  // leading "./../"
               mch_memmove(p - 2, p, (Unt)(p_end - p) + 1); // strip leading "./"
               p_end -= 2;
               tail -= 2;
            }
            p = tail;      // skip to char after ".." or "../"
         }
      } else {
          ++components;      // simple path component
          p = getnextcomp(p);
      }
    } while (*p != ZERO);

    return (Unt)(p_end - filename);
}

//Return TRUE if the string "p" contains a wildcard that mch_expandpath() can expand.
int
mch_has_exp_wildcard(CS p) {
   for ( ; *p; MB_PTR_ADV(p)) {
      if (*p == '\\' && p[1] != ZERO)
         ++p;
      else
         if (firstOccurrence((CS)"*?[{'", *p) != NULL)
            return TRUE;
   }
   return FALSE;
}

//Return TRUE if the string "p" contains a wildcard. Don't recognize '~' at the end as a wildcard.
int
mch_has_wildcard(CS p){
   for ( ; *p; MB_PTR_ADV(p)) {
      if (*p == '\\' && p[1] != ZERO)
         ++p;
      else
         if (firstOccurrence((CS)"*?[{`'$", *p) != NULL || (*p == '~' && p[1] != ZERO))
            return TRUE;
   }
   return FALSE;
}

private int
have_wildcard(int num, Arr(CS) file){
   for (int i = 0; i < num; i++) {
      if (mch_has_wildcard(file[i]))
          return 1;
   } 
   return 0;
}

private int
save_patterns(int num_pat, Arr(CS) pat, OUT ExpandMatch* files) {
   files->c = ALLOC_MULT(CS, num_pat);
   if (!files->c)
      return FAIL;
   for (int i = 0; i < num_pat; i++) {
      CS s = copyStr(pat[i]);
      // Be compatible with expand_filename(): halve the number of backslashes.
      backslash_halve(s);
      files->c[i] = s;
   }
   files->len = num_pat;
   return OK;
}

void
f_simplify(Var* argvars, Var* returnVar) {
   CS p = tv_get_string_strict(&argvars[0]);
   returnVar->string = copyStr(p);
   simplify_filename(returnVar->string);   // simplify in place
   returnVar->tag = VAR_STRING;
}

//Build the shell command:
//- Set $nonomatch depending on EW_NOTFOUND (hopefully the shell recognizes this).
//- Add the shell command to print the expanded names.
//- Add the temp file name.
//- Add the file name patterns.
private Text
buildShellCommandForWildcardExpansion( CS tempname, int num_pat, Arr(CS) pat, Unt flags) {
#define STRING_INIT(s) \
      {(CS)(s), STRLEN_LITERAL(s)}
            // vimglob() function to define for Posix shell
   static Text sh_vimglob_func = 
      STRING_INIT("vimglob() { while [ $# -ge 1 ]; do echo \"$1\"; shift; done }; vimglob >");
            // vimglob() function with globstar setting enabled, only for bash >= 4.X
   static Text sh_globstar_opt = 
      STRING_INIT("[[ ${BASH_VERSINFO[0]} -ge 4 ]] && shopt -s globstar; ");
#undef STRING_INIT

   //Compute the length of the command. We need 2 extra bytes: for the optional '&' and for the 
   //ZERO. Worst case: "unset nonomatch; print -N >" plus two is 29
   Unt tempnamelen = STRLEN(tempname);
   Unt len = tempnamelen + 29;
   len += sh_vimglob_func.len + sh_globstar_opt.len;

   for (int i = 0; i < num_pat; ++i) {
      // Count the length of the patterns in the same way as they are put in "command" below.
      ++len;            // add space
      for (int j = 0; pat[i][j] != ZERO; ++j) {
         if (firstOccurrence(SHELL_SPECIAL, pat[i][j]) != NULL)
            ++len;      // may add a backslash
         ++len;
      }
   }

   CS command = alloc(len);
   Unt commandlen = 0;
   commandlen = eeSnprintf(
       command, len, "%s%s%s", sh_globstar_opt.c, sh_vimglob_func.c, tempname
   );

   for (int i = 0; i < num_pat; ++i) {
      //When using system(), always add extra quotes, because the shell
      //is started twice.  Otherwise put a backslash before special
      //characters, except inside ``.
      Boole intick = false;

      CS p = command + commandlen;
      *p++ = ' ';
      for (Unt j = 0; pat[i][j] != ZERO; ++j) {
         if (pat[i][j] == '`')
            intick = !intick;
         ei (pat[i][j] == '\\' && pat[i][j + 1] != ZERO) {
            // Remove a backslash, take char literally. But keep backslash inside backticks, 
            // before a special character and before a backtick.
            if (intick
                    || firstOccurrence(SHELL_SPECIAL, pat[i][j + 1]) != NULL
                    || pat[i][j + 1] == '`'
            )
               *p++ = '\\';
            ++j;
         } ei (!intick
             && ((flags & EW_KEEPDOLLAR) == 0 || pat[i][j] != '$')
                  && firstOccurrence(SHELL_SPECIAL, pat[i][j]) != NULL)
             // Put a backslash before a special character, but not
             // when inside ``. And not for $var when EW_KEEPDOLLAR is set.
             *p++ = '\\';

         // Copy one character.
         *p++ = pat[i][j];
      }
      *p = ZERO;
      commandlen = (Unt)(p - command);
   }
   return (Text){command, commandlen};
}

//This code does wild-card pattern matching using the shell
//
//return OK for success, FAIL for error (you may lose some memory) and put
//an error message into *file.
//
//num_pat is number of input patterns
//pat is array of pointers to input patterns
//num_file is pointer to number of matched file names
//file is pointer to array of pointers to matched file names
private int
mch_expand_wildcards(int num_pat, Arr(CS) pat, Unt flags, OUT ExpandMatch* matches){
                                               //EW_* flags
   lo("mch_expand_wc"); 

   //If there are no wildcards, just copy the names to allocated memory.
   //Save a lot of time, because we don't have to start a new shell.
   if (!have_wildcard(num_pat, pat))
      return save_patterns(num_pat, pat, OUT matches);

   // get a name for the temp file
   CS tempname;
   if ((tempname = eeTempName('o', FALSE)) == NULL) {
      emsg(_(e_cant_get_temp_file_name));
      return FAIL;
   }

   Text command = buildShellCommandForWildcardExpansion(tempname, num_pat, pat, flags);
   Unt shellOpts = SHELL_EXPAND | SHELL_SILENT;
   if ((flags & EW_SILENT) == 0)
      shellOpts |= SHELL_SHOW_MSG;

   //Using zsh -G: If a pattern has no matches, it is just deleted from the argument list, 
   //otherwise zsh gives an error message and doesn't expand any other pattern.
   CS extraArg = null;

   // execute the shell command
   lo("mch_expand_wildcards [%s]", command.c);
   int shellResult = call_shell(command.c, extraArg, shellOpts);

   eeglFree(command.c);

   if (shellResult != 0) {           // chCallShell() failed
      mch_remove(tempname);
      eeglFree(tempname);
   
      // With interactive completion, the error message is not printed.
      if ((flags & EW_SILENT) == 0) {
         redraw_later_clear();   // probably messed up screen
         msg_putchar('\n');      // clear bottom line quickly
         commlineRowG = visibleRowsG - 1;   // continue on last line
         msg(_(e_cannot_expand_wildcards));
         msg_start();      // don't overwrite this message
      }
      goto notfound;
   }

   //read the names from the file into memory
   FILE* fd = fopen((char *)tempname, READBIN);
   if (!fd) {
      // Something went wrong, perhaps a file name with a special char.
      if ((flags & EW_SILENT) == 0) {
         msg(_(e_cannot_expand_wildcards));
         msg_start();      // don't overwrite this message
      }
      eeglFree(tempname);
      goto notfound;
   }
   fseek(fd, 0L, SEEK_END);
   long llen = ftell(fd);         // get size of temp file
   fseek(fd, 0L, SEEK_SET);
   
   // just in case ftell() would fail
   CS buf = (llen < 0) ? null : alloc(llen + 1);
   Unt len = llen;
   int readLen = FREAD(buf, 1, len, fd);
   fclose(fd);
   mch_remove(tempname);
   if (readLen != (int)len) {
      // unexpected read error
      showErrFmtMsg(_(e_cant_read_file_str), tempname);
      eeglFree(tempname);
      eeglFree(buf);
      return FAIL;
   }
   eeglFree(tempname);

   // file names are separated with Space
   // file names are separated with NL
   buf[len] = ZERO;      // make sure the buf ends in ZERO
   Unt entryCount = 0;
   for (CS p = buf; *p != ZERO; p = skipwhite(p)) {
      entryCount++;
      while (*p != '\n' && *p != ZERO)
         ++p;
      if (*p != ZERO)
         ++p;
   }
   if (entryCount == 0) {
      //Can happen when using /bin/sh and typing ":e $NO_SUCH_VAR^I".
      ///bin/sh will happily expand it to nothing rather than returning an
      //error; and hey, it's good to check anyway -- webb.
      eeglFree(buf);
      goto notfound;
   }
   matches->len = entryCount;
   matches->c = ALLOC_MULT(CS, entryCount);

   // Isolate the individual file names.
   CS p = buf;
   for (Unt i = 0; i < matches->len; ++i) {
      matches->c[i] = p;
      while (*p != '\n' && *p != ZERO)
         ++p;
      if (p == buf + len)      // last entry
         *p = ZERO;
      else {
         *p++ = ZERO;
         p = skipwhite(p);      // skip to next entry
      }
   }

   // Move the file names to allocated memory.
   Unt j = 0;
   for (Unt i = 0; i < matches->len; ++i) {
      // Require the files to exist.   Helps when using /bin/sh
      if (!(flags & EW_NOTFOUND) && mch_getperm(matches->c[i]) < 0)
         continue;

      // check if this entry should be included
      int dir = (mch_isdir(matches->c[i]));
      if ((dir && !(flags & EW_DIR)) || (!dir && !(flags & EW_FILE)))
          continue;

      // Skip files that are not executable if we check for that.
      if (!dir && (flags & EW_EXEC) && !mch_can_exe(matches->c[i], NULL, !(flags & EW_SHELLCMD)))
         continue;

      p = alloc(STRLEN(matches->c[i]) + 1 + dir);
      STRCPY(p, matches->c[i]);
      if (dir)
         add_pathsep(p);       // add '/' to a directory name
      matches->c[j++] = p;
   }
   eeglFree(buf);
   matches->len = j;

   if (matches->len == 0) {      // rejected all entries
      EE_CLEAR(matches);
      goto notfound;
   }

   return OK;

notfound:
   if (flags & EW_NOTFOUND)
      return save_patterns(num_pat, pat, OUT matches);
   return FAIL;
}

//Expand "file" for all comma-separated directories in "path".
//Add the matches to "matches". Caller must init "matches". 
void
fiGlobpath(
   CS path,
   CS file,
   OUT ExpandMatch* matches,
   Unt expand_options,
   Boole onlyDirs
){
   Expand xp = {};
   xp.files.a = matches->a;
   Unt pathlen; // length of the path portion of buf (including trailing slash)

   expandInit(&xp);
   xp.context = onlyDirs ? EXPAND_DIRECTORIES : EXPAND_FILES;

   Byte buf[MAXPATHL];
   Unt filelen = STRLEN(file);

   // Loop over all entries in {path}.
   while (*path != ZERO) {
      // Copy one item of the path to buf[] and concatenate the file name.
      pathlen = (Unt)copy_option_part(&path, buf, MAXPATHL, ",");
      Unt seplen = (*buf != ZERO && !after_pathsep(buf, buf + pathlen)) ? 1 : 0;

      if (pathlen + seplen + filelen + 1 <= MAXPATHL) {
         if (seplen > 0) {
            buf[pathlen] = '/';
            pathlen++;
         }
         STRCPY(buf + pathlen, file);

         ExpandMatch matches = {};
         matches.a = xp.files.a;
         if (expandFromContext(OUT &xp, buf, WILD_SILENT|expand_options, OUT &matches) != FAIL 
               && matches.len > 0
         ) {
            expandEscape(OUT &xp, buf, WILD_SILENT|expand_options, OUT &matches);
         }
      }
   }
}

//}}}
//{{{reading files

private int readdirex_sort;

int
mch_chdir(char *path) {
   if (p_verbose >= 5) {
      verbose_enter();
      smsg("chdir(%s)", path);
      verbose_leave();
   }
   return chdir(path);
}

void
filemess(Book* book, CS name, CS s, int attr){
   int msg_scroll_save;
   int prevMsgCol = msgColG;

   if (msg_silent != 0)
      return;
   msg_add_fname(book, name);       // put file name in IObuff with quotes

   // If it's extremely long, truncate it.
   Unt len = STRLEN(IObuff);
   if (len > IOSIZE - 100) {
      len = IOSIZE - 100;
      IObuff[len] = ZERO;
   }

   // Avoid an over-long translation to cause trouble.
   if (*s != ZERO)
      STRNCPY(IObuff + len, s, 99);

   //For the first message may have to start a new line. For further ones overwrite the previous 
   //one, reset msg_scroll before calling filemess().
   msg_scroll_save = msg_scroll;
   if (!isExitingG && p_verbose == 0)
      msg_scroll = FALSE;
   if (!msg_scroll)   // wait a bit when overwriting an error msg
      check_for_delay(FALSE);
   msg_start();
   if (prevMsgCol != 0 && msgColG == 0)
      msg_putchar('\r');  // overwrite any previous message.
   msg_scroll = msg_scroll_save;
   msg_scrolled_ign = TRUE;
   //may truncate the message to avoid a hit-return prompt
   msgOuttransDeco(msg_may_trunc(IObuff), attr);
   msg_clr_eos();
   out_flush();
   msg_scrolled_ign = FALSE;
}

//Read lines from file "fname" into the book after line "from".
//
//1. We allocate blocks with lalloc, as big as possible.
//2. Each block is filled with characters from the file with a single read().
//3. The lines are inserted in the book with ml_append().
//
//(caller must check that fname != NULL, unless READ_STDIN is used)
//
//"lines_to_skip" is the number of lines that must be skipped
//"lines_to_read" is the number of lines that are appended
//When not recovering lines_to_skip is 0 and lines_to_read MAXLNUM.
//
//flags:
//READ_NEW   starting to edit a new book
//READ_FILTER   reading filter output
//READ_STDIN   read from stdin instead of a file
//READ_BOOK   read from curBook instead of a file (converting after reading
//     stdin)
//READ_NOFILE   do not read a file, only trigger BufReadCmd
//READ_DUMMY   read into a dummy book (to check if file contents changed)
//READ_KEEP_UNDO  don't clear undo info or read it from a file
//READ_FIFO   read from fifo/socket instead of a file
//
//return FAIL for failure, NOTDONE for directory (failure), or OK
int
readfile(
   CS fname,
   CS sfname,
   LineNr from,
   LineNr lines_to_skip,
   LineNr lines_to_read,
   Invocation* invo,         // can be NULL!
   Unt flags
){
   int retval = FAIL;   // jump to "theend" instead of returning
   int fd = 0;
   int newfile = (flags & READ_NEW);
   int filtering = (flags & READ_FILTER);
   int read_stdin = (flags & READ_STDIN);
   int read_buffer = (flags & READ_BOOK);
   int read_fifo = (flags & READ_FIFO);
   int set_options = newfile || read_buffer || (invo && invo->read_edit);
   LineNr   read_buf_lnum = 1;   // next line to read from curBook
   ColNr   read_buf_col = 0;   // next char to read from this line
   Byte   c;
   LineNr   lnum = from;
   CS ptr = NULL;      // pointer into read buffer
   CS buffer = NULL;      // read buffer
   CS nebuffer = NULL;   // init to shut up gcc
   CS line_start = NULL;   // init to shut up gcc
   int      wasempty;      // buffer was empty before reading
   ColNr   len;
   long   size = 0;
   CS p;
   FileSize   filesize = 0;
   int      skip_read = FALSE;
   ContextSha256 sha_ctx;
   int      read_undo_file = FALSE;
   int      split = 0;      // number of split lines
#define UNKNOWN    0x0fffffff      // file size is unknown
   LineNr   linecnt;
   int      error = FALSE;      // errors encountered
   long   linerest = 0;      // remaining chars in line
   int      perm = 0;
   int      swap_mode = -1;      // protection bits for swap file
   FileStat   st;
   LineNr skip_count = 0;
   LineNr read_count = 0;
   int msg_save = msg_scroll;
   LineNr read_no_eol_lnum = 0;   // non-zero lnum when last line of
               // last read was missing the eol
   int file_rewind = FALSE;
   LineNr illegal_byte = 0;   // line nr with illegal byte
   int bad_char_behavior = BAD_REPLACE; // BAD_KEEP, BAD_DROP or character to replace with
   int converted = FALSE;   // TRUE if conversion done
   int notconverted = FALSE;   // TRUE if conversion wanted but it wasn't possible
   Pos  orig_start;
   Book* old_curbuf;
   static CS msg_is_a_directory = S"is a directory";
   Unt fnamelen = 0;

   curBook->auDidFileType = FALSE; // reset before triggering any autocommands
   curBook->noEolLnum = 0;   // in case it was set by the previous read

   // Remember the initial values of curBook, curBook->fullFileName and
   // curBook->currFileName to detect whether they are altered as a result of
   // executing nasty autocommands.  Also check if "fname" and "sfname"
   // point to one of these values.
   old_curbuf = curBook;
   CS old_fullFileName = curBook->fullFileName;
   CS old_currFileName = curBook->currFileName;
   int using_fullFileName = (fname == curBook->fullFileName)
                     || (sfname == curBook->fullFileName);
   int using_currFileName = (fname == curBook->currFileName) || (sfname == curBook->currFileName);

   // After reading a file the cursor line changes but we don't want to display the line.
   ex_no_reprint = TRUE;

   // don't display the file info for another buffer now
   needFileinfoG = FALSE;

   //For Unix: Use the short file name whenever possible.
   //Avoids problems with networks and when directory names are changed.
   if (sfname == NULL)
      sfname = fname;
   fname = sfname;

   //The BufReadCmd and FileReadCmd events intercept the reading process by
   //executing the associated commands instead.
   if (!filtering && !read_stdin && !read_buffer) {
      orig_start = curBook->opStart;

      // Set '[ mark to the line above where the lines go (line 1 if zero).
      curBook->opStart.lnum = ((from == 0) ? 1 : from);
      curBook->opStart.col = 0;

      if (newfile) {
         if (auCommApplyWithInvo(EVENT_BUFREADCMD, NULL, sfname, FALSE, curBook, invo)) {
            retval = OK;
            if (aborting())
                retval = FAIL;
            // The BufReadCmd code usually uses ":read" to get the text and
            // perhaps ":file" to change the buffer name. But we should
            // consider this to work like ":edit", thus reset the
            // BF_NOTEDITED flag.  Then ":write" will work to overwrite the
            // same file.
            if (retval == OK)
               curBook->flags &= ~BF_NOTEDITED;
            goto theend;
         }
      } ei (auCommApplyWithInvo(EVENT_FILEREADCMD, sfname, sfname, FALSE, NULL, invo)) {
          retval = aborting() ? FAIL : OK;
          goto theend;
      }

      curBook->opStart = orig_start;

      if (flags & READ_NOFILE) {
          // Return NOTDONE instead of FAIL so that BufEnter can be triggered
          // and other operations don't fail.
          retval = NOTDONE;
          goto theend;
      }
   }

   if (p_verbose == 0)
      msg_scroll = FALSE;   // overwrite previous file message
   else
      msg_scroll = TRUE;   // don't overwrite previous file message

   if (fname && *fname != ZERO) {
      fnamelen = STRLEN(fname);

      // If the name is too long we might crash further on, quit here.
      if (fnamelen >= MAXPATHL) {
         filemess(curBook, fname, (CS)_("Illegal file name"), 0);
         msg_end();
         msg_scroll = msg_save;
         goto theend;
      }

      // If the name ends in a path separator, we can't open it.  Check here,
      // because reading the file may actually work, but then creating the
      // swap file may destroy it!  Reported on MS-DOS and Win 95.
      if (after_pathsep(fname, fname + fnamelen)) {
         filemess(curBook, fname, (CS)_(msg_is_a_directory), 0);
         msg_end();
         msg_scroll = msg_save;
         retval = NOTDONE;
         goto theend;
      }
   }
   if (!read_stdin && fname)
      perm = mch_getperm(fname);

   if (!read_stdin && !read_buffer && !read_fifo) {
      if (perm >= 0 && !S_ISREG(perm)          // not a regular file ...
               && !S_ISFIFO(perm)       // ... or fifo
               && !S_ISSOCK(perm)       // ... or socket
# ifdef OPEN_CHR_FILES
               && !(S_ISCHR(perm) && is_dev_fd_file(fname))
            // ... or a character special file named /dev/fd/<n>
# endif
                     )
      {
         //On Unix it is possible to read a directory, so we have to check for it before the open()
         if (S_ISDIR(perm)) {
            filemess(curBook, fname, (CS)_(msg_is_a_directory), 0);
            retval = NOTDONE;
         } else
            filemess(curBook, fname, (CS)_("is not a file"), 0);
         msg_end();
         msg_scroll = msg_save;
         goto theend;
      }
   }

   //Set default or forced @binary.
   set_file_options(invo);

   //When opening a new file we take the modifiable flag from the file.
   //Default is r/w, can be set to r/o below. Don't reset it when in immutable mode.
   //Only set/reset immutable when BF_CHECK_RO is set.
   Boole check_readonly = (newfile && (curBook->flags & BF_CHECK_RO));
   if (check_readonly && !optImmutableMode())
      curBook->o.modifiable = true;

   if (newfile && !read_stdin && !read_buffer && !read_fifo) {
      // Remember time of file.
      if (stat((char *)fname, &st) >= 0) {
         buf_store_time(curBook, &st, fname);
         curBook->readTime = curBook->modifiedTime;
         curBook->readTimeNs = curBook->modifiedTimeNs;
         //Use the protection bits of the original file for the swap file. This makes it possible 
         //for others to read the name of the edited file from the swapfile, but only if they can 
         //read the edited file. Remove the "write" and "execute" bits for group and others
         //(they must not write the swapfile). Add the "read" and "write" bits for the user, 
         //otherwise we may not be able to write to the file ourselves.
         //Setting the bits is done below, after creating the swap file.
         swap_mode = (st.st_mode & 0644) | 0600;
      } else {
          curBook->modifiedTime = 0;
          curBook->modifiedTimeNs = 0;
          curBook->readTime = 0;
          curBook->readTimeNs = 0;
          curBook->origSize = 0;
          curBook->origMode = 0;
      }

      // Reset the "new file" flag.  It will be set again below when the file doesn't exist.
      curBook->flags &= ~(BF_NEW | BF_NEW_W);
   }

   //check readonly with perm and mch_access()
   Boole file_readonly = false;
   
   if (read_stdin) {
   } ei (!read_buffer) {
      if ( !(perm & 0222) || mch_access(fname, W_OK))
         file_readonly = true;
      fd = open((char *)fname, O_RDONLY | O_EXTRA, 0);
   }

   if (fd < 0) { //{{{ cannot open at all
      msg_scroll = msg_save;
         if (newfile) {
            if (perm < 0
#ifdef ENOENT
               && errno == ENOENT
#endif
            ){
               //Set the 'new-file' flag, so that when the file has
               //been created by someone else, a ":w" will complain.
               curBook->flags |= BF_NEW;

               // Create a swap file now, so that other Eegls are warned
               // that we are editing this file.  Don't do this for a
               // "nofile" or "nowrite" book type.
               if (!bt_dontwrite(curBook)) {
                  check_need_swap(newfile);
                  // SwapExists autocommand may mess things up
                  if (curBook != old_curbuf
                     || (using_fullFileName && (old_fullFileName != curBook->fullFileName))
                     || (using_currFileName && (old_currFileName != curBook->currFileName))
                  ) {
                     emsg(_(e_autocommands_changed_buffer_or_buffer_name));
                     goto theend;
                  }
               }
               if (dir_of_file_exists(fname))
                  filemess(curBook, sfname, (CS)new_file_message(), 0);
               else
                  filemess(curBook, sfname, (CS)_("[New DIRECTORY]"), 0);
               // Even though this is a new file, it might have been
               // edited before and deleted.  Get the old marks.
               check_marks_read();
               auCommApplyWithInvo(EVENT_BUFNEWFILE, sfname, sfname, FALSE, curBook, invo);

               if (!aborting())   // autocmds may abort script processing
                  retval = OK;       // a new file is not an error
               goto theend;
            } else {
                filemess(curBook, sfname, (CS)(
# ifdef EFBIG
                   (errno == EFBIG) ? _("[File too big]") :
# endif
# ifdef EOVERFLOW
                   (errno == EOVERFLOW) ? _("[File too big]") :
# endif
                        _("[Permission Denied]")), 0);
                curBook->o.modifiable = false;
            }
          }

      goto theend;
    } //}}}

   //Only set the 'ro' flag for readonly files the first time they are
   //loaded. Help files always get readonly mode
   if ((check_readonly && file_readonly) || curBook->kind == BOOK_HELP) {
      curBook->o.modifiable = false;
   } 

   if (set_options) {
      // Don't change 'eol' if reading from buffer as it will already be
      // correctly set when reading stdin.
      if (!read_buffer) {
          curBook->startEof = FALSE;
          curBook->startEol = TRUE;
      }
   }

   // Create a swap file now, so that other Eegls are warned that we are editing this file.
   // Don't do this for a "nofile" or "nowrite" buffer type.
   if (!bt_dontwrite(curBook)) {
      check_need_swap(newfile);
      if (!read_stdin && (curBook != old_curbuf
            || (using_fullFileName && (old_fullFileName != curBook->fullFileName))
            || (using_currFileName && (old_currFileName != curBook->currFileName)))) {
         emsg(_(e_autocommands_changed_buffer_or_buffer_name));
         if (!read_buffer)
            close(fd);
         goto theend;
      }
      //Set swap file protection bits after creating it.
      if (swap_mode > 0 && curBook->mem.mfile != NULL && curBook->mem.mfile->fName != NULL) {
         CS swap_fname = curBook->mem.mfile->fName;

         //If the group-read bit is set but not the world-read bit, then the group must be equal 
         //to the group of the original file.  If we can't make that happen then reset the 
         //group-read bit. This avoids making the swap file readable to more users when the
         //primary group of the user is too permissive.
         if ((swap_mode & 044) == 040) {
            FileStat   swap_st;

            if (stat((char *)swap_fname, &swap_st) >= 0
                  && st.st_gid != swap_st.st_gid
                  && fchown(curBook->mem.mfile->fd, -1, st.st_gid) == -1
            )
               swap_mode &= 0600;
         }

         (void)mch_setperm(swap_fname, (long)swap_mode);
      }
   }

   // If "Quit" selected at ATTENTION dialog, don't load the file
   if (swap_exists_action == SEA_QUIT) {
      if (!read_buffer && !read_stdin)
         close(fd);
      goto theend;
   }

   ++no_wait_return;       // don't wait for return yet

   // Set '[ mark to the line above where the lines go (line 1 if zero).
   orig_start = curBook->opStart;
   curBook->opStart.lnum = ((from == 0) ? 1 : from);
   curBook->opStart.col = 0;

   if (!read_buffer) {
      int m = msg_scroll;
      int n = msg_scrolled;

      //The file must be closed again, autocommands may want to change the file before reading it
      if (!read_stdin)
         close(fd);      // ignore errors

      //The output from the autocommands should not overwrite anything and should not be 
      //overwritten: Set msg_scroll, restore its value if no output was done.
      msg_scroll = TRUE;
      if (filtering)
         auCommApplyWithInvo(EVENT_FILTERREADPRE, NULL, sfname, FALSE, curBook, invo);
      ei (newfile)
         auCommApplyWithInvo(EVENT_BUFREADPRE, NULL, sfname, FALSE, curBook, invo);
      else
         auCommApplyWithInvo(EVENT_FILEREADPRE, sfname, sfname, FALSE, NULL, invo);
      curBook->opStart = orig_start;

      if (msg_scrolled == n)
         msg_scroll = m;

      if (aborting()) {  // autocmds may abort script processing
         --no_wait_return;
         msg_scroll = msg_save;
         curBook->o.modifiable = false;
         goto theend;
      }
      //Don't allow the autocommands to change the current book. Try to re-open the file.
      //Don't allow the autocommands to change the book name either
      //(cd for example) if it invalidates fname or sfname.
      if (!read_stdin && (curBook != old_curbuf
         || (using_fullFileName && (old_fullFileName != curBook->fullFileName))
         || (using_currFileName && (old_currFileName != curBook->currFileName))
         || (fd = open((char *)fname, O_RDONLY | O_EXTRA, 0)) < 0))
      {
         --no_wait_return;
         msg_scroll = msg_save;
         if (fd < 0)
            emsg(_(e_readpre_autocommands_made_file_unreadable));
         else
            emsg(_(e_readpre_autocommands_must_not_change_current_buffer));
         curBook->o.modifiable = false;
         goto theend;
      }
   }

   // Autocommands may add lines to the file, need to check if it is empty
   wasempty = (curBook->mem.flags & ML_EMPTY);

   if (!recoveryModeG && !filtering && !(flags & READ_DUMMY)) {
   //Show the user that we are busy reading the input.  Sometimes this
   //may take a while.  When reading from stdin another program may
   //still be running, don't move the cursor to the last line, unless always using the GUI.
   if (read_stdin) {
      if (!is_not_a_term()) {
         mch_msg(_("Eegl: Reading from stdin...\n"));
      }
   } ei (!read_buffer)
       filemess(curBook, sfname, (CS)"", 0);
   }

   msg_scroll = FALSE;         // overwrite the file message

   // Set linecnt now, after the autocommands, which may change them.
   linecnt = curBook->mem.lineCount;

   // "++bad=" argument.
   if (invo && invo->bad_char != 0) {
      bad_char_behavior = invo->bad_char;
      if (set_options)
         curBook->badChar = invo->bad_char;
   } else
      curBook->badChar = 0;

   c = TRUE;

   if (file_rewind) {
      if (read_buffer) {
         read_buf_lnum = 1;
         read_buf_col = 0;
      } ei (read_stdin || lseek(fd, (FileSize)0L, SEEK_SET) != 0) {
         // Can't rewind the file, give up.
         error = TRUE;
         goto failed;
      }
      // Delete the previously read lines.
      while (lnum > from)
         ml_delete(lnum--);
      file_rewind = FALSE;
   }

   if (!skip_read) {
      linerest = 0;
      filesize = 0;
      skip_count = lines_to_skip;
      read_count = lines_to_read;
      read_undo_file = (newfile && (flags & READ_KEEP_UNDO) == 0
                 && curBook->fullFileName
                 && curBook->o.undoFile
                 && !filtering
                 && !read_fifo
                 && !read_stdin
                 && !read_buffer);
      if (read_undo_file)
          sha256_start(&sha_ctx);
   }

   while (!error && !gotInterruptG) {
      //We allocate as much space for the file as we can get, plus space for the old line plus 
      //room for one terminating ZERO. The amount is limited by the fact that read() only 
      //can read up to max_unsigned characters (and other things).
      if (!skip_read) {
#if defined(SSIZE_MAX) && (SSIZE_MAX < 0x10000L)
         size = SSIZE_MAX;          // use max I/O size, 52K
#else
         // Use buffer >= 64K.  Add linerest to double the size if the
         // line gets very long, to avoid a lot of copying. But don't
         // read more than 1 Mbyte at a time, so we can be interrupted.
         size = 0x10000L + linerest;
         if (size > 0x100000L)
            size = 0x100000L;
#endif
      }

      // Protect against the argument of lalloc() going negative.
      if (size < 0 || size + linerest + 1 < 0 || linerest >= MAXCOL) {
          ++split;
          *ptr = NL;          // split line by inserting a NL
          size = 1;
      } else {
         if (!skip_read) {
            for ( ; size >= 10; size = (long)((Ulong)size >> 1)) {
               if ((nebuffer = lalloc(size + linerest + 1, FALSE)) != NULL)
                  break;
            }
            if (!nebuffer) {
               do_outofmem_msg((Ulong)(size * 2 + linerest + 1));
               error = TRUE;
               break;
            }
            if (linerest)   // copy characters from the previous buffer
               mch_memmove(nebuffer, ptr - linerest, (Unt)linerest);
            eeglFree(buffer);
            buffer = nebuffer;
            ptr = buffer + linerest;
            line_start = buffer;

            if (read_buffer) {
               // Read bytes from curBook.  Used for converting text read from stdin.
               if (read_buf_lnum > from)
                  size = 0;
               else {
                  int   n, ni;

                  long tlen = 0;
                  for (;;) {
                     p = ml_get(read_buf_lnum) + read_buf_col;
                     n = ml_get_len(read_buf_lnum) - read_buf_col;
                     if ((int)tlen + n + 1 > size) {
                        // Filled up to "size", append partial line.
                        // Change NL to ZERO to reverse the effect done below.
                        n = (int)(size - tlen);
                        for (ni = 0; ni < n; ++ni) {
                           if (p[ni] == NL)
                              ptr[tlen++] = ZERO;
                           else
                              ptr[tlen++] = p[ni];
                        }
                        read_buf_col += n;
                        break;
                     }

                     // Append whole line and new-line.  Change NL
                     // to ZERO to reverse the effect done below.
                     for (ni = 0; ni < n; ++ni) {
                        if (p[ni] == NL)
                           ptr[tlen++] = ZERO;
                        else
                           ptr[tlen++] = p[ni];
                     }
                     ptr[tlen++] = NL;
                     read_buf_col = 0;
                     if (++read_buf_lnum > from) {
                        size = tlen;
                        break;
                     }
                  }
               }
            } else {
               //Read bytes from the file.
               long read_size = size;
               size = read_eintr(fd, ptr, read_size);
               // Did we reach end of file?
            }

            if (size < 0) {          // read error
               error = TRUE;
            }
         }
         skip_read = FALSE;

         // Break here for a read error or end-of-file.
         if (size <= 0)
            break;

      --ptr;
      while (++ptr, --size >= 0) {
         if ((c = *ptr) != ZERO && c != NL)  // catch most common case
            continue;
         if (c == ZERO)
            *ptr = NL;   // NULs are replaced by newlines!
         else {
            if (skip_count == 0) {
               *ptr = ZERO;      // end of line
               len = (ColNr)(ptr - line_start + 1);
               if (ml_append(lnum, line_start, len, newfile) == FAIL) {
                  error = TRUE;
                  break;
               }
               if (read_undo_file)
                  sha256_update(&sha_ctx, line_start, len);
               ++lnum;
               if (--read_count == 0) {
                  error = TRUE;       // break loop
                  line_start = ptr;   // nothing left to write
                  break;
               }
            } else
               --skip_count;
            line_start = ptr + 1;
         }
      }
      
      linerest = (long)(ptr - line_start);
      ui_breakcheck();
      }
   }

failed:
   // not an error, max. number of lines reached
   if (error && read_count == 0)
      error = FALSE;

   // If we get EOF in the middle of a line, note the fact by resetting
   // 'endofline' and add the line normally.
   if (!error && !gotInterruptG && linerest != 0) {
      // remember for when writing
      *ptr = ZERO;
      len = (ColNr)(ptr - line_start + 1);
      if (ml_append(lnum, line_start, len, newfile) == FAIL)
         error = TRUE;
      else {
         if (read_undo_file)
            sha256_update(&sha_ctx, line_start, len);
         read_no_eol_lnum = ++lnum;
      }
   }

   if (!read_buffer && !read_stdin)
      close(fd);            // errors are ignored
   else {
      int fdflags = fcntl(fd, F_GETFD);

      if (fdflags >= 0 && (fdflags & FD_CLOEXEC) == 0)
         (void)fcntl(fd, F_SETFD, fdflags | FD_CLOEXEC);
   }
   eeglFree(buffer);

#ifdef HAVE_DUP
   if (read_stdin) {
      // Use stderr for stdin, makes shell commands work.
      close(0);
      (void)dup(2);
   }
#endif

   --no_wait_return;         // may wait for return now

   if (recoveryModeG) { //  In recovery mode everything but autocommands is skipped.
      goto afterRecovery;
   }
   
   // need to delete the last line, which comes from the empty book
   if (newfile && wasempty && !(curBook->mem.flags & ML_EMPTY)) {
       ml_delete(curBook->mem.lineCount);
       --linecnt;
   }
   linecnt = curBook->mem.lineCount - linecnt;
   if (filesize == 0)
      linecnt = 0;
   if (newfile || read_buffer) {
      drawCurBookLater(UPD_NOT_VALID);
      // After reading the text into the buffer the diff info needs to be updated.
      diff_invalidate(curBook);
      // All folds in the portal are invalid now. Mark them for update before triggering autocomms
      foldUpdateAll(curPor);
   } ei (linecnt)      // appended at least one line
      appended_lines_mark(from, linecnt);

   //If we were reading from the same terminal as where messages go, the screen will have been 
   //messed up. Switch on raw mode now and clear the screen.
   if (read_stdin) {
      termSetMode(TMODE_RAW);   // set to raw mode
      starttermcap();
      screenclear();
   }

   if (gotInterruptG) {
      if (!(flags & READ_DUMMY)) {
         filemess(curBook, sfname, (CS)_(e_interrupted), 0);
         if (newfile)
             curBook->o.modifiable = false;
      }
      msg_scroll = msg_save;
      check_marks_read();
      retval = OK;   // an interrupt isn't really an error
      goto theend;
   }

   if (!filtering && !(flags & READ_DUMMY)) {
      msg_add_fname(curBook, sfname);   // fname in IObuff with quotes
      c = FALSE;

      int buflen = (int)STRLEN(IObuff);
      if (S_ISFIFO(perm)) {            // fifo
         buflen += eeSnprintf(IObuff + buflen, IOSIZE - buflen, _("[fifo]"));
         c = TRUE;
      }
      if (S_ISSOCK(perm)) {            // or socket
         buflen += eeSnprintf(IObuff + buflen, IOSIZE - buflen, _("[socket]"));
         c = TRUE;
      }
#ifdef OPEN_CHR_FILES
      if (S_ISCHR(perm)) {            // or character special
         buflen += eeSnprintf(IObuff + buflen, IOSIZE - buflen, _("[character special]"));
         c = TRUE;
      }
#endif
      if (!curBook->o.modifiable) {
         buflen += eeSnprintf(IObuff + buflen, IOSIZE - buflen, "[-]");
         c = TRUE;
      }
      if (read_no_eol_lnum) {
         msg_add_eol();
         c = TRUE;
      }
      if (split) {
         buflen += eeSnprintf(IObuff + buflen, IOSIZE - buflen, _("[long lines split]"));
         c = TRUE;
      }
      if (notconverted) {
         buflen += eeSnprintf(IObuff + buflen, IOSIZE - buflen, _("[NOT converted]"));
         c = TRUE;
      } ei (converted) {
         buflen += eeSnprintf(IObuff + buflen, IOSIZE - buflen, _("[converted]"));
         c = TRUE;
      }
      if (illegal_byte > 0) {
         eeSnprintf(IObuff + buflen, IOSIZE - buflen,
            _("[ILLEGAL BYTE in line %ld]"), (long)illegal_byte);
         c = TRUE;
      } ei (error) {
         eeSnprintf(IObuff + buflen, IOSIZE - buflen, _("[READ ERRORS]"));
         c = TRUE;
      }
      msg_add_lines(c, (long)linecnt, filesize);

      EE_CLEAR(msgAfterRedrawG);
      msg_scrolled_ign = TRUE;
      {
      if (msgColG > 0)
         msg_putchar('\r');  // overwrite previous message
      p = (CS)msgTruncDeco(IObuff, 0);
      }
      if (read_stdin || read_buffer || restart_edit != 0
          || (msg_scrolled != 0 && !need_wait_return))
      // Need to repeat the message after redrawing when:
      // - When reading from stdin (the screen will be cleared next).
      // - When restart_edit is set (otherwise there will be a delay
      //   before redrawing).
      // - When the screen was scrolled but there is no wait-return prompt.
      set_keep_msg(p, 0);
      msg_scrolled_ign = FALSE;
   }

   // with errors writing the file requires ":w!"
   if (newfile && (error || (illegal_byte > 0 && bad_char_behavior != BAD_KEEP)))
      curBook->o.modifiable = false;

   u_clearline();       // cannot use "U" command after adding lines

   // cursor at first new line.
   curPor->cursor.lnum = from + 1;
   check_cursor_lnum();
   beginline(BL_WHITE | BL_FIX);       // on first non-blank

   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
      // Set '[ and '] marks to the newly read lines.
      curBook->opStart.lnum = from + 1;
      curBook->opStart.col = 0;
      curBook->opEnd.lnum = from + linecnt;
      curBook->opEnd.col = 0;
   }

afterRecovery: 
   msg_scroll = msg_save;

   // Get the marks before executing autocommands, so they can be used there.
   check_marks_read();

   //We remember if the last line of the read didn't have
   //an eol even when 'binary' is off, to support turning 'fixeol' off,
   //or writing the read again with 'binary' on.  The latter is required
   //for ":autocmd FileReadPost *.gz set bin|'[,']!gunzip" to work.
   curBook->noEolLnum = read_no_eol_lnum;

   // When reloading a buffer put the cursor at the first line that is different.
   if ((flags & READ_KEEP_UNDO) != 0)
      u_find_first_changed();

   // When opening a new file locate undo info and read it.
   if (read_undo_file) {
      Byte hash[UNDO_HASH_SIZE];
      sha256_finish(&sha_ctx, hash);
      u_read_undo(NULL, hash, fname);
   }

   if (!read_stdin && !read_fifo && (!read_buffer || sfname != NULL)) {
      int m = msg_scroll;
      int n = msg_scrolled;

      //The output from the autocommands should not overwrite anything and
      //should not be overwritten: Set msg_scroll, restore its value if no output was done.
      msg_scroll = TRUE;
      if (filtering)
         auCommApplyWithInvo(EVENT_FILTERREADPOST, NULL, sfname, FALSE, curBook, invo);
      ei (newfile || (read_buffer && sfname != NULL)) {
         auCommApplyWithInvo(EVENT_BUFREADPOST, NULL, sfname,
                          FALSE, curBook, invo);
         if (!curBook->auDidFileType && *curBook->fileType != ZERO)
            //EVENT_FILETYPE was not triggered but the book already has a
            //filetype. Trigger EVENT_FILETYPE using the existing filetype.
            applyAutocomms(EVENT_FILETYPE, curBook->fileType, curBook->currFileName, TRUE, curBook);
      } else
         auCommApplyWithInvo(EVENT_FILEREADPOST, sfname, sfname, FALSE, NULL, invo);
      if (msg_scrolled == n)
         msg_scroll = m;
      if (aborting())       // autocmds may abort script processing
         goto theend;
   }

   if (!(recoveryModeG && error))
      retval = OK;

theend:
   if (curBook->mem.mfile != NULL && curBook->mem.mfile->mf_dirty == MF_DIRTY_YES_NOSYNC)
      // OK to sync the swap file now
      curBook->mem.mfile->mf_dirty = MF_DIRTY_YES;

   return retval;
}

//}}}
//{{{blob i/o

// Read blob from file "fd". Caller has allocated a blob in "returnVar". Return OK or FAIL.
int
read_blob(FILE* fd, Var* returnVar, FileSize offset, FileSize size_arg) {
   Blob* blob = returnVar->blob;
   struct stat st;
   int whence;
   FileSize   size = size_arg;

   if (fstat(fileno(fd), &st) < 0)
      return FAIL;  // can't read the file, error

   if (offset >= 0) {
      // The size defaults to the whole file.  If a size is given it is
      // limited to not go past the end of the file.
      if (size == -1 || (size > st.st_size - offset && !S_ISCHR(st.st_mode)))
         // size may become negative, checked below
         size = st.st_size - offset;
      whence = SEEK_SET;
   } else {
      // limit the offset to not go before the start of the file
      if (-offset > st.st_size && !S_ISCHR(st.st_mode))
         offset = -st.st_size;
      // Size defaults to reading until the end of the file.
      if (size == -1 || size > -offset)
         size = -offset;
      whence = SEEK_END;
   }
   if (size <= 0 || (offset != 0 && fseeko(fd, offset, whence) != 0))
      return OK;

   if (ga_grow(&blob->c, (int)size) == FAIL)
      return FAIL;
   blob->c.len = (int)size;
   if (fread(blob->c.c, 1, blob->c.len, fd) < (Unt)blob->c.len) {
      // An empty blob is returned on error.
      blob_free(returnVar->blob);
      returnVar->blob = NULL;
      return FAIL;
   }
   return OK;
}

// Write "blob" to file "fd". Return OK or FAIL.
int
write_blob(FILE* fd, Blob* blob) {
   if (fwrite(blob->c.c, 1, blob->c.len, fd) < (Unt)blob->c.len) {
      emsg(_(e_error_while_writing));
      return FAIL;
   }
   return OK;
}

//}}}
//{{{aux functions

#if defined(OPEN_CHR_FILES) || defined(PROTO)
//Return TRUE if the file name argument is of the form "/dev/fd/\d\+", which is the name of files 
//used for process substitution output by some shells on some operating systems, e.g., bash on 
//SunOS. Do not accept "/dev/fd/[012]", opening these may hang Eegl.
int
is_dev_fd_file(CS fname) {
   return STRNCMP(fname, "/dev/fd/", 8) == 0
       && EE_ISDIGIT(fname[8])
       && *skipdigits(fname + 9) == ZERO
       && (fname[9] != ZERO || (fname[8] != '0' && fname[8] != '1' && fname[8] != '2'));
}
#endif

//Fill "*invo" to force the 'binary' option to be equal to the book "book". 
//Used for calling readfile(). Return OK or FAIL.
int
prep_exarg(Invocation* invo, Book* book){
   invo->comm = alloc(15);
   invo->bad_char = book->badChar;
   invo->force_bin = book->o.binary ? FORCE_BIN : FORCE_NOBIN;
   invo->read_edit = false;
   invo->forceit = false;
   return OK;
}

// Set default or forced @binary.
void
set_file_options(Invocation* invo) {
   // set or reset @binary
   if (invo && invo->force_bin != 0) {
      OptionChange cha = (OptionChange){
            .ref = (OptionRef){.tag = OPTION_BOOLE, .boole = &curBook->o.binary},
            .oldVal = optBoole(curBook->o.binary),
            .newVal = optBoole(invo->force_bin == FORCE_BIN),
            .setScope = SET_LOCAL
            };
      optSetBinary(OUT &cha);
   }
}

// Return TRUE if a file appears to be read-only from the file permissions.
int
check_file_readonly(CS fname, Unt perm) {  // known permissions on file
   return ( (perm & 0222) == 0 || mch_access(fname, W_OK));
}

//Call fsync() with Mac-specific exception. Return fsync() result: zero for success.
int
eeFsync(int fd){
   int r = fsync(fd);
   return r;
}

//Set the name of the current book. Use when the book doesn't have a
//name and a ":r" or ":w" command with a file name is used.
int
set_rw_fname(CS fname, CS sfname){
   Book* book = curBook;

   // It's like the unnamed book is deleted....
   if (curBook->o.bookListed)
      applyAutocomms(EVENT_BUFDELETE, NULL, NULL, FALSE, curBook);
   applyAutocomms(EVENT_BUFWIPEOUT, NULL, NULL, FALSE, curBook);
   if (aborting())       // autocmds may abort script processing
      return FAIL;
   if (curBook != book) {
      // We are in another book now, don't do the renaming.
      emsg(_(e_autocommands_changed_buffer_or_buffer_name));
      return FAIL;
   }

   if (setfname(curBook, fname, sfname, FALSE) == OK)
      curBook->flags |= BF_NOTEDITED;

   // ....and a new named one is created
   applyAutocomms(EVENT_BUFNEW, NULL, NULL, FALSE, curBook);
   if (curBook->o.bookListed)
      applyAutocomms(EVENT_BUFADD, NULL, NULL, FALSE, curBook);
   if (aborting())       // autocmds may abort script processing
      return FAIL;

   // Do filetype detection now if 'filetype' is empty.
   if (*curBook->fileType == ZERO) {
      if (auGroupExists(S"filetypedetect"))
          (void)do_doautocmd(S"filetypedetect BufRead", FALSE, NULL);
   }

   return OK;
}

//Put file name into IObuff with quotes.
void
msg_add_fname(Book* book, CS fname){
   if (!fname)
      fname = S"-stdin-";
   home_replace(book, fname, IObuff + 1, IOSIZE - 4, TRUE);
   IObuff[0] = '"';
   STRCAT(IObuff, "\" ");
}

//Append line and character count to IObuff.
void
msg_add_lines(int insert_space, long lnum, FileSize nchars) {
   int  len = (int)STRLEN(IObuff);
   eeSnprintf(
      IObuff + len, IOSIZE - (Unt)len, _("%s%ldLines, %ldB"), insert_space ? " " : "", 
      lnum, (Long)nchars
   );
}

//Append message for missing line separator to IObuff.
void
msg_add_eol(void){
   STRCAT(IObuff, _("[Incomplete last line]"));
}

int
time_differs(FileStat* st, long mtime, long mtime_ns UNUSED){
   return
#ifdef ST_MTIM_NSEC
   (long)st->ST_MTIM_NSEC != mtime_ns ||
#endif
   // On a FAT filesystem, esp. under Linux, there are only 5 bits to store
   // the seconds. Since the roundoff is done when flushing the inode, the
   // time may change unexpectedly by one second!!!
   (long)st->st_mtime - mtime > 1 || mtime - (long)st->st_mtime > 1
   ;
}

//Try to find a shortname by comparing the fullname with the current directory.
//Return "full_path" or pointer into "full_path" if shortened.
CS
shorten_fname1(CS full_path){
   Byte dirname[MAXPATHL];
   CS p = full_path;
   if (mch_dirname(dirname, MAXPATHL) == OK) {
      p = shorten_fname(full_path, dirname);
      if (!p || *p == ZERO)
         p = full_path;
   }
   return p;
}

//Try to find a shortname by comparing the fullname with the current directory.
//Return NULL if not shorter name possible, pointer into "full_path" otherwise.
CS
shorten_fname(CS full_path, CS dir_name){
   CS p;

   if (full_path == NULL)
      return NULL;
   int len = (int)STRLEN(dir_name);
   if (fnamencmp(dir_name, full_path, len) == 0) {
      p = full_path + len;
      if (*p == '/')
         ++p;
      else
         p = NULL;
   } else
      p = NULL;
   return p;
}

//Shorten filename of a book.
//When "force" is TRUE: Use full path from now on for files currently being
//edited, both for file name and swap file name.  Try to shorten the file
//names a bit, if safe to do so.
//When "force" is FALSE: Only try to shorten absolute file names.
//For books that have buftype "nofile" or "scratch": never change the file name.
void
shorten_buf_fname(Book* book, CS dirname, int force) {
   if (book->currFileName
       && !bt_nofilename(book)
       && !path_with_url(book->currFileName)
       && (force || book->shortFileName == NULL || !fiIsRelative(book->shortFileName))
   ) {
      if (book->shortFileName != book->fullFileName)
         EE_CLEAR(book->shortFileName);
      CS p = shorten_fname(book->fullFileName, dirname);
      if (p) {
         book->shortFileName = copyStr(p);
         book->currFileName = book->shortFileName;
      }
      if (!p || book->currFileName == NULL)
         book->currFileName = book->fullFileName;
   }
}

// Shorten filenames for all books.
void
shorten_fnames(Boole force){
   Byte dirname[MAXPATHL];

   mch_dirname(dirname, MAXPATHL);
   Book* book;
   FOR_ALL_BOOKS(book) {
      shorten_buf_fname(book, dirname, force);

      // Always make the swap file name a full path, a "nofile" book may also have a swap file
      mf_fullname(book->mem.mfile);
   }
   status_redraw_all();
   needRedrawTabpanelG = TRUE;
   popup_update_preview_title();
}

//Add extension to file name - change path/fo.o.h to path/fo.o.h.ext
//
//Assumed that fname is a valid name found in the filesystem we assure that the return value is a 
//different name and ends in 'ext'. "ext" MUST be at most 4 characters long if it starts with a 
//dot, 3 characters otherwise. Space for the returned name is allocated, must be freed later.
//Return NULL when out of memory.
CS
fiAppendFileExtension(CS fname, CS ext, Boole prepend_dot) {  // may prepend a '.' to file name
   CS retval;
   CS s;
   CS e;
   CS ptr;
   int      fnamelen;

   int extlen = (int)STRLEN(ext);

   //If there is no file name we must get the name of the current directory
   //(we need the full path in case :cd is used).
   if (fname == NULL || *fname == ZERO) {
      retval = alloc(MAXPATHL + extlen + 3);
      if (mch_dirname(retval, MAXPATHL) == FAIL || (fnamelen = (int)STRLEN(retval)) == 0) {
         eeglFree(retval);
         return NULL;
      }
      if (!after_pathsep(retval, retval + fnamelen)) {
         retval[fnamelen] = '/';
         fnamelen++;
         retval[fnamelen] = ZERO;
      }
      prepend_dot = FALSE;       // nothing to prepend a dot to
   } else {
      fnamelen = (int)STRLEN(fname);
      retval = alloc(fnamelen + extlen + 3);
      STRCPY(retval, fname);
   }

   //search backwards until we hit a '/', '\' or ':'
   //Then truncate what is after the '/', '\' or ':'
   for (ptr = retval + fnamelen; ptr > retval; MB_PTR_BACK(retval, ptr)) {
      if (*ptr == '/') {
         ++ptr;
         break;
      }
   }

   // the file name has at most BASENAMELEN characters.
   Unt ptrlen = (Unt)(fnamelen - (ptr - retval));
   if (ptrlen > (unsigned)BASENAMELEN) {
      ptrlen = BASENAMELEN;
      ptr[ptrlen] = ZERO;
   }

   s = ptr + ptrlen;

   //Append the extension. ext can start with '.' and cannot exceed 3 more characters.
   STRCPY(s, ext);
   //Prepend the dot.
   if (prepend_dot && *(e = fiGetShortFiName(retval)) != '.') {
      mch_memmove(e + 1, e, (Unt)(((fnamelen + extlen) - (e - retval)) + 1));   // +1 for ZERO
      *e = '.';
   }

   //Check that, after appending the extension, the file name is really different.
   if (fname != NULL && STRCMP(fname, retval) == 0) {
      // we search for a character that can be replaced by '_'
      while (--s >= ptr) {
         if (*s != '_') {
            *s = '_';
            break;
         }
      }
      if (s < ptr)   // fname was "________.<ext>", how tricky!
         *ptr = 'v';
   }
   return retval;
}

//Like fgets(), but if the file line is too long, it is truncated and the rest of the line is 
//thrown away. Return TRUE for end-of-file. If the line is truncated then buf[size - 2] 
//will not be ZERO.
int
eeFgets(CS buf, int size, FILE *fp){
#define FGETS_SIZE 200
   Byte tbuilder[FGETS_SIZE];

   buf[size - 2] = ZERO;
   CS eof = (CS)fgets((char *)buf, size, fp);
   if (buf[size - 2] != ZERO && buf[size - 2] != '\n') {
      buf[size - 1] = ZERO;       // Truncate the line

      // Now throw away the rest of the line:
      do {
         tbuilder[FGETS_SIZE - 2] = ZERO;
         (void)fgets((char *)tbuilder, FGETS_SIZE, fp);
      } while (tbuilder[FGETS_SIZE - 2] != ZERO && tbuilder[FGETS_SIZE - 2] != '\n');
   }
   return (eof == NULL);
}

//rename() only works if both files are on the same file system, this
//function will (attempts to?) copy the file across if rename fails -- webb
//Return -1 for failure, 0 for success.
int
eeRename(CS from, CS to){
   int      n;
   int      ret;
   FileStat   st;
   int      use_tmp_file = FALSE;

   //When the names are identical, there is nothing to do.  When they refer
   //to the same file (ignoring case and slash/backslash differences) but
   //the file name differs we need to go through a temp file.
   if (fnamecmp(from, to) == 0) {
      return 0;
   }

   //Fail if the "from" file doesn't exist.  Avoids that "to" is deleted.
   if (stat((char *)from, &st) < 0)
      return -1;

   {
   FileStat   st_to;

   // It's possible for the source and destination to be the same file.
   // This happens when "from" and "to" differ in case and are on a FAT32
   // filesystem.  In that case go through a temp file name.
   if (stat((char *)to, &st_to) >= 0
      && st.st_dev == st_to.st_dev
      && st.st_ino == st_to.st_ino)
       use_tmp_file = TRUE;
   }

   if (use_tmp_file) {
      char   tempname[MAXPATHL + 1];

      //Find a name that doesn't exist and is in the same directory.
      //Rename "from" to "tempname" and then rename "tempname" to "to".
      if (STRLEN(from) >= MAXPATHL - 5)
         return -1;
      STRCPY(tempname, from);
      for (n = 123; n < 99999; ++n) {
         sprintf((char *)fiGetShortFiName((CS)tempname), "%d", n);
         if (stat(tempname, &st) < 0) {
            if (mch_rename((char *)from, tempname) == 0) {
               if (mch_rename(tempname, (char *)to) == 0)
                  return 0;
               //Strange, the second step failed.  Try moving the file back and return failure.
               (void)mch_rename(tempname, (char *)from);
               return -1;
            }
            // If it fails for one temp name it will most likely fail
            // for any temp name, give up.
            return -1;
          }
      }
      return -1;
   }

   //Delete the "to" file, this is required on some systems to make the mch_rename() work, on 
   //other systems it makes sure that we don't have two files when the mch_rename() fails.

   mch_remove(to);

   //First try a normal rename, return if it works.
   if (mch_rename((char *)from, (char *)to) == 0)
      return 0;

   // Rename() failed, try copying the file.
   ret = eeCopyfile(from, to);
   if (ret != OK)
      return -1;

   //Remove copied original file
   if (stat((char *)from, &st) >= 0)
      mch_remove(from);

   return 0;
}

//Create the new file with same permissions as the original. Return FAIL for failure, OK for success
private int
eeCopyfile(CS from, CS to){
   CS errmsg = NULL;
   Byte linkbuf[MAXPATHL + 1];

   FileStat st;
   int ret = lstat((char *)from, &st);
   if (ret >= 0 && S_ISLNK(st.st_mode)) {
      ret = -1;

      int len = readlink((char *)from, (char*)linkbuf, MAXPATHL);
      if (len > 0) {
         linkbuf[len] = ZERO;

         // Create link
         ret = symlink((char*)linkbuf, (char *)to);
      }

      return ret == 0 ? OK : FAIL;
   }

   long perm = mch_getperm(from);
   int fd_in = open((char *)from, O_RDONLY|O_EXTRA, 0);
   if (fd_in == -1) {
      return FAIL;
   }

   // Create the new file with same permissions as the original.
   int fd_out = open((char *)to, O_CREAT|O_EXCL|O_WRONLY|O_EXTRA|O_NOFOLLOW, (int)perm);
   if (fd_out == -1) {
      close(fd_in);
      return FAIL;
   }

   CS buf = alloc(WRITEBUFSIZE);
   int n;
   while ((n = read_eintr(fd_in, buf, WRITEBUFSIZE)) > 0) {
      if (write_eintr(fd_out, buf, n) != n) {
         errmsg = _(e_error_writing_to_str);
         break;
      }
   } 

   eeglFree(buf);
   close(fd_in);
   if (close(fd_out) < 0)
      errmsg = _(e_error_closing_str);
   if (n < 0) {
      errmsg = _(e_error_reading_str);
      to = from;
   }
   if (errmsg) {
      showErrFmtMsg(errmsg, to);
      return FAIL;
   }
   return OK;
}

private int already_warned = FALSE;

//{{{book work

//Check if any not hidden book has been changed.
//Postpone the check if there are characters in the stuff buffer, a global
//command is being executed, a mapping is being executed or an autocommand is busy.
//Return TRUE if some message was written (screen should be redrawn and cursor positioned).
int
check_timestamps( int      focus) {     // called for GUI focus event
   Book* book;
   int      didit = 0;
   int      n;

   // Don't check timestamps while system() or another low-level function may
   // cause us to lose and gain focus.
   if (no_check_timestamps > 0)
      return FALSE;

   // Avoid doing a check twice.  The OK/Reload dialog can cause a focus
   // event and we would keep on checking if the file is steadily growing.
   // Do check again after typing something.
   if (focus && did_check_timestamps) {
      need_check_timestamps = TRUE;
      return FALSE;
   }

   if (!stuff_empty() || global_busy || !typebuf_typed()
         || autocmd_busy || curBookLock > 0 || allBookLock > 0)
      need_check_timestamps = TRUE;      // check later
   else {
      ++no_wait_return;
      did_check_timestamps = TRUE;
      already_warned = FALSE;
      FOR_ALL_BOOKS(book) {
         // Only check books in a portal.
         if (book->countPortals > 0) {
            BookRef bufref;

            bookStoreInRef(OUT &bufref, book);
            n = fiCheckBookTimestamp(book);
            if (didit < n)
                didit = n;
            if (n > 0 && !bookRefValid(&bufref)) {
                // Autocommands have removed the book, start at the first one again.
                book = firstBook;
                continue;
            }
         }
      }
      --no_wait_return;
      need_check_timestamps = FALSE;
      if (need_wait_return && didit == 2) {
         // make sure msg isn't overwritten
         msg_puts(S"\n");
         out_flush();
      }
   }
   return didit;
}

//Move all the lines from buffer "frombuf" to buffer "tobuf".
//Return OK or FAIL.  When FAIL "tobuf" is incomplete and/or "frombuf" is not empty.
private int
move_lines(Book* frombuf, Book* tobuf) {
   Book* tbuf = curBook;
   int      retval = OK;
   LineNr   lnum;
   CS p;

   // Copy the lines in "frombuf" to "tobuf".
   curBook = tobuf;
   for (lnum = 1; lnum <= frombuf->mem.lineCount; ++lnum) {
      p = copySubstr(memGetLine(frombuf, lnum, false), memGetBookLen(frombuf, lnum));
      if (p == NULL || ml_append(lnum - 1, p, 0, FALSE) == FAIL) {
          eeglFree(p);
          retval = FAIL;
          break;
      }
      eeglFree(p);
   }

   // Delete all the lines in "frombuf".
   if (retval != FAIL) {
      curBook = frombuf;
      for (lnum = curBook->mem.lineCount; lnum > 0; --lnum)
         if (ml_delete(lnum) == FAIL) {
            // Oops!  We could try putting back the saved lines, but that might fail again...
            retval = FAIL;
            break;
         }
   }

   curBook = tbuf;
   return retval;
}

//Check if book "book" has been changed.
//Also check if the file for a new book unexpectedly appeared.
//return 1 if a changed book was found.
//return 2 if a message has been displayed.
//return 0 otherwise.
int
fiCheckBookTimestamp(
   Book* book
){
   FileStat   st;
   int      stat_res;
   int      retval = 0;
   CS mesg = NULL;
   CS mesg2 = Em;
   int      helpmesg = FALSE;
   enum {
      RELOAD_NONE,
      RELOAD_NORMAL,
      RELOAD_DETECT
   }      reload = RELOAD_NONE;
   int      can_reload = FALSE;
   FileSize   orig_size = book->origSize;
   int      orig_mode = book->origMode;
   static int   busy = FALSE;
   int      n;
   BookRef   bufref;

   bookStoreInRef(OUT &bufref, book);

   // If there is no file name, the book is not loaded, 'buftype' is
   // set, we are in the middle of a save or being called recursively: ignore this book.
   if (book->fullFileName == NULL
       || book->mem.mfile == NULL
       || !bt_normal(book)
       || book->isBeingSaved
       || busy
       || book->term != NULL
       )
   return 0;

   if (   (book->flags & BF_NOTEDITED) != 0
       && book->modifiedTime != 0
       && ((stat_res = stat((char *)book->fullFileName, &st)) < 0
      || time_differs(&st, book->modifiedTime, book->modifiedTimeNs)
      || st.st_size != book->origSize
      || (int)st.st_mode != book->origMode
      )
   ) {
      long prev_modifiedTime = book->modifiedTime;

      retval = 1;

      // set modifiedTime to stop further warnings (e.g., when executing FileChangedShell autocmd)
      if (stat_res < 0) {
         // Check the file again later to see if it re-appears.
         book->modifiedTime = -1;
         book->origSize = 0;
         book->origMode = 0;
      } else
         buf_store_time(book, &st, book->fullFileName);

      // Don't do anything for a directory.  Might contain the file explorer.
      if (mch_isdir(book->currFileName))
          ;

      ei (!doWasBookChanged(book) && stat_res >= 0)
         reload = RELOAD_NORMAL;
      else {
         CS reason;
         Unt  reasonlen;

         if (stat_res < 0) {
            reason = S"deleted";
            reasonlen = STRLEN_LITERAL("deleted");
         } ei (doWasBookChanged(book)) {
            reason = S"conflict";
            reasonlen = STRLEN_LITERAL("conflict");
         }
         //Check if the file contents really changed to avoid giving a warning when only the 
         //timestamp was set (e.g., checked out of CVS).  Always warn when the buffer was changed.
         ei (orig_size != book->origSize || buf_contents_changed(book)) {
            reason = S"changed";
            reasonlen = STRLEN_LITERAL("changed");
         } ei (orig_mode != book->origMode) {
            reason = S"mode";
            reasonlen = STRLEN_LITERAL("mode");
         } else {
            reason = S"time";
            reasonlen = STRLEN_LITERAL("time");
         }

         //Only give the warning if there are no FileChangedShell autocommands.
         //Avoid being called recursively by setting "busy".
         busy = TRUE;
         set_EeglVar_string(VV_FCS_REASON, reason, (int)reasonlen);
         set_EeglVar_string(VV_FCS_CHOICE, Em, 0);
         ++allBookLock;
         n = applyAutocomms(
               EVENT_FILECHANGEDSHELL, book->currFileName, book->currFileName, FALSE, book
         );
         --allBookLock;
         busy = FALSE;
         if (n) {
            if (!bookRefValid(&bufref))
                emsg(_(e_filechangedshell_autocommand_deleted_buffer));
            CS s = get_EeglVar_str(VV_FCS_CHOICE);
            if (STRCMP(s, "reload") == 0 && *reason != 'd')
               reload = RELOAD_NORMAL;
            ei (STRCMP(s, "edit") == 0)
               reload = RELOAD_DETECT;
            ei (STRCMP(s, "ask") == 0)
               n = FALSE;
            else
               return 2;
         }
         if (!n) {
            if (*reason == 'd') {
               // Only give the message once.
               if (prev_modifiedTime != -1)
                  mesg = _(e_file_str_no_longer_available);
            } else {
               helpmesg = TRUE;
               can_reload = TRUE;
               if (reason[2] == 'n') {
                  mesg = _("W12: Warning: File \"%s\" has changed and the buffer was changed in Eegl as well");
                  mesg2 = _("See \":help W12\" for more info.");
               } ei (reason[1] == 'h') {
                  mesg = _("W11: Warning: File \"%s\" has changed since editing started");
                  mesg2 = _("See \":help W11\" for more info.");
               } ei (*reason == 'm') {
                  mesg = _("W16: Warning: Mode of file \"%s\" has changed since editing started");
                  mesg2 = _("See \":help W16\" for more info.");
               } else {
                  // Only timestamp changed, store it to avoid a warning in check_mtime() later.
                  book->readTime = book->modifiedTime;
                  book->readTimeNs = book->modifiedTimeNs;
               }
            }
         }
      }

   } ei ((book->flags & BF_NEW) != 0 && (book->flags & BF_NEW_W) == 0 
         && eeFexists(book->fullFileName)
   ) {
      retval = 1;
      mesg = _("W13: Warning: File \"%s\" has been created after editing started");
      book->flags |= BF_NEW_W;
      can_reload = TRUE;
   }

   if (mesg) {
      CS path = home_replace_save(book, book->currFileName);
      if (path) {
         Unt  tbufsize;

         if (!helpmesg)
            mesg2 = E;
         tbufsize = STRLEN(mesg) + STRLEN(path) + 2 + STRLEN(mesg2) + 1; //+2 for "\n" or "; "
                                                                         // and +1 for ZERO
         CS tbuf = alloc(tbufsize);
         int tbuflen = eeSnprintf(tbuf, tbufsize, mesg, path);
         //Set warningmsg here, before the unimportant and output-specific mesg2 has been appended
         set_EeglVar_string(VV_WARNINGMSG, (CS)tbuf, tbuflen);
         if (can_reload) {
             if (*mesg2 != ZERO)
            eeSnprintf(tbuf + tbuflen, tbufsize - tbuflen, "\n%s", mesg2);
             switch (do_dialog(EE_WARNING, (CS)_("Warning"),
                (CS)tbuf,
                (CS)_("&OK\n&Load File\nLoad File &and Options"),
                1, NULL, TRUE))
             {
            case 2:
                reload = RELOAD_NORMAL;
                break;
            case 3:
                reload = RELOAD_DETECT;
                break;
             }
         } ei(stateG > MODE_NORMAL_BUSY || (stateG & MODE_COMMLINE) || already_warned) {
            if (*mesg2 != ZERO)
               eeSnprintf(tbuf + tbuflen, tbufsize - tbuflen, "; %s", mesg2);
            emsg(tbuf);
            retval = 2;
         } else {
             if (!autocmd_busy) {
            msg_start();
            msgPutsDeco(tbuf, getDecoFlags(HLF_E) + MSG_HIST);
            if (*mesg2 != ZERO)
                msgPutsDeco(mesg2, getDecoFlags(HLF_W) + MSG_HIST);
            msg_clr_eos();
            (void)msg_end();
            if (emsg_silent == 0 && !in_assert_fails) {
                out_flush();
               // give the user some time to think about it
               ui_delay(1004L, TRUE);

                // don't redraw and erase the message
                redrawCommlineG = FALSE;
            }
             }
             already_warned = TRUE;
         }

          eeglFree(tbuf);
          eeglFree(path);
      }
   }

   if (reload != RELOAD_NONE) {
      // Reload the book.
      buf_reload(book, orig_mode, reload == RELOAD_DETECT);
      if (book->o.undoFile && book->fullFileName != NULL) {
         Byte hash[UNDO_HASH_SIZE];
         Book* save_curbuf = curBook;

         // Any existing undo file is unusable, write it now.
         curBook = book;
         u_compute_hash(hash);
         u_write_undo(NULL, FALSE, book, hash);
         curBook = save_curbuf;
      }
   }

   // Trigger FileChangedShell when the file was changed in any way.
   if (bookRefValid(&bufref) && retval != 0) {
      (void)applyAutocomms(
            EVENT_FILECHANGEDSHELLPOST, book->currFileName, book->currFileName, FALSE, book
      );
   } 

   return retval;
}

//Reload a book that is already loaded. Used when the file was changed outside of Eegl.
//"orig_mode" is book->origMode before the need for reloading was detected.
//book->origMode may have been reset already.
void
buf_reload(Book* book, int orig_mode, int reload_options){
   Invocation invo;
   Pos   old_cursor;
   LineNr   old_topline;
   Boole  old_ro = book->o.modifiable;
   Book* savebuf;
   BookRef   bufref;
   int      saved = OK;
   AutocommSave   aco;
   int      flags = READ_NEW;
   int      prepped = OK;

   // Set curPor/curBook for "book" and save some things.
   auCommPrepareBook(&aco, book);
   if (curBook != book) {
      // Failed to find a window for "book", it is dangerous to continue, better bail out.
      return;
   }

   // Unless reload_options is set, we only want to read the text from the
   // file, not reset the syntax highlighting, clear marks, diff status, etc.
   // Force the "binary" option to be the same.
   if (reload_options)
      CLEAR_FIELD(invo);
   else
      prepped = prep_exarg(&invo, book);

   if (prepped == OK) {
   old_cursor = curPor->cursor;
   old_topline = curPor->topLine;

   if (p_ur < 0 || curBook->mem.lineCount <= p_ur) {
       // Save all the text, so that the reload can be undone.
       // Sync first so that this is a separate undo-able action.
       u_sync(FALSE);
       saved = u_savecommon(0, curBook->mem.lineCount + 1, 0, TRUE);
       flags |= READ_KEEP_UNDO;
   }

   //To behave like when a new file is edited (matters for BufReadPost autocommands) we first need 
   //to delete the current book contents. But if reading the file fails we should keep
   //the old contents. Can't use memory only, the file might be too big. Use a hidden book to 
   //move the book contents to.
   if (CURBOOK_EMPTY() || saved == FAIL)
      savebuf = NULL;
   else {
      // Allocate a book without putting it in the book list.
      savebuf = bookNew(NULL, NULL, (LineNr)1, BLN_DUMMY);
      bookStoreInRef(OUT &bufref, savebuf);
      if (savebuf != NULL && book == curBook) {
         // Open the memline.
         curBook = savebuf;
         curPor->book = savebuf;
         saved = ml_open(curBook);
         curBook = book;
         curPor->book = book;
      }
      if (savebuf == NULL || saved == FAIL || book != curBook || move_lines(book, savebuf) == FAIL) {
         showErrFmtMsg(_(e_could_not_prepare_for_reloading_str), book->currFileName);
         saved = FAIL;
      }
   }

   if (saved == OK) {
      int old_msg_silent = msg_silent;

      curBook->flags |= BF_CHECK_RO;   // check for RO again
      curBook->keepFiletype = TRUE;   // don't detect 'filetype'

      if (readfile(book->fullFileName, book->currFileName, (LineNr)0,
         (LineNr)0,
         (LineNr)MAXLNUM, &invo, flags) != OK
      ) {
         if (!aborting())
            showErrFmtMsg(_(e_could_not_reload_str), book->currFileName);
         if (savebuf != NULL && bookRefValid(&bufref) && book == curBook) {
            //Put the text back from the save book. First delete any lines that readfile() added.
            while (!CURBOOK_EMPTY()) {
               if (ml_delete(book->mem.lineCount) == FAIL)
                  break;
            } 
            (void)move_lines(savebuf, book);
         }
      } ei (book == curBook) {  // "book" still valid
         //Mark the book as unmodified and free undo info.
         unchanged(book, TRUE);
         if ((flags & READ_KEEP_UNDO) == 0)
            invalidateUndoBufferAndFreeBlocks(book);
         else {
            // Mark all undo states as changed.
            u_unchanged(curBook);
         }
      }

      msg_silent = old_msg_silent;
   }
   eeglFree(invo.comm);

   if (savebuf != NULL && bookRefValid(&bufref))
       bookWipe(savebuf, FALSE);

   // Invalidate diff info if necessary.
   diff_invalidate(curBook);

   // Restore the topline and cursor position and check it (lines may have been removed).
   if (old_topline > curBook->mem.lineCount)
      curPor->topLine = curBook->mem.lineCount;
   else
      curPor->topLine = old_topline;
   curPor->cursor = old_cursor;
   check_cursor();
   update_topline();
   curBook->keepFiletype = FALSE;
   {
      Portal   *wp;
      Tab   *t;
      // Update folds unless they are defined manually.
      FOR_ALL_TAB_PORTALS(t, wp) {
         if (wp->book == curPor->book)
            foldUpdateAll(wp);
      } 
   }
   // If the mode didn't change and @modifiable was set, keep the old value; the user probably used 
   // the ":view" command. But don't reset it, might have had a read error.
   if (orig_mode == curBook->origMode)
       curBook->o.modifiable |= old_ro;

   }

   // restore curPor/curBook and a few other things
   auCommRestoreBook(&aco);
   // Careful: autocommands may have made "book" invalid!
}

void
buf_store_time(Book *book, FileStat *st, CS fname UNUSED){
   book->modifiedTime = (long)st->st_mtime;
#ifdef ST_MTIM_NSEC
   book->modifiedTimeNs = (long)st->ST_MTIM_NSEC;
#else
   book->modifiedTimeNs = 0;
#endif
   book->origSize = st->st_size;
   book->origMode = (int)st->st_mode;
}

//}}}

//Adjust the line with missing eol, used for the next write.
//Used for do_filter(), when the input lines for the filter are deleted.
void
write_lnum_adjust(LineNr offset){
   if (curBook->noEolLnum != 0)   // only if there is a missing eol
      curBook->noEolLnum += offset;
}

private int
compare_readdirex_item(const void *p1, const void *p2) {
   CS name1 = bagGetString(*(Bag**)p1, tConst("name"), FALSE);
   CS name2 = bagGetString(*(Bag**)p2, tConst("name"), FALSE);
   if (readdirex_sort == READDIR_SORT_BYTE)
      return STRCMP(name1, name2);
   if (readdirex_sort == READDIR_SORT_IC)
      return caseInsensitiveCompare(name1, name2);

   return STRCOLL(name1, name2);
}

private int
compare_readdir_item(const void *s1, const void *s2) {
   if (readdirex_sort == READDIR_SORT_BYTE)
      return STRCMP(*(char **)s1, *(char **)s2);
   if (readdirex_sort == READDIR_SORT_IC)
      return caseInsensitiveCompare(*(char **)s1, *(char **)s2);

   return STRCOLL(*(char **)s1, *(char **)s2);
}

//Core part of "readdir()" and "readdirex()" function.
//Retrieve the list of files/directories of "path" into "gap".
//If "withattr" is TRUE, retrieve the names and their attributes.
//If "withattr" is FALSE, retrieve the names only. Return OK for success, FAIL for failure.
private int
readdir_core(ArrayList   *gap, int withattr UNUSED, int sort) {
   int         failed = FALSE;
   ga_init2(gap, sizeof(void *), 20);

#define FREE_ITEM(item)   do { \
   if (withattr) \
       bagUnref((Bag*)(item)); \
   else \
       eeglFree(item); \
   } while (0)

   readdirex_sort = READDIR_SORT_BYTE;

# undef FREE_ITEM

   if (!failed && gap->len > 0 && sort > READDIR_SORT_NONE) {
      readdirex_sort = sort;
      if (withattr)
         qsort((void*)gap->c, (Unt)gap->len, sizeof(Bag*), compare_readdirex_item);
      else
         qsort((void*)gap->c, (Unt)gap->len, sizeof(CS), compare_readdir_item);
   }

   return failed ? FAIL : OK;
}

//return TRUE if "name" is a directory, NOT a symlink to a directory
//return FALSE if "name" is not a directory. return FALSE for error
int
mch_isrealdir(CS name) {
   struct stat statb;

   if (*name == ZERO)       // Some stat()s don't flag "" as an error.
      return FALSE;
   if (lstat((char *)name, &statb))
      return FALSE;
   return (S_ISDIR(statb.st_mode) ? TRUE : FALSE);
}

//TRUE if "name" is a directory or a symlink to a directory
//FALSE if "name" is not a directory. FALSE for error
Boole
mch_isdir(CS name) {
   struct stat statb;

   if (*name == ZERO)       // Some stat()s don't flag "" as an error.
      return false;
   if (stat((char *)name, &statb))
      return false;
   return S_ISDIR(statb.st_mode) ? true : false;
}

//Check what "name" is:
//NODE_NORMAL: file or directory (or doesn't exist)
//NODE_WRITABLE: writable device, socket, fifo, etc.
//NODE_OTHER: non-writable things
int
mch_nodetype(CS name) {
   struct stat   st;

   if (stat((char *)name, &st))
      return NODE_NORMAL;
   if (S_ISREG(st.st_mode) || S_ISDIR(st.st_mode))
      return NODE_NORMAL;
   if (S_ISBLK(st.st_mode))   // block device isn't writable
      return NODE_OTHER;
   // Everything else is writable?
   return NODE_WRITABLE;
}


//Delete "name" and everything in it, recursively. return true for success, false if some file was 
//not deleted.
private Boole
recursivelyDeleteDir(CS name){
   Boole result = 0;

   // A symbolic link to a directory itself is deleted, not the directory it points to.
   if (mch_isrealdir(name)) {
      CS exp = copyStr(name);
      ArrayList    ga;

      if (readdir_core(&ga, FALSE, READDIR_SORT_NONE) == OK) {
         int   len = eeSnprintf(nameBuffG, MAXPATHL, "%s/", exp);

         for (int i = 0; i < ga.len; ++i) {
            eeSnprintf(nameBuffG + len, MAXPATHL - len, "%s", ((Byte **)ga.c)[i]);
            if (!recursivelyDeleteDir(nameBuffG))
                // Remember the failure but continue deleting any further entries.
                result = false;
         }
         ga_clear_strings(&ga);
         if (mch_rmdir(exp) != 0)
            result = false;
      } else
         result = false;
      eeglFree(exp);
   } else
      result = mch_remove(name) == 0;

   return result;
}

private long   temp_count = 0;      // Temp filename counter.

//Open temporary directory and take file lock to prevent to be auto-cleaned.
private void
eeOpentempdir(void) {
   if (eeTempDir_dpG)
      return;

   DIR* dp = opendir((const char*)eeTempDirG);
   if (!dp)
      return;

   eeTempDir_dpG = dp;
   flock(dirfd(eeTempDir_dpG), LOCK_SH);
}

// Close temporary directory - it automatically release file lock.
private void
eeClosetempdir(void) {
   if (!eeTempDir_dpG)
      return;

   closedir(eeTempDir_dpG);
   eeTempDir_dpG = NULL;
}

// Delete the temp directory and all files it contains.
void
eeDelTempDir(void) {
   if (!eeTempDirG)
      return;

   eeClosetempdir();
   // remove the trailing path separator
   fiGetShortFiName(eeTempDirG)[-1] = ZERO;
   recursivelyDeleteDir(eeTempDirG);
   EE_CLEAR(eeTempDirG);
}

//Directory "tempdir" was created. Expand this name to a full path and put it in "eeTempDirG". 
//This avoids that using ":cd" would confuse us. "tempdir" must be no longer than MAXPATHL.
private void
eeSettempdir(CS tempdir){
   Byte buf[MAXPATHL + 2];
   
   if (eeFullFileName(tempdir, buf, MAXPATHL, FALSE) == FAIL)
      STRCPY(buf, tempdir);
   Unt buflen = STRLEN(buf);
   if (!after_pathsep(buf, buf + buflen)) {
      buf[buflen] = '/';
      buflen++;
   }
   eeTempDirG = copySubstr(buf, buflen);
   eeOpentempdir();
}

//eeTempName(): Return a unique name that can be used for a temp file.
//
//The temp file is NOT guaranteed to be created. If "keep" is FALSE it is guaranteed to NOT be 
//created.
//
//The returned pointer is to allocated memory.
//The returned pointer is NULL if no valid name was found.
CS
eeTempName(
   int extra_char UNUSED,  // char to use in the name instead of '?'
   int keep UNUSED
) {
#ifdef USE_TMPNAM
   Byte itmp[L_tmpnam];   // use tmpnam()
#else
   Byte itmp[TEMPNAMELEN];
#endif


   static CS tempdirs[] = {SMAP((CS), TEMPDIRNAMES)};
   int i;

   //This will create a directory for private use by this instance of Eegl. This is done once, and 
   //the same directory is used for all temp files. This method avoids security problems because 
   //of symlink attacks et al. It's also a bit faster, because we only need to check for an existing
   //file when creating the directory and not for each temp file.
   if (!eeTempDirG) {
      // Try the entries in TEMPDIRNAMES to create the temp directory.
      for (i = 0; i < (int)ARRAY_LENGTH(tempdirs); ++i) {
         //Expand $TMP, leave room for "/v1100000/999999999".
         //Skip the directory check if the expansion fails.
         Unt itmplen = doExpandEnv(OUT (Text){itmp, TEMPNAMELEN - 20}, (CS)tempdirs[i]);
         if (itmp[0] != '$' && mch_isdir(itmp)) {
         //directory exists
         if (!after_pathsep(itmp, itmp + itmplen)) {
            itmp[itmplen] = '/';
            itmplen++;
         }

         //Make sure the umask doesn't remove the executable bit.
         //"repl" has been reported to use "177".
         mode_t umask_save = umask(077);
         //Leave room for filename
         STRCPY(itmp + itmplen, "vXXXXXX");
         itmplen += STRLEN_LITERAL("vXXXXXX");
         if (mkdtemp((char *)itmp) != NULL)
            eeSettempdir(itmp);
         (void)umask(umask_save);
         if (eeTempDirG != NULL)
             break;
         }
      }
   }

   if (eeTempDirG) {
      //There is no need to check if the file exists, because we own the directory and nobody 
      //else creates a file in it.
      int itmplen = eeSnprintf(itmp, sizeof(itmp), "%s%ld", eeTempDirG, temp_count++);
      return copySubstr(itmp, (Unt)itmplen);
   }

   return NULL;
}

//Try matching a filename with a "pattern" ("prog" is NULL), or use the precompiled regprog "prog"
//("pattern" is NULL). That avoids calling compileRegexp() often. Used for autocommands and 
//'wildignore'. Return TRUE if there is a match, FALSE otherwise.
int
match_file_pat(
   CS pattern,      // pattern to match with
   RegProg** prog,         // pre-compiled regprog or NULL
   CS fname,         // full path of file name
   CS sfname,      // short file name or NULL
   CS tail,         // tail of path
   Boole allow_dirs      // allow matching with dir
){
   int      result = FALSE;
   RegMatch   regmatch;
   regmatch.rm_ic = FALSE;
   if (prog)
      regmatch.regprog = *prog;
   else
      regmatch.regprog = compileRegexp(pattern, RE_MAGIC);

   //Try for a match with the pattern with:
   //1. the full file name, when the pattern has a '/'.
   //2. the short file name, when the pattern has a '/'.
   //3. the tail of the file name, when the pattern has no '/'.
   if (regmatch.regprog != NULL
        && ((allow_dirs
           && (eeRegexec(&regmatch, fname, (ColNr)0)
             || (sfname != NULL && eeRegexec(&regmatch, sfname, (ColNr)0))))
          || (!allow_dirs && eeRegexec(&regmatch, tail, (ColNr)0)))
   ) {
      result = TRUE;
   } 

   if (prog != NULL)
      *prog = regmatch.regprog;
   else
      eeRegFree(regmatch.regprog);
   return result;
}

//Return TRUE if a file matches with a pattern in "list". "list" is a comma-separated list of 
//patterns, like 'wildignore'. "sfname" is the short file name or NULL, "ffname" the long file name
int
match_file_list(CS list, CS sfname, CS ffname){
   Byte buf[MAXPATHL];
   Boole allow_dirs;

   CS tail = fiGetShortFiName(sfname);

   // try all patterns in 'wildignore'
   CS p = list;
   while (*p) {
      copy_option_part(&p, buf, MAXPATHL, ",");
      CS regpat = file_pat_to_reg_pat(buf, NULL, OUT &allow_dirs);
      int match = match_file_pat(regpat, NULL, ffname, sfname, tail, allow_dirs);
      eeglFree(regpat);
      if (match)
         return TRUE;
   }
   return FALSE;
}

//Convert the given pattern "pat" which has shell style wildcards in it, into a regular expression,
//and return the result in allocated memory. If there is a directory path separator to be matched, 
//then TRUE is put in allow_dirs, otherwise FALSE is put there -- webb.
//Handle backslashes before special characters, like "\*" and "\ ".
CS
file_pat_to_reg_pat(
   CS pat,
   CS pat_end,   // first char after pattern or NULL
   OUT Boole* allow_dirs   // Result passed back out in here
){
   int size = 2; // '^' at start, '$' at end
   CS p;
   int i;
   int nested = 0;
   int add_dollar = TRUE;

   if (allow_dirs)
      *allow_dirs = false;
   if (!pat_end)
      pat_end = pat + STRLEN(pat);

   for (p = pat; p < pat_end; p++) {
      switch (*p) {
      case '*':
      case '.':
      case ',':
      case '{':
      case '}':
      case '~':
         size += 2;   // extra backslash
      break;
      default:
         size++;
         break;
      }
   }
   CS reg_pat = alloc(size + 1);

   i = 0;

   if (pat[0] == '*') {
      while (pat[0] == '*' && pat < pat_end - 1)
         pat++;
   } else
      reg_pat[i++] = '^';
   CS endp = pat_end - 1;
   if (endp >= pat && *endp == '*') {
      while (endp - pat > 0 && *endp == '*')
         endp--;
      add_dollar = FALSE;
   }
   for (p = pat; *p && nested >= 0 && p <= endp; p++) {
      switch (*p) {
      case '*':
         reg_pat[i++] = '.';
         reg_pat[i++] = '*';
         while (p[1] == '*')   // "**" matches like "*"
            ++p;
         break;
      case '.':
      case '~':
         reg_pat[i++] = '\\';
         reg_pat[i++] = *p;
         break;
      case '?':
         reg_pat[i++] = '.';
         break;
      case '\\':
         if (p[1] == ZERO)
            break;
         // Undo escaping from ExpandEscape():
         // foo\?bar -> foo?bar
         // foo\%bar -> foo%bar
         // foo\,bar -> foo,bar
         // foo\ bar -> foo bar
         // Don't unescape \, * and others that are also special in a
         // regexp.
         // An escaped { must be unescaped since we use magic not
         // verymagic.  Use "\\\{n,m\}"" to get "\{n,m}".
         if (*++p == '?')
            reg_pat[i++] = '?';
         else
            if (*p == ',' || *p == '%' || *p == '#' || isSpace(*p) || *p == '{' || *p == '}')
               reg_pat[i++] = *p;
            ei (*p == '\\' && p[1] == '\\' && p[2] == '{') {
               reg_pat[i++] = '\\';
               reg_pat[i++] = '{';
               p += 2;
            } else {
               if (allow_dirs && *p == '/')
                  *allow_dirs = true;
               reg_pat[i++] = '\\';
               reg_pat[i++] = *p;
            }
         break;
      case '{':
         reg_pat[i++] = '\\';
         reg_pat[i++] = '(';
         nested++;
            break;
      case '}':
         reg_pat[i++] = '\\';
         reg_pat[i++] = ')';
         --nested;
         break;
      case ',':
         if (nested) {
            reg_pat[i++] = '\\';
            reg_pat[i++] = '|';
         } else
             reg_pat[i++] = ',';
         break;
      default:
         if (allow_dirs && *p == '/')
            *allow_dirs = true;
         reg_pat[i++] = *p;
         break;
      }
   }
   if (add_dollar)
      reg_pat[i++] = '$';
   reg_pat[i] = ZERO;
   if (nested != 0) {
      if (nested < 0)
         emsg(_(e_missing_open_curly));
      else
         emsg(_(e_missing_close_curly));
      EE_CLEAR(reg_pat);
   }
   return reg_pat;
}

//Version of read() that retries when interrupted by EINTR (possibly by a SIGWINCH).
long
read_eintr(int fd, void* buf, Unt bufsize) {
   long ret;

   for (;;) {
      ret = eeReadFromFile(fd, buf, bufsize);
      if (ret >= 0 || errno != EINTR)
         break;
   }
   return ret;
}

//Version of write() that retries when interrupted by EINTR (possibly by a SIGWINCH).
long
write_eintr(int fd, void *buf, Unt bufsize) {
   long    ret = 0;
   long    wlen;

   // Repeat the write() so long it didn't fail, other than being interrupted by a signal.
   while (ret < (long)bufsize) {
      wlen = eeWriteToFile(fd, (char *)buf + ret, bufsize - ret);
      if (wlen < 0) {
         if (errno != EINTR)
            break;
      } else
         ret += wlen;
   }
   return ret;
}


#ifndef SEEK_SET
# define SEEK_SET 0
#endif
#ifndef SEEK_END
# define SEEK_END 2
#endif

//Get the stdout of an external command.
//If "ret_len" is NULL replace ZERO characters with NL.  When "ret_len" is not
//NULL store the length there. Return an allocated string, or NULL for error.
CS
get_cmd_output(
   CS cmd,
   CS infile,   // optional input file name
   Unt flags,      // can be SHELL_SILENT
   OUT int* ret_len
) {
   CS tempname;
   int len;
   int i = 0;
   FILE   *fd;

   // get a name for the temp file
   if ((tempname = eeTempName('o', FALSE)) == NULL) {
      emsg(_(e_cant_get_temp_file_name));
      return NULL;
   }

   // Add the redirection stuff
   CS command = make_filter_cmd(cmd, infile, tempname);
   if (!command)
      goto done;

   //Call the shell to execute the command (errors are ignored). Don't check timestamps here.
   ++no_check_timestamps;
   call_shell(command, null, SHELL_DOOUT | SHELL_EXPAND | flags);
   --no_check_timestamps;

   eeglFree(command);

   //read the names from the file into memory
   fd = fopen((char *)tempname, READBIN);

   // Not being able to seek means we can't read the file.
   if (fd == NULL
       || fseek(fd, 0L, SEEK_END) == -1
       || (len = ftell(fd)) == -1      // get size of temp file
       || fseek(fd, 0L, SEEK_SET) == -1)   // back to the start
    {
      showErrFmtMsg(_(e_cannot_read_from_str_2), tempname);
      if (fd)
         fclose(fd);
      goto done;
   }

   CS buf = alloc(len + 1);
   fclose(fd);
   mch_remove(tempname);
   if (i != len) {
      showErrFmtMsg(_(e_cant_read_file_str), tempname);
      EE_CLEAR(buf);
   } ei (ret_len == NULL) {
      // Change ZERO into SOH, otherwise the string is truncated.
      for (i = 0; i < len; ++i)
         if (buf[i] == ZERO)
            buf[i] = 1;

      buf[len] = ZERO;   // make sure the buf is terminated
   } else
      *ret_len = len;

done:
   eeglFree(tempname);
   return buf;
}


private void
get_cmd_output_as_returnVar(Var* argvars, OUT Var* returnVar, int retlist) {
   CS res = NULL;
   Byte   *p;
   Byte   *infile = NULL;
   int      err = FALSE;
   FILE   *fd;
   List   *list = NULL;
   int      flags = SHELL_SILENT;

   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   if (argvars[1].tag != VAR_UNKNOWN) {
      //Write the text to a temp file, to be used for input of the shell command.
      if ((infile = eeTempName('i', TRUE)) == NULL) {
         emsg(_(e_cant_get_temp_file_name));
         goto errret;
      }

      fd = fopen((char *)infile, WRITEBIN);
      if (fd == NULL) {
         showErrFmtMsg(_(e_cant_open_file_str), infile);
         goto errret;
      }
      if (argvars[1].tag == VAR_NUMBER) {
         Book* buf = bookFindFileByBookNr(argvars[1].number);
         if (!buf) {
            showErrFmtMsg(_(e_book_nr_does_not_exist), argvars[1].number);
            fclose(fd);
            goto errret;
         }

         for (LineNr lnum = 1; lnum <= buf->mem.lineCount; lnum++) {
            for (p = memGetLine(buf, lnum, false); *p != ZERO; ++p) {
               if (putc(*p == '\n' ? ZERO : *p, fd) == EOF) {
                  err = TRUE;
                  break;
               }
            } 
            if (putc(NL, fd) == EOF) {
                err = TRUE;
                break;
            }
         }
      } ei (argvars[1].tag == VAR_LIST) {
         if (write_list(fd, argvars[1].list, TRUE) == FAIL)
            err = TRUE;
      } else {
         Unt len;
         Byte buf[NUMBUFLEN];

         p = convertVarToString(&argvars[1], buf);
         if (!p) {
            fclose(fd);
            goto errret;      // type error; errmsg already given
         }
         len = STRLEN(p);
         if (len > 0 && fwrite(p, len, 1, fd) != 1)
            err = TRUE;
      }
      if (fclose(fd) != 0)
         err = TRUE;
      if (err) {
         emsg(_(e_error_writing_temp_file));
         goto errret;
      }
   }

   // Omit SHELL_COOKED when invoked with ":silent".  Avoids that the shell
   // echoes typeahead, that messes up the display.
   if (!msg_silent)
      flags += SHELL_COOKED;

   if (retlist) {
      int      len;
      ListItem   *li;
      CS s = NULL;
      CS start;
      CS end;
      int      i;

      res = get_cmd_output(tv_get_string(&argvars[0]), infile, flags, OUT &len);
      if (res == NULL)
         goto errret;

      list = list_alloc();

      for (i = 0; i < len; ++i) {
         start = res + i;
         while (i < len && res[i] != NL)
            ++i;
         end = res + i;

         s = alloc(end - start + 1);

         for (p = s; start < end; ++p, ++start)
            *p = *start == ZERO ? NL : *start;
         *p = ZERO;

         li = listitem_alloc();
         li->c = (Var){.tag = VAR_STRING, .lock = 0, .string = s};
         list_append(list, li);
      }

      returnVar_list_set(returnVar, list);
      list = NULL;
   } else {
      res = get_cmd_output(tv_get_string(&argvars[0]), infile, flags, NULL);
      returnVar->string = res;
      res = NULL;
   }

errret:
   if (infile) {
      mch_remove(infile);
      eeglFree(infile);
   }
   if (res)
      eeglFree(res);
   if (list)
      list_free(list);
}

void
f_system(Var* argvars, Var* returnVar) {
   get_cmd_output_as_returnVar(argvars, returnVar, FALSE);
}

void
f_systemlist(Var* argvars, Var* returnVar) {
   get_cmd_output_as_returnVar(argvars, returnVar, TRUE);
}

//}}}
//{{{low-level functions

// Return 0 for not writable, 1 for writable file, 2 for a dir which we have rights to write into.
int
filewritable(CS fname) {
   int retval = 0;
   int perm = mch_getperm(fname);
   if ((perm & 0222) && mch_access(fname, W_OK) == 0) {
      ++retval;
      if (mch_isdir(fname))
         ++retval;
   }
   return retval;
}


// Read 2 bytes from "fd" and turn them into an int, MSB first. Return -1 when encountering EOF.
int
get2c(FILE* fd) {
   int n = getc(fd);
   if (n == EOF) return -1;
      int c = getc(fd);
   if (c == EOF) return -1;
      return (n << 8) + c;
}

// Read 3 bytes from "fd" and turn them into an int, MSB first. Returns -1 when encountering EOF.
int
get3c(FILE* fd) {
   int n = getc(fd);
   if (n == EOF) return -1;
   int c = getc(fd);
   if (c == EOF) return -1;
   n = (n << 8) + c;
   c = getc(fd);
   if (c == EOF) return -1;
   return (n << 8) + c;
}

// Read 4 bytes from "fd" and turn them into an int, MSB first. Returns -1 when encountering EOF.
int
get4c(FILE* fd) {
   // Use unsigned rather than int otherwise result is undefined when left-shift sets the 
   // most-significant bit

   int c = getc(fd);
   if (c == EOF) return -1;
   unsigned n = (unsigned)c;
   c = getc(fd);
   if (c == EOF) return -1;
   n = (n << 8) + (unsigned)c;
   c = getc(fd);
   if (c == EOF) return -1;
   n = (n << 8) + (unsigned)c;
   c = getc(fd);
   if (c == EOF) return -1;
   n = (n << 8) + (unsigned)c;
   return (int)n;
}

// Read a string of length "cnt" from "fd" into allocated memory.
// Return NULL when unable to read that many bytes.
CS
read_string(FILE* fd, int cnt) {
   CS str = alloc(cnt + 1);

   // Read the string. Quit when running into the EOF.
   int i;
   for (i = 0; i < cnt; ++i) {
      int c = getc(fd);
      if (c == EOF) {
          eeglFree(str);
          return NULL;
      }
      str[i] = c;
   }
   str[i] = ZERO;
   return str;
}

//}}}
//{{{shell interaction

// Call shell. Call chCallShell
int
call_shell(CS cmd, NULLABLE CS extraArg, int opt) {
   if (p_verbose > 3) {
      verbose_enter();
      smsg(_("Calling shell to execute: \"%s\""), cmd ? cmd : S"bash");
      msgPutcharDeco('\n', 0);
      cursor_on();
      verbose_leave();
   }

   // The external command may update a tags file, clear cached tags.
   tag_freematch();

   int retval = chCallShell(cmd, extraArg, opt);
   // Check the portal size, in case it changed while executing the external command.
   shell_resized_check();

   set_EeglVar_nr(VV_SHELL_ERROR, (long)retval);
   return retval;
}

//}}}
//{{{security interaction

// Get file permissions for 'name'. Return -1 when it doesn't exist.
long
mch_getperm(CS name) {
   struct stat statb;

   // Keep the #ifdef outside of stat(), it may be a macro.
   if (stat((char *)name, &statb))
      return -1;
   return statb.st_mode;
}

// Set file permission for "name" to "perm". FAIL for failure, OK otherwise.
int
mch_setperm(CS name, long perm) {
   return (chmod((char *)name, (mode_t)perm) == 0 ? OK : FAIL);
}

// Copy extended attributes from_file to to_file
void
mch_copy_xattr(CS from_file, CS to_file) {
   if (!from_file)
      return;
      
   Long   keylen, vallen, max_vallen = 0;
   CS val = NULL;
   CS errmsg = NULL;

   // get the length of the extended attributes
   Long size = listxattr((char *)from_file, NULL, 0);
   // not supported or no attributes to copy
   if (size <= 0)
      return;
   CS xattr_buf = alloc(size);
   size = listxattr((char *)from_file, (char *)xattr_buf, size);
   Long tsize = size;

   errno = 0;

   for (int round = 0; round < 2; round++) {
      CS key = xattr_buf;
      if (round == 1)
          size = tsize;

      while (size > 0) {
         vallen = getxattr((char *)from_file, (char*)key, val, round ? max_vallen : 0);
         // only set the attribute in the second round
         if (vallen >= 0 && round && setxattr((char*)to_file, (char*)key, val, vallen, 0) == 0) {
         } ei (errno) {
            switch (errno) {
            case E2BIG:
               errmsg = e_xattr_e2big;
               goto error_exit;
            case ENOTSUP:
            case EACCES:
            case EPERM:
               break;
            case ERANGE:
               errmsg = e_xattr_erange;
               goto error_exit;
            default:
               errmsg = e_xattr_other;
               goto error_exit;
            }
         }

         if (round == 0 && vallen > max_vallen)
            max_vallen = vallen;

         // add one for terminating null
         keylen = STRLEN(key) + 1;
         size -= keylen;
         key += keylen;
      } 
      if (round)
          break;

      val = alloc(max_vallen + 1);
   }
error_exit:
   eeglFree(xattr_buf);
   eeglFree(val);

   if (errmsg != NULL)
      emsg(_(errmsg));
}

// Set file permission for open file "fd" to "perm". FAIL for failure, OK otherwise.
int
mch_fsetperm(int fd, long perm) {
   return (fchmod(fd, (mode_t)perm) == 0 ? OK : FAIL);
}

// 1 if "name" is an executable file, 0 if not or it doesn't exist.
private int
executable_file(CS name) {
   struct stat   st;

   if (stat((char *)name, &st))
      return 0;
   return S_ISREG(st.st_mode) && mch_access(name, X_OK) == 0;
}

//Return TRUE if "name" can be found in $PATH and executed, FALSE if not.
//If "use_path" is FALSE only check if "name" is executable.
//Return -1 if unknown.
int
mch_can_exe(CS name, Arr(CS) path, int use_path) {
   Unt   bufsize;
   Unt   buflen;
   CS e;
   CS p_end;
   Unt   elen;
   int      retval;

   //When "use_path" is false and if it's an absolute or relative path, don't need to use $PATH.
   if (!use_path || fiGetShortFiName(name) != name) {
      // There must be a path separator, files in the current directory
      // can't be executed.
      if ((use_path || fiGetShortFiName(name) != name) && executable_file(name)) {
         if (path) {
            if (name[0] != '/')
               *path = fiExpandAndCopy(name, TRUE);
            else
               *path = copyStr(name);
         }
         return TRUE;
      }
      return FALSE;
   }

   CS p = (CS)getenv("PATH");
   if (p == NULL || *p == ZERO)
      return -1;
   p_end = p + STRLEN(p);
   bufsize = STRLEN(name) + (Unt)(p_end - p) + 2;
   CS buf = alloc(bufsize);

   // Walk through all entries in $PATH to check if "name" exists there and is an executable file
   for (;;) {
      e = (CS)strchr((char *)p, ':');
      if (!e)
         e = p_end;
      elen = (Unt)(e - p);
      if (elen <= 1) {     // empty entry means current dir
         p = (CS)"./";
         elen = STRLEN_LITERAL("./");
      }
      buflen = eeSnprintf(buf, bufsize, "%.*s%s%s",
         (int)elen,
         p,
         (after_pathsep(p, p + elen)) ? "" : "/",
         name
      );
      retval = executable_file(buf);
      if (retval == 1) {
         if (path) {
            *path = buf[0] == '/' ? copySubstr(buf, buflen) : fiExpandAndCopy(buf, TRUE);
         }
         break;
      }

      if (*e != ':')
          break;
      p = e + 1;
   }

   eeglFree(buf);
   return retval;
}

//}}}
