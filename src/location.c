//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## location.c: functions for location lists (searches, errors from compilation, help greps etc)

#include "eegl.h"
#include <sys/stat.h> // for stat


typedef struct DirStack DirStack; 
struct DirStack {
   DirStack* next;
   CS dirname;
};


#define FORWARD_FILE    3
#define BACKWARD_FILE   7
#define STACK_CAPACITY 20


// For each error the next struct is allocated and linked in a list.
typedef struct LocLine LocLine;
struct LocLine {
   LocLine* next;   // pointer to next error in the list
   LocLine* prev;   // pointer to previous error in the list
   LineNr lNum;   // line number where the error occurred
   LineNr endLNum;   // line number when the error has range or zero
   int fNum;   // file number for the line
   int col;      // column where the error occurred
   int endCol;   // column when the error has range or zero
   int errNum;      // error number
   Arr(Byte) moduleName;   // module name for this error
   Arr(Byte) fName;   // different filename if there're hard links
   Arr(Byte) pattern;   // search pattern for the error
   Arr(Byte) text;   // description of the error
   Byte visCol;   // set to TRUE if col and endCol is screen column
   Byte isCleared;   // set to TRUE if line has been deleted
   Byte kind;   // type of the error (mostly 'E'); 1 for :helpgrep
   Var userData;   // custom user data associated with this item
   Byte isValid;   // valid error message detected
};

// There is a stack of location lists.
#define INVALID_LL_IND (-1)
#define INVALID_LL_BUFNR (0)

//Quickfix/Location list definition
//Contains a list of entries (LocLine). first points to the first entry
//and last points to the last entry. count contains the list size.
//
//Usually the list contains one or more entries. But an empty list can be
//created using setqflist()/setloclist() with a title and/or user context
//information and entries can be added later using setqflist()/setloclist().
typedef struct {
   Unt id;      // Unique identifier for this list
   LocLine* first;   // pointer to the first error
   LocLine* last;   // pointer to the last error
   LocLine* curr;   // pointer to the current error
   int count;   // number of errors (0 means empty list)
   int currentIdx;   // current index in the error list
   int noValidEntries;   // TRUE if not a single valid entry found
   int hasUserData; // TRUE if at least one item has user_data attached
   Arr(Byte) title;   // title derived from the command that created
            // the error list or set by setqflist
   Var* qf_ctx;   // context set by setqflist/setloclist
   Callback  textFn;   // 'quickfixtextfunc' callback function

   DirStack* dirStack;
   Arr(Byte) dir;
   DirStack* fileStack;
   Arr(Byte) currFName;
   int         qf_multiline;
   int         qf_multiignore;
   int         qf_multiscan;
   long      changedTick;
} LocationList;

//Quickfix/Location list stack definition
//Contains a list of location lists (LocationList)
struct LocationStack {
   // Count of references to this list. Used only for location lists.
   // When a location list portal reference this list, refcount
   // will be 2. Otherwise, refcount will be 1. When refcount
   // reaches 0, the list is freed.
   int refcount;
   int listcount;       // current number of lists
   int currList;       // current error list
   int cap;        // maximum number of lists
   Arr(LocationList) lists;
   int      bufNum;       // location portal's book number
};

private LocationStack *mainStackG;   // points to mainStackG_actual if memory allocation is successful.

private LocationStack locationStacksS[COUNT_LOC_LISTS];
private Unt lastUsedLlIdS = 0;   // Last used location list id

private Boole isMakeRunningS = false; // if a "make" job is in progress, then listId, else -1
private List* makeInProgressS; // the list of messages from a running "make" command


#define FMT_PATTERNS 14      // maximum number of % recognized


// Structure used to hold the info of one part of 'errorformat'
typedef struct ErrorFormatInfo ErrorFormatInfo;
struct ErrorFormatInfo {
    RegProg       *prog;   // pre-formatted part of 'errorformat'
    ErrorFormatInfo       *next;   // pointer to next (NULL if last)
    Byte       addr[FMT_PATTERNS]; // indices of used % patterns
    Byte       prefix;   // prefix of this format line:
            //   'D' enter directory
            //   'X' leave directory
            //   'A' start of multi-line message
            //   'E' error message
            //   'W' warning message
            //   'I' informational message
            //   'N' note message
            //   'C' continuation line
            //   'Z' end of multi-line message
            //   'G' general, unspecific message
            //   'P' push file (partial) message
            //   'Q' pop/quit file (partial) message
            //   'O' overread (partial) message
    Byte       flags;   // additional flags given in prefix
            //   '-' do not include this line
            //   '+' include whole line in message
    int          conthere;   // %> used
};

// List of location lists to be deleted.
// Used to delay the deletion of locations lists by autocmds.
typedef struct DeletionList DeletionList;
struct DeletionList {
    DeletionList* next;
    LocationStack      *stack;
};



// :vimgrep command arguments
typedef struct {
   long tomatch;   // maximum number of matches to find
   CS spat;      // search pattern
   Unt      flags;      // search modifier
   Arr(CS) fnames;   // list of files to search
   int fcount;      // number of files
   RegMultilineMatch   regmatch;   // compiled search pattern
   CS title;   // quickfix list title
} VimGrepArgs;

private DeletionList* deletionListG = NULL;

// Counter to prevent autocmds from freeing up location lists when they are
// still being used.
private int qfBusynessG = 0;

private ErrorFormatInfo   *fmt_start = NULL; // cached across qf_parse_line() calls

// callback function for 'quickfixtextfunc'
private Callback locationTextFnS;

private void pop(LocationStack *stack, int adjust);
private void push(LocationList newList, LocationStack* stack);
private void newLocList(LocationStack *stack, Byte *title);
private int addEntry(
      LocationList* ll, Byte* dir, Byte *fname, Byte *module, int bufnum, 
      Byte *mesg, long lnum, long end_lnum, int col, int end_col, int vis_col, 
      Byte *pattern, int nr, int type, Var *user_data, Boole valid
);
private void freeAList(LocationList *ll);
private CS createMsg(int, int);
private int   getBookNrForPath(LocationList *ll, Byte *, Byte *);
private Arr(Byte) pushDir(Byte *, DirStack **, int is_file_stack);
private CS popDir(DirStack **);
private CS guessFilepath(LocationList *ll, Byte *);
private void jumpToNewPortal(LocationStack *stack, Unt dir, int errornr, Boole forceit, Boole newPort
);
private void   formatText(ArrayList *gap, Byte *text);
private void   addRangeInformationToArrayList(ArrayList *gap, LocLine *lline);
private Boole   updatePortalPos(LocationStack *stack, int old_currentIdx);
private Portal   *findPortalIntoLocList(LocationStack *stack);
private Book   *findLlBook(LocationStack *stack);
private void   updateBook(LocationStack *stack, LocLine *oldLast);
private void fillBookWithLocList(LocationList *ll, Book* book, LocLine *oldLast, int getLlPortalId);
private Book   *loadDummyBook(Byte *fname, Byte *dirname_start, Byte *resulting_dir);
private void   wipeDummyBook(Book *book, Byte *dirname_start);
private void   unloadDummyBook(Book *book, Byte *dirname_start);
private int   entry_is_closer_to_target(
      LocLine *entry, LocLine *other_entry, int target_fnum, int target_lnum, int target_col
);
private Arr(Byte) vgr_get_auname(CommIndex id);

private int vimgrepProcessArgs(Invocation* invo, OUT VimGrepArgs* args);
private int elckGrepFiles(LocationStack*, VimGrepArgs*, OUT Boole*, OUT Book**, OUT CS*);
private void vgr_init_regmatch(RegMultilineMatch *regmatch, Byte *s);
private void updateChangedTick(LocationList *ll);

private LocationList* getCurrent(LocationStack *stack);

private void
jumpToFirstMatchAndUpdateDir(
   LocationStack* stack,
   Boole forceit,
   OUT Boole* redrawForDummy,
   OUT Book* firstMatchBook,
   CS target_dir);

// Quickfix portal check helper macro
#define isLocListPortalDOW(wp) (isLocationListBook((wp)->book) && (wp)->locationStackRef == NULL)
// Location list portal check helper macro
#define IS_LL_PORTAL(wp) (isLocationListBook((wp)->book) && (wp)->locationStackRef != NULL)

//Return location list for portal 'wp'
//For location list portal, return the referenced location list
#define GET_LOC_LIST(wp) (IS_LL_PORTAL(wp) ? (wp)->locationStackRef : NULL)

//Macro to loop through all the items in a quickfix list
//Quickfix item index starts from 1, so i below starts at 1
#define FOR_ALL_LL_ITEMS(ll, lline, i) \
          for (i = 1, lline = ll->first; \
             !gotInterruptG && i <= ll->count && lline != NULL; \
             ++i, lline = lline->next)

//Looking up a book can be slow if there are many.  Remember the last one
//to make this a lot faster if there are multiple matches in the same file.
private CS lastBookNameS = NULL;
private BookRef  last_bufref = {NULL, 0, 0};

private ArrayList tempList;


// Get a growarray to buffer text in.  Shared between various commands to avoid many alloc/free calls.
private ArrayList *
getTempList(void) {
   static int initialized = FALSE;

   if (!initialized) {
      initialized = TRUE;
      ga_init2(&tempList, 1, 256);
   }

   //Reset the length to zero.  Retain c from previous use to avoid many alloc/free calls.
   tempList.len = 0;

   return &tempList;
}

//The "tempList" arraylist buffer is reused across multiple loclist commands as
//a temporary buffer to reduce the number of alloc/free calls.  But if the
//buffer size is large, then to avoid holding on to that memory, clear the
//grow array.  Otherwise just reset the grow array length.
private void
clearArrayList(void) {
   if (tempList.cap > 1000)
      ga_clear(&tempList);
   else
      tempList.len = 0;
}

//Maximum number of bytes allowed per line while reading a errorfile.
#define LINE_MAXLEN 4096

//Patterns used.  Keep in sync with parseFormats[].
private struct fmtpattern{
   CS pattern;
   Byte   convchar;
} FORMAT_PATTERNS[FMT_PATTERNS] = { SMAP1((CS),
   ".\\+", 'f',      // only used when at end
   "\\d\\+", 'b',    // 1
   "\\d\\+", 'n',    // 2
   "\\d\\+", 'l',    // 3
   "\\d\\+", 'e',    // 4
   "\\d\\+", 'c',    // 5
   "\\d\\+", 'k',    // 6
   ".", 't',         // 7
#define FMT_PATTERN_M 8
   ".\\+", 'm',      // 8
#define FMT_PATTERN_R 9
   ".*", 'r',        // 9
   "[-    .]*", 'p', // 10
   "\\d\\+", 'v',    // 11
   ".\\+", 's',      // 12
   ".\\+", 'o'       // 13
)};

//Convert an errorformat pattern to a regular expression pattern.
//See FORMAT_PATTERNS definition above for the list of supported patterns.  The
//pattern specifier is supplied in "efmpat".  The converted pattern is stored
//in "regpat".  Returns a pointer to the location after the pattern.
private CS
convertErrorFormatToRegex(
   CS efmpat,
   CS regpat,
   ErrorFormatInfo* efminfo,
   int idx,
   int round
){
   CS srcptr;

   if (efminfo->addr[idx]) {
      // Each errorformat pattern can occur only once
      showErrFmtMsg(_(e_too_many_chr_in_format_string), *efmpat);
      return NULL;
   }
   if ((idx && idx < FMT_PATTERN_R
      && firstOccurrence((CS)"DXOPQ", efminfo->prefix) != NULL)
       || (idx == FMT_PATTERN_R
      && firstOccurrence((CS)"OPQ", efminfo->prefix) == NULL))
    {
      showErrFmtMsg(_(e_unexpected_chr_in_format_str), *efmpat);
      return NULL;
   }
   efminfo->addr[idx] = (Byte)++round;
   *regpat++ = '\\';
   *regpat++ = '(';
   if (*efmpat == 'f' && efmpat[1] != ZERO) {
      if (efmpat[1] != '\\' && efmpat[1] != '%') {
         // A file name may contain spaces, but this isn't
         // in "\f".  For "%f:%l:%m" there may be a ":" in
         // the file name.  Use ".\{-1,}x" instead (x is
         // the next character), the requirement that :999:
         // follows should work.
         STRCPY(regpat, ".\\{-1,}");
         regpat += 7;
      } else {
          // File name followed by '\\' or '%': include as
          // many file name chars as possible.
          STRCPY(regpat, "\\f\\+");
          regpat += 4;
      }
   } else {
      srcptr = FORMAT_PATTERNS[idx].pattern;
      while ((*regpat = *srcptr++) != ZERO)
          ++regpat;
   }
   *regpat++ = '\\';
   *regpat++ = ')';

   return regpat;
}

//Convert a scanf-like format in 'errorformat' to a regular expression.
//Return a pointer to the location after the pattern.
private CS
scanf_fmt_to_regpat(
   Byte** pefmp,
   CS efm,
   int   len,
   CS regpat
) {
   CS efmp = *pefmp;

   if (*efmp == '[' || *efmp == '\\') {
      if ((*regpat++ = *efmp) == '[')   { // %*[^a-z0-9] etc.
         if (efmp[1] == '^')
            *regpat++ = *++efmp;
         if (efmp < efm + len) {
            *regpat++ = *++efmp;       // could be ']'
         while (efmp < efm + len && (*regpat++ = *++efmp) != ']')
             // skip ;
         if (efmp == efm + len) {
             emsg(_(e_missing_rsb_in_format_string));
             return NULL;
         }
         }
      } ei (efmp < efm + len)   // %*\D, %*\s etc.
         *regpat++ = *++efmp;
      *regpat++ = '\\';
      *regpat++ = '+';
   } else {
      // TODO: scanf()-like: %*ud, %*3c, %*f, ... ?
      showErrFmtMsg(_(e_unsupported_chr_in_format_string), *efmp);
         return NULL;
   }

   *pefmp = efmp;

   return regpat;
}

//Analyze/parse an errorformat prefix.
private Byte *
efm_analyze_prefix(Byte *efmp, ErrorFormatInfo *efminfo){
   if (firstOccurrence((CS)"+-", *efmp) != NULL)
      efminfo->flags = *efmp++;
   if (firstOccurrence((CS)"DXAEWINCZGOPQ", *efmp) != NULL)
      efminfo->prefix = *efmp;
   else {
      showErrFmtMsg(_(e_invalid_chr_in_format_string_prefix), *efmp);
      return NULL;
   }

   return efmp;
}

//Convert a 'errorformat' string part in 'efm' to a regular expression
//pattern. The resulting regex pattern is returned in "regpat". Additional
//information about the 'erroformat' pattern is returned in "fmt_ptr". Return OK or FAIL.
private int
ErrorFormatInfoo_regpat(
   CS efm,
   int len,
   ErrorFormatInfo* fmt_ptr,
   CS regpat
){
   Byte   *efmp;
   Unt      idx = 0;

   // Build a regexp pattern for a 'errorformat' option part
   CS ptr = regpat;
   *ptr++ = '^';
   int round = 0;
   for (efmp = efm; efmp < efm + len; ++efmp) {
      if (*efmp == '%') {
         ++efmp;
         for (idx = 0; idx < FMT_PATTERNS; ++idx) {
            if (FORMAT_PATTERNS[idx].convchar == *efmp)
               break;
         } 
         if (idx < FMT_PATTERNS) {
            ptr = convertErrorFormatToRegex(efmp, ptr, fmt_ptr, idx, round);
            if (ptr == NULL)
                return FAIL;
            round++;
         } ei (*efmp == '*') {
         ++efmp;
         ptr = scanf_fmt_to_regpat(&efmp, efm, len, ptr);
         if (ptr == NULL)
             return FAIL;
         } ei (firstOccurrence((CS)"%\\.^$~[", *efmp) != NULL)
            *ptr++ = *efmp;      // regexp magic characters
         ei (*efmp == '#')
            *ptr++ = '*';
         ei (*efmp == '>')
            fmt_ptr->conthere = TRUE;
         ei (efmp == efm + 1) {     // analyse prefix
            // prefix is allowed only at the beginning of the errorformat
            // option part
            efmp = efm_analyze_prefix(efmp, fmt_ptr);
            if (efmp == NULL)
                return FAIL;
         } else {
            showErrFmtMsg(_(e_invalid_chr_in_format_string), *efmp);
            return FAIL;
         }
      } else {        // copy normal character
         if (*efmp == '\\' && efmp + 1 < efm + len)
            ++efmp;
         ei (firstOccurrence((CS)".*^$~[", *efmp) != NULL)
            *ptr++ = '\\';   // escape regexp atoms
         if (*efmp)
            *ptr++ = *efmp;
      }
   }
   *ptr++ = '$';
   *ptr = ZERO;

   return OK;
}

// Free the 'errorformat' information list
private void
free_efm_list(ErrorFormatInfo** efm_first){
   for (ErrorFormatInfo* efm_ptr = *efm_first; efm_ptr; efm_ptr = *efm_first) {
      *efm_first = efm_ptr->next;
      eeRegFree(efm_ptr->prog);
      eeglFree(efm_ptr);
   }
   fmt_start = NULL;
}

//Compute the size of the buffer used to convert a 'errorformat' pattern into
//a regular expression pattern.
private int
efm_regpat_bufsz(CS efm){
   int sz = (FMT_PATTERNS * 3) + ((int)STRLEN(efm) << 2);
   for (int i = FMT_PATTERNS; i > 0; )
      sz += (int)STRLEN(FORMAT_PATTERNS[--i].pattern);
   sz += 2; // "%f" can become two chars longer

   return sz;
}

//Return the length of a 'errorformat' option part (separated by ",").
private int
efm_option_part_len(Byte *efm){
   int len;

   for (len = 0; efm[len] != ZERO && efm[len] != ','; ++len) {
      if (efm[len] == '\\' && efm[len + 1] != ZERO)
          ++len;
   } 

   return len;
}

//Parse the 'errorformat' option. Multiple parts in the 'errorformat' option
//are parsed and converted to regular expressions. Returns information about
//the parsed 'errorformat' option.
private ErrorFormatInfo *
parse_efm_option(CS efm){
   ErrorFormatInfo* fmt_ptr = NULL;
   ErrorFormatInfo* fmtFirst = NULL;
   ErrorFormatInfo* fmt_last = NULL;
   CS fmtstr = NULL;
   int len;

   // Each part of the format string is copied and modified from errorformat
   // to regex prog.  Only a few % characters are allowed.

   // Get some space to modify the format string into.
   int sz = efm_regpat_bufsz(efm);
   if ((fmtstr = alloc_id(sz, aid_ll_efm_fmtstr)) == NULL)
      goto parse_efm_error;

   while (efm[0] != ZERO) {
      // Allocate a new eformat structure and put it at the end of the list
      fmt_ptr = ALLOC_CLEAR_ONE_ID(ErrorFormatInfo, aid_ll_efm_fmtpart);
      if (fmt_ptr == NULL)
         goto parse_efm_error;
      if (fmtFirst == NULL)       // first one
         fmtFirst = fmt_ptr;
      else
         fmt_last->next = fmt_ptr;
      fmt_last = fmt_ptr;

      // Isolate one part in the 'errorformat' option
      len = efm_option_part_len(efm);

      if (ErrorFormatInfoo_regpat(efm, len, fmt_ptr, fmtstr) == FAIL)
          goto parse_efm_error;
      if ((fmt_ptr->prog = compileRegexp(fmtstr, RE_MAGIC + RE_STRING)) == NULL)
          goto parse_efm_error;
      // Advance to next part
      efm = skip_to_option_part(efm + len);   // skip comma and spaces
   }

   if (fmtFirst == NULL)   // nothing found
      emsg(_(e_errorformat_contains_no_pattern));

   goto parse_efm_end;

parse_efm_error:
   free_efm_list(&fmtFirst);

parse_efm_end:
   eeglFree(fmtstr);

   return fmtFirst;
}

enum {
   QF_FAIL = 0,
   QF_OK = 1,
   QF_END_OF_INPUT = 2,
   QF_NOMEM = 3,
   QF_IGNORE_LINE = 4,
   QF_MULTISCAN = 5,
   QF_ABORT = 6
};

typedef enum {
   SOURCE_FILENAME, // a proto-source, so to speak - will be turned into SOURCE_FILE after opening
   SOURCE_FILE, // reading locations from file
   SOURCE_BOOK, // reading locations from an Eegl buffer
   SOURCE_STRING, // reading locations from a big ole string
   SOURCE_LIST // reading location from a Var containing a list of strings
} SourceKind;

typedef struct { // SOURCE_FILENAME
   CS c;
} FileNameSource;

typedef struct { // SOURCE_FILE
   FILE* c;
} FileSource;


typedef struct { // SOURCE_BOOK
   Book* c;
   LineNr start;
   LineNr end;
} BookSource;


typedef struct { // SOURCE_STRING
   CS c;
} StringSource;

typedef struct { // SOURCE_LIST
   ListItem* c;
} ListSource;

typedef struct { // A source can be a file, a Book, a string var or a list vaar
   SourceKind tag;
   union {
      FileNameSource FileName;
      FileSource File;
      BookSource Book;
      StringSource String;
      ListSource List;
   };
} Source;

// State information used to parse lines and add entries to a quickfix/location list.
typedef struct {
   Source source;
   CS linebuf;
   int      linelen;
   Byte   *growbuf;
   int      growbufsiz;
} LocationState;

//Allocate more memory for the line buffer used for parsing lines.
private CS
growLineBuffer(LocationState* state, int newsz) {
   CS  p;

   // If the line exceeds LINE_MAXLEN exclude the last
   // byte since it's not a NL character.
   state->linelen = newsz > LINE_MAXLEN ? LINE_MAXLEN - 1 : newsz;
   if (state->growbuf == NULL) {
      state->growbuf = alloc_id(state->linelen + 1, aid_ll_linebuf);
      if (state->growbuf == NULL)
         return NULL;
      state->growbufsiz = state->linelen;
   } ei (state->linelen > state->growbufsiz) {
      p = eeRealloc(state->growbuf, state->linelen + 1);
      state->growbuf = p;
      state->growbufsiz = state->linelen;
   }
   return state->growbuf;
}

//Get the next string (separated by newline) from a string source
private int
nextStringLine(LocationState *state) {
   // Get the next line from the supplied string
   CS inputString = state->source.String.c;
   if (*inputString== ZERO) // Reached the end of the string
      return QF_END_OF_INPUT;

   CS p = firstOccurrence(inputString, '\n');
   int len = p ? (int)(p - inputString) + 1 : (int)STRLEN(inputString);

   if (len > IOSIZE - 2) {
      state->linebuf = growLineBuffer(state, len);
      if (state->linebuf == NULL)
         return QF_NOMEM;
   } else {
      state->linebuf = IObuff;
      state->linelen = len;
   }
   copySubstrToAllocation(state->linebuf, (Text){inputString, state->linelen});

   // Increment using len in order to discard the rest of the
   // line if it exceeds LINE_MAXLEN.
   inputString += len;
   state->source.String.c = inputString;

   return QF_OK;
}

// Get the next string from the List items
private int
nextListLine(LocationState* state) {
   ListItem* listItem = state->source.List.c;

   while (listItem != NULL && (listItem->c.tag != VAR_STRING || listItem->c.string == NULL))
      listItem = listItem->next;   // Skip non-string items

   if (listItem == NULL) {     // End of the list
      state->source.List.c = NULL;
      return QF_END_OF_INPUT;
   }

   int len = (int)STRLEN(listItem->c.string);
   if (len > IOSIZE - 2) {
      state->linebuf = growLineBuffer(state, len);
      if (state->linebuf == NULL)
         return QF_NOMEM;
   } else {
      state->linebuf = IObuff;
      state->linelen = len;
   }

   copySubstrToAllocation(state->linebuf, (Text){listItem->c.string, state->linelen});

   state->source.List.c = listItem->next;   // next item
   return QF_OK;
}

//Get the next string from state->buf.
private int
nextBufLine(LocationState *state) {
   // Get the next line from the supplied buffer
  
   if (state->source.Book.start >= state->source.Book.end)
      return QF_END_OF_INPUT;

   CS p_buf = memGetLine(state->source.Book.c, state->source.Book.start, false);
   int len = memGetBookLen(state->source.Book.c, state->source.Book.start);
   state->source.Book.start++;

   if (len > IOSIZE - 2) {
      state->linebuf = growLineBuffer(state, len);
      if (state->linebuf == NULL)
         return QF_NOMEM;
   } else {
      state->linebuf = IObuff;
      state->linelen = len;
   }
   copySubstrToAllocation(state->linebuf, (Text){p_buf, state->linelen});

   return QF_OK;
}

// Get the next string when source = file.
private int
nextFileLine(LocationState *state) {
   if (fgets((char *)IObuff, IOSIZE, state->source.File.c) == NULL)
      return QF_END_OF_INPUT;

   Boole discard = false;
   state->linelen = (int)STRLEN(IObuff);
   if (state->linelen == IOSIZE - 1 && !(IObuff[state->linelen - 1] == '\n')) {
   
      // The current line exceeds IObuff, continue reading using
      // growbuf until EOL or LINE_MAXLEN bytes is read.
      if (state->growbuf == NULL) {
         state->growbufsiz = 2 * (IOSIZE - 1);
         state->growbuf = alloc_id(state->growbufsiz, aid_ll_linebuf);
         if (state->growbuf == NULL)
            return QF_NOMEM;
      }

      // Copy the read part of the line, excluding null-terminator
      memcpy(state->growbuf, IObuff, IOSIZE - 1);
      int growbuflen = state->linelen;

      for (;;) {
         if (fgets(
                  (char *)state->growbuf + growbuflen, 
                  state->growbufsiz - growbuflen, 
                  state->source.File.c) == NULL
         ) {
            break;
         } 
         state->linelen = (int)STRLEN(state->growbuf + growbuflen);
         growbuflen += state->linelen;
         if ((state->growbuf)[growbuflen - 1] == '\n')
            break;
         if (state->growbufsiz == LINE_MAXLEN) {
            discard = true;
            break;
         }

         state->growbufsiz = 2 * state->growbufsiz < LINE_MAXLEN 
            ? 2 * state->growbufsiz : LINE_MAXLEN;
         Arr(Byte) p;
         p = eeRealloc(state->growbuf, state->growbufsiz);
         state->growbuf = p;
     }

      while (discard) {
         // The current line is longer than LINE_MAXLEN, continue
         // reading but discard everything until EOL or EOF is reached.
         if (fgets((char *)IObuff, IOSIZE, state->source.File.c) == NULL
                || (int)STRLEN(IObuff) < IOSIZE - 1
                || IObuff[IOSIZE - 2] == '\n') {
            break;
         } 
      }

      state->linebuf = state->growbuf;
      state->linelen = growbuflen;
   } else
      state->linebuf = IObuff;

    return QF_OK;
}

// Get the next string from the source
private inline int
getNextLine(LocationState *state) {
   int status = QF_FAIL;
   switch (state->source.tag) {
   case SOURCE_FILE: {
      status = nextFileLine(state);
      break;
   }
   case SOURCE_BOOK: {
      status = nextBufLine(state);
      break;
   }
   case SOURCE_STRING: {
      status = nextStringLine(state);
      break;
   }
   case SOURCE_LIST: {
      status = nextListLine(state);
      break;
   }
   case SOURCE_FILENAME: break;
   }

   if (status != QF_OK)
      return status;

   // remove newline/CR from the line
   if (state->linelen > 0 && state->linebuf[state->linelen - 1] == '\n') {
      state->linebuf[state->linelen - 1] = ZERO;
   }

   return QF_OK;
}

typedef struct {
    CS namebuf;
    int      bnr;
    CS module;
    CS errmsg;
    int      errmsglen;
    long   lnum;
    long   end_lnum;
    int      col;
    int      end_col;
    Byte   use_viscol;
    CS pattern;
    int enr;
    int type;
    Var* user_data;
    int valid;
} Fields;

