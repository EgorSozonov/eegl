//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## strings.c: utility functions for string manipulation 
 
//#include "eegl.h"

#include <string.h>
#include "base.h"

//{{{charset (utf-8)

#include <wchar.h>       // for towupper() and towlower()

#define URL_SLASH   1      // path_is_url() has found "://"
#define URL_BACKSLASH   2      // path_is_url() has found ":\\"


// Translate any special characters in buf[bufsize] in-place.
// The result is a string with only printable characters, but if there is not
// enough room, not all characters will be translated.
void
trans_characters(CS buf, int bufsize) {
   int      len;      // length of string needing translation
   int      room;      // room in buffer after string
   CS trs;      // translated character
   int      trs_len;   // length of trs[]

   len = (int)STRLEN(buf);
   room = bufsize - len;
   while (*buf != 0) {
      // Assume a multi-byte character doesn't need translation.
      if ((trs_len = utfCharLen(buf)) > 1)
         len -= trs_len;
      else {
         trs = transchar_byte(*buf);
         trs_len = (int)STRLEN(trs);
         if (trs_len > 1) {
            room -= trs_len - 1;
            if (room <= 0)
               return;
            mch_memmove(buf + trs_len, buf + 1, (Unt)len);
         }
         mch_memmove(buf, trs, (Unt)trs_len);
         --len;
      }
      buf += trs_len;
   }
}

// Translate a string into allocated memory, replacing special chars with printable chars.
CS
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
         STRCAT(res, transchar_byte(*p++));
   }
   return res;
}

//Convert the string "str[orglen]" to do ignore-case comparing. Uses the current locale.
//When "buf" is NULL, return an allocated string (NULL for out-of-memory).
//Otherwise put the result in "buf[bufLen]".
CS
str_foldcase(
   CS str,
   Unt orglen,
   CS buf,
   Unt bufLen
) {
   ArrayList   ga;
   Unt len = orglen;

#define GA_CHAR(i)  ((CS)ga.c)[i]
#define GA_PTR(i)   ((CS)ga.c + (i))
#define STR_CHAR(i)  (buf ? buf[i] : GA_CHAR(i))
#define STR_PTR(i)   (buf ? buf + (i) : GA_PTR(i))

   // Copy "str" into "buf" or allocated memory, unmodified.
   if (buf) {
      if (len >= bufLen)       // Ugly!
         len = bufLen - 1;
      mch_memmove(buf, str, (Unt)len);
      buf[len] = ZERO;
   } else {
      ga_init2(&ga, 1, 10);
      if (ga_grow(&ga, len + 1) == FAIL)
         return NULL;
      mch_memmove(ga.c, str, (Unt)len);
      ga.len = len;
      GA_CHAR(len) = ZERO;
   }

   // Make each character lower case.
   Unt i = 0;
   while (STR_CHAR(i) != ZERO) {
      int c = mb_ptr2char(STR_PTR(i));
      Unt olen = utf_ptr2len(STR_PTR(i));
      int lc = utf_tolower(c);

      //Only replace the character when it is not an invalid sequence (ASCII character or more 
      //than one byte) and utf_tolower() doesn't return the original character.
      if ((c < 0x80 || olen > 1) && c != lc) {
         Unt nlen = mb_char2len(lc);

         //If the byte length changes need to shift the following characters forward or backward.
         if (olen != nlen) {
            if (nlen > olen) {
               if (buf ? len + nlen - olen >= bufLen : ga_grow(&ga, nlen - olen + 1) == FAIL
               ) {
                  // out of memory, keep old char
                  lc = c;
                  nlen = olen;
               }
            }
            if (olen != nlen) {
               if (!buf) {
                  STRMOVE(buf + i + nlen, buf + i + olen);
                  len += nlen - olen;
               } else {
                  STRMOVE(GA_PTR(i) + nlen, GA_PTR(i) + olen);
                  ga.len += nlen - olen;
               }
            }
         }
         (void)mb_char2bytes(lc, STR_PTR(i));
      }
      // skip to next multi-byte char
      i += utfCharLen(STR_PTR(i));
   }

   return buf ? buf : (CS)ga.c;
}

//Convert the lower 4 bits of byte "c" to its hex character.
//Lower case letters are used to avoid the confusion of <F1> being 0xf1 or function key 1.
unsigned
nr2hex(unsigned c) {
   if ((c & 0xf) <= 9)
      return (c & 0xf) + '0';
   return (c & 0xf) - 10 + 'a';
}

void
transchar_hex(CS buf, int c) {
   buf[0] = '<';
   int i = 0;
   if (c > 255) {
      buf[++i] = nr2hex((unsigned)c >> 12);
      buf[++i] = nr2hex((unsigned)c >> 8);
   }
   buf[++i] = nr2hex((unsigned)c >> 4);
   buf[++i] = nr2hex((unsigned)c);
   buf[++i] = '>';
   buf[++i] = ZERO;
}

//Return the number of character cells string "s[len]" will take on the
//screen, counting TABs as two characters: "^I".
int
eeglStrNsize(CS s, int len) {
   int size = 0;

   while (*s != ZERO && --len >= 0) {
      int l = utfCharLen(s);

      size += ptr2cells(s);
      s += l;
      len -= l - 1;
   }

   return size;
}

//Return the number of character cells string "s" will take on the screen,
//counting TABs as two characters: "^I".
int
eeglStrSize(CS s) {
   return eeglStrNsize(s, (int)MAXCOL);
}

//return true if 'c' is a valid file-name character or a wildcard character
//Assume characters above 0x100 are valid (multi-byte).
//Explicitly interpret ']' as a wildcard character as mch_has_wildcard("]") returns false.
int
eeIsFnameChar_or_wc(Unt c) {
   Byte buf[2] = {(Byte)c, ZERO};
   return eeIsFnameChar(c) || c == ']' || mch_has_wildcard(buf);
}

// Skip over ' ' and '\t'.
CS
skipwhite(CS q) {
   CS p = q;
   for (; SPACE_OR_TAB(*p); p++)
      {}
   return p;
}

// Skip over ' '
CS
skipSpace(CS q) {
   CS p = q;

   while (*p == ' ')
      ++p;
   return p;
}

// skip over ' ', '\t' and '\n'.
CS
skipwhite_and_nl(CS q) {
   CS p = q;

   while (SPACE_OR_TAB(*p) || *p == NL)
      ++p;
   return p;
}

// getwhitecols: return the number of whitespace columns (bytes) at the start of a given line
int
getwhitecols_curline(void) {
   return getwhitecols(ml_get_curline());
}

int
getwhitecols(CS p) {
   return skipwhite(p) - p;
}

// skip over digits
CS
skipdigits(CS q) {
   CS p = q;

   while (EE_ISDIGIT(*p))   // skip to next non-digit
      ++p;
   return p;
}

//Equivalent of eeIsDigit and eeIsXDigit() that can handle characters > 0x100.
private int
eeIsBDigit(int c) {
   return (c == '0' || c == '1');
}

// skip over binary digits
CS
skipbin(CS q) {
   CS p = q;

   while (eeIsBDigit(*p))   // skip to next non-digit
      ++p;
   return p;
}

// skip over digits and hex characters
CS
skiphex(CS q) {
   CS p = q;

   while (eeIsXDigit(*p))   // skip to next non-digit
      ++p;
   return p;
}


// skip to bin digit (or ZERO after the string)
CS
skiptobin(CS q) {
   CS p = q;
   while (*p != ZERO && !eeIsBDigit(*p))   // skip to next digit
      ++p;
   return p;
}

// skip to digit (or ZERO after the string)
CS
skiptodigit(CS q) {
   CS p = q;

   while (*p != ZERO && !EE_ISDIGIT(*p))   // skip to next digit
      ++p;
   return p;
}

// skip to hex character (or ZERO after the string)
CS
skiptohex(CS q) {
   CS p = q;

   while (*p != ZERO && !eeIsXDigit(*p))   // skip to next digit
      ++p;
   return p;
}

//Variant of isdigit() that can handle characters > 0x100.
//We don't use isdigit() here, because on some systems it also considers
//superscript 1 to be a digit. Use the EE_ISDIGIT() macro for simple arguments.
int
eeIsDigit(int c) {
   return (c >= '0' && c <= '9');
}

