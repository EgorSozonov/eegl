//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## window.c: functions for displaying the window in Wayland

#include "eegl.h"
#include "h/data.types.h"
#include "h/data.h"
#include "h/channel.types.h"
#include "h/channel.h"
#include "h/book.h"
#include "h/location.types.h"
#include "h/location.h"
#include "h/hilite.h"
#include "h/memory.h"
#include "h/window.h"
#include "h/do.h"
#include "h/draw.types.h"
#include "h/draw.h"
#include "h/eval.h"
#include "h/fileio.h"
#include "h/input.types.h"
#include "h/input.h"
#include "h/juggle.h"
#include "h/message.h"
#include "h/motor.h"
#include "h/normal.h"
#include "h/insert.h"
#include "h/portal.h"
#include "h/search.h"
#include "h/script.h"
#include "h/strings.h"
#include "h/term.h"
#include "h/ui.h"

// for shm_open:
#include <sys/mman.h>
#include <sys/select.h>
#include <fcntl.h>

//{{{@@forward declarations
private YankReg * getYRegister(int reg);
private CS get_expr_line_src(void);
private int stuff_yank(int regname, CS p);
private CS execreg_line_continuation(Arr(Text) lines, long *idx);
private void put_reedit_in_typeBufG(int silent);
private int put_in_typeBufG(
    CS s,
    int      esc,
    int      colon,       // add ':' before the line
    int      silent)
;
private void free_yank(long n);
private int yank_copy_line(BlockDef* bd, long y_idx, int exclude_trailing_space);
private void copy_yank_reg(YankReg *reg);
private void dis_msg(
   Byte   *p,
   int      skip_esc       // if true, ignore trailing ESC
);
private void writeToRegister(
   OUT YankReg* yReg,    // pointer to yank register
   Unt yank_type, //MCHAR, MLINE, MBLOCK, MAUTO
   CS str,        //string to put in register
   Long len,      //length of string
   Long blocklen, //width of Visual block
   int str_list   //true iff str is a CString
);
private CS getreg_wrap_one_line(CS s, int flags);
private int init_write_reg(
   int name,
   YankReg** old_y_previous,
   YankReg** old_y_current,
   int must_append,
   Unt*
);
private void finish_write_reg(int name, YankReg* old_y_previous, YankReg* old_y_current);
private void clip_yank_selection(int type, CS str, Long len);
private void copyToClipboard();
private void clip_wl_set_selection(ClipBoard *);
private int copy0();
private int paste0();
//}}}
//{{{copy-and-paste registers

//Registers:
//  0 = unnamed register, for normal yanks and puts
//  1..9 = registers '1' to '9', for deletes
//10..35 = registers 'a' to 'z' ('A' to 'Z' for appending)
//    36 = delete register '-'
//    37 = Selection register '*'.
//    38 = Clipboard register '+'.
private YankReg   y_regs[NUM_REGISTERS];

private YankReg   *y_current;       // ptr to current yankreg
private int      y_append;       // true when appending
private YankReg   *y_previous = NULL; // ptr to last written yankreg

pub YankReg *
get_y_regs(void) {
   return y_regs;
}

private YankReg *
getYRegister(int reg) {
   return &y_regs[reg];
}

pub YankReg *
get_y_current(void) {
   return y_current;
}

pub YankReg *
get_y_previous(void) {
   return y_previous;
}

pub void
set_y_current(YankReg *yreg) {
   y_current = yreg;
}

pub void
set_y_previous(YankReg *yreg) {
   y_previous = yreg;
}

pub void
reset_y_append(void) {
   y_append = false;
}


//Keep the last expression line here, for repeating.
private Byte   *expr_line = NULL;
private Invocation   *exprInvoS = NULL;

//Get an expression for the "\"=expr1" or "CTRL-R =expr1" Return '=' when OK, ZERO otherwise.
pub int
get_expr_register(void) {
   CS new_line = getCommline('=', 0L, 0, 0);
   if (!new_line)
      return ZERO;
   if (*new_line == ZERO)   // use previous line
      eeglFree(new_line);
   else
      set_expr_line(new_line, NULL);
   return '=';
}

//Set the expression for the '=' register. Argument must be an allocated string.
//"invo" may be used if the next line needs to be checked when evaluating the expression.
pub void
set_expr_line(CS new_line, Invocation* invo) {
   eeglFree(expr_line);
   expr_line = new_line;
   exprInvoS = invo;
}

//Get the result of the '=' register expression.
//Return a pointer to allocated memory, or NULL for failure.
pub CS
get_expr_line(void) {
   Byte   *expr_copy;
   Byte   *rv;
   static int   nested = 0;

   if (!expr_line)
      return NULL;

   // Make a copy of the expression, because evaluating it may cause it to be changed.
   expr_copy = copyStr(expr_line);

   //When we are invoked recursively limit the evaluation to 10 levels. Then return the string as-is
   if (nested >= 10)
      return expr_copy;

   ++nested;
   rv = evalToStringWithInvo(expr_copy, true, exprInvoS, false);
   --nested;
   eeglFree(expr_copy);
   return rv;
}

//Get the '=' register expression itself, without evaluating it.
private CS
get_expr_line_src(void) {
   if (!expr_line)
      return NULL;
   return copyStr(expr_line);
}

//Check if 'regname' is a valid name of a yank register.
//Note: There is no check for 0 (default register), caller should do this
pub int
valid_yank_reg(int regname, Boole writing) {      // if true check for writable registers
   if (      (regname > 0 && ASCII_ISALNUM(regname))
          || (!writing && firstOccurrence((CS)"/.%:=", regname) != NULL)
          || regname == '#'
          || regname == '"'
          || regname == '-'
          || regname == '_'
          || regname == '*'
          || regname == '+'
          || (!writing && regname == '~')
                        )
      return true;
   // clipboard support not enabled in this build
   ei (regname == '*' || regname == '+') {
      // Warn about missing clipboard support once
      msg_warn_missing_clipboard();
      return false;
   }
   return false;
}

//Set y_current and y_append, according to the value of "regname".
//Cannot handle the '_' register.
//Must only be called with a valid register name!
//
//If regname is 0 and writing, use register 0. If regname is 0 and reading, use previous register
//
//Return true when the register should be inserted literally (selection or clipboard).
pub int
get_yank_register(int regname, int writing) {
   int       i;
   int       ret = false;

   y_append = false;
   if ((regname == 0 || regname == '"') && !writing && y_previous != NULL) {
      y_current = y_previous;
      return ret;
   }
   i = regname;
   if (EE_ISDIGIT(i))
      i -= '0';
   ei (ASCII_ISLOWER(i))
      i = i - 'a' + 10;
   ei (ASCII_ISUPPER(i)) {
      i = i - 'A' + 10;
      y_append = true;
   } ei (regname == '-')
      i = DELETION_REGISTER;
   //When selection is not available, use register 0 instead of '*'
   ei (regname == '*') {
      i = STAR_REGISTER;
      ret = true;
   }
   // When clipboard is not available, use register 0 instead of '+'
   ei (regname == '+') {
      i = PLUS_REGISTER;
      ret = true;
   } ei (!writing && regname == '~')
      i = TILDE_REGISTER;
   else      // not 0-9, a-z, A-Z or '-': use register 0
      i = 0;
   y_current = &(y_regs[i]);
   if (writing)   // remember the register we write into for do_put()
      y_previous = y_current;
   return ret;
}

//Obtain the contents of a "normal" register. The register is made empty.
//The returned pointer has allocated memory, use put_register() later.
pub void *
get_register(Unt name, int copy) {  // make a copy, if false make register empty.
   // When Visual area changed, may have to update selection. Obtain the selection too.
   if (name == '*') {
      copyToClipboard();
   }

   get_yank_register(name, 0);
   YankReg* reg = ALLOC_ONE(YankReg);
   *reg = *y_current;
   if (copy) {
      // If we run out of memory some or all of the lines are empty.
      if (reg->y_size == 0 || y_current->y_array == NULL)
         reg->y_array = NULL;
      else
         reg->y_array = ALLOC_MULT(Text, reg->y_size);
      if (reg->y_array != NULL) {
         for (int i = 0; i < reg->y_size; ++i) {
            reg->y_array[i].c = copySubstr(
                  y_current->y_array[i].c, y_current->y_array[i].len
            );
            reg->y_array[i].len = y_current->y_array[i].len;
         }
      }
   }
   else
      y_current->y_array = NULL;
   return (void *)reg;
}

// Put "reg" into register "name".  Free any previous contents and "reg".
pub void
put_register(int name, void *reg) {
   get_yank_register(name, 0);
   free_yank_all();
   *y_current = *(YankReg *)reg;
   eeglFree(reg);
}

pub void
free_register(void *reg) {
    YankReg tmp;

    tmp = *y_current;
    *y_current = *(YankReg *)reg;
    free_yank_all();
    eeglFree(reg);
    *y_current = tmp;
}

// return true if the current yank register has type MLINE
pub int
yank_register_mline(int regname) {
   if (regname != 0 && !valid_yank_reg(regname, false))
      return false;
   if (regname == '_')      // black hole is always empty
      return false;
   get_yank_register(regname, false);
   return (y_current->y_type == MLINE);
}

// Start or stop recording into a yank register. Return FAIL for failure, OK otherwise.
pub int
do_record(int c) {
   Byte       *p;
   static int       regname;
   YankReg       *old_y_previous, *old_y_current;
   int          retval;

   if (reg_recording == 0) {      // start recording
      // registers 0-9, a-z and " are allowed
      if (c < 0 || (!ASCII_ISALNUM(c) && c != '"'))
         retval = FAIL;
      else {
         reg_recording = c;
         showmode();
         regname = c;
         retval = OK;
      }
   } else {       // stop recording
      // Get the recorded key hits.  K_SPECIAL and CSI will be escaped, this
      // needs to be removed again to put it in a register.  exec_reg then
      // adds the escaping back later.
      reg_recording = 0;
      msg(S"");
      p = get_recorded();
      if (!p)
         retval = FAIL;
      else {
         // Remove escaping for CSI and K_SPECIAL in multi-byte chars.
         eeUnescapeCsi(p);

         // We don't want to change the default register here, so save and
         // restore the current register name.
         old_y_previous = y_previous;
         old_y_current = y_current;

         retval = stuff_yank(regname, p);

         y_previous = old_y_previous;
         y_current = old_y_current;
      }
   }
   return retval;
}

