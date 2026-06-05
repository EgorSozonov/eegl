//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## insert.c: functions for Insert mode (manual input of text)

#include "eegl.h"

#define BACKSPACE_CHAR           1
#define BACKSPACE_WORD           2
#define BACKSPACE_WORD_NOT_SPACE 3
#define BACKSPACE_LINE           4

//Set when doing something for completion that may call edit() recursively, which is not allowed.
private Boole isCompletionBusyS = FALSE;

//{{{forward declarations

private void insertStartVisualBlockMode(void);
private void insertRegular(Unt, Boole, Boole);
private void redo_literal(int c);
private void start_arrow_common(Pos *end_insert_pos, int change);
private void check_spell_redraw(void);
private void stop_insert(Pos *end_insert_pos, int esc, int nomove);
private Boole  echeck_abbr(Unt);
private int del_char_after_col(int limit_col);
private void insertRegisterContents(void);
private void ins_ctrl_g(void);
private void ins_ctrl_hat(void);
private int  ins_esc(long *count, int commChar, int nomove);
private int ins_start_select(int c);
private void ins_ctrl_o(void);
private void ins_shift(Unt c, int lastc);
private void ins_del(void);
private int  ins_bs(int c, int mode, int *inserted_space_p);
private void ins_left(void);
private void ins_home(Unt c);
private void ins_end(Unt c);
private void ins_s_left(void);
private void ins_right(void);
private void ins_s_right(void);
private void ins_up(int startcol);
private void ins_pageup(void);
private void ins_down(int startcol);
private void ins_pagedown(void);
private void ins_drop(void);
private int ins_tab(void);
private Unt ins_ctrl_ey(Unt tc);
private void ins_ctrl_x(void);
private int ctrl_x_mode_normal(void);
private int ctrl_x_mode_scroll(void);
private int ctrl_x_mode_files(void);
private int ctrl_x_mode_tags(void);
private int ctrl_x_mode_path_patterns(void);
private int ctrl_x_mode_path_defines(void);
private int ctrl_x_mode_dictionary(void);
private int ctrl_x_mode_thesaurus(void);
private int ctrl_x_mode_cmdline(void);
private int ctrl_x_mode_function(void);
private int ctrl_x_mode_omni(void);
private int ctrl_x_mode_eval(void);
private int ctrl_x_mode_line_or_eval(void);
private int ctrl_x_mode_register(void);
private void compl_status_clear(void);
private int has_compl_option(int dict_opt);
private int ins_compl_accept_char(int c);
private int ins_compl_has_shown_match(void);
private int ins_compl_long_shown_match(void);
private int pum_wanted(void);
private Unt ins_compl_leader_len(void);
private void ins_compl_clear(void);
private Boole ins_compl_used_match(void);
private void ins_compl_init_get_longest(void);
private int ins_compl_enter_selects(void);
private ColNr ins_compl_col(void);
private int ins_compl_preinsert_effect(void);
private int ins_compl_bs(void);
private void ins_compl_addleader(int c);
private void ins_compl_addfrommatch(void);
private Boole ins_compl_prep(Unt c);
private int ins_compl_cancel(void);
private void ins_compl_delete(void);
private void ins_compl_insert(int move_cursor);
private Unt ins_complete(Unt c, Boole enable_pum);
private Boole ins_compl_setup_autocompl(Unt c);
private int ins_eol(Unt c);
private void redrawInInsertMode(Boole ready);

//}}}

private ColNr   insertStartG_textlen;   // length of line when insert started
private ColNr   insertStartG_blank_vcol;   // vcol for first inserted blank
private Boole   update_insertStartOrigS = true; // set insertStartOrigG to insertStartG

private Text last_insert = {null, 0}; //text of the previous insert, K_SPECIAL and CSI are escaped
private int   last_insert_skip; // nr of chars in front of previous insert
private int   new_insert_skip;  // nr of chars in front of current insert
private int   did_restart_edit; // "restart_edit" when calling edit()

private int can_cindent; // may do cindenting on this line

private Boole needUndoS; // call u_save() before inserting a char. Set when edit() is called.
                             // after that, arrow_used is used.

private int   dont_sync_undo = FALSE;   // CTRL-G U prevents syncing undo for
                                       // the next left/right cursor key
               
//{{{Editing. Actual input character handling in Insert mode

// Return the character immediately before the cursor.
private int
char_before_cursor(void) {
   if (curPor->cursor.col == 0)
      return -1;

   CS line = ml_get_curline();

   CS p = line + curPor->cursor.col;
   int prev_len = mb_head_off(line, p - 1) + 1;
   return mb_ptr2char(p - prev_len);
}

//edit(): Start inserting text.
//
//"commChar" can be:
//'i'   normal insert command
//'a'   normal append command
//K_PS bracketed paste
//'r'   "r<CR>" command: insert one <CR>.  Note: count can be > 1, for redo,
//  but still only one <CR> is inserted.  The <Esc> is not used for redo.
//'g'   "gI" command.
//
//This function is not called recursively.  For CTRL-O commands, it returns
//and lets the caller handle the Normal-mode command.
//
//Return TRUE if a CTRL-O command caused the return (insert mode pending).
int
edit(Unt commChar, int startln, long count){
                   // if set, insert at start of line
   Unt c = 0;
   CS ptr;
   int lastc = 0;
   int mincol;
   static LineNr o_lnum = 0;
   int i;
   int did_backspace = TRUE;       // previous char was backspace
   LineNr   old_topline = 0;       // topline before insertion
   int old_topfill = -1;
   int inserted_space = FALSE;     // just inserted a space
   int nomove = FALSE;          // don't move cursor on return
   int commChar_todo = commChar;

   // Remember whether editing was restarted after CTRL-O.
   did_restart_edit = restart_edit;

   // sleep before redrawing, needed for "CTRL-O :" that results in an error message
   check_for_delay(TRUE);

   // set insertStartOrigG to insertStartG
   update_insertStartOrigS = true;

   // Don't allow changes in the book while editing the commline.  The
   // caller of getCommline() may get confused.
   // Don't allow recursive insert mode when busy with completion.
   if (textlock != 0 || ins_compl_active() || isCompletionBusyS || pum_visible()) {
      emsg(_(e_not_allowed_to_change_text_or_change_portal));
      return FALSE;
   }
   ins_compl_clear();       // clear stuff for CTRL-X mode

   // Trigger InsertEnter autocommands.  Do not do this for "r<CR>" or "grx".
   if (commChar != 'r' && commChar != 'v') {
      Pos save_cursor = curPor->cursor;

      if (commChar == 'R')
         ptr = S"r";
      ei (commChar == 'V')
         ptr = S"v";
      else
         ptr = S"i";
      set_EeglVar_string(VV_INSERTMODE, ptr, 1);
      set_EeglVar_string(VV_CHAR, NULL, -1);  // clear v:char
      ins_applyAutocomms(EVENT_INSERTENTER);


      //Make sure the cursor didn't move. Do call check_cursor_col() in case the text was modified.
      //Since Insert mode was not started yet a call to check_cursor_col() may move the cursor, 
      //especially with the "A" command, thus set stateG to avoid that. Also check that the
      //line number is still valid (lines may have been deleted).
      //Do not restore if v:char was set to a non-empty string.
      if (!EQUAL_POS(curPor->cursor, save_cursor)
         && *get_EeglVar_str(VV_CHAR) == ZERO
         && save_cursor.lnum <= curBook->mem.lineCount
      ) {
         int saveState = stateG;

         curPor->cursor = save_cursor;
         stateG = MODE_INSERT;
         check_cursor_col();
         stateG = saveState;
      }
   }

   // When doing a paste with the middle mouse button, insertStartG is set to where the paste started.
   if (where_paste_started.lnum != 0)
      insertStartG = where_paste_started;
   else {
      insertStartG = curPor->cursor;
      if (startln)
         insertStartG.col = 0;
   }
   insertStartG_textlen = (ColNr)linetabsize_str(ml_get_curline());
   insertStartG_blank_vcol = MAXCOL;
   if (!didAindentG)
      ai_col = 0;

   if (commChar != ZERO && restart_edit == 0) {
      ResetRedobuff();
      inpAppendNumberToRedoBuff(count);
      if (commChar == 'V' || commChar == 'v') {
         // "gR" or "gr" command
         AppendCharToRedobuff('g');
         AppendCharToRedobuff((commChar == 'v') ? 'r' : 'R');
      } else {
         if (commChar == K_PS)
            AppendCharToRedobuff('a');
         else
            AppendCharToRedobuff(commChar);
         if (commChar == 'g')          // "gI" command
            AppendCharToRedobuff('I');
         ei (commChar == 'r')       // "r<CR>" command
            count = 1;          // insert only one <CR>
      }
   }

   stateG = MODE_INSERT;

   may_trigger_modechanged();
   stop_insert_mode = FALSE;

   // Need to position cursor again when on a TAB and when on a char with virtual text.
   if (gchar_cursor() == TAB || curBook->hasTextprop)
      curPor->cacheState &= ~(VALID_WROW|VALID_WCOL|VALID_VIRTCOL);

   //Enable langmap or IME, indicated by 'iminsert'.
   //Note that IME may enabled/disabled without us noticing here, thus the
   //'iminsert' value may not reflect what is actually used. It is updated when hitting <Esc>.
   if (curBook->o.b_p_iminsert == B_IMODE_LMAP)
      stateG |= MODE_LANGMAP;

   setmouse();
   clear_showcmd();

   //Handle restarting Insert mode. Don't do this for "CTRL-O ." (repeat an insert): In 
   //that case we get here with something in the stuff buffer.
   if (restart_edit != 0 && stuff_empty()) {
      //After a paste we consider text typed to be part of the insert for
      //the pasted text. You can backspace over the pasted text too.
      if (where_paste_started.lnum)
         arrow_used = FALSE;
      else
         arrow_used = TRUE;
      restart_edit = 0;

      //If the cursor was after the end-of-line before the CTRL-O and it is now at the end-of-line,
      //put it after the end-of-line (this is not correct in very rare cases).
      //Also do this if curswant is greater than the current virtual
      //column.  Eg after "^O$" or "^O80|".
      validate_virtcol();
      update_curswant();
      if (((ins_at_eol && curPor->cursor.lnum == o_lnum)
             || curPor->cursWant > curPor->virtCol)
            && *(ptr = ml_get_curline() + curPor->cursor.col) != ZERO) {
         if (ptr[1] == ZERO)
            ++curPor->cursor.col;
         else {
            i = utfCharLen(ptr);
            if (ptr[i] == ZERO)
               curPor->cursor.col += i;
         } 
      }
      ins_at_eol = FALSE;
   } else
      arrow_used = FALSE;


   // Need to save the line for undo before inserting the first char.
   needUndoS = true;

   where_paste_started.lnum = 0;
   can_cindent = TRUE;
   // The cursor line is not in a closed fold
   if (did_restart_edit == 0)
      foldOpenCursor();

   //If 'showmode' is set, show the current (insert/replace/..) mode.
   //A warning message for changing a readonly file is given here, before
   //actually changing anything.  It's put after the mode, if any.
   i = 0;
   if (p_smd && msg_silent == 0)
      i = showmode();

   if (did_restart_edit == 0)
      change_warning(i == 0 ? 0 : i + 1);

    ui_cursor_shape();      // may show different cursor shape

   //Get the current length of the redo buffer, those characters have to be
   //skipped if we want to get to the inserted characters.
   Text inserted = get_inserted();
   new_insert_skip = (int)inserted.len;
   if (inserted.c != NULL)
      eeglFree(inserted.c);

   old_indent = 0;

   // Main loop in Insert mode: repeat until Insert mode is left.
   for (;;) {
      if (arrow_used)       // don't repeat insert when arrow key used
         count = 0;

      if (update_insertStartOrigS)
         insertStartOrigG = insertStartG;

      if (stop_insert_mode && !ins_compl_active()) {
         // ":stopinsert" used or 'insertmode' reset
         count = 0;
         goto doESCkey;
      }

      // set curPor->cursWant for next K_DOWN or K_UP
      if (!arrow_used)
         curPor->setCursWant = TRUE;

      // If there is no typeahead may check for timestamps (e.g., for when a
      // menu invoked a shell command).
      if (stuff_empty()) {
         did_check_timestamps = FALSE;
         if (need_check_timestamps)
            check_timestamps(FALSE);
      }

      // When emsg() was called msg_scroll will have been set.
      msg_scroll = FALSE;

      // Open fold at the cursor line, according to 'foldopen'.
      if (p_fdo & FDO_INSERT)
         foldOpenCursor();
      // Close folds where the cursor isn't, according to 'foldclose'
      if (!char_avail())
         foldCheckClose();

      if (bt_prompt(curBook)) {
         init_prompt(commChar_todo);
         commChar_todo = ZERO;
      }

      //If we inserted a character at the last position of the last line in the portal, scroll 
      //the portal one line up. This avoids an extra redraw.
      //This is detected when the cursor column is smaller after inserting something.
      //Don't do this when the topline changed already, it has already been adjusted 
      //(by insertchar() calling openLine())). Also don't do this when @smoothscroll is set, as 
      //the portal should then be scrolled by screen lines.
      if (curBook->needsRedraw
            && curPor->o.wrap
            && !curPor->o.smoothScroll
            && !did_backspace
            && curPor->topLine == old_topline
            && curPor->topFill == old_topfill
            && count <= 1
      ){
         mincol = curPor->cursorCol;
         validate_cursor_col();

         if (
            (int)curPor->cursorCol < mincol - curBook->o.shiftWidth
             && curPor->cursorRow == curPor->height - 1 - curPor->o.scrollOff
             && (curPor->cursor.lnum != curPor->topLine || curPor->topFill > 0)
         ) {
            if (curPor->topFill > 0)
               --curPor->topFill;
            ei (getFolds(curPor->topLine, NULL, &old_topline))
               set_topline(curPor, old_topline + 1);
            else
               set_topline(curPor, curPor->topLine + 1);
         }
      }

      // May need to adjust topLine to show the cursor.
      if (count <= 1)
         update_topline();

      did_backspace = FALSE;

      if (count <= 1)
         validate_cursor();      // may set must_redraw

      //Redraw the display when no characters are waiting.
      //Also shows mode, ruler and positions cursor.
      redrawInInsertMode(true);

      if (curPor->o.scrollBind)
         normPostProcessScrollbind(TRUE);

      if (curPor->o.cursorBind)
         do_check_cursorbind();
      if (count <= 1)
         update_curswant();
      old_topline = curPor->topLine;
      old_topfill = curPor->topFill;

      // May request the keyboard protocol state now.
      may_send_t_RK();

      //Get a character for Insert mode.  Ignore K_IGNORE and K_NOP.
      if (c != K_CURSORHOLD)
         lastc = c;      // remember the previous char for CTRL-D

      // After using CTRL-G U the next cursor key will not break undo.
      if (dont_sync_undo == MAYBE)
         dont_sync_undo = TRUE;
      else
         dont_sync_undo = FALSE;
      if (commChar == K_PS)
         // Got here from normal mode when bracketed paste started.
         c = K_PS;
      else {
         do {
            c = safe_vgetc();

            if (stop_insert_mode || (c == K_IGNORE && term_use_loop())) {
               // Insert mode ended, possibly from a callback, or a timer
               // must have opened a terminal portal.
               if (c != K_IGNORE && c != K_NOP)
                  vungetc(c);
               count = 0;

               if (!bt_prompt(curPor->book) && !bt_terminal(curPor->book) && stop_insert_mode)
                  // :stopinsert command via callback or via server command
                  nomove = FALSE;
               else
                  nomove = TRUE;
               ins_compl_prep(ESC);
               goto doESCkey;
            }
         } while (c == K_IGNORE || c == K_NOP);
      } 

      // Don't want K_CURSORHOLD for the second key, e.g., after CTRL-V.
      did_cursorhold = TRUE;

      // If the window was made so small that nothing shows, make it at least
      // one line and one column when typing.
      if (KeyTyped && !KeyStuffed)
         portEnsureSize();

      //Special handling of keys while the popup menu is visible or wanted and the cursor is still
      //in the completed word.  Only when there is a match, skip this when no matches were found.
      if (ins_compl_active() && curPor->cursor.col >= ins_compl_col()
            && ins_compl_has_shown_match() && pum_wanted()
      ) {
         // BS: Delete one character from "compl_leader".
         if ((c == K_BS || c == Ctrl_H)
               && curPor->cursor.col > ins_compl_col()
               && (c = ins_compl_bs()) == ZERO)
            continue;

         // When no match was selected or it was edited.
         if (!ins_compl_used_match()) {
            // CTRL-L: Add one character from the current match to
            // "compl_leader".  Except when at the original match and
            // there is nothing to add, CTRL-L works like CTRL-P then.
            if (c == Ctrl_L && (!ctrl_x_mode_line_or_eval() || ins_compl_long_shown_match())) {
               ins_compl_addfrommatch();
               continue;
            }

            // A non-white character that fits in with the current completion: add to "compl_leader"
            if (ins_compl_accept_char(c)) {
               ins_compl_addleader(c);
               continue;
            }

            // Pressing CTRL-Y selects the current match.  When
            // ins_compl_enter_selects() is set the Enter key does the same.
            if ((c == Ctrl_Y || (ins_compl_enter_selects()
                      && (c == ENTER || c == K_KENTER || c == NL)))
               && stop_arrow() == OK
            ){
               ins_compl_delete();
               ins_compl_insert(FALSE);
            }
            // Delete preinserted text when typing special chars
            ei (IS_WHITE_NL_OR_ZERO(c) && ins_compl_preinsert_effect())
               ins_compl_delete();
          }
      }

      //Prepare for or stop CTRL-X mode.  This doesn't do completion, but
      //it does fix up the text when finishing completion.
      ins_compl_init_get_longest();
      if (ins_compl_prep(c))
         continue;

      // CTRL-\ CTRL-N goes to Normal mode,
      // CTRL-\ CTRL-G goes to mode selected with 'insertmode',
      // CTRL-\ CTRL-O is like CTRL-O but without moving the cursor.
      if (c == Ctrl_BSL) {
         // may need to redraw when no more chars available now
         redrawInInsertMode(false);
         ++no_mapping;
         ++allow_keys;
         c = plain_vgetc();
         --no_mapping;
         --allow_keys;
         if (c != Ctrl_N && c != Ctrl_G && c != Ctrl_O) {
            // it's something else
            vungetc(c);
            c = Ctrl_BSL;
         } else {
            if (c == Ctrl_O) {
               ins_ctrl_o();
               ins_at_eol = FALSE;   // cursor keeps its column
               nomove = TRUE;
            }
            count = 0;
            goto doESCkey;
          }
      }

      if ((c == Ctrl_V || c == Ctrl_Q) && ctrl_x_mode_cmdline())
         goto docomplete;
      if (c == Ctrl_V || c == Ctrl_Q) {
         insertStartVisualBlockMode();
         c = Ctrl_V;   // pretend CTRL-V is last typed character
         continue;
      }

      //If @keymodel contains "startsel", may start selection.  If it
      //does, a CTRL-O and c will be stuffed, we need to get these characters.
      if (ins_start_select(c))
         continue;

      //The big switch to handle a character in insert mode.
      switch (c) {
      case ESC:   // End input mode
         if (echeck_abbr(ESC + ABBR_OFF))
            break;
         // FALLTHROUGH

      case Ctrl_C:   // End input mode
         if (c == Ctrl_C && commPortTypeG != 0) {
            // Close the cmdline window.
            commPortResultG = K_IGNORE;
            gotInterruptG = FALSE; // don't stop executing autocommands et al.
            nomove = TRUE;
            goto doESCkey;
         }
         if (c == Ctrl_C && bt_prompt(curBook) && invoke_prompt_interrupt()) {
            if (!bt_prompt(curBook))
               // book changed to a non-prompt book, get out of Insert mode
               goto doESCkey;
            break;
         }

   do_intr:
   doESCkey:
         //This is the ONLY return from edit()!
         //Always update o_lnum, so that a "CTRL-O ." that adds a line
         //still puts the cursor back after the inserted text.
         if (ins_at_eol && gchar_cursor() == ZERO)
            o_lnum = curPor->cursor.lnum;

         if (ins_esc(&count, commChar, nomove)) {
            // When CTRL-C was typed gotInterruptG will be set, with the result
            // that the autocommands won't be executed. When mapped gotInterruptG
            // is not set, but let's keep the behavior the same.
            if (commChar != 'r' && commChar != 'v' && c != Ctrl_C)
                ins_applyAutocomms(EVENT_INSERTLEAVE);
            did_cursorhold = FALSE;

            if (!char_avail() && curBook->lastChangeTickInsert == CHANGEDTICK(curBook))
                curBook->lastChangeTick = CHANGEDTICK(curBook);
            return (c == Ctrl_O);
         }
         continue;

      case Ctrl_Z:   // suspend when 'insertmode' set
         goto normalchar;   // insert CTRL-Z as normal char

      case Ctrl_O:   // execute one command
         if (ctrl_x_mode_omni())
            goto docomplete;
         if (echeck_abbr(Ctrl_O + ABBR_OFF))
            break;
         ins_ctrl_o();

         count = 0;
         goto doESCkey;

      case K_HELP:   // Help key works like <ESC> <Help>
      case K_F1:
      case K_XF1:
         stuffcharReadbuff(K_HELP);
         goto doESCkey;

      case K_ZERO:   // Insert the previously inserted text.
      case ZERO:
      case Ctrl_A:
         // For ^@ the trailing ESC will end the insert, unless there is an error.
         if (stuff_inserted(ZERO, 1L, (c == Ctrl_A)) == FAIL && c != Ctrl_A)
            goto doESCkey;      // quit insert mode
         inserted_space = FALSE;
         break;

      case Ctrl_R:   // insert the contents of a register
         if (ctrl_x_mode_register() && !ins_compl_active())
            goto docomplete;
         insertRegisterContents();
         auto_format(FALSE, TRUE);
         inserted_space = FALSE;
         break;

      case Ctrl_G:   // commands starting with CTRL-G
         ins_ctrl_g();
         break;

      case Ctrl_HAT:   // switch input mode and/or langmap
         ins_ctrl_hat();
         break;


      case Ctrl_D:   // Make indent one shiftwidth smaller.
         if (ctrl_x_mode_path_defines())
            goto docomplete;
         // FALLTHROUGH

      case Ctrl_T:   // Make indent one shiftwidth greater.
         if (c == Ctrl_T && ctrl_x_mode_thesaurus()) {
            if (has_compl_option(FALSE))
               goto docomplete;
            break;
         }

         ins_shift(c, lastc);
         auto_format(FALSE, TRUE);
         inserted_space = FALSE;
         break;

      case K_DEL:   // delete character under the cursor
      case K_KDEL:
         ins_del();
         auto_format(FALSE, TRUE);
         break;

      case K_BS:   // delete character before the cursor
      case K_S_BS:
      case Ctrl_H:
         did_backspace = ins_bs(c, BACKSPACE_CHAR, &inserted_space);
         auto_format(FALSE, TRUE);
         if (did_backspace && p_ac && !char_avail() && curPor->cursor.col > 0) {
            c = char_before_cursor();
            if (ins_compl_setup_autocompl(c)) {
                drawUpdateScreen(UPD_VALID); // Show char deletion immediately
                out_flush();
                goto docomplete; // Trigger autocompletion
            }
         }
         break;

      case Ctrl_W:   // delete word before the cursor
         if (bt_prompt(curBook) && (modMaskG & MOD_MASK_SHIFT) == 0) {
            // In a prompt window CTRL-W is used for window commands.
            // Use Shift-CTRL-W to delete a word.
            stuffcharReadbuff(Ctrl_W);
            restart_edit = 'A';
            nomove = TRUE;
            count = 0;
            goto doESCkey;
         }
         did_backspace = ins_bs(c, BACKSPACE_WORD, &inserted_space);
         auto_format(FALSE, TRUE);
         break;

      case Ctrl_U:   // delete all inserted text in current line
         // CTRL-X CTRL-U completes with 'completefunc'.
         if (ctrl_x_mode_function())
            goto docomplete;
         did_backspace = ins_bs(c, BACKSPACE_LINE, &inserted_space);
         auto_format(FALSE, TRUE);
         inserted_space = FALSE;
         break;

      case K_LEFTMOUSE:   // mouse keys
      case K_LEFTMOUSE_NM:
      case K_LEFTDRAG:
      case K_LEFTRELEASE:
      case K_LEFTRELEASE_NM:
      case K_MOUSEMOVE:
      case K_MIDDLEMOUSE:
      case K_MIDDLEDRAG:
      case K_MIDDLERELEASE:
      case K_RIGHTMOUSE:
      case K_RIGHTDRAG:
      case K_RIGHTRELEASE:
      case K_X1MOUSE:
      case K_X1DRAG:
      case K_X1RELEASE:
      case K_X2MOUSE:
      case K_X2DRAG:
      case K_X2RELEASE:
         ins_mouse(c);
         break;

      case K_MOUSEDOWN: // Default action for scroll wheel up: scroll up
         ins_mousescroll(MSCR_DOWN);
         break;

      case K_MOUSEUP:   // Default action for scroll wheel down: scroll down
         ins_mousescroll(MSCR_UP);
         break;

      case K_MOUSELEFT: // Scroll wheel left
         ins_mousescroll(MSCR_LEFT);
         break;

      case K_MOUSERIGHT: // Scroll wheel right
         ins_mousescroll(MSCR_RIGHT);
         break;

      case K_PS:
         bracketed_paste(PASTE_INSERT, FALSE, NULL);
         if (commChar == K_PS)
            // invoked from normal mode, bail out
            goto doESCkey;
         break;
      case K_PE:
         // Got K_PE without K_PS, ignore.
         break;

      case K_IGNORE:   // Something mapped to nothing
         break;

      case K_COMMAND:          // <Cmd>command<CR>
      case K_SCRIPT_COMMAND: {      // <ScriptCmd>command<CR>
         do_cmdkey_command(c, 0);

         if (term_use_loop())
            // Started a terminal that gets the input, exit Insert mode.
            goto doESCkey;
         if (curBook->undo.synced)
            // The command caused undo to be synced.  Need to save the
            // line for undo before inserting the next char.
            needUndoS = true;
         }
         break;

      case K_CURSORHOLD:   // Didn't type something for a while.
         ins_applyAutocomms(EVENT_CURSORHOLDI);
         did_cursorhold = TRUE;
         // If CTRL-G U was used apply it to the next typed key.
         if (dont_sync_undo == TRUE)
            dont_sync_undo = MAYBE;
         break;

      case K_HOME:   // <Home>
      case K_KHOME:
      case K_S_HOME:
      case K_C_HOME:
         ins_home(c);
         break;

      case K_END:   // <End>
      case K_KEND:
      case K_S_END:
      case K_C_END:
         ins_end(c);
         break;

      case K_LEFT:   // <Left>
         if (modMaskG & (MOD_MASK_SHIFT|MOD_MASK_CTRL))
            ins_s_left();
         else
            ins_left();
         break;

      case K_S_LEFT:   // <S-Left>
      case K_C_LEFT:
         ins_s_left();
         break;

      case K_RIGHT:   // <Right>
         if (modMaskG & (MOD_MASK_SHIFT|MOD_MASK_CTRL))
            ins_s_right();
         else
            ins_right();
         break;

      case K_S_RIGHT:   // <S-Right>
      case K_C_RIGHT:
          ins_s_right();
          break;

      case K_UP:   // <Up>
         if (pum_visible())
            goto docomplete;
         if (modMaskG & MOD_MASK_SHIFT)
            ins_pageup();
         else
            ins_up(FALSE);
         break;

      case K_S_UP:   // <S-Up>
      case K_PAGEUP:
      case K_KPAGEUP:
         if (pum_visible())
            goto docomplete;
         ins_pageup();
         break;

      case K_DOWN:   // <Down>
         if (pum_visible())
            goto docomplete;
         if (modMaskG & MOD_MASK_SHIFT)
            ins_pagedown();
         else
            ins_down(FALSE);
         break;

      case K_S_DOWN:   // <S-Down>
      case K_PAGEDOWN:
      case K_KPAGEDOWN:
         if (pum_visible())
            goto docomplete;
         ins_pagedown();
         break;

      case K_DROP:   // drag-n-drop event
         ins_drop();
         break;

      case K_S_TAB:   // When not mapped, use like a normal TAB
         c = TAB;
         // FALLTHROUGH

      case TAB:   // TAB or Complete patterns along path
         if (modMaskG & (MOD_MASK_SHIFT|MOD_MASK_CTRL)) {
            if (ins_tab())
               goto normalchar;   // insert TAB as a normal char
         } ei (ctrl_x_mode_path_patterns()) 
            goto docomplete;
         else {
            // go to normal mode
            goto doESCkey;
         }
         
         
         inserted_space = FALSE;
         auto_format(FALSE, TRUE);
         break;

      case K_KENTER:   // <Enter>
         c = ENTER;
         // FALLTHROUGH
      case ENTER:
      case NL:
         // In a quickfix window a <CR> jumps to the error under the cursor.
         if (isLocationListBook(curBook) && c == ENTER) {
            if (curPor->locationStackRef == NULL)    // quickfix window
               executeCommLine(S".mc");
            else                // location list portal
               executeCommLine(S".ll");
            break;
         }
         if (commPortTypeG != 0) {
            // Execute the command in the commline portal
            commPortResultG = ENTER;
            goto doESCkey;
         }
         if (bt_prompt(curBook)) {
            invoke_prompt_callback();
            if (!bt_prompt(curBook))
               // book changed to a non-prompt book, get out of Insert mode
               goto doESCkey;
            break;
         }
         if (ins_eol(c) == FAIL)
            goto doESCkey;       // out of memory
         auto_format(FALSE, FALSE);
         inserted_space = FALSE;
         break;

      case Ctrl_K:       // digraph or keyword completion
         if (ctrl_x_mode_dictionary()) {
            if (has_compl_option(TRUE))
               goto docomplete;
            break;
         }
         goto normalchar;

      case Ctrl_X:   // Enter CTRL-X mode
          ins_ctrl_x();
          break;

      case Ctrl_RSB:   // Tag name completion after ^X
         if (!ctrl_x_mode_tags())
            goto normalchar;
         goto docomplete;

      case Ctrl_F:   // File name completion after ^X
         if (!ctrl_x_mode_files())
            goto normalchar;
         goto docomplete;
      case Ctrl_L:   // Whole line completion after ^X
         if (!ctrl_x_mode_whole_line()) {
            goto normalchar;
         }
         // FALLTHROUGH

      case Ctrl_P:   // Do previous/next pattern completion
      case Ctrl_N:
         //if @complete is empty then plain ^P is no longer special, but it is under other ^X modes
         if (!curBook->o.complete
                && (ctrl_x_mode_normal() || ctrl_x_mode_whole_line())
                && !compl_status_local()
         )
            goto normalchar;

   docomplete:
         isCompletionBusyS = true;
         disable_fold_update++;  // don't redraw folds here
         if (ins_complete(c, true) == FAIL)
            compl_status_clear();
         disable_fold_update--;
         isCompletionBusyS = false;
         can_si = may_do_si(); // allow smartindenting
         break;

      case Ctrl_Y:   // copy from previous line or scroll down
      case Ctrl_E:   // copy from next line      or scroll up
         c = ins_ctrl_ey(c);
         break;

      default:
         if (c == extraInterruptCharG)      // special interrupt char
            goto do_intr;

   normalchar:
         //Insert a normal character. If the new value is already inserted or an empty string
         // then don't insert any character.
         if (c == ZERO)
             break;
         // Try to perform smart-indenting.
         ins_try_si(c);

         if (c == ' ') {
            inserted_space = TRUE;
         if (inindent(0))
            can_cindent = FALSE;
         if (insertStartG_blank_vcol == MAXCOL && curPor->cursor.lnum == insertStartG.lnum)
            insertStartG_blank_vcol = get_nolist_virtcol();
         }

         //Insert a normal character and check for abbreviations on a
         //special character.  Let CTRL-] expand abbreviations without inserting it.
         if (eeIsWordc(c) 
               || (!echeck_abbr((c >= 0x100) ? (c + ABBR_OFF) : c)
                  // Add ABBR_OFF for characters above 0x100, this is what check_abbr() expects.
                  && c != Ctrl_RSB)
         ) {
            insertRegular(c, false, false);
         }

         auto_format(FALSE, TRUE);

         // When inserting a character the cursor line must never be in a closed fold.
         foldOpenCursor();
         // Trigger autocompletion
         if (p_ac && !char_avail() && ins_compl_setup_autocompl(c)) {
            drawUpdateScreen(UPD_VALID); // Show character immediately
            out_flush();
            goto docomplete;
         }

         break;
      }   // end of switch (c)

      // If typed something may trigger CursorHoldI again.
      if (c != K_CURSORHOLD 
         // but not in CTRL-X mode, a script can't restore the state
         && ctrl_x_mode_normal()
      ) {
         did_cursorhold = FALSE;
      } 

      // Check if we need to cancel completion mode because the portal or tab was changed
      if (ins_compl_active() && !ins_compl_win_active(curPor))
         ins_compl_cancel();

      // If the cursor was moved we didn't just insert a space
      if (arrow_used)
         inserted_space = FALSE;

   }   // for (;;)
   // NOTREACHED
}

//Redraw for Insert mode. This is postponed until getting the next character to make '$' in the 
//'cpo' option work correctly. Only redraw when there are no characters available. This speeds up
//inserting sequences of characters (e.g., for CTRL-R).
private void
redrawInInsertMode(Boole ready) {      // not busy with something
   if (char_avail())
      return;

   // Trigger CursorMoved if the cursor moved.  Not when the popup menu is
   // visible, the command might delete it.
   if (ready && popup_visible && !EQUAL_POS(last_cursormoved, curPor->cursor) && !pum_visible()) {
      //Need to update the screen first, to make sure syntax highlighting is correct after making 
      //a change (e.g., inserting a "(".  The autocommand may also require a redraw, so it's done
      //again below, unfortunately.
      if (syntax_present(curPor) && must_redraw)
         drawUpdateScreen(0);
      if (popup_visible)
         popup_check_cursor_pos();
      last_cursormoved = curPor->cursor;
   }

   if (ready)
      may_trigger_win_scrolled_resized();

   // Trigger SafeState if nothing is pending.
   may_trigger_safestate(ready && !ins_compl_active() && !pum_visible());

   if (must_redraw)
      drawUpdateScreen(0);
   ei (mustClearCommlineG || redrawCommlineG)
      showmode();      // clear cmdline and show mode
   showruler(FALSE);
   setcursor();
   emsg_on_display = FALSE;   // may remove error message now
}

//Handle a CTRL-V or CTRL-Q typed in Insert mode.
private void
insertStartVisualBlockMode(void) {
   Boole did_putchar = false;

   // may need to redraw when no more chars available now
   redrawInInsertMode(FALSE);

   if (redrawing() && !char_avail()) {
      edit_putchar('^', TRUE);
      did_putchar = true;
   }
   AppendToRedobuff((CS)CTRL_V_STR);   // CTRL-V

   add_to_showcmd_c(Ctrl_V);

   // Do not change any modifyOtherKeys ESC sequence to a normal key for CTRL-SHIFT-V.
   Unt c = get_literal(modMaskG & MOD_MASK_SHIFT);
   if (did_putchar)
      // when the line fits in 'columns' the '^' is at the start of the next
      // line and will not removed by the redraw
      edit_unputchar();
   clear_showcmd();

   insertRegular(c, false, true);
}

//After getting an ESC or CSI for a literal key: If the typeahead buffer
//contains a modifyOtherKeys sequence then decode it and return the result.
//Otherwise return "c". Note that this doesn't wait for characters, they must be in the typeahead
//buffer already.
private int
decodeModifyOtherKeys(int c) {
   CS p = typeBufG.c + typeBufG.currPos;
   int idx;
   int form = 0;
   int argidx = 0;
   int arg[2] = {0, 0};

   // Recognize:
   // form 0: {lead}{key};{modifier}u
   // form 1: {lead}27;{modifier};{key}~
   if (typeBufG.validLen >= 4 && (c == CSI || (c == ESC && *p == '['))) {
      idx = (*p == '[');
      while (idx < typeBufG.validLen && argidx < 2) {
         if (p[idx] == ';')
            ++argidx;
         ei (EE_ISDIGIT(p[idx]))
            arg[argidx] = arg[argidx] * 10 + (p[idx] - '0');
         else
            break;
         ++idx;
      }
      int kitty_no_mods = argidx == 0;
      if (idx < typeBufG.validLen
         && p[idx] == (form == 1 ? '~' : 'u')
         && (argidx == 1 || kitty_no_mods)
      ){
         // Match, consume the code.
         typeBufG.currPos += idx + 1;
         typeBufG.validLen -= idx + 1;
         if (typeBufG.validLen == 0)
            typebuf_was_filled = FALSE;

         modMaskG = kitty_no_mods ? 0 : decode_modifiers(arg[!form]);
         c = mergeModifierKey(arg[form], &modMaskG);
      }
   }

   return c;
}

//}}}
//{{{Putting characters on the screen

// Put a character directly onto the screen.  It's not stored in a buffer.
// Used while handling CTRL-K, CTRL-V, etc. in Insert mode.
private int  pc_status;
#define PC_STATUS_UNSET  0   // pc_bytes was not set
#define PC_STATUS_RIGHT  1   // right half of double-wide char
#define PC_STATUS_LEFT   2   // left half of double-wide char
#define PC_STATUS_SET    3   // pc_bytes was filled
private Byte pc_bytes[MB_MAXBYTES + 1]; // saved bytes
private char  charDecoFlagsS;
private int  pc_row;
private int  pc_col;

void
edit_putchar(int c, Boole needDoHilite) {
   if (!screenLinesG)
      return;

   update_topline();   // just in case topLine isn't valid
   validate_cursor();
   char decoFl = needDoHilite ? getDecoFlags(HLF_8) : 0;
   pc_row = curPor->portalRow + curPor->cursorRow;
   pc_col = curPor->portalCol;
   pc_status = PC_STATUS_UNSET;
   pc_col += curPor->cursorCol;
   if (mb_lefthalve(pc_row, pc_col))
      pc_status = PC_STATUS_LEFT;

   // save the character to be able to put it back
   if (pc_status == PC_STATUS_UNSET) {
      screen_getbytes(pc_row, pc_col, pc_bytes, OUT &charDecoFlagsS);
      pc_status = PC_STATUS_SET;
   }
   screen_putchar(c, pc_row, pc_col, decoFl);
}

