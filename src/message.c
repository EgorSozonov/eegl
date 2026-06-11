//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## message.c: functions for displaying messages on the command line

#define MESSAGE_FILE      // don't include prototype for smsg()

#include "eegl.h"

typedef struct MsgHist MsgHist;
struct MsgHist {
   MsgHist* next;
   CS c;
   char deco;
};

private MsgHist *first_msg_hist = NULL;
private MsgHist *last_msg_hist = NULL;
private int msg_hist_len = 0;
private int msg_hist_max = 500;      // The default max value is 500
                   
// flags obtained from the 'messagesopt' option
#define MESSAGES_HIT_ENTER   0x001
#define MESSAGES_WAIT      0x002
#define MESSAGES_HISTORY   0x004

// args in 'messagesopt' option
#define MESSAGES_OPT_HIT_ENTER "hit-enter"
#define MESSAGES_OPT_WAIT "wait:"
#define MESSAGES_OPT_HISTORY "history:"

// The default is "hit-enter,history:500"
private int msg_flags = MESSAGES_HIT_ENTER | MESSAGES_HISTORY;
private int msg_wait = 0;
private FILE *verbose_fd = NULL;
private int  verbose_did_open = false;

// Text builder holding one message line, sized up to the longest line ever printed
private Text longestLineS = (Text){.len = 0, .c = null};

declStruct(MsgChunk);
private MsgChunk *lastChunkS = NULL; // last displayed text

//{{{forward declarations

private void addMsgHistory(CS s, int len, char flags);
private void check_msg_hist(void);
private void hit_return_msg(void);
private void homeReplaceDeco(Byte *fname, char flags);
private void printWithDecoAndMaxLen(Arr(Byte const) str, int maxlen, char flags);
private void toDisplay(Byte *str, int maxlen, char flags, int recurse);
private void inc_msg_scrolled(void);
private void saveToScrollback(Byte **sb_str, Byte *s, char flags, int *sb_col, int finish);
private void t_puts(int *t_col, Byte *t_s, Byte *s, char flags);
private void toPrintf(Byte *str, int maxlen);
private int do_more_prompt(int typedChar);
private void msg_screen_putchar(int c, char flags);
private void msg_moremsg(int full);
private int  msg_check_screen(void);
private void redir_write(Byte *s, int maxlen);
private Byte *msg_show_console_dialog(Byte *message, Byte *buttons, int dfltbutton);
private int   confirm_msg_used = false;   // displaying confirm_msg
private Byte   *confirm_msg = NULL;      // ":confirm" message
private Byte   *confirm_msg_tail;      // tail of confirm_msg
private void display_confirm_msg(void);
private int emsg_to_channel_log = false;
private Boole isVerboseFileDefined();
private MsgChunk *moveToStartOfScreenLine(MsgChunk *mps);
private MsgChunk * disp_sb_line(int row, MsgChunk *smp, int clear_to_eol);

//}}}

//When writing messages to the screen, there are many different situations.
//A number of variables is used to remember the current state:
//msg_didany       true when messages were written since the last time the user reacted to a prompt.
//         Reset: After hitting a key for the hit-return prompt,
//         hitting <CR> for the command line or input().
//         Set: When any message is written to the screen.
//msg_didout       true when something was written to the current line.
//         Reset: When advancing to the next line, when the current
//         text can be overwritten.
//         Set: When any message is written to the screen.
//msg_nowait       No extra delay for the last drawn message.
//         Used in normal_cmd() before the mode message is drawn.
//emsg_on_display  There was an error message recently.  Indicates that there
//         should be a delay before redrawing.
//msg_scroll       The next message should not overwrite the current one.
//msg_scrolled       How many lines the screen has been scrolled (because of
//         messages).  Used in drawUpdateScreen() to scroll the screen
//         back.  Incremented each time the screen scrolls a line.
//msg_scrolled_ign  true when msg_scrolled is non-zero and msgPutsDeco()
//         writes something without scrolling should not make
//         need_wait_return to be set.  This is a hack to make ":ts"
//         work without an extra prompt.
//lines_left       Number of lines available for messages before the
//         more-prompt is to be given.  -1 when not set.
//need_wait_return true when the hit-return prompt is needed.
//         Reset: After giving the hit-return prompt, when the user
//         has answered some other prompt.
//         Set: When the ruler or typeahead display is overwritten,
//         scrolling the screen for some message.
//msgAfterRedrawG       Message to be displayed after redrawing the screen, in
//         main_loop().
//         This is an allocated string or NULL when not used.

//{{{message utils

//Truncate a string such that it can be printed without causing a scroll.
//Return an allocated string or NULL when no truncating is done.
CS
msg_strtrunc(CS s, int force) {      // always truncate
   CS buf = null;

   // May truncate message to avoid a hit-return prompt
   if ((!msg_scroll && !need_wait_return && msg_silent == 0) || force) {
      int len = eeglStrSize(s);
      int room;
      if (msg_scrolled != 0 || inEchoPortalG)
         // Use all the columns.
         room = (int)(visibleRowsG - msgRowG) * visibleColsG - 1;
      else
         // Use up to 'showcmd' column.
         room = (int)(visibleRowsG - msgRowG - 1) * visibleColsG + sc_col - 1;
      if (len > room && room > 0) {
         // UTF-8 may have up to 18 bytes per cell (6 per char, up to two composing chars)
         len = (room + 2) * 18;
         buf = alloc(len);
         trunc_string(s, buf, room, len);
      }
   }
   return buf;
}

// Truncate a string "s" to "builder" with cell width "room". "s" and "builder" may be equal
void
trunc_string(
   Byte   *s,
   Byte   *builder,
   int      room_in,
   int      buflen)
{
   Unt   room = room_in - 3; // "..." takes 3 chars
   Unt   half;
   Unt   len = 0;
   int      e;
   int      i;
   int      n;

   if (*s == ZERO) {
      if (buflen > 0)
         *builder = ZERO;
      return;
   }

   if (room_in < 3)
      room = 0;
   half = room / 2;

   // First part: Start of the string.
   for (e = 0; len < half && e < buflen; ++e) {
      if (s[e] == ZERO) {
         // text fits without truncating!
         builder[e] = ZERO;
         return;
      }
      n = ptr2cells(s + e);
      if (len + n > half)
          break;
      len += n;
      builder[e] = s[e];
      for (n = utfCharLen(s + e); --n > 0; ) {
         if (++e == buflen)
             break;
         builder[e] = s[e];
      }
   }

   // Last part: End of the string.
   i = e;
   // For UTF-8 we can go backwards easily.
   half = i = (int)STRLEN(s);
   for (;;) {
      do {
         half = half - mb_head_off(s, s + half - 1) - 1;
      } while (half > 0 && utf_iscomposing(mb_ptr2char(s + half)));
      n = ptr2cells(s + half);
      if (len + n > room || half == 0)
         break;
      len += n;
      i = (int)half;
   }


   if (i <= e + 3) {
      // text fits without truncating
      if (s != builder) {
         len = STRLEN(s);
         if (len >= (Unt)buflen)
            len = buflen - 1;
         len = len - e + 1;
         if (len < 1)
            builder[e - 1] = ZERO;
         else
            mch_memmove(builder + e, s + e, len);
      }
   } ei (e + 3 < buflen) {
      // set the middle and copy the last part
      mch_memmove(builder + e, "...", (Unt)3);
      len = STRLEN(s + i) + 1;
      if (len >= (Unt)buflen - e - 3)
          len = buflen - e - 3 - 1;
      mch_memmove(builder + e + 3, s + i, len);
      builder[e + 3 + len - 1] = ZERO;
   } else {
      // can't fit in the "...", just truncate it
      builder[buflen - 1] = ZERO;
   }
}

// Prepare for outputting characters in the command line.
void
msg_start(void) {
   int      did_return = false;

   if (msgRowG < commlineRowG)
      msgRowG = commlineRowG;

   if (!msg_silent) {
      EE_CLEAR(msgAfterRedrawG);
      needFileinfoG = false;
   }

   if (need_clr_eos) {
      // Halfway an ":echo" command and getting an (error) message: clear
      // any text from the command.
      need_clr_eos = false;
      msg_clr_eos();
   }

   if (inEchoPortalG) {
      if (popup_message_win_visible()
             && ((msgColG > 0 && (msg_scroll || !fullScreenG)) || inEchoPortalG)) {
          Portal *wp = popup_get_messagePort();

          // start a new line
          curBook = wp->book;
          ml_append(wp->book->mem.lineCount,
                        (CS)"", (ColNr)0, false);
          curBook = curPor->book;
      }
      msgColG = 0;
   } else
   if (!msg_scroll && fullScreenG) {  // overwrite last message
      msgRowG = commlineRowG;
      msgColG = 0;
   } ei (msg_didout || inEchoPortalG) {
      // start message on next line
      msg_putchar('\n');
      did_return = true;
      commlineRowG = msgRowG;
   } 
   if (!msg_didany || lines_left < 0)
      msg_starthere();
   if (msg_silent == 0) {
      msg_didout = false;          // no output on current line yet
      cursor_off();
   }

   // when redirecting, may need to start a new line.
   if (!did_return)
      redir_write((CS)"\n", -1);
}

// Note that the current msg position is where messages start.
void
msg_starthere(void) {
   lines_left = commlineRowG;
   msg_didany = false;
}

void
msg_putchar(Unt c) {
   msgPutcharDeco(c, 0);
}

void
msgPutcharDeco(Unt c, char flags) {
   Byte   builder[MB_MAXBYTES + 1];

   if (IS_SPECIAL(c)) {
      builder[0] = K_SPECIAL;
      builder[1] = K_SECOND(c);
      builder[2] = K_THIRD(c);
      builder[3] = ZERO;
   } else
      builder[mb_char2bytes(c, builder)] = ZERO;
   msgPutsDeco(builder, flags);
}

void
msg_outnum(long n) {
   Byte builder[20];

   sprintf((char*)builder, "%ld", n);
   msg_puts(builder);
}

void
msg_home_replace(Byte *fname) {
   homeReplaceDeco(fname, 0);
}

void
msg_home_replace_hl(Byte *fname) {
   homeReplaceDeco(fname, getDecoFlags(HLF_D));
}

private void
homeReplaceDeco(CS fname, char flags) {
   CS name = home_replace_save(NULL, fname);
   if (name)
      msgOuttransDeco(name, flags);
   eeglFree(name);
}

// Output 'len' characters in 'str' (including ZEROSs) with translation if 'len' is -1, 
// output up to a ZERO character. Return the number of characters it takes on the screen.
int
msg_outtrans(Byte* str) {
   return msgOuttransDeco(str, 0);
}

int
msgOuttransDeco(CS str, Byte flags) {
   return msgOuttransLenDeco(mbText(str), flags);
}

int
msgTranslatedSlice(Text slice) {
    return msgOuttransLenDeco(slice, 0);
}

// Output one character at "p". Return pointer to the next character. Handle multi-byte characters
CS
msgOneChar(CS p, char flags) {
   int l;

   if ((l = utfCharLen(p)) > 1) {
      msgOuttransLenDeco((Text){p, l}, flags);
      return p + l;
   }
   msgPutsDeco(transchar_byte(*p), flags);
   return p + 1;
}

int
msgOuttransLenDeco(Text slice, char flags) {
   int      retval = 0;
   Arr(Byte) str = slice.c;
   Arr(Byte) plain_start = slice.c;
   Byte* s;
   int      mb_l;
   int      c;
   int      save_gotInterruptG = gotInterruptG;

   // Only quit when gotInterruptG was set in here.
   gotInterruptG = false;

   if (flags == 0)
      flags = getDecoFlags(HLF_MSG);

   // if MSG_HIST flag set, add message to history
   if (flags & MSG_HIST) {
      addMsgHistory(slice.c, slice.len, flags);
      flags &= ~MSG_HIST;
   }

   // When drawing over the command line no need to clear it later or remove
   // the mode message.
   if (msg_silent == 0 && slice.len > 0 && msgRowG >= commlineRowG && msgColG == 0) {
      mustClearCommlineG = false;
      isModeDisplayedG = false;
   }

   // If the string starts with a composing character, first draw a space on which the composing 
   // char can be drawn
   if (utf_iscomposing(mb_ptr2char(str)))
      msgPutsDeco(S" ", flags);

   // Go over the string.  Special characters are translated and printed.
   // Normal characters are printed several at a time.
   
   for (int len = slice.len; --len >= 0 && !gotInterruptG;) {
      // Don't include composing chars after the end.
      mb_l = utfCharLen_len(str, len + 1);
      if (mb_l > 1) {
         c = (*mb_ptr2char)(str);
         if (bookIsCharPrintable(c))
            // printable multi-byte char: count the cells.
            retval += mb_ptr2cells(str);
         else {
            // unprintable multi-byte char: print the printable chars so far and the translation 
            // of the unprintable one
            if (str > plain_start)
               printWithDecoAndMaxLen(plain_start, (int)(str - plain_start), flags);
            plain_start = str + mb_l;
            msgPutsDeco(transchar_buf(c), flags == 0 ? getDecoFlags(HLF_8) : flags);
            retval += char2cells(c);
         }
         len -= mb_l - 1;
         str += mb_l;
      } else {
         s = transchar_byte(*str);
         if (s[1] != ZERO) {
            // unprintable char: print the printable chars so far and the
            // translation of the unprintable char.
            if (str > plain_start)
               printWithDecoAndMaxLen(plain_start, (int)(str - plain_start), flags);
            plain_start = str + 1;
            msgPutsDeco(s, flags == 0 ? getDecoFlags(HLF_8) : flags);
            retval += (int)STRLEN(s);
         } else
            ++retval;
         ++str;
      }
   }

   if (str > plain_start && !gotInterruptG)
      // print the printable chars at the end
      printWithDecoAndMaxLen(plain_start, (int)(str - plain_start), flags);

   gotInterruptG |= save_gotInterruptG;

   return retval;
}

