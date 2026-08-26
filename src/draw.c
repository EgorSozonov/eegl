//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## draw.c: drawing text lines to the screen 
 
#include "eegl.h"

//used for @hlsearch hilite matching
private Match screenSearchP;

//{{{@@forward declarations


private int screenclear2(Boole doclear);
private void lineinvalid(Unt off, int width);
private int doPortalLines(Portal *, int , int , int , int , Unt );
private void lineclear(Unt off, int width, Unt decoId);
private void markFollowingPortalsForRedraw(Portal* po);
private void msg_pos_mode(void);
private void recording_mode(char flags);
private void singleChar(Unt off, int row, int col);

private void updatePortal(Portal* po);
private void statusLineCustom(Portal* po);
private int  didUpdateOnePortal;


//}}}
//{{{low level

private Arr(Decoration) screenDecosP = null;
private Arr(ColNr) screenColS = null;

//Flag for each line whether it wraps to the next line.
private Arr(Boole) lineWrapsP = null;   // line wraps to next line
private int isScreenClearedP = false;   // has screen been cleared? yes/no/maybe
private Boole avoidLineInsertionS = false; // don't insert lines
private int rulerColP;      // column for ruler

// Flag that is set when drawing for a callback, not from the main command loop.
private int redrawingForCallbackS INIT(= 0);

//The characters that are currently on the screen are kept in screenTextP[].
//It is a single block of characters, the size of the screen plus one line.
//The decorations for those characters are kept in ScreenDecosG[].
//The virtual column in the line is kept in ScreenCols[].
//
//"lineStartsP[n]" is the offset from ScreenLines[] for the start of line 'n'.
//That value is used for ScreenLinesUC[], screenDecosP[] and ScreenCols[].
//Note: before the screen is initialized, these can be null.
private CS screenTextP = null;
private Arr(Unt) lineStartsP = null;

//Lower level code for displaying on the screen.
//
//Output to the screen (console, terminal emulator) is minimized
//by remembering what is already on the screen, and only updating the parts that changed.
//
//screenTextP[off]  Contains a copy of the whole screen, as it is currently
//   displayed (excluding text written by external commands).
//   Dimensions of screenTextP = (visibleRowsG + 1) * visibleColumnsG
//screenDecosP[off] Contains the associated (drawn) decorations.
//screenColS[off]   Contains the virtual columns in the line. -1 means not
//                  available or before buffer text.
//
//
//Multi-byte characters are converted to Unicode and stored in screenLinesUCG[]. 
//screenTextP[] contains the first byte only. For an ASCII character without composing chars 
//screenLinesUCG[] will be 0 and screenLinesCG[][] is not used.
//When the character occupies two display cells, the next byte in screenTextP[] is 0.
//screenLinesCG[][] contain up to 'maxcombine' composing characters (drawn on top of the first 
//character). There is 0 after the last one used.
//
//The screen_*() functions write to the screen and handle updating screenTextP[].

// One screen line to be displayed.  Points into screenTextP.
private CS currScreenLineS = null;

private Arr(Unt) screenLinesCG; //for composing characters. Blocks of MAX_COMBINED_SYMBOLS nums


//Using Unicode characters, the character in ScreenLinesUC[] contains the Unicode for 
//the character at this position, or ZERO when the character in ScreenLines[] is to be 
//used (ASCII char). The composing characters are to be drawn on top of the original character.
//ScreenLinesC[0][off] is only to be used when ScreenLinesUC[off] != 0.
private Arr(Unt) screenLinesUCG;   // decoded UTF-8 characters

// The decorations that are actually active for writing to the screen.
private Decoration activeDecoP;
private Decoration defaultDecoP;

// Ugly global: overrule decoration used by singleChar()
private VTermDeco screen_charDeco = DECO_NONE;

pub void
drawInit() {
   defaultDecoP = getFullDecoration(0);
   activeDecoP = defaultDecoP;
}

//If "po" is a popup portal, then get the "Pmenu" hilite decoration.
pub Decoration
getPortcolorDeco(Portal* po) {
   if (PORTAL_IS_POPUP(po)) {
      if (isInfoPopup(po))
         return getFullDecoration(HLF_PSI);    // PmenuSel
      else
         return getFullDecoration(HLF_PNI);    // Pmenu
   } else {
      return getFullDecoration(HLF_NONE);
   }
}

// Call fillRowsWithTwoChars() with a column offset. Return the new offset.
private int
fillRowsWithCharsWithColumnOffset(
   Portal* po,
   int c1,
   int c2,
   int off,
   int width,
   int row,
   int endrow,
   Decoration deco
) {
   Unt nn = off + width;

   if (nn > po->width)
      nn = po->width;
   fillRowsWithTwoChars(
      po->windowRow + row, po->windowRow + endrow,
      po->windowCol + off, (int)po->windowCol + nn, c1, c2, deco
   );
   return nn;
}

private Decoration
toScreenDeco(Unt hiId) {
   Decoration deco = getFullDecoration(hiId);
   return (Decoration) {
      .hiId = hiId, .flags = deco.flags, .under = deco.under, 
      .fieldPresence = deco.fieldPresence & HI_HAS_UNDER
   };
}

//Clear lines near the end of the portal and mark the unused lines with "c1". use "c2" as the 
//filler character. When "draw_margin" is true then draw the sign, fold and number columns.
private void
drawVoidAtPortalEnd(
   Portal* po,
   Unt c1,
   Unt c2,
   int draw_margin,
   int row,
   int endrow,
   Short hl
){
   int n = 0;
   Decoration deco = toScreenDeco(hl);

   if (draw_margin) {
      if (isSigncolumnOn(po))
         // draw the sign column
         n = fillRowsWithCharsWithColumnOffset(
             po, ' ', ' ', n, 2, row, endrow, getFullDecoration(HLF_SC)
         );
      // draw the number column
      n = fillRowsWithCharsWithColumnOffset(
         po, ' ', ' ', n, number_width(po) + 1, row, endrow, getFullDecoration(HLF_N)
      );
   }

   fillRowsWithTwoChars(
      po->windowRow + row, po->windowRow + endrow, po->windowCol + n, (int)P_ENDCOL(po), c1, c2, 
      deco
   );

   normSetEmptyRowCount(po, row);
}

//Return if the composing characters at "offFrom" and "offTo" differ.
//Only to be used when screenLinesUCG[offFrom] != 0.
private int
comp_char_differs(int offFrom, int offTo) {
   for (Unt i = 0; i < MAX_COMBINED_SYMBOLS; ++i) {
      if (screenLinesCG[MAX_COMBINED_SYMBOLS*offFrom + i] 
            != screenLinesCG[MAX_COMBINED_SYMBOLS*offTo + i]
      )
         return true;
      if (screenLinesCG[MAX_COMBINED_SYMBOLS*offFrom + i] == 0)
         return false;
   }
   return false;
}

//Check whether the given character needs redrawing:
//- the (first byte of the) character is different
//- the decorations are different
//- the character is multi-byte and the next byte is different
//- the character is two cells wide and the second cell differs.
private Boole
charNeedsRedraw(int from, int to, int cols) {
   return (cols > 0
          && ((screenTextP[from] != screenTextP[to]
                || screenDecosP[from].flags != screenDecosP[to].flags)
                  || (screenLinesUCG[from] != screenLinesUCG[to]
                        || (screenLinesUCG[from] != 0 && comp_char_differs(from, to))
                     )
             )
   );
}

//Return the index in screenTextP[] for the current screen line.
pub int
screen_get_current_line_off(void) {
   return (int)(currScreenLineS - screenTextP);
}

//Return true if this position has a higher level popup or this cell is
//transparent in the current popup.
private int
blocked_by_popup(int row, int col) {
   if (!popup_visible)
      return false;
   int off = row * screenLinesColsG + col;
   return popupMaskG[off] > screenZindexG || popupTransparencyG[off];
}

// Reset the hiliting.  Used before clearing the screen.
pub void
resetActiveDeco(void) {
   //Use decorations that are very unlikely to appear in text
   activeDecoP.flags = DECO_BOLD | DECO_UNDERLINE | DECO_INVERSE;
}

//Return true if the character at "row" / "col" is under the popup menu and it
//will be redrawn soon or it is under another popup.
private int
skipForPopup(int row, int col) {
   // Popup portals with zindex higher than POPUPMENU_ZINDEX go on top.
   if (pum_under_menu(row, col, true) && screenZindexG <= POPUPMENU_ZINDEX)
      return true;
   if (blocked_by_popup(row, col))
      return true;
   return false;
}

// Get the character to use in a separator between vertically split portals. 
// Get its decrations into "*deco".
private Unt
fillchar_vsep(OUT Decoration* deco) {
   *deco = getFullDecoration(HLF_C);
   return (deco->flags == 0 && fillCharsG.vert == ' ') ? '|' : fillCharsG.vert;
}

//Move one "cooked" screen line to the screen, but only the characters that
//have actually changed.  Handle insert/delete character.
//"coloff" gives the first column on the screen for this line.
//"endcol" gives the columns where valid characters are.
//"clear_width" is the width of the portal.  It's > 0 if the rest of the line
//needs to be cleared, negative otherwise.
//"flags" can have bits:
//SLF_POPUP       popup portal
//SLF_RIGHTLEFT    rightleft portal:
//   When true and "clear_width" > 0, clear columns 0 to "endcol"
//   When false and "clear_width" > 0, clear columns "endcol" to "clear_width"
//SLF_INC_VCOL:
//   When false, use "last_vcol" for screenColS[] of the columns to clear.
//   When true, use an increasing sequence starting from "last_vcol + 1" for
//   screenColS[] of the columns to clear.
pub void
screen_line(
   int row,
   int coloff,
   int endcol,
   int clear_width,
   ColNr last_vcol,
   Unt flags
){
   int col = 0;
   Boole force = false;   // force update rest of the line
   Boole redraw_this;   //does character need redraw?
   Boole redraw_next;   //redraw_this for next character
   int clear_next = false;

   // Check for illegal row and col, just in case.
   if (row >= visibleRowsG)
      row = visibleRowsG - 1;
   if (endcol > visibleColsG)
      endcol = visibleColsG;

   clip_may_clear_selection(row, row);

   Unt offFrom = (Unt)(currScreenLineS - screenTextP);
   Unt offTo = lineStartsP[row] + coloff;

   // First char of a popup portal may go on top of the right half of a
   // double-wide character. Clear the left half to avoid it getting the popup
   // portal background color.
   if (coloff > 0
         && screenTextP[offTo] == 0
         && screenLinesUCG[offTo - 1] != 0
         && mb_char2cells(screenLinesUCG[offTo - 1]) > 1
   ){
      screenTextP[offTo - 1] = ' ';
      screenLinesUCG[offTo - 1] = 0;
      singleChar(offTo - 1, row, col + coloff - 1);
   }

   redraw_next = charNeedsRedraw(offFrom, offTo, endcol - col);

   while (col < endcol) {
      redraw_this = redraw_next;
      redraw_next = force || charNeedsRedraw(offFrom + 1, offTo + 1, endcol - col - 1);

      // Do not redraw if under the popup menu.
      if (redraw_this && skipForPopup(row, col + coloff))
         redraw_this = false;

      if (redraw_this) {
         screenTextP[offTo] = screenTextP[offFrom];
         screenLinesUCG[offTo] = screenLinesUCG[offFrom];
         if (screenLinesUCG[offFrom] != 0) {
            for (Unt i = 0; i < MAX_COMBINED_SYMBOLS; ++i) {
               screenLinesCG[MAX_COMBINED_SYMBOLS*offTo + i] 
                  = screenLinesCG[MAX_COMBINED_SYMBOLS*offFrom + i];
            } 
         }
         screenDecosP[offTo] = screenDecosP[offFrom];
         singleChar(offTo, row, col + coloff);
      }

      screenColS[offTo] = screenColS[offFrom];
      offTo++;
      offFrom++;
      col++;
   }

   if (clear_next && !skipForPopup(row, col + coloff)) {
      // Clear the second half of a double-wide character of which the left
      // half was overwritten with a single-wide character.
      screenTextP[offTo] = ' ';
      screenLinesUCG[offTo] = 0;
      singleChar(offTo, row, col + coloff);
   }

   if (clear_width > 0) {
      // blank out the rest of the line
      while (col < clear_width 
            && screenTextP[offTo] == ' '
            && screenDecosP[offTo].flags == 0
            && screenLinesUCG[offTo] == 0
      ){
         screenColS[offTo] = (flags & SLF_INC_VCOL) ? ++last_vcol : last_vcol;
         ++offTo;
         ++col;
      }
      if (col < clear_width) {
         fillRowsWithTwoChars(row, row + 1, col + coloff, clear_width + coloff, ' ', ' ', defaultDecoP);
         while (col < clear_width) {
            screenColS[offTo++] = (flags & SLF_INC_VCOL) ? ++last_vcol : last_vcol;
            ++col;
         }
      }
   }

   if (clear_width > 0 && !(flags & SLF_POPUP)) {  //no separator for popup portal
      //For a portal that has a right neighbor, draw the separator char
      //right of the portal contents. But not on top of a popup portal.
      if (coloff + col < (int)firstPor->windowCol + (int)topframeG->width) {
         if (!skipForPopup(row, col + coloff)) {
            Decoration deco;
            int c = fillchar_vsep(OUT &deco);
            if (screenTextP[offTo] != (Byte)c
               || ((int)screenLinesUCG[offTo] != (c >= 0x80 ? c : 0))
               || getDecoFlags(screenDecosP[offTo].hiId) != deco.flags
            ){
               screenTextP[offTo] = c;
               screenDecosP[offTo].hiId = HLF_C;
               screenDecosP[offTo].flags = getDecoFlags(HLF_C);
               
               if (c >= 0x80) {
                   screenLinesUCG[offTo] = c;
                   screenLinesCG[MAX_COMBINED_SYMBOLS*offTo] = 0;
               } else
                   screenLinesUCG[offTo] = 0;
               singleChar(offTo, row, col + coloff);
            }
         }
      } else
         lineWrapsP[row] = false;
   }
}

// Draw the vertical separator right of portal "po" starting with line "row".
private void
drawVerticalSeparator(Portal* po, int row) {
   if (!po->vsepWidth)
      return;

   // draw the vertical separator right of this portal
   Decoration deco;
   int c = fillchar_vsep(OUT &deco);
   fillRowsWithTwoChars(
      po->windowRow + row, po->windowRow + po->height, P_ENDCOL(po), P_ENDCOL(po) + 1, c, ' ', 
      deco
   );
}

//Return true if the status line of portal "po" is connected to the status line of the portal 
//right of it.  If not, then it's a vertical separator. Only call if (po->vsepWidth != 0).
pub int
stl_connected(Portal* po) {
   Frame* fr = po->frame;
   while (fr->parent) {
      if (fr->parent->layout == FR_COL) {
         if (fr->next)
            break;
      } else {
         if (fr->next)
            return true;
      }
      fr = fr->parent;
   }
   return false;
}

//Get the value to show for the language mappings, active @keymap
pub int
drawGetKeymapStr(Portal* po, OUT Text buf) {       // buffer for the result
   if (po->book->o.b_p_iminsert != B_IMODE_LMAP)
      return 0;

   Book* old_curbuf = curBook;
   Portal* old_curPor = curPor;
   Byte to_evaluate[] = "b:keymap_name";

   curBook = po->book;
   curPor = po;
   ++emsg_skip;
   CS p = eval_to_string(to_evaluate, false, false);
   CS s = p;
   --emsg_skip;
   curBook = old_curbuf;
   curPor = old_curPor;
   if (p == NULL || *p == ZERO) {
      p = (CS)"lang";
   }
   int plen = eeSnprintf(buf.c, buf.len, "<%s>", p);
   eeglFree(s);
   if (plen < 0 || plen + 1 > (int)buf.len) {
      buf.c[0] = ZERO;
      plen = 0;
   }

   return plen;
}

// Return the row for drawing the statusline and the ruler of portal "po".
private int
statusline_row(Portal* po) {
   if (po->frame->width == STATUS_HEIGHT && !portalIsPopup(po))
      return po->windowRow;
   return po->windowRow + po->height - 1;
}

private void
startDrawingHilite(Short hiId) {
   if (!fullScreenG)
      return;
   activeDecoP = getFullDecoration(hiId);
   Decoration fullDeco = getFullDecoration(activeDecoP.hiId);
      
   if ((activeDecoP.flags & DECO_BOLD) != 0 && *termCodesG[KS_MD] != ZERO)
      out_str(termCodesG[KS_MD]);
   ei ((activeDecoP.flags & DECO_BOLD) != 0 && (fullDeco.fieldPresence & HI_HAS_FG) != 0)
      // If the Normal FG color has BOLD flag and the new HL has a FG color defined, clear BOLD
      out_str(termCodesG[KS_ME]);
      
   if ((activeDecoP.flags & DECO_UNDERCURL) && *termCodesG[KS_UCS] != ZERO)
      out_str(termCodesG[KS_UCS]);
      
   if (((activeDecoP.flags & DECO_UNDERLINE) != 0
         || ((activeDecoP.flags & DECO_UNDERCURL) != 0 && *termCodesG[KS_UCS] == ZERO))
       && *termCodesG[KS_US] != ZERO
   ) {
      out_str(termCodesG[KS_US]);
   } 
   
   if ((activeDecoP.flags & DECO_ITALIC) && *termCodesG[KS_CZH] != ZERO)
      out_str(termCodesG[KS_CZH]);
      
   if ((activeDecoP.flags & DECO_INVERSE) && *termCodesG[KS_MR] != ZERO)
      out_str(termCodesG[KS_MR]);

   // Output the color or start string after bold etc., in case the bold overrides the color setting
   if ((fullDeco.fieldPresence & HI_HAS_FG) != 0)
      termApplyFgColor(fullDeco.fg);
   if ((fullDeco.fieldPresence & HI_HAS_BG) != 0)
      termApplyBgColor(fullDeco.bg);
   if ((fullDeco.fieldPresence & HI_HAS_UNDER) != 0) {
      termApplyUnderColor(fullDeco.under);
   } 
}

// Redraw the status line or ruler of portal "po".
private void
statusLineOrRuler(Portal* po, Boole draw_ruler) {
   static int busy = false;
   int col = 0;
   int opt_scope = 0;
   //There is a tiny chance that this gets called recursively: When redrawing a status line 
   //triggers redrawing the ruler. Avoid trouble by forbidding recursion.
   if (busy || !po)
      return;
      
   Byte buf[MAXPATHL];
   busy = true;
   
   int row = statusline_row(po);
   Decoration deco;
   Unt fillchar = statusLineNextChar(OUT &deco, po);
   int maxwidth = po->width;
   Byte oname;
   CS stl;
   if (draw_ruler) {
      stl = p_ruf ? p_ruf : S"";
      oname = STATLINE_RULERFORMAT;
      //advance past any leading group spec - implicit in rulerColP
      if (*stl == '%') {
         if (*++stl == '-')
            stl++;
         if (atoi((char *)stl)) {
            while (EE_ISDIGIT(*stl))
               stl++;
         } 
         if (*stl++ != '(')
            stl = p_ruf;
      }
      col = rulerColP - (visibleColsG - maxwidth);
      if (col < (maxwidth + 1) / 2)
         col = (maxwidth + 1) / 2;
      maxwidth -= col;
   } else {
      oname = STATLINE_STATUSLINE;
      stl = po->o.statusLine;
      opt_scope = OPT_LOCAL;
   }
   col += po->windowCol;

   if (maxwidth <= 0)
      goto theend;

   //Temporarily reset @diff, we don't want a side effect from moving the cursor away and back.
   Portal* tgtPo = po ? po : curPor;
   Boole diffSaved = tgtPo->o.diff;
   tgtPo->o.diff = false;

   //Make a copy, because the statusline may include a function call that
   //might change the option value and free the memory.
   stl = copyStr(stl);

   startDrawingHilite(deco.hiId);
   Arr(StatusLineHilite) labels;
   int width = bookRenderStatusLine(
      tgtPo, OUT buf, sizeof(buf), stl ? stl : S"", oname, opt_scope,
      fillchar, maxwidth, OUT &labels
   );
   eeglFree(stl);
   tgtPo->o.diff = diffSaved;

   // Make all characters printable.
   CS p = sanitizeStr(buf);
   int len;
   if (p) {
      len = eeSnprintf(buf, sizeof(buf), "%s", p);
      eeglFree(p);
   }  else
      len = (int)STRLEN(buf);

   // fill up with "fillchar"
   while (width < maxwidth && len < (int)sizeof(buf) - 1) {
      len += mb_char2bytes(fillchar, buf + len);
      ++width;
   }
   buf[len] = ZERO;

   // Draw each snippet
   p = buf;
   drawTextLen(p, len, row, col, deco.flags);
   col += eeglStrNsize(p, len);

theend:
   busy = false;
}

// Output a single character directly to the screen and update screenTextP.
pub void
screen_putchar(int c, Unt row, Unt col, char decoFlags) {
   Byte buf[MB_MAXBYTES + 1];

   buf[mb_char2bytes(c, buf)] = ZERO;
   drawText(buf, row, col, decoFlags);
}

//Convert the character at screen position "off" to a sequence of bytes.
//Include the composing characters. "buf" must at least have the length MB_MAXBYTES + 1.
//Only to be used when screenLinesUCG[off] != 0. Return the produced number of bytes.
private int
utfc_char2bytes(int off, CS buf) {
   int len = mb_char2bytes(screenLinesUCG[off], buf);
   for (Unt i = 0; i < MAX_COMBINED_SYMBOLS; ++i) {
      if (screenLinesCG[MAX_COMBINED_SYMBOLS*off + i] == 0)
          break;
      len += mb_char2bytes(screenLinesCG[MAX_COMBINED_SYMBOLS*off + i], buf + len);
   }
   return len;
}


//Get a single character directly from screenTextP into "bytes", which must
//have a size of "MB_MAXBYTES + 1".
//If "deco" is not NULL, return the character's decoration flags into "*deco".
pub void
screen_getbytes(int row, int col, Byte* bytes, OUT Byte* decoFlags) {
   // safety check
   if (!screenTextP || row >= screenLinesRowsG || col >= screenLinesColsG)
      return;

   Unt off = lineStartsP[row] + col;
   if (decoFlags)
      *decoFlags = screenDecosP[off].flags;
   bytes[0] = screenTextP[off];
   bytes[1] = ZERO;

   if (screenLinesUCG[off] != 0)
      bytes[utfc_char2bytes(off, bytes)] = ZERO;
}

// Return true if composing characters for screen posn "off" differs from
// composing characters in "characterCombiner". Only to be used when screenLinesUCG[off] != 0.
private int
screen_comp_differs(int off, int* characterCombiner) {
   for (Unt i = 0; i < MAX_COMBINED_SYMBOLS; ++i) {
      if (screenLinesCG[MAX_COMBINED_SYMBOLS*off + i] != (Unt)characterCombiner[i])
         return true;
      if (characterCombiner[i] == 0)
         break;
   }
   return false;
}

//Put string '*text' on the screen at position 'row' and 'col', with
//decorations 'deco', and update screenTextP[] and screenDecosP[].
//Note: only outputs within one row, message is truncated at screen boundary!
//Note: if screenTextP[], row and/or col is invalid, nothing is done.
pub void
drawText(CS text, Unt row, Unt col, Byte decoFlags){
   drawTextLen(text, -1, row, col, decoFlags);
}

// Like drawText(), but output "text[len]".  When "len" is -1, output up to a ZERO.
pub void
drawTextLen(
   CS text,
   int textlen,
   Unt row,
   int col,
   Byte decoFlagsArg
) {
   CS ptr = text;
   int len = textlen;
   int mbyte_blen = 1;
   int mbyte_cells = 1;
   int characterCombiner[MAX_COMBINED_SYMBOLS];
   int clear_next_cell = false;
   int force_redraw_this;
   int force_redraw_next = false;
   int need_redraw;
   Byte decoFlags = decoFlagsArg;

   //Safety check. The check for negative row and column is to fix issue
   //#4102. TODO: find out why row/col could be negative.
   if (!screenTextP
         || (int)row >= screenLinesRowsG || row == UNT 
         || (int)col >= screenLinesColsG || col < 0)
      return;
   Unt off = lineStartsP[row] + col;

   while ((int)col < screenLinesColsG && (len < 0 || (int)(ptr - text) < len) && *ptr != ZERO) {
      Unt c = *ptr;
      // check if this is the first byte of a multibyte
      mbyte_blen = len > 0 ? utfCharLen_len(ptr, (int)((text + len) - ptr)) : utfCharLen(ptr);
      Unt u8c = len >= 0
            ? utfc_ptr2char_len(ptr, characterCombiner, (int)((text + len) - ptr))
            : utfc_ptr2char(ptr, characterCombiner);
      mbyte_cells = mb_char2cells(u8c);
      if ((int)col + mbyte_cells > screenLinesColsG) {
          // Only 1 cell left, but character requires 2 cells:
          // display a '>' in the last column to avoid wrapping.
          c = '>';
          mbyte_cells = 1;
      }

      force_redraw_this = force_redraw_next;
      force_redraw_next = false;

      need_redraw = screenTextP[off] != c
         || (mbyte_cells == 2 && screenTextP[off + 1] != 0)
         || ((screenLinesUCG[off] != (Unt)(c < 0x80 && characterCombiner[0] == 0 ? 0 : u8c)
               || (screenLinesUCG[off] != 0 && screen_comp_differs(off, characterCombiner)))
            )
         || screenDecosP[off].flags != decoFlags;

      if ((need_redraw || force_redraw_this) && !skipForPopup(row, col)) {
         // The bold trick makes a single row of pixels appear in the next character. When a bold 
         // character is removed, the next character should be redrawn too.  This happens for our own
         // GUI and for some xterms.
         if (need_redraw && screenTextP[off] != ' ' && ( term_is_xterm)) {
            if ((getDecoFlags(screenDecosP[off].hiId) & DECO_BOLD) != 0)
               force_redraw_next = true;
         }
         if (clear_next_cell)
            clear_next_cell = false;

         screenTextP[off] = c;
         screenDecosP[off].flags = decoFlags;
         screenDecosP[off].hiId = 0;
         screenColS[off] = -1;
         if (c < 0x80 && characterCombiner[0] == 0)
            screenLinesUCG[off] = 0;
         else {
            screenLinesUCG[off] = u8c;
            for (Unt i = 0; i < MAX_COMBINED_SYMBOLS; ++i) {
               screenLinesCG[MAX_COMBINED_SYMBOLS*off + i] = characterCombiner[i];
               if (characterCombiner[i] == 0)
                  break;
            }
         }
         if (mbyte_cells == 2) {
            screenTextP[off + 1] = 0;
            screenDecosP[off + 1].flags = decoFlags;
            screenDecosP[off + 1].hiId = 0;
            screenColS[off + 1] = -1;
         }
         singleChar(off, row, col);
      }
      off += mbyte_cells;
      col += mbyte_cells;
      ptr += mbyte_blen;
      if (clear_next_cell) {
         // This only happens at the end, display one space next. Keep the decorations from before
         ptr = S" ";
         len = -1;
         decoFlags = screenDecosP[off].flags;
      }
   }

   //If we detected the next character needs to be redrawn, but the text
   //doesn't extend up to there, update the character here.
   if (force_redraw_next && (int)col < screenLinesColsG && !skipForPopup(row, col)) {
      singleChar(off, row, col);
   }
}

// Prepare for @hlsearch hiliting.
private void
start_search_hl(void) {
   if (!p_hls || hiliteSearchG)
      return;

   end_search_hl();  // just in case it wasn't called before
   last_pat_prog(&screenSearchP.rm);
   screenSearchP.extra = OVERLAY_DECO_INVERT;
}

// Clean up for @hlsearch hiliting.
pub void
end_search_hl(void) {
   if (!screenSearchP.rm.regprog)
      return;

   eeRegFree(screenSearchP.rm.regprog);
   screenSearchP.rm.regprog = NULL;
}

pub void
drawStopHilite(void) {
   if (activeDecoP.hiId == SHORT) {
      return;
   }
   
   Boole do_ME = false;       // output KS_ME code

   // Often all ending-codes are equal to KS_ME. Avoid outputting the same sequence several times
   int is_under = (activeDecoP.flags & (DECO_UNDERCURL));
   if (is_under && *termCodesG[KS_UCE] != ZERO) {
      if (STRCMP(termCodesG[KS_UCE], termCodesG[KS_ME]) == 0)
         do_ME = true;
      else
         out_str(termCodesG[KS_UCE]);
   }
   if ((activeDecoP.flags & DECO_UNDERLINE) != 0 || (is_under && *termCodesG[KS_UCE] == ZERO)) {
      if (STRCMP(termCodesG[KS_UE], termCodesG[KS_ME]) == 0)
         do_ME = true;
      else {
         out_str(termCodesG[KS_UE]);
         
      } 
   }
   if ((activeDecoP.flags & DECO_ITALIC) != 0) {
      if (STRCMP(termCodesG[KS_CZR], termCodesG[KS_ME]) == 0)
         do_ME = true;
      else
         out_str(termCodesG[KS_CZR]);
   }
   if (do_ME || (activeDecoP.flags & (DECO_BOLD | DECO_INVERSE)) != 0)
      out_str(termCodesG[KS_ME]);

   termApplyFgColor(defaultFgColorG);
   termApplyBgColor(defaultBgColorG);
   activeDecoP = defaultDecoP;
}

//Reset the colors for a cterm. Used when leaving Eegl. The machine-specific code may override this
//again.
pub void
reset_cterm_colors(void) {
   // set Normal cterm colors
   out_str(termCodesG[KS_OP]);
   activeDecoP.hiId = 0;
   if (currentlyBoldG) {
      out_str(termCodesG[KS_ME]);
   }
}

//Put character screenTextP["off"] on the screen at position "row" and "col",
//using the decorations from screenDecosP["off"].
private void
singleChar(Unt off, int row, int col) {
   //Check for illegal values, just in case (could happen just after resizing)
   if (row >= screenLinesRowsG || col >= screenLinesColsG)
      return;

   //Outputting a character in the last cell on the screen may scroll the screen up. Only do it 
   //when the "xn" termcap property is set, otherwise mark the character invalid (update it when 
   //scrolled up).
   if (*termCodesG[KS_XN] == ZERO && row == screenLinesRowsG - 1 && col == screenLinesColsG - 1) {
      screenDecosP[off].flags = 0;
      screenColS[off] = -1;
      return;
   }

   // Stop hiliting first, so it's easier to move the cursor.
   Short hiId;
   if (screen_charDeco != DECO_NONE)
      hiId = screen_charDeco;
   else
      hiId = screenDecosP[off].hiId;
   if (activeDecoP.hiId != hiId)
      drawStopHilite();

   windgoto(row, col);

   if (activeDecoP.hiId != hiId)
      startDrawingHilite(hiId);

   if (screenLinesUCG[off] != 0) {
      Byte buf[MB_MAXBYTES + 1];
      if (utf_ambiguous_width(screenLinesUCG[off])) {
         // not sure where the cursor is after drawing the ambiguous width character
         screenCursColG = 9999;
      } ei (mb_char2cells(screenLinesUCG[off]) > 1)
         ++screenCursColG;

      // Convert the UTF-8 character to bytes and write it.
      buf[utfc_char2bytes(off, buf)] = ZERO;
      out_str(buf);
   } else {
      out_char(screenTextP[off]);
   }
   screenCursColG++;
}

// Draw a rectangle of the screen, inverted when "invert" is true.
// This uses the contents of screenTextP[] and doesn't change it.
pub void
screen_draw_rectangle(int row, int col, int height, int width, Boole invert) {
   if (!screenTextP)
      return;

   if (invert)
      screen_charDeco = DECO_INVERSE;
   for (int r = row; r < row + height; ++r) {
      int off = lineStartsP[r];
      for (int c = col; c < col + width; ++c) {
         if (!skipForPopup(r, c))
            singleChar(off + c, r, c);
      }
   }
   screen_charDeco = DECO_NONE;
}

// Redraw the characters for a vertically split portal
private void
redraw_block(int row, int end, Portal* po) {
   clip_may_clear_selection(row, end - 1);

   int col;
   int width;
   if (!po) {
      col = firstPor->windowCol;
      width = topframeG->width;
   } else {
      col = po->windowCol;
      width = po->width;
   }
   screen_draw_rectangle(row, col, end - row, width, false);
}

private void
space_to_screenline(int off, Byte decoFlags) {
   screenTextP[off] = ' ';
   screenDecosP[off].flags = decoFlags;
   screenColS[off] = -1;
   screenLinesUCG[off] = 0;
}

