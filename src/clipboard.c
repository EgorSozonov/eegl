//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## clipboard.c: Functions to handle the clipboard and copy-and-paste registers

#include "eegl.h"

//{{{copy-and-paste registers

//Registers:
//  0 = unnamed register, for normal yanks and puts
//  1..9 = registers '1' to '9', for deletes
//10..35 = registers 'a' to 'z' ('A' to 'Z' for appending)
//    36 = delete register '-'
//    37 = Selection register '*'.
//    38 = Clipboard register '+'.
//                                 or FEAT_WAYLAND defined
private YankReg   y_regs[NUM_REGISTERS];

private YankReg   *y_current;       // ptr to current yankreg
private int      y_append;       // TRUE when appending
private YankReg   *y_previous = NULL; // ptr to last written yankreg

private int   stuff_yank(int, CS);
private void   put_reedit_in_typeBufG(int silent);
private int   put_in_typeBufG(CS s, int esc, int colon, int silent);
private int   yank_copy_line(BlockDef* bd, long y_idx, int exclude_trailing_space);
private void   copy_yank_reg(YankReg *reg);
private void   dis_msg(CS p, int skip_esc);

private void may_set_selection(void);
private void clip_gen_set_selection(ClipBoard *cbd);

YankReg *
get_y_regs(void) {
   return y_regs;
}

YankReg *
get_y_register(int reg) {
   return &y_regs[reg];
}

YankReg *
get_y_current(void) {
   return y_current;
}

YankReg *
get_y_previous(void) {
   return y_previous;
}

void
set_y_current(YankReg *yreg) {
   y_current = yreg;
}

void
set_y_previous(YankReg *yreg) {
   y_previous = yreg;
}

void
reset_y_append(void) {
   y_append = FALSE;
}


//Keep the last expression line here, for repeating.
private Byte   *expr_line = NULL;
private Invocation   *exprInvoS = NULL;

//Get an expression for the "\"=expr1" or "CTRL-R =expr1" Return '=' when OK, ZERO otherwise.
int
get_expr_register(void) {
   Byte   *new_line;

   new_line = getCommline('=', 0L, 0, 0);
   if (new_line == NULL)
      return ZERO;
   if (*new_line == ZERO)   // use previous line
      eeglFree(new_line);
   else
      set_expr_line(new_line, NULL);
   return '=';
}

//Set the expression for the '=' register. Argument must be an allocated string.
//"invo" may be used if the next line needs to be checked when evaluating the expression.
void
set_expr_line(CS new_line, Invocation* invo) {
   eeglFree(expr_line);
   expr_line = new_line;
   exprInvoS = invo;
}

//Get the result of the '=' register expression.
//Return a pointer to allocated memory, or NULL for failure.
CS
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
int
valid_yank_reg(
   int       regname,
   int       writing)       // if TRUE check for writable registers
{
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
      return TRUE;
   // clipboard support not enabled in this build
   ei (regname == '*' || regname == '+') {
      // Warn about missing clipboard support once
      msg_warn_missing_clipboard();
      return FALSE;
   }
   return FALSE;
}