//Variant of isxdigit() that can handle characters > 0x100. We don't use isxdigit() here, because 
//on some systems it also considers superscript 1 to be a digit.
int
eeIsXDigit(Unt c) {
   return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

Boole
eeIsLower(Unt c) {
   if (c <= '@')
      return false;
      
   if (c >= 0x80) {
      return utf_islower(c);
   }
   return SAFE_islower(c) != 0;
}

int
eeIsUpper(Unt c) {
   if (c <= '@')
      return false;
   if (c >= 0x80) {
      return utf_isupper(c);
   }
   return SAFE_isupper(c) != 0;
}

int
eeglIsAlfa(int c) {
   return eeIsLower(c) || eeIsUpper(c);
}

int
eeglToUpper(Unt c) {
   if (c <= '@')
      return c;
   if (c >= 0x80) {
      return utf_toupper(c);
   }
   if (c < 0x80)
      return TOUPPER_ASC(c);
   return TOUPPER_LOC(c);
}

int
eeglToLower(Unt c) {
   if (c <= '@')
      return c;
   if (c >= 0x80) {
       return utf_tolower(c);
   }
   if (c < 0x80)
      return TOLOWER_ASC(c);
   return TOLOWER_LOC(c);
}

// skiptowhite: skip over text until ' ' or '\t' or ZERO.
CS
skiptowhite(CS p) {
   while (*p != ' ' && *p != '\t' && *p != ZERO)
      ++p;
   return p;
}

// skiptowhite_esc: Like skiptowhite(), but also skip escaped chars
CS
skiptowhite_esc(CS p) {
   while (*p != ' ' && *p != '\t' && *p != ZERO) {
      if ((*p == '\\' || *p == Ctrl_V) && *(p + 1) != ZERO)
         ++p;
      ++p;
   }
   return p;
}

// "a b c d" -> "d"
CS
skipToLastSpace(CS p) {
   CS lastSpace = null;
   for (; *p != ZERO; p++) {
      if (*p == ' ')
         lastSpace = p;
   }
   return lastSpace ? lastSpace : p;
}

// Get a number from a string and skip over it.
long
parseLong(OUT CS* pp) {
   CS p = *pp;
   long retval = atol((char *)p);
   if (*p == '-')      // skip negative sign
      ++p;
   p = skipdigits(p);      // skip to next non-digit
   *pp = p;
   return retval;
}

// Get a number from a string and skip over it. Allow for embedded single quotes.
long
parseLong_quoted(OUT CS* pp) {
   CS str = *pp;
   long   retval = 0;

   if (*str == '-')
      ++str;
   while (EE_ISDIGIT(*str)) {
      if (retval >= LONG_MAX / 10 - 10)
         retval = LONG_MAX;
      else
         retval = retval * 10 - '0' + *str;
      ++str;
   } 
   if (**pp == '-') {
      if (retval == LONG_MAX)
         retval = LONG_MIN;
      else
         retval = -retval;
   }
   *pp = str;
   return retval;
}

// Return true if "lbuf" is empty or only contains blanks.
int
eeIsBlankLine(CS lbuf) {
   CS p = skipwhite(lbuf);
   return (*p == ZERO || *p == '\r' || *p == '\n');
}

//Convert a string into a long and/or unsigned long, taking care of
//hexadecimal and binary numbers.  Accepts a '-' sign.
//If "prep" is not NULL, returns a flag to indicate the type of the number:
// 0       decimal
// 'B'       bin
// 'b'       bin
// 'X'       hex
// 'x'       hex
//If "len" is not NULL, the length of the number in characters is returned.
//If "nptr" is not NULL, the signed result is returned in it.
//If "unptr" is not NULL, the unsigned result is returned in it.
//If "what" contains STR2NR_BIN recognize binary numbers
//If "what" contains STR2NR_HEX recognize hex numbers
//If "what" contains STR2NR_FORCE always assume bin/hex.
//If "what" contains STR2NR_QUOTE ignore embedded single quotes
//If maxlen > 0, check at a maximum maxlen chars.
//If strict is true, check the number strictly. return *len = 0 if fail.
void
readLongNumber(
   CS start,
   int* prep,    // return: type of number 0 = decimal, 'x' or 'X' is hex, 'b' or 'B' is bin
   int* len,     // return: detected length of number
   int what,     // what numbers to recognize
   Long* nptr, // return: signed result
   Ulong* unptr,  // return: unsigned result
   int maxlen,   // max length of string to check
   Boole strict,   // check strictly
   Boole* overflow  // when not NULL set to true for overflow
){
   CS ptr = start;
   int          pre = 0;      // default is decimal
   int          negative = false;
   Ulong    un = 0;

   if (len)
      *len = 0;

   if (ptr[0] == '-') {
      negative = true;
      ++ptr;
   }

   // Recognize hex, and bin.
   if (ptr[0] == '0' && ptr[1] != '8' && ptr[1] != '9' && (maxlen == 0 || maxlen > 1)) {
      pre = ptr[1];
      if ((what & STR2NR_HEX)
            && (pre == 'X' || pre == 'x') && eeIsXDigit(ptr[2])
            && (maxlen == 0 || maxlen > 2))
         // hexadecimal
         ptr += 2;
      ei ((what & STR2NR_BIN)
            && (pre == 'B' || pre == 'b') && eeIsBDigit(ptr[2])
            && (maxlen == 0 || maxlen > 2))
         // binary
         ptr += 2;
      else { // decimal
         pre = 0;
      }
   }

   // Do the conversion manually to avoid sscanf() quirks.
   int n = 1;
   if (pre == 'B' || pre == 'b' || ((what & STR2NR_BIN) && (what & STR2NR_FORCE))) {
      // bin
      if (pre != 0)
          n += 2;       // skip over "0b"
      while ('0' <= *ptr && *ptr <= '1') {
          // avoid ubsan error for overflow
          if (un <= UVARNUM_MAX / 2)
         un = 2 * un + (Ulong)(*ptr - '0');
          else {
         un = UVARNUM_MAX;
         if (overflow != NULL)
             *overflow = true;
          }
          ++ptr;
          if (n++ == maxlen)
         break;
          if ((what & STR2NR_QUOTE) && *ptr == '\'' && '0' <= ptr[1] && ptr[1] <= '1') {
         ++ptr;
         if (n++ == maxlen)
             break;
          }
      }
   } ei (pre != 0 || ((what & STR2NR_HEX) && (what & STR2NR_FORCE))) {
      // hex
      if (pre != 0)
          n += 2;       // skip over "0x"
      while (eeIsXDigit(*ptr)) {
         // avoid ubsan error for overflow
         if (un <= UVARNUM_MAX / 16)
            un = 16 * un + (Ulong)hex2nr(*ptr);
         else {
            un = UVARNUM_MAX;
            if (overflow)
                *overflow = true;
         }
         ++ptr;
         if (n++ == maxlen)
            break;
         if ((what & STR2NR_QUOTE) && *ptr == '\'' && eeIsXDigit(ptr[1])) {
            ++ptr;
            if (n++ == maxlen)
                break;
         }
      }
   } else {
      // decimal
      while (EE_ISDIGIT(*ptr)) {
         Ulong    digit = (Ulong)(*ptr - '0');

         // avoid ubsan error for overflow
         if (un < UVARNUM_MAX / 10
             || (un == UVARNUM_MAX / 10 && digit <= UVARNUM_MAX % 10))
            un = 10 * un + digit;
         else {
            un = UVARNUM_MAX;
            if (overflow != NULL)
                *overflow = true;
         }
         ++ptr;
         if (n++ == maxlen)
            break;
         if ((what & STR2NR_QUOTE) && *ptr == '\'' && EE_ISDIGIT(ptr[1])) {
            ++ptr;
            if (n++ == maxlen)
               break;
         }
      }
   }

   // Check for an alphanumeric character immediately following, that is most likely a typo.
   if (strict && n - 1 != maxlen && ASCII_ISALNUM(*ptr))
      return;

   if (prep != NULL)
      *prep = pre;
   if (len != NULL)
      *len = (int)(ptr - start);
   if (nptr) {
      if (negative) {  // account for leading '-' for decimal numbers
          // avoid ubsan error for overflow
         if (un > VARNUM_MAX) {
            *nptr = VARNUM_MIN;
            if (overflow != NULL)
                *overflow = true;
         } else
            *nptr = -(Long)un;
      } else {
         // prevent a large unsigned number to become negative
         if (un > VARNUM_MAX) {
            un = VARNUM_MAX;
            if (overflow != NULL)
                *overflow = true;
         }
         *nptr = (Long)un;
      }
   }
   if (unptr)
      *unptr = un;
}

//Return the value of a single hex character.
//Only valid when the argument is '0' - '9', 'A' - 'F' or 'a' - 'f'.
int
hex2nr(int c) {
   if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
   if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
   return c - '0';
}

// Convert two hex characters to a byte. Return -1 if one of the characters is not hex.
int
hexhex2nr(CS p) {
   if (!eeIsXDigit(p[0]) || !eeIsXDigit(p[1]))
      return -1;
   return (hex2nr(p[0]) << 4) + hex2nr(p[1]);
}

//Return true if "str" starts with a backslash that should be removed. '$' is a valid file name 
//character, we don't remove the backslash before it.  This means it is not possible to use an 
//environment variable after a backslash. "~/\$EEGL\doc" is taken literally, only "$EEGL\doc" works.
//Assume a file name doesn't start with a space.
//For multi-byte names, never remove a backslash before a non-ascii
//character, assume that all multi-byte characters are valid file name characters.
int
rem_backslash(CS filename) {
   return (filename[0] == '\\' && filename[1] != ZERO);
}

// Halve the number of backslashes in a file name argument.
void
backslash_halve(CS p) {
   for ( ; *p; ++p) {
      if (rem_backslash(p))
         STRMOVE(p, p + 1);
   } 
}

// backslash_halve() plus save the result in allocated memory.
CS
backslash_halve_save(CS p) {
   CS res = copyStr(p);
   backslash_halve(res);
   return res;
}

//}}}
//{{{directory name

// Append a sub-directory name to DirName. Ensure that the underlying array end with a slash.
void
appendSubDir(CS subDir, OUT DirName* dn) {
   Int const rawLen = STRLEN(subDir);
   if (rawLen == 0) {
      return;
   } 
   
   Boole needToAppendSlash = subDir[rawLen - 1] != '/';
   Int len = rawLen + (needToAppendSlash ? 1 : 0);
   
   if (dn->len + len > dn->cap) {
      Int newCap = dn->len + len;
      Arr(Byte) newCont = allocateArray(newCap + 1, Byte, dn->a);
      if (dn->c) {
         memcpy(newCont, dn->c, dn->len);
      } 
      dn->c = newCont;
      dn->cap = newCap;
   }
   memcpy(dn->c + dn->len, subDir, rawLen);
   if (needToAppendSlash) {
      dn->c[dn->len + rawLen] = '/';
   }
   dn->len += len;
   dn->c[len] = ZERO;
}

// Remove one subdirectory from DirName. Return false if impossible (because it's already empty)
Boole
removeSubDir(OUT DirName* restrict dn) {
   CS p = (CS)dn->c + dn->len - 2;
   for (; *p != '/' && p >= dn->c; p--)
      {} 
   if (p > dn->c) {
      dn->len = p - dn->c;
      return true;
   } else {
      return false; 
   }
}

// Copy full dir name to an allocation outside the arena & glue a file name to its end.
CS
toFullFileName(Text fileName, DirName* dn) {
   CS theString = alloc(dn->len + fileName.len + 1);
   memcpy(theString, dn->c, dn->len);
   memcpy(theString + dn->len, fileName.c, fileName.len);
   theString[dn->len + fileName.len] = ZERO;
   return theString;
}

//}}}
//{{{chunky strings

// Linked lists of array chars living in an Arena. Very useful for file path construction

declStruct(ChunkString);
struct ChunkString {
   Arr(Byte const) c;
   Int len;
   ChunkString* next;
};

struct ChunkyString {
   ChunkString first;
   ChunkString* last;
   Int len; 
   Arena* a;
};

ChunkyString*
createChunkyString(Arr(Byte const) c, Arena* a) {
   ChunkyString* result = allocate(ChunkyString, a);
   Int len = strlen((Arr(char))c);
   *result = (ChunkyString){
      .first = (ChunkString){ .len = len, .c = c, .next = null }, 
      .last = &(result->first), 
      .len = len, .a = a
   };
   return result;
}

void
appendToChunkyString(Arr(Byte const) c, ChunkyString* chunky) {
   Int len = strlen((Arr(char))c);
   ChunkString* penult = chunky->last;
   ChunkString* newLast = allocate(ChunkString, chunky->a);
   *newLast = (ChunkString){.c = c, .len = len, .next = penult};
   chunky->last = newLast;
   chunky->len += len;
}

// Linearize a chunky string into a contiguous ZERO-terminated string in the same arena
CS
toStringChunky(ChunkyString* chunky) {
   CS contiguous = allocateArray(chunky->len + 1, Byte, chunky->a);
   contiguous[chunky->len] = ZERO;
   int i = 0;
   for (ChunkString* p = &(chunky->first); p; p = p->next) {
      memcpy(contiguous + i, p->c, p->len);
      i += p->len;
   }
   return contiguous;
}

//}}}
//{{{unicode

// Return the number of bytes the UTF-8 encoding of the character at "p" takes.
// This includes following composing characters.
Unt
utfCharLen(CS p) {
   int      b0 = *p;
   if (b0 == ZERO)
      return 0;
   if (b0 < 0x80 && p[1] < 0x80)   // be quick for ASCII
      return 1;
      
   // Skip over first UTF-8 char, stopping at a ZERO byte.
   Unt len = utf_ptr2len(p);

   // Check for illegal byte.
   if (len == 1 && b0 >= 0x80)
      return 1;

   // Check for composing characters.  We can handle only the first 6, but
   // skip all of them (otherwise the cursor would get stuck).
   for (;;) {
      if (p[len] < 0x80 || !UTF_COMPOSINGLIKE(p + prevlen, p + len))
          return len;

      // Skip over composing char
      len += utf_ptr2len(p + len);
   }
}

//Return the number of bytes the UTF-8 encoding of the character at "p[size]" takes.
//This includes following composing characters.
//Return 0 for an empty string. Return 1 for an illegal char or an incomplete byte sequence.
Unt
utfCharLen_len(Byte* p, int size) {
   int len;

   if (size < 1 || *p == ZERO)
      return 0;
   if (p[0] < 0x80 && (size == 1 || p[1] < 0x80)) // be quick for ASCII
      return 1;

   // Skip over first UTF-8 char, stopping at a ZERO byte.
   len = utf_ptr2len_len(p, size);

   // Check for illegal byte and incomplete byte sequence.
   if ((len == 1 && p[0] >= 0x80) || len > size)
      return 1;

   //Check for composing characters.  We can handle only the first six, but
   //skip all of them (otherwise the cursor would get stuck).
   while (len < size) {
      int len_next_char;

      if (p[len] < 0x80)
         break;

      //Next character length should not go beyond size to ensure that
      //UTF_COMPOSINGLIKE(...) does not read beyond size.
      len_next_char = utf_ptr2len_len(p + len, size - len);
      if (len_next_char > size - len)
         break;

      if (!UTF_COMPOSINGLIKE(p + prevlen, p + len))
         break;

      // Skip over composing char
      len += len_next_char;
   }
   return len;
}

//}}}
//{{{basic string manipulation

Text
mbText(NULLABLE CS b) {
   return b ? (Text){b, STRLEN(b)} : (Text){null, 0};
}

Text
text(CS b) {
   return (Text){b, STRLEN(b)};
}

Boole
eq_Text_Text(Text a, Text b) {
   return a.len == b.len && memcmp(a.c, b.c, a.len) == 0;
}

Boole
eq_Text_CString(Text a, CS b) {
   return a.len == STRLEN(b) && memcmp(a.c, b, a.len) == 0;
}

Boole
eq_CString_CString(CS a, CS b) {
   Unt len = STRLEN(a);
   return len == STRLEN(b) && memcmp(a, b, len + 1) == 0;
}

//Move input text forward to some position within it.
//Precond: "t" points into "inp"
Text
skipTo(Text inp, CS t) {
   return (Text){.c = t, .len = inp.len - (t - inp.c)};
}

// Find the start of the next word.
// Return a pointer to the first char of the word. Also stop at a ZERO.
CS
findWordStart(CS ptr) {
   while (*ptr != ZERO && *ptr != '\n' && mb_get_class(ptr) <= 1)
      ptr += utfCharLen(ptr);
   return ptr;
}

//"asdf,bcjk"  => "asdf,bcjk"
// ^                   ^
CS
skipToComma(CS s) {
   for (; *s != ZERO && *s != ','; s++)
      {}
   return s;
}

//Skip to next part of an option argument: Skip space and comma.
//"asdf,  bcjk"  => "asdf,  bcjk"
//     ^                    ^
CS
skip_to_option_part(CS p) {
   if (*p == ',')
      ++p;
   while (*p == ' ')
      ++p;
   return p;
}

#define USING_FLOAT_STUFF

// Copy "string" into newly allocated memory.
CS
copyStr(CS string) {
   Unt len = STRLEN(string) + 1;
   CS p = alloc(len);
   mch_memmove(p, string, len);
   return p;
}

//Copy up to "len" bytes of "string" into newly allocated memory and terminate with a ZERO.
//The allocated memory always has size "len + 1", also when "string" is shorter.
CS
copySubstr(CS string, Unt len) {
   CS p = alloc(len + 1);
   STRNCPY(p, string, len);
   p[len] = ZERO;
   return p;
}

Sbuf
sbuf(Unt cap) {
   if (cap > 0) {
      CS c = malloc(cap + 1);
      c[cap] = ZERO;
      return (Sbuf){.c = c, .len = 0, .cap = cap};
   } else {
      return (Sbuf){.c = null, .len = 0, .cap = 0};
   }
}

//Write a string to buffer. Precondition: the buffer has sufficient space
void
concatToBuf(Text s, OUT Sbuf* buf) {
   if (s.len > 0) {
      memcpy(buf->c + buf->len, s.c, s.len);
      buf->c[buf->len + s.len] = ZERO;
      buf->len += s.len + 1;
   }
}

// Copy up to "len" bytes of "string" into the arena and terminate with a ZERO.
// The allocated memory always has size "len + 1", even when "string" is shorter.
CS
copySubstrA(CS string, Unt len, Arena* a) {
   CS p = allocateArray(len + 1, Byte, a);
   p[len] = ZERO;
   STRNCPY(p, string, len);
   return p;
}

// Copy a text into newly allocated memory and terminate with a ZERO.
Text
copyText(Text slice) {
   CS retVal = alloc(slice.len + 1);
   STRNCPY(retVal, slice.c, slice.len);
   retVal[slice.len] = ZERO;
   return text(retVal);
}

CS
copyStrA(CS string, Arena* a) {
   Unt len = STRLEN(string) + 1;
   CS p = allocateArray(len + 1, Byte, a);
   mch_memmove(p, string, len);
   return p;
}

// Same as copyStr(), but any characters found in esc_chars are preceded by a backslash.
CS
copyStr_escaped(CS string, CS escChars) {
   return copyStr_escaped_ext(string, escChars, '\\', false, null);
}

// Same as copyStr(), but any characters found in esc_chars are preceded by a backslash.
CS
copyStrEscapedA(CS string, CS escChars, Arena* a) {
   return copyStr_escaped_ext(string, escChars, '\\', false, a);
}

//Same as copyStr_escaped(), but when "bsl" is true also escape characters where rem_backslash() 
//would remove the backslash. Escape the characters with "cc".
//If "a" is non-null, allocation is within it, otherwise it's separate allocations
CS
copyStr_escaped_ext(CS string, CS esc_chars, Unt cc, Boole bsl, Arena* a) {
   int l;

   // First count the number of backslashes required.
   // Then allocate the memory and insert them.
   Unt length = 1;            // count the trailing ZERO
   CS p;
   for (p = string; *p; p++) {
      if ((l = utfCharLen(p)) > 1) {
         length += l;      // count a multibyte char
         p += l - 1;
         continue;
      }
      if (firstOccurrence(esc_chars, *p) != NULL || (bsl && rem_backslash(p)))
         ++length;         // count a backslash
      ++length;         // count an ordinary char
   }
   CS escaped_string = a ? allocateArray(length, Byte, a) : alloc(length);
   CS p2 = escaped_string;
   for (p = string; *p; p++) {
      if ((l = utfCharLen(p)) > 1) {
         mch_memmove(p2, p, (Unt)l);
         p2 += l;
         p += l - 1;      // skip multibyte char
         continue;
      }
      if (firstOccurrence(esc_chars, *p) != NULL || (bsl && rem_backslash(p)))
          *p2++ = cc;
      *p2++ = *p;
    }
    *p2 = ZERO;
    return escaped_string;
}

Arr(CS)
splitByCharIntoArray(CS inp, Byte delimiter, OUT Unt* len) {
   Unt count = 1;
   for (Unt i = 0; inp[i] != ZERO; i++) {
      if (inp[i] == delimiter && inp[i + 1] != delimiter) {
         count++;
      }
   }

   Arr(CS) result = alloc(sizeof(CS)*count);
   Unt resultInd = 0;
   CS prev = inp;
   for (Unt i = 0; inp[i] != ZERO; i++) {
      if (inp[i] == delimiter && inp[i + 1] != delimiter) {
         int chunkLen = inp + i - prev;
         CS chunk = alloc(chunkLen + 1); // +1 for the ZERO
         memcpy(chunk, prev, chunkLen);
         chunk[chunkLen] = ZERO;
         result[resultInd] = chunk;
         resultInd++;
         prev = inp + i;
      }
   }
   *len = count;
   return result;
}

// "a b c" -> ["a" "b" "c"]
ArrayList
splitBySpace(CS inp) {
   ArrayList split;
   ga_init(&split);
   split.c = alloc(2*sizeof(Byte*));
   split.cap = 2;
   
   int prevInd = -1;
   CS p;
   for (p = inp; *p != ZERO; p++) {
      if (*p == ' ') {
         int lenPiece = p - inp - prevInd - 1;
         if (lenPiece == 0) {
            continue;
         }
         
         CS piece = alloc(lenPiece + 1);
         memcpy(piece, inp + prevInd + 1, lenPiece);
         piece[lenPiece] = ZERO;  
         ga_add_string(&split, piece);
         
         prevInd = p - inp;
      }
   }
   if (p - inp > prevInd + 1) {
      int lenPiece = p - inp - prevInd - 1;
      CS piece = alloc(lenPiece + 1);
      memcpy(piece, inp + prevInd + 1, lenPiece);
      piece[lenPiece] = ZERO;  
      ga_add_string(&split, piece);
   }
   return split;
}

//Escape "string" for use as a shell argument with system().
//This uses single quotes.
//Escape a newline, depending on the 'shell' option. When "do_special" is true also replace 
//"!", "%", "#" and things starting
//with "<" like "<cfile>".
//When "do_newline" is false do not escape newline unless it is csh shell.
//Return the result in allocated memory, NULL if we have run out.
CS
copyStr_shellescape(CS string, int do_special, int do_newline) {
   Unt l;

   // First count the number of extra bytes required.
   Unt length = STRLEN(string) + 3;  // two quotes and a trailing ZERO
   CS p; 
   for (p = string; *p != ZERO; MB_PTR_ADV(p)) {
      if (*p == '\'') {
         length += 3;      // ' => '\''
      }
      if ((*p == '\n' && do_newline) || (*p == '!' && do_special)) {
         ++length;         // insert backslash
      }
      if (do_special && find_commline_var(p, &l) >= 0) {
         ++length;         // insert backslash
         p += l - 1;
      }
   }

   // Allocate memory for the result and fill it.
   CS escaped_string = alloc(length);
   CS d = escaped_string;

   // add opening quote
   *d++ = '\'';

   for (p = string; *p != ZERO; ) {
      if (*p == '\'') {
         *d++ = '\'';
         *d++ = '\\';
         *d++ = '\'';
         *d++ = '\'';
         ++p;
         continue;
      }
      if ((*p == '\n' && do_newline) || (*p == '!' && do_special)) {
         *d++ = '\\';
         *d++ = *p++;
         continue;
      }
      if (do_special && find_commline_var(p, &l) >= 0) {
         *d++ = '\\';      // insert backslash
         memcpy(d, p, l);   // copy the var
         d += l;
         p += l;
         continue;
      }

      MB_COPY_CHAR(p, d);
   }

   // add terminating quote and finish with a ZERO
      *d++ = '\'';
   *d = ZERO;

    return escaped_string;
}

//Like copyStr(), but make all characters uppercase.
//This uses ASCII lower-to-upper case translation, language independent.
CS
copyStr_up(CS string) {
   CS p1 = copyStr(string);
   asciiToUpper(p1);
   return p1;
}

//Like copySubstr(), but make all characters uppercase.
//This uses ASCII lower-to-upper case translation, language independent.
CS
copySubstr_up(CS string, Unt len) {
   CS p1 = copySubstr(string, len);
   asciiToUpper(p1);
   return p1;
}

//Eegl has its own isspace() function, because on some machines isspace()
//can't handle characters above 128.
int
isSpace(int x) {
   return ((x >= 9 && x <= 13) || x == ' ');
}

//ASCII lower-to-upper case translation, language independent.
void
asciiToUpper(CS p) {
   if (!p)
      return;

   CS p1 = p;
   int c;
   while ((c = *p1) != ZERO) {
      *p1++ = (c < 'a' || c > 'z') ? c : (c - 0x20);
   } 
}

//Make string "s" all upper-case and return it in allocated memory.
//Handle multi-byte characters as well as possible.
private CS
strup_save(CS orig) {
   CS p;
   CS res = p = copyStr(orig);
      
   while (*p != ZERO) {
      int c = mb_ptr2char(p);
      Unt l = utf_ptr2len(p);
      if (c == 0) {
         // overlong sequence, use only the first byte
         c = *p;
         l = 1;
      }
      int uc = utf_toupper(c);

      //Reallocate string when byte count changes. This is rare,
      //thus it's OK to do another malloc()/free().
      Unt newl = mb_char2len(uc);
      if (newl != l) {
         CS s = alloc(STRLEN(res) + 1 + newl - l);
         mch_memmove(s, res, p - res);
         STRCPY(s + (p - res) + newl, p + l);
         p = s + (p - res);
         eeglFree(res);
         res = s;
      }

      mb_char2bytes(uc, p);
      p += newl;
   }
   return res;
}

//Skip from starting quote to ending quote, including skipping escaped quotes
Text
skipQuoted(Text s) {
   CS const sentinel = s.c + s.len;
   for (CS a = s.c + 1; a < sentinel; a++) {
      if (*a == '\\' && a[1] == '"')
         a++;
      ei (*a == '"')
         return (Text){a + 1, s.len - (a - s.c) - 1};
   }
   return (Text){sentinel, 0};
}

//Skip from starting apostrofe to ending apostrofe, including skipping escaped quotes
Text
skipSingleQuoted(Text s) {
   CS const sentinel = s.c + s.len;
   for (CS a = s.c + 1; a < sentinel; a++) {
      if (*a == '\\' && a[1] == '\'')
         a++;
      ei (*a == '\'')
         return (Text){a + 1, s.len - (a - s.c) - 1};
   }
   return (Text){sentinel, 0};
}

//Make string "s" all lower-case and return it in allocated memory.
//Handles multi-byte characters as well as possible.
CS
strlow_save(CS orig) {
   CS res = copyStr(orig);
      
   for (CS p = res; *p != ZERO;) {
      int c = mb_ptr2char(p);
      Unt l = utf_ptr2len(p);
      if (c == 0) {
         // overlong sequence, use only the first byte
         c = *p;
         l = 1;
      }
      int lc = utf_tolower(c);

      //Reallocate string when byte count changes. This is rare,
      //thus it's OK to do another malloc()/free().
      Unt newl = mb_char2len(lc);
      if (newl != l) {
         CS s = alloc(STRLEN(res) + 1 + newl - l);
         mch_memmove(s, res, p - res);
         STRCPY(s + (p - res) + newl, p + l);
         p = s + (p - res);
         eeglFree(res);
         res = s;
      }

      mb_char2bytes(lc, p);
      p += newl;
   }
   return res;
}

//delete spaces at the end of a string
void
del_trailing_spaces(CS ptr) {
   CS q = ptr + STRLEN(ptr);
   while (--q > ptr && SPACE_OR_TAB(q[0]) && q[-1] != '\\' && q[-1] != Ctrl_V)
      *q = ZERO;
}

//Like strncpy(), but always terminate the result with one ZERO.
//"to" must be "len + 1" long!
void
copySubstrToAllocation(OUT CS to, Text from) {
   STRNCPY(to, from.c, from.len);
   to[from.len] = ZERO;
}

//Like strcat(), but make sure the result fits in "tosize" bytes and is
//always ZERO terminated. "from" and "to" may overlap.
void
concatenateStrings(CS to, CS from, Unt tosize) {
   Unt tolen = STRLEN(to);
   Unt fromlen = STRLEN(from);

   if (tolen + fromlen + 1 > tosize) {
      mch_memmove(to + tolen, from, tosize - tolen - 1);
      to[tosize - 1] = ZERO;
   } else
      mch_memmove(to + tolen, from, fromlen + 1);
}

//Compare two ASCII strings, for length "len", ignoring case, ignoring locale (mostly matters 
//for Turkish locale where i I might be different). return 0 for match, < 0 for smaller, > 0 for 
//bigger
int
compareAscii(CS s1, CS s2, Unt len) {
   int i = 0;
   while (len > 0) {
      i = TOLOWER_ASC(*s1) - TOLOWER_ASC(*s2);
      if (i != 0)
         break;         // this character is different
      if (*s1 == ZERO)
         break;         // strings match until ZERO
      ++s1;
      ++s2;
      --len;
   }
   return i;
}

//Search for first occurrence of "c" in "string". Version of strchr() that handles strings with 
//characters from 128 to 255 correctly. Don't return a pointer to the ZERO at the string end
CS
firstOccurrence(CS string, Unt c) {
   CS p = string;
   if (c >= 0x80) {
      while (*p != ZERO) {
         int l = utfCharLen(p);

         // Avoid matching an illegal byte here.
         if ((Unt)mb_ptr2char(p) == c && l > 1)
            return p;
         p += l;
      }
      return NULL;
   }
   Unt b;
   while ((b = *p) != ZERO) {
      if (b == c)
         return p;
      p += utfCharLen(p);
   }
   return NULL;
}

//Search for first occurrence of byte "b" in "string". do not return a pointer to the ZERO at the end of the string.
CS
firstByteOccurrence(CS string, Byte b) {
   for (CS p = string; *p != ZERO; p++) {
      if (*p == b) {
         return p;
      }
   }
   return null;
}

//Version of strchr() that only works for bytes and handles unsigned char
//strings with characters above 128 correctly. It also doesn't return a
//pointer to the ZERO at the end of the string.
CS
eeStrbyte(CS string, Unt c) {
   for (CS p = string; *p != ZERO; p++) {
      if (*p == c)
         return p;
   }
   return NULL;
}

//Search for last occurrence of "c" in "string". Version of strrchr() that handles unsigned char 
//strings with characters from 128 to 255 correctly.  It also doesn't return a pointer to the 
//ZERO at the end of the string. Return NULL if not found. Does not handle multi-byte char 
//for "c"!
CS
lastOccurrence(CS string, int c) {
   CS retval = NULL;
   for (CS p = string; *p != ZERO; MB_PTR_ADV(p)) {
      if (*p == c)
         retval = p;
   }
   return retval;
}

//Eegl's version of strpbrk(), in case it's missing.
//Don't generate a prototype for this, causes problems when it's not used.
#ifndef PROTO
# ifndef HAVE_STRPBRK
#  ifdef eeStrpbrk
#   undef eeStrpbrk
#  endif
CS
eeStrpbrk(CS s, CS charset) {
   while (*s) {
      if (firstOccurrence(charset, *s) != NULL)
         return s;
      MB_PTR_ADV(s);
   }
   return NULL;
}
# endif
#endif

// Sort an array of strings.
private int
stringComparer(void const *s1, void const*s2) {
    return STRCMP(*(char **)s1, *(char **)s2);
}

void
sortStrings(Arr(CS) files, int count) {
   qsort((void *)files, (Unt)count, sizeof(CS), stringComparer);
}

// Return true if string "s" contains a non-ASCII character (128 or higher). false for null
int
has_non_ascii(CS s) {
   if (s) {
      for (CS p = s; *p != ZERO; ++p) {
         if (*p >= 128)
            return true;
      } 
   }
   return false;
}

// Concatenate two strings and return the result in allocated memory.
CS
concat_str(CS str0, CS str1) {
   Unt l = str0 ? STRLEN(str0) : 0;

   CS dest = alloc(l + (str1 == NULL ? 0 : STRLEN(str1)) + 1L);
   if (str0)
      STRCPY(dest, str0);
   else
      *dest = ZERO;
      
   if (str1)
      STRCPY(dest + l, str1);
   return dest;
}

// Reverse text into allocated memory. Return the allocated string
CS
reverse_text(CS s) {
   Unt len = STRLEN(s);
   CS rev = alloc(len + 1);

   for (Unt s_i = 0, rev_i = len; s_i < len; ++s_i) {
      int mb_len = utfCharLen(s + s_i);
      rev_i -= mb_len;
      mch_memmove(rev + rev_i, s + s_i, mb_len);
      s_i += mb_len - 1;
   }
   rev[len] = ZERO;
   return rev;
}

//Return string "str" in ' quotes, doubling ' characters. If "str" is NULL an empty string is 
//assumed. If "function" is true make it function('string').
CS
string_quote(CS str, int function) {
   CS p;

   Unt len = (function ? 13 : 3);
   if (str) {
      len += (unsigned)STRLEN(str);
      for (p = str; *p != ZERO; MB_PTR_ADV(p))
          if (*p == '\'')
         ++len;
   }
   CS r = alloc(len);
   CS s = r;

   if (function) {
      STRCPY(r, "function('");
      r += 10;
   } else
      *r++ = '\'';
   if (str) {
      for (p = str; *p != ZERO; ) {
          if (*p == '\'')
         *r++ = '\'';
          MB_COPY_CHAR(p, r);
      }
   } 
   *r++ = '\'';
   if (function)
      *r++ = ')';
   *r++ = ZERO;
   return s;
}

// Count the number of times "needle" occurs in string "haystack". Case is ignored if "ic" is true.
long
string_count(CS haystack, CS needle, int ic) {
   long   n = 0;
   CS p = haystack;

   if (!p || !needle || *needle == ZERO)
      return 0;

   if (ic) {
      Unt len = STRLEN(needle);
      while (*p != ZERO) {
         if (MB_STRNICMP(p, needle, len) == 0) {
            ++n;
            p += len;
         } else
            MB_PTR_ADV(p);
      }
   } else {
      CS next;
      while ((next = (CS)strstr((char *)p, (char *)needle)) != NULL) {
          ++n;
          p = next + STRLEN(needle);
      }
   } 

   return n;
}

//Make a Var of the first character of "input" and store it in "output". Return OK or FAIL.
private int
copy_first_char_to_tv(CS input, Var* output) {
   Byte buf[MB_MAXBYTES + 1];

   if (input == NULL || output == NULL)
      return FAIL;

   int len = utfCharLen(input);
   STRNCPY(buf, input, len);
   buf[len] = ZERO;
   output->tag = VAR_STRING;
   output->string = copyStr(buf);

   return output->string == NULL ? FAIL : OK;
}

//Implementation of map() and filter() for a String. Apply "expr" to every
//character in string "str" and return the result in "returnVar".
void
string_filter_map(
   CS str,
   FilterMap filtermap,
   Var* expr,
   Var* returnVar
) {
   CS p;
   Var   tv;
   ArrayList   ga;
   int      len = 0;
   int      idx = 0;
   int      rem;
   Var   newtv;

   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   // set_EeglVar_nr() doesn't set the type
   set_EeglVar_type(VV_KEY, VAR_NUMBER);

   ga_init2(&ga, sizeof(char), 80);
   for (p = str; *p != ZERO; p += len) {
      if (copy_first_char_to_tv(p, &tv) == FAIL)
         break;
      len = (int)STRLEN(tv.string);

      set_EeglVar_nr(VV_KEY, idx);
      if (filter_map_one(&tv, expr, filtermap, &newtv, &rem) == FAIL || anyEmsgG) {
         clearVar(&newtv);
         clearVar(&tv);
         break;
      }
      if (filtermap == FILTERMAP_MAP || filtermap == FILTERMAP_MAPNEW) {
         if (newtv.tag != VAR_STRING) {
            clearVar(&newtv);
            clearVar(&tv);
            emsg(_(e_string_required));
            break;
         } else
            ga_concat(&ga, newtv.string);
      }
      ei (filtermap == FILTERMAP_FOREACH || !rem)
         ga_concat(&ga, tv.string);

      clearVar(&newtv);
      clearVar(&tv);

      ++idx;
   }
   ga_append(&ga, ZERO);
   returnVar->string = ga.c;
}

//Implementation of reduce() for String "argvars[0]" using the function "expr"
//starting with the optional initial value "argvars[2]" and return the result in "returnVar".
void
string_reduce(Var* argvars, Var* expr, Var* returnVar) {
   CS p = tv_get_string(&argvars[0]);
   int      len;
   Var   argv[3];
   int      r;
   int      called_emsg_start = called_emsg;

   if (argvars[2].tag == VAR_UNKNOWN) {
      if (*p == ZERO) {
         showErrFmtMsg(_(e_reduce_of_an_empty_str_with_no_initial_value), "String");
         return;
      }
      if (copy_first_char_to_tv(p, returnVar) == FAIL)
         return;
      p += STRLEN(returnVar->string);
   } ei (check_for_string_arg(argvars, 2) == FAIL)
      return;
   else
      copy_tv(OUT returnVar, &argvars[2]);

   for ( ; *p != ZERO; p += len) {
      argv[0] = *returnVar;
      if (copy_first_char_to_tv(p, &argv[1]) == FAIL)
         break;
      len = (int)STRLEN(argv[1].string);

      r = eval_expr_typval(expr, true, argv, 2, returnVar);

      clearVar(&argv[0]);
      clearVar(&argv[1]);
      if (r == FAIL || called_emsg != called_emsg_start)
          return;
   }
}

// Implementation of "byteidx()" and "byteidxcomp()" functions
private void
byteidx_common(Var* argvars, Var* returnVar, Boole comp) {
   returnVar->number = -1;

   CS str = convertVarToStringSingleUse(&argvars[0]);
   Long idx = varGetNumberChk(argvars + 1, NULL);
   if (!str || idx < 0)
      return;

   Long   utf16idx = false;
   if (argvars[2].tag != VAR_UNKNOWN) {
      Boole error = false;
      utf16idx = varGetNumberChk(argvars + 2, OUT &error);
      if (error)
         return;
      if (utf16idx < 0 || utf16idx > 1) {
         showErrFmtMsg(_(e_using_number_as_bool_nr), utf16idx);
         return;
      }
   }

   Unt (*ptr2len)(Byte*);
   if (comp)
      ptr2len = utf_ptr2len;
   else
      ptr2len = utfCharLen;

   CS t = str;
   for ( ; idx > 0; idx--) {
      if (*t == ZERO)      // EOL reached
         return;
      if (utf16idx) {
         int clen = ptr2len(t);
         int c = (clen > 1) ? mb_ptr2char(t) : *t;
         if (c > 0xFFFF)
            idx--;
      }
      if (idx > 0)
         t += ptr2len(t);
   }
   returnVar->number = (Long)(t - str);
}

void
f_byteidx(Arr(Var) argvars, OUT Var* returnVar) {
   byteidx_common(argvars, returnVar, false);
}

void
f_byteidxcomp(Var* argvars, Var* returnVar) {
   byteidx_common(argvars, returnVar, true);
}

void
f_charidx(Var* argvars, Var* returnVar) {
   returnVar->number = -1;

   if (check_for_string_arg(argvars, 0) == FAIL
         || check_for_number_arg(argvars, 1) == FAIL
         || check_for_opt_bool_arg(argvars, 2) == FAIL
         || (argvars[2].tag != VAR_UNKNOWN && check_for_opt_bool_arg(argvars, 3) == FAIL))
      return;

   CS str = convertVarToStringSingleUse(&argvars[0]);
   Long   idx = varGetNumberChk(argvars + 1, NULL);
   if (str == NULL || idx < 0)
      return;

   Long   countcc = false;
   Long   utf16idx = false;
   if (argvars[2].tag != VAR_UNKNOWN) {
      countcc = tv_get_bool(&argvars[2]);
      if (argvars[3].tag != VAR_UNKNOWN)
         utf16idx = tv_get_bool(&argvars[3]);
   }

   Unt (*ptr2len)(CS);
   if (countcc)
      ptr2len = utf_ptr2len;
   else
      ptr2len = utfCharLen;

   Unt len = 0;
   for (CS p = str; utf16idx ? idx >= 0 : p <= str + idx; len++) {
      if (*p == ZERO) {
         // If the index is exactly the number of bytes or utf-16 code units
         // in the string then return the length of the string in characters.
         if (utf16idx ? (idx == 0) : (p == (str + idx)))
            returnVar->number = len;
         return;
      }
      if (utf16idx) {
         idx--;
         int clen = ptr2len(p);
         int c = (clen > 1) ? mb_ptr2char(p) : *p;
         if (c > 0xFFFF)
            idx--;
      }
      p += ptr2len(p);
   }

   returnVar->number = len > 0 ? len - 1 : 0;
}

// Add the bytes from "str" to "blob".
private void
blob_from_string(CS str, Blob* blob) {
   Unt len = STRLEN(str);

   for (Unt i = 0; i < len; i++) {
      int ch = str[i];

      if (str[i] == NL)
         // Translate newlines in the string to ZERO character
         ch = ZERO;

      ga_append(&blob->c, ch);
   }
}

//Return a string created from the bytes in blob starting at "start_idx". A NL character in the 
//blob indicates end of string. A ZERO character in the blob is translated to a NL.
//On return, "start_idx" points to next byte to process in blob.
private CS
string_from_blob(Blob *blob, long *start_idx) {
   ArrayList str_ga;
   int idx;

   ga_init2(&str_ga, sizeof(char), 80);

   long blen = blob_len(blob);

   for (idx = *start_idx; idx < blen; idx++) {
      Byte byte = (Byte)blob_get(blob, idx);
      if (byte == NL) {
         idx++;
         break;
      }

      if (byte == ZERO)
         byte = NL;

      ga_append(&str_ga, byte);
   }

   ga_append(&str_ga, ZERO);

   CS ret_str = copyStr(str_ga.c);
   *start_idx = idx;

   ga_clear(&str_ga);
   return ret_str;
}

//"blob2str()" function Converts a blob to a string, ensuring valid UTF-8 encoding.
void
f_blob2str(Arr(Var) argvars, OUT Var* returnVar) {
   if (check_for_blob_arg(argvars, 0) == FAIL || check_for_oself_arg(argvars, 1) == FAIL)
      return;

   allocReturnList(returnVar);

   Blob* blob = argvars->blob;
   if (blob == NULL)
      return;
   int blen = blob_len(blob);

   long idx = 0;
   while (idx < blen) {
      CS str = string_from_blob(blob, &idx);
      if (!str)
         break;

      int ret = list_append_string(returnVar->list, str, -1);
      if (ret == FAIL)
         break;
   }
}

// "str2blob()" function
void
f_str2blob(Arr(Var) argvars, OUT Var* returnVar) {
   if (confirmVarIsList(argvars, 0) == FAIL || check_for_oself_arg(argvars, 1) == FAIL)
      return;

   if (returnVar_blob_alloc(returnVar) == FAIL)
      return;

   Blob* blob = returnVar->blob;

   List* list = argvars[0].list;
   if (!list)
      return;

   ListItem* li;
   FOR_ALL_LIST_ITEMS(list, li) {
      if (li->c.tag != VAR_STRING)
         continue;

      CS str = li->c.string;
      if (!str)
         str = E;

      if (li != list->first)
         // Each list string item is separated by a newline in the blob
         ga_append(&blob->c, NL);

      blob_from_string(str, blob);
   }
}

void
f_str2list(Arr(Var) argvars, OUT Var* returnVar) {
   allocReturnList(returnVar);

   CS p = tv_get_string(&argvars[0]);

   for ( ; *p != ZERO; p += utf_ptr2len(p))
      list_append_number(returnVar->list, mb_ptr2char(p));
}

void
f_str2nr(Var* argvars, Var* returnVar) {
   int      base = 10;
   Long   n;
   int      what = 0;

   if (argvars[1].tag != VAR_UNKNOWN) {
      base = (int)tv_get_number(&argvars[1]);
      if (base != 2 && base != 10 && base != 16) {
         emsg(_(e_invalid_argument));
         return;
      }
      if (argvars[2].tag != VAR_UNKNOWN && tv_get_bool(&argvars[2]))
         what |= STR2NR_QUOTE;
   }

   CS p = skipwhite(tv_get_string_strict(&argvars[0]));
   Boole isneg = (*p == '-');
   if (*p == '+' || *p == '-')
      p = skipwhite(p + 1);
      
   switch (base) {
   case 2: what |= STR2NR_BIN + STR2NR_FORCE; break;
   case 16: what |= STR2NR_HEX + STR2NR_FORCE; break;
   }
   readLongNumber(p, NULL, NULL, what, &n, NULL, 0, false, NULL);
   // Text after the number is silently ignored.
   returnVar->number = isneg ? -n : n;
}

void
f_strgetchar(Var* argvars, Var* returnVar) {
   Boole error = false;
   int      byteidx = 0;
   returnVar->number = -1;
   CS str = convertVarToStringSingleUse(&argvars[0]);
   if (!str)
      return;
      
   int len = (int)STRLEN(str);
   int charidx = (int)varGetNumberChk(argvars + 1, OUT &error);
   if (error)
      return;

   while (charidx >= 0 && byteidx < len) {
      if (charidx == 0) {
         returnVar->number = mb_ptr2char(str + byteidx);
         break;
      }
      --charidx;
      byteidx += MB_CPTR2LEN(str + byteidx);
   }
}

void
f_stridx(Var* argvars, Var* returnVar) {
   Byte buf[NUMBUFLEN];
   int start_idx;

   CS needle = convertVarToStringSingleUse(&argvars[1]);
   CS haystack = convertVarToString(&argvars[0], buf);
   CS save_haystack = haystack;
   returnVar->number = -1;
   if (needle == NULL || haystack == NULL)
      return;      // type error; errmsg already given

   if (argvars[2].tag != VAR_UNKNOWN) {
      Boole error = false;

      start_idx = (int)varGetNumberChk(argvars + 2, OUT &error);
      if (error || start_idx >= (int)STRLEN(haystack))
         return;
      if (start_idx >= 0)
         haystack += start_idx;
   }

   CS pos   = (CS)strstr((char *)haystack, (char *)needle);
   if (pos)
      returnVar->number = (Long)(pos - save_haystack);
}

void
f_string(Arr(Var) argvars, OUT Var* returnVar) {
   CS tofree;
   Byte numBuf[NUMBUFLEN];

   returnVar->tag = VAR_STRING;
   returnVar->string = tv2string(&argvars[0], &tofree, numBuf, get_copyID());
   // Make a copy if we have a value but it's not in allocated memory.
   if (returnVar->string != NULL && tofree == NULL)
      returnVar->string = copyStr(returnVar->string);
}

void
f_strlen(Arr(Var) argvars, OUT Var* returnVar) {
   returnVar->number = (Long)(STRLEN(tv_get_string(&argvars[0])));
}

private void
strchar_common(Arr(Var) argvars, OUT Var* returnVar, int skipcc) {
   CS s = tv_get_string(&argvars[0]);
   Long      len = 0;
   
   Unt (*func_inpAdvanceMultibyte)(OUT CS* pp);
   func_inpAdvanceMultibyte = skipcc ? inpAdvanceMultibyte : mb_cptr2char_adv;
   
   while (*s != ZERO) {
      func_inpAdvanceMultibyte(&s);
      ++len;
   }
   returnVar->number = len;
}

void
f_strcharlen(Arr(Var) argvars, OUT Var* returnVar) {
   strchar_common(argvars, returnVar, true);
}

void
f_strchars(Arr(Var) argvars, OUT Var* returnVar) {
   Long      skipcc = false;
   if (argvars[1].tag != VAR_UNKNOWN) {
      Boole error = false;
      skipcc = varGetNumberChk(argvars + 1, OUT &error);
      if (error)
         return;
      if (skipcc < 0 || skipcc > 1) {
         showErrFmtMsg(_(e_using_number_as_bool_nr), skipcc);
         return;
      }
    }

    strchar_common(argvars, returnVar, skipcc);
}

void
f_strdisplaywidth(Arr(Var) argvars, OUT Var* returnVar) {
   int      col = 0;
   returnVar->number = -1;

   CS s = tv_get_string(&argvars[0]);
   if (argvars[1].tag != VAR_UNKNOWN)
      col = (int)tv_get_number(&argvars[1]);

   returnVar->number = (Long)(linetabsize_col(col, s) - col);
}

void
f_strwidth(Var* argvars, OUT Var* returnVar) {
   CS s = tv_get_string_strict(argvars);
   returnVar->number = (Long)(mb_string2cells(s, -1));
}

void
f_strcharpart(Arr(Var) argvars, OUT Var* returnVar) {
   int nbyte = 0;
   int skipcc = false;
   int len = 0;
   Boole error = false;

   CS p = tv_get_string(&argvars[0]);
   int slen = (int)STRLEN(p);

   int nchar = (int)varGetNumberChk(argvars + 1, OUT &error);
   if (!error) {
      if (argvars[2].tag != VAR_UNKNOWN && argvars[3].tag != VAR_UNKNOWN) {
         skipcc = varGetNumberChk(argvars + 3, OUT &error);
         if (error)
            return;
         if (skipcc < 0 || skipcc > 1) {
            showErrFmtMsg(_(e_using_number_as_bool_nr), skipcc);
            return;
         }
      }

      if (nchar > 0) {
         while (nchar > 0 && nbyte < slen) {
            nbyte += skipcc ? utfCharLen(p + nbyte) : MB_CPTR2LEN(p + nbyte);
            --nchar;
         }
      } else
         nbyte = nchar;
      if (argvars[2].tag != VAR_UNKNOWN) {
         int charlen = (int)tv_get_number(&argvars[2]);
         while (charlen > 0 && nbyte + len < slen) {
            int off = nbyte + len;

            len += (off < 0) ? 1 : (skipcc ? utfCharLen(p + off) : MB_CPTR2LEN(p + off));
            --charlen;
         }
      } else
          len = slen - nbyte;    // default: all bytes that are available.
   }

   // Only return the overlap between the specified part and the actual string.
   if (nbyte < 0) {
      len += nbyte;
      nbyte = 0;
   } ei (nbyte > slen)
      nbyte = slen;
   if (len < 0)
      len = 0;
   ei (nbyte + len > slen)
      len = slen - nbyte;

   returnVar->tag = VAR_STRING;
   returnVar->string = copySubstr(p + nbyte, len);
}

void
f_strpart(Arr(Var) argvars, OUT Var* returnVar) {
   Boole error = false;

   CS p = tv_get_string(&argvars[0]);
   int slen = (int)STRLEN(p);

   int n = (int)varGetNumberChk(argvars + 1, OUT &error);
   int      len;
   if (error)
      len = 0;
   ei (argvars[2].tag != VAR_UNKNOWN)
      len = (int)tv_get_number(&argvars[2]);
   else
      len = slen - n;       // default len: all bytes that are available.

   // Only return the overlap between the specified part and the actual string.
   if (n < 0) {
      len += n;
      n = 0;
   } ei (n > slen)
      n = slen;
   if (len < 0)
      len = 0;
   ei (n + len > slen)
      len = slen - n;

   if (argvars[2].tag != VAR_UNKNOWN && argvars[3].tag != VAR_UNKNOWN) {
      // length in characters
      int off;
      for (off = n; off < slen && len > 0; --len)
         off += utfCharLen(p + off);
      len = off - n;
   }

   returnVar->tag = VAR_STRING;
   returnVar->string = copySubstr(p + n, len);
}

void
f_strridx(Arr(Var) argvars, OUT Var* returnVar) {
   Byte buf[NUMBUFLEN];

   CS needle = convertVarToStringSingleUse(&argvars[1]);
   CS haystack = convertVarToString(&argvars[0], buf);

   returnVar->number = -1;
   if (needle == NULL || haystack == NULL)
      return;      // type error; errmsg already given

   int haystack_len = (int)STRLEN(haystack);
   int endInd;
   if (argvars[2].tag != VAR_UNKNOWN) {
      // Third argument: upper limit for index
      endInd = (int)varGetNumberChk(argvars + 2, NULL);
      if (endInd < 0)
          return;   // can never find a match
   } else
      endInd = haystack_len;

   CS lastmatch = NULL;
   if (*needle == ZERO) {
      // Empty string matches past the end.
      lastmatch = haystack + endInd;
   } else {
      for (CS rest = haystack; *rest != '\0'; ++rest) {
         rest = STRSTR(rest, needle);
         if (rest == NULL || rest > haystack + endInd)
            break;
         lastmatch = rest;
      }
   }

   returnVar->number = lastmatch ? (Long)(lastmatch - haystack) : -1;
}

void
f_strtrans(Arr(Var) argvars, OUT Var* returnVar) {
   returnVar->tag = VAR_STRING;
   returnVar->string = sanitizeStr(tv_get_string(&argvars[0]));
}

// "tolower(string)" function
void
f_tolower(Arr(Var) argvars, OUT Var* returnVar) {
   returnVar->tag = VAR_STRING;
   returnVar->string = strlow_save(tv_get_string(&argvars[0]));
}

// "toupper(string)" function
void
f_toupper(Arr(Var) argvars, OUT Var* returnVar) {
   returnVar->tag = VAR_STRING;
   returnVar->string = strup_save(tv_get_string(&argvars[0]));
}

// "tr(string, fromstr, tostr)" function
void
f_tr(Arr(Var) argvars, OUT Var* returnVar) {
   CS p;
   int  first = true;
   Byte buf[NUMBUFLEN];
   Byte buffer1[NUMBUFLEN];

   CS in_str = tv_get_string(&argvars[0]);
   CS fromstr = convertVarToString(&argvars[1], buf);
   CS tostr = convertVarToString(&argvars[2], buffer1);

   // Default return value: empty string.
   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;
   if (fromstr == NULL || tostr == NULL)
      return;      // type error; errmsg already given
      
   ArrayList   ga;
   ga_init2(&ga, sizeof(char), 80);

   // fromstr and tostr have to contain the same number of chars
   while (*in_str != ZERO) {
      int inlen = utfCharLen(in_str);
      CS cpstr = in_str;
      int cplen = inlen;
      int idx = 0;
      int fromlen;
      int tolen;
      for (p = fromstr; *p != ZERO; p += fromlen) {
         fromlen = utfCharLen(p);
         if (fromlen == inlen && STRNCMP(in_str, p, inlen) == 0) {
            for (p = tostr; *p != ZERO; p += tolen) {
               tolen = utfCharLen(p);
               if (idx-- == 0) {
                   cplen = tolen;
                   cpstr = p;
                   break;
               }
            }
            if (*p == ZERO)   // tostr is shorter than fromstr
               goto error;
            break;
         }
         ++idx;
      }

      if (first && cpstr == in_str) {
         // Check that fromstr and tostr have the same number of
         // (multi-byte) characters.  Done only once when a character
         // of in_str doesn't appear in fromstr.
         first = false;
         for (p = tostr; *p != ZERO; p += tolen) {
            tolen = utfCharLen(p);
            --idx;
         }
         if (idx != 0)
            goto error;
      }

      (void)ga_grow(&ga, cplen);
      mch_memmove((char *)ga.c + ga.len, cpstr, (Unt)cplen);
      ga.len += cplen;

      in_str += inlen;
   }

   // add a terminating ZERO
   (void)ga_grow(&ga, 1);
   ga_append(&ga, ZERO);

   returnVar->string = ga.c;
   
   return;   
   
error:
   showErrFmtMsg(_(e_invalid_argument_str), fromstr);
   ga_clear(&ga);
   return;
}

// "trim({expr})" function
void
f_trim(Arr(Var) argvars, OUT Var* returnVar) {
   Byte buffer0[NUMBUFLEN];
   Byte buffer1[NUMBUFLEN];
   CS mask = NULL;
   CS p;
   int      dir = 0;

   returnVar->tag = VAR_STRING;
   returnVar->string = NULL;

   CS head = convertVarToString(&argvars[0], buffer0);
   if (!head)
      return;

   if (check_for_opt_string_arg(argvars, 1) == FAIL)
      return;

    if (argvars[1].tag == VAR_STRING) {
      mask = convertVarToString(&argvars[1], buffer1);
      if (*mask == ZERO)
         mask = NULL;

      if (argvars[2].tag != VAR_UNKNOWN) {
         Boole error = false;

         // leading or trailing characters to trim
         dir = (int)varGetNumberChk(argvars + 2, OUT &error);
         if (error)
            return;
         if (dir < 0 || dir > 2) {
            showErrFmtMsg(_(e_invalid_argument_str), tv_get_string(&argvars[2]));
            return;
         }
      }
   }

   if (dir == 0 || dir == 1) {
      // Trim leading characters
      while (*head != ZERO) {
         Unt c1 = mb_ptr2char(head);
         if (mask == NULL) {
            if (c1 > ' ' && c1 != 0xa0)
                break;
         } else {
            for (p = mask; *p != ZERO; MB_PTR_ADV(p))
               if (c1 == mb_ptr2char(p))
                  break;
            if (*p == ZERO)
                break;
         }
         MB_PTR_ADV(head);
      }
   }

   CS tail = head + STRLEN(head);
   if (dir == 0 || dir == 2) {
      // Trim trailing characters
      CS prev;
      for (; tail > head; tail = prev) {
         prev = tail;
         MB_PTR_BACK(head, prev);
         Unt c1 = mb_ptr2char(prev);
         if (mask == NULL) {
            if (c1 > ' ' && c1 != 0xa0)
                break;
         } else {
            for (p = mask; *p != ZERO; MB_PTR_ADV(p))
               if (c1 == mb_ptr2char(p))
                  break;
            if (*p == ZERO)
               break;
         }
      }
   }
   returnVar->string = copySubstr(head, tail - head);
}

private CS e_printf = e_insufficient_arguments_for_printf;

//Get number argument from "idxp" entry in "tvs".  First entry is 1. Skip the entry.
private Long
tv_nr(Var* tvs, OUT int* idxp) {
   int idx = *idxp - 1;
   Long n = 0;
   Boole err = false;

   if (tvs[idx].tag == VAR_UNKNOWN)
      emsg(_(e_printf));
   else {
      ++*idxp;
      n = varGetNumberChk(tvs + idx, OUT &err);
      if (err)
          n = 0;
   }
   return n;
}

//Get string argument from "idxp" entry in "tvs". First entry is 1.
//If "tofree" is NULL convertVarToStringSingleUse() is used. Some types (e.g. List)
//are not converted to a string.
//If "tofree" is not NULL echo_string() is used. All types are converted to
//a string with the same format as ":echo". The caller must free "*tofree". NULL for an error.
private CS
tv_str(Var* tvs, int* idxp, Byte** tofree) {
   int idx = *idxp - 1;
   CS s = NULL;
   static Byte numBuf[NUMBUFLEN];

   if (tvs[idx].tag == VAR_UNKNOWN)
      emsg(_(e_printf));
   else {
      ++*idxp;
      if (tofree)
         s = echo_string(&tvs[idx], tofree, numBuf, get_copyID());
      else
         s = convertVarToStringSingleUse(&tvs[idx]);
   }
   return s;
}

//Get float argument from "idxp" entry in "tvs".  First entry is 1.
private double
tv_float(Var* tvs, int* idxp) {
   int idx = *idxp - 1;
   double f = 0;

   if (tvs[idx].tag == VAR_UNKNOWN)
      emsg(_(e_printf));
   else {
      ++*idxp;
      if (tvs[idx].tag == VAR_FLOAT)
         f = tvs[idx].floatt;
      ei (tvs[idx].tag == VAR_NUMBER)
         f = (double)tvs[idx].number;
      else
         emsg(_(e_expected_float_argument_for_printf));
   }
   return f;
}

//Return the representation of infinity for printf() function:
//"-inf", "inf", "+inf", " inf", "-INF", "INF", "+INF" or " INF".
private CS
infinity_str(Unt positive, char fmt_spec, int force_sign, int space_for_positive) {
   static CS table[] = {SMAP((CS),
      "-inf", "inf", "+inf", " inf",
      "-INF", "INF", "+INF", " INF"
   )};
   int idx = positive * (1 + force_sign + force_sign * space_for_positive);

   if (ASCII_ISUPPER(fmt_spec))
      idx += 4;
   return table[idx];
}

//This code was included to provide a portable vsnprintf() and snprintf().
//Some systems may provide their own, but we always use this one for consistency.
//
//This code is based on snprintf.c - a portable implementation of snprintf
//by Mark Martinec <mark.martinec@ijs.si>, Version 2.2, 2000-10-06.
//Included with permission.  It was heavily modified to fit in Eegl.
//The original code, including useful comments, can be found here:
//  http://www.ijs.si/software/snprintf/
//
//This snprintf() only supports the following conversion specifiers:
//s, c, d, u, o, x, X, p  (and synonyms: i, D, U, O - see below)
//with flags: '-', '+', ' ', '0' and '#'.
//An asterisk is supported for field width as well as precision.
//
//Limited support for floating point was added: 'f', 'F', 'e', 'E', 'g', 'G'.
//
//Length modifiers 'h' (short int) and 'l' (long int) and 'll' (long long int)
//are supported.  NOTE: for 'll' the argument is Long or Ulong.
//
//The locale is not used, the string is used as a byte string.  This is only
//relevant for double-byte encodings where the second byte may be '%'.
//
//It is permitted for "str_m" to be zero, and it is permitted to specify NULL
//pointer for resulting string argument if "str_m" is zero (as per ISO C99).
//
//The return value is the number of characters which would be generated
//for the given input, excluding the trailing ZERO. If this value
//is greater or equal to "str_m", not all characters from the result
//have been stored in str, output bytes beyond the ("str_m"-1) -th character
//are discarded. If "str_m" is greater than zero it is guaranteed
//the resulting string will be ZERO-terminated.

//When va_list is not supported we only define eeSnprintf().
//
//eeVarPrintf0() can be invoked with either "va_list" or a list of
//"Var".  When the latter is not used it must be NULL.

// When generating prototypes all of this is skipped, cproto doesn't
// understand this.
#ifndef PROTO

// Like eeVsnprintf() but append to the string.
int
eeSnprintfAdd0(CS str, Unt str_m, const char *fmt, ...) {
   va_list   ap;
   int      str_l;
   Unt   len = STRLEN(str);
   Unt   space;

   if (str_m <= len)
      space = 0;
   else
      space = str_m - len;
   va_start(ap, fmt);
   str_l = eeVsnprintf(str + len, space, fmt, ap);
   va_end(ap);
   return str_l;
}

int
eeSnprintf0(CS str, Unt str_m, const char *fmt, ...) {
   va_list   ap;
   va_start(ap, fmt);
   int str_l = eeVsnprintf(str, str_m, fmt, ap);
   va_end(ap);
   return str_l;
}

//Like eeSnprintf() except the return value can be safely used to increment a buffer length.
//Normal `snprintf()` (and `eeSnprintf()`) returns the number of bytes that
//would have been copied if the destination buffer was large enough.
//This means that you cannot rely on it's return value for the destination
//length because the destination may be shorter than the source. This function
//guarantees the returned length will never be greater than the destination length.
Unt
eeSnprintfSafelen0(CS str, Unt str_m, const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
   int str_l = eeVsnprintf(str, str_m, fmt, ap);
   va_end(ap);

   if (str_l < 0) {
      *str = ZERO;
      return 0;
   }
   return ((Unt)str_l >= str_m) ? str_m - 1 : (Unt)str_l;
}

int
eeVsnprintf(CS str, Unt   str_m, char const* fmt, va_list ap) {
   return eeVarPrintf0(str, str_m, fmt, ap, NULL);
}

enum {
   TYPE_UNKNOWN = -1,
   TYPE_INT,
   TYPE_LONGINT,
   TYPE_LONGLONGINT,
   TYPE_UNSIGNEDINT,
   TYPE_UNSIGNEDLONGINT,
   TYPE_UNSIGNEDLONGLONGINT,
   TYPE_POINTER,
   TYPE_PERCENT,
   TYPE_CHAR,
   TYPE_STRING,
   TYPE_FLOAT
};

//Types that can be used in a format string
private int
format_typeof(CS type) {
   // allowed values: \0, h, l, L
   Byte    length_modifier = '\0';

   // current conversion specifier character
   Byte    fmt_spec = '\0';

   // parse 'h', 'l' and 'll' length modifiers
   if (*type == 'h' || *type == 'l') {
      length_modifier = *type;
      type++;
      if (length_modifier == 'l' && *type == 'l') {
         // double l = __int64 / Long
         length_modifier = 'L';
         type++;
      }
   }
   fmt_spec = *type;

   // common synonyms:
   switch (fmt_spec) {
   case 'i': fmt_spec = 'd'; break;
   case '*': fmt_spec = 'd'; length_modifier = 'h'; break;
   case 'D': fmt_spec = 'd'; length_modifier = 'l'; break;
   case 'U': fmt_spec = 'u'; length_modifier = 'l'; break;
   case 'O': fmt_spec = 'o'; length_modifier = 'l'; break;
   default: break;
   }

   // get parameter value, do initial processing
   switch (fmt_spec) {
   // '%' and 'c' behave similar to 's' regarding flags and field
   // widths
   case '%':
      return TYPE_PERCENT;

   case 'c':
      return TYPE_CHAR;

   case 's':
   case 'S':
      return TYPE_STRING;

   case 'd': case 'u':
   case 'b': case 'B':
   case 'o':
   case 'x': case 'X':
   case 'p': {
      // NOTE: the u, b, o, x, X and p conversion specifiers
      // imply the value is unsigned;  d implies a signed value

      // 0 if numeric argument is zero (or if pointer is
      // NULL for 'p'), +1 if greater than zero (or nonzero
      // for unsigned arguments), -1 if negative (unsigned argument is never negative)

      if (fmt_spec == 'p')
         return TYPE_POINTER;
      ei (fmt_spec == 'b' || fmt_spec == 'B')
         return TYPE_UNSIGNEDLONGLONGINT;
      ei (fmt_spec == 'd') {
         // signed
         switch (length_modifier) {
         case '\0':
         case 'h':
            // char and short arguments are passed as int.
            return TYPE_INT;
         case 'l':
            return TYPE_LONGINT;
         case 'L':
            return TYPE_LONGLONGINT;
         }
      } else {
         // unsigned
         switch (length_modifier) {
         case '\0':
         case 'h':
            return TYPE_UNSIGNEDINT;
         case 'l':
            return TYPE_UNSIGNEDLONGINT;
         case 'L':
            return TYPE_UNSIGNEDLONGLONGINT;
         }
      }
   }
   break;

   case 'f':
   case 'F':
   case 'e':
   case 'E':
   case 'g':
   case 'G':
      return TYPE_FLOAT;
   }

   return TYPE_UNKNOWN;
}

private CS
format_typename(CS type) {
   switch (format_typeof(type)) {
   case TYPE_INT: return _("int");
   case TYPE_LONGINT: return _("long int");
   case TYPE_LONGLONGINT: return _("long long int");
   case TYPE_UNSIGNEDINT: return _("unsigned int");
   case TYPE_UNSIGNEDLONGINT: return _("unsigned long int");
   case TYPE_UNSIGNEDLONGLONGINT: return _("unsigned long long int");
   case TYPE_POINTER: return _("pointer");
   case TYPE_PERCENT: return _("percent");
   case TYPE_CHAR: return _("char");
   case TYPE_STRING: return _("string");
   case TYPE_FLOAT: return _("float");
   default: return _("unknown");
   }
}

private int
adjust_types(OUT Byte*** ap_types, int arg, int* num_posarg, CS type) {
   if (*ap_types == NULL || *num_posarg < arg) {
      int       idx;
      Arr(CS) new_types;

      if (*ap_types == NULL)
         new_types = ALLOC_CLEAR_MULT(Byte *, arg);
      else
         new_types = eeRealloc((Byte **)*ap_types, arg * sizeof(Byte *));

      for (idx = *num_posarg; idx < arg; ++idx)
          new_types[idx] = NULL;

      *ap_types = new_types;
      *num_posarg = arg;
   }

   if ((*ap_types)[arg - 1] != NULL) {
      if ((*ap_types)[arg - 1][0] == '*' || type[0] == '*') {
         CS pt = type;
         if (pt[0] == '*')
            pt = (*ap_types)[arg - 1];

         if (pt[0] != '*') {
            switch (pt[0]) {
            case 'd': case 'i': 
               break;
            default:
               showErrFmtMsg(
                     _(e_positional_num_field_spec_reused_str_str), 
                     arg, 
                     format_typename((*ap_types)[arg - 1]), format_typename(type)
               );
               return FAIL;
            }
         }
      } else {
         if (format_typeof(type) != format_typeof((*ap_types)[arg - 1])) {
            showErrFmtMsg(_( e_positional_arg_num_type_inconsistent_str_str), arg, format_typename(type), 
                  format_typename((*ap_types)[arg - 1]));
            return FAIL;
         }
      }
   }

    (*ap_types)[arg - 1] = type;

    return OK;
}

private void
format_overflow_error(CS pstart) {
   Unt   arglen = 0;
   CS p = pstart;

   while (EE_ISDIGIT((int)(*p)))
      ++p;

   arglen = p - pstart;
   CS argcopy = ALLOC_CLEAR_MULT(Byte, arglen + 1);
   if (argcopy) {
      STRNCPY(argcopy, pstart, arglen);
      showErrFmtMsg(_( e_val_too_large), argcopy);
      free(argcopy);
   } else
      showErrFmtMsg(_(e_out_of_memory_allocating_nr_bytes), arglen);
}

#define MAX_ALLOWED_STRING_WIDTH 1048576    // 1 MiB

private int
get_unsigned_int(CS pstart, OUT Byte** p, unsigned int *uj, int overflow_err) {
   *uj = **p - '0';
   ++*p;

   while (EE_ISDIGIT((int)(**p)) && *uj < MAX_ALLOWED_STRING_WIDTH) {
      *uj = 10 * *uj + (unsigned int)(**p - '0');
      (*p)++;
   }

   if (*uj > MAX_ALLOWED_STRING_WIDTH) {
      if (overflow_err) {
          format_overflow_error(pstart);
          return FAIL;
      } else
          *uj = MAX_ALLOWED_STRING_WIDTH;
   }

   return OK;
}


private int
parse_fmt_types(Byte*** ap_types, int* num_posarg, CS fmt, Var* tvs UNUSED) {
   Byte* p = fmt;
   CS arg = NULL;

   int      any_pos = 0;
   int      any_arg = 0;
   int      arg_idx;

#define CHECK_POS_ARG do { \
    if (any_pos && any_arg) \
    { \
   showErrFmtMsg(_( e_cannot_mix_positional_and_non_positional_str), fmt); \
   goto error; \
    } \
} while (0);

   if (p == NULL)
      return OK;

   while (*p != ZERO) {
      if (*p != '%') {
         CS q = STRCHR(p + 1, '%');
         Unt  n = (q == NULL) ? STRLEN(p) : (Unt)(q - p);

         p += n;
      } else {
         // allowed values: \0, h, l, L
         Byte   length_modifier = '\0';

          // variable for positional arg
         int      pos_arg = -1;
         CS ptype = NULL;
         Byte* pstart = p+1;

         p++;  // skip '%'

         // First check to see if we find a positional argument specifier
         ptype = p;

         while (EE_ISDIGIT(*ptype))
            ++ptype;

         if (*ptype == '$') {
            if (*p == '0') {
                // 0 flag at the wrong place
                showErrFmtMsg(_( e_invalid_format_specifier_str), fmt);
                goto error;
            }

            // Positional argument
            unsigned int uj;

            if (get_unsigned_int(pstart, OUT &p, &uj, tvs != NULL) == FAIL)
                goto error;

            pos_arg = uj;

            any_pos = 1;
            CHECK_POS_ARG;

            ++p;
         }

         // parse flags
         while (*p == '0' || *p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '\'') {
            switch (*p) {
            case '0': break;
            case '-': break;
            case '+': break;
            case ' ': // If both the ' ' and '+' flags appear, the ' '
                 // flag should be ignored
                 break;
            case '#': break;
            case '\'': break;
            }
            p++;
         }
         // If the '0' and '-' flags both appear, the '0' flag should be ignored.

         // parse field width
         if (*(arg = p) == '*') {
            p++;

            if (EE_ISDIGIT((int)(*p))) {
               // Positional argument field width
               unsigned int uj;

               if (get_unsigned_int(arg + 1, OUT &p, &uj, tvs != NULL) == FAIL)
                  goto error;

               if (*p != '$') {
                  showErrFmtMsg(_( e_invalid_format_specifier_str), fmt);
                  goto error;
               } else {
                  ++p;
                  any_pos = 1;
                  CHECK_POS_ARG;

                  if (adjust_types(ap_types, uj, num_posarg, arg) == FAIL)
                      goto error;
               }
            } else {
               any_arg = 1;
               CHECK_POS_ARG;
            }
         } ei (EE_ISDIGIT((int)(*p))) {
            // Unt could be wider than unsigned int; make sure we treat
            // argument like common implementations do
            CS digstart = p;
            unsigned int uj;

            if (get_unsigned_int(digstart, OUT &p, &uj, tvs != NULL) == FAIL)
               goto error;

            if (*p == '$') {
               showErrFmtMsg(_( e_invalid_format_specifier_str), fmt);
               goto error;
            }
         }

         // parse precision
         if (*p == '.') {
            p++;

            if (*(arg = p) == '*') {
               p++;

               if (EE_ISDIGIT((int)(*p))) {
                  // Parse precision
                  unsigned int uj;

                  if (get_unsigned_int(arg + 1, OUT &p, &uj, tvs != NULL) == FAIL)
                      goto error;

                  if (*p == '$') {
                      any_pos = 1;
                      CHECK_POS_ARG;

                      ++p;

                      if (adjust_types(ap_types, uj, num_posarg, arg) == FAIL)
                     goto error;
                  } else {
                      showErrFmtMsg(_( e_invalid_format_specifier_str), fmt);
                      goto error;
                  }
               } else {
                  any_arg = 1;
                  CHECK_POS_ARG;
               }
            } ei (EE_ISDIGIT((int)(*p))) {
               // Unt could be wider than unsigned int; make sure we
               // treat argument like common implementations do
               CS digstart = p;
               unsigned int uj;

               if (get_unsigned_int(digstart, OUT &p, &uj, tvs != NULL) == FAIL)
                  goto error;

               if (*p == '$') {
                  showErrFmtMsg(_( e_invalid_format_specifier_str), fmt);
                  goto error;
               }
            }
         }

         if (pos_arg != -1) {
            any_pos = 1;
            CHECK_POS_ARG;

            ptype = p;
         }

         // parse 'h', 'l' and 'll' length modifiers
         if (*p == 'h' || *p == 'l') {
            length_modifier = *p;
            p++;
            if (length_modifier == 'l' && *p == 'l') {
               // double l = __int64 / Long
               // length_modifier = 'L';
               p++;
            }
         }

         switch (*p) {
         // Check for known format specifiers. % is special!
         case 'i':
         case '*':
         case 'd':
         case 'u':
         case 'o':
         case 'D':
         case 'U':
         case 'O':
         case 'x':
         case 'X':
         case 'b':
         case 'B':
         case 'c':
         case 's':
         case 'S':
         case 'p':
         case 'f':
         case 'F':
         case 'e':
         case 'E':
         case 'g':
         case 'G':
            if (pos_arg != -1) {
            if (adjust_types(ap_types, pos_arg, num_posarg, ptype) == FAIL)
                goto error;
            } else {
               any_arg = 1;
               CHECK_POS_ARG;
            }
            break;

         default:
            if (pos_arg != -1) {
               showErrFmtMsg(_( e_cannot_mix_positional_and_non_positional_str), fmt);
               goto error;
            }
         }

         if (*p != ZERO)
            p++;     // step over the just processed conversion specifier
      }
   }

   for (arg_idx = 0; arg_idx < *num_posarg; ++arg_idx) {
      if ((*ap_types)[arg_idx] == NULL) {
         showErrFmtMsg(_(e_fmt_arg_nr_unused_str), arg_idx + 1, fmt);
         goto error;
      }

      if (tvs && tvs[arg_idx].tag == VAR_UNKNOWN) {
         showErrFmtMsg(_(e_positional_nr_out_of_bounds_str), arg_idx + 1, fmt);
         goto error;
      }
   }

   return OK;

error:
   eeglFree((Byte**)*ap_types);
   *ap_types = NULL;
   *num_posarg = 0;
   return FAIL;
}