//Fill the screen from "start_row" to "end_row" (exclusive), from "start_col" to "end_col" 
//(exclusive) with character "c1" in first column followed by "c2" in the other columns. 
//Use decorations "decoFlags".
pub void
fillRowsWithTwoChars(
   Unt start_row,
   Unt end_row,
   Unt start_col,
   Unt end_col,
   Unt c1,
   Unt c2,
   Decoration deco
){
   Unt col;
   int off;
   int end_off;
   int c;
   int force_next = false;

   if ((int)end_row > screenLinesRowsG)      // safety check
      end_row = screenLinesRowsG;
   if ((int)end_col > screenLinesColsG)   // safety check
      end_col = screenLinesColsG;
   if (!screenTextP || start_row >= end_row || start_col >= end_col)
      return;

   //it's a "normal" terminal when not in a cterm
   for (Unt row = start_row; row < end_row; ++row) {
      //Try to use delete-line termcap code, when no decorations or in a "normal" terminal, where 
      //a bold/italic space is just a space.
      Boole did_delete = false;
      if (c2 == ' '
         && end_col == visibleColsG
         && can_clear(termCodesG[KS_CE])
         && (deco.flags == 0)
      ){
         // check if we really need to clear something
         col = start_col;
         if (c1 != ' ')         // don't clear first char
            ++col;

         off = lineStartsP[row] + col;
         end_off = lineStartsP[row] + end_col;

         // skip blanks (used often, keep it fast!)
         while (off < end_off && screenTextP[off] == ' '
                && screenDecosP[off].flags == 0 && screenLinesUCG[off] == 0
         ) {
            ++off;
         } 
         if (off < end_off) {    // something to be cleared
            col = off - lineStartsP[row];
            drawStopHilite();
            term_windgoto(row, col);// clear rest of this screen line
            out_str(termCodesG[KS_CE]);
            screen_start();      // don't know where cursor is now
            col = end_col - col;
            while (col--) {     // clear chars in screenTextP
               space_to_screenline(off, 0);
               ++off;
            }
         }
         did_delete = true;      // the chars are cleared now
      }

      off = lineStartsP[row] + start_col;
      c = c1;
      for (col = start_col; col < end_col; ++col) {
         if ((screenTextP[off] != c
             || ((int)screenLinesUCG[off] != (c >= 0x80 ? c : 0))
             || screenDecosP[off].flags != deco.flags
             || mustRedrawG == UPD_CLEAR  // screen clear pending
             || force_next
             )
             // Skip if under a(nother) popup.
             && !skipForPopup(row, col)
         ) {
            // The bold trick may make a single row of pixels appear in the next character.  When a 
            // bold character is removed, the next character should be redrawn too.  This happens for 
            // our own GUI and for some xterms.
            if (term_is_xterm) {
               if (screenTextP[off] != ' ' && (screenDecosP[off].flags & DECO_BOLD) != 0)
                  force_next = true;
               else
                  force_next = false;
            }
            screenTextP[off] = c;
            if (c >= 0x80) {
               screenLinesUCG[off] = c;
               screenLinesCG[MAX_COMBINED_SYMBOLS*off] = 0;
            } else
               screenLinesUCG[off] = 0;
            screenDecosP[off].flags = deco.flags;
            if (screenDecosP[off].hiId == SHORT) {
               screenDecosP[off] = deco;
            }
            if (!did_delete || c != ' ') {
               singleChar(off, row, col);
            } 
         }
         screenColS[off] = -1;
         ++off;
         if (col == start_col) {
            if (did_delete)
               break;
            c = c2;
          }
      }
      if (end_col == visibleColsG)
         lineWrapsP[row] = false;
      if (row == visibleRowsG - 1) {    // overwritten the command line
         redrawCommlineG = true;
         if (start_col == 0 && end_col == visibleColsG
                && c1 == ' ' && c2 == ' ' && deco.flags == 0 && !popup_overlaps_cmdline()) {
            mustClearCommlineG = false;   // command line has been cleared
         } 
         if (start_col == 0)
            isModeDisplayedG = false; // mode cleared or overwritten
      }
   }
}

// Check if there should be a delay. Used before clearing or redrawing the screen or the commline
pub void
check_for_delay(int check_msg_scroll) {
   if ((emsg_on_display || (check_msg_scroll && msg_scroll))
       && !did_wait_return
       && emsg_silent == 0
       && !in_assert_fails
   ) {
      out_flush();
      ui_delay(1006L, true);
      emsg_on_display = false;
      if (check_msg_scroll)
         msg_scroll = false;
   }
}

// Init tabIndsG[] to zero: Clicking outside of tabs has no effect.
private void
clear_tabIndsG(void) {
   for (int scol = 0; scol < visibleColsG; ++scol)
      tabIndsG[scol] = 0;
}

//screen_valid -  allocate screen buffers if size changed
//  If "doclear" is true: clear screen if it has been resized.
//  Returns true if there is a valid screen to write to.
//  Returns false when starting up and screen not initialized yet.
pub Boole
screen_valid(Boole doclear) {
   screenalloc(doclear);      // allocate screen buffers if size changed
   return (screenTextP != NULL);
}

//Resize the shell to visibleRowsG and visibleColsG.
//Allocate screenTextP[] and associated items.
//
//There may be some time between setting visibleRowsG and visibleColsG and (re)allocating
//screenTextP[]. This happens when starting up and when (manually) changing
//the shell size. Always use screenLinesRowsG and screenLinesColsG to access items
//in screenTextP[]. Use visibleRowsG and visibleColsG for positioning text etc. where the
//final size of the shell is needed.
pub void
screenalloc(Boole doclear) {
   int new_row, old_row;
   Portal* po;
   static int entered = false;      // avoid recursiveness
   static int done_outofmem_msg = false;   // did outofmem message
   int retry_count = 0;
   int found_null;

retry:
   //Allocation of the screen buffers is done only when the size changes and
   //when visibleRowsG and visibleColsG have been set and we have started doing full screen stuff.
   if ((screenTextP
         && visibleRowsG == screenLinesRowsG
         && visibleColsG == screenLinesColsG
         && screenLinesUCG
        )
          || visibleRowsG == 0
          || visibleColsG == 0
          || (!fullScreenG && !screenTextP)) {
      return;
   } 

   // It's possible that we produce an out-of-memory message below, which
   // will cause this function to be called again.  To break the loop, just return here.
   if (entered)
      return;
   entered = true;

   // Note: the portal sizes are updated before reallocating the arrays, so we must not redraw here!
   ++isRedrawingDisabledG;

   win_new_shellsize();    // fit the portals in the new sized shell

   computeColumnsForRulerAndCommand();      // recompute columns for shown command and ruler

   //We're changing the size of the screen.
   //- Allocate new arrays for screenTextP and screenDecosP.
   //- Move lines from the old arrays into the new arrays, clear extra
   //   lines (unless the screen is going to be cleared).
   //- Free the old arrays.
   //
   //If anything fails, make screenTextP NULL, so we don't do anything!
   //Continuing with the old screenTextP may result in a crash, because the size is wrong.
   Tab* t; 
   FOR_ALL_TAB_PORTALS(t, po)
      freePortalLsizes(po);
   for (Unt i = 0; i < AUCMD_PORTAL_COUNT; ++i) {
      if (autoCommPortG[i].port)
          freePortalLsizes(autoCommPortG[i].port);
   } 
   // global popup portals
   FOR_ALL_POPUPPORTS(po) {
      freePortalLsizes(po);
   } 
    // tab-local popup portals
   FOR_ALL_TABS(t) {
      FOR_ALL_POPUPPORTS_IN_TAB(t, po)
          freePortalLsizes(po);
   } 

   Arr(Byte) newScreenLines = LALLOC_MULT(Byte, (visibleRowsG + 1) * visibleColsG);
   
   //array of  blocks of MAX_COMBINED_SYMBOLS
   Arr(Unt) new_screenLinesCG = 
      LALLOC_CLEAR_MULT(Unt, MAX_COMBINED_SYMBOLS * (visibleRowsG + 1) * visibleColsG);
   Arr(Unt) new_screenLinesUCG = LALLOC_MULT(Unt, (visibleRowsG + 1) * visibleColsG);
   Arr(Decoration) newScreenDecos = LALLOC_MULT(Decoration, (visibleRowsG + 1) * visibleColsG);
   // Clear screenColS to avoid a warning for uninitialized memory in jump_to_mouse().
   Arr(ColNr) newScreenCols = LALLOC_CLEAR_MULT(ColNr, (visibleRowsG + 1) * visibleColsG);
   Arr(Unt) newLineStarts = LALLOC_MULT(unsigned, visibleRowsG);
   Arr(Boole) newLineWraps = LALLOC_MULT(Boole, visibleRowsG);
   Arr(Unt) new_tabInds = LALLOC_MULT(Unt, visibleColsG);
   Arr(Short) newPopupMask = LALLOC_MULT(Short, visibleRowsG * visibleColsG);
   Arr(Short) newPopupMaskNext = LALLOC_MULT(Short, visibleRowsG * visibleColsG);
   Arr(Byte) newPopupTransparency = LALLOC_MULT(Byte, visibleRowsG * visibleColsG);

   FOR_ALL_TAB_PORTALS(t, po) {
      allocLinesPortal(po);
   }
   for (Unt i = 0; i < AUCMD_PORTAL_COUNT; ++i) {
      if (autoCommPortG[i].port && autoCommPortG[i].port->lines == NULL) {
         allocLinesPortal(autoCommPortG[i].port);
      }
   }
   // global popup portals
   FOR_ALL_POPUPPORTS(po) {
      allocLinesPortal(po);
   } 
   // tab-local popup portals
   FOR_ALL_TABS(t) {
      FOR_ALL_POPUPPORTS_IN_TAB(t, po) {
         allocLinesPortal(po);
      } 
   } 

   found_null = false;
   if (!new_screenLinesCG) {
      found_null = true;
   }
   if (!newScreenLines
       || (!new_screenLinesUCG || found_null)
       || !newScreenDecos
       || !newScreenCols
       || !newLineStarts
       || !newLineWraps
       || !new_tabInds
       || !newPopupMask
       || !newPopupMaskNext
       || !newPopupTransparency
   ) {
      if (screenTextP || !done_outofmem_msg) {
         // guess the size
         do_outofmem_msg((Ulong)((visibleRowsG + 1) * visibleColsG));

         // Remember we did this to avoid getting outofmem messages over and over again.
         done_outofmem_msg = true;
      }
      EE_CLEAR(newScreenLines);
      EE_CLEAR(new_screenLinesUCG);
      EE_CLEAR(new_screenLinesCG);
      EE_CLEAR(newScreenDecos);
      EE_CLEAR(newScreenCols);
      EE_CLEAR(newLineStarts);
      EE_CLEAR(newLineWraps);
      EE_CLEAR(new_tabInds);
      EE_CLEAR(newPopupMask);
      EE_CLEAR(newPopupMaskNext);
      EE_CLEAR(newPopupTransparency);
   } else {
      done_outofmem_msg = false;

      for (new_row = 0; new_row < visibleRowsG; ++new_row) {
         newLineStarts[new_row] = new_row * visibleColsG;
         newLineWraps[new_row] = false;

         (void)memset(
            newScreenLines + new_row * visibleColsG, ' ', (Unt)visibleColsG * sizeof(Byte)
         );
         (void)memset(
            new_screenLinesUCG + new_row * visibleColsG, 0, (Unt)visibleColsG * sizeof(Unt)
         );
         (void)memset(
             new_screenLinesCG + MAX_COMBINED_SYMBOLS * new_row * visibleColsG, 
             0, 
             MAX_COMBINED_SYMBOLS * (Unt)visibleColsG * sizeof(Unt) 
         );
         (void)memset(
               newScreenDecos + new_row * visibleColsG, 0, (Unt)visibleColsG * sizeof(char)
         );
         (void)memset(
               newScreenCols + new_row * visibleColsG, 0, (Unt)visibleColsG * sizeof(ColNr)
         );

         //If the screen is not going to be cleared, copy as much as
         //possible from the old screen to the new one and clear the rest
         //(used when resizing the portal at the "--more--" prompt or when
         //executing an external command, for the GUI).
         if (!doclear) {
            old_row = new_row + (screenLinesRowsG - visibleRowsG);
            if (old_row >= 0 && screenTextP) {
               int len = (screenLinesColsG < visibleColsG) ? screenLinesColsG : visibleColsG;
               
               // When switching to utf-8, don't copy characters, they may be invalid now.
               if (screenLinesUCG) {
                  MEMMOVE(newScreenLines + newLineStarts[new_row],
                     screenTextP + lineStartsP[old_row], (Unt)len * sizeof(Byte)
                  );
               } 
               if (screenLinesUCG) {
                  MEMMOVE(
                     new_screenLinesUCG + newLineStarts[new_row],
                     screenLinesUCG + lineStartsP[old_row],
                     (Unt)len * sizeof(Unt)
                  );
                  MEMMOVE(
                     new_screenLinesCG + MAX_COMBINED_SYMBOLS * newLineStarts[new_row],
                     screenLinesCG + MAX_COMBINED_SYMBOLS * lineStartsP[old_row], 
                     (Unt)len * sizeof(Unt) * MAX_COMBINED_SYMBOLS
                  );
               }
               MEMMOVE(
                  newScreenDecos + newLineStarts[new_row],
                  screenDecosP + lineStartsP[old_row], (Unt)len
               );
               MEMMOVE(
                  newScreenCols + newLineStarts[new_row],
                  screenDecosP + lineStartsP[old_row], (Unt)len * sizeof(ColNr)
               );
            }
         }
      }
      // Use the last line of the screen for the current line.
      currScreenLineS = newScreenLines + visibleRowsG * visibleColsG;
      memset(newPopupMask, 0, visibleRowsG * visibleColsG * sizeof(short));
      memset(newPopupTransparency, 0, visibleRowsG * visibleColsG * sizeof(char));
   }

   free_screenlines();

   // NOTE: this may result in all pointers nullifying.
   screenTextP = newScreenLines;
   screenLinesUCG = new_screenLinesUCG;
   screenLinesCG = new_screenLinesCG;
   screenDecosP = newScreenDecos;
   screenColS = newScreenCols;
   lineStartsP = newLineStarts;
   lineWrapsP = newLineWraps;
   tabIndsG = new_tabInds;
   popupMaskG = newPopupMask;
   popupMaskNextG = newPopupMaskNext;
   popupTransparencyG = newPopupTransparency;
   needRefreshPopupMaskG = true;

   // It's important that screenLinesRowsG and screenLinesColsG reflect the actual
   // size of screenTextP[].  Set them before calling anything.
   screenLinesRowsG = visibleRowsG;
   screenLinesColsG = visibleColsG;

   drawSetMustRedraw(UPD_CLEAR);   // need to clear the screen later
   if (doclear)
      screenclear2(true);
   clear_tabIndsG();


   entered = false;
   if (isRedrawingDisabledG > 0)
      --isRedrawingDisabledG;

   // Do not apply autocommands more than 3 times to avoid an endless loop
   // in case applying autocommands always changes visibleRowsG or visibleColsG.
   if (starting == 0 && ++retry_count <= 3) {
      applyAutocomms(EVENT_EEGLRESIZED, NULL, NULL, false, curBook);
      // In rare cases, autocommands may have altered visibleRowsG or visibleColsG,
      // jump back to check if we need to allocate the screen again.
      goto retry;
   }
}

pub void
free_screenlines(void) {
   EE_CLEAR(screenLinesUCG);
   EE_CLEAR(screenLinesCG);
   EE_CLEAR(screenTextP);
   EE_CLEAR(screenDecosP);
   EE_CLEAR(screenColS);
   EE_CLEAR(lineStartsP);
   EE_CLEAR(lineWrapsP);
   EE_CLEAR(tabIndsG);
   EE_CLEAR(popupMaskG);
   EE_CLEAR(popupMaskNextG);
   EE_CLEAR(popupTransparencyG);
}

//Clear the screen. May delay if there is something the user should read. Allocated the screen for
//resizing if needed. Return true when the screen was actually cleared, false if all display
//cells were marked for updating.
pub int
screenclear(void) {
   check_for_delay(false);
   screenalloc(false);          // allocate screen buffers if size changed
   return screenclear2(true);       // clear the screen
}

// Do not clear the screen but mark everything for redraw.
private void
redraw_as_cleared(void) {
   screenclear2(false);
}

private int
screenclear2(Boole doclear) {
   if (starting == NO_SCREEN || !screenTextP)
      return false;
      
   int did_clear = false;

   activeDecoP = defaultDecoP;   // force setting the None colors
   drawStopHilite();   // don't want hiliting here

   // disable selection without redrawing it
   clip_scroll_selection(9999);

   // blank out screenTextP
   for (int i = 0; i < visibleRowsG; ++i) {
      lineclear(lineStartsP[i], (int)visibleColsG, 0);
      lineWrapsP[i] = false;
   }

   if (doclear && can_clear(termCodesG[KS_CL])) {
      out_str(termCodesG[KS_CL]);      // clear the display
      did_clear = true;
      mustClearCommlineG = false;
      isModeDisplayedG = false;
   } else {
      // can't clear the screen, mark all chars with invalid decorations
      for (int i = 0; i < visibleRowsG; ++i)
         lineinvalid(lineStartsP[i], (int)visibleColsG);
      mustClearCommlineG = true;
   }

   isScreenClearedP = true;   // can use contents of screenTextP now

   markFollowingPortalsForRedraw(firstPor);   // redraw all regular portals
   redrawCommlineG = true;
   needRedrawTabpanelG = true;
   if (mustRedrawG == UPD_CLEAR)   // no need to clear again
      mustRedrawG = UPD_NOT_VALID;
   msg_scrolled = 0;      // compute_cmdrow() uses this
   compute_cmdrow();
   popup_redraw_all();      // redraw all popup portals
   msgRowG = commlineRowG;   // put cursor on last line for messages
   msgColG = 0;
   screen_start();      // don't know where cursor is now
   msg_didany = false;
   msg_didout = false;

   return did_clear;
}

// Clear one line in screenTextP.
private void
lineclear(unsigned off, int width, Unt hiId) {
   (void)memset(screenTextP + off, ' ', (Unt)width * sizeof(Byte));
   (void)memset(screenLinesUCG + off, 0, (Unt)width * sizeof(Unt));
   (void)memset(screenDecosP + off, hiId, (Unt)width);
   (void)memset(screenColS + off, -1, (Unt)width * sizeof(ColNr));
}

// Mark one line in screenTextP invalid by setting the decorations to an invalid value.
private void
lineinvalid(Unt off, int width) {
   (void)memset(screenDecosP + off, -1, (Unt)width);
   (void)memset(screenColS + off, -1, (Unt)width * sizeof(ColNr));
}

// To be called when chars were sent to the terminal directly, outputting test on "screen_lnum".
pub void
drawLineWasClobbered(int screen_lnum) {
   lineinvalid(lineStartsP[screen_lnum], (int)visibleColsG);
}

// Copy part of a Screenline for vertically split portal "po".
private void
linecopy(int to, int from, Portal* po) {
   Unt offTo = lineStartsP[to] + po->windowCol;
   Unt offFrom = lineStartsP[from] + po->windowCol;

   MEMMOVE(screenTextP + offTo, screenTextP + offFrom, po->width * sizeof(Byte));

   MEMMOVE(screenLinesUCG + offTo, screenLinesUCG + offFrom, po->width * sizeof(Unt));
   MEMMOVE(
      screenLinesCG + MAX_COMBINED_SYMBOLS*offTo, 
      screenLinesCG + MAX_COMBINED_SYMBOLS*offFrom, 
      MAX_COMBINED_SYMBOLS * po->width * sizeof(Unt)
   );
   MEMMOVE( screenDecosP + offTo, screenDecosP + offFrom, po->width);
   MEMMOVE(screenColS + offTo, screenColS + offFrom, po->width * sizeof(ColNr));
}

//Return true if clearing with term string "p" would work.
//It can't work when the string is empty or it won't set the right background.
//Don't clear to end-of-line when there are popups, it may cause flicker.
pub int
can_clear(CS p) {
    return (*p != ZERO 
             && (*termCodesG[KS_UT] != ZERO)
             && !(p == termCodesG[KS_CE] && popup_visible)
    );
}

//Reset cursor position. Use whenever cursor was moved because of outputting
//something directly to the screen (shell commands) or a terminal control code.
pub void
screen_start(void) {
   screenCursRowG = screenCursColG = 9999;
}

//Move the cursor to position "row","col" in the screen.
//This tries to find the most efficient way to move, minimizing the number of
//characters sent to the terminal.
pub void
windgoto(int row, int col) {
  int i;
  int plan;
  int cost;
  int wouldbe_col;
  int noinvcurs;
  CS bs;
  int goto_cost;

#define GOTO_COST   7   // assume a term_windgoto() takes about 7 chars
#define HIGHL_COST  5   // assume unhilite takes 5 chars

#define PLAN_LE    1
#define PLAN_CR    2
#define PLAN_NL    3
#define PLAN_WRITE 4
   // Can't use screenTextP unless initialized
   if (!screenTextP)
      return;
   if (col == screenCursColG && row == screenCursRowG)
      return;

   // Check for valid position.
   if (row < 0)   // portal without text lines?
      row = 0;
   if (row >= screenLinesRowsG)
      row = screenLinesRowsG - 1;
   if (col >= screenLinesColsG)
      col = screenLinesColsG - 1;

   // check if no cursor movement is allowed in hilite mode
   if (activeDecoP.flags != 0 && *termCodesG[KS_MS] == ZERO)
      noinvcurs = HIGHL_COST;
   else
      noinvcurs = 0;
   goto_cost = GOTO_COST + noinvcurs;

   //Plan how to do the positioning:
   //1. Use CR to move it to column 0, same row.
   //2. Use termCodesG[KS_LE] to move it a few columns to the left.
   //3. Use NL to move a few lines down, column 0.
   //4. Move a few columns to the right with termCodesG[KS_ND] or by writing chars.
   //
   //Don't do this if the cursor went beyond the last column, the cursor
   //position is unknown then (some terminals wrap, some don't )
   //
   //First check if the hiliting decorations allow us to write
   //characters to move the cursor to the right.
   if (row >= screenCursRowG && screenCursColG < visibleColsG) {
      // If the cursor is in the same row, bigger col, we can use CR or termCodesG[KS_LE].
      bs = NULL;
      char activeDeco = activeDecoP.flags;
      if (row == screenCursRowG && col < screenCursColG) {
         bs = termCodesG[KS_LE];          // "cursor left"
         if (*bs)
            cost = (screenCursColG - col) * (int)STRLEN(bs);
         else
            cost = 999;
         if (col + 1 < cost) {     // using CR is less characters
            plan = PLAN_CR;
            wouldbe_col = 0;
            cost = 1;          // CR is just one character
         } else {
            plan = PLAN_LE;
            wouldbe_col = col;
         }
         if (noinvcurs) {         // will stop hiliting
            cost += noinvcurs;
            activeDeco = 0;
         }
      }
      // If the cursor is above where we want to be, we can use CR LF.
      ei (row > screenCursRowG) {
         plan = PLAN_NL;
         wouldbe_col = 0;
         cost = (row - screenCursRowG) * 2;  // CR LF
         if (noinvcurs) {         // will stop hiliting
            cost += noinvcurs;
            activeDeco = 0;
         }
      }

      // If the cursor is in the same row, smaller col, just use write.
      else {
         plan = PLAN_WRITE;
         wouldbe_col = screenCursColG;
         cost = 0;
      }

      //Check if any characters that need to be written have the correct decorations. Also avoid 
      //UTF-8 characters.
      i = col - wouldbe_col;
      Decoration* dec;
      if (i > 0)
         cost += i;
      if (cost < goto_cost && i > 0) {
         // Check if the decorations are correct without additionally stopping hiliting
         dec = screenDecosP + lineStartsP[row] + wouldbe_col;
         for (; i && dec->flags == activeDeco; dec++)
            --i;
         if (i != 0) {
            // Try if it works when hiliting is stopped here.
            if ((--dec)->flags == 0) {
               cost += noinvcurs;
               while (i && (dec++)->flags == 0)
                  --i;
            }
            if (i != 0)
                cost = 999;   // different decorations, don't do it
         }
         // Don't use an UTF-8 char for positioning, it's slow.
         for (i = wouldbe_col; i < col; ++i) {
            if (screenLinesUCG[lineStartsP[row] + i] != 0) {
               cost = 999;
               break;
            }
         } 
      }

      // We can do it without term_windgoto()!
      if (cost < goto_cost) {
         if (plan == PLAN_LE) {
            if (noinvcurs)
               drawStopHilite();
            while (screenCursColG > col) {
               out_str(bs);
               --screenCursColG;
            }
         } ei (plan == PLAN_CR) {
            if (noinvcurs)
               drawStopHilite();
            out_char('\r');
            screenCursColG = 0;
         } ei (plan == PLAN_NL) {
            if (noinvcurs)
               drawStopHilite();
            while (screenCursRowG < row) {
               out_char('\n');
               ++screenCursRowG;
            }
            screenCursColG = 0;
         }

         i = col - screenCursColG;
         if (i > 0) {
            // Use cursor-right if it's one character only.  Avoids removing a line of pixels from 
            // the last bold char, when using the bold trick in the GUI.
            if (termCodesG[KS_ND][0] != ZERO && termCodesG[KS_ND][1] == ZERO) {
                while (i-- > 0)
               out_char(*termCodesG[KS_ND]);
            } else {
               int off = lineStartsP[row] + screenCursColG;
               while (i-- > 0) {
                  if (getDecoFlags(screenDecosP[off].hiId) != activeDecoP.flags)
                     drawStopHilite();
                  out_char(screenTextP[off]);
                  ++off;
               }
            }
         }
      }
   } else
      cost = 999;

   if (cost >= goto_cost) {
      if (noinvcurs)
         drawStopHilite();
      if (row == screenCursRowG && (col > screenCursColG) && *termCodesG[KS_CRI] != ZERO)
         term_cursor_right(col - screenCursColG);
      else
         term_windgoto(row, col);
   }
   screenCursRowG = row;
   screenCursColG = col;
}

// Set cursor to its position in the current portal.
pub void
setcursor(void) {
   setcursor_mayforce(false);
}

// Set cursor to its position in the current portal. When "force" is true also when not redrawing.
pub void
setcursor_mayforce(int force) {
   if (force || redrawing()) {
      validate_cursor();
      windgoto(curPor->windowRow + curPor->cursorRow, curPor->windowCol + (curPor->cursorCol));
   }
}


//Insert 'line_count' lines at 'row' in portal 'po'.
//If 'invalid' is true the po->lines[].bookLnum is invalidated.
//If 'mayclear' is true the screen will be cleared if it is faster than scrolling.
//Return FAIL if the lines are not inserted, OK for success.
pub int
insertLinesIntoPortal(
   Portal   *po,
   int      row,
   int      line_count,
   int      invalid,
   int      mayclear
) {
   if (invalid)
      po->validLines = 0;

   // with only a few lines it's not worth the effort
   if (po->height < 5)
      return FAIL;

   // with the popup menu visible this might not work correctly
   if (pum_visible())
      return FAIL;

   if (line_count > (int)po->height - row)
      line_count = (int)po->height - row;

   int retval = doPortalLines(po, row, line_count, mayclear, false, 0);
   if (retval != MAYBE)
      return retval;

   //If there is a next portal or a status line, we first try to delete the lines at the bottom 
   //to avoid messing what is after the portal. If this fails and there are following portals, 
   //don't do anything to avoid messing up those portals, better just redraw.
   Boole did_delete = false;
   if (screen_del_lines(0, po->windowRow + po->height - line_count,
              line_count, (int)visibleRowsG, false, 0, NULL) == OK
   ) {
      did_delete = true;
   } ei (po->next) {
      return FAIL;
   } 
   // if no lines deleted, blank the lines that will end up below the portal
   if (!did_delete) {
      po->statusLineNeedsRedraw = true;
      redrawCommlineG = true;
      int nextrow = po->windowRow + po->height + STATUS_HEIGHT;
      int lastrow = nextrow + line_count;
      if (lastrow > visibleRowsG)
         lastrow = visibleRowsG;
      fillRowsWithTwoChars(
         nextrow - line_count, lastrow - line_count, po->windowCol, (int)P_ENDCOL(po), ' ', ' ', 
         defaultDecoP
      );
   }

   if (drawInsertLines(0, po->windowRow + row, line_count, (int)visibleRowsG, 0, NULL) == FAIL) {
      // deletion will have messed up other portals
      if (did_delete) {
         po->statusLineNeedsRedraw = true;
         markFollowingPortalsForRedraw(po->next);
      }
      return FAIL;
   }

   return OK;
}

//Delete "line_count" portal lines at "row" in portal "po".
//If "invalid" is true curPor->lines[] is invalidated.
//If "mayclear" is true the screen will be cleared if it is faster than scrolling
//Return OK for success, FAIL if the lines are not deleted.
pub int
deleteLinesFromPortal(
   Portal   *po,
   int      row,
   int      line_count,
   int      invalid,
   int      mayclear,
   Unt      clearHiId       // for clearing lines
){
   if (invalid)
      po->validLines = 0;

   if (line_count > (int)po->height - row)
      line_count = (int)po->height - row;

   int retval = doPortalLines(po, row, line_count, mayclear, true, clearHiId);
   if (retval != MAYBE)
      return retval;

   if (screen_del_lines(0, po->windowRow + row, line_count,
               (int)visibleRowsG, false, clearHiId, NULL) == FAIL)
      return FAIL;

   //Try to put the status lines at the correct place. If we can't do that, they have to be redrawn.
   if (drawInsertLines(0, po->windowRow + po->height - line_count,
               line_count, (int)visibleRowsG, clearHiId, NULL) == FAIL
   ){
      po->statusLineNeedsRedraw = true;
      markFollowingPortalsForRedraw(po->next);
   }
   // If this is the last portal and there is no status line, redraw the command line later
   else
      redrawCommlineG = true;
   return OK;
}

//Common code for insertLinesIntoPortal() and deleteLinesFromPortal().
//Return OK or FAIL when the work has been done. Return MAYBE when not finished yet.
private int
doPortalLines(
   Portal   *po,
   int row,
   int line_count,
   int mayclear,
   int del,
   Unt clearHiId
) {
   if (!redrawing() || line_count <= 0)
      return FAIL;

   //When inserting lines would result in loss of command output, just redraw the lines.
   if (avoidLineInsertionS && !del)
      return FAIL;

   //only a few lines left: redraw is faster
   if (mayclear && visibleRowsG - line_count < 5 && po->width == topframeG->width) {
      if (!avoidLineInsertionS)
         screenclear();       // will set po->validLines to 0
      return FAIL;
   }

   //this doesn't work when there are popups visible
   if (popup_visible)
      return FAIL;

   //Delete all remaining lines
   if (row + line_count >= (int)po->height) {
      fillRowsWithTwoChars(po->windowRow + row, po->windowRow + po->height,
         po->windowCol, (int)P_ENDCOL(po), ' ', ' ', defaultDecoP);
      return OK;
   }

   //When scrolling, the message on the command line should be cleared, otherwise it will stay 
   //there forever. Don't do this when avoiding to insert lines.
   if (!avoidLineInsertionS)
      mustClearCommlineG = true;

   //If the terminal can set a scroll region, use that.
   //Always do this in a vertically split portal. This will redraw from screenTextP[] when 
   //t_CV isn't defined. That's faster than using drawLineOnScreen().
   //Don't use a scroll region when we are going to redraw the text, writing a character in the 
   //lower right corner of the scroll region may cause a scroll-up .
   if (scroll_region || po->width != topframeG->width) {
      if (scroll_region && (po->width == topframeG->width || *termCodesG[KS_CSV] != ZERO))
         scroll_region_set(po, row);
      int retval;
      if (del)
         retval = screen_del_lines(
            po->windowRow + row, 0, line_count, po->height - row, false, clearHiId, po
         );
      else
         retval = 
            drawInsertLines(po->windowRow + row, 0, line_count, po->height - row, clearHiId, po);
      if (scroll_region && (po->width == topframeG->width || *termCodesG[KS_CSV] != ZERO))
         scroll_region_reset();
      return retval;
   }

   if (po->next) // don't delete/insert on fast terminal
      return FAIL;

   return MAYBE;
}

//portal 'po' and everything after it is messed up, mark it for redraw
private void
markFollowingPortalsForRedraw(Portal* po) {
   while (po) {
      redrawPortLater(po, UPD_NOT_VALID);
      po->statusLineNeedsRedraw = true;
      po = po->next;
   }
   redrawCommlineG = true;
}

//The rest of the routines in this section perform screen manipulations. The given operation is 
//performed physically on the screen. The corresponding change is also made to the internal screen
//image. In this way, the editor anticipates the effect of editing changes on the appearance of 
//the screen. That way, when we call screenupdate a complete redraw isn't usually
//necessary. Another advantage is that we can keep adding code to anticipate
//screen changes, and in the meantime, everything still works.

//types for inserting or deleting lines
#define USE_T_CAL   1
#define USE_T_CDL   2
#define USE_T_AL    3
#define USE_T_CE    4
#define USE_T_DL    5
#define USE_T_SR    6
#define USE_NL      7
#define USE_T_CD    8
#define USE_REDRAW  9

