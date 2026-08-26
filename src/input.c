//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## getchar.c: Code related to getting a character from the user or scripts, redo & stuff buffers

#include "eegl.h"

#include <wchar.h>

// These buffers are used for storing:
// - stuffed characters: A command that is translated into another command.
// - redo characters: will redo the last change.
// - recorded characters: for the "q" command.
//
// The bytes are stored like in the typeahead buffer:
// - K_SPECIAL introduces a special key (two more bytes follow). A literal K_SPECIAL is stored as 
// K_SPECIAL KS_SPECIAL KE_FILLER.
//   A literal CSI is stored as CSI KS_EXTRA KE_CSI.
// These translations are also done on multi-byte characters!
//
// Escaping CSI bytes is done by the system-specific input functions, called by ui_inchar().
// Escaping K_SPECIAL is done by ingestChar(). Un-escaping is done by vgetc().

#define MINIMAL_SIZE 20         // minimal size for b_str

private TextHeader redobuff = {{NULL, 0, {ZERO}}, NULL, 0, 0, false};
private TextHeader old_redobuff = {{NULL, 0, {ZERO}}, NULL, 0, 0, false};
private TextHeader recordbuff = {{NULL, 0, {ZERO}}, NULL, 0, 0, false};

private int TYPEAHEAD_CHAR = 0;      // typeahead char that's not flushed
private Byte typedchars[MAXMAPLEN + 1] = { ZERO };  // typed chars before map
private int typedchars_pos = 0;

private Boole isMouseBelowBottomLineS INIT(= false);// mouse below last line
private Boole isMouseRightOfEolS INIT(= false);         // mouse right of line

//When block_redo is true the redo buffer will not be changed. Used by edit() to repeat insertions.
private int block_redo = false;

private int keyNoremapG = 0;       // remapping flags

// Variables used by vGetOrPeek() and flush_buffers().
//
// [     mappedLen    | - -unmapped- -]
// [ -invalid- | validLen | ZERO ]
//             ^
//             currPos 
// typeBufG.c[] contains all characters that are not consumed yet.
// typeBufG.c[typeBufG.currPos] is the first valid character.
// typeBufG.c[typeBufG.currPos + typeBufG.validLen - 1] is the last valid char.
// typeBufG.c[typeBufG.currPos + typeBufG.validLen] must be ZERO.
// The head of the book may contain the result of mappings, abbreviations and @a commands. The 
// length of this part is typeBufG.mappedLen.
// typeBufG.silentCnt is the part where <silent> applies.
// After the head are characters that come from the terminal.
// typeBufG.noAbbrCnt is the number of characters in typeBufG.c that
// should not be considered for abbreviations.
// Some parts of typeBufG.c must be processed without custom mappings. These parts are remembered
// in typeBufG.noremap[], which is the same length as typeBufG.c[] and
// contains RM_NONE for the characters that are not to be remapped.
// typeBufG.noremap[typeBufG.currPos] is the first valid flag.
// (typeBufG has been put in globals.h, because termTryParseTermcode() needs it).
#define RM_YES      0   // noremap: remap
#define RM_NONE     1   // noremap: don't remap
#define RM_SCRIPT   2   // noremap: remap local script mappings
#define RM_ABBR     4   // noremap: don't remap, do abbrev.

// typeBufG.c has three parts: room in front (for result of mappings), the
// middle for typeahead and room for new characters.
#define TYPELEN_INIT   (5 * (MAXMAPLEN + 3))
private Byte typeBufG_init[TYPELEN_INIT];   // initial typeBufG.c
private Byte noremapbuf_init[TYPELEN_INIT];   // initial typeBufG.noremap

private Unt lastRecordedLen = 0;   // number of last recorded chars

private MapBlock* last_used_map = NULL;
private int last_used_sid = -1;

//{{{@@forward declarations
private void freeBuffer(TextHeader* buf);
private CS get_buffcont(
   TextHeader* buffer,
   Boole dozero,       // count == zero is not an error
   OUT Unt* len       // the length of the returned buffer
);
private int appendDigitInt(int *value, int digit);
private void add_buff(
   TextHeader* buf,
   CS s,
   long slen
);
private void deleteBufferTail(TextHeader *buf, int slen);
private void addNumToBuf(TextHeader *buf, long n);
private void addCharToBuf(TextHeader *buf, Unt c);
private int read_readbuffers(int advance);
private int read_readbuf(TextHeader* buf, int advance);
private void start_stuff(void);
private int read_redo(int init, int old_redo);
private void copy_redo(int old_redo);
private void initTypebuf(void);
private int gotchars_add_byte(GotCharsState *state, Byte byte);
private void gotchars(CS chars, int len);
private void maySyncUndo(void);
private void reallocateTypebuf(void);
private void free_typeBufG(void);
private int  can_get_old_char(void);
private void closeScript(void);
private void updateScript(int c);
private void addByteToShowcmd(Byte byte);
private int plainVgetcNopaste(void);
private void getcharCommon(Arr(Var) argvars, Var* returnVar, Boole allow_number);
private int atAnInsertCompletionKey(void);
private int checkSimplifyModifier(int const maxOffset);
private MatchFinding searchForPartialMappings(int foundKeylen, int timedout, Boole isAbstractPlugMapping);
private MapResult handleMapping(OUT int* foundKeylen, int timedout, OUT int* mapdepth);
private void check_end_reg_executing(int advance);
private Unt vGetOrPeek(Boole advance);
private int ingestChar(CS buf, int maxlen, long wait_time);
private int fixInputBuffer(OUT CS buf, int len);
private CS getCommandNameCb(
   Unt promptc UNUSED,
   void* cookie UNUSED,
   int indent UNUSED,
   GetlineAlgo do_concat UNUSED
);
private long time_diff_ms(TimeVal *t1, TimeVal *t2);
private int get_mouse_class(CS p);
private void find_start_of_word(Pos*pos);
private void find_end_of_word(Pos* pos);
private void do_mousescroll(ActionArg* cap);
private int get_pseudo_mouse_code(Unt button, Boole is_click, Boole is_drag);
private int do_mousescroll_horiz(Ulong leftcol);
//}}}
//{{{keyboard input

#define TTYM_SGR      0x80

// Free and clear a buffer.
private void
freeBuffer(TextHeader* buf) {
   TextChunk* np;
   for (TextChunk* p = buf->first.next; p != NULL; p = np) {
      np = p->next;
      eeglFree(p);
   }
   buf->first.next = NULL;
   buf->bh_curr = NULL;
}

// Return the contents of a buffer as a single string.
// K_SPECIAL and CSI in the returned string are escaped.
private CS
get_buffcont(
   TextHeader* buffer,
   Boole dozero,       // count == zero is not an error
   OUT Unt* len       // the length of the returned buffer
){
   Ulong count = 0;
   CS p = NULL;
   CS p2;
   CS str;
   Unt i = 0;

   // compute the total length of the string
   for (TextChunk* bp = buffer->first.next; bp != NULL; bp = bp->next)
      count += (Ulong)bp->b_strlen;

   if ((count > 0 || dozero) && (p = alloc(count + 1)) != NULL) {
         p2 = p;
         for (TextChunk* bp = buffer->first.next; bp != NULL; bp = bp->next) {
             for (str = bp->b_str; *str; p2++, str++) {
                *p2 = *str;
             }
         } 
         *p2 = ZERO;
         i = (Unt)(p2 - p);
   }

   if (len)
      *len = i;

   return p;
}

// Return the contents of the record buffer as a single string and clear the record buffer.
// K_SPECIAL and CSI in the returned string are escaped.
pub CS
get_recorded(void) {
   Unt len;
   CS p = get_buffcont(&recordbuff, true, OUT &len);
   if (!p)
      return NULL;

   freeBuffer(&recordbuff);

   //Remove the characters that were added the last time, these must be the
   //(possibly mapped) characters that stopped the recording.
   if (len >= lastRecordedLen) {
      len -= lastRecordedLen;
      p[len] = ZERO;
   }

   // When stopping recording from Insert mode with CTRL-O q, also remove the CTRL-O.
   if (len > 0 && restart_edit != 0 && p[len - 1] == Ctrl_O)
      p[len - 1] = ZERO;

   return (p);
}

// Return the contents of the redo buffer as a single string.
// K_SPECIAL and CSI in the returned string are escaped.
pub Text
get_inserted(void) {
   Unt len = 0;
   CS str = get_buffcont(&redobuff, false, &len);
   Text ret = { str, len };
   return ret;
}

//Get a key stroke directly from the user. Ignore mouse clicks and scrollbar events, except a 
//click for the left button (used at the more prompt). Don't use vgetc(), because it syncs undo 
//and eats mapped characters. Disadvantage: typeahead is ignored.
//Translate the interrupt character for Unix to ESC.
pub Unt
get_keystroke(void) {
   CS buf = S"";
   Unt buflen = 150;
   int maxlen;
   int len = 0;
   int count;
   Unt c;
   int save_mapped_ctrl_c = mapped_ctrl_c;
   int waited = 0;

   mapped_ctrl_c = false;   // mappings are not used here
   for (;;) {
      cursor_on();
      out_flush();

      // Leave some room for termTryParseTermcode() to insert a key code into (max
      // 5 chars plus ZERO).  And fixInputBuffer() can triple the number of bytes.
      maxlen = (buflen - 6 - len) / 3;
      if (!buf)
         buf = alloc(buflen);
      ei (maxlen < 10) {
         // Need some more space. This might happen when receiving a long escape sequence.
         buflen += 100;
         buf = eeRealloc(buf, buflen);
         maxlen = (buflen - 6 - len) / 3;
      }
      if (!buf) {
         do_outofmem_msg((Ulong)buflen);
         return ESC;  // panic!
      }

      // First time: blocking wait.  Second time: wait up to 100ms for a terminal code to complete.
      count = ui_inchar(buf + len, maxlen, len == 0 ? -1L : 100L, 0);
      if (count > 0) {
         // Replace zero and CSI by a special key code.
         count = fixInputBuffer(buf + len, count);
         len += count;
         waited = 0;
      } ei (len > 0)
          ++waited;       // keep track of the waiting time

      // Incomplete termcode and not timed out yet: get more characters
      if ((count = termTryParseTermcode(1, OUT (Text){buf, buflen}, OUT &len)) < 0
             && (!p_ttimeout || waited * 100L < (p_ttm < 0 ? p_tm : p_ttm))
      )
         continue;

      if (count == KEYLEN_REMOVED) { // key code removed
         if (mustRedrawG != 0 && !need_wait_return 
               && (stateG & (MODE_COMMLINE | MODE_HITRETURN | MODE_ASKMORE)) == 0
         ) {
            // Redrawing was postponed, do it now.
            drawUpdateScreen(0);
            setcursor(); // put cursor back where it belongs
         }
         continue;
      }
      if (count > 0)      // found a termcode: adjust length
         len = count;
      if (len == 0)      // nothing typed yet
         continue;

      // Handle modifier and/or special key code.
      c = buf[0];
      if (c == K_SPECIAL) {
         c = TO_SPECIAL(buf[1], buf[2]);
         if (buf[1] == KS_MODIFIER
             || c == K_IGNORE
             || (is_mouse_key(c) && c != K_LEFTMOUSE)
         ) {
            if (buf[1] == KS_MODIFIER)
               modMaskG = buf[2];
            len -= 3;
            if (len > 0)
               MEMMOVE(buf, buf + 3, (Unt)len);
            continue;
          }
          break;
      }
      if (utf8CharLens[c] > len)
         continue;   // more bytes to get
      buf[len >= (int)buflen ? (int)buflen - 1 : len] = ZERO;
      c = mb_ptr2char(buf);
      if (c == extraInterruptCharG)
          c = ESC;
      break;
   }
   eeglFree(buf);

   mapped_ctrl_c = save_mapped_ctrl_c;
   return c;
}

// For overflow detection, add a digit safely to an int value.
private int
appendDigitInt(int *value, int digit) {
   int x = *value;
   if (x > ((INT_MAX - digit) / 10))
      return FAIL;
   *value = x * 10 + digit;
   return OK;
}

//Get a number from the user. When "mouse_used" is not NULL allow using the mouse.
pub int
get_number(Boole allowColonToUpdate, int* mouse_used) {
   int   n = 0;
   Unt   c;
   int typed = 0;

   if (mouse_used)
      *mouse_used = false;

   // When not printing messages, the user won't know what to type, return a
   // zero (as if CR was hit).
   if (msg_silent != 0)
      return 0;

   ++no_mapping;
   ++allow_keys;      // no mapping here, but recognize keys
   for (;;) {
      windgoto(msgRowG, msgColG);
      c = safe_vgetc();
      if (EE_ISDIGIT(c)) {
         if (appendDigitInt(&n, c - '0') == FAIL)
            return 0;
         msg_putchar(c);
         ++typed;
      } ei (c == K_DEL || c == K_KDEL || c == K_BS || c == Ctrl_H) {
         if (typed > 0) {
            msg_puts((CS)"\b \b");
            --typed;
         }
         n /= 10;
      } ei (mouse_used != NULL && c == K_LEFTMOUSE) {
         *mouse_used = true;
         n = mouseRowG + 1;
         break;
      } ei (n == 0 && c == ':' && allowColonToUpdate) {
         stuffcharReadbuff(':');
         commlineRowG = msgRowG;
         skip_redraw = true;       // skip redraw once
         do_redraw = false;
         break;
      } ei (c == Ctrl_C || c == ESC || c == 'q') {
         n = 0;
         break;
      } ei (c == ENTER || c == NL )
         break;
   }
   --no_mapping;
   --allow_keys;
   return n;
}


// Add string "s" after the current block of buffer "buf".
// K_SPECIAL and CSI should have been escaped already.
private void
add_buff(
   TextHeader* buf,
   CS s,
   long slen
) {  // length of "s" or -1
   if (slen < 0)
      slen = (long)STRLEN(s);
   if (slen == 0)            // don't add empty strings
      return;

   if (buf->first.next == NULL) {  // first add to list
      buf->bh_curr = &(buf->first);
      buf->bh_create_newblock = true;
   } ei (buf->bh_curr == NULL) {  // buffer has already been read
      internalErrMsg(e_add_to_internal_buffer_that_was_already_read_from);
      return;
   } ei (buf->bh_index != 0) {
      MEMMOVE(buf->first.next->b_str,
         buf->first.next->b_str + buf->bh_index,
         (buf->first.next->b_strlen - buf->bh_index) + 1);
      buf->first.next->b_strlen -= buf->bh_index;
      buf->bh_space += buf->bh_index;
   }
   buf->bh_index = 0;

   if (!buf->bh_create_newblock && buf->bh_space >= (int)slen) {
      copySubstrToAllocation(buf->bh_curr->b_str + buf->bh_curr->b_strlen, (Text){s, (Unt)slen});
      buf->bh_curr->b_strlen += slen;
      buf->bh_space -= slen;
   } else {
      Ulong       len;
      TextChunk *p;

      if (slen < MINIMAL_SIZE)
         len = MINIMAL_SIZE;
      else
         len = slen;
      p = alloc(offsetof(TextChunk, b_str) + len + 1);
      copySubstrToAllocation(p->b_str, (Text){s, (Unt)slen});
      p->b_strlen = slen;
      buf->bh_space = (int)(len - slen);
      buf->bh_create_newblock = false;

      p->next = buf->bh_curr->next;
      buf->bh_curr->next = p;
      buf->bh_curr = p;
   }
}

// Delete "slen" bytes from the end of "buf". Only works when it was just added.
private void
deleteBufferTail(TextHeader *buf, int slen) {
   if (buf->bh_curr == NULL)
      return;  // nothing to delete
   if (buf->bh_curr->b_strlen < (Unt)slen)
      return;

   buf->bh_curr->b_str[buf->bh_curr->b_strlen - (Unt)slen] = ZERO;
   buf->bh_curr->b_strlen -= slen;
   buf->bh_space += slen;
}

// Add number "n" to buffer "buf".
private void
addNumToBuf(TextHeader *buf, long n) {
   Byte number[32];

   int numberlen = eeSnprintf(number, sizeof(number), "%ld", n);
   add_buff(buf, number, (long)numberlen);
}

// Add character 'c' to buffer "buf".
// Translate special keys, ZERO, CSI, K_SPECIAL and multibyte characters.
private void
addCharToBuf(TextHeader *buf, Unt c) {
   Byte bytes[MB_MAXBYTES + 1];
   Byte temp[4];
   long templen;

   int len =  (IS_SPECIAL(c)) ? 1 : mb_char2bytes(c, bytes);
   for (int i = 0; i < len; ++i) {
      if (!IS_SPECIAL(c))
         c = bytes[i];

      if (IS_SPECIAL(c) || c == K_SPECIAL || c == ZERO) {
         // translate special key code into three byte sequence
         temp[0] = K_SPECIAL;
         temp[1] = K_SECOND(c);
         temp[2] = K_THIRD(c);
         temp[3] = ZERO;
         templen = 3;
      } else {
         temp[0] = c;
         temp[1] = ZERO;
         templen = 1;
      }
      add_buff(buf, temp, templen);
   }
}

// First read ahead buffer. Used for translated commands.
private TextHeader readbuf1 = {{NULL, 0, {ZERO}}, NULL, 0, 0, false};

// Second read ahead buffer. Used for redo.
private TextHeader readbuf2 = {{NULL, 0, {ZERO}}, NULL, 0, 0, false};

// Get one byte from the read buffers.  Use readbuf1 one first, use readbuf2
// if that one is empty. If advance == true, go to the next char.
// No translation is done K_SPECIAL and CSI are escaped.
private int
read_readbuffers(int advance) {
   Unt c = read_readbuf(&readbuf1, advance);
   if (c == ZERO)
      c = read_readbuf(&readbuf2, advance);
   return c;
}

private int
read_readbuf(TextHeader* buf, int advance) {
   if (buf->first.next == NULL)  // buffer is empty
      return ZERO;

   TextChunk* curr = buf->first.next;
   Byte c = curr->b_str[buf->bh_index];

   if (advance) {
      if (curr->b_str[++buf->bh_index] == ZERO) {
         buf->first.next = curr->next;
         eeglFree(curr);
         buf->bh_index = 0;
      }
   }
   return c;
}

// Prepare the read buffers for reading (if they contain something).
private void
start_stuff(void) {
   if (readbuf1.first.next != NULL) {
      readbuf1.bh_curr = &(readbuf1.first);
      readbuf1.bh_create_newblock = true;   // force a new block to be created (see add_buff())
   }
   if (readbuf2.first.next != NULL) {
      readbuf2.bh_curr = &(readbuf2.first);
      readbuf2.bh_create_newblock = true;   // force a new block to be created (see add_buff())
   }
}

// Return true if the stuff buffer is empty.
pub int
stuff_empty(void) {
   return (readbuf1.first.next == NULL && readbuf2.first.next == NULL);
}

// Return true if readbuf1 is empty.  There may still be redo characters in redbuf2.
pub int
readbuf1_empty(void) {
   return (readbuf1.first.next == NULL);
}

// Set a typeahead character that won't be flushed.
pub void
typeahead_noflush(int c) {
   TYPEAHEAD_CHAR = c;
}

// Argument for flush_buffers().
pub typedef enum {
   FLUSH_MINIMAL,
   FLUSH_TYPEAHEAD,   // flush current typebuf contents
   FLUSH_INPUT      // flush typebuf and inchar() input
} FlushBuffers;

//Remove the contents of the stuff buffer and the mapped characters in the
//typeahead buffer (used in case of an error). If "flush_typeahead" is true,
//flush all typeahead characters (used when interrupted by a CTRL-C).
pub void
flush_buffers(FlushBuffers flush_typeahead) {
   initTypebuf();

   start_stuff();
   while (read_readbuffers(true) != ZERO)
      {}

   if (flush_typeahead == FLUSH_MINIMAL) {
      // remove mapped characters at the start only, but only when enough space left in typeBufG
      if (typeBufG.currPos + typeBufG.mappedLen >= typeBufG.len) {
         typeBufG.currPos = MAXMAPLEN;
         typeBufG.validLen = 0;
      } else {
         typeBufG.currPos += typeBufG.mappedLen;
         typeBufG.validLen -= typeBufG.mappedLen;
      }
      if (typeBufG.validLen == 0)
         typebuf_was_filled = false;
   } else {
      // remove typeahead
      if (flush_typeahead == FLUSH_INPUT) {
          //We have to get all characters because we may delete the first part of an escape 
          //sequence. In an xterm we get one char at a time and we have to get them all.
          while (ingestChar(typeBufG.c, typeBufG.len - 1, 10L) != 0)
             {}
      }
      typeBufG.currPos = MAXMAPLEN;
      typeBufG.validLen = 0;
      // Reset the flag that text received from a client or from feedkeys()
      // was inserted in the typeahead buffer.
      typebuf_was_filled = false;
   }
   typeBufG.mappedLen = 0;
   typeBufG.silentCnt = 0;
   cmd_silent = false;
   typeBufG.noAbbrCnt = 0;
   if (++typeBufG.changeCnt == 0)
      typeBufG.changeCnt = 1;
}

// The previous contents of the redo buffer is kept in old_redobuffer.
// This is used for the CTRL-O <.> command in insert mode.
pub void
ResetRedobuff(void) {
   if (block_redo)
      return;

   freeBuffer(&old_redobuff);
   old_redobuff = redobuff;
   redobuff.first.next = NULL;
}

// Discard the contents of the redo buffer and restore the previous redo buffer.
pub void
CancelRedo(void) {
   if (block_redo)
      return;

   freeBuffer(&redobuff);
   redobuff = old_redobuff;
   old_redobuff.first.next = NULL;
   start_stuff();
   while (read_readbuffers(true) != ZERO)
      {}
}

// Save redobuff and old_redobuff to save_redobuff and save_old_redobuff.
// Used before executing autocommands and user functions.
pub void
saveRedobuff(SaveRedo* save_redo) {
   save_redo->sr_redobuff = redobuff;
   redobuff.first.next = NULL;
   save_redo->sr_old_redobuff = old_redobuff;
   old_redobuff.first.next = NULL;

   // Make a copy, so that ":normal ." in a function works.
   Unt slen;
   CS s = get_buffcont(&save_redo->sr_redobuff, false, &slen);
   if (s == NULL)
      return;

   add_buff(&redobuff, s, (long)slen);
   eeglFree(s);
}

// Restore redobuff and old_redobuff from save_redobuff and save_old_redobuff.
// Used after executing autocommands and user functions.
pub void
restoreRedobuff(SaveRedo* save_redo) {
   freeBuffer(&redobuff);
   redobuff = save_redo->sr_redobuff;
   freeBuffer(&old_redobuff);
   old_redobuff = save_redo->sr_old_redobuff;
}

// Append "s" to the redo buffer. K_SPECIAL and CSI should already have been escaped.
pub void
AppendToRedobuff(CS s) {
   if (!block_redo)
      add_buff(&redobuff, s, -1L);
}

// Append to Redo buffer literally, escaping special characters with CTRL-V.
// K_SPECIAL and CSI are escaped as well.
pub void
AppendToRedobuffLit(CS str, int len) {      // "len" = length of "str" or -1 for up to the ZERO
   if (block_redo)
      return;
      
   CS s = str;
   CS start;
   while (len < 0 ? *s != ZERO : s - str < len) {
      // Put a string of normal characters in the redo buffer (that's faster).
      start = s;
      while (*s >= ' ' && *s < DEL && (len < 0 || s - str < len))
         ++s;

      // Don't put '0' or '^' as last character, just in case a CTRL-D is typed next.
      if (*s == ZERO && (s[-1] == '0' || s[-1] == '^'))
         --s;
      if (s > start)
         add_buff(&redobuff, start, (long)(s - start));

      if (*s == ZERO || (len >= 0 && s - str >= len))
         break;

      // Handle a special or multibyte character. Handle composing chars separately.
      Unt c = mb_cptr2char_adv(&s);
      if (c < ' ' || c == DEL || (*s == ZERO && (c == '0' || c == '^')))
         addCharToBuf(&redobuff, Ctrl_V);

      // CTRL-V '0' must be inserted as CTRL-V 048
      if (*s == ZERO && c == '0')
         add_buff(&redobuff, (CS)"048", 3L);
      else
         addCharToBuf(&redobuff, c);
   }
}

// Append "s" to the redo buffer, leaving 3-byte special key codes unmodified
// and escaping other K_SPECIAL and CSI bytes.
pub void
AppendToRedobuffSpec(CS s) {
   if (block_redo)
      return;

   while (*s != ZERO) {
      if (*s == K_SPECIAL && s[1] != ZERO && s[2] != ZERO) {
         // Insert special key literally.
         add_buff(&redobuff, s, 3L);
         s += 3;
      } else
         addCharToBuf(&redobuff, mb_cptr2char_adv(&s));
   }
}

// Append a character to the redo buffer.
// Translates special keys, ZERO, CSI, K_SPECIAL and multibyte characters.
pub void
AppendCharToRedobuff(Unt c) {
   if (!block_redo)
      addCharToBuf(&redobuff, c);
}

// Append a number to the redo buffer.
pub void
inpAppendNumberToRedoBuff(long n) {
   if (!block_redo)
      addNumToBuf(&redobuff, n);
}

// Append string "s" to the stuff buffer. CSI and K_SPECIAL must already have been escaped.
pub void
stuffReadbuff(CS s) {
   add_buff(&readbuf1, s, -1L);
}

// Append string "s" to the redo stuff buffer.
// CSI and K_SPECIAL must already have been escaped.
pub void
stuffRedoReadbuff(CS s) {
   add_buff(&readbuf2, s, -1L);
}

pub void
stuffReadbuffLen(CS s, long len) {
   add_buff(&readbuf1, s, len);
}

// Stuff "s" into the stuff buffer, leaving special key codes unmodified and
// escaping other K_SPECIAL and CSI bytes. Change CR, LF and ESC into a space.
pub void
stuffReadbuffSpec(CS s) {
   while (*s != ZERO) {
      if (*s == K_SPECIAL && s[1] != ZERO && s[2] != ZERO) {
      // Insert special key literally.
      stuffReadbuffLen(s, 3L);
      s += 3;
      } else {
         int c = mb_cptr2char_adv(&s);
            if (c == ENTER || c == NL || c == ESC)
               c = ' ';
         stuffcharReadbuff(c);
      }
   }
}