private void
skip_to_arg(
    Arr(CS) ap_types,
    va_list   ap_start,
    va_list   *ap,
    int      *arg_idx,
    int      *arg_cur,
    CS fmt
) {
   int      arg_min = 0;

   if (*arg_cur + 1 == *arg_idx) {
      ++*arg_cur;
      ++*arg_idx;
      return;
   }

   if (*arg_cur >= *arg_idx) {
      // Reset ap to ap_start and skip arg_idx - 1 types
      va_end(*ap);
      va_copy(*ap, ap_start);
   } else {
      // Skip over any we should skip
      arg_min = *arg_cur;
   }

   for (*arg_cur = arg_min; *arg_cur < *arg_idx - 1; ++*arg_cur) {
      if (ap_types == NULL || ap_types[*arg_cur] == NULL) {
          internalErrFmtMsg(e_aptypes_is_null_nr_str, *arg_cur, fmt);
          return;
      }

      CS p = ap_types[*arg_cur];

      int fmt_type = format_typeof(p);

      // get parameter value, do initial processing
      switch (fmt_type) {
      case TYPE_PERCENT:
      case TYPE_UNKNOWN:
          break;

      case TYPE_CHAR:
          va_arg(*ap, int);
          break;

      case TYPE_STRING:
          va_arg(*ap, char *);
          break;

      case TYPE_POINTER:
          va_arg(*ap, void *);
          break;

      case TYPE_INT:
          va_arg(*ap, int);
          break;

      case TYPE_LONGINT:
          va_arg(*ap, long int);
          break;

      case TYPE_LONGLONGINT:
          va_arg(*ap, Long);
          break;

      case TYPE_UNSIGNEDINT:
          va_arg(*ap, unsigned int);
          break;

      case TYPE_UNSIGNEDLONGINT:
          va_arg(*ap, unsigned long int);
          break;

      case TYPE_UNSIGNEDLONGLONGINT:
          va_arg(*ap, Ulong);
          break;

      case TYPE_FLOAT:
          va_arg(*ap, double);
          break;
      }
   }

   //Because we know that after we return from this call, a va_arg() call is made, we can 
   //pre-emptively increment the current argument index.
   ++*arg_cur;
   ++*arg_idx;

   return;
}