//insert lines on the screen and update screenTextP[]
//"end" is the line after the scrolled part. Normally it is visibleRowsG.
//When scrolling region used "off" is the offset from the top for the region.
//"row" and "end" are relative to the start of the region.
//
//return FAIL for failure, OK for success.
pub int
drawInsertLines(
   int off,
   int row,
   int line_count,
   int end,
   int clearHiId,
   Portal* po       // NULL or portal to use width from
){
   int i;
   int j;
   int cursor_row;
   int cursor_col = 0;
   int type;
   int can_ce = can_clear(termCodesG[KS_CE]);
   //FAIL if
   //- there is no valid screen
   //- the line count is less than one
   //- the line count is more than 'ttyscroll'
   //- "end" is more than "visibleRowsG" (safety check, should not happen)
   //- redrawing for a callback and there is a modeless selection
   //- there is a popup portal
   if (!screen_valid(true)
        || line_count <= 0 || line_count > p_ttyscroll
        || end > visibleRowsG
        || (clipboard.state != SELECT_CLEARED && redrawingForCallbackS > 0)
        || popup_visible
   )
      return FAIL;

   //There are seven ways to insert lines:
   //0. When in a vertically split portal and t_CV isn't set, redraw the
   //   characters from screenTextP[].
   //1. Use termCodesG[KS_CD] (clear to end of display) if it exists and the result of
   //    the insert is just empty lines
   //2. Use termCodesG[KS_CAL] (insert multiple lines) if it exists and termCodesG[KS_AL] is not
   //    present or line_count > 1. It looks better if we do all the inserts at once.
   //3. Use termCodesG[KS_CDL] (delete multiple lines) if it exists and the result of the
   //    insert is just empty lines and termCodesG[KS_CE] is not present or line_count > 1.
   //4. Use termCodesG[KS_AL] (insert line) if it exists.
   //5. Use termCodesG[KS_CE] (erase line) if it exists and the result of the insert is
   //    just empty lines.
   //6. Use termCodesG[KS_DL] (delete line) if it exists and the result of the insert is
   //    just empty lines.
   //7. Use termCodesG[KS_SR] (scroll reverse) if it exists and inserting at row 0 and
   //    the 'da' flag is not set or we have clear line capability.
   //8. redraw the characters from screenTextP[].
   //
   //Careful: In a hpterm scroll reverse doesn't work as expected, it moves
   //the scrollbar for the portal. It does have insert line, use that if it exists.
   int result_empty = (row + line_count >= end);
   if (po && po->width != topframeG->width && *termCodesG[KS_CSV] == ZERO) {
      //Avoid that lines are first cleared here and then redrawn, which
      //results in many characters updated twice. This happens with CTRL-F
      //in a vertically split portal. With line-by-line scrolling USE_REDRAW should be faster.
      if (line_count > 3)
         return FAIL;
      type = USE_REDRAW;
   } ei (can_clear(termCodesG[KS_CD]) && result_empty)
      type = USE_T_CD;
   ei (*termCodesG[KS_CAL] != ZERO 
         && (line_count > 1 || *termCodesG[KS_AL] == ZERO)
   )
      type = USE_T_CAL;
   ei (*termCodesG[KS_CDL] != ZERO && result_empty && (line_count > 1 || !can_ce))
      type = USE_T_CDL;
   ei (*termCodesG[KS_AL] != ZERO)
      type = USE_T_AL;
   ei (can_ce && result_empty)
      type = USE_T_CE;
   ei (*termCodesG[KS_DL] != ZERO && result_empty)
      type = USE_T_DL;
   ei (*termCodesG[KS_SR] != ZERO && row == 0 && (*termCodesG[KS_DA] == ZERO || can_ce))
      type = USE_T_SR;
   else
      return FAIL;

   //For clearing lines, screen_del_lines() is used. This will also take care of t_db if necessary
   if (type == USE_T_CD || type == USE_T_CDL || type == USE_T_CE || type == USE_T_DL)
      return screen_del_lines(off, row, line_count, end, false, 0, po);

   //If text is retained below the screen, first clear or delete as many
   //lines at the bottom of the portal as are about to be inserted so that
   //the deleted lines won't later surface during a screen_del_lines.
   if (*termCodesG[KS_DB])
      screen_del_lines(off, end - line_count, line_count, end, false, 0, po);

   //Remove a modeless selection when inserting lines halfway the screen
   //or not the full width of the screen.
   if (off + row > 0 || (po && po->width != topframeG->width))
      clip_clear_selection(&clipboard);
   else
      clip_scroll_selection(-line_count);

   if (po && po->windowCol != 0 && *termCodesG[KS_CSV] != ZERO && *termCodesG[KS_CCS] == ZERO)
      cursor_col = po->windowCol;

   if (*termCodesG[KS_CCS] != ZERO)      // cursor relative to region
      cursor_row = row;
   else
      cursor_row = row + off;

   // Shift lineStartsP[] line_count down to reflect the inserted lines.
   //Clear the inserted lines in screenTextP[].
   row += off;
   end += off;
   for (i = 0; i < line_count; ++i) {
      if (po && po->width != topframeG->width) {
         //need to copy part of a line
         j = end - 1 - i;
         while ((j -= line_count) >= row)
            linecopy(j + line_count, j, po);
         j += line_count;
         if (can_clear(S" "))
            lineclear(lineStartsP[j] + po->windowCol, po->width, clearHiId);
         else
            lineinvalid(lineStartsP[j] + po->windowCol, po->width);
         lineWrapsP[j] = false;
      } else {
         j = end - 1 - i;
         Unt temp = lineStartsP[j];
         while ((j -= line_count) >= row) {
            lineStartsP[j + line_count] = lineStartsP[j];
            lineWrapsP[j + line_count] = lineWrapsP[j];
         }
         lineStartsP[j + line_count] = temp;
         lineWrapsP[j + line_count] = false;
         if (can_clear(S" "))
            lineclear(temp, (int)visibleColsG, clearHiId);
         else
            lineinvalid(temp, (int)visibleColsG);
      }
   }

   drawStopHilite();
   windgoto(cursor_row, cursor_col);
   if (clearHiId != 0)
      startDrawingHilite(clearHiId);

   // redraw the characters
   if (type == USE_REDRAW)
      redraw_block(row, end, po);
   ei (type == USE_T_CAL) {
      term_append_lines(line_count);
      screen_start();      // don't know where cursor is now
   } else {
      for (i = 0; i < line_count; i++) {
         if (type == USE_T_AL) {
            if (i && cursor_row != 0)
               windgoto(cursor_row, cursor_col);
            out_str(termCodesG[KS_AL]);
         } else  // type == USE_T_SR
            out_str(termCodesG[KS_SR]);
         screen_start();       // don't know where cursor is now
      }
   }

   //With scroll-reverse and 'da' flag set we need to clear the lines that
   //have been scrolled down into the region.
   if (type == USE_T_SR && *termCodesG[KS_DA]) {
      for (i = 0; i < line_count; ++i) {
         windgoto(off + i, cursor_col);
         out_str(termCodesG[KS_CE]);
         screen_start();       // don't know where cursor is now
      }
   }

   needRedrawTabpanelG = true;

   return OK;
}

//Delete lines on the screen and update screenTextP[].
//"end" is the line after the scrolled part. Normally it is visibleRowsG.
//When scrolling region used "off" is the offset from the top for the region.
//"row" and "end" are relative to the start of the region.
//
//Return OK for success, FAIL if the lines are not deleted.
pub int
screen_del_lines(
   int      off,
   int      row,
   int      line_count,
   int      end,
   int      force,      // even when line_count > p_ttyscroll
   Int      clearHiId,   // used for clearing lines
   Portal* po      // NULL or portal to use width from
){
   int      j;
   int      i;
   int      cursor_row;
   int      cursor_col = 0;
   int      cursor_end;
   int      result_empty;   // result is empty until end of region
   int      can_delete;   // deleting line codes can be used
   int      type;

   //FAIL if
   //- there is no valid screen
   //- the screen has to be redrawn completely
   //- the line count is less than one
   //- the line count is more than 'ttyscroll'
   //- "end" is more than "visibleRowsG" (safety check, should not happen)
   //- redrawing for a callback and there is a modeless selection
   if (!screen_valid(true)
          || line_count <= 0
          || (!force && line_count > p_ttyscroll)
          || end > visibleRowsG
          || (clipboard.state != SELECT_CLEARED && redrawingForCallbackS > 0)
    )
      return FAIL;

    // Check if the rest of the current region will become empty.
    result_empty = row + line_count >= end;

    // We can delete lines only when 'db' flag not set or when 'ce' option available.
    can_delete = (*termCodesG[KS_DB] == ZERO || can_clear(termCodesG[KS_CE]));

   //There are six ways to delete lines:
   //0. When in a vertically split portal and t_CV isn't set, redraw the
   //   characters from screenTextP[].
   //1. Use termCodesG[KS_CD] if it exists and the result is empty.
   //2. Use newlines if row == 0 and count == 1 or termCodesG[KS_CDL] does not exist.
   //3. Use termCodesG[KS_CDL] (delete multiple lines) if it exists and line_count > 1 or
   //    none of the other ways work.
   //4. Use termCodesG[KS_CE] (erase line) if the result is empty.
   //5. Use termCodesG[KS_DL] (delete line) if it exists.
   //6. redraw the characters from screenTextP[].
   if (po && po->width != topframeG->width && *termCodesG[KS_CSV] == ZERO) {
      //Avoid that lines are first cleared here and then redrawn, which results in many 
      //characters updated twice.  This happens with CTRL-F in a vertically split portal.  
      //With line-by-line scrolling USE_REDRAW should be faster.
      if (line_count > 3)
          return FAIL;
      type = USE_REDRAW;
   } ei (can_clear(termCodesG[KS_CD]) && result_empty)
      type = USE_T_CD;
   ei (row == 0 && (line_count == 1 || *termCodesG[KS_CDL] == ZERO))
      type = USE_NL;
   ei (*termCodesG[KS_CDL] != ZERO && line_count > 1 && can_delete)
      type = USE_T_CDL;
   ei (can_clear(termCodesG[KS_CE]) && result_empty && (po == NULL || po->width == topframeG->width))
      type = USE_T_CE;
   ei (*termCodesG[KS_DL] != ZERO && can_delete)
      type = USE_T_DL;
   ei (*termCodesG[KS_CDL] != ZERO && can_delete)
      type = USE_T_CDL;
   else
      return FAIL;

   //Remove a modeless selection when deleting lines halfway the screen or
   //not the full width of the screen.
   if (off + row > 0 || (po && po->width != topframeG->width))
      clip_clear_selection(&clipboard);
   else
      clip_scroll_selection(line_count);


   if (po && po->windowCol != 0 && *termCodesG[KS_CSV] != ZERO && *termCodesG[KS_CCS] == ZERO)
      cursor_col = po->windowCol;

   if (*termCodesG[KS_CCS] != ZERO) {       // cursor relative to region
      cursor_row = row;
      cursor_end = end;
   } else {
      cursor_row = row + off;
      cursor_end = end + off;
   }

   //Now shift lineStartsP[] line_count up to reflect the deleted lines.
   //Clear the inserted lines in screenTextP[].
   row += off;
   end += off;
   for (i = 0; i < line_count; ++i) {
      if (po && po->width != topframeG->width) {
         //need to copy part of a line
         j = row + i;
         while ((j += line_count) <= end - 1)
            linecopy(j - line_count, j, po);
         j -= line_count;
         if (can_clear(S" "))
            lineclear(lineStartsP[j] + po->windowCol, po->width, clearHiId);
         else
            lineinvalid(lineStartsP[j] + po->windowCol, po->width);
         lineWrapsP[j] = false;
      } else {
         //whole width, moving the line pointers is faster
         j = row + i;
         Unt temp = lineStartsP[j];
         while ((j += line_count) <= end - 1) {
            lineStartsP[j - line_count] = lineStartsP[j];
            lineWrapsP[j - line_count] = lineWrapsP[j];
         }
         lineStartsP[j - line_count] = temp;
         lineWrapsP[j - line_count] = false;
         if (can_clear((CS)" "))
            lineclear(temp, (int)visibleColsG, clearHiId);
         else
            lineinvalid(temp, (int)visibleColsG);
      }
   }

   if (activeDecoP.hiId != clearHiId)
      drawStopHilite();
   if (clearHiId != 0)
      startDrawingHilite(clearHiId);

   //redraw the characters
   if (type == USE_REDRAW)
      redraw_block(row, end, po);
   ei (type == USE_T_CD) {    //delete the lines
      windgoto(cursor_row, cursor_col);
      out_str(termCodesG[KS_CD]);
      screen_start();         //don't know where cursor is now
   } ei (type == USE_T_CDL) {
      windgoto(cursor_row, cursor_col);
      term_delete_lines(line_count);
      screen_start();         //don't know where cursor is now
   }
   //Deleting lines at top of the screen or scroll region: Just scroll
   //the whole screen (scroll region) up by outputting newlines on the last line.
   ei (type == USE_NL) {
      windgoto(cursor_end - 1, cursor_col);
      for (i = line_count; --i >= 0; )
         out_char('\n');      // cursor will remain on same line
   } else {
      for (i = line_count; --i >= 0; ) {
         if (type == USE_T_DL) {
            windgoto(cursor_row, cursor_col);
            out_str(termCodesG[KS_DL]);      // delete a line
         } else { // type == USE_T_CE
            windgoto(cursor_row + i, cursor_col);
            out_str(termCodesG[KS_CE]);      // erase a line
         }
         screen_start();      // don't know where cursor is now
      }
   }

   // If the 'db' flag is set, we need to clear the lines that have been
   // scrolled up at the bottom of the region.
   if (*termCodesG[KS_DB] && (type == USE_T_DL || type == USE_T_CDL)) {
      for (i = line_count; i > 0; --i) {
         windgoto(cursor_end - i, cursor_col);
         out_str(termCodesG[KS_CE]);      // erase a line
         screen_start();      // don't know where cursor is now
      }
   }

   needRedrawTabpanelG = true;

   return OK;
}

// Return true when postponing displaying the mode message: when not redrawing or inside a mapping
pub int
skip_showmode(void) {
   // Call char_avail() only when we are going to show something, because it
   // takes a bit of time. redrawing() may also call char_avail().
   if (global_busy
       || msg_silent != 0
       || !redrawing()
       || (char_avail() && !keyWasTypedG)
   ) {
      redrawModeG = true;      // show mode later
      return true;
   }
   return false;
}

private void
ruler(Portal* po, int always) {
   int empty_line = false;
   
   //Check if cursor.lnum is valid, since ruler() may be called
   //after deleting lines, before cursor.lnum is corrected.
   if (po->cursor.lnum > po->book->mem.lineCount)
      return;

   if (p_ruf) {
      statusLineOrRuler(po, true);
      return;
   }

   //Check if not in Insert mode and the line is empty (will show "0-1").
   if ((stateG & MODE_INSERT) == 0 && *memGetLine(po->book, po->cursor.lnum, false) == ZERO)
      empty_line = true;

   // Only draw the ruler when something changed.
   validate_virtcol_win(po);
   if (redrawCommlineG
       || always
       || po->cursor.lnum != po->ruler.pos.lnum
       || po->cursor.col != po->ruler.pos.col
       || po->virtCol != po->ruler.virtCol
       || po->cursor.coladd != po->ruler.pos.coladd
       || po->topLine != po->ruler.topLine
       || po->book->mem.lineCount != po->ruler.lineCount
       || po->topFill != po->ruler.topFill
       || empty_line != po->ruler.isLineEmpty
   ){
      int   row;
      int   fillchar;
      int   off;
      int   width;
      ColNr   virtcol;
#define RULER_BUF_LEN 70
      Byte buffer[RULER_BUF_LEN];
      int bufferlen;
      Byte rel_pos[RULER_BUF_LEN];
      int rel_poslen;
      int this_ru_col;

      cursor_off();
      Decoration deco;
      row = statusline_row(po);
      fillchar = statusLineNextChar(OUT &deco, po);
      off = po->windowCol;
      width = po->width;

      // In list mode virtcol needs to be recomputed
      virtcol = po->virtCol;
      if (po->o.list && listCharsG.tab1 == ZERO) {
         po->o.list = false;
         bookGetVirtualColInVirtualMode(po, &po->cursor, NULL, &virtcol, NULL);
         po->o.list = true;
      }

      // row number, column number is appended
      // l10n: leave as-is unless a space after the comma is preferred
      // l10n: do not add any row/column label, due to the limited space
      bufferlen = eeSnprintf(buffer, RULER_BUF_LEN, _("%ld,"),
         (po->book->mem.flags & ML_EMPTY)
             ? 0L
             : (long)(po->cursor.lnum));
      bufferlen += col_print(buffer + bufferlen, RULER_BUF_LEN - bufferlen,
            empty_line ? 0 : (int)po->cursor.col + 1,
            (int)virtcol + 1);

      //Add a "50%" if there is room for it.
      //On the last line, don't print in the last column (scrolls the screen up on some terminals).
      rel_poslen = get_rel_pos(po, rel_pos, RULER_BUF_LEN);
      int n1 = bufferlen + eeglStrSize(rel_pos); //scratch value

      this_ru_col = rulerColP - (visibleColsG - width);
      // Never use more than half the portal/screen width, leave the other half for the filename.
      int n2 = (width + 1) / 2; //scratch value
      if (this_ru_col < n2)
         this_ru_col = n2;
      if (this_ru_col + n1 < width) {
         // need at least space for rel_pos + ZERO
         while (this_ru_col + n1 < width
                && RULER_BUF_LEN > bufferlen + rel_poslen + 1) {  // +1 for ZERO
            bufferlen += mb_char2bytes(fillchar, buffer + bufferlen);
            ++n1;
         }
         bufferlen += eeSnprintf(buffer + bufferlen, RULER_BUF_LEN - bufferlen,
                "%s", rel_pos);
      }
      // Truncate at portal boundary.
      for (n1 = 0, n2 = 0; buffer[n1] != ZERO; n1 += utfCharLen(buffer + n1)) {
         n2 += mb_ptr2cells(buffer + n1);
         if (this_ru_col + n2 > width) {
            bufferlen = n1;
            buffer[bufferlen] = ZERO;
            break;
         }
      }

      drawText(buffer, row, this_ru_col + off, deco.flags);
      n1 = redrawCommlineG;
      fillRowsWithTwoChars(
         row, row + 1, this_ru_col + off + bufferlen, (off + width), fillchar, fillchar, deco
      );
      // don't redraw the command line because of showing the ruler
      redrawCommlineG = n1;
      po->cursor = po->cursor;
      po->virtCol = po->virtCol;
      po->ruler.isLineEmpty = empty_line;
      po->topLine = po->topLine;
      po->ruler.lineCount = po->book->mem.lineCount;
      po->topFill = po->topFill;
   }
}

//Show the current mode and ruler.
//If mustClearCommlineG, clear the rest of the cmdline. If not mustClearCommlineG, there may be a 
//message there that needs to be cleared only if a mode is shown. If redrawModeG is true show or 
//clear the mode. Return the length of the message (0 if no message).
pub int
showmode(void) {
   int      length = 0;
   int      do_mode;
   char     flags;
   int      nwr_save;

   do_mode = p_smd && msg_silent == 0
       && ((stateG & MODE_INSERT)
            || restart_edit != ZERO
            || VIsual_active);
   if (do_mode || reg_recording != 0) {
      if (skip_showmode())
          return 0;      // show mode later

      nwr_save = need_wait_return;

      // wait a bit before overwriting an important message
      check_for_delay(false);

      // if the cmdline is more than one line high, erase top lines
      int need_clear = mustClearCommlineG;
      if (mustClearCommlineG && commlineRowG < visibleRowsG - 1)
         msgClearCommline();         // will reset mustClearCommlineG

      // Position on the last line in the portal, column 0
      msg_pos_mode();
      cursor_off();
      flags = getDecoFlags(HLF_CM);         // hilite mode
      if (do_mode) {
         msgPutsDeco((CS)"--", flags);
         // CTRL-X in Insert mode
         if (editSubmodeMsgG) {
            //These messages can get long, avoid a wrap in a narrow
            //portal. Prefer showing editSubmodeExtraMsgG.
            length = (visibleRowsG - msgRowG) * visibleColsG - 3;
            if (editSubmodeExtraMsgG)
               length -= eeglStrSize(editSubmodeExtraMsgG);
            if (length > 0) {
               if (editSubmodePreMsgG)
                  length -= eeglStrSize(editSubmodePreMsgG);
               if (length - eeglStrSize(editSubmodeMsgG) > 0) {
                  if (editSubmodePreMsgG)
                     msgPutsDeco(editSubmodePreMsgG, flags);
                  msgPutsDeco(editSubmodeMsgG, flags);
               }
               if (editSubmodeExtraMsgG) {
                  msgPutsDeco((CS)" ", flags);  // add a space in between
                  char subDeco = getDecoFlags(editSubmodeHiG);
                  msgPutsDeco(editSubmodeExtraMsgG, subDeco);
               }
            }
         } else {
            if (stateG & MODE_INSERT) {
               msgPutsDeco(_(" INSERT"), flags);
            } ei (restart_edit == 'I' || restart_edit == 'i' 
                  || restart_edit == 'a' || restart_edit == 'A'
            )
               msgPutsDeco(_(" (insert)"), flags);
            ei (restart_edit == 'R')
               msgPutsDeco(_(" (replace)"), flags);

            if (VIsual_active) {
               CS p;
               // Don't concatenate separate words to avoid translation problems.
               switch ((VIsual_mode == Ctrl_V) * 2 + (VIsual_mode == 'V')) {
               case 0:   p = N_(" VISUAL"); break;
               case 1:   p = N_(" VISUAL LINE"); break;
               case 2:   p = N_(" VISUAL BLOCK"); break;
               case 4:   p = N_(" SELECT"); break;
               case 5:   p = N_(" SELECT LINE"); break;
               default:  p = N_(" SELECT BLOCK"); break;
               }
               msgPutsDeco(_(p), flags);
            }
            msgPutsDeco((CS)" --", flags);
         }

         need_clear = true;
      }
      if (reg_recording != 0 && editSubmodeMsgG == NULL) {  // otherwise it gets too long
         recording_mode(flags);
         need_clear = true;
      }

      isModeDisplayedG = true;
      if (need_clear || mustClearCommlineG || redrawModeG)
         msg_clr_eos();
      msg_didout = false;      // overwrite this message
      length = msgColG;
      msgColG = 0;
      need_wait_return = nwr_save;   // never ask for hit-return for this
   } ei (mustClearCommlineG && msg_silent == 0)
      // Clear the whole command line.  Will reset "mustClearCommlineG".
      msgClearCommline();
   ei (redrawModeG) {
      msg_pos_mode();
      msg_clr_eos();
   }

   // In Visual mode the size of the selected area must be redrawn.
   if (VIsual_active)
   clear_showcmd();

   redrawCommlineG = false;
   redrawModeG = false;
   mustClearCommlineG = false;

   return length;
}

// Position for a mode message.
private void
msg_pos_mode(void) {
   msgColG = 0;
   msgRowG = visibleRowsG - 1;
}

//Delete mode message.  Used when ESC is typed which is expected to end
//Insert mode (but Insert mode didn't end yet!). Caller should check "isModeDisplayedG".
pub void
unshowmode(int force) {
   // Don't delete it right now, when not redrawing or inside a mapping.
   if (!redrawing() || (!force && char_avail() && !keyWasTypedG))
      redrawCommlineG = true;      // delete mode later
   else
      clearmode();
}

// Clear the mode message.
pub void
clearmode(void) {
   int msgRowSaved = msgRowG;
   int saveMsgCol = msgColG;

   msg_pos_mode();
   if (reg_recording != 0)
      recording_mode(getDecoFlags(HLF_CM));
   msg_clr_eos();

   msgColG = saveMsgCol;
   msgRowG = msgRowSaved;
}

private void
recording_mode(char flags) {
   msgPutsDeco(_("recording"), flags);
   Byte s[4];

   SPRINTF(s, " @%c", reg_recording);
   msgPutsDeco(s, flags);
}

//Get buffer name for "book" into nameBuffG[].
//Take care of special book names and translate special characters.
pub void
drawGetTranslatedBookName(Book* book) {
   if (bookSpName(book))
      copySubstrToAllocation(nameBuffG, (Text){bookSpName(book), MAXPATHL - 1});
   ei (book && book->kind == BOOK_HELP) { 
      strPrintShortName(book->currFileName, nameBuffG, MAXPATHL);
   } else
      home_replace(book->currFileName, nameBuffG, MAXPATHL, true);
   trans_characters(nameBuffG, MAXPATHL);
}

// Get the character to use in a status line. Write its decorations into "*deco"
pub Unt
statusLineNextChar(OUT Decoration* deco, Portal* po) {
   if (bt_terminal(po->book)) {
      if (po == curPor) {
         *deco = getFullDecoration(HLF_ST);
         return fillCharsG.stl;
      } else {
         *deco = getFullDecoration(HLF_STNC);
         return fillCharsG.stlnc;
      }
   } ei (po == curPor) {
      *deco = getFullDecoration(HLF_S);
      return fillCharsG.stl;
   } else {
      *deco = getFullDecoration(HLF_SNC);
      return fillCharsG.stlnc;
   }
}

// Return true if redrawing should currently be done.
pub int
redrawing(void) {
   if (disable_redraw_for_testing)
      return 0;
   else
      return ((isRedrawingDisabledG == 0 || ignore_redraw_flag_for_testing) 
         && !(p_lz && char_avail() && !keyWasTypedG && !do_redraw)
      );
}

// Return true if printing messages should currently be done.
pub int
messaging(void) {
   return (!(p_lz && char_avail() && !keyWasTypedG));
}

//Compute columns for ruler and shown command. 'shownCommandColG' is also used to
//decide what the maximum length of a message on the status line can be.
//If there is a status line for the last portal, 'shownCommandColG' is independent of 'rulerColP'.

#define COL_RULER 17       // columns needed by standard ruler

pub void
computeColumnsForRulerAndCommand(void) {
   int last_has_status = last_stl_height(false) > 0;

   shownCommandColG = 0;
   rulerColP = (rulerWidthG ? rulerWidthG : COL_RULER) + 1;
   // no last status line, adjust shownCommandColG
   if (!last_has_status)
      shownCommandColG = rulerColP;
   if (p_sloc == SHOW_COMM_LAST) {
      shownCommandColG += SHOWCMD_COLS;
      if (last_has_status)       // no need for separating space
          ++shownCommandColG;
   }
   shownCommandColG = visibleColsG - shownCommandColG;
   rulerColP = visibleColsG - rulerColP;
   if (shownCommandColG <= 0)      // screen too narrow, will become a mess
      shownCommandColG = 1;
   if (rulerColP <= 0)
      rulerColP = 1;
   set_EeglVar_nr(VV_ECHOSPACE, shownCommandColG - 1);
}

//Return the width of the 'number' and 'relativenumber' column.
//Caller may need to check if 'number' or 'relativenumber' is set.
//Otherwise it depends on 'numberwidth' and the line count.
pub int
number_width(Portal* po) {
   // cursor line shows absolute line number
   LineNr lnum = po->book->mem.lineCount;

   if (lnum == po->lineCountSaved && po->numWidthCached == po->o.numberWidth)
      return po->lineCountSaved;
   po->lineCountSaved = lnum;

   int n = 0;
   do {
      lnum /= 10;
      ++n;
   } while (lnum > 0);

   //'numberwidth' gives the minimal width plus one
   if (n < po->o.numberWidth - 1)
      n = po->o.numberWidth - 1;

   //If 'signcolumn' is on for the portal, then the minimal width for the number column is 2.
   if (n < 2 && isSigncolumnOn(po))
      n = 2;

   po->lineCountSaved = n;
   po->numWidthCached = po->o.numberWidth;
   return n;
}

// Return the current cursor column. This is the actual position on the screen. First column is 0.
pub int
screen_screencol(void) {
   return screenCursColG;
}

// Return the current cursor row. This is the actual position on the screen. First row is 0.
pub int
screen_screenrow(void) {
    return screenCursRowG;
}

//Call strAdvanceMultibyte(p) and returns the character.
//If "p" starts with "\x", "\u" or "\U" the hex or unicode value is used.
private int
get_encoded_char_adv(Byte **p) {
   Byte *s = *p;

   if (s[0] == '\\' && (s[1] == 'x' || s[1] == 'u' || s[1] == 'U')) {
      Long num = 0;
      int       bytes;
      int       n;

      for (bytes = s[1] == 'x' ? 1 : s[1] == 'u' ? 2 : 4; bytes > 0; --bytes) {
         *p += 2;
         n = hexhex2nr(*p);
         if (n < 0)
            return 0;
         num = num * 256 + n;
      }
      *p += 2;
      return num;
   }
   return strAdvanceMultibyte(p);
}

private typedef struct {
   Unt* cp;
   Text   name;
} CharsTableEntry;

#define CHARSTAB_ENTRY(cp, name) \
    {(cp), {(CS)(name), STRLEN_LITERAL(name)}}

private CharsTableEntry fillCharsTable[] = {
    CHARSTAB_ENTRY(&fillCharsG.stl,       "stl"),
    CHARSTAB_ENTRY(&fillCharsG.stlnc,       "stlnc"),
    CHARSTAB_ENTRY(&fillCharsG.vert,       "vert"),
    CHARSTAB_ENTRY(&fillCharsG.fold,       "fold"),
    CHARSTAB_ENTRY(&fillCharsG.foldopen,    "foldopen"),
    CHARSTAB_ENTRY(&fillCharsG.foldclosed,  "foldclose"),
    CHARSTAB_ENTRY(&fillCharsG.foldsep,       "foldsep"),
    CHARSTAB_ENTRY(&fillCharsG.diff,       "diff"),
    CHARSTAB_ENTRY(&fillCharsG.eob,       "eob"),
    CHARSTAB_ENTRY(&fillCharsG.lastline,    "lastline"),
    CHARSTAB_ENTRY(&fillCharsG.tpl_vert,    "tpl_vert"),
    CHARSTAB_ENTRY(&fillCharsG.trunc,       "trunc"),
    CHARSTAB_ENTRY(&fillCharsG.truncrl,       "truncrl"),
};
private CharsTableEntry listCharTable[] = {
    CHARSTAB_ENTRY(&listCharsG.eol,       "eol"),
    CHARSTAB_ENTRY(&listCharsG.ext,       "extends"),
    CHARSTAB_ENTRY(&listCharsG.nbsp,       "nbsp"),
    CHARSTAB_ENTRY(&listCharsG.prec,       "precedes"),
    CHARSTAB_ENTRY(&listCharsG.space,       "space"),
    CHARSTAB_ENTRY(&listCharsG.tab2,       "tab"),
    CHARSTAB_ENTRY(&listCharsG.trail,       "trail"),
    CHARSTAB_ENTRY(&listCharsG.lead,       "lead"),
    CHARSTAB_ENTRY(NULL,          "conceal"),
    CHARSTAB_ENTRY(NULL,          "multispace"),
    CHARSTAB_ENTRY(NULL,          "leadmultispace")
};

private CS
field_value_err(OUT ErrBuilder* errb, CS fmt, CS field) {
   if (!errb->c)
      return E;
   eeSnprintf(errb->c, errb->len, _(fmt), field);
   return errb->c;
}

//Handle setting @listchars or @fillchars. "value" points to either the global or the 
//portal-local value. "is_listchars" is true for "listchars" and false for "fillchars".
//When "apply" is false do not store the flags, only check for errors.
//Assume monocell characters. Return error message, NULL if it's OK.
pub CS
set_chars_option(CS newVal, Boole is_listchars, OUT ErrBuilder* errb){
   int       round, i, entries;
   CS  p;
   CS s;
   int       c1 = 0, c2 = 0, c3 = 0;
   CS last_multispace = NULL;  // Last occurrence of "multispace:"
   CS last_lmultispace = NULL; // Last occurrence of "leadmultispace:"
   int       multispace_len = 0;         // Length of lcs-multispace string
   int       lead_multispace_len = 0;  // Length of lcs-leadmultispace string

   CharsTableEntry* tab;
   if (is_listchars) {
      tab = listCharTable;
      CLEAR_FIELD(listCharsG);
      entries = ARRAY_LENGTH(listCharTable);
   } else {
      tab = fillCharsTable;
      entries = ARRAY_LENGTH(fillCharsTable);
   }

   // first round: check for valid value, second round: assign values
   for (round = 0; round < 2; ++round) {
      if (round > 0) {
         // After checking that the value is valid: set defaults.
         if (is_listchars) {
            for (i = 0; i < entries; ++i) {
               if (tab[i].cp)
                  *(tab[i].cp) = ZERO;
            } 
            listCharsG.tab1 = ZERO;
            listCharsG.tab3 = ZERO;

            if (multispace_len > 0) {
               listCharsG.multispace = ALLOC_MULT(Unt, multispace_len + 1);
               listCharsG.multispace[multispace_len] = ZERO;
            } else
               listCharsG.multispace = NULL;

            if (lead_multispace_len > 0) {
               listCharsG.leadmultispace = ALLOC_MULT(Unt, lead_multispace_len + 1);
               listCharsG.leadmultispace[lead_multispace_len] = ZERO;
            } else
               listCharsG.leadmultispace = NULL;
         } else {
            fillCharsG.stl = ' ';
            fillCharsG.stlnc = ' ';
            fillCharsG.vert = ' ';
            fillCharsG.fold = '-';
            fillCharsG.foldopen = '-';
            fillCharsG.foldclosed = '+';
            fillCharsG.foldsep = '|';
            fillCharsG.diff = '-';
            fillCharsG.eob = '~';
            fillCharsG.lastline = '@';
            fillCharsG.tpl_vert = '|';
            fillCharsG.trunc = '>';
            fillCharsG.truncrl = '<';
         }
      }
      p = newVal;
      while (*p) {
         for (i = 0; i < entries; ++i) {
            if (!(STRNCMP(
                        p, tab[i].name.c, tab[i].name.len) == 0 && p[tab[i].name.len] == ':'
                  )) {
                continue;
            } 

            s = p + tab[i].name.len + 1;

            if (is_listchars && STRCMP(tab[i].name.c, "multispace") == 0) {
               if (round == 0) {
                  // Get length of lcs-multispace string in first round
                  last_multispace = p;
                  multispace_len = 0;
                  while (*s != ZERO && *s != ',') {
                     c1 = get_encoded_char_adv(&s);
                     if (bookChar2Cells(c1) > 1)
                        return field_value_err(
                           OUT errb, e_wrong_character_width_for_field_str, tab[i].name.c
                        );
                     ++multispace_len;
                  }
                  if (multispace_len == 0)
                     // lcs-multispace cannot be an empty string
                     return field_value_err(
                        OUT errb, e_wrong_number_of_characters_for_field_str, tab[i].name.c
                     );
               } else {
                  int multispacePos = 0;

                  while (*s != ZERO && *s != ',') {
                     c1 = get_encoded_char_adv(&s);
                     if (p == last_multispace && listCharsG.multispace)
                        listCharsG.multispace[multispacePos++] = c1;
                  }
               }
               p = s;
               break;
            }

            if (is_listchars && STRCMP(tab[i].name.c, "leadmultispace") == 0) {
               if (round == 0) {
                  // Get length of lcs-leadmultispace string in first round
                  last_lmultispace = p;
                  lead_multispace_len = 0;
                  while (*s != ZERO && *s != ',') {
                     c1 = get_encoded_char_adv(&s);
                     if (bookChar2Cells(c1) > 1) {
                        return field_value_err(
                           OUT errb, e_wrong_character_width_for_field_str, tab[i].name.c
                        );
                     } 
                     ++lead_multispace_len;
                  }
                  if (lead_multispace_len == 0)
                     // lcs-leadmultispace cannot be an empty string
                     return field_value_err(
                         OUT errb, e_wrong_number_of_characters_for_field_str, tab[i].name.c
                     );
               } else {
                  int multispacePos = 0;
                  while (*s != ZERO && *s != ',') {
                     c1 = get_encoded_char_adv(&s);
                     if (p == last_lmultispace && listCharsG.leadmultispace)
                        listCharsG.leadmultispace[multispacePos++] = c1;
                  }
               }
               p = s;
               break;
            }

            c2 = c3 = 0;
            if (*s == ZERO) {
               return field_value_err(
                   OUT errb, e_wrong_number_of_characters_for_field_str, tab[i].name.c
               );
            }
            c1 = get_encoded_char_adv(&s);
            if (bookChar2Cells(c1) > 1)
               return field_value_err(
                     OUT errb, e_wrong_character_width_for_field_str, tab[i].name.c
               );
            if (tab[i].cp == &listCharsG.tab2) {
               if (*s == ZERO) {
                  return field_value_err(
                     OUT errb, e_wrong_number_of_characters_for_field_str, tab[i].name.c
                  );
               } 
               c2 = get_encoded_char_adv(&s);
               if (bookChar2Cells(c2) > 1)
                  return field_value_err(
                     OUT errb, e_wrong_character_width_for_field_str, tab[i].name.c
                  );
               if (!(*s == ',' || *s == ZERO)) {
                  c3 = get_encoded_char_adv(&s);
                  if (bookChar2Cells(c3) > 1)
                     return field_value_err(
                         OUT errb, e_wrong_character_width_for_field_str, tab[i].name.c
                     );
               }
            }

            if (*s == ',' || *s == ZERO) {
               if (round > 0) {
                  if (tab[i].cp == &listCharsG.tab2) {
                     listCharsG.tab1 = c1;
                     listCharsG.tab2 = c2;
                     listCharsG.tab3 = c3;
                  } ei (tab[i].cp)
                      *(tab[i].cp) = c1;

               }
               p = s;
               break;
            }
            else {
               return field_value_err(
                  OUT errb, e_wrong_number_of_characters_for_field_str, tab[i].name.c
               );
            } 
         }

         if (i == entries)
            return e_invalid_argument;

         if (*p == ',')
            ++p;
      }
   }

   if (is_listchars) {
      eeglFree(listCharsG.multispace);
      eeglFree(listCharsG.leadmultispace);
   }

   return NULL;   // no error
}