// Append a character to the stuff buffer.
// Translates special keys, ZERO, CSI, K_SPECIAL and multibyte characters.
pub void
stuffcharReadbuff(Unt c) {
   addCharToBuf(&readbuf1, c);
}

// Append a number to the stuff buffer.
pub void
stuffnumReadbuff(long n) {
   addNumToBuf(&readbuf1, n);
}

// Stuff a string into the typeahead buffer, such that edit() will insert it
// literally ("literally" true) or interpret is as typed characters.
pub void
stuffescaped(CS arg, int literally) {
    int      c;
    CS start;

    while (*arg != ZERO) {
       // Stuff a sequence of normal ASCII characters, that's fast.  Also
       // stuff K_SPECIAL to get the effect of a special key when "literally" is true.
       start = arg;
       while ((*arg >= ' ' && *arg < DEL) || (*arg == K_SPECIAL && !literally)) {
          ++arg;
       }
       if (arg > start)
          stuffReadbuffLen(start, (long)(arg - start));

       // stuff a single special character
       if (*arg != ZERO) {
          c = mb_cptr2char_adv(&arg);
          if (literally && ((c < ' ' && c != TAB) || c == DEL))
             stuffcharReadbuff(Ctrl_V);
          stuffcharReadbuff(c);
      }
   }
}

// Read a character from the redo buffer. Translate K_SPECIAL, CSI and multibyte characters.
// The redo buffer is left as it is.
// If init is true, prepare for redo, return FAIL if nothing to redo, OK otherwise.
// If old is true, use old_redobuff instead of redobuff.
private int
read_redo(int init, int old_redo) {
   static TextChunk* bp;
   static CS p;
   Unt c;
   int n;
   Byte buf[MB_MAXBYTES + 1];
   int i;

   if (init) {
      if (old_redo)
         bp = old_redobuff.first.next;
      else
         bp = redobuff.first.next;
      if (bp == NULL)
         return FAIL;
      p = bp->b_str;
      return OK;
   }
   if ((c = *p) != ZERO) {
      // Reverse the conversion done by addCharToBuf()
      // For a multi-byte character get all the bytes and return the converted character.
      if ((c != K_SPECIAL || p[1] == KS_SPECIAL))
          n = MB_BYTE2LEN_CHECK(c);
      else
          n = 1;
      for (i = 0; ; ++i) {
         if (c == K_SPECIAL) { // special key or escaped K_SPECIAL
            c = TO_SPECIAL(p[1], p[2]);
            p += 2;
         }
         if (*++p == ZERO && bp->next != NULL) {
            bp = bp->next;
            p = bp->b_str;
         }
         buf[i] = c;
         if (i == n - 1) {  // last byte of a character
            if (n != 1)
                c = mb_ptr2char(buf);
            break;
         }
         c = *p;
         if (c == ZERO)   // cannot happen?
            break;
      }
   }

   return c;
}

// Copy the rest of the redo buffer into the stuff buffer (in a slow way).
// If old_redo is true, use old_redobuff instead of redobuff.
// The escaped K_SPECIAL and CSI are copied without translation.
private void
copy_redo(int old_redo) {
   int       c;
   while ((c = read_redo(false, old_redo)) != ZERO)
      addCharToBuf(&readbuf2, c);
}

// Stuff the redo buffer into readbuf2. Insert the redo count into the command.
// If "old_redo" is true, the last but one command is repeated
// instead of the last command (inserting text). This is used for
// CTRL-O <.> in insert mode
//
// return FAIL for failure, OK otherwise
pub int
start_redo(long count, int old_redo) {
   // init the pointers; return if nothing to redo
   if (read_redo(true, old_redo) == FAIL)
      return FAIL;

   Unt c = read_redo(false, old_redo);

   if (c == K_SID) {
      // Copy the <SID>{sid}; sequence
      addCharToBuf(&readbuf2, c);
      for (;;) {
         c = read_redo(false, old_redo);
         addCharToBuf(&readbuf2, c);
         if (!SAFE_isdigit(c))
            break;
      }
      c = read_redo(false, old_redo);
   }

   // copy the buffer name, if present
   if (c == '"') {
      add_buff(&readbuf2, (CS)"\"", 1L);
      c = read_redo(false, old_redo);

      // if a numbered buffer is used, increment the number
      if (c >= '1' && c < '9')
          ++c;
      addCharToBuf(&readbuf2, c);

      // the expression register should be re-evaluated
      if (c == '=') {
          addCharToBuf(&readbuf2, ENTER);
          cmd_silent = true;
      }

      c = read_redo(false, old_redo);
   }

   if (c == 'v') {  // redo Visual
      VIsual = curPor->cursor;
      VIsual_active = true;
      VIsual_reselect = true;
      isRedoVisualBusy = true;
      c = read_redo(false, old_redo);
   }

   // try to enter the count (in place of a previous count)
   if (count) {
      while (EE_ISDIGIT(c))   // skip "old" count
         c = read_redo(false, old_redo);
      addNumToBuf(&readbuf2, count);
   }

   // copy the rest from the redo buffer into the stuff buffer
   addCharToBuf(&readbuf2, c);
   copy_redo(old_redo);
   return OK;
}

// Repeat the last insert (R, o, O, a, A, i or I command) by stuffing
// the redo buffer into readbuf2.
// return FAIL for failure, OK otherwise
pub int
start_redo_ins(void) {
   if (read_redo(true, false) == FAIL)
      return FAIL;
   start_stuff();

   int       c;
   // skip the count and the command character
   while ((c = read_redo(false, false)) != ZERO) {
      if (firstOccurrence((CS)"AaIiRrOo", c) != NULL) {
         if (c == 'O' || c == 'o')
            add_buff(&readbuf2, NL_STR, -1L);
         break;
      }
   }

   // copy the typed text from the redo buffer into the stuff buffer
   copy_redo(false);
   block_redo = true;
   return OK;
}

pub void
stop_redo_ins(void) {
   block_redo = false;
}

//Initialize typeBufG.c to point to typeBufG_init.
//alloc() cannot be used here: In out-of-memory situations it would be impossible to type anything.
private void
initTypebuf(void) {
    if (typeBufG.c)
       return;

    typeBufG.c = typeBufG_init;
    typeBufG.noremap = noremapbuf_init;
    typeBufG.len = TYPELEN_INIT;
    typeBufG.validLen = 0;
    typeBufG.currPos = MAXMAPLEN + 4;
    typeBufG.changeCnt = 1;
}

// true when keys cannot be remapped.
pub int
noremap_keys(void) {
   return keyNoremapG & (RM_NONE|RM_SCRIPT);
}

// Insert a string in position 'offset' in the typeahead buffer (for "@r"
// and ":normal" command, vGetOrPeek() and termTryParseTermcode()).
//
// If "noremap" is REMAP_YES, new string can be mapped again.
// If "noremap" is REMAP_NONE, new string cannot be mapped again.
// If "noremap" is REMAP_SKIP, first char of new string cannot be mapped again,
// but abbreviations are allowed.
// If "noremap" is REMAP_SCRIPT, new string cannot be mapped again, except for
//         script-local mappings.
// If "noremap" is > 0, that many characters of the new string cannot be mapped.
//
// If "nottyped" is true, the string does not return keyWasTypedG (don't use when
// "offset" is non-zero!).
//
// If "silent" is true, cmd_silent is set when the characters are obtained.
//
// return FAIL for failure, OK otherwise
pub int
insertIntoTypebuf(
   CS str,
   Unt noremap,
   int offset,
   Boole nottyped,
   Boole silent
) {
   Byte   *s1, *s2;
   int      newlen;
   int      addlen;
   int      i;
   int      newoff;
   int      val;
   int      nrm;

   initTypebuf();
   if (++typeBufG.changeCnt == 0)
      typeBufG.changeCnt = 1;
   state_no_longer_safe(S"insertIntoTypebuf()");

   addlen = (int)STRLEN(str);

   if (offset == 0 && addlen <= typeBufG.currPos) {
      //Easy case: there is room in front of typeBufG.c[typeBufG.currPos]
      typeBufG.currPos -= addlen;
      MEMMOVE(typeBufG.c + typeBufG.currPos, str, (Unt)addlen);
   } ei (typeBufG.validLen == 0 && typeBufG.len >= addlen + 3 * (MAXMAPLEN + 4)) {
      //Book is empty and string fits in the existing buffer.
      //Leave some space before and after, if possible.
      typeBufG.currPos = (typeBufG.len - addlen - 3 * (MAXMAPLEN + 4)) / 2;
      MEMMOVE(typeBufG.c + typeBufG.currPos, str, (Unt)addlen);
   } else {
      int extra;

      //Need to allocate a new buffer. In typeBufG.c there must always be room for 
      //3 * (MAXMAPLEN + 4) characters.  We add some extra room to avoid having to allocate 
      //too often.
      newoff = MAXMAPLEN + 4;
      extra = addlen + newoff + 4 * (MAXMAPLEN + 4);
      if (typeBufG.validLen > 2147483647 - extra) {
         // string is getting too long for a 32 bit int
         emsg(_(e_command_too_complex));    // also calls flush_buffers
         setcursor();
         return FAIL;
      }
      newlen = typeBufG.validLen + extra;
      s1 = alloc(newlen);
      s2 = alloc(newlen);
      typeBufG.len = newlen;

      // copy the old chars, before the insertion point
      MEMMOVE(s1 + newoff, typeBufG.c + typeBufG.currPos, (Unt)offset);
      // copy the new chars
      MEMMOVE(s1 + newoff + offset, str, (Unt)addlen);
      // copy the old chars, after the insertion point, including the ZERO at the end
      MEMMOVE(s1 + newoff + offset + addlen,
                    typeBufG.c + typeBufG.currPos + offset,
                      (Unt)(typeBufG.validLen - offset + 1));
      if (typeBufG.c != typeBufG_init)
         eeglFree(typeBufG.c);
      typeBufG.c = s1;

      MEMMOVE(s2 + newoff, typeBufG.noremap + typeBufG.currPos, (Unt)offset);
      MEMMOVE(
         s2 + newoff + offset + addlen, typeBufG.noremap + typeBufG.currPos + offset,
         (Unt)(typeBufG.validLen - offset)
      );
      if (typeBufG.noremap != noremapbuf_init)
         eeglFree(typeBufG.noremap);
      typeBufG.noremap = s2;

      typeBufG.currPos = newoff;
   }
   typeBufG.validLen += addlen;

   // If noremap == REMAP_SCRIPT: do remap script-local mappings.
   if (noremap == REMAP_SCRIPT)
      val = RM_SCRIPT;
   ei (noremap == REMAP_SKIP)
      val = RM_ABBR;
   else
      val = RM_NONE;

   //Adjust typeBufG.noremap[] for the new characters:
   //If noremap == REMAP_NONE or REMAP_SCRIPT: new characters are (sometimes) not remappable
   //If noremap == REMAP_YES: all the new characters are mappable
   //If noremap  > 0: "noremap" characters are not remappable, the rest mappable
   if (noremap == REMAP_SKIP)
      nrm = 1;
   ei (noremap >= UNT_NEG)
      nrm = addlen;
   else
      nrm = noremap;
   for (i = 0; i < addlen; ++i) {
      typeBufG.noremap[typeBufG.currPos + i + offset] = (--nrm >= 0) ? val : RM_YES;
   }

   // mappedLen and silentCnt only remember the length of mapped and/or
   // silent mappings at the start of the buffer, assuming that a mapped
   // sequence doesn't result in typed characters.
   if (nottyped || typeBufG.mappedLen > offset)
      typeBufG.mappedLen += addlen;
   if (silent || typeBufG.silentCnt > offset) {
      typeBufG.silentCnt += addlen;
      cmd_silent = true;
   }
   if (typeBufG.noAbbrCnt && offset == 0)   // and not used for abbrev.s
      typeBufG.noAbbrCnt += addlen;

   return OK;
}

// Put character "c" back into the typeahead buffer. Can be used for a character obtained by 
// vgetc() that needs to be put back. Use cmd_silent, keyWasTypedG and keyNoremapG to restore the 
// flags belonging to the char. Return the length of what was inserted.
pub int
ins_char_typebuf(int c, int modifiers){
   Byte   buf[MB_MAXBYTES * 3 + 4];
   int len = special_to_buf(c, modifiers, true, buf);

   buf[len] = ZERO;
   (void)insertIntoTypebuf(buf, keyNoremapG, 0, !keyWasTypedG, cmd_silent);
   return len;
}

//Return true if the typeahead buffer was changed (while waiting for a
//character to arrive).  Happens when a message was received from a client or from feedkeys().
//But check in a more generic way to avoid trouble: When "typeBufG.c"
//changed it was reallocated and the old pointer can no longer be used.
//Or "typeBufG.currPos" may have been changed and we would overwrite characters
//that was just added.
pub int
typebuf_changed(int changeCnt){   // old value of typeBufG.changeCnt
   return (changeCnt != 0 && (typeBufG.changeCnt != changeCnt || typebuf_was_filled));
}

// Return true if there are no untyped characters in the typeahead buffer
// (untyped = result from a mapping or come from ":normal").
pub int
typebuf_typed(void) {
   return typeBufG.mappedLen == 0;
}

// Return the number of characters that are mapped (or not typed).
pub int
typebuf_maplen(void) {
   return typeBufG.mappedLen;
}

// remove "len" characters from typeBufG.c[typeBufG.currPos + offset]
pub void
del_typebuf(int len, int offset) {
   int i;

   if (len == 0)
      return;      // nothing to do

   typeBufG.validLen -= len;

   // Easy case: Just increase typeBufG.currPos.
   if (offset == 0 && typeBufG.len - (typeBufG.currPos + len) >= 3 * MAXMAPLEN + 3)
      typeBufG.currPos += len;
   // Have to move the characters in typeBufG.c[] and typeBufG.noremap[]
   else {
      i = typeBufG.currPos + offset;
      // Leave some extra room at the end to avoid reallocation.
      if (typeBufG.currPos > MAXMAPLEN) {
         MEMMOVE(typeBufG.c + MAXMAPLEN, typeBufG.c + typeBufG.currPos, (Unt)offset);
         MEMMOVE(
            typeBufG.noremap + MAXMAPLEN, typeBufG.noremap + typeBufG.currPos, (Unt)offset
         );
         typeBufG.currPos = MAXMAPLEN;
      }
      // adjust typeBufG.c (include the ZERO at the end)
      MEMMOVE(
         typeBufG.c + typeBufG.currPos + offset, typeBufG.c + i + len, 
         (Unt)(typeBufG.validLen - offset + 1)
      );
      // adjust typeBufG.noremap[]
      MEMMOVE(
         typeBufG.noremap + typeBufG.currPos + offset, typeBufG.noremap + i + len, 
         (Unt)(typeBufG.validLen - offset)
      );
   }

   if (typeBufG.mappedLen > offset) {    // adjust mappedLen
      if (typeBufG.mappedLen < offset + len)
         typeBufG.mappedLen = offset;
      else
         typeBufG.mappedLen -= len;
   }
   if (typeBufG.silentCnt > offset) {     // adjust silentCnt
      if (typeBufG.silentCnt < offset + len)
         typeBufG.silentCnt = offset;
      else
         typeBufG.silentCnt -= len;
   }
   if (typeBufG.noAbbrCnt > offset) {  // adjust noAbbrCnt
      if (typeBufG.noAbbrCnt < offset + len)
         typeBufG.noAbbrCnt = offset;
      else
         typeBufG.noAbbrCnt -= len;
   }

   // Reset the flag that text received from a client or from feedkeys()
   // was inserted in the typeahead buffer.
   typebuf_was_filled = false;
   if (++typeBufG.changeCnt == 0)
      typeBufG.changeCnt = 1;
}

// stateG for adding bytes to a recording or 'showcmd'.
privateComp typedef struct {
   Byte   buf[MB_MAXBYTES * 3 + 4];
   int      prev_c;
   Unt   buflen;
   unsigned   pending_special;
   unsigned   pending_mbyte;
} GotCharsState;

// Add a single byte to a recording or 'showcmd'.
// Return true if a full key has been received, false otherwise.
private int
gotchars_add_byte(GotCharsState *state, Byte byte) {
   Unt c = state->buf[state->buflen++] = byte;
   int retval = false;
   int in_special = state->pending_special > 0;
   int in_mbyte = state->pending_mbyte > 0;

   if (in_special)
      state->pending_special--;
   ei (c == K_SPECIAL)
      // When receiving a special key sequence, store it until we have all
      // the bytes and we can decide what to do with it.
      state->pending_special = 2;

   if (state->pending_special > 0)
      goto ret_false;

   if (in_mbyte)
      state->pending_mbyte--;
   else {
      if (in_special) {
         if (state->prev_c == KS_MODIFIER)
            // When receiving a modifier, wait for the modified key.
            goto ret_false;
         c = TO_SPECIAL(state->prev_c, c);
         if (c == K_FOCUSGAINED || c == K_FOCUSLOST)
            // Drop K_FOCUSGAINED and K_FOCUSLOST, they are not useful in a recording.
            state->buflen = 0;
      }
      // When receiving a multibyte character, store it until we have all
      // the bytes, so that it won't be split between two buffer blocks,
      // and deleteBufferTail() will work properly.
      state->pending_mbyte = MB_BYTE2LEN_CHECK(c) - 1;
   }

   if (state->pending_mbyte > 0)
      goto ret_false;

   retval = true;
ret_false:
   state->prev_c = c;
   return retval;
}

// Write typed characters to script file. If recording is on put the character in the record buffer
private void
gotchars(CS chars, int len) {
    CS s = chars;
    Unt i;
    int todo = len;
    static GotCharsState state;

    while (todo-- > 0) {
      if (!gotchars_add_byte(&state, *s++))
          continue;

      // Handle one byte at a time; no translation to be done.
      for (i = 0; i < state.buflen; ++i)
          updateScript(state.buf[i]);

      if (reg_recording != 0) {
          state.buf[state.buflen] = ZERO;
          add_buff(&recordbuff, state.buf, (long)state.buflen);
          // remember how many chars were last recorded
          lastRecordedLen += state.buflen;
      }
      state.buflen = 0;
   }

   maySyncUndo();

   // output "debug mode" message next time in debug mode
   debug_did_msg = false;

   // Since characters have been typed, consider the following to be in
   // another mapping.  Search string will be kept in history.
   ++maptick;
}

// Record an <Ignore> key.
pub void
gotchars_ignore(void) {
   Byte nop_buf[3] = { K_SPECIAL, KS_EXTRA, KE_IGNORE };
   gotchars(nop_buf, 3);
}

// Undo the last gotchars() for "len" bytes.  To be used when putting a typed
// character back into the typeahead buffer, thus gotchars() will be called again.
// Only affects recorded characters.
pub void
ungetchars(int len) {
   if (reg_recording == 0)
      return;

   deleteBufferTail(&recordbuff, len);
   lastRecordedLen -= len;
}

// Sync undo. Called when typed characters are obtained from the typeahead buffer, or when a menu 
// is used. Do not sync:
// - In Insert mode, unless cursor key has been used.
// - While reading a script file.
// - When no_u_sync is non-zero.
private void
maySyncUndo(void) {
   if ((!(stateG & (MODE_INSERT | MODE_COMMLINE)) || arrow_used) && scriptin[curscript] == NULL)
      u_sync(false);
}

// Make "typeBufG" empty and allocate new buffers.
private void
reallocateTypebuf(void) {
   typeBufG.c = alloc(TYPELEN_INIT);
   typeBufG.noremap = alloc(TYPELEN_INIT);
   typeBufG.len = TYPELEN_INIT;
   typeBufG.currPos = MAXMAPLEN + 4;  // can insert without realloc
   typeBufG.validLen = 0;
   typeBufG.mappedLen = 0;
   typeBufG.silentCnt = 0;
   typeBufG.noAbbrCnt = 0;
   if (++typeBufG.changeCnt == 0)
      typeBufG.changeCnt = 1;
   typebuf_was_filled = false;
}

// Free the buffers of "typeBufG".
private void
free_typeBufG(void) {
   if (typeBufG.c == typeBufG_init)
      internal_error((CS)"Free typeBufG 1");
   else
      EE_CLEAR(typeBufG.c);
   if (typeBufG.noremap == noremapbuf_init)
      internal_error((CS)"Free typeBufG 2");
   else
      EE_CLEAR(typeBufG.noremap);
}

// When doing ":so! file", the current typeahead needs to be saved, and
// restored when "file" has been read completely.
private Typeahead saved_typeBufG[NSCRIPT];

pub int
save_typebuf(void) {
   initTypebuf();
   saved_typeBufG[curscript] = typeBufG;
   reallocateTypebuf();
   return OK;
}

private Unt old_char = UNT;   // character put back by vungetc()
private int oldModMask;   // modMaskG for ungotten character
private int old_mouse_row;   // mouse_row related to old_char
private int old_mouse_col;   // mouse_col related to old_char
private int old_keyWasStuffedG;   // whether old_char was stuffed

private int 
can_get_old_char(void) {
   // If the old character was not stuffed and characters have been added to
   // the stuff buffer, need to first get the stuffed characters instead.
   return old_char != UNT && (old_keyWasStuffedG || stuff_empty());
}

// Save all 3 kinds of typeahead, so that the user must type at a prompt.
pub void
save_typeahead(TypeaheadSave *tp) {
   tp->save_typebuf = typeBufG;
   reallocateTypebuf();
   tp->typebuf_valid = true;
   if (!tp->typebuf_valid)
      typeBufG = tp->save_typebuf;

   tp->old_char = old_char;
   tp->oldModMask = oldModMask;
   old_char = UNT;

   tp->save_readbuf1 = readbuf1;
   readbuf1.first.next = NULL;
   tp->save_readbuf2 = readbuf2;
   readbuf2.first.next = NULL;
# ifdef USE_INPUT_BUF
   tp->save_inputbuf = get_input_buf();
# endif
}

// Restore the typeahead to what it was before calling save_typeahead().
// The allocated memory is freed, can only be called once!
// When "overwrite" is false input typed later is kept.
pub void
restore_typeahead(TypeaheadSave* tp, Boole overwrite) {
    if (tp->typebuf_valid) {
       free_typeBufG();
       typeBufG = tp->save_typebuf;
    }

    old_char = tp->old_char;
    oldModMask = tp->oldModMask;

    freeBuffer(&readbuf1);
    readbuf1 = tp->save_readbuf1;
    freeBuffer(&readbuf2);
    readbuf2 = tp->save_readbuf2;
# ifdef USE_INPUT_BUF
    set_input_buf(tp->save_inputbuf, overwrite);
# endif
}

// Open a new script file for the ":source!" command.
pub void
openscript(CS name, Boole directly) {
   if (curscript + 1 == NSCRIPT) {
      emsg(_(e_scripts_nested_too_deep));
      return;
   }

   if (ignore_script)
      // Not reading from script, also don't open one.  TODO Warning message
      return;

   if (scriptin[curscript] != NULL)   // already reading script
      ++curscript;
   // use nameBuffG for expanded name
   doExpandEnv(OUT nameBuffTextG, name);
   if ((scriptin[curscript] = fopen((char *)nameBuffG, READBIN)) == NULL) {
      showErrFmtMsg(_(e_cant_open_file_str), name);
      if (curscript)
         --curscript;
      return;
   }
   if (save_typebuf() == FAIL)
      return;

   //Execute the commands from the file right now when using ":source!" after ":global" or 
   //":argdo" or in a loop. Also when another command follows. This means the display won't be 
   //updated. Don't do this always, "make test" would fail.
   if (directly) {
      Operator oper;
      int save_State = stateG;
      int save_restart_edit = restart_edit;
      int save_finish_op = finish_op;
      int save_msg_scroll = msg_scroll;

      stateG = MODE_NORMAL;
      msg_scroll = false;   // no msg scrolling in Normal mode
      restart_edit = 0;   // don't go to Insert mode
      clear_oparg(&oper);
      finish_op = false;

      int oldcurscript = curscript;
      do {
         update_topline_cursor();   // update cursor position and topline
         normalAction(&oper, false);   // execute one action
         (void)vpeekc();      // check for end of file
      } while (scriptin[oldcurscript] != NULL);

      stateG = save_State;
      msg_scroll = save_msg_scroll;
      restart_edit = save_restart_edit;
      finish_op = save_finish_op;
   }
}

// Close the currently active input script.
private void
closeScript(void) {
   free_typeBufG();
   typeBufG = saved_typeBufG[curscript];

   fclose(scriptin[curscript]);
   scriptin[curscript] = NULL;
   if (curscript > 0)
      --curscript;
}

#if defined(EXITFREE) || defined(PROTO)
pub void
close_all_scripts(void) {
    while (scriptin[0] != NULL)
       closeScript();
}
#endif

// Return true when reading keys from a script file.
pub int
using_script(void) {
   return scriptin[curscript] != NULL;
}

// This function is called just before doing a blocking wait.  Thus after
// waiting 'updatetime' for a character to arrive.
pub void
before_blocking(void) {
   updateScript(0);
   if (may_garbage_collect)
      garbage_collect(false);
}

// updateScript() is called when a character can be written into the script
// file or when we have waited some time for a character (c == 0)
//
// All the changed memfiles are synced if c == 0 or when the number of typed
// characters reaches 'updatecount' and 'updatecount' is non-zero.
private void
updateScript(int c) {
    static int count = 0;

    if (c != 0 && scriptout)
       putc(c, scriptout);
    if (c == 0 || (++count >= 200)) {
        ml_sync_all(c == 0, true);
        count = 0;
    }
    if (typedchars_pos < MAXMAPLEN) {
       typedchars[typedchars_pos] = c;
       typedchars_pos++;
    }
}