//Stuff string "p" into yank register "regname" as a single line (append if
//uppercase).   "p" must have been alloced.
//
//return FAIL for failure, OK otherwise
private int
stuff_yank(int regname, CS p) {
   // check for read-only register
   if (regname != 0 && !valid_yank_reg(regname, true)) {
      eeglFree(p);
      return FAIL;
   }
   if (regname == '_') {        // black hole: don't do anything
      eeglFree(p);
      return OK;
   }

   Unt plen = STRLEN(p);
   get_yank_register(regname, true);
   if (y_append && y_current->y_array != NULL) {
      Text    *pp;
      Byte       *tmp;
      Unt       tmplen;

      pp = &(y_current->y_array[y_current->y_size - 1]);
      tmplen = pp->len + plen;
      tmp = alloc(tmplen + 1);
      STRCPY(tmp, pp->c);
      STRCPY(tmp + pp->len, p);
      eeglFree(p);
      eeglFree(pp->c);
      pp->c = tmp;
      pp->len = tmplen;
   } else {
      free_yank_all();
      if ((y_current->y_array = ALLOC_ONE(Text)) == NULL) {
         eeglFree(p);
         return FAIL;
      }
      y_current->y_array[0].c = p;
      y_current->y_array[0].len = plen;
      y_current->y_size = 1;
      y_current->y_type = MCHAR;  // used to be MLINE, why?
      y_current->y_time_set = eeTime();
   }
   return OK;
}

// Last executed register (@ command)
private int execreg_lastc = ZERO;

pub int
get_execreg_lastc(void) {
   return execreg_lastc;
}

pub void
set_execreg_lastc(int lastc) {
   execreg_lastc = lastc;
}

/*
 * When executing a register as a series of ex-commands, if the
 * line-continuation character is used for a line, then join it with one or
 * more previous lines. Note that lines are processed backwards starting from
 * the last line in the register.
 *
 * Arguments:
 *   lines - list of lines in the register
 *   idx - index of the line starting with \ or "\. Join this line with all the
 *      immediate predecessor lines that start with a \ and the first line
 *      that doesn't start with a \. Lines that start with a comment "\
 *      character are ignored.
 *
 * Returns the concatenated line. The index of the line that should be
 * processed next is returned in idx.
 */
private CS
execreg_line_continuation(Arr(Text) lines, long *idx) {
   ArrayList   ga;
   long   i = *idx;
   Byte   *p;
   int      cmd_start;
   int      cmd_end = i;
   int      j;

   ga_init2(&ga, sizeof(Byte), 400);

   // search backwards to find the first line of this command.
   // Any line not starting with \ or "\ is the start of the command.
   while (--i > 0) {
      p = skipwhite(lines[i].c);
      if (*p != '\\' && (p[0] != '"' || p[1] != '\\' || p[2] != ' '))
          break;
   }
   cmd_start = i;

   // join all the lines
   ga_concat(&ga, lines[cmd_start].c);
   for (j = cmd_start + 1; j <= cmd_end; j++) {
      p = skipwhite(lines[j].c);
      if (*p == '\\') {
         // Adjust the growsize to the current length to
         // speed up concatenating many lines.
         if (ga.len > 400) {
            if (ga.len > 8000)
               ga.ga_growsize = 8000;
            else
               ga.ga_growsize = ga.len;
         }
         ga_concat(&ga, p + 1);
      }
   }
   ga_append(&ga, ZERO);
   CS retVal = copySubstr(ga.c, ga.len);
   ga_clear(&ga);

   *idx = i;
   return retVal;
}

//Execute a yank register: copy it into the stuff buffer.
//
//Return FAIL for failure, OK otherwise.
pub int
do_execreg(
    int       regname,
    int       colon,      // insert ':' before each line
    int       addcr,      // always add '\n' to end of line
    int       silent)      // set "silent" flag in typeahead buffer
{
    long   i;
    Byte   *p;
    int      retval = OK;
    int      remap;

   // repeat previous one
   if (regname == '@') {
      if (execreg_lastc == ZERO) {
         emsg(_(e_no_previously_used_register));
         return FAIL;
      }
      regname = execreg_lastc;
   }
   // check for valid regname
   if (regname == '%' || regname == '#' || !valid_yank_reg(regname, false)) {
      emsg_invreg(regname);
      return FAIL;
   }
   execreg_lastc = regname;

   regname = may_get_selection(regname);

   // black hole: don't stuff anything
   if (regname == '_')
      return OK;

    // use last command line
   if (regname == ':') {
      if (lastCommlineG == NULL) {
          emsg(_(e_no_previous_command_line));
          return FAIL;
      }
      // don't keep the cmdline containing @:
      EE_CLEAR(newLastCommlineG);
      // Escape all control characters with a CTRL-V
      p = copyStr_escaped_ext(
            lastCommlineG,
            S"\001\002\003\004\005\006\007" "\010\011\012\013\014\015\016\017"
             "\020\021\022\023\024\025\026\027" "\030\031\032\033\034\035\036\037",
            Ctrl_V, false, null
      );
      if (p != NULL) {
          // When in Visual mode "'<,'>" will be prepended to the command.
          // Remove it when it's already there.
          if (VIsual_active && STRNCMP(p, "'<,'>", 5) == 0)
         retval = put_in_typeBufG(p + 5, true, true, silent);
          else
         retval = put_in_typeBufG(p, true, true, silent);
      }
      eeglFree(p);
   } ei (regname == '=') {
      p = get_expr_line();
      if (p == NULL)
          return FAIL;
      retval = put_in_typeBufG(p, true, colon, silent);
      eeglFree(p);
    } ei (regname == '.') {      // use last inserted text
      p = get_last_insert_save();
      if (p == NULL)    {
         emsg(_(e_no_inserted_text_yet));
         return FAIL;
   }
   retval = put_in_typeBufG(p, false, colon, silent);
   eeglFree(p);
   } else {
      get_yank_register(regname, false);
      if (y_current->y_array == NULL)
          return FAIL;

      // Disallow remapping for ":@r".
      remap = colon ? REMAP_NONE : REMAP_YES;

      // Insert lines into typeahead buffer, from last one to first one.
      put_reedit_in_typeBufG(silent);
      for (i = y_current->y_size; --i >= 0; ) {
         CS escaped;
         CS str;
         int free_str = false;

         // insert NL between lines and after last line if type is MLINE
         if (y_current->y_type == MLINE || i < y_current->y_size - 1 || addcr) {
            if (insertIntoTypebuf((CS)"\n", remap, 0, true, silent) == FAIL)
               return FAIL;
         }

         // Handle line-continuation for :@<register>
         str = y_current->y_array[i].c;
         if (colon && i > 0) {
            p = skipwhite(str);
            if (*p == '\\' || (p[0] == '"' && p[1] == '\\' && p[2] == ' ')) {
               str = execreg_line_continuation(y_current->y_array, &i);
               if (str == NULL)
                  return FAIL;
               free_str = true;
            }
         }
         escaped = copyStr_escape_csi(str);
         if (free_str)
            eeglFree(str);
         retval = insertIntoTypebuf(escaped, remap, 0, true, silent);
         eeglFree(escaped);
         if (retval == FAIL)
            return FAIL;
         if (colon && insertIntoTypebuf((CS)":", remap, 0, true, silent) == FAIL)
            return FAIL;
      }
      reg_executing = regname == 0 ? '"' : regname; // disable "q" command
      pending_end_reg_executing = false;
   }
   return retval;
}

//If "restart_edit" is not zero, put it in the typeahead buffer, so that it's
//used only after other typeahead has been processed.
private void
put_reedit_in_typeBufG(int silent) {
   Byte   buf[3];

   if (restart_edit == ZERO)
      return;

   if (restart_edit == 'V') {
      buf[0] = 'g';
      buf[1] = 'R';
      buf[2] = ZERO;
   } else {
      buf[0] = restart_edit == 'I' ? 'i' : restart_edit;
      buf[1] = ZERO;
   }
   if (insertIntoTypebuf(buf, REMAP_NONE, 0, true, silent) == OK)
      restart_edit = ZERO;
}

//Insert register contents "s" into the typeahead buffer, so that it will be executed again.
//When "esc" is true it is to be taken literally: Escape CSI characters and no remapping.
private int
put_in_typeBufG(
    CS s,
    int      esc,
    int      colon,       // add ':' before the line
    int      silent)
{
    int      retval = OK;

   put_reedit_in_typeBufG(silent);
   if (colon)
      retval = insertIntoTypebuf((CS)"\n", REMAP_NONE, 0, true, silent);
   if (retval == OK) {
      Byte   *p;

      if (esc)
         p = copyStr_escape_csi(s);
      else
         p = s;
      if (p == NULL)
         retval = FAIL;
      else
         retval = insertIntoTypebuf(p, esc ? REMAP_NONE : REMAP_YES, 0, true, silent);
      if (esc)
         eeglFree(p);
   }
   if (colon && retval == OK)
       retval = insertIntoTypebuf((CS)":", REMAP_NONE, 0, true, silent);
   return retval;
}

//Insert a yank register: copy it into the Read buffer.
//Used by CTRL-R command and middle mouse button in insert mode.
//
//return FAIL for failure, OK otherwise
pub int
insert_reg(Unt regname, int literally_arg) {  // insert literally, not as if typed
   Long i;
   int retval = OK;
   Byte   *arg;
   int allocated;
   int literally = literally_arg;

   // It is possible to get into an endless loop by having CTRL-R a in
   // register a and then, in insert mode, doing CTRL-R a.
   // If you hit CTRL-C, the loop will be broken here.
   ui_breakcheck();
   if (gotInterruptG)
      return FAIL;

   // check for valid regname
   if (regname != ZERO && !valid_yank_reg(regname, false))
      return FAIL;

   regname = may_get_selection(regname);

   if (regname == '.')         // insert last inserted text
      retval = stuff_inserted(ZERO, 1L, true);
   ei (get_spec_reg(regname, &arg, &allocated, true)) {
      if (!arg)
         return FAIL;
      stuffescaped(arg, literally);
      if (allocated)
         eeglFree(arg);
   } else {           // name or number register
      if (get_yank_register(regname, false))
         literally = true;
      if (y_current->y_array == NULL)
         retval = FAIL;
      else {
         for (i = 0; i < y_current->y_size; ++i) {
            if (regname == '-' && y_current->y_type == MCHAR) {
               int dir = BACKWARD;

               AppendCharToRedobuff(Ctrl_R);
               AppendCharToRedobuff(regname);
               do_put(regname, NULL, dir, 1L, PUT_CURSEND);
            } else {
               stuffescaped(y_current->y_array[i].c, literally);
               // Insert a newline between lines and after last line if
               // y_type is MLINE.
               if (y_current->y_type == MLINE || i < y_current->y_size - 1)
                  stuffcharReadbuff('\n');
            }
         }
      }
   }

   return retval;
}

