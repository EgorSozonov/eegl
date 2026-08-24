//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## regexp.c: Eegl-specific handling of regular expressions: compileRegexp(), eeRegexec(), eeRegsub()

//By default: do not create debugging logs or files related to regular expressions, even when 
//compiling with -DDEBUG. Uncomment the second line to get the regexp debugging.
#undef DEBUG
// #define DEBUG

#include "eegl.h"

//{{{header

/*
 *
 * NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE
 *
 * This is NOT the original regular expression code as written by Henry
 * Spencer.  This code has been modified specifically for use with Eegl, and
 * should not be used apart from compiling Eegl.  If you want a good regular
 * expression library, get the original code.
 *
 * NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE
 */
 

// How many braces are allowed.
// TODO(RE): Use dynamic memory allocation instead of static, like here
#define MAX_BRACES 20

// how many states are allowed
#define MAX_STATES 100000
#define TOO_EXPENSIVE (-1)

public declStruct(RState);
// NFA state. Such a state may have no outgoing edge, when it is a MATCH state.
public struct RState {
   Unt         c; // a char
   RState      *out;
   RState      *out1;
   int         id;
   int         lastlist[2]; // 0: normal, 1: recursive
   int         val;
};

// Structure used by the NFA matcher.
public struct RegProg {
   // These three members implement RegProg
   Unt regflags;
   Unt re_engine;
   Unt flags;
   int re_in_use;

   RState* start;      // points into state[]

   int reganch;   // pattern starts with ^
   int regstart;   // char at start of pattern
   Byte* input;   // plain text to match with

   int has_zend;   // pattern contains \ze
   int has_backref;   // pattern contains \1 .. \9
   int reghasz;
   Byte* pattern;
   int nsubexp;   // number of ()
   int nstate;
   bool hadEol;
   RState state[1];   // actually longer..
};


// Since the out pointers in the list are always uninitialized, we use the pointers themselves
// as storage for the StateLists.
private typedef union StateList StateList;
union StateList {
   StateList* next;
   RState* s;
};


// A partially built NFA without the matching state filled in.
private typedef struct {
   RState *start; // points at the start state.
   StateList   *out; // a list of places that need to be set to the next state for this fragment.
} Frag;

//Structure to be used for single-line matching.
//Sub-match "no" starts at "startp[no]" and ends just before "endp[no]".
//When there is no match, the pointer is NULL.
public struct RegMatch {
   RegProg* regprog;
   Byte* startp[NSUBEXP];
   Byte* endp[NSUBEXP];

   ColNr rm_matchcol;   // match start without "\zs"
   int rm_ic;
};

#ifdef DEBUG
   private Arr(Byte) currExpr;
#endif


private int numComplexBracesS; // Complex \{...} count
private Byte hadEndbraceS[NSUBEXP];   // flags, true if end of () found

private sig_atomic_t dummy_timeout_flag = 0;
private volatile sig_atomic_t *timeout_flag = &dummy_timeout_flag;

//Magic characters have a special meaning, they don't match literally.
//Magic characters are negative.  This separates them from literal characters
//(possibly multi-byte). Only ASCII characters can be Magic.
#define Magic(x)  (Unt)((x) - 256)
#define un_Magic(x)   ((x) + 256)
#define is_Magic(x)   ((x) < 0)

private int
no_Magic(int x) {
   if (is_Magic(x))
      return un_Magic(x);
   return x;
}

private int
toggle_Magic(int x) {
   if (is_Magic(x))
      return un_Magic(x);
   return Magic(x);
}

private int timeout_nesting = 0;

private sig_atomic_t *saved_timeout_flag;

//The first byte of the BT regexp internal "program" is actually this magic number; the start node
//begins in the second byte.  It's used to catch the most severe mutilation of the program by the 
//caller.

#define REGMAGIC   0234

// Utility definitions.
#define UCHARAT(p)   ((int)*(CS)(p))

// Used for an error (down from) compileRegexp(): give the error message, set anyRegexEmsgG and 
// return NULL
#define EMSG_RET_NULL(m) return (emsg((m)), anyRegexEmsgG = true, (void *)NULL)
#define IEMSG_RET_NULL(m) return (internalErrMsg((m)), anyRegexEmsgG = true, (void *)NULL)
#define EMSG_RET_FAIL(m) return (emsg((m)), anyRegexEmsgG = true, FAIL)
#define EMSG2_RET_NULL(m, c) return \
   (showErrFmtMsg((const char *)(m), (c) ? "" : "\\"), anyRegexEmsgG = true, (void *)NULL)
#define EMSG3_RET_NULL(m, c, a) return \
   (showErrFmtMsg((const char *)(m), (c) ? "" : "\\", (a)), anyRegexEmsgG = true, (void *)NULL)
#define EMSG2_RET_FAIL(m, c) return \
   (showErrFmtMsg((const char *)(m), (c) ? "" : "\\"), anyRegexEmsgG = true, FAIL)
#define EMSG_ONE_RET_NULL EMSG2_RET_NULL(_(e_invalid_item_in_str_brackets), reg_magic == MAGIC_ALL)


#define MAX_LIMIT   (32767L << 16L)

#define NOT_MULTI  0
#define MULTI_ONE  1
#define MULTI_MULT 2

//return values for regmatch()
#define RA_FAIL    1   // something failed, abort
#define RA_CONT    2   // continue in inner loop
#define RA_BREAK   3   // break inner loop
#define RA_MATCH   4   // successful match
#define RA_NOMATCH 5   // didn't match

//Return NOT_MULTI if c is not a "multi" operator.
//Return MULTI_ONE if c is a single "multi" operator.
//Return MULTI_MULT if c is a multi "multi" operator.
private int
re_multi_type(Unt c) {
   if (c == Magic('@') || c == Magic('=') || c == Magic('?'))
      return MULTI_ONE;
   if (c == Magic('*') || c == Magic('+') || c == Magic('{'))
      return MULTI_MULT;
   return NOT_MULTI;
}

private Byte* reg_prev_sub = NULL;
private Unt reg_prev_sublen = 0;

//REGEXP_INRANGE contains all characters which are always special in a [] range after '\'.
//REGEXP_ABBR contains all characters which act as abbreviations after '\'.
//These are:
// \n   - New line (NL).
// \r   - Carriage Return (CR).
// \t   - Tab (TAB).
// \e   - Escape (ESC).
// \b   - Backspace (Ctrl_H).
// \d  - Character code in decimal, eg \d123
// \x   - Character code in hex, eg \x4a
// \u   - Multibyte character code, eg \u20ac
// \U   - Long multibyte character code, eg \U12345678
private Byte REGEXP_INRANGE[] = "]^-n\\";
private Byte REGEXP_ABBR[] = "nrtebdoxuU";

// Translate '\x' to its control character, except "\n", which is Magic.
private Unt
backslash_trans(Unt c) {
   switch (c) {
   case 'r':   return ENTER;
   case 't':   return TAB;
   case 'e':   return ESC;
   case 'b':   return BS;
   }
   return c;
}

public enum {
   CHAR_CLASS_ALNUM = 0,
   CHAR_CLASS_ALPHA,
   CHAR_CLASS_BLANK,
   CHAR_CLASS_CNTRL,
   CHAR_CLASS_DIGIT,
   CHAR_CLASS_GRAPH,
   CHAR_CLASS_LOWER,
   CHAR_CLASS_PRINT,
   CHAR_CLASS_PUNCT,
   CHAR_CLASS_SPACE,
   CHAR_CLASS_UPPER,
   CHAR_CLASS_XDIGIT,
   CHAR_CLASS_TAB,
   CHAR_CLASS_RETURN,
   CHAR_CLASS_BACKSPACE,
   CHAR_CLASS_ESCAPE,
   CHAR_CLASS_IDENT,
   CHAR_CLASS_KEYWORD,
   CHAR_CLASS_FNAME,
   CHAR_CLASS_NONE = 99
};

// Specific version of character class functions. Using a table to keep this fast.
private short characterClasses[256];

#define RI_DIGIT   0x01
#define RI_HEX   0x02
#define RI_WORD   0x08
#define RI_HEAD   0x10
#define RI_ALPHA   0x20
#define RI_LOWER   0x40
#define RI_UPPER   0x80
#define RI_WHITE   0x100

private void
initCharacterClasses(void) {
   static Boole done = false;

   if (done)
      return;

   for (int i = 0; i < 256; ++i) {
      if (i >= '0' && i <= '9')
         characterClasses[i] = RI_DIGIT + RI_HEX + RI_WORD;
      ei (i >= 'a' && i <= 'f')
         characterClasses[i] = RI_WORD + RI_HEAD + RI_ALPHA + RI_LOWER + RI_HEX;
      ei (i >= 'g' && i <= 'z')
         characterClasses[i] = RI_WORD + RI_HEAD + RI_ALPHA + RI_LOWER;
      ei (i >= 'A' && i <= 'F')
         characterClasses[i] = RI_WORD + RI_HEAD + RI_ALPHA + RI_UPPER + RI_HEX;
      ei (i >= 'G' && i <= 'Z')
         characterClasses[i] = RI_WORD + RI_HEAD + RI_ALPHA + RI_UPPER;
      ei (i == '_')
         characterClasses[i] = RI_WORD + RI_HEAD;
      else
         characterClasses[i] = 0;
   }
   characterClasses[' '] |= RI_WHITE;
   characterClasses['\t'] |= RI_WHITE;
   done = true;
}

#define ri_digit(c)   ((c) < 0x100 && (characterClasses[c] & RI_DIGIT))
#define ri_hex(c)   ((c) < 0x100 && (characterClasses[c] & RI_HEX))
#define ri_word(c)   ((c) < 0x100 && (characterClasses[c] & RI_WORD))
#define ri_head(c)   ((c) < 0x100 && (characterClasses[c] & RI_HEAD))
#define ri_alpha(c)   ((c) < 0x100 && (characterClasses[c] & RI_ALPHA))
#define ri_lower(c)   ((c) < 0x100 && (characterClasses[c] & RI_LOWER))
#define ri_upper(c)   ((c) < 0x100 && (characterClasses[c] & RI_UPPER))
#define ri_white(c)   ((c) < 0x100 && (characterClasses[c] & RI_WHITE))

// flags for regflags
#define RF_ICASE     1   // ignore case
#define RF_NOICASE   2   // don't ignore case
#define RF_HASNL     4   // can match a NL
#define RF_ICOMBINE  8   // ignore combining characters
#define RF_LOOKBH   16   // uses "\@<=" or "\@<!"

// Global work variables for compileRegexp().

private Byte* regparse;   // Input-scan pointer.
private int regnpar;   // () count.
private int currZParensS;   // \z() count.
private int re_has_z;   // \z item detected
private unsigned regflags;   // RF_ flags for prog

private Magic reg_magic;   // magicness of the pattern

private int reg_string;   // matching with a string instead of a buffer line
private int reg_strict;   // "[abc" is illegal

// META contains all characters that may be magic, except '^' and '$'.
// META[] is used often enough to justify turning it into a table.
private Byte META_flags[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
//         %  &     (  )  *  +         .
    0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 0, 0, 1, 0,
//     1  2  3   4  5  6  7  8  9   <  =  >  ?
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1,
//  @  A     C   D     F     H  I     K   L  M    O
    1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1,
//  P        S      U  V  W  X     Z  [       _
    1, 0, 0, 1, 0, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 1,
//     a     c   d     f     h  i     k   l  m  n  o
    0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1,
//  p        s      u  v  w  x     z  {   |     ~
    1, 0, 0, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1
};

private Unt   curchr;      // currently parsed character
// Previous character.  Note: prevchr is sometimes -1 when we are not at the
// start, eg in /[ ^I]^ the pattern was never found even if it existed,
// because ^ was taken to be magic -- webb
private Unt   prevchr;
private Unt   prevprevchr;   // previous-previous character
private Unt   nextchr;   // used for ungetchr() ???

// arguments for reg()
#define REG_NOPAREN  0   // toplevel reg()
#define REG_PAREN    1   // \(\)
#define REG_ZPAREN   2   // \z(\)
#define REG_NPAREN   3   // \%(\)

private typedef struct {
   Byte* regparse;
   int prevchr_len;
   int curchr;
   int prevchr;
   int prevprevchr;
   int nextchr;
   int at_start;
   int prev_at_start;
   int regnpar;
} ParseState;

private void   initchr(CS);
private Unt   getchr(void);
private void   skipchr_keepstart(void);
private Unt   peekchr(void);
private void   skipchr(void);
private Long   gethexchrs(int maxinputlen);
private Long   getdecchrs(void);
private Long   coll_get_char(void);
private int   cstrncmp(Byte *s1, Byte *s2, int *n);
private Byte   *cstrchr(Byte *, int);
private int   re_mult_next(CS what);
private int   reg_iswordc(int);

//}}}
//{{{implementation details

// Check for a character class name "[:name:]". "pp" points to the '['. Returns one of the CLASS_
// items. CLASS_NONE means that no item was recognized. Otherwise "pp" is advanced to after the item
private int
get_char_class(Byte **pp) {
   // must be sorted by the 'value' field because it is used by bsearch()!
   private Kv char_characterClasses[] = {
   KEYVALUE_ENTRY(CHAR_CLASS_ALNUM, "alnum:]"),
   KEYVALUE_ENTRY(CHAR_CLASS_ALPHA, "alpha:]"),
   KEYVALUE_ENTRY(CHAR_CLASS_BACKSPACE, "backspace:]"),
   KEYVALUE_ENTRY(CHAR_CLASS_BLANK, "blank:]"),
   KEYVALUE_ENTRY(CHAR_CLASS_CNTRL, "cntrl:]"),
   KEYVALUE_ENTRY(CHAR_CLASS_DIGIT, "digit:]"),
   KEYVALUE_ENTRY(CHAR_CLASS_ESCAPE, "escape:]"),
   KEYVALUE_ENTRY(CHAR_CLASS_FNAME, "fname:]"),
   KEYVALUE_ENTRY(CHAR_CLASS_GRAPH, "graph:]"),
   KEYVALUE_ENTRY(CHAR_CLASS_IDENT, "ident:]"),
   KEYVALUE_ENTRY(CHAR_CLASS_KEYWORD, "keyword:]"),
   KEYVALUE_ENTRY(CHAR_CLASS_LOWER, "lower:]"),
   KEYVALUE_ENTRY(CHAR_CLASS_PRINT, "print:]"),
   KEYVALUE_ENTRY(CHAR_CLASS_PUNCT, "punct:]"),
   KEYVALUE_ENTRY(CHAR_CLASS_RETURN, "return:]"),
   KEYVALUE_ENTRY(CHAR_CLASS_SPACE, "space:]"),
   KEYVALUE_ENTRY(CHAR_CLASS_TAB, "tab:]"),
   KEYVALUE_ENTRY(CHAR_CLASS_UPPER, "upper:]"),
   KEYVALUE_ENTRY(CHAR_CLASS_XDIGIT, "xdigit:]")
   };

   // check that the value of "pp" has a chance of matching
   if ((*pp)[1] == ':' && ASCII_ISLOWER((*pp)[2])
         && ASCII_ISLOWER((*pp)[3]) && ASCII_ISLOWER((*pp)[4])
   ) {
      Kv *entry;
      // this function can be called repeatedly with the same value for "pp"
      // so we cache the last found entry.
      static Kv *last_entry = NULL;

      Kv target;
      target.key = 0;
      target.value.c = *pp + 2;
      target.value.len = 0;   // not used, see cmp_keyvalue_value_n()

      if (last_entry != NULL && cmp_keyvalue_value_n(&target, last_entry) == 0)
         entry = last_entry;
      else
         entry = (Kv *)bsearch(&target, &char_characterClasses,
                  ARRAY_LENGTH(char_characterClasses),
                  sizeof(char_characterClasses[0]), cmp_keyvalue_value_n
         );
      if (entry) {
         last_entry = entry;
         *pp += entry->value.len + 2;
         return entry->key;
      }
   }
   return CHAR_CLASS_NONE;
}

//Start a timer that will cause the regexp to abort after "msec".
//This doesn't work well recursively.  In case it happens anyway, the first
//set timeout will prevail, nested ones are ignored.
//The caller must make sure there is a matching disable_regexp_timeout() call!
public void
init_regexp_timeout(long msec) {
   if (timeout_nesting == 0)
      timeout_flag = start_timeout(msec);
   ++timeout_nesting;
}

public void
disable_regexp_timeout(void) {
   if (timeout_nesting == 0)
      internalErrMsg(S"disable_regexp_timeout() called without active timer");
   ei (--timeout_nesting == 0) {
      stop_timeout();
      timeout_flag = &dummy_timeout_flag;
   }
}

// Return true if compiled regular expression "prog" can match a line break.
public int
re_multiline(RegProg* prog){
   return (prog->regflags & RF_HASNL);
}

// Check for an equivalence class name "[=a=]".  "pp" points to the '['.
// Returns a character representing the class. Zero means that no item was
// recognized.  Otherwise "pp" is advanced to after the item.
private int
get_equi_class(Byte **pp) {
   Unt c;
   int l = 1;
   Byte* p = *pp;

   if (p[1] == '=' && p[2] != ZERO) {
      l = utfCharLen(p + 2);
      if (p[l + 2] == '=' && p[l + 3] == ']') {
         c = mb_ptr2char(p + 2);
         *pp += l + 4;
         return c;
      }
   }
   return 0;
}

//{{{reading and skipping

//Check for a collating element "[.a.]".  "pp" points to the '['.
//Returns a character. Zero means that no item was recognized.  Otherwise
//"pp" is advanced to after the item.
//Currently only single characters are recognized!
private int
get_coll_element(Byte **pp) {
   Unt      c;
   int      l = 1;
   Byte   *p = *pp;

   if (p[0] != ZERO && p[1] == '.' && p[2] != ZERO) {
      l = utfCharLen(p + 2);
      if (p[l + 2] == '.' && p[l + 3] == ']') {
         c = mb_ptr2char(p + 2);
         *pp += l + 4;
         return c;
      }
   }
   return 0;
}

// Skip over a "[]" range. "p" must point to the character after the '['.
// The returned pointer is on the matching ']', or the terminating ZERO.
private Byte *
skip_anyof(Byte *p) {
   int l;

   if (*p == '^')   // Complement of range.
      ++p;
   if (*p == ']' || *p == '-')
      ++p;
   while (*p != ZERO && *p != ']') {
      if ((l = utfCharLen(p)) > 1) {
         p += l;
      } ei (*p == '-') {
          ++p;
          if (*p != ']' && *p != ZERO)
             MB_PTR_ADV(p);
      } ei (*p == '\\'
         && (firstOccurrence(REGEXP_INRANGE, p[1]) != NULL || (firstOccurrence(REGEXP_ABBR, p[1]) != NULL))) {
          p += 2;
      } ei (*p == '[') {
          if (get_char_class(&p) == CHAR_CLASS_NONE
             && get_equi_class(&p) == 0
             && get_coll_element(&p) == 0
             && *p != ZERO)
         ++p; // it is not a class name and not ZERO
      } else
         ++p;
   }

   return p;
}

//Skip past regular expression.
//Stop at end of "startp" or where "delim" is found ('/', '?', etc).
//Take care of characters with a backslash in front of it.
//Skip strings inside [ and ].
public Byte *
skip_regexp(Byte   *startp, int      delim, int      magic) {
   return skip_regexp_ex(startp, delim, magic, NULL, NULL, NULL);
}

// Call skip_regexp() and when the delimiter does not match give an error and return NULL.
public Byte *
skip_regexp_err( Byte   *startp, int      delim, int      magic) {
   Byte *p = skip_regexp(startp, delim, magic);

   if (*p != delim) {
      showErrFmtMsg(_(e_missing_delimiter_after_search_pattern_str), startp);
      return NULL;
   }
   return p;
}

//skip_regexp() with extra arguments:
//When "newp" is not NULL and "dirc" is '?', make an allocated copy of the expression and change 
//"\?" to "?".  If "*newp" is not NULL the expression is changed in-place.
//If a "\?" is changed to "?" then "dropped" is incremented, unless NULL.
//If "magic_val" is not NULL, returns the effective magicness of the pattern
public Byte *
skip_regexp_ex(
   Byte* startp,
   int dirc,
   int magic,
   Byte** newp,
   int* dropped,
   Magic* magic_val
) {
   Magic mymagic;
   Byte* p = startp;
   Unt startplen = 0;

   if (magic)
      mymagic = MAGIC_ON;
   else
      mymagic = MAGIC_OFF;

   for (; p[0] != ZERO; MB_PTR_ADV(p)) {
      if (p[0] == dirc)   // found end of regexp
          break;
      if ((p[0] == '[' && mymagic >= MAGIC_ON) 
            || (p[0] == '\\' && p[1] == '[' && mymagic <= MAGIC_OFF)
      ){
         p = skip_anyof(p + 1);
         if (p[0] == ZERO)
            break;
      } ei (p[0] == '\\' && p[1] != ZERO) {
         if (dirc == '?' && newp != NULL && p[1] == '?') {
         // change "\?" to "?", make a copy first.
         if (startplen == 0)
             startplen = STRLEN(startp);
         if (*newp == NULL) {
            *newp = copySubstr(startp, startplen);
            if (*newp != NULL) {
               p = *newp + (p - startp);
               startp = *newp;
            }
         }
         if (dropped != NULL)
            ++*dropped;
         if (*newp != NULL)
            MEMMOVE(p, p + 1, startplen - ((p + 1) - startp) + 1);
         else
            ++p;
         } else
            ++p;    // skip next character
         if (*p == 'v')
            mymagic = MAGIC_ALL;
         ei (*p == 'V')
            mymagic = MAGIC_NONE;
      }
   }
   if (magic_val != NULL)
      *magic_val = mymagic;
   return p;
}

// Functions for getting characters from the regexp input.
private int   prevchr_len;   // byte length of previous char
private int   at_start;   // True when on the first character
private int   prev_at_start;  // True when on the second character

// Start parsing at "str".
private void
initchr(Byte *str) {
   regparse = str;
   prevchr_len = 0;
   curchr = prevprevchr = prevchr = nextchr = -1;
   at_start = true;
   prev_at_start = false;
}

// Save the current parse state, so that it can be restored and parsing restarts in the same state
private void
saveParseState(ParseState *ps) {
   ps->regparse = regparse;
   ps->prevchr_len = prevchr_len;
   ps->curchr = curchr;
   ps->prevchr = prevchr;
   ps->prevprevchr = prevprevchr;
   ps->nextchr = nextchr;
   ps->at_start = at_start;
   ps->prev_at_start = prev_at_start;
   ps->regnpar = regnpar;
}

// Restore a previously saved parse state.
private void
restoreParseState(ParseState *ps) {
   regparse = ps->regparse;
   prevchr_len = ps->prevchr_len;
   curchr = ps->curchr;
   prevchr = ps->prevchr;
   prevprevchr = ps->prevprevchr;
   nextchr = ps->nextchr;
   at_start = ps->at_start;
   prev_at_start = ps->prev_at_start;
   regnpar = ps->regnpar;
}


// Get the next character without advancing.
private Unt
peekchr(void) {
   static int after_slash = false;
   if (curchr != UNT)
      return curchr;

   switch (curchr = regparse[0]) {
   case '.':
   case '[':
   case '~':
      // magic when 'magic' is on
      if (reg_magic >= MAGIC_ON)
         curchr = Magic(curchr);
      break;
   case '(':
   case ')':
   case '{':
   case '%':
   case '+':
   case '=':
   case '?':
   case '@':
   case '!':
   case '&':
   case '|':
   case '<':
   case '>':
   case '#':   // future ext.
   case '"':   // future ext.
   case '\'':   // future ext.
   case ',':   // future ext.
   case '-':   // future ext.
   case ':':   // future ext.
   case ';':   // future ext.
   case '`':   // future ext.
   case '/':   // Can't be used in / command
      // magic only after "\v"
      if (reg_magic == MAGIC_ALL)
         curchr = Magic(curchr);
      break;
   case '*':
       // * is not magic as the very first character, eg "?*ptr", when
       // after '^', eg "/^*ptr" and when after "\(", "\|", "\&".  But
      // "\(\*" is not magic, thus must be magic if "after_slash"
      if (reg_magic >= MAGIC_ON
          && !at_start
          && !(prev_at_start && prevchr == Magic('^'))
          && (after_slash
            || (prevchr != Magic('(')
                && prevchr != Magic('&')
                && prevchr != Magic('|')))
      )
         curchr = Magic('*');
      break;
   case '^':
      // '^' is only magic as the very first character and if it's after
      // "\(", "\|", "\&' or "\n"
      if (reg_magic >= MAGIC_OFF
          && (at_start
            || reg_magic == MAGIC_ALL
            || prevchr == Magic('(')
            || prevchr == Magic('|')
            || prevchr == Magic('&')
            || prevchr == Magic('n')
            || (no_Magic(prevchr) == '('
                && prevprevchr == Magic('%')))
      ){
         curchr = Magic('^');
         at_start = true;
         prev_at_start = false;
      }
      break;
   case '$':
       // '$' is only magic as the very last char and if it's in front of
       // either "\|", "\)", "\&", or "\n"
      if (reg_magic >= MAGIC_OFF) {
         Byte *p = regparse + 1;
         int is_magic_all = (reg_magic == MAGIC_ALL);

         // ignore \c \C \m \M \v \V and \Z after '$'
         while (p[0] == '\\' && (p[1] == 'c' || p[1] == 'C'
                || p[1] == 'm' || p[1] == 'M'
                || p[1] == 'v' || p[1] == 'V' || p[1] == 'Z'))
         {
            if (p[1] == 'v')
               is_magic_all = true;
            ei (p[1] == 'm' || p[1] == 'M' || p[1] == 'V')
               is_magic_all = false;
            p += 2;
         }
         if (p[0] == ZERO
               || (p[0] == '\\'
                   && (p[1] == '|' || p[1] == '&' || p[1] == ')' || p[1] == 'n'))
               || (is_magic_all
                   && (p[0] == '|' || p[0] == '&' || p[0] == ')'))
               || reg_magic == MAGIC_ALL
         )
             curchr = Magic('$');
      }
      break;
   case '\\': {
      Unt c = regparse[1];

      if (c == ZERO)
         curchr = '\\';   // trailing '\'
      ei (c <= '~' && META_flags[c]) {
          /*
           * META contains everything that may be magic sometimes,
           * except ^ and $ ("\^" and "\$" are only magic after
           * "\V").  We now fetch the next character and toggle its
           * magicness.  Therefore, \ is so meta-magic that it is
           * not in META.
           */
          curchr = -1;
          prev_at_start = at_start;
          at_start = false;   // be able to say "/\*ptr"
          ++regparse;
          ++after_slash;
          peekchr();
          --regparse;
          --after_slash;
          curchr = toggle_Magic(curchr);
      } ei (firstOccurrence(REGEXP_ABBR, c)) {
         // Handle abbreviations, like "\t" for TAB -- webb
         curchr = backslash_trans(c);
      } ei (reg_magic == MAGIC_NONE && (c == '$' || c == '^')) {
         curchr = toggle_Magic(c);
      } else {
         //Next character can never be (made) magic? Then backslashing it won't do anything.
         curchr = mb_ptr2char(regparse + 1);
      }
      break;
      }

   default:
      curchr = mb_ptr2char(regparse);
   }

   return curchr;
}

// Eat one lexed character.  Do this in a way that we can undo it.
private void
skipchr(void) {
    // peekchr() eats a backslash, do the same here
    if (*regparse == '\\')
   prevchr_len = 1;
    else
   prevchr_len = 0;
    if (regparse[prevchr_len] != ZERO) {
       // exclude composing chars that utfCharLen does include
       prevchr_len += utf_ptr2len(regparse + prevchr_len);
   }
   regparse += prevchr_len;
   prev_at_start = at_start;
   at_start = false;
   prevprevchr = prevchr;
   prevchr = curchr;
   curchr = nextchr;       // use previously unget char, or -1
   nextchr = -1;
}

/*
 * Skip a character while keeping the value of prev_at_start for at_start.
 * prevchr and prevprevchr are also kept.
 */
private void
skipchr_keepstart(void) {
   int as = prev_at_start;
   int pr = prevchr;
   int prpr = prevprevchr;

   skipchr();
   at_start = as;
   prevchr = pr;
   prevprevchr = prpr;
}

//Get the next character from the pattern. We know about magic and such, so
//therefore we need a lexical analyzer.
private Unt
getchr(void) {
   int chr = peekchr();
   skipchr();
   return chr;
}

//Get and return the value of the hex string at the current position.
//Return -1 if there is no valid hex number.
//The position is updated:
//    blahblah\%x20asdf
//     before-^ ^-after
//The parameter controls the maximum number of input characters. This will be
//2 when reading a \%x20 sequence and 4 when reading a \%u20AC sequence.
private Long
gethexchrs(int maxinputlen) {
   Ulong   nr = 0;
   int      c;
   int      i;

   for (i = 0; i < maxinputlen; ++i) {
      c = regparse[0];
      if (!eeIsXDigit(c))
         break;
      nr <<= 4;
      nr |= hex2nr(c);
      ++regparse;
   }

   if (i == 0)
      return -1;
   return nr;
}

//Get and return the value of the decimal string immediately after the
//current position. Return -1 for invalid. Consume all digits.
private Long
getdecchrs(void) {
   Ulong   nr = 0;
   Unt      c;
   int      i;

   for (i = 0; ; ++i) {
      c = regparse[0];
      if (c < '0' || c > '9')
         break;
      nr *= 10;
      nr += c - '0';
      ++regparse;
      curchr = -1; // no longer valid
   }

   if (i == 0)
      return -1;
   return nr;
}


/*
 * read_limits - Read two integers to be taken as a minimum and maximum.
 * If the first character is '-', then the range is reversed.
 * Should end with 'end'.  If minval is missing, zero is default, if maxval is
 * missing, a very big number is the default.
 */
private int
read_limits(long *minval, long *maxval) {
    int      reverse = false;
    Byte   *first_char;
    long   tmp;

    if (*regparse == '-') {
   // Starts with '-', so reverse the range later
   regparse++;
   reverse = true;
    }
    first_char = regparse;
    *minval = parseLong(&regparse);
    if (*regparse == ',') {      // There is a comma
   if (eeIsDigit(*++regparse))
       *maxval = parseLong(&regparse);
   else
       *maxval = MAX_LIMIT;
    }
    ei (EE_ISDIGIT(*first_char))
   *maxval = *minval;       // It was \{n} or \{-n}
    else
   *maxval = MAX_LIMIT;       // It was \{} or \{-}
    if (*regparse == '\\')
   regparse++;   // Allow either \{...} or \{...\}
   if (*regparse != '}')
      EMSG2_RET_FAIL(_(e_syntax_error_in_str_curlies), reg_magic == MAGIC_ALL);

   /*
    * Reverse the range if there was a '-', or make sure it is in the right
    * order otherwise.
    */
   if ((!reverse && *minval > *maxval) || (reverse && *minval < *maxval)) {
      tmp = *minval;
      *minval = *maxval;
      *maxval = tmp;
   }
   skipchr();      // let's be friends with the lexer again
   return OK;
}

//}}}

//eeRegexec and friends

// Global work variables for eeRegexec().
private void cleanup_subexpr(void);
private void cleanup_zsubexpr(void);
private int  match_with_backref(LineNr start_lnum, ColNr start_col, LineNr end_lnum, ColNr end_col, int *bytelen);

//Sometimes need to save a copy of a line.  Since alloc()/free() is very
//slow, we keep one allocated piece of memory and only re-allocate it when
//it's too small.  It's freed in bt_regexec_both() when finished.
private Byte* reg_tofree = NULL;
private Unt reg_tofreelen;

//Structure used to store the execution state of the regex 
//Which ones are set depends on whether a single-line or multi-line match is done:
//           single-line      multi-line
//match       &RegMatch       NULL
//multiMatch     NULL         &RegMultilineMatch
//reg_startp  match->startp   <invalid>
//reg_endp      match->endp   <invalid>
//reg_startpos  <invalid>     multiMatch->startpos
//reg_endpos    <invalid>     multiMatch->endpos
//portal         NULL         portal in which to search
//book           curBook      book in which to search
//reg_firstlnum   <invalid>   first line in which to search
//reg_maxline      0          last line nr
//reg_line_lbr false or true  false
private typedef struct {
   RegMatch* match;
   RegMultilineMatch* multiMatch;

   Byte** reg_startp;
   Byte** reg_endp;
   PosNoVirt* reg_startpos;
   PosNoVirt* reg_endpos;

   Portal* portal;
   Book* book;
   LineNr reg_firstlnum;
   LineNr reg_maxline;
   int reg_line_lbr;   // "\n" in string is line break

   // The current match-position is stord in these variables:
   LineNr lnum;   // line number, relative to first line
   Byte* line;  // start of current line
   Byte* input; // current input, points into "line"

   int need_clear_subexpr;   // subexpressions still need to be cleared
   int need_clear_zsubexpr;   // extmatch subexpressions still need to be cleared

   // Internal copy of 'ignorecase'.  It is set at each call to eeRegexec().
   // Normally it gets the value of "rm_ic" or "rmm_ic", but when the pattern
   // contains '\c' or '\C' the value is overruled.
   int reg_ic;

   // Similar to "reg_ic", but only for 'combining' characters.  Set with \Z
   // flag in the regexp.  Defaults to false, always.
   int         reg_icombine;

   // Copy of "rmm_maxcol": maximum column to search for a match.  Zero when
   // there is no maximum.
   ColNr      reg_maxcol;

   // RState for the regexec.
   int nfa_has_zend;       // NFA regexp \ze operator encountered.
   int nfa_has_backref;    // NFA regexp \1 .. \9 encountered.
   int nfa_nsubexpr;       // Number of sub expressions actually being used
            // during execution. 1 if only the whole match
            // (subexpr 0) is used.
   // listid is global, so that it increases on recursive calls to
   // match(), which means we don't have to clear the lastlist field of all the states.
   int nfa_listid;
   int nfa_alt_listid;

   int nfa_has_zsubexpr;   // NFA regexp has \z( ), set zsubexpr.
} Execution;

private Execution   exe;
private int      isBusyS = false;

//Return true if character 'c' is included in 'iskeyword' option for "buf" buffer.
private int
reg_iswordc(int c) {
   return eeIsWordc_buf(c, exe.book);
}

private int can_f_submatch = false;   // true when submatch() can be used

// This struct is used for reg_submatch(). Needed for when the
// substitution string is an expression that contains a call to substitute()
// and submatch().
private typedef struct {
   RegMatch   *sm_match;
   RegMultilineMatch   *sm_mmatch;
   LineNr   sm_firstlnum;
   LineNr   sm_maxline;
   int      sm_line_lbr;
} regsubMatch;

private regsubMatch rsm;  // can only be used when can_f_submatch is true

private typedef enum {
    RGLF_LINE = 0x01,
    RGLF_LENGTH = 0x02,
    RGLF_SUBMATCH = 0x04
} reg_getline_flags_T;

//
// common code for reg_getline(), reg_getline_len(), reg_getline_submatch() and
// reg_getline_submatch_len().
// the flags argument (which is a bitmask) controls what info is to be returned and whether
// or not submatch is in effect.
// note:
private void
reg_getline_common(
    LineNr      lnum,
    reg_getline_flags_T   flags,
    Byte      **line,
    ColNr      *length)
{
    int get_line = flags & RGLF_LINE;
    int get_length = flags & RGLF_LENGTH;
    LineNr firstlnum;
    LineNr maxline;

   if (flags & RGLF_SUBMATCH) {
      firstlnum = rsm.sm_firstlnum + lnum;
      maxline = rsm.sm_maxline;
   } else {
      firstlnum = exe.reg_firstlnum + lnum;
      maxline = exe.reg_maxline;
   }

   // when looking behind for a match/no-match lnum is negative. but we can't go before line 1.
   if (firstlnum < 1) {
      if (get_line)
         *line = NULL;
      if (get_length)
         *length = 0;

      return;
   }

   if (lnum > maxline) {
      // must have matched the "\n" in the last line.
      if (get_line)
         *line = S"";
      if (get_length)
         *length = 0;

      return;
   }

   if (get_line)
      *line = memGetLine(exe.book, firstlnum, false);
   if (get_length)
      *length = memGetBookLen(exe.book, firstlnum);
}

// Get pointer to the line "lnum", which is relative to "reg_firstlnum".
private Byte *
reg_getline(LineNr lnum) {
   Byte *line;

   reg_getline_common(lnum, RGLF_LINE, &line, NULL);

   return line;
}

// Get length of line "lnum", which is relative to "reg_firstlnum".
private ColNr
reg_getline_len(LineNr lnum) {
   ColNr length;

   reg_getline_common(lnum, RGLF_LENGTH, NULL, &length);

   return length;
}

private Byte   *reg_startzp[NSUBEXP];   // Workspace to mark beginning
private Byte   *reg_endzp[NSUBEXP];   //   and end of \z(...\) matches
private PosNoVirt   reg_startzpos[NSUBEXP];   // idem, beginning pos
private PosNoVirt   reg_endzpos[NSUBEXP];   // idem, end pos

// true if using multi-line regexp.
#define REG_MULTI   (exe.match == NULL)

// Create a new extmatch and mark it as referenced once.
private RegExternalMatch *
make_extmatch(void){
   RegExternalMatch* em = ALLOC_CLEAR_ONE(RegExternalMatch);
   if (em)
      em->refcnt = 1;
   return em;
}

// Add a reference to an extmatch.
public RegExternalMatch *
ref_extmatch(RegExternalMatch *em){
   if (em)
      em->refcnt++;
   return em;
}

// Remove a reference to an extmatch.  If there are no references left, free the info.
public void
unref_extmatch(RegExternalMatch *em) {
   if (em && --em->refcnt <= 0) {
      for (int i = 0; i < NSUBEXP; ++i)
         eeglFree(em->matches[i]);
      eeglFree(em);
   }
}

// Get class of previous character.
private int
reg_prev_class(void) {
   if (exe.input > exe.line)
      return inpGetClassForBook(exe.input - 1 - mb_head_off(exe.line, exe.input - 1), exe.book);
   return -1;
}