// Handle the new value of @fillchars
pub CS
drawSetFillChars(CS newVal, OUT ErrBuilder* errb){
   return set_chars_option(newVal, false, errb);
}

// Handle the new value of @listchars.
pub CS
drawSetListChars(CS newVal, OUT ErrBuilder* errb) {
   return set_chars_option(newVal, true, errb);
}

// Function given to expandGeneric() to obtain possible arguments of the @fillchars
pub CS
get_fillchars_name(Expand *xp UNUSED, int idx) {
   if (idx < 0 || idx >= (int)ARRAY_LENGTH(fillCharsTable))
      return NULL;
   return fillCharsTable[idx].name.c;
}

// Function given to expandGeneric() to obtain possible arguments of the @listchars
pub CS
get_listchars_name(Expand *xp UNUSED, int idx) {
   if (idx < 0 || idx >= (int)ARRAY_LENGTH(listCharTable))
      return NULL;

   return listCharTable[idx].name.c;
}

//Check the values of @listchars and @fillchars.
//Return an untranslated error messages if any of them is invalid, NULL otherwise.
pub CS
check_chars_options(CS newVal) {
   ErrBuilder errb = {};
   if (drawSetListChars(newVal, &errb))
      return e_conflicts_with_value_of_listchars;
   if (drawSetFillChars(newVal, &errb))
      return e_conflicts_with_value_of_fillchars;
   return NULL;
}

//}}}
//{{{high level

//Code for updating all the portals on the screen.
//
//drawUpdateScreen() is the function that updates all portals and status lines.
//It is called form the main loop when mustRedrawG is non-zero. It may be
//called from other places when an immediate screen update is needed.
//
//The part of the buffer that is displayed in a portal is set with:
//- topLine (first buffer line in portal)
//- topFill (filler lines above the first line)
//- leftCol (leftmost cell in portal),
//- skipCol (skipped cells of first line)
//
//Commands that only move the cursor around in a portal, do not need to take
//action to update the display.  The main loop will check if topLine is
//valid and update it (scroll the portal) when needed.
//
//Commands that scroll a portal change topLine and must call
//check_cursor() to move the cursor into the visible part of the portal, and
//call redraw_later(UPD_VALID) to have the portal displayed by drawUpdateScreen() later.
//
//Commands that change text in the buffer must call changed_bytes() or changed_lines() to mark the 
//area that changed and will require updating later. The main loop will call drawUpdateScreen(), 
//which will update each portal that shows the changed buffer. This assumes text above the change
//can remain displayed as it is.  Text after the change may need updating for scrolling, folding 
//and syntax hiliting.
//
//Commands that change how a portal is displayed (e.g., setting 'list') or
//invalidate the contents of a portal in another way (e.g., change fold
//settings), must call redraw_later(UPD_NOT_VALID) to have the whole portal
//redisplayed by drawUpdateScreen() later.
//
//Commands that change how a buffer is displayed (e.g., setting 'tabstop') must call 
//drawCurBookLater(UPD_NOT_VALID) to have all the portals for the
//buffer redisplayed by drawUpdateScreen() later.
//
//Commands that change hiliting and possibly cause a scroll too must call 
//redraw_later(UPD_SOME_VALID) to update the whole portal but still use scrolling to avoid 
//redrawing everything.  But the length of displayed lines must not change, use UPD_NOT_VALID then.
//
//Commands that move the portal position must call redraw_later(UPD_NOT_VALID).
//TODO: should minimize redrawing by scrolling when possible.
//
//Commands that change everything (e.g., resizing the screen) must call
//redraw_all_later(UPD_NOT_VALID) or redraw_all_later(UPD_CLEAR).
//
//Things that are handled indirectly:
//- When messages scroll the screen up, msg_scrolled will be set and drawUpdateScreen() called to 
//  redraw
 

private FoldInfo portFoldS;   // info for folds


//Based on the current value of curPor->topLine, transfer a screenful
//of stuff from Filemem to screenTextP[], and update curPor->bottomLine.
//Return OK when the screen was updated, FAIL if it was not done.
pub int
drawUpdateScreen(Unt type_arg) {
   Unt type = type_arg;
   Portal* po;
   static int   did_intro = false;
   int no_update = false;
   int save_pum_will_redraw = pum_will_redraw;

   // Don't do anything if the screen structures are (not yet) valid.
   if (!screen_valid(true))
      return FAIL;

   if (type == UPD_VALID_NO_UPDATE) {
      no_update = true;
      type = 0;
   }

   {
   // Before updating the screen, notify any listeners of changed text.
   Book* book;
   FOR_ALL_BOOKS(book)
      jugInvokeListenersOnChangedText(book);
   }

   // May have postponed updating diffs.
   if (diffNeedsRedrawG)
      diff_redraw(true);

   if (mustRedrawG) {
      if (type < mustRedrawG)       // use maximal type
         type = mustRedrawG;

      //mustRedrawG is reset here, so that when we run into some weird reason to redraw while 
      //busy redrawing (e.g., asynchronous scrolling), or update_topline() in updatePortal()
      //will cause a scroll, the screen will be redrawn later or in updatePortal().
      mustRedrawG = 0;
   }

   //May need to update lines[].
   if (curPor->validLines == 0 && type < UPD_NOT_VALID && !termDoUpdatePortal(curPor))
      type = UPD_NOT_VALID;

   //Postpone the redrawing when it's not needed and when being called recursively.
   if (!redrawing() || updating_screen) {
      redraw_later(type);      // remember type for next time
      mustRedrawG = type;
      if (type > UPD_INVERTED_ALL)
         curPor->validLines = 0;   // don't use lines[].height now
      return FAIL;
   }
   updating_screen = true;

   // Update popupMaskG if needed. This may set redrawTop and redrawBott in some portals.
   may_update_popup_mask(type);

   ++display_tick;       // let syntax code know we're in a next round of display updating
   if (no_update)
      avoidLineInsertionS = true;

   // if the screen was scrolled up when displaying a message, scroll it down
   if (msg_scrolled) {
      mustClearCommlineG = true;
      if (type != UPD_CLEAR) {
          if (msg_scrolled > visibleRowsG - 5) {       // redrawing is faster
            type = UPD_NOT_VALID;
            redraw_as_cleared();
          } else {
            check_for_delay(false);
            if (drawInsertLines(0, 0, msg_scrolled, (int)visibleRowsG, 0, NULL) == FAIL) {
               type = UPD_NOT_VALID;
               redraw_as_cleared();
            }
            FOR_ALL_PORTALS(po) {
               if (msg_scrolled > -1 && po->windowRow < msg_scrolled) {
                  if (po->windowRow + po->height > (Unt)msg_scrolled
                        && po->redrawType < UPD_REDRAW_TOP
                        && po->validLines > 0
                        && po->topLine == po->lines[0].bookLnum
                  ){
                     po->rowsToUpdate = msg_scrolled - po->windowRow;
                     po->redrawType = UPD_REDRAW_TOP;
                  } else {
                     po->redrawType = UPD_NOT_VALID;
                     if (po->windowRow + po->height + STATUS_HEIGHT <= (Unt)msg_scrolled)
                        po->statusLineNeedsRedraw = true;
                  }
               }
            }
            if (!no_update)
               redrawCommlineG = true;
         }
         needRedrawTabpanelG = true;
      }
      msg_scrolled = 0;
      need_wait_return = false;
   }

   // reset commlineRowG now (may have been changed temporarily)
   compute_cmdrow();
   
   // Check for changed highlighting
   //if (need_highlight_changed)
   //   highlight_changed();

   if (type == UPD_CLEAR) {     // first clear screen
      screenclear();      // will reset mustClearCommlineG
      type = UPD_NOT_VALID;
      // mustRedrawG may be set indirectly, avoid another redraw later
      mustRedrawG = 0;
   }

   if (mustClearCommlineG)      // going to clear commline (done below)
      check_for_delay(false);

   // Force redraw when width of 'number' or 'relativenumber' column changes.
   if (curPor->redrawType < UPD_NOT_VALID && curPor->numberColWidth != number_width(curPor))
      curPor->redrawType = UPD_NOT_VALID;

   // Only start redrawing if there is really something to do.
   if (type == UPD_INVERTED)
      update_curswant();
   if (curPor->redrawType < type
       && !((type == UPD_VALID
                && curPor->lines[0].isValid
                && curPor->topFill == curPor->topFillOld
                && curPor->bottFill == curPor->bottFillOld
                && curPor->topLine == curPor->lines[0].bookLnum)
             || (type == UPD_INVERTED
                && VIsual_active
                && curPor->prevVisualEnd == curPor->cursor.lnum
                && curPor->prevVisualMode == VIsual_mode
                && (curPor->cacheState & VALID_VIRTCOL)
                && curPor->oldCursWant == curPor->cursWant)
           )
   ) {
      curPor->redrawType = type;
   } 

   if (needRedrawTabpanelG || type >= UPD_NOT_VALID)
      draw_tabpanel();

   //Correct stored syntax hiliting info for changes in each displayed book. Each book must only 
   //be done once.
   FOR_ALL_PORTALS(po) {
      if (po->book->needsRedraw) {
         Portal* wwp;

         // Check if we already did this buffer.
         for (wwp = firstPor; wwp != po; wwp = wwp->next) {
            if (wwp->book == po->book)
                break;
         } 
         if (wwp == po && syntax_present(po))
            syn_stack_apply_changes(po->book);
      }
   }

   if (pum_redraw_in_same_position())
      //Avoid flicker if the popup menu is going to be redrawn in the same position.
      pum_will_redraw = true;

   //Go from top to bottom through the portals, redrawing the ones that need it
   didUpdateOnePortal = false;
   screenSearchP.rm.regprog = NULL;
   FOR_ALL_PORTALS(po) {
      if (po->redrawType != 0) {
         cursor_off();
         updatePortal(po);
      }

      //redraw status line after the portal to minimize cursor movement
      if (po->statusLineNeedsRedraw) {
         cursor_off();
         redrawPortalStatusLine(po, true); // any popup menu will be redrawn below
      }
   }
   end_search_hl();

   // May need to redraw the popup menu.
   pum_will_redraw = save_pum_will_redraw;
   pum_may_redraw();

   // Reset needsRedraw flags.  Going through all portals is probably faster
   // than going through all buffers (there could be many buffers).
   FOR_ALL_PORTALS(po)
      po->book->needsRedraw = false;

   // Display popup portals on top of the portals and command line.
   update_popups(updatePortal);

   FOR_ALL_PORTALS(po) {
      //If this portal is into a terminal, after redrawing all portals, the
      //dirty row range can be reset.
      termDidUpdatePortal(po);
   } 

   after_updating_screen(true);

   //Clear or redraw the command line.  Done last, because scrolling may
   //mess up the command line.
   if (mustClearCommlineG || redrawCommlineG || redrawModeG)
      showmode();

   if (no_update)
      avoidLineInsertionS = false;

   //May put up an introductory message when not editing a file
   if (!did_intro)
      maybe_intro_message();
   did_intro = true;

   return OK;
}

//Redraw the status line of portal po.
//
//If inversion is possible we use it. Else '=' characters are used.
//If "ignore_pum" is true, also redraw statusline when the popup menu is displayed.
pub void
redrawPortalStatusLine(Portal* po, Boole ignore_pum) {
   Decoration deco;
   static Boole busy = false;

   //It's possible to get here recursively if the @statusline expression (indirectly)
   //invokes ":redrawstatus". Simply ignore the call then.
   if (busy)
      return;
   busy = true;

   int row = statusline_row(po);

   po->statusLineNeedsRedraw = false;
   if (!redrawing() //don't update status line when popup menu is visible and may be drawn over 
                    //it, unless it will be redrawn later
       || (!ignore_pum && pum_visible())
   ) { //Don't redraw right now, do it later.
      po->statusLineNeedsRedraw = true;
   } ei (po->o.statusLine) { // redraw custom status line
      statusLineCustom(po);
   } else {
      Unt fillchar = statusLineNextChar(OUT &deco, po);
      drawGetTranslatedBookName(po->book);
      CS p = nameBuffG;
      int plen = (int)STRLEN(p);

      if ((bookIsHelp(po->book)
             || po->isPreview
             || doWasBookChanged(po->book)
             || !po->book->o.modifiable)
         && plen < MAXPATHL - 1
      ){
         *(p + plen++) = ' ';   // replace ZERO with space
         *(p + plen) = ZERO;
      }
      if (bookIsHelp(po->book))
         plen += eeSnprintf(p + plen, MAXPATHL - plen, "%s", _("[Help]"));
      if (po->isPreview)
         plen += eeSnprintf(p + plen, MAXPATHL - plen, "%s", _("[Preview]"));
      if (doWasBookChanged(po->book) && !bt_terminal(po->book)) {
         plen += eeSnprintf(p + plen, MAXPATHL - plen, "[+]");
      } 
      if (!po->book->o.modifiable)
         plen += eeSnprintf(p + plen, MAXPATHL - plen, "[-]");

      int this_ru_col = rulerColP - (visibleColsG - po->width);
      int n = (po->width + 1) / 2;// scratch value
      if (this_ru_col < n)
         this_ru_col = n;
      if (this_ru_col <= 1) {
         p = S"<";      // No room for file name!
         plen = 1;
      } else {
         // Count total number of display cells.
         plen = mb_string2cells(p, -1);

         // Find first character that will fit. Going from start to end is much faster.
         int i;
         for (i = 0; p[i] != ZERO && plen >= this_ru_col - 1; i += utfCharLen(p + i))
            plen -= mb_ptr2cells(p + i);
         if (i > 0) {
            p = p + i - 1;
            *p = '<';
            ++plen;
         }
      }
      drawText(p, row, po->windowCol, deco.flags);
      fillRowsWithTwoChars(
         row, row + 1, plen + po->windowCol, this_ru_col + po->windowCol, fillchar, fillchar, deco
      );
            
      int nameBufflen;
      if ((nameBufflen = drawGetKeymapStr(po, OUT nameBuffTextG)) > 0
            && (this_ru_col - plen) > (nameBufflen + 1)
      ) {
         drawText(
            nameBuffG, row, (int)(this_ru_col - nameBufflen - 1 + po->windowCol), deco.flags
         );
      } 

      ruler(po, true);

      // Draw the 'showcmd' information if 'showcmdloc' == "statusline".
      if (p_sloc == SHOW_COMM_STATUSLINE) {
         n = this_ru_col - plen - 2;          // perform the calculation here so we only do it once
         int width = MIN(10, n);

         if (width > 0) {
            drawTextLen(
               showcmd_buf, width, row, po->windowCol + this_ru_col - width - 1, deco.flags
            );
         } 
      }
   }

   // May need to draw the character below the vertical separator.
   if (po->vsepWidth != 0 && redrawing()) {
      Unt fillchar = stl_connected(po) 
         ? statusLineNextChar(OUT &deco, po) : fillchar_vsep(OUT &deco);
      screen_putchar(fillchar, row, P_ENDCOL(po), deco.flags);
   }
   busy = false;
}

// Redraw the status line according to 'statusline' and take care of any errors encountered.
private void
statusLineCustom(Portal* po) {
   static Boole busy = false;

   //Do not recurse. This can happen when the statusline contains an expr that triggers a redraw
   if (busy)
      return;
   busy = true;
   statusLineOrRuler(po, false);
   busy = false;
}

//Show current status info in ruler and various other places
//If always is false, only show ruler if position has changed.
pub void
showruler(int always) {
   if (!always && !redrawing())
      return;
   if (pum_visible()) {
      // Don't redraw right now, do it later.
      curPor->statusLineNeedsRedraw = true;
      return;
   }
   if (curPor->o.statusLine)
      statusLineCustom(curPor);
   else
      ruler(curPor, always);

   if (needRedrawTabpanelG)
      draw_tabpanel();
}

// To be called when "updating_screen" was set before and now the postponed side effects may happen
pub void
after_updating_screen(int may_resize_shell UNUSED) {
    updating_screen = false;
    term_check_channel_closed_recently();
}

// Update all portals into the current book.
pub void
drawUpdateCurBook(Unt type) {
   drawCurBookLater(type);
   drawUpdateScreen(type);
}

// Copy "text" to screenTextP using "deco". Returns the next screen column.
private int
text_to_screenline(Portal* po, CS text, int col) {
   int off = (int)(currScreenLineS - screenTextP);

   int characterCombiner[MAX_COMBINED_SYMBOLS];
   int idx = off + col;

   // Store multibyte characters in screenTextP[] et al. correctly.
   for (CS p = text; *p != ZERO; ) {
      int cells = mb_ptr2cells(p);
      int c_len = utfCharLen(p);
      if (col + cells > (int)po->width)
         break;
      screenTextP[idx] = *p;
      int u8c = utfc_ptr2char(p, characterCombiner);
      if (*p < 0x80 && characterCombiner[0] == 0) {
         screenLinesUCG[idx] = 0;
      } else {
         // Non-basic multilingual plane character: display as ? or fullwidth ?.
         screenLinesUCG[idx] = u8c;
         for (Unt i = 0; i < MAX_COMBINED_SYMBOLS; ++i) {
            screenLinesCG[MAX_COMBINED_SYMBOLS*idx + i] = characterCombiner[i];
            if (characterCombiner[i] == 0)
                break;
         }
      }
      if (cells > 1)
         screenTextP[idx + 1] = 0;
      col += cells;
      idx += cells;
      p += c_len;
   }
   return col;
}

// Copy "buf[len]" to screenTextP["off"] and set decoration flags to "flags".
private void
copyTextWithDecos(int off, CS buf, int len, char flags) {
   MEMMOVE(screenTextP + off, buf, (Unt)len);
   memset(screenLinesUCG + off, 0, sizeof(Unt) * (Unt)len);
   for (int i = 0; i < len; ++i)
      screenDecosP[off + i].flags = flags;
}

//Get the line number relative to the current cursor position, i.e. the
//difference between line number and cursor position. Only look for lines that
//can be visible, folded lines don't count.
private LineNr
get_cursor_rel_lnum(Portal* po, LineNr lnum)  { // line number to get the result for
   LineNr cursor = po->cursor.lnum;
   LineNr retval = 0;

   if (hasAnyFolding(po)) {
      if (lnum > cursor) {
         while (lnum > cursor) {
            (void)getFoldsPortal(po, lnum, OUT &lnum, NULL, true, NULL);
            // if lnum and cursor are in the same fold, now lnum <= cursor
            if (lnum > cursor)
               retval++;
            lnum--;
         }
      } ei (lnum < cursor) {
         while (lnum < cursor) {
         (void)getFoldsPortal(po, lnum, NULL, OUT &lnum, true, NULL);
         // if lnum and cursor are in the same fold, now lnum >= cursor
         if (lnum < cursor)
             retval--;
         lnum++;
          }
      }
      // ei (lnum == cursor)
      //     retval = 0;
   } else
      retval = lnum - cursor;

   return retval;
}

// Display one folded line.
private void
fold_line(
   Portal* po,
   long fold_count,
   FoldInfo* foldinfo,
   LineNr lnum,
   int row
){
   Byte buf[1];
   Pos *top, *bot;
   LineNr lnume = lnum + fold_count - 1;
   int off = (int)(currScreenLineS - screenTextP);

   // Build the fold line:
   // 1. Add the commPortTypeG for the command-line portal
   // 2. Add the 'number' or 'relativenumber' column
   // 3. Compose the text
   // 4. Add the text
   // 5. set hiliting for the Visual area an other text
   int col = 0;

   // 1. Add the commPortTypeG for the command-line portal
   if (po == commPortPortG) {
      screenTextP[off] = commPortTypeG;
      screenDecosP[off].flags = getDecoFlags(HLF_AT);
      screenLinesUCG[off] = 0;
      ++col;
   }

# define RL_MEMSET(p, v, l) \
   do { \
   for (int ri = 0; ri < l; ++ri) \
       screenDecosP[off + (p) + ri].flags = v; \
   } while (0)

   // Set all decorations of the 'number' or 'relativenumber' column and the text
   RL_MEMSET(col, getDecoFlags(HLF_FL), (int)po->width - col);

   // If signs are being displayed, add two spaces.
   if (isSigncolumnOn(po)) {
      int len = po->width - col;
      if (len > 0) {
         if (len > 2)
            len = 2;
         copyTextWithDecos(off + col, (CS)"  ", len, getDecoFlags(HLF_FL));
         col += len;
      }
   }

   // 3. Add the 'number' or 'relativenumber' column
   int len = po->width - col;
   if (len > 0) {
      int w = number_width(po);
      long    num;
      char* fmt = "%*ld ";

      if (len > w + 1)
         len = w + 1;

      if (!po->o.relativeNumber)
         // 'norelativenumber'
         num = (long)lnum;
      else {
         // 'relativenumber', don't use negative numbers
         num = labs((long)get_cursor_rel_lnum(po, lnum));
         if (num == 0 && po->o.relativeNumber) {
             // 'number' + 'relativenumber': cursor line shows absolute
             // line number
             num = lnum;
             fmt = "%-*ld ";
         }
      }

      eeSnprintf(buf, sizeof(buf), fmt, w, num);
      copyTextWithDecos(off + col, buf, len, getDecoFlags(HLF_FL));
      col += len;
   }

   // 4. Compose the folded-line string with 'foldtext', if set.
   CS text = get_foldtext(po, lnum, lnume, foldinfo, buf);

   int txtcol = col;   // remember where text starts

   // 5. move the text to currScreenLineS.  Fill up with "fold" from @fillchars.
   //    Right-left text is put in columns 0 - number-col, normal text is put
   //    in columns number-col - portal-width.
   col = text_to_screenline(po, text, col);

   // Fill the rest of the line with the fold filler
   while (col < (int)po->width) {
      int c = fillCharsG.fold;
      if (c >= 0x80) {
         screenLinesUCG[off + col] = c;
         screenLinesCG[MAX_COMBINED_SYMBOLS*(off + col)] = 0;
         screenTextP[off + col] = 0x80; // avoid storing zero
      } else {
         screenLinesUCG[off + col] = 0;
         screenTextP[off + col] = c;
      }
      col++;
   }

   if (text != buf)
      eeglFree(text);

   // 6. set hiliting for the Visual area an other text.
   // If all folded lines are in the Visual area, hilite the line.
   if (VIsual_active && po->book == curPor->book) {
      if (LTOREQ_POS(curPor->cursor, VIsual)) {
         // Visual is after curPor->cursor
         top = &curPor->cursor;
         bot = &VIsual;
      } else {
         // Visual is before curPor->cursor
         top = &VIsual;
         bot = &curPor->cursor;
      }
      if (lnum >= top->lnum
         && lnume <= bot->lnum
         && (VIsual_mode != 'v'
             || ((lnum > top->lnum || (lnum == top->lnum && top->col == 0))
                  && (lnume < bot->lnum
                      || (lnume == bot->lnum && (bot->col) >= memGetBookLen(po->book, lnume))))
            )
      ) {
         if (VIsual_mode == Ctrl_V) {
            // Visual block mode: hilite the chars part of the block
            if (po->oldCursorFcol + txtcol < (ColNr)po->width) {
               if (po->oldCursorLcol != MAXCOL && po->oldCursorLcol + txtcol < (ColNr)po->width)
                  len = po->oldCursorLcol;
               else
                  len = po->width - txtcol;
               RL_MEMSET(
                   po->oldCursorFcol + txtcol, getDecoFlags(HLF_V), 
                   len - (int)po->oldCursorFcol
               );
            }
         } else {
            // Set all decorations of the text
            RL_MEMSET(txtcol, getDecoFlags(HLF_V), (int)po->width - txtcol);
         }
      }
   }

   screen_line(row + po->windowRow, po->windowCol, po->width, po->width, -1, 0);

   // Update cursorLineHeight and isCursorLineFolded if the cursor line was
   // updated (saves a call to plines() later).
   if (po == curPor && lnum <= curPor->cursor.lnum && lnume >= curPor->cursor.lnum) {
      curPor->cursorLineRow = row;
      curPor->cursorLineHeight = 1;
      curPor->isCursorLineFolded = true;
      curPor->cacheState |= (VALID_CHEIGHT|VALID_CROW);
   }
}

private typedef struct {
   int topEnd;
   int midStart;
   int midEnd;
   int botStart; //first row of the bot area that needs updating. 999 when no bot area updating
   LineNr modTop;
   LineNr modBot;
   LineNr oldBottLine;
   Boole eof;
   Boole topToMod; //redraw above modTop
} UpdatePortalInfo;


private void
updatePortalFinish(Portal* po, UpdatePortalInfo u) {
   Book* book = po->book;
   LineNr lnum = po->topLine;   // first line shown in portal
   static Boole recursive = false;   // being called recursively

   Boole didline = false; // if true, we finished the last line
   // Update all the portal rows.
   int idx = 0;      // first entry in lines[].height
   int row = 0;
   int srow = 0;
      
// remember what happened to the previous line, to know if
// check_visual_highlight() can be used
# define DID_NONE 1   // didn't update a line
# define DID_LINE 2   // updated a normal line
# define DID_FOLD 3   // updated a folded line

   Unt didUpdate = DID_NONE;
   long fold_count;
   LineNr syntax_last_parsed = 0;      // last parsed text line
   int j; 
   for (;;) {
      // stop updating when reached the end of the  portal (check for _past_
      // the end of the portal is at the end of the loop)
      if (row == (int)po->height) {
         didline = true;
         break;
      }

      // stop updating when hit the end of the file
      if (lnum > book->mem.lineCount) {
         u.eof = true;
         break;
      }

      //Remember the starting row of the line that is going to be dealt
      //with. It is used further down when the line doesn't fit.
      srow = row;

      //Update a line when it is in an area that needs updating, when it has changes or lines[idx]
      //is invalid. "bot_start" may be halfway a wrapped line after using deleteLinesFromPortal(),
      //check if the current line includes it. When syntax folding is being used, the saved syntax 
      //states will already have been updated, we can't see where the syntax state is
      //the same again, just update until the end of the portal.
      if (row < u.topEnd
         || (row >= u.midStart && row < u.midEnd)
         || u.topToMod
         || idx >= po->validLines
         || (row + po->lines[idx].height > u.botStart)
         || (u.modTop != 0
             && (lnum == u.modTop
                 || (lnum >= u.modTop
                      && (lnum < u.modBot
                           || didUpdate == DID_FOLD
                           || (didUpdate == DID_LINE
                               && syntax_present(po)
                               && (syntax_check_changed(lnum))
                              )
                           //match in fixed position might need redraw if lines were 
                           //inserted or deleted
                           || (po->firstMatch && book->needsRedraw && book->lineCountDiff != 0)
                         )
                   )
                )
            )
         || (po->o.cursorLine && lnum == po->cursor.lnum)
         || lnum == po->lastCursorLine
      ){
         if (lnum == u.modTop)
            u.topToMod = false;

         //When at start of changed lines: May scroll following lines up or down to minimize 
         //redrawing. Don't do this when the change continues until the end.
         //Don't scroll when redrawing the top, scrolled already above.
         if (lnum == u.modTop && u.modBot != MAXLNUM && row >= u.topEnd) {
            int old_rows = 0;
            int new_rows = 0;
            int xtra_rows;
            LineNr l;
            int i;

            //Count the old number of portal rows, using lines[], which
            //should still contain the sizes for the lines as they are currently displayed.
            for (i = idx; i < po->validLines; ++i) {
               //Only valid lines have a meaningful bookLnum.  Invalid
               //lines are part of the changed area.
               if (po->lines[i].isValid && po->lines[i].bookLnum == u.modBot)
                  break;
               old_rows += po->lines[i].height;
               if (po->lines[i].isValid && po->lines[i].lastBookLnum + 1 == u.modBot) {
                  //Must have found the last valid entry above modBot.
                  //Add following invalid entries.
                  ++i;
                  while (i < po->validLines && !po->lines[i].isValid)
                     old_rows += po->lines[i++].height;
                  break;
               }
            }

            if (i >= po->validLines) {
                //We can't find a valid line below the changed lines,
                //need to redraw until the end of the portal.
                //Inserting/deleting lines has no use.
                u.botStart = 0;
            } else {
               //Able to count old number of rows: Count new portal
               //rows, and may insert/delete lines
               j = idx;
               for (l = lnum; l < u.modBot; ++l) {
                  if (getFoldsPortal(po, l, NULL, OUT &l, true, NULL))
                     ++new_rows;
                  else {
                     if (l == po->topLine) {
                        int n = plines_win_nofill(po, l, false) + po->topFill;
                        n -= adjust_plines_for_skipcol(po);
                        if (n > (int)po->height)
                           n = (int)po->height;
                        new_rows += n;
                     } else
                        new_rows += plines_win(po, l, true);
                  }
                  ++j;
                  if (new_rows > (int)po->height - row - 2) {
                     // it's getting too much, must redraw the rest
                     new_rows = 9999;
                     break;
                  }
               }
               xtra_rows = new_rows - old_rows;
               if (xtra_rows < 0) {
                  //May scroll text up. If there is not enough remaining text or scrolling fails, 
                  //must redraw the rest. If scrolling works, must redraw the text
                  //below the scrolled text.
                  if (row - xtra_rows >= (int)po->height - 2)
                     u.modBot = MAXLNUM;
                  else {
                     check_for_delay(false);
                     if (deleteLinesFromPortal(po, row, -xtra_rows, false, false, 0) == FAIL)
                        u.modBot = MAXLNUM;
                     else
                        u.botStart = po->height + xtra_rows;
                  }
               } ei (xtra_rows > 0) {
                  //May scroll text down. If there is not enough
                  //remaining text of scrolling fails, must redraw the rest.
                  if (row + xtra_rows >= (int)po->height - 2)
                      u.modBot = MAXLNUM;
                  else {
                     check_for_delay(false);
                     if (insertLinesIntoPortal(po, row + old_rows, xtra_rows, false, false) == FAIL)
                        u.modBot = MAXLNUM;
                     ei (u.topEnd > row + old_rows)
                        // Scrolled the part at the top that requires updating down.
                        u.topEnd += xtra_rows;
                  }
               }

               // When not updating the rest, may need to move lines[] entries.
               if (u.modBot != MAXLNUM && i != j) {
                  if (j < i) {
                     int x = row + new_rows;

                     // move entries in lines[] upwards
                     for (;;) {
                        // stop at last valid entry in lines[]
                        if (i >= po->validLines) {
                           po->validLines = j;
                           break;
                        }
                        po->lines[j] = po->lines[i];
                        // stop at a line that won't fit
                        if (x + (int)po->lines[j].height > (int)po->height) {
                           po->validLines = j + 1;
                           break;
                        }
                        x += po->lines[j++].height;
                        ++i;
                     }
                     if (u.botStart > x)
                        u.botStart = x;
                  } else { // j > i
                     // move entries in lines[] downwards
                     j -= i;
                     po->validLines += j;
                     if (po->validLines > (int)po->height)
                        po->validLines = po->height;
                     for (i = po->validLines; i - j >= idx; --i)
                        po->lines[i] = po->lines[i - j];

                     //The lines[] entries for inserted lines are
                     //now invalid, but height may be used above. Reset to zero.
                     while (i >= idx) {
                        po->lines[i].height = 0;
                        po->lines[i--].isValid = false;
                     }
                  }
               }
            }
         }

         //When lines are folded, display one line for all of them.
         //Otherwise, display normally (can be several display lines when 'wrap' is on).
         fold_count = foldedCount(po, lnum, OUT &portFoldS);
         if (fold_count != 0) {
            fold_line(po, fold_count, &portFoldS, lnum, row);
            ++row;
            --fold_count;
            po->lines[idx].isFolded = true;
            po->lines[idx].lastBookLnum = lnum + fold_count;
            didUpdate = DID_FOLD;
         } ei (idx < po->validLines
             && po->lines[idx].isValid
             && po->lines[idx].bookLnum == lnum
             && lnum > po->topLine
             && !PORTAL_IS_POPUP(po)
             && srow + po->lines[idx].height > (int)po->height
             && diff_check_fill(po, lnum) == 0
         ) {
            // This line is not going to fit. Don't draw anything here, will draw "@  " lines below
            row = po->height + 1;
         } else {
            prepare_search_hl(po, &screenSearchP, lnum);
            // Let the syntax stuff know we skipped a few lines.
            if (syntax_last_parsed != 0 && syntax_last_parsed + 1 < lnum && syntax_present(po))
               syntax_end_parsing(po, syntax_last_parsed + 1);
            // Display one line.
            row = drawLineOnScreen(po, lnum, srow, po->height, 0);

            po->lines[idx].isFolded = false;
            po->lines[idx].lastBookLnum = lnum;
            didUpdate = DID_LINE;
            syntax_last_parsed = lnum;
         }

          po->lines[idx].bookLnum = lnum;
          po->lines[idx].isValid = true;

         //Past end of the portal or end of the portal. Note that after resizing po->height may 
         //end up too big. That's a problem elsewhere, but we prevent a crash here.
         if (row > (int)po->height || row + po->windowRow >= visibleRowsG) {
            // we may need the size of that too long line later on
            po->lines[idx].height = plines_win(po, lnum, true);
            ++idx;
            break;
         }
         po->lines[idx].height = row - srow;
         ++idx;
         lnum += fold_count + 1;
      } else {
          // If:
          // - 'number' is set and below inserted/deleted lines, or
          // - 'relativenumber' is set and cursor moved vertically,
          // the text doesn't need to be redrawn, but the number column does.
          if ((u.modTop != 0 && lnum >= u.modBot && book->needsRedraw && book->lineCountDiff != 0)
             || (po->o.relativeNumber && po->lastCursorLnumRnu != po->cursor.lnum)
         ) {
            fold_count = foldedCount(po, lnum, OUT &portFoldS);
            if (fold_count != 0)
               fold_line(po, fold_count, &portFoldS, lnum, row);
            else
               (void)drawLineOnScreen(po, lnum, srow, po->height, po->lines[idx].height);
         }

         // This line does not need to be drawn, advance to the next one.
         row += po->lines[idx++].height;
         if (row > (int)po->height)   // past end of screen
            break;
         lnum = po->lines[idx - 1].lastBookLnum + 1;
         didUpdate = DID_NONE;
      }

      if (lnum > book->mem.lineCount) {
         u.eof = true;
         break;
      }

      //Safety check: if any of the height values is wrong we might go over the end of lines[].
      if (idx >= visibleRowsG)
         break;
   }

   // End of loop over all portal lines.

   // Now that the portal has been redrawn with the old and new cursor line, update lastCursorLine.
   po->lastCursorLine = po->o.cursorLine ? po->cursor.lnum : 0;
   po->lastCursorLnumRnu = po->o.relativeNumber ? po->cursor.lnum : 0;

   if (idx > po->validLines)
      po->validLines = idx;

   // Let the syntax stuff know we stop parsing here.
   if (syntax_last_parsed != 0 && syntax_present(po))
      syntax_end_parsing(po, syntax_last_parsed + 1);

   // If we didn't hit the end of the file, and we didn't finish the last
   // line we were working on, then the line didn't fit.
   po->emptyRowCount = 0;
   po->fillerRowCount = 0;
   if (!u.eof && !didline) {
      if (lnum == po->topLine) {
         // Single line that does not fit! Don't overwrite it, it can be edited.
         po->bottomLine = lnum + 1;
      } ei (diff_check_fill(po, lnum) >= (int)po->height - srow) {
         // Portal ends in filler lines.
         po->bottomLine = lnum;
         po->fillerRowCount = po->height - srow;
      } ei (PORTAL_IS_POPUP(po)) {
         // popup line that doesn't fit is left as-is
         po->bottomLine = lnum;
      } else {
         drawVoidAtPortalEnd(po, fillCharsG.lastline, ' ', true, srow, po->height, HLF_AT);
         po->bottomLine = lnum;
      }
   } else {
      drawVerticalSeparator(po, row);
      if (u.eof) {     // we hit the end of the file
         po->bottomLine = book->mem.lineCount + 1;
         j = diff_check_fill(po, po->bottomLine);
         if (j > 0 && !po->bottFill) {
            // Display filler lines at the end of the file.
            Unt filler = (bookChar2Cells(fillCharsG.diff) > 1) ? '-' : fillCharsG.diff;
            if (row + j > (int)po->height)
               j = po->height - row;
            drawVoidAtPortalEnd(po, filler, filler, true, row, row + (int)j, HLF_DED);
            row += j;
          }
      }
      po->bottomLine = lnum;

      // Make sure the rest of the screen is blank.
      // write the "eob" character from @fillchars to rows that aren't part of the file.
      if (PORTAL_IS_POPUP(po))
         drawVoidAtPortalEnd(po, ' ', ' ', false, row, po->height, HLF_AT);
      else
         drawVoidAtPortalEnd(po, fillCharsG.eob, ' ', false, row, po->height, HLF_NONE);
  }

   // Reset the type of redrawing required, the portal has been updated.
   po->redrawType = 0;
   po->topFillOld = po->topFill;
   po->bottFillOld = po->bottFill;

   //There is a trick with bottomLine. If we invalidate it on each change that might modify it, 
   //this will cause a lot of expensive calls to plines() in update_topline() each time. 
   //Therefore the value of bottomLine is often approximated, and this value is used to
   //compute the value of topLine. If the value of bottomLine was wrong, check that the value of 
   //topLine is correct (cursor is on the visible part of the text).  If it's not, we need to 
   //redraw again. Mostly this just means scrolling up a few lines, so it doesn't look too bad.
   //Only do this for the current portal (where changes are relevant).
   po->cacheState |= VALID_BOTLINE;
   if (po == curPor && po->bottomLine != u.oldBottLine && !recursive) {
      recursive = true;
      curPor->cacheState &= ~VALID_TOPLINE;
      update_topline();   // may invalidate bottomLine again

      // New redraw either due to updated topline, wcol fix or reset skipcol.
      if (po->redrawType != 0) {
         // Don't update for changes in buffer again.
         Boole needsRedrawSaved = curBook->needsRedraw;
         curBook->needsRedraw = false;
         j = curBook->lineCountDiff;
         curBook->lineCountDiff = 0;
         curs_columns(true);
         updatePortal(curPor);
         curBook->needsRedraw = needsRedrawSaved;
         curBook->lineCountDiff = j;
      }
      // Other portals might have redrawType raised in update_topline().
      mustRedrawG = 0;
      
      Portal* cPo;
      FOR_ALL_PORTALS(cPo) {
         if (cPo->redrawType > mustRedrawG)
            mustRedrawG = cPo->redrawType;
      } 
      recursive = false;
   }
}

