//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## portal.c: portals (views) into text for user interface

#include "eegl.h"

//{{{forward declarations

private void cmd_with_count(CS cmd, CS bufp, Unt bufsize, long prenum);
private void init(Portal* newp, Portal* oldp, Unt flags);
private void initg_some(Portal* newp, Portal* oldp);
private void frame_comp_pos(Frame *topfrp, int *row, int *col);
private void frame_setheight(Frame *curfrp, int height);
private void frame_setwidth(Frame *curfrp, int width);
private void exchangePortal(long);
private void rotatePortals(int, int);
private void equalizeHeightRec(
   Portal* next_curPor, int current, Frame *topfr, int dir, int col, int row, int width, int height
);
private void triggerPortalNewPre(void);
private void triggerPortalClosed(Portal *port);
private Portal *freePortalMem(Portal *port, Unt* dirp, Tab *t);
private Frame *getAltFrame(Portal *port, Tab *t);
private Tab *altTab(void);
private Portal* frameToPort(Frame* fr);
private int frameHasPortal(Frame *fr, Portal *po);
private void frame_new_height(Frame *topfrp, int height, int topfirst, int wfh, int set_ch);
private int frame_fixed_height(Frame *fr);
private int frame_fixed_width(Frame *fr);
private void frame_add_statusline(Frame *fr);
private void frameNewWidth(Frame *topfrp, int width, int leftfirst, int wfw);
private void frame_add_vsep(Frame *fr);
private int frame_minwidth(Frame *topfrp, Portal *next_curPor);
private void frame_fix_width(Portal *po);
private int allocateFirstPortal(Portal *oldPortal);
private void neframe(Portal *po);
private Tab *alloc_tab(void);
private int leaveTab(Book *newCurBook, int trigger_leave_autocmds);
private void enterTab(
      Tab *t, Book *oldCurBook, int trigger_enter_autocmds, int trigger_leave_autocmds
);
private void frame_fix_height(Portal *po);
private Unt frame_minheight(Frame *topfrp, Portal *next_curPor);
private int mayOpenTab(void);
private int enterPortalWorker(Portal *po, int flags);
private void freePortal(Portal *po, Tab *t);
private void append(Portal *after, Portal *po);
private void frame_append(Frame *after, Frame *fr);
private void frame_insert(Frame *before, Frame *fr);
private void frame_remove(Frame *fr);
private void gotoPortal_ver(int up, long count);
private void gotoPortal_hor(int left, long count);
private void frame_add_height(Frame *fr, int n);
private void last_status_rec(Frame *fr, int statusline);
private void frame_flatten(Frame *fr);
private void restoreFrame(Portal *po, int dir, Frame *unflat_altfr);

private int make_snapshot_rec(Frame *fr, Frame **fr1);
private void clearSnapshot(Tab *t, int idx);
private void clearSnapshot_rec(Frame *fr);
private int check_snapshot_rec(Frame *sn, Frame *fr);
private Portal *restore_snapshot_rec(Frame *sn, Frame *fr);
private Portal *get_snapshot_curPor(int idx);

private void portalNewWidth(Portal *po, int newWidth);
private void portalNewHeight(Portal *po, int newHeight);

private int frame_check_height(Frame *topfrp, int height);
private int frame_check_width(Frame *topfrp, int width);

private Portal* allocPortal(Portal *after, int hidden);

private int popup_closePortal(Portal* port);
private void pum_position_info_popup(Portal *po);

//}}}
//{{{portals

#define TCL_LEFT      0
#define TCL_USELAST   1

#define NOPORT      ((Portal *)-1)   // non-existing portal

// Lowest number used for portal ID. Cannot have this many portals.
#define MIN_PORT_ID 1000


#define ROWS_AVAIL (visibleRowsG - commlineHeightG)

// flags for enterPortalWorker()
#define WEE_UNDO_SYNC         0x01
#define WEE_CURWIN_INVALID      0x02
#define WEE_TRIGGER_NEW_AUTOCMDS   0x04
#define WEE_TRIGGER_ENTER_AUTOCMDS   0x08
#define WEE_TRIGGER_LEAVE_AUTOCMDS   0x10
#define WEE_ALLOW_PARSE_MESSAGES   0x20

private CS m_onlyone = S"Already only one portal";

// When non-zero splitting a portal is forbidden.  Used to avoid that nasty
// autocommands mess up the portal structure.
private int split_disallowed = 0;

// When non-zero closing a portal is forbidden.  Used to avoid that nasty
// autocommands mess up the portal structure.
private int close_disallowed = 0;

//Disallow changing the portal layout (split portal, close portal, move portal). Resizing is 
//still allowed. Used for autocommands that temporarily use another portal and need to
//make sure the previously selected portal is still there.
//Must be matched with exactly one call to portalLayout_unlock()!
void
portalLayout_lock(void) {
   ++split_disallowed;
   ++close_disallowed;
}

void
portalLayout_unlock(void) {
   --split_disallowed;
   --close_disallowed;
}

// When the portal layout cannot be changed give an error and return TRUE.
// "cmd" indicates the action being performed and is used to pick the relevant error message.
Boole
portalLayout_locked(CommIndex cmd) {
   if (split_disallowed > 0 || close_disallowed > 0) {
      if (close_disallowed == 0 && cmd == C_tabnew)
         emsg(_(e_cannot_split_portal_when_closing_buffer));
      else
         emsg(_(e_not_allowed_to_change_portal_layout_in_this_autocmd));
      return true;
   }
   return false;
}

// Check if the current portal is allowed to move to a different book.
// If the portal has 'portFixBuf', this function will return FALSE.
Boole
portCheckCanSetCurBookDisabled(void) {
   if (curPor->o.portFixBuf) {
      emsg(_(e_portfixbuf_cannot_go_to_buffer));
      return false;
   }
   return true;
}

// Check if the current portal is allowed to move to a different book.
// If the portal has 'portFixBuf', then forceit must be TRUE or this function will return FALSE.
Boole
portCheckCanSetCurBookForceIt(Boole forceit) {
   if (!forceit && curPor->o.portFixBuf) {
      emsg(_(e_portfixbuf_cannot_go_to_buffer));
      return false;
   }
   return true;
}

// Return the current portal, unless in the cmdline portal and "prevPor" is set, then "prevPor"
Portal*
prevPor_curPor(void) {
   // In commPort, the alternative book should be used.
   return inCommPort() && prevPor != NULL ? prevPor : curPor;
}

//If the @switchbook option contains "useopen" or "loadTab", then try to jump to a portal 
//containing "book". Return the pointer to the portal that was jumped to or NULL.
Portal*
switchBufGotoPortalIntoBuf(Book* book) {
   if (!book)
      return null;

   Portal* po = null;
   // If 'switchbook' contains "useopen": jump to first portal in the current
   // tab containing "buf" if one exists.
   if ((p_swb & SWB_USEOPEN) != 0)
      po = portTryFindOpenBook(book);

   // If 'switchbook' contains "loadTab": jump to 1st portal in any tab containing "buf" if one exists
   if (!po && (p_swb & SWB_USETAB) != 0)
      po = buf_jump_open_tab(book);

   return po;
}

// All CTRL-W portal commands are handled here, called from normal_cmd().
void
doPortal(int nchar, long prenum, Unt xchar) { // extra char from ":wincmd gx" or ZERO
   long   prenum1;
   Portal* po;
   CS ptr;
   LineNr lnum = -1;
   Unt type = FIND_DEFINE;
   int len;
   Byte cbuf[40];

   if (portErrorIfPopup(true))
      return;

#define CHECK_COMMPORT \
    do { \
   if (commPortTypeG != 0) \
   { \
       emsg(_(e_invalid_in_commline_portal)); \
       return; \
   } \
    } while (0)

   prenum1 = prenum == 0 ? 1 : prenum;

   switch (nchar) {
   // split current portal in two parts, horizontally
   case 'S':
   case Ctrl_S:
   case 's':
      CHECK_COMMPORT;
      reset_VIsual_and_resel();   // stop Visual mode
      // When splitting the location portal open a new book in it,
      // don't replicate the location book.
      if (isLocationListBook(curBook))
         goto newPortal;
      (void)splitPortal((int)prenum, 0);
      break;

   // split current portal in two parts, vertically
   case Ctrl_V:
   case 'v':
      CHECK_COMMPORT;
      reset_VIsual_and_resel();   // stop Visual mode
      // When splitting the location portal open a new book in it,
      // don't replicate the location book.
      if (isLocationListBook(curBook))
         goto newPortal;
      (void)splitPortal((int)prenum, WSP_VERT);
      break;

   // split current portal and edit alternate file
   case Ctrl_HAT:
   case '^':
      CHECK_COMMPORT;
      reset_VIsual_and_resel();   // stop Visual mode

      if (bookFindFileByBookNr(prenum == 0 ? curPor->altFnum : prenum) == NULL) {
         if (prenum == 0)
            emsg(_(e_no_alternate_file));
         else
            showErrFmtMsg(_(e_book_nr_not_found), prenum);
         break;
      }

      if (!curBookLocked() && splitPortal(0, 0) == OK)
          (void)booklistGetFile(
             prenum == 0 ? curPor->altFnum : prenum,
             (LineNr)0, GETF_ALT, FALSE);
      break;

   // open new portal
   case Ctrl_N:
   case 'n':
      CHECK_COMMPORT;
      reset_VIsual_and_resel();   // stop Visual mode
newPortal:
      if (prenum)
          // portal height
          eeSnprintf(cbuf, sizeof(cbuf) - 5, "%ld", prenum);
      else
          cbuf[0] = ZERO;
      if (nchar == 'v' || nchar == Ctrl_V)
          STRCAT(cbuf, "v");
      STRCAT(cbuf, "new");
      executeCommLine(cbuf);
      break;

   // quit current portal
   case Ctrl_Q:
   case 'q':
      reset_VIsual_and_resel();   // stop Visual mode
      cmd_with_count((CS)"quit", cbuf, sizeof(cbuf), prenum);
      executeCommLine(cbuf);
      break;

   // close current portal
   case Ctrl_C:
   case 'c':
      reset_VIsual_and_resel();   // stop Visual mode
      cmd_with_count((CS)"close", cbuf, sizeof(cbuf), prenum);
      executeCommLine(cbuf);
      break;

   // close preview portal
   case Ctrl_Z:
   case 'z':
      CHECK_COMMPORT;
      reset_VIsual_and_resel();   // stop Visual mode
      executeCommLine((CS)"pclose");
      break;

   // cursor to preview portal
   case 'P':
      FOR_ALL_PORTALS(po) {
         if (po->isPreview)
            break;
      } 
      if (!curtab->previewPortal)
         emsg(_(e_there_is_no_preview_portal));
      else
         gotoPortal(curtab->previewPortal);
      break;

   // close all but current portal
   case Ctrl_O:
   case 'o':
      CHECK_COMMPORT;
      reset_VIsual_and_resel();   // stop Visual mode
      cmd_with_count((CS)"only", cbuf, sizeof(cbuf), prenum);
      executeCommLine(cbuf);
      break;

   // cursor to next portal with wrap around
   case Ctrl_W:
   case 'w':
   // cursor to previous portal with wrap around
   case 'W':
      CHECK_COMMPORT;
      if (ONLY_ONE_PORTAL && prenum != 1)   // just one portal
          beep_flush();
      else {
         if (prenum) {        // go to specified portal
            for (po = firstPor; --prenum > 0; ) {
               if (po->next == NULL)
                  break;
               else
                  po = po->next;
            }
         } else {
            if (nchar == 'W') {      // go to previous portal
               po = curPor->prev;
               if (po == NULL)
                  po = lastPor;       // wrap around
            } else {            // go to next portal
               po = curPor->next;
               if (!po)
                  po = firstPor;       // wrap around
            }
          }
          gotoPortal(po);
      }
      break;

   // cursor to portal below
   case 'j':
   case K_DOWN:
   case Ctrl_J:
      CHECK_COMMPORT;
      gotoPortal_ver(FALSE, prenum1);
      break;

   // cursor to portal above
   case 'k':
   case K_UP:
   case Ctrl_K:
      CHECK_COMMPORT;
      gotoPortal_ver(TRUE, prenum1);
      break;

   // cursor to left portal
   case 'h':
   case K_LEFT:
   case Ctrl_H:
   case K_BS:
      CHECK_COMMPORT;
      gotoPortal_hor(TRUE, prenum1);
      break;

   // cursor to right portal
   case 'l':
   case K_RIGHT:
   case Ctrl_L:
      CHECK_COMMPORT;
      gotoPortal_hor(FALSE, prenum1);
      break;

   // move portal to new tab
   case 'T':
      CHECK_COMMPORT;
      if (onePortal())
          msg(_(m_onlyone));
      else {
          Tab   *oldtab = curtab;
          Tab   *newtab;

          // First create a new tab with the portal, then go back to
          // the old tab and close the portal there.
          po = curPor;
         if (portNewTab((int)prenum) == OK && isTabValid(oldtab)) {
            newtab = curtab;
            gotoTab(oldtab, TRUE, TRUE);
            if (curPor == po)
                closePortal(curPor, FALSE);
            if (isTabValid(newtab))
                gotoTab(newtab, TRUE, TRUE);
          }
      }
      break;

   // cursor to top-left portal
   case 't':
   case Ctrl_T:
      gotoPortal(firstPor);
      break;

   // cursor to bottom-right portal
   case 'b':
   case Ctrl_B:
      gotoPortal(lastPor);
      break;

   // cursor to last accessed (previous) portal
   case 'p':
   case Ctrl_P:
      if (!portalIsValid(prevPor))
          beep_flush();
      else
          gotoPortal(prevPor);
      break;

   // exchange current and next portal
   case 'x':
   case Ctrl_X:
      CHECK_COMMPORT;
      exchangePortal(prenum);
      break;

   // rotate portal downwards
    case Ctrl_R:
    case 'r':
      CHECK_COMMPORT;
      reset_VIsual_and_resel();   // stop Visual mode
      rotatePortals(FALSE, (int)prenum1);    // downwards
      break;

   // rotate portals upwards
   case 'R':
      CHECK_COMMPORT;
      reset_VIsual_and_resel();   // stop Visual mode
      rotatePortals(TRUE, (int)prenum1);       // upwards
      break;

   // move portal to the very top/bottom/left/right
    case 'K':
    case 'J':
    case 'H':
    case 'L':
      CHECK_COMMPORT;
      if (ONLY_ONE_PORTAL)
          beep_flush();
      else {
          int dir = ((nchar == 'H' || nchar == 'L') ? WSP_VERT : 0)
             | ((nchar == 'H' || nchar == 'K') ? WSP_TOP : WSP_BOT);
          (void)splitPortalmove(curPor, (int)prenum, dir);
      }
      break;

// make all portals the same width and/or height
    case '=': {
          int mod = commModifierG.cmod_split & (WSP_VERT | WSP_HOR);
          portEqualizeHeight(NULL, FALSE, mod == WSP_VERT ? 'v' : mod == WSP_HOR ? 'h' : 'b');
      }
      break;

   // increase current portal height
   case '+':
      portSetHeight(curPor->height + (int)prenum1, curPor);
      break;

   // decrease current portal height
   case '-':
      portSetHeight(curPor->height - (int)prenum1, curPor);
      break;

   // set current portal height
   case Ctrl__:
   case '_':
      portSetHeight(prenum ? (int)prenum : 9999, curPor);
      break;

   // increase current portal width
   case '>':
      portSetWidth(curPor->width + (int)prenum1, curPor);
      break;

   // decrease current portal width
   case '<':
      portSetWidth(curPor->width - (int)prenum1, curPor);
      break;

// set current portal width
    case '|':
      portSetWidth(prenum != 0 ? (int)prenum : 9999, curPor);
      break;

// jump to tag and split portal if tag exists (in preview portal)
    case '}':
      CHECK_COMMPORT;
      if (prenum)
          g_do_tagpreview = prenum;
      else
          g_do_tagpreview = p_pvh;
      // FALLTHROUGH
    case ']':
    case Ctrl_RSB:
      CHECK_COMMPORT;
      // keep Visual mode, can select words to use as a tag
      if (prenum)
          postponed_split = prenum;
      else
          postponed_split = -1;
      if (nchar != '}')
          g_do_tagpreview = 0;

      // Execute the command right here, required when "wincmd ]"
      // was used in a function.
      do_nv_ident(Ctrl_RSB, ZERO);
      postponed_split = 0;
      break;

// edit file name under cursor in a new portal
   case 'f':
   case 'F':
   case Ctrl_F:
portGotoFile:
      CHECK_COMMPORT;
      if (check_text_or_curbuf_locked(NULL))
         break;

      ptr = grab_file_name(prenum1, &lnum);
      if (ptr) {
          Tab* oldtab = curtab;
          Portal* oldPortal = curPor;
          setpcmark();

         // If 'switchbook' is set to 'useopen' or 'loadTab' and the
         // file is already opened in a portal, then jump to it.
         po = NULL;
         if ((p_swb & (SWB_USEOPEN | SWB_USETAB)) != 0 && commModifierG.cmod_tab == 0)
            po = switchBufGotoPortalIntoBuf(booklistFindByNameExpandingLinks(ptr));

         if (po == NULL && splitPortal(0, 0) == OK) {
            RESET_BINDING(curPor);
            if (startEditingFile(0, ptr, NULL, NULL, ECMD_LASTL, ECMD_HIDE, NULL) == FAIL) {
               // Failed to open the file, close the portal opened for it.
               closePortal(curPor, FALSE);
               goto_tab_port(oldtab, oldPortal);
            } else
               po = curPor;
         }

         if (po && nchar == 'F' && lnum >= 0) {
            curPor->cursor.lnum = lnum;
            check_cursor_lnum();
            beginline(BL_SOL | BL_FIX);
         }
         eeglFree(ptr);
      }
      break;

   // Go to the first occurrence of the identifier under cursor along path in a new portal -- webb
   case 'i':             // Go to any match
   case Ctrl_I:
      type = FIND_ANY;
      // FALLTHROUGH
   case 'd':             // Go to definition, using 'define'
   case Ctrl_D:
      CHECK_COMMPORT;
      if ((len = find_ident_under_cursor(&ptr, FIND_IDENT)) == 0)
          break;

      // Make a copy, if the line was changed it will be freed.
      ptr = copySubstr(ptr, len);

      find_pattern_in_path(
         ptr, 0, len, TRUE, prenum == 0 ? TRUE : FALSE, type,
         prenum1, ACTION_SPLIT, (LineNr)1, (LineNr)MAXLNUM, FALSE, FALSE
      );
      eeglFree(ptr);
      curPor->setCursWant = true;
      break;

   // Quickfix portal only: view the result under the cursor in a new split.
   case K_KENTER:
   case ENTER:
      if (isLocationListBook(curBook))
         llViewLocation(TRUE);
      break;

// CTRL-W g  extended commands
   case 'g':
   case Ctrl_G:
      CHECK_COMMPORT;
      ++no_mapping;
      ++allow_keys;   // no mapping for xchar, but allow key codes
      if (xchar == ZERO)
         xchar = plain_vgetc();
      LANGMAP_ADJUST(xchar, true);
      --no_mapping;
      --allow_keys;
      (void)add_to_showcmd(xchar);

      switch (xchar) {
      case '}':
         xchar = Ctrl_RSB;
         if (prenum)
             g_do_tagpreview = prenum;
         else
             g_do_tagpreview = p_pvh;
         // FALLTHROUGH
      case ']':
      case Ctrl_RSB:
         // keep Visual mode, can select words to use as a tag
         if (prenum)
            postponed_split = prenum;
         else
            postponed_split = -1;

         // Execute the command right here, required when "wincmd g}" was used in a function
         do_nv_ident('g', xchar);
         postponed_split = 0;
         break;

      case 'f':       // CTRL-W gf: "gf" in a new tab
      case 'F':       // CTRL-W gF: "gF" in a new tab
         commModifierG.cmod_tab = indexOfTab(curtab) + 1;
         nchar = xchar;
         goto portGotoFile;

      case 't':       // CTRL-W gt: go to next tab
         gotoTabById((int)prenum);
         break;

      case 'T':       // CTRL-W gT: go to previous tab
         gotoTabById(-(int)prenum1);
         break;

      case TAB:       // CTRL-W g<Tab>: go to last used tab
         if (goto_tabpage_lastused() == FAIL)
            beep_flush();
         break;

      default:
         beep_flush();
         break;
      }
      break;

    default:   beep_flush();
      break;
   }
}

// Figure out the address type for ":wincmd".
void
getPortCommAddressType(CS arg, Invocation* invo) {
   switch (*arg) {
   case 'S':
   case Ctrl_S:
   case 's':
   case Ctrl_N:
   case 'n':
   case 'j':
   case Ctrl_J:
   case 'k':
   case Ctrl_K:
   case 'T':
   case Ctrl_R:
   case 'r':
   case 'R':
   case 'K':
   case 'J':
   case '+':
   case '-':
   case Ctrl__:
   case '_':
   case '|':
   case ']':
   case Ctrl_RSB:
   case 'g':
   case Ctrl_G:
   case Ctrl_V:
   case 'v':
   case 'h':
   case Ctrl_H:
   case 'l':
   case Ctrl_L:
   case 'H':
   case 'L':
   case '>':
   case '<':
   case '}':
   case 'f':
   case 'F':
   case Ctrl_F:
   case 'i':
   case Ctrl_I:
   case 'd':
   case Ctrl_D:
     // portal size or any count
     invo->addressKind = ADDR_OTHER;
     break;

   case Ctrl_HAT:
   case '^':
     // book number
     invo->addressKind = ADDR_BUFFERS;
     break;

   case Ctrl_Q:
   case 'q':
   case Ctrl_C:
   case 'c':
   case Ctrl_O:
   case 'o':
   case Ctrl_W:
   case 'w':
   case 'W':
   case 'x':
   case Ctrl_X:
     // Portal number
     invo->addressKind = ADDR_PORTALS;
     break;

   case Ctrl_Z:
   case 'z':
   case 'P':
   case 't':
   case Ctrl_T:
   case 'b':
   case Ctrl_B:
   case 'p':
   case Ctrl_P:
   case '=':
   case ENTER:
     // no count
     invo->addressKind = ADDR_NONE;
     break;
  }
}

private void
cmd_with_count(
   CS cmd,
   CS bufp,
   Unt   bufsize,
   long   prenum)
{
   if (prenum > 0)
      eeSnprintf(bufp, bufsize, "%s %ld", cmd, prenum);
   else
      STRCPY(bufp, cmd);
}

// If "split_disallowed" is set, or "po"'s book is closing, give an error and return FAIL.  
// Otherwise return OK.
int
check_split_disallowed(Portal* po) {
   if (split_disallowed > 0) {
      emsg(_(e_cant_split_portal_while_closing_another));
      return FAIL;
   }
   if (po->book->lockedSplit) {
      emsg(_(e_cannot_split_portal_when_closing_buffer));
      return FAIL;
   }
   return OK;
}

//Split the current portal. Implement CTRL-W s and :split
//
//"size" is the height or width for the new portal, 0 to use half of current height or width.
//
//"flags":
//WSP_ROOM: require enough room for new portal
//WSP_VERT: vertical split.
//WSP_TOP:  open portal at the top-left of the shell (help portal).
//WSP_BOT:  open portal at the bottom-right of the shell (quickfix portal).
//WSP_HELP: creating the help portal, keep layout snapshot
//
//return FAIL for failure, OK otherwise
int
splitPortal(int size, int flags) {
   if (portErrorIfPopup(true) || check_split_disallowed(curPor) == FAIL)
      return FAIL;

   // When the ":tab" modifier was used open a new tab instead.
   if (mayOpenTab() == OK)
      return OK;

   // Add flags from ":vertical", ":topleft" and ":botright".
   flags |= commModifierG.cmod_split;
   if ((flags & WSP_TOP) && (flags & WSP_BOT)) {
      emsg(_(e_cant_split_topleft_and_botright_at_the_same_time));
      return FAIL;
   }

   // When creating the help portal make a snapshot of the portal layout.
   // Otherwise clear the snapshot, it's now invalid.
   if (flags & WSP_HELP)
      make_snapshot(SNAP_HELP_IDX);
   else
      clearSnapshot(curtab, SNAP_HELP_IDX);

   return splitPortal_ins(size, flags, NULL, 0, NULL);
}

//When "newPort" is NULL: split the current portal in two.
//When "newPort" is not NULL: insert this portal at the far
//top/left/right/bottom.
//When "to_flatten" is not NULL: flatten this frame before reorganising frames;
//remains unflattened on failure.
//
//On failure, if "newPort" was not NULL, no changes will have been made to the
//portal layout or sizes. Return FAIL for failure, OK otherwise.
int
splitPortal_ins(
   int      size,
   int      flags,
   Portal* newPort,
   int dir,
   Frame* to_flatten)
{
   Portal* po = newPort;
   Portal* oldPortal;
   int      new_size = size;
   int      i;
   int      need_status = 0;
   int      do_equal = FALSE;
   int      needed;
   int      available;
   int      oldPortal_height = 0;
   int      layout;
   Frame   *fr, *curfrp, *fr2, *prevfrp;
   int      before;
   int      minheight;
   int      wmh1;
   int      did_set_fraction = FALSE;
   int      retval = FAIL;

   // Do not redraw here, curPor->book may be invalid.
   ++isRedrawingDisabledG;

   if (!newPort)
      triggerPortalNewPre();

   if (flags & WSP_TOP)
      oldPortal = firstPor;
   ei (flags & WSP_BOT)
      oldPortal = lastPor;
   else
      oldPortal = curPor;

   // add a status line when splitting the first portal
   if (ONLY_ONE_PORTAL && oldPortal->statusHeight == 0) {
      if (!(flags & WSP_FORCE_ROOM) && VISIBLE_HEIGHT(oldPortal) <= MIN_PORTAL_HEIGHT) {
         emsg(_(e_not_enough_room));
         goto theend;
      }
      need_status = STATUS_HEIGHT;
   }

   if (flags & WSP_VERT) {
      int   wmw1;
      int   minwidth;

      layout = FR_ROW;

      //Check if we are able to split the current portal and compute its width.
      // Current portal requires at least 1 space.
      wmw1 = MIN_PORTAL_WIDTH;
      needed = wmw1 + 1;
      if (flags & WSP_ROOM)
         needed += p_wiw - wmw1;
      if (flags & (WSP_BOT | WSP_TOP)) {
         minwidth = frame_minwidth(topframeG, NOPORT);
         available = topframeG->width;
         needed += minwidth;
      } ei (p_ea) {
         minwidth = frame_minwidth(oldPortal->frame, NOPORT);
         prevfrp = oldPortal->frame;
         for (fr = oldPortal->frame->parent; fr != NULL; fr = fr->parent) {
            if (fr->layout == FR_ROW)
               FOR_ALL_FRAMES(fr2, fr->child) {
                  if (fr2 != prevfrp)
                     minwidth += frame_minwidth(fr2, NOPORT);
               } 
            prevfrp = fr;
         }
         available = topframeG->width;
         needed += minwidth;
      } else {
         minwidth = frame_minwidth(oldPortal->frame, NOPORT);
         available = oldPortal->frame->width;
         needed += minwidth;
      }
      if (!(flags & WSP_FORCE_ROOM) && available < needed) {
         emsg(_(e_not_enough_room));
         goto theend;
      }
      if (new_size == 0)
         new_size = oldPortal->width / 2;
      if (new_size > available - minwidth - 1)
         new_size = available - minwidth - 1;
      if (new_size < wmw1)
         new_size = wmw1;

      // if it doesn't fit in the current portal, need portEqualizeHeight()
      if (oldPortal->width - new_size - 1 < MIN_PORTAL_WIDTH)
         do_equal = TRUE;

      // We don't like to take lines for the new portal from a
      // 'portfixwidth' portal.  Take them from a portal to the left or right
      // instead, if possible. Add one for the separator.
      if (oldPortal->o.portFixWidth)
         portSetWidth(oldPortal->width + new_size + 1, oldPortal);

      // Only make all portals the same width if one of them (except oldPortal)
      // is wider than one of the split portals.
      if (!do_equal && p_ea && size == 0 && *p_ead != 'v' && oldPortal->frame->parent != NULL) {
         fr = oldPortal->frame->parent->child;
         while (fr) {
            if (fr->port != oldPortal && fr->port
                  && ((int)fr->port->width > new_size
                      || fr->port->width > oldPortal->width - new_size - 1)
            ) {
               do_equal = TRUE;
               break;
            }
            fr = fr->next;
         }
      }
   } else {
      layout = FR_COL;

      // Check if we are able to split the current portal and compute its height.
      // Current portal requires at least 1 space.
      wmh1 = MIN_PORTAL_HEIGHT;
      needed = wmh1 + STATUS_HEIGHT;
      if (flags & WSP_ROOM)
          needed += p_wh - wmh1;
      if (flags & (WSP_BOT | WSP_TOP)) {
          minheight = frame_minheight(topframeG, NOPORT) + need_status;
          available = topframeG->height;
          needed += minheight;
      } ei (p_ea) {
         minheight = frame_minheight(oldPortal->frame, NOPORT) + need_status;
         prevfrp = oldPortal->frame;
         for (fr = oldPortal->frame->parent; fr != NULL; fr = fr->parent) {
            if (fr->layout == FR_COL) {
               FOR_ALL_FRAMES(fr2, fr->child) {
                  if (fr2 != prevfrp)
                     minheight += frame_minheight(fr2, NOPORT);
               } 
            } 
            prevfrp = fr;
         }
         available = topframeG->height;
         needed += minheight;
      } else {
          minheight = frame_minheight(oldPortal->frame, NOPORT) + need_status;
          available = oldPortal->frame->height;
          needed += minheight;
      }
      if (!(flags & WSP_FORCE_ROOM) && available < needed) {
          emsg(_(e_not_enough_room));
          goto theend;
      }
      oldPortal_height = oldPortal->height;
      if (need_status) {
          oldPortal->statusHeight = STATUS_HEIGHT;
          oldPortal_height -= STATUS_HEIGHT;
      }
      if (new_size == 0)
          new_size = oldPortal_height / 2;
      if (new_size > available - minheight - STATUS_HEIGHT)
          new_size = available - minheight - STATUS_HEIGHT;
      if (new_size < wmh1)
          new_size = wmh1;

      // if it doesn't fit in the current portal, need portEqualizeHeight()
      if (oldPortal_height - new_size - STATUS_HEIGHT < MIN_PORTAL_HEIGHT)
          do_equal = TRUE;

      // We don't like to take lines for the new portal from a
      // 'portfixheight' portal.  Take them from a portal above or below instead, if possible.
      if (oldPortal->o.portFixHeight) {
         // Set fraction now so that the cursor keeps the same relative
         // vertical position using the old height.
         set_fraction(oldPortal);
         did_set_fraction = TRUE;

         portSetHeight(oldPortal->height + new_size + STATUS_HEIGHT, oldPortal);
         oldPortal_height = oldPortal->height;
         if (need_status)
            oldPortal_height -= STATUS_HEIGHT;
      }

      // Only make all portals the same height if one of them (except oldPortal)
      // is higher than one of the split portals.
      if (!do_equal && p_ea && size == 0 && *p_ead != 'h'
         && oldPortal->frame->parent != NULL)
      {
         fr = oldPortal->frame->parent->child;
         while (fr) {
            if (fr->port != oldPortal && fr->port != NULL
               && ((int)fr->port->height > new_size
                   || (int)fr->port->height > oldPortal_height - new_size - STATUS_HEIGHT)
            ) {
               do_equal = TRUE;
               break;
            }
            fr = fr->next;
         }
      }
   }

   // allocate new portal structure and link it in the portal list
   if ((flags & WSP_TOP) == 0
       && ((flags & WSP_BOT)
            || (flags & WSP_BELOW)
            || (!(flags & WSP_ABOVE)
                && ( (flags & WSP_VERT) ? p_spr : p_sb)))
   ) {
      // new portal below/right of current one
      if (!newPort)
         po = allocPortal(oldPortal, FALSE);
      else
         append(oldPortal, po);
   } else {
      if (!newPort)
         po = allocPortal(oldPortal->prev, FALSE);
      else
         append(oldPortal->prev, po);
   }

   if (!newPort) {
      if (!po)
         goto theend;

      neframe(po);
      if (!po->frame) {
         freePortal(po, NULL);
         goto theend;
      }

      // make the contents of the new portal the same as the current one
      init(po, curPor, flags);
   }

   // Going to reorganize frames now, make sure they're flat.
   if (to_flatten != NULL)
   frame_flatten(to_flatten);

   //Reorganize the tree of frames to insert the new portal.
   if (flags & (WSP_TOP | WSP_BOT)) {
      if ((topframeG->layout == FR_COL && (flags & WSP_VERT) == 0)
          || (topframeG->layout == FR_ROW && (flags & WSP_VERT) != 0)
      ){
         curfrp = topframeG->child;
         if (flags & WSP_BOT) {
            while (curfrp->next)
               curfrp = curfrp->next;
         } 
      } else
         curfrp = topframeG;
      before = (flags & WSP_TOP);
   } else {
   curfrp = oldPortal->frame;
   if (flags & WSP_BELOW)
       before = FALSE;
   ei (flags & WSP_ABOVE)
       before = TRUE;
   ei (flags & WSP_VERT)
       before = !p_spr;
   else
       before = !p_sb;
   }
   if (curfrp->parent == NULL || curfrp->parent->layout != layout) {
      // Need to create a new frame in the tree to make a branch.
      fr = ALLOC_CLEAR_ONE(Frame);
      *fr = *curfrp;
      curfrp->layout = layout;
      fr->parent = curfrp;
      fr->next = NULL;
      fr->prev = NULL;
      curfrp->child = fr;
      curfrp->port = NULL;
      curfrp = fr;
      if (fr->port != NULL)
         oldPortal->frame = fr;
      else {
         FOR_ALL_FRAMES(fr, fr->child) {
            fr->parent = curfrp;
         } 
      } 
   }

   if (!newPort)
      fr = po->frame;
   else
      fr = newPort->frame;
   fr->parent = curfrp->parent;

   // Insert the new frame at the right place in the frame list.
   if (before)
      frame_insert(curfrp, fr);
   else
      frame_append(curfrp, fr);

   // Set fraction now so that the cursor keeps the same relative
   // vertical position.
   if (!did_set_fraction)
      set_fraction(oldPortal);
   po->fraction = oldPortal->fraction;

   if (flags & WSP_VERT) {
      po->scroll = curPor->scroll;

      if (need_status) {
         portalNewHeight(oldPortal, oldPortal->height - 1);
         oldPortal->statusHeight = need_status;
      }
      if (flags & (WSP_TOP | WSP_BOT)) {
         // set height and row of new portal to full height
         po->portalRow = 0;
         portalNewHeight(po, curfrp->height - 1);
         po->statusHeight = 1;
      } else {
         // height and row of new portal is same as current portal
         po->portalRow = oldPortal->portalRow;
         portalNewHeight(po, VISIBLE_HEIGHT(oldPortal));
         po->statusHeight = oldPortal->statusHeight;
      }
      fr->height = curfrp->height;

      // "new_size" of the current portal goes to the new portal, use
      // one column for the vertical separator
      portalNewWidth(po, new_size);
      if (before)
          po->vsepWidth = 1;
      else {
          po->vsepWidth = oldPortal->vsepWidth;
          oldPortal->vsepWidth = 1;
      }
      if (flags & (WSP_TOP | WSP_BOT)) {
          if (flags & WSP_BOT)
         frame_add_vsep(curfrp);
          // Set width of neighbor frame
          frameNewWidth(curfrp, curfrp->width
              - (new_size + ((flags & WSP_TOP) != 0)), flags & WSP_TOP, FALSE);
      }
      else
          portalNewWidth(oldPortal, oldPortal->width - (new_size + 1));
      if (before) {  // new portal left of current one
          po->portalCol = oldPortal->portalCol;
          oldPortal->portalCol += new_size + 1;
      } else      // new portal right of current one
          po->portalCol = oldPortal->portalCol + oldPortal->width + 1;
      frame_fix_width(oldPortal);
      frame_fix_width(po);
   } else {
      // width and column of new portal is same as current portal
      if (flags & (WSP_TOP | WSP_BOT)) {
          po->portalCol = firstPor->portalCol;
          portalNewWidth(po, topframeG->width);
          po->vsepWidth = 0;
      } else {
          po->portalCol = oldPortal->portalCol;
          portalNewWidth(po, oldPortal->width);
          po->vsepWidth = oldPortal->vsepWidth;
      }
      fr->width = curfrp->width;

      // "new_size" of the current portal goes to the new portal, use
      // one row for the status line
      portalNewHeight(po, new_size);
      int old_status_height = oldPortal->statusHeight;
      if (flags & (WSP_TOP | WSP_BOT)) {
         int new_height = curfrp->height - new_size;

         new_height -= STATUS_HEIGHT;
         if (flags & WSP_BOT)
            frame_add_statusline(curfrp);
         frame_new_height(curfrp, new_height, flags & WSP_TOP, FALSE, FALSE);
      } else
         portalNewHeight(oldPortal, oldPortal_height - (new_size + STATUS_HEIGHT));
      if (before) {   // new portal above current one
         po->portalRow = oldPortal->portalRow;
         po->statusHeight = STATUS_HEIGHT;
         oldPortal->portalRow += po->height + STATUS_HEIGHT;
      } else {     // new portal below current one
         po->portalRow = oldPortal->portalRow + VISIBLE_HEIGHT(oldPortal) + STATUS_HEIGHT;
         po->statusHeight = old_status_height;
         if (!(flags & WSP_BOT))
            oldPortal->statusHeight = STATUS_HEIGHT;
      }
      frame_fix_height(po);
      frame_fix_height(oldPortal);
   }

   if (flags & (WSP_TOP | WSP_BOT))
      computePosPortal();

    // Both portals need redrawing.  Update all status lines, in case they
    // show something related to the portal count or position.
    redrawPortLater(po, UPD_NOT_VALID);
    redrawPortLater(oldPortal, UPD_NOT_VALID);
    status_redraw_all();

   if (need_status) {
      msgRowG = visibleRowsG - 1;
      msgColG = sc_col;
      msg_clr_eos_force();   // Old command/ruler may still be there
      computeColumnsForRulerAndCommand();
      msgRowG = visibleRowsG - 1;
      msgColG = 0;   // put position back at start of line
   }

   // equalize the portal sizes.
   if (do_equal || dir != 0)
      portEqualizeHeight(po, TRUE, (flags & WSP_VERT) ? (dir == 'v' ? 'b' : 'h') : dir == 'h' ? 'b' : 'v');

   // Don't change the portal height/width to 'winheight' / 'winwidth' if a size was given.
   if (flags & WSP_VERT) {
      i = p_wiw;
      if (size != 0)
         p_wiw = size;
    } else {
      i = p_wh;
      if (size != 0)
         p_wh = size;
    }

   //make the new portal the current portal
   (void)enterPortalWorker(po, (newPort == NULL ? WEE_TRIGGER_NEW_AUTOCMDS : 0)
          | WEE_TRIGGER_ENTER_AUTOCMDS | WEE_TRIGGER_LEAVE_AUTOCMDS);
   if (flags & WSP_VERT)
      p_wiw = i;
   else
      p_wh = i;
   retval = OK;

theend:
   if (isRedrawingDisabledG > 0)
      --isRedrawingDisabledG;
   return retval;
}


// Initialize portal "newp" from portal "oldp". Used when splitting a portal and when creating a 
// new tab. The portals will both edit the same book. WSP_NEWLOC may be specified in flags to 
// prevent the location list from being copied.
private void
init(Portal* newp, Portal* oldp, Unt flags UNUSED) {
   newp->book = oldp->book;
   newp->ownSyntax = &(oldp->book->syntax);
   oldp->book->countPortals++;
   newp->cursor = oldp->cursor;
   newp->cacheState = 0;
   newp->cursWant = oldp->cursWant;
   newp->setCursWant = oldp->setCursWant;
   newp->topLine = oldp->topLine;
   newp->topFill = oldp->topFill;
   newp->leftCol = oldp->leftCol;
   newp->prevContextMark = oldp->prevContextMark;
   newp->prevPrevContextMark = oldp->prevPrevContextMark;
   newp->altFnum = oldp->altFnum;
   newp->cursorRow = oldp->cursorRow;
   newp->fraction = oldp->fraction;
   newp->prevFraction = oldp->prevFraction;
   copy_jumplist(oldp, newp);
   newp->locationStackRef = NULL;
   newp->localDir = (oldp->localDir == NULL) ? NULL : copyStr(oldp->localDir);
   newp->prevdir = (oldp->prevdir == NULL) ? NULL : copyStr(oldp->prevdir);

   // copy tagstack and folds
   for (Unt i = 0; i < oldp->tagStackLen; i++) {
      Taggy* tag = &newp->tagStack[i];
      *tag = oldp->tagStack[i];
      if (tag->tagname != NULL)
         tag->tagname = copyStr(tag->tagname);
      if (tag->user_data != NULL)
         tag->user_data = copyStr(tag->user_data);
   }
   newp->tagStackInd = oldp->tagStackInd;
   newp->tagStackLen = oldp->tagStackLen;

   // Keep same changelist position in new portal.
   newp->changeListInd = oldp->changeListInd;

   copyFoldingState(oldp, newp);

   initg_some(newp, oldp);
   termUpdatePortcolor(newp);
}

//Initialize portal "newp" from portal "old". Only the essential things are copied.
private void
initg_some(Portal *newp, Portal *oldp) {
   // Use the same argument list.
   newp->argList = oldp->argList;
   ++newp->argList->al_refcount;
   newp->argListInd = oldp->argListInd;

   // copy options from existing portal
   portCopyOptions(newp, oldp);
}

// Return TRUE if "port" is a global popup or a popup in the current tab
int
portalValidPopup(Portal *port) {
   Portal   *po;
   FOR_ALL_POPUPPORTS(po) {
      if (po == port)
          return TRUE;
   } 
   FOR_ALL_POPUPPORTS_IN_TAB(curtab, po) {
      if (po == port)
          return TRUE;
   } 
   return FALSE;
}

// Check if "port" is a pointer to an existing portal in the current tab
int
portalIsValid(Portal* port) {
   if (!port)
      return FALSE;
      
   Portal   *po;
   FOR_ALL_PORTALS(po) {
      if (po == port)
          return TRUE;
   } 
   return portalValidPopup(port);
}

// Find portal "id" in the current tab. Also find popup portals. Return NULL if not found.
Portal *
portFindById(int id) {
   Portal   *po;

   FOR_ALL_PORTALS(po) {
      if (po->id == id)
          return po;
   } 
   FOR_ALL_POPUPPORTS(po) {
      if (po->id == id)
          return po;
   } 
   FOR_ALL_POPUPPORTS_IN_TAB(curtab, po) {
      if (po->id == id)
          return po;
   } 
   return NULL;
}

// Check if "port" is a pointer to an existing portal in any tab
int
doesPortalExistInAnyTab(Portal *port) {
   if (!port)
      return FALSE;
   Portal   *po;
   Tab   *t;
   FOR_ALL_TABS(t) {
      FOR_ALL_PORTALS_IN_TAB(t, po) {
         if (po == port)
            return TRUE;
      }
      FOR_ALL_POPUPPORTS_IN_TAB(t, po)
         if (po == port)
            return TRUE;
   }
   return portalValidPopup(port);
}

// Return the number of portals.
int
portCount(void) {
   int      count = 0;

   Portal   *po;
   FOR_ALL_PORTALS(po)
      ++count;
   return count;
}

// Make "count" portals on the screen. Return actual number of portals on the screen.
// Must be called when there is just one portal, filling the whole screen (excluding the command
// line).
int
makePortals(int count, int      vertical) {
   int      maxcount;

   if (vertical) {
      // Each portal needs at least MIN_PORTAL_WIDTH lines and a separator column.
      maxcount = (curPor->width + curPor->vsepWidth - (p_wiw - MIN_PORTAL_WIDTH)) 
         / (MIN_PORTAL_WIDTH + 1);
   } else {
      // Each portal needs at least MIN_PORTAL_HEIGHT lines and a status line.
      maxcount = (VISIBLE_HEIGHT(curPor) + curPor->statusHeight
                 - (p_wh - MIN_PORTAL_HEIGHT)) / (MIN_PORTAL_HEIGHT + STATUS_HEIGHT);
   }

   if (maxcount < 2)
      maxcount = 2;
   if (count > maxcount)
      count = maxcount;

   // add status line now, otherwise first portal will be too big
   if (count > 1)
      last_status(TRUE);

   // Don't execute autocommands while creating the portals.  Must do that
   // when putting the books in the portals.
   block_autocmds();
   
   int todo; // number of portals left to create
   for (todo = count - 1; todo > 0; --todo) {
      if (vertical) {
         if (splitPortal(curPor->width - (curPor->width - todo)
                  / (todo + 1) - 1, WSP_VERT | WSP_ABOVE) == FAIL)
            break;
      } else {
         if (splitPortal(
                  curPor->height - (curPor->height - todo * STATUS_HEIGHT) / (todo + 1) 
                     - STATUS_HEIGHT, 
                  WSP_ABOVE
               ) == FAIL)
            break;
      }
   } 

   unblock_autocmds();

   // return actual number of portals
   return (count - todo);
}

// Exchange current and next portal
private void
exchangePortal(long prenum) {
   Frame* fr;
   Frame* fr2;
   Portal* po;
   Portal* wp2;
   int temp;

   if (portErrorIfPopup(true))
      return;
   if (ONLY_ONE_PORTAL) {      // just one portal
      beep_flush();
      return;
   }
   if (text_or_buf_locked()) {
      beep_flush();
      return;
   }

   // find portal to exchange with
   if (prenum) {
      fr = curPor->frame->parent->child;
      while (fr && --prenum > 0)
         fr = fr->next;
   } ei (curPor->frame->next)   // Swap with next
      fr = curPor->frame->next;
   else    // Swap last portal in row/col with previous
      fr = curPor->frame->prev;

   // We can only exchange a portal with another portal, not with a frame
   // containing portals.
   if (!fr || !fr->port || fr->port == curPor)
      return;
   po = fr->port;

   //1. remove curPor from the list. Remember after which portal it was in wp2
   //2. insert curPor before po in the list
   //if po != wp2
   //   3. remove po from the list
   //   4. insert po after wp2
   //5. exchange the status line height and vsep width.
   wp2 = curPor->prev;
   fr2 = curPor->frame->prev;
   if (po->prev != curPor) {
      removePortal(curPor, NULL);
      frame_remove(curPor->frame);
      append(po->prev, curPor);
      frame_insert(fr, curPor->frame);
   }
   if (po != wp2) {
      removePortal(po, NULL);
      frame_remove(po->frame);
      append(wp2, po);
      if (fr2 == NULL)
         frame_insert(po->frame->parent->child, po->frame);
      else
         frame_append(fr2, po->frame);
   }
   temp = curPor->statusHeight;
   curPor->statusHeight = po->statusHeight;
   po->statusHeight = temp;
   temp = curPor->vsepWidth;
   curPor->vsepWidth = po->vsepWidth;
   po->vsepWidth = temp;

   frame_fix_height(curPor);
   frame_fix_height(po);
   frame_fix_width(curPor);
   frame_fix_width(po);

   computePosPortal();      // recompute portal positions

   if (po->book != curBook)
      reset_VIsual_and_resel();
   ei (VIsual_active)
      po->cursor = curPor->cursor;

   enterPortal(po, TRUE);
   redraw_all_later(UPD_NOT_VALID);
}

//rotate portals: if upwards TRUE the second portal becomes the first one
//        if upwards FALSE the first portal becomes the second one
private void
rotatePortals(int upwards, int count) {
   Portal   *wp1;
   Portal   *wp2;
   Frame   *fr;
   int      n;

   if (ONLY_ONE_PORTAL) {     // nothing to do
      beep_flush();
      return;
   }

   // Check if all frames in this row/col have one portal.
   FOR_ALL_FRAMES(fr, curPor->frame->parent->child) {
      if (!fr->port) {
         emsg(_(e_cannot_rotate_when_another_portal_is_split));
         return;
      }
   } 

   while (count--) {
      if (upwards) {     // first portal becomes last portal
         // remove first portal/frame from the list
         fr = curPor->frame->parent->child;
         wp1 = fr->port;
         removePortal(wp1, NULL);
         frame_remove(fr);

         // find last frame and append removed portal/frame after it
         for ( ; fr->next != NULL; fr = fr->next)
            {}
         append(fr->port, wp1);
         frame_append(fr, wp1->frame);

         wp2 = fr->port;      // previously last portal
      } else  {       // last portal becomes first portal
         // find last portal/frame in the list and remove it
         for (fr = curPor->frame; fr->next != NULL; fr = fr->next)
            {} 
         wp1 = fr->port;
         wp2 = wp1->prev;          // will become last portal
         removePortal(wp1, NULL);
         frame_remove(fr);

         // append the removed portal/frame before the first in the list
         append(fr->parent->child->port->prev, wp1);
         frame_insert(fr->parent->child, fr);
      }

      // exchange status height and vsep width of old and new last portal
      n = wp2->statusHeight;
      wp2->statusHeight = wp1->statusHeight;
      wp1->statusHeight = n;
      frame_fix_height(wp1);
      frame_fix_height(wp2);
      n = wp2->vsepWidth;
      wp2->vsepWidth = wp1->vsepWidth;
      wp1->vsepWidth = n;
      frame_fix_width(wp1);
      frame_fix_width(wp2);

      // recompute portalRow and portalCol for all portals
      computePosPortal();
   }

   redraw_all_later(UPD_NOT_VALID);
}

//Move "po" into a new split in a given direction, possibly relative to the current portal.
//"po" must be valid in the current tabpage.
//Returns FAIL for failure, OK otherwise.
int
splitPortalmove(Portal* po, int size, Unt flags) {
   int      height = po->height;

   if (ONLY_ONE_PORTAL)
      return OK;   // nothing to do
   if (check_split_disallowed(po) == FAIL)
      return FAIL;

   // Remove the portal and frame from the tree of frames.  Don't flatten any
   // frames yet so we can restore things if splitPortal_ins fails.
   Frame* unflat_altfr;
   Unt dir;
   portRemoveFrame(po, OUT &dir, NULL, OUT &unflat_altfr);
   removePortal(po, NULL);
   last_status(FALSE);       // may need to remove last status line
   computePosPortal();   // recompute portal positions

   // Split a portal on the desired side and put "po" there.
   if (splitPortal_ins(size, flags, po, dir, unflat_altfr) == FAIL) {
      // splitPortal_ins doesn't change sizes or layout if it fails to insert an
      // existing portal, so just undo portRemoveFrame.
      restoreFrame(po, dir, unflat_altfr);
      append(po->prev, po);
      return FAIL;
   }

   // If splitting horizontally, try to preserve height.
   // Note that splitPortal_ins autocommands may have immediately closed "po"!
   if (size == 0 && !(flags & WSP_VERT) && portalIsValid(po)) {
      portSetHeight(height, po);
      if (p_ea) {
          // Equalize portals.  Note that splitPortal_ins autocommands may have
          // made a portal other than "po" current.
          portEqualizeHeight(curPor, curPor == po, 'v');
      }
   }
   return OK;
}

//Move portal "port0" to below/right of "port1" and make "port0" the current
//portal. Only works within the same frame!
void
portMoveAfter(Portal* port0, Portal* port1) {
   int height;

   // check if the arguments are reasonable
   if (port0 == port1)
      return;

   // check if there is something to do
   if (port1->next != port0) {
      if (port0->frame->parent != port1->frame->parent) {
          internalErrMsg((CS)"Trying to move a portal into another frame");
          return;
      }

      // may need to move the status line/vertical separator of the last portal
      if (port0 == lastPor) {
         height = port0->prev->statusHeight;
         port0->prev->statusHeight = port0->statusHeight;
         port0->statusHeight = height;
         if (port0->prev->vsepWidth == 1) {
            // Remove the vertical separator from the last-but-one portal,
            // add it to the last portal.  Adjust the frame widths.
            port0->prev->vsepWidth = 0;
            port0->prev->frame->width -= 1;
            port0->vsepWidth = 1;
            port0->frame->width += 1;
         }
      } ei (port1 == lastPor) {
         height = port0->statusHeight;
         port0->statusHeight = port1->statusHeight;
         port1->statusHeight = height;
         if (port0->vsepWidth == 1) {
            // Remove the vertical separator from port0, add it to the last
            // portal, port1.  Adjust the frame widths.
            port1->vsepWidth = 1;
            port1->frame->width++;
            port0->vsepWidth = 0;
            port0->frame->width--;
         }
      }
      removePortal(port0, NULL);
      frame_remove(port0->frame);
      append(port1, port0);
      frame_append(port1->frame, port0->frame);

      computePosPortal();   // recompute portalRow for all portals
      redraw_later(UPD_NOT_VALID);
   }
   enterPortal(port0, FALSE);
}

// Make all portals the same height.
// 'next_curPor' will soon be the current portal, make sure it has enough rows.
void
portEqualizeHeight(
   Portal* next_curPor,   // pointer to current portal to be or NULL
   int      current,   // do only frame with current portal
   Unt      dir   // 'v' for vertically, 'h' for horizontally, 'b' for both, 0 for using p_ead
){
   if (dir == 0)
      dir = *p_ead;
   equalizeHeightRec(
      next_curPor ? next_curPor : curPor, 
      current, topframeG, dir, firstPor->portalCol, 0, topframeG->width, topframeG->height
   );
}

// Set a frame to a new position and height, spreading the available room equally over contained 
// frames. The portal "next_curPor" (if not NULL) should at least get the size from 
// 'winheight' and 'winwidth' if possible.
private void
equalizeHeightRec(
   Portal* next_curPor,   // pointer to current portal to be or NULL
   int current,   // do only frame with current portal
   Frame* topfr,      // frame to set size off
   int dir,      // 'v', 'h' or 'b', see portEqualizeHeight()
   int col,      // horizontal position for frame
   int row,      // vertical position for frame
   int width,      // new width of frame
   int height      // new height of frame
){
   int n, m;
   int extra_sep = 0;
   int portCount, totalPortCount = 0;
   Frame* fr;
   int next_curPor_size = 0;
   int room = 0;
   int new_size;
   int has_next_curPor = 0;
   int hnc;

   if (topfr->layout == FR_LEAF) {
      // Set the width/height of this frame.
      // Redraw when size or position changes
      if (topfr->height != (Unt)height || topfr->port->portalRow != row
         || topfr->width != (Unt)width || topfr->port->portalCol != col
      ) {
          topfr->port->portalRow = row;
          frame_new_height(topfr, height, FALSE, FALSE, FALSE);
          topfr->port->portalCol = col;
          frameNewWidth(topfr, width, FALSE, FALSE);
          redraw_all_later(UPD_NOT_VALID);
      }
   } ei (topfr->layout == FR_ROW) {
      topfr->width = width;
      topfr->height = height;

      if (dir != 'v') {        // equalize frame widths
         // Compute the maximum number of portals horizontally in this frame.
         n = frame_minwidth(topfr, NOPORT);
         // add one for the rightmost portal, it doesn't have a separator
         if (col + width >= firstPor->portalCol + (int)topframeG->width)
            extra_sep = 1;
         else
            extra_sep = 0;
         totalPortCount = (n + extra_sep) / (MIN_PORTAL_WIDTH + 1);
         has_next_curPor = frameHasPortal(topfr, next_curPor);

         //Compute width for "next_curPor" portal and room available for other portals.
         //"m" is the minimal width when counting p_wiw for "next_curPor".
         m = frame_minwidth(topfr, next_curPor);
         room = width - m;
         if (room < 0) {
            next_curPor_size = p_wiw + room;
            room = 0;
         } else {
            next_curPor_size = -1;
            FOR_ALL_FRAMES(fr, topfr->child) {
               if (!frame_fixed_width(fr))
                  continue;
               // If 'portfixwidth' set keep the portal width if possible.
               // Watch out for this portal being the next_curPor.
               n = frame_minwidth(fr, NOPORT);
               new_size = fr->width;
               if (frameHasPortal(fr, next_curPor)) {
                  room += p_wiw - MIN_PORTAL_WIDTH;
                  next_curPor_size = 0;
                  if (new_size < p_wiw)
                      new_size = p_wiw;
               } else
                  // These portals don't use up room.
                  totalPortCount -= (n + (fr->next == NULL ? extra_sep : 0)) 
                                    / (MIN_PORTAL_WIDTH + 1);
               room -= new_size - n;
               if (room < 0) {
                  new_size += room;
                  room = 0;
               }
               fr->newWidth = new_size;
            }
            if (next_curPor_size == -1) {
               if (!has_next_curPor)
                  next_curPor_size = 0;
               ei (totalPortCount > 1
                   && (room + (totalPortCount - 2))
                          / (totalPortCount - 1) > p_wiw)
                {
               // Can make all portals wider than 'winwidth', spread the room equally.
               next_curPor_size = (room + p_wiw
                         + (totalPortCount - 1) * MIN_PORTAL_WIDTH
                         + (totalPortCount - 1)) / totalPortCount;
               room -= next_curPor_size - p_wiw;
               } else
                  next_curPor_size = p_wiw;
            }
         }

         if (has_next_curPor)
            --totalPortCount;      // don't count curPor
      }

      FOR_ALL_FRAMES(fr, topfr->child) {
         portCount = 1;
         if (fr->next == NULL)
            // last frame gets all that remains (avoid roundoff error)
            new_size = width;
         ei (dir == 'v')
            new_size = fr->width;
         ei (frame_fixed_width(fr)) {
            new_size = fr->newWidth;
            portCount = 0;       // doesn't count as a sizeable portal
         } else {
            // Compute the maximum number of portals horiz. in "fr".
            n = frame_minwidth(fr, NOPORT);
            portCount = (n + (fr->next == NULL ? extra_sep : 0)) / (MIN_PORTAL_WIDTH + 1);
            m = frame_minwidth(fr, next_curPor);
            if (has_next_curPor)
               hnc = frameHasPortal(fr, next_curPor);
            else
               hnc = FALSE;
            if (hnc)       // don't count next_curPor
               --portCount;
            if (totalPortCount == 0)
               new_size = room;
            else
               new_size = (portCount * room + ((unsigned)totalPortCount >> 1)) / totalPortCount;
            if (hnc) {      // add next_curPor size
               next_curPor_size -= p_wiw - (m - n);
               if (next_curPor_size < 0)
                  next_curPor_size = 0;
               new_size += next_curPor_size;
               room -= new_size - next_curPor_size;
            } else
               room -= new_size;
            new_size += n;
         }

         // Skip frame that is full width when splitting or closing a portal, unless equalizing all 
         // frames
         if (!current || dir != 'v' || topfr->parent != NULL
             || (new_size != (int)fr->width)
             || frameHasPortal(fr, next_curPor))
            equalizeHeightRec(next_curPor, current, fr, dir, col, row, new_size, height);
         col += new_size;
         width -= new_size;
         totalPortCount -= portCount;
      }
   } else { // topfr->layout == FR_COL
      topfr->width = width;
      topfr->height = height;

      if (dir != 'h') {        // equalize frame heights
         // Compute maximum number of portals vertically in this frame.
         n = frame_minheight(topfr, NOPORT);
         // add one for the bottom portal if it doesn't have a statusline
         extra_sep = 0;
         totalPortCount = (n + extra_sep) / (MIN_PORTAL_HEIGHT + 1);
         has_next_curPor = frameHasPortal(topfr, next_curPor);

         //Compute height for "next_curPor" portal and room available for other portals.
         //"m" is the minimal height when counting p_wh for "next_curPor".
         m = frame_minheight(topfr, next_curPor);
         room = height - m;
         if (room < 0) {
            // The room is less than 'winheight', use all space for the current portal.
            next_curPor_size = p_wh + room;
            room = 0;
         } else {
            next_curPor_size = -1;
            FOR_ALL_FRAMES(fr, topfr->child) {
               if (!frame_fixed_height(fr))
                  continue;
               // If 'portfixheight' set, keep the portal height if possible.
               // Watch out for this portal being the next_curPor.
               n = frame_minheight(fr, NOPORT);
               new_size = fr->height;
               if (frameHasPortal(fr, next_curPor)) {
                  room += p_wh - MIN_PORTAL_HEIGHT;
                  next_curPor_size = 0;
                  if (new_size < p_wh)
                      new_size = p_wh;
               } else
               // These portals don't use up room.
               totalPortCount -= (n + (fr->next == NULL ? extra_sep : 0)) / (MIN_PORTAL_HEIGHT + 1);
               room -= new_size - n;
               if (room < 0) {
                  new_size += room;
                  room = 0;
               }
               fr->newHeight = new_size;
            }
            if (next_curPor_size == -1) {
               if (!has_next_curPor)
                  next_curPor_size = 0;
               ei (totalPortCount > 1 && (room + (totalPortCount - 2)) / (totalPortCount - 1) > p_wh) {
                  // can make all portals higher than 'winheight', spread the room equally.
                  next_curPor_size = (room + p_wh
                           + (totalPortCount - 1) * MIN_PORTAL_HEIGHT
                           + (totalPortCount - 1)) / totalPortCount;
                  room -= next_curPor_size - p_wh;
               } else
                  next_curPor_size = p_wh;
            }
         }

         if (has_next_curPor)
            --totalPortCount;      // don't count curPor
      }

      FOR_ALL_FRAMES(fr, topfr->child) {
         portCount = 1;
         if (fr->next == NULL)
            // last frame gets all that remains (avoid roundoff error)
            new_size = height;
         ei (dir == 'h')
            new_size = fr->height;
         ei (frame_fixed_height(fr)) {
            new_size = fr->newHeight;
            portCount = 0;       // doesn't count as a sizeable portal
         } else {
            // Compute the maximum number of portals vert. in "fr".
            n = frame_minheight(fr, NOPORT);
            portCount = (n + (fr->next == NULL ? extra_sep : 0)) / (MIN_PORTAL_HEIGHT + 1);
            m = frame_minheight(fr, next_curPor);
            if (has_next_curPor)
               hnc = frameHasPortal(fr, next_curPor);
            else
               hnc = FALSE;
            if (hnc)       // don't count next_curPor
               --portCount;
            if (totalPortCount == 0)
               new_size = room;
            else
               new_size = (portCount * room + ((unsigned)totalPortCount >> 1)) / totalPortCount;
            if (hnc) {      // add next_curPor size
               next_curPor_size -= p_wh - (m - n);
               new_size += next_curPor_size;
               room -= new_size - next_curPor_size;
            } else
                room -= new_size;
            new_size += n;
         }
         // Skip full-width frame when splitting or closing a portal, unless equalizing all frames
         if (!current || dir != 'h' || topfr->parent != NULL
                || (new_size != (int)fr->height)
                || frameHasPortal(fr, next_curPor))
            equalizeHeightRec(next_curPor, current, fr, dir, col, row, width, new_size);
         row += new_size;
         height -= new_size;
         totalPortCount -= portCount;
      }
   }
}

void
leavingPortal(Portal* port) {
   // Only matters for a prompt portal
   if (!bt_prompt(port->book))
      return;

   // When leaving a prompt portal stop Insert mode and perhaps restart
   // it when entering that portal again.
   port->book->promptInsert = restart_edit;
   if (restart_edit != 0 && isModeDisplayedG)
      mustClearCommlineG = TRUE;      // unshow mode later
   restart_edit = ZERO;

   // When leaving the portal (or closing it) was done from a callback, we need to break out of 
   // the Insert mode loop and restart Insert mode when entering the portal again.
   if ((stateG & MODE_INSERT) && !stop_insert_mode) {
      stop_insert_mode = TRUE;
      if (port->book->promptInsert == ZERO)
          port->book->promptInsert = 'A';
   }
}

void
enteringPortal(Portal* port) {
   // Only matters for a prompt portal.
   if (!bt_prompt(port->book))
      return;

   // When switching to a prompt book that was in Insert mode, don't stop
   // Insert mode, it may have been set in leavingPortal().
      if (port->book->promptInsert != ZERO)
      stop_insert_mode = FALSE;

   // When entering the prompt portal restart Insert mode if we were in Insert
   // mode when we left it and not already in Insert mode.
   if ((stateG & MODE_INSERT) == 0)
      restart_edit = port->book->promptInsert;
}

private void
initEmptyPortal(Portal* po) {
   redrawPortLater(po, UPD_NOT_VALID);
   po->validLines = 0;
   po->cursor.lnum = 1;
   po->cursWant = po->cursor.col = 0;
   po->cursor.coladd = 0;
   po->prevContextMark.lnum = 1;   // pcmark not cleared but set to line 1
   po->prevContextMark.col = 0;
   po->prevPrevContextMark.lnum = 0;
   po->prevPrevContextMark.col = 0;
   po->topLine = 1;
   po->topFill = 0;
   po->bottomLine = 2;
   po->cacheState = 0;
   po->ownSyntax = &po->book->syntax;
   termResetPortcolor(po);
}

// Init the current portal "curPor". Called when a new file is being edited.
void
curPor_init(void) {
   initEmptyPortal(curPor);
}

// Close all portals into book "buf".
void
closePortalsInto(Book* book, Boole keep_curPor)  {     // don't close "curPor"
   Portal   *po;
   Unt count = indexOfTab(NULL);

   ++isRedrawingDisabledG;

   for (po = firstPor; po && !ONLY_ONE_PORTAL; ) {
      if (po->book == book && (!keep_curPor || po != curPor)
         && !(portalLocked(po) || po->book->locked > 0))
      {
         if (closePortal(po, FALSE) == FAIL)
            // If closing the portal fails give up, to avoid looping forever.
            break;

         // Start all over, autocommands may change the portal layout.
         po = firstPor;
      } else
         po = po->next;
   }

   // Also check portals in other tabs
   Tab* nexttp;
   for (Tab* t = firstTabG; t != NULL; t = nexttp) {
      nexttp = t->next;
      if (t == curtab) {
         continue;
      } 
      FOR_ALL_PORTALS_IN_TAB(t, po) {
         if (po->book == book && !(portalLocked(po) || po->book->locked > 0)) {
            closePortal_othertab(po, FALSE, t);

            // Start all over, the tab may be closed and
            // autocommands may change the portal layout.
            nexttp = firstTabG;
            break;
         }
      }
   }

   if (isRedrawingDisabledG > 0)
      --isRedrawingDisabledG;

   if (count != indexOfTab(NULL))
      apply_autocmds(EVENT_TABCLOSED, NULL, NULL, false, curBook);
}

//Return TRUE if the current portal is the only portal that exists (ignoring "autoCommPortG[]").
//FALSE if there is a portal, possibly in another tab.
int
lastPortal(void) {
   return (onePortal() && firstTabG->next == NULL);
}

// Return TRUE if there is only one portal other than "autoCommPortG[]" in the current tab.
int
onePortal(void) {
   Portal* po;
   Boole seen_one = false;
   FOR_ALL_PORTALS(po) {
      if (!is_autoCommPort(po)) {
         if (seen_one)
            return false;
         seen_one = true;
      }
   }
   return TRUE;
}

//Close the possibly last portal in a tab.
//Return FALSE if there are other portals and nothing is done, TRUE otherwise.
private int
closeLastPortalInTab(Portal* port, int free_buf, Tab* prev_curtab) {
   if (!ONLY_ONE_PORTAL)
      return FALSE;
      
   Book* oldCurBook = curBook;

   //Closing the last portal in a tab.  First go to another tab
   //page and then close the portal and the tab.  This avoids that
   //curPor and curtab are invalid while we are freeing memory, they may
   //be used in GUI events.
   //Don't trigger autocommands yet, they may use wrong values, so do that below.
   gotoTab(altTab(), FALSE, TRUE);

   // Safety check: Autocommands may have closed the portal when jumping
   // to the other tab.
   if (isTabValid(prev_curtab) && prev_curtab->firstPor == port)
      closePortal_othertab(port, free_buf, prev_curtab);
   enteringPortal(curPor);
   // Since gotoTab above did not trigger *Enter autocommands, do that now.
   apply_autocmds(EVENT_TABCLOSED, NULL, NULL, false, curBook);
   if (p_stpl)
      shell_new_columns();
   apply_autocmds(EVENT_PORTENTER, NULL, NULL, false, curBook);
   apply_autocmds(EVENT_TABENTER, NULL, NULL, false, curBook);
   if (oldCurBook != curBook)
      apply_autocmds(EVENT_BUFENTER, NULL, NULL, false, curBook);
   return TRUE;
}

//Close the book of "port" and unload it if "action" is DOBOOK_UNLOAD.
//"action" can also be zero (do nothing) or DOBUF_WIPE.
//"abort_if_last" is passed to closeBook(): abort closing if all other portals are closed.
private void
closePortalBook(Portal* port, int action, int abort_if_last) {
   // Free independent synblock before the book is freed.
   if (port->book)
      reset_synblock(port);

   //When a quickfix/location list portal is closed and the book is
   //displayed in only one portal, then unlist the book.
   if (port->book != NULL && isLocationListBook(port->book) && port->book->countPortals == 1)
      port->book->o.bookListed = false;

   //Close the link to the book.
   if (port->book) {
      BookRef bufref;
      bookStoreInRef(OUT &bufref, curBook);
      
      port->locked = TRUE;
      closeBook(port, port->book, action, abort_if_last, TRUE);
      if (doesPortalExistInAnyTab(port))
          port->locked = FALSE;
      // Make sure curBook is valid. It could become invalid if 'bufhidden' is "wipe" (can it now?)
      if (!bookRefValid(&bufref))
          curBook = firstBook;
    }
}

//Close portal "port".  Only works for the current tab. If "free_buf" is TRUE related book may 
//be unloaded.
//Called by :quit, :close, :xit, :wq and findtag(). Returns FAIL when the portal was not closed.
Unt
closePortal(Portal* port, int free_buf) {
   Portal* po;
   Boole isOtherBook = false;
   Boole close_curPor = false;
   Boole isHelpPortal = false;
   Tab* prev_curtab = curtab;
   Frame* portFrame = port->frame->parent;
   int had_diffmode = port->o.diff;
   Boole did_decrement = false;

   // Can close a popup portal with a terminal if the job has finished.
   if (may_close_term_popup() == OK)
      return OK;
   if (portErrorIfPopup(true))
      return FAIL;

   if (lastPortal()) {
      emsg(_(e_cannot_close_last_portal));
      return FAIL;
   }
   if (portalLayout_locked(C_close))
      return FAIL;

   if (portalLocked(port) || (port->book != NULL && port->book->locked > 0))
      return FAIL; // portal is already being closed
      
   if (portUnlisted(port)) {
      emsg(_(e_cannot_close_autocmd_or_popup_portal));
      return FAIL;
   }
   if ((is_autoCommPort(firstPor) || is_autoCommPort(lastPor)) && onePortal()) {
      emsg(_(e_cannot_close_portal_only_autocomm_portal_would_remain));
      return FAIL;
   }

   // When closing the last portal in a tab first go to another tab 
   // and then close the portal and the tab to avoid that curPor and
   // curtab are invalid while we are freeing memory.
   if (closeLastPortalInTab(port, free_buf, prev_curtab))
      return FAIL;

   // When closing the help portal, try restoring a snapshot after closing
   // the portal.  Otherwise clear the snapshot, it's now invalid.
   if (bookIsHelp(port->book))
      isHelpPortal = true;
   else
      clearSnapshot(curtab, SNAP_HELP_IDX);

   if (port == curPor) {
      leavingPortal(curPor);
      //Guess which portal is going to be the new current portal.
      //This may change because of the autocommands (sigh).
      po = frameToPort(getAltFrame(port, NULL));

      //Be careful: If autocommands delete the portal or cause this portal
      //to be the last one left, return now.
      if (po->book != curBook) {
         reset_VIsual_and_resel();   // stop Visual mode

         isOtherBook = true;
         if (!portalIsValid(port))
            return FAIL;
         port->locked = TRUE;
         apply_autocmds(EVENT_BUFLEAVE, NULL, NULL, false, curBook);
         if (!portalIsValid(port))
            return FAIL;
         port->locked = FALSE;
         if (lastPortal())
            return FAIL;
      }
      port->locked = TRUE;
      apply_autocmds(EVENT_PORTLEAVE, NULL, NULL, false, curBook);
      if (!portalIsValid(port))
          return FAIL;
      port->locked = FALSE;
      if (lastPortal())
          return FAIL;
      // autocmds may abort script processing
      if (aborting())
          return FAIL;
   }

   if (popup_closePortal(port) && !portalIsValid(port))
      return FAIL;

   // Trigger WinClosed just before starting to free portal-related resources.
   triggerPortalClosed(port);
   // autocmd may have freed the portal already.
   if (!doesPortalExistInAnyTab(port))
      return OK;

   closePortalBook(port, free_buf ? DOBOOK_UNLOAD : 0, TRUE);

   if (portalIsValid(port) && port->book == NULL && !popup_is_popup(port) && lastPortal()) {
      // Autocommands have closed all portals, quit now.  Restore
      // curPor->book, otherwise writing eeglinfo may fail.
      if (curPor->book == NULL)
         curPor->book = curBook;
      exitEegl(0);
   }

   // Autocommands may have moved to another tab.
   if (curtab != prev_curtab && doesPortalExistInAnyTab(port) && port->book == NULL) {
      // Need to close the portal anyway, since the buffer is NULL.
      closePortal_othertab(port, FALSE, prev_curtab);
      return FAIL;
   }

   // Autocommands may have closed the portal already or closed the only
   // other portal.
   if (!portalIsValid(port) || lastPortal()
         || closeLastPortalInTab(port, free_buf, prev_curtab))
      return FAIL;

   // Now we are really going to close the portal.  Disallow any autocommand
   // to split a portal to avoid trouble.
   // Also bail out of parse_queued_messages() to avoid it tries to update the screen.
   ++split_disallowed;
   ++dont_parse_messages;

   // Free the memory used for the portal and get the portal that received the screen space.
   Unt dir;
   po = freePortalMem(port, OUT &dir, NULL);

   if (isHelpPortal) {
      // Closing the help portal moves the cursor back to the current portal of the snapshot.
      Portal* prevPort = get_snapshot_curPor(SNAP_HELP_IDX);

      if (portalIsValid(prevPort))
          po = prevPort;
   }

   // Make sure curPor isn't invalid.  It can cause severe trouble when
   // printing an error message.  For portEqualizeHeight() curBook needs to be valid too.
   if (port == curPor) {
      curPor = po;
      if (po->isPreview || isLocationListBook(po->book)) {
         //If the cursor goes to the preview or the quickfix portal, try
         //finding another portal to go to.
         for (;;) {
            if (po->next == NULL)
               po = firstPor;
            else
               po = po->next;
            if (po == curPor)
               break;
            if (!po->isPreview && !isLocationListBook(po->book)) {
               curPor = po;
               break;
            }
         }
      }
      curBook = curPor->book;
      close_curPor = TRUE;

      // The cursor position may be invalid if the buffer changed after last
      // using the portal.
      check_cursor();
   }

   //If last portal has a status line now and we don't want one, remove the
   //status line.  Do this before portEqualizeHeight(), because it may change the
   //height of a portal
   last_status(FALSE);

   if (p_ea && (*p_ead == 'b' || *p_ead == dir))
      // If the frame of the closed portal contains the new current portal,
      // only resize that frame.  Otherwise resize all portals.
      portEqualizeHeight(curPor, curPor->frame->parent == portFrame, dir);
   else {
      computePosPortal();
   }
   if (close_curPor) {
      // Pass WEE_ALLOW_PARSE_MESSAGES to decrement dont_parse_messages
      // before autocommands.
      did_decrement = enterPortalWorker(po,
            WEE_CURWIN_INVALID | WEE_TRIGGER_ENTER_AUTOCMDS
                  | WEE_TRIGGER_LEAVE_AUTOCMDS | WEE_ALLOW_PARSE_MESSAGES
         );
         if (isOtherBook)
             // careful: after this po and port may be invalid!
             apply_autocmds(EVENT_BUFENTER, NULL, NULL, false, curBook);
   }

   --split_disallowed;
   if (!did_decrement)
      --dont_parse_messages;

   // After closing the help portal, try restoring the portal layout from before it was opened.
   if (isHelpPortal)
      restore_snapshot(SNAP_HELP_IDX, close_curPor);

   // If the portal had 'diff' set and now there is only one portal left in
   // the tab with 'diff' set, and "closeoff" is in 'diffopt', then execute ":diffoff!".
   if (diffopt_closeoff() && had_diffmode && curtab == prev_curtab) {
      int   diffcount = 0;
      Portal   *dPort;
      FOR_ALL_PORTALS(dPort) {
         if (dPort->o.diff)
            ++diffcount;
      } 
      if (diffcount == 1)
         executeCommLine((CS)"diffoff!");
   }

   redraw_all_later(UPD_NOT_VALID);
   return OK;
}

private void
triggerPortalNewPre(void) {
   portalLayout_lock();
   apply_autocmds(EVENT_PORTNEWPRE, NULL, NULL, false, NULL);
   portalLayout_unlock();
}

private void
triggerPortalClosed(Portal* port) {
   static Boole recursive = false;
   Byte portId[NUMBUFLEN];

   if (recursive)
      return;
   recursive = true;
   eeSnprintf(portId, sizeof(portId), "%d", port->id);
   apply_autocmds(EVENT_WINCLOSED, portId, portId, false, port->book);
   recursive = false;
}

//directly is TRUE if the portal is closed by ':tabclose' or ':tabonly'.
//This allows saving the session before closing multi-portal tab.
void
trigger_tabclosedpre(Tab* t, int directly) {
   static Boole recursive = false;
   static Boole skip = false;
   Tab* ptp = curtab;

   // Quickly return when no TabClosedPre autocommands to be executed or already executing
   if (!has_tabclosedpre() || recursive)
      return;

   // Skip if the event have been triggered by ':tabclose' recently
   if (skip) {
      skip = FALSE;
      return;
   }

   if (isTabValid(t)) {
      gotoTab(t, FALSE, FALSE);
      if (directly)
         skip = TRUE;
   }
   recursive = true;
   portalLayout_lock();
   apply_autocmds(EVENT_TABCLOSEDPRE, NULL, NULL, false, NULL);
   portalLayout_unlock();
   recursive = false;
   // tabpage may have been modified or deleted by autocmds
   if (isTabValid(ptp))
      // try to recover the tappage first
      gotoTab(ptp, FALSE, FALSE);
   else
      // fall back to the first tappage
      gotoTab(firstTabG, FALSE, FALSE);
}

// Make a snapshot of all the portal scroll positions and sizes of the current tab
void
portSnapshotScrollSizes(void) {
   Portal* po;
   FOR_ALL_PORTALS(po) {
      po->lastTopline = po->topLine;
      po->lastTopFill = po->topFill;
      po->lastLeftCol = po->leftCol;
      po->lastSkipCol = po->skipCol;
      po->lastWidth = po->width;
      po->lastHeight = po->height;
   }
}

private int did_initial_scroll_size_snapshot = FALSE;

void
may_make_initial_scroll_size_snapshot(void) {
   if (!did_initial_scroll_size_snapshot) {
      did_initial_scroll_size_snapshot = TRUE;
      portSnapshotScrollSizes();
   }
}

//Create a dictionary with information about size and scroll changes in a portal.
//Return the dictionary with refcount set to one. NULL when out of memory.
private Bag *
makePortInfoDict(
   int width,
   int height,
   int topline,
   int topfill,
   int leftcol,
   int skipcol
) {
   Bag* d = allocBag();
   d->refcount = 1;

   // not actually looping, for breaking out on error
   while (1) {
      Var tv;
      tv.lock = 0;
      tv.tag = VAR_NUMBER;

      tv.number = width;
      if (bagAddVar(d, S"width", &tv) == FAIL)
         break;
      tv.number = height;
      if (bagAddVar(d, S"height", &tv) == FAIL)
         break;
      tv.number = topline;
      if (bagAddVar(d, S"topline", &tv) == FAIL)
         break;
      tv.number = topfill;
      if (bagAddVar(d, S"topfill", &tv) == FAIL)
         break;
      tv.number = leftcol;
      if (bagAddVar(d, S"leftcol", &tv) == FAIL)
         break;
      tv.number = skipcol;
      if (bagAddVar(d, S"skipcol", &tv) == FAIL)
         break;
      return d;
    }
    bagUnref(d);
    return NULL;
}

// Return values of checkWhichPortalsResized():
//#define CWSR_SCROLLED   1  // at least one portal scrolled
//#define CWSR_RESIZED   2  // at least one portal size changed

//This function is used for three purposes:
//1. Goes over all portals in the current tab and sets:
//   "size_count" to the nr of portals with size changes.
//   "firstScrollPort" to the first portal with any relevant changes.
//   "firstResizedPort" to the first portal with size changes.
//
//2. When the first three arguments are NULL but "portlist" is not,
//   "portlist" is set to the list of portal IDs with size changes.
//
//3. When the first three arguments are NULL but "v_event" is not,
//   information about changed portals is added to "v_event".
private void
checkWhichPortalsResized(
   int* size_count,
   Portal** firstScrollPort,
   Portal** firstResizedPort,
   List* portlist,
   OUT Bag* v_event)
{
   int listidx = 0;
   int tot_width = 0;
   int tot_height = 0;
   int tot_topline = 0;
   int tot_topfill = 0;
   int tot_leftcol = 0;
   int tot_skipcol = 0;

   Portal *po;
   FOR_ALL_PORTALS(po) {
      int ignore_scroll = event_ignored(EVENT_PORTSCROLLED, po->o.eventIgnorePort);
      int size_changed = !event_ignored(EVENT_WINRESIZED, po->o.eventIgnorePort)
                && (po->lastWidth != po->width || po->lastHeight != po->height);
      if (size_changed) {
         if (portlist) {
            // Add this portal to the list of changed portals.
            Var tv;
            tv.lock = 0;
            tv.tag = VAR_NUMBER;
            tv.number = po->id;
            list_set_item(portlist, listidx++, &tv);
         } ei (size_count != NULL) {
            ++*size_count;
            if (*firstResizedPort == NULL)
               *firstResizedPort = po;
            // For WinScrolled the first portal with a size change is used
            // even when it didn't scroll.
            if (*firstScrollPort == NULL && !ignore_scroll)
               *firstScrollPort = po;
         }
      }

      int scroll_changed = !ignore_scroll
               && (po->lastTopline != po->topLine
               || po->lastTopFill != po->topFill
               || po->lastLeftCol != po->leftCol
               || po->lastSkipCol != po->skipCol);
      if (scroll_changed
          && firstScrollPort != NULL && *firstScrollPort == NULL)
          *firstScrollPort = po;

      if ((size_changed || scroll_changed) && v_event) {
         // Add info about this portal to the v:event dictionary.
         int width = po->width - po->lastWidth;
         int height = po->height - po->lastHeight;
         int topline = po->topLine - po->lastTopline;
         int topfill = po->topFill - po->lastTopFill;
         int leftcol = po->leftCol - po->lastLeftCol;
         int skipcol = po->skipCol - po->lastSkipCol;
         Bag *d = makePortInfoDict(width, height, topline, topfill, leftcol, skipcol);
         if (!d)
            break;
         Byte portId[NUMBUFLEN];
         eeSnprintf(portId, sizeof(portId), "%d", po->id);
         if (bagAddBag(v_event, portId, d) == FAIL) {
            bagUnref(d);
            break;
         }
         --d->refcount;

         tot_width += abs(width);
         tot_height += abs(height);
         tot_topline += abs(topline);
         tot_topfill += abs(topfill);
         tot_leftcol += abs(leftcol);
         tot_skipcol += abs(skipcol);
      }
   }

   if (v_event) {
      Bag *alldict = makePortInfoDict(
         tot_width, tot_height, tot_topline, tot_topfill, tot_leftcol, tot_skipcol
      );
      if (alldict) {
         if (bagAddBag(v_event, S"all", alldict) == FAIL)
            bagUnref(alldict);
         else
            --alldict->refcount;
      }
   }
}

// Trigger WinScrolled and/or WinResized if any portal in the current tab scrolled or changed size
void
may_trigger_win_scrolled_resized(void) {
   static Boole recursive = false;
   int doResizeG = has_winresized();
   int do_scroll = has_winscrolled();

   //Do not trigger WinScrolled or WinResized recursively.  Do not trigger
   //before the initial snapshot of the w_last_ values was made.
   if (recursive || !(do_scroll || doResizeG) || !did_initial_scroll_size_snapshot)
      return;

   int size_count = 0;
   Portal* firstScrollPort = NULL, *firstResizedPort = NULL;
   checkWhichPortalsResized(&size_count, &firstScrollPort, &firstResizedPort, NULL, NULL);
   int trigger_resize = doResizeG && size_count > 0;
   int trigger_scroll = do_scroll && firstScrollPort != NULL;
   if (!trigger_resize && !trigger_scroll)
      return;  // no relevant changes
   List* portalsList = NULL;
   if (trigger_resize) {
      // Create the list for v:event.portals before making the snapshot.
      portalsList = list_alloc_with_items(size_count);
      if (portalsList)
         checkWhichPortalsResized(NULL, NULL, NULL, portalsList, NULL);
   }

   Bag* scroll_dict = NULL;
   if (trigger_scroll) {
      // Create the dict with entries for v:event before making the snapshot.
      scroll_dict = allocBag();
      scroll_dict->refcount = 1;
      checkWhichPortalsResized(NULL, NULL, NULL, NULL, scroll_dict);
   }

   //WinScrolled/WinResized are triggered only once, even when multiple
   //portals scrolled or changed size.  Store the current values before
   //triggering the event, if a scroll or resize happens as a side effect
   //then WinScrolled/WinResized is triggered for that later.
   portSnapshotScrollSizes();

   //"curPor" may be different from the actual current portal, make sure it can be restored
   portalLayout_lock();
   recursive = true;

   //If both are to be triggered do WinResized first.
   if (trigger_resize && portalsList) {
      SaveVEvent  save_v_event;
      Bag* v_event = get_v_event(&save_v_event);

      if (bagAddList(v_event, S"portals", portalsList) == OK) {
         bagSetItemsRo(v_event);
         Byte portId[NUMBUFLEN];
         eeSnprintf(portId, sizeof(portId), "%d", firstResizedPort->id);
         apply_autocmds(EVENT_WINRESIZED, portId, portId, false, firstResizedPort->book);
      }
      restore_v_event(v_event, &save_v_event);
   }

   if (trigger_scroll && scroll_dict) {
      SaveVEvent  save_v_event;
      Bag* v_event = get_v_event(&save_v_event);

      // Move the entries from scroll_dict to v_event.
      bagExtend(v_event, scroll_dict, (CS)"move");
      bagSetItemsRo(v_event);
      bagUnref(scroll_dict);
      Byte portId[NUMBUFLEN];
      eeSnprintf(portId, sizeof(portId), "%d", firstScrollPort->id);
      apply_autocmds(EVENT_PORTSCROLLED, portId, portId, false, firstScrollPort->book);
      restore_v_event(v_event, &save_v_event);
   }

   recursive = false;
   portalLayout_unlock();
}

// Close portal "port" in tab "t", which is not the current tab. This may be the last portal in 
// that tab and result in closing the tab, thus "t" may become invalid!
// Caller must check if buffer is hidden.
void
closePortal_othertab(Portal* port, int free_buf, Tab *t) {
   Portal* po;
   Tab* ptp = NULL;
   int      free_tp = FALSE;

   // Get here with port->book == NULL when closePortal() detects the tab
   // page changed.
   if (portalLocked(port) || (port->book != NULL && port->book->locked > 0))
      return; // portal is already being closed

   // Trigger WinClosed just before starting to free portal-related resources.
   // If the buffer is NULL, it isn't safe to trigger autocommands,
   // and closePortal() should have already triggered WinClosed.
   if (port->book != NULL) {
      triggerPortalClosed(port);
      // autocmd may have freed the portal already.
      if (!doesPortalExistInAnyTab(port))
         return;
   }

   if (t->firstPor == t->lastPor) {
      trigger_tabclosedpre(t, FALSE);
      // autocmd may have freed the portal already.
      if (!doesPortalExistInAnyTab(port))
         return;
   }

   if (port->book)
      // Close the link to the buffer.
      closeBook(port, port->book, free_buf ? DOBOOK_UNLOAD : 0, FALSE, TRUE);

   // Careful: Autocommands may have closed the tab or made it the current tab
   for (ptp = firstTabG; ptp != NULL && ptp != t; ptp = ptp->next)
      {}
   if (!ptp || t == curtab) {
      // If the buffer was removed from the portal we have to give it any buffer.
      if (doesPortalExistInAnyTab(port) && port->book == NULL) {
          port->book = firstBook;
          ++firstBook->countPortals;
          initEmptyPortal(port);
      }
      return;
   }

   // Autocommands may have closed the portal already.
   for (po = t->firstPor; po != NULL && po != port; po = po->next)
      {}
   if (!po)
      return;

   // When closing the last portal in a tab remove the tab
   if (t->firstPor == t->lastPor) {
      int h = 0;

      if (t == firstTabG)
         firstTabG = t->next;
      else {
         for (ptp = firstTabG; ptp != NULL && ptp->next != t; ptp = ptp->next)
            {}
         
         if (!ptp) {
            internal_error((CS)"closePortal_othertab()");
            return;
         }
         ptp->next = t->next;
      }
      free_tp = TRUE;
      shell_new_columns();
      if (h != 0)
         shell_new_rows();
   }

   // Free the memory used for the portal.
   Unt dir;
   freePortalMem(port, OUT &dir, t);

   if (free_tp)
      freeTab(t);
}

// Free the memory used for a portal. Returns a pointer to the portal that got the freed up space.
private Portal *
freePortalMem(
   Portal* port,
   OUT Unt* dirp,      // set to 'v' or 'h' for direction if 'ea'
   Tab* t      // tab "port" is in, NULL for current
){
   Tab* portTab = t == NULL ? curtab : t;

   // Remove the portal and its frame from the tree of frames.
   Frame* fr = port->frame;
   Portal* po = portRemoveFrame(port, OUT dirp, t, NULL);
   eeglFree(fr);
   freePortal(port, t);

   // When deleting the current portal in the tab, select a new current portal.
   if (port == portTab->curPor)
      portTab->curPor = po;

   return po;
}

#if defined(EXITFREE) || defined(PROTO)
void
portFreeAll(void) {
   // avoid an error for switching tabpage with the commline portal open
   commPortTypeG = 0;
   commPortBookG = NULL;
   commPortPortG = NULL;

   while (firstTabG->next)
      tabClose();

   Unt dummy;
   for (int i = 0; i < AUCMD_PORTAL_COUNT; ++i) {
      if (autoCommPortG[i].port) {
          (void)freePortalMem(autoCommPortG[i].port, &dummy, NULL);
          autoCommPortG[i].port = NULL;
      }
   }

   while (firstPor != NULL)
      (void)freePortalMem(firstPor, &dummy, NULL);

   // No portal should be used after this. Set curPor to NULL to crash instead of using freed memory
   curPor = NULL;
}
#endif

// Remove a portal and its frame from the tree of frames.
// Return a pointer to the portal that got the freed up space.
Portal *
portRemoveFrame(
   Portal* port,
   OUT Unt* dirp,      // set to 'v' or 'h' for direction if 'ea'
   Tab* t,      // tab "port" is in, NULL for current
   OUT Frame** unflat_altfr // if not NULL, set to pointer of frame that got the space, and it 
                           // is not flattened
){
   Frame* fr;
   Frame* frp3;
   Frame* frp_close = port->frame;
   int      row, col;

   // If there is only one portal there is nothing to remove.
   if (t == NULL ? ONLY_ONE_PORTAL : t->firstPor == t->lastPor)
      return NULL;

   // Save the position of the containing frame (which will also contain the
   // altframe) before we remove anything, to recompute portal positions later.
   Portal* po = frameToPort(frp_close->parent);
   row = po->portalRow;
   col = po->portalCol;

   // Remove the portal from its frame.
   Frame* fr2 = getAltFrame(port, t);
   po = frameToPort(fr2);

   // Remove this frame from the list of frames.
   frame_remove(frp_close);

   if (frp_close->parent->layout == FR_COL) {
      // When 'portfixheight' is set, try to find another frame in the column
      // (as close to the closed frame as possible) to distribute the height to.
      if (fr2->port != NULL && fr2->port->o.portFixHeight) {
         fr = frp_close->prev;
         frp3 = frp_close->next;
         while (fr || frp3) {
         if (fr) {
            if (!frame_fixed_height(fr)) {
               fr2 = fr;
               po = frameToPort(fr2);
               break;
            }
            fr = fr->prev;
         }
         if (frp3) {
            if (frp3->port != NULL && !frp3->port->o.portFixHeight) {
               fr2 = frp3;
               po = frp3->port;
               break;
            }
            frp3 = frp3->next;
         }
         }
      }
      frame_new_height(fr2, fr2->height + frp_close->height,
                fr2 == frp_close->next, FALSE, FALSE);
      *dirp = 'v';
   } else {
      // When 'portfixwidth' is set, try to find another frame in the column
      // (as close to the closed frame as possible) to distribute the width to.
      if (fr2->port != NULL && fr2->port->o.portFixWidth) {
         fr = frp_close->prev;
         frp3 = frp_close->next;
         while (fr || frp3) {
            if (fr) {
               if (!frame_fixed_width(fr)) {
                  fr2 = fr;
                  po = frameToPort(fr2);
                  break;
               }
               fr = fr->prev;
            }
            if (frp3) {
               if (frp3->port != NULL && !frp3->port->o.portFixWidth) {
                  fr2 = frp3;
                  po = frp3->port;
                  break;
               }
               frp3 = frp3->next;
            }
         }
      }
      frameNewWidth(fr2, fr2->width + frp_close->width, fr2 == frp_close->next, FALSE);
      *dirp = 'h';
   }

   // If the altframe wasn't adjacent and left/above, resizing it will have
   // changed portal positions within the parent frame.  Recompute them.
   if (fr2 != frp_close->prev)
      frame_comp_pos(frp_close->parent, &row, &col);

   if (!unflat_altfr)
      frame_flatten(fr2);
   else
      *unflat_altfr = fr2;

   return po;
}

// Flatten "fr" into its parent frame if it's the only child, also merging its
// list with the grandparent if they share the same layout.
// Free "fr" if flattened; also "fr->parent" if it has the same layout.
private void
frame_flatten(Frame* fr) {
   if (fr->next || fr->prev)
      return;

   // There is no other frame in this list, move its info to the parent and remove it.
   fr->parent->layout = fr->layout;
   fr->parent->child = fr->child;
   Frame* fr2; 
   FOR_ALL_FRAMES(fr2, fr->child) {
      fr2->parent = fr->parent;
   } 
   fr->parent->port = fr->port;
   if (fr->port)
      fr->port->frame = fr->parent;
      
   fr2 = fr->parent;
   if (topframeG->child == fr)
      topframeG->child = fr2;
   eeglFree(fr);

   fr = fr2->parent;
   if (fr && fr->layout == fr2->layout) {
      // The frame above the parent has the same layout, have to merge
      // the frames into this list.
      if (fr->child == fr2)
         fr->child = fr2->child;
      fr2->child->prev = fr2->prev;
      if (fr2->prev != NULL)
          fr2->prev->next = fr2->child;
      for (Frame* frp3 = fr2->child; ; frp3 = frp3->next) {
         frp3->parent = fr;
         if (!frp3->next) {
            frp3->next = fr2->next;
            if (fr2->next)
               fr2->next->prev = frp3;
            break;
         }
      }
      if (topframeG->child == fr2)
          topframeG->child = fr;
      eeglFree(fr2);
   }
}

//Undo changes from a prior call to portRemoveFrame, also restoring lost vertical separators and 
//statuslines, and changed portal positions for portals within "unflat_altfr".
//Caller must ensure no other changes were made to the layout or portal sizes!
private void
restoreFrame(Portal* po, int dir, Frame* unflat_altfr) {
   Frame* fr = po->frame;

   // Put "po"'s frame back where it was.
   if (fr->prev != NULL)
      frame_append(fr->prev, fr);
   else
      frame_insert(fr->next, fr);

   // Vertical separators to the left may have been lost.  Restore them.
   if (po->vsepWidth == 0 && fr->parent->layout == FR_ROW && fr->prev)
      frame_add_vsep(fr->prev);

   // Statuslines above may have been lost.  Restore them.
   if (po->statusHeight == 0 && fr->parent->layout == FR_COL && fr->prev)
      frame_add_statusline(fr->prev);

   // Restore the lost room that was redistributed to the altframe.  Also
   // adjusts portal sizes to fit restored statuslines/separators, if needed.
   if (dir == 'v') {
      frame_new_height(
         unflat_altfr, unflat_altfr->height - fr->height, unflat_altfr == fr->next, FALSE, FALSE
      );
   } ei (dir == 'h') {
      frameNewWidth(
         unflat_altfr, unflat_altfr->width - fr->width, unflat_altfr == fr->next, FALSE
      );
   }

   //Recompute portal positions within the parent frame to restore them.
   //Positions were unchanged if the altframe was adjacent and left/above.
   if (unflat_altfr != fr->prev) {
      Portal* topleft = frameToPort(fr->parent);
      int row = topleft->portalRow;
      int col = topleft->portalCol;

      frame_comp_pos(fr->parent, &row, &col);
   }
}

//Return a pointer to the frame that will receive the empty screen space that
//is left over after "port" is closed.
//
//If 'splitbelow' or 'splitright' is set, the space goes above or to the left
//by default.  Otherwise, the free space goes below or to the right.  The
//result is that opening a portal and then immediately closing it will
//preserve the initial portal layout.  The 'wfh' and 'wfw' settings are
//respected when possible.
private Frame*
getAltFrame(Portal* port, Tab* t) {     // tab "port" is in, NULL for current
   if (!t ? ONLY_ONE_PORTAL : t->firstPor == t->lastPor)
      return altTab()->curPor->frame;

   Frame* fr = port->frame;

   if (!fr->prev)
      return fr->next;
   if (!fr->next)
      return fr->prev;

   // By default the next portal will get the space that was abandoned by this portal
   Frame* target_fr = fr->next;
   Frame* other_fr  = fr->prev;

   // If this is part of a column of portals and 'splitbelow' is true then the
   // previous portal will get the space.
   if (fr->parent && fr->parent->layout == FR_COL && p_sb) {
      target_fr = fr->prev;
      other_fr  = fr->next;
   }

   // If this is part of a row of portals, and 'splitright' is true then the
   // previous portal will get the space.
   if (fr->parent && fr->parent->layout == FR_ROW && p_spr) {
      target_fr = fr->prev;
      other_fr  = fr->next;
   }

   // If 'wfh' or 'wfw' is set for the target and not for the alternate
   // portal, reverse the selection.
   if (fr->parent != NULL && fr->parent->layout == FR_ROW) {
      if (frame_fixed_width(target_fr) && !frame_fixed_width(other_fr))
          target_fr = other_fr;
   } else {
      if (frame_fixed_height(target_fr) && !frame_fixed_height(other_fr))
          target_fr = other_fr;
   }

   return target_fr;
}

// Return the tabpage that will be used if the current one is closed.
private Tab *
altTab(void) {
   // Use the last accessed tab, if possible.
   if (p_tcl == TCL_USELAST && isTabValid(lastUsedTabG))
      return lastUsedTabG;

   // Use the next tab, if possible.
   Boole forward = curtab->next != NULL && (p_tcl != TCL_LEFT || curtab == firstTabG);

   Tab   *t = NULL;
   if (forward)
      t = curtab->next;
   else {
      // Use the previous tab
      for (t = firstTabG; t->next != curtab; t = t->next)
          ;
   } 

   return t;
}

// Find the left-upper portal in frame "fr".
private Portal*
frameToPort(Frame *fr) {
   while (!fr->port)
      fr = fr->child;
   return fr->port;
}

// Return TRUE if frame "fr" contains portal "po".
private int
frameHasPortal(Frame *fr, Portal *po) {
   if (fr->layout == FR_LEAF)
      return fr->port == po;

   Frame   *p;
   FOR_ALL_FRAMES(p, fr->child) {
      if (frameHasPortal(p, po))
         return TRUE;
   } 
   return FALSE;
}

// @commheight value explicitly set by the user: portal commands are allowed to
// resize the topframe to values higher than this minimum, but not lower.
private int min_set_ch = 1;

// Set a new height for a frame.  Recursively sets the height for contained
// frames and portals.  Caller must take care of positions.
private void
frame_new_height(
   Frame   *topfrp,
   int      height,
   int      topfirst,   // resize topmost contained frame first
   int      wfh,  // obey @portfixheight when there is a choice; may cause the height not to be set
   int      set_ch      // set @commheight to resize topframe
){
   Frame   *fr;
   int      extra_lines;
   int      h;

   if (topfrp->parent == NULL && set_ch) {
      // topframe: update the command line height, with side effects.
      int new_ch = MAX(min_set_ch, commlineHeightG + topfrp->height - height);
      int save_ch = min_set_ch;
      if (new_ch != commlineHeightG)
         optSetByName(S"commheight", (OptionValue){.tag = OPTION_NUM, .num = new_ch}, SET_LOCAL);
      min_set_ch = save_ch;
      height = MIN(height, ROWS_AVAIL);
   }
   if (topfrp->port) {
      // Simple case: just one portal.
      portalNewHeight(topfrp->port, height - topfrp->port->statusHeight);
   } ei (topfrp->layout == FR_ROW) {
      do {
         // All frames in this row get the same new height.
         FOR_ALL_FRAMES(fr, topfrp->child) {
            frame_new_height(fr, height, topfirst, wfh, set_ch);
            if ((int)fr->height > height) {
               // Could not fit the portals, make the whole row higher.
               height = fr->height;
               break;
            }
         }
      } while (fr != NULL);
   } else {   // layout == FR_COL
      // Complicated case: Resize a column of frames.  Resize the bottom
      // frame first, frames above that when needed.

      fr = topfrp->child;
      if (wfh)
         // Advance past frames with one portal with 'wfh' set.
         while (frame_fixed_height(fr)) {
            fr = fr->next;
            if (!fr)
               return;       // no frame without 'wfh', give up
         }
      if (!topfirst) {
         // Find the bottom frame of this column
         while (fr->next != NULL)
            fr = fr->next;
         if (wfh)
         // Advance back for frames with one portal with 'wfh' set.
         while (frame_fixed_height(fr))
             fr = fr->prev;
      }

      extra_lines = height - topfrp->height;
      if (extra_lines < 0) {
         // reduce height of contained frames, bottom or top frame first
         while (fr) {
            h = frame_minheight(fr, NULL);
            if ((int)fr->height + extra_lines < h) {
                extra_lines += fr->height - h;
                frame_new_height(fr, h, topfirst, wfh, set_ch);
            } else {
                frame_new_height(fr, fr->height + extra_lines, topfirst, wfh, set_ch);
                break;
            }
            if (topfirst) {
               do
                  fr = fr->next;
               while (wfh && fr != NULL && frame_fixed_height(fr));
            } else {
               do
                  fr = fr->prev;
               while (wfh && fr != NULL && frame_fixed_height(fr));
            }
            // Increase "height" if we could not reduce enough frames.
            if (!fr)
                height -= extra_lines;
         }
      } ei (extra_lines > 0) {
          // increase height of bottom or top frame
          frame_new_height(fr, fr->height + extra_lines, topfirst, wfh, set_ch);
      }
   }
   topfrp->height = height;
}

// Return TRUE if height of frame "fr" should not be changed because of the 'portfixheight' option.
private int
frame_fixed_height(Frame *fr) {
   // frame with one portal: fixed height if 'portfixheight' set.
   if (fr->port != NULL)
      return fr->port->o.portFixHeight;

   if (fr->layout == FR_ROW) {
      // The frame is fixed height if one of the frames in the row is fixed height.
      FOR_ALL_FRAMES(fr, fr->child) {
         if (frame_fixed_height(fr))
            return TRUE;
      }
      return FALSE;
   }

   // fr->layout == FR_COL: The frame is fixed height if all of the
   // frames in the row are fixed height.
   FOR_ALL_FRAMES(fr, fr->child) {
      if (!frame_fixed_height(fr))
          return FALSE;
   } 
   return TRUE;
}

// Return TRUE if width of frame "fr" should not be changed because of the 'portfixwidth' option
private int
frame_fixed_width(Frame* fr) {
   // frame with one portal: fixed width if 'portfixwidth' set.
   if (fr->port)
      return fr->port->o.portFixWidth;

   if (fr->layout == FR_COL) {
      // The frame is fixed width if one of the frames in the row is fixed width.
      FOR_ALL_FRAMES(fr, fr->child) {
         if (frame_fixed_width(fr))
            return TRUE;
      } 
      return FALSE;
   }

   // fr->layout == FR_ROW: The frame is fixed width if all of the
   // frames in the row are fixed width.
   FOR_ALL_FRAMES(fr, fr->child) {
      if (!frame_fixed_width(fr))
          return FALSE;
   } 
   return TRUE;
}

//Add a status line to portals at the bottom of "fr". Note: Does not check if there is room!
private void
frame_add_statusline(Frame* fr) {
   Portal* po;

   if (fr->layout == FR_LEAF) {
      po = fr->port;
      po->statusHeight = STATUS_HEIGHT;
   } ei (fr->layout == FR_ROW) {
      // Handle all the frames in the row.
      FOR_ALL_FRAMES(fr, fr->child)
         frame_add_statusline(fr);
   } else { // fr->layout == FR_COL
      // Only need to handle the last frame in the column.
      for (fr = fr->child; fr->next != NULL; fr = fr->next)
          ;
      frame_add_statusline(fr);
   }
}

//Set width of a frame.  Handles recursively going through contained frames.
//May remove separator line for portals at the right side (for closePortal()).
private void
frameNewWidth(
   Frame   *topfrp,
   int      width,
   int      leftfirst,   // resize leftmost contained frame first
   int      wfw)      // obey 'portfixwidth' when there is a choice;
            // may cause the width not to be set
{
   Frame   *fr;
   int      extra_cols;
   int      w;
   Portal   *po;

   if (topfrp->layout == FR_LEAF) {
      // Simple case: just one portal.
      po = topfrp->port;
      // Find out if there are any portals right of this one.
      for (fr = topfrp; fr->parent != NULL; fr = fr->parent) {
         if (fr->parent->layout == FR_ROW && fr->next != NULL)
            break;
      } 
      if (fr->parent == NULL)
         po->vsepWidth = 0;
      portalNewWidth(po, width - po->vsepWidth);
   } ei (topfrp->layout == FR_COL) {
      do {
          // All frames in this column get the same new width.
         FOR_ALL_FRAMES(fr, topfrp->child) {
            frameNewWidth(fr, width, leftfirst, wfw);
            if ((int)fr->width > width) {
               // Could not fit the portals, make whole column wider.
               width = fr->width;
               break;
            }
         }
      } while (fr != NULL);
   } else {   // layout == FR_ROW
      // Complicated case: Resize a row of frames.  Resize the rightmost
      // frame first, frames left of it when needed.

      fr = topfrp->child;
      if (wfw)
         // Advance past frames with one portal with 'wfw' set.
         while (frame_fixed_width(fr)) {
            fr = fr->next;
            if (!fr)
               return;       // no frame without 'wfw', give up
         }
      if (!leftfirst) {
         // Find the rightmost frame of this row
         while (fr->next)
            fr = fr->next;
         if (wfw) {
            // Advance back for frames with one portal with 'wfw' set.
            while (frame_fixed_width(fr))
                fr = fr->prev;
         } 
      }

      extra_cols = width - topfrp->width;
      if (extra_cols < 0) {
          // reduce frame width, rightmost frame first
          while (fr != NULL) {
         w = frame_minwidth(fr, NULL);
         if ((int)fr->width + extra_cols < w) {
             extra_cols += fr->width - w;
             frameNewWidth(fr, w, leftfirst, wfw);
         } else {
             frameNewWidth(fr, fr->width + extra_cols,
                              leftfirst, wfw);
             break;
         }
         if (leftfirst) {
            do
               fr = fr->next;
            while (wfw && fr != NULL && frame_fixed_width(fr));
         } else {
            do
               fr = fr->prev;
            while (wfw && fr != NULL && frame_fixed_width(fr));
         }
         // Increase "width" if we could not reduce enough frames.
         if (!fr)
            width -= extra_cols;
         }
      } ei (extra_cols > 0) {
         // increase width of rightmost frame
         frameNewWidth(fr, fr->width + extra_cols, leftfirst, wfw);
      }
   }
   topfrp->width = width;
}

//Add the vertical separator to portals at the right side of "fr".
//Note: Does not check if there is room!
private void
frame_add_vsep(Frame* fr) {
   Portal* po;

   if (fr->layout == FR_LEAF) {
      po = fr->port;
      if (po->vsepWidth == 0) {
          if (po->width > 0)   // don't make it negative
         --po->width;
          po->vsepWidth = 1;
      }
   } ei (fr->layout == FR_COL) {
      // Handle all the frames in the column.
      FOR_ALL_FRAMES(fr, fr->child)
         frame_add_vsep(fr);
   } else {// fr->layout == FR_ROW
      // Only need to handle the last frame in the row.
      fr = fr->child;
      while (fr->next)
         fr = fr->next;
      frame_add_vsep(fr);
   }
}

//Set frame width from the portal it contains.
private void
frame_fix_width(Portal *po) {
   po->frame->width = po->width + po->vsepWidth;
}

//Set frame height from the portal it contains.
private void
frame_fix_height(Portal *po) {
   po->frame->height = VISIBLE_HEIGHT(po) + po->statusHeight;
}

//Compute the minimal height for frame "topfrp".
//When "next_curPor" isn't NULL, use p_wh for this portal.
//When "next_curPor" is NOPORT, don't use at least one line for the current portal.
private Unt
frame_minheight(Frame *topfrp, Portal *next_curPor) {
   Frame   *fr;
   Unt m;
   Unt n;

   if (topfrp->port) {
      if (topfrp->port == next_curPor)
         m = p_wh + topfrp->port->statusHeight;
      else {
         // portal: minimal height of the portal plus status line
         m = MIN_PORTAL_HEIGHT + topfrp->port->statusHeight;
      }
   } ei (topfrp->layout == FR_ROW) {
      // get the minimal height from each frame in this row
      m = 0;
      FOR_ALL_FRAMES(fr, topfrp->child) {
         n = frame_minheight(fr, next_curPor);
         if (n > m)
            m = n;
      }
   } else {
      // Add up the minimal heights for all frames in this column.
      m = 0;
      FOR_ALL_FRAMES(fr, topfrp->child)
         m += frame_minheight(fr, next_curPor);
   }

   return m;
}

//Compute the minimal width for frame "topfrp".
//When "next_curPor" isn't NULL, use p_wiw for this portal.
//When "next_curPor" is NOPORT, don't use at least one column for the current portal.
private int
frame_minwidth(
   Frame* topfrp,
   Portal* next_curPor   // use p_wh and p_wiw for next_curPor
){
   Frame   *fr;
   int m, n;

   if (topfrp->port) {
      if (topfrp->port == next_curPor)
         m = p_wiw + topfrp->port->vsepWidth;
      else {
         // portal: minimal width of the portal plus separator column
         m = MIN_PORTAL_WIDTH + topfrp->port->vsepWidth;
         // Current portal is minimal one column wide
         if (MIN_PORTAL_WIDTH == 0 && topfrp->port == curPor && next_curPor == NULL)
            ++m;
      }
   } ei (topfrp->layout == FR_COL) {
      // get the minimal width from each frame in this column
      m = 0;
      FOR_ALL_FRAMES(fr, topfrp->child) {
         n = frame_minwidth(fr, next_curPor);
         if (n > m)
            m = n;
      }
   } else {
      // Add up the minimal widths for all frames in this row.
      m = 0;
      FOR_ALL_FRAMES(fr, topfrp->child)
          m += frame_minwidth(fr, next_curPor);
   }

   return m;
}


//Try to close all portals except current one.
//Buffers in the other portals become hidden if 'hidden' is set, or '!' is
//used and the buffer was modified.
//
//Used by ":bdel" and ":only".
void
close_others(Boole message) {
   if (onePortal()) {
      if (message && !autocmd_busy)
          msg(_(m_onlyone));
      return;
   }

   // Be very careful here: autocommands may change the portal layout.
   Portal* nextwp;
   for (Portal* po = firstPor; portalIsValid(po); po = nextwp) {
      nextwp = po->next;
      if (po == curPor)      // don't close current portal
          continue;

      // autoccommands messed this one up
      if (!bookIsValid(po->book) && portalIsValid(po)) {
          po->book = NULL;
          closePortal(po, 0);
          continue;
      }
      if (!portalIsValid(po)) {    // autocommands messed po up
          nextwp = firstPor;
          continue;
      }
      closePortal(po, FALSE);
   }

   if (message && !ONLY_ONE_PORTAL)
      emsg(_(e_other_portal_contains_changes));
}

// Store the relevant portal pointers for tab "t".  To be used before loadTab().
void
unloadTab(Tab *t) {
   t->topframe = topframeG;
   t->firstPor = firstPor;
   t->lastPor = lastPor;
   t->curPor = curPor;
}

// When switching tabpage, handle other side-effects in command_height(), but
// avoid setting frame sizes which are still correct.
private int command_frame_height = TRUE;

// Set the relevant pointers to use tab "t".  May want to call unloadTab() first.
void
loadTab(Tab* t) {
   curtab = t;
   topframeG = curtab->topframe;
   firstPor = curtab->firstPor;
   lastPor = curtab->lastPor;
   curPor = curtab->curPor;
}

//Allocate the first portal and put an empty buffer in it. Called from main().
//Return FAIL when something goes wrong (out of memory).
int
portAllocFirst(void) {
   allocateFirstPortal(NULL);

   firstTabG = alloc_tab();
   curtab = firstTabG;
   unloadTab(firstTabG);

   return OK;
}

//Allocate and init a portal that is not a regular portal.
//This can only be done after the first portal is fully initialized, thus it
//can't be in portAllocFirst().
Portal *
portAllocPopup(void) {
   Portal* po = allocPortal(NULL, TRUE);
   if (!po)
      return NULL;
      
   // We need to initialize options with something, using the current portal makes most sense.
   initg_some(po, curPor);

   RESET_BINDING(po);
   neframe(po);
   return po;
}

// Initialize portal "po" to display buffer "buf".
void
initPopupPortal(Portal* po, Book* book) {
   po->book = book;
   ++book->countPortals;
   initEmptyPortal(po); // set cursor and topline to safe values

   // Make sure localDir is NULL to avoid a chdir() in enterPortalWorker().
   EE_CLEAR(po->localDir);
}

//Allocate the first portal or the first portal in a new tab.
//When "oldPortal" is NULL create an empty buffer for it.
//When "oldPortal" is not NULL copy info from it to the new portal.
//Return FAIL when something goes wrong (out of memory).
private int
allocateFirstPortal(Portal* oldPortal) {
   curPor = allocPortal(NULL, FALSE);
   if (curPor == NULL)
      return FAIL;
   if (oldPortal == NULL) {
      // Very first portal, need to create an empty buffer for it and
      // initialize from scratch.
      curBook = bookNew(NULL, NULL, 1L, BLN_LISTED);
      if (curPor == NULL || curBook == NULL)
          return FAIL;
      curPor->book = curBook;
      curPor->ownSyntax = &(curBook->syntax);
      curBook->countPortals = 1;   // there is one portal
      curPor->argList = &argListG;
      curPor_init();      // init current portal
   } else {
      // First portal in new tab, initialize it from "oldPortal".
      init(curPor, oldPortal, 0);

      // We don't want cursor- and scroll-binding in the first portal.
      RESET_BINDING(curPor);
   }

   neframe(curPor);
   if (curPor->frame == NULL)
      return FAIL;
   topframeG = curPor->frame;
   topframeG->width = COLUMNS_WITHOUT_TPL();
   topframeG->height = visibleRowsG - commlineHeightG;

   return OK;
}

// Create a frame for portal "po".
private void
neframe(Portal *po) {
    Frame *fr = ALLOC_CLEAR_ONE(Frame);

    po->frame = fr;
   if (fr == NULL)
   return;
    fr->layout = FR_LEAF;
    fr->port = po;
}

// Initialize the portal and frame size to the maximum.
void
portalInitSize(void) {
   firstPor->height = ROWS_AVAIL;
   firstPor->prevHeight = ROWS_AVAIL;
   topframeG->height = ROWS_AVAIL;
   firstPor->width = topframeG->width;
}

//Allocate a new Tab and init the values. Return NULL when out of memory.
private Tab *
alloc_tab(void) {
   Tab* t = ALLOC_CLEAR_ONE(Tab);

   // init t: variables
   t->vars = allocBag_id(aid_newtabpage_tvars);
   if (t->vars == NULL) {
      eeglFree(t);
      return NULL;
   }
   init_var_dict(t->vars, &t->tabVar, VAR_SCOPE);

   t->diff_invalid = TRUE;
   t->ch_used = commlineHeightG;

   return t;
}

void
freeTab(Tab *t) {
   int idx;

   diff_clear(t);
   while (t->firstPopupPort)
      popupCloseTab(t, t->firstPopupPort->id, TRUE);
   for (idx = 0; idx < SNAP_COUNT; ++idx)
      clearSnapshot(t, idx);
   vars_clear(&t->vars->hashTable);   // free all t: variables
   hash_init(&t->vars->hashTable);
   unref_var_dict(t->vars);

   if (t == lastUsedTabG)
      lastUsedTabG = NULL;

   eeglFree(t->localdir);
   eeglFree(t->prevdir);


   eeglFree(t);
}

// Create a new Tab with one portal.
// It will edit the current buffer, like after ":split". When "after" is 0 put it just after the 
// current Tab. Otherwise put it just before tab "after".
// Does not trigger WinNewPre, since the portal structures are not completely setup yet and could 
// cause dereferencing NULL pointers Return FAIL or OK.
int
portNewTab(int after) {
   Tab   *t = curtab;
   Tab   *prev_tp = curtab;
   int      n;
   int prev_columns = COLUMNS_WITHOUT_TPL();

   if (commPortTypeG != 0) {
      emsg(_(e_invalid_in_commline_portal));
      return FAIL;
   }
   if (portalLayout_locked(C_tabnew))
      return FAIL;

   Tab* newTab = alloc_tab();
   if (newTab == NULL)
      return FAIL;

   // Remember the current portals in this Tab
   if (leaveTab(curBook, TRUE) == FAIL) {
      eeglFree(newTab);
      return FAIL;
   }
   curtab = newTab;

   newTab->localdir = (t->localdir == NULL) ? NULL : copyStr(t->localdir);

   // Create a new empty portal.
   if (allocateFirstPortal(t->curPor) == OK) {
      // Make the new Tab the new topframe.
      if (after == 1) {
         // New tab becomes the first one.
         newTab->next = firstTabG;
         firstTabG = newTab;
      } else {
         if (after > 0) {
            // Put new tab before tab "after".
            n = 2;
            for (t = firstTabG; t->next != NULL && n < after; t = t->next)
               ++n;
         }
         newTab->next = t->next;
         t->next = newTab;
      }
      newTab->firstPor = newTab->lastPor = newTab->curPor = curPor;

      portalInitSize();
      firstPor->portalRow = 0;
      firstPor->prevPortRow = firstPor->portalRow;
      portComputeScroll(curPor);

      newTab->topframe = topframeG;
      last_status(FALSE);

      lastUsedTabG = prev_tp;

      enteringPortal(curPor);
      if (prev_columns != COLUMNS_WITHOUT_TPL())
          shell_new_columns();
      redraw_all_later(UPD_NOT_VALID);
      apply_autocmds(EVENT_PORTNEW, NULL, NULL, false, curBook);
      apply_autocmds(EVENT_PORTENTER, NULL, NULL, false, curBook);
      apply_autocmds(EVENT_TABNEW, NULL, NULL, false, curBook);
      apply_autocmds(EVENT_TABENTER, NULL, NULL, false, curBook);
      return OK;
   }

   // Failed, get back the previous Tab
   enterTab(curtab, curBook, TRUE, TRUE);
   return FAIL;
}

// Open a new tab if ":tab cmd" was used. Will edit the same buffer, like with ":split".
// Return OK if a new tab was created, FAIL otherwise.
private int
mayOpenTab(void) {
   int      n = (commModifierG.cmod_tab == 0) ? postponed_split_tab : commModifierG.cmod_tab;

   if (n == 0)
      return FAIL;

   commModifierG.cmod_tab = 0;       // reset it to avoid doing it twice
   postponed_split_tab = 0;
   return portNewTab(n);
}

// Create up to "maxcount" tabs with empty portals. Returns the number of resulting tabs
int
make_tabpages(int maxcount) {
   int      count = maxcount;
   int      todo;

   // Limit to 16 tabs.
   if (count > 16)
      count = 16;

   // Abstain from executing autocommands while creating the tabs. Must do that when opening 
   // portals into the buffers
   block_autocmds();

   for (todo = count - 1; todo > 0; --todo) {
      if (portNewTab(0) == FAIL)
          break;
   } 

   unblock_autocmds();

   // return actual number of tabs
   return (count - todo);
}

// Return TRUE when "tpc" points to a valid tab
int
isTabValid(Tab *tpc) {
   Tab   *t;
   FOR_ALL_TABS(t) {
      if (t == tpc)
         return TRUE;
   } 
   return FALSE;
}

// Return TRUE when "tpc" points to a valid tab and at least one portal is valid.
int
areTabAndPortalValid(Tab *tpc) {    Tab   *t;
   Portal   *po;

   FOR_ALL_TABS(t) {
      if (t == tpc) {
         FOR_ALL_PORTALS_IN_TAB(t, po) {
            if (doesPortalExistInAnyTab(po))
               return TRUE;
         }
         return FALSE;
      }
   }
   // shouldn't happen
   return FALSE;
}

// Close "tab", assuming it has no portals in it. There must be another tab or this will crash.
void
closeTab(Tab *tab) {
   if (p_tcl == TCL_USELAST && lastUsedTabG) {
      gotoTab(lastUsedTabG, FALSE, FALSE);
   } else {
      Tab* prev;
      if (tab == firstTabG) {
         firstTabG = tab->next;
         prev = firstTabG;
      } else {
         for (prev = firstTabG; prev && prev->next != tab; prev = prev->next)
             ;
         assert(prev != NULL);
         prev->next = tab->next;
      }

      gotoTab(prev, FALSE, FALSE);
   }
   freeTab(tab);
}

// Find tab "n" (first one is 1).  Returns NULL when not found.
Tab *
getTab(int n) {
   if (n == 0)
      return curtab;

   int      i = 1;
   Tab* t;
   for (t = firstTabG; t != NULL && i != n; t = t->next)
      ++i;
   return t;
}

// Get index of tab "t". First one has index 1. When not found returns number of tabs plus one.
Unt
indexOfTab(Tab* needle) {
   Unt      i = 1;
   for (Tab* t = firstTabG; t && t != needle; t = t->next)
      ++i;
   return i;
}

//Prepare for leaving the current tab.
//When autocommands change "curtab" we don't leave the tab and return FAIL.
//Careful: When OK is returned need to get a new tab very very soon!
private int
leaveTab(
   Book   *newCurBook,      // what is going to be the new curBook, NULL if unknown
   int      trigger_leave_autocmds)
{
   Tab   *t = curtab;

   leavingPortal(curPor);
   reset_VIsual_and_resel();   // stop Visual mode
   if (trigger_leave_autocmds) {
   if (newCurBook != curBook) {
      apply_autocmds(EVENT_BUFLEAVE, NULL, NULL, false, curBook);
      if (curtab != t)
         return FAIL;
   }
   apply_autocmds(EVENT_PORTLEAVE, NULL, NULL, false, curBook);
   if (curtab != t)
      return FAIL;
   apply_autocmds(EVENT_TABLEAVE, NULL, NULL, false, curBook);
   if (curtab != t)
      return FAIL;
   }

   mouseResetDragPortal();
   t->curPor = curPor;
   t->prevPor = prevPor;
   t->firstPor = firstPor;
   t->lastPor = lastPor;
   t->old_Rows = visibleRowsG;
   if (t->old_Columns != -1) {
      t->old_Columns = topframeG->width;
      t->old_coloff = firstPor->portalCol;
   }
   firstPor = NULL;
   lastPor = NULL;
   return OK;
}

//Start using tab "t".
//Only to be used after leaveTab() or freeing the current tab.
//Only trigger *Enter autocommands when trigger_enter_autocmds is TRUE.
//Only trigger *Leave autocommands when trigger_leave_autocmds is TRUE.
private void
enterTab(
   Tab   *t,
   Book   *oldCurBuf,
   int      trigger_enter_autocmds,
   int      trigger_leave_autocmds)
{
   int      old_off = t->firstPor->portalRow;
   Portal   *next_prevPor = t->prevPor;
   Tab   *last_tab = curtab;

   loadTab(t);

   if (commlineHeightG != curtab->ch_used) {
      // Use the stored value of commlineHeightG, so that it can be different for each tab
      int new_ch = curtab->ch_used;
      curtab->ch_used = commlineHeightG;
      command_frame_height = FALSE;
      optSetByName(S"commheight", (OptionValue){.tag = OPTION_NUM, .num = new_ch}, SET_LOCAL);
      command_frame_height = TRUE;
   }

   // We would like doing the TabEnter event first, but we don't have a
   // valid current portal yet, which may break some commands.
   // This triggers autocommands, thus may make "t" invalid.
   (void)enterPortalWorker(t->curPor, WEE_CURWIN_INVALID
        | (trigger_enter_autocmds ? WEE_TRIGGER_ENTER_AUTOCMDS : 0)
        | (trigger_leave_autocmds ? WEE_TRIGGER_LEAVE_AUTOCMDS : 0));
   prevPor = next_prevPor;

   last_status(FALSE);      // status line may appear or disappear
   computePosPortal();      // recompute portalRow for all portals
   diff_need_scrollbind = TRUE;

   // If there was a click in a portal, it won't be usable for a following drag.
   mouseResetDragPortal();

   // The tabpage line may have appeared or disappeared, may need to resize
   // the frames for that. When the Eegl portal was resized need to update frame sizes too.
   if (curtab->old_Rows != visibleRowsG || (old_off != firstPor->portalRow))
      shell_new_rows();
   if (curtab->old_Columns != COLUMNS_WITHOUT_TPL() || curtab->old_coloff != TPL_LCOL()) {
      if (starting == 0) {
         shell_new_columns();   // update portal widths
         curtab->old_Columns = topframeG->width;
         curtab->old_coloff = firstPor->portalCol;
      } else
         curtab->old_Columns = -1;  // update portal widths later
   }

   lastUsedTabG = last_tab;

   // Apply autocommands after updating the display, when 'rows' and
   // 'columns' have been set correctly.
   if (trigger_enter_autocmds) {
      apply_autocmds(EVENT_TABENTER, NULL, NULL, false, curBook);
      if (oldCurBuf != curBook)
          apply_autocmds(EVENT_BUFENTER, NULL, NULL, false, curBook);
   }

   redraw_all_later(UPD_NOT_VALID);
}

// Go to tab "n".  For ":tab N" and "Ngt". When "n" is 9999 go to the last tab.
void
gotoTabById(int n) {
   Tab   *t = NULL;  // shut up compiler
   Tab   *ttp;
   int      i;

   if (text_locked()) {
      // Not allowed when editing the command line.
      text_locked_msg();
      return;
   }

   // If there is only one it can't work.
   if (firstTabG->next == NULL) {
      if (n > 1)
         beep_flush();
      return;
   }

   if (n == 0) {
      // No count, go to next tab, wrap around end.
      if (curtab->next == NULL)
          t = firstTabG;
      else
          t = curtab->next;
   } ei (n < 0) {
      // "gT": go to previous tab, wrap around end.  "N gT" repeats this N times.
      ttp = curtab;
      for (i = n; i < 0; ++i) {
         for (t = firstTabG; t->next != ttp && t->next != NULL; t = t->next)
            {}
         ttp = t;
      }
   } ei (n == 9999) {
      // Go to last tab.
      for (t = firstTabG; t->next != NULL; t = t->next)
         {} 
   } else {
      // Go to tab "n".
      t = getTab(n);
      if (!t) {
          beep_flush();
          return;
      }
   }

   gotoTab(t, TRUE, TRUE);
}

//Go to tab "t".
//Only trigger *Enter autocommands when trigger_enter_autocmds is TRUE.
//Only trigger *Leave autocommands when trigger_leave_autocmds is TRUE.
void
gotoTab(
   Tab   *t,
   int      trigger_enter_autocmds,
   int      trigger_leave_autocmds
){
   if (trigger_enter_autocmds || trigger_leave_autocmds)
      CHECK_COMMPORT;

   // Don't repeat a message in another tab
   set_keep_msg(NULL, 0);

   skipPortFixScrollG = TRUE;
   if (t != curtab && leaveTab(t->curPor->book, trigger_leave_autocmds) == OK) {
      if (isTabValid(t))
         enterTab(t, curBook, trigger_enter_autocmds, trigger_leave_autocmds);
      else
         enterTab(curtab, curBook, trigger_enter_autocmds, trigger_leave_autocmds);
   }
   skipPortFixScrollG = FALSE;
}

// Go to the last accessed tab, if there is one. Return OK or FAIL
int
goto_tabpage_lastused(void) {
   if (!isTabValid(lastUsedTabG))
      return FAIL;

   gotoTab(lastUsedTabG, TRUE, TRUE);
   return OK;
}

// Enter portal "po" in tab "t".
void
goto_tab_port(Tab *t, Portal *po) {
   gotoTab(t, TRUE, TRUE);
   if (curtab == t && portalIsValid(po)) {
       enterPortal(po, TRUE);
   }
}

// Move the current tab to after tab "nr".
void
moveTab(Unt nr) {
   Unt      n = 1;
   Tab   *t, *dest;

   if (firstTabG->next == NULL)
      return;

   if (movingTabsForbiddenG)
      return;

   for (t = firstTabG; t->next && n < nr; t = t->next, n++)
      {}

   if (t == curtab || (nr > 0 && t->next && t->next == curtab))
      return;

   dest = t;

   // Remove the current tab from the list of tabs.
   if (curtab == firstTabG)
      firstTabG = curtab->next;
   else {
      FOR_ALL_TABS(t)
         if (t->next == curtab)
            break;
      if (t == NULL)   // "cannot happen"
         return;
      t->next = curtab->next;
   }

   // Re-insert it at the specified position.
   if (nr <= 0) {
      curtab->next = firstTabG;
      firstTabG = curtab;
   } else {
      curtab->next = dest->next;
      dest->next = curtab;
   }

   // Need to redraw the tabpanel.  Tab contents doesn't change.
   needRedrawTabpanelG = TRUE;
}


 // Go to another portal.
 // When jumping to another buffer, stop Visual mode.  Do this before changing portals so we can 
 // yank the selection into the '*' register. (note: this may trigger ModeChanged autocommand!)
 // When jumping to another portal on the same buffer, adjust its cursor position to keep the same 
 // Visual area.
void
gotoPortal(Portal *po) {
   if (portErrorIfPopup(true))
      return;
   if (popup_is_popup(po)) {
      emsg(_(e_not_allowed_to_enter_popup_portal));
      return;
   }
   if (text_or_buf_locked()) {
      beep_flush();
      return;
   }

   if (po->book != curBook)
      // careful: triggers ModeChanged autocommand
      reset_VIsual_and_resel();
   ei (VIsual_active)
      po->cursor = curPor->cursor;

    // autocommand may have made po invalid
   if (!portalIsValid(po))
      return;
   enterPortal(po, TRUE);
}


//Get the above or below neighbor portal of the specified portal.
//  up - TRUE for the above neighbor
//  count - nth neighbor portal
//Return the specified portal if the neighbor is not found.
private Portal *
vertNeighbor(Tab *t, Portal *po, int up, long count) {
   if (popup_is_popup(po))
      // popups don't have neighbors.
      return NULL;
   Frame* foundfr = po->frame;
   while (count--) {
      //First go upwards in the tree of frames until we find an upwards or downwards neighbor.
      Frame* fr = foundfr;
      Frame   *nfr;
      for (;;) {
         if (fr == t->topframe)
            goto end;
         if (up)
            nfr = fr->prev;
         else
            nfr = fr->next;
         if (fr->parent->layout == FR_COL && nfr != NULL)
            break;
         fr = fr->parent;
      }

      //Now go downwards to find the bottom or top frame in it.
      for (;;) {
         if (nfr->layout == FR_LEAF) {
            foundfr = nfr;
            break;
         }
         fr = nfr->child;
         if (nfr->layout == FR_ROW) {
            // Find the frame at the cursor row.
            while (fr->next
                  && frameToPort(fr)->portalCol + fr->width <= (Unt)po->portalCol + po->cursorCol)
               fr = fr->next;
         }
         if (nfr->layout == FR_COL && up)
         while (fr->next)
             fr = fr->next;
         nfr = fr;
      }
    }
end:
   return foundfr != NULL ? foundfr->port : NULL;
}

// Move to portal above or below "count" times.
private void
gotoPortal_ver(
    int      up,      // TRUE to go to port above
    long   count)
{
   if (portErrorIfTermPopup())
      return;
   Portal* port = vertNeighbor(curtab, curPor, up, count);
   if (port != NULL)
   gotoPortal(port);
}

//Get the left or right neighbor portal of the specified portal.
//  left - TRUE for the left neighbor
//  count - nth neighbor portal
//Return the specified portal if the neighbor is not found.
Portal *
portHorizNeighbor(Tab *t, Portal* po, int left, long count) {
   Frame   *fr;
   Frame   *nfr;
   Frame   *foundfr;

   if (popup_is_popup(po))
      // popups don't have neighbors.
      return NULL;
   foundfr = po->frame;
   while (count--) {
      // First go upwards in the tree of frames until we find a left or right neighbor.
      fr = foundfr;
      for (;;) {
         if (fr == t->topframe)
            goto end;
         if (left)
            nfr = fr->prev;
         else
            nfr = fr->next;
         if (fr->parent->layout == FR_ROW && nfr != NULL)
            break;
         fr = fr->parent;
      }

      // Now go downwards to find the leftmost or rightmost frame in it.
      for (;;) {
         if (nfr->layout == FR_LEAF) {
            foundfr = nfr;
            break;
         }
         fr = nfr->child;
         if (nfr->layout == FR_COL) {
            // Find the frame at the cursor row.
            while (fr->next
               && frameToPort(fr)->portalRow + fr->height <= (Unt)po->portalRow + po->cursorRow
            )
               fr = fr->next;
         }
         if (nfr->layout == FR_ROW && left) {
            while (fr->next)
               fr = fr->next;
         } 
         nfr = fr;
      }
   }
end:
   return foundfr != NULL ? foundfr->port : NULL;
}

// Move to left or right portal.
private void
gotoPortal_hor(
   int      left,      // TRUE to go to left port
   long   count)
{
   if (portErrorIfTermPopup())
      return;
   Portal* port = portHorizNeighbor(curtab, curPor, left, count);
   if (port)
      gotoPortal(port);
}

//Make portal "po" the current portal
void
enterPortal(Portal *po, int undo_sync) {
   (void)enterPortalWorker(
       po, 
      (undo_sync ? WEE_UNDO_SYNC : 0) | WEE_TRIGGER_ENTER_AUTOCMDS | WEE_TRIGGER_LEAVE_AUTOCMDS
   );
}

// Used after making another portal the current one: change directory if needed.
void
portFixCurrentDir(void) {
   if (curPor->localDir || curtab->localdir) {
      Byte   *dirname;

      // Portal or tab has a local directory: Save current directory as
      // global directory (unless that was done already) and change to the local directory
      if (globaldir == NULL) {
         Byte   cwd[MAXPATHL];

         if (mch_dirname(cwd, MAXPATHL) == OK)
            globaldir = copyStr(cwd);
      }
      if (curPor->localDir != NULL)
         dirname = curPor->localDir;
      else
         dirname = curtab->localdir;

      if (mch_chdir((char *)dirname) == 0) {
         last_chdir_reason = NULL;
         shorten_fnames(TRUE);
      }
   } ei (globaldir) {
      //Portal doesn't have a local directory and we are not in the global
      //directory: Change to the global directory.
      (void)mch_chdir((char *)globaldir);
      EE_CLEAR(globaldir);
      last_chdir_reason = NULL;
      shorten_fnames(TRUE);
   }
}

//Make portal "po" the current portal.
//Can be called with "flags" containing WEE_CURWIN_INVALID, which means that
//curPor has just been closed and isn't valid.
//Return TRUE when dont_parse_messages was decremented.
private int
enterPortalWorker(Portal *po, int flags) {
   int      isOtherBook = FALSE;
   int      curPor_invalid = (flags & WEE_CURWIN_INVALID);
   int      did_decrement = FALSE;

   if (po == curPor && curPor_invalid == 0)   // nothing to do
      return FALSE;

   if (curPor_invalid == 0)
      leavingPortal(curPor);

   if (curPor_invalid == 0 && (flags & WEE_TRIGGER_LEAVE_AUTOCMDS)) {
      //Be careful: If autocommands delete the portal, return now.
      if (po->book != curBook) {
         apply_autocmds(EVENT_BUFLEAVE, NULL, NULL, false, curBook);
         isOtherBook = TRUE;
         if (!portalIsValid(po))
            return FALSE;
      }
      apply_autocmds(EVENT_PORTLEAVE, NULL, NULL, false, curBook);
      if (!portalIsValid(po))
         return FALSE;
      // autocomms may abort script processing
      if (aborting())
         return FALSE;
   }

   // sync undo before leaving the current buffer
   if ((flags & WEE_UNDO_SYNC) && curBook != po->book)
      u_sync(FALSE);

   // Might need to scroll the old portal before switching, e.g., when the cursor was moved
   if (curPor_invalid == 0)
      update_topline();

   if (po->book != curBook)
      optsCopyToBook(po->book, BCO_ENTER);
   if (curPor_invalid == 0) {
      prevPor = curPor;   // remember for CTRL-W p
      curPor->statusLineNeedsRedraw = TRUE;
   }
   curPor = po;
   curBook = po->book;
   check_cursor();
   if (!virtual_active())
      curPor->cursor.coladd = 0;
   changed_line_abv_curs();
   // Now it is OK to parse messages again, which may be needed in autocommands.
   if (flags & WEE_ALLOW_PARSE_MESSAGES) {
      --dont_parse_messages;
      did_decrement = TRUE;
   }

   portFixCurrentDir();

   enteringPortal(curPor);
   // Careful: autocommands may close the portal and make "po" invalid
   if (flags & WEE_TRIGGER_NEW_AUTOCMDS)
      apply_autocmds(EVENT_PORTNEW, NULL, NULL, false, curBook);
   if (flags & WEE_TRIGGER_ENTER_AUTOCMDS) {
      apply_autocmds(EVENT_PORTENTER, NULL, NULL, false, curBook);
      if (isOtherBook)
         apply_autocmds(EVENT_BUFENTER, NULL, NULL, false, curBook);
   }

   curPor->statusLineNeedsRedraw = true;
   if (bt_terminal(curPor->book))
      // terminal is likely in another mode
      redrawModeG = TRUE;
   needRedrawTabpanelG = TRUE;
   if (restart_edit)
      redraw_later(UPD_VALID);   // causes status line redraw

   // set portal height to desired minimal value
   if (curPor->height < p_wh && !curPor->o.portFixHeight  && !popup_is_popup(curPor))
      portSetHeight((int)p_wh, curPor);
   ei (curPor->height == 0)
      portSetHeight(1, curPor);

   // set portal width to desired minimal value
   if (curPor->width < p_wiw && !curPor->o.portFixWidth)
      portSetWidth((int)p_wiw, curPor);

   setmouse();         // in case jumped to/from help buffer

   // Change directories when the 'acd' option is set.
   DO_AUTOCHDIR;

   return did_decrement;
}

//Jump to the first open portal into buffer "buf", if one exists.
//Return a pointer to the portal found, otherwise NULL.
Portal *
portTryFindOpenBook(Book *book) {
   Portal   *po = NULL;

   if (curPor->book == book)
      po = curPor;
   else {
      FOR_ALL_PORTALS(po) {
         if (po->book == book)
            break;
      } 
   }
   if (po)
      enterPortal(po, FALSE);
   return po;
}

//Jump to the first open portal in any tab that contains book "book", if one exists. First search 
//in the portals present in the current tab. Return a pointer to the found portal, otherwise NULL.
Portal *
buf_jump_open_tab(Book *book) {
   Portal   *po = portTryFindOpenBook(book);
   if (po)
      return po;

   Tab   *t;
   FOR_ALL_TABS(t) {
      if (t != curtab) {
          FOR_ALL_PORTALS_IN_TAB(t, po)
         if (po->book == book)
             break;
         if (po) {
            goto_tab_port(t, po);
            if (curPor != po)
                po = NULL;   // something went wrong
            break;
          }
      }
   } 
   return po;
}

private int lastPortIdS = MIN_PORT_ID - 1;

// Allocate a portal structure and link it in the portal list when "hidden" is FALSE.
private Portal *
allocPortal(Portal *after, int hidden) {
   // allocate portal structure and linesizes arrays
   Portal* newPort = ALLOC_CLEAR_ONE(Portal);
   allocLinesPortal(newPort);

   newPort->id = ++lastPortIdS;

   // init w: variables
   newPort->internalVars = allocBag_id(aid_newwin_wvars);
   if (newPort->internalVars == NULL) {
      freePortalLsizes(newPort);
      eeglFree(newPort);
      return NULL;
   }
   init_var_dict(newPort->internalVars, &newPort->wVar, VAR_SCOPE);

   // Don't execute autocommands while the portal is not properly
   // initialized yet.  gui_create_scrollbar() may trigger a FocusGained event.
   block_autocmds();

   // link the portal in the portal list
   if (!hidden)
      append(after, newPort);
   newPort->portalCol = TPL_LCOL();
   newPort->width = COLUMNS_WITHOUT_TPL();

   // position the display and the cursor at the top of the file.
   newPort->topLine = 1;
   newPort->topFill = 0;
   newPort->bottomLine = 2;
   newPort->cursor.lnum = 1;
   newPort->scbindPos = 1;


   // We won't calculate fraction until resizing the portal
   newPort->fraction = 0;
   newPort->prevFraction = -1;

   normInitFoldForPortal(newPort);
   unblock_autocmds();
   newPort->nextMatchId = 1000;  // up to 1000 can be picked by the user
   return newPort;
}

// Remove portal 'po' from the portal list and free the structure.
private void
freePortal(Portal* po, Tab* t) { // tab "po" is in, NULL for current
   PortInfo   *wip;

   clearFolding(po);

   alist_unlink(po->argList);

   // Don't execute autocommands while the portal is halfway being deleted.
   // gui_mch_destroy_scrollbar() may trigger a FocusGained event.
   block_autocmds();

   optClearPortOptions(&po->o);

   vars_clear(&po->internalVars->hashTable);   // free all w: variables
   hash_init(&po->internalVars->hashTable);
   unref_var_dict(po->internalVars);

   {

      if (prevPor == po)
         prevPor = NULL;
      Tab   *search;
      FOR_ALL_TABS(search) {
         if (search->prevPor == po)
            search->prevPor = NULL;
      } 
   }
   freePortalLsizes(po);

   for (Unt i = 0; i < po->tagStackLen; ++i)
      tagstack_clear_entry(&po->tagStack[i]);
   eeglFree(po->localDir);
   eeglFree(po->prevdir);

   // Remove the portal from the portInfos lists, it may happen that the
   // freed memory is re-used for another portal.
   Book   *book;
   FOR_ALL_BOOKS(book) {
      FOR_ALL_BOOK_PORTINFOS(book, wip) {
         if (wip->portal == po) {
            PortInfo   *wip2;
            
            // If there already is an entry with "portal" set to NULL it must be removed, it 
            // would never be used. Skip "wip" itself, otherwise Coverity complains.
            FOR_ALL_BOOK_PORTINFOS(book, wip2) {
               if (wip2 != wip && wip2->portal == NULL) {
                  if (wip2->next)
                     wip2->next->prev = wip2->prev;
                  if (!wip2->prev)
                     book->portInfos = wip2->next;
                  else
                     wip2->prev->next = wip2->next;
                  free_wininfo(wip2);
                  break;
               }
            } 

            wip->portal = NULL;
         }
      }
   }

   clear_matches(po);
   free_jumplist(po);

   evFreeCallback(&po->pup.closeCb);
   evFreeCallback(&po->pup.filterCb);
   for (Unt i = 0; i < 4; ++i)
      EE_CLEAR(po->pup.borderHilite[i]);
   eeglFree(po->pup.scrollbarHilite);
   eeglFree(po->pup.thumbHilite);
   eeglFree(po->pup.title);
   list_unref(po->pup.mask);
   eeglFree(po->pup.maskCells);

   if (doesPortalExistInAnyTab(po))
      removePortal(po, t);
   if (autocmd_busy) {
      po->next = auPendingFreePortalsG;
      auPendingFreePortalsG = po;
   } else
      eeglFree(po);

   unblock_autocmds();
}

// Return TRUE if "po" is not in the list of portals: the autocmd portals or a popup
int
portUnlisted(Portal *po) {
   return is_autoCommPort(po) || PORTAL_IS_POPUP(po);
}

// Free a popup portal. This does not take the portal out of the portal list
// and assumes there is only one toplevel frame, no splits.
void
portFreePopup(Portal* port) {
   if (port->book) {
      if (bt_popup(port->book))
         closePortalBook(port, DOBOOK_WIPE_REUSE, FALSE);
      else
         closeBook(port, port->book, 0, FALSE, FALSE);
   }
   // the timer may have been cleared, making the pointer invalid
   if (timer_valid(port->pup.timer))
      stop_timer(port->pup.timer);
   eeglFree(port->frame);
   freePortal(port, NULL);
}

// Append portal "po" in the portal list after "after".
private void
append(Portal *after, Portal *po) {
   Portal   *before;

   if (after == NULL)       // after NULL is in front of the first
      before = firstPor;
   else
      before = after->next;

   po->next = before;
   po->prev = after;
   if (after == NULL)
      firstPor = po;
   else
      after->next = po;
   if (!before)
      lastPor = po;
   else
      before->prev = po;
}

// Remove a portal from the portal list.
void
removePortal( Portal* port, Tab* t) { // tab "port" is in, NULL for current
   if (port->prev)
      port->prev->next = port->next;
   ei (!t)
      firstPor = curtab->firstPor = port->next;
   else
      t->firstPor = port->next;

   if (port->next)
      port->next->prev = port->prev;
   ei (!t)
      lastPor = curtab->lastPor = port->prev;
   else
      t->lastPor = port->prev;
}

// Append frame "fr" in a frame list after frame "after".
private void
frame_append(Frame *after, Frame *frame) {
   frame->next = after->next;
   after->next = frame;
   if (frame->next)
      frame->next->prev = frame;
   frame->prev = after;
}

// Insert frame "fr" in a frame list before frame "before".
private void
frame_insert(Frame *before, Frame *fr) {
   fr->next = before;
   fr->prev = before->prev;
   before->prev = fr;
   if (fr->prev != NULL)
      fr->prev->next = fr;
   else
      fr->parent->child = fr;
}

// Remove a frame from a frame list.
private void
frame_remove(Frame *fr) {
   if (fr->prev != NULL)
      fr->prev->next = fr->next;
   else
      fr->parent->child = fr->next;
   if (fr->next != NULL)
      fr->next->prev = fr->prev;
}

// Allocate lines[] for portal "po". Return FAIL for failure, OK for success.
void
allocLinesPortal(Portal *po) {
   po->validLines = 0;
   po->lines = ALLOC_CLEAR_MULT(PortLine, visibleRowsG);
}

// free lsize arrays for a portal
void
freePortalLsizes(Portal *po) {
   // TODO: why would po be NULL here?
   if (po)
      EE_CLEAR(po->lines);
}

// Called from win_new_shellsize() after visibleRowsG changed.
// This only does the current tab, others must be done when made active.
// Note: When called together with shell_new_columns(), call shell_new_columns()
// first to avoid this function updating firstPor->portalCol first.
void
shell_new_rows(void) {
   int      h = (int)ROWS_AVAIL;

   if (!firstPor)   // not initialized yet
      return;
   if ((Unt)h < frame_minheight(topframeG, NULL))
      h = frame_minheight(topframeG, NULL);

   // First try setting the heights of portals with 'portfixheight'. If that
   // doesn't result in the right height, forget about that option.
   frame_new_height(topframeG, h, FALSE, TRUE, FALSE);
   if (!frame_check_height(topframeG, h))
      frame_new_height(topframeG, h, FALSE, FALSE, FALSE);

   computePosPortal();      // recompute portalRow and portalCol
   compute_cmdrow();
   curtab->ch_used = commlineHeightG;

   needRedrawTabpanelG = TRUE;
#if 0
   // Disabled: don't want making the screen smaller make a portal larger.
   if (p_ea)
   portEqualizeHeight(curPor, FALSE, 'v');
#endif
}

// Called from win_new_shellsize() after visibleColsG changed.
void
shell_new_columns(void) {
   if (!firstPor)   // not initialized yet
      return;

   int savePortalCol = firstPor->portalCol;
   Unt save_width = topframeG->width;
   int w = COLUMNS_WITHOUT_TPL();

   // First try setting the widths of portals with 'portfixwidth'.  If that
   // doesn't result in the right width, forget about that option.
   frameNewWidth(topframeG, w, FALSE, TRUE);
   if (!frame_check_width(topframeG, w))
      frameNewWidth(topframeG, w, FALSE, FALSE);

   computePosPortal();      // recompute portalRow and portalCol

   if (p_ea && firstPor->portalCol + topframeG->width == savePortalCol + save_width 
         && (firstPor->portalCol != savePortalCol || topframeG->width != save_width))
      portEqualizeHeight(curPor, FALSE, 0);

   needRedrawTabpanelG = TRUE;
#if 0
    // Disabled: don't want making the screen smaller make a portal larger.
   if (p_ea)
   portEqualizeHeight(curPor, FALSE, 'h');
#endif
}

// Save the size of all portals into "gap".
void
portalSaveSizes(OUT ArrayList *gap) {

   ga_init2(gap, sizeof(int), 1);
   if (ga_grow(gap, portCount() * 2 + 1) == FAIL)
      return;

   // first entry is the total lines available for portals
   ((int *)gap->c)[gap->len++] = ROWS_AVAIL - last_stl_height(FALSE);

   Portal   *po;
   FOR_ALL_PORTALS(po) {
      ((int *)gap->c)[gap->len++] = po->width + po->vsepWidth;
      ((int *)gap->c)[gap->len++] = po->height;
   }
}

// Restore portal sizes, but only if the number of portals is still the same
// and total lines available for portals didn't change. Does not free the growarray.
void
portRestoreSize(ArrayList *gap) {
    Portal   *po;
    int      i, j;

   if (portCount() * 2 + 1 == gap->len
       && ((int *)gap->c)[0] == ROWS_AVAIL - last_stl_height(FALSE)
   ){
      // The order matters, because frames contain other frames, but it's
      // difficult to get right. The easy way out is to do it twice.
      for (j = 0; j < 2; ++j) {
         i = 1;
         FOR_ALL_PORTALS(po) {
            frame_setwidth(po->frame, ((int *)gap->c)[i++]);
            portSetHeight(((int *)gap->c)[i++], po);
         }
      }
      // recompute the portal positions
      computePosPortal();
   }
}

// Update the position for all portals, using the width and height of the frames.
// Return the row just after the last portal.
void
computePosPortal(void) {
   int      row = 0;
   int      col = TPL_LCOL();
   frame_comp_pos(topframeG, &row, &col);
}

// Update the position of the portals in frame "topfrp", using the width and height of the frames.
// "*row" and "*col" are the top-left position of the frame.  They are updated
// to the bottom-right position plus one.
private void
frame_comp_pos(Frame *topfrp, int *row, int *col) {
   Frame   *fr;
   int      startcol;
   int      startrow;
   int      h;

   Portal* po = topfrp->port;
   if (po) {
      if (po->portalRow != *row || po->portalCol != *col) {
          // position changed, redraw
          po->portalRow = *row;
          po->portalCol = *col;
          redrawPortLater(po, UPD_NOT_VALID);
          po->statusLineNeedsRedraw = TRUE;
      }
      // WinBar will not show if the portal height is zero
      h = VISIBLE_HEIGHT(po) + po->statusHeight;
      *row += h > (int)topfrp->height ? (int)topfrp->height : h;
      *col += po->width + po->vsepWidth;
   } else {
      startrow = *row;
      startcol = *col;
      FOR_ALL_FRAMES(fr, topfrp->child) {
         if (topfrp->layout == FR_ROW)
            *row = startrow;   // all frames are at the same row
         else
            *col = startcol;   // all frames are at the same col
         frame_comp_pos(fr, row, col);
      }
   }
}

// Make the current portal show at least one line and one column.
void
portEnsureSize(void) {
   if (curPor->height == 0)
      portSetHeight(1, curPor);
   if (curPor->width == 0)
      portSetWidth(1, curPor);
}

// Set the height of portal "port" and take care of repositioning other portals to fit around it
void
portSetHeight(int height, Portal* port) {
   if (port == curPor) {
      // Always keep current portal at least one line high, even when 'winminheight' is 0
      if (height < MIN_PORTAL_HEIGHT)
         height = MIN_PORTAL_HEIGHT;
      if (height == 0)
         height = 1;
   }

   frame_setheight(port->frame, height + port->statusHeight);

   // recompute the portal positions
   computePosPortal();

   redraw_all_later(UPD_NOT_VALID);
}

// Set the height of a frame to "height" and take care that all frames and
// portals inside it are resized.  Also resize frames on the left and right if
// the are in the same FR_ROW frame.
//
// Strategy:
// If the frame is part of a FR_COL frame, try fitting the frame in that
// frame.  If that doesn't work (the FR_COL frame is too small), recursively
// go to containing frames to resize them and make room.
// If the frame is part of a FR_ROW frame, all frames must be resized as well.
// Check for the minimal height of the FR_ROW frame.
// At the top level we can also use change the command line height.
private void
frame_setheight(Frame *curfrp, int height) {
   int      room;      // total number of lines available
   int      take;      // number of lines taken from other portals
   int      room_cmdline;   // lines available from cmdline
   int      run;
   Frame   *fr;
   int      h;
   int      room_reserved;

   // If the height already is the desired value, nothing to do.
   if ((int)curfrp->height == height)
      return;

   if (!curfrp->parent) {
      // topframe: can only change the command line height
      if (height > 0)
         frame_new_height(curfrp, height, FALSE, FALSE, TRUE);
   } ei (curfrp->parent->layout == FR_ROW) {
      // Row of frames: Also need to resize frames left and right of this
      // one.  First check for the minimal height of these.
      h = frame_minheight(curfrp->parent, NULL);
      if (height < h)
          height = h;
      frame_setheight(curfrp->parent, height);
   } else {
      // Column of frames: try to change only frames in this column.
      //Do this twice:
      //1: compute room available, if it's not enough try resizing the
      //   containing frame.
      //2: compute the room available and adjust the height to it.
      //Try not to reduce the height of a portal with 'portfixheight' set.
      for (run = 1; run <= 2; ++run) {
         room = 0;
         room_reserved = 0;
         FOR_ALL_FRAMES(fr, curfrp->parent->child) {
            if (fr != curfrp && fr->port && fr->port->o.portFixHeight)
               room_reserved += fr->height;
            room += fr->height;
            if (fr != curfrp)
               room -= frame_minheight(fr, NULL);
         }
         if (curfrp->width != topframeG->width)
            room_cmdline = 0;
         else {
            room_cmdline = visibleRowsG - commlineHeightG - (lastPor->portalRow
                        + VISIBLE_HEIGHT(lastPor)
                        + lastPor->statusHeight);
            if (room_cmdline < 0)
                room_cmdline = 0;
         }

         if (height <= room + room_cmdline)
            break;
         if (run == 2 || curfrp->width == topframeG->width) {
            height = room + room_cmdline;
            break;
         }
         frame_setheight(
            curfrp->parent, 
            height + frame_minheight(curfrp->parent, NOPORT) - (int)MIN_PORTAL_HEIGHT - 1
         );
      }

      // Compute the number of lines we will take from others frames (can be negative!).
      take = height - curfrp->height;

      // If there is not enough room, also reduce the height of a portal
      // with 'portFixHeight' set.
      if (height > room + room_cmdline - room_reserved)
         room_reserved = room + room_cmdline - height;
      // If there is only a 'portFixHeight' portal and making the
      // portal smaller, need to make the other portal taller.
      if (take < 0 && room - (int)curfrp->height < room_reserved)
          room_reserved = 0;

      if (take > 0 && room_cmdline > 0) {
         // use lines from cmdline first
         if (take < room_cmdline)
            room_cmdline = take;
         take -= room_cmdline;
         topframeG->height += room_cmdline;
      }

      // set the current frame to the new height
      frame_new_height(curfrp, height, FALSE, FALSE, TRUE);

      // First take lines from the frames after the current frame.  If
      // that is not enough, takes lines from frames above the current frame.
      for (run = 0; run < 2; ++run) {
         if (run == 0)
            fr = curfrp->next;   // 1st run: start with next portal
         else
            fr = curfrp->prev;   // 2nd run: start with prev portal
         while (fr != NULL && take != 0) {
            h = frame_minheight(fr, NULL);
            if (room_reserved > 0
               && fr->port
               && fr->port->o.portFixHeight)
            {
               if (room_reserved >= (int)fr->height)
                  room_reserved -= fr->height;
               else {
                  if ((int)fr->height - room_reserved > take)
                     room_reserved = fr->height - take;
                  take -= fr->height - room_reserved;
                  frame_new_height(fr, room_reserved, FALSE, FALSE, TRUE);
                  room_reserved = 0;
                }
            } else {
               if ((int)fr->height - take < h) {
                  take -= fr->height - h;
                  frame_new_height(fr, h, FALSE, FALSE, TRUE);
               } else {
                  frame_new_height(fr, fr->height - take, FALSE, FALSE, TRUE);
                  take = 0;
               }
            }
            if (run == 0)
               fr = fr->next;
            else
               fr = fr->prev;
         }
      }
   }
}

void
portSetWidth(int width, Portal *po) {
   // Always keep current portal at least one column wide
   if (po == curPor) {
      if (width < MIN_PORTAL_WIDTH)
          width = MIN_PORTAL_WIDTH;
   } ei (width < 0)
      width = 0;

   frame_setwidth(po->frame, width + po->vsepWidth);

   // recompute the portal positions
   computePosPortal();

   redraw_all_later(UPD_NOT_VALID);
}

// Set the width of a frame to "width" and take care that all frames and
// portals inside it are resized.  Also resize frames above and below if the
// are in the same FR_ROW frame.
//
// Strategy is similar to frame_setheight()
//
// Set current portal width and take care of repositioning other portals to fit around it.
private void
frame_setwidth(Frame *curfrp, int width) {
   int      room;      // total number of lines available
   int      take;      // number of lines taken from other portals
   int      run;
   Frame   *fr;
   int      w;
   int      room_reserved;

   // If the width already is the desired value, nothing to do.
   if (curfrp->width == (Unt)width)
      return;

   if (!curfrp->parent)
      // topframe: can't change width
      return;

   if (curfrp->parent->layout == FR_COL) {
      // Column of frames: Also need to resize frames above and below of
      // this one.  First check for the minimal width of these.
      w = frame_minwidth(curfrp->parent, NULL);
      if (width < w)
          width = w;
      frame_setwidth(curfrp->parent, width);
   } else {
      //Row of frames: try to change only frames in this row.
      //
      //Do this twice:
      //1: compute room available, if it's not enough try resizing the
      //   containing frame.
      //2: compute the room available and adjust the width to it.
      for (run = 1; run <= 2; ++run) {
         room = 0;
         room_reserved = 0;
         FOR_ALL_FRAMES(fr, curfrp->parent->child) {
         if (fr != curfrp && fr->port && fr->port->o.portFixWidth)
            room_reserved += fr->width;
         room += fr->width;
         if (fr != curfrp)
            room -= frame_minwidth(fr, NULL);
         }

         if (width <= room)
            break;
         if (run == 2 || curfrp->height >= ROWS_AVAIL) {
            width = room;
            break;
         }
         frame_setwidth(
            curfrp->parent, 
            width + frame_minwidth(curfrp->parent, NOPORT) - (int)MIN_PORTAL_WIDTH - 1
         );
      }

      // Compute the number of lines we will take from others frames (can be negative!).
      take = width - curfrp->width;

      // If there is not enough room, also reduce the width of a portal with 'portfixwidth' set.
      if (width > room - room_reserved)
         room_reserved = room - width;
      // If there is only a 'portfixwidth' portal and making the
      // portal smaller, need to make the other portal narrower.
      if (take < 0 && room - (int)curfrp->width < room_reserved)
         room_reserved = 0;

      // set the current frame to the new width
      frameNewWidth(curfrp, width, FALSE, FALSE);

      // First take lines from the frames right of the current frame.  If
      // that is not enough, takes lines from frames left of the current frame.
      for (run = 0; run < 2; ++run) {
         if (run == 0)
           fr = curfrp->next;   // 1st run: start with next portal
         else
           fr = curfrp->prev;   // 2nd run: start with prev portal
         while (fr && take != 0) {
            w = frame_minwidth(fr, NULL);
            if (room_reserved > 0 && fr->port && fr->port->o.portFixWidth) {
               if (room_reserved >= (int)fr->width)
                  room_reserved -= fr->width;
               else {
                  if ((int)fr->width - room_reserved > take)
                     room_reserved = fr->width - take;
                  take -= fr->width - room_reserved;
                  frameNewWidth(fr, room_reserved, FALSE, FALSE);
                  room_reserved = 0;
               }
            } else {
               if ((int)fr->width - take < w) {
                  take -= fr->width - w;
                  frameNewWidth(fr, w, FALSE, FALSE);
               } else {
                  frameNewWidth(fr, fr->width - take, FALSE, FALSE);
                  take = 0;
               }
            }
            if (run == 0)
               fr = fr->next;
            else
               fr = fr->prev;
         }
      }
   }
}

// Status line of dragwin is dragged "offset" lines down (negative is up).
void
portDragStatusLine(Portal *dragwin, int offset) {
   int      room;
   int      up;   // if TRUE, drag status line up, otherwise down
   int      n;

   Frame* fr = dragwin->frame;
   Frame* curfr = fr;
   if (fr != topframeG) {     // more than one portal
      fr = fr->parent;
      // When the parent frame is not a column of frames, its parent should be.
      if (fr->layout != FR_COL) {
         curfr = fr;
         if (fr != topframeG)   // only a row of portals, may drag statusline
            fr = fr->parent;
      }
   }

   // If this is the last frame in a column, may want to resize the parent
   // frame instead (go two up to skip a row of frames).
   while (curfr != topframeG && !curfr->next) {
      if (fr != topframeG)
         fr = fr->parent;
      curfr = fr;
      if (fr != topframeG)
          fr = fr->parent;
   }

   if (offset < 0) { // drag up
      up = TRUE;
      offset = -offset;
      // sum up the room of the current frame and above it
      if (fr == curfr) {
         // only one portal
         room = fr->height - frame_minheight(fr, NULL);
      } else {
         room = 0;
         for (fr = fr->child; ; fr = fr->next) {
            room += fr->height - frame_minheight(fr, NULL);
            if (fr == curfr)
               break;
         }
      }
      fr = curfr->next;      // put fr at frame that grows
   } else {   // drag down
      up = FALSE;
      // Only dragging the last status line can reduce commlineHeightG.
      room = visibleRowsG - commlineRowG;
      if (curfr->next == NULL)
         --room;
      else
         room -= commlineHeightG;
      if (room < 0)
         room = 0;
      // sum up the room of frames below of the current one
      FOR_ALL_FRAMES(fr, curfr->next) {
         room += fr->height - frame_minheight(fr, NULL);
      } 
      fr = curfr;         // put fr at portal that grows
   }

   if (room < offset)      // Not enough room
      offset = room;      // Move as far as we can
   if (offset <= 0)
      return;

   // Grow frame fr by "offset" lines. Doesn't happen when dragging the last status line up.
   if (fr)
      frame_new_height(fr, fr->height + offset, up, FALSE, TRUE);

   if (up)
      fr = curfr;      // current frame gets smaller
   else
      fr = curfr->next;   // next frame gets smaller

   // Now make the other frames smaller.
   while (fr != NULL && offset > 0) {
      n = frame_minheight(fr, NULL);
      if ((int)fr->height - offset <= n) {
         offset -= fr->height - n;
         frame_new_height(fr, n, !up, FALSE, TRUE);
      } else {
         frame_new_height(fr, fr->height - offset, !up, FALSE, TRUE);
         break;
      }
      if (up)
         fr = fr->prev;
      else
         fr = fr->next;
   }
   computePosPortal();

   redraw_all_later(UPD_SOME_VALID);
   showmode();
}

// Separator line of dragwin is dragged "offset" lines right (negative is left).
void
portDragVsepLine(Portal *dragwin, int offset) {
   int      room;
   int      left;   // if TRUE, drag separator line left, otherwise right
   int      n;

   Frame* fr = dragwin->frame;
   if (fr == topframeG)      // only one portal (cannot happen?)
      return;
   Frame* curfr = fr;
   fr = fr->parent;
   // When the parent frame is not a row of frames, its parent should be.
   if (fr->layout != FR_ROW) {
      if (fr == topframeG)   // only a column of portals (cannot happen?)
         return;
      curfr = fr;
      fr = fr->parent;
   }

   // If this is the last frame in a row, may want to resize a parent frame instead.
   while (!curfr->next) {
      if (fr == topframeG)
         break;
      curfr = fr;
      fr = fr->parent;
      if (fr != topframeG) {
         curfr = fr;
         fr = fr->parent;
      }
   }

   if (offset < 0) {// drag left
      left = TRUE;
      offset = -offset;
      // sum up the room of the current frame and left of it
      room = 0;
      for (fr = fr->child; ; fr = fr->next) {
         room += fr->width - frame_minwidth(fr, NULL);
         if (fr == curfr)
            break;
      }
      fr = curfr->next;      // put fr at frame that grows
   } else {   // drag right
      left = FALSE;
      // sum up the room of frames right of the current one
      room = 0;
      FOR_ALL_FRAMES(fr, curfr->next) {
         room += fr->width - frame_minwidth(fr, NULL);
      } 
      fr = curfr;         // put fr at portal that grows
   }

   if (room < offset)      // Not enough room
      offset = room;      // Move as far as we can
   if (offset <= 0)      // No room at all, quit.
      return;
   if (!fr)
      // This can happen when calling win_move_separator() on the rightmost
      // portal. Just don't do anything.
      return;

   // grow frame fr by offset lines
   frameNewWidth(fr, fr->width + offset, left, FALSE);

   // shrink other frames: current and at the left or at the right
   if (left)
      fr = curfr;      // current frame gets smaller
   else
      fr = curfr->next;   // next frame gets smaller

   while (fr != NULL && offset > 0) {
      n = frame_minwidth(fr, NULL);
      if ((int)fr->width - offset <= n) {
         offset -= fr->width - n;
         frameNewWidth(fr, n, !left, FALSE);
      } else {
         frameNewWidth(fr, fr->width - offset, !left, FALSE);
         break;
      }
      if (left)
         fr = fr->prev;
      else
         fr = fr->next;
   }
   computePosPortal();
   redraw_all_later(UPD_NOT_VALID);
}

#define FRACTION_MULT   16384L

// Set po->fraction for the current cursorRow and height.
// Has no effect when the portal is less than two lines.
void
set_fraction(Portal *po) {
   if (po->height > 1)
      // When cursor is in the first line the percentage is computed as if
      // it's halfway that line.  Thus with two lines it is 25%, with three
      // lines 17%, etc.  Similarly for the last line: 75%, 83%, etc.
      po->fraction = ((long)po->cursorRow * FRACTION_MULT + FRACTION_MULT / 2) / (long)po->height;
}

// Set the height of a portal. "height" excludes any portal toolbar.
// This takes care of the things inside the portal, not what happens to the portal position, the 
// frame or to other portal.
private void
portalNewHeight(Portal* po, int height) {
   Unt      prevHeight = po->height;

   //Don't want a negative height.  Happens when splitting a tiny portal.
   //Will equalize heights soon to fix it.
   if (height < 0)
      height = 0;
   if (po->height == (Unt)height)
      return;       // nothing to do

   if (po->height > 0) {
      if (po == curPor)
          // cursorRow needs to be valid. When setting 'laststatus' this may
          // call portalNewHeight() recursively.
          validate_cursor();
      if (po->height != prevHeight)
          return;  // Recursive call already changed the size, bail out here
              //   to avoid the following to mess things up.
      if (po->cursorRow != po->prevFraction)
          set_fraction(po);
   }

   po->height = height;
   po->statusLineNeedsRedraw = TRUE;
   portComputeScroll(po);

   // There is no point in adjusting the scroll position when exiting.  Some
   // values might be invalid.
   if (!exiting) {
      po->skipCol = 0;
      scroll_to_fraction(po, prevHeight);
   }
}

//{{{cursor functions

//Make sure curPor->cursor.lnum is valid.
void
check_cursor_lnum(void) {
   if (curPor->cursor.lnum > curBook->mem.lineCount) {
      // If there is a closed fold at the end of the file, put the cursor in
      // its first line.  Otherwise in the last line.
      if (!getFolds(curBook->mem.lineCount, &curPor->cursor.lnum, NULL))
         curPor->cursor.lnum = curBook->mem.lineCount;
   }
   if (curPor->cursor.lnum <= 0)
      curPor->cursor.lnum = 1;
}


//Make sure curPor->cursor.col is valid.
void
check_cursor_col(void) {
   check_cursor_col_win(curPor);
}

//Set "curPor->leftCol" to "leftcol". Adjust the cursor position if needed. Return TRUE if the 
//cursor was moved.
int
set_leftcol(ColNr leftcol) {
   int      retval = FALSE;

   // Return quickly when there is no change.
   if (curPor->leftCol == leftcol)
      return FALSE;
   curPor->leftCol = leftcol;

   changed_cline_bef_curs();
   long lastcol = curPor->leftCol + curPor->width - curPor_col_off() - 1;
   validate_virtcol();

   // If the cursor is right or left of the screen, move it to last or first
   // visible character.
   long siso = get_sidescrolloff_value();
   if (curPor->virtCol > (ColNr)(lastcol - siso)) {
      retval = TRUE;
      coladvance((ColNr)(lastcol - siso));
   } ei (curPor->virtCol < curPor->leftCol + siso) {
      retval = TRUE;
      (void)coladvance((ColNr)(curPor->leftCol + siso));
   }

    // If the start of the character under the cursor is not on the screen,
    // advance the cursor one more char.  If this fails (last char of the
    // line) adjust the scrolling.
    ColNr   s, e;
    bookGetVirtualColInVirtualMode(curPor, &curPor->cursor, &s, NULL, &e);
    if (e > (ColNr)lastcol) {
      retval = TRUE;
      coladvance(s - 1);
   } ei (s < curPor->leftCol) {
      retval = TRUE;
      if (coladvance(e + 1) == FAIL) { // there isn't another character
         curPor->leftCol = s;   // adjust leftCol instead
         changed_cline_bef_curs();
      }
   }

   if (retval)
      curPor->setCursWant = true;
   redraw_later(UPD_NOT_VALID);
   return retval;
}


//Make sure po->cursor.col is valid.
void
check_cursor_col_win(Portal* po) {
   ColNr      oldcol = po->cursor.col;

   ColNr len = memGetBookLen(po->book, po->cursor.lnum);
   if (len == 0)
      po->cursor.col = 0;
   ei (po->cursor.col >= len) {
      // Allow cursor past end-of-line when:
      // - in Insert mode or restarting Insert mode
      // - in Visual mode and 'selection' isn't "old"
      // - 'virtualedit' is set
      if ((stateG & MODE_INSERT) || restart_edit || VIsual_active || virtual_active())
         po->cursor.col = len;
      else {
         po->cursor.col = len - 1;
         // Move the cursor to the head byte.
         mb_adjustpos(po->book, &po->cursor);
      }
   } ei (po->cursor.col < 0)
      po->cursor.col = 0;

   if (oldcol == MAXCOL)
      po->cursor.coladd = 0;
}

//make sure curPor->cursor in on a valid character
void
check_cursor(void) {
   check_cursor_lnum();
   check_cursor_col();
}

//Check if VIsual position is valid, correct it if not.
//Can be called when in Visual mode and a change has been made.
void
check_visual_pos(void) {
   if (VIsual.lnum > curBook->mem.lineCount) {
      VIsual.lnum = curBook->mem.lineCount;
      VIsual.col = 0;
      VIsual.coladd = 0;
   } else {
      int len = ml_get_len(VIsual.lnum);

      if (VIsual.col > len) {
          VIsual.col = len;
          VIsual.coladd = 0;
      }
   }
}

//Make sure curPor->cursor is not on the ZERO at the end of the line.
//Allow it when in Visual mode and 'selection' is not "old".
void
adjust_cursor_col(void) {
   if (curPor->cursor.col > 0 && !VIsual_active
          && gchar_cursor() == ZERO
   ) {
      --curPor->cursor.col;
   }
}


//}}}
//{{{portal line countin'

//Return the number of portal lines occupied by book line "lnum". Include any filler lines.
int
plines(LineNr lnum) {
   return plines_win(curPor, lnum, TRUE);
}

int
plines_win(
   Portal   *wp,
   LineNr   lnum,
   int      limit_winheight)   // when TRUE limit to portal height
{
   // Check for filler lines above this book line. When folded the result is one line anyway.
   return plines_win_nofill(wp, lnum, limit_winheight) + diff_check_fill(wp, lnum);
}

//Return the number of portal lines occupied by book line "lnum". Do not include filler lines.
int
plines_nofill(LineNr lnum) {
   return plines_win_nofill(curPor, lnum, TRUE);
}

int
plines_win_nofill(
    Portal   *wp,
    LineNr   lnum,
    int      limit_winheight)   // when TRUE limit to portal height
{
    int      lines;

    if (wp->width == 0)
   return 1;

    // Folded lines are handled just like an empty line.
    // NOTE: Caller must handle lines that are MAYBE folded.
    if (lineFolded(wp, lnum) == TRUE)
   return 1;

   if (!wp->o.wrap)
      // add a line for each "above" and "below" aligned text property
      lines = 1  + prop_count_above_below(wp->book, lnum);
   else
      lines = plines_win_nofold(wp, lnum);

   if (limit_winheight && lines > (int)wp->height)
      return wp->height;
   return lines;
}

//Return number of portal lines physical line "lnum" will occupy in portal
//"wp". Does not care about folding, 'wrap' or 'diff'.
int
plines_win_nofold(Portal *wp, LineNr lnum) {
   CS s = memGetLine(wp->book, lnum, false);
   CharTableSize cts;
   bookInitCharsForKeywordsSizeArg(OUT &cts, wp, lnum, 0, s, s);
   if (*s == ZERO && !cts.cts_has_prop_with_text)
      return 1; // be quick for an empty line
   drawLineOnScreentabsize_cts(&cts, (ColNr)MAXCOL);
   clear_chartabsize_arg(&cts);
   long col = (int)cts.cts_vcol;

   // If list mode is on, then the '$' at the end of the line may take up one
   // extra column.
   if (wp->o.list && listCharsG.eol != ZERO)
      col += 1;

   //Add column offset for 'number', 'relativenumber' and 'foldcolumn'.
   int width = wp->width - normalPortalColumnOffset(wp);
   if (width <= 0)
      return 32000;
   if (col <= width)
      return 1;
   col -= width;
   return (col + (width - 1)) / width + 1;
}

//Return number of portal lines the physical line range from "first" until
//"last" will occupy in portal "wp". Takes into account folding, 'wrap',
//topfill and filler lines beyond the end of the book. Limit to "max" lines.
int
plines_m_win(Portal *wp, LineNr first, LineNr last, int max) {
   int      count = 0;

   while (first <= last && count < max) {
      int   x;

      // Check if there are any really folded lines, but also included lines
      // that are maybe folded.
      x = foldedCount(wp, first, NULL);
      if (x > 0) {
         ++count;       // count 1 for "+-- folded" line
         first += x;
      } else {
         if (first == wp->topLine)
            count += plines_win_nofill(wp, first, FALSE) + wp->topFill;
         else
            count += plines_win(wp, first, FALSE);
         ++first;
      }
   }
   if (first == wp->book->mem.lineCount + 1)
      count += diff_check_fill(wp, first);
   return MIN(max, count);
}


//Like plines_win(), but only reports the number of physical screen lines
//used from the start of the line to the given column number.
private int
plinesUpToCol(Portal *wp, LineNr lnum, long column) {
   //Check for filler lines above this book line. When folded, the result is one line anyway.
   int lines = diff_check_fill(wp, lnum);

   if (!wp->o.wrap)
      return lines + 1;

   if (wp->width == 0)
      return lines + 1;

   CS line = memGetLine(wp->book, lnum, false);

   CharTableSize cts;
   bookInitCharsForKeywordsSizeArg(OUT &cts, wp, lnum, 0, line, line);
   while (*cts.cts_ptr != ZERO && --column >= 0) {
      cts.cts_vcol += win_lbr_chartabsize(&cts, NULL);
      MB_PTR_ADV(cts.cts_ptr);
   }

   //If *cts.cts_ptr is a TAB, and the TAB is not displayed as ^I, and we're not in MODE_INSERT 
   //state, then col must be adjusted so that it represents the last screen position of the TAB. 
   //This only fixes an error when the TAB wraps from one screen line to the next (when
   //'columns' is not a multiple of 'ts') -- webb.
   long col = cts.cts_vcol;
   if (*cts.cts_ptr == TAB && (stateG & MODE_NORMAL) && (!wp->o.list || listCharsG.tab1))
      col += win_lbr_chartabsize(&cts, NULL) - 1;
   clear_chartabsize_arg(&cts);

   //Add column offset for 'number', 'relativenumber', 'foldcolumn', etc.
   int width = wp->width - normalPortalColumnOffset(wp);
   if (width <= 0)
      return 9999;

   lines += 1;
   if (col > width)
      lines += (col - width) / width + 1;
   return lines;
}

//}}}

void
scroll_to_fraction(Portal *po, int prevHeight) {
   LineNr   lnum;
   int      sline, line_size;
   int      height = po->height;

   // Don't change topLine in any of these cases:
   // - portal height is 0
   // - 'scrollbind' is set and this isn't the current portal
   // - portal height is sufficient to display the whole book and first line is visible.
   if (height > 0
      && (!po->o.scrollBind || po == curPor)
      && (height < po->book->mem.lineCount || po->topLine > 1)
   ) {
      // Find a value for topLine that shows the cursor at the same
      // relative position in the portal as before (more or less).
      lnum = po->cursor.lnum;
      if (lnum < 1)      // can happen when starting up
         lnum = 1;
      po->cursorRow = ((long)po->fraction * (long)height - 1L) / FRACTION_MULT;
      line_size = plinesUpToCol(po, lnum, (long)(po->cursor.col)) - 1;
      sline = po->cursorRow - line_size;

      if (sline >= 0) {
         // Make sure the whole cursor line is visible, if possible.
         int rows = plines_win(po, lnum, FALSE);
         if (sline > (int)po->height - rows) {
            sline = po->height - rows;
            po->cursorRow -= rows - line_size;
         }
      }

      if (sline < 0) {
         // Cursor line would go off top of screen if cursorRow was this high.
         // Make cursor line the first line in the portal. If not enough room, use skipCol.
         po->cursorRow = line_size;
         if (po->cursorRow >= (int)po->height && (po->width - normalPortalColumnOffset(po)) > 0) {
            po->skipCol += po->width - normalPortalColumnOffset(po);
            --po->cursorRow;
            while (po->cursorRow >= (int)po->height) {
               po->skipCol += po->width - normalPortalColumnOffset(po);
               --po->cursorRow;
            }
         }
      } ei (sline > 0) {
         while (sline > 0 && lnum > 1) {
            getFoldsPortal(po, lnum, &lnum, NULL, TRUE, NULL);
            if (lnum == 1) {
               // first line in book is folded
               line_size = 1;
               --sline;
               break;
            }
            --lnum;
            if (lnum == po->topLine)
               line_size = plines_win_nofill(po, lnum, TRUE) + po->topFill;
            else
               line_size = plines_win(po, lnum, TRUE);
            sline -= line_size;
         }

         if (sline < 0) {
            // Line we want at top would go off top of screen.  Use next line instead.
            getFoldsPortal(po, lnum, NULL, &lnum, TRUE, NULL);
            lnum++;
            po->cursorRow -= line_size + sline;
         } ei (sline > 0) {
            // First line of file reached, use that as topline.
            lnum = 1;
            po->cursorRow -= sline;
         }
      }
      set_topline(po, lnum);
   }

   if (po == curPor)
      curs_columns(FALSE);   // validate cursorRow

   if (prevHeight > 0)
      po->prevFraction = po->cursorRow;

    redrawPortLater(po, UPD_SOME_VALID);
    invalidate_botline_win(po);
}

// Set the width of a portal
private void
portalNewWidth(Portal *po, int width) {
   // Should we give an error if width < 0?
   po->width = width < 0 ? 0 : width;
   po->validLines = 0;
   changed_line_abv_curs_win(po);
   invalidate_botline_win(po);

   if (po == curPor)
      curs_columns(TRUE);   // validate cursorRow

   redrawPortLater(po, UPD_NOT_VALID);
   po->statusLineNeedsRedraw = TRUE;
}

void
portComputeScroll(Portal *po) {
   po->scroll = ((Unt)po->height >> 1);
   if (po->scroll == 0)
      po->scroll = 1;
}

// Command_height: called whenever commlineHeightG has been changed.
void
command_height(void) {
   int      old_p_ch = curtab->ch_used;

   // Find bottom frame with width of screen.
   Frame *fr = lastPor->frame;
   while (fr->width != topframeG->width && fr->parent != NULL)
   fr = fr->parent;

   // Avoid changing the height of a portal with 'portfixheight' set.
   while (fr->prev != NULL && fr->layout == FR_LEAF && fr->port->o.portFixHeight)
      fr = fr->prev;

   while (commlineHeightG > old_p_ch && command_frame_height) {
      if (!fr) {
         emsg(_(e_not_enough_room));
         commlineHeightG = old_p_ch;
         break;
      }
      int h = MIN(commlineHeightG - old_p_ch, fr->height - frame_minheight(fr, NULL));
      frame_add_height(fr, -h);
      old_p_ch += h;
      fr = fr->prev;
   }
   if (commlineHeightG < old_p_ch && command_frame_height && fr != NULL)
      frame_add_height(fr, (int)(old_p_ch - commlineHeightG));

   // Recompute portal positions.
   computePosPortal();
   commlineRowG = visibleRowsG - commlineHeightG;
   redrawCommlineG = TRUE;

   // Clear the commheight area.
   if (msg_scrolled == 0 && fullScreenG) {
      fillRowsWithTwoChars(commlineRowG, (int)visibleRowsG, 0, (int)visibleColsG, ' ', ' ', 0);
      msgRowG = commlineRowG;
   }

    // Use the value of commlineHeightG that we remembered.  This is needed for when the
    // GUI starts up, we can't be sure in what order things happen.  And when
    // commlineHeightG was changed in another tab
    curtab->ch_used = commlineHeightG;
    min_set_ch = commlineHeightG;
}

// Resize frame "fr" to be "n" lines higher (negative for less high).
// Also resize the frames it is contained in.
private void
frame_add_height(Frame *fr, int n) {
   frame_new_height(fr, fr->height + n, FALSE, FALSE, FALSE);
   for (;;) {
      fr = fr->parent;
      if (!fr)
         break;
      fr->height += n;
   }
}

// Add or remove a status line for the bottom portal(s), according to the value of 'laststatus'.
void
last_status(int morePorts) {  // pretend there are two or more portals
   // Don't make a difference between horizontal or vertical split.
   last_status_rec(topframeG, last_stl_height(morePorts) > 0);
}

private void
last_status_rec(Frame *fr, int statusline) {
   Frame   *fp;
   Portal   *po;
   if (fr->layout == FR_LEAF) {
      po = fr->port;
   if (po->statusHeight != 0 && !statusline) {
      // remove status line
      portalNewHeight(po, po->height + 1);
      po->statusHeight = 0;
      computeColumnsForRulerAndCommand();
   } ei (po->statusHeight == 0 && statusline) {
      // Find a frame to take a line from.
      fp = fr;
      while (fp->height <= frame_minheight(fp, NULL)) {
         if (fp == topframeG) {
            emsg(_(e_not_enough_room));
            return;
         }
         // In a column of frames: go to frame above.  If already at
         // the top or in a row of frames: go to parent.
         if (fp->parent->layout == FR_COL && fp->prev != NULL)
            fp = fp->prev;
         else
            fp = fp->parent;
      }
      po->statusHeight = 1;
      if (fp != fr) {
         frame_new_height(fp, fp->height - 1, FALSE, FALSE, FALSE);
         frame_fix_height(po);
         computePosPortal();
      } else
         portalNewHeight(po, po->height - 1);
      computeColumnsForRulerAndCommand();
      redraw_all_later(UPD_SOME_VALID);
   }
   // Set prevHeight when difference is due to 'laststatus'.
   if (abs((int)po->height - (int)po->prevHeight) == 1)
       po->prevHeight = po->height;
   } ei (fr->layout == FR_ROW) {
      // vertically split portals, set status line for each one
      FOR_ALL_FRAMES(fp, fr->child)
         last_status_rec(fp, statusline);
   } else {
      // horizontally split portal, set status line for last one
      for (fp = fr->child; fp->next != NULL; fp = fp->next)
         {} 
      last_status_rec(fp, statusline);
   }
}

// Return the height of the last portal's statusline.
int
last_stl_height(int morePorts) {  // pretend there are two or more portals
   return (morePorts || !ONLY_ONE_PORTAL) ? STATUS_HEIGHT : 0;
}

// Return the minimal number of rows that is needed on the screen to display
// the current number of portals.
int
min_rows(void) {
   if (!firstPor)   // not initialized yet
      return MIN_LINES;

   return frame_minheight(curtab->topframe, NULL) + MIN_COMMHEIGHT;
}

// The minimal number of rows that is needed on the screen to display
// the current number of portals for all tabs. Is no less than 2
Unt
minRowsForAllTabs(void) {
   if (!firstPor)   // not initialized yet
      return MIN_LINES;

   Unt total = 0;
   Tab* t;
   FOR_ALL_TABS(t) {
      Unt n = frame_minheight(t->topframe, NULL);
      if (total < n)
         total = n;
   }
   total += MIN_COMMHEIGHT;
   return total;
}

//Return TRUE if there is only one portal and only one tab, not counting a help or preview portal, 
//unless it is the current portal. Do not count unlisted portals.
Boole
onlyOnePortal(void) {
   // If the current portal is a popup then there always is another portal.
   if (popup_is_popup(curPor))
      return false;

   // If there is another tab there always is another portal.
   if (firstTabG->next)
      return false;

   Unt      count = 0;
   Portal   *po;
   FOR_ALL_PORTALS(po) {
      if (po->book
         && (!((bookIsHelp(po->book) && !bookIsHelp(curBook))
             || po->isPreview
           ) || po == curPor) && !is_autoCommPort(po))
          ++count;
   } 
   return (count <= 1);
}

// Implementation of check_lnums() and check_lnums_nested().
private void
check_lnums_both(int do_curPor, int nested) {
   Portal   *po;
   Tab   *t;
   FOR_ALL_TAB_PORTALS(t, po) {
      if ((do_curPor || po != curPor) && po->book == curBook) {
         int need_adjust;

         if (!nested) {
            // save the original cursor position and topline
            po->cursorSaved.cursor_save = po->cursor;
            po->cursorSaved.topLineSave = po->topLine;
         }

         need_adjust = po->cursor.lnum > curBook->mem.lineCount;
         if (need_adjust)
            po->cursor.lnum = curBook->mem.lineCount;
         if (need_adjust || !nested)
            // save the (corrected) cursor position
            po->cursorSaved.cursor_corr = po->cursor;

         need_adjust = po->topLine > curBook->mem.lineCount;
         if (need_adjust)
            po->topLine = curBook->mem.lineCount;
         if (need_adjust || !nested)
            // save the (corrected) topline
            po->cursorSaved.topLineCorr = po->topLine;
      }
   } 
}

// Correct the cursor line number in other portals.  Used after changing the
// current book, and before applying autocommands.
// When "do_curPor" is TRUE, also check current portal.
void
check_lnums(int do_curPor) {
   check_lnums_both(do_curPor, FALSE);
}

// Like check_lnums() but for when check_lnums() was already called.
void
check_lnums_nested(int do_curPor) {
   check_lnums_both(do_curPor, TRUE);
}

// Reset cursor and topline to its stored values from check_lnums().
// check_lnums() must have been called first!
void
reset_lnums(void) {
   Portal   *po;
   Tab   *t;

   FOR_ALL_TAB_PORTALS(t, po) {
      if (po->book == curBook) {
         // Restore the value if the autocommand didn't change it and it was set.
         // Note: This triggers e.g. on BufReadPre, when the book is not yet
         //       loaded, so cannot validate the book line
         if (EQUAL_POS(po->cursorSaved.cursor_corr, po->cursor)
                 && po->cursorSaved.cursor_save.lnum != 0) {
            po->cursor = po->cursorSaved.cursor_save;
         } 
         if (po->cursorSaved.topLineCorr == po->topLine && po->cursorSaved.topLineSave != 0)
            po->topLine = po->cursorSaved.topLineSave;
         if (po->cursorSaved.topLineSave > po->book->mem.lineCount)
            po->cacheState &= ~VALID_TOPLINE;
      }
   } 
}

// A snapshot of the portal sizes, to restore them after closing the help
// or other portal.
// Only these fields are used:
// layout
// width
// height
// next
// child
// port (only valid for the old curPor, NULL otherwise)

// Create a snapshot of the current frame sizes. "idx" is SNAP_HELP_IDX or SNAP_AUCMD_IDX.
// Return FAIL if out of memory, OK otherwise.
int
make_snapshot(int idx) {
   clearSnapshot(curtab, idx);
   if (make_snapshot_rec(topframeG, &curtab->snapshot[idx]) == FAIL) {
      clearSnapshot(curtab, idx);
      return FAIL;
   }
   return OK;
}

private int
make_snapshot_rec(Frame *source, OUT Frame **fr) {
   *fr = ALLOC_CLEAR_ONE(Frame);
   (*fr)->layout = source->layout;
   (*fr)->width = source->width;
   (*fr)->height = source->height;
   if ((source->next && make_snapshot_rec(source->next, &((*fr)->next)) == FAIL)
       || (source->child && make_snapshot_rec(source->child, &((*fr)->child)) == FAIL)
   ) {
      return FAIL;
   }
   if (source->layout == FR_LEAF && source->port == curPor)
      (*fr)->port = curPor;
   return OK;
}

// Remove any existing snapshot.
private void
clearSnapshot(Tab *t, int idx) {
   clearSnapshot_rec(t->snapshot[idx]);
   t->snapshot[idx] = NULL;
}

private void
clearSnapshot_rec(Frame *fr) {
   if (!fr)
      return;
   clearSnapshot_rec(fr->next);
   clearSnapshot_rec(fr->child);
   eeglFree(fr);
}

// Traverse a snapshot to find the previous curPor.
private Portal *
get_snapshot_curPor_rec(Frame *ft) {
   Portal   *po;

   if (ft->next) {
      if ((po = get_snapshot_curPor_rec(ft->next)) != NULL)
         return po;
   }
   if (ft->child) {
      if ((po = get_snapshot_curPor_rec(ft->child)) != NULL)
         return po;
   }

   return ft->port;
}

// Return the current portal stored in the snapshot or NULL.
private Portal *
get_snapshot_curPor(int idx) {
   if (curtab->snapshot[idx] == NULL)
      return NULL;

   return get_snapshot_curPor_rec(curtab->snapshot[idx]);
}

//Restore a previously created snapshot, if there is any.
//This is only done if the screen size didn't change and the portal layout is still the same.
//"idx" is SNAP_HELP_IDX or SNAP_AUCMD_IDX.
void
restore_snapshot(
   int      idx,
   int      close_curPor)       // closing current portal
{
   if (curtab->snapshot[idx]
       && curtab->snapshot[idx]->width == topframeG->width
       && curtab->snapshot[idx]->height == topframeG->height
       && check_snapshot_rec(curtab->snapshot[idx], topframeG) == OK)
    {
      Portal* po = restore_snapshot_rec(curtab->snapshot[idx], topframeG);
      computePosPortal();
      if (po && close_curPor)
         gotoPortal(po);
      redraw_all_later(UPD_NOT_VALID);
   }
   clearSnapshot(curtab, idx);
}

//Check if frames "sn" and "fr" have the same layout, same following frames
//and same children. And the portal pointer is valid.
private int
check_snapshot_rec(Frame *sn, Frame *fr) {
   if (sn->layout != fr->layout
          || (!sn->next) != (!fr->next)
          || (!sn->child) != (!fr->child)
          || (sn->next && check_snapshot_rec(sn->next, fr->next) == FAIL)
          || (sn->child && check_snapshot_rec(sn->child, fr->child) == FAIL)
          || (sn->port && !portalIsValid(sn->port)))
      return FAIL;
   return OK;
}

// Copy the size of snapshot frame "sn" to frame "fr".  Do the same for all following frames and 
// children. Return a pointer to the old current portal, or NULL.
private Portal *
restore_snapshot_rec(Frame *sn, Frame *fr) {
   Portal   *po = NULL;
   Portal   *wp2;

   fr->height = sn->height;
   fr->width = sn->width;
   if (fr->layout == FR_LEAF) {
      frame_new_height(fr, fr->height, FALSE, FALSE, FALSE);
      frameNewWidth(fr, fr->width, FALSE, FALSE);
      po = sn->port;
   }
   if (sn->next) {
      wp2 = restore_snapshot_rec(sn->next, fr->next);
      if (wp2)
         po = wp2;
   }
   if (sn->child) {
      wp2 = restore_snapshot_rec(sn->child, fr->child);
      if (wp2 != NULL)
          po = wp2;
   }
   return po;
}

// Return TRUE if "topfrp" and its children are at the right height.
private int
frame_check_height(Frame *topfrp, int height) {
   Frame *fr;

   if (topfrp->height != (Unt)height)
      return FALSE;

   if (topfrp->layout == FR_ROW) {
      FOR_ALL_FRAMES(fr, topfrp->child) {
         if (fr->height != (Unt)height)
            return FALSE;
      } 
   } 

   return TRUE;
}

// Return TRUE if "topfrp" and its children are at the right width.
private int
frame_check_width(Frame *topfrp, int width) {
   Frame *fr;

   if (topfrp->width != (Unt)width)
      return FALSE;

   if (topfrp->layout == FR_COL) {
      FOR_ALL_FRAMES(fr, topfrp->child) {
         if (fr->width != (Unt)width)
            return FALSE;
      } 
   }

   return TRUE;
}

// Simple int comparison function for use with qsort()
//private int
//intComparer(const void *pa, const void *pb) {
//   const int a = *(const int *)pa;
//   int const b = *(const int *)pb;
//   if (a > b)
//      return 1;
//   if (a < b)
//      return -1;
//   return 0;
//}

int
getLastPortId(void) {
   return lastPortIdS;
}

// Don't let autocommands close the given portal
int
portalLocked(Portal *po) {
   return po->locked;
}

//}}}
//{{{evaluations for portals

private int
getPortalId(Var *argvars) {
   if (argvars[0].tag == VAR_UNKNOWN)
      return curPor->id;
   int portId = tv_get_number(&argvars[0]);
   if (portId <= 0)
      return 0;
      
   Portal   *po;
   if (argvars[1].tag == VAR_UNKNOWN)
      po = firstPor;
   else {
      Tab   *t;
      int tabnr = tv_get_number(&argvars[1]);

      FOR_ALL_TABS(t) {
         if (--tabnr == 0)
            break;
      } 
      if (!t)
         return -1;
      if (t == curtab)
         po = firstPor;
      else
         po = t->firstPor;
   }
   for ( ; po; po = po->next) {
      if (--portId == 0)
         return po->id;
   } 
   return 0;
}

private void
portIdToTabPort(Var *argvars, List *list) {
   Portal   *po;
   Tab   *t;
   int      portId = 1;
   int      tabnr = 1;
   int      id = tv_get_number(&argvars[0]);

   FOR_ALL_TABS(t) {
      FOR_ALL_PORTALS_IN_TAB(t, po) {
         if (po->id == id) {
            list_append_number(list, tabnr);
            list_append_number(list, portId);
            return;
         }
         ++portId;
      }
      ++tabnr;
      portId = 1;
   }
   list_append_number(list, 0);
   list_append_number(list, 0);
}

// Return the portal pointer of portal "id".
Portal *
getPortalById(int id) {
   return getPortAndTab(id, NULL);
}

// Return the portal and tab pointer of portal "id". Returns NULL when not found.
Portal *
getPortAndTab(int id, OUT Tab **tpp) {
   Portal   *po;
   Tab   *t;

   FOR_ALL_TAB_PORTALS(t, po) {
      if (po->id == id) {
         if (tpp)
            *tpp = t;
         return po;
      }
   } 
   
   // popup portals are in separate lists
   FOR_ALL_TABS(t) {
      FOR_ALL_POPUPPORTS_IN_TAB(t, po) {
         if (po->id == id) {
            if (tpp != NULL)
               *tpp = t;
            return po;
         }
      } 
   } 
   FOR_ALL_POPUPPORTS(po) {
      if (po->id == id) {
         if (tpp != NULL)
            *tpp = curtab;  // any tabpage would do
         return po;
      }
   } 

   return NULL;
}

private int
getPortById(Var *argvars) {
   Portal   *po;
   int       nr = 1;
   int       id = tv_get_number(&argvars[0]);

   FOR_ALL_PORTALS(po) {
      if (po->id == id)
          return nr;
      ++nr;
   }
   return 0;
}

// Find portal specified by "vp" in tab "t".
// Return current portal if "vp" is number zero. Return NULL if not found.
Portal *
portFindByNr(Var* vp, Tab* t) {  // NULL for current tab
   Portal   *po;
   int      nr = (int)varGetNumberChk(vp, NULL);

   if (nr < 0)
      return NULL;
   if (nr == 0)
      return curPor;

   FOR_ALL_PORTALS_IN_TAB(t, po) {
      if (nr >= MIN_PORT_ID) {
         if (po->id == nr)
            return po;
      } ei (--nr <= 0)
         break;
   }
   if (nr >= MIN_PORT_ID) {
      // check tab-local popup portals
      for (po = (t == NULL ? curtab : t)->firstPopupPort; po; po = po->next)
         if (po->id == nr)
            return po;
      // check global popup portals
      FOR_ALL_POPUPPORTS(po)
          if (po->id == nr)
         return po;
      return NULL;
   }
   return po;
}

// Find a portal: When using a Portal ID in any tab, when using a number in the current tab.
// Return NULL when not found.
Portal *
portFindByNrOrId(Var *vp) {
   int   nr = (int)varGetNumberChk(vp, NULL);

   if (nr >= MIN_PORT_ID)
      return getPortalById(tv_get_number(vp));
   return portFindByNr(vp, NULL);
}

// Find portal specified by "wvp" in tabpage "tvp". Return the tab in 'ptp'
Portal *
find_tabwin(
    Var   *wvp,   // VAR_UNKNOWN for current portal
    Var   *tvp,   // VAR_UNKNOWN for current tab
    Tab   **ptp)
{
   Portal   *po = NULL;
   Tab   *t = NULL;
   long   n;

   if (wvp->tag != VAR_UNKNOWN) {
      if (tvp->tag != VAR_UNKNOWN) {
         n = (long)tv_get_number(tvp);
         if (n >= 0)
            t = getTab(n);
      } else
         t = curtab;

      if (t) {
         po = portFindByNr(wvp, t);
         if (po == NULL && wvp->tag == VAR_NUMBER && wvp->number != -1)
            // A portal with the specified number is not found
            t = NULL;
      }
   } else {
      po = curPor;
      t = curtab;
   }

   if (ptp)
      *ptp = t;

   return po;
}

// Get the layout of the given tab for winlayout() and add it to "l".
private void
get_framelayout(Frame *fr, List *l, int outer) {
   if (!fr)
      return;
      
   List* fr_list;
   if (outer)
      // outermost call from f_winlayout()
      fr_list = l;
   else {
      fr_list = list_alloc();
      if (list_append_list(l, fr_list) == FAIL) {
         eeglFree(fr_list);
         return;
      }
   }

   if (fr->layout == FR_LEAF) {
      if (fr->port) {
          list_append_string(fr_list, (CS)"leaf", -1);
          list_append_number(fr_list, fr->port->id);
      }
   } else {
      list_append_string(fr_list,
           fr->layout == FR_ROW ?  (CS)"row" : (CS)"col", -1);

      List* portList = list_alloc();
      if (list_append_list(fr_list, portList) == FAIL) {
         eeglFree(portList);
         return;
      }

      Frame* child = fr->child;
      while (child) {
         get_framelayout(child, portList, FALSE);
         child = child->next;
      }
   }
}

// Common code for tabpagewinnr() and winnr().
private int
getPortalIdInTab(Tab *t, Var *argvar) {
   int      nr = 1;
   Portal   *po;
   Byte   *arg;

   Portal* twin = (t == curtab) ? curPor : t->curPor;
   if (argvar->tag != VAR_UNKNOWN) {
      int   invalid_arg = FALSE;

      arg = convertVarToStringSingleUse(argvar);
      if (!arg)
         nr = 0;      // type error; errmsg already given
      ei (STRCMP(arg, "$") == 0)
         twin = (t == curtab) ? lastPor : t->lastPor;
      ei (STRCMP(arg, "#") == 0) {
         twin = (t == curtab) ? prevPor : t->prevPor;
      } else {
         long   count;
         Byte   *endp;

         // Extract the portal count (if specified). e.g. winnr('3j')
         count = strtol((char *)arg, (char **)&endp, 10);
         if (count <= 0)
            count = 1;   // if count is not specified, default to 1
         if (endp && *endp != '\0') {
            if (STRCMP(endp, "j") == 0)
               twin = vertNeighbor(t, twin, FALSE, count);
            ei (STRCMP(endp, "k") == 0)
               twin = vertNeighbor(t, twin, TRUE, count);
            ei (STRCMP(endp, "h") == 0)
               twin = portHorizNeighbor(t, twin, TRUE, count);
            ei (STRCMP(endp, "l") == 0)
               twin = portHorizNeighbor(t, twin, FALSE, count);
            else
               invalid_arg = TRUE;
         } else
            invalid_arg = TRUE;
      }
      if (!twin)
         nr = 0;

      if (invalid_arg) {
         showErrFmtMsg(_(e_invalid_expression_str), arg);
         nr = 0;
      }
   }

   if (nr <= 0)
      return 0;

   for (po = (t == curtab) ? firstPor : t->firstPor; po != twin; po = po->next) {
      if (!po) {
         // didn't find it in this tabpage
         nr = 0;
         break;
      }
      ++nr;
   }
   return nr;
}

// Return information about a portal as a dictionary.
private Bag *
get_win_info(Portal *po, short tpnr, short winnr) {
   Bag* bag = allocBag();

   // make sure bottomLine is valid
   validate_botline_win(po);

   bagAddNumber(bag, S"tabnr", tpnr);
   bagAddNumber(bag, S"winnr", winnr);
   bagAddNumber(bag, S"winid", po->id);
   bagAddNumber(bag, S"height", po->height);
   bagAddNumber(bag, S"winrow", po->portalRow + 1);
   bagAddNumber(bag, S"topline", po->topLine);
   bagAddNumber(bag, S"botline", po->bottomLine - 1);
   bagAddNumber(bag, S"width", po->width);
   bagAddNumber(bag, S"wincol", po->portalCol + 1);
   bagAddNumber(bag, S"textoff", normalPortalColumnOffset(po));
   bagAddNumber(bag, S"bufnr", po->book->fiNum);
   bagAddNumber(bag, S"leftcol", po->leftCol);
   bagAddNumber(bag, S"terminal", bt_terminal(po->book));
   bagAddNumber(bag, S"quickfix", isLocationListBook(po->book));
   bagAddNumber(bag, S"loclist", (isLocationListBook(po->book) && po->locationStackRef));

   // Add a reference to portal variables
   bagAddBag(bag, S"variables", po->internalVars);

   return bag;
}

// Return information (variables, options, etc.) about a tab as a dictionary.
private Bag *
getTabInfo(Tab *t, int tp_idx) {
   Bag* bag = allocBag();

   bagAddNumber(bag, S"tabnr", tp_idx);

   List* l = list_alloc();
   Portal* po;
   FOR_ALL_PORTALS_IN_TAB(t, po)
      list_append_number(l, (Long)po->id);
   bagAddList(bag, S"portals", l);

   // Make a reference to tabpage variables
   bagAddBag(bag, S"variables", t->vars);

   return bag;
}

void
f_gettabinfo(Var *argvars, Var *returnVar) {
   Tab   *t, *tparg = NULL;
   int      tpnr = 0;

   allocReturnList(returnVar);

   if (argvars[0].tag != VAR_UNKNOWN) {
      // Information about one tab
      tparg = getTab((int)varGetNumberChk(argvars, NULL));
      if (!tparg)
         return;
   }

   // Get information about a specific tab or all tabs
   FOR_ALL_TABS(t) {
      tpnr++;
      if (tparg != NULL && t != tparg)
         continue;
      Bag* b = getTabInfo(t, tpnr);
      if (b)
         listAppendBag(returnVar->list, b);
      if (tparg)
         return;
    }
}

void
f_getwininfo(Var *argvars, Var *returnVar) {
   Tab   *t;
   Portal   *po = NULL, *wparg = NULL;
   Bag   *d;
   Short tabnr = 0, portNr;

   allocReturnList(returnVar);

   if (argvars[0].tag != VAR_UNKNOWN) {
      wparg = getPortalById(tv_get_number(&argvars[0]));
      if (wparg == NULL)
         return;
   }

   // Collect information about either all the portals across all the tab
   // pages or one particular portal.
   FOR_ALL_TABS(t) {
      tabnr++;
      portNr = 0;
      FOR_ALL_PORTALS_IN_TAB(t, po) {
         portNr++;
         if (wparg && po != wparg)
            continue;
         d = get_win_info(po, tabnr, portNr);
         if (d)
            listAppendBag(returnVar->list, d);
         if (wparg)
            // found information about a specific portal
            return;
      }
   }
   if (wparg) {
      tabnr = 0;
      FOR_ALL_TABS(t) {
         tabnr++;
         FOR_ALL_POPUPPORTS_IN_TAB(t, po) {
            if (po == wparg)
               break;
         } 
      }
      d = get_win_info(wparg, t == NULL ? 0 : tabnr, 0);
      if (d)
         listAppendBag(returnVar->list, d);
   }
}

// "getwinpos({timeout})" function
void
f_getwinpos(Var *argvars UNUSED, Var *returnVar) {
   int x = -1;
   int y = -1;

   allocReturnList(returnVar);

   Long timeout = 100;

   if (argvars[0].tag != VAR_UNKNOWN)
      timeout = tv_get_number(&argvars[0]);

   (void)uiGetPortPos(&x, &y, timeout);
   list_append_number(returnVar->list, (Long)x);
   list_append_number(returnVar->list, (Long)y);
}


void
f_getwinposx(Var *argvars UNUSED, Var *returnVar) {
   returnVar->number = -1;
   int       x, y;

   if (uiGetPortPos(&x, &y, 100) == OK)
       returnVar->number = x;
}

void
f_getwinposy(Var *argvars UNUSED, OUT Var *returnVar) {
   returnVar->number = -1;
   int       x, y;
   if (uiGetPortPos(&x, &y, 100) == OK)
      returnVar->number = y;
}

void
f_tabpagenr(Var *argvars UNUSED, Var *returnVar) {
   int      nr = 1;
   Byte   *arg;

   if (argvars[0].tag != VAR_UNKNOWN) {
      arg = convertVarToStringSingleUse(&argvars[0]);
      nr = 0;
      if (arg) {
         if (STRCMP(arg, "$") == 0)
            nr = indexOfTab(NULL) - 1;
         ei (STRCMP(arg, "#") == 0)
            nr = isTabValid(lastUsedTabG) ? indexOfTab(lastUsedTabG) : 0;
         else
            showErrFmtMsg(_(e_invalid_expression_str), arg);
      }
   } else
      nr = indexOfTab(curtab);
   returnVar->number = nr;
}

void
f_tabpagewinnr(Var *argvars UNUSED, Var *returnVar) {
   Tab* t = getTab((int)tv_get_number(&argvars[0]));
   returnVar->number = t ? getPortalIdInTab(t, &argvars[1]) : 0;
}

void
f_win_execute(Var *argvars, Var *returnVar) {
   int      id;
   Tab   *t;
   Portal   *po;
   SwitchPort   switchPort;

   // Return an empty string if something fails.
   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   id = (int)tv_get_number(argvars);
   po = getPortAndTab(id, &t);
   if (!po || !t)
      return;

   Pos   curpos = po->cursor;
   Byte   cwd[MAXPATHL];
   int   cwd_status = FAIL;

   // Getting and setting directory can be slow on some systems, only do
   // this when the current or target portal/tab have a local directory or 'acd' is set.
   if (curPor != po
          && (curPor->localDir
               || po->localDir
               || (curtab != t && (curtab->localdir || t->localdir))
             )
   )
      cwd_status = mch_dirname(cwd, MAXPATHL);

   if (portSwitchNoblock(&switchPort, po, t, TRUE) == OK) {
      check_cursor();
      execute_common(argvars, returnVar, 1);
   }
   portRestoreNoblock(&switchPort, TRUE);
   if (cwd_status == OK)
      mch_chdir((char *)cwd);

   // Update the status line if the cursor moved.
   if (portalIsValid(po) && !EQUAL_POS(curpos, po->cursor))
      po->statusLineNeedsRedraw = TRUE;

   // In case the command moved the cursor or changed the Visual area, check it is valid.
   check_cursor();
   if (VIsual_active)
      check_pos(curBook, &VIsual);
}

void
f_win_findbuf(Var *argvars, Var* returnVar) {
   allocReturnList(returnVar);
   
   int bufnr = tv_get_number(&argvars[0]);
   List* list = returnVar->list;
   Portal   *po;
   Tab   *t;
   FOR_ALL_TAB_PORTALS(t, po) {
      if (po->book->fiNum == bufnr)
         list_append_number(list, po->id);
   } 
}

void
f_win_getid(Var *argvars, Var *returnVar) {
   returnVar->number = getPortalId(argvars);
}

void
f_gotoPortalid(Var *argvars, Var *returnVar) {
   int id = tv_get_number(&argvars[0]);
   if (curPor->id == id) {
      // Nothing to do.
      returnVar->number = 1;
      return;
   }

   if (text_or_buf_locked())
      return;
   if (popup_is_popup(curPor) && curBook->term) {
      emsg(_(e_not_allowed_for_terminal_in_popup_portal));
      return;
   }
   Portal   *po;
   Tab   *t;
   FOR_ALL_TAB_PORTALS(t, po)
   if (po->id == id) {
       // When jumping to another book, stop Visual mode.
       if (VIsual_active && po->book != curBook) {
            end_visual_mode();
       } 
       goto_tab_port(t, po);
       returnVar->number = 1;
       return;
   }
}

void
f_win_id2tabwin(Var *argvars, Var *returnVar) {
   allocReturnList(returnVar);
   portIdToTabPort(argvars, returnVar->list);
}

void
f_win_id2win(Var *argvars, Var *returnVar) {
   returnVar->number = getPortById(argvars);
}

void
f_win_move_separator(Var *argvars, Var *returnVar) {
   int      offset;

   returnVar->number = FALSE;

   Portal* po = portFindByNrOrId(&argvars[0]);
   if (!po || portalValidPopup(po))
      return;
   if (!portalIsValid(po)) {
      emsg(_(e_cannot_resize_portal_in_another_tab));
      return;
   }

   offset = (int)tv_get_number(&argvars[1]);
   portDragVsepLine(po, offset);
   returnVar->number = TRUE;
}

void
f_win_move_statusline(Var *argvars, Var *returnVar) {
   returnVar->number = FALSE;

   Portal* po = portFindByNrOrId(&argvars[0]);
   if (!po || portalValidPopup(po))
      return;
   if (!portalIsValid(po)) {
      emsg(_(e_cannot_resize_portal_in_another_tab));
      return;
   }

   int offset = (int)tv_get_number(&argvars[1]);
   portDragStatusLine(po, offset);
   returnVar->number = TRUE;
}

void
f_win_screenpos(Var *argvars, Var *returnVar) {
   allocReturnList(returnVar);
   Portal* po = portFindByNrOrId(&argvars[0]);
   list_append_number(returnVar->list, po == NULL ? 0 : po->portalRow + 1);
   list_append_number(returnVar->list, po == NULL ? 0 : po->portalCol + 1);
}

void
f_splitPortalmove(Var *argvars, Var *returnVar) {
   Portal   *po, *targetPort;
   Portal* oldPort = curPor;
   int     flags = 0, size = 0;

   returnVar->number = -1;

   po = portFindByNrOrId(&argvars[0]);
   targetPort = portFindByNrOrId(&argvars[1]);

   if (!po || !targetPort || po == targetPort
       || !portalIsValid(po) || !portalIsValid(targetPort)
       || portalValidPopup(po) || portalValidPopup(targetPort)
   ) {
      emsg(_(e_invalid_portal_number));
      return;
   }

   if (argvars[2].tag != VAR_UNKNOWN) {
      DictItem  *di;

      if (check_for_nonnull_dict_arg(argvars, 2) == FAIL)
          return;

      Bag* b = argvars[2].bag;
      if (bagGetBool(b, tConst("vertical"), false))
          flags |= WSP_VERT;
      if ((di = bagFind(b, tConst("rightbelow"))) != NULL)
          flags |= tv_get_bool(&di->c) ? WSP_BELOW : WSP_ABOVE;
      size = (int)bagGetNumber(b, tConst("size"));
   }

   // Check if we're allowed to continue before we bother switching portals.
   if (text_or_buf_locked() || check_split_disallowed(po) == FAIL)
      return;

   if (curPor != targetPort)
      gotoPortal(targetPort);

   // Autocommands may have sent us elsewhere or closed "po" or "oldPort".
   if (curPor == targetPort && portalIsValid(po)) {
      if (splitPortalmove(po, size, flags) == OK)
          returnVar->number = 0;
   } else
      emsg(_(e_autocommands_caused_command_to_abort));

   if (oldPort != curPor && portalIsValid(oldPort))
      gotoPortal(oldPort);
}

// "win_gettype(nr)" function
void
f_win_gettype(Var *argvars, Var *returnVar) {
   Portal   *po = curPor;

   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   if (argvars[0].tag != VAR_UNKNOWN) {
      po = portFindByNrOrId(&argvars[0]);
      if (!po) {
         returnVar->string = copyStr(S"unknown");
         return;
      }
   }
   if (is_autoCommPort(po))
      returnVar->string = copyStr(S"autocmd");
   ei (po->isPreview)
      returnVar->string = copyStr(S"preview");
   ei (PORTAL_IS_POPUP(po))
      returnVar->string = copyStr(S"popup");
   ei (po == commPortPortG)
      returnVar->string = copyStr(S"command");
   ei (isLocationListBook(po->book))
      returnVar->string = copyStr(po->locationStackRef ? S"loclist" : S"quickfix");

}

void
f_getcmdwintype(Var *argvars UNUSED, Var *returnVar) {
   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;
   returnVar->string = alloc(2);

   returnVar->string[0] = commPortTypeG;
   returnVar->string[1] = ZERO;
}

// "winbufnr(nr)" function
void
f_winbufnr(Var *argvars, Var *returnVar) {
   Portal* po = portFindByNrOrId(&argvars[0]);
   returnVar->number = po ? po->book->fiNum : -1;
}

void
f_wincol(Var *argvars UNUSED, Var *returnVar) {
   validate_cursor();
   returnVar->number = curPor->cursorCol + 1;
}

// "winheight(nr)" function
void
f_winheight(Var *argvars, Var *returnVar) {
   Portal* po = portFindByNrOrId(&argvars[0]);
   returnVar->number = po ? (Long)po->height : -1;
}

void
f_winlayout(Var *argvars, Var *returnVar) {
   allocReturnList(returnVar);

   Tab   *t;
   if (argvars[0].tag == VAR_UNKNOWN)
      t = curtab;
   else {
      t = getTab((int)tv_get_number(&argvars[0]));
      if (!t)
         return;
   }
   get_framelayout(t->topframe, returnVar->list, TRUE);
}

void
f_winline(Var *argvars UNUSED, Var *returnVar) {
   validate_cursor();
   returnVar->number = curPor->cursorRow + 1;
}

void
f_winnr(Var *argvars UNUSED, Var *returnVar) {
   int nr = getPortalIdInTab(curtab, &argvars[0]);
   returnVar->number = nr;
}

void
f_winrestcmd(Var *argvars UNUSED, Var *returnVar) {
   Portal   *po;
   Byte   buf[50];

   ArrayList   ga;
   ga_init2(&ga, sizeof(char), 70);

   // Do this twice to handle some portal layouts properly.
   for (int i = 0; i < 2; ++i) {
      int portId = 1;
      FOR_ALL_PORTALS(po) {
         sprintf((char *)buf, ":%dresize %d|", portId, po->height);
         ga_concat(&ga, buf);
         sprintf((char *)buf, "vert :%dresize %d|", portId, po->width);
         ga_concat(&ga, buf);
         ++portId;
      }
   }
   ga_append(&ga, ZERO);

   returnVar->string = ga.c;
   returnVar->tag = VAR_STRING;
}

void
f_winrestview(Var *argvars, Var *returnVar UNUSED) {
   if (check_for_nonnull_dict_arg(argvars, 0) == FAIL)
      return;

   Bag* dict = argvars[0].bag;
   if (bagHasKey(dict, tConst("lnum")))
      curPor->cursor.lnum = (LineNr)bagGetNumber(dict, tConst("lnum"));
   if (bagHasKey(dict, tConst("col")))
      curPor->cursor.col = (ColNr)bagGetNumber(dict, tConst("col"));
   if (bagHasKey(dict, tConst("coladd")))
      curPor->cursor.coladd = (ColNr)bagGetNumber(dict, tConst("coladd"));
   if (bagHasKey(dict, tConst("curswant"))) {
      curPor->cursWant = (ColNr)bagGetNumber(dict, tConst("curswant"));
      curPor->setCursWant = false;
   }

   if (bagHasKey(dict, tConst("topline")))
      set_topline(curPor, (LineNr)bagGetNumber(dict, tConst("topline")));
   if (bagHasKey(dict, tConst("topfill")))
      curPor->topFill = (int)bagGetNumber(dict, tConst("topfill"));
   if (bagHasKey(dict, tConst("leftcol")))
      curPor->leftCol = (ColNr)bagGetNumber(dict, tConst("leftcol"));
   if (bagHasKey(dict, tConst("skipcol")))
      curPor->skipCol = (ColNr)bagGetNumber(dict, tConst("skipcol"));

   check_cursor();
   portalNewHeight(curPor, curPor->height);
   portalNewWidth(curPor, curPor->width);
   didChangePortalSettingCurPor();

   if (curPor->topLine <= 0)
      curPor->topLine = 1;
   if (curPor->topLine > curBook->mem.lineCount)
      curPor->topLine = curBook->mem.lineCount;
   check_topfill(curPor, TRUE);
}

void
f_winsaveview(Var *argvars UNUSED, Var *returnVar) {
   allocReturnDict(returnVar);
      
   Bag* dict = returnVar->bag;
   bagAddNumber(dict, S"lnum", (long)curPor->cursor.lnum);
   bagAddNumber(dict, S"col", (long)curPor->cursor.col);
   bagAddNumber(dict, S"coladd", (long)curPor->cursor.coladd);
   update_curswant();
   bagAddNumber(dict, S"curswant", (long)curPor->cursWant);

   bagAddNumber(dict, S"topline", (long)curPor->topLine);
   bagAddNumber(dict, S"topfill", (long)curPor->topFill);
   bagAddNumber(dict, S"leftcol", (long)curPor->leftCol);
   bagAddNumber(dict, S"skipcol", (long)curPor->skipCol);
}

// "winwidth(nr)" function
void
f_winwidth(Var *argvars, Var *returnVar) {
   Portal* po = portFindByNrOrId(&argvars[0]);
   returnVar->number = po ? (Long)po->width : -1;
}

// Set "port" to be the curPor and "t" to be the current tab. portRestore() MUST be called to 
// undo, also when FAIL is returned. No autocommands will be executed until portRestore() is 
// called. When "no_display" is TRUE the display won't be affected, no redraw is
// triggered, another tabpage access is limited. Returns FAIL if switching to "port" failed.
int
portSwitch(SwitchPort *switchPort, Portal* port, Tab* t, int no_display) {
   block_autocmds();
   return portSwitchNoblock(switchPort, port, t, no_display);
}

// As portSwitch() but without blocking autocommands.
int
portSwitchNoblock(
   SwitchPort *switchPort,
   Portal       *port,
   Tab   *t,
   int       no_display)
{
   CLEAR_POINTER(switchPort);
   switchPort->curPor = curPor;
   if (port == curPor)
      switchPort->samePortal = TRUE;
   else {
      // Disable Visual selection, because redrawing may fail.
      switchPort->isVisualActive = VIsual_active;
      VIsual_active = FALSE;
   }

   if (t) {
      switchPort->currTab = curtab;
      if (no_display) {
         unloadTab(curtab);
         loadTab(t);
      } else
         gotoTab(t, FALSE, FALSE);
   }
   if (!portalIsValid(port))
      return FAIL;
   curPor = port;
   curBook = curPor->book;
   return OK;
}

// Restore current tabpage and portal saved by portSwitch(), if still valid.
// When "no_display" is TRUE the display won't be affected, no redraw is triggered.
void
portRestore(SwitchPort* switchPort, int no_display) {
   portRestoreNoblock(switchPort, no_display);
   unblock_autocmds();
}

// As portRestore() but without unblocking autocommands.
void
portRestoreNoblock(
   SwitchPort* switchPort,
   int       no_display)
{
   if (switchPort->currTab && isTabValid(switchPort->currTab)) {
      if (no_display) {
         Portal   *old_tp_curPor = curtab->curPor;
         unloadTab(curtab);
         // Don't change the curPor of the tabpage we temporarily visited.
         curtab->curPor = old_tp_curPor;
         loadTab(switchPort->currTab);
      } else
         gotoTab(switchPort->currTab, FALSE, FALSE);
   }

   if (!switchPort->samePortal)
      VIsual_active = switchPort->isVisualActive;

   if (portalIsValid(switchPort->curPor)) {
      curPor = switchPort->curPor;
      curBook = curPor->book;
   } ei (PORTAL_IS_POPUP(curPor))
      // original portal was closed and now we're in a popup portal: Go to the first valid portal
      gotoPortal(firstPor);
}

//}}}
//{{{popup portals. See :help popup

// Tab that was used to fill popup_mask.
private Tab* popupMaskTabS INIT(= NULL);

#define NOTIFICATION_TIMEOUT 3000 // milliseconds for notification kind of popups


// Values for w_popup_flags.
#define POPF_IS_POPUP    0x01   // this is a popup window
#define POPF_HIDDEN      0x02   // popup is not displayed
#define POPF_HIDDEN_FORCE 0x04  // popup is explicitly set to not be displayed
#define POPF_CURSORLINE  0x08   // popup is highlighting at the cursorline
#define POPF_ON_CMDLINE  0x10   // popup overlaps command line
#define POPF_DRAG        0x20   // popup can be moved by dragging border
#define POPF_DRAGALL     0x40   // popup can be moved by dragging everywhere
#define POPF_RESIZE      0x80   // popup can be resized by dragging
#define POPF_MAPPING    0x100   // mapping keys
#define POPF_INFO       0x200   // used for info of popup menu
#define POPF_INFO_MENU  0x400   // align info popup with popup menu
#define POPF_POSINVERT  0x800   // vertical position can be inverted

typedef struct {
   char   *pp_name;
   PopupPosition   pp_val;
} PopposEntry;

private PopposEntry popposEntriesS[] = {
   {"botleft", POPPOS_BOTLEFT},
   {"topleft", POPPOS_TOPLEFT},
   {"botright", POPPOS_BOTRIGHT},
   {"topright", POPPOS_TOPRIGHT},
   {"center", POPPOS_CENTER}
};

private int const defaultBorderChars[] = { 9472, 9474, 9472, 9474, 9484, 9488, 9496, 9492 };

// Portal used for ":echowindow"
private Portal *messagePort = NULL;

// Time used for the next ":echowindow" message in msec.
private int  message_win_time = 3000;

// Flag set when a message is added to the message portal, timer is started
// when the message portal is drawn.  This might be after pressing Enter at the hit-enter prompt.
private int    start_message_win_timer = FALSE;

private void mayStartMessagePortalTimer(Portal *po);

private int popup_on_cmdline = FALSE;

private void adjustPosition(Portal *po);

//Get option value for "key", which is "line" or "col". Handle "cursor+N" and "cursor-N".
//Return MAXCOL if the entry is not present.
private int
popup_options_one(Bag *dict, CS key) {
   Byte   *s;
   Byte   *endp;
   int      n = 0;

   DictItem* di = bagFind(dict, mbText(key));
   if (!di)
      return MAXCOL;

   CS val = tv_get_string(&di->c);
   if (STRNCMP(val, "cursor", 6) != 0)
      return bagGetNumber_check(dict, mbText(key));

   setcursor_mayforce(TRUE);
   s = val + 6;
   if (*s != ZERO) {
      endp = s;
      if (*skipwhite(s) == '+' || *skipwhite(s) == '-')
          n = strtol((char *)s, (char **)&endp, 10);
      if (endp != NULL && *skipwhite(endp) != ZERO) {
          showErrFmtMsg(_(e_invalid_expression_str), val);
          return 0;
      }
   }

   if (STRCMP(key, "line") == 0)
      n = screen_screenrow() + 1 + n;
   else // "col"
      n = screen_screencol() + 1 + n;

   // Zero means "not set", use -1 instead.
   if (n == 0)
      n = -1;
   return n;
}

private int 
set_padding_border(Bag *dict, int *array, CS name, int max_val) {
   DictItem* di = bagFind(dict, mbText(name));
   if (!di)
      return OK;

   if (di->c.tag != VAR_LIST) {
      emsg(_(e_list_required));
      return FAIL;
   }

   List   *list = di->c.list;
   if (!list)
      return OK;

   for (int i = 0; i < 4; ++i)
      array[i] = 1;

   CHECK_LIST_MATERIALIZE(list);
   ListItem* li;
   int i = 0;
   for (li = list->first; i < 4 && i < list->len; ++i, li = li->next) {
      int nr = (int)tv_get_number(&li->c);
      if (nr >= 0)
         array[i] = nr > max_val ? max_val : nr;
   }

   return OK;
}

// Used when popup options contain "moved": set default moved values.
private void
set_moved_values(Portal *po) {
   po->pup.curPor = curPor;
   po->pup.lnum = curPor->cursor.lnum;
   po->pup.minCol = curPor->cursor.col;
   po->pup.maxCol = curPor->cursor.col;
}

// Used when popup options contain "moved" with "word" or "WORD"
private void
set_moved_columns(Portal *po, int flags) {
   Byte   *ptr;
   int len = find_ident_under_cursor(&ptr, flags | FIND_NOERROR);

   if (len <= 0)
      return;

   po->pup.minCol = (int)(ptr - ml_get_curline());
   po->pup.maxCol = po->pup.minCol + len - 1;
}

//Used when popup options contain "mousemoved": set default moved values.
private void
set_mousemoved_values(Portal *po) {
   po->pup.mouseRow = mouseRowG;
   po->pup.mouseMinCol = mouseColG;
   po->pup.mouseMaxCol = mouseColG;
}

private void
update_popup_uses_mouse_move(void) {
   popup_uses_mouse_move = FALSE;
   if (!popup_visible)
      return;

   Portal *po;
   FOR_ALL_POPUPPORTS(po) {
      if (po->pup.mouseRow != 0) {
         popup_uses_mouse_move = TRUE;
         return;
      }
   } 
   FOR_ALL_POPUPPORTS_IN_TAB(curtab, po) {
      if (po->pup.mouseRow != 0) {
         popup_uses_mouse_move = TRUE;
         return;
      }
   } 
}

//Used when popup options contain "moved" with "word" or "WORD".
private void
set_mousemoved_columns(Portal *po, int flags) {
   Portal   *textwp;
   Byte   *text;
   int      col;
   Pos   pos;
   ColNr   mcol;

   
   if (find_word_under_cursor(mouseRowG, mouseColG, TRUE, flags,
              &textwp, &pos.lnum, &text, NULL, &col) != OK)
      return;

   // convert text column to mouse column
   pos.col = col;
   pos.coladd = 0;
   getvcol(textwp, &pos, &mcol, NULL, NULL);
   po->pup.mouseMinCol = mcol;

   pos.col = col + (ColNr)STRLEN(text) - 1;
   getvcol(textwp, &pos, NULL, NULL, &mcol);
   po->pup.mouseMaxCol = mcol;
   eeglFree(text);
}

// TRUE if "row"/"col" is on the border of the popup. The values are relative to the top-left corner
int
popup_on_border(Portal *po, int row, int col) {
   return (row == 0 && po->pup.border[0] > 0)
       || (row == popup_height(po) - 1 && po->pup.border[2] > 0)
       || (col == 0 && po->pup.border[3] > 0)
       || (col == popup_width(po) - 1 && po->pup.border[1] > 0);
}

//Return TRUE and close the popup if "row"/"col" is on the "X" button of the popup and pup.close is
//POPCLOSE_BUTTON. The values are relative to the top-left corner. Caller should check the left 
//mouse button was clicked. Return TRUE if the popup was closed.
int
popup_close_if_on_X(Portal *po, int row, int col) {
   if (po->pup.close == POPCLOSE_BUTTON && row == 0 && col == popup_width(po) - 1) {
      popup_close_for_mouse_click(po);
      return TRUE;
   }
   return FALSE;
}

// Values set when dragging a popup portal starts.
private int drag_start_row;
private int drag_start_col;
private int drag_start_wantline;
private int drag_start_wantcol;
private int drag_on_resize_handle;

// Mouse down on border of popup portal: start dragging it. Uses mouseCol and mouseRow.
void
popup_start_drag(Portal *po, int row, int col) {
   drag_start_row = mouseRowG;
   drag_start_col = mouseColG;
   if (po->pup.wantLine <= 0)
      drag_start_wantline = po->portalRow + 1;
   else
      drag_start_wantline = po->pup.wantLine;
   if (po->pup.wantCol == 0)
      drag_start_wantcol = po->portalCol + 1;
   else
      drag_start_wantcol = po->pup.wantCol;

   // Stop centering the popup
   if (po->pup.pos == POPPOS_CENTER)
      po->pup.pos = POPPOS_TOPLEFT;

    drag_on_resize_handle = po->pup.border[1] > 0
             && po->pup.border[2] > 0
             && row == popup_height(po) - 1
             && col == popup_width(po) - 1;

   if (po->pup.pos != POPPOS_TOPLEFT && drag_on_resize_handle) {
      if (po->pup.pos == POPPOS_TOPRIGHT || po->pup.pos == POPPOS_BOTRIGHT)
         po->pup.wantCol = po->portalCol + 1;
      if (po->pup.pos == POPPOS_BOTLEFT)
         po->pup.wantLine = po->portalRow + 1;
      po->pup.pos = POPPOS_TOPLEFT;
   }
}

// Mouse moved while dragging a popup portal: adjust the portal popup position or resize.
void
popup_drag(Portal *po) {
   // The popup may be closed before dragging stops.
   if (!portalValidPopup(po))
      return;

   if ((po->pup.flags & POPF_RESIZE) && drag_on_resize_handle) {
      int width_inc = mouseColG - drag_start_col;
      int height_inc = mouseRowG - drag_start_row;

      if (width_inc != 0) {
         int width = po->width + width_inc;

         if (width < 1)
            width = 1;
         po->pup.minWidth = width;
         po->pup.maxWidth = width;
         drag_start_col = mouseColG;
      }

      if (height_inc != 0) {
         int height = po->height + height_inc;

         if (height < 1)
            height = 1;
         po->pup.minHeight = height;
         po->pup.maxHeight = height;
         drag_start_row = mouseRowG;
      }

      adjustPosition(po);
      return;
   }

   if (!(po->pup.flags & (POPF_DRAG | POPF_DRAGALL)))
      return;
   po->pup.wantLine = drag_start_wantline + (mouseRowG - drag_start_row);
   if (po->pup.wantLine < 1)
      po->pup.wantLine = 1;
   if (po->pup.wantLine > visibleRowsG)
      po->pup.wantLine = visibleRowsG;
   po->pup.wantCol = drag_start_wantcol + (mouseColG - drag_start_col);
   if (po->pup.wantCol < 1)
      po->pup.wantCol = 1;
   if (po->pup.wantCol > visibleColsG)
      po->pup.wantCol = visibleColsG;

   adjustPosition(po);
}

// Set pup.firstLine to match the current "po->topLine".
void
popup_set_firstline(Portal *po) {
   Unt height = po->height;

   po->pup.firstLine = po->topLine;
   adjustPosition(po);

   // we don't want the popup to get smaller, decrement the first line until it doesn't
   while (po->pup.firstLine > 1 && po->height < height) {
      --po->pup.firstLine;
      adjustPosition(po);
   }
}

// TRUE if the position is in the popup portal scrollbar.
int
popup_is_in_scrollbar(Portal *po, int row, int col) {
   return po->pup.hasScrollbar
      && row >= po->pup.border[0]
      && row < popup_height(po) - po->pup.border[2]
      && col == popup_width(po) - po->pup.border[1] - 1;
}


// Handle a click in a popup portal, if it is in the scrollbar.
void
popup_handle_scrollbar_click(Portal *po, int row, int col) {
   if (!popup_is_in_scrollbar(po, row, col))
      return;

   int height = popup_height(po);
   int netopLine = po->topLine;

   if (row >= height / 2) {
      // Click in lower half, scroll down.
      if (po->topLine < po->book->mem.lineCount)
         ++netopLine;
   } ei (po->topLine > 1)
      // click on upper half, scroll up.
      --netopLine;

   if (netopLine == po->topLine)
      return;

   set_topline(po, netopLine);
   if (po == curPor) {
      if (po->cursor.lnum < po->topLine) {
         po->cursor.lnum = po->topLine;
         check_cursor();
      } ei (po->cursor.lnum >= po->bottomLine) {
         po->cursor.lnum = po->bottomLine - 1;
         check_cursor();
      }
   }
   popup_set_firstline(po);
   redrawPortLater(po, UPD_NOT_VALID);
}

// Add a timer to "po" with "time" (milliseconds).
// If "close" is true use popup_close(), otherwise popup_hide().
private void
addTimeout(Portal *po, int time, int close) {
   Byte cbbuf[50];

   eeSnprintf(cbbuf, sizeof(cbbuf),
      close ? "(_) => popup_close(%d)" : "(_) => popup_hide(%d)",
      po->id
   );
   
   po->pup.timer = create_timer(time, 0);
   Var tv;
   Callback cb = get_callback(&tv);
   if (cb.name != NULL && !cb.needsFreeing) {
      cb.name = copyStr(cb.name);
      cb.needsFreeing = TRUE;
   }
   po->pup.timer->callback = cb;
   clearVar(&tv);
}

private PopupPosition
get_pos_entry(Bag *d, int give_error) {
   Byte  *str = bagGetString(d, tConst("pos"), FALSE);

   if (!str)
      return POPPOS_NONE;

   for (int nr = 0; nr < (int)ARRAY_LENGTH(popposEntriesS); ++nr) {
      if (STRCMP(str, popposEntriesS[nr].pp_name) == 0)
         return popposEntriesS[nr].pp_val;
   } 

   if (give_error)
      showErrFmtMsg(_(e_invalid_argument_str), str);
   return POPPOS_NONE;
}

// Shared between createPopup() and f_popup_move().
private void
applyMoveParams(Portal *po, Bag* params) {
   int      nr;
   Boole boolVal;
   DictItem   *di;

   if ((nr = bagGetNumber_def(params, tConst("minwidth"), -1)) >= 0)
      po->pup.minWidth = nr;
   if ((nr = bagGetNumber_def(params, tConst("minheight"), -1)) >= 0)
      po->pup.minHeight = nr;
   if ((nr = bagGetNumber_def(params, tConst("maxwidth"), -1)) >= 0)
      po->pup.maxWidth = nr;
   if ((nr = bagGetNumber_def(params, tConst("maxheight"), -1)) >= 0)
      po->pup.maxHeight = nr;

   nr = popup_options_one(params, (CS)"line");
   if (nr != MAXCOL)
      po->pup.wantLine = nr;
   nr = popup_options_one(params, (CS)"col");
   if (nr != MAXCOL)
      po->pup.wantCol = nr;

   boolVal = bagGetBool(params, tConst("fixed"), false);
   po->pup.fixed = boolVal;
      
   PopupPosition ppt = get_pos_entry(params, TRUE);
   if (ppt != POPPOS_NONE)
      po->pup.pos = ppt;

   Byte* str = bagGetString(params, tConst("textprop"), FALSE);
   if (str) {
      po->pup.propType = 0;
      if (*str != ZERO) {
         po->pup.propPort = curPor;
         di = bagFind(params, tConst("textpropwin"));
         if (di) {
            po->pup.propPort = portFindByNrOrId(&di->c);
            if (!doesPortalExistInAnyTab(po->pup.propPort))
               po->pup.propPort = curPor;
         }
         Text t = text(str);
         nr = findPropTypeIdByName(t, po->pup.propPort->book);
         if (nr <= 0)
            nr = findPropTypeIdByName(t, NULL);
         if (nr <= 0)
            showErrFmtMsg(_(e_invalid_argument_str), str);
         else
            po->pup.propType = nr;
      }
   }

   di = bagFind(params, tConst("textpropid"));
   if (di)
      po->pup.propId = bagGetNumber(params, tConst("textpropid"));
}

// Handle "moved" and "mousemoved" arguments.
private void
handle_moved_argument(Portal *po, DictItem *di, int mousemoved) {
   if (di->c.tag == VAR_STRING && di->c.string != NULL) {
      Byte  *s = di->c.string;
      int   flags = 0;

      if (STRCMP(s, "word") == 0)
          flags = FIND_IDENT | FIND_STRING;
      ei (STRCMP(s, "WORD") == 0)
          flags = FIND_STRING;
      ei (STRCMP(s, "expr") == 0)
          flags = FIND_IDENT | FIND_STRING | FIND_EVAL;
      ei (STRCMP(s, "any") != 0)
          showErrFmtMsg(_(e_invalid_argument_str), s);
      if (flags != 0) {
         if (mousemoved)
            set_mousemoved_columns(po, flags);
         else
            set_moved_columns(po, flags);
      }
   } ei (di->c.tag == VAR_LIST
       && di->c.list
       && (di->c.list->len == 2 || di->c.list->len == 3)
   ) {
      List       *l = di->c.list;
      ListItem  *li;
      int       mincol;
      int       maxcol;

      CHECK_LIST_MATERIALIZE(l);
      li = l->first;
      if (l->len == 3) {
         Long nr = tv_get_number(&l->first->c);

         // Three numbers, might be from popup_getoptions().
         if (mousemoved)
            po->pup.mouseRow = nr;
         else
            po->pup.lnum = nr;
         li = li->next;
         if (nr == 0)
            po->pup.curPor = NULL;
      }

      mincol = tv_get_number(&li->c);
      maxcol = tv_get_number(&li->next->c);
      if (mousemoved) {
         po->pup.mouseMinCol = mincol;
         po->pup.mouseMaxCol = maxcol;
      } else {
         po->pup.minCol = mincol;
         po->pup.maxCol = maxcol;
      }
   } else
      showErrFmtMsg(_(e_invalid_argument_str), tv_get_string(&di->c));
}

private void
check_highlight(Bag *dict, CS name, Byte **pval) {
   DictItem* di = bagFind(dict, mbText(name));
   if (!di)
      return;

   if (di->c.tag != VAR_STRING)
      showErrFmtMsg(_(e_invalid_value_for_argument_str), name);
   else {
      Byte* str = tv_get_string(&di->c);
      if (*str != ZERO)
         *pval = copyStr(str);
   }
}

// Scroll to show the line with the cursor.
private void
scrollToCurrent(Portal *po) {
   if (po->cursor.lnum < po->topLine)
      po->topLine = po->cursor.lnum;
   ei (po->cursor.lnum >= po->bottomLine && (po->cacheState & VALID_BOTLINE)) {
      po->topLine = po->cursor.lnum - po->height + 1;
      if (po->topLine < 1)
          po->topLine = 1;
      ei (po->topLine > po->book->mem.lineCount)
          po->topLine = po->book->mem.lineCount;
      while (po->topLine < po->cursor.lnum
            && po->topLine < po->book->mem.lineCount
            && plines_m_win(po, po->topLine, po->cursor.lnum, po->height + 1) > (int)po->height)
         ++po->topLine;
   }

   // Don't let "firstline" cause a scroll.
   if (po->pup.firstLine > 0)
      po->pup.firstLine = po->topLine;
}

//Get the sign group name for portal "po". Return a pointer to a static buffer, overwritten on the 
//next call.
private CS
popup_get_sign_name(Portal *po) {
   static Byte buf[30];

   eeSnprintf(buf, sizeof(buf), "popup-%d", po->id);
   return buf;
}

//Highlight the line with the cursor. Also scroll the text to put the cursor line in view.
private void
highlightCurrentLine(Portal *po) {
   int       sign_id = 0;
   Byte  *sign_name = popup_get_sign_name(po);

   markDeleteSigns(po->book, (CS)"PopUpMenu");

   if ((po->pup.flags & POPF_CURSORLINE) != 0) {
      scrollToCurrent(po);

      if (!sign_exists_by_name(sign_name)) {
         char *linehl = "PopupSelected";
         sign_define_by_name( sign_name, NULL, (CS)linehl, NULL, NULL, NULL, SIGN_DEF_PRIO);
      }

      sign_place(
         &sign_id, (CS)"PopUpMenu", sign_name, po->book, po->cursor.lnum, SIGN_DEF_PRIO
      );
      redrawPortLater(po, UPD_NOT_VALID);
   } else
      sign_undefine_by_name(sign_name, false);
   po->pup.lastCurline = po->cursor.lnum;
}

// Shared between createPopup() and f_popup_setoptions().
private int
apply_general_options(Portal* po, Bag* dict) {

   // TODO: flip

   DictItem* di = bagFind(dict, tConst("firstline"));
   if (di) {
      po->pup.firstLine = bagGetNumber(dict, tConst("firstline"));
      if (po->pup.firstLine < 0)
         po->pup.firstLine = -1;
   }

   Boole nr = bagGetBool(dict, tConst("scrollbar"), false);
   po->pup.wantScrollbar = nr;

   CS str = bagGetString(dict, tConst("title"), FALSE);
   if (str) {
      eeglFree(po->pup.title);
      po->pup.title = copyStr(str);
   }

   Boole bl = bagGetBool(dict, tConst("wrap"), false);
   po->o.wrap = bl;

   bl = bagGetBool(dict, tConst("drag"), false);
   if (bl)
      po->pup.flags |= POPF_DRAG;
   else
      po->pup.flags &= ~POPF_DRAG;
      
   bl = bagGetBool(dict, tConst("dragall"), false);
   if (bl)
      po->pup.flags |= POPF_DRAGALL;
   else
      po->pup.flags &= ~POPF_DRAGALL;

   bl = bagGetBool(dict, tConst("posinvert"), false);
   if (bl)
      po->pup.flags |= POPF_POSINVERT;
   else
      po->pup.flags &= ~POPF_POSINVERT;

   bl = bagGetBool(dict, tConst("resize"), false);
   if (bl)
      po->pup.flags |= POPF_RESIZE;
   else
      po->pup.flags &= ~POPF_RESIZE;

   di = bagFind(dict, tConst("close"));
   if (di) {
      int ok = TRUE;

      if (di->c.tag == VAR_STRING && di->c.string != NULL) {
         Byte  *s = di->c.string;
         if (STRCMP(s, "none") == 0)
            po->pup.close = POPCLOSE_NONE;
         ei (STRCMP(s, "button") == 0)
            po->pup.close = POPCLOSE_BUTTON;
         ei (STRCMP(s, "click") == 0)
            po->pup.close = POPCLOSE_CLICK;
         else
            ok = FALSE;
      } else
         ok = FALSE;
      if (!ok)
         showErrFmtMsg(_(e_invalid_value_for_argument_str_str), "close", tv_get_string(&di->c));
   }

   str = bagGetString(dict, tConst("highlight"), FALSE);
   if (str) {
      optSetStringOptionDirectInPort(po, S"portcolor", str, OPT_LOCAL, 0);
      termUpdatePortcolor(po);
   }

   if (set_padding_border(dict, po->pup.padding, S"padding", 999) == FAIL 
         || set_padding_border(dict, po->pup.border, S"border", 1) == FAIL
   )
      return FAIL;

   di = bagFind(dict, tConst("borderhighlight"));
   if (di) {
      if (di->c.tag != VAR_LIST || di->c.list == NULL) {
          emsg(_(e_list_required));
          return FAIL;
      } else {
         List* list = di->c.list;
         ListItem   *li = list->first;

         CHECK_LIST_MATERIALIZE(list);
         for (int i = 0; i < 4 && i < list->len; ++i, li = li->next) {
            str = tv_get_string(&li->c);
            if (*str != ZERO) {
                eeglFree(po->pup.borderHilite[i]);
                po->pup.borderHilite[i] = copyStr(str);
            }
         }
         if (list->len == 1 && po->pup.borderHilite[0] != NULL) {
            for (int i = 1; i < 4; ++i) {
                eeglFree(po->pup.borderHilite[i]);
                po->pup.borderHilite[i] = copyStr(po->pup.borderHilite[0]);
            }
         } 
      }
   }

   di = bagFind(dict, tConst("borderchars"));
   if (di) {
      if (di->c.tag != VAR_LIST) {
         emsg(_(e_list_required));
         return FAIL;
      } else {
         List   *list = di->c.list;
         ListItem   *li;
         int      i;

         if (list) {
            CHECK_LIST_MATERIALIZE(list);
            for (i = 0, li = list->first; i < 8 && i < list->len; ++i, li = li->next) {
               str = tv_get_string(&li->c);
               if (*str != ZERO)
                  po->pup.borderChar[i] = mb_ptr2char(str);
            }
            if (list->len == 1)
               for (i = 1; i < 8; ++i)
                  po->pup.borderChar[i] = po->pup.borderChar[0];
            if (list->len == 2) {
               for (i = 4; i < 8; ++i)
                  po->pup.borderChar[i] = po->pup.borderChar[1];
               for (i = 1; i < 4; ++i)
                  po->pup.borderChar[i] = po->pup.borderChar[0];
            }
         }
      }
   } else {
      for (int i = 0; i < 8; ++i) {
         po->pup.borderChar[i] = defaultBorderChars[i];
      } 
   }

   check_highlight(dict, S"scrollbarhighlight", &po->pup.scrollbarHilite);
   check_highlight(dict, S"thumbhighlight", &po->pup.thumbHilite);

   di = bagFind(dict, tConst("zindex"));
   if (di) {
      po->pup.zIndex = bagGetNumber(dict, tConst("zindex"));
   if (po->pup.zIndex < 1)
      po->pup.zIndex = POPUPWIN_DEFAULT_ZINDEX;
   if (po->pup.zIndex > 32000)
      po->pup.zIndex = 32000;
   }

   di = bagFind(dict, tConst("mask"));
   if (di) {
      int ok = FALSE;
      if (di->c.tag == VAR_LIST && di->c.list != NULL) {
         ListItem *li;

         ok = TRUE;
         FOR_ALL_LIST_ITEMS(di->c.list, li) {
            if (li->c.tag != VAR_LIST || !li->c.list || li->c.list->len != 4) {
               ok = FALSE;
               break;
            } else
               CHECK_LIST_MATERIALIZE(li->c.list);
         }
      }
      if (ok) {
         po->pup.mask = di->c.list;
         ++po->pup.mask->refcount;
         EE_CLEAR(po->pup.maskCells);
      } else {
         showErrFmtMsg(_(e_invalid_value_for_argument_str), "mask");
         return FAIL;
      }
   }

   // Add timer to close the popup after some time.
   nr = bagGetNumber(dict, tConst("time"));
   if (nr > 0)
      addTimeout(po, nr, TRUE);

   di = bagFind(dict, tConst("moved"));
   if (di) {
      set_moved_values(po);
      handle_moved_argument(po, di, FALSE);
   }

   di = bagFind(dict, tConst("mousemoved"));
   if (di) {
      set_mousemoved_values(po);
      handle_moved_argument(po, di, TRUE);
   }

   bl = bagGetBool(dict, tConst("cursorline"), false);
   if (bl)
      po->pup.flags |= POPF_CURSORLINE;
   else
      po->pup.flags &= ~POPF_CURSORLINE;

   di = bagFind(dict, tConst("filter"));
   if (di) {
      Callback callback = get_callback(&di->c);

      if (callback.name != NULL) {
         evFreeCallback(&po->pup.filterCb);
         set_callback(&po->pup.filterCb, &callback);
         if (callback.needsFreeing)
            eeglFree(callback.name);
      }
   }
   bl = bagGetBool(dict, tConst("mapping"), false);
   if (bl)
      po->pup.flags |= POPF_MAPPING;
   else
      po->pup.flags &= ~POPF_MAPPING;

   str = bagGetString(dict, tConst("filtermode"), FALSE);
   if (str) {
      if (STRCMP(str, "a") == 0)
         po->pup.filterMode = MODE_ALL;
      else
         po->pup.filterMode = mode_str2flags(str);
   }

   di = bagFind(dict, tConst("callback"));
   if (!di)
      return OK;

   Callback   callback = get_callback(&di->c);
   if (!callback.name)
      return OK;

   evFreeCallback(&po->pup.closeCb);
   set_callback(&po->pup.closeCb, &callback);
   if (callback.needsFreeing)
      eeglFree(callback.name);

   return OK;
}

//Go through the params in "dict" and apply them to popup portal "po".
//"create" is TRUE when creating a new popup portal.
private int
applyParams(Portal *po, Bag* params, int create) {
   applyMoveParams(po, params);

   if (create) {
      optSetStringOptionDirectInPort(po, S"signcolumn", S"no", OPT_LOCAL, 0);
   } 

   if (apply_general_options(po, params) == FAIL)
      return FAIL;

   Boole bl = bagGetBool(params, tConst("hidden"), false);
   if (bl)
      po->pup.flags |= POPF_HIDDEN | POPF_HIDDEN_FORCE;

   // when "firstline" and "cursorline" are both set and the cursor would be
   // above or below the displayed lines, move the cursor to "firstline".
   if (po->pup.firstLine > 0 && (po->pup.flags & POPF_CURSORLINE)) {
      if (po->pup.firstLine > po->book->mem.lineCount)
          po->cursor.lnum = po->book->mem.lineCount;
      ei (po->cursor.lnum < po->pup.firstLine
         || po->cursor.lnum >= po->pup.firstLine + po->height)
          po->cursor.lnum = po->pup.firstLine;
      po->topLine = po->pup.firstLine;
      po->cacheState &= ~VALID_BOTLINE;
   }

   needRefreshPopupMaskG = TRUE;
   highlightCurrentLine(po);

   return OK;
}

// Add lines to the popup from a list of strings.
private void
add_popup_strings(Book* book, List *l) {
   ListItem  *li;
   LineNr    lnum = 0;
   Byte   *p;

   FOR_ALL_LIST_ITEMS(l, li) {
      if (li->c.tag == VAR_STRING) {
         p = li->c.string;
         memAppendBook(book, lnum++, p == NULL ? Em : p, (ColNr)0, TRUE);
      }
   }
}

// Add lines to the popup from a list of dictionaries.
private void
add_popup_dicts(Book* book, List *l) {
   ListItem  *li;
   ListItem  *pli;
   LineNr    lnum = 0;
   Byte   *p;
   Bag   *dict;

   // first add the text lines
   FOR_ALL_LIST_ITEMS(l, li) {
      if (li->c.tag != VAR_BAG) {
         showErrFmtMsg(_(e_argument_1_list_item_nr_dictionary_required), lnum + 1);
         return;
      }
      dict = li->c.bag;
      p = dict == NULL ? NULL : bagGetString(dict, tConst("text"), FALSE);
      memAppendBook(book, lnum++, p == NULL ? (CS)"" : p, (ColNr)0, TRUE);
   }

   // add the text properties
   lnum = 1;
   for (li = l->first; li != NULL; li = li->next, ++lnum) {
      dict = li->c.bag;
      DictItem* di = bagFind(dict, tConst("props"));
      if (!di) {
         continue;
      }
      if (di->c.tag != VAR_LIST) {
         emsg(_(e_list_required));
         return;
      }
      List* plist = di->c.list;
      if (plist) {
         FOR_ALL_LIST_ITEMS(plist, pli) {
            if (pli->c.tag != VAR_BAG) {
               emsg(_(e_dictionary_required));
               return;
            }
            dict = pli->c.bag;
            if (dict) {
               int col = bagGetNumber(dict, tConst("col"));
               prop_add_common(lnum, col, dict, book, NULL);
            }
         }
      }
   }
}

// Get the padding plus border at the top, adjusted to 1 if there is a title.
int
popup_top_extra(Portal *po) {
   int   extra = po->pup.border[0] + po->pup.padding[0];

   if (extra == 0 && po->pup.title != NULL && *po->pup.title != ZERO)
      return 1;
   return extra;
}

// Get the padding plus border at the left.
int
popup_left_extra(Portal *po) {
   return po->pup.border[3] + po->pup.padding[3];
}

// Return the height of popup portal "po", including border and padding.
int
popup_height(Portal *po) {
   return po->height + popup_top_extra(po) + po->pup.padding[2] + po->pup.border[2];
}

// Return the width of popup portal "po", including border, padding and scrollbar.
int
popup_width(Portal *po) {
   // leftCol is how many columns of the core are left of the screen
   // pup.rightOff is how many columns of the core are right of the screen
   return po->width + po->leftCol + popup_extra_width(po) + po->pup.rightOff;
}

// Return the extra width of popup portal "po": border, padding and scrollbar.
int
popup_extra_width(Portal *po) {
   return po->pup.padding[3] + po->pup.border[3]
       + po->pup.padding[1] + po->pup.border[1]
       + po->pup.hasScrollbar;
}

// Adjust the position and size of the popup to fit on the screen.
private void
adjustPosition(Portal *po) {
   LineNr   lnum;
   int      wrapped = 0;
   int      maxwidth;
   int      maxwidth_no_scrollbar;
   int      width_with_scrollbar = 0;
   int      used_maxwidth = FALSE;
   int      margin_width = 0;
   int      maxspace;
   int      center_vert = FALSE;
   int      center_hor = FALSE;
   int      allow_adjust_left = !po->pup.fixed;
   int      top_extra = popup_top_extra(po);
   int      right_extra = po->pup.border[1] + po->pup.padding[1];
   int      bot_extra = po->pup.border[2] + po->pup.padding[2];
   int      left_extra = po->pup.border[3] + po->pup.padding[3];
   int      extra_height = top_extra + bot_extra;
   int      extra_width = left_extra + right_extra;
   int      w_height_before_limit;
   int      org_winrow = po->portalRow;
   int      org_wincol = po->portalCol;
   int      org_width = po->width;
   int      org_height = po->height;
   int      org_leftcol = po->leftCol;
   int      org_leftoff = po->pup.leftOff;
   int      minwidth, minheight;
   int      maxheight = visibleRowsG;
   int      wantline = po->pup.wantLine;  // adjusted for textprop
   int      wantcol = po->pup.wantCol;    // adjusted for textprop
   int      use_wantcol = wantcol != 0;
   int      adjust_height_for_top_aligned = FALSE;

   po->portalRow = 0;
   po->portalCol = 0;
   po->leftCol = 0;
   po->pup.leftOff = 0;
   po->pup.rightOff = 0;

   // May need to update the "cursorline" highlighting, which may also change "topline"
   if (po->pup.lastCurline != po->cursor.lnum)
      highlightCurrentLine(po);
 
   if (po->pup.propType > 0 && portalIsValid(po->pup.propPort)) {
      Portal       *prop_win = po->pup.propPort;
      TextProp  prop;
      LineNr    prop_lnum;
      Pos       pos;
      int       screen_row;
      int       screen_scol;
      int       screen_ccol;
      int       screen_ecol;

      // Popup portal is positioned relative to a text property.
      if (find_visible_prop(prop_win,
               po->pup.propType, po->pup.propId,
               &prop, &prop_lnum) == FAIL)
      {
         // Text property is no longer visible, hide the popup.
         // Unhiding the popup is done in check_popup_unhidden().
         if ((po->pup.flags & POPF_HIDDEN) == 0) {
            po->pup.flags |= POPF_HIDDEN;
            if (portalIsValid(po->pup.propPort))
                redrawPortLater(po->pup.propPort, UPD_SOME_VALID);
         }
         return;
      }

      // Compute the desired position from the position of the text property. Use "wantline" and 
      // "wantcol" as offsets.
      pos.lnum = prop_lnum;
      pos.col = prop.col;
      if (po->pup.pos == POPPOS_TOPLEFT || po->pup.pos == POPPOS_BOTLEFT)
         pos.col += prop.len - 1;
      textpos2screenpos(prop_win, &pos, &screen_row, &screen_scol, &screen_ccol, &screen_ecol);

      if (screen_scol == 0) {
          // position is off screen, make the width zero to hide it.
          po->width = 0;
          return;
      }
      if (po->pup.pos == POPPOS_TOPLEFT || po->pup.pos == POPPOS_TOPRIGHT)
         // below the text
         wantline = screen_row + wantline + 1;
      else
         // above the text
         wantline = screen_row + wantline - 1;
      center_vert = FALSE;
      if (po->pup.pos == POPPOS_TOPLEFT || po->pup.pos == POPPOS_BOTLEFT)
         // right of the text
         wantcol = screen_ecol + wantcol;
      else
         // left of the text
         wantcol = screen_scol + wantcol - 2;
      use_wantcol = TRUE;
   } else {
      // If no line was specified default to vertical centering.
      if (wantline == 0)
         center_vert = TRUE;
      ei (wantline < 0)
         // If "wantline" is negative it actually means zero.
         wantline = 0;
      if (wantcol < 0)
         // If "wantcol" is negative it actually means zero.
         wantcol = 0;
   }

   if (po->pup.pos == POPPOS_CENTER) {
      // center after computing the size
      center_vert = TRUE;
      center_hor = TRUE;
   } else {
      if (wantline > 0 && (po->pup.pos == POPPOS_TOPLEFT || po->pup.pos == POPPOS_TOPRIGHT)) {
         po->portalRow = wantline - 1;
         if (po->portalRow >= visibleRowsG)
            po->portalRow = visibleRowsG - 1;
      }
      if (po->pup.pos == POPPOS_BOTTOM) {
          //Assume that each book line takes one screen line, and one line for the top border.
          //First make sure commlineRowG is valid, calling drawUpdateScreen() will set it only later.
          compute_cmdrow();
          po->portalRow = MAX(commlineRowG - po->book->mem.lineCount - 1, 0);
      }

      if (!use_wantcol)
         center_hor = TRUE;
      ei (wantcol > 0 && (po->pup.pos == POPPOS_TOPLEFT || po->pup.pos == POPPOS_BOTLEFT)) {
         po->portalCol = wantcol - 1;
         // Need to see at least one character after the decoration.
         if (po->portalCol > firstPor->portalCol + (int)topframeG->width - left_extra - 1)
            po->portalCol = firstPor->portalCol + topframeG->width - left_extra - 1;
      }
   }

   // When centering or right aligned, use maximum width. When left aligned use the space 
   // available, but shift to the left when we hit the right of the screen.
   maxspace = firstPor->portalCol + topframeG->width - po->portalCol - left_extra;
   maxwidth = maxspace;
   if (po->pup.maxWidth > 0 && maxwidth > po->pup.maxWidth) {
      allow_adjust_left = FALSE;
      maxwidth = po->pup.maxWidth;
   }

   margin_width = number_width(po) + 1; // for the line number column
   if (isSigncolumnOn(po))
      margin_width += 2;
   if (margin_width >= maxwidth)
      margin_width = maxwidth - 1;

   minwidth = po->pup.minWidth;
   minheight = po->pup.minHeight;
   // A terminal popup initially does not have content, use a default minimal width of 20 
   // characters and height of 5 lines.
   if (po->book->term != NULL) {
      if (minwidth == 0)
          minwidth = 20;
      if (minheight == 0)
          minheight = 5;
   }

   if (po->pup.maxHeight > 0)
      maxheight = po->pup.maxHeight;
   ei (po->pup.pos == POPPOS_BOTTOM)
      maxheight = commlineRowG - 1;

   //start at the desired first line
   if (po->pup.firstLine > 0)
      po->topLine = po->pup.firstLine;
   if (po->topLine < 1)
      po->topLine = 1;
   ei (po->topLine > po->book->mem.lineCount)
      po->topLine = po->book->mem.lineCount;

   //Compute width based on longest text line and the 'wrap' option. Use a minimum width of one, 
   //so that something shows when there is no text. When "firstline" is -1 then start with the 
   //last book line and go backwards. TODO: more accurate wrapping
   po->width = 1;
   if (po->pup.firstLine < 0)
      lnum = po->book->mem.lineCount;
   else
      lnum = po->topLine;
   while (lnum >= 1 && lnum <= po->book->mem.lineCount) {
      int len;
      int width = po->width;

      // Count Tabs for what they are worth and compute the length based on the maximum width 
      // (matters when 'showbreak' is set). "margin_width" is added to "len" where it matters.
      if ((int)po->width < maxwidth)
          po->width = maxwidth;
      len = linetabsize(po, lnum);
      po->width = width;

      if (len + margin_width > maxwidth
         && allow_adjust_left
         && (po->pup.pos == POPPOS_TOPLEFT || po->pup.pos == POPPOS_BOTLEFT)
      ){
         // adjust leftwise to fit text on screen
         int shift_by = len + margin_width - maxwidth;

         if (shift_by > po->portalCol) {
            int truncate_shift = shift_by - po->portalCol;

            shift_by -= truncate_shift;
         }

         po->portalCol -= shift_by;
         maxwidth += shift_by;
         po->width = maxwidth;
      }
      if (po->o.wrap) {
         while (len + margin_width > maxwidth) {
            ++wrapped;
            len -= maxwidth - margin_width;
            po->width = maxwidth;
            used_maxwidth = TRUE;
         }
      }
      if ((int)po->width < len + margin_width) {
         po->width = len + margin_width;
         if (po->pup.maxWidth > 0 && po->width > (Unt)po->pup.maxWidth)
            po->width = po->pup.maxWidth;
      }

      if (po->pup.firstLine < 0)
         --lnum;
      else
         ++lnum;

      // do not use the width of lines we're not going to show
      if (maxheight > 0
            && ((po->pup.firstLine >= 0 ? lnum - po->topLine : po->book->mem.lineCount - lnum) 
                  + wrapped) >= maxheight
      ) {
          break;
      } 
   }

   if (po->pup.firstLine < 0)
      po->topLine = lnum + 1;

   po->pup.hasScrollbar = po->pup.wantScrollbar
      && (po->topLine > 1 || lnum <= po->book->mem.lineCount);
   if (po->book->term != NULL && !term_is_finished(po->book))
   // Terminal portal with running job never has a scrollbar, adjusts to window height.
   po->pup.hasScrollbar = FALSE;
   maxwidth_no_scrollbar = maxwidth;
   if (po->pup.hasScrollbar) {
      ++right_extra;
      ++extra_width;
      // make space for the scrollbar if needed, when lines wrap and when applying minwidth
      if (maxwidth + right_extra >= maxspace
         && (used_maxwidth || (minwidth > 0 && (int)po->width < minwidth))
      ) {
          maxwidth -= po->pup.padding[1] + 1;
      } 
   }

   if (po->pup.title != NULL && *po->pup.title != ZERO) {
   int title_len = eeglStrSize(po->pup.title) + 2 - extra_width;

   if (minwidth < title_len)
      minwidth = title_len;
   }

   if (minwidth > 0 && (int)po->width < minwidth)
      po->width = minwidth;
   if (po->width > (Unt)maxwidth) {
      if (po->width > (Unt)maxspace && !po->o.wrap)
         // some columns cut off on the right
         po->pup.rightOff = po->width - maxspace;

      // If the portal doesn't fit because 'minwidth' is set then the
      // scrollbar is at the far right of the screen, use the size without the scrollbar.
      if (po->pup.hasScrollbar && po->pup.minWidth > 0) {
          int off = po->width - maxwidth;

          if (off > right_extra)
         extra_width -= right_extra;
          else
         extra_width -= off;
          po->width = maxwidth_no_scrollbar;
      } else {
          po->width = maxwidth;

          // when adding a scrollbar below need to adjust the width
          width_with_scrollbar = maxwidth_no_scrollbar - right_extra;
      }
   }
   if (center_hor) {
      po->portalCol = (firstPor->portalCol + topframeG->width - po->width - extra_width) / 2;
      if (po->portalCol < 0)
         po->portalCol = 0;
   } ei (po->pup.pos == POPPOS_BOTRIGHT || po->pup.pos == POPPOS_TOPRIGHT) {
      int leftOff = wantcol - (po->width + extra_width);

      //Right aligned: move to the right if needed. No truncation: that would change the height.
      if (leftOff >= 0)
          po->portalCol = leftOff;
      ei (po->pup.fixed) {
         //"col" specifies the right edge, but popup doesn't fit, skip some
         //columns when displaying the portal, minus left border and padding.
         if (-leftOff > left_extra)
            po->leftCol = -leftOff - left_extra;
         po->width -= po->leftCol;
         po->pup.leftOff = -leftOff;
         if (po->width == UNT)
            po->width = 0;
      }
   }

   if (po->o.wrap 
         || (!po->pup.fixed && (po->pup.pos == POPPOS_TOPLEFT || po->pup.pos == POPPOS_BOTLEFT))
   ) {
      int want_col = 0;

      // try to show the right border and any scrollbar
      want_col = left_extra + po->width + right_extra;
      if (want_col > 0 && po->portalCol > 0
             && po->portalCol + want_col >= (int)(firstPor->portalCol + topframeG->width)
      ){
         po->portalCol = firstPor->portalCol + topframeG->width - want_col;
         if (po->portalCol < 0)
            po->portalCol = 0;
      }
   }

   po->height = po->book->mem.lineCount - po->topLine
                         + 1 + wrapped;
   if (minheight > 0 && po->height < (Unt)minheight)
      po->height = minheight;
   if (maxheight > 0 && po->height > (Unt)maxheight)
      po->height = maxheight;
   w_height_before_limit = po->height;
   if (po->height > visibleRowsG - po->portalRow)
      po->height = visibleRowsG - po->portalRow;

   if (center_vert) {
      po->portalRow = (visibleRowsG - po->height - extra_height) / 2;
      if (po->portalRow < 0)
          po->portalRow = 0;
   } ei (po->pup.pos == POPPOS_BOTRIGHT || po->pup.pos == POPPOS_BOTLEFT) {
      if ((po->height + extra_height) <= (Unt)wantline)
          // bottom aligned: may move down
          po->portalRow = wantline - (po->height + extra_height);
      ei (wantline * 2 >= visibleRowsG || !(po->pup.flags & POPF_POSINVERT)) {
          // Bottom aligned but does not fit, and less space on the other
          // side or "posinvert" is off: reduce height.
          po->portalRow = 0;
          po->height = wantline - extra_height;
      } else {
          // Not enough space and more space on the other side: make top aligned.
          po->portalRow = (wantline < 0 ? 0 : wantline) + 1;
          adjust_height_for_top_aligned = TRUE;
      }
   } ei (po->pup.pos == POPPOS_TOPRIGHT || po->pup.pos == POPPOS_TOPLEFT) {
      if (po != popupDragPortG
         && wantline + (po->height + extra_height) - 1 > visibleRowsG
         && wantline * 2 > visibleRowsG
         && (po->pup.flags & POPF_POSINVERT)
      ){
         //top aligned and not enough space below but there is space above:
         //make bottom aligned and recompute the height
         po->height = w_height_before_limit;
         po->portalRow = wantline - 2 - po->height - extra_height;
         if (po->portalRow < 0) {
            po->height += po->portalRow;
            po->portalRow = 0;
         }
      } else {
         po->portalRow = wantline - 1;
         adjust_height_for_top_aligned = TRUE;
      }
   }

   if (adjust_height_for_top_aligned && po->pup.wantScrollbar
           && po->portalRow + po->height + extra_height > visibleRowsG
   ){
      // Bottom of the popup goes below the last line, reduce the height and add a scrollbar.
      po->height = visibleRowsG - po->portalRow - extra_height;
      if (po->book->term == NULL || term_is_finished(po->book)) {
          po->pup.hasScrollbar = TRUE;
          if (width_with_scrollbar > 0)
         po->width = width_with_scrollbar;
      }
   }

   // make sure portalRow is valid
   if (po->portalRow >= visibleRowsG)
      po->portalRow = visibleRowsG - 1;
   ei (po->portalRow < 0)
      po->portalRow = 0;

   if (po->portalCol + po->width > firstPor->portalCol + topframeG->width)
      po->portalCol = firstPor->portalCol + topframeG->width - po->width;
   ei (po->portalCol < firstPor->portalCol)
      po->portalCol = firstPor->portalCol;
   if (po->portalCol < 0)
      po->portalCol = 0;

   if ((int)po->height != org_height)
      portComputeScroll(po);

   po->pup.lastChangedTick = CHANGEDTICK(po->book);
   if (portalIsValid(po->pup.propPort)) {
      po->pup.propChangedTick = CHANGEDTICK(po->pup.propPort->book);
      po->pup.propTopline = po->pup.propPort->topLine;
   }

   //Need to update popupMaskG if the position or size changed.
   //And redraw portals and statuslines that were behind the popup.
   if (org_winrow != po->portalRow
       || org_wincol != po->portalCol
       || org_leftcol != po->leftCol
       || org_leftoff != po->pup.leftOff
       || org_width != (int)po->width
       || org_height != (int)po->height
   ){
      redrawPortLater(po, UPD_NOT_VALID);
      if (po->pup.flags & POPF_ON_CMDLINE)
         mustClearCommlineG = TRUE;
      needRefreshPopupMaskG = TRUE;
   }
}

// Return TRUE if "type" is POPUP_NOTIFICATION or POPUP_MESSAGE_WIN.
private int
isNotification(PopupKind kind) {
   return kind == POPUP_NOTIFICATION || kind == POPUP_MESSAGE_WIN;
}

//Make "book" empty and set the contents to "text". Used by createPopup() and popup_settext().
private inline void
setBookText(Book* book, Var text) {
   // Clear the book, then replace the lines.
   for (int lnum = book->mem.lineCount; lnum > 0; --lnum)
      ml_deleteBufLine(book, lnum);

   // Add text to the book.
   if (text.tag == VAR_STRING) {
      CS s = text.string ? text.string : Em;
      Unt len = STRLEN(s) + 1;

      // just a string
      memAppendBook(book, 0, s, len, FALSE);
   } else {
      List *l = text.list;

      if (l != NULL && l->len > 0) {
         if (l->first == &range_list_item)
            emsg(_(e_using_number_as_string));
         ei (l->first->c.tag == VAR_STRING)
            // list of strings
            add_popup_strings(book, l);
         else
            // list of dictionaries
            add_popup_dicts(book, l);
      }
   }

   // delete the line that was in the empty book
   ml_deleteBufLine(book, book->mem.lineCount);
}

//Parse the 'previewpopup' or 'completepopup' option and apply the values to
//portal "po" if it is not NULL. Return FAIL if the parsing fails.
private int
parse_popup_option(Portal *po, Boole is_preview) {
   if (is_preview)
      return FAIL;
   CS p = p_cpp;

   if (po)
      po->pup.flags &= ~POPF_INFO_MENU;

   for ( ; *p != ZERO; p += (*p == ',' ? 1 : 0)) {
      Byte* s = p;
      int x;

      Byte* e = firstOccurrence(p, ':');
      if (!e || e[1] == ZERO)
         return FAIL;

      p = firstOccurrence(e, ',');
      if (!p)
         p = e + STRLEN(e);
      Byte* dig = e + 1;
      x = parseLong(&dig);

      // Note: Keep this in sync with p_popup_option_values.
      if (STRNCMP(s, "height:", 7) == 0) {
         if (dig != p)
            return FAIL;
         if (po) {
            if (is_preview)
               po->pup.minHeight = x;
            po->pup.maxHeight = x;
         }
      } ei (STRNCMP(s, "width:", 6) == 0) {
         if (dig != p)
            return FAIL;
         if (po) {
            if (is_preview)
                po->pup.minWidth = x;
            po->pup.maxWidth = x;
            po->pup.maxWidthOpt = x;
         }
      } ei (STRNCMP(s, "highlight:", 10) == 0) {
         if (po) {
            int c = *p;

            *p = ZERO;
            optSetStringOptionDirectInPort(po, S"portcolor", s + 10, OPT_LOCAL, 0);
            *p = c;
         }
      } ei (STRNCMP(s, "border:", 7) == 0) {
          // Note: Keep this in sync with p_popup_option_border_values.
          Byte   *arg = s + 7;
          int      on = STRNCMP(arg, "on", 2) == 0 && arg + 2 == p;
          int      off = STRNCMP(arg, "off", 3) == 0 && arg + 3 == p;
          int      i;

         if (!on && !off)
            return FAIL;
         if (po != NULL) {
            for (i = 0; i < 4; ++i)
                po->pup.border[i] = on ? 1 : 0;
            if (off)
                // only show the X for close when there is a border
                po->pup.close = POPCLOSE_NONE;
         }
      } ei (STRNCMP(s, "align:", 6) == 0) {
         // Note: Keep this in sync with p_popup_option_align_values.
         Byte   *arg = s + 6;
         int      item = STRNCMP(arg, "item", 4) == 0 && arg + 4 == p;
         int      menu = STRNCMP(arg, "menu", 4) == 0 && arg + 4 == p;

         if (!menu && !item)
            return FAIL;
         if (po != NULL && menu)
            po->pup.flags |= POPF_INFO_MENU;
      } else
          return FAIL;
   }
   return OK;
}

//Parse the 'previewpopup' option and apply the values to portal "po" if it is not NULL.
//Return FAIL if the parsing fails.
int
parse_previewpopup(Portal *po) {
   return parse_popup_option(po, TRUE);
}

// Parse the 'completepopup' option and apply the values to portal "po" if it is not NULL.
// Return FAIL if the parsing fails.
int
parse_completepopup(Portal *po) {
   return parse_popup_option(po, FALSE);
}

// Set pup.wantLine and pup.wantCol for the cursor position in the current portal.
// Keep at least "width" columns from the right of the screen.
void
popup_set_wantpos_cursor(Portal *po, int width, Bag *d) {
   PopupPosition ppt = POPPOS_NONE;

   if (d != NULL)
      ppt = get_pos_entry(d, FALSE);

    setcursor_mayforce(TRUE);
   if (ppt == POPPOS_TOPRIGHT || ppt == POPPOS_TOPLEFT) {
      po->pup.wantLine = curPor->portalRow + curPor->cursorRow + 2;
   } else {
      po->pup.wantLine = curPor->portalRow + curPor->cursorRow;
      if (po->pup.wantLine == 0) { // cursor in first line
          po->pup.wantLine = 2;
          po->pup.pos = ppt == POPPOS_BOTRIGHT ? POPPOS_TOPRIGHT : POPPOS_TOPLEFT;
      }
   }

   po->pup.wantCol = curPor->portalCol + curPor->cursorCol + 1;
   if (po->pup.wantCol > visibleColsG - width) {
      po->pup.wantCol = visibleColsG - width;
      if (po->pup.wantCol < 1)
          po->pup.wantCol = 1;
   }

   adjustPosition(po);
}

//Set pup.wantLine and pup.wantCol for the a given screen position.
//Caller must take care of running into the portal border.
void
popup_set_wantpos_rowcol(Portal *po, int row, int col) {
   po->pup.wantLine = row;
   po->pup.wantCol = col;
   adjustPosition(po);
}

//Add a border and left&right padding.
private void
add_border_left_right_padding(Portal *po) {
   for (int i = 0; i < 4; ++i) {
      po->pup.border[i] = 1;
      po->pup.padding[i] = (i & 1) ? 1 : 0;
   }
}

//Return TRUE if there is any popup portal with a terminal book.
private Boole
popup_terminal_exists(void) {
   Portal   *po;
   FOR_ALL_POPUPPORTS(po) {
      if (po->book->term)
         return TRUE;
   } 
   Tab   *t;
   FOR_ALL_TABS(t) {
      FOR_ALL_POPUPPORTS_IN_TAB(t, po) {
         if (po->book->term)
            return TRUE;
      } 
   } 
   return FALSE;
}

//Mark all popup portals in the current tab and global for redrawing.
void
popup_redraw_all(void) {
   Portal   *po;
   FOR_ALL_POPUPPORTS(po)
      po->redrawType = UPD_NOT_VALID;
   FOR_ALL_POPUPPORTS_IN_TAB(curtab, po)
      po->redrawType = UPD_NOT_VALID;
}

//Set the color for a notification portal
private void
updateNotificationColor(Portal *po, PopupKind type) {
   Arr(char) hiname = type == POPUP_MESSAGE_WIN ? "InfoMsg" : "PopupNotification";
   optSetStringOptionDirectInPort(po, S"portcolor", (CS)hiname, OPT_LOCAL, 0);
}

private void
initPopupBook(Book* book) {
   optSetStringOptionDirectInBook(
       book, S"booktype", S"popup", OPT_LOCAL, 0
   );
   book->o.swapFile = FALSE;   // no swap file
   book->o.bookListed = false;    // unlisted book
   book->locked = TRUE;   // prevent deleting the book

   // Avoid that 'buftype' is reset when this book is entered.
   book->o.initialized = true;
}


// createPopup({text}, {options})
// popup_atcursor({text}, {options})
// When creating a preview or info popup "argvars" and "returnVar" are NULL.
// If the first arg is a number, it's interpreted as the book index.
// If it's a string, then a new book is created and filled with that string.
Portal*
createPopup(Arr(Var) argvars, OUT Var* returnVar, PopupKind kind) {
   Portal   *po;
   Tab* tab = NULL;
   int      tabnr = 0;
   Book* book = NULL;
   Bag* params = NULL;
   int      i;

   if (argvars) {
      // Check that arguments look OK.
      if (argvars[0].tag == VAR_NUMBER) {
         book = bookFindFileByBookNr(argvars[0].number);
         if (!book) {
            showErrFmtMsg(_(e_book_nr_does_not_exist), argvars[0].number);
            return NULL;
         }
         if (book->term && popup_terminal_exists()) {
            emsg(_(e_cannot_open_second_popup_with_terminal));
            return NULL;
         }
      } ei (!(argvars[0].tag == VAR_STRING && argvars[0].string != NULL)
             && !(argvars[0].tag == VAR_LIST && argvars[0].list != NULL)
      ) {
         emsg(_(e_buffer_number_text_or_list_required));
         return NULL;
      }
      if (check_for_nonnull_dict_arg(argvars, 1) == FAIL)
         return NULL;
      params = argvars[1].bag;
   }
   
   if (isNotification(kind)) {
      tabnr = -1;  // notifications are global by default
   } ei (params != NULL) {
      if (bagHasKey(params, tConst("tabpage"))) {
         tabnr = (int)bagGetNumber(params, tConst("tabpage"));
         if (tabnr > 0) {
            tab = getTab(tabnr);
            if (!tab) {
               showErrFmtMsg(_(e_tabpage_not_found_nr), tabnr);
               return NULL;
            }
         }
      } else
         tabnr = 0;
   }

   if (book && book->lockedSplit) {
      // disallow opening a popup to a closing book, which like splitting,
      // can result in more portals displaying it
      emsg(_(e_cannot_open_a_popup_portal_to_a_closing_buffer));
      return NULL;
   }

   // Create the portal and book.
   po = portAllocPopup();
   if (!po)
      return NULL;
      
   if (returnVar)
      returnVar->number = po->id;
      
   po->pup.pos = POPPOS_TOPLEFT;
   po->pup.flags = POPF_IS_POPUP | POPF_MAPPING | POPF_POSINVERT;

   Boole createdNewBook;
   if (book) {
      // use existing book
      createdNewBook = false;
      initPopupPortal(po, book);
      optSetLocalOptionsToDefault(po, false);
      swap_exists_action = SEA_READONLY;
      bookEnsureLoaded(book);
      swap_exists_action = SEA_NONE;
   } else {
      // create a new book associated with the popup
      createdNewBook = true;
      book = bookNew(NULL, NULL, (LineNr)0, BLN_NEW|BLN_DUMMY|BLN_REUSE);
      if (!book) {
         portFreePopup(po);
         return NULL;
      }
      ml_open(book);

      initPopupPortal(po, book);

      optSetLocalOptionsToDefault(po, true);
      initPopupBook(book);
   }
   po->o.wrap = TRUE;   // 'wrap' is default on
   po->o.scrollOff = 0;      // 'scrolloff' zero

   if (tab) {
      // popup on specified tab
      po->next = tab->firstPopupPort;
      tab->firstPopupPort = po;
   } ei (tabnr == 0) {
      // popup on current tab
      po->next = curtab->firstPopupPort;
      curtab->firstPopupPort = po;
   } else { // (tabnr < 0)
      Portal *prev = firstPopupPortG;

      // Global popup: add at the end, so that it gets displayed on top of
      // older ones with the same zindex. Matters for notifications.
      if (!firstPopupPortG)
         firstPopupPortG = po;
      else {
         while (prev->next)
            prev = prev->next;
         prev->next = po;
      }
   }

   if (createdNewBook && argvars != NULL) {
      setBookText(book, argvars[0]);
   } 

   if (kind == POPUP_ATCURSOR || kind == POPUP_PREVIEW) {
      po->pup.pos = POPPOS_BOTLEFT;
   }
   if (kind == POPUP_ATCURSOR) {
      popup_set_wantpos_cursor(po, 0, params);
      set_moved_values(po);
      set_moved_columns(po, FIND_STRING);
   }

   if (kind == POPUP_BEVAL) {
      po->pup.pos = POPPOS_BOTLEFT;

      // by default use the mouse position
      po->pup.wantLine = mouseRowG;
      if (po->pup.wantLine <= 0) { // mouse on first line
         po->pup.wantLine = 2;
         po->pup.pos = POPPOS_TOPLEFT;
      }
      po->pup.wantCol = mouseColG + 1;
      set_mousemoved_values(po);
      set_mousemoved_columns(po, FIND_IDENT + FIND_STRING + FIND_EVAL);
   }

   // set default values
   po->pup.zIndex = POPUPWIN_DEFAULT_ZINDEX;
   po->pup.close = POPCLOSE_NONE;

   if (isNotification(kind)) {
      Portal* twp;
      Portal* nextPort;
      int height = book->mem.lineCount + 3;

      po->pup.pos = POPPOS_BOTRIGHT;
      // Try to not overlap with another global popup. Guess we need 3
      // more screen lines than book lines.
      po->pup.wantLine = 1;
      for (twp = firstPopupPortG; twp; twp = nextPort) {
         nextPort = twp->next;
         if (twp != po
             && twp->pup.zIndex == POPUPWIN_NOTIFICATION_ZINDEX
             && twp->portalRow <= po->pup.wantLine - 1 + height
             && twp->portalRow + popup_height(twp) > po->pup.wantLine - 1
         ){
            // move to below this popup and restart the loop to check for
            // overlap with other popups
            po->pup.wantLine = twp->portalRow + popup_height(twp) + 1;
            nextPort = firstPopupPortG;
         }
      }
      if (po->pup.wantLine + height > visibleRowsG) {
         // can't avoid overlap, put on top in the hope that message goes away soon.
         po->pup.wantLine = 1;
      }

      po->pup.wantCol = 10;
      po->pup.zIndex = POPUPWIN_NOTIFICATION_ZINDEX;
      po->pup.minWidth = 20;
      po->pup.flags |= POPF_DRAG;
      po->pup.close = POPCLOSE_CLICK;
      for (i = 0; i < 4; ++i)
         po->pup.border[i] = 1;
      po->pup.padding[1] = 1;
      po->pup.padding[3] = 1;

      updateNotificationColor(po, kind);
   } ei (kind == POPUP_DIALOG || kind == POPUP_MENU) {
      po->pup.pos = POPPOS_CENTER;
      po->pup.zIndex = POPUPWIN_DIALOG_ZINDEX;
      po->pup.flags |= POPF_DRAG;
      po->pup.flags &= ~POPF_MAPPING;
      add_border_left_right_padding(po);
   } ei (kind == POPUP_MENU) {
      Var   tv;
      tv.tag = VAR_STRING;
      tv.string = (CS)"popup_filter_menu";
      Callback callback = get_callback(&tv);
      if (callback.name != NULL) {
         set_callback(&po->pup.filterCb, &callback);
         if (callback.needsFreeing)
            eeglFree(callback.name);
      }

      po->o.wrap = 0;
      po->pup.flags |= POPF_CURSORLINE;
   } ei (kind == POPUP_PREVIEW) {
         po->pup.flags |= POPF_DRAG | POPF_RESIZE;
         po->pup.close = POPCLOSE_BUTTON;
      for (i = 0; i < 4; ++i)
          po->pup.border[i] = 1;
      parse_previewpopup(po);
      popup_set_wantpos_cursor(po, po->pup.minWidth, params);
   } ei (kind == POPUP_INFO) {
      po->pup.pos = POPPOS_TOPLEFT;
      po->pup.flags |= POPF_DRAG | POPF_RESIZE;
      po->pup.close = POPCLOSE_BUTTON;
      add_border_left_right_padding(po);
      parse_completepopup(po);
   }

   for (i = 0; i < 4; ++i)
      EE_CLEAR(po->pup.borderHilite[i]);
   for (i = 0; i < 8; ++i)
      po->pup.borderChar[i] = 0;
   po->pup.wantScrollbar = 1;
   po->pup.fixed = 0;
   po->pup.filterMode = MODE_ALL;

   if (params) {
      if (applyParams(po, params, TRUE) == FAIL) {
         (void)popup_close(po->id, FALSE);
         return NULL;
      }
   }

   if (isNotification(kind) && po->pup.timer == NULL) {
      addTimeout(po, NOTIFICATION_TIMEOUT, kind == POPUP_NOTIFICATION);
   } 

   adjustPosition(po);

   po->vsepWidth = 0;

   redraw_all_later(UPD_NOT_VALID);
   needRefreshPopupMaskG = TRUE;

   // When running a terminal in the popup it becomes the current portal.
   if (book->term)
      enterPortal(po, FALSE);

   return po;
}

//popup_clear()
void
f_popup_clear(Var *argvars, Var *returnVar UNUSED) {
   int force = FALSE;

   if (argvars[0].tag != VAR_UNKNOWN)
      force = (int)tv_get_bool(&argvars[0]);
   close_all_popups(force);
}

//createPopup({text}, {options})
void
f_createPopup(Var *argvars, OUT Var *returnVar) {
   createPopup(argvars, returnVar, POPUP_NORMAL);
}

//popup_atcursor({text}, {options})
void
f_popup_atcursor(Var *argvars, OUT Var *returnVar) {
   createPopup(argvars, OUT returnVar, POPUP_ATCURSOR);
}

//popup_beval({text}, {options})
void
f_popup_beval(Var *argvars, OUT Var *returnVar) {
   createPopup(argvars, OUT returnVar, POPUP_BEVAL);
}

//Invoke the close callback for portal "po" with value "result".
//Careful: The callback may make "po" invalid!
private void
invokeCallback(Portal *po, Var *result) {
   Var   returnVar;
   Var   argv[3];

   returnVar.tag = VAR_UNKNOWN;

   argv[0].tag = VAR_NUMBER;
   argv[0].number = (Long)po->id;

   if (result && result->tag != VAR_UNKNOWN)
      copy_tv(OUT &argv[1], result);
   else {
      argv[1].tag = VAR_NUMBER;
      argv[1].number = 0;
   }

   argv[2].tag = VAR_UNKNOWN;

   call_callback(&po->pup.closeCb, -1, &returnVar, 2, argv);
   if (result)
      clearVar(&argv[1]);
   clearVar(&returnVar);
}

//Make "prevPor" the current portal, unless it's equal to "po".
//Otherwise make "firstPor" the current portal.
private void
back_to_prevPor(Portal *po) {
   if (portalIsValid(prevPor) && po != prevPor)
      enterPortal(prevPor, FALSE);
   else
      enterPortal(firstPor, FALSE);
}

//Close popup "po" and invoke any close callback for it.
//Careful: callback function might have freed the popup portal already
private void
popup_close_and_callback(Portal *po, Var *arg) {
   if (!portalIsValid(po))
      return;

   int id = po->id;

   if (po == curPor && curBook->term != NULL) {
      Portal *owp;

      // Closing popup portal with a terminal: put focus back on the first that works:
      // - another popup portal with a terminal
      // - the previous portal
      // - the first one.
      FOR_ALL_POPUPPORTS(owp) {
         if (owp != curPor && owp->book->term != NULL)
            break;
      } 
      if (owp)
         enterPortal(owp, FALSE);
      else {
         FOR_ALL_POPUPPORTS_IN_TAB(curtab, owp) {
            if (owp != curPor && owp->book->term != NULL)
               break;
         }
         if (owp)
            enterPortal(owp, FALSE);
         else
            back_to_prevPor(po);
      }
   }

   // Just in case a check higher up is missing.
   if (po == curPor && portErrorIfPopup(false)) {
      // To avoid getting stuck when win_execute() does something that causes
      // an error, stop calling the filter callback.
      evFreeCallback(&po->pup.filterCb);
      return;
   }

   CHECK_CURBOOK;
   if (po->pup.closeCb.name != NULL)
      // Careful: This may make "po" invalid.
      invokeCallback(po, arg);

   popup_close(id, FALSE);
   CHECK_CURBOOK;
}

void
popup_close_with_retval(Portal *po, int retval) {
   Var res;

   res.tag = VAR_NUMBER;
   res.number = retval;
   popup_close_and_callback(po, &res);
}

//Close popup "po" because of a mouse click.
void
popup_close_for_mouse_click(Portal *po) {
   popup_close_with_retval(po, -2);
}

private void
check_mouse_moved(Portal *po, Portal *mouse_wp) {
   // Close the popup when all if these are true:
   // - the mouse is not on this popup
   // - "mousemoved" was used
   // - the mouse is no longer on the same screen row or the mouse column is
   //   outside of the relevant text
   if (po != mouse_wp
       && po->pup.mouseRow != 0
       && (po->pup.mouseRow != mouseRowG
         || mouseColG < po->pup.mouseMinCol
         || mouseColG > po->pup.mouseMaxCol)
   ) {
      // Careful: this makes "po" invalid.
      popup_close_with_retval(po, -2);
   }
}

// Called when the mouse moved: may close a popup with "mousemoved".
void
popup_handle_mouse_moved(void) {
   Portal* nextwp;
   int       row = mouseRowG;
   int       col = mouseColG;

   // find the portal where the mouse is in
   Portal* mouse_wp = mouseFindPortal(&row, &col, FIND_POPUP);

   for (Portal* po = firstPopupPortG; po; po = nextwp) {
      nextwp = po->next;
      check_mouse_moved(po, mouse_wp);
   }
   for (Portal* po = curtab->firstPopupPort; po; po = nextwp) {
      nextwp = po->next;
      check_mouse_moved(po, mouse_wp);
   }
}

// In a filter: check if the typed key is a mouse event that is used for dragging the popup.
private void
filter_handle_drag(Portal *po, int c, Var *returnVar) {
   int   row = mouseRowG;
   int   col = mouseColG;

   if ((po->pup.flags & (POPF_DRAG | POPF_DRAGALL))
          && is_mouse_key(c)
          && (po == popupDragPortG
              || po == mouseFindPortal(&row, &col, FIND_POPUP)))
      // do not consume the key, allow for dragging the popup
      returnVar->number = 0;
}

// popup_filter_menu({id}, {key})
void
f_popup_filter_menu(Var *argvars, Var *returnVar) {
   int id = tv_get_number(&argvars[0]);
   Portal* po = getPortalById(id);
   // If the popup has been closed, do not consume the key.
   if (!po)
      return;
      
   CS key = tv_get_string(&argvars[1]);
   Unt c = *key;
   if (c == K_SPECIAL && key[1] != ZERO)
      c = TO_SPECIAL(key[1], key[2]);

   // consume all keys until done
   returnVar->tag = VAR_BOOL;
   returnVar->number = VVAL_TRUE;
   Var   res;
   res.tag = VAR_NUMBER;

   LineNr old_lnum = po->cursor.lnum;
   if (c == 'k' || c == 'K' || c == K_UP || c == Ctrl_P) {
      if (po->cursor.lnum > 1)
         --po->cursor.lnum;
      else
         po->cursor.lnum = po->book->mem.lineCount;
   }
   if (c == 'j' || c == 'J' || c == K_DOWN || c == Ctrl_N) {
      if (po->cursor.lnum < po->book->mem.lineCount)
         ++po->cursor.lnum;
      else
         po->cursor.lnum = 1;
   }
   if (old_lnum != po->cursor.lnum) {
      // caller will call highlightCurrentLine()
      return;
   }

   if (c == 'x' || c == 'X' || c == ESC || c == Ctrl_C) {
      // Cancelled, invoke callback with -1
      res.number = -1;
      popup_close_and_callback(po, &res);
      return;
   }
   if (c == ' ' || c == K_KENTER || c == ENTER || c == NL) {
      // Invoke callback with current index.
      res.number = po->cursor.lnum;
      popup_close_and_callback(po, &res);
      return;
   }

    filter_handle_drag(po, c, returnVar);
}

// popup_filter_yesno({id}, {key})
void
f_popup_filter_yesno(Var *argvars, Var *returnVar) {
   int      id;
   Portal   *po;
   Byte   *key;
   Var   res;
   int      c;


   id = tv_get_number(&argvars[0]);
   po = getPortalById(id);
   key = tv_get_string(&argvars[1]);
   // If the popup has been closed don't consume the key.
   if (!po)
      return;

   c = *key;
   if (c == ENTER && need_wait_return)
      return;
   if (c == K_SPECIAL && key[1] != ZERO)
      c = TO_SPECIAL(key[1], key[2]);

   // consume all keys until done
   returnVar->tag = VAR_BOOL;
   returnVar->number = VVAL_TRUE;

   if (c == 'y' || c == 'Y')
      res.number = 1;
   ei (c == 'n' || c == 'N' || c == 'x' || c == 'X' || c == ESC)
      res.number = 0;
   else {
      filter_handle_drag(po, c, returnVar);
      return;
   }

   // Invoke callback
   res.tag = VAR_NUMBER;
   popup_close_and_callback(po, &res);
}

// popup_dialog({text}, {options})
void
f_popup_dialog(Var *argvars, Var *returnVar) {
   createPopup(argvars, returnVar, POPUP_DIALOG);
}

// popup_menu({text}, {options})
void
f_popup_menu(Var *argvars, Var *returnVar) {
   createPopup(argvars, returnVar, POPUP_MENU);
}

// popup_notification({text}, {options})
void
f_popup_notification(Var *argvars, Var *returnVar) {
   createPopup(argvars, returnVar, POPUP_NOTIFICATION);
}

//Find the popup portal with portal-ID "id".
//If the popup portal does not exist NULL is returned.
//If the portal is not a popup portal, and error message is given.
private Portal *
findPopupPortal(int id) {
   Portal *po = getPortalById(id);

   if (po && !PORTAL_IS_POPUP(po)) {
      showErrFmtMsg(_(e_window_nr_is_not_popup_portal), id);
      return NULL;
   }
   return po;
}

// popup_close({id})
void
f_popup_close(Var *argvars, Var *returnVar UNUSED) {
   int      id;
   Portal   *po;

   id = (int)tv_get_number(argvars);
   if (curBook->term == NULL && portErrorIfPopup(true))
      // if the popup contains a terminal, it will become hidden
      return;

   po = findPopupPortal(id);
   if (po)
      popup_close_and_callback(po, &argvars[1]);
}

void
popup_hide(Portal *po) {
   if (portErrorIfTermPopup() || (po->pup.flags & POPF_HIDDEN) != 0)
      return;

   po->pup.flags |= POPF_HIDDEN;
   // Do not decrement countPortals, we still reference the book.
   if (po->portalRow + popup_height(po) >= (int)commlineRowG)
      mustClearCommlineG = TRUE;
   redraw_all_later(UPD_NOT_VALID);
   needRefreshPopupMaskG = TRUE;
}

// popup_hide({id})
void
f_popup_hide(Var *argvars, Var *returnVar UNUSED) {
   int id = (int)tv_get_number(argvars);
   Portal* po = findPopupPortal(id);
   if (!po)
      return;

   popup_hide(po);
   po->pup.flags |= POPF_HIDDEN_FORCE;
}

void
popup_show(Portal *po) {
   if ((po->pup.flags & POPF_HIDDEN) == 0)
      return;

   po->pup.flags &= ~POPF_HIDDEN;
   redraw_all_later(UPD_NOT_VALID);
   needRefreshPopupMaskG = TRUE;
}

//popup_show({id})
void
f_popup_show(Var *argvars, Var *returnVar UNUSED) {
   int id = (int)tv_get_number(argvars);
   Portal* po = findPopupPortal(id);
   if (!po)
      return;

   po->pup.flags &= ~POPF_HIDDEN_FORCE;
   popup_show(po);
   if (po->pup.flags & POPF_INFO)
      pum_position_info_popup(po);
}

//popup_settext({id}, {text})
void
f_popup_settext(Var *argvars, Var *returnVar UNUSED) {
   int id = (int)tv_get_number(&argvars[0]);
   Portal* po = findPopupPortal(id);
   if (!po || check_for_string_or_list_arg(argvars, 1) == FAIL)
      return;

   setBookText(po->book, argvars[1]);
   redrawPortLater(po, UPD_NOT_VALID);
   adjustPosition(po);
}

//popup_setbuf({id}, {bufnr})
void
f_popup_setbuf(Var *argvars, Var *returnVar UNUSED) {
   returnVar->tag = VAR_BOOL;
   returnVar->number = VVAL_FALSE;

   if (check_for_number_arg(argvars, 0) == FAIL || check_for_buffer_arg(argvars, 1) == FAIL)
      return;

   int id = (int)tv_get_number(&argvars[0]);
   Portal* po = findPopupPortal(id);
   if (po == NULL)
      return;

   Book* book = daGetBookFromArg(&argvars[1]);

   if (!book)
      return;
   if (book->term && popup_terminal_exists()) {
      emsg(_(e_cannot_open_second_popup_with_terminal));
      return;
   }

   if (po->book != book) {
      po->book->countPortals--;
      initPopupPortal(po, book);
      optSetLocalOptionsToDefault(po, false);
      swap_exists_action = SEA_READONLY;
      bookEnsureLoaded(book);
      swap_exists_action = SEA_NONE;
      redrawPortLater(po, UPD_NOT_VALID);
      adjustPosition(po);
   }
   returnVar->number = VVAL_TRUE;
}

private void
popup_free(Portal *po) {
   sign_undefine_by_name(popup_get_sign_name(po), false);
   po->book->locked = FALSE;
   if (po->portalRow + popup_height(po) >= (int)commlineRowG)
      mustClearCommlineG = TRUE;
   portFreePopup(po);

   if (po == messagePort)
      messagePort = NULL;

   redraw_all_later(UPD_NOT_VALID);
   needRefreshPopupMaskG = TRUE;
}

private void
error_for_popup_portal(void) {
   emsg(_(e_not_allowed_in_popup_portal));
}

Boole
portErrorIfPopup(Boole also_with_term) {
   //win_execute() may set "curPor" to a popup portal temporarily, but many
   //commands are disallowed then. When a terminal runs in the popup, most
   //things are allowed. When a terminal is finished it can be closed.
   if (PORTAL_IS_POPUP(curPor) && (also_with_term || curBook->term == NULL)) {
      error_for_popup_portal();
      return true;
   }
   return false;
}

//Close a popup portal by Portal-id. Does not invoke the callback.
//Return OK if the popup was closed, FAIL otherwise.
int
popup_close(int id, int force) {
   Portal   *prev = NULL;
   // go through global popups
   for (Portal* po = firstPopupPortG; po; prev = po, po = po->next) {
      if (po->id == id) {
         if (po == curPor) {
            if (!force) {
               error_for_popup_portal();
               return FAIL;
            }
            back_to_prevPor(po);
         }
         if (!prev)
            firstPopupPortG = po->next;
         else
            prev->next = po->next;
         popup_free(po);
         return OK;
      }
   }

   // go through tab-local popups
   Tab *t;
   FOR_ALL_TABS(t) {
      if (popupCloseTab(t, id, force) == OK)
          return OK;
   } 
   return FAIL;
}

// Close a popup portal with Portal-id "id" in tab "tab".
int
popupCloseTab(Tab* tab, int id, int force) {
   Portal* po;
   Portal** root = &tab->firstPopupPort;
   Portal* prev = NULL;

   for (po = *root; po; prev = po, po = po->next) {
      if (po->id == id) {
         if (po == curPor) {
            if (!force) {
               error_for_popup_portal();
               return FAIL;
            }
            back_to_prevPor(po);
         }
         if (!prev)
            *root = po->next;
         else
            prev->next = po->next;
         popup_free(po);
         return OK;
      }
   }
   return FAIL;
}

void
close_all_popups(int force) {
   if (!force && portErrorIfPopup(true))
      return;
   while (firstPopupPortG) {
      if (popup_close(firstPopupPortG->id, force) == FAIL)
         return;
   } 
   while (curtab->firstPopupPort) {
      if (popup_close(curtab->firstPopupPort->id, force) == FAIL)
          return;
   } 
}

// popup_move({id}, {options})
void
f_popup_move(Var *argvars, Var *returnVar UNUSED) {
   int      id;

   id = (int)tv_get_number(argvars);
   Portal* po = findPopupPortal(id);
   if (po == NULL)
      return;  // invalid {id}

   if (check_for_nonnull_dict_arg(argvars, 1) == FAIL)
      return;
   Bag* params = argvars[1].bag;

   applyMoveParams(po, params);

   if (po->portalRow + po->height >= commlineRowG)
      mustClearCommlineG = TRUE;
   adjustPosition(po);
}

// popup_setoptions({id}, {options})
void
f_popup_setoptions(Var *argvars, Var *returnVar UNUSED) {
   int id = (int)tv_get_number(argvars);
   Portal* po = findPopupPortal(id);
   if (!po)
      return;  // invalid {id}

   if (check_for_nonnull_dict_arg(argvars, 1) == FAIL)
      return;
   Bag* params = argvars[1].bag;
   LineNr old_firstline = po->pup.firstLine;

   (void)applyParams(po, params, FALSE);

   if (old_firstline != po->pup.firstLine)
      redrawPortLater(po, UPD_NOT_VALID);
   adjustPosition(po);
}

// popup_getpos({id})
void
f_popup_getpos(Var *argvars, Var *returnVar) {
   allocReturnDict(returnVar);

   int id = (int)tv_get_number(argvars);
   Portal* po = findPopupPortal(id);
   if (!po)
      return;  // invalid {id}
   int top_extra = popup_top_extra(po);
   int left_extra = po->pup.border[3] + po->pup.padding[3];

   // we know how much space we need, avoid resizing halfway
   Bag* dict = returnVar->bag;
   hash_lock_size(&dict->hashTable, 11);

   bagAddNumber(dict, S"line", po->portalRow + 1);
   bagAddNumber(dict, S"col", po->portalCol + 1);
   bagAddNumber(dict, S"width", po->width + left_extra
       + po->pup.border[1] + po->pup.padding[1]);
   bagAddNumber(dict, S"height", po->height + top_extra
       + po->pup.border[2] + po->pup.padding[2]);

    bagAddNumber(dict, S"core_line", po->portalRow + 1 + top_extra);
    bagAddNumber(dict, S"core_col", po->portalCol + 1 + left_extra);
    bagAddNumber(dict, S"core_width", po->width);
    bagAddNumber(dict, S"core_height", po->height);

    bagAddNumber(dict, S"scrollbar", po->pup.hasScrollbar);
    bagAddNumber(dict, S"firstline", po->topLine);
    bagAddNumber(dict, S"lastline", po->bottomLine - 1);
    bagAddNumber(dict, S"visible",
       portalIsValid(po) && (po->pup.flags & POPF_HIDDEN) == 0);

    hash_unlock(&dict->hashTable);
}

// popup_list()
void
f_popup_list(Var *argvars UNUSED, OUT Var *returnVar) {
   allocReturnList(returnVar);
      
   Portal   *po;
   FOR_ALL_POPUPPORTS(po)
      list_append_number(returnVar->list, po->id);
      
   Tab   *t;
   FOR_ALL_TABS(t) {
      FOR_ALL_POPUPPORTS_IN_TAB(t, po)
         list_append_number(returnVar->list, po->id);
   } 
}

// popup_locate({row}, {col})
void
f_popup_locate(Var *argvars, Var *returnVar) {
   int row = tv_get_number(&argvars[0]) - 1;
   int col = tv_get_number(&argvars[1]) - 1;
   Portal* po = mouseFindPortal(&row, &col, FIND_POPUP);
   if (po && PORTAL_IS_POPUP(po))
      returnVar->number = po->id;
}

//For popup_getoptions(): add a "border" or "padding" entry to "dict".
private void
get_padding_border(Bag *dict, int *array, CS name) {
   if (array[0] == 0 && array[1] == 0 && array[2] == 0 && array[3] == 0)
      return;

   List* list = list_alloc();
   bagAddList(dict, name, list);
   if (array[0] != 1 || array[1] != 1 || array[2] != 1 || array[3] != 1) {
      for (int i = 0; i < 4; ++i)
         list_append_number(list, array[i]);
   } 
}

// For popup_getoptions(): add a "borderhighlight" entry to "dict".
private void
get_borderhighlight(Bag *dict, Portal *po) {
   int       i;
   for (i = 0; i < 4; ++i) {
      if (po->pup.borderHilite[i] != NULL)
          break;
   } 
   if (i == 4)
      return;

   List* list = list_alloc();
   bagAddList(dict, S"borderhighlight", list);
   for (i = 0; i < 4; ++i)
      list_append_string(list, po->pup.borderHilite[i], -1);
}

// For popup_getoptions(): add a "borderchars" entry to "dict".
private void
get_borderchars(Bag *bag, Portal *po) {
   Byte  buf[NUMBUFLEN];
   
   int       i;
   for (i = 0; i < 8; ++i) {
      if (po->pup.borderChar[i] != 0)
         break;
   } 
   if (i == 8)
      return;

   List* list = list_alloc();
   bagAddList(bag, S"borderchars", list);
   for (i = 0; i < 8; ++i) {
      int len = mb_char2bytes(po->pup.borderChar[i], buf);
      list_append_string(list, buf, len);
   }
}

//For popup_getoptions(): add a "moved" and "mousemoved" entry to "dict".
private void
get_moved_list(Bag *bag, Portal *po) {
   List* list = list_alloc();
   bagAddList(bag, S"moved", list);
   list_append_number(list, po->pup.lnum);
   list_append_number(list, po->pup.minCol);
   list_append_number(list, po->pup.maxCol);
   
   list = list_alloc();
   bagAddList(bag, S"mousemoved", list);
   list_append_number(list, po->pup.mouseRow);
   list_append_number(list, po->pup.mouseMinCol);
   list_append_number(list, po->pup.mouseMaxCol);
}

//popup_getoptions({id})
void
f_popup_getoptions(Var *argvars, OUT Var *returnVar) {
   allocReturnDict(returnVar);

   int id = (int)tv_get_number(argvars);
   Portal* po = findPopupPortal(id);
   if (!po)
      return;

   Bag* b = returnVar->bag;
   bagAddNumber(b, S"line", po->pup.wantLine);
   bagAddNumber(b, S"col", po->pup.wantCol);
   bagAddNumber(b, S"minwidth", po->pup.minWidth);
   bagAddNumber(b, S"minheight", po->pup.minHeight);
   bagAddNumber(b, S"maxheight", po->pup.maxHeight);
   bagAddNumber(b, S"maxwidth", po->pup.maxWidth);
   bagAddNumber(b, S"firstline", po->pup.firstLine);
   bagAddNumber(b, S"scrollbar", po->pup.wantScrollbar);
   bagAddNumber(b, S"zindex", po->pup.zIndex);
   bagAddNumber(b, S"fixed", po->pup.fixed);
   if (po->pup.propType && doesPortalExistInAnyTab(po->pup.propPort)) {
      PropType* pt = text_prop_type_by_id(po->pup.propPort->book, po->pup.propType);

      if (pt != NULL)
         bagAddString(b, S"textprop", pt->name);
      bagAddNumber(b, S"textpropwin", po->pup.propPort->id);
      bagAddNumber(b, S"textpropid", po->pup.propId);
   }
   bagAddString(b, S"title", po->pup.title);
   bagAddNumber(b, S"wrap", po->o.wrap);
   bagAddNumber(b, S"drag", (po->pup.flags & POPF_DRAG) != 0);
   bagAddNumber(b, S"dragall", (po->pup.flags & POPF_DRAGALL) != 0);
   bagAddNumber(b, S"mapping", (po->pup.flags & POPF_MAPPING) != 0);
   bagAddNumber(b, S"resize", (po->pup.flags & POPF_RESIZE) != 0);
   bagAddNumber(b, S"posinvert", (po->pup.flags & POPF_POSINVERT) != 0);
   bagAddNumber(b, S"cursorline", (po->pup.flags & POPF_CURSORLINE) != 0);
   bagAddString(b, S"highlight", po->o.hiliteGroupName);
   if (po->pup.scrollbarHilite)
      bagAddString(b, S"scrollbarhighlight", po->pup.scrollbarHilite);
   if (po->pup.thumbHilite)
      bagAddString(b, S"thumbhighlight", po->pup.thumbHilite);

   // find the tab that holds this popup
   int i = 1;
   Tab* t;
   FOR_ALL_TABS(t) {
      Portal *twp;

      FOR_ALL_POPUPPORTS_IN_TAB(t, twp) {
         if (twp->id == id)
            break;
      } 
      if (twp)
         break;
      ++i;
   }
   if (!t)
      i = -1;  // must be global
   ei (t == curtab)
      i = 0;
   bagAddNumber(b, S"tabpage", i);

   get_padding_border(b, po->pup.padding, S"padding");
   get_padding_border(b, po->pup.border, S"border");
   get_borderhighlight(b, po);
   get_borderchars(b, po);
   get_moved_list(b, po);

   if (po->pup.filterCb.name)
      bagAddCallback(b, S"filter", &po->pup.filterCb);
   if (po->pup.closeCb.name)
      bagAddCallback(b, S"callback", &po->pup.closeCb);

   for (i = 0; i < (int)ARRAY_LENGTH(popposEntriesS); ++i) {
      if (po->pup.pos == popposEntriesS[i].pp_val) {
         bagAddString(b, S"pos", (CS)popposEntriesS[i].pp_name);
         break;
      }
   } 

   bagAddString(b, S"close", 
      (CS)( po->pup.close == POPCLOSE_BUTTON 
         ? "button" : po->pup.close == POPCLOSE_CLICK ? "click" : "none"
      )
   );

   bagAddNumber(b, S"time", po->pup.timer ? (long)po->pup.timer->tr_interval : 0L);
}

//Return TRUE if the current portal is running a terminal in a popup portal.
//FALSE when the job has ended.
Boole
portErrorIfTermPopup(void) {
   if (PORTAL_IS_POPUP(curPor) && curBook->term && term_job_running(curBook->term)) {
      emsg(_(e_not_allowed_for_terminal_in_popup_portal));
      return TRUE;
   }
   return FALSE;
}

//Reset all the "handled_flag" flags in global popup portals and popup portals in the current tab
//Each calling function should use a different flag, see the list at
//POPUP_HANDLED_1.  This won't work with recursive calls though.
void
popup_reset_handled(int handled_flag) {
   Portal *po;

   FOR_ALL_POPUPPORTS(po) {
      po->pup.handled &= ~handled_flag;
   } 
   FOR_ALL_POPUPPORTS_IN_TAB(curtab, po) {
      po->pup.handled &= ~handled_flag;
   } 
}

// Find the next visible popup where "handled_flag" is not set. Must have called 
// popup_reset_handled() first. When "lowest" is TRUE find the popup with the lowest zindex, 
// otherwise the popup with the highest zindex.
Portal *
find_next_popup(int lowest, int handled_flag) {
   Portal   *po;
   Portal   *found_wp;
   int       found_zindex;

   found_zindex = lowest ? INT_MAX : 0;
   found_wp = NULL;
   FOR_ALL_POPUPPORTS(po) {
      if ((po->pup.handled & handled_flag) == 0
         && (po->pup.flags & POPF_HIDDEN) == 0
         && (lowest ? po->pup.zIndex < found_zindex : po->pup.zIndex > found_zindex)
      ){
          found_zindex = po->pup.zIndex;
          found_wp = po;
      }
   }
   FOR_ALL_POPUPPORTS_IN_TAB(curtab, po) {
      if ((po->pup.handled & handled_flag) == 0
            && (po->pup.flags & POPF_HIDDEN) == 0
            && (lowest ? po->pup.zIndex < found_zindex : po->pup.zIndex > found_zindex)
      ){
         found_zindex = po->pup.zIndex;
         found_wp = po;
      }
   } 

   if (found_wp)
      found_wp->pup.handled |= handled_flag;
   return found_wp;
}

//Invoke the filter callback for portal "po" with typed character "c". Uses the global "modMaskG" 
//for modifiers. Return the return value of the filter or -1 for CTRL-C in the current portal.
//Careful: The filter may make "po" invalid!
private int
invoke_popup_filter(Portal *po, int c) {
   int      res;
   Var   returnVar;
   Var   argv[3];
   Byte   buf[NUMBUFLEN];
   LineNr   old_lnum = po->cursor.lnum;
   int      prev_anyEmsgG = anyEmsgG;

   // Emergency exit: CTRL-C closes the popup.
   if (c == Ctrl_C) {
      int save_gotInterruptG = gotInterruptG;
      int was_curPor = po == curPor;

      // Reset gotInterruptG to avoid the callback isn't called.
      gotInterruptG = FALSE;
      popup_close_with_retval(po, -1);
      gotInterruptG |= save_gotInterruptG;

      // If the popup is the current portal it probably fails to close.  Then
      // do not consume the key.
      if (was_curPor && po == curPor)
         return -1;
      return TRUE;
   }

   argv[0].tag = VAR_NUMBER;
   argv[0].number = (Long)po->id;

   // Convert the number to a string, so that the function can use:
   //       if a:c == "\<F2>"
   buf[special_to_buf(c, modMaskG, FALSE, buf)] = ZERO;
   argv[1].tag = VAR_STRING;
   argv[1].string = copyStr(buf);

   argv[2].tag = VAR_UNKNOWN;

   // NOTE: The callback might close the popup and make "po" invalid.
   if (call_callback(&po->pup.filterCb, -1, &returnVar, 2, argv) == FAIL) {
      // Cannot call the function, close the popup to avoid that the filter
      // eats keys and the user is stuck.  Might as well eat the key.
      popup_close_with_retval(po, -1);
      res = TRUE;
   } else {
      if (portalValidPopup(po) && old_lnum != po->cursor.lnum)
         highlightCurrentLine(po);

      // If an error message was given always return FALSE, so that keys are
      // not consumed and the user can type something.
      // If we get three errors in a row then close the popup.  Decrement the
      // error count by 1/10 if there are no errors, thus allowing up to 1 in
      // 10 calls to cause an error.
      if (portalValidPopup(po) && anyEmsgG > prev_anyEmsgG) {
         po->pup.filterErrors += 10;
         if (po->pup.filterErrors >= 30)
            popup_close_with_retval(po, -1);
         res = FALSE;
      } else {
         if (portalValidPopup(po) && po->pup.filterErrors > 0)
            --po->pup.filterErrors;
         res = tv_get_bool(&returnVar);
      }
   }

   eeglFree(argv[1].string);
   clearVar(&returnVar);
   return res;
}

//Called when "c" was typed: invoke popup filter callbacks. Return TRUE when the character was 
//consumed
int
popup_do_filter(Unt c) {
   static Boole recursive = false;
   int      res = FALSE;
   Portal   *po;
   int      save_KeyTyped = KeyTyped;
   int      state;
   int      was_must_redraw = must_redraw;

   // Popup portal with terminal always gets focus.
   if (popup_is_popup(curPor) && curBook->term != NULL)
      return FALSE;

   if (recursive)
      return false;
   recursive = true;

   if (c == K_LEFTMOUSE) {
      int row = mouseRowG;
      int col = mouseColG;

      po = mouseFindPortal(&row, &col, FIND_POPUP);
      if (po != NULL && popup_close_if_on_X(po, row, col))
        res = TRUE;
   }

   popup_reset_handled(POPUP_HANDLED_2);
   state = get_real_state();
   while (res == FALSE && (po = find_next_popup(FALSE, POPUP_HANDLED_2)) != NULL) {
      if (po->pup.filterCb.name != NULL && (po->pup.filterMode & state) != 0)
         res = invoke_popup_filter(po, c);
   } 

   // when Ctrl-C and no popup has been processed (res is still FALSE)
   // Try to find and close a popup that has no filter callback
   if (c == Ctrl_C && res == FALSE) {
      popup_reset_handled(POPUP_HANDLED_2);
      po = find_next_popup(FALSE, POPUP_HANDLED_2);
      if (po != NULL) {
         popup_close_with_retval(po, -1);
         res = TRUE;
      }
   }


   if (must_redraw > was_must_redraw) {
      int save_gotInterruptG = gotInterruptG;

      // Reset gotInterruptG to avoid a function used in the statusline aborts.
      gotInterruptG = FALSE;
      redraw_after_callback(FALSE, FALSE);
      gotInterruptG |= save_gotInterruptG;
   }
   recursive = false;
   KeyTyped = save_KeyTyped;

   // When interrupted return FALSE to avoid looping.
   return res == -1 ? FALSE : res;
}

//Return TRUE if there is a popup visible with a filter callback and the "mapping" property off.
int
popup_no_mapping(void) {
   Portal   *po;

   for (int round = 1; round <= 2; ++round) {
      for (po = round == 1 ? firstPopupPortG : curtab->firstPopupPort; po; po = po->next) {
         if (po->pup.filterCb.name != NULL 
               && (po->pup.flags & (POPF_HIDDEN | POPF_MAPPING)) == 0
         )
            return TRUE;
      } 
   } 
   return FALSE;
}

// When the cursor moved: check if any popup needs to be closed if the cursor moved far enough
void
popup_check_cursor_pos(void) {
   Portal *po;

   popup_reset_handled(POPUP_HANDLED_3);
   while ((po = find_next_popup(TRUE, POPUP_HANDLED_3)) != NULL) {
      if (po->pup.curPor != NULL
            && (curPor != po->pup.curPor
                || curPor->cursor.lnum != po->pup.lnum
                || curPor->cursor.col < po->pup.minCol
                || curPor->cursor.col > po->pup.maxCol)
      )
         popup_close_with_retval(po, -1);
   } 
}

// Update "maskCells".
private void
popup_update_mask(Portal *po, int width, int height) {
   ListItem   *lio, *li;
   Byte   *cells;
   int      row, col;

   if (po->pup.mask == NULL || width == 0 || height == 0) {
      EE_CLEAR(po->pup.maskCells);
      return;
   }
   if (po->pup.maskCells && po->pup.maskHeight == height && po->pup.maskWidth == width) {
      return;  // cache is still valid
   } 

   eeglFree(po->pup.maskCells);
   po->pup.maskCells = allocZeroed((Unt)width * height);
   if (po->pup.maskCells == NULL)
      return;
   cells = po->pup.maskCells;

   FOR_ALL_LIST_ITEMS(po->pup.mask, lio) {
      int cols, cole;
      int lines, linee;

      li = lio->c.list->first;
      cols = tv_get_number(&li->c);
      if (cols < 0)
          cols = width + cols + 1;
      if (cols <= 0)
          cols = 1;
      li = li->next;
      cole = tv_get_number(&li->c);
      if (cole < 0)
          cole = width + cole + 1;
      if (cole > width)
          cole = width;
      li = li->next;
      lines = tv_get_number(&li->c);
      if (lines < 0)
          lines = height + lines + 1;
      if (lines <= 0)
          lines = 1;
      li = li->next;
      linee = tv_get_number(&li->c);
      if (linee < 0)
          linee = height + linee + 1;
      if (linee > height)
          linee = height;

      for (row = lines - 1; row < linee; ++row)
         for (col = cols - 1; col < cole; ++col)
            cells[row * width + col] = 1;
   }
}

//Return TRUE if "col" / "line" matches with an entry in pup.mask.
//"col" and "line" are screen coordinates.
private int
popupMaskGed(Portal *po, int width, int height, int screencol, int screenline) {
   int col = screencol - po->portalCol + po->pup.leftOff;
   int line = screenline - po->portalRow;

   return col >= 0 && col < width
       && line >= 0 && line < height
       && po->pup.maskCells[line * width + col];
}

//Set flags in popupTransparencyG[] for portal "po" to "val".
private void
update_popupTransparencyG(Portal *po, int val) {
   if (po->pup.mask == NULL)
      return;

   int      width = popup_width(po);
   int      height = popup_height(po);
   ListItem   *lio, *li;
   int      cols, cole;
   int      lines, linee;
   int      col, line;

   FOR_ALL_LIST_ITEMS(po->pup.mask, lio) {
      li = lio->c.list->first;
      cols = tv_get_number(&li->c);
      if (cols < 0)
          cols = width + cols + 1;
      li = li->next;
      cole = tv_get_number(&li->c);
      if (cole < 0)
          cole = width + cole + 1;
      li = li->next;
      lines = tv_get_number(&li->c);
      if (lines < 0)
          lines = height + lines + 1;
      li = li->next;
      linee = tv_get_number(&li->c);
      if (linee < 0)
          linee = height + linee + 1;

      --cols;
      cols -= po->pup.leftOff;
      if (cols < 0)
          cols = 0;
      cole -= po->pup.leftOff;
      --lines;
      if (lines < 0)
          lines = 0;
      for (line = lines; line < linee && line + po->portalRow < screenLinesRowsG; ++line) {
         for (col = cols; col < cole && col + po->portalCol < screenLinesColsG; ++col) {
            popupTransparencyG[(line + po->portalRow) * screenLinesColsG + col + po->portalCol] 
               = val;
         } 
      } 
   }
}

//Only called when popup portal "po" is hidden: If the portal is positioned next to a text property,
//and it is now visible, then  unhide the popup. We don't check if visible popups become hidden, 
//that is done in adjustPosition(). Return TRUE if the popup became unhidden.
private int
check_popup_unhidden(Portal *po) {
   if (po->pup.propType > 0 && portalIsValid(po->pup.propPort)) {
      TextProp  prop;
      LineNr    lnum;

      if ((po->pup.flags & POPF_HIDDEN_FORCE) == 0
         && find_visible_prop(
               po->pup.propPort, po->pup.propType, po->pup.propId, &prop, &lnum
            ) == OK
      ){
         po->pup.flags &= ~POPF_HIDDEN;
         po->pup.propTopline = 0; // force repositioning
         return TRUE;
      }
   }
   return FALSE;
}

//Return TRUE if adjustPosition() needs to be called for "po".
//That is when the book in the popup was changed, or the popup is following a textprop and the 
//referenced book was changed. Or when the cursor line changed and "cursorline" is set.
private inline int
popup_need_position_adjust(Portal *po) {
   if (po->pup.lastChangedTick != CHANGEDTICK(po->book))
      return TRUE;
   if (portalIsValid(po->pup.propPort)
          && (po->pup.propChangedTick != CHANGEDTICK(po->pup.propPort->book)
             || po->pup.propTopline != po->pup.propPort->topLine))
      return TRUE;

   // May need to adjust the width if the cursor moved.
   return po->cursor.lnum != po->pup.lastCurline;
}

//Update "popupMaskG" if needed. Also recompute the popup size and positions.
//Also update "popup_visible" and "popup_uses_mouse_move". Also marks portal lines for redrawing.
void
may_update_popup_mask(int type) {
   Portal   *po;
   Arr(Short) mask;
   int      line, col;
   int      redraw_all_popups = FALSE;
   Boole redrawingAllPortals;

   // Need to recompute when switching tabs. Also recompute when the type is UPD_CLEAR or 
   // UPD_NOT_VALID, something basic (such as the screen size) must have changed.
   if (popupMaskTabS != curtab || type >= UPD_NOT_VALID) {
      needRefreshPopupMaskG = TRUE;
      redraw_all_popups = TRUE;
   }

   // Check if any popup portal book has changed and if any popup connected
   // to a text property has become visible.
   FOR_ALL_POPUPPORTS(po) {
      if (po->pup.flags & POPF_HIDDEN)
         needRefreshPopupMaskG |= check_popup_unhidden(po);
      ei (popup_need_position_adjust(po))
         needRefreshPopupMaskG = TRUE;
   } 
   FOR_ALL_POPUPPORTS_IN_TAB(curtab, po) {
      if (po->pup.flags & POPF_HIDDEN)
         needRefreshPopupMaskG |= check_popup_unhidden(po);
      ei (popup_need_position_adjust(po))
         needRefreshPopupMaskG = TRUE;
   } 

   if (!needRefreshPopupMaskG)
      return;

   // Need to update the mask, something has changed.
   needRefreshPopupMaskG = FALSE;
   popupMaskTabS = curtab;
   popup_visible = FALSE;

   // If redrawing all portals, just update "popupMaskG".
   // If redrawing only what is needed, update "popupMaskNextG" and then
   // compare with "popupMaskG" to see what changed.
   redrawingAllPortals = true;
   FOR_ALL_PORTALS(po) {
      if (po->redrawType < UPD_SOME_VALID)
         redrawingAllPortals = false;
   } 
   if (redrawingAllPortals)
      mask = popupMaskG;
   else
      mask = popupMaskNextG;
   memset(mask, 0, (Unt)screenLinesRowsG * screenLinesColsG * sizeof(short));

   // Find the portal with the lowest zindex that hasn't been handled yet,
   // so that the portal with a higher zindex overwrites the value in popupMaskG.
   popup_reset_handled(POPUP_HANDLED_4);
   while ((po = find_next_popup(TRUE, POPUP_HANDLED_4)) != NULL) {
      int width;
      int height;

      popup_visible = TRUE;

      // Recompute the position if the text changed. It may make the popup
      // hidden if it's attach to a text property that is no longer visible.
      if (redraw_all_popups || popup_need_position_adjust(po)) {
         adjustPosition(po);
         if (po->pup.flags & POPF_HIDDEN)
            continue;
      }

      width = popup_width(po);
      height = popup_height(po);
      popup_update_mask(po, width, height);
      for (line = po->portalRow;
         line < po->portalRow + height && line < screenLinesRowsG; ++line)
         for (col = po->portalCol;
          col < po->portalCol + width - po->pup.leftOff && col < screenLinesColsG; 
          ++col
         ) {
            if (po->pup.zIndex < POPUPMENU_ZINDEX
                  && pum_visible()
                  && pum_under_menu(line, col, FALSE))
               mask[line * screenLinesColsG + col] = POPUPMENU_ZINDEX;
            ei (po->pup.maskCells == NULL || !popupMaskGed(po, width, height, col, line))
               mask[line * screenLinesColsG + col] = po->pup.zIndex;
         } 
   }

   // Only check which lines are to be updated if not already updating all lines.
   if (mask == popupMaskNextG) {
      int       *plines_cache = ALLOC_CLEAR_MULT(int, visibleRowsG);
      Portal       *prev_wp = NULL;

      for (line = 0; line < screenLinesRowsG; ++line) {
         int       col_done = 0;

         for (col = 0; col < screenLinesColsG; ++col) {
            int off = line * screenLinesColsG + col;

            if (popupMaskG[off] != popupMaskNextG[off]) {
               popupMaskG[off] = popupMaskNextG[off];

               if (line >= (int)commlineRowG) {
                  // the command line needs to be cleared if text below the popup is now visible.
                  if (!msg_scrolled && popupMaskNextG[off] == 0)
                      mustClearCommlineG = TRUE;
               } ei (col >= col_done) {
                  LineNr   lnum;
                  int      line_cp = line;
                  int      col_cp = col;

                  // The screen position "line" / "col" needs to be redrawn.  Figure out what portal that 
                  // is and update redrawTop and redrawBott.  Only needs to be done once for each 
                  // portal line.
                  po = mouseFindPortal(&line_cp, &col_cp, IGNORE_POPUP);
                  if (po != NULL) {
                      // A terminal portal needs to be redrawn.
                     if (bt_terminal(po->book))
                        redrawPortLater(po, UPD_NOT_VALID);
                     else {
                        if (po != prev_wp) {
                           memset(plines_cache, 0, sizeof(int) * visibleRowsG);
                           prev_wp = po;
                        }

                        if (line_cp >= (int)po->height)
                           // In (or below) status line
                           po->statusLineNeedsRedraw = TRUE;
                        else {
                           // compute position in the book line from the position in the portal
                           (void)mouse_comp_pos(po, &line_cp, &col_cp, &lnum, plines_cache);
                           drawPortLineLater(po, lnum);
                        }
                     }

                     // This line is going to be redrawn, no need to check until the right 
                     // side of the portal
                     col_done = po->portalCol + po->width - 1;
                  }
               }
            }
         }
      }

      eeglFree(plines_cache);
   }

   update_popup_uses_mouse_move();
}

//If the current portal is a popup and something relevant changed, recompute the position and size
void
may_update_popup_position(void) {
   if (popup_is_popup(curPor) && popup_need_position_adjust(curPor))
      adjustPosition(curPor);
}

//Return a string of "len" spaces in IObuff.
private CS
get_spaces(int len) {
    memset(IObuff, ' ', (Unt)len);
    IObuff[len] = ZERO;
   return IObuff;
}

//Update popup portals.  They are drawn on top of normal portals.
//"win_update" is called for each popup portal, lowest zindex first.
void
update_popups(void (*win_update)(Portal *po)) {
   Portal   *po;
   int top_off;
   int left_extra;
   int total_width;
   int total_height;
   int top_padding;
   Decoration popupDeco;
   Decoration borderDeco[4];
   int border_char[8];
   Byte buf[MB_MAXBYTES];
   int row;
   int wincol;
   int padcol = 0;
   int padendcol = 0;
   int i;
   int sb_thumb_top = 0;
   int sb_thumb_height = 0;
   char scrollDeco = 0;
   char thumbFlags = 0;

   // hide the cursor until redrawing is done.
   cursor_off();

   // Find the portal with the lowest zindex that hasn't been updated yet,
   // so that the portal with a higher zindex is drawn later, thus goes on top.
   popup_reset_handled(POPUP_HANDLED_5);
   while ((po = find_next_popup(TRUE, POPUP_HANDLED_5)) != NULL) {
      int title_len = 0;
      int title_wincol;

      //This drawing uses the zindex of the popup portal, so that it's on top of the text but 
      //doesn't draw when another popup with higher zindex is on top of the character.
      screenZindexG = po->pup.zIndex;

      //Set flags in popupTransparencyG[] for masked cells.
      update_popupTransparencyG(po, 1);

      //adjust portalRow and portalCol for border and padding, since
      //win_update() doesn't handle them.
      top_off = popup_top_extra(po);
      left_extra = po->pup.padding[3] + po->pup.border[3] - po->pup.leftOff;
      if (po->portalCol + left_extra < 0)
          left_extra = -po->portalCol;
      po->portalRow += top_off;
      po->portalCol += left_extra;

      // Draw the popup text, unless it's off screen.
      if (po->portalRow < screenLinesRowsG && po->portalCol < screenLinesColsG) {
         // May need to update the "cursorline" highlighting, which may also change "topline"
         if (po->pup.lastCurline != po->cursor.lnum)
            highlightCurrentLine(po);

         win_update(po);

         // move the cursor into the visible lines, otherwise executing
         // commands with win_execute() may cause the text to jump.
         if (po->cursor.lnum < po->topLine)
            po->cursor.lnum = po->topLine;
         ei (po->cursor.lnum >= po->bottomLine)
            po->cursor.lnum = po->bottomLine - 1;
      }

      po->portalRow -= top_off;
      po->portalCol -= left_extra;

      // Add offset for border and padding if not done already.
      if ((po->flags & WFLAG_WCOL_OFF_ADDED) == 0) {
         po->cursorCol += left_extra;
         po->flags |= WFLAG_WCOL_OFF_ADDED;
      }
      if ((po->flags & WFLAG_WROW_OFF_ADDED) == 0) {
         po->cursorRow += top_off;
         po->flags |= WFLAG_WROW_OFF_ADDED;
      }

      total_width = popup_width(po) - po->pup.rightOff;
      total_height = popup_height(po);
      popupDeco = getPortcolorDeco(po);

      if (po->portalRow + total_height > (int)commlineRowG)
         po->pup.flags |= POPF_ON_CMDLINE;
      else
         po->pup.flags &= ~POPF_ON_CMDLINE;

      border_char[0] = border_char[2] = 0x2550;
      border_char[1] = border_char[3] = 0x2551;
      border_char[4] = 0x2554;
      border_char[5] = 0x2557;
      border_char[6] = (po->pup.flags & POPF_RESIZE) ? 0x21f2 : 0x255d;
      border_char[7] = 0x255a;
      for (i = 0; i < 8; ++i) {
         if (po->pup.borderChar[i] != 0)
            border_char[i] = po->pup.borderChar[i];
      } 

      for (i = 0; i < 4; ++i) {
         borderDeco[i] = popupDeco;
         if (po->pup.borderHilite[i] != NULL)
            borderDeco[i] = decosByHiliteName(po->pup.borderHilite[i]);
      }

      // Title goes on top of border or padding.
      title_wincol = po->portalCol + 1;
      if (po->pup.title != NULL) {
          title_len = eeglStrSize(po->pup.title);

         // truncate the title if too long
         if (title_len > total_width - 2) {
            int   title_byte_len = (int)STRLEN(po->pup.title);
            CS title_text = alloc(title_byte_len + 1);

            trunc_string(po->pup.title, title_text, total_width - 2, title_byte_len + 1);
            drawText(
               title_text, po->portalRow, title_wincol,
               po->pup.border[0] > 0 ? borderDeco[0].flags : popupDeco.flags
            );
            eeglFree(title_text);

            title_len = total_width - 2;
         } else {
            drawText(po->pup.title, po->portalRow, title_wincol,
                  po->pup.border[0] > 0 ? borderDeco[0].flags : popupDeco.flags);
         } 
      }

      wincol = po->portalCol - po->pup.leftOff;
      top_padding = po->pup.padding[0];
      if (po->pup.border[0] > 0) {
         // top border; do not draw over the title
         if (title_len > 0) {
            fillRowsWithTwoChars(
               po->portalRow, po->portalRow + 1, wincol < 0 ? 0 : wincol, title_wincol,
               po->pup.border[3] != 0 
                  && po->pup.leftOff == 0 ? border_char[4] : border_char[0],
               border_char[0], borderDeco[0].flags
            );
            fillRowsWithTwoChars(
               po->portalRow, po->portalRow + 1, title_wincol + title_len, wincol + total_width,
               border_char[0], border_char[0], borderDeco[0].flags
            );
         } else {
         fillRowsWithTwoChars(po->portalRow, po->portalRow + 1,
            wincol < 0 ? 0 : wincol, wincol + total_width,
            po->pup.border[3] != 0 && po->pup.leftOff == 0 ? border_char[4] : border_char[0],
            border_char[0], borderDeco[0].flags);
         }
         if (po->pup.border[1] > 0) {
            buf[mb_char2bytes(border_char[5], buf)] = ZERO;
            drawText(buf, po->portalRow, wincol + total_width - 1, borderDeco[1].flags);
         }
      }
      ei (po->pup.padding[0] == 0 && popup_top_extra(po) > 0)
         top_padding = 1;

      if (top_padding > 0 || po->pup.padding[2] > 0) {
         padcol = wincol + po->pup.border[3];
         padendcol = po->portalCol + total_width - po->pup.border[1] - po->pup.hasScrollbar;
         if (padcol < 0) {
            padendcol += padcol;
            padcol = 0;
         }
      }
      if (top_padding > 0) {
         row = po->portalRow + po->pup.border[0];
         if (title_len > 0 && row == po->portalRow) {
            // top padding and no border; do not draw over the title
            fillRowsWithTwoChars(row, row + 1, padcol, title_wincol, ' ', ' ', popupDeco.flags);
            fillRowsWithTwoChars(
               row, row + 1, title_wincol + title_len, padendcol, ' ', ' ', popupDeco.flags
            );
            row += 1;
            top_padding -= 1;
         }
         fillRowsWithTwoChars(row, row + top_padding, padcol, padendcol, ' ', ' ', popupDeco.flags);
      }

      // Compute scrollbar thumb position and size.
      if (po->pup.hasScrollbar) {
         LineNr   linecount = po->book->mem.lineCount;
         int      height = po->height;
         int      last;

         sb_thumb_height = ((LineNr)height * height + linecount / 2) / linecount;
         if (po->topLine > 1 && sb_thumb_height == height)
            --sb_thumb_height;  // scrolled, no full thumb
         if (sb_thumb_height == 0)
            sb_thumb_height = 1;
         if (linecount <= po->height || po->height == 0)
            // it just fits, avoid divide by zero
            sb_thumb_top = 0;
         else
            sb_thumb_top = (po->topLine - 1 + (linecount / po->height) / 2)
                  * (po->height - sb_thumb_height) / (linecount - po->height);
         if (po->topLine > 1 && sb_thumb_top == 0 && height > 1)
            sb_thumb_top = 1;  // show it's scrolled
         last = total_height - top_off - po->pup.border[2];
         if (sb_thumb_top >= last)
            // show at least one character
            sb_thumb_top = last - 1;

         if (po->pup.scrollbarHilite)
            scrollDeco = decosByHiliteName(po->pup.scrollbarHilite).flags;
         else
            scrollDeco = getDecoFlags(HLF_PSB);
         if (po->pup.thumbHilite)
            thumbFlags = decosByHiliteName(po->pup.thumbHilite).flags;
         else
            thumbFlags = getDecoFlags(HLF_PST);
      }

      for (i = po->pup.border[0]; i < total_height - po->pup.border[2]; ++i) {
          int   pad_left;
          // left and right padding only needed next to the body
          int do_padding =
             i >= po->pup.border[0] + po->pup.padding[0]
             && i < total_height - po->pup.border[2] - po->pup.padding[2];

         row = po->portalRow + i;

         // left border
         if (po->pup.border[3] > 0 && wincol >= 0) {
            buf[mb_char2bytes(border_char[3], buf)] = ZERO;
            drawText(buf, row, wincol, borderDeco[3].flags);
         }
         if (do_padding && po->pup.padding[3] > 0) {
            int col = wincol + po->pup.border[3];

            // left padding
            pad_left = po->pup.padding[3];
            if (col < 0) {
               pad_left += col;
               col = 0;
            }
            if (pad_left > 0)
               drawText(get_spaces(pad_left), row, col, popupDeco.flags);
         }
         // scrollbar
         if (po->pup.hasScrollbar) {
            int line = i - top_off;
            int scroll_col = po->portalCol + total_width - 1 - po->pup.border[1];

            if (line >= 0 && line < (int)po->height)
               screen_putchar(
                  ' ', row, scroll_col, 
                  (line >= sb_thumb_top && line < sb_thumb_top + sb_thumb_height)
                     ? thumbFlags : scrollDeco
               );
            else
               screen_putchar(' ', row, scroll_col, popupDeco.flags);
         }
         // right border
         if (po->pup.border[1] > 0) {
            buf[mb_char2bytes(border_char[1], buf)] = ZERO;
            drawText(buf, row, wincol + total_width - 1, borderDeco[1].flags);
         }
         // right padding
         if (do_padding && po->pup.padding[1] > 0)
            drawText(
               get_spaces( po->pup.padding[1]), 
               row, 
               wincol + po->pup.border[3] + po->pup.padding[3] + po->width + po->leftCol,
               popupDeco.flags
            );
      }

      if (po->pup.padding[2] > 0) {
         // bottom padding
         row = po->portalRow + po->pup.border[0] + po->pup.padding[0] + po->height;
         fillRowsWithTwoChars(
            row, row + po->pup.padding[2], padcol, padendcol, ' ', ' ', popupDeco.flags
         );
      }

      if (po->pup.border[2] > 0) {
         // bottom border
         row = po->portalRow + total_height - 1;
         fillRowsWithTwoChars(
             row , row + 1, wincol < 0 ? 0 : wincol, wincol + total_width,
             po->pup.border[3] != 0 && po->pup.leftOff == 0 ? border_char[7] : border_char[2],
             border_char[2], borderDeco[2].flags
         );
         if (po->pup.border[1] > 0) {
            buf[mb_char2bytes(border_char[6], buf)] = ZERO;
            drawText(buf, row, wincol + total_width - 1, borderDeco[2].flags);
         }
      }

      if (po->pup.close == POPCLOSE_BUTTON) {
          // close button goes on top of anything at the top-right corner
          buf[mb_char2bytes('X', buf)] = ZERO;
          drawText(buf, po->portalRow, wincol + total_width - 1,
               po->pup.border[0] > 0 ? borderDeco[0].flags : popupDeco.flags);
      }

      update_popupTransparencyG(po, 0);

      // Back to the normal zindex.
      screenZindexG = 0;

      // if this was the message portal popup may start the timer now
      mayStartMessagePortalTimer(po);
   }

   // In case win_update() called start_search_hl().
   end_search_hl();
}

// Mark references in callbacks of one popup portal.
private int
set_ref_in_one_popup(Portal *po, int copyID) {
   int      abort = FALSE;
   Var   tv;

   if (po->pup.closeCb.cb_partial != NULL) {
      tv.tag = VAR_PARTIAL;
      tv.partial = po->pup.closeCb.cb_partial;
      abort = abort || set_ref_in_item(&tv, copyID, NULL, NULL);
   }
   if (po->pup.filterCb.cb_partial != NULL) {
      tv.tag = VAR_PARTIAL;
      tv.partial = po->pup.filterCb.cb_partial;
      abort = abort || set_ref_in_item(&tv, copyID, NULL, NULL);
   }
    abort = abort || set_ref_in_list(po->pup.mask, copyID);
   return abort;
}

// Set reference in callbacks of popup portals.
int
set_ref_in_popups(int copyID) {
   int      abort = FALSE;
   Portal   *po;
   Tab   *tab;

   for (po = firstPopupPortG; !abort && po; po = po->next)
      abort = abort || set_ref_in_one_popup(po, copyID);

   FOR_ALL_TABS(tab) {
      for (po = tab->firstPopupPort; !abort && po; po = po->next)
          abort = abort || set_ref_in_one_popup(po, copyID);
      if (abort)
          break;
   }
   return abort;
}

int
popup_is_popup(Portal *po) {
   return po->pup.flags != 0;
}

// Find an existing popup used as the preview portal, in the current tab. Return NULL if not found.
Portal *
popupFindPreviewPortal(void) {
   return curtab->previewPortal;
}

// Find an existing popup used as the info portal, in the current tab. Return NULL if not found.
Portal *
popupFindInfoPortal(void) {
   Portal *po;

   // info portal popup is always local to tab
   FOR_ALL_POPUPPORTS_IN_TAB(curtab, po) {
      if (po->pup.flags & POPF_INFO)
          return po;
   } 
   return NULL;
}

void
f_popup_findecho(Var *argvars UNUSED, OUT Var *returnVar) {
   returnVar->number = messagePort == NULL ? 0 : messagePort->id;
}

void
f_popup_findinfo(Var *argvars UNUSED, OUT Var *returnVar) {
   Portal   *po = popupFindInfoPortal();
   returnVar->number = po == NULL ? 0 : po->id;
}

void
f_popup_findpreview(Var *argvars UNUSED, OUT Var *returnVar) {
   Portal* po = popupFindPreviewPortal();

   returnVar->number = po == NULL ? 0 : po->id;
}

//Create a popup to be used as the preview or info portal.
//NOTE: this makes the popup the current portal, so that the file can be edited.  However it 
//must not remain the current portal, which the caller must make sure of.
int
portalCreatePreviewPortal(int info) {
   Portal* po = createPopup(NULL, NULL, info ? POPUP_INFO : POPUP_PREVIEW);
   if (!po)
      return FAIL;
      
   if (info)
      po->pup.flags |= POPF_INFO;
   else
      po->isPreview = true;
   curtab->previewPortal = po;

   // Set the width to a reasonable value, so that topLine can be computed.
   if (po->pup.minWidth > 0)
      po->width = po->pup.minWidth;
   ei (po->pup.maxWidth > 0)
      po->width = po->pup.maxWidth;
   else
      po->width = curPor->width;

   // Will switch to another book soon, dummy one can be wiped.
   po->book->locked = FALSE;

   enterPortal(po, FALSE);
   return OK;
}

// Close any preview popup.
void
popup_close_preview(void) {
   Portal *po = popupFindPreviewPortal();
   if (po)
      popup_close_with_retval(po, -1);
}

// Hide the info popup.
private void
popup_hide_info(void) {
   Portal *po = popupFindInfoPortal();
   if (po) {
      popup_on_cmdline = po->pup.flags & POPF_ON_CMDLINE;
      popup_hide(po);
   }
}

// Close any info popup.
void
popup_close_info(void) {
   Portal *po = popupFindInfoPortal();
   if (po)
      popup_close_with_retval(po, -1);
}

//Return TRUE if a popup extends into the cmdline area.
int
popup_overlaps_cmdline(void) {
   return popup_on_cmdline;
}


//Get the message portal. Return NULL if something failed.
Portal*
popup_get_messagePort(void) {
   if (messagePort != NULL)
      return messagePort;

   messagePort = createPopup(NULL, NULL, POPUP_MESSAGE_WIN);
   if (!messagePort)
      return NULL;

   // use the full screen width
   messagePort->width = visibleColsG;

   // position at bottom of screen
   messagePort->pup.pos = POPPOS_BOTTOM;
   messagePort->pup.wantCol = 1;
   messagePort->pup.minWidth = 9999;
   messagePort->pup.firstLine = -1;

   // no padding, border at the top
   for (int i = 0; i < 4; ++i)
      messagePort->pup.padding[i] = 0;
   for (int i = 1; i < 4; ++i)
      messagePort->pup.border[i] = 0;

   if (messagePort->pup.timer != NULL)
      messagePort->pup.timer->tr_keep = TRUE;
   return messagePort;
}

//If the message portal is not visible: show it
//If the message portal is visible: reset the timeout
void
popup_show_messagePort(void) {
   if (!messagePort)
      return;

   if ((messagePort->pup.flags & POPF_HIDDEN) != 0) {
      // the highlight may have changed.
      updateNotificationColor(messagePort, POPUP_MESSAGE_WIN);
      popup_show(messagePort);
   }
   start_message_win_timer = TRUE;
}

private void
mayStartMessagePortalTimer(Portal *po) {
   if (po == messagePort && start_message_win_timer) {
      if (messagePort->pup.timer != NULL) {
         messagePort->pup.timer->tr_interval = message_win_time;
         timer_start(messagePort->pup.timer);
         message_win_time = 3000;
      }
      start_message_win_timer = FALSE;
   }
}

int
popup_message_win_visible(void) {
   return messagePort && (messagePort->pup.flags & POPF_HIDDEN) == 0;
}

// If the message portal is visible, hide it.
void
popup_hide_messagePort(void) {
   if (messagePort)
      popup_hide(messagePort);
}

// Values saved in start_echowindow() and restored in end_echowindow()
private int save_msg_didout = FALSE;
private int saveMsgCol = 0;
// Values saved in end_echowindow() and restored in start_echowindow()
private int ew_msg_didout = FALSE;
private int echoPortMsgColS = 0;

//Invoked before outputting a message for ":echowindow". "time_sec" is the display time, zero means
//using the default 3 sec.
void
start_echowindow(int time_sec) {
   inEchoPortalG = TRUE;
   save_msg_didout = msg_didout;
   saveMsgCol = msgColG;
   msg_didout = ew_msg_didout;
   msgColG = echoPortMsgColS;
   if (time_sec != 0)
      message_win_time = time_sec * 1000;
}

//Invoked after outputting a message for ":echowindow".
void
end_echowindow(void) {
   inEchoPortalG = FALSE;

   if ((stateG & MODE_HITRETURN) == 0)
      // show the message portal now
      redraw_cmd(FALSE);

   // do not overwrite messages
   ew_msg_didout = TRUE;
   echoPortMsgColS = msgColG == 0 ? 1 : msgColG;
   msg_didout = save_msg_didout;
   msgColG = saveMsgCol;
}

//Close any popup for a text property associated with "port". Return TRUE if a popup was closed.
private int
popup_closePortal(Portal* port) {
   int       ret = FALSE;

   for (int round = 1; round <= 2; ++round) {
      Portal   *next;
      for (Portal* po = round == 1 ? firstPopupPortG : curtab->firstPopupPort; po; po = next) {
         next = po->next;
         if (po->pup.propPort == port) {
            popup_close_with_retval(po, -1);
            ret = TRUE;
         }
      }
   } 
   return ret;
}

// Set the title of the popup portal to the file name.
void
setPopupTitle(Portal *po) {
   if (po->book->currFileName == NULL)
      return;

   Byte   dirname[MAXPATHL];

   mch_dirname(dirname, MAXPATHL);
   shorten_buf_fname(po->book, dirname, FALSE);

   eeglFree(po->pup.title);
   Unt len = STRLEN(po->book->currFileName) + 3;
   po->pup.title = alloc((int)len);
   eeSnprintf(po->pup.title, len, " %s ", po->book->currFileName);
   redrawPortLater(po, UPD_VALID);
}

// If there is a preview popup, update the title. Used after changing directory.
void
popup_update_preview_title(void) {
   Portal *port = popupFindPreviewPortal();
   if (port)
      setPopupTitle(port);
}

// Show a popup notification (like a toast) with a timeout.
void
showNotification(CS text) {
   Var vars[2];
   vars[0] = *allocStringVar(text);
   Bag* emptyBag = allocBag();
   Var dictVar = (Var){.tag = VAR_BAG, .bag = emptyBag};
   vars[1] = dictVar;
   createPopup(vars, NULL, POPUP_MESSAGE_WIN);
}

Boole canStartDrag(Portal* po, int row, int col) {
   return ((po->pup.flags & (POPF_DRAG | POPF_RESIZE)) && popup_on_border(po, row, col))
                      || (po->pup.flags & POPF_DRAGALL);
}

Boole isInfoPopup(Portal* po) {
   return (po->pup.flags & POPF_INFO) > 0;
}

//{{{pop-up menus (pum)

private Arr(PopupItem) displayedItemsS = NULL;   // items of displayed popup menu
private Unt menuLen;         // nr of items in "displayedItemsS"
private int selectedItemInd;      // index of selected item or -1
private int firstItemIndS = 0;      // index of top item

private Boole callUpdateScreen = false;
private int pum_in_cmdline = FALSE;

private int pum_height;         // nr of displayed pum items
private int pum_width;         // width of displayed pum items
private int pum_base_width;      // width of pum items base
private int pum_kind_width;      // width of pum items kind column
private int pum_extra_width;      // width of extra stuff
private int pum_scrollbar;      // TRUE when scrollbar present

private int pum_row;         // top row of pum
private int pum_col;         // left column of pum

private Portal *pumPort = NULL;
private Unt pumRow;
private Unt pumHeight;
private Unt pumCol;
private Unt pumWCol;
private Unt pumWWidth;

// Some parts are not updated when a popup menu is visible.  Setting this flag
// makes pum_visible() return FALSE even when there is a popup menu.
private int pum_pretend_not_visible = FALSE;

private int pum_set_selected(int n, int repeat);

#define PUM_DEF_HEIGHT 10

private void
computeSize(void) {
   int   w;

   // Compute the width of the widest match and the widest extra.
   pum_base_width = 0;
   pum_kind_width = 0;
   pum_extra_width = 0;
   for (Unt i = 0; i < menuLen; ++i) {
      if (displayedItemsS[i].pum_text != NULL) {
          w = eeglStrSize(displayedItemsS[i].pum_text);
          if (pum_base_width < w)
         pum_base_width = w;
      }
      if (displayedItemsS[i].pum_kind != NULL) {
         w = eeglStrSize(displayedItemsS[i].pum_kind) + 1;
         if (pum_kind_width < w)
            pum_kind_width = w;
      }
      if (displayedItemsS[i].pum_extra != NULL) {
         w = eeglStrSize(displayedItemsS[i].pum_extra) + 1;
         if (pum_extra_width < w)
            pum_extra_width = w;
      }
   }
}

//Show the popup menu with items "array[size]". "array" must remain valid until pum_undisplay() is 
//called! When possible the leftmost character is aligned with cursor column. The menu appears 
//above the screen line "row" or at "row" + "height" - 1.
void
pum_display(
   PopupItem   *array,
   Unt size,
   int selected   // index of initially selected item, none if out of range
){
   int def_width;
   int max_width;
   int context_lines;
   int cursor_col;
   int above_row;
   int below_row;
   int cline_visible_offset;
   int content_width;
   int right_edge_col;
   int redo_count = 0;
   Portal* pvPort;

   do {
      def_width = p_pw;
      if (p_pmw > 0 && def_width > p_pmw)
         def_width = p_pmw;
      above_row = 0;
      below_row = commlineRowG;

      // Pretend the pum is already there to avoid that must_redraw is set when 'cuc' is on.
      displayedItemsS = (PopupItem *)1;
      validate_cursor_col();
      displayedItemsS = NULL;

      // Remember the essential parts of the portal position and size, so we
      // can decide when to reposition the popup menu.
      pumPort = curPor;
      if (stateG & MODE_COMMLINE)
         // cmdline completion popup menu
         pumRow = commlineRowG;
      else
         pumRow = curPor->cursorRow + curPor->portalRow;
      pumHeight = curPor->height;
      pumCol = curPor->portalCol;
      pumWCol = curPor->cursorCol;
      pumWWidth = curPor->width;

      FOR_ALL_PORTALS(pvPort) {
         if (pvPort->isPreview)
            break;
      } 
      if (pvPort) {
         if ((int)pvPort->portalRow < curPor->portalRow)
            above_row = pvPort->portalRow + pvPort->height;
         ei (pvPort->portalRow > (int)(curPor->portalRow + curPor->height))
            below_row = pvPort->portalRow;
      }

      //Figure out the size and position of the pum.
      pum_height = MIN(size, PUM_DEF_HEIGHT);
      if (p_ph > 0 && pum_height > p_ph)
          pum_height = p_ph;

      // Put the pum below "pumRow" if possible.  If there are few lines
      // decide on where there is more room.
      if ((int)pumRow + 2 >= below_row - pum_height
            && (int)pumRow - above_row > (below_row - above_row) / 2
      ) {
         // pum above "pumRow"
         if (stateG & MODE_COMMLINE)
            // for cmdline pum, no need for context lines
            context_lines = 0;
         else
            // Leave two lines of context if possible
            context_lines = MIN(2, curPor->cursorRow - curPor->cursorLineRow);

         if (pumRow >= size + context_lines) {
            pum_row = pumRow - size - context_lines;
            pum_height = size;
         } else {
            pum_row = 0;
            pum_height = pumRow - context_lines;
         }
         if (p_ph > 0 && pum_height > p_ph) {
            pum_row += pum_height - p_ph;
            pum_height = p_ph;
         }
      } else {
         // pum below "pumRow"
         if (stateG & MODE_COMMLINE)
            // for cmdline pum, no need for context lines
            context_lines = 0;
         else {
            // Leave three lines of context if possible
            validate_cheight();
            cline_visible_offset = curPor->cursorLineRow
                      + curPor->cursorLineHeight - curPor->cursorRow;
            context_lines = MIN(3, cline_visible_offset);
         }

         pum_row = pumRow + context_lines;
         pum_height = MIN(below_row - pum_row, (int)size);
         if (p_ph > 0 && pum_height > p_ph)
            pum_height = p_ph;
      }

      // don't display when we only have room for one line
      if (pum_height < 1 || (pum_height == 1 && size > 1))
         return;

      // If there is a preview portal above, avoid drawing over it.
      if (pvPort && pum_row < above_row && pum_height > above_row) {
         pum_row = above_row;
         pum_height = pumRow - above_row;
      }

      displayedItemsS = array;
      menuLen = size;
      computeSize();
      max_width = pum_base_width;
      if (p_pmw > 0 && max_width > p_pmw)
          max_width = p_pmw;

      // Calculate column
      if (stateG & MODE_COMMLINE)
         // cmdline completion popup menu
         cursor_col = cmdline_compl_startcol();
      else {
         // cursorCol includes virtual text "above"
         int wcol = curPor->cursorCol % curPor->width;
         cursor_col = curPor->portalCol + wcol;
      }

      // if there are more items than room we need a scrollbar
      if (pum_height < (int)size) {
         pum_scrollbar = 1;
         ++max_width;
      } else
         pum_scrollbar = 0;

      if (def_width < max_width)
          def_width = max_width;

      if (((cursor_col < visibleColsG - p_pw || cursor_col < visibleColsG - max_width))) {
         // align pum with "cursor_col"
         pum_col = cursor_col;

         // start with the maximum space available
         pum_width = visibleColsG - pum_col - pum_scrollbar;

         content_width = max_width + pum_kind_width + pum_extra_width + 1;
         if (pum_width > content_width && pum_width > p_pw) {
            // Reduce width to fit item
            pum_width = MAX(content_width, p_pw);
            if (p_pmw > 0 && pum_width > p_pmw)
               pum_width = p_pmw;
         } ei (((cursor_col > p_pw || cursor_col > max_width))) {
            // align pum edge with "cursor_col"
            {
               right_edge_col = visibleColsG - max_width - pum_scrollbar;
               if (curPor->portalCol > right_edge_col && max_width <= p_pw)
                  // use full width to end of the screen
                  pum_col = MAX(0, right_edge_col);
            }

            pum_width = visibleColsG - pum_col - pum_scrollbar;

            if (pum_width < p_pw) {
               pum_width = p_pw;
               if (p_pmw > 0 && pum_width > p_pmw)
                  pum_width = p_pmw;
               if (pum_width >= visibleColsG - pum_col)
                  pum_width = visibleColsG - pum_col - 1;
            } ei (pum_width > content_width && pum_width > p_pw) {
               pum_width = MAX(content_width, p_pw);
               if (p_pmw > 0 && pum_width > p_pmw)
                  pum_width = p_pmw;
            } ei (p_pmw > 0 && pum_width > p_pmw) {
               pum_width = p_pmw;
            }
         }

      } ei (visibleColsG < def_width) {
         // not enough room, will use what we have
         pum_col = 0;
         pum_width = visibleColsG - 1;
         if (p_pmw > 0 && pum_width > p_pmw)
            pum_width = p_pmw;
      } else {
         if (max_width > p_pw)
            max_width = p_pw;   // truncate
         if (p_pmw > 0 && max_width > p_pmw)
            max_width = p_pmw;
         pum_col = visibleColsG - max_width;
         pum_width = max_width - pum_scrollbar;
      }

      // Set selected item and redraw.  If the window size changed need to
      // redo the positioning.  Limit this to two times, when there is not
      // much room the window size will keep changing.
   } while (pum_set_selected(selected, redo_count) && ++redo_count <= 2);

    pum_redraw();
}

//Set a flag that when pum_redraw() is called it first calls drawUpdateScreen().
//This will avoid clearing and redrawing the popup menu, prevent flicker.
void
pum_callUpdateScreen(void) {
   callUpdateScreen = TRUE;

   // Update the cursor position to be able to compute the popup menu
   // position.  The cursor line length may have changed because of the
   // inserted completion.
   curPor->cacheState &= ~(VALID_CROW|VALID_CHEIGHT);
   validate_cursor();
}

//Return TRUE if we are going to redraw the popup menu and the screen position
//"row"/"col" is under the popup menu.
int
pum_under_menu(int row, int col, int only_redrawing) {
   return (!only_redrawing || pum_will_redraw)
       && row >= pum_row
       && row < pum_row + pum_height
       && col >= pum_col - 1
       && col < pum_col + pum_width + pum_scrollbar;
}

// Computes decorations of text on the popup menu.
// Return decorations for every cell, or NULL if all decorations are the same.
private Arr(Decoration)
computeTextDeco(Byte* text, Short hiId, Decoration userDeco) {
   if (*text == ZERO || (hiId != HLF_PSI && hiId != HLF_PNI)
          || (getDecoFlags(HLF_PMSI) == getDecoFlags(HLF_PSI)
              && getDecoFlags(HLF_PMNI) == getDecoFlags(HLF_PNI)))
      return NULL;

   Boole isSelect = hiId == HLF_PSI;
   Byte* leader = (stateG & MODE_COMMLINE) ? cmdline_compl_pattern() : ins_compl_leader();
   if (!leader || *leader == ZERO)
      return NULL;

   Arr(Decoration) decos = ALLOC_MULT(Decoration, eeglStrSize(text));
   if (!decos)
      return NULL;

   Boole inFuzzy = (stateG & MODE_COMMLINE) 
      ? cmdline_compl_is_fuzzy() : (curBook->o.completeOpt & COT_FUZZY) != 0;
   Unt leaderLen = STRLEN(leader);

   ArrayList* ga = NULL;
   if (inFuzzy)
      ga = fuzzyMatchStr_with_pos(text, leader);

   Decoration newDeco;
   int cellIdx = 0;
   int      matchedLen = -1;
   Unt   char_pos = 0;
   Byte   *ptr = text;
   while (*ptr != ZERO) {
      newDeco = decorationsG[hiId];

      if (ga) {
         // Handle fuzzy matching
         for (int i = 0; i < ga->len; i++) {
            if (char_pos == ((Unt *)ga->c)[i]) {
               newDeco = decorationsG[isSelect ? HLF_PMSI : HLF_PMNI];
               newDeco = combineDecorations(decorationsG[hiId], newDeco);
               break;
            }
         }
      } else {
         if (matchedLen < 0 && MB_STRNICMP(ptr, leader, leaderLen) == 0)
            matchedLen = (int)leaderLen;
         if (matchedLen > 0) {
            newDeco = decorationsG[isSelect ? HLF_PMSI : HLF_PMNI];
            newDeco = combineDecorations(decorationsG[hiId], newDeco);
            matchedLen--;
         }
      }

      newDeco = combineDecorations(getFullDecoration(HLF_PNI), newDeco);
      if (userDeco.hiId != SHORT)
         newDeco = combineDecorations(newDeco, userDeco);

      int charCells = mb_ptr2cells(ptr);
      for (int i = 0; i < charCells; i++)
         decos[cellIdx + i] = newDeco;
      cellIdx += charCells;

      MB_PTR_ADV(ptr);
      char_pos++;
   }

   if (ga) {
      ga_clear(ga);
      eeglFree(ga);
   }
   return decos;
}

// Display text on the popup menu with specific decorations
private void
pum_drawText_withDecos(
   int      row,
   int      col,
   int      cells UNUSED,
   Byte   *text,
   int      textlen,
   Arr(Decoration) decos)
{
   int      col_start = col;
   Byte   *ptr = text;
   int      char_len;
   // Render text with proper decorations
   while (*ptr != ZERO && ptr < text + textlen) {
      char_len = utfCharLen(ptr);
      drawTextLen(ptr, char_len, row, col, decos[col - col_start].flags);
      col += mb_ptr2cells(ptr);
      ptr += char_len;
   }
}

private inline void
pum_align_order(int *order) {
    int is_default = cia_flags == 0;
    order[0] = is_default ? CPT_ABBR : cia_flags / 100;
    order[1] = is_default ? CPT_KIND : (cia_flags / 10) % 10;
    order[2] = is_default ? CPT_MENU : cia_flags % 10;
}

private inline CS
pum_get_item(int index, int type) {
   switch(type) {
   case CPT_ABBR: return displayedItemsS[index].pum_text;
   case CPT_KIND: return displayedItemsS[index].pum_kind;
   case CPT_MENU: return displayedItemsS[index].pum_extra;
   }
   return NULL;
}

private inline Decoration
combineUserDecos(int idx, int type, Decoration defaultDeco) {
   Decoration userDecos[] = {
      displayedItemsS[idx].abbreviationDeco,
      displayedItemsS[idx].kindDeco
   };

   return userDecos[type].hiId != SHORT 
      ? combineDecorations(defaultDeco, userDecos[type])
      : defaultDeco;
}

// Display text with proper decorations in the popup menu.
// Return the adjusted column position after drawing.
private int
displayText(
   int row,
   int col,
   Arr(Byte) text,
   char decoFlags,
   Arr(Decoration) decos,
   int width,        // width already calculated in outer loop
   int widthLimit,
   int totwidth,
   int next_isempty,
   int selected)
{
   Byte  *st_end = NULL;
   int over_cell = 0;
   int pad = next_isempty ? 0 : 2;
   int truncated = FALSE;
   int remaining = 0;
   char truncDeco = decorationsG[selected ? HLF_PSI : HLF_PNI].flags;
   Unt trunc = fillCharsG.trunc != ZERO ? fillCharsG.trunc : '>';

   if (!text)
      return col;

   int size = (int)STRLEN(text);
   int cells = mb_string2cells(text, size);
   truncated = widthLimit == p_pmw && widthLimit - totwidth < cells + pad;

   // only draw the text that fits
   while (size > 0 && col + cells > widthLimit + pum_col) {
      --size;
      size -= mb_head_off(text, text + size);
      cells -= mb_ptr2cells(text + size);
   }

   // truncated
   if (truncated) {
      remaining = widthLimit - totwidth - 1;
      if (cells > remaining) {
         st_end = text + size;
         while (st_end > text && cells > remaining) {
            MB_PTR_BACK(text, st_end);
            cells -= mb_ptr2cells(st_end);
         }
         size = st_end - text;
      }

      if (cells < remaining)
         over_cell = remaining - cells;
      cells = mb_string2cells(text, size);
      width = cells + over_cell + 1;
   }

   if (!decos)
      drawTextLen(text, size, row, col, decoFlags);
   else
      pum_drawText_withDecos(row, col, cells, text, size, decos);

   if (truncated) {
      if (over_cell > 0) {
         fillRowsWithTwoChars(
            row, row + 1, col + cells, col + cells + over_cell, ' ', ' ', decoFlags
         );
      } 

      screen_putchar(trunc, row, col + cells + over_cell, truncDeco);
   }

   EE_CLEAR(text);
   return col + width;
}


//Process and display a single popup menu item (text/kind/extra).
//Return the new column position after drawing.
private int
drawMenuItem(
   int row,
   int col,
   int idx,
   int j,         // Current position in order array
   int* order,    // Order array
   Short hiId,
   char decoFlags,
   int* totwidth_ptr,
   int next_isempty
){
   int item_type = order[j];
   Byte* s = NULL;
   Byte* p = pum_get_item(idx, item_type);
   int width = 0; // item width
   int w;         // char width
   int selected = idx == selectedItemInd;

   for ( ; ; MB_PTR_ADV(p)) {
      if (!s)
         s = p;
      w = ptr2cells(p);
      if (*p != ZERO && *p != TAB && *totwidth_ptr + w <= pum_width) {
         width += w;
         continue;
      }

      // Display the text that fits or comes before a Tab. First convert it to printable characters
      Arr(Decoration) decos = NULL;
      int saved = *p;

      if (saved != ZERO)
         *p = ZERO;
      Byte* sanitizedText = sanitizeStr(s);
      if (saved != ZERO)
         *p = saved;

      if (item_type == CPT_ABBR)
         decos = computeTextDeco(sanitizedText, hiId, displayedItemsS[idx].abbreviationDeco);
      col = displayText(
            row, col, sanitizedText, decoFlags, decos, width, pum_width, *totwidth_ptr, next_isempty, 
            selected
      );

      if (decos)
         EE_CLEAR(decos);

      if (*p != TAB)
          break;

      // Display two spaces for a Tab.
      drawTextLen((CS)"  ", 2, row, col, decoFlags);
      col += 2;
      *totwidth_ptr += 2;
      s = NULL;  // start text at next char
      width = 0;
   }

   return col;
}

// Draw the scrollbar for the popup menu.
private void
pum_draw_scrollbar(
   int row,
   int i,
   int thumb_pos,
   int thumb_height
){
   if (pum_scrollbar <= 0)
      return;
   char deco = (i >= thumb_pos && i < thumb_pos + thumb_height) ?
         getDecoFlags(HLF_PST) : getDecoFlags(HLF_PSB);
   screen_putchar(' ', row, pum_col + pum_width, deco);
}

// Redraw the popup menu, using "firstItemIndS" and "selectedItemInd".
void
pum_redraw(void) {
   int row = pum_row;
   int col;
   Arr(Short) hiIds; // array used for highlights
   Short hiId;
   Decoration deco;
   int i, j;
   int idx;
   Byte* p = NULL;
   int totwidth;
   int thumb_pos = 0;
   int thumb_height = 1;
   int item_type;
   int order[3];
   int next_isempty = FALSE;
   int n;
   int items_width_array[3] = { pum_base_width, pum_kind_width, pum_extra_width };
   int basic_width;  // first item width
   int last_isabbr = FALSE;
   Decoration origDeco;
   int scroll_range = menuLen - pum_height;

   Short hisNorm[3]; // hilite ids for normal
   Short hisSel[3];  // hilite ids for selections
   // "word"/"abbr"
   hisNorm[0] = HLF_PNI;
   hisSel[0] = HLF_PSI;
   // "kind"
   hisNorm[1] = HLF_PNK;
   hisSel[1] = HLF_PSK;
   // "extra text"
   hisNorm[2] = HLF_PNX;
   hisSel[2] = HLF_PSX;

   if (callUpdateScreen) {
      callUpdateScreen = FALSE;
      // Do not redraw in pum_may_redraw() and don't draw in the area where the popup menu will be
      pum_will_redraw = TRUE;
      drawUpdateScreen(0);
      pum_will_redraw = FALSE;
   }

   // never display more than we have
   firstItemIndS = MIN(firstItemIndS, scroll_range);

   if (pum_scrollbar) {
      thumb_height = pum_height * pum_height / menuLen;
      if (thumb_height == 0)
         thumb_height = 1;
      thumb_pos = (firstItemIndS * (pum_height - thumb_height) + scroll_range / 2) / scroll_range;
   }

   // The popup menu is drawn over popup menus with zindex under POPUPMENU_ZINDEX.
   screenZindexG = POPUPMENU_ZINDEX;

   for  (i = 0; i < pum_height; ++i) {
      idx = i + firstItemIndS;
      hiIds = (idx == selectedItemInd) ? hisSel : hisNorm;
      hiId = hiIds[0]; // start with "word" highlight
      deco = decorationsG[hiId];

      // prepend a space if there is room
      if (pum_col > 0)
         screen_putchar(' ', row, pum_col - 1, deco.flags);

      // Display each entry, use 2 spaces for a Tab. Do this 3 times and order from p_cia
      col = pum_col;
      totwidth = 0;
      pum_align_order(order);
      basic_width = items_width_array[order[0]];
      last_isabbr = order[2] == CPT_ABBR;
      for (j = 0; j < 3; ++j) {
         item_type = order[j];
         hiId = hiIds[item_type];
         deco = decorationsG[hiId];
         origDeco = deco;
         if (item_type < 2)  // try combine decoration with user custom
            deco = combineUserDecos(idx, item_type, deco);
         p = pum_get_item(idx, item_type);

         if (j + 1 < 3)
            next_isempty = pum_get_item(idx, order[j + 1]) == NULL;

         if (p)
            // Process and display the item
            col = drawMenuItem(row, col, idx, j, order, hiId, deco.flags, &totwidth, next_isempty);

         if (j > 0)
            n = items_width_array[order[1]] + (last_isabbr ? 0 : 1);
         else
            n = order[j] == CPT_ABBR ? 1 : 0;

         // Stop when there is nothing more to display.
         if (j == 2
                || (next_isempty 
                      && (j == 1 || (j == 0 && pum_get_item(idx, order[j + 2]) == NULL)))
                || basic_width + n >= pum_width)
            break;
         fillRowsWithTwoChars(
            row, row + 1, col, pum_col + basic_width + n, ' ', ' ', origDeco.flags
         );
         col = pum_col + basic_width + n;
         totwidth = basic_width + n;
      }

      fillRowsWithTwoChars(
         row, row + 1, col, pum_col + pum_width, ' ', ' ', origDeco.flags
      );
      pum_draw_scrollbar(row, i, thumb_pos, thumb_height);

      ++row;
   }

    screenZindexG = 0;
}

// Position the info popup relative to the popup menu item.
private void
pum_position_info_popup(Portal *po) {
   int col = pum_col + pum_width + pum_scrollbar + 1;
   int row = pum_row;
   int botpos = POPPOS_BOTLEFT;
   int   used_maxwidth_opt = FALSE;

   po->pup.pos = POPPOS_TOPLEFT;
   if (visibleColsG - col < 20 && visibleColsG - col < pum_col) {
      col = pum_col - 1;
      po->pup.pos = POPPOS_TOPRIGHT;
      botpos = POPPOS_BOTRIGHT;
      po->pup.maxWidth = pum_col - 1;
   } else
      po->pup.maxWidth = visibleColsG - col + 1;
   po->pup.maxWidth -= popup_extra_width(po);
   if (po->pup.maxWidthOpt > 0 && po->pup.maxWidth > po->pup.maxWidthOpt) {
      // option value overrules computed value
      po->pup.maxWidth = po->pup.maxWidthOpt;
      used_maxwidth_opt = TRUE;
   }

   row -= popup_top_extra(po);
   if (po->pup.flags & POPF_INFO_MENU) {
      if (pum_row < (int)pumRow) {
         // menu above cursor line, align with bottom
         row += pum_height;
         po->pup.pos = botpos;
      }
      else
          // menu below cursor line, align with top
          row += 1;
   } else
      // align with the selected item
      row += selectedItemInd - firstItemIndS + 1;

   po->pup.flags &= ~POPF_HIDDEN;
   if (po->pup.maxWidth < 10 && !used_maxwidth_opt)
      // The popup is not going to fit or will overlap with the cursor position, hide the popup.
      po->pup.flags |= POPF_HIDDEN;
   else
      popup_set_wantpos_rowcol(po, row, col);
}

//Set the index of the currently selected item. The menu will scroll when necessary. When "n" is 
//out of range don't scroll. This may be repeated when the preview portal is used:
//"repeat" == 0: open preview portal normally
//"repeat" == 1: open preview portal but don't set the size
//"repeat" == 2: don't open preview portal
//Return TRUE when the window was resized and the location of the popup menu must be recomputed.
private int
pum_set_selected(int n, int repeat UNUSED) {
   int resized = FALSE;
   int context = pum_height / 2;
   int scroll_offset;
   int prev_selected = selectedItemInd;
   Unt cur_cot_flags = curBook->o.completeOpt;
   int has_info = FALSE;

   selectedItemInd = n;
   scroll_offset = selectedItemInd - pum_height;

   if (selectedItemInd >= 0 && selectedItemInd < (int)menuLen) {
      if (firstItemIndS > selectedItemInd - 4) {
         // scroll down; when we did a jump it's probably a PageUp then scroll a whole page
         if (firstItemIndS > selectedItemInd - 2) {
            firstItemIndS -= pum_height - 2;
            if (firstItemIndS < 0)
               firstItemIndS = 0;
            ei (firstItemIndS > selectedItemInd)
               firstItemIndS = selectedItemInd;
         } else
            firstItemIndS = selectedItemInd;
      } ei (firstItemIndS < scroll_offset + 5) {
         // scroll up; when we did a jump it's probably a PageDown then
         // scroll a whole page
         if (firstItemIndS < scroll_offset + 3)
            firstItemIndS = MAX(firstItemIndS + pum_height - 2, scroll_offset + 1);
         else
            firstItemIndS = scroll_offset + 1;
      }

      // Give a few lines of context when possible.
      context = MIN(context, 3);
      if (pum_height > 2) {
         if (firstItemIndS > selectedItemInd - context)
            firstItemIndS = MAX(selectedItemInd - context, 0);  // scroll down
         ei (firstItemIndS < selectedItemInd + context - pum_height + 1)
            firstItemIndS = selectedItemInd + context - pum_height + 1;  // up
      }
      // adjust for the number of lines displayed
      firstItemIndS = MIN(firstItemIndS, (int)menuLen - pum_height);

      //Show extra info in the preview portal if there is something and
      //'completeopt' contains "preview" or "popup" or "popuphidden".
      //Skip this when tried twice already.
      //Skip this also when there is not much room.
      //Skip this for command-portal when 'completeopt' contains "preview".
      //NOTE: Be very careful not to sync undo!
      if (displayedItemsS[selectedItemInd].pum_info != NULL
         && visibleRowsG > 10
         && repeat <= 1
         && (cur_cot_flags & COT_ANY_PREVIEW)
         && !((cur_cot_flags & COT_PREVIEW) && commPortTypeG != 0))
      {
         Portal   *curPor_save = curPor;
         Tab   *curtab_save = curtab;
         UsePopup   use_popup;
         has_info = TRUE;
         if (cur_cot_flags & COT_POPUPHIDDEN)
            use_popup = USEPOPUP_HIDDEN;
         ei (cur_cot_flags & COT_POPUP)
            use_popup = USEPOPUP_NORMAL;
         else
            use_popup = USEPOPUP_NONE;
         if (use_popup != USEPOPUP_NONE)
            // don't use WinEnter or WinLeave autocommands for the info popup
            block_autocmds();
         // Open a preview portal and set "curPor" to it.
         // 3 lines by default, prefer 'previewheight' if set and smaller.
         g_do_tagpreview = 3;
         if (p_pvh > 0 && p_pvh < g_do_tagpreview)
            g_do_tagpreview = p_pvh;
         ++isRedrawingDisabledG;
         // Prevent undo sync here, if an autocommand syncs undo weird
         // things can happen to the undo tree.
         ++no_u_sync;
         resized = prepare_tagpreview(FALSE, FALSE, use_popup);
         --no_u_sync;
         if (isRedrawingDisabledG > 0)
            --isRedrawingDisabledG;
         g_do_tagpreview = 0;

         if (curPor->isPreview || (curPor->pup.flags & POPF_INFO)) {
            // Don't want to sync undo in the current book.
            ++no_u_sync;
            int res = startEditingFile(0, NULL, NULL, NULL, ECMD_ONE, 0, NULL);
            --no_u_sync;
            if (res == OK) {
               // Edit a new, empty book. Set options for a "wipeout" book.
               optChangeAndReportError(
                  S"swapfile", (OptionValue){.tag = OPTION_BOOLE, .boole = false}, SET_LOCAL
               );
               optChangeAndReportError(
                  S"buflisted", (OptionValue){.tag = OPTION_BOOLE, .boole = false}, SET_LOCAL
               );
               optChangeAndReportError(S"booktype", optStr("nofile"), OPT_LOCAL);
               optChangeAndReportError(
                  S"diff", (OptionValue){.tag = OPTION_BOOLE, .boole = false}, SET_LOCAL
               );
            }
            if (res == OK) {
               Byte   *p, *e;
               LineNr   lnum = 0;

               for (p = displayedItemsS[selectedItemInd].pum_info; *p != ZERO; ) {
                  e = firstOccurrence(p, '\n');
                  if (!e) {
                     ml_append(lnum++, p, 0, FALSE);
                     break;
                  }
                  *e = ZERO;
                  ml_append(lnum++, p, (int)(e - p + 1), FALSE);
                  *e = '\n';
                  p = e + 1;
               }
               // delete the empty last line
               ml_delete(curBook->mem.lineCount);

               //Increase the height of the preview portal to show the text, but no more than 
               //'previewheight' lines.
               if (repeat == 0 && use_popup == USEPOPUP_NONE) {
                  lnum = MIN(lnum, p_pvh);
                  if (curPor->height < lnum) {
                      portSetHeight((int)lnum, curPor);
                      resized = TRUE;
                  }
               }

               curBook->wasModified = false;
               curBook->o.modifiable = false;
               
               if (selectedItemInd != prev_selected) {
                  curPor->pup.firstLine = 0;
                  curPor->topLine = 1;
               } ei (curPor->topLine > curBook->mem.lineCount)
                  curPor->topLine = curBook->mem.lineCount;
               curPor->cursor.lnum = curPor->topLine;
               curPor->cursor.col = 0;
               if (use_popup != USEPOPUP_NONE) {
                  pum_position_info_popup(curPor);
                  if (portalIsValid(curPor_save))
                     redrawPortLater(curPor_save, UPD_SOME_VALID);
               }
               if ((curPor != curPor_save && portalIsValid(curPor_save))
                   || (curtab != curtab_save && isTabValid(curtab_save))
               ){
                  int save_redr_status;

                  if (curtab != curtab_save && isTabValid(curtab_save))
                      gotoTab(curtab_save, FALSE, FALSE);

                  // When the first completion is done and the preview
                  // portal is not resized, skip the preview portal's status line redrawing.
                  if (ins_compl_active() && !resized)
                      curPor->statusLineNeedsRedraw = FALSE;

                  // Return cursor to where we were
                  validate_cursor();
                  redraw_later(UPD_SOME_VALID);

                  // When the preview portal was resized we need to update the view on the book. 
                  // Only go back to the portal when needed, otherwise it will always be redrawn.
                  if (resized && portalIsValid(curPor_save)) {
                      ++no_u_sync;
                      enterPortal(curPor_save, TRUE);
                      --no_u_sync;
                      update_topline();
                  }

                  // Update the screen before drawing the popup menu.
                  // Enable updating the status lines.
                  pum_pretend_not_visible = TRUE;

                  // But don't draw text at the new popup menu position, it causes flicker. When 
                  // resizing we need to draw anyway, the position may change later.
                  // Also do not redraw the status line of the original current portal here, to 
                  // avoid it gets drawn with StatusLineNC for a moment and cause flicker.
                  pum_will_redraw = !resized;
                  save_redr_status = curPor_save->statusLineNeedsRedraw;
                  curPor_save->statusLineNeedsRedraw = FALSE;
                  drawUpdateScreen(0);
                  pum_pretend_not_visible = FALSE;
                  pum_will_redraw = FALSE;
                  curPor_save->statusLineNeedsRedraw = save_redr_status;

                  if (!resized && portalIsValid(curPor_save)) {
                     Portal *po = curPor;
                     ++no_u_sync;
                     enterPortal(curPor_save, TRUE);
                     --no_u_sync;
                     if (use_popup == USEPOPUP_HIDDEN && portalIsValid(po))
                        popup_hide(po);
                  }

                  // May need to update the screen again when there are autocommands involved.
                  pum_pretend_not_visible = TRUE;
                  pum_will_redraw = !resized;
                  drawUpdateScreen(0);
                  pum_pretend_not_visible = FALSE;
                  pum_will_redraw = FALSE;
                  callUpdateScreen = false;
               }
            }
         }
         if (PORTAL_IS_POPUP(curPor))
            // can't keep focus in a popup portal
            enterPortal(firstPor, TRUE);
         if (use_popup != USEPOPUP_NONE)
            unblock_autocmds();
      }
   }
   if (!has_info)
      // hide any popup info portal
      popup_hide_info();

   return resized;
}

// Undisplay the popup menu (later).
void
pum_undisplay(void) {
   displayedItemsS = NULL;
   redraw_all_later(UPD_NOT_VALID);
   needRedrawTabpanelG = TRUE;
   if (pum_in_cmdline) {
      mustClearCommlineG = TRUE;
      pum_in_cmdline = FALSE;
   }
   status_redraw_all();
   // hide any popup info portal
   popup_hide_info();
}

// Clear the popup menu.  Currently only resets the offset to the first displayed item.
void
pum_clear(void) {
   firstItemIndS = 0;
}

//Return TRUE if the popup menu is displayed. Used to avoid some redrawing that could overwrite 
//it. Overruled when "pum_pretend_not_visible" is set, used to redraw the status lines.
int
pum_visible(void) {
   return !pum_pretend_not_visible && displayedItemsS != NULL;
}

// Return TRUE if the popup can be redrawn in the same position.
private int
pum_in_same_position(void) {
   return pumPort != curPor
       || ((int)pumRow == curPor->cursorRow + curPor->portalRow
            && pumHeight == curPor->height
            && (int)pumCol == curPor->portalCol
            && pumWWidth == curPor->width
         );
}

// Return TRUE when pum_may_redraw() will call pum_redraw().
// This means that the pum area should not be overwritten to avoid flicker.
int
pum_redraw_in_same_position(void) {
   if (!pum_visible() || pum_will_redraw)
      return FALSE;  // nothing to do

   return pum_in_same_position();
}

// Reposition the popup menu to adjust for portal layout changes.
void
pum_may_redraw(void) {
   PopupItem   *array = displayedItemsS;
   Unt      len = menuLen;
   int      selected = selectedItemInd;

   if (!pum_visible() || pum_will_redraw)
      return;  // nothing to do

   if (pum_in_same_position()) {
      pum_redraw();  // Redraw portal in same position
   } else {
      int wcol = curPor->cursorCol;

      // Portal layout changed, recompute the position.
      // Use the remembered cursorCol value, the cursor may have moved when a
      // completion was inserted, but we want the menu in the same position.
      pum_undisplay();
      curPor->cursorCol = pumWCol;
      curPor->cacheState |= VALID_WCOL;
      pum_display(array, len, selected);
      curPor->cursorCol = wcol;
   }
}

//Return the height of the popup menu, the number of entries visible.
//Only valid when pum_visible() returns TRUE!
int
pum_get_height(void) {
   return pum_height;
}

// Add size information about the pum to "dict".
void
pum_set_event_info(Bag *bag) {
   if (!pum_visible())
      return;
   (void)bagAddNumber(bag, S"height", pum_height);
   (void)bagAddNumber(bag, S"width", pum_width);
   (void)bagAddNumber(bag, S"row", pum_row);
   (void)bagAddNumber(bag, S"col", pum_col);
   (void)bagAddNumber(bag, S"size", menuLen);
   (void)bagAdd_bool(bag, S"scrollbar", pum_scrollbar ? VVAL_TRUE : VVAL_FALSE);
}

private void
pum_position_at_mouse(int min_width) {
   if (visibleRowsG - mouseRowG > menuLen || visibleRowsG - mouseRowG > mouseRowG) {
      // Enough space below the mouse row, or there is more space below the mouse row than above.
      pum_row = mouseRowG + 1;
      if (pum_height > visibleRowsG - pum_row)
          pum_height = visibleRowsG - pum_row;
      if (pum_row + pum_height > (int)commlineRowG)
          pum_in_cmdline = TRUE;
   } else {
   // Show above the mouse row, reduce height if it does not fit.
   pum_row = mouseRowG - menuLen;
   if (pum_row < 0) {
       pum_height += pum_row;
       pum_row = 0;
   }
   }

   if (visibleColsG - mouseColG >= pum_base_width || visibleColsG - mouseColG > min_width)
      // Enough space to show at mouse column.
      pum_col = mouseColG;
   else
       // Not enough space, right align with portal.
       pum_col = visibleColsG -  MIN(pum_base_width, min_width);
   pum_width = visibleColsG - pum_col;

   pum_width = MIN(pum_width, pum_base_width + 1);
   // Do not redraw at cursor position.
   pumPort = NULL;
}


private PopupItem *balloon_array = NULL;
private Unt balloonArraySizeS;

# define BALLOON_MIN_WIDTH 50
# define BALLOON_MIN_HEIGHT 10

typedef struct {
   Byte   *start;
   int      bytelen;
   int      cells;
   int      indent;
} balpart_T;

//Split a string into parts to display in the balloon. Aimed at output from gdb. Attempt to split 
//at white space, preserve quoted strings and make a struct look good. Resulting array is stored 
//in "array" and its the size is returned. Return value is at least 1
Unt
balloonSplitMessage(CS mesg, OUT Arr(PopupItem)* array) {
   ArrayList   ga;
   balpart_T   *item;
   int quoted = FALSE;
   int indent = 0;
   int max_cells = 0;
   Unt max_height = MIN(visibleRowsG, 4) / 2 - 1;
   int long_item_count = 0;
   int split_long_items = FALSE;

   ga_init2(&ga, sizeof(balpart_T), 20);
   CS p = mesg;

   while (*p != ZERO) {
      if (ga_grow(&ga, 1) == FAIL)
         goto failed;
      item = ((balpart_T *)ga.c) + ga.len;
      item->start = p;
      item->indent = indent;
      item->cells = indent * 2;
      ++ga.len;
      while (*p != ZERO) {
         if (*p == '"')
            quoted = !quoted;
         ei (*p == '\n')
            break;
         ei (*p == '\\' && p[1] != ZERO)
            ++p;
         ei (!quoted) {
            if ((*p == ',' && p[1] == ' ') || *p == '{' || *p == '}') {
               // Looks like a good point to break.
               if (*p == '{')
                  ++indent;
               ei (*p == '}' && indent > 0)
                  --indent;
               ++item->cells;
               p = skipwhite(p + 1);
               break;
            }
         }
         item->cells += ptr2cells(p);
         p += utfCharLen(p);
      }
      item->bytelen = p - item->start;
      if (*p == '\n')
         ++p;
      if (item->cells > max_cells)
         max_cells = item->cells;
      long_item_count += (item->cells - 1) / BALLOON_MIN_WIDTH;
   }

   Unt height = 2 + ga.len;

   // If there are long items and the height is below the limit: split lines
   if (long_item_count > 0 && height + long_item_count <= max_height) {
      split_long_items = TRUE;
      height += long_item_count;
   }

   // Limit to half the portal height, it has to fit above or below the mouse position.
   if (height > max_height)
      height = max_height;
   *array = ALLOC_CLEAR_MULT(PopupItem, height);
   if (*array == NULL)
      goto failed;

   // Add an empty line above and below, looks better.
   (*array)->pum_text = copyStr(E);
   (*array + height - 1)->pum_text = copyStr(E);

   for (Unt line = 1, item_idx = 0; line < height - 1; ++item_idx) {
      int   skip;
      int   thislen;
      int   copylen;
      int   ind;
      int   cells;

      item = ((balpart_T *)ga.c) + item_idx;
      if (item->bytelen == 0)
         (*array)[line++].pum_text = copyStr((CS)"");
      else {
         for (skip = 0; skip < item->bytelen; skip += thislen) {
            if (split_long_items && item->cells >= BALLOON_MIN_WIDTH) {
               cells = item->indent * 2;
               for (p = item->start + skip;
                    p < item->start + item->bytelen;
                    p += utfCharLen(p)
               ) {
                  if ((cells += ptr2cells(p)) > BALLOON_MIN_WIDTH)
                     break;
               } 
               thislen = p - (item->start + skip);
            } else
               thislen = item->bytelen;

            // put indent at the start
            p = alloc(thislen + item->indent * 2 + 1);
            for (ind = 0; ind < item->indent * 2; ++ind)
               p[ind] = ' ';

            // exclude spaces at the end of the string
            for (copylen = thislen; copylen > 0; --copylen) {
               if (item->start[skip + copylen - 1] != ' ')
                  break;
            } 

            copySubstrToAllocation(p + ind, (Text){item->start + skip, copylen});
            (*array)[line].pum_text = p;
            item->indent = 0;  // wrapped line has no indent
            ++line;
         }
      } 
   }
   ga_clear(&ga);
   return height;

failed:
   ga_clear(&ga);
   return 0;
}

void
ui_remove_balloon(void) {
   if (!balloon_array)
      return;

   pum_undisplay();
   while (balloonArraySizeS > 0)
      eeglFree(balloon_array[--balloonArraySizeS].pum_text);
   EE_CLEAR(balloon_array);
}

// Terminal version of a balloon, uses the popup menu code.
void
ui_post_balloon(CS mesg, List *list) {
   ui_remove_balloon();

   if (!mesg && !list) {
      pum_undisplay();
      return;
   }
   if (list) {
      balloonArraySizeS = list->len;
      balloon_array = ALLOC_CLEAR_MULT(PopupItem, list->len);
      if (!balloon_array)
         return;
      CHECK_LIST_MATERIALIZE(list);
      ListItem  *li = list->first;
      for (int idx = 0; li; li = li->next, ++idx) {
         CS text = convertVarToStringSingleUse(&li->c);
         balloon_array[idx].pum_text = copyStr(text == NULL ? (CS)"" : text);
      }
   } else
      balloonArraySizeS = balloonSplitMessage(mesg, &balloon_array);

   if (balloonArraySizeS == 0)
      return;

   displayedItemsS = balloon_array;
   menuLen = balloonArraySizeS;
   computeSize();
   pum_scrollbar = 0;
   pum_height = balloonArraySizeS;

   pum_position_at_mouse(BALLOON_MIN_WIDTH);
   selectedItemInd = -1;
   firstItemIndS = 0;
   pum_redraw();
}

// Called when the mouse moved, may remove any displayed balloon.
void
ui_may_remove_balloon(void) {
   // For now: remove the balloon whenever the mouse moves to another screen cell.
   ui_remove_balloon();
}

//}}}
//{{{ballon evaluation

//Find text under the mouse position "row" / "col".
//If "getword" is TRUE the returned text in "*textp" is not the whole line but
//the relevant word in allocated memory.
//Return OK if found.
//Return FAIL if not found, no text at the mouse position.
int
find_word_under_cursor(
   int       mouserow,
   int       mousecol,
   int       getword,
   int       flags,   // flags for find_ident_at_pos()
   Portal** winp,   // may be NULL
   LineNr* lnump,   // may be NULL
   Byte** textp,
   int* colp,   // column where mouse hovers, can be NULL
   int* startcolp // column where text starts, can be NULL
){
   int row = mouserow;
   int col = mousecol;
   int      scol;
   LineNr   lnum;

   *textp = NULL;
   Portal* po = mouseFindPortal(OUT &row, OUT &col, FAIL_POPUP);
   if (!po || row < 0 || row >= (int)po->height || col >= (int)po->width)
      return FAIL;

   // Found a portal and the cursor is in the text. Now find the line number.
   if (mouse_comp_pos(po, OUT &row, OUT &col, &lnum, NULL))
      return FAIL;      // position is below the last line

   // Not past end of the file.
   CS lbuf = memGetLine(po->book, lnum, false);
   if (col > (int)drawLineOnScreentabsize(po, lnum, lbuf, (ColNr)MAXCOL))
      return FAIL;      // past end of line

   // Not past end of line.
   if (getword) {
      int len;
      Pos* spos = NULL, *epos = NULL;

      if (VIsual_active) {
         if (LT_POS(VIsual, curPor->cursor)) {
            spos = &VIsual;
            epos = &curPor->cursor;
         } else {
            spos = &curPor->cursor;
            epos = &VIsual;
         }
      }

      col = vcol2col(po, lnum, col, NULL);
      scol = col;

      if (VIsual_active
         && po->book == curPor->book
         && (lnum == spos->lnum
             ? col >= spos->col
             : lnum > spos->lnum)
         && (lnum == epos->lnum
             ? col <= epos->col
             : lnum < epos->lnum))
      {
         // Visual mode and pointing to the line with the
         // Visual selection: return selected text, with a maximum of one line.
         if (spos->lnum != epos->lnum || spos->col == epos->col)
            return FAIL;

         lbuf = memGetLine(curPor->book, VIsual.lnum, false);
         len = epos->col - spos->col;
         len += utfCharLen(lbuf + epos->col);
         lbuf = copySubstr(lbuf + spos->col, len);
         lnum = spos->lnum;
         col = spos->col;
         scol = col;
      } else {
         // Find the word under the cursor.
         ++emsg_off;
         len = find_ident_at_pos(po, lnum, (ColNr)col, &lbuf, &scol, flags);
         --emsg_off;
         if (len == 0)
            return FAIL;
         lbuf = copySubstr(lbuf, len);
      }
   } else
      scol = col;

   if (winp)
      *winp = po;
   if (lnump)
      *lnump = lnum;
   *textp = lbuf;
   if (colp)
      *colp = col;
   if (startcolp)
      *startcolp = scol;

   return OK;
}


//Get the text and position to be evaluated for "beval".
//If "getword" is TRUE the returned text is not the whole line but the relevant word in allocated
//memory. OK or FAIL.
int
get_beval_info(
   BalloonEval   *beval,
   int      getword,
   Portal      **winp,
   LineNr   *lnump,
   Byte      **textp,
   int      *colp)
{
   int row = mouseRowG;
   int col = mouseColG;
   if (row < 0 || col < 0)
      return FAIL;

   if (find_word_under_cursor(
         row, col, getword, FIND_IDENT + FIND_STRING + FIND_EVAL, winp, lnump, textp, 
         colp, NULL
       ) == OK
   ) {
      beval->ts = (*winp)->book->o.shiftWidth;
      return OK;
   }

   return FAIL;
}

// Show a balloon with "mesg" or "list". Hide the balloon when both are NULL.
void
post_balloon(BalloonEval *beval UNUSED, CS mesg, List *list UNUSED) {
   ui_post_balloon(mesg, list);
}

//TRUE if the balloon eval has been enabled:
//'ballooneval' for the GUI and 'balloonevalterm' for the terminal.
//Also checks if the screen isn't scrolled up.
private int
can_use_beval(void) {
   return (0 || (p_bevalterm)) && msg_scrolled == 0;
}

// Evaluate the expression 'bexpr' and set the text in the balloon 'beval'.
private void
bexpr_eval(
   BalloonEval* beval,
   CS bexpr,
   Portal* po,
   LineNr lnum,
   int col,
   CS text)
{
   Portal   *cw;
   long   winnr = 0;
   Book   *save_curbuf;
   static Byte  *result = NULL;
   Unt   len;

   ScriptPos   save_sctx = scriptPosG;

   // Convert portal pointer to number.
   for (cw = firstPor; cw != po; cw = cw->next)
       ++winnr;

   set_EeglVar_nr(VV_BEVAL_BUFNR, (long)po->book->fiNum);
   set_EeglVar_nr(VV_BEVAL_WINNR, winnr);
   set_EeglVar_nr(VV_BEVAL_WINID, po->id);
   set_EeglVar_nr(VV_BEVAL_LNUM, (long)lnum);
   set_EeglVar_nr(VV_BEVAL_COL, (long)(col + 1));
   set_EeglVar_string(VV_BEVAL_TEXT, text, -1);
   eeglFree(text);

   //Temporarily change the curBook, so that we can determine whether
   //the book-local balloonexpr option was set insecurely.
   save_curbuf = curBook;
   curBook = po->book;
   curBook = save_curbuf;
   ++textlock;

   if (bexpr == curBook->o.balloonExpr) {
      ScriptPos *sp = optGetScriptPos(S"balloonexpr");

      if (sp)
         scriptPosG = *sp;
   } else
      scriptPosG = curBook->o.scriptLocs[BOOK_balloonExpr];

   eeglFree(result);
   result = eval_to_string(bexpr, TRUE, TRUE);

   // Remove one trailing newline, it is added when the result was a
   // list and it's hardly ever useful.  If the user really wants a
   // trailing newline he can add two and one remains.
   if (result) {
      len = STRLEN(result);
      if (len > 0 && result[len - 1] == NL)
         result[len - 1] = ZERO;
   }

   --textlock;
   scriptPosG = save_sctx;

   set_EeglVar_string(VV_BEVAL_TEXT, NULL, -1);
   if (result != NULL && result[0] != ZERO)
      post_balloon(beval, result, NULL);

   // The 'balloonexpr' evaluation may show something on the screen that requires a screen update
   if (must_redraw)
      redraw_after_callback(FALSE, FALSE);
}

// Common code, invoked when the cursor is resting for a moment.
void
general_beval_cb(BalloonEval *beval, int state UNUSED) {
   Portal   *po;
   int      col;
   LineNr   lnum;
   Byte   *text;
   Byte   *bexpr;
   static Boole recursive = false;

   // Don't do anything when 'ballooneval' is off, messages scrolled the
   // portals up or we have no beval area.
   if (!can_use_beval() || beval == NULL)
      return;

   // Don't do this recursively.  Happens when the expression evaluation
   // takes a long time and invokes something that checks for CTRL-C typed.
   if (recursive)
      return;
   recursive = true;

   if (get_beval_info(beval, TRUE, &po, &lnum, &text, &col) == OK) {
      bexpr = po->book->o.balloonExpr;
      if (*bexpr != ZERO) {
         bexpr_eval(beval, bexpr, po, lnum, col, text);
         recursive = false;
         return;
      }
   }

   recursive = false;
}

//}}}

//}}}