// Convert "c_arg" plus "modifiers" to merge the effect of modifyOtherKeys into
// the character.  Also for when the Kitty key protocol is used.
pub Unt
mergeModifierKey(Unt cArg, Unt* modifiers) {
    Unt c = cArg;

    // CTRL only uses the lower 5 bits of the character.
    if (*modifiers & MOD_MASK_CTRL) {
      if ((c >= '`' && c <= 0x7f) || (c >= '@' && c <= '_')) {
         c &= 0x1f;
         if (c == ZERO) {
            c = K_ZERO;
         }
      } ei (c == '6')
         // CTRL-6 is equivalent to CTRL-^
         c = 0x1e;
      if (c != cArg) {
         *modifiers &= ~MOD_MASK_CTRL;
      }
   }

   // Alt/Meta sets the 8th bit of the character.
   if ((*modifiers & (MOD_MASK_META | MOD_MASK_ALT)) && c <= 127) {
      // Some terminals (esp. Kitty) do not include Shift in the character.
      // Apply it here to get consistency across terminals.  Only do ASCII
      // letters, for other characters it depends on the keyboard layout.
      if ((*modifiers & MOD_MASK_SHIFT) && c >= 'a' && c <= 'z') {
         c += 'a' - 'A';
         *modifiers &= ~MOD_MASK_SHIFT;
      }
      c += 0x80;
      *modifiers &= ~(MOD_MASK_META | MOD_MASK_ALT);
   }

   return c;
}

// Add a single byte to 'showcmd' for a partially matched mapping.
// Call add_to_showcmd() if a full key has been received.
private void
addByteToShowcmd(Byte byte) {
   static GotCharsState state;
   Unt modifiers = 0;
   int c = ZERO;

   if (msg_silent != 0)
      return;

   if (!gotchars_add_byte(&state, byte))
      return;

   state.buf[state.buflen] = ZERO;
   state.buflen = 0;

   CS ptr = state.buf;
   if (ptr[0] == K_SPECIAL && ptr[1] == KS_MODIFIER && ptr[2] != ZERO) {
      modifiers = ptr[2];
      ptr += 3;
   }

   if (*ptr != ZERO) {
      CS mb_ptr = mb_unescape(&ptr);
      c = mb_ptr ? mb_ptr2char(mb_ptr) : *ptr++;
      if (c <= 0x7f) {
         // Merge modifiers into the key to make the result more readable.
         Unt modifiersAfter = modifiers;
         Unt modifiedC = mergeModifierKey(c, &modifiersAfter);

         if (modifiersAfter == 0) {
            modifiers = 0;
            c = modifiedC;
         }
      }
   }

   //TODO: is there a more readable and yet compact representation of modifiers and special keys?
   if (modifiers != 0) {
      add_to_showcmd(K_SPECIAL);
      add_to_showcmd(KS_MODIFIER);
      add_to_showcmd(modifiers);
   }
   if (c != ZERO)
      add_to_showcmd(c);
   while (*ptr != ZERO)
      add_to_showcmd(*ptr++);
}


//Get the next input character.
//Can return a special key or a multi-byte character.
//Can return ZERO when called recursively, use safe_vgetc() if that's not wanted.
//Set modMaskG to the set of modifiers that are held down based on the MOD_MASK_* symbols 
//that are read first.
//Translate escaped K_SPECIAL and CSI bytes to a K_SPECIAL or CSI byte.
//Collect the bytes of a multibyte character into the whole character.
//Return the modifiers in the global "modMaskG".
pub Unt
vgetc(void) {
   Unt c, c2;
   Unt n;
   Byte buf[MB_MAXBYTES + 1];

   // Do garbage collection when garbagecollect() was called previously and
   // we are now at the toplevel.
   if (may_garbage_collect && want_garbage_collect)
      { garbage_collect(false); }

   // If a character was put back with vungetc, it was already processed. Return it directly.
   if (can_get_old_char()) {
      c = old_char;
      old_char = UNT;
      modMaskG = oldModMask;
      mouseRowG = old_mouse_row;
      mouseColG = old_mouse_col;
      goto afterGotChar; 
   } 
    
   // number of characters recorded from the last vgetc() call
   static Unt   lastVgetcRecordedLen = 0;
 
   modMaskG = 0;
   vgetcModMaskG = 0;
   vgetcOrigCharG = 0;
 
   // lastRecordedLen can be larger than lastVgetcRecordedLen if peeking records more
   lastRecordedLen -= lastVgetcRecordedLen;
 
   for (;;) {  // this is done twice if there are modifiers
      Boole didIncrement = false;
     
      // No mapping in popup portals if they disabled mappings and a modifier has been read
      if (modMaskG || popup_no_mapping()) {
         ++no_mapping;
         ++allow_keys;
         // modMaskG value may change, remember we did the increment
         didIncrement = false;
      }
      c = vGetOrPeek(true);
      
      if (didIncrement) {
         --no_mapping;
         --allow_keys;
      }
     
      // Get two extra bytes for special keys, handle modifiers.
      if (c == K_SPECIAL) {
         int save_allow_keys = allow_keys;
          
         ++no_mapping;
         allow_keys = 0;        // make sure backspace is not found
         c2 = vGetOrPeek(true); // no mapping for these chars
         c = vGetOrPeek(true);
         --no_mapping;
         allow_keys = save_allow_keys;
         if (c2 == KS_MODIFIER) {
            modMaskG = c;
            continue;
         }
         c = TO_SPECIAL(c2, c);
           
         // K_ESC is used to avoid ambiguity with the single Esc character that might be the 
         // start of an escape sequence. Convert it back to a single Esc here.
         if (c == K_ESC)
            { c = ESC; }
           
         if (c == K_SID) {
            // Handle <SID>{sid};  Do up to 20 digits for safety.
            last_used_sid = 0;
            for (int j = 0; j < 20 && SAFE_isdigit(c = vGetOrPeek(true)); ++j)
               last_used_sid = last_used_sid * 10 + (c - '0');
            last_used_map = NULL;
            continue;
         }
      }
     
      // For a multi-byte character get all the bytes and return the converted character.
      // Note: This will loop until enough bytes are received!
      if ((n = MB_BYTE2LEN_CHECK(c)) > 1) {
         ++no_mapping;
         buf[0] = c;
         for (Unt i = 1; i < n; ++i) {
            buf[i] = vGetOrPeek(true);
            if (buf[i] == K_SPECIAL) {
               // Must be a K_SPECIAL - KS_SPECIAL - KE_FILLER sequence, which represents a 
               // K_SPECIAL (0x80), or a CSI - KS_EXTRA - KE_CSI sequence, which represents a CSI 
               // (0x9B), or a K_SPECIAL - KS_EXTRA - KE_CSI, which is CSI too.
               c = vGetOrPeek(true);
               if (vGetOrPeek(true) == KE_CSI && c == KS_EXTRA)
                  { buf[i] = CSI; }
            }
         }
         --no_mapping;
         c = mb_ptr2char(buf);
      }
     
      if (vgetcOrigCharG == 0) {
         vgetcModMaskG = modMaskG;
         vgetcOrigCharG = c;
      }
     
      // A keypad or special function key was not mapped, use it like its ASCII equivalent.
      switch (c) {
      case K_KPLUS:   c = '+'; break;
      case K_KMINUS:   c = '-'; break;
      case K_KDIVIDE:   c = '/'; break;
      case K_KMULTIPLY: c = '*'; break;
      case K_KENTER:   c = ENTER; break;
      case K_KPOINT: c = '.'; break;
      case K_K0:   c = '0'; break;
      case K_K1:   c = '1'; break;
      case K_K2:   c = '2'; break;
      case K_K3:   c = '3'; break;
      case K_K4:   c = '4'; break;
      case K_K5:   c = '5'; break;
      case K_K6:   c = '6'; break;
      case K_K7:   c = '7'; break;
      case K_K8:   c = '8'; break;
      case K_K9:   c = '9'; break;
     
      case K_XHOME:
      case K_ZHOME: { 
         if (modMaskG == MOD_MASK_SHIFT) {
            c = K_S_HOME;
            modMaskG = 0;
         } ei (modMaskG == MOD_MASK_CTRL) {
            c = K_C_HOME;
            modMaskG = 0;
         } else
            c = K_HOME;
          break;
      }
      case K_XEND:
      case K_ZEND: {
         if (modMaskG == MOD_MASK_SHIFT) {
            c = K_S_END;
            modMaskG = 0;
         } ei (modMaskG == MOD_MASK_CTRL) {
            c = K_C_END;
            modMaskG = 0;
         } else
            c = K_END;
         break;
      }
      case K_XUP:   c = K_UP; break;
      case K_XDOWN:   c = K_DOWN; break;
      case K_XLEFT:   c = K_LEFT; break;
      case K_XRIGHT:   c = K_RIGHT; break;
      }
     
      break;
   }

   lastVgetcRecordedLen = lastRecordedLen;
afterGotChar:
   //In the main loop "may_garbage_collect" can be set to do garbage collection in the first next
   //vgetc(). It's disabled after that to avoid internally used Lists and Bags to be freed.
   may_garbage_collect = false;

   if (c != K_MOUSEMOVE && c != K_IGNORE && c != K_CURSORHOLD) {
      //Don't trigger 'balloonexpr' unless only the mouse was moved.
      bevalexpr_due_set = false;
      ui_remove_balloon();
   }
   //Only filter keys that do not come from ":normal". Keys from feedkeys() are filtered.
   if ((!ex_normal_busy || in_feedkeys) && popup_do_filter(c)) {
       if (c == Ctrl_C)
          { gotInterruptG = false; } // avoid looping
       c = K_IGNORE;
   }

   // Clear the next typedchars_pos
   typedchars_pos = 0;

   // Need to process the character before we know it's safe to do something else.
   if (c != K_IGNORE)
      { state_no_longer_safe(S"key typed"); }
       
   return c;
}

// Like vgetc(), but never return a ZERO when called recursively, get a key
// directly from the user (ignoring typeahead).
pub Unt
safe_vgetc(void) {
   Unt c = vgetc();
   if (c == ZERO)
       c = get_keystroke();
   return c;
}

// Like safe_vgetc(), but loop to handle K_IGNORE. Also ignore scrollbar events.
// Does not handle bracketed paste - do not use the result for commands.
private int
plainVgetcNopaste(void) {
   Unt c;
   do {
      c = safe_vgetc();
   } while (c == K_IGNORE || c == K_VER_SCROLLBAR || c == K_HOR_SCROLLBAR || c == K_MOUSEMOVE);
   return c;
}

//Like safe_vgetc(), but loop to handle K_IGNORE. Also ignore scrollbar events.
pub Unt
plain_vgetc(void) {
   Unt c = plainVgetcNopaste();
   if (c == K_PS) {
      //Only handle the first pasted character. Drop the rest, since we
      //don't know what to do with it.
      c = bracketed_paste(PASTE_ONE_CHAR, false, NULL);
   } 

   return c;
}

//Check if a character is available, such that vgetc() will not block.
//If the next character is a special character or multi-byte, the returned character is not valid!.
//Return ZERO if no character is available.
pub Unt
vpeekc(void) {
   if (can_get_old_char())
      { return old_char; }
   return vGetOrPeek(false);
}

// Like vpeekc(), but don't allow mapping.  Do allow checking for terminal codes.
pub int
vpeekc_nomap(void) {
    ++no_mapping;
    ++allow_keys;
    int c = vpeekc();
    --no_mapping;
    --allow_keys;
    return c;
}

// Check if any character is available, also half an escape sequence.
// Trick: when no typeahead found, but there is something in the typeahead
// buffer, it must be an ESC that is recognized as the start of a key code.
pub Unt
vpeekc_any(void) {
   Unt c = vpeekc();
   if (c == ZERO && typeBufG.validLen > 0)
      { c = ESC; }
   return c;
}

// Call vpeekc() without causing anything to be mapped.
// Return true if a character is available, false otherwise.
pub int
char_avail(void) {
    // When test_override("char_avail", 1) was called pretend there is no
    // typeahead.
    if (disable_char_avail_for_testing)
       { return false; }
    ++no_mapping;
    int retval = vpeekc();
    --no_mapping;
    return (retval != ZERO);
}

// "getchar()" and "getcharstr()" functions
private void
getcharCommon(Arr(Var) argvars, Var* returnVar, Boole allow_number) {
   Long n = 0;
   int  called_emsg_start = called_emsg;
   Boole error = false;
   Boole simplify = true;
   Byte cursor_flag = 'm';

   if (argvars[0].tag != VAR_UNKNOWN && argvars[1].tag == VAR_BAG) {
      Bag* d = argvars[1].bag;

      if (allow_number)
         allow_number = bagGetBool(d, tConst("number"), true);
      ei (bagHasKey(d, tConst("number")))
         showErrFmtMsg(_(e_invalid_argument_str), "number");

      simplify = bagGetBool(d, tConst("simplify"), true);

      CS cursor_str = bagGetString(d, tConst("cursor"), false);
      if (cursor_str != NULL) {
         if (STRCMP(cursor_str, "hide") != 0
               && STRCMP(cursor_str, "keep") != 0
               && STRCMP(cursor_str, "msg") != 0)
            { showErrFmtMsg(_(e_invalid_value_for_argument_str_str), "cursor",  cursor_str); }
         else
            { cursor_flag = cursor_str[0]; }
      }
    }

    if (called_emsg != called_emsg_start)
       { return; }

    // vpeekc() used to check for messages, but that caused problems, invoking
    // a callback where it was not expected.  Some plugins use getchar(1) in a
    // loop to await a message, therefore make sure we check for messages here.
    parse_queued_messages();

    if (cursor_flag == 'h')
       cursor_sleep();
    ei (cursor_flag == 'm')
       windgoto(msgRowG, msgColG);

    ++no_mapping;
    ++allow_keys;
    if (!simplify)
       ++no_reduce_keys;
    for (;;) {
      if (argvars[0].tag == VAR_UNKNOWN
           || (argvars[0].tag == VAR_NUMBER && argvars[0].number == -1))
         // getchar(): blocking wait.
         n = plainVgetcNopaste();
      ei (varGetNumberChk(argvars, OUT &error))
         // getchar(1): only check if char avail
         n = vpeekc_any();
      ei (error || vpeekc_any() == ZERO)
         // illegal argument or getchar(0) and no char avail: return zero
         n = 0;
      else
         // getchar(0) and char avail() != ZERO: get a character.
         // Note that vpeekc_any() returns K_SPECIAL for K_IGNORE.
         n = safe_vgetc();

      if (n == K_IGNORE || n == K_MOUSEMOVE || n == K_VER_SCROLLBAR || n == K_HOR_SCROLLBAR)
         continue;
      break;
   }
   --no_mapping;
   --allow_keys;
   if (!simplify)
      --no_reduce_keys;

   if (cursor_flag == 'h')
      cursor_unsleep();

   set_EeglVar_nr(VV_MOUSE_WIN, 0);
   set_EeglVar_nr(VV_MOUSE_WINID, 0);
   set_EeglVar_nr(VV_MOUSE_LNUM, 0);
   set_EeglVar_nr(VV_MOUSE_COL, 0);

   if (n != 0 && (!allow_number || IS_SPECIAL(n) || modMaskG != 0)) {
      Byte temp[10];   // modifier: 3, mbyte-char: 6, ZERO: 1
      int i = 0;

      // Turn a special key into three bytes, plus modifier.
      if (modMaskG != 0) {
           temp[i++] = K_SPECIAL;
           temp[i++] = KS_MODIFIER;
           temp[i++] = modMaskG;
      }
      if (IS_SPECIAL(n)) {
         temp[i++] = K_SPECIAL;
         temp[i++] = K_SECOND(n);
         temp[i++] = K_THIRD(n);
      } else
         i += mb_char2bytes(n, temp + i);

      temp[i] = ZERO;
      returnVar->tag = VAR_STRING;
      returnVar->string = copySubstr(temp, i);

      if (is_mouse_key(n)) {
         int row = mouseRowG;
         int col = mouseColG;
         Portal* port;
         LineNr   lnum;
         Portal* wp;
         int      winnr = 1;

         if (row >= 0 && col >= 0) {
            // Find the portal at the mouse coordinates and compute the text position.
            port = mouseFindPortal(OUT &row, OUT &col, FIND_POPUP);
            if (!port)
               return;
            (void)mouse_comp_pos(port, OUT &row, OUT &col, &lnum, NULL);
            if (PORTAL_IS_POPUP(port)) {
               winnr = 0;
             } else {
               for (wp = firstPor; wp != port && wp; wp = wp->next) {
                  ++winnr;
               }
            } 
            set_EeglVar_nr(VV_MOUSE_WIN, winnr);
            set_EeglVar_nr(VV_MOUSE_WINID, port->id);
            set_EeglVar_nr(VV_MOUSE_LNUM, lnum);
            set_EeglVar_nr(VV_MOUSE_COL, col + 1);
         }
      }
   } ei (!allow_number)
       returnVar->tag = VAR_STRING;
    else
       returnVar->number = n;
}

pub void
f_getchar(Arr(Var) argvars, Var* returnVar) {
   getcharCommon(argvars, returnVar, true);
}

pub void
f_getcharstr(Arr(Var) argvars, Var* returnVar) {
   getcharCommon(argvars, returnVar, false);
}

pub void
f_getcharmod(Arr(Var) argvars UNUSED, Var* returnVar) {
   returnVar->number = modMaskG;
}

#define MAX_REPEAT_PARSE 8

//Process messages that have been queued for clientserver. Also check if any jobs have ended.
//These functions can call arbitrary scripts and should only be called when it is safe
pub void
parse_queued_messages(void) {
   int old_curPor_id;
   int old_curbuf_fnum;
   int save_may_garbage_collect = may_garbage_collect;
   static int entered = 0;
   int was_safe = get_was_safe_state();

   //Do not handle messages while redrawing, because it may cause buffers to
   //change or be wiped while they are being redrawn.
   //Also bail out when parsing messages was explicitly disabled.
   if (updating_screen || dont_parse_messages)
      return;

   // If memory allocation fails during startup we'll exit but curBook or curPor could be NULL.
   if (!curBook || !curPor)
      return;

   old_curbuf_fnum = curBook->fiNum;
   old_curPor_id = curPor->id;

   ++entered;

   // may_garbage_collect is set in main_loop() to do garbage collection when
   // blocking to wait on a character.  We don't want that while parsing
   // messages, a callback may invoke vgetc() while lists and bags are in use
   // in the call stack.
   may_garbage_collect = false;

   // Loop when a job ended, but don't keep looping forever.
   for (Unt i = 0; i < MAX_REPEAT_PARSE; ++i) {
      //Write any buffer lines still to be written.
      channel_write_any_lines();

      //Process the messages queued on channels.
      channel_parse_messages();
      //Check if any jobs have ended.  If so, repeat the above to handle
      //changes, e.g. stdin may have been closed.
      if (job_check_ended())
         continue;
      free_unused_terminals();

#ifdef SIGUSR1
      if (got_sigusr1) {
          applyAutocomms(EVENT_SIGUSR1, NULL, NULL, false, curBook);
          got_sigusr1 = false;
      }
#endif
      break;
   }

   // When not nested we'll go back to waiting for a typed character.  If it
   // was safe before then this triggers a SafeStateAgain autocommand event.
   if (entered == 1 && was_safe)
      may_trigger_safestateagain();

   may_garbage_collect = save_may_garbage_collect;

   // If the current portal or buffer changed we need to bail out of the
   // waiting loop.  E.g. when a job exit callback closes the terminal portal.
   if (curPor->id != old_curPor_id || curBook->fiNum != old_curbuf_fnum)
      ins_char_typebuf(K_IGNORE, 0);

   --entered;
}


privateComp typedef enum {
   mrFail,    // failed, break loop
   mrGet,     // get a character from typeahead
   mrRetry,   // try to map again
   mrNoMatch  // no matching mapping, get char
} MapResult;

// Check if the bytes at the start of the typeahead buffer are a character used
// in Insert mode completion.  This includes the form with a CTRL modifier.
private int
atAnInsertCompletionKey(void) {
   CS p = typeBufG.c + typeBufG.currPos;
   int c = *p;

   if (typeBufG.validLen > 3
       && (c == K_SPECIAL || c == CSI)  // CSI is used by the GUI
       && p[1] == KS_MODIFIER
       && (p[2] & MOD_MASK_CTRL))
      c = p[3] & 0x1f;
   return (ctrl_x_mode_not_default() && eeIsCtrlXKey(c))
      || (compl_status_local() && (c == Ctrl_N || c == Ctrl_P));
}

// Check if typeBufG.c[] contains a modifier plus key that can be changed into just a key, apply 
// that. Check from typeBufG.c[typeBufG.currPos] to typeBufG.c[typeBufG.currPos + "maxOffset"].
// Return the length of the replaced bytes, 0 if nothing changed, -1 for error.
private int
checkSimplifyModifier(int const maxOffset) {
   int      offset;
   CS input;

   for (offset = 0; offset < maxOffset; ++offset) {
      if (offset + 3 >= typeBufG.validLen)
         break;
      input = typeBufG.c + typeBufG.currPos + offset;
      
      if ((input[0] != K_SPECIAL && input[0] != CSI) || input[1] != KS_MODIFIER) {
         continue;
      }
      // A modifier was not used for a mapping, apply it to ASCII keys.
      // Shift would already have been applied.
      Unt modifier = input[2];
      Unt   c = input[3];
      Unt cModified = mergeModifierKey(c, &modifier);
      if (cModified == c) { // no Ctrl, Alt etc pressed
         continue;
      }

      Byte new_string[MB_MAXBYTES];
      int len;

      if (offset == 0) {
         // At the start: remember the character and modMaskG before merging: in some cases, 
         // e.g. at the hit-return prompt, they are put back in the typeahead buffer.
         vgetcOrigCharG = c;
         vgetcModMaskG = input[2];
      }
      if (IS_SPECIAL(cModified)) {
         new_string[0] = K_SPECIAL;
         new_string[1] = K_SECOND(cModified);
         new_string[2] = K_THIRD(cModified);
         len = 3;
      } else {
         len = mb_char2bytes(cModified, new_string);
      }
      Text newText = (Text){new_string, len};
      if (modifier == 0) { // all the modifier keys have been handled
         if (termPutStrIntoTypeBuf(offset, 4, newText) == FAIL) {
            return -1;
         }
      } else {
         input[2] = modifier;
         if (termPutStrIntoTypeBuf(offset + 3, 1, newText) == FAIL) {
            return -1;
         }
      }
      return len;
   }
   return 0;
}

privateComp typedef struct {
   MapBlock* longestFull;
   MapBlock* foundMapping;
   int maxMLen; //max_mlen
   int matchLen; //mlen
   int currLen; // mp_match_len 
   int wantTermcode; // 1 if termcode expected after maxMLen
   int keylen;
} MatchFinding;

// Loop until a partially matching mapping is found or all (local) mappings have been checked.
// The longest full match is remembered in this var. A full match is only accepted if there
// is no partial match, so "aa" and "aaa" can both be mapped to different commands.
private MatchFinding
searchForPartialMappings(int foundKeylen, int timedout, Boole isAbstractPlugMapping) {
   MatchFinding fin = {};
   
   int localState = get_real_state();
   fin.keylen = foundKeylen;
   int nolmaplen;
   
   // Check for a mappable key sequence.
   // Walk through one maphash[] list until we find an entry that matches.
   Unt typebufChar = typeBufG.c[typeBufG.currPos];
   // Don't look for mappings if:
   // - no_mapping set: mapping disabled (e.g. for CTRL-V)
   // - maphash_valid not set: no mappings present.
   // - typeBufG.c[typeBufG.currPos] should not be remapped
   // - waiting for "hit return to continue" and CR or SPACE typed
   // - waiting for a char with --more--
   // - in Ctrl-X mode, and we get a valid char for that mode
   if (no_mapping == 0 
      && isMappingTableValid()
      && (typebufChar != '0' || isZeroJustANumberG == 0)
      && (typeBufG.mappedLen == 0 || isAbstractPlugMapping
           || ((typeBufG.noremap[typeBufG.currPos] & (RM_NONE|RM_ABBR)) == 0))
      && !(stateG == MODE_HITRETURN && (typebufChar == ENTER || typebufChar == ' '))
      && stateG != MODE_ASKMORE
      && stateG != MODE_CONFIRM
      && !atAnInsertCompletionKey()
   ) {
      if (typebufChar == K_SPECIAL) {
         nolmaplen = 2;
      } else {
         LANGMAP_ADJUST(typebufChar, 
           (stateG & (MODE_COMMLINE | MODE_INSERT)) == 0
         );
         nolmaplen = 0;
      }

      fin.foundMapping = getBufMappingTableList(localState, typebufChar);
      MapBlock* foundMapping1 = getMappingTableList(localState, typebufChar);
       
      if (fin.foundMapping == NULL) { // There are no buffer-local mappings
         fin.foundMapping = foundMapping1;
         foundMapping1 = NULL;
      }

      // {{{ main mapping loop
      
      fin.currLen = 0;
      for ( ; fin.foundMapping != NULL; ) {
         // Only consider an entry if the first character matches and it is for the current state.
         // Skip ":lmap" mappings if keys were mapped.
         if (
               fin.foundMapping->lhs[0] == typebufChar
            && (fin.foundMapping->mode & localState)
            && !(fin.foundMapping->simplified && typeBufG.mappedLen == 0)
            && ((fin.foundMapping->mode & MODE_LANGMAP) == 0 || typeBufG.mappedLen == 0)
         ) {
               
            int nomap = nolmaplen;
            Unt modifiers = 0;
            // find the match length of this mapping
            for (
               fin.matchLen = 1; 
               fin.matchLen < typeBufG.validLen; 
               ++fin.matchLen
            ) {
               Unt currChar = typeBufG.c[typeBufG.currPos + fin.matchLen];
               if (nomap > 0) {
                  if (nomap == 2 && currChar == KS_MODIFIER)
                     modifiers = 1;
                  ei (nomap == 1 && modifiers == 1)
                     modifiers = currChar;
                  --nomap;
               } else {
                  if (currChar == K_SPECIAL)
                     nomap = 2;
                  ei (mergeModifierKey(currChar, &modifiers) == currChar) {
                     // Only apply 'langmap' if merging modifiers into the key will not result
                     // in another character, so that 'langmap' behaves consistently in
                     // different terminals and GUIs.
                     LANGMAP_ADJUST(currChar, true);
                  }
                  modifiers = 0;
               }

               if (fin.foundMapping->lhs[fin.matchLen] != currChar)
                  break;
            }

            //Don't allow mapping the first byte(s) of a multi-byte char.
            //Happens when mapping <M-a> and then changing 'encoding'. Beware that 0x80 is escaped.
            {
               CS p1 = fin.foundMapping->lhs;
               CS p2 = mb_unescape(&p1);

               if (p2 && utf8CharLens[typebufChar] > utfCharLen(p2))
                  fin.matchLen = 0;
            }

            //Check whether an entry matches.
            //- Full match: matchLen == keylen
            //- Partial match: matchLen == typeBufG.validLen
            fin.keylen = fin.foundMapping->keylen;
            if (fin.matchLen == fin.keylen 
                  || (fin.matchLen == typeBufG.validLen && typeBufG.validLen < fin.keylen)
            ) {

               // If only script-local mappings are allowed, check if the mapping starts with K_SNR
               Byte* s = typeBufG.noremap + typeBufG.currPos;
               if (*s == RM_SCRIPT
                     && (fin.foundMapping->lhs[0] != K_SPECIAL 
                           || fin.foundMapping->lhs[1] != KS_EXTRA 
                           || fin.foundMapping->lhs[2] != KE_SNR))
                  goto nextIter;

               // If one of the typed keys cannot be remapped, skip the entry.
               int n;
               for (n = fin.matchLen; --n >= 0; ) {
                  if (*s++ & (RM_NONE|RM_ABBR))
                     break;
               }
               if (!isAbstractPlugMapping && n >= 0)
                  goto nextIter;
               if (fin.keylen > typeBufG.validLen) {
                  if (!timedout 
                        && !(fin.longestFull != NULL && fin.longestFull->nowait)) { 
                     // break at a partial match
                     fin.keylen = KEYLEN_INCOMPLETE_MAPPING;
                     break;
                  }
               } ei (fin.keylen > fin.currLen) {
                  // found a longer match
                  fin.longestFull = fin.foundMapping;
                  fin.currLen = fin.keylen;
               }
            } else {
               // No match; may have to check for termcode at next character.
               // If the first character that didn't match is K_SPECIAL then check for a termcode.
               // This isn't perfect but should work in most cases.
               if (fin.maxMLen < fin.matchLen) {
                  fin.maxMLen = fin.matchLen;
                  fin.wantTermcode = fin.foundMapping->lhs[fin.matchLen] == K_SPECIAL;
               } ei (fin.maxMLen == fin.matchLen && fin.foundMapping->rhs[fin.matchLen] == K_SPECIAL) {
                  fin.wantTermcode = 1;
               } 

               // Check termcode for uppercase character to properly process the 
               // "ESC[27;2;<ascii code>~" control sequences.
               if (ASCII_ISUPPER(fin.foundMapping->rhs[fin.matchLen]))
                  fin.wantTermcode = 1;
            }
         }

nextIter:
         if (fin.foundMapping->next == NULL) {
            fin.foundMapping = foundMapping1;
            foundMapping1 = NULL;
         } else {
            fin.foundMapping = fin.foundMapping->next;
         }
           
      } //}}}

      // If no partial match found, use the longest full match.
      if (fin.keylen != KEYLEN_INCOMPLETE_MAPPING && fin.longestFull != NULL) {
         fin.foundMapping = fin.longestFull;
         fin.keylen = fin.currLen;
      }
   }
   return fin;
}