//Update a single portal.
//
//This may cause the portals below it also to be redrawn (when clearing the
//screen or scrolling lines).
//
//How the portal is redrawn depends on po->redrawType. Each type also implies the one below it.
//UPD_NOT_VALID     redraw the whole portal
//UPD_SOME_VALID    redraw the whole portal but do scroll when possible
//UPD_REDRAW_TOP    redraw the top rowsToUpdate portal lines, otherwise like UPD_VALID
//UPD_INVERTED      redraw the changed part of the Visual area
//UPD_INVERTED_ALL  redraw the whole Visual area
//UPD_VALID         1. scroll up/down to adjust for a changed topLine
//                  2. update lines at the top when scrolled down
//                  3. redraw changed text:
//                     - if po->book->needsRedraw set, update lines between
//                       needsRedrawTop and needsRedrawBott.
//                     - if po->redrawTop non-zero, redraw lines between
//                       po->redrawTop and po->redrawBott.
//                     - continue redrawing when syntax status is invalid.
//                  4. if scrolled up, update lines at the bottom.
//This results in three areas that may need updating:
//top:   from first row to topEnd (when scrolled down)
//mid: from midStart to midEnd (update inversion or changed text)
//bot: from botStart to last row (when scrolled up)
private void
updatePortal(Portal* po) {
   Book* book = po->book;
   int topEnd = 0; //Below last row of the top area that needs updating. 
                   //0 when no top area updating.
   int midStart = 999;//first row of the mid area that needs
                       //updating. 999 when no mid area updating.
   int midEnd = 0; //Below last row of the mid area that needs updating. 
                   //0 when no mid area updating.
   int bot_start = 999;//first row of the bot area that needs
                       // updating. 999 when no bot area updating
   int scrolled_down = false; //true when scrolled down when topLine got smaller a bit
   Boole top_to_mod = false;    

   int row;      // current portal row to display
   LineNr lnum;  // current buffer lnum to display
   int idx;      // current index in lines[]
   int srow;     // starting row of the current line

   Boole eof = false;   // if true, we hit the end of the file
   long j;
   LineNr oldBottLine = po->bottomLine;
   LineNr modTop = 0;
   LineNr modBot = 0;

   // This needs to be done only for the first portal when drawUpdateScreen() is called.
   if (!didUpdateOnePortal) {
      didUpdateOnePortal = true;
      start_search_hl();
      // When Visual area changed, may have to update selection.
      clip_update_selection(&clipboard);
   }

   int type = po->redrawType;

   if (type == UPD_NOT_VALID) {
      po->statusLineNeedsRedraw = true;
      po->validLines = 0;
   }

   // Portal frame is zero-height: nothing to draw.
   if (po->height == 0 || (po->frame->width == STATUS_HEIGHT && !portalIsPopup(po))) {
      po->redrawType = 0;
      return;
   }

   // Portal is zero-width: Only need to draw the separator.
   if (po->width == 0) {
      // draw the vertical separator right of this portal
      drawVerticalSeparator(po, 0);
      po->redrawType = 0;
      return;
   }

   // If this portal is into a terminal, redraw works completely differently.
   if (termDoUpdatePortal(po)) {
      termUpdatePortal(po);
      po->redrawType = 0;
      return;
   }

   searchInitHilite(po, &screenSearchP);

   // Make sure skipcol is valid, it depends on various options and the portal width.
   if (po->skipCol > 0 && (int)po->width > normalPortalColumnOffset(po)) {
      int w = 0;
      int width1 = po->width - normalPortalColumnOffset(po);
      int add = width1;

      while (w < po->skipCol) {
         if (w > 0)
            add = width1;
         w += add;
      }
      if (w != po->skipCol)
         // always round down, the higher value may not be valid
         po->skipCol = w - add;
   }

   // Force redraw when width of number column changes.
   int i = number_width(po);
   if (po->numberColWidth != i) {
      type = UPD_NOT_VALID;
      po->numberColWidth = i;
   } else {
      // Set modTop to the first line that needs displaying because of
      // changes. Set modBot to the first line after the changes.
      modTop = po->redrawTop;
      if (po->redrawBott != 0)
         modBot = po->redrawBott + 1;
      else
         modBot = 0;
      if (book->needsRedraw) {
         if (modTop == 0 || modTop > book->needsRedrawTop) {
            modTop = book->needsRedrawTop;
            // Need to redraw lines above the change that may be included in a pattern match.
            if (syntax_present(po)) {
               modTop -= book->syntax.syncLinebreaks;
               if (modTop < 1)
                  modTop = 1;
            }
         }
         if (modBot == 0 || modBot < book->needsRedrawBott)
            modBot = book->needsRedrawBott;

         //When @hlsearch is on and using a multi-line search pattern, a change in one line may 
         //make the Search hiliting in a previous line invalid. Simple solution: redraw all 
         //visible lines above the change. Same for a match pattern.
         if (screenSearchP.rm.regprog && re_multiline(screenSearchP.rm.regprog))
            top_to_mod = true;
         else {
            MatchItem* cur = po->firstMatch;
            while (cur) {
               if (cur->match.regprog && re_multiline(cur->match.regprog)) {
                  top_to_mod = true;
                  break;
               }
               cur = cur->next;
            }
         }
      }

      if (searchLastLnumG > 0) {
         // CurSearch was used last time, need to redraw the line with it to
         // avoid having two matches hilited with CurSearch.
         if (modTop == 0 || modTop > searchLastLnumG)
            modTop = searchLastLnumG;
         if (modBot == 0 || modBot < searchLastLnumG + 1)
            modBot = searchLastLnumG + 1;
      }

      if (modTop != 0 && hasAnyFolding(po)) {
         // A change in a line can cause lines above it to become folded or unfolded. Find the top 
         // most buffer line that may be affected. If the line was previously folded and displayed,
         // get the first line of that fold. If the line is folded now, get the first folded line.
         // Use the minimum of these two.

         // Find last valid lines[] entry above modTop. Set lnumt to the line below it. If there 
         // is no valid entry, use topLine. Find the first valid lines[] entry below modBot. Set 
         // lnumb to this line. If there is no valid entry, use MAXLNUM.
         LineNr lnumt = po->topLine;
         LineNr lnumb = MAXLNUM;
         for (i = 0; i < po->validLines; ++i) {
            if (!po->lines[i].isValid) {
               continue;
            }
            if (po->lines[i].lastBookLnum < modTop)
               lnumt = po->lines[i].lastBookLnum + 1;
            if (lnumb == MAXLNUM && po->lines[i].bookLnum >= modBot) {
               lnumb = po->lines[i].bookLnum;
            }
         } 

         (void)getFoldsPortal(po, modTop, OUT &modTop, NULL, true, NULL);
         if (modTop > lnumt)
            modTop = lnumt;

         // Now do the same for the bottom line (one above modBot).
         --modBot;
         (void)getFoldsPortal(po, modBot, NULL, OUT &modBot, true, NULL);
         ++modBot;
         if (modBot < lnumb)
            modBot = lnumb;
      }

      // When a change starts above topLine and the end is below
      // topLine, start redrawing at topLine. If the end of the change is above topLine: do like 
      // no change was made, but redraw the first line to find changes in syntax.
      if (modTop != 0 && modTop < po->topLine) {
         if (modBot > po->topLine)
            modTop = po->topLine;
         ei (syntax_present(po))
            topEnd = 1;
      }
   }
   po->redrawTop = 0;   // reset for next time
   po->redrawBott = 0;
   searchLastLnumG = 0;

   // When only displaying the lines at the top, set topEnd. Used when
   // portal has scrolled down for msg_scrolled.
   if (type == UPD_REDRAW_TOP) {
      j = 0;
      for (i = 0; i < po->validLines; ++i) {
         j += po->lines[i].height;
         if (j >= po->rowsToUpdate) {
            topEnd = j;
            break;
         }
      }
      if (topEnd == 0)
         // not found (cannot happen?): redraw everything
         type = UPD_NOT_VALID;
      else
         // top area defined, the rest is UPD_VALID
         type = UPD_VALID;
   }

   //Trick: we want to avoid clearing the screen twice. screenclear() will
   //set "isScreenClearedP" to true.  The special value MAYBE (which is still
   //non-zero and thus not false) will indicate that screenclear() was not called.
   if (isScreenClearedP)
      isScreenClearedP = MAYBE;

   // If there are no changes on the screen that require a complete redraw, handle three cases:
   // 1: we are off the top of the screen by a few lines: scroll down
   // 2: po->topLine is below po->lines[0].bookLnum: may scroll up
   // 3: po->topLine is po->lines[0].bookLnum: find first entry in lines[] that needs updating.
   if ((type == UPD_VALID || type == UPD_SOME_VALID
            || type == UPD_INVERTED || type == UPD_INVERTED_ALL)
       && !po->bottFill && !po->bottFillOld
   ) {
      if (modTop != 0
         && po->topLine == modTop
         && (!po->lines[0].isValid
             || po->topLine == po->lines[0].bookLnum)
      ) {
          //topLine is the first changed line and portal is not scrolled,
          //the scrolling from changed lines will be done further down.
      } ei (po->lines[0].isValid
         && (po->topLine < po->lines[0].bookLnum
             || (po->topLine == po->lines[0].bookLnum && po->topFill > po->topFillOld)
            )
      ) {
         // New topline is above old topline: May scroll down.
         if (hasAnyFolding(po)) {
            // count the number of lines we are off, counting a sequence of folded lines as one
            j = 0;
            for (LineNr ln = po->topLine; ln < po->lines[0].bookLnum; ++ln) {
               ++j;
               if (j >= po->height - 2)
                  break;
               (void)getFoldsPortal(po, ln, NULL, OUT &ln, true, NULL);
            }
         } else
            j = po->lines[0].bookLnum - po->topLine;
         if (j < po->height - 2) {     // not too far off
            i = plines_m_win(po, po->topLine, po->lines[0].bookLnum - 1, po->height);
            // insert extra lines for previously invisible filler lines
            if (po->lines[0].bookLnum != po->topLine)
                i += diff_check_fill(po, po->lines[0].bookLnum) - po->topFillOld;
            if (i < (int)po->height - 2) {  // less than a screen off
               // Try to insert the correct number of lines.
               // If not the last portal, delete the lines at the bottom.
               // insertLinesIntoPortal may fail when the terminal can't do it.
               if (i > 0)
                  check_for_delay(false);
               if (insertLinesIntoPortal(po, 0, i, false, po == firstPor) == OK) {
                  if (po->validLines != 0) {
                     // Need to update rows that are new, stop at the first one that scrolled down
                     topEnd = i;
                     scrolled_down = true;

                     // Move the entries that were scrolled, disable
                     // the entries for the lines to be redrawn.
                     if ((po->validLines += j) > (int)po->height)
                        po->validLines = po->height;
                     for (idx = po->validLines; idx - j >= 0; idx--)
                        po->lines[idx] = po->lines[idx - j];
                     while (idx >= 0)
                        po->lines[idx--].isValid = false;
                  }
               } else
                  midStart = 0; //redraw all lines
            } else
               midStart = 0;    //redraw all lines
         } else
            midStart = 0;       //redraw all lines
      } else {
         // New topline is at or below old topline: May scroll up.
         // When topline didn't change, find first entry in lines[] that needs updating.

         // Try to find po->topLine in po->lines[].bookLnum.  The check
         // for "visibleRowsG" is in case "height" is incorrect somehow.
         j = -1;
         row = 0;
         for (i = 0; i < po->validLines && i < visibleRowsG; i++) {
            if (po->lines[i].isValid && po->lines[i].bookLnum == po->topLine) {
               j = i;
               break;
            }
            row += po->lines[i].height;
         }
         if (j == -1) {
            // if po->topLine is not in po->lines[].bookLnum redraw all lines
            midStart = 0;
         } else {
            // Try to delete the correct number of lines.
            // po->topLine is at po->lines[i].bookLnum.
            // If the topline didn't change, delete old filler lines,
            // otherwise delete filler lines of the new topline...
            if (po->lines[0].bookLnum == po->topLine)
               row += po->topFillOld;
            else
               row += diff_check_fill(po, po->topLine);
            // ... but don't delete new filler lines.
            row -= po->topFill;
            if (row > visibleRowsG)  // just in case
                row = visibleRowsG;
            if (row > 0) {
               check_for_delay(false);
               if (deleteLinesFromPortal(po, 0, row, false, po == firstPor, 0) == OK)
                  bot_start = po->height - row;
               else
                  midStart = 0;      // redraw all lines
            }
            if ((row == 0 || bot_start < 999) && po->validLines != 0) {
               // Skip the lines (below the deleted lines) that are still valid and don't need 
               // redrawing. Copy their info upwards, to compensate for the deleted lines. Set
               // bot_start to the first row that needs redrawing.
               bot_start = 0;
               idx = 0;
               for (;;) {
                  po->lines[idx] = po->lines[j];
                  // stop at line that didn't fit, unless it is still valid (no lines deleted)
                  if (row > 0 && bot_start + row + (int)po->lines[j].height > (int)po->height) {
                     po->validLines = idx + 1;
                     break;
                  }
                  bot_start += po->lines[idx++].height;

                  // stop at the last valid entry in lines[].height
                  if (++j >= po->validLines) {
                      po->validLines = idx;
                      break;
                  }
               }
               // Correct the first entry for filler lines at the top
               // when it won't get updated below.
               if (po->o.diff && bot_start > 0) {
                  int n = plines_win_nofill(po, po->topLine, false)
                        + po->topFill - adjust_plines_for_skipcol(po);
                  if (n > (int)po->height)
                     n = po->height;
                  po->lines[0].height = n;
               }
            }
         }
      }

      // When starting redraw in the first line, redraw all lines.
      if (midStart == 0)
          midEnd = po->height;

      // When deleteLinesFromPortal() or insertLinesIntoPortal() caused the screen to be
      // cleared (only happens for the first portal) or when screenclear()
      // was called directly above, "mustRedrawG" will have been set to
      // UPD_NOT_VALID, need to reset it here to avoid redrawing twice.
      if (isScreenClearedP == true)
          mustRedrawG = 0;
   } else {
      // Not UPD_VALID or UPD_INVERTED: redraw all lines.
      midStart = 0;
      midEnd = po->height;
   }

   if (type == UPD_SOME_VALID) {
      // UPD_SOME_VALID: redraw all lines.
      midStart = 0;
      midEnd = po->height;
      type = UPD_NOT_VALID;
   }

   // check if we are updating or removing the inverted part
   if ((VIsual_active && book == curPor->book)
       || (po->prevVisualEnd != 0 && type != UPD_NOT_VALID)
   ) {
      LineNr from, to;

      if (VIsual_active) {
         if (VIsual_mode != po->prevVisualMode || type == UPD_INVERTED_ALL) {
            //If the type of Visual selection changed, redraw the whole
            //selection. Also when the ownership of the X selection is gained or lost.
            if (curPor->cursor.lnum < VIsual.lnum) {
               from = curPor->cursor.lnum;
               to = VIsual.lnum;
            } else {
               from = VIsual.lnum;
               to = curPor->cursor.lnum;
            }
            // redraw more when the cursor moved as well
            if (po->prevVisualEnd < from)
               from = po->prevVisualEnd;
            if (po->prevVisualEnd > to)
               to = po->prevVisualEnd;
            if (po->oldVisualLnum < from)
               from = po->oldVisualLnum;
            if (po->oldVisualLnum > to)
               to = po->oldVisualLnum;
         } else {
            // Find the line numbers that need to be updated: The lines between the old cursor 
            // position and the current cursor position.  Also check if the Visual position changed.
            if (curPor->cursor.lnum < po->prevVisualEnd) {
               from = curPor->cursor.lnum;
               to = po->prevVisualEnd;
            } else {
               from = po->prevVisualEnd;
               to = curPor->cursor.lnum;
               if (from == 0)   // Visual mode just started
                  from = to;
            }

            if (VIsual.lnum != po->oldVisualLnum || VIsual.col != po->oldVisualCol) {
               if (po->oldVisualLnum < from && po->oldVisualLnum != 0)
                  from = po->oldVisualLnum;
               if (po->oldVisualLnum > to)
                  to = po->oldVisualLnum;
               if (VIsual.lnum < from)
                  from = VIsual.lnum;
               if (VIsual.lnum > to)
                  to = VIsual.lnum;
            }
         }

         // If in block mode and changed column or curPor->cursWant: update all lines.
         // First compute the actual start and end column.
         if (VIsual_mode == Ctrl_V) {
            ColNr       fromc, toc;

            getvcols(po, &VIsual, &curPor->cursor, &fromc, &toc);
            ++toc;
            if (curPor->cursWant == MAXCOL) {
               int cursor_above = curPor->cursor.lnum < VIsual.lnum;

               // Need to find the longest line.
               toc = 0;
               Pos pos;
               pos.coladd = 0;
               for (pos.lnum = curPor->cursor.lnum; 
                    cursor_above ? pos.lnum <= VIsual.lnum : pos.lnum >= VIsual.lnum;
                    pos.lnum += cursor_above ? 1 : -1
               ) {
                  pos.col = (int)memGetBookLen(po->book, pos.lnum);
                  ColNr t;
                  bookGetVirtualColInVirtualMode(po, &pos, NULL, NULL, &t);
                  if (toc < t)
                     toc = t;
               }
               ++toc;
            }

            if (fromc != po->oldCursorFcol || toc != po->oldCursorLcol) {
               if (from > VIsual.lnum)
                  from = VIsual.lnum;
               if (to < VIsual.lnum)
                  to = VIsual.lnum;
            }
            po->oldCursorFcol = fromc;
            po->oldCursorLcol = toc;
         }
      } else {
         //Use the line numbers of the old Visual area.
         if (po->prevVisualEnd < po->oldVisualLnum) {
            from = po->prevVisualEnd;
            to = po->oldVisualLnum;
         } else {
            from = po->oldVisualLnum;
            to = po->prevVisualEnd;
         }
      }

      //There is no need to update lines above the top of the portal.
      if (from < po->topLine)
         from = po->topLine;

      //If we know the value of bottomLine, use it to restrict the update to
      //the lines that are visible in the portal.
      if (po->cacheState & VALID_BOTLINE) {
         if (from >= po->bottomLine)
            from = po->bottomLine - 1;
         if (to >= po->bottomLine)
            to = po->bottomLine - 1;
      }

      //Find the minimal part to be updated. Watch out for scrolling that made entries in lines[]
      //invalid. E.g., CTRL-U makes the first half of lines[] invalid and sets topEnd; need to 
      //redraw from topEnd to the "to" line. A middle mouse click with a Visual selection may 
      //change the text above the Visual area and reset isValid, do count these for midEnd (in srow)
      if (midStart > 0) {
         lnum = po->topLine;
         idx = 0;
         srow = 0;
         if (scrolled_down)
            midStart = topEnd;
         else
            midStart = 0;
         while (lnum < from && idx < po->validLines) {  // find start
            if (po->lines[idx].isValid)
               midStart += po->lines[idx].height;
            ei (!scrolled_down)
               srow += po->lines[idx].height;
            ++idx;
            if (idx < po->validLines && po->lines[idx].isValid)
               lnum = po->lines[idx].bookLnum;
            else
               ++lnum;
         }
         srow += midStart;
         midEnd = po->height;
         for ( ; idx < po->validLines; ++idx) {     // find end
            if (po->lines[idx].isValid && po->lines[idx].bookLnum >= to + 1) {
               // Only update until first row of this line
               midEnd = srow;
               break;
            }
            srow += po->lines[idx].height;
         }
      }
   }

   if (VIsual_active && book == curPor->book) {
      po->prevVisualMode = VIsual_mode;
      po->prevVisualEnd = curPor->cursor.lnum;
      po->oldVisualLnum = VIsual.lnum;
      po->oldVisualCol = VIsual.col;
      po->oldCursWant = curPor->cursWant;
   } else {
      po->prevVisualMode = 0;
      po->prevVisualEnd = 0;
      po->oldVisualLnum = 0;
      po->oldVisualCol = 0;
   }

   // reset gotInterruptG, otherwise regexp won't work
   int save_gotInterrupt = gotInterruptG;
   gotInterruptG = 0;
   portFoldS.fi_level = 0;
   updatePortalFinish(
         po, 
         (UpdatePortalInfo){
            topEnd, midStart, midEnd, bot_start, modTop, modBot, oldBottLine, eof, top_to_mod
         }
   );
   
   // restore gotInterruptG, unless CTRL-C was hit while redrawing
   if (!gotInterruptG)
      gotInterruptG = save_gotInterrupt;
}

//Redraw as soon as possible. When the command line is not scrolled, redraw right away and restore
//what was on the command line. Return a code indicating what happened.
pub int
redraw_asap(int type) {
   int cols = screenLinesColsG;
   int ret = 0;
   Unt* screenlineUC = NULL;   // copy from screenLinesUCG[]
   Arr(Unt) screenlineC;   // copy from screenLinesCG[][]

   redraw_later(type);
   if (msg_scrolled
          || (stateG != MODE_NORMAL && stateG != MODE_NORMAL_BUSY)
          || isExitingG)
      return ret;

   // Allocate space to save the text displayed in the command line area.
   int rows = screenLinesRowsG - commlineRowG;
   Arr(Byte) screenline = LALLOC_MULT(Byte, rows * cols); //copy from screenTextP[]
   Arr(Unt) screenDecosP = LALLOC_MULT(Unt, rows * cols); //copy from screenDecosP[]
   if (!screenline)
      ret = 2;
   screenlineUC = LALLOC_MULT(Unt, rows * cols);
   screenlineC = LALLOC_MULT(Unt, MAX_COMBINED_SYMBOLS * rows * cols);

   if (ret != 2) {
      //Save the text displayed in the command line area.
      for (int r = 0; r < rows; ++r) {
         MEMMOVE(
            screenline + r * cols, 
            screenTextP + lineStartsP[commlineRowG + r], 
            (Unt)cols * sizeof(Byte)
         );
         MEMMOVE(
            screenDecosP + r * cols, 
            screenDecosP + lineStartsP[commlineRowG + r], 
            (Unt)cols * sizeof(Unt)
         );
         MEMMOVE(
            screenlineUC + r * cols, 
            screenLinesUCG + lineStartsP[commlineRowG + r],
            (Unt)cols * sizeof(Unt)
         );
         MEMMOVE(
             screenlineC + MAX_COMBINED_SYMBOLS * r * cols, 
             screenLinesCG + MAX_COMBINED_SYMBOLS * lineStartsP[commlineRowG + r], 
             MAX_COMBINED_SYMBOLS * (Unt)cols * sizeof(Unt)
         );
      }

      drawUpdateScreen(0);
      ret = 3;

      if (mustRedrawG == 0) {
         int off = (int)(currScreenLineS - screenTextP);

         // Restore the text displayed in the command line area.
         for (int r = 0; r < rows; ++r) {
            MEMMOVE(currScreenLineS, screenline + r * cols, (Unt)cols * sizeof(Byte));
            MEMMOVE(screenDecosP + off, screenDecosP + r * cols, (Unt)cols * sizeof(Unt));
            MEMMOVE(screenLinesUCG + off, screenlineUC + r * cols, (Unt)cols * sizeof(Unt));
            MEMMOVE(
               screenLinesCG + MAX_COMBINED_SYMBOLS * off, 
               screenlineC + MAX_COMBINED_SYMBOLS * r * cols, 
               MAX_COMBINED_SYMBOLS * (Unt)cols * sizeof(Unt)
            );
            screen_line(commlineRowG + r, 0, cols, cols, -1, 0);
         }
         ret = 4;
      }
   }

   eeglFree(screenline);
   eeglFree(screenDecosP);
   eeglFree(screenlineUC);
   eeglFree(screenlineC);

   // Show the intro message when appropriate.
   maybe_intro_message();
   setcursor();
   return ret;
}

//Invoked after an asynchronous callback is called.
//If an echo command was used the cursor needs to be put back where
//it belongs. If hiliting was changed a redraw is needed.
//If "call_drawUpdateScreen" is false don't call drawUpdateScreen() when at the command line.
//If "redraw_message" is true.
pub void
redraw_after_callback(int call_drawUpdateScreen, int do_message) {
   ++redrawingForCallbackS;

   if (   stateG == MODE_HITRETURN || stateG == MODE_ASKMORE
       || stateG == MODE_SETWSIZE  || stateG == MODE_EXTERNCMD
       || stateG == MODE_CONFIRM
   ) {
      if (do_message)
         repeat_message();
   } ei (stateG & MODE_COMMLINE) {
      if (pum_visible())
         cmdline_pum_display();

      // Don't redraw when in prompt_for_number().
      if (commlineRowG > 0) {
         //Redrawing only works when the screen didn't scroll. Don't clear wildmenu entries.
         if (msg_scrolled == 0 && wild_menu_showing == 0 && call_drawUpdateScreen)
            drawUpdateScreen(0);

         // Redraw in the same position, so that the user can continue editing the command.
         redrawCommlineEx(false);
      }
   } ei ((stateG & (MODE_NORMAL | MODE_INSERT | MODE_TERMINAL)) != 0) {
      update_topline();
      validate_cursor();

      // keep the command line if possible
      drawUpdateScreen(UPD_VALID_NO_UPDATE);
      setcursor();

      if (msg_scrolled == 0) {
         // don't want a hit-enter prompt when something else is displayed
         msg_didany = false;
         need_wait_return = false;
      }
   }
   cursor_on();
   out_flush();

   --redrawingForCallbackS;
}

//Redraw the current portal later, with drawUpdateScreen(type).
//Set mustRedrawG only if not already set to a higher value.
//E.g. if mustRedrawG is UPD_CLEAR, type UPD_NOT_VALID will do nothing.
pub void
redraw_later(int type) {
   redrawPortLater(curPor, type);
}

pub void
redrawPortLater(Portal* po, Unt type) {
   if (!isExitingG && !redraw_not_allowed && po->redrawType < type) {
      po->redrawType = type;
      if (type >= UPD_NOT_VALID)
          po->validLines = 0;
      if (mustRedrawG < type)   // mustRedrawG is the maximum of all portals
          mustRedrawG = type;
   }
}

//Force a complete redraw later.  Also resets the hiliting.  To be used
//after executing a shell command that messes up the screen.
pub void
redraw_later_clear(void) {
   redraw_all_later(UPD_CLEAR);
   resetActiveDeco();
}

// Mark all portals to be redrawn later.  Except popup portals.
pub void
redraw_all_later(Unt type) {
   Portal* po;
   FOR_ALL_PORTALS(po)
      redrawPortLater(po, type);
   // This may be needed when switching tabs.
   drawSetMustRedraw(type);
}

#if 0  // not actually used yet, it probably should
//Mark all portals, including popup portals, to be redrawn.
pub void
redraw_all_portals_later(int type) {
   redraw_all_later(type);
   popup_redraw_all();      // redraw all popup portals
}
#endif

//Set "mustRedrawG" to "type" unless it already has a higher value or it is currently not allowed.
pub void
drawSetMustRedraw(Unt type) {
   if (!redraw_not_allowed && mustRedrawG < type)
      mustRedrawG = type;
}

//Mark all portals that are editing the current buffer to be updated later.
pub void
drawCurBookLater(int type) {
   drawBookLater(curBook, type);
}

pub void
drawBookLater(Book* book, int type) {
   Portal* po;
   FOR_ALL_PORTALS(po) {
      if (po->book == book)
          redrawPortLater(po, type);
   }
   // terminal in popup portal is not in list of portals
   if (curPor->book == book)
      redrawPortLater(curPor, type);
}

pub void
drawBookLineLater(Book* book, LineNr lnum) {
   Portal* po;
   FOR_ALL_PORTALS(po) {
      if (po->book == book && lnum >= po->topLine && lnum < po->bottomLine)
          drawPortLineLater(po, lnum);
   } 
}