void
msg_make(CS arg) {
   int       i;
   static CS str = S"eeffoc";
   static CS rs = S"Plon#dqg#vxjduB";

   arg = skipwhite(arg);
   for (i = 5; *arg && i >= 0; --i) {
      if (*arg++ != str[i])
          break;
   } 
   if (i < 0) {
      msg_putchar('\n');
      for (i = 0; rs[i]; ++i)
         msg_putchar(rs[i] - 3);
   }
}

//Output the string 'str' up to a ZERO character.
//Return the number of characters it takes on the screen.
//If K_SPECIAL is encountered, then it is taken in conjunction with the
//following character and shown as <F1>, <S-Up> etc.  Any other character
//which is not printable shown in <> form.
//If 'from' is true (lhs of a mapping), a space is shown as <Space>.
//If a character is displayed in one of these special ways, is also
//highlighted (its highlight name is '8' in the p_hl variable).
//Otherwise characters are not highlighted.
//This function is used to show mappings, where we want to see how to type
//the character/string -- webb
int
msg_outtrans_special(
   CS strstart,
   int from,   // true for lhs of a mapping
   int maxlen  // screen columns, 0 for unlimited
){
   CS str = strstart;
   int retval = 0;
   CS text;
   int len;

   Byte flags = getDecoFlags(HLF_8);
   while (*str != ZERO) {
      // Leading and trailing spaces need to be displayed in <> form.
      if ((str == strstart || str[1] == ZERO) && *str == ' ') {
         text = S"<Space>";
         ++str;
      } else
         text = str2special(&str, from, false);
      if (text[0] != ZERO && text[1] == ZERO)
         // single-byte character or illegal byte
         text = transchar_byte((Byte)text[0]);
      len = eeglStrSize((CS)text);
      if (maxlen > 0 && retval + len >= maxlen)
         break;
      // Highlight special keys
      msgPutsDeco(text, len > 1 && utfCharLen((CS)text) <= 1 ? flags : 0);
      retval += len;
    }
    return retval;
}

//Return the lhs or rhs of a mapping, with the key codes turned into printable
//strings, in an allocated string.
CS
str2special_save(
   Byte  *str,
   int       replace_spaces,   // true to replace " " with "<Space>".
            // used for the lhs of mapping and keytrans().
   int       replace_lt      // true to replace "<" with "<lt>".
){
   ArrayList   ga;
   Byte   *p = str;

   ga_init2(&ga, 1, 40);
   while (*p != ZERO)
      ga_concat(&ga, str2special(&p, replace_spaces, replace_lt));
   ga_append(&ga, ZERO);
   return (CS)ga.c;
}

//Return the printable string for the key codes at "*sp". On illegal byte return a string with only
//that byte. Used for translating the lhs or rhs of a mapping to printable chars.
//Advances "sp" to the next code.
CS
str2special(
   Byte** sp,
   int      replace_spaces,   // true to replace " " with "<Space>".
            // used for the lhs of mapping and keytrans().
   int  replace_lt)   // true to replace "<" with "<lt>".
{
   Unt         c;
   static Byte   builder[7];
   Byte      *str = *sp;
   int         modifiers = 0;
   int         special = false;

   Byte   *p;

   // Try to un-escape a multi-byte character. Return the un-escaped
   // string if it is a multi-byte character.
   p = mb_unescape(sp);
   if (p)
      return p;

   c = *str;
   if ((c == K_SPECIAL) && str[1] != ZERO && str[2] != ZERO) {
      if (str[1] == KS_MODIFIER) {
         modifiers = str[2];
         str += 3;
         c = *str;
      }
      if ((c == K_SPECIAL) && str[1] != ZERO && str[2] != ZERO) {
         c = TO_SPECIAL(str[1], str[2]);
         str += 2;
      }
      if (IS_SPECIAL(c) || modifiers)   // special key
         special = true;
    }

   if (!IS_SPECIAL(c) && utf8CharLens[c] > 1) {
      *sp = str;
      // Try to un-escape a multi-byte character after modifiers.
      CS p = mb_unescape(sp);
      if (p)
         //Since 'special' is true the multi-byte character 'c' will be
         //processed by get_special_key_name()
         c = (*mb_ptr2char)(p);
      else
         //illegal byte
         *sp = str + 1;
   } else
      //single-byte character, ZERO or illegal byte
      *sp = str + (*str == ZERO ? 0 : 1);

   //Make special keys and C0 control characters in <> form, also <M-Space>.
   if (special
         || c < ' '
         || (replace_spaces && c == ' ')
         || (replace_lt && c == '<'))
      return get_special_key_name(c, modifiers);
   builder[0] = c;
   builder[1] = ZERO;
   return builder;
}

// Translate a key sequence into special key names.
void
str2specialbuf(Byte *sp, OUT Byte *builder, int len) {
   *builder = ZERO;
   while (*sp) {
      Byte* s = str2special(&sp, false, false);
      if ((int)(STRLEN(s) + STRLEN(builder)) < len)
         STRCAT(builder, s);
   }
}

// print line for :print or :list command
void
msg_prt_line(CS s, int list) {
   Unt      c;
   int      col = 0;
   int      n_extra = 0;
   Unt      c_extra = 0;
   int      c_final = 0;
   CS p_extra = NULL;       // init to make SASC shut up
   int      n;
   char      flags = 0;
   CS trail = NULL;
   CS lead = NULL;
   int      in_multispace = false;
   int      multispace_pos = 0;
   int      l;
   Byte builder[MB_MAXBYTES + 1];

   if (curPor->o.list)
      list = true;

   if (list) {
      // find start of trailing whitespace
      if (listCharsG.trail) {
          trail = s + STRLEN(s);
          while (trail > s && SPACE_OR_TAB(trail[-1]))
         --trail;
      }
      // find end of leading whitespace
      if (listCharsG.lead || listCharsG.leadmultispace) {
         lead = s;
         while (SPACE_OR_TAB(lead[0]))
            lead++;
         // in a line full of spaces all of them are treated as trailing
         if (*lead == ZERO)
            lead = NULL;
      }
   }

   // output a space for an empty line, otherwise the line will be
   // overwritten
   if (*s == ZERO && !(list && listCharsG.eol != ZERO))
      msg_putchar(' ');

   while (!gotInterruptG) {
      if (n_extra > 0) {
         --n_extra;
         if (n_extra == 0 && c_final)
            c = c_final;
         ei (c_extra > 0)
            c = c_extra;
         else
            c = *p_extra++;
      } ei ((l = utfCharLen(s)) > 1) {
         col += mb_ptr2cells(s);
         if (l >= MB_MAXBYTES) {
            STRCPY(builder, "?");
         } ei (listCharsG.nbsp != ZERO && list
             && (mb_ptr2char(s) == 160 || mb_ptr2char(s) == 0x202f)
         ){
            int len = mb_char2bytes(listCharsG.nbsp, builder);
            builder[len] = ZERO;
         } else {
            mch_memmove(builder, s, (Unt)l);
            builder[l] = ZERO;
         }
         msg_puts(builder);
         s += l;
         continue;
      } else {
         flags = 0;
         c = *s++;
         if (list) {
            in_multispace = c == ' ' && (*s == ' ' || (col > 0 && s[-2] == ' '));
            if (!in_multispace)
               multispace_pos = 0;
         }
         if (c == TAB && (!list || listCharsG.tab1)) {
            // tab amount depends on current column
            n_extra = 0;
            if (!list) {
               c = ' ';
               c_extra = ' ';
               c_final = ZERO;
            } else {
               c = (n_extra == 0 && listCharsG.tab3) ? listCharsG.tab3 : listCharsG.tab1;
               c_extra = listCharsG.tab2;
               c_final = listCharsG.tab3;
               flags = getDecoFlags(HLF_8);
            }
         } ei (c == 160 && list && listCharsG.nbsp != ZERO) {
            c = listCharsG.nbsp;
            flags = getDecoFlags(HLF_8);
         } ei (c == ZERO && list && listCharsG.eol != ZERO) {
            p_extra = Em;
            c_extra = ZERO;
            c_final = ZERO;
            n_extra = 1;
            c = listCharsG.eol;
            flags = getDecoFlags(HLF_AT);
            --s;
         } ei (c != ZERO && (n = byte2cells(c)) > 1) {
            n_extra = n - 1;
            p_extra = transchar_byte(c);
            c_extra = ZERO;
            c_final = ZERO;
            c = *p_extra++;
            // Use special coloring to be able to distinguish <hex> from
            // the same in plain text.
            flags = getDecoFlags(HLF_8);
         } ei (c == ' ') {
            if (lead && s <= lead && in_multispace && listCharsG.leadmultispace) {
               c = listCharsG.leadmultispace[multispace_pos++];
               if (listCharsG.leadmultispace[multispace_pos] == ZERO)
                  multispace_pos = 0;
               flags = getDecoFlags(HLF_8);
            } ei (lead && s <= lead && listCharsG.lead != ZERO) {
                c = listCharsG.lead;
                flags = getDecoFlags(HLF_8);
            } ei (trail && s > trail) {
                c = listCharsG.trail;
                flags = getDecoFlags(HLF_8);
            } ei (in_multispace && listCharsG.multispace) {
                c = listCharsG.multispace[multispace_pos++];
                if (listCharsG.multispace[multispace_pos] == ZERO)
               multispace_pos = 0;
                flags = getDecoFlags(HLF_8);
            } ei (list && listCharsG.space != ZERO) {
                c = listCharsG.space;
                flags = getDecoFlags(HLF_8);
            }
         }
      }

      if (c == ZERO)
          break;

      msgPutcharDeco(c, flags);
      col++;
   }
   msg_clr_eos();
}

// Use drawText() to output one multi-byte character.
// Return the pointer "s" advanced to the next character.
private CS
drawText_mbyte(CS s, int l, char flags) {
   msg_didout = true;      // remember that line is not empty
   int cw = mb_ptr2cells(s);
   if (cw > 1 && ( msgColG == visibleColsG - 1)) {
      // Doesn't fit, print a highlighted '>' to fill it up.
      msg_screen_putchar('>', getDecoFlags(HLF_AT));
      return s;
   }

   drawTextLen(s, l, msgRowG, msgColG, flags);
   msgColG += cw;
   if (msgColG >= visibleColsG) {
      msgColG = 0;
      ++msgRowG;
   }
   return s + l;
}

private void
ensureLength(Unt len) {
   if (longestLineS.len > len) {
      eeglFree(longestLineS.c);
      longestLineS.c = alloc(len);
      longestLineS.len = len;
   }
}

// Print a message when there is no valid screen.
private void
toPrintf(CS str, int maxlen) {
   CS bbb = str;
   Boole isSilent = silentModeG && p_verbose == 0;

   for (CS aaa = str; (maxlen < 0 || (int)(aaa - str) < maxlen) && *aaa != ZERO; aaa++) {
      if (isSilent) {
         goto skipped;
      }
      // print linewise with NL --> CR NL translation (for Unix, not for "--version")
      if (*aaa == NL) {
         int n = (int)(aaa - bbb);
         if (n > 0) {
            ensureLength((Unt)n + 3);
            memcpy(longestLineS.c, bbb, n);
            if (!info_message) {
               longestLineS.c[n] = ENTER;
               n++;
            } 
            longestLineS.c[n] = NL;
            n++;
            longestLineS.c[n] = ZERO;
            n++; 
            if (info_message)   // informative message, not an error
               mch_msg(longestLineS.c);
            else
               mch_errmsg(longestLineS.c);
         } else {
            if (info_message)   // informative message, not an error
               mch_msg(S"\r\n");
            else
               mch_errmsg(S"\r\n");
         }
         bbb = aaa + 1;
      }
skipped:
      // primitive way to compute the current column
      if (*aaa == ENTER || *aaa == NL)
         msgColG = 0;
      else
         ++msgColG;
   }

   if (bbb && *bbb != ZERO && !isSilent) {
      if (info_message)
         mch_msg(bbb);
      else
         mch_errmsg(bbb);
   }

   msg_didout = true;       // assume that line is not empty
}