//Set y_current and y_append, according to the value of "regname".
//Cannot handle the '_' register.
//Must only be called with a valid register name!
//
//If regname is 0 and writing, use register 0. If regname is 0 and reading, use previous register
//
//Return TRUE when the register should be inserted literally (selection or clipboard).
int
get_yank_register(int regname, int writing) {
   int       i;
   int       ret = FALSE;

   y_append = FALSE;
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
      y_append = TRUE;
   } ei (regname == '-')
      i = DELETION_REGISTER;
    // When selection is not available, use register 0 instead of '*'
   ei (clipboard.available && regname == '*') {
      i = STAR_REGISTER;
      ret = TRUE;
   }
   // When clipboard is not available, use register 0 instead of '+'
   ei (clipboard.available && regname == '+') {
      i = PLUS_REGISTER;
      ret = TRUE;
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
void *
get_register(int      name, int copy) {  // make a copy, if FALSE make register empty.
   YankReg   *reg;
   int      i;

   // When Visual area changed, may have to update selection. Obtain the selection too.
   if (name == '*' && clipboard.available) {
      clip_update_selection(&clipboard);
      may_get_selection(name);
   }
   if (name == '+' && clipboard.available) {
      clip_update_selection(&clipboard);
      may_get_selection(name);
   }

   get_yank_register(name, 0);
   reg = ALLOC_ONE(YankReg);
   if (reg == NULL)
      return (void *)NULL;

   *reg = *y_current;
   if (copy) {
      // If we run out of memory some or all of the lines are empty.
      if (reg->y_size == 0 || y_current->y_array == NULL)
         reg->y_array = NULL;
      else
         reg->y_array = ALLOC_MULT(Text, reg->y_size);
      if (reg->y_array != NULL) {
         for (i = 0; i < reg->y_size; ++i) {
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
void
put_register(int name, void *reg) {
   get_yank_register(name, 0);
   free_yank_all();
   *y_current = *(YankReg *)reg;
   eeglFree(reg);

   // Send text written to clipboard register to the clipboard.
   may_set_selection();
}

void
free_register(void *reg) {
    YankReg tmp;

    tmp = *y_current;
    *y_current = *(YankReg *)reg;
    free_yank_all();
    eeglFree(reg);
    *y_current = tmp;
}

// return TRUE if the current yank register has type MLINE
int
yank_register_mline(int regname) {
   if (regname != 0 && !valid_yank_reg(regname, FALSE))
      return FALSE;
   if (regname == '_')      // black hole is always empty
      return FALSE;
   get_yank_register(regname, FALSE);
   return (y_current->y_type == MLINE);
}

// Start or stop recording into a yank register. Return FAIL for failure, OK otherwise.
int
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
      msg(E);
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
   if (regname != 0 && !valid_yank_reg(regname, TRUE)) {
      eeglFree(p);
      return FAIL;
   }
   if (regname == '_') {        // black hole: don't do anything
      eeglFree(p);
      return OK;
   }

   Unt plen = STRLEN(p);
   get_yank_register(regname, TRUE);
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

int
get_execreg_lastc(void) {
   return execreg_lastc;
}

void
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
int
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
   if (regname == '%' || regname == '#' || !valid_yank_reg(regname, FALSE)) {
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
         retval = put_in_typeBufG(p + 5, TRUE, TRUE, silent);
          else
         retval = put_in_typeBufG(p, TRUE, TRUE, silent);
      }
      eeglFree(p);
   } ei (regname == '=') {
      p = get_expr_line();
      if (p == NULL)
          return FAIL;
      retval = put_in_typeBufG(p, TRUE, colon, silent);
      eeglFree(p);
    } ei (regname == '.') {      // use last inserted text
      p = get_last_insert_save();
      if (p == NULL)    {
         emsg(_(e_no_inserted_text_yet));
         return FAIL;
   }
   retval = put_in_typeBufG(p, FALSE, colon, silent);
   eeglFree(p);
   } else {
      get_yank_register(regname, FALSE);
      if (y_current->y_array == NULL)
          return FAIL;

      // Disallow remapping for ":@r".
      remap = colon ? REMAP_NONE : REMAP_YES;

      // Insert lines into typeahead buffer, from last one to first one.
      put_reedit_in_typeBufG(silent);
      for (i = y_current->y_size; --i >= 0; ) {
         CS escaped;
         CS str;
         int free_str = FALSE;

         // insert NL between lines and after last line if type is MLINE
         if (y_current->y_type == MLINE || i < y_current->y_size - 1 || addcr) {
            if (insertIntoTypebuf((CS)"\n", remap, 0, TRUE, silent) == FAIL)
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
               free_str = TRUE;
            }
         }
         escaped = copyStr_escape_csi(str);
         if (free_str)
            eeglFree(str);
         retval = insertIntoTypebuf(escaped, remap, 0, TRUE, silent);
         eeglFree(escaped);
         if (retval == FAIL)
            return FAIL;
         if (colon && insertIntoTypebuf((CS)":", remap, 0, TRUE, silent) == FAIL)
            return FAIL;
      }
      reg_executing = regname == 0 ? '"' : regname; // disable "q" command
      pending_end_reg_executing = FALSE;
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
   if (insertIntoTypebuf(buf, REMAP_NONE, 0, TRUE, silent) == OK)
      restart_edit = ZERO;
}

//Insert register contents "s" into the typeahead buffer, so that it will be executed again.
//When "esc" is TRUE it is to be taken literally: Escape CSI characters and no remapping.
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
      retval = insertIntoTypebuf((CS)"\n", REMAP_NONE, 0, TRUE, silent);
   if (retval == OK) {
      Byte   *p;

      if (esc)
         p = copyStr_escape_csi(s);
      else
         p = s;
      if (p == NULL)
         retval = FAIL;
      else
         retval = insertIntoTypebuf(p, esc ? REMAP_NONE : REMAP_YES, 0, TRUE, silent);
      if (esc)
         eeglFree(p);
   }
   if (colon && retval == OK)
       retval = insertIntoTypebuf((CS)":", REMAP_NONE, 0, TRUE, silent);
   return retval;
}

//Insert a yank register: copy it into the Read buffer.
//Used by CTRL-R command and middle mouse button in insert mode.
//
//return FAIL for failure, OK otherwise
int
insert_reg(
    int      regname,
    int      literally_arg)   // insert literally, not as if typed
{
    long   i;
    int      retval = OK;
    Byte   *arg;
    int      allocated;
    int      literally = literally_arg;

    // It is possible to get into an endless loop by having CTRL-R a in
    // register a and then, in insert mode, doing CTRL-R a.
    // If you hit CTRL-C, the loop will be broken here.
    ui_breakcheck();
    if (gotInterruptG)
   return FAIL;

    // check for valid regname
    if (regname != ZERO && !valid_yank_reg(regname, FALSE))
   return FAIL;

    regname = may_get_selection(regname);

    if (regname == '.')         // insert last inserted text
   retval = stuff_inserted(ZERO, 1L, TRUE);
    ei (get_spec_reg(regname, &arg, &allocated, TRUE)) {
   if (arg == NULL)
       return FAIL;
   stuffescaped(arg, literally);
   if (allocated)
       eeglFree(arg);
    } else {           // name or number register
   if (get_yank_register(regname, FALSE))
       literally = TRUE;
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

//If "regname" is a special register, return TRUE and store a pointer to its value in "retVal".
int
get_spec_reg(
   int      regname,
   OUT CS* retVal,
   int      *allocated,   // return: TRUE when value was allocated
   int      errmsg)      // give error message when failing
{
   int      cnt;

   *retVal = E;
   *allocated = FALSE;
   switch (regname) {
   case '%':      // file name
      if (errmsg)
         check_fname();   // will give emsg if not set
      *retVal = curBook->currFileName;
      return TRUE;

   case '#':      // alternate file name
      *retVal = getaltfname(errmsg);   // may give emsg if not set
      return TRUE;

   case '=':      // result of expression
      *retVal = get_expr_line();
      *allocated = TRUE;
      return TRUE;

   case ':':      // last command line
      if (!lastCommlineG && errmsg)
         emsg(_(e_no_previous_command_line));
      *retVal = lastCommlineG != E ? lastCommlineG : E;
      return TRUE;

   case '/':      // last search-pattern
      CS lastPat = last_search_pat();
      if (!lastPat && errmsg)
         emsg(_(e_no_previous_regular_expression));
      *retVal = lastPat ? lastPat : E;
      return TRUE;

   case '.':      // last inserted text
      *retVal = get_last_insert_save();
      *allocated = TRUE;
      if (*retVal == E && errmsg)
         emsg(_(e_no_inserted_text_yet));
      return TRUE;

   case Ctrl_F:      // Filename under cursor
   case Ctrl_P:      // Path under cursor, expand via "path"
      if (!errmsg)
         return FALSE;
      *retVal = file_name_at_cursor(
            FNAME_MESS | FNAME_HYP | (regname == Ctrl_P ? FNAME_EXP : 0), 1L, NULL
      );
      *allocated = TRUE;
      return TRUE;

   case Ctrl_W:      // word under cursor
   case Ctrl_A:      // WORD (mnemonic All) under cursor
       if (!errmsg)
      return FALSE;
       cnt = find_ident_under_cursor(retVal, regname == Ctrl_W
               ?  (FIND_IDENT|FIND_STRING) : FIND_STRING);
       *retVal = cnt ? copySubstr(*retVal, cnt) : E;
       *allocated = TRUE;
       return TRUE;

   case Ctrl_L:      // Line under cursor
      if (!errmsg)
         return FALSE;

      *retVal = memGetLine(curPor->book,
         curPor->cursor.lnum, FALSE);
       return TRUE;

   case '_':      // black hole: always empty
       *retVal = (CS)"";
       return TRUE;
   }

   return FALSE;
}

//Paste a yank register into the command line. Only for non-special registers.
//Used by CTRL-R command in command-line mode.
//insert_reg() can't be used here, because special characters from the
//register contents will be interpreted as commands.
//return FAIL for failure, OK otherwise
int
cmdline_paste_reg(
   int regname,
   int literally_arg,   // Insert text literally instead of "as typed"
   int remcr)      // don't add CR characters
{
   long   i;
   int      literally = literally_arg;

   if (get_yank_register(regname, FALSE))
      literally = TRUE;
   if (y_current->y_array == NULL)
      return FAIL;

   for (i = 0; i < y_current->y_size; ++i) {
      cmdline_paste_str(y_current->y_array[i].c, literally);

      // Insert ^M between lines and after last line if type is MLINE.
      // Don't do this when "remcr" is TRUE.
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
void
shift_delete_registers(void) {
   int      n;

   y_current = &y_regs[9];
   free_yank_all();         // free register nine
   for (n = 9; n > 1; --n)
      y_regs[n] = y_regs[n - 1];
   y_current = &y_regs[1];
   if (!y_append)
      y_previous = y_current;
   y_regs[1].y_array = NULL;      // set register one to empty
}

void
yank_do_autocmd(Operator* opArg, YankReg *reg) {
   static int recursive = FALSE;
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

   recursive = TRUE;
   textlock++;
   apply_autocmds(EVENT_TEXTYANKPOST, NULL, NULL, false, curBook);
   textlock--;
   recursive = FALSE;

   // Empty the dictionary, v:event is still valid
   restore_v_event(v_event, &save_v_event);
}

// set all the yank registers to empty (called from main())
void
init_yank(void) {
   for (int i = 0; i < NUM_REGISTERS; ++i)
      y_regs[i].y_array = NULL;
}

#if defined(EXITFREE) || defined(PROTO)
void
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

void
free_yank_all(void) {
   free_yank(y_current->y_size);
}

//Yank the text between "opArg->start" and "opArg->end" into a yank register.
//If we are to append (uppercase register), we first yank into a new yank
//register and then concatenate the old and the new one (so we keep the old
//one in case of out-of-memory).
//
//Return FAIL for failure, OK otherwise.
int
op_yank(Operator *opArg, int deleting, int mess) {
    long      y_idx;      // index in y_array[]
    YankReg      *curr;      // copy of y_current
    YankReg      newreg;      // new yank register when appending
    LineNr      lnum;      // current line number
    int         yanktype = opArg->motion_type;
    long      yanklines = opArg->line_count;
    LineNr      yankendlnum = opArg->end.lnum;
    Byte      *pnew;
    BlockDef   bd;

                // check for read-only register
    if (opArg->regname != 0 && !valid_yank_reg(opArg->regname, TRUE)) {
   beep_flush();
   return FAIL;
    }
    if (opArg->regname == '_')       // black hole: nothing to do
   return OK;

   if ((!clipboard.available && opArg->regname == '*') 
          || (!clipboard.available && opArg->regname == '+')
   ) {
      opArg->regname = 0;
      msg_warn_missing_clipboard();
   }

    if (!deleting)          // op_delete() already set y_current
   get_yank_register(opArg->regname, TRUE);

    curr = y_current;
                // append to existing contents
    if (y_append && y_current->y_array != NULL)
   y_current = &newreg;
    else
   free_yank_all();       // free previously yanked lines

    // If the cursor was in column 1 before and after the movement, and the
    // operator is not inclusive, the yank is always linewise.
    if (       opArg->motion_type == MCHAR
       && opArg->start.col == 0
       && !opArg->inclusive
       && (!opArg->is_VIsual)
       && !opArg->block_mode
       && opArg->end.col == 0
       && yanklines > 1)
    {
      yanktype = MLINE;
      --yankendlnum;
      --yanklines;
   }

   y_current->y_size = yanklines;
   y_current->y_type = yanktype;   // set the yank register type
   y_current->y_width = 0;
   y_current->y_array = lallocZeroed(sizeof(Text)* yanklines, TRUE);
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

            if (yank_copy_line(&bd, y_idx, FALSE) == FAIL)
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

   // If we were yanking to the '*' register, send result to clipboard.
   // If no register was specified, and "unnamed" in 'clipboard', make a copy to the '*' register.
   if (clipboard.available
       && (curr == &(y_regs[STAR_REGISTER]) || (!deleting && opArg->regname == 0))
   ) {
      if (curr != &(y_regs[STAR_REGISTER]))
         // Copy the text from register 0 to the clipboard register.
         copy_yank_reg(&(y_regs[STAR_REGISTER]));

      clip_own_selection(&clipboard);
      clip_gen_set_selection(&clipboard);
   }

   // If we were yanking to the '+' register, send result to selection.
   // Also copy to the '*' register, in case auto-select is off.  But not when
   // 'clipboard' has "unnamedplus" and not "unnamed"; and not when
   // deleting and both "unnamedplus" and "unnamed".
   if (clipboard.available
       && (curr == &(y_regs[PLUS_REGISTER]) || (!deleting && opArg->regname == 0)))
    {
      if (curr != &(y_regs[PLUS_REGISTER]))
         // Copy the text from register 0 to the clipboard register.
         copy_yank_reg(&(y_regs[PLUS_REGISTER]));

      clip_own_selection(&clipboard);
      clip_gen_set_selection(&clipboard);
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
   mch_memmove(pnew, bd->textstart, (Unt)bd->textlen);
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
   y_current->y_array = lallocZeroed(sizeof(Text) * y_current->y_size, TRUE);
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
void
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
   int      allocated = FALSE;
   Pos   orig_start = curBook->opStart;
   Pos   orig_end = curBook->opEnd;

   // Adjust register name for "unnamed" in 'clipboard'.
   adjust_clip_reg(&regname);
   (void)may_get_selection(regname);

   curBook->opStart = curPor->cursor;   // default for '[ mark
   curBook->opEnd = curPor->cursor;   // default for '] mark

   // Using inserted text works differently, because the register includes
   // special characters (newlines, etc.).
   if (regname == '.') {
      if (VIsual_active)
          stuffcharReadbuff(VIsual_mode);
      (void)stuff_inserted((dir == FORWARD ? (count == -1 ? 'o' : 'a') :
                   (count == -1 ? 'O' : 'i')), count, FALSE);
      // Putting the text is done later, so can't really move the cursor to
      // the next character.  Use "l" to simulate it.
      if ((flags & PUT_CURSEND) && gchar_cursor() != ZERO)
          stuffcharReadbuff('l');
      return;
   }

   // For special registers '%' (file name), '#' (alternate file name) and
   // ':' (last command line), etc. we have to create a fake yank register.
   // For compiled code "expr_result" holds the expression result.
   Text insertText = (Text){E, 0};
   if (regname == '=' && expr_result)
      insertText.c = expr_result;
   ei (get_spec_reg(regname, &insertText.c, &allocated, TRUE) && insertText.c == NULL)
      return;

   // Autocommands may be executed when saving lines for undo.  This might
   // make "y_array" invalid, so we start undo now to avoid that.
   if (u_save(curPor->cursor.lnum, curPor->cursor.lnum + 1) == FAIL)
      goto end;

   if (insertText.c != E) {
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
      get_yank_register(regname, FALSE);

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
         ml_append(curPor->cursor.lnum, ptr, (ColNr)0, FALSE);
         eeglFree(ptr);

         oldp = ml_get_curline();
         p = oldp + curPor->cursor.col;
         if (dir == FORWARD && *p != ZERO)
            MB_PTR_ADV(p);
         ptr = copySubstr(oldp, (Unt)(p - oldp));
         if (ptr == NULL)
            goto end;
         ml_replace(curPor->cursor.lnum, ptr, FALSE);
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
          (void)getFolds(lnum, &lnum, NULL);
      else
          (void)getFolds(lnum, NULL, &lnum);
      if (dir == FORWARD)
          ++lnum;
      //In an empty buffer the empty line is going to be replaced, include it in the saved lines.
      if ((BUFEMPTY() ? u_save(0, 2) : u_save(lnum - 1, lnum)) == FAIL)
          goto end;
      if (dir == FORWARD)
          curPor->cursor.lnum = lnum - 1;
      else
          curPor->cursor.lnum = lnum;
      curBook->opStart = curPor->cursor;   // for mark_adjust()
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
            if (ml_append(curBook->mem.lineCount, (CS)"", (ColNr)1, FALSE) == FAIL)
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
         mch_memmove(ptr, oldp, (Unt)bd.textcol);
         ptr += bd.textcol;

         // may insert some spaces before the new text
         memset(ptr, ' ', (Unt)bd.startspaces);
         ptr += bd.startspaces;

         // insert the new text
         for (j = 0; j < count; ++j) {
            mch_memmove(ptr, y_array[i].c, (Unt)yanklen);
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
         mch_memmove(ptr, oldp + bd.textcol + delcount,
               (Unt)(oldlen - bd.textcol - delcount + 1));
         ml_replace(curPor->cursor.lnum, newp, FALSE);

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
               mch_memmove(newp, oldp, (Unt)col);
               ptr = newp + col;
               for (i = 0; i < count; ++i) {
                  mch_memmove(ptr, y_array[0].c, (Unt)yanklen);
                  ptr += yanklen;
               }
               mch_memmove(ptr, oldp + col, (Unt)(oldlen - col) + 1);       // +1 for ZERO

                // compute the byte offset for the last character
                first_byte_off = mb_head_off(newp, ptr - 1);

                // Note: this may free "newp"
                ml_replace(lnum, newp, FALSE);

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
         int      first_indent = TRUE;
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
               ml_append(lnum, newp, (ColNr)0, FALSE);
               ++new_lnum;
               eeglFree(newp);

               oldp = ml_get(lnum);
               newp = alloc(col + yanklen + 1); // copy first part of line
               mch_memmove(newp, oldp, (Unt)col); // append to first line
               mch_memmove(newp + col, y_array[0].c, (Unt)(yanklen + 1));
               ml_replace(lnum, newp, FALSE);

               curPor->cursor.lnum = lnum;
               i = 1;
            }

            for (; i < y_size; ++i) {
                if (y_type != MCHAR || i < y_size - 1) {
               if (ml_append(lnum, y_array[i].c, (ColNr)0, FALSE) == FAIL)
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
                   first_indent = FALSE;
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
         mark_adjust(curBook->opStart.lnum + (y_type == MCHAR), (LineNr)MAXLNUM, nr_lines, 0L);

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
   curPor->setCursWant = TRUE;

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

    VIsual_active = FALSE;

    // If the cursor is past the end of the line put it at the end.
    adjust_cursor_eol();
}

// Return the character name of the register with the given number.
int
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
int
get_unname_register(void) {
   return y_previous == NULL ? -1 : y_previous - &y_regs[0];
}

// ":dis" and ":registers": Display the contents of the yank registers.
void
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
      adjust_clip_reg(&name);
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
         int do_show = FALSE;
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
               for (p = yb->y_array[j].c; *p != ZERO && (n -= ptr2cells(p)) >= 0; ++p) {
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
      dis_msg(p, TRUE);
   }

   // display last command line
   if (lastCommlineG != NULL && (arg == NULL || firstOccurrence(arg, ':') != NULL)
                && !gotInterruptG && !message_filtered(lastCommlineG))
   {
      msg_puts(S"\n  c  \":   ");
      dis_msg(lastCommlineG, FALSE);
   }

   // display current file name
   if (curBook->currFileName != NULL
       && (arg == NULL || firstOccurrence(arg, '%') != NULL) && !gotInterruptG
               && !message_filtered(curBook->currFileName))
    {
      msg_puts(S"\n  c  \"%   ");
      dis_msg(curBook->currFileName, FALSE);
   }

   // display alternate file name
   if ((arg == NULL || firstOccurrence(arg, '%') != NULL) && !gotInterruptG) {
      Byte       *fname;
      LineNr    dummy;

      if (bookGetFnameByFileId(0, &fname, &dummy) != FAIL && !message_filtered(fname)) {
          msg_puts(S"\n  c  \"#   ");
          dis_msg(fname, FALSE);
      }
   }

   // display last search pattern
   if (last_search_pat() != NULL
       && (!arg || firstOccurrence(arg, '/') != NULL) && !gotInterruptG
                  && !message_filtered(last_search_pat())
   ) {
      msg_puts(S"\n  c  \"/   ");
      dis_msg(last_search_pat(), FALSE);
   }

   // display last used expression
   if (expr_line && (!arg || firstOccurrence(arg, '=') != NULL)
              && !gotInterruptG && !message_filtered(expr_line)) {
      msg_puts(S"\n  c  \"=   ");
      dis_msg(expr_line, FALSE);
   }
}

//display a string for do_dis(); truncate at end of screen line
private void
dis_msg(
   Byte   *p,
   int      skip_esc       // if TRUE, ignore trailing ESC
){
   int n = (int)visibleColsG - 6;
   while (*p != ZERO && !(*p == ESC && skip_esc && *(p + 1) == ZERO)
         && (n -= ptr2cells(p)) >= 0
   ) {
      int l = utfCharLen(p);
      msgTranslatedSlice((Text){p, l});
      p += l;
   }
   ui_breakcheck();
}

// Put a string into a register.  When the register is not empty, the string is appended.
private void
str_to_reg(
   YankReg   *y_ptr,      // pointer to yank register
   int      yank_type,   // MCHAR, MLINE, MBLOCK, MAUTO
   CS str,      // string to put in register
   long   len,      // length of string
   long   blocklen,   // width of Visual block
   int      str_list)   // TRUE if str is a CString
{
   int      type;         // MCHAR, MLINE or MBLOCK
   int      lnum;
   long   start;
   long   i;
   int      extra;
   int      extraline = 0;      // extra line at the end
   Byte   *s;
   Byte   **ss;

   if (y_ptr->y_array == NULL)      // NULL means empty register
      y_ptr->y_size = 0;

   if (yank_type == MAUTO)
      type = ((str_list || (len > 0 && (str[len - 1] == NL || str[len - 1] == ENTER)))
                          ? MLINE : MCHAR);
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
      if (y_ptr->y_size > 0 && y_ptr->y_type == MCHAR) {
         append = true;
         --newlines;   // uncount newline when appending first line
      }
   }

   // Without any lines make the register empty.
   if (y_ptr->y_size + newlines == 0) {
      EE_CLEAR(y_ptr->y_array);
      return;
   }

   // Allocate an array to hold the pointers to the new register lines.
   // If the register was not empty, move the existing lines to the new array.
   Text* pp = lallocZeroed((y_ptr->y_size + newlines) * sizeof(Text), TRUE);
   for (lnum = 0; lnum < y_ptr->y_size; ++lnum)
      pp[lnum] = y_ptr->y_array[lnum];
   eeglFree(y_ptr->y_array);
   y_ptr->y_array = pp;
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
            extra = (int)y_ptr->y_array[lnum].len;
         } else
            extra = 0;
         s = alloc(i + extra + 1);
         if (extra)
            mch_memmove(s, y_ptr->y_array[lnum].c, (Unt)extra);
         if (append)
            eeglFree(y_ptr->y_array[lnum].c);
         if (i > 0)
            mch_memmove(s + extra, str + start, (Unt)i);
         extra += i;
         s[extra] = ZERO;
         y_ptr->y_array[lnum].c = s;
         y_ptr->y_array[lnum].len = extra;
         ++lnum;
         while (--extra >= 0) {
            if (*s == ZERO)
               *s = '\n';       // replace ZERO with newline
            ++s;
         }
         append = false;          // only first line is appended
      }
   }
   y_ptr->y_type = type;
   y_ptr->y_size = lnum;
   if (type == MBLOCK)
      y_ptr->y_width = (blocklen < 0 ? maxlen - 1 : blocklen);
   else
      y_ptr->y_width = 0;
   y_ptr->y_time_set = eeTime();
}

// Replace the contents of the '~' register with str.
void
dnd_yank_drag_data(CS str, long len) {
   YankReg* curr = y_current;
   y_current = &y_regs[TILDE_REGISTER];
   free_yank_all();
   str_to_reg(y_current, MCHAR, str, len, 0L, FALSE);
   y_current = curr;
}


//Return the type of a register. MAUTO for error. Used for getregtype().
Byte
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

   if (regname != ZERO && !valid_yank_reg(regname, FALSE))
      return MAUTO;

   get_yank_register(regname, FALSE);

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
CS
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
   if (regname != ZERO && !valid_yank_reg(regname, FALSE))
      return NULL;

   regname = may_get_selection(regname);

   if (get_spec_reg(regname, &retval, &allocated, FALSE)) {
      if (retval == NULL)
         return NULL;
      if (allocated)
         return getreg_wrap_one_line(retval, flags);
      return getreg_wrap_one_line(copyStr(retval), flags);
   }

   get_yank_register(regname, FALSE);
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
   int      name,
   YankReg   **old_y_previous,
   YankReg   **old_y_current,
   int      must_append,
   int      *yank_type UNUSED)
{
   if (!valid_yank_reg(name, TRUE)) {     // check for valid reg name
      emsg_invreg(name);
      return FAIL;
   }

   // Don't want to change the current (unnamed) register
   *old_y_previous = y_previous;
   *old_y_current = y_current;

   get_yank_register(name, TRUE);
   if (!y_append && !must_append)
      free_yank_all();
   return OK;
}

private void
finish_write_reg(
   int      name,
   YankReg   *old_y_previous,
   YankReg   *old_y_current)
{
   // Send text of clipboard register to the clipboard.
   may_set_selection();

   // ':let @" = "val"' should change the meaning of the "" register
   if (name != '"')
      y_previous = old_y_previous;
   y_current = old_y_current;
}

//Store string "str" in register "name".
//"maxlen" is the maximum number of bytes to use, -1 for all bytes.
//If "must_append" is TRUE, always append to the register.  Otherwise append
//if "name" is an uppercase letter.
//Note: "maxlen" and "must_append" don't work for the "/" register.
//Careful: 'str' is modified, you may have to use a copy!
//If "str" ends in '\n' or '\r', use linewise, otherwise use characterwise.
void
write_reg_contents(
   int      name,
   CS str,
   int maxlen,
   int must_append)
{
   write_reg_contents_ex(name, str, maxlen, must_append, MAUTO, 0L);
}

void
write_reg_contents_lst(
   int      name,
   Byte   **strings,
   int      maxlen UNUSED,
   int      must_append,
   int      yank_type,
   long   block_len)
{
   YankReg  *old_y_previous, *old_y_current;

   if (name == '/' || name == '=') {
      Byte   *s;

      if (strings[0] == NULL)
         s = E;
      ei (strings[1] != NULL) {
         emsg(_(e_search_pattern_and_expression_register_may_not_contain_two_or_more_lines));
         return;
      } else
         s = strings[0];
      write_reg_contents_ex(name, s, -1, must_append, yank_type, block_len);
      return;
   }

   if (name == '_')       // black hole: nothing to do
      return;

   if (init_write_reg(name, &old_y_previous, &old_y_current, must_append,
      &yank_type) == FAIL)
   return;

   str_to_reg(y_current, yank_type, (CS)strings, -1, block_len, TRUE);
   finish_write_reg(name, old_y_previous, old_y_current);
}

void
write_reg_contents_ex(
   int name,
   CS str,
   int maxlen,
   int must_append,
   int yank_type,
   long block_len
) {
   YankReg   *old_y_previous, *old_y_current;
   long   len = (maxlen >= 0) ? maxlen :  (long)STRLEN(str);
      
   // Special case: '/' search pattern
   if (name == '/') {
      set_last_search_pat(str, RE_SEARCH, TRUE, TRUE);
      return;
   }

   if (name == '#') {
      Book   *buf;

      if (EE_ISDIGIT(*str)) {
         int   num = atoi((char *)str);
         buf = bookFindFileByBookNr(num);
         if (!buf)
            showErrFmtMsg(_(e_book_nr_does_not_exist), (long)num);
      } else
         buf = bookFindFileByBookNr(booklistFindPattern(str, str + len, TRUE, FALSE, FALSE));
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

   str_to_reg(y_current, yank_type, str, len, block_len, FALSE);
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

//EE_ATOM_NAME is the older Eegl-specific selection type for X11.  Still
//supported for when a mix of Eegl versions is used.
#define EE_ATOM_NAME "_EE_TEXT"
# define SELECT_MODE_CHAR   0
# define SELECT_MODE_WORD   1
# define SELECT_MODE_LINE   2

//{{{wayland

#if defined(FEAT_WAYLAND)
// Mime types we support sending and receiving
// Mimes with a lower index in the array are prioritized first when we are
// receiving data.
private const char *supported_mimes[] = {
    EE_ATOM_NAME,
    "text/plain;charset=utf-8",
    "text/plain",
    "UTF8_STRING",
    "STRING",
    "TEXT"
};

private void clip_wl_receive_data(ClipBoard *cbd, const char *mime_type, int fd);
private void clip_wl_request_selection(ClipBoard *cbd);
private void clip_wl_send_data(const char *mime_type, int fd, WaylandSelection);
private int clip_wl_own_selection(ClipBoard *cbd);
private void clip_wl_lose_selection(ClipBoard *cbd);
private void clip_wl_set_selection(ClipBoard *cbd);
private void clip_wl_selection_cancelled(WaylandSelection selection);

#endif

//}}}

//Selection stuff using Visual mode, for cutting and pasting text to other windows.

//Call this to initialise the clipboard.  Pass it FALSE if the clipboard code
//is included, but the clipboard can not be used, or TRUE if the clipboard can
//be used.  Eg unix may call this with FALSE, then call it again with TRUE if
//the GUI starts.
void
clip_init(int can_use){
   ClipBoard* cb = &clipboard;
   for (;;) {
      // No need to init again if cbd is already available
      if (can_use && cb->available)
         goto skip;

      cb->available  = can_use;
      cb->owned      = FALSE;
      cb->start.lnum = 0;
      cb->start.col  = 0;
      cb->end.lnum   = 0;
      cb->end.col    = 0;
      cb->state      = SELECT_CLEARED;

   skip:
      if (cb == &clipboard)
         break;
      cb = &clipboard;
   }
}

//Check whether the VIsual area has changed, and if so try to become the owner
//of the selection, and free any old converted selection we may still have
//lying around.  If the VIsual mode has ended, make a copy of what was
//selected so we can still give it to others.   Will probably have to make sure
//this is called whenever VIsual mode is ended.
void
clip_update_selection(ClipBoard *clip){
   Pos start, end;

   // If visual mode is only due to a redo command ("."), then ignore it
   if (!isRedoVisualBusy && VIsual_active && (stateG & MODE_NORMAL)) {
      if (LT_POS(VIsual, curPor->cursor)) {
         start = VIsual;
         end = curPor->cursor;
         end.col += utfCharLen(ml_get_cursor()) - 1;
      } else {
         start = curPor->cursor;
         end = VIsual;
      }
      if (!EQUAL_POS(clip->start, start)
         || !EQUAL_POS(clip->end, end)
         || clip->vmode != VIsual_mode)
      {
         clip_clear_selection(clip);
         clip->start = start;
         clip->end = end;
         clip->vmode = VIsual_mode;
         clip_free_selection(clip);
         clip_own_selection(clip);
         clip_gen_set_selection(clip);
      }
   }
}

private int
clip_gen_own_selection(ClipBoard *cbd){
#ifdef FEAT_WAYLAND
   return clip_wl_own_selection(cbd);
#else
   return clip_xterm_own_selection(cbd);
#endif
}

void
clip_own_selection(ClipBoard *cbd){
   // Also want to check somehow that we are reading from the keyboard rather than a mapping etc
   // Always own the selection, we might have lost it without being notified, e.g. during a 
   // ":sh" command.
   if (!cbd->available) {
      return;
   }
   int was_owned = cbd->owned;

   cbd->owned = (clip_gen_own_selection(cbd) == OK);
   if (!was_owned && cbd == &clipboard) {
      // May have to show a different kind of hiliting for the selected area. There is no specific
      // redraw command for this, just redraw all portals into the current book.
      if (cbd->owned
            && (get_real_state() == MODE_VISUAL)
            && getDecoFlags(HLF_V) != getDecoFlags(HLF_VNC)
      )
         drawCurBookLater(UPD_INVERTED_ALL);
   }
}

private void
clip_gen_lose_selection(ClipBoard *cbd) {
#ifdef FEAT_WAYLAND
   clip_wl_lose_selection(cbd);
#else
   clip_xterm_lose_selection(cbd);
#endif
}

void
clip_lose_selection(ClipBoard *cbd) {
#ifdef FEAT_X11
   int       was_owned = cbd->owned;
#endif
   int     visual_selection = FALSE;

   if (cbd == &clipboard)
      visual_selection = TRUE;

   clip_free_selection(cbd);
   cbd->owned = FALSE;
   if (visual_selection)
      clip_clear_selection(cbd);
   clip_gen_lose_selection(cbd);
#ifdef FEAT_X11
   if (visual_selection) {
      // May have to show a different kind of hiliting for the selected area. There is no specific 
      // redraw command for this, just redraw all portals into the current book.
      if (was_owned
         && (get_real_state() == MODE_VISUAL)
         && getDecoFlags(HLF_V) != getDecoFlags(HLF_VNC)
         && !exiting)
      {
          update_curbuf(UPD_INVERTED_ALL);
          setcursor();
          cursor_on();
          out_flush();
      }
   }
#endif
}

private void
clip_copy_selection(ClipBoard *clip) {
   if (VIsual_active && (stateG & MODE_NORMAL) && clip->available) {
      clip_update_selection(clip);
      clip_free_selection(clip);
      clip_own_selection(clip);
      if (clip->owned)
         clip_get_selection(clip);
      clip_gen_set_selection(clip);
   }
}

private int global_change_count = 0; // if set, inside a start_global_changes
private int clipboard_needs_update = FALSE; // clipboard needs to be updated
private int clip_did_set_selection = TRUE;

// Save state and reset it.
void
start_global_changes(void) {
   if (++global_change_count > 1)
      return;
   clipboard_needs_update = FALSE;

   if (clip_did_set_selection) {
      clip_did_set_selection = FALSE;
   }
}

//Return TRUE if setting the clipboard was postponed, it already contains the right text.
private int
is_clipboard_needs_update(void){
   return clipboard_needs_update;
}

void
end_global_changes(void){
   if (--global_change_count > 0)
      // recursive
      return;
   if (!clip_did_set_selection) {
      clip_did_set_selection = TRUE;
      if (clipboard_needs_update) {
         // only store something in the clipboard if we have yanked anything to it
         clip_own_selection(&clipboard);
         clip_gen_set_selection(&clipboard);
      }
   }
   clipboard_needs_update = FALSE;
}

// Called when Visual mode is ended: update the selection.
void
clip_auto_select(void){
   clip_copy_selection(&clipboard);
}



// Stuff for general mouse selection, without using Visual mode.

//Compare two screen positions ala strcmp()
private int
clip_compare_pos(int row1, int col1, int row2, int col2) {
   if (row1 > row2) return(1);
   if (row1 < row2) return(-1);
   if (col1 > col2) return(1);
   if (col1 < col2) return(-1);
   return(0);
}

// "how" flags for clip_invert_area()
#define CLIP_CLEAR   1
#define CLIP_SET     2
#define CLIP_TOGGLE  3

#define CLIP_ZINDEX 32000

//Invert or un-invert a rectangle of the screen. "invert" is true if the result is inverted.
private void
clip_invert_rectangle(
   ClipBoard* cbd,
   int row_arg,
   int col_arg,
   int height_arg,
   int width_arg,
   int invert
) {
   int row = row_arg;
   int col = col_arg;
   int height = height_arg;
   int width = width_arg;

   // this goes on top of all popup portals
   screenZindexG = CLIP_ZINDEX;

   if (col < cbd->min_col) {
      width -= cbd->min_col - col;
      col = cbd->min_col;
   }
   if (width > cbd->max_col - col)
      width = cbd->max_col - col;
   if (row < cbd->min_row) {
      height -= cbd->min_row - row;
      row = cbd->min_row;
   }
   if (height > cbd->max_row - row + 1)
      height = cbd->max_row - row + 1;
   screen_draw_rectangle(row, col, height, width, invert);
   screenZindexG = 0;
}

//Invert a region of the display between a starting and ending row and column Values for "how":
//CLIP_CLEAR:  undo inversion
//CLIP_SET:    set inversion
//CLIP_TOGGLE: set inversion if pos1 < pos2, undo inversion otherwise.
//0: invert (GUI only).
private void
clip_invert_area(
   ClipBoard* cbd,
   int      row1,
   int      col1,
   int      row2,
   int      col2,
   int      how)
{
   int      invert = FALSE;
   int      max_col;

   max_col = cbd->max_col - 1;

   if (how == CLIP_SET)
      invert = TRUE;

   // Swap the from and to positions so the from is always before
   if (clip_compare_pos(row1, col1, row2, col2) > 0) {
      int tmp_row, tmp_col;

      tmp_row = row1;
      tmp_col = col1;
      row1   = row2;
      col1   = col2;
      row2   = tmp_row;
      col2   = tmp_col;
   } ei (how == CLIP_TOGGLE)
      invert = TRUE;

   // If all on the same line, do it the easy way
   if (row1 == row2) {
      clip_invert_rectangle(cbd, row1, col1, 1, col2 - col1, invert);
   } else {
      // Handle a piece of the first line
      if (col1 > 0) {
         clip_invert_rectangle(cbd, row1, col1, 1, (int)visibleColsG - col1, invert);
         row1++;
      }

      // Handle a piece of the last line
      if (col2 < max_col) {
         clip_invert_rectangle(cbd, row2, 0, 1, col2, invert);
         row2--;
      }

      // Handle the rectangle that's left
      if (row2 >= row1)
         clip_invert_rectangle(cbd, row1, 0, row2 - row1 + 1, (int)visibleColsG, invert);
   }
}

//Start, continue or end a modeless selection.  Used when editing the
//command-line, in the commline portal and when the mouse is in a popup portal.
void
clip_modeless(int button, int is_click, int is_drag){
   int repeat = ((clipboard.mode == SELECT_MODE_CHAR
      || clipboard.mode == SELECT_MODE_LINE) && (modMaskG & MOD_MASK_2CLICK))
      || (clipboard.mode == SELECT_MODE_WORD && (modMaskG & MOD_MASK_3CLICK));
   if (is_click && button == MOUSE_RIGHT) {
      // Right mouse button: If there was no selection, start one. Otherwise extend the 
      // existing selection.
      if (clipboard.state == SELECT_CLEARED)
         clip_start_selection(mouseColG, mouseRowG, FALSE);
      clip_process_selection(button, mouseColG, mouseRowG, repeat);
   } ei (is_click)
      clip_start_selection(mouseColG, mouseRowG, repeat);
   ei (is_drag) {
      // Don't try extending a selection if there isn't one.  Happens when
      // button-down is in the cmdline and them moving mouse upwards.
      if (clipboard.state != SELECT_CLEARED)
         clip_process_selection(button, mouseColG, mouseRowG, repeat);
   } else // release
      clip_process_selection(MOUSE_RELEASE, mouseColG, mouseRowG, FALSE);
}

//Update the currently selected region by adding and/or subtracting from the
//beginning or end and inverting the changed area(s).
private void
clip_update_modeless_selection(ClipBoard* cb, int row1, int col1, int row2, int col2){
   // See if we changed at the beginning of the selection
   if (row1 != cb->start.lnum || col1 != (int)cb->start.col) {
      clip_invert_area(cb, row1, col1, (int)cb->start.lnum, cb->start.col, CLIP_TOGGLE);
      cb->start.lnum = row1;
      cb->start.col  = col1;
   }

   // See if we changed at the end of the selection
   if (row2 != cb->end.lnum || col2 != (int)cb->end.col) {
      clip_invert_area(cb, (int)cb->end.lnum, cb->end.col, row2, col2, CLIP_TOGGLE);
      cb->end.lnum = row2;
      cb->end.col  = col2;
   }
}

//Find the starting and ending positions of the word at the given row and
//column.  Only white-separated words are recognized here.
#define CHAR_CLASS(c)   (c <= ' ' ? ' ' : eeIsWordc(c))

private void
clip_get_word_boundaries(ClipBoard *cb, int row, int col) {
   if (row >= screenLinesRowsG || col >= screenLinesColsG || screenLinesG == NULL)
      return;

   CS p = screenLinesG + lineOffsetG[row];
   if (p[col] == 0)
      --col;
   int start_class = CHAR_CLASS(p[col]);

   int temp_col = col;
   for ( ; temp_col > 0; temp_col--) {
      if (CHAR_CLASS(p[temp_col - 1]) != start_class && !(p[temp_col - 1] == 0))
          break;
   } 
   cb->word_start_col = temp_col;

   temp_col = col;
   for ( ; temp_col < screenLinesColsG; temp_col++) {
      if (CHAR_CLASS(p[temp_col]) != start_class && !(p[temp_col] == 0))
         break;
   } 
   cb->word_end_col = temp_col;
}

//Find the column position for the last non-whitespace character on the given
//line at or before start_col.
private int
clip_get_line_end(ClipBoard *cbd UNUSED, int row){
   if (row >= screenLinesRowsG || screenLinesG == NULL)
      return 0;
      
   int       i;
   for (i = cbd->max_col; i > 0; i--) {
      if (screenLinesG[lineOffsetG[row] + i - 1] != ' ')
         break;
   } 
   return i;
}

// Start the selection
void
clip_start_selection(int col, int row, int repeated_click) {
   ClipBoard   *cb = &clipboard;
   int row_cp = row;
   int col_cp = col;

   Portal* po = mouseFindPortal(&row_cp, &col_cp, FIND_POPUP);
   if (po && PORTAL_IS_POPUP(po) && popup_is_in_scrollbar(po, row_cp, col_cp))
      // click or double click in scrollbar does not start a selection
      return;

   if (cb->state == SELECT_DONE)
      clip_clear_selection(cb);

   row = check_row(row);
   col = check_col(col);
   col = mb_fix_col(col, row);

   cb->start.lnum  = row;
   cb->start.col   = col;
   cb->end       = cb->start;
   cb->origin_row  = (Short)cb->start.lnum;
   cb->state       = SELECT_IN_PROGRESS;
   if (po && PORTAL_IS_POPUP(po)) {
      //Click in a popup portal restricts selection to that portal, excluding the border.
      cb->min_col = po->portalCol + po->pup.border[3];
      cb->max_col = po->portalCol + popup_width(po) - po->pup.border[1] - po->pup.hasScrollbar;
      if (cb->max_col > screenLinesColsG)
         cb->max_col = screenLinesColsG;
      cb->min_row = po->portalRow + po->pup.border[0];
      cb->max_row = po->portalRow + popup_height(po) - 1 - po->pup.border[2];
   } else {
      cb->min_col = 0;
      cb->max_col = screenLinesColsG;
      cb->min_row = 0;
      cb->max_row = screenLinesRowsG;
   }

   if (repeated_click) {
      if (++cb->mode > SELECT_MODE_LINE)
         cb->mode = SELECT_MODE_CHAR;
   } else
      cb->mode = SELECT_MODE_CHAR;

   switch (cb->mode) {
   case SELECT_MODE_CHAR:
      cb->origin_start_col = cb->start.col;
      cb->word_end_col = clip_get_line_end(cb, (int)cb->start.lnum);
      break;

   case SELECT_MODE_WORD:
      clip_get_word_boundaries(cb, (int)cb->start.lnum, cb->start.col);
      cb->origin_start_col = cb->word_start_col;
      cb->origin_end_col   = cb->word_end_col;

      clip_invert_area(
         cb, (int)cb->start.lnum, cb->word_start_col, (int)cb->end.lnum, cb->word_end_col, CLIP_SET
      );
      cb->start.col = cb->word_start_col;
      cb->end.col   = cb->word_end_col;
      break;

   case SELECT_MODE_LINE:
      clip_invert_area(
         cb, (int)cb->start.lnum, 0, (int)cb->start.lnum, (int)visibleColsG, CLIP_SET
      );
      cb->start.col = 0;
      cb->end.col   = visibleColsG;
      break;
   }

   cb->prev = cb->start;

#ifdef DEBUG_SELECTION
   printf("Selection started at (%ld,%d)\n", cb->start.lnum, cb->start.col);
#endif
}

// Continue processing the selection
void
clip_process_selection(int button, int col, int row, Unt repeated_click) {
   ClipBoard   *cb = &clipboard;
   int diff;
   int slen = 1;   // cursor shape width

   if (button == MOUSE_RELEASE) {
      if (cb->state != SELECT_IN_PROGRESS)
         return;

      // Check to make sure we have something selected
      if (cb->start.lnum == cb->end.lnum && cb->start.col == cb->end.col) {
         cb->state = SELECT_CLEARED;
         return;
      }

#ifdef DEBUG_SELECTION
      printf("Selection ended: (%ld,%d) to (%ld,%d)\n", cb->start.lnum,
         cb->start.col, cb->end.lnum, cb->end.col);
#endif
      clip_copy_modeless_selection(FALSE);

      cb->state = SELECT_DONE;
      return;
   }

   row = check_row(row);
   col = check_col(col);
   col = mb_fix_col(col, row);

   if (col == (int)cb->prev.col && row == cb->prev.lnum && !repeated_click)
      return;

   //When extending the selection with the right mouse button, swap the
   //start and end if the position is before half the selection
   if (cb->state == SELECT_DONE && button == MOUSE_RIGHT) {
   //If the click is before the start, or the click is inside the
   //selection and the start is the closest side, set the origin to the
   //end of the selection.
   if (clip_compare_pos(row, col, (int)cb->start.lnum, cb->start.col) < 0
      || (clip_compare_pos(row, col,
                  (int)cb->end.lnum, cb->end.col) < 0
          && (((cb->start.lnum == cb->end.lnum
             && cb->end.col - col > col - cb->start.col))
         || ((diff = (cb->end.lnum - row) -
                     (row - cb->start.lnum)) > 0
             || (diff == 0 && col < (int)(cb->start.col +
                      cb->end.col) / 2)))))
   {
       cb->origin_row = (Short)cb->end.lnum;
       cb->origin_start_col = cb->end.col - 1;
       cb->origin_end_col = cb->end.col;
   } else {
       cb->origin_row = (Short)cb->start.lnum;
       cb->origin_start_col = cb->start.col;
       cb->origin_end_col = cb->start.col;
   }
   if (cb->mode == SELECT_MODE_WORD && !repeated_click)
      cb->mode = SELECT_MODE_CHAR;
   }

   // set state, for when using the right mouse button
   cb->state = SELECT_IN_PROGRESS;

#ifdef DEBUG_SELECTION
   printf("Selection extending to (%d,%d)\n", row, col);
#endif

   if (repeated_click && ++cb->mode > SELECT_MODE_LINE)
      cb->mode = SELECT_MODE_CHAR;

   switch (cb->mode) {
   case SELECT_MODE_CHAR:
      // If we're on a different line, find where the line ends
      if (row != cb->prev.lnum)
         cb->word_end_col = clip_get_line_end(cb, row);

      // See if we are before or after the origin of the selection
      if (clip_compare_pos(row, col, cb->origin_row, cb->origin_start_col) >= 0) {
         if (col >= (int)cb->word_end_col)
            clip_update_modeless_selection(cb, cb->origin_row,
                cb->origin_start_col, row, (int)visibleColsG);
         else {
            if (mb_lefthalve(row, col))
              slen = 2;
            clip_update_modeless_selection(cb, cb->origin_row, cb->origin_start_col, row, col + slen);
         }
      } else {
         if (mb_lefthalve(cb->origin_row, cb->origin_start_col))
            slen = 2;
         if (col >= (int)cb->word_end_col)
            clip_update_modeless_selection(cb, row, cb->word_end_col,
                cb->origin_row, cb->origin_start_col + slen);
         else
            clip_update_modeless_selection(cb, row, col,
                cb->origin_row, cb->origin_start_col + slen);
      }
      break;

   case SELECT_MODE_WORD:
      // If we are still within the same word, do nothing
      if (row == cb->prev.lnum && col >= (int)cb->word_start_col
             && col < (int)cb->word_end_col && !repeated_click)
         return;

       // Get new word boundaries
       clip_get_word_boundaries(cb, row, col);

      // Handle being after the origin point of selection
      if (clip_compare_pos(row, col, cb->origin_row, cb->origin_start_col) >= 0)
         clip_update_modeless_selection(
            cb, cb->origin_row, cb->origin_start_col, row, cb->word_end_col
         );
      else
         clip_update_modeless_selection(
            cb, row, cb->word_start_col, cb->origin_row, cb->origin_end_col
         );
      break;

   case SELECT_MODE_LINE:
      if (row == cb->prev.lnum && !repeated_click)
         return;

      if (clip_compare_pos(row, col, cb->origin_row, cb->origin_start_col) >= 0)
         clip_update_modeless_selection(cb, cb->origin_row, 0, row, (int)visibleColsG);
      else
         clip_update_modeless_selection(cb, row, 0, cb->origin_row, (int)visibleColsG);
      break;
   }

   cb->prev.lnum = row;
   cb->prev.col  = col;

#ifdef DEBUG_SELECTION
   printf("Selection is: (%ld,%d) to (%ld,%d)\n", cb->start.lnum,
      cb->start.col, cb->end.lnum, cb->end.col);
#endif
}

// Called from outside to clear selected region from the display
void
clip_clear_selection(ClipBoard *cbd){

   if (cbd->state == SELECT_CLEARED)
      return;

   clip_invert_area(
      cbd, (int)cbd->start.lnum, cbd->start.col, (int)cbd->end.lnum, cbd->end.col, CLIP_CLEAR
   );
   cbd->state = SELECT_CLEARED;
}

// Clear the selection if any lines from "row1" to "row2" are inside of it.
void
clip_may_clear_selection(int row1, int row2){
   if (clipboard.state == SELECT_DONE
          && row2 >= clipboard.start.lnum
          && row1 <= clipboard.end.lnum)
      clip_clear_selection(&clipboard);
}

//Called before the screen is scrolled up or down.  Adjusts the line numbers
//of the selection.  Call with big number when clearing the screen.
void
clip_scroll_selection(int       rows)  {    // negative for scroll down
   int       lnum;

   if (clipboard.state == SELECT_CLEARED)
      return;

   lnum = clipboard.start.lnum - rows;
   if (lnum <= 0)
      clipboard.start.lnum = 0;
   ei (lnum >= screenLinesRowsG)   // scrolled off of the screen
      clipboard.state = SELECT_CLEARED;
   else
      clipboard.start.lnum = lnum;

   lnum = clipboard.end.lnum - rows;
   if (lnum < 0)         // scrolled off of the screen
      clipboard.state = SELECT_CLEARED;
   ei (lnum >= screenLinesRowsG)
      clipboard.end.lnum = screenLinesRowsG - 1;
   else
      clipboard.end.lnum = lnum;
}

// Convert from the GUI selection string into the '*'/'+' register.
private void
clip_yank_selection(
    int      type,
    Byte   *str,
    long   len,
    ClipBoard *cbd)
{
   YankReg *y_ptr;

   if (cbd == &clipboard)
      y_ptr = get_y_register(PLUS_REGISTER);
   else
      y_ptr = get_y_register(STAR_REGISTER);

   clip_free_selection(cbd);
   str_to_reg(y_ptr, type, str, len, -1, FALSE);
}


//Copy the currently selected area into the '*' register so it will be available for pasting.
//When "both" is TRUE also copy to the '+' register.
void
clip_copy_modeless_selection(int both UNUSED) {
   Byte   *bufp;
   int      row;
   int      start_col;
   int      end_col;
   int      line_end_col;
   int      add_newline_flag = FALSE;
   Byte   *p;
   int      row1 = clipboard.start.lnum;
   int      col1 = clipboard.start.col;
   int      row2 = clipboard.end.lnum;
   int      col2 = clipboard.end.col;

   // Can't use screenLinesG unless initialized
   if (screenLinesG == NULL)
      return;

   //Make sure row1 <= row2, and if row1 == row2 that col1 <= col2.
   if (row1 > row2) {
      row = row1; row1 = row2; row2 = row;
      row = col1; col1 = col2; col2 = row;
   } ei (row1 == row2 && col1 > col2) {
      row = col1; col1 = col2; col2 = row;
   }
   if (col1 < clipboard.min_col)
      col1 = clipboard.min_col;
   if (col2 > clipboard.max_col)
      col2 = clipboard.max_col;
   if (row1 > clipboard.max_row || row2 < clipboard.min_row)
      return;
   if (row1 < clipboard.min_row)
      row1 = clipboard.min_row;
   if (row2 > clipboard.max_row)
      row2 = clipboard.max_row;
   // correct starting point for being on right half of double-wide char
   p = screenLinesG + lineOffsetG[row1];
   if (p[col1] == 0)
      --col1;

   // Create a temporary buffer for storing the text
   int len = (row2 - row1 + 1) * visibleColsG + 1;
   len *= MB_MAXBYTES;
   CS buffer = alloc(len);

   // Process each row in the selection
   for (bufp = buffer, row = row1; row <= row2; row++) {
      if (row == row1)
          start_col = col1;
      else
          start_col = clipboard.min_col;

      if (row == row2)
          end_col = col2;
      else
          end_col = clipboard.max_col;

      line_end_col = clip_get_line_end(&clipboard, row);

      // See if we need to nuke some trailing whitespace
      if (end_col >= clipboard.max_col && (row < row2 || end_col > line_end_col)
      ){
         // Get rid of trailing whitespace
         end_col = line_end_col;
         if (end_col < start_col)
            end_col = start_col;

         // If the last line extended to the end, add an extra newline
         if (row == row2)
            add_newline_flag = TRUE;
      }

      //If after the first row, we need to always add a newline
      if (row > row1 && !lineWrapsG[row - 1])
         *bufp++ = NL;

      //Safety check for in case resizing went wrong
      if (row < screenLinesRowsG && end_col <= screenLinesColsG) {
         int   i;
         int   ci;

         int off = lineOffsetG[row];
         for (i = start_col; i < end_col; ++i) {
            // The base character is either in screenLinesUCG[] or
            // screenLinesG[].
            if (screenLinesUCG[off + i] == 0)
               *bufp++ = screenLinesG[off + i];
            else {
               bufp += mb_char2bytes(screenLinesUCG[off + i], bufp);
               for (ci = 0; ci < MAX_COMBINED_SYMBOLS; ++ci) {
                   // Add a composing character.
                   if (screenLinesCG[ci][off + i] == 0)
                  break;
                   bufp += mb_char2bytes(screenLinesCG[ci][off + i],
                                 bufp);
               }
            }
            // Skip right half of double-wide character.
            if (screenLinesG[off + i + 1] == 0)
               ++i;
         }
      }
   }

   // Add a newline at the end if the selection ended there
   if (add_newline_flag)
      *bufp++ = NL;

    // First cleanup any old selection and become the owner.
    clip_free_selection(&clipboard);
    clip_own_selection(&clipboard);

    // Yank the text into the '*' register.
    clip_yank_selection(MCHAR, buffer, (long)(bufp - buffer), &clipboard);

    // Make the register contents available to the outside world.
    clip_gen_set_selection(&clipboard);

#ifdef FEAT_X11
   if (both) {
      // Do the same for the '+' register.
      clip_free_selection(&clipboard);
      clip_own_selection(&clipboard);
      clip_yank_selection(MCHAR, buffer, (long)(bufp - buffer), &clipboard);
      clip_gen_set_selection(&clipboard);
   }
#endif
   eeglFree(buffer);
}

private void
clip_gen_set_selection(ClipBoard *cbd){
   if (!clip_did_set_selection) {
      // Updating postponed, so that accessing the system clipboard won't
      // hang Eegl when accessing it many times (e.g. on a :g command).
      if (cbd == &clipboard) {
          clipboard_needs_update = TRUE;
          return;
      }
   }
#ifdef FEAT_WAYLAND
   clip_wl_set_selection(cbd);
#else
   clip_xterm_set_selection(cbd);
#endif
}

private void
clip_gen_request_selection(ClipBoard *cbd){
#ifdef FEAT_WAYLAND
   clip_wl_request_selection(cbd);
#else
   clip_xterm_request_selection(cbd);
#endif
}

// Stuff for the X clipboard
#if defined(FEAT_X11) || defined(PROTO)
# include <X11/Xatom.h>
# include <X11/Intrinsic.h>

//Open the application context (if it hasn't been opened yet).
//Used for the xterm clipboard.
void
open_app_context(void) {
   if (app_context == NULL) {
      XtToolkitInitialize();
      app_context = XtCreateApplicationContext();
   }
}

private Atom   eeglAtom;   // Eegl's own special selection format
private Atom   utf8_atom;
private Atom   compound_text_atom;
private Atom   text_atom;
private Atom   targets_atom;
private Atom   timestamp_atom;   // Used to get a timestamp

void
x11_setup_atoms(Display *dpy) {
   eeglAtom           = XInternAtom(dpy, EE_ATOM_NAME,   False);
   utf8_atom          = XInternAtom(dpy, "UTF8_STRING",   False);
   compound_text_atom = XInternAtom(dpy, "COMPOUND_TEXT", False);
   text_atom          = XInternAtom(dpy, "TEXT",      False);
   targets_atom       = XInternAtom(dpy, "TARGETS",      False);
   clipboard.sel_atom = XA_PRIMARY;
   clipboard.sel_atom = XInternAtom(dpy, "CLIPBOARD",      False);
   timestamp_atom     = XInternAtom(dpy, "TIMESTAMP",      False);
}

//X Selection stuff, for cutting and pasting text to other portals.

private Boolean
clip_x11_convert_selection_cb(
    Widget   w UNUSED,
    Atom   *sel_atom,
    Atom   *target,
    Atom   *type,
    XtPointer   *value,
    Ulong   *length,
    int      *format)
{
   static Byte   *save_result = NULL;
   static Ulong   save_length = 0;
   Byte       *string;
   int          motion_type;
   ClipBoard    *cbd;
   int          i;

   if (*sel_atom == clipboard.sel_atom)
      cbd = &clipboard;
   else
      cbd = &clipboard;

   if (!cbd->owned)
      return False;       // Shouldn't ever happen

   // requestor wants to know what target types we support
   if (*target == targets_atom) {
      static Atom array[7];

      *value = (XtPointer)array;
      i = 0;
      array[i++] = targets_atom;
      array[i++] = eeglAtom;
      array[i++] = utf8_atom;
      array[i++] = XA_STRING;
      array[i++] = text_atom;
      array[i++] = compound_text_atom;

      *type = XA_ATOM;
      // This used to be: *format = sizeof(Atom) * 8; but that caused
      // crashes on 64 bit machines. (Peter Derr)
      *format = 32;
      *length = i;
      return True;
    }

   if ( *target != XA_STRING
         && (*target != utf8_atom)
         && *target != eeglAtom
         && *target != text_atom
         && *target != compound_text_atom
   )
      return False;

   clip_get_selection(cbd);
   motion_type = clip_convert_selection(&string, length, cbd);
   if (motion_type < 0)
      return False;

   // For our own format, the first byte contains the motion type
   if (*target == eeglAtom)
      (*length)++;


   if (save_length < *length || save_length / 2 >= *length)
      *value = XtRealloc((char *)save_result, (Cardinal)*length + 1);
   else
      *value = save_result;
   save_result = (CS)*value;
   save_length = *length;

   if (*target == XA_STRING || (*target == utf8_atom)) {
      mch_memmove(save_result, string, (Unt)(*length));
      *type = *target;
   } ei (*target == compound_text_atom || *target == text_atom) {
      XTextProperty   text_prop;
      char      *string_nt = (char *)save_result;
      int      conv_result;

      // create ZERO terminated string which XmbTextListToTextProperty wants
      mch_memmove(string_nt, string, (Unt)*length);
      string_nt[*length] = ZERO;
      conv_result = XmbTextListToTextProperty(X_DISPLAY, &string_nt,
                     1, XCompoundTextStyle, &text_prop);
      if (conv_result != Success) {
          eeglFree(string);
          return False;
      }
      *value = (XtPointer)(text_prop.value);   //    from plain text
      *length = text_prop.nitems;
      *type = compound_text_atom;
      XtFree((char *)save_result);
      save_result = (CS)*value;
      save_length = *length;
   } else {
      save_result[0] = motion_type;
      mch_memmove(save_result + 1, string, (Unt)(*length - 1));
      *type = eeglAtom;
   }
   *format = 8;       // 8 bits per char
   eeglFree(string);
   return True;
}

private void
clip_x11_lose_ownership_cb(Widget w UNUSED, Atom *sel_atom) {
   if (*sel_atom == clipboard.sel_atom)
      clip_lose_selection(&clipboard);
   else
      clip_lose_selection(&clipboard);
}

private void
clip_x11_notify_cb(Widget w UNUSED, Atom *sel_atom UNUSED, Atom *target UNUSED) {
   // To prevent automatically freeing the selection value.
}

// Property callback to get a timestamp for XtOwnSelection.
# if defined(FEAT_X11)
private void
clip_x11_timestamp_cb(
   Widget   w,
   XtPointer   n UNUSED,
   XEvent   *event,
   Boolean   *cont UNUSED
) {
   Atom       actual_type;
   int          format;
   unsigned  long  nitems, bytes_after;
   unsigned char   *prop=NULL;
   XPropertyEvent  *xproperty=&event->xproperty;

   // Must be a property notify, state can't be Delete (True), has to be
   // one of the supported selection types.
   if (event->type != PropertyNotify || xproperty->state
          || (xproperty->atom != clipboard.sel_atom && xproperty->atom != clipboard.sel_atom))
      return;

   if (XGetWindowProperty(xproperty->display, xproperty->window,
     xproperty->atom, 0, 0, False, timestamp_atom, &actual_type, &format,
                  &nitems, &bytes_after, &prop))
      return;

   if (prop)
      XFree(prop);

   // Make sure the property type is "TIMESTAMP" and it's 32 bits.
   if (actual_type != timestamp_atom || format != 32)
      return;

   // Get the selection, using the event timestamp.
   if (XtOwnSelection(w, xproperty->atom, xproperty->time,
       clip_x11_convert_selection_cb, clip_x11_lose_ownership_cb,
       clip_x11_notify_cb) == OK
   ) {
   // Set the "owned" flag now, there may have been a call to lose_ownership_cb in between.
   if (xproperty->atom == clipboard.sel_atom)
       clipboard.owned = TRUE;
   else
       clipboard.owned = TRUE;
    }
}

void
x11_setup_selection(Widget w){
    XtAddEventHandler(w, PropertyChangeMask, False,
       /*(XtEventHandler)*/clip_x11_timestamp_cb, (XtPointer)NULL);
}
# endif

private void
clip_x11_request_selection_cb(
   Widget   w UNUSED,
   XtPointer   success,
   Atom   *sel_atom,
   Atom   *type,
   XtPointer   value,
   Ulong   *length,
   int      *format)
{
   int      motion_type = MAUTO;
   Ulong   len;
   Byte   *p;
   char   **text_list = NULL;
   ClipBoard   *cbd;
   Byte   *tmpbuf = NULL;

   if (*sel_atom == clipboard.sel_atom)
      cbd = &clipboard;
   else
      cbd = &clipboard;

   if (value == NULL || *length == 0) {
      clip_free_selection(cbd);   // nothing received, clear register
      *(int *)success = FALSE;
      return;
   }
   p = (CS)value;
   len = *length;
   if (*type == eeglAtom) {
      motion_type = *p++;
      len--;
   } ei (*type == compound_text_atom || *type == utf8_atom) {
      XTextProperty   text_prop;
      int      n_text = 0;
      int      status;

      text_prop.value = (unsigned char *)value;
      text_prop.encoding = *type;
      text_prop.format = *format;
      text_prop.nitems = len;
      status = XmbTextPropertyToTextList(X_DISPLAY, &text_prop, &text_list, &n_text);
      if (status != Success || n_text < 1) {
         *(int *)success = FALSE;
         return;
      }
      p = (CS)text_list[0];
      len = STRLEN(p);
    }
    clip_yank_selection(motion_type, p, (long)len, cbd);

   if (text_list != NULL)
   XFreeStringList(text_list);
    eeglFree(tmpbuf);
    XtFree((char *)value);
    *(int *)success = TRUE;
}

void
clip_x11_request_selection(
   Widget   myShell,
   Display   *dpy,
   ClipBoard   *cbd)
{
   XEvent   event;
   Atom   type;
   static int   success;
   int      i;
   Tyme   start_time;
   int      timed_out = FALSE;

   for (i = 1; i < 6; i++) {
      switch (i) {
      case 1:  type = eeglAtom;      break;
      case 2:  type = utf8_atom;      break;
      case 3:  type = compound_text_atom; break;
      case 4:  type = text_atom;      break;
      default: type = XA_STRING;
      }
      success = MAYBE;
      XtGetSelectionValue(myShell, cbd->sel_atom, type,
          clip_x11_request_selection_cb, (XtPointer)&success, CurrentTime);

      // Make sure the request for the selection goes out before waiting for
      // a response.
      XFlush(dpy);

      /*
       * Wait for result of selection request, otherwise if we type more
       * characters, then they will appear before the one that requested the
       * paste!  Don't worry, we will catch up with any other events later.
       */
      start_time = time(NULL);
      while (success == MAYBE) {
         if (XCheckTypedEvent(dpy, PropertyNotify, &event)
             || XCheckTypedEvent(dpy, SelectionNotify, &event)
             || XCheckTypedEvent(dpy, SelectionRequest, &event))
          {
         // This is where clip_x11_request_selection_cb() should be
         // called.  It may actually happen a bit later, so we loop
         // until "success" changes.
         // We may get a SelectionRequest here and if we don't handle
         // it we hang.  KDE klipper does this, for example.
         // We need to handle a PropertyNotify for large selections.
         XtDispatchEvent(&event);
         continue;
          }

         // Time out after 2 to 3 seconds to avoid that we hang when the
         // other process doesn't respond.  Note that the SelectionNotify
         // event may still come later when the selection owner comes back
         // to life and the text gets inserted unexpectedly.  Don't know
         // why that happens or how to avoid that :-(.
         if (time(NULL) > start_time + 2) {
            timed_out = TRUE;
            break;
         }

         // Do we need this?  Probably not.
         XSync(dpy, False);

          // Wait for 1 msec to avoid that we eat up all CPU time.
          ui_delay(1L, TRUE);
      }

      if (success == TRUE)
          return;

      // don't do a retry with another type after timing out, otherwise we
      // hang for 15 seconds.
      if (timed_out)
          break;
   }

   // Final fallback position - use the X CUT_BUFFER0 store
   yank_cut_buffer0(dpy, cbd);
}

void
clip_x11_lose_selection(Widget myShell, ClipBoard *cbd) {
   XtDisownSelection(myShell, cbd->sel_atom, XtLastTimestampProcessed(XtDisplay(myShell)));
}

int
clip_x11_own_selection(Widget myShell, ClipBoard *cbd){
   // When using the GUI we have proper timestamps, use the one of the last
   // event.  When in the console we don't get events (the terminal gets
   // them), Get the time by a zero-length append, clip_x11_timestamp_cb will
   // be called with the current timestamp.
   if (!XChangeProperty(XtDisplay(myShell), XtWindow(myShell),
         cbd->sel_atom, timestamp_atom, 32, PropModeAppend, NULL, 0))
      return FAIL;
   // Flush is required in a terminal as nothing else is doing it.
   XFlush(XtDisplay(myShell));
   return OK;
}

//Send the current selection to the clipboard.  Do nothing for X because we
//will fill in the selection only when requested by another app.
void
clip_x11_set_selection(ClipBoard *cbd UNUSED)
{
}

#endif

#if defined(FEAT_X11) || defined(PROTO)
// Get the contents of the X CUT_BUFFER0 and put it in "cbd".
void
yank_cut_buffer0(Display *dpy, ClipBoard *cbd){
   int      nbytes = 0;
   CS buffer = (CS)XFetchBuffer(dpy, &nbytes, 0);

   if (nbytes <= 0) {
      return;
   } 
   int  done = FALSE;

   if (!done)  // use the text without conversion
      clip_yank_selection(MCHAR, buffer, (long)nbytes, cbd);
   XFree((void *)buffer);
   if (p_verbose > 0) {
      verbose_enter();
      verb_msg(_("Used CUT_BUFFER0 instead of empty selection"));
      verbose_leave();
   }
}
#endif

//SELECTION / PRIMARY ('*')
//
//Text selection stuff that uses the selection register '*'.  It is the last text we
//had highlighted with VIsual mode.  With mouse support, clicking the middle
//button performs the paste, otherwise you will need to do <"*p>. "
//If not under X, it is synonymous with the clipboard register '+'.
//
//X CLIPBOARD ('+')
//
//Text selection stuff that uses the clipboard register '+'.
//Under X, this matches the standard cut/paste buffer CLIPBOARD selection.
//It will be used for unnamed cut/pasting is 'clipboard' contains "unnamed",
//otherwise you will need to do <"+p>. "
//If not under X, it is synonymous with the selection register '*'.

void
clip_free_selection(ClipBoard *cbd) {
   YankReg *y_ptr = get_y_current();

   if (cbd == &clipboard)
      set_y_current(get_y_register(PLUS_REGISTER));
   else
      set_y_current(get_y_register(STAR_REGISTER));
   free_yank_all();
   get_y_current()->y_size = 0;
   set_y_current(y_ptr);
}

//Get the selected text and put it in register '*' or '+'.
void
clip_get_selection(ClipBoard *cbd) {
   YankReg   *old_y_previous, *old_y_current;
   Pos   old_cursor;
   Pos   old_visual;
   int      old_visual_mode;
   ColNr   old_curswant;
   int      old_set_curswant;
   Pos   old_op_start, old_op_end;
   Operator   oa;
   ActionArg   ca;

   if (cbd->owned) {
      if ((cbd == &clipboard
            && get_y_register(PLUS_REGISTER)->y_array != NULL)
            || (cbd == &clipboard
                && get_y_register(STAR_REGISTER)->y_array != NULL))
         return;

      // Avoid triggering autocmds such as TextYankPost.
      block_autocmds();

      // Get the text between clipboard.start & clipboard.end
      old_y_previous = get_y_previous();
      old_y_current = get_y_current();
      old_cursor = curPor->cursor;
      old_curswant = curPor->cursWant;
      old_set_curswant = curPor->setCursWant;
      old_op_start = curBook->opStart;
      old_op_end = curBook->opEnd;
      old_visual = VIsual;
      old_visual_mode = VIsual_mode;
      clear_oparg(&oa);
      oa.regname = (cbd == &clipboard ? '+' : '*');
      oa.opTy = OP_YANK;
      CLEAR_FIELD(ca);
      ca.oper = &oa;
      ca.cmdchar = 'y';
      ca.count1 = 1;
      ca.retval = CA_NO_ADJ_OP_END;
      visualOperator(&ca, 0, TRUE);

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
   } ei (!is_clipboard_needs_update()) {
      clip_free_selection(cbd);

      // Try to get selected text from another portal
      clip_gen_request_selection(cbd);
   }
}


//Convert the '*'/'+' register into a selection string returned in *str with length *len.
//Return the motion type, or -1 for failure.
int
clip_convert_selection(Byte **str, Ulong *len, ClipBoard *cbd) {
   Byte   *p;
   int      lnum;
   int      i, j;
   YankReg   *y_ptr;

   if (cbd == &clipboard)
      y_ptr = get_y_register(PLUS_REGISTER);
   else
      y_ptr = get_y_register(STAR_REGISTER);

   *str = NULL;
   *len = 0;
   if (y_ptr->y_array == NULL)
      return -1;

   for (i = 0; i < y_ptr->y_size; i++)
      *len += (Ulong)y_ptr->y_array[i].len + 1; // 1 for the end of line char

   // Don't want newline character at end of last line if we're in MCHAR mode.
   if (y_ptr->y_type == MCHAR && *len >= 1)
      (*len)--;

   p = *str = alloc(*len + 1);   // add one to avoid zero
   lnum = 0;
   for (i = 0, j = 0; i < (int)*len; i++, j++) {
      if (y_ptr->y_array[lnum].c[j] == '\n')
         p[i] = ZERO;
      ei (y_ptr->y_array[lnum].c[j] == ZERO) {
         p[i] = '\n';
         lnum++;
         j = -1;
      } else
         p[i] = y_ptr->y_array[lnum].c[j];
   }
   return y_ptr->y_type;
}

//When "regname" is a clipboard register, obtain the selection.  If it's not
//available return zero, otherwise return "regname".
int
may_get_selection(int regname) {
   if (regname == '*') {
      if (!clipboard.available)
         regname = 0;
      else
         clip_get_selection(&clipboard);
   } ei (regname == '+') {
      if (!clipboard.available)
         regname = 0;
      else
         clip_get_selection(&clipboard);
   }
   return regname;
}

// If we have written to a clipboard register, send the text to the clipboard.
private void
may_set_selection(void){
   if ((get_y_current() == get_y_register(STAR_REGISTER)) && clipboard.available) {
      clip_own_selection(&clipboard);
      clip_gen_set_selection(&clipboard);
   } ei ((get_y_current() == get_y_register(PLUS_REGISTER)) && clipboard.available) {
      clip_own_selection(&clipboard);
      clip_gen_set_selection(&clipboard);
   }
}

//Adjust the register name pointed to with "rp" for the clipboard being
//used always and the clipboard being available.
void
adjust_clip_reg(int *rp){
   //If no reg. specified, and "unnamed" or "unnamedplus" is in 'clipboard',
   //use '*' or '+' reg, respectively. "unnamedplus" prevails.
   if (*rp == 0) {
      *rp = (clipboard.available) ? '+' : '*';
   }
   if ((!clipboard.available && *rp == '*') || (!clipboard.available && *rp == '+')) {
      msg_warn_missing_clipboard();
      *rp = 0;
   }
}

#if defined(FEAT_WAYLAND) || defined(PROTO)

//Read data from a file descriptor and write it to the given clipboard.
private void
clip_wl_receive_data(ClipBoard *cbd, const char *mime_type, int fd) {
   Byte   *start, *final, *enc;
   ArrayList   buf;
   int      motion_type = MAUTO;
   Long   r = 0;
   fd_set rfds;
   TimeVal  tv;

   FD_ZERO(&rfds);
   FD_SET(fd, &rfds);

   // Make pipe (read end) non-blocking
   if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) == -1)
      return;

   ga_init2(&buf, 1, 4096);

   // 4096 bytes seems reasonable for initial buffer size
   if (ga_grow(&buf, 4096) == FAIL)
      return;

   start = buf.c;

   // Only poll before reading when we first start, then we do non-blocking
   // reads and check for EAGAIN or EINTR to signal to poll again.
   goto poll_data;

   while (errno = 0, TRUE) {
      r = read(fd, start, buf.cap - 1 - buf.len);

      if (r == 0)
          break;
      ei (r < 0) {
          if (errno == EAGAIN || errno == EINTR) {
   poll_data:
         tv.tv_sec = 0;
         tv.tv_usec = p_wtm * 1000;
         if (select(fd + 1, &rfds, NULL, NULL, &tv) > 0)
            continue;
         }
         break;
      }

      start += r;
      buf.len += r;

      // Realloc if we are at the end of the buffer
      if (buf.len >= buf.cap - 1) {
         if (ga_grow(&buf, 8192) == FAIL)
            break;
         start = buf.c + buf.len;
      }
   }

   if (buf.len == 0) {
      clip_free_selection(cbd); // Nothing received, clear register
      ga_clear(&buf);
      return;
   }

   final = buf.c;

   if (STRCMP(mime_type, EE_ATOM_NAME) == 0 && buf.len >= 2) {
      motion_type = *final++;;
      buf.len--;
   }

   clip_yank_selection(motion_type, final, (long)buf.len, cbd);
   ga_clear(&buf);
}

//Get the current selection and fill the respective register for cbd with the data.
private void
clip_wl_request_selection(ClipBoard *cbd) {
   WaylandSelection       selection;
   ArrayList          *mime_types;
   int             len;
   const char          *chosen_mime = NULL;

   if (cbd == &clipboard)
      selection = WAYLAND_SELECTION_PRIMARY;
   else
      return;

   // Get mime types that the source client offers
   mime_types = wayland_cb_get_mime_types(selection);

   if (mime_types == NULL || mime_types->len == 0) {
      // Selection is empty/cleared
      clip_free_selection(cbd);
      return;
   }

   len = ARRAY_LENGTH(supported_mimes);

   // Loop through and pick the one we want to receive from
   for (int i = 0; i < len && chosen_mime == NULL; i++) {
      for (int k = 0; k < mime_types->len && chosen_mime == NULL; k++) {
         char *mime_type = ((char**)mime_types->c)[k];

         if (STRCMP(mime_type, supported_mimes[i]) == 0)
            chosen_mime = supported_mimes[i];
      }
   }
   if (!chosen_mime)
      return;

   int fd = wayland_cb_receive_data(chosen_mime, selection);

   if (fd == -1)
      return;

   // Start reading the file descriptor returned
   clip_wl_receive_data(cbd, chosen_mime, fd);

   close(fd);
}

//Write data from either the clip or plus register, depending on the given
//selection, to the file descriptor that the receiving client will read from.
private void
clip_wl_send_data(
   const char       *mime_type,
   int          fd,
   WaylandSelection selection
) {
   ClipBoard       *cbd;
   Ulong       length;
   CS string;
   Long       written = 0;
   Unt       total = 0;
   int          did_motion_type = TRUE;
   int          motion_type;
   int          skip_len_check = FALSE;
   fd_set       wfds;
   TimeVal  tv;

   FD_ZERO(&wfds);
   FD_SET(fd, &wfds);
   tv.tv_sec = 0;
   tv.tv_usec = p_wtm * 1000;
   if (selection == WAYLAND_SELECTION_REGULAR)
      cbd = &clipboard;
   ei (selection == WAYLAND_SELECTION_PRIMARY)
      cbd = &clipboard;
   else
      return;

   // Shouldn't happen unless there is a bug.
   if (!cbd->owned)
      return;

   // Get the current selection
   clip_get_selection(cbd);
   motion_type = clip_convert_selection(&string, &length, cbd);

   if (motion_type < 0)
      goto exit;

   if (STRCMP(mime_type, EE_ATOM_NAME) == 0)
      did_motion_type = FALSE;

   while ((total < (Unt)length || skip_len_check) 
         && select(fd + 1, NULL, &wfds, NULL, &tv) > 0
   ) {
      // First byte sent is motion type for Eegl-specific formats
      if (!did_motion_type) {
         if (total == 1) {
            total = 0;
            did_motion_type = TRUE;
            continue;
         }
         // We cast to char so that we only send one byte
         written = write( fd, (Byte*)&motion_type, 1);
         skip_len_check = TRUE;
      } else {
         // write the actual selection to the fd
         written = write(fd, string + total, length - total);
         if (skip_len_check)
             skip_len_check = FALSE;
      }

      if (written == -1)
         break;
      total += written;

      tv.tv_sec = 0;
      tv.tv_usec = p_wtm * 1000;
   }
exit:
   eeglFree(string);
}

//Called if another client gains ownership of the given selection. If so then
//lose the selection internally.
private void
clip_wl_selection_cancelled(WaylandSelection selection) {
   if (selection == WAYLAND_SELECTION_REGULAR)
   clip_lose_selection(&clipboard);
    ei (selection == WAYLAND_SELECTION_PRIMARY)
   clip_lose_selection(&clipboard);
}

//Own the selection that cbd corresponds to. Start listening for requests from
//other Wayland clients so they can receive data from us. Return OK on success and FAIL on failure.
private int
clip_wl_own_selection(ClipBoard *cbd) {
   WaylandSelection selection;

   if (cbd == &clipboard)
      selection = WAYLAND_SELECTION_PRIMARY;
   else
      return FAIL;

   return wayland_cb_own_selection(
      clip_wl_send_data,
      clip_wl_selection_cancelled,
      supported_mimes,
      sizeof(supported_mimes)/sizeof(*supported_mimes),
      selection
   );
}

//Disown the selection that cbd corresponds to. Note that the the cancelled
//event is not sent when the data source is destroyed.
private void
clip_wl_lose_selection(ClipBoard *cbd) {
   if (cbd == &clipboard)
      wayland_cb_lose_selection(WAYLAND_SELECTION_REGULAR);

   // wayland_cb_lose_selection(selection);
}

//Send the current selection to the clipboard. Do nothing for Wayland because
//we will fill in the selection only when requested by another client.
private void
clip_wl_set_selection(ClipBoard *cbd UNUSED) {
}

#endif // FEAT_WAYLAND

//}}}