int
eeVarPrintf0(
   CS str,
   Unt str_m,
   char const* fmt,
   va_list ap_start,
   Var* tvs
) {
   Unt   str_l = 0; // number of formatted characters. That is, the number of characters that 
                    // would have been written to the string buf if it were large enough.

   CS p = (CS)fmt;
   int      arg_cur = 0;
   int      num_posarg = 0;
   int      arg_idx = 1;
   va_list   ap;
   Byte** ap_types = NULL;

   if (parse_fmt_types(&ap_types, &num_posarg, (CS)fmt, tvs) == FAIL)
      return 0;

   va_copy(ap, ap_start);

   if (!p)
      p = S"";
   while (*p != ZERO) {
      if (*p != '%') {
         CS q = STRCHR(p + 1, '%');
         Unt  n = (q == NULL) ? STRLEN(p) : (Unt)(q - p);

         // Copy up to the next '%' or ZERO without any changes.
         if (str_l < str_m) {
            Unt avail = str_m - str_l;
            mch_memmove(str + str_l, p, n > avail ? avail : n);
         }
         p += n;
         str_l += n;
      } else {
         Unt  min_field_width = 0, precision = 0;
         int       zero_padding = 0, precision_specified = 0, justify_left = 0;
         int       alternate_form = 0, force_sign = 0;

         // If both the ' ' and '+' flags appear, the ' ' flag should be ignored.
         int space_for_positive = 1;

         // allowed values: \0, h, l, L
         char length_modifier = '\0';

         // temporary buffer for simple numeric->string conversion
# define TMP_LEN 350   // On my system 1e308 is the biggest number possible.
         // That sounds reasonable to use as the maximum printable.
         Byte tmp[TMP_LEN];

         // string address in case of string argument
         CS str_arg = NULL;

         // natural field width of arg without padding and sign
         Unt  str_arg_l;

         // unsigned char argument value - only defined for c conversion.
         // N.B. standard explicitly states the char argument for the c conversion is unsigned
         unsigned char uchar_arg;

         // number of zeros to be inserted for numeric conversions as
         // required by the precision or minimal field width
         Unt  number_of_zeros_to_pad = 0;

         // index into tmp where zero padding is to be inserted
         Unt  zero_padding_insertion_ind = 0;

         // current conversion specifier character
         char    fmt_spec = '\0';

         // buffer for 's' and 'S' specs
         Byte  *tofree = NULL;

         // variables for positional arg
         int       pos_arg = -1;
         CS ptype;


         p++;  // skip '%'

         // First check to see if we find a positional argument specifier
         ptype = p;

         while (EE_ISDIGIT(*ptype))
            ++ptype;

         if (*ptype == '$') {
            // Positional argument
            CS digstart = p;
            unsigned int uj;

            if (get_unsigned_int(digstart, OUT &p, &uj, tvs != NULL) == FAIL)
               goto error;

            pos_arg = uj;

            ++p;
         }

         // parse flags
         while (*p == '0' || *p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '\'') {
            switch (*p) {
            case '0': zero_padding = 1; break;
            case '-': justify_left = 1; break;
            case '+': force_sign = 1; space_for_positive = 0; break;
            case ' ': force_sign = 1;
               // If both the ' ' and '+' flags appear, the ' ' flag should be ignored
               break;
            case '#': alternate_form = 1; break;
            case '\'': break;
            }
            p++;
         }
         // If the '0' and '-' flags both appear, the '0' flag should be ignored.

         // parse field width
         if (*p == '*') {
            int j;
            CS digstart = p + 1;

            p++;

            if (EE_ISDIGIT((int)(*p))) {
               // Positional argument field width
               unsigned int uj;

               if (get_unsigned_int(digstart, OUT &p, &uj, tvs != NULL) == FAIL)
                  goto error;

               arg_idx = uj;

               ++p;
            }

            j = tvs
               ? tv_nr(tvs, OUT &arg_idx) 
               : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt), 
                   va_arg(ap, int)
                 );

            if (j > MAX_ALLOWED_STRING_WIDTH) {
               if (tvs) {
                  format_overflow_error(digstart);
                  goto error;
               } else
                  j = MAX_ALLOWED_STRING_WIDTH;
            }

            if (j >= 0)
               min_field_width = j;
            else {
               min_field_width = -j;
               justify_left = 1;
            }
         } ei (EE_ISDIGIT((int)(*p))) {
            // Unt could be wider than unsigned int; make sure we treat
            // argument like common implementations do
            CS digstart = p;
            unsigned int uj;

            if (get_unsigned_int(digstart, OUT &p, &uj, tvs != NULL) == FAIL)
               goto error;

            min_field_width = uj;
         }

         // parse precision
         if (*p == '.') {
            p++;
            precision_specified = 1;

            if (EE_ISDIGIT((int)(*p))) {
               // Unt could be wider than unsigned int; make sure we
               // treat argument like common implementations do
               CS digstart = p;
               unsigned int uj;

               if (get_unsigned_int(digstart, OUT &p, &uj, tvs != NULL) == FAIL)
                  goto error;

               precision = uj;
            } ei (*p == '*') {
               int j;
               CS digstart = p;

               p++;

               if (EE_ISDIGIT((int)(*p))) {
                  // positional argument
                  unsigned int uj;

                  if (get_unsigned_int(digstart, OUT &p, &uj, tvs != NULL) == FAIL)
                     goto error;

                  arg_idx = uj;

                  ++p;
               }

               j = tvs 
                  ? tv_nr(tvs, OUT &arg_idx) 
                  : (skip_to_arg( ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt), 
                     va_arg(ap, int)
                    );

               if (j > MAX_ALLOWED_STRING_WIDTH) {
                  if (tvs) {
                     format_overflow_error(digstart);
                     goto error;
                  } else
                     j = MAX_ALLOWED_STRING_WIDTH;
               }

               if (j >= 0)
                  precision = j;
               else {
                  precision_specified = 0;
                  precision = 0;
               }
            }
         }

         // parse 'h', 'l' and 'll' length modifiers
         if (*p == 'h' || *p == 'l') {
            length_modifier = *p;
            p++;
            if (length_modifier == 'l' && *p == 'l') {
                // double l = __int64 / Long
                length_modifier = 'L';
                p++;
            }
         }
         fmt_spec = *p;

         // common synonyms:
         switch (fmt_spec) {
         case 'i': fmt_spec = 'd'; break;
         case 'D': fmt_spec = 'd'; length_modifier = 'l'; break;
         case 'U': fmt_spec = 'u'; length_modifier = 'l'; break;
         case 'O': fmt_spec = 'o'; length_modifier = 'l'; break;
         default: break;
         }

         switch (fmt_spec) {
         case 'd': case 'u': case 'o': case 'x': case 'X':
            if (tvs != NULL && length_modifier == '\0')
               length_modifier = 'L';
         }

         if (pos_arg != -1)
            arg_idx = pos_arg;

         // get parameter value, do initial processing
         switch (fmt_spec) {
         // '%' and 'c' behave similar to 's' regarding flags and field widths
         case '%':
         case 'c':
         case 's':
         case 'S':
            str_arg_l = 1;
            switch (fmt_spec) {
            case '%':
                str_arg = p;
                break;

            case 'c': {
               int j;

               j = tvs 
                  ? tv_nr(tvs, OUT &arg_idx) 
                  : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt),
                        va_arg(ap, int)
                    );

               // standard demands unsigned char
               uchar_arg = (unsigned char)j;
               str_arg = &uchar_arg;
               break;
               }

            case 's':
            case 'S':
               str_arg = tvs 
                  ? tv_str(tvs, &arg_idx, &tofree) 
                  : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt),
                    va_arg(ap, Byte *)
                    );

               if (str_arg == NULL) {
                  str_arg = S"[NULL]";
                  str_arg_l = 6;
               }
               // make sure not to address string beyond the specified precision !!!
               ei (!precision_specified)
                  str_arg_l = STRLEN(str_arg);
               // truncate string if necessary as requested by precision
               ei (precision == 0)
                  str_arg_l = 0;
               else {
                  CS q = memchr(str_arg, '\0',
                       precision <= (Unt)0x7fffffffL ? precision
                                  : (Unt)0x7fffffffL);

                  str_arg_l = (q == NULL) ? precision : (Unt)(q - str_arg);
               }
               if (fmt_spec == 'S') {
                  Byte   *p1;
                  Unt   i;
                  int   cell;

                  for (i = 0, p1 = (CS)str_arg; *p1; p1 += utfCharLen(p1)) {
                     cell = mb_ptr2cells(p1);
                     if (precision_specified && i + cell > precision)
                        break;
                     i += cell;
                  }

                  str_arg_l = p1 - (CS)str_arg;
                  if (min_field_width != 0)
                     min_field_width += str_arg_l - i;
                }
                break;

            default:
                break;
            }
         break;

         case 'd': case 'u':
         case 'b': case 'B':
         case 'o':
         case 'x': case 'X':
         case 'p': {
            //NOTE: the u, b, o, x, X and p conversion specifiers
            //imply the value is unsigned;  d implies a signed value

            //0 if numeric argument is zero (or if pointer is NULL for 'p'), +1 if greater than 
            //zero (or nonzero for unsigned arguments), -1 if negative (unsigned argument is 
            //never negative)
            int arg_sign = 0;

            //only set for length modifier h, or for no length modifiers
            int int_arg = 0;
            Unt uint_arg = 0;

            //only set for length modifier l
            long int long_arg = 0;
            unsigned long int ulong_arg = 0;

            //only set for length modifier ll
            Long llong_arg = 0;
            Ulong ullong_arg = 0;

            //only set for b conversion
            Ulong bin_arg = 0;

            //pointer argument value -only defined for p conversion
            void *ptr_arg = NULL;

            if (fmt_spec == 'p') {
               length_modifier = '\0';
               ptr_arg = tvs
                  ? (void *)tv_str(tvs, &arg_idx, NULL) 
                  : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt),
                     va_arg(ap, void *)
                    );

               if (ptr_arg != NULL)
                   arg_sign = 1;
            } ei (fmt_spec == 'b' || fmt_spec == 'B') {
               bin_arg = tvs 
                  ? (Ulong)tv_nr(tvs, OUT &arg_idx) 
                  : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt),
                     va_arg(ap, Ulong)
                    );

               if (bin_arg != 0)
                   arg_sign = 1;
            } ei (fmt_spec == 'd') {
               // signed
               switch (length_modifier) {
               case '\0':
               case 'h':
                   // char and short arguments are passed as int.
                   int_arg = tvs
                      ? tv_nr(tvs, OUT &arg_idx) 
                      : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt),
                         va_arg(ap, int)
                        );

                   if (int_arg > 0)
                  arg_sign =  1;
                   ei (int_arg < 0)
                  arg_sign = -1;
                   break;
               case 'l':
                   long_arg = tvs 
                      ? tv_nr(tvs, OUT &arg_idx) 
                      : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt),
                         va_arg(ap, long int)
                        );

                   if (long_arg > 0)
                  arg_sign =  1;
                   ei (long_arg < 0)
                  arg_sign = -1;
                   break;
               case 'L':
                   llong_arg = tvs
                      ? tv_nr(tvs, OUT &arg_idx) 
                      : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt),
                         va_arg(ap, Long)
                         );

                   if (llong_arg > 0)
                  arg_sign =  1;
                   ei (llong_arg < 0)
                  arg_sign = -1;
                   break;
               }
            } else {
               // unsigned
               switch (length_modifier) {
               case '\0':
               case 'h':
                  uint_arg = tvs
                     ? (unsigned)tv_nr(tvs, OUT &arg_idx) 
                     : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt),
                        va_arg(ap, unsigned int)
                       );

                  if (uint_arg != 0)
                      arg_sign = 1;
                  break;
               case 'l':
                  ulong_arg = tvs
                     ? (unsigned long) tv_nr(tvs, OUT &arg_idx) 
                     : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt),
                         va_arg(ap, unsigned long int)
                       );

                  if (ulong_arg != 0)
                      arg_sign = 1;
                  break;
               case 'L':
                  ullong_arg = tvs
                     ? (Ulong) tv_nr(tvs, OUT &arg_idx) 
                     : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt),
                        va_arg(ap, Ulong)
                       );

                  if (ullong_arg != 0)
                      arg_sign = 1;
                  break;
               }
            }

            str_arg = tmp;
            str_arg_l = 0;

            // NOTE:
            //   For d, i, u, o, x, and X conversions, if precision is
            //   specified, the '0' flag should be ignored.
            if (precision_specified)
               zero_padding = 0;
            if (fmt_spec == 'd') {
               if (force_sign && arg_sign >= 0)
                   tmp[str_arg_l++] = space_for_positive ? ' ' : '+';
               // leave negative numbers for sprintf to handle, to
               // avoid handling tricky cases like (short int)-32768
            } ei (alternate_form) {
               if (arg_sign != 0
                       && (fmt_spec == 'b' || fmt_spec == 'B'
                        || fmt_spec == 'x' || fmt_spec == 'X') )
               {
                   tmp[str_arg_l++] = '0';
                   tmp[str_arg_l++] = fmt_spec;
               }
               // alternate form should have no effect for p conversion, but ...
            }

            zero_padding_insertion_ind = str_arg_l;
            if (!precision_specified)
               precision = 1;   // default precision is 1
            if (precision == 0 && arg_sign == 0) {
               // When zero value is formatted with an explicit
               // precision 0, the resulting formatted string is
               // empty (d, i, u, b, B, o, x, X, p).
            } else {
               char   f[6];
               int   f_l = 0;

               // construct a simple format string for sprintf
               f[f_l++] = '%';
               if (!length_modifier)
                   ;
               ei (length_modifier == 'L') {
                   f[f_l++] = 'l';
                   f[f_l++] = 'l';
               }
               else
                   f[f_l++] = length_modifier;
               f[f_l++] = fmt_spec;
               f[f_l++] = '\0';

               if (fmt_spec == 'p')
                  str_arg_l += SPRINTF(tmp + str_arg_l, f, ptr_arg);
               ei (fmt_spec == 'b' || fmt_spec == 'B') {
                  Byte       b[8 * sizeof(Ulong)];
                  Unt       b_l = 0;
                  Ulong    bn = bin_arg;

                  do {
                     b[sizeof(b) - ++b_l] = '0' + (bn & 0x1);
                     bn >>= 1;
                  } while (bn != 0);

                  memcpy(tmp + str_arg_l, b + sizeof(b) - b_l, b_l);
                  str_arg_l += b_l;
               }
               ei (fmt_spec == 'd') {
                   // signed
                   switch (length_modifier) {
                   case '\0': str_arg_l += SPRINTF(tmp + str_arg_l, f, int_arg); break;
                   case 'h': str_arg_l += SPRINTF(tmp + str_arg_l, f, (Short)int_arg); break;
                   case 'l': str_arg_l += SPRINTF(tmp + str_arg_l, f, long_arg); break;
                   case 'L': str_arg_l += SPRINTF(tmp + str_arg_l, f, llong_arg); break;
                   }
               } else {
                  // unsigned
                  switch (length_modifier) {
                  case '\0': str_arg_l += SPRINTF(tmp + str_arg_l, f, uint_arg); break;
                  case 'h': str_arg_l += SPRINTF( tmp + str_arg_l, f, (Short)uint_arg); break;
                  case 'l': str_arg_l += SPRINTF(tmp + str_arg_l, f, ulong_arg); break;
                  case 'L': str_arg_l += SPRINTF(tmp + str_arg_l, f, ullong_arg); break;
                  }
               }

               // include the optional minus sign and possible "0x" in the region before the zero 
               // padding insertion point
               if (zero_padding_insertion_ind < str_arg_l && tmp[zero_padding_insertion_ind] == '-')
                  zero_padding_insertion_ind++;
               if (zero_padding_insertion_ind + 1 < str_arg_l
                     && tmp[zero_padding_insertion_ind]   == '0'
                     && (tmp[zero_padding_insertion_ind + 1] == 'x'
                         || tmp[zero_padding_insertion_ind + 1] == 'X')
               )
                  zero_padding_insertion_ind += 2;
            }

            Unt num_of_digits = str_arg_l - zero_padding_insertion_ind;

            // zero padding to specified precision?
            if (num_of_digits < precision)
                number_of_zeros_to_pad = precision - num_of_digits;
             // zero padding to specified minimal field width?
            if (!justify_left && zero_padding) {
               int n = (int)(min_field_width - (str_arg_l + number_of_zeros_to_pad));
               if (n > 0)
                  number_of_zeros_to_pad += n;
            }
            break;
         }

         case 'f':
         case 'F':
         case 'e':
         case 'E':
         case 'g':
         case 'G': {
            // Floating point.
            Byte format[40];
            int      l;
            int      remove_trailing_zeroes = false;

            double f = tvs
               ? tv_float(tvs, &arg_idx) 
               : (skip_to_arg(ap_types, ap_start, &ap, &arg_idx, &arg_cur, (CS)fmt), 
                  va_arg(ap, double)
                 );

            double abs_f = f < 0 ? -f : f;

            if (fmt_spec == 'g' || fmt_spec == 'G') {
               // Would be nice to use %g directly, but it prints
               // "1.0" as "1", we don't want that.
               if ((abs_f >= 0.001 && abs_f < 10000000.0)
                                 || abs_f == 0.0)
                   fmt_spec = ASCII_ISUPPER(fmt_spec) ? 'F' : 'f';
               else
                   fmt_spec = fmt_spec == 'g' ? 'e' : 'E';
               remove_trailing_zeroes = true;
            }

            if ((fmt_spec == 'f' || fmt_spec == 'F') &&
# ifdef VAX
                abs_f > 1.0e38
# else
                abs_f > 1.0e307
# endif
            ) {
               // Avoid a buffer overflow
               STRCPY(tmp, infinity_str(f > 0.0, fmt_spec, force_sign, space_for_positive));
               str_arg_l = STRLEN(tmp);
               zero_padding = 0;
            } else {
               if (isnan(f)) {
                  // Not a number: nan or NAN
                  STRCPY(tmp, ASCII_ISUPPER(fmt_spec) ? "NAN" : "nan");
                  str_arg_l = 3;
                  zero_padding = 0;
               } ei (isinf(f)) {
                  STRCPY(tmp, infinity_str(f > 0.0, fmt_spec, force_sign, space_for_positive));
                  str_arg_l = STRLEN(tmp);
                  zero_padding = 0;
               } else {
                  // Regular float number
                  format[0] = '%';
                  l = 1;
                  if (force_sign)
                     format[l++] = space_for_positive ? ' ' : '+';
                  if (precision_specified) {
                     Unt max_prec = TMP_LEN - 10;

                     // Make sure we don't get more digits than we
                     // have room for.
                     if ((fmt_spec == 'f' || fmt_spec == 'F') && abs_f > 1.0)
                        max_prec -= (Unt)log10(abs_f);
                     if (precision > max_prec)
                        precision = max_prec;
                     l += SPRINTF(format + l, ".%d", (int)precision);
                  }
                  format[l] = fmt_spec == 'F' ? 'f' : fmt_spec;
                  format[l + 1] = ZERO;

                  str_arg_l = SPRINTF(tmp, format, f);
               }

               if (remove_trailing_zeroes) {
                  int i;
                  CS tp;

                  // Using %g or %G: remove superfluous zeroes.
                  if (fmt_spec == 'f' || fmt_spec == 'F')
                     tp = tmp + str_arg_l - 1;
                  else {
                     tp = firstOccurrence((CS)tmp, fmt_spec == 'e' ? 'e' : 'E');
                     if (tp) {
                        // Remove superfluous '+' and leading zeroes from the exponent.
                        if (tp[1] == '+') {
                           // Change "1.0e+07" to "1.0e07"
                           STRMOVE(tp + 1, tp + 2);
                           --str_arg_l;
                        }
                        i = (tp[1] == '-') ? 2 : 1;
                        while (tp[i] == '0') {
                           // Change "1.0e07" to "1.0e7"
                           STRMOVE(tp + i, tp + i + 1);
                           --str_arg_l;
                        }
                        --tp;
                     }
                  }

                  if (tp && !precision_specified) {
                     // Remove trailing zeroes, but keep the one just after a dot.
                     while (tp > tmp + 2 && *tp == '0' && tp[-1] != '.') {
                         STRMOVE(tp, tp + 1);
                         --tp;
                         --str_arg_l;
                     }
                  } 
               } else {
                  // Be consistent: some printf("%e") use 1.0e+12
                  // and some 1.0e+012.  Remove one zero in the last case.
                  CS tp = firstOccurrence((CS)tmp, fmt_spec == 'e' ? 'e' : 'E');
                  if (tp != NULL && (tp[1] == '+' || tp[1] == '-')
                       && tp[2] == '0'
                       && eeIsDigit(tp[3])
                       && eeIsDigit(tp[4])
                  ) {
                     STRMOVE(tp + 2, tp + 3);
                     --str_arg_l;
                  }
               }
            }
            if (zero_padding && min_field_width > str_arg_l && (tmp[0] == '-' || force_sign)) {
               // padding 0's should be inserted after the sign
               number_of_zeros_to_pad = min_field_width - str_arg_l;
               zero_padding_insertion_ind = 1;
            }
            str_arg = tmp;
            break;
         }

         default:
            // unrecognized conversion specifier, keep format string as-is
            zero_padding = 0;  // turn zero padding off for non-numeric conversion
            justify_left = 1;
            min_field_width = 0;          // reset flags

            // discard the unrecognized conversion, just keep *
            // the unrecognized conversion character
            str_arg = p;
            str_arg_l = 0;
            if (*p != ZERO)
                str_arg_l++;  // include invalid conversion specifier
                    // unchanged if not at end-of-string
            break;
         }

         if (*p != ZERO)
            p++;     // step over the just processed conversion specifier

         // insert padding to the left as requested by min_field_width;
         // this does not include the zero padding in case of numerical conversions
         if (!justify_left) {
            // left padding with blank or zero
            int pn = (int)(min_field_width - (str_arg_l + number_of_zeros_to_pad));

            if (pn > 0) {
               if (str_l < str_m) {
                  Unt avail = str_m - str_l;
                  memset(
                     str + str_l, zero_padding ? '0' : ' ', (Unt)pn > avail ? avail : (Unt)pn
                  );
               }
               str_l += pn;
            }
         }

         //zero padding as requested by the precision or by the minimal
         //field width for numeric conversions required?
         if (number_of_zeros_to_pad == 0) {
            //will not copy first part of numeric right now, *
            //force it to be copied later in its entirety
            zero_padding_insertion_ind = 0;
         } else {
            // insert first part of numerics (sign or '0x') before zero padding
            int zn = (int)zero_padding_insertion_ind;

            if (zn > 0) {
               if (str_l < str_m) {
                  Unt avail = str_m - str_l;
                  mch_memmove(str + str_l, str_arg, (Unt)zn > avail ? avail : (Unt)zn);
               }
               str_l += zn;
            }

            // insert zero padding as requested by the precision or min field width
            zn = (int)number_of_zeros_to_pad;
            if (zn > 0) {
               if (str_l < str_m) {
                  Unt avail = str_m - str_l;
                  memset(str + str_l, '0', (Unt)zn > avail ? avail : (Unt)zn);
               }
               str_l += zn;
            }
         }

         // insert formatted string
         // (or as-is conversion specifier for unknown conversions)
         {
         int sn = (int)(str_arg_l - zero_padding_insertion_ind);

         if (sn > 0) {
            if (str_l < str_m) {
               Unt avail = str_m - str_l;
               mch_memmove(
                  str + str_l, str_arg + zero_padding_insertion_ind, 
                  (Unt)sn > avail ? avail : (Unt)sn
               );
            }
            str_l += sn;
         }
         }

         // insert right padding
         if (justify_left) {
            // right blank padding to the field width
            int pn = (int)(min_field_width - (str_arg_l + number_of_zeros_to_pad));

            if (pn > 0) {
               if (str_l < str_m) {
                  Unt avail = str_m - str_l;

                  memset(str + str_l, ' ', (Unt)pn > avail ? avail : (Unt)pn);
               }
               str_l += pn;
            }
         }
         eeglFree(tofree);
      }
    }

   if (str_m > 0) {
      // make sure the string is ZERO-terminated even at the expense of
      // overwriting the last character (shouldn't happen, but just in case)
      //
      str[str_l <= str_m - 1 ? str_l : str_m - 1] = '\0';
   }

   if (tvs != NULL && tvs[num_posarg != 0 ? num_posarg : arg_idx - 1].tag != VAR_UNKNOWN)
      emsg(_(e_too_many_arguments_to_printf));

