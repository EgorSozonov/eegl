//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## do.c: functions for executing Commands. The meat of the IDE.

#include "eegl.h"
#include <float.h>
pub int stat(const char* restrict path, struct stat* restrict buf);
int mkdir(const char* pathname, mode_t mode);

#include "h/data.types.h"
#include "h/data.h"
#include "h/book.h"
#include "h/channel.types.h"
#include "h/channel.h"
#include "h/input.types.h"
#include "h/input.h"
#include "h/diff.h"
#include "h/do.h"
#include "h/draw.types.h"
#include "h/draw.h"
#include "h/hilite.types.h"
#include "h/hilite.h"
#include "h/eval.h"
#include "h/fileio.types.h"
#include "h/fileio.h"
#include "h/location.types.h"
#include "h/location.h"
#include "h/message.h"
#include "h/motor.types.h"
#include "h/motor.h"
#include "h/option.h"
#include "h/portal.h"
#include "h/regexp.h"
#include "h/script.h"
#include "h/strings.h"
#include "h/tag.h"
#include "h/term.h"
#include "h/ui.h"
#include "h/wheel.types.h"
#include "h/wheel.h"
#include "h/window.h"

private Boole anySyntaxEmsgS; //anyEmsgG set because of a syntax error
//{{{types

//Struct to save a few things while debugging.  Used in doCommand() only.
typedef struct {
   int force_abort;
   Exception* caught_stack;
   int anyEmsgG;
   int gotInterruptG;
   int did_throw;
   Boole need_rethrow;
   Exception* current_exception;
} DebugStuff;


//Structure passed around between functions.
typedef struct {
   Book* bk;
   FILE* file;
} BufInfo;


//flags for skipEeglGrepPat()
pub
#define VGR_GLOBAL  1
#define VGR_NOJUMP  2
#define VGR_FUZZY   4

//Structure used to save the current state.  Used when executing Normal mode
//commands while in any other mode.
typedef struct {
   int save_msg_scroll;
   int save_restart_edit;
   int save_msg_didout;
   int save_State;
   int save_finish_op;
   int save_opcount;
   int save_reg_executing;
   int save_pending_end_reg_executing;
   TypeaheadSave   tabuf;
} SaveState;

#define TABSIZE_MAX 16
typedef struct tm Tm; 

//}}}
//{{{@@forward decls
private int linelen(OUT int* has_tab);
private int string_compare(const void *s1, const void *s2);
private int sort_compare(const void *s1, const void *s2);
private void doCopy(LineNr line1, LineNr line2, LineNr n);
private int prevcmd_is_set(void);
private void do_filter(
   LineNr line1,
   LineNr line2,
   Invocation* invo,      //for forced 'ff' and 'fenc'
   CS cmd,
   Boole do_in,
   Boole do_out
);
private void print_line_no_prefix(LineNr lnum, int list);
private void print_line(LineNr lnum, int list);
private int renameBook(CS new_fname);
private int check_writable(CS fname);
private int check_overwrite(
   Invocation* invo,
   Book* book,
   CS fname,       //file name to be used (can differ from book->fullFName)
   CS fullFName,    //full path version of fname
   Boole other)       //writing under other name
;
private Boole isWritingForbidden(void);
private Boole check_readonly(OUT Boole* forceit, Book* book);
private int check_regexp_delim(int c);
private void global_exe_one(CS cmd, LineNr lnum);
private CS skipEeglGrepPat_ext(CS p, Byte **s, Unt* flags, Byte** nulp, int *cp);
private void add_bufnum(int *bufnrs, int *bufnump, int nr);
private void saveDbgStuff(DebugStuff* dsp);
private void restore_DebugStuff(DebugStuff* dsp);
private Boole isSameFile(int fnum, CS fullFName);
private void msg_verbose_cmd(LineNr lnum, CS cmd);
private int do_cmd_argument(CS cmd);
private int compute_buffer_local_count(int addressKind, int lnum, int offset);
private int getPortNr(Portal* portal);
private int current_tab_nr(Tab *tab);
private void doOneCommand(
   OUT CS* commline,
   Unt flags,
   LineGetter fgetline,
   void* cookie      //argument for fgetline()
);
private int checkforcmd_opt(
   OUT CS* pp,      //start of command
   CS cmd,      //name of command
   int len,      //required length
   int noparen
);
private void append_command(CS cmd);
private int oneLetterCommand(CS p, OUT CommIndex *idx);
private void addr_error(CommandAddress addressKind);
private LineNr default_address(Invocation* invo);
private void address_default_all(Invocation* invo);
private void get_flags(Invocation* invo);
private void ex_script_ni(Invocation* invo);
private CS invalid_range(Invocation* invo);
private void correct_range(Invocation* invo);
private CS skip_grep_pat(Invocation* invo);
private CS replaceMakeProgramName(Invocation* invo, OUT CS p, OUT CS* commline);
private CS repl_commline(
   Invocation* invo,
   CS src,
   Unt srclen,
   CS repl,
   OUT CS* commline
);
private CS getargcmd(OUT CS* argp);
private CS get_bad_name(Expand*, int idx);
private int getargopt(Invocation* invo);
private CS get_argoname(Expand*, int idx);
private void do_exbuffer(Invocation* invo);
private int before_quit_autocmds(Portal *po, int quit_all);
private void closePortalInternal(Portal* port, Tab* t);
private int getTabRelatedArg(Invocation* invo);
private List * call_findfunc(CS pat, int cmdcomplete);
private CS findFnFindFile(CS findarg, int findarg_len, int count);
private CS get_prevdir(CdScopeKind scope);
private void mayPrint(Invocation* invo);
private void close_redir(void);
private int save_current_state(SaveState* sst);
private void restore_current_state(SaveState* sst);
private void tagCmd(Invocation* invo, CS name);
private void prepare_preview_window(void);
private void back_to_current_window(Portal *curPor_save);
private void u_check_tree(UndoHeader *uhp, UndoHeader *exp_uh_next, UndoHeader *exp_altPrev);
private void u_check(int newhead_may_be_NULL);
private int u_inssub(LineNr lnum);
private int u_save_line(UndoLine *ul, LineNr lnum);
private Boole has_prop_w_flags(LineNr lnum, int flags);
private void corruption_error(char *mesg, CS file_name);
private void u_free_uhp(UndoHeader *uhp);
private int writeToUndoFile(BufInfo* bi, Arr(Byte) ptr, Unt len);
private int undo_write_bytes(BufInfo* bi, Ulong nr, int len);
private void put_header_ptr(BufInfo *bi, UndoHeader *uhp);
private int undo_read_4c(BufInfo *bi);
private int undo_read_2c(BufInfo *bi);
private int undo_read_byte(BufInfo *bi);
private time_t undo_read_time(BufInfo *bi);
private int undo_read(BufInfo *bi, CS buffer, Unt size);
private CS readStringFromFile(BufInfo *bi, Unt len);
private int serialize_header(BufInfo* bi, Arr(Byte) hash);
private int serialize_uhp(BufInfo* bi, UndoHeader* uhp);
private UndoHeader * unserialize_uhp(BufInfo* bi, CS file_name);
private int serialize_uep(BufInfo* bi, UndoEntry* uep);
private UndoEntry * unserialize_uep(BufInfo *bi, int *error, CS file_name);
private void serialize_pos(BufInfo *bi, Pos pos);
private void deserializePos(BufInfo *bi, Pos *pos);
private void serialize_visualinfo(BufInfo *bi, VisualInfo *info);
private void unserialize_visualinfo(BufInfo *bi, VisualInfo *info);
private void u_doit(int startcount);
private void u_undoredo(Boole undo);
private void u_undo_end(
   Boole did_undo,  //just did an undo
   Boole absolute   //used ":undo N"
);
private void u_unch_branch(UndoHeader* uhp);
private UndoEntry * u_get_headentry(void);
private void u_getbot(void);
private void u_freeheader(
   Book* book,
   UndoHeader* uhp,
   UndoHeader** uhpp)   //if not NULL reset when freeing this header
;
private void freeBranch(
   Book* book,
   UndoHeader* uhp,
   UndoHeader** uhpp   //if not NULL reset when freeing this header
);
private void u_freeentries(
   Book       *book,
   UndoHeader       *uhp,
   UndoHeader       **uhpp)   //if not NULL reset when freeing this header
;
private void freeEntry(UndoEntry *uep, long n);
private void invalidateUndoBuffer(Book *book);
private void u_blockfree(Book* book);
private void u_saveline(LineNr lnum);
private void evalTree(Book* book, UndoHeader* first_uhp, List* list);
private void check_status(Book* book);
private void checkRecordedChanges(
   Book* book,
   LineNr lnum,
   LineNr lnume,
   long xtra
);
private void may_record_change(
    LineNr   lnum,
    ColNr   col,
    LineNr   lnume,
    long   xtra
);
private void remove_listener(Book* book, Listener *lnr, Listener *prev);
private void changed_common(
   LineNr   lnum,
   ColNr   col,
   LineNr   lnume,
   long   xtra
);
private void changedOneline(Book* book, LineNr lnum);
private void insertOrReplaceChar(Unt c, Boole replace);
private Boole op_on_lines(int op);
private Long get_new_sw_indent(
   int      left,      //true if shift is to the left
   int      round,      //true if new indent is to be to a tabstop
   Long   amount,      //Number of shifts
   Long   sw_val)
;
private void shift_block(Operator *oper, int amount);
private void block_insert(
   Operator* oper,
   CS s,
   Unt slen,
   int b_insert,
   OUT BlockDef* bdp)
;
private int getviscol2(ColNr col, ColNr coladd);
private void mb_adjust_opend(Operator *oper);
private void replaceAndMoveBack(Unt c);
private int op_replace(Operator *oper, Unt c);
private void op_tilde(Operator* oper);
private Boole swapchars(Unt opTy, Pos* pos, int length);
private int get_last_leader_offset(CS line, Byte **flags);
private int do_addsub(
   int opTy,
   Pos* pos,
   int length,
   LineNr prenum1
);
private Long line_count_info(
    CS line,
    Long* wc,
    Long* cc,
    Long limit,
    int eol_size
);
private void op_colon(Operator *oper);
private void op_function(Operator* oper);
private void get_op_vcol(Operator* oper, ColNr redo_VIsual_vcol, int initial);
private int isCommandModeChar(ActionArg* aArg);
private void pbyte(Pos lp, int c);
private Tm * eeLocaltime(
   Tyme const* timep,      //timestamp for local representation
   OUT Tm* result //pointer to caller return buffer
);
private int list2proftime(Var *arg, ProfTime *tm);
private void insert_timer(Timer* timer);
private void remove_timer(Timer* timer);
private void free_timer(Timer* timer);
private void timer_callback(Timer *timer);
private Timer * find_timer(long id);
private void stop_all_timers(void);
private void add_timer_info(OUT Var* returnVar, Timer *timer);
private void add_timer_info_all(OUT Var* returnVar);
private void time_diff(TimeVal *then, TimeVal *now);
private double profile_float(ProfTime *tm);
private void set_flag(union sigval);
private void set_flag(union sigval);
private int coladvance2(
   Pos   *pos,
   int      addspaces,   //change the text to achieve our goal?
   int      finetune,   //change char offset for the exact column
   ColNr   wcol_arg   //column to move to (can be negative)
);
private long get_sw_value_pos(Book* book, Pos *pos, int left);
private long get_sw_value_indent(Book* book, int left);
private long get_sw_value_col(Book* book, ColNr, int);
private int get_indent_str(CS ptr, int ts);
private void emsg_text_too_long(void);
private void fixthisline(int (*get_the_indent)(void));
private Boole cin_is_cinword(CS line);
private CS skipStringLiteral(CS p);
//}}}
//{{{sortin' and filterin'

//":ascii" and "ga".
pub void
do_ascii(Invocation*){
   int cval;
   Byte buf1[20];
   Byte buf2[20];
   Byte buf3[7];
   int cc[MAX_COMBINED_SYMBOLS];
   int ci = 0;
   int len;

   int c = utfc_ptr2char(ml_get_cursor(), cc);
   if (c == ZERO) {
      msg((CS)"ZERO");
      return;
   }

   IObuff[0] = ZERO;
   if (c < 0x80) {
      if (c == NL)       //ZERO is stored as NL
         c = ZERO;
      else
         cval = c;
      if (bookIsCharPrintable_strict(c) && (c < ' ' || c > '~')) {
         transchar_nonprint(buf3, c);
         eeSnprintf(buf1, sizeof(buf1), "  <%s>", (char *)buf3);
      } else
         buf1[0] = ZERO;
      if (c >= 0x80) {
         eeSnprintf(buf2, sizeof(buf2), "  <M-%s>", (char *)transchar(c & 0x7f));
      } else {
         buf2[0] = ZERO;
      } 
      eeSnprintf(
         IObuff, IOSIZE, _("<%s>%s%s  %d,  Hex %02x"), transchar(c), buf1, buf2, cval, cval
      );
      c = cc[ci++];
   }

   //Repeat for combining characters.
   while (c >= 0x100) {
      len = (int)STRLEN(IObuff);
      //This assumes every multi-byte char is printable...
      if (len > 0)
         IObuff[len++] = ' ';
      IObuff[len++] = '<';
      if (utf_iscomposing(c))
         IObuff[len++] = ' '; //draw composing char on top of a space
      len += mb_char2bytes(c, IObuff + len);
          eeSnprintf(IObuff + len, IOSIZE - len,
             c < 0x10000 ? _("> %d, Hex %04x")
                    : _("> %d, Hex %08x"),
                    c, c);
      if (ci == MAX_COMBINED_SYMBOLS)
         break;
      c = cc[ci++];
   }

   msg(IObuff);
}

//":left", ":center" and ":right": align text.
pub void
c_align(Invocation* invo) {
   int      len;
   int      indent = 0;
   int width = atoi((char *)invo->arg);
   Pos save_curpos = curPor->cursor;
   if (invo->id == C_left) {   //width is used for new indent
      if (width >= 0)
         indent = width;
   } else {
   //if 'textwidth' set, use it
   //ei 'wrapmargin' set, use it
   //if invalid value, use 80
   if (width <= 0)
       width = curBook->o.textWidth;
   if (width == 0 && curBook->o.wrapMargin > 0)
       width = curPor->width - curBook->o.wrapMargin;
   if (width <= 0)
       width = 80;
   }

   if (u_save((LineNr)(invo->line1 - 1), (LineNr)(invo->line2 + 1)) == FAIL)
      return;

   int new_indent;
   for (curPor->cursor.lnum = invo->line1; curPor->cursor.lnum <= invo->line2; 
         ++curPor->cursor.lnum
   ) {
      if (invo->id == C_left)      //left align
         new_indent = indent;
      else {
         int has_tab = false;   //avoid uninit warnings
         len = linelen(invo->id == C_right ? &has_tab : NULL) - get_indent();

         if (len <= 0)         //skip blank lines
            continue;

         if (invo->id == C_center)
            new_indent = (width - len) / 2;
         else {
            new_indent = width - len;   //right align

            //Make sure that embedded TABs don't make the text go too far to the right.
            if (has_tab) {
               while (new_indent > 0) {
                  (void)set_indent(new_indent, 0);
                  if (linelen(NULL) <= width) {
                     //Now try to move the line as much as possible to the right. Stop when it moves too far.
                     do
                        (void)set_indent(++new_indent, 0);
                     while (linelen(NULL) <= width);
                     --new_indent;
                     break;
                  }
                  --new_indent;
               }
            } 
         }
      }
      if (new_indent < 0)
         new_indent = 0;
      (void)set_indent(new_indent, 0);      //set indent
   }
   doChangedLines(invo->line1, 0, invo->line2 + 1, 0L);
   curPor->cursor = save_curpos;
   beginline(BL_WHITE | BL_FIX);
}

//Get the length of the current line, excluding trailing white space.
private int
linelen(OUT int* has_tab) {
   //Get the line.  If it's empty bail out early (could be the empty string for an unloaded book)
   CS line = ml_get_curline();
   if (*line == ZERO)
      return 0;

   //find the first non-blank character
   CS first = skipwhite(line);

   //find the character after the last non-blank character
   CS last;
   for (last = first + STRLEN(first); last > first && SPACE_OR_TAB(last[-1]); --last)
      {}
   int save = *last;
   *last = ZERO;
   int len = linetabsize_str(line);   //get line length on screen
   if (has_tab)      //check for embedded TAB
      *has_tab = (firstOccurrence(first, TAB) != NULL);
   *last = save;

   return len;
}

//Book for two lines used during sorting.  They are allocated to
//contain the longest line being sorted.
private CS sortbuf1;
private CS sortbuf2;

private int   sort_lc;   //sort using locale
private int   sort_ic;   //ignore case
private int   sort_nr;   //sort on number
private int   sort_rx;   //sort on regex instead of skipping it
private int   sort_flt;   //sort on floating number

private int   sort_abort;   //flag to indicate if sorting has been interrupted

//Struct to store info to be sorted.
typedef struct {
   LineNr   lnum;         //line number
   union {
      struct {
         Long   start_col_nr;   //starting column number
         Long   end_col_nr;   //ending column number
      } line;
      struct {
         Long   value;      //value if sorting by integer
         int is_number;      //true when line contains a number
      } num;
      double value_flt;      //value if sorting by float
   } st_u;
} SortingInfo;

private int
string_compare(const void *s1, const void *s2) {
   if (sort_lc)
      return strcoll((char *)s1, (char *)s2);
   return sort_ic ? caseInsensitiveCompare(s1, s2) : STRCMP(s1, s2);
}

private int
sort_compare(const void *s1, const void *s2) {
    SortingInfo   l1 = *(SortingInfo *)s1;
    SortingInfo   l2 = *(SortingInfo *)s2;
    int      result = 0;

    //If the user interrupts, there's no way to stop qsort() immediately, but
    //if we return 0 every time, qsort will assume it's done sorting and
    //exit.
   if (sort_abort)
   return 0;
    fast_breakcheck();
   if (gotInterruptG)
   sort_abort = true;

   if (sort_nr) {
   if (l1.st_u.num.is_number != l2.st_u.num.is_number)
       result = l1.st_u.num.is_number > l2.st_u.num.is_number ? 1 : -1;
   else
       result = l1.st_u.num.value == l2.st_u.num.value ? 0
              : l1.st_u.num.value > l2.st_u.num.value ? 1 : -1;
    } ei (sort_flt)
   result = l1.st_u.value_flt == l2.st_u.value_flt ? 0
              : l1.st_u.value_flt > l2.st_u.value_flt ? 1 : -1;
    else {
   //We need to copy one line into "sortbuf1", because there is no
   //guarantee that the first pointer becomes invalid when obtaining the
   //second one.
   STRNCPY(sortbuf1, ml_get(l1.lnum) + l1.st_u.line.start_col_nr,
           l1.st_u.line.end_col_nr - l1.st_u.line.start_col_nr + 1);
   sortbuf1[l1.st_u.line.end_col_nr - l1.st_u.line.start_col_nr] = 0;
   STRNCPY(sortbuf2, ml_get(l2.lnum) + l2.st_u.line.start_col_nr,
           l2.st_u.line.end_col_nr - l2.st_u.line.start_col_nr + 1);
   sortbuf2[l2.st_u.line.end_col_nr - l2.st_u.line.start_col_nr] = 0;

   result = string_compare(sortbuf1, sortbuf2);
    }

    //If two lines have the same value, preserve the original line order.
   if (result == 0)
   return (int)(l1.lnum - l2.lnum);
    return result;
}

//":sort".
pub void
c_sort(Invocation* invo) {
   RegMatch   regmatch;
   int      len;
   LineNr   lnum;
   long   maxlen = 0;
   SortingInfo   *nrs;
   Unt   count = (Unt)(invo->line2 - invo->line1 + 1);
   Unt   i;
   CS p;
   CS s;
   CS s2;
   Byte c;         //temporary character storage
   int unique = false;
   long deleted;
   ColNr start_col;
   ColNr end_col;
   int sort_what = 0;
   int format_found = 0;
   int change_occurred = false; //Book contents changed.

   //Sorting one line is really quick!
   if (count <= 1)
      return;

   if (u_save((LineNr)(invo->line1 - 1), (LineNr)(invo->line2 + 1)) == FAIL)
      return;
   sortbuf1 = NULL;
   sortbuf2 = NULL;
   regmatch.regprog = NULL;
   nrs = ALLOC_MULT(SortingInfo, count);

   sort_abort = sort_ic = sort_lc = sort_rx = sort_nr = 0;
   sort_flt = 0;

   for (p = invo->arg; *p != ZERO; ++p) {
      if (SPACE_OR_TAB(*p))
          ;
      ei (*p == 'i')
          sort_ic = true;
      ei (*p == 'l')
          sort_lc = true;
      ei (*p == 'r')
          sort_rx = true;
      ei (*p == 'n') {
          sort_nr = 1;
          ++format_found;
      } ei (*p == 'f') {
          sort_flt = 1;
          ++format_found;
      } ei (*p == 'b') {
          sort_what = STR2NR_BIN + STR2NR_FORCE;
          ++format_found;
      } ei (*p == 'x') {
          sort_what = STR2NR_HEX + STR2NR_FORCE;
          ++format_found;
      }
      ei (*p == 'u')
          unique = true;
      ei (isComment(p))
          break;
      ei (!ASCII_ISALPHA(*p) && regmatch.regprog == NULL) {
         s = skip_regexp_err(p + 1, *p, true);
         if (s == NULL)
            goto sortend;
         *s = ZERO;
         //Use last search pattern if sort pattern is empty.
         if (s == p + 1) {
            if (last_search_pat().len == 0) {
                emsg(_(e_no_previous_regular_expression));
                goto sortend;
            }
            regmatch.regprog = compileRegexp(last_search_pat().c, RE_MAGIC);
          } else
            regmatch.regprog = compileRegexp(p + 1, RE_MAGIC);
         if (regmatch.regprog == NULL)
            goto sortend;
         p = s;      //continue after the regexp
         regmatch.rm_ic = p_ic;
      } else {
         showErrFmtMsg(_(e_invalid_argument_str), p);
         goto sortend;
      }
    }

    //Can only have one of 'n', 'b', 'o' and 'x'.
   if (format_found > 1) {
      emsg(_(e_invalid_argument));
      goto sortend;
   }

   //From here on "sort_nr" is used as a flag for any integer number sorting.
   sort_nr += sort_what;

   //Make an array with all line numbers.  This avoids having to copy all
   //the lines into allocated memory.
   //When sorting on strings "start_col_nr" is the offset in the line, for
   //numbers sorting it's the number to sort on.  This means the pattern
   //matching and number conversion only has to be done once per line.
   //Also get the longest line length for allocating "sortbuf".
   for (lnum = invo->line1; lnum <= invo->line2; ++lnum) {
      s = ml_get(lnum);
      len = ml_get_len(lnum);
      if (maxlen < len)
         maxlen = len;

      start_col = 0;
      end_col = len;
      if (regmatch.regprog && eeRegexec(&regmatch, s, 0)) {
         if (sort_rx) {
            start_col = (ColNr)(regmatch.startp[0] - s);
            end_col = (ColNr)(regmatch.endp[0] - s);
         } else
            start_col = (ColNr)(regmatch.endp[0] - s);
      } else
         if (regmatch.regprog != NULL)
            end_col = 0;

      if (sort_nr || sort_flt) {
         //Make sure readLongNumber() doesn't read any digits past the end
         //of the match, by temporarily terminating the string there
         s2 = s + end_col;
         c = *s2;
         *s2 = ZERO;
         //Sorting on number: Store the number itself.
         p = s + start_col;
         if (sort_nr) {
            if (sort_what & STR2NR_HEX)
                s = skiptohex(p);
            ei (sort_what & STR2NR_BIN)
                s = skiptobin(p);
            else
                s = skiptodigit(p);
            if (s > p && s[-1] == '-')
                --s;  //include preceding negative sign
            if (*s == ZERO) {
                //line without number should sort before any number
                nrs[lnum - invo->line1].st_u.num.is_number = false;
                nrs[lnum - invo->line1].st_u.num.value = 0;
            } else {
               nrs[lnum - invo->line1].st_u.num.is_number = true;
               readLongNumber(
                  s, NULL, NULL, sort_what,
                  &nrs[lnum - invo->line1].st_u.num.value,
                  NULL, 0, false, NULL
               );
            }
         } else {
         s = skipwhite(p);
         if (*s == '+')
             s = skipwhite(s + 1);

         if (*s == ZERO)
            //empty line should sort before any number
            nrs[lnum - invo->line1].st_u.value_flt = -DBL_MAX;
         else
            nrs[lnum - invo->line1].st_u.value_flt = strtod((char *)s, NULL);
         }
         *s2 = c;
      } else {
          //Store the column to sort at.
          nrs[lnum - invo->line1].st_u.line.start_col_nr = start_col;
          nrs[lnum - invo->line1].st_u.line.end_col_nr = end_col;
      }

      nrs[lnum - invo->line1].lnum = lnum;

      if (regmatch.regprog)
          fast_breakcheck();
      if (gotInterruptG)
          goto sortend;
    }

   //Allocate a buffer that can hold the longest line.
   sortbuf1 = alloc(maxlen + 1);
   sortbuf2 = alloc(maxlen + 1);

   //Sort the array of line numbers.  Note: can't be interrupted!
   qsort((void *)nrs, count, sizeof(SortingInfo), sort_compare);

   if (sort_abort)
      goto sortend;

   //Insert the lines in the sorted order below the last one.
   lnum = invo->line2;
   for (i = 0; i < count; ++i) {
      LineNr get_lnum = nrs[invo->forceit ? count - i - 1 : i].lnum;

      //If the original line number of the line being placed is not the same
      //as "lnum" (accounting for offset), we know that the buffer changed.
      if (get_lnum + ((LineNr)count - 1) != lnum)
          change_occurred = true;

      s = ml_get(get_lnum);
      if (!unique || i == 0 || string_compare(s, sortbuf1) != 0) {
          //Copy the line into a buffer, it may become invalid in
          //ml_append(). And it's needed for "unique".
          STRCPY(sortbuf1, s);
          if (ml_append(lnum++, sortbuf1, (ColNr)0, false) == FAIL)
         break;
      }
      fast_breakcheck();
      if (gotInterruptG)
          goto sortend;
    }

    //delete the original lines if appending worked
   if (i == count) {
      for (i = 0; i < count; ++i)
          ml_delete(invo->line1);
   } else
      count = 0;

   //Adjust marks for deleted (or added) lines and prepare for displaying.
   deleted = (long)(count - (lnum - invo->line2));
   if (deleted > 0) {
      markAdjust(invo->line2 - deleted, invo->line2, (long)MAXLNUM, -deleted, true);
      msgmore(-deleted);
   } ei (deleted < 0)
      markAdjust(invo->line2, MAXLNUM, -deleted, 0L, true);

   if (change_occurred || deleted != 0)
      doChangedLines(invo->line1, 0, invo->line2 + 1, -deleted);

   curPor->cursor.lnum = invo->line1;
   beginline(BL_WHITE | BL_FIX);

sortend:
   eeglFree(nrs);
   eeglFree(sortbuf1);
   eeglFree(sortbuf2);
   eeRegFree(regmatch.regprog);
   if (gotInterruptG)
      emsg(_(e_interrupted));
}

//":uniq".
pub void
c_uniq(Invocation* invo) {
   RegMatch   regmatch;
   int      len;
   LineNr   lnum;
   long   maxlen = 0;
   LineNr   count = invo->line2 - invo->line1 + 1;
   CS p;
   CS s;
   Byte save_c = 0;      //temporary character storage
   int keep_only_unique = false;
   int keep_only_not_unique = invo->forceit ? true : false;
   long deleted = 0;
   ColNr start_col;
   ColNr end_col;
   int change_occurred = false; //Book contents changed.

   //Uniq one line is really quick!
   if (count <= 1)
      return;

   if (u_save((LineNr)(invo->line1 - 1), (LineNr)(invo->line2 + 1)) == FAIL)
      return;
   sortbuf1 = NULL;
   regmatch.regprog = NULL;

   sort_abort = sort_ic = sort_lc = sort_rx = sort_nr = 0;
   sort_flt = 0;

   for (p = invo->arg; *p != ZERO; ++p) {
      if (SPACE_OR_TAB(*p))
          ;
      ei (*p == 'i')
          sort_ic = true;
      ei (*p == 'l')
          sort_lc = true;
      ei (*p == 'r')
          sort_rx = true;
      ei (*p == 'u') {
         //'u' is only valid when '!' is not given.
         if (!keep_only_not_unique)
            keep_only_unique = true;
      } ei (isComment(p))   //comment start
         break;
      ei (!ASCII_ISALPHA(*p) && regmatch.regprog == NULL) {
         s = skip_regexp_err(p + 1, *p, true);
         if (s == NULL)
            goto uniqend;
         *s = ZERO;
         //Use last search pattern if uniq pattern is empty.
         if (s == p + 1) {
            if (last_search_pat().len == 0) {
                emsg(_(e_no_previous_regular_expression));
                goto uniqend;
            }
            regmatch.regprog = compileRegexp(last_search_pat().c, RE_MAGIC);
         } else
            regmatch.regprog = compileRegexp(p + 1, RE_MAGIC);
         if (regmatch.regprog == NULL)
            goto uniqend;
         p = s;      //continue after the regexp
         regmatch.rm_ic = p_ic;
      } else {
          showErrFmtMsg(_(e_invalid_argument_str), p);
          goto uniqend;
      }
   }

   //Find the length of the longest line.
   for (lnum = invo->line1; lnum <= invo->line2; ++lnum) {
      len = ml_get_len(lnum);
      if (maxlen < len)
         maxlen = len;

      if (gotInterruptG)
         goto uniqend;
   }

   //Allocate a buffer that can hold the longest line.
   sortbuf1 = alloc(maxlen + 1);

   //Delete lines according to options.
   int match_continue = false;
   int next_is_unmatch = false;
   int is_match;
   LineNr done_lnum = invo->line1 - 1;
   LineNr delete_lnum = 0;
   for (LineNr i = 0; i < count; ++i) {
      LineNr get_lnum = invo->line1 + i;

      s = ml_get(get_lnum);
      len = ml_get_len(get_lnum);

      start_col = 0;
      end_col = len;
      if (regmatch.regprog && eeRegexec(&regmatch, s, 0)) {
          if (sort_rx) {
         start_col = (ColNr)(regmatch.startp[0] - s);
         end_col = (ColNr)(regmatch.endp[0] - s);
          }
          else
         start_col = (ColNr)(regmatch.endp[0] - s);
      } ei(regmatch.regprog)
         end_col = 0;
      if (end_col > 0) {
         save_c = s[end_col];
         s[end_col] = ZERO;
      }

      is_match = i > 0 ? !string_compare(&s[start_col], sortbuf1) : false;
      delete_lnum = 0;
      if (next_is_unmatch) {
         is_match = false;
         next_is_unmatch = false;
      }

      if (!keep_only_unique && !keep_only_not_unique) {
          if (is_match)
         delete_lnum = get_lnum;
          else
         STRCPY(sortbuf1, &s[start_col]);
      } ei (keep_only_not_unique) {
          if (is_match) {
         done_lnum = get_lnum - 1;
         delete_lnum = get_lnum;
         match_continue = true;
          } else {
         if (i > 0 && !match_continue && get_lnum - 1 > done_lnum) {
             delete_lnum = get_lnum - 1;
             next_is_unmatch = true;
         }
         ei (i >= count - 1)
             delete_lnum = get_lnum;
         match_continue = false;
         STRCPY(sortbuf1, &s[start_col]);
          }
      } else //keep_only_unique
      {
          if (is_match) {
         if (!match_continue)
             delete_lnum = get_lnum - 1;
         else
             delete_lnum = get_lnum;
         match_continue = true;
          } else {
         if (i == 0 && match_continue)
             delete_lnum = get_lnum;
         match_continue = false;
         STRCPY(sortbuf1, &s[start_col]);
          }
      }

      if (end_col > 0)
          s[end_col] = save_c;

      if (delete_lnum > 0) {
          ml_delete(delete_lnum);
          i -= get_lnum - delete_lnum + 1;
          count--;
          deleted++;
          change_occurred = true;
      }

      fast_breakcheck();
      if (gotInterruptG)
          goto uniqend;
   }

   //Adjust marks for deleted lines and prepare for displaying.
   markAdjust(invo->line2 - deleted, invo->line2, (long)MAXLNUM, -deleted, true);
   msgmore(-deleted);

   if (change_occurred)
      doChangedLines(invo->line1, 0, invo->line2 + 1, -deleted);

   curPor->cursor.lnum = invo->line1;
   beginline(BL_WHITE | BL_FIX);

uniqend:
   eeglFree(sortbuf1);
   eeRegFree(regmatch.regprog);
   if (gotInterruptG)
      emsg(_(e_interrupted));
}

//:move command - move lines line1-line2 to line dest
//return FAIL for failure, OK otherwise
pub int
do_move(LineNr line1, LineNr line2, LineNr dest) {
   CS str;
   LineNr l;
   LineNr extra;       //Num lines added before line1
   LineNr num_lines;  //Num lines moved
   LineNr last_line;  //Last line in file after adding new text
   Tab* t;

   if (dest >= line1 && dest < line2) {
      emsg(_(e_cannot_move_range_of_lines_into_itself));
      return FAIL;
   }

   //Do nothing if we are not actually moving any lines.  This will prevent
   //the 'modified' flag from being set without cause.
   if (dest == line1 - 1 || dest == line2) {
      //Move the cursor as if lines were moved (see below) to be backwards
      //compatible.
      if (dest >= line1)
         curPor->cursor.lnum = dest;
      else
         curPor->cursor.lnum = dest + (line2 - line1) + 1;

      return OK;
   }

    num_lines = line2 - line1 + 1;

    /*
     * First we copy the old text to its new location -- webb
     * Also copy the flag that ":global" command uses.
     */
   if (u_save(dest, dest + 1) == FAIL)
   return FAIL;
   for (extra = 0, l = line1; l <= line2; l++) {
      str = copySubstr(ml_get(l + extra), ml_get_len(l + extra));
      if (str) {
         ml_append(dest + l - line1, str, (ColNr)0, false);
         eeglFree(str);
         if (dest < line1)
            extra++;
      }
   }

   //Now we must be careful adjusting our marks so that we don't overlap our markAdjust() calls.
   //
   //We adjust the marks within the old text so that they refer to the
   //last lines of the file (temporarily), because we know no other marks
   //will be set there since these line numbers did not exist until we added our new lines.
   //
   //Then we adjust the marks on lines between the old and new text positions
   //(either forwards or backwards).
   //
   //And Finally we adjust the marks we put at the end of the file back to
   //their final destination at the new text position -- webb
   last_line = curBook->mem.lineCount;
   markAdjust(line1, line2, last_line - line2, 0L, false);
   Portal* po;
   if (dest >= line2) {
      markAdjust(line2 + 1, dest, -num_lines, 0L, false);
      FOR_ALL_TAB_PORTALS(t, po) {
         if (po->book == curBook)
            foldMoveRange(&po->folds, line1, line2, dest);
      }
      if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
          curBook->opStart.lnum = dest - num_lines + 1;
          curBook->opEnd.lnum = dest;
      }
   } else {
      markAdjust(dest + 1, line1 - 1, num_lines, 0L, false);
      FOR_ALL_TAB_PORTALS(t, po) {
         if (po->book == curBook)
            foldMoveRange(&po->folds, dest + 1, line1 - 1, line2);
      }
      if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
          curBook->opStart.lnum = dest + 1;
          curBook->opEnd.lnum = dest + num_lines;
      }
   }
   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0)
      curBook->opStart.col = curBook->opEnd.col = 0;
   markAdjust(last_line - num_lines + 1, last_line, -(last_line - dest - extra), 0L, false);

   //Now we delete the original text -- webb
   if (u_save(line1 + extra - 1, line2 + extra + 1) == FAIL)
      return FAIL;

   for (l = line1; l <= line2; l++)
      ml_delete_flags(line1 + extra, ML_DEL_MESSAGE);

   if (!global_busy) {
      smsg(NGETTEXT("%ld line moved", "%ld lines moved", num_lines),
            (long)num_lines);
   } 

   //Leave the cursor on the last of the moved lines.
   if (dest >= line1)
      curPor->cursor.lnum = dest;
   else
      curPor->cursor.lnum = dest + (line2 - line1) + 1;

   if (line1 < dest) {
      dest += num_lines + 1;
      last_line = curBook->mem.lineCount;
      if (dest > last_line + 1)
          dest = last_line + 1;
      doChangedLines(line1, 0, dest, 0L);
   } else
      doChangedLines(dest + 1, 0, line1 + num_lines, 0L);

   return OK;
}

//":copy"
private void
doCopy(LineNr line1, LineNr line2, LineNr n) {
   CS p;

   LineNr count = line2 - line1 + 1;
   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
      curBook->opStart.lnum = n + 1;
      curBook->opEnd.lnum = n + count;
      curBook->opStart.col = curBook->opEnd.col = 0;
   }

   //there are three situations:
   //1. destination is above line1
   //2. destination is between line1 and line2
   //3. destination is below line2
   //
   //n = destination (when starting)
   //curPor->cursor.lnum = destination (while copying)
   //line1 = start of source (while copying)
   //line2 = end of source (while copying)
   if (u_save(n, n + 1) == FAIL)
      return;

   curPor->cursor.lnum = n;
   while (line1 <= line2) {
      //need to make a copy because the line will be unlocked within ml_append()
      p = copySubstr(ml_get(line1), ml_get_len(line1));
      if (p) {
         ml_append(curPor->cursor.lnum, p, (ColNr)0, false);
         eeglFree(p);
      }
      //situation 2: skip already copied lines
      if (line1 == n)
          line1 = curPor->cursor.lnum;
      ++line1;
      if (curPor->cursor.lnum < line1)
          ++line1;
      if (curPor->cursor.lnum < line2)
          ++line2;
      ++curPor->cursor.lnum;
    }

    appended_lines_mark(n, count);
   if (VIsual_active)
   check_pos(curBook, &VIsual);

    msgmore((long)count);
}

private CS prevcmd = NULL;   //the previous command

#if defined(EXITFREE)
pub void
free_prev_shellcmd(void) {
    eeglFree(prevcmd);
}
#endif

//Check that "prevcmd" is not NULL. If it is NULL, then give an error message and return false.
private int
prevcmd_is_set(void) {
   if (prevcmd == NULL) {
   emsg(_(e_no_previous_command));
   return false;
    }
    return true;
}

//Handle the ":!cmd" command.   Also for ":r !cmd" and ":w !cmd"
//Bangs in the argument are replaced with the previously entered command.
//Remember the argument.
pub void
do_bang(
   int addr_count,
   Invocation* invo,
   Boole forceit,
   Boole do_in,
   Boole do_out
) {
   CS arg = invo->arg;   //command
   LineNr line1 = invo->line1;   //start of range
   LineNr line2 = invo->line2;   //end of range
   CS newcmd = NULL;
   Boole free_newcmd = false;    //need to free() newcmd
   CS t;
   CS p;
   int scroll_save = msg_scroll;

   if (addr_count == 0) {     //:!
      msg_scroll = false;       //don't scroll here
      doFlushAllBooks();
      msg_scroll = scroll_save;
   }

   //Try to find an embedded bang, like in ":!<cmd> ! [args]"
   //":!!" is indicated by the 'forceit' variable.
   int ins_prevcmd = forceit;

   //Skip leading white space to avoid a strange error with some shells.
   CS trailarg = skipwhite(arg);
   do {
      int len = (int)STRLEN(trailarg) + 1;
      if (newcmd)
         len += (int)STRLEN(newcmd);
      if (ins_prevcmd) {
         if (!prevcmd_is_set()) {
            eeglFree(newcmd);
            return;
         }
         len += (int)STRLEN(prevcmd);
      }
      t = alloc(len);
      *t = ZERO;
      if (newcmd)
         STRCAT(t, newcmd);
      if (ins_prevcmd)
         STRCAT(t, prevcmd);
      p = t + STRLEN(t);
      STRCAT(t, trailarg);
      eeglFree(newcmd);
      newcmd = t;

      //Scan the rest of the argument for '!', which is replaced by the
      //previous command.  "\!" is replaced by "!"
      trailarg = NULL;
      while (*p) {
         if (*p == '!') {
            if (p > newcmd && p[-1] == '\\')
               STRMOVE(p - 1, p);
            else {
               trailarg = p;
               *trailarg++ = ZERO;
               ins_prevcmd = true;
               break;
            }
         }
         ++p;
      }
   } while (trailarg);

   //Only set "prevcmd" if there is a command to run, otherwise keep te one we have.
   if (STRLEN(newcmd) > 0) {
      eeglFree(prevcmd);
      prevcmd = newcmd;
    } else
      free_newcmd = true;

   if (bangredo) {     //put cmd in redo buffer for ! command
      if (!prevcmd_is_set())
         goto theend;

      //If % or # appears in the command, it must have been escaped.
      //Reescape them, so that redoing them does not substitute them by the buffername.
      CS cmd = copyStr_escaped(prevcmd, S"%#");

      inpAppendLitToRedoBuff(cmd, -1);
      eeglFree(cmd);
      inpAppendToRedoBuff((CS)"\n");
      bangredo = false;
   }
   if (addr_count == 0) {     //:!
      //echo the command
      msg_start();
      msg_putchar(':');
      msg_putchar('!');
      msg_outtrans(newcmd);
      msg_clr_eos();
      windgoto(msgRowG, msgColG);

      do_shell(newcmd, 0);
   } else { //:range!
      //Careful: This may recursively call do_bang() again! (because of autocommands)
      do_filter(line1, line2, invo, newcmd, do_in, do_out);
      applyAutocomms(EVENT_SHELLFILTERPOST, NULL, NULL, false, curBook);
   }

theend:
   if (free_newcmd)
      eeglFree(newcmd);
}

//do_filter: filter lines through a command given by the user
//
//We mostly use temp files and the chCallShell() function here. This would normally be done using 
//pipes on a Unix machine, but this is more portable to non-unix machines. The chCallShell() 
//fn needs to be able to deal with redirection somehow, and should handle things like looking
//at the PATH env. variable, and adding reasonable extensions to the command name given by the 
//user. All reasonable versions of chCallShell() do this. Alternatively, if on Unix and redirecting 
//input or output, but not both, and the @shelltemp option isn't set, use pipes.
//We use input redirection if do_in is true. We use output redirection if do_out is true.
private void
do_filter(
   LineNr line1,
   LineNr line2,
   Invocation* invo,      //for forced 'ff' and 'fenc'
   CS cmd,
   Boole do_in,
   Boole do_out
) {
   CS itmp = NULL;
   CS otmp = NULL;
   Book* curBookSaved = curBook;
   Unt shell_flags = 0;
   Pos orig_start = curBook->opStart;
   Pos orig_end = curBook->opEnd;
   int save_cmod_flags = commModifierG.cmod_flags;

   if (!cmd || cmd[0] == ZERO)       //no filter command
      return;

   //Temporarily disable lockmarks since that's needed to propagate changed
   //regions of the book for foldUpdate(), linecount, etc.
   commModifierG.cmod_flags &= ~CMOD_LOCKMARKS;

   Pos cursor_save = curPor->cursor;
   LineNr linecount = line2 - line1 + 1;
   curPor->cursor.lnum = line1;
   curPor->cursor.col = 0;
   changed_line_abv_curs();
   invalidate_botline();

   //When using temp files:
   //1. * Form temp file names
   //2. * Write the lines to a temp file
   //3.   Run the filter command on the temp file
   //4. * Read the output of the command into the buffer
   //5. * Delete the original lines to be filtered
   //6. * Remove the temp files
   //
   //When writing the input with a pipe or when catching the output with a pipe, only need to do 3

   if (do_out)
      shell_flags |= SHELL_DOOUT;

   if (!do_in && do_out) {
      //Use a pipe to fetch stdout of the command, do not use a temp file.
      shell_flags |= SHELL_READ;
      curPor->cursor.lnum = line2;
   } ei (do_in && !do_out) {
      //Use a pipe to write stdin of the command, do not use a temp file.
      shell_flags |= SHELL_WRITE;
      curBook->opStart.lnum = line1;
      curBook->opEnd.lnum = line2;
   } ei (do_in && do_out) {
      //Use a pipe to write stdin and fetch stdout of the command, do not use a temp file.
      shell_flags |= SHELL_READ|SHELL_WRITE;
      curBook->opStart.lnum = line1;
      curBook->opEnd.lnum = line2;
      curPor->cursor.lnum = line2;
   } else if ((do_in && (itmp = eeTempName('i', false)) == NULL)
      || (do_out && (otmp = eeTempName('o', false)) == NULL)
   ) {
      emsg(_(e_cant_get_temp_file_name));
      goto filterend;
   }

   //The writing and reading of temp files will not be shown.
   ++no_wait_return;      //don't call wait_return() while busy
   if (itmp && bookWrite(curBook, itmp, NULL, line1, line2, invo,
                  false, false, false, true) != OK
   ) {
      msg_putchar('\n');      //keep message from bookWrite()
      --no_wait_return;
      if (!aborting())
         //will call wait_return()
         (void)showErrFmtMsg(_(e_cant_create_file_str), itmp);
      goto filterend;
   }
   if (curBook != curBookSaved)
      goto filterend;

   if (!do_out)
      msg_putchar('\n');

   //Create the shell command in allocated memory.
   CS cmd_buf = make_filter_cmd(cmd, itmp, otmp);
   if (!cmd_buf)
      goto filterend;

   windgoto((int)visibleRowsG - 1, 0);
   cursor_on();

   //When not redirecting the output the command can write anything to the screen. Clear the 
   //screen later. If do_in is false, this could be something like ":r !cat", which may
   //also mess up the screen, clear it later.
   if (!do_out || !do_in)
      redraw_later_clear();

   if (do_out) {
      if (u_save(line2, (LineNr)(line2 + 1)) == FAIL) {
         eeglFree(cmd_buf);
         goto error;
      }
      drawCurBookLater(UPD_VALID);
   }
   LineNr read_linecount = curBook->mem.lineCount;

   //When chCallShell() fails wait_return() is called to give the user a
   //chance to read the error messages. Otherwise errors are ignored, so you
   //can see the error messages from the command that appear on stdout; use 'u' to fix the text
   //Switch to cooked mode when not redirecting stdin, avoids that something like ":r !cat" hangs.
   //Pass on the SHELL_DOOUT flag when the output is being redirected.
   if (chCallShell(text(cmd_buf), SHELL_FILTER | SHELL_COOKED | shell_flags).status != 0) {
      redraw_later_clear();
      wait_return(false);
   }
   eeglFree(cmd_buf);

   did_check_timestamps = false;
   need_check_timestamps = true;

   //When interrupting the shell command, it may still have produced some
   //useful output. Reset gotInterruptG here, so that readfile() won't cancel reading.
   ui_breakcheck();
   gotInterruptG = false;

   if (do_out) {
      if (otmp) {
         if (readfile(otmp, NULL, line2, (LineNr)0, (LineNr)MAXLNUM, invo, READ_FILTER) != OK) {
            if (!aborting()) {
               msg_putchar('\n');
               showErrFmtMsg(_(e_cant_read_file_str), otmp);
            }
            goto error;
         }
         if (curBook != curBookSaved)
            goto filterend;
      }

      read_linecount = curBook->mem.lineCount - read_linecount;

      if ((shell_flags & SHELL_READ) != 0) {
         curBook->opStart.lnum = line2 + 1;
         curBook->opEnd.lnum = curPor->cursor.lnum;
         appended_lines_mark(line2, read_linecount);
      }

      if (do_in) {
         if (read_linecount >= linecount)
            //move all marks from old lines to new lines
            markAdjust(line1, line2, linecount, 0L, true);
         ei (save_cmod_flags & CMOD_LOCKMARKS) {
            //Move marks from the lines below the new lines down by the number of lines lost.
            //Move marks from the lines that will be deleted to the new lines and below.
            markAdjust(line2 + 1, (LineNr)MAXLNUM, linecount - read_linecount, 0L, true);
            markAdjust(line1, line2, linecount, 0L, true);
         } else {
            //move marks from old lines to new lines, delete marks that are in deleted lines
            markAdjust(line1, line1 + read_linecount - 1, linecount, 0L, true);
            markAdjust(line1 + read_linecount, line2, MAXLNUM, 0L, true);
         }

         //Put cursor on first filtered line for ":range!cmd".
         //Adjust '[ and '] (set by bookWrite()).
         curPor->cursor.lnum = line1;
         del_lines(linecount, true);
         curBook->opStart.lnum -= linecount;   //adjust '[
         curBook->opEnd.lnum -= linecount;      //adjust ']
         write_lnum_adjust(-linecount);      //adjust last line for next write
         foldUpdate(curPor, curBook->opStart.lnum, curBook->opEnd.lnum);
      } else {
         //Put cursor on last new line for ":r !cmd".
         linecount = curBook->opEnd.lnum - curBook->opStart.lnum + 1;
         curPor->cursor.lnum = curBook->opEnd.lnum;
      }

      beginline(BL_WHITE | BL_FIX);       //cursor on first non-blank
      --no_wait_return;

      if (do_in) {
         eeSnprintf(msg_buf, sizeof(msg_buf), _("%ld lines filtered"), (long)linecount);
         if (msg(msg_buf) && !msg_scroll)
            //save message to display it after redraw
            set_keep_msg((CS)msg_buf, 0);
      } else
         msgmore((long)linecount);
   } else {
error:
      //put cursor back in same position for ":w !cmd"
      curPor->cursor = cursor_save;
      --no_wait_return;
      wait_return(false);
   }

filterend:

   commModifierG.cmod_flags = save_cmod_flags;
   if (curBook != curBookSaved) {
      --no_wait_return;
      emsg(_(e_filter_autocommands_must_not_change_current_buffer));
   } ei (commModifierG.cmod_flags & CMOD_LOCKMARKS) {
      curBook->opStart = orig_start;
      curBook->opEnd = orig_end;
   }

   if (itmp)
      mch_remove(itmp);
   if (otmp)
      mch_remove(otmp);
   eeglFree(itmp);
   eeglFree(otmp);
}

//Call a shell to execute a command. When "cmd" is NULL, start an interactive shell.
pub void
do_shell(CS cmd, Unt flags) {   //may be SHELL_DOOUT when output is redirected
   int keep_termcap = !termcap_active;

   //For autocommands we want to get the output on the current screen, to avoid having to type 
   //return below.
   msg_putchar('\r');         //put cursor at start of line
   if (!autocmd_busy && !keep_termcap)
      termStopTerminfo();
   msg_putchar('\n');      //may shift screen one line up

   //warning message before calling the shell
   if (!autocmd_busy && msg_silent == 0) {
      Book* book;
      FOR_ALL_BOOKS(book) {
         if (bookWasChangedNotTerm(book)) {
            msg_puts(_("[No write since last change]\n"));
            break;
         }
      }
   } 
   //This windgoto is required for when the '\n' resulted in a 
   //"delete line 1" command to the terminal.
   if (!termIsScreenBeingSwapped())
      windgoto(msgRowG, msgColG);
   cursor_on();
   
   (void)chCallShell(text(cmd), SHELL_COOKED | flags);
   did_check_timestamps = false;
   need_check_timestamps = true;

   //put the message cursor at the end of the screen, avoids wait_return()
   //to overwrite the text that the external command showed
   if (!termIsScreenBeingSwapped()) {
      msgRowG = visibleRowsG - 1;
      msgColG = 0;
   }

   if (autocmd_busy) {
      if (msg_silent == 0)
         redraw_later_clear();
   } else {
      //For ":sh" there is no need to call wait_return(), just redraw.
      //Otherwise there is probably text on the screen that the user wants
      //to read before redrawing, so call wait_return().

      if (!keep_termcap)   //if keep_termcap is true didn't stop termcap
         starttermcap();   //start termcap if not done by wait_return()
   }

   display_errors();

   applyAutocomms(EVENT_SHELLCMDPOST, NULL, NULL, false, curBook);
}

//Ask the user to enter a number.
//When "mouse_used" is not NULL allow using the mouse and in that case return the line number.
pub int
prompt_for_number(int *mouse_used) {
   int      save_commlineRowG;
   int      save_State;

   //When using ":silent" assume that <CR> was entered.
   if (mouse_used)
      msg_puts(_("Type number and <Enter> or click with the mouse (q or empty cancels): "));
   else
      msg_puts(_("Type number and <Enter> (q or empty cancels): "));

   //Set the state such that text can be selected/copied/pasted and we still
   //get mouse events. redraw_after_callback() will not redraw if commlineRowG
   //is zero.
   save_commlineRowG = commlineRowG;
   commlineRowG = 0;
   save_State = stateG;
   stateG = MODE_COMMLINE;
   //May show different mouse shape.
   setmouse();

   int i = get_number(true, mouse_used);
   if (keyWasTypedG) {
      //don't call wait_return() now
      if (msgRowG > 0)
         commlineRowG = msgRowG - 1;
      need_wait_return = false;
      msg_didany = false;
      msg_didout = false;
   } else
      commlineRowG = save_commlineRowG;
   stateG = save_State;
   //May need to restore mouse shape.
   setmouse();

   return i;
}

//Create a shell command from a command string, input redirection file and
//output redirection file.
//Return an allocated string with the shell command, or NULL for failure.
pub CS
make_filter_cmd(CS cmd, NULLABLE CS inputFName, NULLABLE CS outputFName){
   Ulong len = (Ulong)STRLEN(cmd) + 3;      //"()" + ZERO

   if (inputFName) {
      len += (Ulong)STRLEN(inputFName) + 9;   //" { < " + " } " + ZERO
   }
   if (outputFName)
      len += (Ulong)STRLEN(outputFName) + 2; //"  "

   CS stringBuild = alloc(len);

   //Put braces around the command (for concatenated commands) when
   //redirecting input and/or output.
   if (inputFName || outputFName) {
      eeSnprintf(stringBuild, len, "(%s)", (char *)cmd);
   } else
      STRCPY(stringBuild, cmd);
   if (inputFName) {
      STRCAT(stringBuild, " < ");
      STRCAT(stringBuild, inputFName);
   }
    
   return stringBuild;
}

//Implementation of ":fixdel", also used by get_stty().
//<BS>    resulting <Del>
// ^?      ^H
//not ^?   ^?
pub void
do_fixdel(Invocation*) {
    CS p = find_termcode(S"kb");
    termAddRecognizedTermcode(S"kD", p && *p == DEL ? (CS)CTRL_H_STR : DEL_STR, false);
}

private void
print_line_no_prefix(LineNr lnum, int list) {
   Byte numbuf[30];
   eeSnprintf(numbuf, sizeof(numbuf), "%*ld ", number_width(curPor), (long)lnum);
   msgPutsDeco(numbuf, getDecoFlags(HLF_N));   //Highlight line nrs
   msg_prt_line(ml_get(lnum), list);
}

//Print a text line.  Also in silent mode ("ex -s").
private void
print_line(LineNr lnum, int list) {
   Boole save_silent = silentModeG;

   //apply :filter /pat/
   if (message_filtered(ml_get(lnum)))
      return;

   msg_start();
   silentModeG = false;
   info_message = true;   //use mch_msg(), not mch_errmsg()
   print_line_no_prefix(lnum, list);
   if (save_silent) {
      msg_putchar('\n');
      cursor_on();      //msg_start() switches it off
      out_flush();
      silentModeG = save_silent;
   }
   info_message = false;
}

private int
renameBook(CS new_fname) {
   Book* book = curBook;
   applyAutocomms(EVENT_BUFFILEPRE, NULL, NULL, false, curBook);
   //book changed, don't change name now
   if (book != curBook)
      return FAIL;
   if (aborting())       //autocmds may abort script processing
      return FAIL;
   //The name of the current book will be changed.
   //A new (unlisted) book entry needs to be made to hold the old file
   //name, which will become the alternate file name.
   //But don't set the alternate file name if the book didn't have a name.
   CS fname = curBook->fullFileName;
   CS sfname = curBook->shortFileName;
   CS xfname = curBook->currFileName;
   curBook->fullFileName = NULL;
   curBook->shortFileName = NULL;
   if (setfname(curBook, new_fname, NULL, true) == FAIL) {
      curBook->fullFileName = fname;
      curBook->shortFileName = sfname;
      return FAIL;
   }
   
   curBook->flags |= BF_NOTEDITED;
   if (xfname && *xfname != ZERO) {
      book = bookNew(fname, xfname, curPor->cursor.lnum, 0);
      if (book && (commModifierG.cmod_flags & CMOD_KEEPALT) == 0)
          curPor->altFnum = book->fiNum;
   }
   eeglFree(fname);
   eeglFree(sfname);
   applyAutocomms(EVENT_BUFFILEPOST, NULL, NULL, false, curBook);

   //Change directories when the 'acd' option is set.
   DO_AUTOCHDIR;
   return OK;
}

//}}}
//{{{writin' to files

//":file[!] [fname]".
pub void
c_file(Invocation* invo) {
   //":0file" removes the file name.  Check for illegal uses ":3file", "0file name", etc.
   if (invo->addr_count > 0 && (*invo->arg != ZERO || invo->line2 > 0 || invo->addr_count > 1)) {
      emsg(_(e_invalid_argument));
      return;
   }

   if (*invo->arg != ZERO || invo->addr_count == 1) {
      if (renameBook(invo->arg) == FAIL)
          return;
      needRedrawTabpanelG = true;
   }

   //print file name if no argument or 'F' is not in 'shortmess'
   fileinfo(false, false, invo->forceit);
}

//":update".
pub void
c_update(Invocation* invo) {
   if (bookWasChanged(curBook))
      (void)do_write(invo);
}

//":write" and ":saveas".
pub void
c_write(Invocation* invo) {
   if (invo->id == C_saveas) {
      //:saveas does not take a range, uses all lines.
      invo->line1 = 1;
      invo->line2 = curBook->mem.lineCount;
   }

   if (invo->usefilter)      //input lines to shell command
      do_bang(1, invo, false, true, false);
   else
      (void)do_write(invo);
}

private int
check_writable(CS fname) {
   if (mch_nodetype(fname) == NODE_OTHER) {
      showErrFmtMsg(_(e_str_is_not_file_or_writable_device), fname);
      return FAIL;
   }
   return OK;
}

//Check if it is allowed to overwrite a file.  If flags has BF_NOTEDITED, BF_NEW or BF_READERR, 
//check for overwriting current file. May set invo->forceit if a dialog says it's OK to overwrite.
//Return OK if it's OK, FAIL if it is not.
private int
check_overwrite(
   Invocation* invo,
   Book* book,
   CS fname,       //file name to be used (can differ from book->fullFName)
   CS fullFName,    //full path version of fname
   Boole other)       //writing under other name
{
   //Write to another file or flags set or not writing the whole file: overwriting only allowed 
   //with '!'. If "other" is false and bt_nofilename(book) is true, this must be
   //writing an "acwrite" book to the same file as its fullFileName, and bookWrite() will only 
   //allow writing with BufWriteCmd autocommands, so there is no need for an overwrite check.
   if (       (other
      || (!bt_nofilename(book)
          && ((book->flags & BF_NOTEDITED)
         || (book->flags & BF_NEW)
         || (book->flags & BF_READERR))))
       && !p_wa
       && eeFexists(fullFName)
   ) {
      if (!invo->forceit && !invo->append) {
         if (mch_isdir(fullFName)) {
            showErrFmtMsg(_(e_str_is_directory), fullFName);
            return FAIL;
         }
         if (p_confirm || (commModifierG.cmod_flags & CMOD_CONFIRM)) {
            Byte buff[DIALOG_MSG_SIZE];
            dialog_msg(buff, _("Overwrite existing file \"%s\"?"), fname);
            if (eeDialog_yesno(EE_QUESTION, NULL, buff, 2) != EE_YES)
                return FAIL;
            invo->forceit = true;
         } else {
            emsg(_(e_file_exists));
            return FAIL;
         }
      }

      //For ":w! filename" check that no swap file exists for "filename".
      if (other && !emsg_silent) {
         CS swapname = fiBuildSwapOrUndoFname(fullFName, false);
         int r = eeFexists(swapname);
         if (r) {
            if (p_confirm || (commModifierG.cmod_flags & CMOD_CONFIRM)) {
               Byte buff[DIALOG_MSG_SIZE];
               dialog_msg(buff, _("Swap file \"%s\" exists, overwrite anyway?"), swapname);
               if (eeDialog_yesno(EE_QUESTION, NULL, buff, 2) != EE_YES) {
                  eeglFree(swapname);
                  return FAIL;
               }
               invo->forceit = true;
            } else {
               showErrFmtMsg(_(e_swap_file_exists_str_silent_overrides), swapname);
               eeglFree(swapname);
               return FAIL;
            }
         }
         eeglFree(swapname);
      }
   }
   return OK;
}


//Write the current book to file "invo->arg".
//If "invo->append" is true, append to the file.
//
//If "*invo->arg == ZERO" write to current file.
//
//Return FAIL for failure, OK otherwise.
pub int
do_write(Invocation* invo) {
   CS fname = NULL;      //init to shut up gcc
   int retval = FAIL;
   CS free_fname = NULL;
   Book* altBook = NULL;
   int name_was_missing;

   if (isWritingForbidden())      //check @modfiable option and p_modifiable command-line flag
      return FAIL;

   CS fullFName = invo->arg;
   Boole sameFile;
   if (*fullFName == ZERO) {
      if (invo->id == C_saveas) {
         emsg(_(e_argument_required));
         goto theend;
      }
      sameFile = true;
   } else {
      fname = fullFName;
      free_fname = fiExpandAndCopy(fullFName, true);
      //When out-of-memory, keep unexpanded file name, because we MUST be
      //able to write the file in this situation.
      if (free_fname)
         fullFName = free_fname;
      sameFile = fNameMatchesCurBook(fullFName);
   }

   //If we have a new file, put its name in the list of alternate file names.
   if (!sameFile) {
      altBook = setaltfname(fullFName, fname, (LineNr)1);
      if (altBook && altBook->mem.mfile) {
         //Overwriting a file that is loaded in another book is not a good idea.
         emsg(_(e_file_is_loaded_in_another_buffer));
         goto theend;
      }
   }

   //A file name is required. "nofile" and "nowrite" books cannot be written implicitly either.
   if (sameFile && (bookDontWrite_msg(curBook) || check_fname() == FAIL
         || check_writable(curBook->fullFileName) == FAIL
         || check_readonly(OUT &invo->forceit, curBook))
   )
      goto theend;

   if (sameFile) {
      fullFName = curBook->fullFileName;
      fname = curBook->currFileName;
      //Not writing the whole file is only allowed with '!'.
      if ((invo->line1 != 1 || invo->line2 != curBook->mem.lineCount)
            && !invo->forceit
            && !invo->append
            && !p_wa
      ){
         if (p_confirm || (commModifierG.cmod_flags & CMOD_CONFIRM)) {
            if (eeDialog_yesno(EE_QUESTION, NULL, (CS)_("Write partial file?"), 2) != EE_YES)
               goto theend;
            invo->forceit = true;
         } else {
            emsg(_(e_use_bang_to_write_partial_buffer));
            goto theend;
         }
      }
   }

   if (check_overwrite(invo, curBook, fname, fullFName, !sameFile) == OK) {
      if (invo->id == C_saveas && altBook) {
         Book* was_curbuf = curBook;

         applyAutocomms(EVENT_BUFFILEPRE, NULL, NULL, false, curBook);
         applyAutocomms(EVENT_BUFFILEPRE, NULL, NULL, false, altBook);
         if (curBook != was_curbuf || aborting()) {
            //book changed, don't change name now
            retval = FAIL;
            goto theend;
         }
         //Exchange the file names for the current and the alternate book. This makes it look 
         //like we are now editing the book under the new name. Must be done before bookWrite(), 
         //because if there is no file name and 'cpo' contains 'F', it will set the file name.
         fname = altBook->currFileName;
         altBook->currFileName = curBook->currFileName;
         curBook->currFileName = fname;
         fname = altBook->fullFileName;
         altBook->fullFileName = curBook->fullFileName;
         curBook->fullFileName = fname;
         fname = altBook->shortFileName;
         altBook->shortFileName = curBook->shortFileName;
         curBook->shortFileName = fname;
         bookHandleNameChange(curBook);

         applyAutocomms(EVENT_BUFFILEPOST, NULL, NULL, false, curBook);
         applyAutocomms(EVENT_BUFFILEPOST, NULL, NULL, false, altBook);
         if (!altBook->o.bookListed) {
            altBook->o.bookListed = true;
            applyAutocomms(EVENT_BUFADD, NULL, NULL, false, altBook);
         }
         if (curBook != was_curbuf || aborting()) {
            //book changed, don't write the file
            retval = FAIL;
            goto theend;
         }

         //If 'filetype' was empty try detecting it now.
         if (!curBook->fileType) {
            if (auGroupExists(S"filetypedetect"))
                (void)do_doautocmd(S"filetypedetect BufRead", true, NULL);
         }

         //Autocommands may have changed book names, esp. when 'autochdir' is set.
         fname = curBook->shortFileName;
      }

      name_was_missing = curBook->fullFileName == NULL;

      retval = bookWrite(curBook, fullFName, fname, invo->line1, invo->line2,
                invo, invo->append, invo->forceit, true, false);
      if (retval == NOTDONE) {
         emsg(_(e_cannot_make_changes_modifiable_is_off));
      } 
      //After ":saveas fname" reset 'readonly'.
      if (invo->id == C_saveas) {
         if (retval == OK) {
            curBook->o.modifiable = true;
            needRedrawTabpanelG = true;
         }
      }

      //Change directories when the 'acd' option is set and the file name got changed or set.
      if (invo->id == C_saveas || name_was_missing)
          DO_AUTOCHDIR;
   }

theend:
   eeglFree(free_fname);
   return retval;
}

//Handle ":wnext", ":wNext" and ":wprevious" commands.
pub void
c_wnext(Invocation* invo){
   int      i;
   if (invo->comm[1] == 'n')
      i = curPor->argListInd + (int)invo->line2;
   else
      i = curPor->argListInd - (int)invo->line2;
   invo->line1 = 1;
   invo->line2 = curBook->mem.lineCount;
   if (do_write(invo) != FAIL)
      do_argfile(invo, i);
}

//}}}
//{{{editing files

//":wall", ":wqall" and ":xall": Write all changed files (and exit).
pub void
do_wqall(Invocation* invo){
   int error = 0;
   int save_forceit = invo->forceit;

   if (invo->id == C_xall || invo->id == C_wqall) {
      if (before_quit_all(invo) == FAIL)
          return;
      isExitingG = true;
   }

   Book* book;
   FOR_ALL_BOOKS(book) {
      if (isExitingG && term_job_running(book->term)) {
          no_write_message_nobang(book);
          ++error;
      } ei (bookWasChanged(book) && !bookDontWrite(book)) {
         //Check if there is a reason the book cannot be written:
         //1. if the book is not modifiable
         //2. if there is no file name (even after browsing)
         //3. if the 'readonly' is set (even after a dialog)
         //4. if overwriting is allowed (even after a dialog)
         if (isWritingForbidden()) {
            ++error;
            break;
         }
         if (book->fullFileName == NULL) {
            showErrFmtMsg(_(e_no_file_name_for_buffer_nr), (long)book->fiNum);
            ++error;
         } ei (check_readonly(OUT &invo->forceit, book)
             || check_overwrite(invo, book, book->currFileName, book->fullFileName, false) == FAIL
         ) {
            ++error;
         } else {
            BookRef bookRef;

            bookStoreInRef(OUT &bookRef, book);
            if (bookWrite_all(book, invo->forceit) != OK)
               ++error;
            //an autocommand may have deleted the book
            if (!bookRefValid(&bookRef))
               book = firstBook;
         }
         invo->forceit = save_forceit;    //check_overwrite() may set it
      }
   }
   if (isExitingG) {
      if (!error)
         exitEegl(0);
      not_exiting();
   }
}

//Check the @modifiable option. Return true and give a message when it's not set.
private Boole
isWritingForbidden(void) {
   if (IMMUTABLE) {
      emsg(_(e_file_not_written_writing_is_disabled_by_write_option));
      return false;
   } 
   return false;
}

//Check if a book is read-only (either 'modifiable' option is not set or file is
//read-only). Ask for overruling in a dialog. Return true and give an error
//message when the book is readonly.
private Boole
check_readonly(OUT Boole* forceit, Book* book) {
   FileStat   st;

   //Handle a file being readonly when the 'readonly' option is set or when
   //the file exists and permissions are read-only.
   //We will send 0777 to check_file_readonly(), as the "perm" variable is
   //important for device checks but not here.
   if (!*forceit && (!book->o.modifiable
         || (stat((char *)book->fullFileName, &st) >= 0 
               && check_file_readonly(book->fullFileName, 0777)))
   ) {
      if ((p_confirm || (commModifierG.cmod_flags & CMOD_CONFIRM)) && book->currFileName) {
         Byte buff[DIALOG_MSG_SIZE];

         if (!book->o.modifiable)
            dialog_msg(buff, _("'readonly' option is set for \"%s\".\nDo you wish to write anyway?"),
                book->currFileName);
         else {
            dialog_msg(buff, _("File permissions of \"%s\" are read-only.\n"
                     "It may still be possible to write it.\nDo you wish to try?"),
                book->currFileName);
         } 

         if (eeDialog_yesno(EE_QUESTION, NULL, buff, 2) == EE_YES) {
            //Set forceit, to force the writing of a readonly file
            *forceit = true;
            return false;
         } else
            return true;
      } ei (!book->o.modifiable)
         emsg(_(e_readonly_option_is_set_add_bang_to_override));
      else
         showErrFmtMsg(_(e_str_is_read_only_add_bang_to_override), book->currFileName);
      return true;
   }

   return false;
}

//Try to abandon the current file and edit a new or existing file.
//"fnum" is the number of the file, if zero use "ffname_arg"/"sfname_arg".
//"lnum" is the line number for the cursor in the new file (if non-zero).
//
//Return:
//GETFILE_ERROR for "normal" error,
//GETFILE_NOT_WRITTEN for "not written" error,
//GETFILE_SAME_FILE for success
//GETFILE_OPEN_OTHER for successfully opening another file.
pub int
getfile(
   int fnum,
   CS ffname_arg,
   CS sfname_arg,
   int setpm,
   LineNr lnum,
   Boole forceit
) {
   CS fullFName = ffname_arg;
   CS sfname = sfname_arg;
   int      retval;
   CS free_me = NULL;

   if (!portCheckCanSetCurBookForceIt(forceit))
      return GETFILE_ERROR;

   if (text_locked())
      return GETFILE_ERROR;
   if (curBookLocked())
      return GETFILE_ERROR;

   Boole sameFile;
   if (fnum == 0) {
      //make fullFName full path, set sfname
      fname_expand(&fullFName, &sfname);
      sameFile = fNameMatchesCurBook(fullFName);
      free_me = fullFName;      //has been allocated, free() later
   } else
      sameFile = (fnum == curBook->fiNum);

   if (!sameFile)
      ++no_wait_return;       //don't wait for autowrite message
   if (!sameFile)
      --no_wait_return;
   if (setpm)
      setpcmark();
   if (sameFile) {
      if (lnum != 0)
         curPor->cursor.lnum = lnum;
      check_cursor_lnum();
      beginline(BL_SOL | BL_FIX);
      retval = GETFILE_SAME_FILE;   //it's in the same file
   } ei (startEditingFile(fnum, fullFName, sfname, NULL, lnum,
           ECMD_HIDE + (forceit ? ECMD_FORCEIT : 0),
         curPor) == OK) {
      retval = GETFILE_OPEN_OTHER;   //opened another file
   } else
      retval = GETFILE_ERROR;      //error encountered

   eeglFree(free_me);
   return retval;
}

private int append_indent = 0;       //autoindent for first line

//":insert" and ":append", also used by ":change"
pub void
c_append(Invocation* invo) {
   CS theline;
   int did_undo = false;
   LineNr lnum = invo->line2;
   int indent = 0;
   CS p;
   int vcol;
   int empty = (curBook->mem.flags & ML_EMPTY);

   //the ! flag toggles autoindent
   if (invo->forceit)
      curBook->o.autoIndent = !curBook->o.autoIndent;

   //First autoindent comes from the line we start on
   if (invo->id != C_change && curBook->o.autoIndent && lnum > 0)
      append_indent = get_indent_lnum(lnum);

   if (invo->id != C_append)
      --lnum;

   //when the buffer is empty need to delete the dummy line
   if (empty && lnum == 1)
      lnum = 0;

   stateG = MODE_INSERT;          //behave like in Insert mode
   if (curBook->o.b_p_iminsert == B_IMODE_LMAP)
      stateG |= MODE_LANGMAP;

   for (;;) {
      msg_scroll = true;
      need_wait_return = false;
      if (curBook->o.autoIndent) {
         if (append_indent >= 0) {
            indent = append_indent;
            append_indent = -1;
         } ei (lnum > 0)
            indent = get_indent_lnum(lnum);
      }
      if (*invo->arg == '|') {
         //Get the text after the trailing bar.
         theline = copyStr(invo->arg + 1);
         *invo->arg = ZERO;
      } ei (!invo->ea_getline) {
         //No getline() function, use the lines that follow. This ends when there is no more.
         break;
      } else {
         int save_State = stateG;

         //Set stateG to avoid the cursor shape to be set to MODE_INSERT
         //state when getline() returns.
         stateG = MODE_COMMLINE;
         theline = invo->ea_getline(ZERO, invo->cookie, indent, true);
         stateG = save_State;
      }
      lines_left = visibleRowsG - 1;
      if (theline == NULL)
          break;


      //Look for the "." after automatic indent.
      vcol = 0;
      for (p = theline; indent > vcol; ++p) {
         if (*p == ' ')
            ++vcol;
         ei (*p == TAB)
            vcol += 8 - vcol % 8;
         else
            break;
      }
      if ((p[0] == '.' && p[1] == ZERO)
         || (!did_undo && u_save(lnum, lnum + 1 + (empty ? 1 : 0)) == FAIL))
      {
         eeglFree(theline);
         break;
      }

      //don't use autoindent if nothing was typed.
      if (p[0] == ZERO)
         theline[0] = ZERO;

      did_undo = true;
      ml_append(lnum, theline, (ColNr)0, false);
      if (empty)
         //there are no marks below the inserted lines
         appended_lines(lnum, 1L);
      else
         appended_lines_mark(lnum, 1L);

      eeglFree(theline);
      ++lnum;

      if (empty) {
         ml_delete(2L);
         empty = false;
      }
   }
   stateG = MODE_NORMAL;

   if (invo->forceit)
      curBook->o.autoIndent = !curBook->o.autoIndent;

   //"start" is set to invo->line2+1 unless that position is invalid (when
   //invo->line2 pointed to the end of the buffer and nothing was appended)
   //"end" is set to lnum when something has been appended, otherwise
   //it is the same as "start"  -- Acevedo
   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
      curBook->opStart.lnum = (invo->line2 < curBook->mem.lineCount) ?
          invo->line2 + 1 : curBook->mem.lineCount;
      if (invo->id != C_append)
          --curBook->opStart.lnum;
      curBook->opEnd.lnum = (invo->line2 < lnum)
                        ? lnum : curBook->opStart.lnum;
      curBook->opStart.col = curBook->opEnd.col = 0;
   }
   curPor->cursor.lnum = lnum;
   check_cursor_lnum();
   beginline(BL_SOL | BL_FIX);

   need_wait_return = false;   //don't use wait_return() now
   ex_no_reprint = true;
}

//":change"
pub void
c_change(Invocation* invo) {
   if (invo->line2 >= invo->line1 && u_save(invo->line1 - 1, invo->line2 + 1) == FAIL)
      return;

   //the ! flag toggles autoindent
   if (invo->forceit ? !curBook->o.autoIndent : curBook->o.autoIndent)
      append_indent = get_indent_lnum(invo->line1);

   LineNr lnum;
   for (lnum = invo->line2; lnum >= invo->line1; --lnum) {
      if (curBook->mem.flags & ML_EMPTY)       //nothing to delete
         break;
      ml_delete(invo->line1);
   }

   //make sure the cursor is not beyond the end of the file now
   check_cursor_lnum();
   deleted_lines_mark(invo->line1, (long)(invo->line2 - lnum));

   //":append" on the line above the deleted lines.
   invo->line2 = invo->line1;
   c_append(invo);
}

//"z" family of commands
pub void
c_z(Invocation* invo) {
   long bigness;
   CS kind;
   int minus = 0;
   LineNr start, end, curs, i;
   int j;
   LineNr lnum = invo->line2;

   //Vi compatible: ":z!" uses display height, without a count uses 'scroll'
   if (invo->forceit)
      bigness = visibleRowsG - 1;
   ei (!ONLY_ONE_PORTAL)
      bigness = curPor->height - 3;
   else
      bigness = curPor->scroll * 2;
   if (bigness < 1)
      bigness = 1;

   CS x = invo->arg;
   kind = x;
   if (*kind == '-' || *kind == '+' || *kind == '=' || *kind == '^' || *kind == '.')
      ++x;
   while (*x == '-' || *x == '+') {
      ++x;
   } 

   if (*x != 0) {
      if (!EE_ISDIGIT(*x)) {
         emsg(_(e_non_numeric_argument_to_z));
         return;
      } else {
         bigness = atol((char *)x);

         //bigness could be < 0 if atol(x) overflows.
         if (bigness > 2 * curBook->mem.lineCount || bigness < 0)
            bigness = 2 * curBook->mem.lineCount;

         if (*kind == '=')
            bigness += 2;
      }
   }

   //the number of '-' and '+' multiplies the distance
   if (*kind == '-' || *kind == '+') {
      for (x = kind + 1; *x == *kind; ++x)
          ;
   }

   switch (*kind) {
   case '-':
      start = lnum - bigness * (LineNr)(x - kind) + 1;
      end = start + bigness - 1;
      curs = end;
      break;
   case '=':
      start = lnum - (bigness + 1) / 2 + 1;
      end = lnum + (bigness + 1) / 2 - 1;
      curs = lnum;
      minus = 1;
      break;
   case '^':
      start = lnum - bigness * 2;
      end = lnum - bigness;
      curs = lnum - bigness;
      break;
   case '.':
      start = lnum - (bigness + 1) / 2 + 1;
      end = lnum + (bigness + 1) / 2 - 1;
      curs = end;
      break;
   default:  //'+'
      start = lnum;
      if (*kind == '+')
         start += bigness * (LineNr)(x - kind - 1) + 1;
      ei (invo->addr_count == 0)
         ++start;
      end = start + bigness - 1;
      curs = end;
      break;
   }

   if (start < 1)
      start = 1;

   if (end > curBook->mem.lineCount)
      end = curBook->mem.lineCount;

   if (curs > curBook->mem.lineCount)
      curs = curBook->mem.lineCount;
   ei (curs < 1)
      curs = 1;

   for (i = start; i <= end; i++) {
      if (minus && i == lnum) {
         msg_putchar('\n');

         for (j = 1; j < visibleColsG; j++)
            msg_putchar('-'); 
      }

      print_line(i, invo->flags & EXFLAG_LIST);

      if (minus && i == lnum) {
         msg_putchar('\n');

         for (j = 1; j < visibleColsG; j++)
            msg_putchar('-');
      }
   }

   if (curPor->cursor.lnum != curs) {
      curPor->cursor.lnum = curs;
      curPor->cursor.col = 0;
   }
   ex_no_reprint = true;
}

//}}}
//{{{substitutions

private CS prevSubstS = NULL;   //previous substitute pattern
private Boole globalNeedBeginlineS = false;   //call beginline() after ":g"

//Flags that are kept between calls to :substitute.
typedef struct {
   Boole do_all;    //do multiple substitutions per line
   Boole do_ask;    //ask for confirmation
   Boole do_count;  //count only
   Boole do_error;  //if false, ignore errors
   Boole do_print;  //print last line with subs.
   Boole do_list;   //list last line with subs.
   Boole do_number; //list last line with line nr
   Boole do_ic;     //ignore case flag
} SubstitutionState;

//Skip over the "sub" part in :s/pat/sub/ where "delimiter" is the separating character.
pub CS
skip_substitute(CS start, int delimiter) {
   CS p = start;

   while (p[0]) {
      if (p[0] == delimiter) {    //end delimiter found
          *p++ = ZERO;         //replace it with a ZERO
          break;
      }
      if (p[0] == '\\' && p[1] != 0)   //skip escaped characters
          ++p;
      MB_PTR_ADV(p);
   }
   return p;
}

private int
check_regexp_delim(int c) {
   if (SAFE_isalpha(c)) {
      emsg(_(e_regular_expressions_cant_be_delimited_by_letters));
      return FAIL;
   }
   return OK;
}

//Perform a substitution from line invo->line1 to line invo->line2 using the
//command pointed to by invo->arg which should be of the form:
//
///pattern/substitution/{flags}
//
//The usual escapes are supported as described in the regexp docs.
//:S is the case-sensitive variant
//The & repeats previous substitute command
pub void
c_substitute(Invocation* invo) {
   LineNr lnum;
   long i = 0;
   RegMultilineMatch regmatch;
   static SubstitutionState subflags = {false, false, false, true, false, false, false, 0};
   SubstitutionState subflags_save;
   int save_do_all;      //remember user specified 'g' flag
   int save_do_ask;      //remember user specified 'c' flag
   Text pat = (Text){null, 0};
   CS sub = null;
   int delimiter;
   int sublen;
   int got_quit = false;
   int got_match = false;
   int which_pat;
   CS p;
   int  save_State;
   LineNr first_line = 0;      //first changed line
   LineNr last_line = 0;      //below last changed line AFTER the change
   LineNr old_line_count = curBook->mem.lineCount;
   LineNr line2;
   long nmatch;         //number of lines in match
   int endcolumn = false;   //cursor in last column when done
   Pos old_cursor = curPor->cursor;
   int keeppatterns = commModifierG.cmod_flags & CMOD_KEEPPATTERNS;
   int save_ma = 0;
   TextProp* text_props = NULL;

   CS cmd = invo->arg;
   if (!global_busy) {
      sub_nsubs = 0;
      sub_nlines = 0;
   }
   int start_nsubs = sub_nsubs;

   if (invo->id == C_tilde)
      which_pat = RE_LAST; //use last used regexp
   else
      which_pat = RE_SUBST; //use last substitute regexp new pattern and substitution
      
   if ((invo->comm[0] == 's' || invo->comm[0] == 'S') && *cmd != ZERO && !SPACE_OR_TAB(*cmd)
      && firstOccurrence(S"0123456789cegriIp|\"", *cmd) == NULL
   ) {
      //don't accept alphanumeric for separator
      if (check_regexp_delim(*cmd) == FAIL)
          return;

      //undocumented vi feature:
      // "\/sub/" and "\?sub?" use last used search pattern (almost like
      // //sub/r).  "\&sub&" use last substitute pattern (like //sub/).
      if (*cmd == '\\') {
         ++cmd;
         if (firstOccurrence((CS)"/?&", *cmd) == NULL) {
            emsg(_(e_backslash_should_be_followed_by));
            return;
         }
         if (*cmd != '&')
            which_pat = RE_SEARCH;       //use last '/' pattern
         pat = (Text){null, 0};          //empty search pattern
         delimiter = *cmd++;          //remember delimiter character
      } else {     //find the end of the regexp
         which_pat = RE_LAST;       //use last used regexp
         delimiter = *cmd++;          //remember delimiter character
         pat = mbText(cmd);             //remember start of search pat
         cmd = skip_regexp_ex(cmd, delimiter, true, &invo->arg, NULL, NULL);
         if (cmd[0] == delimiter)       //end delimiter found
            *cmd++ = ZERO;          //replace it with a ZERO
      }

      //Small incompatibility: vi sees '\n' as end of the command, but in
      //Eeegl we want to use '\n' to find/substitute a ZERO.
      p = cmd;       //remember the start of the substitution
      cmd = skip_substitute(cmd, delimiter);
      sub = copyStr(p);

      if (!invo->skip) {
         if (!keeppatterns) {
            eeglFree(prevSubstS);
            prevSubstS = copyStr(sub);
         }
      }
   } ei (!invo->skip) {  //use previous pattern and substitution
      if (!prevSubstS) {  //there is no previous command
         emsg(_(e_no_previous_substitute_regular_expression));
         return;
      }
      pat = (Text){null, 0};      //search_regcomp() will use previous pattern
      sub = copyStr(prevSubstS);

      //Vi compatibility quirk: repeating with ":s" keeps the cursor in the
      //last column after using "$".
      endcolumn = (curPor->cursWant == MAXCOL);
   }

   //Recognize ":%s/\n//" and turn it into a line join, which is much more efficient.
   //TODO: find a generic solution to make line-joining operations more
   //efficient, avoid allocating a string that grows in size.
   if (pat.len > 1 && STRCMP(pat.c, "\\n") == 0
       && *sub == ZERO
       && (*cmd == ZERO || (cmd[1] == ZERO && (*cmd == 'g' || *cmd == 'l'
                    || *cmd == 'p' || *cmd == '#'))))
    {
      if (invo->skip) {
          eeglFree(sub);
          return;
      }
      curPor->cursor.lnum = invo->line1;
      if (*cmd == 'l')
          invo->flags = EXFLAG_LIST;
      ei (*cmd == '#')
          invo->flags = EXFLAG_NR;
      ei (*cmd == 'p')
          invo->flags = EXFLAG_PRINT;

      //The number of lines joined is the number of lines in the range plus
      //one.  One less when the last line is included.
      LineNr joined_lines_count = invo->line2 - invo->line1 + 1;
      if (invo->line2 < curBook->mem.lineCount)
         ++joined_lines_count;
      if (joined_lines_count > 1) {
         (void)doJoinLinesUnderCursor(joined_lines_count, false, true, false, true);
         sub_nsubs = joined_lines_count - 1;
         sub_nlines = 1;
         (void)do_sub_msg(false);
         mayPrint(invo);
      }

      if (!keeppatterns)
         save_re_pat(RE_SUBST, pat, true);
      //put pattern in history
      scrAddToHistory(HIST_SEARCH, pat, true, ZERO);
      eeglFree(sub);

      return;
   }

   //Find trailing options.  When '&' is used, keep old options.
   if (*cmd == '&')
      ++cmd;
   else {
      subflags.do_all = true; //default is global on
      subflags.do_ask = false;
      subflags.do_error = true;
      subflags.do_print = false;
      subflags.do_list = false;
      subflags.do_count = false;
      subflags.do_number = false;
      subflags.do_ic = 0;
   }
   while (*cmd) {
      //Note that 'g' and 'c' are always inverted, 'r' is never inverted.
      if (*cmd == 'g')
         subflags.do_all = !subflags.do_all;
      ei (*cmd == 'c')
         subflags.do_ask = !subflags.do_ask;
      ei (*cmd == 'n')
         subflags.do_count = true;
      ei (*cmd == 'e')
         subflags.do_error = !subflags.do_error;
      ei (*cmd == 'r')       //use last used regexp
         which_pat = RE_LAST;
      ei (*cmd == 'p')
         subflags.do_print = true;
      ei (*cmd == '#') {
         subflags.do_print = true;
         subflags.do_number = true;
      } ei (*cmd == 'l') {
         subflags.do_print = true;
         subflags.do_list = true;
      } ei (*cmd == 'i')       //ignore case
         subflags.do_ic = 'i';
      ei (*cmd == 'I')       //don't ignore case
         subflags.do_ic = 'I';
      else
         break;
      ++cmd;
   }
   if (subflags.do_count)
      subflags.do_ask = false;

   save_do_all = subflags.do_all;
   save_do_ask = subflags.do_ask;

   //check for a trailing count
   cmd = skipwhite(cmd);
   if (EE_ISDIGIT(*cmd)) {
      i = parseLong(&cmd);
      if (i <= 0 && !invo->skip && subflags.do_error) {
         emsg(_(e_positive_count_required));
         eeglFree(sub);
         return;
      } ei (i >= INT_MAX) {
         Byte buf[20];
         eeSnprintf(buf, sizeof(buf), "%ld", i);
         showErrFmtMsg(_(e_val_too_large), buf);
         eeglFree(sub);
         return;
      }
      invo->line1 = invo->line2;
      invo->line2 += i - 1;
      if (invo->line2 > curBook->mem.lineCount)
         invo->line2 = curBook->mem.lineCount;
   }

   //check for trailing command or garbage
   cmd = skipwhite(cmd);
   if (*cmd != ZERO && !isComment(cmd)) {      //if not end-of-line or comment
      showErrFmtMsg(_(e_trailing_characters_str), cmd);
      eeglFree(sub);
      return;
   }

   if (invo->skip) {      //not executing commands, only parsing
      eeglFree(sub);
      return;
   }

   if (!subflags.do_count && (!curBook->o.modifiable || !p_modifiable)) {
      //Substitution is not allowed in immutable buffers
      emsg(_(e_cannot_make_changes_modifiable_is_off));
      eeglFree(sub);
      return;
   }

   if (search_regcomp(pat, NULL, RE_SUBST, which_pat, SEARCH_HIS, OUT &regmatch) == FAIL) {
      if (subflags.do_error)
         emsg(_(e_invalid_command));
      eeglFree(sub);
      return;
   }

   //the 'i' or 'I' flag overrules 'ignorecase' and 'smartcase'
   if (invo->comm[0] == 'S') {
      regmatch.rmm_ic = false;
   } ei (subflags.do_ic == 'i') {
      regmatch.rmm_ic = true;
   } ei (subflags.do_ic == 'I') {
      regmatch.rmm_ic = false;
   }

   CS sub_firstline = NULL;//allocated copy of first sub line

   //If the substitute pattern starts with "\=" then it's an expression.
   //Make a copy, a recursive function may free it.
   //Otherwise, '~' in the substitute pattern is replaced with the old
   //pattern.  We do it here once to avoid it to be replaced over and over again.
   if (sub[0] == '\\' && sub[1] == '=') {
      p = copyStr(sub);
      eeglFree(sub);
      sub = p;
   } else {
      p = regtilde(sub);

      if (p != sub) {
          eeglFree(sub);
          sub = p;
      }
   }

   //Check for a match on each line.
   line2 = invo->line2;
   for (lnum = invo->line1; lnum <= line2 && !(got_quit || aborting()); ++lnum) {
      nmatch = eeRegexec_multi(&regmatch, curPor, curBook, lnum, (ColNr)0, NULL);
      if (nmatch) {
         ColNr   copycol;
         ColNr   matchcol;
         ColNr   prev_matchcol = MAXCOL;
         CS new_end;
         CS new_start = null;
         unsigned   new_start_len = 0;
         CS p1;
         int did_sub = false;
         int lastone;
         int len, copy_len, needed_len;
         long   nmatch_tl = 0;   //nr of lines matched below lnum
         int do_again;   //do it again after joining lines
         int skip_match = false;
         LineNr   sub_firstlnum;   //nr of first sub line
         int apc_flags = APC_SAVE_FOR_UNDO | APC_SUBSTITUTE;
         ColNr total_added =  0;
         int text_prop_count = 0;

         //The new text is build up step by step, to avoid too much
         //copying.  There are these pieces:
         //sub_firstline   The old text, unmodified.
         //copycol      Column in the old text where we started
         //        looking for a match; from here old text still
         //        needs to be copied to the new text.
         //matchcol      Column number of the old text where to look
         //        for the next match.  It's just after the
         //        previous match or one further.
         //prev_matchcol   Column just after the previous match (if any).
         //        Mostly equal to matchcol, except for the first
         //        match and after skipping an empty match.
         //regmatch.*pos   Where the pattern matched in the old text.
         //new_start   The new text, all that has been produced so far.
         //new_end      The new text, where to append new text.
         //
         //lnum      The line number where we found the start of
         //        the match.  Can be below the line we searched
         //        when there is a \n before a \zs in the
         //        pattern.
         //sub_firstlnum   The line number in the buffer where to look
         //        for a match.  Can be different from "lnum"
         //        when the pattern or substitute string contains
         //        line breaks.
         //
         //Special situations:
         //- When the substitute string contains a line break, the part up
         //  to the line break is inserted in the text, but the copy of
         //  the original line is kept.  "sub_firstlnum" is adjusted for the inserted lines.
         //- When the matched pattern contains a line break, the old line
         //  is taken from the line at the end of the pattern.  The lines
         //  in the match are deleted later, "sub_firstlnum" is adjusted accordingly.
         //
         //The new text is built up in new_start[].  It has some extra
         //room to avoid using alloc()/free() too often.  new_start_len is
         //the length of the allocated memory at new_start.
         //
         //Make a copy of the old line, so it won't be taken away when
         //updating the screen or handling a multi-line match.  The "old_"
         //pointers point into this copy.
         sub_firstlnum = lnum;
         copycol = 0;
         matchcol = 0;

         //At first match, remember current cursor position.
         if (!got_match) {
            setpcmark();
            got_match = true;
         }

         //Loop until nothing more to replace in this line.
         //1. Handle match with empty string.
         //2. If do_ask is set, ask for confirmation.
         //3. substitute the string.
         //4. if do_all is set, find next match
         //5. break if there isn't another match in this line
         for (;;) {
            //Advance "lnum" to the line where the match starts.  The
            //match does not start in the first line when there is a line break before \zs.
            if (regmatch.startpos[0].lnum > 0) {
               lnum += regmatch.startpos[0].lnum;
               sub_firstlnum += regmatch.startpos[0].lnum;
               nmatch -= regmatch.startpos[0].lnum;
               EE_CLEAR(sub_firstline);
            }

            //Match might be after the last line for "\n\zs" matching at the end of the last line.
            if (lnum > curBook->mem.lineCount)
               break;

            if (sub_firstline == NULL) {
               sub_firstline = copySubstr(ml_get(sub_firstlnum), ml_get_len(sub_firstlnum));
               if (sub_firstline == NULL) {
                  eeglFree(new_start);
                  goto outofmem;
               }
            }

            //Save the line number of the last change for the final cursor position
            curPor->cursor.lnum = lnum;
            do_again = false;

            //1. Match empty string does not count, except for first
            //match. This reproduces the strange vi behaviour. This also catches endless loops.
            if (matchcol == prev_matchcol
               && regmatch.endpos[0].lnum == 0
               && matchcol == regmatch.endpos[0].col
            ){
               if (sub_firstline[matchcol] == ZERO)
                  //We already were at the end of the line.  Don't look
                  //for a match in this line again.
                  skip_match = true;
               else {
                //search for a match at next column
                   matchcol += utfCharLen(sub_firstline + matchcol);
               }
               goto skip;
            }

            //Normally we continue searching for a match just after the previous match.
            matchcol = regmatch.endpos[0].col;
            prev_matchcol = matchcol;

            //2. If do_count is set only increase the counter.
            //   If do_ask is set, ask for confirmation.
            if (subflags.do_count) {
               //For a multi-line match, put matchcol at the ZERO at the end of the line and 
               //set nmatch to one, so that we continue looking for a match on the next line.
               //Avoids that ":s/\nB\@=//gc" get stuck.
               if (nmatch > 1) {
                  matchcol = (ColNr)STRLEN(sub_firstline);
                  nmatch = 1;
                  skip_match = true;
               }
               sub_nsubs++;
               did_sub = true;
               //Skip the substitution, unless an expression is used
               if (!(sub[0] == '\\' && sub[1] == '='))
                  goto skip;
            }

            if (subflags.do_ask) {
               Unt typed = 0;

               //change stateG to MODE_CONFIRM, so that the mouse works properly
               save_State = stateG;
               stateG = MODE_CONFIRM;
               setmouse();      //disable mouse in xterm
               curPor->cursor.col = regmatch.startpos[0].col;
               if (curPor->o.diff)
                  do_check_cursorbind();


               //Loop until 'y', 'n', 'q', CTRL-E or CTRL-Y typed.
               while (subflags.do_ask) {
                  CS orig_line = NULL;
                  int len_change = 0;
                  int save_p_lz = p_lz;
                  int save_p_fen = curPor->o.foldEnable;

                  curPor->o.foldEnable = false;
                  //Invert the matched string. Remove the inversion afterwards.
                  int save_isRedrawingDisabledG = isRedrawingDisabledG;
                  isRedrawingDisabledG = 0;

                  //avoid calling drawUpdateScreen() in vgetorpeek()
                  p_lz = false;

                  if (new_start) {
                     //There already was a substitution, we would like to show this to the user. 
                     //We cannot really update the line, it would change what matches.  
                     //Temporarily replace the line and change it back afterwards.
                     orig_line = copySubstr(ml_get(lnum), ml_get_len(lnum));
                     if (orig_line) {
                        CS new_line = concat_str(new_start, sub_firstline + copycol);

                        if (new_line == NULL)
                           EE_CLEAR(orig_line);
                        else {
                        //Position the cursor relative to the
                        //end of the line, the previous
                        //substitute may have inserted or
                        //deleted characters before the
                        //cursor.
                        len_change = (int)STRLEN(new_line)
                                - (int)STRLEN(orig_line);
                        curPor->cursor.col += len_change;
                        ml_replace(lnum, new_line, false);
                         }
                     }
                   }

                  search_match_lines = regmatch.endpos[0].lnum - regmatch.startpos[0].lnum;
                  search_match_endcol = regmatch.endpos[0].col + len_change;
                  if (search_match_lines == 0 && search_match_endcol == 0)
                     //highlight at least one character for /^/
                     search_match_endcol = 1;
                  highlight_match = true;

                  update_topline();
                  validate_cursor();
                  drawUpdateScreen(UPD_SOME_VALID);
                  highlight_match = false;
                  redraw_later(UPD_SOME_VALID);

                  curPor->o.foldEnable = save_p_fen;
                  if (msgRowG == visibleRowsG - 1)
                     msg_didout = false;   //avoid a scroll-up
                  msg_starthere();
                  i = msg_scroll;
                  msg_scroll = 0;      //truncate msg when needed
                  msg_no_more = true;
                  //write message same highlighting as for wait_return()
                  smsgDeco(getDecoFlags(HLF_R), _("replace with %s (y/n/a/q/l/^E/^Y)?"), sub);
                  msg_no_more = false;
                  msg_scroll = i;
                  showruler(true);
                  windgoto(msgRowG, msgColG);
                  isRedrawingDisabledG = save_isRedrawingDisabledG;

                  ++no_mapping;   //don't map this key
                  ++allow_keys;   //allow special keys
                  typed = plain_vgetc();
                  --allow_keys;
                  --no_mapping;

                  //clear the question
                  msg_didout = false;   //don't scroll up
                  msgColG = 0;
                  gotoCommline(true);
                  p_lz = save_p_lz;

                  //restore the line
                  if (orig_line)
                     ml_replace(lnum, orig_line, false);

                  need_wait_return = false; //no hit-return prompt
                  if (typed == 'q' || typed == ESC || typed == Ctrl_C 
                        || typed == extraInterruptCharG
                  ){
                     got_quit = true;
                     break;
                  }
                  if (typed == 'n')
                     break;
                  if (typed == 'y')
                     break;
                  if (typed == 'l') {
                     //last: replace and then stop
                     subflags.do_all = false;
                     line2 = lnum;
                     break;
                  }
                  if (typed == 'a') {
                      subflags.do_ask = false;
                      break;
                  }
                  if (typed == Ctrl_E)
                      scrollup_clamp();
                  ei (typed == Ctrl_Y)
                      scrolldown_clamp();
               }
               stateG = save_State;
               setmouse();

               if (typed == 'n') {
                  //For a multi-line match, put matchcol at the ZERO at the end of the line 
                  //and set nmatch to one, so that we continue looking for a match on the next 
                  //line. Avoids that ":%s/\nB\@=//gc" and ":%s/\n/,\r/gc" get stuck when 
                  //pressing 'n'.
                  if (nmatch > 1) {
                     matchcol = (ColNr)STRLEN(sub_firstline);
                     skip_match = true;
                  }
                  goto skip;
               }
               if (got_quit)
                  goto skip;
            }

            //Move the cursor to the start of the match, so that we can use "\=col(".").
            curPor->cursor.col = regmatch.startpos[0].col;

            //3. substitute the string.
            save_ma = curBook->o.modifiable;
            if (subflags.do_count) {
               //prevent accidentally changing the buffer by a function
               curBook->o.modifiable = false;
            }
            //Save flags for recursion.  They can change for e.g.
            //:s/^/\=execute("s#^##gn")
            subflags_save = subflags;

            //Disallow changing text or switching portal in an expression.
            ++textlock;
            //Get length of substitution part, including the ZERO. When it fails, sublen is 0
            sublen = eeRegsub_multi(&regmatch,
                      sub_firstlnum - regmatch.startpos[0].lnum,
                      sub, sub_firstline, 0,
                      REGSUB_BACKSLASH
                      | (REGSUB_MAGIC));
            --textlock;

            //If getting the substitute string caused an error, don't do the replacement.
            //Don't keep flags set by a recursive call.
            subflags = subflags_save;
            if (sublen == 0 || aborting() || subflags.do_count) {
                curBook->o.modifiable = save_ma;
                goto skip;
            }

            //When the match included the "$" of the last line it may
            //go beyond the last line of the buffer.
            if (nmatch > curBook->mem.lineCount - sub_firstlnum + 1) {
                nmatch = curBook->mem.lineCount - sub_firstlnum + 1;
                skip_match = true;
                //safety check
                if (nmatch < 0)
               goto skip;
            }

            //Need room for:
            //- result so far in new_start (not for first sub in line)
            //- original text up to match
            //- length of substituted part
            //- original text after match
            //Adjust text properties here, since we have all information needed.
            if (nmatch == 1) {
               p1 = sub_firstline;
               if (curBook->hasTextprop) {
                     int bytes_added = 
                        sublen - 1 - (regmatch.endpos[0].col - regmatch.startpos[0].col);

                  //When text properties are changed, need to save for
                  //undo first, unless done already.
                  if (adjustPropColumns(
                           lnum, total_added + regmatch.startpos[0].col, bytes_added, apc_flags
                     )
                  ) {
                      apc_flags &= ~APC_SAVE_FOR_UNDO;
                  } 
                  //Offset for column byte number of the text property
                  //in the resulting buffer afterwards.
                  total_added += bytes_added;
               }
            } else {
               LineNr   lastlnum = sub_firstlnum + nmatch - 1;
               if (curBook->hasTextprop) {

                  //Props in the first line may be shortened or deleted
                  if (adjustPropColumns(
                           lnum, total_added + regmatch.startpos[0].col, -MAXCOL, apc_flags
                           )
                  ) {
                      apc_flags &= ~APC_SAVE_FOR_UNDO;
                  } 
                  total_added -= (ColNr)STRLEN( sub_firstline + regmatch.startpos[0].col);

                  //Props in the last line may be moved or deleted
                  if (adjustPropColumns(lastlnum, 0, -regmatch.endpos[0].col, apc_flags))
                      //When text properties are changed, need to save
                      //for undo first, unless done already.
                      apc_flags &= ~APC_SAVE_FOR_UNDO;

                  //Copy the text props of the last line, they will be
                  //later appended to the changed line.
                  CS propStart;
                  text_prop_count = get_text_props(OUT &propStart, curBook, lastlnum, false);
                  if (text_prop_count > 0) {
                     //TODO: what when we already did this?
                     eeglFree(text_props);
                     text_props = ALLOC_MULT(TextProp, text_prop_count);

                     MEMMOVE(text_props, propStart, text_prop_count * sizeof(TextProp));
                     //After joining the text prop columns will increase.
                     for (int pi = 0; pi < text_prop_count; ++pi)
                        text_props[pi].col += regmatch.startpos[0].col + sublen - 1;
                  }
               }
               p1 = ml_get(lastlnum);
               nmatch_tl += nmatch - 1;
               if (curBook->hasTextprop)
                  total_added += (ColNr)STRLEN(p1 + regmatch.endpos[0].col);
            }
            copy_len = regmatch.startpos[0].col - copycol;
            needed_len = copy_len + ((unsigned)STRLEN(p1) - regmatch.endpos[0].col) + sublen + 1;
            if (new_start == NULL) {
               //Get some space for a temporary buffer to do the substitution into (and some 
               //extra space to avoid too many calls to alloc()/free()).
               new_start_len = needed_len + 50;
               new_start = allocZeroed(new_start_len);
               *new_start = ZERO;
               new_end = new_start;
            } else {
               //Check if the temporary buffer is long enough to do the
               //substitution into.  If not, make it larger (with a bit
               //extra to avoid too many calls to alloc()/free()).
               len = (unsigned)STRLEN(new_start);
               needed_len += len;
               if (needed_len > (int)new_start_len) {
                  new_start_len = needed_len + 50;
                  p1 = allocZeroed(new_start_len);
                  MEMMOVE(p1, new_start, (Unt)(len + 1));
                  eeglFree(new_start);
                  new_start = p1;
               }
               new_end = new_start + len;
            }

            //copy the text up to the part that matched
            MEMMOVE(new_end, sub_firstline + copycol, (Unt)copy_len);
            new_end += copy_len;

            if ((int)new_start_len - copy_len < sublen)
               sublen = new_start_len - copy_len - 1;

            ++textlock;
            (void)eeRegsub_multi(&regmatch,
                      sub_firstlnum - regmatch.startpos[0].lnum,
                        sub, new_end, sublen,
                        REGSUB_COPY | REGSUB_BACKSLASH | (REGSUB_MAGIC));
            --textlock;
            sub_nsubs++;
            did_sub = true;

            //Move the cursor to the start of the line, to avoid that it
            //is beyond the end of the line after the substitution.
            curPor->cursor.col = 0;

            //For a multi-line match, make a copy of the last matched line and continue in that one.
            if (nmatch > 1) {
               sub_firstlnum += nmatch - 1;
               eeglFree(sub_firstline);
               sub_firstline = copySubstr(ml_get(sub_firstlnum), ml_get_len(sub_firstlnum));
               //When going beyond the last line, stop substituting.
               if (sub_firstlnum <= line2)
                  do_again = true;
               else
                  subflags.do_all = false;
            }

            //Remember next character to be copied.
            copycol = regmatch.endpos[0].col;

            if (skip_match) {
               //Already hit end of the buffer, sub_firstlnum is one less than what it ought to be.
               eeglFree(sub_firstline);
               sub_firstline = copyStr((CS)"");
               copycol = 0;
            }

            /*
             * Now the trick is to replace CTRL-M chars with a real line break. This would make
             * it impossible to insert a CTRL-M in the text.  The line break can be avoided by 
             * preceding the CTRL-M with a backslash.  To be able to insert a backslash,
             * they must be doubled in the string and are halved here.
             */
            for (p1 = new_end; *p1; ++p1) {
               if (p1[0] == '\\' && p1[1] != ZERO) { //remove backslash
                  STRMOVE(p1, p1 + 1);
                  if (curBook->hasTextprop) {
                     //When text properties are changed, need to save
                     //for undo first, unless done already.
                     if (adjustPropColumns(lnum, (ColNr)(p1 - new_start), -1, apc_flags))
                        apc_flags &= ~APC_SAVE_FOR_UNDO;
                  }
               } ei (*p1 == ENTER) {
                  if (u_inssub(lnum) == OK) {  //prepare for undo
                     ColNr   plen = (ColNr)(p1 - new_start + 1);

                     *p1 = ZERO;          //truncate up to the CR
                     ml_append(lnum - 1, new_start, plen, false);
                     markAdjust(lnum + 1, (LineNr)MAXLNUM, 1L, 0L, true);
                     if (subflags.do_ask)
                        appended_lines(lnum - 1, 1L);
                     else {
                        if (first_line == 0)
                           first_line = lnum;
                        last_line = lnum + 1;
                     }
                     adjustPropsForSplit(lnum + 1, lnum, plen, 1, false);
                     //all line numbers increase
                     ++sub_firstlnum;
                     ++lnum;
                     ++line2;
                     //move the cursor to the new line, like Vi
                     ++curPor->cursor.lnum;
                     //copy the rest
                     STRMOVE(new_start, p1 + 1);
                     p1 = new_start - 1;
                  }
               } else
                  p1 += utfCharLen(p1) - 1;
            }

            //4. If do_all is set, find next match.
            //Prevent endless loop with patterns that match empty
            //strings, e.g. :s/$/pat/g or :s/[a-z]* /(&)/g.
            //But ":s/\n/#/" is OK.
      skip:
            //We already know that we did the last subst when we are at
            //the end of the line, except that a pattern like
            //"bar\|\nfoo" may match at the ZERO.  "lnum" can be below
            //"line2" when there is a \zs in the pattern after a line break.
            lastone = (skip_match
               || gotInterruptG
               || got_quit
               || lnum > line2
               || !(subflags.do_all || do_again)
               || (sub_firstline[matchcol] == ZERO && nmatch <= 1
                      && !re_multiline(regmatch.regprog)));
            nmatch = -1;

            //Replace the line in the buffer when needed.  This is skipped when there are more 
            //matches. The check for nmatch_tl is needed for when multi-line matching must replace
            //the lines before trying to do another match, otherwise "\@<=" won't work.
            //When the match starts below where we start searching, also need to replace the line 
            //first (using \zs after \n).
            if (lastone
               || nmatch_tl > 0
               || (nmatch = eeRegexec_multi(&regmatch, curPor,
                           curBook, sub_firstlnum,
                            matchcol, NULL)) == 0
               || regmatch.startpos[0].lnum > 0
            ){
                if (new_start) {
                  //Copy the rest of the line, that didn't match. "matchcol" has to be adjusted, 
                  //we use the end of the line as reference, because the substitute may
                  //have changed the number of characters. Same for "prev_matchcol".
                  STRCAT(new_start, sub_firstline + copycol);
                  matchcol = (ColNr)STRLEN(sub_firstline) - matchcol;
                  prev_matchcol = (ColNr)STRLEN(sub_firstline) - prev_matchcol;

                  if (u_savesub(lnum) != OK)
                     break;
                  ml_replace(lnum, new_start, true);
                  if (text_props)
                     add_text_props(lnum, text_props, text_prop_count);
                  if (nmatch_tl > 0) {
                     //Matched lines have now been substituted and are useless, delete them. 
                     //The part after the match has been appended to new_start, we don't need
                     //it in the buffer.
                     ++lnum;
                     if (u_savedel(lnum, nmatch_tl) != OK)
                        break;
                     for (i = 0; i < nmatch_tl; ++i)
                        ml_delete(lnum);
                     markAdjust(lnum, lnum + nmatch_tl - 1, (long)MAXLNUM, -nmatch_tl, true);
                     if (subflags.do_ask)
                        deleted_lines(lnum, nmatch_tl);
                     --lnum;
                     line2 -= nmatch_tl; //nr of lines decreases
                     nmatch_tl = 0;
                  }

                  //When asking, undo is saved each time, must also set
                  //changed flag each time.
                  if (subflags.do_ask)
                     changed_bytes(lnum, 0);
                  else {
                     if (first_line == 0)
                        first_line = lnum;
                     last_line = lnum + 1;
                  }

                  sub_firstlnum = lnum;
                  eeglFree(sub_firstline);    //free the temp buffer
                  sub_firstline = new_start;
                  new_start = NULL;
                  matchcol = (ColNr)STRLEN(sub_firstline) - matchcol;
                  prev_matchcol = (ColNr)STRLEN(sub_firstline) - prev_matchcol;
                  copycol = 0;
               }
               if (nmatch == -1 && !lastone)
                  nmatch = eeRegexec_multi(&regmatch, curPor, curBook, sub_firstlnum, matchcol, NULL);

               //5. break if there is no other match on this line
               if (nmatch <= 0) {
                  //If the match found didn't start where we were searching, do the next search in the 
                  //line where we found the match.
                  if (nmatch == -1)
                     lnum -= regmatch.startpos[0].lnum;
                  break;
               }
            }

            line_breakcheck();
          }

          if (did_sub)
         ++sub_nlines;
          eeglFree(new_start);   //for when substitute was cancelled
          EE_CLEAR(sub_firstline);   //free the copy of the original line
      }

      line_breakcheck();
   }

   if (first_line != 0) {
      //Need to subtract the number of added lines from "last_line" to get
      //the line number before the change (same as adding the number of deleted lines).
      i = curBook->mem.lineCount - old_line_count;
      doChangedLines(first_line, 0, last_line - i, i);
   }

outofmem:
   eeglFree(sub_firstline); //may have to free allocated copy of the line

   eeglFree(text_props);

   //":s/pat//n" doesn't move the cursor
   if (subflags.do_count)
      curPor->cursor = old_cursor;

   if (sub_nsubs > start_nsubs) {
      if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
         //Set the '[ and '] marks.
         curBook->opStart.lnum = invo->line1;
         curBook->opEnd.lnum = line2;
         curBook->opStart.col = curBook->opEnd.col = 0;
      }

      if (!global_busy) {
         //when interactive leave cursor on the match
         if (!subflags.do_ask) {
            if (endcolumn)
                coladvance((ColNr)MAXCOL);
            else
                beginline(BL_WHITE | BL_FIX);
         }
         if (!do_sub_msg(subflags.do_count) && subflags.do_ask)
            msg(S"");
      } else
         globalNeedBeginlineS = true;
      if (subflags.do_print)
         print_line(curPor->cursor.lnum, subflags.do_list);
   } ei (!global_busy) {
      if (gotInterruptG)      //interrupted
          emsg(_(e_interrupted));
      ei (got_match)   //did find something but nothing substituted
          msg(S"");
      ei (subflags.do_error)   //nothing found
          showErrFmtMsg(_(e_pattern_not_found_str), get_search_pat());
   }

   if (subflags.do_ask && hasAnyFolding(curPor))
      //Cursor position may require updating
      didChangePortalSettingCurPor();

   eeRegFree(regmatch.regprog);
   eeglFree(sub);

   //Restore the flag values, they can be used for ":&&".
   subflags.do_all = save_do_all;
   subflags.do_ask = save_do_ask;
}

//Give message for number of substitutions. Can also be used after a ":global" command.
//Return true if a message was given.
pub int
do_sub_msg(int       count_only) {    //used 'n' flag for ":s"
   //Only report substitutions when:
   //- command was typed by user, or number of changed lines > 0
   //- giving messages is not disabled by 'lazyredraw'
   if (messaging()) {

      if (gotInterruptG)
         STRCPY(msg_buf, _("(Interrupted) "));
      else
         *msg_buf = ZERO;

      CS msg_single = count_only
          ? NGETTEXT("%ld match on %ld line", "%ld matches on %ld line", sub_nsubs)
          : NGETTEXT("%ld substitution on %ld line", "%ld substitutions on %ld line", sub_nsubs);
      CS msg_plural = count_only
          ? NGETTEXT("%ld match on %ld lines", "%ld matches on %ld lines", sub_nsubs)
          : NGETTEXT("%ld substitution on %ld lines", "%ld substitutions on %ld lines", sub_nsubs);

      eeSnprintfAdd(
            msg_buf, sizeof(msg_buf), NGETTEXT(msg_single, msg_plural, sub_nlines),
            sub_nsubs, (long)sub_nlines
      );

      if (msg(msg_buf))
         //save message to display it after redraw
         set_keep_msg(msg_buf, 0);
      return true;
   }
   if (gotInterruptG) {
      emsg(_(e_interrupted));
      return true;
   }
   return false;
}

//Get the previous substitute pattern.
pub CS
get_old_sub(void) {
   return prevSubstS;
}

//Set the previous substitute pattern.  "val" must be allocated.
pub void
set_old_sub(CS val) {
   eeglFree(prevSubstS);
   prevSubstS = val;
}

#if defined(EXITFREE)
pub void
free_old_sub(void) {
   eeglFree(prevSubstS);
}
#endif

//}}}
//{{{global

private void
global_exe_one(CS cmd, LineNr lnum) {
   curPor->cursor.lnum = lnum;
   curPor->cursor.col = 0;
   if (*cmd == ZERO || *cmd == '\n')
      doCommand(S"p", NULL, NULL, DOCMD_NOWAIT);
   else
      doCommand(cmd, NULL, NULL, DOCMD_NOWAIT);
}

//Execute a global command of the form:
//
//g/pattern/X : execute X on all lines where pattern matches
//v/pattern/X : execute X on all lines where pattern does not match
//
//where 'X' is a Command
//
//The command character (as well as the trailing slash) is optional, and is assumed to be 'p' if 
//missing.
//
//This is implemented in two passes: first we scan the file for the pattern and set a mark for 
//each line that (not) matches. Secondly we execute the command for each line that has a mark. 
//This is required because after deleting lines we do not know where to search for the next match.
pub void
c_global(Invocation* invo) {
   LineNr lnum;      //line number according to old situation
   int ndone = 0;
   int type;      //first char of cmd: 'v' or 'g'
   CS cmd;      //command argument

   Byte delim;      //delimiter, normally '/'
   Text pat;
   CS used_pat;
   RegMultilineMatch   regmatch;
   int match;
   int which_pat;

   //When nesting the command works on one line.  This allows for
   //":g/found/v/notfound/command".
   if (global_busy && (invo->line1 != 1 || invo->line2 != curBook->mem.lineCount)) {
      //will increment global_busy to break out of the loop
      emsg(_(e_cannot_do_global_recursive_with_range));
      return;
   }

   if (invo->forceit)          //":global!" is like ":vglobal"
     type = 'v';
   else
     type = *invo->comm;
   cmd = invo->arg;
   which_pat = RE_LAST;       //default: use last used regexp


   //undocumented feature: 
   // "\/" and "\?": use previous search pattern.
   //     "\&": use previous substitute pattern.
   if (*cmd == '\\') {
      ++cmd;
      if (firstOccurrence((CS)"/?&", *cmd) == NULL) {
         emsg(_(e_backslash_should_be_followed_by));
         return;
      }
      if (*cmd == '&')
          which_pat = RE_SUBST;   //use previous substitute pattern
      else
          which_pat = RE_SEARCH;   //use previous search pattern
      ++cmd;
      pat = (Text){null, 0};
   } ei (*cmd == ZERO) {
      emsg(_(e_regular_expression_missing_from_global));
      return;
   } ei (check_regexp_delim(*cmd) == FAIL) {
      return;
   } else {
      delim = *cmd;      //get the delimiter
      ++cmd;         //skip delimiter if there is one
      pat = text(cmd);      //remember start of pattern
      cmd = skip_regexp_ex(cmd, delim, true, &invo->arg, NULL, NULL);
      if (cmd[0] == delim)          //end delimiter found
          *cmd++ = ZERO;          //replace it with a ZERO
   }

   if (search_regcomp(pat, &used_pat, RE_BOTH, which_pat, SEARCH_HIS, OUT &regmatch) 
         == FAIL
   ) {
      emsg(_(e_invalid_command));
      return;
   }

   if (global_busy) {
      lnum = curPor->cursor.lnum;
      match = eeRegexec_multi(&regmatch, curPor, curBook, lnum,
                            (ColNr)0, NULL);
      if ((type == 'g' && match) || (type == 'v' && !match))
          global_exe_one(cmd, lnum);
   } else {
      //pass 1: set marks for each (not) matching line
      for (lnum = invo->line1; lnum <= invo->line2 && !gotInterruptG; ++lnum) {
         //a match on this line?
         match = eeRegexec_multi(&regmatch, curPor, curBook, lnum, (ColNr)0, NULL);
         if (regmatch.regprog == NULL)
            break;  //re-compiling regprog failed
         if ((type == 'g' && match) || (type == 'v' && !match)) {
            ml_setmarked(lnum);
            ndone++;
         }
         line_breakcheck();
      }

      //pass 2: execute the command for each line that has been marked
      if (gotInterruptG)
         msg(_(e_interrupted));
      ei (ndone == 0) {
         if (type == 'v') {
            smsg(_("Pattern found in every line: %s"), used_pat);
         } else {
            showErrFmtMsg(_(e_pattern_not_found_str), used_pat);
         }
      } else {
          global_exe(cmd);
      }

      ml_clearmarked();      //clear rest of the marks
   }

   eeRegFree(regmatch.regprog);
}

//Execute "cmd" on lines marked with ml_setmarked().
pub void
global_exe(CS cmd) {
   LineNr old_lcount;   //mem.lineCount before the command
   Book    *old_buf = curBook;   //remember what buffer we started in
   LineNr lnum;      //line number according to old situation

   //Set current position only once for a global command.
   //If global_busy is set, setpcmark() will not do anything.
   //If there is an error, global_busy will be incremented.
   setpcmark();

   //When the command writes a message, don't overwrite the command.
   msg_didout = true;

   sub_nsubs = 0;
   sub_nlines = 0;
   globalNeedBeginlineS = false;
   global_busy = 1;
   old_lcount = curBook->mem.lineCount;
   while (!gotInterruptG && (lnum = ml_firstmarked()) != 0 && global_busy == 1) {
      global_exe_one(cmd, lnum);
      ui_breakcheck();
   }

   global_busy = 0;
   if (globalNeedBeginlineS)
      beginline(BL_WHITE | BL_FIX);
   else
      check_cursor();   //cursor may be beyond the end of the line

   //the cursor may not have moved in the text but a change in a previous
   //line may move it on the screen
   changed_line_abv_curs();

   //If it looks like no message was written, allow overwriting the
   //command with the report for number of changes.
   if (msgColG == 0 && msg_scrolled == 0)
      msg_didout = false;

   //If substitutes done, report number of substitutes, otherwise report
   //number of extra or deleted lines.
   //Don't report extra or deleted lines in the edge case where the buffer
   //we are in after execution is different from the buffer we started in.
   if (!do_sub_msg(false) && curBook == old_buf)
      msgmore(curBook->mem.lineCount - old_lcount);
}

//}}}
//{{{misc

//Set up for a tagpreview. Make the preview portal the current portal.
//Return true when it was created.
pub int
prepare_tagpreview(
   int      undo_sync,       //sync undo when leaving the portal
   int      use_previewpopup,   //use popup if 'previewpopup' set
   UsePopup   use_popup       //use other popup portal
){
   if (curPor->isPreview)
      return false;

   //If there is already a preview portal open, use that one.
   Portal* po;
   if (use_previewpopup) {
      po = popupFindPreviewPortal();
      if (po)
         popup_set_wantpos_cursor(po, po->pup.minWidth, NULL);
   } ei (use_popup != USEPOPUP_NONE) {
      po = popupFindInfoPortal();
      if (po) {
         if (use_popup == USEPOPUP_NORMAL)
            popup_show(po);
         else
            popup_hide(po);
         //When the popup moves or resizes it may reveal part of
         //another portal.  TODO: can this be done more efficiently?
         redraw_all_later(UPD_NOT_VALID);
      }
   } else {
      FOR_ALL_PORTALS(po) {
         if (po->isPreview)
            break;
      } 
   }
   if (po) {
      enterPortal(po, undo_sync);
      return false;
   }

   //There is no preview portal open yet.  Create one.
   if ((use_previewpopup) || use_popup != USEPOPUP_NONE)
      return portalCreatePreviewPortal(use_popup != USEPOPUP_NONE);
   if (splitPortal(g_do_tagpreview > 0 ? g_do_tagpreview : 0, 0) == FAIL)
      return false;
   curPor->isPreview = true;
   curPor->o.portFixHeight = true;
   curPor->o.diff = false;       //no 'diff', don't take over scrollbinding and cursorbinding
   return true;
}


//Make the user happy.
pub void
c_smile(Invocation*) {
   static char *code[] = {
   "\34 \4o\14$\4ox\30 \2o\30$\1ox\25 \2o\36$\1o\11 \1o\1$\3 \2$\1 \1o\1$x\5 \1o\1 \1$\1 \2o\10 "
   "\1o\44$\1o\7 \2$\1 \2$\1 \2$\1o\1$x\2 \2o\1 \1$\1 \1$\1 \1\"\1$\6 \1o\11$\4 \15$\4 \11$\1o\7 "
   "\3$\1o\2$\1o\1$x\2 \1\"\6$\1o\1$\5 \1o\11$\6 \13$\6 \12$\1o\4 \10$x\4 \7$\4 \13$\6 \13$\6 "
   "\27$x\4 \27$\4 \15$\4 \16$\2 \3\"\3$x\5 \1\"\3$\4\"\61$\5 \1\"\3$x\6 \3$\3 \1o\62$\5 "
   "\1\"\3$\1ox\5 \1o\2$\1\"\3 \63$\7 \3$\1ox\5 \3$\4 \55$\1\"\1 \1\"\6$",
   "\5o\4$\1ox\4 \1o\3$\4o\5$\2 \45$\3 \1o\21$x\4 \10$\1\"\4$\3 \42$\5 \4$\10\"x\3 \4\"\7 \4$\4 "
   "\1\"\34$\1\"\6 \1o\3$x\16 \1\"\3$\1o\5 \3\"\22$\1\"\2$\1\"\11 \3$x\20 \3$\1o\12 "
   "\1\"\2$\2\"\6$\4\"\13 \1o\3$x\21 \4$\1o\40 \1o\3$\1\"x\22 \1\"\4$\1o\6 \1o\6$\1o\1\"\4$\1o\10 "
   "\1o\4$x\24 \1\"\5$\2o\5 \2\"\4$\1o\5$\1o\3 \1o\4$\2\"x\27 \2\"\5$\4o\2 \1\"\3$\1o\11$\3\"x\32 "
   "\2\"\7$\2o\1 \12$x\42 \4\"\13$x\46 \14$x\47 \12$\1\"x\50 \1\"\3$\4\"x"
   };

   msg_start();
   msg_putchar('\n');
   for (int i = 0; i < 2; ++i) {
      for (char* p = code[i]; *p != ZERO; ++p) {
         if (*p == 'x')
            msg_putchar('\n');
         else {
            for (int n = *p++; n > 0; --n) {
               msg_putchar(*p);
            } 
         } 
      } 
   } 
   msg_clr_eos();
}

//":drop" Open the first argument in a portal, and the argument list is redefined.
pub void
c_drop(Invocation* invo) {
   int      split = false;
   Portal   *po;

   if (portErrorIfPopup(false) || portErrorIfTermPopup())
      return;

   //Check if the first argument is already being edited in a portal. If so, jump to that portal.
   //We would actually need to check all arguments, but that's complicated
   //and mostly only one file is dropped.
   //This also ignores wildcards, since it is very unlikely the user is
   //editing a file name with a wildcard character.
   set_arglist(invo->arg);

   //Expanding wildcards may result in an empty argument list.  E.g. when
   //editing "foo.pyc" and ".pyc" is in 'wildignore'.  Assume that we
   //already did an error message for this.
   if (ARGCOUNT == 0)
      return;

   if (commModifierG.cmod_tab) {
      //":tab drop file ...": open a tab for each argument that isn't
      //edited in a portal yet.  It's like ":tab all" but without closing portals or tabs.
      c_all(invo);
      commModifierG.cmod_tab = 0;
      c_rewind(invo);
      return;
   }

   //":drop file ...": Edit the first argument.  Jump to an existing portal if possible, edit in 
   //current portal if the current book can be abandoned, otherwise open a new portal.
   Book* book = bookFindFileByBookNr(ARGLIST[0].fnum);

   Tab   *t;
   FOR_ALL_TAB_PORTALS(t, po) {
      if (po->book == book) {
         goto_tab_port(t, po);
         curPor->argListInd = 0;
         if (!bookWasChanged(curBook)) {
            bookCheckTimestamp(curBook);
         }
         if (curBook->mem.flags & ML_EMPTY)
            c_rewind(invo);
         return;
      }
   }

   //Fake a ":sfirst" or ":first" command edit the first argument.
   if (split) {
      invo->id = C_sfirst;
      invo->comm[0] = 's';
   } else
      invo->id = C_first;
   c_rewind(invo);
}

//As skipEeglGrepPat() and store the character overwritten by ZERO in "cp"
//and the pointer to it in "nulp".
private CS
skipEeglGrepPat_ext(CS p, Byte **s, Unt* flags, Byte** nulp, int *cp) {
   int c;

   if (eeIsIdentifierChar(*p)) {
      //":vimgrep pattern fname"
      if (s)
         *s = p;
      p = skiptowhite(p);
      if (s && *p != ZERO) {
         if (nulp != NULL) {
            *nulp = p;
            *cp = *p;
         }
         *p++ = ZERO;
      }
   } else {
      //":vimgrep /pattern/[g][j] fname"
      if (s)
         *s = p + 1;
      c = *p;
      p = skip_regexp(p + 1, c, true);
      if (*p != c)
         return NULL;

      //Truncate the pattern.
      if (s) {
         if (nulp) {
            *nulp = p;
            *cp = *p;
         }
         *p = ZERO;
      }
      ++p;

      //Find the flags
      while (*p == 'g' || *p == 'j' || *p == 'f') {
         if (flags) {
            if (*p == 'g')
               *flags |= VGR_GLOBAL;
            ei (*p == 'j')
               *flags |= VGR_NOJUMP;
            else
               *flags |= VGR_FUZZY;
          }
          ++p;
      }
   }
   return p;
}

//Skip over the pattern argument of ":vimgrep /pat/[g][j]". Put the start of the pattern in "*s", 
//unless "s" is NULL. If "flags" is not NULL put the flags in it: VGR_GLOBAL, VGR_NOJUMP.
//If "s" is not NULL terminate the pattern with a ZERO.
//Return a pointer to the char just past the pattern plus flags.
pub CS
skipEeglGrepPat(CS p, Byte **s, Unt *flags) {
   return skipEeglGrepPat_ext(p, s, flags, NULL, NULL);
}

//":argdo", ":windo", ":bufdo", ":tabdo", ":ldo"
pub void
c_listDo(Invocation* invo) {
   int i;
   Portal* po;
   Tab* t;
   Book* book = curBook;
   int next_fnum = 0;

   if (curPor->o.portFixBuf) {
      if (portalIsValid(prevPor) && !prevPor->o.portFixBuf)
          //@portfixbuf is set; attempt to change to a portal without it.
          gotoPortal(prevPor);
      if (curPor->o.portFixBuf) {
          //Split the portal, which will have its @portfixbuf off, and set curPor to that
          (void)splitPortal(0, 0);

         if (curPor->o.portFixBuf) {
            //Autocommands set @portfixbuf or sent us to another portal
            //with it set, or we failed to split the portal.  Give up.
            emsg(_(e_portfixbuf_cannot_go_to_buffer));
            return;
         }
      }
   }

   CS save_ei = NULL;

   if (invo->id != C_windo && invo->id != C_tabdo) {
      //Don't do syntax HL autocommands. Skipping the syntax file is a great speed improvement.
      save_ei = au_event_disable(S",Syntax");

      FOR_ALL_BOOKS(book) {
         book->flags &= ~BF_SYN_SET;
      } 
      book = curBook;
   }

   i = 0;
   //start at the invo->line1 argument/portal/book
   po = firstPor;
   t = firstTabG;
   switch (invo->id) {
   case C_windo:
      for ( ; po && i + 1 < invo->line1; po = po->next)
          i++;
      break;
   case C_tabdo:
      for ( ; t && i + 1 < invo->line1; t = t->next)
          i++;
      break;
   case C_argdo:
      i = invo->line1 - 1;
      break;
   default:
      break;
   }
   //set pcmark now
   if (invo->id == C_bufdo) {
      //Advance to the first listed book after "invo->line1".
      for (book = firstBook; 
            book && (book->fiNum < invo->line1 || !book->o.bookListed); 
            book = book->next
      ) {
         if (book->fiNum > invo->line2) {
            book = NULL;
            break;
         }
      } 
      if (book)
         bookGoto(invo, DOBOOK_FIRST, FORWARD, book->fiNum);
   } else
       setpcmark();
   listcmd_busy = true;       //avoids setting pcmark below

   while (!gotInterruptG && book) {
      if (invo->id == C_argdo) {
         //go to argument "i"
         if (i == ARGCOUNT)
             break;
         //Don't call do_argfile() when already there, it will try reloading the file.
         if (curPor->argListInd != i || !editing_arg_idx(curPor)) {
             do_argfile(invo, i);
         }
         if (curPor->argListInd != i)
             break;
      } ei (invo->id == C_windo) {
         //go to portal "po"
         if (!portalIsValid(po))
            break;
         gotoPortal(po);
         if (curPor != po)
            break;  //something must be wrong
         po = curPor->next;
      } ei (invo->id == C_tabdo) {
         //go to portal "t"
         if (!isTabValid(t))
            break;
         gotoTab(t, true, true);
         t = t->next;
      } ei (invo->id == C_bufdo) {
         //Remember the number of the next listed book, in case
         //":bwipe" is used or autocommands do something strange.
         next_fnum = -1;
         for (book = curBook->next; book; book = book->next) {
            if (book->o.bookListed) {
               next_fnum = book->fiNum;
               break;
            }
         } 
      }

      ++i;

      //execute the command
      doCommand(invo->arg, invo->ea_getline, invo->cookie, DOCMD_VERBOSE + DOCMD_NOWAIT);

      if (invo->id == C_bufdo) {
         //Done?
         if (next_fnum < 0 || next_fnum > invo->line2)
             break;
         //Check if the book still exists.
         FOR_ALL_BOOKS(book) {
            if (book->fiNum == next_fnum)
               break;
         } 
         if (!book)
            break;

         bookGoto(invo, DOBOOK_FIRST, FORWARD, next_fnum);

         //If autocommands took us elsewhere, quit here.
         if (curBook->fiNum != next_fnum)
            break;
      }

      if (invo->id == C_windo) {
         validate_cursor();   //cursor may have moved

         //scrollbinding is required when @diff has been set
         if (curPor->o.diff)
            normPostProcessScrollbind(true);
      }

      if (invo->id == C_windo || invo->id == C_tabdo)
         if (i >= invo->line2)
            break;
      if (invo->id == C_argdo && i >= invo->line2)
         break;
   }
   listcmd_busy = false;

   if (save_ei) {
      Book      *bnext;
      AutocommSave   aco;

      au_event_restore(save_ei);

      for (book = firstBook; book; book = bnext) {
         bnext = book->next;
         if (book->countPortals > 0 && (book->flags & BF_SYN_SET)) {
            book->flags &= ~BF_SYN_SET;

            //book was opened while Syntax autocommands were disabled,
            //need to trigger them now.
            if (book == curBook)
               applyAutocomms(
                  EVENT_SYNTAX, curBook->syntaxName, curBook->currFileName, true, curBook
               );
            else {
               auCommPrepareBook(&aco, book);
               if (curBook == book) {
                  applyAutocomms(EVENT_SYNTAX, book->syntaxName, book->currFileName, true, book);
                  auCommRestoreBook(&aco);
               }
            }

            //start over, in case autocommands messed things up.
            bnext = firstBook;
          }
      }
    }
}

//":compiler[!] {name}"
pub void
c_compiler(Invocation* invo) {
   CS old_cur_comp = NULL;
   CS p;

   if (*invo->arg == ZERO) {
      //List all compiler scripts.
      executeCommLine((CS)"echo fiGlobpath(&rtp, 'compiler/*.vim')");
                  //) keep the indenter happy...
      return;
   }

   CS buf = alloc(STRLEN(invo->arg) + 14);

   if (invo->forceit) {
      //":compiler! {name}" sets global options
      executeCommLine((CS)
         "command -nargs=* -keepscript CompilerSet set <args>");
   } else {
      //":compiler! {name}" sets local options.
      //To remain backwards compatible "current_compiler" is always
      //used.  A user's compiler plugin may set it, the distributed
      //plugin will then skip the settings.  Afterwards set
      //"b:current_compiler" and restore "current_compiler".
      //Explicitly prepend "g:" to make it work in a function.
      old_cur_comp = get_var_value((CS)"g:current_compiler");
      if (old_cur_comp)
         old_cur_comp = copyStr(old_cur_comp);
      executeCommLine((CS) "command -nargs=* -keepscript CompilerSet setlocal <args>");
   }
   unletImpl(S"g:current_compiler", true);
   unletImpl(S"b:current_compiler", true);

   sprintf((char *)buf, "compiler/%s.vim", invo->arg);
   if (source_runtime(buf, DIP_ALL) == FAIL)
      showErrFmtMsg(_(e_compiler_not_supported_str), invo->arg);
   eeglFree(buf);

   executeCommLine((CS)":delcommand CompilerSet");

   //Set "b:current_compiler" from "current_compiler".
   p = get_var_value((CS)"g:current_compiler");
   if (p)
      set_internal_string_var((CS)"b:current_compiler", p);

   //Restore "current_compiler" for ":compiler {name}".
   if (!invo->forceit) {
      if (old_cur_comp) {
         set_internal_string_var((CS)"g:current_compiler", old_cur_comp);
         eeglFree(old_cur_comp);
      } else
         unletImpl((CS)"g:current_compiler", true);
   }
}

//":checktime [buffer]"
pub void
c_checktime(Invocation* invo){
   Book   *book;
   int      save_no_check_timestamps = no_check_timestamps;

   no_check_timestamps = 0;
   if (invo->addr_count == 0)   //default is all books
      check_timestamps(false);
   else {
      book = bookFindFileByBookNr((int)invo->line2);
      if (book)   //cannot happen?
         (void)bookCheckTimestamp(book);
   }
   no_check_timestamps = save_no_check_timestamps;
}
//}}}
//{{{writing & flushing

//If 'autowrite' option set, try to write the file. Careful: autocommands may make "book" invalid!
//return FAIL for failure, OK otherwise
pub int
autowrite(Book *book, int forceit) {
   if (!(p_aw || p_awa) || book->o.modifiable
        //never autowrite a "nofile" or "nowrite" book
        || bookDontWrite(book)
        || (!forceit && !book->o.modifiable) || !book->fullFileName
   )
      return FAIL;
   BookRef bookRef;
   bookStoreInRef(OUT &bookRef, book);
   int r = bookWrite_all(book, forceit);

   //Writing may succeed but the book still changed, e.g., when there is a
   //conversion error.  We do want to return FAIL then.
   if (bookRefValid(&bookRef) && bookWasChanged(book))
      r = FAIL;
   return r;
}

//Flush all books, except the ones that are readonly or are never written.
pub void
doFlushAllBooks(void) {
   if (!p_aw && !p_awa)
      return;
   Book* book;
   FOR_ALL_BOOKS(book) {
      if (bookWasChanged(book) && book->o.modifiable && !bookDontWrite(book)) {
         BookRef bookRef;
         bookStoreInRef(OUT &bookRef, book);

         (void)bookWrite_all(book, false);

         //an autocommand may have deleted the book
         if (!bookRefValid(&bookRef))
            book = firstBook;
      }
   }
}

//Return true if buffer was changed and cannot be abandoned. For flags use the CCGD_ values.
pub int
check_changed(Book *book, int flags) {
   int      forceit = (flags & CCGD_FORCEIT);
   BookRef   bookRef;

   bookStoreInRef(OUT &bookRef, book);

   if (       !forceit
       && bookWasChanged(book)
       && ((flags & CCGD_MULTWIN) || book->countPortals <= 1)
       && (!(flags & CCGD_AW) || autowrite(book, forceit) == FAIL)
    ){
      if ((p_confirm || (commModifierG.cmod_flags & CMOD_CONFIRM)) && book->o.modifiable) {
         if (term_job_running(book->term)) {
            return term_confirm_stop(book) == FAIL;
         }

         Book   *buf2;
         int      count = 0;

         if (flags & CCGD_ALLBOOKS) {
            FOR_ALL_BOOKS(buf2) {
               if (bookWasChanged(buf2) && (buf2->fullFileName)) {
                  ++count;
               } 
            } 
         }
         if (!bookRefValid(&bookRef))
            //Autocommand deleted buffer, oops!  It's not changed now.
            return false;

         dialog_changed(book, count > 1);

         if (!bookRefValid(&bookRef))
         //Autocommand deleted buffer, oops!  It's not changed now.
            return false;
         return bookWasChanged(book);
      }
      if (flags & CCGD_EXCMD)
         no_write_message();
      else
         no_write_message_nobang(curBook);
      return true;
   }
   return false;
}

//Ask the user what to do when abandoning a changed buffer. Must check 'write' option first!
pub void
dialog_changed(Book* book, int checkall) {  //may abandon all changed buffers
   Byte buff[DIALOG_MSG_SIZE];
   int ret;
   Book* buf2;
   Invocation invo;

   dialog_msg(buff, _("Save changes to \"%s\"?"), book->currFileName);
   if (checkall)
      ret = eeDialog_yesnoallcancel(EE_QUESTION, NULL, buff, 1);
   else
      ret = eeDialog_yesnocancel(EE_QUESTION, NULL, buff, 1);

   //Init invo pseudo-structure, this is needed for the check_overwrite() function.
   CLEAR_FIELD(invo);

   if (ret == EE_YES) {
      int   empty_bufname;

      empty_bufname = book->currFileName == NULL ? true : false;
      if (empty_bufname)
         bookSetName(book->fiNum, S"Untitled");

      if (check_overwrite(&invo, book, book->currFileName, book->fullFileName, false) == OK) {
         //didn't hit Cancel
         if (bookWrite_all(book, false) == OK)
            return;
      }

      //restore to empty when write failed
      if (empty_bufname) {
         book->currFileName = NULL;
         EE_CLEAR(book->fullFileName);
         EE_CLEAR(book->shortFileName);
         unchanged(book, false);
      }
   } ei (ret == EE_NO) {
      unchanged(book, false);
   } ei (ret == EE_ALL) {
      //Write all modified files that can be written.
      //Skip readonly buffers, these need to be confirmed individually.
      FOR_ALL_BOOKS(buf2) {
         if (bookWasChanged(buf2)
             && buf2->fullFileName
             && !bookDontWrite(buf2)
             && buf2->o.modifiable
         ) {
            BookRef bookRef;

            bookStoreInRef(OUT &bookRef, buf2);
            if (buf2->currFileName && check_overwrite(&invo, buf2,
                    buf2->currFileName, buf2->fullFileName, false) == OK
            )
               //didn't hit Cancel
               (void)bookWrite_all(buf2, false);

            //an autocommand may have deleted the buffer
            if (!bookRefValid(&bookRef))
               buf2 = firstBook;
          }
      }
   } ei (ret == EE_DISCARDALL) {
      FOR_ALL_BOOKS(buf2)
         unchanged(buf2, false);
   }
}

//Add a buffer number to "bufnrs", unless it's already there.
private void
add_bufnum(int *bufnrs, int *bufnump, int nr) {
   for (int i = 0; i < *bufnump; ++i) {
      if (bufnrs[i] == nr)
         return;
   } 
   bufnrs[*bufnump] = nr;
   *bufnump = *bufnump + 1;
}

//true if any buffer was changed and cannot be abandoned. That changed buffer becomes the 
//current buffer. When "unload" is true the current buffer is unloaded instead of making it
//hidden.  This is used for ":q!".
pub int
check_changed_any(Boole checkOnlyHidden, Boole unload) {
   int      ret = false;
   Book   *book;
   int      save;
   int      i;
   int      bufnum = 0;
   int      bufcount = 0;
   int      *bufnrs;
   Tab   *t;
   Portal   *po;

   //Make a list of all buffers, with the most important ones first.
   FOR_ALL_BOOKS(book)
      ++bufcount;

   if (bufcount == 0)
      return false;

   bufnrs = ALLOC_MULT(int, bufcount);

   //curBook
   bufnrs[bufnum++] = curBook->fiNum;

   //books in current tab
   FOR_ALL_PORTALS(po) {
      if (po->book != curBook)
         add_bufnum(bufnrs, &bufnum, po->book->fiNum);
   } 

    //buffers in other tabs
   FOR_ALL_TABS(t) {
      if (t != curtab) {
         FOR_ALL_PORTALS_IN_TAB(t, po)
            add_bufnum(bufnrs, &bufnum, po->book->fiNum);
      } 
   } 

   //any other book
   FOR_ALL_BOOKS(book)
      add_bufnum(bufnrs, &bufnum, book->fiNum);

   for (i = 0; i < bufnum; ++i) {
      book = bookFindFileByBookNr(bufnrs[i]);
      if (!book)
         continue;
      if ((!checkOnlyHidden || book->countPortals == 0) && bookWasChanged(book)) {
         BookRef bookRef;

         bookStoreInRef(OUT &bookRef, book);
         if (term_job_running(book->term)) {
            if (term_try_stop_job(book) == FAIL)
                break;
         } else
            //Try auto-writing the buffer.  If this fails but the buffer no
            //longer exists it's not changed, that's OK.
            if (check_changed(book, (p_awa ? CCGD_AW : 0)
                | CCGD_MULTWIN
                | CCGD_ALLBOOKS) && bookRefValid(&bookRef)
            )
               break;       //didn't save - still changes
      }
   }

   if (i >= bufnum)
      goto theend;

   //Get here if "book" cannot be abandoned.
   ret = true;
   isExitingG = false;
   //When ":confirm" used, don't give an error message.
   if (!(p_confirm || (commModifierG.cmod_flags & CMOD_CONFIRM))) {
      //There must be a wait_return() for this message, bookDo()
      //may cause a redraw.  But wait_return() is a no-op when vgetc()
      //is busy (Quit used from window menu), then make sure we don't cause a scroll up.
      if (vgetcBusyG > 0) {
          msgRowG = commlineRowG;
          msgColG = 0;
          msg_didout = false;
      }
      if (
         term_job_running(book->term)
             ? showErrFmtMsg(_(e_job_still_running_in_buffer_str), book->currFileName)
             :
         showErrFmtMsg(_(e_no_write_since_last_change_for_buffer_str),
             bookSpName(book) ? bookSpName(book) : book->currFileName))
      {
          save = no_wait_return;
          no_wait_return = false;
          wait_return(false);
          no_wait_return = save;
      }
   }

   //Try to find a portal into the buffer.
   if (book != curBook) {
      FOR_ALL_TAB_PORTALS(t, po) {
         if (po->book == book) {
            BookRef bookRef;
            bookStoreInRef(OUT &bookRef, book);

            goto_tab_port(t, po);

            //Paranoia: did autocomm wipe out the buffer with changes?
            if (!bookRefValid(&bookRef))
                goto theend;
            goto buf_found;
         }
      } 
   } 
buf_found:

   //Open the changed buffer in the current portal.
   if (book != curBook)
      bookSetCurBook(book, unload ? DOBOOK_UNLOAD : DOBOOK_GOTO);

theend:
    eeglFree(bufnrs);
    return ret;
}

//return FAIL if there is no file name, OK if there is one give error message for FAIL
pub int
check_fname(void) {
   if (curBook->fullFileName == NULL) {
      emsg(_(e_no_file_name));
      return FAIL;
   }
   return OK;
}

//Flush the contents of a book, unless it has no file name.
//Return FAIL for failure, NOTDONE for refusal, OK otherwise
pub int
bookWrite_all(Book* book, Boole forceit) {
   Book* curBookSaved = curBook;

   int retval = bookWrite(
      book, book->fullFileName, book->currFileName, (LineNr)1, book->mem.lineCount, NULL,
      false, forceit, true, false
   );
   if (retval == NOTDONE) {
      emsg(_(e_cannot_make_changes_modifiable_is_off));
   }
   if (curBook != curBookSaved) {
      msg_source(getDecoFlags(HLF_W));
      msg(_("Warning: Entered other buffer unexpectedly (check autocommands)"));
   }
   return retval;
}

//}}}
//{{{command array

private int quitmore = 0;
private int ex_pressedreturn = false;

private void append_command(CS cmd);

private void do_exbuffer(Invocation* invo);
private CS getargcmd(OUT CS*);
private int getargopt(Invocation* invo);

private LineNr default_address(Invocation* invo);
private void address_default_all(Invocation* invo);
private void get_flags(Invocation* invo);
#define HAVE_EX_SCRIPT_NI
private void   ex_script_ni(Invocation* invo);
private CS invalid_range(Invocation* invo);
private void   correct_range(Invocation* invo);
private CS replaceMakeProgramName(Invocation* invo, OUT CS p, OUT CS* commline);
private CS repl_commline(
      Invocation* invo, CS src, Unt srclen, CS repl, OUT CS* commline
);
private void   prepare_preview_window(void);
private void   back_to_current_window(Portal *curPor_save);
# define ex_syntime      c_ni
# define ex_loadkeymap   c_ni
private void   close_redir(void);
#define ex_diffoff       c_ni
#define ex_diffpatch     c_ni
#define ex_diffgetput    c_ni
#define ex_diffsplit     c_ni
#define ex_diffthis      c_ni
#define ex_diffupdate    c_ni


#define ex_profile       c_ni

//Declare the full commands table[].
#define DO_DECLARE_COMMANDS
#include "commands.h"
#undef DO_DECLARE_COMMANDS
#include "indices/commands.h"

private Byte dollar_command[2] = {'$', ZERO};

private void
saveDbgStuff(DebugStuff* dsp) {
   dsp->force_abort   = force_abort;      force_abort = false;
   dsp->caught_stack   = caught_stack;      caught_stack = NULL;

   //Necessary for debugging an inactive ":catch", ":finally", ":endtry"
   dsp->anyEmsgG     = anyEmsgG;      anyEmsgG     = false;
   dsp->gotInterruptG = gotInterruptG; gotInterruptG  = false;
   dsp->did_throw    = did_throw;      did_throw    = false;
   dsp->need_rethrow = need_rethrow;   need_rethrow = false;
   dsp->current_exception = current_exception;   current_exception = NULL;
}

private void
restore_DebugStuff(DebugStuff* dsp) {
   suppress_errthrow = false;
   force_abort = dsp->force_abort;
   caught_stack = dsp->caught_stack;
   anyEmsgG = dsp->anyEmsgG;
   gotInterruptG = dsp->gotInterruptG;
   did_throw = dsp->did_throw;
   need_rethrow = dsp->need_rethrow;
   current_exception = dsp->current_exception;
}

//Check if files are the same file.
//fnum is a buffer number. 0 == current buffer, 1-or-more must be a valid buffer ID.
//fullFName is a full path to where a buffer lives on-disk or would live on-disk.
private Boole
isSameFile(int fnum, CS fullFName) {
   if (fnum != 0) {
      if (fnum == curBook->fiNum)
         return true;
      return false;
   }

   if (!fullFName)
      return false;

   if (*fullFName == ZERO)
      return true;

   //TODO: Need a reliable way to know whether a buffer is meant to live
   //on-disk !curBook->isDevNumValid is not always available (example: missing
   //on Portals)
   if (curBook->shortFileName && *curBook->shortFileName != ZERO)
      //This occurs with unsaved buffers. In which case `fullFName` actually
      //corresponds to curBook->shortFileName
      return fnamecmp(fullFName, curBook->shortFileName) == 0;

   return fNameMatchesCurBook(fullFName);
}

//Print the executed command for when 'verbose' is set.
//When "lnum" is 0 only print the command.
private void
msg_verbose_cmd(LineNr lnum, CS cmd) {
   ++no_wait_return;
   verbose_enter_scroll();

   if (lnum == 0)
      smsg(_("Executing: %s"), cmd);
   else
      smsg(_("line %ld: %s"), (long)lnum, cmd);
   if (msg_silent == 0)
      msg_puts(S"\n");   //don't overwrite this

   verbose_leave_scroll();
   --no_wait_return;
}

//Execute a simple command line.  Used for translated commands like "*".
pub int
executeCommLine(CS cmd) {
   return doCommand(cmd, NULL, NULL, DOCMD_VERBOSE|DOCMD_NOWAIT|DOCMD_KEYTYPED);
}

//Execute the "+cmd" argument of "edit +cmd fname" and the like.
//This allows for using a range without ":" in Vim9 script.
private int
do_cmd_argument(CS cmd) {
   return doCommand(cmd, NULL, NULL, DOCMD_VERBOSE|DOCMD_NOWAIT|DOCMD_KEYTYPED);
}

//Handle when "did_throw" is set after executing commands.
pub void
handle_did_throw(void) {
   CS p = NULL;
   MsgList* messages = NULL;
   ESTACK_CHECK_DECLARATION;

   //If the uncaught exception is a user exception, report it as an error. If it is an error 
   //exception, display the saved error message now.  For an interrupt exception, do nothing; the
   //interrupt message is given elsewhere.
   switch (current_exception->type) {
   case ET_USER:
      eeSnprintf(IObuff, IOSIZE, _(e_exception_not_caught_str), current_exception->value);
      p = copyStr(IObuff);
      break;
   case ET_ERROR:
      messages = current_exception->messages;
      current_exception->messages = NULL;
      break;
   case ET_INTERRUPT:
      break;
   }

   estack_push(ETYPE_EXCEPT, current_exception->throw_name, current_exception->throw_lnum);
   ESTACK_CHECK_SETUP;
   current_exception->throw_name = NULL;

   discard_current_exception();   //uses IObuff if 'verbose'

   //If "silent!" is active the uncaught exception is not fatal.
   if (emsg_silent == 0) {
      suppress_errthrow = true;
      force_abort = true;
   }

   if (messages) {
      do {
         MsgList* next = messages->next;
         emsg(messages->msg);
         eeglFree(messages->msg);
         eeglFree(messages->sfile);
         eeglFree(messages);
         messages = next;
      }
      while (messages);
   } ei (p) {
      emsg(p);
      eeglFree(p);
   }
   eeglFree(SOURCING_NAME);
   ESTACK_CHECK_NOW;
   estack_pop();
}

//Get the next line source line without advancing.
pub CS
getline_peek(
   LineGetter fgetline,
   void* cookie      //argument for fgetline()
){
   //When "fgetline" is "get_loop_line()" use the "cookie" to find the
   //cookie that's originally used to obtain the lines.  This may be nested several levels.
   LineGetter gp = fgetline;
   if (gp == &scrGetSourceLine)
      return source_nextline(cookie);
   return NULL;
}


//Helper function to apply an offset for buffer commands, i.e. ":bdelete",
//":bwipeout", etc. Returns the book file number.
private int
compute_buffer_local_count(int addressKind, int lnum, int offset) {
   Book* nextBook;
   int count = offset;

   Book* book = firstBook;
   while (book->next && book->fiNum < lnum)
      book = book->next;
   while (count != 0) {
      count += (offset < 0) ? 1 : -1;
      nextBook = (offset < 0) ? book->prev : book->next;
      if (nextBook == NULL)
         break;
      book = nextBook;
      if (addressKind == ADDR_LOADED_BUFFERS)
         //skip over unloaded buffers
         while (bookNoMemfile(book)) {
            nextBook = (offset < 0) ? book->prev : book->next;
            if (!nextBook)
               break;
            book = nextBook;
         }
   }
   //we might have gone too far, last buffer is not loadedd
   if (addressKind == ADDR_LOADED_BUFFERS) {
      while (bookNoMemfile(book)) {
         nextBook = (offset >= 0) ? book->prev : book->next;
         if (nextBook == NULL)
            break;
         book = nextBook;
      }
   }
   return book->fiNum;
}

//Return the portal number of "portal". When "portal" is NULL, return the number of portal.
private int
getPortNr(Portal* portal) {
   int      nr = 0;
   Portal   *port;
   FOR_ALL_PORTALS(port) {
      ++nr;
      if (port == portal)
         break;
   }
   return nr;
}

private int
current_tab_nr(Tab *tab) {
   int nr = 0;
   Tab   *t;
   FOR_ALL_TABS(t) {
      ++nr;
      if (t == tab)
         break;
   }
   return nr;
}

#define GET_PORT_NR getPortNr(curPor)
#define LAST_WIN_NR getPortNr(NULL)
#define CURRENT_TAB_NR current_tab_nr(curtab)
#define LAST_TAB_NR current_tab_nr(NULL)

//}}}
//{{{command execution

//doCommand(): execute one command line
//
//1. Execute "commline" when it is not NULL.
//   Otherwise, or if more lines are needed, fgetline() is used.
//2. Split up in parts separated with '|'.
//
//This function can be called recursively!
//
//flags:
//DOCMD_VERBOSE  - The command will be included in the error message.
//DOCMD_NOWAIT   - Don't call wait_return() and friends.
//DOCMD_REPEAT   - Repeat execution until fgetline() returns NULL.
//DOCMD_KEYTYPED - Don't reset keyWasTypedG.
//DOCMD_EXCRESET - Reset the exception environment (used for debugging).
//DOCMD_KEEPLINE - Store first typed line (for repeating with ".").
//
//return FAIL if commline could not be executed, OK otherwise
pub int
doCommand(
   CS commline,
   LineGetter fgetline,
   void* cookie,      //argument for fgetline()
   Unt flags
){
   CS commlineCopy = NULL;   //copy of cmd line
   int used_getline = false;   //used "fgetline" to obtain command
   static int recursive = 0;      //recursive depth
   int msg_didout_before_start = 0;
   int count = 0;      //line number count
   Boole did_inc_isRedrawingDisabledG = false;
   int retval = OK;
   DebugStuff debug_saved;   //saved things for debug mode
   MsgList** saved_msg_list = NULL;
   MsgList* private_msg_list = NULL;

   //"fgetline" and "cookie" passed to doOneCommand()
   LineGetter commGetLine;
   void* commCookie;
   //For every pair of doCommand()/doOneCommand() calls, use an extra memory location for storing 
   //error messages to be converted to an exception. This ensures that the do_errthrow() call in 
   //doOneCommand() does not combine the messages stored by an earlier invocation of doOneCommand()
   //with the command name of the later one. This would happen when
   //BufWritePost autocommands are executed after a write error.
   saved_msg_list = msg_list;
   msg_list = &private_msg_list;

   //Initialize "force_abort"  and "suppress_errthrow" at the top level.
   if (!recursive) {
      force_abort = false;
      suppress_errthrow = false;
   }

   //If requested, store and reset the global values controlling the
   //exception handling (used when debugging). Otherwise clear it to avoid
   //a bogus compiler warning when the optimizer uses inline functions...
   if (flags & DOCMD_EXCRESET)
      saveDbgStuff(&debug_saved);
   else
      CLEAR_FIELD(debug_saved);

   //"did_throw" will be set to true if an exception will be thrown
   did_throw = false;
   //"anyEmsgG" will be set to true when emsg() is used, in which case we cancel the whole command 
   //line, and any if/endif or loop. If force_abort is set, we cancel everything.
   anyEmsgG = false;

   //keyWasTypedG is only set when calling vgetc(). Reset it here when not calling vgetc() 
   //(sourced command lines).
   if ((flags & DOCMD_KEYTYPED) == 0 && fgetline != &scrGetTypedCommand)
      keyWasTypedG = false;

   //Continue executing command lines: when repeating until there are no more lines (for ":source")
   CS nextCommline = commline;
   do {
      //stop skipping cmds for an error msg after all endif/while/for
      if (!nextCommline && !force_abort ) {
         anyEmsgG = false;
      }

      //2. If no line given, get an allocated line with fgetline().
      if (!nextCommline) {
         //Need to set msg_didout for the first line after an ":if",
         //otherwise the ":if" will be overwritten.
         if (count == 1 && fgetline ==  &scrGetTypedCommand)
            msg_didout = true;
         if (!fgetline || (nextCommline = fgetline(':', cookie,  0, GETLINE_CONCAT_CONT)) == NULL) {
            //Don't call wait_return() for aborted command line. The NULL
            //returned for the end of a sourced file or executed function doesn't do this.
            if (keyWasTypedG && (flags & DOCMD_REPEAT) == 0)
               need_wait_return = false;
            retval = FAIL;
            break;
         }
         used_getline = true;

         //Keep the first typed line. Clear it when more lines are typed.
         if ((flags & DOCMD_KEEPLINE) != 0) {
            eeglFree(repeatCommlineG);
            if (count == 0)
               repeatCommlineG = copyStr(nextCommline);
            else
               repeatCommlineG = NULL;
         }
      }
      //3. Make a copy of the command so we can mess with it.
      ei (!commlineCopy) {
         nextCommline = copyStr(nextCommline);
      }
      commlineCopy = nextCommline;

      commGetLine = fgetline;
      commCookie = cookie;

      if (count++ == 0) {
         //All output from the commands is put below each other, without waiting for a return. 
         //Don't do this when executing commands from a script or when being called recursive 
         //(e.g. for ":e +command file").
         if ((flags & DOCMD_NOWAIT) == 0 && !recursive) {
            msg_didout_before_start = msg_didout;
            msg_didany = false; //no output yet
            msg_start();
            msg_scroll = true;  //put messages below each other
            ++no_wait_return;   //don't wait for return until finished
            ++isRedrawingDisabledG;
            did_inc_isRedrawingDisabledG = true;
         }
      }

      if ((p_verbose == 15 && SOURCING_NAME) || p_verbose >= 16)
         msg_verbose_cmd(SOURCING_LNUM, commlineCopy);

      //2. Execute one command.
      //  "commlineCopy" can change, e.g. for '%' and '#' expansion.
      ++recursive;
      doOneCommand(OUT &commlineCopy, flags, commGetLine, commCookie);
      nextCommline = null;
      --recursive;

      EE_CLEAR(commlineCopy);

      //If the command was typed, remember it for the ':' register.
      //Do this AFTER executing the command to make :@: work.
      if (fgetline == &scrGetTypedCommand && newLastCommlineG) {
         eeglFreeString(lastCommlineG);
         lastCommlineG = newLastCommlineG;
         newLastCommlineG = NULL;
      }

      //If the outermost try conditional (across function calls and sourced
      //files) is aborted because of an error, an interrupt, or an uncaught
      //exception, cancel everything.  If it is left normally, reset
      //force_abort to get the non-EH compatible abortion behavior for the rest of the script.
      if (!anyEmsgG && !gotInterruptG && !did_throw)
         force_abort = false;
   }
   //Continue executing command lines when:
   //- no CTRL-C typed, no aborting error, no exception thrown or try conditionals need to be 
   //checked for executing finally clauses or catching an interrupt exception
   //- didn't get an error message or lines are not typed
   //- looping for ":source" command.
   while (!gotInterruptG && (!anyEmsgG || !force_abort) && !did_throw
       && (!anyEmsgG || !used_getline || fgetline != &scrGetTypedCommand)
       && (nextCommline || (flags & DOCMD_REPEAT) != 0)
   ); //do while

   eeglFree(commlineCopy);
   anySyntaxEmsgS = false;

   //When an exception is being thrown out of the outermost try conditional, discard the 
   //uncaught exception, disable the conversion of interrupts or errors to exceptions, and 
   //ensure that no more commands are executed.
   if (did_throw)
      handle_did_throw();
   //On an interrupt or an aborting error not converted to an exception, disable the conversion 
   //of errors to exceptions. (Interrupts are not converted anymore, here.) This enables also 
   //the interrupt message when force_abort is set and anyEmsgG unset in case of an interrupt
   //from a finally clause after an error.
   ei (gotInterruptG || (anyEmsgG && force_abort))
      suppress_errthrow = true;

   if (fgetline == &scrGetSourceLine) {
   } else {
      //Go to debug mode when returning from a function in which we are single-stepping.
      if (fgetline ==  &scrGetSourceLine)
         do_debug(fgetline ==  &scrGetSourceLine
             ? (CS)_("End of sourced file")
             : (CS)_("End of function")
         );
   }

   //Restore the exception environment (done after returning from the debugger).
   if ((flags & DOCMD_EXCRESET) != 0)
      restore_DebugStuff(&debug_saved);

   msg_list = saved_msg_list;

   //If there was too much output to fit on the command line, ask the user to
   //hit return before redrawing the screen. With the ":global" command we do
   //this only once after the command is finished.
   if (did_inc_isRedrawingDisabledG) {
      if (isRedrawingDisabledG > 0)
         --isRedrawingDisabledG;
      --no_wait_return;
      msg_scroll = false;

      //When error, no need to wait for hit-return. Also for an error situation.
      if (retval == FAIL ) {
         need_wait_return = false;
         msg_didany = false;      //don't wait when restarting edit
      } ei (need_wait_return) {
         //The msg_start() above clears msg_didout. The wait_return() we do
         //here should not overwrite the command that may be shown before doing that.
         msg_didout |= msg_didout_before_start;
         wait_return(false);
      }
   }

   return retval;
}

//Execute one Command.
//
//If "flags" has DOCMD_VERBOSE, the command will be included in the error message.
//
//1. skip comment lines and leading space
//2. handle command modifiers
//3. find the command
//4. parse range
//5. Parse the command.
//6. parse arguments
//7. switch on command name
//
//Note: "fgetline" can be NULL.
//
//This function may be called recursively!
private void
doOneCommand(
   OUT CS* commline,
   Unt flags,
   LineGetter fgetline,
   void* cookie      //argument for fgetline()
){
   Invocation invo;         //command arguments
   CLEAR_FIELD(invo);
   invo.line1 = 1;
   invo.line2 = 1;
   //When the last file has not been edited :q has to be typed twice.
   if (quitmore
       //avoid that an autocommand, e.g. QuitPre, does this
       && fgetline != &getnextac
   ) {
      --quitmore;
   } 

   //Reset browse, confirm, etc..  They are restored when returning, for recursive calls.
   CommandModifier saveCommModifier = commModifierG;
   CS errorMsg = null;
   Boole did_set_expr_line = false;
   //"#!anything" is handled like a comment.
   if ((*commline)[0] == '#' && (*commline)[1] == '!')
      goto doend;
   if (isComment(*commline)) {
      *commline = skipLine(*commline);
      if ((*commline)[0] == ZERO)
         *commline = null;
      goto doend; 
   }

   int save_reg_executing = reg_executing;
   int save_pending_end_reg_executing = pending_end_reg_executing;
   LineNr lnum;
   Long n;
   Unt sourcing = flags & DOCMD_VERBOSE;
   
   //1. Skip comment lines and leading white space and colons.
   //2. Handle command modifiers.
   //The "invo" structure holds the arguments that can be used.
   invo.comm = *commline;
   invo.commline = commline;
   invo.ea_getline = fgetline;
   invo.cookie = cookie;
   if (parse_command_modifiers(&invo, OUT &errorMsg, &commModifierG, false) == FAIL)
      goto doend;
   applyCommModifiers(&commModifierG);
   CS after_modifier = invo.comm;

   invo.skip = anyEmsgG || gotInterruptG || did_throw;

   //3. Skip over the range to find the command.  Let "p" point to after it.
   //
   //We need the command to know what kind of range it uses.
   CS cmd = invo.comm;
   
   Boole may_have_range = true;
   if (may_have_range)
      invo.comm = skip_range(invo.comm, true, NULL);

   CS p = findCommand(&invo, NULL, NULL);

   invo.comm = cmd;

   //May go to debug mode.  If this happens and the ">quit" debug command is
   //used, throw an interrupt exception and skip the next command.
   dbg_check_breakpoint(&invo);
   if (!invo.skip && gotInterruptG) {
      invo.skip = true;
   }

   //4. parse a range specifier of the form: addr [,addr] [;addr] ..
   //
   //where 'addr' is:
   //
   //%         (entire file)
   //$  [+-NUM]
   //'x [+-NUM] (where x denotes a currently defined mark)
   //.  [+-NUM]
   //[+-NUM]..
   //NUM
   //
   //The invo.comm pointer is updated to point to the first character following the range spec. 
   //If an initial address is found, but no second, the upper bound is equal to the lower.

   //invo.addressKind for user commands is set by find_ucmd
   if (!IS_USER_COMMAND(invo.id)) {
      if (invo.id != COUNT_COMMANDS)
         invo.addressKind = commands[(int)invo.id].addressKind;
      else
         invo.addressKind = ADDR_LINES;

      //:wincmd range depends on the argument.
      if (invo.id == C_wincmd && p)
         getPortCommAddressType(skipwhite(p), &invo);
      if (invo.id == C_ll && isLocationListBook(curBook))
         invo.addressKind = ADDR_OTHER;
   }

   if (!may_have_range)
      invo.line1 = invo.line2 = default_address(&invo);
   ei (parse_cmd_address(&invo, OUT &errorMsg, false) == FAIL)
      goto doend;

   //5. Parse the command.
   //Skip ':' and any white space
   invo.comm = skipwhite(invo.comm);
   while (*invo.comm == ':')
      invo.comm = skipwhite(invo.comm + 1);

   //If we got a line, but no command, then go to the line.
   if (*invo.comm == ZERO || isComment(invo.comm)) {
      if (invo.skip)       //skip this if inside :if
         goto doend;
      errorMsg = ex_range_without_command(&invo);
      goto doend;
   }

   //If this looks like an undefined user command and there are CmdUndefined
   //autocommands defined, trigger the matching autocommands.
   if (p && invo.id == COUNT_COMMANDS && !invo.skip
       && ASCII_ISUPPER(*invo.comm)
       && has_cmdundefined()
   ) {
      int ret;

      p = invo.comm;
      while (ASCII_ISALNUM(*p))
         ++p;
      p = copySubstr(invo.comm, p - invo.comm);
      ret = applyAutocomms(EVENT_CMDUNDEFINED, p, p, true, NULL);
      eeglFree(p);
      //If the autocommands did something and didn't cause an error, try finding the command again
      p = (ret && !aborting()) ? findCommand(&invo, NULL, NULL) : invo.comm;
   }

   if (!p) {
      if (!invo.skip)
         errorMsg = _(e_ambiguous_use_of_user_defined_command);
      goto doend;
   }
   //Check for wrong commands.
   if (*p == '!' && invo.comm[1] == 0151 && invo.comm[0] == 78 && !IS_USER_COMMAND(invo.id)) {
      errorMsg = uc_fun_cmd();
      goto doend;
   }

   Boole did_append_cmd = false;
   if (invo.id == COUNT_COMMANDS) {
      if (!invo.skip) {
         STRCPY(IObuff, _(e_not_an_editor_command));
         if (!sourcing) {
            //If the modifier was parsed OK the error must be in the following command
            if (after_modifier)
               append_command(after_modifier);
            else
               append_command(*commline);
            did_append_cmd = true;
         }
         errorMsg = IObuff;
         anySyntaxEmsgS = true;
      }
      goto doend;
   }

   int ni = (!IS_USER_COMMAND(invo.id) && (commands[invo.id].fn == c_ni
#ifdef HAVE_EX_SCRIPT_NI
        || commands[invo.id].fn == ex_script_ni
#endif
        ));//set when Not Implemented

   //forced commands
   if (*p == '!' && invo.id != C_substitute) {
      ++p;
      invo.forceit = true;
   } else
      invo.forceit = false;

   //6. Parse arguments.  Then check for errors.
   if (!IS_USER_COMMAND(invo.id))
      invo.argFlags = (long)commands[(int)invo.id].flags;

   if (!invo.skip) {
      if ((IMMUTABLE) && (invo.argFlags & MODIFY) != 0) {
          //Command not allowed in immutable buffers
          errorMsg = _(e_cannot_make_changes_modifiable_is_off);
          goto doend;
      }

      if (!IS_USER_COMMAND(invo.id)) {
         if (commPortTypeG != 0 && !(invo.argFlags & COMMPORT)) {
            //Command not allowed in the command line portal
            errorMsg = _(e_invalid_in_commline_portal);
            goto doend;
         }
         if (text_locked() && !(invo.argFlags & LOCK_OK)) {
            //Command not allowed when text is locked
            errorMsg = _(get_text_locked_msg());
            goto doend;
         }
      }

      //Disallow editing another buffer when "curBookLock" is set.
      //Do allow ":checktime" (it is postponed).
      //Do allow ":edit" (check for an argument later).
      //Do allow ":file" with no arguments (check for an argument later).
      if (!(invo.argFlags & (COMMPORT | LOCK_OK))
            && invo.id != C_checktime
            && invo.id != C_edit
            && invo.id != C_file
            && !IS_USER_COMMAND(invo.id)
            && curBookLocked()
      )
         goto doend;

      if (!ni && !(invo.argFlags & RANGE) && invo.addr_count > 0) {
          errorMsg = _(e_no_range_allowed);
          goto doend;
      }
   }

   if (!ni && !(invo.argFlags & BANG) && invo.forceit) {
      errorMsg = _(e_no_bang_allowed);
      goto doend;
   }

   //Don't complain about the range if it is not used
   //(could happen if line_count is accidentally set to 0).
   if (!invo.skip && !ni && (invo.argFlags & RANGE)) {
      //If the range is backwards, ask for confirmation and, if given, swap
      //invo.line1 & invo.line2 so it's forwards again.
      //When global command is busy, don't ask, will fail below.
      if (!global_busy && invo.line1 > invo.line2) {
         if (msg_silent == 0) {
            if (sourcing) {
               errorMsg = _(e_backwards_range_given);
               goto doend;
            }
            if (ask_yesno((CS)_("Backwards range given, OK to swap"), false) != 'y')
               goto doend;
         }
         lnum = invo.line1;
         invo.line1 = invo.line2;
         invo.line2 = lnum;
      }
      if ((errorMsg = invalid_range(&invo)) != NULL)
         goto doend;
   }

   if ((invo.addressKind == ADDR_OTHER) && invo.addr_count == 0)
      //default is 1, not cursor
      invo.line2 = 1;

   correct_range(&invo);

   if (((invo.argFlags & WHOLEFOLD) || invo.addr_count >= 2) && !global_busy
          && invo.addressKind == ADDR_LINES) {
      //Put the first line at the start of a closed fold, put the last line
      //at the end of a closed fold.
      (void)getFolds(invo.line1, OUT &invo.line1, NULL);
      (void)getFolds(invo.line2, NULL, OUT &invo.line2);
   }

   //For the ":make" and ":grep" commands we insert the 'makeprg'/'grepprg'
   //option here, so things like % get expanded.
   p = replaceMakeProgramName(&invo, OUT p, commline);
   if (!p)
      goto doend;

   //Skip to start of argument. Don't do this for the ":!" command, because ":!! -l" needs the space
   if (invo.id == C_bang)
      invo.arg = p;
   else
      invo.arg = skipwhite(p);

   //":file" cannot be run with an argument when "curBookLock" is set
   if (invo.id == C_file && *invo.arg != ZERO && curBookLocked())
      goto doend;

   //Check for "++opt=val" argument. Must be first, allow ":w ++enc=utf8 !cmd"
   if (invo.argFlags & ARGOPT) {
      while (invo.arg[0] == '+' && invo.arg[1] == '+') {
         if (getargopt(&invo) == FAIL && !ni) {
            errorMsg = _(e_invalid_argument);
            goto doend;
         }
      } 
   } 

   if (invo.id == C_write || invo.id == C_update) {
      if (*invo.arg == '>') {        //append
         if (*++invo.arg != '>') {     //typed wrong
            errorMsg = _(e_use_w_or_w_gt_gt);
            goto doend;
         }
         invo.arg = skipwhite(invo.arg + 1);
         invo.append = true;
      } ei (*invo.arg == '!' && invo.id == C_write) { //:w !filter
         ++invo.arg;
         invo.usefilter = true;
      }
   }

   if (invo.id == C_read) {
      if (invo.forceit) {
         invo.usefilter = true;      //:r! filter if invo.forceit
         invo.forceit = false;
      } ei (*invo.arg == '!') {     //:r !filter
         ++invo.arg;
         invo.usefilter = true;
      }
   }

   if (invo.id == C_lshift || invo.id == C_rshift) {
      invo.amount = 1;
      while (*invo.arg == *invo.comm) {     //count number of '>' or '<'
          ++invo.arg;
          ++invo.amount;
      }
      invo.arg = skipwhite(invo.arg);
   }

   //Check for "+command" argument, before checking for next command.
   //Don't do this for ":read !cmd" and ":write !cmd".
   if ((invo.argFlags & CMDARG) && !invo.usefilter)
      invo.higherOrderComm = getargcmd(OUT &invo.arg);

   //For commands that do not use '|' inside their argument: Check for '|' to
   //separate commands and '//' to start comments.
   //
   //Otherwise: Check for <newline> to end a shell command.
   //Also do this for ":read !cmd", ":write !cmd" and ":global".
   //Also do this inside a { - } block after :command and :autocmd.
   //Any others?
   if ((invo.argFlags & TRLBAR) && !invo.usefilter) {
      separateNextCommand(&invo, false);
   } ei ( invo.id == C_bang
       || invo.id == C_terminal
       || invo.id == C_global
       || invo.id == C_vglobal
       || invo.usefilter
    ) {
      for (p = invo.arg; *p; ++p) {
          //Remove one backslash before a newline
         if (*p == '\\' && p[1] == '\n')
            STRMOVE(p, p + 1);
         ei (*p == '\n' && (invo.argFlags & EXPR_ARG) == 0) {
            *p = ZERO;
            break;
         }
      }
   }

   if ((invo.argFlags & DFLALL) && invo.addr_count == 0)
      address_default_all(&invo);

   //accept numbered register only when no count allowed (:put)
   if ((invo.argFlags & REGSTR)
          && *invo.arg != ZERO
             //Do not allow register = for user commands
          && (!IS_USER_COMMAND(invo.id) || *invo.arg != '=')
          && !((invo.argFlags & COUNT) && EE_ISDIGIT(*invo.arg))
   ) {
      if (valid_yank_reg(*invo.arg, (!IS_USER_COMMAND(invo.id)
                && invo.id != C_put && invo.id != C_iput))) {
         invo.regname = *invo.arg++;
         //for '=' register: accept the rest of the line as an expression
         if (invo.arg[-1] == '=' && invo.arg[0] != ZERO) {
            if (!invo.skip) {
               set_expr_line(copyStr(invo.arg), &invo);
               did_set_expr_line = true;
            }
            invo.arg += STRLEN(invo.arg);
         }
         invo.arg = skipwhite(invo.arg);
      }
   }

   //Check for a count.  When accepting a BUFNAME, don't use "123foo" as a
   //count, it's a buffer name.
   if ((invo.argFlags & COUNT) && EE_ISDIGIT(*invo.arg)
       && (!(invo.argFlags & BUFNAME) || *(p = skipdigits(invo.arg + 1)) == ZERO
                       || SPACE_OR_TAB(*p))) {
      n = parseLong_quoted(&invo.arg);
      invo.arg = skipwhite(invo.arg);
      if (n <= 0 && !ni && (invo.argFlags & ZERO_LINE_OK) == 0) {
          errorMsg = _(e_positive_count_required);
          goto doend;
      }
      if (invo.addressKind != ADDR_LINES) {  //e.g. :buffer 2, :sleep 3
          invo.line2 = n;
          if (invo.addr_count == 0)
         invo.addr_count = 1;
      } else {
         invo.line1 = invo.line2;
         if (invo.line2 >= (Long)LONG_MAX - (n - 1))
            invo.line2 = (Long)LONG_MAX;  //avoid overflow
         else
            invo.line2 += n - 1;
         ++invo.addr_count;
         if (invo.line2 > curBook->mem.lineCount) {
            showErrFmtMsg(
               e_line_number_out_of_range_nr_past_the_end, invo.line2 - curBook->mem.lineCount
            );
            invo.line2 = curBook->mem.lineCount;
         } 
      }
   }

   //Check for flags: 'l', 'p' and '#'.
   if ((invo.argFlags & FLAGS) != 0)
      get_flags(&invo);
      
   if (!ni && !(invo.argFlags & EXTRA) && *invo.arg != ZERO
        && !isComment(invo.arg) && (*invo.arg != '|' || (invo.argFlags & TRLBAR) == 0)
   ) {
      //no arguments allowed but there is something
      errorMsg = ex_errmsg(e_trailing_characters_str, invo.arg);
      goto doend;
   }

   if (!ni && (invo.argFlags & NEEDARG) && *invo.arg == ZERO) {
      errorMsg = _(e_argument_required);
      goto doend;
   }
//{{{ Skip
   //Skip the command when it's not going to be executed.
   //The commands like :if, :endif, etc. always need to be executed.
   //Also make an exception for commands that handle a trailing command themselves.
   if (invo.skip) {
      switch (invo.id) {
      //Commands that handle '|' themselves.  Check: A command should
      //either have the TRLBAR flag, appear in this list or appear in
      //the list at ":help :bar".
      case C_aboveleft:
      case C_and:
      case C_belowright:
      case C_botright:
      case C_browse:
      case C_call:
      case C_confirm:
      case C_const:
      case C_delfunction:
      case C_djump:
      case C_dlist:
      case C_dsearch:
      case C_dsplit:
      case C_echo:
      case C_echoerr:
      case C_echomsg:
      case C_echon:
      case C_eval:
      case C_execute:
      case C_filter:
      case C_final:
      case C_help:
      case C_hide:
      case C_horizontal:
      case C_ijump:
      case C_ilist:
      case C_isearch:
      case C_isplit:
      case C_keepalt:
      case C_keepjumps:
      case C_keepmarks:
      case C_keeppatterns:
      case C_leftabove:
      case C_let:
      case C_lockmarks:
      case C_lockvar:
      case C_match:
      case C_noautocmd:
      case C_noswapfile:
      case C_psearch:
      case C_rightbelow:
      case C_silent:
      case C_substitute:
      case C_syntax:
      case C_tab:
      case C_tilde:
      case C_topleft:
      case C_unlet:
      case C_unlockvar:
      case C_verbose:
      case C_vertical:
      case C_wincmd:
         break;

      default:      goto doend;
      }
    }
//}}}
   if ((invo.argFlags & XFILE) && expand_filename(&invo, OUT commline, OUT &errorMsg) == FAIL)
      goto doend;

   //Accept book name. Cannot be used at the same time with a book number. Don't do this for 
   //a user command.
   if ((invo.argFlags & BUFNAME) && *invo.arg != ZERO && invo.addr_count == 0
          && !IS_USER_COMMAND(invo.id)) {
      //:bdelete, :bwipeout and :bunload take several arguments, separated
      //by spaces: find next space (skipping over escaped characters).
      //The others take one argument: ignore trailing spaces.
      if (invo.id == C_bdelete || invo.id == C_bwipeout || invo.id == C_bunload)
         p = skiptowhite_esc(invo.arg);
      else {
         p = invo.arg + STRLEN(invo.arg);
         while (p > invo.arg && SPACE_OR_TAB(p[-1]))
            --p;
      }
      invo.line2 = booklistFindPattern(invo.arg, p, (invo.argFlags & BUFUNL) != 0,
                           false, false);
      if (invo.line2 < 0)       //failed
          goto doend;
      invo.addr_count = 1;
      invo.arg = skipwhite(p);
   }

   //7. Execute the command.
   if (IS_USER_COMMAND(invo.id)) {
      //Execute a user-defined command.
      do_ucmd(&invo);
   } else {
      //Call the function to execute the builtin command.
      (commands[invo.id].fn)(&invo);
      if (invo.errmsg)
         errorMsg = invo.errmsg;
   }


doend:
   if (curPor->cursor.lnum == 0) {  //can happen with zero line number
      curPor->cursor.lnum = 1;
      curPor->cursor.col = 0;
   }

   if (errorMsg && *errorMsg != ZERO && !anyEmsgG) {
      if ((sourcing || !keyWasTypedG) && !did_append_cmd) {
          if (errorMsg != IObuff) {
             STRCPY(IObuff, errorMsg);
             errorMsg = IObuff;
          }
          append_command(*commline);
      }
      emsg(errorMsg);
   }

   if (did_set_expr_line)
      set_expr_line(NULL, NULL);

   undoCommModifier(&commModifierG);
   commModifierG = saveCommModifier;
   reg_executing = save_reg_executing;
   pending_end_reg_executing = save_pending_end_reg_executing;

   eeglFree(invo.commlineToFree);
}

//}}}
//{{{command parsin'

private Byte ex_error_buf[MSG_BUF_LEN];

//Return an error message with argument included. Use a static buffer, only the last error will be 
//kept. "msg" will be translated, caller should use N_().
pub CS
ex_errmsg(CS msg, CS arg) {
   eeSnprintf(ex_error_buf, MSG_BUF_LEN, _(msg), arg);
   return ex_error_buf;
}

//The "+" string used in place of an empty command in Command mode.
//This string is used in pointer comparison.
private char exmode_plus[] = "+";

//Handle a range without a command. Returns an error message on failure.
pub CS
ex_range_without_command(Invocation* invo) {
   CS errorMsg = NULL;

   if (*invo->comm == '|') {
      invo->id = C_print;
      invo->argFlags = RANGE+COUNT+TRLBAR;
      if ((errorMsg = invalid_range(invo)) == NULL) {
          correct_range(invo);
          c_print(invo);
      }
   } ei (invo->addr_count != 0) {
      if (invo->line2 > curBook->mem.lineCount) {
          //A line number past the file is put at the end of the file.
         invo->line2 = curBook->mem.lineCount;
      }

      if (invo->line2 < 0)
          errorMsg = _(e_invalid_range);
      else {
          if (invo->line2 == 0)
         curPor->cursor.lnum = 1;
          else
         curPor->cursor.lnum = invo->line2;
          beginline(BL_SOL | BL_FIX);
      }
   }
   return errorMsg;
}

//Check for a command with optional tail.
//If there is a match advance "pp" to the argument and return true.
//If "noparen" is true do not recognize the command followed by "(" or ".".
private int
checkforcmd_opt(
   OUT CS* pp,      //start of command
   CS cmd,      //name of command
   int len,      //required length
   int noparen
) {
   int i;
   for (i = 0; cmd[i] != ZERO; ++i) {
      if (((CS)cmd)[i] != (*pp)[i])
         break;
   } 
   if (i >= len && !ASCII_ISALPHA((*pp)[i]) && (*pp)[i] != '_'
          && (!noparen || ((*pp)[i] != '(' && (*pp)[i] != '.'))) {
      *pp = skipwhite(*pp + i);
      return true;
   }
   return false;
}

//Check for a command with optional tail.
//If there is a match advance "pp" to the argument and return true.
pub int
checkforcmd(
   OUT CS* pp,      //start of command
   CS cmd,      //name of command
   int      len
) {      //required length
   return checkforcmd_opt(OUT pp, cmd, len, false);
}

//Check for a command with optional tail, not followed by "(" or ".".
//If there is a match advance "pp" to the argument and return true.
pub int
checkforcmd_noparen(
    OUT CS* pp,      //start of command
    CS cmd,      //name of command
    int len      //required length
){
   return checkforcmd_opt(pp, cmd, len, true);
}

//Parse and skip over command modifiers:
//- update invo->comm
//- store flags in "cmod".
//- Set ex_pressedreturn for an empty command line.
//When "skip_only" is true the global variables are not changed, except for "commModifierG".
//When "skip_only" is false then undoCommModifier() must be called later to free any 
//cmod_filter_regmatch.regprog.
//Call applyCommModifiers() to get the side effects of the modifiers:
//- set p_verbose for ":verbose"
//- set msg_silent for ":silent"
//- set 'eventignore' to "all" for ":noautocmd"
//Return FAIL when the command is not to be executed. May set "errorMsg" to an error message.
pub int
parse_command_modifiers(
   Invocation* invo,
   OUT CS* errorMsg,
   CommandModifier* cmod,
   int skip_only
){
   CS orig_cmd = invo->comm;
   CS cmd_start = NULL;
   int use_plus_cmd = false;
   int has_visual_range = false;

   CLEAR_POINTER(cmod);
   cmod->cmod_flags = stickyCommandModifiersG;

   if (STRNCMP(invo->comm, "'<,'>", 5) == 0) {
      //The automatically inserted Visual area range is skipped, so that
      //typing ":commModifierG cmd" in Visual mode works without having to move the
      //range to after the modifiers. The command will be "'<,'>commModifierG cmd",
      //parse "commModifierG cmd" and then put back "'<,'>" before "cmd" below.
      invo->comm += 5;
      cmd_start = invo->comm;
      has_visual_range = true;
   }

   //Repeat until no more command modifiers are found.
   for (;;) {
      while (*invo->comm == ' ' || *invo->comm == '\t' || *invo->comm == ':') {
         ++invo->comm;
      }

      //ignore comment and empty lines
      if (isComment(invo->comm)) {
         //a comment ends at a NL
         return FAIL;
      }
      if (*invo->comm == '\n') {
         return FAIL;
      }
      if (*invo->comm == ZERO) {
         if (!skip_only) {
            ex_pressedreturn = true;
         }
         return FAIL;
      }

      CS p = skip_range(invo->comm, true, NULL);

      switch (*p) {
      //When adding an entry, also modify modeInfoTable[].
      case 'a':   
         if (!checkforcmd_noparen(&invo->comm, S"aboveleft", 3))
           break;
        cmod->cmod_split |= WSP_ABOVE;
        continue;

      case 'b':
         if (checkforcmd_noparen(&invo->comm, S"belowright", 3)) {
            cmod->cmod_split |= WSP_BELOW;
            continue;
         }
         if (checkforcmd_opt(OUT &invo->comm, S"browse", 3, true)) {
            cmod->cmod_flags |= CMOD_BROWSE;
            continue;
         }
         if (!checkforcmd_noparen(&invo->comm, S"botright", 2))
            break;
         cmod->cmod_split |= WSP_BOT;
         continue;

      case 'c':   
        if (!checkforcmd_opt(OUT &invo->comm, S"confirm", 4, true))
           break;
        cmod->cmod_flags |= CMOD_CONFIRM;
        continue;

      case 'k':   
        if (checkforcmd_noparen(&invo->comm, S"keepmarks", 3)) {
           cmod->cmod_flags |= CMOD_KEEPMARKS;
           continue;
        }
        if (checkforcmd_noparen(&invo->comm, S"keepalt", 5)) {
           cmod->cmod_flags |= CMOD_KEEPALT;
           continue;
        }
        if (checkforcmd_noparen(&invo->comm, S"keeppatterns", 5)) {
           cmod->cmod_flags |= CMOD_KEEPPATTERNS;
           continue;
        }
        if (!checkforcmd_noparen(&invo->comm, S"keepjumps", 5))
           break;
        cmod->cmod_flags |= CMOD_KEEPJUMPS;
        continue;

      case 'f': {   //only accept ":filter {pat} cmd"
         CS reg_pat;
         CS nulp = NULL;
         int       c = 0;

         if (!checkforcmd_noparen(&p, S"filter", 4)
            || *p == ZERO
            || endsComm(p)
         )
            //in ":filter #pat# cmd" # does not start a comment
            break;
         if (*p == '!') {
            cmod->cmod_filter_force = true;
            p = skipwhite(p + 1);
            if (*p == ZERO || endsComm(p))
               break;
         }
         if (skip_only)
            p = skipEeglGrepPat(p, NULL, NULL);
         else
            //NOTE: This puts a ZERO after the pattern.
            p = skipEeglGrepPat_ext(p, &reg_pat, NULL, &nulp, &c);
         if (p == NULL || *p == ZERO)
            break;
         if (!skip_only) {
            cmod->cmod_filter_regmatch.regprog = compileRegexp(reg_pat, RE_MAGIC);
            if (cmod->cmod_filter_regmatch.regprog == NULL)
               break;
            //restore the character overwritten by ZERO
            if (nulp)
               *nulp = c;
         }
         invo->comm = p;
         continue;
      }

      case 'h':   
         if (checkforcmd_noparen(&invo->comm, S"horizontal", 3)) {
            cmod->cmod_split |= WSP_HOR;
            continue;
         }
         //":hide" and ":hide | cmd" are not modifiers
         if (p != invo->comm || !checkforcmd_noparen(&p, S"hide", 3)
                     || *p == ZERO || endsComm(p))
            break;
         invo->comm = p;
         cmod->cmod_flags |= CMOD_HIDE;
         continue;

      case 'l':   
         if (checkforcmd_noparen(&invo->comm, S"lockmarks", 3)) {
            cmod->cmod_flags |= CMOD_LOCKMARKS;
            continue;
         }

        if (!checkforcmd_noparen(&invo->comm, S"leftabove", 5))
           break;
        cmod->cmod_split |= WSP_ABOVE;
        continue;

      case 'n':   
        if (checkforcmd_noparen(&invo->comm, S"noautocmd", 3)) {
           cmod->cmod_flags |= CMOD_NOAUTOCMD;
           continue;
        }
        if (!checkforcmd_noparen(&invo->comm, S"noswapfile", 3))
           break;
        cmod->cmod_flags |= CMOD_NOSWAPFILE;
        continue;

      case 'r':   
        if (!checkforcmd_noparen(&invo->comm, S"rightbelow", 6))
           break;
        cmod->cmod_split |= WSP_BELOW;
        continue;

      case 's':   
        if (!checkforcmd_noparen(&invo->comm, S"silent", 3))
            break;
        cmod->cmod_flags |= CMOD_SILENT;
        if (*invo->comm == '!' && !SPACE_OR_TAB(invo->comm[-1])) {
            //":silent!", but not "silent !cmd"
            invo->comm = skipwhite(invo->comm + 1);
            cmod->cmod_flags |= CMOD_ERRSILENT;
        }
        continue;

      case 't':   
         if (checkforcmd_noparen(&p, S"tab", 3)) {
            if (!skip_only) {
               long tabnr = doGetCommandAddress(invo, &invo->comm,
                        ADDR_TABS, invo->skip,
                        skip_only, false, 1);
               if (tabnr == MAXLNUM)
                  cmod->cmod_tab = indexOfTab(curtab) + 1;
               else {
                  if (tabnr < 0 || tabnr > LAST_TAB_NR) {
                     *errorMsg = _(e_invalid_range);
                     return FAIL;
                  }
                  cmod->cmod_tab = tabnr + 1;
              }
            }
            invo->comm = p;
            continue;
         }
         if (!checkforcmd_noparen(&invo->comm, S"topleft", 2))
            break;
         cmod->cmod_split |= WSP_TOP;
         continue;

      case 'u':   
         if (!checkforcmd_noparen(&invo->comm, S"unsilent", 3))
            break;
         cmod->cmod_flags |= CMOD_UNSILENT;
         continue;

      case 'v':   
         if (checkforcmd_noparen(&invo->comm, S"vertical", 4)) {
            cmod->cmod_split |= WSP_VERT;
            continue;
         }
         if (!checkforcmd_noparen(&p, S"verbose", 4))
            break;
         if (eeIsDigit(*invo->comm)) {
            //zero means not set, one is verbose == 0, etc.
            cmod->cmod_verbose = atoi((char *)invo->comm) + 1;
         } else
            cmod->cmod_verbose = 2;  //default: verbose == 1
         invo->comm = p;
         continue;
      }
      break;
   }

   if (has_visual_range) {
      if (invo->comm > cmd_start) {
         //Move the '<,'> range to after the modifiers and insert a colon. Since the modifiers 
         //have been parsed put the colon on top of the space: "'<,'>mod cmd" -> "mod:'<,'>cmd
         //Put invo->comm after the colon.
         if (use_plus_cmd) {
            Unt len = STRLEN(cmd_start);

            //Special case: empty command uses "+":
            // "'<,'>mods" -> "mods *+
            // Use "*" instead of "'<,'>" to avoid the command getting
            // longer, in case it was allocated.
            MEMMOVE(orig_cmd, cmd_start, len);
            STRCPY(orig_cmd + len, " *+");
         } else {
            MEMMOVE(cmd_start - 5, cmd_start, invo->comm - cmd_start);
            invo->comm -= 5;
            MEMMOVE(invo->comm - 1, ":'<,'>", 6);
         }
      } else
         //No modifiers, move the pointer back. Special case: change empty command to "+".
         if (use_plus_cmd)
            invo->comm = (CS)"'<,'>+";
         else
            invo->comm = orig_cmd;
   } ei (use_plus_cmd)
     invo->comm = (CS)exmode_plus;

   return OK;
}

//Apply the command modifiers. Save current state into commModifierG, call undoCommModifier() later
pub void
applyCommModifiers(CommandModifier* cmod) {
   if (cmod->cmod_verbose > 0) {
      if (cmod->cmod_verbose_save == 0)
          cmod->cmod_verbose_save = p_verbose + 1;
      p_verbose = cmod->cmod_verbose - 1;
   }

   if ((cmod->cmod_flags & (CMOD_SILENT | CMOD_UNSILENT)) && cmod->cmod_save_msg_silent == 0) {
      cmod->cmod_save_msg_silent = msg_silent + 1;
      cmod->cmod_save_msg_scroll = msg_scroll;
   }
   if (cmod->cmod_flags & CMOD_SILENT)
      ++msg_silent;
   if (cmod->cmod_flags & CMOD_UNSILENT)
      msg_silent = 0;

   if (cmod->cmod_flags & CMOD_ERRSILENT) {
      ++emsg_silent;
      ++cmod->cmod_did_esilent;
   }

   if ((cmod->cmod_flags & CMOD_NOAUTOCMD) && cmod->cmod_save_ei == NULL) {
      //Set @eventignore to "all".
      //First save the existing option value for restoring it later.
      cmod->cmod_save_ei = p_ei ? copyStr(p_ei) : null;
      optChangeStringOptionDirect(S"eventignore", S"all", 0, SID_NONE);
   }
}

//Undo and free contents of "cmod".
pub void
undoCommModifier(CommandModifier *cmod) {
   if (cmod->cmod_verbose_save > 0) {
      p_verbose = cmod->cmod_verbose_save - 1;
      cmod->cmod_verbose_save = 0;
   }

   if (cmod->cmod_save_ei) {
      //Restore 'eventignore' to the value before ":noautocmd".
      optChangeStringOptionDirect(S"eventignore", cmod->cmod_save_ei, 0, SID_NONE);
      cmod->cmod_save_ei = NULL;
   }

   eeRegFree(cmod->cmod_filter_regmatch.regprog);

   if (cmod->cmod_save_msg_silent > 0) {
      //messages could be enabled for a serious error, need to check if the
      //counters don't become negative
      if (!anyEmsgG || msg_silent > cmod->cmod_save_msg_silent - 1)
          msg_silent = cmod->cmod_save_msg_silent - 1;
      emsg_silent -= cmod->cmod_did_esilent;
      if (emsg_silent < 0)
          emsg_silent = 0;
      //Restore msg_scroll, it's set by file I/O commands, even when no
      //message is actually displayed.
      msg_scroll = cmod->cmod_save_msg_scroll;

      //"silent reg" or "silent echo x" inside "redir" leaves msgColG
      //somewhere in the line.  Put it back in the first column.
      if (redirecting())
          msgColG = 0;

      cmod->cmod_save_msg_silent = 0;
      cmod->cmod_did_esilent = 0;
   }
}

//Parse the address range, if any, in "invo". May set the last search pattern, unless "silent" 
//is true. Return FAIL and set "errorMsg" or return OK.
pub int
parse_cmd_address(Invocation* invo, CS* errorMsg, int silent) {
   int      address_count = 1;
   LineNr   lnum;
   int      need_check_cursor = false;
   int      ret = FAIL;

   //Repeat for all ',' or ';' separated addresses.
   for (;;) {
      invo->line1 = invo->line2;
      invo->line2 = default_address(invo);
      invo->comm = skipwhite(invo->comm);
      lnum = doGetCommandAddress(invo, &invo->comm, invo->addressKind, invo->skip, silent,
                  invo->addr_count == 0, address_count++);
      if (invo->comm == NULL)   //error detected
          goto theend;
      if (lnum == MAXLNUM) {
         if (*invo->comm == '%') {  //'%' - all lines
            ++invo->comm;
            switch (invo->addressKind) {
              case ADDR_LINES:
              case ADDR_OTHER:
                 invo->line1 = 1;
                 invo->line2 = curBook->mem.lineCount;
                 break;
              case ADDR_LOADED_BUFFERS: {
                  Book* book = firstBook;

                  while (book->next && bookNoMemfile(book))
                     book = book->next;
                  invo->line1 = book->fiNum;
                  book = lastBook;
                  while (book->prev && bookNoMemfile(book))
                     book = book->prev;
                  invo->line2 = book->fiNum;
                  break;
               }
               case ADDR_BUFFERS:
                  invo->line1 = firstBook->fiNum;
                  invo->line2 = lastBook->fiNum;
                  break;
               case ADDR_PORTALS:
               case ADDR_TABS:
                  if (IS_USER_COMMAND(invo->id)) {
                      invo->line1 = 1;
                      invo->line2 = invo->addressKind == ADDR_PORTALS ? LAST_WIN_NR : LAST_TAB_NR;
                  } else {
                      //there is no Vim command which uses '%' and ADDR_PORTALS or ADDR_TABS
                      *errorMsg = _(e_invalid_range);
                      goto theend;
                  }
                  break;
               case ADDR_TABS_RELATIVE:
                case ADDR_UNSIGNED:
                case ADDR_QUICKFIX:
               *errorMsg = _(e_invalid_range);
               goto theend;
                case ADDR_ARGUMENTS:
               if (ARGCOUNT == 0)
                   invo->line1 = invo->line2 = 0;
               else
               {
                   invo->line1 = 1;
                   invo->line2 = ARGCOUNT;
               }
               break;
                case ADDR_QUICKFIX_VALID:
               invo->line1 = 1;
               invo->line2 = llGetValidSize(invo);
               if (invo->line2 == 0)
                   invo->line2 = 1;
               break;
                case ADDR_NONE:
               //Will give an error later if a range is found.
               break;
            }
            ++invo->addr_count;
          } ei (*invo->comm == '*') {
               Pos       *fp;

               //'*' - visual area
               if (invo->addressKind != ADDR_LINES) {
                   *errorMsg = _(e_invalid_range);
                   goto theend;
               }

               ++invo->comm;
               if (!invo->skip) {
                   fp = getmark('<', false);
                   if (check_mark(fp) == FAIL)
                      goto theend;
                   invo->line1 = fp->lnum;
                   fp = getmark('>', false);
                   if (check_mark(fp) == FAIL)
                      goto theend;
                   invo->line2 = fp->lnum;
                   ++invo->addr_count;
               }
          }
      }
      else
          invo->line2 = lnum;
      invo->addr_count++;

      if (*invo->comm == ';')
      {
          if (!invo->skip)
          {
         curPor->cursor.lnum = invo->line2;

         //Don't leave the cursor on an illegal line or column, but do
         //accept zero as address, so 0;/PATTERN/ works correctly
         //(where zero usually means to use the first line).
         //Check the cursor position before returning.
         if (invo->line2 > 0)
             check_cursor();
         else
             check_cursor_col();
         need_check_cursor = true;
          }
      } ei (*invo->comm != ',')
          break;
      ++invo->comm;
   }

   //One address given: set start and end lines.
   if (invo->addr_count == 1) {
      invo->line1 = invo->line2;
      //... but only implicit: really no address given
      if (lnum == MAXLNUM)
         invo->addr_count = 0;
   }
   ret = OK;

theend:
   if (need_check_cursor)
      check_cursor();
   return ret;
}

//Append "cmd" to the error message in IObuff.
//Take care of limiting the length and handling 0xa0, which would be invisible otherwise.
private void
append_command(CS cmd) {
   Unt  len = STRLEN(IObuff);
   CS s = cmd;
   CS d;

   if (len > IOSIZE - 100) {
      //Not enough space, truncate and put in "...".
      d = IObuff + IOSIZE - 100;
      d -= mb_head_off(IObuff, d);
      STRCPY(d, "...");
   }
   STRCAT(IObuff, ": ");
   d = IObuff + STRLEN(IObuff);
   while (*s != ZERO && d - IObuff + 5 < IOSIZE) {
      if (s[0] == 0xc2 && s[1] == 0xa0) {
         s += 2;
         STRCPY(d, "<a0>");
         d += 4;
      } ei (d - IObuff + utfCharLen(s) + 1 >= IOSIZE)
         break;
      else
         MB_COPY_CHAR(s, d);
   }
   *d = ZERO;
}

//If "start" points "&opt", "&l:opt", "&g:opt" or "$ENV" return a pointer to
//the name.  Otherwise just return "start".
pub CS
skip_option_env_lead(CS start) {
   CS name = start;
   if (*start == '&') {
      if ((start[1] == 'l' || start[1] == 'g') && start[2] == ':')
         name += 3;
      else
         name += 1;
   } ei (*start == '$')
      name += 1;
   return name;
}

//Return true and set "*idx" if "p" points to a one letter command.
//- The 'k' command can directly be followed by any character
//     but :keepa[lt] is another command, as are :keepj[umps],
//     :kee[pmarks] and :keepp[atterns].
//- The 's' command can be followed directly by 'c', 'g', 'i', 'I' or 'r'
//     but :sre[wind] is another command, as are :scr[iptnames],
//     :scs[cope], :sim[alt], :sig[ns] and :sil[ent].
private int
oneLetterCommand(CS p, OUT CommIndex *idx) {
   if (p[0] == 'k'  && (p[1] != 'e' || (p[1] == 'e' && p[2] != 'e'))) {
      *idx = C_k;
      return true;
   }
   if (p[0] == 's'
          && ((p[1] == 'c' && (p[2] == ZERO || (p[2] != 's' && p[2] != 'r'
               && (p[3] == ZERO || (p[3] != 'i' && p[4] != 'p')))))
             || p[1] == 'g'
             || (p[1] == 'i' && p[2] != 'm' && p[2] != 'l' && p[2] != 'g')
             || p[1] == 'I'
             || (p[1] == 'r' && p[2] != 'e'))
   ) {
      *idx = C_substitute;
      return true;
   }
   return false;
}

//true if "cmd" starts with "123->", a number followed by a method call.
pub int
number_method(CS cmd) {
   CS p = skipdigits(cmd);
   return p > cmd && (p = skipwhite(p))[0] == '-' && p[1] == '>';
}

//Find a Command by its name, either built-in or user. Start of the name can be found at 
//invo->comm. Set invo->id and return a pointer to char after the command name.
//"full" is set to true if the whole command name matched.
//
//If "lookup" is not NULL recognize expression without "eval" or "call" and assignment without 
//"let".  Sets invo->id to the command while returning "invo->comm".
//
//Return NULL for an ambiguous user command.
pub CS
findCommand(Invocation* invo, int* full, int (*lookup)(CS, Unt, int cmd)) {
   int len;
   int i;

   CS p = invo->comm;
   if (lookup) {
      CS pskip = skip_option_env_lead(invo->comm);

      if (firstOccurrence((CS)"{('[\"@&$", *p) != NULL
            || ((p = to_name_const_end(pskip)) > invo->comm && *p != ZERO)
            || (p[0] == '0' && p[1] == 'z')
      ) {
         if (*invo->comm == '&'
             || (invo->comm[0] == '$' && invo->comm[1] != '\'' && !isComment(invo->comm))
             || (invo->comm[0] == '@'
                  && (valid_yank_reg(invo->comm[1], false) || invo->comm[1] == '@'))
         ){
            if (*invo->comm == '&') {
               p = invo->comm + 1;
               if (STRNCMP("l:", p, 2) == 0 || STRNCMP("g:", p, 2) == 0)
                  p += 2;
               p = toNameEnd(p, false);
            } ei (*invo->comm == '$') {
               p = toNameEnd(invo->comm + 1, false);
            } else {
               p = invo->comm + 2;
            }
            if (endsComm(skipwhite(p))) {
               //"&option <NL>", "$ENV <NL>" and "@r <NL>" are the start of an expression.
               invo->id = C_eval;
               return invo->comm;
            }
            //"&option" can be followed by "->" or "=", check below
         }

         CS swp = skipwhite(p);
         if (
            //"(..." is an expression. "funcname(" is always a function call.
            *p == '('
             || (p == invo->comm
               ? (
                   //"{..." is a dict expression or block start.
                   *invo->comm == '{'
                   //"'string'->func()" is an expression.
                || *invo->comm == '\''
                   //'"string"->func()' is an expression.
                || isComment(invo->comm)
                   //'$"string"->func()' is an expression.
                   //"$'string'->func()" is an expression.
                || (invo->comm[0] == '$' && (invo->comm[1] == '\'' || isComment(invo->comm)))
                   //'0z1234->func()' is an expression.
                || (invo->comm[0] == '0' && invo->comm[1] == 'z')
                   //"g:varname" is an expression.
                || invo->comm[1] == ':')
                   //"varname->func()" is an expression.
               : (*swp == '-' && swp[1] == '>'))
         ) {
            invo->id = C_eval;
            return invo->comm;
         }

         if ((p != invo->comm && (
                  //"varname[]" is an expression.
                  *p == '['
                  //"varname.key" is an expression.
                  || (*p == '.' && (ASCII_ISALPHA(p[1]) || p[1] == '_'))
                )
             )
             //g:[key] is an expression
             || STRNCMP(invo->comm, "g:[", 3) == 0
         ){
            //When followed by "=" or "+=" then it is an assignment. Skip over the whole thing, 
            //which can be:
            //  name.member = val
            //  name[a : b] = val
            //  name[idx] = val
            //  name[idx].member = val
            //  etc.
            invo->id = C_eval;
            return invo->comm;
         }

         //"[...]->Method()" is a list expression, but "[a, b] = Func()" is an assignment.
         //If there is no line break inside the "[...]" then "p" is
         //advanced to after the right bracket by to_name_const_end(): check if a "=" follows.
         //If "[...]" has a line break "p" still points at the left bracket and it
         //can't be an assignment.
         if (*invo->comm == '[') {
            p = to_name_const_end(invo->comm);
            if (p == invo->comm && *p == '[') {
               int count = 0;
               int   semicolon = false;

               p = skip_var_list(invo->comm, &count, &semicolon, true);
            }
            CS eq = p;
            if (eq) {
                eq = skipwhite(eq);
               if (firstOccurrence((CS)"+-*/%.", *eq) != NULL) {
                  if (eq[0] == '.' && eq[1] == '.')
                      ++eq;
                  ++eq;
               }
            }
            if (p == NULL || p == invo->comm || *eq != '=') {
                invo->id = C_eval;
                return invo->comm;
            }
         }
      }

      //1234->func() is a method call
      if (number_method(invo->comm)) {
         invo->id = C_eval;
         return invo->comm;
      }

      //"g:", "s:" and "l:" are always assumed to be a variable, thus start
      //an expression. A global/substitute/list command needs to use a longer name.
      if (firstOccurrence(S"gsl", *p) != NULL && p[1] == ':') {
          invo->id = C_eval;
          return invo->comm;
      }

      //If it is an ID it might be a variable with an operator on the next
      //line, if the variable exists it can't be a command.
      if (p > invo->comm && endsComm(skipwhite(p))
            && (lookup(invo->comm, p - invo->comm, true) == OK
                || (ASCII_ISALPHA(invo->comm[0]) && invo->comm[1] == ':'))) {
          invo->id = C_eval;
          return invo->comm;
      }
   }

   //Isolate the command and search for it in the command table.
   p = invo->comm;
   if (oneLetterCommand(p, OUT &invo->id)) {
      ++p;
      if (full)
         *full = true;
   } else {
      while (ASCII_ISALPHA(*p))
         ++p;
          
      //check for non-alpha command
      if (p == invo->comm && firstOccurrence((CS)"@*!=><&~#}", *p) != NULL)
         ++p;
      len = (int)(p - invo->comm);
      //The "d" command can directly be followed by 'l' or 'p' flag
      if (*invo->comm == 'd' && (p[-1] == 'l' || p[-1] == 'p')) {
          //Check for ":dl", ":dell", etc. to ":deletel": that's
          //:delete with the 'l' flag.  Same for 'p'.
          for (i = 0; i < len; ++i)
         if (invo->comm[i] != ((CS)"delete")[i])
             break;
         if (i == len - 1) {
            --len;
            if (p[-1] == 'l')
               invo->flags |= EXFLAG_LIST;
            else
               invo->flags |= EXFLAG_PRINT;
         }
      }

      if (ASCII_ISLOWER(invo->comm[0])) {
         int c1 = invo->comm[0];
         int c2 = len == 1 ? ZERO : invo->comm[1];

         if (generatedCommandCount != (int)COUNT_COMMANDS) {
            lo("Generated command count = %d but COUNT_COMMANDS = %d", 
                  generatedCommandCount, (int)COUNT_COMMANDS
            );
            internalErrMsg(e_command_table_needs_to_be_updated_run_make_ids);
            exitEegl(1);
         }

         //Use a precomputed index for fast look-up in commands[]
         //taking into account the first 2 letters of invo->comm.
         invo->id = commandIndices0[c1 - 'a'];
         if (ASCII_ISLOWER(c2))
            invo->id += commandIndices1[c1 - 'a'][c2 - 'a'];
      }
      ei (ASCII_ISUPPER(invo->comm[0]))
          invo->id = C_Next;
      else
          invo->id = C_bang;

      for ( ; (int)invo->id < (int)COUNT_COMMANDS; invo->id = (CommIndex)((int)invo->id + 1)) {
         if (STRNCMP(commands[(int)invo->id].name, (char *)invo->comm, (Unt)len) == 0) {
            if (full && commands[invo->id].name[len] == ZERO)
               *full = true;
            break;
         }
      } 

      //Do not recognize ":*" as the star command 
      if (invo->id == C_star)
         p = invo->comm;

      //Look for a user defined command as a last resort.  Let ":Print" be
      //overruled by a user defined command.
      if ((invo->id == COUNT_COMMANDS || invo->id == C_Print)
            && *invo->comm >= 'A' && *invo->comm <= 'Z') {
         //User defined commands may contain digits.
         while (ASCII_ISALNUM(*p))
            ++p;
         p = find_ucmd(invo, p, full, NULL, NULL);
      }
      if (!p || p == invo->comm)
         invo->id = COUNT_COMMANDS;
   }

   return p;
}

typedef struct {
   char   *name;
   int      minlen;
   int      has_count;  //:123verbose  :3tab
} CommModeInfo;

private CommModeInfo modeInfoTable[] = {
   {"aboveleft", 3, false},
   {"belowright", 3, false},
   {"botright", 2, false},
   {"browse", 3, false},
   {"confirm", 4, false},
   {"filter", 4, false},
   {"hide", 3, false},
   {"horizontal", 3, false},
   {"keepalt", 5, false},
   {"keepjumps", 5, false},
   {"keepmarks", 3, false},
   {"keeppatterns", 5, false},
   {"leftabove", 5, false},
   {"legacy", 3, false},
   {"lockmarks", 3, false},
   {"noautocmd", 3, false},
   {"noswapfile", 3, false},
   {"rightbelow", 6, false},
   {"silent", 3, false},
   {"tab", 3, true},
   {"topleft", 2, false},
   {"unsilent", 3, false},
   {"verbose", 4, true},
   {"vertical", 4, false}
};   //modeInfoTable

//Return length of a command modifier (including optional count). Return 0 when it's not a modifier.
pub int
modifier_len(CS cmd) {
   CS p = cmd;
   if (EE_ISDIGIT(*cmd))
      p = skipwhite(skipdigits(cmd + 1));
   for (int i = 0; i < (int)ARRAY_LENGTH(modeInfoTable); ++i) {
      int j = 0;
      for (; p[j] != ZERO; ++j) {
         if (p[j] != modeInfoTable[i].name[j])
            break;
      } 
      if (!ASCII_ISALPHA(p[j]) && j >= modeInfoTable[i].minlen
                  && (p == cmd || modeInfoTable[i].has_count))
         return j + (int)(p - cmd);
   }
   return 0;
}

//Return > 0 if the command "name" exists.
//Return 2 if there is an exact match.
//Return 3 if there is an ambiguous match.
pub int
cmd_exists(CS name) {
   int full = false;

   //Check command modifiers.
   for (int i = 0; i < (int)ARRAY_LENGTH(modeInfoTable); ++i) {
      int j = 0;
      for (; name[j] != ZERO; ++j) {
         if (name[j] != modeInfoTable[i].name[j])
            break;
      } 
      if (name[j] == ZERO && j >= modeInfoTable[i].minlen)
         return (modeInfoTable[i].name[j] == ZERO ? 2 : 1);
   }

   //Check built-in commands and user defined commands.
   //For ":2match" and ":3match" we need to skip the number.
   Invocation   invo;
   invo.comm = (*name == '2' || *name == '3') ? name + 1 : name;
   invo.id = (CommIndex)0;
   invo.flags = 0;
   CS p = findCommand(&invo, &full, NULL);
   if (!p)
      return 3;
   if (eeIsDigit(*name) && invo.id != C_match)
      return 0;
   if (*skipwhite(p) != ZERO)
      return 0;   //trailing garbage
   return (invo.id == COUNT_COMMANDS ? 0 : (full ? 2 : 1));
}

pub void
f_fullcommand(Var *argvars, Var *returnVar) {
   int      save_cmod_flags = commModifierG.cmod_flags;

   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   CS name = tv_get_string(&argvars[0]);
   if (!name)
      return;

   while (*name == ':')
      name++;
   name = skip_range(name, true, NULL);

   Invocation invo;
   invo.comm = (*name == '2' || *name == '3') ? name + 1 : name;
   invo.id = (CommIndex)0;
   invo.addr_count = 0;
   ++emsg_silent;  //don't complain about using "en" in Vim9 script
   CS p = findCommand(&invo, NULL, NULL);
   --emsg_silent;
   if (!p || invo.id == COUNT_COMMANDS)
      goto theend;

   returnVar->string = copyStr(IS_USER_COMMAND(invo.id)
             ? get_user_command_name(invo.useridx, invo.id)
             : commands[invo.id].name);
theend:
   commModifierG.cmod_flags = save_cmod_flags;
}

pub CommIndex
commandGetInd(CS cmd, int len) {
   CommIndex idx;
   if (!oneLetterCommand(cmd, OUT &idx)) {
      for (idx = (CommIndex)0; (int)idx < (int)COUNT_COMMANDS; idx = (CommIndex)((int)idx + 1)) {
         if (STRNCMP(commands[(int)idx].name, cmd, (Unt)len) == 0)
            break;
      } 
   } 

   return idx;
}

pub long
commandGetFlags(CommIndex idx) {
   return (long)commands[(int)idx].flags;
}

//Skip a range specifier of the form: addr [,addr] [;addr] ..
//
//Backslashed delimiters after / or ? will be skipped, and commands will
//not be expanded between /'s and ?'s or after "'".
//
//Also skip white space and ":" characters after the range.
//Return the "cmd" pointer advanced to beyond the range.
pub CS
skip_range(
   CS cmd_start,
   int skip_star,   //skip "*" used for Visual range
   Unt* ctx)      //pointer to context or NULL
{
   CS cmd = cmd_start;
   unsigned   delim;

   while (firstOccurrence((CS)" \t0123456789.$%'/?-+,;\\", *cmd) != NULL) {
      if (*cmd == '\\') {
         if (cmd[1] == '?' || cmd[1] == '/' || cmd[1] == '&')
            ++cmd;
         else
            break;
      } ei (*cmd == '\'') {
         CS p = cmd;

         //a quote is only valid at the start or after a separator
         while (p > cmd_start) {
            --p;
            if (!SPACE_OR_TAB(*p))
               break;
         }
         if (cmd > cmd_start && !SPACE_OR_TAB(*p) && *p != ',' && *p != ';')
            break;
         if (*++cmd == ZERO && ctx != NULL)
            *ctx = EXPAND_NOTHING;
      } ei (*cmd == '/' || *cmd == '?') {
         delim = *cmd++;
         while (*cmd != ZERO && *cmd != delim)
            if (*cmd++ == '\\' && *cmd != ZERO)
               ++cmd;
         if (*cmd == ZERO && ctx)
            *ctx = EXPAND_NOTHING;
      }
      if (*cmd != ZERO)
         ++cmd;
   }

   //Skip ":" and white space.
   while (*cmd == ':')
      cmd = skipwhite(cmd + 1);

    //Skip "*" used for Visual range.
   if (skip_star && *cmd == '*')
       cmd = skipwhite(cmd + 1);

   return cmd;
}

private void
addr_error(CommandAddress addressKind) {
   if (addressKind == ADDR_NONE)
      emsg(_(e_no_range_allowed));
   else
      emsg(_(e_invalid_range));
}

//Return the default address for an address type.
private LineNr
default_address(Invocation* invo) {
   LineNr lnum = 0;

   switch (invo->addressKind) {
   case ADDR_LINES:
   case ADDR_OTHER:
      //Default is the cursor line number.  Avoid using an invalid
      //line number though.
      if (curPor->cursor.lnum > curBook->mem.lineCount)
         lnum = curBook->mem.lineCount;
      else
         lnum = curPor->cursor.lnum;
      break;
   case ADDR_PORTALS:
      lnum = GET_PORT_NR;
      break;
   case ADDR_ARGUMENTS:
      lnum = curPor->argListInd + 1;
      if (lnum > ARGCOUNT)
         lnum = ARGCOUNT;
      break;
   case ADDR_LOADED_BUFFERS:
   case ADDR_BUFFERS:
      lnum = curBook->fiNum;
      break;
   case ADDR_TABS:
      lnum = CURRENT_TAB_NR;
      break;
   case ADDR_TABS_RELATIVE:
   case ADDR_UNSIGNED:
      lnum = 1;
      break;
   case ADDR_QUICKFIX:
      lnum = llGetCurrIndex(invo);
      break;
   case ADDR_QUICKFIX_VALID:
      lnum = llGetCurrValidIndex(invo);
      break;
   case ADDR_NONE:
      //Will give an error later if a range is found.
      break;
   }
   return lnum;
}

//Get a single Command address.
//
//Set ptr to the next character after the part that was interpreted. Set ptr to NULL when an 
//error is encountered. This may set the last used search pattern.
//
//Return MAXLNUM when no address was found.
pub LineNr
doGetCommandAddress(
   Invocation* invo,
   OUT CS* ptr,
   CommandAddress   addressKind,
   int skip,      //only skip the address, don't use it
   int silent,      //no errors or side effects
   int to_other_file,  //flag: may jump to other file
   int address_count //1 for first address, >1 after comma
){
   int c;
   int i;
   long n;
   Pos pos;
   Pos* fp;
   Book* book;

   CS cmd = skipwhite(*ptr);
   LineNr lnum = MAXLNUM;
   do {
      switch (*cmd) {
      case '.':             //'.' - Cursor position
         ++cmd;
         switch (addressKind) {
         case ADDR_LINES:
         case ADDR_OTHER:
            lnum = curPor->cursor.lnum;
            break;
         case ADDR_PORTALS:
            lnum = GET_PORT_NR;
            break;
         case ADDR_ARGUMENTS:
            lnum = curPor->argListInd + 1;
            break;
         case ADDR_LOADED_BUFFERS:
         case ADDR_BUFFERS:
            lnum = curBook->fiNum;
            break;
         case ADDR_TABS:
            lnum = CURRENT_TAB_NR;
            break;
         case ADDR_NONE:
         case ADDR_TABS_RELATIVE:
         case ADDR_UNSIGNED:
            addr_error(addressKind);
            cmd = NULL;
            goto error;
            break;
         case ADDR_QUICKFIX:
            lnum = llGetCurrIndex(invo);
            break;
         case ADDR_QUICKFIX_VALID:
            lnum = llGetCurrValidIndex(invo);
            break;
         }
         break;

      case '$':             //'$' - last line
         ++cmd;
         switch (addressKind) {
         case ADDR_LINES:
         case ADDR_OTHER:
            lnum = curBook->mem.lineCount;
            break;
         case ADDR_PORTALS:
            lnum = LAST_WIN_NR;
            break;
         case ADDR_ARGUMENTS:
            lnum = ARGCOUNT;
            break;
         case ADDR_LOADED_BUFFERS:
            book = lastBook;
            while (!book->mem.mfile) {
               if (!book->prev)
                  break;
               book = book->prev;
            }
            lnum = book->fiNum;
            break;
         case ADDR_BUFFERS:
               lnum = lastBook->fiNum;
               break;
         case ADDR_TABS:
               lnum = LAST_TAB_NR;
               break;
         case ADDR_NONE:
         case ADDR_TABS_RELATIVE:
         case ADDR_UNSIGNED:
               addr_error(addressKind);
               cmd = NULL;
               goto error;
               break;
         case ADDR_QUICKFIX:
               lnum = llGetSize(invo);
               if (lnum == 0)
                   lnum = 1;
               break;
         case ADDR_QUICKFIX_VALID:
               lnum = llGetValidSize(invo);
               if (lnum == 0)
                   lnum = 1;
               break;
         }
         break;

      case '\'':             //''' - mark
         if (*++cmd == ZERO) {
             cmd = NULL;
             goto error;
         }
         if (addressKind != ADDR_LINES) {
             addr_error(addressKind);
             cmd = NULL;
             goto error;
         }
         if (skip)
            ++cmd;
         else {
            //Only accept a mark in another file when it is used by itself: ":'M".
            fp = getmark(*cmd, to_other_file && cmd[1] == ZERO);
            ++cmd;
            if (fp == (Pos *)-1)
               //Jumped to another file.
               lnum = curPor->cursor.lnum;
            else {
               if (check_mark(fp) == FAIL) {
                  cmd = NULL;
                  goto error;
               }
               lnum = fp->lnum;
            }
         }
         break;

      case '/':
      case '?':         //'/' or '?' - search
         c = *cmd++;
         if (addressKind != ADDR_LINES) {
            addr_error(addressKind);
            cmd = NULL;
            goto error;
         }
         if (skip)  { //skip "/pat/"
            cmd = skip_regexp(cmd, c, true);
            if (*cmd == c)
               ++cmd;
         } else {
            pos = curPor->cursor; //save curPor->cursor

            //When '/' or '?' follows another address, start from there.
            if (lnum > 0 && lnum != MAXLNUM)
               curPor->cursor.lnum = lnum > curBook->mem.lineCount ? curBook->mem.lineCount : lnum;

            //Start a forward search at the end of the line (unless before the first line).
            //Start a backward search at the start of the line. This makes sure we never match in 
            //the current line, and can match anywhere in the next/previous line.
            curPor->cursor.col = (c == '/' && curPor->cursor.lnum > 0) ? MAXCOL : 0;
            searchcmdlen = 0;
            Unt flags = silent ? SEARCH_KEEP : SEARCH_HIS | SEARCH_MSG;
            if (!do_search(NULL, c, c, text(cmd), 1L, flags, NULL)) {
               curPor->cursor = pos;
               cmd = NULL;
               goto error;
            }
            lnum = curPor->cursor.lnum;
            curPor->cursor = pos;
            //adjust command string pointer
            cmd += searchcmdlen;
         }
         break;

      case '\\':          //"\?", "\/" or "\&", repeat search
         ++cmd;
         if (addressKind != ADDR_LINES) {
            addr_error(addressKind);
            cmd = NULL;
            goto error;
         }
         if (*cmd == '&')
            i = RE_SUBST;
         ei (*cmd == '?' || *cmd == '/')
            i = RE_SEARCH;
         else {
            emsg(_(e_backslash_should_be_followed_by));
            cmd = NULL;
            goto error;
         }

         if (!skip) {
            //When search follows another address, start from there.
            if (lnum != MAXLNUM)
               pos.lnum = lnum;
            else
               pos.lnum = curPor->cursor.lnum;

            //Start the search just like for the above do_search().
            if (*cmd != '?')
               pos.col = MAXCOL;
            else
               pos.col = 0;
            pos.coladd = 0;
            if (searchit(curPor, curBook, &pos, NULL,
                  *cmd == '?' ? BACKWARD : FORWARD,
                  (Text){null, 0}, 1L, SEARCH_MSG, i, NULL) != FAIL)
               lnum = pos.lnum;
            else {
               cmd = NULL;
               goto error;
            }
         }
         ++cmd;
         break;

      default:
      if (EE_ISDIGIT(*cmd))   //absolute line number
         lnum = parseLong(&cmd);
      }

      for (;;) {
         cmd = skipwhite(cmd);
         if (*cmd != '-' && *cmd != '+' && !EE_ISDIGIT(*cmd))
            break;

         if (lnum == MAXLNUM) {
            switch (addressKind) {
            case ADDR_LINES:
            case ADDR_OTHER:
               //"+1" is same as ".+1"
               lnum = curPor->cursor.lnum;
               break;
            case ADDR_PORTALS:
               lnum = GET_PORT_NR;
               break;
            case ADDR_ARGUMENTS:
               lnum = curPor->argListInd + 1;
               break;
            case ADDR_LOADED_BUFFERS:
            case ADDR_BUFFERS:
               lnum = curBook->fiNum;
               break;
            case ADDR_TABS:
               lnum = CURRENT_TAB_NR;
               break;
            case ADDR_TABS_RELATIVE:
               lnum = 1;
               break;
            case ADDR_QUICKFIX:
               lnum = llGetCurrIndex(invo);
               break;
            case ADDR_QUICKFIX_VALID:
               lnum = llGetCurrValidIndex(invo);
               break;
            case ADDR_NONE:
            case ADDR_UNSIGNED:
               lnum = 0;
               break;
            }
         }

         if (EE_ISDIGIT(*cmd))
            i = '+';      //"number" is same as "+number"
         else
            i = *cmd++;
         if (!EE_ISDIGIT(*cmd))   //'+' is '+1'
            n = 1;
         else {
            //"number", "+number" or "-number"
            n = parseLong(&cmd);
            if (n == MAXLNUM) {
               emsg(_(e_line_number_out_of_range));
               cmd = NULL;
               goto error;
            }
         }

         if (addressKind == ADDR_TABS_RELATIVE) {
            emsg(_(e_invalid_range));
            cmd = NULL;
            goto error;
         } ei (addressKind == ADDR_LOADED_BUFFERS|| addressKind == ADDR_BUFFERS)
            lnum = compute_buffer_local_count( addressKind, lnum, (i == '-') ? -1 * n : n);
         else {
            //Relative line addressing: need to adjust for lines in a
            //closed fold after the first address.
            if (addressKind == ADDR_LINES && (i == '-' || i == '+') && address_count >= 2)
                (void)getFolds(lnum, NULL, OUT &lnum);
            if (i == '-')
                lnum -= n;
            else {
               if (lnum >= 0 && n >= (Long)LONG_MAX - lnum) {
                  emsg(_(e_line_number_out_of_range));
                  cmd = NULL;
                  goto error;
               }
               lnum += n;
            }
         }
      }
   } while (*cmd == '/' || *cmd == '?');

error:
   *ptr = cmd;
   return lnum;
}

//Set invo->line1 and invo->line2 to the whole range.
//Used for commands with the DFLALL flag and no range given.
private void
address_default_all(Invocation* invo) {
   invo->line1 = 1;
   switch (invo->addressKind) {
   case ADDR_LINES:
   case ADDR_OTHER:
      invo->line2 = curBook->mem.lineCount;
      break;
   case ADDR_LOADED_BUFFERS: {
      Book *book = firstBook;

      while (book->next && bookNoMemfile(book))
         book = book->next;
      invo->line1 = book->fiNum;
      book = lastBook;
      while (book->prev && bookNoMemfile(book))
          book = book->prev;
      invo->line2 = book->fiNum;
      break;
      }
   case ADDR_BUFFERS:
      invo->line1 = firstBook->fiNum;
      invo->line2 = lastBook->fiNum;
      break;
   case ADDR_PORTALS:
      invo->line2 = LAST_WIN_NR;
      break;
   case ADDR_TABS:
      invo->line2 = LAST_TAB_NR;
      break;
   case ADDR_TABS_RELATIVE:
      invo->line2 = 1;
      break;
   case ADDR_ARGUMENTS:
      if (ARGCOUNT == 0)
         invo->line1 = invo->line2 = 0;
      else
         invo->line2 = ARGCOUNT;
      break;
   case ADDR_QUICKFIX_VALID:
      invo->line2 = llGetValidSize(invo);
      if (invo->line2 == 0)
         invo->line2 = 1;
      break;
   case ADDR_NONE:
   case ADDR_UNSIGNED:
   case ADDR_QUICKFIX:
      internalErrMsg(S"Cannot use DFLALL with ADDR_NONE, ADDR_UNSIGNED or ADDR_QUICKFIX");
      break;
   }
}

//Get flags from a command argument.
private void
get_flags(Invocation* invo) {
   while (firstOccurrence((CS)"lp#", *invo->arg) != NULL) {
      if (*invo->arg == 'l')
          invo->flags |= EXFLAG_LIST;
      ei (*invo->arg == 'p')
          invo->flags |= EXFLAG_PRINT;
      else
          invo->flags |= EXFLAG_NR;
      invo->arg = skipwhite(invo->arg + 1);
   }
}

//Function called for command which is Not Implemented.  NI!
pub void
c_ni(Invocation* invo) {
   if (!invo->skip)
      invo->errmsg = _(e_sorry_command_is_not_available_in_this_version);
}

#ifdef HAVE_EX_SCRIPT_NI
//Function called for script command which is Not Implemented.  NI!
//Skips over ":perl <<EOF" constructs.
private void
ex_script_ni(Invocation* invo) {
   if (!invo->skip)
      c_ni(invo);
   else
      eeglFree(script_get(invo, invo->arg));
}
#endif

//Check range in a command for validity. Return NULL when valid, error message when invalid.
private CS
invalid_range(Invocation* invo) {
   Book   *book;

   if (invo->line1 < 0 || invo->line2 < 0 || invo->line1 > invo->line2)
      return _(e_invalid_range);

   if (invo->argFlags & RANGE) {
      switch (invo->addressKind) {
      case ADDR_LINES:
         if (invo->line2 > curBook->mem.lineCount
                + (invo->id == C_diffget || invo->id == C_diffput)
         )
            return _(e_invalid_range);
         break;
      case ADDR_ARGUMENTS:
         //add 1 if ARGCOUNT is 0
         if (invo->line2 > ARGCOUNT + (!ARGCOUNT))
             return _(e_invalid_range);
         break;
      case ADDR_BUFFERS:
         //Only a boundary check, not whether the buffers actually exist.
         if (invo->line1 < 1 || invo->line2 > get_highest_fnum())
             return _(e_invalid_range);
         break;
      case ADDR_LOADED_BUFFERS:
         book = firstBook;
         while (bookNoMemfile(book)) {
            if (!book->next)
               return _(e_invalid_range);
            book = book->next;
         }
         if (invo->line1 < book->fiNum)
             return _(e_invalid_range);
         book = lastBook;
         while (bookNoMemfile(book)) {
            if (!book->prev)
               return _(e_invalid_range);
            book = book->prev;
         }
         if (invo->line2 > book->fiNum)
            return _(e_invalid_range);
         break;
      case ADDR_PORTALS:
         if (invo->line2 > LAST_WIN_NR)
            return _(e_invalid_range);
         break;
      case ADDR_TABS:
         if (invo->line2 > LAST_TAB_NR)
            return _(e_invalid_range);
         break;
      case ADDR_TABS_RELATIVE:
      case ADDR_OTHER:
         //Any range is OK.
         break;
      case ADDR_QUICKFIX:
         //No error for value that is too big, will use the last entry.
         if (invo->line2 <= 0) {
            if (invo->addr_count == 0)
               return _(e_no_entries_in_location_list);
            return _(e_invalid_range);
         }
         break;
      case ADDR_QUICKFIX_VALID:
         if ((invo->line2 != 1 && invo->line2 > llGetValidSize(invo)) || invo->line2 < 0)
            return _(e_invalid_range);
         break;
      case ADDR_UNSIGNED:
      case ADDR_NONE:
         //Will give an error elsewhere.
         break;
      }
   }
   return NULL;
}

//Correct the range for zero line number, if required.
private void
correct_range(Invocation* invo) {
   if (!(invo->argFlags & ZERO_LINE_OK)) { //zero in range not allowed
      if (invo->line1 == 0)
         invo->line1 = 1;
      if (invo->line2 == 0)
         invo->line2 = 1;
   }
}

//For a ":vimgrep" or ":vimgrepadd" command return a pointer past the
//pattern.  Otherwise return invo->arg.
private CS
skip_grep_pat(Invocation* invo) {
   CS p = invo->arg;

   if (*p != ZERO && (invo->id == C_vimgrep
         || invo->id == C_vimgrepadd
         || grepIsActuallyInternal(invo->id))
   ) {
      p = skipEeglGrepPat(p, NULL, NULL);
      if (!p)
         p = invo->arg;
   }
   return p;
}

//For the ":make" and ":grep" commands insert the @makeprog/@grepprog option
//into the command line, so that things like % get expanded.
private CS
replaceMakeProgramName(Invocation* invo, OUT CS p, OUT CS* commline) {
   CS programName;
   CS pos;
   CS ptr;
   int len;
   int i;

   //Don't do it when ":vimgrep" is used for ":grep".
   if ((invo->id == C_make || invo->id == C_grep || invo->id == C_grepadd)
          && !grepIsActuallyInternal(invo->id)
   ) {
      if (invo->id == C_grep || invo->id == C_grepadd) {
         programName = curBook->o.grepProg;
      } else {
         programName = curBook->o.makeProg;
      }
      if (!programName)
         return p;
         
      p = skipwhite(p);

      CS newCommline;
      if ((pos = (CS)STRSTR(programName, "$*")) != NULL) {
         //replace $* by given arguments
         i = 1;
         while ((pos = (CS)STRSTR(pos + 2, "$*")) != NULL)
            ++i;
         len = (int)STRLEN(p);
         newCommline = alloc(STRLEN(programName) + (Unt)i * (len - 2) + 1);
         ptr = newCommline;
         while ((pos = (CS)strstr((char *)programName, "$*")) != NULL) {
            i = (int)(pos - programName);
            STRNCPY(ptr, programName, i);
            STRCPY(ptr += i, p);
            ptr += len;
            programName = pos + 2;
         }
         STRCPY(ptr, programName);
      } else {
         Unt programNameLen = STRLEN(programName);

         newCommline = alloc(programNameLen + STRLEN(p) + 2);
         STRCPY(newCommline, programName);
         STRCPY(newCommline + programNameLen, " ");
         STRCPY(newCommline + programNameLen + 1, p);
      }
      
      msg_make(p);

      //'invo->comm' is not set here, because it is not used at C_make
      eeglFree(*commline);
      *commline = newCommline;
      p = newCommline;
   }
   return p;
}

//Expand file name in a command argument. When an error is detected, "errorMsg" is set to a 
//non-NULL pointer. Return FAIL for failure, OK otherwise.
pub int
expand_filename(Invocation* invo, OUT CS* commline, OUT CS* errorMsg){
   CS repl;
   Unt srclen;
   int n;
   int escaped;

   //Skip a regexp pattern for ":vimgrep[add] pat file..."
   CS p = skip_grep_pat(invo);

   //Decide to expand wildcards *before* replacing '%', '#', etc.  If
   //the file name contains a wildcard it should not cause expanding.
   //(it will be expanded anyway if there is a wildcard before replacing).
   int has_wildcards = mch_has_wildcard(p);//need to expand wildcards
   while (*p != ZERO) {
      //Skip over `=expr`, wildcards in it are not expanded.
      if (p[0] == '`' && p[1] == '=') {
         p += 2;
         (void)skip_expr(&p, NULL);
         if (*p == '`')
            ++p;
         continue;
      }
      //Quick check if this cannot be the start of a special string.
      //Also removes backslash before '%', '#' and '<'.
      if (firstOccurrence((CS)"%#<", *p) == NULL) {
         ++p;
         continue;
      }

      //Try to find a match at this position.
      repl = evalVars(
            OUT &(invo->higherOrderLnum), OUT errorMsg, 
            p, invo->arg, &srclen, &escaped, true
      );
      if (*errorMsg)      //error detected
         return FAIL;
      if (repl == NULL) {     //no match found
         p += srclen;
         continue;
      }

      //Wildcards won't be expanded below, the replacement is taken
      //literally. But do expand "~/file", "~user/file" and "$HOME/file".
      if (firstOccurrence(repl, '$') != NULL || firstOccurrence(repl, '~') != NULL) {
         CS l = repl;
         repl = doExpandEnvInMultiplePaths(repl);
         eeglFree(l);
      }

      //Need to escape white space et al. with a backslash. Don't do this for:
      //- replacement that already has been escaped: "##"
      //- shell commands (may have to use quotes instead).
      if (!invo->usefilter
         && !escaped
         && invo->id != C_bang
         && invo->id != C_grep
         && invo->id != C_grepadd
         && invo->id != C_make
         && invo->id != C_terminal
      ) {
         CS l;
# define ESCAPE_CHARS escape_chars

         for (l = repl; *l; ++l) {
            if (firstOccurrence(ESCAPE_CHARS, *l) != NULL) {
               l = copyStr_escaped(repl, ESCAPE_CHARS);
               eeglFree(repl);
               repl = l;
               break;
            }
         } 
      }

      //For a shell command a '!' must be escaped.
      if ((invo->usefilter || invo->id == C_bang || invo->id == C_terminal)
                && eeStrpbrk(repl, S"!") != NULL
      ) {
         CS l = copyStr_escaped(repl, S"!");
         eeglFree(repl);
         repl = l;
      }

      p = repl_commline(invo, p, srclen, repl, OUT commline);
      eeglFree(repl);
      if (!p)
         return FAIL;
   }

   //One file argument: Expand wildcards.
   //Don't do this with ":r !command" or ":w !command".
   if ((invo->argFlags & NOSPC_IN_EXTRA) && !invo->usefilter) {
      //May do this twice:
      //1. Replace environment variables.
      //2. Replace any other wildcards, remove backslashes.
      for (n = 1; n <= 2; ++n) {
         if (n == 2) {
            //Halve the number of backslashes (this is Vi compatible).
            //When wildcards are expanded, this is done by expandWildcard() below.
            if (!has_wildcards)
               backslash_halve(invo->arg);
          }

         if (has_wildcards) {
            if (n == 1) {
               //First loop: May expand environment variables. This can be done much faster with 
               //doExpandEnv() than with something else (e.g., calling a shell).
               //After expanding environment variables, check again if there are still wildcards 
               //present.
               if (firstOccurrence(invo->arg, '$') != NULL 
                     || firstOccurrence(invo->arg, '~') != NULL
               ) {
                  doExpandEnvVarsWithEscaped(
                        OUT (Text){nameBuffG, MAXPATHL}, invo->arg, true, NULL
                  );
                  has_wildcards = mch_has_wildcard(nameBuffG);
                  p = nameBuffG;
               }  else
                  p = NULL;
            } else { //n == 2
               Expand   xpc;
               int options = WILD_LIST_NOTFOUND | WILD_NOERROR | WILD_ADD_SLASH;

               expandInit(&xpc);
               xpc.context = EXPAND_FILES;
               if (p_wic)
                  options += WILD_ICASE;
               p = expandWildcard(&xpc, invo->arg, NULL, options, WILD_EXPAND_FREE);
               if (!p)
                  return FAIL;
            }
            if (p) {
               (void)repl_commline(invo, invo->arg, STRLEN(invo->arg), p, OUT commline);
               if (n == 2)   //p came from expandWildcard()
                  eeglFree(p);
            }
         }
      }
   }
   return OK;
}

//Replace part of the command line, keeping invo->comm, invo->arg and invo->nextComm correct.
//"src" points to the part that is to be replaced, of length "srclen". "repl" is the replacement 
//string. Return a pointer to the character after the replaced string, or null for failure.
private CS
repl_commline(
   Invocation* invo,
   CS src,
   Unt srclen,
   CS repl,
   OUT CS* commline
) {
   //The new command line is build in newCommline[]. First allocate it.
   //Careful: a "+cmd" argument may have been ZERO terminated.
   Unt repllen = STRLEN(repl);
   Unt taillen = STRLEN(src + srclen);
   Unt i = (src - *commline) + repllen + taillen + 3;
   CS newCommline = alloc(i);

   //Copy the stuff before the expanded part.
   //Copy the expanded stuff.
   //Copy what came after the expanded part.
   //Copy the next commands, if there are any.
   Unt newCommlineLen = src - *commline;   //length of part before replacement
   MEMMOVE(newCommline, *commline, newCommlineLen);

   MEMMOVE(newCommline + newCommlineLen, repl, repllen);
   newCommlineLen += repllen;      //remember the end of the string
   STRCPY(newCommline + newCommlineLen, src + srclen);
   src = newCommline + newCommlineLen;   //remember where to continue

   invo->comm = newCommline + (invo->comm - *commline);
   invo->arg = newCommline + (invo->arg - *commline);
   if (invo->higherOrderComm && invo->higherOrderComm != dollar_command)
      invo->higherOrderComm = newCommline + (invo->higherOrderComm - *commline);
   eeglFree(*commline);
   *commline = newCommline;

   return src;
}

//Check for '//' to start comments. If "keep_backslash" is true, do not remove any backslash.
pub void
separateNextCommand(Invocation* invo, int keep_backslash) {
   for (CS p = skip_grep_pat(invo) ; *p; MB_PTR_ADV(p)) {
      if (*p == Ctrl_V) {
         if ((invo->argFlags & (CTRLV | XFILE)) || keep_backslash)
            ++p;      //skip CTRL-V and next char
         else //remove CTRL-V and skip next char
            STRMOVE(p, p + 1);
         if (*p == ZERO)      //stop at ZERO after CTRL-V
            break;
      }
      //Skip over `=expr` when wildcards are expanded.
      ei (p[0] == '`' && p[1] == '=' && (invo->argFlags & XFILE)) {
         p += 2;
         (void)skip_expr(&p, NULL);
         if (*p == ZERO)      //stop at ZERO after CTRL-V
            break;
      }

      //Check for '//': start of comment or '|': next command :redir @" doesn't either.
      ei ((isComment(p) && !(invo->argFlags & NOTRLCOM))
         || (*p == '|' && invo->id != C_append && invo->id != C_change && invo->id != C_insert)
         || *p == '\n'
      ){
         //We remove the '\' before the '|', unless CTRLV is used AND 'b' is present in 'cpoptions'.
         if (*(p - 1) == '\\') {
            if (!keep_backslash) {
                STRMOVE(p - 1, p);   //remove the '\'
                --p;
            }
         } else {
            *p = ZERO;
            break;
         }
      }
   }

   if (!(invo->argFlags & NOTRLCOM))   //remove trailing spaces
      del_trailing_spaces(invo->arg);
}

//get + command from command argument
private CS
getargcmd(OUT CS* argp) {
   CS arg = *argp;
   CS command = NULL;

   if (*arg == '+') {      //+[command]
      ++arg;
      if (isSpace(*arg) || *arg == ZERO)
          command = dollar_command;
      else {
          command = arg;
          arg = skip_cmd_arg(command, true);
          if (*arg != ZERO)
         *arg++ = ZERO;      //terminate command with ZERO
      }

      arg = skipwhite(arg);   //skip over spaces
      *argp = arg;
   }
   return command;
}

//Find end of "+command" argument.  Skip over "\ " and "\\".
pub CS
skip_cmd_arg(CS p, int rembs) {   //true to halve the number of backslashes
   while (*p && !isSpace(*p)) {
      if (*p == '\\' && p[1] != ZERO) {
         if (rembs)
            STRMOVE(p, p + 1);
         else
            ++p;
      }
      MB_PTR_ADV(p);
   }
   return p;
}

pub int
get_bad_opt(CS p, Invocation* invo) {
   if (caseInsensitiveCompare(p, "keep") == 0)
      invo->bad_char = BAD_KEEP;
   ei (caseInsensitiveCompare(p, "drop") == 0)
      invo->bad_char = BAD_DROP;
   ei (utf8CharLens[*p] == 1 && p[1] == ZERO)
      invo->bad_char = *p;
   else
      return FAIL;
   return OK;
}

//Function given to expandGeneric() to obtain the list of bad= names.
private CS
get_bad_name(Expand*, int idx) {
   //Note: Keep this in sync with getargopt.
   static CS p_bad_values[] = {
      S"?",
      S"keep",
      S"drop",
   };

   if (idx < 0 || idx >= (int)ARRAY_LENGTH(p_bad_values))
      return NULL;

   return p_bad_values[idx];
}

//Get "++opt=arg" argument. Return FAIL or OK.
private int
getargopt(Invocation* invo) {
   CS arg = invo->arg + 2;
   int      *pp = NULL;
   int      bad_char_idx;
   //Note: Keep this in sync with get_argoname.

   //":edit ++[no]bin[ary] file"
   if (STRNCMP(arg, "bin", 3) == 0 || STRNCMP(arg, "nobin", 5) == 0) {
      if (*arg == 'n') {
          arg += 2;
          invo->force_bin = FORCE_NOBIN;
      } else
         invo->force_bin = FORCE_BIN;
      if (!checkforcmd(&arg, S"binary", 3))
         return FAIL;
      invo->arg = skipwhite(arg);
      return OK;
   }

   //":read ++edit file"
   if (STRNCMP(arg, "edit", 4) == 0) {
      invo->read_edit = true;
      invo->arg = skipwhite(arg + 4);
      return OK;
   }

   if (STRNCMP(arg, "bad", 3) == 0) {
      arg += 3;
      pp = &bad_char_idx;
   }

   if (!pp || *arg != '=')
      return FAIL;

   ++arg;
   *pp = (int)(arg - invo->comm);
   arg = skip_cmd_arg(arg, false);
   invo->arg = skipwhite(arg);
   *arg = ZERO;

   //Check ++bad= argument.  Must be a single-byte character, "keep" or "drop".
   if (get_bad_opt(invo->comm + bad_char_idx, invo) == FAIL)
      return FAIL;

   return OK;
}

//Function given to expandGeneric() to obtain the list of ++opt names.
private CS
get_argoname(Expand*, int idx) {
   //Note: Keep this in sync with getargopt.
   static CS p_opt_values[] = {SMAP((CS),
      "encoding=",
      "binary",
      "nobinary",
      "bad=",
      "edit"
   )};

   if (idx < 0 || idx >= (int)ARRAY_LENGTH(p_opt_values))
      return NULL;

   return p_opt_values[idx];
}

//Command-line expansion for ++opt=name.
pub int
expand_argopt(
   CS pat,
   Expand* xp,
   RegMatch* rmp,
   OUT ExpandMatch* matches
) {
   if (xp->input.c > xp->fullInput && *(xp->input.c - 1) == '=') {
      Byte *(*cb)(Expand *, int) = NULL;
      CS name_end = xp->input.c - 1;
      if (name_end - xp->fullInput >= 3 && STRNCMP(name_end - 3, "bad", 3) == 0)
         cb = get_bad_name;

      if (cb) {
         return expandGeneric(
             pat, xp, rmp, cb, false, OUT matches
         );
      }
      return FAIL;
   }

   return expandGeneric(
       pat,
       xp,
       rmp,
       get_argoname,
       false,
       OUT matches
   );
}

//}}}
//{{{misc 1

pub void
c_autocmd(Invocation* invo) {
   //Disallow autocommands from .exrc and .vimrc in current directory for security reasons.
   if (invo->id == C_autocmd)
      do_autocmd(invo->arg, invo->forceit);
   else
      do_augroup(invo->arg, invo->forceit);
}

//":doautocmd": Apply the automatic commands to the current book.
pub void
c_doautocmd(Invocation* invo) {
   CS arg = invo->arg;
   Boole did_aucmd;

   (void)do_doautocmd(arg, true, OUT &did_aucmd);
}

//:[N]bunload[!] [N] [bookname] unload book
//:[N]bdelete[!] [N] [bookname] delete book from book list
//:[N]bwipeout[!] [N] [bookname] delete book really
pub void
c_bunload(Invocation* invo) {
   if (portErrorIfPopup(true))
      return;
   invo->errmsg = do_bufdel(
       invo->id == C_bdelete 
       ? DOBOOK_DEL : (invo->id == C_bwipeout ? DOBOOK_WIPE : DOBOOK_UNLOAD), invo->arg,
       invo->addr_count, (int)invo->line1, (int)invo->line2, invo->forceit);
}

//:[N]book [N]   to book N
//:[N]sbook [N]  to book N
pub void
c_book(Invocation* invo) {
   if (portErrorIfPopup(true))
      return;
   do_exbuffer(invo);
}

//":book" command and alike.
private void
do_exbuffer(Invocation* invo) {
   if (*invo->arg)
      invo->errmsg = ex_errmsg(e_trailing_characters_str, invo->arg);
   else {
      if (invo->addr_count == 0)   //default is current book
         bookGoto(invo, DOBOOK_CURRENT, FORWARD, 0);
      else
         bookGoto(invo, DOBOOK_FIRST, FORWARD, (int)invo->line2);
      if (invo->higherOrderComm)
         do_cmd_argument(invo->higherOrderComm);
   }
}

//:[N]bmodified [N]   to next mod. book
//:[N]sbmodified [N]   to next mod. book
pub void
c_bmodified(Invocation* invo) {
   bookGoto(invo, DOBOOK_MOD, FORWARD, (int)invo->line2);
   if (invo->higherOrderComm)
      do_cmd_argument(invo->higherOrderComm);
}

//:[N]bnext [N]   to next book
//:[N]sbnext [N]   split and to next book
pub void
c_bnext(Invocation* invo){
   if (portErrorIfPopup(true))
      return;

   bookGoto(invo, DOBOOK_CURRENT, FORWARD, (int)invo->line2);
   if (invo->higherOrderComm)
      do_cmd_argument(invo->higherOrderComm);
}

//:[N]bNext [N]   to previous book
//:[N]bprevious [N]   to previous book
//:[N]sbNext [N]   split and to previous book
//:[N]sbprevious [N]   split and to previous book
pub void
c_bprevious(Invocation* invo) {
   if (portErrorIfPopup(true))
      return;

   bookGoto(invo, DOBOOK_CURRENT, BACKWARD, (int)invo->line2);
   if (invo->higherOrderComm)
      do_cmd_argument(invo->higherOrderComm);
}

//:brewind      to first book
//:bfirst      to first book
//:sbrewind      split and to first book
//:sbfirst      split and to first book
pub void
c_brewind(Invocation* invo) {
   if (portErrorIfPopup(true))
      return;

   bookGoto(invo, DOBOOK_FIRST, FORWARD, 0);
   if (invo->higherOrderComm)
      do_cmd_argument(invo->higherOrderComm);
}

//:blast      to last book
//:sblast      split and to last book
pub void
c_blast(Invocation* invo) {
   if (portErrorIfPopup(true))
      return;

   bookGoto(invo, DOBOOK_LAST, BACKWARD, 0);
   if (invo->higherOrderComm)
      do_cmd_argument(invo->higherOrderComm);
}

//Return the next command, after the first '|' or '\n'. NULL if not found.
pub CS
find_nextcmd(CS p) {
   while (*p != '\n') {
      if (*p == ZERO)
         return NULL;
      ++p;
   }
   return (p + 1);
}

//Function given to expandGeneric() to obtain the list of command names.
pub CS
get_command_name(Expand *, int idx) {
   if (idx >= (int)COUNT_COMMANDS)
      return expand_user_command_name(idx);
      //the following are not real commands
   if (STRNCMP(commands[idx].name, "{", 1) == 0 || STRNCMP(commands[idx].name, "}", 1) == 0)
      return S"";
   return commands[idx].name;
}

pub void
c_hilite(Invocation* invo) {
   if (*invo->arg == ZERO && invo->comm[2] == '!')
      msg(_("Greetings, Eegl user!"));
   doHilite(invo->arg, invo->forceit, false);
}


//Call this function if we thought we were going to exit, but we won't
//(because of an error). May need to restore the terminal mode.
pub void
not_exiting(void) {
   isExitingG = false;
   termSetMode(TMODE_RAW);
}

private int
before_quit_autocmds(Portal *po, int quit_all) {
   applyAutocomms(EVENT_QUITPRE, NULL, NULL, false, po->book);

   //Bail out when autocommands closed the portal. Refuse to quit when the book in the last 
   //portal is being closed (can only happen in autocommands).
   if (!portalIsValid(po)
          || curBookLocked()
          || (po->book->countPortals == 1 && po->book->locked > 0))
      return true;

   if (quit_all) {
      applyAutocomms(EVENT_EXITPRE, NULL, NULL, false, curBook);
      //Refuse to quit when locked or when the portal was closed or the book in the last portal 
      //is being closed (can only happen in autocommands).
      if (!portalIsValid(po) || curBookLocked()
              || (curBook->countPortals == 1 && curBook->locked > 0))
          return true;
   }

   return false;
}

//":quit": quit current portal, quit Eegl if the last portal is closed.
//":{nr}quit": quit portal {nr}
//Also used when closing a terminal portal that's the last one.
pub void
c_quit(Invocation* invo) {
    Portal   *po;

   if (commPortTypeG != 0) {
      commPortResultG = Ctrl_C;
      return;
   }
   //Don't quit while editing the command line.
   if (text_locked()) {
      text_locked_msg();
      return;
   }
   if (invo->addr_count > 0) {
      int   wnr = invo->line2;

      for (po = firstPor; po->next != NULL; po = po->next)
         if (--wnr <= 0)
            break;
    } else
      po = curPor;

   //Refuse to quit when locked.
   if (curBookLocked())
      return;

   //Trigger QuitPre and maybe ExitPre
   if (before_quit_autocmds(po, false))
      return;

   //If there is only one relevant portal, we will exit.
   if (onlyOnePortal())
      isExitingG = true;
   if (onlyOnePortal() && check_changed_any(invo->forceit, true)) {
      not_exiting();
   } else {
      //quit last portal
      //Note: onlyOnePortal() returns true, even if a help portal is still open. In that case 
      //only quit, if no address has been specified. Example:
      //:h|wincmd w|1q     - don't quit
      //:h|wincmd w|q      - quit
      if (onlyOnePortal() && (ONLY_ONE_PORTAL || invo->addr_count == 0))
         exitEegl(0);
      not_exiting();
      //close portal; may free book
      closePortal(po, invo->forceit);
    }
}

//":cquit".
pub void
c_cquit(Invocation* invo) {
   //this does not always pass on the exit code to the Manx compiler. why?
   exitEegl(invo->addr_count > 0 ? (int)invo->line2 : EXIT_FAILURE);
}

//Do preparations for "qall" and "wqall". Return FAIL when quitting should be aborted.
pub int
before_quit_all(Invocation* invo) {
   if (commPortTypeG != 0) {
      if (invo->forceit)
          commPortResultG = K_XF1;   //ex_window() takes care of this
      else
          commPortResultG = K_XF2;
      return FAIL;
   }

    //Don't quit while editing the command line.
   if (text_locked()) {
      text_locked_msg();
      return FAIL;
   }

   if (before_quit_autocmds(curPor, true))
      return FAIL;

   return OK;
}

//":qall": try to quit all portals
pub void
c_quit_all(Invocation* invo) {
   if (before_quit_all(invo) == FAIL)
      return;
   isExitingG = true;
   if (invo->forceit || !check_changed_any(false, false))
      exitEegl(0);
   not_exiting();
}

//":close": close current portal; if it is the last one, close the program
pub void
c_close(Invocation* invo) {
   if (commPortTypeG != 0)
      commPortResultG = Ctrl_C;
   ei (!text_locked() && !curBookLocked()) {
      if (invo->addr_count == 0)
         closePortalInternal(curPor, NULL);
      else {
         Portal   *port;
         int      portNr = 0;
         FOR_ALL_PORTALS(port) {
            portNr++;
            if (portNr == invo->line2)
               break;
         }
         if (port == NULL)
            port = lastPor;
         closePortalInternal(port, NULL);
      }
   }
}

//}}}
//{{{tabs & portals, closing & openin'

//callback function for 'findfunc'
private Callback findFnCb;


//":pclose": Close any preview portal.
pub void
c_pclose(Invocation*) {
   Portal* port;

   //First close any normal portal.
   FOR_ALL_PORTALS(port) {
      if (port->isPreview) {
         closePortalInternal(port, NULL);
         return;
      }
   }
   //Also when 'previewpopup' is empty, it might have been cleared.
   popup_close_preview();
}


//Close portal "port" and take care of handling closing the last portal into a modified book.
private void
closePortalInternal(Portal* port, Tab* t) {     //NULL or the tab "port" is in
   //Never close the autocommand portal.
   if (is_autoCommPort(port)) {
      emsg(_(e_cannot_close_autocmd_or_popup_portal));
      return;
   }
   if (portalLayout_locked(C_close))
      return;


   //free book when not hiding it or when it's a scratch book
   if (lastPortal())
      exitEegl(0);
   ei (t == NULL)
      closePortal(port, false);
   else
      closePortal_othertab(port, false, t);
}

//Handle the argument for a tab-related command. Return a tab number.
//When an error is encountered then invo->errmsg is set.
private int
getTabRelatedArg(Invocation* invo) {
   int tabId;
   int unaccept_arg0 = (invo->id == C_tabmove) ? 0 : 1;

   if (invo->arg && *invo->arg != ZERO) {
      CS p = invo->arg;
      CS p_save;
      int    relative = 0; //argument +N/-N means: go to N places to the
                 //right/left relative to the current position.

      if (*p == '-') {
         relative = -1;
         p++;
      } ei (*p == '+') {
         relative = 1;
         p++;
      }

      p_save = p;
      tabId = parseLong(&p);

      if (relative == 0) {
         if (STRCMP(p, "$") == 0)
            tabId = LAST_TAB_NR;
         ei (STRCMP(p, "#") == 0) {
            if (isTabValid(lastUsedTabG))
               tabId = indexOfTab(lastUsedTabG);
            else {
               invo->errmsg = ex_errmsg(e_invalid_value_for_argument_str, invo->arg);
               tabId = 0;
               goto theend;
            }
         } ei (p == p_save || *p_save == '-' || *p != ZERO || tabId > LAST_TAB_NR) {
            //No numbers as argument.
            invo->errmsg = ex_errmsg(e_invalid_argument_str, invo->arg);
            goto theend;
         }
      } else {
         if (*p_save == ZERO)
            tabId = 1;
         ei (p == p_save || *p_save == '-' || *p != ZERO || tabId == 0) {
            //No numbers as argument.
            invo->errmsg = ex_errmsg(e_invalid_argument_str, invo->arg);
            goto theend;
         }
         tabId = tabId * relative + indexOfTab(curtab);
         if (!unaccept_arg0 && relative == -1)
            --tabId;
      }
      if (tabId < unaccept_arg0 || tabId > LAST_TAB_NR)
         invo->errmsg = ex_errmsg(e_invalid_argument_str, invo->arg);
    }
   ei (invo->addr_count > 0) {
      if (unaccept_arg0 && invo->line2 == 0) {
         invo->errmsg = _(e_invalid_range);
         tabId = 0;
      } else {
         tabId = invo->line2;
         if (!unaccept_arg0) {
            CS cmdp = invo->comm;

            while (--cmdp > *invo->commline
               && (SPACE_OR_TAB(*cmdp) || EE_ISDIGIT(*cmdp)))
                ;
            if (*cmdp == '-') {
               --tabId;
               if (tabId < unaccept_arg0)
                  invo->errmsg = _(e_invalid_range);
            }
         }
      }
   } else {
      switch (invo->id) {
      case C_tabnext:
         tabId = indexOfTab(curtab) + 1;
         if (tabId > LAST_TAB_NR)
            tabId = 1;
         break;
      case C_tabmove:
         tabId = LAST_TAB_NR;
         break;
      default:
         tabId = indexOfTab(curtab);
      }
   }

theend:
   return tabId;
}

//":tabclose": close current tab, unless it is the last one. ":tabclose N": close tab N.
pub void
c_tabclose(Invocation* invo) {
   if (commPortTypeG != 0) {
      commPortResultG = K_IGNORE;
      return;
   }

   if (firstTabG->next == NULL) {
      emsg(_(e_cannot_close_last_tab_page));
      return;
   }

   if (portalLayout_locked(C_tabclose))
      return;

   int tabId = getTabRelatedArg(invo);
   if (invo->errmsg != NULL)
      return;

   Tab* t = getTab(tabId);
   if (!t) {
      inpFlushIfNotSilent();
      return;
   }
   if (t != curtab) {
      tabCloseOther(t);
      return;
   } ei (!text_locked() && !curBookLocked())
      tabClose();
}

//":tabonly": close all tabs except the current one
pub void
c_tabonly(Invocation* invo) {
   if (commPortTypeG != 0) {
      commPortResultG = K_IGNORE;
      return;
   }

   if (firstTabG->next == NULL) {
      msg(_("Already only one tab"));
      return;
   }

   if (portalLayout_locked(C_tabonly))
      return;

   int tabId = getTabRelatedArg(invo);
   if (invo->errmsg)
      return;

   gotoTabById(tabId);
   //Repeat this up to a 1000 times, because autocommands may mess up the lists.
   for (int done = 0; done < 1000; ++done) {
      Tab* t;
      FOR_ALL_TABS(t) {
         if (t->topframe != topframeG) {
            tabCloseOther(t);
            //if we failed to close it quit
            if (isTabValid(t))
               done = 1000;
            //start over, "t" is now invalid
            break;
         }
      } 
      if (firstTabG->next == NULL)
         break;
   }
}

//Close the current tab
pub void
tabClose() {
   if (portalLayout_locked(C_tabclose))
      return;

   trigger_tabclosedpre(curtab, true);

   //First close all the portals but the current one.  If that worked then
   //close the last portal in this tab, that will close it.
   if (!ONLY_ONE_PORTAL)
      portCloseOthers(true);
   if (ONLY_ONE_PORTAL)
      closePortalInternal(curPor, NULL);
}

//Close tab "p", which is not the current tab. Note that autocommands may make "t" invalid.
pub void
tabCloseOther(Tab *t) {
   int      done = 0;
   trigger_tabclosedpre(t, true);

   //Limit to 1000 portals, autocommands may add a portal while we close one. OK, so I'm paranoid..
   while (++done < 1000) {
      Portal* po = t->firstPor;
      closePortalInternal(po, t);

      //Autocommands may delete the tab under our fingers and we may
      //fail to close a portal with a modified book.
      if (!isTabValid(t) || t->firstPor == po)
          break;
   }

   applyAutocomms(EVENT_TABCLOSED, NULL, NULL, false, curBook);
}

//":only".
pub void
c_only(Invocation* invo) {
   if (portalLayout_locked(C_only))
      return;
   if (invo->addr_count > 0) {
      Portal* po;
      int wnr = invo->line2;
      for (po = firstPor; --wnr > 0; ) {
         if (!po->next)
            break;
         else
            po = po->next;
      }
      gotoPortal(po);
   }
   portCloseOthers(true);
}

pub void
c_hide(Invocation* invo) {
   //":hide" or ":hide | cmd": hide current portal
   if (invo->skip)
      return;

   if (portalLayout_locked(C_hide))
   return;
   if (invo->addr_count == 0)
   closePortal(curPor, false);   //don't free book
    else {
      int winnr = 0;
      Portal* po;

      FOR_ALL_PORTALS(po) {
         winnr++;
         if (winnr == invo->line2)
            break;
      }
      if (!po)
         po = lastPor;
      closePortal(po, false);
   }
}

//":exit", ":xit" and ":wq": Write file and quit the current portal.
pub void
c_exit(Invocation* invo) {
   if (commPortTypeG != 0) {
      commPortResultG = Ctrl_C;
      return;
   }
   //Don't quit while editing the command line.
   if (text_locked()) {
      text_locked_msg();
      return;
   }

   //we plan to exit if there is only one relevant portal
   if (onlyOnePortal())
      isExitingG = true;

   //Write the book for ":wq" or when it was changed.
   //Trigger QuitPre and ExitPre.
   //Check if we can exit now, after autocommands have changed things.
   if (((invo->id == C_wq || bookWasChanged(curBook)) && do_write(invo) == FAIL)
       || before_quit_autocmds(curPor, false)
       || (onlyOnePortal() && check_changed_any(invo->forceit, false))
   ) {
      not_exiting();
   } else {
      if (onlyOnePortal())       //quit last portal, exit Eegl
         exitEegl(0);
      not_exiting();
      //Quit current portal, may free the book.
      closePortal(curPor, false);
   }
}

//":print", ":list", ":number".
pub void
c_print(Invocation* invo) {
   if (curBook->mem.flags & ML_EMPTY)
      emsg(_(e_empty_buffer));
   else {
      for ( ;!gotInterruptG; ui_breakcheck()) {
         print_line(invo->line1, invo->id == C_list || (invo->flags & EXFLAG_LIST));
         if (++invo->line1 > invo->line2)
            break;
         out_flush();       //show one line at a time
      }
      setpcmark();
      //put cursor at last line
      curPor->cursor.lnum = invo->line2;
      beginline(BL_SOL | BL_FIX);
   }

   ex_no_reprint = true;
}

pub void
c_goto(Invocation* invo) {
   goto_byte(invo->line2);
}

//":shell".
pub void
c_shell(Invocation*) {
   do_shell(NULL, 0);
}

//":preserve".
pub void
c_preserve(Invocation*) {
   curBook->flags |= BF_PRESERVED;
   ml_preserve(curBook, true);
}

//":recover".
pub void
c_recover(Invocation* invo) {
   //Set recoveryModeG right away to avoid the ATTENTION prompt.
   recoveryModeG = true;
   if (!check_changed(curBook, (p_awa ? CCGD_AW : 0)
              | CCGD_MULTWIN
              | (invo->forceit ? CCGD_FORCEIT : 0)
              | CCGD_EXCMD)
          && (*invo->arg == ZERO || setfname(curBook, invo->arg, NULL, true) == OK)
   )
      ml_recover(true);
   recoveryModeG = false;
}

//Command modifier used in a wrong way.  Also for other commands that can't appear at the toplevel
pub void
c_wrongmodifier(Invocation* invo) {
   invo->errmsg = ex_errmsg(e_invalid_command_str, invo->comm);
}

//Call 'findfunc' to obtain a list of file names.
private List *
call_findfunc(CS pat, int cmdcomplete) {
   Var   args[3];
   Var   returnVar;
   ScriptPos   saved_sctx = scriptPosG;
   args[0].tag = VAR_STRING;
   args[0].string = pat;
   args[1].tag = VAR_BOOL;
   args[1].number = cmdcomplete;
   args[2].tag = VAR_UNKNOWN;

   //Lock the text to prevent weird things from happening. Also disallow switching to another 
   //portal, it should not be needed and may end up in Insert mode in another book.
   ++textlock;

   ScriptPos* ctx = optGetScriptPos(S"findfunc");
   if (ctx)
      scriptPosG = *ctx;

   Callback* cb = curBook->o.findFn;
   int retval = call_callback(cb, -1, &returnVar, 2, args);

   scriptPosG = saved_sctx;

   --textlock;

   List *retlist = NULL;

   if (retval == OK) {
      if (returnVar.tag == VAR_LIST)
         retlist = list_copy(returnVar.list, false, false, get_copyID());
      else
         emsg(_(e_invalid_return_type_from_findfunc));

      clearVar(&returnVar);
   }

   return retlist;
}

//Find file names matching "pat" using @findfunc and return it in "files".
//Used for expanding the :find, :sfind and :tabfind command argument.
//Return OK on success and FAIL otherwise.
pub int
expand_findfunc(CS pat, OUT ExpandMatch* matches) {
   List* l = call_findfunc(pat, VVAL_TRUE);
   if (!l)
      return FAIL;

   int len = list_len(l);
   if (len == 0)       //empty List
      return FAIL;

   //Copy all the List items
   ListItem *li;
   FOR_ALL_LIST_ITEMS(l, li) {
      if (li->c.tag == VAR_STRING) {
         addExpandMatch(copyStr(li->c.string), OUT matches);
      }
   }
   list_free(l);

   return OK;
}

//Use 'findfunc' to find file 'findarg'. The 'count' argument is used to find the n'th matching file
private CS
findFnFindFile(CS findarg, int findarg_len, int count) {
   List* fname_list;
   CS ret_fname = NULL;
   int fname_count;

   Byte cc = findarg[findarg_len];
   findarg[findarg_len] = ZERO;

   fname_list = call_findfunc(findarg, VVAL_FALSE);
   fname_count = list_len(fname_list);

   if (fname_count == 0)
      showErrFmtMsg(_(e_cant_find_file_str_in_path), findarg);
   else {
      if (count > fname_count)
          showErrFmtMsg(_(e_no_more_file_str_found_in_path), findarg);
      else {
         ListItem *li = list_find(fname_list, count - 1);
         if (li && li->c.tag == VAR_STRING)
            ret_fname = copyStr(li->c.string);
      }
   }

   if (fname_list != NULL)
   list_free(fname_list);

    findarg[findarg_len] = cc;

    return ret_fname;
}

//Setter for the @findfunc option. Return NULL on success or an error message on failure.
pub CS
setFindFn(OptionChange* cha) {
   int   retval;

   if (cha->setScope == SET_LOCAL) {
      //book-local option set
      updateStringRef(cha);
      retval = optSetCallback(OUT curBook->o.findFn, cha->newVal.string);
   } else {
      //global option set
      retval = optSetCallback(OUT &findFnCb, *cha->ref.string);
      //when using :set, free the local callback
      if (cha->setScope == SET_GLOBAL)
         evFreeCallback(curBook->o.findFn);
   }

   if (retval == FAIL)
      return e_invalid_argument;

   //If the option value starts with <SID> or s:, then replace that with the script identifier.
   OptionRef ref = cha->ref;
   CS name = get_scriptlocal_funcname(*ref.string);
   if (name) {
      *ref.string = name;
   }

   return NULL;
}

# if defined(EXITFREE)
pub void
doFreeFindFnOption(void) {
   evFreeCallback(&findFnCb);
}
# endif

//Mark the global @findfunc callback with "copyID" so that it is not garbage collected.
pub int
set_ref_in_findfunc(int copyID) {
   int abort = memSetRefInCallback(&findFnCb, copyID);
   return abort;
}

//:sview [+command] file   split portal with new file, read-only
//:split [[+command] file]   split portal with current or new file
//:vsplit [[+command] file]   split portal vertically with current or new file
//:new [[+command] file]   split portal with no or new file
//:vnew [[+command] file]   split vertically portal with no or new file
//:sfind [+command] file   split portal with file in 'path'
//
//:tabedit         open new Tab with empty portal
//:tabedit [+command] file   open new Tab and edit "file"
//:tabnew [[+command] file]   just like :tabedit
//:tabfind [+command] file   open new Tab and find "file"
pub void
c_splitview(Invocation* invo) {
   Portal* old_curPor = curPor;
   CS fname = null;
   Boole use_tab = invo->id == C_tabedit
             || invo->id == C_tabfind
             || invo->id == C_tabnew;

   if (portErrorIfPopup(true))
      return;

   //A ":split" in the location portal works like ":new".  Don't want two
   //location portals.  But it's OK when doing ":tab split".
   if (isLocationListBook(curBook) && commModifierG.cmod_tab == 0) {
      if (invo->id == C_split)
         invo->id = C_new;
      if (invo->id == C_vsplit)
         invo->id = C_vnew;
   }

   if (invo->id == C_sfind || invo->id == C_tabfind) {
      CS file_to_find = NULL;
      FileSearchCtx* search_ctx = NULL;

      if (curBook->o.findFn) {
          fname = findFnFindFile(invo->arg, (int)STRLEN(invo->arg),
                      invo->addr_count > 0 ? invo->line2 : 1);
      } else {
         fname = findFileInPath(
                mbText(invo->arg), FNAME_MESS, true, curBook->fullFileName, 
                OUT &file_to_find, OUT &search_ctx
         );
         eeglFree(file_to_find);
         eeFindFile_cleanup(search_ctx);
      }
      if (!fname)
         goto theend;
      invo->arg = fname;
   }
   //Either open new tab or split the portal
   if (use_tab) {
      if (portNewTab(commModifierG.cmod_tab != 0 ? commModifierG.cmod_tab
             : invo->addr_count == 0 ? 0 : (int)invo->line2 + 1) != FAIL) {
         do_exedit(invo, old_curPor);

         //set the alternate book for the portal we came from
         if (curPor != old_curPor
               && portalIsValid(old_curPor)
               && old_curPor->book != curBook
               && (commModifierG.cmod_flags & CMOD_KEEPALT) == 0)
            old_curPor->altFnum = curBook->fiNum;
      }
   } ei (splitPortal(
             invo->addr_count > 0 ? (int)invo->line2 : 0, *invo->comm == 'v' ? WSP_VERT : 0
          ) != FAIL
   ) {
      //Disable @diff when editing another file, but keep it when doing ":split" without arguments.
      if (*invo->arg != ZERO)
          curPor->o.diff = false;
      else
          normPostProcessScrollbind(false);
      do_exedit(invo, old_curPor);
   }

theend:
   eeglFree(fname);
}

//Open a new tab.
pub void
tabNew(void) {
   Invocation   invo;
   CLEAR_FIELD(invo);
   invo.id = C_tabnew;
   invo.comm = (CS)"tabn";
   invo.arg = (CS)"";
   c_splitview(&invo);
}

//:tabnext command
pub void
c_tabnext(Invocation* invo) {
   int tabId;

   if (portErrorIfPopup(false))
      return;
   switch (invo->id) {
   case C_tabfirst:
   case C_tabrewind:
       gotoTabById(1);
       break;
   case C_tablast:
       gotoTabById(9999);
       break;
   case C_tabprevious:
   case C_tabNext:
      if (invo->arg && *invo->arg != ZERO) {
         CS p = invo->arg;
         CS p_save = p;

         tabId = parseLong(&p);
         if (p == p_save || *p_save == '-' || *p != ZERO || tabId == 0) {
            //No numbers as argument.
            invo->errmsg = ex_errmsg(e_invalid_argument_str, invo->arg);
            return;
         }
      } else {
         if (invo->addr_count == 0)
             tabId = 1;
         else {
             tabId = invo->line2;
             if (tabId < 1) {
            invo->errmsg = _(e_invalid_range);
            return;
             }
         }
      }
      gotoTabById(-tabId);
      break;
   default: //C_tabnext
      tabId = getTabRelatedArg(invo);
      if (invo->errmsg == NULL)
         gotoTabById(tabId);
      break;
   }
}

pub void
c_tabmove(Invocation* invo) {
   int tabId = getTabRelatedArg(invo);
   if (invo->errmsg == NULL)
      moveTab(tabId);
}

//:tabs command: List tabs and their contents.
pub void
c_tabs(Invocation*) {
   Portal* po;
   int tabcount = 1;

   msg_start();
   msg_scroll = true;
   for (Tab* t = firstTabG; t && !gotInterruptG; t = t->next) {
      msg_putchar('\n');
      eeSnprintf(IObuff, IOSIZE, _("Tab %d"), tabcount++);
      msgOuttransDeco(IObuff, getDecoFlags(HLF_T));
      out_flush();       //output one line at a time
      ui_breakcheck();

      if (t  == curtab)
         po = firstPor;
      else
         po = t->firstPor;
      for ( ; po && !gotInterruptG; po = po->next) {
         msg_putchar('\n');
         msg_putchar(po == curPor ? '>' : ' ');
         msg_putchar(' ');
         msg_putchar(bookWasChanged(po->book) ? '+' : ' ');
         msg_putchar(' ');
         if (bookSpName(po->book) != NULL)
            copySubstrToAllocation(OUT IObuff, (Text){bookSpName(po->book), IOSIZE - 1});
         ei (po->book->kind == BOOK_HELP) { 
            strPrintShortName(po->book->currFileName, IObuff, IOSIZE);
         } else
            home_replace(po->book->currFileName, IObuff, IOSIZE, true);
         msg_outtrans(IObuff);
         out_flush();       //output one line at a time
         ui_breakcheck();
      }
   }
}

//}}}
//{{{misc2

//":mode": Set screen mode. If no argument given, just get the screen size and redraw.
pub void
c_mode(Invocation* invo) {
   if (*invo->arg == ZERO)
      shell_resized();
   else
      emsg(_(e_screen_mode_setting_not_supported));
}

//":resize". set, increment or decrement current portal height
pub void
c_resize(Invocation* invo) {
   int      n;
   Portal   *po = curPor;

   if (invo->addr_count > 0) {
      n = invo->line2;
      for (po = firstPor; po->next && --n > 0; po = po->next)
          ;
   }

   n = atol((char *)invo->arg);
   if (commModifierG.cmod_split & WSP_VERT) {
      if (*invo->arg == '-' || *invo->arg == '+')
          n += po->width;
      ei (n == 0 && invo->arg[0] == ZERO)   //default is very wide
          n = 9999;
      portSetWidth(n, po);
   } else {
      if (*invo->arg == '-' || *invo->arg == '+')
         n += po->height;
      ei (n == 0 && invo->arg[0] == ZERO)   //default is very high
         n = 9999;
      portSetHeight(n, po);
   }
}

//":find [+command] <file>" command.
pub void
c_find(Invocation* invo) {
   if (!portCheckCanSetCurBookForceIt(invo->forceit))
      return;

   CS fname = NULL;
   int count;
   CS file_to_find = NULL;
   FileSearchCtx* search_ctx = NULL;

   if (curBook->o.findFn) {
      fname = findFnFindFile(
          invo->arg, (int)STRLEN(invo->arg), invo->addr_count > 0 ? invo->line2 : 1
      );
   } else {
      fname = findFileInPath(
         mbText(invo->arg), FNAME_MESS, true, curBook->fullFileName, 
         OUT &file_to_find, OUT &search_ctx
      );
      
      if (invo->addr_count > 0) {
         //Repeat finding the file "count" times. This matters when it appears
         //several times in the path.
         count = invo->line2;
         while (fname && --count > 0) {
            eeglFree(fname);
            fname = findFileInPath(
               (Text){NULL, 0}, FNAME_MESS, false, curBook->fullFileName, 
               OUT &file_to_find, OUT &search_ctx
            );
         }
      }
      EE_CLEAR(file_to_find);
      eeFindFile_cleanup(search_ctx);
   }

   if (!fname)
      return;

   invo->arg = fname;
   do_exedit(invo, NULL);
   eeglFree(fname);
}

//":open" simulation: for now works just like ":visual".
pub void
c_open(Invocation* invo) {
   RegMatch   regmatch;
   CS p;

   curPor->cursor.lnum = invo->line2;
   beginline(BL_SOL | BL_FIX);
   if (*invo->arg == '/') {
      //":open /pattern/": put cursor in column found with pattern
      ++invo->arg;
      p = skip_regexp(invo->arg, '/', true);
      *p = ZERO;
      regmatch.regprog = compileRegexp(invo->arg, RE_MAGIC);
      if (regmatch.regprog) {
         //make a copy of the line, when searching for a mark it might be flushed
         CS line = copyStr(ml_get_curline());

         regmatch.rm_ic = p_ic;
         if (eeRegexec(&regmatch, line, (ColNr)0))
            curPor->cursor.col = (ColNr)(regmatch.startp[0] - line);
         else
            emsg(_(e_no_match));
         eeRegFree(regmatch.regprog);
         eeglFree(line);
      }
      //Move to the ZERO, ignore any other arguments.
      invo->arg += STRLEN(invo->arg);
   }
   check_cursor();

   invo->id = C_visual;
   do_exedit(invo, NULL);
}

//":edit", ":badd", ":balt", ":visual".
pub void
c_edit(Invocation* invo) {
   CS fullFName = invo->id == C_enew ? NULL : invo->arg;

   //Exclude commands which keep the portal's current book
   if ( invo->id != C_badd
          && invo->id != C_balt
          //All other commands must obey 'portfixbuf' / ! rules
          && (!isSameFile(0, fullFName) && !portCheckCanSetCurBookForceIt(invo->forceit))
   )
      return;
      
   if (invo->id == C_edit && STRCHR(invo->arg, ' ') != NULL) { //:e a.txt b.txt
      ArrayList names = splitBySpace(invo->arg);
      for (int i = 0; i < names.len; i++) {
         CS name = ((Arr(CS))names.c)[i];
         Invocation oneNameArg = *invo;
         oneNameArg.arg = name;
         do_exedit(&oneNameArg, NULL);
      }
      return;
   } else {
      do_exedit(invo, NULL);
   }
}

//":edit <file>" command and alike.
pub void
do_exedit(Invocation* invo, Portal* old_curPor) {      //curPor before doing a split or NULL
   if ((invo->id != C_pedit && portErrorIfPopup(false)) || portErrorIfTermPopup())
      return;

   if ((invo->id == C_new || invo->id == C_tabnew || invo->id == C_tabedit || invo->id == C_vnew)
         && *invo->arg == ZERO
   ) {
      //":new" or ":tabnew" without argument: edit a new empty book
      setpcmark();
      (void)startEditingFile(
         0, NULL, NULL, invo, ECMD_ONE, ECMD_HIDE + (invo->forceit ? ECMD_FORCEIT : 0),
         old_curPor ? null : curPor
      );
   } ei ((invo->id != C_split && invo->id != C_vsplit) || *invo->arg != ZERO) {
      //Can't edit another file when "textlock" or "curBookLock" is set.
      //Only ":edit" or ":script" can bring us here, others are stopped earlier.
      if (*invo->arg != ZERO && text_or_buf_locked())
         return;
      Boole modifiable = false;

      if (invo->id == C_enew) //immutability doesn't make sense in an empty book
         modifiable = true;
      if (invo->id != C_balt && invo->id != C_badd)
         setpcmark();
      if (startEditingFile(
           0, 
           (invo->id == C_enew ? NULL : invo->arg),
           NULL, invo, invo->higherOrderLnum,
           ECMD_HIDE | (invo->forceit ? ECMD_FORCEIT : 0)
                //after a split, we can use an existing book
                | (old_curPor ? ECMD_OLDBUF : 0)
                | (invo->id == C_badd ? ECMD_ADDBUF : 0)
                | (invo->id == C_balt ? ECMD_ALTBUF : 0)
                | (modifiable ? ECMD_MODIFIABLE : 0),
                old_curPor == NULL ? curPor : NULL
         ) == FAIL
      ){
         //Editing the file failed. If the portal was split, close it.
         if (old_curPor) {
            //Reset the error/interrupt/exception state here so that
            //aborting() returns false when closing a portal.
            Cleanup   cs;
            enter_cleanup(OUT &cs);
            closePortal(curPor, false);

            //Restore the error/interrupt/exception state if not discarded by a new aborting
            //error, interrupt, or uncaught exception.
            leave_cleanup(&cs);
         }
      } ei (curBook->o.modifiable && curBook->countPortals == 1) {
         //When editing an already visited book, @modifiable won't be set but the previous value 
         //is kept. With ":view" and ":sview" we want the file to be readonly, except when 
         //another portal is editing the same book.
         curBook->o.modifiable = false;
      }
   } else  {
      if (invo->higherOrderComm)
         do_cmd_argument(invo->higherOrderComm);
      check_arg_idx(curPor);
   }

   //if ":split file" worked, set alternate file name in old portal to new file
   if (old_curPor
          && *invo->arg != ZERO
          && curPor != old_curPor
          && portalIsValid(old_curPor)
          && old_curPor->book != curBook
          && (commModifierG.cmod_flags & CMOD_KEEPALT) == 0) {
      old_curPor->altFnum = curBook->fiNum;
   } 

   ex_no_reprint = true;
}

//":syncbind" forces all scrollbound portals to have the same relative offset.
//(1998-11-02 16:21:01  R. Edward Ralston <eralston@computer.org>)
pub void
c_syncbind(Invocation*) {
   Portal   *po;
   Portal   *save_curPor = curPor;
   Book   *save_curbuf = curBook;
   long   topline;
   long   y;
   LineNr   old_linenr = curPor->cursor.lnum;

   setpcmark();

   //determine max topline
   if (curPor->o.diff) {
      topline = curPor->topLine;
      FOR_ALL_PORTALS(po) {
         if (po->o.diff && po->book) {
            y = po->book->mem.lineCount - curPor->o.scrollOff;
            if (topline > y)
               topline = y;
          }
      }
      if (topline < 1)
         topline = 1;
   } else {
      topline = 1;
   }


   //Set all scrollbound portals to the same topline.
   FOR_ALL_PORTALS(curPor) {
      if (curPor->o.diff) {
         curBook = curPor->book;
         y = topline - curPor->topLine;
         if (y > 0)
            scrollup(y, true);
         else
            scrolldown(-y, true);
         curPor->scbindPos = topline;
         redraw_later(UPD_VALID);
         cursor_correct();
         curPor->statusLineNeedsRedraw = true;
      }
   }
   curPor = save_curPor;
   curBook = save_curbuf;
   if (curPor->o.diff) {
      did_syncbind = true;
      checkpcmark();
      if (old_linenr != curPor->cursor.lnum) {
         Byte ctrl_o[2] = {Ctrl_O, 0};
         insertIntoTypebuf(ctrl_o, REMAP_NONE, 0, true, false);
      }
   }
}

pub void
c_read(Invocation* invo) {
   if (invo->usefilter) {        //:r!cmd
      do_bang(1, invo, false, false, true);
      return;
   }

   if (u_save(invo->line2, (LineNr)(invo->line2 + 1)) == FAIL)
      return;

   int      i;
   if (*invo->arg == ZERO) {
       if (check_fname() == FAIL)   //check for no file name
      return;
       i = readfile(curBook->fullFileName, curBook->currFileName,
          invo->line2, (LineNr)0, (LineNr)MAXLNUM, invo, 0);
   } else {
       (void)setaltfname(invo->arg, invo->arg, (LineNr)1);
       i = readfile(invo->arg, NULL, invo->line2, (LineNr)0, (LineNr)MAXLNUM, invo, 0);

   }
   if (i != OK) {
      if (!aborting())
          showErrFmtMsg(_(e_cant_open_file_str), invo->arg);
   } else {
      drawCurBookLater(UPD_VALID);
   }
}

private CS prev_dir = NULL;

#if defined(EXITFREE)
pub void
free_cd_dir(void) {
   EE_CLEAR(prev_dir);
   EE_CLEAR(globaldir);
}
#endif

//Get the previous directory for the given chdir scope.
private CS
get_prevdir(CdScopeKind scope) {
   if (scope == CDSCOPE_WINDOW)
      return curPor->prevdir;
   ei (scope == CDSCOPE_TABPAGE)
      return curtab->prevdir;
   return prev_dir;
}

//Deal with the side effects of changing the current directory.
//When 'scope' is CDSCOPE_TABPAGE then this was after an ":tcd" command.
//When 'scope' is CDSCOPE_WINDOW then this was after an ":lcd" command.
pub void
post_chdir(CdScopeKind scope) {
   if (scope != CDSCOPE_WINDOW)
      //Clear tab local directory for both :cd and :tcd
      EE_CLEAR(curtab->localdir);
   EE_CLEAR(curPor->localDir);
   if (scope != CDSCOPE_GLOBAL) {
      CS pdir = get_prevdir(scope);

      //If still in the global directory, need to remember current
      //directory as the global directory.
      if (!globaldir && pdir)
         globaldir = copyStr(pdir);

      //Remember this local directory for the portal.
      if (mch_dirname(nameBuffG, MAXPATHL) == OK) {
         if (scope == CDSCOPE_TABPAGE)
            curtab->localdir = copyStr(nameBuffG);
         else
            curPor->localDir = copyStr(nameBuffG);
      }
   } else {
      //We are now in the global directory, no need to remember its name.
      EE_CLEAR(globaldir);
   }

   shorten_fnames(true);
}

//Trigger DirChangedPre for "acmd_fname" with directory "new_dir".
pub void
trigger_DirChangedPre(CS acmd_fname, CS new_dir) {
   applyAutocomms(EVENT_DIRCHANGEDPRE, acmd_fname, new_dir, false, curBook);
}

//Change directory function used by :cd/:tcd/:lcd commands and the
//chdir() function.
//scope == CDSCOPE_WINDOW: changes the portal-local directory
//scope == CDSCOPE_TABPAGE: changes the tab-local directory
//Otherwise: change the global directory
//Return true if the directory is successfully changed.
pub int
changedir_func(CS new_dir, CdScopeKind scope){
   CS pdir = NULL;
   int      dir_differs;
   CS acmd_fname = NULL;
   CS* pp;
   Byte   *tofree;

   if (new_dir == NULL || allbuf_locked()) {
       return false;
   }

   //":cd -": Change to previous directory
   if (STRCMP(new_dir, "-") == 0) {
      pdir = get_prevdir(scope);
      if (pdir == NULL) {
         emsg(_(e_no_previous_directory));
         return false;
      }
      new_dir = pdir;
   }

   //Save current directory for next ":cd -"
   pdir = (mch_dirname(nameBuffG, MAXPATHL) == OK) ? copyStr(nameBuffG) : null;

   //":cd" means: go to home directory.
   if (*new_dir == ZERO) {
      //use nameBuffG for home directory name
      doExpandEnv(OUT nameBuffTextG, S"$HOME");
      new_dir = nameBuffG;
   }
   dir_differs = pdir == NULL || pathcmp(pdir, new_dir, -1) != 0;
   if (dir_differs) {
      if (scope == CDSCOPE_WINDOW)
         acmd_fname = (CS)"window";
      ei (scope == CDSCOPE_TABPAGE)
         acmd_fname = (CS)"tabpage";
      else
         acmd_fname = (CS)"global";
      trigger_DirChangedPre(acmd_fname, new_dir);

      if (eeChdir(new_dir)) {
          emsg(_(e_command_failed));
          eeglFree(pdir);
          return false;
      }
   }

   if (scope == CDSCOPE_WINDOW)
      pp = &curPor->prevdir;
   ei (scope == CDSCOPE_TABPAGE)
      pp = &curtab->prevdir;
   else
      pp = &prev_dir;
   tofree = *pp;  //new_dir may use this
   *pp = pdir;

   post_chdir(scope);

   if (dir_differs)
      applyAutocomms(EVENT_DIRCHANGED, acmd_fname, new_dir, false, curBook);
   eeglFree(tofree);
   return true;
}

//":cd", ":tcd", ":lcd", ":chdir" ":tchdir" and ":lchdir".
pub void
c_cd(Invocation* invo) {
   CS new_dir = invo->arg;
   CdScopeKind   scope = CDSCOPE_GLOBAL;

   if (invo->id == C_lcd || invo->id == C_lchdir)
      scope = CDSCOPE_WINDOW;
   ei (invo->id == C_tcd || invo->id == C_tchdir)
      scope = CDSCOPE_TABPAGE;

   if (changedir_func(new_dir, scope) && (keyWasTypedG || p_verbose >= 5))
      //Echo the new current directory if the command was typed.
      c_pwd(invo);
}

//":pwd".
pub void
c_pwd(Invocation*) {
   if (mch_dirname(nameBuffG, MAXPATHL) == OK) {
      if (p_verbose > 0) {
         CS context = S"global";

         if (curPor->localDir)
            context = S"window";
         ei (curtab->localdir)
            context = S"tabpage";
         smsg("[%s] %s", context, (char *)nameBuffG);
      } else
         msg(nameBuffG);
   } else
      emsg(_(e_directory_unknown));
}

//":=".
pub void
c_equal(Invocation* invo) {
   smsg("%ld", (long)invo->line2);
   mayPrint(invo);
}

pub void
c_sleep(Invocation* invo) {
   int      n;
   long   len;

   if (cursor_valid()) {
      n = curPor->windowRow + curPor->cursorRow - msg_scrolled;
      if (n >= 0)
         windgoto(n, curPor->windowCol + curPor->cursorCol);
   }

   len = invo->line2;
   switch (*invo->arg) {
   case 'm': break;
   case ZERO: len *= 1000L; break;
   default: showErrFmtMsg(_(e_invalid_argument_str), invo->arg); return;
   }

   //Hide the cursor if invoked with !
   do_sleep(len, invo->forceit);
}

//Sleep for "msec" milliseconds, but keep checking for a CTRL-C every second.
//Hide the cursor if "hide_cursor" is true.
pub void
do_sleep(long msec, int hide_cursor) {
   long   done = 0;
   long   wait_now;
   Elapsed   start_tv;

   //Remember at what time we started, so that we know how much longer we
   //should wait after waiting for a bit.
   ELAPSED_INIT(start_tv);

   if (hide_cursor)
      cursor_sleep();
   else
      cursor_on();

   out_flush();
   while (!gotInterruptG && done < msec) {
      wait_now = msec - done > 1000L ? 1000L : msec - done; {
          long    due_time = check_due_timer();

          if (due_time > 0 && due_time < wait_now)
         wait_now = due_time;
      }
      if (has_any_channel() && wait_now > 20L)
          wait_now = 20L;
      ui_delay(wait_now, true);

      if (has_any_channel())
          ui_breakcheck_force(true);
      else
          ui_breakcheck();
      //Process the clientserver messages that may have been received in the call to ui_breakcheck() 
      //when the GUI is in use. This may occur when running a test case.
      parse_queued_messages();

      //actual time passed
      done = ELAPSED_FUNC(start_tv);
   }

   //If CTRL-C was typed to interrupt the sleep, drop the CTRL-C from the
   //input buffer, otherwise a following call to input() fails.
   if (gotInterruptG)
      (void)vpeekc();

   if (hide_cursor)
      cursor_unsleep();
}

pub void
c_wincmd(Invocation* invo) {
   int      xchar = ZERO;
   CS p;

   if (*invo->arg == 'g' || *invo->arg == Ctrl_G) {
      //CTRL-W g and CTRL-W CTRL-G  have an extra command character
      if (invo->arg[1] == ZERO) {
         emsg(_(e_invalid_argument));
         return;
      }
      xchar = invo->arg[1];
      p = invo->arg + 2;
   } else
      p = invo->arg + 1;

   p = skipwhite(p);
   if (*p != ZERO && !isComment(p))
      emsg(_(e_invalid_argument));
   ei (!invo->skip) {
      //Pass flags on for ":vertical wincmd ]".
      postponed_split_flags = commModifierG.cmod_split;
      postponed_split_tab = commModifierG.cmod_tab;
      doPortal(*invo->arg, invo->addr_count > 0 ? invo->line2 : 0L, xchar);
      postponed_split_flags = 0;
      postponed_split_tab = 0;
   }
}

//":winpos".
pub void
c_portPos(Invocation* invo) {
   int      x, y;
   CS arg = invo->arg;
   CS p;

   if (*arg == ZERO) {
       emsg(_(e_obtaining_window_position_not_implemented_for_this_platform));
   } else {
      x = parseLong(&arg);
      arg = skipwhite(arg);
      p = arg;
      y = parseLong(&arg);
      if (*p == ZERO || *arg != ZERO) {
         emsg(_(e_winpos_requires_two_number_arguments));
         return;
      }
      if (*termCodesG[KS_CWP])
         term_set_winpos(x, y);
    }
}

//Handle commands that work like operators: ":delete", ":yank", ":>" and ":<"
pub void
c_operators(Invocation* invo) {
   Operator   oper;

   doClearOpArg(&oper);
   oper.regname = invo->regname;
   oper.start.lnum = invo->line1;
   oper.end.lnum = invo->line2;
   oper.line_count = invo->line2 - invo->line1 + 1;
   oper.motion_type = MLINE;
   virtual_op = false;
   if (invo->id != C_yank) {  //position cursor for undo
      setpcmark();
      curPor->cursor.lnum = invo->line1;
      beginline(BL_SOL | BL_FIX);
   }

   if (VIsual_active)
   end_visual_mode();

   switch (invo->id) {
   case C_delete:
      oper.opTy = OP_DELETE;
      op_delete(&oper);
      break;

   case C_yank:
      oper.opTy = OP_YANK;
      (void)op_yank(&oper, false, true);
      break;

   default:    //C_rshift or C_lshift
      if (invo->id == C_rshift)
         oper.opTy = OP_RSHIFT;
      else
         oper.opTy = OP_LSHIFT;
      op_shift(&oper, false, invo->amount);
      break;
   }
   virtual_op = MAYBE;
   mayPrint(invo);
}

//":put".
pub void
c_put(Invocation* invo) {
   //":0put" works like ":1put!".
   if (invo->line2 == 0) {
      invo->line2 = 1;
      invo->forceit = true;
   }
   curPor->cursor.lnum = invo->line2;
   check_cursor_col();
   do_put(invo->regname, NULL, invo->forceit ? BACKWARD : FORWARD, 1L, PUT_LINE|PUT_CURSLINE);
}

//":iput".
pub void
c_iput(Invocation* invo) {
   //":0iput" works like ":1iput!".
   if (invo->line2 == 0) {
      invo->line2 = 1;
      invo->forceit = true;
   }
   curPor->cursor.lnum = invo->line2;
   check_cursor_col();
   do_put(
      invo->regname, NULL, invo->forceit ? BACKWARD : FORWARD, 1L,
      PUT_LINE|PUT_CURSLINE|PUT_FIXINDENT
   );
}

//Handle ":copy" and ":move".
pub void
c_copymove(Invocation* invo) {
   long n = doGetCommandAddress(invo, &invo->arg, invo->addressKind, false, false, false, 1);
   if (invo->arg == NULL) {      //error detected
      return;
   }
   get_flags(invo);

   //move or copy lines from 'invo->line1'-'invo->line2' to below line 'n'
   if (n == MAXLNUM || n < 0 || n > curBook->mem.lineCount) {
      emsg(_(e_invalid_range));
      return;
   }

   if (invo->id == C_move) {
      if (do_move(invo->line1, invo->line2, n) == FAIL)
         return;
   } else
      doCopy(invo->line1, invo->line2, n);
   u_clearline();
   beginline(BL_SOL | BL_FIX);
   mayPrint(invo);
}

//Print the current line if flags were given to the command.
private void
mayPrint(Invocation* invo) {
   if (invo->flags != 0) {
      print_line(curPor->cursor.lnum, (invo->flags & EXFLAG_LIST));
      ex_no_reprint = true;
   }
}

//":join".
pub void
c_join(Invocation* invo) {
   curPor->cursor.lnum = invo->line1;
   if (invo->line1 == invo->line2) {
      if (invo->addr_count >= 2)   //:2,2join does nothing
         return;
      if (invo->line2 == curBook->mem.lineCount) {
         inpFlushIfNotSilent();
         return;
      }
      ++invo->line2;
   }
   (void)doJoinLinesUnderCursor(invo->line2 - invo->line1 + 1, !invo->forceit, true, true, true);
   beginline(BL_WHITE | BL_FIX);
   mayPrint(invo);
}

//":[addr]@r" or ":[addr]*r": execute register
pub void
c_at(Invocation* invo) {
   int prev_len = typeBufG.validLen;

   curPor->cursor.lnum = invo->line2;
   check_cursor_col();

   //get the register name.  No name means to use the previous one
   int c = *invo->arg;
   if (c == ZERO || (c == '*' && *invo->comm == '*'))
      c = '@';
   //Put the register in the typeahead buffer with the "silent" flag.
   if (do_execreg(c, true, true, true) == FAIL) {
      inpFlushIfNotSilent();
      return;
   }

    int   save_efr = executingFromRegG;

   executingFromRegG = true;

   //Execute from the typeahead buffer.
   //Continue until the stuff buffer is empty and all added characters have been consumed.
   while (!stuff_empty() || typeBufG.validLen > prev_len)
      (void)doCommand(NULL, scrGetTypedCommand, NULL, DOCMD_NOWAIT|DOCMD_VERBOSE);

   executingFromRegG = save_efr;
}

//":!".
pub void
c_bang(Invocation* invo) {
   do_bang(invo->addr_count, invo, invo->forceit, true, true);
}

//":undo".
pub void
c_undo(Invocation* invo) {
   if (invo->addr_count == 1)       //:undo 123
      undo_time(invo->line2, false, false, true);
   else
      u_undo(1);
}

pub void
c_wundo(Invocation* invo) {
   Byte hash[UNDO_HASH_SIZE];

   u_compute_hash(hash);
   u_write_undo(invo->arg, invo->forceit, curBook, hash);
}

pub void
c_rundo(Invocation* invo) {
   Byte hash[UNDO_HASH_SIZE];

   u_compute_hash(hash);
   u_read_undo(invo->arg, hash, NULL);
}

//":redo".
pub void
c_redo(Invocation*) {
   u_redo(1);
}

//":earlier" and ":later".
pub void
c_later(Invocation* invo) {
   long   count = 0;
   int      sec = false;
   int      file = false;
   CS p = invo->arg;

   if (*p == ZERO)
      count = 1;
   ei (SAFE_isdigit(*p)) {
      count = parseLong(&p);
      switch (*p) {
         case 's': ++p; sec = true; break;
         case 'm': ++p; sec = true; count *= 60; break;
         case 'h': ++p; sec = true; count *= 60 * 60; break;
         case 'd': ++p; sec = true; count *= 24 * 60 * 60; break;
         case 'f': ++p; file = true; break;
      }
   }

   if (*p != ZERO)
      showErrFmtMsg(_(e_invalid_argument_str), invo->arg);
   else
      undo_time(invo->id == C_earlier ? -count : count, sec, file, false);
}

//":redir": start/stop redirection.
pub void
c_redir(Invocation* invo) {
   CS mode;
   CS fname;
   CS arg = invo->arg;

   if (redir_execute) {
      emsg(_(e_cannot_use_redir_inside_execute));
      return;
   }

   if (caseInsensitiveCompare(invo->arg, "END") == 0)
      close_redir();
   else {
      if (*arg == '>') {
         ++arg;
         if (*arg == '>') {
            ++arg;
            mode = S"a";
         } else
            mode = S"w";
         arg = skipwhite(arg);

         close_redir();

         //Expand environment variables and "~/".
         fname = doExpandEnvInMultiplePaths(arg);
         if (!fname)
            return;

         redir_fd = doOpenCommandsFile(fname, invo->forceit, mode);
         eeglFree(fname);
      } ei (*arg == '@') {
          //redirect to a register a-z (resp. A-Z for appending)
          close_redir();
          ++arg;
          if (ASCII_ISALPHA(*arg)
             || *arg == '*'
             || *arg == '+'
             || *arg == '"'
         ) {
            redir_reg = *arg++;
            if (*arg == '>' && arg[1] == '>')  //append
               arg += 2;
            else {
               //Can use both "@a" and "@a>".
               if (*arg == '>')
                  arg++;
               //Make register empty when not using @A-@Z and the command is valid.
               if (*arg == ZERO && !SAFE_isupper(redir_reg))
                  write_reg_contents(redir_reg, (CS)"", -1, false);
            }
         }
         if (*arg != ZERO) {
            redir_reg = 0;
            showErrFmtMsg(_(e_invalid_argument_str), invo->arg);
         }
      } ei (*arg == '=' && arg[1] == '>') {
         int append;

         //redirect to a variable
         close_redir();
         arg += 2;

         if (*arg == '>') {
            ++arg;
            append = true;
         } else
            append = false;

         if (var_redir_start(skipwhite(arg), append) == OK)
            redir_vname = 1;
      }

      //TODO: redirect to a buffer
      else
         showErrFmtMsg(_(e_invalid_argument_str), invo->arg);
   }

   //Make sure redirection is not off.  Can happen for commline completion
   //that indirectly invokes a command to catch its output.
   if (redir_fd || redir_reg || redir_vname)
      redir_off = false;
}

//":redraw": force redraw, with clear for ":redraw!".
pub void
c_redraw(Invocation* invo) {
   redraw_cmd(invo->forceit);
}

//":redraw": force redraw, with clear if "clear" is true.
pub void
redraw_cmd(int clear) {
   int save_isRedrawingDisabledG = isRedrawingDisabledG;
   isRedrawingDisabledG = 0;

   int save_p_lz = p_lz;
   p_lz = false;

   validate_cursor();
   update_topline();
   drawUpdateScreen(clear ? UPD_CLEAR : VIsual_active ? UPD_INVERTED : 0);
   isRedrawingDisabledG = save_isRedrawingDisabledG;
   p_lz = save_p_lz;

   //After drawing the statusline screen_attr may still be set.
   drawStopHilite();

   //Reset msg_didout, so that a message that's there is overwritten.
   msg_didout = false;
   msgColG = 0;

   //No need to wait after an intentional redraw.
   need_wait_return = false;

    //When invoked from a callback or autocmd the command line may be active.
   if (stateG & MODE_COMMLINE)
      redrawCommline();

   out_flush();
}

//":redrawstatus": force redraw of status line(s)
pub void
c_redrawstatus(Invocation* invo) {
   if (invo->forceit)
      status_redraw_all();
   else
      drawAllStatusLinesOfCurBookLater();
   if (msg_scrolled && (stateG & MODE_COMMLINE))
      return;  //redraw later

   int save_isRedrawingDisabledG = isRedrawingDisabledG;
   isRedrawingDisabledG = 0;

   int save_p_lz = p_lz;
   p_lz = false;

   if (stateG & MODE_COMMLINE)
      redraw_statuslines();
   else
      drawUpdateScreen(VIsual_active ? UPD_INVERTED : 0);
   isRedrawingDisabledG = save_isRedrawingDisabledG;
   p_lz = save_p_lz;
   out_flush();
}

//":redrawtabpanel": force redraw of the tabpanel
pub void
c_redrawtabpanel(Invocation*) {
   int save_isRedrawingDisabledG = isRedrawingDisabledG;
   isRedrawingDisabledG = 0;

   int save_p_lz = p_lz;
   p_lz = false;

   draw_tabpanel();

   isRedrawingDisabledG = save_isRedrawingDisabledG;
   p_lz = save_p_lz;
   out_flush();
}

private void
close_redir(void) {
   if (redir_fd) {
      fclose(redir_fd);
      redir_fd = NULL;
   }
   redir_reg = 0;
   if (redir_vname) {
      var_redir_stop();
      redir_vname = 0;
   }
}

pub int
eeMkdir_emsg(CS name, int prot) {
   if (eeMkdir(name, prot) != 0) {
      showErrFmtMsg(_(e_cannot_create_directory_str), name);
      return FAIL;
   }
   return OK;
}

//Open a file for writing for a command, with some checks. Return file descriptor, NULL on failure
pub FILE *
doOpenCommandsFile(CS fname, int forceit, CS mode) { //"w" for create new file or "a" for append
   FILE* fd;

   //with Unix it is possible to open a directory
   if (mch_isdir(fname)) {
      showErrFmtMsg(_(e_str_is_directory), fname);
      return NULL;
   }
   if (!forceit && mode[0] != 'a' && eeFexists(fname)) {
      showErrFmtMsg(_(e_str_exists_add_bang_to_override), fname);
      return NULL;
   }

   if ((fd = FOPEN(fname, mode)) == NULL)
      showErrFmtMsg(_(e_cannot_open_str_for_writing_2), fname);

   return fd;
}

//":mark" and ":k".
pub void
c_mark(Invocation* invo) {
   if (*invo->arg == ZERO) {     //No argument?
      emsg(_(e_argument_required));
      return;
   }

   if (invo->arg[1] != ZERO) {  //more than one character? showErrFmtMsg(_(e_trailing_characters_str), invo->arg);
      return;
   }

   Pos pos = curPor->cursor;      //save curPor->cursor
   curPor->cursor.lnum = invo->line2;
   beginline(BL_WHITE | BL_FIX);
   if (setmark(*invo->arg) == FAIL)   //set mark
      emsg(_(e_argument_must_be_letter_or_forward_backward_quote));
   curPor->cursor = pos;      //restore curPor->cursor
}

//Update topLine, leftCol and the cursor position.
pub void
update_topline_cursor(void) {
   check_cursor();      //put cursor on valid line
   update_topline();
   if (!curPor->o.wrap)
      validate_cursor();
   update_curswant();
}

//Save the current stateG and go to Normal mode. Return true if the typeahead could be saved.
private int
save_current_state(SaveState* sst) {
   sst->save_msg_scroll = msg_scroll;
   sst->save_restart_edit = restart_edit;
   sst->save_msg_didout = msg_didout;
   sst->save_State = stateG;
   sst->save_finish_op = finish_op;
   sst->save_opcount = opcount;
   sst->save_reg_executing = reg_executing;
   sst->save_pending_end_reg_executing = pending_end_reg_executing;

   msg_scroll = false;          //no msg scrolling in Normal mode
   restart_edit = 0;          //don't go to Insert mode

   //Save the current typeahead.  This is required to allow using ":normal" from an event handler
   //and makes sure we don't hang when the argument ends with half a command.
   save_typeahead(&sst->tabuf);
   return sst->tabuf.typebuf_valid;
}

private void
restore_current_state(SaveState* sst) {
   //Restore the previous typeahead.
   restore_typeahead(&sst->tabuf, false);

   msg_scroll = sst->save_msg_scroll;
   restart_edit = sst->save_restart_edit;
   finish_op = sst->save_finish_op;
   opcount = sst->save_opcount;
   reg_executing = sst->save_reg_executing;
   pending_end_reg_executing = sst->save_pending_end_reg_executing;
   msg_didout |= sst->save_msg_didout;   //don't reset msg_didout now

   //Restore the state (needed when called from a function executed for
   //'indentexpr'). Update the mouse and cursor, they may have changed.
   stateG = sst->save_State;
   ui_cursor_shape();      //may show different cursor shape
}

//":normal[!] {commands}": Execute normal mode commands.
pub void
c_normal(Invocation* invo) {
   SaveState saveState;
   int      l;

   if (ex_normal_lock > 0) {
      emsg(_(e_not_allowed_here));
      return;
   }
   if (ex_normal_busy >= MAX_MAPPING_RECURSION) {
      emsg(_(e_recursive_use_of_normal_too_deep));
      return;
   }

   //vgetc() expects a CSI and K_SPECIAL to have been escaped.  Don't do
   //this for the K_SPECIAL leading byte, otherwise special keys will not work.
   int   len = 0;

   //Count the number of characters to be escaped.
   CS arg = NULL;
   for (CS p = invo->arg; *p != ZERO; ++p) {
      for (l = utfCharLen(p) - 1; l > 0; --l) {
         if (*++p == K_SPECIAL)     //trailbyte K_SPECIAL or CSI
            len += 2;
      } 
   }
   if (len > 0) {
      arg = alloc(STRLEN(invo->arg) + len + 1);
      len = 0;
      for (CS p = invo->arg; *p != ZERO; ++p) {
         arg[len++] = *p;
         for (l = utfCharLen(p) - 1; l > 0; --l) {
            arg[len++] = *++p;
            if (*p == K_SPECIAL) {
                arg[len++] = KS_SPECIAL;
                arg[len++] = KE_FILLER;
            }
         }
         arg[len] = ZERO;
      }
   }

   ++ex_normal_busy;
   if (save_current_state(&saveState)) {
      //Repeat the :normal command for each line in the range.  When no
      //range given, execute it just once, without positioning the cursor first.
      do {
         if (invo->addr_count != 0) {
            curPor->cursor.lnum = invo->line1++;
            curPor->cursor.col = 0;
            check_cursor_moved(curPor);
         }

         exec_normal_cmd(arg
              ? arg
              : invo->arg, invo->forceit ? REMAP_NONE : REMAP_YES, false);
      } while (invo->addr_count > 0 && invo->line1 <= invo->line2 && !gotInterruptG);
   }

   //Might not return to the main loop when in an event handler.
   update_topline_cursor();

   restore_current_state(&saveState);
   --ex_normal_busy;
   setmouse();
   ui_cursor_shape();      //may show different cursor shape

   eeglFree(arg);
}

//":startinsert", ":startreplace" and ":startgreplace"
pub void
c_startinsert(Invocation* invo) {
   if (invo->forceit) {
      //cursor line can be zero on startup
      if (!curPor->cursor.lnum)
         curPor->cursor.lnum = 1;
      set_cursor_for_append_to_line();
   }
   //Ignore this when running in an active terminal.
   if (term_job_running(curBook->term))
      return;

   //Ignore the command when already in Insert mode.  Inserting an
   //expression register that invokes a function can do this.
   if (stateG & MODE_INSERT)
      return;

   restart_edit = 'a';

   if (!invo->forceit) {
      if (invo->id == C_startinsert)
         restart_edit = 'i';
      curPor->cursWant = 0;       //avoid MAXCOL
   }

   if (VIsual_active)
      showmode();
}

//":stopinsert"
pub void
c_stopinsert(Invocation*) {
   restart_edit = 0;
   stop_insert_mode = true;
   //when called from remote_expr in insert mode, make sure insert mode is
   //ended by adding K_NOP to the typeahead buffer
   if (vgetcBusyG)
      ins_char_typebuf(K_NOP, 0);
   clearmode();
}

//Execute normal mode command "cmd". "remap" can be REMAP_NONE or REMAP_YES.
pub void
exec_normal_cmd(CS cmd, int remap, int silent) {
   //Stuff the argument into the typeahead buffer.
   insertIntoTypebuf(cmd, remap, 0, true, silent);
   exec_normal(false, false, false);
}

//Execute normalAction() until there is no typeahead left.
//When "use_vpeekc" is true use vpeekc() to check for available chars.
pub void
exec_normal(int was_typed, int use_vpeekc, int may_use_terminal_loop) {
   //When calling vpeekc() from feedkeys() it will return Ctrl_C when there
   //is nothing to get, so also check for Ctrl_C.
   Operator oper;
   doClearOpArg(&oper);
   finish_op = false;
   Unt c;
   while ((!stuff_empty()
      || ((was_typed || !typebuf_typed()) && typeBufG.validLen > 0)
      || (use_vpeekc && (c = vpeekc()) != ZERO && c != Ctrl_C)) && !gotInterruptG
   ) {
      update_topline_cursor();
      if (may_use_terminal_loop && term_use_loop()
         && oper.opTy == OP_NOP && oper.regname == ZERO
         && !VIsual_active
      ) {
         //If terminal_loop() returns OK we got a key that is handled
         //in Normal model.  With FAIL we first need to position the
         //cursor and the screen needs to be redrawn.
         if (terminal_loop(true) == OK)
            normalAction(&oper);
      } else {
          //execute a Normal mode comm
          normalAction(&oper);
      }
   }
}

pub void
c_checkpath(Invocation* invo) {
   find_pattern_in_path(NULL, 0, 0, false, false, CHECK_PATH, 1L,
      invo->forceit ? ACTION_SHOW_ALL : ACTION_SHOW,
      (LineNr)1, (LineNr)MAXLNUM, invo->forceit, false);
}

//":psearch"
pub void
c_psearch(Invocation* invo) {
   g_do_tagpreview = p_pvh;
   c_findpat(invo);
   g_do_tagpreview = 0;
}

pub void
c_findpat(Invocation* invo) {
   int      whole = true;
   CS p;
   int      action;

   switch (commands[invo->id].name[2]) {
   case 'e':   //":psearch", ":isearch" and ":dsearch"
      if (commands[invo->id].name[0] == 'p')
         action = ACTION_GOTO;
      else
         action = ACTION_SHOW;
      break;
   case 'i':   //":ilist" and ":dlist"
       action = ACTION_SHOW_ALL;
       break;
   case 'u':   //":ijump" and ":djump"
       action = ACTION_GOTO;
       break;
   default:   //":isplit" and ":dsplit"
       action = ACTION_SPLIT;
       break;
   }

   long n = 1;
   if (eeIsDigit(*invo->arg))   { //get count
      n = parseLong(&invo->arg);
      invo->arg = skipwhite(invo->arg);
   }
   if (*invo->arg == '/') {  //Match regexp, not just whole words
      whole = false;
      ++invo->arg;
      p = skip_regexp(invo->arg, '/', true);
      if (*p) {
         *p++ = ZERO;
         p = skipwhite(p);

         //Check for trailing illegal characters
         if (!endsComm(invo->arg))
            invo->errmsg = ex_errmsg(e_trailing_characters_str, p);
      }
   }
   if (!invo->skip)
   find_pattern_in_path(invo->arg, 0, (int)STRLEN(invo->arg),
      whole, !invo->forceit,
      *invo->comm == 'd' ? FIND_DEFINE : FIND_ANY, n, action,
      invo->line1, invo->line2, invo->forceit, false);
}

private void
tagCmd(Invocation* invo, CS name) {
   int      cmd;

   switch (name[1]) {
   case 'j': cmd = DT_JUMP;   //":tjump"
        break;
   case 's': cmd = DT_SELECT;   //":tselect"
        break;
   case 'p': cmd = DT_PREV;   //":tprevious"
        break;
   case 'N': cmd = DT_PREV;   //":tNext"
        break;
   case 'n': cmd = DT_NEXT;   //":tnext"
        break;
   case 'o': cmd = DT_POP;      //":pop"
        break;
   case 'f':         //":tfirst"
   case 'r': cmd = DT_FIRST;   //":trewind"
        break;
   case 'l': cmd = DT_LAST;   //":tlast"
        break;
   default:         //":tag"
      if (p_cst && *invo->arg != ZERO)  {
         c_cstag(invo);
         return;
      }
      cmd = DT_TAG;
      break;
   }

   if (name[0] == 'l') {
      c_ni(invo);
      return;
   }

   do_tag(invo->arg, cmd, invo->addr_count > 0 ? (int)invo->line2 : 1, invo->forceit, true);
}



//":ptag", ":ptselect", ":ptjump", ":ptnext", etc.
pub void
c_ptag(Invocation* invo) {
   g_do_tagpreview = p_pvh;  //will be reset to 0 in tagCmd()
   tagCmd(invo, commands[invo->id].name + 1);
}

//":pedit"
pub void
c_pedit(Invocation* invo) {
   Portal   *curPor_save = curPor;
   prepare_preview_window();

   //Edit the file.
   do_exedit(invo, NULL);

   back_to_current_window(curPor_save);
}

//":pbook"
pub void
c_pbuffer(Invocation* invo) {
   Portal   *curPor_save = curPor;
   prepare_preview_window();

   //Go to the book.
   do_exbuffer(invo);

   back_to_current_window(curPor_save);
}

private void
prepare_preview_window(void) {
   if (portErrorIfPopup(true))
      return;

   //Open the preview portal or popup and make it the current portal.
   g_do_tagpreview = p_pvh;
   prepare_tagpreview(true, true, false);
}

private void
back_to_current_window(Portal *curPor_save) {
   if (curPor != curPor_save && portalIsValid(curPor_save)) {
      //Return cursor to where we were
      validate_cursor();
      redraw_later(UPD_VALID);
      enterPortal(curPor_save, true);
   } ei (PORTAL_IS_POPUP(curPor)) {
      //can't keep focus in popup portal
      enterPortal(firstPor, true);
   }
   g_do_tagpreview = 0;
}

//":stag", ":stselect" and ":stjump".
pub void
c_stag(Invocation* invo) {
   postponed_split = -1;
   postponed_split_flags = commModifierG.cmod_split;
   postponed_split_tab = commModifierG.cmod_tab;
   tagCmd(invo, commands[invo->id].name + 1);
   postponed_split_flags = 0;
   postponed_split_tab = 0;
}

//":tag", ":tselect", ":tjump", ":tnext", etc.
pub void
c_tag(Invocation* invo) {
   tagCmd(invo, commands[invo->id].name);
}

enum {
   SPEC_PERC = 0,
   SPEC_HASH,
   SPEC_CWORD,       //cursor word
   SPEC_CCWORD,    //cursor WORD
   SPEC_CEXPR,       //expr under cursor
   SPEC_CFILE,       //cursor path name
   SPEC_SFILE,       //":so" file name
   SPEC_SLNUM,       //":so" file line number
   SPEC_STACK,       //call stack
   SPEC_SCRIPT,    //script file name
   SPEC_AFILE,       //autocommand file name
   SPEC_ABUF,       //autocommand book number
   SPEC_AMATCH,    //autocommand match name
   SPEC_SFLNUM,    //script file line number
   SPEC_SID       //script ID: <SNR>123_
};

//Check "str" for starting with a special commline variable.
//If found return one of the SPEC_ values and set "*usedlen" to the length of
//the variable.  Otherwise return -1 and "*usedlen" is unchanged.
pub int
find_commline_var(CS src, Unt *usedlen) {
   //must be sorted by the 'value' field because it is used by bsearch()!
   static Kv spec_str_tab[] = {
      KEYVALUE_ENTRY(SPEC_SID, "SID>"),       //script ID: <SNR>123_
      KEYVALUE_ENTRY(SPEC_ABUF, "abuf>"),       //autocommand book number
      KEYVALUE_ENTRY(SPEC_AFILE, "afile>"),       //autocommand file name
      KEYVALUE_ENTRY(SPEC_AMATCH, "amatch>"),       //autocommand match name
      KEYVALUE_ENTRY(SPEC_CCWORD, "cWORD>"),       //cursor WORD
      KEYVALUE_ENTRY(SPEC_CEXPR, "cexpr>"),       //expr under cursor
      KEYVALUE_ENTRY(SPEC_CFILE, "cfile>"),       //cursor path name
      KEYVALUE_ENTRY(SPEC_CWORD, "cword>"),       //cursor word
      KEYVALUE_ENTRY(SPEC_SCRIPT, "script>"),       //script file name
      KEYVALUE_ENTRY(SPEC_SFILE, "sfile>"),       //":so" file name
      KEYVALUE_ENTRY(SPEC_SFLNUM, "sflnum>"),       //script file line number
      KEYVALUE_ENTRY(SPEC_SLNUM, "slnum>"),       //":so" file line number
      KEYVALUE_ENTRY(SPEC_STACK, "stack>")       //call stack
   };
   Kv target;
   Kv *entry;

   switch (*src) {
   case '%':
      *usedlen = 1;
      return SPEC_PERC;

   case '#':
      *usedlen = 1;
      return SPEC_HASH;

   case '<':
      target.key = 0;
      target.value = (Text){src + 1, 0}; //skip '<'. see cmp_keyvalue_value_n()

      entry = (Kv *)bsearch(&target, &spec_str_tab,
      ARRAY_LENGTH(spec_str_tab), sizeof(spec_str_tab[0]),
      cmp_keyvalue_value_n);
   if (!entry)
      return -1;

   *usedlen = entry->value.len + 1;
   return entry->key;

    default:
   break;
   }

   return -1;
}

//}}}
//{{{Evaluate commline variables.
//
//change "%"       to curBook->fullFileName
//    "#"       to curPor->altFnum
//    "%%"       to curPor->altFnum in Vim9 script
//    "<cword>" to word under the cursor
//    "<cWORD>" to WORD under the cursor
//    "<cexpr>" to C-expression under the cursor
//    "<cfile>" to path name under the cursor
//    "<sfile>" to sourced file name
//    "<stack>" to call stack
//    "<script>" to current script name
//    "<slnum>" to sourced file line number
//    "<afile>" to file name for autocommand
//    "<abuf>"  to book number for autocommand
//    "<amatch>" to matching name for autocommand
//
//When an error is detected, "errorMsg" is set to a non-NULL pointer (may be
//"" for error without a message) and NULL is returned.
//Returns an allocated string if a valid match was found.
//Returns NULL if no match was found.   "usedlen" then still contains the
//number of characters to skip.
pub CS
evalVars(
   OUT LineNr* lnump,      //line number for :e command, or NULL
   OUT CS* errorMsg,   //pointer to error message
   CS src,      //pointer into commandline
   CS srcstart,   //beginning of valid memory for src
   Unt* usedlen,   //characters after src that are used
   int* escaped,   //return value has escaped white space (can be NULL)
   int empty_is_error   //empty result is considered an error
){
   int      i;
   CS s;
   CS result;
   CS resultbuf = NULL;
   Unt   resultlen;
   Book* book;
   int valid = VALID_HEAD + VALID_PATH;    //assume valid result
   int spec_idx;
   int tilde_file = false;
   int skip_mod = false;
   Byte   strbuf[30];

   *errorMsg = NULL;
   if (escaped)
      *escaped = false;

   //Check if there is something to do.
   spec_idx = find_commline_var(src, usedlen);
   if (spec_idx < 0) {//no match
      *usedlen = 1;
      return NULL;
   }

   //Skip when preceded with a backslash "\%" and "\#".
   //Note: In "\\%" the % is also not recognized!
   if (src > srcstart && src[-1] == '\\') {
      *usedlen = 0;
      STRMOVE(src - 1, src);   //remove backslash
      return NULL;
   }

   //word or WORD under cursor
   if (spec_idx == SPEC_CWORD || spec_idx == SPEC_CCWORD || spec_idx == SPEC_CEXPR) {
      resultlen = find_ident_under_cursor(&result,
         spec_idx == SPEC_CWORD ? (FIND_IDENT | FIND_STRING)
            : spec_idx == SPEC_CEXPR ? (FIND_IDENT | FIND_STRING | FIND_EVAL)
            : FIND_STRING);
      if (resultlen == 0) {
         *errorMsg = S"";
         return NULL;
      }
   }

   //'#': Alternate file name
   //'%': Current file name
   //     File name under the cursor
   //     File name for autocommand
   // and following modifiers
   else {
      Unt off = 0;

      switch (spec_idx) {
      case SPEC_PERC:
         //'%': current file
         if (curBook->currFileName == NULL) {
            result = (CS)"";
            valid = 0;       //Must have ":p:h" to be valid
         } else {
            result = curBook->currFileName;
            tilde_file = STRCMP(result, "~") == 0;
         }
         break;
         //"%%" alternate file
         off = 1;
         //FALLTHROUGH
      case SPEC_HASH:      //'#' or "#99": alternate file
         if (off == 0 ? src[1] == '#' : src[2] == '%') {
            //"##" or "%%%": the argument list
            result = arg_all();
            resultbuf = result;
            *usedlen = off + 2;
            if (escaped)
               *escaped = true;
            skip_mod = true;
            break;
         }
         s = src + off + 1;
         if (*s == '<')      //"#<99" uses v:oldfiles
            ++s;
         i = (int)parseLong(&s);
         if (s == src + off + 2 && src[off + 1] == '-')
            //just a minus sign, don't skip over it
            s--;
         *usedlen = (int)(s - src); //length of what we expand

         if (src[off + 1] == '<' && i != 0) {
            if (*usedlen < off + 2) {
               //Should we give an error message for #<text?
               *usedlen = off + 1;
               return NULL;
            }
         } else {
            if (i == 0 && src[off + 1] == '<' && *usedlen > off + 1)
               *usedlen = off + 1;
            book = bookFindFileByBookNr(i);
            if (!book) {
               *errorMsg = _(e_no_alternate_file_name_to_substitute_for_hash);
               return NULL;
            }
            if (lnump)
               *lnump = ECMD_LAST;
            if (book->currFileName == NULL) {
               result = (CS)"";
               valid = 0;       //Must have ":p:h" to be valid
            } else {
               result = book->currFileName;
               tilde_file = STRCMP(result, "~") == 0;
            }
         }
         break;

      case SPEC_CFILE:   //file name under cursor
         result = file_name_at_cursor(FNAME_MESS|FNAME_HYP, 1L, NULL);
         if (!result) {
            *errorMsg = S"";
            return NULL;
         }
         resultbuf = result;       //remember allocated string
         break;

      case SPEC_AFILE:   //file name for autocommand
         result = autocmd_fname;
         if (result && !autocmd_fname_full) {
            //Still need to turn the fname into a full path.  It is
            //postponed to avoid a delay when <afile> is not used.
            autocmd_fname_full = true;
            result = fiExpandAndCopy(autocmd_fname, false);
            eeglFree(autocmd_fname);
            autocmd_fname = result;
         }
         if (!result) {
            *errorMsg = _(e_no_autocommand_file_name_to_substitute_for_afile);
            return NULL;
         }
         result = shorten_fname1(result);
         break;

      case SPEC_ABUF:      //book number for autocommand
         if (autocmd_bufnr <= 0) {
            *errorMsg = _(e_no_autocommand_buffer_number_to_substitute_for_abuf);
            return NULL;
         }
         sprintf((char *)strbuf, "%d", autocmd_bufnr);
         result = strbuf;
         break;

      case SPEC_AMATCH:   //match name for autocommand
         result = autocmd_match;
         if (!result) {
            *errorMsg = _(e_no_autocommand_match_name_to_substitute_for_amatch);
            return NULL;
         }
         break;

      case SPEC_SFILE:   //file name for ":so" command
         result = estack_sfile(ESTACK_SFILE);
         if (!result) {
            *errorMsg = _(e_no_source_file_name_to_substitute_for_sfile);
            return NULL;
         }
         resultbuf = result;       //remember allocated string
         break;
      case SPEC_STACK:   //call stack
         result = estack_sfile(ESTACK_STACK);
         if (!result) {
            *errorMsg = _(e_no_call_stack_to_substitute_for_stack);
            return NULL;
         }
         resultbuf = result;       //remember allocated string
         break;
      case SPEC_SCRIPT:   //script file name
         result = estack_sfile(ESTACK_SCRIPT);
         if (!result) {
            *errorMsg = _(e_no_script_file_name_to_substitute_for_script);
            return NULL;
         }
         resultbuf = result;       //remember allocated string
         break;

      case SPEC_SLNUM:   //line in file for ":so" command
         if (!SOURCING_NAME || SOURCING_LNUM == 0) {
            *errorMsg = _(e_no_line_number_to_use_for_slnum);
            return NULL;
         }
         sprintf((char *)strbuf, "%ld", SOURCING_LNUM);
         result = strbuf;
         break;

      case SPEC_SFLNUM:   //line in script file
         if (scriptPosG.lineNr + SOURCING_LNUM == 0) {
            *errorMsg = _(e_no_line_number_to_use_for_sflnum);
            return NULL;
         }
         sprintf((char *)strbuf, "%ld", (long)(scriptPosG.lineNr + SOURCING_LNUM));
         result = strbuf;
         break;

      case SPEC_SID:
         if (scriptPosG.sid <= 0) {
            *errorMsg = _(e_using_sid_not_in_script_context);
            return NULL;
         }
         sprintf((char *)strbuf, "<SNR>%d_", scriptPosG.sid);
         result = strbuf;
         break;

      default:
         result = S""; //avoid gcc warning
         break;
      }

      resultlen = STRLEN(result);   //length of new string
      if (src[*usedlen] == '<') {  //remove the file name extension
         ++*usedlen;
         if ((s = lastOccurrence(result, '.')) != NULL && s >= fiGetShortFiName(result))
            resultlen = s - result;
      } ei (!skip_mod) {
         valid |= modify_fname(src, tilde_file, usedlen, &result, &resultbuf, &resultlen);
         if (!result) {
            *errorMsg = S""; 
            return NULL;
         }
      }
   }

   if (resultlen == 0 || valid != VALID_HEAD + VALID_PATH) {
      if (empty_is_error) {
         if (valid != VALID_HEAD + VALID_PATH)
            *errorMsg = _(e_empty_file_name_for_percent_or_hash_only_works_with_ph);
         else
            *errorMsg = _(e_evaluates_to_an_empty_string);
      }
      result = NULL;
   } else
      result = copySubstr(result, resultlen);
   eeglFree(resultbuf);
   return result;
}

//Expand the <sfile> string in "arg".
//Return an allocated string, or NULL for any error.
pub CS
expand_sfile(CS arg) {
   Unt resultlen = STRLEN(arg);
   CS result = copySubstr(arg, resultlen);
   if (!result)
      return NULL;
      

   for (CS p = result; *p; ) {
      if (STRNCMP(p, "<sfile>", 7) != 0)
         ++p;
      else {
         CS errorMsg;
         CS result;
         Unt   len;
         Unt   srclen;
         //replace "<sfile>" with the sourced file name, and do ":" stuff
         CS repl = evalVars(
               null, OUT &errorMsg,
               p, result, &srclen, NULL, true
         );
         if (errorMsg) {
            if (*errorMsg != ZERO)
               emsg(errorMsg);
            eeglFree(result);
            return NULL;
         }
         if (repl == NULL)  {    //no match (cannot happen)
            p += srclen;
            continue;
         }
         Unt repllen = STRLEN(repl);
         resultlen += (repllen - srclen);
         CS newres = alloc(resultlen + 1);
         len = p - result;
         MEMMOVE(newres, result, len);
         STRCPY(newres + len, repl);
         len += repllen;
         STRCPY(newres + len, p + srclen);
         eeglFree(repl);
         eeglFree(result);
         result = newres;
         p = newres + len;      //continue after the match
      }
   }

   return result;
}

//}}}
//{{{dialogs

//Make a dialog message in "buff[DIALOG_MSG_SIZE]". "format" must contain "%s".
pub void
dialog_msg(CS buff, CS format, CS fname) {
   if (!fname)
      fname = _("Untitled");
   eeSnprintf(buff, DIALOG_MSG_SIZE, format, fname);
}

private int filetype_detect = false;
private int filetype_plugin = false;
private int filetype_indent = false;

//":filetype [plugin] [indent] {on,off,detect}"
//on: Load the filetype.vim file to install autocommands for file types.
//off: Load the ftoff.vim file to remove all autocommands for file types.
//plugin on: load filetype.vim and ftplugin.vim
//plugin off: load ftplugof.vim
//indent on: load filetype.vim and indent.vim
//indent off: load indoff.vim
pub void
c_filetype(Invocation* invo) {
   CS arg = invo->arg;
   int plugin = false;
   int indent = false;

   if (*invo->arg == ZERO) {
      //Print current status.
      smsg("filetype detection:%s  plugin:%s  indent:%s",
         filetype_detect ? "ON" : "OFF",
         filetype_plugin ? (filetype_detect ? "ON" : "(on)") : "OFF",
         filetype_indent ? (filetype_detect ? "ON" : "(on)") : "OFF");
      return;
    }

   //Accept "plugin" and "indent" in any order.
   for (;;) {
      if (STRNCMP(arg, "plugin", 6) == 0) {
         plugin = true;
         arg = skipwhite(arg + 6);
         continue;
      }
      if (STRNCMP(arg, "indent", 6) == 0) {
         indent = true;
         arg = skipwhite(arg + 6);
         continue;
      }
      break;
   }
   if (STRCMP(arg, "on") == 0 || STRCMP(arg, "detect") == 0) {
      if (*arg == 'o' || !filetype_detect) {
         source_runtime((CS)FILETYPE_FILE, DIP_ALL);
         filetype_detect = true;
         if (plugin) {
            source_runtime((CS)FTPLUGIN_FILE, DIP_ALL);
            filetype_plugin = true;
          }
         if (indent) {
            source_runtime((CS)INDENT_FILE, DIP_ALL);
            filetype_indent = true;
         }
      }
      if (*arg == 'd') {
          (void)do_doautocmd(S"filetypedetect BufRead", true, NULL);
      }
   } ei (STRCMP(arg, "off") == 0) {
      if (plugin || indent) {
         if (plugin) {
            source_runtime((CS)FTPLUGOF_FILE, DIP_ALL);
            filetype_plugin = false;
         }
         if (indent) {
            source_runtime((CS)INDOFF_FILE, DIP_ALL);
            filetype_indent = false;
         }
      } else {
         source_runtime((CS)FTOFF_FILE, DIP_ALL);
         filetype_detect = false;
      }
   } else
      showErrFmtMsg(_(e_invalid_argument_str), arg);
}

pub void
setHlsearch(Boole flag) {
   hiliteSearchG = flag;
}

//":nohlsearch"
pub void
c_nohlsearch(Invocation*) {
   setHlsearch(false);
   redraw_all_later(UPD_SOME_VALID);
}

pub void
c_fold(Invocation* invo) {
   if (foldManualAllowed(true))
      foldCreate(invo->line1, invo->line2);
}

pub void
c_foldopen(Invocation* invo) {
   opFoldRange(invo->line1, invo->line2, invo->id == C_foldopen, invo->forceit, false);
}

pub void
c_folddo(Invocation* invo) {
   //First set the marks for all lines closed/open.
   for (LineNr lnum = invo->line1; lnum <= invo->line2; ++lnum) {
      if (getFolds(lnum, NULL, NULL) == (invo->id == C_folddoclosed))
          ml_setmarked(lnum);
   } 

   //Execute the command on the marked lines.
   global_exe(invo->arg);
   ml_clearmarked();      //clear rest of the marks
}

pub int
get_pressedreturn(void) {
   return ex_pressedreturn;
}

pub void
set_pressedreturn(int val) {
   ex_pressedreturn = val;
}

pub int
commandFlagNoSpacesInExtra() {
   return NOSPC_IN_EXTRA;
}

pub int
commandFlagExpandWildcards() {
   return XFILE;
}

//Ask for a reply from the user, a 'y' or a 'n', with prompt "str" (which should have been 
//translated already). No other characters are accepted, the message is repeated until a valid
//reply is entered or CTRL-C is hit. If direct is true, don't use vgetc() but ui_inchar(), don't 
//get characters from any buffers but directly from the user.
//
//return the 'y' or 'n'
pub int
ask_yesno(CS str, int direct) {
   int r = ' ';
   int save_State = stateG;

   if (isExitingG)      //put terminal in raw mode for this question
      termSetMode(TMODE_RAW);
   ++no_wait_return;
   stateG = MODE_CONFIRM; //mouse behaves like with :confirm
   setmouse();            //disable mouse for xterm
   ++no_mapping;
   ++allow_keys;          //no mapping here, but recognize keys

   while (r != 'y' && r != 'n') {
      //same hiliting as for wait_return()
      smsgDeco(getDecoFlags(HLF_R), "%s (y/n)?", str);
      if (direct)
         r = get_keystroke();
      else
         r = plain_vgetc();
      if (r == Ctrl_C || r == ESC)
         r = 'n';
      msg_putchar(r);       //show what you typed
      out_flush();
   }
   --no_wait_return;
   stateG = save_State;
   setmouse();
   --no_mapping;
   --allow_keys;

   return r;
}

//}}
//}}}
//{{{Environment variables

//Call doExpandEnv() and store the result in an allocated string.
//This is not very memory efficient, this expects the result to be freed again soon.
pub CS
doExpandEnvInMultiplePaths(CS src) {
   return doExpandEnvInFilePaths(src, false);
}

//Call doExpandEnv() and store the result in an allocated string.
//This is not very memory efficient, this expects the result to be freed again soon.
//When "singleFileName", handle the string as one file name, only expand "~" at the start.
pub CS
doExpandEnvInFilePaths(CS src, Boole singleFileName) {
   CS p = alloc(MAXPATHL);
   doExpandEnvVarsWithEscaped(OUT (Text){p, MAXPATHL}, src, singleFileName, NULL);
   return p;
}

//Expand environment variable with path name.
//"~/" is also expanded, using $HOME.   For Unix "~user/" is expanded.
//Skip over "\ ", "\~" and "\$".
//If anything fails no expansion is done and dst equals src.
pub Unt
doExpandEnv(
   OUT Text dst, //where to put the result
   NULLABLE CS src  //input string e.g. "$HOME/eegl.hlp"
){
   if (!src)
      return 0;
   return doExpandEnvVarsWithEscaped(OUT dst, src, false, NULL);
}

//Expand env vars. Return number of bytes written
pub Unt
doExpandEnvVarsWithEscaped(
   OUT Text dst, //where to put the result. Length must be sufficient!
   CS srcArg,    //input string e.g. "$HOME/eegl.help"
   Boole one,    //"srcp" is one file name
   CS startstr   //start again after this (can be NULL)
) {
   CS tail;
   int c;
   CS var;
   int at_start = true; //at start of a name
   int startstr_len = 0;
   CS wr = dst.c;
   CS const sentinel = dst.c + dst.len - 1; //leave one char space for "\,"
   if (startstr)
      startstr_len = (int)STRLEN(startstr);

   CS src = skipwhite(srcArg);
   while (*src != ZERO && wr < sentinel) {
      //Skip over `=expr`.
      if (src[0] == '`' && src[1] == '=') {
         var = src;
         src += 2;
         (void)skip_expr(OUT &src, NULL);
         if (*src == '`')
            ++src;
         int lenSkipped = src - var;
         if (wr + lenSkipped > sentinel)
            lenSkipped = sentinel - wr;
         copySubstrToAllocation(wr, (Text){var, lenSkipped});
         wr += lenSkipped;
         continue;
      }
      Boole copyChar = true;
      if ((*src == '$') || (*src == '~' && at_start)) {
         Boole mustfree = false;   //var was allocated, need to free it later

         //The variable name is copied into dst temporarily, because it may
         //be a string in read-only memory and a ZERO needs to be appended.
         if (*src == '$') { //environment var

            //Unix has ${var-name} type environment vars
            tail = src + 1;
            var = wr;
            int spaceLeft = sentinel - wr - 1;
            if (*tail == '{' && !eeIsIdentifierChar('{')) {
               tail++;   //ignore '{'
               while (spaceLeft-- > 0 && *tail != ZERO && *tail != '}')
                  *var++ = *tail++;
            } else {
               while (spaceLeft-- > 0 && *tail != ZERO && (eeIsIdentifierChar(*tail)))
                  *var++ = *tail++;
            }

            if (src[1] == '{' && *tail != '}')
               var = NULL;
            else {
               if (src[1] == '{')
                  ++tail;
               *var = ZERO;
               var = eeglGetEnv(wr);
            }
         } ei ( src[1] == ZERO || src[1] == '/' || firstOccurrence(S" ,\t\n", src[1]) != NULL) { //home directory
            var = homedir;
            tail = src + 1;
         }

         if (var && *var != ZERO) {
            c = (int)STRLEN(var);
            if (wr + c + STRLEN(tail) + 1 < sentinel) {
               STRCPY(wr, var);
               wr += c;
               //if var[] ends in a path separator and tail[] starts with it, skip a character
               if (after_pathsep(wr, wr + c) && *tail == '/')
                  ++tail;
               src = tail;
               copyChar = false;
            }
         }
         if (mustfree)
            eeglFree(var);
      }

      if (copyChar)   { //copy at least one char
         //Recognize the start of a new name, for '~'.
         //Don't do this when "one" is true, to avoid expanding "~" in ":edit foo ~ foo".
         at_start = false;
         if (src[0] == '\\' && src[1] != ZERO) {
            *wr++ = *src++;
         } ei ((src[0] == ' ' || src[0] == ',') && !one)
            at_start = true;
         if (wr < sentinel - 1) {
            *wr++ = *src++;

            if (startstr && src - startstr_len >= srcArg
                  && STRNCMP(src - startstr_len, startstr, startstr_len) == 0
            )
               at_start = true;
         }
      }
   }
   *wr = ZERO;

   return (Unt)(wr - dst.c);
}

//Eegl's version of getenv(). Special handling of $HOME, $EEGL and $EEGLRUNTIME.
//"mustfree" is set to true when the returned string is allocated.  It must be
//initialized to false by the caller.
pub NULLABLE CS
eeglGetEnv(CS name) {
   CS p = mch_getenv(name);
   if (p && *p == ZERO)       //empty is the same as not set
      p = NULL;
   return p;
}

//Remove environment variable "name" and take care of side effects.
pub void
eeUnsetenv(CS var) {
   unsetenv((char *)var);
}

//Set environment variable "name" and take care of side effects.
pub void
eeSetenv_ext(CS name, CS val) {
   eeSetenv(name, val);
   if (caseInsensitiveCompare(name, "HOME") == 0)
      init_homedir();
}

//Our portable version of setenv.
pub void
eeSetenv(CS name, CS val) {
   mch_setenv(name, val, 1);
   //When setting $EEGLRUNTIME adjust the directory to find message
   //translations to $EEGLRUNTIME/lang.
   if (*val != ZERO && caseInsensitiveCompare(name, "EEGLRUNTIME") == 0) {
      CS buf = concat_str(val, (CS)"/lang");
      BINDTEXTDOMAIN(EEGLPACKAGE, buf);
      eeglFree(buf);
   }
}

//}}}
//{{{break checks

//Check for CTRL-C pressed, but only once in a while.
//Should be used instead of ui_breakcheck() for functions that check for
//each line in the file.  Calling ui_breakcheck() each time takes too much
//time, because it can be a system call.

#ifndef BREAKCHECK_SKIP
# define BREAKCHECK_SKIP 1000
#endif

private int breakcheck_count = 0;

pub void
line_breakcheck(void) {
   if (++breakcheck_count >= BREAKCHECK_SKIP) {
      breakcheck_count = 0;
      ui_breakcheck();
   }
}

//Like line_breakcheck() but check 10 times less often.
pub void
fast_breakcheck(void) {
   if (++breakcheck_count >= BREAKCHECK_SKIP * 10) {
      breakcheck_count = 0;
      ui_breakcheck();
   }
}

//Like line_breakcheck() but check 100 times less often.
pub void
veryfast_breakcheck(void) {
   if (++breakcheck_count >= BREAKCHECK_SKIP * 100) {
      breakcheck_count = 0;
      ui_breakcheck();
   }
}

//}}}
//{{{multi-level undo facility
//
//The saved lines are stored in a list of lists (one for each book):
//
//    oldHead----------------------------------------------------+
//                                                               |
//                                                               V
//           +--------------+      +--------------+        +--------------+
//newHead--->|   u_header   |      | u_header     |        |   u_header   |
//           |      next  ------>  |   next    ------>     |   next     ---->NULL
//    NULL <--------prev    |<---------prev       |<---------  prev       |
//           |   uh_entry   |      |   uh_entry   |        |   uh_entry   |
//           +--------|-----+      +--------|-----+        +--------|-----+
//                    |                     |                       |
//                    V                     V                       V
//           +--------------+      +--------------+       +--------------+
//           |   u_entry    |      |   u_entry    |       |   u_entry    |
//           |   ue_next    |      |   ue_next    |       |   ue_next    |
//           +--------|-----+      +--------|-----+       +--------|-----+
//                    |                     |                      |
//                    V                     V                      V
//           +--------------+              NULL                   NULL
//           |   u_entry    |
//           |   ue_next    |
//           +--------|-----+
//                    |
//                    V
//                   etc.
//
//Each u_entry list contains the information for one undo or redo.
//curBook->undo.currHead points to the header of the last undo (the next redo),
//or is NULL if nothing has been undone (end of the branch).
//
//For keeping alternate undo/redo branches the uh_alt field is used.  Thus at
//each point in the list a branch may appear for an alternate to redo.  The
//uh_seq field is numbered sequentially to be able to find a newer or older
//branch.
//
//         +---------------+   +---------------+
//oldHead->|    u_header   |   |    u_header   |
//         |     altNext  ---->|    altNext  ----> NULL
//   NULL <----- altPrev   |<------ altPrev    |
//         |     prev      |   |    prev       |
//         +-----|---------+   +-----|---------+
//               |                   |
//               V                   V
//         +---------------+   +---------------+
//         | u_header      |   | u_header      |
//         |   altNext     |   |   altNext     |
//newHead->|   altPrev     |   |   altPrev     |
//         |   prev        |   |   prev        |
//         +-----|---------+   +-----|---------+
//               |                   |
//               V                   V
//             NULL            +---------------+    +---------------+
//                             | u_header      |    |    u_header   |
//                             |   altNext   ------>|    altNext    |
//                             |   altPrev     |<------  altPrev    |
//                             |   prev        |    |    prev       |
//                             +-----|---------+    +-----|---------+
//                                   |                    |
//                                  etc.                 etc.
//
//
//All data is allocated and will all be freed when the book is unloaded.

//Uncomment the next line for including the u_check() function.  This warns
//for errors in the debug information.
//#define U_DEBUG 1
#define UH_MAGIC 0x18dade   //value for uh_magic when in use
#define UE_MAGIC 0xabc123   //value for ue_magic when in use

//Size of buffer used for writing.
#define WRITE_BUILDER_SIZE 8192

#define U_ALLOC_LINE(size) lalloc(size, false)

//used in undo_end() to report number of added and deleted lines
private long   u_newcount, u_oldcount;

//???
private int   undo_undoes = false;

private int   lastmark = 0;

#if defined(U_DEBUG)
//Validate the undo structures. Print a warning when something looks wrong.
private int seen_currHead;
private int seen_newHead;
private int header_count;

private void
u_check_tree(UndoHeader *uhp, UndoHeader *exp_uh_next, UndoHeader *exp_altPrev) {
   if (!uhp)
      return;
      
   ++header_count;
   if (uhp == curBook->undo.currHead && ++seen_currHead > 1) {
      emsg("currHead found twice (looping?)");
      return;
   }
   if (uhp == curBook->undo.newHead && ++seen_newHead > 1) {
      emsg("newHead found twice (looping?)");
      return;
   }

   if (uhp->uh_magic != UH_MAGIC)
      emsg("uh_magic wrong (may be using freed memory)");
   else {
      //Check pointers back are correct.
      if (uhp->next.ptr != exp_uh_next) {
         emsg("next wrong");
         smsg("expected: 0x%x, actual: 0x%x", exp_uh_next, uhp->next.ptr);
      }
      if (uhp->altPrev.ptr != exp_altPrev) {
         emsg("altPrev wrong");
         smsg("expected: 0x%x, actual: 0x%x", exp_altPrev, uhp->altPrev.ptr);
      }

      //Check the undo tree at this header.
      for (UndoEntry* uep = uhp->uh_entry; uep != NULL; uep = uep->ue_next) {
         if (uep->ue_magic != UE_MAGIC) {
            emsg("ue_magic wrong (may be using freed memory)");
            break;
         }
      }

      //Check the next alt tree.
      u_check_tree(uhp->altNext.ptr, uhp->next.ptr, uhp);

      //Check the next header in this branch.
      u_check_tree(uhp->prev.ptr, uhp, NULL);
   }
}

private void
u_check(int newhead_may_be_NULL) {
   seen_newHead = 0;
   seen_currHead = 0;
   header_count = 0;

   u_check_tree(curBook->undo.oldHead, NULL, NULL);

   if (seen_newHead == 0 && curBook->undo.oldHead != NULL
       && !(newhead_may_be_NULL && curBook->undo.newHead == NULL))
      showErrFmtMsg("newHead invalid: 0x%x", curBook->undo.newHead);
   if (curBook->undo.currHead != NULL && seen_currHead == 0)
      showErrFmtMsg("currHead invalid: 0x%x", curBook->undo.currHead);
   if (header_count != curBook->undo.countHeaders) {
      emsg("countHeaders invalid");
      smsg("expected: %ld, actual: %ld",
                   (long)header_count, (long)curBook->countHeaders);
   }
}
#endif

//Save the current line for both the "u" and "U" command. Careful: may trigger autocommands that 
//reload the book. Return OK or FAIL.
pub int
u_save_cursor(void) {
   return (u_save((LineNr)(curPor->cursor.lnum - 1), (LineNr)(curPor->cursor.lnum + 1)));
}

//Save the lines between "top" and "bot" for both the "u" and "U" command. "top" may be 0 and 
//"bot" may be curBook->mem.lineCount + 1. Careful: may trigger autocommands that reload the 
//book. Return FAIL when lines could not be saved, OK otherwise.
pub int
u_save(LineNr top, LineNr bot) {
   if (undo_off)
      return OK;

   if (top >= bot || bot > curBook->mem.lineCount + 1)
      return FAIL;   //rely on caller to give an error message

   if (top + 2 == bot)
      u_saveline((LineNr)(top + 1));

   return (u_savecommon(top, bot, (LineNr)0, false));
}

//Save the line "lnum" (used by ":s" and "~" command). The line is replaced, so the new bottom line
//is lnum + 1. Careful: may trigger autocommands that reload the book.
//Return FAIL when lines could not be saved, OK otherwise.
pub int
u_savesub(LineNr lnum) {
   if (undo_off)
      return OK;

   return (u_savecommon(lnum - 1, lnum + 1, lnum + 1, false));
}

//A new line is inserted before line "lnum" (used by :s command). The line is inserted, so the new 
//bottom line is lnum + 1. Careful: may trigger autocommands that reload the book.
//Return FAIL when lines could not be saved, OK otherwise.
private int
u_inssub(LineNr lnum) {
   if (undo_off)
      return OK;

   return (u_savecommon(lnum - 1, lnum, lnum + 1, false));
}

//Save the lines "lnum" - "lnum" + nlines (used by delete command).
//The lines are deleted, so the new bottom line is lnum, unless the book becomes empty.
//Careful: may trigger autocommands that reload the book.
//Return FAIL when lines could not be saved, OK otherwise.
pub int
u_savedel(LineNr lnum, long nlines) {
   if (undo_off)
      return OK;

   return (u_savecommon(lnum - 1, lnum + nlines,
           nlines == curBook->mem.lineCount ? 2 : lnum, false));
}

//true when undo is allowed.  Otherwise give an error message and return false.
pub int
undo_allowed(void) {
   //Don't allow changes when @modifiable is off.
   if (IMMUTABLE) {
      emsg(_(e_cannot_make_changes_modifiable_is_off));
      return false;
   }

   //Don't allow changes in the book while editing the commline. The
   //caller of getCommline() may get confused.
   if (textlock != 0) {
      emsg(_(e_not_allowed_to_change_text_or_change_portal));
      return false;
   }

   return true;
}

//u_save_line(): save an allocated copy of line "lnum" into "ul".
//Return FAIL when out of memory.
private int
u_save_line(UndoLine *ul, LineNr lnum) {
   CS line = ml_get(lnum);
   ul->ul_textlen = ml_get_len(lnum);
   if (curBook->mem.lineLen == 0) {
      ul->ul_len = 1;
      ul->ul_line = copyStr(S"");
    } else {
      //This uses the length in the memline, thus text properties are included.
      ul->ul_len = curBook->mem.lineLen;
      ul->ul_line = eeMemsave(line, ul->ul_len);
   }
   return ul->ul_line == NULL ? FAIL : OK;
}

//return true if line "lnum" has text property "flags".
private Boole
has_prop_w_flags(LineNr lnum, int flags) {
   CS props;
   int proplen = get_text_props(OUT &props, curBook, lnum, false);

   for (int i = 0; i < proplen; ++i) {
      TextProp prop;
      MEMMOVE(OUT &prop, props + i * sizeof prop, sizeof prop);
      if ((prop.flags & flags) != 0)
         return true;
   }
   return false;
}

//Common code for various ways to save text before a change.
//"top" is the line above the first changed line.
//"bot" is the line below the last changed line.
//"newbot" is the new bottom line.  Use zero when not known.
//"reload" is true when saving for a book reload.
//Careful: may trigger autocommands that reload the book.
//Return FAIL when lines could not be saved, OK otherwise.
pub int
u_savecommon(LineNr top, LineNr bot, LineNr newbot, int reload) {
   LineNr   lnum;
   long   i;
   UndoHeader   *uhp;
   UndoHeader   *old_curhead;
   UndoEntry   *uep;
   UndoEntry   *prev_uep;
   long   size;

   if (!reload) {
      //When making changes is not allowed return FAIL.  It's a crude way
      //to make all change commands fail.
      if (!undo_allowed())
          return FAIL;

      //A change in a terminal book removes the hiliting.
      uiBeforeLeavingTerminal();

      //Saving text for undo means we are going to make a change.  Give a
      //warning for a read-only file before making the change, so that the
      //FileChangedRO event can replace the book with a read-write version
      //(e.g., obtained from a source control system).
      change_warning(0);
      if (bot > curBook->mem.lineCount + 1) {
          //This happens when the FileChangedRO autocommand changes the
          //file in a way it becomes shorter.
          emsg(_(e_line_count_changed_unexpectedly));
          return FAIL;
      }
   }

#ifdef U_DEBUG
    u_check(false);
#endif

   //Include the line above if a text property continues from it.
   //Include the line below if a text property continues to it.
   if (bot - top > 1) {
      if (top > 0 && has_prop_w_flags(top + 1, TEXT_PROP_CONT_PREV))
          --top;
      if (bot <= curBook->mem.lineCount && has_prop_w_flags(bot - 1, TEXT_PROP_CONT_NEXT)) {
          ++bot;
          if (newbot != 0)
         ++newbot;
      }
   }

   size = bot - top - 1;

   //If curBook->undo.synced == true make a new header.
   if (curBook->undo.synced) {
      //Need to create new entry in changeList.
      curBook->newChange = true;

      if (p_ul >= 0) {
         //Make a new header entry.  Do this first so that we don't mess
         //up the undo info when out of memory.
         uhp = U_ALLOC_LINE(sizeof(UndoHeader));
#ifdef U_DEBUG
         uhp->uh_magic = UH_MAGIC;
#endif
      } else
         uhp = NULL;

      //If we undid more than we redid, move the entry lists before and
      //including curBook->undo.currHead to an alternate branch.
      old_curhead = curBook->undo.currHead;
      if (old_curhead != NULL) {
          curBook->undo.newHead = old_curhead->next.ptr;
          curBook->undo.currHead = NULL;
      }

      //free headers to keep the size right
      while (curBook->undo.countHeaders > p_ul && curBook->undo.oldHead != NULL) {
         UndoHeader       *uhfree = curBook->undo.oldHead;

         if (uhfree == old_curhead)
            //Can't reconnect the branch, delete all of it.
            freeBranch(curBook, uhfree, &old_curhead);
         ei (uhfree->altNext.ptr == NULL)
            //There is no branch, only free one header.
            u_freeheader(curBook, uhfree, &old_curhead);
         else {
            //Free the oldest alternate branch as a whole.
            while (uhfree->altNext.ptr != NULL)
               uhfree = uhfree->altNext.ptr;
            freeBranch(curBook, uhfree, &old_curhead);
         }
#ifdef U_DEBUG
         u_check(true);
#endif
      }

      if (!uhp) {     //no undo at all
         if (old_curhead != NULL)
            freeBranch(curBook, old_curhead, NULL);
         curBook->undo.synced = false;
         return OK;
      }

      uhp->prev.ptr = NULL;
      uhp->next.ptr = curBook->undo.newHead;
      uhp->altNext.ptr = old_curhead;
      if (old_curhead) {
         uhp->altPrev.ptr = old_curhead->altPrev.ptr;
         if (uhp->altPrev.ptr != NULL)
            uhp->altPrev.ptr->altNext.ptr = uhp;
         old_curhead->altPrev.ptr = uhp;
         if (curBook->undo.oldHead == old_curhead)
            curBook->undo.oldHead = uhp;
      } else
         uhp->altPrev.ptr = NULL;
      if (curBook->undo.newHead != NULL)
         curBook->undo.newHead->prev.ptr = uhp;

      uhp->uh_seq = ++curBook->undo.seqLast;
      curBook->undo.seqCurr = uhp->uh_seq;
      uhp->uh_time = eeTime();
      uhp->uh_save_nr = 0;
      curBook->undo.timeCurr = uhp->uh_time + 1;

      uhp->uh_walk = 0;
      uhp->uh_entry = NULL;
      uhp->uh_getbot_entry = NULL;
      uhp->uh_cursor = curPor->cursor;   //save cursor pos. for undo
      if (virtual_active() && curPor->cursor.coladd > 0)
         uhp->uh_cursor_vcol = getviscol();
      else
         uhp->uh_cursor_vcol = -1;

      //save changed and book empty flag for undo
      uhp->uh_flags = (curBook->wasModified ? UH_CHANGED : 0) +
                ((curBook->mem.flags & ML_EMPTY) ? UH_EMPTYBUF : 0);

      //save named marks and Visual marks for undo
      MEMMOVE(uhp->uh_namedm, curBook->namedMarks, sizeof(Pos) * NMARKS);
      uhp->uh_visual = curBook->visual;

      curBook->undo.newHead = uhp;
      if (curBook->undo.oldHead == NULL)
         curBook->undo.oldHead = uhp;
      curBook->undo.countHeaders++;
   } else {
      if (p_ul < 0)   //no undo at all
         return OK;

      //When saving a single line, and it has been saved just before, it
      //doesn't make sense saving it again.  Saves a lot of memory when
      //making lots of changes inside the same line.
      //This is only possible if the previous change didn't increase or
      //decrease the number of lines.
      //Check the ten last changes.  More doesn't make sense and takes too long.
      if (size == 1) {
         uep = u_get_headentry();
         prev_uep = NULL;
         for (i = 0; i < 10; ++i) {
            if (!uep)
                break;

            //If lines have been inserted/deleted we give up.
            //Also when the line was included in a multi-line save.
            if ((curBook->undo.newHead->uh_getbot_entry != uep
                   ? (uep->ue_top + uep->ue_size + 1
                  != (uep->ue_bot == 0
                      ? curBook->mem.lineCount + 1
                      : uep->ue_bot))
                   : uep->ue_lcount != curBook->mem.lineCount)
               || (uep->ue_size > 1
                   && top >= uep->ue_top
                   && top + 2 <= uep->ue_top + uep->ue_size + 1))
                break;

            //If it's the same line we can skip saving it again.
            if (uep->ue_size == 1 && uep->ue_top == top) {
               if (i > 0) {
                  //It's not the last entry: get ue_bot for the last
                  //entry now. Following deleted/inserted lines go to the re-used entry.
                  u_getbot();
                  curBook->undo.synced = false;

                  //Move the found entry to become the last entry. The order of undo/redo doesn't 
                  //matter for the entries we move it over, since they don't change the line
                  //count and don't include this line. It does matter for the found entry if the line 
                  //count is changed by the executed command.
                  prev_uep->ue_next = uep->ue_next;
                  uep->ue_next = curBook->undo.newHead->uh_entry;
                  curBook->undo.newHead->uh_entry = uep;
               }

                //The executed command may change the line count.
                if (newbot != 0)
               uep->ue_bot = newbot;
                ei (bot > curBook->mem.lineCount)
               uep->ue_bot = 0;
                else {
               uep->ue_lcount = curBook->mem.lineCount;
               curBook->undo.newHead->uh_getbot_entry = uep;
                }
                return OK;
            }
            prev_uep = uep;
            uep = uep->ue_next;
          }
      }

      //find line number for ue_bot for previous u_save()
      u_getbot();
    }

   //add lines in front of entry list
   uep = U_ALLOC_LINE(sizeof(UndoEntry));
   CLEAR_POINTER(uep);
#ifdef U_DEBUG
   uep->ue_magic = UE_MAGIC;
#endif

   uep->ue_size = size;
   uep->ue_top = top;
   if (newbot != 0)
      uep->ue_bot = newbot;
      //Use 0 for ue_bot if bot is below last line. Otherwise we have to compute ue_bot later.
   ei (bot > curBook->mem.lineCount)
      uep->ue_bot = 0;
   else {
      uep->ue_lcount = curBook->mem.lineCount;
      curBook->undo.newHead->uh_getbot_entry = uep;
   }

   if (size > 0) {
      uep->ue_array = U_ALLOC_LINE(sizeof(UndoLine) * size);
      for (i = 0, lnum = top + 1; i < size; ++i) {
          fast_breakcheck();
          if (gotInterruptG) {
             freeEntry(uep, i);
             return FAIL;
          }
          if (u_save_line(&uep->ue_array[i], lnum++) == FAIL) {
             freeEntry(uep, i);
             goto nomem;
          }
      }
    } else {
       uep->ue_array = NULL;
    }
    uep->ue_next = curBook->undo.newHead->uh_entry;
    curBook->undo.newHead->uh_entry = uep;
    curBook->undo.synced = false;
    undo_undoes = false;

#ifdef U_DEBUG
    u_check(false);
#endif
    return OK;

nomem:
    msg_silent = 0;   //must display the prompt
    if (ask_yesno((CS)_("No undo possible; continue anyway"), true) == 'y') {
       undo_off = true;       //will be reset when character typed
       return OK;
    }
    do_outofmem_msg((Ulong)0);
    return FAIL;
}


# define UF_START_MAGIC       "Vim\237UnDo\345"  //magic at start of undofile
# define UF_START_MAGIC_LEN   9
# define UF_HEADER_MAGIC   0x5fd0   //magic at start of header
# define UF_HEADER_END_MAGIC   0xe7aa   //magic after last header
# define UF_ENTRY_MAGIC      0xf518   //magic at start of entry
# define UF_ENTRY_END_MAGIC   0x3581   //magic after last entry
# define UF_VERSION      2   //2-byte undofile version number

//extra fields for header
# define UF_LAST_SAVE_NR   1

//extra fields for uhp
# define UHP_SAVE_NR      1

//Compute the hash for the current buffer text into hash[UNDO_HASH_SIZE].
pub void
u_compute_hash(OUT Byte hash[UNDO_HASH_SIZE]) {
   ContextSha256 ctx;
   LineNr lnum;

   sha256_start(&ctx);
   for (lnum = 1; lnum <= curBook->mem.lineCount; ++lnum)
      sha256_update(&ctx, ml_get(lnum), (Unt)(ml_get_len(lnum) + 1));
   sha256_finish(&ctx, hash);
}

private void
corruption_error(char *mesg, CS file_name) {
   showErrFmtMsg(_(e_corrupted_undo_file_str_str), mesg, file_name);
}

private void
u_free_uhp(UndoHeader *uhp) {
   UndoEntry* uep = uhp->uh_entry;
   while (uep) {
      UndoEntry* nuep = uep->ue_next;
      freeEntry(uep, uep->ue_size);
      uep = nuep;
   }
   eeglFree(uhp);
}

//Write a sequence of bytes to the undo file. Book as needed. Return OK or FAIL.
private int
writeToUndoFile(BufInfo* bi, Arr(Byte) ptr, Unt len) {
   if (fwrite(ptr, len, (Unt)1, bi->file) != 1)
      return FAIL;
   return OK;
}

//Write a number, most significant byte first, in "len" bytes.
//Must match with undo_read_?c() functions.
//Return OK or FAIL.
private int
undo_write_bytes(BufInfo* bi, Ulong nr, int len) {
   Byte  buf[8];
   int i;
   int bufi = 0;

   for (i = len - 1; i >= 0; --i)
      buf[bufi++] = (Byte)(nr >> (i * 8));
   return writeToUndoFile(bi, buf, (Unt)len);
}

//Write the pointer to an undo header.  Instead of writing the pointer itself
//we use the sequence number of the header.  This is converted back to
//pointers when reading.
private void
put_header_ptr(BufInfo *bi, UndoHeader *uhp) {
   undo_write_bytes(bi, (Ulong)(uhp != NULL ? uhp->uh_seq : 0), 4);
}

private int
undo_read_4c(BufInfo *bi) {
   return get4c(bi->file);
}

private int
undo_read_2c(BufInfo *bi) {
   return get2c(bi->file);
}

private int
undo_read_byte(BufInfo *bi) {
   return getc(bi->file);
}

private time_t
undo_read_time(BufInfo *bi) {
   return get8ctime(bi->file);
}

//Read "buffer[size]" from the undo file. Return OK or FAIL.
private int
undo_read(BufInfo *bi, CS buffer, Unt size) {
   int retval = OK;

   if (fread(buffer, size, 1, bi->file) != 1)
      retval = FAIL;

   if (retval == FAIL)
      //Error may be checked for only later. Fill with zeros, so that the reader won't use garbage
      memset(buffer, 0, size);
   return retval;
}

//Read a string of length "len" from "bi->bi_fd". "len" can be zero to allocate an empty line.
//Append a ZERO. Return a pointer to allocated memory or NULL for failure.
private CS
readStringFromFile(BufInfo *bi, Unt len) {
   CS ptr = alloc(len + 1);

   if (len > 0 && undo_read(bi, ptr, len) == FAIL) {
      eeglFree(ptr);
      return NULL;
   }
   //In case there are text properties there already is a ZERO, but
   //checking for that is more expensive than just adding a dummy byte.
   ptr[len] = ZERO;
   return ptr;
}

//Writes the header
private int
serialize_header(BufInfo* bi, Arr(Byte) hash) {
   Book* book = bi->bk;
   FILE* fp = bi->file;
   Byte time_buf[8];

   //Start writing, first the magic marker and undo info version.
   if (fwrite(UF_START_MAGIC, (Unt)UF_START_MAGIC_LEN, (Unt)1, fp) != 1)
      return FAIL;

   undo_write_bytes(bi, (Ulong)UF_VERSION, 2);

   //Write a hash of the buffer text, so that we can verify it is still the
   //same when reading the buffer text.
   if (writeToUndoFile(bi, hash, (Unt)UNDO_HASH_SIZE) == FAIL)
      return FAIL;

   //book-specific data
   undo_write_bytes(bi, (Ulong)book->mem.lineCount, 4);
   undo_write_bytes(bi, (Ulong)book->undo.line.ul_textlen, 4);
   if (book->undo.line.ul_textlen > 0 
         && writeToUndoFile(bi, book->undo.line.ul_line, (Unt)book->undo.line.ul_textlen) == FAIL
   )
      return FAIL;
   undo_write_bytes(bi, (Ulong)book->undo.lineLnum, 4);
   undo_write_bytes(bi, (Ulong)book->undo.lineCol, 4);

   //Undo structures header data
   put_header_ptr(bi, book->undo.oldHead);
   put_header_ptr(bi, book->undo.newHead);
   put_header_ptr(bi, book->undo.currHead);

   undo_write_bytes(bi, (Ulong)book->undo.countHeaders, 4);
   undo_write_bytes(bi, (Ulong)book->undo.seqLast, 4);
   undo_write_bytes(bi, (Ulong)book->undo.seqCurr, 4);
   time_to_bytes(book->undo.timeCurr, time_buf);
   writeToUndoFile(bi, time_buf, 8);

   //Optional fields.
   undo_write_bytes(bi, 4, 1);
   undo_write_bytes(bi, UF_LAST_SAVE_NR, 1);
   undo_write_bytes(bi, (Ulong)book->undo.saveNrLast, 4);

   undo_write_bytes(bi, 0, 1);  //end marker

   return OK;
}

private int
serialize_uhp(BufInfo* bi, UndoHeader* uhp) {
   Byte time_buf[8];

   if (undo_write_bytes(bi, (Ulong)UF_HEADER_MAGIC, 2) == FAIL)
      return FAIL;

   put_header_ptr(bi, uhp->next.ptr);
   put_header_ptr(bi, uhp->prev.ptr);
   put_header_ptr(bi, uhp->altNext.ptr);
   put_header_ptr(bi, uhp->altPrev.ptr);
   undo_write_bytes(bi, uhp->uh_seq, 4);
   serialize_pos(bi, uhp->uh_cursor);
   undo_write_bytes(bi, (Ulong)uhp->uh_cursor_vcol, 4);
   undo_write_bytes(bi, (Ulong)uhp->uh_flags, 2);
   //Assume NMARKS will stay the same.
   for (int i = 0; i < NMARKS; ++i)
      serialize_pos(bi, uhp->uh_namedm[i]);
   serialize_visualinfo(bi, &uhp->uh_visual);
   time_to_bytes(uhp->uh_time, time_buf);
   writeToUndoFile(bi, time_buf, 8);

   //Optional fields.
   undo_write_bytes(bi, 4, 1);
   undo_write_bytes(bi, UHP_SAVE_NR, 1);
   undo_write_bytes(bi, (Ulong)uhp->uh_save_nr, 4);

   undo_write_bytes(bi, 0, 1);  //end marker

   //Write all the entries.
   for (UndoEntry* uep = uhp->uh_entry; uep != NULL; uep = uep->ue_next) {
      undo_write_bytes(bi, (Ulong)UF_ENTRY_MAGIC, 2);
      if (serialize_uep(bi, uep) == FAIL)
         return FAIL;
   }
   undo_write_bytes(bi, (Ulong)UF_ENTRY_END_MAGIC, 2);
   return OK;
}

private UndoHeader *
unserialize_uhp(BufInfo* bi, CS file_name) {
   int i;
   int c;
   int error;

   UndoHeader* uhp = U_ALLOC_LINE(sizeof(UndoHeader));
   CLEAR_POINTER(uhp);
#ifdef U_DEBUG
   uhp->uh_magic = UH_MAGIC;
#endif
   uhp->next.seq = undo_read_4c(bi);
   uhp->prev.seq = undo_read_4c(bi);
   uhp->altNext.seq = undo_read_4c(bi);
   uhp->altPrev.seq = undo_read_4c(bi);
   uhp->uh_seq = undo_read_4c(bi);
   if (uhp->uh_seq <= 0) {
      corruption_error("uh_seq", file_name);
      eeglFree(uhp);
      return NULL;
   }
   deserializePos(bi, &uhp->uh_cursor);
   uhp->uh_cursor_vcol = undo_read_4c(bi);
   uhp->uh_flags = undo_read_2c(bi);
   for (i = 0; i < NMARKS; ++i)
      deserializePos(bi, &uhp->uh_namedm[i]);
   unserialize_visualinfo(bi, &uhp->uh_visual);
   uhp->uh_time = undo_read_time(bi);

   //Optional fields.
   for (;;) {
      int len = undo_read_byte(bi);
      int what;

      if (len == EOF) {
          corruption_error("truncated", file_name);
          u_free_uhp(uhp);
          return NULL;
      }
      if (len == 0)
          break;
      what = undo_read_byte(bi);
      switch (what) {
      case UHP_SAVE_NR:
         uhp->uh_save_nr = undo_read_4c(bi);
         break;
      default:
         //field not supported, skip
         while (--len >= 0)
             (void)undo_read_byte(bi);
      }
   }

   //Unserialize the uep list.
   UndoEntry* last_uep = NULL;
   while ((c = undo_read_2c(bi)) == UF_ENTRY_MAGIC) {
      error = false;
      UndoEntry* uep = unserialize_uep(bi, &error, file_name);
      if (last_uep)
         last_uep->ue_next = uep;
      else
         uhp->uh_entry = uep;
      last_uep = uep;
      if (!uep || error) {
          u_free_uhp(uhp);
          return NULL;
      }
   }
   if (c != UF_ENTRY_END_MAGIC) {
      corruption_error("entry end", file_name);
      u_free_uhp(uhp);
      return NULL;
   }

   return uhp;
}

//Serialize "uep".
private int
serialize_uep(BufInfo* bi, UndoEntry* uep) {
   undo_write_bytes(bi, (Ulong)uep->ue_top, 4);
   undo_write_bytes(bi, (Ulong)uep->ue_bot, 4);
   undo_write_bytes(bi, (Ulong)uep->ue_lcount, 4);
   undo_write_bytes(bi, (Ulong)uep->ue_size, 4);
   for (int i = 0; i < uep->ue_size; ++i) {
   //Text is written without the text properties, since we cannot restore
   //the text property types.
   if (undo_write_bytes(bi, (Ulong)uep->ue_array[i].ul_textlen, 4) == FAIL)
       return FAIL;
   if (uep->ue_array[i].ul_textlen > 0
      && writeToUndoFile(bi, uep->ue_array[i].ul_line, uep->ue_array[i].ul_textlen) == FAIL)
       return FAIL;
   }
   return OK;
}

private UndoEntry *
unserialize_uep(BufInfo *bi, int *error, CS file_name) {
   UndoLine   *array = NULL;
   CS line;

   UndoEntry* uep = U_ALLOC_LINE(sizeof(UndoEntry));
   CLEAR_POINTER(uep);
#ifdef U_DEBUG
   uep->ue_magic = UE_MAGIC;
#endif
   uep->ue_top = undo_read_4c(bi);
   uep->ue_bot = undo_read_4c(bi);
   uep->ue_lcount = undo_read_4c(bi);
   uep->ue_size = undo_read_4c(bi);
   if (uep->ue_size > 0) {
      if (uep->ue_size < (Long)LONG_MAX / (int)sizeof(CS))
         array = U_ALLOC_LINE(sizeof(UndoLine) * uep->ue_size);
      if (array == NULL) {
         *error = true;
         return uep;
      }
      memset(array, 0, sizeof(UndoLine) * uep->ue_size);
   }
   uep->ue_array = array;

   for (int i = 0; i < uep->ue_size; ++i) {
      int line_len = undo_read_4c(bi);
      if (line_len >= 0)
         line = readStringFromFile(bi, (Unt)line_len);
      else {
         line = NULL;
         corruption_error("line length", file_name);
      }
      if (line == NULL) {
         *error = true;
         return uep;
      }
      array[i].ul_line = line;
      array[i].ul_len = line_len + 1;
      array[i].ul_textlen = line_len;
   }
    return uep;
}

//Serialize "pos".
private void
serialize_pos(BufInfo *bi, Pos pos) {
   undo_write_bytes(bi, (Ulong)pos.lnum, 4);
   undo_write_bytes(bi, (Ulong)pos.col, 4);
   undo_write_bytes(bi, (Ulong)pos.coladd, 4);
}

//Deserialize the Pos at the current position.
private void
deserializePos(BufInfo *bi, Pos *pos) {
   pos->lnum = undo_read_4c(bi);
   if (pos->lnum < 0)
      pos->lnum = 0;
   pos->col = undo_read_4c(bi);
   if (pos->col < 0)
      pos->col = 0;
   pos->coladd = undo_read_4c(bi);
   if (pos->coladd < 0)
      pos->coladd = 0;
}

//Serialize "info".
private void
serialize_visualinfo(BufInfo *bi, VisualInfo *info) {
   serialize_pos(bi, info->vi_start);
   serialize_pos(bi, info->vi_end);
   undo_write_bytes(bi, (Ulong)info->vi_mode, 4);
   undo_write_bytes(bi, (Ulong)info->vi_curswant, 4);
}

//Unserialize the VisualInfo at the current position.
private void
unserialize_visualinfo(BufInfo *bi, VisualInfo *info) {
   deserializePos(bi, &info->vi_start);
   deserializePos(bi, &info->vi_end);
   info->vi_mode = undo_read_4c(bi);
   info->vi_curswant = undo_read_4c(bi);
}

//Write the undo tree into an undo file.
//When "name" is not NULL, use it as the name of the undo file.
//Otherwise use book->fullFileName to generate the undo file name.
//"book" must never be null, book->fullFileName is used to obtain the original file permissions.
//"forceit" is true for ":wundo!", false otherwise.
//"hash[UNDO_HASH_SIZE]" must be the hash value of the buffer text.
pub void
u_write_undo(CS name, Boole forceit, Book* book, Arr(Byte) hash) {
   UndoHeader* uhp;
   CS file_name;
   int      mark;
#ifdef U_DEBUG
   int headers_written = 0;
#endif
   int fd;
   int write_ok = false;
   int st_old_valid = false;
   FileStat st_old;
   FileStat st_new;
   BufInfo bi;
   CLEAR_FIELD(bi);

   if (!name) {
      file_name = fiBuildSwapOrUndoFname(book->fullFileName, false);
      if (file_name == NULL) {
         if (p_verbose > 0) {
            verbose_enter();
            smsg(_("Cannot write undo file in any directory in 'undodir'"));
            verbose_leave();
         }
         return;
      }
   } else
      file_name = name;

   //Decide about the permission to use for the undo file.  If the book
   //has a name use the permission of the original file.  Otherwise only
   //allow the user to access the undo file.
   Unt perm = 0600;
   if (book->fullFileName) {
      if (stat((char *)book->fullFileName, OUT &st_old) >= 0) {
          perm = st_old.st_mode;
          st_old_valid = true;
      }
   }

   //strip any s-bit and executable bit
   perm = perm & 0666;

   //If the undo file already exists, verify that it actually is an undo file, and delete it.
   if (mch_getperm(file_name) >= 0) {
      if (!name || !forceit) {
         //Check we can read it and it's an undo file.
         fd = open((char *)file_name, O_RDONLY|O_EXTRA, 0);
         if (fd < 0) {
            if (p_verbose > 0)
               verbose_enter();
            if (name || p_verbose > 0)
               smsg( _("Will not overwrite with undo file, cannot read: %s"), file_name);
            if (p_verbose > 0)
               verbose_enter();
            goto theend;
         } else {
            Byte mbuf[UF_START_MAGIC_LEN];
            int len = fiReadEintr(fd, mbuf, UF_START_MAGIC_LEN);
            close(fd);
            
            if (len < UF_START_MAGIC_LEN || memcmp(mbuf, UF_START_MAGIC, UF_START_MAGIC_LEN) != 0) {
               if (p_verbose > 0)
                  verbose_enter();
               if (name || p_verbose > 0)
                  smsg(_("Will not overwrite, this is not an undo file: %s"), file_name);
               if (p_verbose > 0)
                  verbose_leave();
               goto theend;
            }
         }
      }
      mch_remove(file_name);
   }

   //If there is no undo information at all, quit here after deleting any
   //existing undo file.
   if (book->undo.countHeaders == 0 && book->undo.line.ul_line == NULL) {
      if (p_verbose > 0)
         verb_msg(_("Skipping undo file write, nothing to undo"));
      goto theend;
   }

   fd = open((char *)file_name, O_CREAT|O_EXTRA|O_WRONLY|O_EXCL|O_NOFOLLOW, perm);
   if (fd < 0) {
      showErrFmtMsg(_(e_cannot_open_undo_file_for_writing_str), file_name);
      goto theend;
   }
   (void)mch_setperm(file_name, perm);
   if (p_verbose > 0) {
      verbose_enter();
      smsg(_("Writing undo file: %s"), file_name);
      verbose_leave();
   }

#ifdef U_DEBUG
    //Check there is no problem in undo info before writing.
    u_check(false);
#endif

    //Try to set the group of the undo file same as the original file. If
    //this fails, set the protection bits for the group same as the protection bits for others.
    if (st_old_valid
       && STAT(file_name, OUT &st_new) >= 0
       && st_new.st_gid != st_old.st_gid
       && fchown(fd, (uid_t)-1, st_old.st_gid) != 0
    )
      mch_setperm(file_name, (perm & 0707) | ((perm & 07) << 3));

   FILE* fp = fdopen(fd, "w");
   if (!fp) {
      showErrFmtMsg(_(e_cannot_open_undo_file_for_writing_str), file_name);
      close(fd);
      mch_remove(file_name);
      goto theend;
   }

   //Undo must be synced.
   u_sync(true);

   bi.bk = book;
   bi.file = fp;
   if (serialize_header(&bi, hash) == FAIL)
      goto write_error;

   //Iteratively serialize UHPs and their UEPs from the top down.
   mark = ++lastmark;
   uhp = book->undo.oldHead;
   while (uhp) {
      //Serialize current UHP if we haven't seen it
      if (uhp->uh_walk != mark) {
         uhp->uh_walk = mark;
#ifdef U_DEBUG
         ++headers_written;
#endif
         if (serialize_uhp(&bi, uhp) == FAIL)
            goto write_error;
      }

      //Now walk through the tree - algorithm from undo_time().
      if (uhp->prev.ptr && uhp->prev.ptr->uh_walk != mark)
         uhp = uhp->prev.ptr;
      ei (uhp->altNext.ptr && uhp->altNext.ptr->uh_walk != mark)
         uhp = uhp->altNext.ptr;
      ei (uhp->next.ptr && !uhp->altPrev.ptr && uhp->next.ptr->uh_walk != mark)
         uhp = uhp->next.ptr;
      ei (uhp->altPrev.ptr)
         uhp = uhp->altPrev.ptr;
      else
         uhp = uhp->next.ptr;
   }

   if (undo_write_bytes(&bi, (Ulong)UF_HEADER_END_MAGIC, 2) == OK)
      write_ok = true;
#ifdef U_DEBUG
   if (headers_written != book->undo.countHeaders) {
      showErrFmtMsg("Written %ld headers, ...", headers_written);
      showErrFmtMsg("... but numhead is %ld", book->undo.countHeaders);
   }
#endif

   if (p_fs && fflush(fp) == 0 && eeFsync(fd) != 0)
      write_ok = false;

write_error:
   fclose(fp);
   if (!write_ok)
      showErrFmtMsg(_(e_write_error_in_undo_file_str), file_name);


theend:
   if (file_name != name)
      eeglFree(file_name);
}

//Load the undo tree from an undo file.
//If "name" is not NULL use it as the undo file name. This also means being
//a bit more verbose.
//Otherwise use curBook->fullFileName to generate the undo file name.
//"hash[UNDO_HASH_SIZE]" must be the hash value of the buffer text.
pub void
u_read_undo(CS name, Arr(Byte) hash, CS orig_name) {
   CS file_name;
   UndoLine line_ptr;
   long  last_save_nr = 0;
   int  old_idx = -1, neidx = -1, cur_idx = -1;
   long  num_read_uhps = 0;
   Tyme seq_time;
   int c;
   UndoHeader* uhp;
   UndoHeader** uhp_table = NULL;
   Byte read_hash[UNDO_HASH_SIZE];
   Byte magic_buf[UF_START_MAGIC_LEN];
#ifdef U_DEBUG
   int* uhp_table_used;
#endif
   FileStat st_orig;
   FileStat st_undo;
   BufInfo bi;

   CLEAR_FIELD(bi);
   line_ptr.ul_len = 0;
   line_ptr.ul_textlen = 0;
   line_ptr.ul_line = NULL;

   if (!name) {
      file_name = fiBuildSwapOrUndoFname(curBook->fullFileName, true);
      if (!file_name)
          return;

      //For safety we only read an undo file if the owner is equal to the
      //owner of the text file or equal to the current user.
      if (stat((char *)orig_name, &st_orig) >= 0
         && stat((char *)file_name, &st_undo) >= 0
         && st_orig.st_uid != st_undo.st_uid
         && st_undo.st_uid != getuid()
      ) {
         if (p_verbose > 0) {
            verbose_enter();
            smsg(_("Not reading undo file, owner differs: %s"), file_name);
            verbose_leave();
         }
         return;
      }
   } else
      file_name = name;

   if (p_verbose > 0) {
      verbose_enter();
      smsg(_("Reading undo file: %s"), file_name);
      verbose_leave();
   }

   FILE* fp = fopen((char *)file_name, "r");
   if (!fp) {
      if (name || p_verbose > 0)
         showErrFmtMsg(_(e_cannot_open_undo_file_for_reading_str), file_name);
      goto error;
   }
   bi.bk = curBook;
   bi.file = fp;

   //Read the undo file header.
   if (fread(magic_buf, UF_START_MAGIC_LEN, 1, fp) != 1
      || memcmp(magic_buf, UF_START_MAGIC, UF_START_MAGIC_LEN) != 0
   ) {
      showErrFmtMsg(_(e_not_an_undo_file_str), file_name);
      goto error;
   }
   int version = get2c(fp);
   if (version != UF_VERSION) {
      showErrFmtMsg(_(e_incompatible_undo_file_str), file_name);
      goto error;
   }

   if (undo_read(&bi, read_hash, (Unt)UNDO_HASH_SIZE) == FAIL) {
      corruption_error("hash", file_name);
      goto error;
   }
   LineNr line_count = (LineNr)undo_read_4c(&bi);
   if (memcmp(hash, read_hash, UNDO_HASH_SIZE) != 0 || line_count != curBook->mem.lineCount) {
      if (p_verbose > 0 || name != NULL) {
         if (!name)
            verbose_enter();
         give_warning((CS) _("File contents changed, cannot use undo info"), true);
         if (name == NULL)
            verbose_leave();
      }
      goto error;
   }

   //Read undo data for "U" command.
   int str_len = undo_read_4c(&bi);
   if (str_len < 0)
      goto error;
   if (str_len > 0) {
      line_ptr.ul_line = readStringFromFile(&bi, (Unt)str_len);
      line_ptr.ul_len = str_len + 1;
      line_ptr.ul_textlen = str_len;
   }
   LineNr line_lnum = (LineNr)undo_read_4c(&bi);
   LineNr line_colnr = (ColNr)undo_read_4c(&bi);
   if (line_lnum < 0 || line_colnr < 0) {
      corruption_error("line lnum/col", file_name);
      goto error;
   }

   //Begin general undo data
   long old_header_seq = undo_read_4c(&bi);
   long new_header_seq = undo_read_4c(&bi);
   long cur_header_seq = undo_read_4c(&bi);
   long num_head = undo_read_4c(&bi);
   long seq_last = undo_read_4c(&bi);
   long seq_cur = undo_read_4c(&bi);
   seq_time = undo_read_time(&bi);

   //Optional header fields.
   for (;;) {
      int len = undo_read_byte(&bi);
      if (len == 0 || len == EOF)
         break;
      int what = undo_read_byte(&bi);
      switch (what) {
      case UF_LAST_SAVE_NR:
         last_save_nr = undo_read_4c(&bi);
         break;
      default:
         //field not supported, skip
         while (--len >= 0)
             (void)undo_read_byte(&bi);
      }
   }

   //uhp_table will store the freshly created undo headers we allocate
   //until we insert them into curBook. The table remains sorted by the
   //sequence numbers of the headers.
   //When there are no headers uhp_table is NULL.
   if (num_head > 0) {
      if (num_head < (Long)LONG_MAX / (Long)sizeof(UndoHeader *))
         uhp_table = U_ALLOC_LINE(num_head * sizeof(UndoHeader *));
      if (uhp_table == NULL)
         goto error;
   }

   while ((c = undo_read_2c(&bi)) == UF_HEADER_MAGIC) {
      if (num_read_uhps >= num_head) {
         corruption_error("num_head too small", file_name);
         goto error;
      }

      uhp = unserialize_uhp(&bi, file_name);
      if (!uhp)
         goto error;
      uhp_table[num_read_uhps++] = uhp;
   }

   if (num_read_uhps != num_head) {
      corruption_error("num_head", file_name);
      goto error;
   }
   if (c != UF_HEADER_END_MAGIC) {
      corruption_error("end marker", file_name);
      goto error;
   }

#ifdef U_DEBUG
   uhp_table_used = allocZeroed(sizeof(int) * num_head + 1);
# define SET_FLAG(j) ++uhp_table_used[j]
#else
# define SET_FLAG(j)
#endif

   //We have put all of the headers into a table. Now we iterate through the
   //table and swizzle each sequence number we have stored in uh_*_seq into
   //a pointer corresponding to the header with that sequence number.
   for (int i = 0; i < num_head; i++) {
      uhp = uhp_table[i];
      if (!uhp)
          continue;
      for (int j = 0; j < num_head; j++) {
         if (uhp_table[j] && i != j && uhp_table[i]->uh_seq == uhp_table[j]->uh_seq) {
            corruption_error("duplicate uh_seq", file_name);
            goto error;
         }
      } 
      for (int j = 0; j < num_head; j++) {
         if (uhp_table[j] != NULL && uhp_table[j]->uh_seq == uhp->next.seq) {
            uhp->next.ptr = uhp_table[j];
            SET_FLAG(j);
            break;
         }
      } 
      for (int j = 0; j < num_head; j++) {
         if (uhp_table[j] != NULL && uhp_table[j]->uh_seq == uhp->prev.seq) {
            uhp->prev.ptr = uhp_table[j];
            SET_FLAG(j);
            break;
         }
      } 
      for (int j = 0; j < num_head; j++) {
         if (uhp_table[j] != NULL && uhp_table[j]->uh_seq == uhp->altNext.seq) {
            uhp->altNext.ptr = uhp_table[j];
            SET_FLAG(j);
            break;
         }
      } 
      for (int j = 0; j < num_head; j++) {
         if (uhp_table[j] != NULL && uhp_table[j]->uh_seq == uhp->altPrev.seq) {
            uhp->altPrev.ptr = uhp_table[j];
            SET_FLAG(j);
            break;
         }
      } 
      if (old_header_seq > 0 && old_idx < 0 && uhp->uh_seq == old_header_seq) {
         old_idx = i;
         SET_FLAG(i);
      }
      if (new_header_seq > 0 && neidx < 0 && uhp->uh_seq == new_header_seq) {
         neidx = i;
         SET_FLAG(i);
      }
      if (cur_header_seq > 0 && cur_idx < 0 && uhp->uh_seq == cur_header_seq) {
         cur_idx = i;
         SET_FLAG(i);
      }
   }

   //Now that we have read the undo info successfully, free the current undo
   //info and use the info from the file.
   u_blockfree(curBook);
   curBook->undo = (Undo){
       .oldHead = old_idx < 0 ? NULL : uhp_table[old_idx],    
       .newHead = neidx < 0 ? NULL : uhp_table[neidx],    
       .currHead = cur_idx < 0 ? NULL : uhp_table[cur_idx],    
       .line = line_ptr,     .lineLnum = line_lnum,    
       .lineCol = line_colnr, .countHeaders = num_head,    
       .seqLast = seq_last,     .seqCurr = seq_cur,    
       .timeCurr = seq_time,    
       .saveNrLast = last_save_nr,    
       .saveNrCurr = last_save_nr,
       .synced = true
   };

   eeglFree(uhp_table);

#ifdef U_DEBUG
   for (i = 0; i < num_head; ++i) {
      if (uhp_table_used[i] == 0)
         showErrFmtMsg("uhp_table entry %ld not used, leaking memory", i);
   } 
   eeglFree(uhp_table_used);
   u_check(true);
#endif

   if (name)
      smsg(_("Finished reading undo file %s"), file_name);
   goto theend;

error:
   eeglFree(line_ptr.ul_line);
   if (uhp_table) {
      for (int i = 0; i < num_read_uhps; i++) {
         if (uhp_table[i])
            u_free_uhp(uhp_table[i]);
      } 
      eeglFree(uhp_table);
   }

theend:
   if (fp)
      fclose(fp);
   if (file_name != name)
      eeglFree(file_name);
   return;
}

pub void
u_undo(int count) {
   //If we get an undo command while executing a macro, we behave like the
   //original vi. If this happens twice in one macro the result will not be compatible.
   if (curBook->undo.synced == false) {
      u_sync(true);
      count = 1;
   }

   undo_undoes = true;
   u_doit(count);
}

pub void
u_redo(int count) {
   undo_undoes = false;
   u_doit(count);
}

//Undo or redo, depending on 'undo_undoes', 'count' times.
private void
u_doit(int startcount) {

   if (!undo_allowed())
       return;

   int count = startcount;
   u_newcount = 0;
   u_oldcount = 0;
   if (curBook->mem.flags & ML_EMPTY)
       u_oldcount = -1;
   while (count--) {
         //Do the change warning now, so that it triggers FileChangedRO when
         //needed.  This may cause the file to be reloaded, that must happen
         //before we do anything, because it may change curBook->undo.currHead and more.
         change_warning(0);

         if (undo_undoes) {
            if (curBook->undo.currHead == NULL) { //first undo
               curBook->undo.currHead = curBook->undo.newHead;
            } ei (p_ul > 0) {//multi level undo
               //get next undo
               curBook->undo.currHead = curBook->undo.currHead->next.ptr;
            }
            //nothing to undo
            if (curBook->undo.countHeaders == 0 || curBook->undo.currHead == NULL) {
               //stick curBook->undo.currHead at end
               curBook->undo.currHead = curBook->undo.oldHead;
               inpFlushIfNotSilent();
               if (count == startcount - 1) {
                  msg(_("Already at oldest change"));
                  return;
               }
               break;
            }
            u_undoredo(true);
         } else {
             if (curBook->undo.currHead == NULL || p_ul <= 0) {
                inpFlushIfNotSilent();   //nothing to redo
                if (count == startcount - 1) {
                   msg(_("Already at newest change"));
                   return;
                }
                break;
             }

             u_undoredo(false);

             //Advance for next redo. Set "newhead" when at the end of the redoable changes.
             if (curBook->undo.currHead->prev.ptr == NULL)
                curBook->undo.newHead = curBook->undo.currHead;
             curBook->undo.currHead = curBook->undo.currHead->prev.ptr;
         }
    }
    u_undo_end(undo_undoes, false);
}

//Undo or redo over the timeline.
//When "step" is negative go back in time, otherwise goes forward in time.
//When "sec" is false make "step" steps, when "sec" is true use "step" as
//seconds.
//When "file" is true use "step" as a number of file writes.
//When "absolute" is true use "step" as the sequence number to jump to. "sec" must be false then.
pub void
undo_time(long step, int sec, int file, int absolute) {
   long target;
   long closest;
   long closest_start;
   long closest_seq = 0;
   long val;
   UndoHeader* uhp = NULL;
   UndoHeader* last;
   int mark;
   int nomark = 0;  //shut up compiler
   int round;
   int dosec = sec;
   int dofile = file;
   int above = false;
   int did_undo = true;

   if (text_locked()) {
      text_locked_msg();
      return;
   }

   //First make sure the current undoable change is synced.
   if (curBook->undo.synced == false)
      u_sync(true);

   u_newcount = 0;
   u_oldcount = 0;
   if (curBook->mem.flags & ML_EMPTY)
      u_oldcount = -1;

   //"target" is the node below which we want to be.
   //Init "closest" to a value we can't reach.
   if (absolute) {
      target = step;
      closest = -1;
   } else {
      if (dosec)
          target = (long)(curBook->undo.timeCurr) + step;
      ei (dofile) {
         if (step < 0) {
            //Going back to a previous write. If there were changes after the last write, count that as 
            //moving one file-write, so that ":earlier 1f" undoes all changes since the last save.
            uhp = curBook->undo.currHead;
            if (uhp)
               uhp = uhp->next.ptr;
            else
               uhp = curBook->undo.newHead;
            if (uhp && uhp->uh_save_nr != 0)
               //"uh_save_nr" was set in the last block, that means
               //there were no changes since the last write
               target = curBook->undo.saveNrCurr + step;
            else
               //count the changes since the last write as one step
               target = curBook->undo.saveNrCurr + step + 1;
            if (target <= 0)
               //Go to before first write: before the oldest change. Use the sequence number for that
               dofile = false;
         } else {
            //Moving forward to a newer write.
            target = curBook->undo.saveNrCurr + step;
            if (target > curBook->undo.saveNrLast) {
               //Go to after last write: after the latest change. Use the sequence number for that.
               target = curBook->undo.seqLast + 1;
               dofile = false;
            }
         }
      } else
         target = curBook->undo.seqCurr + step;
      if (step < 0) {
         if (target < 0)
            target = 0;
         closest = -1;
      } else {
         if (dosec)
            closest = (long)(eeTime() + 1);
         ei (dofile)
            closest = curBook->undo.saveNrLast + 2;
         else
            closest = curBook->undo.seqLast + 2;
         if (target >= closest)
            target = closest - 1;
      }
   }
   closest_start = closest;
   closest_seq = curBook->undo.seqCurr;

   //When "target" is 0; Back to origin.
   if (target == 0) {
      mark = lastmark;  //avoid that GCC complains
      goto target_zero;
   }

   //May do this twice:
   //1. Search for "target", update "closest" to the best match found.
   //2. If "target" not found search for "closest".
   //
   //When using the closest time we use the sequence number in the second
   //round, because there may be several entries with the same time.
   for (round = 1; round <= 2; ++round) {
      //Find the path from the current state to where we want to go.  The
      //desired state can be anywhere in the undo tree, need to go all over
      //it.  We put "nomark" in uh_walk where we have been without success,
      //"mark" where it could possibly be.
      mark = ++lastmark;
      nomark = ++lastmark;

      if (curBook->undo.currHead == NULL)   //at leaf of the tree
         uhp = curBook->undo.newHead;
      else
         uhp = curBook->undo.currHead;

      while (uhp) {
         uhp->uh_walk = mark;
         if (dosec)
            val = (long)(uhp->uh_time);
         ei (dofile)
            val = uhp->uh_save_nr;
         else
            val = uhp->uh_seq;

         if (round == 1 && !(dofile && val == 0)) {
            //Remember the header that is closest to the target. It must be at least in the right 
            //direction (checked with "seqCurr").  When the timestamp is equal find the
            //highest/lowest sequence number.
            if ((step < 0 ? uhp->uh_seq <= curBook->undo.seqCurr
                     : uhp->uh_seq > curBook->undo.seqCurr)
               && ((dosec && val == closest)
                   ? (step < 0
                  ? uhp->uh_seq < closest_seq
                  : uhp->uh_seq > closest_seq)
                   : closest == closest_start
                  || (val > target
                      ? (closest > target
                     ? val - target <= closest - target
                     : val - target <= target - closest)
                      : (closest > target
                     ? target - val <= closest - target
                     : target - val <= target - closest)))
            ) {
               closest = val;
               closest_seq = uhp->uh_seq;
            }
         }

         //Quit searching when we found a match.  But when searching for a
         //time we need to continue looking for the best uh_seq.
         if (target == val && !dosec) {
            target = uhp->uh_seq;
            break;
         }

         //go down in the tree if we haven't been there
         if (uhp->prev.ptr != NULL && uhp->prev.ptr->uh_walk != nomark
                   && uhp->prev.ptr->uh_walk != mark)
         uhp = uhp->prev.ptr;

         //go to alternate branch if we haven't been there
         ei (uhp->altNext.ptr
             && uhp->altNext.ptr->uh_walk != nomark
             && uhp->altNext.ptr->uh_walk != mark
         )
            uhp = uhp->altNext.ptr;

         //go up in the tree if we haven't been there and we are at the
         //start of alternate branches
         ei (uhp->next.ptr && !uhp->altPrev.ptr
             && uhp->next.ptr->uh_walk != nomark
             && uhp->next.ptr->uh_walk != mark
         ) {
            //If still at the start we don't go through this change.
            if (uhp == curBook->undo.currHead)
               uhp->uh_walk = nomark;
            uhp = uhp->next.ptr;
         } else {
            //need to backtrack; mark this node as useless
            uhp->uh_walk = nomark;
            if (uhp->altPrev.ptr != NULL)
               uhp = uhp->altPrev.ptr;
            else
               uhp = uhp->next.ptr;
         }
      }

      if (uhp)    //found it
         break;

      if (absolute) {
         showErrFmtMsg(_(e_undo_number_nr_not_found), step);
         return;
      }

      if (closest == closest_start) {
         if (step < 0)
            msg(_("Already at oldest change"));
         else
            msg(_("Already at newest change"));
         return;
      }

      target = closest_seq;
      dosec = false;
      dofile = false;
      if (step < 0)
         above = true;   //stop above the header
   }

target_zero:
   //If we found it: Follow the path to go to where we want to be.
   
   if (!uhp && target != 0) {
      goto theEnd;
   } 
   //First go up the tree as much as needed.
   while (!gotInterruptG) {
      //Do the change warning now, for the same reason as above.
      change_warning(0);

      uhp = curBook->undo.currHead;
      if (!uhp)
         uhp = curBook->undo.newHead;
      else
         uhp = uhp->next.ptr;
      if (!uhp || (target > 0 && uhp->uh_walk != mark) || (uhp->uh_seq == target && !above))
         break;
      curBook->undo.currHead = uhp;
      u_undoredo(true);
      if (target > 0)
         uhp->uh_walk = nomark;   //don't go back down here
   }

   //When back to origin, redo is not needed.
   if (target > 0) {
      //And now go down the tree (redo), branching off where needed.
      while (!gotInterruptG) {
         //Do the change warning now, for the same reason as above.
         change_warning(0);

         uhp = curBook->undo.currHead;
         if (!uhp)
            break;

         //Go back to the first branch with a mark.
         while (uhp->altPrev.ptr != NULL && uhp->altPrev.ptr->uh_walk == mark)
            uhp = uhp->altPrev.ptr;

         //Find the last branch with a mark, that's the one.
         last = uhp;
         while (last->altNext.ptr != NULL && last->altNext.ptr->uh_walk == mark)
            last = last->altNext.ptr;
         if (last != uhp) {
            //Make the used branch the first entry in the list of
            //alternatives to make "u" and CTRL-R take this branch.
            while (uhp->altPrev.ptr != NULL)
               uhp = uhp->altPrev.ptr;
            if (last->altNext.ptr != NULL)
               last->altNext.ptr->altPrev.ptr = last->altPrev.ptr;
            last->altPrev.ptr->altNext.ptr = last->altNext.ptr;
            last->altPrev.ptr = NULL;
            last->altNext.ptr = uhp;
            uhp->altPrev.ptr = last;

            if (curBook->undo.oldHead == uhp)
               curBook->undo.oldHead = last;
            uhp = last;
            if (uhp->next.ptr != NULL)
               uhp->next.ptr->prev.ptr = uhp;
         }
         curBook->undo.currHead = uhp;

         if (uhp->uh_walk != mark)
            break;       //must have reached the target

         //Stop when going backwards in time and didn't find the exact header we were looking for.
         if (uhp->uh_seq == target && above) {
            curBook->undo.seqCurr = target - 1;
            break;
         }

         u_undoredo(false);

         //Advance "curhead" to below the header we last used.  If it
         //becomes NULL then we need to set "newhead" to this leaf.
         if (uhp->prev.ptr == NULL)
            curBook->undo.newHead = uhp;
         curBook->undo.currHead = uhp->prev.ptr;
         did_undo = false;

         if (uhp->uh_seq == target)   //found it!
             break;

         uhp = uhp->prev.ptr;
         if (uhp == NULL || uhp->uh_walk != mark) {
             //Need to redo more but can't find it...
             internal_error((CS)"undo_time()");
             break;
         }
      }
   }
theEnd: 
   u_undo_end(did_undo, absolute);
}

//u_undoredo: common code for undo and redo
//
//The lines in the file are replaced by the lines in the entry list at
//curBook->undo.currHead. The replaced lines in the file are saved in the entry
//list for the next undo/redo.
//
//When "undo" is true we go up in the tree, when false we go down.
private void
u_undoredo(Boole undo) {
   UndoLine* newarray = NULL;
   LineNr oldsize;
   LineNr newsize;
   LineNr top, bot;
   LineNr lnum;
   LineNr newlnum = MAXLNUM;
   Pos new_curpos = curPor->cursor;
   long i;
   UndoEntry *newlist = NULL;
   int old_flags;
   int new_flags;
   Pos namedm[NMARKS];
   VisualInfo visualinfo;
   int empty_buffer;          //buffer became empty
   UndoHeader* curhead = curBook->undo.currHead;

   //Don't want autocommands using the undo structures here, they are invalid till the end.
   block_autocmds();

#ifdef U_DEBUG
   u_check(false);
#endif
   old_flags = curhead->uh_flags;
   new_flags = (curBook->wasModified ? UH_CHANGED : 0) +
         ((curBook->mem.flags & ML_EMPTY) ? UH_EMPTYBUF : 0);
   setpcmark();

   //save marks before undo/redo
   MEMMOVE(namedm, curBook->namedMarks, sizeof(Pos) * NMARKS);
   visualinfo = curBook->visual;
   curBook->opStart.lnum = curBook->mem.lineCount;
   curBook->opStart.col = 0;
   curBook->opEnd.lnum = 0;
   curBook->opEnd.col = 0;

   UndoEntry *nuep;
   for (UndoEntry* uep = curhead->uh_entry; uep != NULL; uep = nuep) {
      top = uep->ue_top;
      bot = uep->ue_bot;
      if (bot == 0)
          bot = curBook->mem.lineCount + 1;
      if (top > curBook->mem.lineCount || top >= bot || bot > curBook->mem.lineCount + 1) {
         unblock_autocmds();
         internalErrMsg(e_u_undo_line_numbers_wrong);
         changed();      //don't want UNCHANGED now
         return;
      }

      oldsize = bot - top - 1;    //number of lines before undo
      newsize = uep->ue_size;       //number of lines after undo

      //Decide about the cursor position, depending on what text changed.
      //Don't set it yet, it may be invalid if lines are going to be added.
      if (top < newlnum) {
         //If the saved cursor is somewhere in this undo block, move it to
         //the remembered position.  Makes "gwap" put the cursor back where it was.
         lnum = curhead->uh_cursor.lnum;
         if (lnum >= top && lnum <= top + newsize + 1) {
            new_curpos = curhead->uh_cursor;
            newlnum = new_curpos.lnum - 1;
         } else {
            //Use the first line that actually changed. Avoids that
            //undoing auto-formatting puts the cursor in the previous line.
            for (i = 0; i < newsize && i < oldsize; ++i) {
               CS p = ml_get(top + 1 + i);

               if (curBook->mem.lineLen != uep->ue_array[i].ul_len
                   || memcmp(uep->ue_array[i].ul_line, p, curBook->mem.lineLen) != 0
               )
                  break;
            }
            if (i == newsize && newlnum == MAXLNUM && uep->ue_next == NULL) {
               newlnum = top;
               new_curpos.lnum = newlnum + 1;
            } ei (i < newsize) {
               newlnum = top + i;
               new_curpos.lnum = newlnum + 1;
            }
         }
      }

      empty_buffer = false;

      //Delete the lines between top and bot and save them in newarray.
      if (oldsize > 0) {
         newarray = U_ALLOC_LINE(sizeof(UndoLine) * oldsize);
         //delete backwards, it goes faster in most cases
         for (lnum = bot - 1, i = oldsize; --i >= 0; --lnum) {
            //what can we do when we run out of memory?
            if (u_save_line(&newarray[i], lnum) == FAIL)
               do_outofmem_msg((Ulong)0);
            //remember we deleted the last line in the buffer, and a
            //dummy empty line will be inserted
            if (curBook->mem.lineCount == 1)
               empty_buffer = true;
            ml_delete_flags(lnum, ML_DEL_UNDO);
         }
      }
      else
          newarray = NULL;

      //make sure the cursor is on a valid line after the deletions
      check_cursor_lnum();

      //Insert the lines in u_array between top and bot.
      if (newsize) {
         for (lnum = top, i = 0; i < newsize; ++i, ++lnum) {
            //If the file is empty, there is an empty line 1 that we
            //should get rid of, by replacing it with the new line.
            if (empty_buffer && lnum == 0)
                ml_replace_len((LineNr)1, uep->ue_array[i].ul_line,
                       uep->ue_array[i].ul_len, true, true);
            else
                ml_append_flags(lnum, uep->ue_array[i].ul_line,
                    (ColNr)uep->ue_array[i].ul_len, ML_APPEND_UNDO);
            eeglFree(uep->ue_array[i].ul_line);
         }
         eeglFree((CS)uep->ue_array);
      }

      //adjust marks
      if (oldsize != newsize) {
         markAdjust(top + 1, top + oldsize, (long)MAXLNUM, (long)newsize - (long)oldsize, true);
         if (curBook->opStart.lnum > top + oldsize)
            curBook->opStart.lnum += newsize - oldsize;
         if (curBook->opEnd.lnum > top + oldsize)
            curBook->opEnd.lnum += newsize - oldsize;
      }
      if (oldsize > 0 || newsize > 0) {
         doChangedLines(top + 1, 0, bot, newsize - oldsize);
      }

      //Set the '[ mark.
      if (top + 1 < curBook->opStart.lnum)
         curBook->opStart.lnum = top + 1;
      //Set the '] mark.
      if (newsize == 0 && top + 1 > curBook->opEnd.lnum)
         curBook->opEnd.lnum = top + 1;
      ei (top + newsize > curBook->opEnd.lnum)
         curBook->opEnd.lnum = top + newsize;

      u_newcount += newsize;
      u_oldcount += oldsize;
      uep->ue_size = oldsize;
      uep->ue_array = newarray;
      uep->ue_bot = top + newsize + 1;

      //insert this entry in front of the new entry list
      nuep = uep->ue_next;
      uep->ue_next = newlist;
      newlist = uep;
   }

   //Ensure the '[ and '] marks are within bounds.
   if (curBook->opStart.lnum > curBook->mem.lineCount)
      curBook->opStart.lnum = curBook->mem.lineCount;
   if (curBook->opEnd.lnum > curBook->mem.lineCount)
      curBook->opEnd.lnum = curBook->mem.lineCount;

   //Set the cursor to the desired position.  Check that the line is valid.
   curPor->cursor = new_curpos;
   check_cursor_lnum();

   curhead->uh_entry = newlist;
   curhead->uh_flags = new_flags;
   if ((old_flags & UH_EMPTYBUF) && CURBOOK_EMPTY())
      curBook->mem.flags |= ML_EMPTY;
   if (old_flags & UH_CHANGED)
      changed();
   else
      unchanged(curBook, true);

   //restore marks from before undo/redo
   for (i = 0; i < NMARKS; ++i) {
      if (curhead->uh_namedm[i].lnum != 0)
         curBook->namedMarks[i] = curhead->uh_namedm[i];
      if (namedm[i].lnum != 0)
         curhead->uh_namedm[i] = namedm[i];
      else
         curhead->uh_namedm[i].lnum = 0;
   }
   if (curhead->uh_visual.vi_start.lnum != 0) {
      curBook->visual = curhead->uh_visual;
      curhead->uh_visual = visualinfo;
   }

   //If the cursor is only off by one line, put it at the same position as
   //before starting the change (for the "o" command).
   //Otherwise the cursor should go to the first undone line.
   if (curhead->uh_cursor.lnum + 1 == curPor->cursor.lnum && curPor->cursor.lnum > 1)
      --curPor->cursor.lnum;
   if (curPor->cursor.lnum <= curBook->mem.lineCount) {
      if (curhead->uh_cursor.lnum == curPor->cursor.lnum) {
         curPor->cursor.col = curhead->uh_cursor.col;
         if (virtual_active() && curhead->uh_cursor_vcol >= 0)
            coladvance((ColNr)curhead->uh_cursor_vcol);
         else
            curPor->cursor.coladd = 0;
      } else
         beginline(BL_SOL | BL_FIX);
   } else {
      //We get here with the current cursor line being past the end (eg after adding lines at the 
      //end of the file, and then undoing it). check_cursor() will move the cursor to the last 
      //line. Move it to the first column here.
      curPor->cursor.col = 0;
      curPor->cursor.coladd = 0;
   }

   //Make sure the cursor is on an existing line and column.
   check_cursor();

   //Remember where we are for "g-" and ":earlier 10s".
   curBook->undo.seqCurr = curhead->uh_seq;
   if (undo) {
      //We are below the previous undo.  However, to make ":earlier 1s"
      //work we compute this as being just above the just undone change.
      if (curhead->next.ptr != NULL)
         curBook->undo.seqCurr = curhead->next.ptr->uh_seq;
      else
         curBook->undo.seqCurr = 0;
   }

   //Remember where we are for ":earlier 1f" and ":later 1f".
   if (curhead->uh_save_nr != 0) {
      if (undo)
         curBook->undo.saveNrCurr = curhead->uh_save_nr - 1;
      else
         curBook->undo.saveNrCurr = curhead->uh_save_nr;
   }

   //The timestamp can be the same for multiple changes, just use the one of
   //the undone/redone change.
   curBook->undo.timeCurr = curhead->uh_time;

   unblock_autocmds();
#ifdef U_DEBUG
   u_check(false);
#endif
}

//If we deleted or added lines, report the number of less/more lines. Otherwise, report the number 
//of changes (this may be incorrect in some cases, but it's better than nothing).
private void
u_undo_end(
   Boole did_undo,  //just did an undo
   Boole absolute   //used ":undo N"
){
   CS msgstr;
   UndoHeader   *uhp;
   Byte msgbuf[80];

   if ((p_fdo & FDO_UNDO) && keyWasTypedG)
      foldOpenCursor();

   if (global_busy       //no messages now, wait until global is finished
          || !messaging())  //'lazyredraw' set, don't do messages now
      return;

   if (curBook->mem.flags & ML_EMPTY)
      --u_newcount;

   u_oldcount -= u_newcount;
   if (u_oldcount == -1)
      msgstr = N_("more line");
   ei (u_oldcount < 0)
      msgstr = N_("more lines");
   ei (u_oldcount == 1)
      msgstr = N_("line less");
   ei (u_oldcount > 1)
      msgstr = N_("fewer lines");
   else {
      u_oldcount = u_newcount;
      if (u_newcount == 1)
         msgstr = N_("change");
      else
         msgstr = N_("changes");
   }

   if (curBook->undo.currHead != NULL) {
   //For ":undo N" we prefer a "after #N" message.
   if (absolute && curBook->undo.currHead->next.ptr != NULL) {
      uhp = curBook->undo.currHead->next.ptr;
      did_undo = false;
   } ei (did_undo)
      uhp = curBook->undo.currHead;
   else
      uhp = curBook->undo.currHead->next.ptr;
   } else
      uhp = curBook->undo.newHead;

   if (!uhp)
      *msgbuf = ZERO;
   else
      add_time(msgbuf, sizeof(msgbuf), uhp->uh_time);

   if (VIsual_active)
     check_pos(curBook, &VIsual);

   smsgDecoKeep(
       0, _("%ld %s; %s #%ld  %s"),
       u_oldcount < 0 ? -u_oldcount : u_oldcount,
       _(msgstr),
       did_undo ? _("before") : _("after"),
       uhp == NULL ? 0L : uhp->uh_seq,
       msgbuf
   );
}

//u_sync: stop adding to the current entry list
pub void
u_sync(int force) {  //Also sync when no_u_sync is set.
   //Skip it when already synced or syncing is disabled.
   if (curBook->undo.synced || (!force && no_u_sync > 0))
      return;
   if (p_ul < 0)
      curBook->undo.synced = true;  //no entries, nothing to do
   else {
      u_getbot();          //compute ue_bot of previous u_save
      curBook->undo.currHead = NULL;
   }
}

//":undolist": List the leaves of the undo tree
pub void
c_undolist(Invocation*) {
   ArrayList   ga;
   UndoHeader   *uhp;
   int changes = 1;
   int len;

   //1: walk the tree to find all leafs, put the info in "ga".
   //2: sort the lines
   //3: display the list
   int mark = ++lastmark;
   int nomark = ++lastmark;
   ga_init2(&ga, sizeof(char *), 20);

   uhp = curBook->undo.oldHead;
   while (uhp != NULL) {
      if (uhp->prev.ptr == NULL && uhp->uh_walk != nomark && uhp->uh_walk != mark) {
         if (ga_grow(&ga, 1) == FAIL)
            break;
         len = eeSnprintf(IObuff, IOSIZE, "%6ld %7d  ", uhp->uh_seq, changes);
         add_time(IObuff + len, IOSIZE - len, uhp->uh_time);

         //we have to call STRLEN() here because add_time() does not report
         //the number of characters added.
         len += (int)STRLEN(IObuff + len);
         if (uhp->uh_save_nr > 0) {
            int n = (len >= 33) ? 0 : 33 - len;

            len += eeSnprintf(
                  IObuff + len, IOSIZE - len, "%*.*s  %3ld", n, n, " ", uhp->uh_save_nr
            );
         }
         ((Byte **)(ga.c))[ga.len++] = copySubstr(IObuff, len);
      }

      uhp->uh_walk = mark;

      //go down in the tree if we haven't been there
      if (uhp->prev.ptr != NULL && uhp->prev.ptr->uh_walk != nomark
                   && uhp->prev.ptr->uh_walk != mark)
      {
          uhp = uhp->prev.ptr;
          ++changes;
      }

      //go to alternate branch if we haven't been there
      ei (uhp->altNext.ptr != NULL
         && uhp->altNext.ptr->uh_walk != nomark
         && uhp->altNext.ptr->uh_walk != mark)
          uhp = uhp->altNext.ptr;

      //go up in the tree if we haven't been there and we are at the
      //start of alternate branches
      ei (uhp->next.ptr != NULL && uhp->altPrev.ptr == NULL
         && uhp->next.ptr->uh_walk != nomark
         && uhp->next.ptr->uh_walk != mark)
      {
          uhp = uhp->next.ptr;
          --changes;
      }

      else {
          //need to backtrack; mark this node as done
          uhp->uh_walk = nomark;
          if (uhp->altPrev.ptr != NULL)
         uhp = uhp->altPrev.ptr;
          else
          {
         uhp = uhp->next.ptr;
         --changes;
          }
      }
   }

   if (ga.len == 0)
      msg(_("Nothing to undo"));
   else {
      sortStrings((Byte **)ga.c, ga.len);

      msg_start();
      msgPutsDeco(_("number changes  when               saved"), getDecoFlags(HLF_T));
      for (Unt i = 0; i < (Unt)ga.len && !gotInterruptG; ++i) {
         msg_putchar('\n');
         if (gotInterruptG)
            break;
         msg_puts(((Byte **)ga.c)[i]);
      }
      msg_end();

      ga_clear_strings(&ga);
   }
}

//":undojoin": continue adding to the last entry list
pub void
c_undojoin(Invocation*) {
   if (!curBook->undo.newHead)
      return;          //nothing changed before
   if (curBook->undo.currHead) {
      emsg(_(e_undojoin_is_not_allowed_after_undo));
      return;
   }
   if (!curBook->undo.synced)
      return;          //already unsynced
   if (p_ul < 0)
      return;          //no entries, nothing to do
   //Append next change to the last entry
   curBook->undo.synced = false;
}

//Called after writing or reloading the file and setting wasModified to false.
//Now an undo means that the buffer is modified.
pub void
u_unchanged(Book* book) {
   u_unch_branch(book->undo.oldHead);
   book->didWarnReadonly = false;
}

//After reloading a buffer which was saved for 'undoreload': Find the first
//line that was changed and set the cursor there.
pub void
u_find_first_changed(void) {
   UndoHeader   *uhp = curBook->undo.newHead;
   LineNr   lnum;

   if (curBook->undo.currHead != NULL || uhp == NULL)
      return;  //undid something in an autocmd?

   //Check that the last undo block was for the whole file.
   UndoEntry* uep = uhp->uh_entry;
   if (uep->ue_top != 0 || uep->ue_bot != 0)
      return;

   for (lnum = 1; lnum < curBook->mem.lineCount && lnum <= uep->ue_size; ++lnum) {
      CS p = memGetLine(curBook, lnum, false);

      if (uep->ue_array[lnum - 1].ul_len != curBook->mem.lineLen
         || memcmp(p, uep->ue_array[lnum - 1].ul_line, uep->ue_array[lnum - 1].ul_len) != 0
      ) {
         CLEAR_POS(&(uhp->uh_cursor));
         uhp->uh_cursor.lnum = lnum;
         return;
      }
   }
   if (curBook->mem.lineCount != uep->ue_size) {
      //lines added or deleted at the end, put the cursor there
      CLEAR_POS(&(uhp->uh_cursor));
      uhp->uh_cursor.lnum = lnum;
   }
}

//Increase the write count, store it in the last undo header, what would be used for "u".
pub void
u_update_save_nr(Book* book) {
   ++book->undo.saveNrLast;
   book->undo.saveNrCurr = book->undo.saveNrLast;
   UndoHeader* uhp = book->undo.currHead;
   uhp = uhp ? uhp->next.ptr : book->undo.newHead;
   if (uhp)
      uhp->uh_save_nr = book->undo.saveNrLast;
}

private void
u_unch_branch(UndoHeader* uhp) {
   for (UndoHeader* uh = uhp; uh != NULL; uh = uh->prev.ptr) {
      uh->uh_flags |= UH_CHANGED;
      if (uh->altNext.ptr != NULL)
          u_unch_branch(uh->altNext.ptr);       //recursive
   }
}

//Get pointer to last added entry. If it's not valid, give an error message and return NULL.
private UndoEntry *
u_get_headentry(void) {
   if (curBook->undo.newHead == NULL || curBook->undo.newHead->uh_entry == NULL) {
      internalErrMsg(e_undo_list_corrupt);
      return NULL;
   }
   return curBook->undo.newHead->uh_entry;
}

//u_getbot(): compute the line number of the previous u_save It is called only when synced is false
private void
u_getbot(void) {
   UndoEntry* uep = u_get_headentry();   //check for corrupt undo list
   if (!uep)
      return;

   uep = curBook->undo.newHead->uh_getbot_entry;
   if (uep) {
      //the new ue_bot is computed from the number of lines that has been
      //inserted (0 - deleted) since calling u_save. This is equal to the
      //old line count subtracted from the current line count.
      LineNr extra = curBook->mem.lineCount - uep->ue_lcount;
      uep->ue_bot = uep->ue_top + uep->ue_size + 1 + extra;
      if (uep->ue_bot < 1 || uep->ue_bot > curBook->mem.lineCount) {
          internalErrMsg(e_undo_line_missing);
          uep->ue_bot = uep->ue_top + 1;  //assume all lines deleted, will
                      //get all the old lines back without deleting the current ones
      }

      curBook->undo.newHead->uh_getbot_entry = NULL;
   }

   curBook->undo.synced = true;
}

//Free one header "uhp" and its entry list and adjust the pointers.
private void
u_freeheader(
   Book* book,
   UndoHeader* uhp,
   UndoHeader** uhpp)   //if not NULL reset when freeing this header
{
   UndoHeader* uhap;

   //When there is an alternate redo list free that branch completely,
   //because we can never go there.
   if (uhp->altNext.ptr != NULL)
      freeBranch(book, uhp->altNext.ptr, uhpp);

   if (uhp->altPrev.ptr != NULL)
      uhp->altPrev.ptr->altNext.ptr = NULL;

   //Update the links in the list to remove the header.
   if (uhp->next.ptr == NULL)
      book->undo.oldHead = uhp->prev.ptr;
   else
      uhp->next.ptr->prev.ptr = uhp->prev.ptr;

   if (uhp->prev.ptr == NULL)
      book->undo.newHead = uhp->next.ptr;
   else {
      for (uhap = uhp->prev.ptr; uhap != NULL; uhap = uhap->altNext.ptr)
         uhap->next.ptr = uhp->next.ptr;
   }

   u_freeentries(book, uhp, uhpp);
}

//Free an alternate branch and any following alternate branches.
private void
freeBranch(
   Book* book,
   UndoHeader* uhp,
   UndoHeader** uhpp   //if not NULL reset when freeing this header
){
   //If this is the top branch we may need to use u_freeheader() to update all the pointers.
   if (uhp == book->undo.oldHead) {
      while (book->undo.oldHead)
         u_freeheader(book, book->undo.oldHead, uhpp);
      return;
   }

   if (uhp->altPrev.ptr)
      uhp->altPrev.ptr->altNext.ptr = NULL;

   UndoHeader* next = uhp;
   UndoHeader* tofree;
   while (next) {
      tofree = next;
      if (tofree->altNext.ptr)
         freeBranch(book, tofree->altNext.ptr, uhpp);   //recursive
      next = tofree->prev.ptr;
      u_freeentries(book, tofree, uhpp);
   }
}

//Free all the undo entries for one header and the header itself.
//This means that "uhp" is invalid when returning.
private void
u_freeentries(
   Book       *book,
   UndoHeader       *uhp,
   UndoHeader       **uhpp)   //if not NULL reset when freeing this header
{
   UndoEntry       *uep, *nuep;

   //Check for pointers to the header that become invalid now.
   if (book->undo.currHead == uhp)
      book->undo.currHead = NULL;
   if (book->undo.newHead == uhp)
      book->undo.newHead = NULL;  //freeing the newest entry
   if (uhpp != NULL && uhp == *uhpp)
      *uhpp = NULL;

   for (uep = uhp->uh_entry; uep != NULL; uep = nuep) {
      nuep = uep->ue_next;
      freeEntry(uep, uep->ue_size);
   }

#ifdef U_DEBUG
   uhp->uh_magic = 0;
#endif
   eeglFree((CS)uhp);
   --book->undo.countHeaders;
}

//free entry 'uep' and 'n' lines in uep->ue_array[]
private void
freeEntry(UndoEntry *uep, long n) {
   while (n > 0)
      eeglFree(uep->ue_array[--n].ul_line);
   eeglFree((CS)uep->ue_array);
#ifdef U_DEBUG
   uep->ue_magic = 0;
#endif
   eeglFree((CS)uep);
}

//invalidate the undo buffer; called when storage has already been released
private void
invalidateUndoBuffer(Book *book) {
   book->undo.newHead = book->undo.oldHead = book->undo.currHead = NULL;
   book->undo.synced = true;
   book->undo.countHeaders = 0;
   book->undo.line.ul_line = NULL;
   book->undo.line.ul_len = 0;
   book->undo.line.ul_textlen = 0;
   book->undo.lineLnum = 0;
}

//Free all allocated memory blocks for the 'book'.
private void
u_blockfree(Book* book) {
   while (book->undo.oldHead)
      u_freeheader(book, book->undo.oldHead, NULL);
   eeglFree(book->undo.line.ul_line);
}

//Free all allocated memory blocks for the 'book'. and invalidate the undo buffer
pub void
invalidateUndoBufferAndFreeBlocks(Book* book) {
   u_blockfree(book);
   invalidateUndoBuffer(book);
}

//Save the line "lnum" for the "U" command.
private void
u_saveline(LineNr lnum) {
   if (lnum == curBook->undo.lineLnum)       //line is already saved
      return;
   if (lnum < 1 || lnum > curBook->mem.lineCount) //should never happen
      return;
   u_clearline();
   curBook->undo.lineLnum = lnum;
   if (curPor->cursor.lnum == lnum)
      curBook->undo.lineCol = curPor->cursor.col;
   else
      curBook->undo.lineCol = 0;
   if (u_save_line(&curBook->undo.line, lnum) == FAIL)
      do_outofmem_msg((Ulong)0);
}

//clear the line saved for the "U" command
//(this is used externally for crossing a line while in insert mode)
pub void
u_clearline(void) {
   if (curBook->undo.line.ul_line == NULL)
      return;

   EE_CLEAR(curBook->undo.line.ul_line);
   curBook->undo.line.ul_len = 0;
   curBook->undo.line.ul_textlen = 0;
   curBook->undo.lineLnum = 0;
}

//Implementation of the "U" command. We allow the cursor to be in another line.
//Careful: may trigger autocommands that reload the book.
pub void
u_undoline(void) {
   if (undo_off)
      return;

   if (curBook->undo.line.ul_line == NULL || curBook->undo.lineLnum > curBook->mem.lineCount) {
      inpFlushIfNotSilent();
      return;
   }

   //first save the line for the 'u' command
   if (u_savecommon(curBook->undo.lineLnum - 1,
             curBook->undo.lineLnum + 1, (LineNr)0, false) == FAIL)
      return;
      
   UndoLine  oldp;
   if (u_save_line(&oldp, curBook->undo.lineLnum) == FAIL) {
      do_outofmem_msg((Ulong)0);
      return;
   }
   ml_replace_len(curBook->undo.lineLnum, curBook->undo.line.ul_line,
                 curBook->undo.line.ul_len, true, false);
   changed_bytes(curBook->undo.lineLnum, 0);
   curBook->undo.line = oldp;

   ColNr t = curBook->undo.lineCol;
   if (curPor->cursor.lnum == curBook->undo.lineLnum)
      curBook->undo.lineCol = curPor->cursor.col;
   curPor->cursor.col = t;
   curPor->cursor.lnum = curBook->undo.lineLnum;
   check_cursor_col();
}

//For undotree(): Append the list of undo blocks at "first_uhp" to "list". Recursive.
private void
evalTree(Book* book, UndoHeader* first_uhp, List* list) {
   UndoHeader  *uhp = first_uhp;
   Bag   *dict;

   while (uhp) {
      dict = allocBag();
      if (!dict)
         return;
      bagAddNumber(dict, S"seq", uhp->uh_seq);
      bagAddNumber(dict, S"time", (long)uhp->uh_time);
      if (uhp == book->undo.newHead)
         bagAddNumber(dict, S"newhead", 1);
      if (uhp == book->undo.currHead)
         bagAddNumber(dict, S"curhead", 1);
      if (uhp->uh_save_nr > 0)
         bagAddNumber(dict, S"save", uhp->uh_save_nr);

      if (uhp->altNext.ptr != NULL) {
         List* alt_list = list_alloc();
         //Recursive call to add alternate undo tree.
         evalTree(book, uhp->altNext.ptr, alt_list);
         bagAddList(dict, S"alt", alt_list);
      }

      listAppendBag(list, dict);
      uhp = uhp->prev.ptr;
   } 
}

//"undofile(name)" function
pub void
f_undofile(Var* argvars, Var* returnVar) {
   returnVar->tag = VAR_STRING;
   CS fname = tv_get_string(&argvars[0]);

   if (*fname == ZERO) {
      //If there is no file name there will be no undo file.
      returnVar->string = NULL;
   } else {
      CS ffname = fiExpandAndCopy(fname, true);

      if (ffname)
         returnVar->string = fiBuildSwapOrUndoFname(ffname, false);
      eeglFree(ffname);
   }
}

//Reset undofile option and delete the undofile
pub void
u_undofile_reset_and_delete(Book* book) {
   if (!book->o.undoFile)
      return;

   CS file_name = fiBuildSwapOrUndoFname(book->fullFileName, true);
   if (file_name) {
      mch_remove(file_name);
      eeglFree(file_name);
   }

   optChangeAndReportError(
      S"undofile", (OptionValue){.tag = OPTION_BOOLE, .boole = 0L}, SET_LOCAL
   );
}

//"undotree(expr)" function
pub void
f_undotree(Var* argvars, Var* returnVar) {
   allocReturnDict(returnVar);

   Var* tv = &argvars[0];
   Book* book = tv->tag == VAR_UNKNOWN ? curBook : evGetBookArg(tv);
   if (!book)
      return;

   Bag *bag = returnVar->bag;

   bagAddNumber(bag, S"synced", (long)book->undo.synced);
   bagAddNumber(bag, S"seq_last", book->undo.seqLast);
   bagAddNumber(bag, S"save_last", book->undo.saveNrLast);
   bagAddNumber(bag, S"seq_cur", book->undo.seqCurr);
   bagAddNumber(bag, S"time_cur", (long)book->undo.timeCurr);
   bagAddNumber(bag, S"save_cur", book->undo.saveNrCurr);

   List *list = list_alloc();
   evalTree(book, book->undo.oldHead, list);
   bagAddList(bag, S"entries", list);
}

//}}}
//{{{changes to text

//If the file is readonly, give a warning message with the first change. Don't do this for 
//autocommands. Don't use emsg() because it flushes the macro buffer.
//If we have undone all changes, "wasModified" will be false, but "didWarnReadonly" will be true.
//"col" is the column for the message; non-zero when in insert mode and 'showmode' is on.
//Careful: may trigger autocommands that reload the book.
pub void
change_warning(int col) {
   static CS w_readonly = S"W10: Warning: Changing a readonly file";

   if (curBook->didWarnReadonly
          || bookWasChanged(curBook)
          || autocmd_busy
          || curBook->o.modifiable)
      return;

   ++curBookLock;
   applyAutocomms(EVENT_FILECHANGEDRO, NULL, NULL, false, curBook);
   --curBookLock;
   if (curBook->o.modifiable)
      return;

   //Do what msg() does, but with a column offset if the warning should
   //be after the mode message.
   msg_start();
   if (msgRowG == visibleRowsG - 1)
      msgColG = col;
   msg_source(getDecoFlags(HLF_W));
   msgPutsDeco(_(w_readonly), getDecoFlags(HLF_W) | MSG_HIST);
   msg_clr_eos();
   (void)msg_end();
   if (msg_silent == 0 && !silentModeG
    && time_for_testing != 1
   ) {
       out_flush();
       ui_delay(1002L, true); //give the user time to think about it
   }
   curBook->didWarnReadonly = true;
   redrawCommlineG = false;   //don't redraw and erase the message
   if (msgRowG < visibleRowsG - 1)
      showmode();
}

//Call this function when something in the current book is changed.
//
//Most often called through changed_bytes() and doChangedLines(), which also
//mark the area of the display to be redrawn.
//
//Careful: may trigger autocommands that reload the book.
pub void
changed(void) {
   if (!curBook->wasModified) {
      int   save_msg_scroll = msg_scroll;

      //Give a warning about changing a read-only file. This may also
      //check-out the file, thus change "curBook"!
      change_warning(0);

      //Create a swap file if that is wanted.
      //Don't do this for "nofile" and "nowrite" book types.
      if (curBook->maySwap && curBook->currFileName && !bookDontWrite(curBook)) {
         int save_need_wait_return = need_wait_return;

         need_wait_return = false;
         memOpenSwapFile(curBook);

         //The memOpenSwapFile() can cause an ATTENTION message.
         //Wait two seconds, to make sure the user reads this unexpected
         //message.  Since we could be anywhere, call wait_return() now,
         //and don't let the emsg() set msg_scroll.
         if (need_wait_return && emsg_silent == 0 && !in_assert_fails) {
            out_flush();
            ui_delay(2002L, true);
            wait_return(true);
            msg_scroll = save_msg_scroll;
         } else
            need_wait_return = save_need_wait_return;
      }
      doOnChangeToText();
   }
   ++CHANGEDTICK(curBook);

   //If a pattern is highlighted, the position may now be invalid.
   highlight_match = false;
}

//check_status: called when the status bars for the book 'book'
//      need to be updated
private void
check_status(Book* book) {
   Portal   *po;
   FOR_ALL_PORTALS(po) {
      if (po->book == book) {
         po->statusLineNeedsRedraw = true;
         drawSetMustRedraw(UPD_VALID);
      }
   } 
}

//Internal part of changed(), no user interaction. Also used for recovery.
pub void
doOnChangeToText(void) {
   curBook->wasModified = true;
   ml_setflags(curBook);
   check_status(curBook);
   needRedrawTabpanelG = true;
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
          //the current change is going to make the line number in
          //the older change invalid, flush now
          doInvokeListenersOnChangedText(curBook);
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

   //If the new change is going to change the line numbers in already listed
   //changes, then flush.
   checkRecordedChanges(curBook, lnum, lnume, xtra);

   if (curBook->recordedChanges == NULL) {
      curBook->recordedChanges = list_alloc();
      ++curBook->recordedChanges->refCount;
      curBook->recordedChanges->lock = VAR_FIXED;
   }

   dict = allocBag();
   bagAddNumber(dict, S"lnum", (Long)lnum);
   bagAddNumber(dict, S"end", (Long)lnume);
   bagAddNumber(dict, S"added", (Long)xtra);
   bagAddNumber(dict, S"col", (Long)col + 1);

   listAppendBag(curBook->recordedChanges, dict);
}

//Return something that fits into an int.
pub int
trim_to_int(Long x) {
   return x > INT_MAX ? INT_MAX : x < INT_MIN ? INT_MIN : x;
}


//listener_add() function
pub void
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

pub void
f_listener_flush(Arr(Var) argVars, OUT Var*) {
   Book* book = curBook;

   if (argVars[0].tag != VAR_UNKNOWN) {
      book = evGetBookArg(&argVars[0]);
      if (book == NULL)
         return;
   }
   doInvokeListenersOnChangedText(book);
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

pub void
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
               //in doInvokeListenersOnChangedText(), clear ID and delete later
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
pub void
may_doInvokeListenersOnChangedText(Book* book, LineNr lnum, LineNr lnume, int added) {
   checkRecordedChanges(book, lnum, lnume, added);
}

//Called when a sequence of changes is done: invoke listeners added with listener_add().
pub void
doInvokeListenersOnChangedText(Book* book) {
   Listener   *lnr;
   Var   returnVar;
   Var   argv[6];
   ListItem   *li;
   LineNr   start = MAXLNUM;
   LineNr   end = 0;
   LineNr   added = 0;
   int      save_updating_screen = updating_screen;
   static int   recursive = false;
   Listener   *next;
   Listener   *prev;

   if (book->recordedChanges == NULL  //nothing changed
          || book->listener == NULL   //no listeners
          || recursive)       //already busy
      return;
   recursive = true;

   //Block messages on channels from being handled, so that they don't make text changes here.
   ++updating_screen;

   argv[0].tag = VAR_NUMBER;
   argv[0].number = book->fiNum; //a:bufnr

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

   //If f_listener_remove() was called may have to remove a listener now.
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
      updating_screen = true;
   else
      after_updating_screen(true);
   recursive = false;
}

//Remove all listeners associated with "book".
pub void
remove_listeners(Book* book) {
   Listener* next;
   for (Listener* lnr = book->listener; lnr != NULL; lnr = next) {
      next = lnr->next;
      evFreeCallback(&lnr->callback);
      eeglFree(lnr);
   }
   book->listener = NULL;
}

//Common code for when a change was made. See doChangedLines() for the arguments.
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

   //mark the book as modified
   changed();

   may_record_change(lnum, col, lnume, xtra);
   if (curPor->o.diff && diff_internal()) {
      curtab->diff_update = true;
      diff_update_line(lnum);
   }

   //set the '. mark
   if ((commModifierG.cmod_flags & CMOD_KEEPJUMPS) == 0) {
      curBook->lastChange.lnum = lnum;
      curBook->lastChange.col = col;

      //Create a new entry if a new undo-able change was started or we
      //don't have an entry yet.
      if (curBook->newChange || curBook->changeListLen == 0) {
         if (curBook->changeListLen == 0)
            add = true;
         else {
            //Don't create a new entry when the line number is the same
            //as the last one and the column is not too far away.  Avoids
            //creating many entries for typing "xxxxx".
            p = &curBook->changeList[curBook->changeListLen - 1];
            if (p->lnum != lnum)
               add = true;
            else {
               cols = comp_textwidth(false);
               if (cols == 0)
                  cols = 79;
               add = (p->col + cols < col || col + cols < p->col);
            }
         }
         if (add) {
            //This is the first of a new sequence of undo-able changes and it's at some distance 
            //of the last change. Use a new position in the changelist.
            curBook->newChange = false;

            if (curBook->changeListLen == JUMPLISTSIZE) {
               //changelist is full: remove oldest entry
               curBook->changeListLen = JUMPLISTSIZE - 1;
               MEMMOVE(curBook->changeList, curBook->changeList + 1,
                       sizeof(Pos) * (JUMPLISTSIZE - 1));
               FOR_ALL_TAB_PORTALS(tp, po) {
                  //Correct position in changelist for other portals into this book.
                  if (po->book == curBook && po->changeListInd > 0)
                      --po->changeListInd;
               }
            }
            FOR_ALL_TAB_PORTALS(tp, po) {
                //For other portals, if the position in the changelist is
                //at the end it stays at the end.
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
         LineNr last = lnume + xtra - 1;  //last line after the change

         //Mark this portal to be redrawn later.
         if (!redraw_not_allowed && po->redrawType < UPD_VALID)
            po->redrawType = UPD_VALID;

         //When inserting/deleting lines and the portal has specific lines
         //to be redrawn, redrawTop and redrawBott may now be invalid,
         //so just redraw everything.
         if (xtra != 0 && po->redrawTop != 0)
            redrawPortLater(po, UPD_NOT_VALID);

         //Reset "skipCol" if the topline length has become smaller to
         //such a degree that nothing will be visible anymore, accounting
         //for 'smoothscroll' <<< or 'listchars' "precedes" marker.
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
         i = getFoldsPortal(po, lnum, OUT &lnum, NULL, false, NULL);
         if (po->cursor.lnum == lnum) {
            po->isCursorLineFolded = i;
         }
         i = getFoldsPortal(po, last, NULL, OUT &last, false, NULL);
         if (po->cursor.lnum == last) {
            po->isCursorLineFolded = i;
         }

         //If the changed line is in a range of previously folded lines,
         //compare with the first line in that range.
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
                //Assume that botline doesn't change (inserted lines make
                //other lines scroll down below botline).
                approximate_botline_win(po);
            }
         }

         //Check if any lines[] entries have become invalid.
         //For entries below the change: Correct the lnums for
         //inserted/deleted lines.  Makes it possible to stop displaying
         //after the change.
         for (i = 0; i < po->validLines; ++i) {
            if (po->lines[i].isValid) {
                if (po->lines[i].bookLnum >= lnum) {
               //Do not change bookLnum at index zero, it is used to
               //compare with topLine.  Invalidate it instead.
               if (po->lines[i].bookLnum < lnume || i == 0) {
                   //line included in change
                   po->lines[i].isValid = false;
               } ei (xtra != 0) {
                   //line below change
                   po->lines[i].bookLnum += xtra;
                   po->lines[i].lastBookLnum += xtra;
               }
                } ei (po->lines[i].lastBookLnum >= lnum) {
               //change somewhere inside this range of folded lines,
               //may need to be redrawn
               po->lines[i].isValid = false;
               }
            }
         }
         //Take care of side effects for setting topLine when folds have
         //changed.  Esp. when the book was changed in another portal.
         if (hasAnyFolding(po)) {
            set_topline(po, po->topLine);
         }
         //If lines have been added or removed, relative numbering always
         //requires an update even if cursor didn't move.
         if (po->o.relativeNumber && xtra != 0) {
            po->lastCursorLnumRnu = 0;
         }

         if (po->o.cursorLine && po->lastCursorLine >= lnum) {
            if (po->lastCursorLine < lnume)
               //If 'cursorline' was inside the change, it has already
               //been invalidated in lines[] by the loop above.
               po->lastCursorLine = 0;
            else
               //If 'cursorline' was below the change, adjust its lnum.
               po->lastCursorLine += xtra;
         }
      }
      if (po == curPor && xtra != 0 && searchLastLnumG >= lnum)
         searchLastLnumG += xtra;
   }

   //Call drawUpdateScreen() later, which checks out what needs to be redrawn,
   //since it notices needsRedraw and then uses b_mod_*.
   drawSetMustRedraw(UPD_VALID);

   //when the cursor line is changed always trigger CursorMoved
   if (lnum <= curPor->cursor.lnum && lnume + (xtra < 0 ? -xtra : xtra) > curPor->cursor.lnum)
      last_cursormoved.lnum = 0;
}

private void
changedOneline(Book* book, LineNr lnum) {
   if (book->needsRedraw) {
      //find the maximum area that must be redisplayed
      if (lnum < book->needsRedrawTop)
          book->needsRedrawTop = lnum;
      ei (lnum >= book->needsRedrawBott)
          book->needsRedrawBott = lnum + 1;
   } else {
      //set the area that must be redisplayed to one line
      book->needsRedraw = true;
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
pub void
changed_bytes(LineNr lnum, ColNr col) {
   changedOneline(curBook, lnum);
   changed_common(lnum, col, lnum + 1, 0L);

   //Diff hiliting in other diff portals may need to be updated too.
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
pub void
inserted_bytes(LineNr lnum, ColNr col, int added) {
   if (curBook->hasTextprop && added != 0)
      adjustPropColumns(lnum, col, added, 0);

   changed_bytes(lnum, col);
}

//Appended "count" lines below line "lnum" in the current book.
//Must be called AFTER the change and after markAdjust().
//Takes care of marking the book to be redrawn and sets the changed flag.
pub void
appended_lines(LineNr lnum, long count) {
   doChangedLines(lnum + 1, 0, lnum + 1, count);
}

//Like appended_lines(), but adjust marks first.
pub void
appended_lines_mark(LineNr lnum, long count) {
   markAdjust(lnum + 1, (LineNr)MAXLNUM, count, 0L, true);
   doChangedLines(lnum + 1, 0, lnum + 1, count);
}

//Deleted "count" lines at line "lnum" in the current book.
//Must be called AFTER the change and after markAdjust().
//Take care of marking the book to be redrawn and sets the changed flag.
pub void
deleted_lines(LineNr lnum, long count) {
   doChangedLines(lnum, 0, lnum + count, -count);
}

//Like deleted_lines(), but adjust marks first.
//Make sure the cursor is on a valid line before calling, a GUI callback may
//be triggered to display the cursor.
pub void
deleted_lines_mark(LineNr lnum, long count) {
   markAdjust(lnum, (LineNr)(lnum + count - 1), (long)MAXLNUM, -count, true);
   doChangedLines(lnum, 0, lnum + count, -count);
}

//Mark the area to be redrawn after a change.
//Consider also calling normInvalidateDisplayOfChangedBookLine().
pub void
doChangedLinesBook(
   Book* book,
   LineNr lnum,       //first line with change
   LineNr lnume,       //line below last changed line
   long xtra       //number of extra lines (negative when deleting)
){
   if (book->needsRedraw) {
      //find the maximum area that must be redisplayed
      if (lnum < book->needsRedrawTop)
         book->needsRedrawTop = lnum;
      if (lnum < book->needsRedrawBott) {
         //adjust old bot position for xtra lines
         book->needsRedrawBott += xtra;
         if (book->needsRedrawBott < lnum)
            book->needsRedrawBott = lnum;
      }
      if (lnume + xtra > book->needsRedrawBott)
         book->needsRedrawBott = lnume + xtra;
      book->lineCountDiff += xtra;
   } else {
      //set the area that must be redisplayed
      book->needsRedraw = true;
      book->needsRedrawTop = lnum;
      book->needsRedrawBott = lnume + xtra;
      book->lineCountDiff = xtra;
   }
}

//Changed lines for the current book.
//Must be called AFTER the change and after markAdjust().
//- mark the book changed by calling changed()
//- mark the portals on this book to be redisplayed
//- invalidate cached values
//"lnum" is the first line that needs displaying, "lnume" the first line below the changed lines 
//(BEFORE the change). When only inserting lines, "lnum" and "lnume" are equal.
//Takes care of calling changed() and updating b_mod_*.
//Careful: may trigger autocommands that reload the book.
pub void
doChangedLines(
   LineNr lnum,    //first line with change
   ColNr col,      //column in first line with change
   LineNr lnume,   //line below last changed line
   long xtra       //number of extra lines (negative when deleting)
){
   doChangedLinesBook(curBook, lnum, lnume, xtra);

   if (xtra == 0 && curPor->o.diff && !diff_internal()) {
      //When the number of lines doesn't change then markAdjust() isn't
      //called and other diff books still need to be marked for displaying.
      Portal       *po;
      FOR_ALL_PORTALS(po) {
         if (po->o.diff && po != curPor) {
            redrawPortLater(po, UPD_VALID);
            LineNr poLnum = diff_lnum_win(lnum, po);
            if (poLnum > 0)
               doChangedLinesBook(po->book, poLnum, lnume - lnum + poLnum, 0L);
         }
      } 
   }

   changed_common(lnum, col, lnume, xtra);
}

//Called when the changed flag must be reset for book "book". When "always_inc_changedtick" is 
//true b:changedtick is incremented also when the changed flag was off.
pub void
unchanged(Book* book, int always_inc_changedtick) {
   if (book->wasModified) {
      book->wasModified = false;
      ml_setflags(book);
      check_status(book);
      needRedrawTabpanelG = true;
      ++CHANGEDTICK(book);
   } ei (always_inc_changedtick)
      ++CHANGEDTICK(book);
}

//Insert string "p" at the cursor position.  Stops at a ZERO.
//Handles Replace mode and multi-byte characters.
pub void
ins_bytes(CS p) {
   ins_bytes_len(p, (int)STRLEN(p));
}

//Insert string "p" with length "len" at the cursor position.
//Handle Replace mode and multi-byte characters.
pub void
ins_bytes_len(CS p, int len) {
   int n;
   for (int i = 0; i < len; i += n) {
      //avoid reading past p[len]
      n = utfCharLen_len(p + i, len - i);
      opInsertCharBytes(p + i, n, false);
   }
}

private void
insertOrReplaceChar(Unt c, Boole replace) {
   Byte buf[MB_MAXBYTES + 1];
   int charLen = mb_char2bytes(c, buf);
   //When "c" is 0x100, 0x200, etc. we don't want to insert a ZERO. Happens for CTRL-Vu9900.
   if (buf[0] == 0)
      buf[0] = '\n';

   opInsertCharBytes(buf, charLen, replace);
}

//Insert a single character at the cursor position.
//Caller must have prepared for undo.
//For multi-byte characters we get the whole character, the caller must convert bytes to character
pub void
insertChar(Unt c) {
   insertOrReplaceChar(c, false);
}

//Replace a single character at the cursor position. Caller must have prepared for undo.
//For multi-byte characters we get the whole character, the caller must convert bytes to character
pub void
replaceChar(Unt c) {
   insertOrReplaceChar(c, true);
}

pub void
opInsertCharBytes(CS targetLine, int charlen, Boole replace) {
   LineNr   lnum = curPor->cursor.lnum;

   //Break tabs if needed.
   if (virtual_active() && curPor->cursor.coladd > 0)
      coladvance_force(getviscol());

   ColNr col = curPor->cursor.col;
   CS oldp = ml_get(lnum);
   int oldLineLen = (int)ml_get_len(lnum) + 1;//length of old line including ZERO

   //The lengths default to the values for when not replacing.
   int oldCharLen = replace ? utfCharLen(oldp + col) : 0; //nr of bytes deleted (0 when not replacing)
   int newCharLen = charlen; //nr of bytes inserted

   CS newp = alloc(oldLineLen + newCharLen - oldCharLen);

   //Copy bytes before the cursor.
   if (col > 0)
      MEMMOVE(newp, oldp, (Unt)col);

   //Copy bytes after the changed character(s).
   CS p = newp + col;
   if (oldLineLen > col + oldCharLen)
      MEMMOVE(p + newCharLen, oldp + col + oldCharLen, (Unt)(oldLineLen - col - oldCharLen));

   //Insert or overwrite the new character.
   MEMMOVE(p, targetLine, charlen);
   int i = charlen;

   //Fill with spaces when necessary.
   while (i < newCharLen) {
      p[i++] = ' ';
   }

   //Replace the line in the book.
   ml_replace(lnum, newp, false);

   //mark the book as changed and prepare for displaying
   changed_bytes(lnum, col);
   if (curBook->hasTextprop && newCharLen != oldCharLen) {
      adjustPropColumns(lnum, col, newCharLen - oldCharLen, 0);
   }

   //Normal insert: move cursor right
   curPor->cursor.col += charlen;

   //TODO: should try to update w_row here, to avoid recomputing it later.
}

//Insert a string at the cursor position. Note: Does NOT handle Replace mode.
//Caller must have prepared for undo.
pub void
ins_str(CS s, Unt slen) {
   LineNr lnum = curPor->cursor.lnum;

   if (virtual_active() && curPor->cursor.coladd > 0)
      coladvance_force(getviscol());

   ColNr col = curPor->cursor.col;
   CS oldp = ml_get(lnum);
   int oldlen = (int)ml_get_len(lnum);

   CS newp = alloc(oldlen + slen + 1);
   if (col > 0)
      MEMMOVE(newp, oldp, (Unt)col);
   MEMMOVE(newp + col, s, slen);
   MEMMOVE(newp + col + slen, oldp + col, (Unt)(oldlen - col + 1));
   ml_replace(lnum, newp, false);
   inserted_bytes(lnum, col, (int)slen);
   curPor->cursor.col += (ColNr)slen;
}

//Delete one character under the cursor.
//If "fixpos" is true, don't leave the cursor on the ZERO after the line.
//Caller must have prepared for undo.
//
//return FAIL for failure, OK otherwise
pub int
del_char(Boole fixpos) {
   //Make sure the cursor is at the start of a character.
   mb_adjust_cursor();
   if (*ml_get_cursor() == ZERO)
      return FAIL;
   return del_chars(1L, fixpos);
}

//Like del_bytes(), but delete characters instead of bytes.
pub int
del_chars(long count, Boole fixpos) {
   long   bytes = 0;
   CS p = ml_get_cursor();
   for (int i = 0; i < count && *p != ZERO; ++i)     {
      int l = utfCharLen(p);
      bytes += l;
      p += l;
   }
   return del_bytes(bytes, fixpos, true);
}

//Delete "count" bytes under the cursor.
//If "fixpos" is true, don't leave the cursor on the ZERO after the line.
//Caller must have prepared for undo.
//
//Return FAIL for failure, OK otherwise.
pub int
del_bytes(Long   count, Boole fixpos_arg, int      use_delcombine) { //'delcombine' option applies
   LineNr lnum = curPor->cursor.lnum;
   ColNr col = curPor->cursor.col;
   int fixpos = fixpos_arg;

   CS oldp = ml_get(lnum);
   ColNr oldlen = (int)ml_get_len(lnum);

   //Can't do anything when the cursor is on the ZERO after the line.
   if (col >= oldlen)
      return FAIL;

   //If "count" is zero there is nothing to do.
   if (count == 0)
      return OK;

   //If "count" is negative the caller must be doing something wrong.
   if (count < 1) {
      internalErrFmtMsg(e_invalid_count_for_del_bytes_nr, count);
      return FAIL;
   }

   //If @delcombine is set and deleting (less than) one character, only
   //delete the last combining character.
   if (p_delcomb && use_delcombine && utfCharLen(oldp + col) >= count) {
      int   cc[MAX_COMBINED_SYMBOLS];
      int   n;

      (void)utfc_ptr2char(oldp + col, cc);
      if (cc[0] != ZERO) {
         //Find the last composing char, there can be several.
         n = col;
         do {
            col = n;
            count = utf_ptr2len(oldp + n);
            n += count;
         } while (UTF_COMPOSINGLIKE(oldp + col, oldp + n));
         fixpos = 0;
      }
   }

   //When count is too big, reduce it.
   Long movelen = (long)oldlen - (long)col - count + 1; //includes trailing ZERO
   if (movelen <= 1) {
      //If we just took off the last character of a non-blank line, and fixpos is true, we don't 
      //want to end up positioned at the ZERO, unless "restart_edit" is set
      if (col > 0 && fixpos && restart_edit == 0) {
         --curPor->cursor.col;
         curPor->cursor.coladd = 0;
         curPor->cursor.col -= (*mb_head_off)(oldp, oldp + curPor->cursor.col);
      }
      count = oldlen - col;
      movelen = 1;
   }
   ColNr newlen = oldlen - count;

   //If the old line has been allocated the deletion can be done in the
   //existing line. Otherwise a new line has to be allocated
   int alloc_newp = !ml_line_alloced();    //check if oldp was allocated
   CS newp;
   if (!alloc_newp)
      newp = oldp;             //use same allocated memory
   else {                   //need to allocate a new line
      newp = alloc(newlen + 1);
      MEMMOVE(newp, oldp, (Unt)col);
   }
   MEMMOVE(newp + col, oldp + col + count, (Unt)movelen);
   if (alloc_newp)
      ml_replace(lnum, newp, false);
   else {
      //Also move any following text properties.
      if (oldlen + 1 < curBook->mem.lineLen)
         MEMMOVE(newp + newlen + 1, oldp + oldlen + 1, (Unt)curBook->mem.lineLen - oldlen - 1);
      curBook->mem.lineLen -= count;
      curBook->mem.lineTextLen = 0;
   }

   //mark the book as changed and prepare for displaying
   inserted_bytes(lnum, col, -count);

   return OK;
}

//insertLine - simply insert a line below or above the current line. Applies autoindent
//Return OK for success, FAIL for failure
pub int
insertLine(Unt      dir) { //FORWARD or BACKWARD
   Pos oldCursor = curPor->cursor;
   //count white space on current line
   int newIndent = get_indent_lnum(curPor->cursor.lnum);
   if (dir == BACKWARD)
      --curPor->cursor.lnum;
   if (ml_append(curPor->cursor.lnum, NULL, (ColNr)0, false) == FAIL) {
      return FAIL;
   }
   ++curPor->cursor.lnum;
    
   (void)set_indent(newIndent, SIN_INSERT);
    
   //Postpone calling doChangedLines(), because it would mess up folding with markers.
   markAdjust(curPor->cursor.lnum + 1, (LineNr)MAXLNUM, 1L, 0L, true);
    
   doChangedLines(curPor->cursor.lnum, curPor->cursor.col, curPor->cursor.lnum + 1, 1L);
   curPor->cursor.lnum = oldCursor.lnum + 1;
   return OK;
}

//get_leader_len() returns the length in bytes of the prefix of the given string which introduces 
//a comment. If this string is not a comment then 0 is returned. When "flags" is not NULL, it is 
//set to point to the flags of the recognized comment leader.
//"backward" must be true for the "O" command.
//If "include_space" is set, include trailing whitespace while calculating the length.
pub int
get_leader_len(CS line, Byte** flags, int backward, int include_space) {
   if (!curBook->o.comments) {
      return 0;
   }
   
   int j;
   int got_com = false;
   Boole foundOne;
   Byte   part_buf[COM_MAX_LEN];   //buffer for one option part
   CS string;      //pointer to comment string
   CS list;
   int middle_match_len = 0;
   CS preList;
   CS saved_flags = NULL;

   int i = 0;
   int result = 0;
   while (SPACE_OR_TAB(line[i]))    //leading white space is ignored
      ++i;

   //Repeat to match several nested comment strings.
   while (line[i] != ZERO) {
      //scan through the 'comments' option for a match
      foundOne = false;
      for (list = curBook->o.comments; *list != ZERO; ) {
         //Get one option part into part_buf[].  Advance "list" to next
         //one.  Put "string" at start of string.
         if (!got_com && flags)
            *flags = list;       //remember where flags started
         preList = list;
         (void)strCutPathFromListOfPaths(OUT &list, OUT part_buf, COM_MAX_LEN, S",");
         string = firstOccurrence(part_buf, ':');
         if (string == NULL)       //missing ':', ignore this part
            continue;
         *string++ = ZERO;       //isolate flags from string

         //If we found a middle match previously, use that match when this is not a middle or end.
         if (middle_match_len != 0
                && firstOccurrence(part_buf, COM_MIDDLE) == NULL
                && firstOccurrence(part_buf, COM_END) == NULL)
            break;

         //When we already found a nested comment, only accept further nested comments.
         if (got_com && firstOccurrence(part_buf, COM_NEST) == NULL)
            continue;

         //When 'O' flag present and using "O" command skip this one.
         if (backward && firstOccurrence(part_buf, COM_NOBACK) != NULL)
            continue;

         //Line contents and string must match.
         //When string starts with white space, must have some white space
         //(but the amount does not need to match, there might be a mix of TABs and spaces).
         if (SPACE_OR_TAB(string[0])) {
            if (i == 0 || !SPACE_OR_TAB(line[i - 1]))
               continue;  //missing white space
            while (SPACE_OR_TAB(string[0]))
               ++string;
         }
         for (j = 0; string[j] != ZERO && string[j] == line[i + j]; ++j)
            {}
         if (string[j] != ZERO)
            continue;  //string doesn't match

         //When 'b' flag set, there must be white space or an end-of-line after the string in the line
         if (firstOccurrence(part_buf, COM_BLANK) != NULL
               && !SPACE_OR_TAB(line[i + j]) && line[i + j] != ZERO)
            continue;

         //We have found a match, stop searching unless this is a middle comment. The middle comment 
         //can be a substring of the end comment in which case it's better to return the length of the
         //end comment and its flags.  Thus we keep searching with middle and end matches and use an 
         //end match if it matches better.
         if (firstOccurrence(part_buf, COM_MIDDLE) != NULL) {
            if (middle_match_len == 0) {
               middle_match_len = j;
               saved_flags = preList;
            }
            continue;
         }
         if (middle_match_len != 0 && j > middle_match_len)
            //Use this match instead of the middle match, since it's a longer thus better match.
            middle_match_len = 0;

         if (middle_match_len == 0)
            i += j;
         foundOne = true;
         break;
      }

      if (middle_match_len != 0) {
         //Use the previously found middle match after failing to find a match with an end.
         if (!got_com && flags != NULL)
            *flags = saved_flags;
         i += middle_match_len;
         foundOne = true;
      }

      //No match found, stop scanning.
      if (!foundOne)
         break;

      result = i;

      //Include any trailing white space.
      while (SPACE_OR_TAB(line[i]))
         ++i;

      if (include_space)
         result = i;

      //If this comment doesn't nest, stop here.
      got_com = true;
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
//     OPENLINE_DO_COM   format comments
//     OPENLINE_KEEPTRAIL   keep trailing spaces
//     OPENLINE_MARKFIX   adjust mark positions after the line break
//     OPENLINE_COM_LIST   format comments with list or 2nd line indent
//     OPENLINE_FORCE_INDENT  set indent from second_line_indent, ignore 'autoindent'
//
//"second_line_indent": indent for after ^^D in Insert mode or if flag OPENLINE_COM_LIST
//"did_do_comment" is set to true when intentionally putting the comment leader in front of the 
//new line.
//
//Return OK for success, FAIL for failure
pub int
openLine(
   Unt flags,
   int second_line_indent
){
   CS savedLine;      //copy of the original line
   CS nextLine = NULL;   //copy of the next line
   CS transferText = NULL;   //what goes to next line
   int transferLen = 0;   //length of transferText string
   int fewerCols = 0;      //fewer columns for mark in new line
   int fewerColsOff = 0;   //columns to skip for mark and textprop adjustment
   Pos old_cursor;      //old cursor position
   int newcol = 0;      //new cursor column
   int newindent = 0;      //auto-indent of the new line
   int n;
   int shouldTruncateLine = false;
   int retval = FAIL;      //return value
   int lead_len;      //length of comment leader
   int comment_start = 0;   //start index of the comment leader
   CS lead_flags;   //position in 'comments' for comment leader
   CS leader = NULL;      //copy of comment leader
   CS allocated = NULL;   //allocated memory
   CS p;
   int      saved_char = ZERO;   //init for GCC
   Pos* pos;
   int do_si = may_do_si();
   int no_si = false;      //reset didSindentG afterwards
   int first_char = ZERO;   //init for GCC
   int didAppend;      //appended a new line
   int at_eol;         //cursor after last character

   //make a copy of the current line so we can mess with it
   savedLine = copySubstr(ml_get_curline(), ml_get_curline_len());

   at_eol = curPor->cursor.col >= (int)ml_get_curline_len();

   if (stateG & MODE_INSERT) {
      transferText = savedLine + curPor->cursor.col;
      if (do_si) { //need first char after new line break
          p = skipwhite(transferText);
          first_char = *p;
      }
      transferLen = (int)STRLEN(transferText);
      
      saved_char = *transferText;
      *transferText = ZERO;
   }

   u_clearline();      //cannot do "U" command when adding lines
   didSindentG = false;
   ai_col = 0;

   //If we just did an auto-indent, then we didn't type anything on
   //the prior line, and it should be truncated.  Do this even if 'ai' is not
   //set because automatically inserting a comment leader also sets didAindentG.
   if (didAindentG)
      shouldTruncateLine = true;

   if ((flags & OPENLINE_FORCE_INDENT) != 0 && second_line_indent >= 0) {
      newindent = second_line_indent;
      //If 'autoindent' and/or 'smartindent' is set, try to figure out what
      //indent to use for the new line.
   } ei (curBook->o.autoIndent || do_si) {
      //count white space on current line
      newindent = get_indent_str(savedLine, (int)curBook->o.shiftWidth);
      if (newindent == 0 && (flags & OPENLINE_COM_LIST) == 0) {
          newindent = second_line_indent; //for ^^D command in insert mode
      }

      //Do smart indenting. In insert/replace mode we may move 
      //some text to the next line. If it starts with '{' don't add an indent. Fixes inserting 
      //a NL before '{' in line
      //  `if (condition) {`
      if (!shouldTruncateLine && do_si && *savedLine != ZERO 
            && (transferText == NULL || first_char != '{')) {
         Byte  last_char;

         old_cursor = curPor->cursor;
         CS ptr = savedLine;
         if ((flags & OPENLINE_DO_COM) != 0) {
             lead_len = get_leader_len(ptr, NULL, false, true);
         } else {
             lead_len = 0;
         }
         //Skip preprocessor directives, unless they are recognized as comments.
         if ( lead_len == 0 && ptr[0] == '#') {
            while (ptr[0] == '#' && curPor->cursor.lnum > 1)
                ptr = ml_get(--curPor->cursor.lnum);
            newindent = get_indent();
         }
         if ((flags & OPENLINE_DO_COM) != 0)
            lead_len = get_leader_len(ptr, NULL, false, true);
         else
            lead_len = 0;
         if (lead_len > 0) {
            //This case gets the following right:
            //      /*
            //       * A comment (read '\' as '/').
            //       */
            //#define IN_THE_WAY
            //      This should line up here;
            p = skipwhite(ptr);
            if (p[0] == '/' && p[1] == '*')
               p++;
            if (p[0] == '*') {
               for (p++; *p; p++) {
                  if (p[0] == '/' && p[-1] == '*') {
                     //End of C comment, indent should line up with the line containing 
                     //the start of the comment.
                     curPor->cursor.col = (ColNr)(p - ptr);
                     if ((pos = findmatch(NULL, ZERO)) != NULL) {
                        curPor->cursor.lnum = pos->lnum;
                        newindent = get_indent();
                        break;
                     }
                     //this may make "ptr" invalid, get it again
                     ptr = ml_get(curPor->cursor.lnum);
                     p = ptr + curPor->cursor.col;
                  }
               }
            }
         } else { //Not a comment line
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
            //Try to catch lines that are split over multiple lines. eg:
            //      if (condition &&
            //        condition) {
            //     Should line up here!
            //      }
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
               didSindentG = true;   //do indent
               no_si = true;   //don't delete it when '{' typed
               //Look for "if" and the like, use 'cinwords'.
               //Don't do this if the previous line ended in ';' or '}'.
            } ei (last_char != ';' && last_char != '}' && cin_is_cinword(ptr)) {
                didSindentG = true;
            }
         }
         curPor->cursor = old_cursor;
      }
      if (do_si)
         can_si = true;

      didAindentG = true;
   }

   //Find out if the current line starts with a comment leader like ` * `.
   //This may then be inserted in front of the new line.
   end_comment_pending = ZERO;
   if ((flags & OPENLINE_DO_COM) != 0) {
      lead_len = get_leader_len(savedLine, &lead_flags, false, true);
   } else
      lead_len = 0;
   if (lead_len > 0) {
      CS lead_repl = NULL;       //replaces comment leader
      int lead_repl_len = 0;       //length of *lead_repl
      Byte lead_middle[COM_MAX_LEN];   //middle-comment string
      Byte lead_end[COM_MAX_LEN];       //end-comment string
      CS comment_end = NULL;       //where lead_end has been found
      int extra_space = false;       //append extra space
      int current_flag;
      int require_blank = false;       //requires blank after middle
      CS p2;

      //If the comment leader has the start, middle or end flag, it may not
      //be used or may be replaced with the middle leader.
      for (p = lead_flags; *p && *p != ':'; ++p) {
         if (*p == COM_BLANK) {
            require_blank = true;
            continue;
         }
         if (*p == COM_START || *p == COM_MIDDLE) {
            current_flag = *p;
            if (*p == COM_START) {
                //find start of middle part
                (void)strCutPathFromListOfPaths(OUT &p, OUT lead_middle, COM_MAX_LEN, S",");
                require_blank = false;
            }

            //Isolate the strings of the middle and end leader.
            while (*p && p[-1] != ':') { //find end of middle flags
               if (*p == COM_BLANK)
                  require_blank = true;
               ++p;
            }
            (void)strCutPathFromListOfPaths(OUT &p, OUT lead_middle, COM_MAX_LEN, S",");

            while (*p && p[-1] != ':') {//find end of end flags
               //Check whether we allow automatic ending of comments
               if (*p == COM_AUTO_END)
                  end_comment_pending = UNT; //means we want to set it
               ++p;
            }
            n = strCutPathFromListOfPaths(OUT &p, OUT lead_end, COM_MAX_LEN, S",");

            if (end_comment_pending == UNT)   //we can set it now
               end_comment_pending = lead_end[n - 1];

            //If the end of the comment is in the same line, don't use the comment leader.
            for (p = savedLine + lead_len; *p; ++p) {
               if (STRNCMP(p, lead_end, n) == 0) {
                  comment_end = p;
                  lead_len = 0;
                  break;
               }
            }

            //Doing "o" on a start of comment inserts the middle leader.
            if (lead_len > 0) {
                if (current_flag == COM_START) {
                  lead_repl = lead_middle;
                  lead_repl_len = (int)STRLEN(lead_middle);
                }

                //If we have hit RETURN immediately after the start comment leader, then put 
                //a space after the middle comment leader on the next line.
                if (!SPACE_OR_TAB(savedLine[lead_len - 1])
                   && ((transferText != NULL && (int)curPor->cursor.col == lead_len)
                  || (transferText == NULL && savedLine[lead_len] == ZERO)
                  || require_blank))
               extra_space = true;
            }
            break;
         }
         if (*p == COM_END) {
            //Doing "o" on the end of a comment does not insert leader. Remember where the end is,
            //might want to use it to find the start (for C-comments).
            comment_end = skipwhite(savedLine);
            lead_len = 0;
            break;

            //Doing "O" on the end of a comment inserts the middle leader.
            //Find the string for the middle leader, searching backwards.
            while (p > curBook->o.comments && *p != ',')
                --p;
            for (
               lead_repl = p; lead_repl > curBook->o.comments && lead_repl[-1] != ':'; --lead_repl
            ) {} 
            lead_repl_len = (int)(p - lead_repl);

            //We can probably always add an extra space when doing "O" on the comment-end
            extra_space = true;

            //Check whether we allow automatic ending of comments
            for (p2 = p; *p2 && *p2 != ':'; p2++) {
                if (*p2 == COM_AUTO_END)
               end_comment_pending = UNT; //means we want to set it
            }
            if (end_comment_pending == UNT) {
               //Find last character in end-comment string
               while (*p2 && *p2 != ',')
                  p2++;
               end_comment_pending = p2[-1];
            }
            break;
         }
         if (*p == COM_FIRST) {
            //Comment leader for first line only:   Don't repeat leader
            //when using "O", blank out leader when using "o".
            lead_repl = (CS)"";
            lead_repl_len = 0;
            break;
         }
      }
      if (lead_len) {
         //allocate buffer (may concatenate transferText later)
         leader = alloc(lead_len + lead_repl_len + extra_space + transferLen
              + (second_line_indent > 0 ? second_line_indent : 0) + 1);
         allocated = leader;          //remember to free it later

         if (!leader)
            lead_len = 0;
         else {
            copySubstrToAllocation(leader, (Text){savedLine, lead_len});

            //TODO: handle multi-byte and double width chars
            for (int li = 0; li < comment_start; ++li) {
               if (!SPACE_OR_TAB(leader[li]))
                  leader[li] = ' ';
            } 

            //Replace leader with lead_repl, right or left adjusted
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
               if (c == COM_RIGHT) {   //right adjusted leader
                  //find last non-white in the leader to line up with
                  for (p = leader + lead_len - 1; p > leader && SPACE_OR_TAB(*p); --p)
                      {} 
                  ++p;

                  //Compute the length of the replaced characters in
                  //screen characters, not bytes.
                  {
                     int       repl_size = eeglStrNsize(lead_repl, lead_repl_len);
                     int       old_size = 0;
                     Byte  *endp = p;
                     int       l;

                     while (old_size < repl_size && p > leader) {
                        MB_PTR_BACK(leader, p);
                        old_size += bookPtr2Cells(p);
                     }
                     l = lead_repl_len - (int)(endp - p);
                     if (l != 0)
                        MEMMOVE(endp + l, endp, (Unt)((leader + lead_len) - endp));
                     lead_len += l;
                  }
                  MEMMOVE(p, lead_repl, (Unt)lead_repl_len);
                  if (p + lead_repl_len > leader + lead_len)
                     p[lead_repl_len] = ZERO;

                  //blank-out any other chars from the old leader.
                  while (--p >= leader) {
                     int l = mb_head_off(leader, p);

                     if (l > 1) {
                        p -= l;
                        if (bookPtr2Cells(p) > 1) {
                           p[1] = ' ';
                           --l;
                        }
                        MEMMOVE(p + 1, p + l + 1, (Unt)((leader + lead_len) - (p + l + 1)));
                        lead_len -= l;
                        *p = ' ';
                     } ei (!SPACE_OR_TAB(*p))
                        *p = ' ';
                  }
               } else { //left adjusted leader
                  p = skipwhite(leader);

                  //Compute the length of the replaced characters in
                  //screen characters, not bytes. Move the part that is not to be overwritten.
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
                     MEMMOVE(
                        p + lead_repl_len, p + i, (Unt)(lead_len - i - (p - leader))
                     );
                     lead_len += lead_repl_len - i;
                  }
                  }
                  MEMMOVE(p, lead_repl, (Unt)lead_repl_len);

                  //Replace any remaining non-white chars in the old leader by spaces. 
                  //Keep Tabs, the indent must remain the same.
                  for (p += lead_repl_len; p < leader + lead_len; ++p) {
                     if (!SPACE_OR_TAB(*p)) {
                        //Don't put a space before a TAB.
                        if (p + 1 < leader + lead_len && p[1] == TAB) {
                           --lead_len;
                           MEMMOVE(p, p + 1, (leader + lead_len) - p);
                       } else {
                           int l = utfCharLen(p);

                           if (l > 1) {
                              if (bookPtr2Cells(p) > 1) {
                                //Replace a double-wide char with
                                //two spaces
                                --l;
                                *p++ = ' ';
                             }
                             MEMMOVE(p + 1, p + l, (leader + lead_len) - p);
                             lead_len -= l - 1;
                           }
                           *p = ' ';
                        }
                     }
                   }
                   *p = ZERO;
                }

                //Recompute the indent, it may have changed.
                if (curBook->o.autoIndent || do_si)
                   newindent = get_indent_str(leader, (int)curBook->o.shiftWidth);

                //Add the indent offset
                if (newindent + off < 0) {
                   off = -newindent;
                   newindent = 0;
                } else {
                   newindent += off;
                }

                //Correct trailing spaces for the shift, so that alignment remains equal
                while (off > 0 && lead_len > 0 && leader[lead_len - 1] == ' ') {
                   //Don't do it when there is a tab before the space
                   if (firstOccurrence(skipwhite(leader), '\t') != NULL)
                      break;
                    --lead_len;
                    --off;
                }

                //If the leader ends in white space, don't add an extra space
                if (lead_len > 0 && SPACE_OR_TAB(leader[lead_len - 1]))
                   extra_space = false;
                leader[lead_len] = ZERO;
            }

            if (extra_space) {
               leader[lead_len++] = ' ';
               leader[lead_len] = ZERO;
            }

            newcol = lead_len;

            //if a new indent will be set below, remove the indent in the comment leader
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

   //(stateG == MODE_INSERT)
   if (transferText)     {
      *transferText = saved_char;      //restore char that ZERO replaced

      //When 'ai' set or "flags" has OPENLINE_DELSPACES, skip to the first non-blank.
      if (curBook->o.autoIndent || (flags & OPENLINE_DELSPACES)) {
         while ((*transferText == ' ' || *transferText == '\t')
             && (!utf_iscomposing(mb_ptr2char(transferText + 1)))
         ){
            ++transferText;
            ++fewerColsOff;
         }
      }

      //columns for marks adjusted for removed columns
      fewerCols = (int)(transferText - savedLine);
   } else {
      transferText = S"";          //append empty line
   }
    
   //concatenate leader and transferText, if there is a leader
   if (lead_len) {
      if ((flags & OPENLINE_COM_LIST) != 0 && second_line_indent > 0) {
         int i;
         int padding = second_line_indent  - (newindent + (int)STRLEN(leader));

         //Here whitespace is inserted after the comment char.
         //Below, set_indent(newindent, SIN_INSERT) will insert the
         //whitespace needed before the comment char.
         for (i = 0; i < padding; i++) {
            STRCAT(leader, " ");
            fewerCols--;
            newcol++;
         }
      }
      STRCAT(leader, transferText);
      transferText = leader;
      didAindentG = true;       //So truncating blanks works with comments
      fewerCols -= lead_len;
   } else
      end_comment_pending = ZERO;  //turns out there was no leader

   old_cursor = curPor->cursor;
   if (ml_append(curPor->cursor.lnum, transferText, (ColNr)transferLen, false) == FAIL)
      goto theend;
   //Postpone calling doChangedLines(), because it would mess up folding with markers.
   markAdjust(curPor->cursor.lnum + 1, (LineNr)MAXLNUM, 1L, 0L, true);
   didAppend = true;
   if (stateG & MODE_INSERT) {
      //Properties after the split move to the next line.
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
      //Copy the indent
      (void)set_indent(newindent, SIN_INSERT);
      fewerCols -= curPor->cursor.col;

      ai_col = curPor->cursor.col;

      newcol += curPor->cursor.col;
     if (no_si)
          didSindentG = false;
   }

   curPor->cursor = old_cursor;

   if (shouldTruncateLine || (stateG & MODE_INSERT)) {
      //truncate current line at cursor
      savedLine[curPor->cursor.col] = ZERO;
      //Remove trailing white space, unless OPENLINE_KEEPTRAIL used.
      if (shouldTruncateLine && (flags & OPENLINE_KEEPTRAIL) == 0) {
         truncate_spaces(savedLine, curPor->cursor.col);
      }
      ml_replace(curPor->cursor.lnum, savedLine, false);
      savedLine = NULL;
      if (didAppend) {
          doChangedLines(
             curPor->cursor.lnum, curPor->cursor.col, curPor->cursor.lnum + 1, 1L
          );
          didAppend = false;

          //Move marks after the line break to the new line.
          if ((flags & OPENLINE_MARKFIX) != 0) {
             mark_col_adjust(curPor->cursor.lnum, curPor->cursor.col + fewerColsOff,
                1L, (long)-fewerCols, 0
             );
          }
          //Keep into account the deleted blanks on the new line.
          if (curBook->hasTextprop && fewerColsOff != 0) {
             adjustPropColumns(curPor->cursor.lnum + 1, 0, -fewerColsOff, 0);
          }
      } else {
            changed_bytes(curPor->cursor.lnum, curPor->cursor.col);
         }
      }

      //Put the cursor on the new line.  Careful: the scrollup() above may
      //have moved cursor, we must use old_cursor.
      curPor->cursor.lnum = old_cursor.lnum + 1;
   if (didAppend)
      doChangedLines(curPor->cursor.lnum, 0, curPor->cursor.lnum, 1L);

   curPor->cursor.col = newcol;
   curPor->cursor.coladd = 0;

   retval = OK;      //success!
theend:
   eeglFree(savedLine);
   eeglFree(nextLine);
   eeglFree(allocated);
   return retval;
}

//Delete from cursor to end of line. Caller must have prepared for undo.
//If "fixpos" is true fix the cursor position when done.
//
//Return FAIL for failure, OK otherwise.
pub int
truncate_line(int fixpos) {
   LineNr   lnum = curPor->cursor.lnum;
   ColNr   col = curPor->cursor.col;

   CS old_line = ml_get(lnum);
   CS newp = (col == 0) ? copyStr(S"") : copySubstr(old_line, col);
   int deleted = (int)ml_get_len(lnum) - col;
   ml_replace(lnum, newp, false);

   //mark the book as changed and prepare for displaying
   inserted_bytes(lnum, curPor->cursor.col, -deleted);

   //If "fixpos" is true we don't want to end up positioned at the ZERO.
   if (fixpos && curPor->cursor.col > 0)
      --curPor->cursor.col;

   return OK;
}

//Delete "nlines" lines at the cursor. Saves the lines for undo first if "undo" is true.
pub void
del_lines(long nlines,   int undo) {
   long   n;
   LineNr   first = curPor->cursor.lnum;

   if (nlines <= 0)
      return;

   //save the deleted lines for undo
   if (undo && u_savedel(first, nlines) == FAIL)
      return;

   for (n = 0; n < nlines; ) {
      if (curBook->mem.flags & ML_EMPTY)       //nothing to delete
         break;

      ml_delete_flags(first, ML_DEL_MESSAGE);
      ++n;

      //If we delete the last line in the file, stop
      if (first > curBook->mem.lineCount)
          break;
   }

   //Correct the cursor position before calling deleted_lines_mark(), it may
   //trigger a callback to display the cursor.
   curPor->cursor.col = 0;
   check_cursor_lnum();

   //adjust marks, mark the book as changed and prepare for displaying
   deleted_lines_mark(first, n);
}

//}}}
//{{{operators

//implementation of various operators: op_shift, op_delete, op_tilde, op_change, op_yank, doJoinLinesUnderCursor

private void shift_block(Operator *oper, int amount);
private void mb_adjust_opend(Operator *oper);
private int do_addsub(int opTy, Pos *pos, int length, LineNr prenum1);
private void pbyte(Pos lp, int c);
#define PBYTE(lp, c) pbyte(lp, c)


//Flags for third item in "opchars".
#define OPF_LINES  1   //operator always works on lines
#define OPF_CHANGE 2   //operator changes text

//The names of operators.
//IMPORTANT: Index must correspond with defines in eegl.h!!! The third field holds OPF_ flags.
private Byte opchars[][3] = {
   {ZERO, ZERO, 0},              //OP_NOP
   {'d', ZERO, OPF_CHANGE},      //OP_DELETE
   {'y', ZERO, 0},               //OP_YANK
   {'c', ZERO, OPF_CHANGE},      //OP_CHANGE
   {'x', ZERO, OPF_CHANGE},      //OP_CUT
   {'<', ZERO, OPF_LINES | OPF_CHANGE},   //OP_LSHIFT
   {'>', ZERO, OPF_LINES | OPF_CHANGE},   //OP_RSHIFT
   {'!', ZERO, OPF_LINES | OPF_CHANGE},   //OP_FILTER
   {'g', '~', OPF_CHANGE},      //OP_TILDE
   {'=', ZERO, OPF_LINES | OPF_CHANGE},   //OP_INDENT
   {'g', 'q', OPF_LINES | OPF_CHANGE},   //OP_FORMAT
   {':', ZERO, OPF_LINES},      //OP_COLON
   {'g', 'U', OPF_CHANGE},      //OP_UPPER
   {'g', 'u', OPF_CHANGE},      //OP_LOWER
   {'J', ZERO, OPF_LINES | OPF_CHANGE},   //DO_JOIN
   {'g', 'J', OPF_LINES | OPF_CHANGE},   //DO_JOIN_NS
   {'g', '?', OPF_CHANGE},       //OP_ROT13
   {'r', ZERO, OPF_CHANGE},      //OP_REPLACE
   {'I', ZERO, OPF_CHANGE},      //OP_INSERT
   {'A', ZERO, OPF_CHANGE},      //OP_APPEND
   {'z', 'f', OPF_LINES},        //OP_FOLD
   {'z', 'o', OPF_LINES},        //OP_FOLDOPEN
   {'z', 'O', OPF_LINES},        //OP_FOLDOPENREC
   {'z', 'c', OPF_LINES},        //OP_FOLDCLOSE
   {'z', 'C', OPF_LINES},        //OP_FOLDCLOSEREC
   {'z', 'd', OPF_LINES},        //OP_FOLDDEL
   {'z', 'D', OPF_LINES},        //OP_FOLDDELREC
   {'g', 'w', OPF_LINES | OPF_CHANGE},   //OP_FORMAT2
   {'g', '@', OPF_CHANGE},       //OP_FUNCTION
   {Ctrl_A, ZERO, OPF_CHANGE},   //OP_ADD
   {Ctrl_X, ZERO, OPF_CHANGE}    //OP_SUB
};

//Translate an action name into an operator type. Must only be called with a valid operator name!
pub Unt
get_op_type(Unt char1, Unt char2) {
   if (char1 == 'r')      //ignore second character
      return OP_REPLACE;
   if (char1 == '~')      //when tilde is an operator
      return OP_TILDE;
   if (char1 == 'g' && char2 == Ctrl_A)   //add
      return OP_ADD;
   if (char1 == 'g' && char2 == Ctrl_X)   //subtract
      return OP_SUB;
   if (char1 == 'z' && char2 == 'y')   //OP_YANK
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

//Return true if operator "op" always works on whole lines.
private Boole
op_on_lines(int op) {
   return (opchars[op][2] & OPF_LINES) != 0;
}

//Return true if operator "op" changes text.
pub Boole
op_is_change(int op) {
   return (opchars[op][2] & OPF_CHANGE) != 0;
}

//Get first operator command character. Returns 'g' or 'z' if there is another command character.
pub int
get_op_char(int optype) {
   return opchars[optype][0];
}

//Get second operator command character.
pub int
get_extra_op_char(int optype) {
   return opchars[optype][1];
}

//op_shift - handle a shift operation
pub void
op_shift(Operator *oper, int curs_top, int amount) {
   if (u_save((LineNr)(oper->start.lnum - 1), (LineNr)(oper->end.lnum + 1)) == FAIL)
      return;

   int block_col = 0;
   if (oper->block_mode)
      block_col = curPor->cursor.col;

   for (long i = oper->line_count; --i >= 0; ) {
      int first_char = *ml_get_curline();
      if (first_char == ZERO)            //empty line
         curPor->cursor.col = 0;
      ei (oper->block_mode)
         shift_block(oper, amount);
      ei (first_char != '#' || !preprocs_left())
         //Move the line right if it doesn't start with '#', 'smartindent'
         //isn't set or 'cindent' isn't set or '#' isn't in 'cino'.
         shift_line(oper->opTy == OP_LSHIFT, true, amount, false);
      ++curPor->cursor.lnum;
   }

   doChangedLines(oper->start.lnum, 0, oper->end.lnum + 1, 0L);
   if (oper->block_mode) {
      curPor->cursor.lnum = oper->start.lnum;
      curPor->cursor.col = block_col;
   } ei (curs_top) {      //put cursor on first line, for ">>"
      curPor->cursor.lnum = oper->start.lnum;
      beginline(BL_SOL | BL_FIX);   //shift_line() may have set cursor.col
   } else
      --curPor->cursor.lnum;   //put cursor on last line, for ":>"

   //The cursor line is not in a closed fold
   foldOpenCursor();

   CS op = (oper->opTy == OP_RSHIFT) ? S">" : S"<";
   CS msg_line_single = NGETTEXT("%ld line %sed %d time", "%ld line %sed %d times", amount);
   CS msg_line_plural = NGETTEXT("%ld lines %sed %d time", "%ld lines %sed %d times", amount);
   eeSnprintf(IObuff, IOSIZE,
      NGETTEXT(msg_line_single, msg_line_plural, oper->line_count),
      oper->line_count, op, amount);
   msgAndKeep(IObuff, 0, true);

   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
      //Set "'[" and "']" marks.
      curBook->opStart = oper->start;
      curBook->opEnd.lnum = oper->end.lnum;
      curBook->opEnd.col = ml_get_len(oper->end.lnum);
      if (curBook->opEnd.col > 0)
         --curBook->opEnd.col;
   }
}


private Long
get_new_sw_indent(
   int      left,      //true if shift is to the left
   int      round,      //true if new indent is to be to a tabstop
   Long   amount,      //Number of shifts
   Long   sw_val)
{
   Long   count = get_indent();
   Long   i, j;

   if (round) {        //round off indent
      i = trim_to_int(count) / sw_val;   //number of 'shiftwidth' rounded down
      j = trim_to_int(count) % sw_val;   //extra spaces
      if (j && left)      //first remove extra spaces
          --amount;
      if (left) {
         i -= amount;
         if (i < 0)
            i = 0;
      } else
         i += amount;
      count = i * sw_val;
   } else {        //original vi indent
      if (left) {
         count -= sw_val * amount;
         if (count < 0)
            count = 0;
      } else
         count += sw_val * amount;
   }

   return count;
}

//Shift the current line 'amount' shiftwidth(s) left (if 'left' is true) or right.
//
//The rules for choosing a shiftwidth are: If 'shiftwidth' is non-zero, use 'shiftwidth'; else if 
//'vartabstop' is not empty, use 'vartabstop'; else use 'tabstop'. The Eegl documentation says 
//nothing about 'softtabstop' or 'varsofttabstop' affecting the shiftwidth, and neither affects the
//shiftwidth in current versions of Eegl, so they are not considered here.
pub void
shift_line(
   int   left,         //true if shift is to the left
   int   round,         //true if new indent is to be to a tabstop
   int   amount,         //Number of shifts
   Boole   call_changed_bytes)   //call changed_bytes()
{
   Long   count;
   long   sw_val = curBook->o.shiftWidth;

   count = get_new_sw_indent(left, round, amount, sw_val);

   //Set new indent
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
   Unt      new_line_len;   //the length of the line after the block shift

   stateG = MODE_INSERT; 
   block_prep(oper, OUT &bd, curPor->cursor.lnum, true);
   if (bd.is_short)
      return;

   //total is number of screen columns to be inserted/removed
   total = (int)((unsigned)amount * (unsigned)sw_val);
   if ((total / sw_val) != amount)
      return; //multiplication overflow

    CS oldp = ml_get_curline();
    oldlen = ml_get_curline_len();

   if (!left) {
      int tabs = 0, spaces = 0;
      CharTableSize   cts;

      //1. Get start vcol
      //2. Total ws vcols
      //3. Divvy into TABs & spp
      //4. Construct new string
      total += bd.pre_whitesp; //all virtual WS up to & incl a split TAB
      ws_vcol = bd.start_vcol - bd.pre_whitesp;
      if (bd.startspaces) {
         if (utfCharLen(bd.textstart) == 1)
            ++bd.textstart;
         else {
            ws_vcol = 0;
            bd.startspaces = 0;
         }
      }

      //TODO: is passing bd.textstart for start of the line OK?
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

      //OK, now total=all the VWS reqd, and textstart points at the 1st non-ws char in the block.
      if (!curBook->o.expandTab)
         tabs = ((ws_vcol % sw_val) + total) / sw_val; //number of tabs
      if (tabs > 0)
         spaces = ((ws_vcol % sw_val) + total) % sw_val; //number of spp
      else
         spaces = total;
      //if we're splitting a TAB, allow for it
      bd.textcol -= bd.pre_whitesp_c - (bd.startspaces != 0);

      new_line_len = bd.textcol + tabs + spaces + (oldlen - (bd.textstart - oldp));
      newp = alloc(new_line_len + 1);
      MEMMOVE(newp, oldp, (Unt)bd.textcol);
      newlen = bd.textcol;
      memset(newp + newlen, TAB, (Unt)tabs);
      newlen += tabs;
      memset(newp + newlen, ' ', (Unt)spaces);
      STRCPY(newp + newlen + spaces, bd.textstart);
   } else {//left
      ColNr       destination_col;   //column to which text in block will
                  //be shifted
      Byte       *verbatim_copy_end;   //end of the part of the line which is
                  //copied verbatim
      ColNr       verbatim_copy_width;//the (displayed) width of this part
                  //of line
      Unt       fill;      //nr of spaces that replace a TAB
      Unt       block_space_width;
      Unt       shift_amount;
      Byte       *non_white = bd.textstart;
      ColNr       non_white_col;
      Unt       fixedlen;      //length of string left of the shift
                  //position (ie the string not being shifted)
      CharTableSize cts;

      /*
       * Firstly, let's find the first non-whitespace character that is
       * displayed after the block's start column and the character's column
       * number. Also, let's calculate the width of all the whitespace
       * characters that are displayed in the block and precede the searched
       * non-whitespace character.
       */

      //If "bd.startspaces" is set, "bd.textstart" points to the character,
      //the part of which is displayed at the block's beginning. Let's start
      //searching from the next character.
      if (bd.startspaces)
          MB_PTR_ADV(non_white);

      //The character's column is in "bd.start_vcol".
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
      //We will shift by "total" or "block_space_width", whichever is less.
      shift_amount = (block_space_width < (Unt)total ? block_space_width : (Unt)total);

      //The column to which we will shift the text.
      destination_col = (ColNr)(non_white_col - shift_amount);

      //Now let's find out how much of the beginning of the line we can
      //reuse without modification.
      verbatim_copy_end = bd.textstart;
      verbatim_copy_width = bd.start_vcol;

      //If "bd.startspaces" is set, "bd.textstart" points to the character
      //preceding the block. We have to subtract its width to obtain its column number.
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

      //If "destination_col" is different from the width of the initial
      //part of the line that will be copied, it means we encountered a tab
      //character, which we will have to partly replace with spaces.
      fill = destination_col - verbatim_copy_width;

      //The replacement line will consist of:
      //- the beginning of the original line up to "verbatim_copy_end",
      //- "fill" number of spaces,
      //- the rest of the line, pointed to by non_white.
      fixedlen = verbatim_copy_end - oldp;
      new_line_len = fixedlen + fill + (oldlen - (non_white - oldp));

      newp = alloc(new_line_len + 1);
      MEMMOVE(newp, oldp, fixedlen);
      newlen = fixedlen;
      memset(newp + newlen, ' ', (Unt)fill);
      STRCPY(newp + newlen + fill, non_white);
   }
   //replace the line
   ml_replace(curPor->cursor.lnum, newp, false);

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
   int      count = 0;   //extra spaces to replace a cut TAB
   int      spaces = 0;   //non-zero if cutting a TAB
   ColNr   offset;      //pointer along new line
   ColNr   startcol;   //column where insert starts
   Byte   *newp, *oldp;   //new, old lines
   LineNr   lnum;      //loop var
   int      oldstate = stateG;
   int sw_val = curBook->o.shiftWidth;

   stateG = MODE_INSERT;

   for (lnum = oper->start.lnum + 1; lnum <= oper->end.lnum; lnum++) {
      block_prep(oper, OUT bdp, lnum, true);
      if (bdp->is_short && b_insert)
         continue;   //OP_INSERT, line ends before block start

      oldp = ml_get(lnum);

      if (b_insert) {
         spaces = bdp->startspaces;
         if (spaces != 0)
            count = sw_val - 1; //we're cutting a TAB
         offset = bdp->textcol;
      } else { //append
         if (!bdp->is_short) { //spaces = padding after block
            spaces = (bdp->endspaces ? sw_val - bdp->endspaces : 0);
            if (spaces != 0)
                count = sw_val - 1; //we're cutting a TAB
            offset = bdp->textcol + bdp->textlen - (spaces != 0);
         } else { //spaces = padding to block edge
            //if $ used, just append to EOL (ie spaces==0)
            if (!bdp->is_MAX)
                spaces = (oper->end_vcol - bdp->end_vcol) + 1;
            count = spaces;
            offset = bdp->textcol + bdp->textlen;
         }
      }

      if (spaces > 0)
         //avoid copying part of a multi-byte character
         offset -= (*mb_head_off)(oldp, oldp + offset);

      if (spaces < 0)  //can happen when the cursor was moved
          spaces = 0;

      //Make sure the allocated size matches what is actually copied below.
      newp = alloc(ml_get_len(lnum) + spaces + slen
             + (spaces > 0 && !bdp->is_short ? sw_val - spaces : 0)
                             + count + 1);

      //copy up to shifted part
      MEMMOVE(newp, oldp, (Unt)offset);
      oldp += offset;

      //insert pre-padding
      memset(newp + offset, ' ', (Unt)spaces);
      startcol = offset + spaces;

      //copy the new text
      MEMMOVE(newp + startcol, s, slen);
      offset += (int)slen;

      if (spaces > 0 && !bdp->is_short) {
         if (*oldp == TAB) {
            //insert post-padding
            memset(newp + offset + spaces, ' ', (Unt)(sw_val - spaces));
            //we're splitting a TAB, don't copy it
            oldp++;
            //We allowed for that TAB, remember this now
            count++;
         } else
            //Not a TAB, no extra spaces
            count = spaces;
      }

      if (spaces > 0)
         offset += count;
      STRCPY(newp + offset, oldp);

      ml_replace(lnum, newp, false);

      if (b_insert)
         //correct any text properties
         inserted_bytes(lnum, startcol, (int)slen);

      if (lnum == oper->end.lnum) {
         //Set "']" mark to the end of the block instead of the end of
         //the insert in the first line.
         curBook->opEnd.lnum = oper->end.lnum;
         curBook->opEnd.col = offset;
      }
   } //for all lnum

   doChangedLines(oper->start.lnum + 1, 0, oper->end.lnum + 1, 0L);

   stateG = oldstate;
}

//Get the screen position of character col with a coladd in the cursor line.
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

pub Unt
gchar_pos(Pos *pos) {
   //When searching columns is sometimes put at the end of a line.
   if (pos->col == MAXCOL)
      return ZERO;
   int ptrlen = ml_get_len(pos->lnum);
   CS ptr = ml_get_pos(pos);
   if (pos->col > ptrlen)
      return ZERO;
   return mb_ptr2char(ptr);
}

pub Unt
gchar_cursor(void) {
   return mb_ptr2char(ml_get_cursor());
}

//Handle a delete operation. Return FAIL if undo failed, OK otherwise.
pub int
op_delete(Operator* oper) {
   int n;
   LineNr      lnum;
   BlockDef   bd;
   LineNr old_lcount = curBook->mem.lineCount;
   int did_yank = false;

   if (curBook->mem.flags & ML_EMPTY)       //nothing to do
      return OK;

   //Nothing to delete, return here.   Do prepare undo, for op_change().
   if (oper->empty)
      return u_save_cursor();

   if (IMMUTABLE) {
      emsg(_(e_cannot_make_changes_modifiable_is_off));
      return FAIL;
   }

   if (oper->opTy == OP_DELETE) { //Deletion ("d" action) saves text only to numbered registers
      oper->regname = '_';
   } else {
      clipGetDefaultRegister(&oper->regname);
   }

   mb_adjust_opend(oper);

   //Check for trying to delete (e.g. "D") in an empty line. Note: For the change operator it is ok
   if (   oper->motion_type == MCHAR
       && oper->line_count == 1
       && oper->opTy == OP_DELETE
       && *ml_get(oper->start.lnum) == ZERO
   ){
      //It's an error to operate on an empty region
      if (virtual_op)
         //Virtual editing: Nothing gets deleted, but we set the '[ and '] marks as if it happened
         goto setmarks;
      return OK;
   }

   //Copy whatever we're about to delete to the register. If a yank register was specified, put 
   //the deleted text into that register. For the black hole register, '_' don't yank anything.
   if (oper->regname != '_') {
      if (oper->regname != 0) {
         //check for read-only register
         if (!valid_yank_reg(oper->regname, true)) {
            inpFlushIfNotSilent();
            return OK;
         }
         get_yank_register(oper->regname, true); //yank into specified register
         if (op_yank(oper, true, false) == OK)   //yank without message
            did_yank = true;
      } else
         reset_y_append(); //not appending to unnamed register

      //Put deleted text into register 1 and shift number registers if the delete contains a line 
      //break, or when using a specific operator (Vi compatible)
      if (oper->motion_type == MLINE || oper->line_count > 1 || oper->use_reg_one) {
         shift_delete_registers();
         if (op_yank(oper, true, false) == OK)
            did_yank = true;
      }

      //Yank into small delete register when no named register specified
      //and the delete is within one line.
      if ((oper->regname == '*' || oper->regname == '+' || oper->regname == 0) 
         && oper->motion_type != MLINE && oper->line_count == 1
      ){
         oper->regname = '-';
         get_yank_register(oper->regname, true);
         if (op_yank(oper, true, false) == OK)
            did_yank = true;
         oper->regname = 0;
      }

      //If there's too much stuff to fit in the yank register, then get a
      //confirmation before doing the delete. This is crude, but simple.
      //And it avoids doing a delete of something we can't put back if we want.
      if (!did_yank) {
         int msg_silent_save = msg_silent;

         msg_silent = 0;   //must display the prompt
         n = ask_yesno((CS)_("cannot yank; delete anyway"), true);
         msg_silent = msg_silent_save;
         if (n != 'y') {
            emsg(_(e_command_aborted));
            return FAIL;
         }
      }

      if (did_yank && has_textyankpost())
         yank_do_autocmd(oper, get_y_current());
   }

   //block mode delete
   if (oper->block_mode) {
      if (u_save((LineNr)(oper->start.lnum - 1), (LineNr)(oper->end.lnum + 1)) == FAIL)
         return FAIL;

      for (lnum = curPor->cursor.lnum; lnum <= oper->end.lnum; ++lnum) {
         block_prep(oper, OUT &bd, lnum, true);
         if (bd.textlen == 0)   //nothing to delete
            continue;

         //Adjust cursor position for tab replaced by spaces and 'lbr'.
         if (lnum == curPor->cursor.lnum) {
            curPor->cursor.col = bd.textcol + bd.startspaces;
            curPor->cursor.coladd = 0;
         }

         //"n" == number of chars deleted
         //If we delete a TAB, it may be replaced by several characters.
         //Thus the number of characters may increase!
         n = bd.textlen - bd.startspaces - bd.endspaces;
         CS oldp = ml_get(lnum);
         CS newp = alloc(ml_get_len(lnum) + 1 - n);
         //copy up to deleted part
         MEMMOVE(newp, oldp, (Unt)bd.textcol);
         //insert spaces
         memset(newp + bd.textcol, ' ', (Unt)(bd.startspaces + bd.endspaces));
         //copy the part after the deleted part
         STRCPY(newp + bd.textcol + bd.startspaces + bd.endspaces, oldp + bd.textcol + bd.textlen);
         //replace the line
         ml_replace(lnum, newp, false);

         if (curBook->hasTextprop && n != 0)
            adjustPropColumns(lnum, bd.textcol, -n, 0);
      }

      check_cursor_col();
      doChangedLines(curPor->cursor.lnum, curPor->cursor.col, oper->end.lnum + 1, 0L);
      oper->line_count = 0;       //no lines deleted
   } ei (oper->motion_type == MLINE) {
      if (oper->opTy == OP_CHANGE) {
         //Delete the lines except the first one.  Temporarily move the
         //cursor to the next line.  Save the current line number, if the
         //last line is deleted it may be changed.
         if (oper->line_count > 1) {
            lnum = curPor->cursor.lnum;
            ++curPor->cursor.lnum;
            del_lines((long)(oper->line_count - 1), true);
            curPor->cursor.lnum = lnum;
         }
         if (u_save_cursor() == FAIL)
            return FAIL;
         if (curBook->o.autoIndent)  {        //don't delete indent
            beginline(BL_WHITE);       //cursor on first non-white
            didAindentG = true;          //delete the indent when ESC hit
            ai_col = curPor->cursor.col;
         } else
            beginline(0);       //cursor in column 0
         truncate_line(false);  //delete the rest of the line, leaving cursor past last char in line
         if (oper->line_count > 1)
            u_clearline();      //"U" command not possible after "2cc"
      } else {
         del_lines(oper->line_count, true);
         beginline(BL_WHITE | BL_FIX);
         u_clearline();   //"U" command not possible after "dd"
      }
   } else {
      if (virtual_op) {
         int      endcol = 0;

         //For virtualedit: break the tabs that are partly included.
         if (gchar_pos(&oper->start) == '\t') {
            if (u_save_cursor() == FAIL)   //save first line for undo
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

         //Break a tab only when it's included in the area.
         if (gchar_pos(&oper->end) == '\t' && (int)oper->end.coladd < oper->inclusive) {
            //save last line for undo
            if (u_save((LineNr)(oper->end.lnum - 1), (LineNr)(oper->end.lnum + 1)) == FAIL)
               return FAIL;
            curPor->cursor = oper->end;
            coladvance_force(getviscol2(oper->end.col, oper->end.coladd));
            oper->end = curPor->cursor;
            curPor->cursor = oper->start;
          }
         mb_adjust_opend(oper);
      }

      if (oper->line_count == 1) {  //delete characters within one line
         if (u_save_cursor() == FAIL)   //save line for undo
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
      } else {          //delete characters between lines
         Pos   curpos;

         //save deleted and changed lines for undo
         if (u_save((LineNr)(curPor->cursor.lnum - 1),
               (LineNr)(curPor->cursor.lnum + oper->line_count)) == FAIL)
            return FAIL;

          truncate_line(true);   //delete from cursor to end of line

          curpos = curPor->cursor;   //remember curPor->cursor
          ++curPor->cursor.lnum;
          del_lines((long)(oper->line_count - 2), false);

          //delete from start of line until op_end
          n = (oper->end.col + 1 - !oper->inclusive);
          curPor->cursor.col = 0;
          (void)del_bytes((long)n, !virtual_op,
                oper->opTy == OP_DELETE && !oper->is_VIsual);
          curPor->cursor = curpos;   //restore curPor->cursor
          (void)doJoinLinesUnderCursor(2, false, false, false, false);
      }
      if (oper->opTy == OP_DELETE)
          auto_format(false, true);
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
   //Backup to the replaced character.
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
   int         had_ctrl_v_cr = false;

   if ((curBook->mem.flags & ML_EMPTY ) || oper->empty)
      return OK;       //nothing to do

   if (c == REPLACE_CR_NCHAR) {
      had_ctrl_v_cr = true;
      c = ENTER;
   } ei (c == REPLACE_NL_NCHAR) {
      had_ctrl_v_cr = true;
      c = NL;
   }

   mb_adjust_opend(oper);

   if (u_save((LineNr)(oper->start.lnum - 1), (LineNr)(oper->end.lnum + 1)) == FAIL)
      return FAIL;

   //block mode replace
   if (oper->block_mode) {
      bd.is_MAX = (curPor->cursWant == MAXCOL);
      for ( ; curPor->cursor.lnum <= oper->end.lnum; ++curPor->cursor.lnum) {
         curPor->cursor.col = 0;  //make sure cursor position is valid
         block_prep(oper, OUT &bd, curPor->cursor.lnum, true);
         if (bd.textlen == 0 && (!virtual_op || bd.is_MAX))
            continue;       //nothing to replace

         //n == number of extra chars required
         //If we split a TAB, it may be replaced by several characters.
         //Thus the number of characters may increase!
         //If the range starts in virtual space, count the initial
         //coladd offset as part of "startspaces"
         if (virtual_op && bd.is_short && *bd.textstart == ZERO) {
            Pos vpos;

         vpos.lnum = curPor->cursor.lnum;
         getvpos(&vpos, oper->start_vcol);
         bd.startspaces += vpos.coladd;
         n = bd.startspaces;
         } else
            //allow for pre spaces
            n = (bd.startspaces ? bd.start_char_vcols - 1 : 0);

         //allow for post spp
         n += (bd.endspaces
             && !bd.is_oneChar
             && bd.end_char_vcols > 0) ? bd.end_char_vcols - 1 : 0;
         //Figure out how many characters to replace.
         numc = oper->end_vcol - oper->start_vcol + 1;
         if (bd.is_short && (!virtual_op || bd.is_MAX))
            numc -= (oper->end_vcol - bd.end_vcol) + 1;

         //A double-wide character can be replaced only up to half the
         //times.
         if (mb_char2cells(c) > 1) {
            if ((numc & 1) && !bd.is_short) {
                ++bd.endspaces;
                ++n;
            }
            numc = numc / 2;
         }

         //Compute bytes needed, move character count to num_chars.
         num_chars = numc;
         numc *= mb_char2len(c);
         //oldlen includes textlen, so don't double count
         n += numc - bd.textlen;

         oldp = ml_get_curline();
         oldlen = ml_get_curline_len();
         newp = alloc(oldlen + 1 + n);
         memset(newp, ZERO, (Unt)(oldlen + 1 + n));
         //copy up to deleted part
         MEMMOVE(newp, oldp, (Unt)bd.textcol);
         newlen = bd.textcol;
         //insert pre-spaces
         memset(newp + newlen, ' ', (Unt)bd.startspaces);
         newlen += bd.startspaces;
         //insert replacement chars CHECK FOR ALLOCATED SPACE
         //REPLACE_CR_NCHAR/REPLACE_NL_NCHAR is used for entering CR literally.
         if (had_ctrl_v_cr || (c != '\r' && c != '\n')) {
            while (--num_chars >= 0)
               newlen += mb_char2bytes(c, newp + newlen);
            if (!bd.is_short) {
                //insert post-spaces
                memset(newp + newlen, ' ', (Unt)bd.endspaces);
                //copy the part after the changed part
                STRCPY(newp + newlen + bd.endspaces,
                  oldp + bd.textcol + bd.textlen);
            }
         } else {
            //Replacing with \r or \n means splitting the line.
            after_p = alloc(oldlen + 1 + n - newlen);
            STRCPY(after_p, oldp + bd.textcol + bd.textlen);
         }

         //replace the line
         ml_replace(curPor->cursor.lnum, newp, false);
         if (after_p != NULL) {
            ml_append(curPor->cursor.lnum++, after_p, 0, false);
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
         int done = false;

         n = gchar_cursor();
         if (n != ZERO) {
            int new_byte_len = mb_char2len(c);
            int old_byte_len = utfCharLen(ml_get_cursor());

            if (new_byte_len > 1 || old_byte_len > 1) {
               //This is slow, but it handles replacing a single-byte
               //with a multi-byte and the other way around.
               if (curPor->cursor.lnum == oper->end.lnum)
                  oper->end.col += new_byte_len - old_byte_len;
               replaceAndMoveBack(c);
               done = true;
            } else {
               if (n == TAB) {
                  int end_vcol = 0;

                  if (curPor->cursor.lnum == oper->end.lnum) {
                      //oper->end has to be recalculated when
                      //the tab breaks
                      end_vcol = getviscol2(oper->end.col,
                                   oper->end.coladd);
                  }
                  coladvance_force(getviscol());
                  if (curPor->cursor.lnum == oper->end.lnum)
                      getvpos(&oper->end, end_vcol);
               }
               //with "coladd" set may move to just after a TAB
               if (gchar_cursor() != ZERO) {
                  PBYTE(curPor->cursor, c);
                  done = true;
               }
            }
          }
         if (!done && virtual_op && curPor->cursor.lnum == oper->end.lnum) {
            int virtcols = oper->end.coladd;

            if (curPor->cursor.lnum == oper->start.lnum
               && oper->start.col == oper->end.col && oper->start.coladd)
                virtcols -= oper->start.coladd;

            //oper->end has been trimmed so it's effectively inclusive;
            //as a result an extra +1 must be counted so we don't trample the ZERO
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

         //Advance to next character, stop at the end of the file.
         if (inc_cursor() == -1)
            break;
      }
   }

   curPor->cursor = oper->start;
   check_cursor();
   doChangedLines(oper->start.lnum, oper->start.col, oper->end.lnum + 1, 0L);

   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
      //Set "'[" and "']" marks.
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
   if (oper->block_mode) {         //Visual block mode
      for (; pos.lnum <= oper->end.lnum; ++pos.lnum) {
         int one_change;

         block_prep(oper, OUT &bd, pos.lnum, false);
         pos.col = bd.textcol;
         one_change = swapchars(oper->opTy, &pos, bd.textlen);
         didChange = didChange || one_change;
      }
      if (didChange)
         doChangedLines(oper->start.lnum, 0, oper->end.lnum + 1, 0L);
   } else {               //not block mode
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
          doChangedLines(oper->start.lnum, oper->start.col, oper->end.lnum + 1, 0L);
      }
   }

   if (!didChange && oper->is_VIsual)
      //No change: need to remove the Visual selection
      drawCurBookLater(UPD_INVERTED);

   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
      //Set '[ and '] marks.
      curBook->opStart = oper->start;
      curBook->opEnd = oper->end;
   }

   smsg(NGETTEXT("%ld line changed", "%ld lines changed", oper->line_count), oper->line_count);
}

//Invoke swapchar() on "length" bytes at position "pos". "pos" is advanced to just after the 
//changed characters. "length" is rounded up to include the whole last multi-byte character.
//Also work correctly when the number of bytes changes. Return true if some character was changed.
private Boole
swapchars(Unt opTy, Pos* pos, int length) {
   int todo;
   Boole didChange = false;

   for (todo = length; todo > 0; --todo) {
      int len = utfCharLen(ml_get_pos(pos));

      //we're counting bytes, not characters
      if (len > 0)
         todo -= len - 1;
      didChange = didChange || swapchar(opTy, pos);
      if (inc(pos) == -1)    //at end of file
         break;
   }
   return didChange;
}

//If opTy == OP_UPPER: make uppercase,
//if opTy == OP_LOWER: make lowercase,
//if opTy == OP_ROT13: do rot13 encoding, else swap case of character at 'pos'.
//Return true when something actually changed.
pub Boole
swapchar(Unt opTy, Pos* pos) {
   Unt c = gchar_pos(pos);

   //Only do rot13 encoding for ASCII characters.
   if (c >= 0x80 && opTy == OP_ROT13)
      return false;

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
         //don't use del_char(), it also removes composing chars
         del_bytes(utf_ptr2len(ml_get_cursor()), false, false);
         insertChar(nc);
         curPor->cursor = sp;
      } else
         PBYTE(*pos, nc);
      return true;
   }
   return false;
}

//op_insert - Insert and append operators for Visual mode.
pub void
op_insert(Operator *oper, long count1) {
   long      pre_textlen = 0;
   ColNr      ind_pre_col = 0, ind_post_col;
   int         ind_pre_vcol = 0, ind_post_vcol = 0;
   BlockDef   bd;
   int         i;
   Pos      t1;
   Pos      start_insert;

   //edit() changes this - record it for OP_APPEND
   bd.is_MAX = (curPor->cursWant == MAXCOL);

   //vis block is still marked. Get rid of it now.
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
      //Get the info about the block before entering the text
      block_prep(oper, OUT &bd, oper->start.lnum, true);
      //Get indent information
      ind_pre_col = (ColNr)getwhitecols_curline();
      ind_pre_vcol = get_indent();
      pre_textlen = ml_get_len(oper->start.lnum) - bd.textcol;
      if (oper->opTy == OP_APPEND)
         pre_textlen -= bd.textlen;
   }

   if (oper->opTy == OP_APPEND) {
      if (oper->block_mode && curPor->cursor.coladd == 0) {
          //Move the cursor to the character right of the block.
          curPor->setCursWant = true;
          while (*ml_get_cursor() != ZERO
             && (curPor->cursor.col < bd.textcol + bd.textlen))
         ++curPor->cursor.col;
          if (bd.is_short && !bd.is_MAX) {
            //First line was too short, make it longer and adjust the
            //values in "bd".
            if (u_save_cursor() == FAIL)
                return;
            for (i = 0; i < bd.endspaces; ++i)
               insertChar(' ');
            bd.textlen += bd.endspaces;
         }
      } else {
          curPor->cursor = oper->end;
          check_cursor_col();

          //Works just like an 'i'nsert on the next character.
          if (!LINEEMPTY(curPor->cursor.lnum)
             && oper->start_vcol != oper->end_vcol)
         inc_cursor();
      }
   }

   t1 = oper->start;
   start_insert = curPor->cursor;
   (void)edit(ZERO, false, (LineNr)count1);

   //When a tab was inserted, and the characters in front of the tab
   //have been converted to a tab as well, the column of the cursor
   //might have actually been reduced, so need to adjust here.
   if (t1.lnum == curBook->opStartOrig.lnum
       && LT_POS(curBook->opStartOrig, t1))
   oper->start = curBook->opStartOrig;

   //If user has moved off this line, we don't know what to do, so do nothing.
   //Also don't repeat the insert when Insert mode ended with CTRL-C.
   if (curPor->cursor.lnum != oper->start.lnum || gotInterruptG)
      return;

   if (oper->block_mode) {
      int ins_len;
      Byte *firstline, *ins_text;
      BlockDef   bd2;
      int did_indent = false;
      Unt len;
      Unt add;
      //offset when cursor was moved in insert mode
      int offset = 0;

      //If indent kicked in, the firstline might have changed.
      //but only do that if the indent actually increased.
      ind_post_col = (ColNr)getwhitecols_curline();
      if (curBook->opStart.col > ind_pre_col && ind_post_col > ind_pre_col) {
          bd.textcol += ind_post_col - ind_pre_col;
          ind_post_vcol = get_indent();
          bd.start_vcol += ind_post_vcol - ind_pre_vcol;
          did_indent = true;
      }

      //The user may have moved the cursor before inserting something, try
      //to adjust the block for that.  But only do it, if the difference
      //does not come from indent kicking in.
      if (oper->start.lnum == curBook->opStartOrig.lnum && !bd.is_MAX && !did_indent) {
          int t = getviscol2(curBook->opStartOrig.col, curBook->opStartOrig.coladd);

         if (oper->opTy == OP_INSERT
             && oper->start.col + oper->start.coladd 
                != curBook->opStartOrig.col + curBook->opStartOrig.coladd
         ) {
            oper->start.col = curBook->opStartOrig.col;
            pre_textlen -= t - oper->start_vcol;
            oper->start_vcol = t;
         } ei (oper->opTy == OP_APPEND
             && oper->start.col + oper->start.coladd
                >= curBook->opStartOrig.col + curBook->opStartOrig.coladd
         ) {
            oper->start.col = curBook->opStartOrig.col;
            //reset pre_textlen to the value of OP_INSERT
            pre_textlen += bd.textlen;
            pre_textlen -= t - oper->start_vcol;
            oper->start_vcol = t;
            oper->opTy = OP_INSERT;
         }
      }

      //Spaces and tabs in the indent may have changed to other spaces and tabs. Get the 
      //starting column again and correct the length.
      //Don't do this when "$" used, end-of-line will have changed.
      //
      //if indent was added and the inserted text was after the indent,
      //correct the selection for the new indent.
      if (did_indent && bd.textcol - ind_post_col > 0) {
          oper->start.col += ind_post_col - ind_pre_col;
          oper->start_vcol += ind_post_vcol - ind_pre_vcol;
          oper->end.col += ind_post_col - ind_pre_col;
          oper->end_vcol += ind_post_vcol - ind_pre_vcol;
      }
      block_prep(oper, OUT &bd2, oper->start.lnum, true);
      if (did_indent && bd.textcol - ind_post_col > 0) {
          //undo for where "oper" is used below
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

      //Subsequent calls to ml_get() flush the firstline data - take a
      //copy of the required string.
      firstline = ml_get(oper->start.lnum);
      len = ml_get_len(oper->start.lnum);
      add = bd.textcol;
      if (oper->opTy == OP_APPEND) {
          add += bd.textlen;
          //account for pressing cursor in insert mode when '$' was used
         if (bd.is_MAX
            && (start_insert.lnum == insertStartG.lnum && start_insert.col > insertStartG.col)
         ) {
            offset = (start_insert.col - insertStartG.col);
            add -= offset;
            if (oper->end_vcol > offset)
                oper->end_vcol -= (offset + 1);
            else
                //moved outside of the visual block, what to do?
                return;
         }
      }
      if (add > len)
          add = len;  //short line, point to the ZERO
      firstline += add;
      len -= add;
      if (pre_textlen >= 0 && (ins_len = (int)len - pre_textlen - offset) > 0) {
         ins_text = copySubstr(firstline, ins_len);
         //block handled here
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
//return true if edit() returns because of a CTRL-O command
pub int
op_change(Operator *oper) {
   LineNr      linenr;
   long      pre_textlen = 0;
   long      pre_indent = 0;
   Byte      *firstline;
   Byte      *ins_text, *newp, *oldp;
   BlockDef   bd;

   ColNr l = oper->start.col;
   if (oper->motion_type == MLINE) {
      l = 0;
      can_si = may_do_si();   //Like opening a new line, do smart indent
   }

   //First delete the text in the region. In an empty book only need to save for undo
   if (curBook->mem.flags & ML_EMPTY) {
      if (u_save_cursor() == FAIL)
         return false;
   } ei (op_delete(oper) == FAIL)
      return false;

   if ((l > curPor->cursor.col) && !LINEEMPTY(curPor->cursor.lnum) && !virtual_op)
      inc_cursor();

   //check for still on same line (<CR> in inserted text meaningless) skip blank lines too
   if (oper->block_mode) {
      //Add spaces before getting the current line length.
      if (virtual_op && (curPor->cursor.coladd > 0 || gchar_cursor() == ZERO))
          coladvance_force(getviscol());
      firstline = ml_get(oper->start.lnum);
      pre_textlen = ml_get_len(oper->start.lnum);
      pre_indent = (long)getwhitecols(firstline);
      bd.textcol = curPor->cursor.col;
   }

   if (oper->motion_type == MLINE)
      fix_indent();

    //Reset finish_op now, don't want it set inside edit().
    int save_finish_op = finish_op;
    finish_op = false;

    int retval = edit(ZERO, false, (LineNr)1);

    finish_op = save_finish_op;

   //In Visual block mode, handle copying the new text to all lines of the block.
   //Don't repeat the insert when Insert mode ended with CTRL-C.
   if (oper->block_mode && oper->start.lnum != oper->end.lnum && !gotInterruptG) {
      //Auto-indenting may have changed the indent.  If the cursor was past
      //the indent, exclude that indent change from the inserted text.
      firstline = ml_get(oper->start.lnum);
      if (bd.textcol > (ColNr)pre_indent) {
         long new_indent = (long)getwhitecols(firstline);

         pre_textlen += new_indent - pre_indent;
         bd.textcol += new_indent - pre_indent;
      }

      int ins_len = (int)ml_get_len(oper->start.lnum) - pre_textlen;
      if (ins_len > 0) {
         //Subsequent calls to ml_get() flush the firstline data - take a
         //copy of the inserted text.
         if ((ins_text = alloc(ins_len + 1)) != NULL) {
            copySubstrToAllocation(ins_text, (Text){firstline + bd.textcol, ins_len});
            for (linenr = oper->start.lnum + 1; linenr <= oper->end.lnum; linenr++) {
               block_prep(oper, OUT &bd, linenr, true);
               if (!bd.is_short || virtual_op) {
                  Pos vpos;
                  Unt newlen;

                  //If the block starts in virtual space, count the
                  //initial coladd offset as part of "startspaces"
                  if (bd.is_short) {
                     vpos.lnum = linenr;
                     (void)getvpos(&vpos, oper->start_vcol);
                  } else
                     vpos.coladd = 0;
                  oldp = ml_get(linenr);
                  newp = alloc(ml_get_len(linenr) + vpos.coladd + ins_len + 1);
                  //copy up to block start
                  MEMMOVE(newp, oldp, (Unt)bd.textcol);
                  newlen = bd.textcol;
                  memset(newp + newlen, ' ', (Unt)vpos.coladd);
                  newlen += vpos.coladd;
                  MEMMOVE(newp + newlen, ins_text, ins_len);
                  STRCPY(newp + newlen + ins_len, oldp + bd.textcol);
                  ml_replace(linenr, newp, false);
                  //Shift the properties for linenr as edit() would do.
                  if (curBook->hasTextprop)
                     adjustPropColumns(linenr, bd.textcol, vpos.coladd + (int)ins_len, 0);
               }
            }
            check_cursor();

            doChangedLines(oper->start.lnum + 1, 0, oper->end.lnum + 1, 0L);
         }
         eeglFree(ins_text);
      }
   }
   auto_format(false, true);

   return retval;
}

//When the cursor is on the ZERO past the end of the line and it should not be
//there, move it left.
pub void
adjust_cursor_eol(void) {
   int adj_cursor = (curPor->cursor.col > 0
            && gchar_cursor() == ZERO
            && !(restart_edit || (stateG & MODE_INSERT)));
   if (!adj_cursor)
      return;

    //Put the cursor on the last character in the line.
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
   Byte part_buf[COM_MAX_LEN];   //buffer for one option part

   //Repeat to match several nested comment strings.
   i = (int)STRLEN(line);
   while (--i >= lower_check_bound) {
      //scan through the @comments option for a match
      foundOne = false;
      for (list = curBook->o.comments; *list; ) {
         CS flags_save = list;

         //Get one option part into part_buf[].  Advance list to next one.
         //put string at start of string.
         (void)strCutPathFromListOfPaths(OUT &list, OUT part_buf, COM_MAX_LEN, S",");
         string = firstOccurrence(part_buf, ':');
         if (!string)   //If everything is fine, this cannot actually happen.
            continue;
         *string++ = ZERO;   //Isolate flags from string.
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
         Byte part_buf2[COM_MAX_LEN];   //buffer for one option part
         int len1, len2, off;

         result = i;
         //If this comment nests, continue searching.
         if (firstOccurrence(part_buf, COM_NEST) != NULL)
            continue;

         lower_check_bound = i;

         //Let's verify whether the comment leader found is a substring
         //of other comment leaders. If it is, let's adjust the
         //lower_check_bound so that we make sure that we have determined
         //the comment leader correctly.

         while (SPACE_OR_TAB(*com_leader))
            ++com_leader;
         len1 = (int)STRLEN(com_leader);

         for (list = curBook->o.comments; *list; ) {
            CS flags_save = list;

            (void)strCutPathFromListOfPaths(OUT &list, OUT part_buf2, COM_MAX_LEN, S",");
            if (flags_save == com_flags)
               continue;
            string = firstOccurrence(part_buf2, ':');
            ++string;
            while (SPACE_OR_TAB(*string))
               ++string;
            len2 = (int)STRLEN(string);
            if (len2 == 0)
               continue;

            //Now we have to verify whether string ends with a substring
            //beginning the com_leader.
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

//If "process" is true and the line begins with a comment leader (possibly
//after some white space), return a pointer to the text after it. Put a boolean
//value indicating whether the line ends with an unclosed comment in "is_comment".
//line - line to be processed,
//process - if false, will only check whether the line ends with an unclosed comment,
//include_space - whether to also skip space following the comment leader,
//is_comment - will indicate whether the current line ends with an unclosed comment.
pub CS
skip_comment(CS line, Boole process, Boole include_space, OUT Boole* is_comment) {
   CS comment_flags = NULL;
   int    lead_len;
   int    leader_offset = get_last_leader_offset(line, &comment_flags);

   *is_comment = false;
   if (leader_offset != -1) {
      //Let's check whether the line ends with an unclosed comment.
      //If the last comment leader has COM_END in flags, there's no comment.
      while (*comment_flags) {
         if (*comment_flags == COM_END || *comment_flags == ':')
            break;
         ++comment_flags;
      }
      if (*comment_flags != COM_END)
         *is_comment = true;
   }

   if (process == false)
      return line;

   lead_len = get_leader_len(line, &comment_flags, false, include_space);

   if (lead_len == 0)
      return line;

   //Find:
   //- COM_END,
   //- colon,
   //whichever comes first.
   while (*comment_flags) {
      if (*comment_flags == COM_END || *comment_flags == ':')
         break;
      ++comment_flags;
   }

   //If we found a colon, it means that we are not processing a line
   //starting with a closing part of a three-part comment. That's good,
   //because we don't want to remove those as this would be annoying.
   if (*comment_flags == ':' || *comment_flags == ZERO)
      line += lead_len;

   return line;
}

//Join 'count' lines (minimal 2) at the cursor position.
//When "save_undo" is true save lines for undo first.
//Set "use_formatoptions" to false when e.g. processing backspace and comment
//leaders should not be removed.
//When setmark is true, sets the '[ and '] mark, else, the caller is expected
//to set those marks.
//
//return FAIL for failure, OK otherwise
pub int
doJoinLinesUnderCursor(
   long count,
   Boole insert_space,
   Boole save_undo,
   Boole use_formatoptions,
   Boole setmark
) {
   CS curr = NULL;
   CS curr_start = NULL;
   CS cend;
   int endcurr1 = ZERO;
   int endcurr2 = ZERO;
   int currsize = 0;   //size of the current line
   int sumsize = 0;   //size of the long new line
   LineNr t;
   ColNr col = 0;
   int ret = OK;
   int* comments = NULL;
   int remove_comments = (use_formatoptions == true) && has_format_option(FO_REMOVE_COMS);
   int propcount = 0;   //number of props over all joined lines
   int props_remaining;

   if (save_undo && u_save((LineNr)(curPor->cursor.lnum - 1),
             (LineNr)(curPor->cursor.lnum + count)) == FAIL
   )
      return FAIL;

   //Allocate an array to store the number of spaces inserted before each
   //line.  We will use it to pre-compute the length of the new line and the
   //proper placement of each original line in the new one.
   CS spaces = lallocZeroed(count, true);
   if (remove_comments) {
      comments = lallocZeroed(count * sizeof(int), true);
      if (!comments) {
          eeglFree(spaces);
          return FAIL;
      }
   }

   //Don't move anything yet, just compute the final line length
   //and setup the array of space strings lengths. This loops forward over the joined lines.
   Boole prev_was_comment;
   for (t = 0; t < count; ++t) {
      curr = curr_start = ml_get((LineNr)(curPor->cursor.lnum + t));
      propcount += count_props((LineNr) (curPor->cursor.lnum + t), t > 0, t + 1 == count);
      if (t == 0 && setmark && (commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
         //Set the '[ mark.
         curPor->book->opStart.lnum = curPor->cursor.lnum;
         curPor->book->opStart.col  = (ColNr)STRLEN(curr);
      }
      if (remove_comments) {
         //We don't want to remove the comment leader if the previous line is not a comment.
         if (t > 0 && prev_was_comment) {
            CS new_curr = skip_comment(curr, true, insert_space, OUT &prev_was_comment);
            comments[t] = (int)(new_curr - curr);
            curr = new_curr;
         } else
            curr = skip_comment(curr, false, insert_space, OUT &prev_was_comment);
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
            //don't add a space if the line is ending in a space
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

   //store the column position before last line
   col = sumsize - currsize - spaces[count - 1];

   //allocate the space for the new line
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
      MEMMOVE(cend, curr, (Unt)currsize);

      if (spaces[t] > 0) {
         cend -= spaces[t];
         memset(cend, ' ', (Unt)(spaces[t]));
      }

      //If deleting more spaces than adding, the cursor moves no more than
      //what is added if it is inside these spaces.
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

   ml_replace_len(curPor->cursor.lnum, newp, (ColNr)newp_len, true, false);

   if (setmark && (commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
      //Set the '] mark.
      curPor->book->opEnd.lnum = curPor->cursor.lnum;
      curPor->book->opEnd.col  = (ColNr)sumsize;
   }

   //Only report the change in the first line here, del_lines() will report
   //the deleted line.
   doChangedLines(curPor->cursor.lnum, currsize, curPor->cursor.lnum + 1, 0L);
   //Delete following lines. To do this we move the cursor there
   //briefly, and then move it back. After del_lines() the cursor may
   //have moved up (last line deleted), so the current lnum is kept in t.
   t = curPor->cursor.lnum;
   ++curPor->cursor.lnum;
   del_lines(count - 1, false);
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
// first/last deleted char minus the number of columns that have to be deleted.
//for yank and tilde:
//- textlen includes the first/last char to be wholly yanked
//- start/endspaces is the number of columns of the first/last yanked char that are to be yanked.
pub void
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
      //Avoid a problem with unwanted linebreaks in block mode.

   bdp->startspaces = 0;
   bdp->endspaces = 0;
   bdp->textlen = 0;
   bdp->start_vcol = 0;
   bdp->end_vcol = 0;
   bdp->is_short = false;
   bdp->is_oneChar = false;
   bdp->pre_whitesp = 0;
   bdp->pre_whitesp_c = 0;
   bdp->end_char_vcols = 0;
   bdp->start_char_vcols = 0;

   CS line = ml_get(lnum);
   CS prev_pstart = line;
   bookInitCharsForKeywordsSizeArg(&cts, curPor, lnum, bdp->start_vcol, line, line);
   while (cts.cts_vcol < oper->start_vcol && *cts.cts_ptr != ZERO) {
   //Count a tab for what it's worth (if list mode not on)
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
   if (bdp->start_vcol < oper->start_vcol) {  //line too short
      bdp->end_vcol = bdp->start_vcol;
      bdp->is_short = true;
      if (!is_del || oper->opTy == OP_APPEND)
         bdp->endspaces = oper->end_vcol - oper->start_vcol + 1;
   } else {
      //notice: this converts partly selected Multibyte characters to spaces, too.
      bdp->startspaces = bdp->start_vcol - oper->start_vcol;
      if (is_del && bdp->startspaces)
          bdp->startspaces = bdp->start_char_vcols - bdp->startspaces;
      pend = pstart;
      bdp->end_vcol = bdp->start_vcol;
      if (bdp->end_vcol > oper->end_vcol) {  //it's all in one character
         bdp->is_oneChar = true;
         if (oper->opTy == OP_INSERT)
            bdp->endspaces = bdp->start_char_vcols - bdp->startspaces;
         ei (oper->opTy == OP_APPEND) {
            bdp->startspaces += oper->end_vcol - oper->start_vcol + 1;
            bdp->endspaces = bdp->start_char_vcols - bdp->startspaces;
         } else {
            bdp->startspaces = oper->end_vcol - oper->start_vcol + 1;
            if (is_del && oper->opTy != OP_LSHIFT) {
               //just putting the sum of those two into bdp->startspaces doesn't work for Visual 
               //replace, so we have to split the tab in two
               bdp->startspaces = bdp->start_char_vcols - (bdp->start_vcol - oper->start_vcol);
               bdp->endspaces = bdp->end_vcol - oper->end_vcol - 1;
            }
         }
      } else {
         bookInitCharsForKeywordsSizeArg(&cts, curPor, lnum, bdp->end_vcol, line, pend);
         prev_pend = pend;
         while (cts.cts_vcol <= oper->end_vcol && *cts.cts_ptr != ZERO) {
            //count a tab for what it's worth (if list mode not on)
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
               || oper->opTy == OP_REPLACE) //line too short
         ){
            bdp->is_short = true;
            //Alternative: include spaces to fill up the block. Disadvantage: can lead to 
            //trailing spaces when the line is short where the text is put
            //if (!is_del || oper->opTy == OP_APPEND)
            if (oper->opTy == OP_APPEND || virtual_op)
                bdp->endspaces = oper->end_vcol - bdp->end_vcol
                                + oper->inclusive;
            else
                bdp->endspaces = 0; //replace doesn't add characters
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
pub void
doCharwiseBlockPrep(
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
   bdp->is_oneChar = false;
   bdp->start_char_vcols = 0;

   if (lnum == start.lnum) {
      startcol = start.col;
      if (virtual_op) {
         getvcol(curPor, &start, &cs, NULL, &ce);
         if (ce != cs && start.coladd > 0) {
            //Part of a tab selected -- but don't double-count it.
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
            //Don't add space for double-wide char; endcol will be on last byte of multi-byte char
            && (*mb_head_off)(p, p + endcol) == 0))
          {
         if (start.lnum == end.lnum && start.col == end.col) {
             //Special case: inside a single char
             bdp->is_oneChar = true;
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
pub void
op_addsub(
   Operator* oper,
   LineNr prenum1,       //Amount of add/subtract
   int g_cmd          //was g<c-a>/g<c-x>
){
   Pos pos;
   BlockDef bd;
   int change_cnt = 0;
   LineNr amount = prenum1;

   //do_addsub() might trigger re-evaluation of 'foldexpr' halfway, when the
   //book is not completely updated yet. Postpone updating folds until before
   //the call to doChangedLines().
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
         doChangedLines(pos.lnum, 0, pos.lnum + 1, 0L);
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
         if (oper->block_mode) {         //Visual block mode
            block_prep(oper, OUT &bd, pos.lnum, false);
            pos.col = bd.textcol;
            length = bd.textlen;
         } ei (oper->motion_type == MLINE) {
            curPor->cursor.col = 0;
            pos.col = 0;
            length = ml_get_len(pos.lnum);
         } else {//oper->motion_type == MCHAR
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
            //Remember the start position of the first change.
            if (change_cnt == 0)
               startpos = curBook->opStart;
            ++change_cnt;
         }

         if (g_cmd && one_change)
            amount += prenum1;
      }

      disable_fold_update--;
      if (change_cnt)
         doChangedLines(oper->start.lnum, 0, oper->end.lnum + 1, 0L);

      if (!change_cnt && oper->is_VIsual)
         //No change: need to remove the Visual selection
         drawCurBookLater(UPD_INVERTED);

      //Set '[ mark if something changed. Keep the last end
      //position from do_addsub().
      if (change_cnt > 0 && (commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0)
         curBook->opStart = startpos;

      smsg(NGETTEXT("%d line changed", "%d lines changed", change_cnt), change_cnt);
   }
}

//Add or subtract 'prenum1' from a number in a line opTy is OP_ADD or OP_SUB
//Return true if some character was changed.
private int
do_addsub(
   int opTy,
   Pos* pos,
   int length,
   LineNr prenum1
){
   int      col;
   int      pre;      //'X'/'x': hex; 'B'/'b': bin
   Ulong   n;
   Ulong   oldn;
   Byte   *ptr;
   int      linelen;
   int      c;
   int      todel;
   int      firstdigit;
   int      subtract;
   int      negative = false;
   int      was_positive = true;
   int      visual = VIsual_active;
   int      didChange = false;
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
         //Found hexadecimal or binary number, move to its start.
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
      inpFlushIfNotSilent();
      goto theend;
   }

   if (ASCII_ISALPHA(firstdigit)) {
      //decrement or increment alphabetic character
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
      didChange = true;
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

      //get the number value (unsigned)
      if (visual && VIsual_mode != 'V')
          maxlen = (curBook->visual.vi_curswant == MAXCOL ? linelen - col : length);

      Boole overflow = false;
      readLongNumber(
         ptr + col, &pre, &length, 0 + STR2NR_HEX, NULL, &n, maxlen, false, OUT &overflow
      );

      //ignore leading '-' for hex and bin numbers
      if (pre && negative) {
          ++col;
          --length;
          negative = false;
      }
      //add or subtract
      subtract = false;
      if (opTy == OP_SUB)
          subtract ^= true;
      if (negative)
          subtract ^= true;

      oldn = n;
      if (!overflow) { //if number is too big don't add/subtract
         if (subtract)
            n -= (Ulong)prenum1;
         else
            n += (Ulong)prenum1;
      }

      //handle wraparound for decimal numbers
      if (!pre) {
         if (subtract) {
            if (n > oldn) {
               n = 1 + (n ^ (Ulong)-1);
               negative ^= true;
            }
         } else {
            //add
            if (n < oldn) {
               n = (n ^ (Ulong)-1);
               negative ^= true;
            }
         }
         if (n == 0)
            negative = false;
      }

      if (subtract)
         //sticking at zero.
         n = (Ulong)0;
      else
         //sticking at 2^64 - 1.
         n = (Ulong)(-1);
      negative = false;

      if (visual && !was_positive && !negative && col > 0) {
         //need to remove the '-'
         col--;
         length++;
      }

      //Delete the old number.
      curPor->cursor.col = col;
      if (!didChange)
         startpos = curPor->cursor;
      didChange = true;
      todel = length;
      c = gchar_cursor();
      //Don't include the '-' in the length, only the length of the part after it is kept the same
      if (c == '-')
         --length;

      save_pos = curPor->cursor;
      for (i = 0; i < todel; ++i) {
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

      //Put the number characters in buf2[].
      if (pre == 'b' || pre == 'B') {
         int bit = 0;
         int bits = sizeof(Ulong) * 8;

         //leading zeros
         for (bit = bits; bit > 0; bit--) {
            if ((n >> (bit - 1)) & 0x1) 
               break;
         } 

         for (buf2len = 0; bit > 0 && buf2len < (NUMBUFLEN - 1); bit--)
            buf2[buf2len++] = ((n >> (bit - 1)) & 0x1) ? '1' : '0';

         buf2[buf2len] = ZERO;
      } ei (pre == 0)
         buf2len = eeSnprintf(buf2, NUMBUFLEN, "%" PRIu64, n);
      else
         buf2len = eeSnprintf(buf2, NUMBUFLEN, "%" PRIx64, n);
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

      //Insert just after the first character to be removed, so that any
      //text properties will be adjusted.  Then delete the old number afterwards.
      save_pos = curPor->cursor;
      if (todel > 0)
          inc_cursor();
      ins_str(buf1, (Unt)buf1len);      //insert the new number
      eeglFree(buf1);

      //del_char() will also mark line needing displaying
      if (todel > 0) {
          int bytes_after = ml_get_curline_len() - curPor->cursor.col;

          //Delete the one character before the insert.
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
      //set the '[ and '] marks
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

pub void
doClearOpArg(Operator *oper) {
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

    //Add eol_size if the end of line was reached before hitting limit.
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
pub void
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

   //Compute the length of the file in characters.
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

            //Make @showbreak empty for a moment to get the correct size.
            p_sbr = null;
            oparg.is_VIsual = 1;
            oparg.block_mode = true;
            oparg.opTy = OP_NOP;
            getvcols(curPor, &min_pos, &max_pos, &oparg.start_vcol, &oparg.end_vcol);
            p_sbr = saved_sbr;
            if (curPor->cursWant == MAXCOL)
                oparg.end_vcol = MAXCOL;
            //Swap the start, end vcol if needed
            if (oparg.end_vcol < oparg.start_vcol) {
                oparg.end_vcol += oparg.start_vcol;
                oparg.start_vcol = oparg.end_vcol - oparg.start_vcol;
                oparg.end_vcol -= oparg.start_vcol;
            }
         }
         line_count_selected = max_pos.lnum - min_pos.lnum + 1;
      }

      for (lnum = 1; lnum <= curBook->mem.lineCount; ++lnum) {
         //Check for a CTRL-C every 100000 characters.
         if (byte_count > last_check) {
            ui_breakcheck();
            if (gotInterruptG)
               return;
            last_check = byte_count + 100000L;
         }

         //Do extra processing for VIsual mode.
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
            //In non-visual mode, check for the line the cursor is on
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
         //Add to the running totals
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
      //Make the range look nice, so it can be repeated.
      if (oper->start.lnum == curPor->cursor.lnum)
         stuffcharReadbuff('.');
      else
         stuffnumReadbuff((long)oper->start.lnum);

      //When using !! on a closed fold the range ".!" works best to operate
      //on, it will be made the whole closed fold later.
      LineNr endOfStartFold = oper->start.lnum;
      (void)getFolds(oper->start.lnum, NULL, OUT &endOfStartFold);
      if (oper->end.lnum != oper->start.lnum && oper->end.lnum != endOfStartFold) {
         //Make it a range with the end line.
         stuffcharReadbuff(',');
         if (oper->end.lnum == curPor->cursor.lnum)
            stuffcharReadbuff('.');
         ei (oper->end.lnum == curBook->mem.lineCount)
            stuffcharReadbuff('$');
         ei (oper->start.lnum == curPor->cursor.lnum
             //do not use ".+number" for a closed fold, it would count folded lines twice
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

   //doCommand() does the rest
}

//callback function for 'operatorfunc'
private Callback opfunc_cb;

//Process the 'operatorfunc' option value. Return OK or FAIL.
pub CS
did_set_operatorfunc(OptionChange *cha) {
   if (optSetCallback(OUT &opfunc_cb, cha->newVal.string) == FAIL)
      return e_invalid_argument;

   return NULL;
}

#if defined(EXITFREE)
pub void
opsFreeOperatorFnOption(void) {
   evFreeCallback(&opfunc_cb);
}
#endif

//Mark the global 'operatorfunc' callback with "copyID" so that it is not garbage collected.
pub int
set_ref_in_opfunc(int copyID) {
   return memSetRefInCallback(&opfunc_cb, copyID);
}

//Handle the "g@" operator: call 'operatorfunc'.
private void
op_function(Operator* oper) {
   Var argv[2];
   Pos orig_start = curBook->opStart;
   Pos orig_end = curBook->opEnd;
   Var returnVar;

   if (!p_opfunc)
      emsg(_(e_operatorfunc_is_empty));
   else {
      //Set '[ and '] marks to text to be operated on.
      curBook->opStart = oper->start;
      curBook->opEnd = oper->end;
      if (oper->motion_type != MLINE && !oper->inclusive)
          //Exclude the end position.
          decl(&curBook->opEnd);

      argv[0].tag = VAR_STRING;
      if (oper->block_mode)
          argv[0].string = (CS)"block";
      ei (oper->motion_type == MLINE)
          argv[0].string = (CS)"line";
      else
          argv[0].string = (CS)"char";
      argv[1].tag = VAR_UNKNOWN;

      //Reset virtual_op so that 'virtualedit' can be changed in the
      //function.
      int save_virtual_op = virtual_op;
      virtual_op = MAYBE;

      //Reset finish_op so that mode() returns the right value.
      int save_finish_op = finish_op;
      finish_op = false;

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

   oper->block_mode = true;

   //prevent from moving onto a trail byte
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

   //if '$' was used, get oper->end_vcol from longest line
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
   //Correct oper->end.col and oper->start.col to be the
   //upper-left and lower-right corner of the block area.
   //
   //(Actually, this does convert column positions into character positions)
   curPor->cursor.lnum = oper->end.lnum;
   coladvance(oper->end_vcol);
   oper->end = curPor->cursor;

   curPor->cursor = oper->start;
   coladvance(oper->start_vcol);
   oper->start = curPor->cursor;
}

//Information for redoing the previous Visual selection.
typedef struct {
   int mode;   //'v', 'V', or Ctrl-V
   LineNr lineCount;   //number of lines
   ColNr vcol;   //number of cols or end column
   long count;   //count for Visual operator
   int extraArg;      //extra argument
} RedoVisual;

private int
isCommandModeChar(ActionArg* aArg) {
   return aArg->cmdchar == ':' || aArg->cmdchar == K_COMMAND || aArg->cmdchar == K_SCRIPT_COMMAND;
}

//Handle an operator after Visual mode or when the movement is finished.
//"clipbYank" is true when yanking text for the clipboard.
pub void
doExecuteVisualOperator(ActionArg* aArg, int old_col, int clipbYank) {
   Operator* oper = aArg->oper;
   Pos old_cursor;
   int restart_edit_save;

   //The visual area is remembered for redo
   static RedoVisual redo_VIsual = {ZERO, 0, 0, 0,0};

   old_cursor = curPor->cursor;

   //If an operation is pending, handle it...
   if ((finish_op || VIsual_active) && oper->opTy != OP_NOP) {
      //Avoid a problem with unwanted linebreaks in block mode.
      oper->is_VIsual = VIsual_active;
      if (oper->motion_force == 'V')
         oper->motion_type = MLINE;
      ei (oper->motion_force == 'v') {
         //If the motion was linewise, "inclusive" will not have been set.
         //Use "exclusive" to be consistent.  Makes "dvj" work nice.
         if (oper->motion_type == MLINE)
            oper->inclusive = false;
         //If the motion already was characterwise, toggle "inclusive"
         ei (oper->motion_type == MCHAR)
            oper->inclusive = !oper->inclusive;
         oper->motion_type = MCHAR;
      } ei (oper->motion_force == Ctrl_V) {
         //Change line- or characterwise motion into Visual block mode.
         if (!VIsual_active) {
            VIsual_active = true;
            VIsual = oper->start;
         }
         VIsual_mode = Ctrl_V;
         VIsual_reselect = false;
      }

      //Never redo yank. Never redo "zf" (define fold).
      if (oper->opTy != OP_YANK
         && ((!VIsual_active || oper->motion_force)
             //Also redo Operator-pending Visual mode mappings
             || (VIsual_active && isCommandModeChar(aArg) && oper->opTy != OP_COLON))
         && aArg->cmdchar != 'D'
         && oper->opTy != OP_FOLD
         && oper->opTy != OP_FOLDOPEN
         && oper->opTy != OP_FOLDOPENREC
         && oper->opTy != OP_FOLDCLOSE
         && oper->opTy != OP_FOLDCLOSEREC
         && oper->opTy != OP_FOLDDEL
         && oper->opTy != OP_FOLDDELREC
      ) {
         prep_redo(oper->regname, aArg->count0,
             get_op_char(oper->opTy), get_extra_op_char(oper->opTy),
             oper->motion_force, aArg->cmdchar, aArg->nchar);
         if (aArg->cmdchar == '/' || aArg->cmdchar == '?') {//was a search
            //Insert the search pattern to really repeat the same command.
            inpAppendLitToRedoBuff(aArg->searchbuf, -1);
            inpAppendToRedoBuff(NL_STR);
         } ei (isCommandModeChar(aArg)) {
            //doCommand() has stored the first typed line in "repeatCommlineG". When several lines 
            //are typed repeating won't be possible.
            if (repeatCommlineG == NULL)
                ResetRedobuff();
            else {
               if (aArg->cmdchar == ':')
                  inpAppendLitToRedoBuff(repeatCommlineG, -1);
               else
                  inpAppendSpecToRedoBuff(repeatCommlineG);
               inpAppendToRedoBuff(NL_STR);
               EE_CLEAR(repeatCommlineG);
            }
         }
      }

      if (isRedoVisualBusy) {
         //Redo of an operation on a Visual area. Use the same size from
         //redo_VIsual.lineCount and redo_VIsual.vcol.
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
         aArg->count0 = redo_VIsual.count;
         if (redo_VIsual.count != 0)
            aArg->count1 = redo_VIsual.count;
         else
            aArg->count1 = 1;
      } ei (VIsual_active) {
         if (!clipbYank) {
            //Save the current VIsual area for '< and '> marks, and "gv"
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

      //Set oper->start to the first position of the operated text, oper->end
      //to the end of the operated text. cursor is equal to oper->start.
      if (LT_POS(oper->start, curPor->cursor)) {
         //Include folded lines completely.
         if (!VIsual_active) {
            if (getFolds(oper->start.lnum, OUT &oper->start.lnum, NULL))
               oper->start.col = 0;
            if ((curPor->cursor.col > 0 || oper->inclusive || oper->motion_type == MLINE)
                  && getFolds(curPor->cursor.lnum, NULL, OUT &curPor->cursor.lnum)
            )
               curPor->cursor.col = ml_get_curline_len();
         }
         oper->end = curPor->cursor;
         curPor->cursor = oper->start;

         //virtCol may have been updated; if the cursor goes back to its
         //previous position virtCol becomes invalid and isn't updated automatically.
         curPor->cacheState &= ~VALID_VIRTCOL;
      } else {
         //Include folded lines completely.
         if (!VIsual_active && oper->motion_type == MLINE) {
            if (getFolds(curPor->cursor.lnum, OUT &curPor->cursor.lnum, NULL))
               curPor->cursor.col = 0;
            if (getFolds(oper->start.lnum, NULL, OUT &oper->start.lnum))
               oper->start.col = ml_get_len(oper->start.lnum);
         }
         oper->end = oper->start;
         oper->start = curPor->cursor;
      }

      //Just in case lines were deleted that make the position invalid.
      check_pos(curPor->book, &oper->end);
      oper->line_count = oper->end.lnum - oper->start.lnum + 1;

      //Set "virtual_op" before resetting VIsual_active.
      virtual_op = virtual_active();

      if (VIsual_active || isRedoVisualBusy) {
         get_op_vcol(oper, redo_VIsual.vcol, true);

         if (!isRedoVisualBusy && !clipbYank) {
            //Prepare to reselect and redo Visual: this is based on the size of the Visual text
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

         //can't redo yank and ":"
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
            //Prepare for redoing.  Only use the nchar field for "r",
            //otherwise it might be the second char of the operator.
            if (aArg->cmdchar == 'g' && (aArg->nchar == 'n' || aArg->nchar == 'N'))
                prep_redo(oper->regname, aArg->count0,
                   get_op_char(oper->opTy),
                   get_extra_op_char(oper->opTy),
                   oper->motion_force, aArg->cmdchar, aArg->nchar);
            ei (!isCommandModeChar(aArg)) {
               int opchar = get_op_char(oper->opTy);
               int extra_opchar = get_extra_op_char(oper->opTy);
               Unt nchar = oper->opTy == OP_REPLACE ? aArg->nchar : ZERO;

               //reverse what nv_replace() did
               if (nchar == REPLACE_CR_NCHAR)
                  nchar = ENTER;
               ei (nchar == REPLACE_NL_NCHAR)
                  nchar = NL;

               if (opchar == 'g' && extra_opchar == '@')
                  //also repeat the count for 'operatorfunc'
                  prep_redo_num2(oper->regname, 0L, ZERO, 'v',
                          aArg->count0, opchar, extra_opchar, nchar);
               else
                  prep_redo(oper->regname, 0L, ZERO, 'v', opchar, extra_opchar, nchar);
            }
            if (!isRedoVisualBusy) {
               redo_VIsual.mode = resel_VIsual_mode;
               redo_VIsual.vcol = resel_VIsual_vcol;
               redo_VIsual.lineCount = resel_VIsual_line_count;
               redo_VIsual.count = aArg->count0;
               redo_VIsual.extraArg = aArg->arg;
            }
         }

         //oper->inclusive defaults to true.
         //If oper->end is on a ZERO (empty line) oper->inclusive becomes
         //false.  This makes "d}P" and "v}dP" work the same.
         if (oper->motion_force == ZERO || oper->motion_type == MLINE)
            oper->inclusive = true;
         if (VIsual_mode == 'V')
            oper->motion_type = MLINE;
         else {
            oper->motion_type = MCHAR;
            if (VIsual_mode != Ctrl_V && *ml_get_pos(&(oper->end)) == ZERO && (!virtual_op)) {
               oper->inclusive = false;
               //Try to include the newline, unless it's an operator that works on lines only.
               if (!op_on_lines(oper->opTy) && oper->end.lnum < curBook->mem.lineCount) {
                  ++oper->end.lnum;
                  oper->end.col = 0;
                  oper->end.coladd = 0;
                  ++oper->line_count;
               }
            }
         }

         isRedoVisualBusy = false;

         //Switch Visual off now, so screen updating does not show inverted text when the screen 
         //is redrawn. With OP_YANK and sometimes with OP_COLON and OP_FILTER there is
         //no screen redraw, so it is done here to remove the inverted part.
         if (!clipbYank) {
            VIsual_active = false;
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

      //Include the trailing byte of a multi-byte char.
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
      //oper->inclusive is false, we put op_end after the last character in the previous line. If 
      //op_start is on or before the first non-blank in the line, the operator becomes linewise
      //(strange, but that's the way vi does it).
      if (  oper->motion_type == MCHAR
         && oper->inclusive == false
         && !(aArg->retval & CA_NO_ADJ_OP_END)
         && oper->end.col == 0
         && (!oper->is_VIsual)
         && !oper->block_mode
         && oper->line_count > 1
      ) {
         oper->end_adjusted = true;       //remember that we did this
         --oper->line_count;
         --oper->end.lnum;
         if (inindent(0))
            oper->motion_type = MLINE;
         else {
            oper->end.col = ml_get_len(oper->end.lnum);
            if (oper->end.col) {
               --oper->end.col;
               oper->inclusive = true;
            }
         }
      } else
         oper->end_adjusted = false;

      switch (oper->opTy) {
      case OP_LSHIFT:
      case OP_RSHIFT:
         op_shift(oper, true, oper->is_VIsual ? (int)aArg->count1 : 1);
         auto_format(false, true);
         break;

      case OP_JOIN_NS:
      case OP_JOIN:
         if (oper->line_count < 2)
            oper->line_count = 2;
         if (curPor->cursor.lnum + oper->line_count - 1 > curBook->mem.lineCount)
            inpFlushIfNotSilent();
         else {
            (void)doJoinLinesUnderCursor(oper->line_count, oper->opTy == OP_JOIN, true, true, true);
            auto_format(false, true);
         }
         break;

      case OP_DELETE:
      case OP_CUT:
         VIsual_reselect = false;       //don't reselect now
         (void)op_delete(oper);
         //save cursor line for undo if it wasn't saved yet
         if (oper->motion_type == MLINE && has_format_option(FO_AUTO) && u_save_cursor() == OK)
            auto_format(false, true);
         break;

      case OP_YANK:
          oper->excludeTrailingWhitespace = aArg->cmdchar == 'z';
          (void)op_yank(oper, false, !clipbYank);
          check_cursor_col();
          break;

      case OP_CHANGE:
         VIsual_reselect = false;       //don't reselect now
         //This is a new edit command, not a restart.  Need to
         //remember it to make 'insertmode' work with mappings for
         //Visual mode.  But do this only once and not when typed and 'insertmode' isn't set.
         if (!keyWasTypedG)
            restart_edit_save = restart_edit;
         else
            restart_edit_save = 0;
         restart_edit = 0;
         //trigger TextChangedI
         curBook->lastChangeTickInsert = CHANGEDTICK(curBook);

         if (op_change(oper))   //will call edit()
            aArg->retval |= CA_COMMAND_BUSY;
         if (restart_edit == 0)
            restart_edit = restart_edit_save;
         break;

      case OP_FILTER:
         bangredo = true;    //do_bang() will put cmd in redo buffer
         //FALLTHROUGH

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
            op_formatexpr(oper);   //use expression
         else {
            if (p_fp || curBook->o.formatProg)
               op_colon(oper);      //use external command
            else
               op_format(oper, false);   //use internal function
         }
         break;
      case OP_FORMAT2:
         op_format(oper, true);   //use internal function
         break;

      case OP_FUNCTION: {
         RedoVisual save_redo_VIsual = redo_VIsual;

         //call 'operatorfunc'
         op_function(oper);

         //Restore the info for redoing Visual mode, the function may
         //invoke another operator and unintentionally change it.
         redo_VIsual = save_redo_VIsual;
         break;
      }

      case OP_INSERT:
      case OP_APPEND:
         VIsual_reselect = false;   //don't reselect now
         //This is a new edit command, not a restart.  Need to
         //remember it to make 'insertmode' work with mappings for
         //Visual mode.  But do this only once.
         restart_edit_save = restart_edit;
         restart_edit = 0;
         //trigger TextChangedI
         curBook->lastChangeTickInsert = CHANGEDTICK(curBook);

         op_insert(oper, aArg->count1);

         //TODO: when inserting in several lines, should format all the lines.
         auto_format(false, true);

         if (restart_edit == 0)
            restart_edit = restart_edit_save;
         else
            aArg->retval |= CA_COMMAND_BUSY;
         break;

      case OP_REPLACE:
         VIsual_reselect = false;   //don't reselect now
         op_replace(oper, aArg->nchar);
         break;

      case OP_FOLD:
         VIsual_reselect = false;   //don't reselect now
         foldCreate(oper->start.lnum, oper->end.lnum);
         break;

      case OP_FOLDOPEN:
      case OP_FOLDOPENREC:
      case OP_FOLDCLOSE:
      case OP_FOLDCLOSEREC:
         VIsual_reselect = false;   //don't reselect now
         opFoldRange(
            oper->start.lnum, oper->end.lnum,
            oper->opTy == OP_FOLDOPEN || oper->opTy == OP_FOLDOPENREC,
            oper->opTy == OP_FOLDOPENREC || oper->opTy == OP_FOLDCLOSEREC,
            oper->is_VIsual
         );
         break;

      case OP_FOLDDEL:
      case OP_FOLDDELREC:
         VIsual_reselect = false;   //don't reselect now
         deleteFold(oper->start.lnum, oper->end.lnum,
                   oper->opTy == OP_FOLDDELREC, oper->is_VIsual);
         break;
      case OP_ADD:
      case OP_SUB:
         VIsual_active = true;
         op_addsub(oper, aArg->count1, redo_VIsual.extraArg);
         VIsual_active = false;
         check_cursor_col();
         break;
      default:
         clearopbeep(oper);
      }
      virtual_op = MAYBE;
      if (!clipbYank) {
         //if 'sol' not set, go back to old column for some commands
         if (!p_sol && oper->motion_type == MLINE && !oper->end_adjusted
             && (oper->opTy == OP_LSHIFT || oper->opTy == OP_RSHIFT
                     || oper->opTy == OP_DELETE)
         ) {
             coladvance(curPor->cursWant = old_col);
         }
      } else {
         curPor->cursor = old_cursor;
      }
      oper->block_mode = false;
      clearop(oper);
      motion_force = ZERO;
    }
}

//put byte 'c' at position 'lp', but
//verify, that the position to place is actually safe
private void
pbyte(Pos lp, int c) {
   CS p = memGetLine(curBook, lp.lnum, true);
   int len = curBook->mem.lineLen;

   //safety check
   if (lp.col >= len) {
      lp.col = (len > 1 ? len - 2 : 0);
   } 
   *(p + lp.col) = c;
}

//}}}
//{{{time

#include <time.h>

//Cache of the current timezone name as retrieved from TZ, or an empty string
//where unset, up to 64 octets long including trailing null byte.
private Byte   tz_cache[64];

#define FOR_ALL_TIMERS(t) \
    for ((t) = firstTimerS; (t) != NULL; (t) = (t)->next)
    

//Call either localtime(3) or localtime_r(3) from POSIX libc time.h, with the
//latter version preferred for reentrancy.
//
//If we use localtime_r(3) and we have tzset(3) available, check to see if the environment variable 
//TZ has changed since the last run, and call tzset(3) to update the global timezone variables if 
//it has.  This is because the POSIX standard doesn't require localtime_r(3) implementations to do 
//that as it does with localtime(3), and we don't want to call tzset(3) every time.
private Tm *
eeLocaltime(
   Tyme const* timep,      //timestamp for local representation
   OUT Tm* result //pointer to caller return buffer
){
   CS tz = mch_getenv(S"TZ");      //pointer for TZ environment var
   if (tz == NULL)
      tz = S"";
   if (STRNCMP(tz_cache, tz, sizeof(tz_cache) - 1) != 0) {
      tzset();
      copySubstrToAllocation((CS)tz_cache, (Text){tz, sizeof(tz_cache) - 1});
   }
   return localtime_r(timep, result);
}

//Return the current time in seconds.  Calls time(), unless test_settime() was used.
pub Tyme
eeTime(void) {
   return time_for_testing == 0 ? time(NULL) : time_for_testing;
}

//Replacement for ctime(), which is not safe to use.
//Requires strftime(), otherwise returns "(unknown)".
//If "thetime" is invalid returns "(invalid)".  Never returns NULL.
//When "add_newline" is true add a newline like ctime() does. Use a static buffer.
pub CS
get_ctime(Tyme thetime, int add_newline) {
   static Byte buf[100];  //hopefully enough for every language
   Tm tmval;
   Tm* curtime = eeLocaltime(&thetime, &tmval);
   if (!curtime)
      copySubstrToAllocation(buf, (Text){_("(Invalid)"), sizeof(buf) - 2});
   else {
      //xgettext:no-c-format
      if (STRFTIME(buf, sizeof(buf) - 2, _("%a %b %d %H:%M:%S %Y"), curtime) == 0) {
         //Quoting "man strftime":
         //> If the length of the result string (including the terminating
         //> null byte) would exceed max bytes, then strftime() returns 0,
         //> and the contents of the array are undefined.
         copySubstrToAllocation((CS)buf, (Text){_("(Invalid)"), sizeof(buf) - 2});
      }
   }
   if (add_newline)
      STRCAT(buf, "\n");
   return buf;
}


//"localtime()" function
pub void
f_localtime(Arr(Var), OUT Var* returnVar) {
   returnVar->number = (Long)time(NULL);
}

//Convert a List to ProfTime. Return FAIL when there is something wrong.
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


//"reltime()" function
pub void
f_reltime(Arr(Var) argVars, OUT Var* returnVar) {
   ProfTime   res;
   ProfTime   start;

   allocReturnList(returnVar);

   if (argVars[0].tag == VAR_UNKNOWN) {
      //No arguments: get current time.
      profile_start(&res);
   } ei (argVars[1].tag == VAR_UNKNOWN) {
      if (list2proftime(&argVars[0], &res) == FAIL) {
         return;
      }
      profile_end(&res);
   } else {
      //Two arguments: compute the difference.
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

pub void
f_reltimefloat(Arr(Var) argVars, OUT Var* returnVar) {
   ProfTime   tm;

   returnVar->tag = VAR_FLOAT;
   returnVar->floatt = 0;

   if (list2proftime(&argVars[0], &tm) == OK)
      returnVar->floatt = profile_float(&tm);
}

pub void
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
pub void
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

//"strptime({format}, {timestring})" function
pub void
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
pub long
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
   did_add_timer = true;
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

//Create a timer and return it. Caller should set the callback.
pub Timer*
create_timer(long msec, int repeat) {
   Timer* timer = ALLOC_CLEAR_ONE(Timer);
   long   prev_id = lastTimerIdS;

   if (++lastTimerIdS <= prev_id)
      //Overflow!  Might cause duplicates...
      lastTimerIdS = 0;
   timer->id = lastTimerIdS;
   insert_timer(timer);
   if (repeat != 0)
      timer->tr_repeat = repeat - 1;
   timer->tr_interval = msec;

   timer_start(timer);
   return timer;
}

//(Re)start a timer.
pub void
timer_start(Timer *timer) {
   profile_setlimit(timer->tr_interval, &timer->due);
   timer->tr_paused = false;
}

//Invoke the callback of "timer".
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

//Call timers that are due. Return the time in msec until the next timer is due.
//Return -1 if there are no pending timers.
pub long
check_due_timer(void) {
   Timer* timer_next;
   long this_due;
   long next_due = -1;
   ProfTime now;
   Boole did_one = false;
   Boole need_drawUpdateScreen = false;
   long current_id = lastTimerIdS;

   //Don't run any timers while exiting, dealing with an error or at the debug prompt.
   if (isExitingG || aborting() || debug_mode)
      return next_due;

   profile_start(&now);
   for (Timer* timer = firstTimerS; timer != NULL && !gotInterruptG; timer = timer_next) {
      timer_next = timer->next;

      if (timer->id == -1 || timer->tr_firing || timer->tr_paused)
         continue;
      this_due = proftime_time_left(&timer->due, &now);
      if (this_due <= 1) {
         //Save and restore a lot of flags, because the timer fires while
         //waiting for a character, which might be halfway a command.
         int save_timer_busy = timer_busy;
         int save_vgetcBusyG = vgetcBusyG;
         int save_anyEmsgG = anyEmsgG;
         int prev_uncaught_emsg = uncaught_emsg;
         int save_called_emsg = called_emsg;
         Unt mustRedrawSaved = mustRedrawG;
         int save_ex_pressedreturn = get_pressedreturn();
         int save_may_garbage_collect = may_garbage_collect;
         ExceptionState estate;

         exception_state_save(&estate);

         //Create a scope for running the timer callback, ignoring most of
         //the current scope, such as being inside a try/catch.
         timer_busy = timer_busy > 0 || vgetcBusyG > 0;
         vgetcBusyG = 0;
         called_emsg = 0;
         anyEmsgG = false;
         mustRedrawG = 0;
         may_garbage_collect = false;
         exception_state_clear();

         //Invoke the callback.
         timer->tr_firing = true;
         timer_callback(timer);
         timer->tr_firing = false;

         //Restore stuff.
         timer_next = timer->next;
         did_one = true;
         timer_busy = save_timer_busy;
         vgetcBusyG = save_vgetcBusyG;
         if (uncaught_emsg > prev_uncaught_emsg)
            ++timer->tr_emsg_count;
         anyEmsgG = save_anyEmsgG;
         called_emsg = save_called_emsg;
         exception_state_restore(&estate);
         if (mustRedrawG != 0)
            need_drawUpdateScreen = true;
         mustRedrawG = mustRedrawG > mustRedrawSaved ? mustRedrawG : mustRedrawSaved;
         set_pressedreturn(save_ex_pressedreturn);
         may_garbage_collect = save_may_garbage_collect;

         //Only fire the timer again if it repeats and stop_timer() wasn't
         //called while inside the callback (id == -1).
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
               timer->tr_paused = true;
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
         bevalexpr_due_set = false;
         if (balloonEval == NULL) {
            balloonEval = ALLOC_CLEAR_ONE(BalloonEval);
            balloonEvalForTerm = true;
         }
         if (balloonEval != NULL) {
            general_beval_cb(balloonEval, 0);
            setcursor();
            out_flush();
         }
      } ei (next_due == -1 || next_due > this_due)
         next_due = this_due;
   }
   //Some terminal portals may need their book updated.
   next_due = term_check_timers(next_due, &now);

   return current_id != lastTimerIdS ? 1 : next_due;
}

//Find a timer by ID.  Returns NULL if not found;
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


//Stop a timer and delete it.
pub void
stop_timer(Timer *timer) {
   if (timer->tr_firing)
      //Free the timer after the callback returns.
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

//Mark references in partials of timers.
pub int
set_ref_in_timer(int copyID) {
   int abort = false;
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

//Return true if "timer" exists in the list of timers.
pub int
timer_valid(Timer *timer) {
   if (!timer)
      return false;

   Timer *t;
   FOR_ALL_TIMERS(t) {
      if (t == timer)
         return true;
   } 
   return false;
}

# if defined(EXITFREE)
pub void
timer_free_all(void) {
   while (firstTimerS != NULL) {
      Timer *timer = firstTimerS;
      remove_timer(timer);
      free_timer(timer);
   }
}
# endif

//"timer_info([timer])" function
pub void
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

//"timer_pause(timer, paused)" function
pub void
f_timer_pause(Arr(Var) argVars, OUT Var*) {
   if (argVars[0].tag != VAR_NUMBER) {
      emsg(_(e_number_expected));
      return;
   }

   int paused = (int)tv_get_bool(&argVars[1]);

   Timer* timer = find_timer((int)tv_get_number(&argVars[0]));
   if (timer != NULL)
      timer->tr_paused = paused;
}

//"timer_start(time, callback [, options])" function
pub void
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

//"timer_stop(timer)" function
pub void
f_timer_stop(Arr(Var) argVars, OUT Var*) {
   if (check_for_number_arg(argVars, 0) == FAIL)
      return;

   Timer* timer = find_timer((int)tv_get_number(&argVars[0]));
   if (timer)
      stop_timer(timer);
}

//"timer_stopall()" function
pub void
f_timer_stopall(Arr(Var), OUT Var*) {
   stop_all_timers();
}

private TimeVal   prev_timeval;

//Save the previous time before doing something that could nest.
//set "*tv_rel" to the time elapsed so far.
pub void
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
pub void
time_pop(void   *tp) {  //actually (TimeVal *)
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

pub void
time_msg(
   CS mesg,
   void* tv_start  //only for scriptRunFile: start time; actually (TimeVal *)
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

//Read 8 bytes from "fd" and turn them into a Tyme, MSB first. Returns -1 when encountering EOF.
pub Tyme
get8ctime(FILE *fd) {
   Tyme   n = 0;

   for (int i = 0; i < 8; ++i) {
      int c = getc(fd);
      if (c == EOF) return -1;
      n = (n << 8) + c;
   }
   return n;
}

//Write Tyme to file "fd" in 8 bytes. Returns FAIL when the write failed.
pub int
put_time(FILE *fd, Tyme the_time) {
   Byte buf[8];

   time_to_bytes(the_time, buf);
   return fwrite(buf, 8, 1, fd) == 1 ? OK : FAIL;
}

//Write Tyme to "buf[8]".
pub void
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
         //">>" doesn't work well when shifting more bits than avail
         buf[bi++] = 0;
      else {
         c = (int)(wtime >> (i * 8));
         buf[bi++] = c;
      }
   }
}

//Put timestamp "tt" in "buf[buflen]" in a nice format.
pub void
add_time(CS buf, Unt buflen, Tyme tt) {
   Tm tmval;
   Tm* curtime;
   Unt   n;

   if (eeTime() - tt >= 100) {
      curtime = eeLocaltime(&tt, &tmval);
      if (eeTime() - tt < (60L * 60L * 12L))
         //within 12 hours
         n = STRFTIME(buf, buflen, "%H:%M:%S", curtime);
      else
         //longer ago
         n = STRFTIME(buf, buflen, "%Y/%m/%d %H:%M:%S", curtime);
      if (n == 0)
         buf[0] = ZERO;
   } else {
      long seconds = (long)(eeTime() - tt);

      eeSnprintf(buf, buflen, NGETTEXT("%ld second ago", "%ld seconds ago", seconds), seconds);
   }
}

//Store the current time in "tm".
pub void
profile_start(ProfTime *tm){
   PROF_GET_TIME(tm);
}

//Put the time "msec" past now in "tm".
pub void
profile_setlimit(long msec, ProfTime *tm) {
   if (msec <= 0)   //no limit
      profile_zero(tm);
   else {
      PROF_GET_TIME(tm);
      Long fsec = (Long)tm->tv_fsec + (Long)msec * (Long)(TV_FSEC_SEC / 1000);
      tm->tv_fsec = fsec % (long)TV_FSEC_SEC;
      tm->tv_sec += fsec / (long)TV_FSEC_SEC;
   }
}

//Return true if the current time is past "tm".
pub int
profile_passed_limit(ProfTime *tm) {
   if (tm->tv_sec == 0)    //timer was not set
      return false;
      
   ProfTime   now;
   PROF_GET_TIME(&now);
   return (now.tv_sec > tm->tv_sec || (now.tv_sec == tm->tv_sec && now.tv_fsec > tm->tv_fsec));
}


//Compute the elapsed time from "tm" till now and store in "tm".
pub void
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

//Subtract the time "tm2" from "tm".
pub void
profile_sub(ProfTime *tm, ProfTime *tm2){
   tm->tv_fsec -= tm2->tv_fsec;
   tm->tv_sec -= tm2->tv_sec;
   if (tm->tv_fsec < 0) {
      tm->tv_fsec += TV_FSEC_SEC;
      --tm->tv_sec;
   }
}

//Return a float that represents the time in "tm".
private double
profile_float(ProfTime *tm){
   return (double)tm->tv_sec + (double)tm->tv_fsec / (double)TV_FSEC_SEC;
}

//Set the time in "tm" to zero.
pub void
profile_zero(ProfTime *tm) {
   tm->tv_fsec = 0;
   tm->tv_sec = 0;
}

//Return a string that represents the time in "tm". Use a static buffer!
pub CS
profile_msg(ProfTime *tm){
   static Byte buf[50];

   SPRINTF(buf, PROF_TIME_FORMAT, (long)tm->tv_sec, (long)tm->tv_fsec);
   return buf;
}

# ifdef ELAPSED_TIMEVAL
//Return time in msec since "start_tv".
pub long
elapsed(TimeVal *start_tv) {
   TimeVal  now_tv;
   gettimeofday(&now_tv, NULL);
   return (now_tv.tv_sec - start_tv->tv_sec) * 1000L + (now_tv.tv_usec - start_tv->tv_usec) / 1000L;
}
# endif

# if defined(PROF_NSEC)
//Implement timeout with timer_create() and timer_settime().
private volatile sig_atomic_t timeout_flag = false;
private timer_t timer_id;
private int timer_created = false;

//Callback for when the timer expires.
private void
set_flag(union sigval) {
   timeout_flag = true;
}

//Stop any active timeout.
pub void
stop_timeout(void) {
   static struct itimerspec disarm = {{0, 0}, {0, 0}};

   if (timer_created) {
      int ret = timer_settime(timer_id, 0, &disarm, NULL);

      if (ret < 0)
         showErrFmtMsg(_(e_could_not_clear_timeout_str), strerror(errno));
   }

   //Clear the current timeout flag; any previous timeout should be
   //considered _not_ triggered.
   timeout_flag = false;
}

//Start the timeout timer.
//
//The return value is a pointer to a flag that is initialised to false. If the
//timeout expires, the flag is set to true. This will only return pointers to
//static memory; i.e. any pointer returned by this function may always be
//safely dereferenced.
//
//This function is not expected to fail, but if it does it will still return a
//valid flag pointer; the flag will remain stuck as false .
pub volatile sig_atomic_t *
start_timeout(long msec) {
   struct itimerspec interval = {
       {0, 0},               //Do not repeat.
       {msec / 1000, (msec % 1000) * 1000000}};   //Timeout interval

   //This is really the caller's responsibility, but let's make sure the
   //previous timer has been stopped.
   stop_timeout();

   if (!timer_created) {
      struct sigevent action = {0};

      action.sigev_notify = SIGEV_THREAD;
      action.sigev_notify_function = set_flag;
      int ret = timer_create(CLOCK_MONOTONIC, &action, &timer_id);
      if (ret < 0) {
         showErrFmtMsg(_(e_could_not_set_timeout_str), strerror(errno));
         return &timeout_flag;
      }
      timer_created = true;
   }

   lo("setting timeout timer to %d sec %ld nsec",
          (int)interval.it_value.tv_sec, (long)interval.it_value.tv_nsec);
   int ret = timer_settime(timer_id, 0, &interval, NULL);
   if (ret < 0)
      showErrFmtMsg(_(e_could_not_set_timeout_str), strerror(errno));

   return &timeout_flag;
}

//To be used before fork/exec: delete any created timer.
pub void
delete_timer(void) {
   if (!timer_created)
      return;

   timer_delete(timer_id);
   timer_created = false;
}

# else //PROF_NSEC

//Implement timeout with setitimer()
private SignalAction      prev_sigaction;
private volatile sig_atomic_t   timeout_flag        = false;
private int         timer_active        = false;
private int         timer_handler_active = false;
private volatile sig_atomic_t   alarm_pending        = false;

//Handle SIGALRM for a timeout.
private void
set_flag(union sigval) {
   if (alarm_pending)
      alarm_pending = false;
   else
      timeout_flag = true;
}

//Stop any active timeout.
pub void
stop_timeout(void) {
   static struct itimerval disarm = {{0, 0}, {0, 0}};
   int             ret;

   if (timer_active) {
      timer_active = false;
      ret = setitimer(ITIMER_REAL, &disarm, NULL);
      if (ret < 0)
         //Should only get here as a result of coding errors.
         showErrFmtMsg(_(e_could_not_clear_timeout_str), strerror(errno));
   }

   if (timer_handler_active) {
      timer_handler_active = false;
      ret = sigaction(SIGALRM, &prev_sigaction, NULL);
      if (ret < 0)
         //Should only get here as a result of coding errors.
         showErrFmtMsg(_(e_could_not_reset_handler_for_timeout_str), strerror(errno));
   }
   timeout_flag = false;
}

//Start the timeout timer.
//
//The return value is a pointer to a flag that is initialised to false. If the timeout expires, the
//flag is set to true. This will only return pointers to static memory; i.e. any pointer returned 
//by this function may always be safely dereferenced.
//
//This function is not expected to fail, but if it does it will still return a valid flag pointer;
//the flag will remain stuck as false.
pub volatile sig_atomic_t*
start_timeout(long msec) {
   struct itimerval   interval = {
       {0, 0},                //Do not repeat.
       {msec / 1000, (msec % 1000) * 1000}};   //Timeout interval
   SignalAction handle_alarm;
   int ret;
   SignalSet sigs;
   SignalSet saved_sigs;

   //This is really the caller's responsibility, but let's make sure the
   //previous timer has been stopped.
   stop_timeout();

   //There is a small chance that SIGALRM is pending and so the handler must
   //ignore it on the first call.
   alarm_pending = false;
   ret = sigemptyset(&sigs);
   ret = ret == 0 ? sigaddset(&sigs, SIGALRM) : ret;
   ret = ret == 0 ? sigprocmask(SIG_BLOCK, &sigs, &saved_sigs) : ret;
   timeout_flag = false;
   ret = ret == 0 ? sigpending(&sigs) : ret;
   if (ret == 0) {
      alarm_pending = sigismember(&sigs, SIGALRM);
      ret = sigprocmask(SIG_SETMASK, &saved_sigs, NULL);
   }
   if (unlikely(ret != 0 || alarm_pending < 0)) {
      //Just catching coding errors. Write an error message, but carry on.
      showErrFmtMsg(_(e_could_not_check_for_pending_sigalrm_str), strerror(errno));
      alarm_pending = false;
   }

   //Set up the alarm handler first.
   ret = sigemptyset(&handle_alarm.sa_mask);
   handle_alarm.sa_handler = set_flag;
   
   handle_alarm.sa_flags = 0;
   ret = ret == 0 ?  sigaction(SIGALRM, &handle_alarm, &prev_sigaction) : ret;
   if (ret < 0) {
      //Should only get here as a result of coding errors.
      showErrFmtMsg(_(e_could_not_set_handler_for_timeout_str), strerror(errno));
      return &timeout_flag;
   }
   timer_handler_active = true;

   //Set up the interval timer once the alarm handler is in place.
   ret = setitimer(ITIMER_REAL, &interval, NULL);
   if (ret < 0) {
      //Should only get here as a result of coding errors.
      showErrFmtMsg(_(e_could_not_set_timeout_str), strerror(errno));
      stop_timeout();
      return &timeout_flag;
   }

   timer_active = true;
   return &timeout_flag;
}
# endif //PROF_NSEC


//}}}
//{{{cursor movement

//Get the screen position of the cursor.
pub int
getviscol(void) {
   ColNr   x;
   bookGetVirtualColInVirtualMode(curPor, &curPor->cursor, OUT &x, NULL, NULL);
   return (int)x;
}

//Go to column "wcol", and add/insert white space as necessary to get the cursor in that column.
//The caller must have saved the cursor line for undo!
pub int
coladvance_force(ColNr wcol) {
   int rc = coladvance2(&curPor->cursor, true, false, wcol);

   if (wcol == MAXCOL)
      curPor->cacheState &= ~VALID_VIRTCOL;
   else {
      //Virtcol is valid
      curPor->cacheState |= VALID_VIRTCOL;
      curPor->virtCol = wcol;
   }
   return rc;
}

//Try to advance the Cursor to the specified screen column "wantcol". If virtual editing: fine 
//tune the cursor position. Note that all virtual positions off the end of a line should share
//a curPor->cursor.col value (n.b. this is equal to STRLEN(line)), beginning at coladd 0.
//return OK if desired column is reached, FAIL if not
pub int
coladvance(ColNr wantcol) {
   int rc = getvpos(&curPor->cursor, wantcol);

   if (wantcol == MAXCOL || rc == FAIL)
      curPor->cacheState &= ~VALID_VIRTCOL;
   ei (*ml_get_cursor() != TAB) {
      //Virtcol is valid when not on a TAB
      curPor->cacheState |= VALID_VIRTCOL;
      curPor->virtCol = wantcol;
   }
   return rc;
}

//Return in "pos" the position of the cursor advanced to screen column "wantcol".
//return OK if desired column is reached, FAIL if not
pub int
getvpos(Pos *pos, ColNr wantcol) {
   return coladvance2(pos, false, virtual_active(), wantcol);
}

private int
coladvance2(
   Pos   *pos,
   int      addspaces,   //change the text to achieve our goal?
   int      finetune,   //change char offset for the exact column
   ColNr   wcol_arg   //column to move to (can be negative)
){
   ColNr   wcol = wcol_arg;
   int      idx;
   ColNr   col = 0;
   int      csize = 0;
   int      head = 0;

   Boole one_more = ((stateG & MODE_INSERT) != 0) || restart_edit != ZERO || (VIsual_active);
   CS line = memGetLine(curBook, pos->lnum, false);
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
         //Count a tab for what it's worth (if list mode not on)
         csize = win_lbr_chartabsize(&cts, &head);
         MB_PTR_ADV(cts.cts_ptr);
         cts.cts_vcol += csize;
         if (at_start)
            //do not count the columns for virtual text above
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

            ml_replace(pos->lnum, newline, false);
            changed_bytes(pos->lnum, (ColNr)idx);
            idx += correct;
            col = wcol;
         } else {
            //Break a tab
            int   correct = wcol - col - csize + 1; //negative!!
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

            ml_replace(pos->lnum, newline, false);
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
         //The width of the last character is used to set coladd.
         if (!one_more) {
            ColNr scol, ecol;
            getvcol(curPor, pos, OUT &scol, NULL, OUT &ecol);
            pos->coladd = ecol - scol;
         }
      } else {
         int b = (int)wcol - (int)col;

         //The difference between wcol and col is used to set coladd.
         if (b > 0 && b < (MAXCOL - 2 * curPor->width))
            pos->coladd = b;

         col += b;
      }
   }

   //prevent from moving onto a trail byte
   mb_adjustpos(curBook, pos);

   return (wcol < 0 || col < wcol) ? FAIL : OK;
}

//Increment the cursor position.  See inc() for return values.
pub int
inc_cursor(void) {
   return inc(&curPor->cursor);
}

//Increment the line pointer "lp" crossing line boundaries as necessary.
//Return 1 when going to the next line.
//Return 2 when moving forward onto a ZERO at the end of the line).
//Return -1 when at the end of file. 0 otherwise.
pub int
inc(Pos *lp) {
   //when searching position may be set to end of a line
   if (lp->col != MAXCOL) {
      CS p = ml_get_pos(lp);
      if (*p != ZERO) {//still within line, move to next char (may be ZERO)
         int l = utfCharLen(p);
         lp->col += l;
         return ((p[l] != ZERO) ? 0 : 2);
      }
   }
   if (lp->lnum != curBook->mem.lineCount) {    //there is a next line
      lp->col = 0;
      lp->lnum++;
      lp->coladd = 0;
      return 1;
   }
   return -1;
}

//incl(lp): same as inc(), but skip the ZERO at the end of non-empty lines
pub int
incl(Pos *lp) {
   int r;
   if ((r = inc(lp)) >= 1 && lp->col)
      r = inc(lp);
   return r;
}

pub int
dec_cursor(void) {
   return dec(&curPor->cursor);
}

//dec(p)
//
//Decrement the line pointer 'p' crossing line boundaries as necessary.
//Return 1 when crossing a line, -1 when at start of file, 0 otherwise.
pub int
dec(Pos *lp) {
   CS p;

   lp->coladd = 0;
   if (lp->col == MAXCOL) {
      //past end of line
      p = ml_get(lp->lnum);
      lp->col = ml_get_len(lp->lnum);
      lp->col -= (*mb_head_off)(p, p + lp->col);
      return 0;
   }

   if (lp->col > 0) {
      //still within line
      lp->col--;
      p = ml_get(lp->lnum);
      lp->col -= (*mb_head_off)(p, p + lp->col);
      return 0;
   }

   if (lp->lnum > 1) {
      //there is a prior line
      lp->lnum--;
      p = ml_get(lp->lnum);
      lp->col = ml_get_len(lp->lnum);
      lp->col -= (*mb_head_off)(p, p + lp->col);
      return 1;
   }

   //at start of file
   return -1;
}

//decl(lp): same as dec(), but skip the ZERO at the end of non-empty lines
pub int
decl(Pos *lp) {
   int r;

   if ((r = dec(lp)) == 1 && lp->col)
      r = dec(lp);
   return r;
}

//Make sure "pos.lnum" and "pos.col" are valid in "buf".
//This allows for the col to be on the ZERO byte.
pub void
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
pub long
get_sw_value(Book *book) {
   return get_sw_value_col(book, 0, false);
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
private long
get_sw_value_indent(Book* book, int left) {
   Pos pos = curPor->cursor;

   pos.col = getwhitecols_curline();
   return get_sw_value_pos(book, &pos, left);
}

//Idem, using virtual column "col".
private long
get_sw_value_col(Book* book, ColNr, int) {
   return book->o.shiftWidth;
}

//Count the size (in portal cells) of the indent in the current line.
pub int
get_indent(void) {
   return get_indent_str(ml_get_curline(), (int)curBook->o.shiftWidth);
}

//Count the size (in portal cells) of the indent in line "lnum".
pub int
get_indent_lnum(LineNr lnum) {
   return get_indent_str(ml_get(lnum), (int)curBook->o.shiftWidth);
}

//Count the size (in portal cells) of the indent in line "lnum" of book "book".
pub int
get_indent_buf(Book* book, LineNr lnum) {
   return get_indent_str(memGetLine(book, lnum, false), (int)book->o.shiftWidth);
}

//count the size (in portal cells) of the indent in line "ptr", with 'tabstop' at "ts"
private int
get_indent_str(CS ptr, int ts) {//if true, count a tab as ^I
   int count = 0;
   for (; *ptr != ZERO; ++ptr) {
      if (*ptr == TAB) {   //count a tab for what it is worth
         count += ts;
      } ei (*ptr == ' ')
         ++count;      //count a space for one
      else
         break;
   }
   return count;
}

//Set the indent of the current line. Leave the cursor on the first non-blank in the line.
//Caller must take care of undo.
//"flags":
// SIN_CHANGED:   call changed_bytes() if the line was changed.
// SIN_INSERT:   insert the indent in front of the line.
// SIN_UNDO:   save line for undo before changing it.
//Return true if the line was changed.
pub int
set_indent(
   int      size,          //measured in spaces
   int      flags
){
   Byte   *s;
   int      line_len;       //size of the line (including the ZERO)
   int      doit = false;
   int      retval = false;

   //First check if there is anything to do and compute the number of
   //characters needed for the indent.
   int todo = size;
   int ind_len = 0; //measured in characters
   CS oldline = ml_get_curline();
   CS p = oldline;
   line_len = ml_get_curline_len() + 1;

   //Calculate the buffer size for the new indent, and check to see if it isn't already set

   //if 'expandtab' isn't set: use TABs
   if (!curBook->o.expandTab) {
      //count tabs required for indent
      while (todo >= (int)curBook->o.shiftWidth) {
         if (*p != TAB)
             doit = true;
         else
             ++p;
         todo -= (int)curBook->o.shiftWidth;
         ++ind_len;
       }
   }
   //count spaces required for indent
   while (todo > 0) {
      if (*p != ' ')
         doit = true;
      else
         ++p;
      --todo;
      ++ind_len;
   }

   //Return if the indent is OK already.
   if (!doit && !SPACE_OR_TAB(*p) && !(flags & SIN_INSERT))
      return false;

   //Allocate memory for the new line.
   if ((flags & SIN_INSERT) != 0)
      p = oldline;
   else {
      p = skipwhite(p);
      line_len -= (int)(p - oldline);
   }

   todo = size;
   CS newline = alloc(ind_len + line_len);
   s = newline;

   //Put the characters in the new line.
   //if 'expandtab' isn't set: use TABs
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
   MEMMOVE(s, p, (Unt)line_len);

   //Replace the line (unless undo fails).
   if (!(flags & SIN_UNDO) || u_savesub(curPor->cursor.lnum) == OK) {
      ColNr old_offset = (ColNr)(p - oldline);
      ColNr new_offset = (ColNr)(s - newline);

      //this may free "newline"
      ml_replace(curPor->cursor.lnum, newline, false);
      if (flags & SIN_CHANGED)
          changed_bytes(curPor->cursor.lnum, 0);

      //Correct saved cursor position if it is in this line.
      if (saved_cursor.lnum == curPor->cursor.lnum) {
         if (saved_cursor.col >= old_offset)
            //cursor was after the indent, adjust for the number of bytes added/removed
            saved_cursor.col += ind_len - old_offset;
         ei (saved_cursor.col >= new_offset)
            //cursor was in the indent, and is now after it, put it back
            //at the start of the indent (replacing spaces with TAB)
            saved_cursor.col = new_offset;
      }
      int added = ind_len - old_offset;

      //When increasing indent this behaves like spaces were inserted at the old indent, when 
      //decreasing indent it behaves like spaces were deleted at the new indent.
      adjustPropColumns(
         curPor->cursor.lnum, added > 0 ? old_offset : (ColNr)ind_len, added, APC_INDENT
      );
      retval = true;
   } else
       eeglFree(newline);

   curPor->cursor.col = ind_len;
   return retval;
}

//Return the indent of the current line after a number.  Return -1 if no
//number was found.  Used for 'n' in 'formatoptions': numbered list.
//Since a pattern is used it can actually handle more than numbers.
pub int
get_number_indent(LineNr lnum) {
   ColNr   col;
   Pos   pos;

   RegMatch   regmatch;
   int      lead_len = 0;   //length of comment leader

   if (lnum > curBook->mem.lineCount)
      return -1;
   pos.lnum = 0;

   //In format_lines() (i.e. not insert mode), fo+=q is needed too...
   if ((stateG & MODE_INSERT) || has_format_option(FO_Q_COMS))
      lead_len = get_leader_len(ml_get(lnum), NULL, false, true);

   regmatch.regprog = compileRegexp(curBook->o.formatListPattern, RE_MAGIC);
   if (regmatch.regprog) {
      regmatch.rm_ic = false;
      //eeRegexec() expects a pointer to a line.  This lets us
      //start matching for the flp beyond any comment leader...
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
pub int
getBreakindentForPort(Portal* po, CS line) {
   static int       prev_indent = 0;   //cached indent value
   static long       prev_ts     = 0L;   //cached tabstop value
   static int       prev_fnum   = 0;   //cached book number
   static CS prev_line  = NULL;   //cached copy of "line"
   static Long prev_tick = 0;   //changedtick of cached value
   static int preList = 0;   //cached list indent
   static int preListopt = 0;   //cached o.breakIndent_list value
   static int prev_no_ts = false;   //cached no_ts value
   //cached formatlistpat value
   static Byte   *prev_flp = NULL;
   int          bri = 0;
   //portal width minus portal margin space, i.e. what rests for text
   const int       eff_wwidth = po->width  - normalPortalColumnOffset(po);

   //In list mode, if 'listchars' "tab" isn't set, a TAB is displayed as ^I.
   int no_ts = po->o.list && listCharsG.tab1 == ZERO;

   //Used cached indent, unless
   //- book changed, or
   //- book was changed, or
   //- @breakindentopt "list" changed, or
   //- @list or @listchars "tab" changed, or
   //- @formatlistpat changed, or
   //- line changed.
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
      //add additional indent for numbered lists
      if (po->breakIndent.list != 0 && po->breakIndent.vcol == 0) {
         RegMatch regmatch;

         regmatch.regprog = compileRegexp(prev_flp, RE_MAGIC + RE_STRING + RE_AUTO + RE_STRICT);

         if (regmatch.regprog != NULL) {
             regmatch.rm_ic = false;
             if (eeRegexec(&regmatch, line, 0)) {
                if (po->breakIndent.list > 0)
                   preList = po->breakIndent.list;
                else {
                   CS ptr = *regmatch.startp;
                   CS end_ptr = *regmatch.endp;
                   int indent = 0;

                   //Compute the width of the matched text.
                   //Use win_chartabsize() so that TAB size is correct, while wrapping is ignored.
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
      //column value has priority
      bri = po->breakIndent.vcol;
      preList = 0;
   } else
      bri = prev_indent + po->breakIndent.shift;

   //add additional indent for numbered lists
   if (po->breakIndent.list > 0)
      bri += preList;

   //indent minus the length of the showbreak string
   if (po->breakIndent.showBreak && p_sbr)
      bri -= eeglStrSize(p_sbr);

   //never indent past left portal margin
   if (bri < 0) {
      bri = 0;

      //always leave at least bri_min characters on the left, if text width is sufficient
   } ei (bri > eff_wwidth - po->breakIndent.min) {
      bri = (eff_wwidth - po->breakIndent.min < 0) ? 0 : eff_wwidth - po->breakIndent.min;
   }

   return bri;
}

//When extra == 0: Return true if the cursor is before or on the first non-blank in the line.
//When extra == 1: Return true if the cursor is before the first non-blank in the line.
pub int
inindent(int extra) {
   Byte   *ptr;
   ColNr   col;

   for (col = 0, ptr = ml_get_curline(); SPACE_OR_TAB(*ptr); ++col)
      ++ptr;
   if (col >= curPor->cursor.col + extra)
      return true;
   else
      return false;
}

//op_reindent - handle reindenting a block of lines.
pub void
op_reindent(Operator *oper, int (*how)(void)) {
   long   i = 0;
   Byte   *l;
   int amount;
   LineNr first_changed = 0;
   LineNr last_changed = 0;
   LineNr start_lnum = curPor->cursor.lnum;

   //Don't even try when @modifiable is off.
   if (IMMUTABLE) {
      emsg(_(e_cannot_make_changes_modifiable_is_off));
      return;
   }

   //Save for undo.  Do this once for all lines, much faster than doing this
   //for each line separately, especially when undoing.
   if (u_savecommon(
         start_lnum - 1, start_lnum + oper->line_count, start_lnum + oper->line_count, false
       ) == OK
   ) {
      for (i = oper->line_count; --i >= 0 && !gotInterruptG; ) {
         //it's a slow thing to do, so give feedback so there's no worry
         //that the computer's just hung.

         if (i > 1 && (i % 50 == 0 || i == oper->line_count - 1))
            smsg(_("%ld lines to indent... "), i);

         l = skipwhite(ml_get_curline());
         if (*l == ZERO || !how)          //empty or blank line
            amount = 0;
         else
            amount = how();       //get the indent for this line

         if (amount >= 0 && set_indent(amount, 0)) {
            //did change the indent, call doChangedLines() later
            if (first_changed == 0)
               first_changed = curPor->cursor.lnum;
            last_changed = curPor->cursor.lnum;
         }
         ++curPor->cursor.lnum;
         curPor->cursor.col = 0;  //make sure it's valid
      }
   } 

   //put cursor on first non-blank of indented line
   curPor->cursor.lnum = start_lnum;
   beginline(BL_SOL | BL_FIX);

   //Mark changed lines so that they will be redrawn.  When Visual
   //highlighting was present, need to continue until the last line.  When
   //there is no change still need to remove the Visual highlighting.
   if (last_changed != 0)
      doChangedLines(
         first_changed, 0, oper->is_VIsual ? start_lnum + oper->line_count : last_changed + 1, 0L
      );
   ei (oper->is_VIsual)
      drawCurBookLater(UPD_INVERTED);

   i = oper->line_count - (i + 1);
   smsg(NGETTEXT("%ld line indented ", "%ld lines indented ", i), i);
   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
      //set '[ and '] marks
      curBook->opStart = oper->start;
      curBook->opEnd = oper->end;
   }
}

//true if lines starting with '#' should be left aligned.
pub int
preprocs_left(void) {
   return curBook->o.smartIndent;
}

//true if the conditions are OK for smart indenting.
pub int
may_do_si(void) {
   return curBook->o.smartIndent && !curBook->o.indentExpr;
}

//Try to do some very smart auto-indenting. Used when inserting a "normal" character.
pub void
doTrySmartIndent(int c) {
   Pos   *pos, old_pos;
   CS ptr;
   int i;
   int temp;

   //do some very smart indenting when entering '{' or '}'
   if (((didSindentG || can_si_back) && c == '{') || (can_si && c == '}' && inindent(0))) {
      //for '}' set indent equal to indent of line containing matching '{'
      if (c == '}' && (pos = findmatch(NULL, '{')) != NULL) {
         old_pos = curPor->cursor;
         //If the matching '{' has a ')' immediately before it (ignoring white-space), then line 
         //up with the start of the line containing the matching '(' if there is one.  This 
         //handles the case where an "if (..\n..) {" statement continues over multiple
         //lines -- webb
         ptr = ml_get(pos->lnum);
         i = pos->col;
         if (i > 0) {     //skip blanks before '{'
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
          //when inserting '{' after "O" reduce indent, but not
          //more than indent of previous line
          temp = true;
          if (c == '{' && can_si_back && curPor->cursor.lnum > 1) {
             old_pos = curPor->cursor;
             i = get_indent();
             while (curPor->cursor.lnum > 1) {
                 ptr = skipwhite(ml_get(--(curPor->cursor.lnum)));

                  //ignore empty lines and lines starting with '#'.
                  if (*ptr != '#' && *ptr != ZERO)
                 break;
               }
               if (get_indent() >= i)
                   temp = false;
               curPor->cursor = old_pos;
           }
          if (temp)
           shift_line(true, false, 1, true);
          }
   }

   //set indent of '#' always to 0
   if (curPor->cursor.col > 0 && can_si && c == '#' && inindent(0)) {
      //remember current indent for next line
      old_indent = get_indent();
      (void)set_indent(0, SIN_CHANGED);
   }

   //Adjust ai_col, the char at this position can be deleted.
   if (ai_col > curPor->cursor.col)
      ai_col = curPor->cursor.col;
}

//Insert an indent (for <Tab> or CTRL-T) or delete an indent (for CTRL-D).
//Keep the cursor on the same character.
//type == INDENT_INC   increase indent (for CTRL-T or <Tab>)
//type == INDENT_DEC   decrease indent (for CTRL-D)
//type == INDENT_SET   set indent to "amount"
//if round is true, round the indent to 'shiftwidth' (only with _INC and _Dec).
pub void
opChangeIndent(
   int type,
   int amount,
   int round,
   Boole call_changed_bytes //call changed_bytes()
){
   int last_vcol;
   int i;

   //for the following tricks we don't want list mode
   int save_p_list = curPor->o.list;
   curPor->o.list = false;
   ignore_text_props = true;
   ColNr vc = getvcol_nolist(&curPor->cursor);
   int vcol = vc;

   //determine offset from first non-blank
   int necursor_col = curPor->cursor.col;
   beginline(BL_WHITE);
   necursor_col -= curPor->cursor.col;

   int insstart_less = curPor->cursor.col; //reduction for insertStartG.col

   //If the cursor is in the indent, compute how many screen columns the
   //cursor is to the left of the first non-blank.
   if (necursor_col < 0)
      vcol = get_indent() - vcol;

   //Set the new indent.  The cursor will be put on the first non-blank.
   if (type == INDENT_SET)
      (void)set_indent(amount, call_changed_bytes ? SIN_CHANGED : 0);
   else {
      int save_State = stateG;
      shift_line(type == INDENT_DEC, round, 1, call_changed_bytes);
      stateG = save_State;
   }
   insstart_less -= curPor->cursor.col;

   //Try to put cursor on same character.
   //If the cursor is at or after the first non-blank in the line, compute the cursor column 
   //relative to the column of the first non-blank character.
   //If we are not in insert mode, leave the cursor on the first non-blank.
   //If the cursor is before the first non-blank, position it relative
   //to the first non-blank, counted in screen columns.
   if (necursor_col >= 0) {
      //When changing the indent while the cursor is touching it, reset insertStartG_col to 0.
      if (necursor_col == 0)
         insstart_less = MAXCOL;
      necursor_col += curPor->cursor.col;
   } ei (!(stateG & MODE_INSERT))
      necursor_col = curPor->cursor.col;
   else {
      CharTableSize cts;

      //Compute the screen column where the cursor should be.
      vcol = get_indent() - vcol;
      curPor->virtCol = (ColNr)((vcol < 0) ? 0 : vcol);

      //Advance the cursor until we reach the right screen column.
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

      //May need to insert spaces to be able to position the cursor on the right screen column.
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

      //When changing the indent while the cursor is in it, reset insertStartG_col to 0.
      insstart_less = MAXCOL;
   }

   curPor->o.list = save_p_list;

   if (necursor_col <= 0)
      curPor->cursor.col = 0;
   else
      curPor->cursor.col = (ColNr)necursor_col;
   curPor->setCursWant = true;
   changed_cline_bef_curs();

   //May have to adjust the start of the insert.
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

    ignore_text_props = false;
}

//Give a "resulting text too long" error and maybe set gotInterruptG.
private void
emsg_text_too_long(void) {
   emsg(_(e_resulting_text_too_long));
   //when not inside a try/catch set gotInterruptG to break out of any loop
   if (trylevel == 0)
       gotInterruptG = true;
}

//":retab".
pub void
c_retab(Invocation *eap) {
   LineNr   lnum;
   int      got_tab = false;
   long   num_spaces = 0;
   long   num_tabs;
   long   len;
   long   col;
   long   vcol;
   long   start_col = 0;      //For start of white-space string
   long   start_vcol = 0;      //For start of white-space string
   long   old_len;
   long   new_len;
   Byte   *ptr;
   CS new_line = (CS)1; //init to non-NULL
   int      did_undo;      //called u_save for current line
   int      temp;
   int      new_ts = 0;
   int      save_list;
   LineNr   first_line = 0;      //first changed line
   LineNr   last_line = 0;      //last changed line
   int      is_indent_only = 0;   //Only process leading whitespace

   save_list = curPor->o.list;
   curPor->o.list = 0;       //don't want list mode here

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
      did_undo = false;
      for (;;) {
         if (SPACE_OR_TAB(ptr[col])) {
              if (!got_tab && num_spaces == 0) {
                 //First consecutive white-space
                 start_vcol = vcol;
                 start_col = col;
              }
              if (ptr[col] == ' ')
                 num_spaces++;
              else
                 got_tab = true;
          } else {
              if (got_tab || (eap->forceit && num_spaces > 1)) {
                  //Retabulate this string of white-space

                  //len is virtual length of white string
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
                 if (did_undo == false) {
                    did_undo = true;
                    if (u_save((LineNr)(lnum - 1), (LineNr)(lnum + 1)) == FAIL) {
                       new_line = NULL;   //flag out-of-memory
                       break;
                    }
                 }

                 //len is actual number of white characters used
                 len = num_spaces + num_tabs;
                 new_len = old_len - col + start_col + len + 1;
                 if (new_len <= 0 || new_len >= MAXCOL) {
                    emsg_text_too_long();
                    break;
                 }
                 new_line = alloc(new_len);
                 if (start_col > 0)
                    MEMMOVE(new_line, ptr, (Unt)start_col);
                 MEMMOVE(new_line + start_col + len, ptr + col, (Unt)(old_len - col + 1));
                 ptr = new_line + start_col;
                 for (col = 0; col < len; col++)
                    ptr[col] = (col < num_tabs) ? '\t' : ' ';
                 if (ml_replace(lnum, new_line, false) == OK)
                    //"new_line" may have been copied
                    new_line = curBook->mem.cachedLine;
                 if (first_line == 0)
                    first_line = lnum;
                 last_line = lnum;
                 ptr = new_line;
                 old_len = new_len - 1;
                 col = start_col + len;
                  }
              }
              got_tab = false;
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
      if (new_line == NULL)          //out of memory
         break;
      line_breakcheck();
   }
   if (gotInterruptG)
      emsg(_(e_interrupted));

   if (curBook->o.shiftWidth != new_ts)
      drawCurBookLater(UPD_NOT_VALID);
   if (first_line != 0)
      doChangedLines(first_line, 0, last_line + 1, 0L);

   curPor->o.list = save_list;   //restore 'list'

   curBook->o.shiftWidth = new_ts;
   coladvance(curPor->cursWant);

   u_clearline();
}

//Get indent level from @indentexpr
pub int
get_expr_indent(void) {
   int      indent = -1;
   ScriptPos save_sctx = scriptPosG;

   //Save and restore cursor position and curswant, in case it was changed via :normal commands
   Pos save_pos = curPor->cursor;
   ColNr save_curswant = curPor->cursWant;
   Boole save_set_curswant = curPor->setCursWant;
   ++textlock;
   scriptPosG = curBook->o.scriptLocs[BOOK_indentExpr];

   //Need to make a copy, the @indentexpr option could be changed while evaluating it.
   CS inde_copy = copyStr(curBook->o.indentExpr);
   if (inde_copy) {
      indent = (int)eval_to_number(inde_copy, true);
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

   //Reset did_throw, unless 'debug' has "throw" and inside a try/catch.
   if (did_throw && ((p_debug && firstOccurrence(p_debug, 't') == NULL) || trylevel == 0)) {
      handle_did_throw();
      did_throw = false;
   }

   //If there is an error, just keep the current indent.
   if (indent < 0)
      indent = get_indent();

   return indent;
}

//Re-indent the current line, based on the current contents of it and the surrounding lines. 
//Fixing the cursor position seems really easy -- I'm very confused what all the part that 
//handles Control-T is doing that I'm not. "get_the_indent" should be get_c_indent 
//or get_expr_indent
private void
fixthisline(int (*get_the_indent)(void)) {
   int amount = get_the_indent();

   if (amount < 0)
      return;

   opChangeIndent(INDENT_SET, amount, 0, true);
   if (linewhite(curPor->cursor.lnum))
      didAindentG = true;   //delete the indent if the line stays empty
}


//true if current book has expression-based indenting.
pub Boole
doIsIndentationExpressionBased(void) {
   return curBook->o.indentExpr != null;
}

//Fix indent for 'expr' indentation
pub void
fix_indent(void) {
   if (doIsIndentationExpressionBased())
      do_expr_indent();
}

pub void
f_indent(Arr(Var) argVars, OUT Var* returnVar) {
   LineNr lnum = tv_get_lnum(argVars);
   if (lnum >= 1 && lnum <= curBook->mem.lineCount)
      returnVar->number = get_indent_lnum(lnum);
   else {
      returnVar->number = -1;
   }
}

//true if the string "line" starts with a word from @cinwords
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

   //We loop because strings may be concatenated: "date""time".
   for ( ; ; ++p) {
      if (p[0] == '\'') {         //'c' or '\n' or '\000'
         if (p[1] == ZERO)          //' at end of line
            break;
         i = 2;
         if (p[1] == '\\' && p[2] != ZERO) {   //'\n' or '\000'
            ++i;
         while (eeIsDigit(p[i - 1]))   //'\000'
             ++i;
         }
         if (p[i - 1] != ZERO && p[i] == '\'') {   //check for trailing '
            p += i;
            continue;
         }
      } ei (p[0] == '"') {        //start of string
         for (++p; p[0]; ++p) {
            if (p[0] == '\\' && p[1] != ZERO)
               ++p;
            ei (p[0] == '"')       //end of string
               break;
         }
         if (p[0] == '"')
            continue; //continue for another string
      } ei (p[0] == 'R' && p[1] == '"') {
         //Raw string: R"[delim](...)[delim]"
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
               continue; //continue for another string
         }
      }
      break;                //no string found
   }
   if (!*p)
      --p;                //backup from ZERO
   return p;
}


//true if "line[col]" is inside a C string.
pub int
is_pos_in_string(CS line, ColNr col) {
   CS p;

   for (p = line; *p && (ColNr)(p - line) < col; ++p)
      p = skipStringLiteral(p);
   return !((ColNr)(p - line) <= col);
}

pub Pos*
find_start_comment(int ind_maxcomment)   {//XXX
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

//Do expression indenting on the current line.
pub void
do_expr_indent(void) {
   if (*curBook->o.indentExpr != ZERO)
      fixthisline(&get_expr_indent);
}

//}}}