// Return true if the current exe.input position matches the Visual area.
private int
reg_match_visual(void) {
   Pos   top, bot;
   LineNr    lnum;
   ColNr   col;
   Portal   *wp = exe.portal == NULL ? curPor : exe.portal;
   int      mode;
   ColNr   start, end;
   ColNr   start2, end2;
   ColNr   cols;
   ColNr   curswant;

   // Check if the book is the current book and not using a string.
   if (exe.book != curBook || VIsual.lnum == 0 || !REG_MULTI)
      return false;

   if (VIsual_active) {
      if (LT_POS(VIsual, wp->cursor)) {
         top = VIsual;
         bot = wp->cursor;
      } else {
         top = wp->cursor;
         bot = VIsual;
      }
      mode = VIsual_mode;
      curswant = wp->cursWant;
   } else {
      if (LT_POS(curBook->visual.vi_start, curBook->visual.vi_end)) {
         top = curBook->visual.vi_start;
         bot = curBook->visual.vi_end;
      } else {
         top = curBook->visual.vi_end;
         bot = curBook->visual.vi_start;
      }
      // a substitute command may have removed some lines
      if (bot.lnum > curBook->mem.lineCount)
         bot.lnum = curBook->mem.lineCount;
      mode = curBook->visual.vi_mode;
      curswant = curBook->visual.vi_curswant;
   }
   lnum = exe.lnum + exe.reg_firstlnum;
   if (lnum < top.lnum || lnum > bot.lnum)
      return false;

   col = (ColNr)(exe.input - exe.line);
   if (mode == 'v') {
      if ((lnum == top.lnum && col < top.col)
            || (lnum == bot.lnum && col >= bot.col + 1))
         return false;
   } ei (mode == Ctrl_V) {
      bookGetVirtualColInVirtualMode(wp, &top, &start, NULL, &end);
      bookGetVirtualColInVirtualMode(wp, &bot, &start2, NULL, &end2);
      if (start2 < start)
         start = start2;
      if (end2 > end)
          end = end2;
      if (top.col == MAXCOL || bot.col == MAXCOL || curswant == MAXCOL)
          end = MAXCOL;

      // bookGetVirtualColInVirtualMode() flushes exe.line, need to get it again
      exe.line = reg_getline(exe.lnum);
      exe.input = exe.line + col;

      cols = drawLineOnScreentabsize(wp, exe.reg_firstlnum + exe.lnum, exe.line, col);
      if (cols < start || cols > end)
          return false;
   }
    return true;
}

// Cleanup the subexpressions, if this wasn't done yet.
// This construction is used to clear the subexpressions only when they are used (to increase speed)
private void
cleanup_subexpr(void) {
   if (!exe.need_clear_subexpr)
      return;

   if (REG_MULTI) {
      // Use 0xff to set lnum to -1
      memset(exe.reg_startpos, 0xff, sizeof(PosNoVirt) * NSUBEXP);
      memset(exe.reg_endpos, 0xff, sizeof(PosNoVirt) * NSUBEXP);
   } else {
      memset(exe.reg_startp, 0, sizeof(CS) * NSUBEXP);
      memset(exe.reg_endp, 0, sizeof(CS) * NSUBEXP);
   }
   exe.need_clear_subexpr = false;
}

private void
cleanup_zsubexpr(void) {
   if (!exe.need_clear_zsubexpr)
      return;

   if (REG_MULTI) {
      // Use 0xff to set lnum to -1
      memset(reg_startzpos, 0xff, sizeof(PosNoVirt) * NSUBEXP);
      memset(reg_endzpos, 0xff, sizeof(PosNoVirt) * NSUBEXP);
   } else {
      memset(reg_startzp, 0, sizeof(CS) * NSUBEXP);
      memset(reg_endzp, 0, sizeof(CS) * NSUBEXP);
   }
   exe.need_clear_zsubexpr = false;
}

// Advance exe.lnum, exe.line and exe.input to the next line.
private void
reg_nextline(void) {
    exe.line = reg_getline(++exe.lnum);
    exe.input = exe.line;
    fast_breakcheck();
}

// Check whether a backreference matches.
// Returns RA_FAIL, RA_NOMATCH or RA_MATCH.
// If "bytelen" is not NULL, it is set to the byte length of the match in the last line.
private int
match_with_backref(
    LineNr start_lnum,
    ColNr  start_col,
    LineNr end_lnum,
    ColNr  end_col,
    int        *bytelen)
{
    LineNr   clnum = start_lnum;
    ColNr   ccol = start_col;
    int      len;
    Byte   *p;

    if (bytelen != NULL)
   *bytelen = 0;
    for (;;) {
   // Since getting one line may invalidate the other, need to make copy. Slow!
   if (exe.line != reg_tofree) {
      len = (int)STRLEN(exe.line);
      if (reg_tofree == NULL || len >= (int)reg_tofreelen) {
         len += 50;   // get some extra
         eeglFree(reg_tofree);
         reg_tofree = alloc(len);
         reg_tofreelen = len;
      }
      STRCPY(reg_tofree, exe.line);
      exe.input = reg_tofree + (exe.input - exe.line);
      exe.line = reg_tofree;
   }

   // Get the line to compare with.
   p = reg_getline(clnum);
   if (clnum == end_lnum)
       len = end_col - ccol;
   else
       len = (int)reg_getline_len(clnum) - ccol;

   if (cstrncmp(p + ccol, exe.input, &len) != 0)
       return RA_NOMATCH;  // doesn't match
   if (bytelen != NULL)
       *bytelen += len;
   if (clnum == end_lnum)
       break;      // match and at end!
   if (exe.lnum >= exe.reg_maxline)
       return RA_NOMATCH;  // text too short

   // Advance to next line.
   reg_nextline();
   if (bytelen != NULL)
       *bytelen = 0;
   ++clnum;
   ccol = 0;
   if (gotInterruptG)
       return RA_FAIL;
    }

    // found a match! Note that exe.line may now point to a copy of the line: that should not matter
    return RA_MATCH;
}

// Used in a place where no * or \+ can follow.
private int
re_mult_next(CS what) {
   if (re_multi_type(peekchr()) == MULTI_MULT) {
      showErrFmtMsg(_(e_nfa_regexp_cannot_repeat_str), what);
      anyRegexEmsgG = true;
      return FAIL;
   }
   return OK;
}

private typedef struct {
   int a, b, c;
} decomp_T;


// 0xfb20 - 0xfb4f
private decomp_T decomp_table[0xfb4f-0xfb20+1] = {
    {0x5e2,0,0},      // 0xfb20   alt ayin
    {0x5d0,0,0},      // 0xfb21   alt alef
    {0x5d3,0,0},      // 0xfb22   alt dalet
    {0x5d4,0,0},      // 0xfb23   alt he
    {0x5db,0,0},      // 0xfb24   alt kaf
    {0x5dc,0,0},      // 0xfb25   alt lamed
    {0x5dd,0,0},      // 0xfb26   alt mem-sofit
    {0x5e8,0,0},      // 0xfb27   alt resh
    {0x5ea,0,0},      // 0xfb28   alt tav
    {'+', 0, 0},      // 0xfb29   alt plus
    {0x5e9, 0x5c1, 0},      // 0xfb2a   shin+shin-dot
    {0x5e9, 0x5c2, 0},      // 0xfb2b   shin+sin-dot
    {0x5e9, 0x5c1, 0x5bc},   // 0xfb2c   shin+shin-dot+dagesh
    {0x5e9, 0x5c2, 0x5bc},   // 0xfb2d   shin+sin-dot+dagesh
    {0x5d0, 0x5b7, 0},      // 0xfb2e   alef+patah
    {0x5d0, 0x5b8, 0},      // 0xfb2f   alef+qamats
    {0x5d0, 0x5b4, 0},      // 0xfb30   alef+hiriq
    {0x5d1, 0x5bc, 0},      // 0xfb31   bet+dagesh
    {0x5d2, 0x5bc, 0},      // 0xfb32   gimel+dagesh
    {0x5d3, 0x5bc, 0},      // 0xfb33   dalet+dagesh
    {0x5d4, 0x5bc, 0},      // 0xfb34   he+dagesh
    {0x5d5, 0x5bc, 0},      // 0xfb35   vav+dagesh
    {0x5d6, 0x5bc, 0},      // 0xfb36   zayin+dagesh
    {0xfb37, 0, 0},      // 0xfb37 -- UNUSED
    {0x5d8, 0x5bc, 0},      // 0xfb38   tet+dagesh
    {0x5d9, 0x5bc, 0},      // 0xfb39   yud+dagesh
    {0x5da, 0x5bc, 0},      // 0xfb3a   kaf sofit+dagesh
    {0x5db, 0x5bc, 0},      // 0xfb3b   kaf+dagesh
    {0x5dc, 0x5bc, 0},      // 0xfb3c   lamed+dagesh
    {0xfb3d, 0, 0},      // 0xfb3d -- UNUSED
    {0x5de, 0x5bc, 0},      // 0xfb3e   mem+dagesh
    {0xfb3f, 0, 0},      // 0xfb3f -- UNUSED
    {0x5e0, 0x5bc, 0},      // 0xfb40   nun+dagesh
    {0x5e1, 0x5bc, 0},      // 0xfb41   samech+dagesh
    {0xfb42, 0, 0},      // 0xfb42 -- UNUSED
    {0x5e3, 0x5bc, 0},      // 0xfb43   pe sofit+dagesh
    {0x5e4, 0x5bc,0},      // 0xfb44   pe+dagesh
    {0xfb45, 0, 0},      // 0xfb45 -- UNUSED
    {0x5e6, 0x5bc, 0},      // 0xfb46   tsadi+dagesh
    {0x5e7, 0x5bc, 0},      // 0xfb47   qof+dagesh
    {0x5e8, 0x5bc, 0},      // 0xfb48   resh+dagesh
    {0x5e9, 0x5bc, 0},      // 0xfb49   shin+dagesh
    {0x5ea, 0x5bc, 0},      // 0xfb4a   tav+dagesh
    {0x5d5, 0x5b9, 0},      // 0xfb4b   vav+holam
    {0x5d1, 0x5bf, 0},      // 0xfb4c   bet+rafe
    {0x5db, 0x5bf, 0},      // 0xfb4d   kaf+rafe
    {0x5e4, 0x5bf, 0},      // 0xfb4e   pe+rafe
    {0x5d0, 0x5dc, 0}      // 0xfb4f   alef-lamed
};

private void
mb_decompose(int c, int *c1, int *c2, int *c3) {
   decomp_T d;

   if (c >= 0xfb20 && c <= 0xfb4f) {
      d = decomp_table[c - 0xfb20];
      *c1 = d.a;
      *c2 = d.b;
      *c3 = d.c;
   } else {
      *c1 = c;
      *c2 = 0;
      *c3 = 0;
   }
}

/*
 * Compare two strings, ignore case if exe.reg_ic set.
 * Return 0 if strings match, non-zero otherwise.
 * Correct the length "*n" when composing characters are ignored
 * or for utf8 when both utf codepoints are considered equal because of
 * case-folding but have different length (e.g. 's' and 'ſ')
 */
private int
cstrncmp(Byte *s1, Byte *s2, int *n) {
    int      result;

   if (!exe.reg_ic)
      result = STRNCMP(s1, s2, *n);
   else {
      Byte *p = s1;
      int n2 = 0;
      int n1 = *n;
      // count the number of characters for byte-length of s1
      while (n1 > 0 && *p != ZERO) {
          n1 -= utfCharLen(s1);
          MB_PTR_ADV(p);
          n2++;
      }
      // count the number of bytes to advance the same number of chars for s2
      p = s2;
      while (n2-- > 0 && *p != ZERO)
         MB_PTR_ADV(p);

      n2 = p - s2;

      result = caseInsensitiveCompareNChars2(s1, s2, *n, n2);
      if (result == 0 && n2 < *n)
         *n = n2;
   }

   // if it failed and it's utf8 and we want to combineignore:
   if (result != 0 && exe.reg_icombine) {
      Byte   *str1, *str2;
      int   c1, c2, c11, c12;
      int   junk;

      // we have to handle the strcmp ourselves, since it is necessary to
      // deal with the composing characters by ignoring them:
      str1 = s1;
      str2 = s2;
      c1 = c2 = 0;
      while ((int)(str1 - s1) < *n) {
         c1 = strAdvanceMultibyte(&str1);
         c2 = strAdvanceMultibyte(&str2);

         // Decompose the character if necessary, into 'base' characters.
         // Currently hard-coded for Hebrew, Arabic to be done...
         if (c1 != c2 && (!exe.reg_ic || utf_fold(c1) != utf_fold(c2))) {
            // decomposition necessary?
            mb_decompose(c1, &c11, &junk, &junk);
            mb_decompose(c2, &c12, &junk, &junk);
            c1 = c11;
            c2 = c12;
            if (c11 != c12
                   && (!exe.reg_ic || utf_fold(c11) != utf_fold(c12)))
                break;
         }
      }
      result = c2 - c1;
      if (result == 0)
         *n = (int)(str2 - s2);
   }

    return result;
}

//cstrchr: This function is used a lot for simple searches, keep it fast!
private Byte *
cstrchr(Byte *s, int c) {
   Byte   *p;
   Unt      cc, lc;

   if (!exe.reg_ic)
      return firstOccurrence(s, c);

   // tolower() and toupper() can be slow, comparing twice should be a lot
   // faster (esp. when using MS Visual C++!).
   // For UTF-8 need to use folded case.
   if (c > 0x80) {
      cc = utf_fold(c);
      lc = cc;
   } else {
      if (MB_ISUPPER(c)) {
         cc = MB_TOLOWER(c);
         lc = cc;
      } ei (MB_ISLOWER(c)) {
         cc = MB_TOUPPER(c);
         lc = c;
      } else
         return firstOccurrence(s, c);
   }
   
   for (p = s; *p != ZERO; p += utfCharLen(p)) {
      Unt uc = mb_ptr2char(p);
      if (c > 0x80 || uc > 0x80) {
         // Do not match an illegal byte.  E.g. 0xff matches 0xc3 0xbf, not 0xff.
         // compare with lower case of the character
         if ((uc < 0x80 || uc != *p) && utf_fold(uc) == lc)
            return p;
      } ei (*p == c || *p == cc)
         return p;
   }

    return NULL;
}

//}}}
//{{{substitutions

private typedef void (*AllOrOne)(int *, int);

private int eeRegsub_both(Byte *source, Var *expr, Byte *dest, int destlen, Unt flags);

private void
do_upper(int *d, int c) {
    *d = MB_TOUPPER(c);
}

private void
do_lower(int *d, int c) {
    *d = MB_TOLOWER(c);
}

//regtilde(): Replace tildes in the pattern by the old pattern.
//
//Short explanation of the tilde: It stands for the previous replacement
//pattern.  If that previous pattern also contains a ~ we should go back a
//step further...  But we insert the previous pattern into the current one
//and remember that.
//This still does not handle the case where "magic" changes.  So require the
//user to keep his hands off of "magic".
//
//The tildes are parsed once before the first call to eeRegsub().
public CS
regtilde(CS source) {
   Byte   *newsub = source;
   Byte   *p;
   Unt   newsublen = 0;
   Byte   tilde[3] = {'~', ZERO, ZERO};
   Unt   tildelen = 1;
   int      error = false;

   for (p = newsub; *p; ++p) {
      if (STRNCMP(p, tilde, tildelen) == 0) {
         Unt prefixlen = p - newsub;      // not including the tilde
         Byte *postfix = p + tildelen;
         Unt postfixlen;
         Unt tmpsublen;

         if (newsublen == 0)
            newsublen = STRLEN(newsub);
         newsublen -= tildelen;
         postfixlen = newsublen - prefixlen;
         tmpsublen = prefixlen + reg_prev_sublen + postfixlen;

         if (tmpsublen > 0 && reg_prev_sub != NULL) {
            Byte *tmpsub;

            // Avoid making the text longer than MAXCOL, it will cause
            // trouble at some point.
            if (tmpsublen > MAXCOL) {
               emsg(_(e_resulting_text_too_long));
               error = true;
               break;
            }
            tmpsub = alloc(tmpsublen + 1);

            // copy prefix
            MEMMOVE(tmpsub, newsub, prefixlen);
            // interpret tilde
            MEMMOVE(tmpsub + prefixlen, reg_prev_sub, reg_prev_sublen);
            // copy postfix
            STRCPY(tmpsub + prefixlen + reg_prev_sublen, postfix);

            if (newsub != source)   // allocated newsub before
                eeglFree(newsub);
            newsub = tmpsub;
            newsublen = tmpsublen;
            p = newsub + prefixlen + reg_prev_sublen;
          }
          else
         MEMMOVE(p, postfix, postfixlen + 1);   // remove the tilde (+1 for the ZERO)

          --p;
      } else {
         if (*p == '\\' && p[1])      // skip escaped characters
            ++p;
         p += utfCharLen(p) - 1;
      }
   }

   if (error) {
      if (newsub != source)
          eeglFree(newsub);
      return source;
   }

   // Store a copy of newsub  in reg_prev_sub.  It is always allocated,
   // because recursive calls may make the returned string invalid.
   // Only store it if there something to store.
   newsublen = p - newsub;
   if (newsublen == 0)
      EE_CLEAR(reg_prev_sub);
   else {
      eeglFree(reg_prev_sub);
      reg_prev_sub = copySubstr(newsub, newsublen);
   }

   if (reg_prev_sub == NULL)
      reg_prev_sublen = 0;
   else
      reg_prev_sublen = newsublen;

   return newsub;
}


// Put the submatches in "argv[argskip]" which is a list passed into
// call_func() by eeRegsub_both().
private int
fill_submatch_list(int argc UNUSED, Var *argv, int argskip, UserFunc *fp) {
   ListItem   *li;
   int      i;
   Byte   *s;
   Var   *listarg = argv + argskip;

   if (!has_varargs(fp) && fp->args.len <= argskip)
      // called function doesn't take a submatches argument
      return argskip;

   // Relies on list to be the first item in StaticList10.
   init_static_list((StaticList10 *)(listarg->list));

   // There are always 10 list items in StaticList10.
   li = listarg->list->first;
   for (i = 0; i < 10; ++i) {
      s = rsm.sm_match->startp[i];
      if (s == NULL || rsm.sm_match->endp[i] == NULL)
          s = NULL;
      else
          s = copySubstr(s, rsm.sm_match->endp[i] - s);
      li->c.tag = VAR_STRING;
      li->c.string = s;
      li = li->next;
    }
    return argskip + 1;
}

private void
clear_submatch_list(StaticList10 *sl) {
    int i;

    for (i = 0; i < 10; ++i)
   eeglFree(sl->items[i].c.string);
}

//eeRegsub() - perform substitutions after a eeRegexec() or
//eeRegexec_multi() match.
//
//If "flags" has REGSUB_COPY really copy into "dest[destlen]".
//Otherwise nothing is copied, only compute the length of the result.
//
//If "flags" has REGSUB_MAGIC then behave like 'magic' is set.
//
//If "flags" has REGSUB_BACKSLASH a backslash will be removed later, need to
//double them to keep them, and insert a backslash before a CR to avoid it
//being replaced with a line break later.
//
//Note: The matched text must not change between the call of
//eeRegexec()/eeRegexec_multi() and eeRegsub()! It would make the back references invalid!
//
//Return the size of the replacement, including terminating ZERO.
public int
eeRegsub(
   RegMatch* rmp,
   Byte* source,
   Var* expr,
   Byte* dest,
   int destlen,
   int flags
) {
   int result;
   Execution exeSaved;
   int isBusyS_save = isBusyS;

   if (isBusyS)
      // Being called recursively, save the state.
      exeSaved = exe;
   isBusyS = true;

    exe.match = rmp;
    exe.multiMatch = NULL;
    exe.reg_maxline = 0;
    exe.book = curBook;
    exe.reg_line_lbr = true;
    result = eeRegsub_both(source, expr, dest, destlen, flags);

    isBusyS = isBusyS_save;
    if (isBusyS)
   exe = exeSaved;

    return result;
}

public int
eeRegsub_multi(
   RegMultilineMatch   *rmp,
   LineNr   lnum,
   Byte   *source,
   Byte   *dest,
   int      destlen,
   int      flags)
{
   int      result;
   Execution   exeSaved;
   int      isBusyS_save = isBusyS;

   if (isBusyS)
      // Being called recursively, save the state.
      exeSaved = exe;
   isBusyS = true;

   exe.match = NULL;
   exe.multiMatch = rmp;
   exe.book = curBook;   // always works on the current book!
   exe.reg_firstlnum = lnum;
   exe.reg_maxline = curBook->mem.lineCount - lnum;
   exe.reg_line_lbr = false;
   result = eeRegsub_both(source, NULL, dest, destlen, flags);

   isBusyS = isBusyS_save;
   if (isBusyS)
      exe = exeSaved;

   return result;
}

// When nesting more than a couple levels it's probably a mistake.
#define MAX_REGSUB_NESTING 4
private Byte* eval_result[MAX_REGSUB_NESTING] = {NULL, NULL, NULL, NULL};

#if defined(EXITFREE) || defined(PROTO)
public void
free_resub_eval_result(void) {
   for (int i = 0; i < MAX_REGSUB_NESTING; ++i)
      EE_CLEAR(eval_result[i]);
}
# endif

private int
eeRegsub_both(
   CS source,
   Var* expr,
   CS dest,
   int destlen,
   Unt flags
) {
   CS src;
   CS dst;
   CS s;
   int      c;
   int      cc;
   int      no = -1;
   AllOrOne   func_all = (AllOrOne)NULL;
   AllOrOne   func_one = (AllOrOne)NULL;
   LineNr   clnum = 0;   // init for GCC
   int      len = 0;   // init for GCC
   static int  nesting = 0;
   int      nested;
   int      copy = flags & REGSUB_COPY;

   // Be paranoid...
   if ((source == NULL && expr == NULL) || dest == NULL) {
      internalErrMsg(e_null_argument);
      return 0;
   }
   if (nesting == MAX_REGSUB_NESTING) {
      emsg(_(e_substitute_nesting_too_deep));
      return 0;
   }
   nested = nesting;
   src = source;
   dst = dest;

   // When the substitute part starts with "\=" evaluate it as an expression.
   if (expr || (source[0] == '\\' && source[1] == '=')) {
      // To make sure that the length doesn't change between checking the
      // length and copying the string, and to speed up things, the
      // resulting string is saved from the call with
      // "flags & REGSUB_COPY" == 0 to the call with
      // "flags & REGSUB_COPY" != 0.
      if (copy) {
         if (eval_result[nested] != NULL) {
            int eval_len = (int)STRLEN(eval_result[nested]);

            if (eval_len < destlen) {
               STRCPY(dest, eval_result[nested]);
               dst += eval_len;
               EE_CLEAR(eval_result[nested]);
            }
         }
      } else {
          int          prev_can_f_submatch = can_f_submatch;
          regsubMatch   rsm_save;

          EE_CLEAR(eval_result[nested]);

         // The expression may contain substitute(), which calls us
         // recursively.  Make sure submatch() gets the text from the first level.
         if (can_f_submatch)
            rsm_save = rsm;
         can_f_submatch = true;
         rsm.sm_match = exe.match;
         rsm.sm_mmatch = exe.multiMatch;
         rsm.sm_firstlnum = exe.reg_firstlnum;
         rsm.sm_maxline = exe.reg_maxline;
         rsm.sm_line_lbr = exe.reg_line_lbr;

         // Although unlikely, it is possible that the expression invokes a
         // substitute command (it might fail, but still).  Therefore keep
         // an array of eval results.
         ++nesting;

         if (expr) {
            Var argv[2];
            Byte buf[NUMBUFLEN];
            Var returnVar;
            StaticList10 matchList;
            FnExe funcexe;

            returnVar.tag = VAR_STRING;
            returnVar.string = NULL;
            argv[0].tag = VAR_LIST;
            argv[0].list = &matchList.list;
            matchList.list.len = 0;
            CLEAR_FIELD(funcexe);
            funcexe.fe_argv_func = fill_submatch_list;
            funcexe.fe_evaluate = true;
            if (expr->tag == VAR_FUNC) {
               s = expr->string;
               call_func(s, -1, &returnVar, 1, argv, &funcexe);
            } ei (expr->tag == VAR_PARTIAL) {
               PartiallyApplied   *partial = expr->partial;

               s = partial_name(partial);
               funcexe.fe_partial = partial;
               call_func(s, -1, &returnVar, 1, argv, &funcexe);
            }
            if (matchList.list.len > 0)
                // fill_submatch_list() was called
                clear_submatch_list(&matchList);

            if (returnVar.tag == VAR_UNKNOWN)
                // something failed, no need to report another error
                eval_result[nested] = NULL;
            else {
               eval_result[nested] = convertVarToString(&returnVar, buf);
               if (eval_result[nested] != NULL)
                  eval_result[nested] = copyStr(eval_result[nested]);
            }
            clearVar(&returnVar);
         } else
            eval_result[nested] = eval_to_string(source + 2, true, false);
         --nesting;

         if (eval_result[nested] != NULL) {
            int had_backslash = false;

            for (s = eval_result[nested]; *s != ZERO; MB_PTR_ADV(s)) {
               // Change NL to CR, so that it becomes a line break,
               // unless called from eeRegexec_nl().
               // Skip over a backslashed character.
               if (*s == NL && !rsm.sm_line_lbr)
                  *s = ENTER;
               ei (*s == '\\' && s[1] != ZERO) {
                  ++s;
                  //Change NL to CR here too, so that this works:
                  //:s|abc\\\ndef|\="aaa\\\nbbb"|  on text:
                  //  abcBACKSLASH
                  //  def
                  //Not when called from eeRegexec_nl().
                  if (*s == NL && !rsm.sm_line_lbr)
                     *s = ENTER;
                  had_backslash = true;
                }
            }
            if (had_backslash && (flags & REGSUB_BACKSLASH)) {
               // Backslashes will be consumed, need to double them.
               s = copyStr_escaped(eval_result[nested], (CS)"\\");
               if (s != NULL) {
                  eeglFree(eval_result[nested]);
                  eval_result[nested] = s;
               }
            }

            dst += STRLEN(eval_result[nested]);
         }

         can_f_submatch = prev_can_f_submatch;
         if (can_f_submatch)
            rsm = rsm_save;
      }
   } else {
      while ((c = *src++) != ZERO) {
         if (c == '&' && (flags & REGSUB_MAGIC))
            no = 0;
         ei (c == '\\' && *src != ZERO) {
            if (*src == '&' && !(flags & REGSUB_MAGIC)) {
               ++src;
               no = 0;
            } ei ('0' <= *src && *src <= '9') {
               no = *src++ - '0';
            } ei (firstOccurrence((CS)"uUlLeE", *src)) {
               switch (*src++) {
               case 'u':   
                  func_one = do_upper;
                  continue;
               case 'U':  
                  func_all = do_upper;
                  continue;
               case 'l':
                  func_one = do_lower;
                  continue;
               case 'L':   
                  func_all = do_lower;
                  continue;
               case 'e':
               case 'E':   
                  func_all = null;
                  func_one = func_all;
                  continue;
               }
            }
         }
         if (no < 0) {        // Ordinary character.
            if (c == K_SPECIAL && src[0] != ZERO && src[1] != ZERO) {
               // Copy a special key as-is.
               if (copy) {
                  if (dst + 3 > dest + destlen) {
                     internalErrMsg(S"eeRegsub_both(): not enough space");
                     return 0;
                  }
                  *dst++ = c;
                  *dst++ = *src++;
                  *dst++ = *src++;
               } else {
                  dst += 3;
                  src += 2;
               }
               continue;
            }

            if (c == '\\' && *src != ZERO) {
               // Check for abbreviations -- webb
               switch (*src) {
                  case 'r':   c = ENTER;   ++src;   break;
                  case 'n':   c = NL;      ++src;   break;
                  case 't':   c = TAB;   ++src;   break;
                // Oh no!  \e already has meaning in subst pat :-(
                // case 'e':   c = ESC;   ++src;   break;
                  case 'b':   c = Ctrl_H;   ++src;   break;

                  // If "backslash" is true the backslash will be removed
                  // later.  Used to insert a literal CR.
                  default:   
                     if (flags & REGSUB_BACKSLASH) {
                        if (copy) {
                           if (dst + 1 > dest + destlen) {
                              internalErrMsg(S"eeRegsub_both(): not enough space");
                              return 0;
                           }
                           *dst = '\\';
                        }
                        ++dst;
                     }
                     c = *src++;
               }
            }

            // Write to buffer, if copy is set.
            if (func_one) {
               func_one(&cc, c);
               func_one = NULL;
            } ei (func_all)
               func_all(&cc, c);
            else // just copy
               cc = c;

            int totlen = utfCharLen(src - 1);
            int charlen = mb_char2len(cc);

            if (copy) {
               if (dst + charlen > dest + destlen) {
                  internalErrMsg(S"eeRegsub_both(): not enough space");
                  return 0;
               }
               mb_char2bytes(cc, dst);
            }
            dst += charlen - 1;
               int clen = utf_ptr2len(src - 1);

               // If the character length is shorter than "totlen", there
               // are composing characters; copy them as-is.
               if (clen < totlen) {
                  if (copy) {
                     if (dst + totlen - clen > dest + destlen) {
                        internalErrMsg(S"eeRegsub_both(): not enough space");
                        return 0;
                     }
                     MEMMOVE(dst + 1, src - 1 + clen, (Unt)(totlen - clen));
                  }
                  dst += totlen - clen;
               }
            src += totlen - 1;
            dst++;
         } else {
            if (REG_MULTI) {
               clnum = exe.multiMatch->startpos[no].lnum;
               if (clnum < 0 || exe.multiMatch->endpos[no].lnum < 0)
                   s = NULL;
               else {
                   s = reg_getline(clnum) + exe.multiMatch->startpos[no].col;
                   if (exe.multiMatch->endpos[no].lnum == clnum)
                  len = exe.multiMatch->endpos[no].col
                            - exe.multiMatch->startpos[no].col;
                   else
                  len = (int)reg_getline_len(clnum) - exe.multiMatch->startpos[no].col;
               }
            } else {
               s = exe.match->startp[no];
               if (!exe.match->endp[no])
                  s = NULL;
               else
                  len = (int)(exe.match->endp[no] - s);
            }
            if (s != NULL) {
               for (;;) {
                  if (len == 0) {
                     if (REG_MULTI) {
                        if (exe.multiMatch->endpos[no].lnum == clnum)
                           break;
                        if (copy) {
                           if (dst + 1 > dest + destlen) {
                              internalErrMsg(S"eeRegsub_both(): not enough space");
                              return 0;
                           }
                           *dst = ENTER;
                        }
                        ++dst;
                        s = reg_getline(++clnum);
                        if (exe.multiMatch->endpos[no].lnum == clnum)
                           len = exe.multiMatch->endpos[no].col;
                        else
                           len = (int)reg_getline_len(clnum);
                     } else
                         break;
                   }
                   ei (*s == ZERO) // we hit ZERO.
                   {
                  if (copy)
                      internalErrMsg(e_damaged_match_string);
                  goto exit;
                   } else {
                  if ((flags & REGSUB_BACKSLASH) && (*s == ENTER || *s == '\\')) {
                     //Insert a backslash in front of a CR, otherwise
                     //it will be replaced by a line break.
                     //Number of backslashes will be halved later, double them here.
                     if (copy) {
                        if (dst + 2 > dest + destlen) {
                           internalErrMsg(S"eeRegsub_both(): not enough space");
                           return 0;
                        }
                        dst[0] = '\\';
                        dst[1] = *s;
                      }
                      dst += 2;
                  } else {
                     c = mb_ptr2char(s);

                     if (func_one) {
                        func_one(&cc, c);
                        func_one = NULL;
                     } ei (func_all)
                        func_all(&cc, c);
                     else // just copy
                        cc = c;

                     //Copy composing characters separately, one at a time.
                     int l = utf_ptr2len(s) - 1;

                     s += l;
                     len -= l;
                     int charlen = mb_char2len(cc);
                     if (copy) {
                        if (dst + charlen > dest + destlen) {
                           internalErrMsg(S"eeRegsub_both(): not enough space");
                           return 0;
                        }
                        mb_char2bytes(cc, dst);
                     }
                     dst += charlen - 1;
                     dst++;
                  }

                  ++s;
                  --len;
                   }
               }
            }
            no = -1;
         }
      }
   } 
   if (copy)
      *dst = ZERO;

public exit:
   return (int)((dst - dest) + 1);
}


private Byte *
reg_getline_submatch(LineNr lnum) {
   Byte* line;
   reg_getline_common(lnum, RGLF_LINE | RGLF_SUBMATCH, OUT &line, NULL);

   return line;
}

private ColNr
reg_getline_submatch_len(LineNr lnum) {
    ColNr length;
    reg_getline_common(lnum, RGLF_LENGTH | RGLF_SUBMATCH, NULL, OUT &length);

    return length;
}

//Used for the submatch() function: get the string from the n'th submatch in
//allocated memory.
//Return NULL when not in a ":s" command and for a non-existing submatch.
public Byte *
reg_submatch(int no) {
   Byte   *retval = NULL;
   Byte   *s;
   int      len;
   int      round;
   LineNr   lnum;

   if (!can_f_submatch || no < 0)
      return NULL;

   if (!rsm.sm_match) {
      // First round: compute the length and allocate memory. Second round: copy the text.
      for (round = 1; round <= 2; ++round) {
         lnum = rsm.sm_mmatch->startpos[no].lnum;
         if (lnum < 0 || rsm.sm_mmatch->endpos[no].lnum < 0)
            return NULL;

         s = reg_getline_submatch(lnum);
         if (!s)  // anti-crash check, cannot happen?
            break;
         s += rsm.sm_mmatch->startpos[no].col;
         if (rsm.sm_mmatch->endpos[no].lnum == lnum) {
            // Within one line: take form start to end col.
            len = rsm.sm_mmatch->endpos[no].col - rsm.sm_mmatch->startpos[no].col;
            if (round == 2)
                copySubstrToAllocation(retval, (Text){s, len});
            ++len;
         } else {
            // Multiple lines: take start line from start col, middle
            // lines completely and end line up to end col.
            len = (int)reg_getline_submatch_len(lnum) - rsm.sm_mmatch->startpos[no].col;
            if (round == 2) {
                STRCPY(retval, s);
                retval[len] = '\n';
            }
            ++len;
            ++lnum;
            while (lnum < rsm.sm_mmatch->endpos[no].lnum) {
                s = reg_getline_submatch(lnum);
                if (round == 2)
               STRCPY(retval + len, s);
                len += (int)reg_getline_submatch_len(lnum);
                if (round == 2)
               retval[len] = '\n';
                ++len;
                ++lnum;
            }
            if (round == 2)
                STRNCPY(retval + len, reg_getline_submatch(lnum),
                          rsm.sm_mmatch->endpos[no].col);
            len += rsm.sm_mmatch->endpos[no].col;
            if (round == 2)
                retval[len] = ZERO;
            ++len;
          }

         if (!retval) {
            retval = alloc(len);
         }
      }
   } else {
      s = rsm.sm_match->startp[no];
      if (!s || rsm.sm_match->endp[no] == NULL)
         retval = NULL;
      else
         retval = copySubstr(s, rsm.sm_match->endp[no] - s);
   }

    return retval;
}

//Used for the submatch() function with the optional non-zero argument: get
//the list of strings from the n'th submatch in allocated memory with NULs represented in NLs.
//Returns a list of allocated strings.  Returns NULL when not in a ":s"
//command, for a non-existing submatch and for any error.
public List *
reg_submatch_list(int no) {
   Byte* s;
   LineNr slnum;
   LineNr elnum;
   ColNr scol;
   ColNr ecol;
   int i;
   int error = false;

   if (!can_f_submatch || no < 0)
      return NULL;

   List* list;
   if (!rsm.sm_match) {
      slnum = rsm.sm_mmatch->startpos[no].lnum;
      elnum = rsm.sm_mmatch->endpos[no].lnum;
      if (slnum < 0 || elnum < 0)
         return NULL;

      scol = rsm.sm_mmatch->startpos[no].col;
      ecol = rsm.sm_mmatch->endpos[no].col;

      list = list_alloc();

      s = reg_getline_submatch(slnum) + scol;
      if (slnum == elnum) {
          if (list_append_string(list, s, ecol - scol) == FAIL)
         error = true;
      } else {
          int max_lnum = elnum - slnum;

          if (list_append_string(list, s, -1) == FAIL)
         error = true;
          for (i = 1; i < max_lnum; i++) {
         s = reg_getline_submatch(slnum + i);
         if (list_append_string(list, s, -1) == FAIL)
             error = true;
          }
          s = reg_getline_submatch(elnum);
          if (list_append_string(list, s, ecol) == FAIL)
         error = true;
      }
   } else {
      s = rsm.sm_match->startp[no];
      if (s == NULL || rsm.sm_match->endp[no] == NULL)
         return NULL;
      list = list_alloc();
      if (list_append_string(list, s, (int)(rsm.sm_match->endp[no] - s)) == FAIL)
         error = true;
    }

   if (error) {
      list_free(list);
      return NULL;
    }
    ++list->refCount;
    return list;
}

//}}}
//{{{ NFA regular expression implementation.

//Logging of NFA engine.
//
//The NFA engine can write four log files:
//- Error log: Contains NFA engine's fatal errors.
//- Dump log: Contains compiled NFA state machine's information.
//- Run log: Contains information of matching procedure.
//- Debug log: Contains detailed information of matching procedure. Can be disabled by undefining 
//  REGEXP_DEBUG_LOG.
//The first one can also be used without debug mode.
//The last three are enabled when compiled as debug mode and individually disabled by commenting 
//them out.
//The log files can get quite big!
//To disable all of this when compiling Eegl for debugging, undefine DEBUG in this file
#ifdef DEBUG
# define REGEXP_ERROR_LOG   "parseBranchexp_error.log"
# define REGEXP_LOGGING
# define REGEXP_DUMP_LOG   "parseBranchexp_dump.log"
# define REGEXP_RUN_LOG   "parseBranchexp_run.log"
# define REGEXP_DEBUG_LOG   "parseBranchexp_debug.log"
#endif

// Added to ANY - NUPPER_IC to include a NL.
#define ADD_NL      31

//{{{ Regex tokens