error:
   eeglFree((Byte*)ap_types);
   va_end(ap);

   //Return the number of characters formatted (excluding trailing ZERO
   //character), that is, the number of characters that would have been
   //written to the buffer if it were large enough.
   return str_l;
}

#endif // PROTO

//Get the byte index for character index "idx" in string "str" with length
//"str_len".  Composing characters are included.
//If going over the end return "str_len".
//If "idx" is negative count from the end, -1 is the last character.
//When going over the start return -1.
long
char_idx2byte(CS str, Unt str_len, Long idx) {
   Long nchar = idx;
   Unt   nbyte = 0;

   if (nchar >= 0) {
      while (nchar > 0 && nbyte < str_len) {
          nbyte += utfCharLen(str + nbyte);
          --nchar;
      }
   } else {
      nbyte = str_len;
      while (nchar < 0 && nbyte > 0) {
         --nbyte;
         nbyte -= mb_head_off(str, str + nbyte);
         ++nchar;
      }
      if (nchar < 0)
         return -1;
   }
   return (long)nbyte;
}

CS
concatStrArray(Arr(CS) arr, Unt len, Text separator, Arena* a) {
   Unt totalLen = 0;
   for (Unt i = 0; i < len; i++) {
      totalLen += STRLEN(arr[i]);
   }
   totalLen += (len - 1)*separator.len;
   CS result = allocateArray(totalLen + 1, Byte, a);
   result[totalLen] = ZERO;

   Unt j = 0;
   for (Unt i = 0; i < len; i++) {
      Unt elemLen = STRLEN(arr[i]);
      memcpy(result + j, arr[i], elemLen);
      j += elemLen;
      memcpy(result + j, separator.c, separator.len);
      j += separator.len;
   }
   return result;
}

#define MAX_EXTENSION_CHECKING_LEN 12

//Return pointer into string where the file extension starts. If no dot is found in the last
//12 bytes, then return the whole string. Precondition: file name must be non-null
CS
fileExtension(Text fName) {
   CS c = fName.c + fName.len - 1;
   int i = 0;
   for (; *c != '.' && i < MAX_EXTENSION_CHECKING_LEN; i++, c--)
      {}  
   if (*c == '.' && i > 0) {
      return c + 1;
   } else {
      return fName.c;
   }
}

// Does longerStr start with shorterStr?
Boole
startsWith(CS longerStr, CS shorterStr) {
   CS l = longerStr;
   CS s = shorterStr;
   for(; *l == *s && *l != ZERO && s != ZERO; l++, s++)
      {}
   return (*s == ZERO);
}

//}}}
//{{{fuzzy searchin'

//Fuzzy matching algorithm and related functions
//
//Portions of this file are adapted from fzy (https://github.com/jhawthorn/fzy)
//Original code:
//  Copyright (c) 2014 John Hawthorn
//  Licensed under the MIT License.
//
//Permission is hereby granted, free of charge, to any person obtaining a copy
//of this software and associated documentation files (the "Software"), to deal
//in the Software without restriction, including without limitation the rights
//to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//copies of the Software, and to permit persons to whom the Software is
//furnished to do so, subject to the following conditions:
//
//The above copyright notice and this permission notice shall be included in
//all copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//THE SOFTWARE.

private int fuzzy_match_item_compare(const void *s1, const void *s2);
private void fuzzy_match_in_list(
      List *l, Byte *str, int matchseq, Byte *key, Callback *item_cb, int retmatchpos, 
      List *fmatchlist, long max_matches
);
private void do_fuzzymatch(Arr(Var) argvars, OUT Var* returnVar, int retmatchpos);
private int fuzzyMatchStr_compare(const void *s1, const void *s2);
private int fuzzy_match_func_compare(const void *s1, const void *s2);
private void sortFnNamesByScore(Arr(FuzzyMatch) fm, int sz);

private double match_positions(CS needle, CS haystack, Unt* positions);
private int has_match(CS needle, CS haystack);

#define SCORE_MAX INFINITY
#define SCORE_MIN (-INFINITY)
#define SCORE_SCALE 1000

typedef struct {
   int      idx;      // used for stable sort
   ListItem* item;
   int score;
   List* lmatchpos;
   CS pat;
   CS itemstr;
   int itemstr_allocated;
   int startpos;
} FuzzyItem;

void
addFuzzyMatch(FuzzyMatch m, OUT Fuzzy* t) {
   if (t->len == t->cap) {
      Arr(FuzzyMatch) newContent = allocateArray(t->cap*2, FuzzyMatch, t->a);
      if (t->len > 0)
         memcpy(newContent, t->c, t->len*sizeof(FuzzyMatch));
      t->c = newContent;
      t->cap *= 2;
   }
   m.idx = t->len;
   t->c[t->len] = m;
   t->len++;
}

//Return true if "pat_arg" matches "str". Also returns the match score in
//"outScore" and the matching character positions in "matches".
int
fuzzy_match(
   CS str,
   CS pat_arg,
   int matchseq,
   int* outScore,
   Arr(Unt) matches,
   int maxMatches
) {
   int complete = false;
   int score = 0;
   int numMatches = 0;
   double fzy_score;

   *outScore = 0;

   CS save_pat = copyStr(pat_arg);
   CS pat = save_pat;
   CS p = pat;

   // Try matching each word in 'pat_arg' in 'str'
   while (true) {
      if (matchseq)
         complete = true;
      else {
         // Extract one word from the pattern (separated by space)
         p = skipwhite(p);
         if (*p == ZERO)
            break;
         pat = p;
         while (*p != ZERO && !SPACE_OR_TAB(mb_ptr2char(p))) {
            MB_PTR_ADV(p);
         }
         if (*p == ZERO)      // processed all the words
            complete = true;
         *p = ZERO;
      }

      score = FUZZY_SCORE_NONE;
      if (has_match(pat, str)) {
         fzy_score = match_positions(pat, str, matches + numMatches);
         score = (fzy_score == SCORE_MIN) ? INT_MIN + 1
            : (fzy_score == SCORE_MAX) ? INT_MAX
            : (fzy_score < 0) ? (int)ceil(fzy_score * SCORE_SCALE - 0.5)
            : (int)floor(fzy_score * SCORE_SCALE + 0.5);
      }

      if (score == FUZZY_SCORE_NONE) {
         numMatches = 0;
         *outScore = FUZZY_SCORE_NONE;
         break;
      }

      if (score > 0 && *outScore > INT_MAX - score)
         *outScore = INT_MAX;
      ei (score < 0 && *outScore < INT_MIN + 1 - score)
         *outScore = INT_MIN + 1;
      else
         *outScore += score;

      numMatches += MB_CHARLEN(pat);

      if (complete || numMatches >= maxMatches)
          break;

      // try matching the next word
      ++p;
   }

   eeglFree(save_pat);
   return numMatches != 0;
}

//Sort the fuzzy matches in the descending order of the match score.
//For items with same score, retain the order using the index (stable sort)
private int
fuzzy_match_item_compare(const void *s1, const void *s2) {
   int v1 = ((FuzzyItem *)s1)->score;
   int v2 = ((FuzzyItem *)s2)->score;

   if (v1 == v2) {
      int exact_match1 = false, exact_match2 = false;
      CS pat = ((FuzzyItem *)s1)->pat;
      int patlen = (int)STRLEN(pat);
      int startpos = ((FuzzyItem *)s1)->startpos;
      exact_match1 = (startpos >= 0) && STRNCMP(pat,
         ((FuzzyItem *)s1)->itemstr + startpos, patlen) == 0;
      startpos = ((FuzzyItem *)s2)->startpos;
      exact_match2 = (startpos >= 0) && STRNCMP(pat,
         ((FuzzyItem *)s2)->itemstr + startpos, patlen) == 0;

      if (exact_match1 == exact_match2) {
         int idx1 = ((FuzzyItem *)s1)->idx;
         int idx2 = ((FuzzyItem *)s2)->idx;
         return idx1 == idx2 ? 0 : idx1 > idx2 ? 1 : -1;
      } ei (exact_match2)
         return 1;
      return -1;
   } else
      return v1 > v2 ? -1 : 1;
}

//Fuzzy search the string 'str' in a list of 'items' and return the matching
//strings in 'fmatchlist'.
//If 'matchseq' is true, then for multi-word search strings, match all the words in sequence.
//If 'items' is a list of strings, then search for 'str' in the list.
//If 'items' is a list of dicts, then either use 'key' to lookup the string
//for each item or use 'item_cb' Funcref function to get the string.
//If 'retmatchpos' is true, then return a list of positions where 'str' matches for each item.
private void
fuzzy_match_in_list(
   List* l,
   CS str,
   int matchseq,
   CS key,
   Callback* item_cb,
   int retmatchpos,
   List* fmatchlist,
   long max_matches
) {
   long match_count = 0;
   Unt matches[FUZZY_MATCH_MAX_LEN];

   long len = list_len(l);
   if (len == 0)
      return;
   if (max_matches > 0 && len > max_matches)
      len = max_matches;

   Arr(FuzzyItem) items = ALLOC_CLEAR_MULT(FuzzyItem, len);
   if (items == NULL)
      return;

   // For all the string items in items, get the fuzzy matching score
   ListItem* li;
   FOR_ALL_LIST_ITEMS(l, li) {
      int      score;
      Byte      *itemstr;
      Var   returnVar;
      int      itemstr_allocate = false;

      if (max_matches > 0 && match_count >= max_matches)
          break;

      itemstr = NULL;
      returnVar.tag = VAR_UNKNOWN;
      if (li->c.tag == VAR_STRING)   // list of strings
         itemstr = li->c.string;
      ei (li->c.tag == VAR_BAG && (key != NULL || item_cb->name != NULL)) {
         // For a dict, either use the specified key to lookup the string or
         // use the specified callback function to get the string.
         if (key)
            itemstr = bagGetString(li->c.bag, text(key), false);
         else {
            Var   argv[2];

            // Invoke the supplied callback (if any) to get the dict item
            li->c.bag->refcount++;
            argv[0].tag = VAR_BAG;
            argv[0].bag = li->c.bag;
            argv[1].tag = VAR_UNKNOWN;
            if (call_callback(item_cb, -1, &returnVar, 1, argv) != FAIL) {
               if (returnVar.tag == VAR_STRING) {
                  itemstr = returnVar.string;
                  itemstr_allocate = true;
               }
            }
            bagUnref(li->c.bag);
         }
      }

      if (itemstr != NULL
         && fuzzy_match(itemstr, str, matchseq, &score, matches, FUZZY_MATCH_MAX_LEN)
      ){
         items[match_count].idx = match_count;
         items[match_count].item = li;
         items[match_count].score = score;
         items[match_count].pat = str;
         items[match_count].startpos = matches[0];
         items[match_count].itemstr = itemstr_allocate ? copyStr(itemstr) : itemstr;
         items[match_count].itemstr_allocated = itemstr_allocate;

         // Copy the list of matching positions in itemstr to a list, if "retmatchpos" is set.
         if (retmatchpos) {
            items[match_count].lmatchpos = list_alloc();
            if (items[match_count].lmatchpos == NULL)
               goto done;

            int   j = 0;
            CS p = str;
            while (*p != ZERO && j < FUZZY_MATCH_MAX_LEN) {
               if (!SPACE_OR_TAB(mb_ptr2char(p)) || matchseq) {
                  if (list_append_number(items[match_count].lmatchpos, matches[j]) == FAIL)
                     goto done;
                  j++;
                }
                MB_PTR_ADV(p);
            }
         }
         ++match_count;
      }
      clearVar(&returnVar);
   }

   if (match_count > 0) {
      List      *retlist;

      //Sort the list by the descending order of the match score
      qsort((void *)items, (Unt)match_count, sizeof(FuzzyItem), fuzzy_match_item_compare);

      //For matchfuzzy(), return a list of matched strings.
      //      ['str1', 'str2', 'str3']
      //For matchfuzzypos(), return a list with three items.
      //The first item is a list of matched strings. The second item
      //is a list of lists where each list item is a list of matched
      //character positions. The third item is a list of matching scores.
      //  [['str1', 'str2', 'str3'], [[1, 3], [1, 3], [1, 3]]]
      if (retmatchpos) {
         li = list_find(fmatchlist, 0);
         if (li == NULL || li->c.list == NULL)
            goto done;
         retlist = li->c.list;
      } else
         retlist = fmatchlist;

      // Copy the matching strings to the return list
      for (int i = 0; i < match_count; i++) {
         if (list_append_tv(retlist, &items[i].item->c) == FAIL)
            goto done;
      }

      // next copy the list of matching positions
      if (retmatchpos) {
         li = list_find(fmatchlist, -2);
         if (li == NULL || li->c.list == NULL)
            goto done;
         retlist = li->c.list;

         for (int i = 0; i < match_count; i++) {
            if (items[i].lmatchpos != NULL) {
               if (list_append_list(retlist, items[i].lmatchpos) == OK)
                  items[i].lmatchpos = NULL;
               else
                  goto done;

            }
         }

         // copy the matching scores
         li = list_find(fmatchlist, -1);
         if (li == NULL || li->c.list == NULL)
            goto done;
         retlist = li->c.list;
         for (int i = 0; i < match_count; i++) {
            if (list_append_number(retlist, items[i].score) == FAIL)
               goto done;
         }
      }
   }

done:
   for (int i = 0; i < match_count; i++) {
      if (items[i].itemstr_allocated)
         eeglFree(items[i].itemstr);

      if (items[i].lmatchpos)
         list_free(items[i].lmatchpos);
   }
   eeglFree(items);
}

//Do fuzzy matching. Returns the list of matched strings in 'returnVar'.
//If 'retmatchpos' is true, also returns the matching character positions.
private void
do_fuzzymatch(Var* argvars, Var* returnVar, int retmatchpos) {
   Callback   cb;
   CS key = NULL;
   int      matchseq = false;
   long   max_matches = 0;

   CLEAR_POINTER(&cb);

   // validate and get the arguments
   if (argvars[0].tag != VAR_LIST || argvars[0].list == NULL) {
      showErrFmtMsg(_(e_argument_of_str_must_be_list),
                 retmatchpos ? "matchfuzzypos()" : "matchfuzzy()");
      return;
   }
   if (argvars[1].tag != VAR_STRING || argvars[1].string == NULL) {
      showErrFmtMsg(_(e_invalid_argument_str), tv_get_string(&argvars[1]));
      return;
   }

   if (argvars[2].tag != VAR_UNKNOWN) {
      Bag      *d;
      DictItem   *di;

      if (check_for_nonnull_dict_arg(argvars, 2) == FAIL)
         return;

      // To search a dict, either a callback function or a key can be specified.
      d = argvars[2].bag;
      if ((di = bagFind(d, tConst("key"))) != NULL) {
         if (di->c.tag != VAR_STRING
             || di->c.string == NULL
             || *di->c.string == ZERO
         ) {
            showErrFmtMsg(_(e_invalid_value_for_argument_str_str), "key", tv_get_string(&di->c));
            return;
         }
         key = tv_get_string(&di->c);
      } ei ((di = bagFind(d, tConst("text_cb"))) != NULL) {
         cb = get_callback(&di->c);
         if (cb.name == NULL) {
            showErrFmtMsg(_(e_invalid_value_for_argument_str), "text_cb");
            return;
         }
      }

      if ((di = bagFind(d, tConst("limit"))) != NULL) {
         if (di->c.tag != VAR_NUMBER) {
            showErrFmtMsg(_(e_invalid_value_for_argument_str), "limit");
            return;
         }
         max_matches = (long)varGetNumberChk(&di->c, NULL);
      }

      if (bagHasKey(d, tConst("matchseq")))
         matchseq = true;
   }

   // get the fuzzy matches
   allocReturnList(returnVar);
   if (retmatchpos) {

      //For matchfuzzypos(), a list with three items are returned. First
      //item is a list of matching strings, the second item is a list of
      //lists with matching positions within each string and the third item
      //is the list of scores of the matches.
      List* l = list_alloc();
      if (list_append_list(returnVar->list, l) == FAIL) {
         list_free(l);
         goto done;
      }
      l = list_alloc();
      if (list_append_list(returnVar->list, l) == FAIL) {
         list_free(l);
         goto done;
      }
      l = list_alloc();
      if (list_append_list(returnVar->list, l) == FAIL) {
         list_free(l);
         goto done;
      }
   }

   fuzzy_match_in_list(argvars[0].list, tv_get_string(&argvars[1]),
       matchseq, key, &cb, retmatchpos, returnVar->list, max_matches);

done:
   evFreeCallback(&cb);
}