// Set the insert start position for when using a prompt book.
void
set_insstart(LineNr lnum, int col) {
   insertStartG.lnum = lnum;
   insertStartG.col = col;
   insertStartOrigG = insertStartG;
   insertStartG_textlen = insertStartG.col;
   insertStartG_blank_vcol = MAXCOL;
   arrow_used = FALSE;
}

// Undo the previous edit_putchar().
void
edit_unputchar(void) {
   if (pc_status != PC_STATUS_UNSET && pc_row >= msg_scrolled) {
      if (pc_status == PC_STATUS_RIGHT)
         ++curPor->cursorCol;
      if (pc_status == PC_STATUS_RIGHT || pc_status == PC_STATUS_LEFT)
         drawPortLineLater(curPor, curPor->cursor.lnum);
      else
         drawText(pc_bytes, pc_row - msg_scrolled, pc_col, charDecoFlagsS);
   }
}

// Truncate the space at the end of a line.  This is to be used only in insert mode
void
truncate_spaces(CS line, Unt len) {
   // find start of trailing white space
   for (int i = (int)len - 1; i >= 0 && SPACE_OR_TAB(line[i]); i--) {
      line[i + 1] = ZERO;
   } 
}

// Backspace the cursor until the given column. May also be used when not in insert mode at all.
// Will attempt not to go before "col" even when there is a composing character.
void
backspace_until_column(int col) {
   while ((int)curPor->cursor.col > col) {
      curPor->cursor.col--;
      if (!del_char_after_col(col))
         break;
   }
}

// Like del_char(), but make sure not to go before column "limit_col".
// Only matters when there are composing characters. Return TRUE when something was deleted.
private int
del_char_after_col(int limit_col UNUSED) {
   if (limit_col >= 0) {
      ColNr ecol = curPor->cursor.col + 1;

      // Make sure the cursor is at the start of a character, but
      // skip forward again when going too far back because of a composing character.
      mb_adjust_cursor();
      while (curPor->cursor.col < (ColNr)limit_col) {
         int l = utf_ptr2len(ml_get_cursor());
         if (l == 0)  // end of line
            break;
         curPor->cursor.col += l;
      }
      if (*ml_get_cursor() == ZERO || curPor->cursor.col == ecol)
         return FALSE;
      del_bytes((long)((int)ecol - curPor->cursor.col), FALSE, TRUE);
   } else
      (void)del_char(false);
   return TRUE;
}

//Next character is interpreted literally.
//A one, two or three digit decimal number is interpreted as its byte value.
//If one or two digits are entered, the next character is given to vungetc().
//For Unicode a character > 255 may be returned.
//If "noReduceKeys" is TRUE do not change any modifyOtherKeys ESC sequence into a normal key, 
//return ESC.
int
get_literal(int noReduceKeys) {
   Unt nc;
   int hex = FALSE;
   int unicode = 0;

   if (gotInterruptG)
      return Ctrl_C;

   ++no_mapping;      // don't map the next key hits
   int cc = 0;
   int i = 0;
   for (;;) {
      nc = plain_vgetc();
      if ((nc == ESC || nc == CSI) && !noReduceKeys)
         nc = decodeModifyOtherKeys(nc);

      if ((modMaskG & ~MOD_MASK_SHIFT) != 0)
         //A character with non-Shift modifiers should not be a valid character for i_CTRL-V_digit.
         break;

      if ((stateG & MODE_COMMLINE) == 0 && MB_BYTE2LEN_CHECK(nc) == 1)
         add_to_showcmd(nc);
      if (nc == 'x' || nc == 'X')
         hex = TRUE;
      ei (nc == 'u' || nc == 'U')
         unicode = nc;
      else {
         if (hex || unicode != 0) {
            if (!eeIsXDigit(nc))
               break;
            cc = cc * 16 + hex2nr(nc);
         } else {
            if (!EE_ISDIGIT(nc))
                break;
            cc = cc * 10 + nc - '0';
         }

         ++i;
      }

      if (cc > 255 && unicode == 0)
         cc = 255;      // limit range to 0-255
      nc = 0;

      if (hex) {     // hex: up to two chars
         if (i >= 2)
            break;
      } ei (unicode) {  // Unicode: up to four or eight chars
         if ((unicode == 'u' && i >= 4) || (unicode == 'U' && i >= 8))
            break;
      } ei (i >= 3)   // decimal: up to three chars
         break;
   }
   if (i == 0) {      // no number entered
      if (nc == K_ZERO) {  // ZERO is stored as NL
         cc = '\n';
         nc = 0;
      } else {
         cc = nc;
         nc = 0;
      }
   }

   if (cc == 0)   // ZERO is stored as NL
      cc = '\n';

   --no_mapping;
   if (nc) {
      vungetc(nc);
      // A character typed with i_CTRL-V_digit cannot have modifiers.
      modMaskG = 0;
   }
   gotInterruptG = FALSE;       // CTRL-C typed after CTRL-V is not an interrupt
   return cc;
}

// Insert character, taking care of special keys and modMaskG
private void
insertRegular(Unt c, Boole allow_modmask, Boole ctrlv) {       // c was typed after CTRL-V
   //Special function key, translate into "<Key>". Up to the last '>' is inserted with ins_str(), 
   //so as not to replace characters in replace mode. Only use modMaskG for special keys, to 
   //avoid things like <S-Space>, unless 'allow_modmask' is TRUE.
   if (IS_SPECIAL(c) || (modMaskG && allow_modmask)) {
      CS p = get_special_key_name(c, modMaskG);
      int len = (int)STRLEN(p);
      c = p[len - 1];
      if (len > 2) {
         if (stop_arrow() == FAIL)
            return;
         p[len - 1] = ZERO;
         ins_str(p, len - 1);
         AppendToRedobuffLit(p, -1);
         ctrlv = FALSE;
      }
   }
   if (stop_arrow() == OK)
      insertchar0(c, ctrlv ? INSCHAR_CTRLV : 0, -1);
}

//Special characters in this context are those that need processing other than the simple 
//insertion that can be performed here. This includes ESC which terminates the insert, and CR/NL
//which need special processing to open up a new line. This routine tries to optimize insertions 
//performed by the "redo", "undo" or "put" commands, so it needs to know when it should
//stop and defer processing to the "normal" mechanism. '0' and '^' are special, because they can
//be followed by CTRL-D.
#define ISSPECIAL(c)   ((c) < ' ' || (c) >= DEL || (c) == '0' || (c) == '^')

//"flags": INSCHAR_FORMAT - force formatting
//      INSCHAR_CTRLV  - char typed just after CTRL-V
//      INSCHAR_NO_FEX - don't use 'formatexpr'
//
//  NOTE: passes the flags value straight through to internal_format() which,
//     beside INSCHAR_FORMAT (above), is also looking for these:
//      INSCHAR_DO_COM   - format comments
//      INSCHAR_COM_LIST - format comments with num list or 2nd line indent
void
insertchar0(
   Unt c,         // character to insert or ZERO
   Unt flags,         // INSCHAR_FORMAT, etc.
   int second_indent      // indent for second line if >= 0
){
   CS p;
   int force_format = flags & INSCHAR_FORMAT;

   int textwidth = comp_textwidth(force_format);
   int fo_ins_blank = has_format_option(FO_INS_BLANK);

   //Try to break the line in two or more pieces when:
   //- Always do this if we have been called to do formatting only.
   //- Always do this when 'formatoptions' has the 'a' flag and the line
   //  ends in white space.
   //- Otherwise:
   //   - Don't do this if inserting a blank
   //   - Don't do this if an existing character is being replaced, unless
   //     we're in MODE_VREPLACE state.
   //   - Do this if the cursor is not on the line where insert started
   //   or - 'formatoptions' doesn't have 'l' or the line was not too long
   //         before the insert.
   //      - 'formatoptions' doesn't have 'b' or a blank was inserted at or
   //        before 'textwidth'
   if (textwidth > 0
       && (force_format
         || (!SPACE_OR_TAB(c)
             && (curPor->cursor.lnum != insertStartG.lnum
            || ((!has_format_option(FO_INS_LONG)
               || insertStartG_textlen <= (ColNr)textwidth)
                && (!fo_ins_blank
               || insertStartG_blank_vcol <= (ColNr)textwidth
                )))))
   ) {
      // Format with @formatexpr when it's set.  Use internal formatting
      // when @formatexpr isn't set or it returns non-zero.
      Boole do_internal = true;
      ColNr virtcol = get_nolist_virtcol()
                 + char2cells(c != ZERO ? c : gchar_cursor());

      if (curBook->o.formatExpr && (flags & INSCHAR_NO_FEX) == 0
         && (force_format || virtcol > (ColNr)textwidth)
      ) {
         do_internal = (fex_format(curPor->cursor.lnum, 1L, c) != 0);
         // It may be required to save for undo again, e.g. when setline() was called.
         needUndoS = true;
      }
      if (do_internal)
         internal_format(textwidth, second_indent, flags, c == ZERO, c);
   }

   if (c == ZERO)       // only formatting was wanted
      return;

   // Check whether this character should end a comment.
   if (didAindentG && c == end_comment_pending) {
      CS line;
      Byte lead_end[COM_MAX_LEN];       // end-comment string
      int middle_len, end_len;

      //Need to remove existing (middle) comment leader and insert end
      //comment leader.  First, check what comment leader we can find.
      int i = get_leader_len(line = ml_get_curline(), &p, FALSE, TRUE);
      if (i > 0 && firstOccurrence(p, COM_MIDDLE) != NULL) {  // Just checking
         // Skip middle-comment string
         while (*p && p[-1] != ':')   // find end of middle flags
            ++p;
         middle_len = doCutPathFromListOfPaths(OUT &p, OUT lead_end, COM_MAX_LEN, S",");
         // Don't count trailing white space for middle_len
         while (middle_len > 0 && SPACE_OR_TAB(lead_end[middle_len - 1]))
            --middle_len;

         // Find the end-comment string
         while (*p && p[-1] != ':')   // find end of end flags
            ++p;
         end_len = doCutPathFromListOfPaths(OUT &p, OUT lead_end, COM_MAX_LEN, S",");

         // Skip white space before the cursor
         i = curPor->cursor.col;
         while (--i >= 0 && SPACE_OR_TAB(line[i]))
            {}
         i++;

         // Skip to before the middle leader
         i -= middle_len;

         // Check some expected things before we go on
         if (i >= 0 && end_len > 0 && lead_end[end_len - 1] == end_comment_pending) {
            // Backspace over all the stuff we want to replace
            backspace_until_column(i);

            // Insert the end-comment string, except for the last
            // character, which will get inserted as normal later.
            ins_bytes_len(lead_end, end_len - 1);
         }
      }
   }
   end_comment_pending = ZERO;

   didAindentG = false;
   didSindentG = false;
   can_si = FALSE;
   can_si_back = FALSE;

   //If there's any pending input, grab up to INPUT_BUFLEN at once. This speeds up normal text 
   //input considerably. Don't do this when 'cindent' or 'indentexpr' is set, because we might
   //need to re-indent at a ':', or any other character (but not what 'paste' is set)..
   //Don't do this when there an InsertCharPre autocommand is defined, because we need to fire 
   //the event for every character. Do the check for InsertCharPre before the call to vpeekc() 
   //because the InsertCharPre autocommand could change the input buffer.

   if (!ISSPECIAL(c)
       && (mb_char2len(c) == 1)
       // Skip typeahead if test_override("char_avail", 1) was called.
       && !disable_char_avail_for_testing
       && vpeekc() != ZERO
       && !jugIsIndentationExpressionBased()
   ) {
#define INPUT_BUFLEN 100
      Byte buf[INPUT_BUFLEN + 1];
      ColNr virtcol = 0;

      buf[0] = c;
      int i = 1;
      if (textwidth > 0)
         virtcol = get_nolist_virtcol();
      //Stop the string when:
      //- no more chars available
      //- finding a special character (command key)
      //- buffer is full
      //- running into the 'textwidth' boundary
      //- need to check for abbreviation: A non-word char after a word-char
      while (      (c = vpeekc()) != ZERO
         && !ISSPECIAL(c)
         && (MB_BYTE2LEN_CHECK(c) == 1)
         && i < INPUT_BUFLEN
         && (textwidth == 0
             || (virtcol += byte2cells(buf[i - 1])) < (ColNr)textwidth)
         && !(!no_abbr && !eeIsWordc(c) && eeIsWordc(buf[i - 1])))
      {
         buf[i] = vgetc();
         i++;
      }

      buf[i] = ZERO;
      ins_str(buf, i);
      if (flags & INSCHAR_CTRLV) {
         redo_literal(*buf);
         i = 1;
      } else
         i = 0;
      if (buf[i] != ZERO)
         AppendToRedobuffLit(buf + i, -1);
   } else {
      int cc;

      if ((cc = mb_char2len(c)) > 1) {
         Byte buf[MB_MAXBYTES + 1];
         mb_char2bytes(c, buf);
         buf[cc] = ZERO;
         opInsertCharBytes(buf, cc, false);
         AppendCharToRedobuff(c);
      } else {
         insertChar(c);
         if (flags & INSCHAR_CTRLV)
            redo_literal(c);
         else
            AppendCharToRedobuff(c);
      }
    }
}

//Put a character in the redo buffer, for when just after a CTRL-V.
private void
redo_literal(int c) {
   Byte buf[10];

   // Only digits need special treatment.  Translate them into a string of three digits.
   if (EE_ISDIGIT(c)) {
      eeSnprintf(buf, sizeof(buf), "%03d", c);
      AppendToRedobuff(buf);
   } else
      AppendCharToRedobuff(c);
}

//start_arrow() is called when an arrow key is used in insert mode.
//For undo/redo it resembles hitting the <ESC> key.
void
start_arrow(Pos* end_insert_pos) {    // can be NULL
   start_arrow_common(end_insert_pos, TRUE);
}

//Like start_arrow() but with end_change argument.
//Will prepare for redo of CTRL-G U if "end_change" is FALSE.
private void
start_arrow_with_change(NULLABLE Pos* end_insert_pos, int end_change) { //end undoable change
   start_arrow_common(end_insert_pos, end_change);
   if (!end_change) {
      AppendCharToRedobuff(Ctrl_G);
      AppendCharToRedobuff('U');
   }
}

private void
start_arrow_common(NULLABLE Pos* end_insert_pos, int end_change) {     // end undoable change
   if (!arrow_used && end_change) {  // something has been inserted
      AppendToRedobuff(ESC_STR);
      stop_insert(end_insert_pos, FALSE, FALSE);
      arrow_used = TRUE;   // this means we stopped the current insert
   }
   check_spell_redraw();
}

//If we skipped highlighting word at cursor, do it now.
//It may be skipped again, thus reset spell_redraw_lnum first.
private void
check_spell_redraw(void) {
   if (spell_redraw_lnum != 0) {
      LineNr   lnum = spell_redraw_lnum;
      spell_redraw_lnum = 0;
      drawPortLineLater(curPor, lnum);
   }
}

//stop_arrow() is called before a change is made in insert mode.
//If an arrow key has been used, start a new insertion. Return FAIL if undo is impossible, 
//shouldn't insert then.
int
stop_arrow(void) {
   if (arrow_used) {
      insertStartG = curPor->cursor;   // new insertion starts here
      if (insertStartG.col > insertStartOrigG.col && !needUndoS)
         // Don't update the original insert position when moved to the
         // right, except when nothing was inserted yet.
         update_insertStartOrigS = false;
      insertStartG_textlen = (ColNr)linetabsize_str(ml_get_curline());

      if (u_save_cursor() == OK) {
          arrow_used = false;
          needUndoS = false;
      }

      ai_col = 0;
      ResetRedobuff();
      AppendToRedobuff((CS)"1i");   // pretend we start an insertion
      new_insert_skip = 2;
   } ei (needUndoS) {
      if (u_save_cursor() == OK)
         needUndoS = false;
   }

   // Always open fold at the cursor line when inserting something.
   foldOpenCursor();

   return (arrow_used || needUndoS ? FAIL : OK);
}

//Do a few things to stop inserting. "end_insert_pos" is where insert ended. It is NULL when 
//we already jumped to another portal/book.
private void
stop_insert(
   Pos* end_insert_pos,
   int esc,         // called by ins_esc()
   int nomove       // <c-\><c-o>, don't move cursor
){
   int cc;
   stop_redo_ins();

   //Save the inserted text for later redo with ^@ and CTRL-A.
   //Don't do it when "restart_edit" was set and nothing was inserted,
   //otherwise CTRL-O w and then <Left> will clear "last_insert".
   Text inserted = get_inserted();
   int added = inserted.c == NULL ? 0 : (int)inserted.len - new_insert_skip;
   if (did_restart_edit == 0 || added > 0) {
      eeglFree(last_insert.c);
      last_insert = inserted;             // structure copy
      last_insert_skip = added < 0 ? 0 : new_insert_skip;
   } else
      eeglFree(inserted.c);

   if (!arrow_used && end_insert_pos != NULL) {
      //Auto-format now.  It may seem strange to do this when stopping an
      //insertion (or moving the cursor), but it's required when appending
      //a line and having it end in a space.  But only do it when something
      //was actually inserted, otherwise undo won't work.
      if (!needUndoS && has_format_option(FO_AUTO)) {
         Pos tpos = curPor->cursor;

         //When the cursor is at the end of the line after a space the
         //formatting will move it to the following word.  Avoid that by
         //moving the cursor onto the space.
         cc = 'x';
         if (curPor->cursor.col > 0 && gchar_cursor() == ZERO) {
            dec_cursor();
            cc = gchar_cursor();
            if (!SPACE_OR_TAB(cc))
                curPor->cursor = tpos;
         }

         auto_format(TRUE, FALSE);

         if (SPACE_OR_TAB(cc)) {
            if (gchar_cursor() != ZERO)
                inc_cursor();
            // If the cursor is still at the same character, also keep the "coladd".
            if (gchar_cursor() == ZERO
               && curPor->cursor.lnum == tpos.lnum
               && curPor->cursor.col == tpos.col)
                curPor->cursor.coladd = tpos.coladd;
         }
      }

      // If a space was inserted for auto-formatting, remove it now.
      check_auto_format(TRUE);

      // If we just did an auto-indent, remove the white space from the end
      // of the line, and put the cursor back. Do this when ESC was used or moving the cursor 
      // up/down. Check for the old position still being valid, just in case the text
      // got changed unexpectedly.
      if (!nomove && didAindentG && (esc || curPor->cursor.lnum != end_insert_pos->lnum)
         && end_insert_pos->lnum <= curBook->mem.lineCount
      ) {
         Pos   tpos = curPor->cursor;
         ColNr   prev_col = end_insert_pos->col;

         curPor->cursor = *end_insert_pos;
         check_cursor_col();  // make sure it is not past the line
         for (;;) {
         if (gchar_cursor() == ZERO && curPor->cursor.col > 0)
            --curPor->cursor.col;
         cc = gchar_cursor();
         if (!SPACE_OR_TAB(cc))
             break;
         if (del_char(true) == FAIL)
             break;  // should not happen
         }
         if (curPor->cursor.lnum != tpos.lnum)
            curPor->cursor = tpos;
         ei (curPor->cursor.col < prev_col) {
            // reset tpos, could have been invalidated in the loop above
            tpos = curPor->cursor;
            tpos.col++;
            if (cc != ZERO && gchar_pos(&tpos) == ZERO)
               ++curPor->cursor.col;   // put cursor back on the ZERO
         }

         // <C-S-Right> may have started Visual mode, adjust the position for
         // deleted characters.
         if (VIsual_active)
            check_visual_pos();
      }
   }
   didAindentG = false;
   didSindentG = FALSE;
   can_si = FALSE;
   can_si_back = FALSE;

   //Set '[ and '] to the inserted text.  When end_insert_pos is NULL we are
   //now in a different book.
   if (end_insert_pos) {
      curBook->opStart = insertStartG;
      curBook->opStartOrig = insertStartOrigG;
      curBook->opEnd = *end_insert_pos;
   }
}

//Set the last inserted text to a single character. Used for the replace command.
void
set_last_insert(Unt c) {
   eeglFree(last_insert.c);
   last_insert.c = alloc(MB_MAXBYTES * 3 + 5);

   CS s = last_insert.c;
   // Use the CTRL-V only when entering a special char
   if (c < ' ' || c == DEL)
      *s++ = Ctrl_V;
   s = add_char2buf(c, s);
   *s++ = ESC;
   *s = ZERO;
   last_insert.len = (Unt)(s - last_insert.c);
   last_insert_skip = 0;
}

#if defined(EXITFREE) || defined(PROTO)
void
free_last_insert(void) {
   EE_CLEAR_STRING(last_insert);
}
#endif

//Add character "c" to buffer "s". Escape the special meaning of K_SPECIAL
//and CSI.  Handle multi-byte characters. Return a pointer to after the added bytes.
CS
add_char2buf(Unt c, CS s) {
   Byte temp[MB_MAXBYTES + 1];

   int len = mb_char2bytes(c, temp);
   for (int i = 0; i < len; ++i) {
      c = temp[i];
      // Need to escape K_SPECIAL and CSI like in the typeahead buffer.
      if (c == K_SPECIAL) {
         *s++ = K_SPECIAL;
         *s++ = KS_SPECIAL;
         *s++ = KE_FILLER;
      } else
         *s++ = c;
   }
   return s;
}

//move cursor to start of line
//if flags & BL_WHITE   move to first non-white
//if flags & BL_SOL   move to first non-white if startofline is set,
//            otherwise keep "curswant" column
//if flags & BL_FIX   don't leave the cursor on a ZERO.
void
beginline(Unt flags) {
   if ((flags & BL_SOL) != 0 && !p_sol)
      coladvance(curPor->cursWant);
   else {
      curPor->cursor.col = 0;
      curPor->cursor.coladd = 0;

      if ((flags & (BL_WHITE | BL_SOL)) != 0) {
         for (CS ptr = ml_get_curline(); 
              SPACE_OR_TAB(*ptr) && !((flags & BL_FIX) && ptr[1] == ZERO); 
              ++ptr
         )
            ++curPor->cursor.col;
      }
      curPor->setCursWant = TRUE;
   }
   adjust_skipcol();
}

//}}}
//{{{inserting special characters

//oneright oneleft cursor_down cursor_up
//
//Move one char {right,left,down,up}.
//Doesn't move onto the ZERO past the end of the line, unless it is allowed.
//Return OK when successful, FAIL when we hit a line of file boundary.
int
oneright(void) {
   if (virtual_active()) {
      Pos prevpos = curPor->cursor;

      // Adjust for multi-wide char (excluding TAB)
      CS ptr = ml_get_cursor();
      coladvance(getviscol() + ((*ptr != TAB
                    && bookIsCharPrintable((*mb_ptr2char)(ptr)))
             ? ptr2cells(ptr) : 1));
      curPor->setCursWant = TRUE;
      // Return OK if the cursor moved, FAIL otherwise (at window edge).
      return (prevpos.col != curPor->cursor.col
             || prevpos.coladd != curPor->cursor.coladd) ? OK : FAIL;
   }

   CS ptr = ml_get_cursor();
   if (*ptr == ZERO)
      return FAIL;       // already at the very end

   int l = utfCharLen(ptr);

   //move "l" bytes right, but don't end up on the ZERO, unless 'virtualedit'
   //contains "onemore".
   if (ptr[l] == ZERO)
      return FAIL;
   curPor->cursor.col += l;

   curPor->setCursWant = TRUE;
   adjust_skipcol();
   return OK;
}

int
oneleft(void) {
   if (virtual_active()) {
      int v = getviscol();
      if (v == 0)
         return FAIL;

      // We might get stuck on 'showbreak', skip over it.
      int width = 1;
      for (;;) {
         coladvance(v - width);
         // getviscol() is slow, skip it when 'showbreak' is empty,
         // 'breakindent' is not set and there are no multi-byte characters
         if (getviscol() < v)
            break;
         ++width;
      }

      if (curPor->cursor.coladd == 1) {
         // Adjust for multi-wide char (not a TAB)
         CS ptr = ml_get_cursor();
         if (*ptr != TAB && bookIsCharPrintable((*mb_ptr2char)(ptr)) && ptr2cells(ptr) > 1)
            curPor->cursor.coladd = 0;
      }

      curPor->setCursWant = TRUE;
      adjust_skipcol();
      return OK;
   }

   if (curPor->cursor.col == 0)
      return FAIL;

   curPor->setCursWant = TRUE;
   --curPor->cursor.col;

   //if the character on the left of the current cursor is a multi-byte
   //character, move to its first byte
   mb_adjust_cursor();
   adjust_skipcol();
   return OK;
}

//Move the cursor up "n" lines in portal "wp". Take care of closed folds.
void
cursor_up_inner(Portal* po, long n) {
   LineNr lnum = po->cursor.lnum;

   if (n >= lnum)
      lnum = 1;
   ei (hasAnyFolding(po)) {
      //Count each sequence of folded lines as one logical line.
      // go to the start of the current fold
      (void)getFoldsPortal(po, lnum, &lnum, NULL, TRUE, NULL);

      while (n--) {
         // move up one line
         --lnum;
         if (lnum <= 1)
            break;
         // If we entered a fold, move to the beginning, unless in
         // Insert mode or when 'foldopen' contains "all": it will open
         // in a moment.
         if (n > 0 || !((stateG & MODE_INSERT) || (p_fdo & FDO_ALL)))
            (void)getFoldsPortal(po, lnum, &lnum, NULL, TRUE, NULL);
      }
      if (lnum < 1)
          lnum = 1;
   } else
      lnum -= n;
   po->cursor.lnum = lnum;
}

int
cursor_up(long   n, Boole upd_topline){       // When TRUE: update topline
   // This fails if the cursor is already in the first line or the count is
   // larger than the line number and '-' is in 'cpoptions'
   LineNr lnum = curPor->cursor.lnum;
   if (n > 0 && lnum <= 1)
       return FAIL;
   cursor_up_inner(curPor, n);

   // try to advance to the column we want to be at
   coladvance(curPor->cursWant);

   if (upd_topline)
      update_topline();   // make sure curPor->topLine is valid

   return OK;
}

//Move the cursor down "n" lines in window "wp". Take care of closed folds.
void
cursor_down_inner(Portal* wp, long n) {
   LineNr lnum = wp->cursor.lnum;
   LineNr line_count = wp->book->mem.lineCount;

   if (lnum + n >= line_count)
      lnum = line_count;
   ei (hasAnyFolding(wp)) {
      LineNr   last;

      // count each sequence of folded lines as one logical line
      while (n--) {
         if (getFoldsPortal(wp, lnum, NULL, &last, TRUE, NULL))
            lnum = last + 1;
         else
            ++lnum;
         if (lnum >= line_count)
            break;
      }
      if (lnum > line_count)
         lnum = line_count;
   } else
      lnum += n;

   wp->cursor.lnum = lnum;
}

//Cursor down a number of logical lines.
int
cursor_down(long n, int upd_topline) {      // When TRUE: update topline
   LineNr lnum = curPor->cursor.lnum;
   LineNr line_count = curPor->book->mem.lineCount;
   // This fails if the cursor is already in the last (folded) line, or would
   // move beyond the last line and '-' is in 'cpoptions'.
   getFoldsPortal(curPor, lnum, NULL, &lnum, TRUE, NULL);
   if (n > 0 && lnum >= line_count)
      return FAIL;
   cursor_down_inner(curPor, n);

   // try to advance to the column we want to be at
   coladvance(curPor->cursWant);

   if (upd_topline)
      update_topline();   // make sure curPor->topLine is valid

   return OK;
}

//Stuff the last inserted text in the read buffer. Last_insert actually is a copy of the redo 
//buffer, so we first have to remove the command.
int
stuff_inserted(
   int       c,      // Command character to be inserted
   long    count,   // Repeat this many times
   int       no_esc   // Don't add an ESC at the end
){
   Byte last = ' ';

   Text insert = get_last_insert();// text to be inserted
   if (insert.c == NULL) {
      emsg(_(e_no_inserted_text_yet));
      return FAIL;
   }

   // may want to stuff the command character, to start Insert mode
   if (c != ZERO)
      stuffcharReadbuff(c);

   if (insert.len > 0) {
      // look for the last ESC in 'insert'
      for (CS p = insert.c + insert.len - 1; p >= insert.c; --p) {
         if (*p == ESC) {
            insert.len = (Unt)(p - insert.c);
            break;
         }
      }
   }

   if (insert.len > 0) {
      CS p = insert.c + insert.len - 1;

      // when the last char is either "0" or "^" it will be quoted if no ESC
      // comes after it OR if it will insert more than once and "ptr" starts with ^D.   -- Acevedo
      if ((*p == '0' || *p == '^') && (no_esc || (*insert.c == Ctrl_D && count > 1))) {
          last = *p;
          --insert.len;
      }
   }

   do {
      stuffReadbuffLen(insert.c, (long)insert.len);
      // a trailing "0" is inserted as "<C-V>048", "^" as "<C-V>^"
      switch (last) {
      case '0':
#define TEXT_TO_INSERT "\026\060\064\070"
          stuffReadbuffLen((CS)TEXT_TO_INSERT, STRLEN_LITERAL(TEXT_TO_INSERT));
#undef TEXT_TO_INSERT
          break;

      case '^':
#define TEXT_TO_INSERT "\026^"
          stuffReadbuffLen((CS)TEXT_TO_INSERT, STRLEN_LITERAL(TEXT_TO_INSERT));
#undef TEXT_TO_INSERT
          break;

      default:
          break;
      }
   } while (--count > 0);

   // may want to stuff a trailing ESC, to get out of Insert mode
   if (!no_esc)
      stuffcharReadbuff(ESC);

   return OK;
}

Text
get_last_insert(void){
   Text insert = {null, 0};

   if (last_insert.c) {
      insert.c = last_insert.c + last_insert_skip;
      insert.len = (Unt)(last_insert.len - last_insert_skip);
   }

   return insert;
}

//Get last inserted string, and remove trailing <Esc>. Return pointer to allocated memory 
//(must be freed) or NULL.
CS
get_last_insert_save(void){
   Text   insert = get_last_insert();

   if (!insert.c)
      return Em;
   CS s = copySubstr(insert.c, insert.len);
   if (!s)
      return Em;

   if (insert.len > 0 && s[insert.len - 1] == ESC)   // remove trailing ESC
      s[--insert.len] = ZERO;
   return s;
}

//Check the word in front of the cursor for an abbreviation. Called when the non-id character "c" 
//has been entered. When an abbreviation is recognized it is removed from the text and the 
//replacement string is inserted in typeBufG.c[], followed by "c".
private Boole
echeck_abbr(Unt c) {
   // Don't check for abbreviation in paste mode, when disabled and just
   // after moving around with cursor keys.
   if (no_abbr || arrow_used)
      return false;

   return check_abbr(c, ml_get_curline(), curPor->cursor.col,
      curPor->cursor.lnum == insertStartG.lnum ? insertStartG.col : 0);
}

private void
insertRegisterContents(void) {
   int      need_redraw = FALSE;
   Unt      regname;
   int      literally = 0;
   int      vis_active = VIsual_active;

   //If we are going to wait for a character, show a '"'.
   pc_status = PC_STATUS_UNSET;
   if (redrawing() && !char_avail()) {
      // may need to redraw when no more chars available now
      redrawInInsertMode(FALSE);

      edit_putchar('"', TRUE);
      add_to_showcmd_c(Ctrl_R);
   }

   //Don't map the register name. This also prevents the mode message to be deleted when ESC is hit
   ++no_mapping;
   ++allow_keys;
   regname = plain_vgetc();
   LANGMAP_ADJUST(regname, TRUE);
   if (regname == Ctrl_R || regname == Ctrl_O || regname == Ctrl_P)    {
      // Get a third key for literal register insertion
      literally = regname;
      add_to_showcmd_c(literally);
      regname = plain_vgetc();
      LANGMAP_ADJUST(regname, TRUE);
   }
   --no_mapping;
   --allow_keys;

   // Don't call u_sync() while typing the expression or giving an error
   // message for it. Only call it explicitly.
   ++no_u_sync;
   if (regname == '=')     {
      Pos   curpos = curPor->cursor;
      // Sync undo when evaluating the expression calls setline() or
      // append(), so that it can be undone separately.
      u_sync_once = 2;

      regname = get_expr_register();

      // Cursor may be moved back a column.
      curPor->cursor = curpos;
      check_cursor();
   }
   if (regname == ZERO || !valid_yank_reg(regname, FALSE)) {
      need_redraw = TRUE;   // remove the '"'
   } else {
      if (literally == Ctrl_O || literally == Ctrl_P)   {
         // Append the command to the redo buffer.
         AppendCharToRedobuff(Ctrl_R);
         AppendCharToRedobuff(literally);
         AppendCharToRedobuff(regname);

         do_put(regname, NULL, BACKWARD, 1L,
         (literally == Ctrl_P ? PUT_FIXINDENT : 0) | PUT_CURSEND);
      } ei (insert_reg(regname, literally) == FAIL) {
         need_redraw = TRUE;   // remove the '"'
      }
      ei (stop_insert_mode)
         // When the '=' register was used and a function was invoked that
         // did ":stopinsert" then stuff_empty() returns FALSE but we won't
         // insert anything, need to remove the '"'
         need_redraw = TRUE;
   }
   --no_u_sync;
   if (u_sync_once == 1)
      needUndoS = true;
   u_sync_once = 0;
   clear_showcmd();

   // If the inserted register is empty, we need to remove the '"'
   if (need_redraw || stuff_empty())
      edit_unputchar();

   // Disallow starting Visual mode here, would get a weird mode.
   if (!vis_active && VIsual_active)
      end_visual_mode();
}

// CTRL-G commands in Insert mode.
private void
ins_ctrl_g(void) {
   // Right after CTRL-X the cursor will be after the ruler.
   setcursor();

   //Don't map the second key. This also prevents the mode message to be deleted when ESC is hit.
   ++no_mapping;
   ++allow_keys;
   int c = plain_vgetc();
   --no_mapping;
   --allow_keys;
   switch (c) {
   // CTRL-G k and CTRL-G <Up>: cursor up to insertStartG.col
   case K_UP:
   case Ctrl_K:
   case 'k': 
      ins_up(TRUE);
      break;

   // CTRL-G j and CTRL-G <Down>: cursor down to insertStartG.col
   case K_DOWN:
   case Ctrl_J:
   case 'j': ins_down(TRUE);
      break;

   // CTRL-G u: start new undoable edit
   case 'u': u_sync(TRUE);
      needUndoS = true;

      //Need to reset insertStartG, esp. because a BS that joins
      //a line to the previous one must save for undo.
      update_insertStartOrigS = false;
      insertStartG = curPor->cursor;
      break;

   // CTRL-G U: do not break undo with the next char
   case 'U':
      // Allow one left/right cursor movement with the next char, without breaking undo.
      dont_sync_undo = MAYBE;
      break;

   case ESC:
      // Esc after CTRL-G cancels it.
      break;
   }
}

//CTRL-^ in Insert mode.
private void
ins_ctrl_hat(void) {
   if (map_to_exists_mode((CS)"", MODE_LANGMAP, FALSE)) {
      // ":lmap" mappings exists, Toggle use of ":lmap" mappings.
      if (stateG & MODE_LANGMAP) {
         curBook->o.b_p_iminsert = B_IMODE_NONE;
         stateG &= ~MODE_LANGMAP;
      } else {
         curBook->o.b_p_iminsert = B_IMODE_LMAP;
         stateG |= MODE_LANGMAP;
      }
   }
   showmode();
   // Show/unshow value of 'keymap' in status lines.
   drawAllStatusLinesOfCurBookLater();
}

//Handle ESC in insert mode.
//Return TRUE when leaving insert mode, FALSE when going to repeat the insert.
private int
ins_esc(long* count, int commChar, int nomove) {      // don't move cursor
   static int   disabled_redraw = FALSE;

   check_spell_redraw();

   int temp = curPor->cursor.col;
   if (disabled_redraw) {
      if (isRedrawingDisabledG > 0)
         --isRedrawingDisabledG;
      disabled_redraw = FALSE;
   }
   if (!arrow_used) {
      //Don't append the ESC for "r<CR>" and "grx".
      //When 'insertmode' is set only CTRL-L stops Insert mode. Needed for when "count" is non-zero
      if (commChar != 'r' && commChar != 'v')
          AppendToRedobuff(ESC_STR);

      // Repeating insert may take a long time.  Check for interrupt now and then.
      if (*count > 0) {
          line_breakcheck();
          if (gotInterruptG)
         *count = 0;
      }

      if (--*count > 0)   {// repeat what was typed
          (void)start_redo_ins();
          if (commChar == 'r' || commChar == 'v')
         stuffRedoReadbuff(ESC_STR);   // no ESC in redo buffer
          ++isRedrawingDisabledG;
          disabled_redraw = TRUE;
          return FALSE;   // repeat the insert
      }
      stop_insert(&curPor->cursor, TRUE, nomove);
   }

   if (commChar != 'r' && commChar != 'v') 
      ins_applyAutocomms(EVENT_INSERTLEAVEPRE);

   // When an autoindent was removed, curswant stays after the indent
   if (restart_edit == ZERO && (ColNr)temp == curPor->cursor.col)
      curPor->setCursWant = TRUE;

   // Remember the last Insert position in the '^ mark.
   if ((commModifierG.cmod_flags & CMOD_KEEPJUMPS) == 0)
      curBook->lastInsert = curPor->cursor;

   //The cursor should end up on the last inserted character.
   //Don't do it for CTRL-O, unless past the end of the line.
   if (!nomove
       && (curPor->cursor.col != 0 || curPor->cursor.coladd > 0)
       && (restart_edit == ZERO || (gchar_cursor() == ZERO && !VIsual_active))
   ) {
      if (curPor->cursor.coladd > 0) {
         oneleft();
         if (restart_edit != ZERO)
            ++curPor->cursor.coladd;
      } else {
         --curPor->cursor.col;
         curPor->cacheState &= ~(VALID_WCOL|VALID_VIRTCOL);
         // Correct cursor for multi-byte character.
         mb_adjust_cursor();
      }
   }

   stateG = MODE_NORMAL;
   may_trigger_modechanged();
   // need to position cursor again when on a TAB and when on a char with virtual text.
   if (gchar_cursor() == TAB || curBook->hasTextprop )
      curPor->cacheState &= ~(VALID_WROW|VALID_WCOL|VALID_VIRTCOL);

   setmouse();
   ui_cursor_shape();      // may show different cursor shape

   // When recording or for CTRL-O, need to display the new mode.
   // Otherwise remove the mode message.
   if (reg_recording != 0 || restart_edit != ZERO)
      showmode();
   ei (p_smd && (gotInterruptG || !skip_showmode()))
      msg(E);

   return TRUE;       // exit Insert mode
}