private Boole
isVerboseFileDefined() {
   return p_vfile != null;
}

//}}}
//{{{info messages

//msg(s) - displays the string 's' on the status line
//When terminal not initialized (yet) mch_errmsg(..) is used.
//return true if wait_return() not called
int
msg(CS s) {
   return msgAndKeep(s, 0, false);
}

// Like msg() but keep it silent when 'verbosefile' is set.
int
verb_msg(CS s) {
   verbose_enter();
   int n = msgAndKeep(s, 0, false);
   verbose_leave();

   return n;
}

int
msgDeco(CS s, char flags) {
   return msgAndKeep(s, flags, false);
}

int
msgAndKeep(
   CS s,
   char      flags,
   int      keep)       // true: set msgAfterRedrawG if it doesn't scroll
{
   static int   entered = 0;
   int      retval;

   // Skip messages not matching ":filter pattern".
   // Don't filter when there is an error.
   if (!emsg_on_display && message_filtered(s))
      return true;

   if (flags == 0)
      set_EeglVar_string(VV_STATUSMSG, s, -1);

   //It is possible that displaying a messages causes a problem (e.g.,
   //when redrawing the window), which causes another message, etc..   To
   //break this loop, limit the recursiveness to 3 levels.
   if (entered >= 3)
      return true;
   ++entered;

   // Add message to history (unless it's a repeated kept message or a
   // truncated message)
   if ((CS)s != msgAfterRedrawG
          || (*s != '<' && last_msg_hist && last_msg_hist->c && STRCMP(s, last_msg_hist->c))
   )
      addMsgHistory((CS)s, -1, flags);

   if (emsg_to_channel_log)
      // Write message in the channel log.
      lo("ERROR: %s", s);

   // Truncate the message if needed.
   msg_start();
   Arr(Byte) builder = msg_strtrunc(s, false);
   if (builder)
      s = builder;

   msgOuttransDeco((CS)s, flags);
   msg_clr_eos();
   retval = msg_end();

   if (keep && retval && eeglStrSize((CS)s)
             < (int)(visibleRowsG - commlineRowG - 1) * visibleColsG + sc_col)
      set_keep_msg(s, 0);

   needFileinfoG = false;

   eeglFree(builder);
   --entered;
   return retval;
}

// Automatic prototype generation does not understand this function. Their protos are in eegl.h
// Note: Caller of smsg() and smsgDeco() must check the resulting string is shorter than IOSIZE!!!
#ifndef PROTO

int eeSnprintf0(CS str, Unt str_m, const char *fmt, ...);

int
smsg0(char const* s, ...) {
   if (!IObuff) {
      // Very early in initialisation and already something wrong, just
      // give the raw message so the user at least gets a hint.
      return msg((CS)s);
   }

   va_list arglist;

   va_start(arglist, s);
   eeVsnprintf(IObuff, IOSIZE, s, arglist);
   va_end(arglist);
   return msg(IObuff);
}

int
smsgDeco0(char flags, const char *s, ...) {
   if (!IObuff) {
      // Very early in initialisation and already something wrong, just
      // give the raw message so the user at least gets a hint.
      return msgDeco((CS)s, flags);
   }

   va_list arglist;

   va_start(arglist, s);
   eeVsnprintf(IObuff, IOSIZE, s, arglist);
   va_end(arglist);
   return msgDeco(IObuff, flags);
}

int
smsgDecoKeep0(char flags, const char *s, ...) {
   if (!IObuff) {
      // Very early in initialisation and already something wrong, just
      // give the raw message so the user at least gets a hint.
      return msgAndKeep((CS)s, flags, true);
   }

   va_list arglist;

   va_start(arglist, s);
   eeVsnprintf(IObuff, IOSIZE, s, arglist);
   va_end(arglist);
   return msgAndKeep(IObuff, flags, true);
}

#endif

//Remember the last sourcing name/lnum used in an error message, so that it
//isn't printed each time when it didn't change.
private int   last_sourcing_lnum = 0;
private Byte   *last_sourcing_name = NULL;

// Reset the last used sourcing name/lnum. Makes sure it's displayed again for the next error msg
void
reset_last_sourcing(void) {
   EE_CLEAR(last_sourcing_name);
   last_sourcing_lnum = 0;
}

#define HAVE_SOURCING_INFO  (exestack.c != NULL && exestack.len > 0)

// Return true if "SOURCING_NAME" differs from "last_sourcing_name".
private int
other_sourcing_name(void) {
   if (HAVE_SOURCING_INFO && SOURCING_NAME) {
      if (last_sourcing_name)
         return STRCMP(SOURCING_NAME, last_sourcing_name) != 0;
      return true;
   }
   return false;
}

//Like msg(), but truncate to a single line if "force" is true. This 
//truncates in another way as for normal messages. Careful: The string may be changed by 
//msg_may_trunc()! Returns a pointer to the printed message, if wait_return() not called.
CS
msgTruncDeco(CS s, char flags) {
   // Add message to history before truncating
   addMsgHistory((CS)s, -1, flags);

   CS ts = msg_may_trunc(s);

   msg_hist_off = true;
   int n = msgDeco(ts, flags);
   msg_hist_off = false;

   if (n)
      return ts;
   return NULL;
}

//Check if message "s" should be truncated at the start (for filenames).
//Return a pointer to where the truncated message starts.
//Note: May change the message by replacing a character with '<'.
CS
msg_may_trunc(CS s) {
   int      n;
   int      room;

   // If @commheight' is zero or something unexpected happened "room" may be negative.
   room = (int)(visibleRowsG - commlineRowG - 1) * visibleColsG + sc_col - 1;
   if (room > 0 && (n = (int)STRLEN(s) - room) > 0){
      int   size = eeglStrSize(s);

      // There may be room anyway when there are multibyte chars.
      if (size <= room)
         return s;

      for (n = 0; size >= room; ) {
         size -= mb_ptr2cells(s + n);
         n += utfCharLen(s + n);
      }
      --n;
      s += n;
      *s = '<';
   }
   return s;
}


//}}}
//{{{error messages

//Get the message about the source, as used for an error message.
//Return an allocated string with room for one more character. NULL when no message is to be given.
private CS
get_emsg_source(void) {
   Byte   *p;

   if (HAVE_SOURCING_INFO && SOURCING_NAME && other_sourcing_name()) {
      Byte* sname = estack_sfile(ESTACK_NONE);
      Byte* tofree = sname;
      if (!sname)
         sname = SOURCING_NAME;

      if (estack_compiling)
         p = (CS)_("Error detected while compiling %s:");
      else
         p = (CS)_("Error detected while processing %s:");
      Arr(Byte) builder = alloc(STRLEN(sname) + STRLEN(p));
      SPRINTF(builder, p, sname);
      eeglFree(tofree);
      return builder;
   }
    return NULL;
}

// Get the message about the source lnum, as used for an error message.
// Returns an allocated string with room for one more character.
// Returns NULL when no message is to be given.
private CS
get_emsg_lnum(void) {
   Byte   *builder, *p;

   // lnum is 0 when executing a command from the command line argument, we don't want a line 
   // number then
   if (SOURCING_NAME
       && (other_sourcing_name() || SOURCING_LNUM != last_sourcing_lnum)
       && SOURCING_LNUM != 0
   ) {
      p = (CS)_("line %4ld:");
      builder = alloc(STRLEN(p) + 20);
      sprintf((char *)builder, (char *)p, (long)SOURCING_LNUM);
      return builder;
   }
   return NULL;
}

//Display name and line number for the source of an error.
//Remember the file name and line number, so that for the next error the info
//is only displayed if it changed.
void
msg_source(char flags) {
   static Boole recursive = false;

   // Bail out if something called here causes an error.
   if (recursive)
      return;
   recursive = true;

   ++no_wait_return;
   Byte *p = get_emsg_source();
   if (p) {
      msg_scroll = true;  // this will take more than one line
      msgDeco(p, flags);
      eeglFree(p);
   }
   p = get_emsg_lnum();
   if (p) {
      msgDeco(p, getDecoFlags(HLF_N));
      eeglFree(p);
      last_sourcing_lnum = SOURCING_LNUM;  // only once for each line
   }

   // remember the last sourcing name printed, also when it's empty
   if (SOURCING_NAME == NULL || other_sourcing_name()) {
      EE_CLEAR(last_sourcing_name);
      if (SOURCING_NAME) {
         last_sourcing_name = copyStr(SOURCING_NAME);
      } 
   }
   --no_wait_return;

   recursive = false;
}

//Return true if not giving error messages right now:
//If "emsg_off" is set: no error messages at the moment.
//If "msg" is in 'debug': do error message but without side effects.
//If "emsg_skip" is set: never do error messages.
private int
emsg_not_now(void) {
   if ((emsg_off > 0 
            && (!p_debug 
               || (firstOccurrence(p_debug, 'm') == NULL && firstOccurrence(p_debug, 't') == NULL))
       ) || emsg_skip > 0
   )
      return true;
   return false;
}

private ArrayList ignore_error_list = GA_EMPTY;

void
ignore_error_for_testing(Byte *error) {
   if (ignore_error_list.ga_itemsize == 0)
      ga_init2(&ignore_error_list, sizeof(CS), 1);

   if (STRCMP("RESET", error) == 0)
      ga_clear_strings(&ignore_error_list);
   else
      ga_copy_string(&ignore_error_list, error);
}

private int
ignore_error(Arr(Byte const) msg) {
   for (int i = 0; i < ignore_error_list.len; ++i) {
      if (STRSTR(msg, ((Byte **)(ignore_error_list.c))[i]))
         return true;
   } 
   return false;
}

//Display an error message
//Call message() to do the real work. When terminal not initialized (yet), use mch_errmsg(..).
//Return true if wait_return() not called.
//Note: caller must check 'emsg_not_now()' before calling this.
private int
emsgImpl(CS s) {
   char      flags;
   CS p;
   int      r;
   int      ignore = false;
   int      severe;

   // When testing some errors are turned into a normal message.
   if (ignore_error(s))
      // don't call msg() if it results in a dialog
      return msg_use_printf() ? false : msg(s);
   
   ++called_emsg;

   // If "emsg_severe" is true: When an error exception is to be thrown,
   // prefer this message over previous messages for the same command.
   severe = emsg_severe;
   emsg_severe = false;

   if (!emsg_off || firstOccurrence(p_debug, 't') != NULL) {
      //Cause a throw of an error exception if appropriate.  Don't display
      //the error message in this case.  (If no matching catch clause will
      //be found, the message will be displayed later on.)  "ignore" is set
      //when the message should be ignored completely (used for the interrupt message).
      if (cause_errthrow((CS)s, severe, &ignore) == true) {
         if (!ignore)
            ++anyEmsgG;
         return true;
      }

      if (in_assert_fails && emsg_assert_fails_msg == NULL) {
         emsg_assert_fails_msg = copyStr((CS)s);
         emsg_assert_fails_lnum = SOURCING_LNUM;
         eeglFree(emsg_assert_fails_context);
         emsg_assert_fails_context = copyStr(SOURCING_NAME == NULL ? E : SOURCING_NAME);
      }

      // set "v:errmsg", also when using ":silent! cmd"
      set_EeglVar_string(VV_ERRMSG, (CS)s, -1);

      // When using ":silent! cmd", ignore error messages. But do write it to the redirection file
      if (emsg_silent != 0) {
         if (emsg_noredir == 0) {
            msg_start();
            p = get_emsg_source();
            if (p) {
               STRCAT(p, "\n");
               redir_write(p, -1);
               eeglFree(p);
            }
            p = get_emsg_lnum();
            if (p) {
               STRCAT(p, "\n");
               redir_write(p, -1);
               eeglFree(p);
            }
            redir_write((CS)s, -1);
         }
         lo("ERROR silent: %s", s);
         return true;
      }

      ex_exitval = 1;

      // Reset msg_silent, an error causes messages to be switched back on.
      msg_silent = 0;
      cmd_silent = false;

      if (global_busy)      // break :global command
         ++global_busy;

      flush_buffers(FLUSH_MINIMAL);  // flush internal buffers
      ++anyEmsgG;            // flag for DoOneCmd()
      ++uncaught_emsg;
   }

   if (!inEchoPortalG)
      emsg_on_display = true;       // remember there is an error message

   flags = getDecoFlags(HLF_E);       // set highlight mode for error messages
   if (msg_scrolled != 0)
      need_wait_return = true;    // needed in case emsg() is called after
                // wait_return() has reset need_wait_return and a redraw is expected because
                // msg_scrolled is non-zero

   emsg_to_channel_log = true;
   // Display name and line number for the source of the error.
   msg_scroll = true;
   msg_source(flags);

   // Display the error message itself.
   msg_nowait = false;         // wait for this msg
   r = msgDeco(s, flags);

   emsg_to_channel_log = false;
   return r;
}