// Handle mappings in the typeahead buffer.
// - When something was mapped, return mrRetry for recursive mappings.
// - When nothing mapped and typeahead has a character: return mrGet.
// - When there is no match yet, return mrNoMatch, need to get more typeahead.
// - On failure (out of memory) return mrFail.
private MapResult
handleMapping(OUT int* foundKeylen, int timedout, OUT int* mapdepth) {
   int i;
   int isAbstractPlugMapping = false; // is this an abstract <plug> mapping?
   
   //{{{<Plug>. If typeahead starts with <Plug> then remap, even for a "noremap" mapping.
   // <Plug> mappings are abstract function names exposed by plugins that users can map concrete
   // keys to.
   // In a plugin:
   // nnoremap <silent> <plug>(SubversiveSubstitute) :<c-u>call subversive#...
   // In a user config: 
   // nmap gc <plug>(SubversiveSubstitute)
   // The plugin author can then change their RHS while the user won't need to change anything
   
   isAbstractPlugMapping = 
             typeBufG.validLen >= 3
          && typeBufG.c[typeBufG.currPos    ] == K_SPECIAL
          && typeBufG.c[typeBufG.currPos + 1] == KS_EXTRA
          && typeBufG.c[typeBufG.currPos + 2] == KE_PLUG;
       
   //}}} 
       
   MatchFinding fin = searchForPartialMappings(*foundKeylen, timedout, isAbstractPlugMapping);
   
   //{{{ May check for a terminal code when there is no mapping or only a partial
   // mapping.  Also check if there is a full mapping with <Esc>, unless timed
   // out, since that is nearly always a partial match with a terminal code.
   if ((fin.foundMapping == NULL 
          || (fin.maxMLen + fin.wantTermcode > fin.currLen)
          || (fin.currLen == 1 && *fin.foundMapping->rhs == ESC && !timedout)
        )
        && fin.keylen != KEYLEN_INCOMPLETE_MAPPING
   ) {
      int savedKeylen = fin.keylen;

      // When no matching mapping found or found a non-matching mapping that matches at least what
      // the matching mapping matched. Check if we have a terminal code, provided:
      // - mapping is allowed,
      // - keys have not been mapped,
      // - and not an ESC sequence, not in insert mode,
      // - and when not timed out.
      if (no_mapping == 0 || allow_keys != 0) {
           if ((typeBufG.mappedLen == 0 
                     || (typeBufG.noremap[typeBufG.currPos] == RM_YES))
                  && !timedout) {
              fin.keylen = termTryParseTermcode(fin.maxMLen + 1, (Text){NULL, 0}, NULL);
           } else {
              fin.keylen = 0;
           } 

           //If no termcode matched but 'pastetoggle' matched partially
           //it's like an incomplete key sequence.
           if (fin.keylen == 0 && savedKeylen == KEYLEN_INCOMPLETE_KEYCODE && !timedout)
              fin.keylen = KEYLEN_INCOMPLETE_KEYCODE;

           // If no termcode matched, try to include the modifier into the
           // key.  This is for when modifyOtherKeys is working.
           check_no_reduce_keys();  // may update the no_reduce_keys flag
           if (fin.keylen == 0 && !no_reduce_keys) {
              fin.keylen = checkSimplifyModifier(fin.maxMLen + 1);

              if (fin.keylen < 0) {    // insertIntoTypebuf() failed
                 return mrFail;
              }
           }

           // When getting a partial match, but the last characters were not
           // typed, don't wait for a typed character to complete the
           // termcode.  This helps a lot when a ":normal" command ends in an ESC.
           if (fin.keylen < 0 && typeBufG.validLen == typeBufG.mappedLen) {
              fin.keylen = 0;
           }
      } else {
         fin.keylen = 0;
      }

      if (fin.keylen == 0 && fin.foundMapping == NULL) {  // no matching terminal code
         // When there was a matching mapping and no termcode could be
         // replaced after another one, use that mapping (loop around).
         // If there was no mapping at all use the character from the
         // typeahead buffer right here.
         *foundKeylen = fin.keylen;
         return mrGet;    // get character from typeahead
      } ei (fin.keylen > 0) {       // full matching terminal code

         *foundKeylen = fin.keylen;
         return mrRetry;   // try mapping again
      }

      // Partial match: get some more characters.  When a matching mapping
      // was found use that one.
      fin.keylen = (fin.foundMapping == NULL || fin.keylen < 0) 
         ? KEYLEN_INCOMPLETE_KEYCODE : fin.currLen;
      
   }//}}}
   //{{{ full match
   
   
   if (fin.keylen >= 0 && fin.keylen <= typeBufG.validLen) {
      // write chars to script file(s)
      if (fin.keylen > typeBufG.mappedLen) {
         gotchars(typeBufG.c + typeBufG.currPos + typeBufG.mappedLen, fin.keylen - typeBufG.mappedLen);
      }

      cmd_silent = (typeBufG.silentCnt > 0);
      del_typebuf(fin.keylen, 0);   // remove the mapped keys

      //Put the replacement string in front of mapstr. 
      //The recursion depth check catches ":map x y" and ":map y x".
      if (++*mapdepth >= MAX_MAPPING_RECURSION) {
         emsg(_(e_recursive_mapping));
         if (stateG & MODE_COMMLINE)
            redrawCommline();
         else
            setcursor();
         flush_buffers(FLUSH_MINIMAL);
         *mapdepth = 0;   // for next one
         *foundKeylen = fin.keylen;
         return mrFail;
      }

      // Copy the values from *mp that are used, because evaluating the
      // expression may invoke a function that redefines the mapping, thereby
      // making *mp invalid.
      int isExpr = fin.foundMapping->expr;
      int isNoremap = fin.foundMapping->noremap;
      int isSilent = fin.foundMapping->silent;
      Arr(Byte) keys = NULL;  // only saved when needed

      Arr(Byte) altKeys = NULL;  // only saved when needed
      Arr(Byte) mapRhs;
      int altKeylen = fin.foundMapping->alt != NULL ? fin.foundMapping->alt->keylen : 0;

      //{{{ Handle ":map <expr>": evaluate the {rhs} as an expression.  Also
      // save and restore the command line for "normal :".
      if (isExpr) {
         int save_vgetcBusyG = vgetcBusyG;
         int save_may_garbage_collect = may_garbage_collect;
         int was_screen_col = screenCursColG;
         int was_screen_row = screenCursRowG;
         int prev_anyEmsgG = anyEmsgG;

         vgetcBusyG = 0;
         may_garbage_collect = false;

         keys = copySubstr(fin.foundMapping->rhs, (Unt)fin.foundMapping->keylen);
         altKeys = fin.foundMapping->alt != NULL 
            ? copySubstr(fin.foundMapping->alt->rhs, (Unt)altKeylen) : NULL;

         mapRhs = eval_map_expr(fin.foundMapping, ZERO);

         // The mapping may do anything, but we expect it to take care of
         // redrawing.  Do put the cursor back where it was.
         windgoto(was_screen_row, was_screen_col);
         out_flush();

         // If an error was displayed and the expression returns an empty
         // string, generate a <Nop> to allow for a redraw.
         if (prev_anyEmsgG != anyEmsgG && (mapRhs == NULL || *mapRhs == ZERO)) {
            Byte   buf[4];

            eeglFree(mapRhs);
            buf[0] = K_SPECIAL;
            buf[1] = KS_EXTRA;
            buf[2] = KE_IGNORE;
            buf[3] = ZERO;
            mapRhs = copySubstr(buf, 3);

            if (stateG & MODE_COMMLINE) {
               // redraw the command below the error
               msg_didout = true;
               if (msgRowG < commlineRowG)
                  msgRowG = commlineRowG;
               redrawcmd();
            }
         }

         vgetcBusyG = save_vgetcBusyG;
         may_garbage_collect = save_may_garbage_collect;
      //}}}
      } else
         { mapRhs = fin.foundMapping->rhs; }
          
      // Insert the 'to' part in the typeBufG. If 'from' field is the same as the start of the 
      // 'to' field, don't remap the first character (but do allow abbreviations).
      // If m_noremap is set, don't remap the whole 'to' part.
      if (mapRhs == NULL) {
         i = FAIL;
      } else {
           int noremap;
           last_used_map = fin.foundMapping;
           last_used_sid = -1;

           if (isNoremap != REMAP_YES) {
              noremap = isNoremap;
           } ei ( 
                 isExpr
                 ? ((keys != NULL && STRNCMP(mapRhs, keys, (Unt)fin.keylen) == 0)
                    || (altKeys != NULL && STRNCMP(mapRhs, altKeys, (Unt)altKeylen) == 0)
                 )
                 : (STRNCMP(mapRhs, fin.foundMapping->lhs, (Unt)fin.keylen) == 0
                    || (fin.foundMapping->alt != NULL 
                       && STRNCMP(
                             mapRhs, fin.foundMapping->alt->lhs, 
                             (Unt)fin.foundMapping->alt->keylen
                          ) == 0
                       )
                 )
            ) {
               noremap = REMAP_SKIP;
            } else {
               noremap = REMAP_YES;
            }      
            i = insertIntoTypebuf(mapRhs, noremap, 0, true, cmd_silent || isSilent);

            if (isExpr)
               eeglFree(mapRhs);
       }

       eeglFree(keys);
       eeglFree(altKeys);
       *foundKeylen = fin.keylen;
       return i == FAIL ? mrFail : mrRetry;
    }

   //}}}

   *foundKeylen = fin.keylen;
   return mrNoMatch;
}

//unget one character (can only be done once!)
//If the character was stuffed, vgetc() will get it next time it is called.
//Otherwise vgetc() will only get it when the stuff buffer is empty.
pub void
vungetc(Unt c) {
   old_char = c;
   oldModMask = modMaskG;
   old_mouse_row = mouseRowG;
   old_mouse_col = mouseColG;
   old_keyWasStuffedG = keyWasStuffedG;
}

// When peeking and not getting a character, reg_executing cannot be cleared
// yet, so set a flag to clear it later.
private void
check_end_reg_executing(int advance) {
   if (reg_executing != 0 && (typeBufG.mappedLen == 0 || pending_end_reg_executing)) {
      if (advance) {
         reg_executing = 0;
         pending_end_reg_executing = false;
      } else {
         pending_end_reg_executing = true;
      }
   }
}

// Get a byte:
// 1. from the stuffbuffer
//   This is used for abbreviated commands like "D" -> "d$". Also used to redo a command for ".".
// 2. from the typeahead buffer
//   Stores text obtained previously but not used yet. Also stores the result of mappings.
//   Also used for the ":normal" command.
// 3. from the user
//   This may do a blocking wait if "advance" is true.
//
// if "advance" is true (vgetc()):
//   Really get the character.
//   keyWasTypedG is set to true in the case the user typed the key.
//   keyWasStuffedG is true if the character comes from the stuff buffer.
// if "advance" is false (vpeekc()):
//   Just look whether there is a character available. Return ZERO if not.
//
//When "no_mapping" is zero, checks for mappings in the current mode. Only returns one byte (of 
//a multi-byte character). K_SPECIAL and CSI may be escaped, need to get two more bytes then.
private Unt
vGetOrPeek(Boole advance) {
   int countRead;
   Unt specialChar;
   int timedout = false; // waited for more than 'timeoutlen'
                         // for mapping to complete or 'ttimeoutlen' for complete key code
   int mapdepth = 0;     // check for recursive mapping
   int mode_deleted = false;   // set when mode has been deleted
   int necursorCol, necursorRow;
   int n;
   int old_wcol, old_wrow;
   int wait_tb_len;

   //This function doesn't work very well when called recursively. This may happen though, due to:
   //1. The call to add_to_showcmd().   char_avail() is then used to check if there is a 
   //character available, which calls this function. In that case we must return ZERO, to 
   //indicate no character is available.
   //2. A GUI callback function writes to the screen, causing a wait_return(). Using ":normal" 
   //can also do this, but it saves the typeahead buffer, thus it should be OK.  But don't get 
   //a key from the user then.
   if (vgetcBusyG > 0 && ex_normal_busy == 0) {
      return ZERO;
   }

   ++vgetcBusyG;

   if (advance) {
      keyWasStuffedG = false;
      typebuf_was_empty = false;
   }
   initTypebuf();

   start_stuff();
   check_end_reg_executing(advance);
   //{{{ main loop
   do {
      // get a character: 1. from the stuffbuffer
      if (TYPEAHEAD_CHAR != 0) {
          countRead = TYPEAHEAD_CHAR;
          if (advance) {
             TYPEAHEAD_CHAR = 0;
          }
      } else
         { countRead = read_readbuffers(advance); }


      if (countRead != ZERO && !gotInterruptG) {
           if (advance) {
              // keyWasTypedG = false;  When the command that stuffed something
              // was typed, behave like the stuffed command was typed.
              // needed for CTRL-W CTRL-] to open a fold, for example.
              keyWasStuffedG = true;
           }
           if (typeBufG.noAbbrCnt == 0) {
              typeBufG.noAbbrCnt = 1;   // no abbreviations now
           }
      } else {
            for (;;) { //{{{inner loop
               //Loop until we either find a matching mapped key, or we are sure that it is not
               //a mapped key. If a mapped key sequence is found we go back to the start to try
               //re-mapping.
               long   wait_time;
               int   keylen = 0;
               int   showcmd_idx;
               check_end_reg_executing(advance);

               // ui_breakcheck() is slow, don't use it too often when inside a mapping.  But call
               //  it each time for typed characters.
               if (typeBufG.mappedLen) {
                  line_breakcheck();
               } else {
                  ui_breakcheck();      // check for CTRL-C
               }

               //{{{ interrupt
               if (gotInterruptG) {
                  // flush all input
                  countRead = ingestChar(typeBufG.c, typeBufG.len - 1, 0L);

                  //If ingestChar() returns true (script file was active) or we are inside a mapping,
                  //get out of Insert mode. Otherwise we behave like having gotten a CTRL-C.
                  //As a result typing CTRL-C in insert mode will really insert a CTRL-C.
                  if ((countRead || typeBufG.mappedLen) && (stateG & (MODE_INSERT | MODE_COMMLINE))) {
                     specialChar = ESC;
                  } else {
                     specialChar = Ctrl_C;
                  }
                  flush_buffers(FLUSH_INPUT);   // flush all typeahead

                  if (advance) {
                     // Also record this character, it might be needed to get out of Insert mode.
                     *typeBufG.c = specialChar;
                     gotchars(typeBufG.c, 1);
                  }
                  cmd_silent = false;

                  break;
               //}}}
               } ei (typeBufG.validLen > 0) {
                  // Check for a mapping in "typeBufG".
                  MapResult result = handleMapping(OUT &keylen, timedout, OUT &mapdepth);
                  if (result == mrRetry) {
                     // try mapping again
                     continue;
                  } ei (result == mrFail) {
                     // failed, use the outer loop
                     countRead = -1;
                     break;
                  } ei (result == mrGet) {
                     // get a character: 2. from the typeahead buffer
                     countRead = typeBufG.c[typeBufG.currPos];
                     if (advance)  {  // remove chars from typeBufG
                        cmd_silent = (typeBufG.silentCnt > 0);
                        if (typeBufG.mappedLen > 0) {
                           keyWasTypedG = false;
                        } else {
                           keyWasTypedG = true;
                           // write char to script file(s)
                           gotchars(typeBufG.c + typeBufG.currPos, 1);
                        }
                        keyNoremapG = typeBufG.noremap[typeBufG.currPos];
                        del_typebuf(1, 0);
                     }
                     break;  // got character, break the inner loop
                  }
                  // mrNoMatch not enough characters, get more
               }

               // <Esc> in INSERT mode
               //get a character: 3. from the user - handle <Esc> in Insert mode
               // Special case: if we get an <ESC> in Insert mode and there are no more characters 
               // at once, we pretend to go out of Insert mode.  This prevents the one second 
               // delay after typing an <ESC>.  If we get something after all, we may have to 
               // redisplay the mode. That the cursor is in the wrong place does not matter.
               // Do not do this if the kitty keyboard protocol is used, every <ESC> is the start
               // of an escape sequence then.
               countRead = 0;
               necursorCol = curPor->cursorCol;
               necursorRow = curPor->cursorRow;
               
               if (countRead < 0) {
                  continue;   // end of input script reached
               } 
               // Allow mapping for just typed characters. When we get here, countRead is 
               // the number of extra bytes and typeBufG.validLen is 1.
               for (n = 1; n <= countRead; ++n)
                  typeBufG.noremap[typeBufG.currPos + n] = RM_YES;
               typeBufG.validLen += countRead;

               // buffer full, don't map
               if (typeBufG.validLen >= typeBufG.mappedLen + MAXMAPLEN) {
                  timedout = true;
                  continue;
               }

               // No typeahead left and inside ":normal".  Must return something to avoid 
               // getting stuck. When an incomplete mapping is present, behave like it timed 
               // out.
               if (ex_normal_busy > 0) {
                   static int tc = 0;
                   if (typeBufG.validLen > 0) {
                      timedout = true;
                      continue;
                   }

                   // Use CTRL-L to make edit() return. For the command line only CTRL-C always 
                   // breaks it.
                   // For the commline portal: Alternate between ESC and CTRL-C: ESC for most 
                   // situations and CTRL-C to close the commline portal.
                   if (terminal_is_active())
                      specialChar = K_CANCEL;
                   ei ((stateG & MODE_COMMLINE) || (commPortTypeG > 0 && tc == ESC))
                      specialChar = Ctrl_C;
                   else
                      specialChar = ESC;
                   tc = specialChar;
                   // set a flag to indicate this wasn't a normal char
                   if (advance)
                      typebuf_was_empty = true;

                   // no chars to block abbreviation for
                   typeBufG.noAbbrCnt = 0;
                   break;
               }

               //get a character: 3. from the user - update display
               //In Insert mode, a screen update is skipped when characters are still available. 
               //But when those available characters are part of a mapping, and we are going to 
               //do a blocking wait here. Need to update the screen to display the changed text
               //so far. Also for when 'lazyredraw' is set and redrawing was postponed because 
               //there was something in the input buffer (e.g., termresponse).
               if (((stateG & MODE_INSERT) != 0 || p_lz)
                     && (stateG & MODE_COMMLINE) == 0 && advance && mustRedrawG != 0 
                     && !need_wait_return
                     ) {
                  drawUpdateScreen(0);
                  setcursor(); // put cursor back where it belongs
               }

               // If we have a partial match (and are going to wait for more input from the user),
               // show the partially matched characters to the user with showcmd.
               showcmd_idx = 0;
               int showing_partial = false;
               if (typeBufG.validLen > 0 && advance) {
                   if (((stateG & (MODE_NORMAL | MODE_INSERT)) || stateG == MODE_LANGMAP)
                      && stateG != MODE_HITRETURN
                   ) {
                      // this looks nice when typing a dead character map
                      if (stateG & MODE_INSERT
                           && bookPtr2Cells(typeBufG.c + typeBufG.currPos + typeBufG.validLen - 1) == 1
                      ) {
                         edit_putchar(typeBufG.c[typeBufG.currPos + typeBufG.validLen - 1], false);
                         setcursor(); // put cursor back where it belongs
                         showing_partial = true;
                      }
                      // need to use the col and row from above here
                      old_wcol = curPor->cursorCol;
                      old_wrow = curPor->cursorRow;
                      curPor->cursorCol = necursorCol;
                      curPor->cursorRow = necursorRow;
                      push_showcmd();
                      if (typeBufG.validLen > SHOWCMD_COLS)
                          showcmd_idx = typeBufG.validLen - SHOWCMD_COLS;
                      while (showcmd_idx < typeBufG.validLen) {
                          addByteToShowcmd(typeBufG.c[typeBufG.currPos + showcmd_idx++]);
                      }
                      curPor->cursorCol = old_wcol;
                      curPor->cursorRow = old_wrow;
                   }

                   //This looks nice when typing a dead character map.
                   //There is no actual command line for get_number().
                   if ((stateG & MODE_COMMLINE)
                         && getCommlineInfo()->commBuf != NULL
                         && bookPtr2Cells(typeBufG.c + typeBufG.currPos + typeBufG.validLen - 1) == 1
                   ) {
                      putcmdline(typeBufG.c[typeBufG.currPos + typeBufG.validLen - 1], false);
                      showing_partial = true;
                   }
               }

               // get a character: 3. from the user - get it
               if (typeBufG.validLen == 0)
                  // timedout may have been set if a mapping with empty RHS
                  // fully matched while longer mappings timed out.
                  { timedout = false; }

               if (advance) {
                  if (typeBufG.validLen == 0
                        || !(p_timeout || (p_ttimeout && keylen == KEYLEN_INCOMPLETE_KEYCODE)))
                     // blocking wait
                     wait_time = -1L;
                  ei (keylen == KEYLEN_INCOMPLETE_KEYCODE && p_ttm >= 0)
                     wait_time = p_ttm;
                  else
                     wait_time = p_tm;
               } else
                  { wait_time = 0; }
               wait_tb_len = typeBufG.validLen;

               // getting raw input from keyboard
               countRead = ingestChar(
                  typeBufG.c + typeBufG.currPos + typeBufG.validLen,
                  typeBufG.len - typeBufG.currPos - typeBufG.validLen - 1,
                  wait_time
               );

               if (showcmd_idx != 0) {
                  pop_showcmd();
               }
               if (showing_partial) {
                   if (stateG & MODE_INSERT)
                      edit_unputchar();
                   if ((stateG & MODE_COMMLINE) && getCommlineInfo()->commBuf != NULL)
                      unputcmdline();
                   else
                      setcursor(); // put cursor back where it belongs
               }

               if (countRead < 0) {
                  continue;        // end of input script reached
               } ei (countRead == ZERO)  {  // no character available
                  if (!advance) {
                     break;
                  } ei (wait_tb_len > 0) { // timed out
                     timedout = true;
                     continue;
                  }
               } else {       // allow mapping for just typed characters
                  while (typeBufG.c[typeBufG.currPos + typeBufG.validLen] != ZERO) {
                     typeBufG.noremap[typeBufG.currPos + typeBufG.validLen] = RM_YES;
                     typeBufG.validLen++;
                  }
               }
            } //}}} inner loop
      } // if (!character from stuffbuf)

      // if advance is false don't loop on NULs
   } while ((countRead < 0 && specialChar != K_CANCEL) || (advance && countRead == ZERO));
   //}}} main loop

   //The "INSERT" message is taken care of here:
   //   if we return an ESC to exit insert mode, the message is deleted
   //   if we don't return an ESC but deleted the message before, redisplay it
   if (advance && p_smd && msg_silent == 0 && (stateG & MODE_INSERT)) {
      if (specialChar == ESC && !mode_deleted && !no_mapping && isModeDisplayedG) {
         if (typeBufG.validLen && !keyWasTypedG)
            redrawCommlineG = true; // delete mode later
         else
            unshowmode(false);
      } ei (specialChar != ESC && mode_deleted) {
         if (typeBufG.validLen && !keyWasTypedG)
            redrawCommlineG = true; // show mode later
         else
            showmode();
      }
   }
   if (timedout && specialChar == ESC)  {
      // When recording there will be no timeout.  Add an <Ignore> after the
      // ESC to avoid that it forms a key code with following characters.
      gotchars_ignore();
   }

   --vgetcBusyG;

   return countRead;
}