public enum {
    SPLIT = 4294967295 - 1024,
    MATCH,
    EMPTY,             // matches 0-length
    START_COLL,          // [abc] start
    END_COLL,          // [abc] end
    START_NEG_COLL,          // [^abc] start
    END_NEG_COLL,          // [^abc] end (postfix only)
    RANGE,             // range of the two previous items (postfix only)
    RANGE_MIN,          // low end of a range
    RANGE_MAX,          // high end of a range
    CONCAT,             // concatenate two previous items (postfix only)
    OR,             // \| (postfix only)
    STAR,             // greedy * (postfix only)
    STAR_NONGREEDY,          // non-greedy * (postfix only)
    QUEST,             // greedy \? (postfix only)
    QUEST_NONGREEDY,       // non-greedy \? (postfix only)

    BOL,             // ^    Begin line
    EOL,             // $    End line
    BOW,             // \<   Begin word
    EOW,             // \>   End word
    BOF,             // \%^  Begin file
    EOFF,             // \%$  End file
    NEWL,
    ZSTART,             // Used for \zs
    ZEND,             // Used for \ze
    NOPEN,             // Start of subexpression marked with \%(
    NCLOSE,             // End of subexpr. marked with \%( ... \)
    START_INVISIBLE,
    START_INVISIBLE_FIRST,
    START_INVISIBLE_NEG,
    START_INVISIBLE_NEG_FIRST,
    START_INVISIBLE_BEFORE,
    START_INVISIBLE_BEFORE_FIRST,
    START_INVISIBLE_BEFORE_NEG,
    START_INVISIBLE_BEFORE_NEG_FIRST,
    START_PATTERN,
    END_INVISIBLE,
    END_INVISIBLE_NEG,
    END_PATTERN,
    COMPOSING,          // Next nodes in NFA are part of the composing multibyte char
    END_COMPOSING,          // End of a composing char in the NFA
    ANY_COMPOSING,          // \%C: Any composing characters.
    OPT_CHARS,          // \%[abc]

    // The following are used only in the postfix form, not in the NFA
    PREV_ATOM_NO_WIDTH,       // Used for \@=
    PREV_ATOM_NO_WIDTH_NEG,       // Used for \@!
    PREV_ATOM_JUST_BEFORE,       // Used for \@<=
    PREV_ATOM_JUST_BEFORE_NEG,  // Used for \@<!
    PREV_ATOM_LIKE_PATTERN,       // Used for \@>

    BACKREF1,          // \1
    BACKREF2,          // \2
    BACKREF3,          // \3
    BACKREF4,          // \4
    BACKREF5,          // \5
    BACKREF6,          // \6
    BACKREF7,          // \7
    BACKREF8,          // \8
    BACKREF9,          // \9
    ZREF1,             // \z1
    ZREF2,             // \z2
    ZREF3,             // \z3
    ZREF4,             // \z4
    ZREF5,             // \z5
    ZREF6,             // \z6
    ZREF7,             // \z7
    ZREF8,             // \z8
    ZREF9,             // \z9
    SKIP,             // Skip characters

    MOPEN,
    MOPEN1,
    MOPEN2,
    MOPEN3,
    MOPEN4,
    MOPEN5,
    MOPEN6,
    MOPEN7,
    MOPEN8,
    MOPEN9,

    MCLOSE,
    MCLOSE1,
    MCLOSE2,
    MCLOSE3,
    MCLOSE4,
    MCLOSE5,
    MCLOSE6,
    MCLOSE7,
    MCLOSE8,
    MCLOSE9,

    ZOPEN,
    ZOPEN1,
    ZOPEN2,
    ZOPEN3,
    ZOPEN4,
    ZOPEN5,
    ZOPEN6,
    ZOPEN7,
    ZOPEN8,
    ZOPEN9,

    ZCLOSE,
    ZCLOSE1,
    ZCLOSE2,
    ZCLOSE3,
    ZCLOSE4,
    ZCLOSE5,
    ZCLOSE6,
    ZCLOSE7,
    ZCLOSE8,
    ZCLOSE9,

    // FIRST_NL
    ANY,      //   Match any one character.
    IDENT,    //   Match identifier char
    SIDENT,   //   Match identifier char but no digit
    KWORD,    //   Match keyword char
    SKWORD,   //   Match word char but no digit
    FNAME,    //   Match file name char
    SFNAME,   //   Match file name char but no digit
    PRINT,    //   Match printable char
    SPRINT,   //   Match printable char but no digit
    WHITE,    //   Match whitespace char
    NWHITE,   //   Match non-whitespace char
    DIGIT,      //   Match digit char
    NDIGIT,      //   Match non-digit char
    HEX,      //   Match hex char
    NHEX,      //   Match non-hex char
    WORD,      //   Match word char
    NWORD,      //   Match non-word char
    HEAD,      //   Match head char
    NHEAD,      //   Match non-head char
    ALPHA,      //   Match alpha char
    NALPHA,      //   Match non-alpha char
    LOWER,      //   Match lowercase char
    NLOWER,      //   Match non-lowercase char
    UPPER,      //   Match uppercase char
    NUPPER,      //   Match non-uppercase char
    LOWER_IC,   //   Match [a-z]
    NLOWER_IC,   //   Match [^a-z]
    UPPER_IC,   //   Match [A-Z]
    NUPPER_IC,   //   Match [^A-Z]

    FIRST_NL = ANY + ADD_NL,
    LAST_NL = NUPPER_IC + ADD_NL,

    CURSOR,      //   Match cursor pos
    LNUM,      //   Match line number
    LNUM_GT,   //   Match > line number
    LNUM_LT,   //   Match < line number
    COL,      //   Match cursor column
    COL_GT,      //   Match > cursor column
    COL_LT,      //   Match < cursor column
    VCOL,      //   Match cursor virtual column
    VCOL_GT,   //   Match > cursor virtual column
    VCOL_LT,   //   Match < cursor virtual column
    MARK,      //   Match mark
    MARK_GT,   //   Match > mark
    MARK_LT,   //   Match < mark
    VISUAL,      //   Match Visual area

    // Character classes [:alnum:] etc
    CLASS_ALNUM,
    CLASS_ALPHA,
    CLASS_BLANK,
    CLASS_CNTRL,
    CLASS_DIGIT,
    CLASS_GRAPH,
    CLASS_LOWER,
    CLASS_PRINT,
    CLASS_PUNCT,
    CLASS_SPACE,
    CLASS_UPPER,
    CLASS_XDIGIT,
    CLASS_TAB,
    CLASS_RETURN,
    CLASS_BACKSPACE,
    CLASS_ESCAPE,
    CLASS_IDENT,
    CLASS_KEYWORD,
    CLASS_FNAME
};

//}}}


// When making changes to classCharsS also change classCodes.
private Byte* classCharsS = (CS)
   ".iIkK"
   "fFpP"
   "sSdD"
   "xXwWhH"
   "aAlL"
   "uU";
// Keep in sync with classCharsS.
private int classCodes[] = {
    ANY, IDENT, SIDENT, KWORD, SKWORD,
    FNAME, SFNAME, PRINT, SPRINT,
    WHITE, NWHITE, DIGIT, NDIGIT,
    HEX, NHEX, WORD, NWORD, HEAD, NHEAD,
    ALPHA, NALPHA, LOWER, NLOWER,
    UPPER, NUPPER
};

// Variables only used in compile() and descendants.
private Unt *postfixStartS;  // holds the postfix form of r.e.
private Unt *postfixEndS;
private Unt *postfixS;

private int countStatesS;   // Number of states in the NFA.
private int stateS;   // Index in the state vector, used in alloc_state()

// struct to save start/end pointer/position in for \(\)
private typedef struct{
   union {
      Byte   *ptr;
      PosNoVirt   pos;
   } se_u;
} StartEnd;

// If not NULL, match must end at this position
private StartEnd *mustEndAtS = NULL;

// 0 for first call to match(), 1 for recursive call.
private int nfa_ll_index = 0;

private int reallocPostfix(void);
private int parse(Unt paren, OUT Boole* hadEol);
#ifdef DEBUG
private void printStateWorker(FILE *debugf, RState *state, ArrayList *indent);
#endif
private int match_follows(RState *startstate, int depth);
private int failure_chance(RState *state, int depth);

// helper functions used when doing parsing
#define EMIT(c) if (postfixS >= postfixEndS && reallocPostfix() == FAIL) \
            return FAIL;      \
         *postfixS++ = c; \
      
      
// Setup to parse the regexp.  Used once to get the length and once to do it.
private void
regcomp_start(Byte* expr, Unt flags) {      // see eeRegcomp()
   initchr(expr);
   if (flags & RE_MAGIC)
      reg_magic = MAGIC_ON;
   else
      reg_magic = MAGIC_OFF;
   reg_string = (flags & RE_STRING);
   reg_strict = (flags & RE_STRICT);

   numComplexBracesS = 0;
   regnpar = 1;
   CLEAR_FIELD(hadEndbraceS);
   currZParensS = 1;
   re_has_z = 0;
   regflags = 0;
}

// Initialize internal variables before NFA compilation. Return OK on success, FAIL otherwise
private int
compile_start(CS expr, Unt flags) {      // see compileRegexp()
   Unt postfix_size;
   int nstate_max;

   countStatesS = 0;
   stateS = 0;
   // A reasonable estimation for maximum size
   nstate_max = (int)(STRLEN(expr) + 1) * 25;

   // Some items blow up in size, such as [A-z].  Add more space for that.
   // When it is still not enough reallocPostfix() will be used.
   nstate_max += 1000;

   // Size for postfix representation of expr.
   postfix_size = sizeof(int) * nstate_max;

   postfixStartS = alloc(postfix_size);
   postfixS = postfixStartS;
   postfixEndS = postfixStartS + nstate_max;
   exe.nfa_has_zend = false;
   exe.nfa_has_backref = false;

   regcomp_start(expr, flags);

   return OK;
}

// Figure out if the NFA state list starts with an anchor, must match at start of the line.
private int
getAnchor(RState *start, int depth) {
   RState *p = start;

   if (depth > 4)
      return 0;

   while (p != NULL) {
      switch (p->c) {
         case BOL:
         case BOF:
            return 1; // yes!

         case ZSTART:
         case ZEND:
         case CURSOR:
         case VISUAL:

         case MOPEN:
         case MOPEN1:
         case MOPEN2:
         case MOPEN3:
         case MOPEN4:
         case MOPEN5:
         case MOPEN6:
         case MOPEN7:
         case MOPEN8:
         case MOPEN9:
         case NOPEN:
         case ZOPEN:
         case ZOPEN1:
         case ZOPEN2:
         case ZOPEN3:
         case ZOPEN4:
         case ZOPEN5:
         case ZOPEN6:
         case ZOPEN7:
         case ZOPEN8:
         case ZOPEN9:
            p = p->out;
            break;

         case SPLIT:
            return getAnchor(p->out, depth + 1) && getAnchor(p->out1, depth + 1);

         default:
            return 0; // noooo
      }
   }
   return 0;
}

// Figure out if the NFA state list starts with a character which must match at start of the match
private int
getRegStart(RState* start, int depth) {
   RState* p = start;

   if (depth > 4)
      return 0;

   while (p != NULL) {
      switch (p->c) {
      // all kinds of zero-width matches
      case BOL:
      case BOF:
      case BOW:
      case EOW:
      case ZSTART:
      case ZEND:
      case CURSOR:
      case VISUAL:
      case LNUM:
      case LNUM_GT:
      case LNUM_LT:
      case COL:
      case COL_GT:
      case COL_LT:
      case VCOL:
      case VCOL_GT:
      case VCOL_LT:
      case MARK:
      case MARK_GT:
      case MARK_LT:
      case MOPEN:
      case MOPEN1:
      case MOPEN2:
      case MOPEN3:
      case MOPEN4:
      case MOPEN5:
      case MOPEN6:
      case MOPEN7:
      case MOPEN8:
      case MOPEN9:
      case NOPEN:
      case ZOPEN:
      case ZOPEN1:
      case ZOPEN2:
      case ZOPEN3:
      case ZOPEN4:
      case ZOPEN5:
      case ZOPEN6:
      case ZOPEN7:
      case ZOPEN8:
      case ZOPEN9:
         p = p->out;
         break;

      case SPLIT: {
         int c1 = getRegStart(p->out, depth + 1);
         int c2 = getRegStart(p->out1, depth + 1);

         if (c1 == c2)
            return c1; // yes!
         return 0;
      }

      default:
         if (p->c > 0)
            return p->c; // yes!
         return 0;
      }
   }
   return 0;
}

//Figure out if the NFA state list contains just literal text and nothing
//else.  If so return a string in allocated memory with what must match after
//regstart.  Otherwise return NULL.
private Byte *
getMatchText(RState *start) {
   RState *p = start;
   int len = 0;
   Byte* ret;
   Byte* s;

   if (p->c != MOPEN)
      return NULL; // just in case
   p = p->out;
   while (p->c < UNT_NEG) {
      len += MB_CHAR2LEN(p->c);
      p = p->out;
   }
   if (p->c != MCLOSE || p->out->c != MATCH)
      return NULL;

   ret = alloc(len);

   p = start->out->out; // skip first char, it goes into regstart
   s = ret;
   while (p->c < UNT_NEG) {
      s += mb_char2bytes(p->c, s);
      p = p->out;
   }
   *s = ZERO;
   return ret;
}

// Allocate more space for postfixStartS.  Called when running above the estimated number of states.
private int
reallocPostfix(void) {
   int nstate_max = (int)(postfixEndS - postfixStartS);
   int new_max;

   //For weird patterns the number of states can be very high. Increasing by
   //50% seems a reasonable compromise between memory use and speed.
   new_max = nstate_max * 3 / 2 + 1;
   Unt* new_start = ALLOC_MULT(Unt, new_max);
   MEMMOVE(new_start, postfixStartS, nstate_max * sizeof(int));
   Unt* old_start = postfixStartS;
   postfixStartS = new_start;
   postfixS = new_start + (Unt)(postfixS - old_start);
   postfixEndS = postfixStartS + new_max;
   eeglFree(old_start);
   return OK;
}

//Search between "start" and "end" and try to recognize a
//character class in expanded form. For example [0-9].
//On success, return the id the character class to be emitted.
//On failure, return 0 (=FAIL)
//Start points to the first char of the range, while end should point to the closing brace.
//Keep in mind that 'ignorecase' applies at execution time, thus [a-z] may
//need to be interpreted as [a-zA-Z].
private Unt
recognizeCharClass(Byte *start, Byte *end, int extra_newl) {
#   define CLASS_not      0x80
#   define CLASS_af      0x40
#   define CLASS_AF      0x20
#   define CLASS_az      0x10
#   define CLASS_AZ      0x08
#   define CLASS_o7      0x04
#   define CLASS_o9      0x02
#   define CLASS_underscore   0x01

   int newl = extra_newl == true;

   if (*end != ']')
      return FAIL;
   Byte* p = start;
   int config = 0;
   if (*p == '^') {
      config |= CLASS_not;
      p++;
   }

   while (p < end) {
      if (p + 2 < end && *(p + 1) == '-') {
         switch (*p) {
         case '0':
            if (*(p + 2) == '9') {
               config |= CLASS_o9;
               break;
            }
            if (*(p + 2) == '7') {
               config |= CLASS_o7;
               break;
            }
            return FAIL;

         case 'a':
            if (*(p + 2) == 'z') {
               config |= CLASS_az;
               break;
            }
            if (*(p + 2) == 'f') {
               config |= CLASS_af;
               break;
            }
            return FAIL;

         case 'A':
            if (*(p + 2) == 'Z') {
               config |= CLASS_AZ;
               break;
            }
            if (*(p + 2) == 'F') {
               config |= CLASS_AF;
               break;
            }
            return FAIL;

         default:
            return FAIL;
         }
         p += 3;
      } ei (p + 1 < end && *p == '\\' && *(p + 1) == 'n') {
         newl = true;
         p += 2;
      } ei (*p == '_') {
         config |= CLASS_underscore;
         p ++;
      } ei (*p == '\n') {
         newl = true;
         p ++;
      } else
         return FAIL;
   } // while (p < end)

   if (p != end)
      return FAIL;

   if (newl == true)
      extra_newl = ADD_NL;

   switch (config) {
   case CLASS_o9:
      return extra_newl + DIGIT;
   case CLASS_not |  CLASS_o9:
      return extra_newl + NDIGIT;
   case CLASS_af | CLASS_AF | CLASS_o9:
      return extra_newl + HEX;
   case CLASS_not | CLASS_af | CLASS_AF | CLASS_o9:
      return extra_newl + NHEX;
   case CLASS_az | CLASS_AZ | CLASS_o9 | CLASS_underscore:
      return extra_newl + WORD;
   case CLASS_not | CLASS_az | CLASS_AZ | CLASS_o9 | CLASS_underscore:
      return extra_newl + NWORD;
   case CLASS_az | CLASS_AZ | CLASS_underscore:
      return extra_newl + HEAD;
   case CLASS_not | CLASS_az | CLASS_AZ | CLASS_underscore:
      return extra_newl + NHEAD;
   case CLASS_az | CLASS_AZ:
      return extra_newl + ALPHA;
   case CLASS_not | CLASS_az | CLASS_AZ:
      return extra_newl + NALPHA;
   case CLASS_az:
      return extra_newl + LOWER_IC;
   case CLASS_not | CLASS_az:
      return extra_newl + NLOWER_IC;
   case CLASS_AZ:
      return extra_newl + UPPER_IC;
   case CLASS_not | CLASS_AZ:
      return extra_newl + NUPPER_IC;
   }
   return FAIL;
}

//Produce the bytes for equivalence class "c".
//Currently only handles latin1, latin9 and utf-8.
//Emits bytes in postfix notation: 'a,b,OR,c,OR' is equivalent to 'a OR b OR c'
//
//NOTE! When changing this function, also update reg_equi_class()
private int
nfa_emit_equi_class(int c) {
#define EMIT2(c)    EMIT(c); EMIT(CONCAT);

#define A_grave 0xc0
#define A_acute 0xc1
#define A_circumflex 0xc2
#define A_virguilla 0xc3
#define A_diaeresis 0xc4
#define A_ring 0xc5
#define C_cedilla 0xc7
#define E_grave 0xc8
#define E_acute 0xc9
#define E_circumflex 0xca
#define E_diaeresis 0xcb
#define I_grave 0xcc
#define I_acute 0xcd
#define I_circumflex 0xce
#define I_diaeresis 0xcf
#define N_virguilla 0xd1
#define O_grave 0xd2
#define O_acute 0xd3
#define O_circumflex 0xd4
#define O_virguilla 0xd5
#define O_diaeresis 0xd6
#define O_slash 0xd8
#define U_grave 0xd9
#define U_acute 0xda
#define U_circumflex 0xdb
#define U_diaeresis 0xdc
#define Y_acute 0xdd
#define a_grave 0xe0
#define a_acute 0xe1
#define a_circumflex 0xe2
#define a_virguilla 0xe3
#define a_diaeresis 0xe4
#define a_ring 0xe5
#define c_cedilla 0xe7
#define e_grave 0xe8
#define e_acute 0xe9
#define e_circumflex 0xea
#define e_diaeresis 0xeb
#define i_grave 0xec
#define i_acute 0xed
#define i_circumflex 0xee
#define i_diaeresis 0xef
#define n_virguilla 0xf1
#define o_grave 0xf2
#define o_acute 0xf3
#define o_circumflex 0xf4
#define o_virguilla 0xf5
#define o_diaeresis 0xf6
#define o_slash 0xf8
#define u_grave 0xf9
#define u_acute 0xfa
#define u_circumflex 0xfb
#define u_diaeresis 0xfc
#define y_acute 0xfd
#define y_diaeresis 0xff
   switch (c) {
   case 'A': case A_grave: case A_acute: case A_circumflex:
   case A_virguilla: case A_diaeresis: case A_ring:
   case 0x100: case 0x102: case 0x104: case 0x1cd:
   case 0x1de: case 0x1e0: case 0x1fa: case 0x200:
   case 0x202: case 0x226: case 0x23a: case 0x1e00:
   case 0x1ea0: case 0x1ea2: case 0x1ea4: case 0x1ea6:
   case 0x1ea8: case 0x1eaa: case 0x1eac: case 0x1eae:
   case 0x1eb0: case 0x1eb2: case 0x1eb4: case 0x1eb6:
      EMIT2('A') EMIT2(A_grave) EMIT2(A_acute)
      EMIT2(A_circumflex) EMIT2(A_virguilla)
      EMIT2(A_diaeresis) EMIT2(A_ring)
      EMIT2(0x100) EMIT2(0x102) EMIT2(0x104)
      EMIT2(0x1cd) EMIT2(0x1de) EMIT2(0x1e0)
      EMIT2(0x1fa) EMIT2(0x200) EMIT2(0x202)
      EMIT2(0x226) EMIT2(0x23a) EMIT2(0x1e00)
      EMIT2(0x1ea0) EMIT2(0x1ea2) EMIT2(0x1ea4)
      EMIT2(0x1ea6) EMIT2(0x1ea8) EMIT2(0x1eaa)
      EMIT2(0x1eac) EMIT2(0x1eae) EMIT2(0x1eb0)
      EMIT2(0x1eb2) EMIT2(0x1eb6) EMIT2(0x1eb4)
      return OK;

   case 'B': case 0x181: case 0x243: case 0x1e02:
   case 0x1e04: case 0x1e06:
      EMIT2('B')
      EMIT2(0x181) EMIT2(0x243) EMIT2(0x1e02)
      EMIT2(0x1e04) EMIT2(0x1e06)
      return OK;

   case 'C': case C_cedilla: case 0x106: case 0x108:
   case 0x10a: case 0x10c: case 0x187: case 0x23b:
   case 0x1e08: case 0xa792:
      EMIT2('C') EMIT2(C_cedilla)
      EMIT2(0x106) EMIT2(0x108) EMIT2(0x10a)
      EMIT2(0x10c) EMIT2(0x187) EMIT2(0x23b)
      EMIT2(0x1e08) EMIT2(0xa792)
      return OK;

   case 'D': case 0x10e: case 0x110: case 0x18a:
   case 0x1e0a: case 0x1e0c: case 0x1e0e: case 0x1e10:
   case 0x1e12:
      EMIT2('D') EMIT2(0x10e) EMIT2(0x110) EMIT2(0x18a)
      EMIT2(0x1e0a) EMIT2(0x1e0c) EMIT2(0x1e0e)
      EMIT2(0x1e10) EMIT2(0x1e12)
      return OK;

   case 'E': case E_grave: case E_acute: case E_circumflex:
   case E_diaeresis: case 0x112: case 0x114: case 0x116:
   case 0x118: case 0x11a: case 0x204: case 0x206:
   case 0x228: case 0x246: case 0x1e14: case 0x1e16:
   case 0x1e18: case 0x1e1a: case 0x1e1c: case 0x1eb8:
   case 0x1eba: case 0x1ebc: case 0x1ebe: case 0x1ec0:
   case 0x1ec2: case 0x1ec4: case 0x1ec6:
      EMIT2('E') EMIT2(E_grave) EMIT2(E_acute)
      EMIT2(E_circumflex) EMIT2(E_diaeresis)
      EMIT2(0x112) EMIT2(0x114) EMIT2(0x116)
      EMIT2(0x118) EMIT2(0x11a) EMIT2(0x204)
      EMIT2(0x206) EMIT2(0x228) EMIT2(0x246)
      EMIT2(0x1e14) EMIT2(0x1e16) EMIT2(0x1e18)
      EMIT2(0x1e1a) EMIT2(0x1e1c) EMIT2(0x1eb8)
      EMIT2(0x1eba) EMIT2(0x1ebc) EMIT2(0x1ebe)
      EMIT2(0x1ec0) EMIT2(0x1ec2) EMIT2(0x1ec4)
      EMIT2(0x1ec6)
      return OK;

   case 'F': case 0x191: case 0x1e1e: case 0xa798:
      EMIT2('F') EMIT2(0x191) EMIT2(0x1e1e) EMIT2(0xa798)
      return OK;

   case 'G': case 0x11c: case 0x11e: case 0x120:
   case 0x122: case 0x193: case 0x1e4: case 0x1e6:
   case 0x1f4: case 0x1e20: case 0xa7a0:
      EMIT2('G') EMIT2(0x11c) EMIT2(0x11e) EMIT2(0x120)
      EMIT2(0x122) EMIT2(0x193) EMIT2(0x1e4)
      EMIT2(0x1e6) EMIT2(0x1f4) EMIT2(0x1e20)
      EMIT2(0xa7a0)
      return OK;

   case 'H': case 0x124: case 0x126: case 0x21e:
   case 0x1e22: case 0x1e24: case 0x1e26: case 0x1e28:
   case 0x1e2a: case 0x2c67:
      EMIT2('H') EMIT2(0x124) EMIT2(0x126) EMIT2(0x21e)
      EMIT2(0x1e22) EMIT2(0x1e24) EMIT2(0x1e26)
      EMIT2(0x1e28) EMIT2(0x1e2a) EMIT2(0x2c67)
      return OK;

   case 'I': case I_grave: case I_acute: case I_circumflex:
   case I_diaeresis: case 0x128: case 0x12a: case 0x12c:
   case 0x12e: case 0x130: case 0x197: case 0x1cf:
   case 0x208: case 0x20a: case 0x1e2c: case 0x1e2e:
   case 0x1ec8: case 0x1eca:
      EMIT2('I') EMIT2(I_grave) EMIT2(I_acute)
      EMIT2(I_circumflex) EMIT2(I_diaeresis)
      EMIT2(0x128) EMIT2(0x12a) EMIT2(0x12c)
      EMIT2(0x12e) EMIT2(0x130) EMIT2(0x197)
      EMIT2(0x1cf) EMIT2(0x208) EMIT2(0x20a)
      EMIT2(0x1e2c) EMIT2(0x1e2e) EMIT2(0x1ec8)
      EMIT2(0x1eca)
      return OK;

   case 'J': case 0x134: case 0x248:
      EMIT2('J') EMIT2(0x134) EMIT2(0x248)
      return OK;

   case 'K': case 0x136: case 0x198: case 0x1e8: case 0x1e30:
   case 0x1e32: case 0x1e34: case 0x2c69: case 0xa740:
       EMIT2('K') EMIT2(0x136) EMIT2(0x198) EMIT2(0x1e8)
       EMIT2(0x1e30) EMIT2(0x1e32) EMIT2(0x1e34)
       EMIT2(0x2c69) EMIT2(0xa740)
       return OK;

   case 'L': case 0x139: case 0x13b: case 0x13d:
   case 0x13f: case 0x141: case 0x23d: case 0x1e36:
   case 0x1e38: case 0x1e3a: case 0x1e3c: case 0x2c60:
      EMIT2('L') EMIT2(0x139) EMIT2(0x13b)
      EMIT2(0x13d) EMIT2(0x13f) EMIT2(0x141)
      EMIT2(0x23d) EMIT2(0x1e36) EMIT2(0x1e38)
      EMIT2(0x1e3a) EMIT2(0x1e3c) EMIT2(0x2c60)
      return OK;

   case 'M': case 0x1e3e: case 0x1e40: case 0x1e42:
      EMIT2('M') EMIT2(0x1e3e) EMIT2(0x1e40)
      EMIT2(0x1e42)
      return OK;

   case 'N': case N_virguilla:
   case 0x143: case 0x145: case 0x147: case 0x1f8:
   case 0x1e44: case 0x1e46: case 0x1e48: case 0x1e4a:
   case 0xa7a4:
      EMIT2('N') EMIT2(N_virguilla)
      EMIT2(0x143) EMIT2(0x145) EMIT2(0x147)
      EMIT2(0x1f8) EMIT2(0x1e44) EMIT2(0x1e46)
      EMIT2(0x1e48) EMIT2(0x1e4a) EMIT2(0xa7a4)
      return OK;

   case 'O': case O_grave: case O_acute: case O_circumflex:
   case O_virguilla: case O_diaeresis: case O_slash:
   case 0x14c: case 0x14e: case 0x150: case 0x19f:
   case 0x1a0: case 0x1d1: case 0x1ea: case 0x1ec:
   case 0x1fe: case 0x20c: case 0x20e: case 0x22a:
   case 0x22c: case 0x22e: case 0x230: case 0x1e4c:
   case 0x1e4e: case 0x1e50: case 0x1e52: case 0x1ecc:
   case 0x1ece: case 0x1ed0: case 0x1ed2: case 0x1ed4:
   case 0x1ed6: case 0x1ed8: case 0x1eda: case 0x1edc:
   case 0x1ede: case 0x1ee0: case 0x1ee2:
      EMIT2('O') EMIT2(O_grave) EMIT2(O_acute)
      EMIT2(O_circumflex) EMIT2(O_virguilla)
      EMIT2(O_diaeresis) EMIT2(O_slash)
      EMIT2(0x14c) EMIT2(0x14e) EMIT2(0x150)
      EMIT2(0x19f) EMIT2(0x1a0) EMIT2(0x1d1)
      EMIT2(0x1ea) EMIT2(0x1ec) EMIT2(0x1fe)
      EMIT2(0x20c) EMIT2(0x20e) EMIT2(0x22a)
      EMIT2(0x22c) EMIT2(0x22e) EMIT2(0x230)
      EMIT2(0x1e4c) EMIT2(0x1e4e) EMIT2(0x1e50)
      EMIT2(0x1e52) EMIT2(0x1ecc) EMIT2(0x1ece)
      EMIT2(0x1ed0) EMIT2(0x1ed2) EMIT2(0x1ed4)
      EMIT2(0x1ed6) EMIT2(0x1ed8) EMIT2(0x1eda)
      EMIT2(0x1edc) EMIT2(0x1ede) EMIT2(0x1ee0)
      EMIT2(0x1ee2)
      return OK;

   case 'P': case 0x1a4: case 0x1e54: case 0x1e56: case 0x2c63:
      EMIT2('P') EMIT2(0x1a4) EMIT2(0x1e54) EMIT2(0x1e56)
      EMIT2(0x2c63)
      return OK;

   case 'Q': case 0x24a:
      EMIT2('Q') EMIT2(0x24a)
      return OK;

   case 'R': case 0x154: case 0x156: case 0x158: case 0x210:
   case 0x212: case 0x24c: case 0x1e58: case 0x1e5a:
   case 0x1e5c: case 0x1e5e: case 0x2c64: case 0xa7a6:
      EMIT2('R') EMIT2(0x154) EMIT2(0x156) EMIT2(0x158)
      EMIT2(0x210) EMIT2(0x212) EMIT2(0x24c) EMIT2(0x1e58)
      EMIT2(0x1e5a) EMIT2(0x1e5c) EMIT2(0x1e5e) EMIT2(0x2c64)
      EMIT2(0xa7a6)
      return OK;

   case 'S': case 0x15a: case 0x15c: case 0x15e: case 0x160:
   case 0x218: case 0x1e60: case 0x1e62: case 0x1e64:
   case 0x1e66: case 0x1e68: case 0x2c7e: case 0xa7a8:
      EMIT2('S') EMIT2(0x15a) EMIT2(0x15c) EMIT2(0x15e)
      EMIT2(0x160) EMIT2(0x218) EMIT2(0x1e60) EMIT2(0x1e62)
      EMIT2(0x1e64) EMIT2(0x1e66) EMIT2(0x1e68) EMIT2(0x2c7e)
      EMIT2(0xa7a8)
      return OK;

   case 'T': case 0x162: case 0x164: case 0x166: case 0x1ac:
   case 0x1ae: case 0x21a: case 0x23e: case 0x1e6a: case 0x1e6c:
   case 0x1e6e: case 0x1e70:
      EMIT2('T') EMIT2(0x162) EMIT2(0x164) EMIT2(0x166)
      EMIT2(0x1ac) EMIT2(0x1ae) EMIT2(0x23e) EMIT2(0x21a)
      EMIT2(0x1e6a) EMIT2(0x1e6c) EMIT2(0x1e6e) EMIT2(0x1e70)
      return OK;

   case 'U': case U_grave: case U_acute: case U_diaeresis:
   case U_circumflex: case 0x168: case 0x16a: case 0x16c:
   case 0x16e: case 0x170: case 0x172: case 0x1af:
   case 0x1d3: case 0x1d5: case 0x1d7: case 0x1d9:
   case 0x1db: case 0x214: case 0x216: case 0x244:
   case 0x1e72: case 0x1e74: case 0x1e76: case 0x1e78:
   case 0x1e7a: case 0x1ee4: case 0x1ee6: case 0x1ee8:
   case 0x1eea: case 0x1eec: case 0x1eee: case 0x1ef0:
      EMIT2('U') EMIT2(U_grave) EMIT2(U_acute)
      EMIT2(U_diaeresis) EMIT2(U_circumflex)
      EMIT2(0x168) EMIT2(0x16a)
      EMIT2(0x16c) EMIT2(0x16e) EMIT2(0x170)
      EMIT2(0x172) EMIT2(0x1af) EMIT2(0x1d3)
      EMIT2(0x1d5) EMIT2(0x1d7) EMIT2(0x1d9)
      EMIT2(0x1db) EMIT2(0x214) EMIT2(0x216)
      EMIT2(0x244) EMIT2(0x1e72) EMIT2(0x1e74)
      EMIT2(0x1e76) EMIT2(0x1e78) EMIT2(0x1e7a)
      EMIT2(0x1ee4) EMIT2(0x1ee6) EMIT2(0x1ee8)
      EMIT2(0x1eea) EMIT2(0x1eec) EMIT2(0x1eee)
      EMIT2(0x1ef0)
      return OK;

   case 'V': case 0x1b2: case 0x1e7c: case 0x1e7e:
      EMIT2('V') EMIT2(0x1b2) EMIT2(0x1e7c) EMIT2(0x1e7e)
      return OK;

   case 'W': case 0x174: case 0x1e80: case 0x1e82: case 0x1e84:
   case 0x1e86: case 0x1e88:
      EMIT2('W') EMIT2(0x174) EMIT2(0x1e80) EMIT2(0x1e82)
      EMIT2(0x1e84) EMIT2(0x1e86) EMIT2(0x1e88)
      return OK;

   case 'X': case 0x1e8a: case 0x1e8c:
      EMIT2('X') EMIT2(0x1e8a) EMIT2(0x1e8c)
      return OK;

   case 'Y': case Y_acute: case 0x176: case 0x178:
   case 0x1b3: case 0x232: case 0x24e: case 0x1e8e:
   case 0x1ef2: case 0x1ef4: case 0x1ef6: case 0x1ef8:
      EMIT2('Y') EMIT2(Y_acute)
      EMIT2(0x176) EMIT2(0x178) EMIT2(0x1b3)
      EMIT2(0x232) EMIT2(0x24e) EMIT2(0x1e8e)
      EMIT2(0x1ef2) EMIT2(0x1ef4) EMIT2(0x1ef6)
      EMIT2(0x1ef8)
      return OK;

   case 'Z': case 0x179: case 0x17b: case 0x17d:
   case 0x1b5: case 0x1e90: case 0x1e92: case 0x1e94:
   case 0x2c6b:
      EMIT2('Z') EMIT2(0x179) EMIT2(0x17b) EMIT2(0x17d)
      EMIT2(0x1b5) EMIT2(0x1e90) EMIT2(0x1e92)
      EMIT2(0x1e94) EMIT2(0x2c6b)
      return OK;

   case 'a': case a_grave: case a_acute: case a_circumflex:
   case a_virguilla: case a_diaeresis: case a_ring:
   case 0x101: case 0x103: case 0x105: case 0x1ce:
   case 0x1df: case 0x1e1: case 0x1fb: case 0x201:
   case 0x203: case 0x227: case 0x1d8f: case 0x1e01:
   case 0x1e9a: case 0x1ea1: case 0x1ea3: case 0x1ea5:
   case 0x1ea7: case 0x1ea9: case 0x1eab: case 0x1ead:
   case 0x1eaf: case 0x1eb1: case 0x1eb3: case 0x1eb5:
   case 0x1eb7: case 0x2c65:
      EMIT2('a') EMIT2(a_grave) EMIT2(a_acute)
      EMIT2(a_circumflex) EMIT2(a_virguilla)
      EMIT2(a_diaeresis) EMIT2(a_ring)
      EMIT2(0x101) EMIT2(0x103) EMIT2(0x105)
      EMIT2(0x1ce) EMIT2(0x1df) EMIT2(0x1e1)
      EMIT2(0x1fb) EMIT2(0x201) EMIT2(0x203)
      EMIT2(0x227) EMIT2(0x1d8f) EMIT2(0x1e01)
      EMIT2(0x1e9a) EMIT2(0x1ea1) EMIT2(0x1ea3)
      EMIT2(0x1ea5) EMIT2(0x1ea7) EMIT2(0x1ea9)
      EMIT2(0x1eab) EMIT2(0x1ead) EMIT2(0x1eaf)
      EMIT2(0x1eb1) EMIT2(0x1eb3) EMIT2(0x1eb5)
      EMIT2(0x1eb7) EMIT2(0x2c65)
      return OK;

   case 'b': case 0x180: case 0x253: case 0x1d6c: case 0x1d80:
   case 0x1e03: case 0x1e05: case 0x1e07:
      EMIT2('b') EMIT2(0x180) EMIT2(0x253) EMIT2(0x1d6c)
      EMIT2(0x1d80) EMIT2(0x1e03) EMIT2(0x1e05) EMIT2(0x1e07)
      return OK;

   case 'c': case c_cedilla: case 0x107: case 0x109: case 0x10b:
   case 0x10d: case 0x188: case 0x23c: case 0x1e09: case 0xa793:
   case 0xa794:
      EMIT2('c') EMIT2(c_cedilla)
      EMIT2(0x107) EMIT2(0x109) EMIT2(0x10b)
      EMIT2(0x10d) EMIT2(0x188) EMIT2(0x23c)
      EMIT2(0x1e09) EMIT2(0xa793) EMIT2(0xa794)
      return OK;

   case 'd': case 0x10f: case 0x111: case 0x257: case 0x1d6d:
   case 0x1d81: case 0x1d91: case 0x1e0b: case 0x1e0d: case 0x1e0f:
   case 0x1e11: case 0x1e13:
      EMIT2('d') EMIT2(0x10f) EMIT2(0x111)
      EMIT2(0x257) EMIT2(0x1d6d) EMIT2(0x1d81)
      EMIT2(0x1d91) EMIT2(0x1e0b) EMIT2(0x1e0d)
      EMIT2(0x1e0f) EMIT2(0x1e11) EMIT2(0x1e13)
      return OK;

   case 'e': case e_grave: case e_acute: case e_circumflex:
   case e_diaeresis: case 0x113: case 0x115: case 0x117:
   case 0x119: case 0x11b: case 0x205: case 0x207:
   case 0x229: case 0x247: case 0x1d92: case 0x1e15:
   case 0x1e17: case 0x1e19: case 0x1e1b: case 0x1e1d:
   case 0x1eb9: case 0x1ebb: case 0x1ebd: case 0x1ebf:
   case 0x1ec1: case 0x1ec3: case 0x1ec5: case 0x1ec7:
      EMIT2('e') EMIT2(e_grave) EMIT2(e_acute)
      EMIT2(e_circumflex) EMIT2(e_diaeresis)
      EMIT2(0x113) EMIT2(0x115)
      EMIT2(0x117) EMIT2(0x119) EMIT2(0x11b)
      EMIT2(0x205) EMIT2(0x207) EMIT2(0x229)
      EMIT2(0x247) EMIT2(0x1d92) EMIT2(0x1e15)
      EMIT2(0x1e17) EMIT2(0x1e19) EMIT2(0x1e1b)
      EMIT2(0x1e1d) EMIT2(0x1eb9) EMIT2(0x1ebb)
      EMIT2(0x1ebd) EMIT2(0x1ebf) EMIT2(0x1ec1)
      EMIT2(0x1ec3) EMIT2(0x1ec5) EMIT2(0x1ec7)
      return OK;

   case 'f': case 0x192: case 0x1d6e: case 0x1d82:
   case 0x1e1f: case 0xa799:
      EMIT2('f') EMIT2(0x192) EMIT2(0x1d6e) EMIT2(0x1d82)
      EMIT2(0x1e1f) EMIT2(0xa799)
      return OK;

   case 'g': case 0x11d: case 0x11f: case 0x121: case 0x123:
   case 0x1e5: case 0x1e7: case 0x1f5: case 0x260: case 0x1d83:
   case 0x1e21: case 0xa7a1:
      EMIT2('g') EMIT2(0x11d) EMIT2(0x11f) EMIT2(0x121)
      EMIT2(0x123) EMIT2(0x1e5) EMIT2(0x1e7)
      EMIT2(0x1f5) EMIT2(0x260) EMIT2(0x1d83)
      EMIT2(0x1e21) EMIT2(0xa7a1)
      return OK;

   case 'h': case 0x125: case 0x127: case 0x21f: case 0x1e23:
   case 0x1e25: case 0x1e27: case 0x1e29: case 0x1e2b:
   case 0x1e96: case 0x2c68: case 0xa795:
      EMIT2('h') EMIT2(0x125) EMIT2(0x127) EMIT2(0x21f)
      EMIT2(0x1e23) EMIT2(0x1e25) EMIT2(0x1e27)
      EMIT2(0x1e29) EMIT2(0x1e2b) EMIT2(0x1e96)
      EMIT2(0x2c68) EMIT2(0xa795)
      return OK;

   case 'i': case i_grave: case i_acute: case i_circumflex:
   case i_diaeresis: case 0x129: case 0x12b: case 0x12d:
   case 0x12f: case 0x1d0: case 0x209: case 0x20b:
   case 0x268: case 0x1d96: case 0x1e2d: case 0x1e2f:
   case 0x1ec9: case 0x1ecb:
      EMIT2('i') EMIT2(i_grave) EMIT2(i_acute)
      EMIT2(i_circumflex) EMIT2(i_diaeresis)
      EMIT2(0x129) EMIT2(0x12b) EMIT2(0x12d)
      EMIT2(0x12f) EMIT2(0x1d0) EMIT2(0x209)
      EMIT2(0x20b) EMIT2(0x268) EMIT2(0x1d96)
      EMIT2(0x1e2d) EMIT2(0x1e2f) EMIT2(0x1ec9)
      EMIT2(0x1ecb) EMIT2(0x1ecb)
      return OK;

   case 'j': case 0x135: case 0x1f0: case 0x249:
      EMIT2('j') EMIT2(0x135) EMIT2(0x1f0) EMIT2(0x249)
      return OK;

   case 'k': case 0x137: case 0x199: case 0x1e9: case 0x1d84:
   case 0x1e31: case 0x1e33: case 0x1e35: case 0x2c6a: case 0xa741:
      EMIT2('k') EMIT2(0x137) EMIT2(0x199) EMIT2(0x1e9)
      EMIT2(0x1d84) EMIT2(0x1e31) EMIT2(0x1e33)
      EMIT2(0x1e35) EMIT2(0x2c6a) EMIT2(0xa741)
      return OK;

   case 'l': case 0x13a: case 0x13c: case 0x13e: case 0x140:
   case 0x142: case 0x19a: case 0x1e37: case 0x1e39: case 0x1e3b:
   case 0x1e3d: case 0x2c61:
      EMIT2('l') EMIT2(0x13a) EMIT2(0x13c)
      EMIT2(0x13e) EMIT2(0x140) EMIT2(0x142)
      EMIT2(0x19a) EMIT2(0x1e37) EMIT2(0x1e39)
      EMIT2(0x1e3b) EMIT2(0x1e3d) EMIT2(0x2c61)
      return OK;

   case 'm': case 0x1d6f: case 0x1e3f: case 0x1e41: case 0x1e43:
      EMIT2('m') EMIT2(0x1d6f) EMIT2(0x1e3f)
      EMIT2(0x1e41) EMIT2(0x1e43)
      return OK;

   case 'n': case n_virguilla: case 0x144: case 0x146: case 0x148:
   case 0x149: case 0x1f9: case 0x1d70: case 0x1d87: case 0x1e45:
   case 0x1e47: case 0x1e49: case 0x1e4b: case 0xa7a5:
      EMIT2('n') EMIT2(n_virguilla)
      EMIT2(0x144) EMIT2(0x146) EMIT2(0x148)
      EMIT2(0x149) EMIT2(0x1f9) EMIT2(0x1d70)
      EMIT2(0x1d87) EMIT2(0x1e45) EMIT2(0x1e47)
      EMIT2(0x1e49) EMIT2(0x1e4b) EMIT2(0xa7a5)
      return OK;

   case 'o': case o_grave: case o_acute: case o_circumflex:
   case o_virguilla: case o_diaeresis: case o_slash:
   case 0x14d: case 0x14f: case 0x151: case 0x1a1:
   case 0x1d2: case 0x1eb: case 0x1ed: case 0x1ff:
   case 0x20d: case 0x20f: case 0x22b: case 0x22d:
   case 0x22f: case 0x231: case 0x275: case 0x1e4d:
   case 0x1e4f: case 0x1e51: case 0x1e53: case 0x1ecd:
   case 0x1ecf: case 0x1ed1: case 0x1ed3: case 0x1ed5:
   case 0x1ed7: case 0x1ed9: case 0x1edb: case 0x1edd:
   case 0x1edf: case 0x1ee1: case 0x1ee3:
      EMIT2('o') EMIT2(o_grave) EMIT2(o_acute)
      EMIT2(o_circumflex) EMIT2(o_virguilla)
      EMIT2(o_diaeresis) EMIT2(o_slash)
      EMIT2(0x14d) EMIT2(0x14f) EMIT2(0x151)
      EMIT2(0x1a1) EMIT2(0x1d2) EMIT2(0x1eb)
      EMIT2(0x1ed) EMIT2(0x1ff) EMIT2(0x20d)
      EMIT2(0x20f) EMIT2(0x22b) EMIT2(0x22d)
      EMIT2(0x22f) EMIT2(0x231) EMIT2(0x275)
      EMIT2(0x1e4d) EMIT2(0x1e4f) EMIT2(0x1e51)
      EMIT2(0x1e53) EMIT2(0x1ecd) EMIT2(0x1ecf)
      EMIT2(0x1ed1) EMIT2(0x1ed3) EMIT2(0x1ed5)
      EMIT2(0x1ed7) EMIT2(0x1ed9) EMIT2(0x1edb)
      EMIT2(0x1edd) EMIT2(0x1edf) EMIT2(0x1ee1)
      EMIT2(0x1ee3)
      return OK;

    case 'p': case 0x1a5: case 0x1d71: case 0x1d7d: case 0x1d88:
    case 0x1e55: case 0x1e57:
      EMIT2('p') EMIT2(0x1a5) EMIT2(0x1d71) EMIT2(0x1d7d)
      EMIT2(0x1d88) EMIT2(0x1e55) EMIT2(0x1e57)
      return OK;

   case 'q': case 0x24b: case 0x2a0:
      EMIT2('q') EMIT2(0x24b) EMIT2(0x2a0)
      return OK;

   case 'r': case 0x155: case 0x157: case 0x159: case 0x211:
   case 0x213: case 0x24d: case 0x27d: case 0x1d72: case 0x1d73:
   case 0x1d89: case 0x1e59: case 0x1e5b: case 0x1e5d: case 0x1e5f:
   case 0xa7a7:
      EMIT2('r') EMIT2(0x155) EMIT2(0x157) EMIT2(0x159)
      EMIT2(0x211) EMIT2(0x213) EMIT2(0x24d) EMIT2(0x27d)
      EMIT2(0x1d72) EMIT2(0x1d73) EMIT2(0x1d89) EMIT2(0x1e59)
      EMIT2(0x1e5b) EMIT2(0x1e5d) EMIT2(0x1e5f) EMIT2(0xa7a7)
      return OK;

   case 's': case 0x15b: case 0x15d: case 0x15f: case 0x161:
   case 0x219: case 0x23f: case 0x1d74: case 0x1d8a: case 0x1e61:
   case 0x1e63: case 0x1e65: case 0x1e67: case 0x1e69: case 0xa7a9:
      EMIT2('s') EMIT2(0x15b) EMIT2(0x15d) EMIT2(0x15f)
      EMIT2(0x161) EMIT2(0x219) EMIT2(0x23f) EMIT2(0x1d74)
      EMIT2(0x1d8a) EMIT2(0x1e61) EMIT2(0x1e63) EMIT2(0x1e65)
      EMIT2(0x1e67) EMIT2(0x1e69) EMIT2(0xa7a9)
      return OK;

   case 't': case 0x163: case 0x165: case 0x167: case 0x1ab:
   case 0x1ad: case 0x21b: case 0x288: case 0x1d75: case 0x1e6b:
   case 0x1e6d: case 0x1e6f: case 0x1e71: case 0x1e97: case 0x2c66:
      EMIT2('t') EMIT2(0x163) EMIT2(0x165) EMIT2(0x167)
      EMIT2(0x1ab) EMIT2(0x1ad) EMIT2(0x21b) EMIT2(0x288)
      EMIT2(0x1d75) EMIT2(0x1e6b) EMIT2(0x1e6d) EMIT2(0x1e6f)
      EMIT2(0x1e71) EMIT2(0x1e97) EMIT2(0x2c66)
      return OK;

   case 'u': case u_grave: case u_acute: case u_circumflex:
   case u_diaeresis: case 0x169: case 0x16b: case 0x16d:
   case 0x16f: case 0x171: case 0x173: case 0x1b0: case 0x1d4:
   case 0x1d6: case 0x1d8: case 0x1da: case 0x1dc: case 0x215:
   case 0x217: case 0x289: case 0x1d7e: case 0x1d99: case 0x1e73:
   case 0x1e75: case 0x1e77: case 0x1e79: case 0x1e7b:
   case 0x1ee5: case 0x1ee7: case 0x1ee9: case 0x1eeb:
   case 0x1eed: case 0x1eef: case 0x1ef1:
      EMIT2('u') EMIT2(u_grave) EMIT2(u_acute)
      EMIT2(u_circumflex) EMIT2(u_diaeresis)
      EMIT2(0x169) EMIT2(0x16b)
      EMIT2(0x16d) EMIT2(0x16f) EMIT2(0x171)
      EMIT2(0x173) EMIT2(0x1d6) EMIT2(0x1d8)
      EMIT2(0x215) EMIT2(0x217) EMIT2(0x1b0)
      EMIT2(0x1d4) EMIT2(0x1da) EMIT2(0x1dc)
      EMIT2(0x289) EMIT2(0x1e73) EMIT2(0x1d7e)
      EMIT2(0x1d99) EMIT2(0x1e75) EMIT2(0x1e77)
      EMIT2(0x1e79) EMIT2(0x1e7b) EMIT2(0x1ee5)
      EMIT2(0x1ee7) EMIT2(0x1ee9) EMIT2(0x1eeb)
      EMIT2(0x1eed) EMIT2(0x1eef) EMIT2(0x1ef1)
      return OK;

   case 'v': case 0x28b: case 0x1d8c: case 0x1e7d: case 0x1e7f:
       EMIT2('v') EMIT2(0x28b) EMIT2(0x1d8c) EMIT2(0x1e7d)
       EMIT2(0x1e7f)
       return OK;

   case 'w': case 0x175: case 0x1e81: case 0x1e83: case 0x1e85:
   case 0x1e87: case 0x1e89: case 0x1e98:
       EMIT2('w') EMIT2(0x175) EMIT2(0x1e81) EMIT2(0x1e83)
       EMIT2(0x1e85) EMIT2(0x1e87) EMIT2(0x1e89) EMIT2(0x1e98)
       return OK;

   case 'x': case 0x1e8b: case 0x1e8d:
      EMIT2('x') EMIT2(0x1e8b) EMIT2(0x1e8d)
      return OK;

   case 'y': case y_acute: case y_diaeresis: case 0x177:
   case 0x1b4: case 0x233: case 0x24f: case 0x1e8f:
   case 0x1e99: case 0x1ef3: case 0x1ef5: case 0x1ef7:
   case 0x1ef9:
      EMIT2('y') EMIT2(y_acute) EMIT2(y_diaeresis)
      EMIT2(0x177) EMIT2(0x1b4) EMIT2(0x233) EMIT2(0x24f)
      EMIT2(0x1e8f) EMIT2(0x1e99) EMIT2(0x1ef3)
      EMIT2(0x1ef5) EMIT2(0x1ef7) EMIT2(0x1ef9)
      return OK;

   case 'z': case 0x17a: case 0x17c: case 0x17e: case 0x1b6:
   case 0x1d76: case 0x1d8e: case 0x1e91: case 0x1e93:
   case 0x1e95: case 0x2c6c:
      EMIT2('z') EMIT2(0x17a) EMIT2(0x17c) EMIT2(0x17e)
      EMIT2(0x1b6) EMIT2(0x1d76) EMIT2(0x1d8e) EMIT2(0x1e91)
      EMIT2(0x1e93) EMIT2(0x1e95) EMIT2(0x2c6c)
      return OK;

      // default: character itself
   }

   EMIT2(c);
   return OK;
#undef EMIT2
}