//If "regname" is a special register, return true and store a pointer to its value in "retVal".
pub int
get_spec_reg(
   int regname,
   OUT CS* retVal,
   int* allocated,   // return: true when value was allocated
   int errmsg      // give error message when failing
){
   int cnt;

   *retVal = S"";
   *allocated = false;
   switch (regname) {
   case '%':      // file name
      if (errmsg)
         check_fname();   // will give emsg if not set
      *retVal = curBook->currFileName;
      return true;

   case '#':      // alternate file name
      *retVal = getaltfname(errmsg);   // may give emsg if not set
      return true;

   case '=':      // result of expression
      *retVal = get_expr_line();
      *allocated = true;
      return true;

   case ':':      // last command line
      if (!lastCommlineG && errmsg)
         emsg(_(e_no_previous_command_line));
      *retVal = lastCommlineG != S"" ? lastCommlineG : S"";
      return true;

   case '/':      // last search-pattern
      CS lastPat = last_search_pat().c;
      if (!lastPat && errmsg)
         emsg(_(e_no_previous_regular_expression));
      *retVal = lastPat ? lastPat : S"";
      return true;

   case '.':      // last inserted text
      *retVal = get_last_insert_save();
      *allocated = true;
      if (*retVal == S"" && errmsg)
         emsg(_(e_no_inserted_text_yet));
      return true;

   case Ctrl_F:      // Filename under cursor
   case Ctrl_P:      // Path under cursor, expand via "path"
      if (!errmsg)
         return false;
      *retVal = file_name_at_cursor(
            FNAME_MESS | FNAME_HYP | (regname == Ctrl_P ? FNAME_EXP : 0), 1L, NULL
      );
      *allocated = true;
      return true;

   case Ctrl_W:      // word under cursor
   case Ctrl_A:      // WORD (mnemonic All) under cursor
       if (!errmsg)
      return false;
       cnt = find_ident_under_cursor(retVal, regname == Ctrl_W
               ?  (FIND_IDENT|FIND_STRING) : FIND_STRING);
       *retVal = cnt ? copySubstr(*retVal, cnt) : S"";
       *allocated = true;
       return true;

   case Ctrl_L:      // Line under cursor
      if (!errmsg)
         return false;

      *retVal = memGetLine(curPor->book,
         curPor->cursor.lnum, false);
       return true;

   case '_':      // black hole: always empty
       *retVal = (CS)"";
       return true;
   }

   return false;
}

//Paste a yank register into the command line. Only for non-special registers.
//Used by CTRL-R command in command-line mode.
//insert_reg() can't be used here, because special characters from the
//register contents will be interpreted as commands.
//return FAIL for failure, OK otherwise
pub int
cmdline_paste_reg(
   int regname,
   int literally_arg,   // Insert text literally instead of "as typed"
   int remcr      // don't add CR characters
){
   long   i;
   int      literally = literally_arg;

   if (get_yank_register(regname, false))
      literally = true;
   if (y_current->y_array == NULL)
      return FAIL;

   for (i = 0; i < y_current->y_size; ++i) {
      cmdline_paste_str(y_current->y_array[i].c, literally);

      // Insert ^M between lines and after last line if type is MLINE.
      // Don't do this when "remcr" is true.
      if ((y_current->y_type == MLINE || i < y_current->y_size - 1) && !remcr)
          cmdline_paste_str((CS)"\r", literally);

      // Check for CTRL-C, in case someone tries to paste a few thousand
      // lines and gets bored.
      ui_breakcheck();
      if (gotInterruptG)
          return FAIL;
   }
   return OK;
}

//Shift the delete registers: "9 is cleared, "8 becomes "9, etc.
pub void
shift_delete_registers(void) {
   y_current = &y_regs[9];
   free_yank_all();         // free register nine
   for (int n = 9; n > 1; --n)
      y_regs[n] = y_regs[n - 1];
   y_current = &y_regs[1];
   if (!y_append)
      y_previous = y_current;
   y_regs[1].y_array = NULL;      // set register one to empty
}

pub void
yank_do_autocmd(Operator* opArg, YankReg *reg) {
   static int recursive = false;
   Byte buf[NUMBUFLEN + 2];
   long reglen = 0;
   SaveVEvent save_v_event;

   if (recursive)
      return;

   Bag* v_event = get_v_event(&save_v_event);

   List* list = list_alloc();

   // yanked text contents
   for (int n = 0; n < reg->y_size; n++)
      list_append_string(list, reg->y_array[n].c, -1);
   list->lock = VAR_FIXED;
   (void)bagAddList(v_event, S"regcontents", list);

   // register name or empty string for unnamed operation
   buf[0] = (Byte)opArg->regname;
   buf[1] = ZERO;
   (void)bagAddString(v_event, S"regname", buf);

   // motion type: inclusive or exclusive
   (void)bagAdd_bool(v_event, S"inclusive", opArg->inclusive);

   // kind of operation (yank, delete, change)
   buf[0] = get_op_char(opArg->opTy);
   buf[1] = get_extra_op_char(opArg->opTy);
   buf[2] = ZERO;
   (void)bagAddString(v_event, S"operator", buf);

   // register type
   buf[0] = ZERO;
   buf[1] = ZERO;
   switch (get_reg_type(opArg->regname, &reglen)) {
   case MLINE: buf[0] = 'V'; break;
   case MCHAR: buf[0] = 'v'; break;
   case MBLOCK:
      eeSnprintf(buf, sizeof(buf), "%c%ld", Ctrl_V, reglen + 1);
      break;
   }
   (void)bagAddString(v_event, S"regtype", buf);

   // selection type - visual or not
   (void)bagAdd_bool(v_event, S"visual", opArg->is_VIsual);

   // Lock the dictionary and its keys
   bagSetItemsRo(v_event);

   recursive = true;
   textlock++;
   applyAutocomms(EVENT_TEXTYANKPOST, NULL, NULL, false, curBook);
   textlock--;
   recursive = false;

   // Empty the dictionary, v:event is still valid
   restore_v_event(v_event, &save_v_event);
}

// set all the yank registers to empty (called from main())
pub void
init_yank(void) {
   for (int i = 0; i < NUM_REGISTERS; ++i)
      y_regs[i].y_array = NULL;
}

#if defined(EXITFREE)
pub void
clear_registers(void) {
   for (int i = 0; i < NUM_REGISTERS; ++i) {
      y_current = &y_regs[i];
      if (y_current->y_array != NULL)
         free_yank_all();
   }
}
#endif

//Free "n" lines from the current yank register. Called for normal freeing and in case of error.
private void
free_yank(long n) {
   if (y_current->y_array == NULL)
      return;

   for (long i = n; --i >= 0; )
      EE_CLEAR_STRING(y_current->y_array[i]);
   EE_CLEAR(y_current->y_array);
}

pub void
free_yank_all(void) {
   free_yank(y_current->y_size);
}