//ingestChar() - get one character from
//   1. a scriptfile
//   2. the keyboard
//
//As many characters as we can get (up to 'maxlen') are put in "buf" and ZERO terminated
//(buffer length must be 'maxlen' + 1). Minimum for "maxlen" is 3!!!!
//
//"changeCnt" is the value of typeBufG.changeCnt if "buf" points into it. When 
//typeBufG.changeCnt changes (e.g., when a message is received from a remote client) "buf" 
//can no longer be used.  "changeCnt" is 0 otherwise.
//
//If we got an interrupt all input is read until none is available.
//
//If wait_time == 0  there is no waiting for the char.
//If wait_time == n  we wait for n msec for a character to arrive.
//If wait_time == -1 we wait forever for a character to arrive.
//
//Return the number of obtained characters, or -1 when end of input script reached.
private int
ingestChar(CS buf, int maxlen, long wait_time) {  // "wait_time" milliseconds
   int len = 0;
   int retesc = false; // return ESC when we got an interrupt
   int changeCnt = typeBufG.changeCnt;
   if (wait_time == -1L || wait_time > 100L) { // flush output before waiting
       cursor_on();
       out_flush();
   }

   //Don't reset these when at the hit-return prompt, otherwise a endless
   //recursive loop may result (write error in swapfile, hit-return, timeout
   //on char wait, flush swapfile, write error....).
   if (stateG != MODE_HITRETURN) {
      did_outofmem_msg = false;   // display out of memory message (again)
      did_swapwrite_msg = false;  // display swap file write error again
   }
   undo_off = false;          // restart undo now

   // Get a character from a script file if there is one.
   // If interrupted: Stop reading script files, close them all.
   int scriptChar = -1;
   while (scriptin[curscript] != NULL && scriptChar < 0 && !ignore_script ) {
      parse_queued_messages();

      if (gotInterruptG || (scriptChar = getc(scriptin[curscript])) < 0) {
         // Reached EOF. Careful: closeScript() frees typeBufG.c[] and buf[] may
         // point inside typeBufG.c[].  Don't use buf[] after this!
         closeScript();
         // When reading script file is interrupted, return an ESC to get back to normal mode.
         // Otherwise return -1, because typeBufG.c[] has changed.
         if (gotInterruptG)
           retesc = true;
         else
           return -1;
      } else {
         buf[0] = scriptChar;
         len = 1;
      }
   }

   if (scriptChar < 0) {  // did not get a character from script
      //If we got an interrupt, skip all previously typed characters and return true if quit 
      //reading script file. Stop reading typeahead when a single CTRL-C was read, 
      //fill_input_buf() returns this when not able to read from stdin. Don't use buf[] here, 
      //closeScript() may have freed typeBufG.c[] and buf may be pointing inside typeBufG.c[].
      if (gotInterruptG) {
          #define DUM_LEN (MAXMAPLEN * 3 + 3)
          Byte dummy[DUM_LEN + 1];

          for (;;) {
             len = ui_inchar(dummy, DUM_LEN, 0L, 0);
             if (len == 0 || (len == 1 && dummy[0] == Ctrl_C))
                break;
          }
          return retesc;
      }

      // Always flush the output characters when getting input characters
      // from the user and not just peeking.
      if (wait_time == -1L || wait_time > 10L)
         out_flush();

      // Fill up to a third of the buffer, because each character may be tripled below.
      len = ui_inchar(OUT buf, maxlen / 3, wait_time, changeCnt);
   }

   // If the typeBufG was changed further down, it is like nothing was added by this call.
   if (typebuf_changed(changeCnt)) {
      return 0;
   }

   //Note the change in the typeahead buffer, this matters for when
   //vGetOrPeek() is called recursively, e.g. using getchar(1) in a timer function.
   if (len > 0 && ++typeBufG.changeCnt == 0)
      typeBufG.changeCnt = 1;

   int count = fixInputBuffer(OUT buf, len);

   return count;
}

// Fix typed characters for use by vgetc() and termTryParseTermcode(). "buf[]" must have room to triple 
// the number of bytes! Return the new length.
private int
fixInputBuffer(OUT CS buf, int len) {
   CS p = buf;

   //3 characters are special: ZERO, CSI and K_SPECIAL.
   //Replace        ZERO by K_SPECIAL KS_ZERO    KE_FILLER
   //Replace K_SPECIAL by K_SPECIAL KS_SPECIAL KE_FILLER
   //Replace       CSI by K_SPECIAL KS_EXTRA   KE_CSI
   for (int i = len; --i >= 0; ++p) {
      if (p[0] == ZERO 
            || (p[0] == K_SPECIAL && (i < 2 || p[1] != KS_EXTRA || p[2] != (int)KE_CURSORHOLD))
            // timeout may generate K_CURSORHOLD
      ) {
         MEMMOVE(p + 3, p + 1, (Unt)i);
         p[2] = K_THIRD(p[0]);
         p[1] = K_SECOND(p[0]);
         p[0] = K_SPECIAL;
         p += 2;
         len += 2;
      }
   }

   *p = ZERO;      // add trailing ZERO
   return len;
}

#if defined(USE_INPUT_BUF) || defined(PROTO)
// Return true when bytes are in the input buffer or in the typeahead buffer.
// Normally the input buffer would be sufficient, but the server_to_input_buf()
// or feedkeys() may insert characters in the typeahead buffer while we are
// waiting for input to arrive.
pub int
input_available(void) {
   return (!eeIsInputBufEmpty()|| typebuf_was_filled);
}
#endif

// Function passed to doCommand() to get the command after a <Cmd> key from typeahead.
private CS
getCommandNameCb(
   Unt promptc UNUSED,
   void* cookie UNUSED,
   int indent UNUSED,
   GetlineAlgo do_concat UNUSED
) {
   ArrayList   line_ga;
   Unt c1 = UNT;
   Unt c2;
   int cmod = 0;
   int aborted = false;
   ga_init2(&line_ga, 1, 32);
   // no mapping for these characters
   no_mapping++;

   gotInterruptG = false;
   while (c1 != ZERO && !aborted) {
      if (ga_grow(&line_ga, 32) == FAIL) {
         aborted = true;
         break;
      }

      if (vGetOrPeek(false) == ZERO) {
         //incomplete <Cmd> is an error, because there is not much the user could do in this state.
         emsg(_(e_cmd_mapping_must_end_with_cr));
         aborted = true;
         break;
      }

      //Get one character at a time.
      c1 = vGetOrPeek(true);
      //Get two extra bytes for special keys
      if (c1 == K_SPECIAL) {
         c1 = vGetOrPeek(true);
         c2 = vGetOrPeek(true);
         if (c1 == KS_MODIFIER) {
            cmod = c2;
            continue;
         }
         c1 = TO_SPECIAL(c1, c2);
         
         //K_ESC is used to avoid ambiguity with the single Esc character
         //that might be the start of an escape sequence.  Convert it back to a single Esc here.
         if (c1 == K_ESC)
            { c1 = ESC; }
      }

      if (gotInterruptG) {
         aborted = true;
      } ei (c1 == '\r' || c1 == '\n') {
         c1 = ZERO;   // end the line
      } ei (c1 == ESC) {
         aborted = true;
      } ei (c1 == K_COMMAND || c1 == K_SCRIPT_COMMAND) {
         // give a nicer error message for this special case
         emsg(_(e_cmd_mapping_must_end_with_cr_before_second_cmd));
         aborted = true;
      } ei (c1 == K_SNR)   {
         ga_concat(&line_ga, (CS)"<SNR>");
      } else {
         if (cmod != 0) {
            ga_append(&line_ga, K_SPECIAL);
            ga_append(&line_ga, KS_MODIFIER);
            ga_append(&line_ga, cmod);
         }
         if (IS_SPECIAL(c1)) {
            ga_append(&line_ga, K_SPECIAL);
            ga_append(&line_ga, K_SECOND(c1));
            ga_append(&line_ga, K_THIRD(c1));
         } else {
            ga_append(&line_ga, c1);
         }
      }
      cmod = 0;
   }

   no_mapping--;

   if (aborted)
      { ga_clear(&line_ga); }

   return (CS)line_ga.c;
}

// If there was a mapping we get its SID.  Otherwise, use "last_used_sid", it is set when redo'ing.
// Put this SID in the redo buffer, so that "." will use the same script context.
pub void
may_add_last_used_map_to_redobuff(void) {
   Byte  buf[3 + 20];
   int buflen;
   int sid = -1;

   if (last_used_map != NULL)
      { sid = last_used_map->scriptCtx.sid; }
   if (sid < 0)
      { sid = last_used_sid; }

   if (sid < 0)
      { return; } 

   // <K_SID>{nr};
   buf[0] = K_SPECIAL;
   buf[1] = KS_EXTRA;
   buf[2] = KE_SID;
   buflen = 3;

   buflen += eeSnprintf(buf + 3, 20, "%d;", sid);
   add_buff(&redobuff, buf, (long)buflen);
}

// return FAIL or OK
pub int
do_cmdkey_command(Unt key, Unt flags) {
   ScriptPos  save_scriptPosG = {-1, 0, 0};
   if (key == K_SCRIPT_COMMAND && (last_used_map || SCRIPT_ID_VALID(last_used_sid))) {
      save_scriptPosG = scriptPosG;
      if (last_used_map) {
         scriptPosG = last_used_map->scriptCtx;
      } else {
         scriptPosG.sid = last_used_sid;
         scriptPosG.lineNr = 0;
      }
   }

   int res = doCommand(null, getCommandNameCb, null, flags);

   if (save_scriptPosG.sid >= 0)
       { scriptPosG = save_scriptPosG; }

   return res;
}

pub void
reset_last_used_map(MapBlock* mp) {
   if (last_used_map != mp)
      { return; }

   last_used_map = NULL;
   last_used_sid = -1;
}

//Return the number of character cells string "s[len]" will take on the
//screen, counting TABs as two characters: "^I".
pub int
eeglStrNsize(CS s, int len) {
   int size = 0;

   while (*s != ZERO && --len >= 0) {
      int l = utfCharLen(s);

      size += bookPtr2Cells(s);
      s += l;
      len -= l - 1;
   }

   return size;
}

//Return the number of character cells string "s" will take on the screen,
//counting TABs as two characters: "^I".
pub int
eeglStrSize(CS s) {
   return eeglStrNsize(s, (int)MAXCOL);
}

//return true if 'c' is a valid file-name character or a wildcard character
//Assume characters above 0x100 are valid (multi-byte).
//Explicitly interpret ']' as a wildcard character as mch_has_wildcard("]") returns false.
pub int
eeIsFnameChar_or_wc(Unt c) {
   Byte buf[2] = {(Byte)c, ZERO};
   return eeIsFnameChar(c) || c == ']' || mch_has_wildcard(buf);
}

//Return true if line "lnum" ends in a white character.
pub int
ends_in_white(LineNr lnum) {
   CS s = ml_get(lnum);
   if (*s == ZERO)
      return false;
   Unt l = ml_get_len(lnum) - 1;
   return SPACE_OR_TAB(s[l]);
}

//Return true if the two comment leaders given are the same.  "lnum" is
//the first line.  White-space is ignored.  Note that the whole of
//'leader1' must match 'leader2_len' characters from 'leader2' -- webb
pub int
same_leader(LineNr lnum, int leader1_len, CS leader1_flags, int leader2_len, CS leader2_flags){
   int idx1 = 0, idx2 = 0;

   if (leader1_len == 0)
      return (leader2_len == 0);

   // If first leader has 'f' flag, the lines can be joined only if the
   // second line does not have a leader.
   // If first leader has 'e' flag, the lines can never be joined.
   // If first leader has 's' flag, the lines can only be joined if there is
   // some text after it and the second line has the 'm' flag.
   if (leader1_flags) {
      for (CS p = leader1_flags; *p && *p != ':'; ++p) {
         if (*p == COM_FIRST)
            return (leader2_len == 0);
         if (*p == COM_END)
            return false;
         if (*p == COM_START) {
            int line_len = ml_get_len(lnum);
            if (line_len <= leader1_len  || leader2_flags == NULL || leader2_len == 0)
               return false;
            for (p = leader2_flags; *p && *p != ':'; ++p) {
               if (*p == COM_MIDDLE)
                  return true;
            } 
            return false;
         }
      }
   }

   // Get current line and next line, compare the leaders.
   // The first line has to be saved, only one line can be locked at a time.
   CS line1 = copySubstr(ml_get(lnum), ml_get_len(lnum));
   for (idx1 = 0; SPACE_OR_TAB(line1[idx1]); ++idx1)
      {} 
      
   CS line2 = ml_get(lnum + 1);
   for (idx2 = 0; idx2 < leader2_len; ++idx2) {
      if (!SPACE_OR_TAB(line2[idx2])) {
         if (line1[idx1++] != line2[idx2])
            break;
      } else {
         while (SPACE_OR_TAB(line1[idx1]))
           ++idx1;
      } 
   }
   eeglFree(line1);
   return (idx2 == leader2_len && idx1 == leader1_len);
}

// getwhitecols: return the number of whitespace columns (bytes) at the start of a given line
pub int
getwhitecols_curline(void) {
   return getwhitecols(ml_get_curline());
}

//Format "line_count" lines, starting at the cursor position.
//When "line_count" is negative, format until the end of the paragraph.
//Lines after the cursor line are saved for undo, caller must have saved the first line.
pub void
format_lines(LineNr   line_count, int avoid_fex) { // don't use 'formatexpr'
   int is_not_par;      // current line not part of parag.
   int next_is_not_par;   // next line not part of paragraph
   int is_end_par;      // at end of paragraph
   int prev_is_end_par = false;// prev. line not part of parag.
   int next_is_start_par = false;
   int leader_len = 0;      // leader len of current line
   int next_leader_len;   // leader len of next line
   CS leader_flags = NULL;   // flags for leader of current line
   CS next_leader_flags = NULL; // flags for leader of next line
   int doCommentsList = 0;   // format comments with 'n' or '2'
   int advance = true;
   int second_indent = -1;   // indent for second line (comment aware)
   int first_par_line = true;
   int smd_save;
   long count;
   int need_set_indent = true;   // set indent of next paragraph
   LineNr first_line = curPor->cursor.lnum;
   int force_format = false;
   int old_State = stateG;

   // length of a line to force formatting: 3 * 'tw'
   int max_len = comp_textwidth(true) * 3;

   // check for 'q', '2', 'n' and 'w' in 'formatoptions'
   Boole doComments = has_format_option(FO_Q_COMS); // format comments?
   Boole do_second_indent = has_format_option(FO_Q_SECOND);
   Boole do_number_indent = has_format_option(FO_Q_NUMBER);
   Boole do_trail_white = has_format_option(FO_WHITE_PAR);

   // Get info about the previous and current line.
   if (curPor->cursor.lnum > 1)
      is_not_par = fmt_check_par(
            curPor->cursor.lnum - 1 , OUT &leader_len, OUT &leader_flags, doComments
      );
   else
      is_not_par = true;
   next_is_not_par = fmt_check_par(
         curPor->cursor.lnum, OUT &next_leader_len, OUT &next_leader_flags, doComments
   );
   is_end_par = (is_not_par || next_is_not_par);
   if (!is_end_par && do_trail_white)
      is_end_par = !ends_in_white(curPor->cursor.lnum - 1);

   curPor->cursor.lnum--;
   for (count = line_count; count != 0 && !gotInterruptG; --count) {
      // Advance to next paragraph.
      if (advance) {
         curPor->cursor.lnum++;
         prev_is_end_par = is_end_par;
         is_not_par = next_is_not_par;
         leader_len = next_leader_len;
         leader_flags = next_leader_flags;
      }

      // The last line to be formatted.
      if (count == 1 || curPor->cursor.lnum == curBook->mem.lineCount) {
         next_is_not_par = true;
         next_leader_len = 0;
         next_leader_flags = NULL;
      } else {
         next_is_not_par = fmt_check_par(
               curPor->cursor.lnum + 1, OUT &next_leader_len, OUT &next_leader_flags, doComments
         );
         if (do_number_indent)
            next_is_start_par = (get_number_indent(curPor->cursor.lnum + 1) > 0);
      }
      advance = true;
      is_end_par = (is_not_par || next_is_not_par || next_is_start_par);
      if (!is_end_par && do_trail_white)
         is_end_par = !ends_in_white(curPor->cursor.lnum);

      // Skip lines that are not in a paragraph.
      if (is_not_par) {
         if (line_count < 0)
         break;
      } else {
          // For the first line of a paragraph, check indent of second line.
          // Don't do this for comments and empty lines.
         if (first_par_line
             && (do_second_indent || do_number_indent)
             && prev_is_end_par
             && curPor->cursor.lnum < curBook->mem.lineCount
         )  {
           if (do_second_indent && !LINEEMPTY(curPor->cursor.lnum + 1)) {
               if (leader_len == 0 && next_leader_len == 0) {
                  // no comment found
                  second_indent = get_indent_lnum(curPor->cursor.lnum + 1);
               }
               else {
                  second_indent = next_leader_len;
                  doCommentsList = 1;
               }
            } ei (do_number_indent) {
               if (leader_len == 0 && next_leader_len == 0) { // no comment found
                  second_indent = get_number_indent(curPor->cursor.lnum);
               } else { // get_number_indent() is now "comment aware"...
                  second_indent = get_number_indent(curPor->cursor.lnum);
                  doCommentsList = 1;
               }
            }
         }

         // When the comment leader changes, it's the end of the paragraph.
         if (curPor->cursor.lnum >= curBook->mem.lineCount
             || !same_leader(curPor->cursor.lnum,
                  leader_len, leader_flags,
                     next_leader_len, next_leader_flags)
         ) {
            //Special case: If the next line starts with a line comment and this line has a line 
            //comment after some text, the paragraf doesn't really end.
            if (next_leader_flags == NULL
               || STRNCMP(next_leader_flags, "://", 3) != 0
               || check_linecomment(ml_get_curline()) == MAXCOL)
            is_end_par = true;
         }

         //If we have got to the end of a paragraph, or the line is
         //getting long, format it.
         if (is_end_par || force_format) {
            if (need_set_indent) {
               int      indent = 0; // amount of indent needed

               // Replace indent in first line of a paragraph with minimal
               // number of tabs and spaces, according to current options.
               // For the very first formatted line keep the current indent.
               if (curPor->cursor.lnum == first_line)
                  indent = get_indent();
               else {
                 if (jugIsIndentationExpressionBased()) {
                     indent = curBook->o.indentExpr ? get_expr_indent() : get_indent();
                 } else
                     indent = get_indent();
               }
               (void)set_indent(indent, SIN_CHANGED);
            }

            // put cursor on last non-space
            stateG = MODE_NORMAL;   // don't go past end-of-line
            coladvance((ColNr)MAXCOL);
            while (curPor->cursor.col && isSpace(gchar_cursor()))
               dec_cursor();

            // do the formatting, without 'showmode'
            stateG = MODE_INSERT;   // for openLine()
            smd_save = p_smd;
            p_smd = false;

            insertchar0(
                  ZERO, INSCHAR_FORMAT + (doComments ? INSCHAR_DO_COM : 0)
                     + (doComments && doCommentsList ? INSCHAR_COM_LIST : 0)
                     + (avoid_fex ? INSCHAR_NO_FEX : 0),
                  second_indent
            );

            stateG = old_State;
            p_smd = smd_save;
            // Cursor and mouse shape shapes may have been updated (e.g. by
            // :normal) in insertchar0(), so they need to be updated here.
            ui_cursor_shape();
            second_indent = -1;
            // at end of par.: need to set indent of next par.
            need_set_indent = is_end_par;
            if (is_end_par) {
               // When called with a negative line count, break at the end of the paragraph.
               if (line_count < 0)
                  break;
               first_par_line = true;
            }
            force_format = false;
         }

         // When still in same paragraph, join the lines together.  But
         // first delete the leader from the second line.
         if (!is_end_par) {
            advance = false;
            curPor->cursor.lnum++;
            curPor->cursor.col = 0;
            if (line_count < 0 && u_save_cursor() == FAIL)
               break;
            if (next_leader_len > 0) {
               (void)del_bytes((long)next_leader_len, false, false);
               mark_col_adjust(curPor->cursor.lnum, (ColNr)0, 0L, (long)-next_leader_len, 0);
            } ei (second_indent > 0) { // the "leader" for FO_Q_SECOND
               int indent = getwhitecols_curline();

               if (indent > 0) {
                  (void)del_bytes(indent, false, false);
                   mark_col_adjust(curPor->cursor.lnum, (ColNr)0, 0L, (long)-indent, 0);
               }
            }
            curPor->cursor.lnum--;
            if (jugJoinLinesUnderCursor(2, true, false, false, false) == FAIL) {
               beep_flush();
               break;
            }
            first_par_line = false;
            // If the line is getting long, format it next time
            if (ml_get_curline_len() > max_len)
               force_format = true;
            else
               force_format = false;
         }
      }
      line_breakcheck();
   }
}

//}}}
//{{{multibyte characters

//The encoding used in the core is set with 'encoding'.  When 'encoding' is
//changed, the following four variables are set (for speed).
//Currently these types of character encodings are supported:
//
//use Unicode characters in UTF-8 encoding.
//        The cell width on the display needs to be determined from
//        the character value.
//        Recognizing bytes is easy: 0xxx.xxxx is a single-byte
//        char, 10xx.xxxx is a trailing byte, 11xx.xxxx is a leading
//        byte of a multi-byte character.
//        To make things complicated, up to six composing characters
//        are allowed.  These are drawn on top of the first char.
//        For most editing the sequence of bytes with composing
//        characters included is considered to be one character.
//        Internally characters are stored in UTF-8 encoding to
//        avoid ZERO bytes.  Conversion happens when doing I/O.
//
//
//If none of these is true, 8-bit bytes are used for a character.  The
//encoding isn't currently specified (TODO).
//
//'encoding' specifies the encoding used in the core.  This is in registers,
//text manipulation, buffers, etc.  Conversion has to be done when characters
//in another encoding are received or send:
//
//                 clipboard
//                       ^
//                       | (2)
//                       V
//                +---------------+
//           (1)  |               | (3)
// keyboard ----->|     core      |-----> display
//                |               |
//                +---------------+
//                       ^
//                       | (4)
//                       V
//                     file
//
//(1) Typed characters arrive in the current locale.
//
//The eeglinfo file is a special case: Only text is converted, not file names.


pub int mb_ptr2cells_len(CS p, int size);

//Set up for using multi-byte characters. Called in three cases:
//- by main() to initialize
//- by set_init_1() after 'encoding' was set to its default.
//- by do_set() when 'encoding' has been set.
//Fill utf8CharLens[] and return NULL when there are no problems. When there is something wrong:
//Return an error message and don't change anything.
pub CS
inputInitCharLens(void) {
   // The cell width depends on the type of multi-byte characters.
   (void)bookInitCharsForKeywordsForCurbook();

   screenalloc(false);
   // GNU gettext 0.10.37 supports this feature: set the codeset used for
   // translated messages independently from the current locale.
   (void)bind_textdomain_codeset(EEGLPACKAGE, "utf-8");

   return NULL;
}

//Get class of pointer:
//0 for blank or ZERO
//1 for punctuation
//2 for an alphanumeric word character
//>2 for other word characters, including CJK and emoji
pub int
mb_get_class(CS p) {
   return inpGetClassForBook(p, curBook);
}

pub int
inpGetClassForBook(CS p, Book* book) {
   if (utf8CharLens[p[0]] == 1) {
      if (p[0] == ZERO || SPACE_OR_TAB(p[0]))
         return 0;
      if (eeIsWordc_buf(p[0], book))
         return 2;
      return 1;
   }
   return utf_class_buf(mb_ptr2char(p), book);
}
#ifdef PROTO
//Check if the character pointed to by "p2" is a composing character when it
//comes after "p1".  For Arabic sometimes "ab" is replaced with "c", which
//behaves like a composing character.
pub int
utf_composinglike(CS p1, CS p2) {
   int c2 = mb_ptr2char(p2);
   if (utf_iscomposing(c2))
      return true;
   if (!arabic_maycombine(c2))
      return false;
   return arabic_combine(mb_ptr2char(p1), c2);
}
#endif

//Convert a UTF-8 byte string to a wide character. Also get up to MAX_COMBINED_SYMBOLS
//composing characters.
pub int
utfc_ptr2char(CS p, OUT int* pcc) {   // return: composing chars, last one is 0
   int cc;
   int i = 0;

   Unt c = mb_ptr2char(p);
   int len = utf_ptr2len(p);

   // Only accept a composing char when the first char isn't illegal.
   if ((len > 1 || *p < 0x80) && p[len] >= 0x80 && UTF_COMPOSINGLIKE(p, p + len)) {
      cc = mb_ptr2char(p + len);
      for (;;) {
         pcc[i++] = cc;
         if (i == MAX_COMBINED_SYMBOLS)
            break;
         len += utf_ptr2len(p + len);
         if (p[len] < 0x80 || !utf_iscomposing(cc = mb_ptr2char(p + len)))
            break;
      }
   }

   if (i < MAX_COMBINED_SYMBOLS)   // last composing char must be 0
      pcc[i] = 0;

   return c;
}

//Convert a UTF-8 byte string to a wide character. Also get up to MAX_COMBINED_SYMBOLS
//composing characters. Use no more than p[maxlen].
pub int
utfc_ptr2char_len(
    CS p,
    OUT int* pcc,   // return: composing chars, last one is 0
    int maxlen
) {
   Unt c = mb_ptr2char(p);
   int len = utf_ptr2len_len(p, maxlen);
   int i = 0;
   // Only accept a composing char when the first char isn't illegal.
   if ((len > 1 || *p < 0x80) && len < maxlen && p[len] >= 0x80 && UTF_COMPOSINGLIKE(p, p + len)) {
      int cc = mb_ptr2char(p + len);
      for (;;) {
         pcc[i++] = cc;
         if (i == MAX_COMBINED_SYMBOLS)
            break;
         len += utf_ptr2len_len(p + len, maxlen - len);
         if (len >= maxlen || p[len] < 0x80 || !utf_iscomposing(cc = mb_ptr2char(p + len)))
            break;
      }
   }

   if (i < MAX_COMBINED_SYMBOLS)   // last composing char must be 0
      pcc[i] = 0;

   return c;
}
// Get class of a Unicode character.
// 0: white space
// 1: punctuation
// 2 or bigger: some class of word character.
pub int
utf_class(int c) {
   return utf_class_buf(c, curBook);
}