// Print error message "s".  Should already be translated. Return true if wait_return() not called
int
emsg(CS s) {
   // Skip this if not giving error messages at the moment.
   if (emsg_not_now())
      return true;

   return emsgImpl(s);
}


#ifndef PROTO  // manual proto with __attribute__

//Print error message "s" with format string and variable arguments.
//"s" should already be translated.
//Note: caller must not use "IObuff" for "s"!
//Return true if wait_return() not called.
int
showErrFmtMsg0(char const* s, ...) {
   // Skip this if not giving error messages at the moment.
   if (emsg_not_now())
      return true;

   if (!IObuff)
      // Very early in initialisation and already something wrong, just
      // give the raw message so the user at least gets a hint.
      return emsgImpl((CS)s);

   va_list ap;

   va_start(ap, s);
   eeVsnprintf(IObuff, IOSIZE, s, ap);
   va_end(ap);
   return emsgImpl(IObuff);
}
#endif

//Same as emsg(...), but abort on error when ABORT_ON_INTERNAL_ERROR is defined. It is used for 
//internal errors only, so that they can be detected when fuzzing Eegl.
void
internalErrMsg(CS s) {
   if (emsg_not_now())
      return;

   // Give a generic error which is translated.  The error itself may not be
   // translated, it almost never shows.
   emsgImpl(_(e_internal_error_please_report_a_bug));

   emsgImpl(s);
#if defined(ABORT_ON_INTERNAL_ERROR)
   set_EeglVar_string(VV_ERRMSG, (CS)s, -1);
   msg_putchar('\n');  // avoid overwriting the error message
   out_flush();
   abort();
#endif
}

#ifndef PROTO  // manual proto with __attribute__
// Same as showErrFmtMsg(...) but abort on error when ABORT_ON_INTERNAL_ERROR is
// defined. It is used for internal errors only, so that they can be
// detected when fuzzing Eegl.
// Note: caller must not pass 'IObuff' as 1st argument.
void
internalErrFmtMsg0(const char *s, ...) {
   if (emsg_not_now())
      return;

   // Give a generic error which is translated.  The error itself may not be
   // translated, it almost never shows.
   emsgImpl(_(e_internal_error_please_report_a_bug));

   if (IObuff == NULL) {
      // Very early in initialisation and already something wrong, just
      // give the raw message so the user at least gets a hint.
      emsgImpl((CS)s);
   } else {
      va_list ap;

      va_start(ap, s);
      eeVsnprintf(IObuff, IOSIZE, s, ap);
      va_end(ap);
      emsgImpl(IObuff);
   }
# ifdef ABORT_ON_INTERNAL_ERROR
    msg_putchar('\n');  // avoid overwriting the error message
    out_flush();
    abort();
# endif
}
#endif

// Give an "Internal error" message.
void
internal_error(CS where) {
   //Give a generic error which is translated. The error itself may not be
   //translated, it almost never shows.
   emsgImpl(_(e_internal_error_please_report_a_bug));
   internalErrFmtMsg(_(e_internal_error_str), where);
}

//Like internal_error() but do not call abort(), to avoid tests using
//test_unknown() and test_void() causing Eegl to exit.
void
internal_error_no_abort(CS where) {
   //Give a generic error which is translated. The error itself may not be
   //translated, it almost never shows.
   emsgImpl(_(e_internal_error_please_report_a_bug));
   showErrFmtMsg(_(e_internal_error_str), where);
}

// emsg3() and emsgn() are in misc2.c to avoid warnings for the prototypes.

void
emsg_invreg(int name) {
    showErrFmtMsg(_(e_invalid_register_name_str), transchar_buf(name));
}

// Give an error message which contains %s for "name[len]".
void
emsg_namelen(CS msg, CS name, int len) {
    CS copy = copySubstr(name, len);
    showErrFmtMsg(msg, copy);
    eeglFree(copy);
}

//}}}
//{{{Message history

private void
addMsgHistory(
   CS s,
   int      len,      // -1 for undetermined length
   char      flags
){
   MsgHist *p;

   if (msg_hist_off || msg_silent != 0)
      return;

   // allocate an entry and add the message at the end of the history
   p = ALLOC_ONE(MsgHist);
   if (!p)
      return;

   if (len < 0)
      len = (int)STRLEN(s);
   // remove leading and trailing newlines
   while (len > 0 && *s == '\n') {
      ++s;
      --len;
   }
   while (len > 0 && s[len - 1] == '\n')
      --len;
   p->c = copySubstr(s, len);
   p->next = NULL;
   p->deco = flags;
   if (last_msg_hist)
      last_msg_hist->next = p;
   last_msg_hist = p;
   if (first_msg_hist == NULL)
      first_msg_hist = last_msg_hist;
   ++msg_hist_len;

   check_msg_hist();
}

// Delete the first (oldest) message from the history. Return FAIL if there are no messages.
int
delete_first_msg(void) {
   MsgHist *p;

   if (msg_hist_len <= 0)
      return FAIL;
   p = first_msg_hist;
   first_msg_hist = p->next;
   if (first_msg_hist == NULL)
      last_msg_hist = NULL;  // history is empty
   eeglFree(p->c);
   eeglFree(p);
   --msg_hist_len;
   return OK;
}

private void
check_msg_hist(void) {
   // Don't let the message history get too big
   while (msg_hist_len > 0 && msg_hist_len > msg_hist_max)
      (void)delete_first_msg();
}


int
messagesopt_changed(CS new) {
   if (!new)
      return OK;

   int messages_flags_new = 0;
   int messages_wait_new = 0;
   int messages_history_new = 0;

   CS p = new;
   while (*p != ZERO) {
      if (STRNCMP(p, MESSAGES_OPT_HIT_ENTER,
           STRLEN_LITERAL(MESSAGES_OPT_HIT_ENTER)) == 0)
      {
          p += STRLEN_LITERAL(MESSAGES_OPT_HIT_ENTER);
          messages_flags_new |= MESSAGES_HIT_ENTER;
      } ei (STRNCMP(p, MESSAGES_OPT_WAIT,
           STRLEN_LITERAL(MESSAGES_OPT_WAIT)) == 0
          && EE_ISDIGIT(p[STRLEN_LITERAL(MESSAGES_OPT_WAIT)])
      ){
          p += STRLEN_LITERAL(MESSAGES_OPT_WAIT);
          messages_wait_new = parseLong(&p);
          messages_flags_new |= MESSAGES_WAIT;
      } ei (STRNCMP(p, MESSAGES_OPT_HISTORY,
           STRLEN_LITERAL(MESSAGES_OPT_HISTORY)) == 0
          && EE_ISDIGIT(p[STRLEN_LITERAL(MESSAGES_OPT_HISTORY)])
      ){
          p += STRLEN_LITERAL(MESSAGES_OPT_HISTORY);
          messages_history_new = parseLong(&p);
          messages_flags_new |= MESSAGES_HISTORY;
      }

      if (*p != ',' && *p != ZERO)
         return FAIL;
      if (*p == ',')
         ++p;
   }

   // Either "wait" or "hit-enter" is required
   if (!(messages_flags_new & (MESSAGES_HIT_ENTER | MESSAGES_WAIT)))
      return FAIL;

   // "history" must be set
   if ((messages_flags_new & MESSAGES_HISTORY) == 0)
      return FAIL;

   if (messages_history_new < 0 || messages_history_new > 10000)
      return FAIL;

   if (messages_wait_new < 0 || messages_wait_new > 10000)
      return FAIL;

   msg_flags = messages_flags_new;
   msg_wait = messages_wait_new;

   msg_hist_max = messages_history_new;
   check_msg_hist();

   return OK;
}

// ":messages" command.
void
c_messages(Invocation *invo) {
   if (STRCMP(invo->arg, "clear") == 0) {
      int keep = invo->addr_count == 0 ? 0 : invo->line2;

      while (msg_hist_len > keep)
         (void)delete_first_msg();
      return;
   }

   if (*invo->arg != ZERO) {
      emsg(_(e_invalid_argument));
      return;
   }

   msg_hist_off = true;

   MsgHist* p = first_msg_hist;
   int c = 0;
   if (invo->addr_count != 0) {
      // Count total messages
      for (; p && !gotInterruptG; p = p->next)
          c++;

      c -= invo->line2;

      // Skip without number of messages specified
      for (p = first_msg_hist; p && !gotInterruptG && c > 0; p = p->next, c--)
         {} 
   }

   if (p == first_msg_hist) {
      Byte* s = get_mess_lang();
      if (s && *s != ZERO)
          // The next comment is extracted by xgettext and put in po file for translators to read.
          msgDeco(
             // Translator: Please replace the name and email address
             // with the appropriate text for your translation.
             _("Messages maintainer: The Eegl Project"),
             getDecoFlags(HLF_T));
   }

   // Display what was not skipped.
   for (; p && !gotInterruptG; p = p->next) {
      if (p->c)
          msgDeco(p->c, p->deco);
   } 

   msg_hist_off = false;
}

//}}}
//{{{user prompts & dialogs

//To be able to scroll back at the "more" and "hit-enter" prompts we need to
//store the displayed text and remember where screen lines start.
struct MsgChunk {
   MsgChunk   *sb_next;
   MsgChunk   *sb_prev;
   char   sb_eol;      // true when line ends after this text
   int      sb_msgColG;   // column in which text starts
   int      sb_attr;   // text attributes
   Byte   sb_text[1];   // text to be displayed, actually longer
};


// Call this after prompting the user.  This will avoid a hit-return message and a delay.
private void
msg_end_prompt(void) {
   need_wait_return = false;
   emsg_on_display = false;
   commlineRowG = msgRowG;
   msgColG = 0;
   msg_clr_eos();
   lines_left = -1;
}

// Wait for the user to hit a key (normally Enter).
// If "redraw" is true, clear and redraw the screen.
// If "redraw" is false, just redraw the screen.
// If "redraw" is -1, don't redraw at all.
void
wait_return(Boole redraw) {
   Unt      c;
   int      oldState;
   int      tmpState;
   int      had_gotInterruptG;
   int      save_reg_recording;
   FILE   *save_scriptout;

   if (redraw)
      set_must_redraw(UPD_CLEAR);

   // If using ":silent cmd", don't wait for a return.  Also don't set
   // need_wait_return to do it later.
   if (msg_silent != 0)
      return;
   if (inEchoPortalG)
      return;

   //When inside vgetc(), we can't wait for a typed character at all.
   //With the global command (and some others) we only need one return at
   //the end. Adjust commlineRowG to avoid the next message overwriting the last one.
   if (vgetcBusyG > 0)
      return;
   need_wait_return = true;
   if (no_wait_return) {
      commlineRowG = msgRowG;
      return;
   }

   redir_off = true;      // don't redirect this message
   oldState = stateG;
   if (quitMoreG) {
      c = ENTER;      // just pretend CR was hit
      quitMoreG = false;
      gotInterruptG = false;
   } else {
      // Make sure the hit-return prompt is on screen when 'guioptions' was just changed.
      screenalloc(false);

      stateG = MODE_HITRETURN;
      setmouse();
      commlineRowG = msgRowG;

      // Avoid the sequence that the user types ":" at the hit-return prompt
      // to start a command, but the file-changed dialog gets in the way.
      if (need_check_timestamps)
          check_timestamps(false);

      if (msg_flags & MESSAGES_HIT_ENTER) {
         hit_return_msg();

         do {
            // Remember "gotInterruptG", if it is set vgetc() probably returns a
            // CTRL-C, but we need to loop then.
            had_gotInterruptG = gotInterruptG;

            // Don't do mappings here, we put the character back in the typeahead buffer.
            ++no_mapping;
            ++allow_keys;

            // Temporarily disable Recording. If Recording is active, the
            // character will be recorded later, since it will be added to
            // the typeBufG after the loop
            save_reg_recording = reg_recording;
            save_scriptout = scriptout;
            reg_recording = 0;
            scriptout = NULL;
            c = safe_vgetc();
            if (had_gotInterruptG && !global_busy)
               gotInterruptG = false;
            --no_mapping;
            --allow_keys;
            reg_recording = save_reg_recording;
            scriptout = save_scriptout;

            // Strange way to allow copying (yanking) a modeless selection at the hit-enter prompt.
            // Use CTRL-Y, because the same is used in Commline-mode and it's harmless when there 
            // is no selection.
            if (c == Ctrl_Y && clipboard.state == SELECT_DONE) {
               clip_copy_modeless_selection();
               c = K_IGNORE;
            }

            //Allow scrolling back in the messages.
            //Also accept scroll-down commands when messages fill the screen, to avoid that 
            //typing one 'j' too many makes the messages disappear.
            if (p_more) {
               if (c == 'b' || c == 'k' || c == 'u' || c == 'g' || c == K_UP || c == K_PAGEUP) {
                  if (msg_scrolled > visibleRowsG)
                     // scroll back to show older messages
                     do_more_prompt(c);
                  else {
                     msg_didout = false;
                     c = K_IGNORE;
                     msgColG = 0;
                  }
                  if (quitMoreG) {
                     c = ENTER;      // just pretend CR was hit
                     quitMoreG = false;
                     gotInterruptG = false;
                  } ei (c != K_IGNORE) {
                     c = K_IGNORE;
                     hit_return_msg();
                  }
               } ei (msg_scrolled > visibleRowsG - 2
                   && (c == 'j' || c == 'd' || c == 'f'
                         || c == K_DOWN || c == K_PAGEDOWN))
               c = K_IGNORE;
            }
         } while ((had_gotInterruptG && c == Ctrl_C)
               || c == K_IGNORE
               || c == K_LEFTDRAG   || c == K_LEFTRELEASE
               || c == K_MIDDLEDRAG || c == K_MIDDLERELEASE
               || c == K_RIGHTDRAG  || c == K_RIGHTRELEASE
               || c == K_MOUSELEFT  || c == K_MOUSERIGHT
               || c == K_MOUSEDOWN  || c == K_MOUSEUP || c == K_MOUSEMOVE
         );
         ui_breakcheck();

         // Avoid that the mouse-up event causes Visual mode to start.
         if (c == K_LEFTMOUSE || c == K_MIDDLEMOUSE || c == K_RIGHTMOUSE 
               || c == K_X1MOUSE || c == K_X2MOUSE
         )
            (void)jump_to_mouse(MOUSE_SETPOS, NULL, 0);
         ei (firstOccurrence((CS)"\r\n ", c) == NULL && c != Ctrl_C) {
            // Put the character back in the typeahead buffer.  Don't use
            // the stuff buffer, because lmaps wouldn't work.
            ins_char_typebuf(vgetcOrigCharG, vgetcModMaskG);
            do_redraw = true;   // need a redraw even though there is typeahead
         }
      } else {
          c = ENTER;
          // Wait to allow the user to verify the output.
          do_sleep(msg_wait, true);
      }
   }
   redir_off = false;

   // If the user hits ':', '?' or '/' we get a command line from the next line.
   if (c == ':' || c == '?' || c == '/') {
      commlineRowG = msgRowG;
      skip_redraw = true;       // skip redraw once
      do_redraw = false;
      skip_term_loop = true;
   }

   // If the window size changed set_shellsize() will redraw the screen.
   // Otherwise the screen is only redrawn if 'redraw' is set and no ':' typed.
   tmpState = stateG;
   stateG = oldState;          // restore stateG before set_shellsize
   setmouse();
   msg_check();

   // When switching screens, we need to output an extra newline on exit.
   if (termIsScreenBeingSwapped() && !termcap_active)
      newlineOnExitG = true;

   need_wait_return = false;
   did_wait_return = true;
   emsg_on_display = false;   // can delete error message now
   lines_left = -1;      // reset lines_left at next msg_start()
   reset_last_sourcing();
   if (msgAfterRedrawG
         && eeglStrSize(msgAfterRedrawG) >= (visibleRowsG - commlineRowG - 1) * visibleColsG + sc_col)
      EE_CLEAR(msgAfterRedrawG);       // don't redisplay message, it's too long

   if (tmpState == MODE_SETWSIZE) { // got resize event while in vgetc()
      starttermcap();          // start termcap before redrawing
      shell_resized();
   } ei (!skip_redraw && (redraw || msg_scrolled != 0)) {
      starttermcap();          // start termcap before redrawing
      redraw_later(UPD_VALID);
   }
}

