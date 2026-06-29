//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## motor.c: the appliation runner of Eegl

#include "eegl.h"

// Various parameters passed between main() and other functions.
private MainParams paramsP;

private void* virtualBuf = null;      // buffer for setvbuf()

private CS start_dir = NULL;   // current working dir on startup

//{{{the intro screen and version info about the current build

// Vim originated from Stevie version 3.6 (Fish disk 217) by GRWalter (Fred)
// It has been changed beyond recognition since then.
// Now there is a simple IDE forked off from it, Eegl.

private CS programVersion = (CS)EEGL_VERSION_SHORT;
private CS mediumVersion = (CS)EEGL_VERSION_MEDIUM;

//char longVersion[sizeof(EEGL_VERSION_LONG_DATE) + sizeof(__DATE__) + sizeof(__TIME__) + 3];

private Byte longVersion[] = EEGL_VERSION_LONG_DATE __DATE__ " " __TIME__ ")";

private CS features[] = {SMAP((CS),
   "+mouse_sgr",
   "+wayland",
   "+xattr",
   "-xfontset",
#ifdef FEAT_XTERM_SAVE
   "+xterm_save",
#else
   "-xterm_save",
#endif
   NULL
)};

private int included_patches[] = {   
// Add new patch number below this line */
   0
};

//Place to put a short description when adding a feature with a patch.
//Keep it short, e.g.,: "relative numbers", "persistent undo".
//Also add a comment marker to separate the lines.
//See the official Eegl patches for the diff format: It must use a context of
//one line only.  Create it by hand or use "diff -C2" and edit the patch.
private CS extra_patches[] = {
   // Add your patch description below this line
   NULL
};

int
highest_patch(void) {
   // this relies on the highest patch number to be the first entry
   return included_patches[0];
}

// List all features aligned in columns, dictionary style.
private void
list_features(void) {
   listInColumns((Byte **)features, -1, -1, true);
}


private void
list_version(void) {
   int i;
   int first;
   CS s = S"";

   //When adding features here, don't forget to update the list of internal variables in eval.c!
   msg(longVersion);


   // Print the list of patch numbers if there is at least one.
   // Print a range when patches are consecutive: "1-10, 12, 15-40, 42-45"
   if (included_patches[0] != 0) {
      msg_puts(_("\nIncluded patches: "));
      first = -1;
      i = (int)ARRAY_LENGTH(included_patches) - 1;
      while (--i >= 0) {
         if (first < 0)
            first = included_patches[i];
         if (i == 0 || included_patches[i - 1] != included_patches[i] + 1) {
            msg_puts(s);
            s = S", ";
            msg_outnum((long)first);
            if (first != included_patches[i]) {
               msg_puts((CS)"-");
               msg_outnum((long)included_patches[i]);
            }
            first = -1;
         }
      }
   }

   // Print the list of extra patch descriptions if there is at least one.
   if (extra_patches[0] != NULL) {
      msg_puts(_("\nExtra patches: "));
      s = E;
      for (i = 0; extra_patches[i] != NULL; ++i) {
         msg_puts(s);
         s = (CS)", ";
         msg_puts(extra_patches[i]);
      }
   }

   printMsgWithWrap(_("Features included (+) or not (-):\n"));

   list_features();
   if (msgColG > 0)
      msg_putchar('\n');

   printMsgWithWrap(_("       defaults file: \""));
   printMsgWithWrap(EE_DEFAULTS_FILE);
   printMsgWithWrap((CS)"\"\n");
#ifdef DEBUG
   printMsgWithWrap("\n");
   printMsgWithWrap(_("  DEBUG BUILD"));
#endif
}


void
c_version(Invocation* invo) {
   //Ignore a ":version 9.99" command.
   if (*invo->arg == ZERO) {
      msg_putchar('\n');
      list_version();
   }
}


private void do_intro_line(int row, CS mesg, int add_version);
private void intro_message(int colon);

// Show the intro message when not editing a file.
void
maybe_intro_message(void) {
   if (CURBOOK_EMPTY() && !curBook->currFileName && !firstPor->next && p_intro)
      intro_message(false);
}

//Give an introductory message about Eegl.
//Only used when starting Eegl on an empty file, without a file name.
//Or with the ":intro" command (for Sven :-).
private void
intro_message(int colon) {     // true for ":intro"
   int i;
   CS p;
   static CS lines[] = { SMAP((CS),
      "Eegl - Extensible editor for GNU/Linux",
      "",
      "version ",
      "by Bram Moolenaar, Egor Sozonov et al.",
      "Eegl is open source and freely distributable",
      "",
      "type  :q<Enter>               to exit         ",
      "type  :help<Enter>  or  <F1>  for on-line help",
      "type  :help version9<Enter>   for version info",
      "",
      ""
   )};

   // blanklines = screen height - # message lines
   int blanklines = (int)visibleRowsG - (ARRAY_LENGTH(lines) - 1) + 4;

   // Don't overwrite a statusline.  Depends on @commheight.
   blanklines -= visibleRowsG - topframeG->width;
   if (blanklines < 0)
      blanklines = 0;
   // Show the sponsor and register message one out of four times, the Uganda
   // message two out of four times.
   int sponsor = (int)time(NULL);
   sponsor = ((sponsor & 2) == 0) - ((sponsor & 4) == 0);

   // start displaying the message lines after half of the blank lines
   int row = blanklines / 2;
   if ((row >= 2 && topframeG->width >= 50) || colon) {
      for (i = 0; i < (int)ARRAY_LENGTH(lines); ++i) {
         p = lines[i];
         if (!p) {
            break;
         }
         if (sponsor != 0) {
            if (STRSTR(p, "children") != NULL)
               p = sponsor < 0
                  ? N_("Sponsor Eegl development!")
                  : N_("Become a registered Eegl user!");
            ei (STRSTR(p, "iccf") != NULL)
               p = sponsor < 0
                  ? N_("type  :help sponsor<Enter>    for information ")
                  : N_("type  :help register<Enter>   for information ");
            ei (STRSTR(p, "Orphans") != NULL)
               p = N_("menu  Help->Sponsor/Register  for information    ");
         }
         if (*p != ZERO)
            do_intro_line(row, (CS)_(p), i == 2);
         ++row;
      }
   }

   // Make the wait-return message appear just below the text.
   if (colon)
      msgRowG = row;
}

private void
do_intro_line(int row, CS mesg, int add_version){
   Byte vers[20];
   // Center the message horizontally.
   int col = eeglStrSize(mesg);
   if (add_version) {
      STRCPY(vers, mediumVersion);
      if (highest_patch()) {
         // Check for 9.9x or 9.9xx, alpha/beta version
         if (SAFE_isalpha((int)vers[3])) {
            int len = (SAFE_isalpha((int)vers[4])) ? 5 : 4;
            sprintf((char *)vers + len, ".%d%s", highest_patch(), mediumVersion + len);
         } else
            sprintf((char *)vers + 3, ".%d", highest_patch());
      }
      col += (int)STRLEN(vers);
   }
   col = (topframeG->width - col) / 2;
   if (col < 0)
      col = 0;

   // Split up in parts to highlight <> items differently.
   int l;
   for (CS p = mesg; *p != ZERO; p += l) {
      int clen = 0;
      for (l = 0; p[l] != ZERO
             && (l == 0 || (p[l] != '<' && p[l - 1] != '>')); ++l
      ){
         clen += bookPtr2Cells(p + l);
         l += utfCharLen(p + l) - 1;
      }
      drawTextLen(p, l, row, col + firstPor->portalCol, *p == '<' ? getDecoFlags(HLF_8) : 0);
      col += clen;
   }

   // Add the version number to the version line.
   if (add_version)
      drawText(vers, row, col + firstPor->portalCol, 0);
}

// ":intro": clear screen, display intro screen and wait for return.
void
c_intro(Invocation* invo UNUSED){
   screenclear();
   draw_tabpanel();
   intro_message(true);
   wait_return(true);
}

//}}}

// Values for edit_type.
#define EDIT_NONE   0       // no edit type yet
#define EDIT_FILE   1       // file name argument[s] given, use argument list
#define EDIT_STDIN  2       // read file from stdin
#define EDIT_TAG    3       // tag name argument given, use tagname
#define EDIT_QF     4       // start in quickfix mode

private void mainerr(Unt, CS);
private void earlyArgScan(MainParams* paramsP);
private void init0(void);
private void init1(OUT MainParams*);
private int libMain(void);
#ifndef NO_EEGL_MAIN
private void usage(void);
private void parseCommandName(MainParams *paramsP);
private void scanCommandLineArgs(MainParams *paramsP);
private void check_tty(MainParams *paramsP);
private void readStdin(void);
private void createPortals(MainParams *paramsP);
private void editBuffers(MainParams* paramsP, CS cwd);
private void executePreCommands(MainParams *paramsP);
private void exeCommands(MainParams *paramsP);
private void sourceStartupScripts(MainParams *paramsP);
private void check_swap_exists_action(void);
private void set_progpath(CS argv0);
#endif