//Yank the text between "opArg->start" and "opArg->end" into a yank register. If we are to append 
//(uppercase register), we first yank into a new yank register and then concatenate the old and 
//the new one (so we keep the old one in case of out-of-memory).
//
//Return FAIL for failure, OK otherwise.
pub int
op_yank(Operator *opArg, int deleting, Boole mess) {
   long y_idx;      // index in y_array[]
   YankReg newreg;      // new yank register when appending
   LineNr lnum;      // current line number
   int yanktype = opArg->motion_type;
   Long yanklines = opArg->line_count;
   LineNr yankendlnum = opArg->end.lnum;
   Byte* pnew;
   BlockDef bd;

                // check for read-only register
   if (opArg->regname != 0 && !valid_yank_reg(opArg->regname, true)) {
      beep_flush();
      return FAIL;
   }
   if (opArg->regname == '_')       // black hole: nothing to do
      return OK;

   if (!deleting)          // op_delete() already set y_current
      get_yank_register(opArg->regname, true);

   YankReg* curr = y_current;
                // append to existing contents
   if (y_append && y_current->y_array != NULL)
      y_current = &newreg;
   else
      free_yank_all();       // free previously yanked lines

   //If the cursor was in column 1 before and after the movement, and the
   //operator is not inclusive, the yank is always linewise.
   if (     opArg->motion_type == MCHAR
         && opArg->start.col == 0
         && !opArg->inclusive
         && !opArg->is_VIsual
         && !opArg->block_mode
         && opArg->end.col == 0
         && yanklines > 1
   ) {
      yanktype = MLINE;
      --yankendlnum;
      --yanklines;
   }

   y_current->y_size = yanklines;
   y_current->y_type = yanktype;   // set the yank register type
   y_current->y_width = 0;
   y_current->y_array = lallocZeroed(sizeof(Text)* yanklines, true);
   y_current->y_time_set = eeTime();

   y_idx = 0;
   lnum = opArg->start.lnum;

   if (opArg->block_mode) {
      // Visual block mode
      y_current->y_type = MBLOCK;       // set the yank register type
      y_current->y_width = opArg->end_vcol - opArg->start_vcol;

      if (curPor->cursWant == MAXCOL && y_current->y_width > 0)
         y_current->y_width--;
   }

   for ( ; lnum <= yankendlnum; lnum++, y_idx++) {
      switch (y_current->y_type) {
      case MBLOCK:
         block_prep(opArg, OUT &bd, lnum, false);
         if (yank_copy_line(&bd, y_idx, opArg->excludeTrailingWhitespace) == FAIL)
            goto fail;
         break;

      case MLINE:
         y_current->y_array[y_idx].len = ml_get_len(lnum);
         if ((y_current->y_array[y_idx].c = copySubstr(ml_get(lnum),
                  y_current->y_array[y_idx].len)) == NULL
         ) {
            EE_CLEAR_STRING(y_current->y_array[y_idx]);
            goto fail;
         }
         break;

      case MCHAR: {
            int tmp;

            jugCharwiseBlockPrep(opArg->start, opArg->end, &bd, lnum, opArg->inclusive);

            // make sure bd.textlen is not longer than the text
            tmp = (int)STRLEN(bd.textstart);
            if (tmp < bd.textlen)
               bd.textlen = tmp;

            if (yank_copy_line(&bd, y_idx, false) == FAIL)
               goto fail;
            break;
         }
         // NOTREACHED
      }
   }

   if (curr != y_current) {  // append the new block to the old block
      Text *new_ptr;
      long j;

      new_ptr = ALLOC_MULT(Text, curr->y_size + y_current->y_size);
      for (j = 0; j < curr->y_size; ++j)
         new_ptr[j] = curr->y_array[j];
      eeglFree(curr->y_array);
      curr->y_array = new_ptr;
      curr->y_time_set = eeTime();

      if (yanktype == MLINE)   // MLINE overrides MCHAR and MBLOCK
          curr->y_type = MLINE;

      // Concatenate the last line of the old block with the first line of the new block
      if (curr->y_type == MCHAR) {
         pnew = alloc(curr->y_array[curr->y_size - 1].len + y_current->y_array[0].len + 1);

         --j;
         STRCPY(pnew, curr->y_array[j].c);
         STRCPY(pnew + curr->y_array[j].len, y_current->y_array[0].c);
         eeglFree(curr->y_array[j].c);
         curr->y_array[j].c = pnew;
         curr->y_array[j].len = curr->y_array[j].len + y_current->y_array[0].len;
         ++j;
         EE_CLEAR_STRING(y_current->y_array[0]);
         y_idx = 1;
      } else
         y_idx = 0;
      while (y_idx < y_current->y_size)
         curr->y_array[j++] = y_current->y_array[y_idx++];
      curr->y_size = j;
      eeglFree(y_current->y_array);
      y_current = curr;
    }

   if (mess) {        // Display message about yank?
      if (yanktype == MCHAR && !opArg->block_mode && yanklines == 1)
         yanklines = 0;
      // Some versions of Vi use ">=" here, some don't...
      Byte namebuf[100];

      if (opArg->regname == ZERO)
         *namebuf = ZERO;
      else
         eeSnprintf(namebuf, sizeof(namebuf), _(" into \"%c"), opArg->regname);

      // redisplay now, so message is not deleted
      update_topline_redraw();
      if (opArg->block_mode) {
         smsg(NGETTEXT("block of %ld line yanked%s", "block of %ld lines yanked%s", yanklines),
            yanklines, namebuf);
      } else {
         smsg(NGETTEXT("%ld line yanked%s", "%ld lines yanked%s", yanklines), yanklines, namebuf);
      }
   }

   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0) {
      // Set "'[" and "']" marks.
      curBook->opStart = opArg->start;
      curBook->opEnd = opArg->end;
      if (yanktype == MLINE && !opArg->block_mode) {
         curBook->opStart.col = 0;
         curBook->opEnd.col = MAXCOL;
      }
      if (yanktype != MLINE && !opArg->inclusive)
         // Exclude the end position.
         decl(&curBook->opEnd);
    }

   //If we were yanking to the '*' register, send result to clipboard.
   //If no register was specified, and "unnamed" in 'clipboard', make a copy to the '*' register.
   if ((curr == &(y_regs[STAR_REGISTER]) || (!deleting && opArg->regname == 0))) {
      if (curr != &(y_regs[STAR_REGISTER]))
         // Copy the text from register 0 to the clipboard register.
         copy_yank_reg(&(y_regs[STAR_REGISTER]));
   }

   //If we were yanking to the '+' register, send result to selection.
   //Also copy to the '*' register, in case auto-select is off. But not when
   //'clipboard' has "unnamedplus" and not "unnamed"; and not when
   //deleting and both "unnamedplus" and "unnamed".
   if ((curr == &(y_regs[PLUS_REGISTER]) || (!deleting && opArg->regname == 0))) {
      if (curr != &(y_regs[PLUS_REGISTER]))
         // Copy the text from register 0 to the clipboard register.
         copy_yank_reg(&(y_regs[PLUS_REGISTER]));

   }

   if (!deleting && has_textyankpost())
      yank_do_autocmd(opArg, y_current);
   return OK;

fail:      // free the allocated lines
   free_yank(y_idx + 1);
   y_current = curr;
   return FAIL;
}

//Copy a block range into a register.
//If "exclude_trailing_space" is set, do not copy trailing whitespaces.
private int
yank_copy_line(BlockDef* bd, long y_idx, int exclude_trailing_space) {
   if (exclude_trailing_space)
      bd->endspaces = 0;
   CS pnew = alloc(bd->startspaces + bd->endspaces + bd->textlen + 1);
   y_current->y_array[y_idx].c = pnew;
   memset(pnew, ' ', (Unt)bd->startspaces);
   pnew += bd->startspaces;
   MEMMOVE(pnew, bd->textstart, (Unt)bd->textlen);
   pnew += bd->textlen;
   memset(pnew, ' ', (Unt)bd->endspaces);
   pnew += bd->endspaces;
   if (exclude_trailing_space) {
      int s = bd->textlen + bd->endspaces;

      while (s > 0 && SPACE_OR_TAB(*(bd->textstart + s - 1))) {
         s = s - (*mb_head_off)(bd->textstart, bd->textstart + s - 1) - 1;
          pnew--;
      }
   }
   *pnew = ZERO;

   y_current->y_array[y_idx].len = (Unt)(pnew - y_current->y_array[y_idx].c);

   return OK;
}

//Make a copy of the y_current register to register "reg".
private void
copy_yank_reg(YankReg *reg) {
   YankReg   *curr = y_current;
   y_current = reg;
   free_yank_all();
   *y_current = *curr;
   y_current->y_array = lallocZeroed(sizeof(Text) * y_current->y_size, true);
   for (long j = 0; j < y_current->y_size; ++j) {
       if ((y_current->y_array[j].c = copySubstr(curr->y_array[j].c, curr->y_array[j].len)) 
            == NULL
      ) {
         free_yank(j);
         y_current->y_size = 0;
         break;
      }
      y_current->y_array[j].len = curr->y_array[j].len;
   }
   y_current = curr;
}