pub void
drawBookAndStatusLater(Book* book, int type) {
   if (wild_menu_showing != 0)
      // Don't redraw while the command line completion is displayed, it would disappear.
      return;
   Portal   *po;
   FOR_ALL_PORTALS(po) {
      if (po->book == book) {
         redrawPortLater(po, type);
         po->statusLineNeedsRedraw = true;
      }
   }
}

// mark all status lines for redraw; used after first :cd
pub void
status_redraw_all(void) {
   Portal* po;
   FOR_ALL_PORTALS(po) {
      po->statusLineNeedsRedraw = true;
      redraw_later(UPD_VALID);
   } 
}

// mark all status lines of the current book for redraw
pub void
drawAllStatusLinesOfCurBookLater(void) {
   Portal* po;
   FOR_ALL_PORTALS(po) {
      if (po->book == curBook) {
         po->statusLineNeedsRedraw = true;
         redraw_later(UPD_VALID);
      }
   } 
}

// Redraw all status lines that need to be redrawn.
pub void
redraw_statuslines(void) {
   Portal* po;
   FOR_ALL_PORTALS(po) {
      if (po->statusLineNeedsRedraw)
         redrawPortalStatusLine(po, false);
   } 

   if (needRedrawTabpanelG)
      draw_tabpanel();
}

// Redraw all status lines at the bottom of frame "frp".
pub void
redrawAllStatusLinesInFrame(Frame *fr) {
   if (fr->layout == FR_LEAF)
      fr->port->statusLineNeedsRedraw = true;
   ei (fr->layout == FR_ROW) {
      FOR_ALL_FRAMES(fr, fr->child)
         redrawAllStatusLinesInFrame(fr);
   } else { // frp->layout == FR_COL
      fr = fr->child;
      while (fr->next)
          fr = fr->next;
      redrawAllStatusLinesInFrame(fr);
   }
}

//Changed something in the current portal, at book line "lnum", which requires that line and 
//possibly other lines to be redrawn.
//Used when entering/leaving Insert mode with the cursor on a folded line. Used to remove the "$" 
//from a change command.
//Note that when also inserting/deleting lines redrawTop and redrawBott
//may become invalid and the whole portal will have to be redrawn.
pub void
drawPortLineLater(Portal* po, LineNr lnum) {
   redrawPortRangeLater(po, lnum, lnum);
}

pub void
redrawPortRangeLater(Portal* po, LineNr first, LineNr last) {
   if (last >= po->topLine && first < po->bottomLine) {
      if (po->redrawTop == 0 || po->redrawTop > first)
         po->redrawTop = first;
      if (po->redrawBott == 0 || po->redrawBott < last)
         po->redrawBott = last;
      redrawPortLater(po, UPD_VALID);
   }
}

//}}}
//{{{draw line (mid-level code)

#define MB_FILLER_CHAR '<'  //character used when a double-width character doesn't fit.

private void
overlayDeco(OUT Decoration* baseDeco, OverlayDeco overlayingDeco) {
   switch (overlayingDeco) {
   case OVERLAY_DECO_INVERT: baseDeco->flags |= DECO_INVERSE; return;
   case OVERLAY_DECO_UNDER: baseDeco->flags |= DECO_UNDERLINE; return;
   case OVERLAY_DECO_UNDERCURL: baseDeco->flags |= DECO_UNDERCURL; return;
   case OVERLAY_DECO_UNDERDASH: baseDeco->flags |= DECO_UNDERDASH; return;
   case OVERLAY_DECO_ALTERED_BG: baseDeco->flags |= DECO_ALTERED_BG; return;
   default: return;
   }
}

//Used when @cursorlineopt contains "screenline": compute the margins between
//which the hiliting is used.
private void
computeHilitingMargins(Portal* po, OUT int* leftCol, OUT int* rightCol) {
   //cache previous calculations depending on virtCol
   static int saved_virtCol;
   static Portal* prev_wp;
   static int prev_width1;
   static int prev_leftCol;
   static int prev_rightCol;

   int cur_col_off = normalPortalColumnOffset(po);
   int width1 = po->width - cur_col_off;

   if (saved_virtCol == po->virtCol && prev_wp == po && prev_width1 == width1) {
      *rightCol = prev_rightCol;
      *leftCol = prev_leftCol;
      return;
   }

   *leftCol = 0;
   *rightCol = width1;

   if (po->virtCol >= (ColNr)width1 && width1 > 0)
      *rightCol = width1 + ((po->virtCol - width1) / width1 + 1) * width1;
   if (po->virtCol >= (ColNr)width1 && width1 > 0)
      *leftCol = (po->virtCol - width1) / width1 * width1 + width1;

   // cache values
   prev_leftCol = *leftCol;
   prev_rightCol = *rightCol;
   prev_wp = po;
   prev_width1 = width1;
   saved_virtCol = po->virtCol;
}

// structure with variables passed between drawLineOnScreen() and other functions
private typedef struct {
   Byte drawState;   // what to draw next

   LineNr lnum;      // line number to be drawn

   int startrow;   // first row in the portal to be drawn
   int endRow;
   
   int row;      // row in the portal, excl windowRow
   int screen_row;   // row on the screen, incl windowRow

   long vcol;      // virtual column, before wrapping
   int col;      // visual column on screen, after wrapping
   int virtualOffset;   // offset for virtual text
   int eol_hl_off;   // 1 if hilited char after EOL
   Unt off;      // offset in screenTextP/screenDecosP
   CS ptr; // current position in text line
   CS line; // current text line start

   Decoration portalDeco;   // background for the whole portal, except margins and "~" lines.
   Decoration portcolorDeco;   // decorations from 'portcolor'
   Decoration cursorlineDeco;   // set when 'cursorline' active
   Decoration lineDeco;   // for the whole line, includes 'cursorline'
   int screen_line_flags;  // flags for screen_line()
   int fromcol;   // start of inverting
   int tocol;      // end of inverting

   long vcol_sbr;       // virtual column after showbreak
   int need_showbreak;       // overlong line, skipping first x chars
   int dont_use_showbreak; // do not use 'showbreak'
   int textPropAbove_count;

   // true when 'cursorlineopt' has "screenline" and cursor is in this line
   int cul_screenline;
   Decoration charDeco;   // decorations for the next character

   int countExtraBytes;   // number of extra bytes (for virtual text)
   CS extraBytes; // virtual text. This is only used when c_extra and c_final are ZERO
   CS p_extra_free;  // extraBytes buffer that needs to be freed
   Decoration extraDeco; // decorations for extraBytes, should be combined with portalDeco if needed
   int toSkipBeforeDeco;    // chars to skip before using extraDeco
   Unt c_extra;   // extra chars, virtual text
   Unt c_final;   // final char, mandatory if set
   int extra_for_textprop; // countExtraBytes set for textprop
   int start_extra_for_textprop; // extra_for_textprop was just set

   // saved "extra" items for when drawState becomes WL_LINE (again)
   int saved_n_extra;
   CS saved_p_extra;
   CS saved_p_extra_free;
   Decoration saved_extraDeco;
   int saved_toSkipBeforeDeco;
   int saved_extra_for_textprop;
   int saved_c_extra;
   int saved_c_final;
   Decoration saved_charDeco;

   Byte extra[NUMBUFLEN + MB_MAXBYTES]; // "%ld " must fit in here, as well any text sign

   Unt diff_hlf;   // type of diff hiliting
   int filler_lines;   // nr of filler lines to be drawn
   int filler_todo;   // nr of filler lines still to do + 1
   SignHilite signHilites;
   // do consider wrapping in linebreak mode only after encountering a non whitespace char
   Boole needLinebreak;
   int textPropNext; // next text property to use
   Boole syntaxHilitingOn;
   int* anyEmsgSave;
   int cellsToSkip;   // nr of cells to skip for leftCol or skipCol
   long bufferLen; // length of the currently built part of the text line
   int changeIndex;
   Short searchHiId;
   Boole inMultispace;   // in multiple consecutive spaces
   int multispacePos;   // position in lcs-multispace string
} DrawCtx;


// drawState values for items that are drawn in sequence:
#define WL_START    0                 // nothing done yet, must be zero
#define WL_COMMLINE (WL_START + 1)    // commline portal column
#define WL_SIGN     (WL_COMMLINE + 1) // column for signs
#define WL_NR       (WL_SIGN + 1)     // line number
#define WL_BRI      (WL_NR + 1)       // @breakindent
#define WL_SBR      (WL_BRI + 1)      // @showbreak or @diff
#define WL_LINE     (WL_SBR + 1)      // text in the line

// Return true if CursorLineSign hilite is to be used.
private int
useCursorLineHilite(Portal* po, LineNr lnum) {
   return po->o.cursorLine && lnum == po->cursor.lnum;
}

//Get information needed to display the sign in line "m->lnum" in portal "po".
//If "nrcol" is true, the sign is going to be displayed in the number column.
//Otherwise the sign is going to be displayed in the sign column.
private void
get_sign_display_info(int nrcol, Portal* po, DrawCtx* m) {
   int   text_sign;

   // Draw two cells with the sign value or blank.
   m->c_extra = ' ';
   m->c_final = ZERO;
   if (nrcol)
      m->countExtraBytes = number_width(po) + 1;
   else {
      if (useCursorLineHilite(po, m->lnum))
         m->charDeco = getFullDecoration(HLF_CLS);
      else
         m->charDeco = getFullDecoration(HLF_SC);
      m->countExtraBytes = 2;
   }

   if (m->row == m->startrow + m->filler_lines && m->filler_todo <= 0) {
      text_sign = (m->signHilites.text) ? m->signHilites.typeNr : 0;
      if (text_sign != 0) {
         m->extraBytes = m->signHilites.text;
         if (m->extraBytes) {
            if (nrcol) {
               int width = number_width(po) - 2;

               memset(m->extra, ' ', width);
               m->countExtraBytes = width;
               m->countExtraBytes += eeSnprintf(
                     m->extra + width, sizeof(m->extra) - width, "%s ", m->extraBytes
               );
               m->extraBytes = m->extra;
            } else
               m->countExtraBytes = (int)STRLEN(m->extraBytes);

            m->c_extra = ZERO;
            m->c_final = ZERO;
         }

         if (useCursorLineHilite(po, m->lnum) && m->signHilites.cursorLineHiId < SHORT)
            m->charDeco.hiId = m->signHilites.cursorLineHiId;
         else
            m->charDeco.hiId = m->signHilites.textHiId;
      }
   }
}

// Display the absolute or relative line number. After the first row fill with blanks
private void
handle_lnum_col(
   Portal* po,
   DrawCtx* m,
   int signPresent,
   Decoration numDeco
) {
   int lnum_row = m->startrow + m->filler_lines + m->textPropAbove_count;
   //If 'signcolumn' is set to 'number' and a sign is present in 'lnum', then display the sign 
   //instead of the line number.
   if (isSigncolumnOn(po) && signPresent && m->signHilites.text)
      get_sign_display_info(true, po, m);
   else {
      // Draw the line number (empty space after wrapping).
      // When there are text properties above the line put the line number below them.
      if (m->row == lnum_row
          && (po->skipCol == 0 || m->row > 0 || po->o.relativeNumber)
      ){
         long num;
         char *fmt = "%*ld ";

         if (!po->o.relativeNumber)
            // 'norelativenumber'
            num = (long)m->lnum;
         else {
            // 'relativenumber', don't use negative numbers
            num = labs((long)get_cursor_rel_lnum(po, m->lnum));
            if (num == 0 && po->o.relativeNumber) {
               // 'relativenumber'
               num = m->lnum;
               fmt = "%-*ld ";
            }
         }

         eeSnprintf(m->extra, sizeof(m->extra), fmt, number_width(po), num);
         if (po->skipCol > 0 && m->startrow == 0) {
            for (m->extraBytes = m->extra; *m->extraBytes == ' '; ++m->extraBytes)
               *m->extraBytes = '-';
         } 
         m->extraBytes = m->extra;
         m->c_extra = ZERO;
         m->c_final = ZERO;
      } else {
         m->c_extra = ' ';
         m->c_final = ZERO;
      }
      m->countExtraBytes = number_width(po) + 1;
      m->charDeco = getFullDecoration(HLF_N);
      // When 'cursorline' is set, hilite the line number of the current line differently.
      // When 'cursorlineopt' does not have "line" only hilite the line number itself.
      // TODO: Can we use CursorLine instead of CursorLineNr when CursorLineNr isn't set?
      if (po->o.cursorLine
              && m->lnum == po->cursor.lnum
              && (m->row == lnum_row || (m->row > lnum_row))
      )
         m->charDeco = getFullDecoration(HLF_CLN);
      if (po->o.relativeNumber 
            && m->lnum < po->cursor.lnum && getDecoFlags(HLF_LNA) != 0
      )
         // Use LineNrAbove
         m->charDeco = getFullDecoration(HLF_LNA);
      if (po->o.relativeNumber 
            && m->lnum > po->cursor.lnum && getDecoFlags(HLF_LNB) != 0
      )
         // Use LineNrBelow
         m->charDeco = getFullDecoration(HLF_LNB);
   }
   if (numDeco.hiId < SHORT)
      m->charDeco = numDeco;
}

private void
breakIndent(Portal* po, DrawCtx* m) {
   if (po->breakIndent.showBreak && m->drawState == WL_BRI - 1 && p_sbr)
      // draw indent after showbreak value
      m->drawState = WL_BRI;
   ei (po->breakIndent.showBreak && m->drawState == WL_SBR)
      // After the showbreak, draw the breakindent
      m->drawState = WL_BRI - 1;

   // draw 'breakindent': indent wrapped text accordingly
   if (m->drawState == WL_BRI - 1) {
      m->drawState = WL_BRI;
      // if m->need_showbreak is set, breakindent also applies
      if (po->o.breakIndent 
            && (m->row > m->startrow + m->filler_lines || m->need_showbreak)
         && !m->dont_use_showbreak
      ){
         m->charDeco = EMPTY_DECO;
         if (m->diff_hlf != 0)
            m->charDeco = getFullDecoration(m->diff_hlf);
         m->extraBytes = NULL;
         m->c_extra = ' ';
         m->c_final = ZERO;
         m->countExtraBytes = getBreakindentForPort(po, memGetLine(po->book, m->lnum, false));
         if (m->row == m->startrow && m->countExtraBytes < 0)
             m->countExtraBytes = 0;

         // Correct start of hilited area for 'breakindent',
         if (m->fromcol >= m->vcol && m->fromcol < m->vcol + m->countExtraBytes)
            m->fromcol = m->vcol + m->countExtraBytes;

         // Correct end of hilited area for 'breakindent'
         if (m->tocol == m->vcol)
            m->tocol += m->countExtraBytes;
      }

      if (po->skipCol > 0 && m->startrow == 0 && po->o.wrap && po->breakIndent.showBreak)
         m->need_showbreak = false;
   }
}

private void
showbreakAndFiller(Portal* po, DrawCtx* m) {
   if (m->filler_todo > 0) {
      // Draw "deleted" diff line(s).
      if (bookChar2Cells(fillCharsG.diff) > 1) {
         m->c_extra = '-';
         m->c_final = ZERO;
      } else {
         m->c_extra = fillCharsG.diff;
         m->c_final = ZERO;
      }
      m->countExtraBytes = po->width - m->col;
      m->charDeco = getFullDecoration(HLF_DED);
   }

   if (p_sbr && m->need_showbreak) {
      // Draw @showbreak at the start of each broken line.
      m->extraBytes = p_sbr;
      m->c_extra = ZERO;
      m->c_final = ZERO;
      m->countExtraBytes = (int)STRLEN(p_sbr);
      m->vcol_sbr = m->vcol + MB_CHARLEN(p_sbr);

      // Correct start of hilited area for @showbreak.
      if (m->fromcol >= m->vcol && m->fromcol < m->vcol_sbr)
          m->fromcol = m->vcol_sbr;

      // Correct end of hilited area for @showbreak
      if (m->tocol == m->vcol)
          m->tocol = m->vcol_sbr;
      m->charDeco = getFullDecoration(HLF_AT);
      // combine @showbreak with @cursorline
      if (m->cursorlineDeco.hiId != SHORT)
         overlayDeco(OUT &m->charDeco, OVERLAY_DECO_ALTERED_BG);
   }

   if (po->skipCol == 0 || m->startrow > 0 || !po->o.wrap || !po->breakIndent.showBreak)
      m->need_showbreak = false;
}

// Return the cell size of virtual text after truncation.
private int
textprop_size_after_trunc(
   Portal* po,
   Unt flags,       // TEXT_PROP_ALIGN_*
   int added,
   int padding,
   CS text,
   OUT int* n_used_ptr
) {
   int   space = (flags & (TEXT_PROP_ALIGN_BELOW | TEXT_PROP_ALIGN_ABOVE))
                   ? (int)po->width - normalPortalColumnOffset(po) : added;
   int strsize = 0;

   // if the remaining size is too small and 'wrap' is set we wrap anyway and use the next line
   if (space < PROP_TEXT_MIN_CELLS && po->o.wrap)
      space += po->width;
   if (flags & (TEXT_PROP_ALIGN_BELOW | TEXT_PROP_ALIGN_ABOVE))
      space -= padding;
      
   CS p;
   for (p = text; *p != ZERO; p += utfCharLen(p)) {
      int clen = bookPtr2Cells(p);

      if (strsize + clen > space)
         break;
      strsize += clen;
   }
   *n_used_ptr = (int)(p - text);

   return strsize;
}

//Take care of padding, right-align and truncation of virtual text after a line. if "numDecoCells" 
//is not NULL then "countExtraBytes" and "extraBytes" are adjusted for any padding, right-align and 
//truncation. Otherwise only the size is computed. When "numDecoCells" is NULL returns the number 
//of screen cells used. Otherwise returns true when drawing continues on the next line.
pub int
text_prop_position(
   Portal* po,
   TextProp* t,
   int vcol,       // current text column
   int scr_col,       // current screen column
   int* countExtraBytes,       // nr of bytes for virtual text
   Byte** extraBytes,       // virtual text
   OUT int* numDecoCells,       // decoration cells, NULL if not used
   int* toSkipBeforeDeco,   // cells to skip deco, NULL if not used
   int do_skip       // skip_cells is not zero
){
   int right = (t->flags & TEXT_PROP_ALIGN_RIGHT);
   int above = (t->flags & TEXT_PROP_ALIGN_ABOVE);
   int below = (t->flags & TEXT_PROP_ALIGN_BELOW);
   int wrap = t->col < MAXCOL || (t->flags & TEXT_PROP_WRAP);
   int padding = t->col == MAXCOL && t->len > 1 ? t->len - 1 : 0;
   int col_with_padding = scr_col + (below ? 0 : padding);
   int room = po->width - col_with_padding;
   int before = room;   // spaces before the text
   int after = 0;      // spaces after the text
   int n_used = *countExtraBytes;
   CS l = NULL;
   int strsize = eeglStrSize(*extraBytes);
   int cells = wrap 
      ? strsize 
      : textprop_size_after_trunc(po, t->flags, before, padding, *extraBytes, OUT &n_used);

   if (wrap || right || above || below || padding > 0 || n_used < *countExtraBytes) {
      int    col_off = normalPortalColumnOffset(po);

      if (above) {
         before = 0;
         after = po->width - cells - normalPortalColumnOffset(po) - padding;
         if (after < 0) {
            // text "above" has too much padding to fit
            padding += after;
            after = 0;
         }
      } else {
         // Right-align: fill with before
         if (right)
            before -= cells;

         // Below-align: empty line add one character
         if (below && vcol == 0 && col_with_padding == col_off && (int)po->width - col_off == before)
            col_with_padding += 1;

         if (before < 0
             || !(right || below)
             || (below ? (col_with_padding <= col_off || !po->o.wrap) : (n_used < *countExtraBytes))
         ) {
            if (right && (wrap || (room < PROP_TEXT_MIN_CELLS && po->o.wrap))) {
               // right-align on next line instead of wrapping if possible
               before = po->width - col_off - strsize + room;
               if (before < 0)
                  before = 0;
               else
                  n_used = *countExtraBytes;
            } ei (below && before > vcol && do_skip)
               before -= vcol;
            else
               before = 0;
         }
      }

      // With 'nowrap' add one to show the "extends" character if needed (it doesn't show if the 
      // text just fits).
      if (!po->o.wrap
            && n_used < *countExtraBytes
            && listCharsG.ext != ZERO
            && po->o.list)
         ++n_used;

      // add 1 for ZERO, 2 for when '…' is used
      if (numDecoCells)
         l = alloc(n_used + before + after + (padding > 0 ? padding : 0) + 3);
      if (!numDecoCells || l) {
         int off = 0;

         if (numDecoCells) {
            memset(l, ' ', before);
            off += before;
            if (padding > 0) {
               memset(l + off, ' ', padding);
               off += padding;
            }
            copySubstrToAllocation(l + off, (Text){*extraBytes, n_used});
            off += n_used;
         } else {
            off = before + after + padding + n_used;
            cells += before + after + padding;
         }
         if (numDecoCells) {
            if (n_used < *countExtraBytes && po->o.wrap) {
               CS lp = l + off - 1;

               Byte buf[MB_MAXBYTES + 1];
               CS cp = buf;

               // change the last character to '…', converted to the current 'encoding'
               STRCPY(buf, "…");

               lp -= mb_ptr2cells(cp) - 1;
               lp -= mb_head_off(l, lp);
               STRCPY(lp, cp);
               n_used = lp - l + 3 - before - padding;
               if (cp != buf)
                  eeglFree(cp);
            } ei (after > 0) {
               memset(l + off, ' ', after);
               l[off + after] = ZERO;
            }

            *extraBytes = l;
            *countExtraBytes = n_used + before + after + padding;
            *numDecoCells = mb_charlen(*extraBytes);
            // toSkipBeforeDeco will not be decremented before drawState is WL_LINE
            *toSkipBeforeDeco = before + (padding > 0 ? padding : 0);
            *numDecoCells -= *toSkipBeforeDeco;
            if (above)
               *numDecoCells -= after;
         }
      }
   }

   if (!numDecoCells)
      return cells;
   return (below && col_with_padding > normalPortalColumnOffset(po) && !po->o.wrap);
}

// Call screen_line() using values from "m". Also takes care of putting "<<<" on the first line 
// for @smoothscroll when @showbreak is not set. When "clear_end" is true clear until the end of 
// the screen line.
private void
wlv_screen_line(Portal* po, DrawCtx* m, int clear_end) {
   if (m->row == 0 && po->skipCol > 0
       // do not overwrite the @showbreak text with "<<<"
       && !p_sbr
       // do not overwrite the @listchars "precedes" text with "<<<"
       && !(po->o.list && listCharsG.prec != 0)
   ) {
      int off = (int)(currScreenLineS - screenTextP);
      int skip = 0;

      if (po->o.relativeNumber) {
         // Do not overwrite the line number, change "123 text" to "123<<<xt"
         while (skip < (int)po->width && EE_ISDIGIT(screenTextP[off])) {
            ++off;
            ++skip;
         }
      } 

      for (int i = 0; i < 3 && i + skip < (int)po->width; ++i) {
         screenTextP[off] = '<';
         screenLinesUCG[off] = 0;
         screenDecosP[off].flags = getFullDecoration(HLF_AT).flags;
         ++off;
      }
   }

   screen_line(
      m->screen_row, po->windowCol, m->col, clear_end ? po->width : -po->width,
      m->vcol - 1, m->screen_line_flags
   );
}

// Called when finished with the line: draw the screen line and handle any hiliting until the 
// right of the portal.
private void
finalizeDrawingLineOnScreen(Portal* po, DrawCtx* m) {
   long v = (po->o.wrap) ? (m->startrow == 0 ? po->skipCol : 0) : po->leftCol;
   int wcol = m->col;
   // check if line ends before left margin
   if (m->vcol < v + wcol - normalPortalColumnOffset(po))
      m->vcol = v + wcol - normalPortalColumnOffset(po);
#  define VCOL_HLC (m->vcol - m->virtualOffset)

   if (m->lineDeco.hiId != SHORT || m->portalDeco.flags != 0) {
      int rightmost_vcol = 0;

      while ((m->col < (int)po->width)) {
         screenTextP[m->off] = ' ';
         screenLinesUCG[m->off] = 0;

         Decoration deco = (m->lineDeco.hiId != SHORT) ? m->portalDeco : m->lineDeco;
         screenDecosP[m->off].flags = deco.flags;
         screenColS[m->off] = m->vcol;
         ++m->off;
         ++m->col;
         ++m->vcol;

         if (VCOL_HLC > rightmost_vcol && m->lineDeco.flags == 0 && m->portalDeco.flags == 0)
            break;
      }
   }

   // Set increasing virtual columns in screenColS[] to set correct curswant
   // (or "coladd" for 'virtualedit') when clicking after end of line.
   m->screen_line_flags |= SLF_INC_VCOL;
   wlv_screen_line(po, m, true);
   m->screen_line_flags &= ~SLF_INC_VCOL;
   ++m->row;
   ++m->screen_row;
}
#undef VCOL_HLC

//Start a screen line at column zero.
//When "save_extra" is true save and reset countExtraBytes, extraBytes, etc.
private void
drawLineOnScreen_start(OUT DrawCtx* m, int save_extra) {
   m->col = 0;
   m->off = (Unt)(currScreenLineS - screenTextP);
   m->needLinebreak = false;

   if (save_extra) {
      // reset the drawing state for the start of a wrapped line
      m->drawState = WL_START;
      m->saved_n_extra = m->countExtraBytes;
      m->saved_p_extra = m->extraBytes;
      eeglFree(m->saved_p_extra_free);
      m->saved_p_extra_free = m->p_extra_free;
      m->p_extra_free = NULL;
      m->saved_extraDeco = m->extraDeco;
      m->saved_toSkipBeforeDeco = m->toSkipBeforeDeco;
      m->saved_extra_for_textprop = m->extra_for_textprop;
      m->saved_c_extra = m->c_extra;
      m->saved_c_final = m->c_final;
      m->needLinebreak = true;
      if (!(m->cul_screenline && m->diff_hlf == 0))
         m->saved_charDeco = m->charDeco;
      else
         m->saved_charDeco = EMPTY_DECO;

      // these are not used until restored in drawLineOnScreen_continue()
      m->countExtraBytes = 0;
      m->toSkipBeforeDeco = 0;
   }
}

// Called when m->drawState is set to WL_LINE.
private void
drawLineOnScreen_continue(DrawCtx* m) {
   if (m->saved_n_extra > 0) {
      // Continue item from end of wrapped line.
      m->countExtraBytes = m->saved_n_extra;
      m->saved_n_extra = 0;
      m->c_extra = m->saved_c_extra;
      m->c_final = m->saved_c_final;
      m->extraBytes = m->saved_p_extra;
      eeglFree(m->p_extra_free);
      m->p_extra_free = m->saved_p_extra_free;
      m->saved_p_extra_free = NULL;
      m->extraDeco = m->saved_extraDeco;
      m->toSkipBeforeDeco = m->saved_toSkipBeforeDeco;
      m->extra_for_textprop = m->saved_extra_for_textprop;
      m->charDeco = m->saved_charDeco;
   } else
      m->charDeco = m->portalDeco;
}

private void
applyCursorlineHilite(DrawCtx* m) {
   overlayDeco(OUT &m->lineDeco, OVERLAY_DECO_ALTERED_BG);
}


#define VCOL_HLC (m->vcol - m->virtualOffset)

private typedef struct {
   Decoration lineDecoSaved; 
   Boole signPresent; 
   LineNr lnum;
   Boole inCurLine; 
   int lastTextpropTextInd; 
   Boole isLineVisible;
   Decoration numDeco;
   int drawingOnlyNumberCol; 
   int left_curline_col;
   int right_curline_col;
   Boole areaHiliting;
   Boole hasExtraHiliting;
   Arr(TextProp) textProps;
   Arr(int) textPropIndices;
   int textPropCount;
   int fromcol_prev; // start of inverting after cursor
   Decoration visualDeco;
   Boole noInvertCursor;
   DiffLine* lineChanges;
   int* changeStart;
   int* changeEnd;
   Boole needDecoFromTerm;
   int currCheckedCol;
   int nextLineCol;
   Arr(Byte) nextLine; //len = (SPWORDLEN * 2);
   int nextLineInd;
   int vcolFirstChar;
   ColNr trailcol;   // start of trailing spaces
   ColNr leadcol;      // start of leading spaces
} Subcontext;

private typedef struct {
   Boole decoPriority;
   int mb_c; 
   Boole mb_utf8; 
   Arr(int) characterCombiner; //len = MAX_COMBINED_SYMBOLS

   Unt listCharEndOfLine;
   Decoration areaDeco; 
   Decoration charDecoSaved;
   Boole textPropFlags; 
   Boole textPropFollows;
   int numDecoCells;
   int textPropAbove;
   int didLineDeco;
   Boole resetOverlayDeco;
   long vcol_prev;
   Decoration multiDeco;
   
   int skippedCells;  // nr of skipped cells for virtual text to be added to m.vcol later
} SubSubcontext;