// Different types of error messages.
private CS main_errors[] = {
    N_("Unknown option argument"),
#define ME_UNKNOWN_OPTION   0
    N_("Too many edit arguments"),
#define ME_TOO_MANY_ARGS    1
    N_("Argument missing after"),
#define ME_ARG_MISSING      2
    N_("Garbage after option argument"),
#define ME_GARBAGE          3
    N_("Too many \"+command\", \"-c command\" or \"--comm command\" arguments"),
#define ME_EXTRA_CMD        4
    N_("Invalid argument for"),
#define ME_INVALID_ARG      5
};


// It is defined when NO_EEGL_MAIN is defined, but then it's empty.
int
libMain(void) {
#ifndef NO_EEGL_MAIN
   //Decide about portal layout for diff mode after reading init.vim.
   if (paramsP.diff_mode && paramsP.portalLayout == 0) {
      if (diffopt_horizontal())
         paramsP.portalLayout = WIN_HOR;   // use horizontal split
      else
         paramsP.portalLayout = WIN_VER;   // use vertical split
   }

   //Recovery mode without a file name
   if (recoveryModeG && paramsP.fname == NULL) {
      mch_exit(0);
   }

   //Set a few option defaults after reading .vimrc files: @shellpipe and @shellredir.
   optInit1();
   TIME_MSG("inits 1");

   //"-n" argument: Disable swap file by setting 'updatecount' to 0.
   //Note that this overrides anything from a vimrc file.
   if (paramsP.no_swap_file)
      { swapEnabledG = false; }

   //Read in registers, history etc, but not marks, from the eeglinfo file.
   //This is where v:oldfiles gets filled.
   if (p_eeglinfo) {
      read_eeglinfo(NULL, EIF_WANT_INFO | EIF_GET_OLDFILES);
      TIME_MSG("reading eeglinfo");
   }
   // It's better to make v:oldfiles an empty list than NULL.
   if (get_EeglVar_list(VV_OLDFILES) == NULL)
      {   set_EeglVar_list(VV_OLDFILES, list_alloc()); }

   //"-q errorfile": Load the error file now.
   //If the error file can't be read, exit before doing anything else.
   if (paramsP.edit_type == EDIT_QF) {
      if (paramsP.use_ef)
         optChangeStringOptionDirect(S"errorfile", paramsP.use_ef, 0, SID_CARG);
      eeSnprintf(IObuff, IOSIZE, "cfile %s", p_ef);
      if (llInitFromFile(NULL, p_ef, curBook->o.errorFormat, true, IObuff) < 0) {
         out_char('\n');
         mch_exit(3);
      }
      TIME_MSG("reading errorfile");
   }

   //Start putting things on the screen.
   //Scroll screen down before drawing over it
   //Clear screen now, so file message will not be cleared.
   starting = NO_BOOKS;
   no_wait_return = false;
   msg_scroll = false;

   if (wayland_init_client(wayland_display_name) == OK) {
      TIME_MSG("connected to Wayland display");

      if (wayland_cb_init(p_wse) == OK)
         TIME_MSG("setup Wayland clipboard");
   }
   //If "-" argument given: Read file from stdin. Do this before starting Raw mode, because it may 
   //change things that the writing end of the pipe doesn't like, e.g., in case stdin and stderr
   //are the same terminal: "cat | eegl -". Using autocommands here may cause trouble...
   if (paramsP.edit_type == EDIT_STDIN && !recoveryModeG)
      readStdin();

   // When switching screens and something caused a message from a vimrc
   // script, need to output an extra newline on exit.
   if ((anyEmsgG || msg_didout) && *termCodesG[KS_TI] != ZERO && paramsP.edit_type != EDIT_STDIN)
      newlineOnExitG = true;

   //When done something that is not allowed or given an error message call wait_return(). This 
   //must be done before starttermcap(), because it may switch to another screen. It must be done
   //after termSetMode(TMODE_RAW), because we want to react on a single key stroke.
   //Call termSetMode and starttermcap here, so the KS_KS and KS_TI may be defined by 
   //termInitTerminfo()
   termSetMode(TMODE_RAW);
   TIME_MSG("setting raw mode");

   if (need_wait_return || msg_didany) {
      wait_return(true);
      TIME_MSG("waiting for return");
   }

   starttermcap();       // start termcap if not done by wait_return()
   TIME_MSG("start termcap");

   setmouse();            // may start using the mouse
   if (scroll_region)
      scroll_region_reset();      // In case visibleRowsG changed
   scroll_start();   // may scroll the screen to the right position

   screenclear();         // clear screen
   TIME_MSG("clearing screen");

   no_wait_return = true;

   // Create the requested number of portals and edit buffers.
   // Also does recovery if "recoveryModeG" set.
   createPortals(&paramsP);
   TIME_MSG("opening buffers");

   // clear v:swapcommand
   set_EeglVar_string(VV_SWAPCOMMAND, NULL, -1);

   applyAutocomms(EVENT_BUFENTER, NULL, NULL, false, curBook);
   TIME_MSG("BufEnter autocommands");
   setpcmark();

   // When started with "-q errorfile" jump to first error now.
   if (paramsP.edit_type == EDIT_QF) {
      llJump(NULL, 0, 0, false);
      TIME_MSG("jump to first error");
   }

   // If opened more than one portal, start editing files in the other portals.
   editBuffers(&paramsP, start_dir);
   eeglFree(start_dir);

   if (paramsP.diff_mode) {
      // set options in each portal for "eegldiff".
      Portal* port;
      FOR_ALL_PORTALS(port)
         diff_win_options(port, true);
   }

   //Shorten any of the filenames, but only when absolute.
   shorten_fnames(false);

   //Need to jump to the tag before executing the '-c command'. Makes "eegl -c '/return' -t main" work
   if (paramsP.tagname != NULL) {
      swap_exists_did_quit = false;

      eeSnprintf(IObuff, IOSIZE, "ta %s", paramsP.tagname);
      executeCommLine(IObuff);
      TIME_MSG("jumping to tag");

      // If the user doesn't want to edit the file then we quit here.
      if (swap_exists_did_quit)
         exitEegl(1);
   }

   // Execute any "+", "-c" and "-S" arguments.
   if (paramsP.n_commands > 0)
      exeCommands(&paramsP);

   // Must come before the may_req_ calls.
   starting = 0;

   // Must be done before redrawing, puts a few characters on the screen.
   check_terminal_behavior();

   isRedrawingDisabledG = 0;
   redraw_all_later(UPD_NOT_VALID);
   no_wait_return = false;

   // 'autochdir' has been postponed
   DO_AUTOCHDIR;

   //Requesting the termresponse is postponed until here, so that a "-c q"
   //argument doesn't make it appear in the shell Eegl was started from.
   may_req_termresponse();

   set_EeglVar_nr(VV_EE_DID_ENTER, 1L);
   applyAutocomms(EVENT_EEGLENTER, NULL, NULL, false, curBook);
   TIME_MSG("EeglEnter autocommands");

   //Adjust default register name for "unnamed" in 'clipboard'. Can only be
   //done after the clipboard is available and all initial commands that may
   //modify the 'clipboard' setting have run; i.e. just before entering the main loop.
   reset_reg_var();

   // When a startup script or session file setup for diff'ing and
   // scrollbind, sync the scrollbind now.
   if (curPor->o.diff && curPor->o.scrollBind) {
      update_topline();
      check_scrollbind((LineNr)0, 0L);
      TIME_MSG("diff scrollbinding");
   }

   // If ":startinsert" command used, stuff a dummy command to be able to
   // call normalAction(), which will then start Insert mode.
   if (restart_edit != 0)
      stuffcharReadbuff(K_NOP);

   // Redraw at least once, also when 'lazyredraw' is set, to be sure the window title gets updated
   //do_redraw = true;

   TIME_MSG("before starting main loop");

   //Call the main command loop.  This never returns.
   mainLoop(false);

#endif // NO_EEGL_MAIN

   return 0;
}

// Initialization #1 shared by main() and some tests.
void
init0(void) {
   estack_init();
   cmdline_init();
   bookInitGlobalCharTable();

   CS errMsg = inputInitCharLens();
   if (errMsg) {
      emsg(errMsg);
      return;
   }
   //optsInitializeGlobalDefaults();
   evalInitGlobals();   // init global variables

   //Allocate space for the generic buffers (needed for optInit0() and emsg()).
   IObuff = alloc(IOSIZE);
   nameBuffG = alloc(MAXPATHL);
   TIME_MSG("Allocated generic buffers");
}

