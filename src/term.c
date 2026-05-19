//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## term.c: terminal and pseudo-teletype functions

#include "eegl.h"
#include <termcap.h>

typedef struct termios TermIos;

//{{{forward declarations

private int may_adjust_key_for_ctrl(int modifiers, Unt key);

//}}}
//{{{terminal state:

//A few Linux systems define outfuntype in termcap.h to be used as the third
//argument for tputs().
# ifdef HAVE_OUTFUNTYPE
#define TPUTSFUNCAST (outfuntype)
# else
#define TPUTSFUNCAST (int (*)(int))
# endif


//Size of the buffer used for tgetent().  Unfortunately this is largely
//undocumented, some systems use 1024.  Using a buffer that is too small
//causes a buffer overrun and a crash.  Use the maximum known value to stay
//on the safe side.
#define TBUFSZ 2048      //buffer size for termcap entry

// start of keys that are not directly used by Eegl but can be mapped
#define BT_EXTRA_KEYS   0x101

private void gatherTermLeaders(void);
private void req_more_codes_from_term(void);
private void got_code_from_term(CS code, int len);
private void handleUnansweredRequests(void);
private void del_termcode_idx(int idx);
private Unt find_term_bykeys(CS src);
private void accept_modifiers_for_function_keys(void);
private Unt may_remove_shift_modifier(Unt modifiers, Unt key);

#if 0  // Change to 1 to enable ch_log() calls for termresponse debugging.
# define DEBUG_TERMRESPONSE
# define LOG_TR1(str) \
      lo("TermResp: %s " str, \
         must_redraw == UPD_NOT_VALID ? "NV" \
         : must_redraw == UPD_CLEAR ? "CL" : "  ")
# define LOG_TRN(fmt,...) \
      lo("TermResp: %s " fmt, \
         must_redraw == UPD_NOT_VALID ? "NV" \
         : must_redraw == UPD_CLEAR ? "CL" : "  ", __VA_ARGS__)
#else
# define LOG_TR1(str) do { /**/ } while (0)
# define LOG_TRN(fmt,...) do { /**/ } while (0)
#endif

private CS invoke_tgetent(CS , CS );

typedef enum {
   STATUS_GET,      // send request when switching to RAW mode
   STATUS_SENT,   // did send request, checking for response
   STATUS_GOT,      // received response
   STATUS_FAIL      // timed out
} RequestProgress;

typedef struct {
   RequestProgress       tr_progress;
   Tyme          tr_start;   // when request was sent, -1 for never
} TermRequest;

#define TERMREQUEST_INIT {STATUS_GET, -1}

// Request Terminal Version status:
private TermRequest crv_status = TERMREQUEST_INIT;

// Request Cursor position report:
private TermRequest u7_status = TERMREQUEST_INIT;

// Request xterm compatibility check:
private TermRequest xcc_status = TERMREQUEST_INIT;

// Request foreground color report:
private TermRequest rfg_status = TERMREQUEST_INIT;
private int fg_r = 0;
private int fg_g = 0;
private int fg_b = 0;
private int bg_r = 255;
private int bg_g = 255;
private int bg_b = 255;

// Request background color report:
private TermRequest backgroundColorRequestS = TERMREQUEST_INIT;

// Request cursor blinking mode report:
private TermRequest cursorBlinkingRequestS = TERMREQUEST_INIT;

// Request cursor style report:
private TermRequest cursorStyleRequestS = TERMREQUEST_INIT;

// Request window's position report:
private TermRequest winPositionRequestS = TERMREQUEST_INIT;

private TermRequest *all_termrequests[] = {
   &crv_status,
   &u7_status,
   &xcc_status,
   &rfg_status,
   &backgroundColorRequestS,
   &cursorBlinkingRequestS,
   &cursorStyleRequestS,
   &winPositionRequestS
};

// The t_8u code may default to a value but get reset when the term response is
// received.  To avoid redrawing too often, only redraw when t_8u is not reset
// and it was supposed to be written.  Unless t_8u was set explicitly.
// FALSE -> don't output t_8u yet
// MAYBE -> tried outputting t_8u while FALSE
// OK    -> can write t_8u
int write_t_8u_state = FALSE;


extern char *UP, *BC, PC; // in termcap.h

# define TGETENT(b, t)   tgetent((char *)(b), (char *)(t))
private CS eeTgetstr(CS s, Byte **pp);

private int focus_state = MAYBE; // TRUE if the Eegl window has focus

// When the cursor shape was detected these values are used:
// 1: block, 2: underline, 3: vertical bar
private int initial_cursor_shape = 0;

// The blink flag from the style response may be inverted from the actual
// blinking state, xterm XORs the flags.
private int initial_cursor_shape_blink = FALSE;

// The blink flag from the blinking-cursor mode response
private int initial_cursor_blink = FALSE;
//}}}
//{{{terminfo: The builtin terminfo entries.

//The entries are also included, the system terminfo may be incomplete and a few Eegl-specific
//entries are added.
//
//The builtin entries can be accessed with "builtin_xterm", "builtin_debug", etc.
//
//Each terminfo is a list of TinfoEntry.
//
//Entries marked with "guessed" may be wrong.
typedef struct {
   CS value; // value
   Unt c;   // either a KS_xxx code (>= 0), or a K_xxx code.
} TinfoEntry;

// Additions for using the Kitty keyboard protocol.
private TinfoEntry builtin_kitty[] = {SMAP1((CS),
   "\033[=1;1u", KS_CTI, //t_TI enables the kitty keyboard protocol.
   "\033[?u", KS_CRK,    //t_RK requests the kitty keyboard protocol state
   "\033[>4;m\033[=0;1u", KS_CTE //t_TE also disables modifyOtherKeys, because t_TI from
                                       //xterm may already have been used.
)};

// Additions for using the RGB colors and terminal font
private TinfoEntry builtin_rgb[] = {SMAP1((CS),
   //These are printf strings, not terminal codes.
   "\033[38;2;%lu;%lu;%lum", (Unt)KS_8F,
   "\033[48;2;%lu;%lu;%lum", (Unt)KS_8B,
   "\033[58;2;%lu;%lu;%lum", (Unt)KS_8U
)};

private TinfoEntry special_term[] = {
   //These are printf strings, not terminal codes.
   {S"\033[%dm", (Unt)KS_CF},
   {null, (Unt)KS_NAME}  // end marker
};

//Return TRUE if "name" looks like some xterm name.
//This matches "xterm.*", thus "xterm-256color", "xterm-kitty", etc.
//Do not consider "xterm-kitty" an xterm, it is not fully xterm compatible,
//using the "xterm-kitty" terminfo entry should work better.
//Seiichi Sato mentioned that "mlterm" works like xterm.
private int
isEeglXterm(CS name) {
   if (!name)
      return FALSE;
   return ((STRNICMP(name, "xterm", 5) == 0
                 && STRNICMP(name, "xterm-kitty", 11) != 0)
      || STRNICMP(name, "nxterm", 6) == 0
      || STRNICMP(name, "kterm", 5) == 0
      || STRNICMP(name, "mlterm", 6) == 0
      || STRNICMP(name, "rxvt", 4) == 0
      || STRNICMP(name, "screen.xterm", 12) == 0
      || STRCMP(name, "builtin_xterm") == 0);
}

//}}}
//{{{functions for controlling the terminal

//NOTE: padding and variable substitution is not performed
UiColor
termgui_mch_get_rgb(UiColor color) {
   return color;
}

// DEFAULT_TERM is used, when no terminal is specified with -T option or $TERM.
#define DEFAULT_TERM S"ansi"

//termCodeS contains currently used terminal output strings.
//The values can be changed by setting the option with the same name.
//Nulls are not allowed! Only empty strings
CS termCodeS[KS_LAST + 1];

private int  needToGatherTermLeaders = FALSE; // need to fill termLeaderG[]
private Byte termLeaderG[256 + 1];         // for check_termcode()
private int  check_for_codes = FALSE;         // check for key code response

// Structure and table to store terminal features that can be detected by
// querying the terminal.  Either by inspecting the termresponse or a more
// specific request.  Besides this there are:
typedef struct {
   CS name;
   int setByTermResponse;
   int status;
} TermProp;

// Values for status.
#define TPR_UNKNOWN     'u'
#define TPR_YES         'y'
#define TPR_NO          'n'
#define TPR_MOUSE_SGR   's'   // use "sgr" for 'ttymouse'

// can request the cursor style without messing up the display
#define TPR_CURSOR_STYLE   0
// can request the cursor blink mode without messing up the display
#define TPR_CURSOR_BLINK   1
// can set the underline color with t_8u without resetting other colors
#define TPR_UNDERLINE_RGB  2
// mouse support - TPR_MOUSE_XTERM, TPR_MOUSE_XTERM2 or TPR_MOUSE_SGR
#define TPR_MOUSE          3
// term response indicates kitty
#define TPR_KITTY          4
// table size
#define TPR_COUNT          5

private TermProp term_props[TPR_COUNT];

// Initialize the term_props table.
// When "all" is FALSE only set those that are detected from the version response.
void
init_term_props(int all) {
   term_props[TPR_CURSOR_STYLE].name = S"cursor_style";
   term_props[TPR_CURSOR_STYLE].setByTermResponse = FALSE;
   term_props[TPR_CURSOR_BLINK].name = S"cursor_blink_mode";
   term_props[TPR_CURSOR_BLINK].setByTermResponse = FALSE;
   term_props[TPR_UNDERLINE_RGB].name = S"underline_rgb";
   term_props[TPR_UNDERLINE_RGB].setByTermResponse = TRUE;
   term_props[TPR_MOUSE].name = S"mouse";
   term_props[TPR_MOUSE].setByTermResponse = TRUE;
   term_props[TPR_KITTY].name = S"kitty";
   term_props[TPR_KITTY].setByTermResponse = FALSE;

   for (int i = 0; i < TPR_COUNT; ++i) {
      if (all || term_props[i].setByTermResponse)
         term_props[i].status = TPR_UNKNOWN;
   }
}

void
f_terminalprops(Var *argvars UNUSED, Var *returnVar) {
   allocReturnDict(returnVar);
   for (Unt i = 0; i < TPR_COUNT; ++i) {
      Byte value[2] = { term_props[i].status, ZERO };
      bagAddString(returnVar->bag, term_props[i].name, value);
   }
}

// Apply entries from a builtin termcap.
private void
applyBuiltinCapability(Arr(TinfoEntry) entries, int len) {
   for (TinfoEntry *p = entries; p < entries + len && p->c != BT_EXTRA_KEYS; ++p) {
      if ((int)p->c >= 0) {  // KS_xx entry
         termCodeS[p->c] = p->value;
      } else {
         Byte  name[2];
         name[0] = KEY2TERMCAP0((int)p->c);
         name[1] = KEY2TERMCAP1((int)p->c);
         add_termcode(name, (CS)p->value, false);
      }
   }
}

private CS key_names[] = {SMAP((CS),
   //Do those ones first, both may cause a screen redraw.
   "Co",
   //disabled, because it switches termguicolors, but that is noticeable and confuses users "RGB",
   "ku", "kd", "kr", "kl", "#2", "#4", "%i", "*7",
   "k1", "k2", "k3", "k4", "k5", "k6"
   ), SMAP((CS),
   "k7", "k8", "k9", "k;", "F1", "F2", "%1", "&8", "kb", "kI", "kD", "kh"
   ), SMAP((CS),
   "@7", "kP", "kN", "K1", "K3", "K4", "K5", "kB", "PS", "PE"
)};

//Get the terminfo entries we need with tgetstr(), tgetflag() and tgetnum().
//"invoke_tgetent()" must have been called before.
//If "*height" or "*width" are nonzero then use the "li" and "col" entries to get their value.
private void
get_term_entries(OUT int* height, OUT int* width) {
   static struct {
      CS name;  //capability name
      Unt dest; //index in termCodeS[]
   } entryNames[] = { SMAP1((CS),
      "ce", KS_CE,  "al", KS_AL,  "AL", KS_CAL, "dl", KS_DL,  "DL", KS_CDL,  "cs", KS_CS,
      "cl", KS_CL,  "cd", KS_CD,  "vi", KS_VI,  "ve", KS_VE,  "mb", KS_MB,   "me", KS_ME, 
      "mr", KS_MR,  "md", KS_MD,  "se", KS_SE,  "so", KS_SO,  "ZH", KS_CZH,  "ZR", KS_CZR
      ), SMAP1((CS),
      "ue", KS_UE,  "us", KS_US,  "Ce", KS_UCE, "Cs", KS_UCS, "Us", KS_USS, "ds", KS_DS,
      "Ds", KS_CDS, "Te", KS_STE, "Ts", KS_STS, "cm", KS_CM,  "sr", KS_SR,  "RI", KS_CRI,
      "vb", KS_VB,  "ks", KS_KS,  "ke", KS_KE,  "ti", KS_TI,  "te", KS_TE 
      ), SMAP1((CS),
      "TI", KS_CTI, "RK", KS_CRK, "TE", KS_CTE, "bc", KS_BC,  "Sb", KS_CSB, "Sf", KS_CSF,
      "AB", KS_CAB, "AF", KS_CAF, "AU", KS_CAU, "le", KS_LE,  "nd", KS_ND,  "op", KS_OP,  
      "RV", KS_CRV, "XM", KS_CXM, "vs", KS_VS,  "VS", KS_CVS, "IS", KS_CIS, "IE", KS_CIE 
      ), SMAP1((CS),
      "SC", KS_CSC, "EC", KS_CEC, "ts", KS_TS,  "fs", KS_FS,  "WP", KS_CWP, "WS", KS_CWS, 
      "SI", KS_CSI, "EI", KS_CEI, "u7", KS_U7,  "RF", KS_RFG, "RB", KS_RBG, "8f", KS_8F,
      "8b", KS_8B,  "8u", KS_8U,  "BE", KS_CBE, "BD", KS_CBD, "ST", KS_CST, "RT", KS_CRT 
      ), SMAP1((CS),
      "Si", KS_SSI, "Ri", KS_SRI, "CF", KS_CF
   )};
   static Byte tstrbuf[TBUFSZ];
   CS tp = tstrbuf;

   // get output strings
   for (Unt i = 0; i < ARRAY_LENGTH(entryNames); ++i) {
      if (termCodeS[entryNames[i].dest] == Em) {
         termCodeS[entryNames[i].dest] = eeTgetstr(entryNames[i].name, &tp);
      }
   }
   for (Unt i = 0; i < ARRAY_LENGTH(termCodeS); i++) {
      if (!termCodeS[i])
         termCodeS[i] = Em;
   }

   // tgetflag() returns 1 if the flag is present, 0 if not and
   // possibly -1 if the flag doesn't exist.
   if (termCodeS[KS_MS] == Em && tgetflag("ms") > 0)
      termCodeS[KS_MS] = S"y";
   if (termCodeS[KS_XS] == Em && tgetflag("xs") > 0)
      termCodeS[KS_XS] = S"y";
   if (termCodeS[KS_XN] == Em && tgetflag("xn") > 0)
      termCodeS[KS_XN] = S"y";
   if (termCodeS[KS_DB] == Em && tgetflag("db") > 0)
      termCodeS[KS_DB] = S"y";
   if (termCodeS[KS_DA] == Em && tgetflag("da") > 0)
      termCodeS[KS_DA] = S"y";
   if (termCodeS[KS_UT] == Em && tgetflag("ut") > 0)
      termCodeS[KS_UT] = S"y";
   if (termCodeS[KS_XON] == Em && tgetflag("xo") > 0)
      termCodeS[KS_XON] = S"y";

   // get key codes
   for (Unt i = 0; i < ARRAY_LENGTH(key_names); ++i) {
      if (find_termcode(key_names[i]) == NULL) {
         CS p = TGETSTR(key_names[i], &tp);

         // if cursor-left == backspace, ignore it (televideo 925)
         if (p && (*p != Ctrl_H || key_names[i][0] != 'k' || key_names[i][1] != 'l'))
            add_termcode(key_names[i], p, false);
      }
   }

   if (*height == 0)
      *height = tgetnum("li");
   if (*width == 0)
      *width = tgetnum("co");

   BC = tgetstr("bc", (char**)&tp);
   UP = tgetstr("up", (char**)&tp);
   CS p = TGETSTR("pc", &tp);
   if (p)
      PC = *p;
}

//Set terminal options for terminal "term".
//Return OK if terminal 'term' was found in a termcap, FAIL otherwise.
//
//While doing this, until ttest(), some options may be NULL, be careful.
int
set_termname(CS termName) {
   int termcap_cleared = FALSE;
   int width = 0, height = 0;
   CS errorMsg = NULL;

   // In silect mode (ex -s) we don't use the 'term' option.
   if (silentModeG)
      return OK;

   // Use external terminfo
   Byte tbuf[TBUFSZ];
   
   // If the external terminfo does not have a matching entry, try the builtin ones.
   if ((errorMsg = invoke_tgetent(tbuf, termName)) == NULL) {
      if (!termcap_cleared) {
         termcap_cleared = TRUE;
      }

      get_term_entries(OUT &height, OUT &width);
   }

   applyBuiltinCapability(builtin_kitty, ARRAY_LENGTH(builtin_kitty));
   accept_modifiers_for_function_keys();

   //There is no good way to detect that the terminal supports RGB colors. Since these termcap
   //entries are non-standard anyway we might as well add them.  But not when one of them was
   //already set.
   if (termCodeS[KS_8F] == Em && termCodeS[KS_8B] == Em && termCodeS[KS_8U] == Em)
      applyBuiltinCapability(builtin_rgb, ARRAY_LENGTH(builtin_rgb));
   if (termCodeS[KS_CF] == Em)
      applyBuiltinCapability(special_term, ARRAY_LENGTH(special_term));

   //special: There is no info in the termcap about whether the cursor
   //positioning is relative to the start of the screen or to the start of the
   //scrolling region. We just guess here. Only msdos pcterm is known to do it relative.
   if (STRCMP(termName, "pcterm") == 0)
      termCodeS[KS_CCS] = S"yes";
   else
      termCodeS[KS_CCS] = Em;

   //Special case: "kitty" may not have a "RV" entry in terminfo, but we need
   //to request the version for several other things to work.
   if (strstr((char *)termName, "kitty") != NULL
         && (termCodeS[KS_CRV] == NULL || *termCodeS[KS_CRV] == ZERO)
   )
      termCodeS[KS_CRV] = S"\033[>c";

   //Any "stty" settings override the default for t_kb from the termcap.
   //This is in os_unix.c, because it depends a lot on the version of unix that is being used.
   //Don't do this when the GUI is active, it uses "t_kb" and "t_kD" directly.
   get_stty();

   //If the termcap has no entry for 'bs' and/or 'del' and the ioctl() also
   //didn't work, use the default CTRL-H
   //The default for t_kD is DEL, unless t_kb is DEL.
   //The copyStr'd strings are probably lost forever, well it's only two
   //bytes.  Don't do this when the GUI is active, it uses "t_kb" and "t_kD" directly.
   {
   CS bs_p = find_termcode(S"kb");
   CS del_p = find_termcode(S"kD");
   if (bs_p == NULL || *bs_p == ZERO)
       add_termcode(S"kb", (bs_p = (CS)CTRL_H_STR), false);
   if ((del_p == NULL || *del_p == ZERO) && (bs_p == NULL || *bs_p != DEL))
       add_termcode(S"kD", (CS)DEL_STR, false);
   }

   term_is_xterm = isEeglXterm(termName);
   // Reset terminal properties that are set based on the termresponse, which
   // will be sent out soon.
   init_term_props(FALSE);

   // If the first number in t_XM is 1006 then the terminal will support SGR mouse reporting.
   if (termCodeS[KS_CXM] != NULL && *termCodeS[KS_CXM] != ZERO) {
      CS p = termCodeS[KS_CXM];

      while (*p != ZERO && !EE_ISDIGIT(*p))
          ++p;
   }

   // Set the 'ttymouse' option to the type of mouse to be used.
   // The termcode for the mouse is added as a side effect in option.c.
   {
   CS p = Em;

   if (!p)
      check_mouse_termcode();   // set mouse termcode anyway
   }

   // First time after setting 'term' a focus event is always reported.
   focus_state = MAYBE;

#ifdef USE_TERM_CONSOLE
   // DEFAULT_TERM indicates that it is the machine console.
   if (STRCMP(termName, DEFAULT_TERM) != 0)
      term_console = FALSE;
   else {
      term_console = TRUE;
   }
#endif

   ttest(TRUE);   // make sure we have a valid set of terminal codes

   fullScreenG = TRUE;      // we can use termcap codes from now on
   LOG_TR1("setting crv_status to STATUS_GET");
   crv_status.tr_progress = STATUS_GET;   // Get terminal version later
   write_t_8u_state = FALSE;

   //Initialize the terminal with the appropriate termcap codes.
   //Set the mouse and window title if possible.
   //Don't do this when starting, need to parse the .vimrc first, because it may redefine t_TI etc.
   if (starting != NO_SCREEN) {
      starttermcap();      // may change terminal mode
      setmouse();      // may start using the mouse
   }

   //display initial screen after ttest() checking. jw.
   if (width <= 0 || height <= 0) {
      //termcap failed to report size set defaults, in case ui_get_shellsize() also fails
      width = 80;
      height = 24;       // most terminals are 24 lines
   }
   set_shellsize(width, height, FALSE);   // may change visibleRowsG
   if (starting != NO_SCREEN) {
      if (scroll_region)
         scroll_region_reset();      // In case visibleRowsG changed
      check_map_keycodes();   // check mappings for terminal codes used

      {
         Book* buf;
         AutocommSave aco;

         // Execute the TermChanged autocommands for each buffer that is loaded.
         FOR_ALL_BOOKS(buf) {
            if (curBook->mem.mfile) {
               auCommPrepareBook(&aco, buf);
               if (curBook == buf) {
                  apply_autocmds(EVENT_TERMCHANGED, NULL, NULL, false, curBook);
                  // restore curPor/curBook and a few other things
                  auCommRestoreBook(&aco);
               }
            }
         }
      }
   }

   may_req_termresponse();

   return OK;
}