//Parse the match for filename ('%f') pattern in regmatch.
//Return the matched value in "fields->namebuf".
private int
qf_parse_fmt_f(RegMatch* rmp, int midx, Fields* fields, int prefix) {

   if (rmp->startp[midx] == NULL || rmp->endp[midx] == NULL)
      return QF_FAIL;

   // Expand ~/file and $HOME/file to full path.
   int c = *rmp->endp[midx];
   *rmp->endp[midx] = ZERO;
   doExpandEnv(OUT (Text){fields->namebuf, CMDBUFFSIZE}, rmp->startp[midx]);
   *rmp->endp[midx] = c;

   // For separate filename patterns (%O, %P and %Q), the specified file should exist.
   if (firstOccurrence((CS)"OPQ", prefix) != NULL
          && mch_getperm(fields->namebuf) == -1)
      return QF_FAIL;

   return QF_OK;
}

//Parse the match for buffer number ('%b') pattern in regmatch.
//Return the matched value in "fields->bnr".
private int
qf_parse_fmt_b(RegMatch* rmp, int midx, Fields* fields) {
   if (rmp->startp[midx] == NULL)
      return QF_FAIL;
   int bnr = (int)atol((char *)rmp->startp[midx]);
   if (bookFindFileByBookNr(bnr) == NULL)
      return QF_FAIL;
   fields->bnr = bnr;
   return QF_OK;
}

//Parse the match for error number ('%n') pattern in regmatch.
//Return the matched value in "fields->enr".
private int
qf_parse_fmt_n(RegMatch* rmp, int midx, Fields* fields) {
   if (rmp->startp[midx] == NULL)
      return QF_FAIL;
   fields->enr = (int)atol((char *)rmp->startp[midx]);
   return QF_OK;
}

//Parse the match for line number ('%l') pattern in regmatch.
//Return the matched value in "fields->lnum".
private int
qf_parse_fmt_l(RegMatch* rmp, int midx, Fields* fields) {
   if (rmp->startp[midx] == NULL)
      return QF_FAIL;
   fields->lnum = atol((char *)rmp->startp[midx]);
   return QF_OK;
}

//Parse the match for end line number ('%e') pattern in regmatch.
//Return the matched value in "fields->end_lnum".
private int
qf_parse_fmt_e(RegMatch* rmp, int midx, Fields* fields) {
   if (rmp->startp[midx] == NULL)
      return QF_FAIL;
   fields->end_lnum = atol((char *)rmp->startp[midx]);
   return QF_OK;
}

//Parse the match for column number ('%c') pattern in regmatch.
//Return the matched value in "fields->col".
private int
qf_parse_fmt_c(RegMatch* rmp, int midx, Fields* fields) {
   if (rmp->startp[midx] == NULL)
      return QF_FAIL;
   fields->col = (int)atol((char *)rmp->startp[midx]);
   return QF_OK;
}

//Parse the match for end column number ('%k') pattern in regmatch.
//Return the matched value in "fields->end_col".
private int
qf_parse_fmt_k(RegMatch* rmp, int midx, Fields* fields) {
   if (rmp->startp[midx] == NULL)
      return QF_FAIL;
   fields->end_col = (int)atol((char *)rmp->startp[midx]);
   return QF_OK;
}

//Parse the match for error type ('%t') pattern in regmatch.
//Return the matched value in "fields->type".
private int
qf_parse_fmt_t(RegMatch* rmp, int midx, Fields* fields) {
   if (rmp->startp[midx] == NULL)
      return QF_FAIL;
   fields->type = *rmp->startp[midx];
   return QF_OK;
}

//Copy a non-error line into the error string. Return the matched line in "fields->errmsg".
private int
copy_nonerror_line(CS linebuf, int linelen, Fields* fields) {
   if (linelen >= fields->errmsglen) {
      // linelen + null terminator
      CS p = eeRealloc(fields->errmsg, linelen + 1);
      fields->errmsg = p;
      fields->errmsglen = linelen + 1;
   }
   // copy whole line to error message
   copySubstrToAllocation(fields->errmsg, (Text){linebuf, linelen});

   return QF_OK;
}

//Parse the match for error message ('%m') pattern in regmatch.
//Return the matched value in "fields->errmsg".
private int
qf_parse_fmt_m(RegMatch* rmp, int midx, Fields* fields) {
   CS p;

   if (rmp->startp[midx] == NULL || rmp->endp[midx] == NULL)
      return QF_FAIL;
   int len = (int)(rmp->endp[midx] - rmp->startp[midx]);
   if (len >= fields->errmsglen) {
      // len + null terminator
      p = eeRealloc(fields->errmsg, len + 1);
      fields->errmsg = p;
      fields->errmsglen = len + 1;
   }
   copySubstrToAllocation(fields->errmsg, (Text){rmp->startp[midx], len});
   return QF_OK;
}

//Parse the match for rest of a single-line file message ('%r') pattern.
//Return the matched value in "tail".
private int
qf_parse_fmt_r(RegMatch* rmp, int midx, OUT CS* tail) {
   if (rmp->startp[midx] == NULL)
      return QF_FAIL;
   *tail = rmp->startp[midx];
   return QF_OK;
}

//Parse the match for the pointer line ('%p') pattern in regmatch.
//Return the matched value in "fields->col".
private int
qf_parse_fmt_p(RegMatch* rmp, int midx, Fields* fields) {
   if (rmp->startp[midx] == NULL || rmp->endp[midx] == NULL)
      return QF_FAIL;
   fields->col = 0;
   for (CS match_ptr = rmp->startp[midx]; match_ptr != rmp->endp[midx]; ++match_ptr) {
      ++fields->col;
      if (*match_ptr == TAB) {
         fields->col += 7;
         fields->col -= fields->col % 8;
      }
   }
   ++fields->col;
   fields->use_viscol = TRUE;
   return QF_OK;
}

//Parse the match for the virtual column number ('%v') pattern in regmatch.
//Return the matched value in "fields->col".
private int
qf_parse_fmt_v(RegMatch* rmp, int midx, Fields* fields) {
   if (rmp->startp[midx] == NULL)
      return QF_FAIL;
   fields->col = (int)atol((char *)rmp->startp[midx]);
   fields->use_viscol = TRUE;
   return QF_OK;
}

//Parse the match for the search text ('%s') pattern in regmatch.
//Return the matched value in "fields->pattern".
private int
qf_parse_fmt_s(RegMatch* rmp, int midx, Fields* fields) {
   if (rmp->startp[midx] == NULL || rmp->endp[midx] == NULL)
      return QF_FAIL;
   int len = (int)(rmp->endp[midx] - rmp->startp[midx]);
   if (len > CMDBUFFSIZE - 5)
      len = CMDBUFFSIZE - 5;
   STRCPY(fields->pattern, "^\\V");
   STRNCAT(fields->pattern, rmp->startp[midx], len);
   fields->pattern[len + 3] = '\\';
   fields->pattern[len + 4] = '$';
   fields->pattern[len + 5] = ZERO;
   return QF_OK;
}

//Parse the match for the module ('%o') pattern in regmatch.
//Return the matched value in "fields->module".
private int
qf_parse_fmt_o(RegMatch* rmp, int midx, Fields* fields) {
   if (rmp->startp[midx] == NULL || rmp->endp[midx] == NULL)
      return QF_FAIL;
   int len = (int)(rmp->endp[midx] - rmp->startp[midx]);
   if (len > CMDBUFFSIZE)
      len = CMDBUFFSIZE;
   STRNCAT(fields->module, rmp->startp[midx], len);
   return QF_OK;
}

//'errorformat' format pattern parser functions.
//The '%f' and '%r' formats are parsed differently from other formats.
//See parseErrorFormatMatch() for details.
//Keep in sync with FORMAT_PATTERNS[].
private int (*parseFormats[FMT_PATTERNS])(RegMatch *, int, Fields *) = {
   NULL, // %f
   qf_parse_fmt_b,
   qf_parse_fmt_n,
   qf_parse_fmt_l,
   qf_parse_fmt_e,
   qf_parse_fmt_c,
   qf_parse_fmt_k,
   qf_parse_fmt_t,
   qf_parse_fmt_m,
   NULL, // %r
   qf_parse_fmt_p,
   qf_parse_fmt_v,
   qf_parse_fmt_s,
   qf_parse_fmt_o
};

//Parse the error format pattern matches in "regmatch" and set the values in
//"fields".  fmt_ptr contains the 'efm' format specifiers/prefixes that have a
//match.  Returns QF_OK if all the matches are successfully parsed. On
//failure, returns QF_FAIL or QF_NOMEM.
private int
parseErrorFormatMatch(
   CS linebuf,
   int linelen,
   ErrorFormatInfo* fmt_ptr,
   RegMatch* regmatch,
   Fields* fields,
   int qf_multiline,
   int qf_multiscan,
   OUT CS* tail
){
   int idx = fmt_ptr->prefix;
   int midx;
   int status;

   if ((idx == 'C' || idx == 'Z') && !qf_multiline)
      return QF_FAIL;
   fields->type = (firstOccurrence((CS)"EWIN", idx) != NULL) ? idx : 0;

   // Extract error message data from matched line.
   // We check for an actual submatch, because "\[" and "\]" in
   // the 'errorformat' may cause the wrong submatch to be used.
   for (int i = 0; i < FMT_PATTERNS; i++) {
      status = QF_OK;
      midx = (int)fmt_ptr->addr[i];
      if (i == 0 && midx > 0)            // %f
          status = qf_parse_fmt_f(regmatch, midx, fields, idx);
      ei (i == FMT_PATTERN_M) {
         if (fmt_ptr->flags == '+' && !qf_multiscan)   // %+
            status = copy_nonerror_line(linebuf, linelen, fields);
         ei (midx > 0)            // %m
            status = qf_parse_fmt_m(regmatch, midx, fields);
      } ei (i == FMT_PATTERN_R && midx > 0)   // %r
         status = qf_parse_fmt_r(regmatch, midx, tail);
      ei (midx > 0)            // others
         status = (parseFormats[i])(regmatch, midx, fields);

      if (status != QF_OK)
         return status;
   }

   return QF_OK;
}

//Parse an error line in 'linebuf' using a single error format string in
//'fmt_ptr->prog' and return the matching values in 'fields'.
//Return QF_OK if the efm format matches completely and the fields are
//successfully copied. Otherwise returns QF_FAIL or QF_NOMEM.
private int
qf_parse_get_fields(
   CS linebuf,
   int linelen,
   ErrorFormatInfo* fmt_ptr,
   Fields* fields,
   int qf_multiline,
   int qf_multiscan,
   OUT CS* tail
) {
   RegMatch   regmatch;
   int      status = QF_FAIL;

   if (qf_multiscan && firstOccurrence((CS)"OPQ", fmt_ptr->prefix) == NULL)
      return QF_FAIL;

   fields->namebuf[0] = ZERO;
   fields->bnr = 0;
   fields->module[0] = ZERO;
   fields->pattern[0] = ZERO;
   if (!qf_multiscan)
      fields->errmsg[0] = ZERO;
   fields->lnum = 0;
   fields->end_lnum = 0;
   fields->col = 0;
   fields->end_col = 0;
   fields->use_viscol = FALSE;
   fields->enr = -1;
   fields->type = 0;
   *tail = NULL;

   // Always ignore case when looking for a matching error.
   regmatch.rm_ic = TRUE;
   regmatch.regprog = fmt_ptr->prog;
   int r = eeRegexec(&regmatch, linebuf, (ColNr)0);
   fmt_ptr->prog = regmatch.regprog;
   if (r) {
      status = parseErrorFormatMatch(
            linebuf, linelen, fmt_ptr, &regmatch, fields, qf_multiline, qf_multiscan, tail
      );
   } 

   return status;
}

//Parse directory error format prefixes (%D and %X).
//Push and pop directories from the directory stack when scanning directory names.
private int
parse_dir_pfx(int idx, Fields* fields, LocationList *ll) {
   if (idx == 'D')   {         // enter directory
      if (*fields->namebuf == ZERO) {
         emsg(_(e_missing_or_empty_directory_name));
         return QF_FAIL;
      }
      ll->dir = pushDir(fields->namebuf, &ll->dirStack, FALSE);
      if (ll->dir == NULL)
         return QF_FAIL;
   } ei (idx == 'X')         // leave directory
      ll->dir = popDir(&ll->dirStack);

   return QF_OK;
}

// Parse global file name error format prefixes (%O, %P and %Q).
private int
parse_file_pfx(
   int idx,
   Fields* fields,
   LocationList* ll,
   CS tail
) {
   fields->valid = FALSE;
   if (*fields->namebuf == ZERO || mch_getperm(fields->namebuf) >= 0) {
      if (*fields->namebuf && idx == 'P')
        ll->currFName = pushDir(fields->namebuf, &ll->fileStack, TRUE);
      ei (idx == 'Q')
        ll->currFName = popDir(&ll->fileStack);
      *fields->namebuf = ZERO;
      if (tail && *tail) {
          STRMOVE(IObuff, skipwhite(tail));
          ll->qf_multiscan = TRUE;
          return QF_MULTISCAN;
      }
   }

   return QF_OK;
}

// Parse a non-error line (a line which doesn't match any of the error format in 'efm').
private int
qf_parse_line_nomatch(CS linebuf, int linelen, Fields* fields) {
   fields->namebuf[0] = ZERO;   // no match found, remove file name
   fields->lnum = 0;      // don't jump to this line
   fields->valid = FALSE;

   return copy_nonerror_line(linebuf, linelen, fields);
}

//Parse multi-line error format prefixes (%C and %Z)
private int
qf_parse_multiline_pfx(
   int idx,
   LocationList* ll,
   Fields* fields
) {
   CS ptr;
   if (!ll->qf_multiignore) {
      LocLine* qfprev = ll->last;
      if (!qfprev)
          return QF_FAIL;
          
      int len;
      if (*fields->errmsg && !ll->qf_multiignore) {
         len = (int)STRLEN(qfprev->text);
         ptr = alloc_id(len + STRLEN(fields->errmsg) + 2, aid_ll_multiline_pfx);
         STRCPY(ptr, qfprev->text);
         eeglFree(qfprev->text);
         qfprev->text = ptr;
         *(ptr += len) = '\n';
         STRCPY(++ptr, fields->errmsg);
      }
      if (qfprev->errNum == -1)
          qfprev->errNum = fields->enr;
      if (eeIsPrintable(fields->type) && !qfprev->kind)
          // only printable chars allowed
          qfprev->kind = fields->type;

      if (!qfprev->lNum)
          qfprev->lNum = fields->lnum;
      if (!qfprev->endLNum)
          qfprev->endLNum = fields->end_lnum;
      if (!qfprev->col) {
          qfprev->col = fields->col;
          qfprev->visCol = fields->use_viscol;
      }
      if (!qfprev->endCol)
          qfprev->endCol = fields->end_col;
      if (!qfprev->fNum)
          qfprev->fNum = getBookNrForPath(ll,
             ll->dir,
             *fields->namebuf || ll->dir != NULL
             ? fields->namebuf
             : ll->currFName != NULL && fields->valid
             ? ll->currFName : 0);
   }
   if (idx == 'Z')
      ll->qf_multiline = ll->qf_multiignore = FALSE;
   line_breakcheck();

   return QF_IGNORE_LINE;
}

//Parse a line and get the quickfix fields. Return the QF_ status.
private int
qf_parse_line(
   LocationList* ll,
   CS linebuf,
   int linelen,
   ErrorFormatInfo* fmtFirst,
   Fields* fields
){
   ErrorFormatInfo* fmt_ptr;
   int idx = 0;
   CS tail = NULL;
   int status;

restofline:
   // If there was no %> item start at the first pattern
   if (fmt_start == NULL)
      fmt_ptr = fmtFirst;
   else {
      // Otherwise start from the last used pattern
      fmt_ptr = fmt_start;
      fmt_start = NULL;
   }

   // Try to match each part of 'errorformat' until we find a complete match or no match.
   fields->valid = TRUE;
   for ( ; fmt_ptr; fmt_ptr = fmt_ptr->next) {
      idx = fmt_ptr->prefix;
      status = qf_parse_get_fields(linebuf, linelen, fmt_ptr, fields,
               ll->qf_multiline, ll->qf_multiscan, OUT &tail);
      if (status == QF_NOMEM)
         return status;
      if (status == QF_OK)
         break;
   }
   ll->qf_multiscan = FALSE;

   if (fmt_ptr == NULL || idx == 'D' || idx == 'X') {
      if (fmt_ptr != NULL) {
          // 'D' and 'X' directory specifiers
          status = parse_dir_pfx(idx, fields, ll);
          if (status != QF_OK)
         return status;
      }

      status = qf_parse_line_nomatch(linebuf, linelen, fields);
      if (status != QF_OK)
          return status;

      if (fmt_ptr == NULL)
          ll->qf_multiline = ll->qf_multiignore = FALSE;
   } ei (fmt_ptr != NULL) {
      // honor %> item
      if (fmt_ptr->conthere)
         fmt_start = fmt_ptr;

      if (firstOccurrence((CS)"AEWIN", idx) != NULL) {
         ll->qf_multiline = TRUE;   // start of a multi-line message
         ll->qf_multiignore = FALSE;// reset continuation
      } ei (firstOccurrence((CS)"CZ", idx) != NULL) {
                  // continuation of multi-line msg
         status = qf_parse_multiline_pfx(idx, ll, fields);
         if (status != QF_OK)
            return status;
      } ei (firstOccurrence((CS)"OPQ", idx) != NULL) {
                  // global file names
         status = parse_file_pfx(idx, fields, ll, tail);
         if (status == QF_MULTISCAN)
            goto restofline;
      }
      if (fmt_ptr->flags == '-') {  // generally exclude this line
         if (ll->qf_multiline)
            // also exclude continuation lines
            ll->qf_multiignore = TRUE;
         return QF_IGNORE_LINE;
      }
   }

   return QF_OK;
}

private int
isStackEmpty(LocationStack* stack) {
   return stack == NULL || stack->listcount <= 0;
}

private int
isEmpty(LocationList* ll) {
   return ll == NULL || ll->count <= 0;
}

// Returns TRUE if the specified location list is not empty and has valid entries.
private int
listHasValidEntries(LocationList* ll) {
    return !isEmpty(ll) && !ll->noValidEntries;
}

// Return a pointer to a list in the specified quickfix stack
private LocationList *
getList(LocationStack* stack, int idx) {
    return stack->lists + idx;
}

// Allocate the fields used for parsing lines and populating a quickfix list.
private int
llAllocateFields(Fields *pfields){
   pfields->namebuf = alloc_id(CMDBUFFSIZE + 1, aid_ll_namebuf);
   pfields->module = alloc_id(CMDBUFFSIZE + 1, aid_ll_module);
   pfields->errmsglen = CMDBUFFSIZE + 1;
   pfields->errmsg = alloc_id(pfields->errmsglen, aid_ll_errmsg);
   pfields->pattern = alloc_id(CMDBUFFSIZE + 1, aid_ll_pattern);
   if (pfields->namebuf == NULL || pfields->errmsg == NULL
         || pfields->pattern == NULL || pfields->module == NULL)
      return FAIL;

   return OK;
}

//Free the fields used for parsing lines and populating a quickfix list.
private void
freeAList_fields(Fields* pfields) {
   eeglFree(pfields->namebuf);
   eeglFree(pfields->module);
   eeglFree(pfields->errmsg);
   eeglFree(pfields->pattern);
}

// Setup the state information used for parsing lines and populating a quickfix list.
private int
setupState(
   Source source,
   OUT LocationState* locState
){
   if (source.tag == SOURCE_FILENAME) {
      FILE* fd = fopen((char *)source.FileName.c, "rw");
      if (fd == NULL) {
         showErrFmtMsg(_(e_cant_open_errorfile_str), source.FileName.c);
         return FAIL;
      }
      locState->source = (Source){.tag = SOURCE_FILE, .File = (FileSource){.c = fd}};
   } 
   locState->source = source;
   return OK;
}

// Cleanup the state information used for parsing lines and populating a quickfix list.
private void
cleanupState(LocationState *locState) {
   Source source = locState->source;
   switch (source.tag) {
   case SOURCE_FILE: {
         if (source.File.c != NULL) {
            fclose(source.File.c);
         } 
      }
   default: break; 
   }
   
   eeglFree(locState->growbuf);
}

//Process the next line from a file/book/list/string and add it to the location list 'll'.
private int
processNextLine(LocationList* ll, ErrorFormatInfo* fmtFirst, LocationState* state, Fields* fields){
   // Get the next line from a file/book/list/string
   int status = getNextLine(state);
   if (status != QF_OK)
      return status;

   status = qf_parse_line(ll, state->linebuf, state->linelen, fmtFirst, fields);
   if (status != QF_OK)
      return status;

   return addEntry(ll,
      ll->dir,
      (*fields->namebuf || ll->dir != NULL)
         ? fields->namebuf : (
            (ll->currFName != NULL && fields->valid) ? ll->currFName : (CS)NULL
         ),
      fields->module,
      fields->bnr,
      fields->errmsg,
      fields->lnum,
      fields->end_lnum,
      fields->col,
      fields->end_col,
      fields->use_viscol,
      fields->pattern,
      fields->enr,
      fields->type,
      fields->user_data,
      fields->valid
   );
}

//Read the location list from a source (file, string, book or list of strings).
//Always use 'errorformat' from "buf" if there is a local value.
//Return -1 for error, number of list items for success.
private int
initWorker(
   Source source,
   OUT LocationStack* stack,
   int ind,
   CS errorformat,
   Boole newlist,      // TRUE: start a new location list
   CS title
) {
   Fields fields;
   LocLine* oldLast = NULL;
   int adding = FALSE;
   static ErrorFormatInfo* fmtFirst = NULL;
   Byte       *efm;
   static Byte   *last_efm = NULL;
   int          retval = -1;   // default: return error flag
   int          status;

   // Do not used the cached book, it may have been wiped out.
   EE_CLEAR(lastBookNameS);

   LocationState state;
   CLEAR_FIELD(state);
   CLEAR_FIELD(fields);
   if ((llAllocateFields(&fields) == FAIL)
        || (setupState(source, OUT &state) == FAIL)) {
      goto initEnd;
   } 

   LocationList* ll;
   if (newlist || ind == stack->listcount) {
      // make place for a new list
      newLocList(stack, title);
      ind = stack->currList;
      ll = getList(stack, ind);
   } else {
      // Adding to existing list, use last entry.
      adding = TRUE;
      ll = getList(stack, ind);
      if (!isEmpty(ll))
         oldLast = ll->last;
   }

   // Use the local value of 'errorformat' if it's set.
   if (source.tag == SOURCE_BOOK && source.Book.c->o.errorFormat != ZERO)
      efm = source.Book.c->o.errorFormat;
   else
      efm = errorformat;

   // If the errorformat didn't change between calls, then reuse the previously parsed values.
   if (last_efm == NULL || (STRCMP(last_efm, efm) != 0)) {
      // free the previously parsed data
      EE_CLEAR(last_efm);
      free_efm_list(&fmtFirst);

      // parse the current 'efm'
      fmtFirst = parse_efm_option(efm);
      if (fmtFirst)
         last_efm = copyStr(efm);
   }

   if (fmtFirst == NULL)   // nothing found
      goto error2;

   // gotInterruptG is reset here, because it was probably set when killing the
   // ":make" command, but we still want to read the errorfile then.
   gotInterruptG = FALSE;

   // Read the lines in the error file one by one.
   // Try to recognize one of the error formats in each line.
   while (!gotInterruptG) {
      status = processNextLine(ll, fmtFirst, &state, &fields);
      if (status == QF_NOMEM)      // memory alloc failure
         goto initEnd;
      if (status == QF_END_OF_INPUT)   // end of input
         break;
      if (status == QF_FAIL)
         goto error2;

      line_breakcheck();
   }
   if (state.source.tag != SOURCE_FILE || !ferror(state.source.File.c)) {
      if (ll->currentIdx == 0) {
         // no valid entry found
         ll->curr = ll->first;
         ll->currentIdx = 1;
         ll->noValidEntries = TRUE;
      } else {
         ll->noValidEntries = FALSE;
         if (ll->curr == NULL)
            ll->curr = ll->first;
      }
      // return number of matches
      retval = ll->count;
      goto initEnd;
   }
   emsg(_(e_error_while_reading_errorfile));
   
error2:
   if (!adding) {
      // Error when creating a new list. Free the new list
      freeAList(ll);
      stack->listcount--;
      if (stack->currList > 0)
         --stack->currList;
   }
   
initEnd:
   push(*ll, stack);
   if (ind == stack->currList)
      updateBook(stack, oldLast);
   cleanupState(&state);
   freeAList_fields(&fields);

   return retval;
}

// Initialize or create a new location list in a stack and update its "change" tick
private int
initAndUpdateTick(
   Source source, OUT LocationStack* stack, CS errorFormat, Boole startNewList, CS title
) {
   int res = initWorker(source, stack, stack->currList, errorFormat, startNewList, title);
   if (res >= 0)
      updateChangedTick(getCurrent(stack));
   return res; 
}

//Read the errorfile "errorFName" into memory, line by line, building the error list.
//Return -1 for error, number of lines for success.
int
llInitFromFile(
   OUT LocationStack* st,
   CS errorFName,
   CS errorformat,
   Boole       newlist,      // TRUE: start a new error list
   CS title
) {
   Source source = (Source){.tag = SOURCE_FILENAME, .FileName = {.c = errorFName}};
   return initAndUpdateTick(source, OUT st, errorformat, newlist > 0, title);
}

// Set the title of the specified location list. Frees the previous title. Prepends ':' to the title
private void
storeTitle(LocationList* ll, CS title) {
   EE_CLEAR(ll->title);
   if (title == NULL)
      return;

   CS p = alloc_id(STRLEN(title) + 2, aid_ll_title);

   ll->title = p;
   if (p)
      STRCPY(p, title);
}

//The title of a location list is set, by default, to the command that created the 
//location list with the ":" prefix. Create a location list title string by prepending ":" to 
//a user command. Returns a pointer to a static buffer with the title.
private CS
copyCommandTitle(CS cmd) {
   static Byte llTitle[IOSIZE];

   eeSnprintf(llTitle, IOSIZE, ":%s", cmd);
   return llTitle;
}

// Return a pointer to the current list in the specified location stack
private LocationList *
getCurrent(LocationStack* stack) {
   return getList(stack, stack->currList);
}

//Pop a location list from the location list stack Automatically adjust currList so that it stays 
//pointed to the same list, unless it is deleted, if so then use the newest created list instead. 
//listcount will be set correctly. The above will only happen if <adjust> is TRUE.
private void
pop(LocationStack* stack, int adjust) {
   freeAList(&stack->lists[0]);
   for (Unt i = 1; i < (Unt)stack->listcount; ++i)
      stack->lists[i - 1] = stack->lists[i];

   // fill with zeroes now unused list at the top
   memset(stack->lists + stack->listcount - 1, 0, sizeof(*stack->lists));

   if (adjust) {
      stack->listcount--;
      if (stack->currList == 0)
         stack->currList = stack->listcount - 1;
      else
         stack->currList--;
   }
}

// Push a location list onto the top of the location stack. If it's full, free the 0th list in it
// and shift all the lists down, then add the new one on top.
private void
push(LocationList newList, LocationStack* stack) {
   int i;
   if (stack->listcount + 1 < STACK_CAPACITY) {
      stack->lists[stack->listcount] = newList;
      stack->currList = stack->listcount;
      stack->listcount++;
   } else {
      LocationList* toFree = stack->lists;
      for (i = 1; i < stack->listcount; ++i)
         stack->lists[i - 1] = stack->lists[i];
      freeAList(toFree);
      stack->lists[STACK_CAPACITY - 1] = newList;   
   }
}