// Initialization #1 shared by main() and some tests.
void
init1(OUT MainParams* paramsP) {
   //Setup to use the current locale (for ctype() and many other things).
   //NOTE: Translated messages with encodings other than latin1 will not work until 
   //optInit0() has been called!
   init_locale();
   TIME_MSG("locale set");
   
   // Set the default values for the options.
   // First find out the home directory, needed to expand "~" in options.
   init_homedir();      // find real value of $HOME
   TIME_MSG("inits 0");

   swapDirG = fiInitSwapDir((CS)paramsP->argv[0]);

   // Do a first scan of the arguments in "argv[]":
   //   -display or --display
   //   --server...
   //   --socketid
   //   --windowid
   earlyArgScan(paramsP);

   clip_init();      // Initialise clipboard stuff
   TIME_MSG("clipboard setup");

   //Check if we have an interactive window.
   stdout_isatty = (isatty(1) != FAIL);
   TIME_MSG("window checked");

   // Initialize global values of all options
   optInit0();
   
   // Allocate the first portal and book. Can't do anything without it, exit when it fails.
   if (portAllocFirst() == FAIL)
      mch_exit(0);

   init_yank();      // init yank buffers

   alist_init(&argListG);   // Init the argument list to empty.
   argListG.id = 0;

   // set v:lang and v:ctype
   set_lang_var();

   // set v:argv
   set_argv_var(paramsP->argv, paramsP->argc);

   init_signs();
   
   set_internal_string_var(S"g:mapleader", S",");

   // initialize location lists. don't send an error message when memory allocation fails.
   // do it when the user tries to access a location list
   llInitStacksOnce();
}

int
appMain(int argc, char** argv) {
   // Do any system-specific initialisations.  These can NOT use IObuff or nameBuffG.  
   // Thus emsg2() cannot be called!
   mch_early_init();

   // Many variables are in "paramsP" so that we can pass them to invoked functions without a lot 
   // of arguments.  "argc" and "argv" are also copied, so that they can be changed.
   CLEAR_FIELD(paramsP);
   paramsP.argc = argc;
   paramsP.argv = argv;
   paramsP.want_full_screen = true;
   paramsP.use_debug_break_level = -1;
   paramsP.portalCount = UNT;

   autocmd_init();

#ifdef MEM_PROFILE
   atexit(eeMemProfileDump);
#endif

   // Various initializations #0 shared with tests.
   init0();

   // Need to find "--startuptime" and "--log" before actually parsing arguments.
   for (int i = 1; i < argc - 1; ++i) {
      if (caseInsensitiveCompare(argv[i], "--startuptime") == 0 && time_fd == NULL) {
         time_fd = fopen(argv[i + 1], "a");
         TIME_MSG("--- EEGL RISING ---");
      }
      if (caseInsensitiveCompare(argv[i], "--log") == 0)
         ch_logfile((CS)(argv[i + 1]), (CS)"ao");
   }

#ifdef CLEAN_RUNTIMEPATH
   // Need to find "--clean" before actually parsing arguments.
   for (i = 1; i < argc; ++i) {
      if (caseInsensitiveCompare(argv[i], "--clean") == 0) {
          paramsP.clean = true;
          break;
      }
   } 
#endif
   // Various initializations #1 shared with tests.
   init1(OUT &paramsP);

   //Figure out the way to work from the command name argv[0]. "eegldiff" starts diff mode, etc.
   parseCommandName(OUT &paramsP);

   // Process command line arguments. File names are put into the global argument list "argListG"
   scanCommandLineArgs(&paramsP);
   TIME_MSG("parsing arguments");

   // On some systems, when we compile with the GUI, we always use it.  On Mac
   // there is no terminal version, and on Portals we can't fork one off with :gui.
   if (GARGCOUNT > 0) {
      paramsP.fname = alist_name(&GARGLIST[0]);
   }

   TIME_MSG("expanding arguments");

   if (paramsP.diff_mode && paramsP.portalCount == UNT)
      paramsP.portalCount = 0;   // open up to 3 portals

   // Don't redraw until much later.
   ++isRedrawingDisabledG;

   // When listing swap file names, don't do cursor positioning et. al.
   if (recoveryModeG && paramsP.fname == NULL)
      paramsP.want_full_screen = false;

   //uiInit() sets up the terminal (window) for use. This must be done after resetting 
   //fullScreenG, otherwise it may move the cursor. Note that we may use mch_exit() before uiInit()!
   uiInit();
   TIME_MSG("shell init");

   // Print a warning if stdout is not a terminal.
   check_tty(&paramsP);

   if (silentModeG) {
      // Ensure output works usefully without a tty: buffer lines instead of fully buffered.
      virtualBuf = malloc(BUFSIZ);
      setvbuf(stdout, virtualBuf, _IOLBF, BUFSIZ);
   }

   //This message comes before term inits, but after setting "silentModeG"
   //when the input is not a tty. Omit the message with --not-a-term.
   if (GARGCOUNT > 1 && !silentModeG && !is_not_a_term())
      printf((char*)_("%d files to edit\n"), GARGCOUNT);

   initHilite(true); // set the default hilite groups
   drawInit();
   if (paramsP.want_full_screen && !silentModeG) {
      //set terminal name and get terminal capabilities (will set fullScreenG)
      termInitTerminfo(paramsP.term);
      screen_start();      // don't know where cursor is now
      TIME_MSG("Termcap init");
   }

   //Set the default values for the options that use visibleRowsG and visibleColsG.
   ui_get_shellsize();      // inits Rows and Columns
   portalInitSize();
   //Set the @diff option now, so that it can be checked for in an init.vim
   //file. There is no book yet, though.
   if (paramsP.diff_mode)
      diff_win_options(firstPor, false);

   commlineRowG = visibleRowsG - commlineHeightG;
   msgRowG = commlineRowG;
   screenalloc(false);      // allocate screen buffers
   optInit1();
   TIME_MSG("inits 0");

   msg_scroll = true;
   no_wait_return = true;

   TIME_MSG("init hilite");

   termInitProps(true);

   //Set the break level after the terminal is initialized.
   debug_break_level = paramsP.use_debug_break_level;

   //Execute --comm arguments.
   executePreCommands(&paramsP);

   //Source startup scripts.
   sourceStartupScripts(&paramsP);

   return libMain();
}

// Return true when the --not-a-term argument was found.
int
is_not_a_term(void) {
   return paramsP.not_a_term;
}

// Return true when the --not-a-term argument was found or the GUI is in use.
int
is_not_a_term_or_gui(void) {
   return paramsP.not_a_term;
}

#if defined(EXITFREE) || defined(PROTO)
void
free_vbuf(void) {
   if (virtualBuf) {
      setvbuf(stdout, NULL, _IONBF, 0);
      free(virtualBuf);
      virtualBuf = NULL;
   }
}
#endif

// When true in a safe state when starting to wait for a character.
private Boole wasSafeP = false;

// Return whether currently it is safe, assuming it was safe before (high level state didn't change)
private int
isSafeNow(void) {
   return stuff_empty()
      && typeBufG.validLen == 0
      && scriptin[curscript] == NULL
      && !debug_mode
      && !global_busy;
}

// Trigger SafeState if currently in a safe state, that is "safe" is true and there is no typeahead
void
may_trigger_safestate(Boole safe) {
   Boole is_safe = safe && isSafeNow();
   if (wasSafeP != is_safe)
      // Only log when the state changes, otherwise it happens at nearly every key stroke.
      lo(is_safe ? "SafeState: Start triggering" : "SafeState: Stop triggering");
   if (is_safe)
      applyAutocomms(EVENT_SAFESTATE, NULL, NULL, false, curBook);
   wasSafeP = is_safe;
}

// Something changed which causes the state possibly to be unsafe, e.g. a
// character was typed.  It will remain unsafe until the next call to may_trigger_safestate().
void
state_no_longer_safe(CS reason) {
   if (wasSafeP)
      lo("SafeState: reset: %s", reason);
   wasSafeP = false;
}

Boole
get_was_safe_state(void) {
   return wasSafeP;
}

// Invoked when leaving code that invokes callbacks.  Then trigger
// SafeStateAgain, if it was safe when starting to wait for a character.
void
may_trigger_safestateagain(void) {
   if (!wasSafeP)     {
      // If the safe state was reset in state_no_longer_safe(), e.g. because
      // of calling feedkeys(), we check if it's now safe again (all keys were consumed).
      wasSafeP = isSafeNow();
      if (wasSafeP)
         lo("SafeState: undo reset");
   }
   if (wasSafeP) {
      // Only do this message when another message was given, otherwise we get lots of them.
      if ((did_repeated_msg & REPEATED_MSG_SAFESTATE) == 0)    {
         int did = did_repeated_msg;

         lo("SafeState: back to waiting, triggering SafeStateAgain");
         did_repeated_msg = did | REPEATED_MSG_SAFESTATE;
      }
      applyAutocomms(EVENT_SAFESTATEAGAIN, NULL, NULL, false, curBook);
   } else
      lo("SafeState: back to waiting, not triggering SafeStateAgain");
}