void
f_matchfuzzy(Arr(Var) argvars, OUT Var* returnVar) {
   do_fuzzymatch(argvars, returnVar, false);
}

void
f_matchfuzzypos(Arr(Var) argvars, OUT Var* returnVar) {
    do_fuzzymatch(argvars, returnVar, true);
}

//Same as fuzzy_match_item_compare() except for use with a string match
private int
fuzzyMatchStr_compare(const void *s0, const void *s1) {
   int score0 = ((FuzzyMatch *)s0)->score;
   int score1 = ((FuzzyMatch *)s1)->score;
   int idx0 = ((FuzzyMatch *)s0)->idx;
   int idx1 = ((FuzzyMatch *)s1)->idx;

   if (score0 == score1)
      return idx0 == idx1 ? 0 : idx0 > idx1 ? 1 : -1;
   else
      return score0 > score1 ? -1 : 1;
}

//Sort fuzzy matches by score
void
fuzzySortByScore(OUT Fuzzy* fuzzy) {
   // Sort the list by the descending order of the match score
   qsort((void *)fuzzy->c, (Unt)fuzzy->len, sizeof(FuzzyMatch), fuzzyMatchStr_compare);
}

//Same as fuzzy_match_item_compare() except for use with a function name
//string match. <SNR> functions should be sorted to the end.
private int
fuzzy_match_func_compare(const void *s1, const void *s2) {
   int      v1 = ((FuzzyMatch *)s1)->score;
   int      v2 = ((FuzzyMatch *)s2)->score;
   int      idx1 = ((FuzzyMatch *)s1)->idx;
   int      idx2 = ((FuzzyMatch *)s2)->idx;
   CS str1 = ((FuzzyMatch *)s1)->str;
   CS str2 = ((FuzzyMatch *)s2)->str;

   if (*str1 != '<' && *str2 == '<')
      return -1;
   if (*str1 == '<' && *str2 != '<')
      return 1;
   if (v1 == v2)
      return idx1 == idx2 ? 0 : idx1 > idx2 ? 1 : -1;
   else
      return v1 > v2 ? -1 : 1;
}

//Sort fuzzy matches of function names by score. <SNR> functions should be sorted to the end.
private void
sortFnNamesByScore(Arr(FuzzyMatch) fm, int sz) {
   // Sort the list by the descending order of the match score
   qsort((void *)fm, (Unt)sz, sizeof(FuzzyMatch), fuzzy_match_func_compare);
}

//Fuzzy match 'pat' in 'str'. Return 0 if there is no match. Otherwise, return the match score.
int
fuzzyMatchStr(CS str, CS pat) {
   int      score = FUZZY_SCORE_NONE;
   Unt   matchpos[FUZZY_MATCH_MAX_LEN];

   if (str == NULL || pat == NULL)
      return score;

   fuzzy_match(str, pat, true, &score, matchpos, sizeof(matchpos) / sizeof(matchpos[0]));

   return score;
}

//Fuzzy match the position of string 'pat' in string 'str'.
//Return a dynamic array of matching positions. If there is no match, return NULL.
ArrayList *
fuzzyMatchStr_with_pos(CS str, CS pat) {
   int          score = FUZZY_SCORE_NONE;
   ArrayList       *match_positions = NULL;
   Unt       matches[FUZZY_MATCH_MAX_LEN];
   int          j = 0;

   if (str == NULL || pat == NULL)
      return NULL;

   match_positions = ALLOC_ONE(ArrayList);
   if (match_positions == NULL)
      return NULL;
   ga_init2(match_positions, sizeof(Unt), 10);

   if (!fuzzy_match(str, pat, false, &score, matches, FUZZY_MATCH_MAX_LEN)
          || score == FUZZY_SCORE_NONE) {
      ga_clear(match_positions);
      eeglFree(match_positions);
      return NULL;
   }

   for (Byte *p = pat; *p != ZERO; MB_PTR_ADV(p)) {
      if (!SPACE_OR_TAB(mb_ptr2char(p))) {
         ga_grow(match_positions, 1);
         ((Unt *)match_positions->c)[match_positions->len] = matches[j];
         match_positions->len++;
         j++;
      }
   }

   return match_positions;
}

// Find the end of the word. Assumes it starts inside a word. Return a pointer to after the word
CS
find_word_end(CS ptr) {
   int start_class = mb_get_class(ptr);
   if (start_class > 1) {
      while (*ptr != ZERO) {
         ptr += utfCharLen(ptr);
         if (mb_get_class(ptr) != start_class)
            break;
      }
   } 
   return ptr;
}


// Find the end of the line, omitting CR and NL at the end. Returns a pointer to just after the line.
CS
find_line_end(CS ptr) {
   CS s = ptr + STRLEN(ptr);
   while (s > ptr && (s[-1] == ENTER || s[-1] == NL))
      --s;
   return s;
}

//This function splits the line pointed to by `*ptr` into words and performs
//a fuzzy match for the pattern `pat` on each word. It iterates through the
//line, moving `*ptr` to the start of each word during the process.
//
//If a match is found:
//- `*ptr` points to the start of the matched word.
//- `*len` is set to the length of the matched word.
//- `*score` contains the match score.
//
//If no match is found, `*ptr` is updated to the end of the line.
int
fuzzyMatchStr_in_line(
   Byte   **ptr,
   CS pat,
   int* len,
   Pos* current_pos,
   int* score)
{
   CS str = *ptr;
   CS strBegin = str;
   CS end = NULL;
   CS start = NULL;
   int found = false;
   Byte save_end;
   CS line_end = NULL;

   if (!str || !pat)
      return found;
   line_end = find_line_end(str);

   while (str < line_end) {
      // Skip non-word characters
      start = findWordStart(str);
      if (*start == ZERO)
          break;
      end = find_word_end(start);

      // Extract the word from start to end
      save_end = *end;
      *end = ZERO;

      // Perform fuzzy match
      *score = fuzzyMatchStr(start, pat);
      *end = save_end;

      if (*score != FUZZY_SCORE_NONE) {
         *len = (int)(end - start);
         found = true;
         *ptr = start;
         if (current_pos)
            current_pos->col += (int)(end - strBegin);
         break;
      }

      // Move to the end of the current word for the next iteration
      str = end;
      // Ensure we continue searching after the current word
      while (*str != ZERO && !eeIsWordPtr(str))
         MB_PTR_ADV(str);
   }

   if (!found)
      *ptr = line_end;

   return found;
}

//Search for the next fuzzy match in the specified buffer. Attempt to find the next occurrence of 
//the given pattern in the buffer, starting from the current position. Handle line wrapping and 
//direction of search. Return true if a match is found, otherwise false.
int
search_for_fuzzy_match(
   Book* book,
   Pos* pos,
   CS pattern,
   int dir,
   Pos* start_pos,
   OUT int* len,
   OUT CS* ptr,
   int* score
) {
   Pos current_pos = *pos;
   Pos circly_end;
   int found_new_match = false;
   int looped_around = false;
   int whole_line = ctrl_x_mode_whole_line();

   if (book == curBook)
      circly_end = *start_pos;
   else {
      circly_end.lnum = book->mem.lineCount;
      circly_end.col = 0;
      circly_end.coladd = 0;
   }

   if (whole_line && start_pos->lnum != pos->lnum)
      current_pos.lnum += dir;

   do {

      // Check if looped around and back to start position
      if (looped_around && EQUAL_POS(current_pos, circly_end))
         break;

      // Ensure current_pos is valid
      if (current_pos.lnum >= 1 && current_pos.lnum <= book->mem.lineCount) {
         // Get the current line buffer
         *ptr = memGetLine(book, current_pos.lnum, false);
         if (!whole_line)
            *ptr += current_pos.col;

         // If ptr is end of line is reached, move to next line
         // or previous line based on direction
         if (*ptr != NULL && **ptr != ZERO) {
            if (!whole_line) {
               // Try to find a fuzzy match in the current line starting from current position
               found_new_match = fuzzyMatchStr_in_line(ptr, pattern, len, &current_pos, score);
               if (found_new_match) {
                  *pos = current_pos;
                  break;
               } ei (looped_around && current_pos.lnum == circly_end.lnum)
                  break;
            } else {
               if (fuzzyMatchStr(*ptr, pattern) != FUZZY_SCORE_NONE) {
                  found_new_match = true;
                  *pos = current_pos;
                  *len = (int)memGetBookLen(book, current_pos.lnum);
                  break;
               }
            }
         }
      }

      // Move to the next line or previous line based on direction
      if (dir == FORWARD) {
         if (++current_pos.lnum > book->mem.lineCount) {
            if (wrapSearchG) {
               current_pos.lnum = 1;
               looped_around = true;
            } else
               break;
         }
      } else {
         if (--current_pos.lnum < 1) {
            if (wrapSearchG) {
               current_pos.lnum = book->mem.lineCount;
               looped_around = true;
            } else
               break;
         }
      }
      current_pos.col = 0;
   } while (true);

   return found_new_match;
}

//Free an array of fuzzy string matches "fuzmatch[count]".
void
fuzmatch_str_free(FuzzyMatch *fuzmatch, int count) {
   if (!fuzmatch)
      return;

   for (int i = 0; i < count; ++i)
      eeglFree(fuzmatch[i].str);
   eeglFree(fuzmatch);
}

//Copy a list of fuzzy matches into a string list after sorting the matches by
//the fuzzy score. Free the memory allocated for 'fuzzy'.
//Return OK on success and FAIL on memory allocation failure.
int
defuzz(
   OUT ExpandMatch* matches,
   Fuzzy fuzzy,
   Boole funcsort
) {
   Unt const len = fuzzy.len;
   if (fuzzy.len == 0)
      goto theend;

   if (matches->cap < len) {
      matches->c = allocateArray(len, CS, matches->a);
      matches->cap = len;
   }

   // Sort the list by the descending order of the match score
   if (funcsort)
      sortFnNamesByScore((void *)fuzzy.c, len);
   else
      fuzzySortByScore(&fuzzy);

   for (Unt i = 0; i < len; i++)
      matches->c[i] = fuzzy.c[i].str;
   
theend:
   matches->len = len;
   return OK;
}

//Fuzzy match algorithm ported from https://github.com/jhawthorn/fzy.
//This implementation extends the original by supporting multibyte characters.

#define MATCH_MAX_LEN FUZZY_MATCH_MAX_LEN

#define SCORE_GAP_LEADING -0.005
#define SCORE_GAP_TRAILING -0.005
#define SCORE_GAP_INNER -0.01
#define SCORE_MATCH_CONSECUTIVE 1.0
#define SCORE_MATCH_SLASH 0.9
#define SCORE_MATCH_WORD 0.8
#define SCORE_MATCH_CAPITAL 0.7
#define SCORE_MATCH_DOT 0.6

private int
has_match(Byte *needle, Byte *haystack) {
   while (*needle != ZERO) {
      int n_char = mb_ptr2char(needle);
      Byte *p = haystack;
      int h_char;
      int matched = false;

      while (*p != ZERO) {
         h_char = mb_ptr2char(p);

         if (n_char == h_char || MB_TOUPPER(n_char) == h_char) {
            matched = true;
            break;
         }
         p += utfCharLen(p);
      }

      if (!matched)
         return 0;

      needle += utfCharLen(needle);
      haystack = p + utfCharLen(p);
   }
   return 1;
}

typedef struct match_struct {
   int needle_len;
   int haystack_len;
   int lower_needle[MATCH_MAX_LEN];     // stores codepoints
   int lower_haystack[MATCH_MAX_LEN];   // stores codepoints
   double match_bonus[MATCH_MAX_LEN];
} match_struct;

#define IS_WORD_SEP(c) ((c) == '-' || (c) == '_' || (c) == ' ')
#define IS_PATH_SEP(c) ((c) == '/')
#define IS_DOT(c)      ((c) == '.')

private double
compute_bonus_codepoint(Unt last_c, Unt c) {
   if (ASCII_ISALNUM(c) || eeIsWordc(c)) {
      if (IS_PATH_SEP(last_c))
         return SCORE_MATCH_SLASH;
      if (IS_WORD_SEP(last_c))
         return SCORE_MATCH_WORD;
      if (IS_DOT(last_c))
         return SCORE_MATCH_DOT;
      if (MB_ISUPPER(c) && MB_ISLOWER(last_c))
         return SCORE_MATCH_CAPITAL;
   }
   return 0;
}

private void
setup_match_struct(match_struct *match, CS needle, CS haystack) {
   int i = 0;
   CS p = needle;
   while (*p != ZERO && i < MATCH_MAX_LEN) {
      Unt c = mb_ptr2char(p);
      match->lower_needle[i++] = MB_TOLOWER(c);
      MB_PTR_ADV(p);
   }
   match->needle_len = i;

   i = 0;
   p = haystack;
   Unt prev_c = '/';
   while (*p != ZERO && i < MATCH_MAX_LEN) {
      Unt c = mb_ptr2char(p);
      match->lower_haystack[i] = MB_TOLOWER(c);
      match->match_bonus[i] = compute_bonus_codepoint(prev_c, c);
      prev_c = c;
      MB_PTR_ADV(p);
      i++;
   }
   match->haystack_len = i;
}

private inline void
match_row(match_struct const* match, int row, double* curr_D,
   double* curr_M, double const* last_D, double const* last_M
) {
   int n = match->needle_len;
   int m = match->haystack_len;
   int i = row;

   const int *lower_needle = match->lower_needle;
   const int *lower_haystack = match->lower_haystack;
   const double *match_bonus = match->match_bonus;

   double prev_score = SCORE_MIN;
   double gap_score = i == n - 1 ? SCORE_GAP_TRAILING : SCORE_GAP_INNER;

   // These will not be used with this value, but not all compilers see it
   double prev_M = SCORE_MIN, prev_D = SCORE_MIN;

   for (int j = 0; j < m; j++) {
      if (lower_needle[i] == lower_haystack[j]) {
         double score = SCORE_MIN;
         if (!i) {
            score = (j * SCORE_GAP_LEADING) + match_bonus[j];
         } ei (j) { /* i > 0 && j > 0*/
             score = MAX(
                prev_M + match_bonus[j],
                // consecutive match, doesn't stack with match_bonus
                prev_D + SCORE_MATCH_CONSECUTIVE);
         }
         prev_D = last_D[j];
         prev_M = last_M[j];
         curr_D[j] = score;
         curr_M[j] = prev_score = MAX(score, prev_score + gap_score);
      } else {
         prev_D = last_D[j];
         prev_M = last_M[j];
         curr_D[j] = SCORE_MIN;
         curr_M[j] = prev_score = prev_score + gap_score;
      }
    }
}

private double
match_positions(Byte *needle, Byte *haystack, Unt *positions) {
   if (!*needle)
      return SCORE_MIN;

   match_struct match;
   setup_match_struct(&match, needle, haystack);

   int n = match.needle_len;
   int m = match.haystack_len;

   if (m > MATCH_MAX_LEN || n > m) {
      // Unreasonably large candidate: return no score
      // If it is a valid match it will still be returned, it will
      // just be ranked below any reasonably sized candidates
      return SCORE_MIN;
   } ei (n == m) {
      // Since this method can only be called with a haystack which
      // matches needle. If the lengths of the strings are equal the
      // strings themselves must also be equal (ignoring case).
      if (positions) {
         for (int i = 0; i < n; i++)
            positions[i] = i;
      } 
      return SCORE_MAX;
   }

   // D[][] Stores the best score for this position ending with a match.
   // M[][] Stores the best possible score at this position.
   double (*D)[MATCH_MAX_LEN], (*M)[MATCH_MAX_LEN];
   M = alloc(sizeof(double) * MATCH_MAX_LEN * n);
   D = alloc(sizeof(double) * MATCH_MAX_LEN * n);
   if (!D)
      return SCORE_MIN;

   match_row(&match, 0, D[0], M[0], D[0], M[0]);
   for (int i = 1; i < n; i++)
      match_row(&match, i, D[i], M[i], D[i - 1], M[i - 1]);

   // backtrace to find the positions of optimal matching
   if (positions) {
      int match_required = 0;
      for (int i = n - 1, j = m - 1; i >= 0; i--) {
         for (; j >= 0; j--) {
            // There may be multiple paths which result in the optimal weight.
            //
            // For simplicity, we will pick the first one
            // we encounter, the latest in the candidate
            // string.
            if (D[i][j] != SCORE_MIN && (match_required || D[i][j] == M[i][j])) {
               // If this score was determined using SCORE_MATCH_CONSECUTIVE, the
               // previous character MUST be a match
               match_required = i && j && M[i][j] == D[i - 1][j - 1] + SCORE_MATCH_CONSECUTIVE;
               positions[i] = j--;
               break;
            }
         }
      }
   }

   double result = M[n - 1][m - 1];

   eeglFree(M);
   eeglFree(D);

   return result;
}

//}}}
//{{{base64

// Base64 character set
private const Byte base64Table[] = 
   "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Base64 decoding table (initialized in initBase64Table() below)
private Byte base64DecodingTableG[256];

// Initialize the base64 decoding table
private void
initBase64Table(void) {
   static Boole wasInitialized = false;

   if (wasInitialized)
      return;

   // Unsupported characters are set to 0xFF
   memset(base64DecodingTableG, 0xFF, sizeof(base64DecodingTableG));

   // Initialize the index for the base64 alphabets
   for (Unt i = 0; i < sizeof(base64Table) - 1; i++)
      base64DecodingTableG[base64Table[i]] = (Byte)i;

   // base64 padding character
   base64DecodingTableG['='] = 0;

   wasInitialized = true;
}

CS
encodeBase64(void const* binaryData_, int inputLen) {
   Unt encodedLen = ((inputLen + 2) / 3) * 4;
   char const* binaryData = binaryData_;

   CS encoded = alloc(encodedLen + 1);

   int i, j;
   for (i = 0, j = 0; i < inputLen;) {
      Unt octetA = i < inputLen ? binaryData[i++] : 0;
      Unt octetB = i < inputLen ? binaryData[i++] : 0;
      Unt octetC = i < inputLen ? binaryData[i++] : 0;

      Unt triple = (octetA << 16) | (octetB << 8) | octetC;

      encoded[j++] = base64Table[(triple >> 18) & 0x3F];
      encoded[j++] = base64Table[(triple >> 12) & 0x3F];
      encoded[j++] = (!octetB && i >= inputLen) ? '=' : base64Table[(triple >> 6) & 0x3F];
      encoded[j++] = (!octetC && i >= inputLen) ? '=' : base64Table[triple & 0x3F];
   }
   encoded[j] = ZERO;

   return encoded;
}

ArrayList
decodeBase64(Arr(Byte const) base64, int inputLen) {
   ArrayList decoded = {.len = 0, .cap = 0};

   if (inputLen == 0)
      return decoded;

   if (inputLen % 4 != 0) {
      // Invalid input length
      showErrFmtMsg(_(e_invalid_argument_str), base64);
      return decoded;
   }

   initBase64Table();

   int decodedLen = (inputLen / 4) * 3;
   if (base64[inputLen - 1] == '=')
      decodedLen--;
   if (base64[inputLen - 2] == '=')
      decodedLen--;
   if (decodedLen == 0) {
      return decoded;
   }
      
   decoded.c = alloc(decodedLen);   
   decoded.cap = decodedLen;

   int i, j;
   for (i = 0, j = 0; i < inputLen;) {
      Unt sextetA = base64DecodingTableG[base64[i++]];
      Unt sextetB = base64DecodingTableG[base64[i++]];
      Unt sextetC = base64DecodingTableG[base64[i++]];
      Unt sextetD = base64DecodingTableG[base64[i++]];

      if (sextetA == 0xFF || sextetB == 0xFF || sextetC == 0xFF || sextetD == 0xFF) {
         // Invalid character
         showErrFmtMsg(_(e_invalid_argument_str), base64);
         return decoded;
      }

      Unt triple = (sextetA << 18) | (sextetB << 12) | (sextetC << 6) | sextetD;

      if (j < decodedLen) {
         ga_append(&decoded, (triple >> 16) & 0xFF);
         j++;
      }
      if (j < decodedLen) {
         ga_append(&decoded, (triple >> 8) & 0xFF);
         j++;
      }
      if (j < decodedLen) {
         ga_append(&decoded, triple & 0xFF);
         j++;
      }
      if (j == decodedLen) {
         // Check for invalid padding bytes (based on the
         // "Base64 Malleability in Practice" ACM paper).
         if ((base64[inputLen - 2] == '=' && ((sextetB & 0xF) != 0))
            || ((base64[inputLen - 1] == '=') && ((sextetC & 0x3) != 0))
         ) {
            showErrFmtMsg(_(e_invalid_argument_str), base64);
            ga_clear(&decoded);
            return decoded;
         }
      }
   }
   decoded.len = decodedLen;
   return decoded;
}

//}}}
//{{{file path names

//Return true if file names "f1" and "f2" are in the same directory.
//"f1" may be a short name, "f2" must be a full path.
int
same_directory(CS f1, CS f2) {
   // safety check
   if (!f1 || !f2)
      return false;
      
   Byte ffname[MAXPATHL];
   (void)eeFullFileName(f1, ffname, MAXPATHL, false);
   CS t1 = gettail_sep(ffname);
   CS t2 = gettail_sep(f2);
   return (t1 - ffname == t2 - f2 && pathcmp(ffname, f2, (int)(t1 - ffname)) == 0);
}


//If the string between "p" and "pend" ends in "name/", return "pend" minus
//the length of "name/".  Otherwise return "pend".
CS
remove_tail(CS p, CS pend, CS name) {
   int      len = (int)STRLEN(name) + 1;
   CS newend = pend - len;

   if (newend >= p
          && fnamencmp(newend, name, len - 1) == 0
          && (newend == p || after_pathsep(p, newend)))
      return newend;
   return pend;
}

//true if "afterSep" points to just after a path separator.
//Take care of multi-byte characters.
Boole
after_pathsep(CS fileName, CS afterSep) {
   return afterSep > fileName && afterSep[-1] == '/' && mb_head_off(fileName, afterSep - 1) == 0;
}

//Check if the "://" of a URL is at the pointer.
Boole
path_is_url(CS p) {
   return (STRNCMP(p, "://", (Unt)3) == 0);
}

//Check if "fname" starts with "name://".
Boole
path_with_url(CS fname) {
   CS p;

   // We accept alphabetic characters and a dash in scheme part.
   // RFC 3986 allows for more, but it increases the risk of matching non-URL text.

   // first character must be alpha
   if (!ASCII_ISALPHA(*fname))
      return false;

   // check body: alpha or dash
   for (p = fname + 1; (ASCII_ISALPHA(*p) || (*p == '-')); ++p)
      {}

   // check last char is not a dash
   if (p[-1] == '-')
      return false;

   // "://" must follow
   return path_is_url(p);
}

//}}}
//{{{text formatting

private int   did_add_space = false;   // auto_format() added an extra space under the cursor

#define WHITECHAR(cc) (SPACE_OR_TAB(cc) && (!utf_iscomposing(mb_ptr2char(ml_get_cursor() + 1))))

//Return true if format option 'x' is in effect.
Boole
has_format_option(int x) {
   return curBook->o.formatOptions && firstOccurrence(curBook->o.formatOptions, x) != NULL;
}

//Write a character at the current cursor position. It is directly written into the block.
private void
pchar_cursor(int c) {
   *(memGetLine(curBook, curPor->cursor.lnum, true) + curPor->cursor.col) = c;
}