//If 'keymodel' contains "startsel", may start selection.
//Return TRUE when a CTRL-O and other keys stuffed.
private int
ins_start_select(int c) {
   if (!km_startsel)
      return FALSE;
   switch (c) {
   case K_KHOME:
   case K_KEND:
   case K_PAGEUP:
   case K_KPAGEUP:
   case K_PAGEDOWN:
   case K_KPAGEDOWN:
      if (!(modMaskG & MOD_MASK_SHIFT))
         break;
      // FALLTHROUGH
   case K_S_LEFT:
   case K_S_RIGHT:
   case K_S_UP:
   case K_S_DOWN:
   case K_S_END:
   case K_S_HOME:
      //Start selection right away, the cursor can move with CTRL-O when beyond the end of the line
      start_selection();

      // Execute the key in (insert) Select mode.
      stuffcharReadbuff(Ctrl_O);
      if (modMaskG) {
         Byte buf[4] = {K_SPECIAL, KS_MODIFIER, modMaskG, ZERO};
         stuffReadbuffLen(buf, 3L);
      }
      stuffcharReadbuff(c);
      return TRUE;
   }
   return FALSE;
}


//Pressed CTRL-O in Insert mode.
private void
ins_ctrl_o(void) {
   restart_edit = 'I';
   if (virtual_active())
      ins_at_eol = FALSE;   // cursor always keeps its column
   else
      ins_at_eol = (gchar_cursor() == ZERO);
}

//If the cursor is on an indent, ^T/^D insert/delete one shiftwidth.  Otherwise ^T/^D behave 
//like a "<<" or ">>". Always round the indent to 'shiftwidth'.
private void
ins_shift(Unt c, int lastc) {
   if (stop_arrow() == FAIL)
      return;
   AppendCharToRedobuff(c);

   //0^D and ^^D: remove all indent.
   if (c == Ctrl_D && (lastc == '0' || lastc == '^') && curPor->cursor.col > 0) {
      --curPor->cursor.col;
      (void)del_char(false);        // delete the '^' or '0'
      if (lastc == '^')
         old_indent = get_indent(); // remember curr. indent
      opChangeIndent(INDENT_SET, 0, 0, true);
   } else
      opChangeIndent(c == Ctrl_D ? INDENT_DEC : INDENT_INC, 0, 0, true);

   if (didAindentG && *skipwhite(ml_get_curline()) != ZERO)
      didAindentG = false;
   didSindentG = false;
   can_si = FALSE;
   can_si_back = FALSE;
   can_cindent = FALSE;   // no cindenting after ^D or ^T
}

// "Delete" key
private void
ins_del(void) {
   int temp;

   if (stop_arrow() == FAIL)
      return;
   if (gchar_cursor() == ZERO) {     // delete newline
      temp = curPor->cursor.col;
      if (jugJoinLinesUnderCursor(2, FALSE, TRUE, FALSE, FALSE) == FAIL) {
      } else {
         curPor->cursor.col = temp;
      }
   } else {
      del_char(false);  // delete char under cursor
   }
   didAindentG = false;
   didSindentG = FALSE;
   can_si = FALSE;
   can_si_back = FALSE;
   AppendCharToRedobuff(K_DEL);
}

// Delete one character for ins_bs().
private void
ins_bs_one(void) {
   dec_cursor();
   (void)del_char(false);
}

//Handle Backspace, delete-word and delete-line in Insert mode.
//Return TRUE when backspace was actually used.
private int
ins_bs(int c, int mode, int* inserted_space_p) {
   LineNr   lnum;
   int      cc;
   int      temp = 0;       // init for GCC
   ColNr   save_col;
   ColNr   mincol;
   int      did_backspace = FALSE;
   int      cpc[MAX_COMBINED_SYMBOLS];       // composing characters
   int      call_fix_indent = FALSE;

   //can't delete anything in an empty file
   //can't backup past first character in buffer
   //can't backup past starting point unless 'backspace' > 1
   //can backup to a previous line if 'backspace' == 0
   if (CURBOOK_EMPTY() || ( curPor->cursor.lnum == 1 && curPor->cursor.col == 0)) {
      return FALSE;
   }

   if (stop_arrow() == FAIL)
      return FALSE;
   int in_indent = inindent(0);
   if (in_indent)
      can_cindent = FALSE;
   end_comment_pending = ZERO;   // After BS, don't auto-end comment

   // Virtualedit:
   //   BACKSPACE_CHAR eats a virtual space
   //   BACKSPACE_WORD eats all coladd
   //   BACKSPACE_LINE eats all coladd and keeps going
   if (curPor->cursor.coladd > 0) {
      if (mode == BACKSPACE_CHAR) {
          --curPor->cursor.coladd;
          return TRUE;
      }
      if (mode == BACKSPACE_WORD) {
          curPor->cursor.coladd = 0;
          return TRUE;
      }
      curPor->cursor.coladd = 0;
   }

   // Delete newline!
   if (curPor->cursor.col == 0) {
      lnum = insertStartG.lnum;
      if (curPor->cursor.lnum == lnum) {
         if (u_save((LineNr)(curPor->cursor.lnum - 2), (LineNr)(curPor->cursor.lnum + 1)) == FAIL)
            return FALSE;
         --insertStartG.lnum;
         insertStartG.col = ml_get_len(insertStartG.lnum);
      }
      //In replace mode:
      //cc < 0: NL was inserted, delete it
      //cc >= 0: NL was replaced, put original characters back
      cc = -1;
      temp = gchar_cursor();   // remember current char
      --curPor->cursor.lnum;

      // When "aw" is in 'formatoptions' we must delete the space at
      // the end of the line, otherwise the line will be broken again when auto-formatting.
      if (has_format_option(FO_AUTO) && has_format_option(FO_WHITE_PAR)) {
         CS ptr = memGetLine(curBook, curPor->cursor.lnum, FALSE);
         int len = ml_get_curline_len();

         if (len > 0 && ptr[len - 1] == ' ') {
            CS newp = alloc(curBook->mem.lineLen - 1);

            mch_memmove(newp, ptr, len - 1);
            newp[len - 1] = ZERO;
            if (curBook->mem.lineLen > len + 1)
               mch_memmove(newp + len, ptr + len + 1, curBook->mem.lineLen - len - 1);

            if ((curBook->mem.flags & ML_LINE_DIRTY) != 0)
               eeglFree(curBook->mem.cachedLine);
            curBook->mem.cachedLine = newp;
            curBook->mem.lineLen--;
            curBook->mem.lineTextLen--;
            curBook->mem.flags |= ML_LINE_DIRTY;
         }
      }
      (void)jugJoinLinesUnderCursor(2, FALSE, FALSE, FALSE, FALSE);
      if (temp == ZERO && gchar_cursor() != ZERO)
         inc_cursor();

      didAindentG = false;
   } else {
      //Delete character(s) before the cursor.
      mincol = 0; // keep indent
      if (mode == BACKSPACE_LINE && (curBook->o.autoIndent || jugIsIndentationExpressionBased())) {
         save_col = curPor->cursor.col;
         beginline(BL_WHITE);
         if (curPor->cursor.col < save_col) {
            mincol = curPor->cursor.col;
            // should now fix the indent to match with the previous line
            call_fix_indent = TRUE;
         }
         curPor->cursor.col = save_col;
      }

      //Handle deleting one 'shiftwidth' or 'softtabstop'.
      if (mode == BACKSPACE_CHAR
         && ((get_sw_value(curBook) != 0) && curPor->cursor.col > 0
               && (*(ml_get_cursor() - 1) == TAB || (*(ml_get_cursor() - 1) == ' '
                     && (!*inserted_space_p || arrow_used)))
            )
      ) {
         ColNr   vcol = 0;
         ColNr   want_vcol;
         CS line;
         CS ptr;
         CS cursor_ptr;
         CS space_ptr;
         ColNr   space_vcol = 0;
         int      prev_space = FALSE;
         ColNr   want_col;

         *inserted_space_p = FALSE;

         space_ptr = ptr = line = ml_get_curline();
         cursor_ptr = line + curPor->cursor.col;

         // Compute virtual column of cursor position, and find the last
         // whitespace before cursor that is preceded by non-whitespace.
         // Use chartabsize() so that virtual text and wrapping are ignored.
         while (ptr < cursor_ptr) {
            int   cur_space = SPACE_OR_TAB(*ptr);

            if (!prev_space && cur_space) {
               space_ptr = ptr;
               space_vcol = vcol;
            }
            vcol += chartabsize(ptr, vcol);
            MB_PTR_ADV(ptr);
            prev_space = cur_space;
         }

         // Compute the virtual column where we want to be.
         want_vcol = vcol > 0 ? vcol - 1 : 0;
         want_vcol -= want_vcol % (int)get_sw_value(curBook);

         // Find the position to stop backspacing.
         // Use chartabsize() so that virtual text and wrapping are ignored.
         while (TRUE) {
            int size = chartabsize(space_ptr, space_vcol);

            if (space_vcol + size > want_vcol)
               break;
            space_vcol += size;
            MB_PTR_ADV(space_ptr);
         }
         want_col = space_ptr - line;

         // Delete characters until we are at or before want_col.
         while (curPor->cursor.col > want_col)
            ins_bs_one();

         // Insert extra spaces until we are at want_vcol.
         for (; space_vcol < want_vcol; space_vcol++) {
            // Remember the first char we inserted
            if (curPor->cursor.lnum == insertStartOrigG.lnum
                     && curPor->cursor.col < insertStartOrigG.col
            )
               insertStartOrigG.col = curPor->cursor.col;
            ins_str(S" ", 1);
         }
      }

      //Delete up to starting point, start of line or previous word.
      else {
         int cclass = mb_get_class(ml_get_cursor());
         do {
            dec_cursor();

            cc = gchar_cursor();
            // look multi-byte character class
            int prev_cclass = cclass;
            cclass = mb_get_class(ml_get_cursor());

            // start of word?
            if (mode == BACKSPACE_WORD && !isSpace(cc)) {
               mode = BACKSPACE_WORD_NOT_SPACE;
               temp = eeIsWordc(cc);
            }
            // end of word?
            ei (mode == BACKSPACE_WORD_NOT_SPACE
               && ((isSpace(cc) || eeIsWordc(cc) != temp) || prev_cclass != cclass)
            ){
               inc_cursor();
               break;
            }
            if (p_delcomb)
               (void)utfc_ptr2char(ml_get_cursor(), cpc);
            (void)del_char(false);
            //If there are combining characters and 'delcombine' is set
            //move the cursor back.  Don't back up before the base character.
            if (p_delcomb && cpc[0] != ZERO)
               inc_cursor();
            // Just a single backspace?:
            if (mode == BACKSPACE_CHAR)
               break;
          } while ( (curPor->cursor.col > mincol));
      }
      did_backspace = TRUE;
   }
   didSindentG = FALSE;
   can_si = FALSE;
   can_si_back = FALSE;
   if (curPor->cursor.col <= 1)
      didAindentG = false;

   if (call_fix_indent)
      fix_indent();

   //It's a little strange to put backspaces into the redo
   //buffer, but it makes auto-indent a lot easier to deal with.
   AppendCharToRedobuff(c);

   // If deleted before the insertion point, adjust it
   if (curPor->cursor.lnum == insertStartOrigG.lnum && curPor->cursor.col < insertStartOrigG.col)
      insertStartOrigG.col = curPor->cursor.col;

   // When deleting a char the cursor line must never be in a closed fold.
   // E.g., when 'foldmethod' is indent and deleting the first non-white char before a Tab.
   if (did_backspace)
      foldOpenCursor();

   return did_backspace;
}

//Handle receiving P_PS: start paste mode. Insert the following text up to P_PE literally.
//When "drop" is TRUE then consume the text and drop it.
int
bracketed_paste(PasteMode mode, int drop, ArrayList *gap) {
   Unt c;
   Byte buf[NUMBUFLEN + MB_MAXBYTES];
   int idx = 0;
   CS end = find_termcode((CS)"PE");
   int ret_char = -1;
   int save_allow_keys = allow_keys;

   // If the end code is too long we can't detect it, read everything.
   if (end && STRLEN(end) >= NUMBUFLEN)
      end = null;
   ++no_mapping;
   allow_keys = 0;

   for (;;) {
      // When the end is not defined read everything there is.
      if (end == NULL && vpeekc() == ZERO)
          break;
      do
          c = vgetc();
      while (c == K_IGNORE || c == K_VER_SCROLLBAR || c == K_HOR_SCROLLBAR);

      if (c == ZERO || gotInterruptG || (ex_normal_busy > 0 && c == Ctrl_C))
          // When CTRL-C was encountered the typeahead will be flushed and we
          // won't get the end sequence.  Except when using ":normal".
          break;

      idx += (*mb_char2bytes)(c, buf + idx);
      buf[idx] = ZERO;
      if (end != NULL && STRNCMP(buf, end, idx) == 0) {
         if (end[idx] == ZERO)
            break; // Found the end of paste code.
         continue;
      }
      if (!drop) {
         switch (mode) {
         case PASTE_CMDLINE:
            put_on_cmdline(buf, idx, TRUE);
            break;

         case PASTE_EX:
            // add one for the ZERO that is going to be appended
            if (gap != NULL && ga_grow(gap, idx + 1) == OK) {
               mch_memmove((char *)gap->c + gap->len, buf, (Unt)idx);
               gap->len += idx;
            }
            break;

         case PASTE_INSERT:
            if (stop_arrow() == OK) {
               c = buf[0];
               if (idx == 1 && (c == ENTER || c == K_KENTER || c == NL))
                  ins_eol(c);
               else {
                  opInsertCharBytes(buf, idx, false);
                  AppendToRedobuffLit(buf, idx);
               }
            }
            break;

         case PASTE_ONE_CHAR:
            if (ret_char == -1) {
               ret_char = (*mb_ptr2char)(buf);
            }
            break;
         }
      }
      idx = 0;
   }

   --no_mapping;
   allow_keys = save_allow_keys;

   return ret_char;
}

private void
ins_left(void) {
   int      end_change = dont_sync_undo == FALSE; // end undoable change

   if ((p_fdo & FDO_HOR) != 0 && KeyTyped)
      foldOpenCursor();
   Pos tpos = curPor->cursor;
   if (oneleft() == OK) {
      start_arrow_with_change(&tpos, end_change);
      if (!end_change)
          AppendCharToRedobuff(K_LEFT);
   }

   // if 'whichwrap' set for cursor in insert mode may go to previous line
   ei (p_ww && firstOccurrence(p_ww, '[') != NULL && curPor->cursor.lnum > 1) {
      // always break undo when moving upwards/downwards, else undo may break
      start_arrow(&tpos);
      --(curPor->cursor.lnum);
      coladvance((ColNr)MAXCOL);
      curPor->setCursWant = TRUE;   // so we stay at the end
   }
   dont_sync_undo = FALSE;
}

private void
ins_home(Unt c) {
   if ((p_fdo & FDO_HOR) && KeyTyped)
      foldOpenCursor();
   Pos tpos = curPor->cursor;
   if (c == K_C_HOME)
      curPor->cursor.lnum = 1;
   curPor->cursor.col = 0;
   curPor->cursor.coladd = 0;
   curPor->cursWant = 0;
   start_arrow(&tpos);
}

private void
ins_end(Unt c) {
   if ((p_fdo & FDO_HOR) && KeyTyped)
      foldOpenCursor();
   Pos tpos = curPor->cursor;
   if (c == K_C_END)
      curPor->cursor.lnum = curBook->mem.lineCount;
   coladvance((ColNr)MAXCOL);
   curPor->cursWant = MAXCOL;

   start_arrow(&tpos);
}

private void
ins_s_left(void) {
   int end_change = dont_sync_undo == FALSE; // end undoable change
   if ((p_fdo & FDO_HOR) && KeyTyped)
      foldOpenCursor();
   if (curPor->cursor.lnum > 1 || curPor->cursor.col > 0) {
      start_arrow_with_change(&curPor->cursor, end_change);
      if (!end_change)
          AppendCharToRedobuff(K_S_LEFT);
      (void)bck_word(1L, FALSE, FALSE);
      curPor->setCursWant = TRUE;
   }
   dont_sync_undo = FALSE;
}

private void
ins_right(void) {
   int end_change = dont_sync_undo == FALSE; // end undoable change

   if ((p_fdo & FDO_HOR) && KeyTyped)
      foldOpenCursor();
   if (gchar_cursor() != ZERO || virtual_active()) {
      start_arrow_with_change(&curPor->cursor, end_change);
      if (!end_change)
         AppendCharToRedobuff(K_RIGHT);
      curPor->setCursWant = TRUE;
      if (virtual_active())
          oneright();
      else {
         curPor->cursor.col += utfCharLen(ml_get_cursor());
      }
   }
   // if 'whichwrap' set for cursor in insert mode, may move the cursor to the next line
   ei (p_ww && firstOccurrence(p_ww, ']') != NULL && curPor->cursor.lnum < curBook->mem.lineCount) {
       start_arrow(&curPor->cursor);
       curPor->setCursWant = TRUE;
       ++curPor->cursor.lnum;
       curPor->cursor.col = 0;
   }
   dont_sync_undo = FALSE;
}

private void
ins_s_right(void) {
   int end_change = dont_sync_undo == FALSE; // end undoable change
   if ((p_fdo & FDO_HOR) && KeyTyped)
      foldOpenCursor();
   if (curPor->cursor.lnum < curBook->mem.lineCount || gchar_cursor() != ZERO) {
      start_arrow_with_change(&curPor->cursor, end_change);
      if (!end_change)
         AppendCharToRedobuff(K_S_RIGHT);
      (void)fwd_word(1L, FALSE, 0);
      curPor->setCursWant = TRUE;
   }
   dont_sync_undo = FALSE;
}

private void
ins_up( int      startcol) {  // when TRUE move to insertStartG.col
   LineNr   old_topline = curPor->topLine;
   int      old_topfill = curPor->topFill;
   Pos tpos = curPor->cursor;
   if (cursor_up(1L, true) == OK) {
      if (startcol)
          coladvance(getvcol_nolist(&insertStartG));
      if (old_topline != curPor->topLine || old_topfill != curPor->topFill)
          redraw_later(UPD_VALID);
      start_arrow(&tpos);
      can_cindent = TRUE;
   }
}

private void
ins_pageup(void) {
   if ((modMaskG & MOD_MASK_CTRL) != 0) {
      // <C-PageUp>: tab back
      if (firstTabG->next != NULL) {
         start_arrow(&curPor->cursor);
         gotoTabById(-1);
      }
      return;
   }

   Pos tpos = curPor->cursor;
   if (pagescroll(BACKWARD, 1L, FALSE) == OK) {
      start_arrow(&tpos);
      can_cindent = TRUE;
   }
}

private void
ins_down(int startcol) {  // when TRUE move to insertStartG.col
   LineNr old_topline = curPor->topLine;
   int old_topfill = curPor->topFill;

   Pos tpos = curPor->cursor;
   if (cursor_down(1L, TRUE) == OK) {
      if (startcol)
         coladvance(getvcol_nolist(&insertStartG));
      if (old_topline != curPor->topLine || old_topfill != curPor->topFill)
         redraw_later(UPD_VALID);
      start_arrow(&tpos);
      can_cindent = TRUE;
   }
}

private void
ins_pagedown(void) {
   if (modMaskG & MOD_MASK_CTRL)  {
      // <C-PageDown>: tab forward
      if (firstTabG->next != NULL)   {
         start_arrow(&curPor->cursor);
         gotoTabById(0);
      }
      return;
   }

   Pos tpos = curPor->cursor;
   if (pagescroll(FORWARD, 1L, FALSE) == OK) {
      start_arrow(&tpos);
      can_cindent = TRUE;
   }
}

private void
ins_drop(void) {
   do_put('~', NULL, BACKWARD, 1L, PUT_CURSEND);
}

//Handle TAB in Insert mode.
//Return TRUE when the TAB needs to be inserted like a normal character.
private int
ins_tab(void) {
   int      i;
   int      temp;

   if (insertStartG_blank_vcol == MAXCOL && curPor->cursor.lnum == insertStartG.lnum)
      insertStartG_blank_vcol = get_nolist_virtcol();
   if (echeck_abbr(TAB + ABBR_OFF))
      return FALSE;

   int ind = inindent(0);
   if (ind)
      can_cindent = FALSE;

   //When nothing special, insert TAB like a normal character.
   if (!curBook->o.expandTab && get_sw_value(curBook) == 0)
      return TRUE;

   if (stop_arrow() == FAIL)
      return TRUE;

   didAindentG = false;
   didSindentG = false;
   can_si = FALSE;
   can_si_back = FALSE;
   AppendToRedobuff((CS)"\t");

   temp = (int)get_sw_value(curBook);
   temp -= get_nolist_virtcol() % temp;

   //Insert the first space with insertChar(). Insert the rest with ins_str(); it will not delete
   //any chars.  For MODE_VREPLACE state, we use ins_char() for all characters.
   insertChar(' ');
   while (--temp > 0) {
      ins_str((CS)" ", 1);
   }

   //When 'expandtab' not set: Replace spaces by TABs where possible.
   if (!curBook->o.expandTab) {
      Pos* cursor;
      ColNr want_vcol, vcol;
      int change_col = -1;
      int save_list = curPor->o.list;
      CS tab = S"\t";
      CharTableSize   cts;

      //Get the current line.
      CS ptr = ml_get_cursor();
      cursor = &curPor->cursor;

      // When 'L' is not in 'cpoptions' a tab always takes up 'ts' spaces.
      curPor->o.list = FALSE;

      // Find first white before the cursor
      Pos fpos = curPor->cursor;
      while (fpos.col > 0 && SPACE_OR_TAB(ptr[-1])) {
         --fpos.col;
         --ptr;
      }

      // compute virtual column numbers of first white and cursor
      getvcol(curPor, &fpos, &vcol, NULL, NULL);
      getvcol(curPor, cursor, &want_vcol, NULL, NULL);

      bookInitCharsForKeywordsSizeArg(&cts, curPor, 0, vcol, tab, tab);

      // Use as many TABs as possible.  Beware of 'breakindent', 'showbreak'
      // and 'linebreak' adding extra virtual columns.
      while (SPACE_OR_TAB(*ptr)) {
         i = lbr_chartabsize(&cts);
         if (cts.cts_vcol + i > want_vcol)
            break;
         if (*ptr != TAB) {
            *ptr = TAB;
            if (change_col < 0) {
               change_col = fpos.col;  // Column of first change
               // May have to adjust insertStartG
               if (fpos.lnum == insertStartG.lnum && fpos.col < insertStartG.col)
                  insertStartG.col = fpos.col;
            }
         }
         ++fpos.col;
         ++ptr;
         cts.cts_vcol += i;
      }
      vcol = cts.cts_vcol;
      clear_chartabsize_arg(&cts);

      if (change_col >= 0) {
         int repl_off = 0;

         // Skip over the spaces we need.
         bookInitCharsForKeywordsSizeArg(&cts, curPor, 0, vcol, ptr, ptr);
         while (cts.cts_vcol < want_vcol && *cts.cts_ptr == ' ') {
            cts.cts_vcol += lbr_chartabsize(&cts);
            ++cts.cts_ptr;
            ++repl_off;
         }
         ptr = cts.cts_ptr;
         vcol = cts.cts_vcol;
         clear_chartabsize_arg(&cts);

         if (vcol > want_vcol) {
            // Must have a char with 'showbreak' just before it.
            --ptr;
            --repl_off;
         }
         fpos.col += repl_off;

         // Delete following spaces.
         i = cursor->col - fpos.col;
         if (i > 0) {
            CS newp = alloc(curBook->mem.lineLen - i);

            int col = ptr - curBook->mem.cachedLine;
            if (col > 0)
               mch_memmove(newp, ptr - col, col);
            mch_memmove(newp + col, ptr + i, curBook->mem.lineLen - col - i);

            if ((curBook->mem.flags & ML_LINE_DIRTY) != 0)
               eeglFree(curBook->mem.cachedLine);
            curBook->mem.cachedLine = newp;
            curBook->mem.lineLen -= i;
            curBook->mem.lineTextLen = 0;
            curBook->mem.flags = (curBook->mem.flags | ML_LINE_DIRTY) & ~ML_EMPTY;
            cursor->col -= i;
         }
      }
      curPor->o.list = save_list;
   }
   return FALSE;
}

//Handle CR or NL in insert mode. Return FAIL when out of memory or can't undo.
private int
ins_eol(Unt c) {
   if (echeck_abbr(c + ABBR_OFF))
      return OK;
   if (stop_arrow() == FAIL)
      return FAIL;

   //In MODE_VREPLACE state, a NL replaces the rest of the line, and starts
   //replacing the next line, so we push all of the characters left on the
   //line onto the replace stack.  This is not done here though, it is done in openLine().

   // Put cursor on ZERO if on the last char and coladd is 1 (happens after CTRL-O).
   if (virtual_active() && curPor->cursor.coladd > 0)
      coladvance(getviscol());

   AppendToRedobuff(NL_STR);
   int i = openLine(has_format_option(FO_RET_COMS) ? OPENLINE_DO_COM : 0, old_indent);
   old_indent = 0;
   can_cindent = TRUE;
   // When inserting a line the cursor line must never be in a closed fold.
   foldOpenCursor();

   return i;
}

//Handle CTRL-E and CTRL-Y in Insert mode: copy char from other line.
 //Return the char to be inserted, or ZERO if none found.
int
ins_copychar(LineNr lnum) {
   if (lnum < 1 || lnum > curBook->mem.lineCount) {
      return ZERO;
   }

   // try to advance to the cursor column
   validate_virtcol();
   CS line = ml_get(lnum);
   CS prev_ptr = line;
   CharTableSize cts;
   bookInitCharsForKeywordsSizeArg(&cts, curPor, lnum, 0, line, line);
   while (cts.cts_vcol < curPor->virtCol && *cts.cts_ptr != ZERO) {
      prev_ptr = cts.cts_ptr;
      cts.cts_vcol += lbr_chartabsize_adv(&cts);
   }
   CS ptr = (cts.cts_vcol > curPor->virtCol) ? prev_ptr : cts.cts_ptr;
   clear_chartabsize_arg(&cts);

   return mb_ptr2char(ptr);
}

// CTRL-Y or CTRL-E typed in Insert mode.
private Unt
ins_ctrl_ey(Unt tc) {
   Unt c = tc;

   if (ctrl_x_mode_scroll()) {
      if (c == Ctrl_Y)
         scrolldown_clamp();
      else
         scrollup_clamp();
      redraw_later(UPD_VALID);
   } else {
      c = ins_copychar(curPor->cursor.lnum + (c == Ctrl_Y ? -1 : 1));
      if (c != ZERO) {
         long   tw_save;

         // The character must be taken literally, insert like it was typed after a CTRL-V, and 
         // pretend 'textwidth' wasn't set.  Digits, 'o' and 'x' are special after a
         // CTRL-V, don't use it for these.
         if (c < 256 && !SAFE_isalnum(c))
            AppendToRedobuff((CS)CTRL_V_STR);   // CTRL-V
         tw_save = curBook->o.textWidth;
         curBook->o.textWidth = -1;
         insertRegular(c, true, false);
         curBook->o.textWidth = tw_save;
         c = Ctrl_V;   // pretend CTRL-V is last character
         auto_format(FALSE, TRUE);
      }
   }
   return c;
}

// Get the value that virtCol would have when 'list' is off.
ColNr
get_nolist_virtcol(void) {
   // check validity of cursor in current book
   if (!curPor->book
         || !curPor->book->mem.mfile
         || curPor->cursor.lnum > curPor->book->mem.lineCount)
      return 0;
   if (curPor->o.list)
      return getvcol_nolist(&curPor->cursor);
   validate_virtcol();
   return curPor->virtCol;
}

void
set_can_cindent(int val) {
    can_cindent = val;
}

// Trigger "event" and take care of fixing undo.
int
ins_applyAutocomms(AutoEvent event) {
   Long   tick = CHANGEDTICK(curBook);
   int r = applyAutocomms(event, NULL, NULL, false, curBook);

   // If u_savesub() was called then we are not prepared to start a new line. Call u_save() with no
   // contents to fix that. Except when leaving Insert mode.
   if (event != EVENT_INSERTLEAVE && tick != CHANGEDTICK(curBook))
      u_save(curPor->cursor.lnum, (LineNr)(curPor->cursor.lnum + 1));

   return r;
}

//}}}
//{{{completion (Ctrl-X) mode

private Callback completeFnS;  // 'completefunc' callback function
private Callback omniFnS;      // 'omnifunc' callback function
private Callback thesaurusCbS; // 'thesaurusfunc' callback function
private Callback customCompleteFnS; 

#define CFC_KEYWORD         0x001
#define CFC_FILES           0x002
#define CFC_WHOLELINE       0x004

//Definitions used for CTRL-X submode.
//Note: If you change CTRL-X submode, you must also maintain ctrl_x_msgs[] and 
//ctrl_x_mode_names[] below
#define CTRL_X_WANT_IDENT   0x100

#define CTRL_X_NORMAL            0  // CTRL-N CTRL-P completion, default
#define CTRL_X_NOT_DEFINED_YET   1
#define CTRL_X_SCROLL            2
#define CTRL_X_WHOLE_LINE        3
#define CTRL_X_FILES             4
#define CTRL_X_TAGS             (5 + CTRL_X_WANT_IDENT)
#define CTRL_X_PATH_PATTERNS    (6 + CTRL_X_WANT_IDENT)
#define CTRL_X_PATH_DEFINES     (7 + CTRL_X_WANT_IDENT)
#define CTRL_X_FINISHED          8
#define CTRL_X_DICTIONARY       (9 + CTRL_X_WANT_IDENT)
#define CTRL_X_THESAURUS        (10 + CTRL_X_WANT_IDENT)
#define CTRL_X_CMDLINE          11
#define CTRL_X_FUNCTION         12
#define CTRL_X_OMNI             13
#define CTRL_X_LOCAL_MSG        15   //only used in "ctrl_x_msgs"
#define CTRL_X_EVAL             16   //for builtin function complete()
#define CTRL_X_CMDLINE_CTRL_X   17   //CTRL-X typed in CTRL_X_CMDLINE
#define CTRL_X_REGISTER         18   //complete words from registers

#define CTRL_X_MSG(i) ctrl_x_msgs[(i) & ~CTRL_X_WANT_IDENT]

// Message for CTRL-X mode, index is ctrl_x_mode.
private CS ctrl_x_msgs[] = {
   N_(" Keyword completion (^N^P)"), // CTRL_X_NORMAL, ^P/^N compl.
   N_(" ^X mode (^]^D^E^F^I^K^L^N^O^P^Rs^U^V^Y)"),
   NULL, // CTRL_X_SCROLL: depends on state
   N_(" Whole line completion (^L^N^P)"),
   N_(" File name completion (^F^N^P)"),
   N_(" Tag completion (^]^N^P)"),
   N_(" Path pattern completion (^N^P)"),
   N_(" Definition completion (^D^N^P)"),
   NULL, // CTRL_X_FINISHED
   N_(" Dictionary completion (^K^N^P)"),
   N_(" Thesaurus completion (^T^N^P)"),
   N_(" Command-line completion (^V^N^P)"),
   N_(" User defined completion (^U^N^P)"),
   N_(" Omni completion (^O^N^P)"),
   N_(" Spelling suggestion (^S^N^P)"),
   N_(" Keyword Local completion (^N^P)"),
   NULL,   // CTRL_X_EVAL doesn't use msg.
   N_(" Command-line completion (^V^N^P)"),
   N_(" Register completion (^N^P)"),
};

private CS ctrl_x_mode_names[] = {SMAP((CS),
   "keyword",
   "ctrl_x",
   "scroll",
   "whole_line",
   "files",
   "tags",
   "path_patterns",
   "path_defines",
   "unknown",          // CTRL_X_FINISHED
   "dictionary",
   "thesaurus",
   "cmdline",
   "function",
   "omni",
   "spell"),
   NULL,          // CTRL_X_LOCAL_MSG only used in "ctrl_x_msgs"
   SMAP((CS),
   "eval",
   "cmdline",
   "register"
)};

// Structure used to store one match for insert completion.
typedef struct InsertCompletion InsertCompletion;
struct InsertCompletion {
   InsertCompletion* next;
   InsertCompletion* prev;
   InsertCompletion* nextMatch;      // matched next InsertCompletion
   Text   cp_str;         // matched text
   CS cp_text[CPT_COUNT];   // text for the menu
   Var   userData;
   CS fName; // file containing the match, allocated when flags has CP_FREE_FNAME
   Unt flags;      // CP_ values
   int cp_number;      // sequence number
   int cp_score;      // fuzzy match score or proximity score
   int cp_in_match_array;   // collected by displayedCompletionsS
   Decoration abbrDeco;   // hilite decoration for abbr
   Decoration kindDeco;   // hilite decoration for kind
   int indexOfSourceInCpt;   // index of this match's source in 'cpt' option
};

// values for flags
#define CP_ORIGINAL_TEXT  1   // the original text when the expansion begun
#define CP_FREE_FNAME     2   // fName is allocated
#define CP_CONT_S_IPOS    4   // use CONT_S_IPOS for compl_cont_status
#define CP_EQUAL          8   // ins_compl_equal() always returns TRUE
#define CP_ICASE         16   // ins_compl_equal() ignores case
#define CP_FAST          32   // use fast_breakcheck instead of ui_breakcheck

//All the current matches are stored in a list.
//"compl_first_match" points to the start of the list.
//"compl_curr_match" points to the currently selected entry.
//"compl_shown_match" is different from compl_curr_match during
//ins_compl_get_exp(), when new matches are added to the list.
//"compl_old_match" points to previous "compl_curr_match".
private InsertCompletion* compl_first_match = NULL;
private InsertCompletion* compl_curr_match = NULL;
private InsertCompletion* compl_shown_match = NULL;
private InsertCompletion* compl_old_match = NULL;

// list used to store the InsertCompletion which have the max score
// used for completefuzzycollect
private InsertCompletion** compl_best_matches = NULL;
private int complCountBestS = 0;
// inserted a longest when completefuzzycollect enabled
private int compl_cfc_longest_ins = FALSE;

// After using a cursor key <Enter> selects a match in the popup menu,
// otherwise it inserts a line break.
private int compl_enter_selects = FALSE;

// When "compl_leader" is not NULL only matches that start with this string are used.
private Text compl_leader = {NULL, 0};

private int compl_get_longest = FALSE; // put longest common string in compl_leader

// Selected one of the matches. When FALSE, the match was edited or using the longest common string
private Boole complUsedMatchS;

// didn't finish finding completions.
private int compl_was_interrupted = FALSE;

// Set when character typed while looking for matches and it means we should
// stop looking for matches.
private int compl_interrupted = FALSE;

private int compl_restarting = FALSE;   // don't insert match

// When the first completion is done "compl_started" is set.  When it's
// FALSE the word to be completed must be located.
private int compl_started = FALSE;

// Which Ctrl-X mode are we in?
private Unt ctrl_x_mode = CTRL_X_NORMAL;

private int compl_matches = 0;       // number of completion matches
private Text compl_pattern = {NULL, 0};    // search pattern for matching items
private Text cpt_compl_pattern = {NULL, 0}; // pattern returned by func in 'cpt'
private Unt compl_direction = FORWARD;
private Unt compl_shows_dir = FORWARD;
private int compl_pending = 0;       // > 1 for postponed CTRL-N
private Pos compl_startpos;
// Length in bytes of the text being completed (this is deleted to be replaced by the match)
private int     compl_length = 0;
private LineNr     compl_lnum = 0;           // lnum where the completion start
private ColNr compl_col = 0; // column where the text starts that is being completed
private ColNr compl_ins_end_col = 0;
private Text compl_orig_text = {NULL, 0};  // text as it was before completion started
private Unt compl_cont_mode = 0;
private Expand compl_xp;

private Portal* compl_curr_win = NULL;  // win where completion is active
private Book* compl_curr_buf = NULL;  // buf where completion is active

#define COMPL_INITIAL_TIMEOUT_MS    80
// Autocomplete uses a decaying timeout: starting from COMPL_INITIAL_TIMEOUT_MS, if the current 
// source exceeds its timeout, it is interrupted and the next begins with half the time. A small 
// minimum timeout ensures every source gets at least a brief chance.
private int compl_autocomplete = FALSE;       // whether autocompletion is active
private int InsertCompletionimeout_ms = COMPL_INITIAL_TIMEOUT_MS;
private int InsertCompletionime_slice_expired = FALSE; // time budget exceeded for current source
private int compl_from_nonkeyword = FALSE;    // completion started from non-keyword

// Halve the current completion timeout, simulating exponential decay.
#define COMPL_MIN_TIMEOUT_MS   5
#define DECAY_InsertCompletionIMEOUT() \
    do { \
   if (InsertCompletionimeout_ms > COMPL_MIN_TIMEOUT_MS) \
       InsertCompletionimeout_ms /= 2; \
    } while (0)

// List of flags for method of completion.
private int     compl_cont_status = 0;
#define CONT_ADDING 1   // "normal" or "adding" expansion
#define CONT_INTRPT (2 + 4) // a ^X interrupted the current expansion. Set only iff N_ADDS is set
#define CONT_N_ADDS 4 // next ^X<> will add-new or expand-current
#define CONT_S_IPOS 8 // next ^X<> will set initial_pos? if so, word-wise-expansion will set SOL
#define CONT_SOL   16 // pattern includes start of line, just for word-wise expansion, 
                       // not set for ^X^L
#define CONT_LOCAL 32 // for ctrl_x_mode 0, ^X^P/^X^N do a local expansion, (eg use complete=.)

private int compl_opt_refresh_always = FALSE;
private int compl_opt_suppress_empty = FALSE;

private int compl_selected_item = -1;

private int* compl_fuzzy_scores;