// Write the hit-return prompt.
private void
hit_return_msg(void) {
   Boole save_p_more = p_more;

   p_more = false;   // don't want to see this message when scrolling back
   if (msg_didout)   // start on a new line
      msg_putchar('\n');
   if (gotInterruptG)
      msg_puts(_("Interrupt: "));

   msgPutsDeco(_("Press ENTER or type command to continue"), getDecoFlags(HLF_R));
   if (!msg_use_printf())
      msg_clr_eos();
   p_more = save_p_more;
}

// Set "msgAfterRedrawG" to "s".  Free the old value and check for NULL pointer.
void
set_keep_msg(Byte *s, char flags) {
   eeglFree(msgAfterRedrawG);
   if (s && msg_silent == 0)
      msgAfterRedrawG = copyStr(s);
   else
      msgAfterRedrawG = NULL;
   keep_msg_more = false;
   decoAfterRedrawG = flags;
}

void
msgmore(long n) {
   if (global_busy || !messaging()) // no messages now, wait until global is finished
                    // 'lazyredraw' set, don't do messages now
      return;

   // We don't want to overwrite another important message, but do overwrite
   // a previous "more lines" or "fewer lines" message, so that "5dd" and
   // then "put" reports the last action.
   if (msgAfterRedrawG && !keep_msg_more)
      return;

   long pn = n > 0 ? n : -n;

   if (n > 0)
       eeSnprintf(msg_buf, MSG_BUF_LEN,
          NGETTEXT("%ld more line", "%ld more lines", pn), pn);
   else
       eeSnprintf(msg_buf, MSG_BUF_LEN,
          NGETTEXT("%ld line less", "%ld fewer lines", pn), pn);
   if (gotInterruptG)
       concatenateStrings((CS)msg_buf, (CS)_(" (Interrupted)"), MSG_BUF_LEN);
   if (msg(msg_buf)) {
       set_keep_msg((CS)msg_buf, 0);
       keep_msg_more = true;
   }
}

// If there currently is a message being displayed, set "msgAfterRedrawG" to it, so
// that it will be displayed again after redraw.
void
set_keep_msg_from_hist(void) {
   if (!msgAfterRedrawG && last_msg_hist && msg_scrolled == 0 && (stateG & MODE_NORMAL))
      set_keep_msg(last_msg_hist->c, last_msg_hist->deco);
}

// Used for "confirm()" function, and the :confirm command prefix.
// Versions which haven't got flexible dialogs yet, and console
// versions, get this generic handler which uses the command line.
//
// type  = one of: EE_QUESTION, EE_INFO, EE_WARNING, EE_ERROR or EE_GENERIC
// title = title string (can be NULL for default)
// (neither used in console dialogs at the moment)
//
// Format of the "buttons" string:
// "Button1Name\nButton2Name\nButton3Name"
// The first button should normally be the default/accept
// The second button should be the 'Cancel' button
// Other buttons- use your imagination!
// A '&' in a button name becomes a shortcut, so each '&' should be before a
// different letter.
//
// Return 0 if cancelled, otherwise the nth button (1-indexed).
int
do_dialog(
   int      type UNUSED,
   Byte   *title UNUSED,
   Byte   *message,
   Byte   *buttons,
   int      dfltbutton,
   Byte   *textfield UNUSED,   // IObuff for inputdialog(), NULL otherwise
   int      ex_cmd)       // when true pressing : accepts default and starts a Command
{
   int      oldState;
   int      retval = 0;
   Byte   *hotkeys;
   int      i;
   TermInputMode   save_tmode;

   // Don't output anything in silent mode ("ex -s")
   if (silentModeG)
      return dfltbutton;   // return default option


   oldState = stateG;
   stateG = MODE_CONFIRM;
   setmouse();

   // Ensure raw mode here.
   save_tmode = cur_tmode;
   termSetMode(TMODE_RAW);

   // Since we wait for a keypress, don't make the user press RETURN as well afterwards
   ++no_wait_return;
   hotkeys = msg_show_console_dialog(message, buttons, dfltbutton);

   Unt c;
   if (hotkeys) {
      for (;;) {
         // Get a typed character directly from the user.
         c = get_keystroke();
         switch (c) {
         case ENTER:      // User accepts default option
         case NL:
            retval = dfltbutton;
            break;
         case Ctrl_C:   // User aborts/cancels
         case ESC:
            retval = 0;
            break;
         default:      // Could be a hotkey?
            if (c >= UNT_NEG)   // special keys are ignored here
               continue;
            if (c == ':' && ex_cmd) {
               retval = dfltbutton;
               ins_char_typebuf(':', 0);
               break;
            }

            // Make the character lowercase, as chars in "hotkeys" are.
            c = MB_TOLOWER(c);
            retval = 1;
            for (i = 0; hotkeys[i]; ++i) {
               if (mb_ptr2char(hotkeys + i) == c)
                  break;
               i += utfCharLen(hotkeys + i) - 1;
               ++retval;
            }
            if (hotkeys[i])
               break;
            // No hotkey match, so keep waiting
            continue;
         }
         break;
      }

      eeglFree(hotkeys);
   }

   termSetMode(save_tmode);
   stateG = oldState;
   setmouse();
   --no_wait_return;
   msg_end_prompt();

   return retval;
}

// Display the ":confirm" message.  Also called when screen resized.
private void
display_confirm_msg(void) {
   // avoid that 'q' at the more prompt truncates the message here
   ++confirm_msg_used;
   if (confirm_msg)
      msgPutsDeco(confirm_msg, getDecoFlags(HLF_M));
   --confirm_msg_used;
}

int
eeDialog_yesno(
    int      type,
    Byte   *title,
    Byte   *message,
    int      dflt)
{
   if (do_dialog(type,
      title == NULL ? (CS)_("Question") : title,
      message,
      (CS)_("&Yes\n&No"), dflt, NULL, false) == 1) {
      return EE_YES;
   } 
   return EE_NO;
}

int
eeDialog_yesnocancel(
    int      type,
    Byte   *title,
    Byte   *message,
    int      dflt)
{
   switch (do_dialog(
         type,
         title == NULL ? (CS)_("Question") : title,
         message,
         (CS)_("&Yes\n&No\n&Cancel"), dflt, NULL, false
   )){
   case 1: return EE_YES;
   case 2: return EE_NO;
   }
   return EE_CANCEL;
}

int
eeDialog_yesnoallcancel(
   int      type,
   Byte   *title,
   Byte   *message,
   int      dflt
){
   switch (do_dialog(type,
      title == NULL ? (CS)"Question" : title,
      message,
      (CS)_("&Yes\n&No\nSave &All\n&Discard All\n&Cancel"), dflt, NULL, false)
   ) {
   case 1: return EE_YES;
   case 2: return EE_NO;
   case 3: return EE_ALL;
   case 4: return EE_DISCARDALL;
   }
   return EE_CANCEL;
}