//Return true if the back reference is legal. We must have seen the close brace.
//TODO: Should also check that we don't refer to something that is repeated
//(+*=): what instance of the repetition should we match?
private int
seen_endbrace(int refnum){
   if (hadEndbraceS[refnum]) {
      return true;
   }
   Byte *p;

   // Trick: check if "@<=" or "@<!" follows, in which case
   // the \1 can appear before the referenced match.
   for (p = regparse; *p != ZERO; ++p)
      if (p[0] == '@' && p[1] == '<' && (p[2] == '!' || p[2] == '='))
         break;
   if (*p == ZERO) {
      emsg(_(e_illegal_back_reference));
      anyRegexEmsgG = true;
      return false;
   }
   
   return true;
}

//{{{parsing

//Parse the lowest level.
//
//An atom can be one of a long list of items.  Many atoms match one character
//in the text.  It is often an ordinary character or a character class.
//Braces can be used to make a pattern into an atom.  The "\z(\)" construct
//is only for syntax highlighting.
//
//atom    ::=     ordinary-atom
//    or  \( pattern \)
//    or  \%( pattern \)
//    or  \z( pattern \)
private int
parseAtom(OUT Boole* hadEol) {
   int      charclass;
   int      equiclass;
   int      collclass;
   int      got_coll_char;
   Byte   *p;
   Byte   *endp;
   Byte   *old_regparse = regparse;
   int      extra = 0;
   int      emit_range;
   int      negated;
   Unt      result;
   Unt      startc = UNT;
   int      save_prev_at_start = prev_at_start;

   Unt c = getchr();
   switch (c) {
   case ZERO:
       EMSG_RET_FAIL(_(e_nfa_regexp_end_encountered_prematurely));
       // -FALLTHROUGH
   case Magic('^'):
       EMIT(BOL);
       break;
   case Magic('$'):
       EMIT(EOL);
       *hadEol = true;
       break;
   case Magic('<'):
       EMIT(BOW);
       break;
   case Magic('>'):
       EMIT(EOW);
       break;
   case Magic('_'):
      c = no_Magic(getchr());
      if (c == ZERO)
         EMSG_RET_FAIL(_(e_nfa_regexp_end_encountered_prematurely));

      if (c == '^') {  // "\_^" is start-of-line
         EMIT(BOL);
         break;
      }
      if (c == '$') {   // "\_$" is end-of-line
         EMIT(EOL);
         *hadEol = true;
         break;
      }

      extra = ADD_NL;

      // "\_[" is collection plus newline
      if (c == '[')
         goto collection;

   // "\_x" is character class plus newline
   // FALLTHROUGH

   // Character classes.
   case Magic('.'):
   case Magic('i'):
   case Magic('I'):
   case Magic('k'):
   case Magic('K'):
   case Magic('f'):
   case Magic('F'):
   case Magic('p'):
   case Magic('P'):
   case Magic('s'):
   case Magic('S'):
   case Magic('d'):
   case Magic('D'):
   case Magic('x'):
   case Magic('X'):
   case Magic('o'):
   case Magic('O'):
   case Magic('w'):
   case Magic('W'):
   case Magic('h'):
   case Magic('H'):
   case Magic('a'):
   case Magic('A'):
   case Magic('l'):
   case Magic('L'):
   case Magic('u'):
   case Magic('U'):
      p = firstOccurrence(classCharsS, no_Magic(c));
      if (p == NULL) {
         if (extra == ADD_NL) {
            showErrFmtMsg(_(e_nfa_regexp_invalid_character_class_nr), c);
            anyRegexEmsgG = true;
            return FAIL;
         }
         internalErrFmtMsg("Unknown character class char: %d", c);
         return FAIL;
      }

      // When '.' is followed by a composing char, ignore the dot, so that the composing char is 
      // matched here.
      if (c == Magic('.') && utf_iscomposing(peekchr())) {
         old_regparse = regparse;
         c = getchr();
         goto nfa_do_multibyte;
      }
      EMIT(classCodes[p - classCharsS]);
      if (extra == ADD_NL) {
         EMIT(NEWL);
         EMIT(OR);
         regflags |= RF_HASNL;
      }
      break;

   case Magic('n'):
      if (reg_string) {
         // In a string "\n" matches a newline character.
         EMIT(NL);
      } else {
         // In buffer text "\n" matches the end of a line.
         EMIT(NEWL);
         regflags |= RF_HASNL;
      }
      break;

   case Magic('('):
      if (parse(REG_PAREN, OUT hadEol) == FAIL)
         return FAIL;       // cascaded error
      break;

   case Magic('|'):
   case Magic('&'):
   case Magic(')'):
      showErrFmtMsg(_(e_nfa_regexp_misplaced_chr), no_Magic(c));
      return FAIL;

   case Magic('='):
   case Magic('?'):
   case Magic('+'):
   case Magic('@'):
   case Magic('*'):
   case Magic('{'):
      // these should follow an atom, not form an atom
      showErrFmtMsg(_(e_nfa_regexp_misplaced_chr), no_Magic(c));
      return FAIL;

   case Magic('~'): {
      Byte       *lp;

      // Previous substitute pattern. Generated as "\%(pattern\)".
      if (reg_prev_sub == NULL) {
          emsg(_(e_no_previous_substitute_regular_expression));
          return FAIL;
      }
      for (lp = reg_prev_sub; *lp != ZERO; MB_CPTR_ADV(lp)) {
         EMIT(mb_ptr2char(lp));
         if (lp != reg_prev_sub) {
            EMIT(CONCAT);
         } 
      }
      EMIT(NOPEN);
      break;
   }

   case Magic('1'):
   case Magic('2'):
   case Magic('3'):
   case Magic('4'):
   case Magic('5'):
   case Magic('6'):
   case Magic('7'):
   case Magic('8'):
   case Magic('9'): {
         int refnum = no_Magic(c) - '1';

         if (!seen_endbrace(refnum + 1))
            return FAIL;
         EMIT(BACKREF1 + refnum);
         exe.nfa_has_backref = true;
      }
      break;

   case Magic('z'):
      c = no_Magic(getchr());
      switch (c) {
      case 's':
         EMIT(ZSTART);
         if (re_mult_next(S"\\zs") == FAIL)
            return FAIL;
         break;
      case 'e':
         EMIT(ZEND);
         exe.nfa_has_zend = true;
         if (re_mult_next(S"\\ze") == FAIL)
            return FAIL;
         break;
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
         // \z1...\z9
         if ((reg_do_extmatch & REX_USE) == 0)
            EMSG_RET_FAIL(_(e_z1_z9_not_allowed_here));
         EMIT(ZREF1 + (no_Magic(c) - '1'));
         // No need to set exe.nfa_has_backref, the sub-matches don't
         // change when \z1 .. \z9 matches or not.
         re_has_z = REX_USE;
         break;
      case '(':
         // \z(
         if ((reg_do_extmatch & REX_SET) == 0)
            EMSG_RET_FAIL(_(e_z_not_allowed_here));
         if (parse(REG_ZPAREN, OUT hadEol) == FAIL)
            return FAIL;       // cascaded error
         re_has_z = REX_SET;
         break;
      default:
         showErrFmtMsg(_(e_nfa_regexp_unknown_operator_z_chr), no_Magic(c));
         return FAIL;
      }
      break;

   case Magic('%'):
      c = no_Magic(getchr());
      switch (c) {
      // () without a back reference
      case '(':
         if (parse(REG_NPAREN, OUT hadEol) == FAIL) {
            return FAIL;
         } 
         EMIT(NOPEN);
         break;

      case 'd':   // %d123 decimal
      case 'x':   // %xab hex 2
      case 'u':   // %uabcd hex 4
      case 'U': {  // %U1234abcd hex 8
         Long nr;
         switch (c) {
         case 'd': nr = getdecchrs(); break;
         case 'x': nr = gethexchrs(2); break;
         case 'u': nr = gethexchrs(4); break;
         case 'U': nr = gethexchrs(8); break;
         default:  nr = -1; break;
         }

         if (nr < 0 || nr > INT_MAX)
            EMSG2_RET_FAIL(_(e_invalid_character_after_str_2), reg_magic == MAGIC_ALL);
         // A ZERO is stored in the text as NL
         // TODO: what if a composing character follows?
         EMIT(nr == 0 ? 0x0a : (long)nr);
         break;
      }

      // Catch \%^ and \%$ regardless of where they appear in the
      // pattern -- regardless of whether or not it makes sense.
      case '^':
         EMIT(BOF);
         break;

      case '$':
         EMIT(EOFF);
         break;

      case '#':
         if (regparse[0] == '=' && regparse[1] >= 48 && regparse[1] <= 50) {
            // misplaced \%#=1
            showErrFmtMsg(_(e_atom_engine_must_be_at_start_of_pattern), regparse[1]);
            return FAIL;
         }
         EMIT(CURSOR);
         break;

      case 'V':
         EMIT(VISUAL);
         break;

      case 'C':
         EMIT(ANY_COMPOSING);
         break;

      case '[': {
         int       n;

         // \%[abc]
         for (n = 0; (c = peekchr()) != ']'; ++n) {
            if (c == ZERO)
               EMSG2_RET_FAIL(_(e_missing_sb_after_str), reg_magic == MAGIC_ALL);
            // recursive call!
            if (parseAtom(OUT hadEol) == FAIL)
               return FAIL;
         }
         getchr();  // get the ]
         if (n == 0)
            EMSG2_RET_FAIL(_(e_empty_str_brackets), reg_magic == MAGIC_ALL);
         EMIT(OPT_CHARS);
         EMIT(n);

         //Emit as "\%(\%[abc]\)" to be able to handle
         //"\%[abc]*" which would cause the empty string to be
         //matched an unlimited number of times. NOPEN is
         //added only once at a position, while SPLIT is
         //added multiple times.  This is more efficient than
         //not allowing SPLIT multiple times, it is used a lot.
         EMIT(NOPEN);
         break;
         }

         default: {
            Ulong   n = 0;
            int   cmp = c;
            int   cur = false;
            int   got_digit = false;

            if (c == '<' || c == '>')
               c = getchr();
            if (no_Magic(c) == '.') {
               cur = true;
               c = getchr();
            }
            while (EE_ISDIGIT(c)) {
               Ulong tmp;

               if (cur) {
                  showErrFmtMsg(_(e_regexp_number_after_dot_pos_search_chr), no_Magic(c));
                  return FAIL;
               }
               tmp = n * 10 + (c - '0');

               if (tmp < n) {
                  // overflow.
                  emsg(_(e_percent_value_too_large));
                  return FAIL;
               }
               n = tmp;
               c = getchr();
               got_digit = true;
            }
            if (c == 'l' || c == 'c' || c == 'v') {
               Ulong limit = INT_MAX;

               if (!cur && !got_digit) {
                  showErrFmtMsg(_(e_nfa_regexp_missing_value_in_chr), no_Magic(c));
                  return FAIL;
               }
               if (c == 'l') {
                  if (cur)
                     n = curPor->cursor.lnum;
                  // \%{n}l  \%{n}<l  \%{n}>l
                  EMIT(cmp == '<' ? LNUM_LT : cmp == '>' ? LNUM_GT : LNUM);
                  if (save_prev_at_start)
                      at_start = true;
               } ei (c == 'c') {
                  if (cur) {
                     n = curPor->cursor.col;
                     n++;
                  }
                  // \%{n}c  \%{n}<c  \%{n}>c
                  EMIT(cmp == '<' ? COL_LT : cmp == '>' ? COL_GT : COL);
               } else {
                  if (cur) {
                     ColNr vcol = 0;
                     bookGetVirtualColInVirtualMode(curPor, &curPor->cursor, NULL, NULL, &vcol);
                     n = ++vcol;
                  }
                  // \%{n}v  \%{n}<v  \%{n}>v
                  EMIT(cmp == '<' ? VCOL_LT : cmp == '>' ? VCOL_GT : VCOL);
                  limit = INT_MAX / MB_MAXBYTES;
               }
               if (n >= limit) {
                  emsg(_(e_percent_value_too_large));
                  return FAIL;
               }
               EMIT((int)n);
               break;
            } ei (no_Magic(c) == '\'' && n == 0) {
               // \%'m  \%<'m  \%>'m
               EMIT(cmp == '<' ? MARK_LT :
               cmp == '>' ? MARK_GT : MARK);
               EMIT(getchr());
               break;
            }
         }
         showErrFmtMsg(_(e_nfa_regexp_unknown_operator_percent_chr), no_Magic(c));
         return FAIL;
      }
      break;

   case Magic('['):
public collection:
      //[abc]  uses START_COLL - END_COLL
      //[^abc] uses START_NEG_COLL - END_NEG_COLL
      //Each character is produced as a regular state, using CONCAT to bind them together.
      //Besides normal characters there can be:
      //- character classes  CLASS_*
      //- ranges, two characters followed by RANGE.

      p = regparse;
      endp = skip_anyof(p);
      if (*endp == ']') {
         Unt plen;
         // Try to reverse engineer character classes. For example, recognize that [0-9] stands for 
         // \d and [A-Za-z_] for \h, and perform the necessary substitutions in the NFA.
         result = recognizeCharClass(regparse, endp, extra == ADD_NL);
         if (result != FAIL) {
            if (result >= FIRST_NL && result <= LAST_NL) {
               EMIT(result - ADD_NL);
               EMIT(NEWL);
               EMIT(OR);
            } else {
               EMIT(result);
            } 
            regparse = endp;
            MB_PTR_ADV(regparse);
            return OK;
         }
         //Failed to recognize a character class. Use the simple version that turns [abc] into 
         //'a' OR 'b' OR 'c'
         startc = UNT;
         negated = false;
         if (*regparse == '^')  {       // negated range
            negated = true;
            MB_PTR_ADV(regparse);
            EMIT(START_NEG_COLL);
         } else {
            EMIT(START_COLL);
         } 
         if (*regparse == '-') {
            startc = '-';
            EMIT(startc);
            EMIT(CONCAT);
            MB_PTR_ADV(regparse);
         }
         // Emit the OR branches for each character in the []
         emit_range = false;
         while (regparse < endp) {
            Unt oldstartc = startc;

            startc = UNT;
            got_coll_char = false;
            if (*regparse == '[') {
               // Check for [: :], [= =], [. .]
               equiclass = collclass = 0;
               charclass = get_char_class(&regparse);
               if (charclass == CHAR_CLASS_NONE) {
                  equiclass = get_equi_class(&regparse);
                  if (equiclass == 0)
                     collclass = get_coll_element(&regparse);
               }

               //Character class like [:alpha:]
               if (charclass != CHAR_CLASS_NONE) {
                  switch (charclass) {
                  case CHAR_CLASS_ALNUM:
                     EMIT(CLASS_ALNUM);
                     break;
                  case CHAR_CLASS_ALPHA:
                     EMIT(CLASS_ALPHA);
                     break;
                  case CHAR_CLASS_BLANK:
                     EMIT(CLASS_BLANK);
                     break;
                  case CHAR_CLASS_CNTRL:
                     EMIT(CLASS_CNTRL);
                     break;
                  case CHAR_CLASS_DIGIT:
                     EMIT(CLASS_DIGIT);
                     break;
                  case CHAR_CLASS_GRAPH:
                     EMIT(CLASS_GRAPH);
                     break;
                  case CHAR_CLASS_LOWER:
                     EMIT(CLASS_LOWER);
                     break;
                  case CHAR_CLASS_PRINT:
                     EMIT(CLASS_PRINT);
                     break;
                  case CHAR_CLASS_PUNCT:
                     EMIT(CLASS_PUNCT);
                     break;
                  case CHAR_CLASS_SPACE:
                     EMIT(CLASS_SPACE);
                     break;
                  case CHAR_CLASS_UPPER:
                     EMIT(CLASS_UPPER);
                     break;
                  case CHAR_CLASS_XDIGIT:
                     EMIT(CLASS_XDIGIT);
                     break;
                  case CHAR_CLASS_TAB:
                     EMIT(CLASS_TAB);
                     break;
                  case CHAR_CLASS_RETURN:
                     EMIT(CLASS_RETURN);
                     break;
                  case CHAR_CLASS_BACKSPACE:
                     EMIT(CLASS_BACKSPACE);
                     break;
                  case CHAR_CLASS_ESCAPE:
                     EMIT(CLASS_ESCAPE);
                     break;
                  case CHAR_CLASS_IDENT:
                     EMIT(CLASS_IDENT);
                     break;
                  case CHAR_CLASS_KEYWORD:
                     EMIT(CLASS_KEYWORD);
                     break;
                  case CHAR_CLASS_FNAME:
                     EMIT(CLASS_FNAME);
                     break;
                  }
                  EMIT(CONCAT);
                  continue;
               }
               //Try equivalence class [=a=] and the like
               if (equiclass != 0) {
                  result = nfa_emit_equi_class(equiclass);
                  if (result == FAIL) {
                     //should never happen
                     EMSG_RET_FAIL(_(e_error_building_nfa_with_equivalence_class));
                  }
                  continue;
               }
               //Try collating class like [. .]
               if (collclass != 0) {
                  startc = collclass;    // allow [.a.]-x as a range
                  //Will emit the proper atom at the end of the while loop.
               }
            }
            //Try a range like 'a-x' or '\t-z'. Also allows '-' as a start character.
            if (*regparse == '-' && oldstartc != UNT) {
               emit_range = true;
               startc = oldstartc;
               MB_PTR_ADV(regparse);
               continue;       // reading the end of the range
            }

            //Now handle simple and escaped characters. Eegl considers "\]", "\^", "\]" and "\\" 
            //special, as well as "\t", "\e", etc. Posix doesn't recognize backslash at all.
            if (*regparse == '\\'
               && regparse + 1 <= endp
               && (firstOccurrence(REGEXP_INRANGE, regparse[1]) != NULL
                 || (firstOccurrence(REGEXP_ABBR, regparse[1]) != NULL)
               )
            ) {
               MB_PTR_ADV(regparse);

               if (*regparse == 'n')
                  startc = (reg_string || emit_range || regparse[1] == '-') ? NL : NEWL;
               ei (*regparse == 'd'
                      || *regparse == 'o'
                      || *regparse == 'x'
                      || *regparse == 'u'
                      || *regparse == 'U'
               ) {
                  // TODO(RE) This needs more testing
                  Long hexValue = coll_get_char();
                  // max UTF-8 Codepoint is U+10FFFF, but allow values until INT_MAX
                  if (hexValue >= INT_MAX)
                     EMSG_RET_FAIL(_(e_unicode_val_too_large));
                  startc = (Unt)hexValue;
                  got_coll_char = true;
                  MB_PTR_BACK(old_regparse, regparse);
               } else {
                  // \r,\t,\e,\b
                  startc = backslash_trans(*regparse);
               }
            }

            // Normal printable char
            if (startc == UNT)
               startc = mb_ptr2char(regparse);

            // Previous char was '-', so this char is end of range.
            if (emit_range) {
               Unt   endc = startc;

               startc = oldstartc;
               if (startc > endc)
                  EMSG_RET_FAIL(_(e_reverse_range_in_character_class));

               if (endc > startc + 2) {
                  // Emit a range instead of the sequence of
                  // individual characters.
                  if (startc == 0) {
                     // \x00 is translated to \x0a, start at \x01.
                     EMIT(1);
                  } else {
                     --postfixS; // remove CONCAT
                  } 
                  EMIT(endc);
                  EMIT(RANGE);
                  EMIT(CONCAT);
               } ei (mb_char2len(startc) > 1 || mb_char2len(endc) > 1) {
                  // Emit the characters in the range.
                  // "startc" was already emitted, so skip it.
                  for (c = startc + 1; c <= endc; c++) {
                     EMIT(c);
                     EMIT(CONCAT);
                  }
               } else {
                  // Emit the range. "startc" was already emitted, so
                  // skip it.
                  for (c = startc + 1; c <= endc; c++) {
                     EMIT(c);
                     EMIT(CONCAT);
                  }
               }
               emit_range = false;
               startc = -1;
            } else {
               // This char (startc) is not part of a range. Just emit it.
               // Normally, simply emit startc. But if we get char
               // code=0 from a collating char, then replace it with 0x0a.
               // This is needed to completely mimic the behaviour of the backtracking engine.
               if (startc == NEWL) {
                  // Line break can't be matched as part of the
                  // collection, add an OR below. But not for negated range.
                  if (!negated)
                     extra = ADD_NL;
               } else {
                  if (got_coll_char == true && startc == 0) {
                     EMIT(0x0a);
                     EMIT(CONCAT);
                  } else {
                     EMIT(startc);
                     if (!(utf_ptr2len(regparse) != (plen = utfCharLen(regparse)))) {
                        EMIT(CONCAT);
                     }
                  }
               }
            }

            if (utf_ptr2len(regparse) != (plen = utfCharLen(regparse))) {
               Unt i = utf_ptr2len(regparse);

               c = mb_ptr2char(regparse + i);

               // Add composing characters
               for (;;) {
                  if (c == 0) {
                     // \x00 is translated to \x0a, start at \x01.
                     EMIT(1);
                  } else {
                     EMIT(c);
                  } 
                  EMIT(CONCAT);
                  if ((i += mb_char2len(c)) >= plen)
                     break;
                  c = mb_ptr2char(regparse + i);
               }
               EMIT(COMPOSING);
               EMIT(CONCAT);
             }
             MB_PTR_ADV(regparse);
         } // while (p < endp)

         MB_PTR_BACK(old_regparse, regparse);
         if (*regparse == '-') {      // if last, '-' is just a char
            EMIT('-');
            EMIT(CONCAT);
         }

         // skip the trailing ]
         regparse = endp;
         MB_PTR_ADV(regparse);

         // Mark end of the collection.
         if (negated == true) {
            EMIT(END_NEG_COLL);
         } else {
            EMIT(END_COLL);
         } 

         // \_[] also matches \n but it's not negated
         if (extra == ADD_NL) {
            EMIT(reg_string ? NL : NEWL);
            EMIT(OR);
         }

         return OK;
      } // if exists closing ]

      if (reg_strict)
         EMSG_RET_FAIL(_(e_missing_rsb_after_str_lsb));
       // FALLTHROUGH

   default: {
      int   plen;

public nfa_do_multibyte:
      //plen is length of current char with composing chars
      if ((int)mb_char2len(c) != (plen = utfCharLen(old_regparse)) || utf_iscomposing(c)) {
         int i = 0;

         // A base character plus composing characters, or just one
         // or more composing characters.
         // This requires creating a separate atom as if enclosing
         // the characters in (), where COMPOSING is the ( and
         // END_COMPOSING is the ). Note that right now we are
         // building the postfix form, not the NFA itself;
         // a composing char could be: a, b, c, COMPOSING
         // where 'b' and 'c' are chars with codes > 256.
         for (;;) {
            EMIT(c);
            if (i > 0) {
               EMIT(CONCAT);
            } 
            if ((i += mb_char2len(c)) >= plen)
               break;
            c = mb_ptr2char(old_regparse + i);
         }
         EMIT(COMPOSING);
         regparse = old_regparse + plen;
      } else { // ordinary normal characters
         c = no_Magic(c);
         EMIT(c);
      }
      return OK;
      }
    }

    return OK;
}