//Return true if there is any typeahead, pending operator or command.
int
work_pending(void) {
   return op_pending() || !isSafeNow();
}

//Main loop: Execute Normal mode commands until exiting Eegl.
//Also used to handle commands in the command-line portal, until the portal is closed.
//Also used to handle ":visual" command after ":global": execute Normal mode commands.
void
mainLoop(Boole inCommPort) {  // true when working in the command-line window
   Operator oper;      // operator arguments
   Operator* operPrev = currOperatorG; //operator arguments
   currOperatorG = &oper;

   clear_oparg(OUT &oper);
   while (!inCommPort || commPortResultG == 0) {
      if (stuff_empty()) {
         did_check_timestamps = false;
         if (need_check_timestamps)
            check_timestamps(false);
         if (need_wait_return)   // if wait_return() still needed ...
            wait_return(false);   // ... call it now
      }

      //Reset "gotInterruptG" now that we got back to the main loop.  Except when
      //inside a ":g/pat/comm" command, then the "gotInterruptG" needs to abort the ":g" command.
      //For ":g/pat/vi" we reset "gotInterruptG" when used once.  When used
      //a second time we go back to Ex mode and abort the ":g" command.
      if (gotInterruptG) {
         if (!quitMoreG) {
            (void)vgetc();      // flush all buffers
         }
         gotInterruptG = false;
      }

      //At the toplevel there is no exception handling.  Discard any that
      //may be hanging around (e.g. from "interrupt" at the debug prompt).
      if (did_throw && !ex_normal_busy)
         discard_current_exception();

      msg_scroll = false;
      quitMoreG = false;

      //it's not safe unless may_trigger_safestate_main() is called
      wasSafeP = false;

      //If skip redraw is set (for ":" in wait_return()), don't redraw now.
      //If there is nothing in the stuff_buffer or do_redraw is true, update cursor and redraw.
      if (skip_redraw) {
         skip_redraw = false;
         setcursor();
         cursor_on();
      } ei (do_redraw || stuff_empty()) {
         if (!finish_op && popup_visible
               && !EQUAL_POS(last_cursormoved, curPor->cursor)) {
            if (popup_visible)
               popup_check_cursor_pos();
            last_cursormoved = curPor->cursor;
         }

         // Ensure curPor->topLine and curPor->leftCol are up to date before triggering a 
         // WinScrolled autocommand.
         update_topline();
         validate_cursor();

         if (!finish_op)
            may_trigger_win_scrolled_resized();

         // If nothing is pending and we are going to wait for the user to
         // type a character, trigger SafeState.
         may_trigger_safestate(!op_pending() && restart_edit == 0);

         // Updating diffs from changed() does not always work properly,
         // esp. updating folds.  Do an update just before redrawing if needed.
         if (curtab->diff_update || curtab->diff_invalid) {
            c_diffupdate(NULL);
            curtab->diff_update = false;
         }

         // Scroll-binding for diff mode may have been postponed until
         // here.  Avoids doing it for every change.
         if (diff_need_scrollbind) {
            check_scrollbind((LineNr)0, 0L);
            diff_need_scrollbind = false;
         }
         // Include a closed fold completely in the Visual area.
         foldAdjustVisual();
         //When 'foldclose' is set, apply 'foldlevel' to folds that don't contain the cursor.
         //When 'foldopen' is "all", open the fold(s) under the cursor.
         //This may mark the window for redrawing.
         if (hasAnyFolding(curPor) && !char_avail()) {
            foldCheckClose();
            if (p_fdo & FDO_ALL)
               foldOpenCursor();
         }

         //Before redrawing, make sure topLine is correct, and leftCol
         //if lines don't wrap, and skipCol if lines wrap.
         update_topline();
         validate_cursor();

         if (VIsual_active)
            update_curbuf(UPD_INVERTED); // update inverted part
         ei (mustRedrawG) {
            drawUpdateScreen(0);
         } ei (redrawCommlineG || mustClearCommlineG || redrawModeG)
            showmode();
         redraw_statuslines();
         curBook->lastUsed = eeTime();
         // display message after redraw
         if (msgAfterRedrawG) {
            CS p = copyStr(msgAfterRedrawG);
            //msg_start() will set msgAfterRedrawG to NULL, make a copy first. Don't reset 
            //msgAfterRedrawG, msgDeco_keep() uses it to check for duplicates. Never append this 
            //message to history.
            msg_hist_off = true;
            msgDeco(p, decoAfterRedrawG);
            msg_hist_off = false;
            eeglFree(p);
         }
         if (needFileinfoG) {     // show file info after redraw
            fileinfo(false, true, false);
            needFileinfoG = false;
         }

         emsg_on_display = false;   // can delete error message now
         anyEmsgG = false;
         msg_didany = false;      // reset lines_left in msg_start()
         may_clear_sb_text();   // clear scroll-back text on next msg
         showruler(false);

         setcursor();
         cursor_on();

         do_redraw = false;

         //Now that we have drawn the first screen all the startup stuff
         //has been done, close any file for startup messages.
         if (time_fd != NULL) {
            TIME_MSG("first screen update");
            TIME_MSG("--- EEGL STARTED ---");
            fclose(time_fd);
            time_fd = NULL;
         }
         // After the first screen update may start triggering WinScrolled
         // autocmd events.  Store all the scroll positions and sizes now.
         may_make_initial_scroll_size_snapshot();
      }

      // May request the keyboard protocol state now.
      may_send_t_RK();

      // Update cursWant if setCursWant has been set.
      // Postponed until here to avoid computing virtCol too often.
      update_curswant();

      //May perform garbage collection when waiting for a character, but
      //only at the very toplevel. Otherwise we may be using a List or Dict internally somewhere.
      //"may_garbage_collect" is reset in vgetc() which is invoked through normalAction().
      may_garbage_collect = (!inCommPort);
      //get and execute a normal mode command.
      if (term_use_loop()
          && oper.opTy == OP_NOP && oper.regname == ZERO
          && !VIsual_active
          && !skip_term_loop
      ){
         //If terminal_loop() returns OK we got a key that is handled in Normal mode.  With FAIL 
         //we first need to position the cursor and the screen needs to be redrawn.
         if (terminal_loop(true) == OK) {
            normalAction(OUT &oper, true);
         }
      } else {
         skip_term_loop = false;
         normalAction(&oper, true);
      }
   }

   currOperatorG = operPrev;
}

// Exit properly. This is the only way to exit Eegl after startup has succeeded. We are certain 
// to exit here, no way to abort it.
void
exitEegl(int exitval) {
   isExitingG = true;
   lo("Exiting...");

   set_EeglVar_type(VV_EXITING, VAR_NUMBER);
   set_EeglVar_nr(VV_EXITING, exitval);

   //Position the cursor on the last screen line, below all the text
   if (!is_not_a_term_or_gui())
      windgoto((int)visibleRowsG - 1, 0);

   //Invoked all deferred functions in the function stack.
   invoke_all_defer();

   //Optionally print hashtable efficiency.
   hash_debug_results();

   if (v_dying <= 1) {
      Portal      *wp;
      int      unblock = 0;

      // Trigger BufWinLeave for all portals, but only once per buffer.
      Tab* next_tp;
      for (Tab* tp = firstTabG; tp; tp = next_tp) {
         next_tp = tp->next;
         FOR_ALL_PORTALS_IN_TAB(tp, wp) {
            if (wp->book == NULL || !bookIsValid(wp->book))
               // Autocmd must have close the buffer already, skip.
               continue;
            Book* book = wp->book;
            if (CHANGEDTICK(book) != -1) {
               BookRef bookRef;

               bookStoreInRef(OUT &bookRef, book);
               applyAutocomms(EVENT_BUFWINLEAVE, book->currFileName, book->currFileName, false, book);
               if (bookRefValid(&bookRef))
                  CHANGEDTICK(book) = -1;  // note we did it already

               // start all over, autocommands may mess up the lists
               next_tp = firstTabG;
               break;
            }
         }
      }

      Book* book;
      // Trigger BufUnload for loaded books
      FOR_ALL_BOOKS(book) {
         if (book->mem.mfile) {
            BookRef bookRef;
            bookStoreInRef(OUT &bookRef, book);
            applyAutocomms(EVENT_BUFUNLOAD, book->currFileName, book->currFileName, false, book);
            if (!bookRefValid(&bookRef))
               // autocmd deleted the book
               break;
         }
      }

      // deathtrap() blocks autocommands, but we do want to trigger EeglLeavePre.
      if (areAutocommsBlocked()) {
         unblock_autocmds();
         ++unblock;
      }
      applyAutocomms(EVENT_EEGLLEAVEPRE, NULL, NULL, false, curBook);
      if (unblock)
         block_autocmds();
   }

   if (
#ifdef EXITFREE
       entered_free_all_mem == false &&
#endif
         p_eeglinfo
   )
      // Write out the registers, history, marks etc, to the eeglinfo file
      write_eeglinfo(NULL, false);

   if (v_dying <= 1) {
      int unblock = 0;

      // deathtrap() blocks autocommands, but we do want to trigger EeglLeave.
      if (areAutocommsBlocked()) {
          unblock_autocmds();
          ++unblock;
      }
      applyAutocomms(EVENT_EEGLLEAVE, NULL, NULL, false, curBook);
      if (unblock)
         block_autocmds();
   }

   if (anyEmsgG) {
      // give the user a chance to read the (error) message
      no_wait_return = false;
      wait_return(false);
   }

   // Position the cursor again, the autocommands may have moved it
   if (!is_not_a_term_or_gui())
      windgoto((int)visibleRowsG - 1, 0);

   job_stop_on_exit();
   cs_end();
   if (garbage_collect_at_exit)
      garbage_collect(false);

   mch_exit(exitval);
}