#if defined(EXITFREE) || defined(PROTO)

#include <term.h>       // declares cur_term

// If supported, delete "cur_term", which caches terminal related entries.
// Avoid that valgrind reports possibly lost memory.
void
free_cur_term(void) {
   if (cur_term)
      del_curterm(cur_term);
}

#endif

//Call tgetent()
//Return error message if it fails, NULL if it's OK.
private CS
invoke_tgetent(CS tbuf, CS terminalName) {
   //Note: Valgrind may report a leak here, because the library keeps one text buffer around that
   //we can't ever free.
   int i = TGETENT(tbuf, terminalName);
   if  (i < 0          // -1 is always an error
 # ifdef TGETENT_ZERO_ERR
      || i == 0       // sometimes zero is also an error
 # endif
     ) {

   //On FreeBSD tputs() gets a SEGV after a tgetent() which fails.  Call
   //tgetent() with the always existing "dumb" entry to avoid a crash or hang.
   //(void)TGETENT(tbuf, "dumb");

   if (i < 0)
# ifdef TGETENT_ZERO_ERR
      return _(e_cannot_open_termcap_file);
   if (i == 0)
# endif
       return _(e_terminal_entry_not_found_in_terminfo);
   }
   return NULL;
}

// Some versions of tgetstr() have been reported to return -1 instead of NULL. Fix that here.
private CS
eeTgetstr(CS s, Byte **pp) {
   CS p = TGETSTR(s, pp);
   if (p == (Byte *)-1)
      p = NULL;
   return (CS)p;
}

//Get visibleColsG and visibleRowsG from the termcap. Used after a window signal if the
//ioctl() fails. It doesn't make sense to call tgetent each time if the "co"
//and "li" entries never change. But on some systems this works.
//Errors while getting the entries are ignored.
void
getlinecol(
   Arr(long) cp,  // columns
   Arr(long) rp   // rows
){
   Byte tbuf[TBUFSZ];

   if (termCodeS[KS_NAME] == Em
         || *termCodeS[KS_NAME] == ZERO
         || invoke_tgetent(tbuf, termCodeS[KS_NAME]) != NULL
   )
      return;

   if (*cp == 0)
      *cp = tgetnum("co");
   if (*rp == 0)
      *rp = tgetnum("li");
}

// Get a string entry from the termcap and add it to the list of recognizedCodeS.
// Used for <t_xx> special keys.
// Give an error message for failure when not sourcing. If force given, replace an existing entry.
// Return FAIL if the entry was not found, OK if the entry was added.
int
add_termcap_entry(CS name, int force) {
   CS string;
   Byte tbuf[TBUFSZ];
   Byte tstrbuf[TBUFSZ];
   CS tp = tstrbuf;

   if (!force && find_termcode(name) != NULL)       // it's already there
      return OK;

   CS term = termCodeS[KS_NAME];
   if (term == Em)       // 'term' not defined yet
      return FAIL;

   // Search in external terminfos
   CS errorMsg = invoke_tgetent(tbuf, term);
   if (!errorMsg) {
      string = TGETSTR(name, &tp);
      if (string && *string != ZERO) {
          add_termcode(name, string, false);
          return OK;
      }
   }

   if (SOURCING_NAME == NULL) {
      if (errorMsg)
          emsg(errorMsg);
      else
          showErrFmtMsg(_(e_no_str_entry_in_termcap), name);
   }
   return FAIL;
}

// Set the terminal name and initialize the terminal options.
// If "name" is NULL or empty, get the terminal name from the environment.
// If that fails, use the default terminal name.
void
termInitTerminfo(CS name) {
   for (Unt i = 0; i < ARRAY_LENGTH(termCodeS); i++) {
      termCodeS[i] = Em;
   }

   CS termName = name;
   if (termName && *termName == ZERO)
      termName = null;       // empty name is equal to no name

   if (!termName)
      termName = mch_getenv(S"TERM");
   if (!termName || *termName == ZERO)
      termName = DEFAULT_TERM;
   optChangeStringOptionDirect(S"term", termName, 0, 0);

   // Set the default terminal name.
   optSetStringDefault(S"term", termName);
   optSetStringDefault(S"ttytype", termName);

   // Avoid using "term" here, because the next mch_getenv() may overwrite it.
   set_termname(termName);
}

// The number of calls to ui_write is reduced by using "out_buf".
#define OUT_SIZE   2047

// add one to allow mch_write() to append a ZERO
private Byte out_buf[OUT_SIZE + 1];

private int out_pos = 0;   // number of chars in out_buf

// Since the maximum number of SGR parameters shown as a normal value range is
// 16, the escape sequence length can be 4 * 16 + lead + tail.
#define MAX_ESC_SEQ_LEN   80

//out_flush(): flush the output buffer and redraw the cursor.
void
out_flush(void) {
   if (out_pos == 0)
      return;

   // set out_pos to 0 before ui_write, to avoid recursiveness
   int len = out_pos;
   out_pos = 0;
   ui_write(out_buf, len, FALSE);
   if (ch_log_output != FALSE) {
      out_buf[len] = ZERO;
      lo("raw %s output: \"%s\"", "terminal", out_buf);
      if (ch_log_output == TRUE)
         ch_log_output = FALSE;  // only log once
   }
}

// out_char(c): put a byte into the output buffer.
//      Flush it if it becomes full.
// This should not be used for outputting text on the screen (use functions
// like msg_puts() and screen_putchar() for that).
void
out_char(unsigned c) {
   if (c == '\n')   // turn LF into CR-LF (CRMOD doesn't seem to do this)
      out_char('\r');

   out_buf[out_pos++] = c;

   // For testing we flush each time.
   if (out_pos >= OUT_SIZE || p_wd)
      out_flush();
}

// Output "c" like out_char(), but don't flush when p_wd is set.
private int
out_char_nf(int c) {
   out_buf[out_pos++] = (unsigned)c;

   if (out_pos >= OUT_SIZE)
      out_flush();
   return (unsigned)c;
}

//A never-padding out_str().
//Use this whenever you don't want to run the string through tputs(). tputs() above is harmless,
//but tputs() from the termcap library is likely to strip off leading digits, that it mistakes
//for padding information, and "%i", "%d", etc.
//This should only be used for writing terminal codes, not for outputting normal text (use
//functions like msg_puts() and screen_putchar() for that).
void
out_str_nf(CS s) {
   // avoid terminal strings being split up
   if (out_pos > OUT_SIZE - MAX_ESC_SEQ_LEN)
      out_flush();

   for (CS p = s; *p != ZERO; ++p)
      out_char_nf(*p);

   // For testing we write one string at a time.
   if (p_wd)
      out_flush();
}

//out_str(s): Put a character string a byte at a time into the output buffer.
//Use tputs(), the termcap parser. (jw)
//This should only be used for writing terminal codes, not for outputting
//normal text (use functions like msg_puts() and screen_putchar() for that).
void
out_str(CS s) {
   if (!s || *s == ZERO)
      return;

   //avoid terminal strings being split up
   if (out_pos > OUT_SIZE - MAX_ESC_SEQ_LEN)
      out_flush();
   tputs((char *)s, 1, TPUTSFUNCAST out_char_nf);

   //For testing we write one string at a time.
   if (p_wd)
      out_flush();
}

//cursor positioning using termcap parser. (jw)
void
term_windgoto(int row, int col) {
   OUT_STR(TGOTO(termCodeS[KS_CM], col, row));
}

void
term_cursor_right(int i) {
   OUT_STR(TGOTO(termCodeS[KS_CRI], 0, i));
}

void
term_append_lines(int line_count) {
    OUT_STR(TGOTO(termCodeS[KS_CAL], 0, line_count));
}

void
term_delete_lines(int line_count) {
   OUT_STR(TGOTO(termCodeS[KS_CDL], 0, line_count));
}

void
term_enable_mouse(int enable) {
   int on = enable ? 1 : 0;
   OUT_STR(TGOTO(termCodeS[KS_CXM], 0, on));
}

void
term_set_winpos(int x, int y) {
   // Can't handle a negative value here
   if (x < 0)
      x = 0;
   if (y < 0)
      y = 0;
   OUT_STR(TGOTO(termCodeS[KS_CWP], y, x));
}

// Return TRUE if we can request the terminal for a response.
private int
can_get_termresponse(void) {
    return cur_tmode == TMODE_RAW
       && termcap_active
       && (is_not_a_term() || (isatty(1) && isatty(read_cmd_fd)));
}

// Set "status" to STATUS_SENT.
private void
termrequest_sent(TermRequest* status) {
   status->tr_progress = STATUS_SENT;
   status->tr_start = time(NULL);
}

// Return TRUE if any of the requests are in STATUS_SENT.
private int
termrequest_any_pending(void) {
   Tyme now = time(NULL);

   for (Unt i = 0; i < ARRAY_LENGTH(all_termrequests); ++i) {
      if (all_termrequests[i]->tr_progress == STATUS_SENT) {
         if (all_termrequests[i]->tr_start > 0 && now > 0
               && all_termrequests[i]->tr_start + 2 < now
         )
            // Sent the request more than 2 seconds ago and didn't get a response, assume it failed.
            all_termrequests[i]->tr_progress = STATUS_FAIL;
         else
            return TRUE;
      }
   }
   return FALSE;
}

private int winpos_x = -1;
private int winpos_y = -1;
private int did_request_winpos = 0;

// Try getting the Eegl window position from the terminal. Return OK or FAIL.
int
term_get_winpos(int* x, int* y, Long timeout) {
   int count = 0;
   int prev_winpos_x = winpos_x;
   int prev_winpos_y = winpos_y;

   if (termCodeS[KS_CGP] == Em || !can_get_termresponse())
      return FAIL;
   winpos_x = -1;
   winpos_y = -1;
   ++did_request_winpos;
   termrequest_sent(&winPositionRequestS);
   OUT_STR(termCodeS[KS_CGP]);
   out_flush();

   // Try reading the result for "timeout" msec.
   while (count++ <= timeout / 10 && !gotInterruptG) {
      (void)vpeekc_nomap();
      if (winpos_x >= 0 && winpos_y >= 0) {
         *x = winpos_x;
         *y = winpos_y;
         return OK;
      }
      ui_delay(10L, FALSE);
   }
   // Do not reset "did_request_winpos", if we timed out the response might
   // still come later and we must consume it.

   winpos_x = prev_winpos_x;
   winpos_y = prev_winpos_y;
   if (timeout < 10 && prev_winpos_y >= 0 && prev_winpos_x >= 0) {
      // Polling: return previous values if we have them.
      *x = winpos_x;
      *y = winpos_y;
      return OK;
   }

   return FALSE;
}

void
term_set_winsize(int height, int width) {
   OUT_STR(TGOTO(termCodeS[KS_CWS], width, height));
}

void
term_font(int n) {
   if (termCodeS[KS_CF] != Em) {
      Byte buffer[20];
      SPRINTF(buffer, termCodeS[KS_CF], 9 + n);
      OUT_STR(buffer);
   }
}

private void
term_color(CS s, int n) {
   Byte buffer[20];
   int i = *s == CSI ? 1 : 2;
   // index in s[] just after <Esc>[ or CSI

   // Special handling of 16 colors, because termcap can't handle it
   // Also accept "\e[3%dm", it is sometimes used.
   // Also accept CSI instead of <Esc>[
   if (n >= 8
         && ((s[0] == ESC && s[1] == '[') || (s[0] == CSI && (i = 1) == 1))
         && s[i] != ZERO
         && (STRCMP(s + i + 1, "%p1%dm") == 0 || STRCMP(s + i + 1, "%dm") == 0)
         && (s[i] == '3' || s[i] == '4')
   ) {
      CS format = S"%s%s%%p1%%dm";
      CS lead = i == 2 ? ( S"\033[") : S"\233";
      CS tail = s[i] == '3' ? (n >= 16 ? S"38;5;" : S"9") : (n >= 16 ? S"48;5;" : S"10");

      SPRINTF(buffer, format, lead, tail);
      OUT_STR(TGOTO(buffer, 0, n >= 16 ? n : n - 8));
   } else
      OUT_STR(TGOTO(s, 0, n));
}

void
term_fg_color(int n) {
   // Use "AF" termcap entry if present, "Sf" entry otherwise
   if (termCodeS[KS_CAF] != Em)
      term_color(termCodeS[KS_CAF], n);
   ei (termCodeS[KS_CSF] != Em)
      term_color(termCodeS[KS_CSF], n);
}

void
term_bg_color(int n) {
   // Use "AB" termcap entry if present, "Sb" entry otherwise
   if (termCodeS[KS_CAB] != Em)
      term_color(termCodeS[KS_CAB], n);
   ei (termCodeS[KS_CSB] != Em)
      term_color(termCodeS[KS_CSB], n);
}

void
term_ul_color(int n) {
   if (termCodeS[KS_CAU] != Em)
      term_color(termCodeS[KS_CAU], n);
}


# define RED(rgb)   (((Ulong)(rgb) >> 16) & 0xFF)
# define GREEN(rgb) (((Ulong)(rgb) >>  8) & 0xFF)
# define BLUE(rgb)  (((Ulong)(rgb)      ) & 0xFF)

private void
term_rgb_color(Byte *s, UiColor rgb) {
# define MAX_COLOR_STR_LEN 100
   Byte buffer[MAX_COLOR_STR_LEN];

   if (*s == ZERO)
      return;
   eeSnprintf(buffer, MAX_COLOR_STR_LEN, (char *)s, RED(rgb), GREEN(rgb), BLUE(rgb));
   OUT_STR(buffer);
}

void
term_fgRgb_color(UiColor rgb) {
   if (rgb != INVALCOLOR)
      term_rgb_color(termCodeS[KS_8F], rgb);
}

void
term_bgRgb_color(UiColor rgb) {
   if (rgb != INVALCOLOR)
      term_rgb_color(termCodeS[KS_8B], rgb);
}

void
term_underlRgb_color(UiColor rgb) {
   // If the user explicitly sets t_8u then use it.  Otherwise wait for
   // termresponse to be received, which is when t_8u would be set and a
   // redraw is needed if it was used.
   if (!optWasSet(S"t_8u") && write_t_8u_state != OK)
      write_t_8u_state = MAYBE;
   else
      term_rgb_color(termCodeS[KS_8U], rgb);
}

// Make sure we have a valid set or terminal options. Replace all null entries by Em
void
ttest(int pairs) {
   //MUST have "cm": cursor motion.
   if (termCodeS[KS_CM] == null)
      emsg(_(e_terminal_capability_cm_required));

   //if "cs" defined, use a scroll region, it's faster.
   if (termCodeS[KS_CS])
      scroll_region = TRUE;
   else
      scroll_region = FALSE;

   if (pairs) {
      // optional pairs. TP goes to normal mode for TI (invert) and TB (bold)
      if (termCodeS[KS_ME] == null) {
         termCodeS[KS_MB] = null;
         termCodeS[KS_MD] = null;
         termCodeS[KS_MR] = null;
         termCodeS[KS_ME] = null;
      }
      if (termCodeS[KS_SO] == null || termCodeS[KS_SE] == null) {
         termCodeS[KS_SO] = null;
         termCodeS[KS_SE] = null;
      }
      if (!termCodeS[KS_US] || termCodeS[KS_UE] == null) {
         termCodeS[KS_US] = null;
         termCodeS[KS_UE] = null;
      }
      if (!termCodeS[KS_CZH] || !termCodeS[KS_CZR]) {
         termCodeS[KS_CZH] = null;
         termCodeS[KS_CZR] = null;
      }

      // termCodeS[KS_VE] is needed even though termCodeS[KS_VI] is not defined
      if (!termCodeS[KS_VE])
         termCodeS[KS_VI] = null;

      //if 'mr' or 'me' is not defined, use 'so' and 'se'
      if (!termCodeS[KS_ME]) {
         termCodeS[KS_ME] = termCodeS[KS_SE];
         termCodeS[KS_MR] = termCodeS[KS_SO];
         termCodeS[KS_MD] = termCodeS[KS_SO];
      }

      // if 'so' or 'se' is not defined, use 'mr' and 'me'
      if (!termCodeS[KS_SO]) {
         termCodeS[KS_SE] = termCodeS[KS_ME];
         if (!termCodeS[KS_MR])
            termCodeS[KS_SO] = termCodeS[KS_MD];
         else
            termCodeS[KS_SO] = termCodeS[KS_MR];
      }

      // if 'ZH' or 'ZR' is not defined, use 'mr' and 'me'
      if (!termCodeS[KS_CZH]) {
         termCodeS[KS_CZR] = termCodeS[KS_ME];
         if (!termCodeS[KS_MR])
            termCodeS[KS_CZH] = termCodeS[KS_MD];
         else
            termCodeS[KS_CZH] = termCodeS[KS_MR];
      }

      // "Sb" and "Sf" come in pairs
      if (!termCodeS[KS_CSB] || !termCodeS[KS_CSF]) {
         termCodeS[KS_CSB] = null;
         termCodeS[KS_CSF] = null;
      }

      // "AB" and "AF" come in pairs
      if (!termCodeS[KS_CAB] || !termCodeS[KS_CAF]) {
         termCodeS[KS_CAB] = null;
         termCodeS[KS_CAF] = null;
      }
   }
   needToGatherTermLeaders = TRUE;
}

#if defined(PROTO)
// Represent the given Ulong as individual bytes, with the most significant
// byte first, and store them in dst.
void
add_long_to_buf(Ulong val, CS dst) {
   for (int i = 1; i <= (int)sizeof(Ulong); i++) {
      int shift = 8 * (sizeof(Ulong) - i);
      dst[i - 1] = (Byte) ((val >> shift) & 0xff);
   }
}

//Interpret the next string of bytes in buf as a long integer, with the most
//significant byte first.  Note that it is assumed that buf has been through
//inchar(), so that ZERO and K_SPECIAL will be represented as three bytes each.
//Puts result in val, and returns the number of bytes read from buf
//(between sizeof(Ulong) and 2 * sizeof(Ulong)), or -1 if not enough bytes were present.
private int
get_long_from_buf(CS buffer, Ulong* val) {
   Byte  bytes[sizeof(Ulong)];

   *val = 0;
   int len = get_bytes_from_buf(buffer, bytes, (int)sizeof(Ulong));
   if (len == -1)
      return -1;
   for (int i = 0; i < (int)sizeof(Ulong); i++) {
      int shift = 8 * (sizeof(Ulong) - 1 - i);
      *val += (Ulong)bytes[i] << shift;
   }
   return len;
}
#endif

// Read the next num_bytes bytes from buffer, and store them in bytes.  Assume
// that buffer has been through inchar().   Returns the actual number of bytes used
// from buf (between num_bytes and num_bytes*2), or -1 if not enough bytes were available.
int
get_bytes_from_buf(CS buffer, CS bytes, int num_bytes) {
   int       len = 0;
   int       i;
   Byte  c;

   for (i = 0; i < num_bytes; i++) {
      if ((c = buffer[len++]) == ZERO)
         return -1;
      if (c == K_SPECIAL) {
         if (buffer[len] == ZERO || buffer[len + 1] == ZERO)       // cannot happen?
            return -1;
         if (buffer[len++] == (int)KS_ZERO)
            c = ZERO;
         // else it should be KS_SPECIAL; when followed by KE_FILLER c is
         // K_SPECIAL, or followed by KE_CSI and c must be CSI.
         if (buffer[len++] == (int)KE_CSI)
            c = CSI;
      } ei (c == CSI && buffer[len] == KS_EXTRA && buffer[len + 1] == (int)KE_CSI)
         // CSI is stored as CSI KS_SPECIAL KE_CSI to avoid confusion with
         // the start of a special key, see add_to_input_buf_csi().
         len += 2;
      bytes[i] = c;
   }
   return len;
}

// Check if the new shell size is valid, correct it if it's too small or way too big.
void
check_shellsize(void) {
   // need room for one window and command line
   if (visibleRowsG < minRowsForAllTabs())
      visibleRowsG = minRowsForAllTabs();
   clampScreenSize();

   // make sure these values are not invalid
   if (commlineRowG >= visibleRowsG)
      commlineRowG = visibleRowsG - 1;
   if (msgRowG >= visibleRowsG)
      msgRowG = visibleRowsG - 1;
}

// Limit visibleRowsG and visibleColsG to avoid an overflow in visibleRowsG * visibleColsG.
void
clampScreenSize(void) {
   if (visibleColsG < MIN_COLUMNS)
      visibleColsG = MIN_COLUMNS;
   ei (visibleColsG > 10000)
      visibleColsG = 10000;
   if (visibleRowsG > 1000)
      visibleRowsG = 1000;
}

//Invoked just before the screen structures are going to be (re)allocated.
void
win_new_shellsize(void) {
   static int old_Rows = 0;
   static int old_Columns = 0;
   static int old_coloff = 0;

   if (old_Columns != COLUMNS_WITHOUT_TPL() || old_coloff != TPL_LCOL()) {
      old_Columns = COLUMNS_WITHOUT_TPL();
      old_coloff = TPL_LCOL();

      shell_new_columns();
   }
   if (old_Rows != visibleRowsG) {
      old_Rows = visibleRowsG;
      shell_new_rows();   // update window sizes
   }
}

//Call this function when the Eegl shell has been resized in any way.
//Will obtain the current size and redraw (also when size didn't change).
void
shell_resized(void){
   set_shellsize(0, 0, FALSE);
}

//Check if the shell size changed.  Handle a resize.
//When the size didn't change, nothing happens.
void
shell_resized_check(void) {
   int old_Rows = visibleRowsG;
   int old_Columns = visibleColsG;

   if (exiting)
      return;

   (void)ui_get_shellsize();
   check_shellsize();
   if (old_Rows != visibleRowsG || old_Columns != visibleColsG)
      shell_resized();
}