//Return false if need to break from the loop in drawLineLoop
private Boole
drawLineSub(DrawCtx* m, Portal* port, Subcontext* c, SubSubcontext* sc, int currSymb) {
   int charsWithOverrulingUnder = 0;       // chars with overruling special deco
   Decoration charDecoSavedForOverruling;
   
   //Use "m->extraDeco", but don't override visual selection hiliting, unless text property 
   //overrides. Don't use "m->extraDeco" until m->toSkipBeforeDeco is 0.
   if (m->toSkipBeforeDeco == 0 && sc->numDecoCells > 0
      && m->drawState == WL_LINE
      && (!sc->decoPriority || (sc->textPropFlags & PT_FLAG_OVERRIDE) != 0)
   ){
      m->charDeco = m->extraDeco;
      if (sc->resetOverlayDeco) {
         sc->resetOverlayDeco = false;
         m->extraDeco = EMPTY_DECO;
      }
   }
   
   Unt lcs_prec_todo = listCharsG.prec; // prec until it's been used

   // Handle the case where we are in column 0 but not on the first
   // character of the line and the user wants us to show us a
   // special character (via @listchars "precedes:<char>").
   if (lcs_prec_todo != ZERO
      && port->o.list
      && (port->o.wrap ? (port->skipCol > 0 && m->row == 0) : port->leftCol > 0)
      && m->filler_todo <= 0
      && m->drawState > WL_NR
      && m->cellsToSkip <= 0
      && currSymb != ZERO
   ){
      currSymb = listCharsG.prec;
      lcs_prec_todo = ZERO;
      if (mb_char2cells(sc->mb_c) > 1)
         //Double-width character being overwritten by the "precedes"
         //character, need to fill up half the character.
         m->c_extra = MB_FILLER_CHAR;
      m->c_final = ZERO;
      m->countExtraBytes = 1;
      sc->numDecoCells = 2;
      m->extraDeco = getFullDecoration(HLF_AT); 
      sc->mb_c = currSymb; 
      if (mb_char2len(currSymb) > 1) {
         sc->mb_utf8 = true;
         sc->characterCombiner[0] = 0;
         currSymb = 0xc0;
      } else
         sc->mb_utf8 = false;   // don't draw as UTF-8
      if (!sc->decoPriority) {
         charDecoSavedForOverruling = m->charDeco; // save current deco
         m->charDeco = getFullDecoration(HLF_AT);
         charsWithOverrulingUnder = 1;
      }
   }

   // At end of the text line or just after the last character.
   if ((currSymb == ZERO || sc->didLineDeco == 1) && m->eol_hl_off == 0) {
      // flag to indicate whether prevcol equals startcol of search_hl or one of the matches
      int prevcol_hl_flag = get_prevcol_hl_flag(
             port, &screenSearchP, (long)(m->ptr - m->line) - (currSymb == ZERO)
      );
      // Invert at least one char, used for Visual and empty line or hilite match at end of 
      // line. If it's beyond the last char on the screen, just overwrite that one (tricky!)  Not
      // needed when a '$' was displayed for 'list'.
      if (listCharsG.eol == sc->listCharEndOfLine
          && ((sc->areaDeco.hiId != SHORT && m->vcol == m->fromcol
                   && (VIsual_mode != Ctrl_V
                        || c->lnum == VIsual.lnum
                        || c->lnum == curPor->cursor.lnum)
                   && currSymb == ZERO)
               // hilite 'hlsearch' match at end of line
               || (prevcol_hl_flag
                   && !(port->o.cursorLine && c->lnum == port->cursor.lnum
                      && !(port == curPor && VIsual_active))
                   && m->diff_hlf == 0
                   && sc->didLineDeco <= 1
                  )
            )
      ) {
         int n = 0;

         if (m->col >= (int)port->width)
            n = -1;
         if (n != 0) {
            // At the portal boundary, hilite the last character
            // instead (better than nothing).
            m->off += n;
            m->col += n;
         } else {
            // Add a blank character to hilite.
            screenTextP[m->off] = ' ';
            screenLinesUCG[m->off] = 0;
         }
         if (sc->areaDeco.hiId == SHORT) {
            // Use decos from the match with highest priority among 'search_hl' and the match list
            Short charHiId;
            get_search_match_hl(port, &screenSearchP, (long)(m->ptr - m->line), &charHiId);
            m->charDeco = getFullDecoration(charHiId);
         }
         screenDecosP[m->off] = m->charDeco;
         screenColS[m->off] = m->vcol;
         ++m->col;
         ++m->off;
         ++m->vcol;
         m->eol_hl_off = 1;
      }
   }

   // At end of the text line.
   if (currSymb == ZERO) {
      if (sc->textPropFollows) {
         // Put the pointer back to the ZERO.
         m->ptr--;
         currSymb = ' ';
      } else {
         finalizeDrawingLineOnScreen(port, m);

         // Update cursorLineHeight and isCursorLineFolded if the cursor line
         // was updated (saves a call to plines() later).
         if (c->inCurLine) {
            curPor->cursorLineRow = m->startrow;
            curPor->cursorLineHeight = m->row - m->startrow;
            curPor->isCursorLineFolded = false;
            curPor->cacheState |= (VALID_CHEIGHT|VALID_CROW);
         }
         return false;
      }
   }

   // Show "extends" character from @listchars if beyond the line end and 'list' is set.
   if (listCharsG.ext != ZERO
      && m->drawState == WL_LINE
      && port->o.list
      && !port->o.wrap
      && m->filler_todo <= 0
      && ( m->col == (int)port->width - 1)
      && (*m->ptr != ZERO
          || sc->listCharEndOfLine != UNT
          || (m->countExtraBytes > 0 && (m->c_extra != ZERO || *m->extraBytes != ZERO))
          || m->textPropNext <= c->lastTextpropTextInd
         )
   ){
      currSymb = listCharsG.ext;
      m->charDeco = getFullDecoration(HLF_AT);
      sc->mb_c = currSymb;
      if (mb_char2len(currSymb) > 1) {
         sc->mb_utf8 = true;
         sc->characterCombiner[0] = 0;
         currSymb = 0xc0;
      } else
         sc->mb_utf8 = false;
   }

   Decoration vcolDecoSaved = EMPTY_DECO;

   if (m->drawState == WL_LINE)
      sc->vcol_prev = m->vcol;

   //Store character to be displayed. Skip characters that are left of the screen for 'nowrap'.
   if (m->drawState < WL_LINE || m->cellsToSkip <= 0) {
      //Store the character.
      screenTextP[m->off] = currSymb;
      if (sc->mb_utf8) {
         screenLinesUCG[m->off] = sc->mb_c;
         if ((currSymb & 0xff) == 0)
            screenTextP[m->off] = 0x80;   // avoid storing zero
         for (Unt i = 0; i < MAX_COMBINED_SYMBOLS; ++i) {
            screenLinesCG[MAX_COMBINED_SYMBOLS * m->off + i] = sc->characterCombiner[i];
            if (sc->characterCombiner[i] == 0)
               return false;
         }
      } else
         screenLinesUCG[m->off] = 0;
      if (sc->multiDeco.hiId < SHORT) {
         screenDecosP[m->off].flags = sc->multiDeco.flags;
         sc->multiDeco = EMPTY_DECO;
      } else
         screenDecosP[m->off].flags = m->charDeco.flags;

      if (m->drawState > WL_NR && m->filler_todo <= 0)
         screenColS[m->off] = m->vcol;
      else
         screenColS[m->off] = -1;

      if (mb_char2cells(sc->mb_c) > 1) {
         //Need to fill two screen columns.
         ++m->off;
         ++m->col;
         //UTF-8: Put a 0 in the second screen char.
         screenTextP[m->off] = 0;

         if (m->drawState > WL_NR && m->filler_todo <= 0)
            screenColS[m->off] = ++m->vcol;
         else
            screenColS[m->off] = -1;

         //When "m->tocol" is halfway a character, set it to the end
         //of the character, otherwise hiliting won't stop.
         if (m->tocol == m->vcol)
            ++m->tocol;

      }
      ++m->off;
      ++m->col;
   } else
      m->cellsToSkip--;

   if (m->drawState > WL_NR && sc->skippedCells > 0) {
      m->vcol += sc->skippedCells;
      sc->skippedCells = 0;
   }

   // Only advance the "m->vcol" when after the 'number' or 'relativenumber' column.
   if (m->drawState > WL_NR && m->filler_todo <= 0)
      ++m->vcol;

   if (vcolDecoSaved.hiId < SHORT)
      m->charDeco = vcolDecoSaved;

   // restore decorations after "precedes" in @listchars
   if (m->drawState > WL_NR && charsWithOverrulingUnder == 1)
      m->charDeco = charDecoSavedForOverruling;
   charsWithOverrulingUnder--; 

   // restore decorations after last @listchars or 'number' char
   if (sc->numDecoCells > 0 && m->drawState == WL_LINE && m->toSkipBeforeDeco == 0 
         && --(sc->numDecoCells) == 0
   )
      m->charDeco = sc->charDecoSaved;
   if (m->toSkipBeforeDeco > 0)
      --m->toSkipBeforeDeco;

   // At end of screen line and there is more to come: Display the line
   // so far.  If there is no more to display it is caught above.
   if ((m->col >= (int)port->width)
         && (m->drawState != WL_LINE
          || *m->ptr != ZERO
          || m->filler_todo > 0
          || sc->textPropAbove || sc->textPropFollows
          || m->textPropNext <= c->lastTextpropTextInd
          || (port->o.list && listCharsG.eol != ZERO && sc->listCharEndOfLine != UNT)
          || (m->countExtraBytes != 0 && (m->c_extra != ZERO || *m->extraBytes != ZERO)))
   ){
      wlv_screen_line(port, m, true);
      ++m->row;
      ++m->screen_row;

      // When not wrapping and finished diff lines, break here.
      if (!port->o.wrap
            && m->filler_todo <= 0
            && !sc->textPropAbove
            && !sc->textPropFollows
      )
         return false;
      if (!port->o.wrap && sc->textPropFollows && !sc->textPropAbove) {
         // do not output more of the line, only the "below" prop
         m->ptr = m->line + (Unt)memGetBookLen(port->book, c->lnum);
         m->dont_use_showbreak = true;
      }

      //When the portal is too narrow, draw all "@" lines.
      if (m->drawState != WL_LINE && m->filler_todo <= 0) {
         drawVoidAtPortalEnd(port, '@', ' ', true, m->row, port->height, HLF_AT);
         drawVerticalSeparator(port, m->row);
         m->row = m->endRow;
      }

      // When line got too long for screen break here.
      if (m->row == m->endRow) {
         ++m->row;
         return false;
      }

      if (screenCursRowG == m->screen_row - 1
           && m->filler_todo <= 0
           && !sc->textPropAbove && !sc->textPropFollows
           && port->width == visibleColsG
      ) {
         // Remember that the line wraps, used for modeless copy.
         lineWrapsP[m->screen_row - 1] = true;

         //Special trick to make copy/paste of wrapped lines work with xterm/screen: write an 
         //extra character beyond the end of the line. This will work with all terminal types
         //(regardless of the xn,am settings).
         //Only do this if the cursor is on the current line (something has been written in it).
         //Don't do this for a portal not at the right screen border.
         //First make sure we are at the end of the screen line, then output the same 
         //character again to let the terminal know about the wrap.  If the terminal doesn't
         //auto-wrap, we overwrite the character.
         if (screenCursColG != (int)port->width) {
            singleChar(
               lineStartsP[m->screen_row - 1] + (Unt)topframeG->width - 1,
               m->screen_row - 1, 
               (int)(topframeG->width - 1)
            );
         } 

         // When there is a multi-byte character, just output a space to keep it simple.
         if (utf8CharLens[screenTextP[lineStartsP[m->screen_row - 1]
                  + (topframeG->width - 1)]] > 1
         )
            out_char(' ');
         else
            out_char(screenTextP[lineStartsP[m->screen_row - 1] + (topframeG->width - 1)]);
            
         // force a redraw of the first char on the next line
         screenDecosP[lineStartsP[m->screen_row]].hiId = 0;
         screen_start();   // don't know where cursor is now
      }

      drawLineOnScreen_start(m, true);

      lcs_prec_todo = listCharsG.prec;
      if (!m->dont_use_showbreak && m->filler_todo <= 0)
         m->need_showbreak = true;
      --m->filler_todo;
      //When the filler lines are actually below the last line of the
      //file, don't draw the line itself, break here.
      if (m->filler_todo == 0 && port->bottFill)
         return false;
   }
   return true; 
}

#define SPWORDLEN 150

// Main loop for drawing a line of text on screen
private void
drawLineLoop(DrawCtx* m, Subcontext* c, Portal* port) {
   SubSubcontext sc;
   sc.mb_c = 0;      // decoded multi-byte character
   sc.textPropFlags = 0;
   sc.decoPriority = false;   // charDeco has priority
   sc.resetOverlayDeco = false;
   int characterCombiner[MAX_COMBINED_SYMBOLS];      // composing UTF-8 chars
   sc.characterCombiner = (Arr(int))characterCombiner;
   
   sc.listCharEndOfLine = listCharsG.eol; // eol until it's been used
   sc.areaDeco = EMPTY_DECO;      // decorations desired by hiliting
   Decoration areaDecoSaved;   // idem for areaDeco
   sc.textPropFollows = false;  // another text prop to display
   sc.vcol_prev = -1;      // "m.vcol" of previous character
   sc.skippedCells = 0; 
               
   sc.textPropAbove = false;  // first doing virtual text above
   
   
   sc.mb_utf8 = false;   // screen char is UTF-8 char
   sc.multiDeco = EMPTY_DECO;      // decorations desired by multibyte
   
   
   Boole inLineBreak = false;   // countExtraBytes set for showing linebreak
   int countActiveTextProps = 0;
   Decoration syntaxDeco = EMPTY_DECO;   // decorations desired by syntax
   Decoration textPropDeco_comb = EMPTY_DECO;  // textPropDeco combined with syntaxDeco
   PropType* text_prop_type = NULL;
   Decoration textPropDeco = EMPTY_DECO;
   int text_prop_id = 0;   // active property ID
   Boole didLine = false;   // set to true when line text done
   sc.numDecoCells = 0;
   Short searchDecoSaved = SHORT;   // searchHiId to be used when countExtraBytes goes to zero
   sc.didLineDeco = 0;
   Boole onLastCol = false;
   int prevSyntaxCol = -1;   // column of prevCharDeco
   Decoration prevCharDeco = EMPTY_DECO;   // syntaxDeco at prevSyntaxCol
   int multibLength = 1;      // multi-byte byte length
   
   // Repeat for the whole displayed line.
   for (;;) {
      // Skip this quickly when working on the text.
      if (m->drawState != WL_LINE) {
         if (m->cul_screenline) {
            m->cursorlineDeco = EMPTY_DECO;
            m->lineDeco = c->lineDecoSaved;
         }
         if (m->drawState == WL_COMMLINE - 1 && m->countExtraBytes == 0) {
            m->drawState = WL_COMMLINE;
            if (port == commPortPortG) {
                // Draw the cmdline character.
                m->countExtraBytes = 1;
                m->c_extra = commPortTypeG;
                m->c_final = ZERO;
                m->charDeco = getFullDecoration(HLF_AT);
            }
         }
         if (m->drawState == WL_SIGN - 1 && m->countExtraBytes == 0) {
            // Show the sign column when desired.
            m->drawState = WL_SIGN;
            if (isSigncolumnOn(port))
               get_sign_display_info(false, port, m);
         }
         if (m->drawState == WL_NR - 1 && m->countExtraBytes == 0) {
            // Show the line number, if desired.
            m->drawState = WL_NR;
            handle_lnum_col(port, m, c->signPresent, c->numDeco);
         }

         // When only displaying the (relative) line number and that's done, stop here.
         if (c->drawingOnlyNumberCol > 0 && m->drawState == WL_NR && m->countExtraBytes == 0) {
            wlv_screen_line(port, m, false);
            // Need to update more screen lines if:
            // - LineNrAbove or LineNrBelow is used, or
            // - still drawing filler lines.
            if ((m->row + 1 - m->startrow < c->drawingOnlyNumberCol
               && (getFullDecoration(HLF_LNA).flags != 0 || getFullDecoration(HLF_LNB).flags != 0))
               || m->filler_todo > 0
            ) {
               ++m->row;
               ++m->screen_row;
               if (m->row == m->endRow)
                  break;
               --m->filler_todo;
               if (m->filler_todo == 0 && port->bottFill)
                  break;
               drawLineOnScreen_start(m, true);
               continue;
            } else
                break;
          }

         // Check if 'breakindent' applies and show it.
         // May change m.drawState to WL_BRI or WL_BRI - 1.
         if (m->countExtraBytes == 0)
            breakIndent(port, m);
         if (m->drawState == WL_SBR - 1 && m->countExtraBytes == 0) {
            m->drawState = WL_SBR;
            showbreakAndFiller(port, m);
         }
         if (m->drawState == WL_LINE - 1 && m->countExtraBytes == 0) {
            m->drawState = WL_LINE;
            drawLineOnScreen_continue(m);  // use m.saved_ values
         }
      }

      if (m->cul_screenline && m->drawState == WL_LINE
         && m->vcol >= c->left_curline_col
         && m->vcol < c->right_curline_col
      )
         applyCursorlineHilite(m);


      if (m->drawState == WL_LINE && (c->areaHiliting || c->hasExtraHiliting)) {
         if (c->textProps) {
            int pi;
            int bcol = (int)(m->ptr - m->line);

            if (m->countExtraBytes > 0 && !inLineBreak)
               --bcol;  // still working on the previous char, e.g. Tab

            // Check if any active property ends.
            for (pi = 0; pi < countActiveTextProps; ++pi) {
               int tpi = c->textPropIndices[pi];
               TextProp* t = c->textProps + tpi;

               // An inline property ends when after the start column plus
               // length. An "above" property ends when used and countExtraBytes is zero.
               if ((t->col != MAXCOL && bcol >= t->col - 1 + t->len)) {
                  if (pi + 1 < countActiveTextProps)
                     MEMMOVE(c->textPropIndices + pi,
                        c->textPropIndices + pi + 1,
                        sizeof(int) * (countActiveTextProps - (pi + 1))
                     );
                  --countActiveTextProps;
                  --pi;
                  // not exactly right but should work in most cases
                  if (inLineBreak && syntaxDeco.hiId == textPropDeco_comb.hiId)
                     syntaxDeco = EMPTY_DECO;
               }
            }

            if (m->countExtraBytes > 0 && inLineBreak)
               // not on the next char yet, don't start another prop
               --bcol;
            // Add any text property that starts in this column.
            while (m->textPropNext < c->textPropCount) {
               int active;
               TextProp *t = c->textProps + m->textPropNext;
               if (t->col == MAXCOL) {
                  if (bcol == 0 && (t->flags & TEXT_PROP_ALIGN_ABOVE))
                     active = true;
                  ei (*m->ptr != ZERO)
                     break;
                  else {
                     // With 'nowrap' and not in the first screen line only "below" text prop can show
                     active = port->o.wrap 
                        || m->row == m->startrow 
                        || (t->flags & TEXT_PROP_ALIGN_BELOW);
                  }
               } else {
                  if (bcol < t->col - 1)
                     break;
                  active = bcol <= t->col - 1 + t->len;
               }

               if (active) {
                  c->textPropIndices[countActiveTextProps] = m->textPropNext;
                  countActiveTextProps++;
               } 
               m->textPropNext++;
            }

            if (m->countExtraBytes == 0
               || (!m->extra_for_textprop 
                   && !(text_prop_type && sc.textPropFlags & PT_FLAG_OVERRIDE))
            ){
               textPropDeco = EMPTY_DECO;
               textPropDeco_comb = EMPTY_DECO;
               sc.textPropFlags = 0;
               text_prop_type = NULL;
               text_prop_id = 0;
               sc.resetOverlayDeco = false;
            }
            if (countActiveTextProps > 0 && m->countExtraBytes == 0) {
               int used_tpi = -1;
               Decoration usedDeco = EMPTY_DECO;
               int other_tpi = -1;

               sc.textPropAbove = false;
               sc.textPropFollows = false;

               //Sort the properties on priority and/or starting last.
               //Then combine the decorations, highest priority last.
               sort_text_props(port->book, c->textProps, c->textPropIndices, countActiveTextProps);

               for (pi = 0; pi < countActiveTextProps; ++pi) {
                  int tpi = c->textPropIndices[pi];
                  TextProp* t = c->textProps + tpi;
                  PropType* pt = text_prop_type_by_id( port->book, t->type);

                  // Only use a text property that can be displayed. Skip "after" properties when 
                  // wrap is off and at the end of the portal.
                  if (pt
                     && (pt->hilite > 0 || t->id < 0)
                     && t->id != -MAXCOL
                     && !(t->id < 0
                         && !port->o.wrap
                         && (t->flags 
                               & (TEXT_PROP_ALIGN_RIGHT | TEXT_PROP_ALIGN_ABOVE | TEXT_PROP_ALIGN_BELOW)
                            ) == 0
                         && m->col >= (int)port->width)
                  ){
                     if (t->col == MAXCOL
                          && *m->ptr == ZERO
                          && ((port->o.list && sc.listCharEndOfLine != UNT
                                && (t->flags & TEXT_PROP_ALIGN_ABOVE) == 0)
                                || (m->ptr == m->line
                                       && !didLine
                                       && (t->flags & TEXT_PROP_ALIGN_BELOW))
                             )
                     ) {
                        //skip this prop, first display the '$' after
                        //the line or display an empty line
                        sc.textPropFollows = true;
                        continue;
                     }

                     if (pt->hilite > 0)
                        textPropDeco = getFullDecoration(pt->hilite);
                     text_prop_type = pt;
                     if (used_tpi >= 0 && c->textProps[used_tpi].id < 0)
                        other_tpi = used_tpi;
                     sc.textPropFlags = pt->flags;
                     text_prop_id = t->id;
                     used_tpi = tpi;
                  }
               }
               if (text_prop_id < 0 && used_tpi >= 0
                   && -text_prop_id <= port->book->textPropText.len
               ){
                  TextProp* t = c->textProps + used_tpi;
                  Byte* p = ((Byte **)port->book ->textPropText.c)[ -text_prop_id - 1];
                  int above = (t->flags & TEXT_PROP_ALIGN_ABOVE);
                  int bail_out = false;

                  // reset the ID in the copy to avoid it being used again
                  t->id = -MAXCOL;

                  if (p) {
                     int right = (t->flags & TEXT_PROP_ALIGN_RIGHT);
                     int below = (t->flags & TEXT_PROP_ALIGN_BELOW);
                     int wrap = t->col < MAXCOL || (t->flags & TEXT_PROP_WRAP);
                     int padding = t->col == MAXCOL && t->len > 1 ? t->len - 1 : 0;

                     // Insert virtual text before the current char, or add after the line end
                     m->extraBytes = p;
                     m->c_extra = ZERO;
                     m->c_final = ZERO;
                     m->countExtraBytes = (int)STRLEN(p);
                     m->extra_for_textprop = true;
                     m->start_extra_for_textprop = true;
                     m->extraDeco = usedDeco;
                     sc.numDecoCells = mb_charlen(p);
                     textPropDeco = EMPTY_DECO;
                     textPropDeco_comb = EMPTY_DECO;
                     if (*m->ptr == ZERO)
                        //don't combine char deco after EOL
                        sc.textPropFlags &= ~PT_FLAG_COMBINE;
                     if (above || below || right || !wrap) {
                        //no @showbreak before "below" text property or after "above" or "right" 
                        // text property
                        m->need_showbreak = false;
                        m->dont_use_showbreak = true;
                     }
                     if ((right || above || below || !wrap || padding > 0) && port->width > 2) {

                        //Take care of padding, right-align and truncation.
                        //Shared with win_lbr_chartabsize(), must do exactly the same.
                        int start_line = text_prop_position(
                           port, t, m->vcol, m->col, &(m->countExtraBytes), &(m->extraBytes),
                           OUT &sc.numDecoCells, &(m->toSkipBeforeDeco), m->cellsToSkip > 0
                        );

                        if (above)
                           m->virtualOffset += eeglStrSize(m->extraBytes);

                        if (sc.listCharEndOfLine == UNT
                              && port->o.wrap
                              && m->col + m->countExtraBytes - 2 > (int)port->width)
                           // don't bail out at end of line
                           sc.textPropFollows = true;

                        //When @wrap is off, for "below" we need to start a new line explicitly
                        if (start_line) {
                           finalizeDrawingLineOnScreen(port, m);

                           // When line got too long for screen break here.
                           if (m->row == m->endRow) {
                              m->row++;
                              break;
                           }
                           drawLineOnScreen_start(m, true);
                           bail_out = true;
                        }
                     }
                  }

                  //If the text didn't reach until the first portal column we need to skip cells.
                  if (m->cellsToSkip > 0) {
                     if (m->countExtraBytes > m->cellsToSkip) {
                        m->countExtraBytes -= m->cellsToSkip;
                        m->extraBytes += m->cellsToSkip;
                        m->toSkipBeforeDeco -= m->cellsToSkip;
                        if (m->toSkipBeforeDeco < 0)
                           m->toSkipBeforeDeco = 0;
                        sc.skippedCells += m->cellsToSkip;
                        m->cellsToSkip = 0;
                     } else {
                        //the whole text is left of the portal, drop it and advance to the next one
                        m->cellsToSkip -= m->countExtraBytes;
                        sc.skippedCells += m->countExtraBytes;
                        m->countExtraBytes = 0;
                        m->toSkipBeforeDeco = 0;
                        bail_out = true;
                     }
                  }

                  //If another text prop follows the condition below at the last portal column 
                  //must know.
                  //If this is an "above" text prop and @wrap is off, then we must wrap anyway
                  sc.textPropAbove = above;
                  sc.textPropFollows = sc.textPropFollows 
                     || (other_tpi != -1
                        && (port->o.wrap
                           || (c->textProps[other_tpi].flags
                               & (TEXT_PROP_ALIGN_BELOW | TEXT_PROP_ALIGN_RIGHT)))
                  );

                  if (bail_out)
                     // starting a new line for "below"
                     continue;
                }
            } ei (m->textPropNext < c->textPropCount
                  && ((*m->ptr != ZERO && m->ptr[utfCharLen(m->ptr)] == ZERO)
                      || (!port->o.wrap && m->col == (int)port->width - 1))
            ){
               //When at last-but-one character and a text property follows after it, we may 
               //need to flush the line after displaying that character.
               //Or when not wrapping and at the rightmost column.

               int only_below_follows = !port->o.wrap && m->col == (int)port->width - 1;
               //TODO: Store "after"/"right"/"below" text properties in order
               //      in the buffer so only `textProps[textPropCount - 1]`
               //      needs to be checked for following "below" virtual text
               for (int i = m->textPropNext; i < c->textPropCount; ++i) {
                  if (c->textProps[i].col == MAXCOL
                     && (!only_below_follows || (c->textProps[i].flags & TEXT_PROP_ALIGN_BELOW))
                  ){
                     sc.textPropFollows = true;
                     break;
                  }
               }
            }
          }

         if (m->start_extra_for_textprop) {
            m->start_extra_for_textprop = false;
            // restore searchHiId and areaDeco when countExtraBytes is down to zero
            searchDecoSaved = m->searchHiId;
            areaDecoSaved = sc.areaDeco;
            m->searchHiId = 0;
            sc.areaDeco = EMPTY_DECO;
         }

         Decoration* areaDecoTmp = m->extra_for_textprop ? &areaDecoSaved : &sc.areaDeco;

         // handle Visual or match hiliting in this line
         if (m->vcol == m->fromcol
                || (m->vcol + 1 == m->fromcol
                     && ((m->countExtraBytes == 0 && mb_ptr2cells(m->ptr) > 1)
                         || (m->countExtraBytes > 0 && m->extraBytes
                        && mb_ptr2cells(m->extraBytes) > 1)))
                || ((int)sc.vcol_prev == c->fromcol_prev && sc.vcol_prev < m->vcol // not at margin
                     && m->vcol < m->tocol)
         )
            *areaDecoTmp = c->visualDeco;      // start hiliting
         ei (areaDecoTmp->hiId != SHORT 
              && (m->vcol == m->tocol || (c->noInvertCursor && (ColNr)m->vcol == port->virtCol))
         )
            areaDecoTmp->hiId = SHORT;      // stop hiliting

         if (m->countExtraBytes == 0) {
            //Check for start/end of 'hlsearch' and other matches.
            //After end, check for start/end of next match.
            //When another match, have to check for start again.
            m->bufferLen = (long)(m->ptr - m->line);
            m->searchHiId = update_search_hl(
               port, c->lnum, (ColNr)m->bufferLen, &(m->line), &screenSearchP, sc.didLineDeco, 
               sc.listCharEndOfLine, OUT &onLastCol
            );
            m->ptr = m->line + (m->bufferLen);  // "line" may have been changed

            //Check if ComplMatchIns hilite is needed.
            if ((stateG & MODE_INSERT) && ins_compl_win_active(port)
                   && (c->inCurLine || ins_compl_lnum_in_range(c->lnum))
            ){
               Decoration insMatchDeco = getDecorationIfColumnIsWithinCompletion(
                  c->lnum, (int)(m->ptr - m->line)
               );
               if (insMatchDeco.hiId != SHORT) {
                  m->searchHiId = insMatchDeco.hiId;
               } 
            }
         }

         if (m->diff_hlf != 0) {
            if (c->lineChanges->num_changes > 0
               && m->changeIndex >= 0
               && m->changeIndex < c->lineChanges->num_changes - 1)
            {
               if (m->ptr - m->line >= 
                     c->lineChanges->changes[m->changeIndex + 1].dc_start[c->lineChanges->bufidx]
               ) {
                  m->changeIndex++;
               }
            }
            int added = false;
            if (c->lineChanges->num_changes > 0 && m->changeIndex >= 0 
                  && m->changeIndex < c->lineChanges->num_changes
            ) {
               added = diff_change_parse(
                   c->lineChanges,
                   c->lineChanges->changes + m->changeIndex,
                   c->changeStart, c->changeEnd
               );
            }
            //When there is extra text (e.g. virtual text) it gets the
            //diff hiliting for the line, but not for changed text.
            if (m->diff_hlf == HLF_CHD 
                  && m->ptr - m->line >= *c->changeStart 
                  && m->countExtraBytes == 0
            )
               m->diff_hlf = added ? HLF_TXA : HLF_TXD;   // added/changed text
            if ((m->diff_hlf == HLF_TXD || m->diff_hlf == HLF_TXA)
                  && ((m->ptr - m->line >= *c->changeEnd && m->countExtraBytes == 0)
                         || (m->countExtraBytes > 0 && m->extra_for_textprop))
            )
               m->diff_hlf = HLF_CHD;      // changed line
            m->lineDeco = getFullDecoration(m->diff_hlf);
            if (port->o.cursorLine && c->lnum == port->cursor.lnum
               && (!m->cul_screenline 
                     || (m->vcol >= c->left_curline_col && m->vcol <= c->right_curline_col))
            )
               applyCursorlineHilite(m);
         }

         if (c->hasExtraHiliting && m->countExtraBytes == 0) {
            if (c->needDecoFromTerm)
               syntaxDeco = uiGetDeco(port, c->lnum, m->vcol);
            else {
               syntaxDeco = EMPTY_DECO;
            }
            // Get syntax decoration.
            if (m->syntaxHilitingOn) {
               //Get the syntax decoration for the character. If there is an error, disable syntax 
               //hiliting
               *m->anyEmsgSave = anyEmsgG;
               anyEmsgG = false;

               m->bufferLen = (long)(m->ptr - m->line);
               if (m->bufferLen == prevSyntaxCol)
                  //at same column again
                  syntaxDeco = prevCharDeco;
               else {
                  syntaxDeco = syntGetDeco((ColNr)m->bufferLen, false);
                  prevSyntaxCol = m->bufferLen;
                  prevCharDeco = syntaxDeco;
               }

               if (anyEmsgG) {
                  port->ownSyntax->b_syn_error = true;
                  m->syntaxHilitingOn = false;
                  syntaxDeco = EMPTY_DECO;
               } else
                  anyEmsgG = *m->anyEmsgSave;

               // Need to get the line again, a multi-line regexp may have made it invalid
               m->line = memGetLine(port->book, c->lnum, false);
               m->ptr = m->line + m->bufferLen;
            }
         }
         if (text_prop_type) {
            syntaxDeco = textPropDeco;
            textPropDeco_comb = syntaxDeco;
         }

         //Decide which of the hilite decorations to use.
         sc.decoPriority = true;
         if (sc.areaDeco.hiId != SHORT) {
            if (highlight_match)
               m->charDeco = sc.areaDeco;
            else 
               //let search hilite show in Visual area if possible
               m->charDeco = getFullDecoration(m->searchHiId);
         } ei (m->searchHiId != 0) {
            m->charDeco = getFullDecoration(m->searchHiId);
         } ei (m->lineDeco.hiId != SHORT 
             && ((m->fromcol == -10 && m->tocol == MAXCOL)
                  || m->vcol < m->fromcol
                  || sc.vcol_prev < c->fromcol_prev
                  || m->vcol >= m->tocol)
         ){
            //Use m->lineDeco when not in the Visual or 'incsearch' area
            //(areaDeco may be empty when "noInvertCursor" is set).
            m->charDeco = m->lineDeco;
            sc.decoPriority = false;
         } else {
            m->charDeco = syntaxDeco;
            sc.decoPriority = false;
         }
         //override with text property hilite when "override" is true
         if (text_prop_type && (sc.textPropFlags & PT_FLAG_OVERRIDE) != 0)
            m->charDeco = textPropDeco;
      }

      //combine decoration with @portcolor
      if (m->portalDeco.hiId != SHORT) {
         if (m->charDeco.hiId == SHORT)
            m->charDeco = m->portalDeco;
         else
            m->charDeco = m->charDeco;
      }

      //Get the next character to put on the screen.
      //The "extraBytes" points to the extra stuff that is inserted to represent special characters 
      //(non-printable stuff) and other things.  When all characters are the same, c_extra is used.
      //If m->c_final is set, it will compulsorily be used at the end.
      //"extraBytes" must end in a ZERO to avoid utfCharLen() reads past 
      //"extraBytes[countExtraBytes]".
      //For the '$' of the 'list' option, countExtraBytes == 1, extraBytes == "".
      Unt currSymb; 
      if (m->countExtraBytes > 0) {
         if (m->c_extra != ZERO || (m->countExtraBytes == 1 && m->c_final != ZERO)) {
            currSymb = (m->countExtraBytes == 1 && m->c_final != ZERO) 
               ? m->c_final : m->c_extra;
            sc.mb_c = currSymb;   // doesn't handle non-utf-8 multi-byte!
            if (mb_char2len(currSymb) > 1) {
               sc.mb_utf8 = true;
               characterCombiner[0] = 0;
               currSymb = 0xc0;
            } else
               sc.mb_utf8 = false;
         } else {
            currSymb = *m->extraBytes;
            sc.mb_c = currSymb;
            //If the UTF-8 character is more than one byte: Decode it into "mb_c".
            multibLength = utfCharLen(m->extraBytes);
            sc.mb_utf8 = false;
            if (multibLength > m->countExtraBytes)
               multibLength = 1;
            ei (multibLength > 1) {
               sc.mb_c = utfc_ptr2char(m->extraBytes, characterCombiner);
               sc.mb_utf8 = false;
               currSymb = 0xc0;
            }
            if (multibLength == 0)  // at the ZERO at end-of-line
               multibLength = 1;

            // If a double-width char doesn't fit display a '>' in the last column
            if ((m->col >= (int)port->width - 1) && mb_char2cells(sc.mb_c) == 2) {
               currSymb = '>';
               sc.mb_c = '>';
               multibLength = 1;
               sc.mb_utf8 = false;
               sc.multiDeco = getFullDecoration(HLF_AT);
               if (m->cursorlineDeco.hiId != SHORT)
                  overlayDeco(OUT &sc.multiDeco, OVERLAY_DECO_ALTERED_BG);

               // put the pointer back to output the double-width
               // character at the start of the next line.
               ++m->countExtraBytes;
               --m->extraBytes;
            } else {
               m->countExtraBytes -= multibLength - 1;
               m->extraBytes += multibLength - 1;
            }
            ++m->extraBytes;
         }
         --m->countExtraBytes;
         if (m->countExtraBytes <= 0) {
            //Only restore searchHiId and areaDeco after "countExtraBytes" in
            //the next screen line is also done.
            if (m->saved_n_extra <= 0) {
               if (m->searchHiId == SHORT)
                  m->searchHiId = searchDecoSaved;
               if (sc.areaDeco.hiId == SHORT && *m->ptr != ZERO)
                  sc.areaDeco = areaDecoSaved;

               if (m->extra_for_textprop)
                  // m->extraDeco should be used at this position but not any further.
                  sc.resetOverlayDeco = true;
            }

            m->extra_for_textprop = false;
            inLineBreak = false;
         }
      } else {
         CS prev_ptr = m->ptr;

         //Get a character from the line itself.
         currSymb = *m->ptr;
         if (currSymb == ZERO) {
            //text is finished, may display a "below" virtual text
            didLine = true;
            //no more cells to skip
            m->cellsToSkip = 0;
            if (term_shobuffer(port->book)
                  && m->vcol == 0
                  && decoEq(m->portalDeco, uiGetDeco(port, c->lnum, -1))
            ) //reset hiliting decoration
               m->portalDeco = EMPTY_DECO;
         }

         sc.mb_c = currSymb;
         // If the UTF-8 character is more than one byte: Decode it into "mb_c".
         multibLength = utfCharLen(m->ptr);
         sc.mb_utf8 = false;
         if (multibLength > 1) {
            sc.mb_c = utfc_ptr2char(m->ptr, characterCombiner);
            //Overlong encoded ASCII or ASCII with composing char
            //is displayed normally, except a ZERO.
            if (sc.mb_c < 0x80) {
               currSymb = sc.mb_c;
            }
            sc.mb_utf8 = true;

            //At start of the line we can have a composing char.
            //Draw it as a space with a composing char.
            if (utf_iscomposing(sc.mb_c)) {
               for (int i = MAX_COMBINED_SYMBOLS - 1; i > 0; --i)
                  characterCombiner[i] = characterCombiner[i - 1];
               characterCombiner[0] = sc.mb_c;
               sc.mb_c = ' ';
            }
         }

         if ((multibLength == 1 && currSymb >= 0x80)
             || (multibLength >= 1 && sc.mb_c == 0)
             || (multibLength > 1 && (!bookIsCharPrintable(sc.mb_c)))
         ) {
            // Illegal UTF-8 byte: display as <xx>.
            // Non-BMP character : display as ? or fullwidth ?.
            transchar_hex(m->extra, sc.mb_c);
            m->extraBytes = m->extra;
            currSymb = *m->extraBytes;
            sc.mb_c = strAdvanceMultibyte(&m->extraBytes);
            sc.mb_utf8 = (currSymb >= 0x80);
            m->countExtraBytes = (int)STRLEN(m->extraBytes);
            m->c_extra = ZERO;
            m->c_final = ZERO;
            if (sc.areaDeco.hiId == SHORT && m->searchHiId == 0) {
               sc.numDecoCells = m->countExtraBytes + 1;
               m->extraDeco = getFullDecoration(HLF_8);
               sc.charDecoSaved = m->charDeco; // save current deco
            }
         } ei (multibLength == 0)  // at the ZERO at end-of-line
            multibLength = 1;
            
         // If a double-width char doesn't fit display a '>' in the
         // last column; the character is displayed at the start of the next line.
         if (( (m->col >= (int)port->width - 1)) && mb_char2cells(sc.mb_c) == 2) {
            currSymb = '>';
            sc.mb_c = currSymb;
            sc.mb_utf8 = false;
            multibLength = 1;
            sc.multiDeco = getFullDecoration(HLF_AT);
            // Put pointer back so that the character will be displayed at the start of next line
            m->ptr--;
         } ei (*m->ptr != ZERO)
            m->ptr += multibLength - 1;

         // If a double-width char doesn't fit at the left side display
         // a '<' in the first column.  Don't do this for unprintable characters.
         if (m->cellsToSkip > 0 && multibLength > 1 && m->countExtraBytes == 0) {
            m->countExtraBytes = 1;
            m->c_extra = MB_FILLER_CHAR;
            m->c_final = ZERO;
            currSymb = ' ';
            if (sc.areaDeco.hiId == SHORT && m->searchHiId == 0) {
               sc.numDecoCells = m->countExtraBytes + 1;
               m->extraDeco = getFullDecoration(HLF_AT);
            }
            sc.mb_c = currSymb;
            sc.mb_utf8 = false;
            multibLength = 1;
         }

         m->ptr++;

         if (c->hasExtraHiliting) {
            m->bufferLen = (long)(m->ptr - m->line);
            if (port->o.list) {
               m->inMultispace = currSymb == ' ' 
                  && (*m->ptr == ' ' || (prev_ptr > m->line && prev_ptr[-1] == ' '));
               if (!m->inMultispace)
                  m->multispacePos = 0;
            }

            // 'list': Change char 160 to 'nbsp' and space to 'space' setting in @listchars.
            // But not when the character is followed by a composing character (use multibLength 
            // to check that).
            if (port->o.list
               && ((((currSymb == 160 && multibLength == 1)
                     || (sc.mb_utf8 && ((sc.mb_c == 160 && multibLength == 2) 
                           || (sc.mb_c == 0x202f && multibLength == 3)))
                    )
                    && listCharsG.nbsp
                   )
                   || (currSymb == ' ' && multibLength == 1
                        && (listCharsG.space 
                           || (m->inMultispace && listCharsG.multispace))
                        && m->ptr - m->line >= c->leadcol
                        && m->ptr - m->line <= c->trailcol)
                  )
            ) {
               if (m->inMultispace && listCharsG.multispace) {
                  currSymb = listCharsG.multispace[m->multispacePos];
                  m->multispacePos++;
                  if (listCharsG.multispace[m->multispacePos] == ZERO)
                     m->multispacePos = 0;
               } else
                  currSymb = (currSymb == ' ') ? listCharsG.space : listCharsG.nbsp;
               if (sc.areaDeco.hiId == SHORT && m->searchHiId == 0) {
                  sc.numDecoCells = 1;
                  m->extraDeco = getFullDecoration(HLF_8);
               }
               sc.mb_c = currSymb;
               if (mb_char2len(currSymb) > 1) {
                  sc.mb_utf8 = true;
                  characterCombiner[0] = 0;
                  currSymb = 0xc0;
               } else
                  sc.mb_utf8 = true;
            }

            if (currSymb == ' ' 
                  && ((c->trailcol != MAXCOL && m->ptr > m->line + c->trailcol) 
                        || (c->leadcol != 0 && m->ptr < m->line + c->leadcol))
            ){
               if (c->leadcol != 0 && m->inMultispace && m->ptr < m->line + c->leadcol
                      && listCharsG.leadmultispace
               ) {
                  currSymb = listCharsG.leadmultispace[m->multispacePos];
                  m->multispacePos++; 
                  if (listCharsG.leadmultispace[m->multispacePos] == ZERO)
                     m->multispacePos = 0;
               } ei (m->ptr > m->line + c->trailcol && listCharsG.trail)
                  currSymb = listCharsG.trail;
               ei (m->ptr < m->line + c->leadcol && listCharsG.lead)
                  currSymb = listCharsG.lead;
               ei (c->leadcol != 0 && listCharsG.space)
                  currSymb = listCharsG.space;


               if (!sc.decoPriority) {
                  sc.numDecoCells = 1;
                  m->extraDeco = getFullDecoration(HLF_8);
               }
               sc.mb_c = currSymb;
               if (mb_char2len(currSymb) > 1) {
                  sc.mb_utf8 = true;
                  characterCombiner[0] = 0;
                  currSymb = 0xc0;
               } else
                  sc.mb_utf8 = false;
            }
         }

         // Handling of non-printable characters.
         if (!bookIsCharPrintable(currSymb)) {
            // when getting a character from the file, we may have to
            // turn it into something else on the way to putting it into "screenTextP".
            if (currSymb == TAB && (!port->o.list || listCharsG.tab1)) {
               int       tab_len = 0;
               long    vcol_adjusted = m->vcol; // removed showbreak len

               // only adjust the tab_len, when at the first column
               // after the showbreak value was drawn
               if (p_sbr && m->vcol == m->vcol_sbr && port->o.wrap)
                  vcol_adjusted = m->vcol - MB_CHARLEN(p_sbr);
               // tab amount depends on current column
               tab_len = (int)port->book->o.shiftWidth - vcol_adjusted 
                  % (int)port->book->o.shiftWidth - 1;

               m->countExtraBytes = tab_len;
               sc.mb_utf8 = false;   // don't draw as UTF-8
               if (port->o.list) {
                  currSymb = (m->countExtraBytes == 0 && listCharsG.tab3)
                              ? listCharsG.tab3
                              : listCharsG.tab1;
                  m->c_extra = listCharsG.tab2;
                  m->c_final = listCharsG.tab3;
                  sc.numDecoCells = tab_len + 1;
                  m->extraDeco = getFullDecoration(HLF_8);
                  sc.mb_c = currSymb;
                  if (mb_char2len(currSymb) > 1) {
                     sc.mb_utf8 = true;
                     characterCombiner[0] = 0;
                     currSymb = 0xc0;
                  }
               } else {
                  m->c_final = ZERO;
                  m->c_extra = ' ';
                  currSymb = ' ';
               }
            } ei (currSymb == ZERO
               && m->countExtraBytes == 0
               && (port->o.list
                   || ((m->fromcol >= 0 || c->fromcol_prev >= 0)
                  && m->tocol > m->vcol
                  && VIsual_mode != Ctrl_V
                  && (m->col < (int)port->width)
                  && !(c->noInvertCursor
                      && c->lnum == port->cursor.lnum
                      && (ColNr)m->vcol == port->virtCol)))
               && sc.listCharEndOfLine != UNT
            ){
               // Display a '$' after the line or hilite an extra character if the line break is 
               // included. For a diff line the hiliting continues after the "$".
               if (m->diff_hlf == 0 && m->lineDeco.hiId == SHORT) {
                  // In virtualedit, visual selections may extend beyond end of line.
                  if (!(c->areaHiliting && virtual_active()
                            && m->tocol != MAXCOL
                            && m->vcol < m->tocol))
                     m->extraBytes = E;
                  m->countExtraBytes = 0;
               }
               if (port->o.list && listCharsG.eol > 0)
                  currSymb = listCharsG.eol;
               else
                  currSymb = ' ';
               sc.listCharEndOfLine = UNT;
               m->ptr--;       // put it back at the ZERO
               if (!sc.decoPriority) {
                  m->extraDeco = getFullDecoration(HLF_AT);
                  sc.numDecoCells = 1;
               }
               sc.mb_c = currSymb;
               if (mb_char2len(currSymb) > 1) {
                  sc.mb_utf8 = true;
                  characterCombiner[0] = 0;
                  currSymb = 0xc0;
               } else
                  sc.mb_utf8 = false;   // don't draw as UTF-8
            } ei (currSymb != ZERO) {
               m->extraBytes = transchar_buf(currSymb);
               if (m->countExtraBytes == 0)
                  m->countExtraBytes = byte2cells(currSymb) - 1;
               m->c_extra = ZERO;
               m->c_final = ZERO;
               m->countExtraBytes = byte2cells(currSymb) - 1;
               currSymb = *m->extraBytes++;
               if (!sc.decoPriority) {
                  sc.numDecoCells = m->countExtraBytes + 1;
                  if (text_prop_type && (sc.textPropFlags & PT_FLAG_OVERRIDE) != 0)
                     m->extraDeco = textPropDeco;
                  else 
                     m->extraDeco = getFullDecoration(HLF_8);
               }
               sc.mb_utf8 = false;   // don't draw as UTF-8
            } ei (VIsual_active
                && (VIsual_mode == Ctrl_V || VIsual_mode == 'v')
                && virtual_active()
                && m->tocol != MAXCOL
                && m->vcol < m->tocol
                && ( (m->col < (int)port->width))
            ){
               currSymb = ' ';
               m->ptr--;       // put it back at the ZERO
            } ei ((
                   m->diff_hlf != SHORT 
                   || m->portalDeco.hiId != SHORT 
                   || m->lineDeco.hiId != SHORT 
               ) && (m->col < (int)port->width)
            ){
               // Hilite until the right side of the portal
               currSymb = ' ';
               m->ptr--;       // put it back at the ZERO

               // Remember we do the char for line hiliting.
               sc.didLineDeco++;

               // don't do search HL for the rest of the line
               if (m->lineDeco.hiId != SHORT && m->charDeco.hiId == m->searchHiId
                     && (sc.didLineDeco > 1 || (port->o.list && listCharsG.eol > 0))
               )
               m->charDeco = m->lineDeco;
               // At end of line: if Sign is present with line hilite, reset charDeco
               // but not when cursorline is active
               if (c->signPresent && m->signHilites.lineHiId > 0 && m->drawState == WL_LINE
                   && !(port->o.cursorLine && c->lnum == port->cursor.lnum)
               )
                  m->charDeco = getFullDecoration(m->signHilites.lineHiId);
               if (m->diff_hlf == HLF_TXD || m->diff_hlf == HLF_TXA) {
                  m->diff_hlf = HLF_CHD;
                  if (c->visualDeco.hiId == SHORT || !decoEq(m->charDeco, c->visualDeco)) {
                     m->charDeco = getFullDecoration(m->diff_hlf);
                     if (port->o.cursorLine && c->lnum == port->cursor.lnum
                            && (!m->cul_screenline
                              || (m->vcol >= c->left_curline_col 
                                    && m->vcol <= c->right_curline_col)
                              )
                     )
                        applyCursorlineHilite(m);
                  }
               }
               if (m->portalDeco.hiId != SHORT) {
                  m->charDeco = m->portalDeco;
                  if (m->lineDeco.hiId != SHORT)
                     m->charDeco = m->lineDeco;
               }
            }
         }
      }
      
      if (!drawLineSub(m, port, c, &sc, currSymb)) {
         break;
      }
   }  // for every character in the line
}