// Define the structure for completion source (in 'cpt' option) information
typedef struct CompletionSource {
   int   refreshAlways;  // Whether 'refresh:always' is set for func
   int   startCol;       // Start column returned by func
   int   maxMatches;       // Max items to display from this source
   Elapsed   matchCollectionStart;       // Timestamp when match collection starts
} CompletionSource;

private CompletionSource *cpt_sources_array; // Pointer to the array of completion sources
private int cpt_sources_count;  // Total number of completion sources specified in the 'cpt' option
private int cpt_sources_index = -1;  // Index of the current completion source being expanded

// "displayedCompletionsS" points the currently displayed list of entries in the
// popup menu. It is NULL when there is no popup menu.
private Arr(PopupItem) displayedCompletionsS = NULL;
private int displayedCompletionsSsize;

private Unt addMatchToList(
   CS str, int len, CS fname, CS* cptext, Var *user_data, Unt cdir, Unt flags, 
   Boole adup, Arr(Decoration) userDecos, int score
);
private void ins_compl_longest_match(InsertCompletion *match);
private void ins_compl_del_pum(void);
private void filterFromFiles(
   ExpandMatch files, int thesaurus, Unt flags, RegMatch *regmatch, CS buf, OUT Unt *dir
);
private void ins_compl_free(void);
private int  ins_compl_need_restart(void);
private void ins_compl_new_leader(void);
private int  get_compl_len(void);
private void ins_compl_restart(void);
private void ins_compl_set_original_text(CS str, Unt len);
private void ins_compl_fixRedoBufForLeader(CS ptr_arg);
private void ins_compl_add_list(List *list);
private void ins_compl_add_dict(Bag *bag);
private int get_userdefined_compl_info(ColNr curs_col, Callback *cb, int *startcol);
private void get_cfn_completion_matches(Callback *cb);
private Callback *get_callback_if_cfn(CS p);
private Unt setup_cpt_sources(void);
private Boole is_cfn_refresh_always(void);
private void cpt_sources_clear(void);
private void cpt_compl_refresh(void);
private Unt  ins_compl_key2dir(Unt c);
private Boole ins_compl_pum_key(Unt c);
private int  ins_compl_key2count(Unt c);
private void show_pum(int prev_cursorRow, int prev_leftCol);
private unsigned  quote_meta(CS dest, CS str, int len);
private Boole ins_compl_has_multiple(void);
private void ins_compl_expand_multiple(CS str);
private void ins_compl_longest_insert(CS prefix);
private void ins_compl_make_linear(void);
private int ins_compl_make_cyclic(void);


// CTRL-X pressed in Insert mode.
void
ins_ctrl_x(void) {
   if (!ctrl_x_mode_cmdline()) {
      // if the next ^X<> won't ADD nothing, then reset compl_cont_status
      if ((compl_cont_status & CONT_N_ADDS) && !p_ac)
         compl_cont_status |= CONT_INTRPT;
      else
         compl_cont_status = 0;
      // We're not sure which CTRL-X mode it will be yet
      ctrl_x_mode = CTRL_X_NOT_DEFINED_YET;
      edit_submode = (CS)_(CTRL_X_MSG(ctrl_x_mode));
      edit_submode_pre = NULL;
      showmode();
   } else
      // CTRL-X in CTRL-X CTRL-V mode behaves differently to make CTRL-X
      // CTRL-V look like CTRL-N
      ctrl_x_mode = CTRL_X_CMDLINE_CTRL_X;

   may_trigger_modechanged();
}

// Functions to check the current CTRL-X mode.
private int ctrl_x_mode_normal(void)
    { return ctrl_x_mode == CTRL_X_NORMAL; }
private int ctrl_x_mode_scroll(void)
    { return ctrl_x_mode == CTRL_X_SCROLL; }
int ctrl_x_mode_whole_line(void)
    { return ctrl_x_mode == CTRL_X_WHOLE_LINE; }
private int ctrl_x_mode_files(void)
    { return ctrl_x_mode == CTRL_X_FILES; }
private int ctrl_x_mode_tags(void)
    { return ctrl_x_mode == CTRL_X_TAGS; }
private int ctrl_x_mode_path_patterns(void)
    { return ctrl_x_mode == CTRL_X_PATH_PATTERNS; }
private int ctrl_x_mode_path_defines(void)
    { return ctrl_x_mode == CTRL_X_PATH_DEFINES; }
private int ctrl_x_mode_dictionary(void)
    { return ctrl_x_mode == CTRL_X_DICTIONARY; }
private int ctrl_x_mode_thesaurus(void)
    { return ctrl_x_mode == CTRL_X_THESAURUS; }
private int ctrl_x_mode_cmdline(void) { 
   return ctrl_x_mode == CTRL_X_CMDLINE || ctrl_x_mode == CTRL_X_CMDLINE_CTRL_X; 
}
private int ctrl_x_mode_function(void)
    { return ctrl_x_mode == CTRL_X_FUNCTION; }
private int ctrl_x_mode_omni(void)
    { return ctrl_x_mode == CTRL_X_OMNI; }
private int ctrl_x_mode_eval(void)
    { return ctrl_x_mode == CTRL_X_EVAL; }
private int ctrl_x_mode_line_or_eval(void)
    { return ctrl_x_mode == CTRL_X_WHOLE_LINE || ctrl_x_mode == CTRL_X_EVAL; }
private int ctrl_x_mode_register(void)
    { return ctrl_x_mode == CTRL_X_REGISTER; }

// Whether other than default completion has been selected.
int
ctrl_x_mode_not_default(void) {
   return ctrl_x_mode != CTRL_X_NORMAL;
}

// Whether CTRL-X was typed without a following character, not including when in CTRL-X CTRL-V mode
int
ctrl_x_mode_not_defined_yet(void) {
   return ctrl_x_mode == CTRL_X_NOT_DEFINED_YET;
}

// Return TRUE if currently in "normal" or "adding" insert completion matches state
int
compl_status_adding(void) {
   return compl_cont_status & CONT_ADDING;
}

// Return TRUE if the completion pattern includes start of line, just for word-wise expansion
int
compl_status_sol(void) {
   return compl_cont_status & CONT_SOL;
}

// Return TRUE if ^X^P/^X^N will do a local completion (i.e. use complete=.)
int
compl_status_local(void) {
   return compl_cont_status & CONT_LOCAL;
}

// Clear the completion status flags
private void
compl_status_clear(void) {
   compl_cont_status = 0;
}

// TRUE if completion is using the forward direction matches
private int
compl_dir_forward(void) {
   return compl_direction == FORWARD;
}

// TRUE if currently showing forward completion matches
private int
compl_shows_dir_forward(void) {
   return compl_shows_dir == FORWARD;
}

// TRUE if currently showing backward completion matches
private int
compl_shows_dir_backward(void) {
   return compl_shows_dir == BACKWARD;
}

// Return TRUE if the 'dictionary' or 'thesaurus' option can be used.
private int
has_compl_option(int dict_opt) {
   if (dict_opt ? (!curBook->o.dictionary) : (!curBook->o.thesaurus && !curBook->o.thesaurusFn)) {
      ctrl_x_mode = CTRL_X_NORMAL;
      edit_submode = NULL;
      msgDeco(dict_opt ? _("'dictionary' option is empty")
              : _("'thesaurus' option is empty"), getDecoFlags(HLF_E));
      if (emsg_silent == 0 && !in_assert_fails)    {
         setcursor();
         out_flush();
         if (!get_EeglVar_nr(VV_TESTING))
            ui_delay(2004L, FALSE);
      }
      return FALSE;
   }
   return TRUE;
}

// Is the character "c" a valid key to go to or keep us in CTRL-X mode? Depends on the current mode
int
eeIsCtrlXKey(Unt c) {
   // Always allow ^R - let its results then be checked
   if (c == Ctrl_R && ctrl_x_mode != CTRL_X_REGISTER)
      return TRUE;

   // Accept <PageUp> and <PageDown> if the popup menu is visible.
   if (ins_compl_pum_key(c))
      return TRUE;

   switch (ctrl_x_mode) {
   case 0:          // Not in any CTRL-X mode
      return (c == Ctrl_N || c == Ctrl_P || c == Ctrl_X);
   case CTRL_X_NOT_DEFINED_YET:
   case CTRL_X_CMDLINE_CTRL_X:
      return (   c == Ctrl_X || c == Ctrl_Y || c == Ctrl_E
          || c == Ctrl_L || c == Ctrl_F || c == Ctrl_RSB
          || c == Ctrl_I || c == Ctrl_D || c == Ctrl_P
          || c == Ctrl_N || c == Ctrl_T || c == Ctrl_V
          || c == Ctrl_Q || c == Ctrl_U || c == Ctrl_O
          || c == Ctrl_S || c == Ctrl_K || c == 's'
          || c == Ctrl_Z || c == Ctrl_R);
   case CTRL_X_SCROLL:
      return (c == Ctrl_Y || c == Ctrl_E);
   case CTRL_X_WHOLE_LINE:
      return (c == Ctrl_L || c == Ctrl_P || c == Ctrl_N);
   case CTRL_X_FILES:
      return (c == Ctrl_F || c == Ctrl_P || c == Ctrl_N);
   case CTRL_X_DICTIONARY:
      return (c == Ctrl_K || c == Ctrl_P || c == Ctrl_N);
   case CTRL_X_THESAURUS:
      return (c == Ctrl_T || c == Ctrl_P || c == Ctrl_N);
   case CTRL_X_TAGS:
      return (c == Ctrl_RSB || c == Ctrl_P || c == Ctrl_N);
   case CTRL_X_PATH_PATTERNS:
      return (c == Ctrl_P || c == Ctrl_N);
   case CTRL_X_PATH_DEFINES:
      return (c == Ctrl_D || c == Ctrl_P || c == Ctrl_N);
   case CTRL_X_CMDLINE:
      return (c == Ctrl_V || c == Ctrl_Q || c == Ctrl_P || c == Ctrl_N || c == Ctrl_X);
   case CTRL_X_FUNCTION:
      return (c == Ctrl_U || c == Ctrl_P || c == Ctrl_N);
   case CTRL_X_OMNI:
      return (c == Ctrl_O || c == Ctrl_P || c == Ctrl_N);
   case CTRL_X_EVAL:
      return (c == Ctrl_P || c == Ctrl_N);
   case CTRL_X_REGISTER:
      return (c == Ctrl_R || c == Ctrl_P || c == Ctrl_N);
   }
   internal_error(S"eeIsCtrlXKey()");
   return FALSE;
}

// TRUE if "match" is the original text when the completion began.
private int
match_at_original_text(InsertCompletion *match) {
   return match->flags & CP_ORIGINAL_TEXT;
}

// Returns TRUE if "match" is the first match in the completion list.
private int
is_first_match(InsertCompletion *match) {
   return match == compl_first_match;
}

// TRUE when character "c" is part of the item currently being completed. Used to decide 
// whether to abandon complete mode when the menu is visible.
private int
ins_compl_accept_char(int c) {
   if (compl_autocomplete && compl_from_nonkeyword)
      return FALSE;

   if (ctrl_x_mode & CTRL_X_WANT_IDENT)
      // When expanding an identifier only accept identifier chars.
      return eeIsIdentifierChar(c);

   switch (ctrl_x_mode) {
   case CTRL_X_FILES:
      // When expanding file name only accept file name chars. But not path separators, so that
      // "proto/<Tab>" expands files in "proto", not "proto/" as a whole
      return eeIsFnameChar(c) && c != '/';

   case CTRL_X_CMDLINE:
   case CTRL_X_CMDLINE_CTRL_X:
   case CTRL_X_OMNI:
      // Command line and Omni completion can work with just about any
      // printable character, but do stop at white space.
      return bookIsCharPrintable(c) && !SPACE_OR_TAB(c);

   case CTRL_X_WHOLE_LINE:
      // For while line completion a space can be part of the line.
      return bookIsCharPrintable(c);
   }
   return eeIsWordc(c);
}

//Get the completed text by inferring the case of the originally typed text.
//If the result is in allocated memory "tofree" is set to it.
private CS
ins_compl_infercase_gettext(
   CS str,
   int char_len,
   int compl_char_len,
   int min_len,
   OUT Byte** tofree
) {
   int i, c;
   int has_lower = FALSE;
   int was_letter = FALSE;
   ArrayList   gap;

   IObuff[0] = ZERO;

   // Allocate wide character array for the completion and fill it.
   Arr(int) wideChars = ALLOC_MULT(int, char_len);

   CS p = str;
   for (i = 0; i < char_len; ++i) {
      wideChars[i] = inpAdvanceMultibyte(&p);
   }

   // Rule 1: Were any chars converted to lower?
   p = compl_orig_text.c;
   for (i = 0; i < min_len; ++i) {
      c = inpAdvanceMultibyte(&p);
      if (MB_ISLOWER(c)) {
         has_lower = TRUE;
         if (MB_ISUPPER(wideChars[i])) {
            // Rule 1 is satisfied.
            for (i = compl_char_len; i < char_len; ++i)
                wideChars[i] = MB_TOLOWER(wideChars[i]);
            break;
         }
      }
   }

   //Rule 2: No lower case, 2nd consecutive letter converted to upper case.
   if (!has_lower) {
      p = compl_orig_text.c;
      for (i = 0; i < min_len; ++i) {
         c = inpAdvanceMultibyte(&p);
         if (was_letter && MB_ISUPPER(c) && MB_ISLOWER(wideChars[i])) {
            // Rule 2 is satisfied.
            for (i = compl_char_len; i < char_len; ++i)
                wideChars[i] = MB_TOUPPER(wideChars[i]);
            break;
         }
         was_letter = MB_ISLOWER(c) || MB_ISUPPER(c);
      }
   }

   // Copy the original case of the part we typed.
   p = compl_orig_text.c;
   for (i = 0; i < min_len; ++i) {
      c = inpAdvanceMultibyte(&p);
      if (MB_ISLOWER(c))
          wideChars[i] = MB_TOLOWER(wideChars[i]);
      ei (MB_ISUPPER(c))
          wideChars[i] = MB_TOUPPER(wideChars[i]);
   }

   // Generate encoding specific output from wide character array.
   p = IObuff;
   i = 0;
   ga_init2(&gap, 1, 500);
   while (i < char_len) {
      if (gap.c != NULL) {
         if (ga_grow(&gap, 10) == FAIL) {
            ga_clear(&gap);
            return (CS)"[failed]";
         }
         p = (CS)gap.c + gap.len;
         gap.len += (*mb_char2bytes)(wideChars[i], p);
         i++;
      } ei ((p - IObuff) + 6 >= IOSIZE) {
         // Multi-byte characters can occupy up to five bytes more than ASCII characters, and we 
         // also need one byte for ZERO, so when getting to six bytes from the edge of IObuff
         // switch to using a growarray. Add the character in the next round.
         if (ga_grow(&gap, IOSIZE) == FAIL) {
            eeglFree(wideChars);
            return (CS)"[failed]";
         }
         *p = ZERO;
         STRCPY(gap.c, IObuff);
         gap.len = (int)(p - IObuff);
      } else
         p += (*mb_char2bytes)(wideChars[i], p);
      i++;
   }
   eeglFree(wideChars);

   if (gap.c) {
      *tofree = gap.c;
      return gap.c;
   }

   *p = ZERO;
   return IObuff;
}

// This is like addMatchToList(), but if 'ic' and 'inf' are set, then the case of the originally 
// typed text is used, and the case of the completed text is inferred, ie this tries to work out
// what case you probably wanted the rest of the word to be in -- webb
Unt
ins_compl_add_infercase(
   CS str_arg,
   int len,
   int icase,
   CS fname,
   Unt dir,
   int cont_s_ipos,  // next ^X<> will set initial_pos
   int score
) {
   CS str = str_arg;
   CS p;
   int char_len;      // count multi-byte characters
   int compl_char_len;
   int min_len;
   Unt flags = 0;
   CS tofree = NULL;

   if (p_ic && curBook->o.inferCase && len > 0) {
      // Infer case of completed part. Find actual length of completion.
      p = str;
      char_len = 0;
      while (*p != ZERO) {
         MB_PTR_ADV(p);
         ++char_len;
      }

      // Find actual length of original text.
      p = compl_orig_text.c;
      compl_char_len = 0;
      while (*p != ZERO) {
         MB_PTR_ADV(p);
         ++compl_char_len;
      }

      // "char_len" may be smaller than "compl_char_len" when using
      // thesaurus, only use the minimum when comparing.
      min_len = MIN(char_len, compl_char_len);
      str = ins_compl_infercase_gettext(str, char_len, compl_char_len, min_len, &tofree);
   }
   if (cont_s_ipos)
      flags |= CP_CONT_S_IPOS;
   if (icase)
      flags |= CP_ICASE;

   Unt res = addMatchToList(str, len, fname, NULL, NULL, dir, flags, false, NULL, score);
   eeglFree(tofree);
   return res;
}

// Check if ctrl_x_mode has been configured in 'completefuzzycollect'
private int
cfc_has_mode(void) {
   if (ctrl_x_mode_normal() || ctrl_x_mode_dictionary())
      return (cfc_flags & CFC_KEYWORD) != 0;
   ei (ctrl_x_mode_files())
      return (cfc_flags & CFC_FILES) != 0;
   ei (ctrl_x_mode_whole_line())
      return (cfc_flags & CFC_WHOLELINE) != 0;
   return FALSE;
}

// Returns TRUE if matches should be sorted based on proximity to the cursor.
private int
is_nearest_active(void) {
   Unt flags = curBook->o.completeOpt;
   return (compl_autocomplete || (flags & COT_NEAREST)) && !(flags & COT_FUZZY);
}

//Add a match to the list of matches. The arguments are:
//    str       - text of the match to add
//    len       - length of "str". If -1, then the length of "str" is computed.
//    fname     - file name to associate with this match.
//    cptext    - list of strings to use with this match (for abbr, menu, info and kind)
//    user_data - user supplied data (any Eegl type) for this match
//    cdir    - match direction. If 0, use "compl_direction".
//    flags_arg - match flags (flags)
//    adup    - accept this match even if it is already present.
//    *userDecos  - list of 2 extra hilite decorations for abbr kind.
//If "cdir" is FORWARD, then the match is added after the current match.
//Otherwise, it is added before the current match.
//
//If the given string is already in the list of completions, then return
//NOTDONE, otherwise add it to the list and return OK. If there is an error,
//maybe because alloc() returns NULL, then FAIL is returned.
private Unt
addMatchToList(
   CS str,
   int len,
   CS fname,
   Byte** cptext,       // extra text for popup menu or NULL
   Var* user_data UNUSED,  // "user_data" entry or NULL
   Unt cdir,
   Unt flags_arg,
   Boole adup,          // accept duplicate match
   Arr(Decoration) userDecos,           // user abbreviation/kind decorations
   int      score
) {
   InsertCompletion   *match, *current, *prev;
   Unt dir = (cdir == 0 ? compl_direction : cdir);
   Unt flags = flags_arg;
   int inserted = FALSE;

   if (flags & CP_FAST)
      fast_breakcheck();
   else
      ui_breakcheck();
   if (gotInterruptG)
      return FAIL;
   if (len < 0)
      len = (int)STRLEN(str);

   // If the same match is already present, don't add it.
   if (compl_first_match != NULL && !adup) {
      match = compl_first_match;
      do {
         if (!match_at_original_text(match)
             && STRNCMP(match->cp_str.c, str, len) == 0
             && ((int)match->cp_str.len <= len || match->cp_str.c[len] == ZERO)
          ){
            if (is_nearest_active() && score > 0 && score < match->cp_score)
               match->cp_score = score;
            return NOTDONE;
         }
         match = match->next;
      } while (match != NULL && !is_first_match(match));
   }

   // Remove any popup menu before changing the list of matches.
   ins_compl_del_pum();

   // Allocate a new match structure. Copy the values to the new match structure.
   match = ALLOC_CLEAR_ONE(InsertCompletion);
   match->cp_number = flags & CP_ORIGINAL_TEXT ? 0 : -1;
   match->cp_str.c = copySubstr(str, len);
   match->cp_str.len = len;

   // match-fname is:
   // - compl_curr_match->fName if it is a string equal to fname.
   // - a copy of fname, CP_FREE_FNAME is set to free later THE allocated mem.
   // - NULL otherwise.   --Acevedo
   if (fname
          && compl_curr_match && compl_curr_match->fName
          && STRCMP(fname, compl_curr_match->fName) == 0
   )
      match->fName = compl_curr_match->fName;
   ei (fname) {
      match->fName = copyStr(fname);
      flags |= CP_FREE_FNAME;
   } else
      match->fName = NULL;
   match->flags = flags;
   match->abbrDeco = userDecos ? userDecos[0] : EMPTY_DECO;
   match->kindDeco = userDecos ? userDecos[1] : EMPTY_DECO;
   match->cp_score = score;
   match->indexOfSourceInCpt = cpt_sources_index;

   if (cptext) {
      for (int i = 0; i < CPT_COUNT; ++i) {
         if (cptext[i] != NULL && *cptext[i] != ZERO)
            match->cp_text[i] = copyStr(cptext[i]);
      }
   }
   if (user_data)
      match->userData = *user_data;

   // Link the new match structure after (FORWARD) or before (BACKWARD) the
   // current match in the list of matches .
   if (!compl_first_match)
      match->next = match->prev = NULL;
   ei (cfc_has_mode() && score != FUZZY_SCORE_NONE && compl_get_longest) {
      current = compl_first_match->next;
      prev = compl_first_match;
      inserted = FALSE;
      // The direction is ignored when using longest and completefuzzycollect, because matches are 
      // inserted and sorted by score.
      while (current != NULL && current != compl_first_match) {
         if (current->cp_score < score) {
            match->next = current;
            match->prev = current->prev;
            if (current->prev)
               current->prev->next = match;
            current->prev = match;
            inserted = TRUE;
            break;
         }
         prev = current;
         current = current->next;
      }
      if (!inserted) {
         prev->next = match;
         match->prev = prev;
         match->next = compl_first_match;
         compl_first_match->prev = match;
      }
   } ei (dir == FORWARD) {
      match->next = compl_curr_match->next;
      match->prev = compl_curr_match;
   } else {  // BACKWARD
      match->next = compl_curr_match;
      match->prev = compl_curr_match->prev;
   }
   if (match->next)
      match->next->prev = match;
   if (match->prev)
      match->prev->next = match;
   else   // if there's nothing before, it is the first match
      compl_first_match = match;
   compl_curr_match = match;

   // Find the longest common string if still doing that.
   if (compl_get_longest && (flags & CP_ORIGINAL_TEXT) == 0 && !cfc_has_mode())
      ins_compl_longest_match(match);

   return OK;
}

// Return TRUE if "str[len]" matches with match->cp_str, considering match->flags.
private int
ins_compl_equal(InsertCompletion *match, CS str, int len) {
   if ((match->flags & CP_EQUAL) != 0)
      return TRUE;
   if ((match->flags & CP_ICASE) != 0)
      return STRNICMP(match->cp_str.c, str, (Unt)len) == 0;
   return STRNCMP(match->cp_str.c, str, (Unt)len) == 0;
}

// when len is -1 mean use whole length of p otherwise part of p
private void
ins_compl_insert_bytes(CS p, int len) {
   if (len == -1)
      len = (int)STRLEN(p);
   ins_bytes_len(p, len);
   compl_ins_end_col = curPor->cursor.col;
}

//Check if the column is within the currently inserted completion text
//column range. If it is, return a special hilite decoration. -1 means normal item.
Decoration
getDecorationIfColumnIsWithinCompletion(LineNr lnum, int col) {
   if (curBook->o.completeOpt & COT_FUZZY)
      return (Decoration){.hiId = SHORT};
   Decoration deco = decosByHiliteName((CS)"ComplMatchIns");
   if (deco.hiId == SHORT)
      return (Decoration){.hiId = SHORT};

   int start_col = compl_col + (int)ins_compl_leader_len();
   if (!ins_compl_has_multiple())
      return (col >= start_col && col < compl_ins_end_col) ? deco : (Decoration){.hiId = SHORT};

   // Multiple lines
   if ((lnum == compl_lnum && col >= start_col && col < MAXCOL) ||
         (lnum > compl_lnum && lnum < curPor->cursor.lnum) ||
         (lnum == curPor->cursor.lnum && col <= compl_ins_end_col)
   )
      return deco;

   return (Decoration){.hiId = SHORT};
}

// Return TRUE if the current completion string contains newline characters,
// indicating it's a multi-line completion.
private Boole
ins_compl_has_multiple(void) {
   return firstOccurrence(compl_shown_match->cp_str.c, '\n') != NULL;
}

//Return TRUE if the given line number falls within the range of a multi-line completion, i.e. 
//between the starting line (compl_lnum) and current cursor line. Always return FALSE for 
//single-line completions.
int
ins_compl_lnum_in_range(LineNr lnum) {
   if (!ins_compl_has_multiple())
      return FALSE;
   return lnum >= compl_lnum && lnum <= curPor->cursor.lnum;
}

// Reduce the longest common string for match "match".
private void
ins_compl_longest_match(InsertCompletion* match) {
   int c1, c2;
   int had_match;

   if (compl_leader.c == NULL) {
      // First match, use it as a whole.
      compl_leader.c = copySubstr(match->cp_str.c, match->cp_str.len);
      if (compl_leader.c == NULL)
         return;

      compl_leader.len = match->cp_str.len;
      had_match = (curPor->cursor.col > compl_col);
      ins_compl_longest_insert(compl_leader.c);

      // When the match isn't there (to avoid matching itself) remove it
      // again after redrawing.
      if (!had_match)
         ins_compl_delete();
      complUsedMatchS = false;

      return;
   }

   // Reduce the text if this match differs from compl_leader.
   CS p = compl_leader.c;
   CS s = match->cp_str.c;
   while (*p != ZERO) {
      c1 = mb_ptr2char(p);
      c2 = mb_ptr2char(s);
      if ((match->flags & CP_ICASE) ? (MB_TOLOWER(c1) != MB_TOLOWER(c2)) : (c1 != c2))
         break;
      MB_PTR_ADV(p);
      MB_PTR_ADV(s);
   }

   if (*p != ZERO) {
      //Leader was shortened, need to change the inserted text.
      *p = ZERO;
      compl_leader.len = (Unt)(p - compl_leader.c);

      had_match = (curPor->cursor.col > compl_col);
      ins_compl_longest_insert(compl_leader.c);

      //When the match isn't there (to avoid matching itself) remove it again after redrawing.
      if (!had_match)
         ins_compl_delete();
   }

   complUsedMatchS = false;
}

// Add an array of matches to the list of matches. Frees matches[].
private void
ins_compl_add_matches(OUT ExpandMatch* matches, int icase) {
   Unt add_r = OK;
   Unt dir = compl_direction;

   for (Unt i = 0; i < matches->len && add_r != FAIL; i++) {
      add_r = addMatchToList(
         matches->c[i], -1, NULL, NULL, NULL, dir, CP_FAST | (icase ? CP_ICASE : 0), false, NULL,
         FUZZY_SCORE_NONE
      );
      if (add_r == OK)
         // if dir was BACKWARD then honor it just once
         dir = FORWARD;
   }
}

//Make the completion list cyclic. Return the number of matches (excluding the original).
private int
ins_compl_make_cyclic(void) {
   InsertCompletion *match;
   int count = 0;

   if (compl_first_match == NULL)
      return 0;

   // Find the end of the list.
   match = compl_first_match;
   // there's always an entry for the compl_orig_text, it doesn't count.
   while (match->next != NULL && !is_first_match(match->next)) {
      match = match->next;
      ++count;
   }
   match->next = compl_first_match;
   compl_first_match->prev = match;

   return count;
}

// Whether there currently is a shown match.
private int
ins_compl_has_shown_match(void) {
   return compl_shown_match == NULL || compl_shown_match != compl_shown_match->next;
}

// Whether the shown match is long enough.
private int
ins_compl_long_shown_match(void) {
   return (int)compl_shown_match->cp_str.len > curPor->cursor.col - compl_col;
}

// Update the screen and when there is any scrolling remove the popup menu.
private void
ins_compl_upd_pum(void) {
   if (!displayedCompletionsS)
      return;

   Unt h = curPor->cursorLineHeight;
   // Update the screen later, before drawing the popup menu over it.
   pum_callUpdateScreen();
   if (h != curPor->cursorLineHeight)
      ins_compl_del_pum();
}

// Remove any popup menu.
private void
ins_compl_del_pum(void) {
   if (!displayedCompletionsS)
      return;

   pum_undisplay();
   EE_CLEAR(displayedCompletionsS);
}

// Return TRUE if the popup menu should be displayed.
private int
pum_wanted(void) {
   // @completeopt must contain "menu" or "menuone"
   if ((curBook->o.completeOpt & COT_ANY_MENU) == 0 && !compl_autocomplete)
      return FALSE;
   return TRUE;
}

//Return TRUE if there are two or more matches to be shown in the popup menu.
//One if 'completopt' contains "menuone".
private int
pum_enough_matches(void) {
   int i = 0;

   // Don't display the popup menu if there are no matches or there is only
   // one (ignoring the original text).
   InsertCompletion* compl = compl_first_match;
   do {
      if (compl == NULL || (!match_at_original_text(compl) && ++i == 2))
          break;
      compl = compl->next;
   } while (!is_first_match(compl));

   if ((curBook->o.completeOpt & COT_MENUONE) || compl_autocomplete)
      return (i >= 1);
   return (i >= 2);
}

// Allocate Bag for the completed item. { word, abbr, menu, kind, info }
private Bag *
ins_compl_allocBag(InsertCompletion *match) {
   Bag* dict = allocBag_lock(VAR_FIXED);

   bagAddString(dict, S"word", match->cp_str.c);
   bagAddString(dict, S"abbr", match->cp_text[CPT_ABBR]);
   bagAddString(dict, S"menu", match->cp_text[CPT_MENU]);
   bagAddString(dict, S"kind", match->cp_text[CPT_KIND]);
   bagAddString(dict, S"info", match->cp_text[CPT_INFO]);
   if (match->userData.tag == VAR_UNKNOWN)
      bagAddString(dict, S"user_data", (CS)"");
   else
      bagAddVar(dict, S"user_data", &match->userData);

   return dict;
}

// Trigger the CompleteChanged event. Invoked each time the Insert mode completion menu is changed
private void
trigger_complete_changed_event(int cur) {
   static Boole recursive = false;
   SaveVEvent save_v_event;

   if (recursive)
      return;

   Bag* item = cur < 0 ? allocBag() : ins_compl_allocBag(compl_curr_match);
   if (!item)
      return;
   Bag* v_event = get_v_event(&save_v_event);
   bagAddBag(v_event, S"completed_item", item);
   pum_set_event_info(v_event);
   bagSetItemsRo(v_event);

   recursive = true;
   textlock++;
   applyAutocomms(EVENT_COMPLETECHANGED, NULL, NULL, false, curBook);
   textlock--;
   recursive = false;

   restore_v_event(v_event, &save_v_event);
}

// Helper functions for mergesort_list().
private void*
cp_get_next(void *node) {
   return ((InsertCompletion*)node)->next;
}

private void
cp_set_next(void *node, void *next) {
   ((InsertCompletion*)node)->next = (InsertCompletion*)next;
}

private void*
cp_get_prev(void* node) {
   return ((InsertCompletion*)node)->prev;
}

private void
cp_set_prev(void* node, void* prev) {
   ((InsertCompletion*)node)->prev = (InsertCompletion*)prev;
}

private int
cp_compare_fuzzy(const void* a, const void* b) {
   int score_a = ((InsertCompletion*)a)->cp_score;
   int score_b = ((InsertCompletion*)b)->cp_score;
   return (score_b > score_a) ? 1 : (score_b < score_a) ? -1 : 0;
}

private int
cp_compare_nearest(const void* a, const void* b) {
   int score_a = ((InsertCompletion*)a)->cp_score;
   int score_b = ((InsertCompletion*)b)->cp_score;
   if (score_a == FUZZY_SCORE_NONE || score_b == FUZZY_SCORE_NONE)
      return 0;
   return (score_a > score_b) ? 1 : (score_a < score_b) ? -1 : 0;
}

// Constructs a new string by prepending text from the current line (from startcol to compl_col) to 
// the given source string. Stores the result in dest. Returns OK or FAIL.
private Unt
prepend_startcol_text(Text* dest, Text* src, int startcol) {
   int prepend_len = compl_col - startcol;
   int new_length = prepend_len + (int)src->len;

   dest->len = (Unt)new_length;
   dest->c = alloc(new_length + 1);  // +1 for ZERO
   CS line = ml_get(curPor->cursor.lnum);

   mch_memmove(dest->c, line + startcol, prepend_len);
   mch_memmove(dest->c + prepend_len, src->c, src->len);
   dest->c[new_length] = ZERO;
   return OK;
}

//Return the completion leader string adjusted for a specific source's
//startcol. If the source's startcol is before compl_col, prepends text from
//the buffer line to the original compl_leader.
private Text*
get_leader_for_startcol(InsertCompletion* match, int cached) {
   static Text adjusted_leader = {E, 0};

   if (!match) {
      EE_CLEAR_STRING(adjusted_leader);
      return NULL;
   }

   if (!cpt_sources_array || !compl_leader.c)
      goto theend;

   int   cpt_idx = match->indexOfSourceInCpt;
   if (cpt_idx < 0 || compl_col <= 0)
      goto theend;
   int startcol = cpt_sources_array[cpt_idx].startCol;

   if (startcol >= 0 && startcol < compl_col) {
      int prepend_len = compl_col - startcol;
      int new_length = prepend_len + (int)compl_leader.len;
      if (cached && (Unt)new_length == adjusted_leader.len && adjusted_leader.c != NULL)
         return &adjusted_leader;

      EE_CLEAR_STRING(adjusted_leader);
      if (prepend_startcol_text(&adjusted_leader, &compl_leader, startcol) != OK)
          goto theend;

      return &adjusted_leader;
   }
theend:
    return &compl_leader;
}

// Set fuzzy score.
private void
set_fuzzy_score(void) {
   InsertCompletion *compl;

   if (!compl_first_match || !compl_leader.c || compl_leader.len == 0)
      return;

   (void)get_leader_for_startcol(NULL, TRUE); // Clear the cache

   compl = compl_first_match;
   do {
      compl->cp_score = fuzzyMatchStr(compl->cp_str.c, get_leader_for_startcol(compl, TRUE)->c);
      compl = compl->next;
   } while (compl != NULL && !is_first_match(compl));
}

// Sort completion matches, excluding the node that contains the leader.
private void
sort_compl_match_list(int (*compare)(const void *, const void *)) {
   InsertCompletion     *compl;

   if (!compl_first_match || is_first_match(compl_first_match->next))
      return;

   compl = compl_first_match->prev;
   ins_compl_make_linear();
   if (compl_shows_dir_forward()) {
      compl_first_match->next->prev = NULL;
      compl_first_match->next = mergesort_list(
         compl_first_match->next, cp_get_next, cp_set_next, cp_get_prev, cp_set_prev, compare
      );
      compl_first_match->next->prev = compl_first_match;
   } else {
      compl->prev->next = NULL;
      compl_first_match = mergesort_list(compl_first_match, cp_get_next,
         cp_set_next, cp_get_prev, cp_set_prev, compare);
      InsertCompletion   *tail = compl_first_match;
      while (tail->next)
         tail = tail->next;
      tail->next = compl;
      compl->prev = tail;
    }
    (void)ins_compl_make_cyclic();
}

//Build a popup menu to show the completion matches.
//Return the popup menu entry that should be selected. Return -1 if nothing should be selected.
private int
ins_compl_build_pum(void) {
   InsertCompletion* compl;
   InsertCompletion* shown_compl = NULL;
   int did_find_shown_match = FALSE;
   int shown_match_ok = FALSE;
   int i = 0;
   int cur = -1;
   Unt cur_cot_flags = curBook->o.completeOpt;
   int compl_no_select = (cur_cot_flags & COT_NOSELECT) != 0 || compl_autocomplete;
   int fuzzy_filter = (cur_cot_flags & COT_FUZZY) != 0;
   InsertCompletion   *match_head = NULL;
   InsertCompletion   *match_tail = NULL;
   InsertCompletion   *matnext = NULL;
   int* match_count = NULL;
   int is_forward = compl_shows_dir_forward();
   int is_cpt_completion = (cpt_sources_array != NULL);
   Text* leader;

   //Need to build the popup menu list.
   displayedCompletionsSsize = 0;

   //If the current match is the original text don't find the first
   //match after it, don't highlight anything.
   if (match_at_original_text(compl_shown_match))
      shown_match_ok = TRUE;

   if (compl_leader.c != NULL
       && STRCMP(compl_leader.c, compl_orig_text.c) == 0
       && shown_match_ok == FALSE
   )
      compl_shown_match = compl_no_select ? compl_first_match : compl_first_match->next;

   if (is_cpt_completion) {
      match_count = ALLOC_CLEAR_MULT(int, cpt_sources_count);
      if (match_count == NULL)
         return -1;
   }

   (void)get_leader_for_startcol(NULL, TRUE); // Clear the cache

   compl = compl_first_match;
   do {
      compl->cp_in_match_array = FALSE;

      // Apply 'smartcase' behavior during normal mode
      if (ctrl_x_mode_normal() && !curBook->o.inferCase && compl_leader.c
            && !ignorecase(compl_leader.c) && !fuzzy_filter)
         compl->flags &= ~CP_ICASE;

      leader = get_leader_for_startcol(compl, TRUE);

      if (!match_at_original_text(compl)
         && (leader->c == NULL || ins_compl_equal(compl, leader->c, (int)leader->len)
             || (fuzzy_filter && compl->cp_score != FUZZY_SCORE_NONE))
      ) {
         // Limit number of items from each source if max_items is set.
         int match_limit_exceeded = FALSE;
         int cur_source = compl->indexOfSourceInCpt;
         if (is_forward && cur_source != -1 && is_cpt_completion) {
            match_count[cur_source]++;
            int max_matches = cpt_sources_array[cur_source].maxMatches;
            if (max_matches > 0 && match_count[cur_source] > max_matches)
               match_limit_exceeded = TRUE;
         }

         if (!match_limit_exceeded) {
            ++displayedCompletionsSsize;
            compl->cp_in_match_array = TRUE;
            if (match_head == NULL)
               match_head = compl;
            else
               match_tail->nextMatch = compl;
            match_tail = compl;

            if (!shown_match_ok && !fuzzy_filter) {
               if (compl == compl_shown_match || did_find_shown_match) {
                  // This item is the shown match or this is the
                  // first displayed item after the shown match.
                  compl_shown_match = compl;
                  did_find_shown_match = TRUE;
                  shown_match_ok = TRUE;
               } else
                  // Remember this displayed match for when the shown match is just below it.
                  shown_compl = compl;
               cur = i;
            } ei (fuzzy_filter) {
               if (i == 0)
                  shown_compl = compl;

               if (!shown_match_ok && compl == compl_shown_match) {
                  cur = i;
                  shown_match_ok = TRUE;
               }
            }
            i++;
         }
      }

      if (compl == compl_shown_match && !fuzzy_filter) {
         did_find_shown_match = TRUE;

         // When the original text is the shown match don't set compl_shown_match.
         if (match_at_original_text(compl))
            shown_match_ok = TRUE;

         if (!shown_match_ok && shown_compl != NULL) {
            // The shown match isn't displayed, set it to the previously displayed match.
            compl_shown_match = shown_compl;
            shown_match_ok = TRUE;
         }
      }
      compl = compl->next;
   } while (compl != NULL && !is_first_match(compl));

   eeglFree(match_count);

   if (displayedCompletionsSsize == 0)
      return -1;

   if (fuzzy_filter && !compl_no_select && !shown_match_ok) {
      compl_shown_match = shown_compl;
      shown_match_ok = TRUE;
      cur = 0;
   }

   displayedCompletionsS = ALLOC_CLEAR_MULT(PopupItem, displayedCompletionsSsize);
   if (!displayedCompletionsS)
      return -1;

   compl = match_head;
   i = 0;
   while (compl) {
      displayedCompletionsS[i].pum_text = compl->cp_text[CPT_ABBR] != NULL
                ? compl->cp_text[CPT_ABBR] : compl->cp_str.c;
      displayedCompletionsS[i].pum_kind = compl->cp_text[CPT_KIND];
      displayedCompletionsS[i].pum_info = compl->cp_text[CPT_INFO];
      displayedCompletionsS[i].pum_cpt_source_idx = compl->indexOfSourceInCpt;
      displayedCompletionsS[i].abbreviationDeco = compl->abbrDeco;
      displayedCompletionsS[i].kindDeco = compl->kindDeco;
      displayedCompletionsS[i].pum_extra = compl->cp_text[CPT_MENU] != NULL
                ? compl->cp_text[CPT_MENU] : compl->fName;
      i++; 
      matnext = compl->nextMatch;
      compl->nextMatch = NULL;
      compl = matnext;
   }

   if (!shown_match_ok)    // no displayed match at all
      cur = -1;

   return cur;
}