//Set size of the Eegl shell.
//If 'mustset' is TRUE, we must set visibleRowsG and Columns, do not get the real
//window size (this is used for the :win command).
//If 'mustset' is FALSE, we may try to get the real window size and if
//it fails use 'width' and 'height'.
private void
set_shellsize_inner(int width, int height, int mustset) {
   if (updating_screen)
      //resizing while in drawUpdateScreen() may cause a crash
      return;

   //curPor->book can be NULL when we are closing a window and the
   //buffer (or window) has already been closed and removing a scrollbar
   //causes a resize event. Don't resize then, it will happen after entering another buffer.
   if (curPor->book == NULL || curPor->lines == NULL)
      return;


   if (mustset || (ui_get_shellsize() == FAIL && height != 0)) {
      visibleRowsG = height;
      visibleColsG = width;
      check_shellsize();
      ui_set_shellsize(mustset);
   } else
      check_shellsize();

   //The portal layout used to be adjusted here, but it now happens in
   //screenalloc() (also invoked from screenclear()). That is because the
   //"busy" check above may skip this, but not screenalloc().

   if (stateG != MODE_ASKMORE && stateG != MODE_EXTERNCMD && stateG != MODE_CONFIRM)
      screenclear();
   else
      screen_start();       // don't know where cursor is now

   if (starting != NO_SCREEN) {

      changed_line_abv_curs();
      invalidate_botline();

      //We only redraw when it's needed:
      //- While at the more prompt or executing an external command, don't
      //  redraw, but position the cursor.
      //- While editing the command line, only redraw that.
      //- Otherwise, redraw right now, and position the cursor.
      //Always need to call drawUpdateScreen() or screenalloc(), to make
      //sure visibleRowsG/visibleColsG and the size of ScreenLines[] is correct!
      if (stateG == MODE_ASKMORE || stateG == MODE_EXTERNCMD || stateG == MODE_CONFIRM) {
         screenalloc(FALSE);
         repeat_message();
      } else {
         if (curPor->o.scrollBind)
            normPostProcessScrollbind(TRUE);
         if (stateG & MODE_COMMLINE) {
            drawUpdateScreen(UPD_NOT_VALID);
            redrawCommline();
         } else {
            update_topline();
            if (pum_visible()) {
                redraw_later(UPD_NOT_VALID);
                ins_compl_show_pum();
            }
            drawUpdateScreen(UPD_NOT_VALID);
            if (redrawing())
               setcursor();
         }
      }
      cursor_on();       // redrawing may have switched it off
   }
   out_flush();
}

void
set_shellsize(int width, int height, int mustset) {
   static int busy = FALSE;
   static int do_run = FALSE;

   if (width < 0 || height < 0)    // just checking...
      return;

   if (stateG == MODE_HITRETURN || stateG == MODE_SETWSIZE) {
      // postpone the resizing
      stateG = MODE_SETWSIZE;
      return;
   }

   //Avoid recursivity. This can happen when setting the window size causes another window-changed
   //signal or when two SIGWINCH signals come very close together. There needs to be another run
   //then after the current one is done to pick up the latest size.
   do_run = TRUE;
   if (busy)
      return;

   while (do_run) {
      do_run = FALSE;
      busy = TRUE;
      set_shellsize_inner(width, height, mustset);
      busy = FALSE;
   }
}

//Output termCodeS[KS_CTE], the t_TE termcap entry, and handle expected effects.
//The code possibly disables modifyOtherKeys and the Kitty keyboard protocol.
void
out_str_t_TE(void) {
    out_str(termCodeS[KS_CTE]);

   //The seenModifyOtherKeys flag is not reset here.  We do expect t_TE to
   //disable modifyOtherKeys, but until Xterm version 377 there is no way to
   //detect it's enabled again after the following t_TI.  We assume that when
   //seenModifyOtherKeys was set before it will still be valid.

   //When the modifyOtherKeys level is detected to be 2 we expect t_TE to disable it. Remembering
   //that it was detected to be enabled is useful in some situations.
   //The following t_TI is expected to request the state and then
   //modify_otherkeys_state will be set again.
   if (modify_otherkeys_state == MOKS_ENABLED || modify_otherkeys_state == MOKS_DISABLED)
      modify_otherkeys_state = MOKS_DISABLED;
   ei (modify_otherkeys_state != MOKS_INITIAL)
      modify_otherkeys_state = MOKS_AFTER_T_TE;
}

private int send_t_RK = FALSE;

//Output termCodeS[KS_TI] and setup for what follows.
void
out_str_t_TI(void) {
   out_str(termCodeS[KS_CTI]);

   //Send t_RK when there is no more work to do.
   send_t_RK = TRUE;
}

//Output termCodeS[KS_CBE], but only when t_PS and t_PE are set.
void
out_str_t_BE(void) {
   Byte *p;

   if (termCodeS[KS_CBE] == Em
       || (p = find_termcode(S"PS")) == NULL || *p == ZERO
       || (p = find_termcode(S"PE")) == NULL || *p == ZERO
   )
      return;
   out_str(termCodeS[KS_CBE]);
}

//If t_TI was recently sent and there is no typeahead or work to do, now send
//t_RK. This is postponed to avoid the response arriving in a shell command or after Eegl exits.
void
may_send_t_RK(void) {
   if (send_t_RK && !work_pending() && !ex_normal_busy && !in_feedkeys && !exiting) {
      send_t_RK = FALSE;
      out_str(termCodeS[KS_CRK]);
      out_flush();
   }
}

//Set the terminal to TMODE_RAW (for Normal mode) or TMODE_COOK (for external commands).
void
termSetMode(TermInputMode tmode) {
   if (!fullScreenG)
      return;

   //When returning after calling a shell cur_tmode is TMODE_UNKNOWN, set the terminal to raw mode,
   //even though we think it already is, because the shell program may have reset the terminal 
   //mode. When we think the terminal is normal, don't try to set it to normal again, because that 
   //causes problems (logout!) on some machines.
   if (tmode != cur_tmode) {
      // May need to check for termCodeS[KS_CRV] response and recognizedCodeS, it
      // doesn't work in Cooked mode, an external program may get them.
      if (tmode != TMODE_RAW && termrequest_any_pending())
         (void)vpeekc_nomap();
      handleUnansweredRequests();
      if (tmode != TMODE_RAW)
         mch_setmouse(FALSE);   // switch mouse off

      //Disable bracketed paste and modifyOtherKeys in cooked mode.
      //Avoid doing this too often, on some terminals the codes are not handled properly.
      if (termcap_active && tmode != TMODE_SLEEP && cur_tmode != TMODE_SLEEP) {
         MAY_WANT_TO_LOG_THIS;

         if (tmode != TMODE_RAW) {
            out_str(termCodeS[KS_CBD]);
            out_str_t_TE();   // possibly disables modifyOtherKeys
          } else {
            out_str_t_BE();   // enable bracketed paste mode (should
                  // be before mch_termSetMode().
            out_str_t_TI();   // possibly enables modifyOtherKeys
          }
      }
      out_flush();
      mch_termSetMode(tmode);   // machine specific function
      cur_tmode = tmode;
      if (tmode == TMODE_RAW)
          setmouse();      // may switch mouse on
      out_flush();
   }
   may_req_termresponse();
}

void
starttermcap(void) {
   if (!fullScreenG || termcap_active)
      return;

   MAY_WANT_TO_LOG_THIS;

   out_str(termCodeS[KS_TI]);         // start termcap mode
   out_str_t_TI();         // start "raw" mode
   out_str(termCodeS[KS_KS]);         // start "keypad transmit" mode
   out_str_t_BE();         // enable bracketed paste mode

   // Enable xterm's focus reporting mode when 'esckeys' is set.
   if (termCodeS[KS_FE] != Em)
      out_str(termCodeS[KS_FE]);

   out_flush();
   termcap_active = TRUE;
   screen_start();         // don't know where cursor is now
   may_req_termresponse();
   //Immediately check for a response.  If t_Co changes, we don't
   //want to redraw with wrong colors first.
   if (crv_status.tr_progress == STATUS_SENT)
       handleUnansweredRequests();
}

void
termStopTerminfo(void) {
   drawStopHilite();
   reset_cterm_colors();

   if (!termcap_active)
      return;

   //May need to discard termCodeS[KS_CRV], termCodeS[KS_U7] or termCodeS[KS_RBG] response.
   if (termrequest_any_pending()) {
      // Give the terminal a chance to respond.
      mch_delay(100L, 0);
#ifdef TCIFLUSH
      // Discard data received but not read.
      if (exiting)
         tcflush(fileno(stdin), TCIFLUSH);
#endif
   }
   // Check for recognizedCodeS first, otherwise an external program may get them.
   handleUnansweredRequests();
   MAY_WANT_TO_LOG_THIS;

   // Disable xterm's focus reporting mode if 'esckeys' is set.
   if (termCodeS[KS_FD] != Em)
      out_str(termCodeS[KS_FD]);

   out_str(termCodeS[KS_CBD]);
   out_str(termCodeS[KS_KE]);         // stop "keypad transmit" mode
   out_flush();
   termcap_active = FALSE;

   //Output t_te before t_TE, t_te may switch between main and alternate
   //screen and following codes may work on the active screen only.
   //
   //When using the Kitty keyboard protocol the main and alternate screen use a separate state. 
   //If we are (or were) using the Kitty keyboard protocol and t_te is not empty (possibly 
   //switching screens) then output t_TE both before and after outputting t_te.
   if (termCodeS[KS_TE] != Em)
      out_str_t_TE();      // probably disables the kitty keyboard protocol

   out_str(termCodeS[KS_TE]);  // stop termcap mode
   cursor_on();    // just in case it is still off
   out_str_t_TE(); // stop "raw" mode, modifyOtherKeys and Kitty keyboard protocol
   screen_start(); // don't know where cursor is now
   out_flush();
}

//Request version string (for xterm) when needed.
//Only do this after switching to raw mode, otherwise the result will be echoed.
//Only do this after startup has finished, to avoid that the response comes
//while executing "-c !cmd" or even after "-c quit".
//Only do this after termcap mode has been started, otherwise the codes for
//the cursor keys may be wrong.
//Only do this when 'esckeys' is on, otherwise the response causes trouble in Insert mode.
//On Unix only do it when both output and input are a tty (avoid writing
//request to terminal while reading from a file).
//The result is caught in check_termcode().
void
may_req_termresponse(void) {
   if (crv_status.tr_progress == STATUS_GET
       && can_get_termresponse()
       && starting == 0
       && termCodeS[KS_CRV] != Em
   ) {
      MAY_WANT_TO_LOG_THIS;
      LOG_TR1("Sending CRV request");
      out_str(termCodeS[KS_CRV]);
      termrequest_sent(&crv_status);
      // check for the characters now, otherwise they might be eaten by get_keystroke()
      out_flush();
      (void)vpeekc_nomap();
   }
}

// Send sequences to the terminal and check with t_u7 how the cursor moves, to find out properties
// of the terminal. Note that this goes out before termCodeS[KS_CRV], so that the result
// can be used when the termresponse arrives.
void
check_terminal_behavior(void) {
   int       did_send = FALSE;

   if (!can_get_termresponse() || starting != 0 || termCodeS[KS_U7] == Em)
      return;

   if (u7_status.tr_progress == STATUS_GET) {
      Byte buffer[16];

      //Ambiguous width check. Check how the terminal treats ambiguous character width (UAX #11).
      //First, we move the cursor to (1, 0) and print a test ambiguous character \u25bd
      //(WHITE DOWN-POINTING TRIANGLE) and then query the current cursor position.  If the
      //terminal treats \u25bd as single width, the position is (1, 1), or if it is treated as
      //double width, that will be (1, 2).  This function has the side effect that changes cursor
      //position, so it must be called immediately after entering termcap mode.
      MAY_WANT_TO_LOG_THIS;
      LOG_TR1("Sending request for ambiwidth check");
      //Do this in the second row.  In the first row the returned sequence
      //may be CSI 1;2R, which is the same as <S-F3>.
      term_windgoto(1, 0);
      buffer[mb_char2bytes(0x25bd, buffer)] = ZERO;
      out_str(buffer);
      out_str(termCodeS[KS_U7]);
      termrequest_sent(&u7_status);
      out_flush();
      did_send = TRUE;

      //This overwrites a few characters on the screen, a redraw is needed
      //after this. Clear them out for now.
      drawStopHilite();
      term_windgoto(1, 0);
      out_str((CS)"  ");
      line_was_clobbered(1);
   }

   if (xcc_status.tr_progress == STATUS_GET && visibleRowsG > 2) {
      //2. Check compatibility with xterm. We move the cursor to (2, 0), print a test sequence and
      //then query the current cursor position.  If the terminal properly handles unknown DCS
      //string and CSI sequence with intermediate byte, the test sequence is ignored and the
      //cursor does not move. If the terminal handles test sequence incorrectly, a garbage string
      //is displayed and the cursor does move.
      MAY_WANT_TO_LOG_THIS;
      LOG_TR1("Sending xterm compatibility test sequence.");
      //Do this in the third row.  Second row is used by ambiguous character width check.
      term_windgoto(2, 0);
      //send the test DCS string.
      out_str((CS)"\033Pzz\033\\");
      //send the test CSI sequence with intermediate byte.
      out_str((CS)"\033[0%m");
      out_str(termCodeS[KS_U7]);
      termrequest_sent(&xcc_status);
      out_flush();
      did_send = TRUE;

      //If the terminal handles test sequence incorrectly, garbage text is
      //displayed. Clear them out for now.
      drawStopHilite();
      term_windgoto(2, 0);
      out_str((CS)"           ");
      line_was_clobbered(2);
   }

   if (did_send) {
      term_windgoto(0, 0);

      //Need to reset the known cursor position.
      screen_start();

      //check for the characters now, otherwise they might be eaten by get_keystroke()
      out_flush();
      (void)vpeekc_nomap();
   }
}

//Similar to requesting the version string: Request the terminal background
//color when it is the right moment.
void
may_req_bg_color(void) {
   if (can_get_termresponse() && starting == 0) {
      Boole didit = false;

      //Only request foreground if t_RF is set.
      if (rfg_status.tr_progress == STATUS_GET && termCodeS[KS_RFG] != Em) {
         MAY_WANT_TO_LOG_THIS;
         LOG_TR1("Sending FG request");
         out_str(termCodeS[KS_RFG]);
         termrequest_sent(&rfg_status);
         didit = true;
      }

      //Only request background if t_RB is set.
      if (backgroundColorRequestS.tr_progress == STATUS_GET && termCodeS[KS_RBG] != Em) {
         MAY_WANT_TO_LOG_THIS;
         LOG_TR1("Sending BG request");
         out_str(termCodeS[KS_RBG]);
         termrequest_sent(&backgroundColorRequestS);
         didit = true;
      }

      if (didit) {
         //check for the characters now, otherwise they might be eaten by get_keystroke()
         out_flush();
         (void)vpeekc_nomap();
      }
   }
}

//Return TRUE when saving and restoring the screen.
int
termIsScreenBeingSwapped(void) {
   return (fullScreenG && termCodeS[KS_TI] != Em);
}

//By outputting the 'cursor very visible' termcap code, for some windowed
//terminals this makes the screen scrolled to the correct position.
//Used when starting Eegl or returning from a shell.
void
scroll_start(void) {
   if (termCodeS[KS_VS] == Em || termCodeS[KS_CVS] == Em)
      return;

   MAY_WANT_TO_LOG_THIS;
   out_str(termCodeS[KS_VS]);
   out_str(termCodeS[KS_CVS]);
   screen_start();      // don't know where cursor is now
}

// True if cursor is not visible
private int cursor_is_off = FALSE;

//True if cursor is not visible due to an ongoing cursor-less sleep
private int cursor_is_asleep = FALSE;

//Enable the cursor without checking if it's already enabled.
void
cursor_on_force(void) {
   out_str(termCodeS[KS_VE]);
   cursor_is_off = FALSE;
   cursor_is_asleep = FALSE;
}

//Enable the cursor if it's currently off.
void
cursor_on(void) {
   if (cursor_is_off && !cursor_is_asleep)
      cursor_on_force();
}

// Disable the cursor.
void
cursor_off(void) {
   if (fullScreenG && !cursor_is_off) {
      out_str(termCodeS[KS_VI]);       // disable cursor
      cursor_is_off = TRUE;
   }
}

// Disable the cursor and mark it disabled by cursor-less sleep
void
cursor_sleep(void) {
   cursor_is_asleep = TRUE;
   cursor_off();
}

// Enable the cursor and mark it not disabled by cursor-less sleep
void
cursor_unsleep(void) {
   cursor_is_asleep = FALSE;
   cursor_on();
}

// Set cursor shape to match Insert or Replace mode.
void
term_cursor_mode(int forced) {
   static int showing_mode = -1;

   // Only do something when redrawing the screen and we can restore the mode.
   if (!fullScreenG || termCodeS[KS_CEI] == Em) {
      if (forced && initial_cursor_shape > 0)
         // Restore to initial values.
         term_cursor_shape(initial_cursor_shape, initial_cursor_blink);
      return;
   }

   if (stateG & MODE_INSERT) {
      if ((forced || showing_mode != MODE_INSERT) && termCodeS[KS_CSI] != Em) {
         out_str(termCodeS[KS_CSI]);       // Insert mode cursor
         showing_mode = MODE_INSERT;
      }
   } ei (forced || showing_mode != MODE_NORMAL) {
      out_str(termCodeS[KS_CEI]);          // non-Insert mode cursor
      showing_mode = MODE_NORMAL;
   }
}

void
term_cursor_color(CS color) {
   if (termCodeS[KS_CSC] == Em)
      return;

   out_str(termCodeS[KS_CSC]);      // set cursor color start
   out_str_nf(color);
   out_str(termCodeS[KS_CEC]);      // set cursor color end
   out_flush();
}

int
blink_state_is_inverted(void) {
   return cursorBlinkingRequestS.tr_progress == STATUS_GOT
      && cursorStyleRequestS.tr_progress == STATUS_GOT
      && initial_cursor_blink != initial_cursor_shape_blink;
}

//"shape": 1 = block, 2 = underline, 3 = vertical bar
void
term_cursor_shape(int shape, int blink) {
   if (termCodeS[KS_CSH] != Em) {
      OUT_STR(TGOTO(termCodeS[KS_CSH], 0, shape * 2 - blink));
      out_flush();
   } else {
      int do_blink = blink;

      // t_SH is empty: try setting just the blink state.
      // The blink flags are XORed together, if the initial blinking from
      // style and shape differs, we need to invert the flag here.
      if (blink_state_is_inverted())
         do_blink = !blink;

      if (do_blink && termCodeS[KS_VS] != Em) {
         out_str(termCodeS[KS_VS]);
         out_flush();
      } ei (!do_blink && termCodeS[KS_CVS] != Em) {
         out_str(termCodeS[KS_CVS]);
         out_flush();
      }
   }
}

//Set scrolling region for window 'wp'. The region starts 'off' lines from the start of the portal.
//Also set the vertical scroll region for a vertically split window. Always the full width of the
//portal, excluding the vertical separator.
void
scroll_region_set(Portal* wp, int off) {
   OUT_STR(TGOTO( termCodeS[KS_CS], wp->portalRow + wp->height - 1, wp->portalRow + off));
   if (termCodeS[KS_CSV] != Em && wp->width != visibleColsG)
      OUT_STR(TGOTO(termCodeS[KS_CSV], wp->portalCol + wp->width - 1, wp->portalCol));
   screen_start();          // don't know where cursor is now
}

//Reset scrolling region to the whole screen.
void
scroll_region_reset(void) {
   OUT_STR(TGOTO(termCodeS[KS_CS], (int)visibleRowsG - 1, 0));
   if (termCodeS[KS_CSV] != Em)
      OUT_STR(TGOTO(termCodeS[KS_CSV], (int)visibleColsG - 1, 0));
   screen_start();          // don't know where cursor is now
}


// List of terminal codes that are currently recognized.

private struct termcode {
   Byte name[2];       // termcap name of entry
   CS code;       // terminal code (in allocated memory)
   int len;       // STRLEN(code)
   int modlen;       // length of part before ";*~".
} *recognizedCodeS = NULL;

private int  tc_max_len = 0; // number of entries that recognizedCodeS[] can hold
private int  tc_len = 0;       // current number of entries in recognizedCodeS[]

private int endsInStar(Text code);

void
clear_termcodes(void) {
   while (tc_len > 0)
      eeglFree(recognizedCodeS[--tc_len].code);
   EE_CLEAR(recognizedCodeS);
   tc_max_len = 0;

   BC = (char*)Em;
   UP = (char*)Em;
   PC = ZERO;         // set pad character to ZERO
   ospeed = 0;

   needToGatherTermLeaders = TRUE;      // need to fill termLeaderG[]
}

#define ATC_FROM_TERM 55

//For xterm we recognize special codes like "ESC[42;*X" and "ESC O*X" that accept modifiers.
//Set "recognizedCodeS[idx].modlen".
private void
adjust_modlen(int idx) {
   recognizedCodeS[idx].modlen = 0;
   int j = endsInStar((Text){recognizedCodeS[idx].code, recognizedCodeS[idx].len});
   if (j <= 0)
      return;

   recognizedCodeS[idx].modlen = recognizedCodeS[idx].len - 1 - j;
   // For "CSI[@;X" the "@" is not included in "modlen".
   if (recognizedCodeS[idx].code[recognizedCodeS[idx].modlen - 1] == '@')
      --recognizedCodeS[idx].modlen;
}