//Prepare for adding a new location list. If the current list is in the middle of the stack, then 
//all the following lists are smashed and then the new list is added.
private void
newLocList(LocationStack* stack, CS title) {
   LocationList* ll;

   //If the current entry is not the last entry, delete entries beyond
   //the current entry. This makes it possible to browse in a tree-like way with ":grep".
   while (stack->listcount > stack->currList + 1)
      freeAList(&stack->lists[--stack->listcount]);

   // When the stack is full, remove to oldest entry Otherwise, add a new entry.
   if (stack->listcount == stack->cap) {
      pop(stack, FALSE);
      stack->currList = stack->listcount - 1; // point to new empty list
   } else
      stack->currList = stack->listcount++;

   ll = getCurrent(stack);
   CLEAR_POINTER(ll);
   storeTitle(ll, title);
   ll->id = ++lastUsedLlIdS;
   ll->hasUserData = FALSE;
}

//Queue location list stack delete request.
private void
locstack_queue_delreq(LocationStack* stack) {
   DeletionList* q = ALLOC_ONE(DeletionList);

   q->stack = stack;
   q->next = deletionListG;
   deletionListG = q;
}

// Return the global location stack portal buffer number.
int
qf_stack_get_bufnr(void) {
   if (mainStackG == NULL)
      return INVALID_LL_BUFNR;
   return mainStackG->bufNum;
}

// Wipe the location portal buffer (if present) for the specified location list.
private void
wipeLlBook(LocationStack* stack) {
   if (stack->bufNum == INVALID_LL_BUFNR)
      return;

   Book* llBook = bookFindFileByBookNr(stack->bufNum);
   if (llBook && llBook->countPortals == 0) {
      int buf_was_null = FALSE;
      // can happen when curPor is going to be closed e.g. curPor->book
      // was already closed in closePortal(), and we are now closing the
      // portal related location list buffer from win_free_mem()
      // but closeBook() calls CHECK_CURBOOK() macro and requires curPor->book == curBook
      if (curPor->book == NULL) {
         curPor->book = curBook;
         buf_was_null = TRUE;
      }

      // If the location buffer is not loaded in any portal, then wipe the buffer.
      closeBook(NULL, llBook, DOBOOK_WIPE, FALSE, FALSE);
      stack->bufNum = INVALID_LL_BUFNR;
      if (buf_was_null)
          curPor->book = NULL;
    }
}


//Free all lists in the stack (not including the stack)
private void
freeAList_list_stack_items(LocationStack *stack) {
   for (int i = 0; i < stack->listcount; ++i)
      freeAList(getList(stack, i));
}

//Free a LocationStack struct completely
private void
freeAList_lists(LocationStack* stack) {
   freeAList_list_stack_items(stack);

   eeglFree(stack->lists);
   eeglFree(stack);
}

// Free a location list stack
private void
ll_free_all(LocationStack** pqi) {
   LocationStack* stack = *pqi;
   if (!stack)
      return;
   *pqi = NULL;   // Remove reference to this list

   // If the location list is still in use, then queue the delete request to be processed later.
   if (qfBusynessG > 0) {
      locstack_queue_delreq(stack);
      return;
   }

   stack->refcount--;
   if (stack->refcount < 1) {
      // No references to this location list. If the location portal buffer is loaded, then wipe it
      wipeLlBook(stack);

      freeAList_lists(stack);
   }
}

// Free all the location lists in the stack.
//private void
//freeAllLocLists(int stackInd) {
//   freeAList_list_stack_items(locationStacksS + stackInd);
//}

//Delay freeing of location list stacks when the location code is running.
//Used to avoid problems with autocmds freeing location list stacks when the
//location code is still referencing the stack.
//Must always call decrementLlBusyness() exactly once after this.
private void
incrementLlBusyness(void) {
   qfBusynessG++;
}

// Safe to free location list stacks. Process any delayed delete requests.
private void
decrementLlBusyness(void) {
   if (--qfBusynessG == 0) {
      // No longer referencing the location lists. Process all the pending delete requests
      while (deletionListG != NULL) {
          DeletionList* q = deletionListG;

          deletionListG = q->next;
          ll_free_all(&q->stack);
          eeglFree(q);
      }
   }
#ifdef ABORT_ON_INTERNAL_ERROR
  if (qfBusynessG < 0) {
      emsg("qfBusynessG has become negative");
      abort();
   }
#endif
}

#if defined(EXITFREE) || defined(PROTO)
void
check_qfBusynessG(void) {
   if (qfBusynessG != 0) {
      showErrFmtMsg("qfBusynessG not zero on exit: %ld", (long)qfBusynessG);
# ifdef ABORT_ON_INTERNAL_ERROR
      abort();
# endif
   }
}
#endif

//{{{creation

//Add an entry with source code link to the end of the list of errors.
//Return QF_OK on success or QF_FAIL on a memory allocation failure.
private int
addEntry(
   LocationList* ll,
   CS dir,      // optional directory name
   CS fname,      // file name or NULL
   CS module,   // module name or NULL
   int bufnum,      // buffer number or zero
   CS mesg,      // message
   long lnum,      // source code line number
   long end_lnum,   // source code end line number
   int col,      // column
   int end_col,   // column for end
   int vis_col,   // using visual column
   CS pattern,   // search pattern
   int nr,      // error number
   int type,      // type character
   Var* user_data,     // custom user data or NULL
   Boole valid      // valid entry
){
   Book* book;
   LocLine* lline;
   LocLine** lastp;   // pointer to last or NULL
   CS p = NULL;

   if ((lline = ALLOC_ONE_ID(LocLine, aid_ll_line)) == NULL)
      return QF_FAIL;
   if (bufnum != 0) {
      book = bookFindFileByBookNr(bufnum);

      lline->fNum = bufnum;
      if (book)
         book->hasLocationEntry = true;
   } else {
      lline->fNum = getBookNrForPath(ll, dir, fname);
      book = bookFindFileByBookNr(lline->fNum);
   }
   CS fullname = FullName_save(fname, true);
   lline->fName = NULL;
   if (book && book->fullFileName && fullname) {
      if (fnamecmp(fullname, book->fullFileName) != 0) {
         p = shorten_fname1(fullname);
         if (p)
            lline->fName = copyStr(p);
      }
   }
   eeglFree(fullname);
   lline->text = copyStr(mesg);
   lline->lNum = lnum;
   lline->endLNum = end_lnum;
   lline->col = col;
   lline->endCol = end_col;
   lline->visCol = vis_col;
   if (user_data == NULL || user_data->tag == VAR_UNKNOWN)
      lline->userData.tag = VAR_UNKNOWN;
   else {
      copy_tv(OUT &lline->userData, user_data);
      ll->hasUserData = TRUE;
   }
   if (!pattern || *pattern == ZERO)
      lline->pattern = NULL;
   else 
      lline->pattern = copyStr(pattern);
      
   if (!module || *module == ZERO)
      lline->moduleName = NULL;
   else 
      lline->moduleName = copyStr(module);
   lline->errNum = nr;
   if (type != 1 && !eeIsPrintable(type)) // only printable chars allowed
      type = 0;
   lline->kind = type;
   lline->isValid = valid;

   lastp = &ll->last;
   if (isEmpty(ll)) {     // first element in the list
      ll->first = lline;
      ll->curr = lline;
      ll->currentIdx = 0;
      lline->prev = NULL;
   } else {
      lline->prev = *lastp;
      (*lastp)->next = lline;
   }
   lline->next = NULL;
   lline->isCleared = FALSE;
   *lastp = lline;
   ++ll->count;
   if (ll->currentIdx == 0 && lline->isValid) {  // first valid entry
      ll->currentIdx = ll->count;
      ll->curr = lline;
   }

   return QF_OK;
}

// Allocate memory for lists member of LocationStack struct.
private LocationList *
allocateLocList(int n) {
   return ALLOC_CLEAR_MULT(LocationList, n);
}


// Initialize all location stacks. Should only be called once.
void
llInitStacksOnce(void) {
   for (int i = 0; i < COUNT_LOC_LISTS; i++) {
      LocationStack* st = locationStacksS + i;
      st->bufNum = INVALID_LL_BUFNR;
      st->lists = allocateLocList(STACK_CAPACITY);
      st->listcount = 0;
   }
}

//}}}
//{{{identification

private LocationStack*
identifyStackByLetter(char letter) {
   switch (letter) {
   case 'm': return locationStacksS + LOC_LIST_MAKE;
   case 'g': return locationStacksS + LOC_LIST_GREP;
   case 'h': return locationStacksS + LOC_LIST_HELP;
   case 't': return locationStacksS + LOC_LIST_TAGS;
   case 'b': return locationStacksS + LOC_LIST_BOOKMARKS;
   case 'c': return locationStacksS + LOC_LIST_CSCOPE;
   default: return NULL;
   }
}

// In commands, loc stacks are identified by a letter like `:lopen g`
// Decode the letter from arg and return one of LOC_LIST_* constants or -1 in case of incorrect arg
private LocationStack*
identifyStack(Var* arg) {
   if (arg->tag != VAR_STRING)
      return NULL;
   return identifyStackByLetter(*(arg->string));
}

private LocationStack*
identifyStackByInvo(Invocation* invo) {
   return identifyStackByLetter(*(invo->arg));
}

// Get the location list stack to use for the specified Command.
private LocationStack *
getStackForCommand(Invocation* invo, int print_emsg) {
   LocationStack* stack = identifyStackByInvo(invo);
   if (stack == NULL && print_emsg)
      emsg(_(e_no_location_stack));

   return stack;
}

//}}}

// Copy location list entries from 'source' to 'dest'.
//private int
//copy_loclist_entries(LocationList* source, LocationList* dest) {
//   int i;
//   LocLine* from_qfp;
//   LocLine* prevp;
//
//   // copy all the location entries in this list
//   FOR_ALL_LL_ITEMS(source, from_qfp, i) {
//      if (addEntry(dest,
//             NULL,
//             NULL,
//             from_qfp->moduleName,
//             0,
//             from_qfp->text,
//             from_qfp->lNum,
//             from_qfp->endLNum,
//             from_qfp->col,
//             from_qfp->endCol,
//             from_qfp->visCol,
//             from_qfp->pattern,
//             from_qfp->errNum,
//             0,
//             &from_qfp->userData,
//             from_qfp->isValid) == QF_FAIL)
//         return FAIL;
//
//      // addEntry() will not set the qf_num field, as the
//      // directory and file names are not supplied. So the fNum
//      // field is copied here.
//      prevp = dest->last;
//      prevp->fNum = from_qfp->fNum;   // file number
//      prevp->kind = from_qfp->kind;   // error type
//      if (source->curr == from_qfp)
//         dest->curr = prevp;      // current location
//   }
//
//   return OK;
//}

//Copy the specified location list 'source' to 'dest'.
//private int
//copy_loclist(LocationList *source, LocationList *dest) {
//   // Some of the fields are populated by addEntry()
//   dest->noValidEntries = source->noValidEntries;
//   dest->hasUserData = source->hasUserData;
//   dest->count = 0;
//   dest->currentIdx = 0;
//   dest->first = NULL;
//   dest->last = NULL;
//   dest->curr = NULL;
//   if (source->title != NULL)
//      dest->title = copyStr(source->title);
//   else
//      dest->title = NULL;
//   if (source->qf_ctx) {
//      dest->qf_ctx = allocVar();
//      if (dest->qf_ctx)
//         copy_tv(OUT dest->qf_ctx, source->qf_ctx);
//   } else
//      dest->qf_ctx = NULL;
//   if (source->textFn.name != NULL)
//      evCopyCallback(&dest->textFn, &source->textFn);
//   else
//      dest->textFn.name = NULL;
//
//   if (source->count) {
//      if (copy_loclist_entries(source, dest) == FAIL)
//         return FAIL;
//   } 
//
//   dest->currentIdx = source->currentIdx;   // current index in the list
//
//   // Assign a new ID for the location list
//   dest->id = ++lastUsedLlIdS;
//   dest->changedTick = 0L;
//
//   // When no valid entries are present in the list, curr points to
//   // the first item in the list
//   if (dest->noValidEntries) {
//      dest->curr = dest->first;
//      dest->currentIdx = 1;
//   }
//
//   return OK;
//}

// Get book number for file "directory/fname". Also sets the hasLocationEntry flag.
private int
getBookNrForPath(LocationList* ll, CS directory, CS fname) {
   CS ptr = NULL;
   Book* book;
   CS bufname;

   if (fname == NULL || *fname == ZERO)      // no file name
      return 0;

   if (directory && !eeIsAbsName(fname)
       && (ptr = concat_fnames(directory, fname, TRUE)) != NULL
   ) {
      // Here we check if the file really exists.
      // This should normally be true, but if make works without
      // "leaving directory"-messages we might have missed a directory change.
      if (mch_getperm(ptr) < 0) {
         eeglFree(ptr);
         directory = guessFilepath(ll, fname);
         if (directory)
            ptr = concat_fnames(directory, fname, TRUE);
         else
            ptr = copyStr(fname);
      }
      // Use concatenated directory name and file name
      bufname = ptr;
   } else
      bufname = fname;

   if (lastBookNameS && STRCMP(bufname, lastBookNameS) == 0 && bookRefValid(&last_bufref)) {
      book = last_bufref.c;
      eeglFree(ptr);
   } else {
      eeglFree(lastBookNameS);
      book = bookNew(bufname, NULL, (LineNr)0, BLN_NOOPT);
      if (bufname == ptr)
         lastBookNameS = bufname;
      else
         lastBookNameS = copyStr(bufname);
      bookStoreInRef(OUT &last_bufref, book);
   }
   if (!book)
      return 0;

   book->hasLocationEntry = true;
   return book->fiNum;
}

// Push dirbuf onto the directory stack and return pointer to actual dir or NULL on error.
private CS
pushDir(CS dirbuf, DirStack** stackptr, int is_file_stack) {
   DirStack* ds_ptr;

   // allocate new stack element and hook it in
   DirStack* ds_new = ALLOC_ONE_ID(DirStack, aid_ll_dirstack);
   if (ds_new == NULL)
      return NULL;

   ds_new->next = *stackptr;
   *stackptr = ds_new;

   // store directory on the stack
   if (eeIsAbsName(dirbuf)
          || (*stackptr)->next == NULL
          || is_file_stack)
      (*stackptr)->dirname = copyStr(dirbuf);
   else {
      // Okay we don't have an absolute path.
      // dirbuf must be a subdir of one of the directories on the stack.
      // Let's search...
      ds_new = (*stackptr)->next;
      (*stackptr)->dirname = NULL;
      while (ds_new) {
         eeglFree((*stackptr)->dirname);
         (*stackptr)->dirname = concat_fnames(ds_new->dirname, dirbuf, TRUE);
         if (mch_isdir((*stackptr)->dirname) == TRUE)
            break;

         ds_new = ds_new->next;
      }

      // clean up all dirs we already left
      while ((*stackptr)->next != ds_new) {
         ds_ptr = (*stackptr)->next;
         (*stackptr)->next = (*stackptr)->next->next;
         eeglFree(ds_ptr->dirname);
         eeglFree(ds_ptr);
      }

      // Nothing found -> it must be on top level
      if (ds_new == NULL) {
         eeglFree((*stackptr)->dirname);
         (*stackptr)->dirname = copyStr(dirbuf);
      }
   }

   if ((*stackptr)->dirname != NULL)
      return (*stackptr)->dirname;
   else {
      ds_ptr = *stackptr;
      *stackptr = (*stackptr)->next;
      eeglFree(ds_ptr);
      return NULL;
   }
}

//pop dirbuf from the directory stack and return previous directory or NULL if stack is empty
private CS
popDir(DirStack **stackptr) {
   DirStack  *ds_ptr;

   // TODO: Should we check if dirbuf is the directory on top of the stack?
   // What to do if it isn't?

   // pop top element and free it
   if (*stackptr != NULL) {
      ds_ptr = *stackptr;
      *stackptr = (*stackptr)->next;
      eeglFree(ds_ptr->dirname);
      eeglFree(ds_ptr);
   }

   // return NEW top element as current dir or NULL if stack is empty
   return *stackptr ? (*stackptr)->dirname : NULL;
}

// clean up directory stack
private void
qf_clean_dir_stack(DirStack** stackptr) {
   DirStack* ds_ptr;

   while ((ds_ptr = *stackptr) != NULL) {
      *stackptr = (*stackptr)->next;
      eeglFree(ds_ptr->dirname);
      eeglFree(ds_ptr);
   }
}

//Check in which directory of the directory stack the given file can be found.
//Returns a pointer to the directory name or NULL if not found.
//Clean up intermediate directory entries.
//
//TODO: How to solve the following problem?
//If we have this directory tree:
//    ./
//    ./aa
//    ./aa/bb
//    ./bb
//    ./bb/x.c
//and make says:
//    making all in aa
//    making all in bb
//    x.c:9: Error
//Then pushDir thinks we are in ./aa/bb, but we are in ./bb.
//guessFilepath will return NULL.
private CS
guessFilepath(LocationList* ll, CS filename) {
   // no dirs on the stack - there's nothing we can do
   if (ll->dirStack == NULL)
      return NULL;

   DirStack* ds_ptr = ll->dirStack->next;
   CS fullname = NULL;
   while (ds_ptr) {
      eeglFree(fullname);
      fullname = concat_fnames(ds_ptr->dirname, filename, TRUE);

      // If concat_fnames failed, just go on. The worst thing that can happen
      // is that we delete the entire stack.
      if ((fullname != NULL) && (mch_getperm(fullname) >= 0))
         break;

      ds_ptr = ds_ptr->next;
   }

   eeglFree(fullname);

   // clean up all dirs we already left
   DirStack* ds_tmp;
   while (ll->dirStack->next != ds_ptr) {
      ds_tmp = ll->dirStack->next;
      ll->dirStack->next = ll->dirStack->next->next;
      eeglFree(ds_tmp->dirname);
      eeglFree(ds_tmp);
   }

   return ds_ptr == NULL ? NULL : ds_ptr->dirname;
}

//Return TRUE if a location list with the given identifier exists.
private int
isIdValid(LocationStack* st, Unt id){
   if (st == NULL)
      return FALSE;

   for (int i = 0; i < st->listcount; ++i) {
      if (st->lists[i].id == id)
         return TRUE;
   } 

   return FALSE;
}

//When loading a file from the location list, the autocommands may modify it.
//This may invalidate the current entry.  This function checks
//whether an entry is still present in the list. Similar to location list.
private int
isEntryPresent(LocationList *ll, LocLine *curr) {
   LocLine* lline;
   int i;
   // Search for the entry in the current list
   FOR_ALL_LL_ITEMS(ll, lline, i) {
      if (lline == curr)
         break;
   } 

   if (i > ll->count) // Entry is not found
      return FALSE;

   return TRUE;
}

//Get the next valid entry in the current location list. Start search from the current entry. 
//Return NULL on failure.
private LocLine *
getNextValidEntry(LocationList* ll, LocLine* curr, int* currentIdx, Unt dir) {
   int idx = *currentIdx;
   int old_fNum = curr->fNum;

   do {
      if (idx == ll->count || curr->next == NULL)
         return NULL;
      ++idx;
      curr = curr->next;
   } while ((!ll->noValidEntries && !curr->isValid)
       || (dir == FORWARD_FILE && curr->fNum == old_fNum));

   *currentIdx = idx;
   return curr;
}

//Get the previous valid entry in the current location list. The
//search starts from the current entry.  Returns NULL on failure.
private LocLine *
getPrevValidEntry(LocationList* ll, LocLine* curr, int* currentIdx, Unt dir) {
   int idx = *currentIdx;
   int old_fNum = curr->fNum;

   do {
      if (idx == 1 || curr->prev == NULL)
         return NULL;
      --idx;
      curr = curr->prev;
    } while ((!ll->noValidEntries && !curr->isValid)
       || (dir == BACKWARD_FILE && curr->fNum == old_fNum));

    *currentIdx = idx;
    return curr;
}

//Get the n'th (errornr) previous/next valid entry from the current entry in the location list.
//  dir == FORWARD or FORWARD_FILE: next valid entry
//  dir == BACKWARD or BACKWARD_FILE: previous valid entry
private LocLine *
get_nth_valid_entry(LocationList* ll, int errornr, Unt dir, int* new_qfidx){
   LocLine* curr = ll->curr;
   int ind = ll->currentIdx;
   LocLine* prev_curr;
   int prev_index;
   CS err = e_no_more_items;

   while (errornr--) {
      prev_curr = curr;
      prev_index = ind;

      if (dir == FORWARD || dir == FORWARD_FILE)
         curr = getNextValidEntry(ll, curr, &ind, dir);
      else
         curr = getPrevValidEntry(ll, curr, &ind, dir);
      if (curr == NULL) {
         curr = prev_curr;
         ind = prev_index;
         if (err) {
            return NULL;
         }
         break;
      }

      err = NULL;
   }

   *new_qfidx = ind;
   return curr;
}

//Get n'th (errornr) entry from the current entry in the location
//list 'll'. Return a pointer to the new entry and the index in 'new_qfidx'
private LocLine *
getNthEntry(LocationList* ll, int errornr, int* new_qfidx) {
   LocLine* curr = ll->curr;
   int ind = ll->currentIdx;

   // New error number is less than the current error number
   while (errornr < ind && ind > 1 && curr->prev) {
      --ind;
      curr = curr->prev;
   }
   // New error number is greater than the current error number
   while (errornr > ind && ind < ll->count && curr->next) {
      ++ind;
      curr = curr->next;
   }

   *new_qfidx = ind;
   return curr;
}

//Get an entry specified by 'errornr' and 'dir' from the current location list. 'errornr' 
//specifies the index of the entry and 'dir' specifies the direction 
//(FORWARD/BACKWARD/FORWARD_FILE/BACKWARD_FILE).
//Return a pointer to the entry and the index the new entry is stored at, 'new_qfidx'.
private LocLine *
getEntry(LocationList* ll, int errornr, Unt dir, OUT int* new_qfidx){
   LocLine   *curr = ll->curr;
   int      qfidx = ll->currentIdx;

   if (dir != 0)    // next/prev valid entry
      curr = get_nth_valid_entry(ll, errornr, dir, &qfidx);
   ei (errornr != 0)   // go to specified number
      curr = getNthEntry(ll, errornr, &qfidx);

   *new_qfidx = qfidx;
   return curr;
}

//Find a portal displaying a Vim help file in the current tab.
private Portal *
findHelpPortal(void) {
   Portal* po;
   FOR_ALL_PORTALS(po) {
      if (bookIsHelp(po->book))
          return po;
   } 

   return NULL;
}

// Find a help portal or open one. If 'newPort' is TRUE, then open a new help portal.
private int
jumpToHelpPortal(int newPort, int *openedPortal) {
   int      flags;

   Portal* helpPort;
   if (commModifierG.cmod_tab != 0 || newPort)
      helpPort = NULL;
   else
      helpPort = findHelpPortal();
   if (helpPort && helpPort->book->countPortals > 0)
      enterPortal(helpPort, TRUE);
   else {
      // Split off help portal; put it at far top if no position
      // specified, the current portal is vertically split and narrow.
      flags = WSP_HELP;
      if (commModifierG.cmod_split == 0 && curPor->width != visibleColsG && curPor->width < 80)
         flags |= WSP_TOP;
      // If the user asks to open a new portal, then copy the location list.
      // Otherwise, don't copy the location list.
      if (!newPort)
         flags |= WSP_NEWLOC;

      if (splitPortal(0, flags) == FAIL)
         return FAIL;

      *openedPortal = TRUE;

      if (curPor->height < p_hh)
         portSetHeight((int)p_hh, curPor);
   }
   restart_edit = 0;

   return OK;
}

// Find a portal into a normal book in the current tab.
private Portal *
findPortalIntoLocList_with_normal_buf(void) {
   Portal* po;
   FOR_ALL_PORTALS(po) {
      if (bt_normal(po->book))
         return po;
   } 

   return NULL;
}

//Go to a portal in any tab containing the specified file.  Returns TRUE
//if successfully jumped to the portal. Otherwise returns FALSE.
private int
qf_goto_tabwin_with_file(int fnum) {
   Tab* t;
   Portal* po;

   FOR_ALL_TAB_PORTALS(t, po) {
      if (po->book->fiNum == fnum) {
          goto_tab_port(t, po);
          return TRUE;
      }
   } 

   return FALSE;
}

//Create a new portal to show a file above the location portal. Called when
//only the location portal is present.
private int
open_new_file_port(LocationStack* llRef) {
   int flags = WSP_ABOVE;
   if (llRef)
      flags |= WSP_NEWLOC;
   if (splitPortal(0, flags) == FAIL)
      return FAIL;      // not enough room for portal
   p_swb = 0;   // don't split again
   RESET_BINDING(curPor);
   return OK;
}

//Go to a portal that shows the right book. If the portal is not found, go
//to the portal just above the location portal. This is used for opening
//a file from a location portal and not from a location portal. If some usable
//portal is previously found, then it is supplied in 'use_win'.
private void
gotoPortalIntoLlFile(Portal* usePort, int fNum) {
   Portal* port = usePort;
   if (!port) {
      // Find the window showing the selected file in the current tab.
      FOR_ALL_PORTALS(port) {
         if (port->book->fiNum == fNum)
            break;
      } 
      if (!port) {
         // Find a previous usable window
         port = curPor;
         do {
            if (bt_normal(port->book))
               break;
            if (port->prev == NULL)
               port = lastPor;   // wrap around the top
            else
               port = port->prev; // go to previous window
         } while (port != curPor);
      }
  }
  gotoPortal(port);
}

//Go to a portal that contains the specified book 'fNum'. If a portal is
//not found, then go to the portal just above the location portal. This is
//used for opening a file from a location portal and not from a location
//portal.
private void
gotoPortalIntoQflFile(int fNum) {
   Portal* port = curPor;
   Portal* altPort = NULL;
   for (;;) {
      if (port->book->fiNum == fNum)
         break;
      if (!port->prev)
         port = lastPor;   // wrap around the top
      else
         port = port->prev;   // go to previous window

      if (isLocListPortalDOW(port)) {
         // Didn't find it, go to the portal before the location portal, unless 'switchbook' 
         // contains 'uselast': in this case we try to jump to the previously used window first.
         if ((p_swb & SWB_USELAST) != 0 && portalIsValid(prevPor) && !prevPor->o.portFixBuf)
            port = prevPor;
         ei (altPort)
            port = altPort;
         ei (curPor->prev)
            port = curPor->prev;
         else
            port = curPor->next;
         break;
      }

      // Remember a usable portal
      if (!altPort && !port->isPreview && !port->o.portFixBuf && bt_normal(port->book))
         altPort = port;
   }

   gotoPortal(port);
}

//Find a suitable portal for opening a file (fNum) from the location list and jump to it.  If 
//there already is a portal into the file, jump to it. Otherwise open a new portal into the file.
//If 'newPort' is TRUE, then always open a new portal. This is called from  location list portals.
private int
jumpToUsablePortal(int fNum, int newPort, int* openedPortal) {
   Portal* usable_wp = NULL;
   int usablePort = FALSE;

   // If opening a new portal, then don't use the location list referred by
   // the current portal. Otherwise two windows will refer to the same location list.
   LocationStack* llRef = newPort ? null : curPor->locationStackRef;

   if (llRef) {
      //Find a non-LL portal with this location list
      usable_wp = NULL;
      if (usable_wp)
         usablePort = TRUE;
   }

   if (!usablePort) {
      //Locate a portal showing a normal book
      Portal* port = findPortalIntoLocList_with_normal_buf();
      if (port)
         usablePort = TRUE;
   }

   // If no usable portal is found and 'switchbook' contains "usetab" then search in other tabs
   if (!usablePort && (p_swb & SWB_USETAB) != 0)
      usablePort = qf_goto_tabwin_with_file(fNum);

   // If there is only one portal and it is a location portal, create a new one above it
   if ((ONLY_ONE_PORTAL && isLocationListBook(curBook)) || !usablePort || newPort) {
      if (open_new_file_port(llRef) != OK)
         return FAIL;
      *openedPortal = TRUE;   // close it when fail
   } else {
      if (curPor->locationStackRef != NULL)   // In a location portal
         gotoPortalIntoLlFile(usable_wp, fNum);
      else               // In a location portal
         gotoPortalIntoQflFile(fNum);
   }

   return OK;
}