//Parse something followed by possible [*+=].
//
//A piece is an atom, possibly followed by a multi, an indication of how many
//times the atom can be matched.  Example: "a*" matches any sequence of "a"
//characters: "", "a", "aa", etc.
//
//piece   ::=       atom
//  or  atom  multi
private int
parsePiece(OUT Boole* hadEol) {
   Unt      i;
   int      op;
   int      ret;
   long   minval, maxval;
   int      greedy = true;      // Braces are prefixed with '-' ?
   ParseState old_state;
   ParseState new_state;
   long   c2;
   int      oldPostfixLen;
   int      my_postfixStartS;
   int      quest;

   // Save the current parse state, so that we can use it if <atom>{m,n} is next.
   saveParseState(&old_state);

   // store current pos in the postfix form, for \{m,n} involving 0s
   my_postfixStartS = (int)(postfixS - postfixStartS);

   ret = parseAtom(OUT hadEol);
   if (ret == FAIL)
      return FAIL;       // cascaded error

   op = peekchr();
   if (re_multi_type(op) == NOT_MULTI)
      return OK;

   skipchr();
   switch (op) {
   case Magic('*'):
      EMIT(STAR);
      break;
   case Magic('+'):
      //Trick: Normally, (a*)\+ would match the whole input "aaa". The first and only submatch 
      //would be "aaa". But the backtracking engine interprets the plus as "try matching one more 
      //time", and a* matches a second time at the end of the input, the empty string. The 
      //submatch will be the empty string.
      //
      //In order to be consistent with the old engine, we replace <atom>+ with <atom><atom>*
      restoreParseState(&old_state);
      curchr = -1;
      if (parseAtom(OUT hadEol) == FAIL)
         return FAIL;
      EMIT(STAR);
      EMIT(CONCAT);
      skipchr();      // skip the \+
      break;

   case Magic('@'):
      c2 = (long)getdecchrs();
      op = no_Magic(getchr());
      i = 0;
      switch(op) {
      case '=':
         // \@=
         i = PREV_ATOM_NO_WIDTH;
         break;
      case '!':
         // \@!
         i = PREV_ATOM_NO_WIDTH_NEG;
         break;
      case '<':
         op = no_Magic(getchr());
         if (op == '=')
            // \@<=
            i = PREV_ATOM_JUST_BEFORE;
         ei (op == '!')
            // \@<!
            i = PREV_ATOM_JUST_BEFORE_NEG;
         break;
      case '>':
         // \@>
         i = PREV_ATOM_LIKE_PATTERN;
         break;
      }
      if (i == 0) {
         showErrFmtMsg(_(e_nfa_regexp_unknown_operator_at_chr), op);
         return FAIL;
      }
      EMIT(i);
      if (i == PREV_ATOM_JUST_BEFORE || i == PREV_ATOM_JUST_BEFORE_NEG) {
         EMIT(c2);
      } 
      break;

   case Magic('?'):
   case Magic('='):
      EMIT(QUEST);
      break;

   case Magic('{'):
      // a{2,5} will expand to 'aaa?a?a?'
      // a{-1,3} will expand to 'aa??a??', where ?? is the nongreedy version of '?'
      // \v(ab){2,3} will expand to '(ab)(ab)(ab)?', where all the parentheses have the same id

      greedy = true;
      c2 = peekchr();
      if (c2 == '-' || c2 == Magic('-')) {
         skipchr();
         greedy = false;
      }
      if (!read_limits(&minval, &maxval))
         EMSG_RET_FAIL(_(e_nfa_regexp_error_reading_repetition_limits));

      //  <atom>{0,inf}, <atom>{0,} and <atom>{}  are equivalent to <atom>*
      if (minval == 0 && maxval == MAX_LIMIT) {
         if (greedy) {     // { { (match the braces)
            // \{}, \{0,}
            EMIT(STAR);
         } else {       // { { (match the braces)
            // \{-}, \{-0,}
            EMIT(STAR_NONGREEDY);
         } 
         break;
      }

      // Special case: x{0} or x{-0}
      if (maxval == 0) {
         // Ignore result of previous call to parseAtom()
         postfixS = postfixStartS + my_postfixStartS;
         // EMPTY is 0-length and works everywhere
         EMIT(EMPTY);
         return OK;
      }

      //Ignore previous call to parseAtom()
      postfixS = postfixStartS + my_postfixStartS;
      //Save parse state after the repeated atom and the \{}
      saveParseState(&new_state);

      quest = (greedy == true? QUEST : QUEST_NONGREEDY);
      for (i = 0; i < maxval; i++) {
         //Goto beginning of the repeated atom
         restoreParseState(&old_state);
         oldPostfixLen = (int)(postfixS - postfixStartS);
         if (parseAtom(OUT hadEol) == FAIL)
            return FAIL;
         //after "minval" times, atoms are optional
         if (i + 1 > minval) {
            if (maxval == MAX_LIMIT) {
               if (greedy) {
                  EMIT(STAR);
               } else {
                  EMIT(STAR_NONGREEDY);
               } 
            } else {
               EMIT(quest);
            } 
         }
         if (oldPostfixLen != my_postfixStartS) {
            EMIT(CONCAT);
         } 
         if (i + 1 > minval && maxval == MAX_LIMIT)
            break;
      }
      // Go to just after the repeated atom and the \{}
      restoreParseState(&new_state);
      curchr = -1;
      break;

   default:
      break;
   }   // end switch

   if (re_multi_type(peekchr()) != NOT_MULTI)
      // Can't have a multi follow a multi.
      EMSG_RET_FAIL(_(e_nfa_regexp_cant_have_multi_follow_multi));

   return OK;
}

//Parse one or more pieces, concatenated.  It matches a match for the
//first piece, followed by a match for the second piece, etc.  Example:
//"f[0-9]b", first matches "f", then a digit and then "b".
//
//concat  ::=       piece
//  or  piece piece
//  or  piece piece piece
//  etc.
private int
parseOneOrMorePieces(OUT Boole* hadEol){
   int      cont = true;
   int      first = true;

   while (cont) {
      switch (peekchr()) {
      case ZERO:
      case Magic('|'):
      case Magic('&'):
      case Magic(')'):
         cont = false;
         break;

      case Magic('Z'):
         regflags |= RF_ICOMBINE;
         skipchr_keepstart();
         break;
      case Magic('c'):
         regflags |= RF_ICASE;
         skipchr_keepstart();
         break;
      case Magic('C'):
         regflags |= RF_NOICASE;
         skipchr_keepstart();
         break;
      case Magic('v'):
         reg_magic = MAGIC_ALL;
         skipchr_keepstart();
         curchr = -1;
         break;
      case Magic('m'):
         reg_magic = MAGIC_ON;
         skipchr_keepstart();
         curchr = -1;
         break;
      case Magic('M'):
         reg_magic = MAGIC_OFF;
         skipchr_keepstart();
         curchr = -1;
         break;
      case Magic('V'):
         reg_magic = MAGIC_NONE;
         skipchr_keepstart();
         curchr = -1;
         break;

      default:
         if (parsePiece(OUT hadEol) == FAIL)
            return FAIL;
      if (first == false) {
         EMIT(CONCAT);
      } else {
         first = false;
      } 
      break;
      }
   }

   return OK;
}

//Parse a branch, one or more concats, separated by "\&".  It matches the
//last concat, but only if all the preceding concats also match at the same
//position.  Examples:
//     "foobeep\&..." matches "foo" in "foobeep".
//     ".*Peter\&.*Bob" matches in a line containing both "Peter" and "Bob"
//
//branch ::=       concat
//     or  concat \& concat
//     or  concat \& concat \& concat
//     etc.
private int
parseBranch(OUT Boole* hadEol) {
   int oldPostfixLen = (int)(postfixS - postfixStartS);

   // First branch, possibly the only one
   if (parseOneOrMorePieces(OUT hadEol) == FAIL)
      return FAIL;

   // Try next concats
   while (peekchr() == Magic('&')) {
      skipchr();
      // if concat is empty do emit a node
      if (oldPostfixLen == (int)(postfixS - postfixStartS)) {
         EMIT(EMPTY);
      } 
      EMIT(NOPEN);
      EMIT(PREV_ATOM_NO_WIDTH);
      oldPostfixLen = (int)(postfixS - postfixStartS);
      if (parseOneOrMorePieces(OUT hadEol) == FAIL)
         return FAIL;
      // if concat is empty do emit a node
      if (oldPostfixLen == (int)(postfixS - postfixStartS)) {
         EMIT(EMPTY);
      } 
      EMIT(CONCAT);
   }

   // if a branch is empty, emit one node for it
   if ((int)(postfixS - postfixStartS) == oldPostfixLen) {
      EMIT(EMPTY);
   } 

   return OK;
}

// Parse a pattern, one or more branches, separated by "\|".  It matches
// anything that matches one of the branches.  Example: "foo\|beep" matches
// "foo" and matches "beep".  If more than one branch matches, the first one is used.
//
// pattern ::=       branch
//  or  branch \| branch
//  or  branch \| branch \| branch
//  etc.
private int
parse(Unt paren, OUT Boole* hadEol) {  // REG_NOPAREN, REG_PAREN, REG_NPAREN or REG_ZPAREN
   int parno = 0;

   if (paren == REG_PAREN) {
      if (regnpar >= NSUBEXP) // Too many `('
         EMSG_RET_FAIL(_(e_nfa_regexp_too_many_parens));
      parno = regnpar++;
   } ei (paren == REG_ZPAREN) {
      // Make a ZOPEN node.
      if (currZParensS >= NSUBEXP)
         EMSG_RET_FAIL(_(e_nfa_regexp_too_many_z));
      parno = currZParensS++;
   }

   if (parseBranch(OUT hadEol) == FAIL)
      return FAIL;       // cascaded error

   while (peekchr() == Magic('|')) {
      skipchr();
      if (parseBranch(OUT hadEol) == FAIL)
         return FAIL;    // cascaded error
      EMIT(OR);
   }

   // Check for proper termination.
   if (paren != REG_NOPAREN && getchr() != Magic(')')) {
      if (paren == REG_NPAREN)
         EMSG2_RET_FAIL(_(e_unmatched_str_percent_open), reg_magic == MAGIC_ALL);
      else
         EMSG2_RET_FAIL(_(e_unmatched_str_open), reg_magic == MAGIC_ALL);
   } ei (paren == REG_NOPAREN && peekchr() != ZERO) {
      if (peekchr() == Magic(')'))
         EMSG2_RET_FAIL(_(e_unmatched_str_close), reg_magic == MAGIC_ALL);
      else
         EMSG_RET_FAIL(_(e_nfa_regexp_proper_termination_error));
   }
   // Here we set the flag allowing back references to this set of parentheses.
   if (paren == REG_PAREN) {
      hadEndbraceS[parno] = true;     // have seen the close paren
      EMIT(MOPEN + parno);
   } ei (paren == REG_ZPAREN) {
      EMIT(ZOPEN + parno);
   }

   return OK;
}

//}}}
//{{{debugging and printing

// Used at the debug prompt: disable the timeout so that expression evaluation can used patterns.
// Must be followed by calling restore_timeout_for_debugging().
public void
save_timeout_for_debugging(void) {
   saved_timeout_flag = (sig_atomic_t *)timeout_flag;
   timeout_flag = &dummy_timeout_flag;
}

public void
restore_timeout_for_debugging(void) {
   timeout_flag = saved_timeout_flag;
}


#ifdef DEBUG
private Byte code[50];

private void
nfa_set_code(int c) {
   int addnl = false;

   if (c >= FIRST_NL && c <= LAST_NL) {
      addnl = true;
      c -= ADD_NL;
   }

   STRCPY(code, "");
   switch (c) {
   case MATCH:     STRCPY(code, "MATCH "); break;
   case SPLIT:     STRCPY(code, "SPLIT "); break;
   case CONCAT:    STRCPY(code, "CONCAT "); break;
   case NEWL:      STRCPY(code, "NEWL "); break;
   case ZSTART:    STRCPY(code, "ZSTART"); break;
   case ZEND:      STRCPY(code, "ZEND"); break;

   case BACKREF1:  STRCPY(code, "BACKREF1"); break;
   case BACKREF2:  STRCPY(code, "BACKREF2"); break;
   case BACKREF3:  STRCPY(code, "BACKREF3"); break;
   case BACKREF4:  STRCPY(code, "BACKREF4"); break;
   case BACKREF5:  STRCPY(code, "BACKREF5"); break;
   case BACKREF6:  STRCPY(code, "BACKREF6"); break;
   case BACKREF7:  STRCPY(code, "BACKREF7"); break;
   case BACKREF8:  STRCPY(code, "BACKREF8"); break;
   case BACKREF9:  STRCPY(code, "BACKREF9"); break;
   case ZREF1:     STRCPY(code, "ZREF1"); break;
   case ZREF2:     STRCPY(code, "ZREF2"); break;
   case ZREF3:     STRCPY(code, "ZREF3"); break;
   case ZREF4:     STRCPY(code, "ZREF4"); break;
   case ZREF5:     STRCPY(code, "ZREF5"); break;
   case ZREF6:     STRCPY(code, "ZREF6"); break;
   case ZREF7:     STRCPY(code, "ZREF7"); break;
   case ZREF8:     STRCPY(code, "ZREF8"); break;
   case ZREF9:     STRCPY(code, "ZREF9"); break;
   case SKIP:      STRCPY(code, "SKIP"); break;

   case PREV_ATOM_NO_WIDTH:
             STRCPY(code, "PREV_ATOM_NO_WIDTH"); break;
   case PREV_ATOM_NO_WIDTH_NEG:
             STRCPY(code, "PREV_ATOM_NO_WIDTH_NEG"); break;
   case PREV_ATOM_JUST_BEFORE:
             STRCPY(code, "PREV_ATOM_JUST_BEFORE"); break;
   case PREV_ATOM_JUST_BEFORE_NEG:
          STRCPY(code, "PREV_ATOM_JUST_BEFORE_NEG"); break;
   case PREV_ATOM_LIKE_PATTERN:
             STRCPY(code, "PREV_ATOM_LIKE_PATTERN"); break;

   case NOPEN:        STRCPY(code, "NOPEN"); break;
   case NCLOSE:       STRCPY(code, "NCLOSE"); break;
   case START_INVISIBLE:   STRCPY(code, "START_INVISIBLE"); break;
   case START_INVISIBLE_FIRST: STRCPY(code, "START_INVISIBLE_FIRST"); break;
   case START_INVISIBLE_NEG: STRCPY(code, "START_INVISIBLE_NEG"); break;
   case START_INVISIBLE_NEG_FIRST: STRCPY(code, "START_INVISIBLE_NEG_FIRST"); break;
   case START_INVISIBLE_BEFORE: STRCPY(code, "START_INVISIBLE_BEFORE"); break;
   case START_INVISIBLE_BEFORE_FIRST: STRCPY(code, "START_INVISIBLE_BEFORE_FIRST"); break;
   case START_INVISIBLE_BEFORE_NEG: STRCPY(code, "START_INVISIBLE_BEFORE_NEG"); break;
   case START_INVISIBLE_BEFORE_NEG_FIRST: STRCPY(code, "START_INVISIBLE_BEFORE_NEG_FIRST"); break;
   case START_PATTERN:     STRCPY(code, "START_PATTERN"); break;
   case END_INVISIBLE:     STRCPY(code, "END_INVISIBLE"); break;
   case END_INVISIBLE_NEG: STRCPY(code, "END_INVISIBLE_NEG"); break;
   case END_PATTERN:       STRCPY(code, "END_PATTERN"); break;

   case COMPOSING:         STRCPY(code, "COMPOSING"); break;
   case END_COMPOSING:     STRCPY(code, "END_COMPOSING"); break;
   case OPT_CHARS:         STRCPY(code, "OPT_CHARS"); break;

   case MOPEN:
   case MOPEN1:
   case MOPEN2:
   case MOPEN3:
   case MOPEN4:
   case MOPEN5:
   case MOPEN6:
   case MOPEN7:
   case MOPEN8:
   case MOPEN9:
       STRCPY(code, "MOPEN(x)");
       code[10] = c - MOPEN + '0';
       break;
   case MCLOSE:
   case MCLOSE1:
   case MCLOSE2:
   case MCLOSE3:
   case MCLOSE4:
   case MCLOSE5:
   case MCLOSE6:
   case MCLOSE7:
   case MCLOSE8:
   case MCLOSE9:
       STRCPY(code, "MCLOSE(x)");
       code[11] = c - MCLOSE + '0';
       break;
   case ZOPEN:
   case ZOPEN1:
   case ZOPEN2:
   case ZOPEN3:
   case ZOPEN4:
   case ZOPEN5:
   case ZOPEN6:
   case ZOPEN7:
   case ZOPEN8:
   case ZOPEN9:
       STRCPY(code, "ZOPEN(x)");
       code[10] = c - ZOPEN + '0';
       break;
   case ZCLOSE:
   case ZCLOSE1:
   case ZCLOSE2:
   case ZCLOSE3:
   case ZCLOSE4:
   case ZCLOSE5:
   case ZCLOSE6:
   case ZCLOSE7:
   case ZCLOSE8:
   case ZCLOSE9:
       STRCPY(code, "ZCLOSE(x)");
       code[11] = c - ZCLOSE + '0';
       break;
   case EOL:      STRCPY(code, "EOL "); break;
   case BOL:      STRCPY(code, "BOL "); break;
   case EOW:      STRCPY(code, "EOW "); break;
   case BOW:      STRCPY(code, "BOW "); break;
   case EOFF:     STRCPY(code, "EOF "); break;
   case BOF:      STRCPY(code, "BOF "); break;
   case LNUM:     STRCPY(code, "LNUM "); break;
   case LNUM_GT:  STRCPY(code, "LNUM_GT "); break;
   case LNUM_LT:  STRCPY(code, "LNUM_LT "); break;
   case COL:      STRCPY(code, "COL "); break;
   case COL_GT:   STRCPY(code, "COL_GT "); break;
   case COL_LT:   STRCPY(code, "COL_LT "); break;
   case VCOL:     STRCPY(code, "VCOL "); break;
   case VCOL_GT:  STRCPY(code, "VCOL_GT "); break;
   case VCOL_LT:  STRCPY(code, "VCOL_LT "); break;
   case MARK:     STRCPY(code, "MARK "); break;
   case MARK_GT:  STRCPY(code, "MARK_GT "); break;
   case MARK_LT:  STRCPY(code, "MARK_LT "); break;
   case CURSOR:   STRCPY(code, "CURSOR "); break;
   case VISUAL:   STRCPY(code, "VISUAL "); break;
   case ANY_COMPOSING:   STRCPY(code, "ANY_COMPOSING "); break;

   case STAR:     STRCPY(code, "STAR "); break;
   case STAR_NONGREEDY: STRCPY(code, "STAR_NONGREEDY "); break;
   case QUEST:    STRCPY(code, "QUEST"); break;
   case QUEST_NONGREEDY: STRCPY(code, "QUEST_NON_GREEDY"); break;
   case EMPTY:    STRCPY(code, "EMPTY"); break;
   case OR:       STRCPY(code, "OR"); break;

   case START_COLL: STRCPY(code, "START_COLL"); break;
   case END_COLL:   STRCPY(code, "END_COLL"); break;
   case START_NEG_COLL: STRCPY(code, "START_NEG_COLL"); break;
   case END_NEG_COLL:   STRCPY(code, "END_NEG_COLL"); break;
   case RANGE:       STRCPY(code, "RANGE"); break;
   case RANGE_MIN:   STRCPY(code, "RANGE_MIN"); break;
   case RANGE_MAX:   STRCPY(code, "RANGE_MAX"); break;

   case CLASS_ALNUM:  STRCPY(code, "CLASS_ALNUM"); break;
   case CLASS_ALPHA:  STRCPY(code, "CLASS_ALPHA"); break;
   case CLASS_BLANK:  STRCPY(code, "CLASS_BLANK"); break;
   case CLASS_CNTRL:  STRCPY(code, "CLASS_CNTRL"); break;
   case CLASS_DIGIT:  STRCPY(code, "CLASS_DIGIT"); break;
   case CLASS_GRAPH:  STRCPY(code, "CLASS_GRAPH"); break;
   case CLASS_LOWER:  STRCPY(code, "CLASS_LOWER"); break;
   case CLASS_PRINT:  STRCPY(code, "CLASS_PRINT"); break;
   case CLASS_PUNCT:  STRCPY(code, "CLASS_PUNCT"); break;
   case CLASS_SPACE:  STRCPY(code, "CLASS_SPACE"); break;
   case CLASS_UPPER:  STRCPY(code, "CLASS_UPPER"); break;
   case CLASS_XDIGIT: STRCPY(code, "CLASS_XDIGIT"); break;
   case CLASS_TAB:    STRCPY(code, "CLASS_TAB"); break;
   case CLASS_RETURN: STRCPY(code, "CLASS_RETURN"); break;
   case CLASS_BACKSPACE:   STRCPY(code, "CLASS_BACKSPACE"); break;
   case CLASS_ESCAPE: STRCPY(code, "CLASS_ESCAPE"); break;
   case CLASS_IDENT:  STRCPY(code, "CLASS_IDENT"); break;
   case CLASS_KEYWORD: STRCPY(code, "CLASS_KEYWORD"); break;
   case CLASS_FNAME:   STRCPY(code, "CLASS_FNAME"); break;

   case ANY:     STRCPY(code, "ANY"); break;
   case IDENT:   STRCPY(code, "IDENT"); break;
   case SIDENT:  STRCPY(code, "SIDENT"); break;
   case KWORD:   STRCPY(code, "KWORD"); break;
   case SKWORD:  STRCPY(code, "SKWORD"); break;
   case FNAME:   STRCPY(code, "FNAME"); break;
   case SFNAME:  STRCPY(code, "SFNAME"); break;
   case PRINT:   STRCPY(code, "PRINT"); break;
   case SPRINT:  STRCPY(code, "SPRINT"); break;
   case WHITE:   STRCPY(code, "WHITE"); break;
   case NWHITE:  STRCPY(code, "NWHITE"); break;
   case DIGIT:   STRCPY(code, "DIGIT"); break;
   case NDIGIT:  STRCPY(code, "NDIGIT"); break;
   case HEX:     STRCPY(code, "HEX"); break;
   case NHEX:    STRCPY(code, "NHEX"); break;
   case WORD:    STRCPY(code, "WORD"); break;
   case NWORD:   STRCPY(code, "NWORD"); break;
   case HEAD:    STRCPY(code, "HEAD"); break;
   case NHEAD:   STRCPY(code, "NHEAD"); break;
   case ALPHA:   STRCPY(code, "ALPHA"); break;
   case NALPHA:  STRCPY(code, "NALPHA"); break;
   case LOWER:   STRCPY(code, "LOWER"); break;
   case NLOWER:  STRCPY(code, "NLOWER"); break;
   case UPPER:   STRCPY(code, "UPPER"); break;
   case NUPPER:  STRCPY(code, "NUPPER"); break;
   case LOWER_IC:  STRCPY(code, "LOWER_IC"); break;
   case NLOWER_IC: STRCPY(code, "NLOWER_IC"); break;
   case UPPER_IC:  STRCPY(code, "UPPER_IC"); break;
   case NUPPER_IC: STRCPY(code, "NUPPER_IC"); break;

   default:
      STRCPY(code, "CHAR(x)");
      code[5] = c;
   }

   if (addnl == true)
      STRCAT(code, " + NEWLINE ");

}

#ifdef REGEXP_LOGGING
private FILE *log_fd;
private Byte e_log_open_failed[] = N_("Could not open temporary log file for writing, displaying on stderr... ");

// Print the postfix notation of the current regexp.
private void
dumpPostfix(Byte *expr, int retval) {
   int *p;

   FILE* f = fopen(REGEXP_DUMP_LOG, "a");
   if (f == NULL)
      return;

   fprintf(f, "\n-------------------------\n");
   if (retval == FAIL)
      fprintf(f, ">>> Regex engine failed... \n");
   ei (retval == OK)
      fprintf(f, ">>> Regex engine succeeded !\n");
   fprintf(f, "Regexp: \"%s\"\nPostfix notation (char): \"", expr);
   for (p = postfixStartS; *p && p < postfixS; p++) {
      nfa_set_code(*p);
      fprintf(f, "%s, ", code);
   }
   fprintf(f, "\"\nPostfix notation (int): ");
   for (p = postfixStartS; *p && p < postfixS; p++)
      fprintf(f, "%d ", *p);
   fprintf(f, "\n\n");
   fclose(f);
}

// Print the NFA starting with a root node "state".
private void
printState(FILE *debugf, RState *state) {
   ArrayList indent;

   ga_init2(&indent, 1, 64);
   ga_append(&indent, '\0');
   printStateWorker(debugf, state, &indent);
   ga_clear(&indent);
}

private void
printStateWorker(FILE *debugf, RState *state, ArrayList *indent) {
   if (!state)
      return;

   fprintf(debugf, "(%2d)", abs(state->id));

   // Output indent
   CS p = (CS)indent->c;
   if (indent->len >= 3) {
      int   last = indent->len - 3;
      Byte   save[2];

      STRNCPY(save, &p[last], 2);
      memcpy(&p[last], "+-", 2);
      fprintf(debugf, " %s", p);
      STRNCPY(&p[last], save, 2);
   } else
      fprintf(debugf, " %s", p);

   nfa_set_code(state->c);
   fprintf(debugf, "%s (%d) (id=%d) val=%d\n",
       code,
       state->c,
       abs(state->id),
       state->val);
   if (state->id < 0)
      return;

   state->id = abs(state->id) * -1;

   // grow indent for state->out
   indent->len -= 1;
   if (state->out1)
      ga_concat(indent, (CS)"| ");
   else
      ga_concat(indent, (CS)"  ");
   ga_append(indent, ZERO);

   printStateWorker(debugf, state->out, indent);

   // replace last part of indent for state->out1
   indent->len -= 3;
   ga_concat(indent, (CS)"  ");
   ga_append(indent, ZERO);

   printStateWorker(debugf, state->out1, indent);

   // shrink indent
   indent->len -= 3;
   ga_append(indent, ZERO);
}

// Print the NFA state machine.
private void
dump(RegProg *prog) {
   FILE *debugf = fopen(REGEXP_DUMP_LOG, "a");

   if (debugf == NULL)
      return;

   printState(debugf, prog->start);

   if (prog->reganch)
      fprintf(debugf, "reganch: %d\n", prog->reganch);
   if (prog->regstart != ZERO)
      fprintf(debugf, "regstart: %c (decimal: %d)\n", prog->regstart, prog->regstart);
   if (prog->input != NULL)
      fprintf(debugf, "input: \"%s\"\n", prog->input);

   fclose(debugf);
}
#endif       // REGEXP_LOGGING
#endif       // DEBUG

//}}}

// NB. Some of the code below is inspired by Russ's.

//Represents an NFA state plus zero or one or two arrows exiting.
//if c == MATCH, no arrows out; matching state.
//If c == SPLIT, unlabeled arrows to out and out1 (if != NULL).
//If c < 256, labeled arrow with character c to out.

private RState   *state_ptr; // points to nfa_prog->state

// Allocate and initialize RState.
private RState *
alloc_state(int c, RState *out, RState *out1) {
   if (stateS >= countStatesS)
      return NULL;

   RState* s = &state_ptr[stateS];
   stateS++;

   s->c    = c;
   s->out  = out;
   s->out1 = out1;
   s->val  = 0;

   s->id   = stateS;
   s->lastlist[0] = 0;
   s->lastlist[1] = 0;

   return s;
}

// Initialize a Frag struct and return it.
private Frag
frag(RState* start, StateList* out) {
   return (Frag){.start = start, .out = out};
}

// Create singleton list containing just outp.
private StateList *
list1(RState** outp) {
   StateList* l = (StateList *)outp;
   l->next = NULL;
   return l;
}

// Patch the list of states at out to point to start.
private void
patch(StateList* l, RState* s) {
   StateList *next;
   for (; l; l = next) {
      next = l->next;
      l->s = s;
   }
}


// Join the two lists l1 and l2, returning the combination.
private StateList *
concat(StateList* l1, StateList* l2) {
   StateList* oldl1 = l1;
   while (l1->next)
      l1 = l1->next;
   l1->next = l2;
   return oldl1;
}

// Stack used for transforming postfix form into NFA.
private Frag empty;

private void
st_error(Unt *postfix UNUSED, Unt* end UNUSED, Unt* p UNUSED) {
#ifdef REGEXP_ERROR_LOG
   int *p2;

   FILE* df = fopen(REGEXP_ERROR_LOG, "a");
   if (df) {
      fprintf(df, "Error popping the stack!\n");
# ifdef DEBUG
      fprintf(df, "Current regexp is \"%s\"\n", regengine.expr);
# endif
      fprintf(df, "Postfix form is: ");
# ifdef DEBUG
      for (p2 = postfix; p2 < end; p2++) {
         nfa_set_code(*p2);
         fprintf(df, "%s, ", code);
      }
      nfa_set_code(*p);
      fprintf(df, "\nCurrent position is: ");
      for (p2 = postfix; p2 <= p; p2 ++) {
         nfa_set_code(*p2);
         fprintf(df, "%s, ", code);
      }
# else
      for (p2 = postfix; p2 < end; p2++)
         fprintf(df, "%d, ", *p2);
      fprintf(df, "\nCurrent position is: ");
      for (p2 = postfix; p2 <= p; p2 ++)
         fprintf(df, "%d, ", *p2);
# endif
      fprintf(df, "\n--------------------------\n");
      fclose(df);
   }
#endif
   emsg(_(e_nfa_regexp_could_not_pop_stack));
}

// Push an item onto the stack.
private void
addFrag(Frag s, Frag** fr, Frag* sentinel) {
   Frag* stackp = *fr;
   if (stackp >= sentinel)
      return;
   *stackp = s;
   (*fr)++;
}

// Pop an item from the stack.
private Frag
removeLastFrag(Frag** p, Frag* stack) {
   (*p)--;
   Frag* stackp = *p;
   if (stackp < stack)
      return empty;
   return **p;
}

// Estimate the maximum byte length of anything matching "state". If unknown or unlimited, return -1
private int
nfa_max_width(RState* startstate, int depth) {
   int l, r;
   RState* state = startstate;
   int len = 0;

   // detect looping in a SPLIT
   if (depth > 4)
      return -1;

   while (state) {
      switch (state->c) {
      case END_INVISIBLE:
      case END_INVISIBLE_NEG:
         // the end, return what we have
         return len;

      case SPLIT:
         // two alternatives, use the maximum
         l = nfa_max_width(state->out, depth + 1);
         r = nfa_max_width(state->out1, depth + 1);
         if (l < 0 || r < 0)
            return -1;
         return len + (l > r ? l : r);

      case ANY:
      case START_COLL:
      case START_NEG_COLL:
         // matches some character, including composing chars
         len += MB_MAXBYTES;
         if (state->c != ANY) {
            // skip over the characters
            state = state->out1->out;
            continue;
         }
         break;

      case DIGIT:
      case WHITE:
      case HEX:
         // ascii
         ++len;
         break;

      case IDENT:
      case SIDENT:
      case KWORD:
      case SKWORD:
      case FNAME:
      case SFNAME:
      case PRINT:
      case SPRINT:
      case NWHITE:
      case NDIGIT:
      case NHEX:
      case WORD:
      case NWORD:
      case HEAD:
      case NHEAD:
      case ALPHA:
      case NALPHA:
      case LOWER:
      case NLOWER:
      case UPPER:
      case NUPPER:
      case LOWER_IC:
      case NLOWER_IC:
      case UPPER_IC:
      case NUPPER_IC:
      case ANY_COMPOSING:
         // possibly non-ascii
         len += 3;
         break;

      case START_INVISIBLE:
      case START_INVISIBLE_NEG:
      case START_INVISIBLE_BEFORE:
      case START_INVISIBLE_BEFORE_NEG:
         // zero-width, out1 points to the END state
         state = state->out1->out;
         continue;

      case BACKREF1:
      case BACKREF2:
      case BACKREF3:
      case BACKREF4:
      case BACKREF5:
      case BACKREF6:
      case BACKREF7:
      case BACKREF8:
      case BACKREF9:
      case ZREF1:
      case ZREF2:
      case ZREF3:
      case ZREF4:
      case ZREF5:
      case ZREF6:
      case ZREF7:
      case ZREF8:
      case ZREF9:
      case NEWL:
      case SKIP:
         // unknown width
         return -1;

      case BOL:
      case EOL:
      case BOF:
      case EOFF:
      case BOW:
      case EOW:
      case MOPEN:
      case MOPEN1:
      case MOPEN2:
      case MOPEN3:
      case MOPEN4:
      case MOPEN5:
      case MOPEN6:
      case MOPEN7:
      case MOPEN8:
      case MOPEN9:
      case ZOPEN:
      case ZOPEN1:
      case ZOPEN2:
      case ZOPEN3:
      case ZOPEN4:
      case ZOPEN5:
      case ZOPEN6:
      case ZOPEN7:
      case ZOPEN8:
      case ZOPEN9:
      case ZCLOSE:
      case ZCLOSE1:
      case ZCLOSE2:
      case ZCLOSE3:
      case ZCLOSE4:
      case ZCLOSE5:
      case ZCLOSE6:
      case ZCLOSE7:
      case ZCLOSE8:
      case ZCLOSE9:
      case MCLOSE:
      case MCLOSE1:
      case MCLOSE2:
      case MCLOSE3:
      case MCLOSE4:
      case MCLOSE5:
      case MCLOSE6:
      case MCLOSE7:
      case MCLOSE8:
      case MCLOSE9:
      case NOPEN:
      case NCLOSE:
      case LNUM_GT:
      case LNUM_LT:
      case COL_GT:
      case COL_LT:
      case VCOL_GT:
      case VCOL_LT:
      case MARK_GT:
      case MARK_LT:
      case VISUAL:
      case LNUM:
      case CURSOR:
      case COL:
      case VCOL:
      case MARK:

      case ZSTART:
      case ZEND:
      case OPT_CHARS:
      case EMPTY:
      case START_PATTERN:
      case END_PATTERN:
      case COMPOSING:
      case END_COMPOSING:
         // zero-width
         break;

      default:
         if (state->c >= UNT_NEG)
            // don't know what this is
            return -1;
         // normal character
         len += MB_CHAR2LEN(state->c);
         break;
      }

      // normal way to continue
      state = state->out;
   }

   // unrecognized, "cannot happen"
   return -1;
}

// Count the number of states in a postfix form.
private int
countStatesInPostfix(Unt* postfix, Unt* end) {
   int count = 0;
   if (!postfix)
      return 0;

   for (Unt* p = postfix; p < end; ++p) {
      switch (*p) {
      case CONCAT:
      case RANGE:
         break;

      case OPT_CHARS:
         ++p;
         count += *p;// get number of characters
         break;

      case PREV_ATOM_NO_WIDTH:
      case PREV_ATOM_NO_WIDTH_NEG:
      case PREV_ATOM_JUST_BEFORE:
      case PREV_ATOM_JUST_BEFORE_NEG:
      case PREV_ATOM_LIKE_PATTERN:
         int pattern = (*p == PREV_ATOM_LIKE_PATTERN);
         count += pattern ? 4 : 2;
         break;
         
      case COMPOSING:   // char with composing char
      case MOPEN:   // \( \) Submatch
      case MOPEN1:
      case MOPEN2:
      case MOPEN3:
      case MOPEN4:
      case MOPEN5:
      case MOPEN6:
      case MOPEN7:
      case MOPEN8:
      case MOPEN9:
      case ZOPEN:   // \z( \) Submatch
      case ZOPEN1:
      case ZOPEN2:
      case ZOPEN3:
      case ZOPEN4:
      case ZOPEN5:
      case ZOPEN6:
      case ZOPEN7:
      case ZOPEN8:
      case ZOPEN9:
      case NOPEN: 
      case BACKREF1:
      case BACKREF2:
      case BACKREF3:
      case BACKREF4:
      case BACKREF5:
      case BACKREF6:
      case BACKREF7:
      case BACKREF8:
      case BACKREF9:
      case ZREF1:
      case ZREF2:
      case ZREF3:
      case ZREF4:
      case ZREF5:
      case ZREF6:
      case ZREF7:
      case ZREF8:
      case ZREF9:
         count += 2;
         break;
         
      case LNUM:
      case LNUM_GT:
      case LNUM_LT:
      case VCOL:
      case VCOL_GT:
      case VCOL_LT:
      case COL:
      case COL_GT:
      case COL_LT:
      case MARK:
      case MARK_GT:
      case MARK_LT:
         ++p; // lnum, col or mark name
         count++;
         break;
      default:
         count++;
         break;
      } // switch(*p)

   } // for (p = postfix; *p; ++p)
   return count + 1;
}