//Put contents of register "regname" into the text.
//Caller must check "regname" to be valid!
//"flags": PUT_FIXINDENT   make indent look nice
//      PUT_CURSEND      leave cursor after end of new text
//      PUT_LINE      force linewise put (":put")
//      PUT_BLOCK_INNER     in block mode, do not add trailing spaces
pub void
do_put(
   int      regname,
   CS expr_result,   // result for regname "=" when compiled
   Unt dir,      // BACKWARD for 'P', FORWARD for 'p'
   long   count,
   Unt      flags
) {
   Byte   *ptr;
   Byte   *newp;
   Byte   *oldp;
   int      yanklen;
   int      totlen = 0;      // init for gcc
   LineNr   lnum;
   ColNr   col;
   long   i;         // index in y_array[]
   int      y_type;
   long   y_size;
   int      oldlen;
   long   y_width = 0;
   ColNr   vcol;
   Text* y_array = NULL;
   YankReg   *y_current_used = NULL;
   long   nr_lines = 0;
   int      allocated = false;
   Pos   orig_start = curBook->opStart;
   Pos   orig_end = curBook->opEnd;

   // Adjust register name for "unnamed" in 'clipboard'.
   clipGetDefaultRegister(&regname);
   (void)may_get_selection(regname);
   
   curBook->opStart = curPor->cursor;   // default for '[ mark
   curBook->opEnd = curPor->cursor;   // default for '] mark

   // Using inserted text works differently, because the register includes
   // special characters (newlines, etc.).
   if (regname == '.') {
      if (VIsual_active)
          stuffcharReadbuff(VIsual_mode);
      (void)stuff_inserted((dir == FORWARD ? (count == -1 ? 'o' : 'a') :
                   (count == -1 ? 'O' : 'i')), count, false);
      // Putting the text is done later, so can't really move the cursor to
      // the next character.  Use "l" to simulate it.
      if ((flags & PUT_CURSEND) && gchar_cursor() != ZERO)
          stuffcharReadbuff('l');
      return;
   }

   // For special registers '%' (file name), '#' (alternate file name) and
   // ':' (last command line), etc. we have to create a fake yank register.
   // For compiled code "expr_result" holds the expression result.
   Text insertText = (Text){null, 0};
   if (regname == '=' && expr_result)
      insertText.c = expr_result;
   ei (get_spec_reg(regname, &insertText.c, &allocated, true) && insertText.c == NULL)
      return;

   // Autocommands may be executed when saving lines for undo.  This might
   // make "y_array" invalid, so we start undo now to avoid that.
   if (u_save(curPor->cursor.lnum, curPor->cursor.lnum + 1) == FAIL)
      goto end;

   if (insertText.len != 0) {
      insertText.len = STRLEN(insertText.c);

      y_type = MCHAR;
      if (regname == '=') {
         Unt  ptrlen;
         Byte  *tmp;

         // For the = register we need to split the string at NL
         // characters.
         // Loop twice: count the number of lines and save them.
         for (;;) {
            y_size = 0;
            ptr = insertText.c;
            ptrlen = insertText.len;
            while (ptr != NULL) {
                if (y_array != NULL)
               y_array[y_size].c = ptr;
                ++y_size;
                tmp = firstOccurrence(ptr, '\n');
                if (tmp == NULL) {
               if (y_array != NULL)
                   y_array[y_size - 1].len = ptrlen;
                }
                else {
               if (y_array != NULL) {
                   *tmp = ZERO;
                   y_array[y_size - 1].len = (Unt)(tmp - ptr);
                   ptrlen -= y_array[y_size - 1].len + 1;
               }
               ++tmp;
               // A trailing '\n' makes the register linewise.
               if (*tmp == ZERO) {
                   y_type = MLINE;
                   break;
               }
                }
                ptr = tmp;
            }
            if (y_array != NULL)
                break;
            y_array = ALLOC_MULT(Text, y_size);
          }
      } else {
          y_size = 1;      // use fake one-line yank register
          y_array = &insertText;
      }
   } else {
      get_yank_register(regname, false);

      y_type = y_current->y_type;
      y_width = y_current->y_width;
      y_size = y_current->y_size;
      y_array = y_current->y_array;
      y_current_used = y_current;
   }

   if (y_type == MLINE) {
      if ((flags & PUT_LINE_SPLIT) != 0) {
         // "p" or "P" in Visual mode: split the lines to put the text in between.
         if (u_save_cursor() == FAIL)
            goto end;
         CS p = ml_get_cursor();
         CS p_orig = p;
         
         Unt plen = ml_get_cursor_len();
         if (dir == FORWARD && *p != ZERO)
            MB_PTR_ADV(p);
         ptr = copySubstr(p, plen - (Unt)(p - p_orig));
         if (!ptr)
            goto end;
         ml_append(curPor->cursor.lnum, ptr, (ColNr)0, false);
         eeglFree(ptr);

         oldp = ml_get_curline();
         p = oldp + curPor->cursor.col;
         if (dir == FORWARD && *p != ZERO)
            MB_PTR_ADV(p);
         ptr = copySubstr(oldp, (Unt)(p - oldp));
         if (ptr == NULL)
            goto end;
         ml_replace(curPor->cursor.lnum, ptr, false);
         ++nr_lines;
         dir = FORWARD;
      }
      if ((flags & PUT_LINE_FORWARD) != 0) {
          // Must be "p" for a Visual block, put lines below the block.
          curPor->cursor = curBook->visual.vi_end;
          dir = FORWARD;
      }
      curBook->opStart = curPor->cursor;   // default for '[ mark
      curBook->opEnd = curPor->cursor;   // default for '] mark
   }

   if (flags & PUT_LINE)   // :put command or "p" in Visual line mode.
      y_type = MLINE;

   if (y_size == 0 || y_array == NULL) {
      showErrFmtMsg(_(e_nothing_in_register_str),
           regname == 0 ? (CS)"\"" : transchar(regname));
      goto end;
   }

   if (y_type == MBLOCK) {
      lnum = curPor->cursor.lnum + y_size + 1;
      if (lnum > curBook->mem.lineCount)
          lnum = curBook->mem.lineCount + 1;
      if (u_save(curPor->cursor.lnum - 1, lnum) == FAIL)
          goto end;
   } ei (y_type == MLINE) {
      lnum = curPor->cursor.lnum;
      // Correct line number for closed fold.  Don't move the cursor yet,
      // u_save() uses it.
      if (dir == BACKWARD)
          (void)getFolds(lnum, OUT &lnum, NULL);
      else
          (void)getFolds(lnum, NULL, OUT &lnum);
      if (dir == FORWARD)
          ++lnum;
      //In an empty buffer the empty line is going to be replaced, include it in the saved lines.
      if ((CURBOOK_EMPTY() ? u_save(0, 2) : u_save(lnum - 1, lnum)) == FAIL)
          goto end;
      if (dir == FORWARD)
          curPor->cursor.lnum = lnum - 1;
      else
          curPor->cursor.lnum = lnum;
      curBook->opStart = curPor->cursor;   // for markAdjust()
   } ei (u_save_cursor() == FAIL)
      goto end;

   lnum = curPor->cursor.lnum;
   col = curPor->cursor.col;

   // Block mode
   if (y_type == MBLOCK) {
      int   delcount;
      int   incr = 0;
      BlockDef bd;
      long   j;
      int   c = gchar_cursor();
      ColNr   endcol2 = 0;

      if (dir == FORWARD && c != ZERO) {
         getvcol(curPor, &curPor->cursor, NULL, NULL, &col);

         // move to start of next multi-byte character
         curPor->cursor.col += utfCharLen(ml_get_cursor());
         ++col;
      } else
         getvcol(curPor, &curPor->cursor, &col, NULL, &endcol2);

      col += curPor->cursor.coladd;
      curPor->cursor.coladd = 0;
      bd.textcol = 0;
      for (i = 0; i < y_size; ++i) {
         int spaces = 0;
         CharTableSize   cts;

         bd.startspaces = 0;
         bd.endspaces = 0;
         vcol = 0;
         delcount = 0;

         // add a new line
         if (curPor->cursor.lnum > curBook->mem.lineCount) {
            if (ml_append(curBook->mem.lineCount, (CS)"", (ColNr)1, false) == FAIL)
               break;
            ++nr_lines;
         }
         // get the old line and advance to the position to insert at
         oldp = ml_get_curline();
         oldlen = ml_get_curline_len();
         bookInitCharsForKeywordsSizeArg(&cts, curPor, curPor->cursor.lnum, 0, oldp, oldp);

         while (cts.cts_vcol < col && *cts.cts_ptr != ZERO) {
            // Count a tab for what it's worth (if list mode not on)
            incr = lbr_chartabsize_adv(&cts);
            cts.cts_vcol += incr;
         }
         vcol = cts.cts_vcol;
         ptr = cts.cts_ptr;
         bd.textcol = (ColNr)(ptr - oldp);
         clear_chartabsize_arg(&cts);

         char shortline = (vcol < col) || (vcol == col && !*ptr) ;

         if (vcol < col) // line too short, pad with spaces
            bd.startspaces = col - vcol;
         ei (vcol > col) {
            bd.endspaces = vcol - col;
            bd.startspaces = incr - bd.endspaces;
            --bd.textcol;
            delcount = 1;
            bd.textcol -= (*mb_head_off)(oldp, oldp + bd.textcol);
            if (oldp[bd.textcol] != TAB) {
               //Only a Tab can be split into spaces. Other characters will have to be moved 
               //to after the block, causing misalignment.
               delcount = 0;
               bd.endspaces = 0;
            }
         }

         yanklen = (int)y_array[i].len;

         if ((flags & PUT_BLOCK_INNER) == 0) {
            // calculate number of spaces required to fill right side of block
            spaces = y_width + 1;
            bookInitCharsForKeywordsSizeArg(&cts, curPor, 0, 0, y_array[i].c, y_array[i].c);

            while (*cts.cts_ptr != ZERO) {
               spaces -= lbr_chartabsize_adv(&cts);
               cts.cts_vcol = 0;
            }
            clear_chartabsize_arg(&cts);
            if (spaces < 0)
               spaces = 0;
          }

         //Insert the new text. First check for multiplication overflow.
         if (yanklen + spaces != 0
              && count > ((INT_MAX - (bd.startspaces + bd.endspaces)) / (yanklen + spaces))
         ) {
            emsg(_(e_resulting_text_too_long));
            break;
         }

         totlen = count * (yanklen + spaces) + bd.startspaces + bd.endspaces;
         newp = alloc(totlen + oldlen + 1);

         // copy part up to cursor to new line
         ptr = newp;
         MEMMOVE(ptr, oldp, (Unt)bd.textcol);
         ptr += bd.textcol;

         // may insert some spaces before the new text
         memset(ptr, ' ', (Unt)bd.startspaces);
         ptr += bd.startspaces;

         // insert the new text
         for (j = 0; j < count; ++j) {
            MEMMOVE(ptr, y_array[i].c, (Unt)yanklen);
            ptr += yanklen;

            // insert block's trailing spaces only if there's text behind
            if ((j < count - 1 || !shortline) && spaces > 0) {
               memset(ptr, ' ', (Unt)spaces);
               ptr += spaces;
            } else
               totlen -= spaces;  // didn't use these spaces
         }

         // may insert some spaces after the new text
         memset(ptr, ' ', (Unt)bd.endspaces);
         ptr += bd.endspaces;

         // move the text after the cursor to the end of the line.
         MEMMOVE(ptr, oldp + bd.textcol + delcount,
               (Unt)(oldlen - bd.textcol - delcount + 1));
         ml_replace(curPor->cursor.lnum, newp, false);

         ++curPor->cursor.lnum;
         if (i == 0)
            curPor->cursor.col += bd.startspaces;
      }

      changed_lines(lnum, 0, curPor->cursor.lnum, nr_lines);

      // Set '[ mark.
      curBook->opStart = curPor->cursor;
      curBook->opStart.lnum = lnum;

      // adjust '] mark
      curBook->opEnd.lnum = curPor->cursor.lnum - 1;
      curBook->opEnd.col = bd.textcol + totlen - 1;
      if (curBook->opEnd.col < 0)
         curBook->opEnd.col = 0;
      curBook->opEnd.coladd = 0;
      if (flags & PUT_CURSEND) {
         ColNr len;

         curPor->cursor = curBook->opEnd;
         curPor->cursor.col++;

         // in Insert mode we might be after the ZERO, correct for that
         len = ml_get_curline_len();
         if (curPor->cursor.col > len)
            curPor->cursor.col = len;
      } else
         curPor->cursor.lnum = lnum;
   } else {
      Pos necursor;

      yanklen = (int)y_array[0].len;

      // Character or Line mode
      if (y_type == MCHAR) {
         // if type is MCHAR, FORWARD is the same as BACKWARD on the next char
         if (dir == FORWARD && gchar_cursor() != ZERO) {
            int bytelen = utfCharLen(ml_get_cursor());

            // put it on the next of the multi-byte character.
            col += bytelen;
            if (yanklen) {
               curPor->cursor.col += bytelen;
               curBook->opEnd.col += bytelen;
            }
         }
         curBook->opStart = curPor->cursor;
      }
      // Line mode: BACKWARD is the same as FORWARD on the previous line
      ei (dir == BACKWARD)
         --lnum;
      necursor = curPor->cursor;

      // simple case: insert into one line at a time
      if (y_type == MCHAR && y_size == 1) {
         LineNr   end_lnum = 0; // init for gcc
         LineNr   start_lnum = lnum;
         int      first_byte_off = 0;

         if (VIsual_active) {
            end_lnum = curBook->visual.vi_end.lnum;
            if (end_lnum < curBook->visual.vi_start.lnum)
               end_lnum = curBook->visual.vi_start.lnum;
            if (end_lnum > start_lnum) {
               Pos   pos;

               // "col" is valid for the first line, in following lines the virtual column needs 
               // to be used.  Matters for multi-byte characters.
               pos.lnum = lnum;
               pos.col = col;
               pos.coladd = 0;
               getvcol(curPor, &pos, NULL, &vcol, NULL);
            }
         }

         if (count == 0 || yanklen == 0) {
            if (VIsual_active)
                lnum = end_lnum;
         } ei (count > INT_MAX / yanklen)
            // multiplication overflow
            emsg(_(e_resulting_text_too_long));
         else {
            totlen = count * yanklen;
            do {
               oldp = ml_get(lnum);
               oldlen = ml_get_len(lnum);
               if (lnum > start_lnum) {
               Pos   pos;

               pos.lnum = lnum;
               if (getvpos(&pos, vcol) == OK)
                   col = pos.col;
               else
                   col = MAXCOL;
                }
               if (VIsual_active && col > oldlen) {
                  lnum++;
                  continue;
               }
               newp = alloc(totlen + oldlen + 1);
               MEMMOVE(newp, oldp, (Unt)col);
               ptr = newp + col;
               for (i = 0; i < count; ++i) {
                  MEMMOVE(ptr, y_array[0].c, (Unt)yanklen);
                  ptr += yanklen;
               }
               MEMMOVE(ptr, oldp + col, (Unt)(oldlen - col) + 1);       // +1 for ZERO

                // compute the byte offset for the last character
                first_byte_off = mb_head_off(newp, ptr - 1);

                // Note: this may free "newp"
                ml_replace(lnum, newp, false);

                inserted_bytes(lnum, col, totlen);

                // Place cursor on last putted char.
                if (lnum == curPor->cursor.lnum)
                {
               // make sure curPor->virtCol is updated
               changed_cline_bef_curs();
               invalidate_botline();
               curPor->cursor.col += (ColNr)(totlen - 1);
                }
                if (VIsual_active)
               lnum++;
            } while (VIsual_active && lnum <= end_lnum);

            if (VIsual_active) // reset lnum to the last visual line
               lnum--;
         }

         // put '] at the first byte of the last character
         curBook->opEnd = curPor->cursor;
         curBook->opEnd.col -= first_byte_off;

         // For "CTRL-O p" in Insert mode, put cursor after last char
         if (totlen && (restart_edit != 0 || (flags & PUT_CURSEND)))
            ++curPor->cursor.col;
         else
            curPor->cursor.col -= first_byte_off;
      } else {
         LineNr   new_lnum = necursor.lnum;
         int      indent;
         int      orig_indent = 0;
         int      indent_diff = 0;   // init for gcc
         int      first_indent = true;
         int      lendiff = 0;
         long   cnt;

         if (flags & PUT_FIXINDENT)
            orig_indent = get_indent();

         // Insert at least one line.  When y_type is MCHAR, break the first line in two.
         for (cnt = 1; cnt <= count; ++cnt) {
            i = 0;
            if (y_type == MCHAR) {
               // Split the current line in two at the insert position.
               // First insert y_array[size - 1] in front of second line.
               // Then append y_array[0] to first line.
               lnum = necursor.lnum;
               ptr = ml_get(lnum) + col;
               totlen = (int)y_array[y_size - 1].len;
               newp = alloc(ml_get_len(lnum) - col + totlen + 1);
               STRCPY(newp, y_array[y_size - 1].c);
               STRCPY(newp + totlen, ptr);
               // insert second line
               ml_append(lnum, newp, (ColNr)0, false);
               ++new_lnum;
               eeglFree(newp);

               oldp = ml_get(lnum);
               newp = alloc(col + yanklen + 1); // copy first part of line
               MEMMOVE(newp, oldp, (Unt)col); // append to first line
               MEMMOVE(newp + col, y_array[0].c, (Unt)(yanklen + 1));
               ml_replace(lnum, newp, false);

               curPor->cursor.lnum = lnum;
               i = 1;
            }

            for (; i < y_size; ++i) {
                if (y_type != MCHAR || i < y_size - 1) {
               if (ml_append(lnum, y_array[i].c, (ColNr)0, false) == FAIL)
                   goto error;
               new_lnum++;
               }
                lnum++;
                ++nr_lines;
                if (flags & PUT_FIXINDENT) {
               Pos   old_pos = curPor->cursor;

               curPor->cursor.lnum = lnum;
               ptr = ml_get(lnum);
               if (cnt == count && i == y_size - 1)
                   lendiff = ml_get_len(lnum);
               if (*ptr == '#' && preprocs_left())
                   indent = 0;     // Leave # lines at start
               ei (*ptr == ZERO)
                   indent = 0;     // Ignore empty lines
               ei (first_indent)
               {
                   indent_diff = orig_indent - get_indent();
                   indent = orig_indent;
                   first_indent = false;
               }
               ei ((indent = get_indent() + indent_diff) < 0)
                   indent = 0;
               (void)set_indent(indent, 0);
               curPor->cursor = old_pos;
               // remember how many chars were removed
               if (cnt == count && i == y_size - 1)
                   lendiff -= ml_get_len(lnum);
                }
            }
            if (cnt == 1)
               new_lnum = lnum;
         }

   error:
         // Adjust marks.
         if (y_type == MLINE) {
            curBook->opStart.col = 0;
           if (dir == FORWARD)
                curBook->opStart.lnum++;
         }
         markAdjust(curBook->opStart.lnum + (y_type == MCHAR), (LineNr)MAXLNUM, nr_lines, 0L, true);

         // note changed text for displaying and folding
         if (y_type == MCHAR)
            changed_lines(curPor->cursor.lnum, col, curPor->cursor.lnum + 1, nr_lines);
         else
            changed_lines(curBook->opStart.lnum, 0, curBook->opStart.lnum, nr_lines);
         if (y_current_used != NULL && (y_current_used != y_current
                       || y_current->y_array != y_array)
         ) {
            //Something invoked through changed_lines() has changed the
            //yank buffer, e.g. a GUI clipboard callback.
            emsg(_(e_yank_register_changed_while_using_it));
            goto end;
         }

         // Put the '] mark on the first byte of the last inserted character.
         // Correct the length for change in indent.
         curBook->opEnd.lnum = new_lnum;
         col = MAX(0, (ColNr)y_array[y_size - 1].len - lendiff);
         if (col > 1) {
            curBook->opEnd.col = col - 1;
            if (y_array[y_size - 1].len > 0)
                curBook->opEnd.col -= mb_head_off(y_array[y_size - 1].c,
                        y_array[y_size - 1].c + y_array[y_size - 1].len - 1);
         } else
            curBook->opEnd.col = 0;

         if (flags & PUT_CURSLINE) {
            // ":put": put cursor on last inserted line
            curPor->cursor.lnum = lnum;
            beginline(BL_WHITE | BL_FIX);
         } ei (flags & PUT_CURSEND) {
            // put cursor after inserted text
            if (y_type == MLINE) {
               if (lnum >= curBook->mem.lineCount)
                  curPor->cursor.lnum = curBook->mem.lineCount;
               else
                  curPor->cursor.lnum = lnum + 1;
               curPor->cursor.col = 0;
            } else {
               curPor->cursor.lnum = new_lnum;
               curPor->cursor.col = col;
               curBook->opEnd = curPor->cursor;
               if (col > 1)
                  curBook->opEnd.col = col - 1;
            }
         } ei (y_type == MLINE) {
            // put cursor on first non-blank in first inserted line
            curPor->cursor.col = 0;
            if (dir == FORWARD)
                ++curPor->cursor.lnum;
            beginline(BL_WHITE | BL_FIX);
         } else   // put cursor on first inserted character
            curPor->cursor = necursor;
      }
   }

   msgmore(nr_lines);
   curPor->setCursWant = true;

   // Make sure the cursor is not after the ZERO.
   int len = ml_get_curline_len();
   if (curPor->cursor.col > len) {
      curPor->cursor.col = len;
   }

end:
   if (commModifierG.cmod_flags & CMOD_LOCKMARKS) {
      curBook->opStart = orig_start;
      curBook->opEnd = orig_end;
   }
   if (allocated)
      eeglFree(insertText.c);
   if (regname == '=')
      eeglFree(y_array);

    VIsual_active = false;

    // If the cursor is past the end of the line put it at the end.
    adjust_cursor_eol();
}