//{{{upper

//Display line "lnum" of portal "po" on the screen. Start at row "startrow", stop when "endrow"
//is reached. When only updating the number column, "drawingOnlyNumberCol" is set to the height of the 
//line, otherwise it is set to 0.
//
//Return the number of last row the line occupies.
pub int
drawLineOnScreen(
   Portal* port,
   LineNr lnum,
   int startrow,
   int endrow,
   int drawingOnlyNumberCol
){
   DrawCtx m;     // mutable context between the massive functions here
   Subcontext c;    // immutable context
   c.inCurLine = port == curPor && lnum == curPor->cursor.lnum;
   m.cellsToSkip = 0;    // nr of cells to skip for leftCol or skipCol
   c.fromcol_prev = -2;   // start of inverting after cursor
   c.noInvertCursor = false;   // don't invert the cursor
   c.isLineVisible = false;
   Pos  pos;

   c.areaHiliting = false; // Visual or incsearch hiliting in this line
   m.searchHiId = 0;   // decorations desired by 'hlsearch' or ComplMatchIns
   m.syntaxHilitingOn = false;   // this buffer has syntax hiliting
   c.lastTextpropTextInd = -1;
   m.textPropNext = 0;   // next text property to use
   c.textProps = NULL;
   c.textPropIndices = NULL;
   Byte nextLine[SPWORDLEN * 2];// text with start of the next line
   c.nextLineCol = 0;   // column where nextline[] starts
   c.nextLineInd = 0;   // index in nextline[] where next line starts
   c.visualDeco = EMPTY_DECO;      // decorations for Visual and incsearch hiliting
   c.lineDecoSaved = EMPTY_DECO;
   c.numDeco = EMPTY_DECO;      // decoration for the number column
   c.currCheckedCol = 0;   // checked column for current line
   c.hasExtraHiliting = false;   // has extra hiliting
   int changeStart = MAXCOL;   // first col of changed area
   int changeEnd = -1;   // last col of changed area
   DiffLine lineChanges;
   m.changeIndex = -1;
   m.inMultispace = false;   // in multiple consecutive spaces
   m.multispacePos = 0;   // position in lcs-multispace string
   c.needDecoFromTerm = false;

   // margin columns for the screen line, needed for when 'cursorlineopt' contains "screenline"
   c.left_curline_col = 0;
   c.right_curline_col = 0;
   Subcontext sc = (Subcontext) {.trailcol = 0, .leadcol = 0};

   if (startrow > endrow)      // past the end already!
      return startrow;

   CLEAR_FIELD(m);

   m.lnum = lnum;
   m.startrow = startrow;
   m.endRow = endrow;
   m.row = startrow;
   m.screen_row = m.row + port->windowRow;
   m.fromcol = -10;
   m.tocol = MAXCOL;
   m.vcol_sbr = -1;

   if (drawingOnlyNumberCol == 0) { // normal line, with content
      //To speed up the loop below, set hasExtraHiliting when there is linebreak,
      //trailing white space and/or syntax processing to be done.
      if (syntax_present(port) && !port->ownSyntax->b_syn_error){
         // Prepare for syntax hiliting in this line. When there is an error, stop hiliting
         *m.anyEmsgSave = anyEmsgG;
         anyEmsgG = false;
         syntaxStartLine(port, lnum);
         if (anyEmsgG)
            port->ownSyntax->b_syn_error = true;
         else {
            anyEmsgG = *m.anyEmsgSave;
            m.syntaxHilitingOn = true;
            c.hasExtraHiliting = true;
         }
      }

      if (term_shobuffer(port->book)) {
         c.hasExtraHiliting = true;
         c.needDecoFromTerm = true;
         m.portalDeco = uiGetDeco(port, lnum, -1);
      }

      // handle Visual active in this portal
      if (VIsual_active && port->book == curPor->book) {
         Pos* top;
         Pos* bot;
         if (LTOREQ_POS(curPor->cursor, VIsual)) {
            // Visual is after curPor->cursor
            top = &curPor->cursor;
            bot = &VIsual;
         } else {
            // Visual is before curPor->cursor
            top = &VIsual;
            bot = &curPor->cursor;
         }
         
         c.isLineVisible = (lnum >= top->lnum && lnum <= bot->lnum);
         if (VIsual_mode == Ctrl_V) {
            // block mode
            if (c.isLineVisible) {
               m.fromcol = port->oldCursorFcol;
               m.tocol = port->oldCursorLcol;
            }
         } else {
            // non-block mode
            if (lnum > top->lnum && lnum <= bot->lnum)
               m.fromcol = 0;
            ei (lnum == top->lnum) {
               if (VIsual_mode == 'V')   // linewise
                  m.fromcol = 0;
               else {
                  bookGetVirtualColInVirtualMode(port, top, (ColNr *)&m.fromcol, NULL, NULL);
                  if (gchar_pos(top) == ZERO)
                     m.tocol = m.fromcol + 1;
               }
            }
            if (VIsual_mode != 'V' && lnum == bot->lnum) {
               if (bot->col == MAXCOL)
                  m.tocol = MAXCOL;
               else {
                  pos = *bot;
                  bookGetVirtualColInVirtualMode(port, &pos, NULL, NULL, (ColNr *)&m.tocol);
                  ++m.tocol;
               }
            }
         }

         //Check if the character under the cursor should not be inverted
         if (!highlight_match && c.inCurLine)
            c.noInvertCursor = true;

         // if inverting in this line set areaHiliting
         if (m.fromcol >= 0) {
            c.areaHiliting = true;
            c.visualDeco = getFullDecoration(HLF_V);
         }
      }
      // handle @incsearch and ":s///c" hiliting
      ei (highlight_match
         && port == curPor
         && lnum >= curPor->cursor.lnum
         && lnum <= curPor->cursor.lnum + search_match_lines
      ){
         if (lnum == curPor->cursor.lnum)
            getvcol(curPor, &(curPor->cursor), (ColNr *)&m.fromcol, NULL, NULL);
         else
            m.fromcol = 0;
         if (lnum == curPor->cursor.lnum + search_match_lines) {
            pos.lnum = lnum;
            pos.col = search_match_endcol;
            getvcol(curPor, &pos, (ColNr *)&m.tocol, NULL, NULL);
         } else
            m.tocol = MAXCOL;
         // do at least one character; happens when past end of line
         if (m.fromcol == m.tocol && search_match_endcol)
            m.tocol = m.fromcol + 1;
         c.areaHiliting = true;
         overlayDeco(OUT &c.visualDeco, OVERLAY_DECO_INVERT);
      }
   }

   LineDiffStatus linestatus;
   m.filler_lines = diff_check_with_linestatus(port, lnum, OUT &linestatus);

   CLEAR_FIELD(lineChanges);

   if (linestatus != LINE_STATUS_UNCHANGED) {
      if (linestatus == LINE_STATUS_CHANGED) {
         if (diff_find_change(port, lnum, &lineChanges)) {
            m.diff_hlf = HLF_ADD;   // added line
         } ei (lineChanges.num_changes > 0) {
            int added = diff_change_parse(
               &lineChanges, &lineChanges.changes[0], &changeStart, &changeEnd
            );
            if (changeStart == 0) {
               if (added)
                  m.diff_hlf = HLF_TXA;   // added text on changed line
               else
                  m.diff_hlf = HLF_TXD;   // changed text on changed line
            } else
               m.diff_hlf = HLF_CHD;   // unchanged text on changed line
            m.changeIndex = 0;
         } else {
            m.diff_hlf = HLF_CHD;   // changed line
            m.changeIndex = 0;
         }
      } else
         m.diff_hlf = HLF_ADD;

      c.areaHiliting = true;
   }

   if (lnum == port->topLine)
      m.filler_lines = port->topFill;

   m.filler_todo = m.filler_lines;

   c.signPresent = markGetSignDecorations(port, lnum, OUT &m.signHilites);
   if (c.signPresent)
      c.numDeco = getFullDecoration(m.signHilites.lineNumHiId);

   // If this line has a sign with line hiliting, set m.lineDeco.
   if (c.signPresent) {
      m.lineDeco = getFullDecoration(m.signHilites.lineHiId);
      // Hilite the current line in the location portal
   } 
   if (isLocationListBook(port->book) && llCurrentEntry(port) == lnum)
      m.lineDeco = getFullDecoration(HLF_QFL);
      
   if (m.lineDeco.flags != 0)
      c.areaHiliting = true;

   m.line = memGetLine(port->book, lnum, false);
   m.ptr = m.line;

   if (port->o.list) {
      if (listCharsG.space
            || listCharsG.multispace
            || listCharsG.leadmultispace
            || listCharsG.trail
            || listCharsG.lead
            || listCharsG.nbsp
      )
         c.hasExtraHiliting = true;

      // find start of trailing whitespace
      if (listCharsG.trail) {
         c.trailcol = memGetBookLen(port->book, lnum);
         while (c.trailcol > (ColNr)0 && SPACE_OR_TAB(m.ptr[sc.trailcol - 1]))
            --c.trailcol;
         c.trailcol += (ColNr)(m.ptr - m.line);
      }
      // find end of leading whitespace
      if (listCharsG.lead || listCharsG.leadmultispace) {
         c.leadcol = 0;
         while (SPACE_OR_TAB(m.ptr[c.leadcol]))
            ++c.leadcol;
         if (m.ptr[c.leadcol] == ZERO)
            // in a line full of spaces all of them are treated as trailing
            c.leadcol = (ColNr)0;
         else
            // keep track of the first column not filled with spaces
            c.leadcol += (ColNr)(m.ptr - m.line) + 1;
      }
   }

   m.portalDeco = getPortcolorDeco(port);
   if (m.portalDeco.flags != 0) {
      m.portalDeco = m.portalDeco;
      c.areaHiliting = true;
   }

   // When skipCol is non-zero and there is virtual text above the actual
   // text, then this much of the virtual text is skipped.
   int skipcol_in_textPropAbove = 0;

   if (PORTAL_IS_POPUP(port))
      m.screen_line_flags |= SLF_POPUP;

   CS propStart;
   c.textPropCount = get_text_props(OUT &propStart, port->book, lnum, false);
   if (c.textPropCount > 0) {
      // Make a copy of the properties, so that they are properly aligned.
      c.textProps = ALLOC_MULT(TextProp, sc.textPropCount);
      MEMMOVE(OUT c.textProps, propStart, sc.textPropCount * sizeof(TextProp));

      // Allocate an array for the indexes.
      c.textPropIndices = ALLOC_MULT(int, sc.textPropCount);
      c.areaHiliting = true;
      c.hasExtraHiliting = true;

      // Find the last text property that inserts text
      for (int i = 0; i < c.textPropCount; ++i) {
         if (c.textProps[i].id < 0)
            c.lastTextpropTextInd = i;
      } 

      //Text props "above" move the line number down to where the text
      //is. Only count the ones that are visible, not those that are skipped because of skipCol
      int text_width = port->width - normalPortalColumnOffset(port);
      for (int i = c.textPropCount - 1; i >= 0; --i) {
         if (c.textProps[i].flags & TEXT_PROP_ALIGN_ABOVE) {
            if (lnum == port->topLine && port->skipCol - skipcol_in_textPropAbove >= text_width){
               //This virtual text above is skipped, remove it from the array
               skipcol_in_textPropAbove += text_width;
               for (int j = i + 1; j < c.textPropCount; ++j)
                  c.textProps[j - 1] = sc.textProps[j];
               ++i;
               c.textPropCount--;
            } else
               ++m.textPropAbove_count;
         }
      } 
   }

   if (drawingOnlyNumberCol > 0) {
      // skip over rows only used for virtual text above
      m.row += m.textPropAbove_count;
      if (m.row >= m.endRow) {
         eeglFree(c.textProps);
         eeglFree(c.textPropIndices);
         return m.row;
      }
      m.screen_row += m.textPropAbove_count;
   }

   c.vcolFirstChar = 0;

   //'nowrap' or 'wrap' and a single line that doesn't fit: Advance to the
   //first character to be displayed.
   if (port->o.wrap)
      m.bufferLen = startrow == 0 ? port->skipCol - skipcol_in_textPropAbove : 0;
   else
      m.bufferLen = port->leftCol;
   if (m.bufferLen > 0 && drawingOnlyNumberCol == 0) {
      Byte* prev_ptr = m.ptr;
      CharTableSize cts;
      int charsize = 0;
      int head = 0;

      bookInitCharsForKeywordsSizeArg(OUT &cts, port, lnum, m.vcol, m.line, m.ptr);
      cts.cts_max_head_vcol = m.bufferLen;
      while (cts.cts_vcol < m.bufferLen && *cts.cts_ptr != ZERO) {
         head = 0;
         charsize = win_lbr_chartabsize(&cts, &head);
         cts.cts_vcol += charsize;
         prev_ptr = cts.cts_ptr;
         MB_PTR_ADV(cts.cts_ptr);
         if (port->o.list) {
            m.inMultispace = *prev_ptr == ' ' 
               && (*cts.cts_ptr == ' ' || (prev_ptr > m.line && prev_ptr[-1] == ' '));
            if (!m.inMultispace)
               m.multispacePos = 0;
            ei (cts.cts_ptr >= m.line + c.leadcol && listCharsG.multispace) {
               m.multispacePos++;
               if (listCharsG.multispace[m.multispacePos] == ZERO)
                  m.multispacePos = 0;
            } ei (cts.cts_ptr < m.line + c.leadcol && listCharsG.leadmultispace) {
               m.multispacePos++;
               if (listCharsG.leadmultispace[m.multispacePos] == ZERO)
                  m.multispacePos = 0;
            }
         }
      }
      m.vcol = cts.cts_vcol;
      m.ptr = cts.cts_ptr;
      clear_chartabsize_arg(&cts);

      // When:
      // - 'cuc' is set, or
      // - the visual mode is active,
      // the end of the line may be before the start of the displayed part.
      if (m.vcol < m.bufferLen 
            && (virtual_active() || (VIsual_active && port->book == curPor->book))
      )
         m.vcol = m.bufferLen;

      // Handle a character that's not completely on the screen: Put ptr at that character but skip 
      // the first few screen characters.
      if (m.vcol > m.bufferLen) {
          m.vcol -= charsize;
          m.ptr = prev_ptr;
      }
      if (m.bufferLen > m.vcol)
         m.cellsToSkip = m.bufferLen - m.vcol - head;

      //Adjust for when the inverted text is before the screen,
      //and when the start of the inverted text is before the screen.
      if (m.tocol <= m.vcol)
         m.fromcol = 0;
      ei (m.fromcol >= 0 && m.fromcol < m.vcol)
         m.fromcol = m.vcol;

      // When skipCol is non-zero, first line needs @showbreak
      if (port->o.wrap)
         m.need_showbreak = true;
   }

   // Correct hiliting for cursor that can't be disabled. Avoids having to check this for each character
   if (m.fromcol >= 0) {
      if (c.noInvertCursor) {
         if ((ColNr)m.fromcol == port->virtCol) {
            //hiliting starts at cursor, let it start just after the cursor
            c.fromcol_prev = m.fromcol;
            m.fromcol = -1;
         } ei ((ColNr)m.fromcol < port->virtCol)
            //restart hiliting after the cursor
            c.fromcol_prev = port->virtCol;
      }
      if (m.fromcol >= m.tocol)
         m.fromcol = -1;
   }

   if (drawingOnlyNumberCol == 0) {
      m.bufferLen = (long)(m.ptr - m.line);
      c.areaHiliting = c.areaHiliting || searchPrepareHiliteLine(
         port, lnum, (ColNr)m.bufferLen, &m.line, &screenSearchP, OUT &m.searchHiId
      );
      m.ptr = m.line + m.bufferLen; // "line" may have been updated
   }

   if ((stateG & MODE_INSERT) && ins_compl_win_active(port)
             && (c.inCurLine || ins_compl_lnum_in_range(lnum)))
      c.areaHiliting = true;

   //Cursor line hiliting for 'cursorline' in the current portal.
   if (port->o.cursorLine && lnum == port->cursor.lnum) {
      //Do not show the cursor line in the text when Visual mode is active,
      //because it's not clear what is selected then.
      if (!(port == curPor && VIsual_active)) {
         m.cul_screenline = (port->o.wrap);

         //Only apply CursorLine hilite here when "screenline" is not
         //present in 'cursorlineopt'.  Otherwise it's done later.
         if (!m.cul_screenline)
            applyCursorlineHilite(&m);
         else {
            c.lineDecoSaved = m.lineDeco;
            computeHilitingMargins(port, OUT &c.left_curline_col, OUT &sc.right_curline_col);
         }
         c.areaHiliting = true;
      }
   }

   drawLineOnScreen_start(&m, false);
   c.lnum = lnum,
   c.drawingOnlyNumberCol = drawingOnlyNumberCol;
   c.lineChanges = &lineChanges;
   c.changeStart = &changeStart;
   c.changeEnd = &changeEnd;
   c.nextLine = nextLine;
   
   drawLineLoop(&m, &sc, port);

   eeglFree(c.textProps);
   eeglFree(c.textPropIndices);

   eeglFree(m.p_extra_free);
   eeglFree(m.saved_p_extra_free);
   return m.row;
}

//}}}
//}}}
//{{{api functions

pub void
f_screenattr(Arr(Var) argvars, Var* returnVar) {
   Byte flags;

   int row = (int)varGetNumberChk(argvars, NULL) - 1;
   int col = (int)varGetNumberChk(argvars + 1, NULL) - 1;
   if (row < 0 || row >= screenLinesRowsG || col < 0 || col >= screenLinesColsG)
      flags = 0;
   else
      flags = screenDecosP[lineStartsP[row] + col].flags;
   returnVar->number = flags;
}

pub void
f_screenchar(Arr(Var) argvars, Var* returnVar) {
   int row = (int)varGetNumberChk(argvars, NULL) - 1;
   int col = (int)varGetNumberChk(argvars + 1, NULL) - 1;
   Unt c;
   if (row < 0 || row >= screenLinesRowsG || col < 0 || col >= screenLinesColsG)
      c = UNT;
   else {
      Byte buf[MB_MAXBYTES + 1];
      screen_getbytes(row, col, buf, NULL);
      c = mb_ptr2char(buf);
   }
   returnVar->number = c;
}

pub void
f_screenchars(Arr(Var) argvars, Var* returnVar) {
   allocReturnList(returnVar);

   int row = (int)varGetNumberChk(argvars, NULL) - 1;
   int col = (int)varGetNumberChk(argvars + 1, NULL) - 1;
   if (row < 0 || row >= screenLinesRowsG || col < 0 || col >= screenLinesColsG)
      return;

   Byte buf[MB_MAXBYTES + 1];
   screen_getbytes(row, col, buf, NULL);
   int pcc[MAX_COMBINED_SYMBOLS];
   int c = utfc_ptr2char(buf, pcc);
   list_append_number(returnVar->list, (Long)c);

   for (int i = 0; i < MAX_COMBINED_SYMBOLS && pcc[i] != 0; ++i)
      list_append_number(returnVar->list, (Long)pcc[i]);
}

//"screencol()" function. First column is 1 to be consistent with virtcol().
pub void
f_screencol(Arr(Var) argvars UNUSED, Var* returnVar) {
   returnVar->number = screen_screencol() + 1;
}

pub void
f_screenrow(Arr(Var) argvars UNUSED, Var* returnVar) {
   returnVar->number = screen_screenrow() + 1;
}

pub void
f_screenstring(Arr(Var) argvars, Var* returnVar) {
   Byte buf[MB_MAXBYTES + 1];

   returnVar->string = NULL;
   returnVar->tag = VAR_STRING;

   int row = (int)varGetNumberChk(argvars, NULL) - 1;
   int col = (int)varGetNumberChk(argvars + 1, NULL) - 1;
   if (row < 0 || row >= screenLinesRowsG || col < 0 || col >= screenLinesColsG)
      return;

   screen_getbytes(row, col, buf, NULL);
   returnVar->string = copyStr(buf);
}


// Scroll the screen up one line for displaying the next message line.
pub void
drawMsgScrollUp(void) {
   if (inEchoPortalG)
      return;
   // scrolling up always works
   screen_del_lines(0, 0, 1, (int)visibleRowsG, true, 0, NULL);

   if (!can_clear(S" ")) {
      //Scrolling up doesn't result in the right background. Set the
      //background here. It's not efficient, but avoids that we have to do it all over the code.
      fillRowsWithTwoChars(
         (int)visibleRowsG - 1, (int)visibleRowsG, 0, (int)visibleColsG, ' ', ' ', 
         getFullDecoration(HLF_MSG)
      );

      //Also clear the last char of the penultimate line if it was not cleared before to avoid 
      //a scroll-up.
      if (screenDecosP[lineStartsP[visibleRowsG - 2] + visibleColsG - 1].hiId == 0) {
          fillRowsWithTwoChars(
             (int)visibleRowsG - 2, (int)visibleRowsG - 1, (int)visibleColsG - 1, 
             (int)visibleColsG, ' ', ' ', getFullDecoration(HLF_MSG)
          );
      } 
   }
}

pub void
drawMsgSetCharAtOffset(Unt c, int off, ScreenCell cell) {
   if (c == ZERO) {
      screenTextP[off] = ' ';
      screenLinesUCG[off] = ZERO;
   } else {
      // composing chars
      for (Unt i = 0; i + 1 < MAX_COMBINED_SYMBOLS; ++i) {
         screenLinesCG[MAX_COMBINED_SYMBOLS * off + i] = cell.chars[i + 1];
         if (cell.chars[i + 1] == 0)
            break;
      }
      if (c >= 0x80 || (MAX_COMBINED_SYMBOLS > 0 && screenLinesCG[MAX_COMBINED_SYMBOLS * off] != 0)) {
          screenTextP[off] = ' ';
          screenLinesUCG[off] = c;
      } else {
          screenTextP[off] = c;
          screenLinesUCG[off] = ZERO;
      }
   }
   screenDecosP[off] = cellToDecoration(cell.deco.flags, cell.deco.fg, cell.deco.bg);
}

pub Byte
drawGetLineWrap(int row) {
   return lineWrapsP[row];
}

pub ColNr
drawGetScreenCol(int offset) {
   return screenColS[offset];
}

pub ColNr
drawGetOffset(int row) {
   return lineStartsP[row];
}

pub Boole
drawHasLines() {
   return screenTextP != null;
}

pub Byte
drawGetLine(int offset) {
   return screenTextP[offset];
}

pub CS
drawGetLinesWithOffset(Unt row) {
   return screenTextP + lineStartsP[row];
}

pub Unt
drawGetScreenComposingChar(int offset, Unt composeInd) {
   return screenLinesCG[MAX_COMBINED_SYMBOLS * offset + composeInd];
}

pub Unt
drawGetScreenUnicodeChar(int offset) {
   return screenLinesUCG[offset];
}

//}}}
