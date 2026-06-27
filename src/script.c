//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## script.c: script files, user command line, its completion and user-defined functions

#include "eegl.h"

#define DECLARE_COMMANDS_FLAGS
#include "commands.h"
#undef DECLARE_COMMANDS_FLAGS

// table to store parsed 'wildmode'
private Byte wim_flags[4];

//{{{forward declarations

private Byte* set_context_in_autocmd(Expand* xp, Byte* arg, int doautocmd);
private CS get_augroup_name(Expand* xp UNUSED, int idx);
private ScriptPos* acp_scriptCtx(AutoPatComm* acp);

//}}}
//{{{script files

// Cookie used by getsourceline().
//
// It is used used to store info for each sourced file. It is shared between scriptRunFile() and 
// getsourceline(). This is passed to do_cmdline().
typedef struct {
   FILE* fp;      // opened file for sourcing
   CS nextline;   // if not NULL: line that was read ahead
   LineNr sourcing_lnum;   // line number of the source file
   Boole finished;   // ":finish" used
   Boole sourceFromCurBook;
   int buf_lnum;   // line number in the current buffer
   ArrayList buflines;   // lines in the current buffer
   LineNr breakpoint;   // next line with breakpoint or zero
   CS fname;      // name of sourced file
   int dbg_tick;   // debug_tick when breakpoint was set
   int level;      // top nesting level of sourced file
} SourceCookie;

// The names of packages that once were loaded are remembered.
private ArrayList ga_loaded = {0, 0, sizeof(CS), 4, NULL};

// last used sequence number for sourcing scripts (scriptPosG.seq)
private int last_current_SID_seq = 0;

private int scriptRunFileInternal( CS fname, OUT int *ret_sid, Invocation* invo, Boole clearvars);


// Initialize the execution stack.
void
estack_init(void){
   if (ga_grow(&exestack, 10) == FAIL)
      mch_exit(0);
      
   Estack* entry;
   entry = ((Estack *)exestack.c) + exestack.len;
   entry->ty = ETYPE_TOP;
   entry->name = NULL;
   entry->lnum = 0;
   entry->info.ufunc = NULL;
   ++exestack.len;
}

//Add an item to the execution stack. Return the new entry or NULL when out of memory.
Estack *
estack_push(CallFrame type, CS name, long lnum) {
   Estack *entry;

   // If memory allocation fails then we'll pop more than we push, eventually
   // at the top level it will be OK again.
   if (ga_grow(&exestack, 1) == FAIL)
      return NULL;

   entry = ((Estack *)exestack.c) + exestack.len;
   entry->ty = type;
   entry->name = name;
   entry->lnum = lnum;
   entry->info.ufunc = NULL;
   ++exestack.len;
   return entry;
}

//Add a user function to the execution stack.
Estack*
estack_push_ufunc(UserFunc *ufunc, long lnum) {
   Estack *entry = estack_push(ETYPE_UFUNC,
      ufunc->uf_name_exp ? ufunc->uf_name_exp : ufunc->uf_name, lnum);
   if (entry)
      entry->info.ufunc = ufunc;
   return entry;
}

//Return true if "ufunc" with "lnum" is already at the top of the exe stack.
int
estack_top_is_ufunc(UserFunc *ufunc, long lnum) {
   if (exestack.len == 0)
      return false;
   Estack* entry = ((Estack *)exestack.c) + exestack.len - 1;
   return entry->ty == ETYPE_UFUNC
      && STRCMP( entry->name, ufunc->uf_name_exp ? ufunc->uf_name_exp : ufunc->uf_name) == 0
      && entry->lnum == lnum;
}

//Take an item off of the execution stack and return it.
Estack*
estack_pop(void){
   if (exestack.len == 0)
      return NULL;
   --exestack.len;
   return ((Estack *)exestack.c) + exestack.len;
}

//Get the current value for "which" in allocated memory.
//"which" is ESTACK_SFILE for <sfile>, ESTACK_STACK for <stack> or
//ESTACK_SCRIPT for <script>.
CS
estack_sfile(EstackArg which UNUSED){
   ArrayList ga;
   Unt len;
   int idx;
   CallFrame last_type = ETYPE_SCRIPT;

   Estack* entry = ((Estack *)exestack.c) + exestack.len - 1;
   if (which == ESTACK_SFILE && entry->ty != ETYPE_UFUNC) {
      if (entry->name == NULL)
         return NULL;
      return copyStr(entry->name);
   }

   //If evaluated in a function or autocommand, return the path of the script
   //where it is defined, at script level the current script path is returned instead.
   if (which == ESTACK_SCRIPT) {
      // Walk the stack backwards, starting from the current frame.
      for (idx = exestack.len - 1; idx >= 0; --idx, --entry) {
         if (entry->ty == ETYPE_UFUNC || entry->ty == ETYPE_AUCMD) {
            ScriptPos *def_ctx = entry->ty == ETYPE_UFUNC
                        ? &entry->info.ufunc->scriptCtx
                        : acp_scriptCtx(entry->info.aucmd);

            return def_ctx->sid > 0 ? copyStr(SCRIPT_ITEM(def_ctx->sid)->sn_name) : NULL;
         } ei (entry->ty == ETYPE_SCRIPT)
            return copyStr(entry->name);
      }
      return NULL;
    }

   // Give information about each stack entry up to the root.
   // For a function we compose the call stack, as it was done in the past:
   //   "function One[123]..Two[456]..Three"
   ga_init2(&ga, sizeof(char), 100);
   for (idx = 0; idx < exestack.len; ++idx) {
      entry = ((Estack *)exestack.c) + idx;
      if (entry->name != NULL) {
          long    lnum = 0;
          Byte  *type_name = (CS)"";
          Byte  *class_name = (CS)"";

         if (entry->ty != last_type) {
            switch (entry->ty) {
                case ETYPE_SCRIPT: type_name = (CS)"script "; break;
                case ETYPE_UFUNC: type_name = (CS)"function "; break;
                default: type_name = (CS)""; break;
            }
            last_type = entry->ty;
         }
         if (idx == exestack.len - 1)
            lnum = which == ESTACK_STACK ? SOURCING_LNUM : 0;
         else
            lnum = entry->lnum;
         len = STRLEN(entry->name) + STRLEN(type_name) + STRLEN(class_name) + 26;
         if (ga_grow(&ga, (int)len) == FAIL)
            break;
         ga_concat(&ga, type_name);
         if (*class_name != ZERO) {
            // For class methods prepend "<class name>." to the function name.
            ga_concat(&ga, (CS)"<SNR>");
            ga.len += eeSnprintf((CS)ga.c + ga.len, 23, "%d_", entry->info.ufunc->scriptCtx.sid);
            ga_concat(&ga, class_name);
            ga_append(&ga, '.');
         }
         ga_concat(&ga, entry->name);
          // For the bottom entry of <sfile>: do not add the line number, it is used in
          // <slnum>.  Also leave it out when the number is not set.
         if (lnum != 0)
            ga.len += eeSnprintf((CS)ga.c + ga.len, 23, "[%ld]", lnum);
         if (idx != exestack.len - 1)
            ga_concat(&ga, (CS)"..");
      }
    }

    ga_append(&ga, '\0');
    return (CS)ga.c;
}

private void
stacktrace_push_item(
   List* l,
   UserFunc* fp,
   Byte* event,
   LineNr lnum,
   CS filepath
) {
   Bag* d = allocBag_lock(VAR_FIXED);
   if (d == NULL)
      return;
   Var tv = (Var){.tag = VAR_BAG, .lock = VAR_LOCKED, .bag = d};

   if (fp)
      bagAddFn(d, S"funcref", fp);
   if (event)
      bagAddString(d, S"event", event);
   bagAddNumber(d, S"lnum", lnum);
   bagAddString(d, S"filepath", filepath);

   list_append_tv(l, &tv);
}

//Create the stacktrace from exestack.
List *
stacktrace_create(void){
   List* l = list_alloc();

   for (Unt i = 0; i < (Unt)exestack.len; ++i) {
      Estack *entry = &((Estack *)exestack.c)[i];
      LineNr lnum = entry->lnum;

      if (entry->ty == ETYPE_SCRIPT)
          stacktrace_push_item(l, NULL, NULL, lnum, entry->name);
      ei (entry->ty == ETYPE_UFUNC) {
          UserFunc *fp = entry->info.ufunc;
          ScriptPos sctx = fp->scriptCtx;
          Byte *filepath = sctx.sid > 0 ? get_scriptname(sctx.sid) : (CS)"";

          lnum += sctx.lineNr;
          stacktrace_push_item(l, fp, NULL, lnum, filepath);
      } ei (entry->ty == ETYPE_AUCMD) {
          ScriptPos sctx = *acp_scriptCtx(entry->info.aucmd);
          Byte *filepath = sctx.sid > 0 ? get_scriptname(sctx.sid) : (CS)"";

          lnum += sctx.lineNr;
          stacktrace_push_item(l, NULL, entry->name, lnum, filepath);
      }
   }
   return l;
}

void
f_getstacktrace(Arr(Var) argvars UNUSED, Var* returnVar) {
    returnVar_list_set(returnVar, stacktrace_create());
}

//Get DIP_ flags from the [where] argument of a :runtime command.
//"*argp" is advanced to after the [where] argument if it is found.
private int
get_runtime_cmd_flags(Byte **argp, Unt where_len) {
   CS arg = *argp;

   if (where_len == 0)
      return 0;

   if (STRNCMP(arg, "START", where_len) == 0) {
      *argp = skipwhite(arg + where_len);
      return DIP_START + DIP_NORTP;
   }
   if (STRNCMP(arg, "OPT", where_len) == 0) {
      *argp = skipwhite(arg + where_len);
      return DIP_OPT + DIP_NORTP;
   }
   if (STRNCMP(arg, "PACK", where_len) == 0) {
      *argp = skipwhite(arg + where_len);
      return DIP_START + DIP_OPT + DIP_NORTP;
   }
   if (STRNCMP(arg, "ALL", where_len) == 0) {
      *argp = skipwhite(arg + where_len);
      return DIP_START + DIP_OPT;
   }

   return 0;
}

//":runtime [where] {name}"
void
c_runtime(Invocation* invo){
   Byte  *arg = invo->arg;
   int       flags = invo->forceit ? DIP_ALL : 0;
   Byte  *p = skiptowhite(arg);
   flags += get_runtime_cmd_flags(&arg, p - arg);
   source_runtime(arg, flags);
}

private Unt runtime_expand_flags;

//Set the completion context for the :runtime command.
void
set_context_in_runtime_cmd(Expand *xp, CS arg) {
   CS p = skiptowhite(arg);
   runtime_expand_flags = *p != ZERO ? get_runtime_cmd_flags(&arg, p - arg) : 0;
   // Skip to the last argument.
   while (*(p = skiptowhite_esc(arg)) != ZERO) {
      if (runtime_expand_flags == 0)
          // When there are multiple arguments and [where] is not specified,
          // use an unrelated non-zero flag to avoid expanding [where].
          runtime_expand_flags = DIP_ALL;
      arg = skipwhite(p);
   }
   xp->context = EXPAND_RUNTIME;
   xp->input = mbText(arg);
}

private void
source_callback(Byte *fname, void *cookie) {
   (void)scriptRunFile(fname, cookie);
}

//Find an already loaded script "name". If found returns its script ID, else -1.
int
find_script_by_name(Byte *name) {
   for (int sid = script_items.len; sid > 0; --sid) {
      // We used to check inode here, but that doesn't work:
      // - If a script is edited and written, it may get a different inode number, even though to 
      // the user it is the same script.
      // - If a script is deleted and another script is written, with a different name, the inode 
      // may be re-used.
      ScriptItem* si = SCRIPT_ITEM(sid);
      if (si->sn_name != NULL && fnamecmp(si->sn_name, name) == 0)
         return sid;
   }
   return -1;
}

//Add a new scriptitem with all items initialized. When running out of memory "error" is set to 
//FAIL. Return the script ID.
private int
get_new_scriptitem(int* error) {
   static ScriptId last_current_SID = 0;
   int sid = ++last_current_SID;
   ScriptItem* si = NULL;

   if (ga_grow(&script_items, (int)(sid - script_items.len)) == FAIL) {
      *error = FAIL;
      return sid;
   }
   while (script_items.len < sid) {
      si = ALLOC_CLEAR_ONE(ScriptItem);
      ++script_items.len;
      SCRIPT_ITEM(script_items.len) = si;
      si->sn_name = NULL;

      // Allocate the local script variables to use for this script.
      new_script_vars(script_items.len);
      ga_init2(&si->sn_var_vals, sizeof(Svar), 10);
      ga_init2(&si->sn_imports, sizeof(Imported), 10);
      ga_init2(&si->sn_type_list, sizeof(TypeSpec), 10);
   }

   // "si" can't be NULL, check only to avoid a compiler warning
   if (si)
      // Used to check script variable index is still valid.
      si->sn_script_seq = scriptPosG.seq;

   return sid;
}

int
get_new_scriptitem_for_fname(int* error, CS fname) {
   int sid = get_new_scriptitem(error);

   if (*error == OK) {
      ScriptItem* si = SCRIPT_ITEM(sid);
      si->sn_name = copyStr(fname);
      si->sn_state = SN_STATE_NOT_LOADED;
   }
   return sid;
}

//If the script for "sid" is a symlink and "sn_source_sid" is not set then initialize it. A new 
//script_item is created if needed.
void
check_script_symlink(int sid) {
   ScriptItem* si = SCRIPT_ITEM(sid);
   if (si->sn_syml_checked || si->sn_sourced_sid > 0)
      return;
   si->sn_syml_checked = true;

   // If fname is a symbolic link, create an script_item for the real file.

   CS real_fname = fiExpandAndCopy(si->sn_name, true);
   if (real_fname != NULL && STRCMP(real_fname, si->sn_name) != 0) {
      int real_sid = find_script_by_name(real_fname);
      int error2 = OK;
      if (real_sid < 0) {
          real_sid = get_new_scriptitem_for_fname(&error2, real_fname);
      }
      if (error2 == OK) {
         si = SCRIPT_ITEM(sid);
         si->sn_sourced_sid = real_sid;
      }
   }
   eeglFree(real_fname);
}

private void
find_script_callback(Byte *fname, void *cookie) {
   int error = OK;
   int *ret_sid = cookie;

   int sid = find_script_by_name(fname);
   if (sid < 0)
      // script does not exist yet, create a new scriptitem
      sid = get_new_scriptitem_for_fname(&error, fname);
   *ret_sid = sid;
}

//Find the patterns in "name" in all directories in "path" and invoke
//"callback(fname, cookie)".
//"prefix" is prepended to each pattern in "name".
//When "flags" has DIP_ALL: source all files, otherwise only the first one.
//When "flags" has DIP_DIR: find directories instead of files.
//When "flags" has DIP_ERR: give an error message if there is no match.
//
//Return FAIL when no file could be sourced, OK otherwise.
int
doInPath(
   CS path,
   CS prefix,
   CS name,
   int    flags,
   void   (*callback)(Byte *fname, void *ck),
   void   *cookie
) {
   CS tail;
   int did_one = false;
   ExpandMatch files = {};
   files.a = createArena();

   Byte builder[MAXPATHL];
   if (p_verbose > 10 && name) {
      verbose_enter();
      if (*prefix != ZERO)
         smsg(_("Searching for \"%s\" under \"%s\" in \"%s\""), name, prefix, path);
      else
         smsg(_("Searching for \"%s\" in \"%s\""), name, path);
      verbose_leave();
   }

   // Loop over all entries in 'runtimepath'.
   CS p = path;
   while (*p != ZERO && ((flags & DIP_ALL) || !did_one)) {

      // Copy the path from 'runtimepath' to builder[].
      doCutPathFromListOfPaths(OUT &p, OUT builder, MAXPATHL, S",");
      Unt buflen = STRLEN(builder);

      // Skip after or non-after directories.
      if (flags & (DIP_NOAFTER | DIP_AFTER)) {
         Boole isAfter = buflen >= 5 && STRCMP(builder + buflen - 5, "after") == 0;

         if ((isAfter && (flags & DIP_NOAFTER)) || (!isAfter && (flags & DIP_AFTER)))
            continue;
      }

      if (!name) {
         (*callback)(builder, (void *) &cookie);
         if (!did_one)
             did_one = (cookie == NULL);
      } ei (buflen + 2 + STRLEN(prefix) + STRLEN(name) < MAXPATHL) {
         add_pathsep(builder);
         STRCAT(builder, prefix);
         tail = builder + STRLEN(builder);

         // Loop over all patterns in "name"
         CS np = name;
         while (*np != ZERO && ((flags & DIP_ALL) || !did_one)) {
            // Append the pattern from "name" to builder[].
            doCutPathFromListOfPaths(OUT &np, OUT tail, (int)(MAXPATHL - (tail - builder)), S"\t ");

            if (p_verbose > 10) {
               verbose_enter();
               smsg(_("Searching for \"%s\""), builder);
               verbose_leave();
            }
            CS builderString = builder;

            // Expand wildcards, invoke the callback for each match.
            if (gen_expand_wildcards(
                  1, (Arr(CS))&builderString, (flags & DIP_DIR) ? EW_DIR : EW_FILE, 
                  OUT &files
                ) == OK
            ) {
               for (Unt i = 0; i < files.len; ++i) {
                  (*callback)(files.c[i], cookie);
                  did_one = true;
                  if ((flags & DIP_ALL) == 0)
                     break;
               }
            }
         }
      }
   }
   
   if (!did_one && name) {
      if (flags & DIP_ERR)
         showErrFmtMsg(_(e_directory_not_found_in_str_str), path, name);
      ei (p_verbose > 0) {
         verbose_enter();
         smsg(_("not found in '%s': \"%s\""), path, name);
         verbose_leave();
      }
   }
   
   deleteArena(files.a);  
   return did_one ? OK : FAIL;
}

//Find "name" in "path". When found, invoke the callback function for it: callback(fname, "cookie")
//When "flags" has DIP_ALL repeat for all matches, otherwise only the first one is used.
//Return OK when at least one match found, FAIL otherwise.
//
//If "name" is NULL, calls callback for each entry in "path". Cookie is passed by reference in
//this case, setting it to NULL indicates that callback has done its job.
private int
do_in_path_and_pp(
   Byte   *path,
   Byte   *name,
   int      flags,
   void   (*callback)(Byte *fname, void *ck),
   OUT void   *cookie
) {
   int done = FAIL;

   if ((flags & DIP_NORTP) == 0)
      done = doInPath(path, E, name, flags, callback, cookie);

   if ((done == FAIL || (flags & DIP_ALL)) && (flags & DIP_START))
      done = doInPath(runtimePath, S"pack/*/start/*/", name, flags, callback, cookie);

   if ((done == FAIL || (flags & DIP_ALL)) && (flags & DIP_OPT))
      done = doInPath(runtimePath, S"pack/*/opt/*/", name, flags, callback, cookie);

   return done;
}

//Just like do_in_path_and_pp(), using 'runtimepath' for "path".
int
do_in_runtimepath(
   Byte   *name,
   int      flags,
   void   (*callback)(Byte *fname, void *ck),
   void   *cookie
) {
   return do_in_path_and_pp(runtimePath, name, flags, callback, cookie);
}

//Source the file "name" from all directories in runtime path.
//"name" can contain wildcards.
//When "flags" has DIP_ALL: source all files, otherwise only the first one.
//
//return FAIL when no file could be sourced, OK otherwise.
int
source_runtime(Byte *name, int flags) {
   return source_in_path(runtimePath, name, flags, NULL);
}

//Just like source_runtime(), but use "path" instead of 'runtimepath'
//and return the script ID in "ret_sid".
int
source_in_path(CS path, CS name, int flags, OUT int* ret_sid) {
   return do_in_path_and_pp(path, name, flags, source_callback, OUT ret_sid);
}

//Find "name" in runtime path. If found, a new scriptitem is created for it and its script ID is 
//returned. If not found, return -1.
int
find_script_in_rtp(Byte *name){
   int sid = -1;

   (void)do_in_path_and_pp(
      runtimePath, name, DIP_START | DIP_NOAFTER, find_script_callback, OUT &sid
   );
   return sid;
}

// Expand wildcards in "pat" and invoke scriptRunFile() for each match.
private void
source_all_matches(Byte *pat) {
   ExpandMatch files = {};
   files.a = createArena();

   if (gen_expand_wildcards(1, &pat, EW_FILE, OUT &files) != OK)
      return;

   for (Unt i = 0; i < files.len; ++i)
      (void)scriptRunFile(files.c[i], NULL);
   
   deleteArena(files.a);
}

// Load scripts in "plugin" and "ftdetect" directories of the package.
private int
load_pack_plugin(Byte *fname) {
   static CS plugpat = (CS)"%s/plugin/**/*.vim";
   static CS ftpat = (CS)"%s/ftdetect/*.vim";
   CS ffname = fiExpandAndCopy(fname, true);
   int retval = FAIL;

   int len = (int)STRLEN(ffname) + (int)STRLEN(ftpat);
   CS pat = alloc(len);
   eeSnprintf(pat, len, plugpat, ffname);
   source_all_matches(pat);

   CS comm = copyStr(S"g:did_load_filetypes");

   // If filetype.vim wasn't loaded yet, the scripts will be found when it loads.
   if (eval_to_number(comm, false) > 0) {
       executeCommLine((CS)"augroup filetypedetect");
       eeSnprintf(pat, len, ftpat, ffname);
       source_all_matches(pat);
       executeCommLine((CS)"augroup END");
   }
   eeglFree(comm);
   eeglFree(pat);
   retval = OK;

   eeglFree(ffname);
   return retval;
}

// used for "cookie" of add_pack_plugin()
private int APP_ADD_DIR;
private int APP_LOAD;
private int APP_BOTH;

private void
add_pack_plugin(Byte *fname, void *cookie) {
   if (cookie != &APP_LOAD) {
      return;
   }
   if (cookie != &APP_ADD_DIR)
      load_pack_plugin(fname);
}

// Load plugins from all packages in the "start" directory.
void
load_start_packages(void){
   did_source_packages = true;
   doInPath(
         runtimePath, E, S"pack/*/start/*", DIP_ALL + DIP_DIR, add_pack_plugin, &APP_LOAD
   );
}

//":packloadall". Find plugins in the package directories and source them.
void
c_packloadall(Invocation* invo) {
   if (!did_source_packages || invo->forceit) {
      // First do a round to add all directories to 'runtimepath', then load
      // the plugins. This allows for plugins to use an autoload directory of another plugin.
      load_start_packages();
   }
}

//":packadd[!] {name}"
void
c_packadd(Invocation* invo) {
   static CS plugpat = (CS)"pack/*/%s/%s";
   int len;
   int res = OK;

   // Round 1: use "start", round 2: use "opt".
   for (int round = 1; round <= 2; ++round) {
      // Only look under "start" when loading packages wasn't done yet.
      if (round == 1 && did_source_packages)
          continue;

      len = (int)STRLEN(plugpat) + (int)STRLEN(invo->arg) + 5;
      CS pat = alloc(len);
      eeSnprintf(pat, len, plugpat, round == 1 ? "start" : "opt", invo->arg);
      // The first round don't give a "not found" error, in the second round
      // only when nothing was found in the first round.
      res = doInPath(
         runtimePath, E, pat,
         DIP_ALL + DIP_DIR + (round == 2 && res == FAIL ? DIP_ERR : 0),
         add_pack_plugin, invo->forceit ? &APP_ADD_DIR : &APP_BOTH
      );
      eeglFree(pat);
   }
}

//Sort "gap" and remove duplicate entries. "gap" is expected to contain a
//list of file names in allocated memory.
void
remove_duplicates(OUT ExpandMatch* matches) {
   Arr(CS) fnames = matches->c;

   sortStrings(fnames, matches->len);
   for (Unt i = matches->len - 1; i < matches->len; --i) {
      if (fnamecmp(fnames[i - 1], fnames[i]) == 0) {
         for (Unt j = i + 1; j < matches->len; ++j)
            fnames[j - 1] = fnames[j];
         matches->len--;
      }
   } 
}

private void
expandRuntimeDirInternal(
   Text pat,
   Unt flags,
   Boole keep_ext,
   OUT ExpandMatch* matches,
   CS dirnames[]
){
   for (int i = 0; dirnames[i] != NULL; ++i) {
      const Unt bufLen = STRLEN(dirnames[i]) + pat.len + 64;
      CS buf = alloc(bufLen);
      Unt gloflags = 0;
      Boole expand_dirs = false;

      // Build base pattern
      eeSnprintf(
         buf, bufLen, "%s%s%s%s", *dirnames[i] ? dirnames[i] : S"", *dirnames[i] ? S"/" : S"",
         pat.c, "*.vim"
      );

expand:
      if ((flags & DIP_NORTP) == 0)
         fiGlobpath(runtimePath, buf, OUT matches, gloflags, expand_dirs);

      if (flags & DIP_START) {
         // Build complete search path: pack/*/start/*/dirnames[i]/pat*.vim
         eeSnprintf(buf, bufLen, "pack/*/start/*/%s%s%s%s",
             *dirnames[i] ? dirnames[i] : S"",
             *dirnames[i] ? S"/" : S"",
             pat.c,
             expand_dirs ? "*" : "*.vim"
         );
         fiGlobpath(runtimePath, buf, OUT matches, gloflags, expand_dirs);
      }

      if ((flags & DIP_OPT) != 0) {
         // Build complete search path: pack/*/opt/*/dirnames[i]/pat*.vim
         eeSnprintf(buf, bufLen, "pack/*/opt/*/%s%s%s%s",
             *dirnames[i] ? dirnames[i] : S"",
             *dirnames[i] ? S"/" : S"", pat.c,
             expand_dirs ? "*" : "*.vim"
         );
         fiGlobpath(runtimePath, buf, OUT matches, gloflags, expand_dirs);
      }

      // Second round for directories
      if (*dirnames[i] == ZERO && !expand_dirs) {
         // expand dir names in another round
         eeSnprintf(buf, bufLen, "%s*", pat.c);
         gloflags = WILD_ADD_SLASH;
         expand_dirs = true;
         goto expand;
      }

      eeglFree(buf);
   }

   int pat_pathsep_cnt = 0;
   for (Unt i = 0; i < pat.len; ++i) {
      if (pat.c[i] == '/')
         ++pat_pathsep_cnt;
   }

   for (Unt i = 0; i < matches->len; ++i) {
      CS match = matches->c[i];
      CS s = match;
      CS e = s + STRLEN(s);
      if (e - 4 > s && !keep_ext && STRNICMP(e - 4, ".vim", 4) == 0) {
         e -= 4;
         *e = ZERO;
      }

      int match_pathsep_cnt = (e > s && e[-1] == '/') ? -1 : 0;
      for (s = e; s > match; MB_PTR_BACK(match, s)) {
         if (s < match || (*s == '/' && ++match_pathsep_cnt > pat_pathsep_cnt))
            break;
      }
      ++s;
      if (s != match)
         MEMMOVE(match, s, e - s + 1);
   }

   if (matches->len == 0)
      return;

   // Sort and remove duplicates which can happen when specifying multiple directories in dirnames
   remove_duplicates(OUT matches);
}

//Expand runtime file names. Search from 'runtimepath':
//  'runtimepath'/{dirnames}/{pat}.vim
//When "flags" has DIP_START: search also from "start" of 'packpath':
//  'packpath'/pack/ * /start/ * /{dirnames}/{pat}.vim
//When "flags" has DIP_OPT: search also from "opt" of 'packpath':
//  'packpath'/pack/ * /opt/ * /{dirnames}/{pat}.vim
//"dirnames" is an array with one or more directory names.
int
expandRuntimeDir(
   CS pat,
   Unt flags,
   Arr(CS) dirnames,
   OUT ExpandMatch* matches
) {
   expandRuntimeDirInternal(text(pat), flags, false, OUT matches, dirnames);
   if (matches->len == 0)
      return FAIL;

   return OK;
}

// Handle command line completion for :runtime command.
private int
expand_runtime_cmd(CS pat, OUT ExpandMatch* matches) {
   Text patTxt = text(pat);
   CS dirnames[] = {S"", NULL};
   expandRuntimeDirInternal(patTxt, runtime_expand_flags, true, OUT matches, dirnames);

   // Try to complete values for [where] argument when none was found.
   if (runtime_expand_flags == 0) {
      CS where_values[] = {SMAP((CS), "START", "OPT", "PACK", "ALL" )};
      for (Unt i = 0; i < ARRAY_LENGTH(where_values); ++i) {
         if (STRNCMP(patTxt.c, where_values[i], patTxt.len) == 0) {
            CS p = copyStrA(where_values[i], matches->a);
            addExpandMatch(p, OUT matches);
         }
      } 
   }

   if (matches->len == 0)
      return FAIL;
   return OK;
}

//Expand loadplugin names: 'packpath'/pack/ * /opt/{pat}
private int
expandPackAddDir(CS pat, OUT ExpandMatch* matches) {
   CS e;
   CS match;
   int pat_len = (int)STRLEN(pat);

   CS s = alloc(pat_len + 26);
   SPRINTF(s, "pack/*/opt/%s*", pat);
   fiGlobpath(runtimePath, s, OUT matches, 0, true);
   eeglFree(s);
   
   if (matches->len == 0)
      return FAIL;

   for (Unt i = 0; i < matches->len; ++i) {
      match = matches->c[i];
      s = fiGetShortFiName(match);
      e = s + STRLEN(s);
      MEMMOVE(match, s, e - s + 1);
   }


   //Sort and remove duplicates which can happen when specifying multiple directories in dirnames
   remove_duplicates(OUT matches);
   return OK;
}

private void
cmd_source(Byte *fname, Invocation* invo) {
   Boole clearvars = false;

   if (*fname != ZERO && STRNCMP(fname, "++clear", 7) == 0) {
      // ++clear argument is supplied
      clearvars = true;
      fname = fname + 7;
      if (*fname != ZERO) {
         showErrFmtMsg(_(e_invalid_argument_str), invo->arg);
         return;
      }
   }

   if (*fname != ZERO && invo != NULL && invo->addr_count > 0) {
      // if a filename is specified to :source, then a range is not allowed
      emsg(_(e_no_range_allowed));
      return;
   }

   if (invo && *fname == ZERO) {
      if (invo->forceit)
         // a file name is needed to source normal mode commands
         emsg(_(e_argument_required));
      else
         // source commands from the current buffer
         scriptRunFileInternal(NULL, NULL, invo, clearvars);
   } ei (invo != NULL && invo->forceit)
      // ":source!": read Normal mode commands
      // Need to execute the commands directly.  This is required at least
      // for:
      // - ":g" command busy
      // - after ":argdo", ":windo" or ":bufdo"
      // - another command follows
      // - inside a loop
      openscript( fname, global_busy || listcmd_busy || invo->cstack->ind >= 0);

   // ":source" read commands
   ei (scriptRunFile(fname, NULL) == FAIL) {
      showErrFmtMsg(_(e_cant_open_file_str), fname);
   } 
}

// ":source {fname}"
void
c_source(Invocation* invo) {
   cmd_source(invo->arg, invo);
}

// ":options"
void
c_options(Invocation   *invo UNUSED) {
   Byte  buf[500];
   int multi_mods = 0;

   buf[0] = ZERO;
   (void)add_win_cmd_modifiers(buf, &commModifierG, &multi_mods);

   eeSetenv((CS)"OPTWIN_CMD", buf);
   cmd_source((CS)SYS_OPTWIN_FILE, NULL);
}

// ":source" and associated commands.

// Return the address holding the next breakpoint line for a source cookie.
LineNr *
source_breakpoint(void *cookie) {
   return &((SourceCookie *)cookie)->breakpoint;
}

//Return the address holding the debug tick for a source cookie.
int *
source_dbg_tick(void *cookie) {
   return &((SourceCookie *)cookie)->dbg_tick;
}

//Return the nesting level for a source cookie.
int
source_level(void *cookie) {
   return ((SourceCookie *)cookie)->level;
}

//Return the readahead line. Note that the pointer may become invalid when
//getting the next line, if it's concatenated with the next one.
CS
source_nextline(void *cookie) {
   return ((SourceCookie *)cookie)->nextline;
}

# define USE_FOPEN_NOINH
//Special function to open a file without handle inheritance. When possible the handle is closed 
//on exec().
private FILE *
fopen_noinh_readbin(CS filename) {
   int fd_tmp = open((char*)filename, O_RDONLY, 0);

   if (fd_tmp == -1)
      return NULL;

   int fdflags = fcntl(fd_tmp, F_GETFD);
   if (fdflags >= 0 && (fdflags & FD_CLOEXEC) == 0)
       (void)fcntl(fd_tmp, F_SETFD, fdflags | FD_CLOEXEC);

   return fdopen(fd_tmp, READBIN);
}

//Initialization for sourcing lines from the current book. Read all the
//lines from the book and store them in the cookie arraylist.
//Return a pointer to the name ":source buffer=<n>" on success and NULL on failure.
private CS
initCurBookForSourcing(OUT SourceCookie* sp, Invocation* invo) {
   if (!curBook)
      return NULL;

   LineNr   curr_lnum;
   CS fname;
   // Use ":source buffer=<num>" as the script name
   if (curBook->fullFileName)
      fname = copyStr(curBook->fullFileName);
   else {
      eeSnprintf(IObuff, IOSIZE, ":source buffer=%d", curBook->fiNum);
      fname = copyStr(IObuff);
   }

   ga_init2(&sp->buflines, sizeof(CS), 100);

   // Copy the lines from the buffer into a grow array
   CS line = null;
   for (curr_lnum = invo->line1; curr_lnum <= invo->line2; curr_lnum++) {
      line = copyStr(ml_get(curr_lnum));
      if (ga_add_string(&sp->buflines, line) == FAIL)
         goto errret;
   }
   sp->buf_lnum = 0;
   sp->sourceFromCurBook = true;
   // When sourcing a range of lines from a buffer, use buffer line number.
   sp->sourcing_lnum = invo->line1 - 1;

   return fname;

errret:
   eeglFree(fname);
   eeglFree(line);
   ga_clear_strings(&sp->buflines);
   return NULL;
}

//If script "sid" is not loaded yet then load it now. Caller must make sure "sid" is a valid script 
//ID. "loaded" is set to true if the script had to be loaded. Return FAIL if loading fails, OK if 
//already loaded or loaded now.
int
may_load_script(int sid, int *loaded) {
   ScriptItem *si = SCRIPT_ITEM(sid);

   if (si->sn_state == SN_STATE_NOT_LOADED) {
      *loaded = true;
      if (scriptRunFile(si->sn_name, NULL) == FAIL) {
         showErrFmtMsg(_(e_cant_open_file_str), si->sn_name);
         return FAIL;
      }
   }
   return OK;
}


//Read the file "fname" and execute its lines as commands.
//When "ret_sid" is not NULL and we loaded the script before, don't load it again.
//
//The "invo" argument is used when sourcing lines from a buffer instead of a file.
//
//If "clearvars" is true, then for scripts which are loaded more than
//once, clear all the functions and variables previously defined in that script.
//
//This function may be called recursively!
//
//Return FAIL if file could not be opened, OK otherwise.
//If a ScriptItem was found or created, "*retSid" is set to the SID.
private int
scriptRunFileInternal(
   CS fname,
   OUT int* ret_sid,
   Invocation* invo,
   Boole      clearvars
){
   SourceCookie cookie;
   CS fname_not_fixed = NULL;
   CS fname_exp = NULL;
   CS firstline = NULL;
   int retval = FAIL;
   ScriptPos save_scriptPosG;
   TimeVal tv_rel;
   TimeVal tv_start;
   int save_stickyCommandModifiersG = stickyCommandModifiersG;
   int trigger_source_post = false;
   FnCallEntry funccalp_entry;
   int save_debug_break_level = debug_break_level;
   ScriptItem* si = NULL;
   ESTACK_CHECK_DECLARATION;

   CLEAR_FIELD(cookie);
   if (!fname) {
      // sourcing lines from a buffer
      fname_exp = initCurBookForSourcing(OUT &cookie, invo);
      if (!fname_exp)
         return FAIL;
   } else {
      fname_not_fixed = doExpandEnvInMultiplePaths(fname);
      if (!fname_not_fixed)
         goto theend;
      fname_exp = fiExpandAndCopy(fname_not_fixed, true);
      if (mch_isdir(fname_exp)) {
         smsg(_("Cannot source a directory: \"%s\""), fname);
         goto theend;
      }
   }

   // See if we loaded this script before.
   int sid = find_script_by_name(fname_exp);
   if (sid > 0 && ret_sid && SCRIPT_ITEM(sid)->sn_state != SN_STATE_NOT_LOADED){
      // Already loaded and no need to load again, return here.
      *ret_sid = sid;
      retval = OK;
      goto theend;
   }

   // Apply SourceCmd autocommands, they should get the file and source it.
   if (has_autocmd(EVENT_SOURCECMD, fname_exp, NULL)
       && applyAutocomms(EVENT_SOURCECMD, fname_exp, fname_exp, false, curBook)
   ) {
      retval = aborting() ? FAIL : OK;
      if (retval == OK)
         // Apply SourcePost autocommands.
         applyAutocomms(EVENT_SOURCEPOST, fname_exp, fname_exp, false, curBook);
      goto theend;
   }

   // Apply SourcePre autocommands, they may get the file.
   applyAutocomms(EVENT_SOURCEPRE, fname_exp, fname_exp, false, curBook);

   if (!cookie.sourceFromCurBook) {
#ifdef USE_FOPEN_NOINH
      cookie.fp = fopen_noinh_readbin(fname_exp);
#else
      cookie.fp = fopen((char *)fname_exp, READBIN);
#endif
   }

   if (cookie.fp == NULL && !cookie.sourceFromCurBook) {
      if (p_verbose > 0) {
         verbose_enter();
         if (SOURCING_NAME == NULL)
            smsg(_("could not source \"%s\""), fname);
         else
            smsg(_("line %ld: could not source \"%s\""), SOURCING_LNUM, fname);
         verbose_leave();
      }
      goto theend;
   }

   //The file exists.
   //- In verbose mode, give a message.
   if (p_verbose > 1) {
      verbose_enter();
      if (SOURCING_NAME == NULL)
         smsg(_("sourcing \"%s\""), fname);
      else
         smsg(_("line %ld: sourcing \"%s\""), SOURCING_LNUM, fname);
      verbose_leave();
   }

   // Check if this script has a breakpoint.
   cookie.breakpoint = dbg_find_breakpoint(true, fname_exp, (LineNr)0);
   cookie.fname = fname_exp;
   cookie.dbg_tick = debug_tick;

   cookie.level = ex_nesting_level;

   if (time_fd)
      time_push(&tv_rel, &tv_start);

   //"legacy" does not apply to commands in the script
   stickyCommandModifiersG = 0;

   save_scriptPosG = scriptPosG;
   scriptPosG.lineNr = 0;

   //Don't use local function variables, if called from a function.
   //Also starts profiling timer for nested script.
   save_funccal(&funccalp_entry);

   //Reset "keyWasTypedG" to avoid some commands thinking they are invoked
   //interactively.  E.g. defining a function would output indent.
   int save_keyWasTypedG = keyWasTypedG;
   keyWasTypedG = false;

   //Check if this script was sourced before to find its SID.
   //Always use a new sequence number.
   scriptPosG.seq = ++last_current_SID_seq;
   if (sid > 0) {
      int      todo;
      EeSetItem   *hi;
      DictItem   *di;

      // loading the same script again
      scriptPosG.sid = sid;
      si = SCRIPT_ITEM(sid);
      if (si->sn_state == SN_STATE_NOT_LOADED) {
         // this script was found but not loaded yet
         si->sn_state = SN_STATE_NEW;
      } else {
         si->sn_state = SN_STATE_RELOAD;

         if (!clearvars) {
            // Script-local variables remain but "const" can be set again.
            EeSet* ht = &SCRIPT_VARS(sid);
            todo = (int)ht->count;
            FOR_ALL_HASHTAB_ITEMS(ht, hi, todo) {
               if (!HASHITEM_EMPTY(hi)) {
                  --todo;
                  di = HI2DI(hi);
                  di->flags |= DI_FLAGS_RELOAD;
               }
            } 
         }
      }
      if (ret_sid)
          *ret_sid = sid;
   } else {
      int error = OK;

      // It's new, generate a new SID and initialize the scriptitem.
      sid = get_new_scriptitem(&error);
      scriptPosG.sid = sid;
      if (error == FAIL)
         goto almosttheend;
      si = SCRIPT_ITEM(sid);
      si->sn_name = fname_exp;
      fname_exp = copyStr(si->sn_name);  // used for autocmd
      if (ret_sid)
         *ret_sid = sid;
   }

   //Keep the sourcing name/lnum, for recursive calls.
   estack_push(ETYPE_SCRIPT, si->sn_name, 0);
   ESTACK_CHECK_SETUP;

   firstline = getsourceline(0, (void *)&cookie, 0, true);

   // Call doCommand, which will call getsourceline() to get the lines.
   doCommand(firstline, &getsourceline, (void *)&cookie, DOCMD_VERBOSE|DOCMD_NOWAIT|DOCMD_REPEAT);
   retval = OK;

   if (gotInterruptG)
      emsg(_(e_interrupted));
   ESTACK_CHECK_NOW;
   estack_pop();
   if (p_verbose > 1) {
      verbose_enter();
      smsg(_("finished sourcing %s"), fname);
      if (SOURCING_NAME)
         smsg(_("continuing in %s"), SOURCING_NAME);
      verbose_leave();
   }
   if (time_fd) {
      eeSnprintf(IObuff, IOSIZE, "sourcing %s", fname);
      time_msg(IObuff, &tv_start);
      time_pop(&tv_rel);
   }

   if (!gotInterruptG)
      trigger_source_post = true;

   //After a "finish" in debug mode, need to break at first command of next sourced file.
   if (save_debug_break_level > ex_nesting_level && debug_break_level == ex_nesting_level)
      ++debug_break_level;

almosttheend:
   // Get "si" again, "script_items" may have been reallocated.
   si = SCRIPT_ITEM(sid);

   restore_funccal();

   keyWasTypedG = save_keyWasTypedG;
   scriptPosG = save_scriptPosG;

   if (cookie.fp != NULL)
      fclose(cookie.fp);
   if (cookie.sourceFromCurBook)
      ga_clear_strings(&cookie.buflines);
   eeglFree(cookie.nextline);
   eeglFree(firstline);

   if (trigger_source_post)
      applyAutocomms(EVENT_SOURCEPOST, fname_exp, fname_exp, false, curBook);

theend:
   if (sid > 0 && ret_sid && fname_not_fixed && fname_exp) {
      int not_fixed_sid = find_script_by_name(fname_not_fixed);

      //If "fname_not_fixed" is a symlink then we source the linked file.
      //If the original name is in the script list we add the ID of the
      //script that was actually sourced.
      if (SCRIPT_ID_VALID(not_fixed_sid) && not_fixed_sid != sid)
          SCRIPT_ITEM(not_fixed_sid)->sn_sourced_sid = sid;
    }

    eeglFree(fname_not_fixed);
    eeglFree(fname_exp);
    stickyCommandModifiersG = save_stickyCommandModifiersG;
    return retval;
}

int
scriptRunFile(CS fname, OUT int* retSid){
   return scriptRunFileInternal(fname, OUT retSid, NULL, false);
}

//":scriptnames"
void
c_scriptnames(Invocation* invo) {
   if (invo->addr_count > 0 || *invo->arg != ZERO) {
      // :script {scriptId}: edit the script
      if (invo->addr_count > 0 && !SCRIPT_ID_VALID(invo->line2))
         emsg(_(e_invalid_argument));
      else {
         if (invo->addr_count > 0)
            invo->arg = SCRIPT_ITEM(invo->line2)->sn_name;
         else {
            doExpandEnv(OUT nameBuffTextG, invo->arg);
            invo->arg = nameBuffG;
         }
         do_exedit(invo, NULL);
      }
      return;
   }

   for (int i = 1; i <= script_items.len && !gotInterruptG; ++i) {
      ScriptItem *si = SCRIPT_ITEM(i);

      if (si->sn_name) {
         Byte sourced_buf[20];

         home_replace(si->sn_name, nameBuffG, MAXPATHL, true);
         if (si->sn_sourced_sid > 0)
            eeSnprintf(sourced_buf, 20, "->%d", si->sn_sourced_sid);
         else
            sourced_buf[0] = ZERO;
         eeSnprintf(IObuff, IOSIZE, "%3d%s%s: %s",
             i,
             sourced_buf,
             si->sn_state == SN_STATE_NOT_LOADED ? " A" : "",
             nameBuffG);
         if (!message_filtered(IObuff)) {
            msg_putchar('\n');
            msg_outtrans(IObuff);
            out_flush();       // output one line at a time
            ui_breakcheck();
         }
      }
   }
}

//Get a pointer to a script name. Used for ":verbose set". Message appended to "Last set from "
CS
get_scriptname(ScriptId id) {
   switch (id) {
   case SID_CMDARG: return (CS)_("--comm argument");
   case SID_CARG: return (CS)_("-c argument");
   case SID_ENV: return (CS)_("environment variable");
   case SID_ERROR: return (CS)_("error handler");
   case SID_WINLAYOUT: return (CS)_("changed window size");
   default: return SCRIPT_ITEM(id)->sn_name;
   }
}

# if defined(EXITFREE) || defined(PROTO)
void
free_scriptnames(void) {
   for (int i = script_items.len; i > 0; --i) {
      ScriptItem *si = SCRIPT_ITEM(i);

      // the variables themselves are cleared in evalvars_clear()
      eeglFree(si->sn_vars);

      eeglFree(si->sn_name);
      free_imports_and_script_vars(i);
      eeglFree(si);
   }
   ga_clear(&script_items);
}

void
free_autoload_scriptnames(void) {
   ga_clear_strings(&ga_loaded);
}
# endif

LineNr
get_sourced_lnum(LineGetter fgetline, void *cookie) {
   return fgetline == getsourceline
      ? ((SourceCookie *)cookie)->sourcing_lnum
      : SOURCING_LNUM;
}

//Return a List of script-local functions defined in the script with id 'sid'.
private List *
get_script_local_funcs(ScriptId sid) {
   EeSet   *functbl;
   EeSetItem   *hi;
   Ulong   todo;

   List* l = list_alloc();

   // Iterate through all the functions in the global function hash table
   // looking for functions with script ID 'sid'.
   functbl = func_tbl_get();
   todo = functbl->count;
   FOR_ALL_HASHTAB_ITEMS(functbl, hi, todo) {
      UserFunc   *fp;

      if (HASHITEM_EMPTY(hi))
         continue;

      --todo;
      fp = HI2UF(hi);

      // Add active functions with script id == 'sid'
      if (!(fp->uf_flags & FC_DEAD) && (fp->scriptCtx.sid == sid)) {
         Byte   *name;

         if (fp->uf_name_exp != NULL)
            name = fp->uf_name_exp;
         else
            name = fp->uf_name;

         list_append_string(l, name, -1);
      }
   }

   return l;
}

//getscriptinfo() function
void
f_getscriptinfo(Arr(Var) argvars, Var* returnVar) {
   Byte   *pat = NULL;
   RegMatch   regmatch;
   int      filterpat = false;
   ScriptId   sid = -1;

   allocReturnList(returnVar);

   if (check_for_oself_arg(argvars, 0) == FAIL)
      return;

   List* l = returnVar->list;

   regmatch.regprog = NULL;
   regmatch.rm_ic = p_ic;

   if (argvars[0].tag == VAR_BAG) {
      DictItem *sid_di = bagFind(argvars[0].bag, tConst("sid"));
      if (sid_di != NULL) {
         Boole error = false;
         sid = varGetNumberChk(&sid_di->c, OUT &error);
         if (error)
            return;
         if (sid <= 0) {
            showErrFmtMsg(_(e_invalid_value_for_argument_str_str), S"sid", tv_get_string(&sid_di->c));
            return;
         }
      } else {
         pat = bagGetString(argvars[0].bag, tConst("name"), true);
         if (pat)
            regmatch.regprog = compileRegexp(pat, RE_MAGIC + RE_STRING);
         if (regmatch.regprog)
            filterpat = true;
      }
   }

   for (Long i = sid > 0 ? sid : 1;
             (i == sid || sid <= 0) && i <= script_items.len; ++i
   ) {
      ScriptItem   *si = SCRIPT_ITEM(i);
      Bag      *d;

      if (si->sn_name == NULL)
          continue;

      if (filterpat && !eeRegexec(&regmatch, si->sn_name, (ColNr)0))
          continue;

      d = allocBag();
      if (     listAppendBag(l, d) == FAIL
            || bagAddString(d, S"name", si->sn_name) == FAIL
            || bagAddNumber(d, S"sid", i) == FAIL
            || bagAddNumber(d, S"sourced", si->sn_sourced_sid) == FAIL
            || bagAdd_bool(d, S"autoload", si->sn_state == SN_STATE_NOT_LOADED) == FAIL)
          return;

      // When a script ID is specified, return information about only the
      // specified script, and add the script-local variables and functions.
      if (sid > 0) {
          Bag   *var_dict;

          var_dict = dict_copy(&si->sn_vars->sv_dict, true, true,
                           get_copyID());
          if (var_dict == NULL
             || bagAddBag(d, S"variables", var_dict) == FAIL
             || bagAddList(d, S"functions", get_script_local_funcs(sid)) == FAIL)
         return;
      }
   }

   eeRegFree(regmatch.regprog);
   eeglFree(pat);
}


private CS
get_one_sourceline(SourceCookie *sp) {
   int len;
   int c;
   CS builder;
   int have_read = false;

   // use a growarray to store the sourced line
   ArrayList ga;
   ga_init2(&ga, 1, 250);

   // Loop until there is a finished line (or end-of-file).
   ++sp->sourcing_lnum;
   for (;;) {
      // make room to read at least 120 (more) characters
      if (ga_grow(&ga, 120) == FAIL)
          break;
      if (sp->sourceFromCurBook) {
         if (sp->buf_lnum >= sp->buflines.len)
            break;          // all the lines are processed
         ga_concat(&ga, ((Byte **)sp->buflines.c)[sp->buf_lnum]);
         sp->buf_lnum++;
         if (ga_grow(&ga, 1) == FAIL)
            break;
         builder = (CS)ga.c;
         builder[ga.len++] = ZERO;
         len = ga.len;
      } else {
         builder = (CS)ga.c;
         if (FGETS(builder + ga.len, ga.cap - ga.len, sp->fp) == NULL)
            break;
         len = ga.len + (int)STRLEN(builder + ga.len);
      }

      have_read = true;
      ga.len = len;

      // If the line was longer than the buffer, read more.
      if (ga.cap - ga.len == 1 && builder[len - 1] != '\n')
          continue;

      if (len >= 1 && builder[len - 1] == '\n') {  // remove trailing NL
         // The '\n' is escaped if there is an odd number of ^V's just
         // before it, first set "c" just before the 'V's and then check
         // len&c parities (is faster than ((len-c)%2 == 0)) -- Acevedo
         for (c = len - 2; c >= 0 && builder[c] == Ctrl_V; c--)
            {}
         if ((len & 1) != (c & 1)) {  // escaped NL, read more
            ++sp->sourcing_lnum;
            continue;
         }

         builder[len - 1] = ZERO;      // remove the NL
      }

      // Check for ^C here now and then, so recursive :so can be broken.
      line_breakcheck();
      break;
   }

   if (have_read)
      return (CS)ga.c;
   eeglFree(ga.c);
   return NULL;
}

//Get one full line from a sourced file. Called by doCommand() when it's called from scriptRunFile().
//
//Return a pointer to the line in allocated memory. Return NULL for end-of-file or some error.
CS
getsourceline(
   Unt c UNUSED,
   void *cookie,
   int indent UNUSED,
   GetlineAlgo options
){
   SourceCookie   *sp = (SourceCookie *)cookie;
   CS line;
   CS p;
   Boole do_bar_cont = options == GETLINE_CONCAT_CONTBAR;

   // If breakpoints have been added/deleted need to check for it.
   if ((sp->dbg_tick < debug_tick) && !sp->sourceFromCurBook) {
      sp->breakpoint = dbg_find_breakpoint(true, sp->fname, SOURCING_LNUM);
      sp->dbg_tick = debug_tick;
   }

   // Set the current sourcing line number.
   SOURCING_LNUM = sp->sourcing_lnum + 1;

   //Get current line. If there is a read-ahead line, use it, otherwise get
   //one now. "fp" is NULL if actually using a string.
   if (sp->finished || (!sp->sourceFromCurBook && sp->fp == NULL))
      line = NULL;
   ei (!sp->nextline)
      line = get_one_sourceline(sp);
   else {
      line = sp->nextline;
      sp->nextline = NULL;
      ++sp->sourcing_lnum;
   }

   //Only concatenate lines starting with a \ when the global option is set
   if (concatenateBackslashesG && line != NULL && options != GETLINE_NONE)    {
      // compensate for the one line read-ahead
      --sp->sourcing_lnum;

      //Get the next line and concatenate it when it starts with a
      //backslash. We always need to read the next line, keep it in sp->nextline.
      //Also check for a comment in between continuation lines: //backslash
      //Also check for an empty line, line starting with '|', but not "||".
      sp->nextline = get_one_sourceline(sp);
      if (sp->nextline
         && (*(p = skipwhite(sp->nextline)) == '\\'
                  || (p[0] == '"' && p[1] == '\\' && p[2] == ' ')
                  || (p[0] == '/' && p[1] == '/' && p[2] == '\\' && p[3] == ' ')
                  || (do_bar_cont && p[0] == '|' && p[1] != '|'))
      ){
         ArrayList    ga;

         ga_init2(&ga, sizeof(Byte), 400);
         ga_concat(&ga, line);
         if (*p == '\\')
            ga_concat(&ga, p + 1);
         ei (*p == '|') {
            ga_concat(&ga, (CS)" ");
            ga_concat(&ga, p);
         }
         for (;;) {
            eeglFree(sp->nextline);
            sp->nextline = get_one_sourceline(sp);
            if (sp->nextline == NULL)
                break;
            p = skipwhite(sp->nextline);
            if (*p == '\\' || (do_bar_cont && p[0] == '|' && p[1] != '|')) {
               //Line continuation via backslash
               //Adjust the growsize to the current length to speed up concatenating many lines.
               if (ga.len > 400) {
                  if (ga.len > 8000)
                     ga.ga_growsize = 8000;
                  else
                     ga.ga_growsize = ga.len;
               }
               if (*p == '\\')
                  ga_concat(&ga, p + 1);
               else {
                  ga_concat(&ga, (CS)" ");
                  ga_concat(&ga, p);
               }
            } ei (!((p[0] == '"' && p[1] == '\\' && p[2] == ' ')
                     || (p[0] == '/' && p[1] == '/' && p[2] == '\\' && p[3] == ' ')
                   )
            )
               break;
               // drop a # comment or "\ comment line
         }
         ga_append(&ga, ZERO);
          
         eeglFree(line);
         line = ga.c;
      }
   }

   // Did we encounter a breakpoint?
   if (!sp->sourceFromCurBook && sp->breakpoint != 0 && sp->breakpoint <= SOURCING_LNUM) {
      dbg_breakpoint(sp->fname, SOURCING_LNUM);
      // Find next breakpoint.
      sp->breakpoint = dbg_find_breakpoint(true, sp->fname, SOURCING_LNUM);
      sp->dbg_tick = debug_tick;
   }

   return line;
}

// Return true if sourcing a script either from a file or a buffer. Otherwise return false.
int
sourcing_a_script(Invocation* invo) {
   return (getline_equal(invo->ea_getline, invo->cookie, getsourceline));
}

// ":finish": Mark a sourced file as finished.
void
c_finish(Invocation* invo) {
   if (sourcing_a_script(invo))
      do_finish(invo, false);
   else
      emsg(_(e_finish_used_outside_of_sourced_file));
}

//Mark a sourced file as finished.  Possibly makes the ":finish" pending.
//Also called for a pending finish at the ":endtry" or after returning from
//an extra doCommand().  "reanimate" is used in the latter case.
void
do_finish(Invocation* invo, int reanimate) {
   if (reanimate)
      ((SourceCookie *)getline_cookie(invo->ea_getline, invo->cookie))->finished = false;

    // Cleanup (and inactivate) conditionals, but stop when a try conditional
    // not in its finally clause (which then is to be executed next) is found.
    // In this case, make the ":finish" pending for execution at the ":endtry".
    // Otherwise, finish normally.
   int idx = cleanup_conditionals(invo->cstack, 0, true);
   if (idx >= 0) {
      invo->cstack->pending[idx] = CSTP_FINISH;
      report_make_pending(CSTP_FINISH, NULL);
   } else
      ((SourceCookie *)getline_cookie(invo->ea_getline, invo->cookie))->finished = true;
}


//Return true when a sourced file had the ":finish" command: Don't give error
//message for missing ":endif". Return false when not sourcing a file.
int
sourceFileIsFinished(LineGetter fgetline, void* cookie) {
   return (getline_equal(fgetline, cookie, getsourceline)
       && ((SourceCookie *)getline_cookie( fgetline, cookie))->finished);
}

//Find the path of a script below the "autoload" directory.
//Return NULL if there is no "/autoload/" in the script name.
private CS
scriname_after_autoload(ScriptItem *si) {
   Byte   *p = si->sn_name;
   Byte   *res = NULL;

   for (;;) {
      Byte *n = (CS)strstr((char *)p, "autoload");

      if (n == NULL)
          break;
      if (n > p && n[-1] == '/' && n[8] == '/')
          res = n + 9;
      p = n + 8;
   }
   return res;
}

//For an autoload script "autoload/dir/script.vim" return the prefix
//"dir#script#" in allocated memory. Return NULL if anything is wrong.
CS
get_autoload_prefix(ScriptItem *si) {
   CS p = scriname_after_autoload(si);

   if (!p)
      return NULL;
   CS prefix = copyStr(p);

   // replace all '/' with '#' and locate ".vim" at the end
   for (p = prefix; *p != ZERO; p += utfCharLen(p)) {
      if (*p == '/')
         *p = '#';
      ei (STRCMP(p, ".vim") == 0) {
         p[0] = '#';
         p[1] = ZERO;
         return prefix;
      }
   }

   // did not find ".vim" at the end
   eeglFree(prefix);
   return NULL;
}

//Return the autoload script name for a function or variable name. Return NULL when out of memory.
//Caller must make sure that "name" contains AUTOLOAD_CHAR.
CS
autoload_name(Byte *name) {
   Byte   *p, *q = NULL;

   // Get the script file name: replace '#' with '/', append ".vim".
   CS scriptname = alloc(STRLEN(name) + 14);
   STRCPY(scriptname, "autoload/");
   STRCAT(scriptname, name[0] == 'g' && name[1] == ':' ? name + 2: name);
   for (p = scriptname + 9; (p = firstOccurrence(p, AUTOLOAD_CHAR)) != NULL; q = p, ++p)
      *p = '/';
   STRCPY(q, ".vim");
   return scriptname;
}

//If "name" has a package name try autoloading the script for it. true if a package was loaded.
int
scriautoload(CS name, int reload) {      // load script again when already loaded
   CS   scriptname;
   CS tofree;
   int ret = false;
   int i;
   int ret_sid;

   CS p;
   // If the name starts with "<SNR>123_" then "123" is the script ID.
   if (name[0] == K_SPECIAL && name[1] == KS_EXTRA && name[2] == KE_SNR) {
      p = name + 3;
      ret_sid = (int)parseLong(&p);
      if (*p == '_' && SCRIPT_ID_VALID(ret_sid)) {
         may_load_script(ret_sid, &ret);
         return ret;
      }
   }

   // If there is no '#' after name[0] there is no package name.
   p = firstOccurrence(name, AUTOLOAD_CHAR);
   if (!p || p == name)
      return false;

   tofree = scriptname = autoload_name(name);
   if (scriptname == NULL)
      return false;

   // Find the name in the list of previously loaded package names.  Skip
   // "autoload/", it's always the same.
   for (i = 0; i < ga_loaded.len; ++i) {
      if (STRCMP(((Byte **)ga_loaded.c)[i] + 9, scriptname + 9) == 0)
          break;
   }
   if (!reload && i < ga_loaded.len)
      ret = false;       // was loaded already
   else {
      // Remember the name if it wasn't loaded already.
      if (i == ga_loaded.len && ga_grow(&ga_loaded, 1) == OK) {
         ((Byte **)ga_loaded.c)[ga_loaded.len++] = scriptname;
         tofree = NULL;
      }

      // Try loading the package from $EEGLRUNTIME/autoload/<name>.vim
      // Use "ret_sid" to avoid loading the same script again.
      if (source_in_path(runtimePath, scriptname, DIP_START, &ret_sid) == OK)
         ret = true;
   }

   eeglFree(tofree);
   return ret;
}

// Take a type that is using entries in a growarray and turn it into a type with allocated entries.
TypeSpec *
alloc_type(TypeSpec *type) {
   if (!type)
      return null;

   // A fixed type never contains allocated types, return as-is.
   if (type->flags & TTFLAG_STATIC)
      return type;

   TypeSpec* ret = ALLOC_ONE(TypeSpec);
   *ret = *type;

   if (ret->member != NULL)
      ret->member = alloc_type(ret->member);

   if (type->argCount > 0 && type->args != NULL) {
      int i;

      ret->args = ALLOC_MULT(TypeSpec *, type->argCount);
      for (i = 0; i < type->argCount; ++i)
         ret->args[i] = alloc_type(type->args[i]);
   } else
      ret->args = NULL;

   return ret;
}

// Free a type that was created with alloc_type().
void
free_type(TypeSpec *type) {
   int i;

   if (type == NULL || (type->flags & TTFLAG_STATIC))
      return;
   if (type->args != NULL) {
      for (i = 0; i < type->argCount; ++i)
         free_type(type->args[i]);
      eeglFree(type->args);
   }

   free_type(type->member);

   eeglFree(type);
}

//Find the end of a variable or function name. Unlike findNameEnd() this does not recognize magic 
//braces. When "use_namespace" is true recognize "b:", "s:", etc.
//Return a pointer to just after the name.  Equal to "arg" if there is no valid name.
CS
toNameEnd(Byte *arg, int use_namespace) {
   // Quick check for valid starting character.
   if (!isValidForScriptName1(*arg))
      return arg;
   CS p;
   for (p = arg + 1; *p != ZERO && isValidForScriptName(*p); MB_PTR_ADV(p)) {
      // Include a namespace such as "s:var" and "v:var".  But "n:" is not
      // and can be used in slice "[n:]".
      if (*p == ':' && (p != arg + 1 || !use_namespace)) {
          break;
      }
   } 
   return p;
}

//Like toNameEnd() but also skip over a list or dict constant.
//Also accept "<SNR>123_Func". This intentionally does not handle line continuation.
Byte *
to_name_const_end(Byte *arg) {
   Byte   *p = arg;
   Var returnVar;

   if (STRNCMP(p, "<SNR>", 5) == 0)
      p = skipdigits(p + 5);
   else
      p = toNameEnd(p, true);
   if (p == arg && *arg == '[') {
      // Can be "[1, 2, 3]->Func()".
      if (eval_list(&p, &returnVar, NULL, false) == FAIL)
         p = arg;
   }
   return p;
}

// Return the length of an assignment operator, or zero if there isn't one.
int
assignment_len(Byte *p, int *heredoc) {
   if (*p == '=') {
      if (p[1] == '<' && p[2] == '<') {
         *heredoc = true;
         return 3;
      }
      return 1;
   }
   if (firstOccurrence((CS)"+-*/%", *p) != NULL && p[1] == '=')
      return 2;
   if (STRNCMP(p, "..=", 3) == 0)
      return 3;
   return 0;
}


//When a script is a symlink it may be imported with one name and sourced
//under another name.  Adjust the import script ID if needed. "*sid" must be a valid script ID.
void
import_check_sourced_sid(int *sid) {
    ScriptItem *script = SCRIPT_ITEM(*sid);

   if (script->sn_sourced_sid > 0)
   *sid = script->sn_sourced_sid;
}

//Find the script-local variable that links to "dest". If "sid" is zero use the current script.
//if "must_find" is true and "dest" cannot be found report an internal error.
//Return NULL if not found and give an internal error.
Svar *
find_typval_in_script(Var *dest, ScriptId sid, int must_find) {
   ScriptItem    *si = SCRIPT_ITEM(sid == 0 ? scriptPosG.sid : sid);
   int          idx;

   // Find the Svar in sn_var_vals.  Start at the end, in a for loop the
   // variable was added at the end.
   for (idx = si->sn_var_vals.len - 1; idx >= 0; --idx) {
      Svar    *sv = ((Svar *)si->sn_var_vals.c) + idx;

      // If "sv_name" is NULL the variable was hidden when leaving a block,
      // don't check "sv_tv" then, it might be used for another variable now.
      if (sv->sv_name != NULL && sv->sv_tv == dest)
          return sv;
   }
   if (must_find)
      internalErrMsg(S"find_typval_in_script(): not found");
   return NULL;
}
//}}}
//{{{debugger for Vimscript

private int debug_greedy = false;   // batch mode debugging: don't save
               // and restore typeahead.
private void do_setdebugtracelevel(Byte *arg);
private void do_checkbacktracelevel(void);
private void do_showbacktrace(Byte *comm);

private Byte *debug_oldval = NULL;   // old and newval for debug expressions
private Byte *debug_newval = NULL;
private int     debug_expr   = 0;   // use debug_expr

int
has_watchexpr(void) {
   return debug_expr;
}

//do_debug(): Debug mode. Repeatedly get commands, until told to continue normal execution.
void
do_debug(Byte *comm){
   int      save_msg_scroll = msg_scroll;
   int      save_State = stateG;
   int      save_anyEmsgG = anyEmsgG;
   int      save_cmd_silent = cmd_silent;
   int      save_msg_silent = msg_silent;
   int      save_emsg_silent = emsg_silent;
   int      save_redir_off = redir_off;
   TypeaheadSave   typeaheadbuf;
   int      typeahead_saved = false;
   int      save_ignore_script = 0;
   int      save_ex_normal_busy;
   int      n;
   Byte   *cmdline = NULL;
   Byte   *p;
   CS tail = NULL;
   static int   last_cmd = 0;
#define CMD_CONT   1
#define CMD_NEXT   2
#define CMD_STEP   3
#define CMD_FINISH   4
#define CMD_QUIT   5
#define CMD_INTERRUPT   6
#define CMD_BACKTRACE   7
#define CMD_FRAME   8
#define CMD_UP      9
#define CMD_DOWN   10

   // Make sure we are in raw mode and start termcap mode.  Might have side effects...
   termSetMode(TMODE_RAW);
   starttermcap();

   ++isRedrawingDisabledG;   // don't redisplay the window
   ++no_wait_return;      // don't wait for return
   anyEmsgG = false;      // don't use error from debugged stuff
   cmd_silent = false;      // display commands
   msg_silent = false;      // display messages
   emsg_silent = false;   // display error messages
   redir_off = true;      // don't redirect debug commands
   save_timeout_for_debugging();   // disable  regexp timeout flag

   stateG = MODE_NORMAL;
   debug_mode = true;

   if (!debug_did_msg)
      msg(_("Entering Debug mode.  Type \"cont\" to continue."));
   if (debug_oldval != NULL) {
      smsg(_("Oldval = \"%s\""), debug_oldval);
      EE_CLEAR(debug_oldval);
   }
   if (debug_newval != NULL) {
      smsg(_("Newval = \"%s\""), debug_newval);
      EE_CLEAR(debug_newval);
   }
   CS sname = estack_sfile(ESTACK_NONE);
   if (sname)
      msg(sname);
   eeglFree(sname);
   if (SOURCING_LNUM != 0)
      smsg(_("line %ld: %s"), SOURCING_LNUM, comm);
   else
      smsg(_("comm: %s"), comm);

   // Repeat getting a command and executing it.
   for (;;) {
      msg_scroll = true;
      need_wait_return = false;

      // Save the current typeahead buffer and replace it with an empty one. This makes sure we 
      // get input from the user here and don't interfere with the commands being executed. 
      // Reset "ex_normal_busy" to avoid the side effects of using ":normal". Save the stuff 
      // buffer and make it empty. Set ignore_script to avoid reading from script input.
      save_ex_normal_busy = ex_normal_busy;
      ex_normal_busy = 0;
      if (!debug_greedy) {
         save_typeahead(&typeaheadbuf);
         typeahead_saved = true;
         save_ignore_script = ignore_script;
         ignore_script = true;
      }

      // don't debug any function call, e.g. from an expression mapping
      n = debug_break_level;
      debug_break_level = -1;

      eeglFree(cmdline);
      cmdline = getcmdline_prompt('>', NULL, 0, EXPAND_NOTHING, NULL);

      debug_break_level = n;
      if (typeahead_saved) {
         restore_typeahead(&typeaheadbuf, true);
         ignore_script = save_ignore_script;
      }
      ex_normal_busy = save_ex_normal_busy;

      commlineRowG = msgRowG;
      msg_starthere();
      if (cmdline != NULL) {
         // If this is a debug command, set "last_cmd".
         // If not, reset "last_cmd". For a blank line use previous command.
         p = skipwhite(cmdline);
         if (*p != ZERO) {
            switch (*p) {
            case 'c': last_cmd = CMD_CONT;
               tail = S"ont";
               break;
            case 'n': last_cmd = CMD_NEXT;
               tail = S"ext";
               break;
            case 's': last_cmd = CMD_STEP;
               tail = S"tep";
               break;
            case 'f':
               last_cmd = 0;
               if (p[1] == 'r') {
                  last_cmd = CMD_FRAME;
                  tail = S"rame";
               } else {
                  last_cmd = CMD_FINISH;
                  tail = S"inish";
               }
               break;
            case 'q': 
               last_cmd = CMD_QUIT;
               tail = S"uit";
               break;
            case 'i': 
               last_cmd = CMD_INTERRUPT;
               tail = S"nterrupt";
               break;
            case 'b': 
               last_cmd = CMD_BACKTRACE;
               if (p[1] == 't')
                  tail = S"t";
               else
                  tail = S"acktrace";
               break;
            case 'w': 
               last_cmd = CMD_BACKTRACE;
               tail = S"here";
               break;
            case 'u': 
               last_cmd = CMD_UP;
               tail = S"p";
               break;
            case 'd': 
               last_cmd = CMD_DOWN;
               tail = S"own";
               break;
            default: 
               last_cmd = 0;
            }
            if (last_cmd != 0) {
               // Check that the tail matches.
               ++p;
               while (*p != ZERO && *p == *tail) {
                 ++p;
                 ++tail;
               }
               if (ASCII_ISALPHA(*p) && last_cmd != CMD_FRAME)
                  last_cmd = 0;
            }
         }

         if (last_cmd != 0) {
            // Execute debug command: decide where to break next and return.
            switch (last_cmd) {
            case CMD_CONT:
               debug_break_level = -1;
               break;
            case CMD_NEXT:
               debug_break_level = ex_nesting_level;
               break;
            case CMD_STEP:
               debug_break_level = 9999;
               break;
            case CMD_FINISH:
               debug_break_level = ex_nesting_level - 1;
               break;
            case CMD_QUIT:
               gotInterruptG = true;
               debug_break_level = -1;
               break;
            case CMD_INTERRUPT:
               gotInterruptG = true;
               debug_break_level = 9999;
               // Do not repeat ">interrupt" comm, continue stepping.
               last_cmd = CMD_STEP;
               break;
            case CMD_BACKTRACE:
               do_showbacktrace(comm);
               continue;
            case CMD_FRAME:
              if (*p == ZERO) {
                  do_showbacktrace(comm);
              } else {
                  p = skipwhite(p);
                  do_setdebugtracelevel(p);
              }
              continue;
            case CMD_UP:
               debug_backtrace_level++;
               do_checkbacktracelevel();
               continue;
            case CMD_DOWN:
               debug_backtrace_level--;
               do_checkbacktracelevel();
               continue;
            }
            // Going out reset backtrace_level
            debug_backtrace_level = 0;
            break;
         }

         // don't debug this command
         n = debug_break_level;
         debug_break_level = -1;
         (void)doCommand(cmdline, getexline, NULL, DOCMD_VERBOSE|DOCMD_EXCRESET);
         debug_break_level = n;
      }
      lines_left = visibleRowsG - 1;
   }
   eeglFree(cmdline);

   if (isRedrawingDisabledG > 0)
      --isRedrawingDisabledG;
   --no_wait_return;
   redraw_all_later(UPD_NOT_VALID);
   need_wait_return = false;
   msg_scroll = save_msg_scroll;
   restore_timeout_for_debugging();
   lines_left = visibleRowsG - 1;
   stateG = save_State;
   debug_mode = false;
   anyEmsgG = save_anyEmsgG;
   cmd_silent = save_cmd_silent;
   msg_silent = save_msg_silent;
   emsg_silent = save_emsg_silent;
   redir_off = save_redir_off;

   // Only print the message again when typing a command before coming back here.
   debug_did_msg = true;
}

private int
get_maxbacktrace_level(CS sname) {
   if (!sname)
      return 0;

   int      maxbacktrace = 0;
   CS p = sname;
   CS q;
   while ((q = STRSTR(p, "..")) != NULL) {
      p = q + 2;
      maxbacktrace++;
   }
   return maxbacktrace;
}

private void
do_setdebugtracelevel(Byte *arg) {
   int level = atoi((char *)arg);
   if (*arg == '+' || level < 0)
      debug_backtrace_level += level;
   else
      debug_backtrace_level = level;

   do_checkbacktracelevel();
}

private void
do_checkbacktracelevel(void) {
   if (debug_backtrace_level < 0) {
      debug_backtrace_level = 0;
      msg(_("frame is zero"));
   } else {
      Byte   *sname = estack_sfile(ESTACK_NONE);
      int   max = get_maxbacktrace_level(sname);

      if (debug_backtrace_level > max) {
         debug_backtrace_level = max;
         smsg(_("frame at highest level: %d"), max);
      }
      eeglFree(sname);
   }
}

private void
do_showbacktrace(CS comm) {
   CS sname;
   CS cur;
   CS next;
   int       i = 0;
   int       max;

   sname = estack_sfile(ESTACK_NONE);
   max = get_maxbacktrace_level(sname);
   if (sname) {
      cur = sname;
      while (!gotInterruptG) {
         next = STRSTR(cur, "..");
         if (next != NULL)
            *next = ZERO;
         if (i == max - debug_backtrace_level)
            smsg("->%d %s", max - i, cur);
         else
            smsg("  %d %s", max - i, cur);
         ++i;
         if (next == NULL)
            break;
         *next = '.';
         cur = next + 2;
      }
      eeglFree(sname);
   }

   if (SOURCING_LNUM != 0)
      smsg(_("line %ld: %s"), (long)SOURCING_LNUM, comm);
   else
      smsg(_("comm: %s"), comm);
}

//":debug".
void
c_debug(Invocation* invo) {
   int      debug_break_level_save = debug_break_level;

   debug_break_level = 9999;
   executeCommLine(invo->arg);
   debug_break_level = debug_break_level_save;
}

private Byte   *debug_breakpoint_name = NULL;
private LineNr   debug_breakpoint_lnum;

//When debugging or a breakpoint is set on a skipped command, no debug prompt
//is shown by do_one_cmd().  This situation is indicated by debug_skipped, and
//debug_skipped_name is then set to the source name in the breakpoint case.  If
//a skipped command decides itself that a debug prompt should be displayed, it
//can do so by calling dbg_check_skipped().
private int   debug_skipped;
private Byte   *debug_skipped_name;

//Go to debug mode when a breakpoint was encountered or "ex_nesting_level" is
//at or below the break level. But only when the line is actually
//executed. Return true and set breakpoint_name for skipped commands that
//decide to execute something themselves. Called from do_one_cmd() before executing a command.
void
dbg_check_breakpoint(Invocation* invo) {
   Byte   *p;

   debug_skipped = false;
   if (debug_breakpoint_name != NULL) {
      if (!invo->skip) {
         // replace K_SNR with "<SNR>"
         if (debug_breakpoint_name[0] == K_SPECIAL
             && debug_breakpoint_name[1] == KS_EXTRA
             && debug_breakpoint_name[2] == KE_SNR)
         p = (CS)"<SNR>";
         else
            p = (CS)"";
         smsg(_("Breakpoint in \"%s%s\" line %ld"),
             p,
             debug_breakpoint_name + (*p == ZERO ? 0 : 3),
             (long)debug_breakpoint_lnum
         );
         debug_breakpoint_name = NULL;
         do_debug(invo->comm);
      } else {
          debug_skipped = true;
          debug_skipped_name = debug_breakpoint_name;
          debug_breakpoint_name = NULL;
      }
   } ei (ex_nesting_level <= debug_break_level) {
      if (!invo->skip)
          do_debug(invo->comm);
      else {
          debug_skipped = true;
          debug_skipped_name = NULL;
      }
   }
}

//Go to debug mode if skipped by dbg_check_breakpoint() because invo->skip was
//set. Return true when the debug mode is entered this time.
int
dbg_check_skipped(Invocation* invo) {
   int      prev_gotInterruptG;

   if (!debug_skipped)
      return false;

    // Save the value of gotInterruptG and reset it.  We don't want a previous
    // interruption cause flushing the input buffer.
    prev_gotInterruptG = gotInterruptG;
    gotInterruptG = false;
    debug_breakpoint_name = debug_skipped_name;
    // invo->skip is true
    invo->skip = false;
    (void)dbg_check_breakpoint(invo);
    invo->skip = true;
    gotInterruptG |= prev_gotInterruptG;
    return true;
}

//The list of breakpoints: dbg_breakp. This is an arraylist of structs.
typedef struct {
   int dbg_nr;      // breakpoint number
   int dbg_type;   // DBG_FUNC, DBG_FILE or DBG_EXPR
   CS dbg_name;   // function, expression or file name
   RegProg* dbg_prog;   // regexp program
   LineNr dbg_lnum;   // line number in function or file
   int dbg_forceit;   // ! used
   Var* dbg_val;       // last result of watchexpression
   int dbg_level;      // stored nested level for expr
} Debuggy;

private ArrayList dbg_breakp = {0, 0, sizeof(Debuggy), 4, NULL};
#define BREAKP(idx)      (((Debuggy *)dbg_breakp.c)[idx])
#define DEBUGGY(gap, idx)   (((Debuggy *)gap->c)[idx])
private int last_breakp = 0;   // nr of last defined breakpoint
private int has_expr_breakpoint = false;

#define PROF_CLEAR_CACHE(gap) do {} while (0)
#define DBG_FUNC   1
#define DBG_FILE   2
#define DBG_EXPR   3

private LineNr debuggy_find(int file,Byte *fname, LineNr after, ArrayList *gap, int *fp);

//Evaluate the "bp->dbg_name" expression and return the result. Disable error messages.
private Var *
eval_expr_no_emsg(Debuggy *bp) {
   // Disable error messages, a bad expression would make Eegl unusable.
   ++emsg_off;
   Var* tv = eval_expr(bp->dbg_name, NULL);
   --emsg_off;

   return tv;
}

//Parse the arguments of ":profile", ":breakadd" or ":breakdel" and put them
//in the entry just after the last one in dbg_breakp.  Note that "dbg_name" is allocated.
//Return FAIL for failure.
private int
dbg_parsearg(CS arg, ArrayList* gap){ // either &dbg_breakp or &prof_ga
   Byte   *p = arg;
   Byte   *q;
   Debuggy *bp;
   int      here = false;

   if (ga_grow(gap, 1) == FAIL)
      return FAIL;
   bp = &DEBUGGY(gap, gap->len);

   // Find "func" or "file".
   if (STRNCMP(p, "func", 4) == 0)
      bp->dbg_type = DBG_FUNC;
   ei (STRNCMP(p, "file", 4) == 0)
      bp->dbg_type = DBG_FILE;
   ei ( STRNCMP(p, "here", 4) == 0) {
      if (curBook->fullFileName == NULL) {
         emsg(_(e_no_file_name));
         return FAIL;
      }
      bp->dbg_type = DBG_FILE;
      here = true;
   } ei ( STRNCMP(p, "expr", 4) == 0)
      bp->dbg_type = DBG_EXPR;
   else {
      showErrFmtMsg(_(e_invalid_argument_str), p);
      return FAIL;
   }
   p = skipwhite(p + 4);

   // Find optional line number.
   if (here)
      bp->dbg_lnum = curPor->cursor.lnum;
   ei ( EE_ISDIGIT(*p)) {
      bp->dbg_lnum = parseLong(&p);
      p = skipwhite(p);
   } else
      bp->dbg_lnum = 0;

   // Find the function or file name.  Don't accept a function name with ().
   if ((!here && *p == ZERO)
       || (here && *p != ZERO)
       || (bp->dbg_type == DBG_FUNC && strstr((char *)p, "()") != NULL)
   ) {
      showErrFmtMsg(_(e_invalid_argument_str), arg);
      return FAIL;
   }

   if (bp->dbg_type == DBG_FUNC)
      bp->dbg_name = copyStr(STRNCMP(p, "g:", 2) == 0 ? p + 2 : p);
   ei (here)
      bp->dbg_name = copyStr(curBook->fullFileName);
   ei (bp->dbg_type == DBG_EXPR) {
      bp->dbg_name = copyStr(p);
      if (bp->dbg_name != NULL)
         bp->dbg_val = eval_expr_no_emsg(bp);
   } else {
      // Expand the file name in the same way as scriptRunFile().  This means
      // doing it twice, so that $DIR/file gets expanded when $DIR is "~/dir".
      q = doExpandEnvInMultiplePaths(p);
      if (!q)
          return FAIL;
      p = doExpandEnvInMultiplePaths(q);
      eeglFree(q);
      if (p == NULL)
         return FAIL;
      if (*p != '*') {
         bp->dbg_name = fiExpandAndCopy(p, true);
         eeglFree(p);
      } else
         bp->dbg_name = p;
   }

   if (bp->dbg_name == NULL)
      return FAIL;
   return OK;
}

//":breakadd".  Also used for ":profile".
void
c_breakadd(Invocation* invo) {
   Debuggy *bp;

   ArrayList* gap = &dbg_breakp;

   if (dbg_parsearg(invo->arg, gap) != OK)
      return;

    bp = &DEBUGGY(gap, gap->len);
    bp->dbg_forceit = invo->forceit;

   if (bp->dbg_type != DBG_EXPR) {
      CS pat = file_pat_to_reg_pat(bp->dbg_name, NULL, NULL);
      bp->dbg_prog = compileRegexp(pat, RE_MAGIC + RE_STRING);
      eeglFree(pat);
      if (pat == NULL || bp->dbg_prog == NULL)
         eeglFree(bp->dbg_name);
      else {
         if (bp->dbg_lnum == 0)   // default line number is 1
            bp->dbg_lnum = 1;
         DEBUGGY(gap, gap->len).dbg_nr = ++last_breakp;
         ++debug_tick;
         ++gap->len;
         PROF_CLEAR_CACHE(gap);
      }
   } else {
      // DBG_EXPR
      DEBUGGY(gap, gap->len++).dbg_nr = ++last_breakp;
      ++debug_tick;
      if (gap == &dbg_breakp)
          has_expr_breakpoint = true;
   }
}

//":debuggreedy".
void
c_debuggreedy(Invocation* invo) {
   if (invo->addr_count == 0 || invo->line2 != 0)
      debug_greedy = true;
   else
      debug_greedy = false;
}

private void
update_has_expr_breakpoint(void) {
   has_expr_breakpoint = false;
   for (int i = 0; i < dbg_breakp.len; ++i) {
      if (BREAKP(i).dbg_type == DBG_EXPR) {
          has_expr_breakpoint = true;
          break;
      }
   } 
}

//true if there is any expression breakpoint.
int
debug_has_expr_breakpoint(void) {
   return has_expr_breakpoint;
}

// ":breakdel" and ":profdel".
void
c_breakdel(Invocation* invo) {
   Debuggy *bp, *bpi;
   int      nr;
   int      todel = -1;
   int      del_all = false;
   int      i;
   LineNr   best_lnum = 0;

   ArrayList* gap = &dbg_breakp;

   if (eeIsDigit(*invo->arg)) {
      // ":breakdel {nr}"
      nr = atol((char *)invo->arg);
      for (i = 0; i < gap->len; ++i) {
         if (DEBUGGY(gap, i).dbg_nr == nr) {
            todel = i;
            break;
         }
      } 
   } ei (*invo->arg == '*') {
      todel = 0;
      del_all = true;
   } else {
      // ":breakdel {func|file|expr} [lnum] {name}"
      if (dbg_parsearg(invo->arg, gap) == FAIL)
         return;
      bp = &DEBUGGY(gap, gap->len);
      for (i = 0; i < gap->len; ++i) {
         bpi = &DEBUGGY(gap, i);
         if (bp->dbg_type == bpi->dbg_type && STRCMP(bp->dbg_name, bpi->dbg_name) == 0
                && (bp->dbg_lnum == bpi->dbg_lnum 
                   || (bp->dbg_lnum == 0 && (best_lnum == 0 || bpi->dbg_lnum < best_lnum)))
         ) {
            todel = i;
            best_lnum = bpi->dbg_lnum;
         }
      }
      eeglFree(bp->dbg_name);
   }

   if (todel < 0) {
      showErrFmtMsg(_(e_breakpoint_not_found_str), invo->arg);
      return;
   }

   while (gap->len > 0) {
      eeglFree(DEBUGGY(gap, todel).dbg_name);
      if (DEBUGGY(gap, todel).dbg_type == DBG_EXPR && DEBUGGY(gap, todel).dbg_val != NULL)
         freeVar(DEBUGGY(gap, todel).dbg_val);
      eeRegFree(DEBUGGY(gap, todel).dbg_prog);
      --gap->len;
      if (todel < gap->len)
          MEMMOVE(
             &DEBUGGY(gap, todel), &DEBUGGY(gap, todel + 1), 
             (gap->len - todel) * sizeof(Debuggy)
         );
       ++debug_tick;
      if (!del_all)
          break;
    }
    PROF_CLEAR_CACHE(gap);

    // If all breakpoints were removed clear the array.
   if (gap->len == 0)
   ga_clear(gap);
   if (gap == &dbg_breakp)
   update_has_expr_breakpoint();
}

// ":breaklist".
void
c_breaklist(Invocation* invo UNUSED) {
   Debuggy *bp;
   if (dbg_breakp.len == 0) {
      msg(_("No breakpoints defined"));
      return;
   }

   for (int i = 0; i < dbg_breakp.len; ++i) {
      bp = &BREAKP(i);
      if (bp->dbg_type == DBG_FILE)
         home_replace(bp->dbg_name, nameBuffG, MAXPATHL, true);
      if (bp->dbg_type != DBG_EXPR)
         smsg(
            _("%3d  %s %s  line %ld"),
            bp->dbg_nr,
            bp->dbg_type == DBG_FUNC ? "func" : "file",
            bp->dbg_type == DBG_FUNC ? bp->dbg_name : nameBuffG,
            (long)bp->dbg_lnum
         );
      else
          smsg(_("%3d  expr %s"),
             bp->dbg_nr, bp->dbg_name);
   }
}

// Find a breakpoint for a function or sourced file.
// Return line number at which to break; zero when no matching breakpoint.
LineNr
dbg_find_breakpoint(
   int      file,       // true for a file, false for a function
   Byte   *fname,       // file or function name
   LineNr   after       // after this line number
){
   return debuggy_find(file, fname, after, &dbg_breakp, NULL);
}

// Common code for dbg_find_breakpoint() and has_profiling().
private LineNr
debuggy_find(
   int      is_file,    // true for a file, false for a function
   Byte   *fname,       // file or function name
   LineNr   after,       // after this line number
   ArrayList   *gap,       // either &dbg_breakp or &prof_ga
   int      *fp)       // if not NULL: return forceit
{
   Debuggy *bp;
   LineNr   lnum = 0;
   CS name = NULL;
   CS short_name = fname;
   int prev_gotInterruptG;

   // Return quickly when there are no breakpoints.
   if (gap->len == 0)
      return (LineNr)0;

   // For a script-local function remove the prefix, so that "profile func Func" matches "Func" in 
   // any script.  Otherwise it's very difficult to profile/debug a script-local function.  It may 
   // match a function in the wrong script, but that is much better than not being able to 
   // profile/debug a function in a script with unknown ID. Also match a script-specific name.
   if (!is_file && fname[0] == K_SPECIAL) {
      short_name = firstOccurrence(fname, '_') + 1;
      name = alloc(STRLEN(fname) + 3);
      STRCPY(name, "<SNR>");
      STRCPY(name + 5, fname + 3);
   }

   for (int i = 0; i < gap->len; ++i) {
      // Skip entries that are not useful or are for a line that is beyond
      // an already found breakpoint.
      bp = &DEBUGGY(gap, i);
      if (((bp->dbg_type == DBG_FILE) == is_file
             && bp->dbg_type != DBG_EXPR && (
         (bp->dbg_lnum > after && (lnum == 0 || bp->dbg_lnum < lnum)))))
      {
          // Save the value of gotInterruptG and reset it.  We don't want a
          // previous interruption cancel matching, only hitting CTRL-C
          // while matching should abort it.
          prev_gotInterruptG = gotInterruptG;
          gotInterruptG = false;
          if ((name != NULL
            && eeRegexec_prog(&bp->dbg_prog, false, name, (ColNr)0))
             || eeRegexec_prog(&bp->dbg_prog, false, short_name, (ColNr)0))
          {
         lnum = bp->dbg_lnum;
         if (fp != NULL)
             *fp = bp->dbg_forceit;
          }
          gotInterruptG |= prev_gotInterruptG;
      } ei (bp->dbg_type == DBG_EXPR) {
         int         line = false;

         Var* tv = eval_expr_no_emsg(bp);
         if (tv) {
            if (bp->dbg_val == NULL) {
                debug_oldval = typval_tostring(NULL, true);
                bp->dbg_val = tv;
                debug_newval = typval_tostring(bp->dbg_val, true);
                line = true;
            } else {
               // Use "==" instead of "is" for strings, that is what we always have done.
               ExprType type = tv->tag == VAR_STRING ? EXPR_EQUAL : EXPR_IS;

               if (typval_compare(tv, bp->dbg_val, type, false) == OK && tv->number == false) {
                  Var *v;

                  line = true;
                  debug_oldval = typval_tostring(bp->dbg_val, true);
                  // Need to evaluate again, typval_compare() overwrites "tv".
                  v = eval_expr_no_emsg(bp);
                  debug_newval = typval_tostring(v, true);
                  freeVar(bp->dbg_val);
                  bp->dbg_val = v;
               }
               freeVar(tv);
            }
         } ei (bp->dbg_val != NULL) {
            debug_oldval = typval_tostring(bp->dbg_val, true);
            debug_newval = typval_tostring(NULL, true);
            freeVar(bp->dbg_val);
            bp->dbg_val = NULL;
            line = true;
         }

         if (line) {
            lnum = after > 0 ? after : 1;
            break;
         }
      }
   }
   if (name != fname)
      eeglFree(name);

   return lnum;
}

// Called when a breakpoint was encountered.
void
dbg_breakpoint(Byte *name, LineNr lnum) {
   // We need to check if this line is actually executed in do_one_cmd()
   debug_breakpoint_name = name;
   debug_breakpoint_lnum = lnum;
}

//}}}
//{{{completions

private int   cmd_showtail;   // Only show path tail in lists ?
private int   may_expand_pattern = false;
private Pos   pre_incsearch_pos; // Cursor position when incsearch started

private void   set_context_for_wildcard_arg(
      Invocation* invo, CS arg, int usefilter, Expand *xp, OUT Unt *context
);
private CS showmatches_gettail(CS s);
private int expand_showtail(Expand *xp);
private int expandShellCommand(CS filepat, Unt flags, OUT ExpandMatch* matches);
private int expandUserDefined(
      CS pat, Expand *xp, RegMatch *regmatch, OUT ExpandMatch* matches
);
private int   expandUserList(Expand *xp, OUT ExpandMatch* matches);
private int   expandPatternInBook(CS pat, Unt dir, OUT ExpandMatch* matches);

// Currently displayed list of entries in the popup menu. NULL when there is no popup menu
private PopupItem *popupItemsS = NULL;
private int popupItemsSsize;
// First column in commline of the matched item for completion.
private int compl_startcol;
private int compl_selected;
// commline before expansion
private Byte *commlineSaved = NULL;

#define SHOW_MATCH(m) (showtail ? showmatches_gettail(matches->c[m]) : matches->c[m])


// Return true if fuzzy completion is supported for a given commline completion context.
private int
commlineFuzzyCompletionSupported(Expand *xp) {
   switch (xp->context) {
   case EXPAND_COLORS:
   case EXPAND_COMPILER:
   case EXPAND_DIRECTORIES:
   case EXPAND_DIRS_IN_CDPATH:
   case EXPAND_FILES:
   case EXPAND_FILES_IN_PATH:
   case EXPAND_FILETYPE:
   case EXPAND_FILETYPECMD:
   case EXPAND_FINDFUNC:
   case EXPAND_HELP:
   case EXPAND_KEYMAP:
   case EXPAND_OLD_OPTION:
   case EXPAND_STRING_OPTION:
   case EXPAND_OWNSYNTAX:
   case EXPAND_PACKADD:
   case EXPAND_RUNTIME:
   case EXPAND_SHELLCMD:
   case EXPAND_SHELLCMDLINE:
   case EXPAND_TAGS:
   case EXPAND_TAGS_LISTFILES:
   case EXPAND_USER_LIST:
      return false;
   default:
      break;
   }

   return (p_wop & WILDOPT_FUZZY) != 0;
}

//Return true if fuzzy completion for commline completion is enabled and 'fuzzystr' is not empty.
//If search pattern is empty, then don't use fuzzy matching.
Boole
scrIsCommlineFuzzyCompletable(CS fuzzystr) {
   return (p_wop & WILDOPT_FUZZY) != 0 && *fuzzystr != ZERO;
}

// sort function for the completion matches. <SNR> functions should be sorted to the end.
private int
sort_func_compare(const void *s1, const void *s2) {
   Byte *p1 = *(Byte **)s1;
   Byte *p2 = *(Byte **)s2;

   if (*p1 != '<' && *p2 == '<')
      return -1;
   if (*p1 == '<' && *p2 != '<')
      return 1;
   return STRCMP(p1, p2);
}

// Escape special characters in the commline completion matches.
private void
wildescape(Expand* xp, CS str, OUT ExpandMatch* files) {
   Byte   *p;
   Unt vse_what = xp->context == EXPAND_BUFFERS ? VSE_BOOK : VSE_NONE;

   if (xp->context == EXPAND_FILES
       || xp->context == EXPAND_FILES_IN_PATH
       || xp->context == EXPAND_SHELLCMD
       || xp->context == EXPAND_BUFFERS
       || xp->context == EXPAND_DIRECTORIES
       || xp->context == EXPAND_DIRS_IN_CDPATH
   ) {
      //Insert a backslash into a file name before a space, \, %, #
      //and wildmatch characters, except '~'.
      for (Unt i = 0; i < files->len; ++i) {
         //for ":set path=" we need to escape spaces twice
         if (xp->backslash & XP_BS_THREE) {
            CS pat = (xp->backslash & XP_BS_COMMA) ?  S" ," : S" ";
            p = copyStr_escaped(files->c[i], pat);
         } ei (xp->backslash & XP_BS_COMMA) {
            if (firstOccurrence(files->c[i], ',') != NULL) {
               p = copyStr_escaped(files->c[i], (CS)",");
               eeglFree(files->c[i]);
               files->c[i] = p;
            }
         }
         p = copyStr_fnameescape(files->c[i], xp->isShell ? VSE_SHELL : vse_what);
         eeglFree(files->c[i]);
         files->c[i] = p;

         // If 'str' starts with "\~", replace "~" at start of files[i] with "\~".
         if (str[0] == '\\' && str[1] == '~' && files->c[i][0] == '~')
            escape_fname(&files->c[i]);
      }
      xp->backslash = XP_BS_NONE;

      // If the first file starts with a '+' escape it.  Otherwise it could be seen as "+comm".
      if (*files->c[0] == '+')
         escape_fname(&files->c[0]);
   } ei (xp->context == EXPAND_TAGS) {
      // Insert a backslash before characters in a tag name that would terminate the ":tag" command
      for (Unt i = 0; i < files->len; ++i) {
         p = copyStr_escaped(files->c[i], S"\\|\"");
         eeglFree(files->c[i]);
         files->c[i] = p;
      }
   }
}

// Escape special characters in the commline completion matches.
void
expandEscape(
   OUT Expand* xp,
   CS str,
   int options,
   OUT ExpandMatch* files
){
  // May change home directory back to "~"
  if (options & WILD_HOME_REPLACE)
     tilde_replace(str, OUT files);

  if (options & WILD_ESCAPE)
     wildescape(xp, str, OUT files);
}

//Return FAIL if this is not an appropriate context in which to do
//completion of anything, return OK if it is (even if there are no matches).
//For the caller, this means that the character is just passed through like a
//normal character (instead of being expanded).  This allows :s/^I^D etc.
private int
nextwild(
   OUT Expand* xp,
   int type,
   int options,   // extra options for expandWildcard()
   int escape      // if true, escape the returned matches
){
   CommlineInfo* ccline = getCommlineInfo();
   CS p;
   int from_wildtrigger_func = options & WILD_FUNC_TRIGGER;

   if (xp->files.len == UNT) {
      pre_incsearch_pos = xp->xp_pre_incsearch_pos;
      if (ccline->input_fn && ccline->context == EXPAND_COMMANDS) {
         // Expand commands typed in input() function
         setCompletionContextForCommand(
               OUT xp, (Text){ccline->commBuf, ccline->cmdlen}, ccline->cmdpos, false
         );
      } else {
          may_expand_pattern = options & WILD_MAY_EXPAND_PATTERN;
          set_expand_context(xp);
          may_expand_pattern = false;
      }
      cmd_showtail = expand_showtail(xp);
   }

   if (xp->context == EXPAND_UNSUCCESSFUL) {
      beep_flush();
      return OK;  // Something illegal on command line
   }
   if (xp->context == EXPAND_NOTHING) {
      // Caller can use the character as a normal char instead
      return FAIL;
   }

   int i = (int)(xp->input.c - ccline->commBuf);
   xp->input.len = ccline->cmdpos - i;

   // Skip showing matches if prefix is invalid during wildtrigger()
   if (from_wildtrigger_func && xp->context == EXPAND_COMMANDS && xp->input.len == 0)
      return FAIL;

   // If cmd_silent is set then don't show the dots, because redrawcmd() below won't remove them.
   if (!cmd_silent && !from_wildtrigger_func) {
      msg_puts(S"...");       // show that we are busy
      out_flush();
   }

   if (type == WILD_NEXT || type == WILD_PREV || type == WILD_PAGEUP || type == WILD_PAGEDOWN) {
      // Get next/previous match for a previous expanded pattern.
      p = expandWildcard(OUT xp, NULL, NULL, 0, type);
   } else {
      Byte   *tmp;

      if (commlineFuzzyCompletionSupported(xp)
         || xp->context == EXPAND_PATTERN_IN_BUF)
          // Don't modify the search string
          tmp = copySubstr(xp->input.c, xp->input.len);
      else
          tmp = addstar(xp->input, xp->context);

      // Translate string into pattern and expand it.
      int use_options = options | WILD_HOME_REPLACE|WILD_ADD_SLASH|WILD_SILENT;
      if (use_options & WILD_KEEP_SOLE_ITEM)
         use_options &= ~WILD_KEEP_SOLE_ITEM;
      if (escape)
         use_options |= WILD_ESCAPE;
      if (p_wic)
         use_options += WILD_ICASE;

      p = expandWildcard(
          OUT xp, tmp, copySubstr(&ccline->commBuf[i], xp->input.len), use_options, type
      );
      eeglFree(tmp);
      // longest match: make sure it is not shorter, happens with :help
      if (p != NULL && type == WILD_LONGEST) {
         Unt j;
         for (j = 0; j < xp->input.len; ++j) {
            Byte  c = ccline->commBuf[i + j];
            if (c == '*' || c == '?')
               break;
         }
         if (STRLEN(p) < j)
            EE_CLEAR(p);
       }
   }

   if (p && !gotInterruptG) {
      Unt   plen = STRLEN(p);
      int   v = OK;

      int difflen = (int)plen - xp->input.len;
      if (ccline->cmdlen + difflen + 4 > ccline->cmdbufflen) {
         v = reallocateCommBuf(ccline->cmdlen + difflen + 4);
         xp->input.c = ccline->commBuf + i;
      }

      if (v == OK) {
         MEMMOVE(&ccline->commBuf[ccline->cmdpos + difflen],
             &ccline->commBuf[ccline->cmdpos],
             (Unt)(ccline->cmdlen - ccline->cmdpos + 1)
         );
         MEMMOVE(&ccline->commBuf[i], p, plen);
         ccline->cmdlen += difflen;
         ccline->cmdpos += difflen;
      }
   }

   redrawcmd();
   cursorcmd();

   // When expanding a ":map" command and no matches are found, assume that
   // the key is supposed to be inserted literally
   if (xp->context == EXPAND_MAPPINGS && p == NULL)
      return FAIL;

   if (xp->files.len == UNT && !p)
      beep_flush();
   ei (xp->files.len == 1 && !(options & WILD_KEEP_SOLE_ITEM))
      // free expanded pattern
      (void)expandWildcard(OUT xp, NULL, NULL, 0, WILD_FREE);

   eeglFree(p);

   return OK;
}

// Create and display a commline completion popup menu with items from 'matches'.
private int
createCommlinePum(
   CommlineInfo* ccline,
   Expand* xp,
   int showtail,
   OUT ExpandMatch* matches
) {
   // Add all the completion matches
   popupItemsS = ALLOC_MULT(PopupItem, matches->len);

   popupItemsSsize = matches->len;
   for (Unt i = 0; i < matches->len; i++) {
      popupItemsS[i].pum_text = SHOW_MATCH(i);
      popupItemsS[i].pum_info = NULL;
      popupItemsS[i].pum_extra = NULL;
      popupItemsS[i].pum_kind = NULL;
      popupItemsS[i].abbreviationDeco = EMPTY_DECO;
      popupItemsS[i].kindDeco = EMPTY_DECO;
   }

   // Compute the popup menu starting column
   compl_startcol = ccline == NULL ? 0 : eeglStrSize(ccline->commBuf) + 1;
   int prefix_len = xp->input.len;
   if (showtail)
      prefix_len += eeglStrSize(showmatches_gettail(matches->c[0])) - eeglStrSize(matches->c[0]);
   compl_startcol = MAX(0, compl_startcol - prefix_len);

   // no default selection
   compl_selected = -1;

   pum_clear();
   cmdline_pum_display();

   return EXPAND_OK;
}

// Display the commline completion matches in a popup menu
void
cmdline_pum_display(void){
    pum_display(popupItemsS, popupItemsSsize, compl_selected);
}

// Return true if the cmdline completion popup menu is being displayed.
int
cmdline_pum_active(void){
   return pum_visible() && popupItemsS != NULL;
}

// Remove the commline completion popup menu (if present), free the list of items and refresh screen
void
cmdline_pum_remove(CommlineInfo *cclp UNUSED, int defer_redraw){
   int   save_keyWasTypedG = keyWasTypedG;
   int   save_isRedrawingDisabledG = isRedrawingDisabledG;
   if (cclp->input_fn)
      isRedrawingDisabledG = 0;

   pum_undisplay();
   EE_CLEAR(popupItemsS);
   popupItemsSsize = 0;
   if (!defer_redraw) {
      int save_p_lz = p_lz;
      p_lz = false;  // avoid the popup menu hanging around
      drawUpdateScreen(0);
      p_lz = save_p_lz;
   } else
      pum_callUpdateScreen();
   redrawcmd();

   // When a function is called (e.g. for 'foldtext') keyWasTypedG might be reset as a side effect.
   keyWasTypedG = save_keyWasTypedG;
   if (cclp->input_fn)
      isRedrawingDisabledG = save_isRedrawingDisabledG;
}

void
cmdline_pum_cleanup(CommlineInfo *cclp){
   cmdline_pum_remove(cclp, false);
   wildmenu_cleanup(cclp);
}

// Return the starting column number to use for the cmdline completion popup menu.
int
cmdline_compl_startcol(void){
   return compl_startcol;
}

// Return the current cmdline completion pattern.
CS
cmdline_compl_pattern(void){
   Expand* xp = getCommlineInfo()->xpc;
   return xp == NULL ? NULL : xp->orig;
}

// true if fuzzy cmdline completion is active, false otherwise.
int
cmdline_compl_is_fuzzy(void){
   Expand* xp = getCommlineInfo()->xpc;
   return xp != NULL && commlineFuzzyCompletionSupported(xp);
}

//Return the number of characters that should be skipped in a status match.
//These are backslashes used for escaping.  Do show backslashes in help tags
//and in search pattern completion matches.
private int
skip_status_match_char(Expand *xp, CS s) {
   if ((rem_backslash(s) && xp->context != EXPAND_HELP
      && xp->context != EXPAND_PATTERN_IN_BUF)
       || ((xp->context == EXPAND_MENUS
          || xp->context == EXPAND_MENUNAMES)
           && (s[0] == '\t' || (s[0] == '\\' && s[1] != ZERO)))
   ){
      return 1;
   }
   return 0;
}

// Get the length of an item as it will be shown in the status line.
private int
status_match_len(Expand *xp, Byte *s) {
   int   len = 0;
   while (*s != ZERO) {
      s += skip_status_match_char(xp, s);
      len += bookPtr2Cells(s);
      MB_PTR_ADV(s);
   }

   return len;
}

//Show wildchar matches in the status line.
//Show at least the "match" item.
//We start at item 'firstMatch' in the list and show all matches that fit.
//
//If inversion is possible we use it. Else '=' characters are used.
private void
redrawPortalStatusLine_matches(
   Expand   *xp,
   Unt      match,
   int      showtail,
   OUT ExpandMatch* matches
){
   int      row;
   int      len;
   int      fillchar;
   int      highlight = true;
   Byte   *selstart = NULL;
   int      selstart_col = 0;
   Byte   *selend = NULL;
   static Unt firstMatch = 0;
   int      add_left = false;
   Byte   *s;
   int      l;

   if (!matches)   // interrupted completion?
      return;

   CS builder = alloc(visibleColsG * MB_MAXBYTES + 1);

   if (match == UNT) {  // don't show match but original text
      match = 0;
      highlight = false;
   }
   // count 1 for the ending ">"
   int clen = status_match_len(xp, SHOW_MATCH(match)) + 3;  // length in screen cells
   if (match == 0)
      firstMatch = 0;
   ei (match < firstMatch) {
      // jumping left, as far as we can go
      firstMatch = match;
      add_left = true;
   } else {
      // check if match fits on the screen
      for (Unt i = firstMatch; i < match; ++i)
          clen += status_match_len(xp, SHOW_MATCH(i)) + 2;
      if (firstMatch > 0)
          clen += 2;
      // jumping right, put match at the left
      if ((long)clen > visibleColsG) {
         firstMatch = match;
         // if showing the last match, we can add some on the left
         clen = 2;
         Unt i;
         for (i = match; i < matches->len; ++i) {
            clen += status_match_len(xp, SHOW_MATCH(i)) + 2;
            if ((long)clen >= visibleColsG)
               break;
         }
         if (i == matches->len)
            add_left = true;
      }
   }
   if (add_left) {
      while (firstMatch > 0) {
         clen += status_match_len(xp, SHOW_MATCH(firstMatch - 1)) + 2;
         if ((long)clen >= visibleColsG)
            break;
         --firstMatch;
      }
   } 

   Decoration deco;
   fillchar = statusLineNextChar(OUT &deco, curPor);

   if (firstMatch == 0) {
      *builder = ZERO;
      len = 0;
   } else {
      STRCPY(builder, "< ");
      len = 2;
   }
   clen = len;

   Unt i = firstMatch;
   while ((long)(clen + status_match_len(xp, SHOW_MATCH(i)) + 2) < visibleColsG) {
      if (i == match) {
         selstart = builder + len;
         selstart_col = clen;
      }

      s = SHOW_MATCH(i);
      for ( ; *s != ZERO; ++s) {
         s += skip_status_match_char(xp, s);
         clen += bookPtr2Cells(s);
         if ((l = utfCharLen(s)) > 1) {
            STRNCPY(builder + len, s, l);
            s += l - 1;
            len += l;
         } else {
            STRCPY(builder + len, transchar_byte(*s));
            len += (int)STRLEN(builder + len);
         }
      }
      if (i == match)
         selend = builder + len;

      *(builder + len++) = ' ';
      *(builder + len++) = ' ';
      clen += 2;
      if (++i == matches->len)
         break;
   }

   if (i != matches->len) {
      *(builder + len++) = '>';
      ++clen;
   }

   builder[len] = ZERO;

   row = commlineRowG - 1;
   if (row >= 0) {
      if (wild_menu_showing == 0) {
         if (msg_scrolled > 0) {
            // Put the wildmenu just above the command line. If there is
            // no room, scroll the screen one line up.
            if (commlineRowG == visibleRowsG - 1) {
               screen_del_lines(0, 0, 1, (int)visibleRowsG, true, 0, NULL);
               ++msg_scrolled;
            } else {
               ++commlineRowG;
               ++row;
            }
            wild_menu_showing = WM_SCROLLED;
         } else {
            // Create status line if needed by setting 'laststatus' to 2.
            // Set 'winminheight' to zero to avoid that the portal is resized.
            if (lastPor->statusHeight == 0) {
               last_status(false);
            }
            wild_menu_showing = WM_SHOWN;
         }
      }

      drawText(builder, row, 0, deco.flags);
      if (selstart != NULL && highlight) {
         *selend = ZERO;
         drawText(selstart, row, selstart_col, getDecoFlags(HLF_WM));
      }

      fillRowsWithTwoChars(row, row + 1, clen, (int)visibleColsG, fillchar, fillchar, deco);
   }

   redrawAllStatusLinesInFrame(topframeG);
   eeglFree(builder);
}

//Get the next or prev cmdline completion match. The index of the match is set
//in "xp->xp_selected"
private CS
get_next_or_prev_match(int mode, Expand *xp) {
   Unt       findex = xp->xp_selected;
   int       ht;

   // When no matches found, return NULL
   if (xp->files.len == UNT)
      return NULL;

   if (mode == WILD_PREV) {
      // Select the last entry if at original text
      if (findex == UNT)
         findex = xp->files.len;
      // Otherwise select the previous entry
      --findex;
   } ei (mode == WILD_NEXT) {
      // Select the next entry
      ++findex;
   } else {  // WILD_PAGEDOWN or WILD_PAGEUP
      // Get the height of popup menu (used for both PAGEUP and PAGEDOWN)
      ht = pum_get_height();
      if (ht > 3)
         ht -= 2;

      if (mode == WILD_PAGEUP) {
         if (findex == 0)
            // at the first entry, don't select any entries
            findex = -1;
         ei (findex == UNT)
            // no entry is selected. select the last entry
            findex = xp->files.len - 1;
         else
            // go up by the pum height
            findex = MAX(findex - ht, 0);
      } else {   // mode == WILD_PAGEDOWN
         if (findex >= xp->files.len - 1)
            // at the last entry, don't select any entries
            findex = -1;
         ei (findex == UNT)
            // no entry is selected, select the first entry
            findex = 0;
         else
            // go down by the pum height
            findex = MIN(findex + ht, xp->files.len - 1);
      }
   }

   // Handle wrapping around
   if (findex >= xp->files.len) {
      // If original text exists, return to it when wrapping around
      if (xp->orig)
          findex = UNT;
      else
          // Wrap around to opposite end
          findex = (findex == UNT) ? xp->files.len - 1 : 0;
   }

   // Display matches on screen
   if (popupItemsS) {
      compl_selected = findex;
      cmdline_pum_display();
   } ei (p_wmnu)
      redrawPortalStatusLine_matches(xp, findex, cmd_showtail, OUT &xp->files);

   xp->xp_selected = findex;
   // Return the original text or the selected match
   return copyStr(findex == UNT ? xp->orig : xp->files.c[findex]);
}

// Start the command-line expansion and get the matches.
private CS
expandOne_start(int mode, OUT Expand* xp, CS str, Unt options){
   int      non_suf_match;      // number without matching suffix
   int      i;
   Byte   *ss = NULL;

   // Do the expansion.
   if (expandFromContext(xp, str, options, OUT &xp->files) == FAIL) {
      //Illegal file name has been silently skipped.  But when there are wildcards, the real 
      //problem is that there was no match, causing the pattern to be added, which has illegal 
      //characters.
      if (!(options & WILD_SILENT) && (options & WILD_LIST_NOTFOUND))
          showErrFmtMsg(_(e_no_match_str_2), str);
   } ei (xp->files.len == 0) {
      if (!(options & WILD_SILENT))
          showErrFmtMsg(_(e_no_match_str_2), str);
   } else {
      // Escape the matches for use on the command line.
      expandEscape(xp, str, options, OUT &xp->files);

      // Check for matching suffixes in file names.
      if (mode != WILD_ALL && mode != WILD_ALL_KEEP && mode != WILD_LONGEST) {
         if (xp->files.len)
            non_suf_match = xp->files.len;
         else
            non_suf_match = 1;
         if ((xp->context == EXPAND_FILES || xp->context == EXPAND_DIRECTORIES)
             && xp->files.len > 1
         ){
            //More than one match; check suffix. The files will have been sorted on matching suffix
            //in expand_wildcards, only need to check the first two.
            non_suf_match = 0;
            for (i = 0; i < 2; ++i) {
               if (match_suffix(xp->files.c[i]))
                  ++non_suf_match;
            } 
         }
         if (non_suf_match != 1) {
            //Can we ever get here unless it's while expanding interactively? If not, we can get 
            //rid of this all together. Don't really want to wait for this message
            //(and possibly have to hit return to continue!).
            if (!(options & WILD_SILENT))
               emsg(_(e_too_many_file_names));
            ei (!(options & WILD_NO_BEEP))
               beep_flush();
         }
         if (!(non_suf_match != 1 && mode == WILD_EXPAND_FREE))
            ss = copyStr(xp->files.c[0]);
      }
   }

   return ss;
}

// Return the longest common part in the list of cmdline completion matches.
private Byte *
find_longest_match(Expand *xp){
   int      mb_len = 1;
   int      c0, ci;
   Unt      i;
   Ulong len;
   for (len = 0; xp->files.c[0][len]; len += mb_len) {
      mb_len = utfCharLen(&xp->files.c[0][len]);
      c0 = mb_ptr2char(&xp->files.c[0][len]);
      for (i = 1; i < xp->files.len; ++i) {
         ci = mb_ptr2char(&xp->files.c[i][len]);
         if (c0 != ci)
            break;
      }
      if (i < xp->files.len) {
         break;
      }
   }

   CS ss = alloc(len + 1);
   copySubstrToAllocation(ss, (Text){xp->files.c[0], (Unt)len});

   return ss;
}

//Do wildcard expansion on the string "str".
//Chars that should not be expanded must be preceded with a backslash.
//Return a pointer to allocated memory containing the new string.
//Return NULL for failure.
//
//"orig" is the originally expanded string, copied to allocated memory. It
//should either be kept in "xp->orig" or freed. When "mode" is WILD_NEXT
//or WILD_PREV "orig" should be NULL.
//
//Results are cached in xp->files and xp->files.len, except when "mode"
//is WILD_EXPAND_FREE or WILD_ALL.
//
//mode = WILD_FREE:        just free previously expanded matches
//mode = WILD_EXPAND_FREE: normal expansion, do not keep matches
//mode = WILD_EXPAND_KEEP: normal expansion, keep matches
//mode = WILD_NEXT:        use next match in multiple match, wrap to first
//mode = WILD_PREV:        use previous match in multiple match, wrap to first
//mode = WILD_ALL:         return all matches concatenated
//mode = WILD_LONGEST:     return longest matched part
//mode = WILD_ALL_KEEP:    get all matches, keep matches
//mode = WILD_APPLY:       apply the item selected in the cmdline completion
//                         popup menu and close the menu.
//mode = WILD_CANCEL:      cancel and close the cmdline completion popup and
//                         use the original text.
//
//options = WILD_LIST_NOTFOUND:  list entries without a match
//options = WILD_HOME_REPLACE:   do home_replace() for buffer names
//options = WILD_USE_NL:         Use '\n' for WILD_ALL
//options = WILD_NO_BEEP:        Don't beep for multiple matches
//options = WILD_ADD_SLASH:      add a slash after directory names
//options = WILD_KEEP_ALL:       don't remove 'wildignore' entries
//options = WILD_SILENT:         don't print warning messages
//options = WILD_ESCAPE:         put backslash before special chars
//options = WILD_ICASE:          ignore case for files
//options = WILD_ALLLINKS;       keep broken links
//The variables xp->context and xp->backslash must have been set!
CS
expandWildcard(
   OUT Expand* xp,
   CS str,
   CS orig,       // allocated copy of original of expanded string
   Unt      options,
   int      mode
) {
   CS ss = NULL;
   Boole orig_saved = false;

   // first handle the case of using an old match
   if (mode == WILD_NEXT || mode == WILD_PREV || mode == WILD_PAGEUP || mode == WILD_PAGEDOWN)
      return get_next_or_prev_match(mode, xp);

   if (mode == WILD_CANCEL)
      ss = copyStr(xp->orig ? xp->orig : S"");
   ei (mode == WILD_APPLY)
      ss = copyStr(xp->xp_selected == UNT
                ? (xp->orig ? xp->orig : S"")
                : xp->files.c[xp->xp_selected]);

   // free old names
   if (xp->files.len != 0 && mode != WILD_ALL && mode != WILD_LONGEST) {
      xp->files.len = 0;
      EE_CLEAR(xp->orig);

      // The entries from files may be used in the PUM, remove it.
      if (popupItemsS)
         cmdline_pum_remove(getCommlineInfo(), false);
   }
   xp->xp_selected = 0;

   if (mode == WILD_FREE)   // only release file name
      return NULL;

   if (xp->files.len == 0 && mode != WILD_APPLY && mode != WILD_CANCEL) {
      eeglFree(xp->orig);
      xp->orig = orig;
      orig_saved = true;

      ss = expandOne_start(mode, OUT xp, str, options);
   }

   // Find longest common part
   if (mode == WILD_LONGEST && xp->files.len > 0) {
      ss = find_longest_match(xp);
      xp->xp_selected = -1;         // next p_wc gets first one
   }

   // Concatenate all matching names.  Unless interrupted, this can be slow
   // and the result probably won't be used.
   if (mode == WILD_ALL && xp->files.len > 0 && !gotInterruptG) {
      Unt   ss_size = 0;
      CS prefix = S"";
      CS suffix = (options & WILD_USE_NL) ? S"\n" : S" ";
      Unt   n = xp->files.len - 1;

      if (xp->xp_prefix == XP_PREFIX_NO) {
         prefix = S"no";
         ss_size = STRLEN_LITERAL("no") * n;
      } ei (xp->xp_prefix == XP_PREFIX_INV) {
         prefix = S"inv";
         ss_size = STRLEN_LITERAL("inv") * n;
      }

      for (Unt i = 0; i < xp->files.len; ++i)
         ss_size += STRLEN(xp->files.c[i]) + 1;   // +1 for the suffix
      ++ss_size;               // +1 for the ZERO

      ss = alloc(ss_size);
      Unt  ss_len = 0;

      for (Unt i = 0; i < xp->files.len; ++i) {
         ss_len += eeSnprintfSafelen(
             ss + ss_len,
             ss_size - ss_len,
             "%s%s%s",
             (i > 0) ? prefix : S"",
             xp->files.c[i],
             (i < n) ? suffix : S"" 
         );
      }
   }

   if (mode == WILD_EXPAND_FREE || mode == WILD_ALL)
      scrExpandCleanup(xp);

   // Free "orig" if it wasn't stored in "xp->orig".
   if (!orig_saved)
      eeglFree(orig);

   return ss;
}

// Prepare an expand structure for use.
void
expandInit(OUT Expand* xp){
   CLEAR_POINTER(xp);
   xp->backslash = XP_BS_NONE;
   xp->xp_prefix = XP_PREFIX_NONE;
   xp->files.len = 0;
   xp->files.cap = 0;
}

// Cleanup an expand structure after use.
void
scrExpandCleanup(OUT Expand* xp){
   deleteArena(xp->files.a);
   xp->files.a = null;
   EE_CLEAR(xp->orig);
}

void
clear_commlineSaved(void){
   EE_CLEAR(commlineSaved);
}

//Display one line of completion matches. Multiple matches are displayed in
//each line (used by wildmode=list and CTRL-D)
//  matches - list of completion match names
//  numMatches - number of completion matches in "matches"
//  lines - number of output lines
//  linenr - line number of matches to display
//  maxlen - maximum number of characters in each line
//  showtail - display only the tail of the full path of a file name
//  dir_attr - hilite decoration to use for directory names
private void
showmatches_oneline(
   Expand   *xp,
   int      lines,
   int      linenr,
   int      maxlen,
   int      showtail,
   int      dir_attr,
   OUT ExpandMatch* matches
) {
   int      i;
   int      isdir;
   Byte   *p;

   int lastlen = 999;
   for (Unt j = linenr; j < matches->len; j += lines) {
      if (xp->context == EXPAND_TAGS_LISTFILES) {
         msgOuttransDeco(matches->c[j], getDecoFlags(HLF_D));
         p = matches->c[j] + STRLEN(matches->c[j]) + 1;
         msg_advance(maxlen + 1);
         msg_puts(p);
         msg_advance(maxlen + 3);
         outputShortenedToALine(text(p + 2), getDecoFlags(HLF_D));
         break;
      }
      for (i = maxlen - lastlen; --i >= 0; )
          msg_putchar(' ');
      if (xp->context == EXPAND_FILES
         || xp->context == EXPAND_SHELLCMD
         || xp->context == EXPAND_BUFFERS
      ){
         // highlight directories
         if (xp->files.len != UNT) {
            Byte   *halved_slash;
            Byte   *exp_path;
            Byte   *path;

            // Expansion was done before and special characters
            // were escaped, need to halve backslashes.  Also
            // $HOME has been replaced with ~/.
            exp_path = doExpandEnvInFilePaths(matches->c[j], true);
            path = exp_path != NULL ? exp_path : matches->c[j];
            halved_slash = backslash_halve_save(path);
            isdir = mch_isdir(halved_slash != NULL ? halved_slash : matches->c[j]);
            eeglFree(exp_path);
            if (halved_slash != path)
                eeglFree(halved_slash);
         } else
            // Expansion was done here, file names are literal.
            isdir = mch_isdir(matches->c[j]);
         if (showtail)
            p = SHOW_MATCH(j);
         else {
            home_replace(matches->c[j], nameBuffG, MAXPATHL, true);
            p = nameBuffG;
         }
      } else {
          isdir = false;
          p = SHOW_MATCH(j);
      }
      lastlen = msgOuttransDeco(p, isdir ? dir_attr : 0);
   }
   if (msgColG > 0) {  // when not wrapped around
      msg_clr_eos();
      msg_putchar('\n');
   }
   out_flush();          // show one line at a time
}

//Show all matches for completion on the command line. Return EXPAND_NOTHING when the character
//that triggered expansion should be inserted like a normal character.
int
showmatches(Expand *xp, int wildmenu, int noselect){
   CommlineInfo* ccline = getCommlineInfo();
   int i;
   int maxlen;
   int lines;
   int columns;
   int attr;
   int showtail;

   // Save cmdline before expansion
   if (ccline->commBuf != NULL) {
      eeglFree(commlineSaved);
      commlineSaved = copySubstr(ccline->commBuf, ccline->cmdlen);
   }
   ExpandMatch matches = {};
   matches.a = xp->files.a;
   
   if (xp->files.len == 0) {
      int retval;
      set_expand_context(xp);
      retval = expandCommline(xp, ccline->commBuf, ccline->cmdpos, OUT &matches);
      if (retval != EXPAND_OK)
          return retval;
      showtail = expand_showtail(xp);
   } else {
      matches = xp->files;
      showtail = cmd_showtail;
   }

   if (wildmenu && (p_wop & WILDOPT_PUM) != 0)
      // cmdline completion popup menu (with wildoptions=pum)
      return createCommlinePum(ccline, xp, showtail && !noselect, OUT &matches);

   if (!wildmenu) {
      msg_didany = false;      // lines_left will be set
      msg_start();         // prepare for paging
      msg_putchar('\n');
      out_flush();
      commlineRowG = msgRowG;
      msg_didany = false;      // lines_left will be set again
      msg_start();         // prepare for paging
   }

   if (gotInterruptG)
      gotInterruptG = false;   // only int. the completion, not the comm line
   ei (wildmenu)
      redrawPortalStatusLine_matches(xp, UNT, showtail, OUT &matches);
   else {
      // find the length of the longest file name
      maxlen = 0;
      for (Unt i = 0; i < matches.len; ++i) {
         int   len;
         if (!showtail && (xp->context == EXPAND_FILES
              || xp->context == EXPAND_SHELLCMD
              || xp->context == EXPAND_BUFFERS)
         ){
            home_replace(matches.c[i], nameBuffG, MAXPATHL, true);
            len = eeglStrSize(nameBuffG);
         } else
            len = eeglStrSize(showtail ? showmatches_gettail(matches.c[i]) : matches.c[i]);
         if (len > maxlen)
            maxlen = len;
      }

      if (xp->context == EXPAND_TAGS_LISTFILES)
          lines = matches.len;
      else {
         // compute the number of columns and lines for the listing
         maxlen += 2;    // two spaces between file names
         columns = ((int)visibleColsG + 2) / maxlen;
         if (columns < 1)
            columns = 1;
         lines = (matches.len + columns - 1) / columns;
      }

      attr = getDecoFlags(HLF_D);   // find out highlighting for directories

      if (xp->context == EXPAND_TAGS_LISTFILES) {
          msgPutsDeco(_("tagname"), getDecoFlags(HLF_T));
          msg_clr_eos();
          msg_advance(maxlen - 3);
          msgPutsDeco(_(" kind file\n"), getDecoFlags(HLF_T));
      }

      // list the files line by line
      for (i = 0; i < lines; ++i) {
         showmatches_oneline(xp, lines, i, maxlen, showtail, attr, OUT &matches);
         if (gotInterruptG) {
            gotInterruptG = false;
            break;
         }
      }

      // we redraw the command below the lines that we have just listed
      // This is a bit tricky, but it saves a lot of screen updating.
      commlineRowG = msgRowG;   // will put it back later
   }

   return EXPAND_OK;
}

//fiGetShortFiName() version for showmatches() and redrawPortalStatusLine_matches():
//Return the tail of file name path "s", ignoring a trailing "/".
private CS
showmatches_gettail(CS s) {
   Byte   *p;
   Byte   *t = s;
   int      had_sep = false;

   for (p = s; *p != ZERO; ) {
      if (*p == '/')
         had_sep = true;
      ei (had_sep) {
         t = p;
         had_sep = false;
      }
      MB_PTR_ADV(p);
   }
   return t;
}

//Return true if we only need to show the tail of completion matches.
//When not completing file names or there is a wildcard in the path false is returned.
private int
expand_showtail(Expand *xp) {
   // When not completing file names a "/" may mean something different.
   if (xp->context != EXPAND_FILES
          && xp->context != EXPAND_SHELLCMD
          && xp->context != EXPAND_DIRECTORIES)
      return false;

   CS end = fiGetShortFiName(xp->input.c);
   if (end == xp->input.c)      // there is no path separator
      return false;

   for (CS s = xp->input.c; s < end; s++) {
      // Skip escaped wildcards.  Only when the backslash is not a path
      // separator, on DOS the '*' "path\*\file" must not be skipped.
      if (rem_backslash(s))
          ++s;
      ei (firstOccurrence(S"*?[", *s) != NULL)
          return false;
   }
   return true;
}

//Prepare a string for expansion.
//When expanding file names: The string will be used with expand_wildcards().
//Copy "fname[len]" into allocated memory and add a '*' at the end.
//When expanding other names: The string will be used with regcomp().  Copy
//the name into allocated memory and prepend "^".
CS
addstar(Text fname, Unt context) {  // EXPAND_FILES etc.
   CS retval;
   CS tail;
   int      ends_in_star;

   if (context != EXPAND_FILES
       && context != EXPAND_FILES_IN_PATH
       && context != EXPAND_SHELLCMD
       && context != EXPAND_DIRECTORIES
       && context != EXPAND_DIRS_IN_CDPATH
   ) {
      //Matching will be done internally (on something other than files).
      //So we convert the file-matching-type wildcards into our kind for
      //use with compileRegexp().  First work out how long it will be:

      //For help tags the translation is done in find_help_tags().
      //For a tag pattern starting with "/" no translation is needed.
      if (context == EXPAND_FINDFUNC
            || context == EXPAND_HELP
            || context == EXPAND_COLORS
            || context == EXPAND_COMPILER
            || context == EXPAND_OWNSYNTAX
            || context == EXPAND_FILETYPE
            || context == EXPAND_KEYMAP
            || context == EXPAND_PACKADD
            || context == EXPAND_RUNTIME
            || ((context == EXPAND_TAGS_LISTFILES || context == EXPAND_TAGS) && fname.c[0] == '/')
      )
         retval = copySubstr(fname.c, fname.len);
      else {
         int new_len = fname.len + 2;      // +2 for '^' at start, ZERO at end
         for (Unt i = 0; i < fname.len; i++) {
            if (fname.c[i] == '*' || fname.c[i] == '~')
               new_len++;   // '*' needs to be replaced by ".*"
                            // '~' needs to be replaced by "\~"

            // Book names are like file names.  "." should be literal
            if (context == EXPAND_BUFFERS && fname.c[i] == '.')
               new_len++;   // "." becomes "\."

            // Custom expansion takes care of special things, match
            // backslashes literally (perhaps also for other types?)
            if ((context == EXPAND_USER_DEFINED
                 || context == EXPAND_USER_LIST) && fname.c[i] == '\\')
               new_len++;   // '\' becomes "\\"
         }
         retval = alloc(new_len);
         retval[0] = '^';
         Unt j = 1;
         for (Unt i = 0; i < fname.len; i++, j++) {
            // Skip backslash.  But why?  At least keep it for custom expansion.
            if (context != EXPAND_USER_DEFINED
                   && context != EXPAND_USER_LIST
                   && fname.c[i] == '\\'
                   && ++i == fname.len)
               break;

            switch (fname.c[i]) {
            case '*':   retval[j++] = '.'; break;
            case '~':   retval[j++] = '\\'; break;
            case '?':   retval[j] = '.'; continue;
            case '.':   
               if (context == EXPAND_BUFFERS)
                  retval[j++] = '\\';
               break;
            case '\\':  
               if (context == EXPAND_USER_DEFINED || context == EXPAND_USER_LIST)
                  retval[j++] = '\\';
               break;
            }
            retval[j] = fname.c[i];
         }
         retval[j] = ZERO;
      }
   } else {
      retval = alloc(fname.len + 4);
      copySubstrToAllocation(retval, fname);

      // Don't add a star to *, ~, ~user, $var or `comm`.
      // * would become **, which walks the whole tree.
      // ~ would be at the start of the file name, but not the tail.
      // $ could be anywhere in the tail.
      // ` could be anywhere in the file name.
      // When the name ends in '$' don't add a star, remove the '$'.
      tail = fiGetShortFiName(retval);
      ends_in_star = (fname.len > 0 && retval[fname.len - 1] == '*');
      for (int i = fname.len - 2; i >= 0; --i) {
         if (retval[i] != '\\')
             break;
         ends_in_star = !ends_in_star;
      }
      int len = fname.len;
      if ((*retval != '~' || tail != retval)
            && !ends_in_star
            && firstOccurrence(tail, '$') == NULL
            && firstOccurrence(retval, '`') == NULL)
         retval[len++] = '*';
      ei (len > 0 && retval[len - 1] == '$')
         --len;
      retval[len] = ZERO;
   }
   return retval;
}

//Must parse the command line so far to work out what context we are in.
//Completion can then be done based on that context.
//This routine sets the variables:
// xp->input       The start of the pattern to be expanded within
//           the command line (ends at the cursor).
// xp->context       The type of thing to expand.  Will be one of:
//
// EXPAND_UNSUCCESSFUL       Used sometimes when there is something illegal on
//            the command line, like an unknown command.
// EXPAND_NOTHING       Unrecognised context for completion, use char like
//            a normal char, rather than for completion.   eg :s/^I/
// EXPAND_COMMANDS       Cursor is still touching the command, so complete it.
// EXPAND_BUFFERS   Complete file names for :buf and :sbuf commands.
// EXPAND_FILES     After command with XFILE set, or after setting
//                  with P_EXPAND set.   eg :e ^I, :w>>^I
// EXPAND_DIRECTORIES       In some cases this is used instead of the latter when we know only 
//    directories are of interest. E.g.  :set dir=^I  and  :cd ^I
// EXPAND_SHELLCMD       After ":!comm", ":r !comm"  or ":w !comm".
// EXPAND_OPTION       Complete variable names.  eg :set d^I
// EXPAND_TAGS          Complete tags from the files in p_tags.  eg :ta a^I
// EXPAND_TAGS_LISTFILES   As above, but list filenames on ^D, after :tselect
// EXPAND_HELP          Complete tags from the file 'helpfile'/tags
// EXPAND_EVENTS       Complete event names
// EXPAND_SYNTAX       Complete :syntax command arguments
// EXPAND_HILITE_GROUP       Complete highlight (syntax) group names
// EXPAND_AUGROUP       Complete autocommand group names
// EXPAND_USER_VARS       Complete user defined variable names, eg :unlet a^I
// EXPAND_MAPPINGS       Complete mapping and abbreviation names, eg :unmap a^I , :cunab x^I
// EXPAND_FUNCTIONS       Complete internal or user defined function names, eg :call sub^I
// EXPAND_USER_FUNC       Complete user defined function names, eg :delf F^I
// EXPAND_EXPRESSION       Complete internal or user defined function/variable
//                        names in expressions, eg :while s^I
// EXPAND_ENV_VARS       Complete environment variable names
// EXPAND_USER          Complete user names
// EXPAND_PATTERN_IN_BUF   Complete pattern in '/', '?', ':s', ':g', etc.
void
set_expand_context(Expand *xp){
   CommlineInfo  *ccline = getCommlineInfo();

   // Handle search commands: '/' or '?'
   if ((ccline->cmdfirstc == '/' || ccline->cmdfirstc == '?') && may_expand_pattern) {
      xp->context = EXPAND_PATTERN_IN_BUF;
      xp->searchDirection = (ccline->cmdfirstc == '/') ? FORWARD : BACKWARD;
      xp->input.c = ccline->commBuf;
      xp->input.len = ccline->cmdpos;
      search_first_line = 0; // Search entire buffer
      return;
   }

   // Only handle ':', '>', or '=' command-lines, or expression input
   if (ccline->cmdfirstc != ':'
       && ccline->cmdfirstc != '>' && ccline->cmdfirstc != '='
       && !ccline->input_fn
   ) {
      xp->context = EXPAND_NOTHING;
      return;
   }

   // Fallback to command-line expansion
   setCompletionContextForCommand(OUT xp, (Text){ccline->commBuf, ccline->cmdlen}, ccline->cmdpos, 
         true
   );
}

//Sets the index of a built-in or user defined command 'comm' in invo->id.
//For user defined commands, the completion context is set in 'xp' and the
//completion flags in 'context'.
//
//Return a pointer to the text after the command or NULL for failure.
private CS
set_cmd_index(CS comm, Invocation* invo, Expand *xp, OUT Unt *context) {
   Byte   *p = NULL;
   int      len = 0;
   int      fuzzy = scrIsCommlineFuzzyCompletable(comm);

   // Isolate the command and search for it in the command table.
   // Exceptions:
   // - the 'k' command can directly be followed by any character, but do
   // accept "keepmarks", "keepalt" and "keepjumps". As fuzzy matching can
   // find matches anywhere in the command name, do this only for command
   // expansion based on regular expression and not for fuzzy matching.
   // - the 's' command can be followed directly by 'c', 'g', 'i', 'I' or 'r'
   if (!fuzzy && (*comm == 'k' && comm[1] != 'e')) {
      invo->id = C_k;
      p = comm + 1;
   } else {
      p = comm;
      while (ASCII_ISALPHA(*p) || *p == '*')    // Allow * wild card
         ++p;
      // check for non-alpha command
      if (p == comm && firstOccurrence((CS)"@*!=><&~#", *p) != NULL)
         ++p;
      len = (int)(p - comm);

      if (len == 0) {
          xp->context = EXPAND_UNSUCCESSFUL;
          return NULL;
      }

      invo->id = commandGetInd(comm, len);

      // User defined commands support alphanumeric characters.
      // Also when doing fuzzy expansion for non-shell commands, support
      // alphanumeric characters.
      if ((comm[0] >= 'A' && comm[0] <= 'Z') || (fuzzy && invo->id != C_bang && *p != ZERO)) {
         while (ASCII_ISALNUM(*p) || *p == '*')   // Allow * wild card
            ++p;
      } 
   }

   // If the cursor is touching the command, and it ends in an alphanumeric
   // character, complete the command name.
   if (*p == ZERO && ASCII_ISALNUM(p[-1]))
      return NULL;

   if (invo->id == COUNT_COMMANDS) {
      if (*comm == 's' && firstOccurrence((CS)"cgriI", comm[1]) != NULL) {
         invo->id = C_substitute;
         p = comm + 1;
      } ei (comm[0] >= 'A' && comm[0] <= 'Z') {
         invo->comm = comm;
         p = find_ucmd(invo, p, NULL, xp, context);
         if (p == NULL)
            invo->id = COUNT_COMMANDS;   // ambiguous user command
      }
   }
   if (invo->id == COUNT_COMMANDS) {
      // Not still touching the command and it was an illegal one
      xp->context = EXPAND_UNSUCCESSFUL;
      return NULL;
   }

   return p;
}

// Set the completion context for a command argument with wild card characters.
private void
set_context_for_wildcard_arg(
   Invocation* invo,
   CS arg,
   int  usefilter,
   Expand* xp,
   OUT Unt* context
) {
   int      c;
   int      in_quote = false;
   CS word = NULL;   // Beginning of word
   int      len = 0;

   // Allow spaces within back-quotes to count as part of the argument being expanded.
   xp->input = text(skipwhite(arg));
   CS p = xp->input.c;
   while (*p != ZERO) {
      c = mb_ptr2char(p);
      if (c == '\\' && p[1] != ZERO)
         ++p;
      ei (c == '`') {
         if (!in_quote) {
            xp->input = skipTo(xp->input, p);
            word = p + 1;
         }
         in_quote = !in_quote;
      }
      // An argument can contain just about everything, except
      // characters that end the command and white space.
      ei (c == '|' || c == '\n' || c == '"' || (SPACE_OR_TAB(c)
#ifdef SPACE_IN_FILENAME
             && (!(invo != NULL && (invo->argFlags & NOSPC_IN_EXTRA)) || usefilter)
#endif
             ))
      {
         len = 0;  // avoid getting stuck when space is in 'isfname'
         while (*p != ZERO) {
            c = mb_ptr2char(p);
            if (c == '`' || eeIsFnameChar_or_wc(c))
                break;
            len = utfCharLen(p);
            MB_PTR_ADV(p);
         }
         if (in_quote)
            word = p;
         else
            xp->input = skipTo(xp->input, p);
         p -= len;
      }
      MB_PTR_ADV(p);
   }

   // If we are still inside the quotes, and we passed a space, just expand from there.
   if (word && in_quote)
      xp->input = skipTo(xp->input, word);
   xp->context = EXPAND_FILES;

   // For a shell command more chars need to be escaped.
   if (usefilter
       || (invo != NULL && (invo->id == C_bang || invo->id == C_terminal))
       || *context == EXPAND_SHELLCMDLINE
   ){
      xp->isShell = true;
      // When still after the command name expand executables.
      if (xp->input.c == skipwhite(arg))
         xp->context = EXPAND_SHELLCMD;
   }

   // Check for environment variable.
   if (xp->input.c[0] == '$') {
      for (p = xp->input.c + 1; *p != ZERO; ++p) {
         if (!eeIsIdentifierChar(*p))
            break;
      } 
      if (*p == ZERO) {
         xp->context = EXPAND_ENV_VARS;
         xp->input.c++;
         xp->input.len--;
         // Avoid that the assignment uses EXPAND_FILES again.
         if (*context != EXPAND_USER_DEFINED && *context != EXPAND_USER_LIST)
            *context = EXPAND_ENV_VARS;
      }
   }
   // Check for user names.
   if (xp->input.c[0] == '~') {
      for (p = xp->input.c + 1; *p != ZERO && *p != '/'; ++p)
          ;
      // Complete ~user only if it partially matches a user name.
      // A full match ~user<Tab> will be replaced by user's home
      // directory i.e. something like ~user<Tab> -> /home/user/
      if (*p == ZERO && p > xp->input.c + 1 && match_user(xp->input.c + 1) >= 1) {
          xp->context = EXPAND_USER;
          xp->input.c++;
          xp->input.len--;
      }
   }
}

// Set the completion context for the "++opt=arg" argument. Always return NULL.
private CS
set_context_in_argopt(Expand *xp, CS arg) {
   Byte* p = firstOccurrence(arg, '=');
   xp->input = p ? text(p + 1) : text(arg);
   xp->context = EXPAND_ARGOPT;
   return NULL;
}

// Set the completion context for :terminal's [options]. Always return NULL.
private CS
set_context_in_terminalopt(Expand *xp, CS arg) {
   Byte* p = firstOccurrence(arg, '=');
   xp->input = p ? text(p + 1) : text(arg);
   xp->context = EXPAND_TERMINALOPT;
   return NULL;
}

// Set the completion context for the :filter command. Return a pointer to the
// next command after the :filter command.
private CS
setContextInFilterComm(Expand *xp, CS arg) {
   if (*arg != ZERO)
      arg = skipEeglGrepPat(arg, NULL, NULL);
   if (!arg || *arg == ZERO) {
      xp->context = EXPAND_NOTHING;
      return NULL;
   }
   return skipwhite(arg);
}

//Set the completion context for the :match command. Return a pointer to the
//next command after the :match command.
private Byte *
setContextInMatchComm(Expand *xp, Byte *arg) {
   if (*arg == ZERO || !endsComm(arg)) {
      // also complete "None"
      set_context_in_echohl_cmd(xp, arg);
      arg = skipwhite(skiptowhite(arg));
      if (*arg != ZERO) {
          xp->context = EXPAND_NOTHING;
          arg = skip_regexp(arg + 1, *arg, true);
      }
   }
   return find_nextcmd(arg);
}

//Return a pointer to the next command after a :global or a :v command.
//NULL if there is no next command.
private Byte *
find_cmd_after_global_cmd(Byte *arg) {
   int      delim;

   delim = *arg;       // get the delimiter
   if (delim)
      ++arg;          // skip delimiter if there is one

   while (arg[0] != ZERO && arg[0] != delim) {
      if (arg[0] == '\\' && arg[1] != ZERO)
          ++arg;
      ++arg;
   }
   if (arg[0] != ZERO)
      return arg + 1;
   return NULL;
}

//Return a pointer to the next command after a :substitute or a :& command.
//NULL if there is no next command.
private CS
find_cmd_after_substitute_cmd(Byte *arg) {
   int delim = *arg;
   if (delim) {
      // skip "from" part
      ++arg;
      arg = skip_regexp(arg, delim, true);

      if (arg[0] != ZERO && arg[0] == delim) {
         // skip "to" part
         ++arg;
         while (arg[0] != ZERO && arg[0] != delim) {
            if (arg[0] == '\\' && arg[1] != ZERO)
                ++arg;
            ++arg;
         }
         if (arg[0] != ZERO)   // skip delimiter
            ++arg;
      }
   }
   while (arg[0] && firstOccurrence((CS)"|\"#", arg[0]) == NULL)
      ++arg;
   if (arg[0] != ZERO)
      return arg;

   return NULL;
}

//Return a pointer to the next command after a :isearch/:dsearch/:ilist
//:dlist/:ijump/:psearch/:djump/:isplit/:dsplit command.
//Return NULL if there is no next command.
private Byte *
find_cmd_after_isearch_cmd(Expand *xp, Byte *arg) {
   arg = skipwhite(skipdigits(arg));       // skip count
   if (*arg != '/')
      return NULL;

   // Match regexp, not just whole words
   for (++arg; *arg && *arg != '/'; arg++) {
      if (*arg == '\\' && arg[1] != ZERO)
          arg++;
   }
   if (*arg) {
      arg = skipwhite(arg + 1);

      // Check for trailing illegal characters
      if (*arg == ZERO || firstOccurrence((CS)"|\"\n", *arg) == NULL)
         xp->context = EXPAND_NOTHING;
      else
         return arg;
    }

    return NULL;
}

//Set the completion context for the :unlet command. Always return NULL.
private CS
set_context_in_unlet_cmd(Expand *xp, CS arg) {
   CS p = firstOccurrence(arg, ' ');
   CS q;
   for (; p != null; p = firstOccurrence(q, ' ')) {
      q = p + 1;
   }

   xp->context = EXPAND_USER_VARS;
   xp->input = mbText(q);

   if (xp->input.c[0] == '$') {
      xp->context = EXPAND_ENV_VARS;
      xp->input.c++;
      xp->input.len--;
   }

   return NULL;
}


// Set the completion context for the :language command. Always return NULL.
private CS
setContextInLangCommand(Expand *xp, CS arg){
   CS p = skiptowhite(arg);
   if (*p == ZERO) {
      xp->context = EXPAND_LANGUAGE;
      xp->input = text(arg);
   } else {
      if ( STRNCMP(arg, "messages", p - arg) == 0
         || STRNCMP(arg, "ctype", p - arg) == 0
         || STRNCMP(arg, "time", p - arg) == 0
         || STRNCMP(arg, "collate", p - arg) == 0
      ) {
         xp->context = EXPAND_LOCALES;
         xp->input = text(skipwhite(p));
      } else
         xp->context = EXPAND_NOTHING;
   }

   return NULL;
}

private enum {
   EXP_FILETYPECMD_ALL,   // expand all :filetype values
   EXP_FILETYPECMD_PLUGIN,   // expand plugin on off
   EXP_FILETYPECMD_INDENT,   // expand indent on off
   EXP_FILETYPECMD_ONOFF,   // expand on off
} filetype_expand_what;

#define EXPAND_FILETYPECMD_PLUGIN 0x01
#define EXPAND_FILETYPECMD_INDENT 0x02
#define EXPAND_FILETYPECMD_ONOFF  0x04

private enum {
   EXP_BREAKPT_ADD,   // expand ":breakadd" sub-commands
   EXP_BREAKPT_DEL,   // expand ":breakdel" sub-commands
   EXP_PROFDEL      // expand ":profdel" sub-commands
} breakpt_expand_what;

//Set the completion context for the :breakadd command. Always return NULL.
private CS
set_context_in_breakadd_cmd(Expand *xp, CS arg, CommIndex id) {
   Byte *subcmd_start;

   xp->context = EXPAND_BREAKPOINT;
   xp->input = mbText(arg);

   if (id == C_breakadd)
      breakpt_expand_what = EXP_BREAKPT_ADD;
   ei (id == C_breakdel)
      breakpt_expand_what = EXP_BREAKPT_DEL;
   else
      breakpt_expand_what = EXP_PROFDEL;

   CS p = skipwhite(arg);
   if (*p == ZERO)
      return NULL;
   subcmd_start = p;

   if (STRNCMP("file ", p, 5) == 0 || STRNCMP("func ", p, 5) == 0) {
      // :breakadd file [lnum] <filename>
      // :breakadd func [lnum] <funcname>
      p += 4;
      p = skipwhite(p);

      // skip line number (if specified)
      if (EE_ISDIGIT(*p)) {
         p = skipdigits(p);
         if (*p != ' ') {
            xp->context = EXPAND_NOTHING;
            return NULL;
         }
         p = skipwhite(p);
      }
      if (STRNCMP("file", subcmd_start, 4) == 0)
         xp->context = EXPAND_FILES;
      else
         xp->context = EXPAND_USER_FUNC;
      xp->input = mbText(p);
   } ei (STRNCMP("expr ", p, 5) == 0) {
      // :breakadd expr <expression>
      xp->context = EXPAND_EXPRESSION;
      xp->input = text(skipwhite(p + 5));
   }

   return NULL;
}

private CS
set_context_in_scriptnames_cmd(Expand *xp, Byte *arg) {
   xp->context = EXPAND_NOTHING;
   xp->input.len = 0;

   Byte* p = skipwhite(arg);
   if (EE_ISDIGIT(*p))
      return NULL;

   xp->context = EXPAND_SCRIPTNAMES;
   xp->input = text(p);

   return NULL;
}

// Set the completion context for the :filetype command. Always return NULL.
private CS
set_context_in_filetype_cmd(Expand *xp, CS arg) {
   xp->context = EXPAND_FILETYPECMD;
   xp->input = mbText(arg);
   filetype_expand_what = EXP_FILETYPECMD_ALL;

   CS p = skipwhite(arg);
   if (*p == ZERO)
      return NULL;

   int  val = 0;
   for (;;) {
      if (STRNCMP(p, "plugin", 6) == 0) {
         val |= EXPAND_FILETYPECMD_PLUGIN;
         p = skipwhite(p + 6);
         continue;
      }
      if (STRNCMP(p, "indent", 6) == 0) {
         val |= EXPAND_FILETYPECMD_INDENT;
         p = skipwhite(p + 6);
         continue;
      }
      break;
   }

   if ((val & EXPAND_FILETYPECMD_PLUGIN) && (val & EXPAND_FILETYPECMD_INDENT))
      filetype_expand_what = EXP_FILETYPECMD_ONOFF;
   ei ((val & EXPAND_FILETYPECMD_PLUGIN))
      filetype_expand_what = EXP_FILETYPECMD_INDENT;
   ei ((val & EXPAND_FILETYPECMD_INDENT))
      filetype_expand_what = EXP_FILETYPECMD_PLUGIN;

   xp->input = text(p);

   return NULL;
}

//Set the completion context for commands that involve a search pattern
//and a line range (e.g., :s, :g, :v).
private void
set_context_with_pattern(Expand *xp){
   int skiplen = 0;
   CommlineInfo  *ccline = getCommlineInfo();
   int patlen, retval;
   Unt dummy;

   ++emsg_off;
   retval = parse_pattern_and_range(&pre_incsearch_pos, &dummy, &skiplen, &patlen);
   --emsg_off;

   // Check if cursor is within search pattern
   if (!retval || ccline->cmdpos <= skiplen || ccline->cmdpos > skiplen + patlen)
      return;

   xp->input.c = ccline->commBuf + skiplen;
   xp->input.len = ccline->cmdpos - skiplen;
   xp->context = EXPAND_PATTERN_IN_BUF;
   xp->searchDirection = FORWARD;
}

//Set the completion context in 'xp' for command 'comm' with index 'id'.
//For user-defined commands and for environment variables, 'compl' has the completion type.
//Return a pointer to the next command. Return NULL if there is no next command.
private CS
setContextByCommandName(
   CS comm,
   CommIndex   id,
   OUT Expand* xp,
   CS arg,
   long argFlags,
   Unt context,
   Boole forceit
){
   Byte  *nextComm;

   switch (id) {
   case C_find:
   case C_sfind:
   case C_tabfind:
      if (xp->context == EXPAND_FILES)
         xp->context = curBook->o.findFn ? EXPAND_FINDFUNC : EXPAND_FILES_IN_PATH;
      break;
   case C_cd:
   case C_chdir:
   case C_tcd:
   case C_tchdir:
   case C_lcd:
   case C_lchdir:
      if (xp->context == EXPAND_FILES)
         xp->context = EXPAND_DIRS_IN_CDPATH;
      break;
   case C_help:
      xp->context = EXPAND_HELP;
      xp->input = mbText(arg);
      break;

   // Command modifiers: return the argument.
   // Also for commands with an argument that is a command.
   case C_aboveleft:
   case C_argdo:
   case C_belowright:
   case C_botright:
   case C_browse:
   case C_bufdo:
   case C_confirm:
   case C_debug:
   case C_folddoclosed:
   case C_folddoopen:
   case C_hide:
   case C_horizontal:
   case C_keepalt:
   case C_keepjumps:
   case C_keepmarks:
   case C_keeppatterns:
   case C_ldo:
   case C_leftabove:
   case C_lfdo:
   case C_lockmarks:
   case C_noautocmd:
   case C_noswapfile:
   case C_rightbelow:
   case C_silent:
   case C_tab:
   case C_tabdo:
   case C_topleft:
   case C_unsilent:
   case C_verbose:
   case C_vertical:
   case C_windo:
   case C_legacy:
      return arg;

   case C_filter:
      return setContextInFilterComm(xp, arg);

   case C_match:
      return setContextInMatchComm(xp, arg);

   // All completion for the +cmdline_compl feature goes here.

   case C_command:
      return set_context_in_user_cmd(xp, arg);

   case C_delcommand:
      xp->context = EXPAND_USER_COMMANDS;
      xp->input = mbText(arg);
      break;

   case C_global:
   case C_vglobal:
      nextComm = find_cmd_after_global_cmd(arg);
      if (!nextComm && may_expand_pattern)
         set_context_with_pattern(xp);
      return nextComm;

   case C_and:
   case C_substitute:
      nextComm = find_cmd_after_substitute_cmd(arg);
      if (!nextComm && may_expand_pattern)
         set_context_with_pattern(xp);
      return nextComm;

   case C_isearch:
   case C_dsearch:
   case C_ilist:
   case C_dlist:
   case C_ijump:
   case C_psearch:
   case C_djump:
   case C_isplit:
   case C_dsplit:
       return find_cmd_after_isearch_cmd(xp, arg);
   case C_autocmd:
       return set_context_in_autocmd(OUT xp, arg, false);
   case C_doautocmd:
   case C_doautoall:
       return set_context_in_autocmd(OUT xp, arg, true);
   case C_get: 
   case C_set:
       optInitExpandContextForSet(OUT xp, arg, SET_LOCAL);
       break;
   case C_getGlobal: 
   case C_setglobal:
       optInitExpandContextForSet(xp, arg, SET_GLOBAL);
       break;
   case C_tag:
   case C_stag:
   case C_ptag:
   case C_ltag:
   case C_tselect:
   case C_stselect:
   case C_ptselect:
   case C_tjump:
   case C_stjump:
   case C_ptjump:
      if ((p_wop & WILDOPT_TAGFILE) != 0)
         xp->context = EXPAND_TAGS_LISTFILES;
      else
         xp->context = EXPAND_TAGS;
      xp->input = mbText(arg);
      break;
   case C_augroup:
       xp->context = EXPAND_AUGROUP;
       xp->input = mbText(arg);
       break;
   case C_syntax:
       set_context_in_syntax_cmd(xp, arg);
       break;
   case C_final:
   case C_const:
   case C_let:
   case C_if:
   case C_elseif:
   case C_while:
   case C_for:
   case C_echo:
   case C_echon:
   case C_execute:
   case C_echomsg:
   case C_echoerr:
   case C_call:
   case C_return:
   case C_lexpr:
   case C_laddexpr:
   case C_lgetexpr:
      set_context_for_expression(xp, arg, id);
      break;

   case C_unlet:
      return set_context_in_unlet_cmd(xp, arg);
   case C_delfunction:
      xp->context = EXPAND_USER_FUNC;
      xp->input = mbText(arg);
      break;
   case C_echohl:
      set_context_in_echohl_cmd(xp, arg);
      break;
   case C_highlight:
      setCompletionContextInHiliteCommand(OUT xp, arg);
      break;
   case C_cscope:
   case C_lcscope:
   case C_scscope:
      set_context_in_cscope_cmd(xp, arg, id);
      break;
   case C_sign:
      set_context_in_sign_cmd(xp, arg);
      break;
   case C_bdelete:
   case C_bwipeout:
   case C_bunload:
      arg = skipToLastSpace(arg);
      // FALLTHROUGH
   case C_book:
   case C_sbuffer:
   case C_pbuffer:
   case C_checktime:
      xp->context = EXPAND_BUFFERS;
      xp->input = mbText(arg);
      break;
   case C_diffget:
   case C_diffput:
      // If current buffer is in diff mode, complete buffer names
      // which are in diff mode, and different than current buffer.
      xp->context = EXPAND_DIFF_BUFFERS;
      xp->input = mbText(arg);
      break;
   case C_USER:
   case C_USER_BUF:
      return set_context_in_user_cmdarg(comm, arg, argFlags, context, xp, forceit);

   case C_map:       case C_noremap:
   case C_nmap:      case C_nnoremap:
   case C_vmap:      case C_vnoremap:
   case C_omap:      case C_onoremap:
   case C_imap:      case C_inoremap:
   case C_cmap:      case C_cnoremap:
   case C_lmap:      case C_lnoremap:
   case C_snoremap:
   case C_tmap:      case C_tnoremap:
   case C_xmap:      case C_xnoremap:
       return set_context_in_map_cmd(xp, comm, arg, forceit, false, false, id);
   case C_unmap:
   case C_nunmap:
   case C_vunmap:
   case C_ounmap:
   case C_iunmap:
   case C_cunmap:
   case C_lunmap:
   case C_sunmap:
   case C_tunmap:
   case C_xunmap:
       return set_context_in_map_cmd(xp, comm, arg, forceit, false, true, id);
   case C_mapclear:
   case C_nmapclear:
   case C_vmapclear:
   case C_omapclear:
   case C_imapclear:
   case C_cmapclear:
   case C_lmapclear:
   case C_tmapclear:
   case C_xmapclear:
      xp->context = EXPAND_MAPCLEAR;
      xp->input = mbText(arg);
      break;

   case C_abbreviate: case C_noreabbrev:
   case C_cabbrev:    case C_cnoreabbrev:
   case C_iabbrev:    case C_inoreabbrev:
      return set_context_in_map_cmd(xp, comm, arg, forceit, true, false, id);
   case C_unabbreviate:
   case C_cunabbrev:
   case C_iunabbrev:
      return set_context_in_map_cmd(xp, comm, arg, forceit, true, true, id);

   case C_compiler:
      xp->context = EXPAND_COMPILER;
      xp->input = mbText(arg);
      break;

   case C_ownsyntax:
      xp->context = EXPAND_OWNSYNTAX;
      xp->input = mbText(arg);
      break;

   case C_packadd:
      xp->context = EXPAND_PACKADD;
      xp->input = mbText(arg);
      break;

   case C_runtime:
      set_context_in_runtime_cmd(xp, arg);
      break;

   case C_language:
      return setContextInLangCommand(xp, arg);
   case C_retab:
      xp->context = EXPAND_RETAB;
      xp->input = mbText(arg);
      break;

   case C_messages:
      xp->context = EXPAND_MESSAGES;
      xp->input = mbText(arg);
      break;

   case C_history:
      xp->context = EXPAND_HISTORY;
      xp->input = mbText(arg);
      break;

   case C_argdelete:
      arg = skipToLastSpace(arg);
      xp->context = EXPAND_ARGLIST;
      xp->input = mbText(arg);
      break;

   case C_breakadd:
   case C_breakdel:
      return set_context_in_breakadd_cmd(xp, arg, id);

   case C_scriptnames:
      return set_context_in_scriptnames_cmd(xp, arg);
   case C_filetype:
      return set_context_in_filetype_cmd(xp, arg);

   default:
      break;
   }
   return NULL;
}

// This is all pretty much copied from doOneCommand(), with all the extra stuff we don't need/want 
// deleted.  Maybe this could be done better if we didn't repeat all this stuff. The only problem 
// is that they may not stay perfectly compatible with each other, but then the command line syntax
// probably won't change that much -- webb.
private CS
set_one_cmd_context(
   OUT Expand   *xp,
   CS buff       // buffer for command string
){
   Byte      *p;
   int len = 0;
   Invocation  invo;
   Unt context = EXPAND_NOTHING;
   Boole forceit = false;
   Boole usefilter = false;  // filter instead of file name

   expandInit(xp);
   xp->files.a = createArena();
   xp->input = mbText(buff);
   xp->fullInput = buff;
   xp->context = EXPAND_COMMANDS;   // Default until we get past command
   invo.argFlags = 0;

   // 1. skip comment lines and leading space, colons or bars
   CS comm;
   for (comm = buff; firstOccurrence((CS)" \t:|", *comm) != NULL; comm++)
      {}
   xp->input = mbText(comm);

   if (*comm == ZERO)
      return NULL;
   if (isComment(comm)) {
      xp->context = EXPAND_NOTHING;
      return NULL;
   }

   // 3. Skip over the range to find the command.
   comm = skip_range(comm, true, &xp->context);
   xp->input = text(comm);
   if (*comm == ZERO)
      return NULL;
   if (*comm == '"') {
      xp->context = EXPAND_NOTHING;
      return NULL;
   }

   if (*comm == '|' || *comm == '\n')
      return comm + 1;         // There's another command

   // Get the command index.
   p = set_cmd_index(comm, &invo, xp, OUT &context);
   if (!p)
      return NULL;

   xp->context = EXPAND_NOTHING; // Default now that we're past command

   if (*p == '!')  {        // forced commands
      forceit = true;
      ++p;
   }

   // 6. parse arguments
   if (!IS_USER_COMMAND(invo.id))
      invo.argFlags = commandGetFlags(invo.id);

   CS arg = skipwhite(p);

   // Does command allow "++argopt" argument?
   if ((invo.argFlags & ARGOPT) || invo.id == C_terminal) {
      while (*arg != ZERO && STRNCMP(arg, "++", 2) == 0) {
         p = arg + 2;
         while (*p && !isSpace(*p))
            MB_PTR_ADV(p);

         // Still touching the command after "++"?
         if (*p == ZERO) {
            if (invo.argFlags & ARGOPT)
               return set_context_in_argopt(xp, arg + 2);
            if (invo.id == C_terminal)
               return set_context_in_terminalopt(xp, arg + 2);
         }

         arg = skipwhite(p);
      }
   }

   if (invo.id == C_write || invo.id == C_update) {
      if (*arg == '>') {       // append
         if (*++arg == '>')
            ++arg;
         arg = skipwhite(arg);
      } ei (*arg == '!' && invo.id == C_write) {  // :w !filter
         ++arg;
         usefilter = true;
      }
   }

   if (invo.id == C_read) {
      usefilter = forceit;         // :r! filter if forced
      if (*arg == '!')  {       // :r !filter
         ++arg;
         usefilter = true;
      }
   }

   if (invo.id == C_lshift || invo.id == C_rshift) {
      while (*arg == *comm)  // allow any number of '>' or '<'
         ++arg;
      arg = skipwhite(arg);
   }

   // Does command allow "+command"?
   if ((invo.argFlags & CMDARG) && !usefilter && *arg == '+') {
      // Check if we're in the +command
      p = arg + 1;
      arg = skip_cmd_arg(arg, false);

      // Still touching the command after '+'?
      if (*arg == ZERO)
         return p;

      // Skip space(s) after +command to get to the real argument
      arg = skipwhite(arg);
   }


   // Check for '|' to separate commands and '"' to start comments.
   // Don't do this for ":read !comm" and ":write !comm".
   if ((invo.argFlags & TRLBAR) && !usefilter) {
      p = arg;
      // ":redir @" is not the start of a comment
      if (invo.id == C_redir && p[0] == '@' && p[1] == '"')
         p += 2;
      while (*p) {
         if (*p == Ctrl_V) {
            if (p[1] != ZERO)
                ++p;
         } ei ( (*p == '"' && !(invo.argFlags & NOTRLCOM)) || *p == '|' || *p == '\n') {
            if (*(p - 1) != '\\') {
               if (*p == '|' || *p == '\n')
                  return p + 1;
               return NULL;    // It's a comment
            }
         }
         MB_PTR_ADV(p);
      }
   }

   if (!(invo.argFlags & EXTRA) && *arg != ZERO && firstOccurrence((CS)"|\"", *arg) == NULL)
      // no arguments allowed but there is something
      return NULL;

   // Find start of last argument (argument just before cursor):
   p = buff;
   xp->input = text(buff);
   len = (int)STRLEN(buff);
   while (*p && p < buff + len) {
      if (*p == ' ' || *p == TAB) {
         // argument starts after a space
         p++;
         xp->input = text(p);
      } else {
         if (*p == '\\' && *(p + 1) != ZERO)
            ++p; // skip over escaped character
         MB_PTR_ADV(p);
      }
   }

   if (invo.argFlags & XFILE)
      set_context_for_wildcard_arg(&invo, arg, usefilter, xp, OUT &context);

   // 6. Switch on command name.
   return setContextByCommandName(comm, invo.id, xp, arg, invo.argFlags, context, forceit);
}

void
setCompletionContextForCommand(
   OUT Expand* xp,
   Text searchPattern,
   int col,        // position of cursor
   Boole use_ccline // use ccline for info
){
   CommlineInfo* ccline = getCommlineInfo();
   Unt context;
   int old_char = ZERO;

   // Avoid a UMR warning from Purify, only save the character if it has been written before.
   if ((Unt)col < searchPattern.len)
      old_char = searchPattern.c[col];
   searchPattern.c[col] = ZERO;
   Arr(Byte) nextComm = searchPattern.c;

   if (use_ccline && ccline->cmdfirstc == '=') {
      // pass COUNT_COMMANDS because there is no real command
      set_context_for_expression(xp, searchPattern.c, COUNT_COMMANDS);
   } ei (use_ccline && ccline->input_fn) {
      xp->context = ccline->context;
      xp->input = mbText(ccline->commBuf);
      xp->completionFn = ccline->completionFn;
      if (xp->context == EXPAND_SHELLCMDLINE) {
         context = xp->context;
         set_context_for_wildcard_arg(NULL, xp->input.c, false, xp, &context);
      }
   } else {
      while (nextComm)
         nextComm = set_one_cmd_context(xp, nextComm);
   } 

   // Store the string here so that call_user_expand_func() can get to them easily.
   xp->fullInput = searchPattern.c;
   xp->xp_col = col;

   searchPattern.c[col] = old_char;
}

//Expand the command line "str" from context "xp". "xp" must have been set by 
//setCompletionContextForCommand(). xp->input points into "str", to where the text that is to be
//expanded starts. Return EXPAND_UNSUCCESSFUL when there is something illegal before the cursor.
//Return EXPAND_NOTHING when there is nothing to expand, might insert the key that triggered 
//expansion literally. Return EXPAND_OK otherwise.
int
expandCommline(
   Expand* xp,
   CS str,      // start of command line
   int col,      // position of cursor
   OUT ExpandMatch* matches
){
   Unt options = WILD_ADD_SLASH|WILD_SILENT;

   if (xp->context == EXPAND_UNSUCCESSFUL) {
      beep_flush();
      return EXPAND_UNSUCCESSFUL;  // Something illegal on command line
   }
   if (xp->context == EXPAND_NOTHING) {
      // Caller can use the character as a normal char instead
      return EXPAND_NOTHING;
   }

   // add star to file name, or convert to regexp if not exp. files.
   xp->input.len = (int)(str + col - xp->input.c);
   CS file_str = NULL;
   if (commlineFuzzyCompletionSupported(xp))
      // If fuzzy matching, don't modify the search string
      file_str = copyStr(xp->input.c);
   else
      file_str = addstar(xp->input, xp->context);

   if (p_wic)
      options += WILD_ICASE;

   // find all files that match the description
   if (expandFromContext(xp, file_str, options, OUT matches) == FAIL) {
      *matches = (ExpandMatch){};
   }
   eeglFree(file_str);

   return EXPAND_OK;
}

// Expand file or directory names. Return OK or FAIL.
private int
expand_files_and_dirs(
   Expand   *xp,
   CS pat,
   int      flags,
   int      options,
   OUT ExpandMatch* matches
) {
   int free_pat = false;
   int ret = FAIL;

   // for ":set path=" and ":set tags=" halve backslashes for escaped space
   if (xp->backslash != XP_BS_NONE) {
      free_pat = true;

      Unt pat_len = STRLEN(pat);
      pat = copySubstr(pat, pat_len);

      CS pat_end = pat + pat_len;
      for (CS p = pat; *p != ZERO; ++p) {
         if (*p != '\\')
            continue;

         CS from;
         if (xp->backslash & XP_BS_THREE
            && *(p + 1) == '\\'
            && *(p + 2) == '\\'
            && *(p + 3) == ' '
         ) {
            from = p + 3;
            MEMMOVE(p, from, (Unt)(pat_end - from) + 1);   // +1 for ZERO
            pat_end -= 3;
         } ei ((xp->backslash & XP_BS_ONE) != 0 && *(p + 1) == ' ') {
            from = p + 1;
            MEMMOVE(p, from, (Unt)(pat_end - from) + 1);   // +1 for ZERO
            --pat_end;
         } ei (xp->backslash & XP_BS_COMMA) {
            if (*(p + 1) == '\\' && *(p + 2) == ',') {
               from = p + 2;
               MEMMOVE(p, from, (Unt)(pat_end - from) + 1);   // +1 for ZERO
               pat_end -= 2;
            }
         }
      }
   }

   if (xp->context == EXPAND_FINDFUNC) {
      ret = expand_findfunc(pat, OUT matches);
   } else {
      if (xp->context == EXPAND_FILES)
         flags |= EW_FILE;
      ei (xp->context == EXPAND_FILES_IN_PATH)
         flags |= (EW_FILE | EW_PATH);
      ei (xp->context == EXPAND_DIRS_IN_CDPATH)
         flags = (flags | EW_DIR | EW_CDPATH) & ~EW_FILE;
      else
         flags = (flags | EW_DIR) & ~EW_FILE;
      if (options & WILD_ICASE)
         flags |= EW_ICASE;

      // Expand wildcards, supporting %:h and the like.
      ret = expand_wildcards_eval(&pat, flags, OUT matches);
   }
   if (free_pat)
      eeglFree(pat);
   return ret;
}

//Function given to expandGeneric() to obtain the possible arguments of the
//":filetype {plugin,indent}" command.
private Byte *
get_filetypecmd_arg(Expand *xp UNUSED, int idx){
   if (idx < 0)
      return NULL;

   if (filetype_expand_what == EXP_FILETYPECMD_ALL && idx < 4) {
      CS opts_all[] = {SMAP((CS), "indent", "plugin", "on", "off" )};
      return (CS)opts_all[idx];
   }
   if (filetype_expand_what == EXP_FILETYPECMD_PLUGIN && idx < 3) {
      CS opts_plugin[] = {SMAP((CS), "plugin", "on", "off" )};
      return (CS)opts_plugin[idx];
   }
   if (filetype_expand_what == EXP_FILETYPECMD_INDENT && idx < 3) {
      CS opts_indent[] = {SMAP((CS), "indent", "on", "off" )};
      return (CS)opts_indent[idx];
   }
   if (filetype_expand_what == EXP_FILETYPECMD_ONOFF && idx < 2) {
      CS opts_onoff[] = {SMAP((CS), "on", "off" )};
      return (CS)opts_onoff[idx];
   }
   return NULL;
}

//Function given to expandGeneric() to obtain the possible arguments of the
//":breakadd {expr, file, func, here}" command.
//":breakdel {func, file, here}" command.
private Byte *
get_breakadd_arg(Expand *xp UNUSED, int idx) {
   if (idx >= 0 && idx <= 3) {
      CS opts[] = {SMAP((CS), "expr", "file", "func", "here" )};

      // breakadd {expr, file, func, here}
      if (breakpt_expand_what == EXP_BREAKPT_ADD)
         return (CS)opts[idx];
      ei (breakpt_expand_what == EXP_BREAKPT_DEL) {
         // breakdel {func, file, here}
         if (idx <= 2)
            return (CS)opts[idx + 1];
      } else {
         // profdel {func, file}
         if (idx <= 1)
            return (CS)opts[idx + 1];
      }
   }
   return NULL;
}

// Function given to expandGeneric() to obtain the possible arguments for the ":scriptnames" command
private Byte *
get_scriptnames_arg(Expand *xp UNUSED, int idx) {
   ScriptItem *si;

   if (!SCRIPT_ID_VALID(idx + 1))
      return NULL;

   si = SCRIPT_ITEM(idx + 1);
   home_replace(si->sn_name, nameBuffG, MAXPATHL, true);
   return nameBuffG;
}

//Function given to expandGeneric() to obtain the possible arguments of the
//":retab {-indentonly}" option.
private CS
get_retab_arg(Expand *xp UNUSED, int idx) {
   if (idx == 0)
      return (CS)"-indentonly";
   return NULL;
}

//Function given to expandGeneric() to obtain the possible arguments of the
//":messages {clear}" command.
private Byte *
get_messages_arg(Expand *xp UNUSED, int idx){
   if (idx == 0)
      return (CS)"clear";
   return NULL;
}

private Byte *
get_mapclear_arg(Expand *xp UNUSED, int idx){
   if (idx == 0)
      return S"<book>";
   return NULL;
}

// Function given to expandGeneric() to obtain an environment variable name.
private CS
getEnvKey(Expand* xp UNUSED, int  idx) {
   extern char** environ;

   CS str = (CS)environ[idx];
   if (!str)
      return NULL;

   int n;
   for (n = 0; n < EXPAND_BUF_LEN - 1; ++n) {
      if (str[n] == '=' || str[n] == ZERO)
         break;
      xp->matchBuilder[n] = str[n];
   }
   xp->matchBuilder[n] = ZERO;
   return xp->matchBuilder;
}

// Do the expansion based on xp->context and 'rmp'.
private int
expandOther(
   CS pat,
   Expand* xp,
   RegMatch* rmp,
   OUT ExpandMatch* matches
){
   static struct expgen {
      Unt      context;
      Byte   *((*func)(Expand *, int));
      int      ic;
      int      escaped;
   } tab[] = {
      {EXPAND_COMMANDS, get_command_name, false, true},
      {EXPAND_FILETYPECMD, get_filetypecmd_arg, true, true},
      {EXPAND_MAPCLEAR, get_mapclear_arg, true, true},
      {EXPAND_MESSAGES, get_messages_arg, true, true},
      {EXPAND_HISTORY, get_history_arg, true, true},
      {EXPAND_USER_COMMANDS, get_user_commands, false, true},
      {EXPAND_USER_ADDR_TYPE, get_user_cmd_addr_type, false, true},
      {EXPAND_USER_CMD_FLAGS, get_user_cmd_flags, false, true},
      {EXPAND_USER_NARGS, get_user_cmd_nargs, false, true},
      {EXPAND_USER_COMPLETE, get_user_cmd_complete, false, true},
      {EXPAND_USER_VARS, get_user_var_name, false, true},
      {EXPAND_FUNCTIONS, get_function_name, false, true},
      {EXPAND_USER_FUNC, get_user_func_name, false, true},
      {EXPAND_DISASSEMBLE, get_disassemble_argument, false, true},
      {EXPAND_EXPRESSION, get_expr_name, false, true},
      {EXPAND_SYNTAX, get_syntax_name, true, true},
      {EXPAND_HILITE_GROUP, getHiliteGroupNameAsCString, true, true},
      {EXPAND_EVENTS, get_event_name, true, false},
      {EXPAND_AUGROUP, get_augroup_name, true, false},
      {EXPAND_CSCOPE, get_cscope_name, true, true},
      {EXPAND_SIGN, get_sign_name, true, true},
      {EXPAND_LANGUAGE, get_lang_arg, true, false},
      {EXPAND_LOCALES, get_locales, true, false},
      {EXPAND_ENV_VARS, getEnvKey, true, true},
      {EXPAND_USER, get_users, true, false},
      {EXPAND_ARGLIST, get_arglist_name, true, false},
      {EXPAND_BREAKPOINT, get_breakadd_arg, true, true},
      {EXPAND_SCRIPTNAMES, get_scriptnames_arg, true, false},
      {EXPAND_RETAB, get_retab_arg, true, true},
   };
   int   i;
   int ret = FAIL;

   // Find a context in the table and call the expandGeneric() with the
   // right function to do the expansion.
   for (i = 0; i < (int)ARRAY_LENGTH(tab); ++i) {
      if (xp->context == tab[i].context) {
         if (tab[i].ic)
            rmp->rm_ic = true;
         ret = expandGeneric(pat, xp, rmp, tab[i].func, tab[i].escaped, OUT matches);
         break;
      }
   }

   return ret;
}

// Map wild expand options to flags for expand_wildcards()
private Unt
map_wildopts_to_ewflags(Unt options) {
   Unt flags = EW_DIR;   // include directories
   if (options & WILD_LIST_NOTFOUND)
      flags |= EW_NOTFOUND;
   if (options & WILD_ADD_SLASH)
      flags |= EW_ADDSLASH;
   if (options & WILD_KEEP_ALL)
      flags |= EW_KEEPALL;
   if (options & WILD_SILENT)
      flags |= EW_SILENT;
   if (options & WILD_NOERROR)
      flags |= EW_NOERROR;
   if (options & WILD_ALLLINKS)
      flags |= EW_ALLLINKS;

   return flags;
}

// Do the expansion based on xp->context and "pat".
int
expandFromContext(
   Expand   *xp,
   CS pat,
   Unt options, // WILD_ flags
   OUT ExpandMatch* matches
){
   RegMatch   regmatch;
   int      ret;
   CS tofree = NULL;
   Boole doFuzzy = scrIsCommlineFuzzyCompletable(pat) && commlineFuzzyCompletionSupported(xp);

   Unt flags = map_wildopts_to_ewflags(options);

   if (xp->context == EXPAND_FILES
         || xp->context == EXPAND_DIRECTORIES
         || xp->context == EXPAND_FILES_IN_PATH
         || xp->context == EXPAND_FINDFUNC
         || xp->context == EXPAND_DIRS_IN_CDPATH
   )
      return expand_files_and_dirs(xp, pat, flags, options, OUT matches);
      
   if (xp->context == EXPAND_HELP) {
      //With an empty argument we would get all the help tags, which is
      //very slow. Get matches for "help" instead.
      if (find_help_tags(*pat == ZERO ? S"help" : pat, false, OUT matches) == OK) {
          cleanup_help_tags(OUT matches);
          return OK;
      }
      return FAIL;
   }

   if (xp->context == EXPAND_SHELLCMD)
      return expandShellCommand(pat, flags, OUT matches);
   if (xp->context == EXPAND_OLD_OPTION)
      return optExpandOldOption(OUT matches);
   if (xp->context == EXPAND_BUFFERS)
      return bufExpandBufnames(pat, options, OUT matches);
   if (xp->context == EXPAND_DIFF_BUFFERS)
      return bufExpandBufnames(pat, options | BOOK_DIFF_FILTER, OUT matches);
   if (xp->context == EXPAND_TAGS || xp->context == EXPAND_TAGS_LISTFILES)
      return expand_tags(xp->context == EXPAND_TAGS, pat, OUT matches);
   if (xp->context == EXPAND_COLORS) {
      CS directories[] = {S"colors", NULL};
      return expandRuntimeDir(pat, DIP_START + DIP_OPT, directories, OUT matches);
   }
   if (xp->context == EXPAND_COMPILER) {
      CS directories[] = {S"compiler", NULL};
      return expandRuntimeDir(pat, 0, directories, OUT matches);
   }
   if (xp->context == EXPAND_OWNSYNTAX) {
      CS directories[] = {S"syntax", NULL};
      return expandRuntimeDir(pat, 0, directories, OUT matches);
   }
   if (xp->context == EXPAND_FILETYPE) {
      CS directories[] = {S"syntax", S"indent", S"ftplugin", NULL};
      return expandRuntimeDir(pat, 0, directories, OUT matches);
   }
   if (xp->context == EXPAND_KEYMAP) {
      CS directories[] = {S"keymap", NULL};
      return expandRuntimeDir(pat, 0, directories, OUT matches);
   }
   if (xp->context == EXPAND_USER_LIST)
      return expandUserList(xp, OUT matches);
   if (xp->context == EXPAND_PACKADD)
      return expandPackAddDir(pat, OUT matches);
   if (xp->context == EXPAND_RUNTIME)
      return expand_runtime_cmd(pat, OUT matches);
   if (xp->context == EXPAND_PATTERN_IN_BUF)
      return expandPatternInBook(pat, xp->searchDirection, OUT matches);

   // When expanding a function name starting with s:, match the <SNR>nr_ prefix.
   if ((xp->context == EXPAND_USER_FUNC || xp->context == EXPAND_DISASSEMBLE)
       && STRNCMP(pat, "^s:", 3) == 0
   ) {
      int len = (int)STRLEN(pat) + 20;

      tofree = alloc(len);
      eeSnprintf(tofree, len, "^<SNR>\\d\\+_%s", pat + 3);
      pat = tofree;
   }

   if (!doFuzzy) {
      regmatch.regprog = compileRegexp(pat, RE_MAGIC);
      if (regmatch.regprog == NULL)
          return FAIL;

      // set ignore-case according to p_ic, p_scs and pat
      regmatch.rm_ic = ignorecase(pat);
   }

   if (xp->context == EXPAND_OPTION)
      ret = optExpandOption(xp, &regmatch, pat, doFuzzy, OUT matches);
   ei (xp->context == EXPAND_STRING_OPTION)
      ret = optExpandForSet(xp, &regmatch, OUT matches);
   ei (xp->context == EXPAND_MAPPINGS)
      ret = expandMappings(pat, &regmatch, OUT matches);
   ei (xp->context == EXPAND_ARGOPT)
      ret = expand_argopt(pat, xp, &regmatch, OUT matches);
   ei (xp->context == EXPAND_HILITE_GROUP)
      ret = expandHiliteGroup(pat, xp, &regmatch, OUT matches);
   ei (xp->context == EXPAND_TERMINALOPT)
      ret = expand_terminal_opt(pat, xp, &regmatch, OUT matches);
   ei (xp->context == EXPAND_USER_DEFINED)
      ret = expandUserDefined(pat, xp, &regmatch, OUT matches);
   else
      ret = expandOther(pat, xp, &regmatch, OUT matches);

   if (!doFuzzy)
      eeRegFree(regmatch.regprog);
   eeglFree(tofree);

   return ret;
}

//Expand a list of names.
//
//Generic function for command line completion. It calls a function to obtain strings, one by one.
//The strings are matched against a regexp program. Matching strings are copied into an array, 
//which is returned.
//If 'doFuzzy' is true, then fuzzy matching is used. Otherwise, regex matching is used.
//
//'sortStartIdx' allows the caller to control sorting behavior. Items before the index will not be
//sorted. Pass 0 to sort all, and -1 to prevent any sorting.
//
//Return OK when no problems encountered, FAIL for error (out of memory).
private int
expandGenericExt(
   CS pat,
   Expand* xp,
   RegMatch* regmatch,
   CS (*fn)(Expand *, int), // return a string from the list
   int escaped,
   int sortStartIdx,
   OUT ExpandMatch* matches
) {
   int i;
   ArrayList ga;
   FuzzyMatch* fuzmatch = NULL;
   int score = 0;
   int match;
   Boole sortTheMatches = false;
   int funcsort = false;
   int sortStartMatchIdx = -1;

   Boole doFuzzy = scrIsCommlineFuzzyCompletable(pat);
   Fuzzy fuzzy = {.len = 0, .cap = 0, .a = matches->a};

   for (i = 0; ; ++i) {
      CS str0 = (*fn)(xp, i);
      if (!str0)       // end of list
         break;
      Text str = (Text){str0, STRLEN(str0)};

      if (xp->input.c[0] != ZERO) {
         if (doFuzzy) {
            score = fuzzyMatchStr(str.c, pat);
            match = (score != FUZZY_SCORE_NONE);
         } else {
            match = eeRegexec(regmatch, str.c, (ColNr)0);
         }
      } else
         match = true;

      if (!match)
         continue;

      if (escaped)
         str.c = copyStr_escaped(str.c, (CS)" \t\\.");
      else
         str.c = copyStr(str.c);

      if (ga_grow(&ga, 1) == FAIL) {
         eeglFree(str.c);
         break;
      }

      if (doFuzzy) {
         fuzmatch = &((FuzzyMatch *)ga.c)[ga.len];
         fuzmatch->idx = ga.len;
         fuzmatch->str = str.c;
         fuzmatch->score = score;
      } else
         ((CS*)ga.c)[ga.len] = str.c;


      if (sortStartIdx >= 0 && i >= sortStartIdx && sortStartMatchIdx == -1) {
         // Found first item to start sorting from. This is usually 0.
         sortStartMatchIdx = ga.len;
      }

      ++ga.len;
   }

   if (ga.len == 0)
      return OK;

   // sort the matches when using regular expression matching and sorting applies to the completion
   // context. Menus and scriptnames should be kept in the specified order.
   if (!doFuzzy && xp->context != EXPAND_MENUNAMES
              && xp->context != EXPAND_STRING_OPTION
              && xp->context != EXPAND_MENUS
              && xp->context != EXPAND_SCRIPTNAMES
              && xp->context != EXPAND_ARGOPT
              && xp->context != EXPAND_TERMINALOPT)
      sortTheMatches = true;

   // <SNR> functions should be sorted to the end.
   if (xp->context == EXPAND_EXPRESSION
       || xp->context == EXPAND_FUNCTIONS
       || xp->context == EXPAND_USER_FUNC
       || xp->context == EXPAND_DISASSEMBLE)
      funcsort = true;

   // Sort the matches.
   if (sortTheMatches && sortStartMatchIdx != -1) {
      if (funcsort)
         // <SNR> functions should be sorted to the end.
         qsort((void *)ga.c, (Unt)ga.len, sizeof(CS), sort_func_compare);
      else
         sortStrings((Byte **)ga.c + sortStartMatchIdx, ga.len - sortStartMatchIdx);
   }

   if (!doFuzzy) {
      *matches = expandMatchOfArrayList(ga);
   } else {
      if (defuzz(OUT matches, fuzzy, funcsort) == FAIL)
         return FAIL;
      matches->len = ga.len;
   }

   // Reset the variables used for special highlight names expansion, so that
   // they don't show up when getting normal highlight names by ID.
   reset_expand_highlight();
   return OK;
}

int
expandGeneric(
   CS pat,
   Expand* xp,
   RegMatch* regmatch,
   CS (*fn)(Expand *, int), // return a string from the list
   int      escaped,
   OUT ExpandMatch* matches
){
   return expandGenericExt(pat, xp, regmatch, fn, escaped, 0, OUT matches);
}


//Expand shell command matches in one directory of $PATH.
private void
expandShellCommand_onedir(
   CS pathed_pattern,    // fully pathed pattern
   Unt pathlen, // length of the path portion of pathed_pattern (0 if no path).
   Unt flags,
   EeSet* ht,
   OUT ExpandMatch* matches
){
   // Expand matches in one directory of $PATH.
   if (expand_wildcards(1, &pathed_pattern, flags, OUT matches) != OK)    
      return;

   for (Unt i = 0; i < matches->len; ++i) {
      CS name = matches->c[i];
      Unt   namelen = STRLEN(name);

      if (namelen > pathlen) {
         //Check if this name has been found.
         Text t = text(name + pathlen);
         Hash hash = calcHash(t);
         EeSetItem* hi = hash_lookup(ht, t, hash);
         if (HASHITEM_EMPTY(hi)) {
            // Remove the path that was prepended.
            MEMMOVE(name, name + pathlen, (Unt)(namelen - pathlen) + 1); // +1 for ZERO
            addExpandMatch(name, matches);
            hash_add_item(ht, hi, text(name), hash);
            name = NULL;
         }
      }
      eeglFree(name);
   }
}

// Complete a shell command. Return FAIL or OK;
private int
expandShellCommand(
   CS filepat,   // pattern to match with command names
   Unt flagsarg,   // EW_ flags
   OUT ExpandMatch* matches
){
   CS path = NULL;
   Boole mustfree = false;
   Byte   *s, *e;
   Unt flags = flagsarg;
   Boole didCurrDir = false;

   Byte builder[MAXPATHL];

   // for ":set path=" and ":set tags=" halve backslashes for escaped space
   int patlen = STRLEN(filepat);
   CS pat = copySubstr(filepat, patlen);

   // Replace "\ " with " ".
   e = pat + patlen;
   for (s = pat; *s != ZERO; ++s) {

      if (*s != '\\')
         continue;

      CS p = s + 1;
      if (*p == ' ') {
         MEMMOVE(s, p, (Unt)(e - p) + 1);     // +1 for ZERO
         --e;
      }
   }
   patlen = (Unt)(e - pat);

   flags |= EW_FILE | EW_EXEC | EW_SHELLCMD;

   if (pat[0] == '.' && (pat[1] == '/' || (pat[1] == '.' && pat[2] == '/')))
      path = S".";
   else {
      //For an absolute name we don't use $PATH.
      if (strIsRelative(pat))
         path = eeglGetEnv(S"PATH");
      if (!path)
         path = S"";
   }

   //Go over all directories in $PATH. Expand matches in that directory and
   //collect them in "matches". When "." is not in $PATH also expand for the
   //current directory, to find "subdir/comm".
   
   EeSet found_ht;
   hash_init(&found_ht);
   for (s = path; ; s = e) {
      Unt pathlen;   // length of the path portion of builder (including trailing slash)
      Unt seplen;

      if (*s == ZERO) {
         if (didCurrDir)
            break;

         // Find directories in the current directory, path is empty.
         didCurrDir = true;
         flags |= EW_DIR;

         e = s;
         pathlen = 0;
         seplen = 0;
      } else {
         e = firstOccurrence(s, ':');
         if (e == NULL)
            e = s + STRLEN(s);

         pathlen = (Unt)(e - s);
         if (STRNCMP(s, ".", pathlen) == 0) {
            didCurrDir = true;
            flags |= EW_DIR;
         } else
            // Do not match directories inside a $PATH item.
            flags &= ~EW_DIR;

         seplen = !after_pathsep(s, e) ? 1 : 0;
      }

      // Make sure that the pathed pattern (ie the path and pattern concatenated
      // together) will fit inside the buffer. If not skip it and move on to the next path.
      if (pathlen + seplen + patlen + 1 <= MAXPATHL) {
         if (pathlen > 0) {
            copySubstrToAllocation(builder, (Text){s, pathlen});
            if (seplen > 0) {
               builder[pathlen] = '/';
               pathlen++;
            }
         }
         STRCPY(builder + pathlen, pat);

         expandShellCommand_onedir(builder, pathlen, flags, &found_ht, OUT matches);
      }

      if (*e != ZERO)
          ++e;
   }

   eeglFree(pat);
   if (mustfree)
      eeglFree(path);
   hash_clear(&found_ht);
   return OK;
}

//Call "user_expand_func()" to invoke a user defined Vim script function and
//return the result (either a string, a List or NULL).
private void *
call_user_expand_func( void   *(*user_expand_func)(Byte *, int, Var *), Expand   *xp) {
   CommlineInfo   *ccline = getCommlineInfo();
   int      keep = 0;
   Var   args[4];
   ScriptPos   save_scriptPosG = scriptPosG;
   Byte   *pat = NULL;
   void   *ret;

   if (xp->completionFn == NULL || xp->completionFn[0] == '\0' || xp->fullInput == NULL)
      return NULL;

   if (ccline->commBuf != NULL) {
      keep = ccline->commBuf[ccline->cmdlen];
      ccline->commBuf[ccline->cmdlen] = 0;
   }

   pat = copySubstr(xp->input.c, xp->input.len);

   args[0].tag = VAR_STRING;
   args[0].string = pat;
   args[1].tag = VAR_STRING;
   args[1].string = xp->fullInput;
   args[2].tag = VAR_NUMBER;
   args[2].number = xp->xp_col;
   args[3].tag = VAR_UNKNOWN;

   scriptPosG = xp->scriptCtx;

   ret = user_expand_func(xp->completionFn, 3, args);

   scriptPosG = save_scriptPosG;
   if (ccline->commBuf != NULL)
      ccline->commBuf[ccline->cmdlen] = keep;

   eeglFree(pat);
   return ret;
}

// Expand names with a function defined by the user (EXPAND_USER_DEFINED and EXPAND_USER_LIST).
private int
expandUserDefined(
   CS pat,
   Expand* xp,
   RegMatch* regmatch,
   OUT ExpandMatch* matches
) {
   Byte   *s;
   Byte   *e;
   int      keep;
   Boole match;
   int      score = 0;

   Boole doFuzzy = scrIsCommlineFuzzyCompletable(pat);
   Fuzzy fuzzy = {.c = null, .len = 0, .cap = 0, .a = matches->a};

   CS retstr = call_user_expand_func(call_func_retstr, xp);
   if (!retstr)
      return FAIL;

   for (s = retstr; *s != ZERO; s = e) {
      e = firstOccurrence(s, '\n');
      if (e == NULL)
          e = s + STRLEN(s);
      keep = *e;
      *e = ZERO;

      if (xp->input.c[0] != ZERO) {
         if (doFuzzy) {
            score = fuzzyMatchStr(s, pat);
            match = (score != FUZZY_SCORE_NONE);
         } else {
            match = eeRegexec(regmatch, s, (ColNr)0);
         }
      } else
         match = true;      // match everything

      *e = keep;

      if (match) {
         CS p = copySubstrA(s, (Unt)(e - s), matches->a);
         if (doFuzzy) {
            addFuzzyMatch((FuzzyMatch){.score = score, .str = p}, OUT &fuzzy);
         } else {
            addExpandMatch(p, OUT matches);
         }
      }

      if (*e != ZERO)
         ++e;
   }
   eeglFree(retstr);

   if (matches->len == 0)
      return OK;

   if (doFuzzy) {
      if (defuzz(OUT matches, fuzzy, false) == FAIL)
         return FAIL;
   }
   return OK;
}

// Expand names with a list returned by a function defined by the user.
private int
expandUserList(
   Expand* xp,
   OUT ExpandMatch* matches
){

   List* retlist = call_user_expand_func(call_func_retlist, xp);
   if (!retlist)
      return FAIL;

   // Loop over the items in the list.
   ListItem* li;
   FOR_ALL_LIST_ITEMS(retlist, li) {
      if (li->c.tag != VAR_STRING || li->c.string == NULL)
          continue;  // Skip non-string items and empty strings

      addExpandMatch(copyStrA(li->c.string, matches->a), matches);
   }
   list_unref(retlist);
   return OK;
}

// Translate some keys pressed when @wildmenu is used.
private int
wildmenu_translate_key(
   CommlineInfo   *cclp,
   Unt      key,
   Expand   *xp,
   int      did_wild_list
) {
   Unt c = key;

   if (cmdline_pum_active()) {
      // When the popup menu is used for cmdline completion:
      //   Up     : go to the previous item in the menu
      //   Down : go to the next item in the menu
      //   Left : go to the parent directory
      //   Right: list the files in the selected directory
      switch (c) {
          case K_UP:     c = K_LEFT; break;
          case K_DOWN:  c = K_RIGHT; break;
          case K_LEFT:  c = K_UP; break;
          case K_RIGHT: c = K_DOWN; break;
          default:     break;
      }
   }

   if (did_wild_list) {
   if (c == K_LEFT)
       c = Ctrl_P;
   ei (c == K_RIGHT)
       c = Ctrl_N;
   }

   // Hitting CR after "emenu Name.": complete submenu
   if (xp->context == EXPAND_MENUNAMES
         && cclp->cmdpos > 1
         && cclp->commBuf[cclp->cmdpos - 1] == '.'
         && cclp->commBuf[cclp->cmdpos - 2] != '\\'
         && (c == '\n' || c == '\r' || c == K_KENTER))
      c = K_DOWN;

   return c;
}

// Delete characters on the command line, from "from" to the current position.
private void
cmdline_del(CommlineInfo *cclp, int from){
   MEMMOVE(cclp->commBuf + from, cclp->commBuf + cclp->cmdpos,
      (Unt)(cclp->cmdlen - cclp->cmdpos + 1));
   cclp->cmdlen -= cclp->cmdpos - from;
   cclp->cmdpos = from;
}

// Handle a key pressed when the wild menu for the menu names (EXPAND_MENUNAMES) is displayed.
private int
wildmenu_process_key_menunames(CommlineInfo *cclp, Unt key, Expand *xp){
   // Hitting <Down> after "emenu Name.": complete submenu
   if (key == K_DOWN && cclp->cmdpos > 0 && cclp->commBuf[cclp->cmdpos - 1] == '.') {
      key = p_wc;
      keyWasTypedG = true;  // in case the key was mapped
   } ei (key == K_UP) {
      // Hitting <Up>: Remove one submenu name in front of the
      // cursor
      int found = false;

      int i = 0;
      int j = (int)(xp->input.c - cclp->commBuf);
      while (--j > 0) {
          // check for start of menu name
          if (cclp->commBuf[j] == ' ' && cclp->commBuf[j - 1] != '\\') {
            i = j + 1;
            break;
         }
         // check for start of submenu name
         if (cclp->commBuf[j] == '.' && cclp->commBuf[j - 1] != '\\') {
            if (found) {
               i = j + 1;
               break;
            } else
               found = true;
         }
      }
      if (i > 0)
          cmdline_del(cclp, i);
      key = p_wc;
      keyWasTypedG = true;  // in case the key was mapped
      xp->context = EXPAND_NOTHING;
    }

    return key;
}

//Handle a key pressed when the wild menu for file names (EXPAND_FILES) or directory names 
//(EXPAND_DIRECTORIES) or shell command names (EXPAND_SHELLCMD) is displayed.
private int
wildmenu_process_key_filenames(CommlineInfo *cclp, Unt key, Expand *xp){
   int      i;
   int      j;
   Byte   upseg[5] = {'/', '.', '.', '/', ZERO};

   if (key == K_DOWN
       && cclp->cmdpos > 0
       && cclp->commBuf[cclp->cmdpos - 1] == '/'
       && (cclp->cmdpos < 3
         || cclp->commBuf[cclp->cmdpos - 2] != '.'
         || cclp->commBuf[cclp->cmdpos - 3] != '.')
   ) {
      // go down a directory
      key = p_wc;
      keyWasTypedG = true;  // in case the key was mapped
   } ei (STRNCMP(xp->input.c, upseg + 1, 3) == 0 && key == K_DOWN) {
      // If in a direct ancestor, strip off one ../ to go down
      int found = false;

      j = cclp->cmdpos;
      i = (int)(xp->input.c - cclp->commBuf);
      while (--j > i) {
         j -= (*mb_head_off)(cclp->commBuf, cclp->commBuf + j);
         if (cclp->commBuf[j] == '/') {
            found = true;
            break;
         }
      }
      if (found
         && cclp->commBuf[j - 1] == '.'
         && cclp->commBuf[j - 2] == '.'
         && (cclp->commBuf[j - 3] == '/' || j == i + 2)
      ){
          cmdline_del(cclp, j - 2);
          key = p_wc;
          keyWasTypedG = true;  // in case the key was mapped
      }
   } ei (key == K_UP) {
      // go up a directory
      int found = false;

      j = cclp->cmdpos - 1;
      i = (int)(xp->input.c - cclp->commBuf);
      while (--j > i) {
         j -= (*mb_head_off)(cclp->commBuf, cclp->commBuf + j);
         if (cclp->commBuf[j] == '/') {
            if (found) {
               i = j + 1;
               break;
            } else
               found = true;
         }
      }

      if (!found)
         j = i;
      ei (STRNCMP(cclp->commBuf + j, upseg, 4) == 0)
         j += 4;
      ei (STRNCMP(cclp->commBuf + j, upseg + 1, 3) == 0 && j == i)
         j += 3;
      else
         j = 0;
      if (j > 0) {
         cmdline_del(cclp, j);
         put_on_cmdline(upseg + 1, 3, false);
      } ei (cclp->cmdpos > i)
         cmdline_del(cclp, i);

      // Now complete in the new directory. Set keyWasTypedG in case the Up key came from a mapping.
      key = p_wc;
      keyWasTypedG = true;
   }

   return key;
}

// Handle a key pressed when the wild menu is displayed
int
wildmenu_process_key(CommlineInfo *cclp, Unt key, Expand *xp) {
   if (xp->context == EXPAND_MENUNAMES)
      return wildmenu_process_key_menunames(cclp, key, xp);
   ei ((xp->context == EXPAND_FILES
         || xp->context == EXPAND_DIRECTORIES
         || xp->context == EXPAND_SHELLCMD))
      return wildmenu_process_key_filenames(cclp, key, xp);

   return key;
}

// Free expanded names when finished walking through the matches
void
wildmenu_cleanup(CommlineInfo *cclp UNUSED) {
   int skt = keyWasTypedG;

   if (!p_wmnu || wild_menu_showing == 0)
      return;

   int save_isRedrawingDisabledG = isRedrawingDisabledG;
   if (cclp->input_fn)
      isRedrawingDisabledG = 0;

   // Clear hiliting applied during wildmenu activity
   setHlsearch(false);

   if (wild_menu_showing == WM_SCROLLED) {
      // Entered command line, move it up
      commlineRowG--;
      redrawcmd();
   } else {
      // restore 'laststatus' and 'winminheight'
      last_status(false);
      drawUpdateScreen(UPD_VALID);   // redraw the screen NOW
      redrawcmd();
   }
   keyWasTypedG = skt;
   wild_menu_showing = 0;
   if (cclp->input_fn)
      isRedrawingDisabledG = save_isRedrawingDisabledG;
}

void
f_getcompletion(Arr(Var) argvars, Var* returnVar) {
   int filtered = false;
   int options = WILD_SILENT | WILD_USE_NL | WILD_ADD_SLASH | WILD_NO_BEEP | WILD_HOME_REPLACE;

   CS input = tv_get_string(&argvars[0]);
   if (check_for_string_arg(argvars, 1) == FAIL)
      return;
   CS type = tv_get_string(&argvars[1]);

   if (argvars[2].tag != VAR_UNKNOWN)
      filtered = varGetNumberChk(argvars + 2, NULL);

   if (p_wic)
      options |= WILD_ICASE;

   // For filtered results, @wildignore is used
   if (!filtered)
      options |= WILD_KEEP_ALL;

   Expand xp;
   expandInit(&xp);
   if (STRCMP(type, "cmdline") == 0) {
      int commlineLen = (int)STRLEN(input);
      setCompletionContextForCommand(OUT &xp, (Text){input, commlineLen}, commlineLen, false);
      xp.input.len = (int)STRLEN(xp.input.c);
      xp.xp_col = commlineLen;
   } else {
      CS pattern_start = input;

      xp.input = mbText(input);
      xp.fullInput = input;

      xp.context = cmdcomplete_str_to_type(type);
      switch (xp.context) {
      case EXPAND_NOTHING:
         showErrFmtMsg(_(e_invalid_argument_str), type);
         return;

      case EXPAND_USER_DEFINED:
         // Must be "custom,funcname" pattern
         if (STRNCMP(type, "custom,", 7) != 0) {
            showErrFmtMsg(_(e_invalid_argument_str), type);
            return;
         }

         xp.completionFn = type + 7;
         break;

      case EXPAND_USER_LIST:
         // Must be "customlist,funcname" pattern
         if (STRNCMP(type, "customlist,", 11) != 0) {
            showErrFmtMsg(_(e_invalid_argument_str), type);
            return;
         }

         xp.completionFn = type + 11;
         break;

      case EXPAND_CSCOPE:
         set_context_in_cscope_cmd(&xp, xp.input.c, C_cscope);
         xp.input.len -= (int)(xp.input.c - pattern_start);
         break;

      case EXPAND_SIGN:
         set_context_in_sign_cmd(&xp, xp.input.c);
         xp.input.len -= (int)(xp.input.c - pattern_start);
         break;

      case EXPAND_RUNTIME:
         set_context_in_runtime_cmd(&xp, xp.input.c);
         xp.input.len -= (int)(xp.input.c - pattern_start);
         break;

      case EXPAND_SHELLCMDLINE: {
         Unt context = EXPAND_SHELLCMDLINE;
         set_context_for_wildcard_arg(NULL, xp.input.c, false, &xp, &context);
         xp.input.len -= (int)(xp.input.c - pattern_start);
         break;
      }

      case EXPAND_FILETYPECMD:
          filetype_expand_what = EXP_FILETYPECMD_ALL;
          break;

      default:
          break;
      }
   }

   CS pat;
   if (commlineFuzzyCompletionSupported(&xp))
      // when fuzzy matching, don't modify the search string
      pat = copySubstr(xp.input.c, xp.input.len);
   else
      pat = addstar(xp.input, xp.context);

   allocReturnList(returnVar);
   if (pat) {
      expandWildcard(OUT &xp, pat, NULL, options, WILD_ALL_KEEP);

      for (Unt i = 0; i < xp.files.len; i++)
         list_append_string(returnVar->list, xp.files.c[i], -1);
   }
   eeglFree(pat);
   scrExpandCleanup(&xp);
}

void
f_getcompletiontype(Arr(Var) argvars, Var* returnVar){

   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   if (check_for_string_arg(argvars, 0) == FAIL)
      return;

   CS pat = tv_get_string(&argvars[0]);
   Expand   xp;
   expandInit(&xp);

   int commLineLen = (int)STRLEN(pat);
   setCompletionContextForCommand(OUT &xp, (Text){pat, commLineLen}, commLineLen, false);
   returnVar->string = cmdcomplete_type_to_str(xp.context, xp.completionFn);

   scrExpandCleanup(&xp);
}

void
f_cmdcomplete_info(Arr(Var) argvars UNUSED, Var* returnVar) {
   CommlineInfo  *ccline = getCommlineInfo();
   allocReturnDict(returnVar);
   if (!ccline || !ccline->xpc || !(ccline->xpc->files.c))
      return;
   Bag* retdict = returnVar->bag;
   int ret = bagAddString(retdict, S"commlineSaved", commlineSaved);
   if (ret == OK)
      ret = bagAddNumber(retdict, S"pum_visible", pum_visible());
   if (ret == OK)
      ret = bagAddNumber(retdict, S"selected", ccline->xpc->xp_selected);
   if (ret == OK) {
      List* l = list_alloc();
      ret = bagAddList(retdict, S"matches", l);
      for (Unt idx = 0; ret == OK && idx < ccline->xpc->files.len; idx++)
          list_append_string(l, ccline->xpc->files.c[idx], -1);
   }
}

//Copy a substring from the current buffer (curBook), spanning from the given
//'start' position to the word boundary after 'end' position.
//The copied string is stored in '*match', and the actual end position of the
//matched text is returned in '*match_end'.
private int
copy_substring_from_pos(Pos *start, Pos *end, Byte **match, Pos *match_end) {
   Byte   *word_end;
   Byte   *line, *start_line, *end_line;
   int      segment_len;
   LineNr   lnum;
   ArrayList ga;
   int exacttext = (p_wop & WILDOPT_EXACT) != 0;

   if (start->lnum > end->lnum || (start->lnum == end->lnum && start->col >= end->col))
      return FAIL; // invalid range

   // Use a growable string (ga)
   ga_init2(&ga, 1, 128);

   // Append start line from start->col to end
   start_line = ml_get(start->lnum);
   Byte  *start_ptr = start_line + start->col;
   int       is_single_line = start->lnum == end->lnum;

   segment_len = is_single_line ? (end->col - start->col)
         : (int)(ml_get_len(start->lnum) - start->col);
   if (ga_grow(&ga, segment_len + 2) != OK)
      return FAIL;

   ga_concat_len(&ga, start_ptr, segment_len);
   if (!is_single_line) {
      if (exacttext)
          ga_concat_len(&ga, (CS)"\\n", 2);
      else
          ga_append(&ga, '\n');
   }

   // Append full lines between start and end
   if (!is_single_line) {
      for (lnum = start->lnum + 1; lnum < end->lnum; lnum++) {
          line = ml_get(lnum);
          if (ga_grow(&ga, ml_get_len(lnum) + 2) != OK)
         return FAIL;
          ga_concat(&ga, line);
          if (exacttext)
         ga_concat_len(&ga, (CS)"\\n", 2);
          else
         ga_append(&ga, '\n');
      }
   }

   // Append partial end line (up to word end)
   end_line = ml_get(end->lnum);
   word_end = find_word_end(end_line + end->col);
   segment_len = (int)(word_end - end_line);
   if (ga_grow(&ga, segment_len) != OK)
      return FAIL;
   ga_concat_len(&ga, end_line + (is_single_line ? end->col : 0),
       segment_len - (is_single_line ? end->col : 0));

   // Null-terminate
   if (ga_grow(&ga, 1) != OK)
      return FAIL;
   ga_append(&ga, ZERO);

   *match = (CS)ga.c;
   match_end->lnum = end->lnum;
   match_end->col = segment_len;

   return OK;
}

//Return true if the given string `str` matches the regex pattern `pat`.
//Honor the @ignorecase (p_ic) and @smartcase (p_scs) options to determine case sensitivity.
private int
is_regex_match(Byte *pat, Byte *str) {
   if (STRCMP(pat, str) == 0)
      return true;

   RegMatch   regmatch;
   int result;

   ++emsg_off;
   ++msg_silent;
   regmatch.regprog = compileRegexp(pat, RE_MAGIC + RE_STRING);
   --emsg_off;
   --msg_silent;

   if (regmatch.regprog == NULL)
      return false;
   regmatch.rm_ic = p_ic;
   if (p_ic && p_scs)
      regmatch.rm_ic = !pat_has_uppercase(pat);

   ++emsg_off;
   ++msg_silent;
   result = eeRegexec_nl(&regmatch, str, (ColNr)0);
   --emsg_off;
   --msg_silent;

   eeRegFree(regmatch.regprog);
   return result;
}

//Construct a new match string by appending text from the buffer (starting at end_match_pos) to the
//given pattern `pat`. The result is a concatenation of `pat` and the word following end_match_pos.
//If 'lowercase' is true, the appended text is converted to lowercase before being combined. 
//Return the newly allocated match string, or NULL on failure.
private CS
concat_pattern_with_buffer_match(Text pat, Pos* end_match_pos, Boole lowercase) {
   CS line = ml_get(end_match_pos->lnum);
   CS word_end = find_word_end(line + end_match_pos->col);
   int match_len = (int)(word_end - (line + end_match_pos->col));
   CS match = alloc(match_len + pat.len + 1);  // +1 for ZERO
   MEMMOVE(match, pat.c, pat.len);
   if (match_len > 0) {
      if (lowercase) {
          Byte  *mword = copySubstr(line + end_match_pos->col,
             match_len);
          if (mword == NULL)
         goto cleanup;
          Byte  *lower = strlow_save(mword);
          eeglFree(mword);
          if (lower == NULL)
         goto cleanup;
          MEMMOVE(match + pat.len, lower, match_len);
          eeglFree(lower);
      } else
          MEMMOVE(match + pat.len, line + end_match_pos->col, match_len);
   }
   match[pat.len + match_len] = ZERO;
   return match;

cleanup:
    eeglFree(match);
    return NULL;
}

// Search for strings matching "pat" in the specified range and return them. Return OK/FAIL
private int
expandPatternInBook(
   CS pat,          // pattern to match
   Unt dir,         // direction: FORWARD or BACKWARD
   OUT ExpandMatch* matches
){
   Pos cur_match_pos, prev_match_pos, end_match_pos, word_end_pos;
   Boole looped_around = false;
   Boole has_range = false;
   Boole compl_started = false;
   CS match;
   CS fullMatch;
   Boole  exacttext = (p_wop & WILDOPT_EXACT) != 0;

   has_range = search_first_line != 0;

   if (pat == NULL || *pat == ZERO)
      return FAIL;

   Text patTxt = text(pat);
   CLEAR_FIELD(cur_match_pos);
   CLEAR_FIELD(prev_match_pos);
   if (has_range)
      cur_match_pos.lnum = search_first_line;
   else
      cur_match_pos = pre_incsearch_pos;

   Unt searchFlags = SEARCH_OPT | SEARCH_NOOF | SEARCH_PEEK | SEARCH_NFMSG
      | (has_range ? SEARCH_START : 0);

   for (;;) {
      ++emsg_off;
      ++msg_silent;
      int found_new_match = searchit(
         NULL, curBook, &cur_match_pos, OUT &end_match_pos, dir, patTxt, 1L, searchFlags, 
         RE_LAST, NULL
      );
      --msg_silent;
      --emsg_off;

      if (found_new_match == FAIL)
          break;

      // If in range mode, check if match is within the range
      if (has_range && (cur_match_pos.lnum < search_first_line
             || cur_match_pos.lnum > search_last_line))
         break;

      if (compl_started) {
         // If we've looped back to an earlier match, stop
         if ((dir == FORWARD && LTOREQ_POS(cur_match_pos, prev_match_pos)) 
               || (dir == BACKWARD && LTOREQ_POS(prev_match_pos, cur_match_pos))
         ){
            if (looped_around)
               break;
            else
               looped_around = true;
         }
      }

      compl_started = true;
      prev_match_pos = cur_match_pos;

      // Abort if user typed a character or interrupted
      if (char_avail() || gotInterruptG) {
         if (gotInterruptG) {
            (void)vpeekc();  // Remove <C-C> from input stream
            gotInterruptG = false; // Don't abandon the command line
         }
         goto cleanup;
      }

      //searchit() can return line number +1 past the last line when
      //searching for "foo\n" if "foo" is at end of buffer.
      if (end_match_pos.lnum > curBook->mem.lineCount) {
         cur_match_pos.lnum = 1;
         cur_match_pos.col = 0;
         cur_match_pos.coladd = 0;
         continue;
      }

      // Extract the matching text prepended to completed word
      if (!copy_substring_from_pos(&cur_match_pos, &end_match_pos, &fullMatch, &word_end_pos))
         break;

      if (exacttext)
         match = fullMatch;
      else {
         // Construct a new match from completed word appended to pattern itself
         match = concat_pattern_with_buffer_match(patTxt, &end_match_pos, false);

         // The regex pattern may include '\C' or '\c'. First, try matching the
         // buffer word as-is. If it doesn't match, try again with the lowercase
         // version of the word to handle smartcase behavior.
         if (!match || !is_regex_match(match, fullMatch)) {
            eeglFree(match);
            match = concat_pattern_with_buffer_match(patTxt, &end_match_pos, true);
            if (!match || !is_regex_match(match, fullMatch)) {
                eeglFree(match);
                eeglFree(fullMatch);
                continue;
            }
         }
         eeglFree(fullMatch);
      }

      // Include this match if it is not a duplicate
      for (Unt i = 0; i < matches->len; ++i) {
         if (STRCMP(match, matches->c[i]) == 0) {
            EE_CLEAR(match);
            break;
         }
      }
      if (match) {
      }
      if (has_range)
         cur_match_pos = word_end_pos;
   }
   return OK;

cleanup:
    return FAIL;
}

//}}}
//{{{ history: Functions for the history of the command-line.

private HistoryEntry *(history[HIST_COUNT]) = {NULL, NULL, NULL, NULL, NULL};
private int   hisidx[HIST_COUNT] = {-1, -1, -1, -1, -1};  // lastused entry
private int   hisnum[HIST_COUNT] = {0, 0, 0, 0, 0}; // identifying (unique) number of newest 
                                                    // history entry
private int   histLenG = 0;      // actual length of history tables

// Return the length of the history tables
int
getHistLen(void) {
   return histLenG;
}

// Return a pointer to a specified history table
Arr(HistoryEntry)
get_histentry(int hist_type) {
   return history[hist_type];
}

void
set_histentry(int hist_type, HistoryEntry* entry) {
   history[hist_type] = entry;
}

int *
get_hisidx(int hist_type) {
   return &hisidx[hist_type];
}

int *
get_hisnum(int hist_type) {
   return &hisnum[hist_type];
}

//Translate a history character to the associated type number.
int
hist_char2type(int c) {
   if (c == ':')
      return HIST_CMD;
   if (c == '=')
      return HIST_EXPR;
   if (c == '@')
      return HIST_INPUT;
   if (c == '>')
      return HIST_DEBUG;
   return HIST_SEARCH;       // must be '?' or '/'
}

//Table of history names. These names are used in :history and various hist...() functions.
//It is sufficient to give the significant prefix of a history name.

private CS (historyNames[]) = {
   [HIST_CMD] = (CS)"command",
   [HIST_SEARCH] = (CS)"search",
   [HIST_EXPR] = (CS)"expr",
   [HIST_INPUT] = (CS)"input",
   [HIST_DEBUG] = (CS)"debug",
   NULL
};

// Function given to expandGeneric() to obtain the possible first arguments of the ":history command
CS
get_history_arg(Expand *xp UNUSED, int idx) {
   CS short_names = (CS)":=@>?/";
   int       short_names_count = (int)STRLEN(short_names);
   int       history_name_count = ARRAY_LENGTH(historyNames) - 1;

   if (idx < short_names_count) {
      xp->matchBuilder[0] = (Byte)short_names[idx];
      xp->matchBuilder[1] = ZERO;
      return xp->matchBuilder;
   }
   if (idx < short_names_count + history_name_count)
      return (CS)historyNames[idx - short_names_count];
   if (idx == short_names_count + history_name_count)
      return (CS)"all";
   return NULL;
}

// init_history() - Initialize the command line history.
// Also used to re-allocate the history when the size changes.
void
init_history(void) {
   int      newlen;       // new length of history table
   HistoryEntry* temp;
   int      type;

   // If size of history table changed, reallocate it
   newlen = (int)p_hi;
   if (newlen == histLenG)      // history length didn't change
      return;

   // history length changed
   for (type = 0; type < HIST_COUNT; ++type) {  // adjust the tables
      if (newlen > 0) {
         temp = ALLOC_MULT(HistoryEntry, newlen);
      } else
         temp = NULL;

      if (hisidx[type] < 0) {     // there are no entries yet
         for (int i = 0; i < newlen; ++i)
            clear_hist_entry(&temp[i]);
      } ei (newlen > histLenG) {  // array becomes bigger
         int   i;
         int   j;

         for (i = 0; i <= hisidx[type]; ++i)
            temp[i] = history[type][i];
         j = i;
         for ( ; i <= newlen - (histLenG - hisidx[type]); ++i)
            clear_hist_entry(&temp[i]);
         for ( ; j < histLenG; ++i, ++j)
            temp[i] = history[type][j];
      } else {          // array becomes smaller or 0
         int   i;
         int   j;

         j = hisidx[type];
         for (i = newlen - 1; ; --i) {
            if (i >= 0)      // copy newest entries
               temp[i] = history[type][j];
            else {        // remove older entries
               eeglFree(history[type][j].hisstr);
               history[type][j].hisstrlen = 0;
            }
            if (--j < 0)
               j = histLenG - 1;
            if (j == hisidx[type])
               break;
         }
         hisidx[type] = newlen - 1;
      }
      eeglFree(history[type]);
      history[type] = temp;
   }
   histLenG = newlen;
}

void
clear_hist_entry(HistoryEntry *hisptr) {
    hisptr->hisnum = 0;
    hisptr->eeglinfo = false;
    hisptr->hisstr = NULL;
    hisptr->hisstrlen = 0;
    hisptr->time_set = 0;
}

// Check if command line 'str' is already in history.
// If 'move_to_front' is true, matching entry is moved to end of history.
int
in_history(
    int       type,
    Byte  *str,
    int       move_to_front,   // Move the entry to the front if it exists
    int       sep,
    int       writing)      // ignore entries read from eeglinfo
{
    int       i;
    int       last_i = -1;
    Byte  *p;
    Unt  len;

   if (hisidx[type] < 0)
      return false;
   i = hisidx[type];
   do {
      if (history[type][i].hisstr == NULL)
         return false;

      // For search history, check that the separator character matches as well.
      p = history[type][i].hisstr;
      if (STRCMP(str, p) == 0
         && !(writing && history[type][i].eeglinfo)
         && (type != HIST_SEARCH || sep == p[history[type][i].hisstrlen + 1])
      ){
         if (!move_to_front)
            return true;
         last_i = i;
         break;
      }
      if (--i < 0)
          i = histLenG - 1;
   } while (i != hisidx[type]);

   if (last_i < 0)
      return false;

   str = history[type][i].hisstr;
   len = history[type][i].hisstrlen;
   while (i != hisidx[type]) {
      if (++i >= histLenG)
          i = 0;
      history[type][last_i] = history[type][i];
      last_i = i;
   }
   history[type][i].hisnum = ++hisnum[type];
   history[type][i].eeglinfo = false;
   history[type][i].hisstr = str;
   history[type][i].hisstrlen = len;
   history[type][i].time_set = eeTime();
   return true;
}

//Convert history name (from table above) to its HIST_ equivalent.
//When "name" is empty, return "command" history. Return -1 for unknown history name.
private int
get_histtype(Byte *name) {
   int len = (int)STRLEN(name);

   // No argument: use current history.
   if (len == 0)
      return hist_char2type(get_cmdline_firstc());

   for (int i = 0; historyNames[i] != NULL; ++i) {
      if (STRNICMP(name, historyNames[i], len) == 0)
          return i;
   } 

   if (firstOccurrence((CS)":=@>?/", name[0]) != NULL && name[1] == ZERO)
      return hist_char2type(name[0]);

   return -1;
}

private int   last_maptick = -1;   // last seen maptick

//Add the given string to the given history.  If the string is already in the
//history then it is moved to the front.  "histype" may be one of he HIST_ values.
void
scrAddToHistory(
   int histype,
   Text newEntry,
   int in_map,      // consider maptick when inside a mapping
   int sep      // separator character used (search hist)
){

   if (histLenG == 0)      // no history
      return;

   if ((commModifierG.cmod_flags & CMOD_KEEPPATTERNS) != 0 && histype == HIST_SEARCH)
      return;

   HistoryEntry* hisptr;
   // Searches inside the same mapping overwrite each other, so that only
   // the last line is kept.  Be careful not to remove a line that was moved
   // down, only lines that were added.
   if ((histype == HIST_SEARCH && in_map) != 0) {
      if (maptick == last_maptick && hisidx[HIST_SEARCH] >= 0) {
         // Current line is from the same mapping, remove it
         hisptr = &history[HIST_SEARCH][hisidx[HIST_SEARCH]];
         eeglFree(hisptr->hisstr);
         clear_hist_entry(hisptr);
         --hisnum[histype];
         if (--hisidx[HIST_SEARCH] < 0)
            hisidx[HIST_SEARCH] = histLenG - 1;
      }
      last_maptick = -1;
   }

   if (in_history(histype, newEntry.c, true, sep, false))
      return;

   if (++hisidx[histype] == histLenG)
      hisidx[histype] = 0;
   hisptr = &history[histype][hisidx[histype]];
   eeglFree(hisptr->hisstr);

   // Store the separator after the ZERO of the string.
   hisptr->hisstr = copySubstr(newEntry.c, newEntry.len + 2);
   hisptr->hisstr[newEntry.len + 1] = sep;
   hisptr->hisstrlen = newEntry.len;

   hisptr->hisnum = ++hisnum[histype];
   hisptr->eeglinfo = false;
   hisptr->time_set = eeTime();
   if (histype == HIST_SEARCH && in_map)
      last_maptick = maptick;
}

// Get identifier of newest history entry. "histype" may be one of the HIST_ values.
private int
get_history_idx(int histype) {
   if (histLenG == 0 || histype < 0 || histype >= HIST_COUNT || hisidx[histype] < 0)
      return -1;

   return history[histype][hisidx[histype]].hisnum;
}

//Calculate history index from a number:
//  num > 0: seen as identifying number of a history entry
//  num < 0: relative position in history wrt newest entry
//"histype" may be one of the HIST_ values.
private int
calc_hist_idx(int histype, int num) {
   int      i;
   HistoryEntry   *hist;
   int      wrapped = false;

   if (histLenG == 0 || histype < 0 || histype >= HIST_COUNT
          || (i = hisidx[histype]) < 0 || num == 0)
      return -1;

   hist = history[histype];
   if (num > 0) {
   while (hist[i].hisnum > num)
       if (--i < 0) {
      if (wrapped)
          break;
      i += histLenG;
      wrapped = true;
       }
   if (i >= 0 && hist[i].hisnum == num && hist[i].hisstr != NULL)
       return i;
    } ei (-num <= histLenG) {
   i += num + 1;
   if (i < 0)
       i += histLenG;
   if (hist[i].hisstr != NULL)
       return i;
   }
   return -1;
}


// Clear all entries of a history. "histype" may be one of the HIST_ values.
private int
clr_history(int histype) {
   int      i;
   HistoryEntry   *hisptr;

   if (histLenG != 0 && histype >= 0 && histype < HIST_COUNT) {
      hisptr = history[histype];
      for (i = histLenG; i--;) {
         eeglFree(hisptr->hisstr);
         clear_hist_entry(hisptr);
         hisptr++;
      }
      hisidx[histype] = -1;   // mark history as cleared
      hisnum[histype] = 0;   // reset identifier counter
      return OK;
   }
   return FAIL;
}

// Remove all entries matching {str} from a history. "histype" may be one of the HIST_ values.
private Boole
del_history_entry(int histype, Byte *str) {
   RegMatch   regmatch;
   HistoryEntry   *hisptr;
   int idx;
   int i;
   int last;
   Boole found = false;

   if (histLenG == 0 || histype < 0 || histype >= HIST_COUNT 
            || *str == ZERO || hisidx[histype] < 0)
      return false;

   idx = hisidx[histype];
   regmatch.regprog = compileRegexp(str, RE_MAGIC + RE_STRING);
   if (regmatch.regprog == NULL)
      return false;

   regmatch.rm_ic = false;   // always match case

   i = last = idx;
   do {
      hisptr = &history[histype][i];
      if (hisptr->hisstr == NULL)
          break;
      if (eeRegexec(&regmatch, hisptr->hisstr, (ColNr)0)) {
          found = true;
          eeglFree(hisptr->hisstr);
          clear_hist_entry(hisptr);
      } else {
         if (i != last) {
            history[histype][last] = *hisptr;
            clear_hist_entry(hisptr);
         }
         if (--last < 0)
            last += histLenG;
      }
      if (--i < 0)
         i += histLenG;
   } while (i != idx);

   if (history[histype][idx].hisstr == NULL)
      hisidx[histype] = -1;

   eeRegFree(regmatch.regprog);
   return found;
}

// Remove an indexed entry from a history. "histype" may be one of the HIST_ values.
private int
del_history_idx(int histype, int idx) {
   int       i, j;

   i = calc_hist_idx(histype, idx);
   if (i < 0)
      return false;
   idx = hisidx[histype];
   eeglFree(history[histype][i].hisstr);
   history[histype][i].hisstrlen = 0;

   // When deleting the last added search string in a mapping, reset
   // last_maptick, so that the last added search string isn't deleted again.
   if (histype == HIST_SEARCH && maptick == last_maptick && i == idx)
   last_maptick = -1;

   while (i != idx) {
      j = (i + 1) % histLenG;
      history[histype][i] = history[histype][j];
      i = j;
   }
   clear_hist_entry(&history[histype][i]);
   if (--i < 0)
      i += histLenG;
   hisidx[histype] = i;
   return true;
}

void
f_histadd(Arr(Var) argvars UNUSED, Var* returnVar) {
   Byte buf[NUMBUFLEN];

   returnVar->number = false;

   CS str = convertVarToStringSingleUse(&argvars[0]);   // NULL on type error
   int histype = str ? get_histtype(str) : -1;
   if (histype < 0)
       return;

   str = tv_get_string_buf(&argvars[1], buf);
   if (*str == ZERO)
       return;

   init_history();
   scrAddToHistory(histype, text(str), false, ZERO);
   returnVar->number = true;
}

void
f_histdel(Arr(Var) argvars UNUSED, Var* returnVar UNUSED) {
   int      n;
   Byte   *str;

   str = convertVarToStringSingleUse(&argvars[0]);   // NULL on type error
   if (!str)
      n = 0;
   ei (argvars[1].tag == VAR_UNKNOWN)
      // only one argument: clear entire history
      n = clr_history(get_histtype(str));
   ei (argvars[1].tag == VAR_NUMBER)
      // index given: remove that entry
      n = del_history_idx(get_histtype(str), (int)tv_get_number(&argvars[1]));
   else {
      Byte builder[NUMBUFLEN];

      // string given: remove all matching entries
      n = del_history_entry(get_histtype(str), tv_get_string_buf(&argvars[1], builder));
   }

   returnVar->number = n;
}

void
f_histget(Arr(Var) argvars UNUSED, Var* returnVar) {
   Byte  *str;


   str = convertVarToStringSingleUse(&argvars[0]);   // NULL on type error
   if (!str)
      returnVar->string = NULL;
   else {
      int type;
      int idx;

      type = get_histtype(str);
      if (argvars[1].tag == VAR_UNKNOWN)
         idx = get_history_idx(type);
      else
         idx = (int)varGetNumberChk(argvars + 1, NULL); // -1 on type error

      idx = calc_hist_idx(type, idx);
      if (idx < 0)
         returnVar->string = copySubstr((CS)"", 0);
      else
         returnVar->string = copySubstr(history[type][idx].hisstr, history[type][idx].hisstrlen);
    }
    returnVar->tag = VAR_STRING;
}

void
f_histnr(Arr(Var) argvars UNUSED, Var* returnVar) {
   Byte* histname = convertVarToStringSingleUse(&argvars[0]);
   int i = histname == NULL ? HIST_CMD - 1 : get_histtype(histname);
   if (i >= HIST_CMD && i < HIST_COUNT)
      returnVar->number = get_history_idx(i);
   else
      returnVar->number = -1;
}

// Very specific function to remove the value in ":set key=val" from the history.
void
remove_key_from_history(void) {
   Byte   *p_start;
   Byte   *p_end;
   Byte   *p;
   int      i;

   i = hisidx[HIST_CMD];
   if (i < 0)
      return;
   p_start = history[HIST_CMD][i].hisstr;
   if (!p_start)
      return;

   p_end = p_start + history[HIST_CMD][i].hisstrlen;
   for (p = p_start; *p; ++p) {
      if (STRNCMP(p, "key", 3) == 0 && !SAFE_isalpha(p[3])) {
         p = firstOccurrence(p + 3, '=');
         if (p == NULL)
            break;
         ++p;
         for (i = 0; p[i] && !SPACE_OR_TAB(p[i]); ++i) {
            if (p[i] == '\\' && p[i + 1])
                ++i;
         } 

         MEMMOVE(p, p + i, (p_end - (p + i)) + 1);       // +1 for the ZERO
         p_end -= i;                      // adjust p_end for shortened string
         --p;
      }
   }

   history[HIST_CMD][i].hisstrlen = (Unt)(p_end - p_start);
}

// :history command - print a history
void
c_history(Invocation* invo) {
   HistoryEntry   *hist;
   int      histype1 = HIST_CMD;
   int      histype2 = HIST_CMD;
   int      hisidx1 = 1;
   int      hisidx2 = -1;
   int      idx;
   int      i, j, k;
   Byte   *end;
   Byte   *arg = invo->arg;

   if (histLenG == 0) {
      msg(_("'history' option is zero"));
      return;
   }

   if (!(EE_ISDIGIT(*arg) || *arg == '-' || *arg == ',')) {
   end = arg;
   while (ASCII_ISALPHA(*end)
      || firstOccurrence((CS)":=@>/?", *end) != NULL)
       end++;
   i = *end;
   *end = ZERO;
   histype1 = get_histtype(arg);
   if (histype1 == -1) {
      if (STRNICMP(arg, "all", STRLEN(arg)) == 0) {
         histype1 = 0;
         histype2 = HIST_COUNT-1;
      } else {
         *end = i;
         showErrFmtMsg(_(e_trailing_characters_str), arg);
         return;
      }
   } else
      histype2 = histype1;
   *end = i;
   } else
      end = arg;
   if (!get_list_range(&end, &hisidx1, &hisidx2) || *end != ZERO) {
      if (*end != ZERO)
         showErrFmtMsg(_(e_trailing_characters_str), end);
      else
         showErrFmtMsg(_(e_val_too_large), arg);
      return;
   }

   for (; !gotInterruptG && histype1 <= histype2; ++histype1) {
      eeSnprintf(IObuff, IOSIZE, "\n      #  %s history", historyNames[histype1]);
      msg_puts_title(IObuff);
      idx = hisidx[histype1];
      hist = history[histype1];
      j = hisidx1;
      k = hisidx2;
      if (j < 0)
         j = (-j > histLenG) ? 0 : hist[(histLenG + j + idx + 1) % histLenG].hisnum;
      if (k < 0)
         k = (-k > histLenG) ? 0 : hist[(histLenG + k + idx + 1) % histLenG].hisnum;
      if (idx >= 0 && j <= k) {
         for (i = idx + 1; !gotInterruptG; ++i) {
            if (i == histLenG)
               i = 0;
            if (hist[i].hisstr != NULL
               && hist[i].hisnum >= j && hist[i].hisnum <= k
               && !message_filtered(hist[i].hisstr)
            ){
               int  len;

               msg_putchar('\n');
               len = eeSnprintf(IObuff, IOSIZE,
                  "%c%6d  ", i == idx ? '>' : ' ', hist[i].hisnum);
               if (eeglStrSize(hist[i].hisstr) > (int)visibleColsG - 10)
                  trunc_string(hist[i].hisstr, IObuff + len, (int)visibleColsG - 10, IOSIZE - (int)len);
               else
                  STRCPY(IObuff + len, hist[i].hisstr);
               msg_outtrans(IObuff);
               out_flush();
            }
            if (i == idx)
               break;
         }
      } 
   }
}

//}}}
//{{{command line functions

// Return value when handling keys in command-line mode.
#define COMMLINE_UNCHANGED  1
#define COMMLINE_CHANGED    2
#define GOTO_NORMAL_MODE    3
#define PROCESS_NEXT_KEY    4


// The current CommlineInfo.  It is initialized in getCommline() and after that
// used by other functions.  When invoking getCommline() recursively it needs
// to be saved with saveCommline() and restored with restoreCommline().
private CommlineInfo commInfo;

private int new_cmdpos;   // position set by setCommlinePos()
private int extra_char = ZERO;  // extra character to display when redrawing the command line
private int extra_char_shift;

private CS getCommandWorker(Unt firstc, long count, int indent, Boole clear_ccline);
private int commlineCharsize(int idx);
private void set_cmdspos(void);
private void set_cmdspos_cursor(void);
private void correct_cmdspos(int idx, int cells);
private void deallocCommBuf(void);
private void allocateCommBuf(int len);
private void draw_cmdline(int start, int len);
private void saveCommline(CommlineInfo *ccp);
private void restoreCommline(CommlineInfo *ccp);
private int cmdline_paste(int regname, int literally, int remcr);
private void redrawPrompt(void);
private int ccheck_abbr(int);
private Unt openCommPort(void);
private int empty_pattern_magic(Byte *pat, Unt len, Magic magic_val);

private void
trigger_cmd_autocmd(int typechar, int evt) {
   Byte   typestr[2];

   typestr[0] = typechar;
   typestr[1] = ZERO;
   applyAutocomms(evt, typestr, typestr, false, curBook);
}

// Abandon the command line.
private void
abandon_cmdline(void) {
   deallocCommBuf();
   if (msg_scrolled == 0)
      compute_cmdrow();
   msg(E);
   redrawCommlineG = true;
}

// Guess that the pattern matches everything.  Only finds specific cases, such
// as a trailing \|, which can happen while typing a pattern.
private int
empty_pattern(Byte *p, Unt len, int delim) {
   Magic   magic_val = MAGIC_ON;

   if (len > 0)
      (void) skip_regexp_ex(p, delim, true, NULL, NULL, &magic_val);
   else
      return true;

   return empty_pattern_magic(p, len, magic_val);
}

private int
empty_pattern_magic(Byte *p, Unt len, Magic magic_val) {
   // remove trailing \v and the like
   while (len >= 2 && p[len - 2] == '\\'
         && firstOccurrence((CS)"mMvVcCZ", p[len - 1]) != NULL)
      len -= 2;

   // true, if the pattern is empty, or the pattern ends with \| and magic is
   // set (or it ends with '|' and very magic is set)
   return len == 0
      || (
          len > 1 && p[len - 1] == '|'
          && (
         (p[len - 2] == '\\' && magic_val == MAGIC_ON) ||
         (p[len - 2] != '\\' && magic_val == MAGIC_ALL)
          )
      );
}

// Struct to store the viewstate during 'incsearch' highlighting.
typedef struct {
   ColNr   vs_curswant;
   ColNr   vs_leftcol;
   ColNr   vs_skipcol;
   LineNr   vs_topline;
   int      vs_topfill;
   LineNr   vs_botline;
   LineNr   vs_empty_rows;
} viewstate_T;

private void
save_viewstate(viewstate_T *vs) {
   vs->vs_curswant = curPor->cursWant;
   vs->vs_leftcol = curPor->leftCol;
   vs->vs_skipcol = curPor->skipCol;
   vs->vs_topline = curPor->topLine;
   vs->vs_topfill = curPor->topFill;
   vs->vs_botline = curPor->bottomLine;
   vs->vs_empty_rows = curPor->emptyRowCount;
}

private void
restore_viewstate(viewstate_T *vs) {
   curPor->cursWant = vs->vs_curswant;
   curPor->leftCol = vs->vs_leftcol;
   curPor->skipCol = vs->vs_skipcol;
   curPor->topLine = vs->vs_topline;
   curPor->topFill = vs->vs_topfill;
   curPor->bottomLine = vs->vs_botline;
   curPor->emptyRowCount = vs->vs_empty_rows;
}

// Struct to store the state of 'incsearch' highlighting.
typedef struct {
   Pos   search_start;   // where 'incsearch' starts searching
   Pos   save_cursor;
   int      winid;      // window where this state is valid
   viewstate_T   init_viewstate;
   viewstate_T   old_viewstate;
   Pos   match_start;
   Pos   match_end;
   int      did_incsearch;
   int      incsearch_postponed;
} IncSearch;

private void
init_incsearch_state(IncSearch *is_state) {
   is_state->winid = curPor->id;
   is_state->match_start = curPor->cursor;
   is_state->did_incsearch = false;
   is_state->incsearch_postponed = false;
   CLEAR_POS(&is_state->match_end);
   is_state->save_cursor = curPor->cursor;  // may be restored later
   is_state->search_start = curPor->cursor;
   save_viewstate(&is_state->init_viewstate);
   save_viewstate(&is_state->old_viewstate);
}

// First move cursor to end of match, then to the start.  This
// moves the whole match onto the screen when 'nowrap' is set.
private void
set_search_match(Pos *t) {
   t->lnum += search_match_lines;
   t->col = search_match_endcol;
   if (t->lnum > curBook->mem.lineCount) {
      t->lnum = curBook->mem.lineCount;
      coladvance((ColNr)MAXCOL);
   }
}

// Parse the :[range]s/foo like commands and return details needed for incsearch and wildmenu 
// completion. Return true if pattern is valid.
// Set skiplen, patlen, search_first_line, and search_last_line.
int
parse_pattern_and_range(
   OUT Pos* incsearch_start,
   OUT Unt* searchDelim,
   OUT int* skiplen,
   OUT int* patlen
){
   Byte   *comm, *p, *end;
   CommandModifier  dummyModifier;
   Invocation   invo;
   Pos   save_cursor;
   int      delim_optional = false;
   int      delim;
   int      use_last_pat;
   Magic     magic = 0;
   CS dummy;

   *skiplen = 0;
   *patlen = commInfo.cmdlen;

   // Default range
   search_first_line = 0;
   search_last_line = MAXLNUM;

   CLEAR_FIELD(invo);
   invo.line1 = 1;
   invo.line2 = 1;
   invo.comm = commInfo.commBuf;
   invo.addressKind = ADDR_LINES;

   // Skip over command modifiers
   parse_command_modifiers(&invo, OUT &dummy, &dummyModifier, true);

   // Skip over the range to find the command.
   comm = skip_range(invo.comm, true, NULL);

   if (firstOccurrence((CS)"sgvl", *comm) == NULL)
      return false;

   // Skip over command name to find pattern separator
   for (p = comm; ASCII_ISALPHA(*p); ++p)
      {}
   if (*skipwhite(p) == ZERO)
      return false;

   if (STRNCMP(comm, "substitute", p - comm) == 0 || STRNCMP(comm, "smagic", p - comm) == 0) {
   } ei (STRNCMP(comm, "sort", MAX(p - comm, 3)) == 0
       || STRNCMP(comm, "uniq", MAX(p - comm, 3)) == 0
   ) {
      // skip over ! and flags
      if (*p == '!')
         p = skipwhite(p + 1);
      while (ASCII_ISALPHA(*(p = skipwhite(p))))
         ++p;
      if (*p == ZERO)
         return false;
   } ei (STRNCMP(comm, "vimgrep", MAX(p - comm, 3)) == 0
       || STRNCMP(comm, "vimgrepadd", MAX(p - comm, 8)) == 0
       || STRNCMP(comm, "lvimgrep", MAX(p - comm, 2)) == 0
       || STRNCMP(comm, "lvimgrepadd", MAX(p - comm, 9)) == 0
       || STRNCMP(comm, "global", p - comm) == 0
   ){
      // skip optional "!"
      if (*p == '!') {
         p++;
         if (*skipwhite(p) == ZERO)
            return false;
      }
      if (*comm != 'g')
         delim_optional = true;
   } else
      return false;

   p = skipwhite(p);
   delim = (delim_optional && eeIsIdentifierChar(*p)) ? ' ' : *p++;
   *searchDelim = delim;

   end = skip_regexp_ex(p, delim, true, NULL, NULL, &magic);
   use_last_pat = end == p && *end == delim;

   if (end == p && !use_last_pat)
      return false;

   // Skip if the pattern matches everything (e.g., for 'hlsearch')
   if (!use_last_pat) {
      char c = *end;
      *end = ZERO;
      int empty = empty_pattern_magic(p, (Unt)(end - p), magic);
      *end = c;
      if (empty)
          return false;
   }

   // Found a non-empty pattern or //
   *skiplen = (int)(p - commInfo.commBuf);
   *patlen = (int)(end - p);

   // Parse the address range
   save_cursor = curPor->cursor;
   curPor->cursor = *incsearch_start;

   parse_cmd_address(&invo, &dummy, true);

   if (invo.addr_count > 0) {
      int reverse_match = invo.line2 < invo.line1;
      search_first_line = reverse_match ? invo.line2 : invo.line1;
      search_last_line = reverse_match ? invo.line1 : invo.line2;
   } ei (comm[0] == 's' && comm[1] != 'o')
      // :s defaults to the current line
      search_first_line = search_last_line = curPor->cursor.lnum;

   curPor->cursor = save_cursor;
   return true;
}

//Return true when 'incsearch' highlighting is to be done.
//Set search_first_line and search_last_line to the address range.
//May change the last search pattern.
private int
incsearchHilitingImpl(
   int firstc,
   OUT Unt* searchDelim,
   OUT IncSearch* is_state,
   OUT int* skiplen,
   OUT int* patlen
) {
   int retval = false;

   *skiplen = 0;
   *patlen = commInfo.cmdlen;

   if (!p_is || cmd_silent)
      return false;

   // By default search all lines
   search_first_line = 0;
   search_last_line = MAXLNUM;

   if (firstc == '/' || firstc == '?') {
      *searchDelim = firstc;
      return true;
   }

   if (firstc != ':')
      return false;

   ++emsg_off;
   retval = parse_pattern_and_range(
         OUT &is_state->search_start, OUT searchDelim, OUT skiplen, OUT patlen
   );
   --emsg_off;

   return retval;
}

private void
finish_incsearch_highlighting(
   int gotesc,
   IncSearch *is_state,
   int call_drawUpdateScreen
){
   if (!is_state->did_incsearch)
      return;

   is_state->did_incsearch = false;
   if (gotesc)
      curPor->cursor = is_state->save_cursor;
   else {
      if (!EQUAL_POS(is_state->save_cursor, is_state->search_start)) {
         // put the '" mark at the original position
         curPor->cursor = is_state->save_cursor;
         setpcmark();
      }
      curPor->cursor = is_state->search_start;
   }
   restore_viewstate(&is_state->old_viewstate);
   highlight_match = false;

   // by default search all lines
   search_first_line = 0;
   search_last_line = MAXLNUM;

   validate_cursor();   // needed for TAB
   status_redraw_all();
   redraw_all_later(UPD_SOME_VALID);
   if (call_drawUpdateScreen)
      drawUpdateScreen(UPD_SOME_VALID);
}

// Do 'incsearch' highlighting if desired.
private void
may_do_incsearch_highlighting(int firstc, long count, OUT IncSearch* is_state) {
   int      skiplen, patlen;
   int      found;  // do_search() result
   Pos   end_pos;
   SearchitArg sia;
   int      next_char;
   int      use_last_pat;
   int      did_do_incsearch = is_state->did_incsearch;
   Unt searchDelim;

   // Parsing range may already set the last search pattern.
   // NOTE: must call restore_last_search_pattern() before returning!
   save_last_search_pattern();

   if (!incsearchHilitingImpl(firstc, OUT &searchDelim, OUT is_state, OUT &skiplen, OUT &patlen)) {
      restore_last_search_pattern();
      finish_incsearch_highlighting(false, is_state, true);
      if (did_do_incsearch && vpeekc() == ZERO)
         // may have skipped a redraw, do it now
         redrawcmd();
      return;
   }

   // If there is a character waiting, search and redraw later.
   if (char_avail()) {
      restore_last_search_pattern();
      is_state->incsearch_postponed = true;
      return;
   }
   is_state->incsearch_postponed = false;

   if (search_first_line == 0)
      // start at the original cursor position
      curPor->cursor = is_state->search_start;
   ei (search_first_line > curBook->mem.lineCount) {
      // start after the last line
      curPor->cursor.lnum = curBook->mem.lineCount;
      curPor->cursor.col = MAXCOL;
   } else {
      // start at the first line in the range
      curPor->cursor.lnum = search_first_line;
      curPor->cursor.col = 0;
   }

   // Use the previous pattern for ":s//".
   next_char = commInfo.commBuf[skiplen + patlen];
   use_last_pat = patlen == 0 && skiplen > 0 && commInfo.commBuf[skiplen - 1] == next_char;

    // If there is no pattern, don't do anything.
   if (patlen == 0 && !use_last_pat)     {
      found = 0;
   setHlsearch(false); // turn off previous hilite
   redraw_all_later(UPD_SOME_VALID);
   } else {
      Unt searchFlags = SEARCH_OPT | SEARCH_NOOF | SEARCH_PEEK;

      cursor_off();   // so the user knows we're busy
      out_flush();
      ++emsg_off;   // so it doesn't beep if bad expr
      if (!p_hls)
         searchFlags |= SEARCH_KEEP;
      if (search_first_line != 0)
         searchFlags |= SEARCH_START;
      commInfo.commBuf[skiplen + patlen] = ZERO;
      CLEAR_FIELD(sia);
      // Set the time limit to half a second.
      sia.sa_tm = 500;
      found = do_search(NULL, firstc == ':' ? '/' : firstc, searchDelim,
                (Text){commInfo.commBuf + skiplen, patlen}, count, searchFlags, &sia
      );
      commInfo.commBuf[skiplen + patlen] = next_char;
      --emsg_off;

      if (curPor->cursor.lnum < search_first_line || curPor->cursor.lnum > search_last_line) {
         // match outside of address range
         found = 0;
         curPor->cursor = is_state->search_start;
      }

      // if interrupted while searching, behave like it failed
      if (gotInterruptG) {
         (void)vpeekc();   // remove <C-C> from input stream
         gotInterruptG = false;   // don't abandon the command line
         found = 0;
      } ei (char_avail())
         // cancelled searching because a char was typed
         is_state->incsearch_postponed = true;
   }
   if (found != 0)
      highlight_match = true;      // highlight position
   else
      highlight_match = false;   // remove highlight

   // First restore the old curPor values, so the screen is positioned in the
   // same way as the actual search command.
   restore_viewstate(&is_state->old_viewstate);
   changed_cline_bef_curs();
   update_topline();

   if (found != 0) {
      Pos       save_pos = curPor->cursor;

      is_state->match_start = curPor->cursor;
      set_search_match(&curPor->cursor);
      validate_cursor();
      end_pos = curPor->cursor;
      is_state->match_end = end_pos;
      curPor->cursor = save_pos;
   } else
      end_pos = curPor->cursor; // shutup gcc 4

   // Disable 'hlsearch' highlighting if the pattern matches everything.
   // Avoids a flash when typing "foo\|".
   if (!use_last_pat) {
      next_char = commInfo.commBuf[skiplen + patlen];
      commInfo.commBuf[skiplen + patlen] = ZERO;
      if (empty_pattern(commInfo.commBuf + skiplen, (Unt)patlen, searchDelim) && hiliteSearchG){
         redraw_all_later(UPD_SOME_VALID);
         setHlsearch(false);
      }
      commInfo.commBuf[skiplen + patlen] = next_char;
    }

   validate_cursor();

   // May redraw the status line to show the cursor position.
   if (curPor->statusHeight > 0)
      curPor->statusLineNeedsRedraw = true;

   drawUpdateScreen(UPD_SOME_VALID);
   highlight_match = false;
   restore_last_search_pattern();

   // Leave it at the end to make CTRL-R CTRL-W work.  But not when beyond the
   // end of the pattern, e.g. for ":s/pat/".
   if (commInfo.commBuf[skiplen + patlen] != ZERO)
      curPor->cursor = is_state->search_start;
   ei (found != 0)
      curPor->cursor = end_pos;

   msg_starthere();
   redrawCommline();
   is_state->did_incsearch = true;
}

// May adjust 'incsearch' highlighting for typing CTRL-G and CTRL-T, go to next or previous match.
// Return FAIL when jumping to commlineUnchanged;
private int
may_adjust_incsearch_highlighting(
   Unt firstc,
   long count,
   OUT IncSearch* is_state,
   int c
){
   // Parsing range may already set the last search pattern.
   // NOTE: must call restore_last_search_pattern() before returning!
   save_last_search_pattern();

   int skiplen, patlen;
   Unt searchDelim;
   if (!incsearchHilitingImpl(firstc, OUT &searchDelim, OUT is_state, OUT &skiplen, OUT &patlen)) {
      restore_last_search_pattern();
      return OK;
   }
   if (patlen == 0 && commInfo.commBuf[skiplen] == ZERO) {
      restore_last_search_pattern();
      return FAIL;
   }

   Pos t;
   Unt searchFlags = SEARCH_NOOF;
   int i;
   int bslsh = false;
   Text pat;
   if (searchDelim == commInfo.commBuf[skiplen]) {
      pat = last_search_pattern();
      if (pat.len == 0) {
         restore_last_search_pattern();
         return FAIL;
      }
      skiplen = 0;
   } else
      pat = (Text){commInfo.commBuf + skiplen, patlen};

   // do not search for the search end delimiter,
   // unless it is part of the pattern
   if (pat.len > 2 && firstc == pat.c[pat.len - 1]) {
      pat.len--;
      if (pat.c[pat.len - 1] == '\\') {
         pat.c[pat.len - 1] = firstc;
         bslsh = true;
      }
   }

   cursor_off();
   out_flush();
   if (c == Ctrl_G) {
      t = is_state->match_end;
      if (LT_POS(is_state->match_start, is_state->match_end))
          // Start searching at the end of the match not at the beginning of the next column.
          (void)decl(&t);
      searchFlags |= SEARCH_COL;
   } else
      t = is_state->match_start;
   if (!p_hls)
      searchFlags |= SEARCH_KEEP;
   ++emsg_off;
   Unt save = pat.c[patlen];
   pat.c[patlen] = ZERO;
   i = searchit(curPor, curBook, &t, NULL,
       c == Ctrl_G ? FORWARD : BACKWARD,
       pat, count, searchFlags, RE_SEARCH, NULL);
   --emsg_off;
   pat.c[patlen] = save;
   if (bslsh)
      pat.c[patlen - 1] = '\\';
   if (i) {
      is_state->search_start = is_state->match_start;
      is_state->match_end = t;
      is_state->match_start = t;
      if (c == Ctrl_T && firstc != '?') {
         // Move just before the current match, so that when nv_search
         // finishes the cursor will be put back on the match.
         is_state->search_start = t;
         (void)decl(&is_state->search_start);
      } ei (c == Ctrl_G && firstc == '?') {
         // Move just after the current match, so that when nv_search
         // finishes the cursor will be put back on the match.
         is_state->search_start = t;
         (void)incl(&is_state->search_start);
      }
      if (LT_POS(t, is_state->search_start) && c == Ctrl_G) {
         // wrap around
         is_state->search_start = t;
         if (firstc == '?')
            (void)incl(&is_state->search_start);
         else
            (void)decl(&is_state->search_start);
      }

      set_search_match(&is_state->match_end);
      curPor->cursor = is_state->match_start;
      changed_cline_bef_curs();
      update_topline();
      validate_cursor();
      highlight_match = true;
      save_viewstate(&is_state->old_viewstate);
      drawUpdateScreen(UPD_NOT_VALID);
      highlight_match = false;
      redrawCommline();
      curPor->cursor = is_state->match_end;
   }
   restore_last_search_pattern();
   return FAIL;
}

//When CTRL-L typed: add character from the match to the pattern.
//May set "*c" to the added character. Return OK when jumping to commlineUnchanged.
private int
may_add_char_to_search(int firstc, OUT Unt *c, OUT IncSearch *is_state) {
   int skiplen, patlen;
   Unt searchDelim;

   //Parsing range may already set the last search pattern.
   //NOTE: must call restore_last_search_pattern() before returning!
   save_last_search_pattern();

   if (!incsearchHilitingImpl(firstc, OUT &searchDelim, OUT is_state, OUT &skiplen, OUT &patlen)) {
      restore_last_search_pattern();
      return FAIL;
   }
   restore_last_search_pattern();

   // Add a character from under the cursor for 'incsearch'.
   if (is_state->did_incsearch) {
      curPor->cursor = is_state->match_end;
      *c = gchar_cursor();
      if (*c != ZERO) {
         //If 'ignorecase' and 'smartcase' are set and the
         //command line has no uppercase characters, convert the character to lowercase.
         if (p_ic && p_scs && !pat_has_uppercase(commInfo.commBuf + skiplen))
            *c = MB_TOLOWER(*c);
         if (*c == searchDelim || firstOccurrence((CS)( "\\~^$.*["), *c) != NULL) {
            // put a backslash before special characters
            stuffcharReadbuff(*c);
            *c = '\\';
         }
         // add any composing characters
         if (mb_char2len(*c) != utfCharLen(ml_get_cursor())) {
            int save_c = *c;

            while (mb_char2len(*c) != utfCharLen(ml_get_cursor())) {
               curPor->cursor.col += mb_char2len(*c);
               *c = gchar_cursor();
               stuffcharReadbuff(*c);
            }
            *c = save_c;
         }
         return FAIL;
      }
   }
   return OK;
}


void
cmdline_init(void) {
   CLEAR_FIELD(commInfo);
}

//Handle CTRL-\ pressed in Command-line mode:
//- CTRL-\ CTRL-N goes to Normal mode
//- CTRL-\ CTRL-G goes to Insert mode when 'insertmode' is set
//- CTRL-\ e prompts for an expression.
private int
cmdline_handle_ctrl_bsl(int c, int *gotesc) {
   ++no_mapping;
   ++allow_keys;
   c = plain_vgetc();
   --no_mapping;
   --allow_keys;

   // CTRL-\ e doesn't work when obtaining an expression, unless it is in a mapping.
   if (c != Ctrl_N && c != Ctrl_G && (c != 'e'
      || (commInfo.cmdfirstc == '=' && keyWasTypedG)
      )
   ){
      vungetc(c);
      return PROCESS_NEXT_KEY;
   }

   if (c == 'e') {
      //Replace the command line with the result of an expression.
      //This will call getCommline() recursively in get_expr_register().
      if (commInfo.cmdpos == commInfo.cmdlen)
         new_cmdpos = 99999;   // keep it at the end
      else
         new_cmdpos = commInfo.cmdpos;

      c = get_expr_register();
      if (c == '=') {
          Byte   *p = NULL;

          // Evaluate the expression.  Set "textlock" to avoid nasty things
          // like going to another buffer.
          ++textlock;
          p = get_expr_line();
          --textlock;

          if (p != NULL) {
         int len = (int)STRLEN(p);

         if (reallocateCommBuf(len + 1) == OK) {
             commInfo.cmdlen = len;
             STRCPY(commInfo.commBuf, p);
             eeglFree(p);

             // Restore the cursor or use the position set with setCommlinePos().
             if (new_cmdpos > commInfo.cmdlen)
            commInfo.cmdpos = commInfo.cmdlen;
             else
            commInfo.cmdpos = new_cmdpos;

             keyWasTypedG = false;   // Don't do p_wc completion.
             redrawcmd();
             return COMMLINE_CHANGED;
         }
         eeglFree(p);
          }
      }
      beep_flush();
      gotInterruptG = false;   // don't abandon the command line
      anyEmsgG = false;
      emsg_on_display = false;
      redrawcmd();
      return COMMLINE_UNCHANGED;
   }

   *gotesc = true;   // will free commInfo.commBuf after putting it in history
   return GOTO_NORMAL_MODE;
}

//Completion for @wildchar or @wildcharm key.
// - hitting <ESC> twice means: abandon command line.
// - wildcard expansion is only done when the @wildchar key is really
//   typed, not when it comes from a macro
//Return COMMLINE_CHANGED if command line is changed or COMMLINE_UNCHANGED.
private int
commline_wildchar_complete(
   Unt c,
   int escape,
   int* did_wild_list,
   int* wim_index_p,
   OUT Expand* xp,
   int* gotesc,
   int redraw_if_menu_empty,
   Pos* pre_incsearch_pos
) {
   int wim_index = *wim_index_p;
   int res;
   int j;
   int options = WILD_NO_BEEP;
   int noselect = (wim_flags[0] & WIM_NOSELECT) != 0;

   if (wim_flags[wim_index] & WIM_BUFLASTUSED)
      options |= WILD_BUFLASTUSED;
   if (noselect)
      options |= WILD_KEEP_SOLE_ITEM;
   if (xp->files.len > 0) {  // typed p_wc at least twice
      // if 'wildmode' contains "list" may still need to list
      if (xp->files.len > 1
         && !*did_wild_list
         && ((wim_flags[wim_index] & WIM_LIST)
             || (p_wmnu && (wim_flags[wim_index] & WIM_FULL) != 0))
      ){
          (void)showmatches(xp,
             p_wmnu && ((wim_flags[wim_index] & WIM_LIST) == 0),
             noselect);
          redrawcmd();
          *did_wild_list = true;
      }
      if (wim_flags[wim_index] & WIM_LONGEST)
         res = nextwild(OUT xp, WILD_LONGEST, options, escape);
      ei (wim_flags[wim_index] & WIM_FULL)
         res = nextwild(OUT xp, WILD_NEXT, options, escape);
      else
         res = OK;       // don't insert 'wildchar' now
   }
   else {         // typed p_wc first time
      if (c == p_wc || c == p_wcm || c == K_WILD) {
         options |= WILD_MAY_EXPAND_PATTERN;
         if (c == K_WILD)
            options |= WILD_FUNC_TRIGGER;
         if (pre_incsearch_pos)
            xp->xp_pre_incsearch_pos = *pre_incsearch_pos;
         else
            xp->xp_pre_incsearch_pos = curPor->cursor;
      }
      wim_index = 0;
      j = commInfo.cmdpos;
      // if 'wildmode' first contains "longest", get longest common part
      if (wim_flags[0] & WIM_LONGEST)
         res = nextwild(OUT xp, WILD_LONGEST, options, escape);
      else
         res = nextwild(OUT xp, WILD_EXPAND_KEEP, options, escape);

      // Remove popup window if no completion items are available
      if (redraw_if_menu_empty && xp->files.len <= 0)
          drawUpdateScreen(0);

      // if interrupted while completing, behave like it failed
      if (gotInterruptG) {
          (void)vpeekc();   // remove <C-C> from input stream
          gotInterruptG = false;   // don't abandon the command line
          (void)expandWildcard(OUT xp, NULL, NULL, 0, WILD_FREE);
          xp->context = EXPAND_NOTHING;
          *wim_index_p = wim_index;
          return COMMLINE_CHANGED;
      }

      // when more than one match, and 'wildmode' first contains "list", or no change and 
      // 'wildmode' contains "longest,list", list all matches
      if (res == OK && xp->files.len > (noselect ? 0 : 1)) {
         // a "longest" that didn't do anything is skipped (but not "list:longest")
         if (wim_flags[0] == WIM_LONGEST && commInfo.cmdpos == j)
            wim_index = 1;
         if ((wim_flags[wim_index] & WIM_LIST)
             || (p_wmnu && (wim_flags[wim_index] & (WIM_FULL | WIM_NOSELECT)))
         ){
         if (!(wim_flags[0] & WIM_LONGEST)) {
            int p_wmnu_save = p_wmnu;

            p_wmnu = 0;

            // remove match
            nextwild(OUT xp, WILD_PREV, options, escape);
            p_wmnu = p_wmnu_save;
         }
         (void)showmatches(xp, p_wmnu
            && ((wim_flags[wim_index] & WIM_LIST) == 0), noselect);
         redrawcmd();
         *did_wild_list = true;
         if (wim_flags[wim_index] & WIM_LONGEST)
            nextwild(OUT xp, WILD_LONGEST, options, escape);
         ei ((wim_flags[wim_index] & WIM_FULL) && !(wim_flags[wim_index] & WIM_NOSELECT))
            nextwild(OUT xp, WILD_NEXT, options, escape);
         }
      } ei (xp->files.len == UNT)
         xp->context = EXPAND_NOTHING;
   }
   if (wim_index < 3)
      ++wim_index;
   if (c == ESC)
      *gotesc = true;

   *wim_index_p = wim_index;
   return (res == OK) ? COMMLINE_CHANGED : COMMLINE_UNCHANGED;
}

// Handle backspace, delete and CTRL-W keys in the command-line mode.
// Return:
//  COMMLINE_UNCHANGED - if the command line is not changed
//  COMMLINE_CHANGED - if the command line is changed
//  GOTO_NORMAL_MODE - go back to normal mode
private int
commlineEraseChars(
   Unt c,
   int indent,
   IncSearch *isp
) {
   if (c == K_KDEL)
      c = K_DEL;

   // Delete current character is the same as backspace on next character, except at end of line.
   if (c == K_DEL && commInfo.cmdpos != commInfo.cmdlen)
      ++commInfo.cmdpos;
   if (c == K_DEL)
      commInfo.cmdpos += mb_off_next(commInfo.commBuf, commInfo.commBuf + commInfo.cmdpos);
   if (commInfo.cmdpos > 0) {
      Byte *p;

      int j = commInfo.cmdpos;
      p = commInfo.commBuf + j;
      p = mb_prevptr(commInfo.commBuf, p);
      if (c == Ctrl_W) {
         while (p > commInfo.commBuf && isSpace(*p))
            p = mb_prevptr(commInfo.commBuf, p);
         int i = mb_get_class(p);
         while (p > commInfo.commBuf && mb_get_class(p) == i)
            p = mb_prevptr(commInfo.commBuf, p);
         if (mb_get_class(p) != i)
            p += utfCharLen(p);
      }
      commInfo.cmdpos = (int)(p - commInfo.commBuf);
      commInfo.cmdlen -= j - commInfo.cmdpos;
      int i = commInfo.cmdpos;
      while (i < commInfo.cmdlen)
         commInfo.commBuf[i++] = commInfo.commBuf[j++];

      // Truncate at the end, required for multi-byte chars.
      commInfo.commBuf[commInfo.cmdlen] = ZERO;
      if (commInfo.cmdlen == 0) {
         isp->search_start = isp->save_cursor;
         // save view settings, so that the screen
         // won't be restored at the wrong position
         isp->old_viewstate = isp->init_viewstate;
      }
      redrawcmd();
   } ei (commInfo.cmdlen == 0 && c != Ctrl_W && commInfo.cmdprompt == NULL && indent == 0) {
      // In debug mode it doesn't make sense to return.
      if (commInfo.cmdfirstc == '>')
         return COMMLINE_UNCHANGED;

      deallocCommBuf();   // no commandline to return

      if (!cmd_silent) {
         msgColG = 0;
         msg_putchar(' ');      // delete ':'
      }
      isp->search_start = isp->save_cursor;
      redrawCommlineG = true;
      return GOTO_NORMAL_MODE;
   }
   return COMMLINE_CHANGED;
}

// Handle the CTRL-^ key in the command-line mode and toggle the use of the
// language :lmap mappings and/or Input Method.
private void
cmdline_toggle_langmap(long *b_im_ptr) {
   if (map_to_exists_mode((CS)"", MODE_LANGMAP, false)) {
      // ":lmap" mappings exists, toggle use of mappings.
      stateG ^= MODE_LANGMAP;
      if (b_im_ptr != NULL) {
         if (stateG & MODE_LANGMAP)
            *b_im_ptr = B_IMODE_LMAP;
         else
            *b_im_ptr = B_IMODE_NONE;
      }
   }
   ui_cursor_shape();   // may show different cursor shape
   // Show/unshow value of 'keymap' in status lines later.
   drawAllStatusLinesOfCurBookLater();
}

// Handle the CTRL-R key in the command-line mode and insert the contents of a register
private int
cmdline_insert_reg(int *gotesc UNUSED) {
   int      i;
   int      c;
   int      literally = false;
   int      save_new_cmdpos = new_cmdpos;
   putcmdline('"', true);
   ++no_mapping;
   ++allow_keys;
   i = c = plain_vgetc();   // CTRL-R <char>
   if (i == Ctrl_O)
      i = Ctrl_R;      // CTRL-R CTRL-O == CTRL-R CTRL-R
   if (i == Ctrl_R)
      c = plain_vgetc();   // CTRL-R CTRL-R <char>
    extra_char = ZERO;
    --no_mapping;
    --allow_keys;
   // Insert the result of an expression.
   new_cmdpos = -1;
   if (c == '=') {
      if (commInfo.cmdfirstc == '=') { // can't do this recursively
         beep_flush();
         c = ESC;
      } else
         c = get_expr_register();
   }
   if (c != ESC) {      // use ESC to cancel inserting register
      literally = i == Ctrl_R || (c == '*' || c == '+') ;
      cmdline_paste(c, literally, false);

      // When there was a serious error, abort getting the command line.
      if (aborting()) {
         *gotesc = true;  // will free commInfo.commBuf after
         // putting it in history
         return GOTO_NORMAL_MODE;
      }
      keyWasTypedG = false;   // Don't do p_wc completion.
      if (new_cmdpos >= 0) {
         // setCommlinePos() was used
         if (new_cmdpos > commInfo.cmdlen)
            commInfo.cmdpos = commInfo.cmdlen;
         else
            commInfo.cmdpos = new_cmdpos;
      }
   }
   new_cmdpos = save_new_cmdpos;

   // remove the double quote
   redrawcmd();

   // With "literally": the command line has already changed.
   // Else: the text has been stuffed, but the command line didn't change yet.
   return literally ? COMMLINE_CHANGED : COMMLINE_UNCHANGED;
}

// Handle the Left and Right mouse clicks in the command-line mode.
private void
cmdline_left_right_mouse(Unt c, int *ignore_drag_release) {
   if (c == K_LEFTRELEASE || c == K_RIGHTRELEASE)
      *ignore_drag_release = true;
   else
      *ignore_drag_release = false;
   if (mouseRowG < (int)commlineRowG) {

      // Handle modeless selection.
      Boole is_click, is_drag;
      int button = get_mouse_button(KEY2TERMCAP1(c), OUT &is_click, OUT &is_drag);
      clip_modeless(button, is_click, is_drag);
      return;
   }

   set_cmdspos();
   for (commInfo.cmdpos = 0; commInfo.cmdpos < commInfo.cmdlen; ++commInfo.cmdpos) {
      int i = commlineCharsize(commInfo.cmdpos);
      if (mouseRowG <= commlineRowG + commInfo.cmdspos / visibleColsG
            && mouseColG < commInfo.cmdspos % visibleColsG + i)
         break;
      // Count ">" for double-wide char that doesn't fit.
      correct_cmdspos(commInfo.cmdpos, i);
      commInfo.cmdpos += utfCharLen(commInfo.commBuf + commInfo.cmdpos) - 1;
      commInfo.cmdspos += i;
   }
}

// Handle the Up, Down, Page Up, Page down, CTRL-N and CTRL-P key in the
// command-line mode. The pressed key is in 'c'.
// Return:
//  COMMLINE_UNCHANGED - if the command line is not changed
//  COMMLINE_CHANGED - if the command line is changed
//  GOTO_NORMAL_MODE - go back to normal mode
private int
cmdline_browse_history(
   Unt c,
   Unt firstc,
   OUT Text* currComm,
   int histype,
   int* hiscnt_p,
   Expand *xp
) {
   int orig_hiscnt;
   int hiscnt = orig_hiscnt = *hiscnt_p;
   Text lookfor = *currComm;
   int res;

   if (getHistLen() == 0 || firstc == ZERO)   // no history
      return COMMLINE_UNCHANGED;

   // save current command string so it can be restored later
   if (lookfor.len == 0) {
      lookfor = copyText((Text){commInfo.commBuf, commInfo.cmdlen});
      lookfor.c[commInfo.cmdpos] = ZERO;
      lookfor.len = commInfo.cmdpos;
   }

   for (;;) {
      // one step backwards
      if (c == K_UP || c == K_S_UP || c == Ctrl_P || c == K_PAGEUP || c == K_KPAGEUP) {
         if (hiscnt == getHistLen())   // first time
            hiscnt = *get_hisidx(histype);
         ei (hiscnt == 0 && *get_hisidx(histype) != getHistLen() - 1)
            hiscnt = getHistLen() - 1;
         ei (hiscnt != *get_hisidx(histype) + 1)
            --hiscnt;
         else  {       // at top of list
            hiscnt = orig_hiscnt;
            break;
         }
      } else {   // one step forwards
         // on last entry, clear the line
         if (hiscnt == *get_hisidx(histype)) {
            hiscnt = getHistLen();
            break;
         }

         // not on a history line, nothing to do
         if (hiscnt == getHistLen())
            break;
         if (hiscnt == getHistLen() - 1)   // wrap around
            hiscnt = 0;
         else
            ++hiscnt;
      }
      if (hiscnt < 0 || get_histentry(histype)[hiscnt].hisstr == NULL) {
          hiscnt = orig_hiscnt;
          break;
      }
      if ((c != K_UP && c != K_DOWN)
            || hiscnt == orig_hiscnt
            || STRNCMP(get_histentry(histype)[hiscnt].hisstr, lookfor.c, lookfor.len) == 0
      )
         break;
   }

   if (hiscnt != orig_hiscnt) {   // jumped to other entry
      CS p;
      Unt plen;
      int old_firstc;

      deallocCommBuf();

      xp->context = EXPAND_NOTHING;
      if (hiscnt == getHistLen()) {
         p = lookfor.c;   // back to the old one
         plen = lookfor.len;
      } else {
         p = get_histentry(histype)[hiscnt].hisstr;
         plen = get_histentry(histype)[hiscnt].hisstrlen;
      }

      if (histype == HIST_SEARCH && p != lookfor.c && (old_firstc = p[plen + 1]) != firstc) {
         int i;
         int j;
         Unt  len;

         // Correct for the separator character used when
         // adding the history entry vs the one used now.
         // First loop: count length. Second loop: copy the characters.
         for (i = 0; i <= 1; ++i) {
            len = 0;
            for (j = 0; p[j] != ZERO; ++j) {
               // Replace old sep with new sep, unless it is escaped.
               if (p[j] == old_firstc && (j == 0 || p[j - 1] != '\\')) {
                  if (i > 0)
                      commInfo.commBuf[len] = firstc;
               } else {
                  // Escape new sep, unless it is already escaped.
                  if (p[j] == firstc && (j == 0 || p[j - 1] != '\\')) {
                      if (i > 0)
                     commInfo.commBuf[len] = '\\';
                      ++len;
                  }
                  if (i > 0)
                      commInfo.commBuf[len] = p[j];
               }
               ++len;
            }
            if (i == 0) {
               allocateCommBuf((int)len);
               if (commInfo.commBuf == NULL) {
                  res = GOTO_NORMAL_MODE;
                  goto done;
               }
            }
         }
         commInfo.commBuf[len] = ZERO;
         commInfo.cmdpos = commInfo.cmdlen = (int)len;
      } else {
         allocateCommBuf((int)plen);
         if (commInfo.commBuf == NULL) {
            res = GOTO_NORMAL_MODE;
            goto done;
         }
         STRCPY(commInfo.commBuf, p);
         commInfo.cmdpos = commInfo.cmdlen = (int)plen;
      }

      redrawcmd();
      res = COMMLINE_CHANGED;
      goto done;
   }
   beep_flush();
   res = COMMLINE_UNCHANGED;

done:
   *currComm = lookfor;
   *hiscnt_p = hiscnt;
   return res;
}

// Initialize the current command-line info.
private void
init_ccline(int firstc, int indent) {
   commInfo.overstrike = false;          // always start in insert mode

   // set some variables for redrawcmd()
   commInfo.cmdfirstc = (firstc == '@' ? 0 : firstc);
   commInfo.cmdindent = (firstc > 0 ? indent : 0);

   // alloc initial commInfo.commBuf
   allocateCommBuf(indent + 50);
   commInfo.cmdlen = commInfo.cmdpos = 0;
   commInfo.commBuf[0] = ZERO;
   sb_text_start_cmdline();

   // autoindent for :insert and :append
   if (firstc <= 0) {
      memset(commInfo.commBuf, ' ', indent);
      commInfo.commBuf[indent] = ZERO;
      commInfo.cmdpos = indent;
      commInfo.cmdspos = indent;
      commInfo.cmdlen = indent;
   }
}

//getCommline() - accept a command line starting with firstc.
//
//firstc == ':'       get ":" command line.
//firstc == '/' or '?'       get search pattern
//firstc == '='       get expression
//firstc == '@'       get text for input() function
//firstc == '>'       get text for debug mode
//firstc == ZERO       get text for :insert command
//firstc == -1          like ZERO, and break on CTRL-C
//
//The line is collected in commInfo.commBuf, which is reallocated to fit the command line.
//
//Careful: getCommline() can be called recursively!
//
//Return pointer to allocated string if there is a commandline, NULL otherwise.
CS
getCommline(
   Unt firstc,
   long count,   // only used for incremental search
   int indent,   // indent for inside conditionals
   GetlineAlgo do_concat UNUSED
){
   return getCommandWorker(firstc, count, indent, true);
}

private Arr(Byte)
getCommandWorker(
   Unt firstc,
   long count UNUSED,   // only used for incremental search
   int indent,      // indent for inside conditionals
   Boole clear_ccline
) {  // clear commInfo first
   static int   depth = 0;       // call depth
   Unt      c = 0;
   int      i;
   int      j;
   int      gotesc = false;      // true when <ESC> just typed
   int      do_abbr;      // when true check for abbr.
   Text lookfor = (Text){NULL, 0};   // string to match
   int      hiscnt;         // current history line in use
   int      histype;      // history type to be used
   IncSearch   is_state;
   int      did_wild_list = false;   // did wild_list() recently
   int      wim_index = 0;      // index in wim_flags[]
   int      res;
   int      save_msg_scroll = msg_scroll;
   int      save_State = stateG;   // remember stateG when called
   int      some_key_typed = false;   // one of the keys was typed
   // mouse drag and release events are ignored, unless they are
   // preceded with a mouse down event
   int ignore_drag_release = true;
   int break_ctrl_c = false;
   long* b_im_ptr = NULL;
   Book* b_im_ptr_buf = NULL;   // buffer where b_im_ptr is valid
   CommlineInfo save_ccline;
   int did_save_ccline = false;
   int wild_type = 0;
   CS prev_cmdbuff = NULL;

   // one recursion level deeper
   ++depth;

   if (commInfo.commBuf != NULL) {
      // Being called recursively.  Since commInfo is global, we need to save
      // the current buffer and restore it when returning.
      saveCommline(&save_ccline);
      did_save_ccline = true;
   }
   if (clear_ccline) 
      CLEAR_FIELD(commInfo);

   if (firstc == UNT) {
      firstc = ZERO;
      break_ctrl_c = true;
   }

   init_incsearch_state(&is_state);

   init_ccline(firstc, indent);

   if (depth == 50) {
      // Somehow got into a loop recursively calling getCommline(), bail out.
      emsg(_(e_command_too_recursive));
      goto theend;
   }

   Expand xp;
   expandInit(&xp);
   xp.files.a = createArena();
   commInfo.xpc = &xp;
   clear_commlineSaved();

   redir_off = true;      // don't redirect the typed command
   if (!cmd_silent) {
      i = msg_scrolled;
      msg_scrolled = 0;      // avoid wait_return() message
      gotoCommline(true);
      msg_scrolled += i;
      redrawPrompt();      // draw prompt or indent
      set_cmdspos();
   }
   xp.context = EXPAND_NOTHING;
   xp.backslash = XP_BS_NONE;
   xp.isShell = false;

   if (commInfo.input_fn) {
      xp.context = commInfo.context;
      xp.input = mbText(commInfo.commBuf);
      xp.completionFn = commInfo.completionFn;
   }

   // Avoid scrolling when called by a recursive doCommand(), e.g. when
   // doing ":@0" when register 0 doesn't contain a CR.
   msg_scroll = false;

   stateG = MODE_COMMLINE;

   if (firstc == '/' || firstc == '?' || firstc == '@') {
      // Use ":lmap" mappings for search pattern and input().
      if (curBook->o.b_p_imsearch == B_IMODE_USE_INSERT)
          b_im_ptr = &curBook->o.b_p_iminsert;
      else
          b_im_ptr = &curBook->o.b_p_imsearch;
      b_im_ptr_buf = curBook;
      if (*b_im_ptr == B_IMODE_LMAP)
         stateG |= MODE_LANGMAP;
   }

   setmouse();
   ui_cursor_shape();      // may show different cursor shape

   // When inside an autocommand for writing "exiting" may be set and
   // terminal mode set to cooked.  Need to set raw mode here then.
   termSetMode(TMODE_RAW);

   if (!debug_mode)
      may_trigger_modechanged();

   init_history();
   hiscnt = getHistLen();   // set hiscnt to impossible history value
   histype = hist_char2type(firstc);

   // If something above caused an error, reset the flags, we do want to type
   // and execute commands. Display may be messed up a bit.
   if (anyEmsgG)
      redrawcmd();

   // Redraw the statusline in case it uses the current mode using the mode() function.
   if (!cmd_silent && msg_scrolled == 0) {
      int   found_one = false;
      Portal   *wp;

      FOR_ALL_PORTALS(wp) {
         if (wp->o.statusLine) {
            wp->statusLineNeedsRedraw = true;
            found_one = true;
         }
      } 

      if (found_one)
         redraw_statuslines();
   }

   anyEmsgG = false;
   gotInterruptG = false;

   // Collect the command string, handling editing keys.
   for (;;) {
      int   end_wildmenu;
      int   prev_cmdpos = commInfo.cmdpos;
      int   skip_pum_redraw = false;

      EE_CLEAR(prev_cmdbuff);

      redir_off = true;   // Don't redirect the typed command.
               // Repeated, because a ":redir" inside
               // completion may switch it on.
      quitMoreG = false;   // reset after CTRL-D which had a more-prompt

      anyEmsgG = false;   // There can't really be a reason why an error
               // that occurs while typing a command should
               // cause the command not to be executed.

      // Trigger SafeState if nothing is pending.
      may_trigger_safestate(xp.files.len == UNT);

      if (commInfo.commBuf != NULL) {
         prev_cmdbuff = copySubstr(commInfo.commBuf, commInfo.cmdpos);
         if (prev_cmdbuff == NULL)
            goto returncmd;
      }

      // Defer screen update to avoid pum flicker during wildtrigger()
      if (c == K_WILD && firstc != '@')
          skip_pum_redraw = true;

      //Get a character. Ignore K_IGNORE and K_NOP, they should not do
      //anything, such as stop completion.
      do {
          cursorcmd();      // set the cursor on the right spot
          c = safe_vgetc();
      } while (c == K_IGNORE || c == K_NOP);

      if (c == K_COMMAND || c == K_SCRIPT_COMMAND) {
         if (do_cmdkey_command(c, DOCMD_NOWAIT) == OK) {
            goto commlineChanged;
         }
      }

      if (keyWasTypedG) {
         some_key_typed = true;
      }

      //Ignore gotInterruptG when CTRL-C was typed here.
      //Don't ignore it in :global, we really need to break then, e.g., for
      //":g/pat/normal /pat" (without the <CR>).
      //Don't ignore it for the input() function.
      if ((c == Ctrl_C || c == extraInterruptCharG)
            && firstc != '@'
            && (!break_ctrl_c)
            && !global_busy)
         gotInterruptG = false;

      // free old command line when finished moving around in the history list
      if (lookfor.len > 0
         && c != K_S_DOWN && c != K_S_UP
         && c != K_DOWN && c != K_UP
         && c != K_PAGEDOWN && c != K_PAGEUP
         && c != K_KPAGEDOWN && c != K_KPAGEUP
         && c != K_LEFT && c != K_RIGHT
         && (xp.files.len < UNT || (c != Ctrl_P && c != Ctrl_N))
      ){
         EE_CLEAR(lookfor.c);
         lookfor.len = 0;
      }

      //When there are matching completions to select <S-Tab> works like
      //CTRL-P (unless 'wc' is <S-Tab>).
      if (c != p_wc && c == K_S_TAB && xp.files.len > 0 && xp.files.len != UNT)
         c = Ctrl_P;

      if (p_wmnu)
         c = wildmenu_translate_key(&commInfo, c, &xp, did_wild_list);

      int key_is_wc = (c == p_wc && keyWasTypedG) || c == p_wcm;
      if ((cmdline_pum_active() || did_wild_list) && !key_is_wc) {
         // Ctrl-Y: Accept the current selection and close the popup menu.
         // Ctrl-E: cancel the cmdline popup menu and return the original text.
         if (c == Ctrl_E || c == Ctrl_Y) {
            wild_type = (c == Ctrl_E) ? WILD_CANCEL : WILD_APPLY;
            if (nextwild(OUT &xp, wild_type, WILD_NO_BEEP, firstc != '@') == FAIL)
               break;
         }
      }

      // Trigger CmdlineLeavePre autocommand
      if (keyWasTypedG && (c == '\n' || c == '\r' || c == K_KENTER || c == ESC
             || c == extraInterruptCharG
             || c == Ctrl_C)
      ){
         if ((c == ESC || c == Ctrl_C) && (wim_flags[0] & WIM_LIST))
            setHlsearch(false);
      }

      // The wildmenu is cleared if the pressed key is not used for navigating the wild menu 
      // (i.e. the key is not 'wildchar' or 'wildcharm' or Ctrl-N or Ctrl-P or Ctrl-A or Ctrl-L).
      // If the popup menu is displayed, then PageDown and PageUp keys are
      // also used to navigate the menu.
      end_wildmenu = (!key_is_wc
         && c != Ctrl_N && c != Ctrl_P && c != Ctrl_A && c != Ctrl_L);
      end_wildmenu = end_wildmenu && (!cmdline_pum_active() ||
                (c != K_PAGEDOWN && c != K_PAGEUP
                 && c != K_KPAGEDOWN && c != K_KPAGEUP));

      // free expanded names when finished walking through matches
      if (end_wildmenu) {
         if (cmdline_pum_active()) {
            skip_pum_redraw = skip_pum_redraw && !key_is_wc
                && (bookIsCharPrintable(c)
                  || c == K_BS || c == Ctrl_H || c == K_DEL
                  || c == K_KDEL || c == Ctrl_W || c == Ctrl_U);
            cmdline_pum_remove(&commInfo, skip_pum_redraw);
         }
         if (xp.files.len != UNT)
            (void)expandWildcard(OUT &xp, NULL, NULL, 0, WILD_FREE);
         did_wild_list = false;
         if (!p_wmnu || (c != K_UP && c != K_DOWN))
            xp.context = EXPAND_NOTHING;
         wim_index = 0;
         wildmenu_cleanup(&commInfo);
      }

      if (p_wmnu)
          c = wildmenu_process_key(&commInfo, c, &xp);

      // CTRL-\ CTRL-N goes to Normal mode, CTRL-\ CTRL-G goes to Insert
      // mode when 'insertmode' is set, CTRL-\ e prompts for an expression.
      if (c == Ctrl_BSL) {
         res = cmdline_handle_ctrl_bsl(c, &gotesc);
         if (res == COMMLINE_CHANGED)
            goto commlineChanged;
         ei (res == COMMLINE_UNCHANGED)
            goto commlineUnchanged;
         ei (res == GOTO_NORMAL_MODE)
            goto returncmd;      // back to comm mode
         c = Ctrl_BSL;      // backslash key not processed by cmdline_handle_ctrl_bsl()
      }

      if (c == Ctrl_F || c == K_COMMPORT) {
          // TODO: why is ex_normal_busy checked here?
          if ((c == K_COMMPORT || ex_normal_busy == 0) && gotInterruptG == false) {
             // Open a portal into the command line history
             c = openCommPort();
             some_key_typed = true;
          }
      }

      if (c == '\n' || c == '\r' || c == K_KENTER || (c == ESC && !keyWasTypedG)) {
         gotesc = false;   // Might have typed ESC previously, don't truncate the cmdline now.
         if (ccheck_abbr(c + ABBR_OFF))
            goto commlineChanged;
         if (!cmd_silent) {
            windgoto(msgRowG, 0);
            out_flush();
         }
         break;
      }

      // Completion for 'wildchar', 'wildcharm', and wildtrigger()
      if ((c == p_wc && !gotesc && keyWasTypedG) || c == p_wcm || c == K_WILD) {
         if (c == K_WILD)
            ++emsg_silent;  // Silence the bell
         res = commline_wildchar_complete(c, firstc != '@', &did_wild_list,
            &wim_index, OUT &xp, &gotesc, c == K_WILD,
            &is_state.search_start
         );
         if (c == K_WILD)
            --emsg_silent;
         if (res == COMMLINE_CHANGED)
            goto commlineChanged;
         if (c == K_WILD)
            goto commlineUnchanged;
      }

      gotesc = false;

      // <S-Tab> goes to last match, in a clumsy way
      if (c == K_S_TAB && keyWasTypedG) {
         if (nextwild(OUT &xp, WILD_EXPAND_KEEP, 0, firstc != '@') == OK) {
            if (xp.files.len > 1
               && ((!did_wild_list && (wim_flags[wim_index] & WIM_LIST)) || p_wmnu)
            ){
               // Trigger the popup menu when wildoptions=pum
               showmatches(
                  &xp, 
                  p_wmnu && ((wim_flags[wim_index] & WIM_LIST) == 0),
                  wim_flags[0] & WIM_NOSELECT
               );
            }
            if (nextwild(OUT &xp, WILD_PREV, 0, firstc != '@') == OK
               && nextwild(OUT &xp, WILD_PREV, 0, firstc != '@') == OK
            )
               goto commlineChanged;
          }
      }

      if (c == ZERO || c == K_ZERO)       // ZERO is stored as NL
         c = NL;

      do_abbr = true;      // default: check for abbreviation

      // If already used to cancel/accept wildmenu, don't process the key further.
      if (wild_type == WILD_CANCEL || wild_type == WILD_APPLY) {
         // Apply search highlighting
         if (is_state.winid != curPor->id)
            init_incsearch_state(&is_state);
         if (keyWasTypedG || vpeekc() == ZERO)
            may_do_incsearch_highlighting(firstc, count, &is_state);
         wild_type = 0;
         goto commlineUnchanged;
      }

      // Big switch for a typed command line character.
      switch (c) {
      case K_BS:
      case Ctrl_H:
      case K_DEL:
      case K_KDEL:
      case Ctrl_W:
         res = commlineEraseChars(c, indent, &is_state);
         if (res == COMMLINE_UNCHANGED)
            goto commlineUnchanged;
         ei (res == GOTO_NORMAL_MODE)
            goto returncmd;      // back to comm mode
         goto commlineChanged;

      case K_INS:
      case K_KINS:
         commInfo.overstrike = !commInfo.overstrike;
         ui_cursor_shape();   // may show different cursor shape
         may_trigger_modechanged();
         drawAllStatusLinesOfCurBookLater();
         redraw_statuslines();
         goto commlineUnchanged;

      case Ctrl_HAT:
         cmdline_toggle_langmap( bookIsValid(b_im_ptr_buf) ? b_im_ptr : NULL);
         goto commlineUnchanged;

      case Ctrl_U:
         // delete all characters left of the cursor
         j = commInfo.cmdpos;
         commInfo.cmdlen -= j;
         i = commInfo.cmdpos = 0;
         while (i < commInfo.cmdlen)
             commInfo.commBuf[i++] = commInfo.commBuf[j++];
         // Truncate at the end, required for multi-byte chars.
         commInfo.commBuf[commInfo.cmdlen] = ZERO;
         if (commInfo.cmdlen == 0)
            is_state.search_start = is_state.save_cursor;
         redrawcmd();
         goto commlineChanged;

      case Ctrl_Y:
         // Copy the modeless selection, if there is one.
         if (clipboard.state != SELECT_CLEARED) {
            if (clipboard.state == SELECT_DONE)
               clip_copy_modeless_selection();
            goto commlineUnchanged;
         }
         break;

      case ESC:   // get here if p_wc != ESC or when ESC typed twice
      case Ctrl_C:
         gotesc = true;      // will free commInfo.commBuf after putting it in history
         goto returncmd;     // back to comm mode

      case Ctrl_R:         // insert register
         res = cmdline_insert_reg(&gotesc);
         if (res == GOTO_NORMAL_MODE)
            goto returncmd;
         if (res == COMMLINE_CHANGED)
            goto commlineChanged;
         goto commlineUnchanged;

      case Ctrl_D:
         if (showmatches(&xp, false, wim_flags[0] & WIM_NOSELECT) == EXPAND_NOTHING)
            break;   // Use ^D as normal char instead

         redrawcmd();
         continue;   // don't do incremental search now

      case K_RIGHT:
      case K_S_RIGHT:
      case K_C_RIGHT:
         do {
            if (commInfo.cmdpos >= commInfo.cmdlen)
               break;
            i = commlineCharsize(commInfo.cmdpos);
            if (keyWasTypedG && commInfo.cmdspos + i >= visibleColsG * visibleRowsG)
               break;
            commInfo.cmdspos += i;
            commInfo.cmdpos += utfCharLen(commInfo.commBuf + commInfo.cmdpos);
         } while ((c == K_S_RIGHT || c == K_C_RIGHT
                   || (modMaskG & (MOD_MASK_SHIFT|MOD_MASK_CTRL)))
            && commInfo.commBuf[commInfo.cmdpos] != ' ');
         set_cmdspos_cursor();
         goto commlineUnchanged;

      case K_LEFT:
      case K_S_LEFT:
      case K_C_LEFT:
         if (commInfo.cmdpos == 0)
            goto commlineUnchanged;
         do {
            --commInfo.cmdpos;
            // move to first byte of char
            commInfo.cmdpos -= (*mb_head_off)(commInfo.commBuf, commInfo.commBuf + commInfo.cmdpos);
            commInfo.cmdspos -= commlineCharsize(commInfo.cmdpos);
         }
         while (commInfo.cmdpos > 0
            && (c == K_S_LEFT || c == K_C_LEFT || (modMaskG & (MOD_MASK_SHIFT|MOD_MASK_CTRL)))
            && commInfo.commBuf[commInfo.cmdpos - 1] != ' ');
         set_cmdspos_cursor();
         goto commlineUnchanged;

      case K_IGNORE:
         // Ignore mouse event or open_cmdwin() result.
         goto commlineUnchanged;

      case K_MIDDLEDRAG:
      case K_MIDDLERELEASE:
         goto commlineUnchanged;   // Ignore mouse

      case K_MIDDLEMOUSE:
         cmdline_paste('*', true, true);
         redrawcmd();
         goto commlineChanged;

      case K_DROP:
         cmdline_paste('~', true, false);
         redrawcmd();
         goto commlineChanged;

      case K_LEFTDRAG:
      case K_LEFTRELEASE:
      case K_RIGHTDRAG:
      case K_RIGHTRELEASE:
         // Ignore drag and release events when the button-down wasn't seen before.
         if (ignore_drag_release)
             goto commlineUnchanged;
         // FALLTHROUGH
      case K_LEFTMOUSE:
      case K_RIGHTMOUSE:
         cmdline_left_right_mouse(c, &ignore_drag_release);
         goto commlineUnchanged;

      // Mouse scroll wheel: ignored here
      case K_MOUSEDOWN:
      case K_MOUSEUP:
      case K_MOUSELEFT:
      case K_MOUSERIGHT:
      // Alternate buttons ignored here
      case K_X1MOUSE:
      case K_X1DRAG:
      case K_X1RELEASE:
      case K_X2MOUSE:
      case K_X2DRAG:
      case K_X2RELEASE:
      case K_MOUSEMOVE:
         goto commlineUnchanged;

      case Ctrl_B:       // begin of command line
      case K_HOME:
      case K_KHOME:
      case K_S_HOME:
      case K_C_HOME:
         commInfo.cmdpos = 0;
         set_cmdspos();
         goto commlineUnchanged;

      case Ctrl_E:       // end of command line
      case K_END:
      case K_KEND:
      case K_S_END:
      case K_C_END:
         commInfo.cmdpos = commInfo.cmdlen;
         set_cmdspos_cursor();
         goto commlineUnchanged;

      case Ctrl_A:       // all matches
         if (cmdline_pum_active())
            // As Ctrl-A completes all the matches, close the popup menu (if present)
            cmdline_pum_cleanup(&commInfo);

         if (nextwild(OUT &xp, WILD_ALL, 0, firstc != '@') == FAIL)
            break;
         xp.context = EXPAND_NOTHING;
         did_wild_list = false;
         goto commlineChanged;

      case Ctrl_L:
         if (may_add_char_to_search(firstc, OUT &c, OUT &is_state) == OK)
            goto commlineUnchanged;

         // completion: longest common part
         if (nextwild(OUT &xp, WILD_LONGEST, 0, firstc != '@') == FAIL)
            break;
         goto commlineChanged;

      case Ctrl_N:       // next match
      case Ctrl_P:       // previous match
         if (xp.files.len > 0) {
            wild_type = (c == Ctrl_P) ? WILD_PREV : WILD_NEXT;
            if (nextwild(OUT &xp, wild_type, 0, firstc != '@') == FAIL)
               break;
            goto commlineChanged;
         }
         // FALLTHROUGH
      case K_UP:
      case K_DOWN:
      case K_S_UP:
      case K_S_DOWN:
      case K_PAGEUP:
      case K_KPAGEUP:
      case K_PAGEDOWN:
      case K_KPAGEDOWN:
         if (cmdline_pum_active()
            && (c == K_PAGEUP || c == K_PAGEDOWN || c == K_KPAGEUP || c == K_KPAGEDOWN)
         ){
            //If the popup menu is displayed, then PageUp and PageDown are used to scroll the menu
            wild_type = WILD_PAGEUP;
            if (c == K_PAGEDOWN || c == K_KPAGEDOWN)
               wild_type = WILD_PAGEDOWN;
            if (nextwild(OUT &xp, wild_type, 0, firstc != '@') == FAIL)
               break;
            goto commlineChanged;
         } else {
            res = cmdline_browse_history(c, firstc, OUT &lookfor, histype, &hiscnt, &xp);
            if (res == COMMLINE_CHANGED)
              goto commlineChanged;
            ei (res == GOTO_NORMAL_MODE)
              goto returncmd;
         }
         goto commlineUnchanged;

      case Ctrl_G:       // next match
      case Ctrl_T:       // previous match
         if (may_adjust_incsearch_highlighting(firstc, count, OUT &is_state, c) == FAIL)
            goto commlineUnchanged;
         break;

      case Ctrl_V:
      case Ctrl_Q: {
         ignore_drag_release = true;
         putcmdline('^', true);

         // Get next (two) character(s).  Do not change any
         // modifyOtherKeys ESC sequence to a normal key for CTRL-SHIFT-V.
         c = get_literal(modMaskG & MOD_MASK_SHIFT);

         do_abbr = false;       // don't do abbreviation now
         extra_char = ZERO;
         // may need to remove ^ when composing char was typed
         if (utf_iscomposing(c) && !cmd_silent) {
            draw_cmdline(commInfo.cmdpos, commInfo.cmdlen - commInfo.cmdpos);
            msg_putchar(' ');
            cursorcmd();
         }
         break;
         }
      case K_PS:
         bracketed_paste(PASTE_CMDLINE, false, NULL);
         goto commlineChanged;

      default:
         if (c == extraInterruptCharG) {
            gotesc = true;   // will free commInfo.commBuf after putting it in history
            goto returncmd;   // back to Normal mode
         }
         //Normal character with no special meaning.  Just set modMaskG
         //to 0x0 so that typing Shift-Space in the GUI doesn't enter
         //the string <S-Space>.  This should only happen after ^V.
         if (!IS_SPECIAL(c))
            modMaskG = 0x0;
         break;
      }
      // End of switch on command line character. We come here if we have a normal character.

      if (do_abbr && (IS_SPECIAL(c) || !eeIsWordc(c))
            && (ccheck_abbr(
               // Add ABBR_OFF for characters above 0x100, this is what check_abbr() expects.
                  (c >= 0x100) ? (c + ABBR_OFF) : c
                )
                || c == Ctrl_RSB)
      )
         goto commlineChanged;

      // put the character in the command line
      if (IS_SPECIAL(c) || modMaskG != 0)
         put_on_cmdline(get_special_key_name(c, modMaskG), -1, true);
      else {
         j = mb_char2bytes(c, IObuff);
         IObuff[j] = ZERO;   // exclude composing chars
         put_on_cmdline(IObuff, j, true);
      }
      goto commlineChanged;

   //This part implements incremental searches for "/" and "?"
   //Jump to commlineUnchanged when a character has been read but the command
   //line did not change. Then we only search and redraw if something changed in the past.
   //Jump to commlineChanged when the command line did change.
   //(Sorry for the goto's, I know it is ugly).
   commlineUnchanged:
      if (commInfo.cmdpos != prev_cmdpos) {
         prev_cmdpos = commInfo.cmdpos;
      }

      if (!is_state.incsearch_postponed)
         continue;

   commlineChanged:
      // If the window changed incremental search state is not valid.
      if (is_state.winid != curPor->id)
         init_incsearch_state(&is_state);
      if (xp.context == EXPAND_NOTHING && (keyWasTypedG || vpeekc() == ZERO))
         may_do_incsearch_highlighting(firstc, count, &is_state);
   }

returncmd:

   // We could have reached here without having a chance to clean up wild menu
   // if certain special keys like <Esc> or <C-\> were used as wildchar. Make
   // sure to still clean up to avoid memory corruption.
   if (cmdline_pum_active())
      cmdline_pum_remove(&commInfo, false);
   wildmenu_cleanup(&commInfo);
   did_wild_list = false;
   wim_index = 0;

   scrExpandCleanup(&xp);
   commInfo.xpc = NULL;
   clear_commlineSaved();

   finish_incsearch_highlighting(gotesc, &is_state, false);

   if (commInfo.commBuf) {
      //Put line in history buffer (":" and "=" only when it was typed).
      if (commInfo.cmdlen && firstc != ZERO && (some_key_typed || histype == HIST_SEARCH)) {
         scrAddToHistory(histype, (Text){commInfo.commBuf, commInfo.cmdlen}, true,
                      histype == HIST_SEARCH ? firstc : ZERO
         );
         if (firstc == ':') {
            eeglFreeString(newLastCommlineG);
            newLastCommlineG = copySubstr(commInfo.commBuf, commInfo.cmdlen);
         }
      }

      if (gotesc)
          abandon_cmdline();
   }

   // If the screen was shifted up, redraw the whole screen (later).
   // If the line is too long, clear it, so ruler and shown command do
   // not get printed in the middle of it.
   msg_check();
   msg_scroll = save_msg_scroll;
   redir_off = false;

   // When the command line was typed, no need for a wait-return prompt.
   if (some_key_typed)
      need_wait_return = false;

   stateG = save_State;

   if (!debug_mode)
      may_trigger_modechanged();

   setmouse();
   ui_cursor_shape();      // may show different cursor shape
   sb_text_end_cmdline();

theend:
    {
      Byte *p = commInfo.commBuf;

      --depth;
      if (did_save_ccline)
          restoreCommline(&save_ccline);
      else
          commInfo.commBuf = NULL;

      eeglFree(prev_cmdbuff);
      return p;
   }
}

//Get a command line with a prompt.
//This is prepared to be called recursively from getCommline() (e.g. by f_input() when evaluating 
//an expression from CTRL-R =). Return the command line in allocated memory, or NULL.
Arr(Byte)
getcmdline_prompt(
   Unt      firstc,
   CS prompt,   // command line prompt
   char      deco,      // decorations for prompt
   int      context,   // type of expansion
   CS completionFn)   // user-defined expansion argument
{
   Arr(Byte) s;
   CommlineInfo   save_ccline;
   int         did_save_ccline = false;
   int         msgColSaved = msgColG;
   int         msg_silent_save = msg_silent;

   if (commInfo.commBuf != NULL) {
      // Save the values of the current cmdline and restore them below.
      saveCommline(&save_ccline);
      did_save_ccline = true;
   }

   CLEAR_FIELD(commInfo);
   commInfo.cmdprompt = prompt;
   commInfo.cmdattr = deco;
   commInfo.context = context;
   commInfo.completionFn = completionFn;
   commInfo.input_fn = (firstc == '@');
   msg_silent = 0;
   s = getCommandWorker(firstc, 1L, false, false);

   if (did_save_ccline)
      restoreCommline(&save_ccline);

   msg_silent = msg_silent_save;
   // Restore msgColG, the prompt from input() may have changed it.
   // But only if called recursively and the commandline is therefore being
   // restored to an old one; if not, the input() prompt stays on the screen,
   // so we need its modified msgColG left intact.
   if (commInfo.commBuf != NULL)
      msgColG = msgColSaved;

   return s;
}

//}}}
//{{{Commline aux

//Read the 'wildmode' option, fill wim_flags[].
int
check_opt_wim(void) {
  Byte   new_wim_flags[4];
  CS p;
  int      i;
  int      idx = 0;
  if (!p_wim)
     return OK;

  for (i = 0; i < 4; ++i)
     new_wim_flags[i] = 0;

  for (p = p_wim; *p; ++p) {
     // Note: Keep this in sync with p_wim_values.
      for (i = 0; ASCII_ISALPHA(p[i]); ++i)
         ;
      if (p[i] != ZERO && p[i] != ',' && p[i] != ':')
         return FAIL;
      if (i == 7 && STRNCMP(p, "longest", 7) == 0)
         new_wim_flags[idx] |= WIM_LONGEST;
      ei (i == 4 && STRNCMP(p, "full", 4) == 0)
         new_wim_flags[idx] |= WIM_FULL;
      ei (i == 4 && STRNCMP(p, "list", 4) == 0)
         new_wim_flags[idx] |= WIM_LIST;
      ei (i == 8 && STRNCMP(p, "lastused", 8) == 0)
        new_wim_flags[idx] |= WIM_BUFLASTUSED;
      ei (i == 8 && STRNCMP(p, "noselect", 8) == 0)
         new_wim_flags[idx] |= WIM_NOSELECT;
      else
         return FAIL;
      p += i;
      if (*p == ZERO)
         break;
      if (*p == ',') {
         if (idx == 3)
            return FAIL;
         ++idx;
      }
   }

   // fill remaining entries with last flag
   while (idx < 3) {
      new_wim_flags[idx + 1] = new_wim_flags[idx];
      ++idx;
   }

   // only when there are no errors, wim_flags[] is changed
   for (i = 0; i < 4; ++i)
      wim_flags[i] = new_wim_flags[i];
   return OK;
}

// Return true when the text must not be changed and we can't switch to
// another window or buffer.  true when editing the command line, evaluating 'balloonexpr', etc.
int
text_locked(void) {
   if (commPortTypeG != 0)
      return true;
   return textlock != 0;
}

// Give an error message for a command that isn't allowed while the commline
// portal is open or editing the commline in another way.
void
text_locked_msg(void) {
   emsg(_(get_text_locked_msg()));
}

CS
get_text_locked_msg(void) {
   if (commPortTypeG != 0)
      return e_invalid_in_commline_portal;
   return e_not_allowed_to_change_text_or_change_portal;
}

// Check for text, portal or buffer locked.
// Give an error message and return true if something is locked.
int
text_or_buf_locked(void) {
   if (text_locked()) {
      text_locked_msg();
      return true;
   }
   return curBookLocked();
}

//Check if "curBookLock" or "allBookLock" is set and return true when it is and give an error msg
int
curBookLocked(void) {
   if (curBookLock > 0) {
      emsg(_(e_not_allowed_to_edit_another_buffer_now));
      return true;
   }
   return allbuf_locked();
}

// Check if "allBookLock" is set and return true when it is and give an error message.
int
allbuf_locked(void) {
   if (allBookLock > 0) {
      emsg(_(e_not_allowed_to_change_buffer_information_now));
      return true;
   }
   return false;
}

//{{{Command line stuff

private int
commlineCharsize(int idx) {
   return bookPtr2Cells(commInfo.commBuf + idx);
}

//Compute the offset of the cursor on the command line for the prompt and indent.
private void
set_cmdspos(void) {
   if (commInfo.cmdfirstc != ZERO)
      commInfo.cmdspos = 1 + commInfo.cmdindent;
   else
      commInfo.cmdspos = 0 + commInfo.cmdindent;
}

//Compute the screen position for the cursor on the command line.
private void
set_cmdspos_cursor(void) {
   int      i, m, c;

   set_cmdspos();
   if (keyWasTypedG) {
      m = visibleColsG * visibleRowsG;
      if (m < 0)   // overflow, visibleColsG or visibleRowsG at weird value
          m = MAXCOL;
   } else
      m = MAXCOL;
   for (i = 0; i < commInfo.cmdlen && i < commInfo.cmdpos; ++i) {
      c = commlineCharsize(i);
      // Count ">" for double-wide multi-byte char that doesn't fit.
      correct_cmdspos(i, c);
      // If the cmdline doesn't fit, show cursor on last visible char.
      // Don't move the cursor itself, so we can still append.
      if ((commInfo.cmdspos += c) >= m) {
         commInfo.cmdspos -= c;
         break;
      }
      i += utfCharLen(commInfo.commBuf + i) - 1;
   } 
}

//Check if the character at "idx", which is "cells" wide, is a multi-byte
//character that doesn't fit, so that a ">" must be displayed.
private void
correct_cmdspos(int idx, int cells) {
   if (utfCharLen(commInfo.commBuf + idx) > 1
         && (*mb_ptr2cells)(commInfo.commBuf + idx) > 1
         && commInfo.cmdspos % visibleColsG + cells > visibleColsG) {
      commInfo.cmdspos++;
   } 
}

// Get a command line for the ":" action
CS
getexline(
   Unt      c,      // normally ':', NUL for ":append"
   void   *cookie UNUSED,
   int      indent,      // indent for inside conditionals
   GetlineAlgo options
){
   // When executing a register, remove ':' that's in front of each line.
   if (executingFromRegG && vpeekc() == ':')
      (void)vgetc();
   return getCommline(c, 1L, indent, options);
}


// Return true if commInfo.overstrike is on.
int
cmdline_overstrike(void) {
   return commInfo.overstrike;
}

// Return true if the cursor is at the end of the cmdline.
int
cmdline_at_end(void) {
    return (commInfo.cmdpos >= commInfo.cmdlen);
}

#if defined(PROTO)
//Return the virtual column number at the current cursor position.
//This is used by the IM code to obtain the start of the preedit string.
ColNr
cmdline_getvcol_cursor(void) {
   if (commInfo.commBuf == NULL || commInfo.cmdpos > commInfo.cmdlen)
      return MAXCOL;

   ColNr   col;
   int   i = 0;

   for (col = 0; i < commInfo.cmdpos; ++col)
      i += utfCharLen(commInfo.commBuf + i);

   return col;
}
#endif

//Deallocate a command line buffer, updating the buffer size and length.
private void
deallocCommBuf(void) {
   EE_CLEAR(commInfo.commBuf);
   commInfo.cmdlen = commInfo.cmdbufflen = 0;
}

//Allocate a new command line buffer.
//Assign the new buffer to commInfo.commBuf and commInfo.cmdbufflen.
private void
allocateCommBuf(int len) {
   //give some extra space to avoid having to allocate all the time
   if (len < 80)
     len = 100;
   else
     len += 20;

   commInfo.commBuf = alloc(len);    // caller should check for out-of-memory
   commInfo.cmdbufflen = len;
}

//Re-allocate the command line to length len + something extra.
//return FAIL for failure, OK otherwise
int
reallocateCommBuf(int len) {
   if (len < commInfo.cmdbufflen)
      return OK;         // no need to resize

   // Keep a copy of the original cmdbuff and it's size so they can be restored/used later
   CS p = commInfo.commBuf;

   allocateCommBuf(len);         // will get some more
   // There isn't always a ZERO after the command, but it may need to be
   // there, thus copy up to the ZERO and add a ZERO.
   MEMMOVE(commInfo.commBuf, p, (Unt)commInfo.cmdlen);
   commInfo.commBuf[commInfo.cmdlen] = ZERO;

   if (commInfo.xpc && commInfo.xpc->input.len > 0
       && commInfo.xpc->context != EXPAND_NOTHING
       && commInfo.xpc->context != EXPAND_UNSUCCESSFUL)
    {
      int i = (int)(commInfo.xpc->input.c - p);

      // If pattern points inside the old commannd buff it needs to be adjusted
      // to point into the newly allocated memory.
      if (i >= 0 && i <= commInfo.cmdlen)
          commInfo.xpc->input = text(commInfo.commBuf + i);
   }

   eeglFree(p);

   return OK;
}

#if defined(PROTO)
private Byte   *arshape_buf = NULL;

# if defined(EXITFREE) || defined(PROTO)
void
free_arshape_buf(void) {
   eeglFree(arshape_buf);
}
# endif
#endif

//Draw part of the cmdline at the current cursor position.
private void
draw_cmdline(int start, int len) {
   msgTranslatedSlice((Text){commInfo.commBuf + start, len});
}

//Put a character on the command line.  Shifts the following text to the
//right when "shift" is true.  Used for CTRL-V, CTRL-K, etc.
//"c" must be printable (fit in one display cell)!
void
putcmdline(int c, int shift) {
   if (cmd_silent)
      return;
   msg_no_more = true;
   msg_putchar(c);
   if (shift)
      draw_cmdline(commInfo.cmdpos, commInfo.cmdlen - commInfo.cmdpos);
   msg_no_more = false;
   cursorcmd();
   extra_char = c;
   extra_char_shift = shift;
}

//Undo a putcmdline(c, false).
void
unputcmdline(void) {
   if (cmd_silent)
      return;
   msg_no_more = true;
   if (commInfo.cmdlen == commInfo.cmdpos)
      msg_putchar(' ');
   else
      draw_cmdline(commInfo.cmdpos, utfCharLen(commInfo.commBuf + commInfo.cmdpos));
   msg_no_more = false;
   cursorcmd();
   extra_char = ZERO;
}

//Put the given string, of the given length, onto the command line.
//If len is -1, then STRLEN() is used to calculate the length.
//If 'redraw' is true then the new part of the command line, and the remaining
//part will be redrawn, otherwise it will not.  If this function is called
//twice in a row, then 'redraw' should be false and redrawcmd() should be called afterwards.
int
put_on_cmdline(Byte *str, int len, int redraw) {
   int      retval;
   Unt      i;
   int      m;
   int      c;

   if (len < 0)
      len = (int)STRLEN(str);

   // Check if commInfo.commBuf needs to be longer
   if (commInfo.cmdlen + len + 1 >= commInfo.cmdbufflen)
      retval = reallocateCommBuf(commInfo.cmdlen + len + 1);
   else
      retval = OK;
      
   if (retval == OK) {
      if (!commInfo.overstrike) {
          MEMMOVE(commInfo.commBuf + commInfo.cmdpos + len,
                         commInfo.commBuf + commInfo.cmdpos,
                    (Unt)(commInfo.cmdlen - commInfo.cmdpos));
          commInfo.cmdlen += len;
      } else {
         // Count nr of characters in the new string.
         m = 0;
         for (i = 0; i < (Unt)len; i += utfCharLen(str + i))
             ++m;
         // Count nr of bytes in cmdline that are overwritten by these characters.
         for (i = commInfo.cmdpos; i < (Unt)commInfo.cmdlen && m > 0;
                i += utfCharLen(commInfo.commBuf + i))
             --m;
         if (i < (Unt)commInfo.cmdlen) {
            MEMMOVE(commInfo.commBuf + commInfo.cmdpos + len,
               commInfo.commBuf + i, (Unt)(commInfo.cmdlen - i));
            commInfo.cmdlen += commInfo.cmdpos + len - i;
         } else
            commInfo.cmdlen = commInfo.cmdpos + len;
      }
      MEMMOVE(commInfo.commBuf + commInfo.cmdpos, str, (Unt)len);
      commInfo.commBuf[commInfo.cmdlen] = ZERO;

      // When the inserted text starts with a composing character,
      // backup to the character before it.  There could be two of them.
      i = 0;
      c = mb_ptr2char(commInfo.commBuf + commInfo.cmdpos);
      while (commInfo.cmdpos > 0 && utf_iscomposing(c)) {
         i = (*mb_head_off)(commInfo.commBuf, commInfo.commBuf + commInfo.cmdpos - 1) + 1;
         commInfo.cmdpos -= i;
         len += i;
         c = mb_ptr2char(commInfo.commBuf + commInfo.cmdpos);
      }
      if (i != 0) {
         // Also backup the cursor position.
         i = bookPtr2Cells(commInfo.commBuf + commInfo.cmdpos);
         commInfo.cmdspos -= i;
         msgColG -= i;
         if (msgColG < 0) {
             msgColG += visibleColsG;
             --msgRowG;
         }
      }

      if (redraw && !cmd_silent) {
         msg_no_more = true;
         i = commlineRowG;
         cursorcmd();
         draw_cmdline(commInfo.cmdpos, commInfo.cmdlen - commInfo.cmdpos);
         // Avoid clearing the rest of the line too often.
         if (commlineRowG != i || commInfo.overstrike)
            msg_clr_eos();
         msg_no_more = false;
      }
      if (keyWasTypedG) {
          m = visibleColsG * visibleRowsG;
          if (m < 0)   // overflow, visibleColsG or visibleRowsG at weird value
         m = MAXCOL;
      } else
          m = MAXCOL;
      for (i = 0; i < (Unt)len; ++i) {
          c = commlineCharsize(commInfo.cmdpos);
         // count ">" for a double-wide char that doesn't fit.
         correct_cmdspos(commInfo.cmdpos, c);
         // Stop cursor at the end of the screen, but do increment the
         // insert position, so that entering a very long command
         // works, even though you can't see it.
         if (commInfo.cmdspos + c < m)
            commInfo.cmdspos += c;

         c = utfCharLen(commInfo.commBuf + commInfo.cmdpos) - 1;
         if (c > len - (int)i - 1)
            c = len - (int)i - 1;
         commInfo.cmdpos += c;
         i += c;
         ++commInfo.cmdpos;
      }
   }
   if (redraw)
   msg_check();
    return retval;
}

private CommlineInfo   prev_ccline;
private int      prev_ccline_used = false;

//Save commInfo, because obtaining the "=" register may execute "normal :comm"
//and overwrite it.  But get_cmdline_str() may need it, thus make it
//available globally in prev_ccline.
private void
saveCommline(CommlineInfo *ccp) {
   if (!prev_ccline_used) {
      CLEAR_FIELD(prev_ccline);
      prev_ccline_used = true;
   }
   *ccp = prev_ccline;
   prev_ccline = commInfo;
   commInfo.commBuf = NULL;  // signal that commInfo is not in use
}

//Restore commInfo after it has been saved with saveCommline().
private void
restoreCommline(CommlineInfo *ccp) {
   commInfo = prev_ccline;
   prev_ccline = *ccp;
}

//Paste a yank register into the command line.
//Used by CTRL-R command in command-line mode.
//insert_reg() can't be used here, because special characters from the
//register contents will be interpreted as commands.
//
//Return FAIL for failure, OK otherwise.
private int
cmdline_paste(
    int regname,
    int literally,   // Insert text literally instead of "as typed"
    int remcr      // remove trailing CR
){
   long      i;
   Byte      *arg;
   Byte      *p;
   int         allocated;

   // check for valid regname; also accept special characters for CTRL-R in the command line
   if (regname != Ctrl_F && regname != Ctrl_P && regname != Ctrl_W
          && regname != Ctrl_A && regname != Ctrl_L
          && !valid_yank_reg(regname, false))
      return FAIL;

   // A register containing CTRL-R can cause an endless loop.  Allow using
   // CTRL-C to break the loop.
   line_breakcheck();
   if (gotInterruptG)
      return FAIL;

   regname = may_get_selection(regname);

   // Need to set "textlock" to avoid nasty things like going to another
   // buffer when evaluating an expression.
   ++textlock;
   i = get_spec_reg(regname, &arg, &allocated, true);
   --textlock;

   if (i) {
      // Got the value of a special register in "arg".
      if (arg == NULL)
          return FAIL;

      // When 'incsearch' is set and CTRL-R CTRL-W used: skip the duplicate
      // part of the word.
      p = arg;
      if (p_is && regname == Ctrl_W) {
          Byte  *w;
          int       len;

         // Locate start of last word in the comm buffer.
         for (w = commInfo.commBuf + commInfo.cmdpos; w > commInfo.commBuf; ) {
            len = (*mb_head_off)(commInfo.commBuf, w - 1) + 1;
            if (!eeIsWordc(mb_ptr2char(w - len)))
               break;
            w -= len;
         }
         len = (int)((commInfo.commBuf + commInfo.cmdpos) - w);
         if (p_ic ? STRNICMP(w, arg, len) == 0 : STRNCMP(w, arg, len) == 0)
            p += len;
      }

      cmdline_paste_str(p, literally);
      if (allocated)
          eeglFree(arg);
      return OK;
    }

    return cmdline_paste_reg(regname, literally, remcr);
}

//Put a string on the command line.
//When "literally" is true, insert literally.
//When "literally" is false, insert as typed, but don't leave the command line.
void
cmdline_paste_str(CS s, int literally) {
   Unt      c, cv;

   if (literally)
      put_on_cmdline(s, -1, true);
   else {
      while (*s != ZERO) {
         cv = *s;
         if (cv == Ctrl_V && s[1])
            ++s;
         c = mb_cptr2char_adv(&s);
         if (cv == Ctrl_V || c == ESC || c == Ctrl_C
             || c == ENTER || c == NL || c == Ctrl_L
             || c == extraInterruptCharG
             || (c == Ctrl_BSL && *s == Ctrl_N))
         stuffcharReadbuff(Ctrl_V);
         stuffcharReadbuff(c);
      }
   } 
}

// This function is called when the screen size changes and with incremental
// search and in other situations where the command line may have been overwritten.
void
redrawCommline(void) {
   redrawCommlineEx(true);
}

//When "do_compute_cmdrow" is true the command line is redrawn at the bottom.
//If false commlineRowG is used, which should redraw in the same place.
void
redrawCommlineEx(int do_compute_cmdrow) {
   if (cmd_silent)
      return;
   need_wait_return = false;
   if (do_compute_cmdrow)
      compute_cmdrow();
   redrawcmd();
   cursorcmd();
}

private void
redrawPrompt(void) {
   int      i;

   if (cmd_silent)
   return;
   if (commInfo.cmdfirstc != ZERO)
   msg_putchar(commInfo.cmdfirstc);
   if (commInfo.cmdprompt != NULL) {
      msgPutsDeco(commInfo.cmdprompt, commInfo.cmdattr);
      commInfo.cmdindent = msgColG + (msgRowG - commlineRowG) * visibleColsG;
      // do the reverse of set_cmdspos()
      if (commInfo.cmdfirstc != ZERO)
         --commInfo.cmdindent;
   } else {
      for (i = commInfo.cmdindent; i > 0; --i)
         msg_putchar(' ');
   } 
}

// Redraw what is currently on the command line.
void
redrawcmd(void) {
   int save_inEchoPortalG = inEchoPortalG;

   if (cmd_silent)
      return;

   // when 'incsearch' is set there may be no command line while redrawing
   if (commInfo.commBuf == NULL) {
      windgoto(commlineRowG, 0);
      msg_clr_eos();
      return;
   }

   // Do not put this in the message window.
   inEchoPortalG = false;

   sb_text_restart_cmdline();
   msg_start();
   redrawPrompt();

   // Don't use more prompt, truncate the cmdline if it doesn't fit.
   msg_no_more = true;
   draw_cmdline(0, commInfo.cmdlen);
   msg_clr_eos();
   msg_no_more = false;

   set_cmdspos_cursor();
   if (extra_char != ZERO)
      putcmdline(extra_char, extra_char_shift);

   //An emsg() before may have set msg_scroll. This is used in normal mode,
   //in cmdline mode we can reset them now.
   msg_scroll = false;      // next message overwrites cmdline

   // Typing ':' at the more prompt may set skip_redraw. We don't want this in commline mode
   skip_redraw = false;

   inEchoPortalG = save_inEchoPortalG;
}

void
compute_cmdrow(void) {
   // ignore "msg_scrolled" in drawUpdateScreen(), it will be reset soon.
   if (msg_scrolled != 0 && !updating_screen)
      commlineRowG = visibleRowsG - 1;
   else
      commlineRowG = lastPor->portalRow + lastPor->height + lastPor->statusHeight;
}

void
cursorcmd(void) {
   if (cmd_silent)
      return;

   msgRowG = commlineRowG + (commInfo.cmdspos / (int)visibleColsG);
   msgColG = commInfo.cmdspos % (int)visibleColsG;
   if (msgRowG >= visibleRowsG)
       msgRowG = visibleRowsG - 1;

   windgoto(msgRowG, msgColG);
}

void
gotoCommline(int clr) {
   msg_start();
   msgColG = 0;       // always start in column 0
   if (clr)          // clear the bottom line(s)
      msg_clr_eos();       // will reset mustClearCommlineG
   windgoto(commlineRowG, 0);
}

//}}}

//Check the word in front of the cursor for an abbreviation. Called when the non-id character "c" 
//has been entered. When an abbreviation is recognized it is removed from the text with
//backspaces and the replacement string is inserted, followed by "c".
private int
ccheck_abbr(int c) {
   int spos = 0;

   if (no_abbr) 
      return false;

   //Do not consider '<,'> be part of the mapping, skip leading whitespace. Actually accept any mark
   while (SPACE_OR_TAB(commInfo.commBuf[spos]) && spos < commInfo.cmdlen)
      spos++;
   if (commInfo.cmdlen - spos > 5
          && commInfo.commBuf[spos] == '\''
          && commInfo.commBuf[spos + 2] == ','
          && commInfo.commBuf[spos + 3] == '\''
   ) {
      spos += 5;
   } else
      // check abbreviation from the beginning of the commandline
      spos = 0;

    return check_abbr(c, commInfo.commBuf, commInfo.cmdpos, spos);
}

//Escape special characters in "fname", depending on "what":
//VSE_NONE: for when used as a file name argument after an Eegl command.
//VSE_SHELL: for a shell command.
//VSE_BUFFER: for the ":buffer" command.
//Return the result in allocated memory.
CS
copyStr_fnameescape(CS fname, Unt what) {
   CS p = copyStr_escaped(fname, what == VSE_SHELL ? SHELL_ESC_CHARS
          : what == VSE_BOOK ? BUFFER_ESC_CHARS : PATH_ESC_CHARS);

   // '>' and '+' are special at the start of some commands, e.g. ":edit" and
   // ":write".  "cd -" has a special meaning.
   if (p != NULL && (*p == '>' || *p == '+' || (*p == '-' && p[1] == ZERO)))
      escape_fname(&p);

   return p;
}

// Put a backslash before the file name in "pp", which is in allocated memory.
void
escape_fname(Byte **pp) {
   CS p = alloc(STRLEN(*pp) + 2);

   p[0] = '\\';
   STRCPY(p + 1, *pp);
   eeglFree(*pp);
   *pp = p;
}

//For each file name in files[num_files]: If 'orig_pat' starts with "~/", replace the home 
//directory with "~".
void
tilde_replace(CS orig_pat, ExpandMatch* files) {
   if (orig_pat[0] == '~' && orig_pat[1] == '/') {
      for (Unt i = 0; i < files->len; ++i) {
         files->c[i] = homeReplaceA(NULL, files->c[i], files->a);
         
      }
   }
}

// Get a pointer to the current command line info.
CommlineInfo *
getCommlineInfo(void) {
   return &commInfo;
}

//Get pointer to the command line info to use. saveCommline() may clear
//commInfo and put the previous value in prev_ccline.
private CommlineInfo *
get_ccline_ptr(void) {
   if ((stateG & MODE_COMMLINE) == 0)
      return NULL;
   if (commInfo.commBuf != NULL)
      return &commInfo;
   if (prev_ccline_used && prev_ccline.commBuf != NULL)
      return &prev_ccline;
   return NULL;
}

//Get the current command-line type. Return ':' or '/' or '?' or '@' or '>' or '-'
//Only work when the command line is being edited. Return ZERO when something is wrong.
private int
getCommlineType(void) {
   CommlineInfo *p = get_ccline_ptr();

   if (p == NULL)
      return ZERO;
   if (p->cmdfirstc == ZERO)
      return (p->input_fn) ? '@' : '-';
   return p->cmdfirstc;
}

// Get the current command line in allocated memory.
// Only works when the command line is being edited. Return NULL when something is wrong.
private CS
get_cmdline_str(void) {
   CommlineInfo* p = get_ccline_ptr();
   if (p == NULL)
      return NULL;
   return copySubstr(p->commBuf, p->cmdlen);
}

// Get the current command-line completion pattern.
private Byte *
get_cmdline_completion_pattern(void) {
   CommlineInfo *p;
   int      context;

   p = get_ccline_ptr();
   if (p == NULL || p->xpc == NULL)
      return NULL;

   context = p->xpc->context;
   if (context == EXPAND_NOTHING) {
      set_expand_context(p->xpc);
      context = p->xpc->context;
      p->xpc->context = EXPAND_NOTHING;
   }
   if (context == EXPAND_UNSUCCESSFUL)
      return NULL;

   CS compl_pat = p->xpc->input.c;

   if (compl_pat == NULL)
      return NULL;

   return copyStr(compl_pat);
}

// Get the command-line completion type.
private CS
get_cmdline_completion(void) {
   CommlineInfo   *p;
   int         context;

   p = get_ccline_ptr();
   if (p == NULL || p->xpc == NULL)
      return NULL;

   context = p->xpc->context;
   if (context == EXPAND_NOTHING) {
      set_expand_context(p->xpc);
      context = p->xpc->context;
      p->xpc->context = EXPAND_NOTHING;
   }
   if (context == EXPAND_UNSUCCESSFUL)
      return NULL;

    return cmdcomplete_type_to_str(context, p->xpc->completionFn);
}

void
f_getcmdcomplpat(Arr(Var) argvars UNUSED, Var* returnVar) {
   returnVar->tag = VAR_STRING;
   returnVar->string = get_cmdline_completion_pattern();
}

void
f_getcmdcompltype(Arr(Var) argvars UNUSED, Var* returnVar) {
   returnVar->tag = VAR_STRING;
   returnVar->string = get_cmdline_completion();
}

void
f_getCommline(Arr(Var) argvars UNUSED, Var* returnVar) {
   returnVar->tag = VAR_STRING;
   returnVar->string = get_cmdline_str();
}

void
f_getcmdpos(Arr(Var) argvars UNUSED, Var* returnVar) {
   CommlineInfo *p = get_ccline_ptr();

   returnVar->number = p != NULL ? p->cmdpos + 1 : 0;
}

void
f_getcmdprompt(Arr(Var) argvars UNUSED, Var* returnVar) {
   CommlineInfo *p = get_ccline_ptr();
   returnVar->tag = VAR_STRING;
   returnVar->string = p != NULL && p->cmdprompt != NULL ? copyStr(p->cmdprompt) : NULL;
}

void
f_getcmdscreenpos(Arr(Var) argvars UNUSED, Var* returnVar) {
   CommlineInfo *p = get_ccline_ptr();

   returnVar->number = p != NULL ? p->cmdspos + 1 : 0;
}

void
f_getcmdtype(Arr(Var) argvars UNUSED, Var* returnVar) {
   returnVar->tag = VAR_STRING;
   returnVar->string = alloc(2);
   returnVar->string[0] = getCommlineType();
   returnVar->string[1] = ZERO;
}

// Set the command line str to "str".
// Return 1 when failed, 0 when OK.
private int
set_cmdline_str(Byte *str, int pos) {
   CommlineInfo  *p = get_ccline_ptr();
   int          len;

   if (p == NULL)
      return 1;

   len = (int)STRLEN(str);
   if (reallocateCommBuf(len + 1) != OK)
      return 1;
   p->cmdlen = len;
   STRCPY(p->commBuf, str);

   p->cmdpos = pos < 0 || pos > p->cmdlen ? p->cmdlen : pos;
   new_cmdpos = p->cmdpos;

   redrawcmd();

   return 0;
}

// Set the command line byte position to "pos". Zero is the first position.
// Only work when the command line is being edited. Return 1 when failed, 0 when OK.
private int
setCommlinePos(int      pos) {
   CommlineInfo *p = get_ccline_ptr();

   if (!p)
      return 1;

   // The position is not set directly but after CTRL-\ e or CTRL-R = has changed the command line.
   if (pos < 0)
      new_cmdpos = 0;
   else
      new_cmdpos = pos;

   return 0;
}

void
f_setcmdline(Arr(Var) argvars, Var* returnVar) {
   int pos = -1;

   if (check_for_string_arg(argvars, 0) == FAIL
       || check_for_opt_number_arg(argvars, 1) == FAIL)
      return;

   if (argvars[1].tag != VAR_UNKNOWN) {
      Boole error = false;

      pos = (int)varGetNumberChk(argvars + 1, OUT &error) - 1;
      if (error)
         return;
      if (pos < 0) {
         emsg(_(e_argument_must_be_positive));
         return;
      }
   }

    // Use tv_get_string() to handle a NULL string like an empty string.
    returnVar->number = set_cmdline_str(tv_get_string(&argvars[0]), pos);
}

void
f_setcmdpos(Arr(Var) argvars, Var* returnVar) {
   int pos = (int)tv_get_number(&argvars[0]) - 1;
   if (pos >= 0)
      returnVar->number = setCommlinePos(pos);
}

// The first character of the current command line.
int
get_cmdline_firstc(void) {
   return commInfo.cmdfirstc;
}

// Get indices "num1,num2" that specify a range within a list (not a range of
// text lines in a buffer!) from a string. Used for ":history" and ":clist".
// Return OK if parsed successfully, otherwise FAIL.
int
get_list_range(Byte **str, int *num1, int *num2) {
   int      len;
   int      first = false;
   Long   num;

   *str = skipwhite(*str);
   if (**str == '-' || eeIsDigit(**str)) { // parse "from" part of range
      readLongNumber(*str, NULL, &len, 0, &num, NULL, 0, false, NULL);
      *str += len;
      // overflow
      if (num > INT_MAX)
          return FAIL;

      *num1 = (int)num;
      first = true;
   }
   *str = skipwhite(*str);
   if (**str == ',') {        // parse "to" part of range
      *str = skipwhite(*str + 1);
      readLongNumber(*str, NULL, &len, 0, &num, NULL, 0, false, NULL);
      if (len > 0) {
         *str = skipwhite(*str + len);
         // overflow
         if (num > INT_MAX)
            return FAIL;

         *num2 = (int)num;
      } ei (!first)      // no number given at all
         return FAIL;
   } ei (first)         // only one number given
      *num2 = *num1;
   return OK;
}


//Open a portal on the current command line and history. Allow editing in this portal. 
//Return when the portal is closed.
//Return:
//  CR    if the command is to be executed
//  Ctrl_C    if it is to be abandoned
//  K_IGNORE if editing continues
private Unt
openCommPort(void) {
   Portal      *wp;
   int i;
   
   Portal* oldPort = curPor;
   int save_restart_edit = restart_edit;
   int save_State = stateG;

   // Can't do this when text or buffer is locked.
   // Can't do this recursively. Can't do it when typing a password.
   if (text_or_buf_locked() || commPortTypeG != 0) {
      beep_flush();
      return K_IGNORE;
   }
   
   BookRef oldBook;
   bookStoreInRef(OUT &oldBook, curBook);

   // Save current portal sizes.
   ArrayList portSizes;
   portalSaveSizes(OUT &portSizes);

   // When using completion in Insert mode with <C-R>=<C-F> one can open the
   // command line portal, but we don't want the popup menu then.
   pum_undisplay();

   // don't use a new tab 
   commModifierG.cmod_tab = 0;
   commModifierG.cmod_flags |= CMOD_NOSWAPFILE;

   // Create a portal into the command line buffer.
   if (splitPortal((int)p_cwh, WSP_BOT) == FAIL) {
      beep_flush();
      ga_clear(&portSizes);
      return K_IGNORE;
   }
   
   // splitPortal() autocommands may have messed with the old portal or buffer.
   // Treat it as abandoning this commline.
   if (!portalIsValid(oldPort) || curPor == oldPort
       || !bookRefValid(&oldBook)
       || oldPort->book != oldBook.c
   ) {
      beep_flush();
      ga_clear(&portSizes);
      return Ctrl_C;
   }
   // Don't let quitting the More prompt make this fail.
   gotInterruptG = false;

   // Set "cmdwin_..." variables before any autocommands may mess things up.
   commPortTypeG = getCommlineType();
   commPortPortG = curPor;

   // Create empty command-line buffer.  Be especially cautious of BufLeave
   // autocommands from startEditingFile(), as commport restrictions do not apply to them!
   int newbuf_status = startEditingFile(0, NULL, NULL, NULL, ECMD_ONE, ECMD_HIDE, NULL);
   int commPortValid = portalIsValid(commPortPortG);
   
   BookRef bufref;
   if (newbuf_status == FAIL || !commPortValid || curPor != commPortPortG 
         || !portalIsValid(oldPort) || !bookRefValid(&oldBook) 
         || oldPort->book != oldBook.c
   ) {
      if (newbuf_status == OK)
         bookStoreInRef(OUT &bufref, curBook);
      if (commPortValid && !lastPortal())
         closePortal(commPortPortG, true);

      // closePortal() autocommands may have already deleted the buffer.
      if (newbuf_status == OK && bookRefValid(&bufref) && bufref.c != curBook)
         closeBook(NULL, bufref.c, DOBOOK_WIPE, false, false);

      commPortTypeG = 0;
      commPortPortG = NULL;
      beep_flush();
      ga_clear(&portSizes);
      return Ctrl_C;
   }
   commPortBookG = curBook;

   optChangeAndReportError(
      S"booktype", (OptionValue){.tag = OPTION_STRING, .string = S"nofile"}, SET_LOCAL
   );
   curBook->o.modifiable = true;
   curPor->o.foldEnable = false;
   RESET_BINDING(curPor);

   // Don't allow switching to another buffer.
   ++curBookLock;

   // Showing the prompt may have set need_wait_return, reset it.
   need_wait_return = false;

   int histtype = hist_char2type(commPortTypeG);
   if (histtype == HIST_CMD || histtype == HIST_DEBUG) {
      if (p_wc == TAB) {
         // Make Tab start command-line completion: Ctrl-X Ctrl-V
         add_map(S"<book> <Tab> <C-X><C-V>", MODE_INSERT, true);
         add_map(S"<book> <Tab> a<C-X><C-V>", MODE_NORMAL, true);

         // Make S-Tab work like CTRL-P in command-line completion
         add_map(S"<book> <S-Tab> <C-P>", MODE_INSERT, true);
      }
   }
   --curBookLock;

   // Reset 'textwidth' after setting 'filetype' (the Eegl filetype plugin sets 'textwidth' to 78).
   curBook->o.textWidth = 0;

   // Fill the buffer with the history.
   init_history();
   
   LineNr lnum;
   if (getHistLen() > 0) {
      i = *get_hisidx(histtype);
      if (i >= 0) {
         lnum = 0;
         Arr(HistoryEntry) restrict history = get_histentry(histtype);
         do {
            if (++i == getHistLen())
               i = 0;
            if (history[i].hisstr != NULL) {
               ml_append(lnum, history[i].hisstr, (ColNr)0, false);
               lnum++;
            } 
         }
         while (i != *get_hisidx(histtype));
      }
   }

   // Replace the empty last line with the current command-line and put the cursor there.
   ml_replace(curBook->mem.lineCount, commInfo.commBuf, true);
   curPor->cursor.lnum = curBook->mem.lineCount;
   curPor->cursor.col = commInfo.cmdpos;
   changed_line_abv_curs();
   invalidate_botline();
   redraw_later(UPD_SOME_VALID);

   stateG = MODE_NORMAL;
   setmouse();

   // Reset here so it can be set by a CommPortEnter autocommand.
   commPortResultG = 0;

   // Trigger CommPortEnter autocommands.
   trigger_cmd_autocmd(commPortTypeG, EVENT_COMMPORTENTER);
   if (restart_edit != 0)   // autocmd with ":startinsert"
      stuffcharReadbuff(K_NOP);

   int save_isRedrawingDisabledG = isRedrawingDisabledG;
   isRedrawingDisabledG = 0;

   // Call the main loop until <CR> or CTRL-C is typed.
   mainLoop(true);

   isRedrawingDisabledG = save_isRedrawingDisabledG;

   int save_keyWasTypedG = keyWasTypedG;

   // Trigger CommPortLeave autocommands.
   trigger_cmd_autocmd(commPortTypeG, EVENT_COMMPORTLEAVE);

   // Restore keyWasTypedG in case it is modified by autocommands
   keyWasTypedG = save_keyWasTypedG;

   commPortTypeG = 0;
   commPortBookG = NULL;
   commPortPortG = NULL;

   // Safety check: The old portal or buffer was changed or deleted: It's a bug if this happens!
   if (!portalIsValid(oldPort) || !bookRefValid(&oldBook) || oldPort->book != oldBook.c) {
      commPortResultG = Ctrl_C;
      emsg(_(e_active_window_or_buffer_changed_or_deleted));
   } else {
      //Autocommands may abort script processing
      if (aborting() && commPortResultG != K_IGNORE)
         commPortResultG = Ctrl_C;
      // Set the new command line from the commline buffer.
      deallocCommBuf();

      if (commPortResultG == K_XF1 || commPortResultG == K_XF2) { // :qa[!] typed
         Byte  *p = (CS)"qa";       // assume commPortResultG == K_XF2
         Unt  plen = 2;

         if (commPortResultG == K_XF1) {
            p = (CS)"qa!";
            plen = 3;
         }

         if (histtype == HIST_CMD) {
            // Execute the command directly.
            commInfo.commBuf = copySubstr(p, plen);
            commInfo.cmdlen = (int)plen;
            commInfo.cmdbufflen = (int)(plen + 1);
            commPortResultG = ENTER;
         } else {
            // First need to cancel what we were doing.
            stuffcharReadbuff(':');
            stuffReadbuff((CS)p);
            stuffcharReadbuff(ENTER);
         }
      } ei (commPortResultG == Ctrl_C)    {
         // :q or :close, don't execute any command and don't modify the comm portal.
         commInfo.commBuf = NULL;
      } else {
         commInfo.cmdlen = ml_get_curline_len();
         commInfo.cmdbufflen = commInfo.cmdlen + 1;
         commInfo.commBuf = copySubstr(ml_get_curline(), commInfo.cmdlen);
      }

      if (commInfo.commBuf == NULL)    {
         commInfo.commBuf = copySubstr((CS)"", 0);
         commInfo.cmdlen = 0;
         commInfo.cmdbufflen = 1;
         commInfo.cmdpos = 0;
         commPortResultG = Ctrl_C;
      } else {
         commInfo.cmdpos = curPor->cursor.col;
         // If the cursor is on the last character, it probably should be after it.
         if (commInfo.cmdpos == commInfo.cmdlen - 1 || commInfo.cmdpos > commInfo.cmdlen)
            commInfo.cmdpos = commInfo.cmdlen;
      }

      // First go back to the original portal.
      wp = curPor;
      bookStoreInRef(OUT &bufref, curBook);

      skipPortFixCursorG = true;
      gotoPortal(oldPort);

      // gotoPortal() may trigger an autocommand that already closes the commline portal.
      if (portalIsValid(wp) && wp != curPor)
          closePortal(wp, true);

      //closePortal() may have already wiped the buffer when 'bh' is
      //set to 'wipe', autocommands may have closed other portals
      if (bookRefValid(&bufref) && bufref.c != curBook)
         closeBook(NULL, bufref.c, DOBOOK_WIPE, false, false);

      portRestoreSize(&portSizes);
      skipPortFixCursorG = false;

      if (commPortResultG == K_IGNORE) {
          // It can be confusing that the comm port still shows, redraw the screen.
          drawUpdateScreen(UPD_VALID);
          set_cmdspos_cursor();
          redrawcmd();
      }
   }

   ga_clear(&portSizes);
   restart_edit = save_restart_edit;

   stateG = save_State;
   may_trigger_modechanged();
   setmouse();

   return commPortResultG;
}

// Return true if in the commport, not editing the command line.
int
inCommPort(void) {
   return commPortTypeG != 0 && getCommlineType() == ZERO;
}

//Used for commands that either take a simple command string argument, or:
//  comm << endmarker
//    {script}
//  endmarker
//Return a pointer to allocated memory with {script} or NULL.
CS
script_get(Invocation* invo, Byte *comm UNUSED) {
   List   *l;
   ListItem   *li;
   Byte   *s;
   ArrayList   ga;

   if (comm[0] != '<' || comm[1] != '<' || invo->ea_getline == NULL)
      return NULL;
   comm += 2;

   l = heredoc_get(invo, comm, true);
   if (l == NULL)
      return NULL;

   ga_init2(&ga, 1, 0x400);

   FOR_ALL_LIST_ITEMS(l, li) {
      s = tv_get_string(&li->c);
      ga_concat(&ga, s);
      ga_append(&ga, '\n');
   }
   ga_append(&ga, ZERO);

    list_free(l);
    return (CS)ga.c;
}

//This function is used by f_input() and f_inputdialog() functions. The third
//argument to f_input() specifies the type of completion to use at the
//prompt. The third argument to f_inputdialog() specifies the value to return
//when the user cancels the prompt.
void
get_user_input(
    Var   *argvars,
    Var   *returnVar,
    int      inputdialog,
    int      secret
){
   Byte   *prompt;
   Byte   *p = NULL;
   int      c;
   Byte   builder[NUMBUFLEN];
   int      cmd_silent_save = cmd_silent;
   Byte   *defstr = (CS)"";
   int      xp_type = EXPAND_NOTHING;
   Byte   *completionFn = NULL;

   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;
   if (input_busy)
       return;  // this doesn't work recursively.

   prompt = convertVarToStringSingleUse(&argvars[0]);

   cmd_silent = false;      // Want to see the prompt.
   if (prompt != NULL) {
      // Only the part of the message after the last NL is considered as
      // prompt for the command line
      p = lastOccurrence(prompt, '\n');
      if (p == NULL) {
          p = prompt;
      } else {
          ++p;
          c = *p;
          *p = ZERO;
          msg_start();
          msg_clr_eos();
          msgPutsDeco(prompt, get_echo_attr());
          msg_didout = false;
          msg_starthere();
          *p = c;
      }
      commlineRowG = msgRowG;

      if (argvars[1].tag != VAR_UNKNOWN)    {
         defstr = convertVarToString(&argvars[1], builder);
         if (defstr != NULL)
            stuffReadbuffSpec(defstr);

         if (!inputdialog && argvars[2].tag != VAR_UNKNOWN) {
            Byte   *xp_name;
            int   xp_namelen;
            long   argFlags = 0;

            // input() with a third argument: completion
            returnVar->string = NULL;

            xp_name = convertVarToString(&argvars[2], builder);
            if (xp_name == NULL)
                return;

            xp_namelen = (int)STRLEN(xp_name);

            if (parse_compl_arg(xp_name, xp_namelen, &xp_type, &argFlags,
                                &completionFn) == FAIL)
                return;
         }
      }

      if (defstr != NULL)    {
          int save_ex_normal_busy = ex_normal_busy;
          int save_vgetcBusyG = vgetcBusyG;
          int save_input_busy = input_busy;

          input_busy |= vgetcBusyG;
          ex_normal_busy = 0;
          vgetcBusyG = 0;
          returnVar->string = getcmdline_prompt(
                secret ? ZERO : '@', p, get_echo_attr(), xp_type, completionFn
          );
          ex_normal_busy = save_ex_normal_busy;
          vgetcBusyG = save_vgetcBusyG;
          input_busy = save_input_busy;
      }
      if (inputdialog && returnVar->string == NULL
            && argvars[1].tag != VAR_UNKNOWN
            && argvars[2].tag != VAR_UNKNOWN)
         returnVar->string = copyStr(tv_get_string_buf( &argvars[2], builder));

      eeglFree(completionFn);

      // since the user typed this, no need to wait for return
      need_wait_return = false;
      msg_didout = false;
    }
    cmd_silent = cmd_silent_save;
}

void
f_wildtrigger(Arr(Var) argvars UNUSED, Var* returnVar UNUSED) {
   if (!(stateG & MODE_COMMLINE) || char_avail() || wild_menu_showing || cmdline_pum_active())
      return;

   int cmd_type = getCommlineType();

   if (cmd_type == ':' || cmd_type == '/' || cmd_type == '?')     {
   // Add K_WILD as a single special key
   Byte   key_string[4];

   key_string[0] = K_SPECIAL;
   key_string[1] = KS_EXTRA;
   key_string[2] = KE_WILD;
   key_string[3] = ZERO;

   // Insert it into the typeahead buffer
   insertIntoTypebuf(key_string, REMAP_NONE, 0, true, false);
   }
}

//}}}
//{{{user commands

typedef struct ucmd {
   CS uc_name;   // The command name
   Unt   uc_namelen;   // The length of the command name (excluding the ZERO)
   Ulong   uc_argt;   // The argument type
   CS uc_rep;   // The command's replacement string
   long   uc_def;      // The default value for a range/count
   int uc_compl;   // completion type
   CommandAddress   uc_addr_type;   // The command's address type
   ScriptPos   uc_scriptCtx;   // SCTX where the command was defined
   int uc_flags;   // some UC_ flags
   CS uc_compl_arg;   // completion argument if any
} UserCommand;

// List of all user commands.
private ArrayList userComms = {0, 0, sizeof(UserCommand), 4, NULL};

// When non-zero it is not allowed to add or remove user commands
private int ucmd_locked = 0;

#define USER_CMD(i) (&((UserCommand *)(userComms.c))[i])
#define USER_CMD_GA(gap, i) (&((UserCommand *)((gap)->c))[i])

// flags used by user commands and :autocmd
#define UC_BUFFER   1   // -buffer: local to current buffer

// Flags used by find_func_even_dead()
#define FFED_IS_GLOBAL 1   // "g:" was used
#define FFED_NO_GLOBAL 2   // only check for script-local functions

// List of names for completion for ":command" with the EXPAND_ flag.
// Must be alphabetical on the 'value' field for completion and because it is used by bsearch()!
private Kv command_complete_tab[] = {
   KEYVALUE_ENTRY(EXPAND_ARGLIST, "arglist"),
   KEYVALUE_ENTRY(EXPAND_AUGROUP, "augroup"),
   KEYVALUE_ENTRY(EXPAND_BREAKPOINT, "breakpoint"),
   KEYVALUE_ENTRY(EXPAND_BUFFERS, "buffer"),
   KEYVALUE_ENTRY(EXPAND_COLORS, "color"),
   KEYVALUE_ENTRY(EXPAND_COMMANDS, "command"),
   KEYVALUE_ENTRY(EXPAND_COMPILER, "compiler"),
   KEYVALUE_ENTRY(EXPAND_CSCOPE, "cscope"),
   KEYVALUE_ENTRY(EXPAND_USER_DEFINED, "custom"),
   KEYVALUE_ENTRY(EXPAND_USER_LIST, "customlist"),
   KEYVALUE_ENTRY(EXPAND_DIFF_BUFFERS, "diff_buffer"),
   KEYVALUE_ENTRY(EXPAND_DIRECTORIES, "dir"),
   KEYVALUE_ENTRY(EXPAND_DIRS_IN_CDPATH, "dir_in_path"),
   KEYVALUE_ENTRY(EXPAND_ENV_VARS, "environment"),
   KEYVALUE_ENTRY(EXPAND_EVENTS, "event"),
   KEYVALUE_ENTRY(EXPAND_EXPRESSION, "expression"),
   KEYVALUE_ENTRY(EXPAND_FILES, "file"),
   KEYVALUE_ENTRY(EXPAND_FILES_IN_PATH, "file_in_path"),
   KEYVALUE_ENTRY(EXPAND_FILETYPE, "filetype"),
   KEYVALUE_ENTRY(EXPAND_FILETYPECMD, "filetypecmd"),
   KEYVALUE_ENTRY(EXPAND_FUNCTIONS, "function"),
   KEYVALUE_ENTRY(EXPAND_HELP, "help"),
   KEYVALUE_ENTRY(EXPAND_HILITE_GROUP, "highlight"),
   KEYVALUE_ENTRY(EXPAND_HISTORY, "history"),
   KEYVALUE_ENTRY(EXPAND_LOCALES, "locale"),
   KEYVALUE_ENTRY(EXPAND_MAPCLEAR, "mapclear"),
   KEYVALUE_ENTRY(EXPAND_MAPPINGS, "mapping"),
   KEYVALUE_ENTRY(EXPAND_MESSAGES, "messages"),
   KEYVALUE_ENTRY(EXPAND_OPTION, "option"),
   KEYVALUE_ENTRY(EXPAND_PACKADD, "packadd"),
   KEYVALUE_ENTRY(EXPAND_RETAB, "retab"),
   KEYVALUE_ENTRY(EXPAND_RUNTIME, "runtime"),
   KEYVALUE_ENTRY(EXPAND_SCRIPTNAMES, "scriptnames"),
   KEYVALUE_ENTRY(EXPAND_SHELLCMD, "shellcmd"),
   KEYVALUE_ENTRY(EXPAND_SHELLCMDLINE, "shellcmdline"),
   KEYVALUE_ENTRY(EXPAND_SIGN, "sign"),
   KEYVALUE_ENTRY(EXPAND_OWNSYNTAX, "syntax"),
   KEYVALUE_ENTRY(EXPAND_TAGS, "tag"),
   KEYVALUE_ENTRY(EXPAND_TAGS_LISTFILES, "tag_listfiles"),
   KEYVALUE_ENTRY(EXPAND_USER, "user"),
   KEYVALUE_ENTRY(EXPAND_USER_VARS, "var")
};

typedef struct {
   CommandAddress key;
   CS fullname;
   Unt fullnamelen;
   CS shortname;
   Unt shortnamelen;
} AddrTypeSpec;

//List of names of address types.  Must be alphabetical for completion.
//Must be sorted by the 'fullname' field because it is used by bsearch()!
#define ADDRTYPE_ENTRY(k, fn, sn) \
   {(k), (fn), STRLEN_LITERAL(fn), (sn), STRLEN_LITERAL(sn)}
private AddrTypeSpec addr_type_complete_tab[] = {
   ADDRTYPE_ENTRY(ADDR_ARGUMENTS, S"arguments", S"arg"),
   ADDRTYPE_ENTRY(ADDR_BUFFERS, S"buffers", S"buf"),
   ADDRTYPE_ENTRY(ADDR_LINES, S"lines", S"line"),
   ADDRTYPE_ENTRY(ADDR_LOADED_BUFFERS, S"loaded_buffers", S"load"),
   ADDRTYPE_ENTRY(ADDR_OTHER, S"other", S"?"),
   ADDRTYPE_ENTRY(ADDR_PORTALS, S"portals", S"port"),
   ADDRTYPE_ENTRY(ADDR_QUICKFIX, S"quickfix", S"qf"),
   ADDRTYPE_ENTRY(ADDR_TABS, S"tabs", S"tab")
};

private int cmp_addr_type(const void *a, const void *b);

// Search for a user command that matches "invo->comm".
// Return id in "invo->id", flags in "invo->argFlags", idx in "invo->useridx".
// Return a pointer to just after the command.
// Return NULL if there is no matching command.
CS
find_ucmd(
   Invocation   *invo,
   Byte   *p,    // end of the command (possibly including count)
   int      *full,    // set to true for a full match
   Expand   *xp,    // used for completion, NULL otherwise
   OUT Unt* context // completion flags or NULL
){
   int      len = (int)(p - invo->comm);
   int      j, k, matchlen = 0;
   UserCommand   *uc;
   int found = false;
   int possible = false;
   CS cp;  // Typed command
   CS np;    // Test name
   ArrayList* gap;
   int      amb_local = false;  // Found ambiguous buffer-local command,
                // only full match global is accepted.

   // Look for buffer-local user commands first, then global ones.
   gap = &prevPor_curPor()->book->userCommands;
   for (;;) {
      for (j = 0; j < gap->len; ++j) {
         uc = USER_CMD_GA(gap, j);
         cp = invo->comm;
         np = uc->uc_name;
         k = 0;
         while (k < len && *np != ZERO && *cp++ == *np++)
            k++;
         if (k == len || (*np == ZERO && eeIsDigit(invo->comm[k]))) {
            // If finding a second match, the command is ambiguous.  But
            // not if a buffer-local command wasn't a full match and a
            // global command is a full match.
            if (k == len && found && *np != ZERO) {
               if (gap == &userComms)
                  return NULL;
               amb_local = true;
            }

            if (!found || (k == len && *np == ZERO)) {
               // If we matched up to a digit, then there could
               // be another command including the digit that we
               // should use instead.
               if (k == len)
                  found = true;
               else
                  possible = true;

               if (gap == &userComms)
                  invo->id = C_USER;
               else
                  invo->id = C_USER_BUF;
               invo->argFlags = (long)uc->uc_argt;
               invo->useridx = j;
               invo->addressKind = uc->uc_addr_type;

               if (context != NULL)
                  *context = uc->uc_compl;
               if (xp) {
                  xp->completionFn = uc->uc_compl_arg;
                  xp->scriptCtx = uc->uc_scriptCtx;
                  xp->scriptCtx.lineNr += SOURCING_LNUM;
                }
               // Do not search for further abbreviations if this is an exact match.
               matchlen = k;
               if (k == len && *np == ZERO) {
                  if (full)
                     *full = true;
                  amb_local = false;
                  break;
               }
            }
         }
      }

      // Stop if we found a full match or searched all.
      if (j < gap->len || gap == &userComms)
          break;
      gap = &userComms;
   }

   // Only found ambiguous matches.
   if (amb_local) {
      if (xp)
         xp->context = EXPAND_UNSUCCESSFUL;
      return NULL;
   }

   // The match we found may be followed immediately by a number. Move "p" back to point to it.
   if (found || possible)
      return p + (matchlen - len);
   return p;
}

// Set completion context for :command
CS
set_context_in_user_cmd(Expand *xp, CS arg_in) {
   CS arg = arg_in;
   CS p;

   // Check for attributes
   while (*arg == '-') {
      arg++;       // Skip "-"
      p = skiptowhite(arg);
      if (*p == ZERO) {
         // Cursor is still in the decorations
         p = firstOccurrence(arg, '=');
         if (p == NULL) {
            // No "=", so complete decoration names
            xp->context = EXPAND_USER_CMD_FLAGS;
            xp->input = text(arg);
            return NULL;
         }

         // For the -complete, -nargs and -addr attributes, we complete
         // their arguments as well.
         if (STRNICMP(arg, "complete", p - arg) == 0) {
            xp->context = EXPAND_USER_COMPLETE;
            xp->input = text(p + 1);
         } ei (STRNICMP(arg, "nargs", p - arg) == 0) {
            xp->context = EXPAND_USER_NARGS;
            xp->input = text(p + 1);
         } ei (STRNICMP(arg, "addr", p - arg) == 0) {
            xp->context = EXPAND_USER_ADDR_TYPE;
            xp->input = text(p + 1);
         }
         return NULL;
      }
      arg = skipwhite(p);
   }

   // After the attributes comes the new command name
   p = skiptowhite(arg);
   if (*p == ZERO) {
      xp->context = EXPAND_USER_COMMANDS;
      xp->input = text(arg);
      return NULL;
   }

   // And finally comes a normal command
   return skipwhite(p);
}

// Set the completion context for the argument of a user defined command.
CS
set_context_in_user_cmdarg(
   CS comm UNUSED,
   CS arg,
   long argFlags,
   Unt context,
   Expand* xp,
   Boole forceit
) {
   if (context == EXPAND_NOTHING)
      return NULL;

   if (argFlags & XFILE) {
      // XFILE: file names are handled before this call
      return NULL;
   }

   if (context == EXPAND_COMMANDS)
      return arg;
   if (context == EXPAND_MAPPINGS)
      return set_context_in_map_cmd(xp, (CS)"map", arg, forceit, false, false, C_map);
      
   // Find start of last argument.
   CS p = arg;
   while (*p) {
      if (*p == ' ')
          // argument starts after a space
          arg = p + 1;
      ei (*p == '\\' && *(p + 1) != ZERO)
          ++p; // skip over escaped character
      MB_PTR_ADV(p);
   }
   xp->input = text(arg);
   xp->context = context;

   return NULL;
}

CS
expand_user_command_name(int idx) {
   return get_user_commands(NULL, idx - (int)COUNT_COMMANDS);
}

// Function given to expandGeneric() to obtain the list of user command names.
CS
get_user_commands(Expand* xp UNUSED, int idx) {
   // In commPort, the alternative buffer should be used.
   Book* book = prevPor_curPor()->book;

   if (idx < book->userCommands.len)
      return USER_CMD_GA(&book->userCommands, idx)->uc_name;

   idx -= book->userCommands.len;
   if (idx < userComms.len) {
      CS name = USER_CMD(idx)->uc_name;

      for (int i = 0; i < book->userCommands.len; ++i) {
         if (STRCMP(name, USER_CMD_GA(&book->userCommands, i)->uc_name) == 0)
            // global command is overruled by buffer-local one
            return S"";
      } 
      return name;
   }
   return NULL;
}

// Get the name of user command "idx".  "id" can be C_USER or C_USER_BUF.
// Return NULL if the command is not found.
CS
get_user_command_name(int idx, int id) {
   if (id == C_USER && idx < userComms.len)
      return USER_CMD(idx)->uc_name;
   if (id == C_USER_BUF) {
      // In commPort, the alternative buffer should be used.
      Book *book = prevPor_curPor()->book;

      if (idx < book->userCommands.len)
         return USER_CMD_GA(&book->userCommands, idx)->uc_name;
   }
   return NULL;
}

// Function given to expandGeneric() to obtain the list of user address type names.
CS
get_user_cmd_addr_type(Expand *xp UNUSED, int idx) {
   if (idx < 0 || idx >= (int)ARRAY_LENGTH(addr_type_complete_tab))
      return NULL;
   return (CS)addr_type_complete_tab[idx].fullname;
}

// Function given to expandGeneric() to obtain the list of user command attributes.
CS
get_user_cmd_flags(Expand *xp UNUSED, int idx) {
   static CS user_cmd_flags[] = {SMAP((CS),
      "addr", "bang", "bar", "buffer", "complete",
      "count", "nargs", "range", "register", "keepscript"
   )};

   if (idx < 0 || idx >= (int)ARRAY_LENGTH(user_cmd_flags))
      return NULL;
   return (CS)user_cmd_flags[idx];
}

// Function given to expandGeneric() to obtain the list of values for -nargs.
CS
get_user_cmd_nargs(Expand *xp UNUSED, int idx) {
   static CS user_cmd_nargs[] = {SMAP((CS), "0", "1", "*", "?", "+" )};

   if (idx < 0 || idx >= (int)ARRAY_LENGTH(user_cmd_nargs))
      return NULL;
   return (CS)user_cmd_nargs[idx];
}

// Function given to expandGeneric() to obtain the list of values for complete.
CS
get_user_cmd_complete(Expand *xp UNUSED, int idx) {
   if (idx < 0 || idx >= (int)ARRAY_LENGTH(command_complete_tab))
      return NULL;
   return command_complete_tab[idx].value.c;
}

// Return the row in the command_complete_tab table that contains the given key.
private Kv *
get_commandtype(int expand) {
   for (int i = 0; i < (int)ARRAY_LENGTH(command_complete_tab); ++i) {
      if (command_complete_tab[i].key == expand)
          return &command_complete_tab[i];
   } 

   return NULL;
}

// Get the name of completion type "expand" as an allocated string.
// "compl_arg" is the function name for "custom" and "customlist" types.
// Return NULL if no completion is available or on allocation failure.
CS
cmdcomplete_type_to_str(int expand, CS compl_arg) {
   Kv* kv = get_commandtype(expand);
   if (!kv || kv->value.c == NULL)
      return NULL;

   CS cmd_compl = kv->value.c;
   if (expand == EXPAND_USER_LIST || expand == EXPAND_USER_DEFINED) {
      CS builder = alloc(STRLEN(cmd_compl) + STRLEN(compl_arg) + 2);
      SPRINTF(builder, "%s,%s", cmd_compl, compl_arg);
      return builder;
   }

   return copyStr(cmd_compl);
}

// Get the index of completion type "complete_str". Return EXPAND_NOTHING if no match found.
int
cmdcomplete_str_to_type(Byte *complete_str) {
   Kv target;
   Kv *entry;
   static Kv *last_entry = NULL;   // cached result

   if (STRNCMP(complete_str, "custom,", 7) == 0)
      return EXPAND_USER_DEFINED;
   if (STRNCMP(complete_str, "customlist,", 11) == 0)
      return EXPAND_USER_LIST;

   target.key = 0;
   target.value.c = complete_str;
   target.value.len = 0;         // not used, see cmp_keyvalue_value()

   if (last_entry != NULL && cmp_keyvalue_value(&target, last_entry) == 0)
      entry = last_entry;
   else {
      entry = (Kv *)bsearch(&target,
          &command_complete_tab,
          ARRAY_LENGTH(command_complete_tab),
          sizeof(command_complete_tab[0]),
          cmp_keyvalue_value);
      if (entry == NULL)
          return EXPAND_NOTHING;

      last_entry = entry;
   }

   return entry->key;
}

// List user commands starting with "name[name_len]".
private void
uc_list(CS name, Unt name_len) {
   int      i, j;
   int      found = false;
   UserCommand   *comm;
   int      len;
   int      over;
   long   a;
   Kv   *entry;

   // don't allow for adding or removing user commands here
   ++ucmd_locked;

   // In commPort, the alternative buffer should be used.
   ArrayList* gap = &prevPor_curPor()->book->userCommands;
   for (;;) {
      for (i = 0; i < gap->len; ++i) {
         comm = USER_CMD_GA(gap, i);
         a = (long)comm->uc_argt;

         // Skip commands which don't match the requested prefix and
         // commands filtered out.
         if (STRNCMP(name, comm->uc_name, name_len) != 0 || message_filtered(comm->uc_name))
            continue;

         // Put out the title first time
         if (!found)
            msg_puts_title(_("\n    Name              Args Address Complete    Definition"));
         found = true;
         msg_putchar('\n');
         if (gotInterruptG)
            break;

         // Special cases
         len = 4;
         if (a & BANG) {
            msg_putchar('!');
            --len;
         }
         if (a & REGSTR) {
            msg_putchar('"');
            --len;
         }
         if (gap != &userComms) {
            msg_putchar('b');
            --len;
         }
         if (a & TRLBAR) {
            msg_putchar('|');
            --len;
         }
         if (len != 0)
            msg_puts((CS)&"    "[4 - len]);

         msgOuttransDeco(comm->uc_name, getDecoFlags(HLF_D));
         len = (int)comm->uc_namelen + 4;

         if (len < 21) {
            // Field padding spaces   12345678901234567
            static Byte spaces[18] = "                 ";
            msg_puts(&spaces[len - 4]);
            len = 21;
         }
         msg_putchar(' ');
         ++len;

         //"over" is how much longer the name is than the column width for
         //the name, we'll try to align what comes after.
         over = len - 22;
         len = 0;

         // Arguments
         switch ((int)(a & (EXTRA|NOSPC_IN_EXTRA|NEEDARG))) {
         case 0:            IObuff[len++] = '0'; break;
         case (EXTRA):      IObuff[len++] = '*'; break;
         case (EXTRA|NOSPC_IN_EXTRA):   IObuff[len++] = '?'; break;
         case (EXTRA|NEEDARG):   IObuff[len++] = '+'; break;
         case (EXTRA|NOSPC_IN_EXTRA|NEEDARG): IObuff[len++] = '1'; break;
         }

         do {
            IObuff[len++] = ' ';
         } while (len < 5 - over);

         // Address / Range
         if (a & (RANGE|COUNT)) {
            if (a & COUNT) {
               // -count=N
               len += eeSnprintf(IObuff + len, IOSIZE - len, "%ldc", comm->uc_def);
            } ei (a & DFLALL)
               IObuff[len++] = '%';
            ei (comm->uc_def >= 0) {
               // -range=N
               len += eeSnprintf(IObuff + len, IOSIZE - len, "%ld", comm->uc_def);
            } else
               IObuff[len++] = '.';
          }

          do {
            IObuff[len++] = ' ';
         } while (len < 8 - over);

         // Address Type
         for (j = 0; j < (int)ARRAY_LENGTH(addr_type_complete_tab); ++j) {
            if (addr_type_complete_tab[j].key != ADDR_LINES
                  && addr_type_complete_tab[j].key == comm->uc_addr_type
            ){
               STRCPY(IObuff + len, addr_type_complete_tab[j].shortname);
               len += (int)addr_type_complete_tab[j].shortnamelen;
               break;
            }
         } 

         do {
            IObuff[len++] = ' ';
         } while (len < 13 - over);

         // Completion
         entry = get_commandtype(comm->uc_compl);
         if (entry != NULL) {
         STRCPY(IObuff + len, entry->value.c);
         len += (int)entry->value.len;
         if (p_verbose > 0 && comm->uc_compl_arg != NULL) {
             Unt uc_compl_arglen = STRLEN(comm->uc_compl_arg);

             if (uc_compl_arglen < 200) {
            IObuff[len++] = ',';
            STRCPY(IObuff + len, comm->uc_compl_arg);
            len += (int)uc_compl_arglen;
             }
         }
         }

         do {
            IObuff[len++] = ' ';
         } while (len < 25 - over);

         IObuff[len] = ZERO;
         msg_outtrans(IObuff);

         msg_outtrans_special(comm->uc_rep, false, name_len == 0 ? visibleColsG - 47 : 0);
         if (p_verbose > 0)
            lastSetMsg(comm->uc_scriptCtx);
         out_flush();
         ui_breakcheck();
         if (gotInterruptG)
            break;
      }
      if (gap == &userComms || i < gap->len)
         break;
      gap = &userComms;
   }

   if (!found)
      msg(_("No user-defined commands found"));

   --ucmd_locked;
}
CS
uc_fun_cmd(void) {
   static Byte fcmd[] = {0x84, 0xaf, 0x60, 0xb9, 0xaf, 0xb5, 0x60, 0xa4,
             0xa5, 0xad, 0xa1, 0xae, 0xa4, 0x60, 0xa1, 0x60,
             0xb3, 0xa8, 0xb2, 0xb5, 0xa2, 0xa2, 0xa5, 0xb2,
             0xb9, 0x7f, 0};
   int      i;

   for (i = 0; fcmd[i]; ++i)
      IObuff[i] = fcmd[i] - 0x40;
   IObuff[i] = ZERO;
   return IObuff;
}

// Parse address type argument
private int
parse_addr_type_arg(CS value, int vallen, CommandAddress* addr_type_arg) {
   AddrTypeSpec target;
   AddrTypeSpec *entry;
   static AddrTypeSpec *last_entry;   // cached result

   target.key = 0;
   target.fullname = value;
   target.fullnamelen = vallen;

   if (last_entry != NULL && cmp_addr_type(&target, last_entry) == 0)
      entry = last_entry;
   else {
      entry = (AddrTypeSpec *)bsearch(
         &target, &addr_type_complete_tab, ARRAY_LENGTH(addr_type_complete_tab),
         sizeof(addr_type_complete_tab[0]), cmp_addr_type
      );
      if (!entry) {
         int i;
         CS err = value;

         for (i = 0; err[i] != ZERO && !SPACE_OR_TAB(err[i]); i++)
            {} 
         err[i] = ZERO;
         showErrFmtMsg(_(e_invalid_address_type_value_str), err);
         return FAIL;
      }

      last_entry = entry;
   }

   *addr_type_arg = entry->key;

   return OK;
}

private int
cmp_addr_type(const void *a, const void *b) {
   AddrTypeSpec *at1 = (AddrTypeSpec *)a;
   AddrTypeSpec *at2 = (AddrTypeSpec *)b;

   return STRNCMP(at1->fullname, at2->fullname, MAX(at1->fullnamelen, at2->fullnamelen));
}

// Parse a completion argument "value[vallen]".
// The detected completion goes in "*context", argument type in "*argFlags".
// When there is an argument, for function and user defined completion, it's copied to allocated 
// memory and stored in "*compl_arg". Return FAIL if something is wrong.
int
parse_compl_arg(
   CS value,
   int vallen,
   int* context,
   long* argFlags,
   OUT CS* compl_arg
) {
   CS arg = NULL;
   Unt arglen = 0;
   int i;
   int valend = vallen;
   Kv target;
   Kv* entry;
   static Kv* last_entry = NULL;       // cached result

   // Look for any argument part - which is the part after any ','
   for (i = 0; i < vallen; ++i) {
      if (value[i] == ',') {
         arg = &value[i + 1];
         arglen = vallen - i - 1;
         valend = i;
         break;
      }
   }

   target.key = 0;
   target.value.c = value;
   target.value.len = valend;

   if (last_entry != NULL && cmp_keyvalue_value_n(&target, last_entry) == 0)
      entry = last_entry;
   else {
      entry = (Kv *)bsearch(&target,
          &command_complete_tab,
          ARRAY_LENGTH(command_complete_tab),
          sizeof(command_complete_tab[0]),
          cmp_keyvalue_value_n);
      if (entry == NULL) {
          showErrFmtMsg(_(e_invalid_complete_value_str), value);
          return FAIL;
      }

      last_entry = entry;
   }

   *context = entry->key;
   if (*context == EXPAND_BUFFERS)
      *argFlags |= BUFNAME;
   ei (*context == EXPAND_DIRECTORIES || *context == EXPAND_FILES || *context == EXPAND_SHELLCMDLINE)
      *argFlags |= XFILE;

   if (*context != EXPAND_USER_DEFINED && *context != EXPAND_USER_LIST &&  arg != NULL) {
      emsg(_(e_completion_argument_only_allowed_for_custom_completion));
      return FAIL;
   }

   if ((*context == EXPAND_USER_DEFINED || *context == EXPAND_USER_LIST) && arg == NULL) {
      emsg(_(e_custom_completion_requires_function_argument));
      return FAIL;
   }

   if (arg != NULL)
      *compl_arg = copySubstr(arg, arglen);

   return OK;
}

// Scan attributes in the ":command" command. Return FAIL when something is wrong.
private int
uc_scan_attr(
   CS attr,
   Unt len,
   long* argFlags,
   long* def,
   int* flags,
   int* context,
   OUT CS* compl_arg,
   CommandAddress* addr_type_arg
) {
   CS p;

   if (len == 0) {
      emsg(_(e_no_attribute_specified));
      return FAIL;
   }

   // First, try the simple attributes (no arguments)
   if (STRNICMP(attr, "bang", len) == 0)
      *argFlags |= BANG;
   ei (STRNICMP(attr, "buffer", len) == 0)
      *flags |= UC_BUFFER;
   ei (STRNICMP(attr, "register", len) == 0)
      *argFlags |= REGSTR;
   ei (STRNICMP(attr, "keepscript", len) == 0)
      *argFlags |= KEEPSCRIPT;
   ei (STRNICMP(attr, "bar", len) == 0)
      *argFlags |= TRLBAR;
   else {
      int   i;
      CS val = NULL;
      Unt   vallen = 0;
      Unt   attrlen = len;

      // Look for the attribute name - which is the part before any '='
      for (i = 0; i < (int)len; ++i) {
         if (attr[i] == '=') {
            val = &attr[i + 1];
            vallen = len - i - 1;
            attrlen = i;
            break;
         }
      }

      if (STRNICMP(attr, "nargs", attrlen) == 0) {
         if (vallen == 1) {
            if (*val == '0')
                // Do nothing - this is the default
                ;
            ei (*val == '1')
               *argFlags |= (EXTRA | NOSPC_IN_EXTRA | NEEDARG);
            ei (*val == '*')
               *argFlags |= EXTRA;
            ei (*val == '?')
               *argFlags |= (EXTRA | NOSPC_IN_EXTRA);
            ei (*val == '+')
               *argFlags |= (EXTRA | NEEDARG);
            else
               goto wrong_nargs;
         } else {
   wrong_nargs:
            emsg(_(e_invalid_number_of_arguments));
            return FAIL;
          }
      } ei (STRNICMP(attr, "range", attrlen) == 0) {
         *argFlags |= RANGE;
         if (vallen == 1 && *val == '%')
            *argFlags |= DFLALL;
         ei (val != NULL) {
            p = val;
            if (*def >= 0) {
      two_count:
                emsg(_(e_count_cannot_be_specified_twice));
                return FAIL;
            }

            *def = parseLong(&p);
            *argFlags |= ZERO_LINE_OK;

            if (p != val + vallen || vallen == 0) {
      invalid_count:
                emsg(_(e_invalid_default_value_for_count));
                return FAIL;
            }
         }
         // default for -range is using buffer lines
         if (*addr_type_arg == ADDR_NONE)
            *addr_type_arg = ADDR_LINES;
      } ei (STRNICMP(attr, "count", attrlen) == 0) {
         *argFlags |= (COUNT | ZERO_LINE_OK | RANGE);
         // default for -count is using any number
         if (*addr_type_arg == ADDR_NONE)
            *addr_type_arg = ADDR_OTHER;

         if (val) {
            p = val;
            if (*def >= 0)
               goto two_count;

            *def = parseLong(&p);

            if (p != val + vallen)
                goto invalid_count;
         }

         if (*def < 0)
            *def = 0;
      } ei (STRNICMP(attr, "complete", attrlen) == 0) {
         if (!val) {
            showErrFmtMsg(_(e_argument_required_for_str), "-complete");
            return FAIL;
         }

         if (parse_compl_arg(val, (int)vallen, context, argFlags, compl_arg) == FAIL)
            return FAIL;
      } ei (STRNICMP(attr, "addr", attrlen) == 0) {
         *argFlags |= RANGE;
         if (!val) {
            showErrFmtMsg(_(e_argument_required_for_str), "-addr");
            return FAIL;
         }
         if (parse_addr_type_arg(val, (int)vallen, addr_type_arg) == FAIL)
            return FAIL;
         if (*addr_type_arg != ADDR_LINES)
            *argFlags |= ZERO_LINE_OK;
      } else {
         Byte ch = attr[len];
         attr[len] = '\0';
         showErrFmtMsg(_(e_invalidDecorationStr), attr);
         attr[len] = ch;
         return FAIL;
      }
   }

   return OK;
}

//// Add a user command to the list or replace an existing one.
//private int
//uc_add_command(
//   Byte   *name,
//   Unt   name_len,
//   Byte   *rep,
//   long   argFlags,
//   long   def,
//   int      flags,
//   int      compl,
//   Byte   *compl_arg UNUSED,
//   CommandAddress   addr_type,
//   int      force
//){
//   UserCommand   *comm = NULL;
//   Byte   *p;
//   int      i;
//   int      cmp = 1;
//   Byte   *rep_buf = NULL;
//   ArrayList   *gap;
//
//   replace_termcodes(rep, &rep_buf, 0, 0, NULL, false);
//   if (!rep_buf) {
//      // can't replace termcodes - try using the string as is
//      rep_buf = copyStr(rep);
//
//      // give up if out of memory
//      if (!rep_buf)
//         return FAIL;
//   }
//
//   // get address of growarray: global or in curBook
//   if (flags & UC_BUFFER) {
//      gap = &curBook->userCommands;
//      if (gap->ga_itemsize == 0)
//         ga_init2(gap, sizeof(UserCommand), 4);
//   } else
//      gap = &userComms;
//
//   // Search for the command in the already defined commands.
//   for (i = 0; i < gap->len; ++i) {
//      comm = USER_CMD_GA(gap, i);
//      cmp = STRNCMP(name, comm->uc_name, name_len);
//      if (cmp == 0) {
//          if (name_len < comm->uc_namelen)
//         cmp = -1;
//          ei (name_len > comm->uc_namelen)
//         cmp = 1;
//      }
//
//      if (cmp == 0) {
//         // Command can be replaced with "command!" and when sourcing the
//         // same script again, but only once.
//         if (!force
//             && (comm->uc_scriptCtx.sid != scriptPosG.sid
//              || comm->uc_scriptCtx.seq == scriptPosG.seq)
//         ){
//            showErrFmtMsg(_(e_command_already_exists_add_bang_to_replace_it_str), name);
//            goto fail;
//         }
//
//         EE_CLEAR(comm->uc_rep);
//         EE_CLEAR(comm->uc_compl_arg);
//         break;
//      }
//
//      // Stop as soon as we pass the name to add
//      if (cmp < 0)
//         break;
//   }
//
//   // Extend the array unless we're replacing an existing command
//   if (cmp != 0) {
//      if (ga_grow(gap, 1) == FAIL)
//          goto fail;
//      if ((p = copySubstr(name, name_len)) == NULL)
//          goto fail;
//
//      comm = USER_CMD_GA(gap, i);
//      MEMMOVE(comm + 1, comm, (gap->len - i) * sizeof(UserCommand));
//
//      ++gap->len;
//
//      comm->uc_name = p;
//      comm->uc_namelen = name_len;
//   }
//
//   comm->uc_rep = rep_buf;
//   comm->uc_argt = argFlags;
//   comm->uc_def = def;
//   comm->uc_compl = compl;
//   comm->uc_scriptCtx = scriptPosG;
//   comm->uc_scriptCtx.lineNr += SOURCING_LNUM;
//   comm->uc_compl_arg = compl_arg;
//   comm->uc_addr_type = addr_type;
//
//   return OK;
//
//fail:
//   eeglFree(rep_buf);
//   eeglFree(compl_arg);
//   return FAIL;
//}

// ":command ..." implementation
void
c_command(Invocation* invo) {
   CS name;
   CS end;
   long argFlags = 0;
   long def = -1;
   int flags = 0;
   int compl = EXPAND_NOTHING;
   CS compl_arg = NULL;
   CommandAddress   addr_type_arg = ADDR_NONE;
   int has_attr = (invo->arg[0] == '-');
   int name_len;

   CS p = invo->arg;

   // Check for attributes
   while (*p == '-') {
      ++p;
      end = skiptowhite(p);
      if (uc_scan_attr(p, end - p, &argFlags, &def, &flags, &compl,
                     &compl_arg, &addr_type_arg) == FAIL)
         goto theend;
      p = skipwhite(end);
   }

   // Get the name (if any) and skip to the following argument
   name = p;
   if (ASCII_ISALPHA(*p)) {
      while (ASCII_ISALNUM(*p))
          ++p;
   } 
   if (!endsComm(p) && !SPACE_OR_TAB(*p)) {
      emsg(_(e_invalid_command_name));
      goto theend;
   }
   end = p;
   name_len = (int)(end - name);

   // If there is nothing after the name, and no decorations were specified,
   // we are listing commands
   p = skipwhite(end);
   if (!has_attr && endsComm(p)) {
      uc_list(name, name_len);
   } ei (!ASCII_ISUPPER(*name)) {
      emsg(_(e_user_defined_commands_must_start_with_an_uppercase_letter));
   } ei ((name_len == 1 && *name == 'X')
        || (name_len <= 4 && STRNCMP(name, "Next", name_len > 4 ? 4 : name_len) == 0)
   ) {
      emsg(_(e_reserved_name_cannot_be_used_for_user_defined_command));
   } ei (compl > 0 && (argFlags & EXTRA) == 0) {
      // Some plugins rely on silently ignoring the mistake
          give_warning_with_source(
                (CS)_(e_complete_used_without_allowing_arguments), true, true);
   }

theend:
   eeglFree(compl_arg);
}

// ":comclear" implementation Clear all user commands, global and for current buffer.
void
c_comclear(Invocation* invo UNUSED) {
   uc_clear(&userComms);
   if (curBook)
      uc_clear(&curBook->userCommands);
}

// If ucmd_locked is set give an error and return true. Otherwise return false.
private int
is_ucmd_locked(void) {
   if (ucmd_locked > 0) {
      emsg(_(e_cannot_change_user_commands_while_listing));
      return true;
   }
   return false;
}

// Clear all user commands for "gap".
void
uc_clear(ArrayList *gap) {
   if (is_ucmd_locked())
      return;

   for (int i = 0; i < gap->len; ++i) {
      UserCommand* comm = USER_CMD_GA(gap, i);
      eeglFree(comm->uc_name);
      comm->uc_namelen = 0;
      eeglFree(comm->uc_rep);
      eeglFree(comm->uc_compl_arg);
   }
   ga_clear(gap);
}

// ":delcommand" implementation
void
c_delcommand(Invocation* invo) {
   int      i = 0;
   UserCommand   *comm = NULL;
   int      res = -1;
   CS arg = invo->arg;
   int buffer_only = false;

   if (STRNCMP(arg, "-buffer", 7) == 0 && SPACE_OR_TAB(arg[7])) {
      buffer_only = true;
      arg = skipwhite(arg + 7);
   }

   ArrayList* gap = &curBook->userCommands;
   for (;;) {
      for (i = 0; i < gap->len; ++i) {
         comm = USER_CMD_GA(gap, i);
         res = STRCMP(arg, comm->uc_name);
         if (res <= 0)
            break;
      }
      if (gap == &userComms || res == 0 || buffer_only)
         break;
      gap = &userComms;
   }

   if (res != 0) {
      showErrFmtMsg(_(buffer_only
             ? e_no_such_user_defined_command_in_current_buffer_str
             : e_no_such_user_defined_command_str), arg);
      return;
   }

   if (is_ucmd_locked())
      return;

   eeglFree(comm->uc_name);
   eeglFree(comm->uc_rep);
   eeglFree(comm->uc_compl_arg);

   --gap->len;

   if (i < gap->len)
      MEMMOVE(comm, comm + 1, (gap->len - i) * sizeof(UserCommand));
}

// Split and quote args for <f-args>.
private CS
uc_split_args(CS arg, Unt *lenp) {
   CS q;

   // Precalculate length
   CS p = arg;
   int len = 2; // Initial and final quotes

   while (*p) {
      if (p[0] == '\\' && p[1] == '\\') {
         len += 2;
         p += 2;
      } ei (p[0] == '\\' && SPACE_OR_TAB(p[1])) {
         len += 1;
         p += 2;
      } ei (*p == '\\' || *p == '"') {
         len += 2;
         p += 1;
      } ei (SPACE_OR_TAB(*p)) {
         p = skipwhite(p);
         if (*p == ZERO)
            break;
         len += 4; // ", "
      } else {
         int charlen = utfCharLen(p);

         len += charlen;
         p += charlen;
      }
   }

   CS builder = alloc(len + 1);

   p = arg;
   q = builder;
   *q++ = '"';
   while (*p) {
      if (p[0] == '\\' && p[1] == '\\') {
         *q++ = '\\';
         *q++ = '\\';
         p += 2;
      } ei (p[0] == '\\' && SPACE_OR_TAB(p[1])) {
         *q++ = p[1];
         p += 2;
      } ei (*p == '\\' || *p == '"') {
         *q++ = '\\';
         *q++ = *p++;
      } ei (SPACE_OR_TAB(*p)) {
         p = skipwhite(p);
         if (*p == ZERO)
            break;
         *q++ = '"';
         *q++ = ',';
         *q++ = ' ';
         *q++ = '"';
      } else {
         MB_COPY_CHAR(p, q);
      }
   }
   *q++ = '"';
   *q = ZERO;

   *lenp = len;
   return builder;
}

private Unt
add_cmd_modifier(
   CS buf,
   Unt buflen,
   CS mod_str,
   Unt   mod_strlen,
   int      *multi_mods
) {
   if (buf) {
      if (*multi_mods) {
         STRCPY(buf + buflen, " ");   // the separating space
         ++buflen;
      }
      STRCPY(buf + buflen, mod_str);
   }

   if (*multi_mods)
      ++mod_strlen;         // +1 for the separating space
   else
      *multi_mods = 1;

   return mod_strlen;
}

// Add modifiers from "cmod->cmod_split" to "builder".  Set "multi_mods" when one
// was added.  Return the number of bytes added.
Unt
add_win_cmd_modifiers(CS builder, CommandModifier* cmod, int* multi_mods) {
   Unt buflen = 0;

   // :aboveleft and :leftabove
   if (cmod->cmod_split & WSP_ABOVE) {
      buflen += add_cmd_modifier(
         builder, buflen, S"aboveleft", STRLEN_LITERAL("aboveleft"), multi_mods
      );
   } 
   // :belowright and :rightbelow
   if (cmod->cmod_split & WSP_BELOW) {
      buflen += add_cmd_modifier(
         builder, buflen, S"belowright", STRLEN_LITERAL("belowright"), multi_mods
      );
   } 
   // :botright
   if (cmod->cmod_split & WSP_BOT) {
      buflen += add_cmd_modifier(
         builder, buflen, S"botright", STRLEN_LITERAL("botright"), multi_mods
      );
   } 

   // :tab
   if (cmod->cmod_tab > 0) {
      Unt tabnr = cmod->cmod_tab - 1;

      if (tabnr == indexOfTab(curtab)) {
         // For compatibility, don't add a tabpage number if it is the same
         // as the default number for :tab.
         buflen += add_cmd_modifier(
                builder, buflen, S"tab", STRLEN_LITERAL("tab"), multi_mods
         );
      } else {
          Byte tab_buf[NUMBUFLEN + 3];
          Unt tab_buflen;

          tab_buflen = eeSnprintf(tab_buf, sizeof(tab_buf), "%dtab", tabnr);
          buflen += add_cmd_modifier(builder, buflen, tab_buf, tab_buflen, multi_mods);
      }
    }

   // :topleft
   if (cmod->cmod_split & WSP_TOP)
      buflen += add_cmd_modifier(builder, buflen, S"topleft", STRLEN_LITERAL("topleft"), multi_mods);
   // :vertical
   if (cmod->cmod_split & WSP_VERT)
      buflen += add_cmd_modifier(
         builder, buflen, S"vertical", STRLEN_LITERAL("vertical"), multi_mods
      );
   // :horizontal
   if (cmod->cmod_split & WSP_HOR) {
      buflen += add_cmd_modifier(
         builder, buflen, S"horizontal", STRLEN_LITERAL("horizontal"), multi_mods
      );
   } 

   return buflen;
}

// Generate text for the "cmod" command modifiers. If "buf" is NULL just return the length.
private Unt
produceCommModifiers(CS builder, CommandModifier *cmod, int quote) {
   Unt  buflen = 0;
   int       multi_mods = 0;
   int       i;
   static Kv mod_entry_tab[] = {
      KEYVALUE_ENTRY(CMOD_BROWSE, "browse"),
      KEYVALUE_ENTRY(CMOD_CONFIRM, "confirm"),
      KEYVALUE_ENTRY(CMOD_HIDE, "hide"),
      KEYVALUE_ENTRY(CMOD_KEEPALT, "keepalt"),
      KEYVALUE_ENTRY(CMOD_KEEPJUMPS, "keepjumps"),
      KEYVALUE_ENTRY(CMOD_KEEPMARKS, "keepmarks"),
      KEYVALUE_ENTRY(CMOD_KEEPPATTERNS, "keeppatterns"),
      KEYVALUE_ENTRY(CMOD_LOCKMARKS, "lockmarks"),
      KEYVALUE_ENTRY(CMOD_NOSWAPFILE, "noswapfile"),
      KEYVALUE_ENTRY(CMOD_UNSILENT, "unsilent"),
      KEYVALUE_ENTRY(CMOD_NOAUTOCMD, "noautocmd"),
   };

   if (quote) {
      ++buflen;
      if (builder) {
         *builder = '"';
         *(builder + buflen) = ZERO;
      }
   } ei (builder)
      *builder = ZERO;

   // the modifiers that are simple flags
   for (i = 0; i < (int)ARRAY_LENGTH(mod_entry_tab); ++i) {
      if (cmod->cmod_flags & mod_entry_tab[i].key) {
         buflen += add_cmd_modifier(
             builder, buflen, mod_entry_tab[i].value.c, mod_entry_tab[i].value.len, 
             &multi_mods
         );
      } 
   } 

   // :silent
   if (cmod->cmod_flags & CMOD_SILENT) {
      if (cmod->cmod_flags & CMOD_ERRSILENT)
         buflen += add_cmd_modifier(builder, buflen, S"silent!",
             STRLEN_LITERAL("silent!"), &multi_mods);
      else
         buflen += add_cmd_modifier(builder, buflen, S"silent",
             STRLEN_LITERAL("silent"), &multi_mods);
   }

    // :verbose
   if (cmod->cmod_verbose > 0) {
      int verbose_value = cmod->cmod_verbose - 1;

      if (verbose_value == 1) {
         buflen += add_cmd_modifier(
               builder, buflen, S"verbose", STRLEN_LITERAL("verbose"), &multi_mods
         );
      } else {
         Byte verbose_buf[NUMBUFLEN];
         Unt verbose_buflen;

         verbose_buflen = eeSnprintf(verbose_buf, sizeof(verbose_buf), "%dverbose", verbose_value);
         buflen += add_cmd_modifier(builder, buflen, verbose_buf, verbose_buflen, &multi_mods);
      }
   }

   // flags from cmod->cmod_split
   buflen += add_win_cmd_modifiers(builder ? builder + buflen : null, cmod, &multi_mods);

   if (quote) {
      if (!builder)
         ++buflen;
      else {
         *(builder + buflen) = '"';
         ++buflen;
         *(builder + buflen) = ZERO;
      }
   }

   return buflen;
}

//Check for a <> code in a user command.
//"code" points to the '<'.  "len" the length of the <> (inclusive).
//"buf" is where the result is to be added.
//"split_buf" points to a buffer used for splitting, caller should free it.
//"split_len" is the length of what "split_buf" contains.
//Return the length of the replacement, which has been added to "buf".
//Return -1 if there was no match, and only the "<" has been copied.
private Unt
uc_check_code(
   Byte   *code,
   Unt   len,
   Byte   *buf,
   UserCommand   *comm,      // the user command we're expanding
   Invocation   *invo,      // ex arguments
   Byte   **split_buf,
   Unt   *split_len)
{
   Unt   result = 0;
   Byte   *p = code + 1;
   Unt   l = len - 2;
   int      quote = 0;
   enum {
      ct_ARGS,
      ct_BANG,
      ct_COUNT,
      ct_LINE1,
      ct_LINE2,
      ct_RANGE,
      ct_MODS,
      ct_REGISTER,
      ct_LT,
      ct_NONE
   } type = ct_NONE;

   if ((firstOccurrence((CS)"qQfF", *p) != NULL) && p[1] == '-') {
      quote = (*p == 'q' || *p == 'Q') ? 1 : 2;
      p += 2;
      l -= 2;
   }

   ++l;
   if (l <= 1)
      type = ct_NONE;
   ei (STRNICMP(p, "args>", l) == 0)
      type = ct_ARGS;
   ei (STRNICMP(p, "bang>", l) == 0)
      type = ct_BANG;
   ei (STRNICMP(p, "count>", l) == 0)
      type = ct_COUNT;
   ei (STRNICMP(p, "line1>", l) == 0)
      type = ct_LINE1;
   ei (STRNICMP(p, "line2>", l) == 0)
      type = ct_LINE2;
   ei (STRNICMP(p, "range>", l) == 0)
      type = ct_RANGE;
   ei (STRNICMP(p, "lt>", l) == 0)
      type = ct_LT;
   ei (STRNICMP(p, "reg>", l) == 0 || STRNICMP(p, "register>", l) == 0)
      type = ct_REGISTER;
   ei (STRNICMP(p, "mods>", l) == 0)
      type = ct_MODS;

   switch (type) {
   case ct_ARGS:
   // Simple case first
   if (*invo->arg == ZERO) {
      if (quote == 1) {
         result = 2;
         if (buf != NULL)
             STRCPY(buf, "''");
      } else
         result = 0;
      break;
   }

   // When specified there is a single argument don't split it.
   // Works for ":Cmd %" when % is "a b c".
   if ((invo->argFlags & NOSPC_IN_EXTRA) && quote == 2)
      quote = 1;

   switch (quote) {
   case 0: // No quoting, no splitting
       result = STRLEN(invo->arg);
       if (buf != NULL)
      STRCPY(buf, invo->arg);
       break;
   case 1: // Quote, but don't split
      result = STRLEN(invo->arg) + 2;
      for (p = invo->arg; *p; ++p) {
          if (*p == '\\' || *p == '"')
          ++result;
      }

      if (buf != NULL) {
      *buf++ = '"';
      for (p = invo->arg; *p; ++p) {
         if (*p == '\\' || *p == '"')
            *buf++ = '\\';
         *buf++ = *p;
      }
      *buf = '"';
      }

       break;
   case 2: // Quote and split (<f-args>)
       // This is hard, so only do it once, and cache the result
       if (*split_buf == NULL)
      *split_buf = uc_split_args(invo->arg, split_len);

       result = *split_len;
       if (buf != NULL && result != 0)
      STRCPY(buf, *split_buf);

       break;
   }
   break;

   case ct_BANG:
      result = invo->forceit ? 1 : 0;
      if (quote)
          result += 2;
      if (buf != NULL) {
          if (quote)
         *buf++ = '"';
          if (invo->forceit)
         *buf++ = '!';
          if (quote)
         *buf = '"';
      }
      break;

    case ct_LINE1:
    case ct_LINE2:
    case ct_RANGE:
    case ct_COUNT: {
      Byte num_buf[NUMBUFLEN];
      long num = (type == ct_LINE1) ? invo->line1 :
            (type == ct_LINE2) ? invo->line2 :
            (type == ct_RANGE) ? invo->addr_count :
            (invo->addr_count > 0) ? invo->line2 : comm->uc_def;
      Unt num_len;

      num_len = eeSnprintf(num_buf, sizeof(num_buf), "%ld", num);
      result = num_len;

      if (quote)
          result += 2;

      if (buf != NULL) {
          if (quote)
         *buf++ = '"';
          STRCPY(buf, num_buf);
          buf += num_len;
          if (quote)
         *buf = '"';
      }

      break;
   }

   case ct_MODS: {
      result = produceCommModifiers(buf, &commModifierG, quote);
      break;
   }

   case ct_REGISTER:
      result = invo->regname ? 1 : 0;
      if (quote)
          result += 2;
      if (buf != NULL) {
         if (quote)
            *buf++ = '\'';
         if (invo->regname)
            *buf++ = invo->regname;
         if (quote)
            *buf = '\'';
      }
      break;

   case ct_LT:
      result = 1;
      if (buf != NULL)
         *buf = '<';
      break;

   default:
      // Not recognized: just copy the '<' and return -1.
      result = (Unt)-1;
      if (buf != NULL)
          *buf = '<';
      break;
   }

   return result;
}

// Execute a user defined command.
void
do_ucmd(Invocation* invo) {
   Byte   *p;
   Byte   *q;

   Byte   *start;
   Byte   *end = NULL;
   Byte   *ksp;
   Unt   len, totlen;

   Unt   split_len = 0;
   Byte   *split_buf = NULL;
   UserCommand   *comm;
   ScriptPos   save_scriptPosG;
   int      restore_scriptPosG = false;

   if (invo->id == C_USER)
      comm = USER_CMD(invo->useridx);
   else
      comm = USER_CMD_GA(&prevPor_curPor()->book->userCommands, invo->useridx);

   //Replace <> in the command by the arguments.
   //First round: "builder" is NULL, compute length, allocate "builder".
   //Second round: copy result into "buf".
   CS builder = NULL;
   for (;;) {
      p = comm->uc_rep;    // source
      q = builder;       // destination
      totlen = 0;

      for (;;) {
         start = firstOccurrence(p, '<');
         if (start)
            end = firstOccurrence(start + 1, '>');
         if (builder) {
            for (ksp = p; *ksp != ZERO && *ksp != K_SPECIAL; ++ksp)
               {}
            if (*ksp == K_SPECIAL
               && (start == NULL || ksp < start || end == NULL)
               && ((ksp[1] == KS_SPECIAL && ksp[2] == KE_FILLER)))
            {
               // K_SPECIAL has been put in the buffer as K_SPECIAL KS_SPECIAL KE_FILLER, like for 
               // mappings, but doCommand() doesn't handle that, so convert it back.
               // Also change K_SPECIAL KS_EXTRA KE_CSI into CSI.
               len = ksp - p;
               if (len > 0) {
                  MEMMOVE(q, p, len);
                  q += len;
               }
               *q++ = ksp[1] == KS_SPECIAL ? K_SPECIAL : CSI;
               p = ksp + 3;
               continue;
            }
         }

         // break if no <item> is found
         if (start == NULL || end == NULL)
            break;

         // Include the '>'
         ++end;

         // Take everything up to the '<'
         len = start - p;
         if (!builder)
            totlen += len;
         else {
            MEMMOVE(q, p, len);
            q += len;
         }

         len = uc_check_code(start, end - start, q, comm, invo, &split_buf, &split_len);
         if (len == (Unt)-1) {
            // no match, continue after '<'
            p = start + 1;
            len = 1;
         } else
            p = end;
         if (!builder)
            totlen += len;
         else
            q += len;
      }
      if (builder) {       // second time here, finished
         STRCPY(q, p);
         break;
      }

      totlen += STRLEN(p);       // Add on the trailing characters
      builder = alloc(totlen + 1);
   }

   if ((comm->uc_argt & KEEPSCRIPT) == 0) {
      restore_scriptPosG = true;
      save_scriptPosG = scriptPosG;
      scriptPosG.sid = comm->uc_scriptCtx.sid;
   }

   (void)doCommand(builder, invo->ea_getline, invo->cookie, DOCMD_VERBOSE|DOCMD_NOWAIT|DOCMD_KEYTYPED);

   // Careful: Do not use "comm" here, it may have become invalid if a user command was added.
   if (restore_scriptPosG) {
      scriptPosG = save_scriptPosG;
   }
   eeglFree(builder);
   eeglFree(split_buf);
}

//}}}
//{{{user functions

// structure used as item in "fc_defer"
typedef struct {
   Arr(Byte) dr_name;   // function name, allocated
   Var dr_argvars[MAX_FUNC_ARGS + 1];
   int argc;
} Deferral;

// Struct used by trans_function_name()
struct FuncDict {
   Bag* bag;   // Dictionary used
   CS newKey;   // new key in "dict" in allocated memory
   DictItem* item;      // Dictionary item used
};

// fixed buffer length for fname_trans_sid()
#define FLEN_FIXED 40


// All user-defined functions are found in this hashtable.
private EeSet userDefinedFnsS;

// Used by get_func_tv()
private ArrayList funcargs = GA_EMPTY;

// pointer to funccal for currently active function
private FnCall *currentCallS = NULL;

// Pointer to list of previously used funccal, still around because some
// item in it is still being used.
private FnCall *previous_funccal = NULL;

private void funccal_unref(FnCall *fc, UserFunc *fp, int force);
private void func_clear(UserFunc *fp, int force);
private int func_free(UserFunc *fp, int force);
private void applyDeferred(FnCall *funccal);
private CS trans_function_name_ext(
   OUT CS* pp, OUT Boole *is_global, Boole skip, Unt flags, FuncDict *fdp, 
   PartiallyApplied **partial, TypeSpec **type, UserFunc **ufunc
);

void
func_init(void) {
   hash_init(&userDefinedFnsS);
}

// Return the function hash table
EeSet *
func_tbl_get(void) {
   return &userDefinedFnsS;
}

// Get one function argument.
// If "evalarg" is not NULL use it to check for an already declared name.
// If "invo" is not NULL use it to check for an already declared name.
// Return a pointer to after the type. When something is wrong, return "arg".
private CS
one_function_arg(
   CS arg,
   ArrayList* newargs,
   int skip
) {
   CS p = arg;
   CS arg_copy = NULL;

   while (ASCII_ISALNUM(*p) || *p == '_')
      ++p;
      
   if (arg == p || SAFE_isdigit(*arg)
       || ((p - arg == 9 && STRNCMP(arg, "firstline", 9) == 0)
                || (p - arg == 8 && STRNCMP(arg, "lastline", 8) == 0))
   ){
      if (!skip)
         showErrFmtMsg(_(e_illegal_argument_str), arg);
      return arg;
   }

   if (newargs != NULL && ga_grow(newargs, 1) == FAIL)
      return arg;
   if (newargs != NULL) {
      int c;
      int i;

      c = *p;
      *p = ZERO;
      arg_copy = copyStr(arg);
      // Check for duplicate argument name.
      for (i = 0; i < newargs->len; ++i) {
         if (STRCMP(((Byte **)(newargs->c))[i], arg_copy) == 0) {
            showErrFmtMsg(_(e_duplicate_argument_name_str), arg_copy);
            eeglFree(arg_copy);
            return arg;
         }
      } 
      ((Byte **)(newargs->c))[newargs->len] = arg_copy;
      newargs->len++;

      *p = c;
   }

   return p;
}

// Handle line continuation in function arguments or body.
// Get a next line, store it in "invo" if appropriate and put the line in
// "lines_to_free" to free the line later.
private CS
get_function_line(
   Invocation      *invo,
   ArrayList   *lines_to_free,
   int      indent,
   GetlineAlgo   getline_options)
{
   CS theline;

   if (invo->ea_getline == NULL)
      theline = getCommline(':', 0L, indent, 0);
   else
      theline = invo->ea_getline(':', invo->cookie, indent, getline_options);
   if (theline != NULL) {
      if (lines_to_free->len > 0
            && invo->commline != NULL
            && *invo->commline == ((Byte **)lines_to_free->c)[lines_to_free->len - 1]
      )
         *invo->commline = theline;
      (void)ga_add_string(lines_to_free, theline);
    }

    return theline;
}

//Get function arguments. "argp" should point to just after the "(", possibly to white space. 
//"argp" is advanced just after "endchar".
private int
get_function_args(
   Byte   **argp,
   Byte   endchar,
   ArrayList   *newargs,
   int      *varargs,
   ArrayList   *default_args,
   int      skip,
   Invocation   *invo,      // can be NULL
   ArrayList   *lines_to_free)
{
   int      mustend = false;
   Byte   *arg;
   Byte   *p;
   int      c;
   int      any_default = false;
   Byte   *whitep = *argp;
   int      need_expr = false;

   if (newargs != NULL)
      ga_init2(newargs, sizeof(CS), 3);
   if (!skip && default_args != NULL)
      ga_init2(default_args, sizeof(CS), 3);

   if (varargs != NULL)
      *varargs = false;

   // Isolate the arguments: "arg1, arg2, ...)"
   arg = skipwhite(*argp);
   p = arg;
   while (*p != endchar) {
      while (invo != NULL && invo->ea_getline != NULL
             && (*p == ZERO || (SPACE_OR_TAB(*whitep) && *p == '#'))
      ){
         // End of the line, get the next one.
         Byte *theline = get_function_line(invo, lines_to_free, 0, GETLINE_CONCAT_CONT);

         if (theline == NULL)
            break;
         whitep = (CS)" ";
         p = skipwhite(theline);
      }

      if (mustend && *p != endchar) {
         if (!skip)
            showErrFmtMsg(_(e_invalid_argument_str), *argp);
         goto err_ret;
      }
      if (*p == endchar && !need_expr)
          break;

      if (p[0] == '.' && p[1] == '.' && p[2] == '.') {
         if (varargs != NULL)
            *varargs = true;
         p += 3;
         mustend = true;
      } else {
         Byte *np;

         arg = p;
         p = one_function_arg(p, newargs, skip);
         if (p == arg)
            break;

         // Recognize " = expr" but not " == expr".  A lambda can have
         // "(a = expr" but "(a == expr" and "(a =~ expr" are not a lambda.
         np = skipwhite(p);
         if (*np == '=' && np[1] != '=' && np[1] != '~' && default_args != NULL) {
            Var   returnVar;

            // find the end of the expression (doesn't evaluate it)
            any_default = true;
            p = skipwhite(np + 1);
            Byte *expr = p;
            if (eval1(&p, &returnVar, NULL) != FAIL) {
               if (!skip) {
                  if (ga_grow(default_args, 1) == FAIL)
                     goto err_ret;

                  if (need_expr)
                     need_expr = false;
                  // trim trailing whitespace
                  while (p > expr && SPACE_OR_TAB(p[-1]))
                     p--;
                  c = *p;
                  *p = ZERO;
                  expr = copyStr(expr);
                  ((Byte **)(default_args->c))[default_args->len] = expr;
                  default_args->len++;
                  *p = c;
                }
            } else {
                mustend = true;
                if (*skipwhite(p) == ZERO)
               need_expr = true;
            }
          } ei (any_default) {
         emsg(_(e_non_default_argument_follows_default_argument));
         goto err_ret;
          }

         if (SPACE_OR_TAB(*p) && *skipwhite(p) == ',') {
            // Be tolerant when skipping
            if (!skip) {
               showErrFmtMsg(_(e_no_white_space_allowed_before_str_str), ",", p);
               goto err_ret;
            }
            p = skipwhite(p);
         }
         if (*p == ',') {
            ++p;
         } else
            mustend = true;
      }
      whitep = p;
      p = skipwhite(p);
   }

   if (*p != endchar)
      goto err_ret;
   ++p;   // skip "endchar"

   *argp = p;
   return OK;

err_ret:
   if (newargs != NULL)
      ga_clear_strings(newargs);
   if (!skip && default_args != NULL)
      ga_clear_strings(default_args);
   return FAIL;
}

// Register function "fp" as using "currentCallS" as its scope.
private int
register_closure(UserFunc *fp) {
   if (fp->uf_scoped == currentCallS)
      // no change
      return OK;
   funccal_unref(fp->uf_scoped, fp, false);
   fp->uf_scoped = currentCallS;
   currentCallS->refcount++;

   if (ga_grow(&currentCallS->fc_ufuncs, 1) == FAIL)
      return FAIL;
   ((UserFunc **)currentCallS->fc_ufuncs.c)[currentCallS->fc_ufuncs.len++] = fp;
   return OK;
}

//If "name" starts with K_SPECIAL and "builder[bufsize]" is big enough return "builder" filled 
//with a readable function name. Otherwise just return "name", thus the return value can always
//be used. "name" and "builder" may be equal.
CS
make_ufunc_name_readable(Byte *name, Byte* builder, Unt bufsize) {
   if (name[0] != K_SPECIAL)
      return name;
   Unt len = STRLEN(name);
   if (len + 3 > bufsize)
      return name;

   MEMMOVE(builder + 5, name + 3, len - 2);  // Include trailing ZERO
   MEMMOVE(builder, "<SNR>", 5);
   return builder;
}

private Byte   lambda_name[8 + NUMBUFLEN];

// Get a name for a lambda.  Returned in static memory.
Text
get_lambda_name(void) {
   static int   lambda_no = 0;

   int n = eeSnprintf(lambda_name, sizeof(lambda_name), "<lambda>%d", ++lambda_no);
   Text ret;
   if (n < 1)
      ret.len = 0;
   ei (n >= (int)sizeof(lambda_name))
      ret.len = sizeof(lambda_name) - 1;
   else
      ret.len = (Unt)n;

   ret.c = lambda_name;
   return ret;
}

//Allocate a "UserFunc" for a function called "name". Make sure the size is right.
private UserFunc *
alloc_ufunc(Byte *name, Unt namelen) {
   Unt  len;
   UserFunc *fp;

   //When the name is short we need to make sure we allocate enough bytes for
   //the whole struct, including any padding.
   len = offsetof(UserFunc, uf_name) + namelen + 1;
   fp = allocZeroed(len < sizeof(UserFunc) ? sizeof(UserFunc) : len);
   if (fp) {
      //Add a type cast to avoid a warning for an overflow, the uf_name[] array
      //can actually extend beyond the struct.
      STRCPY((void *)fp->uf_name, name);
      fp->uf_namelen = namelen;

      if (name[0] == K_SPECIAL) {
          len = namelen + 3;          // including +1 for ZERO
          fp->uf_name_exp = alloc(len);
          if (fp->uf_name_exp != NULL)
         eeSnprintf(fp->uf_name_exp, len, "<SNR>%s", fp->uf_name + 3);
      }
   }

   return fp;
}

#if defined(PROTO)
//Register a native C callback which can be called from Vim script.
//Return the name of the Vim script function.
Byte *
register_cfunc(cfunc_T cb, cfunc_free_T cb_free, void *state) {
   Text   name = get_lambda_name();
   UserFunc* fp = alloc_ufunc(name.c, name.len);
   if (!fp)
      return NULL;

   fp->refcount = 1;
   fp->uf_varargs = true;
   fp->uf_flags = FC_CFUNC | FC_LAMBDA;
   fp->uf_calls = 0;
   fp->scriptCtx = scriptPosG;
   fp->uf_cb = cb;
   fp->uf_cb_free = cb_free;
   fp->uf_cb_state = state;

   hash_add(&userDefinedFnsS, UF2HIKEY(fp), S"add C function");

   return name.c;
}
#endif

//Skip over "->" or "=>" after the arguments of a lambda.
//If ": type" is found make "ret_type" point to "type".
//If "white_error" is not NULL check for correct use of white space and set
//"white_error" to true if there is an error.
//Return NULL if no valid arrow found.
private CS
skip_arrow(
   CS start,
   int   equal_arrow,
   int   *white_error
) {
    Byte  *s = start;
    Byte  *bef = start - 2; // "start" points to > of ->

   if (equal_arrow) {
      bef = s;
      s = skipwhite(s);
      if (*s != '=')
         return NULL;
      ++s;
   }
   if (*s != '>')
      return NULL;
   if (white_error != NULL && ((!SPACE_OR_TAB(*bef) && *bef != '{') || !IS_WHITE_OR_ZERO(s[1]))) {
      *white_error = true;
      showErrFmtMsg(_(e_white_space_required_before_and_after_str_at_str), equal_arrow ? "=>" : "->", bef);
      return NULL;
   }
   return skipwhite(s + 1);
}

//Check if "*comm" points to a function command and if so advance "*comm" and return true.
//Otherwise return false; Do not consider "function(" to be a command.
private int
isFunctionComm(CS* comm) {
   CS p = *comm;

   if (checkforcmd(&p, S"function", 2)) {
      if (*p == '(')
         return false;
      *comm = p;
      return true;
   }
   return false;
}

//Called when defining a function: The context may be needed for script
//variables declared in a block that is visible now but not when the function
//is compiled or called later.
private void
function_using_block_scopes(UserFunc *fp, CondStack *cstack) {
   if (cstack == NULL || cstack->ind < 0)
      return;

   int count = cstack->ind + 1;
   fp->uf_block_ids = ALLOC_MULT(int, count);
   MEMMOVE(fp->uf_block_ids, cstack->cs_block_id, sizeof(int) * count);
   fp->uf_block_depth = count;

   // Set flag in each block to indicate a function was defined.  This
   // is used to keep the variable when leaving the block, see
   // hide_script_var().
   for (int i = 0; i <= cstack->ind; ++i)
      cstack->flags[i] |= CSF_FUNC_DEF;
}

//Read the body of a function, put every line in "newlines". This stops at "endfunction".
//"newlines" must already have been initialized. "invo->id" is C_block
private int
get_function_body(
   Invocation       *invo,
   ArrayList    *newlines,
   Byte       *line_arg_in,
   ArrayList    *lines_to_free
){
   LineNr   sourcing_lnum_top = SOURCING_LNUM;
   LineNr   sourcing_lnum_off;
   int      saved_wait_return = need_wait_return;
   Byte   *line_arg = line_arg_in;
#define MAX_FUNC_NESTING 50
   int nesting = 0;
   GetlineAlgo getline_options;
   int indent = 2;
   Byte *skip_until = NULL;
   int ret = FAIL;
   int is_heredoc = false;
   int heredoc_concat_len = 0;
   ArrayList heredoc_ga;
   Byte* heredoc_trimmed = NULL;
   Unt heredoc_trimmedlen = 0;

   ga_init2(&heredoc_ga, 1, 500);

   // Detect having skipped over comment lines to find the return
   // type.  Add NULL lines to keep the line count correct.
   sourcing_lnum_off = get_sourced_lnum(invo->ea_getline, invo->cookie);
   if (SOURCING_LNUM < sourcing_lnum_off) {
      sourcing_lnum_off -= SOURCING_LNUM;
      if (ga_grow(newlines, sourcing_lnum_off) == FAIL)
         goto theend;
      while (sourcing_lnum_off-- > 0)
         ((Byte **)(newlines->c))[newlines->len++] = NULL;
   }

   getline_options = GETLINE_CONCAT_CONT;
   for (;;) {
      Byte   *theline;
      Byte   *p;
      Byte   *arg;

      if (keyWasTypedG) {
         msg_scroll = true;
         saved_wait_return = false;
      }
      need_wait_return = false;

      if (line_arg) {
         // Use invo->arg, split up in parts by line breaks.
         theline = line_arg;
         p = firstOccurrence(theline, '\n');
         if (!p)
            line_arg += STRLEN(line_arg);
         else {
            *p = ZERO;
            line_arg = p + 1;
         }
      } else {
         theline = get_function_line(invo, lines_to_free, indent, getline_options);
      }
      if (keyWasTypedG)
         lines_left = visibleRowsG - 1;
      if (theline == NULL) {
         // Use the start of the function for the line number.
         SOURCING_LNUM = sourcing_lnum_top;
         if (skip_until != NULL)
            showErrFmtMsg(_(e_missing_heredoc_end_marker_str), skip_until);
         else
            emsg(_(e_missing_endfunction));
         goto theend;
      }

      // Detect line continuation: SOURCING_LNUM increased more than one.
      sourcing_lnum_off = get_sourced_lnum(invo->ea_getline, invo->cookie);
      if (SOURCING_LNUM < sourcing_lnum_off)
         sourcing_lnum_off -= SOURCING_LNUM;
      else
         sourcing_lnum_off = 0;

      if (skip_until) {
         // Don't check for ":endfunc"/":enddef" between
         // * ":append" and "."
         // * ":let {var-name} =<< [trim] {marker}" and "{marker}"
         if (!heredoc_trimmed
             || (is_heredoc && skipwhite(theline) == theline)
             || STRNCMP(theline, heredoc_trimmed, heredoc_trimmedlen) == 0
         ) {
            if (heredoc_trimmed == NULL)
               p = theline;
            ei (is_heredoc)
               p = skipwhite(theline) == theline ? theline : theline + heredoc_trimmedlen;
            else
                p = theline + heredoc_trimmedlen;
            if (STRCMP(p, skip_until) == 0) {
               EE_CLEAR(skip_until);
               EE_CLEAR(heredoc_trimmed);
               heredoc_trimmedlen = 0;
               getline_options = GETLINE_CONCAT_CONT;
               is_heredoc = false;

               if (heredoc_concat_len > 0) {
                  // Replace the starting line with all the concatenated
                  // lines.
                  ga_concat(&heredoc_ga, theline);
                  eeglFree(((Byte **)(newlines->c))[ heredoc_concat_len - 1]);
                  ((Byte **)(newlines->c))[heredoc_concat_len - 1] = heredoc_ga.c;
                  ga_init(&heredoc_ga);
                  heredoc_concat_len = 0;
                  theline += STRLEN(theline);  // skip the "EOF"
               }
            }
         }
      } else {
         Byte  *end;

         // skip ':' and blanks
         for (p = theline; SPACE_OR_TAB(*p) || *p == ':'; ++p)
            {}

         // Check for "endfunction". When a ":" follows, it must be a dict key; "enddef: value,"
         if (checkforcmd(&p, S"endfunction", 4) && *p != ':') {
            if (nesting-- == 0) {
               Byte *nextComm = NULL;

               if (*p == '|' || *p == '}')
                  nextComm = p + 1;
               ei (line_arg != NULL && *skipwhite(line_arg) != ZERO)
                  nextComm = line_arg;
               ei (*p != ZERO && !isComment(p) && p_verbose > 0) {
                  SOURCING_LNUM = sourcing_lnum_top + newlines->len + 1;
                  give_warning2((CS)
                  _("W22: Text found after :endfunction: %s"),
                  p, true);
               }
               if (nextComm != NULL && *skipwhite(nextComm) != ZERO) {
                  // Another command follows. If the line came from "invo"
                  // we can simply point into it, otherwise we need to
                  // change "invo->commline" to point to the last fetched line.
                  if (lines_to_free->len > 0
                        && *invo->commline 
                           != ((Byte **)lines_to_free->c)[lines_to_free->len - 1]
                  ) {
                      // *commline will be freed later, thus remove the line from lines_to_free.
                      eeglFree(*invo->commline);
                      *invo->commline = ((Byte **)lines_to_free->c)
                              [lines_to_free->len - 1];
                      --lines_to_free->len;
                  }
               }
               break;
            }
         }

         // Increase indent inside "if", "while", "for" and "try", decrease at "end".
         if (indent > 2 && (*p == '}' || STRNCMP(p, "end", 3) == 0))
            indent -= 2;
         ei (STRNCMP(p, "if", 2) == 0
                || STRNCMP(p, "wh", 2) == 0
                || STRNCMP(p, "for", 3) == 0
                || STRNCMP(p, "try", 3) == 0)
            indent += 2;

         // Check for defining a function inside this function.
         if (isFunctionComm(&p)) {
            if (*p == '!')
               p = skipwhite(p + 1);
            p += scriptCheckScriptPrefix(p);
            eeglFree(trans_function_name(&p, NULL, true, 0));
            if (*skipwhite(p) == '(') {
               if (nesting == MAX_FUNC_NESTING - 1)
                  emsg(_(e_function_nesting_too_deep));
               else {
                  ++nesting;
                  indent += 2;
               }
            }
         }

         if (isComment(p)) {
            // Not a comment line: check for nested inline function.

            end = p + STRLEN(p) - 1;

            while (end > p && SPACE_OR_TAB(*end))
               --end;
            if (end > p + 1 && *end == '{' && SPACE_OR_TAB(end[-1])) {
               // check for trailing "=> {": start of an inline function
               --end;
               while (end > p && SPACE_OR_TAB(*end))
                  --end;
               int is_block = end > p + 2 && end[-1] == '=' && end[0] == '>';
               if (!is_block) {
                  Byte *s = p;

                  // check for line starting with "au" for :autocmd or
                  // "com" for :command, these can use a {} block
                  is_block = checkforcmd_noparen(&s, S"autocmd", 2)
                           || checkforcmd_noparen(&s, S"command", 3);
               }

               if (is_block) {
                  if (nesting == MAX_FUNC_NESTING - 1)
                      emsg(_(e_function_nesting_too_deep));
                  else {
                      ++nesting;
                      indent += 2;
                  }
               }
            }
         }

         // Check for ":append", ":change", ":insert".  Not for :def.
         CS tp = p = skip_range(p, false, NULL);
         if ((checkforcmd(&p, S"append", 1)
                || checkforcmd(&p, S"change", 1)
                || checkforcmd(&p, S"insert", 1))
                && (*p == '!' || *p == '|' || IS_WHITE_NL_OR_ZERO(*p)))
            skip_until = copySubstr((CS)".", 1);
         else
            p = tp;

         if (!is_heredoc) {
            // Check for ":comm v =<< [trim] EOF"
            //       and ":comm [a, b] =<< [trim] EOF"
            //       and "lines =<< [trim] EOF" for Vim9
            // Where "comm" can be "let", "var", "final" or "const".
            arg = p;
            if (checkforcmd(&arg, S"let", 2)
               || checkforcmd(&arg, S"var", 3)
               || checkforcmd(&arg, S"final", 5)
               || checkforcmd(&arg, S"const", 5)
            ) {
               int      var_count = 0;
               int      semicolon = 0;

               arg = skip_var_list(arg, &var_count, &semicolon, true);
               if (arg != NULL)
                  arg = skipwhite(arg);
               if (arg != NULL && STRNCMP(arg, "=<<", 3) == 0) {
                  int has_trim = false;

                  p = skipwhite(arg + 3);
                  while (true) {
                     if (STRNCMP(p, "trim", 4) == 0 && (p[4] == ZERO || SPACE_OR_TAB(p[4]))) {
                        // Ignore leading white space.
                        p = skipwhite(p + 4);
                        has_trim = true;
                        continue;
                     }
                     if (STRNCMP(p, "eval", 4) == 0 && (p[4] == ZERO || SPACE_OR_TAB(p[4]))) {
                        // Ignore leading white space.
                        p = skipwhite(p + 4);
                        continue;
                     }
                     break;
                  }
                  if (has_trim) {
                     heredoc_trimmedlen = skipwhite(theline) - theline;
                     heredoc_trimmed = copySubstr(theline, heredoc_trimmedlen);
                     if (heredoc_trimmed == NULL)
                        heredoc_trimmedlen = 0;
                  }
                  skip_until = copySubstr(p, skiptowhite(p) - p);
                  getline_options = GETLINE_NONE;
                  is_heredoc = true;
               }
            }
         }
      }

      // Add the line to the function.
      if (ga_grow_id(newlines, 1 + sourcing_lnum_off, aid_get_func) == FAIL)
         goto theend;

      if (heredoc_concat_len > 0) {
         // For a :def function "python << EOF" concatenates all the lines,
         // to be used for the instruction later.
         ga_concat(&heredoc_ga, theline);
         ga_concat(&heredoc_ga, (CS)"\n");
         p = copySubstr(S"", 0);
      } else {
         //Copy the line to newly allocated memory.  get_one_sourceline()
         //allocates 250 bytes per line, this saves 80% on average.  The
         //cost is an extra alloc/free.
         p = copyStr(theline);
      }
      ((Byte **)(newlines->c))[newlines->len++] = p;

      // Add NULL lines for continuation lines, so that the line count is
      // equal to the index in the growarray.
      while (sourcing_lnum_off-- > 0)
         ((Byte **)(newlines->c))[newlines->len++] = NULL;

      // Check for end of invo->arg.
      if (line_arg && *line_arg == ZERO)
          line_arg = NULL;
    }

   // Return OK when no error was detected.
   if (!anyEmsgG)
      ret = OK;

theend:
   eeglFree(skip_until);
   eeglFree(heredoc_trimmed);
   eeglFree(heredoc_ga.c);
   need_wait_return |= saved_wait_return;
   return ret;
}

//Handle the body of a lambda.  *arg points to the "{", process statements until the matching "}".
//When not evaluating "newargs" is NULL.
//When successful "returnVar" is set to a funcref.
private int
lambda_function_body(
   Byte       **arg,
   Var    *returnVar,
   EvalCtx   *evalarg,
   ArrayList    *newargs,
   ArrayList    *default_args
) {
   int      evaluate = (evalarg->eval_flags & EVAL_EVALUATE);
   ArrayList   *gap = &evalarg->eval_ga;
   ArrayList   *freegap = &evalarg->eval_freega;
   UserFunc   *ufunc = NULL;
   Invocation   invo;
   ArrayList   newlines;
   Byte   *commline = NULL;
   int      ret = FAIL;
   PartiallyApplied   *pt;
   int      lnum_save = -1;
   LineNr   sourcing_lnum_top = SOURCING_LNUM;
   Byte   *line_arg = NULL;

   *arg = skipwhite(*arg + 1);
   if (**arg == '|' || !endsComm(*arg)) {
      showErrFmtMsg(_(e_trailing_characters_str), *arg);
      return FAIL;
   }

   // When there is a line break use what follows for the lambda body.
   // Makes lambda body initializers work for object and enum member variables.
   if (**arg == '\n')
      line_arg = *arg + 1;

   CLEAR_FIELD(invo);
   invo.id = C_block;
   invo.forceit = false;
   invo.commline = &commline;
   invo.skip = !evaluate;
   invo.ea_getline = evalarg->eval_getline;
   invo.cookie = evalarg->eval_cookie;

   ga_init2(&newlines, sizeof(CS), 10);
   if (get_function_body(&invo, &newlines, line_arg, &evalarg->eval_tofree_ga) == FAIL)
      goto erret;

   // When inside a lambda must add the function lines to evalarg.eval_ga.
   evalarg->eval_break_count += newlines.len;
   if (gap->ga_itemsize > 0) {
      int   idx;
      Byte   *last;
      Unt  plen;
      Byte  *pnl;

      for (idx = 0; idx < newlines.len; ++idx) {
         Byte  *p = ((Byte **)newlines.c)[idx];
         if (!p)
            // comment line in the lambda body
            continue;

         p = skipwhite(p);

         if (ga_grow(gap, 1) == FAIL || ga_grow(freegap, 1) == FAIL)
            goto erret;

         // Going to concatenate the lines after parsing.  For an empty or
         // comment line use an empty string.
         // Insert NL characters at the start of each line, the string will
         // be split again later in .get_lambda_tv().
         if (*p == ZERO || isComment(p)) {
            p = (CS)"";
            plen = 0;
          } else
            plen = STRLEN(p);
         pnl = copySubstr((CS)"\n", plen + 1);
         if (pnl != NULL)
            MEMMOVE(pnl + 1, p, plen + 1);
         ((Byte **)gap->c)[gap->len++] = pnl;
         ((Byte **)freegap->c)[freegap->len++] = pnl;
      }
      if (ga_grow(gap, 1) == FAIL || ga_grow(freegap, 1) == FAIL)
          goto erret;
      // nothing is following the "}"
      last = S"}";
      plen = 1;
      pnl = copySubstr((CS)"\n", plen + 1);
      MEMMOVE(pnl + 1, last, plen + 1);
      ((Byte **)gap->c)[gap->len++] = pnl;
      ((Byte **)freegap->c)[freegap->len++] = pnl;
   }

   *arg = S"";

   if (!evaluate) {
      ret = OK;
      goto erret;
   }

   Text name = get_lambda_name();
   ufunc = alloc_ufunc(name.c, name.len);
   if (ufunc == NULL)
      goto erret;
   if (hash_add(&userDefinedFnsS, (Text){ufunc->uf_name, ufunc->uf_namelen}, S"add function") 
         == FAIL
   )
      goto erret;
   ufunc->uf_flags = FC_LAMBDA;
   ufunc->refcount = 1;
   ufunc->args = *newargs;
   newargs->c = NULL;
   ufunc->defaultArgs = *default_args;
   default_args->c = NULL;

   // error messages are for the first function line
   lnum_save = SOURCING_LNUM;
   SOURCING_LNUM = sourcing_lnum_top;

   pt = ALLOC_CLEAR_ONE(PartiallyApplied);
   if (pt == NULL)
      goto erret;
   pt->fn = ufunc;
   pt->refcount = 1;

   ufunc->lines = newlines;
   newlines.c = NULL;
   ufunc->scriptCtx = scriptPosG;
   ufunc->scriptCtx.lineNr += sourcing_lnum_top;

   function_using_block_scopes(ufunc, evalarg->eval_cstack);

   returnVar->partial = pt;
   returnVar->tag = VAR_PARTIAL;
   ufunc = NULL;
   ret = OK;

erret:
   if (lnum_save >= 0)
      SOURCING_LNUM = lnum_save;
   ga_clear_strings(&newlines);
   if (newargs != NULL)
      ga_clear_strings(newargs);
   ga_clear_strings(default_args);
   if (ufunc != NULL) {
      func_clear(ufunc, true);
      func_free(ufunc, true);
   }
   return ret;
}

//Parse a lambda expression and get a Funcref from "*arg" into "returnVar". "arg" points to the 
//opening brace in "{arg -> expr}" or the opening paren in "(arg) => expr"
//Return OK or FAIL, or NOTDONE for dict or {expr}.
int
get_lambda_tv(
   Byte       **arg,
   Var    *returnVar,
   EvalCtx   *evalarg
) {
   int      evaluate = evalarg != NULL && (evalarg->eval_flags & EVAL_EVALUATE);
   ArrayList   newargs;
   ArrayList   newlines;
   ArrayList   *pnewargs;
   ArrayList   default_args;
   UserFunc   *fp = NULL;
   PartiallyApplied   *pt = NULL;
   int      varargs;
   Byte   *ret_type = NULL;
   int      ret;
   Byte   *s;
   Byte   *start, *end;
   int      *old_eval_lavars = eval_lavars_used;
   int      eval_lavars = false;
   Byte   *tofree2 = NULL;
   int      equal_arrow = **arg == '(';
   int      white_error = false;
   int      called_emsg_start = called_emsg;
   long   start_lnum = SOURCING_LNUM;
   if (equal_arrow) {
      return NOTDONE;
   } 

   ga_init(&newargs);
   ga_init(&newlines);

   // First, check if this is really a lambda expression. "->" or "=>" must
   // be found after the arguments.
   s = *arg + 1;
   ret = get_function_args(&s, equal_arrow ? ')' : '-', NULL,
        NULL, &default_args, true, NULL, NULL);
   if (ret == FAIL || skip_arrow(s, equal_arrow, NULL) == NULL) {
      return called_emsg == called_emsg_start ? NOTDONE : FAIL;
   }

   // Parse the arguments for real.
   if (evaluate)
      pnewargs = &newargs;
   else
      pnewargs = NULL;
   *arg += 1;
   ret = get_function_args(
      arg, equal_arrow ? ')' : '-', pnewargs,
      &varargs, &default_args, false, NULL, NULL
   );
   if (ret == FAIL 
         || (s = skip_arrow(*arg, equal_arrow, equal_arrow ? &white_error : NULL)) == NULL
   ) {
      ga_clear_strings(&newargs);
      return white_error ? FAIL : NOTDONE;
   }
   *arg = s;

   // Skipping over linebreaks may make "ret_type" invalid, make a copy.
   if (ret_type != NULL) {
      ret_type = copyStr(ret_type);
      tofree2 = ret_type;
   }

   // Set up a flag for checking local variables and arguments.
   if (evaluate)
      eval_lavars_used = &eval_lavars;

   *arg = skipwhite_and_linebreak(*arg, evalarg);

   // Recognize opening brace as the start of a function body.
   if (equal_arrow && **arg == '{') {
      if (evalarg == NULL)
         // cannot happen?
         goto theend;
      SOURCING_LNUM = start_lnum;  // used for where lambda is defined
      if (lambda_function_body(arg, returnVar, evalarg, pnewargs,
               &default_args) == FAIL)
          goto errret;
      goto theend;
   }
   if (default_args.len > 0) {
      emsg(_(e_cannot_use_default_values_in_lambda));
      goto errret;
   }

   // Get the start and the end of the expression.
   start = *arg;
   ret = skip_expr_concatenate(arg, &start, &end, evalarg);
   if (ret == FAIL)
      goto errret;

   if (!equal_arrow) {
      *arg = skipwhite_and_linebreak(*arg, evalarg);
      if (**arg != '}') {
         showErrFmtMsg(_(e_expected_right_curly_str), *arg);
         goto errret;
      }
      ++*arg;
   }

   if (evaluate) {
      int       len;
      int       flags = FC_LAMBDA;
      Byte       *p;
      Byte       *line_end;
      Text name = get_lambda_name();

      fp = alloc_ufunc(name.c, name.len);
      if (!fp)
         goto errret;
      pt = ALLOC_CLEAR_ONE(PartiallyApplied);
      if (!pt)
         goto errret;

      ga_init2(&newlines, sizeof(CS), 1);
      if (ga_grow(&newlines, 1) == FAIL)
         goto errret;

      // If there are line breaks, we need to split up the string.
      line_end = firstOccurrence(start, '\n');
      if (line_end == NULL || line_end > end)
          line_end = end;

      // Add "return " before the expression (or the first line).
      len = 7 + (int)(line_end - start) + 1;
      p = alloc(len);
      ((Byte **)(newlines.c))[newlines.len++] = p;
      STRCPY(p, "return ");
      copySubstrToAllocation(p + 7, (Text){start, line_end - start});

      if (line_end != end) {
         // Add more lines, split by line breaks.  Thus is used when a
         // lambda with { cmds } is encountered.
         while (*line_end == '\n') {
            if (ga_grow(&newlines, 1) == FAIL)
               goto errret;
            start = line_end + 1;
            line_end = firstOccurrence(start, '\n');
            if (line_end == NULL)
               line_end = end;
            ((Byte **)(newlines.c))[newlines.len++] = copySubstr(start, line_end - start);
         }
      }

      if (strstr((char *)p + 7, "a:") == NULL)
         // No a: variables are used for sure.
         flags |= FC_NOARGS;

      fp->refcount = 1;
      fp->args = newargs;
      ga_init(&fp->defaultArgs);

      fp->lines = newlines;
      if (currentCallS != NULL && eval_lavars) {
         flags |= FC_CLOSURE;
         if (register_closure(fp) == FAIL)
            goto errret;
      }

      //In Vim script a lambda can be called with more args than args.len.
      fp->uf_varargs = true;
      fp->uf_flags = flags;
      fp->uf_calls = 0;
      fp->scriptCtx = scriptPosG;
      // Use the line number of the arguments.
      fp->scriptCtx.lineNr += start_lnum;

      function_using_block_scopes(fp, evalarg->eval_cstack);

      pt->fn = fp;
      pt->refcount = 1;
      returnVar->partial = pt;
      returnVar->tag = VAR_PARTIAL;

      hash_add(&userDefinedFnsS, (Text){fp->uf_name, fp->uf_namelen}, S"add lambda");
   }

theend:
   eval_lavars_used = old_eval_lavars;
   eeglFree(tofree2);

   return OK;

errret:
   ga_clear_strings(&newargs);
   ga_clear_strings(&newlines);
   ga_clear_strings(&default_args);
   if (fp != NULL) {
      eeglFree(fp->uf_name_exp);
      eeglFree(fp);
   }
   eeglFree(pt);
   eeglFree(tofree2);
   eval_lavars_used = old_eval_lavars;
   return FAIL;
}

//Check if "name" is a variable of type VAR_FUNC. If so, return the function name it contains, 
//otherwise return "name". If "partialp" is not NULL, and "name" is of type VAR_PARTIAL, also set
//"partialp". If "found_var" is not NULL and a variable was found, set it to true.
Text
deref_func_name(
   Text name,
   OUT PartiallyApplied** partialp,
   TypeSpec** type,
   Boole no_autoload,
   OUT Boole* found_var)
{
   Var   *tv = NULL;
   Byte   *s = NULL;
   EeSet   *ht;
   int      did_type = false;

   if (partialp)
      *partialp = NULL;

   Byte cc = name.c[name.len];
   name.c[name.len] = ZERO;
   Text retval = name;

   DictItem* v = findVar_also_in_script(name.c, &ht, no_autoload);
   name.c[name.len] = cc;
   if (v) {
      tv = &v->c;
   } ei (STRNCMP(name.c, "s:", 2) == 0) {
      if (STRNCMP(name.c, "s:", 2) == 0) {
         retval.len -= 2;
      }
   }

   if (tv) {
      if (found_var)
         *found_var = true;
      if (tv->tag == VAR_FUNC) {
         if (!tv->string) {
            return (Text){null, 0};// just in case
         }
         retval = text(tv->string);
      } ei (tv->tag == VAR_PARTIAL) {
         PartiallyApplied *pt = tv->partial;
         if (!pt) {
            return (Text){null, 0};// just in case
         }
         if (partialp)
            *partialp = pt;
         retval.c = partial_name(pt);
         retval.len = (int)STRLEN(s);
      }

      if (s) {
         if (!did_type && type != NULL && ht == get_script_local_ht()) {
            Svar* sv = find_typval_in_script(tv, 0, true);
            if (sv)
               *type = sv->sv_type;
         }
         return retval;
      }
   }

   return name;
}

//Give an error message with a function name.  Handle <SNR> things.
//"ermsg" is to be passed without translation, use N_() instead of _().
void
emsg_funcname(CS ermsg, Byte *name) {
   Byte   *p = name;

   if (name[0] == K_SPECIAL && name[1] != ZERO && name[2] != ZERO)
      p = concat_str((CS)"<SNR>", name + 3);
   showErrFmtMsg(_(ermsg), p);
   if (p != name)
      eeglFree(p);
}

//Get function arguments at "*arg" and advance it. Return them in "*argvars[MAX_FUNC_ARGS + 1]" 
//and the count in "argcount". On failure FAIL is returned but "argvars[argcount]" are still set
int
get_func_arguments(
   Byte** arg,
   OUT EvalCtx* evalarg,
   int partial_argc,
   Var* argvars,
   int* argcount
){
   Byte   *argp = *arg;
   int      ret = OK;

   while (*argcount < MAX_FUNC_ARGS - partial_argc) {
      // skip the '(' or ',' and possibly line breaks
      argp = skipwhite_and_linebreak(argp + 1, evalarg);

      if (*argp == ')' || *argp == ',' || *argp == ZERO)
          break;

      int arg_idx = *argcount;
      if (eval1(&argp, &argvars[arg_idx], OUT evalarg) == FAIL) {
         ret = FAIL;
         break;
      }
      ++*argcount;

      argp = skipwhite(argp);
      if (*argp != ',')
         break;
   }

   argp = skipwhite_and_linebreak(argp, evalarg);
   if (*argp == ')')
      ++argp;
   else
      ret = FAIL;
   *arg = argp;
   return ret;
}

// Call a function and put the result in "returnVar". Return OK or FAIL.
int
get_func_tv(
   CS name,      // name of the function
   int len,      // length of "name" or -1 to use strlen()
   Var* returnVar,
   Byte   **arg,      // argument, pointing to the '('
   EvalCtx* evalarg,   // for line continuation
   FnExe* funcexe)   // various values
{
   Byte   *argp;
   int      ret;
   Var   argvars[MAX_FUNC_ARGS + 1];   // vars for arguments
   int      argcount = 0;         // number of arguments found
   int evaluate = evalarg == NULL ? false : (evalarg->eval_flags & EVAL_EVALUATE);

   argp = *arg;
   ret = get_func_arguments(
       &argp, OUT evalarg, (funcexe->fe_partial == NULL ? 0 : funcexe->fe_partial->argc), argvars, 
       &argcount
   );

   if (ret == OK) {
      int   i = 0;

      if (get_EeglVar_nr(VV_TESTING)) {
          // Prepare for calling test_garbagecollect_now(), need to know
          // what variables are used on the call stack.
          if (funcargs.ga_itemsize == 0)
         ga_init2(&funcargs, sizeof(Var *), 50);
          for (i = 0; i < argcount; ++i)
         if (ga_grow(&funcargs, 1) == OK)
             ((Var **)funcargs.c)[funcargs.len++] =
                             &argvars[i];
      }

      ret = call_func(name, len, returnVar, argcount, argvars, funcexe);

      funcargs.len -= i;
   } ei (!aborting() && evaluate) {
      if (argcount == MAX_FUNC_ARGS)
         emsg_funcname(e_too_many_arguments_for_function_str_2, name);
      else
         emsg_funcname(e_invalid_arguments_for_function_str, name);
   }

    while (--argcount >= 0)
   clearVar(&argvars[argcount]);

   *arg = skipwhite(argp);
   return ret;
}

//Return true if "p" starts with "<SID>" or "s:". Only works if scriptCheckScriptPrefix() returned 
//non-zero for "p"!
private int
eval_fname_sid(Byte *p) {
    return (*p == 's' || TOUPPER_ASC(p[2]) == 'I');
}

//In a script change <SID>name() and s:name() to K_SNR 123_name(). Change <SNR>123_name() to 
//K_SNR 123_name(). Use "fname_buf[FLEN_FIXED + 1]" when it fits, otherwise allocate memory and 
//set "tofree".
Byte *
fname_trans_sid(
   Byte       *name,
   Byte       *fname_buf,
   Byte       **tofree,
   FnError *error)
{
   Byte   *scriname;
   Byte   *fname;
   Unt   fnamelen;
   Unt   fname_buflen;

   scriname = name + scriptCheckScriptPrefix(name);
   if (scriname == name)
      return name;  // no prefix

   fname_buf[0] = K_SPECIAL;
   fname_buf[1] = KS_EXTRA;
   fname_buf[2] = (int)KE_SNR;
   fname_buflen = 3;
   if (!eval_fname_sid(name))   // "<SID>" or "s:"
      fname_buf[fname_buflen] = ZERO;
   else {
      if (scriptPosG.sid <= 0)
          *error = FCERR_SCRIPT;
      else {
          fname_buflen += eeSnprintf(fname_buf + 3,
                     FLEN_FIXED - 3,
                     "%ld_",
                     (long)scriptPosG.sid);
      }
   }
   fnamelen = fname_buflen + STRLEN(scriname);
   if (fnamelen < FLEN_FIXED) {
      STRCPY(fname_buf + fname_buflen, scriname);
      fname = fname_buf;
   } else {
      fname = alloc(fnamelen + 1);
      *tofree = fname;
      eeSnprintf(fname, fnamelen + 1, "%s%s", fname_buf, scriname);
   }
   return fname;
}

//Concatenate the script ID and function name into  "<SNR>99_name".
//"buffer" must have size MAX_FUNC_NAME_LEN.
void
func_name_with_sid(CS name, int sid, CS builder) {
   // A script-local function is stored as "<SNR>99_name".
   builder[0] = K_SPECIAL;
   builder[1] = KS_EXTRA;
   builder[2] = (int)KE_SNR;
   eeSnprintf(builder + 3, MAX_FUNC_NAME_LEN - 3, "%ld_%s", (long)sid, name);
}

// Find the function "name" in script "sid" prefixing the autoload prefix.
private UserFunc *
find_func_with_prefix(Byte *name, int sid) {
   if (firstOccurrence(name, AUTOLOAD_CHAR) != NULL)
      return NULL;   // already has the prefix
   if (!SCRIPT_ID_VALID(sid))
      return NULL;   // not in a script
   return NULL;
}

//Find a function by name, return pointer to it in ufuncs.
//When "flags" has FFED_IS_GLOBAL don't find script-local or imported functions.
//When "flags" has "FFED_NO_GLOBAL" don't find global functions.
//Return NULL for unknown function.
UserFunc *
find_func_even_dead(CS name, int flags) {
   if ((flags & FFED_NO_GLOBAL) == 0) {
      EeSetItem* hi = hash_find(&userDefinedFnsS, text(STRNCMP(name, "g:", 2) == 0 ? name + 2 : name));
      if (!HASHITEM_EMPTY(hi))
         return HI2UF(hi);
   }

   // Find autoload function if this is an autoload script.
   return find_func_with_prefix(name[0] == 's' && name[1] == ':' ? name + 2 : name, scriptPosG.sid);
}

//Find a function by name, return pointer to it in ufuncs.
//"cctx" is passed in a :def function to find imported functions.
//Return NULL for unknown or dead function.
UserFunc *
find_func(CS name, Boole is_global) {
   UserFunc* fp = find_func_even_dead(name, is_global ? FFED_IS_GLOBAL : 0);

   if (fp && (fp->uf_flags & FC_DEAD) == 0)
      return fp;
   return NULL;
}

//Return true if "ufunc" is a global function.
int
func_is_global(UserFunc *ufunc) {
   return ufunc->uf_name[0] != K_SPECIAL;
}

// Return true if "ufunc" must be called with a g: prefix in Vim9 script.
int
func_requires_g_prefix(UserFunc *ufunc) {
    return func_is_global(ufunc)
       && (ufunc->uf_flags & FC_LAMBDA) == 0
       && firstOccurrence(ufunc->uf_name, AUTOLOAD_CHAR) == NULL
       && !SAFE_isdigit(ufunc->uf_name[0]);
}

//Copy the function name of "fp" to buffer "builder". "builder" must be able to hold the function 
//name plus three bytes. Take care of script-local function names.
private int
cat_func_name(CS builder, Unt bufsize, UserFunc *fp) {
   int   len;

   if (!func_is_global(fp))
      len = eeSnprintf(builder, bufsize, "<SNR>%s", fp->uf_name + 3);
   else
      len = eeSnprintf(builder, bufsize, "%s", fp->uf_name);

   return (len >= (int)bufsize) ? (int)bufsize - 1 : len;
}

// Add a number variable "name" to dict "dp" with value "nr".
private void
add_nr_var(
   Bag   *dp,
   DictItem   *v,
   CS name,
   Long nr)
{
   STRCPY(v->key, name);
   v->flags = DI_FLAGS_RO | DI_FLAGS_FIX;
   hash_add(&dp->hashTable, (Text){v->key, v->len}, S"add variable");
   v->c = (Var){.tag = VAR_NUMBER, .lock = VAR_FIXED, .number = nr};
}

// Free "fc".
private void
free_funccal(FnCall *fc) {
    int   i;

    for (i = 0; i < fc->fc_ufuncs.len; ++i) {
   UserFunc *fp = ((UserFunc **)(fc->fc_ufuncs.c))[i];

   // When garbage collecting a FnCall may be freed before the
   // function that references it, clear its uf_scoped field.
   // The function may have been redefined and point to another
   // FnCall, don't clear it then.
   if (fp != NULL && fp->uf_scoped == fc)
       fp->uf_scoped = NULL;
   }
   ga_clear(&fc->fc_ufuncs);

   func_ptr_unref(fc->fn);
   eeglFree(fc);
}

//Free "fc" and what it contains. Can be called only when "fc" is kept beyond the period of it 
//called, i.e. after cleanup_function_call(fc).
private void
free_funccal_contents(FnCall *fc) {
   ListItem   *li;

   // Free all l: variables.
   vars_clear(&fc->localVars.hashTable);

   // Free all a: variables.
   vars_clear(&fc->argVars.hashTable);

   // Free the a:000 variables.
   FOR_ALL_LIST_ITEMS(&fc->arguments, li)
   clearVar(&li->c);

   free_funccal(fc);
}

//Handle the last part of returning from a function: free the local hashtable.
//Unless it is still in use by a closure.
private void
cleanup_function_call(FnCall *fc) {
    int   may_free_fc = fc->refcount <= 0;
    int   free_fc = true;

    currentCallS = fc->fc_caller;

    // Free all l: variables if not referred.
   if (may_free_fc && fc->localVars.refcount == DO_NOT_FREE_CNT)
      vars_clear(&fc->localVars.hashTable);
   else
      free_fc = false;

    // If the a:000 list and the l: and a: dicts are not referenced and
    // there is no closure using it, we can free the FnCall and what's
    // in it.
   if (may_free_fc && fc->argVars.refcount == DO_NOT_FREE_CNT)
      vars_clear_ext(&fc->argVars.hashTable, false);
   else {
      int       todo;
      EeSetItem  *hi;
      DictItem  *di;

      free_fc = false;

      // Make a copy of the a: variables, since we didn't do that above.
      todo = (int)fc->argVars.hashTable.count;
      FOR_ALL_HASHTAB_ITEMS(&fc->argVars.hashTable, hi, todo) {
         if (!HASHITEM_EMPTY(hi)) {
            --todo;
            di = HI2DI(hi);
            copy_tv(OUT &di->c, &di->c);
         }
      }
   }

   if (may_free_fc && fc->arguments.refcount == DO_NOT_FREE_CNT)
      fc->arguments.first = NULL;
   else {
      ListItem *li;

      free_fc = false;

      // Make a copy of the a:000 items, since we didn't do that above.
      FOR_ALL_LIST_ITEMS(&fc->arguments, li)
         copy_tv(OUT &li->c, &li->c);
   }

   if (free_fc)
      free_funccal(fc);
   else {
      static int made_copy = 0;

      // "fc" is still in use.  This can happen when returning "a:000",
      // assigning "l:" to a global variable or defining a closure.
      // Link "fc" in the list for garbage collection later.
      fc->fc_caller = previous_funccal;
      previous_funccal = fc;

      if (want_garbage_collect)
          // If garbage collector is ready, clear count.
          made_copy = 0;
      ei (++made_copy >= (int)((4096 * 1024) / sizeof(*fc))) {
          // We have made a lot of copies, worth 4 Mbyte.  This can happen
          // when repetitively calling a function that creates a reference to
          // itself somehow. Call the garbage collector soon to avoid using too much memory.
          made_copy = 0;
          want_garbage_collect = true;
      }
    }
}

//Return true if "name" is a numbered function, ignoring a "g:" prefix.
private int
numbered_function(Byte *name) {
   return SAFE_isdigit(*name)
       || (name[0] == 'g' && name[1] == ':' && SAFE_isdigit(name[2]));
}

/*
 * There are two kinds of function names:
 * 1. ordinary names, function defined with :function or :def;
 *    can start with "<SNR>123_" literally or with K_SPECIAL.
 * 2. Numbered functions and lambdas: "<lambda>123"
 * For the first we only count the name stored in userDefinedFnsS as a reference,
 * using function() does not count as a reference, because the function is
 * looked up by name.
 */
int
func_name_refcount(Byte *name) {
    return numbered_function(name) || (name[0] == '<' && name[1] == 'l');
}

//Unreference "fc": decrement the reference count and free it when it becomes zero. "fp" is 
//detached from "fc". When "force" is true, we are exiting.
private void
funccal_unref(FnCall *fc, UserFunc *fp, int force) {
   if (!fc)
      return;

   if (--fc->refcount <= 0 
         && (force || (
         fc->arguments.refcount == DO_NOT_FREE_CNT
         && fc->localVars.refcount == DO_NOT_FREE_CNT
         && fc->argVars.refcount == DO_NOT_FREE_CNT))
   ) {
      for (FnCall** pfc = &previous_funccal; *pfc; pfc = &(*pfc)->fc_caller) {
         if (fc == *pfc) {
            *pfc = fc->fc_caller;
            free_funccal_contents(fc);
            return;
         }
      }
   } 
   for (int i = 0; i < fc->fc_ufuncs.len; ++i) {
      if (((UserFunc **)(fc->fc_ufuncs.c))[i] == fp)
          ((UserFunc **)(fc->fc_ufuncs.c))[i] = NULL;
   } 
}

//Remove the function from the function hashtable.  If the function was
//deleted while it still has references this was already done.
//Return true if the entry was deleted, false if it wasn't found.
private int
func_remove(UserFunc *fp) {
   // Return if it was already virtually deleted.
   if (fp->uf_flags & FC_DEAD)
      return false;

   EeSetItem* hi = hash_find(&userDefinedFnsS, (Text){fp->uf_name, fp->uf_namelen});
   if (HASHITEM_EMPTY(hi))
      return false;

   hash_remove(&userDefinedFnsS, hi, S"remove function");
   fp->uf_flags |= FC_DELETED;
   return true;
}

private void
func_clear_items(UserFunc *fp) {
   ga_clear_strings(&(fp->args));
   ga_clear_strings(&(fp->defaultArgs));
   ga_clear_strings(&(fp->lines));
   EE_CLEAR(fp->uf_block_ids);
   EE_CLEAR(fp->uf_va_name);
}

//Free all things that a function contains. Does not free the function itself, use func_free() 
//for that. When "force" is true we are exiting.
private void
func_clear(UserFunc *fp, int force) {
   if (fp->uf_cleared)
      return;
   fp->uf_cleared = true;

   // clear this function
   func_clear_items(fp);
   funccal_unref(fp->uf_scoped, fp, force);
}

//Free a function and remove it from the list of functions. Does not free what a function contains,
//call func_clear() first. When "force" is true we are exiting. Return OK when the function was 
//actually freed.
private int
func_free(UserFunc *fp, int force) {
   // Only remove it when not done already, otherwise we would remove a newer
   // version of the function with the same name.
   if ((fp->uf_flags & (FC_DELETED | FC_REMOVED)) == 0)
      func_remove(fp);

   if ((fp->uf_flags & FC_DEAD) == 0 || force) {
      EE_CLEAR(fp->uf_name_exp);
      eeglFree(fp);
      return OK;
   }
   return FAIL;
}

//Free all things that a function contains and free the function itself. When "force" is true we 
//are exiting.
void
func_clear_free(UserFunc *fp, int force) {
   func_clear(fp, force);
   if (force || func_name_refcount(fp->uf_name))
      func_free(fp, force);
   else
      fp->uf_flags |= FC_DEAD;
}

private int   funcdepth = 0;

//Increment the function call depth count. Return FAIL when going over 'maxfuncdepth'.
//Otherwise return OK, must call funcdepth_decrement() later!
int
funcdepth_increment(void) {
   if (funcdepth >= p_mfd) {
      emsg(_(e_function_call_depth_is_higher_than_maxfuncdepth));
      return FAIL;
   }
   ++funcdepth;
   return OK;
}

void
funcdepth_decrement(void) {
   --funcdepth;
}

//Get the current function call depth.
int
funcdepth_get(void) {
   return funcdepth;
}

//Restore the function call depth.  This is for cases where there is no
//guarantee funcdepth_decrement() can be called exactly the same number of
//times as funcdepth_increment().
void
funcdepth_restore(int depth) {
   funcdepth = depth;
}

//Allocate a FnCall, link it in currentCallS and fill in "fp" and "returnVar".
//Must be followed by one call to remove_funccal() or cleanup_function_call().
//Return NULL when allocation fails.
FnCall *
create_funccal(UserFunc *fp, Var* returnVar) {
   FnCall *fc = ALLOC_CLEAR_ONE(FnCall);

   if (!fc)
      return NULL;
   fc->fc_caller = currentCallS;
   currentCallS = fc;
   fc->fn = fp;
   func_ptr_ref(fp);
   fc->fc_returnVar = returnVar;
   return fc;
}

//To be called when returning from a compiled function; restores currentCallS.
void
remove_funccal(void) {
   FnCall *fc = currentCallS;
   currentCallS = fc->fc_caller;
   free_funccal(fc);
}

//Call a user function.
private FnError
call_user_func(
   UserFunc   *fp,      // pointer to function
   int      argcount,   // nr of args
   Var   *argvars,   // arguments
   Var   *returnVar,      // return value
   FnExe   *funcexe,   // context
   Bag* selfdict)   // Dictionary for "self"
{
   ScriptPos   save_scriptPosG;
   int      save_stickyCommandModifiersG = stickyCommandModifiersG;
   FnCall   *fc;
   int      save_anyEmsgG;
   FnError retval = FCERR_NONE;
   int      default_arg_err = false;
   DictItem   *v;
   int      fixvar_idx = 0;   // index in fc_fixvar[]
   int      i;
   int      ai;
   int      islambda = false;
   Byte   numbuf[NUMBUFLEN];
   Byte   *name;
   Unt   namelen;
   Var   *tv_to_free[MAX_FUNC_ARGS];
   int      tv_to_free_len = 0;
   ESTACK_CHECK_DECLARATION;


   // If depth of calling is getting too high, don't execute the function.
   if (funcdepth_increment() == FAIL) {
      returnVar->tag = VAR_NUMBER;
      returnVar->number = -1;
      return FCERR_FAILED;
   }

   line_breakcheck();      // check for CTRL-C hit

   fc = create_funccal(fp, returnVar);
   if (!fc)
      return FCERR_OTHER;
   fc->fc_level = ex_nesting_level;
   // Check if this function has a breakpoint.
   fc->fc_breakpoint = dbg_find_breakpoint(false, fp->uf_name, (LineNr)0);
   fc->fc_dbg_tick = debug_tick;
   // Set up fields for closure.
   ga_init2(&fc->fc_ufuncs, sizeof(UserFunc *), 1);

    islambda = fp->uf_flags & FC_LAMBDA;

   //Note about using fc->fc_fixvar[]: This is an array of FIXVAR_CNT
   //variables with names up to VAR_SHORT_LEN long.  This avoids having to
   //alloc/free each argument variable and saves a lot of time.
   //Init l: variables.
   init_var_dict(&fc->localVars, &fc->localVarsVar, VAR_DEF_SCOPE);
   if (selfdict != NULL) {
      // Set l:self to "selfdict".  Use "name" to avoid a warning from
      // some compiler that checks the destination size.
      v = &fc->fc_fixvar[fixvar_idx++].var;
      name = v->key;
      STRCPY(name, "self");
      v->flags = DI_FLAGS_RO | DI_FLAGS_FIX;
      hash_add(&fc->localVars.hashTable, textOfDi(v), S"set self dictionary");
      v->c = (Var){.tag = VAR_BAG, .lock = 0, .bag = selfdict};
      ++selfdict->refcount;
   }

   //Init a: variables, unless none found (in lambda).
   //Set a:0 to "argcount" less number of named arguments, if >= 0.
   //Set a:000 to a list with room for the "..." arguments.
   init_var_dict(&fc->argVars, &fc->argVarsVar, VAR_SCOPE);
   if ((fp->uf_flags & FC_NOARGS) == 0) {
      add_nr_var(
         &fc->argVars, &fc->fc_fixvar[fixvar_idx++].var, S"0",
         (Long)(argcount >= fp->args.len ? argcount - fp->args.len : 0)
      );
   } 
   fc->argVars.lock = VAR_FIXED;
   if ((fp->uf_flags & FC_NOARGS) == 0) {
      //Use "name" to avoid a warning from some compilers that checks the destination size.
      v = &fc->fc_fixvar[fixvar_idx++].var;
      name = v->key;
      STRCPY(name, "000");
      v->flags = DI_FLAGS_RO | DI_FLAGS_FIX;
      hash_add(&fc->argVars.hashTable, textOfDi(v), S"function argument");
      v->c = (Var){.tag = VAR_LIST, .lock = VAR_FIXED, .list = &fc->arguments };
   }
   CLEAR_FIELD(fc->arguments);
   fc->arguments.refcount = DO_NOT_FREE_CNT;
   fc->arguments.lock = VAR_FIXED;

   //Set a:firstline to "firstline" and a:lastline to "lastline".
   //Set a:name to named arguments.
   //Set a:N to the "..." arguments.
   //Skipped when no a: variables used (in lambda).
   if ((fp->uf_flags & FC_NOARGS) == 0) {
      add_nr_var(
         &fc->argVars, &fc->fc_fixvar[fixvar_idx++].var, S"firstline", 
         (Long)funcexe->fe_firstline
      );
      add_nr_var(&fc->argVars, &fc->fc_fixvar[fixvar_idx++].var,
               S"lastline", (Long)funcexe->fe_lastline);
   }
   for (i = 0; i < argcount || i < fp->args.len; ++i) {
      int       addlocal = false;
      Var    def_returnVar;
      int       isdefault = false;

      ai = i - fp->args.len;
      if (ai < 0) {
         // named argument a:name
         name = FUNCARG(fp, i);
         if (islambda)
            addlocal = true;

         // evaluate named argument default expression
         isdefault = ai + fp->defaultArgs.len >= 0
                && (i >= argcount || (argvars[i].tag == VAR_SPECIAL
                  && argvars[i].number == VVAL_NONE));
         if (isdefault) {
            Byte       *default_expr = NULL;

            def_returnVar.tag = VAR_NUMBER;
            def_returnVar.number = -1;

            default_expr = ((Byte **)(fp->defaultArgs.c))
                         [ai + fp->defaultArgs.len];
            if (eval1(&default_expr, &def_returnVar, &EVALARG_EVALUATE) == FAIL) {
                default_arg_err = 1;
                break;
            }
         }

         namelen = STRLEN(name);
      } else {
          if ((fp->uf_flags & FC_NOARGS) != 0)
         // Bail out if no a: arguments used (in lambda).
         break;

          // "..." argument a:1, a:2, etc.
          namelen = eeSnprintf(numbuf, sizeof(numbuf), "%d", ai + 1);
          name = numbuf;
      }
      if (fixvar_idx < FIXVAR_CNT && namelen <= VAR_SHORT_LEN) {
          v = &fc->fc_fixvar[fixvar_idx++].var;
          v->flags = DI_FLAGS_RO | DI_FLAGS_FIX;
          STRCPY(v->key, name);
      } else {
         v = dictitem_alloc(mbText(name));
         if (v == NULL)
            break;
         v->flags |= DI_FLAGS_RO | DI_FLAGS_FIX;
      }

      // Note: the values are copied directly to avoid alloc/free.
      // "argvars" must have VAR_FIXED for v_lock.
      v->c = isdefault ? def_returnVar : argvars[i];
      v->c.lock = VAR_FIXED;

      if (isdefault)
          // Need to free this later, no matter where it's stored.
          tv_to_free[tv_to_free_len++] = &v->c;

      if (addlocal) {
          // Named arguments should be accessed without the "a:" prefix in
          // lambda expressions. Add to the l: dict.
          copy_tv(OUT &v->c, &v->c);
          hash_add(&fc->localVars.hashTable, textOfDi(v), S"local variable");
      } else
          hash_add(&fc->argVars.hashTable, textOfDi(v), S"add variable");

      if (ai >= 0 && ai < MAX_FUNC_ARGS) {
          ListItem *li = &fc->fc_l_listitems[ai];

          li->c = argvars[i];
          li->c.lock = VAR_FIXED;
          list_append(&fc->arguments, li);
      }
   }

   // Don't redraw while executing the function.
   ++isRedrawingDisabledG;

   estack_push_ufunc(fp, 1);
   ESTACK_CHECK_SETUP;
   if (p_verbose >= 12) {
      ++no_wait_return;
      verbose_enter_scroll();

      smsg(_("calling %s"), SOURCING_NAME);
      if (p_verbose >= 14) {
         Byte   builder[MSG_BUF_LEN];
         Byte   numbuf2[NUMBUFLEN];
         Byte   *tofree;
         Byte   *s;

         msg_puts(S"(");
         for (i = 0; i < argcount; ++i) {
            if (i > 0)
                msg_puts(S", ");
            if (argvars[i].tag == VAR_NUMBER)
                msg_outnum((long)argvars[i].number);
            else {
               // Do not want errors such as E724 here.
               ++emsg_off;
               s = tv2string(&argvars[i], &tofree, numbuf2, 0);
               --emsg_off;
               if (s) {
                  if (eeglStrSize(s) > MSG_BUF_CLEN) {
                     trunc_string(s, builder, MSG_BUF_CLEN, MSG_BUF_LEN);
                     s = builder;
                  }
                  msg_puts(s);
                  eeglFree(tofree);
               }
            }
         }
         msg_puts(S")");
      }
      msg_puts(S"\n");   // don't overwrite this either

      verbose_leave_scroll();
      --no_wait_return;
   }

   // "legacy" does not apply to commands in the function
   stickyCommandModifiersG = 0;

   save_scriptPosG = scriptPosG;
   scriptPosG = fp->scriptCtx;
   save_anyEmsgG = anyEmsgG;
   anyEmsgG = false;

   if (default_arg_err && (fp->uf_flags & FC_ABORT || trylevel > 0 )) {
      anyEmsgG = true;
      retval = FCERR_FAILED;
   } ei (islambda) {
      Byte *p = *(Byte **)fp->lines.c + 7;

      //A Lambda always has the command "return {expr}". It is much faster
      //to evaluate {expr} directly.
      ++ex_nesting_level;
      (void)eval1(&p, returnVar, &EVALARG_EVALUATE);
      --ex_nesting_level;
   } else
      // call doCommand() to execute the lines
      doCommand(NULL, get_func_line, (void *)fc, DOCMD_NOWAIT|DOCMD_VERBOSE|DOCMD_REPEAT);

   // Invoke functions added with ":defer".
   applyDeferred(currentCallS);

   if (isRedrawingDisabledG > 0)
      --isRedrawingDisabledG;

   // when the function was aborted because of an error, return -1
   if ((anyEmsgG && (fp->uf_flags & FC_ABORT)) || returnVar->tag == VAR_UNKNOWN) {
      clearVar(returnVar);
      returnVar->tag = VAR_NUMBER;
      returnVar->number = -1;
   }
    // when being verbose, mention the return value
   if (p_verbose >= 12) {
      ++no_wait_return;
      verbose_enter_scroll();

      if (aborting())
         smsg(_("%s aborted"), SOURCING_NAME);
      ei (fc->fc_returnVar->tag == VAR_NUMBER)
         smsg(_("%s returning #%ld"), SOURCING_NAME, (long)fc->fc_returnVar->number);
      else {
         Byte   builder[MSG_BUF_LEN];
         Byte   numbuf2[NUMBUFLEN];
         Byte   *tofree;

         //The value may be very long.  Skip the middle part, so that we
         //have some idea how it starts and ends. smsg() would always
         //truncate it at the end. Don't want errors such as E724 here.
         ++emsg_off;
         CS s = tv2string(fc->fc_returnVar, &tofree, numbuf2, 0);
         --emsg_off;
         if (s) {
            if (eeglStrSize(s) > MSG_BUF_CLEN) {
               trunc_string(s, builder, MSG_BUF_CLEN, MSG_BUF_LEN);
               s = builder;
            }
            smsg(_("%s returning %s"), SOURCING_NAME, s);
            eeglFree(tofree);
         }
      }
      msg_puts(S"\n");   // don't overwrite this either

      verbose_leave_scroll();
      --no_wait_return;
   }

   ESTACK_CHECK_NOW;
   estack_pop();
   scriptPosG = save_scriptPosG;

   stickyCommandModifiersG = save_stickyCommandModifiersG;

   if (p_verbose >= 12 && SOURCING_NAME != NULL) {
      ++no_wait_return;
   verbose_enter_scroll();

   smsg(_("continuing in %s"), SOURCING_NAME);
   msg_puts(S"\n");   // don't overwrite this either

   verbose_leave_scroll();
   --no_wait_return;
   }

   anyEmsgG |= save_anyEmsgG;
   funcdepth_decrement();
   for (i = 0; i < tv_to_free_len; ++i)
      clearVar(tv_to_free[i]);
   cleanup_function_call(fc);

   return retval;
}

//Check the argument count for user function "fp".
//Return FCERR_UNKNOWN if OK, FCERR_TOOFEW or FCERR_TOOMANY otherwise.
FnError
check_user_func_argcount(UserFunc *fp, int argcount) {
   int regular_args = fp->args.len;

   if (argcount < regular_args - fp->defaultArgs.len)
      return FCERR_TOOFEW;
   ei (!has_varargs(fp) && argcount > regular_args)
      return FCERR_TOOMANY;
   return FCERR_UNKNOWN;
}

// Call a user function after checking the arguments.
FnError
call_user_func_check(
   UserFunc       *fp,
   int       argcount,
   Var    *argvars,
   Var    *returnVar,
   FnExe   *funcexe,
   Bag       *selfdict)
{
    FnError error = FCERR_NONE;

   if (fp->uf_flags & FC_RANGE && funcexe->fe_doesrange != NULL)
   *funcexe->fe_doesrange = true;
    error = check_user_func_argcount(fp, argcount);
   if (error != FCERR_UNKNOWN)
   return error;

   if ((fp->uf_flags & FC_DICT) && selfdict == NULL) {
      error = FCERR_DICT;
   } else {
      int      did_save_redo = false;
      SaveRedo   save_redo;

      //Call the user function.
      //Save and restore search patterns, script variables and redo buffer.
      save_search_patterns();
      if (!ins_compl_active()) {
          saveRedobuff(&save_redo);
          did_save_redo = true;
      }
      ++fp->uf_calls;
      error = call_user_func(fp, argcount, argvars, returnVar, funcexe,
                  (fp->uf_flags & FC_DICT) ? selfdict : NULL);
      if (--fp->uf_calls <= 0 && fp->refcount <= 0)
          // Function was unreferenced while being used, free it now.
          func_clear_free(fp, false);
      if (did_save_redo)
          restoreRedobuff(&save_redo);
      restore_search_patterns();
   }

   return error;
}

private FnCallEntry *funccal_stack = NULL;

//Save the current function call pointer, and set it to NULL.
//Used when executing autocommands and for ":source".
void
save_funccal(FnCallEntry *entry) {
   entry->top_funccal = currentCallS;
   entry->next = funccal_stack;
   funccal_stack = entry;
   currentCallS = NULL;
}

void
restore_funccal(void) {
   if (funccal_stack == NULL)
      internal_error(S"restore_funccal()");
    else {
      currentCallS = funccal_stack->top_funccal;
      funccal_stack = funccal_stack->next;
   }
}

FnCall *
get_current_funccal(void) {
    return currentCallS;
}

//Return true when currently at the script level:
//- not in a function
//- not executing an autocommand
//Note that when an autocommand sources a script the result is false;
int
at_script_level(void) {
   return currentCallS == NULL && autocmd_match == NULL;
}

// Mark all functions of script "sid" as deleted.
void
delete_scrifntions(int sid) {
   EeSetItem   *hi;
   UserFunc   *fp;
   Ulong   todo = 1;
   Byte   builder[30];
   Unt   len;

   builder[0] = K_SPECIAL;
   builder[1] = KS_EXTRA;
   builder[2] = (int)KE_SNR;
   len = 3 + eeSnprintf(builder + 3, sizeof(builder) - 3, "%d_", sid);

   while (todo > 0) {
      todo = userDefinedFnsS.count;
      FOR_ALL_HASHTAB_ITEMS(&userDefinedFnsS, hi, todo)
         if (!HASHITEM_EMPTY(hi)) {
            fp = HI2UF(hi);
            if (STRNCMP(fp->uf_name, builder, len) == 0) {
               int changed = userDefinedFnsS.changes;

               fp->uf_flags |= FC_DEAD;

               if (fp->uf_calls > 0) {
                  // Function is executing, don't free it but do remove
                  // it from the hashtable.
                  if (func_remove(fp))
                      fp->refcount--;
               } else {
                  func_clear(fp, true);
                  // When clearing a function another function can be
                  // cleared as a side effect.  When that happens start over.
                  if (changed != userDefinedFnsS.changes)
                     break;
               }
            }
            --todo;
          }
    }
}

#if defined(EXITFREE) || defined(PROTO)
void
free_all_functions(void) {
   EeSetItem   *hi;
   UserFunc   *fp;
   Ulong   skipped = 0;
   Ulong   todo = 1;
   int      changed;

   // Clean up the currentCallS chain and the funccal stack.
   while (currentCallS != NULL) {
      clearVar(currentCallS->fc_returnVar);
      cleanup_function_call(currentCallS);
      if (currentCallS == NULL && funccal_stack != NULL)
          restore_funccal();
   }

   // First clear what the functions contain.  Since this may lower the
   // reference count of a function, it may also free a function and change
   // the hash table. Restart if that happens.
   while (todo > 0) {
      todo = userDefinedFnsS.count;
      FOR_ALL_HASHTAB_ITEMS(&userDefinedFnsS, hi, todo) {
         if (!HASHITEM_EMPTY(hi)) {
            // clear the def function index now
            fp = HI2UF(hi);
            fp->uf_flags &= ~FC_DEAD;

            // Only free functions that are not refcounted, those are
            // supposed to be freed when no longer referenced.
            if (func_name_refcount(fp->uf_name))
               ++skipped;
            else {
               changed = userDefinedFnsS.changes;
               func_clear(fp, true);
               if (changed != userDefinedFnsS.changes) {
                  skipped = 0;
                  break;
               }
            }
            --todo;
         }
      } 
    }

   // Now actually free the functions.  Need to start all over every time,
   // because func_free() may change the hash table.
   skipped = 0;
   while (userDefinedFnsS.count > skipped) {
      todo = userDefinedFnsS.count;
      FOR_ALL_HASHTAB_ITEMS(&userDefinedFnsS, hi, todo) {
         if (!HASHITEM_EMPTY(hi)) {
            --todo;
            // Only free functions that are not refcounted, those are
            // supposed to be freed when no longer referenced.
            fp = HI2UF(hi);
            if (func_name_refcount(fp->uf_name))
               ++skipped;
            else {
               if (func_free(fp, false) == OK) {
                  skipped = 0;
                  break;
               }
               // did not actually free it
               ++skipped;
            }
         }
      } 
   }
   if (skipped == 0)
      hash_clear(&userDefinedFnsS);

   free_def_functions();
}
#endif

//Return true if "name" looks like a builtin function name: starts with a
//lower case letter, doesn't contain AUTOLOAD_CHAR or ':', no "." after the name.
//"len" is the length of "name", or -1 for ZERO terminated.
private Boole
builtin_function(Text name) {
   if (!ASCII_ISLOWER(name.c[0]) || name.c[1] == ':')
      return false;
   for (Unt i = 0; i < name.len; ++i) {
      if (name.c[i] == AUTOLOAD_CHAR)
         return false;
      if (!isValidForScriptName(name.c[i])) {
         // "name.something" is not a builtin function
         if (name.c[i] == '.')
            return false;
         break;
      }
   }
   return true;
}

int
func_call(
   Byte   *name,
   Var   *args,
   PartiallyApplied   *partial,
   Bag   *selfdict,
   Var   *returnVar)
{
   List   *l = args->list;
   ListItem   *item;
   Var   argv[MAX_FUNC_ARGS + 1];
   int      argc = 0;
   int      r = 0;

   CHECK_LIST_MATERIALIZE(l);
   FOR_ALL_LIST_ITEMS(l, item) {
      if (argc == MAX_FUNC_ARGS - (partial == NULL ? 0 : partial->argc)) {
         emsg(_(e_too_many_arguments));
         break;
      }
      // Make a copy of each argument.  This is needed to be able to set
      // v_lock to VAR_FIXED in the copy without changing the original list.
      copy_tv(OUT &argv[argc++], &item->c);
   }

   if (item == NULL) {
      FnExe   funcexe;
      int      namelen = -1;

      CLEAR_FIELD(funcexe);
      funcexe.fe_firstline = curPor->cursor.lnum;
      funcexe.fe_lastline = curPor->cursor.lnum;
      funcexe.fe_evaluate = true;
      funcexe.fe_partial = partial;
      funcexe.fe_selfdict = selfdict;
      r = call_func(name, namelen, returnVar, argc, argv, &funcexe);
   }

   // Free the arguments.
   while (argc > 0)
      clearVar(&argv[--argc]);

   return r;
}

private int callback_depth = 0;

int
get_callback_depth(void) {
   return callback_depth;
}

//Invoke call_func() with a callback. Return FAIL if the callback could not be called.
int
call_callback(
   Callback   *callback,
   int      len,      // length of "name" or -1 to use strlen()
   Var   *returnVar,      // return value goes here
   int      argcount,   // number of "argvars"
   Var   *argvars)   // vars for arguments, must have "argcount" PLUS ONE elements!
{
   FnExe   funcexe;
   int      ret;

   if (callback->name == NULL || *callback->name == ZERO)
      return FAIL;

   if (callback_depth > p_mfd) {
      emsg(_(e_command_too_recursive));
      return FAIL;
   }

   CLEAR_FIELD(funcexe);
   funcexe.fe_evaluate = true;
   funcexe.fe_partial = callback->cb_partial;
   ++callback_depth;
   ret = call_func(callback->name, len, returnVar, argcount, argvars, &funcexe);
   --callback_depth;

   // When a :def function was called that uses :try an error would be turned
   // into an exception.  Need to give the error here.
   if (need_rethrow && current_exception != NULL && trylevel == 0) {
      need_rethrow = false;
      handle_did_throw();
   }

   return ret;
}

//call the 'callback' function and return the result as a number.
//Return -2 when calling the function fails.  Uses argv[0] to argv[argc - 1]
//for the function arguments. argv[argc] should have type VAR_UNKNOWN.
Long
call_callback_retnr(
   Callback   *callback,
   int      argcount,   // number of "argvars"
   Var   *argvars)   // vars for arguments, must have "argcount" PLUS ONE elements!
{
   Var   returnVar;
   Long   retval;

   if (call_callback(callback, -1, &returnVar, argcount, argvars) == FAIL)
      return -2;

   retval = varGetNumberChk(&returnVar, NULL);
   clearVar(&returnVar);
   return retval;
}

//Give an error message for the result of a function. Nothing if "error" is FCERR_NONE.
void
user_func_error(FnError error, Byte *name, int found_var) {
   switch (error) {
   case FCERR_UNKNOWN:
      if (found_var)
         emsg_funcname(e_not_callable_type_str, name);
      else
         emsg_funcname(e_unknown_function_str, name);
      break;
   case FCERR_NOTMETHOD:
      emsg_funcname(e_cannot_use_function_as_method_str, name);
      break;
   case FCERR_DELETED:
      emsg_funcname(e_function_was_deleted_str, name);
      break;
   case FCERR_TOOMANY:
      emsg_funcname(e_too_many_arguments_for_function_str, name);
      break;
   case FCERR_TOOFEW:
      emsg_funcname(e_not_enough_arguments_for_function_str, name);
      break;
   case FCERR_SCRIPT:
      emsg_funcname(e_using_sid_not_in_script_context_str, name);
      break;
   case FCERR_DICT:
      emsg_funcname(e_calling_dict_function_without_dictionary_str, name);
      break;
   case FCERR_OTHER:
   case FCERR_FAILED:
      // assume the error message was already given
      break;
   case FCERR_NONE:
      break;
    }
}

// Call a function with its resolved parameters
//
// FAIL when the function can't be called,  OK otherwise.
// Also return OK when an error was encountered while executing the function.
int
call_func(
   Arr(Byte) funcname,   // name of the function
   int      len,      // length of "name" or -1 to use strlen()
   Var   *returnVar,      // return value goes here
   int      argcount_in,   // number of "argvars"
   Var   *argvars_in,   // vars for arguments, must have "argcount" PLUS ONE elements!
   FnExe   *funcexe)   // more arguments
{
   int      ret = FAIL;
   FnError   error = FCERR_NONE;
   int      i;
   UserFunc   *fp = NULL;
   Byte   fname_buf[FLEN_FIXED + 1];
   Byte   *tofree = NULL;
   Byte   *fname = NULL;
   Byte   *name = NULL;
   int      argcount = argcount_in;
   Var   *argvars = argvars_in;
   Bag   *selfdict = funcexe->fe_selfdict;
   Var   argv[MAX_FUNC_ARGS + 1]; // used when "partial" or "funcexe->fe_basetv" is not NULL
   int      argv_clear = 0;
   int      argv_base = 0;

   // Initialize returnVar so that it is safe for caller to invoke clearVar(returnVar)
   // even when call_func() return FAIL.
   returnVar->tag = VAR_UNKNOWN;

   PartiallyApplied* partial = funcexe->fe_partial;
   if (partial)
      fp = partial->fn;
   if (!fp)
      fp = funcexe->fe_ufunc;

   if (!fp) {
      // Make a copy of the name, if it comes from a funcref variable it
      // could be changed or deleted in the called function.
      name = len > 0 ? copySubstr(funcname, len) : copyStr(funcname);

      fname = fname_trans_sid(name, fname_buf, &tofree, &error);
   }

   if (funcexe->fe_doesrange)
      *funcexe->fe_doesrange = false;

   if (partial) {
      // When the function has a partial with a dict and there is a dict
      // argument, use the dict argument.  That is backwards compatible.
      // When the dict was bound explicitly use the one from the partial.
      if (partial->self != NULL && (selfdict == NULL || !partial->isAuto))
          selfdict = partial->self;
      if (error == FCERR_NONE && partial->argc > 0) {
         for (argv_clear = 0; argv_clear < partial->argc; ++argv_clear) {
            if (argv_clear + argcount_in >= MAX_FUNC_ARGS) {
               error = FCERR_TOOMANY;
               goto theend;
            }
            copy_tv(OUT &argv[argv_clear], &partial->argv[argv_clear]);
         }
         for (i = 0; i < argcount_in; ++i)
            argv[i + argv_clear] = argvars_in[i];
         argvars = argv;
         argcount = partial->argc + argcount_in;
      }
   }

   if (error == FCERR_NONE && funcexe->fe_evaluate) {
      CS rfname = fname;
      Boole   is_global = false;

      // Skip "g:" before a function name.
      if (fp == NULL && fname[0] == 'g' && fname[1] == ':') {
         is_global = true;
         rfname = fname + 2;
      }

      returnVar->tag = VAR_NUMBER;   // default returnVar is number zero
      returnVar->number = 0;
      error = FCERR_UNKNOWN;

      if (fp || !builtin_function(mbText(rfname))) {
         // User defined function.
         if (!fp) {
            fp = find_func(rfname, is_global);
         }

         // Trigger FuncUndefined event, may load the function.
         if (!fp
             && applyAutocomms(EVENT_FUNCUNDEFINED, rfname, rfname, true, NULL)
             && !aborting()
         ) {
            // executed an autocommand, search for the function again
            fp = find_func(rfname, is_global);
         }
         // Try loading a package.
         if (!fp && scriautoload(rfname, true) && !aborting()) {
            // loaded a package, search for the function again
            fp = find_func(rfname, is_global);
         }

         if (fp && (fp->uf_flags & FC_DELETED))
            error = FCERR_DELETED;
         ei (fp) {
            if (funcexe->fe_argv_func != NULL) {
               // postponed filling in the arguments, do it now
               argcount = funcexe->fe_argv_func(argcount, argvars, argv_clear, fp);
            }

            if (funcexe->fe_basetv != NULL) {
               // Method call: base->Method()
               MEMMOVE(&argv[1], argvars, sizeof(Var) * argcount);
               argv[0] = *funcexe->fe_basetv;
               argcount++;
               argvars = argv;
               argv_base = 1;
            }


            if (error == FCERR_NONE || error == FCERR_UNKNOWN)
               error = call_user_func_check(
                  fp, argcount, argvars, returnVar, funcexe, selfdict
               );
         }
      } ei (funcexe->fe_basetv != NULL) {
         //expr->method(): Find the method name in the table, call its
         //implementation with the base as one of the arguments.
         error = call_internal_method(fname, argcount, argvars, returnVar, funcexe->fe_basetv);
      } else {
         // Find the function name in the table, call its implementation.
         error = call_internal_func(fname, argcount, argvars, returnVar);
      }

      //The function call (or "FuncUndefined" autocommand sequence) might have been aborted by an 
      //error, an interrupt, or an explicitly thrown exception that has not been caught so far. 
      //This situation can be tested for by calling aborting().  For an error in an internal
      //function or for the "E132" error in call_user_func(), however, the throw point at which the
      //"force_abort" flag (temporarily reset by emsg()) is normally updated has not been reached 
      //yet. We need to update that flag first to make aborting() reliable.
      update_force_abort();
   }
   if (error == FCERR_NONE)
      ret = OK;

theend:
   //Report an error unless the argument evaluation or function call has been
   //cancelled due to an aborting error, an interrupt, or an exception.
   if (!aborting())
      user_func_error(error, (name != NULL) ? name : funcname, funcexe->fe_found_var);

   // clear the copies made from the partial
   while (argv_clear > 0)
      clearVar(&argv[--argv_clear + argv_base]);

   eeglFree(tofree);
   eeglFree(name);

   return ret;
}

// Call a function without arguments, partial or dict. This is like call_func() when the call is
// only "FuncName()". To be used by "expr" options. Return NOTDONE when the function could not be 
// found
int
call_simple_func(
   CS funcname,   // name of the function
   Unt len,      // length of "name"
   OUT Var* returnVar      // return value goes here
){
   int      ret = FAIL;
   FnError   error = FCERR_NONE;
   Byte   fname_buf[FLEN_FIXED + 1];
   CS tofree = NULL;
   CS fname;
   CS rfname;
   Boole is_global = false;
   UserFunc   *fp;

   returnVar->tag = VAR_NUMBER;   // default returnVar is number zero
   returnVar->number = 0;

   // Make a copy of the name, an option can be changed in the function.
   CS name = copySubstr(funcname, len);
   if (!name)
      return ret;

   fname = fname_trans_sid(name, fname_buf, &tofree, &error);

   // Skip "g:" before a function name.
   if (fname[0] == 'g' && fname[1] == ':') {
      is_global = true;
      rfname = fname + 2;
   } else
      rfname = fname;
   fp = find_func(rfname, is_global);
   if (fp == NULL)
      ret = NOTDONE;
   ei (fp != NULL && (fp->uf_flags & FC_DELETED))
      error = FCERR_DELETED;
   ei (fp != NULL) {
      Var argvars[1];
      FnExe   funcexe;

      argvars[0].tag = VAR_UNKNOWN;
      CLEAR_FIELD(funcexe);
      funcexe.fe_evaluate = true;

      error = call_user_func_check(fp, 0, argvars, returnVar, &funcexe, NULL);
      if (error == FCERR_NONE)
          ret = OK;
   }

   user_func_error(error, name, false);
   eeglFree(tofree);
   eeglFree(name);

   return ret;
}

CS
printable_func_name(UserFunc *fp) {
   return fp->uf_name_exp != NULL ? fp->uf_name_exp : fp->uf_name;
}

// When "prev_changes" does not equal "changes" give an error and return
// true.  Otherwise return false.
private int
function_list_modified(int prev_changes) {
   if (prev_changes != userDefinedFnsS.changes) {
      emsg(_(e_function_list_was_modified));
      return true;
   }
   return false;
}

// List the head of the function: "function name(arg1, arg2)".
private int
list_func_head(UserFunc *fp, int indent) {
   int prev_changes = userDefinedFnsS.changes;
   int j;

   msg_start();

   // a timer at the more prompt may have deleted the function
   if (function_list_modified(prev_changes))
      return FAIL;

   if (indent)
      msg_puts(S"   ");
   msg_puts(S"function ");
   msg_puts(printable_func_name(fp));
   msg_putchar('(');
   for (j = 0; j < fp->args.len; ++j) {
      if (j)
         msg_puts(S", ");
      msg_puts(FUNCARG(fp, j));
      if (j >= fp->args.len - fp->defaultArgs.len) {
         msg_puts(S" = ");
         msg_puts(((Byte **)(fp->defaultArgs.c))[j - fp->args.len + fp->defaultArgs.len]);
      }
   }
   if (fp->uf_varargs) {
      if (j)
          msg_puts((CS)", ");
      msg_puts((CS)"...");
   }
   if (fp->uf_va_name) {
      if (!fp->uf_varargs) {
         if (j)
            msg_puts((CS)", ");
         msg_puts((CS)"...");
      }
      msg_puts(fp->uf_va_name);
   }
   msg_putchar(')');

   if (fp->uf_flags & FC_ABORT)
      msg_puts(S" abort");
   if (fp->uf_flags & FC_RANGE)
      msg_puts(S" range");
   if (fp->uf_flags & FC_DICT)
      msg_puts(S" dict");
   if (fp->uf_flags & FC_CLOSURE)
      msg_puts(S" closure");
   msg_clr_eos();
   if (p_verbose > 0)
      lastSetMsg(fp->scriptCtx);

   return OK;
}

//Get a function name, translating "<SID>" and "<SNR>". Also handle a Funcref in a List or 
//Dictionary. Return the function name in allocated memory, or NULL for failure.
//Set "*is_global" to true when the function must be global, unless "is_global" is NULL.
//flags:
//TFN_INT:       internal function name OK
//TFN_IN_CLASS:    function in a class
//TFN_QUIET:       be quiet
//TFN_NO_AUTOLOAD: do not use script autoloading
//TFN_NO_DEREF:    do not dereference a Funcref
//Advances "pp" to just after the function name (if no error).
CS
trans_function_name(
   OUT CS* name,
   OUT Boole* is_global,
   Boole skip,      // only find the end, don't evaluate
   Unt flags
) {
   return trans_function_name_ext(
         OUT name, OUT is_global, skip, flags, NULL, NULL, NULL, NULL
   );
}

//trans_function_name() with extra arguments.
//"fdp", "partial", "type" and "ufunc" can be NULL.
private CS
trans_function_name_ext(
   OUT CS* pp,
   OUT Boole* is_global,
   Boole skip,      // only find the end, don't evaluate
   Unt flags,
   FuncDict* fdp,      // return: info about dictionary used
   PartiallyApplied** partial,   // return: partial of a FuncRef
   OUT TypeSpec** type,      // return: type of funcref
   OUT UserFunc** ufunc   // return: function
){
   CS name = NULL;
   CS start;
   CS end;
   Byte   sid_buf[20];
   int      len;
   int      extra = 0;
   int      prefix_g = false;

   if (fdp)
      CLEAR_POINTER(fdp);
   start = *pp;

   //Check for hard coded <SNR>: already translated function ID (from a user command).
   if ((*pp)[0] == K_SPECIAL && (*pp)[1] == KS_EXTRA && (*pp)[2] == (int)KE_SNR) {
      *pp += 3;
      len = get_id_len(pp) + 3;
      return copySubstr(start, len);
   }

   //A name starting with "<SID>" or "<SNR>" is local to a script. But
   //don't skip over "s:", getLval() needs it for "s:dict.func".
   int lead = scriptCheckScriptPrefix(start);
   if (lead > 2)
      start += lead;

   //Note that TFN_ flags use the same values as GLV_ flags.
   Lval lv; 
   end = getLval(OUT &lv, 
      (GetLval){
         .name = mbText(start), .returnVar = null, .unlet = false, .skip = skip,
         .flags = flags | GLV_READ_ONLY | GLV_PREFER_FUNC, .fneFlag = lead > 2 ? 0 : FNE_CHECK_START
      }
   );
   if (end == start) {
      if (!skip) {
         emsg(_(e_function_name_required));
      } 
      goto theend;
   }
   if (!end || (lv.var != NULL && (lead > 2 || lv.ll_range))) {
      //Report an invalid expression in braces, unless the expression
      //evaluation has been cancelled due to an aborting error, an interrupt, or an exception.
      if (!aborting()) {
         if (end)
            showErrFmtMsg(_(e_invalid_argument_str), start);
      } else
          *pp = findNameEnd(mbText(start), NULL, FNE_INCL_BR).c;
      goto theend;
   }

   if (lv.ll_ufunc) {
      if (ufunc)
         *ufunc = lv.ll_ufunc;
      name = copySubstr(lv.ll_ufunc->uf_name, lv.ll_ufunc->uf_namelen);
      *pp = end;
      goto theend;
   }

   if (lv.var) {
      if (fdp) {
         fdp->bag = lv.bag;
         fdp->newKey = lv.newKey.c;
         lv.newKey = (Text){null, 0};
         fdp->item = lv.ll_di;
      }
      if (lv.var->tag == VAR_FUNC && lv.var->string) {
         name = copyStr(lv.var->string);
         *pp = end;
      } ei (lv.var->tag == VAR_PARTIAL && lv.var->partial) {
         name = copyStr(partial_name(lv.var->partial));
         *pp = end;
         if (partial)
            *partial = lv.var->partial;
      } else {
         if (!skip && !(flags & TFN_QUIET) && (!fdp || !lv.bag || !fdp->newKey))
            emsg(_(e_funcref_required));
         else
            *pp = end;
         name = NULL;
      }
      goto theend;
   }

   if (lv.name.len == 0) {
      // Error found, but continue after the function name.
      *pp = end;
      goto theend;
   }

   // Check if the name is a Funcref.  If so, use the value.
   if (lv.expandedName.len > 0) {
      len = lv.expandedName.len;
      Text t = deref_func_name(lv.expandedName, partial, type, (flags & TFN_NO_AUTOLOAD) != 0, NULL);
      len = t.len;
      name = (t.c != lv.expandedName.c) ? t.c : null;
   } ei (!(flags & TFN_NO_DEREF)) {
      len = (int)(end - *pp);
      Text t = deref_func_name(
         (Text){*pp, end - *pp}, partial, type, (flags & TFN_NO_AUTOLOAD) != 0, NULL
      );
      len = t.len;
      name = (t.c != *pp) ? t.c : null;
   }
   if (name) { // func ref?
      name = copyStr(name);
      *pp = end;
      if (name && STRNCMP(name, "<SNR>", 5) == 0) {
         // Change "<SNR>" to the byte sequence.
         name[0] = K_SPECIAL;
         name[1] = KS_EXTRA;
         name[2] = (int)KE_SNR;
         MEMMOVE(name + 3, name + 5, STRLEN(name + 5) + 1);
      }
      goto theend;
   }

   if (lv.expandedName.len > 0) {
      len = lv.expandedName.len;
      if (lead <= 2 && eq(lv.name, lv.expandedName) && STRNCMP(lv.name.c, "s:", 2) == 0){
         //When there was "s:" already or the name expanded to get a leading "s:" then remove it.
         lv.name.c += 2;
         lv.name.len -= 2;
         len -= 2;
         lead = 2;
      }
   } else {
      //skip over "s:" and "g:"
      if (lead == 2 || (lv.name.c[0] == 'g' && lv.name.c[1] == ':')) {
         if (lv.name.c[0] == 'g') {
            if (is_global) {
               *is_global = true;
            } else {
               prefix_g = true;
               extra = 2;
            }
         }
         lv.name.c += 2;
         lv.name.len -= 2;
      }
      len = lv.name.len;
   }
   if (len <= 0) {
      if (!skip) {
         emsg(_(e_function_name_required));
      } 
      goto theend;
   }

   //Copy the function name to allocated memory. Accept <SID>name() inside a script, translate 
   //into <SNR>123_name(). Accept <SNR>123_name() outside a script.
   if (skip)
      lead = 0;   //do nothing
   ei (lead > 0) {
      lead = 3;
      if ((lv.expandedName.len > 0  && eval_fname_sid(lv.expandedName.c)) || eval_fname_sid(*pp)) {
         Unt  sid_buflen;
         //It's script-local, "s:" or "<SID>"
         if (scriptPosG.sid <= 0) {
            emsg(_(e_using_sid_not_in_script_context));
            goto theend;
         }
         sid_buflen = eeSnprintf(
            sid_buf, sizeof(sid_buf), "%ld_", (long)scriptPosG.sid
         );
         lead += (int)sid_buflen;
      }
   }
   //The function name must start with an upper case letter (unless it is a
   //Vim9 class new() function or a Vim9 class private method or one of the
   //supported Vim9 object builtin functions)
   ei ((flags & TFN_INT) == 0 && (builtin_function(lv.name))) {
      showErrFmtMsg(_(e_function_name_must_start_with_capital_or_s_str), start);
      goto theend;
   }
   if (!skip && (flags & TFN_QUIET) == 0 && (flags & TFN_NO_DEREF) == 0) {
      CS cp = firstOccurrence(lv.name.c, ':');
      if (cp && cp < end) {
         showErrFmtMsg(_(e_function_name_cannot_contain_colon_str), start);
         goto theend;
      }
   }

   name = alloc(len + lead + extra + 1);
   if (!skip && (lead > 0)) {
      name[0] = K_SPECIAL;
      name[1] = KS_EXTRA;
      name[2] = (int)KE_SNR;
      if (lead > 3)   // If it's "<SID>"
         STRCPY(name + 3, sid_buf);
   } ei (prefix_g) {
      name[0] = 'g';
      name[1] = ':';
   }
   MEMMOVE(name + lead + extra, lv.name.c, (Unt)len);
   name[lead + extra + len] = ZERO;
   *pp = end;

theend:
   clear_lval(OUT &lv);
   return name;
}

//If the @funcname starts with "s:" or "<SID>", then expand it to the current script ID and 
//return the expanded function name. The caller should free the returned name. If not called 
//from a script context or the function name doesn't start with these prefixes, then return NULL.
//Don't check whether the script-local function exists or not.
CS
get_scriptlocal_funcname(CS funcname) {
   Byte   sid_buf[25];
   Unt   sid_buflen;
   int      off;
   Unt   newnamesize;
   Byte   *p = funcname;

   if (funcname == NULL)
      return NULL;

   if (STRNCMP(funcname, "s:", 2) != 0 && STRNCMP(funcname, "<SID>", 5) != 0) {
      // The function name does not have a script-local prefix.
      return NULL;
   } else
      off = *funcname == 's' ? 2 : 5;

   if (!SCRIPT_ID_VALID(scriptPosG.sid)) {
      emsg(_(e_using_sid_not_in_script_context));
      return NULL;
   }
   //Expand s: prefix into <SNR>nr_<name>
   sid_buflen = eeSnprintf(sid_buf, sizeof(sid_buf), "<SNR>%ld_", (long)scriptPosG.sid);
   newnamesize = sid_buflen + STRLEN(p + off) + 1;
   CS newname = alloc(newnamesize);
   eeSnprintf(newname, newnamesize, "%s%s", sid_buf, p + off);

   return newname;
}

//Return script-local "fname" with the 3-byte sequence replaced by
//printable <SNR> in allocated memory.
CS
alloc_printable_func_name(Byte *fname) {
   CS n = alloc(STRLEN(fname + 3) + 6);
   STRCPY(n, "<SNR>");
   STRCPY(n + 5, fname + 3);
   return n;
}

//Call trans_function_name(), except that a lambda is returned as-is.
//Return the name in allocated memory.
CS
save_function_name(
   OUT CS* name,
   OUT Boole* is_global,
   Boole skip,
   Unt flags,
   OUT FuncDict* fudi
) {
   CS p = *name;
   CS saved;

   if (STRNCMP(p, "<lambda>", 8) == 0) {
      p += 8;
      (void)parseLong(&p);
      saved = copySubstr(*name, p - *name);
      CLEAR_POINTER(fudi);
   } else
      saved = trans_function_name_ext(OUT &p, OUT is_global, skip, flags, fudi, NULL, NULL, NULL);
    
   *name = p;
   return saved;
}

//List functions. When "regmatch" is NULL all of then. Otherwise functions matching "regmatch".
void
list_functions(RegMatch *regmatch) {
   int      prev_changes = userDefinedFnsS.changes;
   Ulong   todo = userDefinedFnsS.count;
   EeSetItem   *hi;

   for (hi = userDefinedFnsS.array; todo > 0 && !gotInterruptG; ++hi) {
      if (!HASHITEM_EMPTY(hi)) {
          UserFunc   *fp = HI2UF(hi);

         --todo;
         if ((fp->uf_flags & FC_DEAD) == 0
             && (regmatch == NULL 
               ? !message_filtered(fp->uf_name) && !func_name_refcount(fp->uf_name)
               : !SAFE_isdigit(*fp->uf_name) && eeRegexec(regmatch, fp->uf_name, 0))
         ) {
            if (list_func_head(fp, false) == FAIL)
               return;
            if (function_list_modified(prev_changes))
               return;
         }
      }
   }
}

// ":function /pat": list functions matching pattern.
private Byte *
list_functions_matching_pat(Invocation* invo) {
   Byte   c;

   CS p = skip_regexp(invo->arg + 1, '/', true);
   if (!invo->skip) {
      RegMatch   regmatch;

      c = *p;
      *p = ZERO;
      regmatch.regprog = compileRegexp(invo->arg + 1, RE_MAGIC);
      *p = c;
      if (regmatch.regprog != NULL) {
          regmatch.rm_ic = p_ic;
          list_functions(&regmatch);
          eeRegFree(regmatch.regprog);
      }
   }
   if (*p == '/')
      ++p;

   return p;
}

// List function "name". Return the function pointer or NULL on failure.
private UserFunc*
listOneFunction(Invocation* invo, CS name, CS p, Boole is_global) {
   int j;

   if (!endsComm(skipwhite(p))) {
      showErrFmtMsg(_(e_trailing_characters_str), p);
      return NULL;
   }

   if (invo->skip || gotInterruptG)
      return NULL;

   UserFunc* fp = find_func(name, is_global);
   if (!fp) {
      emsg_funcname(e_undefined_function_str, invo->arg);
      return NULL;
   }

   // Check no function was added or removed from a timer, e.g. at
   // the more prompt. "fp" may then be invalid.
   int prev_changes = userDefinedFnsS.changes;

   if (list_func_head(fp, true) != OK)
      return fp;

   for (j = 0; j < fp->lines.len && !gotInterruptG; ++j) {
      if (FUNCLINE(fp, j) == NULL)
         continue;
      msg_putchar('\n');
      msg_outnum((long)(j + 1));
      if (j < 9)
         msg_putchar(' ');
      if (j < 99)
         msg_putchar(' ');
      if (function_list_modified(prev_changes))
         break;
      msg_prt_line(FUNCLINE(fp, j), false);
      out_flush();   // show a line at a time
      ui_breakcheck();
   }

   if (!gotInterruptG) {
      msg_putchar('\n');
      if (!function_list_modified(prev_changes)) {
         msg_puts(S"   endfunction");
      }
   }

   return fp;
}

int
get_func_arity(CS name, int *required, int *optional, int *varargs) {
   UserFunc* ufunc = NULL;
   int      argcount = 0;
   int      min_argcount = 0;

   Unt idx = find_internal_func(name);
   if (idx < UNT) {
      internal_func_get_argcount(idx, OUT &argcount, OUT &min_argcount);
      *varargs = false;
   } else {
      Byte fname_buf[FLEN_FIXED + 1];
      Byte* tofree = NULL;
      FnError error = FCERR_NONE;

      // May need to translate <SNR>123_ to K_SNR.
      CS fname = fname_trans_sid(name, fname_buf, OUT &tofree, OUT &error);
      if (error == FCERR_NONE)
         ufunc = find_func(fname, false);
      eeglFree(tofree);

      if (ufunc == NULL)
          return FAIL;

      argcount = ufunc->args.len;
      min_argcount = ufunc->args.len - ufunc->defaultArgs.len;
      *varargs = has_varargs(ufunc);
   }

   *required = min_argcount;
   *optional = argcount - min_argcount;

   return OK;
}

//Return 5 if "p" starts with "<SID>" or "<SNR>" (ignoring case).
//Return 2 if "p" starts with "s:". 0 otherwise.
int
scriptCheckScriptPrefix(CS p) {
   //Use caseInsensitiveCompareNChars() because in Turkish comparing the "I" may not work with
   //the standard library function.
   if (p[0] == '<' && (MB_STRNICMP(p + 1, "SID>", 4) == 0 || MB_STRNICMP(p + 1, "SNR>", 4) == 0))
      return 5;
   if (p[0] == 's' && p[1] == ':')
      return 2;
   return 0;
}

int
translated_function_exists(CS name, Boole is_global) {
   if (builtin_function(mbText(name)))
      return has_internal_func(name);
   return find_func(name, is_global) != NULL;
}

//Return true when "ufunc" has old-style "..." varargs or named varargs "...name: type".
int
has_varargs(UserFunc *ufunc) {
   return ufunc->uf_varargs || ufunc->uf_va_name != NULL;
}

//Return true if a function "name" exists. If "no_deref" is true, do not dereference a Funcref.
int
function_exists(CS name, int no_deref) {
   CS nm = name;
   int n = false;
   Boole is_global = false;

   Unt flag = TFN_INT | TFN_QUIET | TFN_NO_AUTOLOAD;
   if (no_deref)
      flag |= TFN_NO_DEREF;
   CS p = trans_function_name(OUT &nm, &is_global, false, flag);
   nm = skipwhite(nm);

   //Only accept "funcname", "funcname ", "funcname (..." and "funcname(...", never "funcname!..."
   if (p && (*nm == ZERO || *nm == '('))
      n = translated_function_exists(p, is_global);
   eeglFree(p);
   return n;
}

// Function given to expandGeneric() to obtain the list of user defined function names.
CS
get_user_func_name(Expand *xp, int idx) {
   static Ulong   done;
   static int      changed;
   static EeSetItem   *hi;
   UserFunc* fp;

   if (idx == 0) {
      done = 0;
      hi = userDefinedFnsS.array;
      changed = userDefinedFnsS.changes;
   }
   if (changed == userDefinedFnsS.changes && done < userDefinedFnsS.count) {
      int len;

      if (done++ > 0)
         ++hi;
      while (HASHITEM_EMPTY(hi))
         ++hi;
      fp = HI2UF(hi);

      //don't show dead, dict and lambda functions
      if ((fp->uf_flags & FC_DEAD) || (fp->uf_flags & FC_DICT)
               || STRNCMP(fp->uf_name, "<lambda>", 8) == 0)
         return S"";

      if (fp->uf_namelen + 4 >= IOSIZE)
         return fp->uf_name;   // prevents overflow

      len = cat_func_name(IObuff, IOSIZE, fp);
      if (xp->context != EXPAND_USER_FUNC && xp->context != EXPAND_DISASSEMBLE) {
         STRCPY(IObuff + len, "(");
         if (!has_varargs(fp) && fp->args.len == 0) {
            ++len;
            STRCPY(IObuff + len, ")");
         }
      }
      return IObuff;
    }
    return NULL;
}

//":delfunction {name}"
void
c_delfunction(Invocation* invo) {
   UserFunc   *fp = NULL;
   FuncDict   fudi;
   Boole is_global = false;

   CS p = invo->arg;
   CS name = trans_function_name_ext(OUT &p, &is_global, invo->skip, 0, &fudi, NULL, NULL, NULL);
   eeglFree(fudi.newKey);
   if (name == NULL) {
      if (fudi.bag && !invo->skip)
         emsg(_(e_funcref_required));
      return;
   }
   if (!endsComm(skipwhite(p))) {
      eeglFree(name);
      showErrFmtMsg(_(e_trailing_characters_str), p);
      return;
   }

   if (numbered_function(name) && !fudi.bag) {
      if (!invo->skip)
         showErrFmtMsg(_(e_invalid_argument_str), invo->arg);
      eeglFree(name);
      return;
   }
   if (!invo->skip)
      fp = find_func(name, is_global);
   eeglFree(name);

   if (invo->skip) {
      return;
   }
   if (!fp) {
      if (!invo->forceit)
         showErrFmtMsg(_(e_unknown_function_str), invo->arg);
      return;
   }
   if (fp->uf_calls > 0) {
      showErrFmtMsg(_(e_cannot_delete_function_str_it_is_in_use), invo->arg);
      return;
   }

   if (fudi.bag) {
      //Delete the dict item that refers to the function, it will
      //invoke func_unref() and possibly delete the function.
      dictitem_remove(fudi.bag, fudi.item, S"delfunction");
   } else {
      //A normal function (not a numbered function or lambda) has a
      //refcount of 1 for the entry in the hashtable.  When deleting
      //it and the refcount is more than one, it should be kept. A numbered function and lambda 
      //should be kept if the refcount is one or more.
      if (fp->refcount > (func_name_refcount(fp->uf_name) ? 0 : 1)) {
         //Function is still referenced somewhere.  Don't free it but
         //do remove it from the hashtable.
         if (func_remove(fp))
            fp->refcount--;
      } else
         func_clear_free(fp, false);
   }
}

// Unreference a Function: decrement the reference count and free it when it becomes zero.
void
func_unref(CS name) {
   if (!name || !func_name_refcount(name))
      return;
   UserFunc* fp = find_func(name, false);
   if (!fp && numbered_function(name)) {
#ifdef EXITFREE
      if (!entered_free_all_mem)
#endif
         internal_error((CS)"func_unref()");
   }
   func_ptr_unref(fp);
}

//Unreference a Function: decrement the reference count and free it when it becomes zero.
//Also when it becomes one and uf_partial points to the function.
void
func_ptr_unref(UserFunc* fp) {
   if (fp && (--fp->refcount <= 0) && fp->uf_calls == 0)
      // Only delete it when it's not being used. Otherwise it's done when "uf_calls" becomes 0
      func_clear_free(fp, false);
}

// Count a reference to a Function.
void
func_ref(CS name) {

   if (name == NULL || !func_name_refcount(name))
      return;
   UserFunc* fp = find_func(name, false);
   if (fp)
      ++fp->refcount;
   ei (numbered_function(name))
      // Only give an error for a numbered function.
      // Fail silently, when named or lambda function isn't found.
      internal_error((CS)"func_ref()");
}

// Count a reference to a Function.
void
func_ptr_ref(UserFunc *fp) {
   if (fp)
      ++fp->refcount;
}

//Return true if items in "fc" do not have "copyID". That means they are not
//referenced from anywhere that is in use.
private int
can_free_funccal(FnCall *fc, int copyID) {
    return (fc->arguments.copyId != copyID
       && fc->localVars.copyId != copyID
       && fc->argVars.copyId != copyID
       && fc->copyId != copyID);
}

// ":return [expr]"
void
c_return(Invocation* invo) {
   Byte   *arg = invo->arg;
   Var   returnVar;
   EvalCtx   evalarg;

   if (!currentCallS) {
      emsg(_(e_return_not_inside_function));
      return;
   }

   init_evalarg(&evalarg);
   evalarg.eval_flags = invo->skip ? 0 : EVAL_EVALUATE;

   if (invo->skip)
      ++emsg_skip;

   if ((*arg != ZERO && *arg != '|' && *arg != '\n')
              && eval0(arg, &returnVar, &evalarg) != FAIL
   ) {
      if (invo->skip)
         clearVar(&returnVar);
   }
   //It's safer to return also on error.
   ei (!invo->skip) {
      //In return statement, cause_abort should be force_abort.
      update_force_abort();
   }

   if (invo->skip)
      --emsg_skip;
   clear_evalarg(&evalarg, invo);
}

// Lower level implementation of "call".  Only called when not skipping.
private int
callInner(
   Invocation* invo,
   CS name,
   OUT CS* arg,
   CS startarg,
   FnExe* funcexe_init,
   EvalCtx* evalarg
) {
   int      doesrange;
   Var   returnVar;
   int      failed = false;

   LineNr lnum = invo->line1;
   for ( ; lnum <= invo->line2; ++lnum) {
      FnExe funcexe;

      if (invo->addr_count > 0) {
         if (lnum > curBook->mem.lineCount) {
            // If the function deleted lines or switched to another buffer
            // the line number may become invalid.
            emsg(_(e_invalid_range));
            break;
         }
         curPor->cursor.lnum = lnum;
         curPor->cursor.col = 0;
         curPor->cursor.coladd = 0;
      }
      *arg = startarg;

      funcexe = *funcexe_init;
      funcexe.fe_doesrange = &doesrange;
      returnVar.tag = VAR_UNKNOWN;   // clearVar() uses this
      if (get_func_tv(name, -1, &returnVar, arg, evalarg, &funcexe) == FAIL) {
         failed = true;
         break;
      }
      if (has_watchexpr())
         dbg_check_breakpoint(invo);

      // Handle a function returning a Funcref, Dictionary or List.
      if (handle_subscript(arg, &returnVar, &EVALARG_EVALUATE, true) == FAIL) {
         failed = true;
         break;
      }

      clearVar(&returnVar);
      if (doesrange)
         break;

      // Stop when immediately aborting on error, or when an interrupt occurred or an exception 
      // was thrown but not caught. get_func_tv() returned OK, so that the check for trailing
      // characters below is executed.
      if (aborting())
         break;
   }
   return failed;
}

// Core part of ":defer func(arg)".  "arg" points to the "(" and is advanced. Return FAIL or OK.
private int
deferInner(CS name, CS* arg, PartiallyApplied* partial, EvalCtx* evalarg) {
   Var   argvars[MAX_FUNC_ARGS + 1];   // vars for arguments
   int      partial_argc = 0;      // number of partial arguments
   int      argcount = 0;         // number of arguments found

   if (currentCallS == NULL) {
      showErrFmtMsg(_(e_str_not_inside_function), "defer");
      return FAIL;
   }
   if (partial != NULL) {
      if (partial->self != NULL) {
         emsg(_(e_cannot_use_partial_with_dictionary_for_defer));
         return FAIL;
      }
      if (partial->argc > 0) {
         partial_argc = partial->argc;
         for (int i = 0; i < partial_argc; ++i)
            copy_tv(OUT &argvars[i], &partial->argv[i]);
      }
   }
   int is_builtin = builtin_function(mbText(name));
   int r = get_func_arguments(arg, OUT evalarg, false, argvars + partial_argc, OUT &argcount);
   argcount += partial_argc;

   if (r == OK) {
      if (is_builtin) {
         Unt idx = find_internal_func(name);

         if (idx == UNT) {
            emsg_funcname(e_unknown_function_str, name);
            r = FAIL;
         } ei (check_internal_func(idx, argcount) == -1)
            r = FAIL;
      } else {
         UserFunc *ufunc = find_func(name, false);

         // we tolerate an unknown function here, it might be defined later
         if (ufunc != NULL) {
            FnError error = check_user_func_argcount(ufunc, argcount);
            if (error != FCERR_UNKNOWN) {
                user_func_error(error, name, false);
                r = FAIL;
            }
         }
      }
   }

   if (r == FAIL) {
      while (--argcount >= 0)
          clearVar(&argvars[argcount]);
      return FAIL;
   }
   return add_defer(name, argcount, argvars);
}

// Return true if currently inside a function call. Give an error message and return false when not.
int
can_add_defer(void) {
   if (get_current_funccal() == NULL) {
      showErrFmtMsg(_(e_str_not_inside_function), "defer");
      return false;
   }
   return true;
}

//Add a deferred call for "name" with arguments "argvars[argcount]".
//Consume "argvars[]". Return OK or FAIL.
int
add_defer(CS name, int argcount_arg, Arr(Var) argvars) {
   CS saved_name = copyStr(name);
   int argcount = argcount_arg;
   Deferral* dr;
   int ret = FAIL;

   if (currentCallS->fc_defer.ga_itemsize == 0)
       ga_init2(&currentCallS->fc_defer, sizeof(Deferral), 10);
   if (ga_grow(&currentCallS->fc_defer, 1) == FAIL)
       goto theend;
   dr = ((Deferral *)currentCallS->fc_defer.c) + currentCallS->fc_defer.len++;
   dr->dr_name = saved_name;
   dr->argc = argcount;
   while (argcount > 0) {
      --argcount;
      dr->dr_argvars[argcount] = argvars[argcount];
   }
   ret = OK;

theend:
    while (--argcount >= 0)
   clearVar(&argvars[argcount]);
    return ret;
}

// Invoked after a function has finished: invoke ":defer" functions.
private void
applyDeferred(FnCall *funccal) {
   for (int idx = funccal->fc_defer.len - 1; idx >= 0; --idx) {
      Deferral* dr = ((Deferral *)funccal->fc_defer.c) + idx;

      if (dr->dr_name == NULL)
         // already being called, can happen if function does ":qa"
         continue;

      FnExe   funcexe;
      CLEAR_FIELD(funcexe);
      funcexe.fe_evaluate = true;

      Var returnVar;
      returnVar.tag = VAR_UNKNOWN;   // clearVar() uses this

      CS name = dr->dr_name;
      dr->dr_name = NULL;

      //If the deferred function is called after an exception, then only the
      //first statement in the function will be executed (because of the
      //exception). So save and restore the try/catch/throw exception state.
      ExceptionState estate;
      exception_state_save(&estate);
      exception_state_clear();

      call_func(name, -1, &returnVar, dr->argc, dr->dr_argvars, &funcexe);

      exception_state_restore(&estate);

      clearVar(&returnVar);
      eeglFree(name);
      for (int i = dr->argc - 1; i >= 0; --i)
         clearVar(&dr->dr_argvars[i]);
   }
   ga_clear(&funccal->fc_defer);
}

private void
invoke_funccall_defer(FnCall *fc) {
   // legacy function
   applyDeferred(fc);
}

// Called when exiting: call all defer functions.
void
invoke_all_defer(void) {
   for (FnCall* fc = currentCallS; fc != NULL; fc = fc->fc_caller)
      invoke_funccall_defer(fc);

   for (FnCallEntry* fce = funccal_stack; fce != NULL; fce = fce->next) {
      for (FnCall* fc = fce->top_funccal; fc != NULL; fc = fc->fc_caller)
          invoke_funccall_defer(fc);
   } 
}

//":1,25call func(arg1, arg2)"   function call.
//":defer func(arg1, arg2)"    deferred function call.
void
c_call(Invocation* invo) {
   CS arg = invo->arg;
   CS startarg;
   CS name;
   CS tofree;
   int      failed = false;
   FuncDict   fudi;
   UserFunc   *ufunc = NULL;
   PartiallyApplied   *partial = NULL;
   EvalCtx   evalarg;
   Boole found_var = false;

   fillEvalArgFromInvo(&evalarg, invo, invo->skip);
   if (invo->skip) {
      Var returnVar;

      // trans_function_name() doesn't work well when skipping, use eval0()
      // instead to skip to any following command, e.g. for:
      //   :if 0 | call dict.foo().bar() | endif
      ++emsg_skip;
      if (eval0(invo->arg, &returnVar, &evalarg) != FAIL)
         clearVar(&returnVar);
      --emsg_skip;
      clear_evalarg(&evalarg, invo);
      return;
   }

   tofree = trans_function_name_ext(
      OUT &arg, NULL, false, TFN_INT, &fudi, &partial, NULL, &ufunc
   );
   if (fudi.newKey != NULL) {
      // Still need to give an error message for missing key.
      showErrFmtMsg(_(e_key_not_present_in_dictionary_str), fudi.newKey);
      eeglFree(fudi.newKey);
   }
   if (tofree == NULL)
      return;

   //Increase refcount on dictionary, it could get deleted when evaluating the arguments
   if (fudi.bag)
      ++fudi.bag->refcount;

   // If it is the name of a variable of type VAR_FUNC or VAR_PARTIAL use its
   // contents. For VAR_PARTIAL get its partial, unless we already have one
   // from trans_function_name().
   name = deref_func_name(text(tofree), partial != NULL ? NULL : &partial,
            NULL, false, OUT &found_var).c;

   // Skip white space to allow ":call func ()".  Not good, but required for
   // backward compatibility.
   startarg = skipwhite(arg);
   if (*startarg != '(') {
      showErrFmtMsg(_(e_missing_parenthesis_str), invo->arg);
      goto end;
   }

   if (invo->id == C_defer) {
      arg = startarg;
      failed = deferInner(name, &arg, partial, &evalarg) == FAIL;
   } else {
      FnExe funcexe;

      CLEAR_FIELD(funcexe);
      funcexe.fe_ufunc = ufunc;
      funcexe.fe_partial = partial;
      funcexe.fe_selfdict = fudi.bag;
      funcexe.fe_firstline = invo->line1;
      funcexe.fe_lastline = invo->line2;
      funcexe.fe_found_var = found_var;
      funcexe.fe_evaluate = true;
      failed = callInner(invo, name, &arg, startarg, &funcexe, &evalarg);
   }

   //When inside :try we need to check for following "| catch" or "| endtry".
   //Not when there was an error, but do check if an exception was thrown.
   if ((!aborting() || did_throw) && (!failed || invo->cstack->tryLevel > 0)) {
      // Check for trailing illegal characters and a following command.
      arg = skipwhite(arg);
      if (!endsComm(arg)) {
         if (!failed && !aborting()) {
            emsg_severe = true;
            showErrFmtMsg(_(e_trailing_characters_str), arg);
         }
      }
   }
   // Must be after using "arg", it may point into memory cleared here.
   clear_evalarg(&evalarg, invo);

end:
   bagUnref(fudi.bag);
   eeglFree(tofree);
}

//Return from a function. Possibly make the return pending. Also called for a pending return 
//at the ":endtry" or after returning from an extra doCommand().  "reanimate" is used in the 
//latter case.  "is_cmd" is set when called due to a ":return" command.  "returnVar" may point to 
//a Var with the return returnVar.  Return true when the return can be carried out,
//false when the return gets pending.
int
do_return(
   Invocation* invo,
   int reanimate,
   int is_cmd,
   void* returnVar
){
   if (reanimate) // Undo the return.
      currentCallS->fc_returned = false;

   CondStack   *cstack = invo->cstack;
   //Cleanup (and inactivate) conditionals, but stop when a try conditional not in its finally 
   //clause (which then is to be executed next) is found. In this case, make the ":return" 
   //pending for execution at the ":endtry". Otherwise, return normally.
   int idx = cleanup_conditionals(invo->cstack, 0, true);
   if (idx >= 0) {
      cstack->pending[idx] = CSTP_RETURN;

      if (!is_cmd && !reanimate)
          // A pending return again gets pending.  "returnVar" points to an
          // allocated variable with the returnVar of the original ":return"'s
          // argument if present or is NULL else.
          cstack->pend.csp_rv[idx] = returnVar;
      else {
         // When undoing a return in order to make it pending, get the stored return returnVar.
         if (reanimate)
            returnVar = currentCallS->fc_returnVar;

         if (returnVar) {
            // Store the value of the pending return.
            if ((cstack->pend.csp_rv[idx] = allocVar()) != NULL)
               *(Var *)cstack->pend.csp_rv[idx] = *(Var *)returnVar;
            else
               emsg(_(e_out_of_memory));
         } else
            cstack->pend.csp_rv[idx] = NULL;

         if (reanimate) {
            //The pending return value could be overwritten by a ":return" without argument in a 
            //finally clause; reset the default return value.
            currentCallS->fc_returnVar->tag = VAR_NUMBER;
            currentCallS->fc_returnVar->number = 0;
         }
      }
      report_make_pending(CSTP_RETURN, returnVar);
   } else {
      currentCallS->fc_returned = true;

      //If the return is carried out now, store the return value.  For
      //a return immediately after reanimation, the value is already there.
      if (!reanimate && returnVar) {
         clearVar(currentCallS->fc_returnVar);
         *currentCallS->fc_returnVar = *(Var *)returnVar;
         if (!is_cmd)
            eeglFree(returnVar);
      }
   }

   return idx < 0;
}

// Free the variable with a pending return value.
void
discard_pending_return(void *returnVar) {
   freeVar((Var *)returnVar);
}

//Generate a return command for producing the value of "returnVar".  The result
//is an allocated string.  Used by report_pending() for verbose messages.
CS
get_return_cmd(void* returnVar) {
   CS s = NULL;
   CS tofree = NULL;
   Byte numbuf[NUMBUFLEN];
   Unt slen = 0;
   Unt IObufflen;

   if (returnVar)
      s = echo_string((Var *)returnVar, &tofree, numbuf, 0);
   if (s == NULL)
      s = S"";
   else
      slen = STRLEN(s);

   STRCPY(IObuff, ":return ");
   STRNCPY(IObuff + 8, s, IOSIZE - 8);
   IObufflen = 8 + slen;
   if (IObufflen >= IOSIZE) {
      STRCPY(IObuff + IOSIZE - 4, "...");
      IObufflen = IOSIZE - 1;
   }
   eeglFree(tofree);
   return copySubstr(IObuff, IObufflen);
}

//Get next function line. Called by doCommand() to get the next line.
//Return allocated string, or NULL for end of function.
CS
get_func_line(Unt c UNUSED, void* cookie, int indent UNUSED, GetlineAlgo options UNUSED) {
   FnCall   *fcp = (FnCall *)cookie;
   UserFunc   *fp = fcp->fn;
   CS retval;
   ArrayList   *gap;  // growarray with function lines

   // If breakpoints have been added/deleted need to check for it.
   if (fcp->fc_dbg_tick != debug_tick) {
      fcp->fc_breakpoint = dbg_find_breakpoint(false, fp->uf_name, SOURCING_LNUM);
      fcp->fc_dbg_tick = debug_tick;
   }

   gap = &fp->lines;
   if (((fp->uf_flags & FC_ABORT) && anyEmsgG && !aborted_in_try()) || fcp->fc_returned)
      retval = NULL;
   else {
      // Skip NULL lines (continuation lines).
      while (fcp->lineNr < gap->len && ((Byte **)(gap->c))[fcp->lineNr] == NULL)
         ++fcp->lineNr;
      if (fcp->lineNr >= gap->len)
         retval = NULL;
      else {
         retval = copyStr(((Byte **)(gap->c))[fcp->lineNr++]);
         SOURCING_LNUM = fcp->lineNr;
      }
   }

    // Did we encounter a breakpoint?
   if (fcp->fc_breakpoint != 0 && fcp->fc_breakpoint <= SOURCING_LNUM) {
      dbg_breakpoint(fp->uf_name, SOURCING_LNUM);
      // Find next breakpoint.
      fcp->fc_breakpoint = dbg_find_breakpoint(false, fp->uf_name, SOURCING_LNUM);
      fcp->fc_dbg_tick = debug_tick;
   }

    return retval;
}

//Return true if the currently active function should be ended, because a
//return was encountered or an error occurred.  Used inside a ":while".
int
func_has_ended(void* cookie) {
   FnCall* fcp = (FnCall*)cookie;

   // Ignore the "abort" flag if the abortion behavior has been changed due to
   // an error inside a try conditional.
   return (((fcp->fn->uf_flags & FC_ABORT) && anyEmsgG && !aborted_in_try()) || fcp->fc_returned);
}

// return true if cookie indicates a function which "abort"s on errors.
int
func_has_abort(void* cookie) {
   return ((FnCall *)cookie)->fn->uf_flags & FC_ABORT;
}

//Turn "dict.Func" into a partial for "Func" bound to "dict". Don't do this when "Func" is already
//a partial that was bound explicitly (auto is false). Change "returnVar" in-place.
//Return the updated "selfdict_in".
Bag *
make_partial(Bag* selfdict_in, Var* returnVar) {
   CS fname;
   UserFunc   *fp = NULL;
   Byte   fname_buf[FLEN_FIXED + 1];
   Bag   *selfdict = selfdict_in;

   if (returnVar->tag == VAR_PARTIAL  && returnVar->partial != NULL
                 && returnVar->partial->fn != NULL)
      fp = returnVar->partial->fn;
   else {
      fname = returnVar->tag == VAR_FUNC ? returnVar->string
                   : returnVar->partial == NULL ? NULL
                        : returnVar->partial->name;
      if (fname == NULL) {
          // There is no point binding a dict to a NULL function, just create a function reference
          returnVar->tag = VAR_FUNC;
          returnVar->string = NULL;
      } else {
          Byte   *tofree = NULL;
          FnError   error;

          // Translate "s:func" to the stored function name.
          fname = fname_trans_sid(fname, fname_buf, &tofree, &error);
          fp = find_func(fname, false);
          eeglFree(tofree);
      }
    }

   if (fp && (fp->uf_flags & FC_DICT)) {
      PartiallyApplied   *pt = ALLOC_CLEAR_ONE(PartiallyApplied);
      if (pt) {
         pt->refcount = 1;
         pt->self = selfdict;
         pt->isAuto = true;
         selfdict = NULL;
         if (returnVar->tag == VAR_FUNC) {
            // Just a function: Take over the function name and use selfdict.
            pt->name = returnVar->string;
         } else {
            PartiallyApplied   *ret_pt = returnVar->partial;
            //Partial: copy the function name, use selfdict and copy args. Can't take over name 
            //or args, the partial might be referenced elsewhere.
            if (ret_pt->name != NULL) {
               pt->name = copyStr(ret_pt->name);
               func_ref(pt->name);
            } else {
               pt->fn = ret_pt->fn;
               func_ptr_ref(pt->fn);
            }
            if (ret_pt->argc > 0) {
               pt->argv = ALLOC_MULT(Var, ret_pt->argc);
               pt->argc = ret_pt->argc;
               for (int i = 0; i < pt->argc; i++)
                  copy_tv(OUT &pt->argv[i], &ret_pt->argv[i]);
            }
            partial_unref(ret_pt);
          }
          returnVar->tag = VAR_PARTIAL;
          returnVar->partial = pt;
      }
   }
   return selfdict;
}

// Return the name of the executed function.
CS
func_name(void* cookie) {
   return ((FnCall *)cookie)->fn->uf_name;
}

// Return the address holding the next breakpoint line for a funccall cookie.
LineNr *
func_breakpoint(void *cookie) {
   return &((FnCall *)cookie)->fc_breakpoint;
}

// Return the address holding the debug tick for a funccall cookie.
int *
func_dbg_tick(void *cookie) {
   return &((FnCall *)cookie)->fc_dbg_tick;
}

// Return the nesting level for a funccall cookie.
int
func_level(void *cookie) {
   return ((FnCall *)cookie)->fc_level;
}

//Return true when a function was ended by a ":return" command.
int
current_func_returned(void) {
   return currentCallS->fc_returned;
}

int
free_unref_funccal(int copyID, int testing) {
   int      did_free = false;
   int      did_free_funccal = false;
   FnCall   *fc, **pfc;

   for (pfc = &previous_funccal; *pfc != NULL; ) {
      if (can_free_funccal(*pfc, copyID)) {
         fc = *pfc;
         *pfc = fc->fc_caller;
         free_funccal_contents(fc);
         did_free = true;
         did_free_funccal = true;
      } else
         pfc = &(*pfc)->fc_caller;
   }
   if (did_free_funccal)
      // When a funccal was freed some more items might be garbage collected, so run again.
      (void)garbage_collect(testing);

   return did_free;
}

// Get function call environment based on backtrace debug level
private FnCall *
get_funccal(void) {
   int      i;
   FnCall   *funccal;
   FnCall   *temp_funccal;

   funccal = currentCallS;
   if (debug_backtrace_level > 0) {
      for (i = 0; i < debug_backtrace_level; i++) {
          temp_funccal = funccal->fc_caller;
          if (temp_funccal)
         funccal = temp_funccal;
          else
         // backtrace level overflow. reset to max
         debug_backtrace_level = i;
      }
   }
   return funccal;
}

// Return the hashtable used for local variables in the current funccal. 
// NULL if there is no current funccal.
EeSet *
get_funccal_local_ht(void) {
   if (!currentCallS || currentCallS->localVars.refcount == 0)
      return NULL;
   return &get_funccal()->localVars.hashTable;
}

// Return the l: scope variable. Return NULL if there is no current funccal.
DictItem *
get_funccal_local_var(void) {
   if (!currentCallS || currentCallS->localVars.refcount == 0)
      return NULL;
   return &get_funccal()->argVarsVar;
}

//Return the hashtable used for argument in the current funccal.
//Return NULL if there is no current funccal.
EeSet *
get_funccal_args_ht(void) {
   if (currentCallS == NULL || currentCallS->localVars.refcount == 0)
      return NULL;
   return &get_funccal()->argVars.hashTable;
}

// Return the a: scope variable. Return NULL if there is no current funccal.
DictItem *
get_funccal_args_var(void) {
   if (currentCallS == NULL || currentCallS->localVars.refcount == 0)
      return NULL;
   return &get_funccal()->argVarsVar;
}

// List function variables, if there is a function.
void
list_func_vars(int *first) {
   if (currentCallS != NULL && currentCallS->localVars.refcount > 0)
      list_hashtable_vars(&currentCallS->localVars.hashTable, S"l:", false, first);
}

//If "ht" is the hashtable for local variables in the current funccal, return
//the dict that contains it. Otherwise return NULL.
Bag *
get_current_funccal_dict(EeSet *ht) {
   if (currentCallS && ht == &currentCallS->localVars.hashTable)
      return &currentCallS->localVars;
   return NULL;
}

// Search EeSetItem in parent scope.
EeSetItem*
find_hi_in_scoped_ht(CS name, EeSet** pht) {
   FnCall* old_currentCallS = currentCallS;
   EeSet   *ht;
   EeSetItem   *hi = NULL;

   if (!currentCallS || currentCallS->fn->uf_scoped == NULL)
      return NULL;

   // Search in parent scope, which can be referenced from a lambda.
   currentCallS = currentCallS->fn->uf_scoped;
   while (currentCallS) {
      CS varname;
      ht = findVarHashTable(mbText(name), OUT &varname);
      if (ht && *varname != ZERO) {
         hi = hash_find(ht, mbText(varname));
         if (!HASHITEM_EMPTY(hi)) {
            *pht = ht;
            break;
         }
      }
      if (currentCallS == currentCallS->fn->uf_scoped)
         break;
      currentCallS = currentCallS->fn->uf_scoped;
   }
   currentCallS = old_currentCallS;

   return hi;
}

// Search variable in parent scope.
DictItem *
findVar_in_scoped_ht(Text name, Boole no_autoload) {
   if (currentCallS == NULL || currentCallS->fn->uf_scoped == NULL)
      return NULL;

   FnCall* old_currentCallS = currentCallS;
   // Search in parent scope which is possible to reference from lambda
   currentCallS = currentCallS->fn->uf_scoped;
   CS varname;
   DictItem* v = null;
   while (currentCallS) {
      EeSet* ht = findVarHashTable(name, OUT &varname);
      if (ht != NULL && *varname != ZERO) {
         v = findVar_in_ht(ht, name.c[0], text(varname), no_autoload);
         if (v)
            break;
      }
      if (currentCallS == currentCallS->fn->uf_scoped)
         break;
      currentCallS = currentCallS->fn->uf_scoped;
   }
   currentCallS = old_currentCallS;
   return v;
}

// Set "copyID + 1" in previous_funccal and callers.
int
set_ref_in_previous_funccal(int copyID) {
   for (FnCall* fc = previous_funccal; fc != NULL; fc = fc->fc_caller) {
      fc->copyId = copyID + 1;
      if (setRefInSet(&fc->localVars.hashTable, copyID + 1, NULL)
            || setRefInSet(&fc->argVars.hashTable, copyID + 1, NULL)
            || set_ref_in_list_items(&fc->arguments, copyID + 1, NULL))
         return true;
   }
   return false;
}

private int
set_ref_in_funccal(FnCall *fc, int copyID) {
   if (fc->copyId != copyID) {
      fc->copyId = copyID;
      if (setRefInSet(&fc->localVars.hashTable, copyID, NULL)
            || setRefInSet(&fc->argVars.hashTable, copyID, NULL)
            || set_ref_in_list_items(&fc->arguments, copyID, NULL)
            || set_ref_in_func(NULL, fc->fn, copyID))
         return true;
   }
   return false;
}

// Set "copyID" in all local vars and arguments in the call stack.
int
set_ref_in_call_stack(int copyID) {
   for (FnCall* fc = currentCallS; fc != NULL; fc = fc->fc_caller) {
      if (set_ref_in_funccal(fc, copyID))
         return true;
   } 

   // Also go through the funccal_stack.
   for (FnCallEntry* entry = funccal_stack; entry; entry = entry->next) {
      for (FnCall* fc = entry->top_funccal; fc != NULL; fc = fc->fc_caller) {
         if (set_ref_in_funccal(fc, copyID))
            return true;
      } 
   } 
   return false;
}

// Set "copyID" in all functions available by name.
int
set_ref_in_functions(int copyID) {
   int todo = (int)userDefinedFnsS.count;
   for (EeSetItem* hi = userDefinedFnsS.array; todo > 0 && !gotInterruptG; ++hi) {
      if (!HASHITEM_EMPTY(hi)) {
         --todo;
         UserFunc* fp = HI2UF(hi);
         if (!func_name_refcount(fp->uf_name) && set_ref_in_func(NULL, fp, copyID))
            return true;
      }
   }
   return false;
}

// Set "copyID" in all function arguments.
int
set_ref_in_func_args(int copyID) {
   for (int i = 0; i < funcargs.len; ++i) {
      if (set_ref_in_item(((Var **)funcargs.c)[i], copyID, NULL, NULL))
         return true;
   } 
   return false;
}

//Mark all lists and dicts referenced through function "name" with "copyID".
//Return true if setting references failed somehow.
int
set_ref_in_func(CS name, UserFunc* fp_in, int copyID) {
   if (!name && !fp_in) {
      return false;
   } 
   
   UserFunc* fp = fp_in;
   Byte   fnameBuilder[FLEN_FIXED + 1];
   Byte   *tofree = NULL;
   int      abort = false;

   if (!fp_in) {
      FnError error = FCERR_NONE;
      CS fname = fname_trans_sid(name, fnameBuilder, &tofree, &error);
      fp = find_func(fname, false);
   }
   if (fp) {
      for (FnCall* fc = fp->uf_scoped; fc; fc = fc->fn->uf_scoped)
         abort = abort || set_ref_in_funccal(fc, copyID);
   }

   eeglFree(tofree);
   return abort;
}

//":function". "lines_to_free" is a list of strings to be freed later.
//Return a pointer to the function or NULL if no function defined.
UserFunc *
define_function(Invocation* invo, ArrayList* lines_to_free) {
   int j;
   Unt namelen = 0;
   Boole is_global = false;
   CS p;
   CS arg;
   CS line_arg = NULL;
   ArrayList newargs;
   ArrayList argtypes;
   ArrayList arg_objm;
   ArrayList default_args;
   ArrayList newlines;
   int varargs = false;
   int flags = 0;
   Byte   *ret_type = NULL;
   int fp_allocated = false;
   int free_fp = false;
   int overwrite = false;
   DictItem   *v;
   FuncDict   fudi;
   static int   func_nr = 0;       // number for nameless function
   EeSetItem   *hi;

   // ":function" without argument: list functions.
   if (isComment(invo->comm)) {
      if (!invo->skip)
         list_functions(NULL);
      return NULL;
   }

   //":function /pat": list functions matching pattern.
   if (*invo->arg == '/') {
      p = list_functions_matching_pat(invo);
      return NULL;
   }

   ga_init(&newargs);
   ga_init(&argtypes);
   ga_init(&arg_objm);
   ga_init(&default_args);

   //Get the function name.  There are these situations:
   //func       normal function name, also when "class_flags" is non-zero
   //         "name" == func, "fudi.bag" == NULL
   //dict.func    new dictionary entry
   //         "name" == NULL, "fudi.bag" set,
   //         "fudi.item" == NULL, "fudi.newKey" == func
   //dict.func    existing dict entry with a Funcref
   //         "name" == func, "fudi.bag" set,
   //         "fudi.item" set, "fudi.newKey" == NULL
   //dict.func    existing dict entry that's not a Funcref
   //         "name" == NULL, "fudi.bag" set,
   //         "fudi.item" set, "fudi.newKey" == NULL
   //s:func       script-local function name
   //g:func       global function name, same as "func"
   p = invo->arg;

   int tfn_flags = TFN_NO_AUTOLOAD | TFN_NEW_FUNC;
   CS name = save_function_name(OUT &p, OUT &is_global, invo->skip, tfn_flags, OUT &fudi);
   int paren = (firstOccurrence(p, '(') != NULL);
   if (!name && (!fudi.bag || !paren) && !invo->skip) {
      //Return on an invalid expression in braces, unless the expression evaluation has been 
      //cancelled due to an aborting error, an interrupt, or an exception.
      if (!aborting()) {
         if (!invo->skip && fudi.newKey)
            showErrFmtMsg(_(e_key_not_present_in_dictionary_str), fudi.newKey);
         eeglFree(fudi.newKey);
         return NULL;
      } else
         invo->skip = true;
   }

   // An error in a function call during evaluation of an expression in magic
   // braces should not cause the function not to be defined.
   int saved_did_emsg = anyEmsgG;
   anyEmsgG = false;

   //":function func" with only function name: list function.
   UserFunc* fp = NULL;
   if (!paren) {
      fp = listOneFunction(invo, name, p, is_global);
      goto ret_free;
   }

   // ":function name(arg1, arg2)" Define function.
   p = skipwhite(p);
   if (*p != '(') {
      if (!invo->skip) {
         showErrFmtMsg(_(e_missing_paren_str), invo->arg);
         goto ret_free;
      }
      // attempt to continue by skipping some text
      if (firstOccurrence(p, '(') != NULL)
         p = firstOccurrence(p, '(');
   }


   ga_init2(&newlines, sizeof(CS), 10);

   if (!invo->skip) {
      //Check the name of the function. Unless it's a dictionary function (which we are overwriting)
      if (name)
         arg = name;
      else
         arg = fudi.newKey;
      if (arg && (!fudi.item
                    || (fudi.item->c.tag != VAR_FUNC && fudi.item->c.tag != VAR_PARTIAL)
                 )
      ) {
          Byte  *name_base = arg;
          int       i;

          if (*arg == K_SPECIAL) {
         name_base = firstOccurrence(arg, '_');
         if (name_base == NULL)
             name_base = arg + 3;
         else
             ++name_base;
         }
         for (i = 0; 
              name_base[i] != ZERO && (i == 0
                  ? isValidForScriptName1(name_base[i])
                  : isValidForScriptName(name_base[i])); 
              ++i
         )
            {}
         if (name_base[i] != ZERO) {
            emsg_funcname(e_invalid_argument_str, arg);
            goto ret_free;
         }
      }
      // Disallow using the g: dict.
      if (fudi.bag && fudi.bag->scope == VAR_DEF_SCOPE) {
          emsg(_(e_cannot_use_g_here));
          goto ret_free;
      }
   }

   // This may get more lines and make the pointers into the first line invalid.
   ++p;
   if (get_function_args(
            &p, ')', &newargs, &varargs, &default_args, invo->skip, invo, lines_to_free
       ) == FAIL
   )
      goto errret_2;
   
   // find extra arguments "range", "dict", "abort" and "closure"
   for (;;) {
      p = skipwhite(p);
      if (STRNCMP(p, "range", 5) == 0) {
         flags |= FC_RANGE;
         p += 5;
      } else if (STRNCMP(p, "dict", 4) == 0) {
         flags |= FC_DICT;
         p += 4;
      } else if (STRNCMP(p, "abort", 5) == 0) {
         flags |= FC_ABORT;
         p += 5;
      } else if (STRNCMP(p, "closure", 7) == 0) {
         flags |= FC_CLOSURE;
         p += 7;
         if (!currentCallS) {
            emsg_funcname(
                e_closure_function_should_not_be_at_top_level_str, name ? name : E
            );
            goto erret;
         }
      } else
         break;
   }

   //When there is a line break use what follows for the function body.
   //Makes 'exe "func Test()\n...\nendfunc"' work.
   if (*p == '\n')
      line_arg = p + 1;

   //Read the body of the function, until "}", ":endfunction" or ":enddef" is found.
   if (keyWasTypedG) {
      //Check if the function already exists, don't let the user type the
      //whole function before telling him it doesn't work!  For a script we
      //need to skip the body to be able to find what follows.
      if (!invo->skip && !invo->forceit) {
         if (fudi.bag && fudi.newKey == NULL)
            emsg(_(e_dictionary_entry_already_exists));
         ei (name && find_func(name, is_global) != NULL)
            emsg_funcname(e_function_str_already_exists_add_bang_to_replace, name);
      }

      if (!invo->skip && anyEmsgG)
          goto erret;

      msg_putchar('\n');       // don't overwrite the function name
      commlineRowG = msgRowG;
   }

   //Save the starting line number.
   LineNr sourcing_lnum_top = SOURCING_LNUM;

   //Do not define the function when getting the body fails and when skipping.
   if ((get_function_body(invo, &newlines, line_arg, lines_to_free) == FAIL) || invo->skip)
      goto erret;

   //If there are no errors, add the function
   if (fudi.bag) {
      Byte numbuf[NUMBUFLEN];

      fp = NULL;
      if (fudi.newKey == NULL && !invo->forceit) {
         emsg(_(e_dictionary_entry_already_exists));
         goto erret;
      }
      if (fudi.item == NULL) {
         // Can't add a function to a locked dictionary
         if (value_check_lock(fudi.bag->lock, mbText(invo->arg), false))
            goto erret;
      }
      // Can't change an existing function if it is locked
      else if (value_check_lock(fudi.item->c.lock, mbText(invo->arg), false))
         goto erret;

      // Give the function a sequential number. Can only be used with a Funcref!
      eeglFree(name);
      namelen = eeSnprintf(numbuf, sizeof(numbuf), "%d", ++func_nr);
      name = copySubstr(numbuf, namelen);
      if (!name)
         goto erret;
   } else {
      CS find_name = name;
      int var_conflict = false;
      int ffed_flags = is_global ? FFED_IS_GLOBAL : 0;

      v = findVar(name, true);
      if (v && v->c.tag == VAR_FUNC)
         var_conflict = true;

      if (var_conflict) {
         emsg_funcname(e_function_name_conflicts_with_variable_str, name);
         goto erret;
      }

      fp = find_func_even_dead(find_name, ffed_flags);

      if (fp) {
         int dead = fp && (fp->uf_flags & FC_DEAD);

         // Function can be replaced with "function!" and when sourcing the
         // same script again, but only once.
         // A name that is used by an import can not be overruled.
         if ((!dead && !invo->forceit
            && (fp->scriptCtx.sid != scriptPosG.sid
              || fp->scriptCtx.seq == scriptPosG.seq))
         ) {
            SOURCING_LNUM = sourcing_lnum_top;
            emsg_funcname(e_function_str_already_exists_add_bang_to_replace, name);
            goto errret_keep;
         }
         if (fp->uf_calls > 0) {
            emsg_funcname( e_cannot_redefine_function_str_it_is_in_use, name);
            goto errret_keep;
         }
         if (fp->refcount > 1) {
            // This function is referenced somewhere, don't redefine it but
            // create a new one.
            --fp->refcount;
            fp->uf_flags |= FC_REMOVED;
            fp = NULL;
            overwrite = true;
         } else {
            CS exp_name = fp->uf_name_exp;

            // redefine existing function, keep the expanded name
            EE_CLEAR(name);
            namelen = 0;
            fp->uf_name_exp = NULL;
            func_clear_items(fp);
            fp->uf_name_exp = exp_name;
            fp->uf_flags &= ~FC_DEAD;
#ifdef FEAT_PROFILE
            fp->uf_profiling = false;
            fp->uf_prof_initialized = false;
#endif
         }
      }
   }

   if (!fp) {
      if (!fudi.bag && firstOccurrence(name, AUTOLOAD_CHAR) != NULL) {
         int slen, plen;
         Byte  *scriptname;

         // Check that the autoload name matches the script name.
         j = FAIL;
         if (SOURCING_NAME) {
            scriptname = autoload_name(name);
            if (scriptname) {
               p = firstOccurrence(scriptname, '/');
               plen = (int)STRLEN(p);
               slen = (int)STRLEN(SOURCING_NAME);
               if (slen > plen && fnamecmp(p, SOURCING_NAME + slen - plen) == 0)
                  j = OK;
               eeglFree(scriptname);
            }
         }
         if (j == FAIL) {
            LineNr save_lnum = SOURCING_LNUM;
            
            SOURCING_LNUM = sourcing_lnum_top;
            showErrFmtMsg(_(e_function_name_does_not_match_script_file_name_str), name);
            SOURCING_LNUM = save_lnum;
            goto erret;
         }
      }

      if (namelen == 0)
          namelen = STRLEN(name);
      fp = alloc_ufunc(name, namelen);
      if (!fp)
          goto erret;
      fp_allocated = true;

      if (fudi.bag) {
         if (!fudi.item) {
            // add new dict entry
            fudi.item = dictitem_alloc(mbText(fudi.newKey));
            if (fudi.item == NULL) {
               EE_CLEAR(fp);
               goto erret;
            }
            if (bagAdd(fudi.bag, fudi.item) == FAIL) {
               eeglFree(fudi.item);
               EE_CLEAR(fp);
               goto erret;
            }
         } else
            // overwrite existing dict entry
            clearVar(&fudi.item->c);
         fudi.item->c.tag = VAR_FUNC;
         fudi.item->c.string = copySubstr(name, namelen);

         // behave like "dict" was used
         flags |= FC_DICT;
      }
   }
   fp->args = newargs;
   fp->defaultArgs = default_args;

   if (fp_allocated) {
      // insert the new function in the function list
      if (overwrite) {
         hi = hash_find(&userDefinedFnsS, mbText(name));
         hi->hi_key = UF2HIKEY(fp);
      } else if (hash_add(&userDefinedFnsS, mbText(fp->uf_name), S"add function") == FAIL) {
         free_fp = true;
         goto erret;
      }
      fp->refcount = 1;
   }

   fp->lines = newlines;
   newlines.c = NULL;
   if ((flags & FC_CLOSURE) != 0) {
      if (register_closure(fp) == FAIL)
         goto erret;
   } else
      fp->uf_scoped = NULL;

#ifdef FEAT_PROFILE
   if (prof_def_func())
      func_do_profile(fp);
#endif
   fp->uf_varargs = varargs;
   fp->uf_flags = flags;
   fp->uf_calls = 0;
   fp->uf_cleared = false;
   fp->scriptCtx = scriptPosG;
   fp->scriptCtx.lineNr += sourcing_lnum_top;
   goto ret_free;

erret:
   if (fp) {
      // these were set to "newargs" and "default_args", which are cleared below
      ga_init(&fp->args);
      ga_init(&fp->defaultArgs);
   }
errret_2:
   if (fp) {
      EE_CLEAR(fp->uf_va_name);
      EE_CLEAR(fp->uf_name_exp);
   }
   if (free_fp)
      EE_CLEAR(fp);
errret_keep:
   ga_clear_strings(&newargs);
   ga_clear_strings(&default_args);
   ga_clear_strings(&newlines);
ret_free:
   ga_clear_strings(&argtypes);
   ga_clear(&arg_objm);
   eeglFree(fudi.newKey);
   eeglFree(name);
   eeglFree(ret_type);
   anyEmsgG |= saved_did_emsg;

   return fp;
}


void
c_function(Invocation* invo) {
   ArrayList linesToFree;
   ga_init2(OUT &linesToFree, sizeof(CS), 50);
   
   (void)define_function(invo, &linesToFree);
   ga_clear_strings(&linesToFree);
}

// Check if a funcref is assigned to a valid variable name.
// true and give an error if not.
int
var_wrong_func_name(
   Text name,    // points to start of variable name
   int    new_var)  // true when creating the variable
{
   // Allow for w: b: s: and t:. Allow autoload variable.
   if (!((firstOccurrence(S"wbt", name.c[0]) != NULL || (name.c[0] == 's')) 
            && name.c[1] == ':'
        )
          && !ASCII_ISUPPER((name.len > 0 && name.c[1] == ':') ? name.c[2] : name.c[0])
          && firstOccurrence(name.c, '#') == NULL
   ){
      showErrFmtMsg(_(e_funcref_variable_name_must_start_with_capital_str), name.c);
      return true;
   }
   // Don't allow hiding a function.  When "v" is not NULL we might be
   // assigning another function to the same var, the type is checked below.
   if (new_var && function_exists(name.c, false)) {
      showErrFmtMsg(_(e_variable_name_conflicts_with_existing_function_str), name.c);
      return true;
   }
   return false;
}

//}}}
//{{{autocommands (event handling hooks)

//The autocommands are stored in a list for each event.
//Autocommands for the same pattern, that are consecutive, are joined
//together, to avoid having to match the pattern too often.
//The result is an array of Autopat lists, which point to AutoComm lists:
//
//lastAutopatS[0]  -----------------------------+
//                                          V
//firstAutopatS[0] --> Autopat.next  -->  Autopat.next -->  NULL
//                     Autopat.comms      Autopat.comms
//                             |          |
//                             V          V
//                     AutoComm.next      AutoComm.next
//                             |          |
//                             V          V
//                     AutoComm.next      NULL
//                             |
//                             V
//                           NULL
//
//lastAutopatS[1]  --------+
//                         V
//firstAutopatS[1] --> Autopat.next  -->  NULL
//                     Autopat.comms
//                         |
//                         V
//                     AutoComm.next
//                         |
//                         V
//                        NULL
//  etc.
//
//The order of AutoComms is important, this is the order in which they were
//defined and will have to be executed.
declStruct(AutoComm);
struct AutoComm {
   CS comm;    // The command to be executed (NULL when command has been removed).
   Boole once;    // "One shot": removed after execution
   Boole nested;  // If autocommands nest here.
   Boole last;    // last command in list
   ScriptPos scriptCtx;      // script context where it is defined
   AutoComm* next;      // next AutoComm in list
};

declStruct(AutoPat);
struct AutoPat {
   AutoPat* next;      // Next AutoPat in AutoPat list; MUST be the first entry.
   CS pat;      // pattern as typed (NULL when pattern has been removed)
   RegProg* reg_prog;      // compiled regprog for pattern
   AutoComm* comms;      // list of commands to do
   Unt group;      // group ID
   int patlen;      // strlen() of pat
   int buflocal_nr;   // !=0 for buffer-local AutoPat
   Byte allow_dirs;      // Pattern may match whole path
   Byte last;      // last pattern for applyAutocomms()
};

//
// special cases:
// BufNewFile and BufRead are searched for ALOT (especially at startup)
// so we pre-determine their index into the autoEvents[] table for fast access.
// Keep these values in sync with autoEvents[]!
#define BUFNEWFILE_INDEX 9
#define BUFREAD_INDEX 10

// Must be sorted by the 'value' field because it is used by bsearch()!
// Must be synchronized with the enum of events in eegl.h:AutoEvent
// Events with positive keys aren't allowed in 'eventignorewin'.
private Kv autoEvents[NUM_EVENTS] = {
   KEYVALUE_ENTRY(-EVENT_BUFADD, "BufAdd"),
   KEYVALUE_ENTRY(-EVENT_BUFADD, "BufCreate"),
   KEYVALUE_ENTRY(-EVENT_BUFDELETE, "BufDelete"),
   KEYVALUE_ENTRY(-EVENT_BUFENTER, "BufEnter"),
   KEYVALUE_ENTRY(-EVENT_BUFFILEPOST, "BufFilePost"),
   KEYVALUE_ENTRY(-EVENT_BUFFILEPRE, "BufFilePre"),
   KEYVALUE_ENTRY(-EVENT_BUFHIDDEN, "BufHidden"),
   KEYVALUE_ENTRY(-EVENT_BUFLEAVE, "BufLeave"),
   KEYVALUE_ENTRY(-EVENT_BUFNEW, "BufNew"),
   KEYVALUE_ENTRY(-EVENT_BUFNEWFILE, "BufNewFile"),   // BUFNEWFILE_INDEX
   KEYVALUE_ENTRY(-EVENT_BUFREADPOST, "BufRead"),   // BUFREAD_INDEX
   KEYVALUE_ENTRY(-EVENT_BUFREADCMD, "BufReadCmd"),
   KEYVALUE_ENTRY(-EVENT_BUFREADPOST, "BufReadPost"),
   KEYVALUE_ENTRY(-EVENT_BUFREADPRE, "BufReadPre"),
   KEYVALUE_ENTRY(-EVENT_BUFUNLOAD, "BufUnload"),
   KEYVALUE_ENTRY(-EVENT_BUFWINENTER, "BufWinEnter"),
   KEYVALUE_ENTRY(-EVENT_BUFWINLEAVE, "BufWinLeave"),
   KEYVALUE_ENTRY(-EVENT_BUFWIPEOUT, "BufWipeout"),
   KEYVALUE_ENTRY(-EVENT_BUFWRITEPRE, "BufWrite"),
   KEYVALUE_ENTRY(-EVENT_BUFWRITECMD, "BufWriteCmd"),
   KEYVALUE_ENTRY(-EVENT_BUFWRITEPOST, "BufWritePost"),
   KEYVALUE_ENTRY(-EVENT_BUFWRITEPRE, "BufWritePre"),
   KEYVALUE_ENTRY(EVENT_CMDUNDEFINED, "CmdUndefined"),
   KEYVALUE_ENTRY(EVENT_COMMPORTENTER, "CommPortEnter"),
   KEYVALUE_ENTRY(EVENT_COMMPORTLEAVE, "CommPortLeave"),
   KEYVALUE_ENTRY(EVENT_COMPLETECHANGED, "CompleteChanged"),
   KEYVALUE_ENTRY(EVENT_COMPLETEDONE, "CompleteDone"),
   KEYVALUE_ENTRY(EVENT_COMPLETEDONEPRE, "CompleteDonePre"),
   KEYVALUE_ENTRY(-EVENT_CURSORHOLD, "CursorHold"),
   KEYVALUE_ENTRY(-EVENT_CURSORHOLDI, "CursorHoldI"),
   KEYVALUE_ENTRY(EVENT_DIFFUPDATED, "DiffUpdated"),
   KEYVALUE_ENTRY(EVENT_DIRCHANGED, "DirChanged"),
   KEYVALUE_ENTRY(EVENT_DIRCHANGEDPRE, "DirChangedPre"),
   KEYVALUE_ENTRY(EVENT_EXITPRE, "ExitPre"),
   KEYVALUE_ENTRY(-EVENT_FILEAPPENDCMD, "FileAppendCmd"),
   KEYVALUE_ENTRY(-EVENT_FILEAPPENDPOST, "FileAppendPost"),
   KEYVALUE_ENTRY(-EVENT_FILEAPPENDPRE, "FileAppendPre"),
   KEYVALUE_ENTRY(-EVENT_FILECHANGEDRO, "FileChangedRO"),
   KEYVALUE_ENTRY(-EVENT_FILECHANGEDSHELL, "FileChangedShell"),
   KEYVALUE_ENTRY(-EVENT_FILECHANGEDSHELLPOST, "FileChangedShePosNoVirtLent"),
   KEYVALUE_ENTRY(-EVENT_FILEREADCMD, "FileReadCmd"),
   KEYVALUE_ENTRY(-EVENT_FILEREADPOST, "FileReadPost"),
   KEYVALUE_ENTRY(-EVENT_FILEREADPRE, "FileReadPre"),
   KEYVALUE_ENTRY(-EVENT_FILETYPE, "FileType"),
   KEYVALUE_ENTRY(-EVENT_FILEWRITECMD, "FileWriteCmd"),
   KEYVALUE_ENTRY(-EVENT_FILEWRITEPOST, "FileWritePost"),
   KEYVALUE_ENTRY(-EVENT_FILEWRITEPRE, "FileWritePre"),
   KEYVALUE_ENTRY(-EVENT_FILTERREADPOST, "FilterReadPost"),
   KEYVALUE_ENTRY(-EVENT_FILTERREADPRE, "FilterReadPre"),
   KEYVALUE_ENTRY(-EVENT_FILTERWRITEPOST, "FilterWritePost"),
   KEYVALUE_ENTRY(-EVENT_FILTERWRITEPRE, "FilterWritePre"),
   KEYVALUE_ENTRY(EVENT_FOCUSGAINED, "FocusGained"),
   KEYVALUE_ENTRY(EVENT_FOCUSLOST, "FocusLost"),
   KEYVALUE_ENTRY(EVENT_FUNCUNDEFINED, "FuncUndefined"),
   KEYVALUE_ENTRY(-EVENT_INSERTENTER, "InsertEnter"),
   KEYVALUE_ENTRY(-EVENT_INSERTLEAVE, "InsertLeave"),
   KEYVALUE_ENTRY(-EVENT_INSERTLEAVEPRE, "InsertLeavePre"),
   KEYVALUE_ENTRY(EVENT_MENUPOPUP, "MenuPopup"),
   KEYVALUE_ENTRY(EVENT_MODECHANGED, "ModeChanged"),
   KEYVALUE_ENTRY(EVENT_OPTIONSET, "OptionSet"),
   KEYVALUE_ENTRY(EVENT_QUICKFIXCMDPOST, "QuickFixCmdPost"),
   KEYVALUE_ENTRY(EVENT_QUICKFIXCMDPRE, "QuickFixCmdPre"),
   KEYVALUE_ENTRY(EVENT_QUITPRE, "QuitPre"),
   KEYVALUE_ENTRY(EVENT_REMOTEREPLY, "RemoteReply"),
   KEYVALUE_ENTRY(EVENT_SAFESTATE, "SafeState"),
   KEYVALUE_ENTRY(EVENT_SAFESTATEAGAIN, "SafeStateAgain"),
   KEYVALUE_ENTRY(EVENT_SESSIONLOADPOST, "SessionLoadPost"),
   KEYVALUE_ENTRY(EVENT_SESSIONWRITEPOST, "SessionWritePost"),
   KEYVALUE_ENTRY(EVENT_SHELLCMDPOST, "ShellCmdPost"),
   KEYVALUE_ENTRY(-EVENT_SHELLFILTERPOST, "ShellFilterPost"),
   KEYVALUE_ENTRY(EVENT_SIGUSR1, "SigUSR1"),
   KEYVALUE_ENTRY(EVENT_SOURCECMD, "SourceCmd"),
   KEYVALUE_ENTRY(EVENT_SOURCEPOST, "SourcePost"),
   KEYVALUE_ENTRY(EVENT_SOURCEPRE, "SourcePre"),
   KEYVALUE_ENTRY(EVENT_SPELLFILEMISSING, "SpellFileMissing"),
   KEYVALUE_ENTRY(EVENT_SWAPEXISTS, "SwapExists"),
   KEYVALUE_ENTRY(EVENT_SYNTAX, "Syntax"),
   KEYVALUE_ENTRY(EVENT_TABCLOSED, "TabClosed"),
   KEYVALUE_ENTRY(EVENT_TABCLOSEDPRE, "TabClosedPre"),
   KEYVALUE_ENTRY(EVENT_TABENTER, "TabEnter"),
   KEYVALUE_ENTRY(EVENT_TABLEAVE, "TabLeave"),
   KEYVALUE_ENTRY(EVENT_TABNEW, "TabNew"),
   KEYVALUE_ENTRY(EVENT_TERMCHANGED, "TermChanged"),
   KEYVALUE_ENTRY(EVENT_TERMINALOPEN, "TerminalOpen"),
   KEYVALUE_ENTRY(EVENT_TERMINALWINOPEN, "TerminalWinOpen"),
   KEYVALUE_ENTRY(EVENT_TERMRESPONSE, "TermResponse"),
   KEYVALUE_ENTRY(EVENT_TERMRESPONSEALL, "TermResponseAll"),
   KEYVALUE_ENTRY(-EVENT_TEXTYANKPOST, "TextYankPost"),
   KEYVALUE_ENTRY(EVENT_USER, "User"),
   KEYVALUE_ENTRY(EVENT_EEGLENTER, "EeglEnter"),
   KEYVALUE_ENTRY(EVENT_EEGLLEAVE, "EeglLeave"),
   KEYVALUE_ENTRY(EVENT_EEGLLEAVEPRE, "EeglLeavePre"),
   KEYVALUE_ENTRY(EVENT_EEGLRESIZED, "EeglResized"),
   KEYVALUE_ENTRY(-EVENT_WINCLOSED, "WinClosed"),
   KEYVALUE_ENTRY(-EVENT_PORTENTER, "WinEnter"),
   KEYVALUE_ENTRY(-EVENT_PORTLEAVE, "WinLeave"),
   KEYVALUE_ENTRY(EVENT_PORTNEW, "WinNew"),
   KEYVALUE_ENTRY(EVENT_PORTNEWPRE, "WinNewPre"),
   KEYVALUE_ENTRY(-EVENT_WINRESIZED, "WinResized"),
   KEYVALUE_ENTRY(-EVENT_PORTSCROLLED, "WinScrolled"),
};

private AutoPat *firstAutopatS[NUM_EVENTS] = { NULL };
private AutoPat *lastAutopatS[NUM_EVENTS] = { NULL };

//struct used to keep status while executing autocommands for an event.
struct AutoPatComm {
   AutoPat* curpat;   // next AutoPat to examine
   AutoComm* nextComm;   // next AutoComm to execute
   Unt group;      // group being used
   CS fname;      // fname to match with
   CS sfname;   // sfname to match with
   CS tail;      // tail of fname
   AutoEvent event;      // current event
   ScriptPos scriptCtx;   // script context where it is defined
   int arg_bufnr;   // Initially equal to <abuf>, set to zero when buf is deleted
   AutoPatComm *next;      // chain of active apc-s for auto-invalidation
};

private AutoPatComm *active_apc_list = NULL; // stack of active autocommands

// Macro to loop over all the patterns for an autocmd event
#define FOR_ALL_AUTOCMD_PATTERNS(event, ap) \
    for ((ap) = firstAutopatS[(int)(event)]; (ap) != NULL; (ap) = (ap)->next)

//augroups stores a list of autocmd group names.
private ArrayList augroups = {0, 0, sizeof(CS), 10, NULL};
#define AUGROUP_NAME(i) (((Byte **)augroups.c)[i])
// use get_deleted_augroup() to get this
private Byte *deleted_augroup = NULL;

//The ID of the current group.  Group 0 is the default one.
private Unt currAugroupS = AUGROUP_DEFAULT;

private int au_need_clean = false;   // need to delete marked patterns

private AutoEvent event_name2nr(Byte *start, Byte **end);
private CS event_nr2name(AutoEvent event);
private int au_get_grouparg(Byte **argp);
private int applyAutocommGroup(AutoEvent event, CS fname, CS fname_io, Boole force, 
   Unt group, Book* book, Invocation* invo);
private void auto_next_pat(AutoPatComm *apc, int stop_at_last);
private Unt findGroup(Byte *name);

private AutoEvent   last_event;
private Unt   last_group;
private Boole   autocommsBlockedS = 0;   // block all autocmds

private CS
get_deleted_augroup(void) {
   if (deleted_augroup == NULL)
      deleted_augroup = (CS)_("--Deleted--");
   return deleted_augroup;
}

//Show the autocommands for one AutoPat.
private void
show_autocmd(AutoPat* ap, AutoEvent event) {
   AutoComm *ac;

   // Check for "gotInterruptG" (here and at various places below), which is set
   // when "q" has been hit for the "--more--" prompt
   if (gotInterruptG)
      return;
   if (ap->pat == NULL)      // pattern has been removed
      return;

   // Make sure no info referenced by "ap" is cleared, e.g. when a timer
   // clears an augroup.  Jump to "theend" after this!
   // "ap->pat" may be cleared anyway.
   ++autocmd_busy;

   msg_putchar('\n');
   if (gotInterruptG)
   goto theend;
   if (event != last_event || ap->group != last_group) {
      if (ap->group != AUGROUP_DEFAULT) {
         if (AUGROUP_NAME(ap->group) == NULL)
            msgPutsDeco(get_deleted_augroup(), getDecoFlags(HLF_E));
         else
            msgPutsDeco(AUGROUP_NAME(ap->group), getDecoFlags(HLF_T));
         msg_puts(S"  ");
      }
      msgPutsDeco(event_nr2name(event), getDecoFlags(HLF_T));
      last_event = event;
      last_group = ap->group;
      msg_putchar('\n');
      if (gotInterruptG)
         goto theend;
   }

   if (!ap->pat)
      goto theend;  // timer might have cleared the pattern or group

   msgColG = 4;
   msg_outtrans(ap->pat);

   for (ac = ap->comms; ac != NULL; ac = ac->next) {
      if (ac->comm == NULL)      // skip removed commands
         continue;

      if (msgColG >= 14)
         msg_putchar('\n');
      msgColG = 14;
      if (gotInterruptG)
         goto theend;
      msg_outtrans(ac->comm);
      if (p_verbose > 0)
         lastSetMsg(ac->scriptCtx);
      if (gotInterruptG)
         goto theend;
      if (ac->next != NULL) {
         msg_putchar('\n');
         if (gotInterruptG)
            goto theend;
      }
   }

theend:
   --autocmd_busy;
}

// Mark an autocommand pattern for deletion.
private void
au_remove_pat(AutoPat* ap) {
   EE_CLEAR(ap->pat);
   ap->buflocal_nr = -1;
   au_need_clean = true;
}

// Mark all commands for a pattern for deletion.
private void
au_remove_cmds(AutoPat *ap) {
   for (AutoComm* ac = ap->comms; ac != NULL; ac = ac->next) {
      EE_CLEAR(ac->comm);
   } 
   au_need_clean = true;
}

//Delete one command from an autocmd pattern.
private void 
au_del_cmd(AutoComm* ac) {
   EE_CLEAR(ac->comm);
   au_need_clean = true;
}

//Cleanup autocommands and patterns that have been deleted.
//This is only done when not executing autocommands.
private void
au_cleanup(void) {
   AutoPat   *ap, **prev_ap;
   AutoComm   *ac, **prev_ac;
   AutoEvent   event;

   if (autocmd_busy || !au_need_clean)
      return;

   // loop over all events
   for (event = (AutoEvent)0; (int)event < NUM_EVENTS; event = (AutoEvent)((int)event + 1)) {
      // loop over all autocommand patterns
      prev_ap = &(firstAutopatS[(int)event]);
      for (ap = *prev_ap; ap != NULL; ap = *prev_ap) {
         int has_cmd = false;

         // loop over all commands for this pattern
         prev_ac = &(ap->comms);
         for (ac = *prev_ac; ac != NULL; ac = *prev_ac) {
            // remove the command if the pattern is to be deleted or when
            // the command has been marked for deletion
            if (ap->pat == NULL || ac->comm == NULL) {
               *prev_ac = ac->next;
               eeglFree(ac->comm);
               eeglFree(ac);
            } else {
               has_cmd = true;
               prev_ac = &(ac->next);
            }
         }

         if (ap->pat && !has_cmd)
            // Pattern was not marked for deletion, but all of its
            // commands were.  So mark the pattern for deletion.
            au_remove_pat(ap);

         // remove the pattern if it has been marked for deletion
         if (!ap->pat) {
            if (!ap->next) {
               if (prev_ap == &(firstAutopatS[(int)event]))
                  lastAutopatS[(int)event] = NULL;
               else
                  // this depends on the "next" field being the first in the struct
                  lastAutopatS[(int)event] = (AutoPat *)prev_ap;
            }
            *prev_ap = ap->next;
            eeRegFree(ap->reg_prog);
            eeglFree(ap);
          } else
            prev_ap = &(ap->next);
      }
   }

   au_need_clean = false;
}

//Called when a book is freed, to remove/invalidate related book-local autocommands.
void
scrRemoveAutocommsFromBook(Book* book) {
   AutoPat       *ap;
   AutoEvent       event;

   // invalidate currently executing autocommands
   for (AutoPatComm* apc = active_apc_list; apc; apc = apc->next) {
      if (book->fiNum == apc->arg_bufnr)
         apc->arg_bufnr = 0;
   } 

   // invalidate buflocals looping through events
   for (event = (AutoEvent)0; (int)event < NUM_EVENTS; event = (AutoEvent)((int)event + 1)) {
      // loop over all autocommand patterns
      FOR_ALL_AUTOCMD_PATTERNS(event, ap) {
         if (ap->buflocal_nr == book->fiNum) {
            au_remove_pat(ap);
            if (p_verbose >= 6) {
               verbose_enter();
               smsg(
                  _("auto-removing autocommand: %s <buffer=%d>"), event_nr2name(event), book->fiNum
               );
               verbose_leave();
            }
         }
      } 
   } 
   au_cleanup();
}

//Add an autocmd group name. Return its ID. AUGROUP_ERROR (< 0) for error.
private Unt
au_new_group(CS name) {
   Unt i = findGroup(name);
   if (i != AUGROUP_ERROR)
      return i;

   // the group doesn't exist yet, add it.  First try using a free entry.
   for (i = 0; i < (Unt)augroups.len; ++i) {
      if (AUGROUP_NAME(i) == NULL)
          break;
   } 
   if (i == (Unt)augroups.len && ga_grow(&augroups, 1) == FAIL)
      return AUGROUP_ERROR;

   AUGROUP_NAME(i) = copyStr(name);
   if (i == (Unt)augroups.len)
      ++augroups.len;

   return i;
}

private void
au_del_group(CS name) {
   AutoEvent   event;
   AutoPat   *ap;
   int      in_use = false;

   Unt i = findGroup(name);
   if (i == AUGROUP_ERROR){    // the group doesn't exist
      return;
   }
   if (i == currAugroupS) {
      emsg(_(e_cannot_delete_current_group));
      return;
   }

   for (event = (AutoEvent)0; (int)event < NUM_EVENTS; event = (AutoEvent)((int)event + 1)) {
      FOR_ALL_AUTOCMD_PATTERNS(event, ap) {
          if (ap->group == i && ap->pat != NULL) {
            give_warning((CS)_("W19: Deleting augroup that is still in use"), true);
            in_use = true;
            event = NUM_EVENTS;
            break;
         }
      } 
   }
   eeglFree(AUGROUP_NAME(i));
   if (in_use)
      AUGROUP_NAME(i) = get_deleted_augroup();
   else
      AUGROUP_NAME(i) = NULL;
}

// Find the ID of an autocomm group name. Return its ID.  Returns AUGROUP_ERROR (< 0) when not found
private Unt
findGroup(CS name) {
   for (Unt i = 0; i < (Unt)augroups.len; ++i) {
      if (AUGROUP_NAME(i) != NULL && AUGROUP_NAME(i) != get_deleted_augroup()
            && STRCMP(AUGROUP_NAME(i), name) == 0)
         return i;
   } 
   return AUGROUP_ERROR;
}

//Return true if augroup "name" exists.
Boole
auGroupExists(CS name) {
   return findGroup(name) != AUGROUP_ERROR;
}

// ":augroup {name}".
void
do_augroup(CS arg, Boole del_group) {
   if (del_group) {
      if (*arg == ZERO)
         emsg(_(e_argument_required));
      else
         au_del_group(arg);
   } ei (caseInsensitiveCompare(arg, "end") == 0)   // ":aug end": back to group 0
      currAugroupS = AUGROUP_DEFAULT;
   ei (*arg) {         // ":aug xxx": switch to group xxx
      Unt i = au_new_group(arg);
      if (i != AUGROUP_ERROR)
         currAugroupS = i;
   } else {            // ":aug": list the group names
      msg_start();
      for (Unt i = 0; i < (Unt)augroups.len; ++i) {
         if (AUGROUP_NAME(i) != NULL) {
            msg_puts(AUGROUP_NAME(i));
            msg_puts(S"  ");
         }
      }
      msg_clr_eos();
      msg_end();
   }
}

void
autocmd_init(void){
   CLEAR_FIELD(autoCommPortG);
}

#if defined(EXITFREE) || defined(PROTO)
void
free_all_autocmds(void){
   for (currAugroupS = -1; currAugroupS < augroups.len; ++currAugroupS)
      do_autocmd(S"", true);

   for (int i = 0; i < augroups.len; ++i) {
      CS s = ((Byte **)(augroups.c))[i];
      if (s != get_deleted_augroup())
         eeglFree(s);
   }
   ga_clear(&augroups);

   // autoCommPortG[] is freed in portFreeAll()
}
#endif

// Return true if "port" is an active entry in autoCommPortG[].
int
is_autoCommPort(Portal *port) {
   for (int i = 0; i < AUCMD_PORTAL_COUNT; ++i) {
      if (autoCommPortG[i].isPortUsed && autoCommPortG[i].port == port)
         return true;
   } 
   return false;
}

//Return the event number for event name "start".
//Return NUM_EVENTS if the event name was not found.
//Return a pointer to the next event name in "end".
private AutoEvent
event_name2nr(CS start, OUT CS* end) {
   CS p;
   static Kv *bufnewfile = &autoEvents[BUFNEWFILE_INDEX];
   static Kv *bufread = &autoEvents[BUFREAD_INDEX];

   // the event name ends with end of line, '|', a blank or a comma
   for (p = start; *p && !SPACE_OR_TAB(*p) && *p != ',' && *p != '|'; ++p)
      {}

   Kv target;
   target.key = 0;
   target.value.c = start;
   target.value.len = (Unt)(p - start);

   //special cases:
   //BufNewFile and BufRead are searched for ALOT (especially at startup) so we check for them first
   Kv *entry;
   if (cmp_keyvalue_value_ni(&target, bufnewfile) == 0)
      entry = bufnewfile;
   ei (cmp_keyvalue_value_ni(&target, bufread) == 0)
      entry = bufread;
   else
      entry = (Kv *)bsearch(
            &target, &autoEvents, NUM_EVENTS, sizeof(autoEvents[0]), cmp_keyvalue_value_ni
      );

   if (*p == ',')
      ++p;
   *end = p;

   return (entry == NULL) ? NUM_EVENTS : (AutoEvent)abs(entry->key);
}

// Return the name for event "event".
private CS
event_nr2name(AutoEvent event) {
   int i;
#define CACHE_SIZE 12
   static int cache_tab[CACHE_SIZE];
   static int cache_last_index = -1;

   if (cache_last_index < 0) {
      for (i = 0; i < CACHE_SIZE; ++i)
          cache_tab[i] = -1;
      cache_last_index = CACHE_SIZE - 1;
   }

   // first look in the cache
   // the cache is circular. to search it we start at the most recent entry
   // and go backwards wrapping around when we get to index 0.
    for (i = cache_last_index; cache_tab[i] >= 0; ) {
      if ((AutoEvent)abs(autoEvents[cache_tab[i]].key) == event)
         return autoEvents[cache_tab[i]].value.c;

      if (i == 0)
         i = CACHE_SIZE - 1;
      else
         --i;

      // are we back at the start?
      if (i == cache_last_index)
         break;
   }

   // look in the event table itself
   for (i = 0; i < NUM_EVENTS; ++i) {
      if ((AutoEvent)abs(autoEvents[i].key) == event) {
         // store the found entry in the next position in the cache,
         // wrapping around when we get to the maximum index.
         if (cache_last_index == CACHE_SIZE - 1)
            cache_last_index = 0;
         else
            ++cache_last_index;
         cache_tab[cache_last_index] = i;
         break;
      }
   }

   return (i == NUM_EVENTS) ? (CS)"Unknown" : autoEvents[i].value.c;
}

// Scan over the events.  "*" stands for all events.
private CS
find_end_event(CS arg, Boole have_group) {      // true when group name was found
   CS pat;
   CS p;

   if (*arg == '*') {
      if (arg[1] && !SPACE_OR_TAB(arg[1])) {
         showErrFmtMsg(_(e_illegal_character_after_star_str), arg);
         return NULL;
      }
      pat = arg + 1;
   } else {
      for (pat = arg; *pat && *pat != '|' && !SPACE_OR_TAB(*pat); pat = p) {
         if ((int)event_name2nr(pat, &p) >= NUM_EVENTS) {
            if (have_group)
                showErrFmtMsg(_(e_no_such_event_str), pat);
            else
                showErrFmtMsg(_(e_no_such_group_or_event_str), pat);
            return NULL;
         }
      }
   }
   return pat;
}

// Return true if "event" is included in 'eventignore(win)'.
Boole
event_ignored(AutoEvent event, NULLABLE CS evIgn) {
   if (!evIgn)
      return false;
      
   int ignored = false;
   
   while (*evIgn != ZERO) {
      int unignore = *evIgn == '-';
      evIgn += unignore;
      if (STRNICMP(evIgn, "all", 3) == 0 && (evIgn[3] == ZERO || evIgn[3] == ',')) {
         ignored = evIgn == p_ei || (autoEvents[event].key <= 0);
         evIgn += 3 + (evIgn[3] == ',');
      } ei (event_name2nr(evIgn, &evIgn) == event) {
         if (unignore)
            return false;
         else
            ignored = true;
      }
   }

   return ignored;
}

// Return OK when the contents of 'eventignore' or 'eventignorewin' is valid, FAIL otherwise
int
check_ei(CS evIgn) {
   int   win = evIgn != p_ei;

   while (*evIgn) {
      if (STRNICMP(evIgn, "all", 3) == 0 && (evIgn[3] == ZERO || evIgn[3] == ','))
         evIgn += 3 + (evIgn[3] == ',');
      else {
         evIgn += (*evIgn == '-');
         AutoEvent event = event_name2nr(evIgn, &evIgn);
         if (event == NUM_EVENTS || (win && autoEvents[event].key > 0))
            return FAIL;
      }
   }

   return OK;
}


//Add "what" to @eventignore to skip loading syntax hiliting for every book shown in 
//the portal. "what" must start with a comma. Return the old value of 'eventignore' in allocated 
//memory.
CS
au_event_disable(CS what) {
   Unt p_ei_len = p_ei ? STRLEN(p_ei) : 0;
   CS save_ei = p_ei ? copySubstr(p_ei, p_ei_len) : null;
   CS new_ei = copySubstr(p_ei, p_ei_len + STRLEN(what));

   if (*what == ',' && !p_ei)
      STRCPY(new_ei, what + 1);
   else
      STRCPY(new_ei + p_ei_len, what);
   optChangeStringOptionDirect(S"eventignore", new_ei, 0, SID_NONE);
   eeglFree(new_ei);
   return save_ei;
}

void
au_event_restore(CS old_ei) {
   if (old_ei) {
      optChangeStringOptionDirect(S"eventignore", old_ei, 0, SID_NONE);
      eeglFree(old_ei);
   }
}

//do_autocmd() -- implement the :autocmd command. Can be used in the following ways:
//
//:autocmd <event> <pat> <comm>    Add <comm> to the list of commands that
//                                 will be automatically executed for <event>
//                                 when editing a file matching <pat>, in the current group.
//:autocmd <event> <pat>         Show the autocommands associated with <event> and <pat>.
//:autocmd <event>               Show the autocommands associated with <event>.
//:autocmd                       Show all autocommands.
//:autocmd! <event> <pat> <comm> Remove all autocommands associated with <event> and <pat>, 
//                               and add the command <comm>, for the current group.
//:autocmd! <event> <pat>        Remove all autocommands associated with
//                               <event> and <pat> for the current group.
//:autocmd! <event>      Remove all autocommands associated with <event> for the current group.
//:autocmd!              Remove ALL autocommands for the current group.
//
// Multiple events and patterns may be given separated by commas. Here are some examples:
//:autocmd bufread,bufenter *.c,*.h   set tw=0 smartindent noic
//:autocmd bufleave        *      set tw=79 nosmartindent ic infercase
//
//:autocmd * *.c      show all autocommands for *.c files.
//
//Mostly a {group} argument can optionally appear before <event>. "invo" can be NULL.
void
do_autocmd(CS arg_in, int forceit) {
   CS arg = arg_in;
   CS envpat = NULL;
   CS comm;
   Boole  commNeedsFreeing = false;
   AutoEvent   event;
   Boole nested = false;
   Boole once = false;
   Unt group;

   // Check for a legal group name.  If not, use AUGROUP_ALL.
   group = au_get_grouparg(&arg);

   //Scan over the events. If we find an illegal name, return here, don't do anything.
   CS pat = find_end_event(arg, group != AUGROUP_ALL);
   if (!pat)
      return;

   pat = skipwhite(pat);
   // Scan over the pattern.  Put a ZERO at the end.
   comm = pat;
   while (*comm && (!SPACE_OR_TAB(*comm) || comm[-1] == '\\'))
      comm++;
   if (*comm)
      *comm++ = ZERO;

   //Expand environment variables in the pattern.
   if (firstOccurrence(pat, '$') != NULL || firstOccurrence(pat, '~') != NULL) {
      envpat = doExpandEnvInMultiplePaths(pat);
      if (envpat)
         pat = envpat;
   }

   comm = skipwhite(comm);
   for (int i = 0; i < 2; i++) {
      if (*comm == ZERO)
         continue;

      // Check for "++once" flag.
      if (STRNCMP(comm, "++once", 6) == 0 && SPACE_OR_TAB(comm[6])) {
         if (once)
            showErrFmtMsg(_(e_duplicate_argument_str), "++once");
         once = true;
         comm = skipwhite(comm + 6);
      }

      // Check for "++nested" flag.
      if ((STRNCMP(comm, "++nested", 8) == 0 && SPACE_OR_TAB(comm[8]))) {
         if (nested) {
            showErrFmtMsg(_(e_duplicate_argument_str), "++nested");
            return;
         }
         nested = true;
         comm = skipwhite(comm + 8);
      }

      // Check for the old "nested" flag in legacy script.
      if (STRNCMP(comm, "nested", 6) == 0 && SPACE_OR_TAB(comm[6])) {
         if (nested) {
            showErrFmtMsg(_(e_duplicate_argument_str), "nested");
            return;
         }
         nested = true;
         comm = skipwhite(comm + 6);
      }
   }

   //Find the start of the commands. Expand <sfile> in it.
   if (*comm != ZERO) {
      comm = expand_sfile(comm);
      if (!comm)       // some error
         return;
      commNeedsFreeing = true;
   }

   //Print header when showing autocommands.
   if (!forceit && *comm == ZERO)
      // Highlight title
      msg_puts_title(_("\n--- Autocommands ---"));

   //Loop over the events.
   last_event = (AutoEvent)-1;      // for listing the event name
   last_group = AUGROUP_ERROR;      // for listing the group name
   
   
   AutoCommCreation auCreation = (AutoCommCreation){
      .group = group, .commandBody = comm, .deleteExisting = (Boole)forceit, 
      .once = once, .nested = nested
   };
   
   
   if (*arg == '*' || *arg == ZERO || *arg == '|') {
      if (*comm != ZERO)
          emsg(_(e_cannot_define_autocommands_for_all_events));
      else
         for (event = (AutoEvent)0; (int)event < NUM_EVENTS; event = (AutoEvent)((int)event + 1)) {
            if (autoEventImpl(event, pat, auCreation) == FAIL)
               break;
         } 
   } else {
      while (*arg && *arg != '|' && !SPACE_OR_TAB(*arg)) {
         if (autoEventImpl(event_name2nr(arg, &arg), pat, auCreation) == FAIL)
            break;
      } 
   }

   if (commNeedsFreeing)
      eeglFree(comm);
   eeglFree(envpat);
}

//Find the group ID in a ":autocmd" or ":doautocmd" argument.
//The "argp" argument is advanced to the following argument. Return the group ID
private int
au_get_grouparg(Byte **argp) {
   CS group_name;
   CS p;
   CS arg = *argp;
   Unt group = AUGROUP_ALL;

   for (p = arg; *p && !SPACE_OR_TAB(*p) && *p != '|'; ++p)
      {}
   if (p <= arg)
      return AUGROUP_ALL;

   group_name = copySubstr(arg, p - arg);
   group = findGroup(group_name);
   if (group == AUGROUP_ERROR)
      group = AUGROUP_ALL;   // no match, use all groups
   else
      *argp = skipwhite(p);   // match, skip over group name
   eeglFree(group_name);
   return group;
}

//do_autocmd() implementation. Delete, replace or create a new autocommand for one event type
int
autoEventImpl(AutoEvent event, CS pat, AutoCommCreation creation) {
   Byte buflocal_pat[25];   // for "<buffer=X>"

   Unt findgroup = creation.group == AUGROUP_ALL ? currAugroupS : creation.group;
   Boole allgroups = creation.group == AUGROUP_ALL 
      && !creation.deleteExisting 
      && *creation.commandBody == ZERO;

   //Show or delete all patterns for an event.
   AutoPat   *ap;
   if (*pat == ZERO) {
      FOR_ALL_AUTOCMD_PATTERNS(event, ap) {
         if (creation.deleteExisting) {
            if (ap->group == findgroup)
               au_remove_pat(ap);
         } ei (creation.group == AUGROUP_ALL || creation.group == ap->group)
            show_autocmd(ap, event);
      }
   }

   //Loop through all the specified patterns.
   for (CS endpat; *pat; pat = (*endpat == ',' ? endpat + 1 : endpat)) {
      //Find end of the pattern. Watch out for a comma in braces, like "*.\{obj,o\}".
      int brace_level = 0;
      for (endpat = pat;
           *endpat && (*endpat != ',' || brace_level || (endpat > pat && endpat[-1] == '\\')); 
           ++endpat
      ) {
         if (*endpat == '{')
            brace_level++;
         ei (*endpat == '}')
            brace_level--;
      }
      if (pat == endpat)      // ignore single comma
         continue;
      int patlen = (int)(endpat - pat);

      //detect special <booklocal[=X]> book-local patterns
      Boole isBuflocal = false;
      int buflocal_nr = 0;

      if (patlen >= 6 && STRNCMP(pat, "<book", 5) == 0 && pat[patlen - 1] == '>') {
         // "<book...>": Error will be printed only for addition.
         // printing and removing will proceed silently.
         isBuflocal = true;
         if (patlen == 6)
            // "<book>"
            buflocal_nr = curBook->fiNum;
         ei (patlen > 7 && pat[5] == '=') {
            if (patlen == 13 && STRNICMP(pat, "<book=abuf>", 13) == 0)
               // "<book=abuf>"
               buflocal_nr = autocmd_bufnr;
            ei (skipdigits(pat + 8) == pat + patlen - 1)
               // "<book=123>"
               buflocal_nr = atoi((char *)pat + 8);
         }
      }

      if (isBuflocal) {
          // normalize pat into standard "<book>#N" form
          sprintf((char *)buflocal_pat, "<book=%d>", buflocal_nr);
          pat = buflocal_pat;         // can modify pat and patlen
          patlen = (int)STRLEN(buflocal_pat);   //   but not endpat
      }

      //Find AutoPat entries with this pattern. When adding a command, it
      //always goes at or after the last one, so start at the end.
      Arr(Arr(AutoPat)) prev_ap; 
      if (!creation.deleteExisting 
            && *creation.commandBody != ZERO 
            && lastAutopatS[(int)event]
      )
         prev_ap = &lastAutopatS[(int)event];
      else
         prev_ap = &firstAutopatS[(int)event];
      while ((ap = *prev_ap) != NULL) {
         if (ap->pat) {
            //Accept a pattern when:
            //- a group was specified and it's that group, or a group was not specified and it's
            //the current group, or a group was not specified and we are listing
            //- the length of the pattern matches
            //- the pattern matches.
            //For <buffer[=X]>, this condition works because we normalize all buffer-local patterns.
            if ((allgroups || ap->group == findgroup)
               && ap->patlen == patlen
               && STRNCMP(pat, ap->pat, patlen) == 0
            ){
               //Remove existing autocommands. If adding any new autocomms for this AutoPat, don't
               //delete the pattern from the autopat list, append to this list.
               if (creation.deleteExisting) {
                  if (*creation.commandBody != ZERO && !ap->next) {
                     au_remove_cmds(ap);
                     break;
                  }
                  au_remove_pat(ap);
               }
               //Show autocomm's for this autopat, or buflocals <buffer=X>
               ei (*creation.commandBody == ZERO)
                  show_autocmd(ap, event);
               //Add autocomm to this autopat, if it's the last one.
               ei (ap->next == NULL)
                  break;
            }
         }
         prev_ap = &ap->next;
      }

      // Add a new command.
      if (*creation.commandBody != ZERO) {
         //If the pattern we want to add a command to does appear at the end of the list (or not 
         //is not in the list at all), add the pattern at the end of the list.
         if (!ap) {
            // refuse to add buffer-local ap if buffer number is invalid
            if (isBuflocal && (buflocal_nr == 0 || bookFindFileByBookNr(buflocal_nr) == NULL)) {
               showErrFmtMsg(_(e_book_nr_invalid_book_number), buflocal_nr);
               return FAIL;
            }

            ap = ALLOC_ONE(AutoPat);
            if (!ap)
               return FAIL;
            ap->pat = copySubstr(pat, patlen);
            ap->patlen = patlen;
            if (!ap->pat) {
               eeglFree(ap);
               return FAIL;
            }

            // need to initialize last_mode for the first ModeChanged autocmd
            if (event == EVENT_MODECHANGED && !has_modechanged())
               get_mode(last_mode);
            // Initialize the fields checked by the WinScrolled and
            // WinResized trigger to prevent them from firing right after the first autocmd is defined.
            if ((event == EVENT_PORTSCROLLED || event == EVENT_WINRESIZED)
               && !(has_winscrolled() || has_winresized())
            ) {
               Tab *save_curtab = curtab;
               Tab *t;
               FOR_ALL_TABS(t) {
                  unloadTab(curtab);
                  loadTab(t);
                  portSnapshotScrollSizes();
               }
               unloadTab(curtab);
               loadTab(save_curtab);
            }

            if (isBuflocal) {
               ap->buflocal_nr = buflocal_nr;
               ap->reg_prog = NULL;
            } else {
               ap->buflocal_nr = 0;
               CS reg_pat = file_pat_to_reg_pat(pat, endpat, OUT &ap->allow_dirs);
               ap->reg_prog = compileRegexp(reg_pat, RE_MAGIC);
               eeglFree(reg_pat);
               if (reg_pat == NULL || ap->reg_prog == NULL) {
                  eeglFree(ap->pat);
                  eeglFree(ap);
                  return FAIL;
               }
            }
            ap->comms = NULL;
            *prev_ap = ap;
            lastAutopatS[(int)event] = ap;
            ap->next = NULL;
            ap->group = (creation.group == AUGROUP_ALL) ? currAugroupS : creation.group;
         }

         // Add the autocomm to the end of the AutoComm list.
         Arr(Arr(AutoComm)) prev_ac = &(ap->comms);
         
         AutoComm* ac;
         while ((ac = *prev_ac) != NULL)
            prev_ac = &ac->next;
         AutoComm* newAutoCommand = ALLOC_ONE(AutoComm);
         if (!newAutoCommand)
            return FAIL;
         newAutoCommand->comm = copyStr(creation.commandBody);
         newAutoCommand->scriptCtx = scriptPosG;
         newAutoCommand->scriptCtx.lineNr += SOURCING_LNUM;
         
         newAutoCommand->next = NULL;
         *prev_ac = newAutoCommand;
         newAutoCommand->once = creation.once;
         newAutoCommand->nested = creation.nested;
      }
   }

   au_cleanup();   // may really delete removed patterns/commands now
   return OK;
}

//Implementation of ":doautocmd [group] event [fname]".
//Return OK for success, FAIL for failure;
int
do_doautocmd(
   CS arg_start,
   Boole do_msg,       // give message for no matching autocmds?
   OUT Boole* didSomething
) {
   CS arg = arg_start;
   Boole nothingDone = true;

   if (didSomething)
      *didSomething = false;

   //Check for a legal group name.  If not, use AUGROUP_ALL.
   Unt group = au_get_grouparg(&arg);

   if (*arg == '*') {
      emsg(_(e_cant_execute_autocommands_for_all_events));
      return FAIL;
   }

   //Scan over the events. If we find an illegal name, return here, don't do anything.
   CS fname = find_end_event(arg, group != AUGROUP_ALL);
   if (!fname)
      return FAIL;

   fname = skipwhite(fname);

   //Loop over the events.
   while (*arg && !endsComm(arg) && !SPACE_OR_TAB(*arg)) {
      if (applyAutocommGroup(event_name2nr(arg, &arg), fname, NULL, true, group, curBook, NULL))
         nothingDone = false;
   } 

   if (nothingDone && do_msg && !aborting())
      smsg(_("No matching autocommands: %s"), arg_start);
   if (didSomething)
      *didSomething = !nothingDone;

   return aborting() ? FAIL : OK;
}

// ":doautoall": execute autocommands for each loaded buffer.
void
c_doautoall(Invocation* invo) {
   int      retval = OK;
   AutocommSave   aco;
   Book* book;
   BookRef   bufref;
   CS arg = invo->arg;
   Boole did_aucmd;

   //This is a bit tricky: For some commands curPor->book needs to be equal to curBook, but 
   //for some buffers there may not be a portal. So we change the buffer for the current portal 
   //for a moment. This gives problems when the autocommands make changes to the list of buffers 
   //or portals...
   FOR_ALL_BOOKS(book) {
      // Only do loaded buffers and skip the current buffer, it's done last.
      if (book->mem.mfile == NULL || book == curBook)
         continue;

      // Find a portal into this buffer and save some values.
      auCommPrepareBook(&aco, book);
      if (curBook != book) {
         // Failed to find a portal into this buffer.  Better not execute autocommands then.
         retval = FAIL;
         break;
      }

      bookStoreInRef(OUT &bufref, book);

      // execute the autocommands for this buffer
      retval = do_doautocmd(arg, false, &did_aucmd);

      // restore the current portal
      auCommRestoreBook(&aco);

      // stop if there is some error or buffer was deleted
      if (retval == FAIL || !bookRefValid(&bufref)) {
         retval = FAIL;
         break;
      }
   }

   // Execute autocommands for the current buffer last.
   if (retval == OK) {
      do_doautocmd(arg, false, &did_aucmd);
   }
}

//Prepare for executing autocommands for (hidden) book "book".
//Search for a visible portal into the current book.  If there is none then use an entry in 
//"autoCommPortG[]". Set "curBook" and "curPor" to match "book".
//When this fails "curBook" is not equal "book".
void
auCommPrepareBook(
   AutocommSave* aco,      // structure to save values in
   Book* book      // new curBook
){
   Portal   *port;
   int save_ea;
   int same_buffer = book == curBook;

   // Find a portal that is into the new buffer
   if (same_buffer)      // be quick when book is curBook
      port = curPor;
   else {
      FOR_ALL_PORTALS(port) {
         if (port->book == book)
            break;
      } 
   } 

   // Allocate a portal when needed.
   Portal *aucPort = NULL;
   int auc_idx = AUCMD_PORTAL_COUNT;
   if (port == NULL) {
      for (auc_idx = 0; auc_idx < AUCMD_PORTAL_COUNT; ++auc_idx) {
         if (!autoCommPortG[auc_idx].isPortUsed) {
            if (autoCommPortG[auc_idx].port == NULL)
               autoCommPortG[auc_idx].port = portAllocPopup();
            aucPort = autoCommPortG[auc_idx].port;
            if (aucPort)
               autoCommPortG[auc_idx].isPortUsed = true;
            break;
         }
      } 

      // If this fails (out of memory or using all AUCMD_WIN_COUNT entries) then we can't reliably 
      // execute the autocmd, return with "curBook" unequal "book".
      if (!aucPort)
         return;
   }

   aco->save_curPor_id = curPor->id;
   aco->save_prevPor_id = prevPor == NULL ? 0 : prevPor->id;
   aco->save_State = stateG;
   if (bt_prompt(curBook))
      aco->save_prompt_insert = curBook->promptInsert;

   if (port) {
      //There is a portal into "book" in the current tab, make it the curPor. This is preferred, it
      //has the least side effects (esp. if "book" is curBook).
      aco->use_autoCommPort_idx = -1;
      curPor = port;
   } else {
      //There is no portal into "book", use "aucPort".  To minimize the side effects, insert it in 
      //the current tab. Anything related to a portal (e.g., setting folds) may have unexpected 
      //results.
      aco->use_autoCommPort_idx = auc_idx;

      initPopupPortal(aucPort, book);

      // Make sure localdir and globaldir are NULL to avoid a chdir() in enterPortal_ext().
      // initPopupPortal() has already set localDir to NULL.
      aco->localdir = curtab->localdir;
      curtab->localdir = NULL;
      aco->globaldir = globaldir;
      globaldir = NULL;

      // Split the current portal, put the aucPort in the upper half.
      // We don't want the BufEnter or WinEnter autocommands.
      block_autocmds();
      make_snapshot(SNAP_AUCMD_IDX);
      save_ea = p_ea;
      p_ea = false;

      (void)splitPortal_ins(0, WSP_TOP | WSP_FORCE_ROOM, aucPort, 0, NULL);
      computePosPortal();   // recompute portal positions
      p_ea = save_ea;
      unblock_autocmds();
      curPor = aucPort;
   }
   curBook = book;
   aco->new_curPor_id = curPor->id;
   bookStoreInRef(OUT &aco->newCurBook, curBook);

   aco->save_VIsual_active = VIsual_active;
   if (!same_buffer)
      // disable the Visual area, position may be invalid in another buffer
      VIsual_active = false;
}

//Cleanup after executing autocommands for a (hidden) buffer.
//Restore the portal as it was (if possible).
void
auCommRestoreBook(AutocommSave* aco)  {    // structure holding saved values
   Portal* curPorSave;

   if (aco->use_autoCommPort_idx >= 0) {
      Portal* acp = autoCommPortG[aco->use_autoCommPort_idx].port;

      //Find "acp". It can't be closed but it may be in another tab. Do not trigger autocommands
      block_autocmds();
      if (curPor != acp) {
         Tab* t;
         Portal* port;

         FOR_ALL_TAB_PORTALS(t, port) {
            if (port == acp) {
               if (t != curtab)
                  gotoTab(t, true, true);
               gotoPortal(acp);
               goto portalFound;
            }
         }
      }
   portalFound:
      --curBook->countPortals;
      int save_stop_insert_mode = stop_insert_mode;
      // May need to stop Insert mode if we were in a prompt buffer.
      leavingPortal(curPor);
      // Do not stop Insert mode when already in Insert mode before.
      if (aco->save_State & MODE_INSERT)
         stop_insert_mode = save_stop_insert_mode;
      // Remove the portal and frame from the tree of frames.
      Byte dummy;
      (void)portRemoveFrame(curPor, OUT &dummy, NULL, NULL);
      removePortal(curPor, NULL);

      // The portal is marked as unused, but it is not freed, it can be used again.
      autoCommPortG[aco->use_autoCommPort_idx].isPortUsed = false;
      last_status(false);       // may need to remove last status line

      if (!areTabAndPortalValid(curtab))
         //no valid portal in current tab
         closeTab(curtab);

      restore_snapshot(SNAP_AUCMD_IDX, false);
      computePosPortal();   // recompute portal positions
      unblock_autocmds();

      curPorSave = portFindById(aco->save_curPor_id);
      if (curPorSave)
         curPor = curPorSave;
      else
         //Hmm, original portal disappeared. Just use the first one.
         curPor = firstPor;
      curBook = curPor->book;
      //May need to restore insert mode for a prompt buffer.
      enteringPortal(curPor);
      if (bt_prompt(curBook))
         curBook->promptInsert = aco->save_prompt_insert;
      prevPor = portFindById(aco->save_prevPor_id);
      vars_clear(&acp->internalVars->hashTable);  // free all w: variables
      hash_init(&acp->internalVars->hashTable);   // re-use the hashtab
      //If :lcd has been used in the autocommand portal, correct current
      //directory before restoring localdir and globaldir.
      if (acp->localDir)
         portFixCurrentDir();
      eeglFree(curtab->localdir);
      curtab->localdir = aco->localdir;
      eeglFree(globaldir);
      globaldir = aco->globaldir;

      //the book contents may have changed
      VIsual_active = aco->save_VIsual_active;
      check_cursor();
      if (curPor->topLine > curBook->mem.lineCount) {
         curPor->topLine = curBook->mem.lineCount;
         curPor->topFill = 0;
      }
   } else {
      //Restore curPor. Use the portal ID, a portal may have been closed
      //and the memory re-used for another one.
      curPorSave = portFindById(aco->save_curPor_id);
      if (curPorSave) {
         //Restore the book which was previously edited by curPor, provided
         //it was changed, we are still the same portal and the book is valid.
         if (curPor->id == aco->new_curPor_id
             && curBook != aco->newCurBook.c
             && bookRefValid(&aco->newCurBook)
             && aco->newCurBook.c->mem.mfile != NULL
         ) {
            if (curPor->ownSyntax == &curBook->syntax)
               curPor->ownSyntax = &aco->newCurBook.c->syntax;
            --curBook->countPortals;
            curBook = aco->newCurBook.c;
            curPor->book = curBook;
            ++curBook->countPortals;
         }

         curPor = curPorSave;
         curBook = curPor->book;
         prevPor = portFindById(aco->save_prevPor_id);

         //In case the autocommand moves the cursor to a position that
         //does not exist in curBook.
         VIsual_active = aco->save_VIsual_active;
         check_cursor();
      }
   }

   VIsual_active = aco->save_VIsual_active;
   check_cursor();       // just in case lines got deleted
   if (VIsual_active)
      check_pos(curBook, &VIsual);
}

private int   autocmd_nested = false;

// Execute autocommands for "event" and file name "fname". Return true if any commands were executed
int
applyAutocomms(
   AutoEvent   event,
   CS fname,       // NULL or empty means use actual file name
   CS fname_io,  // fname to use for <afile> on cmdline
   Boole force,       // when true, ignore autocmd_busy
   Book* book       // buffer for <abuf>
){
   return applyAutocommGroup(event, fname, fname_io, force, AUGROUP_ALL, book, NULL);
}

// Like applyAutocomms(), but with extra "invo" argument.  This takes care of setting v:filearg.
int
auCommApplyWithInvo(
   AutoEvent event,
   CS fname,
   CS fname_io,
   Boole force,
   Book* book,
   Invocation* invo
) {
   return applyAutocommGroup(event, fname, fname_io, force, AUGROUP_ALL, book, invo);
}

// Like applyAutocomms(), but handles the caller's retval.  If the script processing is being 
// aborted or if retval is FAIL when inside a try conditional, no autocommands are executed.  If 
// otherwise the autocommands cause the script to be aborted, retval is set to FAIL.
int
applyAutocommsRetval(
   AutoEvent   event,
   CS fname,      // NULL or empty means use actual file name
   CS fname_io,   // fname to use for <afile> on cmdline
   Boole force,     // when true, ignore autocmd_busy
   Book* book,      // book for <abuf>
   int* retval    // pointer to caller's retval
){
   if (should_abort(*retval))
      return false;

   int did_cmd = applyAutocommGroup(event, fname, fname_io, force, AUGROUP_ALL, book, NULL);
   if (did_cmd && aborting())
      *retval = FAIL;
   return did_cmd;
}

// Return true when there is a CursorHold autocommand defined.
private int
has_cursorhold(void) {
   return (firstAutopatS[(int)(get_real_state() == MODE_NORMAL_BUSY
             ? EVENT_CURSORHOLD : EVENT_CURSORHOLDI)] != NULL);
}

// Return true if the CursorHold event can be triggered.
int
trigger_cursorhold(void) {
   if (!did_cursorhold
       && has_cursorhold()
       && reg_recording == 0
       && typeBufG.validLen == 0
       && !ins_compl_active())
    {
      int state = get_real_state();
      if (state == MODE_NORMAL_BUSY || (state & MODE_INSERT) != 0)
         return true;
   }
   return false;
}

// Return true when there is a WinResized autocommand defined.
int
has_winresized(void) {
   return (firstAutopatS[(int)EVENT_WINRESIZED] != NULL);
}

// Return true when there is a WinScrolled autocommand defined.
int
has_winscrolled(void) {
   return (firstAutopatS[(int)EVENT_PORTSCROLLED] != NULL);
}

// Return true when there is an CmdUndefined autocommand defined.
int
has_cmdundefined(void) {
   return (firstAutopatS[(int)EVENT_CMDUNDEFINED] != NULL);
}

// Return true when there is a TextYankPost autocommand defined.
int
has_textyankpost(void) {
   return (firstAutopatS[(int)EVENT_TEXTYANKPOST] != NULL);
}

// Return true when there is a CompleteChanged autocommand defined.
int
has_completechanged(void) {
   return (firstAutopatS[(int)EVENT_COMPLETECHANGED] != NULL);
}

//true when there is a ModeChanged autocommand defined.
int
has_modechanged(void) {
   return (firstAutopatS[(int)EVENT_MODECHANGED] != NULL);
}

// Execute autocommands for "event" and file name "fname". Return true if any commands were executed
private int
applyAutocommGroup(
   AutoEvent event,
   CS fname,        // NULL or empty means use actual file name
   CS fname_io,     // fname to use for <afile> on cmdline, NULL means use fname
   Boole force,     // when true, ignore autocmd_busy
   Unt group,       // group ID, or AUGROUP_ALL
   Book* book,      // book for <abuf>
   Invocation* invo // command arguments
){
   CS sfname = NULL;   // short file name
   CS tail;
   Boole save_changed;
   int retval = false;
   CS save_autocmd_fname;
   int save_autocmd_fname_full;
   int save_autocmd_bufnr;
   CS save_autocmd_match;
   int save_autocmd_busy;
   int save_autocmd_nested;
   static int   nesting = 0;
   AutoPatComm patcmd;
   AutoPat   *ap;
   ScriptPos   save_scriptPosG;
   FnCallEntry funccal_entry;
   CS save_cmdarg;
   long   save_cmdbang;
   static Boole filechangeshell_busy = false;
   int did_save_redobuff = false;
   SaveRedo save_redo;
   int save_keyWasTypedG = keyWasTypedG;
   ESTACK_CHECK_DECLARATION;

   // Quickly return if there are no autocommands for this event or autocommands are blocked.
   if (event == NUM_EVENTS || firstAutopatS[(int)event] == NULL || autocommsBlockedS > 0)
      goto BYPASS_AU;

   //When autocommands are busy, new autocommands are only executed when
   //explicitly enabled with the "nested" flag.
   if (autocmd_busy && !(force || autocmd_nested))
      goto BYPASS_AU;

   //Quickly return when immediately aborting on error, or when an interrupt
   //occurred or an exception was thrown but not caught.
   if (aborting())
      goto BYPASS_AU;

   //FileChangedShell never nests, because it can create an endless loop.
   if (filechangeshell_busy && (event == EVENT_FILECHANGEDSHELL
                  || event == EVENT_FILECHANGEDSHELLPOST))
      goto BYPASS_AU;

   //Ignore events in @eventignore.
   if (event_ignored(event, p_ei))
      goto BYPASS_AU;

   Boole portIgnore = false;
   //If event is allowed in 'eventignorewin', check if curPor or all portals
   //into "book" are ignoring the event.
   if (book == curBook && autoEvents[event].key <= 0)
      portIgnore = event_ignored(event, curPor->o.eventIgnorePort);
   ei (book && autoEvents[event].key <= 0 && book->countPortals > 0) {
      Tab *t;
      Portal* port;
      portIgnore = true;
      FOR_ALL_TAB_PORTALS(t, port) {
         if (port->book == book && !event_ignored(event, port->o.eventIgnorePort)) {
            portIgnore = false;
            break;
         }
      } 
   }
   if (portIgnore)
      goto BYPASS_AU;

   //Allow nesting of autocommands, but restrict the depth, because it's
   //possible to create an endless loop.
   if (nesting == 10) {
      emsg(_(e_autocommand_nesting_too_deep));
      goto BYPASS_AU;
   }

   //Check if these autocommands are disabled.  Used when doing ":all" or ":ball".
   if ((autocmd_no_enter && (event == EVENT_PORTENTER || event == EVENT_BUFENTER))
          || (autocmd_no_leave && (event == EVENT_PORTLEAVE || event == EVENT_BUFLEAVE)))
      goto BYPASS_AU;

   //Save the autocmd_* variables and info about the current buffer.
   save_autocmd_fname = autocmd_fname;
   save_autocmd_fname_full = autocmd_fname_full;
   save_autocmd_bufnr = autocmd_bufnr;
   save_autocmd_match = autocmd_match;
   save_autocmd_busy = autocmd_busy;
   save_autocmd_nested = autocmd_nested;
   save_changed = curBook->wasModified;
   Book* old_curBook = curBook;

   //Set the file name to be used for <afile>.
   //Make a copy to avoid that changing a buffer name or directory makes it invalid.
   if (!fname_io) {
      if (event == EVENT_OPTIONSET
            || event == EVENT_MODECHANGED
            || event == EVENT_TERMRESPONSEALL)
         autocmd_fname = NULL;
      ei (fname && !endsComm(fname))
         autocmd_fname = fname;
      ei (book)
         autocmd_fname = book->fullFileName;
      else
         autocmd_fname = NULL;
   } else
      autocmd_fname = fname_io;
   if (autocmd_fname)
      autocmd_fname = copyStr(autocmd_fname);
   autocmd_fname_full = false; // call fiExpandAndCopy() later

   //Set the buffer number to be used for <abuf>.
   if (book == NULL)
      autocmd_bufnr = 0;
   else
      autocmd_bufnr = book->fiNum;

   //When the file name is NULL or empty, use the file name of buffer "book".
   //Always use the full path of the file name to match with, in case "allow_dirs" is set.
   if (!fname || *fname == ZERO) {
      if (!book)
         fname = NULL;
      else {
         if (event == EVENT_SYNTAX)
            fname = book->syntaxName;
         else if (event == EVENT_FILETYPE)
            fname = book->fileType;
         else {
            if (book->shortFileName != NULL)
               sfname = copyStr(book->shortFileName);
            fname = book->fullFileName;
         }
      }
      if (!fname)
         fname = E;
      fname = copyStr(fname);   // make a copy, so we can change it
   } else {
      sfname = copyStr(fname);
      // Don't try expanding FileType, Syntax, FuncUndefined, PortalID,
      // ColorScheme, QuickFixCmd*, DirChanged and similar.
      if (event == EVENT_FILETYPE
         || event == EVENT_SYNTAX
         || event == EVENT_COMMPORTENTER
         || event == EVENT_COMMPORTLEAVE
         || event == EVENT_CMDUNDEFINED
         || event == EVENT_FUNCUNDEFINED
         || event == EVENT_REMOTEREPLY
         || event == EVENT_SPELLFILEMISSING
         || event == EVENT_QUICKFIXCMDPRE
         || event == EVENT_OPTIONSET
         || event == EVENT_QUICKFIXCMDPOST
         || event == EVENT_DIRCHANGED
         || event == EVENT_DIRCHANGEDPRE
         || event == EVENT_MODECHANGED
         || event == EVENT_MENUPOPUP
         || event == EVENT_USER
         || event == EVENT_WINCLOSED
         || event == EVENT_WINRESIZED
         || event == EVENT_PORTSCROLLED
         || event == EVENT_TERMRESPONSEALL)
      {
         fname = copyStr(fname);
         autocmd_fname_full = true; // don't expand it later
      } else
         fname = fiExpandAndCopy(fname, false);
   }

   //Set the name to be used for <amatch>.
   autocmd_match = fname;

   // Don't redraw while doing autocommands.
   ++isRedrawingDisabledG;

   // name and lnum are filled in later
   estack_push(ETYPE_AUCMD, NULL, 0);
   ESTACK_CHECK_SETUP;

   save_scriptPosG = scriptPosG;


   // Don't use local function variables, if called from a function.
   save_funccal(&funccal_entry);

   //When starting to execute autocommands, save the search patterns.
   if (!autocmd_busy) {
      save_search_patterns();
      if (!ins_compl_active()) {
         saveRedobuff(&save_redo);
         did_save_redobuff = true;
      }
      curBook->didFiletype = curBook->keepFiletype;
   }

   //Note that we are applying autocmds.  Some commands need to know.
   autocmd_busy = true;
   filechangeshell_busy = (event == EVENT_FILECHANGEDSHELL);
   ++nesting;      // see matching decrement below

   // Remember that FileType was triggered.  Used for did_filetype().
   if (event == EVENT_FILETYPE)
      curBook->didFiletype = true;

   tail = fiGetShortFiName(fname);

   // Find first autocommand that matches
   CLEAR_FIELD(patcmd);
   patcmd.curpat = firstAutopatS[(int)event];
   patcmd.group = group;
   patcmd.fname = fname;
   patcmd.sfname = sfname;
   patcmd.tail = tail;
   patcmd.event = event;
   patcmd.arg_bufnr = autocmd_bufnr;
   auto_next_pat(&patcmd, false);

   // found one, start executing the autocommands
   if (patcmd.curpat) {
      // add to active_apc_list
      patcmd.next = active_apc_list;
      active_apc_list = &patcmd;

      // set v:cmdarg (only when there is a matching pattern)
      save_cmdbang = (long)get_EeglVar_nr(VV_CMDBANG);
      if (invo) {
         save_cmdarg = set_cmdarg(invo, NULL);
         set_EeglVar_nr(VV_CMDBANG, (long)invo->forceit);
      } else
         save_cmdarg = NULL;   // avoid gcc warning
      retval = true;
      // mark the last pattern, to avoid an endless loop when more patterns
      // are added when executing autocommands
      for (ap = patcmd.curpat; ap->next != NULL; ap = ap->next)
          ap->last = false;
      ap->last = true;

      // Make sure cursor and topline are valid.  The first time the current
      // values are saved, restored by reset_lnums().  When nested only the
      // values are corrected when needed.
      if (nesting == 1)
         check_lnums(true);
      else
         check_lnums_nested(true);

      int save_anyEmsgG = anyEmsgG;
      int save_ex_pressedreturn = get_pressedreturn();

      doCommand(NULL, &getnextac, (void *)&patcmd, DOCMD_NOWAIT|DOCMD_VERBOSE|DOCMD_REPEAT);

      anyEmsgG += save_anyEmsgG;
      set_pressedreturn(save_ex_pressedreturn);

      if (nesting == 1)
         // restore cursor and topline, unless they were changed
         reset_lnums();

      if (invo) {
         (void)set_cmdarg(NULL, save_cmdarg);
         set_EeglVar_nr(VV_CMDBANG, save_cmdbang);
      }
      // delete from active_apc_list
      if (active_apc_list == &patcmd)       // just in case
          active_apc_list = patcmd.next;
   }

   if (isRedrawingDisabledG > 0)
      --isRedrawingDisabledG;
   autocmd_busy = save_autocmd_busy;
   filechangeshell_busy = false;
   autocmd_nested = save_autocmd_nested;
   eeglFree(SOURCING_NAME);
   ESTACK_CHECK_NOW;
   estack_pop();
   eeglFree(autocmd_fname);
   autocmd_fname = save_autocmd_fname;
   autocmd_fname_full = save_autocmd_fname_full;
   autocmd_bufnr = save_autocmd_bufnr;
   autocmd_match = save_autocmd_match;
   scriptPosG = save_scriptPosG;
   restore_funccal();
   keyWasTypedG = save_keyWasTypedG;
   eeglFree(fname);
   eeglFree(sfname);
   --nesting;      // see matching increment above

   //When stopping to execute autocommands, restore the search patterns and
   //the redo buffer. Free any buffers in the auPendingFreeBooksG list and
   //free any portals in the auPendingFreePortalsG list.
   if (!autocmd_busy) {
      restore_search_patterns();
      if (did_save_redobuff)
          restoreRedobuff(&save_redo);
      curBook->didFiletype = false;
      while (auPendingFreeBooksG != NULL) {
         Book *b = auPendingFreeBooksG->next;
         eeglFree(auPendingFreeBooksG);
         auPendingFreeBooksG = b;
      }
      while (auPendingFreePortalsG) {
         Portal *w = auPendingFreePortalsG->next;
         eeglFree(auPendingFreePortalsG);
         auPendingFreePortalsG = w;
      }
   }

   //Some events don't set or reset the Changed flag. Check if still in the same buffer!
   if (curBook == old_curBook
       && (event == EVENT_BUFREADPOST
            || event == EVENT_BUFWRITEPOST
            || event == EVENT_FILEAPPENDPOST
            || event == EVENT_EEGLLEAVE
            || event == EVENT_EEGLLEAVEPRE)
   ) {
      curBook->wasModified = save_changed;
   }

   au_cleanup();   // may really delete removed patterns/commands now

BYPASS_AU:
   // When wiping out a buffer make sure all its buffer-local autocommands are deleted.
   if (event == EVENT_BUFWIPEOUT && book != NULL)
      scrRemoveAutocommsFromBook(book);

   if (retval == OK && event == EVENT_FILETYPE)
      curBook->auDidFileType = true;

   return retval;
}

private CS old_termresponse = NULL;
private CS old_termu7resp = NULL;
private CS old_termblinkresp = NULL;
private CS old_termrbgresp = NULL;
private CS old_termrfgresp = NULL;
private CS old_termstyleresp = NULL;

//Block triggering autocommands until unblock_autocmd() is called.
//Can be used recursively, so long as it's symmetric.
void
block_autocmds(void) {
   // Remember the value of v:termresponse.
   if (autocommsBlockedS == 0) {
      old_termresponse = get_EeglVar_str(VV_TERMRESPONSE);
      old_termu7resp = get_EeglVar_str(VV_TERMU7RESP);
      old_termblinkresp = get_EeglVar_str(VV_TERMBLINKRESP);
      old_termrbgresp = get_EeglVar_str(VV_TERMRBGRESP);
      old_termrfgresp = get_EeglVar_str(VV_TERMRFGRESP);
      old_termstyleresp = get_EeglVar_str(VV_TERMSTYLERESP);
   }
   ++autocommsBlockedS;
}

void
unblock_autocmds(void) {
   --autocommsBlockedS;

   // When v:termresponse, etc, were set while autocommands were blocked,
   // trigger the autocommands now.  Esp. useful when executing a shell
   // command during startup (vimdiff).
   if (autocommsBlockedS == 0) {
      if (get_EeglVar_str(VV_TERMRESPONSE) != old_termresponse) {
          applyAutocomms(EVENT_TERMRESPONSE, NULL, NULL, false, curBook);
          applyAutocomms(EVENT_TERMRESPONSEALL, S"version", NULL, false, curBook);
      }
      if (get_EeglVar_str(VV_TERMU7RESP) != old_termu7resp) {
          applyAutocomms(EVENT_TERMRESPONSEALL, S"ambiguouswidth", NULL, false, curBook);
      }
      if (get_EeglVar_str(VV_TERMBLINKRESP) != old_termblinkresp) {
          applyAutocomms(EVENT_TERMRESPONSEALL, S"cursorblink", NULL, false, curBook);
      }
      if (get_EeglVar_str(VV_TERMRBGRESP) != old_termrbgresp) {
          applyAutocomms(EVENT_TERMRESPONSEALL, S"background", NULL, false, curBook);
      }
      if (get_EeglVar_str(VV_TERMRFGRESP) != old_termrfgresp) {
          applyAutocomms(EVENT_TERMRESPONSEALL, S"foreground", NULL, false, curBook);
      }
      if (get_EeglVar_str(VV_TERMSTYLERESP) != old_termstyleresp) {
          applyAutocomms(EVENT_TERMRESPONSEALL, S"cursorshape", NULL, false, curBook);
      }
   }
}

int
areAutocommsBlocked(void) {
   return autocommsBlockedS != 0;
}

//Find next autocommand pattern that matches.
private void
auto_next_pat(AutoPatComm* apc, int stop_at_last) {      // stop when 'last' flag is set
   AutoPat* ap;
   AutoComm* cp;
   CS name;
   CS s;
   CS namep;

   Estack* entry = ((Estack *)exestack.c) + exestack.len - 1;

   // Clear the exestack entry for this ETYPE_AUCMD entry.
   EE_CLEAR(entry->name);
   entry->info.aucmd = NULL;

   for (ap = apc->curpat; ap && !gotInterruptG; ap = ap->next) {
      apc->curpat = NULL;

      //Only use a pattern when it has not been removed, has commands and
      //the group matches. For buffer-local autocommands only check the buffer number.
      if (ap->pat && ap->comms && (apc->group == AUGROUP_ALL || apc->group == ap->group)) {
         // execution-condition
         if (ap->buflocal_nr == 0
             ? (match_file_pat(NULL, &ap->reg_prog, apc->fname,
                     apc->sfname, apc->tail, ap->allow_dirs))
             : ap->buflocal_nr == apc->arg_bufnr)
          {
            name = event_nr2name(apc->event);
            s = _("%s Autocommands for \"%s\"");
            namep = alloc(STRLEN(s) + STRLEN(name) + ap->patlen + 1);
            SPRINTF(namep, s, name, ap->pat);
            if (p_verbose >= 8) {
               verbose_enter();
               smsg(_("Executing %s"), namep);
               verbose_leave();
            }

            // Update the exestack entry for this autocmd.
            entry->name = namep;
            entry->info.aucmd = apc;

            apc->curpat = ap;
            // mark last command
            for (cp = ap->comms; cp->next != NULL; cp = cp->next)
                cp->last = false;
            cp->last = true;
         }
         line_breakcheck();
         if (apc->curpat != NULL)       // found a match
            break;
      }
      if (stop_at_last && ap->last)
         break;
    }
}

//Get the script context where autocommand "acp" is defined.
private ScriptPos*
acp_scriptCtx(AutoPatComm *acp) {
   return &acp->scriptCtx;
}

//Get next autocommand command. Called by doCommand() to get the next line for ":if".
//Return allocated string, or NULL for end of autocommands.
CS
getnextac(Unt c UNUSED, void* cookie, int indent UNUSED, GetlineAlgo options UNUSED) {
   CS retval;

   // Can be called again after returning the last line.
   AutoPatComm* acp = (AutoPatComm *)cookie;
   if (acp->curpat == NULL)
      return NULL;

   // repeat until we find an autocommand to execute
   for (;;) {
      // skip removed commands
      while (acp->nextComm && !acp->nextComm->comm)
         if (acp->nextComm->last)
            acp->nextComm = NULL;
         else
            acp->nextComm = acp->nextComm->next;

      if (acp->nextComm)
         break;

      // at end of commands, find next pattern that matches
      if (acp->curpat->last)
         acp->curpat = NULL;
      else
         acp->curpat = acp->curpat->next;
      if (acp->curpat)
         auto_next_pat(acp, true);
      if (!acp->curpat)
         return NULL;
   }

   AutoComm* ac = acp->nextComm;

   if (p_verbose >= 9) {
      verbose_enter_scroll();
      smsg(_("autocommand %s"), ac->comm);
      msg_puts(S"\n");   // don't overwrite this either
      verbose_leave_scroll();
   }
   retval = copyStr(ac->comm);
   // Remove one-shot ("once") autocmd in anticipation of its execution.
   if (ac->once)
      au_del_cmd(ac);
   autocmd_nested = ac->nested;
   scriptPosG = ac->scriptCtx;
   acp->scriptCtx = scriptPosG;
   if (ac->last)
      acp->nextComm = NULL;
   else
      acp->nextComm = ac->next;
   return retval;
}

//Return true if there is a matching autocommand for "fname". To account for buffer-local 
//autocommands, function needs to know in which buffer the file will be opened.
int
has_autocmd(AutoEvent event, CS sfname, Book* book) {
   AutoPat* ap;
   CS tail = fiGetShortFiName(sfname);
   int retval = false;

   CS fname = fiExpandAndCopy(sfname, false);
   if (!fname)
      return false;

   FOR_ALL_AUTOCMD_PATTERNS(event, ap) {
      if (ap->pat && ap->comms
            && (ap->buflocal_nr == 0
         ? match_file_pat(NULL, &ap->reg_prog, fname, sfname, tail, ap->allow_dirs)
         : book && ap->buflocal_nr == book->fiNum
         )
      ){
          retval = true;
          break;
      }
   } 

   eeglFree(fname);

   return retval;
}

//Function given to expandGeneric() to obtain the list of autocommand group names.
private CS
get_augroup_name(Expand* xp UNUSED, int idx) {
   if (idx == augroups.len)      // add "END" add the end
      return (CS)"END";
   if (idx < 0 || idx >= augroups.len)   // end of list
      return NULL;
   if (AUGROUP_NAME(idx) == NULL || AUGROUP_NAME(idx) == get_deleted_augroup())
      // skip deleted entries
      return E;
   return AUGROUP_NAME(idx);      // return a name
}

private int include_groups = false;

private CS
set_context_in_autocmd(Expand* xp, CS arg, int doautocmd) {  //true for :doauto*, false for :autocmd

   // check for a group name, skip it if present
   include_groups = false;
   CS p = arg;
   Unt group = au_get_grouparg(&arg);
   // If there only is a group name that's what we expand.
   if (*arg == ZERO && group != AUGROUP_ALL && !SPACE_OR_TAB(arg[-1])) {
      arg = p;
      group = AUGROUP_ALL;
   }

   // skip over event name
   for (p = arg; *p != ZERO && !SPACE_OR_TAB(*p); ++p) {
      if (*p == ',')
          arg = p + 1;
   } 
   if (*p == ZERO) {
      if (group == AUGROUP_ALL)
         include_groups = true;
      xp->context = EXPAND_EVENTS;       // expand event name
      xp->input = text(arg);
      return NULL;
   }

   // skip over pattern
   arg = skipwhite(p);
   while (*arg && (!SPACE_OR_TAB(*arg) || arg[-1] == '\\'))
      arg++;
   if (*arg)
      return arg;             // expand (next) command

   if (doautocmd)
      xp->context = EXPAND_FILES;       // expand file names
   else
      xp->context = EXPAND_NOTHING;    // pattern is not expanded
   return NULL;
}

//Function given to expandGeneric() to obtain the list of event names.
CS
get_event_name(Expand* xp UNUSED, int idx) {
   if (idx < augroups.len) {     // First list group names, if wanted
      if (!include_groups || AUGROUP_NAME(idx) == NULL 
            || AUGROUP_NAME(idx) == get_deleted_augroup()
      )
         return (CS)"";   // skip deleted entries
      return AUGROUP_NAME(idx);   // return a name
   }

   int i = idx - augroups.len;
   if (i < 0 || i >= NUM_EVENTS)
      return NULL;

   return autoEvents[i].value.c;
}

//Function given to expandGeneric() to obtain the list of event names. Don't include groups.
CS
get_event_name_no_group(Expand* xp UNUSED, int idx, int win) {
   if (idx < 0 || idx >= NUM_EVENTS)
      return NULL;

   if (!win)
      return autoEvents[idx].value.c;

   // Need to check subset of allowed values for 'eventignorewin'.
   int j = 0;
   for (int i = 0; i < NUM_EVENTS; ++i) {
      j += autoEvents[i].key <= 0;
      if (j == idx + 1)
         return autoEvents[i].value.c;
   }

   return NULL;
}

//Return true when there is a TabClosedPre autocommand defined.
int
has_tabclosedpre(void) {
   return (firstAutopatS[(int)EVENT_TABCLOSEDPRE] != NULL);
}

//Return true if autocmd is supported.
int
autocmd_supported(CS name) {
   CS p;
   return (event_name2nr(name, &p) != NUM_EVENTS);
}

//Return true if an autocommand is defined for a group, event and
//pattern:  The group can be omitted to accept any group. "event" and "pattern"
//can be NULL to accept any event and pattern. "pattern" can be NULL to accept
//any pattern. Book-local patterns <book> or <book=N> are accepted.
//Used for:
//  exists("#Group") or
//  exists("#Group#Event") or
//  exists("#Group#Event#pat") or
//  exists("#Event") or
//  exists("#Event#pat")
int
au_exists(CS arg) {
   Byte   *pattern = NULL;
   Byte   *event_name;
   Byte   *p;
   AutoEvent   event;
   AutoPat   *ap;
   Book   *buflocal_buf = NULL;
   int      retval = false;

   // Make a copy so that we can change the '#' chars to a ZERO.
   CS arg_save = copyStr(arg);
   p = firstOccurrence(arg_save, '#');
   if (p)
      *p++ = ZERO;

   // First, look for an autocmd group name
   Unt group = findGroup(arg_save);
   if (group == AUGROUP_ERROR) {
      // Didn't match a group name, assume the first argument is an event.
      group = AUGROUP_ALL;
      event_name = arg_save;
   } else {
      if (!p) {
         // "Group": group name is present and it's recognized
         retval = true;
         goto theend;
      }

      // Must be "Group#Event" or "Group#Event#pat".
      event_name = p;
      p = firstOccurrence(event_name, '#');
      if (p)
         *p++ = ZERO;       // "Group#Event#pat"
    }

   pattern = p;       // "pattern" is NULL when there is no pattern

   // find the index (enum) for the event name
   event = event_name2nr(event_name, &p);

   // return false if the event name is not recognized
   if (event == NUM_EVENTS)
      goto theend;

   // Find the first autocommand for this event.
   // If there isn't any, return false; If there is one and no pattern given, return true.
   ap = firstAutopatS[(int)event];
   if (!ap)
      goto theend;

   // if pattern is "<book>", special handling is needed which uses curBook
   // for pattern "<book=N>, fnamecmp() will work fine
   if (pattern && caseInsensitiveCompare(pattern, "<book>") == 0)
      buflocal_buf = curBook;

   // Check if there is an autocommand with the given pattern.
   for ( ; ap; ap = ap->next) {
      // only use a pattern when it has not been removed and has commands.
      // For buffer-local autocommands, fnamecmp() works fine.
      if (ap->pat != NULL && ap->comms != NULL
          && (group == AUGROUP_ALL || ap->group == group)
          && (!pattern
            || (buflocal_buf == NULL
                ? fnamecmp(ap->pat, pattern) == 0
                : ap->buflocal_nr == buflocal_buf->fiNum)))
      {
          retval = true;
          break;
      }
   } 

theend:
   eeglFree(arg_save);
   return retval;
}

//autocmd_add() and autocmd_delete() functions
private void
autocommAddOrDelete(Arr(Var) argvars, Var* returnVar, Boole delete) {
   DictItem* di;
   AutoEvent event;
   CS group_name = NULL;
   Unt      group;
   CS pat = NULL;
   List* pat_list;
   ListItem   *pli;
   CS comm = NULL;
   CS end;
   int      once;
   int      nested;
   int      retval = VVAL_TRUE;
   int      save_augroup = currAugroupS;

   returnVar->tag = VAR_BOOL;
   returnVar->number = VVAL_FALSE;

   if (confirmVarIsList(argvars, 0) == FAIL)
      return;

   List* aucmd_list = argvars[0].list;
   if (aucmd_list == NULL)
      return;

   ListItem* li;
   FOR_ALL_LIST_ITEMS(aucmd_list, li) {
      EE_CLEAR(group_name);
      EE_CLEAR(comm);
      CS event_name = NULL;
      List* event_list = NULL;
      pat = NULL;
      pat_list = NULL;

      if (li->c.tag != VAR_BAG)
         continue;

      Bag* event_dict = li->c.bag;
      if (event_dict == NULL)
         continue;

      di = bagFind(event_dict, tConst("event"));
      if (di != NULL) {
         if (di->c.tag == VAR_STRING) {
            event_name = di->c.string;
            if (event_name == NULL) {
               emsg(_(e_string_required));
               continue;
            }
         } ei (di->c.tag == VAR_LIST) {
            event_list = di->c.list;
            if (event_list == NULL) {
               emsg(_(e_list_required));
               continue;
            }
         } else {
            emsg(_(e_string_or_list_expected));
            continue;
         }
      }

      group_name = bagGetString(event_dict, tConst("group"), true);
      if (group_name == NULL || *group_name == ZERO)
         // if the autocomm group name is not specified, then use the current autocomm group
         group = currAugroupS;
      else {
         group = findGroup(group_name);
         if (group == AUGROUP_ERROR) {
            if (delete) {
               showErrFmtMsg(_(e_no_such_group_str), group_name);
               retval = VVAL_FALSE;
               break;
            }
            // group is not found, create it now
            group = au_new_group(group_name);
            if (group == AUGROUP_ERROR) {
               showErrFmtMsg(_(e_no_such_group_str), group_name);
               retval = VVAL_FALSE;
               break;
            }

            currAugroupS = group;
         }
      }

      //if a buffer number is specified, then generate a pattern of the form
      //"<buffer=n>. Otherwise, use the pattern supplied by the user.
      if (bagHasKey(event_dict, tConst("bufnr"))) {
         Long bnum = bagGetNumber_def(event_dict, tConst("bufnr"), -1);
         if (bnum == -1)
            continue;

         eeSnprintf(IObuff, IOSIZE, "<buffer=%d>", (int)bnum);
         pat = IObuff;
      } else {
         di = bagFind(event_dict, tConst("pattern"));
         if (di) {
            if (di->c.tag == VAR_STRING) {
                pat = di->c.string;
               if (pat == NULL) {
                  emsg(_(e_string_required));
                  continue;
               }
            } ei (di->c.tag == VAR_LIST) {
               pat_list = di->c.list;
               if (pat_list == NULL) {
                  emsg(_(e_list_required));
                  continue;
               }
            } else {
                emsg(_(e_string_or_list_expected));
                continue;
            }
         } ei (delete)
            pat = E;
      }

      once = bagGetBool(event_dict, tConst("once"), false);
      nested = bagGetBool(event_dict, tConst("nested"), false);
      // if 'replace' is true, then remove all the commands associated with
      // this autocmd event/group and add the new command.
      Boole replace = bagGetBool(event_dict, tConst("replace"), false);

      comm = bagGetString(event_dict, tConst("comm"), true);
      if (!comm) {
         if (delete)
            comm = copyStr(E);
         else
            continue;
      }

      if (delete && (event_name == NULL || (event_name[0] == '*' && event_name[1] == ZERO))) {
         // if the event name is not specified or '*', delete all the events
         for (event = (AutoEvent)0; (int)event < NUM_EVENTS;
             event = (AutoEvent)((int)event + 1)
         ) {
            if (autoEventImpl(event, pat,
                        (AutoCommCreation){
                           .group = group, .commandBody = comm, .deleteExisting = delete, 
                           .once = once, .nested = nested
                        }
                ) == FAIL
            ) {
               retval = VVAL_FALSE;
               break;
            }
         }
      } else {
         Byte *p = NULL;

         ListItem* eli = NULL;
         end = NULL;
         while (true) {
            if (event_list) {
               if (!eli)
                  eli = event_list->first;
               else
                  eli = eli->next;
               if (eli == NULL)
                  break;
               if (eli->c.tag != VAR_STRING || (p = eli->c.string) == NULL) {
                  emsg(_(e_string_required));
                  break;
               }
            } else {
               if (!p)
                  p = event_name;
               if (p == NULL || *p == ZERO)
                  break;
            }

            event = event_name2nr(p, &end);
            if (event == NUM_EVENTS || *end != ZERO) {
                // this also catches something following a valid event name
                showErrFmtMsg(_(e_no_such_event_str), p);
                retval = VVAL_FALSE;
                break;
            }
            
            AutoCommCreation auCreation = (AutoCommCreation){
               .group = group, .commandBody = comm, .deleteExisting = delete || replace,
               .once = once, .nested = nested
            };
            if (pat) {
               if (autoEventImpl(event, pat, auCreation) == FAIL) {
                  retval = VVAL_FALSE;
                  break;
               }
            } ei (pat_list) {
               FOR_ALL_LIST_ITEMS(pat_list, pli) {
                  if (pli->c.tag != VAR_STRING || pli->c.string == NULL) {
                     emsg(_(e_string_required));
                     continue;
                  }
                  if (autoEventImpl(event, pli->c.string, auCreation) == FAIL){
                     retval = VVAL_FALSE;
                     break;
                  }
               }
               if (retval == VVAL_FALSE)
                  break;
            }
            if (event_name != NULL)
                p = end;
          }
      }

      //if only the autocmd group name is specified for delete and the
      //autocomm event, pattern and comm are not specified, then delete the autocomm group.
      if (delete && group_name 
            && (!event_name || event_name[0] == ZERO)
            && (!pat || pat[0] == ZERO)
            && (!comm || comm[0] == ZERO)
      )
         au_del_group(group_name);
   }

   EE_CLEAR(group_name);
   EE_CLEAR(comm);

   currAugroupS = save_augroup;
   returnVar->number = retval;
}

// autocmd_add() function
void
f_autocmd_add(Arr(Var) argvars, Var* returnVar) {
   autocommAddOrDelete(argvars, returnVar, false);
}

// autocmd_delete() function
void
f_autocmd_delete(Arr(Var) argvars, Var* returnVar) {
   autocommAddOrDelete(argvars, returnVar, true);
}

// Return a List of autocomms.
void
f_autocmd_get(Arr(Var) argvars, Var* returnVar) {
   AutoEvent   event_arg = NUM_EVENTS;
   AutoEvent   event;
   AutoPat   *ap;
   AutoComm* ac;
   List   *event_list;
   Bag* event_dict;
   CS event_name = NULL;
   CS pat = NULL; 
   CS name = NULL;
   Unt group = AUGROUP_ALL;

   allocReturnList(returnVar);
   if (check_for_oself_arg(argvars, 0) == FAIL)
      return;

   if (argvars[0].tag == VAR_BAG) {
      // return only the autocmds in the specified group
      if (bagHasKey(argvars[0].bag, tConst("group"))) {
         name = bagGetString(argvars[0].bag, tConst("group"), true);
         if (name == NULL)
            return;

         if (*name == ZERO)
            group = AUGROUP_DEFAULT;
         else {
            group = findGroup(name);
            if (group == AUGROUP_ERROR) {
               showErrFmtMsg(_(e_no_such_group_str), name);
               eeglFree(name);
               return;
            }
         }
         eeglFree(name);
      }

      // return only the autocmds for the specified event
      if (bagHasKey(argvars[0].bag, tConst("event"))) {
         name = bagGetString(argvars[0].bag, tConst("event"), true);
         if (!name)
            return;

         if (name[0] == '*' && name[1] == ZERO)
            event_arg = NUM_EVENTS;
         else {
            Kv target;
            Kv *entry;

            target.key = 0;
            target.value.c = name;
            target.value.len = STRLEN(target.value.c);
            entry = (Kv *)bsearch(&target, &autoEvents,
                NUM_EVENTS, sizeof(autoEvents[0]), cmp_keyvalue_value_ni);
            if (!entry) {
               showErrFmtMsg(_(e_no_such_event_str), name);
               eeglFree(name);
               return;
            }
            event_arg = (AutoEvent)abs(entry->key);
          }
          eeglFree(name);
      }

      // return only the autocmds for the specified pattern
      if (bagHasKey(argvars[0].bag, tConst("pattern"))) {
         pat = bagGetString(argvars[0].bag, tConst("pattern"), true);
         if (!pat)
            return;
      }
   }

   event_list = returnVar->list;

   // iterate through all the autocomm events
   for (event = (AutoEvent)0; (int)event < NUM_EVENTS; event = (AutoEvent)((int)event + 1)) {
      if (event_arg != NUM_EVENTS && event != event_arg)
         continue;

      event_name = event_nr2name(event);

      // iterate through all the patterns for this autocmd event
      FOR_ALL_AUTOCMD_PATTERNS(event, ap) {
         if (!ap->pat)      // pattern has been removed
            continue;

         if (group != AUGROUP_ALL && group != ap->group)
            continue;

         if (pat && STRCMP(pat, ap->pat) != 0)
            continue;

         CS group_name = get_augroup_name(NULL, ap->group);

         // iterate through all the commands for this pattern and add one item for each comm
         for (ac = ap->comms; ac; ac = ac->next) {
            event_dict = allocBag();
            if (listAppendBag(event_list, event_dict) == FAIL) {
               eeglFree(pat);
               return;
            }

            if (bagAddString(event_dict, S"event", event_name) == FAIL
               || bagAddString(
                     event_dict, S"group", group_name == NULL ? S"" : group_name
                  ) == FAIL
               || (ap->buflocal_nr != 0
                  && (bagAddNumber(event_dict, S"bufnr", ap->buflocal_nr) == FAIL))
                  || bagAddString(event_dict, S"pattern", ap->pat) == FAIL
                  || bagAddString(event_dict, S"comm", ac->comm) == FAIL
                  || bagAdd_bool(event_dict, S"once", ac->once) == FAIL
                  || bagAdd_bool(event_dict, S"nested", ac->nested) == FAIL
            ){
                eeglFree(pat);
                return;
            }
         }
      }
   }

   eeglFree(pat);
}

//}}}