//Format text at the current insert position.
//If the INSCHAR_COM_LIST flag is present, then the value of second_indent
//will be the comment leader length sent to openLine().
void
internal_format(
   int textwidth,
   int second_indent,
   int flags,
   int format_only,
   Unt c // character to be inserted (can be ZERO)
){
   int cc;
   int skip_pos;
   int save_char = ZERO;
   int haveto_redraw = false;
   int fo_ins_blank = has_format_option(FO_INS_BLANK);
   int fo_multibyte = has_format_option(FO_MBYTE_BREAK);
   int fo_rigor_tw  = has_format_option(FO_RIGOROUS_TW);
   int fo_white_par = has_format_option(FO_WHITE_PAR);
   int first_line = true;
   ColNr   leader_len;
   int no_leader = false;
   int doComments = (flags & INSCHAR_DO_COM);
   int safe_tw = trim_to_int(8 * (Long)textwidth);
   int has_bri = curPor->o.breakIndent;

   // make sure win_lbr_chartabsize() counts correctly
   curPor->o.breakIndent = false;

   // When 'ai' is off we don't want a space under the cursor to be
   // deleted.  Replace it with an 'x' temporarily.
   if (!curBook->o.autoIndent) {
      cc = gchar_cursor();
      if (SPACE_OR_TAB(cc)) {
         save_char = cc;
         pchar_cursor('x');
      }
   }

   // Repeat breaking lines, until the current line is not too long.
   while (!gotInterruptG) {
      int startcol;      // Cursor column at entry
      int wantcol;      // column at textwidth border
      int foundcol;      // column for start of spaces
      ColNr len;
      ColNr virtcol;
      ColNr col;
      int wcc;         // counter for whitespace chars
      int did_do_comment = false;
      int first_pass;

      //Cursor is currently at the end of line. No need to format
      //if line length is less than textwidth (8 * textwidth for utf safety)
      if (curPor->cursor.col < safe_tw) {
         virtcol = get_nolist_virtcol() + char2cells(c != ZERO ? c : gchar_cursor());
         if (virtcol <= (ColNr)textwidth)
            break;
      }

      if (no_leader)
         doComments = false;
      ei (!(flags & INSCHAR_FORMAT) && has_format_option(FO_WRAP_COMS))
         doComments = true;

      // Don't break until after the comment leader
      if (doComments) {
         CS line = ml_get_curline();

         leader_len = get_leader_len(line, NULL, false, true);
      } else
         leader_len = 0;

     //If the line doesn't start with a comment leader, then don't
     //start one in a following broken line.  Avoids that a %word
     //moved to the start of the next line causes all following lines to start with %.
     if (leader_len == 0)
         no_leader = true;
     if (!(flags & INSCHAR_FORMAT) && leader_len == 0 && !has_format_option(FO_WRAP))
         break;
     if ((startcol = curPor->cursor.col) == 0)
         break;

      // find column of textwidth border
      coladvance((ColNr)textwidth);
      wantcol = curPor->cursor.col;

      // If startcol is large (a long line), formatting takes too much
      // time. The algorithm is O(n^2), it walks from the end of the
      // line to textwidth border every time for each line break.
      //
      // Ceil to 8 * textwidth to optimize.
      curPor->cursor.col = startcol < safe_tw ? startcol : safe_tw;

      foundcol = 0;
      skip_pos = 0;
      first_pass = true;

      // Find position to break at. Stop at first entered white when 'formatoptions' has 'v'
      while ((!fo_ins_blank && !has_format_option(FO_INS_VI))
             || (flags & INSCHAR_FORMAT)
             || curPor->cursor.lnum != insertStartG.lnum
             || curPor->cursor.col >= insertStartG.col
      ){
        if (first_pass && c != ZERO) {
            cc = c;
            first_pass = false;
         } else
            cc = gchar_cursor();
         if (WHITECHAR(cc)) {

            // find start of sequence of blanks
            wcc = 0;
            while (curPor->cursor.col > 0 && WHITECHAR(cc)) {
               dec_cursor();
               cc = gchar_cursor();

               // Increment count of how many whitespace chars in this
               // group; we only need to know if it's more than one.
               if (wcc < 2)
                  wcc++;
           }
           
           if (curPor->cursor.col == 0 && WHITECHAR(cc))
               break;      // only spaces in front of text

            // Don't break after a period when 'formatoptions' has 'p' and
            // there are less than two spaces.
            if (has_format_option(FO_PERIOD_ABBR) && cc == '.' && wcc < 2)
               continue;

            // Don't break until after the comment leader
            if (curPor->cursor.col < leader_len)
               break;
            if (has_format_option(FO_ONE_LETTER)) {
               // do not break after one-letter words
               if (curPor->cursor.col == 0)
                  break;   // one-letter word at begin
               // do not break "#a b" when 'tw' is 2
               if (curPor->cursor.col <= leader_len)
                  break;
               col = curPor->cursor.col;
               dec_cursor();
               cc = gchar_cursor();

               if (WHITECHAR(cc))
                  continue;   // one-letter, continue
               curPor->cursor.col = col;
            }

            inc_cursor();

            foundcol = curPor->cursor.col;
            if (curPor->cursor.col <= (ColNr)wantcol)
                break;
         } ei ((cc >= 0x100 || !utf_allow_break_before(cc)) && fo_multibyte){
            Unt ncc;
            int allow_break;

            // Break after or before a multi-byte character.
            if (curPor->cursor.col != startcol) {
               // Don't break until after the comment leader
               if (curPor->cursor.col < leader_len)
                  break;
               col = curPor->cursor.col;
               inc_cursor();
               ncc = gchar_cursor();

               allow_break = utf_allow_break(cc, ncc);

               // If we have already checked this position, skip!
               if (curPor->cursor.col != skip_pos && allow_break) {
               foundcol = curPor->cursor.col;
               if (curPor->cursor.col <= (ColNr)wantcol)
                   break;
               }
               curPor->cursor.col = col;
            }

            if (curPor->cursor.col == 0)
               break;

            ncc = cc;
            col = curPor->cursor.col;

            dec_cursor();
            cc = gchar_cursor();

            if (WHITECHAR(cc))
                continue;      // break with space
            // Don't break until after the comment leader.
            if (curPor->cursor.col < leader_len)
                break;

            curPor->cursor.col = col;
            skip_pos = curPor->cursor.col;

            allow_break = (utf_allow_break(cc, ncc));

            // Must handle this to respect line break prohibition.
            if (allow_break) {
               foundcol = curPor->cursor.col;
            }
            if (curPor->cursor.col <= (ColNr)wantcol) {
               int ncc_allow_break = utf_allow_break_before(ncc);

               if (allow_break)
                  break;
               if (!ncc_allow_break && !fo_rigor_tw) {
                  //Enable at most 1 punct hang outside of textwidth.
                  if (curPor->cursor.col == startcol) {
                     //We are inserting a non-breakable char, postpone
                     //line break check to next insert.
                     break;
                  }

                  //Neither cc nor ncc is ZERO if we are here, so it's safe to inc_cursor.
                  col = curPor->cursor.col;

                  inc_cursor();
                  cc  = ncc;
                  ncc = gchar_cursor();
                  //handle insert
                  ncc = (ncc != ZERO) ? ncc : c;

                  allow_break = (utf_allow_break(cc, ncc));

                  if (allow_break) {
                     // Break only when we are not at end of line.
                     break;
                  }
                  curPor->cursor.col = col;
               }
            }
         }
         if (curPor->cursor.col == 0)
            break;
         dec_cursor();
      }

      if (foundcol == 0) {     // no spaces, cannot break line
         curPor->cursor.col = startcol;
         break;
      }

      // adjust startcol for spaces that will be deleted and
      // characters that will remain on top line
      curPor->cursor.col = foundcol;
      while ((cc = gchar_cursor(), WHITECHAR(cc)) && (!fo_white_par || curPor->cursor.col < startcol))
         inc_cursor();
      startcol -= curPor->cursor.col;
      if (startcol < 0)
         startcol = 0;

      // put cursor after pos. to break line
      if (!fo_white_par)
         curPor->cursor.col = foundcol;

      // Split the line just before the margin.
      // Only insert/delete lines, but don't really redraw the window.
      openLine(OPENLINE_DELSPACES + OPENLINE_MARKFIX
             + (fo_white_par ? OPENLINE_KEEPTRAIL : 0)
             + (doComments ? OPENLINE_DO_COM : 0)
             + OPENLINE_FORMAT
             + ((flags & INSCHAR_COM_LIST) ? OPENLINE_COM_LIST : 0),
          ((flags & INSCHAR_COM_LIST) ? second_indent : old_indent)
      );
      if (!(flags & INSCHAR_COM_LIST))
          old_indent = 0;

      // If a comment leader was inserted, may also do this on a following line.
      if (did_do_comment)
          no_leader = false;

      if (first_line) {
          if (!(flags & INSCHAR_COM_LIST)) {
             //This section is for auto-wrap of numeric lists. When not in insert mode (i.e. 
             //format_lines()), the INSCHAR_COM_LIST flag will be set and openLine() will handle 
             //it (as seen above). The code here (and in get_number_indent()) will recognize 
             //comments if needed...
             if (second_indent < 0 && has_format_option(FO_Q_NUMBER))
                 second_indent = get_number_indent(curPor->cursor.lnum - 1);
             if (second_indent >= 0) {
               if (leader_len > 0 && second_indent - leader_len > 0) {
                   int padding = second_indent - leader_len;

                   //We started at the first_line of a numbered list that has a comment. the 
                   //openLine() function has inserted the proper comment leader and positioned
                   //the cursor at the end of the split line. Now we add the additional whitespace 
                   //needed after the comment leader for the numbered list.
                   for (int i = 0; i < padding; i++)
                      ins_str((CS)" ", 1);
                } else {
                   (void)set_indent(second_indent, SIN_CHANGED);
                }
             }
          }
          first_line = false;
      }

      // Check if cursor is not past the ZERO off the line, cindent
      // may have added or removed indent.
      curPor->cursor.col += startcol;
      len = ml_get_curline_len();
      if (curPor->cursor.col > len)
         curPor->cursor.col = len;

      haveto_redraw = true;
      set_can_cindent(true);
      // moved the cursor, don't autoindent or cindent now
      didAindentG = false;
      didSindentG = false;
      can_si = false;
      can_si_back = false;
      line_breakcheck();
   }

   if (save_char != ZERO)      // put back space after cursor
      pchar_cursor(save_char);

   curPor->o.breakIndent = has_bri;
   if (!format_only && haveto_redraw) {
      update_topline();
      drawCurBookLater(UPD_VALID);
   }
}

//Blank lines, and lines containing only the comment leader, are left untouched by the formatting.
//The function returns true in this case.  It also returns true when a line starts with the end 
//of a comment ('e' in comment flags), so that this line is skipped, and not joined to the
//previous line.  A new paragraph starts after a blank line, or when the
//comment leader changes -- webb.
private int
fmt_check_par(LineNr lnum, OUT int* leader_len, OUT CS* leader_flags, int doComments) {
   CS flags = NULL;

   CS ptr = ml_get(lnum);
   *leader_len = doComments ? get_leader_len(ptr, leader_flags, false, true) : 0;

   if (*leader_len > 0) {
      // Search for 'e' flag in comment leader flags.
      flags = *leader_flags;
      while (*flags && *flags != ':' && *flags != COM_END)
          ++flags;
   }

   return (*skipwhite(ptr + *leader_len) == ZERO
       || (*leader_len > 0 && *flags == COM_END)
       || startPS(lnum, ZERO, false));
}

//Return true if line "lnum" ends in a white character.
private int
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
private int
same_leader(
   LineNr lnum,
   int leader1_len,
   CS leader1_flags,
   int leader2_len,
   CS leader2_flags
){
   int       idx1 = 0, idx2 = 0;

   if (leader1_len == 0)
      return (leader2_len == 0);

   // If first leader has 'f' flag, the lines can be joined only if the
   // second line does not have a leader.
   // If first leader has 'e' flag, the lines can never be joined.
   // If first leader has 's' flag, the lines can only be joined if there is
   // some text after it and the second line has the 'm' flag.
   if (leader1_flags != NULL) {
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

//Return true when a paragraph starts in line "lnum".  Return false when the
//previous line is in the same paragraph.  Used for auto-formatting.
private int
paragraph_start(LineNr lnum) {
   int leader_len = 0;      // leader len of current line
   CS leader_flags = NULL;   // flags for leader of current line
   int next_leader_len;   // leader len of next line
   CS next_leader_flags;   // flags for leader of next line
   int doComments;      // format comments

   if (lnum <= 1)
      return true;      // start of the file

   CS p = ml_get(lnum - 1);
   if (*p == ZERO)
      return true;      // after empty line

   doComments = has_format_option(FO_Q_COMS);
   if (  // after non-paragraph line
         fmt_check_par(lnum - 1, OUT &leader_len, OUT &leader_flags, doComments)
         // "lnum" is not a paragraph line
         || fmt_check_par(lnum, OUT &next_leader_len, OUT &next_leader_flags, doComments)
         // missing trailing space in previous line.
         || (has_format_option(FO_WHITE_PAR) && !ends_in_white(lnum - 1))
         // numbered item starts in "lnum".
         || (has_format_option(FO_Q_NUMBER) && get_number_indent(lnum) > 0)
         // change of comment leader.
         ||  !same_leader(lnum - 1, leader_len, leader_flags, next_leader_len, next_leader_flags)
   ){
      return true;      
   } 

   return false;
}

//Called after inserting or deleting text: When 'formatoptions' includes the
//'a' flag format from the current line until the end of the paragraph.
//Keep the cursor at the same position relative to the text.
//The caller must have saved the cursor line for undo, following ones will be saved here.
void
auto_format(
    int      trailblank,   // when true also format with trailing blank
    int      prev_line   // may start in previous line
){
   if (!has_format_option(FO_AUTO))
      return;

   Pos pos = curPor->cursor;
   CS old = ml_get_curline();

   // may remove added space
   check_auto_format(false);

   //Don't format in Insert mode when the cursor is on a trailing blank, the user might insert 
   //normal text next. Also skip formatting when "1" is in 'formatoptions' and there is a single 
   //character before the cursor. Otherwise the line would be broken and when typing another 
   //non-white next they are not joined back together.
   int wasatend = (pos.col == ml_get_curline_len());
   if (*old != ZERO && !trailblank && wasatend) {
      dec_cursor();
      int cc = gchar_cursor();
      if (!WHITECHAR(cc) && curPor->cursor.col > 0 && has_format_option(FO_ONE_LETTER))
         dec_cursor();
      cc = gchar_cursor();
      if (WHITECHAR(cc)) {
         curPor->cursor = pos;
         return;
      }
      curPor->cursor = pos;
   }

   //With the 'c' flag in @formatoptions and 't' missing: only format comments.
   if (has_format_option(FO_WRAP_COMS) && !has_format_option(FO_WRAP)
            && get_leader_len(old, NULL, false, true) == 0)
      return;

   //May start formatting in a previous line, so that after "x" a word is moved to the previous 
   //line if it fits there now.  Only when this is not the start of a paragraph.
   if (prev_line && !paragraph_start(curPor->cursor.lnum)) {
      --curPor->cursor.lnum;
   if (u_save_cursor() == FAIL)
       return;
   }

   //Do the formatting and restore the cursor position.  "saved_cursor" will
   //be adjusted for the text formatting.
   saved_cursor = pos;
   format_lines((LineNr)-1, false);
   curPor->cursor = saved_cursor;
   saved_cursor.lnum = 0;

   if (curPor->cursor.lnum > curBook->mem.lineCount) {
      // "cannot happen"
      curPor->cursor.lnum = curBook->mem.lineCount;
      coladvance((ColNr)MAXCOL);
   } else
      check_cursor_col();

   //Insert mode: If the cursor is now after the end of the line while it
   //previously wasn't, the line was broken.  Because of the rule above we
   //need to add a space when 'w' is in 'formatoptions' to keep a paragraph formatted.
   if (!wasatend && has_format_option(FO_WHITE_PAR)) {
      CS new = ml_get_curline();
      ColNr   len = ml_get_curline_len();
      if (curPor->cursor.col == len) {
         CS pnew = copySubstr(new, len + 2);
         pnew[len] = ' ';
         pnew[len + 1] = ZERO;
         ml_replace(curPor->cursor.lnum, pnew, false);
         // remove the space later
         did_add_space = true;
      } else
         // may remove added space
         check_auto_format(false);
   }

   check_cursor();
}

//When an extra space was added to continue a paragraph for auto-formatting,
//delete it now.  The space must be under the cursor, just after the insert position.
void
check_auto_format(int      end_insert){      // true when ending Insert mode

   if (!did_add_space)
      return;

   Unt c = ' ';
   Unt cc = gchar_cursor();
   if (!WHITECHAR(cc))
      // Somehow the space was removed already.
      did_add_space = false;
   else {
      if (!end_insert) {
         inc_cursor();
         c = gchar_cursor();
         dec_cursor();
      }
      if (c != ZERO) {
         // The space is no longer at the end of the line, delete it.
         del_char(false);
         did_add_space = false;
      }
   }
}

//Find out textwidth to be used for formatting:
//  if 'textwidth' option is set, use it
//  ei 'wrapmargin' option is set, use curPor->width - 'wrapmargin'
//  if invalid value, use 0.
//  Set default to window width (maximum 79) for "gq" operator.
int
comp_textwidth(int ff) {  // force formatting (for "gq" command)
   int textwidth = curBook->o.textWidth;
   if (textwidth == 0 && curBook->o.wrapMargin) {
      //The width is the portal width minus 'wrapmargin' minus all the
      //things that add to the margin.
      textwidth = curPor->width - curBook->o.wrapMargin;
      if (curBook == commPortBookG)
          textwidth -= 1;
      if (isSigncolumnOn(curPor))
         textwidth -= 1;
      if (curPor->o.relativeNumber)
         textwidth -= 8;
   }
   if (textwidth < 0)
      textwidth = 0;
   if (ff && textwidth == 0) {
      textwidth = curPor->width - 1;
      if (textwidth > 79)
          textwidth = 79;
   }
   return textwidth;
}

//Implementation of the format operator 'gq'.
void
op_format(
   Operator* oper,
   int keep_cursor      // keep cursor on same text char
){
   long old_line_count = curBook->mem.lineCount;

   //Place the cursor where the "gq" or "gw" command was given, so that "u" can put it back there.
   curPor->cursor = oper->cursor_start;

   if (u_save((LineNr)(oper->start.lnum - 1), (LineNr)(oper->end.lnum + 1)) == FAIL)
      return;
   curPor->cursor = oper->start;

   if (oper->is_VIsual)
      // When there is no change: need to remove the Visual selection
      drawCurBookLater(UPD_INVERTED);

   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0)
      // Set '[ mark at the start of the formatted area
      curBook->opStart = oper->start;

   // For "gw" remember the cursor position and put it back below (adjusted
   // for joined and split lines).
   if (keep_cursor)
      saved_cursor = oper->cursor_start;

   format_lines(oper->line_count, keep_cursor);

   // Leave the cursor at the first non-blank of the last formatted line.
   // If the cursor was moved one line back (e.g. with "Q}") go to the next
   // line, so "." will do the next lines.
   if (oper->end_adjusted && curPor->cursor.lnum < curBook->mem.lineCount)
      ++curPor->cursor.lnum;
   beginline(BL_WHITE | BL_FIX);
   old_line_count = curBook->mem.lineCount - old_line_count;
   msgmore(old_line_count);

   if ((commModifierG.cmod_flags & CMOD_LOCKMARKS) == 0)
      // put '] mark on the end of the formatted area
      curBook->opEnd = curPor->cursor;

   if (keep_cursor) {
      curPor->cursor = saved_cursor;
      saved_cursor.lnum = 0;

      // formatting may have made the cursor position invalid
      check_cursor();
   }

   if (oper->is_VIsual) {
      Portal* po;
      FOR_ALL_PORTALS(po) {
         if (po->prevVisualEnd != 0) {
            // When lines have been inserted or deleted, adjust the end of
            // the Visual area to be redrawn.
            if (po->prevVisualEnd > po->oldVisualLnum)
               po->prevVisualEnd += old_line_count;
            else
               po->oldVisualLnum += old_line_count;
         }
      }
   }
}

// Implementation of the format operator 'gq' for when using 'formatexpr'.
void
op_formatexpr(Operator* oper) {
   if (oper->is_VIsual)
      // When there is no change: need to remove the Visual selection
      drawCurBookLater(UPD_INVERTED);

   if (fex_format(oper->start.lnum, oper->line_count, ZERO) != 0)
      // As documented: when 'formatexpr' returns non-zero fall back to internal formatting.
      op_format(oper, false);
}

int
fex_format(LineNr lnum, long count, int c) {  // character to be inserted
   ScriptPos   save_sctx = scriptPosG;

   // Set v:lnum to the first line number and v:count to the number of lines.
   // Set v:char to the character to be inserted (can be ZERO).
   set_EeglVar_nr(VV_LNUM, lnum);
   set_EeglVar_nr(VV_COUNT, count);
   set_EeglVar_char(c);

   // Make a copy, the option could be changed while calling it.
   CS fex = copyStr(curBook->o.formatExpr);
   scriptPosG = curBook->o.scriptLocs[PORT_foldExpr];

   // Evaluate the function.
   int r = (int)eval_to_number(fex, true);

   set_EeglVar_string(VV_CHAR, NULL, -1);
   eeglFree(fex);
   scriptPosG = save_sctx;

   return r;
}

//Format "line_count" lines, starting at the cursor position.
//When "line_count" is negative, format until the end of the paragraph.
//Lines after the cursor line are saved for undo, caller must have saved the first line.
void
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
//{{{simple formats

//private Short
//hexDigit(int c) {
//   if (SAFE_isdigit(c))
//      return c - '0';
//   c = TOLOWER_ASC(c);
//   if (c >= 'a' && c <= 'f')
//      return c - 'a' + 10;
//   return SHORT;
//}

//Return true if "val" is a valid name: only consists of alphanumeric ASCII
//characters or characters in "allowed".
int
valid_name(CS val, CS allowed) {
   for (CS s = val; *s != ZERO; ++s) {
      if (!ASCII_ISALNUM(*s) && firstOccurrence((CS)allowed, *s) == NULL)
          return false;
   } 
   return true;
}

//Return true if character "c" can be used in a variable or function name.
//Do not include '{' or '}' for magic braces.
int
isValidForScriptName(int c) {
   return ASCII_ISALNUM(c) || c == '_' || c == ':' || c == AUTOLOAD_CHAR;
}

//Return true if character "c" can be used as the first character in a
//variable or function name (excluding '{' and '}').
int
isValidForScriptName1(int c) {
   return ASCII_ISALPHA(c) || c == '_';
}

//Return true if character "c" can be used as the first character of a dictionary key.
int
isValidForFirstCharDictKey(int c) {
   return ASCII_ISALNUM(c) || c == '_';
}

//}}}
//{{{xxd (hex dumping of binary data)

private Byte version[] = "xxd 2025-08-08 by Juergen Welse ifgert et al.";
private Byte osver[] = "";

#define BIN_READ(dummy)  "r"
#define BIN_WRITE(dummy) "w"
#define BIN_CREAT(dummy) O_CREAT
#define BIN_ASSIGN(fp, dummy) fp
#define PATH_SEP '/'

// open has only two arguments on the Mac
#if __MWERKS__
# define OPEN(name, mode, umask) open(name, mode)
#else
# define OPEN(name, mode, umask) open(name, mode, umask)
#endif

#ifndef __P
# if defined(__STDC__)
#  define __P(a) a
# else
#  define __P(a) ()
# endif
#endif

#define TRY_SEEK   /* attempt to use lseek, or skip forward by reading */
#define COLS 256   /* change here, if you ever need more columns */

//LLEN is the maximum length of a line; other than the visible characters
//we need to consider also the escape color sequence prologue/epilogue,
//(11 bytes for each character).
#define LLEN \
    (39            /* addr: ⌈log10(ULONG_MAX)⌉ if "-d" flag given. We assume ULONG_MAX = 2**128 */ \
    + 2            /* ": " */ \
    + 13 * COLS    /* hex dump with colors */ \
    + (COLS - 1)   /* whitespace between groups if "-g1" option given and "-c" maxed out */ \
    + 2            /* whitespace */ \
    + 12 * COLS    /* ASCII dump with colors */ \
    + 2)           /* "\n\0" */

//LLEN_NO_COLOR is the maximum length of a line excluding the colors.
#define LLEN_NO_COLOR \
    (39         /* addr: ⌈log10(ULONG_MAX)⌉ if "-d" flag given. We assume ULONG_MAX = 2**128 */ \
    + 2         /* ": " */ \
    + 9 * COLS  /* hex dump, worst case: bitwise output using -b */ \
    + 2         /* whitespace */ \
    + COLS      /* ASCII dump */ \
    + 2)        /* "\n\0" */

private Byte hexxa[] = "0123456789abcdef0123456789ABCDEF";
private CS hexx = hexxa;

// the different hextypes known by this program:
#define HEX_NORMAL         0x00 // no flags set
#define HEX_POSTSCRIPT     0x01
#define HEX_CINCLUDE       0x02
#define HEX_BITS           0x04 // not hex a dump, but bits: 01111001
#define HEX_LITTLEENDIAN   0x08

#define CONDITIONAL_CAPITALIZE(c) (capitalize ? toupper((unsigned char)(c)) : (c))

#define COLOR_PROLOGUE(color) \
l_colored[c++] = '\033'; \
l_colored[c++] = '['; \
l_colored[c++] = '1'; \
l_colored[c++] = ';'; \
l_colored[c++] = '3'; \
l_colored[c++] = (color); \
l_colored[c++] = 'm';

#define COLOR_EPILOGUE \
l_colored[c++] = '\033'; \
l_colored[c++] = '['; \
l_colored[c++] = '0'; \
l_colored[c++] = 'm';

#define COLOR_RED '1'
#define COLOR_GREEN '2'
#define COLOR_YELLOW '3'
#define COLOR_BLUE '4'
#define COLOR_WHITE '7'

private char *pname;

private void
exit_with_usage(void) {
  fprintf(stderr, "Usage:\n       %s [options] [infile [outfile]]\n", pname);
  fprintf(stderr, "    or\n       %s -r [-s [-]offset] [-c cols] [-ps] [infile [outfile]]\n", pname);
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "    -a          toggle autoskip: A single '*' replaces nul-lines. Default off.\n");
  fprintf(stderr, "    -b          binary digit dump (incompatible with -ps). Default hex.\n");
  fprintf(stderr, "    -C          capitalize variable names in C include file style (-i).\n");
  fprintf(stderr, "    -c cols     format <cols> octets per line. Default 16 (-i: 12, -ps: 30).\n");
  fprintf(stderr, "    -e          little-endian dump (incompatible with -ps,-i,-r).\n");
  fprintf(stderr, "    -g bytes    number of octets per group in normal output. Default 2 (-e: 4).\n");
  fprintf(stderr, "    -h          print this summary.\n");
  fprintf(stderr, "    -i          output in C include file style.\n");
  fprintf(stderr, "    -l len      stop after <len> octets.\n");
  fprintf(stderr, "    -n name     set the variable name used in C include output (-i).\n");
  fprintf(stderr, "    -o off      add <off> to the displayed file position.\n");
  fprintf(stderr, "    -ps         output in postscript plain hexdump style.\n");
  fprintf(stderr, "    -r          reverse operation: convert (or patch) hexdump into binary.\n");
  fprintf(stderr, "    -r -s off   revert with <off> added to file positions found in hexdump.\n");
  fprintf(stderr, "    -d          show offset in decimal instead of hex.\n");
  fprintf(stderr, "    -s %sseek  start at <seek> bytes abs. %sinfile offset.\n",
     "[+][-]", "(or +: rel.) ");
  fprintf(stderr, "    -u          use upper case hex letters.\n");
  fprintf(stderr, "    -R when     colorize the output; <when> can be 'always', 'auto' or 'never'. Default: 'auto'.\n"),
  fprintf(stderr, "    -v          show version: \"%s%s\".\n", version, osver);
  exit(1);
}