// Show the popup menu for the list of matches.
// Also adjusts "compl_shown_match" to an entry that is actually displayed.
void
ins_compl_show_pum(void) {
   int i;
   int cur = -1;
   ColNr   col;

   if (!pum_wanted() || !pum_enough_matches())
      return;

   // Update the screen later, before drawing the popup menu over it.
   pum_callUpdateScreen();

   if (displayedCompletionsS == NULL)
      // Need to build the popup menu list.
      cur = ins_compl_build_pum();
   else {
      // popup menu already exists, only need to find the current item.
      for (i = 0; i < displayedCompletionsSsize; ++i) {
         if (displayedCompletionsS[i].pum_text == compl_shown_match->cp_str.c
             || displayedCompletionsS[i].pum_text == compl_shown_match->cp_text[CPT_ABBR]
         ) {
            cur = i;
            break;
         }
      }
   }

   if (!displayedCompletionsS) {
      if (compl_started && has_completechanged())
         trigger_complete_changed_event(cur);
      return;
   }

   // Compute the screen column of the start of the completed text.
   // Use the cursor to get all wrapping and other settings right.
   col = curPor->cursor.col;
   curPor->cursor.col = compl_col;
   compl_selected_item = cur;
   pum_display(displayedCompletionsS, displayedCompletionsSsize, cur);
   curPor->cursor.col = col;

   // After adding leader, set the current match to shown match.
   if (compl_started && compl_curr_match != compl_shown_match)
      compl_curr_match = compl_shown_match;

   if (has_completechanged())
      trigger_complete_changed_event(cur);
}

#define DICT_FIRST   (1)  //use just first element in "dict"
#define DICT_EXACT   (2)  //"dict" is the exact name of a file

// Get current completion leader
CS
ins_compl_leader(void) {
   return compl_leader.c ? compl_leader.c : compl_orig_text.c;
}

// Get current completion leader length
private Unt
ins_compl_leader_len(void) {
   return compl_leader.c != NULL ? compl_leader.len : compl_orig_text.len;
}

//Add any identifiers that match the given pattern "pat" in the list of
//dictionary files "dict_start" to the list of completions.
private void
ins_compl_dictionaries(
   NULLABLE CS dict_start,
   CS pat,
   Unt flags,      // DICT_FIRST and/or DICT_EXACT
   int thesaurus   // Thesaurus completion
){
   if (!dict_start)
      return;
      
   CS dict = dict_start;
   CS ptr;
   RegMatch   regmatch;
   Unt dir = compl_direction;

   CS buf = alloc(LSIZE);
   regmatch.regprog = NULL;   // so that we can goto theend

   // If @infercase is set, don't use 'smartcase' here
   Boole smartCaseSaved = p_scs;
   if (curBook->o.inferCase)
      p_scs = FALSE;

   // When invoked to match whole lines for CTRL-X CTRL-L adjust the pattern
   // to only match at the start of a line.  Otherwise just match the
   // pattern. Also need to double backslashes.
   if (ctrl_x_mode_line_or_eval()) {
      CS pat_esc = copyStr_escaped(pat, (CS)"\\");
      if (!pat_esc)
         goto theend;
         
      Unt len = STRLEN(pat_esc) + 10;
      ptr = alloc(len);
      eeSnprintf(ptr, len, "^\\s*\\zs\\V%s", pat_esc);
      regmatch.regprog = compileRegexp(ptr, RE_MAGIC);
      eeglFree(pat_esc);
      eeglFree(ptr);
    } else {
      regmatch.regprog = compileRegexp(pat, RE_MAGIC);
      if (regmatch.regprog == NULL)
         goto theend;
   }

   // ignore case depends on 'ignorecase', 'smartcase' and "pat"
   regmatch.rm_ic = ignorecase(pat);
   ExpandMatch files = {};
   files.a = createArena();
   while (*dict != ZERO && !gotInterruptG && !compl_interrupted) {
      // copy one dictionary file name into buf
      if (flags == DICT_EXACT) {
          files = (ExpandMatch){.c = &dict, .len = 1};
      } else {
         // Expand wildcards in the dictionary name, but do not allow backticks
         doCutPathFromListOfPaths(OUT &dict, OUT buf, LSIZE, S",");
         if (!thesaurus && STRCMP(buf, "spell") == 0)
            files.len = 0;
         ei (firstOccurrence(buf, '`') != NULL
                || expand_wildcards(1, &buf, EW_FILE|EW_SILENT, OUT &files) != OK)
            files.len = 0;
      }

      if (files.len == 0) {
         //Complete from active spelling.  Skip "\<" in the pattern, we don't use it as a RE.
         if (pat[0] == '\\' && pat[1] == '<')
            ptr = pat + 2;
         else
            ptr = pat;
      } else  {  // avoid warning for using "files" uninit
         filterFromFiles(files, thesaurus, flags,
                (cfc_has_mode() ? NULL : &regmatch), buf, OUT &dir);
      }
      if (flags != 0)
         break;
   }
   deleteArena(files.a);

theend:
   p_scs = smartCaseSaved;
   eeRegFree(regmatch.regprog);
   eeglFree(buf);
}

//Add all the words in the line "*buf_arg" from the thesaurus file "fname"
//skipping the word at 'skip_word'.  Returns OK on success.
private Unt
thesaurus_add_words_in_line(CS fname, OUT CS* buf_arg, Unt dir, CS skip_word) {
   Unt status = OK;
   CS wstart;

   // Add the other matches on the line
   CS ptr = *buf_arg;
   while (!gotInterruptG) {
      // Find start of the next word. Skip whitespace and punctuation.
      ptr = findWordStart(ptr);
      if (*ptr == ZERO || *ptr == NL)
          break;
      wstart = ptr;

      // Find end of the word.
      // Japanese words may have characters in different classes, only separate words
      // with single-byte non-word characters.
      while (*ptr != ZERO) {
         int l = utfCharLen(ptr);

         if (l < 2 && !eeIsWordc(*ptr))
             break;
         ptr += l;
      }

      // Add the word. Skip the regexp match.
      if (wstart != skip_word) {
         status = ins_compl_add_infercase(wstart, (int)(ptr - wstart), p_ic,
             fname, dir, FALSE, FUZZY_SCORE_NONE);
         if (status == FAIL)
            break;
      }
   }

   *buf_arg = ptr;
   return status;
}

// Process "count" dictionary/thesaurus "files" and add the text matching "regmatch".
private void
filterFromFiles(
   OUT ExpandMatch files,
   int thesaurus,
   Unt flags,
   RegMatch* regmatch,
   CS buf,
   OUT Unt* dir
) {
   CS ptr;
   FILE* fp;
   Unt add_r;
   CS leader = NULL;
   int leader_len = 0;
   int in_fuzzy_collect = cfc_has_mode();
   int score = 0;
   int len = 0;
   CS line_end = NULL;

   if (in_fuzzy_collect) {
      leader = ins_compl_leader();
      leader_len = (int)ins_compl_leader_len();
   }

   for (Unt i = 0; i < files.len && !gotInterruptG && !ins_compl_interrupted(); i++) {
      fp = FOPEN(files.c[i], "r");  // open dictionary file
      if (flags != DICT_EXACT && !compl_autocomplete) {
         msg_hist_off = TRUE;   // reset in msgTruncDeco()
         eeSnprintf(IObuff, IOSIZE, _("Scanning dictionary: %s"), files.c[i]);
         (void)msgTruncDeco(IObuff, getDecoFlags(HLF_R));
      }

      if (!fp)
         continue;

      // Read dictionary file line by line. Check each line for a match.
      while (!gotInterruptG && !ins_compl_interrupted() && !eeFgets(buf, LSIZE, fp)) {
         ptr = buf;
         if (regmatch) {
            while (eeRegexec(regmatch, buf, (ColNr)(ptr - buf))) {
               ptr = regmatch->startp[0];
               ptr = ctrl_x_mode_line_or_eval() ? find_line_end(ptr) : find_word_end(ptr);
               add_r = ins_compl_add_infercase(
                     regmatch->startp[0], (int)(ptr - regmatch->startp[0]),
                     p_ic, files.c[i], *dir, FALSE, FUZZY_SCORE_NONE
               );
               if (thesaurus) {
                  // For a thesaurus, add all the words in the line
                  ptr = buf;
                  add_r = thesaurus_add_words_in_line(
                        files.c[i], OUT &ptr, *dir, regmatch->startp[0]
                  );
               }
               if (add_r == OK)
                  // if dir was BACKWARD then honor it just once
                  *dir = FORWARD;
               ei (add_r == FAIL)
                  break;
               // avoid expensive call to eeRegexec() when at end of line
               if (*ptr == '\n' || gotInterruptG)
                  break;
            }
         } ei (in_fuzzy_collect && leader_len > 0) {
            line_end = find_line_end(ptr);
            while (ptr < line_end) {
               if (fuzzyMatchStr_in_line(&ptr, leader, &len, NULL, &score)) {
                  CS end_ptr = ctrl_x_mode_line_or_eval() 
                     ? find_line_end(ptr) : find_word_end(ptr);
                  add_r = ins_compl_add_infercase(
                     ptr, (int)(end_ptr - ptr), p_ic, files.c[i], *dir, FALSE, score
                  );
                  if (add_r == FAIL)
                     break;
                  ptr = end_ptr;  // start from next word
                  if (compl_get_longest && ctrl_x_mode_normal()
                        && compl_first_match->next
                        && score == compl_first_match->next->cp_score)
                     complCountBestS++;
               }
            }
         }
         line_breakcheck();
         ins_compl_check_keys(50, false);
      }
      fclose(fp);
   }
}

// Free a completion item in the list
private void
ins_compl_item_free(InsertCompletion* match) {
   EE_CLEAR_STRING(match->cp_str);
   // several entries may use the same fname, free it just once.
   if (match->flags & CP_FREE_FNAME)
      eeglFree(match->fName);
   for (int i = 0; i < CPT_COUNT; ++i)
      eeglFree(match->cp_text[i]);
   clearVar(&match->userData);
   eeglFree(match);
}

// Free the list of completions
private void
ins_compl_free(void) {
   InsertCompletion *match;

   EE_CLEAR_STRING(compl_pattern);
   EE_CLEAR_STRING(compl_leader);

   if (compl_first_match == NULL)
      return;

   ins_compl_del_pum();
   pum_clear();

   compl_curr_match = compl_first_match;
   do {
      match = compl_curr_match;
      compl_curr_match = compl_curr_match->next;
      ins_compl_item_free(match);
   } while (compl_curr_match != NULL && !is_first_match(compl_curr_match));
   compl_first_match = compl_curr_match = NULL;
   compl_shown_match = NULL;
   compl_old_match = NULL;
}

// Reset/clear the completion state.
private void
ins_compl_clear(void){
   compl_cont_status = 0;
   compl_started = FALSE;
   compl_cfc_longest_ins = FALSE;
   compl_matches = 0;
   compl_selected_item = -1;
   compl_ins_end_col = 0;
   compl_curr_win = NULL;
   compl_curr_buf = NULL;
   EE_CLEAR_STRING(compl_pattern);
   EE_CLEAR_STRING(compl_leader);
   edit_submode_extra = NULL;
   EE_CLEAR_STRING(compl_orig_text);
   compl_enter_selects = FALSE;
   cpt_sources_clear();
   compl_autocomplete = FALSE;
   compl_from_nonkeyword = FALSE;
   complCountBestS = 0;
   // clear v:completed_item
   set_EeglVar_dict(VV_COMPLETED_ITEM, allocBag_lock(VAR_FIXED));
}

// Return TRUE when Insert completion is active.
int
ins_compl_active(void) {
   return compl_started;
}

// Return True when wp is the actual completion window
int
ins_compl_win_active(Portal *wp) {
    return ins_compl_active() && wp == compl_curr_win
   && wp->book == compl_curr_buf;
}

// Selected a match. If FALSE, the match was either edited or using the longest common string
private Boole
ins_compl_used_match(void) {
   return complUsedMatchS;
}

// Initialize get longest common string.
private void
ins_compl_init_get_longest(void) {
   compl_get_longest = FALSE;
}

// Return TRUE when insert completion is interrupted.
int
ins_compl_interrupted(void) {
   return compl_interrupted || InsertCompletionime_slice_expired;
}

// Return TRUE if the <Enter> key selects a match in the completion popup menu.
private int
ins_compl_enter_selects(void) {
   return compl_enter_selects;
}

// Return the column where the text starts that is being completed
private ColNr
ins_compl_col(void) {
   return compl_col;
}

// Return the length in bytes of the text being completed
int
ins_compl_len(void) {
   return compl_length;
}

// Return TRUE when the @completeopt "preinsert" flag is in effect, otherwise return FALSE.
private int
ins_compl_has_preinsert(void) {
   Unt cur_cot_flags = curBook->o.completeOpt;
   return (cur_cot_flags & (COT_PREINSERT | COT_FUZZY | COT_MENUONE))
      == (COT_PREINSERT | COT_MENUONE) && !compl_autocomplete;
}

// Return TRUE if the pre-insert effect is valid and the cursor is within the `compl_ins_end_col`
private int
ins_compl_preinsert_effect(void) {
   if (!ins_compl_has_preinsert())
      return FALSE;
   return curPor->cursor.col < compl_ins_end_col;
}

//Delete one character before the cursor and show the subset of the matches
//that match the word that is now before the cursor.
//Return the character to be used, ZERO if the work is done and another char
//to be got from the user.
private int
ins_compl_bs(void) {
   if (ins_compl_preinsert_effect())
      ins_compl_delete();

   CS line = ml_get_curline();
   CS p = line + curPor->cursor.col;
   MB_PTR_BACK(line, p);

   // Stop completion when the whole word was deleted. For Omni completion
   // allow the word to be deleted, we won't match everything.
   if ((int)(p - line) - (int)compl_col < 0
          || ((int)(p - line) - (int)compl_col == 0 && !ctrl_x_mode_omni())
          || ctrl_x_mode_eval()
   )
      return K_BS;

   // Deleted more than what was used to find matches or didn't finish
   // finding all matches: need to look for matches all over again.
   if (curPor->cursor.col <= compl_col + compl_length || ins_compl_need_restart())
      ins_compl_restart();

   EE_CLEAR_STRING(compl_leader);
   compl_leader.len = (Unt)((p - line) - compl_col);
   compl_leader.c = copySubstr(line + compl_col, compl_leader.len);
   if (compl_leader.c == NULL) {
      compl_leader.len = 0;
      return K_BS;
   }

   ins_compl_new_leader();
   if (compl_shown_match != NULL)
      // Make sure current match is not a hidden item.
      compl_curr_match = compl_shown_match;
   return ZERO;
}

// Return TRUE when we need to find matches again, ins_compl_restart() is to be called.
private int
ins_compl_need_restart(void) {
    // Return TRUE if we didn't complete finding matches or when the
    // 'completefunc' returned "always" in the "refresh" dictionary item.
    return compl_was_interrupted
      || ((ctrl_x_mode_function() || ctrl_x_mode_omni()) && compl_opt_refresh_always);
}

//Called after changing "compl_leader".
//Show the popup menu with a different set of matches.
//May also search for matches again if the previous search was interrupted.
private void
ins_compl_new_leader(void) {
   Unt cur_cot_flags = curBook->o.completeOpt;
   int save_cursorRow = curPor->cursorRow;
   int save_leftCol = curPor->leftCol;

   ins_compl_del_pum();
   ins_compl_delete();
   ins_compl_insert_bytes(compl_leader.c + get_compl_len(), -1);
   complUsedMatchS = false;

   if (p_acl > 0) {
      drawUpdateScreen(UPD_VALID); // Show char (deletion) immediately
      out_flush();
   }

   if (compl_started) {
      ins_compl_set_original_text(compl_leader.c, compl_leader.len);
      if (is_cfn_refresh_always())
         cpt_compl_refresh();
   } else {
      //Matches were cleared, need to search for them now.  Before drawing
      //the popup menu display the changed text before the cursor.  Set
      //"compl_restarting" to avoid that the first match is inserted.
      pum_callUpdateScreen();
      save_cursorRow = curPor->cursorRow;
      save_leftCol = curPor->leftCol;
      compl_restarting = TRUE;
      if (p_ac)
         compl_autocomplete = TRUE;
      if (ins_complete(Ctrl_N, false) == FAIL)
         compl_cont_status = 0;
      compl_restarting = FALSE;
   }

   // When @completeopt contains "fuzzy", set the cp_score and maybe sort
   if (cur_cot_flags & COT_FUZZY) {
      set_fuzzy_score();
      // Sort the matches linked list based on fuzzy score
      if (!(cur_cot_flags & COT_NOSORT)) {
         sort_compl_match_list(cp_compare_fuzzy);
         if ((cur_cot_flags & (COT_NOINSERT | COT_NOSELECT)) == COT_NOINSERT && compl_first_match) {
            compl_shown_match = compl_first_match;
            if (compl_shows_dir_forward())
                compl_shown_match = compl_first_match->next;
         }
      }
   }

   compl_enter_selects = !complUsedMatchS && compl_selected_item != -1;

   // Show the popup menu with a different set of matches.
   if (!compl_interrupted)
      show_pum(save_cursorRow, save_leftCol);

   // Don't let Enter select the original text when there is no popup menu.
   if (displayedCompletionsS == NULL)
      compl_enter_selects = FALSE;
   ei (ins_compl_has_preinsert() && compl_leader.len > 0)
      ins_compl_insert(TRUE);
}

//Return the length of the completion, from the completion start column to
//the cursor column. Making sure it never goes below zero.
private int
get_compl_len(void) {
   int off = (int)curPor->cursor.col - (int)compl_col;
   return MAX(0, off);
}

// Append one character to the match leader.  May reduce the number of matches.
private void
ins_compl_addleader(int c) {
   if (ins_compl_preinsert_effect())
      ins_compl_delete();
   if (stop_arrow() == FAIL)
      return;
      
   int cc;
   if ((cc = mb_char2len(c)) > 1) {
      Byte buf[MB_MAXBYTES + 1];

      mb_char2bytes(c, buf);
      buf[cc] = ZERO;
      opInsertCharBytes(buf, cc, false);
      if (compl_opt_refresh_always)
         AppendToRedobuff(buf);
   } else {
      insertChar(c);
      if (compl_opt_refresh_always)
         AppendCharToRedobuff(c);
   }

   // If we didn't complete finding matches we must search again.
   if (ins_compl_need_restart())
      ins_compl_restart();

   // When 'always' is set, don't reset compl_leader. While completing,
   // cursor doesn't point original position, changing compl_leader would break redo.
   if (!compl_opt_refresh_always) {
      EE_CLEAR_STRING(compl_leader);
      compl_leader.len = (Unt)(curPor->cursor.col - compl_col);
      compl_leader.c = copySubstr(ml_get_curline() + compl_col, compl_leader.len);
      if (!compl_leader.c) {
          compl_leader.len = 0;
          return;
      }

      ins_compl_new_leader();
   }
}

//Setup for finding completions again without leaving CTRL-X mode. Used when
//BS or a key was typed while still searching for matches.
private void
ins_compl_restart(void) {
   ins_compl_free();
   compl_started = FALSE;
   compl_matches = 0;
   compl_cont_status = 0;
   compl_cont_mode = 0;
   cpt_sources_clear();
   compl_autocomplete = FALSE;
   compl_from_nonkeyword = FALSE;
   complCountBestS = 0;
}

//Set the first match, the original text.
private void
ins_compl_set_original_text(CS str, Unt len) {
   // Replace the original text entry.
   // The CP_ORIGINAL_TEXT flag is either at the first item or might possibly
   // be at the last item for backward completion
   if (match_at_original_text(compl_first_match)) {  // safety check
      CS p = copySubstr(str, len);
      EE_CLEAR_STRING(compl_first_match->cp_str);
      compl_first_match->cp_str.c = p;
      compl_first_match->cp_str.len = len;
   } ei (compl_first_match->prev && match_at_original_text(compl_first_match->prev)) {
      CS p = copySubstr(str, len);
      EE_CLEAR_STRING(compl_first_match->prev->cp_str);
      compl_first_match->prev->cp_str.c = p;
      compl_first_match->prev->cp_str.len = len;
   }
}

//Append one character to the match leader.  May reduce the number of matches.
private void
ins_compl_addfrommatch(void) {
   int len = (int)curPor->cursor.col - (int)compl_col;
   int c;

   CS p = compl_shown_match->cp_str.c;
   if ((int)compl_shown_match->cp_str.len <= len)  { // the match is too short
      InsertCompletion* cp;

      // When still at the original match use the first entry that matches the leader.
      if (!match_at_original_text(compl_shown_match))
         return;

      p = NULL;
      Unt plen = 0;
      for (cp = compl_shown_match->next; cp && !is_first_match(cp); cp = cp->next) {
         if (compl_leader.c == NULL || ins_compl_equal(cp, compl_leader.c, (int)compl_leader.len)) {
            p = cp->cp_str.c;
            plen = cp->cp_str.len;
            break;
         }
      }
      if (p == NULL || (int)plen <= len)
          return;
    }
    p += len;
    c = mb_ptr2char(p);
    ins_compl_addleader(c);
}

//Set the CTRL-X completion mode based on the key "c" typed after a CTRL-X.
//Use the global variables: ctrl_x_mode, edit_submode, edit_submode_pre,
//compl_cont_mode and compl_cont_status. Return TRUE when the character is not to be inserted.
private Boole
set_ctrl_x_mode(Unt c) {
   switch (c) {
   case Ctrl_E:
   case Ctrl_Y:
      // scroll the window one line up or down
      ctrl_x_mode = CTRL_X_SCROLL;
      edit_submode = (CS)_(" (insert) Scroll (^E/^Y)");
      edit_submode_pre = NULL;
      showmode();
      break;
   case Ctrl_L:
       // complete whole line
       ctrl_x_mode = CTRL_X_WHOLE_LINE;
       break;
   case Ctrl_F:
       // complete filenames
       ctrl_x_mode = CTRL_X_FILES;
       break;
   case Ctrl_K:
       // complete words from a dictionary
       ctrl_x_mode = CTRL_X_DICTIONARY;
       break;
   case Ctrl_R:
       // When CTRL-R is followed by '=', don't trigger register completion
       // This allows expressions like <C-R>=func()<CR> to work normally
       if (vpeekc() == '=')
      break;
       ctrl_x_mode = CTRL_X_REGISTER;
       break;
   case Ctrl_T:
       // complete words from a thesaurus
       ctrl_x_mode = CTRL_X_THESAURUS;
       break;
   case Ctrl_U:
       // user defined completion
       ctrl_x_mode = CTRL_X_FUNCTION;
       break;
   case Ctrl_O:
       // omni completion
       ctrl_x_mode = CTRL_X_OMNI;
       break;
   case Ctrl_RSB:
       // complete tag names
       ctrl_x_mode = CTRL_X_TAGS;
       break;
   case Ctrl_I:
   case K_S_TAB:
       // complete keywords from included files
       ctrl_x_mode = CTRL_X_PATH_PATTERNS;
       break;
   case Ctrl_D:
       // complete definitions from included files
       ctrl_x_mode = CTRL_X_PATH_DEFINES;
       break;
   case Ctrl_V:
   case Ctrl_Q:
       // complete Eegl commands
       ctrl_x_mode = CTRL_X_CMDLINE;
       break;
   case Ctrl_Z:
       // stop completion
       ctrl_x_mode = CTRL_X_NORMAL;
       edit_submode = NULL;
       showmode();
       return true;
   case Ctrl_P:
   case Ctrl_N:
      // ^X^P means LOCAL expansion if nothing interrupted (eg we just started ^X mode, or there
      // were enough ^X's to cancel the previous mode, say ^X^F^X^X^P or ^P^X^X^X^P, see below)
      // do normal expansion when interrupting a different mode (say ^X^F^X^P or ^P^X^X^P, see 
      // below) nothing changes if interrupting mode 0, (eg, the flag doesn't change when going 
      // to ADDING mode  -- Acevedo
      if (!(compl_cont_status & CONT_INTRPT))
         compl_cont_status |= CONT_LOCAL;
      ei (compl_cont_mode != 0)
         compl_cont_status &= ~CONT_LOCAL;
      // FALLTHROUGH
   default:
      //If we have typed at least 2 ^X's... for modes != 0, we set compl_cont_status = 0 (eg, as 
      //if we had just started ^X mode). For mode 0, we set "compl_cont_mode" to an impossible
      //value, in both cases ^X^X can be used to restart the same mode (avoiding ADDING mode).
      //Undocumented feature: In a mode != 0 ^X^P and ^X^X^P start 'complete' and local ^P 
      //expansions respectively. In mode 0 an extra ^X is needed since ^X^P goes to ADDING mode
      //-- Acevedo
      if (c == Ctrl_X) {
         if (compl_cont_mode != 0)
            compl_cont_status = 0;
         else
            compl_cont_mode = CTRL_X_NOT_DEFINED_YET;
      }
      ctrl_x_mode = CTRL_X_NORMAL;
      edit_submode = NULL;
      showmode();
      break;
   }

   return false;
}

// Trigger CompleteDone event and adds relevant information to v:event
private void
trigger_complete_done_event(int mode, CS word) {
   SaveVEvent   save_v_event;
   Bag* v_event = get_v_event(&save_v_event);

   mode = mode & ~CTRL_X_WANT_IDENT;
   CS modeStr = (ctrl_x_mode_names[mode]) ? (CS)ctrl_x_mode_names[mode] : null;

   (void)bagAddString(v_event, S"complete_word", !word ? Em : word);
   (void)bagAddString(v_event, S"complete_type", modeStr ? modeStr : Em);

   bagSetItemsRo(v_event);
   ins_applyAutocomms(EVENT_COMPLETEDONE);

   restore_v_event(v_event, &save_v_event);
}

// Stop insert completion mode
private int
ins_compl_stop(Unt c, int prev_mode, int retval) {

   // Remove pre-inserted text when present.
   if (ins_compl_preinsert_effect() && ins_compl_win_active(curPor))
      ins_compl_delete();

   // Get here when we have finished typing a sequence of ^N and
   // ^P or other completion characters in CTRL-X mode.  Free up
   // memory that was used, and make sure we can redo the insert.
   if (compl_curr_match || compl_leader.c || c == Ctrl_E) {
      CS ptr = NULL;

      // If any of the original typed text has been changed, eg when ignorecase is set, we must 
      // add back-spaces to the redo buffer. We add as few as necessary to delete just the part
      // of the original text that has changed. When using the longest match, edited the match or 
      // used CTRL-E then don't use the current match.
      if (compl_curr_match != NULL && complUsedMatchS && c != Ctrl_E)
         ptr = compl_curr_match->cp_str.c;
      ins_compl_fixRedoBufForLeader(ptr);
   }

   Boole want_cindent = (can_cindent && jugIsIndentationExpressionBased());

   // When completing whole lines: fix indent for 'cindent'.
   // Otherwise, break line if it's too long.
   if (compl_cont_mode == CTRL_X_WHOLE_LINE) {
      // re-indent the current line
      if (want_cindent) {
          do_expr_indent();
          want_cindent = false;   // don't do it again
      }
   } else {
      int prev_col = curPor->cursor.col;

      // put the cursor on the last char, for 'tw' formatting
      if (prev_col > 0)
          dec_cursor();
      // only format when something was inserted
      if (!arrow_used && !needUndoS && c != Ctrl_E)
          insertchar0(ZERO, 0, -1);
      if (prev_col > 0
         && ml_get_curline()[curPor->cursor.col] != ZERO)
          inc_cursor();
    }

   // If the popup menu is displayed pressing CTRL-Y means accepting
   // the selection without inserting anything.  When
   // compl_enter_selects is set the Enter key does the same.
   CS word = NULL;
   if ((c == Ctrl_Y || (compl_enter_selects
          && (c == ENTER || c == K_KENTER || c == NL)))
       && pum_visible()
   ){
      word = copyStr(compl_shown_match->cp_str.c);
      retval = TRUE;
   }

   // CTRL-E means completion is Ended, go back to the typed text.
   // but only do this, if the Popup is still visible
   if (c == Ctrl_E) {
      CS p = NULL;
      Unt   plen = 0;

      ins_compl_delete();
      if (compl_leader.c != NULL) {
         p = compl_leader.c;
         plen = compl_leader.len;
      } ei (compl_first_match != NULL) {
         p = compl_orig_text.c;
         plen = compl_orig_text.len;
      }
      if (p) {
         int compl_len = get_compl_len();

         if ((int)plen > compl_len)
            ins_compl_insert_bytes(p + compl_len, (int)plen - compl_len);
      }
      retval = TRUE;
   }

   auto_format(FALSE, TRUE);

   //Trigger the CompleteDonePre event to give scripts a chance to
   //act upon the completion before clearing the info, and restore
   //ctrl_x_mode, so that complete_info() can be used.
   ctrl_x_mode = prev_mode;
   ins_applyAutocomms(EVENT_COMPLETEDONEPRE);

   ins_compl_free();
   compl_started = FALSE;
   compl_matches = 0;
   msgClearCommline();   // necessary for "noshowmode"
   ctrl_x_mode = CTRL_X_NORMAL;
   compl_enter_selects = FALSE;
   if (edit_submode != NULL) {
      edit_submode = NULL;
      showmode();
   }
   compl_autocomplete = FALSE;
   compl_from_nonkeyword = FALSE;
   compl_best_matches = 0;

   if (c == Ctrl_C && commPortTypeG != 0)
      // Avoid the popup menu remains displayed when leaving the command line window.
      drawUpdateScreen(0);
   // Trigger the CompleteDone event to give scripts a chance to act upon the end of completion.
   trigger_complete_done_event(prev_mode, word);
   eeglFree(word);

   return retval;
}

// Cancel completion.
private int
ins_compl_cancel(void) {
   return ins_compl_stop(' ', ctrl_x_mode, TRUE);
}

//Prepare for Insert mode completion, or stop it. Called just after typing a character in Insert 
//mode. Return TRUE when the character is not to be inserted;
private Boole
ins_compl_prep(Unt c) {
   Boole retval = false;
   int prev_mode = ctrl_x_mode;

   // Forget any previous 'special' messages if this is actually
   // a ^X mode key - bar ^R, in which case we wait to see what it gives us.
   if (c != Ctrl_R && eeIsCtrlXKey(c))
      edit_submode_extra = NULL;

   // Ignore end of mouse scroll/movement.
   if (c == K_MOUSEDOWN || c == K_MOUSEUP
          || c == K_MOUSELEFT || c == K_MOUSERIGHT || c == K_MOUSEMOVE
          || c == K_COMMAND || c == K_SCRIPT_COMMAND)
      return retval;

   // Ignore mouse events in a popup window
   if (is_mouse_key(c)) {
      // Ignore drag and release events, the position does not need to be in
      // the popup and it may have just closed.
      if (c == K_LEFTRELEASE
            || c == K_LEFTRELEASE_NM
            || c == K_MIDDLERELEASE
            || c == K_RIGHTRELEASE
            || c == K_X1RELEASE
            || c == K_X2RELEASE
            || c == K_LEFTDRAG
            || c == K_MIDDLEDRAG
            || c == K_RIGHTDRAG
            || c == K_X1DRAG
            || c == K_X2DRAG
      )
         return retval;
      if (popup_visible) {
         int row = mouseRowG;
         int col = mouseColG;
         Portal* po = mouseFindPortal(&row, &col, FIND_POPUP);

         if (po && PORTAL_IS_POPUP(po))
            return retval;
      }
   }

   if (ctrl_x_mode == CTRL_X_CMDLINE_CTRL_X && c != Ctrl_X) {
      if (c == Ctrl_V || c == Ctrl_Q || c == Ctrl_Z || ins_compl_pum_key(c)
         || !eeIsCtrlXKey(c)
      ) {
         // Not starting another completion mode.
         ctrl_x_mode = CTRL_X_CMDLINE;

         // CTRL-X CTRL-Z should stop completion without inserting anything
         if (c == Ctrl_Z)
            retval = true;
      } else {
         ctrl_x_mode = CTRL_X_CMDLINE;

         // Other CTRL-X keys first stop completion, then start another completion mode.
         ins_compl_prep(' ');
         ctrl_x_mode = CTRL_X_NOT_DEFINED_YET;
      }
   }

   // Set "compl_get_longest" when finding the first matches.
   if (ctrl_x_mode_not_defined_yet() || (ctrl_x_mode_normal() && !compl_started)) {
      compl_get_longest = (curBook->o.completeOpt & COT_LONGEST) != 0;
      complUsedMatchS = true;
   }

   if (ctrl_x_mode_not_defined_yet())
      //We have just typed CTRL-X and aren't quite sure which CTRL-X mode it will be yet.
      //Now we decide.
      retval = set_ctrl_x_mode(c);
   ei (ctrl_x_mode_not_default()) {
      // We're already in CTRL-X mode, do we stay in it?
      if (!eeIsCtrlXKey(c)) {
         ctrl_x_mode = ctrl_x_mode_scroll() ? CTRL_X_NORMAL : CTRL_X_FINISHED;
         edit_submode = NULL;
      }
      showmode();
   }

   if (compl_started || ctrl_x_mode == CTRL_X_FINISHED) {
      // Show error message from attempted keyword completion (probably 'Pattern not found') until 
      // another key is hit, then go back to showing what mode we are in.
      showmode();
      if ((ctrl_x_mode_normal() && c != Ctrl_N && c != Ctrl_P
                      && c != Ctrl_R && !ins_compl_pum_key(c)
          ) || ctrl_x_mode == CTRL_X_FINISHED
      )
         retval = ins_compl_stop(c, prev_mode, retval);
   } ei (ctrl_x_mode == CTRL_X_LOCAL_MSG)
      //Trigger the CompleteDone event to give scripts a chance to act
      //upon the (possibly failed) completion.
      trigger_complete_done_event(ctrl_x_mode, NULL);

    may_trigger_modechanged();

   // reset continue_* if we left expansion-mode, if we stay they'll be
   // (re)set properly in ins_complete()
   if (!eeIsCtrlXKey(c)) {
      compl_cont_status = 0;
      compl_cont_mode = 0;
   }

   return retval;
}

//Fix the redo buffer for the completion leader replacing some of the typed
//text. This inserts backspaces and appends the changed text.
//"ptr" is the known leader text or ZERO.
private void
ins_compl_fixRedoBufForLeader(CS ptr_arg) {
    int len = 0;
    CS p;
    CS ptr = ptr_arg;

   if (!ptr) {
      if (compl_leader.c)
         ptr = compl_leader.c;
      else
         return;  // nothing to do
   }
   if (compl_orig_text.c != NULL) {
      p = compl_orig_text.c;
      // Find length of common prefix between original text and new completion
      while (p[len] != ZERO && p[len] == ptr[len])
          len++;
      // Adjust length to not break inside a multi-byte character
      if (len > 0)
          len -= (*mb_head_off)(p, p + len);
      // Add backspace characters for each remaining character in original text
      for (p += len; *p != ZERO; MB_PTR_ADV(p))
          AppendCharToRedobuff(K_BS);
   }
   if (ptr)
      AppendToRedobuffLit(ptr + len, -1);
}

//Loop through the list of portals, loaded-books or non-loaded-books (depending on flag) 
//starting from book and looking for a non-scanned book (other than curBook).  curBook is special:
//if it is called with book=curBook then it has to be the first call for a given flag/expansion.
//Return the book to scan, if any, otherwise returns curBook -- Acevedo
private Book*
ins_compl_next_buf(Book* book, Unt flag) {
   static Portal    *wp = NULL;
   Boole skipBook;

   if (flag == 'w') {     // just portals
      if (book == curBook || !portalIsValid(wp))
         // first call for this flag/expansion or window was closed
         wp = curPor;

      while (TRUE) {
         // Move to next window (wrap to first window if at the end)
         wp = (wp->next) ? wp->next : firstPor;
         // Break if we're back at start or found an unscanned book
         if (wp == curPor || !wp->book->scanned)
            break;
      }
      book = wp->book;
   } else {
      // 'b' (just loaded books), 'u' (just non-loaded books) or 'U' (unlisted books)
      // When completing whole lines skip unloaded books.
      while (TRUE) {
         // Move to next book (wrap to first book if at the end)
         book = (book->next) ? book->next : firstBook;
         // Break if we're back at start book
         if (book == curBook)
            break;

         // Check book conditions based on flag
         if (flag == 'U')
            skipBook = book->o.bookListed;
         else
            skipBook = !book->o.bookListed || (book->mem.mfile == NULL) != (flag == 'u');

         // Break if we found a book that matches our criteria
         if (!skipBook && !book->scanned)
            break;
      }
   }
   return book;
}