//Edit the selected file or help file. Returns OK if successfully edited the file, FAIL on failing
//to open the book and QF_ABORT if the location list was freed by an autocmd when opening the 
//book.
private int
jumpAndEditBook(
   LocationStack* stack,
   LocLine* curr,
   int forceit,
   int prevPortId,
   int* openedPortal
){
   LocationList* ll = getCurrent(stack);
   int old_changedtick = ll->changedTick;
   int retval = OK;
   int old_currList = stack->currList;
   int idSave = ll->id;

   if (curr->kind == 1) {
      //Open help file (startEditingFile() will set kind == BOOK_HELP, readfile() will
      //set readonly flag).
      retval = startEditingFile(curr->fNum, NULL, NULL, NULL, (LineNr)1,
         ECMD_HIDE + ECMD_SET_HELP, prevPortId == curPor->id ? curPor : NULL
      );
   } else {
      int   fnum = curr->fNum;

      if (!forceit && curPor->o.portFixBuf && curBook->fiNum != fnum) {
         if (curPor->locationStackRef != NULL) { 
            //Location lists cannot split or reassign their portal so 'portfixbuf' portals must fail
            emsg(_(e_portfixbuf_cannot_go_to_buffer));
            return FAIL;
         } 

         if (portalIsValid(prevPor) && !prevPor->o.portFixBuf 
               && !isLocationListBook(prevPor->book)
         ) {
            //'portfixbuf' is set; attempt to change to a window without it
            //that isn't a location list portal.
            gotoPortal(prevPor);
         }
         if (curPor->o.portFixBuf) {
            // Split the window, which will be 'noportfixbuf', and set curPor to that
            if (splitPortal(0, 0) == OK)
               *openedPortal = TRUE;

            if (curPor->o.portFixBuf) {
               // Autocommands set 'portfixbuf' or sent us to another window
               // with it set, or we failed to split the window. Give up,
               // but don't return immediately, as they may have messed with the list.
               emsg(_(e_portfixbuf_cannot_go_to_buffer));
               retval = FAIL;
            }
         }
      }

      if (retval == OK) {
         retval = booklistGetFile(fnum, (LineNr)1, GETF_SETMARK | GETF_SWITCH, forceit);
      }
   }

   // If a location list, check whether the associated portal is still present
   Portal* wp = getPortalById(prevPortId);
   if (!wp) {
      emsg(_(e_current_window_was_closed));
      *openedPortal = FALSE;
      return QF_ABORT;
   }

   if (!isIdValid(stack, idSave)) {
      emsg(_(e_current_location_list_was_changed));
      return QF_ABORT;
   }

   // Check if the list was changed. Pointers may happen to be identical, so also check changedTick
   if (old_currList != stack->currList
       || old_changedtick != ll->changedTick
       || !isEntryPresent(ll, curr)
   ) {
      emsg(_(e_current_location_list_was_changed));
      return QF_ABORT;
   }

   return retval;
}

// Go to an entry in the current file using either line/column number or a search pattern.
private void
jumpToEntry(LineNr lNum, int col, Byte visCol, CS pattern){
   LineNr i;

   if (pattern == NULL) {
      // Go to line with error, unless lNum is 0.
      i = lNum;
      if (i > 0) {
          if (i > curBook->mem.lineCount)
         i = curBook->mem.lineCount;
          curPor->cursor.lnum = i;
      }
      if (col > 0) {
         curPor->cursor.coladd = 0;
         if (visCol == TRUE)
            coladvance(col - 1);
         else
            curPor->cursor.col = col - 1;
         curPor->setCursWant = true;
         check_cursor();
      } else
         beginline(BL_WHITE | BL_FIX);
   } else {
      Pos save_cursor;

      // Move the cursor to the first line in the book
      save_cursor = curPor->cursor;
      curPor->cursor.lnum = 0;
      if (!do_search(NULL, '/', '/', pattern, STRLEN(pattern), (long)1, SEARCH_KEEP, NULL))
         curPor->cursor = save_cursor;
   }
}

// Display location list index and size message
private void
printMsg(
   LocationStack* stack,
   int currentIdx,
   LocLine* curr,
   Book* oldCurBook,
   LineNr old_lnum
) {
   ArrayList* gap = getTempList();

   // Update the screen before showing the message, unless the screen scrolled up.
   if (!msg_scrolled)
      update_topline_redraw();
   eeSnprintf(IObuff, IOSIZE, _("(%d of %d)%s%s: "), currentIdx,
       getCurrent(stack)->count,
       curr->isCleared ? _(" (line deleted)") : E,
       createMsg(curr->kind, curr->errNum));
   // Add the message, skipping leading whitespace and newlines.
   ga_concat(gap, IObuff);
   formatText(gap, skipwhite(curr->text));
   ga_append(gap, ZERO);

   //Output the message. Overwrite to avoid scrolling when the 'O'
   //flag is present in 'shortmess'; But when not jumping, print the whole message.
   LineNr i = msg_scroll;
   if (curBook == oldCurBook && curPor->cursor.lnum == old_lnum)
      msg_scroll = TRUE;
   ei (!msg_scrolled && shortmess(SHM_OVERALL))
      msg_scroll = FALSE;
   msgAndKeep((CS)gap->c, 0, TRUE);
   msg_scroll = i;

   clearArrayList();
}

//Find a usable portal for opening a file from the location list. If a portal is not found then 
//open a new portal. If 'newPort' is TRUE, then open a new portal. Return OK if successfully 
//jumped or opened a portal. Return FAIL if not able to jump/open a portal. Return NOTDONE if 
//a file is not associated with the entry. Return QF_ABORT if the location list was modified
//by an autocmd.
private int
jumpOrOpenPortal(LocationStack* stack, LocLine* curr, int newPort, int* openedPortal){
   LocationList* ll = getCurrent(stack);
   int      old_changedtick = ll->changedTick;
   int      old_currList = stack->currList;

   // For ":helpgrep" find a help portal or open one.
   if (curr->kind == 1 && (!bookIsHelp(curPor->book) || commModifierG.cmod_tab != 0)
       && jumpToHelpPortal(newPort, openedPortal) == FAIL)
          return FAIL;
          
   if (old_currList != stack->currList
       || old_changedtick != ll->changedTick
       || !isEntryPresent(ll, curr)
   ){
      emsg(_(e_current_location_list_was_changed));
      return QF_ABORT;
   }

   // If currently in the location portal, find another portal to show the file in.
   if (isLocationListBook(curBook) && !*openedPortal) {
      // If there is no file specified, we don't know where to go.
      // But do advance, otherwise ":mn" gets stuck.
      if (curr->fNum == 0)
          return NOTDONE;
      if (jumpToUsablePortal(curr->fNum, newPort, openedPortal) == FAIL)
          return FAIL;
   }
   if (old_currList != stack->currList
       || old_changedtick != ll->changedTick
       || !isEntryPresent(ll, curr)
   ) {
      emsg(_(e_current_location_list_was_changed));
   }

   return OK;
}

//Edit a selected file from the location list and jump to a
//particular line/column, adjust the folds and display a message about the jump.
//Returns OK on success and FAIL on failing to open the file/buffer.  Returns
//QF_ABORT if the location list is freed by an autocmd when opening the file.
private int
jumpToBuffer(
   LocationStack* stack,
   int currentIdx,
   LocLine* curr,
   int forceit,
   int prevPortId,
   int* openedPortal,
   int openfold,
   int print_message
) {
   int retval = OK;

   //If there is a file name, read the wanted file if needed, and check autowrite etc.
   Book* oldCurBook = curBook;
   LineNr old_lnum = curPor->cursor.lnum;

   if (curr->fNum != 0) {
      retval = jumpAndEditBook(stack, curr, forceit, prevPortId, openedPortal);
      if (retval != OK)
          return retval;
   }

   // When not switched to another book, still need to set pc mark
   if (curBook == oldCurBook)
      setpcmark();

   jumpToEntry(curr->lNum, curr->col, curr->visCol, curr->pattern);

   if ((p_fdo & FDO_LOCATION) != 0 && openfold)
      foldOpenCursor();
   if (print_message)
      printMsg(stack, currentIdx, curr, oldCurBook, old_lnum);

   return retval;
}

LocationStack*
getLocationStack(int ind) {
   if (ind < 0 || ind >= COUNT_LOC_LISTS)
      return NULL;
   return locationStacksS + ind;   
}

// Jump to an entry and try to use an existing portal.
void
llJump(LocationStack* stack, Unt dir, int errornr, Boole forceit){
   jumpToNewPortal(stack, dir, errornr, forceit, false);
}

//Jump to a loclist line.
//If dir == 0 go to entry "errornr".
//If dir == FORWARD go "errornr" valid entries forward.
//If dir == BACKWARD go "errornr" valid entries backward.
//If dir == FORWARD_FILE go "errornr" valid entries files backward.
//If dir == BACKWARD_FILE go "errornr" valid entries files backward
//ei "errornr" is zero, redisplay the same line
//If 'forceit' is TRUE, then can discard changes to the current buffer.
//If 'newPort' is TRUE, then open the file in a new portal.
private void
jumpToNewPortal(
   LocationStack* stack,
   Unt dir,
   int errornr,
   Boole forceit,
   Boole newPort
){
   LocationList* ll;
   LocLine* curr;
   LocLine* old_curr;
   int currentIdx;
   int old_currentIdx;
   Unt old_swb = p_swb;
   int prevPortId;
   int openedPortal = FALSE;
   int print_message = TRUE;
   int old_KeyTyped = KeyTyped; // getting file may reset it
   int retval = OK;

   if (isStackEmpty(stack) || isEmpty(getCurrent(stack))) {
      emsg(_(e_no_entries_in_location_list));
      return;
   }

   incrementLlBusyness();

   ll = getCurrent(stack);

   curr = ll->curr;
   old_curr = curr;
   currentIdx = ll->currentIdx;
   old_currentIdx = currentIdx;

   curr = getEntry(ll, errornr, dir, &currentIdx);
   if (curr == NULL) {
      curr = old_curr;
      currentIdx = old_currentIdx;
      goto theend;
   }

   ll->currentIdx = currentIdx;
   ll->curr = curr;
   if (updatePortalPos(stack, old_currentIdx))
      // No need to print the error message if it's visible in the error portal
      print_message = FALSE;

   prevPortId = curPor->id;

   retval = jumpOrOpenPortal(stack, curr, newPort, &openedPortal);
   if (retval == FAIL)
      goto failed;
   if (retval == QF_ABORT) {
      stack = NULL;
      curr = NULL;
      goto theend;
   }
   if (retval == NOTDONE)
      goto theend;

   retval = jumpToBuffer(stack, currentIdx, curr, forceit, prevPortId,
              &openedPortal, old_KeyTyped, print_message);
   if (retval == QF_ABORT) {
      // Location list was modified by an autocmd
      stack = NULL;
      curr = NULL;
   }

   if (retval != OK) {
      if (openedPortal)
         closePortal(curPor, TRUE);    // Close opened portal
      if (curr != NULL && curr->fNum != 0) {
         // Couldn't open file, so put index back where it was.  This could
         // happen if the file was readonly and we changed something.
   failed:
         curr = old_curr;
         currentIdx = old_currentIdx;
      }
   }
theend:
   if (stack) {
      ll->curr = curr;
      ll->currentIdx = currentIdx;
   }
   if (p_swb != old_swb) {
      // Restore old @switchbook value, but not when an autocommand has changed the value.
      p_swb = old_swb;
   }
   decrementLlBusyness();
}

// Highlight attributes used for displaying entries from the location list.
private Decoration fileDeco;
private Decoration separatorDeco;
private Decoration lineDeco;

//Display information about a single entry from the location list.
//Used by ":mlist/:llist" commands.
//'cursel' will be set to TRUE for the currently selected entry in the list.
private void
displayListEntry(LocLine* lline, int ind, int cursel) {
   Book* book;
   int filter_entry;
   ArrayList* gap;

   CS fname = NULL;
   if (lline->moduleName != NULL && *lline->moduleName != ZERO)
      eeSnprintf(IObuff, IOSIZE, "%2d %s", ind, lline->moduleName);
   else {
      if (lline->fNum != 0 && (book = bookFindFileByBookNr(lline->fNum)) != NULL) {
         if (lline->fName == NULL)
            fname = book->currFileName;
         else
            fname = lline->fName;
         if (lline->kind == 1)   // :helpgrep
            fname = gettail(fname);
      }
      if (fname == NULL)
         sprintf((char *)IObuff, "%2d", ind);
      else
         eeSnprintf(IObuff, IOSIZE, "%2d %s", ind, fname);
   }

   // Support for filtering entries using :filter /pat/ clist
   // Match against the module name, file name, search pattern and
   // text of the entry.
   filter_entry = TRUE;
   if (lline->moduleName != NULL && *lline->moduleName != ZERO)
      filter_entry &= message_filtered(lline->moduleName);
   if (filter_entry && fname != NULL)
      filter_entry &= message_filtered(fname);
   if (filter_entry && lline->pattern != NULL)
      filter_entry &= message_filtered(lline->pattern);
   if (filter_entry)
      filter_entry &= message_filtered(lline->text);
   if (filter_entry)
      return;

   msg_putchar('\n');
   msgOuttransDeco(IObuff, cursel ? getDecoFlags(HLF_QFL) : fileDeco.flags);

   if (lline->lNum != 0)
      msgPutsDeco(S":", separatorDeco.flags);
      
   gap = getTempList();
   if (lline->lNum != 0)
      addRangeInformationToArrayList(gap, lline);
      
   ga_concat(gap, createMsg(lline->kind, lline->errNum));
   ga_append(gap, ZERO);
   msgPutsDeco((CS)gap->c, lineDeco.flags);
   msgPutsDeco((CS)":", separatorDeco.flags);
   if (lline->pattern != NULL) {
      gap = getTempList();
      formatText(gap, lline->pattern);
      ga_append(gap, ZERO);
      msg_puts((CS)gap->c);
      msgPutsDeco(S":", separatorDeco.flags);
   }
   msg_puts(S" ");

   //Remove newlines and leading whitespace from the text.  For an
   //unrecognized line keep the indent, the compiler may mark a word
   //with ^^^^.
   gap = getTempList();
   formatText(gap, (fname != NULL || lline->lNum != 0) ? skipwhite(lline->text) : lline->text);
   ga_append(gap, ZERO);
   msg_prt_line((CS)gap->c, FALSE);
   out_flush();      // show one line at a time
}

// ":llist": list all locations
void
c_list(Invocation* invo) {
   int      i;
   int      idx1 = 1;
   int      idx2 = -1;
   CS arg = invo->arg;
   Boole plus = false;
   int all = invo->forceit;   // if not :ml!, only show recognized errors
   LocationStack   *stack;
   if ((stack = getStackForCommand(invo, TRUE)) == NULL)
      return;

   if (isStackEmpty(stack) || isEmpty(getCurrent(stack))) {
      emsg(_(e_no_entries_in_location_list));
      return;
   }
   if (*arg == '+') {
      ++arg;
      plus = true;
   }
   if (!get_list_range(&arg, &idx1, &idx2) || *arg != ZERO) {
      showErrFmtMsg(_(e_trailing_characters_str), arg);
      return;
   }
   LocationList* ll = getCurrent(stack);
   if (plus) {
      i = ll->currentIdx;
      idx2 = i + idx1;
      idx1 = i;
   } else {
      i = ll->count;
      if (idx1 < 0)
         idx1 = (-idx1 > i) ? 0 : idx1 + i + 1;
      if (idx2 < 0)
         idx2 = (-idx2 > i) ? 0 : idx2 + i + 1;
   }

   // Shorten all the file names, so that it is easy to read
   shorten_fnames(FALSE);

   // Get the attributes for the different location highlight items.  Note
   // that this depends on syntax items defined in the qf.vim syntax file
   fileDeco = decosByHiliteName((CS)"qfFileName");
   if (fileDeco.flags == 0)
      fileDeco = getFullDecoration(HLF_D);
   separatorDeco = decosByHiliteName((CS)"qfSeparator");
   if (separatorDeco.flags == 0)
      separatorDeco = getFullDecoration(HLF_D);
   lineDeco = decosByHiliteName((CS)"qfLineNr");
   if (lineDeco.flags == 0)
      lineDeco = getFullDecoration(HLF_N);

   if (ll->noValidEntries)
      all = TRUE;
      
   LocLine* lline;
   FOR_ALL_LL_ITEMS(ll, lline, i) {
      if ((lline->isValid || all) && idx1 <= i && i <= idx2)
         displayListEntry(lline, i, i == ll->currentIdx);

      ui_breakcheck();
   }
   clearArrayList();
}

//Remove newlines and leading whitespace from an error message. Add the result to the list "gap"
private void
formatText(ArrayList *gap, CS text) {
   CS p = text;
   while (*p != ZERO) {
      if (*p == '\n') {
         ga_append(gap, ' ');
         while (*++p != ZERO) {
            if (!SPACE_OR_TAB(*p) && *p != '\n')
               break;
         } 
      } else
         ga_append(gap, *p++);
   }
}

//Add the range information from the lnum, col, end_lnum, and end_col values
//of a location entry to the grow array "gap".
private void
addRangeInformationToArrayList(ArrayList* gap, LocLine* lline) {
   CS builder = IObuff;
   int bufsize = IOSIZE;

   eeSnprintf(builder, bufsize, "%ld", lline->lNum);
   int len = (int)STRLEN(builder);

   if (lline->endLNum > 0 && lline->lNum != lline->endLNum) {
      eeSnprintf(builder + len, bufsize - len, "-%ld", lline->endLNum);
      len += (int)STRLEN(builder + len);
   }
   if (lline->col > 0) {
      eeSnprintf(builder + len, bufsize - len, " col %d", lline->col);
      len += (int)STRLEN(builder + len);
      if (lline->endCol > 0 && lline->col != lline->endCol) {
         eeSnprintf(builder + len, bufsize - len, "-%d", lline->endCol);
         len += (int)STRLEN(builder + len);
      }
   }

   ga_concat_len(gap, builder, len);
}

// Display information (list number, list size and the title) about a location list.
private void
qf_msg(LocationStack* stack, int which, CS lead) {
    CS title = stack->lists[which].title;
    int count = stack->lists[which].count;
    Byte builder[IOSIZE];

    eeSnprintf(
       builder, 
       IOSIZE, 
       _("%serror list %d of %d; %d errors "),
       lead,
       which + 1,
       stack->listcount,
       count
   );

   if (title) {
      Unt len = STRLEN(builder);
      if (len < 34) {
         memset(builder + len, ' ', 34 - len);
         builder[34] = ZERO;
      }
      concatenateStrings(builder, (CS)title, IOSIZE);
   }
   trunc_string(builder, builder, visibleColsG - 1, IOSIZE);
   msg(builder);
}

//":molder [count]": Up in the location stack. TODO remove
//":mnewer [count]": Down in the location stack.
//":lolder [count]": Up in the location list stack.
//":lnewer [count]": Down in the location list stack.
void
c_llAge(Invocation* invo) {
   LocationStack* stack;

   if ((stack = getStackForCommand(invo, TRUE)) == NULL)
      return;

   int count = (invo->addr_count != 0) ? invo->line2 : 1;
   while (count--) {
      if (invo->id == C_lolder) {
         if (stack->currList == 0) {
            emsg(_(e_at_bottom_of_quickfix_stack));
            break;
         }
         --stack->currList;
      } else {
         if (stack->currList >= stack->listcount - 1) {
            emsg(_(e_at_top_of_quickfix_stack));
            break;
         }
         ++stack->currList;
      }
   }
   qf_msg(stack, stack->currList, E);
   updateBook(stack, NULL);
}

// Display the information about all the location lists in the stack
void
qf_history(Invocation* invo) {
   LocationStack   *stack = getStackForCommand(invo, FALSE);
   int      i;

   if (invo->addr_count > 0) {
      if (stack == NULL) {
          emsg(_(e_no_location_stack));
          return;
      }

      // Jump to the specified location list
      if (invo->line2 > 0 && invo->line2 <= stack->listcount) {
          stack->currList = invo->line2 - 1;
          qf_msg(stack, stack->currList, E);
          updateBook(stack, NULL);
      } else
          emsg(_(e_invalid_range));

      return;
   }

   if (isStackEmpty(stack))
      msg(_("No entries"));
   else {
      for (i = 0; i < stack->listcount; ++i)
          qf_msg(stack, i, i == stack->currList ? (CS)"> " : (CS)"  ");
   } 
}

//Free all the entries in the error list "idx". Note that other information
//associated with the list like context and title are not freed.
private void
freeItems(LocationList* ll) {
   LocLine* lline;
   LocLine* nextLine;
   int      stop = FALSE;

   while (ll->count && ll->first != NULL) {
      lline = ll->first;
      nextLine = lline->next;
      if (!stop) {
         eeglFree(lline->fName);
         eeglFree(lline->moduleName);
         eeglFree(lline->text);
         eeglFree(lline->pattern);
         clearVar(&lline->userData);
         stop = (lline == nextLine);
         eeglFree(lline);
         if (stop)
            // Somehow count may have an incorrect value, set it to 1
            // to avoid crashing when it's wrong.
            // TODO: Avoid count being incorrect.
            ll->count = 1;
         else
            ll->first = nextLine;
      }
      --ll->count;
   }

   ll->currentIdx = 0;
   ll->first = NULL;
   ll->last = NULL;
   ll->curr = NULL;
   ll->noValidEntries = TRUE;

   qf_clean_dir_stack(&ll->dirStack);
   ll->dir = NULL;
   qf_clean_dir_stack(&ll->fileStack);
   ll->currFName = NULL;
   ll->qf_multiline = FALSE;
   ll->qf_multiignore = FALSE;
   ll->qf_multiscan = FALSE;
}

// Free location list "idx". Frees all the entries, associated context information and the title.
private void
freeAList(LocationList* ll) {
   freeItems(ll);

   EE_CLEAR(ll->title);
   freeVar(ll->qf_ctx);
   ll->qf_ctx = NULL;
   evFreeCallback(&ll->textFn);
   ll->id = 0;
   ll->changedTick = 0L;
}

// Adjust entries between two lines of curBook by an amount. This is analogous to adjusting marks 
// and must happen simultaneously.
void
llAdjustEntries(
   LineNr line1,
   LineNr line2,
   long amount, // how much to adjust entries in [line1; line2]. If == MAXLNUM, lines are deleted
   long amount_after // amount to adjust entries in lines (line2; ...)
){
   Boole isBufferLinked = false;

   if (!(curBook->hasLocationEntry))
      return;
      
   for (int i = 0; i < COUNT_LOC_LISTS; i++) {
      LocationStack* st = locationStacksS + i;
      for (int lInd = 0; lInd < st->listcount; ++lInd) {
         LocationList *ll = getList(st, lInd);

         if (isEmpty(ll)) {
            continue;
         }
         LocLine   *lline;
         int j;
         FOR_ALL_LL_ITEMS(ll, lline, j) {
            if (lline->fNum == curBook->fiNum) {
               isBufferLinked = true;
               if (lline->lNum >= line1 && lline->lNum <= line2) {
                  if (amount == MAXLNUM)
                     lline->isCleared = TRUE;
                  else
                     lline->lNum += amount;
               } ei (amount_after && lline->lNum > line2)
                  lline->lNum += amount_after;
            }
         } 
      }
   }

   if (!isBufferLinked)
      curBook->hasLocationEntry = false;
}

//Make a nice message out of the error character and the error number:
// char    number   message
// e or E    0      " error"
// w or W    0      " warning"
// i or I    0      " info"
// n or N    0      " note"
// 0         0      ""
// other     0      " c"
// e or E    n      " error n"
// w or W    n      " warning n"
// i or I    n      " info n"
// n or N    n      " note n"
// 0         n      " error n"
// other     n      " c n"
// 1         x      ""   :helpgrep
private CS
createMsg(int c, int nr) {
   static Byte   builder[20];
   static Byte   cc[3];
   
   CS p;
   if (c == 'W' || c == 'w')
      p = S" warning";
   ei (c == 'I' || c == 'i')
      p = S" info";
   ei (c == 'N' || c == 'n')
      p = S" note";
   ei (c == 'E' || c == 'e' || (c == 0 && nr > 0))
      p = S" error";
   ei (c == 0 || c == 1)
      p = S"";
   else {
      cc[0] = ' ';
      cc[1] = c;
      cc[2] = ZERO;
      p = cc;
   }

   if (nr <= 0)
      return p;

   sprintf((char *)builder, "%s %3d", (char *)p, nr);
   return builder;
}

//When "split" is FALSE: Open the entry/result under the cursor.
//When "split" is TRUE: Open the entry/result under the cursor in a new portal.
void
llViewLocation(int split) {
   LocationStack* stack;
   if (IS_LL_PORTAL(curPor))
      stack = curPor->locationStackRef;
   else {
      emsg(_(e_no_location_stack));
      return;
   }

   if (isEmpty(getCurrent(stack))) {
      emsg(_(e_no_entries_in_location_list));
      return;
   }

   if (split) {
      // Open the selected entry in a new portal
      jumpToNewPortal(stack, 0, (long)curPor->cursor.lnum, false, true);
      executeCommLine((CS) "clearjumps");
      return;
   }

   executeCommLine((CS)(IS_LL_PORTAL(curPor) ? ".ll" : ".mc"));
}

//":mwindow": open the location portal if we have errors to display, close it if not. TODO delete
//":lwindow": open the location list portal if we have locations to display, close it if not.
void
c_cPortal(Invocation* invo) {
   LocationStack* stack;
   if ((stack = getStackForCommand(invo, TRUE)) == NULL)
      return;

   LocationList* ll = getCurrent(stack);

   // Look for an existing location portal.
   Portal* po = findPortalIntoLocList(stack);

   //If a location portal is open but we have no errors to display, close the portal. If a 
   //location portal is not open, then open it if we have errors; otherwise, leave it closed.
   if (isStackEmpty(stack)
       || ll->noValidEntries
       || isEmpty(ll)
   ) {
     if (po)
         c_lClose(invo);
   } ei (!po)
      c_lOpen(invo);
}

// ":lclose": close the window showing the location list
void
c_lClose(Invocation* invo) {
   LocationStack   *stack;
   if ((stack = getStackForCommand(invo, FALSE)) == NULL)
      return;

   // Find existing location portal and close it.
   Portal* port = findPortalIntoLocList(stack);
   if (port != NULL)
      closePortal(port, FALSE);
}

// Set "w:quickfix_title" if "stack" has a title.
private void
setTitleVar(LocationList* ll) {
   if (ll->title != NULL)
      set_internal_string_var((CS)"w:quickfix_title", ll->title);
}

// Go to a location list portal (if present).
// Return OK if the window is found, FAIL otherwise.
private int
gotoLocationPortal(LocationStack* stack, int resize, int sz, int vertsplit) {
   Portal* port = findPortalIntoLocList(stack);
   if (!port)
      return FAIL;

   gotoPortal(port);
   if (resize) {
      if (vertsplit) {
         if (sz != (int)port->width)
            portSetHeight(sz, curPor);
      } ei (sz != (int)port->height && port->height + port->statusHeight < commlineRowG)
         portSetHeight(sz, curPor);
   }

   return OK;
}