//Add a new entry for "name[2]" to the list of terminal codes.
//Note that "name" may not have a terminating ZERO.
//The list is kept alphabetical for ":set termcap"
//"flags" is TRUE when replacing 7-bit by 8-bit controls is desired.
//"flags" can also be ATC_FROM_TERM for got_code_from_term().
void
add_termcode(CS name, CS string, Boole isAtcFromTerm) {
   if (!string || *string == ZERO) {
      del_termcode(name);
      return;
   }

   CS action = S"Setting";
   CS s = copyStr(string);

   struct termcode *new_tc;
   int          i, j;
   int len = (int)STRLEN(s);

   needToGatherTermLeaders = TRUE;

   // need to make space for more entries
   if (tc_len == tc_max_len) {
      tc_max_len += 20;
      new_tc = ALLOC_MULT(struct termcode, tc_max_len);
      for (i = 0; i < tc_len; ++i)
         new_tc[i] = recognizedCodeS[i];
      eeglFree(recognizedCodeS);
      recognizedCodeS = new_tc;
   }

   //Look for existing entry with the same name, it is replaced.
   //Look for an existing entry that is alphabetical higher, the new entry
   //is inserted in front of it.
   for (i = 0; i < tc_len; ++i) {
      if (recognizedCodeS[i].name[0] < name[0])
         continue;
      if (recognizedCodeS[i].name[0] == name[0]) {
         if (recognizedCodeS[i].name[1] < name[1])
            continue;
         // Exact match: May replace old code.
         if (recognizedCodeS[i].name[1] == name[1]) {
            if (isAtcFromTerm == ATC_FROM_TERM
                 && (j = endsInStar((Text){recognizedCodeS[i].code, recognizedCodeS[i].len})) > 0
            ) {
               //Don't replace ESC[123;*X or ESC O*X with another when
               //invoked from got_code_from_term().
               if (len == recognizedCodeS[i].len - j
                      && STRNCMP(s, recognizedCodeS[i].code, len - 1) == 0
                      && s[len - 1] == recognizedCodeS[i].code[recognizedCodeS[i].len - 1]
               ) {
                  // They are equal but for the ";*": don't add it.
                  lo("Termcap entry %c%c did not change", name[0], name[1]);
                  eeglFree(s);
                  return;
               }
           } else {
               // Replace old code.
               lo("Termcap entry %c%c was: %s", name[0], name[1], recognizedCodeS[i].code);
               eeglFree(recognizedCodeS[i].code);
               --tc_len;
               break;
            }
         }
      }
      // Found alphabetically larger entry, move rest to insert new entry
      action = S"Adding";
      for (j = tc_len; j > i; --j)
         recognizedCodeS[j] = recognizedCodeS[j - 1];
      break;
   }

   lo("%s termcap entry %c%c to %s", action, name[0], name[1], s);
   recognizedCodeS[i].name[0] = name[0];
   recognizedCodeS[i].name[1] = name[1];
   recognizedCodeS[i].code = s;
   recognizedCodeS[i].len = len;
   adjust_modlen(i);

   ++tc_len;
}

// Some function keys may include modifiers, but the terminfo entries
// do not indicate that.  Insert ";*" where we expect modifiers might appear.
private void
accept_modifiers_for_function_keys(void) {
   RegMatch regmatch;
   CLEAR_FIELD(regmatch);
   regmatch.rm_ic = TRUE;
   regmatch.regprog = compileRegexp(S"^\033[\\d\\+\\~$", RE_MAGIC);

   for (int i = 0; i < tc_len; ++i) {
      if (!regmatch.regprog)
         return;

      // skip PasteStart and PasteEnd
      if (recognizedCodeS[i].name[0] == 'P'
         && (recognizedCodeS[i].name[1] == 'S' || recognizedCodeS[i].name[1] == 'E')
      )
         continue;

      CS s = recognizedCodeS[i].code;
      if (s && eeRegexec(&regmatch, s, (ColNr)0)) {
         Unt len = STRLEN(s);
         Byte *ns = alloc(len + 3);
         mch_memmove(ns, s, len - 1);
         mch_memmove(ns + len - 1, ";*~", 4);
         eeglFree(s);
         recognizedCodeS[i].code = ns;
         recognizedCodeS[i].len += 2;
         adjust_modlen(i);
      }
   }

   eeRegFree(regmatch.regprog);
}

//Check termcode "code[len]" for ending in ;*X or *X. The "X" can be any character.
//Return 0 if not found, 2 for ;*X and 1 for *X.
private int
endsInStar(Text code) {
   // Shortest is <M-O>*X.  With ; shortest is <CSI>@;*X
   if (code.len >= 3 && code.c[code.len - 2] == '*') {
      return (code.len >= 5 && code.c[code.len - 3] == ';') ? 2 : 1;
   }
   return 0;
}

CS
find_termcode(CS name) {
   for (int i = 0; i < tc_len; ++i) {
      if (recognizedCodeS[i].name[0] == name[0] && recognizedCodeS[i].name[1] == name[1])
         return recognizedCodeS[i].code;
   }
   return NULL;
}

CS
get_termcode(int i) {
   if (i >= tc_len)
      return NULL;
   return &recognizedCodeS[i].name[0];
}

//Length of the terminal code at index 'idx'.
int
get_termcode_len(int idx) {
   return recognizedCodeS[idx].len;
}

void
del_termcode(CS name) {
   if (!recognizedCodeS)   // nothing there yet
      return;

   needToGatherTermLeaders = TRUE;

   for (int i = 0; i < tc_len; ++i) {
      if (recognizedCodeS[i].name[0] == name[0] && recognizedCodeS[i].name[1] == name[1]) {
         del_termcode_idx(i);
         return;
      }
   }
   // not found. Give error message?
}

private void
del_termcode_idx(int idx) {
   eeglFree(recognizedCodeS[idx].code);
   --tc_len;
   for (int i = idx; i < tc_len; ++i)
      recognizedCodeS[i] = recognizedCodeS[i + 1];
}

private LineNr orig_topline = 0;
private int orig_topfill = 0;

//Checking for double-clicks ourselves.
//"orig_topline" is used to avoid detecting a double-click when the window
//contents scrolled (e.g., when 'scrolloff' is non-zero).

//Set orig_topline.  Used when jumping to another window, so that a double click still works.
void
set_mouse_topline(Portal* po) {
   orig_topline = po->topLine;
   orig_topfill = po->topFill;
}

// TRUE if the top line and top fill of window 'wp' matches the saved topline and topfill.
int
is_mouse_topline(Portal* po) {
   return orig_topline == po->topLine && orig_topfill == po->topFill;
}

//If "buffer" is NULL put "string[new_slen]" in typeBufG; "bufLen" is not used.
//If "buffer" is not NULL put "string[new_slen]" in "buf[bufsize]" and adjust "bufLen".
//Remove "slen" bytes. Return FAIL for error.
int
termPutStrIntoTypebuf(
   int offset,
   int slen,
   CS string,
   int new_slen,
   CS buffer,
   int bufsize,
   OUT int* bufLen
){
   int extra = new_slen - slen;
   string[new_slen] = ZERO;
   if (!buffer) {
      if (extra < 0)
         //remove matched chars, taking care of noremap
         del_typebuf(-extra, offset);
      ei (extra > 0)
         //insert the extra space we need
         if (insertIntoTypebuf(string + slen, REMAP_YES, offset, FALSE, FALSE) == FAIL)
            return FAIL;

      // Careful: del_typebuf() and insertIntoTypebuf() may have reallocated typeBufG.c[]!
      mch_memmove(typeBufG.c + typeBufG.currPos + offset, string, (Unt)new_slen);
   } else {
      if (extra < 0)
         // remove matched characters
         mch_memmove(buffer + offset, buffer + offset - extra,
                     (Unt)(*bufLen + offset + extra));
      ei (extra > 0) {
         //Insert the extra space we need. If there is insufficient space, return -1.
         if (*bufLen + extra + new_slen >= bufsize)
            return FAIL;
         mch_memmove(buffer + offset + extra, buffer + offset, (Unt)(*bufLen - offset));
      }
      mch_memmove(buffer + offset, string, (Unt)new_slen);
      *bufLen = *bufLen + extra + new_slen;
   }
   return OK;
}

// Decode a modifier number as xterm provides it into MOD_MASK bits.
Unt
decode_modifiers(int n) {
   int code = n - 1;
   Unt modifiers = 0;

   if (code & 1)
      modifiers |= MOD_MASK_SHIFT;
   if (code & 2)
      modifiers |= MOD_MASK_ALT;
   if (code & 4)
      modifiers |= MOD_MASK_CTRL;
   if (code & 8)
      modifiers |= MOD_MASK_META;
   // Any further modifiers are silently dropped.

   return modifiers;
}

private int
modifiers2keycode(Unt modifiers, Unt* key, CS string) {
   int new_slen = 0;

   if (modifiers == 0)
      return 0;

   // Some keys have the modifier included.  Need to handle that here to
   // make mappings work.  This may result in a special key, such as K_S_TAB.
   *key = simplify_key(*key, &modifiers);
   if (modifiers != 0) {
      string[new_slen++] = K_SPECIAL;
      string[new_slen++] = (int)KS_MODIFIER;
      string[new_slen++] = modifiers;
   }
   return new_slen;
}

// Handle a cursor position report.
private void
handle_u7_response(int* arg, CS tp UNUSED, int csi_len UNUSED) {
   if (arg[0] == 2 && arg[1] >= 2) {
      LOG_TRN("Received U7 status: %s", tp);
      u7_status.tr_progress = STATUS_GOT;
      did_cursorhold = TRUE;
   } ei (arg[0] == 3) {
      LOG_TRN("Received compatibility test result: %s", tp);
      xcc_status.tr_progress = STATUS_GOT;

      //Third row: xterm compatibility test.
      //If the cursor is on the first column then the terminal can handle
      //the request for cursor style and blinking.
      int value = arg[1] == 1 ? TPR_YES : TPR_NO;
      term_props[TPR_CURSOR_STYLE].status = value;
      term_props[TPR_CURSOR_BLINK].status = value;
   }
}

//Handle a response to termCodeS[KS_CRV]: {lead}{first}{x};{vers};{y}c
//Xterm and alike use '>' for {first}. Rxvt sends "{lead}?1;2c".
private void
handle_version_response(int first, int* arg, int argc) {
   //The xterm version.  It is set to zero when it can't be an actual xterm version.
   int version = arg[1];

   LOG_TRN("Received CRV response: %s", tp);
   crv_status.tr_progress = STATUS_GOT;
   did_cursorhold = TRUE;

   //Reset terminal properties that are set based on the termresponse.
   //Mainly useful for tests that send the termresponse multiple times.
   //For testing all props can be reset.
   init_term_props(reset_term_props_on_termresponse);

   //Screen sends 40500.
   //rxvt sends its version number: "20703" is 2.7.3.
   //Ignore it for when the user has set 'term' to xterm, even though it's an rxvt.
   if (version > 20000)
      version = 0;

   // Figure out more if the response is CSI > 99 ; 99 ; 99 c
   if (first == '>' && argc == 3) {
      // mintty 2.9.5 sends 77;20905;0c. (77 is ASCII 'M' for mintty.)
      if (arg[0] == 77) {
         // mintty can do SGR mouse reporting
         term_props[TPR_MOUSE].status = TPR_MOUSE_SGR;
      }

      // libvterm sends 0;100;0
      // Konsole sends 0;115;0 and works the same way
      if ((version == 100 || version == 115) && arg[0] == 0 && arg[2] == 0) {
         // Libvterm can handle SGR mouse reporting.
         term_props[TPR_MOUSE].status = TPR_MOUSE_SGR;
      }

      if (version == 95) {
         //Mac Terminal.app sends 1;95;0
         if (arg[0] == 1 && arg[2] == 0) {
            term_props[TPR_UNDERLINE_RGB].status = TPR_YES;
            term_props[TPR_MOUSE].status = TPR_MOUSE_SGR;
         }
         //iTerm2 sends 0;95;0
         ei (arg[0] == 0 && arg[2] == 0) {
            // iTerm2 can do SGR mouse reporting
            term_props[TPR_MOUSE].status = TPR_MOUSE_SGR;
         }
         // old iTerm2 sends 0;95;
         ei (arg[0] == 0 && arg[2] == -1)
            term_props[TPR_UNDERLINE_RGB].status = TPR_YES;
      }

      //screen sends 83;40500;0 83 is 'S' in ASCII.
      if (arg[0] == 83) {
         //screen supports SGR mouse codes since 4.7.0
         term_props[TPR_MOUSE].status = TPR_MOUSE_SGR;
      }

      //If no recognized terminal has set mouse behavior, assume xterm.
      if (term_props[TPR_MOUSE].status == TPR_UNKNOWN) {
         term_props[TPR_MOUSE].status = TPR_MOUSE_SGR;
      }

      //Detect terminals that set $TERM to something like
      //"xterm-256color" but are not fully xterm compatible.
      //
      //Gnome terminal sends 1;3801;0, 1;4402;0 or 1;2501;0.
      //Newer Gnome-terminal sends 65;6001;1.
      //xfce4-terminal sends 1;2802;0.
      //screen sends 83;40500;0
      //Assuming any version number over 2500 is not an
      //xterm (without the limit for rxvt and screen).
      if (arg[1] >= 2500)
          term_props[TPR_UNDERLINE_RGB].status = TPR_YES;

      ei (version == 136 && arg[2] == 0) {
         term_props[TPR_UNDERLINE_RGB].status = TPR_YES;

         // PuTTY sends 0;136;0
         if (arg[0] == 0) {
            // supports sgr-like mouse reporting.
            term_props[TPR_MOUSE].status = TPR_MOUSE_SGR;
         }
         // vandyke SecureCRT sends 1;136;0
      }

      //Konsole sends 0;115;0 - but t_u8 does not actually work, therefore commented out.
      //ei (version == 115 && arg[0] == 0 && arg[2] == 0)
      //    term_props[TPR_UNDERLINE_RGB].status = TPR_YES;

      //Kitty up to 9.x sends 1;400{version};{secondary-version}
      if (arg[0] == 1 && arg[1] >= 4000 && arg[1] <= 4009) {
          term_props[TPR_KITTY].status = TPR_YES;
          term_props[TPR_KITTY].setByTermResponse = TRUE;

          //Kitty can handle SGR mouse reporting.
          term_props[TPR_MOUSE].status = TPR_MOUSE_SGR;
      }

      //GNU screen sends 83;30600;0, 83;40500;0, etc.
      //30600/40500 is a version number of GNU screen. DA2 support is added
      //on 3.6.  DCS string has a special meaning to GNU screen, but xterm
      //compatibility checking does not detect GNU screen.
      if (arg[0] == 83 && arg[1] >= 30600) {
          term_props[TPR_CURSOR_STYLE].status = TPR_NO;
          term_props[TPR_CURSOR_BLINK].status = TPR_NO;
      }

      //Xterm first responded to this request at patch level
      //95, so assume anything below 95 is not xterm and hopefully supports
      //the underline RGB color sequence.
      if (version < 95)
          term_props[TPR_UNDERLINE_RGB].status = TPR_YES;

      //Getting the cursor style is only supported properly by xterm since
      //version 279 (otherwise it returns 0x18).
      if (version < 279)
          term_props[TPR_CURSOR_STYLE].status = TPR_NO;

      //Take action on the detected properties.

      if (termCodeS[KS_8U] != Em && write_t_8u_state == MAYBE)
         // Did skip writing t_8u, a complete redraw is needed.
         redraw_later_clear();
      write_t_8u_state = OK;  // can output t_8u now

      int need_flush = FALSE;

      //Only request the cursor style if t_SH and t_RS are
      //set. Only supported properly by xterm since version
      //279 (otherwise it returns 0x18).
      //Only when getting the cursor style was detected to work.
      //Not for Terminal.app, it can't handle t_RS, it echoes the characters to the screen.
      if (cursorStyleRequestS.tr_progress == STATUS_GET
         && term_props[TPR_CURSOR_STYLE].status == TPR_YES
         && termCodeS[KS_CSH] != Em
         && termCodeS[KS_CRS] != Em)
      {
          MAY_WANT_TO_LOG_THIS;
          LOG_TR1("Sending cursor style request");
          out_str(termCodeS[KS_CRS]);
          termrequest_sent(&cursorStyleRequestS);
          need_flush = TRUE;
      }

      //Only request the cursor blink mode if t_RC set. Not
      //for Gnome terminal, it can't handle t_RC, it
      //echoes the characters to the screen. Only when getting the cursor style was detected to work.
      if (cursorBlinkingRequestS.tr_progress == STATUS_GET
         && term_props[TPR_CURSOR_BLINK].status == TPR_YES
         && termCodeS[KS_CRC] != Em)
      {
          MAY_WANT_TO_LOG_THIS;
          LOG_TR1("Sending cursor blink mode request");
          out_str(termCodeS[KS_CRC]);
          termrequest_sent(&cursorBlinkingRequestS);
          need_flush = TRUE;
      }

      if (need_flush)
          out_flush();
   }
}

//Add "key" to "buf" and return the number of bytes used.
//Handle special keys and multi-byte characters.
private int
add_key_to_buf(Unt key, CS buffer) {
   int idx = 0;

   if (IS_SPECIAL(key)) {
      buffer[idx++] = K_SPECIAL;
      buffer[idx++] = KEY2TERMCAP0(key);
      buffer[idx++] = KEY2TERMCAP1(key);
   } else
      idx += mb_char2bytes(key, buffer + idx);
   return idx;
}

// Shared between handle_key_with_modifier() and handle_csi_function_key().
private int
put_key_modifiers_in_typeBuf(
   Unt key_arg,
   Unt modifiers_arg,
   int csi_len,
   int offset,
   CS buffer,
   int bufsize,
   int* bufLen
) {
   //Some keys need adjustment when the Ctrl modifier is used.
   Unt key = may_adjust_key_for_ctrl(modifiers_arg, key_arg);

   //May remove the shift modifier if it's already included in the key.
   Unt modifiers = may_remove_shift_modifier(modifiers_arg, key);

   //Produce modifiers with K_SPECIAL KS_MODIFIER {mod}
   Byte string[MAX_KEY_CODE_LEN + 1];
   int new_slen = modifiers2keycode(modifiers, &key, string);

   //Add the bytes for the key.
   new_slen += add_key_to_buf(key, string + new_slen);

   string[new_slen] = ZERO;
   if (termPutStrIntoTypebuf(offset, csi_len, string, new_slen, buffer, bufsize, bufLen) == FAIL)
      return -1;
   return new_slen - csi_len + offset;
}

// Handle a sequence with key and modifier, one of:
//   {lead}27;{modifier};{key}~
//   {lead}{key};{modifier}u
// Return the difference in length.
private int
handle_key_with_modifier(
   int arg[static 3],
   int   trail,
   int   csi_len,
   int   offset,
   CS buffer,
   int   bufsize,
   int   *bufLen)
{
   Unt key = trail == 'u' ? arg[0] : arg[2];
   Unt modifiers = decode_modifiers(arg[1]);

   // Some terminals do not apply the Shift modifier to the key.  To make
   // mappings consistent we do it here.  TODO: support more keys.
   if ((modifiers & MOD_MASK_SHIFT) && key >= 'a' && key <= 'z')
      key += 'A' - 'a';

   // Putting Esc in the buffer creates ambiguity, it can be the start of an
   // escape sequence.  Use K_ESC to avoid that.
   if (key == ESC)
      key = K_ESC;

   return put_key_modifiers_in_typeBuf(
         key, modifiers, csi_len, offset, buffer, bufsize, bufLen
   );
}

// Handle a sequence with key without a modifier: {lead}{key}u. Return the difference in length.
private int
handle_key_without_modifier(
   int arg[static 3],
   int csiLen,
   int offset,
   CS buffer,
   int   bufsize,
   int   *bufLen)
{
   Byte  string[MAX_KEY_CODE_LEN + 1];
   int newSlen;

   if (arg[0] == ESC) {
      //Putting Esc in the buffer creates ambiguity, it can be the start of
      //an escape sequence. Use K_ESC to avoid that.
      string[0] = K_SPECIAL;
      string[1] = KS_EXTRA;
      string[2] = KE_ESC;
      newSlen = 3;
   } else
      newSlen = add_key_to_buf(arg[0], string);

   if (termPutStrIntoTypebuf(offset, csiLen, string, newSlen, buffer, bufsize, bufLen) == FAIL)
      return -1;
   return newSlen - csiLen + offset;
}

// CSI function key without or with modifiers:
//   {lead}[ABCDEFHPQRS]
//   {lead}1;{modifier}[ABCDEFHPQRS]
// Return 0 when not recognized, a positive number when recognized.
private int
handle_csi_function_key(
   int   argc,
   int arg[static 3],
   int   trail,
   int   csi_len,
   OUT CS key_name,
   int   offset,
   CS buffer,
   int   bufsize,
   int   *bufLen)
{
   key_name[0] = 'k';
   switch (trail) {
   case 'A': key_name[1] = 'u'; break;  // K_UP
   case 'B': key_name[1] = 'd'; break;  // K_DOWN
   case 'C': key_name[1] = 'r'; break;  // K_RIGHT
   case 'D': key_name[1] = 'l'; break;  // K_LEFT

   // case 'Em': keypad BEGIN - not supported
   case 'F': key_name[0] = '@'; key_name[1] = '7'; break;  // K_END
   case 'H': key_name[1] = 'h'; break;  // K_HOME

   case 'P': key_name[1] = '1'; break;  // K_F1
   case 'Q': key_name[1] = '2'; break;  // K_F2
   case 'R': key_name[1] = '3'; break;  // K_F3
   case 'S': key_name[1] = '4'; break;  // K_F4

   default: return 0;  // not recognized
   }

   int key = TERMCAP2KEY(key_name[0], key_name[1]);
   int modifiers = argc == 2 ? decode_modifiers(arg[1]) : 0;
   put_key_modifiers_in_typeBuf(key, modifiers, csi_len, offset, buffer, bufsize, bufLen);
   return csi_len;
}