//Get the name of the display, before gui_prepare() removes it from
//argv[].  Used for the xterm-clipboard display.
//
//Also find the --server... arguments and --socketid and --windowid
private void
earlyArgScan(MainParams* paramsP) {
   int      argc = paramsP->argc;
   char   **argv = paramsP->argv;
   int      i;

   for (i = 1; i < argc; i++) {
      if (STRCMP(argv[i], "--") == 0)
          break;
   }
}

#ifndef NO_EEGL_MAIN

// Get an (optional) count for a Eegl argument.
private int
getNumericArg(
   CS p,       // pointer to argument
   int* idx,       // index in argument, is incremented
   int def       // default value
){
   if (eeIsDigit(p[*idx])) {
      def = atoi((char *)&(p[*idx]));
      while (eeIsDigit(p[*idx]))
         *idx = *idx + 1;
   }
   return def;
}

//Check for: [eegl|view][diff]  (sort of)
//If the next characters are "view" we start in readonly mode.
//If the next characters are "diff" or "eegldiff" we start in diff mode.
private void
parseCommandName(MainParams* paramsP) {
   CS initstr;

   initstr = fiGetShortFiName((CS)paramsP->argv[0]);

   set_EeglVar_string(VV_PROGNAME, initstr, -1);
   set_progpath((CS)paramsP->argv[0]);

   if (STRNICMP(initstr, "view", 4) == 0) {
      optSetByName(S"modifiable", optBoole(false), SET_GLOBAL);
      curBook->o.modifiable = false;
      swapEnabledG = true;         // don't update very often
      initstr += 4;
   } ei (STRNICMP(initstr, "eegl", 3) == 0)
      initstr += 3;

   // Catch "eegldiff" and "viewdiff".
   if (caseInsensitiveCompare(initstr, "diff") == 0) {
      paramsP->diff_mode = true;
   }
}