// Set options for the buffer in the location list portal.
private void
setPortalOptions() {
   // switch off 'swapfile'
   optChangeAndReportError(
      S"swapfile", (OptionValue){.tag = OPTION_BOOLE, .boole = false}, SET_LOCAL
   );
   optChangeAndReportError(
      S"booktype", (OptionValue){.tag = OPTION_STRING, .string = S"location"}, SET_LOCAL
   );
   optChangeAndReportError(
      S"bufhdden", (OptionValue){.tag = OPTION_STRING, .string = S"hide"}, SET_LOCAL
   );
   RESET_BINDING(curPor);
   curPor->o.diff = FALSE;
   optChangeAndReportError(
      S"foldmethod", (OptionValue){.tag = OPTION_STRING, .string = S"manual"}, SET_LOCAL
   );
}

// Open a new location list portal, load the location buffer and
// set the appropriate options for the portal.
// Returns FAIL if the portal could not be opened.
private int
openNewPortal(LocationStack* stack, int height) {
   Portal* oldPort = curPor;
   Tab* prevtab = curtab;
   Unt flags = 0;

   Book* llBook = findLlBook(stack);

   // The current portal becomes the previous one afterwards.
   Portal* port = curPor;

   if (commModifierG.cmod_split == 0)
      // Create the new location portal at the very bottom, except when
      // :belowright or :aboveleft is used.
      gotoPortal(lastPor);
      
   // Default is to open the portal below the current portal
   if (commModifierG.cmod_split == 0)
      flags = WSP_BELOW;
      
   flags |= WSP_NEWLOC;
   if (splitPortal(height, flags) == FAIL)
      return FAIL;      // not enough room for portal
      
   RESET_BINDING(curPor);

   //For the location list portal, create a reference to the
   //location list stack from the portal 'port'.
   curPor->locationStackRef = stack;
   stack->refcount++;

   if (oldPort != curPor)
      oldPort = NULL;  // don't store info when in another portal
   if (llBook != NULL) {
      // Use the existing location buffer
      if (startEditingFile(llBook->fiNum, NULL, NULL, NULL, ECMD_ONE,
             ECMD_HIDE + ECMD_OLDBUF + ECMD_NOWINENTER, oldPort) == FAIL)
         return FAIL;
   } else {
      // Create a new location buffer
      if (startEditingFile(0, NULL, NULL, NULL, ECMD_ONE, ECMD_HIDE + ECMD_NOWINENTER,
                               oldPort) == FAIL)
         return FAIL;

      //save the number of the new buffer
      stack->bufNum = curBook->fiNum;
   }

   //Set the options for the location buffer/portal (if not already done)
   //Do this even if the location buffer was already present, as an autocmd
   //might have previously deleted (:bdelete) the location buffer.
   if (!isLocationListBook(curBook))
      setPortalOptions();

   // Only set the height when still in the same tab and there is no portal to the side.
   if (curtab == prevtab && curPor->width == visibleColsG)
      portSetHeight(height, curPor);
   curPor->o.portFixHeight = TRUE;       // set 'winfixheight'
   if (portalIsValid(port))
      prevPor = port;

   return OK;
}

// ":lopen": open a window that shows the location list.
void
c_lOpen(Invocation* invo) {
   LocationStack* stack;
   int      status = FAIL;

   if ((stack = getStackForCommand(invo, TRUE)) == NULL)
      return;

   incrementLlBusyness();

   int height;
   if (invo->addr_count != 0)
      height = invo->line2;
   else
      height = QF_WINHEIGHT;

   reset_VIsual_and_resel();         // stop Visual mode

   // Find an existing location portal, or open a new one.
   if (commModifierG.cmod_tab == 0)
      status = gotoLocationPortal(stack, invo->addr_count != 0, height, commModifierG.cmod_split & WSP_VERT);
   if (status == FAIL) {
      if (openNewPortal(stack, height) == FAIL) {
         decrementLlBusyness();
         return;
      }
   }
   LocationList* ll = getCurrent(stack);
   setTitleVar(ll);
   // Save the current index here, as updating the location buffer may free the location list
   int lnum = ll->currentIdx;

   // Fill the buffer with the location list.
   fillBookWithLocList(ll, curBook, NULL, curPor->id);

   decrementLlBusyness();

   curPor->cursor.lnum = lnum;
   curPor->cursor.col = 0;
   check_cursor();
   update_topline();      // scroll to show the line
}

// Move the cursor in the location portal to "lnum".
private void
gotoLine(Portal* po, LineNr lnum) {
   Portal* old_curPor = curPor;
   curPor = po;
   curBook = po->book;
   curPor->cursor.lnum = lnum;
   curPor->cursor.col = 0;
   curPor->cursor.coladd = 0;
   curPor->cursWant = 0;
   update_topline();      // scroll to show the line
   redraw_later(UPD_VALID);
   curPor->statusLineNeedsRedraw = TRUE;   // update ruler
   
   curPor = old_curPor;
   curBook = curPor->book;
}

 // :mbottom/:lbottom commands.
void
c_lBottom(Invocation* invo) {
   LocationStack* stack;
   if ((stack = getStackForCommand(invo, TRUE)) == NULL)
      return;

   Portal* po = findPortalIntoLocList(stack);
   if (po && po->cursor.lnum != po->book->mem.lineCount)
      gotoLine(po, po->book->mem.lineCount);
}

// Return the line number of the current entry in its location portal.
// Precondition: it's a location portal.
LineNr
llCurrentEntry(Portal* po) {
   return getCurrent(po->locationStackRef)->currentIdx;
}

// Update the cursor position in the location portal to the current error.
// Return TRUE if there is a location portal.
private Boole
updatePortalPos(LocationStack* stack, int      old_currentIdx) {   // previous currentIdx or zero
   int currentIdx = getCurrent(stack)->currentIdx;

   // Put the cursor on the current error in the location portal, so that it's viewable.
   Portal* port = findPortalIntoLocList(stack);
   if (port != NULL
       && currentIdx <= port->book->mem.lineCount
       && old_currentIdx != currentIdx
   ) {
      if (currentIdx > old_currentIdx) {
         port->redrawTop = old_currentIdx;
         port->redrawBott = currentIdx;
      } else {
         port->redrawTop = currentIdx;
         port->redrawBott = old_currentIdx;
      }
      gotoLine(port, currentIdx);
   }
   return port != NULL;
}

// Check whether the given portal is displaying the specified location stack.
private Boole
isLocListPortal(Portal* port, LocationStack* stack) {
   // A portal displaying the location buffer will have the locationStackRef field set to NULL.
   // A portal displaying a location list buffer will have the locationStackRef
   // pointing to the location list.
   if (bookIsValid(port->book) 
         && isLocationListBook(port->book) && (port->locationStackRef == stack)
   )
      return true;

   return false;
}

// Find a portal into the location stack 'stack' in the current tab.
private Portal *
findPortalIntoLocList(LocationStack* stack) {
   Portal* port;
   FOR_ALL_PORTALS(port) {
      if (isLocListPortal(port, stack))
         return port;
   } 
   return NULL;
}

// Find a location buffer. Searches in open portals in all the tabs.
private Book*
findLlBook(LocationStack* stack) {
   if (stack->bufNum != INVALID_LL_BUFNR) {
      Book* llBook = bookFindFileByBookNr(stack->bufNum);
      if (llBook)
         return llBook;
      // buffer is no longer present
      stack->bufNum = INVALID_LL_BUFNR;
   }

   Portal* po;
   Tab* t;
   FOR_ALL_TAB_PORTALS(t, po) {
      if (isLocListPortal(po, stack))
         return po->book;
   } 

   return NULL;
}

// Process the 'quickfixtextfunc' option value. Returns OK or FAIL.
CS
did_set_quickfixtextfunc(OptionChange* cha UNUSED) {
   if (optSetCallback(OUT &locationTextFnS, p_qftf) == FAIL)
      return e_invalid_argument;

   return NULL;
}

// Update the w:quickfix_title variable in the location list portal in all the tabs.
private void
updateTitleVar(LocationStack* stack) {
   LocationList* ll = getCurrent(stack);
   Tab* t;
   Portal* port;
   Portal* savedPor = curPor;

   FOR_ALL_TAB_PORTALS(t, port) {
      if (isLocListPortal(port, stack)) {
         curPor = port;
         setTitleVar(ll);
      }
   }
   curPor = savedPor;
}

// Find the location book. If it exists, update the contents.
private void
updateBook(LocationStack* stack, LocLine* oldLast) {
   // Check if a book for the location list exists. Update it.
   Book* book = findLlBook(stack);
   if (!book)
      return;

   LineNr old_line_count = book->mem.lineCount;
   int getLlPortalId = 0;

   Portal* port = findPortalIntoLocList(stack);
   if (!port)
      return;
      
   getLlPortalId = port->id;

   // autocommands may cause trouble
   incrementLlBusyness();

   int doFill = TRUE;
   AutocommSave aco;
   if (oldLast == NULL) {
      // set curPor/curBook to book and save a few things
      auCommPrepareBook(&aco, book);
      if (curBook != book)
         doFill = FALSE;  // failed to find a portal into "book"
   }

   if (doFill) {
      updateTitleVar(stack);

      fillBookWithLocList(getCurrent(stack), book, oldLast, getLlPortalId);
      ++CHANGEDTICK(book);

      if (oldLast == NULL) {
         (void)updatePortalPos(stack, 0);

         // restore curPor/curBook and a few other things
         auCommRestoreBook(&aco);
      }
   }

   // Only redraw when added lines are visible. This avoids flickering
   // when the added lines are not visible.
   if ((port = findPortalIntoLocList(stack)) != NULL && old_line_count < port->bottomLine)
      drawBookLater(book, UPD_NOT_VALID);

   // always called after incrementLlBusyness()
   decrementLlBusyness();
}

// Add an error line to the loc list book.
private inline int
addLine(
   Book* book,      // location portal's book
   LineNr   lnum,
   LocLine* lline,
   CS dirname,
   Boole  firstBookLine,
   CS qftf_str
){
   Book* errBook;
   ArrayList   *gap;

   gap = getTempList();

   // If the 'quickfixtextfunc' returned a non-empty custom string for this entry, then use it
   if (qftf_str != NULL && *qftf_str != ZERO) {
      ga_concat(gap, qftf_str);
   } else {
      if (lline->moduleName != NULL)
         ga_concat(gap, lline->moduleName);
      ei (lline->fNum != 0
            && (errBook = bookFindFileByBookNr(lline->fNum)) != NULL
            && errBook->currFileName
      ){
         if (lline->kind == 1)   // :helpgrep
            ga_concat(gap, gettail(errBook->currFileName));
         else {
            // Shorten the file name if not done already.
            // For optimization, do this only for the first entry in a buffer.
            if (firstBookLine 
                  && (errBook->shortFileName == NULL || mch_isFullName(errBook->shortFileName))
            ){
               if (*dirname == ZERO)
                  mch_dirname(dirname, MAXPATHL);
               shorten_buf_fname(errBook, dirname, FALSE);
            }
            if (lline->fName == NULL)
               ga_concat(gap, errBook->currFileName);
            else
               ga_concat(gap, lline->fName);
          }
      }

      ga_append(gap, '|');

      if (lline->lNum > 0) {
         addRangeInformationToArrayList(gap, lline);
         ga_concat(gap, createMsg(lline->kind, lline->errNum));
      } ei (lline->pattern != NULL)
         formatText(gap, lline->pattern);
      ga_append(gap, '|');
      ga_append(gap, ' ');

      // Remove newlines and leading whitespace from the text. For an unrecognized line keep the 
      // indent, the compiler may mark a word with ^^^^.
      formatText(gap, gap->len > 3 ? skipwhite(lline->text) : lline->text);
   }

   ga_append(gap, ZERO);
   if (memAppendBook(book, lnum, gap->c, gap->len, FALSE) == FAIL)
      return FAIL;

   return OK;
}

// Call the 'quickfixtextfunc' function to get the list of lines to display in the location portal 
// for the entries 'start_idx' to 'end_idx'.
private List *
callLocListToText(LocationList *ll, int getLlPortalId, long start_idx, long end_idx) {
   Callback   *cb = &locationTextFnS;
   List   *qftf_list = NULL;
   static int recursive = FALSE;

   if (recursive)
      return NULL;  // this doesn't work properly recursively
   recursive = TRUE;

   // If 'quickfixtextfunc' is set, then use the user-supplied function to get the text to display.
   // Use the local value of 'quickfixtextfunc' if it is set.
   if (ll->textFn.name != NULL)
      cb = &ll->textFn;
   if (cb->name != NULL) {
      Var args[1];
      Bag* d;
      Var returnVar;

      // create the dict argument
      if ((d = allocBag_lock(VAR_FIXED)) == NULL) {
         recursive = FALSE;
         return NULL;
      }
      bagAddNumber(d, S"winid", (long)getLlPortalId);
      bagAddNumber(d, S"id", (long)ll->id);
      bagAddNumber(d, S"start_idx", start_idx);
      bagAddNumber(d, S"end_idx", end_idx);
      ++d->refcount;
      args[0].tag = VAR_BAG;
      args[0].bag = d;

      qftf_list = NULL;
      if (call_callback(cb, 0, &returnVar, 1, args) != FAIL) {
         if (returnVar.tag == VAR_LIST) {
            qftf_list = returnVar.list;
            qftf_list->refcount++;
         }
         clearVar(&returnVar);
      }
      bagUnref(d);
   }

   recursive = FALSE;
   return qftf_list;
}

// Fill current buffer with location entries, replacing any previous contents curBook must be the 
// location buffer! If "oldLast" is not NULL append the items after this one. When "oldLast" is 
// NULL then "book" must equal "curBook"! Because ml_delete() is used and autocommands will be run.
private void
fillBookWithLocList(LocationList *ll, Book* book, LocLine *oldLast, int getLlPortalId) {
   LineNr lnum;
   LocLine* lline;
   int keyTypedSave = KeyTyped;
   List* locList = NULL;
   ListItem* listItem = NULL;

   if (!oldLast) {
      if (book != curBook) {
         internal_error(S"fillBookWithLocList()");
         return;
      }

      // delete all existing lines
      //
      // Note: we cannot store undo information, because
      // ll book is usually not allowed to be modified.
      //
      // So we need to clean up undo information
      // otherwise autocommands may invalidate the undo stack
      while ((curBook->mem.flags & ML_EMPTY) == 0)
         (void)ml_delete((LineNr)1);

      Portal* wp;
      Tab* t;
      FOR_ALL_TAB_PORTALS(t, wp) {
         if (wp->book == curBook)
            wp->skipCol = 0;
      } 

      // Remove all undo information
      invalidateUndoBufferAndFreeBlocks(curBook);
   }

   // Check if there is anything to display
   if (ll && ll->first) {
      Byte dirname[MAXPATHL];
      int invalid_val = FALSE;
      int prev_bufnr = -1;

      dirname[0] = ZERO;

      // Add one line for each error
      if (!oldLast) {
         lline = ll->first;
         lnum = 0;
      } else {
         if (oldLast->next != NULL)
            lline = oldLast->next;
         else
           lline = oldLast;
         lnum = book->mem.lineCount;
      }

      locList = callLocListToText(ll, getLlPortalId, (long)(lnum + 1), (long)ll->count);
      if (locList != NULL)
         listItem = locList->first;

      while (lnum < ll->count) {
         CS str = NULL;

         //Use the text supplied by the user defined function (if any).
         //If the returned value is not string, then ignore the rest
         //of the returned values and use the default.
         if (listItem != NULL && !invalid_val) {
            str = convertVarToStringSingleUse(&listItem->c);
            if (str == NULL)
               invalid_val = TRUE;
         }

         if (addLine(book, lnum, lline, dirname, prev_bufnr != lline->fNum, str) == FAIL)
            break;

         prev_bufnr = lline->fNum;
         ++lnum;
         lline = lline->next;
         if (lline == NULL)
            break;

         if (listItem != NULL)
            listItem = listItem->next;
      }

      if (oldLast == NULL)
          // Delete the empty line which is now at the end
          (void)ml_delete(lnum + 1);

      clearArrayList();
   }

   // correct cursor position
   check_lnums(TRUE);

   if (oldLast == NULL) {
      // Set the 'filetype' to "qf" each time after filling the book.
      // This resembles reading a file into a book, it's more logical when using autocommands.
      ++curBookLock;
      optChangeAndReportError(
         S"filetype", (OptionValue){.tag = OPTION_STRING, .string = S"qf"}, SET_LOCAL
      );
      curBook->o.modifiable = false;

      curBook->keepFiletype = TRUE;   // don't detect 'filetype'
      apply_autocmds(EVENT_BUFREADPOST, S"quickfix", NULL, false, curBook);
      apply_autocmds(EVENT_BUFWINENTER, S"quickfix", NULL, false, curBook);
      curBook->keepFiletype = FALSE;
      --curBookLock;

      // make sure it will be redrawn
      drawCurBookLater(UPD_NOT_VALID);
   }

   // Restore KeyTyped, setting 'filetype' may reset it.
   KeyTyped = keyTypedSave;
}

// For every change made to the location list, update the changed tick.
private void
updateChangedTick(LocationList *ll) {
   ll->changedTick++;
}
// Return the location list number with the given identifier. Returns -1 if list is not found.
private int
idToNr(LocationStack* stack, Unt listId) {
   for (int ind = 0; ind < stack->listcount; ind++) {
      if (stack->lists[ind].id == listId)
          return ind;
   } 
   return INVALID_LL_IND;
}

// If the current list is not "idSave" and we can find the list with that ID then make it the 
// current list. This is used when autocommands may have changed the current list.
// Return OK if successfully restored the list. Return FAIL if the list with the specified 
// identifier (idSave) is not found in the stack.
private int
restoreList(LocationStack* stack, Unt idSave){
   if (getCurrent(stack)->id == idSave)
      return OK;

   int curlist = idToNr(stack, idSave);
   if (curlist < 0)
      // list is absent
      return FAIL;
   stack->currList = curlist;
   return OK;
}

// Jump to the first entry if there is one.
private void
jumpToFirstEntry(LocationStack* stack, Unt idSave, Boole forceit) {
   if (restoreList(stack, idSave) == FAIL)
      return;

   if (!portCheckCanSetCurBookForceIt(forceit))
      return;

   // Autocommands might have cleared the list, check for that.
   if (!isEmpty(getCurrent(stack)))
      llJump(stack, 0, 0, forceit);
}

// Return TRUE when using ":vimgrep" for ":grep".
int
grepIsActuallyInternal(CommIndex id) {
   return (id == C_grep || id == C_grepadd) && STRCMP("internal", curBook->o.grepProg) == 0;
}

// Return the grep autocmd name.
private Byte *
getGrepAutocommand(CommIndex id) {
   switch (id) {
   case C_grep:       return (CS)"grep";
   case C_grepadd:   return (CS)"grepadd";
   case C_elck:   return (CS)"elck";
   default: return NULL;
   }
}

// Return the name for the errorfile, in allocated memory. Find a new unique name when 
// @makeef contains "##". Return NULL for error.
private Arr(Byte)
buildErrorFileName(void) {
   static int   start = -1;
   static int   off = 0;
   FileStat   sb;

   CS name;
   if (*p_mef == ZERO) {
      name = eeTempName('e', FALSE);
      if (!name)
         emsg(_(e_cant_get_temp_file_name));
      return name;
   }

   CS p;
   for (p = p_mef; *p; ++p) {
      if (p[0] == '#' && p[1] == '#')
         break;
   } 

   if (*p == ZERO)
      return copyStr(p_mef);

   // Keep trying until the name doesn't exist yet.
   for (;;) {
      if (start == -1)
         start = mch_get_pid();
      else
         off += 19;

      name = alloc_id(STRLEN(p_mef) + 30, aid_ll_mef_name);
      if (name == NULL)
         break;
      STRCPY(name, p_mef);
      sprintf((char *)name + (p - p_mef), "%d%d", start, off);
      STRCAT(name, p + 2);
      if (mch_getperm(name) < 0
             // Don't accept a symbolic link, it's a security risk.
             && lstat((char *)name, &sb) < 0
         )
          break;
      eeglFree(name);
   }
   return name;
}

// Form the complete command line to invoke 'make'/'grep'. Quote the command
// using 'shellquote' and append 'shellpipe'. Echo the fully formed command.
private CS
buildFullShellCommand(CS makecmd, CS fname) {
   Unt len = STRLEN(makecmd) + 1;
   if (*p_sp != ZERO)
      len += (unsigned)STRLEN(p_sp) + (unsigned)STRLEN(fname) + 3;
   CS cmd = alloc_id(len, aid_ll_makecmd);
   SPRINTF(cmd, "%s", (char *)makecmd);

   // If 'shellpipe' empty: don't redirect to 'errorfile'.
   if (*p_sp != ZERO)
      append_redir(cmd, len, p_sp, fname);

   // Display the fully formed command.  Output a newline if there's something
   // else than the :make command that was typed (in which case the cursor is in column 0).
   if (msgColG == 0)
      msg_didout = FALSE;
   msg_start();
   msg_puts(S":!");
   msg_outtrans(cmd);      // show what we are doing

   return cmd;
}

//Process :eegl command arguments. The command syntax is:
//
//  :{count}eegl /{pattern}/[g][j]
private int
eeglProcessArgs(Invocation* invo, OUT VimGrepArgs* args) {
   CLEAR_POINTER(args);

   args->regmatch.regprog = NULL;
   args->title = copyStr(copyCommandTitle(*invo->commline));

   if (invo->addr_count > 0)
      args->tomatch = invo->line2;
   else
      args->tomatch = MAXLNUM;

   // Get the search pattern: either white-separated or enclosed in //
   CS p = skipEeglGrepPat(invo->arg, &args->spat, &args->flags);
   if (!p) {
      emsg(_(e_invalid_search_pattern_or_delimiter));
      return FAIL;
   }

   vgr_init_regmatch(&args->regmatch, args->spat);
   if (args->regmatch.regprog == NULL)
      return FAIL;

   p = skipwhite(p);
   if (*p != ZERO) {
      emsg(_(e_trailing_characters_str));
      return FAIL;
   }

   return OK;
}

// Internal grep of all files, the "eegl" or "eegrep" commands.
// They search all files except the .git subfolder and put the results into a location list
void
c_elgrep(Invocation* invo) {
   VimGrepArgs args;
   LocationList* ll;
   CS target_dir = NULL;

   if (!portCheckCanSetCurBookForceIt(invo->forceit))
      return;

   CS auName = vgr_get_auname(invo->id);
   if (auName
         && apply_autocmds(EVENT_QUICKFIXCMDPRE, auName, curBook->currFileName, true, curBook)
         && aborting()
   ) {
      return;
   }

   LocationStack* stack = locationStacksS + LOC_LIST_GREP;

   if (eeglProcessArgs(invo, OUT &args) == FAIL)
      goto theend;

   if ((invo->id != C_elckadd) || isStackEmpty(stack)) {
      // make place for a new list
      newLocList(stack, args.title);
   } 

   incrementLlBusyness();

   Book* firstMatchBook = NULL;
   Boole redrawForDummy = false;
   int status = elckGrepFiles(stack, &args, OUT &redrawForDummy, &firstMatchBook, OUT &target_dir);
   ExpandMatch m = (ExpandMatch){.c = args.fnames, .len = args.fcount, .a = createArena()};
   if (status != OK) {
      decrementLlBusyness();
      goto theend;
   }

   ll = getCurrent(stack);
   ll->noValidEntries = FALSE;
   ll->curr = ll->first;
   ll->currentIdx = 1;
   updateChangedTick(ll);

   updateBook(stack, NULL);

   // Remember the current location list identifier, so that we can check for
   // autocommands changing the current list.
   Unt idSave = getCurrent(stack)->id;

   if (auName != NULL)
      apply_autocmds(EVENT_QUICKFIXCMDPOST, auName, curBook->currFileName, true, curBook);
   // The QuickFixCmdPost autocmd may free the location list. Check the list is still valid.
   if (!isIdValid(stack, idSave) || restoreList(stack, idSave) == FAIL) {
      decrementLlBusyness();
      goto theend;
   }

   // Jump to first match.
   if (!isEmpty(getCurrent(stack))) {
      if ((args.flags & VGR_NOJUMP) == 0)
         jumpToFirstMatchAndUpdateDir(
               stack, invo->forceit, OUT &redrawForDummy, OUT firstMatchBook, target_dir
         );
   } else
      showErrFmtMsg(_(e_no_match_str_2), args.spat);

   decrementLlBusyness();

   // If we loaded a dummy buffer into the current portal, the autocommands
   // may have messed up things, need to redraw and recompute folds.
   if (redrawForDummy) {
      foldUpdateAll(curPor);
   }

theend:
   deleteArena(m.a);
   eeglFree(args.title);
   eeglFree(target_dir);
   eeRegFree(args.regmatch.regprog);
}

// Used for ":grep" and ":grepadd"
void
c_grep(Invocation* invo) {
   CS errorformat = curBook->o.errorFormat;
   Boole newlist = true;

   // Redirect ":grep" to ":vimgrep" if 'grepprg' is "internal".
   if (grepIsActuallyInternal(invo->id)) {
      c_vimgrep(invo);
      return;
   }

   CS auName = getGrepAutocommand(invo->id);
   if (auName
            && apply_autocmds(EVENT_QUICKFIXCMDPRE, auName, curBook->currFileName, true, curBook)
            && aborting()
   ) {
      return;
   }

   doFlushAllBooks();
   CS fname = buildErrorFileName();
   if (!fname)
      return;
   mch_remove(fname);       // in case it's not unique

   CS comm = buildFullShellCommand(invo->arg, fname);
   if (!comm) {
      eeglFree(fname);
      return;
   }

   // let the shell know if we are redirecting output or not
   do_shell(comm, *p_sp != ZERO ? SHELL_DOOUT : 0);
   do_shell(comm, SHELL_DOOUT);

   incrementLlBusyness();

   if (invo->id != C_make)
      errorformat =  curBook->o.grepFormat;
   if (invo->id == C_grepadd)
      newlist = false;
      
   LocationStack* stack = locationStacksS + LOC_LIST_GREP;
   int res = llInitFromFile(stack, fname, errorformat, newlist, copyCommandTitle(*invo->commline));

   // Remember the current location list identifier, so that we can
   // check for autocommands changing the current list.
   Unt llIdSaved = getCurrent(stack)->id;
   if (auName)
      apply_autocmds(EVENT_QUICKFIXCMDPOST, auName, curBook->currFileName, true, curBook);
   if (res > 0 && !invo->forceit && isIdValid(stack, llIdSaved))
      // display the first error
      jumpToFirstEntry(stack, llIdSaved, FALSE);

   decrementLlBusyness();
   mch_remove(fname);
   eeglFree(fname);
   eeglFree(comm);
}

// Initialize the location list for the in-progress "make" command
void initInProgressLl() {
   if (makeInProgressS) {
      list_free(makeInProgressS);
   }
   
   TypeSpec* stringSpec = ALLOC_ONE(TypeSpec);
   stringSpec->tag = VAR_STRING;
   stringSpec->args = NULL;
   TypeSpec* listSpec = ALLOC_ONE(TypeSpec);
   listSpec->args = NULL;
   
   listSpec->tag = VAR_LIST;
   listSpec->member = stringSpec;
   listSpec->args = NULL;
   makeInProgressS = ALLOC_CLEAR_ONE(List);
   makeInProgressS->ty = listSpec;
   
   if (makeInProgressS) {
      isMakeRunningS = 0;
   }
}

// Callback for a single error message from "make"
private void
makeReceiveMessage(Arr(Byte) msg) {
   Var newMessage = (Var){.tag = VAR_STRING, .lock = FALSE, .string = msg };
   list_append_tv(makeInProgressS, &newMessage);
}