// Convert a postfix form into its equivalent NFA. Return the start state on success, NULL otherwise
private RState *
buildAutomaton(Arr(Unt) postfix, Unt* end) {
   Unt mopen;
   Unt mclose;
   Frag e1;
   Frag e2;
   Frag e;
   RState* s;
   RState* s1;
   RState* matchstate;
   RState* ret = NULL;

   if (postfix == NULL)
      return NULL;

#define PUSH(s) addFrag((s), &stackp, sentinel)
#define POP()   removeLastFrag(&stackp, stack); \
         if (stackp < stack) { \
            st_error(postfix, end, p); \
            eeglFree(stack); \
            return NULL;     \
         }

   // Allocate space for the stack. Max states on the stack: "countStatesS".
   Frag* stack = ALLOC_MULT(Frag, countStatesS + 1);
   Frag* stackp = stack;
   Frag* sentinel = stack + (countStatesS + 1);

   Unt* p;
   for (p = postfix; p < end; ++p) {
      switch (*p) {
      case CONCAT:
         // Concatenation. Pay attention: this operator does not exist in the r.e. itself
         // (it is implicit, really).  It is added when r.e. is parsed to postfix form in parse().
         e2 = POP();
         e1 = POP();
         patch(e1.out, e2.start);
         PUSH(frag(e1.start, e2.out));
         break;

      case OR:
         // Alternation
         e2 = POP();
         e1 = POP();
         s = alloc_state(SPLIT, e1.start, e2.start);
         if (!s)
            goto theend;
         PUSH(frag(s, concat(e1.out, e2.out)));
         break;

      case STAR:
         // Zero or more, prefer more
         e = POP();
         s = alloc_state(SPLIT, e.start, NULL);
         if (!s)
            goto theend;
         patch(e.out, s);
         PUSH(frag(s, list1(&s->out1)));
         break;

      case STAR_NONGREEDY:
         // Zero or more, prefer zero
         e = POP();
         s = alloc_state(SPLIT, NULL, e.start);
         if (!s)
            goto theend;
         patch(e.out, s);
         PUSH(frag(s, list1(&s->out)));
         break;

      case QUEST:
         // one or zero atoms=> greedy match
         e = POP();
         s = alloc_state(SPLIT, e.start, NULL);
         if (!s)
            goto theend;
         PUSH(frag(s, concat(e.out, list1(&s->out1))));
         break;

      case QUEST_NONGREEDY:
         // zero or one atoms => non-greedy match
         e = POP();
         s = alloc_state(SPLIT, NULL, e.start);
         if (!s)
            goto theend;
         PUSH(frag(s, concat(e.out, list1(&s->out))));
         break;

      case END_COLL:
      case END_NEG_COLL:
         // On the stack is the sequence starting with START_COLL or START_NEG_COLL and 
         // all possible characters. Patch it to add the output to the start.
         e = POP();
         s = alloc_state(END_COLL, NULL, NULL);
         if (!s)
            goto theend;
         patch(e.out, s);
         e.start->out1 = s;
         PUSH(frag(e.start, list1(&s->out)));
         break;

      case RANGE:
         // Before this are two characters, the low and high end of a range.  Turn them into two 
         // states with MIN and MAX.
         e2 = POP();
         e1 = POP();
         e2.start->val = e2.start->c;
         e2.start->c = RANGE_MAX;
         e1.start->val = e1.start->c;
         e1.start->c = RANGE_MIN;
         patch(e1.out, e2.start);
         PUSH(frag(e1.start, e2.out));
         break;

      case EMPTY:
         // 0-length, used in a repetition with max/min count of 0
         s = alloc_state(EMPTY, NULL, NULL);
         if (s == NULL)
            goto theend;
         PUSH(frag(s, list1(&s->out)));
         break;

      case OPT_CHARS: {
         int n;
         // \%[abc] implemented as:
         //    SPLIT
         //    +-CHAR(a)
         //    | +-SPLIT
         //    |   +-CHAR(b)
         //    |   | +-SPLIT
         //    |   |   +-CHAR(c)
         //    |   |   | +-next
         //    |   |   +- next
         //    |   +- next
         //    +- next
         n = *++p; // get number of characters
         s = NULL; // avoid compiler warning
         e1.out = NULL; // stores list with out1's
         s1 = NULL; // previous SPLIT to connect to
         while (n-- > 0) {
            e = POP(); // get character
            s = alloc_state(SPLIT, e.start, NULL);
            if (s == NULL)
               goto theend;
            if (e1.out == NULL)
               e1 = e;
            patch(e.out, s1);
            concat(e1.out, list1(&s->out1));
            s1 = s;
         }
         PUSH(frag(s, e1.out));
         break;
      }

      case PREV_ATOM_NO_WIDTH:
      case PREV_ATOM_NO_WIDTH_NEG:
      case PREV_ATOM_JUST_BEFORE:
      case PREV_ATOM_JUST_BEFORE_NEG:
      case PREV_ATOM_LIKE_PATTERN: {
         int before = (*p == PREV_ATOM_JUST_BEFORE || *p == PREV_ATOM_JUST_BEFORE_NEG);
         int pattern = (*p == PREV_ATOM_LIKE_PATTERN);
         int start_state;
         int end_state;
         int n = 0;
         RState *zend;
         RState *skip;

         switch (*p) {
         case PREV_ATOM_NO_WIDTH:
            start_state = START_INVISIBLE;
            end_state = END_INVISIBLE;
            break;
         case PREV_ATOM_NO_WIDTH_NEG:
            start_state = START_INVISIBLE_NEG;
            end_state = END_INVISIBLE_NEG;
            break;
         case PREV_ATOM_JUST_BEFORE:
            start_state = START_INVISIBLE_BEFORE;
            end_state = END_INVISIBLE;
            break;
         case PREV_ATOM_JUST_BEFORE_NEG:
            start_state = START_INVISIBLE_BEFORE_NEG;
            end_state = END_INVISIBLE_NEG;
            break;
         default: // PREV_ATOM_LIKE_PATTERN:
            start_state = START_PATTERN;
            end_state = END_PATTERN;
            break;
         }

         if (before)
            n = *++p; // get the count

         //The \@= operator: match the preceding atom with zero width.
         //The \@! operator: no match for the preceding atom.
         //The \@<= operator: match for the preceding atom.
         //The \@<! operator: no match for the preceding atom.
         //Surrounds the preceding atom with START_INVISIBLE and END_INVISIBLE, similarly to MOPEN

         e = POP();
         s1 = alloc_state(end_state, NULL, NULL);
         if (s1 == NULL)
            goto theend;

         s = alloc_state(start_state, e.start, s1);
         if (s == NULL)
            goto theend;
         if (pattern) {
            // ZEND -> END_PATTERN -> SKIP -> what follows.
            skip = alloc_state(SKIP, NULL, NULL);
            if (skip == NULL)
                goto theend;
            zend = alloc_state(ZEND, s1, NULL);
            if (zend == NULL)
                goto theend;
            s1->out= skip;
            patch(e.out, zend);
            PUSH(frag(s, list1(&skip->out)));
         } else {
            patch(e.out, s1);
            PUSH(frag(s, list1(&s1->out)));
            if (before) {
               if (n <= 0)
                  // See if we can guess the maximum width, it avoids a lot of pointless tries
                  n = nfa_max_width(e.start, 0);
               s->val = n; // store the count
            }
         }
         break;
      }

      case COMPOSING:   // char with composing char
#if 0
         // TODO
         if (regflags & RF_ICOMBINE) {
            // use the base character only
         }
#endif
         // FALLTHROUGH

      case MOPEN:   // \( \) Submatch
      case MOPEN1:
      case MOPEN2:
      case MOPEN3:
      case MOPEN4:
      case MOPEN5:
      case MOPEN6:
      case MOPEN7:
      case MOPEN8:
      case MOPEN9:
      case ZOPEN:   // \z( \) Submatch, for variables used in hilites
      case ZOPEN1:
      case ZOPEN2:
      case ZOPEN3:
      case ZOPEN4:
      case ZOPEN5:
      case ZOPEN6:
      case ZOPEN7:
      case ZOPEN8:
      case ZOPEN9:
      case NOPEN:   // \%( \) "Invisible Submatch"
         mopen = *p;
         switch (*p) {
         case NOPEN: mclose = NCLOSE; break;
         case ZOPEN: mclose = ZCLOSE; break;
         case ZOPEN1: mclose = ZCLOSE1; break;
         case ZOPEN2: mclose = ZCLOSE2; break;
         case ZOPEN3: mclose = ZCLOSE3; break;
         case ZOPEN4: mclose = ZCLOSE4; break;
         case ZOPEN5: mclose = ZCLOSE5; break;
         case ZOPEN6: mclose = ZCLOSE6; break;
         case ZOPEN7: mclose = ZCLOSE7; break;
         case ZOPEN8: mclose = ZCLOSE8; break;
         case ZOPEN9: mclose = ZCLOSE9; break;
         case COMPOSING: mclose = END_COMPOSING; break;
         default:
             // MOPEN, MOPEN1 .. MOPEN9
             mclose = *p + NSUBEXP;
             break;
         }

         // Allow "MOPEN" as a valid postfix representation for the empty regexp "". In this 
         // case, the NFA will be MOPEN -> MCLOSE. Note that this also allows
         // empty groups of parenthesis, and empty mbyte chars
         if (stackp == stack) {
            s = alloc_state(mopen, NULL, NULL);
            if (s == NULL)
                goto theend;
            s1 = alloc_state(mclose, NULL, NULL);
            if (s1 == NULL)
                goto theend;
            patch(list1(&s->out), s1);
            PUSH(frag(s, list1(&s1->out)));
            break;
         }

         // At least one node was emitted before MOPEN, so at least one node will be between 
         // MOPEN and MCLOSE
         e = POP();
         s = alloc_state(mopen, e.start, NULL);   // `('
         if (s == NULL)
            goto theend;

         s1 = alloc_state(mclose, NULL, NULL);   // `)'
         if (s1 == NULL)
            goto theend;
         patch(e.out, s1);

         if (mopen == COMPOSING)
            // COMPOSING->out1 = END_COMPOSING
            patch(list1(&s->out1), s1);

         PUSH(frag(s, list1(&s1->out)));
         break;

      case BACKREF1:
      case BACKREF2:
      case BACKREF3:
      case BACKREF4:
      case BACKREF5:
      case BACKREF6:
      case BACKREF7:
      case BACKREF8:
      case BACKREF9:
      case ZREF1:
      case ZREF2:
      case ZREF3:
      case ZREF4:
      case ZREF5:
      case ZREF6:
      case ZREF7:
      case ZREF8:
      case ZREF9:
         s = alloc_state(*p, NULL, NULL);
         if (s == NULL)
            goto theend;
         s1 = alloc_state(SKIP, NULL, NULL);
         if (s1 == NULL)
            goto theend;
         patch(list1(&s->out), s1);
         PUSH(frag(s, list1(&s1->out)));
         break;

      case LNUM:
      case LNUM_GT:
      case LNUM_LT:
      case VCOL:
      case VCOL_GT:
      case VCOL_LT:
      case COL:
      case COL_GT:
      case COL_LT:
      case MARK:
      case MARK_GT:
      case MARK_LT: {
         int n = *++p; // lnum, col or mark name

         s = alloc_state(p[-1], NULL, NULL);
         if (s == NULL)
            goto theend;
         s->val = n;
         PUSH(frag(s, list1(&s->out)));
         break;
      }

      case ZSTART:
      case ZEND:
      default:
         // Operands
         s = alloc_state(*p, NULL, NULL);
         if (s == NULL)
            goto theend;
         PUSH(frag(s, list1(&s->out)));
         break;
      } // switch(*p)
   } // for (p = postfix; *p; ++p)

   e = POP();
   if (stackp != stack) {
      eeglFree(stack);
      EMSG_RET_NULL(
         _(e_nfa_regexp_while_converting_from_postfix_to_nfa_too_many_stats_left_on_stack)
      );
   }

   if (stateS >= countStatesS) {
      eeglFree(stack);
      EMSG_RET_NULL(_(e_nfa_regexp_not_enough_space_to_store_whole_nfa));
   }

   matchstate = &state_ptr[stateS]; // the match state
   stateS++;
   matchstate->c = MATCH;
   matchstate->out = matchstate->out1 = NULL;
   matchstate->id = 0;

   patch(e.out, matchstate);
   ret = e.start;

public theend:
   eeglFree(stack);
   return ret;

#undef POP1
#undef PUSH1
#undef POP2
#undef PUSH2
#undef POP
#undef PUSH
}

// After building the NFA program, inspect it to add optimization hints.
private void
addOptimizationHints(RegProg* prog) {
   for (int i = 0; i < prog->nstate; ++i) {
      Unt c = prog->state[i].c;
      if (  c == START_INVISIBLE
         || c == START_INVISIBLE_NEG
         || c == START_INVISIBLE_BEFORE
         || c == START_INVISIBLE_BEFORE_NEG
      ){
         int directly;

         // Do it directly when what follows is possibly the end of the match.
         if (match_follows(prog->state[i].out1->out, 0))
            directly = true;
         else {
            int ch_invisible = failure_chance(prog->state[i].out, 0);
            int ch_follows = failure_chance(prog->state[i].out1->out, 0);

            // Postpone when the invisible match is expensive or has a lower chance of failing.
            if (c == START_INVISIBLE_BEFORE || c == START_INVISIBLE_BEFORE_NEG) {
               // "before" matches are very expensive when
               // unbounded, always prefer what follows then,
               // unless what follows will always match.
               // Otherwise strongly prefer what follows.
               if (prog->state[i].val <= 0 && ch_follows > 0)
                  directly = false;
               else
                  directly = ch_follows * 10 < ch_invisible;
            } else {
               // normal invisible, first do the one with the
               // highest failure chance
               directly = ch_follows < ch_invisible;
            }
         }
         if (directly)
            // switch to the _FIRST state
            ++prog->state[i].c;
      }
   }
}

/////////////////////////////////////////////////////////////////
// NFA execution code.
/////////////////////////////////////////////////////////////////

private typedef struct {
   int in_use; // number of subexpr with useful info

   // When REG_MULTI is true list.multi is used, otherwise list.line.
   union {
      struct multipos {
         LineNr start_lnum;
         LineNr end_lnum;
         ColNr start_col;
         ColNr end_col;
      } multi[NSUBEXP];
      struct linepos {
         Byte* start;
         Byte* end;
      } line[NSUBEXP];
   } list;
   ColNr   orig_start_col;  // list.multi[0].start_col without \zs
} Submatch;

private typedef struct {
   Submatch norm; // \( .. \) matches
   Submatch synt; // \z( .. \) matches
} Submatches;

// PostponedMatch stores a Postponed Invisible Match.
private typedef struct {
   int      result;      // PIM_*, see below
   RState   *state;      // the invisible match start state
   Submatches   subs;      // submatch info, only party used
   union {
      PosNoVirt   pos;
      Byte   *ptr;
   } end;         // where the match must end
} PostponedMatch;

// Values for done in PostponedMatch.
#define PIM_UNUSED   0   // pim not used
#define PIM_TODO     1   // pim not done yet
#define PIM_MATCH    2   // pim executed, matches
#define PIM_NOMATCH  3   // pim executed, no match


// nfa_thread_T contains execution information of a NFA state
private typedef struct {
   RState   *state;
   int      count;
   PostponedMatch   pim;      // if pim.result != PIM_UNUSED: postponed invisible match
   Submatches   subs;      // submatch info, only party used
} nfa_thread_T;

// nfa_List contains the alternative NFA execution states.
private typedef struct {
   nfa_thread_T    *t;      // allocated array of states
   int          n;      // nr of states currently in "t"
   int          len;   // max nr of states in "t"
   int          id;      // ID of the list
   int          has_pim;   // true when any state has a PIM
} nfa_List;

#ifdef REGEXP_LOGGING
private void log_subexpr(Submatch *sub);

private void
log_subsexpr(Submatches *subs) {
    log_subexpr(&subs->norm);
    if (exe.nfa_has_zsubexpr)
   log_subexpr(&subs->synt);
}

private void
log_subexpr(Submatch *sub) {
    int j;

    for (j = 0; j < sub->in_use; j++)
   if (REG_MULTI)
       fprintf(log_fd,
          "*** group %d, start: c=%d, l=%d, end: c=%d, l=%d\n",
          j,
          sub->list.multi[j].start_col,
          (int)sub->list.multi[j].start_lnum,
          sub->list.multi[j].end_col,
          (int)sub->list.multi[j].end_lnum);
   else {
       char *s = (char *)sub->list.line[j].start;
       char *e = (char *)sub->list.line[j].end;

       fprintf(log_fd, "*** group %d, start: \"%s\", end: \"%s\"\n",
          j,
          s == NULL ? "NULL" : s,
          e == NULL ? "NULL" : e);
   }
}

private char *
pim_info(PostponedMatch *pim) {
    static char buf[30];

    if (pim == NULL || pim->result == PIM_UNUSED)
   buf[0] = ZERO;
    else
    {
   sprintf(buf, " PIM col %d", REG_MULTI ? (int)pim->end.pos.col
      : (int)(pim->end.ptr - exe.input));
    }
    return buf;
}

#endif

// Used during execution: whether a match has been found.
private int       nfa_match;
private int      *timedOutS;

private void copy_sub(Submatch *to, Submatch *from);
private int pim_equal(PostponedMatch *one, PostponedMatch *two);

// Copy postponed invisible match info from "from" to "to".
private void
copy_pim(PostponedMatch *to, PostponedMatch *from) {
    to->result = from->result;
    to->state = from->state;
    copy_sub(&to->subs.norm, &from->subs.norm);
    if (exe.nfa_has_zsubexpr)
   copy_sub(&to->subs.synt, &from->subs.synt);
    to->end = from->end;
}

private void
clear_sub(Submatch *sub) {
    if (REG_MULTI)
   // Use 0xff to set lnum to -1
   memset(sub->list.multi, 0xff,
              sizeof(struct multipos) * exe.nfa_nsubexpr);
    else
   memset(sub->list.line, 0,
               sizeof(struct linepos) * exe.nfa_nsubexpr);
    sub->in_use = 0;
}

// Copy the submatches from "from" to "to".
private void
copy_sub(Submatch *to, Submatch *from) {
    to->in_use = from->in_use;
    if (from->in_use <= 0)
   return;

   // Copy the match start and end positions.
   if (REG_MULTI) {
      MEMMOVE(&to->list.multi[0],
         &from->list.multi[0],
         sizeof(struct multipos) * from->in_use);
      to->orig_start_col = from->orig_start_col;
   } else
      MEMMOVE(&to->list.line[0], &from->list.line[0], sizeof(struct linepos) * from->in_use);
}

// Like copy_sub() but exclude the main match.
private void
copy_sub_off(Submatch *to, Submatch *from) {
   if (to->in_use < from->in_use)
      to->in_use = from->in_use;
   if (from->in_use <= 1)
      return;

   // Copy the match start and end positions.
   if (REG_MULTI)
      MEMMOVE(
         &to->list.multi[1], &from->list.multi[1], sizeof(struct multipos) * (from->in_use - 1)
      );
   else
      MEMMOVE(
         &to->list.line[1], &from->list.line[1], sizeof(struct linepos) * (from->in_use - 1)
      );
}

// Like copy_sub() but only do the end of the main match if \ze is present.
private void
copy_ze_off(Submatch *to, Submatch *from) {
   if (!exe.nfa_has_zend)
      return;

   if (REG_MULTI) {
      if (from->list.multi[0].end_lnum >= 0) {
         to->list.multi[0].end_lnum = from->list.multi[0].end_lnum;
         to->list.multi[0].end_col = from->list.multi[0].end_col;
      }
   } else {
      if (from->list.line[0].end != NULL)
         to->list.line[0].end = from->list.line[0].end;
   }
}

// Return true if "sub1" and "sub2" have the same start positions.
// When using back-references also check the end position.
private int
sub_equal(Submatch *sub1, Submatch *sub2) {
   int      i;
   int      todo;
   LineNr   s1;
   LineNr   s2;
   Byte   *sp1;
   Byte   *sp2;

   todo = sub1->in_use > sub2->in_use ? sub1->in_use : sub2->in_use;
   if (REG_MULTI) {
      for (i = 0; i < todo; ++i) {
          if (i < sub1->in_use)
         s1 = sub1->list.multi[i].start_lnum;
          else
         s1 = -1;
          if (i < sub2->in_use)
         s2 = sub2->list.multi[i].start_lnum;
          else
         s2 = -1;
          if (s1 != s2)
         return false;
          if (s1 != -1 && sub1->list.multi[i].start_col
                       != sub2->list.multi[i].start_col)
         return false;

          if (exe.nfa_has_backref) {
         if (i < sub1->in_use)
             s1 = sub1->list.multi[i].end_lnum;
         else
             s1 = -1;
         if (i < sub2->in_use)
             s2 = sub2->list.multi[i].end_lnum;
         else
             s2 = -1;
         if (s1 != s2)
             return false;
         if (s1 != -1 && sub1->list.multi[i].end_col
                         != sub2->list.multi[i].end_col)
         return false;
          }
      }
    } else {
   for (i = 0; i < todo; ++i) {
       if (i < sub1->in_use)
      sp1 = sub1->list.line[i].start;
       else
      sp1 = NULL;
       if (i < sub2->in_use)
      sp2 = sub2->list.line[i].start;
       else
      sp2 = NULL;
       if (sp1 != sp2)
      return false;
       if (exe.nfa_has_backref) {
      if (i < sub1->in_use)
          sp1 = sub1->list.line[i].end;
      else
          sp1 = NULL;
      if (i < sub2->in_use)
          sp2 = sub2->list.line[i].end;
      else
          sp2 = NULL;
      if (sp1 != sp2)
          return false;
       }
   }
    }

    return true;
}

// Check whether we are past the time limit, if there is one.
private int
nfa_did_time_out(void) {
   if (*timeout_flag) {
      if (timedOutS != NULL) {
         if (!*timedOutS)
            lo("NFA regexp timed out");
         *timedOutS = true;
      }
      return true;
   }
   return false;
}

#ifdef REGEXP_LOGGING
private void
open_debug_log(int result) {
    log_fd = fopen(REGEXP_RUN_LOG, "a");
    if (log_fd == NULL) {
   emsg(_(e_log_open_failed));
   log_fd = stderr;
    }

    fprintf(log_fd, "****************************\n");
    fprintf(log_fd, "FINISHED RUNNING match() recursively\n");
    fprintf(log_fd, "MATCH = %s\n", result == true ? "OK" : result == MAYBE
       ? "MAYBE" : "false");
    fprintf(log_fd, "****************************\n");
}

private void
report_state(char *action,
        Submatch *sub,
        RState *state,
        int lid,
        PostponedMatch *pim)
{
    int col;

    if (sub->in_use <= 0)
   col = -1;
    ei (REG_MULTI)
   col = sub->list.multi[0].start_col;
    else
   col = (int)(sub->list.line[0].start - exe.line);
    nfa_set_code(state->c);
    if (log_fd == NULL)
   open_debug_log(MAYBE);

    fprintf(log_fd, "> %s state %d to list %d. char %d: %s (start col %d)%s\n",
       action, abs(state->id), lid, state->c, code, col,
       pim_info(pim));
}
#endif

// Return true if the same state is already in list "l" with the same positions as "subs".
private int
has_state_with_pos(
    nfa_List      *l,   // runtime state list
    RState      *state,   // state to update
    Submatches      *subs,   // pointers to subexpressions
    PostponedMatch      *pim)   // postponed match or NULL
{
    nfa_thread_T   *thread;
    int         i;

    for (i = 0; i < l->n; ++i) {
   thread = &l->t[i];
   if (thread->state->id == state->id
      && sub_equal(&thread->subs.norm, &subs->norm)
      && (!exe.nfa_has_zsubexpr
            || sub_equal(&thread->subs.synt, &subs->synt))
      && pim_equal(&thread->pim, pim))
       return true;
    }
    return false;
}

// Return true if "one" and "two" are equal.  That includes when both are not set.
private int
pim_equal(PostponedMatch *one, PostponedMatch *two) {
   int one_unused = (one == NULL || one->result == PIM_UNUSED);
   int two_unused = (two == NULL || two->result == PIM_UNUSED);

   if (one_unused)
   // one is unused: equal when two is also unused
   return two_unused;
    if (two_unused)
   // one is used and two is not: not equal
   return false;
    // compare the state id
    if (one->state->id != two->state->id)
   return false;
    // compare the position
    if (REG_MULTI)
   return one->end.pos.lnum == two->end.pos.lnum
       && one->end.pos.col == two->end.pos.col;
    return one->end.ptr == two->end.ptr;
}

// Return true if "state" leads to a MATCH without advancing the input.
private int
match_follows(RState *startstate, int depth) {
   RState       *state = startstate;

   // avoid too much recursion
   if (depth > 10)
      return false;

   while (state != NULL) {
      switch (state->c) {
         case MATCH:
         case MCLOSE:
         case END_INVISIBLE:
         case END_INVISIBLE_NEG:
         case END_PATTERN:
            return true;

         case SPLIT:
            return match_follows(state->out, depth + 1) || match_follows(state->out1, depth + 1);

         case START_INVISIBLE:
         case START_INVISIBLE_FIRST:
         case START_INVISIBLE_BEFORE:
         case START_INVISIBLE_BEFORE_FIRST:
         case START_INVISIBLE_NEG:
         case START_INVISIBLE_NEG_FIRST:
         case START_INVISIBLE_BEFORE_NEG:
         case START_INVISIBLE_BEFORE_NEG_FIRST:
         case COMPOSING:
            // skip ahead to next state
            state = state->out1->out;
            continue;

         case ANY:
         case ANY_COMPOSING:
         case IDENT:
         case SIDENT:
         case KWORD:
         case SKWORD:
         case FNAME:
         case SFNAME:
         case PRINT:
         case SPRINT:
         case WHITE:
         case NWHITE:
         case DIGIT:
         case NDIGIT:
         case HEX:
         case NHEX:
         case WORD:
         case NWORD:
         case HEAD:
         case NHEAD:
         case ALPHA:
         case NALPHA:
         case LOWER:
         case NLOWER:
         case UPPER:
         case NUPPER:
         case LOWER_IC:
         case NLOWER_IC:
         case UPPER_IC:
         case NUPPER_IC:
         case START_COLL:
         case START_NEG_COLL:
         case NEWL:
            // state will advance input
            return false;

         default:
            if (state->c > 0)
                // state will advance input
                return false;

            // Others: zero-width or possibly zero-width, might still find
            // a match at the same position, keep looking.
            break;
      }
      state = state->out;
   }
   return false;
}


// Return true if "state" is already in list "l".
private int
state_in_list(
    nfa_List      *l,   // runtime state list
    RState      *state,   // state to update
    Submatches      *subs   // pointers to subexpressions
){
   if (state->lastlist[nfa_ll_index] == l->id) {
      if (!exe.nfa_has_backref || has_state_with_pos(l, state, subs, NULL))
         return true;
   }
   return false;
}

// Offset used for "off" by addstate_here().
#define ADDSTATE_HERE_OFFSET 10

//Add "state" and possibly what follows to state list ".".
//Return "subs_arg", possibly copied into temp_subs.
//Return NULL when recursiveness is too deep or timed out.
private Submatches *
addstate(
   nfa_List      *l,       // runtime state list
   RState      *state,       // state to update
   Submatches      *subs_arg,  // pointers to subexpressions
   PostponedMatch      *pim,       // postponed look-behind match
   int         off_arg    // byte offset, when -1 go to next line
){
   int         subidx;
   int         off = off_arg;
   int         add_here = false;
   int         listindex = 0;
   int         k;
   int         found = false;
   nfa_thread_T   *thread;
   struct multipos   save_multipos;
   int         save_in_use;
   Byte      *save_ptr;
   int         i;
   Submatch      *sub;
   Submatches      *subs = subs_arg;
   static Submatches   temp_subs;
#ifdef REGEXP_LOGGING
   int         did_print = false;
#endif
   static int      depth = 0;

   if (nfa_did_time_out())
      return NULL;

   //This function is called recursively.  When the depth is too much we run
   //out of stack and crash, limit recursiveness here.
   if (++depth >= 5000 || subs == NULL) {
      --depth;
      return NULL;
   }

   if (off_arg <= -ADDSTATE_HERE_OFFSET) {
      add_here = true;
      off = 0;
      listindex = -(off_arg + ADDSTATE_HERE_OFFSET);
   }

   switch (state->c) {
   case NCLOSE:
   case MCLOSE:
   case MCLOSE1:
   case MCLOSE2:
   case MCLOSE3:
   case MCLOSE4:
   case MCLOSE5:
   case MCLOSE6:
   case MCLOSE7:
   case MCLOSE8:
   case MCLOSE9:
   case ZCLOSE:
   case ZCLOSE1:
   case ZCLOSE2:
   case ZCLOSE3:
   case ZCLOSE4:
   case ZCLOSE5:
   case ZCLOSE6:
   case ZCLOSE7:
   case ZCLOSE8:
   case ZCLOSE9:
   case MOPEN:
   case ZEND:
   case SPLIT:
   case EMPTY:
      // These nodes are not added themselves but their "out" and/or "out1" may be added below
      break;

   case BOL:
   case BOF:
      // "^" won't match past end-of-line, don't bother trying. Except when at the end of the 
      // line, or when we are going to the next line for a look-behind match.
      if (exe.input > exe.line
             && *exe.input != ZERO
             && (mustEndAtS == NULL || !REG_MULTI || exe.lnum == mustEndAtS->se_u.pos.lnum))
         goto skip_add;
      // FALLTHROUGH

   case MOPEN1:
   case MOPEN2:
   case MOPEN3:
   case MOPEN4:
   case MOPEN5:
   case MOPEN6:
   case MOPEN7:
   case MOPEN8:
   case MOPEN9:
   case ZOPEN:
   case ZOPEN1:
   case ZOPEN2:
   case ZOPEN3:
   case ZOPEN4:
   case ZOPEN5:
   case ZOPEN6:
   case ZOPEN7:
   case ZOPEN8:
   case ZOPEN9:
   case NOPEN:
   case ZSTART:
       // These nodes need to be added so that we can bail out when it was added to this list 
       // before at the same position to avoid an endless loop for "\(\)*"

   default:
      if (state->lastlist[nfa_ll_index] == l->id && state->c != SKIP) {
      // This state is already in the list, don't add it again, unless it is an MOPEN that is 
      // used for a backreference or when there is a PIM. For MATCH check the position,
      // lower position is preferred.
      if (!exe.nfa_has_backref && pim == NULL && !l->has_pim && state->c != MATCH) {
         // When called from addstate_here() do insert before existing states.
         if (add_here) {
            for (k = 0; k < l->n && k < listindex; ++k)
               if (l->t[k].state->id == state->id) {
                  found = true;
                  break;
               }
         }
         if (!add_here || found) {
   skip_add:
#ifdef REGEXP_LOGGING
            nfa_set_code(state->c);
            fprintf(log_fd, 
               "> Not adding state %d to list %d. char %d: %s pim: %s has_pim: %d found: %d\n",
               abs(state->id), l->id, state->c, code,
               pim == NULL ? "NULL" : "yes", l->has_pim, found
            );
#endif
            --depth;
            return subs;
         }
      }

      // Do not add the state again when it exists with the same positions.
      if (has_state_with_pos(l, state, subs, pim))
         goto skip_add;
      }

      // When there are backreferences or PIMs the number of states may be (a lot) bigger than 
      // anticipated.
      if (l->n == l->len) {
         int      newlen = l->len * 3 / 2 + 50;
         Unt      newsize = newlen * sizeof(nfa_thread_T);
         nfa_thread_T   *newt;

         if ((long)(newsize >> 10) >= p_mmp) {
             emsg(_(e_pattern_uses_more_memory_than_maxmempattern));
             --depth;
             return NULL;
         }
         if (subs != &temp_subs) {
            // "subs" may point into the current array, need to make a copy 'fore it becomes invalid
            copy_sub(&temp_subs.norm, &subs->norm);
            if (exe.nfa_has_zsubexpr)
               copy_sub(&temp_subs.synt, &subs->synt);
            subs = &temp_subs;
         }

         newt = eeRealloc(l->t, newsize);
         l->t = newt;
         l->len = newlen;
      }

      // add the state to the list
      state->lastlist[nfa_ll_index] = l->id;
      thread = &l->t[l->n++];
      thread->state = state;
      if (pim == NULL)
         thread->pim.result = PIM_UNUSED;
      else {
         copy_pim(&thread->pim, pim);
         l->has_pim = true;
      }
      copy_sub(&thread->subs.norm, &subs->norm);
      if (exe.nfa_has_zsubexpr)
         copy_sub(&thread->subs.synt, &subs->synt);
#ifdef REGEXP_LOGGING
      report_state("Adding", &thread->subs.norm, state, l->id, pim);
      did_print = true;
#endif
   }

#ifdef REGEXP_LOGGING
   if (!did_print)
      report_state("Processing", &subs->norm, state, l->id, pim);
#endif
   switch (state->c) {
   case MATCH:
       break;

   case SPLIT:
       // order matters here
       subs = addstate(l, state->out, subs, pim, off_arg);
       subs = addstate(l, state->out1, subs, pim, off_arg);
       break;

   case EMPTY:
   case NOPEN:
   case NCLOSE:
       subs = addstate(l, state->out, subs, pim, off_arg);
       break;

   case MOPEN:
   case MOPEN1:
   case MOPEN2:
   case MOPEN3:
   case MOPEN4:
   case MOPEN5:
   case MOPEN6:
   case MOPEN7:
   case MOPEN8:
   case MOPEN9:
   case ZOPEN:
   case ZOPEN1:
   case ZOPEN2:
   case ZOPEN3:
   case ZOPEN4:
   case ZOPEN5:
   case ZOPEN6:
   case ZOPEN7:
   case ZOPEN8:
   case ZOPEN9:
   case ZSTART:
      if (state->c == ZSTART) {
        subidx = 0;
        sub = &subs->norm;
      } ei (state->c >= ZOPEN && state->c <= ZOPEN9) {
        subidx = state->c - ZOPEN;
        sub = &subs->synt;
      }
      else {
        subidx = state->c - MOPEN;
        sub = &subs->norm;
     }

     // avoid compiler warnings
     save_ptr = NULL;
     CLEAR_FIELD(save_multipos);

     // Set the position (with "off" added) in the subexpression.  Save
     // and restore it when it was in use.  Otherwise fill any gap.
     if (REG_MULTI) {
        if (subidx < sub->in_use) {
            save_multipos = sub->list.multi[subidx];
            save_in_use = -1;
        } else {
           save_in_use = sub->in_use;
           for (i = sub->in_use; i < subidx; ++i) {
              sub->list.multi[i].start_lnum = -1;
              sub->list.multi[i].end_lnum = -1;
           }
           sub->in_use = subidx + 1;
        }
        if (off == -1) {
           sub->list.multi[subidx].start_lnum = exe.lnum + 1;
           sub->list.multi[subidx].start_col = 0;
        } else {
           sub->list.multi[subidx].start_lnum = exe.lnum;
           sub->list.multi[subidx].start_col = (ColNr)(exe.input - exe.line + off);
        }
        sub->list.multi[subidx].end_lnum = -1;
      } else {
         if (subidx < sub->in_use) {
             save_ptr = sub->list.line[subidx].start;
             save_in_use = -1;
         } else {
            save_in_use = sub->in_use;
            for (i = sub->in_use; i < subidx; ++i) {
               sub->list.line[i].start = NULL;
               sub->list.line[i].end = NULL;
            }
            sub->in_use = subidx + 1;
         }
         sub->list.line[subidx].start = exe.input + off;
      }

       subs = addstate(l, state->out, subs, pim, off_arg);
       if (subs == NULL)
      break;
       // "subs" may have changed, need to set "sub" again
       if (state->c >= ZOPEN && state->c <= ZOPEN9)
      sub = &subs->synt;
       else
      sub = &subs->norm;

      if (save_in_use == -1) {
         if (REG_MULTI)
            sub->list.multi[subidx] = save_multipos;
         else
            sub->list.line[subidx].start = save_ptr;
      } else
         sub->in_use = save_in_use;
      break;

   case MCLOSE:
       if (exe.nfa_has_zend && (REG_MULTI
         ? subs->norm.list.multi[0].end_lnum >= 0
         : subs->norm.list.line[0].end != NULL))
       {
      // Do not overwrite the position set by \ze.
      subs = addstate(l, state->out, subs, pim, off_arg);
      break;
       }
       // FALLTHROUGH
   case MCLOSE1:
   case MCLOSE2:
   case MCLOSE3:
   case MCLOSE4:
   case MCLOSE5:
   case MCLOSE6:
   case MCLOSE7:
   case MCLOSE8:
   case MCLOSE9:
   case ZCLOSE:
   case ZCLOSE1:
   case ZCLOSE2:
   case ZCLOSE3:
   case ZCLOSE4:
   case ZCLOSE5:
   case ZCLOSE6:
   case ZCLOSE7:
   case ZCLOSE8:
   case ZCLOSE9:
   case ZEND:
      if (state->c == ZEND) {
         subidx = 0;
         sub = &subs->norm;
      } ei (state->c >= ZCLOSE && state->c <= ZCLOSE9) {
         subidx = state->c - ZCLOSE;
         sub = &subs->synt;
      } else {
         subidx = state->c - MCLOSE;
         sub = &subs->norm;
      }

       // We don't fill in gaps here, there must have been an MOPEN that
       // has done that.
      save_in_use = sub->in_use;
      if (sub->in_use <= subidx)
         sub->in_use = subidx + 1;
       if (REG_MULTI) {
      save_multipos = sub->list.multi[subidx];
      if (off == -1) {
          sub->list.multi[subidx].end_lnum = exe.lnum + 1;
          sub->list.multi[subidx].end_col = 0;
      } else {
          sub->list.multi[subidx].end_lnum = exe.lnum;
          sub->list.multi[subidx].end_col =
                 (ColNr)(exe.input - exe.line + off);
      }
      // avoid compiler warnings
      save_ptr = NULL;
      } else {
         save_ptr = sub->list.line[subidx].end;
         sub->list.line[subidx].end = exe.input + off;
         // avoid compiler warnings
         CLEAR_FIELD(save_multipos);
      }

      subs = addstate(l, state->out, subs, pim, off_arg);
      if (!subs)
         break;
      // "subs" may have changed, need to set "sub" again
      if (state->c >= ZCLOSE && state->c <= ZCLOSE9)
         sub = &subs->synt;
      else
         sub = &subs->norm;

      if (REG_MULTI)
         sub->list.multi[subidx] = save_multipos;
      else
         sub->list.line[subidx].end = save_ptr;
      sub->in_use = save_in_use;
      break;
    }
    --depth;
    return subs;
}

// Like addstate(), but the new state(s) are put at position "*ip".
// Used for zero-width matches, next state to use is the added one.
// This makes sure the order of states to be tried does not change, which
// matters for alternatives.
private Submatches *
addstate_here(
   nfa_List      *l,   // runtime state list
   RState      *state,   // state to update
   Submatches      *subs,   // pointers to subexpressions
   PostponedMatch      *pim,   // postponed look-behind match
   int         *ip)
{
   int tlen = l->n;
   int count;
   int listidx = *ip;
   Submatches *r;

   // First add the state(s) at the end, so that we know how many there are.
   // Pass the listidx as offset (avoids adding another argument to addstate()).
   r = addstate(l, state, subs, pim, -listidx - ADDSTATE_HERE_OFFSET);
   if (r == NULL)
      return NULL;

   // when "*ip" was at the end of the list, nothing to do
   if (listidx + 1 == tlen)
      return r;

   // re-order to put the new state at the current position
   count = l->n - tlen;
   if (count == 0)
      return r; // no state got added
   if (count == 1) {
      // overwrite the current state
      l->t[listidx] = l->t[l->n - 1];
   } ei (count > 1) {
      if (l->n + count - 1 >= l->len) {
         // not enough space to move the new states, reallocate the list
         // and move the states to the right position
         int          newlen = l->len * 3 / 2 + 50;
         Unt       newsize = newlen * sizeof(nfa_thread_T);
         nfa_thread_T    *newl;

         if ((long)(newsize >> 10) >= p_mmp) {
            emsg(_(e_pattern_uses_more_memory_than_maxmempattern));
            return NULL;
         }
         newl = alloc(newsize);
         l->len = newlen;
         MEMMOVE(&(newl[0]),
            &(l->t[0]),
            sizeof(nfa_thread_T) * listidx);
         MEMMOVE(&(newl[listidx]),
            &(l->t[l->n - count]),
            sizeof(nfa_thread_T) * count);
         MEMMOVE(&(newl[listidx + count]),
            &(l->t[listidx + 1]),
            sizeof(nfa_thread_T) * (l->n - count - listidx - 1));
         eeglFree(l->t);
         l->t = newl;
      } else {
          // make space for new states, then move them from the end to the current position
          MEMMOVE(&(l->t[listidx + count]),
             &(l->t[listidx + 1]),
             sizeof(nfa_thread_T) * (l->n - listidx - 1));
          MEMMOVE(&(l->t[listidx]),
             &(l->t[l->n - 1]),
             sizeof(nfa_thread_T) * count);
      }
    }
    --l->n;
    *ip = listidx - 1;

    return r;
}