//If the cursor moves on an trail byte, set the cursor on the lead byte.
//Thus it moves left if necessary. Return true when the cursor was adjusted.
pub void
mb_adjust_cursor(void) {
   mb_adjustpos(curBook, &curPor->cursor);
}

//Adjust position "*lp" to point to the first byte of a multi-byte character.
//If it points to a tail byte it's moved backwards to the head byte.
pub void
mb_adjustpos(Book* book, Pos *lp) {
   CS p;

   if (lp->col > 0 || lp->coladd > 1) {
      p = memGetLine(book, lp->lnum, false);
      if (*p == ZERO || memGetBookLen(book, lp->lnum) < lp->col)
          lp->col = 0;
      else
          lp->col -= mb_head_off(p, p + lp->col);
   }
}

pub void
f_charclass(Arr(Var) argvars, Var* returnVar UNUSED) {
   if (check_for_string_arg(argvars, 0) == FAIL || argvars[0].string == NULL)
      return;
   returnVar->number = mb_get_class(argvars[0].string);
}

pub int
utf_class_buf(Unt c, Book* book) {
   // sorted list of non-overlapping intervals
   static struct clinterval {
      unsigned int first;
      unsigned int last;
      unsigned int class;
   } classes[] = {
      {0x037e, 0x037e, 1},      // Greek question mark
      {0x0387, 0x0387, 1},      // Greek ano teleia
      {0x055a, 0x055f, 1},      // Armenian punctuation
      {0x0589, 0x0589, 1},      // Armenian full stop
      {0x05be, 0x05be, 1},
      {0x05c0, 0x05c0, 1},
      {0x05c3, 0x05c3, 1},
      {0x05f3, 0x05f4, 1},
      {0x060c, 0x060c, 1},
      {0x061b, 0x061b, 1},
      {0x061f, 0x061f, 1},
      {0x066a, 0x066d, 1},
      {0x06d4, 0x06d4, 1},
      {0x0700, 0x070d, 1},      // Syriac punctuation
      {0x0964, 0x0965, 1},
      {0x0970, 0x0970, 1},
      {0x0df4, 0x0df4, 1},
      {0x0e4f, 0x0e4f, 1},
      {0x0e5a, 0x0e5b, 1},
      {0x0f04, 0x0f12, 1},
      {0x0f3a, 0x0f3d, 1},
      {0x0f85, 0x0f85, 1},
      {0x104a, 0x104f, 1},      // Myanmar punctuation
      {0x10fb, 0x10fb, 1},      // Georgian punctuation
      {0x1361, 0x1368, 1},      // Ethiopic punctuation
      {0x166d, 0x166e, 1},      // Canadian Syl. punctuation
      {0x1680, 0x1680, 0},
      {0x169b, 0x169c, 1},
      {0x16eb, 0x16ed, 1},
      {0x1735, 0x1736, 1},
      {0x17d4, 0x17dc, 1},      // Khmer punctuation
      {0x1800, 0x180a, 1},      // Mongolian punctuation
      {0x2000, 0x200b, 0},      // spaces
      {0x200c, 0x2027, 1},      // punctuation and symbols
      {0x2028, 0x2029, 0},
      {0x202a, 0x202e, 1},      // punctuation and symbols
      {0x202f, 0x202f, 0},
      {0x2030, 0x205e, 1},      // punctuation and symbols
      {0x205f, 0x205f, 0},
      {0x2060, 0x27ff, 1},      // punctuation and symbols
      {0x2070, 0x207f, 0x2070},   // superscript
      {0x2080, 0x2094, 0x2080},   // subscript
      {0x20a0, 0x27ff, 1},      // all kinds of symbols
      {0x2800, 0x28ff, 0x2800},   // braille
      {0x2900, 0x2998, 1},      // arrows, brackets, etc.
      {0x29d8, 0x29db, 1},
      {0x29fc, 0x29fd, 1},
      {0x2e00, 0x2e7f, 1},      // supplemental punctuation
      {0x3000, 0x3000, 0},      // ideographic space
      {0x3001, 0x3020, 1},      // ideographic punctuation
      {0x3030, 0x3030, 1},
      {0x303d, 0x303d, 1},
      {0x3040, 0x309f, 0x3040},   // Hiragana
      {0x30a0, 0x30ff, 0x30a0},   // Katakana
      {0x3300, 0x9fff, 0x4e00},   // CJK Ideographs
      {0xac00, 0xd7a3, 0xac00},   // Hangul Syllables
      {0xf900, 0xfaff, 0x4e00},   // CJK Ideographs
      {0xfd3e, 0xfd3f, 1},
      {0xfe30, 0xfe6b, 1},      // punctuation forms
      {0xff00, 0xff0f, 1},      // half/fullwidth ASCII
      {0xff1a, 0xff20, 1},      // half/fullwidth ASCII
      {0xff3b, 0xff40, 1},      // half/fullwidth ASCII
      {0xff5b, 0xff65, 1},      // half/fullwidth ASCII
      {0x1d000, 0x1d24f, 1},      // Musical notation
      {0x1d400, 0x1d7ff, 1},      // Mathematical Alphanumeric Symbols
      {0x1f000, 0x1f2ff, 1},      // Game pieces; enclosed characters
      {0x1f300, 0x1f9ff, 1},      // Many symbol blocks
      {0x20000, 0x2a6df, 0x4e00},   // CJK Ideographs
      {0x2a700, 0x2b73f, 0x4e00},   // CJK Ideographs
      {0x2b740, 0x2b81f, 0x4e00},   // CJK Ideographs
      {0x2f800, 0x2fa1f, 0x4e00},   // CJK Ideographs
   };

   int bot = 0;
   int top = ARRAY_LENGTH(classes) - 1;
   int mid;

   // First quick check for Latin1 characters, use 'iskeyword'.
   if (c < 0x100) {
      if (c == ' ' || c == '\t' || c == ZERO || c == 0xa0)
          return 0;       // blank
      if (eeIsWordc_buf(c, book))
          return 2;       // word character
      return 1;       // punctuation
   }

   // emoji
   if (strInEmojiTable(c))
      return 3;

   // binary search in table
   while (top >= bot) {
      mid = (bot + top) / 2;
      if (classes[mid].last < (unsigned int)c)
         bot = mid + 1;
      ei (classes[mid].first > (unsigned int)c)
         top = mid - 1;
      else
         return (int)classes[mid].class;
   }

   // most other characters are "word" characters
   return 2;
}

privateComp typedef struct { // copy from strings.c
   long first;
   long last;
} Interval;

//For UTF-8 character "c" return 2 for a double-width character, 1 for others.
//Return 4 or 6 for an unprintable character.
//Is only correct for characters >= 0x80.
pub int
mb_char2cells(int c) {
   if (c >= 0x100) {
      if (!utf_printable(c))
         return 6;      // unprintable, displays <xxxx>
      if (strInDoubleWidthTable(c))
         return 2;
   }
   // Characters below 0x100 are influenced by 'isprint' option
   ei (c >= 0x80 && !bookIsCharPrintable(c))
      return 4;      // unprintable, displays <xx>

   return 1;
}

// mb_char2cells() with different argument type for libvterm.
pub int
utf_uint2cells(Unt c) {
   if (c >= 0x100 && utf_iscomposing((int)c))
      return 0;
   return mb_char2cells(c);
}


// Translate a string into allocated memory, replacing special chars with printable chars.
pub CS
sanitizeStr(CS s) {
   int      l, c;
   Byte   hexbuf[11];

   // Compute the length of the result, taking account of unprintable multi-byte characters
   int len = 0;
   CS p = s;
   while (*p != ZERO) {
      if ((l = utfCharLen(p)) > 1) {
         c = mb_ptr2char(p);
         p += l;
         if (bookIsCharPrintable(c))
            len += l;
         else {
           transchar_hex(hexbuf, c);
           len += (int)STRLEN(hexbuf);
         }
      } else {
         l = byte2cells(*p++);
         if (l > 0)
            len += l;
         else
            len += 4;   // illegal byte sequence
      }
   }
   CS res = alloc(len + 1);
   *res = ZERO;
   p = s;
   while (*p != ZERO) {
      if ((l = utfCharLen(p)) > 1) {
         c = mb_ptr2char(p);
         if (bookIsCharPrintable(c))
            STRNCAT(res, p, l);   // append printable multi-byte char
         else
            transchar_hex(res + STRLEN(res), c);
         p += l;
      } else
         STRCAT(res, bookTranscharByte(*p++));
   }
   return res;
}

//"g8": show bytes of the UTF-8 char under the cursor.
pub void
show_utf8(void) {
   int rlen = 0;

   // Get the byte length of the char under the cursor, including composing
   // characters.
   CS line = ml_get_cursor();
   int len = utfCharLen(line);
   if (len == 0) {
      msg(S"ZERO");
      return;
   }

   int clen = 0;
   for (int i = 0; i < len; ++i) {
      if (clen == 0) {
         // start of (composing) character, get its length
         if (i > 0) {
            STRCPY(IObuff + rlen, "+ ");
            rlen += 2;
         }
         clen = utf_ptr2len(line + i);
      }
      SPRINTF(IObuff + rlen, "%02x ", (line[i] == NL) ? ZERO : line[i]);  // ZERO is stored as NL
      --clen;
      rlen += (int)STRLEN(IObuff + rlen);
      if (rlen > IOSIZE - 20)
         break;
   }

   msg(IObuff);
}

//Translate any special characters in buf[bufsize] in-place.
//The result is a string with only printable characters, but if there is not
//enough room, not all characters will be translated.
pub void
trans_characters(CS buf, int bufsize) {
   int len;      // length of string needing translation
   int room;      // room in buffer after string
   CS trs;      // translated character
   int trs_len;   // length of trs[]

   len = (int)STRLEN(buf);
   room = bufsize - len;
   while (*buf != 0) {
      // Assume a multi-byte character doesn't need translation.
      if ((trs_len = utfCharLen(buf)) > 1)
         len -= trs_len;
      else {
         trs = bookTranscharByte(*buf);
         trs_len = (int)STRLEN(trs);
         if (trs_len > 1) {
            room -= trs_len - 1;
            if (room <= 0)
               return;
            MEMMOVE(buf + trs_len, buf + 1, (Unt)len);
         }
         MEMMOVE(buf, trs, (Unt)trs_len);
         --len;
      }
      buf += trs_len;
   }
}

pub int
mb_ptr2cells(CS p) {
   // Need to convert to a character number.
   if (*p >= 0x80) {
      int c = mb_ptr2char(p);
      // An illegal byte is displayed as <xx>.
      if (utf_ptr2len(p) == 1 || c == ZERO)
         return 4;
      // If the char is ASCII it must be an overlong sequence.
      if (c < 0x80)
         return bookChar2Cells(c);
      return mb_char2cells(c);
   }
   return 1;
}

pub int
mb_ptr2cells_len(CS p, int size) {
   // Need to convert to a wide character.
   if (size > 0 && *p >= 0x80) {
      if (utfNeedTruncate(p, size))
          return 1;  // truncated
      int c = mb_ptr2char(p);
      // An illegal byte is displayed as <xx>.
      if (utf_ptr2len(p) == 1 || c == ZERO)
          return 4;
      // If the char is ASCII it must be an overlong sequence.
      if (c < 0x80)
          return bookChar2Cells(c);
      return mb_char2cells(c);
    }
    return 1;
}

//Return the number of cells occupied by string "p".
//Stop at a ZERO character.  When "len" >= 0 stop at character "p[len]".
pub int
mb_string2cells(CS p, int len) {
   int clen = 0;

   for (int i = 0; (len < 0 || i < len) && p[i] != ZERO; i += utfCharLen(p + i))
      clen += mb_ptr2cells(p + i);
   return clen;
}

// Find the start of the next word.
// Return a pointer to the first char of the word. Also stop at a ZERO.
pub CS
findWordStart(CS ptr) {
   while (*ptr != ZERO && *ptr != '\n' && mb_get_class(ptr) <= 1)
      ptr += utfCharLen(ptr);
   return ptr;
}

//}}}
//{{{mouse-handling functions

// bit masks for modifiers:
#define MOUSE_SHIFT 0x04
#define MOUSE_ALT   0x08
#define MOUSE_CTRL  0x10

// Indexes for the tab panel:
//   N > 0 for label of tab N
//   N == 0 for no label
//   N < 0 for closing tab -N
//   N == -999 for closing current tab
#define CLOSING_CURRENT_TAB 4000000000
#define NO_LABEL_TAB        4000000001
#define CLOSING_TAB         2000000000 // tab indices (N + this number) mean "closing tab N"

// jump_to_mouse() returns one of first five these values, possibly with some of the other 4 added
#define IN_UNKNOWN            0
#define IN_BOOK               1
#define IN_STATUS_LINE        2   // on status or command line
#define IN_SEP_LINE           4   // on vertical separator line
#define IN_OTHER_WIN          8   // in other portal but can't go there
#define CURSOR_MOVED      0x100
#define MOUSE_FOLD_CLOSE  0x200   // clicked on '-' in fold column
#define MOUSE_FOLD_OPEN   0x400   // clicked on '+' in fold column
#define MOUSE_WINBAR      0x800   // in portal toolbar
private Boole mouse_ison = false;

//<linux/keyboard.h> contains defines conflicting with "keymap.h", I just copied relevant defines 
//here. A cleaner solution would be to put gpm code into separate file and include there 
//linux/keyboard.h
//#include <linux/keyboard.h>
#define KG_SHIFT     0
#define KG_CTRL      2
#define KG_ALT       3
#define KG_ALTGR     1
#define KG_SHIFTL    4
#define KG_SHIFTR    5
#define KG_CTRLL     6
#define KG_CTRLR     7
#define KG_CAPSSHIFT 8

//Horiziontal and vertical steps used when scrolling. When negative scroll by a whole page.
private long mouse_hor_step = 6;
private long mouse_vert_step = 3;

private int do_mousescroll_horiz(Ulong leftcol);

//Return the duration from t1 to t2 in milliseconds.
private long
time_diff_ms(TimeVal *t1, TimeVal *t2) {
   // This handles wrapping of tv_usec correctly without any special case.
   // Example of 2 pairs (tv_sec, tv_usec) with a duration of 5 ms:
   //      t1 = (1, 998000) t2 = (2, 3000) gives:
   //      (2 - 1) * 1000 + (3000 - 998000) / 1000 -> 5 ms.
   return (t2->tv_sec - t1->tv_sec) * 1000 + (t2->tv_usec - t1->tv_usec) / 1000;
}

//Get class of a character for selection: same class means same word.
//0: blank
//1: punctuation groups
//2: normal word character
//>2: multi-byte word character.
private int
get_mouse_class(CS p) {
   if (utf8CharLens[p[0]] > 1)
      return mb_get_class(p);

   Unt c = *p;
   if (c == ' ' || c == '\t')
      return 0;

   if (eeIsWordc(c))
      return 2;

   // There are a few special cases where we want certain combinations of
   // characters to be considered as a single word.  These are things like
   // "->", "/ *", "*=", "+=", "&=", "<=", ">=", "!=" etc.  Otherwise, each
   // character is in its own class.
   if (c != ZERO && firstOccurrence((CS)"-+*/%<>&|^!=", c) != NULL)
      return 1;
   return c;
}

//Move "pos" back to the start of the word it's in.
private void
find_start_of_word(Pos*pos) {
   CS line = ml_get(pos->lnum);
   int cclass = get_mouse_class(line + pos->col);

   int col;
   while (pos->col > 0) {
      col = pos->col - 1;
      col -= (*mb_head_off)(line, line + col);
      if (get_mouse_class(line + col) != cclass)
          break;
      pos->col = col;
   }
}

//Move "pos" forward to the end of the word it's in.
//When 'selection' is "exclusive", the position is just after the word.
private void
find_end_of_word(Pos* pos) {
   int col;

   CS line = ml_get(pos->lnum);
   int cclass = get_mouse_class(line + pos->col);
   while (line[pos->col] != ZERO) {
      col = pos->col + utfCharLen(line + pos->col);
      if (get_mouse_class(line + col) != cclass) {
         break;
      }
      pos->col = col;
   }
}

# define USE_POPUP_SETPOS
# define NEED_VCOL2COL