//Copy a Callback struct from src to *dest, clearing any existing
//entry and allocating memory for the destination.
private Unt
copyCompletionCbs(OUT Callback* dest, Callback* src) {
   evFreeCallback(dest);

   dest = ALLOC_ONE(Callback);

   if (src->name != NULL && src->name != ZERO)
      evCopyCallback(dest, src);

   return OK;
}

//Parse the @thesaurusfunc value and set the callback function.
//Invoked when the 'thesaurusfunc' option is set. The option value can be a
//name of a function (string), or function(<name>) or funcref(<name>) or a lambda expression.
CS
did_set_thesaurusfunc(OptionChange* cha) {
   int retval;
   updateStringRef(cha);

   if (cha->setScope == SET_LOCAL)
      // buffer-local option set
      retval = optSetCallback(OUT curBook->o.thesaurusFn, cha->newVal.string);
   else {
      // global option set
      retval = optSetCallback(OUT &thesaurusCbS, cha->newVal.string);
   }

   return retval == FAIL ? e_invalid_argument : NULL;
}

//Mark the global 'completefunc' 'omnifunc' and 'thesaurusfunc' callbacks with
//"copyID" so that they are not garbage collected.
int
set_ref_in_insexpand_funcs(int copyID) {
   return memSetRefInCallback(&completeFnS, copyID)
                 || memSetRefInCallback(&omniFnS, copyID)
                 || memSetRefInCallback(&thesaurusCbS, copyID)
                 || memSetRefInCallback(&customCompleteFnS, copyID);

}

// Get the user-defined completion function name for completion "type"
private CS
get_complete_funcname(int type) {
   switch (type) {
   case CTRL_X_FUNCTION: return curBook->o.completeFn->name;
   case CTRL_X_OMNI: return curBook->o.omniFn->name;
   case CTRL_X_THESAURUS: return curBook->o.thesaurusFn->name;
   default: return Em;
   }
}

// Get the callback to use for insert mode completion.
private Callback*
get_insert_callback(int type) {
   if (type == CTRL_X_FUNCTION)
      return curBook->o.completeFn;
   if (type == CTRL_X_OMNI)
      return curBook->o.omniFn;
   // CTRL_X_THESAURUS
   return curBook->o.thesaurusFn ? curBook->o.thesaurusFn : &thesaurusCbS;
}

//Execute user defined complete function 'completefunc', 'omnifunc' or 'thesaurusfunc', and get 
//matches in "matches". "type" can be one of CTRL_X_OMNI, CTRL_X_FUNCTION, or CTRL_X_THESAURUS.
//Callback function "cb" is set if triggered by a function in the 'cpt' option; otherwise, it's null
private void
expand_by_function(int type, CS base, Callback* cb) {
   List* matchlist = NULL;
   Bag* matchdict = NULL;
   Var args[3];
   int save_State = stateG;
   int is_cfntion = (cb != NULL);

   if (!is_cfntion) {
      CS funcname = get_complete_funcname(type);
      if (*funcname == ZERO)
         return;
      cb = get_insert_callback(type);
   }

   // Call 'completefunc' to obtain the list of matches.
   args[0].tag = VAR_NUMBER;
   args[0].number = 0;
   args[1].tag = VAR_STRING;
   args[1].string = base  ? base : (CS)"";
   args[2].tag = VAR_UNKNOWN;

   Pos pos = curPor->cursor;
   //Lock the text to avoid weird things from happening. Also disallow switching to another portal, 
   //it should not be needed and may end up in Insert mode in a different book.
   ++textlock;

   Var returnVar;
   int retval = call_callback(cb, 0, OUT &returnVar, 2, args);

   // Call a function which returns a list or dict.
   if (retval == OK) {
      switch (returnVar.tag) {
      case VAR_LIST:
         matchlist = returnVar.list;
         break;
      case VAR_BAG:
         matchdict = returnVar.bag;
         break;
      case VAR_SPECIAL:
         if (returnVar.number == VVAL_NONE)
            compl_opt_suppress_empty = TRUE;
         // FALLTHROUGH
      default:
         emsg(_(e_list_or_number_required));
         clearVar(&returnVar);
         break;
      }
   }
   --textlock;

   curPor->cursor = pos;   // restore the cursor position
   check_cursor();  // make sure cursor position is valid, just in case
   validate_cursor();
   if (!EQUAL_POS(curPor->cursor, pos)) {
      emsg(_(e_complete_function_deleted_text));
      goto theend;
   }

   if (matchlist)
      ins_compl_add_list(matchlist);
   ei (matchdict)
      ins_compl_add_dict(matchdict);

theend:
   // Restore stateG, it might have been changed.
   stateG = save_State;

   if (matchdict)
      bagUnref(matchdict);
   if (matchlist)
      list_unref(matchlist);
}

private inline Decoration
getUserDecoration(CS hlname) {
   if (hlname && *hlname != ZERO)
      return decosByHiliteName(hlname);
   return EMPTY_DECO;
}

//Add a match to the list of matches from a typeval_T.
//If the given string is already in the list of completions, then return
//NOTDONE, otherwise add it to the list and return OK.  If there is an error,
//maybe because alloc() returns NULL, then FAIL is returned.
//When "fast" is TRUE use fast_breakcheck() instead of ui_breakcheck().
private int
ins_compl_add_tv(Var* tv, Unt dir, int fast) {
   CS word;
   int      dup = FALSE;
   int      empty = FALSE;
   int      flags = fast ? CP_FAST : 0;
   CS  cptext[CPT_COUNT];
   Var   user_data;
   CS user_abbr_hlname;
   CS user_kind_hlname;
   Decoration userDecos[2] = { EMPTY_DECO, EMPTY_DECO };

   user_data.tag = VAR_UNKNOWN;
   if (tv->tag == VAR_BAG && tv->bag) {
      word = bagGetString(tv->bag, tConst("word"), false);
      cptext[CPT_ABBR] = bagGetString(tv->bag, tConst("abbr"), false);
      cptext[CPT_MENU] = bagGetString(tv->bag, tConst("menu"), false);
      cptext[CPT_KIND] = bagGetString(tv->bag, tConst("kind"), false);
      cptext[CPT_INFO] = bagGetString(tv->bag, tConst("info"), false);

      user_abbr_hlname = bagGetString(tv->bag, tConst("abbr_hlgroup"), false);
      userDecos[0] = getUserDecoration(user_abbr_hlname);

      user_kind_hlname = bagGetString(tv->bag, tConst("kind_hlgroup"), false);
      userDecos[1] = getUserDecoration(user_kind_hlname);

      bagGetVar(tv->bag, tConst("user_data"), &user_data);
      if (bagGetString(tv->bag, tConst("icase"), false) != NULL 
            && bagGetNumber(tv->bag, tConst("icase"))
      )
         flags |= CP_ICASE;
      if (bagGetString(tv->bag, tConst("dup"), false) != NULL)
         dup = bagGetNumber(tv->bag, tConst("dup"));
      if (bagGetString(tv->bag, tConst("empty"), false) != NULL)
         empty = bagGetNumber(tv->bag, tConst("empty"));
      if (bagGetString(tv->bag, tConst("equal"), false) != NULL 
            && bagGetNumber(tv->bag, tConst("equal"))
      )
         flags |= CP_EQUAL;
   } else {
      word = convertVarToStringSingleUse(tv);
      CLEAR_FIELD(cptext);
   }
   if (!word || (!empty && *word == ZERO)) {
      clearVar(&user_data);
      return FAIL;
   }
   Unt status = addMatchToList(word, -1, NULL, cptext,
       &user_data, dir, flags, dup, userDecos, FUZZY_SCORE_NONE);
   if (status != OK)
      clearVar(&user_data);
   return status;
}

// Add completions from a list.
private void
ins_compl_add_list(List* list) {
   ListItem   *li;
   Unt      dir = compl_direction;

   // Go through the List with matches and add each of them.
   CHECK_LIST_MATERIALIZE(list);
   FOR_ALL_LIST_ITEMS(list, li) {
      if (ins_compl_add_tv(&li->c, dir, TRUE) == OK)
         // if dir was BACKWARD then honor it just once
         dir = FORWARD;
      ei (anyEmsgG)
         break;
   }
}

// Add completions from a dict.
private void
ins_compl_add_dict(Bag* dict) {
   // Check for optional "refresh" item.
   compl_opt_refresh_always = FALSE;
   DictItem* di_refresh = bagFind(dict, tConst("refresh"));
   if (di_refresh != NULL && di_refresh->c.tag == VAR_STRING) {
      CS v = di_refresh->c.string;
      if (v && STRCMP(v, (CS)"always") == 0)
         compl_opt_refresh_always = TRUE;
   }

   // Add completions from a "words" list.
   DictItem* di_words = bagFind(dict, tConst("words"));
   if (di_words != NULL && di_words->c.tag == VAR_LIST)
      ins_compl_add_list(di_words->c.list);
}

//Start completion for the complete() function.
//"startcol" is where the matched text starts (1 is first column). "list" is the list of matches.
private void
set_completion(ColNr startcol, List *list) {
   int save_cursorRow = curPor->cursorRow;
   int save_leftCol = curPor->leftCol;
   int flags = CP_ORIGINAL_TEXT;
   Unt cur_cot_flags = curBook->o.completeOpt;
   int compl_longest = (cur_cot_flags & COT_LONGEST) != 0;
   int compl_no_insert = (cur_cot_flags & COT_NOINSERT) != 0;
   int compl_no_select = (cur_cot_flags & COT_NOSELECT) != 0;

   // If already doing completions stop it.
   if (ctrl_x_mode_not_default())
      ins_compl_prep(' ');
   ins_compl_clear();
   ins_compl_free();
   compl_get_longest = compl_longest;

   compl_direction = FORWARD;
   if (startcol > curPor->cursor.col)
      startcol = curPor->cursor.col;
   compl_col = startcol;
   compl_lnum = curPor->cursor.lnum;
   compl_length = (int)curPor->cursor.col - (int)startcol;
   // compl_pattern doesn't need to be set
   compl_orig_text.c = copySubstr(ml_get_curline() + compl_col,
                     (Unt)compl_length);
   if (p_ic)
      flags |= CP_ICASE;
   if (compl_orig_text.c == NULL) {
      compl_orig_text.len = 0;
      return;
   }
   compl_orig_text.len = (Unt)compl_length;
   if (addMatchToList(compl_orig_text.c,
         (int)compl_orig_text.len, NULL, NULL, NULL, 0,
         flags | CP_FAST, false, NULL, FUZZY_SCORE_NONE) != OK
   )
      return;

   ctrl_x_mode = CTRL_X_EVAL;

   ins_compl_add_list(list);
   compl_matches = ins_compl_make_cyclic();
   compl_started = TRUE;
   complUsedMatchS = true;
   compl_cont_status = 0;

   compl_curr_match = compl_first_match;
   int no_select = compl_no_select || compl_longest;
   if (compl_no_insert || no_select) {
      ins_complete(K_DOWN, false);
      if (no_select)
         // Down/Up has no real effect.
         ins_complete(K_UP, false);
   } else
      ins_complete(Ctrl_N, false);
   compl_enter_selects = compl_no_insert;

   // Lazily show the popup menu, unless we got interrupted.
   if (!compl_interrupted)
      show_pum(save_cursorRow, save_leftCol);
   may_trigger_modechanged();
   out_flush();
}

void
f_complete(Arr(Var) argvars, Var* returnVar UNUSED) {
   if ((stateG & MODE_INSERT) == 0) {
      emsg(_(e_complete_can_only_be_used_in_insert_mode));
      return;
   }

   // Check for undo allowed here, because if something was already inserted
   // the line was already saved for undo and this check isn't done.
   if (!undo_allowed())
      return;

   if (confirmVarIsNonnullList(argvars, 1) != FAIL) {
      int startcol = (int)varGetNumberChk(argvars, NULL);
      if (startcol > 0)
         set_completion(startcol - 1, argvars[1].list);
   }
}

void
f_complete_add(Arr(Var) argvars, Var* returnVar) {
   returnVar->number = ins_compl_add_tv(&argvars[0], 0, FALSE);
}

void
f_complete_check(Arr(Var) argvars UNUSED, Var* returnVar) {
   int save_isRedrawingDisabledG = isRedrawingDisabledG;
   isRedrawingDisabledG = 0;

   ins_compl_check_keys(0, true);
   returnVar->number = ins_compl_interrupted();

   isRedrawingDisabledG = save_isRedrawingDisabledG;
}

// Add match item to the return list. Returns FAIL if out of memory, OK otherwise.
private Unt
add_match_to_list( Var  *returnVar, CS str, int len, int pos) {
   List* match = list_alloc();

   Unt ret;
   if ((ret = list_append_number(match, pos + 1)) == FAIL
       || (ret = list_append_string(match, str, len)) == FAIL
       || (ret = list_append_list(returnVar->list, match)) == FAIL
   ) {
      eeglFree(match);
      return FAIL;
   }

   return OK;
}

void
f_complete_match(Arr(Var) argvars, Var* returnVar) {
   LineNr lnum;
   ColNr col;
   RegMatch  regmatch;
   CS cur_end = NULL;
   int bytepos = 0;
   Byte part[MAXPATHL];
   int ret;

   allocReturnList(returnVar);

   CS ise = curBook->o.expandTriggers;

   if (argvars[0].tag == VAR_UNKNOWN) {
      lnum = curPor->cursor.lnum;
      col = curPor->cursor.col;
   } ei (argvars[1].tag == VAR_UNKNOWN) {
      emsg(_(e_invalid_argument));
      return;
   } else {
      lnum = (LineNr)tv_get_number(&argvars[0]);
      col = (ColNr)tv_get_number(&argvars[1]);
      if (lnum < 1 || lnum > curBook->mem.lineCount) {
          showErrFmtMsg(_(e_invalid_line_number_nr), lnum);
          return;
      }
      if (col < 1 || col > memGetBookLen(curBook, lnum)) {
          showErrFmtMsg(_(e_invalid_column_number_nr), col + 1);
          return;
      }
   }

   CS line = memGetLine(curBook, lnum, FALSE);
   if (!line)
      return;

   CS before_cursor = copySubstr(line, col);
   if (!before_cursor)
      return;

   if (!ise) {
      regmatch.regprog = compileRegexp((CS)"\\k\\+$", RE_MAGIC);
      if (regmatch.regprog) {
         if (eeRegexec_nl(&regmatch, before_cursor, (ColNr)0)) {
            CS trig = copySubstr(regmatch.startp[0], regmatch.endp[0] - regmatch.startp[0]);
            if (trig == NULL) {
               eeglFree(before_cursor);
               eeRegFree(regmatch.regprog);
               return;
            }

            bytepos = (int)(regmatch.startp[0] - before_cursor);
            ret = add_match_to_list(returnVar, trig, -1, bytepos);
            eeglFree(trig);
            if (ret == FAIL) {
                eeglFree(before_cursor);
                eeRegFree(regmatch.regprog);
                return;
            }
         }
         eeRegFree(regmatch.regprog);
      }
   } else {
      CS p = ise;
      CS p_space = NULL;

      cur_end = before_cursor + (int)STRLEN(before_cursor);

      while (*p != ZERO) {
         int       len = 0;
         if (p_space) {
            len = p - p_space - 1;
            memcpy(part, p_space + 1, len);
            p_space = NULL;
         } else {
            CS next_comma = firstOccurrence((*p == ',') ? p + 1 : p, ',');
            if (next_comma && *(next_comma + 1) == ' ')
               p_space = next_comma;

            len = doCutPathFromListOfPaths(OUT &p, OUT part, MAXPATHL, S",");
         }

         if (len > 0 && len <= col) {
            if (STRNCMP(cur_end - len, part, len) == 0) {
               bytepos = col - len;
               if (add_match_to_list(returnVar, part, len, bytepos) == FAIL) {
                  eeglFree(before_cursor);
                  return;
               }
            }
         }
      }
   }

   eeglFree(before_cursor);
}

// Return Insert completion mode name string
private CS
ins_compl_mode(void) {
   if (ctrl_x_mode_not_defined_yet() || ctrl_x_mode_scroll() || compl_started)
      return (CS)ctrl_x_mode_names[ctrl_x_mode & ~CTRL_X_WANT_IDENT];

   return (CS)"";
}

// Assign the sequence number to all the completion matches which don't have one assigned yet.
private void
ins_compl_update_sequence_numbers(void) {
   int      number = 0;
   InsertCompletion   *match;

   if (compl_dir_forward()) {
      // Search backwards for the first valid (!= -1) number. This should normally succeed already at
      // the first loop cycle, so it's fast!
      for (match = compl_curr_match->prev; match && !is_first_match(match); match = match->prev) {
         if (match->cp_number != -1) {
            number = match->cp_number;
            break;
         }
      } 
      if (match) {
         // go up and assign all numbers which are not assigned yet
         for (match = match->next; match != NULL && match->cp_number == -1; match = match->next)
            match->cp_number = ++number;
      } 
   } else { // BACKWARD
      // Search forwards (upwards) for the first valid (!= -1)
      // number. This should normally succeed already at the first loop cycle, so it's fast!
      for (match = compl_curr_match->next; match && !is_first_match(match); match = match->next) {
         if (match->cp_number != -1) {
            number = match->cp_number;
            break;
         }
      }
      if (match) {
         // go down and assign all numbers which are not assigned yet
         for (match = match->prev; match && match->cp_number == -1; match = match->prev)
            match->cp_number = ++number;
      }
   }
}

// Fill the dict of complete_info
private void
fill_complete_info_dict(Bag *di, InsertCompletion *match, int add_match) {
   bagAddString(di, S"word", match->cp_str.c);
   bagAddString(di, S"abbr", match->cp_text[CPT_ABBR]);
   bagAddString(di, S"menu", match->cp_text[CPT_MENU]);
   bagAddString(di, S"kind", match->cp_text[CPT_KIND]);
   bagAddString(di, S"info", match->cp_text[CPT_INFO]);
   if (add_match)
      bagAdd_bool(di, S"match", match->cp_in_match_array);
   if (match->userData.tag == VAR_UNKNOWN)
      // Add an empty string for backwards compatibility
      bagAddString(di, S"user_data", (CS)"");
   else
      bagAddVar(di, S"user_data", &match->userData);
}

// Get complete information
private void
get_complete_info(List *what_list, Bag *retdict) {
   int      ret = OK;
   ListItem   *item;
#define CI_WHAT_MODE      0x01
#define CI_WHAT_PUM_VISIBLE   0x02
#define CI_WHAT_ITEMS      0x04
#define CI_WHAT_SELECTED   0x08
#define CI_WHAT_COMPLETED   0x10
#define CI_WHAT_MATCHES      0x20
#define CI_WHAT_ALL      0xff
   int      what_flag;

   if (!what_list)
      what_flag = CI_WHAT_ALL & ~(CI_WHAT_MATCHES | CI_WHAT_COMPLETED);
   else {
      what_flag = 0;
      CHECK_LIST_MATERIALIZE(what_list);
      FOR_ALL_LIST_ITEMS(what_list, item) {
         CS what = tv_get_string(&item->c);

         if (STRCMP(what, "mode") == 0)
            what_flag |= CI_WHAT_MODE;
         ei (STRCMP(what, "pum_visible") == 0)
            what_flag |= CI_WHAT_PUM_VISIBLE;
         ei (STRCMP(what, "items") == 0)
            what_flag |= CI_WHAT_ITEMS;
         ei (STRCMP(what, "selected") == 0)
            what_flag |= CI_WHAT_SELECTED;
         ei (STRCMP(what, "completed") == 0)
            what_flag |= CI_WHAT_COMPLETED;
         ei (STRCMP(what, "matches") == 0)
            what_flag |= CI_WHAT_MATCHES;
      }
   }

   if (ret == OK && (what_flag & CI_WHAT_MODE))
      ret = bagAddString(retdict, S"mode", ins_compl_mode());

   if (ret == OK && (what_flag & CI_WHAT_PUM_VISIBLE))
      ret = bagAddNumber(retdict, S"pum_visible", pum_visible());

   if (ret == OK && (what_flag & (CI_WHAT_ITEMS | CI_WHAT_SELECTED
                | CI_WHAT_MATCHES | CI_WHAT_COMPLETED))
   ){
      List       *li = NULL;
      Bag       *di;
      InsertCompletion     *match;
      int         selected_idx = -1;
      int       has_items = what_flag & CI_WHAT_ITEMS;
      int       has_matches = what_flag & CI_WHAT_MATCHES;
      int       has_completed = what_flag & CI_WHAT_COMPLETED;

      if (has_items || has_matches) {
         li = list_alloc();
         ret = bagAddList(retdict, (has_matches && !has_items) ? S"matches" : S"items", li);
      }
      if (ret == OK && what_flag & CI_WHAT_SELECTED)
         if (compl_curr_match != NULL && compl_curr_match->cp_number == -1)
            ins_compl_update_sequence_numbers();
      if (ret == OK && compl_first_match != NULL) {
         int list_idx = 0;
         match = compl_first_match;
         do {
            if (!match_at_original_text(match)) {
               if (has_items || (has_matches && match->cp_in_match_array)) {
                  di = allocBag();
                  ret = listAppendBag(li, di);
                  if (ret != OK)
                     return;
                  fill_complete_info_dict(di, match, has_matches && has_items);
               }
               if (compl_curr_match != NULL
                   && compl_curr_match->cp_number == match->cp_number)
                  selected_idx = list_idx;
               if (!has_matches || match->cp_in_match_array)
                  list_idx++;
            }
            match = match->next;
         }
          while (match != NULL && !is_first_match(match));
      }
      if (ret == OK && (what_flag & CI_WHAT_SELECTED))
          ret = bagAddNumber(retdict, S"selected", selected_idx);

      if (ret == OK && selected_idx != -1 && has_completed) {
          di = allocBag();
          fill_complete_info_dict(di, compl_curr_match, FALSE);
          ret = bagAddBag(retdict, S"completed", di);
      }
   }
}

void
f_complete_info(Arr(Var) argvars, Var* returnVar) {
   List   *what_list = NULL;

   allocReturnDict(returnVar);

   if (argvars[0].tag != VAR_UNKNOWN) {
      if (confirmVarIsList(argvars, 0) == FAIL)
         return;
      what_list = argvars[0].list;
   }
   get_complete_info(what_list, returnVar->bag);
}

// Returns TRUE when using a user-defined function for thesaurus completion.
private int
thesaurus_func_complete(int type) {
   return type == CTRL_X_THESAURUS && (curBook->o.thesaurusFn != ZERO);
}

// Check if 'cpt' list index can be advanced to the next completion source.
private int
may_advance_cpt_index(CS cpt) {
   CS p = cpt;

   if (cpt_sources_index == -1)
      return FALSE;
   while (*p == ',' || *p == ' ') // Skip delimiters
      p++;
   return (*p != ZERO);
}

// Return value of process_next_cpt_value()
enum {
   INS_COMPL_CPT_OK = 1,
   INS_COMPL_CPT_CONT,
   INS_COMPL_CPT_END
};

//state information used for getting the next set of insert completion matches.
typedef struct {
   CS e_cpt_copy;      // copy of 'complete'
   CS e_cpt;         // current entry in "e_cpt_copy"
   Book* scannedBook;      // book being scanned
   Pos* cur_match_pos;      // current match position
   Pos prev_match_pos;      // previous match position
   int set_match_pos;      // save first_match_pos/last_match_pos
   Pos first_match_pos;   // first match position
   Pos last_match_pos;      // last match position
   int found_all;      // found all matches of a certain type.
   CS dict;         // dictionary file to search
   int dict_f;         // "dict" is an exact file name or not
   Callback* func_cb;      // callback of function in 'cpt' option
} InsertionCompletionNext;

//Process the next 'complete' option value in st->e_cpt.
//
//If successful, the arguments are set as below:
//  st->cpt - pointer to the next option value in "st->cpt"
//  InsertCompletionype_arg - type of insert mode completion to use
//  st->found_all - all matches of this type are found
//  st->scannedBook - search for completions in this buffer
//  st->first_match_pos - position of the first completion match
//  st->last_match_pos - position of the last completion match
//  st->set_match_pos - TRUE if the first match position should be saved to
//            avoid loops after the search wraps around.
//  st->dict - name of the dictionary or thesaurus file to search
//  st->dict_f - flag specifying whether "dict" is an exact file name or not
//
//Return INS_COMPL_CPT_OK if the next value is processed successfully.
//Return INS_COMPL_CPT_CONT to skip the current completion source matching
//the "st->e_cpt" option value and process the next matching source.
//Return INS_COMPL_CPT_END if all the values in "st->e_cpt" are processed.
private int
process_next_cpt_value(
   OUT InsertionCompletionNext* st,
   OUT Unt* InsertCompletionype_arg,
   Pos* start_match_pos,
   int fuzzy_collect,
   OUT int* advance_cpt_idx
){
   Unt insertCompletionType = UNT;
   int status = INS_COMPL_CPT_OK;
   int skip_source = compl_autocomplete && compl_from_nonkeyword;

   st->found_all = FALSE;
   *advance_cpt_idx = FALSE;

   while (*st->e_cpt == ',' || *st->e_cpt == ' ')
      st->e_cpt++;

   if (*st->e_cpt == '.' && !curBook->scanned && !skip_source && !InsertCompletionime_slice_expired) {
      st->scannedBook = curBook;
      st->first_match_pos = *start_match_pos;
      // Move the cursor back one character so that ^N can match the word immediately after 
      // the cursor.
      if (ctrl_x_mode_normal() && (!fuzzy_collect && dec(&st->first_match_pos) < 0)) {
          //Move the cursor to after the last character in the book, so that word at start of 
          //book is found correctly.
          st->first_match_pos.lnum = st->scannedBook->mem.lineCount;
          st->first_match_pos.col = ml_get_len(st->first_match_pos.lnum);
      }
      st->last_match_pos = st->first_match_pos;
      insertCompletionType = 0;

      // Remember the first match so that the loop stops when we
      // wrap and come back there a second time.
      st->set_match_pos = TRUE;
   } ei (!skip_source && !InsertCompletionime_slice_expired
       && firstOccurrence((CS)"buwU", *st->e_cpt) != NULL
       && (st->scannedBook = ins_compl_next_buf(st->scannedBook, *st->e_cpt)) != curBook
   ) {
      // Scan a buffer, but not the current one.
      if (st->scannedBook->mem.mfile != NULL) {  // loaded buffer
         compl_started = TRUE;
         st->first_match_pos.col = st->last_match_pos.col = 0;
         st->first_match_pos.lnum = st->scannedBook->mem.lineCount + 1;
         st->last_match_pos.lnum = 0;
         insertCompletionType = 0;
      } else {  // unloaded buffer, scan like dictionary
         st->found_all = TRUE;
         if (st->scannedBook->currFileName == NULL) {
            status = INS_COMPL_CPT_CONT;
            goto done;
         }
         insertCompletionType = CTRL_X_DICTIONARY;
         st->dict = st->scannedBook->currFileName;
         st->dict_f = DICT_EXACT;
      }
      if (!compl_autocomplete) {
         msg_hist_off = TRUE;   // reset in msgTruncDeco()
         eeSnprintf(IObuff, IOSIZE, _("Scanning: %s"),
             st->scannedBook->currFileName == NULL
            ? bookSpName(st->scannedBook)
            : st->scannedBook->shortFileName == NULL
                ? st->scannedBook->currFileName
                : st->scannedBook->shortFileName);
         (void)msgTruncDeco(IObuff, getDecoFlags(HLF_R));
      }
   } ei (*st->e_cpt == ZERO)
      status = INS_COMPL_CPT_END;
   else {
      if (ctrl_x_mode_line_or_eval())
         insertCompletionType = UNT;
      ei (*st->e_cpt == 'F' || *st->e_cpt == 'o') {
         insertCompletionType = CTRL_X_FUNCTION;
         st->func_cb = get_callback_if_cfn(st->e_cpt);
         if (!st->func_cb)
            insertCompletionType = UNT;
      } ei (!skip_source) {
         if (*st->e_cpt == 'k' || *st->e_cpt == 's') {
            if (*st->e_cpt == 'k')
               insertCompletionType = CTRL_X_DICTIONARY;
            else
               insertCompletionType = CTRL_X_THESAURUS;
            if (*++st->e_cpt != ',' && *st->e_cpt != ZERO) {
                st->dict = st->e_cpt;
                st->dict_f = DICT_FIRST;
            }
         } ei (*st->e_cpt == 'i')
            insertCompletionType = CTRL_X_PATH_PATTERNS;
         ei (*st->e_cpt == 'd')
            insertCompletionType = CTRL_X_PATH_DEFINES;
         ei (*st->e_cpt == ']' || *st->e_cpt == 't') {
            insertCompletionType = CTRL_X_TAGS;
            if (!compl_autocomplete) {
                msg_hist_off = TRUE;   // reset in msgTruncDeco()
                eeSnprintf(IObuff, IOSIZE, _("Scanning tags."));
                (void)msgTruncDeco(IObuff, getDecoFlags(HLF_R));
            }
         } else
            insertCompletionType = UNT;
      }

      // in any case e_cpt is advanced to the next entry
      (void)doCutPathFromListOfPaths(OUT &st->e_cpt, OUT IObuff, IOSIZE, S",");
      *advance_cpt_idx = may_advance_cpt_index(st->e_cpt);

      st->found_all = TRUE;
      if (insertCompletionType == UNT)
          status = INS_COMPL_CPT_CONT;
   }

done:
   *InsertCompletionype_arg = insertCompletionType;
   return status;
}

// Get the next set of identifiers or defines matching "compl_pattern" in included files.
private void
get_next_include_file_completion(Unt insertCompletionType) {
   find_pattern_in_path(
      compl_pattern.c, compl_direction,
      (int)compl_pattern.len, FALSE, FALSE,
      (insertCompletionType == CTRL_X_PATH_DEFINES && !(compl_cont_status & CONT_SOL))
       ? FIND_DEFINE : FIND_ANY, 
      1L, ACTION_EXPAND, (LineNr)1, (LineNr)MAXLNUM, FALSE, compl_autocomplete
   );
}

// Get the next set of words matching "compl_pattern" in dictionary or thesaurus files.
private void
get_next_dict_tsr_completion(int insertCompletionType, CS dict, int dict_f) {
   if (thesaurus_func_complete(insertCompletionType))
      expand_by_function(insertCompletionType, compl_pattern.c, NULL);
   else {
      ins_compl_dictionaries(
         dict 
            ? dict
            : (insertCompletionType == CTRL_X_THESAURUS ? curBook->o.thesaurus : curBook->o.dictionary),
         compl_pattern.c,
         dict ? dict_f : 0,
         insertCompletionType == CTRL_X_THESAURUS
      );
   } 
}

// Get the next set of tag names matching "compl_pattern".
private void
get_next_tag_completion(void) {
   ExpandMatch matches = {};
   matches.a = createArena();

   //set p_ic according to p_ic, p_scs and pat for find_tags().
   int save_p_ic = p_ic;
   p_ic = ignorecase(compl_pattern.c);

   //Find up to TAG_MANY matches. Avoids that an enormous number
   //of matches is found when compl_pattern is empty
   g_tag_at_cursor = TRUE;
   if (find_tags(
         compl_pattern.c,
         TAG_REGEXP | TAG_NAMES | TAG_NOIC | TAG_INS_COMP 
            | (ctrl_x_mode_not_default() ? TAG_VERBOSE : 0),
         TAG_MANY, curBook->fullFileName, OUT &matches
      ) == OK && matches.len > 0
   )
      ins_compl_add_matches(OUT &matches, p_ic);
   deleteArena(matches.a); 
   g_tag_at_cursor = FALSE;
   p_ic = save_p_ic;
}

// insert prefix with redraw
private void
ins_compl_longest_insert(CS prefix) {
   ins_compl_delete();
   ins_compl_insert_bytes(prefix + get_compl_len(), -1);
   redrawInInsertMode(FALSE);
}

//Calculate the longest common prefix among the best fuzzy matches
//stored in compl_best_matches, and insert it as the longest.
private void
fuzzy_longest_match(void) {
   int i = 0;
   int j = 0;
   CS match_str = NULL;
   CS prefix_ptr = NULL;
   CS match_ptr = NULL;
   CS leader = NULL;
   Unt leader_len = 0;
   InsertCompletion   *compl = NULL;
   int more_candidates = FALSE;

   if (complCountBestS == 0)
      return;

   InsertCompletion* nn_compl = compl_first_match->next->next;
   if (nn_compl && nn_compl != compl_first_match)
      more_candidates = TRUE;

   compl = ctrl_x_mode_whole_line() ? compl_first_match : compl_first_match->next;
   if (complCountBestS == 1) {
      // no more candidates insert the match str
      if (!more_candidates) {
          ins_compl_longest_insert(compl->cp_str.c);
          complCountBestS = 0;
      }
      complCountBestS = 0;
      return;
   }

   compl_best_matches = (InsertCompletion **)alloc(complCountBestS * sizeof(InsertCompletion *));
   while (compl != NULL && i < complCountBestS) {
      compl_best_matches[i] = compl;
      compl = compl->next;
      i++;
   }

   CS prefix = compl_best_matches[0]->cp_str.c;
   int prefix_len = (int)compl_best_matches[0]->cp_str.len;

   for (i = 1; i < complCountBestS; i++) {
      match_str = compl_best_matches[i]->cp_str.c;
      prefix_ptr = prefix;
      match_ptr = match_str;
      j = 0;

      while (j < prefix_len && *match_ptr != ZERO && *prefix_ptr != ZERO) {
         if (STRNCMP(prefix_ptr, match_ptr, utfCharLen(prefix_ptr)) != 0)
            break;

         MB_PTR_ADV(prefix_ptr);
         MB_PTR_ADV(match_ptr);
         j++;
      }

      if (j > 0)
         prefix_len = j;
   }

   leader = ins_compl_leader();
   leader_len = ins_compl_leader_len();

   // skip non-consecutive prefixes
   if (leader_len > 0 && STRNCMP(prefix, leader, leader_len) != 0)
      goto end;

   prefix = copySubstr(prefix, prefix_len);
   if (prefix) {
      ins_compl_longest_insert(prefix);
      compl_cfc_longest_ins = TRUE;
      eeglFree(prefix);
   }

end:
   eeglFree(compl_best_matches);
   compl_best_matches = NULL;
   complCountBestS = 0;
}

// Get the next set of filename matching "compl_pattern".
private void
get_next_filename_completion(void) {
   CS ptr;
   CS leader = ins_compl_leader();
   Unt   leader_len = ins_compl_leader_len();;
   int      in_fuzzy_collect = (cfc_has_mode() && leader_len > 0);
   CS last_sep = NULL;
   int need_collect_bests = in_fuzzy_collect && compl_get_longest;
   int max_score = 0;
   int current_score = 0;
   Unt dir = compl_direction;

   if (in_fuzzy_collect) {
      last_sep = lastOccurrence(leader, '/');
      if (last_sep == NULL) {
         // No path separator or separator is the last character,
         // fuzzy match the whole leader
         EE_CLEAR_STRING(compl_pattern);
         compl_pattern.c = copySubstr((CS)"*", 1);
         if (compl_pattern.c == NULL)
            return;
         compl_pattern.len = 1;
      } ei (*(last_sep + 1) == '\0')
         in_fuzzy_collect = FALSE;
      else {
         // Split leader into path and file parts
         int path_len = last_sep - leader + 1;
         CS path_with_wildcard = alloc(path_len + 2);
         eeSnprintf(path_with_wildcard, path_len + 2, "%*.*s*", path_len, path_len, leader);
         EE_CLEAR_STRING(compl_pattern);
         compl_pattern.c = path_with_wildcard;
         compl_pattern.len = path_len + 1;

         // Move leader to the file part
         leader = last_sep + 1;
         leader_len -= path_len;
      }
   }

   ExpandMatch matches = {};
   matches.a = createArena();
   if (expand_wildcards(1, &compl_pattern.c, EW_FILE|EW_DIR|EW_ADDSLASH|EW_SILENT, OUT &matches) 
         != OK) {
      deleteArena(matches.a);
      return;
   } 

   // May change home directory back to "~".
   tilde_replace(compl_pattern.c, OUT &matches);

   if (in_fuzzy_collect) {
      Fuzzy fuzzy = {};
      fuzzy.a = matches.a;

      for (Unt i = 0; i < matches.len; i++) {
         ptr = matches.c[i];
         int score = fuzzyMatchStr(ptr, leader);
         if (score != FUZZY_SCORE_NONE) {
            addFuzzyMatch((FuzzyMatch){.score = score, .str = ptr }, OUT &fuzzy);
         }
      }

      if (fuzzy.len > 0) {
         CS match = NULL;
         fuzzySortByScore(OUT &fuzzy);

         for (Unt i = 0; i < fuzzy.len; ++i) {
            match = matches.c[fuzzy.c[i].idx];
            current_score = compl_fuzzy_scores[fuzzy.c[i].idx];
            if (addMatchToList(match, -1, NULL, NULL, NULL, dir,
                  CP_FAST | ((p_wic) ? CP_ICASE : 0),
                  false, NULL, current_score) == OK
            )
               dir = FORWARD;

            if (need_collect_bests) {
               if (i == 0 || current_score == max_score) {
                  complCountBestS++;
                  max_score = current_score;
               }
            }
         }

      }

      if (complCountBestS > 0 && compl_get_longest)
         fuzzy_longest_match();
      return;
   }

   if (matches.len > 0)
      ins_compl_add_matches(OUT &matches, p_wic);
   deleteArena(matches.a); 
}