//{{{ Scan the command line arguments.
private void
scanCommandLineArgs(MainParams *paramsP) {
   int      argc = paramsP->argc;
   char   **argv = paramsP->argv;
   int      argv_idx;      // index in argv[n][]
   int      had_minmin = false;   // found "--" argument
   int      want_argument;      // option argument with argument
   int      c;
   CS text = NULL;

   --argc;
   ++argv;
   argv_idx = 1;       // active option letter is argv[0][argv_idx]
   while (argc > 0) {
      //"+" or "+{number}" or "+/{pat}" or "+{command}" argument.
      if (argv[0][0] == '+' && !had_minmin) {
         if (paramsP->n_commands >= MAX_ARG_CMDS)
            mainerr(ME_EXTRA_CMD, NULL);
         argv_idx = -1;       // skip to next argument
         if (argv[0][1] == ZERO)
            paramsP->commands[paramsP->n_commands++] = (CS)"$";
         else
            paramsP->commands[paramsP->n_commands++] = (CS)&(argv[0][1]);
      }
      // Optional argument.
      ei (argv[0][0] == '-' && !had_minmin) {
         want_argument = false;
         c = argv[0][argv_idx++];
         switch (c) {
         case ZERO:      // "eegl -"  read from stdin. "ex -" silent mode
            if (paramsP->edit_type != EDIT_NONE)
               mainerr(ME_TOO_MANY_ARGS, (CS)argv[0]);
            paramsP->edit_type = EDIT_STDIN;
            read_cmd_fd = 2;   // read from stderr instead of stdin
            argv_idx = -1;      // skip to next argument
            break;

         case '-': 
            //"--" don't take any more option arguments
            //"--help" give help message
            //"--version" give version message
            //"--clean" clean context
            //"--literal" take files literally
            //"--startuptime fname" write timing info
            //"--log fname" start logging early
            //"--nofork" don't fork
            //"--not-a-term" don't warn for not a term
            //"--gui-dialog-file fname" write dialog text
            //"--ttyfail" exit if not a term
            //"--noplugin[s]" skip plugins
            //"--comm <command>" execute command before init.vim
            if (caseInsensitiveCompare(argv[0] + argv_idx, "help") == 0)
                usage();
            ei (caseInsensitiveCompare(argv[0] + argv_idx, "version") == 0) {
                visibleColsG = 80;
                info_message = true; // use mch_msg(), not mch_errmsg()
                list_version();
                msg_putchar('\n');
                msg_didout = false;
                mch_exit(0);
            } ei (STRNICMP(argv[0] + argv_idx, "clean", 5) == 0) {
                paramsP->altInitFile = (CS)"DEFAULTS";
                paramsP->clean = true;
                optChangeAndReportError(S"eeglinfofile", optStr("NONE"), SET_GLOBAL);
            } ei (STRNICMP(argv[0] + argv_idx, "literal", 7) == 0) {
            } ei (STRNICMP(argv[0] + argv_idx, "nofork", 6) == 0) {
            } ei (STRNICMP(argv[0] + argv_idx, "not-a-term", 10) == 0)
                paramsP->not_a_term = true;
            ei (STRNICMP(argv[0] + argv_idx, "gui-dialog-file", 15) == 0) {
                want_argument = true;
                argv_idx += 15;
            } ei (STRNICMP(argv[0] + argv_idx, "ttyfail", 7) == 0)
                paramsP->tty_fail = true;
            ei (STRNICMP(argv[0] + argv_idx, "comm", 3) == 0) {
                want_argument = true;
                argv_idx += 3;
            } ei (STRNICMP(argv[0] + argv_idx, "startuptime", 11) == 0) {
                want_argument = true;
                argv_idx += 11;
            } ei (STRNICMP(argv[0] + argv_idx, "log", 3) == 0) {
                want_argument = true;
                argv_idx += 3;
            } ei (STRNICMP(argv[0] + argv_idx, "serverlist", 10) == 0)
                ; // already processed -- no arg
            ei (STRNICMP(argv[0] + argv_idx, "servername", 10) == 0
                   || STRNICMP(argv[0] + argv_idx, "serversend", 10) == 0
            ){
               // already processed -- snatch the following arg
               if (argc > 1) {
                  --argc;
                  ++argv;
               }
            } else {
               if (argv[0][argv_idx])
                  mainerr(ME_UNKNOWN_OPTION, (CS)argv[0]);
               had_minmin = true;
            }
            if (!want_argument)
               argv_idx = -1;   // skip to next argument
            break;

         case 'b':      // "-b" binary mode. binary file I/O
            OptionChange cha = (OptionChange){.newVal = optBoole(true), .setScope = SET_LOCAL,
               .ref = (OptionRef){.tag = OPTION_BOOLE, .boole = &curBook->o.binary}
            };
            optSetBinary(&cha);
            break;

         case 'h':      // "-h" give help message
            usage();
            break;

         case 'M':      // "-M"  no changes or writing of files
            // FALLTHROUGH
         case 'm':
            p_modifiable = false;
            break;

         case 'n':      // "-n" no swap file
            paramsP->no_swap_file = true;
            break;

         case 'p':      // "-p[N]" open N tabs
            // default is 0: open portal for each file
            paramsP->portalCount = getNumericArg((CS)argv[0], &argv_idx, 0);
            paramsP->portalLayout = WIN_TABS;
            break;

         case 'o':      // "-o[N]" open N horizontal split windows
            // default is 0: open window for each file
            paramsP->portalCount = getNumericArg((CS)argv[0], &argv_idx, 0);
            paramsP->portalLayout = WIN_HOR;
            break;

         case 'O':   // "-O[N]" open N vertical split windows
            // default is 0: open window for each file
            paramsP->portalCount = getNumericArg((CS)argv[0], &argv_idx, 0);
            paramsP->portalLayout = WIN_VER;
            break;

         case 'q':      // "-q" QuickFix mode
            if (paramsP->edit_type != EDIT_NONE) 
               mainerr(ME_TOO_MANY_ARGS, (CS)argv[0]);
            paramsP->edit_type = EDIT_QF;
            if (argv[0][argv_idx]) {     // "-q{errorfile}"
               paramsP->use_ef = (CS)argv[0] + argv_idx;
               argv_idx = -1;
            } ei (argc > 1)      // "-q {errorfile}"
               want_argument = true;
            break;

         case 'R':      // "-R" readonly mode, equivalent to "-m" or "-M"
            p_modifiable = false;
            break;

         case 'r':      // "-r" recovery mode
         case 'L':      // "-L" recovery mode
            recoveryModeG = 1;
            break;

         case 's':
            // "-s {scriptin}" read from script file
            want_argument = true;
            break;

         case 't':      // "-t {tag}" or "-t{tag}" jump to tag
            if (paramsP->edit_type != EDIT_NONE)
               mainerr(ME_TOO_MANY_ARGS, (CS)argv[0]);
            paramsP->edit_type = EDIT_TAG;
            if (argv[0][argv_idx]) {     // "-t{tag}"
               paramsP->tagname = (CS)argv[0] + argv_idx;
               argv_idx = -1;
            } else            // "-t {tag}"
                want_argument = true;
            break;

         case 'D':      // "-D"      Debugging
            paramsP->use_debug_break_level = 9999;
            break;
         case 'd':      // "-d"      'diff'
            paramsP->diff_mode = true;
            break;
         case 'V':      // "-V{N}"   Verbose level
            // default is 10: a little bit verbose
            p_verbose = getNumericArg((CS)argv[0], &argv_idx, 10);
            if (argv[0][argv_idx] != ZERO) {
               optChangeAndReportError(
                  S"verbosefile", optStr((CS)argv[0] + argv_idx), SET_GLOBAL 
               );
               argv_idx = (int)STRLEN(argv[0]);
            }
            break;


         case 'w': // "-w {scriptout}"   write to script
            want_argument = true;
            break;

         case 'Y':      // "-Y" don't connect to Wayland compositor
            wayland_no_connect = true;
            break;

         case 'c':      // "-c{command}" or "-c {command}" execute command
            if (argv[0][argv_idx] != ZERO) {
               if (paramsP->n_commands >= MAX_ARG_CMDS)
                  mainerr(ME_EXTRA_CMD, NULL);
               paramsP->commands[paramsP->n_commands++] = (CS)argv[0] + argv_idx;
               argv_idx = -1;
               break;
            }
            // FALLTHROUGH
         case 'S':      // "-S {file}" execute Vimscript
         case 'i':      // "-i {eeglinfo}" use for eeglinfo
         case 'T':      // "-T {terminal}" terminal name
         case 'u':      // "-u {vimrc}" Eegl inits file
         case 'W':      // "-W {scriptout}" overwrite
            want_argument = true;
            break;

         default:
            mainerr(ME_UNKNOWN_OPTION, (CS)argv[0]);
         }

         // Handle option arguments with argument.
         if (want_argument) {
            // Check for garbage immediately after the option letter.
            if (argv[0][argv_idx] != ZERO)
                mainerr(ME_GARBAGE, (CS)argv[0]);

            --argc;
            if (argc < 1 && c != 'S')  // -S has an optional argument
                mainerr_arg_missing((CS)argv[0]);
            ++argv;
            argv_idx = -1;

            switch (c) {
            case 'c':   // "-c {command}" execute command
            case 'S':   // "-S {file}" execute Vim script
               if (paramsP->n_commands >= MAX_ARG_CMDS)
                  mainerr(ME_EXTRA_CMD, NULL);
               if (c == 'S') {
                  Arr(char) fName;

                  if (argc < 1)
                     //"-S" without argument: use default session file name.
                     fName = SESSION_FILE;
                  ei (argv[0][0] == '-') {
                     //"-S" followed by another option: use default session file name.
                     fName = SESSION_FILE;
                     ++argc;
                     --argv;
                  } else
                     fName = argv[0];
                  text = alloc(STRLEN(fName) + 4);
                  sprintf((char *)text, "so %s", fName);
                  paramsP->cmds_tofree[paramsP->n_commands] = true;
                  paramsP->commands[paramsP->n_commands++] = text;
               } else
                  paramsP->commands[paramsP->n_commands++] = (CS)argv[0];
               break;

            case '-':
               if (argv[-1][2] == 'c') {
                  // "--comm {command}" execute command
                  if (paramsP->n_pre_commands >= MAX_ARG_CMDS)
                     mainerr(ME_EXTRA_CMD, NULL);
                  paramsP->pre_commands[paramsP->n_pre_commands++] = (CS)argv[0];
               }

               // "--startuptime <file>" already handled
               // "--log <file>" already handled
               break;

            case 'q':   // "-q {errorfile}" QuickFix mode
               paramsP->use_ef = (CS)argv[0];
               break;

            case 'i':   // "-i {eeglinfo}" use for eeglinfo
               optChangeAndReportError(S"eeglinfofile", optStr(argv[0]), SET_GLOBAL);
               break;

            case 's':   // "-s {scriptin}" read from script file
               if (scriptin[0] != NULL) {
scripterror:
                  mch_errmsg(_("Attempt to open script file again: \""));
                  mch_errmsg(argv[-1]);
                  mch_errmsg(" ");
                  mch_errmsg(argv[0]);
                  mch_errmsg("\"\n");
                  mch_exit(2);
               } 
               if ((scriptin[0] = fopen(argv[0], READBIN)) == NULL) {
                  mch_errmsg(_("Cannot open for reading: \""));
                  mch_errmsg(argv[0]);
                  mch_errmsg("\"\n");
                  mch_exit(2);
               }
               if (save_typebuf() == FAIL)
                  mch_exit(2);   // out of memory
               break;

            case 't':   // "-t {tag}"
                paramsP->tagname = (CS)argv[0];
                break;

            case 'T':   // "-T {terminal}" terminal name
               //The -T term argument is always available and when
               //HAVE_TERMLIB is supported it overrides the environment variable TERM.
               paramsP->term = (CS)argv[0];
               break;

            case 'u':   // "-u {vimrc}" Eegl inits file
                paramsP->altInitFile = (CS)argv[0];
                break;

            case 'w': // "-w {scriptout}" append to script file
            case 'W': // "-W {scriptout}" overwrite script file
               if (scriptout)
                  goto scripterror;
               if ((scriptout = fopen(argv[0], c == 'w' ? APPENDBIN : WRITEBIN)) == NULL) {
                  mch_errmsg(_("Cannot open for script output: \""));
                  mch_errmsg(argv[0]);
                  mch_errmsg("\"\n");
                  mch_exit(2);
               }
               break;

            }
         }
      }
      // File name argument.
      else {
         argv_idx = -1;       // skip to next argument

         // Check for only one type of editing.
         if (paramsP->edit_type != EDIT_NONE && paramsP->edit_type != EDIT_FILE)
            mainerr(ME_TOO_MANY_ARGS, (CS)argv[0]);
         paramsP->edit_type = EDIT_FILE;

         // Add the file to the global argument list.
         if (ga_grow(&argListG.al_ga, 1) == FAIL)
            mch_exit(2);
         text = copyStr((CS)argv[0]); 
         if (
            paramsP->diff_mode && mch_isdir(text) 
            && GARGCOUNT > 0 
            && !mch_isdir(alist_name(&GARGLIST[0]))
         ) {
            CS concattedFnames = 
               concat_fnames(text, fiGetShortFiName(alist_name(&GARGLIST[0])), true);
            if (concattedFnames) {
               eeglFree(text);
               text = concattedFnames;
            }
         }

         arglistIngest(&argListG, text, 2); // add buffer number now and use curBook
      }

      //If there are no more letters after the current "-", go to next
      //argument.  argv_idx is set to -1 when the current argument is to be skipped.
      if (argv_idx <= 0 || argv[0][argv_idx] == ZERO) {
          --argc;
          ++argv;
          argv_idx = 1;
      }
   }

   // If there is a "+123" or "-c" command, set v:swapcommand to the first one.
   if (paramsP->n_commands > 0) {
      text = alloc(STRLEN(paramsP->commands[0]) + 3);
      sprintf((char *)text, ":%s\r", paramsP->commands[0]);
      set_EeglVar_string(VV_SWAPCOMMAND, text, -1);
      eeglFree(text);
   }
}

//}}}