//Do the appropriate action for the current mouse click in the current mode.
//Not used for Command-line mode.
//
//[Normal and Visual Mode]:
//event       modifier position           visual      change       action
//                       cursor                       portal
//_______________________________________________________________________________.
//left press     -         yes                  end      yes                     |
//left press     C         yes                  end      yes     "^]" (2)        |
//left press     S         yes   end (popup: extend)     yes     "*" (2)         |
//left drag      -         yes   start if moved           no                     |
//left release   -         yes   start if moved           no                     |
//middle press   -         yes   if not active            no     put register    |
//middle press   -         yes   if active                no     yank and put    |
//right press    -         yes   start or extend         yes                     |
//right press    S         yes   no change               yes     "#" (2)         |
//right drag     -         yes   extend                   no                     |
//right release  -         yes   extend                   no                     |
//_______________________________________________________________________________. 
//
//[Insert Mode]:
//event    modifier    position           visual      change       action
//                       cursor                        portal
//_______________________________________________________________________________.
//left press     -         yes   (cannot be active)      yes                     |
//left press     C         yes   (cannot be active)      yes     "CTRL-O^]" (2)  |
//left press     S         yes   (cannot be active)      yes     "CTRL-O*" (2)   |
//left drag      -         yes   start or extend (1)      no     CTRL-O (1)      |
//left release   -         yes   start or extend (1)      no     CTRL-O (1)      |
//middle press   -          no   (cannot be active)       no     put register    |
//right press    -         yes   start or extend         yes     CTRL-O          |
//right press    S         yes   (cannot be active)      yes     "CTRL-O#" (2)   |
//_______________________________________________________________________________.
//(1) only if mouse pointer moved since press
//(2) only if click is in same book
//
//Return true if start_arrow() should be called for edit mode.
pub int
do_mouse(
   Operator* oper,  // operator argument, can be NULL
   Unt c,           // K_LEFTMOUSE, etc
   Unt dir,         // Direction to 'put' if necessary
   long count,
   int fixindent    // PUT_FIXINDENT if fixing indent necessary
){
   static Boole do_always = false;   // ignore 'mouse' setting next time
   static Boole got_click = false;   // got a click some time back

   Unt      which_button;   // MOUSE_LEFT, _MIDDLE or _RIGHT
   Boole is_click = false; // If false it's a drag or release event
   Boole is_drag = false;  // If true it's a drag event
   Unt jump_flags = 0;   // flags for jump_to_mouse()
   Pos   start_visual;
   int      moved;      // Has cursor moved?
   int      in_status_line;   // mouse in status line
   static Boole in_tabpanel = false; // mouse clicked in tabpanel
   int in_sep_line;   // mouse in vertical separator line
   Unt c1;
   Unt c2; 
   Pos   save_cursor;
   Portal   *old_curPor = curPor;
   static Pos orig_cursor;
   ColNr   leftcol, rightcol;
   Pos   end_visual;
   int      diff;
   int      old_active = VIsual_active;
   Unt      old_mode = VIsual_mode;
   int      regname;

   save_cursor = curPor->cursor;

   // When GUI is active, always recognize mouse events, otherwise:
   // - Ignore mouse event in normal mode if 'mouse' doesn't include 'n'.
   // - Ignore mouse event in visual mode if 'mouse' doesn't include 'v'.
   // - For command line and insert mode 'mouse' is checked before calling do_mouse().
   if (do_always)
      do_always = false;

   for (;;) {
      which_button = get_mouse_button(KEY2TERMCAP1(c), OUT &is_click, &is_drag);
      if (is_drag) {
         //If the next character is the same mouse event then use that one. Speeds up dragging 
         //the status line. Note: Since characters added to the stuff buffer in the code
         //below need to come before the next character, do not do this when the current character 
         //was stuffed.
         if (!keyWasStuffedG && vpeekc() != ZERO) {
            int save_mouse_row = mouseRowG;
            int save_mouse_col = mouseColG;

            //Need to get the character, peeking doesn't get the actual one.
            Unt nc = safe_vgetc();
            if (c == nc)
               continue;
            vungetc(nc);
            mouseRowG = save_mouse_row;
            mouseColG = save_mouse_col;
         }
      }
      break;
   }

   if (c == K_MOUSEMOVE) {
      // Mouse moved without a button pressed.
      ui_may_remove_balloon();
      if (p_bevalterm) {
         profile_setlimit(p_bdlay, &bevalexpr_due);
         bevalexpr_due_set = true;
      }
      popup_handle_mouse_moved();
      return false;
   }

   // Ignore drag and release events if we didn't get a click.
   if (is_click) {
      got_click = true;
      in_tabpanel = false;
   } else {
      if (!got_click)         // didn't get click, ignore
          return false;
      if (!is_drag) {        // release, reset got_click
         got_click = false;
         if (in_tabpanel) {
            in_tabpanel = false;
            return false;
         }
      }
   }

   // CTRL right mouse button does CTRL-T
   if (is_click && (modMaskG & MOD_MASK_CTRL) && which_button == MOUSE_RIGHT) {
      if (stateG & MODE_INSERT)
         stuffcharReadbuff(Ctrl_O);
      if (count > 1)
         stuffnumReadbuff(count);
      stuffcharReadbuff(Ctrl_T);
      got_click = false;      // ignore drag&release now
      return false;
   }

   // CTRL only works with left mouse button
   if ((modMaskG & MOD_MASK_CTRL) && which_button != MOUSE_LEFT)
      return false;

   // When a modifier is down, ignore drag and release events, as well as
   // multiple clicks and the middle mouse button.
   // Accept shift-leftmouse drags when 'mousemodel' is "popup.*".
   if ((modMaskG & (MOD_MASK_SHIFT | MOD_MASK_CTRL | MOD_MASK_ALT | MOD_MASK_META))
       && (!is_click
         || (modMaskG & MOD_MASK_MULTI_CLICK)
         || which_button == MOUSE_MIDDLE)
       && !((modMaskG & MOD_MASK_ALT) && which_button == MOUSE_RIGHT)
    ) {
      return false;
    } 

   // If the button press was used as the movement command for an operator
   // (eg "d<MOUSE>"), or it is the middle button that is held down, ignore drag/release events.
   if (!is_click && which_button == MOUSE_MIDDLE)
      return false;

   if (oper)
      regname = oper->regname;
   else
      regname = 0;

   // Middle mouse button does a 'put' of the selected text
   if (which_button == MOUSE_MIDDLE) {
      if (stateG == MODE_NORMAL) {
         // If an operator was pending, we don't know what the user wanted
         // to do. Go back to normal mode: Clear the operator and beep().
         if (oper != NULL && oper->opTy != OP_NOP) {
            clearopbeep(oper);
            return false;
          }

          // If visual was active, yank the highlighted text and put it
          // before the mouse pointer position.
          // In Select mode replace the highlighted text with the clipboard.
         if (VIsual_active) {
            stuffcharReadbuff('y');
            stuffcharReadbuff(K_MIDDLEMOUSE);
            do_always = true;   // ignore 'mouse' setting next time
            return false;
         }
         // The rest is below jump_to_mouse()
      } ei ((stateG & MODE_INSERT) == 0)
         return false;

      //Middle click in insert mode doesn't move the mouse, just insert the contents of a register.
      //'.' register is special, can't insert that with do_put().
      //Also paste at the cursor if the current mode isn't in 'mouse' (only happens for the GUI).
      if ((stateG & MODE_INSERT) != 0) {
         if (regname == '.')
            insert_reg(regname, true);
         else {
            if (regname == ZERO)
               regname = '*';
            do_put(regname, NULL, BACKWARD, 1L, fixindent | PUT_CURSEND);

            // Repeat it with CTRL-R CTRL-O r or CTRL-R CTRL-P r
            AppendCharToRedobuff(Ctrl_R);
            AppendCharToRedobuff(fixindent ? Ctrl_P : Ctrl_O);
            AppendCharToRedobuff(regname == 0 ? '"' : regname);
         }
         return false;
      }
   }

   // When dragging or button-up stay in the same portal.
   if (!is_click)
      jump_flags |= MOUSE_FOCUS | MOUSE_DID_MOVE;

   start_visual.lnum = 0;

   // Check for clicking in the tab panel.
   if (mouseRowG < (int)firstPor->windowRow + (int)topframeG->width
      && (mouseColG < firstPor->windowCol 
         || mouseColG >= (int)firstPor->windowCol + (int)topframeG->width)
   ) {
      if (is_drag) {
         if (in_tabpanel) {
            c1 = get_tabNr_on_tabpanel();
            
            moveTab((c1 == 0 || c1 == UNT) ? 9999 : (c1 < indexOfTab(curtab) ? c1 - 1 : c1));
         }
         return false;
      }

      // click in a tab selects that tab
      if (is_click && commPortTypeG == 0) {
         in_tabpanel = true;
         c1 = get_tabNr_on_tabpanel();
         if (c1 < CLOSING_TAB) {
            if ((modMaskG & MOD_MASK_MULTI_CLICK) == MOD_MASK_2CLICK) {
               // double click opens new page
               end_visual_mode_keep_button();
               tabNew();
               moveTab(c1 == 0 ? 9999 : c1 - 1);
            } else {
               // Go to specified tab, or next one if not clicking on a label.
               gotoTabById(c1);

               // It's like clicking on the status line of a portal.
               if (curPor != old_curPor)
                  end_visual_mode_keep_button();
            }
         } else {
            // Close the current or specified tab
            Tab* t = (c1 == CLOSING_TAB) ? curtab : getTab(c1 - CLOSING_TAB);
            if (t == curtab) {
               if (firstTabG->next != NULL)
                  tabClose();
            } ei (t)
               tabCloseOther(t);
         }
      }
      return true;
   } ei (is_drag && in_tabpanel) {
      c1 = get_tabNr_on_tabpanel();
      moveTab(c1 <= 0 ? 9999 : c1 - 1);
      return false;
   }

   if (tabIndsG) { // only when initialized
      // Check for clicking in the tab line.
      if (mouseRowG == 0 && firstPor->windowRow > 0) {
         if (is_drag) {
            return false;
         }

         //click in a tab selects that tab
         if (is_click && commPortTypeG == 0 && mouseColG < firstPor->windowCol + (int)topframeG->width) {
            c1 = tabIndsG[mouseColG];
            if (c1 < CLOSING_TAB) {
               if ((modMaskG & MOD_MASK_MULTI_CLICK) == MOD_MASK_2CLICK) {
                  // double click opens new page
                  end_visual_mode_keep_button();
                  tabNew();
                  moveTab(c1 == 0 ? 9999 : c1 - 1);
               } else {
                  // Go to specified tab, or next one if not clicking on a label.
                  gotoTabById(c1);

                  // It's like clicking on the status line of a portal.
                  if (curPor != old_curPor)
                      end_visual_mode_keep_button();
                }
            } else {
               // Close the current or specified tab
               Tab   *t;
               if (c1 == CLOSING_CURRENT_TAB)
                  t = curtab;
               else
                  t = getTab(c1 - CLOSING_TAB);
               if (t == curtab) {
                  if (firstTabG->next)
                     tabClose();
               } ei (t)
                  tabCloseOther(t);
            }
         }
         return true;
      }
   }
   if ((stateG & (MODE_NORMAL | MODE_INSERT)) && !(modMaskG & (MOD_MASK_SHIFT | MOD_MASK_CTRL))) {
      if (which_button == MOUSE_LEFT) {
         if (is_click) {
            // stop Visual mode for a left click in a portal, but not when on a status line
            if (VIsual_active)
                jump_flags |= MOUSE_MAY_STOP_VIS;
         } else
            jump_flags |= MOUSE_MAY_VIS;
      } ei (which_button == MOUSE_RIGHT) {
         if (is_click && VIsual_active) {
            // Remember the start and end of visual before moving the cursor.
            if (LT_POS(curPor->cursor, VIsual)) {
                start_visual = curPor->cursor;
                end_visual = VIsual;
            } else {
                start_visual = VIsual;
                end_visual = curPor->cursor;
            }
         }
         jump_flags |= MOUSE_FOCUS;
         jump_flags |= MOUSE_MAY_VIS;
      }
   }

   // If an operator is pending, ignore all drags and releases until the next mouse click.
   if (!is_drag && oper != NULL && oper->opTy != OP_NOP) {
      got_click = false;
      oper->motion_type = MCHAR;
   }

   // When releasing the button let jump_to_mouse() know.
   if (!is_click && !is_drag)
      jump_flags |= MOUSE_RELEASED;

   // JUMP!
   jump_flags = jump_to_mouse(jump_flags, oper ? OUT &(oper->inclusive) : null, which_button);

   moved = (jump_flags & CURSOR_MOVED);
   in_status_line = (jump_flags & IN_STATUS_LINE);
   in_sep_line = (jump_flags & IN_SEP_LINE);

   // When jumping to another portal, clear a pending operator.  That's a bit
   // friendlier than beeping and not jumping to that portal.
   if (curPor != old_curPor && oper != NULL && oper->opTy != OP_NOP)
      clearop(oper);

   if (modMaskG == 0
       && !is_drag
       && (jump_flags & (MOUSE_FOLD_CLOSE | MOUSE_FOLD_OPEN))
       && which_button == MOUSE_LEFT
   ) {
      // open or close a fold at this line
      if (jump_flags & MOUSE_FOLD_OPEN)
         openFold(curPor->cursor.lnum, 1L);
      else
         closeFold(curPor->cursor.lnum, 1L);
      // don't move the cursor if still in the same portal
      if (curPor == old_curPor)
         curPor->cursor = save_cursor;
   }

   if ((jump_flags & IN_OTHER_WIN) && !VIsual_active) {
      clip_modeless(which_button, is_click, is_drag);
      return false;
   }

   //Set global flag that we are extending the Visual area with mouse dragging
   if (VIsual_active && is_drag && curPor->o.scrollOff > 0) {
      //In the very first line, allow scrolling one line
      if (mouseRowG == 0)
         mouseDraggingG = 2;
      else
         mouseDraggingG = 1;
    }

   //When dragging the mouse above the portal, scroll down.
   if (is_drag && mouseRowG < 0 && !in_status_line) {
      scroll_redraw(false, 1L);
      mouseRowG = 0;
   }

   if (start_visual.lnum) {     // right click in visual mode
      //When ALT is pressed make Visual mode blockwise.
      if (modMaskG & MOD_MASK_ALT)
         VIsual_mode = Ctrl_V;

      //In Visual-block mode, divide the area in four, pick up the corner
      //that is in the quarter that the cursor is in.
      if (VIsual_mode == Ctrl_V) {
         getvcols(curPor, &start_visual, &end_visual, &leftcol, &rightcol);
         if (curPor->cursWant > (leftcol + rightcol) / 2)
            end_visual.col = leftcol;
         else
            end_visual.col = rightcol;
         if (curPor->cursor.lnum >= (start_visual.lnum + end_visual.lnum) / 2)
            end_visual.lnum = start_visual.lnum;

         // move VIsual to the right column
         start_visual = curPor->cursor;       // save the cursor pos
         curPor->cursor = end_visual;
         coladvance(end_visual.col);
         VIsual = curPor->cursor;
         curPor->cursor = start_visual;       // restore the cursor
      } else {
         // If the click is before the start of visual, change the start.
         // If the click is after the end of visual, change the end.  If
         // the click is inside the visual, change the closest side.
         if (LT_POS(curPor->cursor, start_visual))
            VIsual = end_visual;
         ei (LT_POS(end_visual, curPor->cursor))
            VIsual = start_visual;
         else {
            // In the same line, compare column number
            if (end_visual.lnum == start_visual.lnum) {
               if (curPor->cursor.col - start_visual.col > end_visual.col - curPor->cursor.col)
                  VIsual = start_visual;
               else
                  VIsual = end_visual;
            }

            // In different lines, compare line number
            else {
               diff = (curPor->cursor.lnum - start_visual.lnum) -
                  (end_visual.lnum - curPor->cursor.lnum);

               if (diff > 0)      // closest to end
                  VIsual = start_visual;
               ei (diff < 0)   // closest to start
                  VIsual = end_visual;
               else {        // in the middle line
                  if (curPor->cursor.col < (start_visual.col + end_visual.col) / 2)
                      VIsual = end_visual;
                  else
                      VIsual = start_visual;
               }
            }
         }
      }
   }
   // If Visual mode started in insert mode, execute "CTRL-O"
   ei ((stateG & MODE_INSERT) && VIsual_active)
      stuffcharReadbuff(Ctrl_O);

   // Middle mouse click: Put text before cursor.
   if (which_button == MOUSE_MIDDLE) {
      if (regname == 0)
         regname = '*';
      if (yank_register_mline(regname)) {
         if (isMouseBelowBottomLineS)
            dir = FORWARD;
      } ei (isMouseRightOfEolS)
         dir = FORWARD;

      if (fixindent) {
         c1 = (dir == BACKWARD) ? '[' : ']';
         c2 = 'p';
      } else {
         c1 = (dir == FORWARD) ? 'p' : 'P';
         c2 = ZERO;
      }
      prep_redo(regname, count, ZERO, c1, ZERO, c2, ZERO);

      //Remember where the paste started, so in edit() insertStartG can be set to this position
      if (restart_edit != 0)
         where_paste_started = curPor->cursor;
      do_put(regname, NULL, dir, count, fixindent | PUT_CURSEND);
   }
   // Ctrl-Mouse click or double click in a location portal jumps to the
   // error under the mouse pointer.
   ei (((modMaskG & MOD_MASK_CTRL)
      || (modMaskG & MOD_MASK_MULTI_CLICK) == MOD_MASK_2CLICK) && isLocationListBook(curBook)
   ){
      if (curPor->locationStackRef == NULL)   // location portal
         executeCommLine((CS)".mc");
      else               // location list portal
         executeCommLine((CS)".ll");
      got_click = false;      // ignore drag&release now
   }

   // Ctrl-Mouse click (or double click in a help portal) jumps to the tag
   // under the mouse pointer.
   ei ((modMaskG & MOD_MASK_CTRL) || (curBook->kind == BOOK_HELP
           && (modMaskG & MOD_MASK_MULTI_CLICK) == MOD_MASK_2CLICK)
   ) {
      if (stateG & MODE_INSERT)
         stuffcharReadbuff(Ctrl_O);
      stuffcharReadbuff(Ctrl_RSB);
      got_click = false;      // ignore drag&release now
   }

   // Shift-Mouse click searches for the next occurrence of the word under the mouse pointer
   ei ((modMaskG & MOD_MASK_SHIFT)) {
      if (stateG & MODE_INSERT)
         stuffcharReadbuff(Ctrl_O);
      if (which_button == MOUSE_LEFT)
         stuffcharReadbuff('*');
      else   // MOUSE_RIGHT
         stuffcharReadbuff('#');
   } ei (in_status_line) {
      // Handle double clicks, unless on status line
   } ei (in_sep_line) {
   } ei ((modMaskG & MOD_MASK_MULTI_CLICK) && (stateG & (MODE_NORMAL | MODE_INSERT)) != 0) {
      if (is_click || !VIsual_active) {
         if (VIsual_active)
            orig_cursor = VIsual;
         else {
            check_visual_highlight();
            VIsual = curPor->cursor;
            orig_cursor = VIsual;
            VIsual_active = true;
            VIsual_reselect = true;
            setmouse();
         }
         if ((modMaskG & MOD_MASK_MULTI_CLICK) == MOD_MASK_2CLICK) {
            // Double click with ALT pressed makes it blockwise.
            if (modMaskG & MOD_MASK_ALT)
               VIsual_mode = Ctrl_V;
            else
               VIsual_mode = 'v';
         } ei ((modMaskG & MOD_MASK_MULTI_CLICK) == MOD_MASK_3CLICK)
            VIsual_mode = 'V';
         ei ((modMaskG & MOD_MASK_MULTI_CLICK) == MOD_MASK_4CLICK)
            VIsual_mode = Ctrl_V;
          // Make sure the clipboard gets updated.  Needed because start and
          // end may still be the same, and the selection needs to be owned
          clipboard.vmode = ZERO;
      }
      // A double click selects a word or a block.
      if ((modMaskG & MOD_MASK_MULTI_CLICK) == MOD_MASK_2CLICK) {
         Pos* pos = NULL;
         int gc;

         if (is_click) {
            //If the character under the cursor (skipping white space) is
            //not a word character, try finding a match and select a (),
            //{}, [], #if/#endif, etc. block.
            end_visual = curPor->cursor;
            while (gc = gchar_pos(&end_visual), SPACE_OR_TAB(gc))
                inc(&end_visual);
            if (oper)
                oper->motion_type = MCHAR;
            if (oper
               && VIsual_mode == 'v'
               && !eeIsWordc(gchar_pos(&end_visual))
               && EQUAL_POS(curPor->cursor, VIsual)
               && (pos = findmatch(oper, ZERO)) != NULL
            ) {
               curPor->cursor = *pos;
               if (oper->motion_type == MLINE)
                  VIsual_mode = 'V';
            }
         }

         if (!pos && (is_click || is_drag)) {
            // When not found a match or when dragging: extend to include a word.
            if (LT_POS(curPor->cursor, orig_cursor)) {
               find_start_of_word(&curPor->cursor);
               find_end_of_word(&VIsual);
            } else {
               find_start_of_word(&VIsual);
               find_end_of_word(&curPor->cursor);
            }
         }
         curPor->setCursWant = true;
      }
      if (is_click)
         drawCurBookLater(UPD_INVERTED);   // update the inversion
   } ei (VIsual_active && !old_active) {
      if (modMaskG & MOD_MASK_ALT)
         VIsual_mode = Ctrl_V;
      else
         VIsual_mode = 'v';
   }

   // If Visual mode changed show it later.
   if ((!VIsual_active && old_active && isModeDisplayedG)
          || (VIsual_active && p_smd && msg_silent == 0
                && (!old_active || VIsual_mode != old_mode))
   )
      redrawCommlineG = true;

   return moved;
}

pub void
ins_mouse(int c) {
   Portal* old_curPor = curPor;
   Pos tpos = curPor->cursor;
   if (do_mouse(NULL, c, BACKWARD, 1L, 0)) {
      Portal* new_curPor = curPor;

      if (curPor != old_curPor && portalIsValid(old_curPor)) {
         //Mouse took us to another portal. We need to go back to the
         //previous one to stop insert there properly.
         curPor = old_curPor;
         curBook = curPor->book;
         if (bt_prompt(curBook))
            //Restart Insert mode when re-entering the prompt buffer.
            curBook->promptInsert = 'A';
      }
      start_arrow(curPor == old_curPor ? &tpos : NULL);
      if (curPor != new_curPor && portalIsValid(new_curPor)) {
         curPor = new_curPor;
         curBook = curPor->book;
      }
      set_can_cindent(true);
    }

    // redraw status lines (in case another portal became active)
    redraw_statuslines();
}

//Common mouse wheel scrolling, shared between Insert mode and NV modes.
//Default action is to scroll mouse_vert_step lines (or mouse_hor_step columns
//depending on the scroll direction) or one page when Shift or Ctrl is used.
//Direction is indicated by "cap->arg":
//   K_MOUSEUP    - MSCR_UP
//   K_MOUSEDOWN  - MSCR_DOWN
//   K_MOUSELEFT  - MSCR_LEFT
//   K_MOUSERIGHT - MSCR_RIGHT
//"curPor" may have been changed to the portal that should be scrolled and
//differ from the portal that actually has focus.
private void
do_mousescroll(ActionArg* cap) {
   int shift_or_ctrl = modMaskG & (MOD_MASK_SHIFT | MOD_MASK_CTRL);

   if (term_use_loop())
      // This portal is a terminal portal, send the mouse event there.
      // Set "typed" to false to avoid an endless loop.
      send_keys_to_term(curBook->term, cap->cmdchar, modMaskG, false);
   ei (cap->arg == MSCR_UP || cap->arg == MSCR_DOWN) {
      // Vertical scrolling
      if (!(stateG & MODE_INSERT) && (mouse_vert_step < 0 || shift_or_ctrl)) {
          // whole page up or down
          pagescroll(cap->arg == MSCR_UP ? FORWARD : BACKWARD, 1L, false);
      } else {
         if (mouse_vert_step < 0 || shift_or_ctrl) {
            // whole page up or down
            cap->count1 = (long)(curPor->bottomLine - curPor->topLine);
         }
         // Don't scroll more than half the portal height.
         ei (curPor->height < mouse_vert_step * 2) {
            cap->count1 = curPor->height / 2;
            if (cap->count1 == 0)
               cap->count1 = 1;
         } else {
            cap->count1 = mouse_vert_step;
         }
         cap->count0 = cap->count1;
         nv_scroll_line(cap);
      }

      if (PORTAL_IS_POPUP(curPor))
         popup_set_firstline(curPor);
   } else {
      // Horizontal scrolling
      long step = (mouse_hor_step < 0 || shift_or_ctrl) ? curPor->width : mouse_hor_step;
      long leftcol = curPor->leftCol + (cap->arg == MSCR_RIGHT ? -step : step);
      if (leftcol < 0)
          leftcol = 0;
      do_mousescroll_horiz((Ulong)leftcol);
   }
   may_trigger_win_scrolled_resized();
}

//Insert mode implementation for scrolling in direction "dir", which is one of the MSCR_ values.
pub void
ins_mousescroll(int dir) {
   ActionArg   cap;
   Operator   oa;
   CLEAR_FIELD(cap);
   clear_oparg(&oa);
   cap.oper = &oa;
   cap.arg = dir;

   switch (dir) {
   case MSCR_UP:
      cap.cmdchar = K_MOUSEUP;
      break;
   case MSCR_DOWN:
      cap.cmdchar = K_MOUSEDOWN;
      break;
   case MSCR_LEFT:
      cap.cmdchar = K_MOUSELEFT;
      break;
   case MSCR_RIGHT:
      cap.cmdchar = K_MOUSERIGHT;
      break;
   default:
      internalErrFmtMsg("Invalid ins_mousescroll() argument: %d", dir);
  }

   Portal *old_curPor = curPor;
   if (mouseRowG >= 0 && mouseColG >= 0) {
      // Find the portal at the mouse pointer coordinates.
      // NOTE: Must restore "curPor" to "old_curPor" before returning!
      int row = mouseRowG;
      int col = mouseColG;
      curPor = mouseFindPortal(OUT &row, OUT &col, FIND_POPUP);
      if (curPor == NULL) {
          curPor = old_curPor;
          return;
      }
      curBook = curPor->book;
   }

   if (curPor == old_curPor) {
      // Don't scroll the current portal if the popup menu is visible.
      if (pum_visible())
         return;
   }

   LineNr   orig_topline = curPor->topLine;
   ColNr   orig_leftcol = curPor->leftCol;
   Pos   orig_cursor = curPor->cursor;

   // Call the common mouse scroll function shared with other modes.
   do_mousescroll(&cap);

   int did_scroll = (orig_topline != curPor->topLine || orig_leftcol != curPor->leftCol);

   curPor->statusLineNeedsRedraw = true;
   curPor = old_curPor;
   curBook = curPor->book;

   // If the portal actually scrolled and the popup menu may overlay the
   // portal, need to redraw it.
   if (did_scroll && pum_visible()) {
      // TODO: Would be more efficient to only redraw the portals that are
      // overlapped by the popup menu.
      redraw_all_later(UPD_NOT_VALID);
      ins_compl_show_pum();
   }

   if (!EQUAL_POS(curPor->cursor, orig_cursor)) {
      start_arrow(&orig_cursor);
      set_can_cindent(true);
   }
}

//true if "c" is a mouse key.
pub Boole
is_mouse_key(Unt c) {
   return c == K_LEFTMOUSE
      || c == K_LEFTMOUSE_NM
      || c == K_LEFTDRAG
      || c == K_LEFTRELEASE
      || c == K_LEFTRELEASE_NM
      || c == K_MOUSEMOVE
      || c == K_MIDDLEMOUSE
      || c == K_MIDDLEDRAG
      || c == K_MIDDLERELEASE
      || c == K_RIGHTMOUSE
      || c == K_RIGHTDRAG
      || c == K_RIGHTRELEASE
      || c == K_MOUSEDOWN
      || c == K_MOUSEUP
      || c == K_MOUSELEFT
      || c == K_MOUSERIGHT
      || c == K_X1MOUSE
      || c == K_X1DRAG
      || c == K_X1RELEASE
      || c == K_X2MOUSE
      || c == K_X2DRAG
      || c == K_X2RELEASE;
}

private struct mousetable {
   Unt pseudo_code;   // Code for pseudo mouse event
   Unt button;      // Which mouse button is it?
   Boole is_click;      // Is it a mouse button click event?
   Boole is_drag;      // Is it a mouse drag event?
} mouse_table[] = {
   {(int)KE_LEFTMOUSE,     MOUSE_LEFT,   true,   false},
   {(int)KE_LEFTDRAG,      MOUSE_LEFT,   false,   true},
   {(int)KE_LEFTRELEASE,   MOUSE_LEFT,   false,  false},
   {(int)KE_MIDDLEMOUSE,   MOUSE_MIDDLE, true,   false},
   {(int)KE_MIDDLEDRAG,    MOUSE_MIDDLE, false,   true},
   {(int)KE_MIDDLERELEASE, MOUSE_MIDDLE, false,  false},
   {(int)KE_RIGHTMOUSE,    MOUSE_RIGHT,  true,   false},
   {(int)KE_RIGHTDRAG,     MOUSE_RIGHT,  false,   true},
   {(int)KE_RIGHTRELEASE,  MOUSE_RIGHT,  false,  false},
   {(int)KE_X1MOUSE,       MOUSE_X1,     true,   false},
   {(int)KE_X1DRAG,        MOUSE_X1,     false,   true},
   {(int)KE_X1RELEASE,     MOUSE_X1,     false,  false},
   {(int)KE_X2MOUSE,       MOUSE_X2,     true,   false},
   {(int)KE_X2DRAG,        MOUSE_X2,     false,   true},
   {(int)KE_X2RELEASE,     MOUSE_X2,     false,  false},
   // DRAG without CLICK
   {(int)KE_MOUSEMOVE,     MOUSE_RELEASE,   false,   true},
   // RELEASE without CLICK
   {(int)KE_IGNORE,        MOUSE_RELEASE,   false,   false},
   {0,            0,      0,   0},
};

//Look up the given mouse code to return the relevant information in the other
//arguments.  Return which button is down or was released.
pub Unt
get_mouse_button(Unt code, OUT Boole* is_click, OUT Boole* is_drag) {
   for (int i = 0; mouse_table[i].pseudo_code > 0; i++) {
      if (code == mouse_table[i].pseudo_code) {
         *is_click = mouse_table[i].is_click;
         *is_drag = mouse_table[i].is_drag;
         return mouse_table[i].button;
      }
   }
   return 0;       // Shouldn't get here
}

//Return the appropriate pseudo mouse event token (KE_LEFTMOUSE etc) based on the given information 
//about which mouse button is down, and whether the mouse was clicked, dragged or released.
private int
get_pseudo_mouse_code(Unt button, Boole is_click, Boole is_drag) {
                     // eg MOUSE_LEFT
   for (int i = 0; mouse_table[i].pseudo_code; i++) {
      if (button == mouse_table[i].button
          && is_click == mouse_table[i].is_click
          && is_drag == mouse_table[i].is_drag
      ) {
         return mouse_table[i].pseudo_code;
      }
   }
   return (int)KE_IGNORE;       // not recognized, ignore it
}

#define HMT_NORMAL    1
#define HMT_NETTERM   2
#define HMT_DEC       4
#define HMT_JSBTERM   8
#define HMT_PTERM    16
#define HMT_URXVT    32
#define HMT_GPM      64
#define HMT_SGR     128
#define HMT_SGR_REL 256
private Unt haveMouseTermcodeP = 0;

pub void
set_mouse_termcode(Unt n, CS s) {
   Byte name[2] = {n, KE_FILLER};
   termAddRecognizedTermcode(name, s, false);
   if (n == KS_SGR_MOUSE)
      haveMouseTermcodeP |= HMT_SGR;
   ei (n == KS_SGR_MOUSE_RELEASE)
      haveMouseTermcodeP |= HMT_SGR_REL;
   else
      haveMouseTermcodeP |= HMT_NORMAL;
}

pub void
del_mouse_termcode(Unt n) {  // KS_MOUSE, KS_NETTERM_MOUSE or KS_DEC_MOUSE
   Byte name[2] = {n, KE_FILLER};
   del_termcode(name);
   if (n == KS_SGR_MOUSE)
      haveMouseTermcodeP &= ~HMT_SGR;
   ei (n == KS_SGR_MOUSE_RELEASE)
      haveMouseTermcodeP &= ~HMT_SGR_REL;
   else
      haveMouseTermcodeP &= ~HMT_NORMAL;
}

//switch mouse on/off depending on current mode and 'mouse'
pub void
setmouse(void) {
   // Should be outside proc, but may break MOUSESHAPE be quick when mouse is off
   if (haveMouseTermcodeP == 0)
      return;

   // don't switch mouse on when not in raw mode (Ex mode)
   if (cur_tmode != TMODE_RAW) {
      mch_setmouse(false);
      return;
   }
   mch_setmouse(true);
}

private Portal* dragPortalS = NULL;   // portal being dragged

//Reset the portal being dragged.  To be called when switching tab.
pub void
mouseResetDragPortal(void) {
   dragPortalS = NULL;
}