private void
perror_exit(int ret) {
  fprintf(stderr, "%s: ", pname);
  perror(NULL);
  exit(ret);
}

private void
error_exit(int ret, char *msg) {
   fprintf(stderr, "%s: %s\n", pname, msg);
   exit(ret);
}

private int
getc_or_die(FILE *fpi) {
   int c = getc(fpi);
   if (c == EOF && ferror(fpi))
      perror_exit(2);
   return c;
}

private void
putc_or_die(int c, FILE *fpo) {
  if (putc(c, fpo) == EOF)
    perror_exit(3);
}

private void
fputs_or_die(char *s, FILE *fpo) {
   if (fputs(s, fpo) == EOF)
      perror_exit(3);
}

// Use a macro to allow for different arguments.
#define FPRINTF_OR_DIE(args) if (fprintf args < 0) perror_exit(3)

private void
fclose_or_die(FILE *fpi, FILE *fpo) {
   if (fclose(fpo) != 0)
      perror_exit(3);
   if (fclose(fpi) != 0)
      perror_exit(2);
}

//If "c" is a hex digit, return the value. Otherwise return -1.
private int
parse_hex_digit(int c) {
   return (c >= '0' && c <= '9') ? c - '0'
      : (c >= 'a' && c <= 'f') ? c - 'a' + 10
      : (c >= 'A' && c <= 'F') ? c - 'A' + 10
      : -1;
}

//If "c" is a bin digit, return the value. Otherwise return -1.
private int
parse_bin_digit(int c) {
   return (c >= '0' && c <= '1') ? c - '0' : -1;
}

//Ignore text on "fpi" until end-of-line or end-of-file. Return the '\n' or EOF character.
//When an error is encountered exit with an error message.
private int
skip_to_eol(FILE *fpi, int c) {
   while (c != '\n' && c != EOF)
      c = getc_or_die(fpi);
   return c;
}

//Max. cols binary characters are decoded from the input stream per line.
//Two adjacent garbage characters after evaluated data delimit valid data.
//Everything up to the next newline is discarded.
//
//The name is historic and came from 'undo type opt h'.
private int
huntype(
  FILE *fpi,
  FILE *fpo,
  int cols,
  int hextype,
  long base_off
) {
  int c, ign_garb = 1, n1 = -1, n2 = 0, n3 = 0, p = cols, bt = 0, b = 0, bcnt = 0;
  long have_off = 0, want_off = 0;

  rewind(fpi);

  while ((c = getc(fpi)) != EOF) {
     if (c == '\r')   /* Doze style input file? */
        continue;

      // Allow multiple spaces.  This doesn't work when there is normal text
      // after the hex codes in the last line that looks like hex, thus only
      // use it for PostScript format. */
      if (hextype == HEX_POSTSCRIPT && (c == ' ' || c == '\n' || c == '\t'))
         continue;

      if (hextype == HEX_NORMAL || hextype == HEX_POSTSCRIPT) {
         n3 = n2;
         n2 = n1;

         n1 = parse_hex_digit(c);
         if (n1 == -1 && ign_garb)
            continue;
      } else {// HEX_BITS
         n1 = parse_hex_digit(c);
         if (n1 == -1 && ign_garb)
            continue;

         bt = parse_bin_digit(c);
         if (bt != -1) {
            b = ((b << 1) | bt);
            ++bcnt;
         }
      }

      ign_garb = 0;

      if ((hextype != HEX_POSTSCRIPT) && (p >= cols)) {
         if (hextype == HEX_NORMAL) {
            if (n1 < 0) {
               p = 0;
               continue;
            }
            want_off = (want_off << 4) | n1;
         } else {/* HEX_BITS */
            if (n1 < 0) {
              p = 0;
              bcnt = 0;
              continue;
            }
            want_off = (want_off << 4) | n1;
         }
         continue;
      }

      if (base_off + want_off != have_off) {
         if (fflush(fpo) != 0)
            perror_exit(3);
         if (fseek(fpo, base_off + want_off - have_off, SEEK_CUR) >= 0)
            have_off = base_off + want_off;
         if (base_off + want_off < have_off)
            error_exit(5, "Sorry, cannot seek backwards.");
         for (; have_off < base_off + want_off; have_off++)
            putc_or_die(0, fpo);
      }

        if (hextype == HEX_NORMAL || hextype == HEX_POSTSCRIPT) {
            if (n2 >= 0 && n1 >= 0) {
               putc_or_die((n2 << 4) | n1, fpo);
               have_off++;
               want_off++;
               n1 = -1;
               if (!hextype && (++p >= cols))
                  // skip the rest of the line as garbage
                  c = skip_to_eol(fpi, c);
            } ei (n1 < 0 && n2 < 0 && n3 < 0)
             // already stumbled into garbage, skip line, wait and see
             c = skip_to_eol(fpi, c);
      } else { // HEX_BITS
        if (bcnt == 8) {
            putc_or_die(b, fpo);
            have_off++;
            want_off++;
            b = 0;
            bcnt = 0;
            if (++p >= cols)
               // skip the rest of the line as garbage
               c = skip_to_eol(fpi, c);
          }
      }

      if (c == '\n') {
         if (hextype == HEX_NORMAL || hextype == HEX_BITS)
            want_off = 0;
         p = cols;
         ign_garb = 1;
      }
   }
   if (fflush(fpo) != 0)
      perror_exit(3);
   fseek(fpo, 0L, SEEK_END);
   fclose_or_die(fpi, fpo);
   return 0;
}


//Print line l with given colors.
private void
print_colored_line(FILE *fp, char *l, char *colors) {
  static char l_colored[LLEN+1];

  if (colors) {
      int c = 0;
      if (colors[0]) {
     COLOR_PROLOGUE(colors[0])
   }
   l_colored[c++] = l[0];
   int i;
   for (i = 1; l[i]; i++) {
      if (colors[i] != colors[i-1]) {
        if (colors[i-1]) {
           COLOR_EPILOGUE
        }
        if (colors[i]) {
           COLOR_PROLOGUE(colors[i])
         }
      }
     l_colored[c++] = l[i];
   }

   if (colors[i]) {
     COLOR_EPILOGUE
   }
   l_colored[c++] = '\0';

      fputs_or_die(l_colored, fp);
    } else
    fputs_or_die(l, fp);
}

//Print line l with given colors. If nz is false, xxdline regards the line as a line of
//zeroes. If there are three or more consecutive lines of zeroes,
//they are replaced by a single '*' character.
//
//If the output ends with more than two lines of zeroes, you
//should call xxdline again with l belse ifng the last line and nz
//negative. This ensures that the last line is shown even when
//it is all zeroes.
//
//If nz is always positive, lines are never suppressed.
private void
xxdline(FILE* fp, char* l, char* colors, int nz) {
   static char z[LLEN_NO_COLOR+1];
   static char z_colors[LLEN_NO_COLOR+1];
   static signed char zero_seen = 0;

   if (!nz && zero_seen == 1) {
      strcpy(z, l);
      memcpy(z_colors, colors, strlen(z));
   }

   if (nz || !zero_seen++) {
      if (nz) {
         if (nz < 0)
            zero_seen--;
         if (zero_seen == 2)
            print_colored_line(fp, z, z_colors);
         if (zero_seen > 2)
            fputs_or_die("*\n", fp);
      }
      if (nz >= 0 || zero_seen > 0)
         print_colored_line(fp, l, colors);

      if (nz)
         zero_seen = 0;
   }

   //If zero_seen > 3, then its exact value doesn't matter, so long as it
   //remains >3 and incrementing it will not cause overflow. */
   if (zero_seen >= 0x7F)
     zero_seen = 4;
}

private char
get_color_char(int e) {
   if (e > 31 && e < 127)
      return COLOR_GREEN;

   ei (e == 9 || e == 10 || e == 13)
      return COLOR_YELLOW;
   ei (e == 0)
      return COLOR_WHITE;
   ei (e == 255)
      return COLOR_BLUE;
   else
      return COLOR_RED;
   return 0;
}

private int
enable_color(void) {
   return isatty(STDOUT_FILENO);
}

int
xxdMain(int argc, char* argv[]) {
   FILE *fp, *fpo;
   int c, e, p = 0, relseek = 1, negseek = 0, revert = 0, i, x;
   int cols = 0, colsgiven = 0, nonzero = 0, autoskip = 0, hextype = HEX_NORMAL;
   int capitalize = 0, decimal_offset = 0;
   int octspergrp = -1;   /* number of octets grouped in output */
   int grplen;      /* total chars per octet group excluding colors */
   long length = -1, n = 0, seekoff = 0;
   unsigned long displayoff = 0;
   static char l[LLEN_NO_COLOR+1];  /* static because it may be too big for stack */
   static char colors[LLEN_NO_COLOR+1]; /* color array */
   char *pp;
   char *varname = NULL;
   int addrlen = 9;
   int color = 0;
   char *no_color;
   char cur_color = 0;

   no_color = getenv("NO_COLOR");
   if (no_color == NULL || no_color[0] == '\0')
    color = enable_color();

  pname = argv[0];
  for (pp = pname; *pp; )
    if (*pp++ == PATH_SEP)
      pname = pp;
#ifdef FILE_SEP
  for (pp = pname; *pp; pp++)
    if (*pp == FILE_SEP) {
   *pp = '\0';
   break;
      }
#endif

   while (argc >= 2) {
      pp = argv[1] + (!STRNCMP(argv[1], "--", 2) && argv[1][2]);
      if (!STRNCMP(pp, "-a", 2)) autoskip = 1 - autoskip;
      ei (!STRNCMP(pp, "-b", 2)) hextype |= HEX_BITS;
      ei (!STRNCMP(pp, "-e", 2)) hextype |= HEX_LITTLEENDIAN;
      ei (!STRNCMP(pp, "-u", 2)) hexx = hexxa + 16;
      ei (!STRNCMP(pp, "-p", 2)) hextype |= HEX_POSTSCRIPT;
      ei (!STRNCMP(pp, "-i", 2)) hextype |= HEX_CINCLUDE;
      ei (!STRNCMP(pp, "-C", 2)) capitalize = 1;
      ei (!STRNCMP(pp, "-d", 2)) decimal_offset = 1;
      ei (!STRNCMP(pp, "-r", 2)) revert++;
      ei (!STRNCMP(pp, "-v", 2)
   ) {
      fprintf(stderr, "%s%s\n", version, osver);
      exit(0);
   } ei (!STRNCMP(pp, "-c", 2)) {
      if (pp[2] && !STRNCMP("apitalize", pp + 2, 9))
         capitalize = 1;
      ei (pp[2] && STRNCMP("ols", pp + 2, 3)) {
         colsgiven = 1;
         cols = (int)strtol(pp + 2, NULL, 0);
      } else {
         if (!argv[2])
            exit_with_usage();
         colsgiven = 1;
         cols = (int)strtol(argv[2], NULL, 0);
         argv++;
         argc--;
      }
   } ei (!STRNCMP(pp, "-g", 2)) {
     if (pp[2] && STRNCMP("roup", pp + 2, 4))
       octspergrp = (int)strtol(pp + 2, NULL, 0);
     else {
         if (!argv[2])
      exit_with_usage();
         octspergrp = (int)strtol(argv[2], NULL, 0);
         argv++;
         argc--;
       }
   } ei (!STRNCMP(pp, "-o", 2)) {
     int reloffset = 0;
     int negoffset = 0;
     if (pp[2] && STRNCMP("ffset", pp + 2, 5))
       displayoff = strtoul(pp + 2, NULL, 0);
      else {
         if (!argv[2])
            exit_with_usage();

         if (argv[2][0] == '+')
            reloffset++;
         if (argv[2][reloffset] == '-')
            negoffset++;

         if (negoffset)
            displayoff = ULONG_MAX - strtoul(argv[2] + reloffset+negoffset, NULL, 0) + 1;
         else
            displayoff = strtoul(argv[2] + reloffset+negoffset, NULL, 0);

         argv++;
         argc--;
      }
   } ei (!STRNCMP(pp, "-s", 2)) {
      relseek = 0;
      negseek = 0;
      if (pp[2] && STRNCMP("kip", pp+2, 3) && STRNCMP("eek", pp+2, 3)) {
         if (pp[2] == '+')
            relseek++;
         if (pp[2+relseek] == '-')
            negseek++;
         seekoff = strtol(pp + 2+relseek+negseek, (char **)NULL, 0);
       } else {
         if (!argv[2])
            exit_with_usage();
         if (argv[2][0] == '+')
            relseek++;
         if (argv[2][relseek] == '-')
            negseek++;
         seekoff = strtol(argv[2] + relseek+negseek, (char **)NULL, 0);
         argv++;
         argc--;
      }
   } ei (!STRNCMP(pp, "-l", 2)) {
      if (pp[2] && STRNCMP("en", pp + 2, 2))
         length = strtol(pp + 2, (char **)NULL, 0);
      else {
         if (!argv[2])
            exit_with_usage();
         length = strtol(argv[2], (char **)NULL, 0);
         argv++;
         argc--;
      }
   } ei (!STRNCMP(pp, "-n", 2)) {
      if (pp[2] && STRNCMP("ame", pp + 2, 3))
         varname = pp + 2;
      else {
         if (!argv[2])
            exit_with_usage();
         varname = argv[2];
         argv++;
         argc--;
      }
   } ei (!STRNCMP(pp, "-R", 2)) {
     char *pw = pp + 2;
     if (!pw[0]) {
         pw = argv[2];
         argv++;
         argc--;
       }
     if (!pw)
       exit_with_usage();
     if (!STRNCMP(pw, "always", 6)) {
         (void)enable_color();
         color = 1;
       }
     ei (!STRNCMP(pw, "never", 5))
       color = 0;
     ei (!STRNCMP(pw, "auto", 4))
       color = enable_color();
     else
       exit_with_usage();
   } ei (!strcmp(argv[1], "--")) {  // end of options
     argv++;
     argc--;
     break;
   }
      ei (pp[0] == '-' && pp[1])   /* unknown option */
   exit_with_usage();
      else
   break;            /* not an option */

      argv++;            /* advance to next argument */
      argc--;
    }

  if (hextype != (HEX_CINCLUDE | HEX_BITS)) {
   // Allow at most one bit to be set in hextype
   if (hextype & (hextype - 1))
       error_exit(1, "only one of -b, -e, -u, -p, -i can be used");
    }

  if (!colsgiven || (!cols && hextype != HEX_POSTSCRIPT))
    switch (hextype) {
      case HEX_POSTSCRIPT:   cols = 30; break;
      case HEX_CINCLUDE:   cols = 12; break;
      case HEX_CINCLUDE | HEX_BITS:
      case HEX_BITS:      cols = 6; break;
      case HEX_NORMAL:
      case HEX_LITTLEENDIAN:
      default:         cols = 16; break;
      }

  if (octspergrp < 0)
    switch (hextype) {
      case HEX_CINCLUDE | HEX_BITS:
      case HEX_BITS:      octspergrp = 1; break;
      case HEX_NORMAL:      octspergrp = 2; break;
      case HEX_LITTLEENDIAN:   octspergrp = 4; break;
      case HEX_POSTSCRIPT:
      case HEX_CINCLUDE:
      default:         octspergrp = 0; break;
      }

  if ((hextype == HEX_POSTSCRIPT && cols < 0) ||
      (hextype != HEX_POSTSCRIPT && cols < 1) ||
      ((hextype == HEX_NORMAL || hextype == HEX_BITS || hextype == HEX_LITTLEENDIAN)
                         && (cols > COLS)))
    {
      fprintf(stderr, "%s: invalid number of columns (max. %d).\n", pname, COLS);
      exit(1);
    }

  if (octspergrp < 1 || octspergrp > cols)
    octspergrp = cols;
  ei (hextype == HEX_LITTLEENDIAN && (octspergrp & (octspergrp-1)))
    error_exit(1, "number of octets per group must be a power of 2 with -e.");

  if (argc > 3)
    exit_with_usage();

  if (argc == 1 || (argv[1][0] == '-' && !argv[1][1]))
    BIN_ASSIGN(fp = stdin, !revert);
  else {
      if ((fp = fopen(argv[1], BIN_READ(!revert))) == NULL) {
     fprintf(stderr,"%s: ", pname);
     perror(argv[1]);
     return 2;
   }
    }

   if (argc < 3 || (argv[2][0] == '-' && !argv[2][1]))
      BIN_ASSIGN(fpo = stdout, revert);
   else {
      int fd;
      int mode = revert ? O_WRONLY : (O_TRUNC|O_WRONLY);

      if (((fd = OPEN(argv[2], mode | BIN_CREAT(revert), 0666)) < 0) 
           || (fpo = fdopen(fd, BIN_WRITE(revert))) == NULL
      ) {
        fprintf(stderr, "%s: ", pname);
        perror(argv[2]);
        return 3;
      }
      rewind(fpo);
   }

   if (revert)
    switch (hextype) {
      case HEX_NORMAL:
      case HEX_POSTSCRIPT:
      case HEX_BITS:
         return huntype(fp, fpo, cols, hextype, negseek ? -seekoff : seekoff);
      break;
      default:
         error_exit(-1, "Sorry, cannot revert this type of hexdump");
      }

   if (seekoff || negseek || !relseek) {
      if (relseek)
         e = fseek(fp, negseek ? -seekoff : seekoff, SEEK_CUR);
      else
         e = fseek(fp, negseek ? -seekoff : seekoff, negseek ? SEEK_END : SEEK_SET);
      if (e < 0 && negseek)
         error_exit(4, "Sorry, cannot seek.");
      if (e >= 0)
         seekoff = ftell(fp);
      else {
         long s = seekoff;

         while (s--) {
            if (getc_or_die(fp) == EOF) {
               error_exit(4, "Sorry, cannot seek.");
            }
         } 
      }
   }

   if (hextype & HEX_CINCLUDE) {
      // A user-set variable name overrides fp == stdin
      if (varname == NULL && fp != stdin)
         varname = argv[1];

      if (varname != NULL) {
         FPRINTF_OR_DIE((fpo, "unsigned char %s", isdigit((unsigned char)varname[0]) ? "__" : ""));
         for (e = 0; (c = varname[e]) != 0; e++)
            putc_or_die(isalnum((unsigned char)c) ? CONDITIONAL_CAPITALIZE(c) : '_', fpo);
         fputs_or_die("[] = {\n", fpo);
      }

      p = 0;
      while ((length < 0 || p < length) && (c = getc_or_die(fp)) != EOF) {
        if (hextype & HEX_BITS) {
            if (p == 0)
               fputs_or_die("  ", fpo);
            ei (p % cols == 0)
               fputs_or_die(",\n  ", fpo);
            else
               fputs_or_die(", ", fpo);

            FPRINTF_OR_DIE((fpo, "0b"));
            for (int j = 7; j >= 0; j--)
               putc_or_die((c & (1 << j)) ? '1' : '0', fpo);
            p++;
          } else {
            FPRINTF_OR_DIE(
               (fpo, 
                (hexx == hexxa) ? "%s0x%02x" : "%s0X%02X", 
                   (p % cols) ? ", " : (!p ? "  " : ",\n  "), c)
            );
            p++;
          }
      }

      if (p)
         fputs_or_die("\n", fpo);

      if (varname != NULL) {
         fputs_or_die("};\n", fpo);
         FPRINTF_OR_DIE((fpo, "unsigned int %s", isdigit((unsigned char)varname[0]) ? "__" : ""));
         for (e = 0; (c = varname[e]) != 0; e++)
            putc_or_die(isalnum((unsigned char)c) ? CONDITIONAL_CAPITALIZE(c) : '_', fpo);
         FPRINTF_OR_DIE((fpo, "_%s = %d;\n", capitalize ? "LEN" : "len", p));
      }

      fclose_or_die(fp, fpo);
      return 0;
   }

   if (hextype == HEX_POSTSCRIPT) {
      p = cols;
      while ((length < 0 || n < length) && (e = getc_or_die(fp)) != EOF) {
         putc_or_die(hexx[(e >> 4) & 0xf], fpo);
         putc_or_die(hexx[e & 0xf], fpo);
         n++;
         if (cols > 0 && !--p) {
            putc_or_die('\n', fpo);
            p = cols;
         }
      }
      if (cols == 0 || p < cols)
         putc_or_die('\n', fpo);
      fclose_or_die(fp, fpo);
      return 0;
   }

   // hextype: HEX_NORMAL or HEX_BITS or HEX_LITTLEENDIAN 
   if (hextype != HEX_BITS) {
      grplen = octspergrp + octspergrp + 1;   /* chars per octet group */
   } else   // hextype == HEX_BITS
      grplen = 8 * octspergrp + 1;

   while ((length < 0 || n < length) && (e = getc_or_die(fp)) != EOF) {
      if (p == 0) {
         addrlen = sprintf(l, decimal_offset ? "%08ld:" : "%08lx:",
              ((unsigned long)(n + seekoff + displayoff)));
         for (c = addrlen; c < LLEN_NO_COLOR; l[c++] = ' ')
            ;
      }
      x = hextype == HEX_LITTLEENDIAN ? p ^ (octspergrp-1) : p;
      c = addrlen + 1 + (grplen * x) / octspergrp;
      if (hextype == HEX_NORMAL || hextype == HEX_LITTLEENDIAN) {
         if (color) {
            cur_color = get_color_char(e);
            colors[c] = cur_color;
            colors[c+1] = cur_color;
         }

         l[c]   = hexx[(e >> 4) & 0xf];
         l[++c] = hexx[e & 0xf];
      } else {// hextype == HEX_BITS */
         for (i = 7; i >= 0; i--)
            l[c++] = (e & (1 << i)) ? '1' : '0';
      }
      if (e)
         nonzero++;
         // When changing this update definition of LLEN and LLEN_NO_COLOR above.
      if (hextype == HEX_LITTLEENDIAN)
         // last group will be fully used, round up 
         c = grplen * ((cols + octspergrp - 1) / octspergrp);
      else
         c = (grplen * cols - 1) / octspergrp;

      if (hextype == HEX_LITTLEENDIAN)
         c -= 1;

      c += addrlen + 3 + p;
      if (color)
         colors[c] = cur_color;
      l[c++] = (e > 31 && e < 127) ? e : '.';
         n++;
      if (++p == cols) {
         l[c++] = '\n';
         l[c] = '\0';

         xxdline(fpo, l, color ? colors : NULL, autoskip ? nonzero : 1);
         memset(colors, 0, c);
         nonzero = 0;
         p = 0;
      }
   }
   if (p) {
      l[c++] = '\n';
      l[c] = '\0';
      if (color) {
         x = p;
         if (hextype == HEX_LITTLEENDIAN) {
            int fill = octspergrp - (p % octspergrp);
            if (fill == octspergrp) fill = 0;

            c = addrlen + 1 + (grplen * (x - (octspergrp-fill))) / octspergrp;

            for (i = 0; i < fill;i++) {
               colors[c] = COLOR_RED;
               l[c++] = ' '; /* empty space */
               x++;
               p++;
            }
         }

         if (hextype != HEX_BITS) {
            c = addrlen + 1 + (grplen * x) / octspergrp;
            c += cols - p;
            c += (cols - p) / octspergrp;

            for (i = cols - p; i > 0;i--) {
               colors[c] = COLOR_RED;
               l[c++] = ' ';
            }
         }
         xxdline(fpo, l, colors, 1);
      } else
         xxdline(fpo, l, NULL, 1);
   } ei (autoskip)
      xxdline(fpo, l, color ? colors : NULL, -1);   // last chance to flush out suppressed lines

   fclose_or_die(fp, fpo);
   return 0;
}

//}}}