//Show the more-prompt and handle the user response. This takes care of scrolling back and 
//displaying previously displayed text. When at hit-enter prompt "typedChar" is the already 
//typed character, otherwise it's ZERO. true when jumping ahead to "confirm_msg_tail".
private int
do_more_prompt(int typedChar) {
   static int entered = false;
   int      usedTypedChar = typedChar;
   int      oldState = stateG;
   int      c;
   int      retval = false;
   int      toscroll;
   MsgChunk   *mp;
   int      i;

   char msgFlags = getDecoFlags(HLF_MSG);

   // We get called recursively when a timer callback outputs a message. In that case don't show
   // another prompt. Also when at the hit-Enter prompt and nothing was typed.
   if (entered || (stateG == MODE_HITRETURN && typedChar == 0))
      return false;
   entered = true;

   MsgChunk* lastChunk = NULL;
   if (typedChar == 'G') {
      // "g<": Find first line on the last page.
      lastChunk = moveToStartOfScreenLine(lastChunkS);
      for (i = 0; i < visibleRowsG - 2 && lastChunk && lastChunk->sb_prev; ++i)
         lastChunk = moveToStartOfScreenLine(lastChunk->sb_prev);
   }

   stateG = MODE_ASKMORE;
   setmouse();
   if (typedChar == ZERO)
      msg_moremsg(false);
   for (;;) {
      // Get a typed character directly from the user.
      if (usedTypedChar != ZERO) {
         c = usedTypedChar;   // was typed at hit-enter prompt
         usedTypedChar = ZERO;
      } else
         c = get_keystroke();

      toscroll = 0;
      switch (c) {
      case BS:      // scroll one line back
      case K_BS:
      case 'k':
      case K_UP:
          toscroll = -1;
          break;

      case ENTER:      // one extra line
      case NL:
      case 'j':
      case K_DOWN:
          toscroll = 1;
          break;

      case 'u':      // Up half a page
          toscroll = -(visibleRowsG / 2);
          break;

      case 'd':      // Down half a page
          toscroll = visibleRowsG / 2;
          break;

      case 'b':      // one page back
      case K_PAGEUP:
          toscroll = -(visibleRowsG - 1);
          break;

      case ' ':      // one extra page
      case 'f':
      case K_PAGEDOWN:
      case K_LEFTMOUSE:
          toscroll = visibleRowsG - 1;
          break;

      case 'g':      // all the way back to the start
          toscroll = -999999;
          break;

      case 'G':      // all the way to the end
          toscroll = 999999;
          lines_left = 999999;
          break;

      case ':':      // start new command line
         if (!confirm_msg_used) {
            // Since gotInterruptG is set all typeahead will be flushed, but we
            // want to keep this ':', remember that in a special way.
            typeahead_noflush(':');
            skip_term_loop = true;
            commlineRowG = visibleRowsG - 1;      // put ':' on this line
            skip_redraw = true;      // skip redraw once
            need_wait_return = false;   // don't wait in main()
         }
         // FALLTHROUGH
      case 'q':      // quit
      case Ctrl_C:
      case ESC:
         if (confirm_msg_used) {
            // Jump to the choices of the dialog.
            retval = true;
         } else {
            gotInterruptG = true;
            quitMoreG = true;
         }
         // When there is some more output (wrapping line) display that
         // without another prompt.
         lines_left = visibleRowsG - 1;
         break;

      case Ctrl_Y:
         //Strange way to allow copying (yanking) a modeless selection at the more prompt.
         //Use CTRL-Y, because the same is used in Cmdline-mode and at the hit-enter prompt. 
         //However, scrolling one line up might be expected...
         if (clipboard.state == SELECT_DONE)
            clip_copy_modeless_selection();
         continue;
      default:      // no valid response
          msg_moremsg(true);
          continue;
      }

      if (toscroll == 0) {
         break;
      }
      if (toscroll < 0) {
         // go to start of last line
         if (!lastChunk)
            mp = moveToStartOfScreenLine(lastChunkS);
         ei (lastChunk->sb_prev)
            mp = moveToStartOfScreenLine(lastChunk->sb_prev);
         else
            mp = NULL;

         // go to start of line at top of the screen
         for (i = 0; i < visibleRowsG - 2 && mp && mp->sb_prev; ++i)
            mp = moveToStartOfScreenLine(mp->sb_prev);

         if (mp && mp->sb_prev) {
            // Find line to be displayed at top.
            for (i = 0; i > toscroll; --i) {
               if (!mp || !mp->sb_prev)
                  break;
               mp = moveToStartOfScreenLine(mp->sb_prev);
               if (!lastChunk)
                  lastChunk = moveToStartOfScreenLine(lastChunkS);
               else
                  lastChunk = moveToStartOfScreenLine(lastChunk->sb_prev);
            }

            if (toscroll == -1 && screen_ins_lines(0, 0, 1, (int)visibleRowsG, 0, NULL) == OK) {
               //display line at top
               (void)disp_sb_line(0, mp, false);
            } else {
               int did_clear = screenclear();

               //redisplay all lines
               for (i = 0; mp && i < visibleRowsG - 1; ++i) {
                  mp = disp_sb_line(i, mp, !did_clear);
                  ++msg_scrolled;
               }
            }
            toscroll = 0;
         }
      } else {
         // First display any text that we scrolled back.
         while (toscroll > 0 && lastChunk) {
            // scroll up, display line at bottom
            drawMsgScrollUp();
            inc_msg_scrolled();
            fillRowsWithTwoChars(
               (int)visibleRowsG - 2, (int)visibleRowsG - 1, 0, (int)visibleColsG, ' ', ' ', 
               msgFlags
            );
            lastChunk = disp_sb_line((int)visibleRowsG - 2, lastChunk, false);
            --toscroll;
         }
      }

      if (toscroll <= 0) {
         // displayed the requested text, more prompt again
         fillRowsWithTwoChars(
            (int)visibleRowsG - 1, (int)visibleRowsG, 0, (int)visibleColsG, ' ', ' ', msgFlags
         );
         msg_moremsg(false);
         continue;
      }

      // display more text, return to caller
      lines_left = toscroll;
      break;
   }

   // clear the --more-- message
   fillRowsWithTwoChars(
      (int)visibleRowsG - 1, (int)visibleRowsG, 0, (int)visibleColsG, ' ', ' ', msgFlags
   );
   stateG = oldState;
   setmouse();
   if (quitMoreG) {
      msgRowG = visibleRowsG - 1;
      msgColG = 0;
   }

   entered = false;
   return retval;
}

//}}}
//{{{putting messages on screen

// Magic chars used in confirm dialog strings
#define DLG_BUTTON_SEP   '\n'
#define DLG_HOTKEY_CHAR   '&'


// Output a string to the screen at position msgRowG, msgColG.
// Update msgRowG and msgColG for the next message.
void
msg_puts(CS s) {
   msgPutsDeco(s, 0);
}

void
msg_puts_title(CS s) {
   msgPutsDeco(s, getDecoFlags(HLF_T));
}

//Show a message in such a way that it always fits in the line. Cut out a part in the middle and 
//replace it with "..." when necessary. Does not handle multi-byte characters!
void
outputShortenedToALine(Text slice, char flags) {
   int slen = slice.len;
   int room = visibleColsG - msgColG;
   if (room >= 20 && slice.len > (Unt)room) {
      slen = (room - 3) / 2;
      msgOuttransLenDeco(slice, flags);
      msgPutsDeco((CS)"...", getDecoFlags(HLF_8));
   }
   msgOuttransLenDeco((Text){.c = slice.c + slice.len - slen, .len = slen}, flags);
}

// Basic function for writing a message with hilite decorations.
void
msgPutsDeco(CS s, char flags) {
   printWithDecoAndMaxLen(s, -1, flags);
}

// Like msgPutsDeco(), but with a maximum length "maxlen" (in bytes).
// When "maxlen" is -1 there is no maximum length.
// When "maxlen" is >= 0 the message is not put in the history.
private void
printWithDecoAndMaxLen(Arr(Byte const) str, int maxlen, char flags) {
   // If redirection is on, also write to the redirection file.
   redir_write((CS)str, maxlen);

   // Don't print anything when using ":silent cmd".
   if (msg_silent != 0)
      return;

   if (flags == 0)
      flags = getDecoFlags(HLF_MSG);

   // if MSG_HIST flag set, add message to history
   if ((flags & MSG_HIST) && maxlen < 0) {
      addMsgHistory((CS)str, -1, flags);
      flags &= ~MSG_HIST;
   }

   // When writing something to the screen after it has scrolled, requires a wait-return prompt 
   // later. Needed when scrolling, resetting need_wait_return after some prompt, and then 
   // outputting something without scrolling Not needed when only using CR to move the cursor.
   if (msg_scrolled != 0 && !msg_scrolled_ign && STRCMP(str, "\r") != 0)
      need_wait_return = true;
   msg_didany = true;      // remember that something was outputted

   //If there is no valid screen, use fprintf so we can see error messages.
   //If termcap is not active, we may be writing in an alternate console portal, cursor 
   //positioning may not work correctly or we just don't know where the cursor is.
   if (msg_use_printf())
      toPrintf((CS)str, maxlen);
   else
      toDisplay((CS)str, maxlen, flags, false);

   needFileinfoG = false;
}

// values for "where"
#define PUT_APPEND 0      // append to "lnum"
#define PUT_TRUNC 1      // replace "lnum"
#define PUT_BELOW 2      // add below "lnum"
            //
// Put text "t_s" until "end" in the message window.
// "where" specifies where to put the text.
private void
put_messagePort(Portal *wp, int where, Byte *t_s, Byte *end, LineNr lnum) {
   Byte  *p;

   if (where == PUT_BELOW) {
      if (*end != ZERO) {
         p = copySubstr(t_s, end - t_s);
      } else
         p = t_s;
      memAppendBook(wp->book, lnum, p, (ColNr)0, false);
      if (p != t_s)
         eeglFree(p);
   } else {
      Byte *newp;

      curBook = wp->book;
      if (where == PUT_APPEND) {
          newp = concat_str(ml_get(lnum), t_s);
          if (newp == NULL)
         return;
          if (*end != ZERO)
         newp[STRLEN(ml_get(lnum)) + (end - t_s)] = ZERO;
      } else {
          newp = copySubstr(t_s, end - t_s);
      }
      ml_replace(lnum, newp, false);
      curBook = curPor->book;
   }
   redrawPortLater(wp, UPD_NOT_VALID);

   // set msgColG so that a newline is written if needed
   msgColG += (int)(end - t_s);
}

// The display part of printWithDecoAndMaxLen().
// May be called recursively to display scroll-back text.
private void
toDisplay(
   Byte   *str,
   int      maxlen,
   char      flags,
   int      recurse
){
   Byte   *s = str;
   Byte   *t_s = str;   // string from "t_s" to "s" is still todo
   int      t_col = 0;   // screen cells todo, 0 when "t_s" not used
   int      l;
   int      cw;
   Byte   *sb_str = str;
   int      sb_col = msgColG;
   int      wrap;
   int      did_last_char;
   int      where = PUT_APPEND;
   Portal   *messagePort = NULL;
   LineNr    lnum = 1;

   if (inEchoPortalG) {
      messagePort = popup_get_messagePort();

      if (messagePort) {
         if (!popup_message_win_visible()) {
            if (*str == NL) {
               // When not showing the message window and the output
               // starts with a NL show the message normally.
               messagePort = NULL;
            } else {
               // currently hidden, make it empty
               curBook = messagePort->book;
               while ((curBook->mem.flags & ML_EMPTY) == 0)
                  ml_delete(1);
               curBook = curPor->book;
            }
         } else {
            lnum = messagePort->book->mem.lineCount;
            if (msgColG == 0)
                where = PUT_TRUNC;
         }
      }
   }

   did_wait_return = false;
   while ((maxlen < 0 || (int)(s - str) < maxlen) && *s != ZERO) {
      // We are at the end of the screen line when:
      // - outputting a newline.
      // - outputting a character in the last column.
      if (!recurse && msgRowG >= visibleRowsG - 1 
            && (*s == '\n' || (
               ((*s != '\r' && msgColG + t_col >= visibleColsG - 1)
                || (*s == TAB && msgColG + t_col >= ((visibleColsG - 1) & ~7))
                || (mb_ptr2cells(s) > 1
                   && msgColG + t_col >= visibleColsG - 2))))
      ){
         //The screen is scrolled up when at the last row (some terminals
         //scroll automatically, some don't.  To avoid problems we scroll ourselves).
         if (t_col > 0) {
            // output postponed text
            if (messagePort) {
               put_messagePort(messagePort, where, t_s, s, lnum);
               t_col = 0;
               where = PUT_BELOW;
            } else
               t_puts(&t_col, t_s, s, flags);
         }

         //When no more prompt and no more room, truncate here
         if (msg_no_more && lines_left == 0)
            break;

         if (messagePort == NULL)
            //Scroll the screen up one line.
            drawMsgScrollUp();

         msgRowG = visibleRowsG - 2;
         if (msgColG >= visibleColsG)   // can happen after screen resize
            msgColG = visibleColsG - 1;

         //Display char in last column before showing more-prompt.
         if (*s >= ' ') {
            if (maxlen >= 0)
               // avoid including composing chars after the end
               l = utfCharLen_len(s, (int)((str + maxlen) - s));
            else
               l = utfCharLen(s);
            s = drawText_mbyte(s, l, flags);
         did_last_char = true;
         } else
            did_last_char = false;

         if (p_more)
            // store text for scrolling back
            saveToScrollback(&sb_str, s, flags, &sb_col, true);

         if (messagePort == NULL) {
            inc_msg_scrolled();
            need_wait_return = true; // may need wait_return() in main()
            redrawCommlineG = true;
            if (commlineRowG > 0)
               --commlineRowG;

            // If screen is completely filled and 'more' is set then wait for a character.
            if (lines_left > 0)
               --lines_left;
         }
         if (p_more && lines_left == 0 && stateG != MODE_HITRETURN && !msg_no_more) {
            if (do_more_prompt(ZERO))
               s = confirm_msg_tail;
            if (quitMoreG)
               return;
         }

         // When we displayed a char in last column need to check if there is still more.
         if (did_last_char)
            continue;
      }

      wrap = *s == '\n'
             || msgColG + t_col >= visibleColsG
             || (mb_ptr2cells(s) > 1
                      && msgColG + t_col >= visibleColsG - 1);
      if (t_col > 0 && (wrap || *s == '\r' || *s == '\b' || *s == '\t' || *s == BELL)) {
         // output any postponed text
         if (messagePort) {
            put_messagePort(messagePort, where, t_s, s, lnum);
            t_col = 0;
            where = PUT_BELOW;
         } else
            t_puts(&t_col, t_s, s, flags);
      }

      if (wrap && p_more && !recurse)
          // store text for scrolling back
          saveToScrollback(&sb_str, s, flags, &sb_col, true);

      if (*s == '\n') {        // go to next line
         if (messagePort) {
            // Ignore a NL when the buffer is empty, it is used to scroll up the text.
            if ((messagePort->book->mem.flags & ML_EMPTY) == 0) {
               put_messagePort(messagePort, PUT_BELOW, t_s, t_s, lnum);
               ++lnum;
            }
         } else
            msg_didout = false;       // remember that line is empty
         msgColG = 0;
         if (++msgRowG >= visibleRowsG)  // safety check
            msgRowG = visibleRowsG - 1;
      } ei (*s == '\r') {     // go to column 0
         msgColG = 0;
         where = PUT_TRUNC;
      } ei (*s == '\b') {      // go to previous char
         if (msgColG)
            --msgColG;
      } ei (*s == TAB) {       // translate Tab into spaces
         if (messagePort)
            msgColG = (msgColG + 7) % 8;
         else do
            msg_screen_putchar(' ', flags);
         while (msgColG & 7);
      } else    {
         cw = mb_ptr2cells(s);
         if (maxlen >= 0)
            // avoid including composing chars after the end
            l = utfCharLen_len(s, (int)((str + maxlen) - s));
         else
            l = utfCharLen(s);

         // When drawing from right to left or when a double-wide character
         // doesn't fit, draw a single character here.  Otherwise collect
         // characters and draw them all at once later.
         if ( (cw > 1 && msgColG + t_col >= visibleColsG - 1)) {
            if (l > 1)
               s = drawText_mbyte(s, l, flags) - 1;
            else
               msg_screen_putchar(*s, flags);
         } else {
            // postpone this character until later
            if (t_col == 0)
               t_s = s;
            t_col += cw;
            s += l - 1;
         }
      }
      ++s;
   }

    // output any postponed text
   if (t_col > 0) {
      if (messagePort)
         put_messagePort(messagePort, where, t_s, s, lnum);
      else
         t_puts(&t_col, t_s, s, flags);
    }

   if (messagePort)
      popup_show_messagePort();
   // Store the text for scroll back, unless it's a newline by itself.
   if (p_more && !recurse && !(s == sb_str + 1 && *sb_str == '\n'))
      saveToScrollback(&sb_str, s, flags, &sb_col, false);

   msg_check();
}