// Return the character name of the register with the given number.
pub int
get_register_name(int num) {
   if (num == -1)
      return '"';
   ei (num < 10)
      return num + '0';
   ei (num == DELETION_REGISTER)
      return '-';
   ei (num == STAR_REGISTER)
      return '*';
   ei (num == PLUS_REGISTER)
      return '+';
   else
      return num + 'a' - 10;
}

// Return the index of the register "" points to.
pub int
get_unname_register(void) {
   return y_previous == NULL ? -1 : y_previous - &y_regs[0];
}

// ":dis" and ":registers": Display the contents of the yank registers.
pub void
c_display(Invocation* invo) {
   int      i, n;
   long   j;
   Byte   *p;
   YankReg   *yb;
   int      name;
   Byte   *arg = invo->arg;
   int      clen;
   int      type;
   Text   insert;

   if (arg && *arg == ZERO)
      arg = NULL;
   char flags = getDecoFlags(HLF_8);

   // Hilite the title
   msg_puts_title(_("\nType Name Content"));
   for (i = -1; i < NUM_REGISTERS && !gotInterruptG; ++i) {
      name = get_register_name(i);
      switch (get_reg_type(name, NULL)) {
      case MLINE: type = 'l'; break;
      case MCHAR: type = 'c'; break;
      default:   type = 'b'; break;
      }
      if (arg && firstOccurrence(arg, name) == NULL
#ifdef ONE_CLIPBOARD
          // Star register and plus register contain the same thing.
         && (name != '*' || firstOccurrence(arg, '+') == NULL)
#endif
         )
          continue;       // did not ask for this register

      // Adjust register name for "unnamed" in 'clipboard'.
      // When it's a clipboard register, fill it with the current contents
      // of the clipboard.
      clipGetDefaultRegister(&name);
      (void)may_get_selection(name);

      if (i == -1) {
         if (y_previous)
            yb = y_previous;
         else
            yb = &(y_regs[0]);
      } else
          yb = &(y_regs[i]);

      if (name == MB_TOLOWER(redir_reg)
         || (firstOccurrence((CS)"\"*+", redir_reg) != NULL &&
             (yb == y_previous || yb == &y_regs[0])))
          continue;       // do not list register being written to, the
                // pointer can be freed

      if (yb->y_array) {
         int do_show = false;
         for (j = 0; !do_show && j < yb->y_size; ++j)
            do_show = !message_filtered(yb->y_array[j].c);

         if (do_show || yb->y_size == 0) {
            msg_putchar('\n');
            msg_puts(S"  ");
            msg_putchar(type);
            msg_puts(S"  ");
            msg_putchar('"');
            msg_putchar(name);
            msg_puts(S"   ");

            n = (int)visibleColsG - 11;
            for (j = 0; j < yb->y_size && n > 1; ++j) {
               if (j) {
                  msgPutsDeco(S"^J", flags);
                  n -= 2;
               }
               for (p = yb->y_array[j].c; *p != ZERO && (n -= bookPtr2Cells(p)) >= 0; ++p) {
                  clen = utfCharLen(p);
                  msgTranslatedSlice((Text){p, clen});
                  p += clen - 1;
               }
            }
            if (n > 1 && yb->y_type == MLINE)
               msgPutsDeco(S"^J", flags);
            out_flush();          // show one line at a time
          }
          ui_breakcheck();
      }
   }

   // display last inserted text
   insert = get_last_insert();
   if ((p = insert.c) != NULL
        && (arg || firstOccurrence(arg, '.') != NULL) && !gotInterruptG && !message_filtered(p)
   ) {
      msg_puts(S"\n  c  \".   ");
      dis_msg(p, true);
   }

   // display last command line
   if (lastCommlineG != NULL && (arg == NULL || firstOccurrence(arg, ':') != NULL)
                && !gotInterruptG && !message_filtered(lastCommlineG))
   {
      msg_puts(S"\n  c  \":   ");
      dis_msg(lastCommlineG, false);
   }

   // display current file name
   if (curBook->currFileName != NULL
       && (arg == NULL || firstOccurrence(arg, '%') != NULL) && !gotInterruptG
               && !message_filtered(curBook->currFileName))
    {
      msg_puts(S"\n  c  \"%   ");
      dis_msg(curBook->currFileName, false);
   }

   // display alternate file name
   if ((arg == NULL || firstOccurrence(arg, '%') != NULL) && !gotInterruptG) {
      Byte       *fname;
      LineNr    dummy;

      if (bookGetFnameByFileId(0, &fname, &dummy) != FAIL && !message_filtered(fname)) {
          msg_puts(S"\n  c  \"#   ");
          dis_msg(fname, false);
      }
   }

   // display last search pattern
   if (last_search_pat().len != 0
       && (!arg || firstOccurrence(arg, '/') != NULL) && !gotInterruptG
                  && !message_filtered(last_search_pat().c)
   ) {
      msg_puts(S"\n  c  \"/   ");
      dis_msg(last_search_pat().c, false);
   }

   // display last used expression
   if (expr_line && (!arg || firstOccurrence(arg, '=') != NULL)
              && !gotInterruptG && !message_filtered(expr_line)) {
      msg_puts(S"\n  c  \"=   ");
      dis_msg(expr_line, false);
   }
}