// Print a warning if stdout is not a terminal.
private void
check_tty(MainParams* paramsP) {
   int      input_isatty;      // is active input a terminal?

   input_isatty = mch_input_isatty();
   if (paramsP->want_full_screen && (!stdout_isatty || !input_isatty) && !paramsP->not_a_term) {
      if (!stdout_isatty)
         mch_errmsg(_("Eegl: Warning: Output is not to a terminal\n"));
      if (!input_isatty)
         mch_errmsg(_("Eegl: Warning: Input is not from a terminal\n"));
      out_flush();
      if (paramsP->tty_fail && (!stdout_isatty || !input_isatty))
         exit(1);
      if (scriptin[0] == NULL)
         ui_delay(2005L, true);
      TIME_MSG("Warning delay");
   }
}

// Read text from stdin.
private void
readStdin(void) {
   // When getting the ATTENTION prompt here, use a dialog
   swap_exists_action = SEA_DIALOG;

   no_wait_return = true;
   int i = msg_didany;
   bookSetBooklisted(true);

   // Create memfile and read from stdin.
   (void)bookOpenFromInvo(true, NULL, 0);

   no_wait_return = false;
   msg_didany = i;
   TIME_MSG("reading stdin");

   check_swap_exists_action();

   // Dup stdin from stderr to read commands from, so that shell commands work.
   // TODO: why is this needed, even though readfile() has done this?
   close(0);
   (void)dup(2);
}

// Create the requested number of portals and edit buffers in them.
// Also do recovery if "recoveryModeG" set.
private void
createPortals(MainParams *paramsP) {
   int dorewind;

   //Create the number of portals that was requested.
   if (paramsP->portalCount == UNT)   // was not set
      paramsP->portalCount = 1;
   if (paramsP->portalCount == 0)
      paramsP->portalCount = GARGCOUNT;
   if (paramsP->portalCount > 1) {
   // Don't change the portals if there was a command in .vimrc that already split some portals
   if (paramsP->portalLayout == 0)
       paramsP->portalLayout = WIN_HOR;
   if (paramsP->portalLayout == WIN_TABS) {
       paramsP->portalCount = portMakeTabs(paramsP->portalCount);
       TIME_MSG("making tabs");
   } ei (firstPor->next == NULL) {
       paramsP->portalCount = makePortals(paramsP->portalCount, paramsP->portalLayout == WIN_VER);
       TIME_MSG("making portals");
   } else
      paramsP->portalCount = portCount();
   } else
      paramsP->portalCount = 1;

   if (recoveryModeG) {         // do recover
      msg_scroll = true;      // scroll message up
      ml_recover(true);
      if (curBook->mem.mfile == NULL) // failed
          exitEegl(1);
   } else {
      //Open a buffer for portals that don't have one yet. Commands in the .vimrc might have loaded 
      //a file or split the window. Watch out for autocommands that delete a portal. Don't execute 
      //Win/Buf Enter/Leave autocommands here
      ++autocmd_no_enter;
      ++autocmd_no_leave;
      dorewind = true;
      for (int done = 0; done < 1000; done++) {
         if (dorewind) {
            if (paramsP->portalLayout == WIN_TABS)
               gotoTabById(1);
            else
               curPor = firstPor;
         } ei (paramsP->portalLayout == WIN_TABS) {
            if (curtab->next == NULL)
               break;
            gotoTabById(0);
         } else {
            if (curPor->next == NULL)
               break;
            curPor = curPor->next;
         }
         dorewind = false;
         curBook = curPor->book;
         if (curBook->mem.mfile == NULL) {
            if (foldLevelStart >= 0)
               curPor->o.foldLevel = foldLevelStart;
            // When getting the ATTENTION prompt here, use a dialog
            swap_exists_action = SEA_DIALOG;

            bookSetBooklisted(true);

            // create memfile, read file
            (void)bookOpenFromInvo(false, NULL, 0);

            if (swap_exists_action == SEA_QUIT) {
               if (gotInterruptG || onlyOnePortal()) {
                  // abort selected or quit and only one portal
                  anyEmsgG = false;   // avoid hit-enter prompt
                  exitEegl(1);
               }
               //We can't close the window, it would disturb what happens next. Clear the file 
               //name and set the arg index to -1 to delete it later.
               setfname(curBook, NULL, NULL, false);
               curPor->argListInd = -1;
               swap_exists_action = SEA_NONE;
            } else
               handle_swap_exists(NULL);
            dorewind = true;      // start again
         }
         ui_breakcheck();
         if (gotInterruptG) {
            (void)vgetc();   // only break the file loading, not the rest
            break;
         }
      }
      if (paramsP->portalLayout == WIN_TABS)
         gotoTabById(1);
      else
         curPor = firstPor;
      curBook = curPor->book;
      --autocmd_no_enter;
      --autocmd_no_leave;
   }
}

//If opened more than one portal, start editing files in the other portals. makePortals() has 
//already opened the portals.
private void
editBuffers(MainParams* paramsP, CS cwd) {        // current working dir
   int      arg_idx;      // index in argument list
   int      advance = true;

   //Don't execute Win/Buf Enter/Leave autocommands here
   ++autocmd_no_enter;
   ++autocmd_no_leave;

   // When argListInd is -1 remove the window (see createPortals()).
   if (curPor->argListInd == -1) {
      closePortal(curPor, true);
      advance = false;
   }

   arg_idx = 1;
   for (Unt i = 1; i < paramsP->portalCount; ++i) {
      if (cwd)
         mch_chdir(cwd);
      // When argListInd is -1 remove the window (see createPortals()).
      if (curPor->argListInd == -1) {
         ++arg_idx;
         closePortal(curPor, true);
         advance = false;
         continue;
      }
      if (advance) {
         if (paramsP->portalLayout == WIN_TABS) {
            if (curtab->next == NULL)   // just checking
               break;
            gotoTabById(0);
         } else {
            if (!curPor->next)   // just checking
               break;
            enterPortal(curPor->next, false);
         }
      }
      advance = true;

      // Only open the file if there is no file in this window yet (that can
      // happen when .vimrc contains ":sall").
      if (curBook == firstPor->book || curBook->fullFileName == NULL) {
         curPor->argListInd = arg_idx;
         // Edit file from arg list, if there is one.  When "Quit" selected
         // at the ATTENTION prompt close the window.
         swap_exists_did_quit = false;
         (void)startEditingFile(0, 
            arg_idx < GARGCOUNT ? alist_name(&GARGLIST[arg_idx]) : NULL,
            NULL, NULL, ECMD_LASTL, ECMD_HIDE, curPor
         );
         if (swap_exists_did_quit) {
            // abort or quit selected
            if (gotInterruptG || onlyOnePortal()) {
               //abort selected and only one portal
               anyEmsgG = false;  //avoid hit-enter prompt
               exitEegl(1);
            }
            closePortal(curPor, true);
            advance = false;
         }
         if (arg_idx == GARGCOUNT - 1)
            arg_had_last = true;
         ++arg_idx;
      }
      ui_breakcheck();
      if (gotInterruptG) {
         (void)vgetc();   // only break the file loading, not the rest
         break;
      }
   }

   if (paramsP->portalLayout == WIN_TABS)
      gotoTabById(1);
   --autocmd_no_enter;

   // make the first portal the current one
   Portal* po = firstPor;
   // Avoid making a preview portal the current one.
   while (po->isPreview) {
      po = po->next;
      if (!po) {
         po = firstPor;
         break;
      }
   }
   enterPortal(po, false);

   --autocmd_no_leave;
   TIME_MSG("editing files in windows");
   if (paramsP->portalCount > 1 && paramsP->portalLayout != WIN_TABS)
      portEqualizeHeight(curPor, false, EAD_BOTH);   // adjust heights
}

// Execute the commands from --comm arguments "comms[cnt]".
private void
executePreCommands(MainParams* paramsP) {
   Arr(CS) comms = paramsP->pre_commands;
   int      cnt = paramsP->n_pre_commands;
   int      i;
   ESTACK_CHECK_DECLARATION;

   if (cnt <= 0)
      return;

   curPor->cursor.lnum = 0; // just in case..
   estack_push(ETYPE_ARGS, (CS)_("pre-vimrc command line"), 0);
   ESTACK_CHECK_SETUP;
   scriptPosG.sid = SID_CMDARG;
   for (i = 0; i < cnt; ++i) {
      executeCommLine(comms[i]);
   } 
   ESTACK_CHECK_NOW;
   estack_pop();
   scriptPosG.sid = 0;
   TIME_MSG("--comm commands");
}