// Return true when ":filter pattern" was used and "msg" does not match "pattern".
int
message_filtered(Byte *msg) {
   if (commModifierG.cmod_filter_regmatch.regprog == NULL)
      return false;
   int  match = eeRegexec(&commModifierG.cmod_filter_regmatch, msg, (ColNr)0);
   return commModifierG.cmod_filter_force ? match : !match;
}

// Output any postponed text for printWithDecoAndMaxLen().
private void
t_puts(
   int      *t_col,
   CS t_s,
   CS s,
   char      flags
){
   // output postponed text
   msg_didout = true;      // remember that line is not empty
   drawTextLen(t_s, (int)(s - t_s), msgRowG, msgColG, flags);
   msgColG += *t_col;
   *t_col = 0;
   // If the string starts with a composing character don't increment the column position for it.
   if (utf_iscomposing(mb_ptr2char(t_s)))
      --msgColG;
   if (msgColG >= visibleColsG) {
      msgColG = 0;
      ++msgRowG;
   }
}

//true when messages should be printed with mch_errmsg().
//This is used when there is no valid screen, so we can see error messages.
//If termcap is not active, we may be writing in an alternate console
//window, cursor positioning may not work correctly (window size may be
//different) or we just don't know where the cursor is.
int
msg_use_printf(void){
   return (!msg_check_screen()
       || !termcap_active
       || (termIsScreenBeingSwapped() && !termcap_active)
   );
}

#if defined(USE_MCH_ERRMSG) || defined(PROTO)

#ifdef mch_errmsg
# undef mch_errmsg
#endif
#ifdef mch_msg
# undef mch_msg
#endif

//Give an error message. To be used when the screen hasn't been initialized yet. When stderr can't 
//be used, collect error messages until the TUI has started and they can be displayed in a message 
//box.
void
mch_errmsg(CS errMsg) {
   // Use stderr if it's a tty. When not going to start the GUI also use stderr.
   if (isatty(2)) {
      fprintf(stderr, "%s", errMsg);
      return;
   }

   // avoid a delay for a message that isn't there
   emsg_on_display = false;

   int len = (int)STRLEN(errMsg) + 1;
   if (errorsG.ga_growsize == 0) {
      errorsG.ga_growsize = 80;
      errorsG.ga_itemsize = 1;
   }
   if (ga_grow(&errorsG, len) == OK) {
      mch_memmove((CS)errorsG.c + errorsG.len, (CS)errMsg, len);
      // remove CR characters, they are displayed
      {
         Byte* p = (CS)errorsG.c + errorsG.len;
         for (;;) {
            p = firstOccurrence(p, '\r');
            if (!p)
                break;
            *p = ' ';
         }
      }
      errorsG.len += (len - 1); // don't count the ZERO at the end
   }
}

// Give a message. To be used when the screen hasn't been initialized yet. When there is no tty, 
// collect messages until the GUI has started and they can be displayed in a message box.
void
mch_msg(CS str) {
   // Use stdout if we have a tty.  This allows "eegl -h | more" and uses mch_errmsg() when started 
   // from the desktop. When not going to start the GUI also use stdout.
   // On Mac, when started from Finder, stderr is the console.
   if (isatty(2)) {
      printf("%s", str);
      return;
   }

   mch_errmsg(str);
}
#endif // USE_MCH_ERRMSG

// Put a character on the screen at the current message position and advance to the next position.
// Only for printable ASCII!
private void
msg_screen_putchar(int c, char flags) {
   msg_didout = true;      // remember that line is not empty
   screen_putchar(c, msgRowG, msgColG, flags); {
      if (++msgColG >= visibleColsG) {
         msgColG = 0;
         ++msgRowG;
      }
   }
}

private void
msg_moremsg(int full) {
   Byte   *s = (CS)_("-- More --");

   char flags = getDecoFlags(HLF_M);
   drawText(s, (int)visibleRowsG - 1, 0, flags);
   if (full) {
      drawText((CS)
         _(" SPACE/d/j: screen/page/line down, b/u/k: up, q: quit "),
            (int)visibleRowsG - 1, eeglStrSize(s), flags
      );
   } 
}

// Repeat the message for the current mode: MODE_ASKMORE, MODE_EXTERNCMD or MODE_CONFIRM.
void
repeat_message(void) {
   if (stateG == MODE_ASKMORE) {
      msg_moremsg(true);   // display --more-- message again
      msgRowG = visibleRowsG - 1;
   } ei (stateG == MODE_CONFIRM) {
      display_confirm_msg();   // display ":confirm" message again
      msgRowG = visibleRowsG - 1;
   } ei (stateG == MODE_EXTERNCMD) {
      windgoto(msgRowG, msgColG); // put cursor back
   } ei (stateG == MODE_HITRETURN || stateG == MODE_SETWSIZE) {
      if (msgRowG == visibleRowsG - 1) {
         // Avoid drawing the "hit-enter" prompt below the previous one,
         // overwrite it.  Esp. useful when regaining focus and a
         // FocusGained autocmd exists but didn't draw anything.
         msg_didout = false;
         msgColG = 0;
         msg_clr_eos();
      }
      hit_return_msg();
      msgRowG = visibleRowsG - 1;
   }
}

//msg_check_screen - check if the screen is initialized. Also check msgRowG and msgColG, if they 
//are too big it may cause a crash. While starting the GUI the terminal codes will be set for the 
//GUI, but the output goes to the terminal. Don't use the terminal codes then.
private int
msg_check_screen(void) {
   if (!fullScreenG || !screen_valid(false))
      return false;

   if (msgRowG >= visibleRowsG)
      msgRowG = visibleRowsG - 1;
   if (msgColG >= visibleColsG)
      msgColG = visibleColsG - 1;
   return true;
}

//Clear from current message position to end of screen.
//Skip this when ":silent" was used, no need to clear for redirection.
void
msg_clr_eos(void) {
   if (msg_silent == 0)
      msg_clr_eos_force();
}

//Clear from current message position to end of screen.
//Note: msgColG is not updated, so we remember the end of the message for msg_check().
void
msg_clr_eos_force(void) {
   if (inEchoPortalG)
      return;  // messages go into a popup
   if (msg_use_printf()) {
      if (fullScreenG) { // only when termcap codes are valid
         if (*termCodeS[KS_CD])
            out_str(termCodeS[KS_CD]);   // clear to end of display
         ei (*termCodeS[KS_CE])
            out_str(termCodeS[KS_CE]);   // clear to end of line
      }
   } else {
      int msgFlags = getDecoFlags(HLF_MSG);

      fillRowsWithTwoChars(msgRowG, msgRowG + 1, msgColG, (int)visibleColsG, ' ', ' ', msgFlags);
      fillRowsWithTwoChars(
            msgRowG + 1, (int)visibleRowsG, 0, (int)visibleColsG, ' ', ' ', msgFlags
      );
   }
}

// Clear the command line.
void
msgClearCommline(void) {
   msgRowG = commlineRowG;
   msgColG = 0;
   msg_clr_eos_force();
}

// end putting a message on the screen
// call wait_return() if the message does not fit in the available space
// return true if wait_return() not called.
int
msg_end(void) {
   //If the string is larger than the portal, or the ruler option is set and we run into it, we 
   //have to redraw the portal. Do not do this if we are abandoning the file or editing the 
   //command line.
   if (!isExitingG && need_wait_return && !(stateG & MODE_COMMLINE)) {
      wait_return(false);
      return false;
   }
   out_flush();
   return true;
}

//If the written message runs into the shown command or ruler, we have to wait for hit-return and 
//redraw the portal later.
void
msg_check(void) {
   if (msgRowG == visibleRowsG - 1 && msgColG >= sc_col && !inEchoPortalG) {
      need_wait_return = true;
      redrawCommlineG = true;
   }
}

//May write a string to the redirection file.
//When "maxlen" is -1 write the whole string, otherwise up to "maxlen" bytes.
private void
redir_write(Byte *str, int maxlen) {
   Byte   *s = str;
   static int   cur_col = 0;

   // Don't do anything for displaying prompts and the like.
   if (redir_off)
      return;

   // If 'verbosefile' is set prepare for writing in that file.
   if (isVerboseFileDefined() && verbose_fd == NULL)
      verbose_open();

   if (redirecting()) {
      // If the string doesn't start with CR or NL, go to msgColG
      if (*s != '\n' && *s != '\r') {
         while (cur_col < msgColG) {
            if (redir_execute)
               execute_redir_str((CS)" ", -1);
            ei (redir_reg)
               write_reg_contents(redir_reg, (CS)" ", -1, true);
            ei (redir_vname)
               var_redir_str((CS)" ", -1);
            ei (redir_fd) {
               fputs(" ", redir_fd);
            } 
            if (verbose_fd)
               fputs(" ", verbose_fd);
            ++cur_col;
         }
      }

      if (redir_execute)
          execute_redir_str(s, maxlen);
      ei (redir_reg)
          write_reg_contents(redir_reg, s, maxlen, true);
      ei (redir_vname)
          var_redir_str(s, maxlen);

      // Write and adjust the current column.
      while (*s != ZERO && (maxlen < 0 || (int)(s - str) < maxlen)) {
         if (!redir_reg && !redir_vname && !redir_execute) {
            if (redir_fd)
                putc(*s, redir_fd);
         } 
         if (verbose_fd)
            putc(*s, verbose_fd);
         if (*s == '\r' || *s == '\n')
            cur_col = 0;
         ei (*s == '\t')
            cur_col += (8 - cur_col % 8);
         else
            ++cur_col;
         ++s;
      }

      if (msg_silent != 0)   // should update msgColG
         msgColG = cur_col;
   }
}

int
redirecting(void) {
   return redir_fd || isVerboseFileDefined() || redir_reg || redir_vname || redir_execute;
}

//Before giving verbose message. Must always be called paired with verbose_leave()!
void
verbose_enter(void) {
   if (isVerboseFileDefined())
      ++msg_silent;
}

//After giving verbose message. Must always be called paired with verbose_enter()!
void
verbose_leave(void) {
   if (isVerboseFileDefined()) {
      if (--msg_silent < 0)
          msg_silent = 0;
   }
}

//Like verbose_enter() and set msg_scroll when displaying the message.
void
verbose_enter_scroll(void) {
   if (isVerboseFileDefined())
      ++msg_silent;
   else
      // always scroll up, don't overwrite
      msg_scroll = true;
}

//Like verbose_leave() and set commlineRowG when displaying the message.
void
verbose_leave_scroll(void) {
   if (isVerboseFileDefined()) {
      if (--msg_silent < 0)
          msg_silent = 0;
   } else
      commlineRowG = msgRowG;
}

// Called when 'verbosefile' is set: stop writing to the file.
void
verbose_stop(void) {
   if (verbose_fd) {
      fclose(verbose_fd);
      verbose_fd = NULL;
   }
   verbose_did_open = false;
}

// Open the file 'verbosefile'. Return FAIL or OK.
int
verbose_open(void) {
   if (verbose_fd == NULL && !verbose_did_open && isVerboseFileDefined() && p_vfile) {
      // Only give the error message once.
      verbose_did_open = true;

      verbose_fd = fopen((char *)p_vfile, "a");
      if (verbose_fd == NULL) {
         showErrFmtMsg(_(e_cant_open_file_str), p_vfile);
         return FAIL;
      }
   }
   return OK;
}

// Give a warning message (for searching). Use 'w' highlighting and may repeat the message 
// after redrawing
void
give_warning(Byte *message, int hl) {
   give_warning_with_source(message, hl, false);
}