//display a string for do_dis(); truncate at end of screen line
private void
dis_msg(
   Byte   *p,
   int      skip_esc       // if true, ignore trailing ESC
){
   int n = (int)visibleColsG - 6;
   while (*p != ZERO && !(*p == ESC && skip_esc && *(p + 1) == ZERO)
         && (n -= bookPtr2Cells(p)) >= 0
   ) {
      int l = utfCharLen(p);
      msgTranslatedSlice((Text){p, l});
      p += l;
   }
   ui_breakcheck();
}

// Put a string into a register.  When the register is not empty, the string is appended.
private void
writeToRegister(
   OUT YankReg* yReg,    // pointer to yank register
   Unt yank_type, //MCHAR, MLINE, MBLOCK, MAUTO
   CS str,        //string to put in register
   Long len,      //length of string
   Long blocklen, //width of Visual block
   int str_list   //true iff str is a CString
){
   int lnum;
   long   start;
   long   i;
   int      extra;
   int      extraline = 0;      // extra line at the end
   Byte   *s;
   Byte   **ss;

   if (yReg->y_array == NULL)      // NULL means empty register
      yReg->y_size = 0;

   Unt type;         // MCHAR, MLINE or MBLOCK
   if (yank_type == MAUTO)
      type = (str_list || (len > 0 && (str[len - 1] == NL || str[len - 1] == ENTER)))
                 ? MLINE : MCHAR;
   else
      type = yank_type;

   // Count the number of lines within the string
   int newlines = 0;   // number of lines added
   Boole append = false;      // append to last line in register
   if (str_list) {
      for (ss = (Byte **) str; *ss != NULL; ++ss)
          ++newlines;
   } else {
      for (i = 0; i < len; i++) {
         if (str[i] == '\n')
            ++newlines;
      } 
      if (type == MCHAR || len == 0 || str[len - 1] != '\n') {
         extraline = 1;
         ++newlines;   // count extra newline at the end
      }
      if (yReg->y_size > 0 && yReg->y_type == MCHAR) {
         append = true;
         --newlines;   // uncount newline when appending first line
      }
   }

   // Without any lines make the register empty.
   if (yReg->y_size + newlines == 0) {
      EE_CLEAR(yReg->y_array);
      return;
   }

   // Allocate an array to hold the pointers to the new register lines.
   // If the register was not empty, move the existing lines to the new array.
   Text* pp = lallocZeroed((yReg->y_size + newlines) * sizeof(Text), true);
   for (lnum = 0; lnum < yReg->y_size; ++lnum)
      pp[lnum] = yReg->y_array[lnum];
   eeglFree(yReg->y_array);
   yReg->y_array = pp;
   long maxlen = 0;

   // Find the end of each line and save it into the array.
   if (str_list) {
      for (ss = (Byte **) str; *ss != NULL; ++ss, ++lnum) {
         pp[lnum].len = STRLEN(*ss);
         pp[lnum].c = copySubstr(*ss, pp[lnum].len);
         if (type == MBLOCK) {
            int charlen = mb_string2cells(*ss, -1);
            if (charlen > maxlen)
               maxlen = charlen;
         }
      }
   } else {
      for (start = 0; start < len + extraline; start += i + 1) {
         int charlen = 0;

         for (i = start; i < len;) { // find the end of the line
            if (str[i] == '\n')
               break;
            if (type == MBLOCK)
               charlen += mb_ptr2cells_len(str + i, len - i);

            if (str[i] == ZERO)
               i++; // registers can have ZERO chars
            else
               i += utfCharLen_len(str + i, len - i);
         }
         i -= start;         // i is now length of line
         if (charlen > maxlen)
            maxlen = charlen;
         if (append) {
            --lnum;
            extra = (int)yReg->y_array[lnum].len;
         } else
            extra = 0;
         s = alloc(i + extra + 1);
         if (extra)
            MEMMOVE(s, yReg->y_array[lnum].c, (Unt)extra);
         if (append)
            eeglFree(yReg->y_array[lnum].c);
         if (i > 0)
            MEMMOVE(s + extra, str + start, (Unt)i);
         extra += i;
         s[extra] = ZERO;
         yReg->y_array[lnum].c = s;
         yReg->y_array[lnum].len = extra;
         ++lnum;
         while (--extra >= 0) {
            if (*s == ZERO)
               *s = '\n';       // replace ZERO with newline
            ++s;
         }
         append = false;          // only first line is appended
      }
   }
   yReg->y_type = type;
   yReg->y_size = lnum;
   if (type == MBLOCK)
      yReg->y_width = (blocklen < 0 ? maxlen - 1 : blocklen);
   else
      yReg->y_width = 0;
   yReg->y_time_set = eeTime();
}

// Replace the contents of the '~' register with str.
pub void
dnd_yank_drag_data(CS str, long len) {
   YankReg* curr = y_current;
   y_current = &y_regs[TILDE_REGISTER];
   free_yank_all();
   writeToRegister(OUT y_current, MCHAR, str, len, 0L, false);
   y_current = curr;
}


//Return the type of a register. MAUTO for error. Used for getregtype().
pub Byte
get_reg_type(int regname, long *reglen) {
   switch (regname) {
   case '%':      // file name
   case '#':      // alternate file name
   case '=':      // expression
   case ':':      // last command line
   case '/':      // last search-pattern
   case '.':      // last inserted text
   case Ctrl_F:   // Filename under cursor
   case Ctrl_P:   // Path under cursor, expand via "path"
   case Ctrl_W:   // word under cursor
   case Ctrl_A:   // WORD (mnemonic All) under cursor
   case '_':      // black hole: always empty
       return MCHAR;
   }

   regname = may_get_selection(regname);

   if (regname != ZERO && !valid_yank_reg(regname, false))
      return MAUTO;

   get_yank_register(regname, false);

   if (y_current->y_array != NULL) {
      if (reglen != NULL && y_current->y_type == MBLOCK)
         *reglen = y_current->y_width;
      return y_current->y_type;
   }
   return MAUTO;
}

//When "flags" has GREG_LIST, return a list with text "s". Otherwise just return "s".
private CS
getreg_wrap_one_line(CS s, int flags) {
   if ((flags & GREG_LIST) != 0){
      List *list = list_alloc();
      if (list_append_string(list, NULL, -1) == FAIL) {
         list_free(list);
         return NULL;
      }
      list->first->c.string = s;
      return (CS)list;
   }
   return s;
}