// Handle a CSI escape sequence.
// - Xterm version string.
//
// - Response to XTQMODKEYS: "{lead} > 4 ; Pv m".
//
// - Cursor position report: {lead}{row};{col}R
//   The final byte must be 'R'. It is used for checking the
//   ambiguous-width character state.
//
// - window position reply: {lead}3;{x};{y}t
//
// - key with modifiers when modifyOtherKeys is enabled or the Kitty keyboard
//   protocol is used:
//       {lead}27;{modifier};{key}~
//       {lead}{key};{modifier}u
//
// - function key with or without modifiers:
//   {lead}[ABCDEFHPQRS]
//   {lead}1;{modifier}[ABCDEFHPQRS]
//
// Return 0 for no match, -1 for partial match, > 0 for full match.
private int
handleControlSequenceIntroducer(
   CS tp,
   int len,
   CS argp,
   int offset,
   CS buffer,
   int bufsize,
   int* bufLen,
   CS key_name,
   int* slen
){
   int      first = -1;  // optional char right after {lead}
   int      trail;        // char that ends CSI sequence
   int      arg[3] = {-1, -1, -1};   // argument numbers
   int      argc = 0;      // number of arguments
   Byte   *ap = argp;
   int      csi_len;

   // Check for non-digit after CSI.
   if (!EE_ISDIGIT(*ap))
      first = *ap++;

   if (first >= 'A' && first <= 'Z') {
      // If "first" is in [ABCDEFHPQRS] then it is actually the "trail" and
      // no argument follows.
      trail = first;
      first = -1;
      --ap;
    } else {
      // Find up to three argument numbers.
      for (argc = 0; argc < 3; ) {
         if (ap >= tp + len)
            return -1;
         if (*ap == ';')
            arg[argc++] = -1;  // omitted number
         ei (EE_ISDIGIT(*ap)) {
            arg[argc] = 0;
            for (;;) {
                   if (ap >= tp + len)
                  return -1;
                   if (!EE_ISDIGIT(*ap))
                  break;
                   arg[argc] = arg[argc] * 10 + (*ap - '0');
                   ++ap;
            }
            ++argc;
         }
         if (*ap == ';')
            ++ap;
         else
            break;
      }

      //mrxvt has been reported to have "+" in the version. Assume
      //the escape sequence ends with a letter or one of "{|}~".
      while (ap < tp + len && !(*ap >= '{' && *ap <= '~') && !ASCII_ISALPHA(*ap))
         ++ap;
      if (ap >= tp + len)
         return -1;
      trail = *ap;
   }

   csi_len = (int)(ap - tp) + 1;

   //Response to XTQMODKEYS: "CSI > 4 ; Pv m" where Pv indicates the
   //modifyOtherKeys level. Drop similar responses.
   if (first == '>' && (argc == 1 || argc == 2) && trail == 'm') {
      if (arg[0] == 4 && argc == 2)
          modify_otherkeys_state = arg[1] == 2 ? MOKS_ENABLED : MOKS_OFF;

      key_name[0] = (int)KS_EXTRA;
      key_name[1] = (int)KE_IGNORE;
      *slen = csi_len;
   }

   // Function key starting with CSI:
   //   {lead}[ABCDEFHPQRS]
   //   {lead}1;{modifier}[ABCDEFHPQRS]
   ei (first == -1 && ASCII_ISUPPER(trail) && (argc == 0 || (argc == 2 && arg[0] == 1))) {
      int res = handle_csi_function_key(argc, arg, trail,
                  csi_len, OUT key_name, offset, buffer, bufsize, bufLen);
      return res <= 0 ? res : len + res;
   }

   // Cursor position report: {lead}{row};{col}R
   // Eat it when there are 2 arguments and it ends in 'R'. Also when u7_status is not "sent", it
   // may be from a previous Eegl that just exited. But not for <S-F3>, it sends something
   // similar, check for row and column to make sense.
   ei (first == -1 && argc == 2 && trail == 'R') {
      handle_u7_response(arg, tp, csi_len);
      key_name[0] = (int)KS_EXTRA;
      key_name[1] = (int)KE_IGNORE;
      *slen = csi_len;
   }

   // Version string: Eat it when there is at least one digit and it ends in 'c'
   ei (termCodeS[KS_CRV] != Em && ap > argp + 1 && trail == 'c') {
      handle_version_response(first, arg, argc);

      *slen = csi_len;
      set_EeglVar_string(VV_TERMRESPONSE, tp, *slen);
      apply_autocmds(EVENT_TERMRESPONSE, NULL, NULL, false, curBook);
      apply_autocmds(EVENT_TERMRESPONSEALL, (CS)"version", NULL, false, curBook);
      key_name[0] = (int)KS_EXTRA;
      key_name[1] = (int)KE_IGNORE;
   }

    // Check blinking cursor from xterm:
    // {lead}?12;1$y       set
    // {lead}?12;2$y       not set
    //
    // {lead} can be <Esc>[ or CSI
    ei (cursorBlinkingRequestS.tr_progress == STATUS_SENT
       && first == '?'
       && ap == argp + 6
       && arg[0] == 12
       && ap[-1] == '$'
       && trail == 'y'
   ){
      initial_cursor_blink = (arg[1] == '1');
      cursorBlinkingRequestS.tr_progress = STATUS_GOT;
      LOG_TRN("Received cursor blinking mode response: %s", tp);
      key_name[0] = (int)KS_EXTRA;
      key_name[1] = (int)KE_IGNORE;
      *slen = csi_len;
      set_EeglVar_string(VV_TERMBLINKRESP, tp, *slen);
      apply_autocmds(EVENT_TERMRESPONSEALL, S"cursorblink", NULL, false, curBook);
   }
   // Kitty keyboard protocol status response: CSI ? flags u
   ei (first == '?' && argc == 1 && trail == 'u') {
      // The protocol has various "progressive enhancement flags" values, but
      // we only check for zero and non-zero here.
      if (arg[0] != '0') {
         // Reset seenModifyOtherKeys just in case some key combination has
         // been seen that set it before we get the status response.
         seenModifyOtherKeys = FALSE;
      }

      key_name[0] = (int)KS_EXTRA;
      key_name[1] = (int)KE_IGNORE;
      *slen = csi_len;
   }

   // Check for a window position response from the terminal:
   //       {lead}3;{x};{y}t
   ei (did_request_winpos && argc == 3 && arg[0] == 3 && trail == 't') {
      winpos_x = arg[1];
      winpos_y = arg[2];
      // got finished code: consume it
      key_name[0] = (int)KS_EXTRA;
      key_name[1] = (int)KE_IGNORE;
      *slen = csi_len;

      if (--did_request_winpos <= 0)
          winPositionRequestS.tr_progress = STATUS_GOT;
   }

   // Key with modifier:
   //   {lead}27;{modifier};{key}~
   //   {lead}{key};{modifier}u
   // Even though we only handle four modifiers and the {modifier} value should be 16 or lower, we
   // accept all modifier values to avoid the raw sequence to be passed through.
   ei ((arg[0] == 27 && argc == 3 && trail == '~') || (argc == 2 && trail == 'u')) {
      return len + handle_key_with_modifier(arg, trail, csi_len, offset, buffer, bufsize, bufLen);
   }

   // Key without modifier (Kitty sends this for Esc): {lead}{key}u
   ei (argc == 1 && trail == 'u') {
      return len + handle_key_without_modifier(arg, csi_len, offset, buffer, bufsize, bufLen);
   }

   // else: Unknown CSI sequence.  We could drop it, but then the user can't create a map for it.
   return 0;
}

// Handle an OSC sequence, fore/background color response from the terminal:
//
//       {lead}{code};rgb:{rrrr}/{gggg}/{bbbb}{tail}
// or    {lead}{code};rgb:{rr}/{gg}/{bb}{tail}
//
// {code} is 10 for foreground, 11 for background
// {lead} can be <Esc>] or OSC
// {tail} can be '\007', <Esc>\ or STERM.
//
// Consume any code that starts with "{lead}11;", it's also
// possible that "rgba" is following.
private int
handle_osc(Byte *tp, Byte *argp, int len, Byte *key_name, int *slen) {
   int      i;

   int j = 1 + (tp[0] == ESC);
   if (len >= j + 3 && (argp[0] != '1' || (argp[1] != '1' && argp[1] != '0') || argp[2] != ';'))
      i = 0; // no match
   else {
      for (i = j; i < len; ++i) {
         if (tp[i] != '\007'
                && (tp[0] == OSC
                   ? tp[i] != STERM
                   : (tp[i] != ESC || i + 1 >= len || tp[i + 1] != '\\'))
         ) {
            continue;
         }
         Boole isBg = argp[1] == '1';
         Boole is4digit = i - j >= 21 && tp[j + 11] == '/' && tp[j + 16] == '/';

         if (i - j >= 15 && STRNCMP(tp + j + 3, "rgb:", 4) == 0
                && (is4digit || (tp[j + 9] == '/' && tp[j + 12] == '/'))
         ){
            Byte *tp_r = tp + j + 7;
            Byte *tp_g = tp + j + (is4digit ? 12 : 10);
            Byte *tp_b = tp + j + (is4digit ? 17 : 13);
            int rval, gval, bval;

            rval = hexhex2nr(tp_r);
            gval = hexhex2nr(tp_g);
            bval = hexhex2nr(tp_b);
            if (isBg) {
               Boole newThemeLite = (3 * '6' < *tp_r + *tp_g + *tp_b);

               LOG_TRN("Received RBG response: %s", tp);
               backgroundColorRequestS.tr_progress = STATUS_GOT;
               bg_r = rval;
               bg_g = gval;
               bg_b = bval;
               if (!optWasSet(S"liteTheme") && liteThemeG != newThemeLite) {
                  // value differs, apply it
                  optChangeAndReportError(S"liteTheme", optBoole(newThemeLite), SET_GLOBAL);
                  reset_optWasSet(S"liteTheme");
                  redraw_asap(UPD_CLEAR);
               }
            } else {
               LOG_TRN("Received RFG response: %s", tp);
               rfg_status.tr_progress = STATUS_GOT;
               fg_r = rval;
               fg_g = gval;
               fg_b = bval;
            }
         }

         // got finished code: consume it
         key_name[0] = (int)KS_EXTRA;
         key_name[1] = (int)KE_IGNORE;
         *slen = i + 1 + (tp[i] == ESC);
         set_EeglVar_string(isBg ? VV_TERMRBGRESP : VV_TERMRFGRESP, tp, *slen);
         apply_autocmds(EVENT_TERMRESPONSEALL,
                isBg ? S"background" : S"foreground", NULL, false, curBook);
         break;
      }
   }
   if (i == len) {
     LOG_TR1("not enough characters for RB");
     return FAIL;
   }
   return OK;
}

// Check for key code response from xterm:
// {lead}{flag}+r<hex bytes><{tail}
//
// {lead} can be <Esc>P or DCS
// {flag} can be '0' or '1'
// {tail} can be Esc>\ or STERM
//
// Check for resource response from xterm (and drop it):
// {lead}{flag}+R<hex bytes>=<value>{tail}
//
// Check for cursor shape response from xterm:
// {lead}1$r<digit> q{tail}
//
// {lead} can be <Esc>P or DCS
// {tail} can be <Esc>\ or STERM
//
// Consume any code that starts with "{lead}.+r" or "{lead}.$r".
private int
handle_dcs(CS tp, CS argp, int len, CS key_name, int* slen) {
   int i;

   LOG_TRN("Received DCS response: %s", (char*)tp);
   int j = 1 + (tp[0] == ESC);
   if (len < j + 3)
      i = len; // need more chars
   ei ((argp[1] != '+' && argp[1] != '$')
       || (argp[2] != 'r' && argp[2] != 'R'))
      i = 0; // no match
   ei (argp[1] == '+') {
      // key code response
      for (i = j; i < len; ++i) {
         if ((tp[i] == ESC && i + 1 < len && tp[i + 1] == '\\') || tp[i] == STERM) {
            // handle a key code response, drop a resource response
            if (i - j >= 3 && argp[2] == 'r')
               got_code_from_term(tp + j, i);
            key_name[0] = (int)KS_EXTRA;
            key_name[1] = (int)KE_IGNORE;
            *slen = i + 1 + (tp[i] == ESC);
            break;
         }
      }
   } else {
      // Probably the cursor shape response.  Make sure that "i"
      // is equal to "len" when there are not sufficient characters.
      for (i = j + 3; i < len; ++i) {
         if (  (i - j == 3 && !SAFE_isdigit(tp[i]))
            || (i - j == 4 && tp[i] != ' ')
            || (i - j == 5 && tp[i] != 'q')
            || (i - j == 6 && tp[i] != ESC && tp[i] != STERM)
         )
            break;

         if ((i - j == 6 && tp[i] == STERM) || (i - j == 7 && tp[i] == '\\')) {
            int number = argp[3] - '0';

            // 0, 1 = block blink, 2 = block
            // 3 = underline blink, 4 = underline
            // 5 = vertical bar blink, 6 = vertical bar
            number = number == 0 ? 1 : number;
            initial_cursor_shape = (number + 1) / 2;
            //The blink flag is actually inverted, compared to the value set with termCodeS[KS_SH].
            initial_cursor_shape_blink = (number & 1) ? FALSE : TRUE;
            cursorStyleRequestS.tr_progress = STATUS_GOT;
            LOG_TRN("Received cursor shape response: %s", tp);

            key_name[0] = (int)KS_EXTRA;
            key_name[1] = (int)KE_IGNORE;
            *slen = i + 1;
            set_EeglVar_string(VV_TERMSTYLERESP, tp, *slen);
            apply_autocmds(EVENT_TERMRESPONSEALL, S"cursorshape", NULL, false, curBook);
            break;
         }
      }
   }

   if (i == len) {
      // These codes arrive many together, each code can be truncated at any point.
      LOG_TR1("not enough characters for XT");
      return FAIL;
   }
   return OK;
}

// Change <xHome> to <Home>, <xUp> to <Up>, etc.
private Unt
handle_x_keys(Unt key) {
   switch (key) {
   case K_XUP:    return K_UP;
   case K_XDOWN:  return K_DOWN;
   case K_XLEFT:  return K_LEFT;
   case K_XRIGHT: return K_RIGHT;
   case K_XHOME:  return K_HOME;
   case K_ZHOME:  return K_HOME;
   case K_XEND:   return K_END;
   case K_ZEND:   return K_END;
   case K_XF1:    return K_F1;
   case K_XF2:    return K_F2;
   case K_XF3:    return K_F3;
   case K_XF4:    return K_F4;
   case K_S_XF1:  return K_S_F1;
   case K_S_XF2:  return K_S_F2;
   case K_S_XF3:  return K_S_F3;
   case K_S_XF4:  return K_S_F4;
   default:       return key;
   }
}


// Check if typeBufG.tb_buf[] contains a terminal key code.
// Check from typeBufG.tb_buf[typeBufG.currPos]
//         to typeBufG.tb_buf[typeBufG.currPos + "max_offset"].
// Return 0 for no match, -1 for partial match, > 0 for full match.
// Return KEYLEN_REMOVED when a key code was deleted.
//With a match, the match is removed, the replacement code is inserted in
//typeBufG.tb_buf[] and the number of characters in typeBufG.tb_buf[] is returned.
//When "buffer" is not NULL, buffer[bufsize] is used instead of typeBufG.tb_buf[].
//"bufLen" is then the length of the string in buffer[] and is updated for inserts and deletes.
int
check_termcode(int max_offset, CS buffer, int bufsize, OUT int* bufLen){
   CS readPos;
   int slen = 0;
   int modslen;
   int len;
   int retval = 0;
   int offset;
   Byte   keyName[2];
   Unt      modifiers;
   CS modifiers_start = NULL;
   Unt key;
   int new_slen;   // Length of what will replace the termcode
   Byte string[MAX_KEY_CODE_LEN + 1];
   int      i, j;
   int      idx = 0;

   // Speed up the checks for terminal codes by gathering all first bytes
   // used in termLeaderG[].  Often this is just a single <Esc>.
   if (needToGatherTermLeaders)
      gatherTermLeaders();

   //Check at several positions in typeBufG.c[], to catch something like "x<Up>" that can be
   //mapped. Stop at max_offset, because characters after that cannot be used for mapping, and
   //with @r commands typeBufG.c[] can become very long. This is used often, KEEP IT FAST!
   for (offset = 0; offset < max_offset; ++offset) {
      if (buffer) {
         if (offset >= *bufLen)
            break;
         readPos = buffer + offset;
         len = *bufLen - offset;
      } else {
         if (offset >= typeBufG.validLen)
            break;
         readPos = typeBufG.c + typeBufG.currPos + offset;
         len = typeBufG.validLen - offset;   // length of the input
      }

      //Don't check characters after K_SPECIAL, those are already
      //translated terminal chars (avoid translating ~@^Hx).
      if (*readPos == K_SPECIAL) {
         offset += 2;   // there are always 2 extra characters
         continue;
      }

      //Raw input from the user:
      //Skip this position if the character does not appear as the first character in
      //termCodeS. This speeds up a lot, since most recognizedCodeS start with the same
      //character (ESC or CSI).
      i = *readPos;
      Byte   *p;
      for (p = termLeaderG; *p && *p != i; ++p)
         {}
      if (*p == ZERO)
         continue;

      readPos[len] = ZERO;
      keyName[0] = ZERO;   // no key name found yet
      keyName[1] = ZERO;   // no key name found yet
      modifiers = 0;      // no modifiers yet

      {
         int  mouse_index_found = -1;

         for (idx = 0; idx < tc_len; ++idx) {
            //Ignore the entry if we are not at the start of typeBufG.c[] and there are not enough
            //characters to make a match.
            slen = recognizedCodeS[idx].len;
            modifiers_start = NULL;
            if (STRNCMP(recognizedCodeS[idx].code, readPos, (Unt)(slen > len ? len : slen)) == 0) {
               int looks_like_mouse_start = FALSE;

               if (len < slen)      // got a partial sequence
                  return -1;      // need to get more chars

               //When found a keypad key, check if there is another key
               //that matches and use that one.  This makes <Home> to be
               //found instead of <kHome> when they produce the same key code.
               if (recognizedCodeS[idx].name[0] == 'K' && EE_ISDIGIT(recognizedCodeS[idx].name[1])) {
                  for (j = idx + 1; j < tc_len; ++j) {
                     if (recognizedCodeS[j].len == slen
                            && STRNCMP(recognizedCodeS[idx].code, recognizedCodeS[j].code, slen) == 0
                     ) {
                        idx = j;
                        break;
                     }
                  }
               }

               if (slen == 2 && len > 2
                  && recognizedCodeS[idx].code[0] == ESC
                  && recognizedCodeS[idx].code[1] == '['
               ){
                 //The mouse termcode "ESC [" is also the prefix of "ESC [ I" (focus gained)
                 //and other keys. Check some more bytes to find out.
                 if (!SAFE_isdigit(readPos[2])) {
                    //ESC [ without number following: Only use it when
                    //there is no other match.
                    looks_like_mouse_start = TRUE;
                 } ei (recognizedCodeS[idx].name[0] == KS_DEC_MOUSE) {
                     Byte  *nr = readPos + 2;
                     int       count = 0;

                    //If a digit is following it could be a key with modifier, e.g., ESC [ 1;2P.
                    //Can be confused with DEC_MOUSE, which requires four numbers following.
                    //If not then it can't be a DEC_MOUSE code.
                    for (;;) {
                        ++count;
                        (void)parseLong(&nr);
                        if (nr >= readPos + len)
                           return -1;   // partial sequence
                        if (*nr != ';')
                           break;
                        ++nr;
                        if (nr >= readPos + len)
                           return -1;   // partial sequence
                     }
                     if (count < 4)
                        continue;   // no match
                  }
               }
               if (looks_like_mouse_start) {
                  // Only use it when there is no other match.
                  if (mouse_index_found < 0)
                     mouse_index_found = idx;
               } else {
                  keyName[0] = recognizedCodeS[idx].name[0];
                  keyName[1] = recognizedCodeS[idx].name[1];
                  break;
               }
            }

            //Check for code with modifier, like xterm uses:
            //<Esc>[123;*X  (modslen == slen - 3)
            //<Esc>[@;*X    (matches <Esc>[X and <Esc>[1;9X )
            //Also <Esc>O*X and <M-O>*X (modslen == slen - 2).
            //When there is a modifier the * matches a number.
            //When there is no modifier the ;* or * is omitted.
            if (recognizedCodeS[idx].modlen > 0 && mouse_index_found < 0) {
                modslen = recognizedCodeS[idx].modlen;
                if (STRNCMP(recognizedCodeS[idx].code, readPos, (Unt)(modslen > len ? len : modslen)) == 0) {
                  int n;

                  if (len <= modslen)   // got a partial sequence
                     return -1;      // need to get more chars

                  if (readPos[modslen] == recognizedCodeS[idx].code[slen - 1])
                     // no modifiers
                     slen = modslen + 1;
                  ei (readPos[modslen] != ';' && modslen == slen - 3)
                     // no match for "code;*X" with "code;"
                     continue;
                  ei (recognizedCodeS[idx].code[modslen] == '@'
                         && (readPos[modslen] != '1'
                               || readPos[modslen + 1] != ';'))
                     // no match for "<Esc>[@" with "<Esc>[1;"
                     continue;
                  else {
                     //Skip over the digits, the final char must
                     //follow. URXVT can use a negative value, thus also accept '-'.
                     for (j = slen - 2; j < len && (SAFE_isdigit(readPos[j])
                            || readPos[j] == '-' || readPos[j] == ';'); ++j)
                         {}
                     ++j;
                     if (len < j)   // got a partial sequence
                        return -1;   // need to get more chars
                     if (readPos[j - 1] != recognizedCodeS[idx].code[slen - 1])
                        continue;   // no match

                     modifiers_start = readPos + slen - 2;

                     // Match!  Convert modifier bits.
                     n = atoi((char *)modifiers_start);
                     modifiers |= decode_modifiers(n);

                     slen = j;
                  }
                  keyName[0] = recognizedCodeS[idx].name[0];
                  keyName[1] = recognizedCodeS[idx].name[1];
                  break;
                }
            }
         }
         if (idx == tc_len && mouse_index_found >= 0) {
            keyName[0] = recognizedCodeS[mouse_index_found].name[0];
            keyName[1] = recognizedCodeS[mouse_index_found].name[1];
         }
      }

      if (keyName[0] == ZERO) {
         // Mouse codes of DEC and pterm start with <ESC>[.  When detecting the start of these
         // mouse codes they might as well be another key code or terminal response.
         Byte *argp = readPos + (readPos[0] == ESC ? 2 : 1);
         // Check for responses from the terminal starting with {lead}:
         // "<Esc>[" or CSI followed by [0-9>?].
         // Also for function keys without a modifier:
         // "<Esc>[" or CSI followed by [ABCDEFHPQRS].
         //
         // - Xterm version string: {lead}>{x};{vers};{y}c
         //   Also eat other possible responses to t_RV, rxvt returns
         //   "{lead}?1;2c".
         //
         // - Response to XTQMODKEYS: "{lead} > 4 ; Pv m".
         //
         // - Cursor position report: {lead}{row};{col}R
         //   The final byte must be 'R'. It is used for checking the ambiguous-width character state
         //
         // - window position reply: {lead}3;{x};{y}t
         //
         // - key with modifiers when modifyOtherKeys is enabled:
         //       {lead}27;{modifier};{key}~
         //       {lead}{key};{modifier}u
         if (((readPos[0] == ESC && len >= 3 && readPos[1] == '[')
            || (readPos[0] == CSI && len >= 2))
                && firstOccurrence((CS)"0123456789>?ABCDEFHPQRS", *argp) != NULL
         ){
            int resp = handleControlSequenceIntroducer(
                  readPos, len, argp, offset, buffer, bufsize, bufLen, keyName, &slen
            );
            if (resp != 0) {
      #ifdef DEBUG_TERMRESPONSE
               if (resp == -1)
                  LOG_TR1("Not enough characters for CSI sequence");
      #endif
               return resp;
            }
         }
         //Check for fore/background color response from the terminal,
         //starting} with <Esc>] or OSC
         ei ((termCodeS[KS_RBG] != Em || termCodeS[KS_RFG] != Em)
              && ((readPos[0] == ESC && len >= 2 && readPos[1] == ']') || readPos[0] == OSC)
         ) {
            if (handle_osc(readPos, argp, len, keyName, &slen) == FAIL)
               return -1;
         }
         //Check for key code response from xterm, starting with <Esc>P or DCS
         //It would only be needed with this condition:
         //       (check_for_codes || cursorStyleRequestS.tr_progress == STATUS_SENT)
         //Now this is always done so that DCS codes don't mess up things.
         ei ((readPos[0] == ESC && len >= 2 && readPos[1] == 'P') || readPos[0] == DCS) {
            if (handle_dcs(readPos, argp, len, keyName, &slen) == FAIL)
               return -1;
         }
      }

      if (keyName[0] == ZERO)
          continue;       // No match at this position, try next one

      // We only get here when we have a complete termcode match

      // If it is a mouse click, get the coordinates.
      if (keyName[0] == KS_MOUSE
         || keyName[0] == KS_SGR_MOUSE
         || keyName[0] == KS_SGR_MOUSE_RELEASE)
      {
         if (check_termcode_mouse(keyName, &modifiers) == -1)
            return -1;
      }

      // Handle FocusIn/FocusOut event sequences reported by XTerm. (CSI I/CSI O)
      if (keyName[0] == KS_EXTRA) {
         if (keyName[1] == KE_FOCUSGAINED) {
            if (!focus_state) {
               ui_focus_change(TRUE);
               did_cursorhold = TRUE;
               focus_state = TRUE;
            }
            keyName[1] = (int)KE_IGNORE;
          } ei (keyName[1] == KE_FOCUSLOST) {
            if (focus_state) {
               ui_focus_change(FALSE);
               did_cursorhold = TRUE;
               focus_state = FALSE;
            }
            keyName[1] = (int)KE_IGNORE;
         }
      }

      // Change <xHome> to <Home>, <xUp> to <Up>, etc.
      key = handle_x_keys(TERMCAP2KEY(keyName[0], keyName[1]));

      // Add any modifier codes to our string.
      new_slen = modifiers2keycode(modifiers, &key, string);

      // Finally, add the special key code to our string
      keyName[0] = KEY2TERMCAP0(key);
      keyName[1] = KEY2TERMCAP1(key);
      if (keyName[0] == KS_KEY) {
         //from ":set <M-b>=xx"
         new_slen += mb_char2bytes(keyName[1], string + new_slen);
      } ei (new_slen == 0 && keyName[0] == KS_EXTRA && keyName[1] == KE_IGNORE) {
         //Do not put K_IGNORE into the buffer, do return KEYLEN_REMOVED to indicate what happened
         retval = KEYLEN_REMOVED;
      } else {
         string[new_slen++] = K_SPECIAL;
         string[new_slen++] = keyName[0];
         string[new_slen++] = keyName[1];
      }
      if (termPutStrIntoTypebuf(offset, slen, string, new_slen, buffer, bufsize, bufLen
          ) == FAIL)
         return -1;
      return retval == 0 ? (len + new_slen - slen + offset) : retval;
   }

   LOG_TR1("normal character");

   return 0;             // no match found
}