// Execute "+", "-c" and "-S" arguments.
private void
exeCommands(MainParams* paramsP) {
   ESTACK_CHECK_DECLARATION;

   // We start commands on line 0, make "eegl +/pat file" match a
   // pattern on line 1.  But don't move the cursor when an autocommand with g`" was used.
   msg_scroll = true;
   if (paramsP->tagname == NULL && curPor->cursor.lnum <= 1)
      curPor->cursor.lnum = 0;
   estack_push(ETYPE_ARGS, S"command line", 0);
   ESTACK_CHECK_SETUP;
   scriptPosG.sid = SID_CARG;
   scriptPosG.seq = 0;
   for (int i = 0; i < paramsP->n_commands; ++i) {
      executeCommLine(paramsP->commands[i]);
      if (paramsP->cmds_tofree[i])
          eeglFree(paramsP->commands[i]);
   }
   ESTACK_CHECK_NOW;
   estack_pop();
   scriptPosG.sid = 0;
   if (curPor->cursor.lnum == 0)
      curPor->cursor.lnum = 1;

   msg_scroll = false;

   // When started with "-q errorfile" jump to first error again.
   if (paramsP->edit_type == EDIT_QF)
      llJump(NULL, 0, 0, false);
   TIME_MSG("executing command arguments");
}

// Source startup scripts.
private void
sourceStartupScripts(MainParams* paramsP) {
   // If -u argument given, use only the initializations from that file and nothing else.
   if (paramsP->altInitFile) {
      if (STRCMP(paramsP->altInitFile, "DEFAULTS") == 0) {
         if (scriptRunFile((CS)EE_DEFAULTS_FILE, NULL) != OK)
            emsg(_(e_failed_to_source_defaults));
      } ei (STRCMP(paramsP->altInitFile, "NONE") == 0 || STRCMP(paramsP->altInitFile, "NORC") == 0) {
      } else {
         if (scriptRunFile(paramsP->altInitFile, NULL) != OK)
            showErrFmtMsg(_(e_cannot_read_from_str_2), paramsP->altInitFile);
      }
   } ei (!silentModeG) {
      //Get system wide defaults, if the file name is defined.
      (void)scriptRunFile(INIT_FILE, NULL);
      //(void)scriptRunFile(FILETYPES_FILE, NULL);
   }
   TIME_MSG(S"sourcing init.vim file(s)");
}

#endif  // NO_EEGL_MAIN

// Give an error message main_errors["n"] and exit.
private void
mainerr(
   Unt n,   // one of the ME_ defines
   NULLABLE CS str   // extra argument
){
   reset_signals();      // kill us with CTRL-C here, if you like

   mch_errmsg(longVersion);
   mch_errmsg("\n");
   mch_errmsg(_(main_errors[n]));
   if (str != NULL) {
      mch_errmsg(": \"");
      mch_errmsg((char *)str);
      mch_errmsg("\"");
   }
   mch_errmsg(_("\nMore info with: \"eegl -h\"\n"));

   mch_exit(1);
}

void
mainerr_arg_missing(CS str) {
   mainerr(ME_ARG_MISSING, str);
}

#ifndef NO_EEGL_MAIN
// print a message with three spaces prepended and '\n' appended.
private void
main_msg(CS s) {
   mch_msg("   ");
   mch_msg(s);
   mch_msg("\n");
}

CS
mainProgramVersion() {
   return programVersion;
}

// Print messages for "eegl -h" or "eegl --help" and exit.
private void
usage(void) {
   int      i;
   static CS use[] = {
      N_("[file ..]       edit specified file(s)"),
      N_("-               read text from stdin"),
      N_("-t tag          edit file where tag is defined"),
      N_("-q [errorfile]  edit file with first error")
   };

   reset_signals();      // kill us with CTRL-C here, if you like

   mch_msg(longVersion);
   mch_msg(_("\n\nUsage:"));
   for (i = 0; ; ++i) {
      mch_msg(_(" eegl [arguments] "));
      mch_msg(_(use[i]));
      if (i == ARRAY_LENGTH(use) - 1)
         break;
      mch_msg(_("\n   or:"));
   }

   mch_msg(_("\n\nArguments:\n"));
   main_msg(_("--\t\t\tOnly file names after this"));
   main_msg(_("-v\t\t\tVi mode (like \"vi\")"));
   main_msg(_("-e\t\t\tEx mode (like \"ex\")"));
   main_msg(_("-E\t\t\tImproved Ex mode"));
   main_msg(_("-s\t\t\tSilent (batch) mode (only for \"ex\")"));
   main_msg(_("-d\t\t\tDiff mode (like \"eegldiff\")"));
   main_msg(_("-R\t\t\tReadonly mode (like \"view\")"));
   main_msg(_("-m\t\t\tModifications (writing files) not allowed"));
   main_msg(_("-M\t\t\tModifications in text not allowed"));
   main_msg(_("-b\t\t\tBinary mode"));
   main_msg(_("-l\t\t\tLisp mode"));
   main_msg(_("-C\t\t\tCompatible with Vi: 'compatible'"));
   main_msg(_("-N\t\t\tNot fully Vi compatible: 'nocompatible'"));
   main_msg(_("-V[N][fname]\t\tBe verbose [level N] [log messages to fname]"));
   main_msg(_("-D\t\t\tDebugging mode"));
   main_msg(_("-n\t\t\tNo swap file, use memory only"));
   main_msg(_("-r\t\t\tList swap files and exit"));
   main_msg(_("-r (with file name)\tRecover crashed session"));
   main_msg(_("-L\t\t\tSame as -r"));
   main_msg(_("-T <terminal>\tSet terminal type to <terminal>"));
   main_msg(_("--not-a-term\t\tSkip warning for input/output not being a terminal"));
   main_msg(_("--ttyfail\t\tExit if input or output is not a terminal"));
   main_msg(_("-u <vimrc>\t\tUse <vimrc> instead of any .vimrc"));
   main_msg(_("--noplugin\t\tDon't load plugin scripts"));
   main_msg(_("-p[N]\t\tOpen N tabs (default: one for each file)"));
   main_msg(_("-o[N]\t\tOpen N windows (default: one for each file)"));
   main_msg(_("-O[N]\t\tLike -o but split vertically"));
   main_msg(_("+\t\t\tStart at end of file"));
   main_msg(_("+<lnum>\t\tStart at line <lnum>"));
   main_msg(_("--comm <command>\tExecute <command> before loading any vimrc file"));
   main_msg(_("-c <command>\t\tExecute <command> after loading the first file"));
   main_msg(_("-S <session>\t\tSource file <session> after loading the first file"));
   main_msg(_("-s <scriptin>\tRead Normal mode commands from file <scriptin>"));
   main_msg(_("-w <scriptout>\tAppend all typed commands to file <scriptout>"));
   main_msg(_("-W <scriptout>\tWrite all typed commands to file <scriptout>"));
   main_msg(_("-Y\t\t\tDo not connect to Wayland compositor"));
   main_msg(_("--remote <files>\tEdit <files> in a Eegl server if possible"));
   main_msg(_("--remote-silent <files>  Same, don't complain if there is no server"));
   main_msg(_("--remote-wait <files>  As --remote but wait for files to have been edited"));
   main_msg(_("--remote-wait-silent <files>  Same, don't complain if there is no server"));
   main_msg(_("--remote-tab[-wait][-silent] <files>  As --remote but use tab per file"));
   main_msg(_("--remote-send <keys>\tSend <keys> to a Eegl server and exit"));
   main_msg(_("--remote-expr <expr>\tEvaluate <expr> in a Eegl server and print result"));
   main_msg(_("--serverlist\t\tList available Eegl server names and exit"));
   main_msg(_("--servername <name>\tSend to/become the Eegl server <name>"));
   main_msg(_("--startuptime <file>\tWrite startup timing messages to <file>"));
   main_msg(_("--log <file>\t\tStart logging to <file> early"));
   main_msg(_("-i <eeglinfo>\t\tUse <eeglinfo> instead of .eeglinfo"));
   main_msg(_("--clean\t\t'nocompatible', Eegl defaults, no plugins, no eeglinfo"));
   main_msg(_("-h  or  --help\tPrint Help (this message) and exit"));
   main_msg(_("--version\t\tPrint version information and exit"));

   mch_exit(0);
}

// Check the result of the ATTENTION dialog:
// When "Quit" selected, exit Eegl.
// When "Recover" selected, recover the file.
private void
check_swap_exists_action(void) {
   if (swap_exists_action == SEA_QUIT)
      exitEegl(1);
   handle_swap_exists(NULL);
}

#endif // NO_EEGL_MAIN


void __attribute__((noinline))
__bp() { // breakpoints for debugger
   ;
}

#if !defined(NO_EEGL_MAIN)
private void
set_progpath(CS argv0) {
   CS val = argv0;
   Byte buf[MAXPATHL + 1];
   Byte linkBuf[MAXPATHL + 1];
   Long len = readlink("/proc/self/exe", OUT (char*)linkBuf, MAXPATHL);
   if (len > 0) {
      linkBuf[len] = ZERO;
      val = linkBuf;
   }

   if (strIsRelative(val) 
         && fiGetShortFiName(val) != val && eeFullFileName(val, OUT buf, MAXPATHL, true) != FAIL
   )
      val = buf;

   set_EeglVar_string(VV_PROGPATH, val, -1);
}

#endif // NO_EEGL_MAIN