//Return the contents of a register as a single allocated string or as a list.
//Used for "@r" in expressions and for getreg(). Return NULL for error.
//Flags:
//  GREG_NO_EXPR   Do not allow expression register
//  GREG_EXPR_SRC   For the expression register: return expression itself,
//        not the result of its evaluation.
//  GREG_LIST   Return a list of lines instead of a single string.
pub CS
get_reg_contents(int regname, int flags) {
   LineNr   i;
   Byte   *retval;
   int      allocated;
   long   len;

   // Don't allow using an expression register inside an expression
   if (regname == '=') {
      if (flags & GREG_NO_EXPR)
         return NULL;
      if (flags & GREG_EXPR_SRC)
         return getreg_wrap_one_line(get_expr_line_src(), flags);
      return getreg_wrap_one_line(get_expr_line(), flags);
   }

   if (regname == '@')       // "@@" is used for unnamed register
      regname = '"';

   // check for valid regname
   if (regname != ZERO && !valid_yank_reg(regname, false))
      return NULL;

   regname = may_get_selection(regname);

   if (get_spec_reg(regname, &retval, &allocated, false)) {
      if (retval == NULL)
         return NULL;
      if (allocated)
         return getreg_wrap_one_line(retval, flags);
      return getreg_wrap_one_line(copyStr(retval), flags);
   }

   get_yank_register(regname, false);
   if (y_current->y_array == NULL)
      return NULL;

   if ((flags & GREG_LIST) != 0){
      List   *list = list_alloc();
      Boole error = false;
      for (i = 0; i < y_current->y_size; ++i) {
         if (list_append_string(list, y_current->y_array[i].c, -1) == FAIL)
            error = true;
      } 
      if (error) {
         list_free(list);
         return NULL;
      }
      return (Byte*)list;
   }

   // Compute length of resulting string.
   len = 0;
   for (i = 0; i < y_current->y_size; ++i) {
      len += (long)y_current->y_array[i].len;

      // Insert a newline between lines and after the last line if y_type is MLINE.
      if (y_current->y_type == MLINE || i < y_current->y_size - 1)
         ++len;
   }

   retval = alloc(len + 1);

   // Copy the lines of the yank register into the string.
   len = 0;
   for (i = 0; i < y_current->y_size; ++i) {
      STRCPY(retval + len, y_current->y_array[i].c);
      len += (long)y_current->y_array[i].len;

      // Insert a newline between lines and after the last line if y_type is MLINE.
      if (y_current->y_type == MLINE || i < y_current->y_size - 1)
          retval[len++] = '\n';
    }
    retval[len] = ZERO;

    return retval;
}

private int
init_write_reg(
   int name,
   YankReg** old_y_previous,
   YankReg** old_y_current,
   int must_append,
   Unt*
) {
   if (!valid_yank_reg(name, true)) {     // check for valid reg name
      emsg_invreg(name);
      return FAIL;
   }

   // Don't want to change the current (unnamed) register
   *old_y_previous = y_previous;
   *old_y_current = y_current;

   get_yank_register(name, true);
   if (!y_append && !must_append)
      free_yank_all();
   return OK;
}

private void
finish_write_reg(int name, YankReg* old_y_previous, YankReg* old_y_current) {
   // ':let @" = "val"' should change the meaning of the "" register
   if (name != '"')
      y_previous = old_y_previous;
   y_current = old_y_current;
}

//Store string "str" in register "name".
//"maxlen" is the maximum number of bytes to use, -1 for all bytes.
//If "must_append" is true, always append to the register.  Otherwise append
//if "name" is an uppercase letter.
//Note: "maxlen" and "must_append" don't work for the "/" register.
//Careful: 'str' is modified, you may have to use a copy!
//If "str" ends in '\n' or '\r', use linewise, otherwise use characterwise.
pub void
write_reg_contents(
   int      name,
   CS str,
   int maxlen,
   int must_append)
{
   write_reg_contents_ex(name, str, maxlen, must_append, MAUTO, 0L);
}

pub void
write_reg_contents_lst(
   int name,
   Byte** strings,
   int,
   int must_append,
   Unt yank_type,
   long block_len
) {
   YankReg  *old_y_previous, *old_y_current;

   if (name == '/' || name == '=') {
      Byte   *s;

      if (strings[0] == NULL)
         s = S"";
      ei (strings[1]) {
         emsg(_(e_search_pattern_and_expression_register_may_not_contain_two_or_more_lines));
         return;
      } else
         s = strings[0];
      write_reg_contents_ex(name, s, -1, must_append, yank_type, block_len);
      return;
   }

   if (name == '_')       // black hole: nothing to do
      return;

   if (init_write_reg(name, &old_y_previous, &old_y_current, must_append, &yank_type) == FAIL)
      return;

   writeToRegister(OUT y_current, yank_type, (CS)strings, -1, block_len, true);
   finish_write_reg(name, old_y_previous, old_y_current);
}

pub void
write_reg_contents_ex(
   int name,
   CS str,
   int maxlen,
   int must_append,
   Unt yank_type,
   long block_len
) {
   YankReg *old_y_previous, *old_y_current;
   Long len = (maxlen >= 0) ? maxlen :  (long)STRLEN(str);
      
   // Special case: '/' search pattern
   if (name == '/') {
      set_last_search_pat(str, RE_SEARCH, true, true);
      return;
   }

   if (name == '#') {
      Book* buf;
      if (EE_ISDIGIT(*str)) {
         int num = atoi((char *)str);
         buf = bookFindFileByBookNr(num);
         if (!buf)
            showErrFmtMsg(_(e_book_nr_does_not_exist), (long)num);
      } else
         buf = bookFindFileByBookNr(booklistFindPattern(str, str + len, true, false, false));
      if (!buf)
         return;
      curPor->altFnum = buf->fiNum;
      return;
   }

   if (name == '=') {
      CS p = copySubstr(str, (Unt)len);
      CS s;
      if (must_append && expr_line) {
         s = concat_str(expr_line, p);
         eeglFree(p);
         p = s;
      }
      set_expr_line(p, NULL);
      return;
   }

   if (name == '_')       // black hole: nothing to do
      return;

   if (init_write_reg(name, &old_y_previous, &old_y_current, must_append, &yank_type) == FAIL)
      return;

   writeToRegister(OUT y_current, yank_type, str, len, block_len, false);
   finish_write_reg(name, old_y_previous, old_y_current);
}

//}}}
//{{{clipboard

//Functions for copying and pasting text between applications.
//This is always included in a GUI version, but may also be included when the
//clipboard and mouse is available to a terminal version such as xterm.
//Note: there are some more functions in ops.c that handle selection stuff.
//
//Also note that the majority of functions here deal with the X 'primary'
//(visible - for Visual mode use) selection, and only that. There are no
//versions of these for the 'clipboard' selection, as Visual mode has no use for them.

// Stuff for general mouse selection, without using Visual mode.

// "how" flags for clip_invert_area()
#define CLIP_CLEAR   1
#define CLIP_SET     2
#define CLIP_TOGGLE  3

#define CLIP_ZINDEX 32000

//Find the starting and ending positions of the word at the given row and
//column.  Only white-separated words are recognized here.
#define CHAR_CLASS(c)   (c <= ' ' ? ' ' : eeIsWordc(c))

// Convert from the selection string into the '*'/'+' register.
private void
clip_yank_selection(int type, CS str, Long len) {
   YankReg* yReg = getYRegister(STAR_REGISTER);
   writeToRegister(OUT yReg, type, str, len, -1, false);
}

//Get the selected text and put it in register '*'
private void
copyToClipboard() {
   //PolyWithStatus fromShell = chCallShell(tConst("wl-paste"), SHELL_READ);
   //_bp(true);

   //Avoid triggering autocmds such as TextYankPost.
   block_autocmds();

   //Get the text between clipboard.start & clipboard.end
   YankReg* old_y_previous = get_y_previous();
   YankReg* old_y_current = get_y_current();
   Pos old_cursor = curPor->cursor;
   ColNr old_curswant = curPor->cursWant;
   int old_set_curswant = curPor->setCursWant;
   Pos old_op_start = curBook->opStart;
   Pos old_op_end = curBook->opEnd;
   Pos old_visual = VIsual;
   int old_visual_mode = VIsual_mode;
   
   Operator oa;
   clear_oparg(&oa);
   oa.regname = '*';
   oa.opTy = OP_YANK;
   
   ActionArg ca;
   CLEAR_FIELD(ca);
   ca.oper = &oa;
   ca.cmdchar = 'y';
   ca.count1 = 1;
   ca.retval = CA_NO_ADJ_OP_END;
   jugExecuteVisualOperator(&ca, 0, true);

   // restore things
   set_y_previous(old_y_previous);
   set_y_current(old_y_current);
   curPor->cursor = old_cursor;
   changed_cline_bef_curs();   // need to update virtCol et al
   curPor->cursWant = old_curswant;
   curPor->setCursWant = old_set_curswant;
   curBook->opStart = old_op_start;
   curBook->opEnd = old_op_end;
   VIsual = old_visual;
   VIsual_mode = old_visual_mode;

   unblock_autocmds();
}

//When "regname" is the clipboard register, obtain the selection. If it's not
//available return zero, otherwise return "regname".
pub int
may_get_selection(Unt regname) {
   if (regname == '*') {
      copyToClipboard();
   }
   return regname;
}

//Adjust the register name pointed to with "rp" for the clipboard being used always.
pub void
clipGetDefaultRegister(OUT int* rp){
   if (*rp == 0) {
      *rp = '+';
   }
}

//Send the current selection to the clipboard. Do nothing for Wayland because
//we will fill in the selection only when requested by another client.
private void
clip_wl_set_selection(ClipBoard *) {
}

//}}}
//{{{new clipboard

private int
copy0() {
    const char *text_to_copy = "Hello from my C program!";

    // Open a pipe to wl-copy
    FILE *fp = popen("wl-copy", "w");
    if (fp == NULL) {
        perror("Failed to run wl-copy");
        return 1;
    }

    // Write the text into the pipe
    fputs(text_to_copy, fp);

    // Close the pipe and check status
    int status = pclose(fp);
    if (status != 0) {
        fprintf(stderr, "wl-copy exited with error\n");
    } else {
        printf("Successfully copied text to Wayland clipboard.\n");
    }

    return 0;
}

private int
paste0() {
   char buffer[128];

   // Open a pipe to read from wl-paste
   FILE *fp = popen("wl-paste", "r");
   if (fp == NULL) {
       perror("Failed to run wl-paste");
       return 1;
   }

   // Read the output from the command a chunk at a time
   printf("Clipboard contents:\n");
   while (fgets(buffer, sizeof(buffer), fp) != NULL) {
       printf("%s", buffer);
   }
   printf("\n");

   // Close the pipe
   int status = pclose(fp);
   if (status != 0) {
       fprintf(stderr, "wl-paste exited with error\n");
   }

   return 0;
}

//}}}