// The callback when "make" program returned its results
private void
makeFinished() {
   isMakeRunningS = false;
   Source source = (Source){
      .tag = SOURCE_LIST, .List = (ListSource){.c = makeInProgressS->first}
   };
   initAndUpdateTick(
      source, OUT locationStacksS + LOC_LIST_MAKE, curBook->o.errorFormat, true, S"make"
   );
   
   if (apply_autocmds(EVENT_QUICKFIXCMDPRE, S"make", curBook->currFileName, true, curBook) 
         && aborting()) {
      return;
   }
   
   if (makeOpenWhenDoneG) {
      Invocation invo;
      invo.comm = (CS)"lopen";
      invo.id = C_lopen;
      invo.line2 = 4;
      invo.arg = (CS)"m"; // the "make" location stack
      c_lOpen(&invo);
   } else {
      showNotification((CS)"make finished, use [m, ]m, :mopen");
   }
}

void
c_make(Invocation* invo UNUSED) {
   if (apply_autocmds(EVENT_QUICKFIXCMDPRE, S"make", curBook->currFileName, true, curBook) 
         && aborting()
   ) {
      return;
   }
   
   if (isMakeRunningS) {
      showNotification((CS)"make is already running");
      return;
   }
   
   doFlushAllBooks();

   Var vars[1];
   vars[0] = *allocStringVar((CS)"bash -c make");
   JobOptions jobOpts = (JobOptions){
      .finishNativeCb = &makeFinished,
      .errNativeCb = &makeReceiveMessage,
   };
   startJob(vars, NULL, &jobOpts, NULL);  
   initInProgressLl();
}

// Returns the number of entries in the current location list.
int
llGetSize(Invocation* invo) {
   LocationStack* stack;
   if ((stack = getStackForCommand(invo, FALSE)) == NULL)
      return 0;
   return getCurrent(stack)->count;
}

// Returns the number of valid entries in the current location list.
int
llGetValidSize(Invocation* invo){
   LocationStack* stack;
   LocLine   *lline;
   int i, sz = 0;
   int prev_fnum = 0;

   if ((stack = getStackForCommand(invo, FALSE)) == NULL)
      return 0;

   LocationList* ll = getCurrent(stack);
   FOR_ALL_LL_ITEMS(ll, lline, i) {
      if (lline->isValid) {
         if (invo->id == C_ldo)
            sz++;   // Count all valid entries
         ei (lline->fNum > 0 && lline->fNum != prev_fnum) {
            // Count the number of files
            sz++;
            prev_fnum = lline->fNum;
         }
      }
   }

   return sz;
}

//Return the current index of the location list. Return 0 if there is an error.
int
llGetCurrIndex(Invocation* invo) {
   LocationStack   *stack;

   if ((stack = getStackForCommand(invo, FALSE)) == NULL)
      return 0;

   return getCurrent(stack)->currentIdx;
}

//Return the current index in the location list (counting only valid
//entries). If no valid entries are in the list, then return 1.
int
llGetCurrValidIndex(Invocation* invo) {
   LocationStack* stack;
   int      i, eidx = 0;
   int      prev_fnum = 0;

   if ((stack = getStackForCommand(invo, FALSE)) == NULL)
      return 1;

   LocationList* ll = getCurrent(stack);
   LocLine* lline = ll->first;

   // check if the list has valid errors
   if (!listHasValidEntries(ll))
      return 1;

   for (i = 1; i <= ll->currentIdx && lline!= NULL; i++, lline = lline->next) {
      if (lline->isValid) {
         if (invo->id == C_lfdo) {
            if (lline->fNum > 0 && lline->fNum != prev_fnum) {
               // Count the number of files
               eidx++;
               prev_fnum = lline->fNum;
            }
         } else
            eidx++;
      }
   }

   return eidx ? eidx : 1;
}

//Get the 'n'th valid error entry in the location list.
//Used by :ldo and :lfdo commands.
//For :ldo returns the 'n'th valid error entry.
//For :lfdo returns the 'n'th valid file entry.
private int
nthValidEntry(LocationList* ll, int n, int fdo){
   int      i, eidx;
   int      prev_fnum = 0;

   // check if the list has valid errors
   if (!listHasValidEntries(ll))
      return 1;

   eidx = 0;
   LocLine* lline;
   FOR_ALL_LL_ITEMS(ll, lline, i) {
   if (lline->isValid) {
      if (fdo) {
         if (lline->fNum > 0 && lline->fNum != prev_fnum) {
            // Count the number of files
            eidx++;
            prev_fnum = lline->fNum;
         }
      } else
         eidx++;
   }

   if (eidx == n)
       break;
   }

   if (i <= ll->count)
      return i;
   else
      return 1;
}

//Location list movement. ":ll", ":lrewind", ":lfirst" and ":llast". ":ldo" and ":lfdo"
void
c_lMove(Invocation* invo) {
   LocationStack* stack;
   int      errornr;

   if ((stack = getStackForCommand(invo, TRUE)) == NULL)
      return;

   if (invo->addr_count > 0)
      errornr = (int)invo->line2;
   else {
      switch (invo->id) {
         case C_ll:
            errornr = 0;
            break;
         case C_lrewind:
         case C_lfirst:
            errornr = 1;
            break;
         default:
            errornr = 32767;
      }
   }

   // For cdo and ldo commands, jump to the nth valid error.
   // For cfdo and lfdo commands, jump to the nth valid file entry.
   if (invo->id == C_ldo || invo->id == C_lfdo) {
      errornr = nthValidEntry(
         getCurrent(stack),
         invo->addr_count > 0 ? (int)invo->line1 : 1,
         invo->id == C_lfdo
      );
   } 

   llJump(stack, 0, errornr, invo->forceit);
}

//":lnext", ":lNext", ":lprevious", ":lnfile", ":lNfile" and ":lpfile".
//Also, used by ":ldo" and ":lfdo" commands.
void
c_lNext(Invocation* invo) {
   LocationStack* stack;
   if ((stack = getStackForCommand(invo, TRUE)) == NULL)
      return;

   int errornr;
   if (invo->addr_count > 0 && (invo->id != C_ldo && invo->id != C_lfdo))
      errornr = (int)invo->line2;
   else
      errornr = 1;

   // Depending on the command jump to either next or previous entry/file.
   int dir;
   switch (invo->id) {
   case C_lnext: case C_ldo:
      dir = FORWARD;
      break;
   case C_lprevious:
      dir = BACKWARD;
      break;
   case C_lnfile: case C_lfdo:
      dir = FORWARD_FILE;
      break;
   case C_lpfile:
      dir = BACKWARD_FILE;
      break;
   default:
      dir = FORWARD;
      break;
   }

   llJump(stack, dir, errornr, invo->forceit);
}

//Find the first entry in the location list 'll' from buffer 'bnr'.
//The index of the entry is stored in 'errornr'.
//Return NULL if an entry is not found.
private LocLine *
findFirstEntryInBuf(LocationList* ll, int bnr, int* errornr){
   LocLine* lline = NULL;
   int idx = 0;

   // Find the first entry in this file
   FOR_ALL_LL_ITEMS(ll, lline, idx)
   if (lline->fNum == bnr)
      break;

   *errornr = idx;
   return lline;
}

//Find the first location entry on the same line as 'entry'. Updates 'errornr'
//with the error number for the first entry. Assumes the entries are sorted in
//the location list by line number.
private LocLine *
qf_find_first_entry_on_line(LocLine* entry, int* errornr) {
    while (!gotInterruptG
          && entry->prev
          && entry->fNum == entry->prev->fNum
          && entry->lNum == entry->prev->lNum) {
      entry = entry->prev;
      --*errornr;
   }

   return entry;
}

//Find the last location entry on the same line as 'entry'. Updates 'errornr'
//with the error number for the last entry. Assumes the entries are sorted in
//the location list by line number.
private LocLine *
qf_find_last_entry_on_line(LocLine* entry, int* errornr){
   while (!gotInterruptG && entry->next
          && entry->fNum == entry->next->fNum
          && entry->lNum == entry->next->lNum
   ) {
      entry = entry->next;
      ++*errornr;
   }

   return entry;
}

//Return TRUE if the specified location entry is
//  after the given line (linewise is TRUE)
//  or after the line and column.
private int
isEntryAfterPos(LocLine* lline, Pos* pos, int linewise){
   if (linewise)
      return lline->lNum > pos->lnum;
   else
      return (lline->lNum > pos->lnum || (lline->lNum == pos->lnum && lline->col > pos->col));
}

//Return TRUE if the specified location entry is
//before the given line (linewise is TRUE) or before the line and column.
private int
qf_entry_before_pos(LocLine *lline, Pos *pos, int linewise){
   if (linewise)
      return lline->lNum < pos->lnum;
   else
      return (lline->lNum < pos->lnum || (lline->lNum == pos->lnum && lline->col < pos->col));
}

//Return TRUE if the specified location entry is on or after the given line (linewise is TRUE)
//or on or after the line and column.
private int
qf_entry_on_or_after_pos(LocLine* lline, Pos* pos, int linewise){
   if (linewise)
      return lline->lNum >= pos->lnum;
   else
      return (lline->lNum > pos->lnum || (lline->lNum == pos->lnum && lline->col >= pos->col));
}

//Return TRUE if the specified location entry is
//on or before the given line (linewise is TRUE) or on or before the line and column.
private int
isEntryOnOrBeforePos(LocLine* lline, Pos* pos, int linewise) {
   if (linewise)
      return lline->lNum <= pos->lnum;
   else
      return (lline->lNum < pos->lnum || (lline->lNum == pos->lnum && lline->col <= pos->col));
}

//Find the first location entry after position 'pos' in buffer 'bnr'.
//If 'linewise' is TRUE, return the entry after the specified line and treat multiple entries on a 
//single line as one. Otherwise returns the entry after the specified line and column.
//'lline' points to the very first entry in the buffer and 'errornr' is the index of the very 
//first entry in the location list. Return NULL if an entry is not found after 'pos'.
private LocLine*
findEntryAfterPos(
   int      bnr,
   Pos      *pos,
   int      linewise,
   LocLine   *lline,
   int      *errornr
){
   if (isEntryAfterPos(lline, pos, linewise))
      // First entry is after position 'pos'
      return lline;

   // Find the entry just before or at the position 'pos'
   while (lline->next != NULL
          && lline->next->fNum == bnr
          && isEntryOnOrBeforePos(lline->next, pos, linewise)
   ) {
      lline = lline->next;
      ++*errornr;
   }

   if (lline->next == NULL || lline->next->fNum != bnr)
      // No entries found after position 'pos'
      return NULL;

   // Use the entry just after position 'pos'
   lline = lline->next;
   ++*errornr;

   return lline;
}

// Find the first location entry before position 'pos' in buffer 'bnr'.
// If 'linewise' is TRUE, returns the entry before the specified line and
// treats multiple entries on a single line as one. Otherwise returns the entry
// before the specified line and column.
// 'lline' points to the very first entry in the buffer and 'errornr' is the
// index of the very first entry in the location list.
// Return NULL if an entry is not found before 'pos'.
private LocLine *
findEntryBeforePos(
   int bnr,
   Pos* pos,
   int linewise,
   LocLine* lline,
   int* errornr
) {
   // Find the entry just before the position 'pos'
   while (lline->next != NULL
       && lline->next->fNum == bnr
       && qf_entry_before_pos(lline->next, pos, linewise)
   ) {
      lline = lline->next;
      ++*errornr;
   }

   if (qf_entry_on_or_after_pos(lline, pos, linewise))
      return NULL;

   if (linewise)
      // If multiple entries are on the same line, then use the first entry
      lline = qf_find_first_entry_on_line(lline, errornr);

   return lline;
}

//Find a location entry in 'll' closest to position 'pos' in buffer 'bnr' in the direction 'dir'.
private LocLine *
findClosestEntry(
   LocationList* ll,
   int bnr,
   Pos* pos,
   int dir,
   int linewise,
   OUT int* errornr
) {
   *errornr = 0;

   // Find the first entry in this file
   LocLine* lline = findFirstEntryInBuf(ll, bnr, errornr);
   if (lline == NULL)
      return NULL;      // no entry in this file

   if (dir == FORWARD)
      lline = findEntryAfterPos(bnr, pos, linewise, lline, errornr);
   else
      lline = findEntryBeforePos(bnr, pos, linewise, lline, errornr);

   return lline;
}

//Get the nth location entry below the specified entry.  Searches forward in
//the list. If linewise is TRUE, then treat multiple entries on a single line as one.
private void
getNthEntryBelow(LocLine *entry_arg, int n, int linewise, int *errornr) {
   LocLine *entry = entry_arg;

   while (n-- > 0 && !gotInterruptG) {
      int      first_errornr = *errornr;

      if (linewise)
          // Treat all the entries on the same line in this file as one
          entry = qf_find_last_entry_on_line(entry, errornr);

      if (entry->next == NULL
         || entry->next->fNum != entry->fNum)
      {
          if (linewise)
         *errornr = first_errornr;
          break;
      }

      entry = entry->next;
      ++*errornr;
    }
}

//Get the nth location entry above the specified entry.  Searches backwards in
//the list. If linewise is TRUE, then treat multiple entries on a single line as one.
private void
getNthEntryAbove(LocLine *entry, int n, int linewise, int *errornr){
   while (n-- > 0 && !gotInterruptG) {
      if (entry->prev == NULL || entry->prev->fNum != entry->fNum)
         break;

      entry = entry->prev;
      --*errornr;

      // If multiple entries are on the same line, then use the first entry
      if (linewise)
         entry = qf_find_first_entry_on_line(entry, errornr);
   }
}

//Find the n'th location entry adjacent to position 'pos' in buffer 'bnr' in
//the specified direction.  Returns the error number in the location list or 0
//if an entry is not found.
private int
findNthAdjacentEntry(
   LocationList* ll,
   int bnr,
   Pos* pos,
   int n,
   int dir,
   int linewise
) {
   int      errornr;
   // Find an entry closest to the specified position
   LocLine* adj_entry = findClosestEntry(ll, bnr, pos, dir, linewise, OUT &errornr);
   if (!adj_entry)
      return 0;

   if (--n > 0) {
      // Go to the n'th entry in the current buffer
      if (dir == FORWARD)
         getNthEntryBelow(adj_entry, n, linewise, &errornr);
      else
         getNthEntryAbove(adj_entry, n, linewise, &errornr);
   }

   return errornr;
}

// Jump to a location entry in the current file nearest to the current line. ":labove", ":lbelow"
void
c_lBelow(Invocation* invo) {
   LocationStack*stack;
   Unt dir;
   int errornr = 0;

   if (invo->addr_count > 0 && invo->line2 <= 0) {
      emsg(_(e_invalid_range));
      return;
   }

   Boole isBufferLinked = true;
   if (!(curBook->hasLocationEntry && isBufferLinked)) {
      emsg(_(e_no_entries_in_location_list));
      return;
   }

   if ((stack = getStackForCommand(invo, TRUE)) == NULL)
      return;

   LocationList* ll = getCurrent(stack);
   // check if the list has valid errors
   if (!listHasValidEntries(ll)) {
      emsg(_(e_no_entries_in_location_list));
      return;
   }

   if (invo->id == C_lbelow)
      // Forward motion commands
      dir = FORWARD;
   else
      dir = BACKWARD;

   Pos pos = curPor->cursor;
   // A location entry column number is 1 based whereas cursor column
   // number is 0 based. Adjust the column number.
   pos.col++;
   errornr = findNthAdjacentEntry(ll, curBook->fiNum, &pos,
            invo->addr_count > 0 ? invo->line2 : 0, dir,
            invo->id == C_lbelow || invo->id == C_labove
   );

   if (errornr > 0)
      llJump(stack, 0, errornr, false);
   else
      emsg(_(e_no_more_items));
}

// Return the autocmd name for the :mfile Commands
private CS
cfile_get_auname(CommIndex id){
   switch (id) {
   case C_lfile:       return S"lfile";
   case C_laddfile:  return S"laddfile";
   default:       return NULL;
   }
}

// ":lfile"/":laddfile" commands.
void
c_lFile(Invocation* invo) {
   Unt   idSave = 0;      // init for gcc
   int      res;

   CS auName = cfile_get_auname(invo->id);
   if (auName && apply_autocmds(EVENT_QUICKFIXCMDPRE, auName, NULL, false, curBook)) {
      if (aborting())
         return;
   }

   if (*invo->arg != ZERO)
      optChangeStringOptionDirect(S"errorfile", invo->arg, 0, 0);
   
   LocationStack* stack = identifyStackByInvo(invo);
   if (stack == NULL) {
      emsg(_(e_no_location_stack));
      return;
   }
   
   incrementLlBusyness();
   // This function is used by the :mfile and :maddfile commands.
   // :mfile always creates a new location list and may jump to the first entry.
   // :maddfile adds to an existing location list. If there is no
   // location list then a new list is created.
   res = llInitFromFile(
      stack, p_ef, curBook->o.errorFormat, (invo->id != C_laddfile), 
      copyCommandTitle(*invo->commline)
   );
   
   if (res >= 0)
      updateChangedTick(getCurrent(stack));
   idSave = getCurrent(stack)->id;
   if (auName != NULL)
      apply_autocmds(EVENT_QUICKFIXCMDPOST, auName, NULL, false, curBook);

   // Jump to the first error for a new list and if autocmds didn't free the list
   if (res > 0 && (invo->id == C_lfile) && isIdValid(stack, idSave))
      // display the first error
      jumpToFirstEntry(stack, idSave, invo->forceit);

   decrementLlBusyness();
}

// Return the vimgrep autocmd name.
private CS
vgr_get_auname(CommIndex id) {
   switch (id) {
   case C_vimgrep:     return (CS)"vimgrep";
   case C_vimgrepadd:  return (CS)"vimgrepadd";
   case C_grep:        return (CS)"grep";
   case C_grepadd:     return (CS)"grepadd";
   case C_elck:        return (CS)"elck";
   default:              return NULL;
   }
}

// Initialize the regmatch used by vimgrep for pattern "s".
private void
vgr_init_regmatch(RegMultilineMatch* regmatch, CS s) {
   // Get the search pattern: either white-separated or enclosed in //
   regmatch->regprog = NULL;

   if (s == NULL || *s == ZERO) {
      // Pattern is empty, use last search pattern.
      if (last_search_pat() == NULL) {
          emsg(_(e_no_previous_regular_expression));
          return;
      }
      regmatch->regprog = compileRegexp(last_search_pat(), RE_MAGIC);
   } else
      regmatch->regprog = compileRegexp(s, RE_MAGIC);

   regmatch->rmm_ic = p_ic;
   regmatch->rmm_maxcol = 0;
}

// Display a file name when vimgrep is running.
private void
vgr_display_fname(Byte *fname) {
   msg_start();
   CS p = msg_strtrunc(fname, TRUE);
   if (p == NULL)
      msg_outtrans(fname);
   else {
      msg_outtrans(p);
      eeglFree(p);
   }
   msg_clr_eos();
   msg_didout = FALSE;       // overwrite this message
   msg_nowait = TRUE;       // don't wait for this message
   msgColG = 0;
   out_flush();
}

// Load a dummy buffer to search for a pattern using vimgrep.
private Book*
vgr_load_dummy_buf(CS fname, CS dirname_start, CS dirname_now) {
   // Don't do Filetype autocommands to avoid loading syntax and
   // indent scripts, a great speed improvement.
   CS save_ei = au_event_disable(S",Filetype");

   // Load file into a buffer, so that 'fileencoding' is detected,
   // autocommands applied, etc.
   Book* book = loadDummyBook(fname, dirname_start, dirname_now);

   au_event_restore(save_ei);

   return book;
}

//Check whether a location list is valid. Autocmds may remove or change a location list when 
//vimgrep is running. If the list is not found, create a new list
private int
vgr_isIdValid(LocationStack* stack, Unt listId, CS title){
   // Verify that the location list was not freed by an autocmd
   if (!isIdValid(stack, listId)) {
      newLocList(stack, title);
   }

   if (restoreList(stack, listId) == FAIL)
      return FALSE;

   return TRUE;
}

//Search for a pattern in all the lines in a buffer and add the matching lines to a location list.
private int
vgr_match_buflines(
   LocationList* ll,
   CS fname,
   Book* book,
   CS spat,
   RegMultilineMatch* regmatch,
   long* tomatch,
   int duplicate_name,
   int flags
) {
   int      found_match = FALSE;
   long   lnum;
   ColNr   col;
   int      pat_len = (int)STRLEN(spat);
   if (pat_len > FUZZY_MATCH_MAX_LEN)
      pat_len = FUZZY_MATCH_MAX_LEN;

   for (lnum = 1; lnum <= book->mem.lineCount && *tomatch > 0; ++lnum) {
      col = 0;
      if (!(flags & VGR_FUZZY)) {
         // Regular expression match
         while (eeRegexec_multi(regmatch, curPor, book, lnum, col, NULL) > 0) {
         //Pass the book number so that it gets used even for a dummy book, unless duplicate_name 
         //is set, then the book will be wiped out below.
         if (addEntry(ll,
                NULL,   // dir
                fname,
                NULL,
                duplicate_name ? 0 : book->fiNum,
                memGetLine(book, regmatch->startpos[0].lnum + lnum, false),
                regmatch->startpos[0].lnum + lnum,
                regmatch->endpos[0].lnum + lnum,
                regmatch->startpos[0].col + 1,
                regmatch->endpos[0].col + 1,
                FALSE,   // vis_col
                NULL,   // search pattern
                0,      // nr
                0,      // type
                NULL,   // user_data
                TRUE   // valid
                ) == QF_FAIL)
         {
             gotInterruptG = TRUE;
             break;
         }
         found_match = TRUE;
         if (--*tomatch == 0)
            break;
         if ((flags & VGR_GLOBAL) == 0 || regmatch->endpos[0].lnum > 0)
            break;
         col = regmatch->endpos[0].col + (col == regmatch->endpos[0].col);
         if (col > memGetBookLen(book, lnum))
            break;
         }
      } else {
         Byte  *str = memGetLine(book, lnum, false);
         ColNr linelen = memGetBookLen(book, lnum);
         int       score;
         Unt   matches[FUZZY_MATCH_MAX_LEN];
         Unt   sz = ARRAY_LENGTH(matches);

         // Fuzzy string match
         CLEAR_FIELD(matches);
         while (fuzzy_match(str + col, spat, FALSE, &score, matches, sz) > 0) {
            //Pass the book number so that it gets used even for a dummy book, unless 
            //duplicate_name is set, then the book will be wiped out below.
            if (addEntry(ll,
                   NULL,   // dir
                   fname,
                   NULL,
                   duplicate_name ? 0 : book->fiNum,
                   str,
                   lnum,
                   0,
                   matches[0] + col + 1,
                   0,
                   FALSE,   // vis_col
                   NULL,   // search pattern
                   0,      // nr
                   0,      // type
                   NULL,   // user_data
                   TRUE   // valid
                   ) == QF_FAIL)
            {
                gotInterruptG = TRUE;
                break;
            }
            found_match = TRUE;
            if (--*tomatch == 0)
                break;
            if ((flags & VGR_GLOBAL) == 0)
                break;
            col = matches[pat_len - 1] + col + 1;
            if (col > linelen)
                break;
          }
      }
      line_breakcheck();
      if (gotInterruptG)
          break;
    }

    return found_match;
}

private void
jumpToFirstMatchAndUpdateDir(
   LocationStack* stack,
   Boole forceit,
   OUT Boole* redrawForDummy,
   OUT Book* firstMatchBook,
   CS target_dir
){
   Book* book = curBook;
   llJump(stack, 0, 0, forceit);
   if (book != curBook)
      // If we jumped to another book redrawing will already be taken care of.
      *redrawForDummy = FALSE;

   // Jump to the directory used after loading the book.
   if (curBook == firstMatchBook && target_dir != NULL) {
      Invocation ea;

      CLEAR_FIELD(ea);
      ea.arg = target_dir;
      ea.id = C_lcd;
      c_cd(&ea);
   }
}

//Process :vimgrep command arguments. The command syntax is:
//
//  :{count}vimgrep /{pattern}/[g][j] {file} ...
private int
vimgrepProcessArgs(Invocation* invo, OUT VimGrepArgs* args) {
   CLEAR_POINTER(args);

   args->regmatch.regprog = NULL;
   args->title = copyStr(copyCommandTitle(*invo->commline));

   if (invo->addr_count > 0)
      args->tomatch = invo->line2;
   else
      args->tomatch = MAXLNUM;

   // Get the search pattern: either white-separated or enclosed in //
   CS p = skipEeglGrepPat(invo->arg, &args->spat, &args->flags);
   if (!p) {
      emsg(_(e_invalid_search_pattern_or_delimiter));
      return FAIL;
   }

   vgr_init_regmatch(&args->regmatch, args->spat);
   if (args->regmatch.regprog == NULL)
      return FAIL;

   p = skipwhite(p);
   if (*p == ZERO) {
      emsg(_(e_file_name_missing_or_invalid_pattern));
      return FAIL;
   }
   ExpandMatch matches = (ExpandMatch){.c = args->fnames, .len = args->fcount };

   // Parse the list of arguments, wildcards have already been expanded.
   if ((bookParseAndExpandFnames(p, TRUE, OUT &matches) == FAIL) || args->fcount == 0) {
      emsg(_(e_no_match));
      return FAIL;
   }

   return OK;
}

// Return TRUE if "book" had an existing swap file, the current swap file does not end in ".swp".
private int
existing_swapfile(Book* book) {
   if (book->mem.mfile != NULL && book->mem.mfile->fName != NULL) {
      CS fname = book->mem.mfile->fName;
      Unt len = STRLEN(fname);
      return fname[len - 1] != 'p' || fname[len - 2] != 'w';
   }
   return FALSE;
}

// Search for a pattern in a list of files and populate the location list with the matches
private int
elckGrepFiles(
   LocationStack* stack,
   VimGrepArgs* invos,
   OUT Boole* redrawForDummy,
   OUT Book** firstMatchBook,
   OUT CS* target_dir
) {
   int status = FAIL;
   Unt idSave = getCurrent(stack)->id;
   int duplicate_name = FALSE;

   Byte dirnameStart[MAXPATHL];
   Byte dirnameNow[MAXPATHL];
   // Remember the current directory, because a BufRead autocommand that does
   // ":lcd %:p:h" changes the meaning of short path names.
   mch_dirname(dirnameStart, MAXPATHL);

   Tyme seconds = (Tyme)0;
   for (int fi = 0; fi < invos->fcount && !gotInterruptG && invos->tomatch > 0; ++fi) {
      CS fname = shorten_fname1(invos->fnames[fi]);
      if (time(NULL) > seconds) {
         // Display the file name every second or so, show the user we are working on it.
         seconds = time(NULL);
         vgr_display_fname(fname);
      }

      Book* book = booklistFindByNameExpandingLinks(invos->fnames[fi]);
      int using_dummy;
      if (!book || book->mem.mfile == NULL) {
         //Remember that a book with this name already exists.
         duplicate_name = (book != NULL);
         using_dummy = TRUE;
         *redrawForDummy = TRUE;
         book = vgr_load_dummy_buf(fname, dirnameStart, dirnameNow);
      } else
         // Use existing, loaded book.
         using_dummy = FALSE;

      //Check whether the location list is still valid. When loading a
      //book above, autocommands might have changed the location list.
      if (!vgr_isIdValid(stack, idSave, invos->title))
         goto theend;

      idSave = getCurrent(stack)->id;

      if (book == NULL) {
         if (!gotInterruptG)
            smsg(_("Cannot open file \"%s\""), fname);
      } else {
         //Try for a match in all lines of the book.
         //For ":1vimgrep" look for first match only.
         int found_match = vgr_match_buflines(getCurrent(stack),
             fname, book, invos->spat, &invos->regmatch,
             &invos->tomatch, duplicate_name, invos->flags);

         if (using_dummy) {
            if (found_match && *firstMatchBook == NULL)
               *firstMatchBook = book;
            if (duplicate_name) {
               //Never keep a dummy buffer if there is another book with the same name.
               wipeDummyBook(book, dirnameStart);
               book = NULL;
            } ei ((commModifierG.cmod_flags & CMOD_HIDE) == 0){
               //When no match was found we don't need to remember the book, wipe it out. If 
               //there was a match and it wasn't the first one or we won't jump there: only unload
               //the buffer. Ignore 'hidden' here, because it may lead to having too many swap files
               if (!found_match) {
                  wipeDummyBook(book, dirnameStart);
                  book = NULL;
               } ei (book != *firstMatchBook
                     || (invos->flags & VGR_NOJUMP)
                     || existing_swapfile(book)
               ) {
                  unloadDummyBook(book, dirnameStart);
                  // Keeping the book, remove the dummy flag.
                  book->flags &= ~BF_DUMMY;
                  book = NULL;
               }
            }

            if (book) {
               // Keeping the buffer, remove the dummy flag.
               book->flags &= ~BF_DUMMY;

               // If the buffer is still loaded we need to use the directory we jumped to below.
               if (book == *firstMatchBook
                      && *target_dir == NULL
                      && STRCMP(dirnameStart, dirnameNow) != 0)
                  *target_dir = copyStr(dirnameNow);

               // The book is still loaded, the Filetype autocommands need to be done now, in 
               // that book. need to be done (again). But not the portal-local options!
               AutocommSave   aco;
               auCommPrepareBook(&aco, book);
               if (curBook == book) {
                  apply_autocmds(EVENT_FILETYPE, book->fileType, book->currFileName, true, book);
                  auCommRestoreBook(&aco);
               }
            }
         }
      }
    }

    status = OK;

theend:
    return status;
}