// Get the next set of command-line completions matching "compl_pattern".
private void
get_next_cmdline_completion(void) {
   ExpandMatch matches = {};
   if (expandCommline(&compl_xp, compl_pattern.c,
      (int)compl_pattern.len, OUT &matches) == EXPAND_OK
   )
      ins_compl_add_matches(OUT &matches, FALSE);
}

//Return the next word or line from buffer "scannedBook" at position
//"cur_match_pos" for completion. The length of the match is set in "len".
private CS
ins_compl_get_next_word_or_line(
   Book* scannedBook,      // buffer being scanned
   Pos* cur_match_pos,      // current match position
   int* match_len,
   int* cont_s_ipos
) {      // next ^X<> will set initial_pos

   *match_len = 0;
   CS ptr = memGetLine(scannedBook, cur_match_pos->lnum, FALSE) + cur_match_pos->col;
   int len = (int)memGetBookLen(scannedBook, cur_match_pos->lnum) - cur_match_pos->col;
   if (ctrl_x_mode_line_or_eval()) {
      if (compl_status_adding()) {
         if (cur_match_pos->lnum >= scannedBook->mem.lineCount)
            return NULL;
         ptr = memGetLine(scannedBook, cur_match_pos->lnum + 1, FALSE);
         len = memGetBookLen(scannedBook, cur_match_pos->lnum + 1);
         CS tmp_ptr = ptr;

         ptr = skipwhite(tmp_ptr);
         len -= (int)(ptr - tmp_ptr);
      }
   } else {
      CS tmp_ptr = ptr;

      if (compl_status_adding() && compl_length <= len) {
         tmp_ptr += compl_length;
         // Skip if already inside a word.
         if (eeIsWordPtr(tmp_ptr))
            return NULL;
         // Find start of next word.
         tmp_ptr = findWordStart(tmp_ptr);
      }
      // Find end of this word.
      tmp_ptr = find_word_end(tmp_ptr);
      len = (int)(tmp_ptr - ptr);

      if (compl_status_adding() && len == compl_length) {
         if (cur_match_pos->lnum < scannedBook->mem.lineCount) {
            //Try next line, if any. the new word will be "join" as if the normal command "J" was 
            //used. IOSIZE is always greater than compl_length, so the next STRNCPY always
            //works -- Acevedo
            STRNCPY(IObuff, ptr, len);
            ptr = memGetLine(scannedBook, cur_match_pos->lnum + 1, FALSE);
            tmp_ptr = ptr = skipwhite(ptr);
            // Find start of next word.
            tmp_ptr = findWordStart(tmp_ptr);
            // Find end of next word.
            tmp_ptr = find_word_end(tmp_ptr);
            if (tmp_ptr > ptr) {
               if (*ptr != ')' && IObuff[len - 1] != TAB) {
                  if (IObuff[len - 1] != ' ')
                     IObuff[len++] = ' ';
                  // IObuf =~ "\k.* ", thus len >= 2
               }
               // copy as much as possible of the new word
               if (tmp_ptr - ptr >= IOSIZE - len)
                  tmp_ptr = ptr + IOSIZE - len - 1;
               STRNCPY(IObuff + len, ptr, tmp_ptr - ptr);
               len += (int)(tmp_ptr - ptr);
               *cont_s_ipos = TRUE;
            }
            IObuff[len] = ZERO;
            ptr = IObuff;
         }
         if (len == compl_length)
            return NULL;
      }
   }

   *match_len = len;
   return ptr;
}

//Get the next set of words matching "compl_pattern" for default completion(s)
//(normal ^P/^N and ^X^L).
//Search for "compl_pattern" in the buffer "st->scannedBook" starting from the
//position "st->start_pos" in the "compl_direction" direction. If
//"st->set_match_pos" is TRUE, then set the "st->first_match_pos" and "st->last_match_pos".
//Return OK if a new next match is found, otherwise returns FAIL.
private Unt
get_next_default_completion(InsertionCompletionNext* st, Pos* start_pos) {
   Unt found_new_match = FAIL;
   int looped_around = FALSE;
   CS ptr = NULL;
   int len = 0;
   int in_fuzzy_collect = (cfc_has_mode() && compl_length > 0)
      || ((curBook->o.completeOpt & COT_FUZZY) && compl_autocomplete);
   CS leader = ins_compl_leader();
   int score = FUZZY_SCORE_NONE;
   Boole inCurBook = st->scannedBook == curBook;

   // If 'infercase' is set, don't use 'smartcase' here
   Boole smartCaseSaved = p_scs;
   if (st->scannedBook->o.inferCase)
      p_scs = FALSE;

   //Buffers other than curBook are scanned from the beginning or the end but never from the 
   //middle, thus setting nowrapscan in this buffer is a good idea, on the other hand, we always set
   //wrapscan for curBook to avoid missing matches -- Acevedo,Webb
   if (!inCurBook)
      wrapSearchG = false;
   ei (*st->e_cpt == '.')
      wrapSearchG = true;
   looped_around = FALSE;
   for (;;) {
      int   cont_s_ipos = FALSE;
      ++msg_silent;  // Don't want messages for wrapscan.

      if (in_fuzzy_collect) {
         found_new_match = search_for_fuzzy_match(
            st->scannedBook, st->cur_match_pos, leader, compl_direction, start_pos, OUT &len, &ptr,
            &score
         );
      }
      //ctrl_x_mode_line_or_eval() || word-wise search that
      //has added a word that was at the beginning of the line
      ei (ctrl_x_mode_whole_line() || ctrl_x_mode_eval() || (compl_cont_status & CONT_SOL))
         found_new_match = search_for_exact_line(st->scannedBook,
                st->cur_match_pos, compl_direction, compl_pattern.c);
      else
         found_new_match = searchit(NULL, st->scannedBook, st->cur_match_pos,
               NULL, compl_direction, compl_pattern.c, (int)compl_pattern.len,
               1L, SEARCH_KEEP + SEARCH_NFMSG, RE_LAST, NULL);
      --msg_silent;
      if (!compl_started || st->set_match_pos) {
         // set "compl_started" even on fail
         compl_started = TRUE;
         st->first_match_pos = *st->cur_match_pos;
         st->last_match_pos = *st->cur_match_pos;
         st->set_match_pos = FALSE;
      } ei (st->first_match_pos.lnum == st->last_match_pos.lnum
         && st->first_match_pos.col == st->last_match_pos.col
      ){
         found_new_match = FAIL;
      } ei (compl_dir_forward()
         && (st->prev_match_pos.lnum > st->cur_match_pos->lnum
             || (st->prev_match_pos.lnum == st->cur_match_pos->lnum
            && st->prev_match_pos.col >= st->cur_match_pos->col)))
      {
         if (looped_around)
            found_new_match = FAIL;
         else
            looped_around = TRUE;
      } ei (!compl_dir_forward()
         && (st->prev_match_pos.lnum < st->cur_match_pos->lnum
             || (st->prev_match_pos.lnum == st->cur_match_pos->lnum
            && st->prev_match_pos.col <= st->cur_match_pos->col)))
      {
         if (looped_around)
            found_new_match = FAIL;
         else
            looped_around = TRUE;
      }
      st->prev_match_pos = *st->cur_match_pos;
      if (found_new_match == FAIL)
         break;

      // when ADDING, the text before the cursor matches, skip it
      if (compl_status_adding() && inCurBook
            && start_pos->lnum == st->cur_match_pos->lnum
            && start_pos->col  == st->cur_match_pos->col)
         continue;

      if (!in_fuzzy_collect)
         ptr = ins_compl_get_next_word_or_line(st->scannedBook, st->cur_match_pos, &len, &cont_s_ipos);
      if (!ptr || (ins_compl_has_preinsert() && STRCMP(ptr, compl_pattern.c) == 0))
         continue;

      if (is_nearest_active() && inCurBook) {
         score = st->cur_match_pos->lnum - curPor->cursor.lnum;
         if (score < 0)
            score = -score;
      }

      if (ins_compl_add_infercase(ptr, len, p_ic,
            inCurBook ? NULL : st->scannedBook->shortFileName,
            0, cont_s_ipos, score) != NOTDONE)
      {
         if (in_fuzzy_collect && score == compl_first_match->next->cp_score)
            complCountBestS++;
         found_new_match = OK;
         break;
      }
   }
   p_scs = smartCaseSaved;
   wrapSearchG = true;

   return found_new_match;
}

//Return the callback function associated with "p" if it refers to a user-defined function in the 
//'complete' option. The "idx" parameter is used for indexing callback entries.
private Callback *
get_callback_if_cfn(CS p) {
   if (*p == 'o')
      return curBook->o.omniFn;

   if (*p == 'F') {
      if (*++p != ',' && *p != ZERO) {
          // Custom completion function 'F{func}' case
          return curBook->o.completeFn->name != NULL ? curBook->o.completeFn : NULL;
      } else
          return curBook->o.completeFn; // @completefunc
   }

   return NULL;
}

//Get completion matches from register contents.
//Extract words from all available registers and adds them to the completion list.
private void
get_register_completion(void) {
   Unt dir = compl_direction;
   YankReg* reg = NULL;
   void* reg_ptr = NULL;
   int adding_mode = compl_status_adding();

   for (int i = 0; i < NUM_REGISTERS; i++) {
      int regname = 0;
      if (i == 0)
         regname = '"';    // unnamed register
      ei (i < 10)
         regname = '0' + i;
      ei (i == DELETION_REGISTER)
         regname = '-';
      ei (i == STAR_REGISTER)
         regname = '*';
      ei (i == PLUS_REGISTER)
         regname = '+';
      else
         regname = 'a' + i - 10;

      // Skip invalid or black hole register
      if (!valid_yank_reg(regname, FALSE) || regname == '_')
         continue;

      reg_ptr = get_register(regname, FALSE);
      if (reg_ptr == NULL)
         continue;

      reg = (YankReg *)reg_ptr;

      for (int j = 0; j < reg->y_size; j++) {
         CS str = reg->y_array[j].c;
         if (!str)
            continue;

         if (adding_mode) {
            int str_len = (int)STRLEN(str);
            if (str_len == 0)
                continue;

            if (!compl_orig_text.c
               || (p_ic ? STRNICMP(str, compl_orig_text.c, compl_orig_text.len) == 0
                  : STRNCMP(str, compl_orig_text.c, compl_orig_text.len) == 0)
            ){
               if (ins_compl_add_infercase(str, str_len, p_ic, NULL,
                     dir, FALSE, FUZZY_SCORE_NONE) == OK)
                  dir = FORWARD;
            }
         } else {
            // Calculate the safe end of string to avoid null byte issues
            CS str_end = str + STRLEN(str);
            CS p = str;

            // Safely iterate through the string
            while (p < str_end && *p != ZERO) {
               CS old_p = p;
               p = findWordStart(p);
               if (p >= str_end || *p == ZERO)
                  break;

               CS word_end = find_word_end(p);

               if (word_end <= p) {
                  word_end = p + utfCharLen(p);
               }

               if (word_end > str_end)
                  word_end = str_end;

               int len = (int)(word_end - p);
               if (len > 0 && (!compl_orig_text.c
                  || (p_ic ? STRNICMP(p, compl_orig_text.c,
                            compl_orig_text.len) == 0
                     : STRNCMP(p, compl_orig_text.c,
                            compl_orig_text.len) == 0))
               ) {
                  if (ins_compl_add_infercase(p, len, p_ic, NULL,
                         dir, FALSE, FUZZY_SCORE_NONE) == OK)
                      dir = FORWARD;
               }

               p = word_end;

               if (p <= old_p) {
                  p = old_p + 1;
                  if (p < str_end)
                     p = old_p + utfCharLen(old_p);
               }
            }
         }
      }

      // Free the register copy
      put_register(regname, reg_ptr);
   }
}

//get the next set of completion matches for "type". TRUE if a new match is found. otherwise FALSE
private Unt
get_next_completion_match(int type, InsertionCompletionNext *st, Pos *ini) {
   Unt found_new_match = FAIL;

   switch (type) {
   case -1:
       break;
   case CTRL_X_PATH_PATTERNS:
   case CTRL_X_PATH_DEFINES:
       get_next_include_file_completion(type);
       break;

   case CTRL_X_DICTIONARY:
   case CTRL_X_THESAURUS:
       get_next_dict_tsr_completion(type, st->dict, st->dict_f);
       st->dict = NULL;
       break;

   case CTRL_X_TAGS:
       get_next_tag_completion();
       break;

   case CTRL_X_FILES:
       get_next_filename_completion();
       break;

   case CTRL_X_CMDLINE:
   case CTRL_X_CMDLINE_CTRL_X:
       get_next_cmdline_completion();
       break;

   case CTRL_X_FUNCTION:
       if (ctrl_x_mode_normal())  // Invoked by a func in 'cpt' option
      get_cfn_completion_matches(st->func_cb);
       else
      expand_by_function(type, compl_pattern.c, NULL);
       break;
   case CTRL_X_OMNI:
       expand_by_function(type, compl_pattern.c, NULL);
       break;

   case CTRL_X_REGISTER:
       get_register_completion();
       break;

   default:   // normal ^P/^N and ^X^L
      found_new_match = get_next_default_completion(st, ini);
      if (found_new_match == FAIL && st->scannedBook == curBook)
         st->found_all = TRUE;
   }

   // check if compl_curr_match has changed, (e.g. other type of expansion added something)
   if (type != 0 && compl_curr_match != compl_old_match)
      found_new_match = OK;

   return found_new_match;
}

// Strip carets followed by numbers. This suffix typically represents the max_matches setting
private void
strip_caret_numbers_in_place(CS str) {
   if (!str)
      return;

   CS read = str;
   CS write = str;
   CS p;
   while (*read) {
      if (*read == '^') {
         p = read + 1;
         while (eeIsDigit(*p))
            p++;
         if ((*p == ',' || *p == '\0') && p != read + 1) {
            read = p;
            continue;
         } else
            *write++ = *read++;
      } else
         *write++ = *read++;
   }
   *write = '\0';
}

// Call functions specified in the 'cpt' option with findstart=1, and retrieve the startcol.
private int
prepare_cpt_compl_funcs(void) {
   Callback* cb = NULL;
   int idx = 0;
   int startcol;

   // Make a copy of 'cpt' in case the buffer gets wiped out
   CS cpt = copyStr(curBook->o.complete);
   strip_caret_numbers_in_place(cpt);

   for (CS p = cpt; *p;) {
      while (*p == ',' || *p == ' ') // Skip delimiters
          p++;
      if (*p == ZERO)
          break;

      cb = get_callback_if_cfn(p);
      if (cb) {
         if (get_userdefined_compl_info(curPor->cursor.col, cb, &startcol) == FAIL) {
            if (startcol == -3)
               cpt_sources_array[idx].refreshAlways = FALSE;
            else
               startcol = -2;
         } ei (startcol < 0 || startcol > curPor->cursor.col)
            startcol = curPor->cursor.col;
         cpt_sources_array[idx].startCol = startcol;
      } else
         cpt_sources_array[idx].startCol = -3;

      (void)doCutPathFromListOfPaths(OUT &p, OUT IObuff, IOSIZE, S","); // Advance p
      idx++;
   }

   eeglFree(cpt);
   return OK;
   return FAIL;
}

// Start the timer for the current completion source.
private void
compl_source_start_timer(int source_idx UNUSED) {
   if (compl_autocomplete && cpt_sources_array != NULL) {
      ELAPSED_INIT(cpt_sources_array[source_idx].matchCollectionStart);
      InsertCompletionime_slice_expired = FALSE;
   }
}

// Safely advance the cpt_sources_index by one.
private int
advance_cpt_sources_index_safe(void) {
   if (cpt_sources_index >= 0 && cpt_sources_index < cpt_sources_count - 1) {
      cpt_sources_index++;
      return OK;
   }
   showErrFmtMsg(_(e_list_index_out_of_range_nr), cpt_sources_index);
   return FAIL;
}

#define COMPL_FUNC_TIMEOUT_MS      300
#define COMPL_FUNC_TIMEOUT_NON_KW_MS   1000
//Get the next expansion(s), using "compl_pattern".
//The search starts at position "ini" in curBook and in the direction compl_direction.
//When "compl_started" is FALSE start at that position, otherwise continue
//where we stopped searching before. This may return before finding all the matches.
//Return the total number of matches or -1 if still unknown -- Acevedo
private int
ins_compl_get_exp(Pos* ini) {
   static InsertionCompletionNext   st;
   static int             st_cleared = FALSE;
   int match_count;
   Unt found_new_match;
   Unt type = ctrl_x_mode;
   int may_advance_cpt_idx = FALSE;
   Pos start_pos = *ini;

   if (!compl_started) {
      Book* book;

      FOR_ALL_BOOKS(book)
         book->scanned = 0;
      if (!st_cleared) {
         CLEAR_FIELD(st);
         st_cleared = TRUE;
      }
      st.found_all = FALSE;
      st.scannedBook = curBook;
      eeglFree(st.e_cpt_copy);
      // Make a copy of 'complete', in case the buffer is wiped out.
      st.e_cpt_copy = copyStr((compl_cont_status & CONT_LOCAL) ? S"." : curBook->o.complete);
      strip_caret_numbers_in_place(st.e_cpt_copy);
      st.e_cpt = st.e_cpt_copy == NULL ? (CS)"" : st.e_cpt_copy;

      // In large buffers, timeout may miss nearby matches — search above cursor
#define LOOKBACK_LINE_COUNT   1000
      if (compl_autocomplete && is_nearest_active()) {
          start_pos.lnum = MAX(1, start_pos.lnum - LOOKBACK_LINE_COUNT);
          start_pos.col = 0;
      }
      st.last_match_pos = st.first_match_pos = start_pos;
   } ei (st.scannedBook != curBook && !bookIsValid(st.scannedBook))
      st.scannedBook = curBook;  // In case the buffer was wiped out.

   compl_old_match = compl_curr_match;   // remember the last current match
   st.cur_match_pos = (compl_dir_forward()) ? &st.last_match_pos : &st.first_match_pos;

   if (cpt_sources_array != NULL && ctrl_x_mode_normal()
       && !ctrl_x_mode_line_or_eval()
       && !(compl_cont_status & CONT_LOCAL)
   ){
      cpt_sources_index = 0;
      if (compl_autocomplete) {
         compl_source_start_timer(0);
         InsertCompletionimeout_ms = COMPL_INITIAL_TIMEOUT_MS;
      }
   }

   // For ^N/^P loop over all the flags/windows/buffers in 'complete'.
   for (;;) {
      found_new_match = FAIL;
      st.set_match_pos = FALSE;

      // For ^N/^P pick a new entry from e_cpt if compl_started is off,
      // or if found_all says this entry is done.  For ^X^L only use the
      // entries from 'complete' that look in loaded buffers.
      if ((ctrl_x_mode_normal() || ctrl_x_mode_line_or_eval())
                  && (!compl_started || st.found_all))
      {
         int status = process_next_cpt_value(OUT &st, OUT &type, &start_pos,
             cfc_has_mode(), OUT &may_advance_cpt_idx);

         if (status == INS_COMPL_CPT_END)
            break;
         if (status == INS_COMPL_CPT_CONT) {
            if (may_advance_cpt_idx) {
               if (!advance_cpt_sources_index_safe())
                  break;
               compl_source_start_timer(cpt_sources_index);
            }
            continue;
         }
      }

      if (compl_autocomplete && type == CTRL_X_FUNCTION)
         // LSP servers may sporadically take >1s to respond (e.g., while loading modules), but 
         // other sources might already have matches. To show results quickly use a short timeout
         // for keyword completion. Allow longer timeout for non-keyword completion
         // where only function based sources (e.g. LSP) are active.
         InsertCompletionimeout_ms = compl_from_nonkeyword
         ? COMPL_FUNC_TIMEOUT_NON_KW_MS : COMPL_FUNC_TIMEOUT_MS;

      // get the next set of completion matches
      found_new_match = get_next_completion_match(type, &st, &start_pos);

      // If complete() was called then compl_pattern has been reset.  The
      // following won't work then, bail out.
      if (compl_pattern.c == NULL)
         break;

      if (may_advance_cpt_idx) {
         if (!advance_cpt_sources_index_safe())
            break;
         compl_source_start_timer(cpt_sources_index);
      }

      // break the loop for specialized modes (use 'complete' just for the
      // generic ctrl_x_mode == CTRL_X_NORMAL) or when we've found a new match
      if ((ctrl_x_mode_not_default() && !ctrl_x_mode_line_or_eval()) || found_new_match != FAIL) {
         if (gotInterruptG)
            break;
         // Fill the popup menu as soon as possible.
         if (type != UNT)
            ins_compl_check_keys(0, false);

         if ((ctrl_x_mode_not_default() && !ctrl_x_mode_line_or_eval()) || compl_interrupted)
            break;
         compl_started = InsertCompletionime_slice_expired ? FALSE : TRUE;
      } else {
         // Mark a buffer scanned when it has been scanned completely
         if (bookIsValid(st.scannedBook) && (type == 0 || type == CTRL_X_PATH_PATTERNS))
            st.scannedBook->scanned = TRUE;

         compl_started = FALSE;
      }

      // Reset the timeout after collecting matches from function source
      if (compl_autocomplete && type == CTRL_X_FUNCTION)
          InsertCompletionimeout_ms = COMPL_INITIAL_TIMEOUT_MS;

      // For `^P` completion, reset `compl_curr_match` to the head to avoid
      // mixing matches from different sources.
      if (!compl_dir_forward()) {
         while (compl_curr_match->prev && !match_at_original_text(compl_curr_match->prev))
            compl_curr_match = compl_curr_match->prev;
      } 
   }
   cpt_sources_index = -1;
   compl_started = TRUE;

   if ((ctrl_x_mode_normal() || ctrl_x_mode_line_or_eval()) && *st.e_cpt == ZERO)
      found_new_match = FAIL;      // Got to end of @complete

   match_count = -1;      // total of matches, unknown
   if (found_new_match == FAIL || (ctrl_x_mode_not_default() && !ctrl_x_mode_line_or_eval()))
      match_count = ins_compl_make_cyclic();

   if (cfc_has_mode() && compl_get_longest && complCountBestS > 0)
      fuzzy_longest_match();

   if (compl_old_match) {
      // If several matches were added (FORWARD) or the search failed and has
      // just been made cyclic then we have to move compl_curr_match to the
      // next or previous entry (if any) -- Acevedo
      compl_curr_match = compl_dir_forward() ? compl_old_match->next : compl_old_match->prev;
      if (compl_curr_match == NULL)
          compl_curr_match = compl_old_match;
   }
   may_trigger_modechanged();

   if (is_nearest_active())
      sort_compl_match_list(cp_compare_nearest);

   return match_count;
}

//Update "compl_shown_match" to the actually shown match, it may differ when
//"compl_leader" is used to omit some of the matches.
private void
ins_compl_update_shown_match(void) {
   (void)get_leader_for_startcol(NULL, TRUE); // Clear the cache
   Text* leader = get_leader_for_startcol(compl_shown_match, TRUE);

   while (!ins_compl_equal(compl_shown_match,
      leader->c, (int)leader->len)
       && compl_shown_match->next != NULL
       && !is_first_match(compl_shown_match->next)
   ){
      compl_shown_match = compl_shown_match->next;
      leader = get_leader_for_startcol(compl_shown_match, TRUE);
   }

   // If we didn't find it searching forward, and compl_shows_dir is
   // backward, find the last match.
   if (compl_shows_dir_backward()
       && !ins_compl_equal(compl_shown_match, leader->c, (int)leader->len)
       && (compl_shown_match->next == NULL || is_first_match(compl_shown_match->next))
   ) {
      while (!ins_compl_equal(compl_shown_match, leader->c, (int)leader->len)
            && compl_shown_match->prev != NULL
            && !is_first_match(compl_shown_match->prev)
      ) {
         compl_shown_match = compl_shown_match->prev;
         leader = get_leader_for_startcol(compl_shown_match, TRUE);
      }
   }
}

// Delete the old text being completed.
private void
ins_compl_delete(void) {
   // In insert mode: Delete the typed part.
   // In replace mode: Put the old characters back, if any.
   int col = compl_col + (compl_status_adding() ? compl_length : 0);
   Text   remaining = {NULL, 0};
   int       orig_col;
   int   has_preinsert = ins_compl_preinsert_effect();
   if (has_preinsert) {
      col += (int)ins_compl_leader_len();
      curPor->cursor.col = compl_ins_end_col;
   }

   if (curPor->cursor.lnum > compl_lnum) {
      if (curPor->cursor.col < ml_get_curline_len()) {
         CS line = ml_get_cursor();
         remaining.len = ml_get_cursor_len();
         remaining.c = copySubstr(line, remaining.len);
         if (!remaining.c)
            return;
      }
      while (curPor->cursor.lnum > compl_lnum) {
         if (ml_delete(curPor->cursor.lnum) == FAIL) {
            if (remaining.c)
               eeglFree(remaining.c);
            return;
         }
         deleted_lines_mark(curPor->cursor.lnum, 1L);
         curPor->cursor.lnum--;
      }
      // move cursor to end of line
      curPor->cursor.col = ml_get_curline_len();
   }

   if ((int)curPor->cursor.col > col) {
      if (stop_arrow() == FAIL) {
         if (remaining.c)
            eeglFree(remaining.c);
         return;
      }
      backspace_until_column(col);
      compl_ins_end_col = curPor->cursor.col;
   }

   if (remaining.c) {
      orig_col = curPor->cursor.col;
      ins_str(remaining.c, remaining.len);
      curPor->cursor.col = orig_col;
      eeglFree(remaining.c);
   }
   //TODO: is this sufficient for redrawing?  Redrawing everything causes
   //flicker, thus we can't do that.
   changed_cline_bef_curs();
   // clear v:completed_item
   set_EeglVar_dict(VV_COMPLETED_ITEM, allocBag_lock(VAR_FIXED));
}

//Insert a completion string that contains newlines. The string is split and inserted line by line.
private void
ins_compl_expand_multiple(CS str) {
   CS  start = str;
   CS curr = str;
   int base_indent = get_indent();

   while (*curr != ZERO) {
      if (*curr == '\n') {
         // Insert the text chunk before newline
         if (curr > start)
            opInsertCharBytes(start, (int)(curr - start), false);

         // Handle newline
         openLine(OPENLINE_KEEPTRAIL | OPENLINE_FORCE_INDENT, base_indent);
         start = curr + 1;
      }
      curr++;
   }

   // Handle remaining text after last newline (if any)
   if (curr > start)
      opInsertCharBytes(start, (int)(curr - start), false);

   compl_ins_end_col = curPor->cursor.col;
}

//Insert the new text being completed.
//"move_cursor" is used when 'completeopt' includes "preinsert" and when TRUE
//cursor needs to move back from the inserted text to the compl_leader.
private void
ins_compl_insert(int move_cursor) {
   int compl_len = get_compl_len();
   int preinsert = ins_compl_has_preinsert();
   CS cp_str = compl_shown_match->cp_str.c;
   Unt cp_str_len = compl_shown_match->cp_str.len;
   Unt leader_len = ins_compl_leader_len();
   CS has_multiple = firstOccurrence(cp_str, '\n');

   // Since completion sources may provide matches with varying start positions, insert only the 
   // portion of the match that corresponds to the intended replacement range
   if (cpt_sources_array) {
      int cpt_idx = compl_shown_match->indexOfSourceInCpt;
      if (cpt_idx >= 0 && compl_col >= 0) {
         int startcol = cpt_sources_array[cpt_idx].startCol;
         if (startcol >= 0 && startcol < (int)compl_col) {
            int skip = (int)compl_col - startcol;
            if ((Unt)skip <= cp_str_len) {
               cp_str_len -= skip;
               cp_str += skip;
            }
         }
      }
   }

   // Make sure we don't go over the end of the string, this can happen with illegal bytes.
   if (compl_len < (int)cp_str_len) {
      if (has_multiple)
         ins_compl_expand_multiple(cp_str + compl_len);
      else {
         ins_compl_insert_bytes(cp_str + compl_len, -1);
         if (preinsert && move_cursor)
            curPor->cursor.col -= (ColNr)(cp_str_len - leader_len);
      }
   }
   if (match_at_original_text(compl_shown_match) || preinsert)
      complUsedMatchS = false;
   else
      complUsedMatchS = true;
   Bag *bag = ins_compl_allocBag(compl_shown_match);

   set_EeglVar_dict(VV_COMPLETED_ITEM, bag);
}

// show the file name for the completion match (if any). Truncate the file name to avoid a wait 
// for return
private void
ins_compl_show_filename(void) {
   CS  lead = _("match in file");
   int      space = sc_col - eeglStrSize((CS)lead) - 2;
   CS s;
   CS e;

   if (space <= 0)
      return;

   // We need the tail that fits.  With double-byte encoding going back from the end is very slow,
   // thus go from the start and keep the text that fits in "space" between "s" and "e".
   for (s = e = compl_shown_match->fName; *e != ZERO; MB_PTR_ADV(e)) {
      space -= ptr2cells(e);
      while (space < 0) {
         space += ptr2cells(s);
         MB_PTR_ADV(s);
      }
   }
   msg_hist_off = TRUE;
   eeSnprintf( IObuff, IOSIZE, "%s %s%s", lead, s > compl_shown_match->fName ? "<" : "", s);
   msg(IObuff);
   msg_hist_off = FALSE;
   redrawCommlineG = FALSE;       // don't overwrite!
}

//Find the appropriate completion item when 'complete' ('cpt') includes a 'max_matches' postfix. 
//In this case, we search for a match where 'cp_in_match_array' is set, indicating that the match 
//is also present in 'displayedCompletionsS'.
private InsertCompletion *
find_next_match_in_menu(void) {
   int       is_forward = compl_shows_dir_forward();
   InsertCompletion *match = compl_shown_match;

   do
      match = is_forward ? match->next : match->prev;
   while (match->next && !match->cp_in_match_array && !match_at_original_text(match));
   return match;
}

//Find the next set of matches for completion. Repeat the completion "todo"
//times. The number of matches found is returned in 'num_matches'.
//
//If "allow_get_expansion" is TRUE, then ins_compl_get_exp() may be called to get more completions.
//If it is FALSE, then do nothing when there are no more completions in the given direction.
//
//If "advance" is TRUE, then completion will move to the first match.
//Otherwise, the original text will be shown.
//
//Return OK on success and FAIL if the number of matches are unknown.
private Unt
find_next_completion_match(
   int allow_get_expansion,
   int todo,      // repeat completion this many times
   int advance,
   int* num_matches
) {
   Boole  found_end = false;
   InsertCompletion   *found_compl = NULL;
   Unt cur_cot_flags = curBook->o.completeOpt;
   int compl_no_select = (cur_cot_flags & COT_NOSELECT) != 0 || compl_autocomplete;
   int compl_fuzzy_match = (cur_cot_flags & COT_FUZZY) != 0;
   Text* leader;

   while (--todo >= 0) {
      if (compl_shows_dir_forward() && compl_shown_match->next != NULL) {
         if (displayedCompletionsS != NULL)
            compl_shown_match = find_next_match_in_menu();
         else
            compl_shown_match = compl_shown_match->next;
         found_end = (compl_first_match != NULL
             && (is_first_match(compl_shown_match->next) || is_first_match(compl_shown_match))
         );
      } ei (compl_shows_dir_backward() && compl_shown_match->prev != NULL) {
         found_end = is_first_match(compl_shown_match);
         if (displayedCompletionsS != NULL)
            compl_shown_match = find_next_match_in_menu();
         else
            compl_shown_match = compl_shown_match->prev;
         found_end |= is_first_match(compl_shown_match);
      } else {
         if (!allow_get_expansion) {
            if (advance) {
               if (compl_shows_dir_backward())
                  compl_pending -= todo + 1;
               else
                  compl_pending += todo + 1;
            }
            return FAIL;
         }

         if (!compl_no_select && advance) {
            if (compl_shows_dir_backward())
               --compl_pending;
            else
               ++compl_pending;
         }

         // Find matches.
         *num_matches = ins_compl_get_exp(&compl_startpos);

         // handle any pending completions
         while (compl_pending != 0 && compl_direction == compl_shows_dir && advance) {
            if (compl_pending > 0 && compl_shown_match->next != NULL) {
               compl_shown_match = compl_shown_match->next;
               --compl_pending;
            }
            if (compl_pending < 0 && compl_shown_match->prev != NULL) {
               compl_shown_match = compl_shown_match->prev;
               ++compl_pending;
            } else
               break;
         }
         found_end = FALSE;
      }

      leader = get_leader_for_startcol(compl_shown_match, FALSE);

      if (!match_at_original_text(compl_shown_match)
            && leader->c
            && !ins_compl_equal(compl_shown_match, leader->c, (int)leader->len)
            && !(compl_fuzzy_match && compl_shown_match->cp_score != FUZZY_SCORE_NONE))
         ++todo;
      else
         // Remember a matching item.
         found_compl = compl_shown_match;

      // Stop at the end of the list when we found a usable match.
      if (found_end) {
         if (found_compl) {
            compl_shown_match = found_compl;
            break;
         }
         todo = 1;       // use first usable match after wrapping around
      }
   }

   return OK;
}

//Fill in the next completion in the current direction.
//If "allow_get_expansion" is TRUE, then we may call ins_compl_get_exp() to get more completions. 
//If it is FALSE, then we just do nothing when there are no more completions in a given direction.
//The latter case is used when we are still in the middle of finding completions, to allow browsing
//through the ones found so far. Return the total number of matches, or -1 if still unknown -- webb.
//
//compl_curr_match is currently being used by ins_compl_get_exp(), so we use compl_shown_match here.
//
//Note that this function may be called recursively once only. First with "allow_get_expansion" 
//TRUE, which calls ins_compl_get_exp(), which in turn calls this function with 
//"allow_get_expansion" FALSE.
private int
ins_compl_next(
   int allow_get_expansion,
   int count,      // repeat completion this many times; should be at least 1
   Boole doInsertMatch   // Insert the newly selected match
){
   int num_matches = -1;
   int todo = count;
   int advance;
   int started = compl_started;
   Book* orig_curbuf = curBook;
   Unt cur_cot_flags = curBook->o.completeOpt;
   int compl_no_insert = (cur_cot_flags & COT_NOINSERT) != 0 || compl_autocomplete;
   int compl_fuzzy_match = (cur_cot_flags & COT_FUZZY) != 0;
   int compl_preinsert = ins_compl_has_preinsert();

   // When user complete function return -1 for findstart which is next
   // time of 'always', compl_shown_match become NULL.
   if (compl_shown_match == NULL)
      return -1;

   if (compl_leader.c
          && !match_at_original_text(compl_shown_match)
          && !compl_fuzzy_match)
      // Update "compl_shown_match" to the actually shown match
      ins_compl_update_shown_match();

   if (allow_get_expansion && doInsertMatch && (!compl_get_longest || complUsedMatchS))
      // Delete old text to be replaced
      ins_compl_delete();

   // When finding the longest common text we stick at the original text,
   // don't let CTRL-N or CTRL-P move to the first match.
   advance = count != 1 || !allow_get_expansion || !compl_get_longest;

   // When restarting the search don't insert the first match either.
   if (compl_restarting) {
      advance = FALSE;
      compl_restarting = FALSE;
   }

   //Repeat this for when <PageUp> or <PageDown> is typed.  But don't wrap around.
   if (find_next_completion_match(allow_get_expansion, todo, advance, &num_matches) == FAIL)
      return -1;

   if (curBook != orig_curbuf) {
      // In case some completion function switched buffer, don't want to
      // insert the completion elsewhere.
      return -1;
   }

   // Insert the text of the new completion, or the compl_leader.
   if (compl_no_insert && !started && !compl_preinsert) {
      ins_compl_insert_bytes(compl_orig_text.c + get_compl_len(), -1);
      complUsedMatchS = false;
   } ei (doInsertMatch) {
      if (!compl_get_longest || complUsedMatchS)
         ins_compl_insert(TRUE);
      else
         ins_compl_insert_bytes(compl_leader.c + get_compl_len(), -1);
   } else
      complUsedMatchS = false;

   if (!allow_get_expansion) {
      // may undisplay the popup menu first
      ins_compl_upd_pum();

      if (pum_enough_matches())
         // Will display the popup menu, don't redraw yet to avoid flicker.
         pum_callUpdateScreen();
      else
         // Not showing the popup menu yet, redraw to show the user what was inserted.
         drawUpdateScreen(0);

      // display the updated popup menu
      ins_compl_show_pum();

      //Delete old text to be replaced, since we're still searching and don't want to match 
      // ourselves!
      ins_compl_delete();
   }

   // Enter will select a match when the match wasn't inserted and the popup menu is visible.
   if (compl_no_insert && !started && compl_selected_item != -1)
      compl_enter_selects = TRUE;
   else
      compl_enter_selects = !doInsertMatch && displayedCompletionsS;

   // Show the file name for the match (if any)
   if (compl_shown_match->fName)
      ins_compl_show_filename();

   return num_matches;
}

// Check if the current completion source exceeded its timeout. If so, stop collecting 
// & halve the timeout
private void
check_elapsed_time(void) {
   if (cpt_sources_array == NULL || cpt_sources_index < 0)
      return;

   Elapsed* start_tv = &cpt_sources_array[cpt_sources_index].matchCollectionStart;
   long elapsed_ms = ELAPSED_FUNC(*start_tv);

   if (elapsed_ms > InsertCompletionimeout_ms) {
      InsertCompletionime_slice_expired = TRUE;
      DECAY_InsertCompletionIMEOUT();
   }
}