//Get the text foreground color, if known.
void
term_get_fg_color(Byte* r, Byte* g, Byte* b) {
   if (rfg_status.tr_progress != STATUS_GOT)
      return;

   *r = fg_r;
   *g = fg_g;
   *b = fg_b;
}

//Get the text background color, if known.
void
term_get_bg_color(Byte* r, Byte* g, Byte* b) {
   if (backgroundColorRequestS.tr_progress != STATUS_GOT)
      return;

   *r = bg_r;
   *g = bg_g;
   *b = bg_b;
}

//Try to get the code for "t_kb" from the stty setting
//
//Even if terminfo claims a backspace key, the user's setting *should* prevail. stty knows more
//about reality than terminfo does, and if somebody's usual erase key is DEL, they're going to get
//really annoyed if their erase key starts doing forward deletes for no reason. (Eric Fischer)
void
get_stty(void) {
   TtyInfo info;
   if (get_tty_info(read_cmd_fd, OUT &info) != OK)
      return;

   Byte   buffer[2];
   extraInterruptCharG = info.interrupt;
   buffer[0] = info.backspace;
   buffer[1] = ZERO;
   add_termcode((CS)"kb", buffer, false);

   // If <BS> and <DEL> are now the same, redefine <DEL>.
   CS p = find_termcode((CS)"kD");
   if (p && p[0] == buffer[0] && p[1] == buffer[1])
      do_fixdel(NULL);
}

private int
mch_tcgetattr(int fd, TermIos* term){
   return (fd < 0) ? -1 : tcgetattr(fd, term);
}

//Obtain the characters that Backspace and Enter produce on "fd". Return OK or FAIL.
int
get_tty_info(int fd, TtyInfo* info) {
   TermIos keys;

   if (mch_tcgetattr(fd, &keys) != -1) {
      info->backspace = keys.c_cc[VERASE];
      info->interrupt = keys.c_cc[VINTR];
      if (keys.c_iflag & ICRNL)
         info->enter = NL;
      else
         info->enter = ENTER;
      if (keys.c_oflag & ONLCR)
         info->nl_does_cr = TRUE;
      else
         info->nl_does_cr = FALSE;
      return OK;
   }
   return FAIL;
}

void
mch_termSetMode(TermInputMode tmode) {
   static int first = TRUE;

   static TermIos told;
   TermIos tnew;

   if (first) {
      first = FALSE;
      mch_tcgetattr(read_cmd_fd, &told);
   }

   tnew = told;
   if (tmode == TMODE_RAW) {
      // ~ICRNL enables typing ^V^M
      // ~IXON disables CTRL-S stopping output, so that it can be mapped.
      tnew.c_iflag &= ~(ICRNL | (termCodeS[KS_XON] == Em ? IXON : 0));
      tnew.c_lflag &= ~(ICANON | ECHO | ISIG | ECHOE
# if defined(IEXTEN)
             | IEXTEN
# endif
               );
# ifdef ONLCR
      // Don't map NL -> CR NL, we do it ourselves. Also disable expanding tabs if possible.
#  ifdef XTABS
      tnew.c_oflag &= ~(ONLCR | XTABS);
#  else
#   ifdef TAB3
      tnew.c_oflag &= ~(ONLCR | TAB3);
#   else
      tnew.c_oflag &= ~ONLCR;
#   endif
#  endif
# endif
      tnew.c_cc[VMIN] = 1;      // return after 1 char
      tnew.c_cc[VTIME] = 0;      // don't wait
   } ei (tmode == TMODE_SLEEP) {
      tnew.c_lflag &= ~(ICANON | ECHO);
      tnew.c_cc[VMIN] = 1;   // return after 1 char
      tnew.c_cc[VTIME] = 0;  // don't wait
   }

   {
   int   n = 10;

   // A signal may cause tcsetattr() to fail (e.g., SIGCONT).  Retry a few times.
   while (tcsetattr(read_cmd_fd, TCSANOW, &tnew) == -1 && errno == EINTR && n > 0)
       --n;
   }
   mch_cur_tmode = tmode;
}

// Set the mouse termcode, depending on the 'term' and 'ttymouse' options.
void
check_mouse_termcode(void) {
   set_mouse_termcode(KS_SGR_MOUSE, S"\233<*M");
   set_mouse_termcode(KS_SGR_MOUSE_RELEASE, S"\233<*m");

   mch_setmouse(FALSE);
   setmouse();
}

//}}}
//{{{modifier key tables

// functions that use lookup tables for various things, generally to do with special key codes.

// Some useful tables.
private struct modmasktable {
   Short modMaskG; // Bit-mask for particular key modifier
   Short mod_flag; // Bit(s) for particular key modifier
   Byte name;      // Single letter name of modifier
} modMaskTable[] = {
   {MOD_MASK_ALT,         MOD_MASK_ALT,    'M'},
   {MOD_MASK_META,        MOD_MASK_META,   'T'},
   {MOD_MASK_CTRL,        MOD_MASK_CTRL,   'C'},
   {MOD_MASK_SHIFT,       MOD_MASK_SHIFT,  'S'},
   {MOD_MASK_MULTI_CLICK, MOD_MASK_2CLICK, '2'},
   {MOD_MASK_MULTI_CLICK, MOD_MASK_3CLICK, '3'},
   {MOD_MASK_MULTI_CLICK, MOD_MASK_4CLICK, '4'},
   // 'A' must be the last one
   {MOD_MASK_ALT,         MOD_MASK_ALT,    'A'},
   {0, 0, ZERO}
   // NOTE: when adding an entry, update MAX_KEY_NAME_LEN!
};

//Shifted key terminal codes and their unshifted equivalent.
//Don't add mouse codes here, they are handled separately!
#define MOD_KEYS_ENTRY_SIZE 5

private Byte modifier_keys_table[] = {
//  mod mask       with modifier      without modifier
   MOD_MASK_SHIFT, '&', '9',         '@', '1',   // begin
   MOD_MASK_SHIFT, '&', '0',         '@', '2',   // cancel
   MOD_MASK_SHIFT, '*', '1',         '@', '4',   // command
   MOD_MASK_SHIFT, '*', '2',         '@', '5',   // copy
   MOD_MASK_SHIFT, '*', '3',         '@', '6',   // create
   MOD_MASK_SHIFT, '*', '4',         'k', 'D',   // delete char
   MOD_MASK_SHIFT, '*', '5',         'k', 'L',   // delete line
   MOD_MASK_SHIFT, '*', '7',         '@', '7',   // end
   MOD_MASK_CTRL,  KS_EXTRA, (int)KE_C_END,   '@', '7',   // end
   MOD_MASK_SHIFT, '*', '9',         '@', '9',   // exit
   MOD_MASK_SHIFT, '*', '0',         '@', '0',   // find
   MOD_MASK_SHIFT, '#', '1',         '%', '1',   // help
   MOD_MASK_SHIFT, '#', '2',         'k', 'h',   // home
   MOD_MASK_CTRL,  KS_EXTRA, (int)KE_C_HOME,   'k', 'h',   // home
   MOD_MASK_SHIFT, '#', '3',         'k', 'I',   // insert
   MOD_MASK_SHIFT, '#', '4',         'k', 'l',   // left arrow
   MOD_MASK_CTRL,  KS_EXTRA, (int)KE_C_LEFT,   'k', 'l',   // left arrow
   MOD_MASK_SHIFT, '%', 'a',         '%', '3',   // message
   MOD_MASK_SHIFT, '%', 'b',         '%', '4',   // move
   MOD_MASK_SHIFT, '%', 'c',         '%', '5',   // next
   MOD_MASK_SHIFT, '%', 'd',         '%', '7',   // options
   MOD_MASK_SHIFT, '%', 'e',         '%', '8',   // previous
   MOD_MASK_SHIFT, '%', 'f',         '%', '9',   // print
   MOD_MASK_SHIFT, '%', 'g',         '%', '0',   // redo
   MOD_MASK_SHIFT, '%', 'h',         '&', '3',   // replace
   MOD_MASK_SHIFT, '%', 'i',         'k', 'r',   // right arr.
   MOD_MASK_CTRL,  KS_EXTRA, (int)KE_C_RIGHT,   'k', 'r',   // right arr.
   MOD_MASK_SHIFT, '%', 'j',         '&', '5',   // resume
   MOD_MASK_SHIFT, '!', '1',         '&', '6',   // save
   MOD_MASK_SHIFT, '!', '2',         '&', '7',   // suspend
   MOD_MASK_SHIFT, '!', '3',         '&', '8',   // undo
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_UP,   'k', 'u',   // up arrow
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_DOWN,   'k', 'd',   // down arrow

                        // vt100 F1
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_XF1,   KS_EXTRA, (int)KE_XF1,
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_XF2,   KS_EXTRA, (int)KE_XF2,
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_XF3,   KS_EXTRA, (int)KE_XF3,
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_XF4,   KS_EXTRA, (int)KE_XF4,

   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F1,   'k', '1',   // F1
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F2,   'k', '2',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F3,   'k', '3',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F4,   'k', '4',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F5,   'k', '5',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F6,   'k', '6',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F7,   'k', '7',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F8,   'k', '8',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F9,   'k', '9',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F10,   'k', ';',   // F10

   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F11,   'F', '1',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F12,   'F', '2',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F13,   'F', '3',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F14,   'F', '4',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F15,   'F', '5',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F16,   'F', '6',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F17,   'F', '7',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F18,   'F', '8',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F19,   'F', '9',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F20,   'F', 'A',

   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F21,   'F', 'B',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F22,   'F', 'C',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F23,   'F', 'D',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F24,   'F', 'E',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F25,   'F', 'F',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F26,   'F', 'G',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F27,   'F', 'H',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F28,   'F', 'I',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F29,   'F', 'J',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F30,   'F', 'K',

   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F31,   'F', 'L',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F32,   'F', 'M',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F33,   'F', 'N',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F34,   'F', 'O',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F35,   'F', 'P',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F36,   'F', 'Q',
   MOD_MASK_SHIFT, KS_EXTRA, (int)KE_S_F37,   'F', 'R',

                        // TAB pseudo code
   MOD_MASK_SHIFT, 'k', 'B', KS_EXTRA, (int)KE_TAB,

   ZERO
};

//}}}
//{{{codes and special chars

typedef struct {
   Boole enabled;       // is this entry available?
   int key;          // special key code or ascii value
   Text name;          // name of key
   Boole is_alt;          // is an alternative name
} KeyNameEntry;

private KeyNameEntry keyNamesTable[] = {
// Must be sorted by the 'name.c' field in ascending order because it is used by bsearch()!
   {true, K_BS, tConst("BackSpace"), true},
   {true, '|', tConst("Bar"), false},
   {true, K_BS, tConst("BS"), false},
   {true, '\\', tConst("Bslash"), false},
   {true, K_COMMAND, tConst("Cmd"), false},
   {true, ENTER, tConst("CR"), false},
   {true, CSI, tConst("CSI"), false},
   {true, K_CURSORHOLD, tConst("CursorHold"), false},
   {false, K_DEC_MOUSE, tConst("DecMouse"), false},
   {true, K_DEL, tConst("Del"), false},
   {true, K_DEL, tConst("Delete"), true},
   {true, K_DOWN, tConst("Down"), false},
   {true, K_DROP, tConst("Drop"), false},
   {true, K_END, tConst("End"), false},
   {true, ENTER, tConst("Enter"), true},
   {true, ESC, tConst("Esc"), false},

   {true, K_F1, tConst("F1"), false},
   {true, K_F10, tConst("F10"), false},
   {true, K_F11, tConst("F11"), false},
   {true, K_F12, tConst("F12"), false},
   {true, K_F13, tConst("F13"), false},
   {true, K_F14, tConst("F14"), false},
   {true, K_F15, tConst("F15"), false},
   {true, K_F16, tConst("F16"), false},
   {true, K_F17, tConst("F17"), false},
   {true, K_F18, tConst("F18"), false},
   {true, K_F19, tConst("F19"), false},

   {true, K_F2, tConst("F2"), false},
   {true, K_F20, tConst("F20"), false},
   {true, K_F21, tConst("F21"), false},
   {true, K_F22, tConst("F22"), false},
   {true, K_F23, tConst("F23"), false},
   {true, K_F24, tConst("F24"), false},
   {true, K_F25, tConst("F25"), false},
   {true, K_F26, tConst("F26"), false},
   {true, K_F27, tConst("F27"), false},
   {true, K_F28, tConst("F28"), false},
   {true, K_F29, tConst("F29"), false},

   {true, K_F3, tConst("F3"), false},
   {true, K_F30, tConst("F30"), false},
   {true, K_F31, tConst("F31"), false},
   {true, K_F32, tConst("F32"), false},
   {true, K_F33, tConst("F33"), false},
   {true, K_F34, tConst("F34"), false},
   {true, K_F35, tConst("F35"), false},
   {true, K_F36, tConst("F36"), false},
   {true, K_F37, tConst("F37"), false},

   {true, K_F4, tConst("F4"), false},
   {true, K_F5, tConst("F5"), false},
   {true, K_F6, tConst("F6"), false},
   {true, K_F7, tConst("F7"), false},
   {true, K_F8, tConst("F8"), false},
   {true, K_F9, tConst("F9"), false},

   {true, K_FOCUSGAINED, tConst("FocusGained"), false},
   {true, K_FOCUSLOST, tConst("FocusLost"), false},
   {true, K_HELP, tConst("Help"), false},
   {true, K_HOME, tConst("Home"), false},
   {true, K_IGNORE, tConst("Ignore"), false},
   {true, K_INS, tConst("Ins"), true},
   {true, K_INS, tConst("Insert"), false},
   { false, K_JSBTERM_MOUSE, tConst("JsbMouse"), false},
   {true, K_K0, tConst("k0"), false},
   {true, K_K1, tConst("k1"), false},
   {true, K_K2, tConst("k2"), false},
   {true, K_K3, tConst("k3"), false},
   {true, K_K4, tConst("k4"), false},
   {true, K_K5, tConst("k5"), false},
   {true, K_K6, tConst("k6"), false},
   {true, K_K7, tConst("k7"), false},
   {true, K_K8, tConst("k8"), false},
   {true, K_K9, tConst("k9"), false},

   {true, K_KDEL, tConst("kDel"), false},
   {true, K_KDIVIDE, tConst("kDivide"), false},
   {true, K_KEND, tConst("kEnd"), false},
   {true, K_KENTER, tConst("kEnter"), false},
   {true, K_KHOME, tConst("kHome"), false},
   {true, K_KINS, tConst("kInsert"), false},
   {true, K_KMINUS, tConst("kMinus"), false},
   {true, K_KMULTIPLY, tConst("kMultiply"), false},
   {true, K_KPAGEDOWN, tConst("kPageDown"), false},
   {true, K_KPAGEUP, tConst("kPageUp"), false},
   {true, K_KPLUS, tConst("kPlus"), false},
   {true, K_KPOINT, tConst("kPoint"), false},
   {true, K_LEFT, tConst("Left"), false},
   {true, K_LEFTDRAG, tConst("LeftDrag"), false},
   {true, K_LEFTMOUSE, tConst("LeftMouse"), false},
   {true, K_LEFTMOUSE_NM, tConst("LeftMouseNM"), false},
   {true, K_LEFTRELEASE, tConst("LeftRelease"), false},
   {true, K_LEFTRELEASE_NM, tConst("LeftReleaseNM"), false},
   {true, NL, tConst("LF"), true},
   {true, NL, tConst("LineFeed"), true},
   {true, '<', tConst("lt"), false},
   {true, K_MIDDLEDRAG, tConst("MiddleDrag"), false},
   {true, K_MIDDLEMOUSE, tConst("MiddleMouse"), false},
   {true, K_MIDDLERELEASE, tConst("MiddleRelease"), false},
   {true, K_MOUSE, tConst("Mouse"), false},
   {true, K_MOUSEDOWN, tConst("MouseDown"), true},
   {true, K_MOUSEMOVE, tConst("MouseMove"), false},
   {true, K_MOUSEUP, tConst("MouseUp"), true},
   { false, K_NETTERM_MOUSE, tConst("NetMouse"), false},
   {true, NL, tConst("NewLine"), true},
   {true, NL, tConst("NL"), false},
   {true, K_ZERO, tConst("ZERO"), false},
   {true, K_PAGEDOWN, tConst("PageDown"), false},
   {true, K_PAGEUP, tConst("PageUp"), false},
   {true, K_PE, tConst("PasteEnd"), false},
   {true, K_PS, tConst("PasteStart"), false},
   {true, K_PLUG, tConst("Plug"), false},
   {true, ENTER, tConst("Return"), true},
   {true, K_RIGHT, tConst("Right"), false},
   {true, K_RIGHTDRAG, tConst("RightDrag"), false},
   {true, K_RIGHTMOUSE, tConst("RightMouse"), false},
   {true, K_RIGHTRELEASE, tConst("RightRelease"), false},
   {true, K_SCRIPT_COMMAND, tConst("ScriptCmd"), false},
   {true, K_MOUSEUP, tConst("ScrollWheelDown"), false},
   {true, K_MOUSERIGHT, tConst("ScrollWheelLeft"), false},
   {true, K_MOUSELEFT, tConst("ScrollWheelRight"), false},
   {true, K_MOUSEDOWN, tConst("ScrollWheelUp"), false},
   {true, K_SGR_MOUSE, tConst("SgrMouse"), false},
   {true, K_SGR_MOUSERELEASE, tConst("SgrMouseRelease"), false},
   {true, K_SNR, tConst("SNR"), false},
   {true, ' ', tConst("Space"), false},
   {true, TAB, tConst("Tab"), false},
   {true, K_TAB, tConst("Tab"), false},
   {true, K_UNDO, tConst("Undo"), false},
   {true, K_UP, tConst("Up"), false},
   { false, K_URXVT_MOUSE, tConst("UrxvtMouse"), false},
   {true, K_X1DRAG, tConst("X1Drag"), false},
   {true, K_X1MOUSE, tConst("X1Mouse"), false},
   {true, K_X1RELEASE, tConst("X1Release"), false},
   {true, K_X2DRAG, tConst("X2Drag"), false},
   {true, K_X2MOUSE, tConst("X2Mouse"), false},
   {true, K_X2RELEASE, tConst("X2Release"), false},
   {true, K_CSI, tConst("xCSI"), false},
   {true, K_XDOWN, tConst("xDown"), false},
   {true, K_XEND, tConst("xEnd"), false},
   {true, K_XF1, tConst("xF1"), false},
   {true, K_XF2, tConst("xF2"), false},
   {true, K_XF3, tConst("xF3"), false},
   {true, K_XF4, tConst("xF4"), false},
   {true, K_XHOME, tConst("xHome"), false},
   {true, K_XLEFT, tConst("xLeft"), false},
   {true, K_XRIGHT, tConst("xRight"), false},
   {true, K_XUP, tConst("xUp"), false},
   {true, K_ZEND, tConst("zEnd"), false},
   {true, K_ZHOME, tConst("zHome"), false}
    // NOTE: When adding a long name update MAX_KEY_NAME_LEN.
};