//":vimgrep {pattern} file(s)". ":vimgrepadd {pattern} file(s)"
void
c_vimgrep(Invocation* invo) {
   if (!portCheckCanSetCurBookForceIt(invo->forceit))
      return;
      
   Boole redrawForDummy = false;
   Book* firstMatchBook = NULL;
   CS target_dir = NULL;

   CS auName = vgr_get_auname(invo->id);
   if (auName
         && apply_autocmds(EVENT_QUICKFIXCMDPRE, auName, curBook->currFileName, true, curBook)
         && aborting()
   )
      return;

   LocationStack* stack = locationStacksS + LOC_LIST_GREP;

   VimGrepArgs args;
   if (vimgrepProcessArgs(invo, OUT &args) == FAIL)
      goto theend;

   if ((invo->id != C_grepadd && invo->id != C_vimgrepadd) || isStackEmpty(stack)) {
      // make place for a new list
      newLocList(stack, args.title);
   } 

   incrementLlBusyness();

   int status = elckGrepFiles(stack, &args, OUT &redrawForDummy, OUT &firstMatchBook, OUT &target_dir);
   
   ExpandMatch matches = (ExpandMatch){.c = args.fnames, .len = args.fcount, .a = createArena() };
   if (status != OK) {
      decrementLlBusyness();
      goto theend;
   }

   LocationList* ll = getCurrent(stack);
   ll->noValidEntries = FALSE;
   ll->curr = ll->first;
   ll->currentIdx = 1;
   updateChangedTick(ll);

   updateBook(stack, NULL);

   //Remember the current location list identifier, so that we can check for
   //autocommands changing the current location list.
   Unt idSave = getCurrent(stack)->id;

   if (auName)
      apply_autocmds(EVENT_QUICKFIXCMDPOST, auName, curBook->currFileName, true, curBook);
   // The QuickFixCmdPost autocmd may free the location list. Check the list
   // is still valid.
   if (!isIdValid(stack, idSave) || restoreList(stack, idSave) == FAIL) {
      decrementLlBusyness();
      goto theend;
   }

   // Jump to first match.
   if (!isEmpty(getCurrent(stack))) {
      if ((args.flags & VGR_NOJUMP) == 0)
         jumpToFirstMatchAndUpdateDir(
               stack, invo->forceit, OUT &redrawForDummy, OUT firstMatchBook, target_dir
         );
   } else
      showErrFmtMsg(_(e_no_match_str_2), args.spat);

   decrementLlBusyness();

   //If we loaded a dummy buffer into the current portal, the autocommands
   //may have messed up things, need to redraw and recompute folds.
   if (redrawForDummy) {
      foldUpdateAll(curPor);
   }

theend:
   deleteArena(matches.a);
   eeglFree(args.title);
   eeglFree(target_dir);
   eeRegFree(args.regmatch.regprog);
}

//Restore current working directory to "dirname_start" if they differ, taking
//into account whether it is set locally or globally.
private void
restore_start_dir(CS dirname_start) {
   Byte dirname_now[MAXPATHL];
   mch_dirname(dirname_now, MAXPATHL);
   if (STRCMP(dirname_start, dirname_now) != 0) {
      // If the directory has changed, change it back by building up an
      // appropriate command and executing it.
      Invocation invo;

      CLEAR_FIELD(invo);
      invo.arg = dirname_start;
      invo.id = (curPor->localDir == NULL) ? C_cd : C_lcd;
      c_cd(&invo);
   }
}

//Load file "fname" into a dummy book and return the book pointer,
//placing the directory resulting from the book load into the
//"resulting_dir" pointer. "resulting_dir" must be allocated by the caller
//prior to calling this function. Restores directory to "dirname_start" prior
//to returning, if autocmds or the 'autochdir' option have changed it.
//
//If creating the dummy book does not fail, must call unloadDummyBook()
//or wipeDummyBook() later!
//
//Return NULL if it fails.
private Book*
loadDummyBook(
   CS fname,
   CS dirname_start,  // in: old directory
   CS resulting_dir  // out: new directory
){
   BookRef   newbufref;
   BookRef   newbuf_to_wipe;
   int      failed = TRUE;
   AutocommSave   aco;
   int      readfile_result;

   // Allocate a book without putting it in the book list.
   Book* newBook = bookNew(NULL, NULL, (LineNr)1, BLN_DUMMY);
   if (!newBook)
      return NULL;
   bookStoreInRef(OUT &newbufref, newBook);

   // Init the options.
   optsCopyToBook(newBook, BCO_ENTER);

   // need to open the memfile before opening a portal into the book
   if (ml_open(newBook) == OK) {
      // Make sure this book isn't wiped out by autocommands.
      ++newBook->locked;

      // set curPor/curBook to book and save a few things
      auCommPrepareBook(&aco, newBook);
      if (curBook == newBook) {
         // Need to set the filename for autocommands.
         (void)setfname(curBook, fname, NULL, FALSE);

         // Create swap file now to avoid the ATTENTION message.
         check_need_swap(TRUE);

         // Remove the "dummy" flag, otherwise autocommands may not work.
         curBook->flags &= ~BF_DUMMY;

         newbuf_to_wipe.c = NULL;
         readfile_result = readfile(
            fname, NULL, (LineNr)0, (LineNr)0, (LineNr)MAXLNUM, NULL, READ_NEW | READ_DUMMY
         );
         --newBook->locked;
         if (readfile_result == OK && !gotInterruptG && !(curBook->flags & BF_NEW)) {
            failed = FALSE;
            if (curBook != newBook) {
                // Bloody autocommands changed the book!  Can happen when
                // using netrw and editing a remote file.  Use the current
                // book instead, delete the dummy one after restoring the portal stuff.
                bookStoreInRef(OUT &newbuf_to_wipe, newBook);
                newBook = curBook;
            }
         }

         // restore curPor/curBook and a few other things
         auCommRestoreBook(&aco);

         if (newbuf_to_wipe.c != NULL && bookRefValid(&newbuf_to_wipe)) {
            block_autocmds();
            wipeDummyBook(newbuf_to_wipe.c, NULL);
            unblock_autocmds();
         }
      }

      // Add back the "dummy" flag, otherwise booklistFindName_stat() won't skip it.
      newBook->flags |= BF_DUMMY;
   }

   // When autocommands/'autochdir' option changed directory: go back.
   // Let the caller know that the resulting dir was first, in case it is important.
   mch_dirname(resulting_dir, MAXPATHL);
   restore_start_dir(dirname_start);

   if (!bookRefValid(&newbufref))
      return NULL;
   if (failed) {
      wipeDummyBook(newBook, dirname_start);
      return NULL;
   }
   return newBook;
}

//Wipe out the dummy book that loadDummyBook() created. Restores
//directory to "dirname_start" if not NULL prior to returning, if autocmds or
//the 'autochdir' option have changed it.
private void
wipeDummyBook(Book* book, CS dirname_start) {
   // If any autocommand opened a portal into the dummy book, close that portal.  
   // If we can't close them all then give up.
   while (book->countPortals > 0) {
      int       did_one = FALSE;
      Portal       *wp;

      if (firstPor->next != NULL)
         FOR_ALL_PORTALS(wp)
         if (wp->book == book) {
             if (closePortal(wp, FALSE) == OK)
            did_one = TRUE;
             break;
         }
      if (!did_one)
          goto fail;
   }

   if (curBook != book && book->countPortals == 0) {  // safety check
      Cleanup   cs;

      // Reset the error/interrupt/exception state here so that aborting()
      // returns FALSE when wiping out the book.  Otherwise it doesn't
      // work when gotInterruptG is set.
      enter_cleanup(&cs);

      bookWipe(book, TRUE);

      // Restore the error/interrupt/exception state if not discarded by a
      // new aborting error, interrupt, or uncaught exception.
      leave_cleanup(&cs);
      if (dirname_start != NULL)
          // When autocommands/'autochdir' option changed directory: go back.
          restore_start_dir(dirname_start);

      return;
    }

fail:
    // Keeping the book, remove the dummy flag.
    book->flags &= ~BF_DUMMY;
}

//Unload the dummy book that loadDummyBook() created. Restores
//directory to "dirname_start" prior to returning, if autocmds or the
//'autochdir' option have changed it.
private void
unloadDummyBook(Book* book, CS dirname_start) {
   if (curBook == book)      // safety check
      return;

   closeBook(NULL, book, DOBOOK_UNLOAD, FALSE, TRUE);

   // When autocommands/'autochdir' option changed directory: go back.
   restore_start_dir(dirname_start);
}

//Copy the specified location entry items into a new bag and append the bag
//to 'list'.  Returns OK on success.
private int
get_qfline_items(LocLine *lline, List *list) {
   // Handle entries with a non-existing book number.
   int bufnum = lline->fNum;
   if (bufnum != 0 && (bookFindFileByBookNr(bufnum) == NULL))
      bufnum = 0;

   Bag* bag = allocBag();
   if (listAppendBag(list, bag) == FAIL)
      return FAIL;

   Byte buf[2];
   buf[0] = lline->kind;
   buf[1] = ZERO;
   return (bagAddNumber(bag, S"bufnr", (long)bufnum) == FAIL
          || bagAddNumber(bag, S"lnum",     (long)lline->lNum) == FAIL
          || bagAddNumber(bag, S"end_lnum", (long)lline->endLNum) == FAIL
          || bagAddNumber(bag, S"col",      (long)lline->col) == FAIL
          || bagAddNumber(bag, S"end_col",  (long)lline->endCol) == FAIL
          || bagAddNumber(bag, S"vcol",     (long)lline->visCol) == FAIL
          || bagAddNumber(bag, S"nr",       (long)lline->errNum) == FAIL
          || bagAddString(bag, S"module", lline->moduleName) == FAIL
          || bagAddString(bag, S"pattern", lline->pattern) == FAIL
          || bagAddString(bag, S"text", lline->text) == FAIL
          || bagAddString(bag, S"type", buf) == FAIL
          || (lline->userData.tag != VAR_UNKNOWN
               && bagAddVar(bag, S"user_data", &lline->userData) == FAIL )
          || bagAddNumber(bag, S"valid", (long)lline->isValid) == FAIL
   ) ? FAIL : OK;
}

//Add each item from a location list to the output list as a dictionary. If ind is -1, use the 
//current list. Otherwise, use the specified list. If entryId is not 0, then return only the 
//specified entry. Otherwise return all the entries.
private int
exportLocList(
   LocationStack* stack,
   int ind,
   int entryId,
   OUT List* list
){
   if (!stack)
      return FAIL;

   if (entryId < 0)
      return OK;

   if (ind == INVALID_LL_IND)
      ind = stack->currList;

   if (ind >= stack->listcount)
      return FAIL;

   LocationList* ll = getList(stack, ind);
   if (isEmpty(ll))
      return FAIL;

   LocLine* lline;
   int i;
   FOR_ALL_LL_ITEMS(ll, lline, i) {
      if (entryId > 0) {
         if (entryId == i)
            return get_qfline_items(lline, list);
      } ei (get_qfline_items(lline, list) == FAIL)
         return FAIL;
   }

   return OK;
}

// Flags used by getqflist()/getloclist() to determine which fields to return.
enum {
   QF_GETLIST_NONE    = 0x0,
   QF_GETLIST_TITLE   = 0x1,
   QF_GETLIST_ITEMS   = 0x2,
   QF_GETLIST_NR      = 0x4,
   QF_GETLIST_WINID   = 0x8,
   QF_GETLIST_CONTEXT = 0x10,
   QF_GETLIST_ID      = 0x20,
   QF_GETLIST_IDX     = 0x40,
   QF_GETLIST_SIZE    = 0x80,
   QF_GETLIST_TICK    = 0x100,
   QF_GETLIST_QFBUFNR = 0x200,
   QF_GETLIST_QFTF    = 0x400,
   QF_GETLIST_ALL     = 0x800
};

//Parse text from 'di' and return the location list items.
//Existing location lists are not modified.
private int
getList_from_lines(Bag* specifics, DictItem* di, OUT Bag* retBag) {
   // Only a List value is supported
   if (di->c.tag != VAR_LIST || di->c.list == NULL)
      return FAIL;

   int status = FAIL;
   CS errorformat = curBook->o.errorFormat;
   
   //If errorformat is supplied then use it, otherwise use the [errorformat] option
   DictItem* item;
   if ((item = bagFind(specifics, tConst("efm"))) != NULL) {
      if (item->c.tag != VAR_STRING || item->c.string == NULL)
         return FAIL;
      errorformat = item->c.string;
   }

   List* l = list_alloc();

   LocationStack* stack = ALLOC_CLEAR_ONE_ID(LocationStack, aid_ll_module);
	if (!stack)
      return FAIL;
   stack->refcount++;
   stack->bufNum = INVALID_LL_BUFNR;
   stack->lists = allocateLocList(STACK_CAPACITY);
   if (stack->lists == NULL) {
      return FAIL;
   }
   
   Source source = (Source){.tag = SOURCE_LIST, .List = (ListSource){.c = di->c.list->first}};
   if (initWorker(source, stack, 0, errorformat, true, NULL) > 0) {
      (void)exportLocList(stack, 0, 0, l);
      freeAList(&stack->lists[0]);
   }

   freeAList_lists(stack);
   bagAddList(retBag, S"items", l);
   status = OK;

   return status;
}

//Return the location list portal identifier in the current tab.
private int
getLlPortalId(LocationStack* stack) {
   // The location portal can be opened even if the location list is not set
   // using ":mopen". This is not true for location lists.
   if (!stack)
      return 0;
   Portal* po = findPortalIntoLocList(stack);
   return (po) ? po->id : 0;
}

//Return the number of the book displayed in the location list portal. If there is no book 
//associated with the list or the book is wiped out, then returns 0.
private int
qf_getprop_qfbufnr(LocationStack* stack, Bag* retBag) {
   int   bufnum = 0;

   if (stack && bookFindFileByBookNr(stack->bufNum) != NULL)
      bufnum = stack->bufNum;

   return bagAddNumber(retBag, S"qfbufnr", bufnum);
}

// Convert the keys in 'specifics' to location list property flags.
private Unt
importKeysFromDict(Bag* specifics) {
   Unt flags = QF_GETLIST_NONE;

   if (bagHasKey(specifics, tConst("all"))) {
      flags |= QF_GETLIST_ALL;
   }

   if (bagHasKey(specifics, tConst("title")))
      flags |= QF_GETLIST_TITLE;

   if (bagHasKey(specifics, tConst("nr")))
      flags |= QF_GETLIST_NR;

   if (bagHasKey(specifics, tConst("winid")))
      flags |= QF_GETLIST_WINID;

   if (bagHasKey(specifics, tConst("context")))
      flags |= QF_GETLIST_CONTEXT;

   if (bagHasKey(specifics, tConst("id")))
      flags |= QF_GETLIST_ID;

   if (bagHasKey(specifics, tConst("items")))
      flags |= QF_GETLIST_ITEMS;

   if (bagHasKey(specifics, tConst("idx")))
      flags |= QF_GETLIST_IDX;

   if (bagHasKey(specifics, tConst("size")))
      flags |= QF_GETLIST_SIZE;

   if (bagHasKey(specifics, tConst("changedtick")))
      flags |= QF_GETLIST_TICK;

   if (bagHasKey(specifics, tConst("qfbufnr")))
      flags |= QF_GETLIST_QFBUFNR;

   if (bagHasKey(specifics, tConst("quickfixtextfunc")))
      flags |= QF_GETLIST_QFTF;

   return flags;
}

//Return the location list index based on 'nr' or 'id' in 'specifics'.
//If 'nr' and 'id' are not present in 'specifics' then return the current location list index.
//If 'nr' is zero then return the current location list index.
//If 'nr' is '$' then return the last location list index.
//If 'id' is present then return the index of the location list with that id.
//If 'id' is zero then return the location list index specified by 'nr'.
//Return -1, if location list is not present or if the stack is empty.
private int
qf_getprop_qfidx(LocationStack* stack, Bag* specifics) {
   DictItem   *di;

   int ind = stack->currList;   // default is the current list
   if ((di = bagFind(specifics, tConst("nr"))) != NULL) {
      // Use the specified location list
      if (di->c.tag == VAR_NUMBER) {
         // for zero use the current list
         if (di->c.number != 0) {
            ind = di->c.number - 1;
            if (ind < 0 || ind >= stack->listcount)
                ind = INVALID_LL_IND;
         }
      } ei (di->c.tag == VAR_STRING
         && di->c.string != NULL
         && STRCMP(di->c.string, "$") == 0)
          // Get the last location list number
          ind = stack->listcount - 1;
      else
          ind = INVALID_LL_IND;
    }

   if ((di = bagFind(specifics, tConst("id"))) != NULL) {
      // Look for a list with the specified id
      if (di->c.tag == VAR_NUMBER) {
         // For zero, use the current list or the list specified by 'nr'
         if (di->c.number != 0)
            ind = idToNr(stack, di->c.number);
      } else
         ind = INVALID_LL_IND;
   }

   return ind;
}

// Return default values for location list properties in retBag.
private int
getPropertyDefaults(LocationStack* stack, Unt flags, OUT Bag* retBag) {
   int      status = OK;

   if (flags & QF_GETLIST_TITLE)
      status = bagAddString(retBag, S"title", (CS)"");
   if ((status == OK) && (flags & QF_GETLIST_ITEMS)) {
      List* l = list_alloc();
      status = bagAddList(retBag, S"items", l);
   }
   if ((status == OK) && (flags & QF_GETLIST_NR))
      status = bagAddNumber(retBag, S"nr", 0);
   if ((status == OK) && (flags & QF_GETLIST_WINID))
      status = bagAddNumber(retBag, S"winid", getLlPortalId(stack));
   if ((status == OK) && (flags & QF_GETLIST_CONTEXT))
      status = bagAddString(retBag, S"context", (CS)"");
   if ((status == OK) && (flags & QF_GETLIST_ID))
      status = bagAddNumber(retBag, S"id", 0);
   if ((status == OK) && (flags & QF_GETLIST_IDX))
      status = bagAddNumber(retBag, S"idx", 0);
   if ((status == OK) && (flags & QF_GETLIST_SIZE))
      status = bagAddNumber(retBag, S"size", 0);
   if ((status == OK) && (flags & QF_GETLIST_TICK))
      status = bagAddNumber(retBag, S"changedtick", 0);
   if ((status == OK) && (flags & QF_GETLIST_QFBUFNR))
      status = qf_getprop_qfbufnr(stack, retBag);
   if ((status == OK) && (flags & QF_GETLIST_QFTF))
      status = bagAddString(retBag, S"quickfixtextfunc", S"");

   return status;
}

// Return the location list title as 'title' in retBag
private int
qf_getprop_title(LocationList* ll, Bag* retBag) {
   return bagAddString(retBag, S"title", ll->title);
}

//Return the location list items/entries as 'items' in retBag.
//If eidx is not 0, then return the item at the specified index.
private int
exportToDict(LocationStack* stack, int ind, int eidx, OUT Bag* retBag) {
   int      status = OK;
   List   *l = list_alloc();
   (void)exportLocList(stack, ind, eidx, l);
   bagAddList(retBag, S"items", l);

   return status;
}

// Return the location list context (if any) as 'context' in retBag.
private int
exportContext(LocationList* ll, OUT Bag* retBag) {
   int status;

   if (ll->qf_ctx != NULL) {
      DictItem* di = dictitem_alloc(tConst("context"));
      copy_tv(OUT &di->c, ll->qf_ctx);
      status = bagAdd(retBag, di);
      if (status == FAIL)
         dictitem_free(di);
   } else
      status = bagAddString(retBag, S"context", (CS)"");

   return status;
}

//Return the current location list index as 'idx' in retBag.
//If a specific entry index (eidx) is supplied, then use that.
private int
qf_getprop_idx(LocationList* ll, int eidx, Bag* retBag) {
   if (eidx == 0) {
      eidx = ll->currentIdx;
      if (isEmpty(ll))
         // For empty lists, current index is set to 0
         eidx = 0;
   }
   return bagAddNumber(retBag, S"idx", eidx);
}

// Return the 'quickfixtextfunc' function of a location list
private int
qf_getprop_qftf(LocationList* ll, Bag* retBag) {
   int status;
   if (ll->textFn.name) {
      Var   tv;
      putCallback(OUT &tv, &ll->textFn);
      status = bagAddVar(retBag, S"quickfixtextfunc", &tv);
      clearVar(&tv);
   } else
      status = bagAddString(retBag, S"quickfixtextfunc", (CS)"");

   return status;
}

// Return location list details (title) as a dictionary. 'specifics' contains the details to 
// return. If 'list_idx' is -1, then current list is used. Otherwise the specified list is used.
private int
getProperties(LocationStack* stack, Bag* specifics, OUT Bag* retBag) {
   int status = OK;
   int ind = INVALID_LL_IND;
   int eidx = 0;
   DictItem* di;

   if ((di = bagFind(specifics, tConst("lines"))) != NULL)
      return getList_from_lines(specifics, di, OUT retBag);


   Unt flags = importKeysFromDict(specifics);

   if (!isStackEmpty(stack))
      ind = qf_getprop_qfidx(stack, specifics);

   // List is not present or is empty
   if (isStackEmpty(stack) || ind == INVALID_LL_IND)
      return getPropertyDefaults(stack, flags, retBag);

   LocationList* ll = getList(stack, ind);

    // If an entry index is specified, use that
   if ((di = bagFind(specifics, tConst("idx"))) != NULL) {
      if (di->c.tag != VAR_NUMBER)
         return FAIL;
      eidx = di->c.number;
   }

   if (flags & QF_GETLIST_TITLE)
      status = qf_getprop_title(ll, retBag);
   if ((status == OK) && (flags & QF_GETLIST_NR))
      status = bagAddNumber(retBag, S"nr", ind + 1);
   if ((status == OK) && (flags & QF_GETLIST_WINID))
      status = bagAddNumber(retBag, S"winid", getLlPortalId(stack));
   if ((status == OK) && (flags & QF_GETLIST_ITEMS))
      status = exportToDict(stack, ind, eidx, retBag);
   if ((status == OK) && (flags & QF_GETLIST_CONTEXT))
      status = exportContext(ll, retBag);
   if ((status == OK) && (flags & QF_GETLIST_ID))
      status = bagAddNumber(retBag, S"id", ll->id);
   if ((status == OK) && (flags & QF_GETLIST_IDX))
      status = qf_getprop_idx(ll, eidx, retBag);
   if ((status == OK) && (flags & QF_GETLIST_SIZE))
      status = bagAddNumber(retBag, S"size", ll->count);
   if ((status == OK) && (flags & QF_GETLIST_TICK))
      status = bagAddNumber(retBag, S"changedtick", ll->changedTick);
   if ((status == OK) && (flags & QF_GETLIST_QFBUFNR))
      status = qf_getprop_qfbufnr(stack, retBag);
   if ((status == OK) && (flags & QF_GETLIST_QFTF))
      status = qf_getprop_qftf(ll, retBag);

   return status;
}

// Add a new location entry to list at 'ind' in the stack 'stack' from the
// items in the dict 'd'. If it is a valid error entry, then set 'valid_entry' to TRUE.
private int
addEntry_from_dict(LocationList* ll, Bag* d, int first_entry, int* valid_entry){
   static int   did_bufnr_emsg;

   if (first_entry)
      did_bufnr_emsg = FALSE;

   CS filename = bagGetString(d,tConst("filename"), TRUE);
   CS module = bagGetString(d,tConst("module"), TRUE);
   int bufnum = (int)bagGetNumber(d, tConst("bufnr"));
   long lnum = (int)bagGetNumber(d, tConst("lnum"));
   long end_lnum = (int)bagGetNumber(d, tConst("end_lnum"));
   int col = (int)bagGetNumber(d, tConst("col"));
   int end_col = (int)bagGetNumber(d, tConst("end_col"));
   int vcol = (int)bagGetNumber(d, tConst("vcol"));
   int nr = (int)bagGetNumber(d, tConst("nr"));
   CS type = bagGetString(d, tConst("type"), TRUE);
   CS pattern = bagGetString(d, tConst("pattern"), TRUE);
   CS text = bagGetString(d, tConst("text"), TRUE);
   if (text == NULL)
      text = copyStr(E);
   Var user_data;
   user_data.tag = VAR_UNKNOWN;
   bagGetVar(d, tConst("user_data"), &user_data);

   Boole valid = true;
   if ((filename == NULL && bufnum == 0) || (lnum == 0 && pattern == NULL))
      valid = FALSE;

   // Mark entries with non-existing book number as not valid. Give the error message only once.
   if (bufnum != 0 && (bookFindFileByBookNr(bufnum) == NULL)) {
      if (!did_bufnr_emsg) {
         did_bufnr_emsg = TRUE;
         showErrFmtMsg(_(e_book_nr_not_found), bufnum);
      }
      valid = false;
      bufnum = 0;
   }

   // If the 'valid' field is present it overrules the detected value.
   if (bagHasKey(d, tConst("valid")))
      valid = bagGetBool(d, tConst("valid"), false);

   int status = addEntry(ll,
        NULL,      // dir
        filename,
        module,
        bufnum,
        text,
        lnum,
        end_lnum,
        col,
        end_col,
        vcol,      // vis_col
        pattern,   // search pattern
        nr,
        type == NULL ? ZERO : *type,
        &user_data,
        valid
   );

   eeglFree(filename);
   eeglFree(module);
   eeglFree(pattern);
   eeglFree(text);
   eeglFree(type);
   clearVar(&user_data);

   if (valid)
      *valid_entry = TRUE;

   return status;
}