void
give_warning_with_source(Byte *message, int hl, int with_source) {
   // Don't do this for ":silent".
   if (msg_silent != 0)
      return;

   // Don't want a hit-enter prompt here.
   ++no_wait_return;

   set_EeglVar_string(VV_WARNINGMSG, message, -1);
   EE_CLEAR(msgAfterRedrawG);
   if (hl)
      decoAfterRedrawG = getDecoFlags(HLF_W);
   else
      decoAfterRedrawG = 0;

   if (with_source) {
      // Do what msg() does, but with a column offset if the warning should
      // be after the mode message.
      msg_start();
      msg_source(getDecoFlags(HLF_W));
      msg_puts(S" ");
      msgPutsDeco(message, getDecoFlags(HLF_W) | MSG_HIST);
      msg_clr_eos();
      (void)msg_end();
   } ei (msgDeco(message, decoAfterRedrawG) && msg_scrolled == 0)
      set_keep_msg(message, decoAfterRedrawG);

   msg_didout = false;       // overwrite this message
   msg_nowait = true;       // don't wait for this message
   msgColG = 0;

   --no_wait_return;
}

void
give_warning2(Byte *message, Byte *a1, int hl) {
   if (IObuff == NULL) {
      // Very early in initialisation and already something wrong, just give
      // the raw message so the user at least gets a hint.
      give_warning(message, hl);
   } else {
      eeSnprintf(IObuff, IOSIZE, (char *)message, a1);
      give_warning(IObuff, hl);
   }
}

// Advance msg cursor to column "col".
void
msg_advance(int col) {
   if (msg_silent != 0) {  // nothing to advance to
      msgColG = col;      // for redirection, may fill it up later
      return;
   }
   if (col >= visibleColsG)      // not enough room
      col = visibleColsG - 1;
   while (msgColG < col)
      msg_putchar(' ');
}

// Warn about missing Clipboard Support
void
msg_warn_missing_clipboard(void) {
   if (!global_busy && !did_warn_clipboard) {
      msg(_("W23: Clipboard register not available, using register 0"));
      did_warn_clipboard = true;
   }
}

//Copy one character from "*from" to "*to", taking care of multi-byte characters. Return the 
//length of the character in bytes.
private int
copy_char(
   Byte   *from,
   Byte   *to,
   int      lowercase)   // make character lower case
{
   if (lowercase) {
      int c = MB_TOLOWER((*mb_ptr2char)(from));
      return (*mb_char2bytes)(c, to);
   } else {
      int len = utfCharLen(from);
      mch_memmove(to, from, (Unt)len);
      return len;
   }
}

// Format the dialog string, and display it at the bottom of
// the screen. Return a string of hotkey chars (if defined) for
// each 'button'. If a button has no hotkey defined, the first character of
// the button is used.
// The hotkeys can be multi-byte characters, but without combining chars.
//
// Return an allocated string with hotkeys, or NULL for error.
private Byte *
msg_show_console_dialog(
   Byte   *message,
   Byte   *buttons,
   int      dfltbutton
){
   int      len = 0;
#define HOTK_LEN (MB_MAXBYTES)
   int      lenhotkey = HOTK_LEN;   // count first button
   Byte   *hotk = NULL;
   Byte   *msgp = NULL;
   Byte   *hotkp = NULL;
   Byte   *r;
   int      copy;
#define HAS_HOTKEY_LEN 30
   Byte   has_hotkey[HAS_HOTKEY_LEN];
   int      first_hotkey = false;   // first char of button is hotkey
   int      idx;

   has_hotkey[0] = false;

   // First loop: compute the size of memory to allocate. Second loop: copy to the allocated memory
   for (copy = 0; copy <= 1; ++copy) {
   r = buttons;
   idx = 0;
   while (*r) {
      if (*r == DLG_BUTTON_SEP) {
         if (copy) {
            *msgp++ = ',';
            *msgp++ = ' ';       // '\n' -> ', '

            // advance to next hotkey and set default hotkey
            hotkp += STRLEN(hotkp);
            hotkp[copy_char(r + 1, hotkp, true)] = ZERO;
            if (dfltbutton)
               --dfltbutton;

            // If no hotkey is specified first char is used.
            if (idx < HAS_HOTKEY_LEN - 1 && !has_hotkey[++idx])
               first_hotkey = true;
         } else {
            len += 3;          // '\n' -> ', '; 'x' -> '(x)'
            lenhotkey += HOTK_LEN;  // each button needs a hotkey
            if (idx < HAS_HOTKEY_LEN - 1)
               has_hotkey[++idx] = false;
         }
      } ei (*r == DLG_HOTKEY_CHAR || first_hotkey) {
         if (*r == DLG_HOTKEY_CHAR)
            ++r;
         first_hotkey = false;
         if (copy) {
            if (*r == DLG_HOTKEY_CHAR)      // '&&a' -> '&a'
               *msgp++ = *r;
            else {
               // '&a' -> '[a]'
               *msgp++ = (dfltbutton == 1) ? '[' : '(';
               msgp += copy_char(r, msgp, false);
               *msgp++ = (dfltbutton == 1) ? ']' : ')';

               // redefine hotkey
               hotkp[copy_char(r, hotkp, true)] = ZERO;
            }
         } else {
            ++len;       // '&a' -> '[a]'
            if (idx < HAS_HOTKEY_LEN - 1)
               has_hotkey[idx] = true;
         }
      } else {
         // everything else copy literally
         if (copy)
            msgp += copy_char(r, msgp, false);
      }

      // advance to the next character
      MB_PTR_ADV(r);
   }

   if (copy) {
      *msgp++ = ':';
      *msgp++ = ' ';
      *msgp = ZERO;
   } else {
       len += (int)(STRLEN(message)
         + 2         // for the NL's
         + STRLEN(buttons)
         + 3);         // for the ": " and ZERO
       lenhotkey++;         // for the ZERO

       // If no hotkey is specified first char is used.
      if (!has_hotkey[0]) {
         first_hotkey = true;
         len += 2;      // "x" -> "[x]"
      }

      // Now allocate and load the strings
      eeglFree(confirm_msg);
      confirm_msg = alloc(len);
      *confirm_msg = ZERO;
      hotk = alloc(lenhotkey);

      *confirm_msg = '\n';
      STRCPY(confirm_msg + 1, message);

      msgp = confirm_msg + 1 + STRLEN(message);
      hotkp = hotk;

      //Define first default hotkey.  Keep the hotkey string ZERO terminated to avoid reading
      //past the end.
      hotkp[copy_char(buttons, hotkp, true)] = ZERO;

      //Remember where the choices start, displaying starts here when "hotkp" typed at the more 
      //prompt.
      confirm_msg_tail = msgp;
      *msgp++ = '\n';
   }
   }

   display_confirm_msg();
   return hotk;
}


//}}}
//{{{scrollin' messages

// Increment "msg_scrolled".
private void
inc_msg_scrolled(void) {
   if (*get_EeglVar_str(VV_SCROLLSTART) == ZERO) {
      Byte* p = SOURCING_NAME;
      Byte* tofree = NULL;
      int len;

      // v:scrollstart is empty, set it to the script/function name and line number
      if (!p)
         p = (CS)_("Unknown");
      else {
         len = (int)STRLEN(p) + 40;
         tofree = alloc(len);
         eeSnprintf(tofree, len, _("%s line %ld"), p, (long)SOURCING_LNUM);
         p = tofree;
      }
      set_EeglVar_string(VV_SCROLLSTART, p, -1);
      eeglFree(tofree);
   }
   ++msg_scrolled;
   set_must_redraw(UPD_VALID);
}


typedef enum {
   SB_CLEAR_NONE = 0,
   SB_CLEAR_ALL,
   SB_CLEAR_COMMLINE_BUSY,
   SB_CLEAR_CMDLINE_DONE
} ScrollBackClearing;

// When to clear text on next msg.
private ScrollBackClearing clearScrollBackS = SB_CLEAR_NONE;

// Store part of a printed message for displaying when scrolling back.
private void
saveToScrollback(
   Byte   **sb_str,   // start of string
   Byte   *s,      // just after string
   char      flags,
   int      *sb_col,
   int      finish      // line ends
){
   MsgChunk   *mp;

   if (clearScrollBackS == SB_CLEAR_ALL || clearScrollBackS == SB_CLEAR_CMDLINE_DONE) {
      clear_sb_text(clearScrollBackS == SB_CLEAR_ALL);
      msg_sb_eol();  // prevent messages from overlapping
      clearScrollBackS = SB_CLEAR_NONE;
   }

   if (s > *sb_str) {
      mp = alloc(offsetof(MsgChunk, sb_text) + (s - *sb_str) + 1);
      mp->sb_eol = finish;
      mp->sb_msgColG = *sb_col;
      mp->sb_attr = flags;
      copySubstrToAllocation(mp->sb_text, (Text){*sb_str, s - *sb_str});

      if (!lastChunkS) {
         lastChunkS = mp;
         mp->sb_prev = NULL;
      } else {
         mp->sb_prev = lastChunkS;
         lastChunkS->sb_next = mp;
         lastChunkS = mp;
      }
      mp->sb_next = NULL;
   } ei (finish && lastChunkS)
      lastChunkS->sb_eol = true;

   *sb_str = s;
   *sb_col = 0;
}

// Finished showing messages, clear the scroll-back text on the next message.
void
may_clear_sb_text(void){
   clearScrollBackS = SB_CLEAR_ALL;
}

// Starting to edit the command line: do not clear messages now.
void
sb_text_start_cmdline(void){
   if (clearScrollBackS == SB_CLEAR_COMMLINE_BUSY)
      // Invoking command line recursively: the previous-level command line
      // doesn't need to be remembered as it will be redrawn when returning
      // to that level.
      sb_text_restart_cmdline();
   else {
      msg_sb_eol();
      clearScrollBackS = SB_CLEAR_COMMLINE_BUSY;
   }
}

// Redrawing the command line: clear the last unfinished line.
void
sb_text_restart_cmdline(void) {
   MsgChunk *tofree;

   // Needed when returning from nested command line.
   clearScrollBackS = SB_CLEAR_COMMLINE_BUSY;

   if (lastChunkS == NULL || lastChunkS->sb_eol)
      // No unfinished line: don't clear anything.
      return;

   tofree = moveToStartOfScreenLine(lastChunkS);
   lastChunkS = tofree->sb_prev;
   if (lastChunkS)
      lastChunkS->sb_next = NULL;
   while (tofree) {
      MsgChunk *tofree_next = tofree->sb_next;

      eeglFree(tofree);
      tofree = tofree_next;
   }
}

// Ending to edit the command line: clear old lines but the last one later.
void
sb_text_end_cmdline(void) {
   clearScrollBackS = SB_CLEAR_CMDLINE_DONE;
}

// Clear any text remembered for scrolling back.
// When "all" is false keep the last line. Called when redrawing the screen.
void
clear_sb_text(int all) {
   MsgChunk   *mp;
   MsgChunk   **lastp;

   if (all)
      lastp = &lastChunkS;
   else {
      if (lastChunkS == NULL)
         return;
      lastp = &moveToStartOfScreenLine(lastChunkS)->sb_prev;
   }

   while (*lastp) {
      mp = (*lastp)->sb_prev;
      eeglFree(*lastp);
      *lastp = mp;
   }
}

// "g<" command.
void
show_sb_text(void) {
   MsgChunk   *mp;

   // Only show something if there is more than one line, otherwise it looks
   // weird, typing a command without output results in one line.
   mp = moveToStartOfScreenLine(lastChunkS);
   if (mp == NULL || mp->sb_prev == NULL) {
   } else {
      do_more_prompt('G');
      wait_return(false);
   }
}

// Move to the start of screen line in already displayed text.
private MsgChunk *
moveToStartOfScreenLine(MsgChunk *mps) {
   MsgChunk *mp = mps;

   while (mp && mp->sb_prev && !mp->sb_prev->sb_eol)
      mp = mp->sb_prev;
   return mp;
}

// Mark the last message chunk as finishing the line.
void
msg_sb_eol(void){
   if (lastChunkS)
      lastChunkS->sb_eol = true;
}

//Display a screen line from previously displayed text at row "row".
//When "clear_to_eol" is set clear the rest of the screen line.
//Return a pointer to the text for the next line (can be NULL).
private MsgChunk *
disp_sb_line(int row, MsgChunk *smp, int clear_to_eol) {
   MsgChunk   *mp = smp;
   Byte   *p;

   for (;;) {
      msgRowG = row;
      msgColG = mp->sb_msgColG;
      p = mp->sb_text;
      if (*p == '\n')       // don't display the line break
         ++p;
      toDisplay(p, -1, mp->sb_attr, true);

      // If clearing the screen did not work (e.g. because of a background
      // color and t_ut isn't set) clear until the last column here.
      if (clear_to_eol)
         fillRowsWithTwoChars(row, row + 1, msgColG, (int)visibleColsG, ' ', ' ', getDecoFlags(HLF_MSG));

      if (mp->sb_eol || mp->sb_next == NULL)
          break;
      mp = mp->sb_next;
   }
   return mp->sb_next;
}

//}}}