// Check character class "class" against current character c.
private int
check_char_class(int class, int c) {
   switch (class) {
   case CHAR_CLASS_ALNUM:
      if (c >= 1 && c < 128 && isalnum(c))
         return OK;
      break;
   case CHAR_CLASS_ALPHA:
      if (c >= 1 && c < 128 && isalpha(c))
         return OK;
      break;
   case CHAR_CLASS_BLANK:
      if (c == ' ' || c == '\t')
         return OK;
      break;
   case CHAR_CLASS_CNTRL:
      if (c >= 1 && c <= 127 && iscntrl(c))
         return OK;
      break;
   case CHAR_CLASS_DIGIT:
      if (EE_ISDIGIT(c))
         return OK;
      break;
   case CHAR_CLASS_GRAPH:
      if (c >= 1 && c <= 127 && isgraph(c))
         return OK;
      break;
   case CHAR_CLASS_LOWER:
      if (MB_ISLOWER(c) && c != 170 && c != 186)
         return OK;
      break;
   case CHAR_CLASS_PRINT:
      if (bookIsCharPrintable(c))
         return OK;
      break;
   case CHAR_CLASS_PUNCT:
      if (c >= 1 && c < 128 && ispunct(c))
         return OK;
      break;
   case CHAR_CLASS_SPACE:
       if ((c >= 9 && c <= 13) || (c == ' '))
      return OK;
       break;
   case CHAR_CLASS_UPPER:
       if (MB_ISUPPER(c))
      return OK;
       break;
   case CHAR_CLASS_XDIGIT:
       if (eeIsXDigit(c))
      return OK;
       break;
   case CHAR_CLASS_TAB:
       if (c == '\t')
      return OK;
       break;
   case CHAR_CLASS_RETURN:
       if (c == '\r')
      return OK;
       break;
   case CHAR_CLASS_BACKSPACE:
       if (c == '\b')
      return OK;
       break;
   case CHAR_CLASS_ESCAPE:
       if (c == '\033')
      return OK;
       break;
   case CHAR_CLASS_IDENT:
      if (eeIsIdentifierChar(c))
         return OK;
      break;
   case CHAR_CLASS_KEYWORD:
      if (reg_iswordc(c))
         return OK;
      break;
   case CHAR_CLASS_FNAME:
      if (eeIsFnameChar(c))
         return OK;
      break;

   default:
      // should not be here :P
      internalErrFmtMsg(_(e_nfa_regexp_invalid_character_class_nr), class);
      return FAIL;
   }
   return FAIL;
}

// Check for a match with subexpression "subidx". Return true if it matches.
private int
match_backref(
   Submatch   *sub,       // pointers to subexpressions
   int      subidx,
   int      *bytelen)   // out: length of match in bytes
{
   int      len;

   if (sub->in_use <= subidx) {
public retempty:
      // backref was not set, match an empty string
      *bytelen = 0;
      return true;
   }

   if (REG_MULTI) {
      if (sub->list.multi[subidx].start_lnum < 0 || sub->list.multi[subidx].end_lnum < 0)
         goto retempty;
      if (sub->list.multi[subidx].start_lnum == exe.lnum 
         && sub->list.multi[subidx].end_lnum == exe.lnum
      ){
         len = sub->list.multi[subidx].end_col - sub->list.multi[subidx].start_col;
         if (cstrncmp(exe.line + sub->list.multi[subidx].start_col, exe.input, &len) == 0) {
            *bytelen = len;
            return true;
         }
      } else {
         if (match_with_backref(
            sub->list.multi[subidx].start_lnum,
            sub->list.multi[subidx].start_col,
            sub->list.multi[subidx].end_lnum,
            sub->list.multi[subidx].end_col,
            bytelen) == RA_MATCH)
         return true;
      }
   } else {
      if (sub->list.line[subidx].start == NULL || sub->list.line[subidx].end == NULL)
         goto retempty;
      len = (int)(sub->list.line[subidx].end - sub->list.line[subidx].start);
      if (cstrncmp(sub->list.line[subidx].start, exe.input, &len) == 0) {
         *bytelen = len;
         return true;
      }
   }
   return false;
}


// Check for a match with \z subexpression "subidx". Return true if it matches.
private int
match_zref(
   int      subidx,
   int      *bytelen)   // out: length of match in bytes
{
   int      len;

   cleanup_zsubexpr();
   if (re_extmatch_in == NULL || re_extmatch_in->matches[subidx] == NULL) {
      // backref was not set, match an empty string
      *bytelen = 0;
      return true;
   }

   len = (int)STRLEN(re_extmatch_in->matches[subidx]);
   if (cstrncmp(re_extmatch_in->matches[subidx], exe.input, &len) == 0) {
      *bytelen = len;
      return true;
   }
   return false;
}

// Save list IDs for all NFA states of "prog" into "list". Also reset the IDs to zero.
// Only used for the recursive value lastlist[1].
private void
nfa_save_listids(RegProg* prog, int *list) {
   // Order in the list is reverse, it's a bit faster that way.
   RState* p = &prog->state[0];
   for (int i = prog->nstate; --i >= 0; ) {
      list[i] = p->lastlist[1];
      p->lastlist[1] = 0;
      ++p;
   }
}

// Restore list IDs from "list" to all NFA states.
private void
nfa_restore_listids(RegProg* prog, int *list) {
   RState* p = &prog->state[0];
   for (int i = prog->nstate; --i >= 0; ) {
      p->lastlist[1] = list[i];
      ++p;
   }
}

private int
nfa_re_num_cmp(Ulong val, int op, Ulong pos) {
   if (op == 1) return pos > val;
   if (op == 2) return pos < val;
   return val == pos;
}

private int match(RegProg* prog, RState *start, Submatches *submatch, Submatches *m);

// Recursively call match()
// "pim" is NULL or contains info about a Postponed Invisible Match (start position).
private int
recursiveMatch(
   RState* state,
   PostponedMatch* pim,
   RegProg* prog,
   Submatches* submatch,
   Submatches* m,
   int** listids,
   int* listids_len
){
   int save_reginput_col = (int)(exe.input - exe.line);
   int save_reglnum = exe.lnum;
   int save_nfa_match = nfa_match;
   int save_nfa_listid = exe.nfa_listid;
   StartEnd* save_mustEndAtS = mustEndAtS;
   StartEnd   endpos;
   StartEnd* endposp = NULL;
   int      result;
   int      need_restore = false;

   if (pim != NULL) {
      // start at the position where the postponed match was
      if (REG_MULTI)
         exe.input = exe.line + pim->end.pos.col;
      else
         exe.input = pim->end.ptr;
   }

   if (state->c == START_INVISIBLE_BEFORE
       || state->c == START_INVISIBLE_BEFORE_FIRST
       || state->c == START_INVISIBLE_BEFORE_NEG
       || state->c == START_INVISIBLE_BEFORE_NEG_FIRST
   ) {
      // The recursive match must end at the current position. When "pim" is
      // not NULL it specifies the current position.
      endposp = &endpos;
      if (REG_MULTI) {
         if (pim == NULL) {
            endpos.se_u.pos.col = (int)(exe.input - exe.line);
            endpos.se_u.pos.lnum = exe.lnum;
         } else
            endpos.se_u.pos = pim->end.pos;
      } else {
         if (pim == NULL)
            endpos.se_u.ptr = exe.input;
         else
            endpos.se_u.ptr = pim->end.ptr;
      }

      // Go back the specified number of bytes, or as far as the
      // start of the previous line, to try matching "\@<=" or
      // not matching "\@<!". This is very inefficient, limit the number of
      // bytes if possible.
      if (state->val <= 0) {
         if (REG_MULTI) {
            exe.line = reg_getline(--exe.lnum);
            if (exe.line == NULL)
                // can't go before the first line
                exe.line = reg_getline(++exe.lnum);
         }
         exe.input = exe.line;
      } else {
         if (REG_MULTI && (int)(exe.input - exe.line) < state->val) {
            // Not enough bytes in this line, go to end of
            // previous line.
            exe.line = reg_getline(--exe.lnum);
            if (exe.line == NULL) {
               // can't go before the first line
               exe.line = reg_getline(++exe.lnum);
               exe.input = exe.line;
            } else
               exe.input = exe.line + reg_getline_len(exe.lnum);
         }
         if ((int)(exe.input - exe.line) >= state->val) {
            exe.input -= state->val;
            exe.input -= mb_head_off(exe.line, exe.input);
         } else {
            exe.input = exe.line;
         }
      }
   }

#ifdef REGEXP_LOGGING
   if (log_fd != stderr)
      fclose(log_fd);
   log_fd = NULL;
#endif
   // Have to clear the lastlist field of the NFA nodes, so that
   // match() and addstate() can run properly after recursion.
   if (nfa_ll_index == 1) {
      // Already calling match() recursively.  Save the lastlist[1]
      // values and clear them.
      if (*listids == NULL || *listids_len < prog->nstate) {
         eeglFree(*listids);
         *listids = ALLOC_MULT(int, prog->nstate);
         *listids_len = prog->nstate;
      }
      nfa_save_listids(prog, *listids);
      need_restore = true;
      // any value of exe.nfa_listid will do
   } else {
      // First recursive match() call, switch to the second lastlist
      // entry.  Make sure exe.nfa_listid is different from a previous
      // recursive call, because some states may still have this ID.
      ++nfa_ll_index;
      if (exe.nfa_listid <= exe.nfa_alt_listid)
         exe.nfa_listid = exe.nfa_alt_listid;
   }

   // Call match() to check if the current concat matches at this
   // position. The concat ends with the node END_INVISIBLE
   mustEndAtS = endposp;
   result = match(prog, state->out, submatch, m);

   if (need_restore)
      nfa_restore_listids(prog, *listids);
   else {
      --nfa_ll_index;
      exe.nfa_alt_listid = exe.nfa_listid;
   }

    // restore position in input text
   exe.lnum = save_reglnum;
   if (REG_MULTI)
      exe.line = reg_getline(exe.lnum);
   exe.input = exe.line + save_reginput_col;
   if (result != TOO_EXPENSIVE) {
      nfa_match = save_nfa_match;
      exe.nfa_listid = save_nfa_listid;
   }
   mustEndAtS = save_mustEndAtS;

#ifdef REGEXP_LOGGING
   open_debug_log(result);
#endif

   return result;
}

// Estimate the chance of a match with "state" failing.
// empty match: 0
// ANY: 1
// specific character: 99
private int
failure_chance(RState *state, int depth) {
   int c = state->c;
   int l, r;

   // detect looping
   if (depth > 4)
      return 1;

   switch (c) {
   case SPLIT:
      if (state->out->c == SPLIT || state->out1->c == SPLIT)
         // avoid recursive stuff
         return 1;
      // two alternatives, use the lowest failure chance
      l = failure_chance(state->out, depth + 1);
      r = failure_chance(state->out1, depth + 1);
      return l < r ? l : r;

   case ANY:
      // matches anything, unlikely to fail
      return 1;

   case MATCH:
   case MCLOSE:
   case ANY_COMPOSING:
       // empty match works always
       return 0;

   case START_INVISIBLE:
   case START_INVISIBLE_FIRST:
   case START_INVISIBLE_NEG:
   case START_INVISIBLE_NEG_FIRST:
   case START_INVISIBLE_BEFORE:
   case START_INVISIBLE_BEFORE_FIRST:
   case START_INVISIBLE_BEFORE_NEG:
   case START_INVISIBLE_BEFORE_NEG_FIRST:
   case START_PATTERN:
       // recursive regmatch is expensive, use low failure chance
       return 5;

   case BOL:
   case EOL:
   case BOF:
   case EOFF:
   case NEWL:
      return 99;

   case BOW:
   case EOW:
      return 90;

   case MOPEN:
   case MOPEN1:
   case MOPEN2:
   case MOPEN3:
   case MOPEN4:
   case MOPEN5:
   case MOPEN6:
   case MOPEN7:
   case MOPEN8:
   case MOPEN9:
   case ZOPEN:
   case ZOPEN1:
   case ZOPEN2:
   case ZOPEN3:
   case ZOPEN4:
   case ZOPEN5:
   case ZOPEN6:
   case ZOPEN7:
   case ZOPEN8:
   case ZOPEN9:
   case ZCLOSE:
   case ZCLOSE1:
   case ZCLOSE2:
   case ZCLOSE3:
   case ZCLOSE4:
   case ZCLOSE5:
   case ZCLOSE6:
   case ZCLOSE7:
   case ZCLOSE8:
   case ZCLOSE9:
   case NOPEN:
   case MCLOSE1:
   case MCLOSE2:
   case MCLOSE3:
   case MCLOSE4:
   case MCLOSE5:
   case MCLOSE6:
   case MCLOSE7:
   case MCLOSE8:
   case MCLOSE9:
   case NCLOSE:
      return failure_chance(state->out, depth + 1);

   case BACKREF1:
   case BACKREF2:
   case BACKREF3:
   case BACKREF4:
   case BACKREF5:
   case BACKREF6:
   case BACKREF7:
   case BACKREF8:
   case BACKREF9:
   case ZREF1:
   case ZREF2:
   case ZREF3:
   case ZREF4:
   case ZREF5:
   case ZREF6:
   case ZREF7:
   case ZREF8:
   case ZREF9:
       // backreferences don't match in many places
       return 94;

   case LNUM_GT:
   case LNUM_LT:
   case COL_GT:
   case COL_LT:
   case VCOL_GT:
   case VCOL_LT:
   case MARK_GT:
   case MARK_LT:
   case VISUAL:
       // before/after positions don't match very often
       return 85;

   case LNUM:
       return 90;

   case CURSOR:
   case COL:
   case VCOL:
   case MARK:
       // specific positions rarely match
       return 98;

   case COMPOSING:
       return 95;

   default:
       if (c > 0)
      // character match fails often
      return 95;
   }

   // something else, includes character classes
   return 50;
}

// Skip until the char "c" we know a match must start with.
private int
skip_to_start(int c, ColNr *colp) {
   Byte *s;

   s = cstrchr(exe.line + *colp, c);
   if (s == NULL)
      return FAIL;
   *colp = (int)(s - exe.line);
   return OK;
}

// Check for a match with input. Called after skip_to_start() has found regstart.
// Return 0 for no match, 1 for a match.
private long
find_input(ColNr *startcol, int regstart, Byte *input) {
   ColNr col = *startcol;
   int       c1, c2;
   int       match;

   for (;;) {
      match = true;
      // skip regstart
      Unt len2 = MB_CHAR2LEN(regstart);
      if (len2 > 1 && MB_CHAR2LEN(mb_ptr2char(exe.line + col)) != len2)
          // because of case-folding of the previously matched text, we may need
          // to skip fewer bytes than mb_char2len(regstart)
          len2 = mb_char2len(utf_fold(regstart));
      for (Unt len1 = 0; input[len1] != ZERO; len1 += MB_CHAR2LEN(c1)) {
         c1 = mb_ptr2char(input + len1);
         c2 = mb_ptr2char(exe.line + col + len2);
         if (c1 != c2 && (!exe.reg_ic || MB_CASEFOLD(c1) != MB_CASEFOLD(c2))) {
            match = false;
            break;
         }
         len2 += utf_ptr2len(exe.line + col + len2);
      }
      if (match
         // check that no composing char follows
         && !(utf_iscomposing(mb_ptr2char(exe.line + col + len2)))
      ){
         cleanup_subexpr();
         if (REG_MULTI) {
            exe.reg_startpos[0].lnum = exe.lnum;
            exe.reg_startpos[0].col = col;
            exe.reg_endpos[0].lnum = exe.lnum;
            exe.reg_endpos[0].col = col + len2;
         } else {
            exe.reg_startp[0] = exe.line + col;
            exe.reg_endp[0] = exe.line + col + len2;
         }
         *startcol = col;
         return 1L;
      }

      // Try finding regstart after the current match.
      col += MB_CHAR2LEN(regstart); // skip regstart
      if (skip_to_start(regstart, &col) == FAIL)
          break;
   }

   *startcol = col;
   return 0L;
}

// Main matching routine.
//
// Run NFA to determine whether it matches exe.input.
//
// When "mustEndAtS" is not NULL it is a required end-of-match position.
//
// Return true if there is a match, false if there is no match,
// TOO_EXPENSIVE if we end up with too many states.
// When there is a match "submatch" contains the positions.
//
// Note: Caller must ensure that: start != NULL.
private int
match(
   RegProg* prog,
   RState* start,
   Submatches* submatch,
   Submatches* m
){
   int      result = false;
   Unt   size = 0;
   int      flag = 0;
   int      go_to_nextline = false;
   nfa_thread_T *t;
   nfa_List   list[2];
   int      listidx;
   nfa_List   *thislist;
   nfa_List   *nextlist;
   int      *listids = NULL;
   int      listids_len = 0;
   RState *add_state;
   int      add_here;
   int      add_count;
   int      add_off = 0;
   int      toplevel = start->c == MOPEN;
   Submatches   *r;
#ifdef REGEXP_DEBUG_LOG
   FILE   *debug;
#endif

   // Some patterns may take a long time to match, especially when using
   // recursiveMatch(). Allow interrupting them with CTRL-C.
   fast_breakcheck();
   if (gotInterruptG)
      return false;
   if (nfa_did_time_out())
      return false;

#ifdef REGEXP_DEBUG_LOG
   debug = fopen(REGEXP_DEBUG_LOG, "a");
   if (debug == NULL) {
      showErrFmtMsg("(NFA) COULD NOT OPEN %s!", REGEXP_DEBUG_LOG);
      return false;
   }
#endif
   nfa_match = false;

   // Allocate memory for the lists of nodes.
   size = (prog->nstate + 1) * sizeof(nfa_thread_T);

   list[0].t = alloc(size);
   list[0].len = prog->nstate + 1;
   list[1].t = alloc(size);
   list[1].len = prog->nstate + 1;
   if (list[0].t == NULL || list[1].t == NULL)
      goto theend;

#ifdef REGEXP_LOGGING
   log_fd = fopen(REGEXP_RUN_LOG, "a");
   if (log_fd == NULL) {
      emsg(_(e_log_open_failed));
      log_fd = stderr;
   }
   fprintf(log_fd, "**********************************\n");
   nfa_set_code(start->c);
   fprintf(log_fd, " RUNNING match() starting with state %d, code %s\n",
   abs(start->id), code);
   fprintf(log_fd, "**********************************\n");
#endif

    thislist = &list[0];
    thislist->n = 0;
    thislist->has_pim = false;
    nextlist = &list[1];
    nextlist->n = 0;
    nextlist->has_pim = false;
#ifdef REGEXP_LOGGING
    fprintf(log_fd, "(---) STARTSTATE first\n");
#endif
    thislist->id = exe.nfa_listid + 1;

    // Inline optimized code for addstate(thislist, start, m, 0) if we know
    // it's the first MOPEN.
    if (toplevel) {
      if (REG_MULTI) {
          m->norm.list.multi[0].start_lnum = exe.lnum;
          m->norm.list.multi[0].start_col = (ColNr)(exe.input - exe.line);
          m->norm.orig_start_col = m->norm.list.multi[0].start_col;
      } else
         m->norm.list.line[0].start = exe.input;
      m->norm.in_use = 1;
      r = addstate(thislist, start->out, m, NULL, 0);
   } else
      r = addstate(thislist, start, m, NULL, 0);
   if (r == NULL) {
      nfa_match = TOO_EXPENSIVE;
      goto theend;
   }

#define   ADD_STATE_IF_MATCH(state)   \
   if (result) {           \
       add_state = state->out;      \
       add_off = clen;         \
   }

   // Run for each character.
   for (;;) {

      Unt curc = mb_ptr2char(exe.input);
      int clen = utfCharLen(exe.input);
      if (curc == ZERO) {
         clen = 0;
         go_to_nextline = false;
      }

      // swap lists
      thislist = &list[flag];
      nextlist = &list[flag ^= 1];
      nextlist->n = 0;       // clear nextlist
      nextlist->has_pim = false;
      ++exe.nfa_listid;
      thislist->id = exe.nfa_listid;
      nextlist->id = exe.nfa_listid + 1;

#ifdef REGEXP_LOGGING
   fprintf(log_fd, "------------------------------------------\n");
   fprintf(log_fd, ">>> Reginput is \"%s\"\n", exe.input);
   fprintf(log_fd, ">>> Advanced one character... Current char is %c (code %d) \n", curc, (int)curc);
   fprintf(log_fd, ">>> Thislist has %d states available: ", thislist->n);
   {
       int i;

       for (i = 0; i < thislist->n; i++)
      fprintf(log_fd, "%d  ", abs(thislist->t[i].state->id));
   }
   fprintf(log_fd, "\n");
#endif

#ifdef REGEXP_DEBUG_LOG
   fprintf(debug, "\n-------------------\n");
#endif
   // If the state lists are empty, we can stop.
   if (thislist->n == 0)
       break;

   // compute nextlist
   for (listidx = 0; listidx < thislist->n; ++listidx) {
       // If the list gets very long there probably is something wrong.
       // At least allow interrupting with CTRL-C.
       fast_breakcheck();
       if (gotInterruptG)
      break;
       if (nfa_did_time_out())
      break;
       t = &thislist->t[listidx];

#ifdef REGEXP_DEBUG_LOG
       nfa_set_code(t->state->c);
       fprintf(debug, "%s, ", code);
#endif
#ifdef REGEXP_LOGGING
       {
      int col;

      if (t->subs.norm.in_use <= 0)
          col = -1;
      ei (REG_MULTI)
          col = t->subs.norm.list.multi[0].start_col;
      else
          col = (int)(t->subs.norm.list.line[0].start - exe.line);
      nfa_set_code(t->state->c);
      fprintf(log_fd, "(%d) char %d %s (start col %d)%s... \n",
         abs(t->state->id), (int)t->state->c, code, col,
         pim_info(&t->pim));
       }
#endif

       // Handle the possible codes of the current state. The most important is MATCH.
       add_state = NULL;
       add_here = false;
       add_count = 0;
       switch (t->state->c) {
       case MATCH: {
         // If the match is not at the start of the line, ends before a
         // composing characters and exe.reg_icombine is not set, that
         // is not really a match.
         if (!exe.reg_icombine && exe.input != exe.line && utf_iscomposing(curc))
             break;

         nfa_match = true;
         copy_sub(&submatch->norm, &t->subs.norm);
         if (exe.nfa_has_zsubexpr)
             copy_sub(&submatch->synt, &t->subs.synt);
#ifdef REGEXP_LOGGING
         log_subsexpr(&t->subs);
#endif
         // Found the left-most longest match, do not look at any other
         // states at this position.  When the list of states is going
         // to be empty quit without advancing, so that "exe.input" is correct.
         if (nextlist->n == 0)
             clen = 0;
         goto nextchar;
      }

      case END_INVISIBLE:
      case END_INVISIBLE_NEG:
      case END_PATTERN:
      // This is only encountered after a START_INVISIBLE or
      // START_INVISIBLE_BEFORE node.
      // They surround a zero-width group, used with "\@=", "\&",
      // "\@!", "\@<=" and "\@<!".
      // If we got here, it means that the current "invisible" group
      // finished successfully, so return control to the parent
      // match().  For a look-behind match only when it ends
      // in the position in "mustEndAtS".
      // Submatches are stored in *m, and used in the parent call.
#ifdef REGEXP_LOGGING
      if (mustEndAtS != NULL) {
         if (REG_MULTI)
            fprintf(log_fd, "Current lnum: %d, endp lnum: %d; current col: %d, endp col: %d\n",
               (int)exe.lnum,
               (int)mustEndAtS->se_u.pos.lnum,
               (int)(exe.input - exe.line),
               mustEndAtS->se_u.pos.col);
         else
            fprintf(log_fd, "Current col: %d, endp col: %d\n",
               (int)(exe.input - exe.line),
               (int)(mustEndAtS->se_u.ptr - exe.input));
      }
#endif
      // If "mustEndAtS" is set it's only a match if it ends at
      // "mustEndAtS"
      if (mustEndAtS != NULL && (REG_MULTI
         ? (exe.lnum != mustEndAtS->se_u.pos.lnum
             || (int)(exe.input - exe.line)
                  != mustEndAtS->se_u.pos.col)
         : exe.input != mustEndAtS->se_u.ptr))
          break;

      // do not set submatches for \@!
      if (t->state->c != END_INVISIBLE_NEG) {
          copy_sub(&m->norm, &t->subs.norm);
          if (exe.nfa_has_zsubexpr)
         copy_sub(&m->synt, &t->subs.synt);
      }
#ifdef REGEXP_LOGGING
      fprintf(log_fd, "Match found:\n");
      log_subsexpr(m);
#endif
      nfa_match = true;
      // See comment above at "goto nextchar".
      if (nextlist->n == 0)
          clen = 0;
      goto nextchar;

      case START_INVISIBLE:
      case START_INVISIBLE_FIRST:
      case START_INVISIBLE_NEG:
      case START_INVISIBLE_NEG_FIRST:
      case START_INVISIBLE_BEFORE:
      case START_INVISIBLE_BEFORE_FIRST:
      case START_INVISIBLE_BEFORE_NEG:
      case START_INVISIBLE_BEFORE_NEG_FIRST: {
#ifdef REGEXP_LOGGING
          fprintf(log_fd, "Failure chance invisible: %d, what follows: %d\n",
             failure_chance(t->state->out, 0),
             failure_chance(t->state->out1->out, 0));
#endif
         // Do it directly if there already is a PIM or when
         // addOptimizationHints() detected it will work better.
         if (t->pim.result != PIM_UNUSED
         || t->state->c == START_INVISIBLE_FIRST
         || t->state->c == START_INVISIBLE_NEG_FIRST
         || t->state->c == START_INVISIBLE_BEFORE_FIRST
         || t->state->c == START_INVISIBLE_BEFORE_NEG_FIRST
         ) {
            int in_use = m->norm.in_use;

            // Copy submatch info for the recursive call, opposite
            // of what happens on success below.
            copy_sub_off(&m->norm, &t->subs.norm);
            if (exe.nfa_has_zsubexpr)
                copy_sub_off(&m->synt, &t->subs.synt);

            // First try matching the invisible match, then what follows.
            result = recursiveMatch(t->state, NULL, prog, submatch, m, &listids, &listids_len);
            if (result == TOO_EXPENSIVE) {
               nfa_match = result;
               goto theend;
            }

            // for \@! and \@<! it is a match when the result is false
            if (result != (t->state->c == START_INVISIBLE_NEG
                   || t->state->c == START_INVISIBLE_NEG_FIRST
                   || t->state->c
                     == START_INVISIBLE_BEFORE_NEG
                   || t->state->c
                    == START_INVISIBLE_BEFORE_NEG_FIRST))
            {
               // Copy submatch info from the recursive call
               copy_sub_off(&t->subs.norm, &m->norm);
               if (exe.nfa_has_zsubexpr)
                  copy_sub_off(&t->subs.synt, &m->synt);
               // If the pattern has \ze and it matched in the sub pattern, use it.
               copy_ze_off(&t->subs.norm, &m->norm);

               // t->state->out1 is the corresponding
               // END_INVISIBLE node; Add its out to the current list (zero-width match).
               add_here = true;
               add_state = t->state->out1->out;
            }
            m->norm.in_use = in_use;
         } else {
            PostponedMatch pim;

            // First try matching what follows.  Only if a match
            // is found verify the invisible match matches.  Add a
            // PostponedMatch to the following states, it contains info about the invisible match.
            pim.state = t->state;
            pim.result = PIM_TODO;
            pim.subs.norm.in_use = 0;
            pim.subs.synt.in_use = 0;
            if (REG_MULTI) {
               pim.end.pos.col = (int)(exe.input - exe.line);
               pim.end.pos.lnum = exe.lnum;
            } else
               pim.end.ptr = exe.input;

            // t->state->out1 is the corresponding END_INVISIBLE
            // node; Add its out to the current list (zero-width match).
            if (addstate_here(thislist, t->state->out1->out, &t->subs, &pim, &listidx) == NULL) {
               nfa_match = TOO_EXPENSIVE;
               goto theend;
            }
         }
      }
      break;

      case START_PATTERN: {
      RState *skip = NULL;
#ifdef REGEXP_LOGGING
      int       skip_lid = 0;
#endif

      // There is no point in trying to match the pattern if the
      // output state is not going to be added to the list.
      if (state_in_list(nextlist, t->state->out1->out, &t->subs)) {
          skip = t->state->out1->out;
#ifdef REGEXP_LOGGING
          skip_lid = nextlist->id;
#endif
      } ei (state_in_list(nextlist, t->state->out1->out->out, &t->subs)) {
          skip = t->state->out1->out->out;
#ifdef REGEXP_LOGGING
          skip_lid = nextlist->id;
#endif
      }
      ei (state_in_list(thislist, t->state->out1->out->out, &t->subs)) {
          skip = t->state->out1->out->out;
#ifdef REGEXP_LOGGING
          skip_lid = thislist->id;
#endif
      }
      if (skip != NULL) {
#ifdef REGEXP_LOGGING
          nfa_set_code(skip->c);
          fprintf(log_fd, "> Not trying to match pattern, output state %d is already in list %d. char %d: %s\n",
             abs(skip->id), skip_lid, skip->c, code);
#endif
          break;
      }
      // Copy submatch info to the recursive call, opposite of what happens afterwards.
      copy_sub_off(&m->norm, &t->subs.norm);
      if (exe.nfa_has_zsubexpr)
          copy_sub_off(&m->synt, &t->subs.synt);

      // First try matching the pattern.
      result = recursiveMatch(t->state, NULL, prog, submatch, m, &listids, &listids_len);
      if (result == TOO_EXPENSIVE) {
          nfa_match = result;
          goto theend;
      }
      if (result) {
          int bytelen;

#ifdef REGEXP_LOGGING
          fprintf(log_fd, "START_PATTERN matches:\n");
          log_subsexpr(m);
#endif
         // Copy submatch info from the recursive call
         copy_sub_off(&t->subs.norm, &m->norm);
         if (exe.nfa_has_zsubexpr)
            copy_sub_off(&t->subs.synt, &m->synt);
         // Now we need to skip over the matched text and then
         // continue with what follows.
         if (REG_MULTI) {
            // TODO: multi-line match
            bytelen = m->norm.list.multi[0].end_col - (int)(exe.input - exe.line);
         } else
            bytelen = (int)(m->norm.list.line[0].end - exe.input);

#ifdef REGEXP_LOGGING
         fprintf(log_fd, "START_PATTERN length: %d\n", bytelen);
#endif
         if (bytelen == 0) {
            // empty match, output of corresponding
            // END_PATTERN/SKIP to be used at current position
            add_here = true;
            add_state = t->state->out1->out->out;
         } ei (bytelen <= clen) {
            // match current character, output of corresponding
            // END_PATTERN to be used at next position.
            add_state = t->state->out1->out->out;
            add_off = clen;
         } else {
            // skip over the matched characters, set character count in SKIP
            add_state = t->state->out1->out;
            add_off = bytelen;
            add_count = bytelen - clen;
         }
      }
      break;
      }

      case BOL:
         if (exe.input == exe.line) {
            add_here = true;
            add_state = t->state->out;
         }
         break;

      case EOL:
         if (curc == ZERO) {
             add_here = true;
             add_state = t->state->out;
         }
         break;

      case BOW:
         result = true;

      if (curc == ZERO)
         result = false;
      else {
         int this_class;

         // Get class of current and previous char (if it exists).
         this_class = inpGetClassForBook(exe.input, exe.book);
         if (this_class <= 1)
            result = false;
         ei (reg_prev_class() == this_class)
            result = false;
      } 
      if (result) {
         add_here = true;
         add_state = t->state->out;
      }
      break;

      case EOW:
      result = true;
      if (exe.input == exe.line)
         result = false;
      else {
         int this_class, prev_class;

         // Get class of current and previous char (if it exists).
         this_class = inpGetClassForBook(exe.input, exe.book);
         prev_class = reg_prev_class();
         if (this_class == prev_class || prev_class == 0 || prev_class == 1)
            result = false;
      } 
      if (result) {
         add_here = true;
         add_state = t->state->out;
      }
      break;

      case BOF:
         if (exe.lnum == 0 && exe.input == exe.line && (!REG_MULTI || exe.reg_firstlnum == 1)) {
            add_here = true;
            add_state = t->state->out;
         }
         break;

      case EOFF:
         if (exe.lnum == exe.reg_maxline && curc == ZERO) {
            add_here = true;
            add_state = t->state->out;
         }
         break;

      case COMPOSING: {
         Unt       mc = curc;
         int       len = 0;
         RState *end;
         RState *sta;
         Unt       cchars[MAX_COMBINED_SYMBOLS];
         int       ccount = 0;
         int       j;

         sta = t->state->out;
         len = 0;
         if (utf_iscomposing(sta->c)) {
            // Only match composing character(s), ignore base
            // character. Used for ".{composing}" and "{composing}" (no preceding character).
            len += mb_char2len(mc);
         }
         if (exe.reg_icombine && len == 0) {
            // If \Z was present, then ignore composing characters.
            // When ignoring the base character this always matches.
            if (sta->c != curc)
               result = FAIL;
            else
               result = OK;
            while (sta->c != END_COMPOSING)
               sta = sta->out;
         }

         // Check base character matches first, unless ignored.
         ei (len > 0 || mc == sta->c) {
            if (len == 0) {
               len += mb_char2len(mc);
               sta = sta->out;
            }

            // We don't care about the order of composing characters.
            // Get them into cchars[] first.
            while (len < clen) {
               mc = mb_ptr2char(exe.input + len);
               cchars[ccount++] = mc;
               len += mb_char2len(mc);
               if (ccount == MAX_COMBINED_SYMBOLS)
                   break;
            }

            //Check that each composing char in the pattern matches a composing char in the text.
            //We do not check if all composing chars are matched.
            result = OK;
            while (sta->c != END_COMPOSING) {
               for (j = 0; j < ccount; ++j) {
                  if (cchars[j] == sta->c)
                     break;
               } 
               if (j == ccount) {
                  result = FAIL;
                  break;
               }
               sta = sta->out;
            }
         } else
            result = FAIL;

         end = t->state->out1;       // END_COMPOSING
         ADD_STATE_IF_MATCH(end);
         break;
       }

      case NEWL:
      if (curc == ZERO && !exe.reg_line_lbr && REG_MULTI && exe.lnum <= exe.reg_maxline) {
         go_to_nextline = true;
         //Pass -1 for the offset, which means taking the position at the start of the next line.
         add_state = t->state->out;
         add_off = -1;
      } ei (curc == '\n' && exe.reg_line_lbr) {
         //match \n as if it is an ordinary character
         add_state = t->state->out;
         add_off = 1;
      }
      break;

      case START_COLL:
      case START_NEG_COLL: {
         // What follows is a list of characters, until END_COLL.
         // One of them must match or none of them must match.
         RState   *state;
         int      result_if_matched;
         Unt      c1, c2;

         // Never match EOL. If it's part of the collection it is added
         // as a separate state with an OR.
         if (curc == ZERO)
            break;

         state = t->state->out;
         result_if_matched = (t->state->c == START_COLL);
         for (;;) {
            if (state->c == COMPOSING) {
               Unt       mc = curc;
               int       len = 0;
               RState *end;
               RState *sta;
               Unt cchars[MAX_COMBINED_SYMBOLS];
               int ccount = 0;
               int j;

               sta = t->state->out->out;
               len = 0;
               if (utf_iscomposing(sta->c)) {
                   // Only match composing character(s), ignore base
                   // character.  Used for ".{composing}" and "{composing}"
                   // (no preceding character).
                   len += mb_char2len(mc);
               }
               if (exe.reg_icombine && len == 0) {
                   // If \Z was present, then ignore composing characters.
                   // When ignoring the base character this always matches.
                   if (sta->c != curc)
                  result = FAIL;
                   else
                  result = OK;
                   while (sta->c != END_COMPOSING)
                  sta = sta->out;
               }
               // Check base character matches first, unless ignored.
               ei (len > 0 || mc == sta->c) {
                   if (len == 0) {
                     len += mb_char2len(mc);
                     sta = sta->out;
                   }

                   // We don't care about the order of composing characters.
                   // Get them into cchars[] first.
                   while (len < clen) {
                  mc = mb_ptr2char(exe.input + len);
                  cchars[ccount++] = mc;
                  len += mb_char2len(mc);
                  if (ccount == MAX_COMBINED_SYMBOLS)
                      break;
                  }

                  // Check that each composing char in the pattern matches a
                  // composing char in the text.  We do not check if all
                  // composing chars are matched.
                  result = OK;
                  while (sta->c != END_COMPOSING) {
                     for (j = 0; j < ccount; ++j) {
                        if (cchars[j] == sta->c)
                           break;
                     } 
                     if (j == ccount) {
                        result = FAIL;
                        break;
                     }
                     sta = sta->out;
                  }
               } else
                  result = FAIL;

               if (t->state->out->out1 != NULL && t->state->out->out1->c == END_COMPOSING) {
                  end = t->state->out->out1;
                  ADD_STATE_IF_MATCH(end);
               }
               break;
            }
            if (state->c == END_COLL) {
               result = !result_if_matched;
               break;
            }
            if (state->c == RANGE_MIN) {
               c1 = state->val;
               state = state->out; // advance to RANGE_MAX
               c2 = state->val;
#ifdef REGEXP_LOGGING
               fprintf(log_fd, "RANGE_MIN curc=%d c1=%d c2=%d\n", curc, c1, c2);
#endif
               if (curc >= c1 && curc <= c2) {
                   result = result_if_matched;
                   break;
               }
               if (exe.reg_ic) {
                  Unt curc_low = MB_CASEFOLD(curc);
                  Boole done = false;

                  for ( ; c1 <= c2; ++c1) {
                     if (MB_CASEFOLD(c1) == curc_low) {
                         result = result_if_matched;
                         done = true;
                         break;
                     }
                  } 
                  if (done)
                     break;
               }
            } ei (state->c >= UNT_NEG
                  ? check_char_class(state->c, curc)
                  : (curc == state->c || (exe.reg_ic && MB_CASEFOLD(curc) == MB_CASEFOLD(state->c)))
            ) {
               result = result_if_matched;
               break;
            }
            state = state->out;
         }
         if (result) {
             // next state is in out of the END_COLL, out1 of
             // START points to the END state
             add_state = t->state->out1->out;
             add_off = clen;
         }
         break;
         }

      case ANY:
         // Any char except '\0', (end of input) does not match.
         if (curc > 0) {
             add_state = t->state->out;
             add_off = clen;
         }
         break;

      case ANY_COMPOSING:
         // On a composing character skip over it.  Otherwise do nothing.  Always matches.
         if (utf_iscomposing(curc)) {
            add_off = clen;
         } else {
            add_here = true;
            add_off = 0;
         }
         add_state = t->state->out;
         break;

      // Character classes like \a for alpha, \d for digit etc.
      case IDENT:   //  \i
         result = eeIsIdentifierChar(curc);
         ADD_STATE_IF_MATCH(t->state);
         break;

      case SIDENT:   //  \I
         result = !EE_ISDIGIT(curc) && eeIsIdentifierChar(curc);
         ADD_STATE_IF_MATCH(t->state);
         break;

      case KWORD:   //  \k
         result = eeIsWordPtr_buf(exe.input, exe.book);
         ADD_STATE_IF_MATCH(t->state);
         break;

      case SKWORD:   //  \K
         result = !EE_ISDIGIT(curc) && eeIsWordPtr_buf(exe.input, exe.book);
         ADD_STATE_IF_MATCH(t->state);
         break;

      case FNAME:   //  \f
         result = eeIsFnameChar(curc);
         ADD_STATE_IF_MATCH(t->state);
         break;

      case SFNAME:   //  \F
         result = !EE_ISDIGIT(curc) && eeIsFnameChar(curc);
         ADD_STATE_IF_MATCH(t->state);
         break;

      case PRINT:   //  \p
         result = bookIsCharPrintable(mb_ptr2char(exe.input));
         ADD_STATE_IF_MATCH(t->state);
         break;

      case SPRINT:   //  \P
         result = !EE_ISDIGIT(curc) && bookIsCharPrintable(mb_ptr2char(exe.input));
         ADD_STATE_IF_MATCH(t->state);
         break;

      case WHITE:   //  \s
         result = SPACE_OR_TAB(curc);
         ADD_STATE_IF_MATCH(t->state);
         break;

      case NWHITE:   //  \S
         result = curc != ZERO && !SPACE_OR_TAB(curc);
         ADD_STATE_IF_MATCH(t->state);
         break;

      case DIGIT:   //  \d
         result = ri_digit(curc);
         ADD_STATE_IF_MATCH(t->state);
         break;

      case NDIGIT:   //  \D
         result = curc != ZERO && !ri_digit(curc);
         ADD_STATE_IF_MATCH(t->state);
         break;

      case HEX:   //  \x
         result = ri_hex(curc);
         ADD_STATE_IF_MATCH(t->state);
         break;

      case NHEX:   //  \X
         result = curc != ZERO && !ri_hex(curc);
         ADD_STATE_IF_MATCH(t->state);
         break;

      case WORD:   //  \w
         result = ri_word(curc);
         ADD_STATE_IF_MATCH(t->state);
         break;

      case NWORD:   //  \W
         result = curc != ZERO && !ri_word(curc);
         ADD_STATE_IF_MATCH(t->state);
         break;

      case HEAD:   //  \h
         result = ri_head(curc);
         ADD_STATE_IF_MATCH(t->state);
         break;

      case NHEAD:   //  \H
         result = curc != ZERO && !ri_head(curc);
         ADD_STATE_IF_MATCH(t->state);
         break;

      case ALPHA:   //  \a
         result = ri_alpha(curc);
         ADD_STATE_IF_MATCH(t->state);
         break;

      case NALPHA:   //  \A
         result = curc != ZERO && !ri_alpha(curc);
         ADD_STATE_IF_MATCH(t->state);
         break;

      case LOWER:   //  \l
         result = ri_lower(curc);
         ADD_STATE_IF_MATCH(t->state);
         break;

      case NLOWER:   //  \L
         result = curc != ZERO && !ri_lower(curc);
         ADD_STATE_IF_MATCH(t->state);
         break;

      case UPPER:   //  \u
         result = ri_upper(curc);
         ADD_STATE_IF_MATCH(t->state);
         break;

      case NUPPER:   // \U
         result = curc != ZERO && !ri_upper(curc);
         ADD_STATE_IF_MATCH(t->state);
         break;

      case LOWER_IC:   // [a-z]
         result = ri_lower(curc) || (exe.reg_ic && ri_upper(curc));
         ADD_STATE_IF_MATCH(t->state);
         break;

      case NLOWER_IC:   // [^a-z]
         result = curc != ZERO && !(ri_lower(curc) || (exe.reg_ic && ri_upper(curc)));
         ADD_STATE_IF_MATCH(t->state);
         break;

      case UPPER_IC:   // [A-Z]
         result = ri_upper(curc) || (exe.reg_ic && ri_lower(curc));
         ADD_STATE_IF_MATCH(t->state);
         break;

      case NUPPER_IC:   // ^[A-Z]
         result = curc != ZERO && !(ri_upper(curc) || (exe.reg_ic && ri_lower(curc)));
         ADD_STATE_IF_MATCH(t->state);
         break;

      case BACKREF1:
      case BACKREF2:
      case BACKREF3:
      case BACKREF4:
      case BACKREF5:
      case BACKREF6:
      case BACKREF7:
      case BACKREF8:
      case BACKREF9:
      case ZREF1:
      case ZREF2:
      case ZREF3:
      case ZREF4:
      case ZREF5:
      case ZREF6:
      case ZREF7:
      case ZREF8:
      case ZREF9: {
      // \1 .. \9  \z1 .. \z9
         int subidx;
         int bytelen;

         if (t->state->c >= BACKREF1 && t->state->c <= BACKREF9) {
            subidx = t->state->c - BACKREF1 + 1;
            result = match_backref(&t->subs.norm, subidx, &bytelen);
         } else {
            subidx = t->state->c - ZREF1 + 1;
            result = match_zref(subidx, &bytelen);
         }

         if (result) {
            if (bytelen == 0) {
               // empty match always works, output of SKIP to be
               // used next
               add_here = true;
               add_state = t->state->out->out;
            } ei (bytelen <= clen) {
               // match current character, jump ahead to out of SKIP
               add_state = t->state->out->out;
               add_off = clen;
            } else {
               // skip over the matched characters, set character
               // count in SKIP
               add_state = t->state->out;
               add_off = bytelen;
               add_count = bytelen - clen;
            }
         }
         break;
      }
      case SKIP:
        // character of previous matching \1 .. \9  or \@>
        if (t->count - clen <= 0) {
           // end of match, go to what follows
           add_state = t->state->out;
           add_off = clen;
        } else {
           // add state again with decremented count
           add_state = t->state;
           add_off = 0;
           add_count = t->count - clen;
        }
        break;

      case LNUM:
      case LNUM_GT:
      case LNUM_LT:
         result = (REG_MULTI &&
            nfa_re_num_cmp(t->state->val, t->state->c - LNUM,
                (Ulong)(exe.lnum + exe.reg_firstlnum)));
         if (result) {
            add_here = true;
            add_state = t->state->out;
         }
         break;

      case COL:
      case COL_GT:
      case COL_LT:
         result = nfa_re_num_cmp(t->state->val, t->state->c - COL,
            (Ulong)(exe.input - exe.line) + 1);
         if (result) {
            add_here = true;
            add_state = t->state->out;
         }
         break;

      case VCOL:
      case VCOL_GT:
      case VCOL_LT: {
         int     op = t->state->c - VCOL;
         ColNr col = (ColNr)(exe.input - exe.line);
         Portal   *wp = exe.portal == NULL ? curPor : exe.portal;

         // Bail out quickly when there can't be a match, avoid the
         // overhead of drawLineOnScreentabsize() on long lines.
         if (op != 1 && col > t->state->val * MB_MAXBYTES)
            break;
         result = false;
         if (op == 1 && col - 1 > t->state->val && col > 100) {
            int ts = wp->book->o.shiftWidth;

            // Guess that a character won't use more columns than tab size, with a minimum of 4.
            if (ts < 3)
               ts = 3;
            result = col > t->state->val * ts;
         }
         if (!result) {
            LineNr    lnum = REG_MULTI ? exe.reg_firstlnum + exe.lnum : 1;
            Ulong       vcol;

            if (REG_MULTI && (lnum <= 0 || lnum > wp->book->mem.lineCount))
               lnum = 1;
            vcol = (Ulong)drawLineOnScreentabsize(wp, lnum, exe.line, col);
            result = nfa_re_num_cmp(t->state->val, op, vcol + 1);
         }
         if (result) {
            add_here = true;
            add_state = t->state->out;
         }
      }
      break;

      case MARK:
      case MARK_GT:
      case MARK_LT: {
         Pos   *pos;
         Unt   col = REG_MULTI ? exe.input - exe.line : 0;

         pos = markGetBook(exe.book, t->state->val, false);

         // Line may have been freed, get it again.
         if (REG_MULTI) {
            exe.line = reg_getline(exe.lnum);
            exe.input = exe.line + col;
         }

         // Compare the mark position to the match position, if the mark
         // exists and mark is set in buf.
         if (pos != NULL && pos->lnum > 0) {
            ColNr pos_col = pos->lnum == exe.lnum + exe.reg_firstlnum
                          && pos->col == MAXCOL
                     ? reg_getline_len(pos->lnum - exe.reg_firstlnum)
                     : pos->col;

            result = (pos->lnum == exe.lnum + exe.reg_firstlnum
               ? (pos_col == (ColNr)(exe.input - exe.line)
                   ? t->state->c == MARK
                   : (pos_col < (ColNr)(exe.input - exe.line)
                  ? t->state->c == (Unt)MARK_GT
                  : t->state->c == (Unt)MARK_LT))
               : (pos->lnum < exe.lnum + exe.reg_firstlnum
                   ? t->state->c == (Unt)MARK_GT
                   : t->state->c == (Unt)MARK_LT));
            if (result) {
               add_here = true;
               add_state = t->state->out;
            }
         }
         break;
      }

      case CURSOR:
         result = (exe.portal != NULL
               && (exe.lnum + exe.reg_firstlnum == exe.portal->cursor.lnum)
               && ((ColNr)(exe.input - exe.line) == exe.portal->cursor.col)
         );
         if (result) {
            add_here = true;
            add_state = t->state->out;
         }
         break;

      case VISUAL:
         result = reg_match_visual();
         if (result) {
            add_here = true;
            add_state = t->state->out;
         }
         break;

      case MOPEN1:
      case MOPEN2:
      case MOPEN3:
      case MOPEN4:
      case MOPEN5:
      case MOPEN6:
      case MOPEN7:
      case MOPEN8:
      case MOPEN9:
      case ZOPEN:
      case ZOPEN1:
      case ZOPEN2:
      case ZOPEN3:
      case ZOPEN4:
      case ZOPEN5:
      case ZOPEN6:
      case ZOPEN7:
      case ZOPEN8:
      case ZOPEN9:
      case NOPEN:
      case ZSTART:
         // These states are only added to be able to bail out when
         // they are added again, nothing is to be done.
         break;

      default: {  // regular character
         Unt c = t->state->c;

#ifdef DEBUG
         if (c < 0)
             internalErrFmtMsg("Negative state char: %ld", (long)c);
#endif
         result = (c == curc);

         if (!result && exe.reg_ic)
            result = MB_CASEFOLD(c) == MB_CASEFOLD(curc);
         // If exe.reg_icombine is not set only skip over the character
         // itself.  When it is set skip over composing characters.
         if (result && !exe.reg_icombine)
             clen = utf_ptr2len(exe.input);
         ADD_STATE_IF_MATCH(t->state);
         break;
      }
      } // switch (t->state->c)

      if (add_state != NULL) {
         PostponedMatch *pim;
         PostponedMatch pim_copy;

         if (t->pim.result == PIM_UNUSED)
            pim = NULL;
         else
            pim = &t->pim;

         // Handle the postponed invisible match if the match might end
         // without advancing and before the end of the line.
         if (pim != NULL && (clen == 0 || match_follows(add_state, 0))) {
            if (pim->result == PIM_TODO) {
#ifdef REGEXP_LOGGING
               fprintf(log_fd, "\n");
               fprintf(log_fd, "==================================\n");
               fprintf(log_fd, "Postponed recursive match()\n");
               fprintf(log_fd, "\n");
#endif
               result = recursiveMatch(pim->state, pim, prog, submatch, m, &listids, &listids_len);
               pim->result = result ? PIM_MATCH : PIM_NOMATCH;
               // for \@! and \@<! it is a match when the result is false
               if (result != (pim->state->c == (Unt)START_INVISIBLE_NEG
                    || pim->state->c == (Unt)START_INVISIBLE_NEG_FIRST
                    || pim->state->c == (Unt)START_INVISIBLE_BEFORE_NEG
                    || pim->state->c == (Unt)START_INVISIBLE_BEFORE_NEG_FIRST)
               ){
                  // Copy submatch info from the recursive call
                  copy_sub_off(&pim->subs.norm, &m->norm);
                  if (exe.nfa_has_zsubexpr)
                     copy_sub_off(&pim->subs.synt, &m->synt);
               }
            } else {
            result = (pim->result == PIM_MATCH);
#ifdef REGEXP_LOGGING
            fprintf(log_fd, "\n");
            fprintf(log_fd, "Using previous recursive match() result, result == %d\n", pim->result);
            fprintf(log_fd, "MATCH = %s\n", result == true ? "OK" : "false");
            fprintf(log_fd, "\n");
#endif
            }

            // for \@! and \@<! it is a match when result is false
            if (result != (pim->state->c == (Unt)START_INVISIBLE_NEG
                 || pim->state->c == (Unt)START_INVISIBLE_NEG_FIRST
                 || pim->state->c == (Unt)START_INVISIBLE_BEFORE_NEG
                 || pim->state->c == (Unt)START_INVISIBLE_BEFORE_NEG_FIRST)
            ) {
            // Copy submatch info from the recursive call
            copy_sub_off(&t->subs.norm, &pim->subs.norm);
            if (exe.nfa_has_zsubexpr)
                copy_sub_off(&t->subs.synt, &pim->subs.synt);
            } else
               // look-behind match failed, don't add the state
               continue;

            // Postponed invisible match was handled, don't add it to
            // following states.
            pim = NULL;
         }

         // If "pim" points into l->t it will become invalid when
         // adding the state causes the list to be reallocated.  Make a
         // local copy to avoid that.
         if (pim == &t->pim) {
            copy_pim(&pim_copy, pim);
            pim = &pim_copy;
         }

         if (add_here)
            r = addstate_here(thislist, add_state, &t->subs, pim, &listidx);
         else {
            r = addstate(nextlist, add_state, &t->subs, pim, add_off);
            if (add_count > 0)
               nextlist->t[nextlist->n - 1].count = add_count;
         }
         if (r == NULL) {
            nfa_match = TOO_EXPENSIVE;
            goto theend;
         }
      }

   } // for (thislist = thislist; thislist->state; thislist++)

   // Look for the start of a match in the current position by adding the
   // start state to the list of states.
   // The first found match is the leftmost one, thus the order of states matters!
   // Do not add the start state in recursive calls of match(),
   // because recursive calls should only start in the first position.
   // Unless "mustEndAtS" is not NULL, then we match the end position.
   // Also don't start a match past the first line.
   if (nfa_match == false
      && ((toplevel
         && exe.lnum == 0
         && clen != 0
         && (exe.reg_maxcol == 0
           || (ColNr)(exe.input - exe.line) < exe.reg_maxcol))
          || (mustEndAtS != NULL
         && (REG_MULTI
             ? (exe.lnum < mustEndAtS->se_u.pos.lnum
                || (exe.lnum == mustEndAtS->se_u.pos.lnum
               && (int)(exe.input - exe.line)
                      < mustEndAtS->se_u.pos.col))
             : exe.input < mustEndAtS->se_u.ptr)))
   ){
#ifdef REGEXP_LOGGING
       fprintf(log_fd, "(---) STARTSTATE\n");
#endif
      // Inline optimized code for addstate() if we know the state is the first MOPEN.
      if (toplevel) {
         int add = true;
         int c;

         if (prog->regstart != ZERO && clen != 0) {
            if (nextlist->n == 0) {
               ColNr col = (ColNr)(exe.input - exe.line) + clen;

               // Nextlist is empty, we can skip ahead to the
               // character that must appear at the start.
               if (skip_to_start(prog->regstart, &col) == FAIL)
                   break;
#ifdef REGEXP_LOGGING
               fprintf(log_fd, "  Skipping ahead %d bytes to regstart\n",
                  col - ((ColNr)(exe.input - exe.line) + clen));
#endif
               exe.input = exe.line + col - clen;
            } else {
               // Checking if the required start character matches is
               // cheaper than adding a state that won't match.
               c = mb_ptr2char(exe.input + clen);
               if (c != prog->regstart 
                     && (!exe.reg_ic || MB_CASEFOLD(c) != MB_CASEFOLD(prog->regstart))
               ) {
#ifdef REGEXP_LOGGING
                   fprintf(log_fd, "  Skipping start state, regstart does not match\n");
#endif
                   add = false;
               }
            }
         }

         if (add) {
            if (REG_MULTI) {
               m->norm.list.multi[0].start_col = (ColNr)(exe.input - exe.line) + clen;
               m->norm.orig_start_col = m->norm.list.multi[0].start_col;
            } else
               m->norm.list.line[0].start = exe.input + clen;
            if (addstate(nextlist, start->out, m, NULL, clen) == NULL) {
               nfa_match = TOO_EXPENSIVE;
               goto theend;
            }
         }
      } else {
         if (addstate(nextlist, start, m, NULL, clen) == NULL) {
            nfa_match = TOO_EXPENSIVE;
            goto theend;
         }
      }
   }

#ifdef REGEXP_LOGGING
   fprintf(log_fd, ">>> Thislist had %d states available: ", thislist->n); {
      int i;

      for (i = 0; i < thislist->n; i++)
         fprintf(log_fd, "%d  ", abs(thislist->t[i].state->id));
   }
   fprintf(log_fd, "\n");
#endif

public nextchar:
      // Advance to the next character, or advance to the next line, or finish.
      if (clen != 0)
         exe.input += clen;
      ei (go_to_nextline 
            || (mustEndAtS != NULL && REG_MULTI && exe.lnum < mustEndAtS->se_u.pos.lnum))
         reg_nextline();
      else
         break;

      // Allow interrupting with CTRL-C.
      line_breakcheck();
      if (gotInterruptG)
         break;
      if (nfa_did_time_out())
         break;
   }

#ifdef REGEXP_LOGGING
   if (log_fd != stderr)
      fclose(log_fd);
   log_fd = NULL;
#endif

public theend:
   // Free memory
   eeglFree(list[0].t);
   eeglFree(list[1].t);
   eeglFree(listids);
#undef ADD_STATE_IF_MATCH
#ifdef REGEXP_DEBUG_LOG
   fclose(debug);
#endif

   return nfa_match;
}