// Check if `entry` is closer to the target than `other_entry`.
//
// Only return TRUE if `entry` is definitively closer. If it's further away, or there's not 
// enough information to tell, return FALSE.
private int
entry_is_closer_to_target(
   LocLine* entry,
   LocLine* other_entry,
   int target_fnum,
   int target_lnum,
   int target_col
) {
   // First, compare entries to target file.
   if (!target_fnum)
      // Without a target file, we can't know which is closer.
      return FALSE;

   int is_target_file = entry->fNum && entry->fNum == target_fnum;
   int other_is_target_file = other_entry->fNum && other_entry->fNum == target_fnum;
   if (!is_target_file && other_is_target_file)
      return FALSE;
   ei (is_target_file && !other_is_target_file)
      return TRUE;

    // Both entries are pointing at the exact same file. Now compare line
    // numbers.
   if (!target_lnum)
      // Without a target line number, we can't know which is closer.
      return FALSE;

   int line_distance = entry->lNum ? labs(entry->lNum - target_lnum) : INT_MAX;
   int other_line_distance = other_entry->lNum ? labs(other_entry->lNum - target_lnum) : INT_MAX;
   if (line_distance > other_line_distance)
      return FALSE;
   ei (line_distance < other_line_distance)
      return TRUE;

   //Both entries are pointing at the exact same line number (or no line
   //number at all). Now compare columns.
   if (!target_col)
      // Without a target column, we can't know which is closer.
      return FALSE;

   int column_distance = entry->col ? abs(entry->col - target_col) : INT_MAX;
   int other_column_distance = other_entry->col ? abs(other_entry->col - target_col): INT_MAX;
   if (column_distance > other_column_distance)
      return FALSE;
   ei (column_distance < other_column_distance)
      return TRUE;

   // It's a complete tie! The exact same file, line, and column.
   return FALSE;
}

// Add list of entries to location list. Each list entry is a dictionary with item information.
private int
addEntries(
   OUT LocationStack* stack,
   int ind,
   List* list,
   CS title,
   LocListAction action
){
   Bag   *d;
   LocLine   *oldLast = NULL;
   int      retval = OK;
   int      valid_entry = FALSE;

   // If there's an entry selected in the location list, remember its location
   // (file, line, column), so we can select the nearest entry in the updated list.
   int prev_fnum = 0;
   int prev_lnum = 0;
   int prev_col = 0;
   LocationList* ll = getList(stack, ind);
   if (ll->curr) {
      prev_fnum = ll->curr->fNum;
      prev_lnum = ll->curr->lNum;
      prev_col = ll->curr->col;
   }

   int select_first_entry = FALSE;
   int select_nearest_entry = FALSE;

   if (action == LL_ACTION_NEW || ind == stack->listcount) {
      select_first_entry = TRUE;
      // make place for a new list
      newLocList(stack, title);
      ind = stack->currList;
      ll = getList(stack, ind);
   } ei (action == LL_ACTION_ADD) {
      if (isEmpty(ll))
         // Appending to empty list, select first entry.
         select_first_entry = TRUE;
      else
         // Adding to existing list, use last entry.
         oldLast = ll->last;
   } ei (action == LL_ACTION_REPLACE) {
      select_first_entry = TRUE;
      freeItems(ll);
      storeTitle(ll, title);
   } ei (action == LL_ACTION_UPDATE) {
      select_nearest_entry = TRUE;
      freeItems(ll);
      storeTitle(ll, title);
   }

   LocLine *entry_to_select = NULL;
   int entry_to_select_index = 0;

   ListItem* li;
   FOR_ALL_LIST_ITEMS(list, li) {
      if (li->c.tag != VAR_BAG)
         continue; // Skip non-dict items

      d = li->c.bag;
      if (!d)
         continue;

      retval = addEntry_from_dict(ll, d, li == list->first, &valid_entry);
      if (retval == QF_FAIL)
         break;

      LocLine *entry = ll->last;
      if ((select_first_entry && entry_to_select == NULL)
          || (select_nearest_entry &&
               (entry_to_select == NULL
                  || entry_is_closer_to_target(
                        entry, entry_to_select, prev_fnum, prev_lnum, prev_col)
               )
             )
      ){
         entry_to_select = entry;
         entry_to_select_index = ll->count;
      }
   }

   // Check if any valid error entries are added to the list.
   if (valid_entry)
      ll->noValidEntries = FALSE;
   ei (ll->currentIdx == 0)
      // no valid entry
      ll->noValidEntries = TRUE;

   // Set the current error.
   if (entry_to_select) {
      ll->curr = entry_to_select;
      ll->currentIdx = entry_to_select_index;
   }

   // Don't update the cursor in location portal when appending entries
   updateBook(stack, oldLast);

   return retval;
}

// Get the location list index from 'nr' or 'id'
private int
qf_setprop_get_qfidx(
   LocationStack* stack,
   Bag* specifics,
   LocListAction action,
   OUT Boole* newlist
){
   DictItem   *di;
   int      ind = stack->currList;    // default is the current list

   if ((di = bagFind(specifics, tConst("nr"))) != NULL) {
      // Use the specified location list
      if (di->c.tag == VAR_NUMBER) {
         // for zero use the current list
         if (di->c.number != 0)
            ind = di->c.number - 1;

         if ((action == LL_ACTION_ADD) && ind == stack->listcount) {
            // When creating a new list, accept ind pointing to the next
            // non-available list and add the new list at the end of the stack.
            *newlist = TRUE;
            ind = isStackEmpty(stack) ? 0 : stack->listcount - 1;
         } ei (ind < 0 || ind >= stack->listcount)
            return INVALID_LL_IND;
         else
            *newlist = FALSE;   // use the specified list
      } ei (di->c.tag == VAR_STRING
            && di->c.string != NULL
            && STRCMP(di->c.string, "$") == 0) {
         if (!isStackEmpty(stack))
            ind = stack->listcount - 1;
         ei (*newlist)
            ind = 0;
         else
            return INVALID_LL_IND;
      } else
          return INVALID_LL_IND;
   }

   if (!*newlist && (di = bagFind(specifics, tConst("id"))) != NULL) {
      // Use the location list with the specified id
      if (di->c.tag != VAR_NUMBER)
         return INVALID_LL_IND;

      return idToNr(stack, di->c.number);
   }

   return ind;
}

private int
setTitle(LocationStack* stack, int ind, Bag* specifics, DictItem* di) {
   LocationList* ll = getList(stack, ind);

   if (di->c.tag != VAR_STRING)
      return FAIL;

   eeglFree(ll->title);
   ll->title = bagGetString(specifics, tConst("title"), TRUE);
   if (ind == stack->currList)
      updateTitleVar(stack);

   return OK;
}

// Set location list items/entries.
private int
setItems(LocationStack* stack, int ind, DictItem* di, LocListAction action) {
   if (di->c.tag != VAR_LIST)
      return FAIL;

   CS title_save = copyStr(stack->lists[ind].title);
   int retval = addEntries(stack, ind, di->c.list, title_save, action);
   eeglFree(title_save);

   return retval;
}

// Set location list entries from a list of lines.
private int
setLinesFromList(
   LocationStack* stack,
   int ind,
   Bag* specifics,
   DictItem* di,
   LocListAction action
){
   CS errorformat = curBook->o.errorFormat;
   DictItem* efm_di;
   int retval = FAIL;

   // Use the user supplied errorformat settings (if present)
   if ((efm_di = bagFind(specifics, tConst("efm"))) != NULL) {
      if (efm_di->c.tag != VAR_STRING || efm_di->c.string == NULL)
         return FAIL;
      errorformat = efm_di->c.string;
   }

   // Only a List value is supported
   if (di->c.tag != VAR_LIST || di->c.list == NULL)
      return FAIL;

   if (action == LL_ACTION_REPLACE || action == LL_ACTION_UPDATE)
      freeItems(&stack->lists[ind]);
   Source source = (Source){ .tag = SOURCE_LIST, .List = (ListSource){.c = di->c.list->first} };
   if (initWorker(source, stack, ind, errorformat, false, NULL) >= 0)
      retval = OK;

   return retval;
}

// Set location list context.
private int
setContext(LocationList* ll, DictItem* di) {
   freeVar(ll->qf_ctx);
   Var* ctx =  allocVar();
   if (ctx)
      copy_tv(OUT ctx, &di->c);
   ll->qf_ctx = ctx;

   return OK;
}

// Set the current index in the specified location list
private int
setCurrentIndex(LocationStack *stack, LocationList *ll, DictItem *di){
   Boole denote = false;
   int  old_qfidx;
   LocLine   *curr;

   // If the specified index is '$', then use the last entry
   int newidx;
   if (di->c.tag == VAR_STRING && di->c.string && STRCMP(di->c.string, "$") == 0) {
      newidx = ll->count;
   } else {
      // Otherwise use the specified index
      newidx = varGetNumberChk(&di->c, OUT &denote);
      if (denote)
         return FAIL;
   }

   if (newidx < 1)      // sanity check
      return FAIL;
   if (newidx > ll->count)
      newidx = ll->count;

   old_qfidx = ll->currentIdx;
   curr = getNthEntry(ll, newidx, &newidx);
   if (!curr)
      return FAIL;
   ll->curr = curr;
   ll->currentIdx = newidx;

   // If the current list is modified and a location portal into it is open, then Update it
   if (getCurrent(stack)->id == ll->id)
      (void)updatePortalPos(stack, old_qfidx);

   return OK;
}

// Set the current callback in the specified location list
private int
setTextFn(LocationList *ll, DictItem *di) {
   evFreeCallback(&ll->textFn);
   Callback callback = get_callback(&di->c);
   if (!callback.name || *callback.name == ZERO)
      return OK;

   set_callback(&ll->textFn, &callback);
   if (callback.needsFreeing)
      eeglFree(callback.name);

   return OK;
}

// Set location list properties (title, items, context). Also used to add items from parsing a list
// of lines. Used by the setqflist() and setloclist() Vim script functions.
private int
setProperties(LocationStack *stack, Bag *specifics, LocListAction action, CS title) {
   int      retval = FAIL;
   Boole newlist = (action == LL_ACTION_NEW || isStackEmpty(stack));

   int ind = qf_setprop_get_qfidx(stack, specifics, action, OUT &newlist);
   if (ind == INVALID_LL_IND)   // List not found
      return FAIL;

   if (newlist) {
      stack->currList = ind;
      newLocList(stack, title);
      ind = stack->currList;
   }

   LocationList* ll = getList(stack, ind);
   DictItem   *di;
   if ((di = bagFind(specifics, tConst("title"))) != NULL)
      retval = setTitle(stack, ind, specifics, di);
   if ((di = bagFind(specifics, tConst("items"))) != NULL)
      retval = setItems(stack, ind, di, action);
   if ((di = bagFind(specifics, tConst("lines"))) != NULL)
      retval = setLinesFromList(stack, ind, specifics, di, action);
   if ((di = bagFind(specifics, tConst("context"))) != NULL)
      retval = setContext(ll, di);
   if ((di = bagFind(specifics, tConst("idx"))) != NULL)
      retval = setCurrentIndex(stack, ll, di);
   if ((di = bagFind(specifics, tConst("quickfixtextfunc"))) != NULL)
      retval = setTextFn(ll, di);

   if (newlist || retval == OK)
      updateChangedTick(ll);
   if (newlist)
      updateBook(stack, NULL);

   return retval;
}

// Free an entire location list stack. If there is a portal into it, then clear it.
private void
freeTheStack(LocationStack* stack) {
   Portal* mbLocPortal = findPortalIntoLocList(stack);
   
   if (mbLocPortal != NULL) {
      // If the location list portal is open, then clear it
      if (stack->currList < stack->listcount)
          freeAList(getCurrent(stack));
      updateBook(stack, NULL);
   }
   freeAList_list_stack_items(stack);
}

// Populate the location list with the items supplied in the list
// of dictionaries. "title" will be copied to w:quickfix_title.
// Otherwise create a new list. When "specifics" is not NULL then only set some properties.
int
setLocationList(
   OUT LocationStack* stack,
   List* newContent,
   LocListAction action,
   CS title,
   Bag* specific
) {
   if (stack == NULL)
      return FAIL;

   int retval = OK;
   if (action == LL_ACTION_FREE) {
      // Free the entire location list stack
      freeTheStack(stack);
      return OK;
   }

   // A dict argument cannot be specified with a non-empty list argument
   if (newContent->len != 0 && specific != NULL) {
      showErrFmtMsg(_(e_invalid_argument_str), _("cannot have both a list and a \"specific\" argument"));
      return FAIL;
   }

   incrementLlBusyness();

   if (!specific)
      retval = addEntries(stack, stack->currList, newContent, title, action);
   if (retval == OK)
      updateChangedTick(getCurrent(stack));
   else {
      retval = setProperties(stack, specific, action, title);
   }

   decrementLlBusyness();

   return retval;
}

private int
checkIfUserDataLocked(LocationStack* stack, int copyID) {
   int abort = FALSE;
   for (int i = 0; i < stack->cap && !abort; ++i) {
      LocationList *ll = &stack->lists[i];
      if (!ll->hasUserData)
         continue;
      LocLine *lline;
      int j;
      FOR_ALL_LL_ITEMS(ll, lline, j) {
         Var* user_data = &lline->userData;
         if (user_data != NULL && user_data->tag != VAR_NUMBER
               && user_data->tag != VAR_STRING && user_data->tag != VAR_FLOAT) {
            abort = abort || set_ref_in_item(user_data, copyID, NULL, NULL);
         } 
      }
   }
   return abort;
}

// Check the location context and callback function if they are in use. For all the lists
// in a location stack.
private int
checkIfContextAndCallbackLocked(LocationStack* stack, int copyID) {
   int abort = FALSE;

   for (int i = 0; i < stack->cap && !abort; ++i) {
      Var* ctx = stack->lists[i].qf_ctx;
      if (ctx != NULL && ctx->tag != VAR_NUMBER
            && ctx->tag != VAR_STRING && ctx->tag != VAR_FLOAT) {
         abort = abort || set_ref_in_item(ctx, copyID, NULL, NULL);
      } 

      Callback* cb = &stack->lists[i].textFn;
      abort = abort || memSetRefInCallback(cb, copyID);
   }

   return abort;
}

private int
markReferencesInStack(LocationStack* st, int copyId) {
   return checkIfContextAndCallbackLocked(st, copyId) || checkIfUserDataLocked(st, copyId);
}

// Mark the context of the quickfix list and the location lists (if present) as "in use". So that 
// garbage collection doesn't free the context.
Boole
set_ref_in_quickfix(int copyId) {
   if (mainStackG == NULL)
      return TRUE;
      
   Boole abort = false;
   for (int i = 0; i < COUNT_LOC_LISTS; i++) {
      abort = abort || markReferencesInStack(locationStacksS + i, copyId);
      if (abort)
         return true;
   }
   return abort || memSetRefInCallback(&locationTextFnS, copyId);
}

// Return the autocmd name for the :lbook commands
private inline Arr(Byte)
getAutocmdNameForCbuffer(CommIndex id) {
   switch (id) {
   case C_lbook:   return S"lbook";
   case C_laddbook:   return S"laddbook";
   default: return NULL;
   }
}

// Process and validate the arguments passed to the :mbook, :maddbook,
// :lbook, :laddbook commands.
private int
processCbookArgs(Invocation* invo, OUT Book** outBook, LineNr* line1, LineNr* line2){
   Book* book = NULL;

   if (*invo->arg == ZERO)
      book = curBook;
   ei (*skipwhite(skipdigits(invo->arg)) == ZERO)
      book = bookFindFileByBookNr(atoi((char *)invo->arg));

   if (book == NULL) {
      emsg(_(e_invalid_argument));
      return FAIL;
   }

   if (book->mem.mfile == NULL) {
      emsg(_(e_buffer_is_not_loaded));
      return FAIL;
   }

   if (invo->addr_count == 0) {
      invo->line1 = 1;
      invo->line2 = book->mem.lineCount;
   }

   if (invo->line1 < 1 || invo->line1 > book->mem.lineCount
       || invo->line2 < 1 || invo->line2 > book->mem.lineCount) {
      emsg(_(e_invalid_range));
      return FAIL;
   }

   *line1 = invo->line1;
   *line2 = invo->line2;
   *outBook = book;

   return OK;
}

// ":[range]lbook [booknr]" command.
// ":[range]laddbook [booknr]" command.
// ":[range]lgetbook [booknr]" command.
void
c_lbook(Invocation* invo) {
   Book* book = NULL;
   LocationStack   *stack;
   int      res;
   Unt   idSave;
   LineNr   line1;
   LineNr   line2;

   CS auName = getAutocmdNameForCbuffer(invo->id);
   if (auName
         && apply_autocmds(EVENT_QUICKFIXCMDPRE, auName, curBook->currFileName, true, curBook)
         && aborting()
   ) {
      return;
   }

   // Must come after autocommands.
   stack = locationStacksS + LOC_LIST_GREP;

   if (processCbookArgs(invo, OUT &book, &line1, &line2) == FAIL)
      return;

   CS title = copyCommandTitle(*invo->commline);

   if (book->shortFileName) {
      eeSnprintf(IObuff, IOSIZE, "%s (%s)", title, book->shortFileName);
      title = IObuff;
   }

   incrementLlBusyness();

   res = initAndUpdateTick(
      (Source){ .tag = SOURCE_BOOK, 
         .Book = (BookSource){.c = book, .start = line1, .end = line2 + 1}
      },
      OUT stack, book->o.errorFormat, (invo->id != C_laddbook), title
   );
   
   if (isStackEmpty(stack)) {
      decrementLlBusyness();
      return;
   }

   // Remember the current location list identifier, so that we can check for autocommands 
   // changing the current list.
   idSave = getCurrent(stack)->id;
   if (auName) {
      Book* curBookSaved = curBook;

      apply_autocmds(EVENT_QUICKFIXCMDPOST, auName, curBook->currFileName, true, curBook);
      if (curBook != curBookSaved)
         // Autocommands changed book, don't jump now, "stack" may be invalid.
         res = 0;
   }
   // Jump to the first error for a new list and if autocmds didn't free the list.
   if (res > 0 && (invo->id == C_lbook) && isIdValid(stack, idSave)) {
      // display the first error
      jumpToFirstEntry(stack, idSave, invo->forceit);
   }

   decrementLlBusyness();
}

// Return the autocmd name for the :lexpr commands.
CS
cexpr_get_auname(CommIndex id) {
   switch (id) {
   case C_lexpr:     return S"lexpr";
   case C_lgetexpr:  return S"lgetexpr";
   case C_laddexpr:  return S"laddexpr";
   default:          return NULL;
   }
}

int
trigger_cexpr_autocmd(int id) {
   CS auName = cexpr_get_auname(id);

   if (auName
         && apply_autocmds(EVENT_QUICKFIXCMDPRE, auName, curBook->currFileName, true, curBook)
   ) {
      if (aborting())
         return FAIL;
   }
   return OK;
}

int
cexpr_core(Invocation* invo, Var *tv) {
   LocationStack* stack = locationStacksS + LOC_LIST_GREP;

   if ((tv->tag == VAR_STRING && tv->string) || (tv->tag == VAR_LIST && tv->list)) {
      CS auName = cexpr_get_auname(invo->id);

      incrementLlBusyness();
      Source source = ((tv->tag == VAR_STRING && tv->string) 
         ? (Source){.tag = SOURCE_STRING, .String = (StringSource){.c = tv->string}}
         : (Source){.tag = SOURCE_LIST, .List = (ListSource){.c = tv->list->first}});
      
      int res = initAndUpdateTick(
         source, OUT stack, curBook->o.errorFormat, (invo->id != C_laddexpr), 
         copyCommandTitle(*invo->commline)
      );
      if (isStackEmpty(stack)) {
         decrementLlBusyness();
         return FAIL;
      }

      // Remember the current location list identifier, so that we can
      // check for autocommands changing the current list.
      Unt idSave = getCurrent(stack)->id;
      if (auName)
          apply_autocmds(EVENT_QUICKFIXCMDPOST, auName, curBook->currFileName, true, curBook);

      // Jump to the first error for a new list and if autocmds didn't free the list.
      if (res > 0 && (invo->id == C_lexpr) && isIdValid(stack, idSave))
         // display the first error
         jumpToFirstEntry(stack, idSave, invo->forceit);
      decrementLlBusyness();
      return OK;
   }

   emsg(_(e_string_or_list_expected));
   return FAIL;
}

//":mexpr {expr}", ":mgetexpr {expr}", ":maddexpr {expr}" command.
//":lexpr {expr}", ":lgetexpr {expr}", ":laddexpr {expr}" command.
//Also: ":maddexpr", ":mgetexpr", "laddexpr" and "laddexpr".
void
c_lExpr(Invocation* invo) {
   if (trigger_cexpr_autocmd(invo->id) == FAIL)
      return;

   // Evaluate the expression.  When the result is a string or a list we can
   // use it to fill the errorlist.
   Var* var = eval_expr(invo->arg, invo);
   if (!var)
      return;

   (void)cexpr_core(invo, var);
   freeVar(var);
}

// Search for a pattern in a help file.
private void
searchInFile(
   LocationList *ll,
   CS fname,
   OUT RegMatch *p_regmatch
){
   FILE* fd = fopen((char *)fname, "r");
   if (!fd)
      return;

   long lnum = 1;
   while (!eeFgets(IObuff, IOSIZE, fd) && !gotInterruptG) {
      CS line = IObuff;
      if (eeRegexec(OUT p_regmatch, line, (ColNr)0)) {
         int   l = (int)STRLEN(line);

         // remove trailing CR, LF, spaces, etc.
         while (l > 0 && line[l - 1] <= ' ')
            line[--l] = ZERO;

         if (addEntry(ll,
                  NULL,   // dir
                  fname,
                  NULL,
                  0,
                  line,
                  lnum,
                  0,
                  (int)(p_regmatch->startp[0] - line)
                  + 1,   // col
                  (int)(p_regmatch->endp[0] - line)
                  + 1,   // end_col
                  FALSE,   // vis_col
                  NULL,   // search pattern
                  0,   // nr
                  1,   // type
                  NULL,   // user_data
                  TRUE   // valid
            ) == QF_FAIL
         ) {
            gotInterruptG = TRUE;
            if (line != IObuff)
               eeglFree(line);
            break;
         }
      }
      if (line != IObuff)
         eeglFree(line);
      ++lnum;
      line_breakcheck();
   }
   fclose(fd);
}

// Search for a pattern in all the help files in the doc directory under the given directory.
private void
searchFilesInDir(LocationList* ll, CS dirname, OUT RegMatch* p_regmatch, CS lang) {
   ExpandMatch files = {};
   files.a = createArena();

   // Find all "*.txt" and "*.??x" files in the "doc" directory.
   add_pathsep(dirname);
   STRCAT(dirname, "doc/*.\\(txt\\|??x\\)");
   if (gen_expand_wildcards(1, &dirname, EW_FILE|EW_SILENT, OUT &files) == OK 
         && files.len > 0
   ) {
      for (Unt fi = 0; fi < files.len && !gotInterruptG; ++fi) {
          // Skip files for a different language.
          if (lang != NULL
                && STRNICMP(lang, files.c[fi] + STRLEN(files.c[fi]) - 3, 2) != 0
                && !(STRNICMP(lang, "en", 2) == 0 
                   && STRNICMP("txt", files.c[fi] + STRLEN(files.c[fi]) - 3, 3) == 0)) {
             continue;
          } 

          searchInFile(ll, files.c[fi], OUT p_regmatch);
      }
   }
   deleteArena(files.a);
}

// ":helpgrep {pattern}"
void
c_helpgrep(Invocation* invo) {
   int updated = FALSE;

   CS auName = S"helpgrep";
   
   if (apply_autocmds(EVENT_QUICKFIXCMDPRE, auName, curBook->currFileName, true, curBook)
         && aborting()) {
      return;
   }

   LocationStack* stack = locationStacksS + LOC_LIST_HELP;

   incrementLlBusyness();

   // Check for a specified language
   CS lang = check_help_lang(invo->arg);
   RegMatch regmatch;
   regmatch.regprog = compileRegexp(invo->arg, RE_MAGIC + RE_STRING);
   regmatch.rm_ic = FALSE;
   if (regmatch.regprog != NULL) {
      LocationList   *ll;

      newLocList(stack, copyCommandTitle(*invo->commline));
      ll = getCurrent(stack);

      searchFilesInDir(ll, (CS)"/usr/share/doc/eegl/", &regmatch, lang);

      eeRegFree(regmatch.regprog);

      ll->noValidEntries = FALSE;
      ll->curr = ll->first;
      ll->currentIdx = 1;
      updateChangedTick(ll);
      updated = TRUE;
   }

   if (updated) // This may open a portal and source scripts
      updateBook(stack, NULL);

   if (auName) {
      apply_autocmds(EVENT_QUICKFIXCMDPOST, auName, curBook->currFileName, true, curBook);
      // When adding a location list to an existing location list stack,
      // if the autocmd made the stack invalid, then just return.
      decrementLlBusyness();
      return;
   }

   // Jump to first match.
   if (!isEmpty(getCurrent(stack)))
      llJump(stack, 0, 0, false);
   else
      showErrFmtMsg(_(e_no_match_str_2), invo->arg);

   decrementLlBusyness();
}

# if defined(EXITFREE) || defined(PROTO)
void
free_quickfix(void) {
   // Free all global location lists
   for (int i = 0; i < COUNT_LOC_LISTS; i++) {
      freeAllLocLists(i);
   }
   ga_clear(&tempList);
}
# endif

// :getloclist m {specifics}
void
f_getloclist(Arr(Var) argvars, OUT Var* returnVar) {
   LocationStack* st = identifyStack(argvars);
   
   Var* specifics = argvars + 1;
   if (specifics == NULL) {
      allocReturnDict(returnVar);
      getProperties(st, NULL, returnVar->bag);
   } ei (specifics->tag == VAR_BAG) {
      Bag* specificss = specifics->bag;
      if (specificss) {
         allocReturnDict(returnVar);
         getProperties(st, specificss, returnVar->bag);
      } else {
         emsg(_(e_dictionary_required));
      }
   } else
      emsg(_(e_dictionary_required));
}

// Set the location stack's current list's contents. Used by "setloclist()" script fn
private void
setLocationListInternal(
   LocationStack* stack,
   Var* listArg,
   Var* actionArg,
   Var* specificArg UNUSED,
   Var* returnVar
){
   static int   recursive = 0;

   returnVar->number = -1;

   if (listArg->tag != VAR_LIST)
      emsg(_(e_list_required));
   ei (recursive != 0)
      emsg(_(e_autocommand_caused_recursive_behavior));
   else {
      List* newContent = listArg->list;
      Bag* specific = NULL;
      Boole isDictValid = TRUE;

      LocListAction action = LL_ACTION_INVALID;
      
      if (actionArg->tag == VAR_STRING) {
         CS act = convertVarToStringSingleUse(actionArg);
         if (act == NULL)
            return;      // type error; errmsg already given
            
         if (act[0] != ZERO && act[1] == ZERO) {
            switch(act[0]){
            case 'a': action = LL_ACTION_ADD; break;
            case 'r': action = LL_ACTION_REPLACE; break;
            case 'u': action = LL_ACTION_UPDATE; break;
            case ' ': action = LL_ACTION_NEW; break;
            case 'f': action = LL_ACTION_FREE; break;
            }
         } 
         if (action == LL_ACTION_INVALID)   
            showErrFmtMsg(_(e_invalid_action_str_1), act);
      } ei (actionArg->tag == VAR_UNKNOWN)
         action = LL_ACTION_NEW;
      else
         emsg(_(e_string_required));

      if (actionArg->tag != VAR_UNKNOWN && specificArg->tag != VAR_UNKNOWN) {
         if (specificArg->tag == VAR_BAG && specificArg->bag)
            specific = specificArg->bag;
         else {
            emsg(_(e_dictionary_required));
            isDictValid = FALSE;
         }
      }

      ++recursive;
      if (newContent 
            && action != LL_ACTION_INVALID && isDictValid
            && setLocationList(stack, newContent, action, (CS)":setloclist()", specific) == OK
      )
         returnVar->number = 0;
      --recursive;
   }
}

void
f_setloclist(Var* argvars, Var* returnVar){
   returnVar->number = -1;
   LocationStack* st= identifyStack(argvars);
   if (st)
      setLocationListInternal(st, &argvars[1], &argvars[2], &argvars[3], returnVar);
}