//Move the cursor to the specified row and column on the screen. Change current portal if 
//necessary. Return an integer with the CURSOR_MOVED bit set if the cursor has moved or unset 
//otherwise.
//
//The MOUSE_FOLD_CLOSE bit is set when clicked on the '-' in a fold column.
//The MOUSE_FOLD_OPEN bit is set when clicked on the '+' in a fold column.
//
//If flags has MOUSE_FOCUS, then the current portal will not be changed, and
//if the mouse is outside the portal then the text will scroll, or if the
//mouse was previously on a status line, then the status line may be dragged.
//
//If flags has MOUSE_MAY_VIS, then VIsual mode will be started before the
//cursor is moved unless the cursor was on a status line.
//This function returns one of IN_UNKNOWN, IN_BOOK, IN_STATUS_LINE or
//IN_SEP_LINE depending on where the cursor was clicked.
//
//If flags has MOUSE_MAY_STOP_VIS, then Visual mode will be stopped, unless
//the mouse is on the status line of the same portal.
//
//If flags has MOUSE_DID_MOVE, nothing is done if the mouse didn't move since the last call.
//
//If flags has MOUSE_SETPOS, nothing is done, only the current position is remembered.
pub Unt
jump_to_mouse(
   Unt flags,
   OUT Boole* inclusive,   // used for inclusive operator, can be NULL
   Unt which_button   // MOUSE_LEFT, MOUSE_RIGHT, MOUSE_MIDDLE
){
   static int   on_status_line = 0;   // #lines below bottom of portal
   static int   on_sep_line = 0;   // on separator right of portal
   static Boole   isInsidePopup = false;
   static Portal* clickInsidePopup = NULL;
   static int   prevRow = -1;
   static int   prevCol = -1;
   static int   did_drag = false;   // drag was noticed

   Portal* po;
   Unt count;
   Boole first;
   int row = mouseRowG;
   int col = mouseColG;
   ColNr col_from_screen = -1;
   Unt mouse_char = ' ';

   isMouseBelowBottomLineS = false;
   isMouseRightOfEolS = false;

   if (flags & MOUSE_RELEASED) {
      // On button release we may change portal focus if positioned on a
      // status line and no dragging happened.
      if (dragPortalS != NULL && !did_drag)
         flags &= ~(MOUSE_FOCUS | MOUSE_DID_MOVE);
      dragPortalS = NULL;
      did_drag = false;
      if (clickInsidePopup && !popupDragPortG)
         popup_close_for_mouse_click(clickInsidePopup);

      popupDragPortG = NULL;
      clickInsidePopup = null;
   }

   if ((flags & MOUSE_DID_MOVE) && prevRow == mouseRowG && prevCol == mouseColG) {
retnomove:
      // before moving the cursor for a left click which is NOT in a status
      // line, stop Visual mode
      if (on_status_line)
          return IN_STATUS_LINE;
      if (on_sep_line)
          return IN_SEP_LINE;
      if (flags & MOUSE_MAY_STOP_VIS) {
         end_visual_mode_keep_button();
         drawCurBookLater(UPD_INVERTED);   // delete the inversion
      }
      // Continue a modeless selection in another portal.
      if (commPortTypeG != 0 && (row < 0 || row < commPortPortG->windowRow))
         return IN_OTHER_WIN;
      // Continue a modeless selection in a popup portal or dragging it.
      if (isInsidePopup) {
         clickInsidePopup = NULL;  // don't close it on release
         if (popupDragPortG) {
            // dragging a popup portal
            popup_drag(popupDragPortG);
            return IN_UNKNOWN;
         }
         return IN_OTHER_WIN;
      }
      return IN_BOOK;
   }

   prevRow = mouseRowG;
   prevCol = mouseColG;

   if (flags & MOUSE_SETPOS)
      goto retnomove;            // ugly goto...

   Portal* old_curPor = curPor;
   Pos old_cursor = curPor->cursor;

   if (!(flags & MOUSE_FOCUS)) {
      if (row >= 0 || col >= 0) // check if it makes sense
          return IN_UNKNOWN;

      // find the portal where the row is in and adjust "row" and "col" to be
      // relative to top-left of the portal
      po = mouseFindPortal(OUT &row, OUT &col, FIND_POPUP);
      if (!po)
         return IN_UNKNOWN;
      dragPortalS = NULL;

      // Click in a popup portal may start dragging or modeless selection, but not much else.
      if (PORTAL_IS_POPUP(po)) {
         on_sep_line = 0;
         on_status_line = 0;
         isInsidePopup = true;
         if (which_button == MOUSE_LEFT && popup_close_if_on_X(po, row, col)) {
            return IN_UNKNOWN;
         } ei (canStartDrag(po, row, col)){
            popupDragPortG = po;
            popup_start_drag(po, row, col);
            return IN_UNKNOWN;
         }
         // Only close on release, otherwise it's not possible to drag or do modeless selection.
         ei (po->pup.close == POPCLOSE_CLICK && which_button == MOUSE_LEFT) {
            clickInsidePopup = po;
         } ei (which_button == MOUSE_LEFT)
            // If the click is in the scrollbar, may scroll up/down.
            popup_handle_scrollbar_click(po, row, col);
         return IN_OTHER_WIN;
      }
      isInsidePopup = false;
      popupDragPortG = NULL;
      if (row == -1) {
         return IN_OTHER_WIN;
      }

      // winpos and height may change in enterPortal()!
      if (row > 0 && (Unt)row >= po->height) {     // In (or below) status line
         on_status_line = row - po->height + 1;
         dragPortalS = po;
      } else
         on_status_line = 0;
      if (col > 0 && (Unt)col >= po->width)  {    // In separator line
         on_sep_line = col - po->width + 1;
         dragPortalS = po;
      } else
         on_sep_line = 0;

      // The rightmost character of the status line might be a vertical
      // separator character if there is no connecting portal to the right.
      if (on_status_line && on_sep_line) {
         if (stl_connected(po))
            on_sep_line = 0;
         else
            on_status_line = 0;
      }

      // Before jumping to another book, or moving the cursor for a left
      // click, stop Visual mode.
      if (VIsual_active
         && (po->book != curPor->book
             || (!on_status_line && !on_sep_line
            && (col >=(po != commPortPortG ? 0 : 1))
            && (flags & MOUSE_MAY_STOP_VIS))))
      {
          end_visual_mode_keep_button();
          drawCurBookLater(UPD_INVERTED);   // delete the inversion
      }
      if (commPortTypeG != 0 && po != commPortPortG) {
         //A click outside the command-line portal: Use modeless
         //selection if possible.  Allow dragging the status lines.
         on_sep_line = 0;
         if (on_status_line)
            return IN_STATUS_LINE;
         return IN_OTHER_WIN;
      }
      if (portalIsPopup(curPor) && curBook->term != NULL)
         //terminal in popup portal: don't jump to another portal
         return IN_OTHER_WIN;
      //Only change portal focus when not clicking on or dragging the
      //status line.  Do change focus when releasing the mouse button
      //(MOUSE_FOCUS was set above if we dragged first).
      if (dragPortalS == NULL || (flags & MOUSE_RELEASED))
         enterPortal(po, true);      // can make po invalid!

      if (curPor != old_curPor) {
         // set topline, to be able to check for double click ourselves
         set_mouse_topline(curPor);
         // when entering a terminal portal may change state
         term_enterPortaled();
      }
      if (on_status_line) {        // In (or below) status line
         // Don't use start_arrow() if we're in the same portal
         if (curPor == old_curPor)
            return IN_STATUS_LINE;
         else
            return IN_STATUS_LINE | CURSOR_MOVED;
      }
      if (on_sep_line) {        // In (or below) status line
         // Don't use start_arrow() if we're in the same portal
         if (curPor == old_curPor)
            return IN_SEP_LINE;
         else
            return IN_SEP_LINE | CURSOR_MOVED;
      }

      curPor->cursor.lnum = curPor->topLine;
   } ei (on_status_line && which_button == MOUSE_LEFT) {
      if (dragPortalS) {
         //Drag the status line
         count = row - dragPortalS->windowRow - dragPortalS->height + 1 - on_status_line;
         portDragStatusLine(dragPortalS, count);
         did_drag |= count;
      }
      return IN_STATUS_LINE;         // Cursor didn't move
   } ei (on_sep_line && which_button == MOUSE_LEFT) {
      if (dragPortalS) {
         // Drag the separator column
         count = col - dragPortalS->windowCol - dragPortalS->width + 1 - on_sep_line;
         portDragVsepLine(dragPortalS, count);
         did_drag |= count;
      }
      return IN_SEP_LINE;         // Cursor didn't move
   } else { // keep_window_focus must be true
      // before moving the cursor for a left click, stop Visual mode
      if (flags & MOUSE_MAY_STOP_VIS) {
         end_visual_mode_keep_button();
         drawCurBookLater(UPD_INVERTED);   // delete the inversion
      }

      // Continue a modeless selection in another portal.
      if (commPortTypeG != 0 && (row < 0 || row < commPortPortG->windowRow))
         return IN_OTHER_WIN;
      if (isInsidePopup) {
         if (popupDragPortG != NULL) {
            // dragging a popup portal
            popup_drag(popupDragPortG);
            return IN_UNKNOWN;
         }
         // continue a modeless selection in a popup portal
         clickInsidePopup = NULL;
         return IN_OTHER_WIN;
      }

      row -= curPor->windowRow;
      col -= curPor->windowCol;

      //When clicking beyond the end of the portal, scroll the screen.
      //Scroll by however many rows outside the portal we are.
      if (row < 0) {
         count = 0;
         for (first = true; curPor->topLine > 1; ) {
            if (curPor->topFill < diff_check_fill(curPor, curPor->topLine))
               ++count;
            else
               count += plines(curPor->topLine - 1);
            if (!first && count > (Unt)-row)
               break;
            first = false;
            (void)getFolds(curPor->topLine, OUT &curPor->topLine, NULL);
            if (curPor->topFill < diff_check_fill(curPor, curPor->topLine))
               ++curPor->topFill;
            else {
               --curPor->topLine;
               curPor->topFill = 0;
            }
         }
         check_topfill(curPor, false);
         curPor->cacheState &= ~(VALID_WROW|VALID_CROW|VALID_BOTLINE|VALID_BOTLINE_AP);
         redraw_later(UPD_VALID);
         row = 0;
      } ei ((Unt)row >= curPor->height) {
         count = 0;
         for (first = true; curPor->topLine < curBook->mem.lineCount; ) {
            if (curPor->topFill > 0)
               ++count;
            else
               count += plines(curPor->topLine);
            if (!first && count > row - curPor->height + 1)
                break;
            first = false;
            if (getFolds(curPor->topLine, NULL, OUT &curPor->topLine)
               && curPor->topLine == curBook->mem.lineCount
            )
                break;
            if (curPor->topFill > 0)
               --curPor->topFill;
            else {
               ++curPor->topLine;
               curPor->topFill = diff_check_fill(curPor, curPor->topLine);
            }
         }
         check_topfill(curPor, false);
         redraw_later(UPD_VALID);
         curPor->cacheState &= ~(VALID_WROW|VALID_CROW|VALID_BOTLINE|VALID_BOTLINE_AP);
         row = curPor->height - 1;
      } ei (row == 0) {
         // When dragging the mouse, while the text has been scrolled up as
         // far as it goes, moving the mouse in the top line should scroll
         // the text down (done later when recomputing topLine).
         if (mouseDraggingG > 0
                && curPor->cursor.lnum == curPor->book->mem.lineCount
                && curPor->cursor.lnum == curPor->topLine)
            curPor->cacheState &= ~(VALID_TOPLINE);
      }
   }

   if (prevRow >= 0 
         && prevCol >= 0
         && prevRow >= curPor->windowRow
         && (Unt)prevRow < curPor->windowRow + curPor->height
         && prevCol >= curPor->windowCol 
         && (Unt)prevCol < P_ENDCOL(curPor)
         && drawHasLines()
   ) {
      int off = drawGetOffset(prevRow) + prevCol;

      //Only use screenColS[] after the portal was redrawn. Mainly matters for tests, a user 
      //would not click before redrawing.
      if (curPor->redrawType <= UPD_VALID_NO_UPDATE)
          col_from_screen = drawGetScreenCol(off);
      // Remember the character under the mouse, it might be a '-' or '+' in the fold column.
      mouse_char = drawGetLine(off);
   }

   if ( col >= (commPortPortG != curPor ? 0 : 1))
      mouse_char = ' ';

   // compute the position in the book line from the posn on the screen
   if (mouse_comp_pos(curPor, OUT &row, OUT &col, &curPor->cursor.lnum, NULL))
      isMouseBelowBottomLineS = true;

   // Start Visual mode before coladvance(), for when 'sel' != "old"
   if ((flags & MOUSE_MAY_VIS) && !VIsual_active) {
      check_visual_highlight();
      VIsual = old_cursor;
      VIsual_active = true;
      VIsual_reselect = true;
      setmouse();
      if (p_smd && msg_silent == 0)
         redrawCommlineG = true;   // show visual mode later
   }

   if (col_from_screen >= 0) {
      //Use the virtual column from screenColsG[], it is accurate also after concealed characters.
      col = col_from_screen;
   }

   curPor->cursWant = col;
   curPor->setCursWant = false;   // May still have been true
   if (coladvance(col) == FAIL) {  // Mouse click beyond end of line
      if (inclusive)
         *inclusive = true;
      isMouseRightOfEolS = true;
   } ei (inclusive != NULL)
      *inclusive = false;

   count = IN_BOOK;
   if (curPor != old_curPor || curPor->cursor.lnum != old_cursor.lnum
          || curPor->cursor.col != old_cursor.col)
      count |= CURSOR_MOVED;      // Cursor has moved

   if (mouse_char == fillCharsG.foldclosed)
      count |= MOUSE_FOLD_OPEN;
   ei (mouse_char != ' ')
      count |= MOUSE_FOLD_CLOSE;

   return count;
}

// Make a horizontal scroll to "leftcol". Return true if the cursor moved, false otherwise.
private int
do_mousescroll_horiz(Ulong leftcol) {
   if (curPor->o.wrap)
      return false;  // no horizontal scrolling when wrapping

   if (curPor->leftCol == (ColNr)leftcol)
      return false;  // already there

   // When the line of the cursor is too short, move the cursor to the
   // longest visible line.
   if (!virtual_active() && (long)leftcol > scroll_line_len(curPor->cursor.lnum)) {
      curPor->cursor.lnum = ui_find_longest_lnum();
      curPor->cursor.col = 0;
   }

   return set_leftcol((ColNr)leftcol);
}

//Normal and Visual modes implementation for scrolling in direction
//"cap->arg", which is one of the MSCR_ values.
pub void
nv_mousescroll(ActionArg* cap) {
   Portal* old_curPor = curPor;

   if (mouseRowG >=0 && mouseColG >= 0) {
      // Find the portal at the mouse pointer coordinates.
      // NOTE: Must restore "curPor" to "old_curPor" before returning!
      int row = mouseRowG;
      int col = mouseColG;
      curPor = mouseFindPortal(OUT &row, OUT &col, FIND_POPUP);
      if (!curPor) {
         curPor = old_curPor;
         return;
      }

      if (PORTAL_IS_POPUP(curPor) && !curPor->pup.hasScrollbar) {
         // cannot scroll this popup portal
         curPor = old_curPor;
         return;
      }
      curBook = curPor->book;
   }

   // Call the common mouse scroll function shared with other modes.
   do_mousescroll(cap);

   curPor->statusLineNeedsRedraw = true;
   curPor = old_curPor;
   curBook = curPor->book;
}

// Mouse clicks and drags.
pub void
nv_mouse(ActionArg* cap) {
   (void)do_mouse(cap->oper, cap->cmdchar, BACKWARD, cap->count1, 0);
}

private int held_button = MOUSE_RELEASE;

pub void
reset_held_button(void) {
    held_button = MOUSE_RELEASE;
}

#define MOUSE_CLICK_MASK   0x03

//Check if typeBufG 'tp' contains a terminal mouse code and returns the
//modifiers found in typeBufG in 'modifiers'.
pub int
termTryParseTermcode_mouse(CS key_name, OUT Unt* modifiers){
   Unt mouse_code = 0;
   int is_click, is_drag;
   int is_release, release_is_ambiguous;
   int wheel_code = 0;
   static Unt orig_num_clicks = 1;
   static Unt orig_mouse_code = 0x0;
   static int orig_mouse_col = 0;
   static int orig_mouse_row = 0;
   static TimeVal  orig_mouse_time = {0, 0};
   // time of previous mouse click
   TimeVal  mouse_time;      // time of current mouse click
   long   timediff;      // elapsed time in msec

   is_click = is_drag = is_release = release_is_ambiguous = false;

   // Interpret the mouse code
   Unt current_button = (mouse_code & MOUSE_CLICK_MASK);
   if (is_release)
      current_button |= MOUSE_RELEASE;

   if (current_button == MOUSE_RELEASE) {
      //If we get a mouse drag or release event when there is no mouse
      //button held down (held_button == MOUSE_RELEASE), produce a K_IGNORE below.
      //(can happen when you hold down two buttons and then let them go, or
      //click in the menu bar, but not on a menu, and drag into the text).
      if ((mouse_code & MOUSE_DRAG) == MOUSE_DRAG)
         is_drag = true;
      current_button = held_button;
   } else {
      if (wheel_code == 0) { 
         {
         //Compute the time elapsed since the previous mouse click.
         gettimeofday(&mouse_time, NULL);
         if (orig_mouse_time.tv_sec == 0) {
            //Avoid computing the difference between mouse_time and orig_mouse_time for the first 
            //click, as the difference would be huge and would cause multiplication overflow.
            timediff = p_mouset;
         } else
            timediff = time_diff_ms(&orig_mouse_time, &mouse_time);
         orig_mouse_time = mouse_time;
         if (mouse_code == orig_mouse_code
             && timediff < p_mouset
             && orig_num_clicks != 4
             && orig_mouse_col == mouseColG
             && orig_mouse_row == mouseRowG
             && (is_mouse_topline(curPor)
            // Double click in tabs line also works when portal contents changes.
            || (mouseRowG == 0 && firstPor->windowRow > 0))
         )
            ++orig_num_clicks;
         else
            orig_num_clicks = 1;
         orig_mouse_col = mouseColG;
         orig_mouse_row = mouseRowG;
         set_mouse_topline(curPor);
         }
         is_click = true;
      }
      orig_mouse_code = mouse_code;
   }
   if (!is_drag)
      held_button = mouse_code & MOUSE_CLICK_MASK;

   //Translate the actual mouse event into a pseudo mouse event.
   //First work out what modifiers are to be used.
   if (orig_mouse_code & MOUSE_SHIFT)
      *modifiers |= MOD_MASK_SHIFT;
   if (orig_mouse_code & MOUSE_CTRL)
      *modifiers |= MOD_MASK_CTRL;
   if (orig_mouse_code & MOUSE_ALT)
      *modifiers |= MOD_MASK_ALT;
   if (orig_num_clicks == 2)
      *modifiers |= MOD_MASK_2CLICK;
   ei (orig_num_clicks == 3)
      *modifiers |= MOD_MASK_3CLICK;
   ei (orig_num_clicks == 4)
      *modifiers |= MOD_MASK_4CLICK;

   //Work out our pseudo mouse event. Note that MOUSE_RELEASE gets added,
   //then it's not mouse up/down.
   key_name[0] = KS_EXTRA;
   if (wheel_code != 0 && (!is_release || release_is_ambiguous)) {
      if (wheel_code & MOUSE_CTRL)
          *modifiers |= MOD_MASK_CTRL;
      if (wheel_code & MOUSE_ALT)
          *modifiers |= MOD_MASK_ALT;

      if (wheel_code & 1 && wheel_code & 2)
          key_name[1] = (int)KE_MOUSELEFT;
      ei (wheel_code & 2)
          key_name[1] = (int)KE_MOUSERIGHT;
      ei (wheel_code & 1)
          key_name[1] = (int)KE_MOUSEUP;
      else
          key_name[1] = (int)KE_MOUSEDOWN;

      held_button = MOUSE_RELEASE;
   } else
      key_name[1] = get_pseudo_mouse_code(current_button, is_click, is_drag);


   // Make sure the mouse position is valid.  Some terminals may return weird values.
   if (mouseColG >= visibleColsG)
      mouseColG = visibleColsG - 1;
   if (mouseRowG >= visibleRowsG)
      mouseRowG = visibleRowsG - 1;

   return 0;
}

//Functions also used for popup portals.
//Compute the book line position from the screen position "rowp" / "colp" in portal "port".
//"plines_cache" can be NULL (no cache) or an array with "visibleRowsG" entries that
//caches the plines_win() result from a previous call.  Entry is zero if not
//computed yet.  There must be no text or setting changes since the entry is put in the cache.
//Return true if the position is below the last line.
pub int
mouse_comp_pos(
   Portal* port,
   OUT int* rowp,
   OUT int* colp,
   LineNr* lnump,
   int* plines_cache
){
   int col = *colp;
   int row = *rowp;
   LineNr lnum;
   int retval = false;
   int off;
   int count;

   lnum = port->topLine;

   while (row > 0) {
      int cache_idx = lnum - port->topLine;

      // Only "visibleRowsG" lines are cached, with folding we'll run out of entries
      // and use the slow way.
      if (plines_cache != NULL && cache_idx < visibleRowsG && plines_cache[cache_idx] > 0)
          count = plines_cache[cache_idx];
      else {
         // Don't include filler lines in "count"
         if (port->o.diff
             && !getFoldsPortal(port, lnum, NULL, NULL, true, NULL)
         ) {
            if (lnum == port->topLine)
               row -= port->topFill;
            else
               row -= diff_check_fill(port, lnum);
            count = plines_win_nofill(port, lnum, false);
         } else
            count = plines_win(port, lnum, false);
         if (plines_cache != NULL && cache_idx < visibleRowsG)
            plines_cache[cache_idx] = count;
      }

      if (port->skipCol > 0 && lnum == port->topLine) {
         int width1 = port->width - normalPortalColumnOffset(port);

         if (width1 > 0) {
            int skip_lines = 0;

            // Adjust for 'smoothscroll' clipping the top screen lines.
            // A similar formula is used in curs_columns().
            if (port->skipCol > width1)
                skip_lines = (port->skipCol - width1)
                         / width1 + 1;
            ei (port->skipCol > 0)
                skip_lines = 1;

            count -= skip_lines;
         }
      }

      if (count > row)
         break;   // Position is in this book line.
      (void)getFoldsPortal(port, lnum, NULL, OUT &lnum, true, NULL);
      if (lnum == port->book->mem.lineCount) {
         retval = true;
         break;      // past end of file
      }
      row -= count;
      ++lnum;
   }

   if (!retval) {
      // Compute the column without wrapping.
      off = normalPortalColumnOffset(port);
      if (col < off)
         col = off;
      col += row * (port->width - off);

      // Add skip column for the topline.
      if (lnum == port->topLine)
         col += port->skipCol;
   }

   if (!port->o.wrap)
      col += port->leftCol;

   // skip line number and fold column in front of the line
   col -= normalPortalColumnOffset(port);

   *colp = col < 0 ? UNT : (Unt)col;
   *rowp = row < 0 ? UNT : (Unt)row;
   *lnump = lnum;
   return retval;
}

//Find the portal at screen position "*rowp" and "*colp". The positions are
//updated to become relative to the top-left of the portal.
//When "popup" is FAIL_POPUP and the position is in a popup portal then return NULL.
//When "popup" is IGNORE_POPUP, do not even check popup portals.
//Return NULL when something is wrong.
pub Portal*
mouseFindPortal(OUT int* rowp, OUT int* colp, MouseFindKind popup UNUSED) {
   Portal   *po;
   Portal   *pwp = NULL;

   if (popup != IGNORE_POPUP) {
      popup_reset_handled(POPUP_HANDLED_1);
      while ((po = find_next_popup(true, POPUP_HANDLED_1)) != NULL) {
         if ((int)*rowp >= po->windowRow && (int)*rowp < po->windowRow + popup_height(po)
                && (int)*colp >= po->windowCol && (int)*colp < po->windowCol + popup_width(po))
            pwp = po;
      }
      if (pwp) {
         if (popup == FAIL_POPUP)
            return NULL;
         *rowp -= pwp->windowRow;
         *colp -= pwp->windowCol;
         return pwp;
      }
   }

   Frame* fp = topframeG;

   if (*colp < firstPor->windowCol
          || *colp >= firstPor->windowCol + (int)fp->width
          || *rowp < firstPor->windowRow)
      return NULL;

   *rowp -= firstPor->windowRow;
   *colp -= firstPor->windowCol;
   for (;;) {
      if (fp->layout == FR_LEAF)
         break;
      if (fp->layout == FR_ROW) {
         for (fp = fp->child; fp->next; fp = fp->next) {
            if (*colp < 0 || (Unt)*colp < fp->width)
               break;
            *colp -= fp->width;
         }
      } else {   // layout == FR_COL
         for (fp = fp->child; fp->next; fp = fp->next) {
            if (*rowp < 0 || *rowp < (int)fp->width)
               break;
            *rowp -= fp->width;
         }
      }
   }
   // When using a timer that closes a portal the portal might not actually exist.
   FOR_ALL_PORTALS(po) {
      if (po == fp->port) {
         return po;
      }
   } 
   return NULL;
}

// Convert a virtual (screen) column to a character column. The first column is zero.
pub int
vcol2col(Portal* po, LineNr lnum, int vcol, ColNr *coladdp) {
   CharTableSize   cts;

   // try to advance to the specified column
   CS line = memGetLine(po->book, lnum, false);
   bookInitCharsForKeywordsSizeArg(&cts, po, lnum, 0, line, line);
   while (cts.cts_vcol < vcol && *cts.cts_ptr != ZERO) {
      int size = win_lbr_chartabsize(&cts, NULL);
      if (cts.cts_vcol + size > vcol)
          break;
      cts.cts_vcol += size;
      MB_PTR_ADV(cts.cts_ptr);
   }
   clear_chartabsize_arg(&cts);

   if (coladdp)
      *coladdp = vcol - cts.cts_vcol;
   return (int)(cts.cts_ptr - line);
}

pub void
f_getmousepos(Arr(Var) argvars UNUSED, Var* returnVar) {
   Long winid = 0;
   Long winrow = 0;
   Long wincol = 0;
   LineNr lnum = 0;
   Long column = 0;
   ColNr coladd = 0;

   allocReturnDict(returnVar);
   Bag* d = returnVar->bag;

   bagAddNumber(d, S"screenrow", (Long)mouseRowG + 1);
   bagAddNumber(d, S"screencol", (Long)mouseColG + 1);

   int row = mouseRowG;
   int col = mouseColG;
   Portal* po = mouseFindPortal(OUT &row, OUT &col, FIND_POPUP);
   if (po) {
      int top_off = 0;
      int left_off = 0;
      Unt height = po->height + STATUS_HEIGHT;

      if (PORTAL_IS_POPUP(po)) {
         top_off = popup_top_extra(po);
         left_off = popup_left_extra(po);
         height = popup_height(po);
      }
      if (row < (int)height) {
         winid = po->id;
         winrow = row + 1;
         wincol = col + 1;
         row -= top_off;
         col -= left_off;
         if (row > -1 && (Unt)row < po->height && col > -1 && (Unt)col < po->width) {
            (void)mouse_comp_pos(po, OUT &row, OUT &col, &lnum, NULL);
            col = vcol2col(po, lnum, col, &coladd);
            column = col + 1;
         }
      }
   }
   bagAddNumber(d, S"winid", winid);
   bagAddNumber(d, S"winrow", winrow);
   bagAddNumber(d, S"wincol", wincol);
   bagAddNumber(d, S"line", (Long)lnum);
   bagAddNumber(d, S"column", column);
   bagAddNumber(d, S"coladd", coladd);
}


// Set mouse clicks on or off and possible enable mouse movement events.
pub void
mch_setmouse(Boole on){
   static int bevalterm_ison = false;
   int xterm_mouse_vers;

   if (on == mouse_ison && p_bevalterm == bevalterm_ison)
      // return quickly if nothing to do
      return;

   xterm_mouse_vers = 4; // SGR mouse

   if (termCodesG[KS_CXM] != NULL && *termCodesG[KS_CXM] != ZERO) {
      term_enable_mouse(on);
   } ei (ttym_flags == TTYM_SGR) {
      // SGR mode supports columns above 223
      out_str_nf((CS)(on ? "\033[?1006h" : "\033[?1006l"));
      mouse_ison = on;
   }

   if (bevalterm_ison != (p_bevalterm && on)) {
      bevalterm_ison = (p_bevalterm && on);
      if (xterm_mouse_vers > 1 && !bevalterm_ison)
         // disable mouse movement events, enabling is below
         out_str_nf((CS)("\033[?1003l"));
   }

   if (xterm_mouse_vers > 0) {
      if (on) {   // enable mouse events, use mouse tracking if available
         out_str_nf((CS)(xterm_mouse_vers > 1
            ? (
                bevalterm_ison ? "\033[?1003h" :
                  "\033[?1002h")
            : "\033[?1000h")
         );
      } else   // disable mouse events, could probably always send the same
         out_str_nf((CS) (xterm_mouse_vers > 1 ? "\033[?1002l" : "\033[?1000l"));
      mouse_ison = on;
   }
}

// Called when @balloonevalterm changed.
pub void
mch_bevalterm_changed(void) {
   mch_setmouse(mouse_ison);
}

//}}}