// Try match of "prog" with at exe.line["col"].
// Return <= 0 for failure, number of lines contained in the match otherwise.
private long
parseBranchtry(
   RegProg* prog,
   ColNr col,
   int* timed_out UNUSED   // flag set on timeout or NULL
){
   int i;
   Submatches   subs, m;
   RState* start = prog->start;
   int result;
#ifdef REGEXP_LOGGING
   FILE* f;
#endif

   exe.input = exe.line + col;
   timedOutS = timed_out;

#ifdef REGEXP_LOGGING
   f = fopen(REGEXP_RUN_LOG, "a");
   if (f) {
      fprintf(f, "\n\n\t=======================================================\n");
# ifdef DEBUG
      fprintf(f, "\tRegexp is \"%s\"\n", regengine.expr);
# endif
      fprintf(f, "\tInput text is \"%s\" \n", exe.input);
      fprintf(f, "\t=======================================================\n\n");
      printState(f, start);
      fprintf(f, "\n\n");
      fclose(f);
   } else
      emsg("Could not open temporary log file for writing");
#endif

   clear_sub(&subs.norm);
   clear_sub(&m.norm);
   clear_sub(&subs.synt);
   clear_sub(&m.synt);
   result = match(prog, start, &subs, &m);
   if (result == false)
      return 0;
   ei (result == TOO_EXPENSIVE)
      return result;

   cleanup_subexpr();
   if (REG_MULTI) {
      for (i = 0; i < subs.norm.in_use; i++) {
         exe.reg_startpos[i].lnum = subs.norm.list.multi[i].start_lnum;
         exe.reg_startpos[i].col = subs.norm.list.multi[i].start_col;

         exe.reg_endpos[i].lnum = subs.norm.list.multi[i].end_lnum;
         exe.reg_endpos[i].col = subs.norm.list.multi[i].end_col;
      }
      if (exe.multiMatch)
         exe.multiMatch->rmm_matchcol = subs.norm.orig_start_col;

      if (exe.reg_startpos[0].lnum < 0) {
         exe.reg_startpos[0].lnum = 0;
         exe.reg_startpos[0].col = col;
      }
      if (exe.reg_endpos[0].lnum < 0) {
         // pattern has a \ze but it didn't match, use current end
         exe.reg_endpos[0].lnum = exe.lnum;
         exe.reg_endpos[0].col = (int)(exe.input - exe.line);
      } else // Use line number of "\ze".
         exe.lnum = exe.reg_endpos[0].lnum;
   } else {
      for (i = 0; i < subs.norm.in_use; i++) {
         exe.reg_startp[i] = subs.norm.list.line[i].start;
         exe.reg_endp[i] = subs.norm.list.line[i].end;
      }

      if (exe.reg_startp[0] == NULL)
         exe.reg_startp[0] = exe.line + col;
      if (exe.reg_endp[0] == NULL)
         exe.reg_endp[0] = exe.input;
   }

   // Package any found \z(...\) matches for export. Default is none.
   unref_extmatch(re_extmatch_out);
   re_extmatch_out = NULL;

   if (prog->reghasz == REX_SET) {
      cleanup_zsubexpr();
      re_extmatch_out = make_extmatch();
      if (!re_extmatch_out)
         return 0;
      // Loop over \z1, \z2, etc.  There is no \z0.
      for (i = 1; i < subs.synt.in_use; i++) {
         if (REG_MULTI) {
            struct multipos *mpos = &subs.synt.list.multi[i];

            // Only accept single line matches that are valid.
            if (mpos->start_lnum >= 0
                  && mpos->start_lnum == mpos->end_lnum
                  && mpos->end_col >= mpos->start_col
            )
               re_extmatch_out->matches[i] = copySubstr(
                     reg_getline(mpos->start_lnum) + mpos->start_col, mpos->end_col - mpos->start_col
               );
         } else {
         struct linepos *lpos = &subs.synt.list.line[i];

         if (lpos->start != NULL && lpos->end != NULL)
             re_extmatch_out->matches[i] =
                copySubstr(lpos->start, lpos->end - lpos->start);
          }
      }
   }

   return 1 + exe.lnum;
}

// Match a regexp against a string ("line" points to the string) or multiple
// lines (if "line" is NULL, use reg_getline()).
//
// Returns <= 0 for failure, number of lines contained in the match otherwise.
private long
parseBranchexec_both(
   Byte   *line,
   ColNr   startcol,   // column to start looking for match
   int      *timed_out // flag set on timeout or NULL
){
   RegProg   *prog;
   long retval = 0L;
   int i;
   ColNr col = startcol;

   if (REG_MULTI) {
      prog = (RegProg *)exe.multiMatch->regprog;
      line = reg_getline((LineNr)0);    // relative to the cursor
      exe.reg_startpos = exe.multiMatch->startpos;
      exe.reg_endpos = exe.multiMatch->endpos;
   } else {
      prog = (RegProg *)exe.match->regprog;
      exe.reg_startp = exe.match->startp;
      exe.reg_endp = exe.match->endp;
   }

   // Be paranoid...
   if (!prog || !line) {
      internalErrMsg(e_null_argument);
      goto theend;
   }

   //If pattern contains "\c" or "\C": overrule value of exe.reg_ic
   if (prog->regflags & RF_ICASE)
      exe.reg_ic = true;
   ei (prog->regflags & RF_NOICASE)
      exe.reg_ic = false;

   //If pattern contains "\Z" overrule value of exe.reg_icombine
   if (prog->regflags & RF_ICOMBINE)
      exe.reg_icombine = true;

   exe.line = line;
   exe.lnum = 0;    // relative to line

   exe.nfa_has_zend = prog->has_zend;
   exe.nfa_has_backref = prog->has_backref;
   exe.nfa_nsubexpr = prog->nsubexp;
   exe.nfa_listid = 1;
   exe.nfa_alt_listid = 2;
#ifdef DEBUG
   regengine.expr = prog->pattern;
#endif

   if (prog->reganch && col > 0)
      return 0L;

   exe.need_clear_subexpr = true;
   //Clear the external match subpointers if necessary.
   if (prog->reghasz == REX_SET) {
      exe.nfa_has_zsubexpr = true;
      exe.need_clear_zsubexpr = true;
   } else {
      exe.nfa_has_zsubexpr = false;
      exe.need_clear_zsubexpr = false;
   }

   if (prog->regstart != ZERO) {
      // Skip ahead until a character we know the match must start with.
      // When there is none there is no match.
      if (skip_to_start(prog->regstart, &col) == FAIL)
          return 0L;

      // If input is set it contains the full text that must match.
      // Nothing else to try. Doesn't handle combining chars well.
      if (prog->input != NULL && *prog->input != ZERO && !exe.reg_icombine) {
         retval = find_input(&col, prog->regstart, prog->input);
         if (REG_MULTI)
            exe.multiMatch->rmm_matchcol = col;
         else
            exe.match->rm_matchcol = col;
         return retval;
      }
   }

   // If the start column is past the maximum column: no need to try.
   if (exe.reg_maxcol > 0 && col >= exe.reg_maxcol)
      goto theend;

   // Set the "countStatesS" used by compile() to zero to trigger an error when
   // it's accidentally used during execution.
   countStatesS = 0;
   for (i = 0; i < prog->nstate; ++i) {
      prog->state[i].id = i;
      prog->state[i].lastlist[0] = 0;
      prog->state[i].lastlist[1] = 0;
   }

   retval = parseBranchtry(prog, col, timed_out);

#ifdef DEBUG
    regengine.expr = NULL;
#endif

public theend:
   if (retval > 0) {
      // Make sure the end is never before the start. Can happen when \zs and \ze are used.
      if (REG_MULTI) {
         PosNoVirt *start = &exe.multiMatch->startpos[0];
         PosNoVirt *end = &exe.multiMatch->endpos[0];

         if (end->lnum < start->lnum || (end->lnum == start->lnum && end->col < start->col))
            exe.multiMatch->endpos[0] = exe.multiMatch->startpos[0];
      } else {
         if (exe.match->endp[0] < exe.match->startp[0])
            exe.match->endp[0] = exe.match->startp[0];

         exe.match->endp[0] = exe.match->startp[0];
         // the whole pattern matched.
         exe.match->rm_matchcol = col;
      }
   }

   return retval;
}


// Compile a regular expression into internal code for the NFA matcher.
// Return the program in allocated space. Returns NULL for an error.
private RegProg *
compile(CS expr, int flags) {
   if (!expr)
      return NULL;

#ifdef DEBUG
   regengine.expr = expr;
#endif

   initCharacterClasses();

   if (compile_start(expr, flags) == FAIL)
      return NULL;

   Boole hadEol = false;
   // Build postfix (RPN) form of the regexp. Needed to build the NFA (and count its size)
   if (parse(REG_NOPAREN, OUT &hadEol) == FAIL)
      goto fail;
   EMIT(MOPEN);
   Unt* postfix = postfixStartS;
   if (!postfix)
      goto fail;       // Cascaded (syntax?) error

    // In order to build the NFA, we parse the input regexp twice:
    // 1. first pass to count size (so we can allocate space)
    // 2. second to emit code
#ifdef REGEXP_LOGGING
   {
   FILE *f = fopen(REGEXP_RUN_LOG, "a");

   if (f != NULL) {
      fprintf(
         f, "\n*****************************\n\n\n\n\tCompiling regexp \"%s\"... hold on !\n", expr
      );
      fclose(f);
   }
   }
#endif

   // PASS 1 Count number of NFA states in "countStatesS". Do not build the NFA.
   countStatesS = countStatesInPostfix(postfix, postfixS);

   // allocate the regprog with space for the compiled regexp
   Unt prog_size = offsetof(RegProg, state) + sizeof(RState) * countStatesS;
   RegProg* prog = alloc(prog_size);
   state_ptr = prog->state;
   prog->re_in_use = false;
   prog->hadEol = hadEol;

   // PASS 2. Build the NFA
   prog->start = buildAutomaton(postfix, postfixS);
   if (prog->start == NULL)
      goto fail;

   prog->regflags = regflags;
   prog->nstate = countStatesS;
   prog->has_zend = exe.nfa_has_zend;
   prog->has_backref = exe.nfa_has_backref;
   prog->nsubexp = regnpar;

   addOptimizationHints(prog);

   prog->reganch = getAnchor(prog->start, 0);
   prog->regstart = getRegStart(prog->start, 0);
   prog->input = getMatchText(prog->start);

#ifdef REGEXP_LOGGING
   dumpPostfix(expr, OK);
   dump(prog);
#endif
   // Remember whether this pattern has any \z specials in it.
   prog->reghasz = re_has_z;
   prog->pattern = copyStr(expr);
#ifdef DEBUG
   regengine.expr = NULL;
#endif

public out:
   EE_CLEAR(postfixStartS);
   postfixS = postfixEndS = NULL;
   state_ptr = NULL;
   return (RegProg *)prog;

public fail:
   EE_CLEAR(prog);
#ifdef REGEXP_LOGGING
   dumpPostfix(expr, FAIL);
#endif
#ifdef DEBUG
   regengine.expr = NULL;
#endif
   goto out;
}

// Free a compiled regexp program, returned by compile().
private void
freeBranch(RegProg *prog) {
   if (prog == NULL)
      return;

   eeglFree(prog->input);
   eeglFree(prog->pattern);
   eeglFree(prog);
}

// Match a regexp against a string.
// "rmp->regprog" is a compiled regexp as returned by compile(). Use curBook for line count 
// and 'iskeyword'. If "line_lbr" is true consider a "\n" in "line" to be a line break.
// Return <= 0 for failure, number of lines contained in the match otherwise.
private int
parseBranchexec_nl(
   RegMatch   *rmp,
   Byte   *line,  // string to match against
   ColNr   col,   // column to start looking for match
   int      line_lbr
){
   exe.match = rmp;
   exe.multiMatch = NULL;
   exe.reg_maxline = 0;
   exe.reg_line_lbr = line_lbr;
   exe.book = curBook;
   exe.portal = NULL;
   exe.reg_ic = rmp->rm_ic;
   exe.reg_icombine = false;
   exe.reg_maxcol = 0;
   return parseBranchexec_both(line, col, NULL);
}

// Initialize the values used for matching against multiple lines
private void
init_regexec_multi(
   RegMultilineMatch   *rmp,
   Portal* win,  // portal in which to search or NULL
   Book* book,  // book in which to search
   LineNr lnum)   // nr of line to start looking for match
{
   exe.match = NULL;
   exe.multiMatch = rmp;
   exe.book = book;
   exe.portal = win;
   exe.reg_firstlnum = lnum;
   exe.reg_maxline = exe.book->mem.lineCount - lnum;
   exe.reg_line_lbr = false;
   exe.reg_ic = rmp->rmm_ic;
   exe.reg_icombine = false;
   exe.reg_maxcol = rmp->rmm_maxcol;
}




// Match a regexp against multiple lines.
// "rmp->regprog" is a compiled regexp as returned by compileRegexp().
// Uses curBook for line count and 'iskeyword'.
//
// Return <= 0 if there is no match.  Return number of lines contained in the
// match otherwise.
//
// Note: the body is the same as bt_regexec() except for parseBranchexec_both()
//
// ! Also NOTE : match may actually be in another line. e.g.:
// when r.e. is \nc, cursor is at 'a' and the text buffer looks like
//
// +-------------------------+
// |a                        |
// |b                        |
// |c                        |
// |                         |
// +-------------------------+
//
// then matchManyLines() returns 3. while the original
// eeRegexec_multi() returns 0 and a second call at line 2 will return 2.
//
// FIXME if this behavior is not compatible.
private long
matchManyLines(
   RegMultilineMatch   *rmp,
   Portal* port,      // portal in which to search or NULL
   Book* book,      // book in which to search
   LineNr   lnum,      // nr of line to start looking for match
   ColNr   col,      // column to start looking for match
   int* timed_out   // flag set on timeout or NULL
){
   init_regexec_multi(rmp, port, book, lnum);
   return parseBranchexec_both(NULL, col, timed_out);
}

#ifdef DEBUG
# undef REGEXP_LOGGING
#endif


//}}}
//{{{common regexp code 

// Compile a regular expression into internal code. Returns the program in allocated memory.
// Use eeRegFree() to free the memory. Returns NULL for an error.
public RegProg *
compileRegexp(CS expr_arg, Unt flags) {
   CS expr = expr_arg;

#ifdef DEBUG
   regengine.expr = expr;
#endif
   // reg_iswordc() uses exe.book
   exe.book = curBook;

   RegProg* prog = compile(expr, flags);

   if (prog) {
      // Store the info needed to call regcomp() again when the engine turns out to be very slow 
      // when executing it.
      prog->flags  = flags;
   }

   return prog;
}

// Check if during the previous call to eeRegcomp the EOL item "$" has been found.
public Boole
regexContainsEol(RegProg* prog) {
   return prog->hadEol;
}


// Free a compiled regexp program, returned by compileRegexp().
public void
eeRegFree(RegProg* prog) {
   if (prog != NULL)
      freeBranch(prog);
}

#if defined(EXITFREE) || defined(PROTO)
public void
free_regexp_stuff(void) {
   ga_clear(&regstack);
   ga_clear(&backpos);
   eeglFree(reg_tofree);
   eeglFree(reg_prev_sub);
}
#endif

// Match a regexp against a string.
// "rmp->regprog" must be a compiled regexp as returned by compileRegexp().
// Note: "rmp->regprog" may be freed and changed.
// Uses curBook for line count and 'iskeyword'.
// When "nl" is true consider a "\n" in "line" to be a line break.
//
// Return true if there is a match, false if not.
private Boole
eeRegexec_string(
   RegMatch   *rmp,
   CS line,  // string to match against
   ColNr   col,    // column to start looking for match
   int      nl
){
   Execution   exeSaved;
   int      isBusyS_save = isBusyS;

   // Cannot use the same prog recursively, it contains state.
   if (rmp->regprog->re_in_use) {
      emsg(_(e_cannot_use_pattern_recursively));
      return false;
   }
   rmp->regprog->re_in_use = true;

   if (isBusyS)
      // Being called recursively, save the state.
      exeSaved = exe;
   isBusyS = true;

   exe.reg_startp = NULL;
   exe.reg_endp = NULL;
   exe.reg_startpos = NULL;
   exe.reg_endpos = NULL;

   int result = parseBranchexec_nl(rmp, line, col, nl);
   rmp->regprog->re_in_use = false;

   isBusyS = isBusyS_save;
   if (isBusyS)
      exe = exeSaved;

   return result > 0;
}

// Note: "*prog" may be freed and changed. Return true if there is a match, false if not.
public Boole
eeRegexec_prog(OUT RegProg** prog, Boole ignore_case, CS line, ColNr   col){
   RegMatch regmatch;
   regmatch.regprog = *prog;
   regmatch.rm_ic = ignore_case;
   int r = eeRegexec_string(&regmatch, line, col, false);
   *prog = regmatch.regprog;
   return r;
}

// Note: "rmp->regprog" may be freed and changed. Return true if there is a match, false if not.
public Boole
eeRegexec(RegMatch* rmp, Byte *line, ColNr col) {
   return eeRegexec_string(rmp, line, col, false);
}

// Like eeRegexec(), but consider a "\n" in "line" to be a line break.
// Note: "rmp->regprog" may be freed and changed. Return true if there is a match, false if not.
public int
eeRegexec_nl(RegMatch *rmp, Byte *line, ColNr col) {
   return eeRegexec_string(rmp, line, col, true);
}

// Match a regexp against multiple lines.
// "rmp->regprog" must be a compiled regexp as returned by compileRegexp().
// Note: "rmp->regprog" may be freed and changed, even set to NULL.
// Uses curBook for line count and 'iskeyword'.
//
// Return zero if there is no match.  Return number of lines contained in the match otherwise.
public long
eeRegexec_multi(
   RegMultilineMatch *rmp,
   Portal* port, // portal in which to search or NULL
   Book* book,  // book in which to search
   LineNr lnum, // nr of line to start looking for match
   ColNr col,   // column to start looking for match
   int* timed_out // flag is set when timeout limit reached
){
   int      result;
   Execution   exeSaved;
   int      isBusyS_save = isBusyS;

   // Cannot use the same prog recursively, it contains state.
   if (rmp->regprog->re_in_use) {
      emsg(_(e_cannot_use_pattern_recursively));
      return false;
   }
   rmp->regprog->re_in_use = true;

   if (isBusyS)
      // Being called recursively, save the state.
      exeSaved = exe;
   isBusyS = true;

   result = matchManyLines(rmp, port, book, lnum, col, timed_out);
   rmp->regprog->re_in_use = false;
   isBusyS = isBusyS_save;
   if (isBusyS)
      exe = exeSaved;

   return result <= 0 ? 0 : result;
}

// Get a number after a backslash that is inside []. When nothing is recognized return a backslash.
private Long
coll_get_char(void) {
   Long   nr = -1;

   switch (*regparse++) {
   case 'd': nr = getdecchrs(); break;
   case 'x': nr = gethexchrs(2); break;
   case 'u': nr = gethexchrs(4); break;
   case 'U': nr = gethexchrs(8); break;
   }
   if (nr < 0) {
      // If getting the number fails be backwards compatible: the character is a backslash.
      --regparse;
      nr = '\\';
   } ei (nr > INT_MAX)
      nr = INT_MAX;
   return nr;
}

//}}}