CS
get_key_name(int i) {
   if (i < 0 || i >= (int)ARRAY_LENGTH(keyNamesTable))
      return NULL;

   return keyNamesTable[i].name.c;
}


//Replace any terminal code strings in from[] with the equivalent internal Eegl representation. 
//This is used for the "from" and "to" part of a mapping, and the "to" part of a menu command.
//Any strings like "<C-UP>" are also replaced. K_SPECIAL by itself is replaced by K_SPECIAL 
//KS_SPECIAL KE_FILLER.
//
//The replacement is done in result[] and finally copied into allocated memory. If this all works 
//well *bufP is set to the allocated memory and a pointer to it is returned. If something fails 
//*bufP is set to NULL and from is returned.
//
//CTRL-V characters are removed.  When "flags" has REPTERM_FROM_PART, a trailing CTRL-V is included,
//otherwise it is removed (for ":map xx ^V", maps xx to nothing).  When 'cpoptions' does not 
//contain 'B', a backslash can be used instead of a CTRL-V.
//
//Flags:
//  REPTERM_FROM_PART   see above
//  REPTERM_DO_LT   also translate <lt>
//  REPTERM_SPECIAL   always accept <key> notation
//  REPTERM_NO_SIMPLIFY   do not simplify <C-H> to 0x08 and set 8th bit for <A-x>
//
//"didSimplify" is set when some <C-H> or <A-x> code was simplified, unless it is NULL.
CS
replace_termcodes(
   CS from,
   CS* bufP,
   ScriptId sid_arg UNUSED,   // script ID to use for <SID>, or 0 to use scriptPosG
   Unt flags,
   OUT Boole* didSimplify,
   Boole recognizeRawKeycodes
){
   int      i;
   int      slen;
   int      key;
   Unt   dlen = 0;
   ArrayList   ga;

   CS src = from;

   //Allocate space for the translation.  Worst case a single character is
   // replaced by 6 bytes (shifted special key), plus a ZERO at the end.
   // In the rare case more might be needed ga_grow() must be called again.
   ga_init2(&ga, 1L, 100);
   if (ga_grow(&ga, (int)(STRLEN(src) * 6 + 1)) == FAIL) { // out of memory
      *bufP = NULL;
      return from;
   }
   CS result = ga.c;   // buffer for resulting string

   // Check for #n at start only: function key n
   if ((flags & REPTERM_FROM_PART) && src[0] == '#' && EE_ISDIGIT(src[1])) {
      result[dlen++] = K_SPECIAL;
      result[dlen++] = 'k';
      if (src[1] == '0')
         result[dlen++] = ';';   // #0 is F10 is "k;"
      else
         result[dlen++] = src[1];   // #3 is F3 is "k3"
      src += 2;
   }

   // Copy each byte from *from to result[dlen]
   while (*src != ZERO) {
      //check for special key codes, like "<C-S-LeftMouse>"
      if ((flags & REPTERM_DO_LT) || STRNCMP(src, "<lt>", 4) != 0) {
         // Change <SID>Func to K_SNR <script-nr> _Func.  This name is used
         // for script-local user functions.
         // (room: 5 * 6 = 30 bytes; needed: 3 + <nr> + 1 <= 14)
         // Also change <SID>name.Func to K_SNR <import-script-nr> _Func.
         // Only if "name" is recognized as an import.
         if (STRNICMP(src, "<SID>", 5) == 0) {
            if (sid_arg < 0 || (sid_arg == 0 && scriptPosG.sid <= 0))
               emsg(_(e_using_sid_not_in_script_context));
            else {
               long    sid = sid_arg != 0 ? sid_arg : scriptPosG.sid;

               src += 5;

               result[dlen++] = K_SPECIAL;
               result[dlen++] = (int)KS_EXTRA;
               result[dlen++] = (int)KE_SNR;
               sprintf((char *)result + dlen, "%ld", sid);
               dlen += STRLEN(result + dlen);
               result[dlen++] = '_';
               continue;
            }
         }
         int fsk_flags = FSK_KEYCODE
           | ((flags & REPTERM_NO_SIMPLIFY) ? 0 : FSK_SIMPLIFY)
           | ((flags & REPTERM_FROM_PART) ? FSK_FROM_PART : 0);
         slen = trans_special(&src, result + dlen, fsk_flags, TRUE, OUT didSimplify);
         if (slen > 0) {
            dlen += slen;
            continue;
         }
     }

     //See if it's an actual key-code. Note that this is also checked after replacing the <> form.
     //Single character codes are NOT replaced (e.g. ^H or DEL), because it could be a character
     //in the file.
     if (!recognizeRawKeycodes) {
        i = find_term_bykeys(src);
        if (i >= 0) {
           result[dlen++] = K_SPECIAL;
           result[dlen++] = recognizedCodeS[i].name[0];
           result[dlen++] = recognizedCodeS[i].name[1];
           src += recognizedCodeS[i].len;
           // If terminal code matched, continue after it.
           continue;
        }
     }

     CS p;
     Byte len;

     //Replace <Leader> by the value of "mapleader".
     //Replace <LocalLeader> by the value of "maplocalleader".
     //If "mapleader" or "maplocalleader" isn't set use a backslash.
     if (STRNICMP(src, "<Leader>", 8) == 0) {
        len = 8;
        p = get_var_value(S"g:mapleader");
     } ei (STRNICMP(src, "<LocalLeader>", 13) == 0) {
        len = 13;
        p = get_var_value(S"g:maplocalleader");
     } else {
        len = 0;
        p = NULL;
     }
     CS s;
     if (len != 0) {
        // Allow up to 8 * 6 characters for "mapleader".
        if (p == NULL || *p == ZERO || STRLEN(p) > 8 * 6)
           s = (CS)"\\";
        else
           s = p;
        while (*s != ZERO)
           result[dlen++] = *s++;
        src += len;
        continue;
     }

     //Remove CTRL-V and ignore the next character.
     //For "from" side the CTRL-V at the end is included, for the "to" part it is removed.
     key = *src;
     if (key == Ctrl_V) {
        ++src;            // skip CTRL-V or backslash
        if (*src == ZERO) {
           if (flags & REPTERM_FROM_PART)
              result[dlen++] = key;
           break;
        }
     }

      // skip multibyte char correctly
      for (i = utfCharLen(src); i > 0; --i) {
         //If the character is K_SPECIAL, replace it with K_SPECIAL KS_SPECIAL KE_FILLER.
         //If compiled with the GUI replace CSI with K_CSI.
         if (*src == K_SPECIAL) {
            result[dlen++] = K_SPECIAL;
            result[dlen++] = KS_SPECIAL;
            result[dlen++] = KE_FILLER;
         } else
            result[dlen++] = *src;
         ++src;
      }
   }
   result[dlen] = ZERO;

   // Copy the new string to allocated memory.
   *bufP = copyStr(result);
   eeglFree(result);
   return *bufP;
}

//Check if there is a special key code for "key" that includes the modifiers specified.
Unt
simplify_key(Unt key, Unt* modifiers) {
   if (!(*modifiers & (MOD_MASK_SHIFT | MOD_MASK_CTRL)))
      return key;

   // TAB is a special case
   if (key == TAB && (*modifiers & MOD_MASK_SHIFT)) {
      *modifiers &= ~MOD_MASK_SHIFT;
      return K_S_TAB;
   }
   int key0 = KEY2TERMCAP0(key);
   int key1 = KEY2TERMCAP1(key);
   for (int i = 0; modifier_keys_table[i] != ZERO; i += MOD_KEYS_ENTRY_SIZE) {
      if (key0 == modifier_keys_table[i + 3]
         && key1 == modifier_keys_table[i + 4]
         && (*modifiers & modifier_keys_table[i])
      ) {
          *modifiers &= ~modifier_keys_table[i];
          return TERMCAP2KEY(modifier_keys_table[i + 1], modifier_keys_table[i + 2]);
      }
   }
   return key;
}

//Find a termcode with keys 'src' (must be ZERO terminated).
//Return the index in recognizedCodeS[], or -1 if not found.
private Unt
find_term_bykeys(CS src) {
   int slen = (int)STRLEN(src);
   for (int i = 0; i < tc_len; ++i) {
      if (slen == recognizedCodeS[i].len && STRNCMP(recognizedCodeS[i].code, src, (Unt)slen) == 0)
          return i;
   }
   return UNT;
}

// Gather the first characters in the terminal key codes into a string.
// Used to speed up check_termcode().
private void
gatherTermLeaders(void) {
   int       len = 0;
   if (check_for_codes || termCodeS[KS_CRS] != Em) {
      termLeaderG[len] = DCS; // the termcode response starts with DCS in 8-bit mode
      len++;
   }
   termLeaderG[len] = ZERO;

   for (int i = 0; i < tc_len; ++i) {
      if (firstOccurrence(termLeaderG, recognizedCodeS[i].code[0]) == NULL) {
         termLeaderG[len] = recognizedCodeS[i].code[0];
         len++;
         termLeaderG[len] = ZERO;
      }
   }

   needToGatherTermLeaders = FALSE;
}

//Show all recognizedCodeS (for ":set termcap")
//This code looks a lot like showoptions(), but is different. "flags" can have OPT_ONECOLUMN.
void
show_termcodes(Unt flags) {
   int col;
   int item_count;
   int run;
   int row, rows;
   int cols;
   int i;
   int len;

#define INC3 27       // try to make three columns
#define INC2 40       // try to make two columns
#define GAP 2       // spaces between columns

   if (tc_len == 0)       // no terminal codes (must be GUI)
      return;
   Arr(int) items = ALLOC_MULT(int, tc_len);

   // Highlight title
   msg_puts_title(_("\n--- Terminal keys ---"));

   //Do the loop three times:
   //1. display the short items (non-strings and short strings)
   //2. display the medium items (medium length strings)
   //3. display the long items (remaining strings)
   //When "flags" has OPT_ONECOLUMN do everything in 3.
   for (run = (flags & OPT_ONECOLUMN) ? 3 : 1; run <= 3 && !gotInterruptG; ++run) {
      //collect the items in items[]
      item_count = 0;
      for (i = 0; i < tc_len; i++) {
         len = show_one_termcode(recognizedCodeS[i].name, recognizedCodeS[i].code, FALSE);
         if ((flags & OPT_ONECOLUMN) ||
                (len <= INC3 - GAP 
                 ? run == 1
                 : (len <= INC2 - GAP 
                    ? run == 2
                    : run == 3))
         )
            items[item_count++] = i;
      }

      // display the items
      if (run <= 2) {
         cols = (visibleColsG + GAP) / (run == 1 ? INC3 : INC2);
         if (cols == 0)
            cols = 1;
         rows = (item_count + cols - 1) / cols;
      } else   // run == 3
         rows = item_count;
      for (row = 0; row < rows && !gotInterruptG; ++row) {
         msg_putchar('\n');         // go to next line
         if (gotInterruptG)         // 'q' typed in more
            break;
         col = 0;
         for (i = row; i < item_count; i += rows) {
            msgColG = col;         // make columns
            show_one_termcode(recognizedCodeS[items[i]].name, recognizedCodeS[items[i]].code, TRUE);
            if (run == 2)
               col += INC2;
            else
               col += INC3;
         }
         out_flush();
         ui_breakcheck();
      }
   }
   eeglFree(items);
}

//Compare two key name entries.
//Note that the target string (p1) may contain additional trailing characters
//that should not factor into the comparison. Example:
//`LeftMouse>", "<LeftMouse>"] ...` should match with `LeftMouse`.
//These characters are identified by eeIsNormalIdentifierChar().
private int
keyNameEntryComparer(void const* a, void const* b) {
   CS p1 = ((KeyNameEntry *)a)->name.c;
   CS p2 = ((KeyNameEntry *)b)->name.c;
   int result = 0;

   if (p1 == p2)
      return 0;

   while (eeIsNormalIdentifierChar(*p1) && *p2 != ZERO) {
      if ((result = TOLOWER_ASC(*p1) - TOLOWER_ASC(*p2)) != 0)
         break;
      ++p1;
      ++p2;
   }

   if (result == 0) {
      if (*p2 == ZERO) {
         if (eeIsNormalIdentifierChar(*p1))
            result = 1;
      } else {
         result = -1;
      }
   }

   return result;
}

//Return a string which contains the name of the given key when the given modifiers are down.
CS
get_special_key_name(Unt c, int modifiers) {
   static Byte string[MAX_KEY_NAME_LEN + 1];
   int len;

   string[0] = '<';
   int idx = 1;

   // Key that stands for a normal character.
   if (IS_SPECIAL(c) && KEY2TERMCAP0(c) == KS_KEY)
      c = KEY2TERMCAP1(c);

   //Translate shifted special keys into unshifted keys and set modifier.
   //Same for CTRL and ALT modifiers.
   if (IS_SPECIAL(c)) {
      for (int i = 0; modifier_keys_table[i] != 0; i += MOD_KEYS_ENTRY_SIZE) {
         if (       KEY2TERMCAP0(c) == (int)modifier_keys_table[i + 1]
             && (int)KEY2TERMCAP1(c) == (int)modifier_keys_table[i + 2]
         ) {
            modifiers |= modifier_keys_table[i];
            c = TERMCAP2KEY(modifier_keys_table[i + 3], modifier_keys_table[i + 4]);
            break;
         }
      }
   }

   // try to find the key in the special key table
   int table_idx = termFindSpecialKey_in_table(c);

   //When not a known special key, and not a printable character, try to extract modifiers.
   if (c > 0 && mb_char2len(c) == 1) {
      if (table_idx < 0 && (!bookIsCharPrintable(c) || (c & 0x7f) == ' ') && (c & 0x80)) {
         c &= 0x7f;
         modifiers |= MOD_MASK_ALT;
         // try again, to find the un-alted key in the special key table
         table_idx = termFindSpecialKey_in_table(c);
      }
      if (table_idx < 0 && !bookIsCharPrintable(c) && c < ' ') {
         c += '@';
         modifiers |= MOD_MASK_CTRL;
      }
   }

   // translate the modifier into a string
   for (int i = 0; modMaskTable[i].name != 'A'; i++) {
      if ((modifiers & modMaskTable[i].modMaskG) == modMaskTable[i].mod_flag) {
         string[idx++] = modMaskTable[i].name;
         string[idx++] = (Byte)'-';
      }
   }

   if (table_idx < 0) {// unknown special key, may output t_xx
      if (IS_SPECIAL(c)) {
         string[idx++] = 'z';
         string[idx++] = 'z';
         string[idx++] = KEY2TERMCAP0(c);
         string[idx++] = KEY2TERMCAP1(c);
      }
      // Not a special key, only modifiers, output directly
      else {
         len = mb_char2len(c);
         if (len == 1 && bookIsCharPrintable(c))
            string[idx++] = c;
         ei (len > 1)
            idx += mb_char2bytes(c, string + idx);
         else {
            CS s = transchar(c);
            while (*s)
               string[idx++] = *s++;
         }
      }
   } else {// use name of special key
      Text* s = &keyNamesTable[table_idx].name;

      if (s->len + idx + 2 <= MAX_KEY_NAME_LEN) {
         STRCPY(string + idx, s->c);
         idx += (int)s->len;
      }
   }
   string[idx++] = '>';
   string[idx] = ZERO;

   return string;
}


//Find the special key with the given name (the given string does not have to
//end with ZERO, the name is assumed to end before the first non-idchar).
//If the name starts with "t_" the next two characters are interpreted as a termcap name.
//Return the key code, or 0 if not found.
int
get_special_key_code(CS name) {
   // If it's <t_xx> we get the code for xx from the termcap
   if (name[0] == 'z' && name[1] == 'z' && name[2] != ZERO && name[3] != ZERO) {
      Byte string[3] = {name[2], name[3], ZERO};
      if (add_termcap_entry(string, FALSE) == OK)
         return TERMCAP2KEY(name[2], name[3]);
   } else {
      KeyNameEntry target;
      target.enabled = true;
      target.key = 0;
      target.name = (Text){.c = name, .len = 0};

      KeyNameEntry* entry = (KeyNameEntry *)bsearch(
          &target,
          &keyNamesTable,
          ARRAY_LENGTH(keyNamesTable),
          sizeof(keyNamesTable[0]),
          keyNameEntryComparer
      );
      if (entry && entry->enabled) {
         Unt key = (Unt)entry->key;
         // Both TAB and K_TAB have name "Tab", and it's unspecified which
         // one bsearch() will return. TAB is the expected one.
         return key == K_TAB ? TAB : key;
      }
   }

   return 0;
}

// Show one termcode entry. Output goes into IObuff[]
int
show_one_termcode(CS name, CS code, int printit) {
   int len;
   if (name[0] > '~') {
      IObuff[0] = ' ';
      IObuff[1] = ' ';
      IObuff[2] = ' ';
      IObuff[3] = ' ';
   } else {
      IObuff[0] = 'z';
      IObuff[1] = 'z';
      IObuff[2] = name[0];
      IObuff[3] = name[1];
   }
   IObuff[4] = ' ';

   CS p = get_special_key_name(TERMCAP2KEY(name[0], name[1]), 0);
   if (p[1] == 'z')
      IObuff[5] = ZERO;
   else
      STRCPY(IObuff + 5, p);
   len = (int)STRLEN(IObuff);
   do
      IObuff[len++] = ' ';
   while (len < 17);
   
   IObuff[len] = ZERO;
   if (!code)
     len += 4;
   else
     len += eeglStrSize(code);

   if (printit) {
      msg_puts(IObuff);
      if (!code)
         msg_puts((CS)"NULL");
      else
         msg_outtrans(code);
   }
   return len;
}

// For Xterm >= 140 compiled with OPT_TCAP_QUERY: Obtain the actually used termcap codes from the
// terminal itself. We get them one by one to avoid a very long response string.
private int xt_index_in = 0;
private int xt_index_out = 0;

private void
req_more_codes_from_term(void) {
   Byte buffer[32];  // extra size to shut up LGTM
   int old_idx = xt_index_out;

   //Don't do anything when going to exit.
   if (exiting)
      return;

   // Send up to 10 more requests out than we received.  Avoid sending too
   // many, there can be a buffer overflow somewhere.
   while (xt_index_out < xt_index_in + 10 && (Unt)xt_index_out < ARRAY_LENGTH(key_names)) {
      CS key = key_names[xt_index_out];

      MAY_WANT_TO_LOG_THIS;
      LOG_TRN("Requesting XT %d: %s", xt_index_out, key);
      if (key[2] != ZERO)
         SPRINTF(buffer, "\033P+q%02x%02x%02x\033\\", key[0], key[1], key[2]);
      else
         SPRINTF(buffer, "\033P+q%02x%02x\033\\", key[0], key[1]);
      out_str_nf(buffer);
      ++xt_index_out;
   }

   // Send the codes out right away.
   if (xt_index_out != old_idx)
      out_flush();
}