//Call this while finding completions, to check whether the user has hit a key
//that should change the currently displayed completion, or exit completion
//mode.  Also, when compl_pending is not zero, show a completion as soon as possible. -- webb
//"frequency" specifies out of how many calls we actually check.
//"in_compl_func" is TRUE when called from complete_check(), don't set compl_curr_match.
void
ins_compl_check_keys(int frequency, Boole in_compl_func) {
   static int   count = 0;
   // Don't check when reading keys from a script, :normal or feedkeys().
   // That would break the test scripts.  But do check for keys when called from complete_check()
   if (!in_compl_func && (using_script() || ex_normal_busy))
      return;

   // Only do this at regular intervals
   if (++count < frequency)
      return;
   count = 0;

   // Check for a typed key. Do use mappings, otherwise eeIsCtrlXKey() can't work correctly
   Unt c = vpeekc_any();
   if (c != ZERO
       // If test_override("char_avail", 1) was called, ignore characters
       // waiting in the typeahead buffer.
       && !disable_char_avail_for_testing
    ) {
      if (eeIsCtrlXKey(c) && c != Ctrl_X && c != Ctrl_R) {
         c = safe_vgetc();   // Eat the character
         compl_shows_dir = ins_compl_key2dir(c);
         (void)ins_compl_next(FALSE, ins_compl_key2count(c), c != K_UP && c != K_DOWN);
      } else {
         // Need to get the character to have KeyTyped set.  We'll put it
         // back with vungetc() below.  But skip K_IGNORE.
         c = safe_vgetc();
         if (c != K_IGNORE) {
            // Don't interrupt completion when the character wasn't typed,
            // e.g., when doing @q to replay keys.
            if (c != Ctrl_R && KeyTyped)
                compl_interrupted = TRUE;

            vungetc(c);
         }
      }
   } ei (compl_autocomplete)
      check_elapsed_time();

   if (compl_pending != 0 && !gotInterruptG && !(cot_flags & COT_NOINSERT) && !compl_autocomplete) {
      // Insert the first match immediately and advance compl_shown_match,
      // before finding other matches.
      int todo = compl_pending > 0 ? compl_pending : -compl_pending;

      compl_pending = 0;
      (void)ins_compl_next(FALSE, todo, TRUE);
   }
}

// Decide the direction of Insert mode complete from the key typed. Return BACKWARD or FORWARD.
private Unt
ins_compl_key2dir(Unt c) {
   if (c == Ctrl_P || c == Ctrl_L || c == K_PAGEUP || c == K_KPAGEUP || c == K_S_UP || c == K_UP)
      return BACKWARD;
   return FORWARD;
}

// Return TRUE for keys that are used for completion only when the popup menu is visible.
private Boole
ins_compl_pum_key(Unt c) {
    return pum_visible() && (c == K_PAGEUP || c == K_KPAGEUP || c == K_S_UP
           || c == K_PAGEDOWN || c == K_KPAGEDOWN || c == K_S_DOWN
           || c == K_UP || c == K_DOWN);
}

//Decide the number of completions to move forward.
//Return 1 for most keys, height of the popup menu for page-up/down keys.
private int
ins_compl_key2count(Unt c) {
   if (ins_compl_pum_key(c) && c != K_UP && c != K_DOWN) {
      int h = pum_get_height();
      if (h > 3)
          h -= 2; // keep some context
      return h;
   }
   return 1;
}

// Return TRUE if completion with "c" should insert the match, FALSE if only to change the 
// currently selected completion.
private Boole
shouldNewCharInsertTheMatch(int c) {
   switch (c) {
   case K_UP:
   case K_DOWN:
   case K_PAGEDOWN:
   case K_KPAGEDOWN:
   case K_S_DOWN:
   case K_PAGEUP:
   case K_KPAGEUP:
   case K_S_UP:
      return false;
   default:
      return true;
   }
}

//Get the pattern, column and length for normal completion (CTRL-N CTRL-P completion)
//Set the global variables: compl_col, compl_length and compl_pattern.
//Use the global variables: compl_cont_status and ctrl_x_mode
private Unt
get_normal_compl_info(CS line, int startcol, ColNr curs_col) {
   if ((compl_cont_status & CONT_SOL) || ctrl_x_mode_path_defines()) {
      if (!compl_status_adding()) {
          while (--startcol >= 0 && eeIsIdentifierChar(line[startcol]))
             {}
          compl_col += ++startcol;
          compl_length = curs_col - startcol;
      }
      if (p_ic) {
          compl_pattern.c = str_foldcase(line + compl_col,
             compl_length, NULL, 0);
         if (compl_pattern.c == NULL) {
            compl_pattern.len = 0;
            return FAIL;
         }
         compl_pattern.len = STRLEN(compl_pattern.c);
      } else {
         compl_pattern.c = copySubstr(line + compl_col, (Unt)compl_length);
         if (compl_pattern.c == NULL) {
            compl_pattern.len = 0;
            return FAIL;
         }
         compl_pattern.len = (Unt)compl_length;
      }
   } ei (compl_status_adding()) {
      CS prefix = S"\\<";
      Unt prefixlen = STRLEN_LITERAL("\\<");

      if (!eeIsWordPtr(line + compl_col)
         || (compl_col > 0 && (eeIsWordPtr(mb_prevptr(line, line + compl_col))))
      ) {
         prefix = S"";
         prefixlen = 0;
      }

      // we need up to 2 extra chars for the prefix
      Unt n = quote_meta(NULL, line + compl_col, compl_length) + prefixlen;
      compl_pattern.c = alloc(n);
      STRCPY((char *)compl_pattern.c, prefix);
      (void)quote_meta(compl_pattern.c + prefixlen,
         line + compl_col, compl_length);
      compl_pattern.len = n - 1;
   } ei (--startcol < 0 || !eeIsWordPtr(mb_prevptr(line, line + startcol + 1))) {
      Unt   len = STRLEN_LITERAL("\\<\\k\\k");

      // Match any word of at least two chars
      compl_pattern.c = copySubstr((CS)"\\<\\k\\k", len);
      if (compl_pattern.c == NULL) {
         compl_pattern.len = 0;
         return FAIL;
      }
      compl_pattern.len = len;
      compl_col += curs_col;
      compl_length = 0;
      compl_from_nonkeyword = TRUE;
   } else {
      //Search the point of change class of multibyte character
      //or not a word single byte character backward.
      int head_off;

      startcol -= (*mb_head_off)(line, line + startcol);
      int base_class = mb_get_class(line + startcol);
      while (--startcol >= 0) {
         head_off = (*mb_head_off)(line, line + startcol);
         if (base_class != mb_get_class(line + startcol - head_off))
            break;
         startcol -= head_off;
      }

      compl_col += ++startcol;
      compl_length = (int)curs_col - startcol;
      if (compl_length == 1) {
         // Only match word with at least two chars -- webb
         // there's no need to call quote_meta, alloc(7) is enough  -- Acevedo
         compl_pattern.c = alloc(7);
         STRCPY((char *)compl_pattern.c, "\\<");
         (void)quote_meta(compl_pattern.c + 2, line + compl_col, 1);
         STRCAT((char *)compl_pattern.c, "\\k");
         compl_pattern.len = STRLEN(compl_pattern.c);
      } else {
         Unt  n = quote_meta(NULL, line + compl_col, compl_length) + 2;

         compl_pattern.c = alloc(n);
         STRCPY((char *)compl_pattern.c, "\\<");
         (void)quote_meta(compl_pattern.c + 2, line + compl_col, compl_length);
         compl_pattern.len = n - 1;
      }
   }

   // Call functions in 'complete' with 'findstart=1'
   if (ctrl_x_mode_normal() && !(compl_cont_status & CONT_LOCAL) && curBook->o.complete) {
      // ^N completion, not complete() or ^X^N
      if (setup_cpt_sources() == FAIL || prepare_cpt_compl_funcs() == FAIL)
          return FAIL;
   }

   return OK;
}

//Get the pattern, column and length for whole line completion or for the complete() function.
//Set the global variables: compl_col, compl_length and compl_pattern.
private int
get_wholeline_compl_info(CS line, ColNr curs_col) {
   compl_col = (ColNr)getwhitecols(line);
   compl_length = (int)curs_col - (int)compl_col;
   if (compl_length < 0)   // cursor in indent: empty pattern
      compl_length = 0;
   if (p_ic) {
      compl_pattern.c = str_foldcase(line + compl_col, compl_length, NULL, 0);
      if (compl_pattern.c == NULL) {
         compl_pattern.len = 0;
         return FAIL;
      }
      compl_pattern.len = STRLEN(compl_pattern.c);
   } else {
      compl_pattern.c = copySubstr(line + compl_col, (Unt)compl_length);
      if (compl_pattern.c == NULL) {
         compl_pattern.len = 0;
         return FAIL;
      }
      compl_pattern.len = (Unt)compl_length;
   }

   return OK;
}

//Get the pattern, column and length for filename completion.
//Set the global variables: compl_col, compl_length and compl_pattern.
private int
get_filename_compl_info(CS line, int startcol, ColNr curs_col) {
   // Go back to just before the first filename character.
   if (startcol > 0) {
      CS p = line + startcol;

      MB_PTR_BACK(line, p);
      while (p > line && eeIsFnameChar(mb_ptr2char(p)))
         MB_PTR_BACK(line, p);
      if (p == line && eeIsFnameChar(mb_ptr2char(p)))
         startcol = 0;
      else
         startcol = (int)(p - line) + 1;
   }

   compl_col += startcol;
   compl_length = (int)curs_col - startcol;
   compl_pattern.c = addstar((Text){line + compl_col, compl_length}, EXPAND_FILES);

   compl_pattern.len = STRLEN(compl_pattern.c);

   return OK;
}

// Get the pattern, column and length for command-line completion.
// Set the global variables: compl_col, compl_length and compl_pattern.
private int
get_cmdline_compl_info(CS line, ColNr curs_col) {
   compl_pattern.c = copySubstr(line, curs_col);
   if (!compl_pattern.c) {
      compl_pattern.len = 0;
      return FAIL;
   }
   compl_pattern.len = curs_col;
   setCompletionContextForCommand(&compl_xp, compl_pattern, curs_col, FALSE);
   if (compl_xp.context == EXPAND_UNSUCCESSFUL || compl_xp.context == EXPAND_NOTHING)
      // No completion possible, use an empty pattern to get a "pattern not found" message.
      compl_col = curs_col;
   else
      compl_col = (int)(compl_xp.input.c - compl_pattern.c);
   compl_length = curs_col - compl_col;

   return OK;
}

// Set global variables related to completion: compl_col, compl_length, compl_pattern and 
// cpt_compl_pattern.
private int
set_compl_globals(int startcol, ColNr curs_col, int is_cpt_compl) {
   if (is_cpt_compl) {
      EE_CLEAR_STRING(cpt_compl_pattern);
      if (startcol < compl_col)
         return prepend_startcol_text(&cpt_compl_pattern, &compl_orig_text, startcol);
      else {
         cpt_compl_pattern.c = copySubstr(compl_orig_text.c, compl_orig_text.len);
         cpt_compl_pattern.len = compl_orig_text.len;
      }
   } else {
      if (startcol < 0 || startcol > curs_col)
         startcol = curs_col;

      // Re-obtain line in case it has changed
      CS line = ml_get(curPor->cursor.lnum);
      int   len = curs_col - startcol;

      compl_pattern.c = copySubstr(line + startcol, (Unt)len);
      if (compl_pattern.c == NULL) {
         compl_pattern.len = 0;
         return FAIL;
      }
      compl_pattern.len = (Unt)len;
      compl_col = startcol;
      compl_length = len;
   }

   return OK;
}

//Get the pattern, column and length for user defined completion ('omnifunc', 'completefunc' and 
//'thesaurusfunc').
//Callback function "cb" is set if triggered by a function in the 'cpt' option; otherwise, it is 
//null. "startcol", when not NULL, contains the column returned by function.
private int
get_userdefined_compl_info(ColNr curs_col, Callback* cb, int* startcol) {
   int ret = FAIL;

   // Call user defined function 'completefunc' with "a:findstart"
   // set to 1 to obtain the length of text to use for completion.
   Var   args[3];
   int      col;
   CS funcname = NULL;
   Pos pos;
   int save_State = stateG;
   int is_cfntion = (cb != NULL);

   if (!is_cfntion) {
      // Call 'completefunc' or 'omnifunc' or 'thesaurusfunc' and get pattern
      // length as a string
      funcname = get_complete_funcname(ctrl_x_mode);
      if (*funcname == ZERO) {
         showErrFmtMsg(_(e_option_str_is_not_set), ctrl_x_mode_function() ? "completefunc" : "omnifunc");
         return FAIL;
      }
      cb = get_insert_callback(ctrl_x_mode);
   }

   args[0].tag = VAR_NUMBER;
   args[0].number = 1;
   args[1].tag = VAR_STRING;
   args[1].string = (CS)"";
   args[2].tag = VAR_UNKNOWN;
   pos = curPor->cursor;
   ++textlock;
   col = call_callback_retnr(cb, 2, args);
   --textlock;

   stateG = save_State;
   curPor->cursor = pos;   // restore the cursor position
   check_cursor();  // make sure cursor position is valid, just in case
   validate_cursor();
   if (!EQUAL_POS(curPor->cursor, pos)) {
      emsg(_(e_complete_function_deleted_text));
      return FAIL;
   }

   if (startcol)
      *startcol = col;

   // Return value -2 means the user complete function wants to cancel the complete without an 
   // error, do the same if the function did not execute successfully.
   if (col == -2 || aborting())
      return FAIL;

   // Return value -3 does the same as -2 and leaves CTRL-X mode.
   if (col == -3) {
      if (is_cfntion)
         return FAIL;
      ctrl_x_mode = CTRL_X_NORMAL;
      edit_submode = NULL;
      msgClearCommline();
      return FAIL;
   }

   // Reset extended parameters of completion, when starting new completion.
   compl_opt_refresh_always = FALSE;
   compl_opt_suppress_empty = FALSE;

   ret = !is_cfntion ? set_compl_globals(col, curs_col, FALSE) : OK;

   return ret;
}

//Get the completion pattern, column and length.
//"startcol" - start column number of the completion pattern/text "cur_col" - current cursor column
//On return, "line_invalid" is set to TRUE, if the current line may have
//become invalid and needs to be fetched again. Return OK on success.
private Unt
compl_get_info(CS line, int startcol, ColNr curs_col, OUT Boole* line_invalid) {
   if (ctrl_x_mode_normal() || ctrl_x_mode_register()
       || (ctrl_x_mode & CTRL_X_WANT_IDENT
      && !thesaurus_func_complete(ctrl_x_mode))
   ) {
      if (get_normal_compl_info(line, startcol, curs_col) != OK)
         return FAIL;
      *line_invalid = true; // 'cpt' func may have invalidated "line"
   } ei (ctrl_x_mode_line_or_eval()) {
      return get_wholeline_compl_info(line, curs_col);
   } ei (ctrl_x_mode_files()) {
      return get_filename_compl_info(line, startcol, curs_col);
   } ei (ctrl_x_mode == CTRL_X_CMDLINE) {
      return get_cmdline_compl_info(line, curs_col);
   } ei (ctrl_x_mode_function() || ctrl_x_mode_omni() || thesaurus_func_complete(ctrl_x_mode)){
      if (get_userdefined_compl_info(curs_col, NULL, NULL) != OK)
         return FAIL;
      *line_invalid = true;   // "line" may have become invalid
   } else {
      internal_error(S"ins_complete()");
      return FAIL;
   }

   return OK;
}

//Continue an interrupted completion mode search in "line".
//If this same ctrl_x_mode has been interrupted use the text from "compl_startpos" to the cursor 
//as a pattern to add a new word instead of expand the one before the cursor, in word-wise if 
//"compl_startpos" is not in the same line as the cursor then fix it (the line has been split 
//because it was longer than 'tw').  if SOL is set then skip the previous pattern, a word
//at the beginning of the line has been inserted, we'll look for that.
private void
ins_compl_continue_search(CS line) {
   // it is a continued search
   compl_cont_status &= ~CONT_INTRPT;   // remove INTRPT
   if (ctrl_x_mode_normal() || ctrl_x_mode_path_patterns() || ctrl_x_mode_path_defines()) {
      if (compl_startpos.lnum != curPor->cursor.lnum) {
         // line (probably) wrapped, set compl_startpos to the first non_blank in the line, if it 
         // is not a wordchar include it to get a better pattern, but then we don't
         // want the "\\<" prefix, check it below
         compl_col = (ColNr)getwhitecols(line);
         compl_startpos.col = compl_col;
         compl_startpos.lnum = curPor->cursor.lnum;
         compl_cont_status &= ~CONT_SOL;   // clear SOL if present
      } else {
         // S_IPOS was set when we inserted a word that was at the beginning of the line, which 
         // means that we'll go to SOL mode but first we need to redefine compl_startpos
         if (compl_cont_status & CONT_S_IPOS) {
            compl_cont_status |= CONT_SOL;
            compl_startpos.col = (ColNr)(skipwhite( line + compl_length + compl_startpos.col) - line);
         }
         compl_col = compl_startpos.col;
      }
      compl_length = curPor->cursor.col - (int)compl_col;
      // IObuff is used to add a "word from the next line" would we
      // have enough space?  just being paranoid
#define   MIN_SPACE 75
      if (compl_length > (IOSIZE - MIN_SPACE)) {
         compl_cont_status &= ~CONT_SOL;
         compl_length = (IOSIZE - MIN_SPACE);
         compl_col = curPor->cursor.col - compl_length;
      }
      compl_cont_status |= CONT_ADDING | CONT_N_ADDS;
      if (compl_length < 1)
          compl_cont_status &= CONT_LOCAL;
   } ei (ctrl_x_mode_line_or_eval() || ctrl_x_mode_register())
      compl_cont_status = CONT_ADDING | CONT_N_ADDS;
   else
      compl_cont_status = 0;
}

// start insert mode completion
private Unt
ins_compl_start(void) {
   int      startcol = 0;       // column where searched text starts
   Boole didAindentSaved = didAindentG;

   // First time we hit ^N or ^P (in a row, I mean)
   didAindentG = false;
   didSindentG = false;
   can_si = FALSE;
   can_si_back = FALSE;
   if (stop_arrow() == FAIL)
      return FAIL;

   CS line = ml_get(curPor->cursor.lnum);
   ColNr curs_col = curPor->cursor.col;
   compl_pending = 0;
   compl_lnum = curPor->cursor.lnum;

   if ((compl_cont_status & CONT_INTRPT) == CONT_INTRPT && compl_cont_mode == ctrl_x_mode)
      // this same ctrl-x_mode was interrupted previously. Continue the completion.
      ins_compl_continue_search(line);
   else
      compl_cont_status &= CONT_LOCAL;

   if (!compl_status_adding()) {  // normal expansion
      compl_cont_mode = ctrl_x_mode;
      if (ctrl_x_mode_not_default())
         // Remove LOCAL if ctrl_x_mode != CTRL_X_NORMAL
         compl_cont_status = 0;
      compl_cont_status |= CONT_N_ADDS;
      compl_startpos = curPor->cursor;
      startcol = (int)curs_col;
      compl_col = 0;
   }

   // Work out completion pattern and original text -- webb
   Boole line_invalid = false;
   if (compl_get_info(line, startcol, curs_col, OUT &line_invalid) == FAIL) {
      if (ctrl_x_mode_function() || ctrl_x_mode_omni() || thesaurus_func_complete(ctrl_x_mode))
         // restore didAindentG, so that adding comment leader works
         didAindentG = didAindentSaved;
      return FAIL;
   }
   // If "line" was changed while getting completion info get it again.
   if (line_invalid)
      line = ml_get(curPor->cursor.lnum);

   if (compl_status_adding()) {
      edit_submode_pre = (CS)_(" Adding");
      if (ctrl_x_mode_line_or_eval()) {
         // Insert a new line, keep indentation but ignore 'comments'.
         compl_startpos.lnum = curPor->cursor.lnum;
         compl_startpos.col = compl_col;
         ins_eol('\r');
         compl_length = 0;
         compl_col = curPor->cursor.col;
         compl_lnum = curPor->cursor.lnum;
      } ei (ctrl_x_mode_normal() && cfc_has_mode()) {
          compl_startpos = curPor->cursor;
          compl_cont_status &= CONT_S_IPOS;
      }
   } else {
      edit_submode_pre = NULL;
      compl_startpos.col = compl_col;
   }

   if (!compl_autocomplete) {
      if (compl_cont_status & CONT_LOCAL)
         edit_submode = (CS)_(ctrl_x_msgs[CTRL_X_LOCAL_MSG]);
      else
         edit_submode = (CS)_(CTRL_X_MSG(ctrl_x_mode));
   }

   // If any of the original typed text has been changed we need to fix the redo buffer.
   ins_compl_fixRedoBufForLeader(NULL);

   // Always add completion for the original text.
   EE_CLEAR_STRING(compl_orig_text);
   compl_orig_text.len = (Unt)compl_length;
   compl_orig_text.c = copySubstr(line + compl_col, (Unt)compl_length);
   Unt flags = CP_ORIGINAL_TEXT;
   if (p_ic)
      flags |= CP_ICASE;
   if (!compl_orig_text.c 
         || addMatchToList(
               compl_orig_text.c, (int)compl_orig_text.len, NULL, NULL, NULL, 0, flags, false, NULL,
               FUZZY_SCORE_NONE
            ) != OK
   ) {
      EE_CLEAR_STRING(compl_pattern);
      EE_CLEAR_STRING(compl_orig_text);
      return FAIL;
   }

   // showmode might reset the internal line pointers, so it must be called before 
   // line = ml_get(), or when this address is no longer needed.  -- Acevedo.
   if (!compl_autocomplete) {
      edit_submode_extra = (CS)_("-- Searching...");
      edit_submode_highl = 0;
      showmode();
      edit_submode_extra = NULL;
      out_flush();
   }

   return OK;
}

// display the completion status message
private void
ins_compl_show_statusmsg(void) {
   // we found no match if the list has only the "compl_orig_text"-entry
   if (is_first_match(compl_first_match->next)) {
      edit_submode_extra = compl_status_adding() && compl_length > 1
               ? _("Hit end of paragraph")
               : _("Pattern not found");
      edit_submode_highl = HLF_E;
   }

   if (edit_submode_extra == NULL) {
      if (match_at_original_text(compl_curr_match)) {
         edit_submode_extra = (CS)_("Back at original");
         edit_submode_highl = HLF_W;
      } ei (compl_cont_status & CONT_S_IPOS) {
         edit_submode_extra = (CS)_("Word from other line");
         edit_submode_highl = 0;
      } ei (compl_curr_match->next == compl_curr_match->prev) {
         edit_submode_extra = (CS)_("The only match");
         edit_submode_highl = 0;
         compl_curr_match->cp_number = 1;
      } else {
         //Update completion sequence number when needed.
         if (compl_curr_match->cp_number == -1)
            ins_compl_update_sequence_numbers();
         //The match should always have a sequence number now, this is just a safety check.
         if (compl_curr_match->cp_number != -1) {
            //Space for 10 text chars. + 2x10-digit no.s = 31.
            //Translations may need more than twice that.
            static Byte match_ref[81];

            if (compl_matches > 0)
               eeSnprintf(
                  match_ref, sizeof(match_ref), _("match %d of %d"), 
                  compl_curr_match->cp_number, compl_matches
               );
            else
               eeSnprintf(
                  match_ref, sizeof(match_ref), _("match %d"), compl_curr_match->cp_number
               );
            edit_submode_extra = match_ref;
            edit_submode_highl = HLF_R;
         }
      }
   }

   // Show a message about what (completion) mode we're in.
   if (!compl_opt_suppress_empty) {
      showmode();
      if (edit_submode_extra) {
         if (!p_smd) {
            msg_hist_off = TRUE;
            msgDeco(edit_submode_extra, getDecoFlags(edit_submode_highl));
            msg_hist_off = FALSE;
         }
      } else
         msgClearCommline();   // necessary for "noshowmode"
   }
}

//Do Insert mode completion. Called when character "c" was typed, which has a meaning for 
//completion. Return OK if completion was done, FAIL if something failed (out of mem).
private Unt
ins_complete(Unt c, Boole enable_pum) {
   Elapsed   matchCollectionStart; // Timestamp when match collection starts

   compl_direction = ins_compl_key2dir(c);
   Boole doInsertMatch = shouldNewCharInsertTheMatch(c);

   if (!compl_started) {
      if (ins_compl_start() == FAIL)
         return FAIL;
   } ei (doInsertMatch && stop_arrow() == FAIL)
      return FAIL;

   if (compl_autocomplete && p_acl > 0)
      ELAPSED_INIT(matchCollectionStart);
   compl_curr_win = curPor;
   compl_curr_buf = curPor->book;
   compl_shown_match = compl_curr_match;
   compl_shows_dir = compl_direction;

   // Find next match (and following matches).
   int save_cursorRow = curPor->cursorRow;
   int save_leftCol = curPor->leftCol;
   int n = ins_compl_next(TRUE, ins_compl_key2count(c), doInsertMatch);

   // may undisplay the popup menu
   ins_compl_upd_pum();

   if (n > 1)      // all matches have been found
      compl_matches = n;
   compl_curr_match = compl_shown_match;
   compl_direction = compl_shows_dir;

   // Eat the ESC that vgetc() returns after a CTRL-C to avoid leaving Insert mode.
   if (gotInterruptG && !global_busy) {
      (void)vgetc();
      gotInterruptG = FALSE;
   }

   // we found no match if the list has only the "compl_orig_text"-entry
   Boole no_matches_found = is_first_match(compl_first_match->next);
   if (no_matches_found) {
      // remove N_ADDS flag, so next ^X<> won't try to go to ADDING mode,
      // because we couldn't expand anything at first place, but if we used
      // ^P, ^N, ^X^I or ^X^D we might want to add-expand a single-char-word
      // (such as M in M'exico) if not tried already.  -- Acevedo
      if (compl_length > 1
         || compl_status_adding()
         || (ctrl_x_mode_not_default()
             && !ctrl_x_mode_path_patterns()
             && !ctrl_x_mode_path_defines()))
          compl_cont_status &= ~CONT_N_ADDS;
   }

   if (compl_curr_match->flags & CP_CONT_S_IPOS)
      compl_cont_status |= CONT_S_IPOS;
   else
      compl_cont_status &= ~CONT_S_IPOS;

   if (!compl_autocomplete)
      ins_compl_show_statusmsg();

   // Wait for the autocompletion delay to expire
   if (compl_autocomplete && p_acl > 0 && !no_matches_found
       && ELAPSED_FUNC(matchCollectionStart) < p_acl
   ) {
      cursor_on();
      setcursor();
      out_flush();
      do {
         if (char_avail()) {
            ins_compl_restart();
            compl_interrupted = TRUE;
            break;
         } else
            ui_delay(2L, TRUE);
      } while (ELAPSED_FUNC(matchCollectionStart) < p_acl);
   }

   // Show the popup menu, unless we got interrupted.
   if (enable_pum && !compl_interrupted)
      show_pum(save_cursorRow, save_leftCol);

   compl_was_interrupted = compl_interrupted;
   compl_interrupted = FALSE;

   return OK;
}

// Return TRUE if the given character 'c' can be used to trigger autocompletion.
private Boole
ins_compl_setup_autocompl(Unt c) {
   if (bookIsCharPrintable(c)) {
      compl_autocomplete = true;
      return true;
   }
   return false;
}

// Remove (if needed) and show the popup menu
private void
show_pum(int prev_cursorRow, int prev_leftCol) {
   // isRedrawingDisabledG may be set when invoked through complete().
   int save_isRedrawingDisabledG = isRedrawingDisabledG;
   isRedrawingDisabledG = 0;

   // If the cursor moved or the display scrolled we need to remove the pum first.
   setcursor();
   if (prev_cursorRow != curPor->cursorRow || prev_leftCol != curPor->leftCol)
      ins_compl_del_pum();

   ins_compl_show_pum();
   setcursor();

   isRedrawingDisabledG = save_isRedrawingDisabledG;
}

//Looks in the first "len" chars. of "src" for search-metachars. If dest is not NULL the chars. 
//are copied there quoting (with a backslash) the metachars, and dest would be ZERO 
//terminated. Return the length (needed) of dest
private unsigned
quote_meta(CS dest, CS src, int len) {
   unsigned   m = (unsigned)len + 1;  // one extra for the ZERO
   for ( ; --len >= 0; src++) {
      switch (*src) {
      case '.':
      case '*':
      case '[':
      if (ctrl_x_mode_dictionary() || ctrl_x_mode_thesaurus())
         break;
      // FALLTHROUGH
      case '~':
         // FALLTHROUGH
      case '\\':
         if (ctrl_x_mode_dictionary() || ctrl_x_mode_thesaurus())
             break;
         // FALLTHROUGH
      case '^':      // currently it's not needed.
      case '$':
         m++;
         if (dest != NULL)
             *dest++ = '\\';
         break;
      }
      if (dest)
         *dest++ = *src;
      // Copy remaining bytes of a multibyte character.
      int mb_len = utfCharLen(src) - 1;
      if (mb_len > 0 && len >= mb_len)
      for (int i = 0; i < mb_len; ++i) {
         --len;
         ++src;
         if (dest)
            *dest++ = *src;
      }
   }
   if (dest)
      *dest = ZERO;

   return m;
}

#if defined(EXITFREE) || defined(PROTO)
void
free_insexpand_stuff(void) {
   EE_CLEAR_STRING(compl_orig_text);
   evFreeCallback(&completeFnS);
   evFreeCallback(&omniFnS);
   evFreeCallback(&thesaurusCbS);
   inClearCompletionCbs(&customCompleteFnS, cpt_cb_count);
}
#endif

// Reset the info associated with completion sources.
private void
cpt_sources_clear(void) {
   EE_CLEAR(cpt_sources_array);
   cpt_sources_index = -1;
   cpt_sources_count = 0;
}

//Setup completion sources.
private Unt
setup_cpt_sources(void) {
   Byte  buf[LSIZE];
   int slen;
   int idx = 0;

   cpt_sources_clear();
   cpt_sources_array = ALLOC_CLEAR_MULT(CompletionSource, 1);

   for (CS p = curBook->o.complete; *p;) {
      while (*p == ',' || *p == ' ') // Skip delimiters
         p++;
      if (*p) { // If not end of string, count this segment
         slen = doCutPathFromListOfPaths(OUT &p, OUT buf, LSIZE, S","); // Advance p
         if (slen > 0) {
            CS caret = firstOccurrence(buf, '^');
            if (caret)
               cpt_sources_array[idx].maxMatches = atoi((char *)caret + 1);
         }
         idx++;
      }
   }

   return OK;
}

// TRUE if any of the completion sources have 'refresh' set to 'always'.
private Boole
is_cfn_refresh_always(void) {
   for (int i = 0; i < cpt_sources_count; i++) {
      if (cpt_sources_array[i].refreshAlways)
         return true;
   } 
   return false;
}

// Make the completion list acyclic.
private void
ins_compl_make_linear(void) {
   if (compl_first_match == NULL || !compl_first_match->prev)
      return;
   InsertCompletion* m = compl_first_match->prev;
   m->next = NULL;
   compl_first_match->prev = NULL;
}

//Remove the matches linked to the current completion source (as indicated by cpt_sources_index) 
//from the completion list.
private InsertCompletion *
remove_old_matches(void) {
   InsertCompletion *sublist_start = NULL, *sublist_end = NULL, *insert_at = NULL;
   InsertCompletion *current, *next;
   int       compl_shown_removed = FALSE;
   int       forward = (compl_first_match->indexOfSourceInCpt < 0);

   compl_direction = forward ? FORWARD : BACKWARD;
   compl_shows_dir = compl_direction;

   // Identify the sublist of old matches that needs removal
   for (current = compl_first_match; current != NULL; current = current->next) {
      if (current->indexOfSourceInCpt < cpt_sources_index &&
         (forward || (!forward && !insert_at)))
          insert_at = current;

      if (current->indexOfSourceInCpt == cpt_sources_index) {
         if (!sublist_start)
            sublist_start = current;
         sublist_end = current;
         if (!compl_shown_removed && compl_shown_match == current)
            compl_shown_removed = TRUE;
      }

      if ((forward && current->indexOfSourceInCpt > cpt_sources_index) || (!forward && insert_at))
         break;
   }

   // Re-assign compl_shown_match if necessary
   if (compl_shown_removed) {
      if (forward)
         compl_shown_match = compl_first_match;
      else {  // Last node will have the prefix that is being completed
         for (current = compl_first_match; current->next != NULL; current = current->next)
            {}
         compl_shown_match = current;
      }
   }

   if (!sublist_start) // No nodes to remove
      return insert_at;

   // Update links to remove sublist
   if (sublist_start->prev)
      sublist_start->prev->next = sublist_end->next;
   else
      compl_first_match = sublist_end->next;

   if (sublist_end->next)
      sublist_end->next->prev = sublist_start->prev;

   // Free all nodes in the sublist
   sublist_end->next = NULL;
   for (current = sublist_start; current; current = next) {
      next = current->next;
      ins_compl_item_free(current);
   }

   return insert_at;
}

//Retrieve completion matches using the callback function "cb" and store the
//'refresh:always' flag.
private void
get_cfn_completion_matches(Callback *cb UNUSED) {
   int   startcol = cpt_sources_array[cpt_sources_index].startCol;

   if (startcol == -2 || startcol == -3)
      return;

   if (set_compl_globals(startcol, curPor->cursor.col, TRUE) == OK) {
      expand_by_function(0, cpt_compl_pattern.c, cb);

      cpt_sources_array[cpt_sources_index].refreshAlways = compl_opt_refresh_always;
      compl_opt_refresh_always = FALSE;
   }
}

// Retrieve completion matches from functions in the 'cpt' option where the 'refresh:always' 
// flag is set
private void
cpt_compl_refresh(void) {
   Callback   *cb = NULL;
   int  startcol, ret;

   // Make the completion list linear (non-cyclic)
   ins_compl_make_linear();
   // Make a copy of 'cpt' in case the buffer gets wiped out
   CS cpt = copyStr(curBook->o.complete);
   strip_caret_numbers_in_place(cpt);

   cpt_sources_index = 0;
   for (CS p = cpt; *p != ZERO;) {
      while (*p == ',' || *p == ' ') // Skip delimiters
         p++;
      if (*p == ZERO)
         break;

      if (cpt_sources_array[cpt_sources_index].refreshAlways) {
         cb = get_callback_if_cfn(p);
         if (cb) {
            compl_curr_match = remove_old_matches();
            ret = get_userdefined_compl_info(curPor->cursor.col, cb, OUT &startcol);
            if (ret == FAIL) {
               if (startcol == -3)
                  cpt_sources_array[cpt_sources_index].refreshAlways = FALSE;
               else
                  startcol = -2;
            } ei (startcol < 0 || startcol > curPor->cursor.col)
               startcol = curPor->cursor.col;
            cpt_sources_array[cpt_sources_index].startCol = startcol;
            if (ret == OK) {
               compl_source_start_timer(cpt_sources_index);
               get_cfn_completion_matches(cb);
            }
         }
      }

      (void)doCutPathFromListOfPaths(OUT &p, OUT IObuff, IOSIZE, S","); // Advance p
      if (may_advance_cpt_index(p))
         (void)advance_cpt_sources_index_safe();
   }
   cpt_sources_index = -1;

   eeglFree(cpt);
   // Make the list cyclic
   compl_matches = ins_compl_make_cyclic();
}

// Function given to expandGeneric() to obtain the list of :disassemble arguments.
CS
get_disassemble_argument(Expand* xp, int idx) {
   if (idx == 0)
      return S"debug";
   if (idx == 1)
      return S"profile";
   return get_user_func_name(xp, idx - 2);
}

// Copy a global callback function to a book-local callback.
private void
copyGlobalToBookLocalCb(Callback* globcb, Callback* bookCb) {
   evFreeCallback(bookCb);
   if (globcb->name && *globcb->name != ZERO)
      evCopyCallback(bookCb, globcb);
}

//Parse the @completefunc option value and set the callback function. Invoked when @completefunc 
//is set. The option value can be a name of a function (string), or function(<name>) or 
//funcref(<name>) or a lambda expression.
CS
setCompletefunc(OptionChange* cha) {
   CS new = cha->newVal.string;
   if (!new || *new == ZERO)
      return e_invalid_argument;
   if (optSetCallback(OUT &completeFnS, new) == FAIL)
      return e_invalid_argument;

   copyGlobalToBookLocalCb(&completeFnS, curBook->o.completeFn);

   return NULL;
}

// Copy the global @omnifunc callback function to the book-local @omnifunc callback for "book".
void
inSetOmniCbForBook(Book* book) {
   copyGlobalToBookLocalCb(&omniFnS, book->o.omniFn);
}

//Copy the global @tagfunc callback function to the book-local 'tagfunc' callback for 'book'.
void
inSetTagCbForBook(Book* book) {
   evFreeCallback(book->o.tagFn);
   if (thesaurusCbS.name && *thesaurusCbS.name != ZERO)
      evCopyCallback(OUT book->o.tagFn, &thesaurusCbS);
}

//Copy global custom 'complete' F{func} callbacks into the given book's local
//callback array. Clear any existing book-local callbacks first.
void
inSetCustomCompletionCbForBook(Book* book) {
   evFreeCallback(book->o.completeFn);
   if (customCompleteFnS.name && *customCompleteFnS.name != ZERO)
      evCopyCallback(OUT book->o.completeFn, &customCompleteFnS);
}

//Parse the @omnifunc option value and set the callback function.
//Invoked when the @omnifunc option is set. The option value can be a
//name of a function (string), or function(<name>) or funcref(<name>) or a lambda expression.
CS
setOmnifunc(OptionChange* cha) {
   if (!cha->newVal.string || *cha->newVal.string == ZERO)
      return e_invalid_argument;
      
   if (optSetCallback(OUT &omniFnS, cha->newVal.string) == FAIL)
      return e_invalid_argument;

   inSetOmniCbForBook(curBook);
   return NULL;
}

//Parse @complete option and initialize F{func} callbacks. Free any existing callbacks and 
//allocate new ones. Only F{func} entries are processed; others are ignored.
Unt
setCompletionCallbacks(OptionChange *cha) {
   if (!curBook)
      return FAIL;

   Byte buf[LSIZE];
   evFreeCallback(curBook->o.completeFn);

   curBook->o.completeFn = ALLOC_CLEAR_MULT(Callback, 1);

   for (CS p = curBook->o.complete; *p != ZERO; ) {
      while (*p == ',' || *p == ' ')
         p++; // Skip delimiters

      if (*p != ZERO) {
         int slen = doCutPathFromListOfPaths(OUT &p, OUT buf, LSIZE, S","); // Advance p
         if (slen > 0 && buf[0] == 'F' && buf[1] != ZERO) {
            CS caret = firstOccurrence(buf, '^');
            if (caret)
               *caret = ZERO;

            if (optSetCallback(OUT curBook->o.completeFn, buf + 1) != OK)
               curBook->o.completeFn->name = NULL;
         }
      }
   }

   if (cha->setScope == SET_GLOBAL // ':setglobal' used insted of ':set'
      // Cache the callback array
      && copyCompletionCbs(
            &completeFnS, curBook->o.completeFn
         ) != OK
   )
      return FAIL;

   return OK;
}

//}}}