// Decode key code response from xterm:
// '<Esc>P1+r<name>=<string><Esc>\' if it is enabled/supported
// '<Esc>P0+r<Esc>\'                if it not enabled
// A "0" instead of the "1" indicates a code that isn't supported.
// Both <name> and <string> are encoded in hex.
// "code" points to the "0" or "1".
private void
got_code_from_term(CS code, int len) {
# define XT_LEN 100
   Byte   name[4];
   Byte   str[XT_LEN];
   Unt i;
   int j = 0;
   int c;

   // A '1' means the code is supported, a '0' means it isn't. If it is supported, there must be a
   // '=' following When half the length is > XT_LEN we can't use it.
   if (code[0] == '1' && (code[7] == '=' || code[9] == '=') && len / 2 < XT_LEN) {
      // Get the name from the response and find it in the table.
      name[0] = hexhex2nr(code + 3);
      name[1] = hexhex2nr(code + 5);
      if (code[9] == '=')
         name[2] = hexhex2nr(code + 7);
      else
         name[2] = ZERO;
      name[3] = ZERO;
      for (i = 0; i < ARRAY_LENGTH(key_names); ++i) {
         if (STRCMP(key_names[i], name) == 0) {
            xt_index_in = i;
            break;
         }
      }

      LOG_TRN("Received XT %d: %s", xt_index_in, (char *)name);

      if (i < ARRAY_LENGTH(key_names)) {
         i = (code[7] == '=') ? 8 : 10;
         for (; (c = hexhex2nr(code + i)) >= 0; i += 2)
            str[j++] = c;
         str[j] = ZERO;
# if 0
          // when RGB result comes back, it is supported when the result contains an '='
          ei (name[0] == 'R' && name[1] == 'G' && name[2] == 'B' && code[9] == '=') {
         int val = atoi((char *)str);
         // only enable it, if termguicolors hasn't been set yet and
         // there are 8 bits per color channel
          }
# endif
         i = find_term_bykeys(str);
         if (i != UNT && name[0] == recognizedCodeS[i].name[0] && name[1] == recognizedCodeS[i].name[1]) {
            // Existing entry with the same name and code - skip.
            lo("got_code_from_term(): Entry %c%c did not change", name[0], name[1]);
         } else {
            if (i != UNT) {
               // Delete an existing entry using the same code.
               lo("got_code_from_term(): Deleting entry %c%c with matching keys %s",
                     recognizedCodeS[i].name[0], recognizedCodeS[i].name[1], str);
               del_termcode_idx(i);
            } else
               lo("got_code_from_term(): Adding entry %c%c with keys %s",
                        name[0], name[1], str);
            add_termcode(name, str, true);
         }
      }
   }

   // May request more codes now that we received one.
   ++xt_index_in;
   req_more_codes_from_term();
}

//Check if there are any unanswered requests and deal with them.
//This is called before starting an external program or getting direct
//keyboard input. We don't want responses to be sent to that program or handled as typed text.
private void
handleUnansweredRequests(void) {
   //If no codes requested or all are answered, no need to wait.
   if (xt_index_out == 0 || xt_index_out == xt_index_in)
      return;

   //Vgetc() will check for and handle any response.
   //Keep calling vpeekc() until we don't get any responses.
   ++no_mapping;
   ++allow_keys;
   for (;;) {
      Unt c = vpeekc();
      if (c == ZERO)       // nothing available
         break;

      // If a response is recognized it's replaced with K_IGNORE, must read
      // it from the input stream.  If there is no K_IGNORE we can't do
      // anything, break here (there might be some responses further on, but
      // we don't want to throw away any typed chars).
      if (c != K_SPECIAL && c != K_IGNORE)
         break;
      c = vgetc();
      if (c != K_IGNORE) {
         vungetc(c);
         break;
      }
   }
   --no_mapping;
   --allow_keys;
}


private int cube_value[] = {
   0x00, 0x5F, 0x87, 0xAF, 0xD7, 0xFF
};

private int grey_ramp[] = {
   0x08, 0x12, 0x1C, 0x26, 0x30, 0x3A, 0x44, 0x4E, 0x58, 0x62, 0x6C, 0x76,
   0x80, 0x8A, 0x94, 0x9E, 0xA8, 0xB2, 0xBC, 0xC6, 0xD0, 0xDA, 0xE4, 0xEE
};

private const Byte ansi_table[16][3] = {
//   R    G    B
  {  0,   0,   0}, // black
  {224,   0,   0}, // dark red
  {  0, 224,   0}, // dark green
  {224, 224,   0}, // dark yellow / brown
  {  0,   0, 224}, // dark blue
  {224,   0, 224}, // dark magenta
  {  0, 224, 224}, // dark cyan
  {224, 224, 224}, // light grey

  {128, 128, 128}, // dark grey
  {255,  64,  64}, // light red
  { 64, 255,  64}, // light green
  {255, 255,  64}, // yellow
  { 64,  64, 255}, // light blue
  {255,  64, 255}, // light magenta
  { 64, 255, 255}, // light cyan
  {255, 255, 255}, // white
};


# define ANSI_INDEX_NONE 0

void
ansi_color2rgb(int nr, OUT Byte *r, OUT Byte *g, OUT Byte *b, OUT Byte *ansi_idx) {
   if (nr < 16) {
      *r = ansi_table[nr][0];
      *g = ansi_table[nr][1];
      *b = ansi_table[nr][2];
      *ansi_idx = nr + 1;
   } else {
      *r = 0;
      *g = 0;
      *b = 0;
      *ansi_idx = ANSI_INDEX_NONE;
   }
}

void
cterm_color2rgb(int nr, Byte* r, Byte* g, Byte* b, Byte* ansi_idx) {
   int idx;

   if (nr < 16) {
      idx = nr;
      *r = ansi_table[idx][0];
      *g = ansi_table[idx][1];
      *b = ansi_table[idx][2];
      *ansi_idx = idx + 1;
   } ei (nr < 232) {
      // 216 color cube
      idx = nr - 16;
      *r = cube_value[idx / 36 % 6];
      *g = cube_value[idx / 6  % 6];
      *b = cube_value[idx      % 6];
      *ansi_idx = ANSI_INDEX_NONE;
   } ei (nr < 256) {
      // 24 grey scale ramp
      idx = nr - 232;
      *r = grey_ramp[idx];
      *g = grey_ramp[idx];
      *b = grey_ramp[idx];
      *ansi_idx = ANSI_INDEX_NONE;
   } else {
      *r = 0;
      *g = 0;
      *b = 0;
      *ansi_idx = ANSI_INDEX_NONE;
   }
}

//Replace K_BS by <BS> and K_DEL by <DEL>. Include any modifiers into the key and drop them.
//Return "len" adjusted for replaced codes.
int
term_replace_keycodes(CS ta_buf, int ta_len, int len_arg) {
   int len = len_arg;
   int i;
   Unt c;

   for (i = ta_len; i < ta_len + len; ++i) {
      if (ta_buf[i] == CSI && len - i > 3 && ta_buf[i + 1] == KS_MODIFIER)    {
         Unt modifiers = ta_buf[i + 2];
         Unt key = ta_buf[i + 3];

         // Try to use the modifier to modify the key.  In any case drop the modifier.
         mch_memmove(ta_buf + i + 1, ta_buf + i + 4, (Unt)(len - i - 3));
         len -= 3;
         if (key < 0x80)
            key = mergeModifierKey(key, &modifiers);
         ta_buf[i] = key;
      } ei (ta_buf[i] == CSI && len - i > 2) {
         c = TERMCAP2KEY(ta_buf[i + 1], ta_buf[i + 2]);
         if (c == K_DEL || c == K_KDEL || c == K_BS) {
            mch_memmove(ta_buf + i + 1, ta_buf + i + 3, (Unt)(len - i - 2));
            if (c == K_DEL || c == K_KDEL)
               ta_buf[i] = DEL;
            else
               ta_buf[i] = Ctrl_H;
            len -= 2;
         }
      } ei (ta_buf[i] == '\r')
         ta_buf[i] = '\n';
      i += utfCharLen_len(ta_buf + i, ta_len + len - i) - 1;
   }
   return len;
}

//Try to include modifiers in the key.
//Changes "Shift-a" to 'A', "Alt-A" to 0xc0, etc.
//When "simplify" is FALSE don't do Ctrl and Alt.
//When "simplify" is TRUE and Ctrl or Alt is removed from modifiers set
//"didSimplify" when it's not NULL.
private int
extractModifiers(Unt key, OUT Unt* modifiers, Boole doSimplify, OUT Boole* didSimplify) {
   if ((*modifiers & MOD_MASK_SHIFT) && ASCII_ISALPHA(key)) {
      key = TOUPPER_ASC(key);
      // With <C-S-a> we keep the shift modifier. But with <S-a>, <A-S-a> and <S-A> we don't
      if (doSimplify || *modifiers == MOD_MASK_SHIFT
            || *modifiers == (MOD_MASK_SHIFT | MOD_MASK_ALT)
            || *modifiers == (MOD_MASK_SHIFT | MOD_MASK_META))
         *modifiers &= ~MOD_MASK_SHIFT;
    }

   // <C-H> and <C-h> mean the same thing, always use "H"
   if ((*modifiers & MOD_MASK_CTRL) && ASCII_ISALPHA(key))
      key = TOUPPER_ASC(key);

   if (doSimplify && (*modifiers & MOD_MASK_CTRL)
       && ((key >= '?' && key <= '_') || ASCII_ISALPHA(key))
   ) {
      key = charMinusCtrl(key);
      *modifiers &= ~MOD_MASK_CTRL;
      // <C-@> is <ZERO>
      if (key == ZERO)
         key = K_ZERO;
      if (didSimplify) {
         //lo("ccc simpl misc.c 3241 key %d", key);
         //*didSimplify = TRUE;
      }
   }

   if (doSimplify && (*modifiers & MOD_MASK_ALT) && key < 0x80) { // avoid creating a lead byte
      key |= 0x80;
      *modifiers &= ~MOD_MASK_ALT;   // remove the ALT (META) modifier
      if (didSimplify) {
         *didSimplify = true;
      }
   }

   return key;
}

//Return the modifier mask bit (MOD_MASK_*) which corresponds to the given
//modifier name ('S' for Shift, 'C' for Ctrl etc).
private int
nameToModMask(Byte c) {
   c = TOUPPER_ASC(c);
   for (int i = 0; modMaskTable[i].modMaskG != 0; i++) {
      if (c == modMaskTable[i].name)
         return modMaskTable[i].mod_flag;
   }
   return 0;
}

//Try translating a <> name at "(*srcp)[]", return the key and put modifiers in "modp".
//"srcp" is advanced to after the <> name. returns 0 if there is no match.
int
termFindSpecialKey(
   OUT Byte** srcp,
   OUT Unt* modp,
   Unt flags,      // FSK_ values
   OUT Boole* didSimplify // found <C-H> or <A-x>
){
   CS end_of_name;
   CS bp;
   int in_string = flags & FSK_IN_STRING;
   Unt modifiers;
   int bit;
   Unt key;
   ULong   n;
   int      l;

   CS src = *srcp;
   if (src[0] != '<')
      return 0;
   if (src[1] == '*')       // <*xxx>: do not simplify
      ++src;

   // Find end of modifier list
   CS last_dash = src;
   for (bp = src + 1; *bp == '-' || eeIsNormalIdentifierChar(*bp); bp++) {
      if (*bp == '-') {
         last_dash = bp;
         if (bp[1] != ZERO) {
            l = utfCharLen(bp + 1);
            // Anything accepted, like <C-?>. <C-"> or <M-"> are not special in strings as " is
            // the string delimiter. With a backslash it works: <M-\">
            if (!(in_string && bp[1] == '"') && bp[l + 1] == '>')
               bp += l;
            ei (in_string && bp[1] == '\\' && bp[2] == '"' && bp[3] == '>')
               bp += 2;
          }
      }
      if (bp[0] == 'z' && bp[1] == 'z' && bp[2] && bp[3])
         bp += 3;   // skip t_xx, xx may be '-' or '>'
      ei (STRNICMP(bp, "char-", 5) == 0) {
         readLongNumber(bp + 5, NULL, &l, STR2NR_ALL, NULL, NULL, 0, TRUE, NULL);
         if (l == 0) {
            emsg(_(e_invalid_argument));
            return 0;
         }
         bp += l + 5;
         break;
      }
   }

   if (*bp == '>') {// found matching '>'
      end_of_name = bp + 1;

      // Which modifiers are given?
      modifiers = 0x0;
      for (bp = src + 1; bp < last_dash; bp++) {
         if (*bp != '-') {
            bit = nameToModMask(*bp);
            if (bit == 0x0)
               break;   // Illegal modifier name
            modifiers |= bit;
         }
      }

      // Legal modifier name.
      if (bp >= last_dash) {
         if (STRNICMP(last_dash + 1, "char-", 5) == 0 && EE_ISDIGIT(last_dash[6])) {
            // <Char-123> or <Char-033> or <Char-0x33>
            readLongNumber(last_dash + 6, NULL, &l, STR2NR_ALL, NULL, &n, 0, TRUE, NULL);
            if (l == 0) {
               emsg(_(e_invalid_argument));
               return 0;
            }
            key = (int)n;
         } else {
            int off = 1;

            // Modifier with single letter, or special key name.
            if (in_string && last_dash[1] == '\\' && last_dash[2] == '"')
               off = 2;
            l = utfCharLen(last_dash + off);
            if (modifiers != 0 && last_dash[l + off] == '>')
               key = mb_ptr2char(last_dash + off);
            else {
               key = get_special_key_code(last_dash + off);
               if (!(flags & FSK_KEEP_X_KEY))
                  key = handle_x_keys(key);
            }
         }

         //get_special_key_code() may return ZERO for invalid special key name.
         if (key != ZERO) {
            //Only use a modifier when there is no special key code that includes the modifier.
            key = simplify_key(key, &modifiers);

            if ((flags & FSK_KEYCODE) == 0) {
               //don't want keycode, use single byte code
               if (key == K_BS)
                  key = BS;
               ei (key == K_DEL || key == K_KDEL)
                  key = DEL;
            } ei (key == 27 && (flags & FSK_FROM_PART) != 0) {
               //Using the Kitty key protocol, which uses K_ESC for an Esc character. For the
               //simplified keys use the Esc character and set didSimplify, then in the
               //non-simplified keys use K_ESC.
               if ((flags & FSK_SIMPLIFY) != 0) {
                  if (didSimplify)
                     *didSimplify = true;
               } else
                  key = K_ESC;
            }

            // Normal Key with modifier: Try to make a single byte code.
            if (!IS_SPECIAL(key))
               key = extractModifiers(
                     key, OUT &modifiers, (flags & FSK_SIMPLIFY) > 0, OUT didSimplify
               );

            *modp = modifiers;
            *srcp = end_of_name;
            return key;
         }
      }
   }
   return 0;
}


//Some keys are used with Ctrl without Shift and are still expected to be
//mapped as if Shift was pressed:
//CTRL-2 is CTRL-@
//CTRL-6 is CTRL-^
//CTRL-- is CTRL-_
//Also, unless no_reduce_keys is set then <C-H> and <C-h> mean the same thing, use "H".
//Return the possibly adjusted key.
private int
may_adjust_key_for_ctrl(int modifiers, Unt key) {
   if ((modifiers & MOD_MASK_CTRL) == 0)
      return key;

   if (ASCII_ISALPHA(key)) {
      check_no_reduce_keys();  // may update the no_reduce_keys flag
      return no_reduce_keys == 0 ? TOUPPER_ASC(key) : key;
   }
   if (key == '2')
      return '@';
   if (key == '6')
      return '^';
   if (key == '-')
      return '_';

   //On a Belgian keyboard AltGr $ is ']', on other keyboards '$' can only be
   //obtained with Shift.  Assume that '$' without shift implies a Belgian
   //keyboard, where CTRL-$ means CTRL-].
   if (key == '$' && (modifiers & MOD_MASK_SHIFT) == 0)
      return ']';

   return key;
}

//Some keys already have Shift included, pass them as normal keys.
//When Ctrl is also used <C-H> and <C-S-H> are different, but <C-S-{> should
//be <C-{>.  Same for <C-S-}> and <C-S-|>. Also for <A-S-a> and <M-S-a>.
//This includes all printable ASCII characters except a-z.
//Digits are included because with AZERTY the Shift key is used to get them.
private Unt
may_remove_shift_modifier(Unt modifiers, Unt key) {
   if ((modifiers == MOD_MASK_SHIFT
            || modifiers == (MOD_MASK_SHIFT | MOD_MASK_ALT)
            || modifiers == (MOD_MASK_SHIFT | MOD_MASK_META))
       && ((key >= '!' && key <= '/')
            || (key >= ':' && key <= 'Z')
            || eeIsDigit(key)
            || (key >= '[' && key <= '`')
            || (key >= '{' && key <= '~'))
   )
      return modifiers & ~MOD_MASK_SHIFT;

   if (modifiers == (MOD_MASK_SHIFT | MOD_MASK_CTRL)
         && (key == '{' || key == '}' || key == '|'))
      return modifiers & ~MOD_MASK_SHIFT;

   return modifiers;
}

//Try to find key "c" in the special key table. Return the index when found, -1 when not found.
int
termFindSpecialKey_in_table(int c) {
   for (int i = 0; i < (int)ARRAY_LENGTH(keyNamesTable); i++) {
      if (c == keyNamesTable[i].key && !keyNamesTable[i].is_alt)
          return keyNamesTable[i].enabled ? i : -1;
   } 

   return -1;
}

//Put the character sequence for "key" with "modifiers" into "dst" and return
//the resulting length.
//When "escape_ks" is TRUE escape K_SPECIAL bytes in the character.
//The sequence is not ZERO terminated. This is how characters in a string are encoded.
int
special_to_buf(Unt key, Unt modifiers, int escape_ks, OUT CS dst) {
   int dlen = 0;

   //Put the appropriate modifier in a string
   if (modifiers != 0) {
      dst[dlen++] = K_SPECIAL;
      dst[dlen++] = KS_MODIFIER;
      dst[dlen++] = (Byte)modifiers;
   }

   if (IS_SPECIAL(key)) {
      dst[dlen++] = K_SPECIAL;
      dst[dlen++] = KEY2TERMCAP0(key);
      dst[dlen++] = KEY2TERMCAP1(key);
   } ei (escape_ks)
      dlen = (int)(add_char2buf(key, dst + dlen) - dst);
   else
      dlen += mb_char2bytes(key, dst + dlen);

   return dlen;
}

//Try translating a <> name at "(*srcp)[]" to "dst[]".
//Return the number of characters added to "dst[]", zero for no match.
//If there is a match, "srcp" is advanced to after the <> name.
//"dst[]" must be big enough to hold the result (up to six characters)!
int
trans_special(
   OUT Byte** srcp,
   CS dst,
   Unt flags,      // FSK_ values
   int escape_ks,   // escape K_SPECIAL bytes in the character
   OUT Boole* didSimplify  // FSK_SIMPLIFY and found <C-H> or <A-x>
){
   Unt modifiers = 0;

   Unt key = termFindSpecialKey(OUT srcp, OUT &modifiers, flags, OUT didSimplify);
   if (key == 0)
      return 0;

   return special_to_buf(key, modifiers, escape_ks, OUT dst);
}

//}}}
//{{{pty

// The stuff in this section mostly comes from the "screen" program.
// Included with permission from Juergen Weigert.
// Copied from "pty.c".  "putenv.c" was used for putenv() in misc2.c.
//
// It has been modified to work better with Eegl.
// The parts that are not used in Eegl have been deleted.
// See the "screen" sources for the complete stuff.
//
// This specific version is distributed under the Eegl license (attribution by
// Juergen Weigert), the GPL applies to the original version, see the copyright notice below.

// Copyright (c) 1993
//   Juergen Weigert (jnweiger@immd4.informatik.uni-erlangen.de)
//   Michael Schroeder (mlschroe@immd4.informatik.uni-erlangen.de)
// Copyright (c) 1987 Oliver Laumann
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program (see the file COPYING); if not, write to the
// Free Software Foundation, Inc.,
// 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA

#include <signal.h>

#include <termios.h>

#ifdef ISC
# include <sys/tty.h>
# include <sys/sioctl.h>
# include <sys/pty.h>
#endif

// if no PTYRANGE[01] is in the config file, we pick a default
#ifndef PTYRANGE0
# define PTYRANGE0 "qprs"
#endif
#ifndef PTYRANGE1
# define PTYRANGE1 "0123456789abcdef"
#endif

// Open all ptys with O_NOCTTY, just to be on the safe side.
#ifndef O_NOCTTY
# define O_NOCTTY 0
#endif

// These should be in stdlib.h, but it depends on _XOPEN_SOURCE.
char *ptsname(int);
int unlockpt(int);
int grantpt(int);
int posix_openpt(int flags);

private void
initmaster(int f) {
   tcflush(f, TCIOFLUSH);
   (void)ioctl(f, TIOCEXCL, (char *) 0); // lock the pty device
}

//This causes a hang on some systems, but is required for a properly working
//pty on others. Needs to be tuned...
int
setup_slavepty(int fd) {
   if (fd < 0)
      return 0;
#if defined(I_PUSH) && !defined(sgi) \
   && !defined(linux) && !defined(__osf__) && !defined(M_UNIX)
   if (ioctl(fd, I_PUSH, "ldterm") != 0)
      return -1;
#endif
   return 0;
}

int
openpty(char **ttyn) {
   int f;
   static Byte TtyName[32];  // used for opening a new pty-pair

   if ((f = posix_openpt(O_RDWR | O_NOCTTY | O_EXTRA)) == -1)
      return -1;

   // SIGCHLD set to SIG_DFL for grantpt() because it fork()s and exec()s pt_chmod
   CS m;
   sighandler_T sigcld = mch_signal(SIGCHLD, SIG_DFL);
   if ((m = (CS)ptsname(f)) == NULL || grantpt(f) || unlockpt(f)) {
      mch_signal(SIGCHLD, sigcld);
      close(f);
      return -1;
   }
   mch_signal(SIGCHLD, sigcld);
   copySubstrToAllocation(TtyName, (Text){m, sizeof(TtyName) - 1});
   initmaster(f);
   *ttyn = (char*)TtyName;
   return f;
}

// Call isatty(fd)
int
mch_isatty(int fd) {
   return isatty(fd);
}

//}}}
