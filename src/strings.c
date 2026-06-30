//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## strings.c: utility functions for string manipulation 
 

#include <string.h>
#include <strings.h>
#ifdef FREESTANDING_STRINGS
#include "base.h"
#include "proto/strings.h"
#define alloc malloc
#define eeRealloc realloc
#define eeglFree(a) if (a) { free(a); }
#else
#include "eegl.h"
#endif

#ifndef PROTO
#include <wchar.h>   //for towupper() and towlower()
#include <wctype.h>  //for towlower()
#include <ctype.h>   //for islower()
#include <stdlib.h>  //for atol()
#include <sys/stat.h>
#include <time.h> // for time()
#endif

//{{{ Arena

#define CHUNK_QUANT 32768

typedef struct ArenaChunk ArenaChunk;

struct ArenaChunk { // :ArenaChunk
   Unt size;
   ArenaChunk* next;
   char memory[]; // flexible array member
};

struct Arena { // :Arena
   ArenaChunk* firstChunk;
   ArenaChunk* currChunk;
   int currInd;
};

Arena*
createArena() { //:createArena
   Arena* result = malloc(sizeof(Arena));

   Unt firstChunkSize = (CHUNK_QUANT - 32);
   ArenaChunk* firstChunk = malloc(firstChunkSize);
   if (!result || !firstChunk)
      { abort(); }

   firstChunk->size = firstChunkSize - sizeof(ArenaChunk);
   firstChunk->next = null;
   result->firstChunk = firstChunk;
   result->currChunk = firstChunk;
   result->currInd = 0;
   return result;
}

private Unt
calculateChunkSize(Unt allocSize) { //:calculateChunkSize
// Calculates memory for a new chunk. Memory is quantized and is always 32 bytes less
// 32 for any possible padding malloc might use internally,
// so that the total allocation size is a good even number of OS memory pages
   Unt fullMemory = sizeof(ArenaChunk) + allocSize + 32;
   // struct header + main memory chunk + space for malloc bookkeep

   int mallocMemory = fullMemory < CHUNK_QUANT
                  ? CHUNK_QUANT
                  : (fullMemory % CHUNK_QUANT > 0
                     ? (fullMemory/CHUNK_QUANT + 1)*CHUNK_QUANT
                     : fullMemory);

   return mallocMemory - 32;
}

void*
allocateOnArena(Unt allocSize, Arena* a) { //:allocateOnArena
// Allocate memory in the arena, malloc'ing a new chunk if needed
   if ((Unt)a->currInd + allocSize >= a->currChunk->size) {
      if (a->currChunk->next != null && a->currChunk->next->size < allocSize) {
         // the next chunk is big enough, so we skip the rest of this chunk and move on
#ifndef FREESTANDING_STRINGS 
         lo("reusing cleared memory from the arena!");
#endif 
         a->currChunk = a->currChunk->next;
         a->currInd = 0;
      } else { // we need to allocate new chunk

         Unt newSize = calculateChunkSize(allocSize);
         ArenaChunk* newChunk = malloc(newSize);
         if (!newChunk) {
            perror("malloc error when allocating arena chunk");
            exit(EXIT_FAILURE);
         };
         // sizeof counts everything but the flexible array member, that's why we subtract it
         newChunk->size = newSize - sizeof(ArenaChunk);
         newChunk->next = a->currChunk->next; // if the arena has a (small) tail, don't lose it

         a->currChunk->next = newChunk;
         a->currChunk = newChunk;
         a->currInd = 0;
      }

   }
   void* result = (void*)(a->currChunk->memory + (a->currInd));
   a->currInd += allocSize;
   if (allocSize % 4 != 0)  {
      a->currInd += (4 - (allocSize % 4));
   }
   return result;
}

void
deleteArena(Arena* ar) { //:deleteArena
// Returns memory of the arena to the OS
   ArenaChunk* curr = ar->firstChunk;
   while (curr != null) {
      ArenaChunk* nextToFree = curr->next;
      free(curr);
      curr = nextToFree;
   }
   free(ar);
}

//private void
//clearArena(Arena* a) { //:clearArena
//// Clears the memory of the arena for reuse. Does not free memory.
//   a->currChunk = a->firstChunk;
//   a->currInd = 0;
//}

void
arenaTryFree(void* start, Unt len, Arena* a) {
// If this memory span is at the very end of this arena, then free it by rewinding
   if ((void*)&(a->currChunk->memory) + (a->currInd - len) == start) {
      a->currInd -= len;
   }
}

//}}}
//{{{charset (utf-8)

#define URL_SLASH      1      // path_is_url() has found "://"
#define URL_BACKSLASH  2      // path_is_url() has found ":\\"

// Lookup table to quickly get the length in bytes of a UTF-8 character from the first byte of a 
// UTF-8 string. Bytes which are illegal when used as the first byte have a 1.
// The ZERO byte has length 1.
private Byte utf8LenTable[256] = {
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
   1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,3,3,3,
   3,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,6,6,1,1
};

// Like utf8LenTable above, but using a zero for illegal lead bytes.
private Byte utf8LenTable_zero[256] = {
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,3,3,3,
   3,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,6,6,0,0
};


//Convert a UTF-8 byte sequence to a character number.
//If the sequence is illegal or truncated by a ZERO the first byte is returned.
//For an overlong sequence this may return zero.
//Do not include composing characters, of course.
Unt
mb_ptr2char(Byte* p) {
   if (p[0] < 0x80)   // be quick for ASCII
      return (Unt)p[0];

   int len = utf8LenTable_zero[p[0]];
   if (len > 1 && (p[1] & 0xc0) == 0x80) {
      if (len == 2)
         return ((p[0] & 0x1f) << 6) + (p[1] & 0x3f);
      if ((p[2] & 0xc0) == 0x80) {
         if (len == 3)
         return ((p[0] & 0x0f) << 12) + ((p[1] & 0x3f) << 6) + (p[2] & 0x3f);
         if ((p[3] & 0xc0) == 0x80) {
            if (len == 4) {
               return ((p[0] & 0x07) << 18) + ((p[1] & 0x3f) << 12)
                  + ((p[2] & 0x3f) << 6) + (p[3] & 0x3f);
           } 
           if ((p[4] & 0xc0) == 0x80) {
               if (len == 5) {
                  return ((p[0] & 0x03) << 24) + ((p[1] & 0x3f) << 18)
                     + ((p[2] & 0x3f) << 12) + ((p[3] & 0x3f) << 6)
                     + (p[4] & 0x3f);
               } 
               if ((p[5] & 0xc0) == 0x80 && len == 6) {
                 return ((p[0] & 0x01) << 30) + ((p[1] & 0x3f) << 24)
                    + ((p[2] & 0x3f) << 18) + ((p[3] & 0x3f) << 12)
                    + ((p[4] & 0x3f) << 6) + (p[5] & 0x3f);
               } 
            }
         }
      }
   }
   // Illegal value, just return the first byte
   return p[0];
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
      MEMMOVE(buf, str, (Unt)len);
      buf[len] = ZERO;
   } else {
      ga_init2(&ga, 1, 10);
      if (ga_grow(&ga, len + 1) == FAIL)
         return NULL;
      MEMMOVE(ga.c, str, (Unt)len);
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
   return islower(c) != 0;
}

int
eeIsUpper(Unt c) {
   if (c <= '@')
      return false;
   if (c >= 0x80) {
      return utf_isupper(c);
   }
   return isupper(c) != 0;
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
   } else
      return TOUPPER_ASC(c);
}

int
eeglToLower(Unt c) {
   if (c <= '@')
      return c;
   if (c >= 0x80) {
       return utf_tolower(c);
   } else
      return TOLOWER_ASC(c);
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
   Long retval = 0;

   if (*str == '-')
      ++str;
   while (EE_ISDIGIT(*str)) {
      if (retval >= (Long)(LONG_MAX / 10 - 10))
         retval = LONG_MAX;
      else
         retval = retval * 10  + (*str - '0');
      ++str;
   } 
   if (**pp == '-') {
      if (retval == (Long)LONG_MAX)
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
   OUT int* prep,    // type of number 0 = decimal, 'x' or 'X' is hex, 'b' or 'B' is bin
   OUT int* len,     // detected length of number
   int what,     // what numbers to recognize
   OUT Long* nptr, // signed result
   OUT Ulong* unptr,  // unsigned result
   int maxlen,   // max length of string to check
   Boole strict,   // check strictly
   Boole* overflow  // when not NULL set to true for overflow
){
   CS ptr = start;
   int pre = 0;      // default is decimal
   int negative = false;
   Ulong un = 0;

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
            && (maxlen == 0 || maxlen > 2)
      )
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
         if (un <= ULONG_MAX / 2)
            un = 2 * un + (Ulong)(*ptr - '0');
         else {
            un = ULONG_MAX;
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
         if (un <= ULONG_MAX / 16)
            un = 16 * un + (Ulong)hex2nr(*ptr);
         else {
            un = ULONG_MAX;
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
         if (un < ULONG_MAX / 10
             || (un == ULONG_MAX / 10 && digit <= ULONG_MAX % 10))
            un = 10 * un + digit;
         else {
            un = ULONG_MAX;
            if (overflow)
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

   if (prep)
      *prep = pre;
   if (len)
      *len = (int)(ptr - start);
   if (nptr) {
      if (negative) {  // account for leading '-' for decimal numbers
          // avoid ubsan error for overflow
         if (un > LONG_MAX) {
            *nptr = LONG_MIN;
            if (overflow)
               *overflow = true;
         } else
            *nptr = -(Long)un;
      } else {
         // prevent a large unsigned number to become negative
         if (un > LONG_MAX) {
            un = LONG_MAX;
            if (overflow)
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

//backslash_halve() plus save the result in allocated memory.
CS
backslash_halve_save(CS p) {
   CS res = copyStr(p);
   backslash_halve(res);
   return res;
}

//Convert non-printable character to two or more printable characters in "charbuf[]". 
//"charbuf" needs to be able to hold five bytes.
//Does NOT work for multi-byte characters, c must be <= 255.
void
transchar_nonprint(CS charbuf, int c) {
   if (c == NL)
      c = ZERO;      // we use newline in place of a ZERO

   ei (c <= 0x7f) {       // 0x00 - 0x1f and 0x7f
      charbuf[0] = '^';
      charbuf[1] = c ^ 0x40;      // DEL displayed as ^?
      charbuf[2] = ZERO;
   } else {
      transchar_hex(charbuf, c);
   }
}
//Convert a UTF-8 byte sequence to a wide character.
//String is assumed to be terminated by ZERO or after "n" bytes, whichever comes first.
//The function is safe in the sense that it never accesses memory beyond the
//first "n" bytes of "s".
//
//On success, return decoded codepoint, advance "s" to the beginning of next character and 
//decrease "n" accordingly.
//
//If end of string was reached, return 0 and, if "n" > 0, advance "s" past ZERO byte.
//
//If byte sequence is illegal or incomplete, returns UNT and does not advance "s".
private Unt
utf_safe_read_char_adv(OUT CS* s, OUT Unt* n){
   Unt c;

   if (*n == 0) // end of buffer
      return 0;

   int k = utf8LenTable_zero[**s];

   if (k == 1) {
      // ASCII character or ZERO
      (*n)--;
      return *(*s)++;
   }

   if ((Unt)k <= *n) {
      //We have a multibyte sequence and it isn't truncated by buffer limits so mb_ptr2char() is 
      //safe to use. Or the first byte is illegal (k=0), and it's also safe to use mb_ptr2char().
      c = mb_ptr2char(*s);

      //On failure, mb_ptr2char() returns the first byte, so here we check equality with the first 
      //byte. The only non-ASCII character which equals the first byte of its own UTF-8 
      //representation is U+00C3 (UTF-8: 0xC3 0x83), so need to check that special case too.
      //It's safe even if n=1, else we would have k=2 > n.
      if (c != (int)(**s) || (c == 0xC3 && (*s)[1] == 0x83)) {
          // byte sequence was successfully decoded
          *s += k;
          *n -= k;
          return c;
      }
   }

   // byte sequence is incomplete or illegal
   return UNT;
}

// Get the length of a UTF-8 byte sequence, excluding any following composing characters.
// Return 0 for "". Return 1 for an illegal byte sequence.
Unt
utf_ptr2len(CS p) {
   if (*p == ZERO)
      return 0;
   Unt len = utf8LenTable[*p];
   for (Unt i = 1; i < len; ++i) {
      if ((p[i] & 0xc0) != 0x80)
         return 1;
   } 
   return len;
}

//Return length of UTF-8 character obtained from the first byte. "b" must be between 0 and 255!
//Return 1 for an invalid first byte value.
Unt
utf_byte2len(int b) {
   return utf8LenTable[b];
}

//Return length of UTF-8 character, obtained from the first byte.
//"b" must be between 0 and 255! Return 0 for an invalid first byte value.
Unt
utf_byte2len_zero(int b) {
   return utf8LenTable_zero[b];
}

Boole
utfNeedTruncate(CS p, int size) {
   return utf_ptr2len_len(p, size) < (Unt)utf8LenTable[*p];
}

//Get the length of UTF-8 byte sequence "p[size]". Do not include any following composing 
//characters. Return 1 for "". 1 for an illegal byte sequence (also in incomplete byte seq.).
//number > "size" for an incomplete byte sequence. Never return 0.
Unt
utf_ptr2len_len(Byte const* p, int size) {
   Unt len = utf8LenTable[*p];
   if (len == 1)
      return 1;   // ZERO, ascii or illegal lead byte
   Unt m;
   if ((int)len > size)
      m = size;   // incomplete byte sequence.
   else
      m = len;
   for (Unt i = 1; i < m; ++i) {
      if ((p[i] & 0xc0) != 0x80)
         return 1;
   } 
   return len;
}

// Return the number of bytes the UTF-8 encoding of character "c" takes.
// This does not include composing characters.
Unt
mb_char2len(Unt c) {
   if (c < 0x80)
      return 1;
   if (c < 0x800)
      return 2;
   if (c < 0x10000)
      return 3;
   if (c < 0x200000)
      return 4;
   if (c < 0x4000000)
      return 5;
   return 6;
}

// Convert Unicode character "c" to UTF-8 string in "buf[]". Returns the number of bytes.
int
mb_char2bytes(Unt c, CS buf) {
   if (c < 0x80)   { // 7 bits
      buf[0] = c;
      return 1;
   }
   if (c < 0x800) {    // 11 bits
      buf[0] = 0xc0 + (c >> 6);
      buf[1] = 0x80 + (c & 0x3f);
      return 2;
   }
   if (c < 0x10000) {      // 16 bits
      buf[0] = 0xe0 + (c >> 12);
      buf[1] = 0x80 + ((c >> 6) & 0x3f);
      buf[2] = 0x80 + (c & 0x3f);
      return 3;
   }
   if (c < 0x200000) {     // 21 bits
      buf[0] = 0xf0 + (c >> 18);
      buf[1] = 0x80 + ((c >> 12) & 0x3f);
      buf[2] = 0x80 + ((c >> 6) & 0x3f);
      buf[3] = 0x80 + (c & 0x3f);
      return 4;
   }
   if (c < 0x4000000) {     // 26 bits
      buf[0] = 0xf8 + (c >> 24);
      buf[1] = 0x80 + ((c >> 18) & 0x3f);
      buf[2] = 0x80 + ((c >> 12) & 0x3f);
      buf[3] = 0x80 + ((c >> 6) & 0x3f);
      buf[4] = 0x80 + (c & 0x3f);
      return 5;
   }
   // 31 bits
   buf[0] = 0xfc + (c >> 30);
   buf[1] = 0x80 + ((c >> 24) & 0x3f);
   buf[2] = 0x80 + ((c >> 18) & 0x3f);
   buf[3] = 0x80 + ((c >> 12) & 0x3f);
   buf[4] = 0x80 + ((c >> 6) & 0x3f);
   buf[5] = 0x80 + (c & 0x3f);
   return 6;
}

// utf_iscomposing() with different argument type for libvterm.
int
utf_iscomposing_uint(Unt c) {
   return utf_iscomposing(c);
}

Boole
utf_islower(Unt a){
   // German sharp s is lower case but has no upper case equivalent.
   return (utf_toupper(a) != a) || a == 0xdf;
}

//Return the lower-case equivalent of "a", which is a UCS-4 character. Use simple case folding.
Unt
utf_tolower(Unt a) {
   // Use ASCII style tolower().
   if (a < 128)
      return TOLOWER_ASC(a);

   // If towlower() is available and handles Unicode, use it.
   return towlower(a);
}

Boole
utf_isupper(Unt a) {
   return (utf_tolower(a) != a);
}

private int
utf_strnicmp(CS s1, CS s2, Unt n1, Unt n2){
   Unt c1, c2;
   int cdiff;
   Byte buffer[6];

   for (;;) {
      c1 = utf_safe_read_char_adv(&s1, &n1);
      c2 = utf_safe_read_char_adv(&s2, &n2);

      if (c1 <= 0 || c2 <= 0)
          break;

      if (c1 == c2)
          continue;

      cdiff = utf_fold(c1) - utf_fold(c2);
      if (cdiff != 0)
         return cdiff;
   }

   //some string ended or has an incomplete/illegal character sequence

   if (c1 == 0 || c2 == 0) {
      //some string ended. shorter string is smaller
      if (c1 == 0 && c2 == 0)
          return 0;
      return c1 == 0 ? -1 : 1;
   }

   //Continue with bytewise comparison to produce some result that
   //would make comparison operations involving this function transitive.
   //
   //If only one string had an error, comparison should be made with
   //folded version of the other string. In this case it is enough
   //to fold just one character to determine the result of comparison.
   if (c1 != UNT && c2 == UNT) {
      n1 = mb_char2bytes(utf_fold(c1), buffer);
      s1 = buffer;
   } ei (c1 == UNT && c2 != UNT) {
      n2 = mb_char2bytes(utf_fold(c2), buffer);
      s2 = buffer;
   }

   while (n1 > 0 && n2 > 0 && *s1 != ZERO && *s2 != ZERO) {
      cdiff = (int)(*s1) - (int)(*s2);
      if (cdiff != 0)
         return cdiff;

      s1++;
      s2++;
      n1--;
      n2--;
   }

   if (n1 > 0 && *s1 == ZERO)
      n1 = 0;
   if (n2 > 0 && *s2 == ZERO)
      n2 = 0;

   if (n1 == 0 && n2 == 0)
      return 0;
   return n1 == 0 ? -1 : 1;
}

//Code for Unicode case-dependent operations.  Based on notes in
//http://www.unicode.org/Public/UNIDATA/CaseFolding.txt
//This code uses simple case folding, not full case folding.
//Last updated for Unicode 5.2.

//The following tables are built by ../runtime/tools/unicode.vim.
//They must be in numeric order, because we use binary search.
//An entry such as {0x41,0x5a,1,32} means that Unicode characters in the
//range from 0x41 to 0x5a inclusive, stepping by 1, are changed to folded/upper/lower by adding 32.
typedef struct {
   Unt rangeStart;
   Unt rangeEnd;
   int step;
   int offset;
} ConvertStruct;

private ConvertStruct foldCase[] = {
   {0x41,0x5a,1,32},
   {0xb5,0xb5,-1,775},
   {0xc0,0xd6,1,32},
   {0xd8,0xde,1,32},
   {0x100,0x12e,2,1},
   {0x132,0x136,2,1},
   {0x139,0x147,2,1},
   {0x14a,0x176,2,1},
   {0x178,0x178,-1,-121},
   {0x179,0x17d,2,1},
   {0x17f,0x17f,-1,-268},
   {0x181,0x181,-1,210},
   {0x182,0x184,2,1},
   {0x186,0x186,-1,206},
   {0x187,0x187,-1,1},
   {0x189,0x18a,1,205},
   {0x18b,0x18b,-1,1},
   {0x18e,0x18e,-1,79},
   {0x18f,0x18f,-1,202},
   {0x190,0x190,-1,203},
   {0x191,0x191,-1,1},
   {0x193,0x193,-1,205},
   {0x194,0x194,-1,207},
   {0x196,0x196,-1,211},
   {0x197,0x197,-1,209},
   {0x198,0x198,-1,1},
   {0x19c,0x19c,-1,211},
   {0x19d,0x19d,-1,213},
   {0x19f,0x19f,-1,214},
   {0x1a0,0x1a4,2,1},
   {0x1a6,0x1a6,-1,218},
   {0x1a7,0x1a7,-1,1},
   {0x1a9,0x1a9,-1,218},
   {0x1ac,0x1ac,-1,1},
   {0x1ae,0x1ae,-1,218},
   {0x1af,0x1af,-1,1},
   {0x1b1,0x1b2,1,217},
   {0x1b3,0x1b5,2,1},
   {0x1b7,0x1b7,-1,219},
   {0x1b8,0x1bc,4,1},
   {0x1c4,0x1c4,-1,2},
   {0x1c5,0x1c5,-1,1},
   {0x1c7,0x1c7,-1,2},
   {0x1c8,0x1c8,-1,1},
   {0x1ca,0x1ca,-1,2},
   {0x1cb,0x1db,2,1},
   {0x1de,0x1ee,2,1},
   {0x1f1,0x1f1,-1,2},
   {0x1f2,0x1f4,2,1},
   {0x1f6,0x1f6,-1,-97},
   {0x1f7,0x1f7,-1,-56},
   {0x1f8,0x21e,2,1},
   {0x220,0x220,-1,-130},
   {0x222,0x232,2,1},
   {0x23a,0x23a,-1,10795},
   {0x23b,0x23b,-1,1},
   {0x23d,0x23d,-1,-163},
   {0x23e,0x23e,-1,10792},
   {0x241,0x241,-1,1},
   {0x243,0x243,-1,-195},
   {0x244,0x244,-1,69},
   {0x245,0x245,-1,71},
   {0x246,0x24e,2,1},
   {0x345,0x345,-1,116},
   {0x370,0x372,2,1},
   {0x376,0x376,-1,1},
   {0x37f,0x37f,-1,116},
   {0x386,0x386,-1,38},
   {0x388,0x38a,1,37},
   {0x38c,0x38c,-1,64},
   {0x38e,0x38f,1,63},
   {0x391,0x3a1,1,32},
   {0x3a3,0x3ab,1,32},
   {0x3c2,0x3c2,-1,1},
   {0x3cf,0x3cf,-1,8},
   {0x3d0,0x3d0,-1,-30},
   {0x3d1,0x3d1,-1,-25},
   {0x3d5,0x3d5,-1,-15},
   {0x3d6,0x3d6,-1,-22},
   {0x3d8,0x3ee,2,1},
   {0x3f0,0x3f0,-1,-54},
   {0x3f1,0x3f1,-1,-48},
   {0x3f4,0x3f4,-1,-60},
   {0x3f5,0x3f5,-1,-64},
   {0x3f7,0x3f7,-1,1},
   {0x3f9,0x3f9,-1,-7},
   {0x3fa,0x3fa,-1,1},
   {0x3fd,0x3ff,1,-130},
   {0x400,0x40f,1,80},
   {0x410,0x42f,1,32},
   {0x460,0x480,2,1},
   {0x48a,0x4be,2,1},
   {0x4c0,0x4c0,-1,15},
   {0x4c1,0x4cd,2,1},
   {0x4d0,0x52e,2,1},
   {0x531,0x556,1,48},
   {0x10a0,0x10c5,1,7264},
   {0x10c7,0x10cd,6,7264},
   {0x13f8,0x13fd,1,-8},
   {0x1c80,0x1c80,-1,-6222},
   {0x1c81,0x1c81,-1,-6221},
   {0x1c82,0x1c82,-1,-6212},
   {0x1c83,0x1c84,1,-6210},
   {0x1c85,0x1c85,-1,-6211},
   {0x1c86,0x1c86,-1,-6204},
   {0x1c87,0x1c87,-1,-6180},
   {0x1c88,0x1c88,-1,35267},
   {0x1c89,0x1c89,-1,1},
   {0x1c90,0x1cba,1,-3008},
   {0x1cbd,0x1cbf,1,-3008},
   {0x1e00,0x1e94,2,1},
   {0x1e9b,0x1e9b,-1,-58},
   {0x1e9e,0x1e9e,-1,-7615},
   {0x1ea0,0x1efe,2,1},
   {0x1f08,0x1f0f,1,-8},
   {0x1f18,0x1f1d,1,-8},
   {0x1f28,0x1f2f,1,-8},
   {0x1f38,0x1f3f,1,-8},
   {0x1f48,0x1f4d,1,-8},
   {0x1f59,0x1f5f,2,-8},
   {0x1f68,0x1f6f,1,-8},
   {0x1f88,0x1f8f,1,-8},
   {0x1f98,0x1f9f,1,-8},
   {0x1fa8,0x1faf,1,-8},
   {0x1fb8,0x1fb9,1,-8},
   {0x1fba,0x1fbb,1,-74},
   {0x1fbc,0x1fbc,-1,-9},
   {0x1fbe,0x1fbe,-1,-7173},
   {0x1fc8,0x1fcb,1,-86},
   {0x1fcc,0x1fcc,-1,-9},
   {0x1fd3,0x1fd3,-1,-7235},
   {0x1fd8,0x1fd9,1,-8},
   {0x1fda,0x1fdb,1,-100},
   {0x1fe3,0x1fe3,-1,-7219},
   {0x1fe8,0x1fe9,1,-8},
   {0x1fea,0x1feb,1,-112},
   {0x1fec,0x1fec,-1,-7},
   {0x1ff8,0x1ff9,1,-128},
   {0x1ffa,0x1ffb,1,-126},
   {0x1ffc,0x1ffc,-1,-9},
   {0x2126,0x2126,-1,-7517},
   {0x212a,0x212a,-1,-8383},
   {0x212b,0x212b,-1,-8262},
   {0x2132,0x2132,-1,28},
   {0x2160,0x216f,1,16},
   {0x2183,0x2183,-1,1},
   {0x24b6,0x24cf,1,26},
   {0x2c00,0x2c2f,1,48},
   {0x2c60,0x2c60,-1,1},
   {0x2c62,0x2c62,-1,-10743},
   {0x2c63,0x2c63,-1,-3814},
   {0x2c64,0x2c64,-1,-10727},
   {0x2c67,0x2c6b,2,1},
   {0x2c6d,0x2c6d,-1,-10780},
   {0x2c6e,0x2c6e,-1,-10749},
   {0x2c6f,0x2c6f,-1,-10783},
   {0x2c70,0x2c70,-1,-10782},
   {0x2c72,0x2c75,3,1},
   {0x2c7e,0x2c7f,1,-10815},
   {0x2c80,0x2ce2,2,1},
   {0x2ceb,0x2ced,2,1},
   {0x2cf2,0xa640,31054,1},
   {0xa642,0xa66c,2,1},
   {0xa680,0xa69a,2,1},
   {0xa722,0xa72e,2,1},
   {0xa732,0xa76e,2,1},
   {0xa779,0xa77b,2,1},
   {0xa77d,0xa77d,-1,-35332},
   {0xa77e,0xa786,2,1},
   {0xa78b,0xa78b,-1,1},
   {0xa78d,0xa78d,-1,-42280},
   {0xa790,0xa792,2,1},
   {0xa796,0xa7a8,2,1},
   {0xa7aa,0xa7aa,-1,-42308},
   {0xa7ab,0xa7ab,-1,-42319},
   {0xa7ac,0xa7ac,-1,-42315},
   {0xa7ad,0xa7ad,-1,-42305},
   {0xa7ae,0xa7ae,-1,-42308},
   {0xa7b0,0xa7b0,-1,-42258},
   {0xa7b1,0xa7b1,-1,-42282},
   {0xa7b2,0xa7b2,-1,-42261},
   {0xa7b3,0xa7b3,-1,928},
   {0xa7b4,0xa7c2,2,1},
   {0xa7c4,0xa7c4,-1,-48},
   {0xa7c5,0xa7c5,-1,-42307},
   {0xa7c6,0xa7c6,-1,-35384},
   {0xa7c7,0xa7c9,2,1},
   {0xa7cb,0xa7cb,-1,-42343},
   {0xa7cc,0xa7d0,4,1},
   {0xa7d6,0xa7da,2,1},
   {0xa7dc,0xa7dc,-1,-42561},
   {0xa7f5,0xa7f5,-1,1},
   {0xab70,0xabbf,1,-38864},
   {0xfb05,0xfb05,-1,1},
   {0xff21,0xff3a,1,32},
   {0x10400,0x10427,1,40},
   {0x104b0,0x104d3,1,40},
   {0x10570,0x1057a,1,39},
   {0x1057c,0x1058a,1,39},
   {0x1058c,0x10592,1,39},
   {0x10594,0x10595,1,39},
   {0x10c80,0x10cb2,1,64},
   {0x10d50,0x10d65,1,32},
   {0x118a0,0x118bf,1,32},
   {0x16e40,0x16e5f,1,32},
   {0x1e900,0x1e921,1,34}
};

//Generic conversion function for case operations.
//Return the converted equivalent of "a", which is a UCS-4 character.  Use
//the given conversion "table".  Uses binary search on "table".
private Unt
utf_convert(Unt a, ConvertStruct table[], int tableSize) {
   int entries = tableSize / sizeof(ConvertStruct);
   int start = 0;
   int end = entries;
   while (start < end) {
      // need to search further
      int mid = (end + start) / 2;
      if (table[mid].rangeEnd < a)
         start = mid + 1;
      else
         end = mid;
   }
   if (start < entries
          && table[start].rangeStart <= a
          && a <= table[start].rangeEnd
          && (a - table[start].rangeStart) % table[start].step == 0)
      return (Unt)((int)a + table[start].offset);
   else
      return a;
}

//Return the folded-case equivalent of "a", which is a UCS-4 character. Uses simple case folding.
Unt
utf_fold(Unt a) {
   if (a < 0x80)
      // be fast for ASCII
      return a >= 0x41 && a <= 0x5a ? a + 32 : a;
   return utf_convert(a, foldCase, (int)sizeof(foldCase));
}

//Return the upper-case equivalent of "a", which is a UCS-4 character.  Use simple case folding.
Unt
utf_toupper(Unt a) {
   // Use ASCII style toupper().
   if (a < 128)
      return TOUPPER_ASC(a);

   //If towupper() is available and handles Unicode, use it.
   return towupper(a);
}

//Version of strnicmp() that handles multi-byte characters.
//Needed for Big5, Shift-JIS and UTF-8 encoding. Return zero if s1 and s2 are equal (ignoring case),
//the difference between two characters otherwise.
int
caseInsensitiveCompareNChars2(CS s1, CS s2, Unt n1, Unt n2) {
   if (n1 == n2)
      return caseInsensitiveCompareNChars(s1, s2, n1);
   else
      return utf_strnicmp(s1, s2, n1, n2);
}

int
caseInsensitiveCompareNChars(CS s1, CS s2, Unt nn) {
   return utf_strnicmp(s1, s2, nn, nn);
}

typedef struct {
   long first;
   long last;
} Interval;

// Return true if "c" is in the sorted "table[size / sizeof(Interval)]".
private Boole
intable(Arr(Interval) table, Unt size, Unt c) {
   // first quick check for Latin1 etc. characters
   if ((long)c < table[0].first)
      return false;

   // binary search in table
   int bot = 0;
   int top = (int)(size / sizeof(Interval) - 1);
   while (top >= bot) {
      int mid = (bot + top) / 2;
      if ((Unt)table[mid].last < c)
         bot = mid + 1;
      ei ((Unt)table[mid].first > c)
         top = mid - 1;
      else
         return true;
   }
   return false;
}

// Sorted list of non-overlapping intervals of all Emoji characters,
// based on http://unicode.org/emoji/charts/emoji-list.html
// Generated by ../runtime/tools/unicode.vim.
// Excludes 0x00a9 and 0x00ae because they are considered latin1.
private Interval emoji_all[] = {
    {0x203c, 0x203c},
    {0x2049, 0x2049},
    {0x2122, 0x2122},
    {0x2139, 0x2139},
    {0x2194, 0x2199},
    {0x21a9, 0x21aa},
    {0x231a, 0x231b},
    {0x2328, 0x2328},
    {0x23cf, 0x23cf},
    {0x23e9, 0x23f3},
    {0x23f8, 0x23fa},
    {0x24c2, 0x24c2},
    {0x25aa, 0x25ab},
    {0x25b6, 0x25b6},
    {0x25c0, 0x25c0},
    {0x25fb, 0x25fe},
    {0x2600, 0x2604},
    {0x260e, 0x260e},
    {0x2611, 0x2611},
    {0x2614, 0x2615},
    {0x2618, 0x2618},
    {0x261d, 0x261d},
    {0x2620, 0x2620},
    {0x2622, 0x2623},
    {0x2626, 0x2626},
    {0x262a, 0x262a},
    {0x262e, 0x262f},
    {0x2638, 0x263a},
    {0x2640, 0x2640},
    {0x2642, 0x2642},
    {0x2648, 0x2653},
    {0x265f, 0x2660},
    {0x2663, 0x2663},
    {0x2665, 0x2666},
    {0x2668, 0x2668},
    {0x267b, 0x267b},
    {0x267e, 0x267f},
    {0x2692, 0x2697},
    {0x2699, 0x2699},
    {0x269b, 0x269c},
    {0x26a0, 0x26a1},
    {0x26a7, 0x26a7},
    {0x26aa, 0x26ab},
    {0x26b0, 0x26b1},
    {0x26bd, 0x26be},
    {0x26c4, 0x26c5},
    {0x26c8, 0x26c8},
    {0x26ce, 0x26cf},
    {0x26d1, 0x26d1},
    {0x26d3, 0x26d4},
    {0x26e9, 0x26ea},
    {0x26f0, 0x26f5},
    {0x26f7, 0x26fa},
    {0x26fd, 0x26fd},
    {0x2702, 0x2702},
    {0x2705, 0x2705},
    {0x2708, 0x270d},
    {0x270f, 0x270f},
    {0x2712, 0x2712},
    {0x2714, 0x2714},
    {0x2716, 0x2716},
    {0x271d, 0x271d},
    {0x2721, 0x2721},
    {0x2728, 0x2728},
    {0x2733, 0x2734},
    {0x2744, 0x2744},
    {0x2747, 0x2747},
    {0x274c, 0x274c},
    {0x274e, 0x274e},
    {0x2753, 0x2755},
    {0x2757, 0x2757},
    {0x2763, 0x2764},
    {0x2795, 0x2797},
    {0x27a1, 0x27a1},
    {0x27b0, 0x27b0},
    {0x27bf, 0x27bf},
    {0x2934, 0x2935},
    {0x2b05, 0x2b07},
    {0x2b1b, 0x2b1c},
    {0x2b50, 0x2b50},
    {0x2b55, 0x2b55},
    {0x3030, 0x3030},
    {0x303d, 0x303d},
    {0x3297, 0x3297},
    {0x3299, 0x3299},
    {0x1f004, 0x1f004},
    {0x1f0cf, 0x1f0cf},
    {0x1f170, 0x1f171},
    {0x1f17e, 0x1f17f},
    {0x1f18e, 0x1f18e},
    {0x1f191, 0x1f19a},
    {0x1f1e6, 0x1f1ff},
    {0x1f201, 0x1f202},
    {0x1f21a, 0x1f21a},
    {0x1f22f, 0x1f22f},
    {0x1f232, 0x1f23a},
    {0x1f250, 0x1f251},
    {0x1f300, 0x1f321},
    {0x1f324, 0x1f393},
    {0x1f396, 0x1f397},
    {0x1f399, 0x1f39b},
    {0x1f39e, 0x1f3f0},
    {0x1f3f3, 0x1f3f5},
    {0x1f3f7, 0x1f4fd},
    {0x1f4ff, 0x1f53d},
    {0x1f549, 0x1f54e},
    {0x1f550, 0x1f567},
    {0x1f56f, 0x1f570},
    {0x1f573, 0x1f57a},
    {0x1f587, 0x1f587},
    {0x1f58a, 0x1f58d},
    {0x1f590, 0x1f590},
    {0x1f595, 0x1f596},
    {0x1f5a4, 0x1f5a5},
    {0x1f5a8, 0x1f5a8},
    {0x1f5b1, 0x1f5b2},
    {0x1f5bc, 0x1f5bc},
    {0x1f5c2, 0x1f5c4},
    {0x1f5d1, 0x1f5d3},
    {0x1f5dc, 0x1f5de},
    {0x1f5e1, 0x1f5e1},
    {0x1f5e3, 0x1f5e3},
    {0x1f5e8, 0x1f5e8},
    {0x1f5ef, 0x1f5ef},
    {0x1f5f3, 0x1f5f3},
    {0x1f5fa, 0x1f64f},
    {0x1f680, 0x1f6c5},
    {0x1f6cb, 0x1f6d2},
    {0x1f6d5, 0x1f6d7},
    {0x1f6dc, 0x1f6e5},
    {0x1f6e9, 0x1f6e9},
    {0x1f6eb, 0x1f6ec},
    {0x1f6f0, 0x1f6f0},
    {0x1f6f3, 0x1f6fc},
    {0x1f7e0, 0x1f7eb},
    {0x1f7f0, 0x1f7f0},
    {0x1f90c, 0x1f93a},
    {0x1f93c, 0x1f945},
    {0x1f947, 0x1f9ff},
    {0x1fa70, 0x1fa7c},
    {0x1fa80, 0x1fa88},
    {0x1fa90, 0x1fabd},
    {0x1fabf, 0x1fac5},
    {0x1face, 0x1fadb},
    {0x1fae0, 0x1fae8},
    {0x1faf0, 0x1faf8}
};


Boole
strInEmojiTable(Unt c) {
   return intable(emoji_all, sizeof(emoji_all), c);
}

Boole
strInDoubleWidthTable(Unt c) {
   // Sorted list of non-overlapping intervals of East Asian double width
   // characters, generated with ../runtime/tools/unicode.vim.
   static Interval doublewidth[] = {
      {0x1100, 0x115f},
      {0x231a, 0x231b},
      {0x2329, 0x232a},
      {0x23e9, 0x23ec},
      {0x23f0, 0x23f0},
      {0x23f3, 0x23f3},
      {0x25fd, 0x25fe},
      {0x2614, 0x2615},
      {0x2630, 0x2637},
      {0x2648, 0x2653},
      {0x267f, 0x267f},
      {0x268a, 0x268f},
      {0x2693, 0x2693},
      {0x26a1, 0x26a1},
      {0x26aa, 0x26ab},
      {0x26bd, 0x26be},
      {0x26c4, 0x26c5},
      {0x26ce, 0x26ce},
      {0x26d4, 0x26d4},
      {0x26ea, 0x26ea},
      {0x26f2, 0x26f3},
      {0x26f5, 0x26f5},
      {0x26fa, 0x26fa},
      {0x26fd, 0x26fd},
      {0x2705, 0x2705},
      {0x270a, 0x270b},
      {0x2728, 0x2728},
      {0x274c, 0x274c},
      {0x274e, 0x274e},
      {0x2753, 0x2755},
      {0x2757, 0x2757},
      {0x2795, 0x2797},
      {0x27b0, 0x27b0},
      {0x27bf, 0x27bf},
      {0x2b1b, 0x2b1c},
      {0x2b50, 0x2b50},
      {0x2b55, 0x2b55},
      {0x2e80, 0x2e99},
      {0x2e9b, 0x2ef3},
      {0x2f00, 0x2fd5},
      {0x2ff0, 0x303e},
      {0x3041, 0x3096},
      {0x3099, 0x30ff},
      {0x3105, 0x312f},
      {0x3131, 0x318e},
      {0x3190, 0x31e5},
      {0x31ef, 0x321e},
      {0x3220, 0x3247},
      {0x3250, 0xa48c},
      {0xa490, 0xa4c6},
      {0xa960, 0xa97c},
      {0xac00, 0xd7a3},
      {0xf900, 0xfaff},
      {0xfe10, 0xfe19},
      {0xfe30, 0xfe52},
      {0xfe54, 0xfe66},
      {0xfe68, 0xfe6b},
      {0xff01, 0xff60},
      {0xffe0, 0xffe6},
      {0x16fe0, 0x16fe3},
      {0x16ff0, 0x16ff1},
      {0x17000, 0x187f7},
      {0x18800, 0x18cd5},
      {0x18cff, 0x18d08},
      {0x1aff0, 0x1aff3},
      {0x1aff5, 0x1affb},
      {0x1affd, 0x1affe},
      {0x1b000, 0x1b122},
      {0x1b132, 0x1b132},
      {0x1b150, 0x1b152},
      {0x1b155, 0x1b155},
      {0x1b164, 0x1b167},
      {0x1b170, 0x1b2fb},
      {0x1d300, 0x1d356},
      {0x1d360, 0x1d376},
      {0x1f004, 0x1f004},
      {0x1f0cf, 0x1f0cf},
      {0x1f18e, 0x1f18e},
      {0x1f191, 0x1f19a},
      {0x1f200, 0x1f202},
      {0x1f210, 0x1f23b},
      {0x1f240, 0x1f248},
      {0x1f250, 0x1f251},
      {0x1f260, 0x1f265},
      {0x1f300, 0x1f320},
      {0x1f32d, 0x1f335},
      {0x1f337, 0x1f37c},
      {0x1f37e, 0x1f393},
      {0x1f3a0, 0x1f3ca},
      {0x1f3cf, 0x1f3d3},
      {0x1f3e0, 0x1f3f0},
      {0x1f3f4, 0x1f3f4},
      {0x1f3f8, 0x1f43e},
      {0x1f440, 0x1f440},
      {0x1f442, 0x1f4fc},
      {0x1f4ff, 0x1f53d},
      {0x1f54b, 0x1f54e},
      {0x1f550, 0x1f567},
      {0x1f57a, 0x1f57a},
      {0x1f595, 0x1f596},
      {0x1f5a4, 0x1f5a4},
      {0x1f5fb, 0x1f64f},
      {0x1f680, 0x1f6c5},
      {0x1f6cc, 0x1f6cc},
      {0x1f6d0, 0x1f6d2},
      {0x1f6d5, 0x1f6d7},
      {0x1f6dc, 0x1f6df},
      {0x1f6eb, 0x1f6ec},
      {0x1f6f4, 0x1f6fc},
      {0x1f7e0, 0x1f7eb},
      {0x1f7f0, 0x1f7f0},
      {0x1f90c, 0x1f93a},
      {0x1f93c, 0x1f945},
      {0x1f947, 0x1f9ff},
      {0x1fa70, 0x1fa7c},
      {0x1fa80, 0x1fa89},
      {0x1fa8f, 0x1fac6},
      {0x1face, 0x1fadc},
      {0x1fadf, 0x1fae9},
      {0x1faf0, 0x1faf8},
      {0x20000, 0x2fffd},
      {0x30000, 0x3fffd}
   };

   return intable(doublewidth, sizeof(doublewidth), c);
}

// Sorted list of non-overlapping intervals of East Asian Ambiguous
// characters, generated with ../runtime/tools/unicode.vim.
private Interval ambiguous[] = {
   {0x00a1, 0x00a1},
   {0x00a4, 0x00a4},
   {0x00a7, 0x00a8},
   {0x00aa, 0x00aa},
   {0x00ad, 0x00ae},
   {0x00b0, 0x00b4},
   {0x00b6, 0x00ba},
   {0x00bc, 0x00bf},
   {0x00c6, 0x00c6},
   {0x00d0, 0x00d0},
   {0x00d7, 0x00d8},
   {0x00de, 0x00e1},
   {0x00e6, 0x00e6},
   {0x00e8, 0x00ea},
   {0x00ec, 0x00ed},
   {0x00f0, 0x00f0},
   {0x00f2, 0x00f3},
   {0x00f7, 0x00fa},
   {0x00fc, 0x00fc},
   {0x00fe, 0x00fe},
   {0x0101, 0x0101},
   {0x0111, 0x0111},
   {0x0113, 0x0113},
   {0x011b, 0x011b},
   {0x0126, 0x0127},
   {0x012b, 0x012b},
   {0x0131, 0x0133},
   {0x0138, 0x0138},
   {0x013f, 0x0142},
   {0x0144, 0x0144},
   {0x0148, 0x014b},
   {0x014d, 0x014d},
   {0x0152, 0x0153},
   {0x0166, 0x0167},
   {0x016b, 0x016b},
   {0x01ce, 0x01ce},
   {0x01d0, 0x01d0},
   {0x01d2, 0x01d2},
   {0x01d4, 0x01d4},
   {0x01d6, 0x01d6},
   {0x01d8, 0x01d8},
   {0x01da, 0x01da},
   {0x01dc, 0x01dc},
   {0x0251, 0x0251},
   {0x0261, 0x0261},
   {0x02c4, 0x02c4},
   {0x02c7, 0x02c7},
   {0x02c9, 0x02cb},
   {0x02cd, 0x02cd},
   {0x02d0, 0x02d0},
   {0x02d8, 0x02db},
   {0x02dd, 0x02dd},
   {0x02df, 0x02df},
   {0x0300, 0x036f},
   {0x0391, 0x03a1},
   {0x03a3, 0x03a9},
   {0x03b1, 0x03c1},
   {0x03c3, 0x03c9},
   {0x0401, 0x0401},
   {0x0410, 0x044f},
   {0x0451, 0x0451},
   {0x2010, 0x2010},
   {0x2013, 0x2016},
   {0x2018, 0x2019},
   {0x201c, 0x201d},
   {0x2020, 0x2022},
   {0x2024, 0x2027},
   {0x2030, 0x2030},
   {0x2032, 0x2033},
   {0x2035, 0x2035},
   {0x203b, 0x203b},
   {0x203e, 0x203e},
   {0x2074, 0x2074},
   {0x207f, 0x207f},
   {0x2081, 0x2084},
   {0x20ac, 0x20ac},
   {0x2103, 0x2103},
   {0x2105, 0x2105},
   {0x2109, 0x2109},
   {0x2113, 0x2113},
   {0x2116, 0x2116},
   {0x2121, 0x2122},
   {0x2126, 0x2126},
   {0x212b, 0x212b},
   {0x2153, 0x2154},
   {0x215b, 0x215e},
   {0x2160, 0x216b},
   {0x2170, 0x2179},
   {0x2189, 0x2189},
   {0x2190, 0x2199},
   {0x21b8, 0x21b9},
   {0x21d2, 0x21d2},
   {0x21d4, 0x21d4},
   {0x21e7, 0x21e7},
   {0x2200, 0x2200},
   {0x2202, 0x2203},
   {0x2207, 0x2208},
   {0x220b, 0x220b},
   {0x220f, 0x220f},
   {0x2211, 0x2211},
   {0x2215, 0x2215},
   {0x221a, 0x221a},
   {0x221d, 0x2220},
   {0x2223, 0x2223},
   {0x2225, 0x2225},
   {0x2227, 0x222c},
   {0x222e, 0x222e},
   {0x2234, 0x2237},
   {0x223c, 0x223d},
   {0x2248, 0x2248},
   {0x224c, 0x224c},
   {0x2252, 0x2252},
   {0x2260, 0x2261},
   {0x2264, 0x2267},
   {0x226a, 0x226b},
   {0x226e, 0x226f},
   {0x2282, 0x2283},
   {0x2286, 0x2287},
   {0x2295, 0x2295},
   {0x2299, 0x2299},
   {0x22a5, 0x22a5},
   {0x22bf, 0x22bf},
   {0x2312, 0x2312},
   {0x2460, 0x24e9},
   {0x24eb, 0x254b},
   {0x2550, 0x2573},
   {0x2580, 0x258f},
   {0x2592, 0x2595},
   {0x25a0, 0x25a1},
   {0x25a3, 0x25a9},
   {0x25b2, 0x25b3},
   {0x25b6, 0x25b7},
   {0x25bc, 0x25bd},
   {0x25c0, 0x25c1},
   {0x25c6, 0x25c8},
   {0x25cb, 0x25cb},
   {0x25ce, 0x25d1},
   {0x25e2, 0x25e5},
   {0x25ef, 0x25ef},
   {0x2605, 0x2606},
   {0x2609, 0x2609},
   {0x260e, 0x260f},
   {0x261c, 0x261c},
   {0x261e, 0x261e},
   {0x2640, 0x2640},
   {0x2642, 0x2642},
   {0x2660, 0x2661},
   {0x2663, 0x2665},
   {0x2667, 0x266a},
   {0x266c, 0x266d},
   {0x266f, 0x266f},
   {0x269e, 0x269f},
   {0x26bf, 0x26bf},
   {0x26c6, 0x26cd},
   {0x26cf, 0x26d3},
   {0x26d5, 0x26e1},
   {0x26e3, 0x26e3},
   {0x26e8, 0x26e9},
   {0x26eb, 0x26f1},
   {0x26f4, 0x26f4},
   {0x26f6, 0x26f9},
   {0x26fb, 0x26fc},
   {0x26fe, 0x26ff},
   {0x273d, 0x273d},
   {0x2776, 0x277f},
   {0x2b56, 0x2b59},
   {0x3248, 0x324f},
   {0xe000, 0xf8ff},
   {0xfe00, 0xfe0f},
   {0xfffd, 0xfffd},
   {0x1f100, 0x1f10a},
   {0x1f110, 0x1f12d},
   {0x1f130, 0x1f169},
   {0x1f170, 0x1f18d},
   {0x1f18f, 0x1f190},
   {0x1f19b, 0x1f1ac},
   {0xe0100, 0xe01ef},
   {0xf0000, 0xffffd},
   {0x100000, 0x10fffd}
};

//Get character at **pp and advance *pp to the next character.
//Note: composing characters are skipped!
Unt
strAdvanceMultibyte(OUT CS* pp) {
   Unt c = mb_ptr2char(*pp);
   *pp += utfCharLen(*pp);
   return c;
}

//Get character at **pp and advance *pp to the next character.
//Note: composing characters are returned as separate characters.
Unt
mb_cptr2char_adv(OUT CS* pp) {
   Unt c = mb_ptr2char(*pp);
   *pp += utf_ptr2len(*pp);
   return c;
}

int
utf_ambiguous_width(Unt c) {
    return c >= 0x80 && (intable(ambiguous, sizeof(ambiguous), c)
       || intable(emoji_all, sizeof(emoji_all), c));
}

//Return offset from "p" to the start of a character, including composing
//characters. "base" must be the start of the C string.
int
mb_head_off(CS base, CS p) {
   CS q;
   CS s;
   int len;

   if (*p < 0x80)      // be quick for ASCII
      return 0;

   // Skip backwards over trailing bytes: 10xx.xxxx. Skip backwards again if on a composing char.
   for (q = p; ; --q) {
      // Move s to the last byte of this char.
      for (s = q; (s[1] & 0xc0) == 0x80; ++s)
         {}
      // Move q to the first byte of this char.
      while (q > base && (*q & 0xc0) == 0x80)
         --q;
      // Check for illegal sequence. Do allow an illegal byte after where we started.
      len = utf8LenTable[*q];
      if (len != (int)(s - q + 1) && len != (int)(p - q + 1))
         return 0;

      if (q <= base)
         break;

      Unt c = mb_ptr2char(q);
      if (utf_iscomposing(c))
         continue;

      break;
   }

   return (int)(p - q);
}

// Whether space is NOT allowed before/after 'c'.
int
utf_eat_space(int cc){
    return ((cc >= 0x2000 && cc <= 0x206F)   // General punctuations
       || (cc >= 0x2e00 && cc <= 0x2e7f)   // Supplemental punctuations
       || (cc >= 0x3000 && cc <= 0x303f)   // CJK symbols and punctuations
       || (cc >= 0xff01 && cc <= 0xff0f)   // Full width ASCII punctuations
       || (cc >= 0xff1a && cc <= 0xff20)   // ..
       || (cc >= 0xff3b && cc <= 0xff40)   // ..
       || (cc >= 0xff5b && cc <= 0xff65));   // ..
}

// Whether line break is allowed before "cc".
Boole
utf_allow_break_before(Unt cc) {
   static const Unt BOL_prohibition_punct[] = {
      '!',
      '%', //(
      ')',
      ',',
      ':',
      ';',
      '>',
      '?', //[
      ']', //{
      '}',
      0x2019, // ’ right single quotation mark
      0x201d, // ” right double quotation mark
      0x2020, // † dagger
      0x2021, // ‡ double dagger
      0x2026, // … horizontal ellipsis
      0x2030, // ‰ per mille sign
      0x2031, // ‱ per ten thousand sign
      0x203c, // ‼ double exclamation mark
      0x2047, // ⁇ double question mark
      0x2048, // ⁈ question exclamation mark
      0x2049, // ⁉ exclamation question mark
      0x2103, // ℃ degree celsius
      0x2109, // ℉ degree fahrenheit
      0x3001, // 、 ideographic comma
      0x3002, // 。 ideographic full stop
      0x3009, // 〉 right angle bracket
      0x300b, // 》 right double angle bracket
      0x300d, // 」 right corner bracket
      0x300f, // 』 right white corner bracket
      0x3011, // 】 right black lenticular bracket
      0x3015, // 〕 right tortoise shell bracket
      0x3017, // 〗 right white lenticular bracket
      0x3019, // 〙 right white tortoise shell bracket
      0x301b, // 〛 right white square bracket
      0xff01, // ！ fullwidth exclamation mark
      0xff09, // ） fullwidth right parenthesis
      0xff0c, // ， fullwidth comma
      0xff0e, // ． fullwidth full stop
      0xff1a, // ： fullwidth colon
      0xff1b, // ； fullwidth semicolon
      0xff1f, // ？ fullwidth question mark
      0xff3d, // ］ fullwidth right square bracket
      0xff5d, // ｝ fullwidth right curly bracket
   };

   int first = 0;
   int last  = ARRAY_LENGTH(BOL_prohibition_punct) - 1;
   int mid   = 0;

   while (first < last) {
      mid = (first + last)/2;

      if (cc == BOL_prohibition_punct[mid])
         return false;
      ei (cc > BOL_prohibition_punct[mid])
         first = mid + 1;
      else
         last = mid - 1;
   }

   return cc != BOL_prohibition_punct[first];
}

//Whether line break is allowed after "cc".
private Boole
utf_allow_break_after(Unt cc) {
   static const Unt EOL_prohibition_punct[] = {
      '(', //)
      '<',
      '[', //]
      '`',
      '{', //}
      //0x2014, // — em dash
      0x2018, // ‘ left single quotation mark
      0x201c, // “ left double quotation mark
      //0x2053, // ～ swung dash
      0x3008, // 〈 left angle bracket
      0x300a, // 《 left double angle bracket
      0x300c, // 「 left corner bracket
      0x300e, // 『 left white corner bracket
      0x3010, // 【 left black lenticular bracket
      0x3014, // 〔 left tortoise shell bracket
      0x3016, // 〖 left white lenticular bracket
      0x3018, // 〘 left white tortoise shell bracket
      0x301a, // 〚 left white square bracket
      0xff08, // （ fullwidth left parenthesis
      0xff3b, // ［ fullwidth left square bracket
      0xff5b, // ｛ fullwidth left curly bracket
   };

   int first = 0;
   int last  = ARRAY_LENGTH(EOL_prohibition_punct) - 1;
   int mid   = 0;

   while (first < last) {
      mid = (first + last)/2;

      if (cc == EOL_prohibition_punct[mid])
         return false;
      ei (cc > EOL_prohibition_punct[mid])
         first = mid + 1;
      else
         last = mid - 1;
    }

    return cc != EOL_prohibition_punct[first];
}

//Whether line break is allowed between "cc" and "ncc".
int
utf_allow_break(Unt cc, Unt ncc) {
   // don't break between two-letter punctuations
   if (cc == ncc
       && (cc == 0x2014 // em dash
         || cc == 0x2026)) // horizontal ellipsis
      return false;

   return utf_allow_break_after(cc) && utf_allow_break_before(ncc);
}

// Copy a character from "*fp" to "*tp" and advance the pointers.
void
mb_copy_char(OUT CS* fp, OUT CS* tp) {
   int l = utfCharLen(*fp);

   MEMMOVE(*tp, *fp, (Unt)l);
   *tp += l;
   *fp += l;
}

//Return the offset from "p" to the first byte of a character.  When "p" is at the start of a 
//character 0 is returned, otherwise the offset to the next character.  Can start anywhere in a 
//stream of bytes.
int
mb_off_next(CS base, CS p) {
   int head_off = mb_head_off(base, p);

   if (head_off == 0)
      return 0;

   return utfCharLen(p - head_off) - head_off;
}

//Return the offset from "p" to the last byte of the character it points
//into.  Can start anywhere in a stream of bytes.
//Composing characters are not included.
int
mb_tail_off(CS base, CS p) {
   if (*p == ZERO)
      return 0;

   // Find the last character that is 10xx.xxxx
   int i;
   for (i = 0; (p[i + 1] & 0xc0) == 0x80; ++i)
       ;
   // Check for illegal sequence.
   int j;
   for (j = 0; p - j > base; ++j) {
      if ((p[-j] & 0xc0) != 0x80)
         break;
   } 
   if (utf8LenTable[p[-j]] != i + j + 1)
      return 0;
   return i;
}

//Return true if string "s" is a valid utf-8 string. When "end" is NULL stop at the first 
//ZERO. Otherwise stop at "end".
int
utf_valid_string(CS s, CS end) {
   CS p = s;

   while (end == NULL ? *p != ZERO : p < end) {
      int l = utf8LenTable_zero[*p];
      if (l == 0)
          return false;   // invalid lead byte
      if (end != NULL && p + l > end)
          return false;   // incomplete byte sequence
      ++p;
      while (--l > 0)
          if ((*p++ & 0xc0) != 0x80)
         return false;   // invalid trail byte
   }
   return true;
}

// Return a pointer to the character before "*p", if there is one.
CS
mb_prevptr(CS line, CS p) {   // start of the string
   if (p > line)
      MB_PTR_BACK(line, p);
   return p;
}

//Return the character length of "str". Each multi-byte character (with
//following composing characters) counts as one.
int
mb_charlen(CS str) {
   if (!str)
      return 0;

   int count;
   CS p = str;
   for (count = 0; *p != ZERO; count++)
      p += utfCharLen(p);

   return count;
}

// Like mb_charlen() but for a string with specified length.
int
mb_charlen_len(CS str, int len) {
   CS p = str;
   int count;

   for (count = 0; *p != ZERO && p < str + len; count++)
      p += utfCharLen(p);

   return count;
}

//Try to un-escape a multi-byte character.
//Used for the "to" and "from" part of a mapping.
//Return the un-escaped string if it is a multi-byte character, and advance
//"pp" to just after the bytes that formed it. Return NULL if no multi-byte char was found.
CS
mb_unescape(OUT CS* pp) {
   static Byte   buf[6];
   int n;
   int m = 0;
   CS str = *pp;

   // Must translate K_SPECIAL KS_SPECIAL KE_FILLER to K_SPECIAL and CSI
   // KS_EXTRA KE_CSI to CSI.
   // Maximum length of a utf-8 character is 4 bytes.
   for (n = 0; str[n] != ZERO && m < 4; ++n) {
      if (str[n] == K_SPECIAL && str[n + 1] == KS_SPECIAL && str[n + 2] == KE_FILLER) {
         buf[m++] = K_SPECIAL;
         n += 2;
      } ei ((str[n] == K_SPECIAL) && str[n + 1] == KS_EXTRA && str[n + 2] == (int)KE_CSI) {
         buf[m++] = CSI;
         n += 2;
      } ei (str[n] == K_SPECIAL) {
         break;      // a special key can't be a multibyte char
      } else {
          buf[m++] = str[n];
      } 
      buf[m] = ZERO;

      //Return a multi-byte character if it's found.  An illegal sequence will result in a 1 here.
      if (utfCharLen(buf) > 1) {
         *pp = str + n + 1;
         return buf;
      }

      // Bail out quickly for ASCII.
      if (buf[0] < 128)
         break;
    }
    return NULL;
}

#include <langinfo.h>

//Return true if "c" is a composing UTF-8 character.  This means it will be
//drawn on top of the preceding character. Based on code from Markus Kuhn.
Boole
utf_iscomposing(Unt c) {
   // Sorted list of non-overlapping intervals.
   // Generated by ../runtime/tools/unicode.vim.
   static Interval combining[] = {
      {0x0300, 0x036f},
      {0x0483, 0x0489},
      {0x0591, 0x05bd},
      {0x05bf, 0x05bf},
      {0x05c1, 0x05c2},
      {0x05c4, 0x05c5},
      {0x05c7, 0x05c7},
      {0x0610, 0x061a},
      {0x064b, 0x065f},
      {0x0670, 0x0670},
      {0x06d6, 0x06dc},
      {0x06df, 0x06e4},
      {0x06e7, 0x06e8},
      {0x06ea, 0x06ed},
      {0x0711, 0x0711},
      {0x0730, 0x074a},
      {0x07a6, 0x07b0},
      {0x07eb, 0x07f3},
      {0x07fd, 0x07fd},
      {0x0816, 0x0819},
      {0x081b, 0x0823},
      {0x0825, 0x0827},
      {0x0829, 0x082d},
      {0x0859, 0x085b},
      {0x0897, 0x089f},
      {0x08ca, 0x08e1},
      {0x08e3, 0x0902},
      {0x093a, 0x093a},
      {0x093c, 0x093c},
      {0x0941, 0x0948},
      {0x094d, 0x094d},
      {0x0951, 0x0957},
      {0x0962, 0x0963},
      {0x0981, 0x0981},
      {0x09bc, 0x09bc},
      {0x09c1, 0x09c4},
      {0x09cd, 0x09cd},
      {0x09e2, 0x09e3},
      {0x09fe, 0x09fe},
      {0x0a01, 0x0a02},
      {0x0a3c, 0x0a3c},
      {0x0a41, 0x0a42},
      {0x0a47, 0x0a48},
      {0x0a4b, 0x0a4d},
      {0x0a51, 0x0a51},
      {0x0a70, 0x0a71},
      {0x0a75, 0x0a75},
      {0x0a81, 0x0a82},
      {0x0abc, 0x0abc},
      {0x0ac1, 0x0ac5},
      {0x0ac7, 0x0ac8},
      {0x0acd, 0x0acd},
      {0x0ae2, 0x0ae3},
      {0x0afa, 0x0aff},
      {0x0b01, 0x0b01},
      {0x0b3c, 0x0b3c},
      {0x0b3f, 0x0b3f},
      {0x0b41, 0x0b44},
      {0x0b4d, 0x0b4d},
      {0x0b55, 0x0b56},
      {0x0b62, 0x0b63},
      {0x0b82, 0x0b82},
      {0x0bc0, 0x0bc0},
      {0x0bcd, 0x0bcd},
      {0x0c00, 0x0c00},
      {0x0c04, 0x0c04},
      {0x0c3c, 0x0c3c},
      {0x0c3e, 0x0c40},
      {0x0c46, 0x0c48},
      {0x0c4a, 0x0c4d},
      {0x0c55, 0x0c56},
      {0x0c62, 0x0c63},
      {0x0c81, 0x0c81},
      {0x0cbc, 0x0cbc},
      {0x0cbf, 0x0cbf},
      {0x0cc6, 0x0cc6},
      {0x0ccc, 0x0ccd},
      {0x0ce2, 0x0ce3},
      {0x0d00, 0x0d01},
      {0x0d3b, 0x0d3c},
      {0x0d41, 0x0d44},
      {0x0d4d, 0x0d4d},
      {0x0d62, 0x0d63},
      {0x0d81, 0x0d81},
      {0x0dca, 0x0dca},
      {0x0dd2, 0x0dd4},
      {0x0dd6, 0x0dd6},
      {0x0e31, 0x0e31},
      {0x0e34, 0x0e3a},
      {0x0e47, 0x0e4e},
      {0x0eb1, 0x0eb1},
      {0x0eb4, 0x0ebc},
      {0x0ec8, 0x0ece},
      {0x0f18, 0x0f19},
      {0x0f35, 0x0f35},
      {0x0f37, 0x0f37},
      {0x0f39, 0x0f39},
      {0x0f71, 0x0f7e},
      {0x0f80, 0x0f84},
      {0x0f86, 0x0f87},
      {0x0f8d, 0x0f97},
      {0x0f99, 0x0fbc},
      {0x0fc6, 0x0fc6},
      {0x102d, 0x1030},
      {0x1032, 0x1037},
      {0x1039, 0x103a},
      {0x103d, 0x103e},
      {0x1058, 0x1059},
      {0x105e, 0x1060},
      {0x1071, 0x1074},
      {0x1082, 0x1082},
      {0x1085, 0x1086},
      {0x108d, 0x108d},
      {0x109d, 0x109d},
      {0x135d, 0x135f},
      {0x1712, 0x1714},
      {0x1732, 0x1733},
      {0x1752, 0x1753},
      {0x1772, 0x1773},
      {0x17b4, 0x17b5},
      {0x17b7, 0x17bd},
      {0x17c6, 0x17c6},
      {0x17c9, 0x17d3},
      {0x17dd, 0x17dd},
      {0x180b, 0x180d},
      {0x180f, 0x180f},
      {0x1885, 0x1886},
      {0x18a9, 0x18a9},
      {0x1920, 0x1922},
      {0x1927, 0x1928},
      {0x1932, 0x1932},
      {0x1939, 0x193b},
      {0x1a17, 0x1a18},
      {0x1a1b, 0x1a1b},
      {0x1a56, 0x1a56},
      {0x1a58, 0x1a5e},
      {0x1a60, 0x1a60},
      {0x1a62, 0x1a62},
      {0x1a65, 0x1a6c},
      {0x1a73, 0x1a7c},
      {0x1a7f, 0x1a7f},
      {0x1ab0, 0x1ace},
      {0x1b00, 0x1b03},
      {0x1b34, 0x1b34},
      {0x1b36, 0x1b3a},
      {0x1b3c, 0x1b3c},
      {0x1b42, 0x1b42},
      {0x1b6b, 0x1b73},
      {0x1b80, 0x1b81},
      {0x1ba2, 0x1ba5},
      {0x1ba8, 0x1ba9},
      {0x1bab, 0x1bad},
      {0x1be6, 0x1be6},
      {0x1be8, 0x1be9},
      {0x1bed, 0x1bed},
      {0x1bef, 0x1bf1},
      {0x1c2c, 0x1c33},
      {0x1c36, 0x1c37},
      {0x1cd0, 0x1cd2},
      {0x1cd4, 0x1ce0},
      {0x1ce2, 0x1ce8},
      {0x1ced, 0x1ced},
      {0x1cf4, 0x1cf4},
      {0x1cf8, 0x1cf9},
      {0x1dc0, 0x1dff},
      {0x20d0, 0x20f0},
      {0x2cef, 0x2cf1},
      {0x2d7f, 0x2d7f},
      {0x2de0, 0x2dff},
      {0x302a, 0x302d},
      {0x3099, 0x309a},
      {0xa66f, 0xa672},
      {0xa674, 0xa67d},
      {0xa69e, 0xa69f},
      {0xa6f0, 0xa6f1},
      {0xa802, 0xa802},
      {0xa806, 0xa806},
      {0xa80b, 0xa80b},
      {0xa825, 0xa826},
      {0xa82c, 0xa82c},
      {0xa8c4, 0xa8c5},
      {0xa8e0, 0xa8f1},
      {0xa8ff, 0xa8ff},
      {0xa926, 0xa92d},
      {0xa947, 0xa951},
      {0xa980, 0xa982},
      {0xa9b3, 0xa9b3},
      {0xa9b6, 0xa9b9},
      {0xa9bc, 0xa9bd},
      {0xa9e5, 0xa9e5},
      {0xaa29, 0xaa2e},
      {0xaa31, 0xaa32},
      {0xaa35, 0xaa36},
      {0xaa43, 0xaa43},
      {0xaa4c, 0xaa4c},
      {0xaa7c, 0xaa7c},
      {0xaab0, 0xaab0},
      {0xaab2, 0xaab4},
      {0xaab7, 0xaab8},
      {0xaabe, 0xaabf},
      {0xaac1, 0xaac1},
      {0xaaec, 0xaaed},
      {0xaaf6, 0xaaf6},
      {0xabe5, 0xabe5},
      {0xabe8, 0xabe8},
      {0xabed, 0xabed},
      {0xfb1e, 0xfb1e},
      {0xfe00, 0xfe0f},
      {0xfe20, 0xfe2f},
      {0x101fd, 0x101fd},
      {0x102e0, 0x102e0},
      {0x10376, 0x1037a},
      {0x10a01, 0x10a03},
      {0x10a05, 0x10a06},
      {0x10a0c, 0x10a0f},
      {0x10a38, 0x10a3a},
      {0x10a3f, 0x10a3f},
      {0x10ae5, 0x10ae6},
      {0x10d24, 0x10d27},
      {0x10d69, 0x10d6d},
      {0x10eab, 0x10eac},
      {0x10efc, 0x10eff},
      {0x10f46, 0x10f50},
      {0x10f82, 0x10f85},
      {0x11001, 0x11001},
      {0x11038, 0x11046},
      {0x11070, 0x11070},
      {0x11073, 0x11074},
      {0x1107f, 0x11081},
      {0x110b3, 0x110b6},
      {0x110b9, 0x110ba},
      {0x110c2, 0x110c2},
      {0x11100, 0x11102},
      {0x11127, 0x1112b},
      {0x1112d, 0x11134},
      {0x11173, 0x11173},
      {0x11180, 0x11181},
      {0x111b6, 0x111be},
      {0x111c9, 0x111cc},
      {0x111cf, 0x111cf},
      {0x1122f, 0x11231},
      {0x11234, 0x11234},
      {0x11236, 0x11237},
      {0x1123e, 0x1123e},
      {0x11241, 0x11241},
      {0x112df, 0x112df},
      {0x112e3, 0x112ea},
      {0x11300, 0x11301},
      {0x1133b, 0x1133c},
      {0x11340, 0x11340},
      {0x11366, 0x1136c},
      {0x11370, 0x11374},
      {0x113bb, 0x113c0},
      {0x113ce, 0x113ce},
      {0x113d0, 0x113d0},
      {0x113d2, 0x113d2},
      {0x113e1, 0x113e2},
      {0x11438, 0x1143f},
      {0x11442, 0x11444},
      {0x11446, 0x11446},
      {0x1145e, 0x1145e},
      {0x114b3, 0x114b8},
      {0x114ba, 0x114ba},
      {0x114bf, 0x114c0},
      {0x114c2, 0x114c3},
      {0x115b2, 0x115b5},
      {0x115bc, 0x115bd},
      {0x115bf, 0x115c0},
      {0x115dc, 0x115dd},
      {0x11633, 0x1163a},
      {0x1163d, 0x1163d},
      {0x1163f, 0x11640},
      {0x116ab, 0x116ab},
      {0x116ad, 0x116ad},
      {0x116b0, 0x116b5},
      {0x116b7, 0x116b7},
      {0x1171d, 0x1171d},
      {0x1171f, 0x1171f},
      {0x11722, 0x11725},
      {0x11727, 0x1172b},
      {0x1182f, 0x11837},
      {0x11839, 0x1183a},
      {0x1193b, 0x1193c},
      {0x1193e, 0x1193e},
      {0x11943, 0x11943},
      {0x119d4, 0x119d7},
      {0x119da, 0x119db},
      {0x119e0, 0x119e0},
      {0x11a01, 0x11a0a},
      {0x11a33, 0x11a38},
      {0x11a3b, 0x11a3e},
      {0x11a47, 0x11a47},
      {0x11a51, 0x11a56},
      {0x11a59, 0x11a5b},
      {0x11a8a, 0x11a96},
      {0x11a98, 0x11a99},
      {0x11c30, 0x11c36},
      {0x11c38, 0x11c3d},
      {0x11c3f, 0x11c3f},
      {0x11c92, 0x11ca7},
      {0x11caa, 0x11cb0},
      {0x11cb2, 0x11cb3},
      {0x11cb5, 0x11cb6},
      {0x11d31, 0x11d36},
      {0x11d3a, 0x11d3a},
      {0x11d3c, 0x11d3d},
      {0x11d3f, 0x11d45},
      {0x11d47, 0x11d47},
      {0x11d90, 0x11d91},
      {0x11d95, 0x11d95},
      {0x11d97, 0x11d97},
      {0x11ef3, 0x11ef4},
      {0x11f00, 0x11f01},
      {0x11f36, 0x11f3a},
      {0x11f40, 0x11f40},
      {0x11f42, 0x11f42},
      {0x11f5a, 0x11f5a},
      {0x13440, 0x13440},
      {0x13447, 0x13455},
      {0x1611e, 0x16129},
      {0x1612d, 0x1612f},
      {0x16af0, 0x16af4},
      {0x16b30, 0x16b36},
      {0x16f4f, 0x16f4f},
      {0x16f8f, 0x16f92},
      {0x16fe4, 0x16fe4},
      {0x1bc9d, 0x1bc9e},
      {0x1cf00, 0x1cf2d},
      {0x1cf30, 0x1cf46},
      {0x1d167, 0x1d169},
      {0x1d17b, 0x1d182},
      {0x1d185, 0x1d18b},
      {0x1d1aa, 0x1d1ad},
      {0x1d242, 0x1d244},
      {0x1da00, 0x1da36},
      {0x1da3b, 0x1da6c},
      {0x1da75, 0x1da75},
      {0x1da84, 0x1da84},
      {0x1da9b, 0x1da9f},
      {0x1daa1, 0x1daaf},
      {0x1e000, 0x1e006},
      {0x1e008, 0x1e018},
      {0x1e01b, 0x1e021},
      {0x1e023, 0x1e024},
      {0x1e026, 0x1e02a},
      {0x1e08f, 0x1e08f},
      {0x1e130, 0x1e136},
      {0x1e2ae, 0x1e2ae},
      {0x1e2ec, 0x1e2ef},
      {0x1e4ec, 0x1e4ef},
      {0x1e5ee, 0x1e5ef},
      {0x1e8d0, 0x1e8d6},
      {0x1e944, 0x1e94a},
      {0xe0100, 0xe01ef}
   };

   return intable(combining, sizeof(combining), c);
}

//Return true for characters that can be displayed in a normal way.
//Only for characters of 0x100 and above!
Boole
utf_printable(Unt c) {
   // Sorted list of non-overlapping intervals.
   // 0xd800-0xdfff is reserved for UTF-16, actually illegal.
   static Interval nonprint[] = {
      {0x070f, 0x070f}, {0x180b, 0x180e}, {0x200b, 0x200f}, {0x202a, 0x202e},
      {0x2060, 0x206f}, {0xd800, 0xdfff}, {0xfeff, 0xfeff}, {0xfff9, 0xfffb},
      {0xfffe, 0xffff}
   };

   return !intable(nonprint, sizeof(nonprint), c);
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
   int b0 = *p;
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

CS
skipLine(CS s) {
   CS p;
   for (p = s; *p != ZERO && *p != '\n'; p++)
      {}
   if (*p == '\n')
      p++;
   return p;
}

#define USING_FLOAT_STUFF

// Copy "string" into newly allocated memory.
CS
copyStr(CS string) {
   Unt len = STRLEN(string) + 1;
   CS p = alloc(len);
   MEMMOVE(p, string, len);
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
   MEMMOVE(p, string, len);
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
         MEMMOVE(p2, p, (Unt)l);
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
CS
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
         MEMMOVE(s, res, p - res);
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
         MEMMOVE(s, res, p - res);
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
      MEMMOVE(to + tolen, from, tosize - tolen - 1);
      to[tosize - 1] = ZERO;
   } else
      MEMMOVE(to + tolen, from, fromlen + 1);
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
      MEMMOVE(rev + rev_i, s + s_i, mb_len);
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
         if (caseInsensitiveCompareNChars(p, needle, len) == 0) {
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

// Like vsnprintf() but append to the string.
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
   str_l = VSNPRINTF(str + len, space, fmt, ap);
   va_end(ap);
   return str_l;
}

int
eeSnprintf0(CS str, Unt str_m, const char *fmt, ...) {
   va_list   ap;
   va_start(ap, fmt);
   int str_l = VSNPRINTF(str, str_m, fmt, ap);
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
   int str_l = VSNPRINTF(str, str_m, fmt, ap);
   va_end(ap);

   if (str_l < 0) {
      *str = ZERO;
      return 0;
   }
   return ((Unt)str_l >= str_m) ? str_m - 1 : (Unt)str_l;
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
//{{{base64

//Base64 character set
private const Byte base64Table[] = 
   "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

//Base64 decoding table (initialized in initBase64Table() below)
private Byte base64DecodingTableG[256];

//Initialize the base64 decoding table
private void
initBase64Table(void) {
   static Boole wasInitialized = false;

   if (wasInitialized)
      return;

   // Unsupported characters are set to 0xFF
   memset(base64DecodingTableG, 0xFF, sizeof(base64DecodingTableG));

   // Initialize the index for the base64 alphabets
   for (Unt i = 0; i < STRLEN_LITERAL(base64Table); i++)
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

//Return false if input argument is malformed
Boole
decodeBase64ToArrayList(OUT ArrayList* ret, Text base64) {
   ArrayList decoded = {.len = 0, .cap = 0};

   if (base64.len == 0 || (base64.len % 4 != 0)) {
      *ret = decoded;
      return false;
   } 

   initBase64Table();

   Unt decodedLen = (base64.len / 4) * 3; // decodedLen >= 3
   if (base64.c[base64.len - 1] == '=')
      decodedLen--;
   if (base64.c[base64.len - 2] == '=')
      decodedLen--;
   if (decodedLen == 0) {
      *ret = decoded;
      return false;
   }
      
   decoded.c = alloc(decodedLen);   
   decoded.cap = decodedLen;

   Unt i, j;
   for (i = 0, j = 0; i < base64.len;) {
      Unt sextetA = base64DecodingTableG[base64.c[i++]];
      Unt sextetB = base64DecodingTableG[base64.c[i++]];
      Unt sextetC = base64DecodingTableG[base64.c[i++]];
      Unt sextetD = base64DecodingTableG[base64.c[i++]];

      if (sextetA == 0xFF || sextetB == 0xFF || sextetC == 0xFF || sextetD == 0xFF) {
         // Invalid character
         *ret = decoded;
         return false;
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
         if ((base64.c[base64.len - 2] == '=' && ((sextetB & 0xF) != 0))
            || ((base64.c[base64.len - 1] == '=') && ((sextetC & 0x3) != 0))
         ) {
            ga_clear(&decoded);
            *ret = decoded;
            return false;
         }
      }
   }
   decoded.len = decodedLen;
   *ret = decoded;
   return true;
}

//Return false if input argument is malformed
Boole
decodeBase64(OUT CS* ret, Text base64) {
   if (base64.len == 0 || (base64.len % 4 != 0)) {
      goto malformedInput;
   } 
   Unt unpaddedLen = base64.len;
   Unt decodedFromUnpaddedLen = base64.len / 4 * 3; // decodedLen >= 3
   Unt decodedFromPaddedLen = 0;
   if (base64.c[base64.len - 1] == '=') {
      decodedFromPaddedLen++;
      decodedFromUnpaddedLen -= 3;
      unpaddedLen -= 4;
      if (base64.c[base64.len - 2] == '=') {
         decodedFromPaddedLen++;
      } 
   } 
   
   initBase64Table();
   
   *ret = alloc(decodedFromPaddedLen + decodedFromUnpaddedLen + 1);
   ret[decodedFromPaddedLen + decodedFromUnpaddedLen] = ZERO;

   Unt i, j;
   for (i = 0, j = 0; i < unpaddedLen;) {
   //TODO SIMD
      Unt sextetA = base64DecodingTableG[base64.c[i++]];
      Unt sextetB = base64DecodingTableG[base64.c[i++]];
      Unt sextetC = base64DecodingTableG[base64.c[i++]];
      Unt sextetD = base64DecodingTableG[base64.c[i++]];

      if (sextetA == 0xFF || sextetB == 0xFF || sextetC == 0xFF || sextetD == 0xFF) {
         // Invalid character
         goto malformedInput;
      }

      Unt triple = (sextetA << 18) | (sextetB << 12) | (sextetC << 6) | sextetD;

      (*ret)[j++] = (triple >> 16) & 0xFF;
      (*ret)[j++] = (triple >> 8) & 0xFF;
      (*ret)[j++] = triple  & 0xFF;
   }
   if (decodedFromPaddedLen == 1) {
      (*ret)[j] = base64DecodingTableG[base64.c[i++]] << 2;
      (*ret)[j] += base64DecodingTableG[base64.c[i]] >> 6;
   } ei (decodedFromPaddedLen == 2) {
      (*ret)[j] = base64DecodingTableG[base64.c[i++]] << 2;
      (*ret)[j++] += base64DecodingTableG[base64.c[i++]] >> 4; //6 bits from first byte & 2 from snd
      (*ret)[j] = ((Byte)(base64DecodingTableG[base64.c[i++]] & 16)) << 4; //4 bits from snd and from third
      (*ret)[j] += base64DecodingTableG[base64.c[i++]] << 2;
   }
   return true;
   
malformedInput:
   *ret = null;
   return false;
}

//}}}
//{{{file path names


//If the string between "p" and "pend" ends in "name/", return "pend" minus
//the length of "name/".  Otherwise return "pend".
CS
remove_tail(CS p, CS pend, CS name) {
   int      len = (int)STRLEN(name) + 1;
   CS newend = pend - len;

   if (newend >= p
          && STRNCMP(newend, name, len - 1) == 0
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
strStartsWithUrl(CS fname) {
   // We accept alphabetic characters and a dash in scheme part.
   // RFC 3986 allows for more, but it increases the risk of matching non-URL text.

   // first character must be alpha
   if (!ASCII_ISALPHA(*fname))
      return false;

   // check body: alpha or dash
   CS p;
   for (p = fname + 1; (ASCII_ISALPHA(*p) || (*p == '-')); ++p)
      {}

   // check last char is not a dash
   if (p[-1] == '-')
      return false;

   // "://" must follow
   return path_is_url(p);
}

//Shorten the path of a file from "~/foo/../.bar/fname" to "~/f/../.b/fname"
//"trim_len" specifies how many characters to keep for each directory.
//Must be 1 or more. It's done in-place.
void
shorten_dir_len(CS str, int trim_len) {
   int skip = false;
   int dirchunk_len = 0;

   CS tail = fiGetShortFiName(str);
   CS d = str;
   for (CS s = str; ; ++s) {
      if (s >= tail) {        // copy the whole tail
         *d++ = *s;
         if (*s == ZERO)
            break;
      } ei (*s == '/') {     // copy '/' and next char
         *d++ = *s;
         skip = false;
         dirchunk_len = 0;
      } ei (!skip) {
         *d++ = *s;         // copy next char
         if (*s != '~' && *s != '.') { // and leading "~" and "."
            ++dirchunk_len; // only count word chars for the size

            // keep copying chars until we have our preferred length (or
            // until the above if/else branches move us along)
            if (dirchunk_len >= trim_len)
                skip = true;
         }
         int l = utfCharLen(s);

         while (--l > 0)
            *d++ = *++s;
      }
   }
}

//Shorten the path of a file from "~/foo/../.bar/fname" to "~/f/../.b/fname" It's done in-place.
void
shorten_dir(CS str){
   shorten_dir_len(str, 1);
}

//Remove the path from a filename completely.
void
strPrintShortName(CS src, CS dst, int dstlen) {
   if (!src) {
      *dst = ZERO;
      return;
   }
   SNPRINTF(dst, dstlen, "%s", fiGetShortFiName(src));
}
//Get the tail of a path: the file name. When the path ends in a path separator, the tail is the 
//ZERO after it. Fail safe: never return NULL.
CS
fiGetShortFiName(CS fname){
   if (!fname)
      return S"";
      
   CS afterSlash;
   CS p;
   for (afterSlash = skipInitialSlashes(fname), p = afterSlash; *p != ZERO; MB_PTR_ADV(p)) {
      if (*p == '/')
         afterSlash = p + 1;
   }
   return afterSlash;
}

//Get pointer to tail of "fname", including path separators.
//Take care of "//". Always return a valid pointer.
// "/etc/a" -> "/a", "/etc" -> "/etc"
CS
gettail_sep(CS fname){
   CS p = skipInitialSlashes(fname);   // don't remove the '/' from "c:/file"
   CS t = fiGetShortFiName(fname);
   while (t > p && after_pathsep(fname, t))
      --t;
   return t;
}

//get the next path component (just after the next path separator).
CS
getnextcomp(CS fname){
   while (*fname && *fname != '/')
      MB_PTR_ADV(fname);
   if (*fname)
      ++fname;
   return fname;
}


//Get a pointer to one character past the initial slashes of a path name
CS
skipInitialSlashes(CS path){
   CS retval = path;
   for (; *retval == '/'; ++retval)
      ++retval;

   return retval;
}


Boole
strIsRelative(CS fname) {
   return (*fname != '/' && *fname != '~');
}


//true if "name" is a full (absolute) path name or URL.
int
eeIsAbsName(CS name){
   return (strStartsWithUrl(name) != 0 || !strIsRelative(name));
}

//Compare path "p[]" to "q[]". If "maxlen" >= 0 compare "p[maxlen]" to "q[maxlen]"
//Return value like strcmp(p, q), but consider path separators.
int
pathcmp(CS p, CS q, int maxlen) {
   int i, j;
   Unt c1, c2;
   CS s = NULL;

   for (i = 0, j = 0; maxlen < 0 || (i < maxlen && j < maxlen);) {
      c1 = mb_ptr2char((CS)p + i);
      c2 = mb_ptr2char((CS)q + j);

      // End of "p": check if "q" also ends or just has a slash.
      if (c1 == ZERO) {
         if (c2 == ZERO)  // full match
            return 0;
         s = q;
         i = j;
         break;
      }

      // End of "q": check if "p" just has a slash.
      if (c2 == ZERO) {
         s = p;
         break;
      }

      if ( c1 != c2) {
         if (c1 == '/')
            return -1;
         if (c2 == '/')
            return 1;
         return c1 - c2;  // no match
      }

      i += utfCharLen((CS)p + i);
      j += utfCharLen((CS)q + j);
   }
   if (s == NULL) //"i" or "j" ran into "maxlen"
      return 0;

   c1 = mb_ptr2char((CS)s + i);
   c2 = mb_ptr2char((CS)s + i + utfCharLen((CS)s + i));
   //ignore a trailing slash, but not "//" or ":/"
   if (c2 == ZERO
       && i > 0
       && !after_pathsep((CS)s, (CS)s + i)
       && c1 == '/'
   )
      return 0;   //match with trailing slash
   if (s == q)
      return -1;  //no match
   return 1;
}

// Return true if "p" contains what looks like an environment variable. Allowing for escaping.
Boole
hasEnvVar(CS p) {
   for ( ; *p; MB_PTR_ADV(p)) {
      if (*p == '\\' && p[1] != ZERO)
         ++p;
      ei (firstOccurrence(S"$",  *p) != NULL)
         return true;
   }
   return false;
}

//Isolate one part of a string option where parts are separated with "sep_chars".
//The part is copied into "buf[maxlen]". "*option" is advanced to the next part.
//The length is returned.
int
strCutPathFromListOfPaths(OUT CS* option, OUT CS buf, int maxlen, CS sep_chars){
   int len = 0;
   CS p = *option;

   // skip '.' at start of option part, for 'suffixes'
   if (*p == '.') {
      buf[len] = *p;
      len++;
      p++;
   } 
   while (*p != ZERO && firstOccurrence((CS)sep_chars, *p) == NULL) {
      //Skip backslash before a separator character and space.
      if (p[0] == '\\' && firstOccurrence((CS)sep_chars, p[1]) != NULL)
         ++p;
      if (len < maxlen - 1) {
         buf[len] = *p;
         len++;
      } 
      ++p;
   }
   buf[len] = ZERO;

   if (*p != ZERO && *p != ',')   // skip non-standard separator
      ++p;
   p = skip_to_option_part(p);   // p points to next file name

   *option = p;
   return len;
}

//Return true if "fname" matches with an entry in "suffixes".
Boole
strMatchLowPrioSuffix(CS fname, CS suffixes){
   if (!suffixes)
      return false;
      
#define MAXSUFLEN 30       // maximum length of a file suffix
   Byte suf_buf[MAXSUFLEN];

   int fnamelen = (int)STRLEN(fname);
   int setsuflen = 0;
   for (CS setsuf = suffixes; *setsuf != ZERO; ) {
      setsuflen = strCutPathFromListOfPaths(OUT &setsuf, OUT suf_buf, MAXSUFLEN, S".,");
      if (setsuflen == 0) {
         CS tail = fiGetShortFiName(fname);

         // empty entry: match name without a '.'
         if (firstOccurrence(tail, '.') == NULL) {
            setsuflen = 1;
            break;
         }
      } else {
         if (fnamelen >= setsuflen 
               && STRNCMP(suf_buf, fname + fnamelen - setsuflen, (Unt)setsuflen) == 0)
            break;
         setsuflen = 0;
      }
   }
   return (setsuflen != 0);
}

//Add a path separator to a file name, unless it already ends in a path separator.
void
add_pathsep(CS p){
   if (*p != ZERO && !after_pathsep(p, p + STRLEN(p)))
      STRCAT(p, "/");
}


//Concatenate file names fname1 and fname2 into allocated memory.
//Only add a '/' or '\\' when 'sep' is true and it is necessary.
CS
concat_fnames(CS fname1, CS fname2, Boole sep){
   CS dest = alloc(STRLEN(fname1) + STRLEN(fname2) + 2);

   STRCPY(OUT dest, fname1);
   if (sep)
      add_pathsep(dest);
   STRCAT(dest, fname2);
   return dest;
}

//}}}
//{{{simple formats

//private Short
//hexDigit(int c) {
//   if (isdigit(c))
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

//If "c" is a hex digit, return the value. Otherwise return -1.
int
parse_hex_digit(int c) {
   return (c >= '0' && c <= '9') ? c - '0'
      : (c >= 'a' && c <= 'f') ? c - 'a' + 10
      : (c >= 'A' && c <= 'F') ? c - 'A' + 10
      : -1;
}

//}}}
//{{{arrayList

// Clear an allocated growing array.
void
ga_clear(ArrayList* gap) {
    eeglFree(gap->c);
    ga_init(gap);
}

// Clear a growing array that contains a list of strings.
void
ga_clear_strings(ArrayList* gap) {
   int i;

   if (gap->c) {
      for (i = 0; i < gap->len; ++i)
         eeglFree(((Byte **)(gap->c))[i]);
   } 
   ga_clear(gap);
}

// Copy a growing array that contains a list of strings.
int
ga_copy_strings(ArrayList *from, ArrayList *to) {
   ga_init2(to, sizeof(CS), 1);
   if (ga_grow(to, from->len) == FAIL)
      return FAIL;

   for (int i = 0; i < from->len; ++i) {
      CS orig = ((Byte **)from->c)[i];
      CS copy = orig ? copyStr(orig) : null;
      ((Byte **)to->c)[i] = copy;
   }
   to->len = from->len;
   return OK;
}

// Initialize a growing array. Don't forget to set ga_itemsize and ga_growsize! Or use ga_init2()
void
ga_init(ArrayList* gap) {
   gap->c = NULL;
   gap->cap = 0;
   gap->len = 0;
}

void
ga_init2(ArrayList *gap, Unt itemsize, int growsize) {
   ga_init(gap);
   gap->ga_itemsize = (int)itemsize;
   gap->ga_growsize = growsize;
}

// Make room in growing array "gap" for at least "n" items. FAIL for failure, OK otherwise.
int
ga_grow(ArrayList *gap, int n) {
   if (gap->cap - gap->len < n)
      return ga_grow_inner(gap, n);
   return OK;
}

int
ga_grow_inner(ArrayList* gap, int n) {
   Unt old_len;
   Unt new_len;

   if (n < gap->ga_growsize)
      n = gap->ga_growsize;

   // A linear growth is very inefficient when the array grows big.  This
   // is a compromise between allocating memory that won't be used and too
   // many copy operations. A factor of 1.5 seems reasonable.
   if (n < gap->len / 2)
      n = gap->len / 2;

   new_len = (Unt)gap->ga_itemsize * (gap->len + n);
   CS pp = eeRealloc(gap->c, new_len);
   old_len = (Unt)gap->ga_itemsize * gap->cap;
   memset(pp + old_len, 0, new_len - old_len);
   gap->cap = gap->len + n;
   gap->c = pp;
   return OK;
}

//For an ArrayList that contains a list of strings: concatenate all the
//strings with a separating "sep".
//Return NULL when out of memory.
CS
ga_concat_strings(ArrayList *gap, char *sep) {
   int i;
   int len = 0;
   int sep_len = (int)STRLEN(sep);

   for (i = 0; i < gap->len; ++i)
      len += (int)STRLEN(((Byte **)(gap->c))[i]) + sep_len;

   CS s = alloc(len + 1);

   *s = ZERO;
   CS p = s;
   for (i = 0; i < gap->len; ++i) {
      if (p != s) {
          STRCPY(p, sep);
          p += sep_len;
      }
      STRCPY(p, ((Byte **)(gap->c))[i]);
      p += STRLEN(p);
   }
   return s;
}

// Make a copy of string "p" and add it to "gap". When out of memory, 
// nothing changes and FAIL is returned.
int
ga_copy_string(ArrayList *gap, CS p) {
   CS cp = copyStr(p);

   if (ga_grow(gap, 1) == FAIL) {
      eeglFree(cp);
      return FAIL;
   }
   ((Byte **)(gap->c))[gap->len] = cp;
   gap->len++;
   return OK;
}

// Add string "p" to "gap". When out of memory, FAIL is returned (caller may want to free "p").
int
ga_add_string(ArrayList *gap, CS p) {
   if (ga_grow(gap, 1) == FAIL)
      return FAIL;
   ((Byte **)(gap->c))[gap->len] = p;
   gap->len++;
   return OK;
}

// Concatenate a string to a growarray which contains bytes.
// When "s" is NULL memory allocation fails does not do anything.
// Note: Does NOT copy the ZERO at the end!
void
ga_concat(ArrayList *gap, CS s) {
   if (s == NULL || *s == ZERO)
      return;
   int len = (int)STRLEN(s);
   if (ga_grow(gap, len) == OK) {
      MEMMOVE((char *)gap->c + gap->len, s, (Unt)len);
      gap->len += len;
    }
}

// Concatenate 'len' bytes from string 's' to a growarray. When "s" is NULL do not do anything.
void
ga_concat_len(ArrayList *gap, CS s, Unt len) {
   if (s == NULL || *s == ZERO || len == 0)
      return;
   if (ga_grow(gap, (int)len) == OK) {
      MEMMOVE((char *)gap->c + gap->len, s, len);
      gap->len += (int)len;
   }
}

// Append one byte to a growarray which contains bytes.
int
ga_append(ArrayList *gap, int c) {
   if (ga_grow(gap, 1) == FAIL)
      return FAIL;
   *((char *)gap->c + gap->len) = c;
   ++gap->len;
   return OK;
}

//}}}
//{{{doubly-linked list

// Iterative merge sort for doubly linked list.
// O(NlogN) worst case, and stable.
//  - The list is divided into blocks of increasing size (1, 2, 4, 8, ...).
//  - Each pair of blocks is merged in sorted order.
//  - Merged blocks are reconnected to build the sorted list.
void *
mergesort_list(
   void *head,
   void *(*get_next)(void *),
   void (*set_next)(void *, void *),
   void *(*get_prev)(void *),
   void (*set_prev)(void *, void *),
   int (*compare)(const void *, const void *)
){
   if (!head || !get_next(head))
      return head;

   // Count length
   int       n = 0;
   void*   curr = head;
   while (curr) {
      n++;
      curr = get_next(curr);
   }

   int   size;
   for (size = 1; size < n; size *= 2) {
      void*   new_head = NULL;
      void*   tail = NULL;
      curr = head;

      while (curr) {
         // Split two runs
         void* left = curr;
         void* right = left;
         int       i;
         for (i = 0; i < size && right; ++i)
            right = get_next(right);

         void* next = right;
         for (i = 0; i < size && next; ++i)
            next = get_next(next);

         // Break links
         void* l_end = right ? get_prev(right) : NULL;
         if (l_end)
            set_next(l_end, NULL);
         if (right)
            set_prev(right, NULL);

         void* r_end = next ? get_prev(next) : NULL;
         if (r_end)
            set_next(r_end, NULL);
         if (next)
            set_prev(next, NULL);

         // Merge
         void    *merged = NULL;
         void    *merged_tail = NULL;

         while (left || right) {
            void   *chosen = NULL;
            if (!left) {
                chosen = right;
                right = get_next(right);
            } ei (!right) {
                chosen = left;
                left = get_next(left);
            } ei (compare(left, right) <= 0) {
                chosen = left;
                left = get_next(left);
            } else {
                chosen = right;
                right = get_next(right);
            }

            if (merged_tail) {
                set_next(merged_tail, chosen);
                set_prev(chosen, merged_tail);
                merged_tail = chosen;
            } else {
                merged = merged_tail = chosen;
                set_prev(chosen, NULL);
            }
         }

          // Connect to full list
         if (!new_head)
            new_head = merged;
         else {
            set_next(tail, merged);
            set_prev(merged, tail);
         }

         // Move tail to end
         while (get_next(merged_tail))
            merged_tail = get_next(merged_tail);
         tail = merged_tail;

         curr = next;
      }

      head = new_head;
   }

   return head;
}

//}}}
//{{{sha256

// FIPS-180-2 compliant SHA-256 implementation
// GPL by Christophe Devine, applies to older version.
// Modified for md5deep, in public domain.
// Modified For Vim, Mohsin Ahmed,
// (original link www.cs.albany.edu/~mosh no longer available)
// Mohsin Ahmed states this work is distributed under the VIM License or GPL,
// at your choice.
//
// Eegl specific notes:
// Functions exported by this file:
//  1. sha256_key() hashes the password to 64 bytes char string.
//  2. sha2_seed() generates a random header.
//  sha256_self_test() is implicitly called once.


#define GET_UINT32(n, b, i)          \
{                   \
    (n) = ( (Unt)(b)[(i)    ] << 24)   \
   | ( (Unt)(b)[(i) + 1] << 16)   \
   | ( (Unt)(b)[(i) + 2] <<  8)   \
   | ( (Unt)(b)[(i) + 3]   );  \
}

#define PUT_UINT32(n,b,i)        \
{                 \
    (b)[(i)    ] = (Byte)((n) >> 24);   \
    (b)[(i) + 1] = (Byte)((n) >> 16);   \
    (b)[(i) + 2] = (Byte)((n) >>  8);   \
    (b)[(i) + 3] = (Byte)((n)      );   \
}

void
sha256_start(ContextSha256* ctx) {
   ctx->total[0] = 0;
   ctx->total[1] = 0;

   ctx->state[0] = 0x6A09E667;
   ctx->state[1] = 0xBB67AE85;
   ctx->state[2] = 0x3C6EF372;
   ctx->state[3] = 0xA54FF53A;
   ctx->state[4] = 0x510E527F;
   ctx->state[5] = 0x9B05688C;
   ctx->state[6] = 0x1F83D9AB;
   ctx->state[7] = 0x5BE0CD19;
}

private void
sha256_process(ContextSha256 *ctx, Byte data[64]) {
   Unt temp1, temp2, W[64];
   Unt A, B, C, D, EE, F, G, H;

   GET_UINT32(W[0],  data,  0);
   GET_UINT32(W[1],  data,  4);
   GET_UINT32(W[2],  data,  8);
   GET_UINT32(W[3],  data, 12);
   GET_UINT32(W[4],  data, 16);
   GET_UINT32(W[5],  data, 20);
   GET_UINT32(W[6],  data, 24);
   GET_UINT32(W[7],  data, 28);
   GET_UINT32(W[8],  data, 32);
   GET_UINT32(W[9],  data, 36);
   GET_UINT32(W[10], data, 40);
   GET_UINT32(W[11], data, 44);
   GET_UINT32(W[12], data, 48);
   GET_UINT32(W[13], data, 52);
   GET_UINT32(W[14], data, 56);
   GET_UINT32(W[15], data, 60);

#define  SHR(x, n) (((x) & 0xFFFFFFFF) >> (n))
#define ROTR(x, n) (SHR(x, n) | ((x) << (32 - (n))))

#define S0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^  SHR(x, 3))
#define S1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^  SHR(x, 10))

#define S2(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define S3(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))

#define F0(x, y, z) (((x) & (y)) | ((z) & ((x) | (y))))
#define F1(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))

#define R(t)            \
(               \
    W[t] = S1(W[(t) -  2]) + W[(t) -  7] +   \
      S0(W[(t) - 15]) + W[(t) - 16]   \
)

#define P(a,b,c,d,e,f,g,h,x,K)           \
{                    \
    temp1 = (h) + S3(e) + F1(e, f, g) + (K) + (x); \
    temp2 = S2(a) + F0(a, b, c);        \
    (d) += temp1; (h) = temp1 + temp2;        \
}

   A = ctx->state[0];
   B = ctx->state[1];
   C = ctx->state[2];
   D = ctx->state[3];
   EE = ctx->state[4];
   F = ctx->state[5];
   G = ctx->state[6];
   H = ctx->state[7];

   P( A, B, C, D, EE, F, G, H, W[ 0], 0x428A2F98);
   P( H, A, B, C, D, EE, F, G, W[ 1], 0x71374491);
   P( G, H, A, B, C, D, EE, F, W[ 2], 0xB5C0FBCF);
   P( F, G, H, A, B, C, D, EE, W[ 3], 0xE9B5DBA5);
   P( EE, F, G, H, A, B, C, D, W[ 4], 0x3956C25B);
   P( D, EE, F, G, H, A, B, C, W[ 5], 0x59F111F1);
   P( C, D, EE, F, G, H, A, B, W[ 6], 0x923F82A4);
   P( B, C, D, EE, F, G, H, A, W[ 7], 0xAB1C5ED5);
   P( A, B, C, D, EE, F, G, H, W[ 8], 0xD807AA98);
   P( H, A, B, C, D, EE, F, G, W[ 9], 0x12835B01);
   P( G, H, A, B, C, D, EE, F, W[10], 0x243185BE);
   P( F, G, H, A, B, C, D, EE, W[11], 0x550C7DC3);
   P( EE, F, G, H, A, B, C, D, W[12], 0x72BE5D74);
   P( D, EE, F, G, H, A, B, C, W[13], 0x80DEB1FE);
   P( C, D, EE, F, G, H, A, B, W[14], 0x9BDC06A7);
   P( B, C, D, EE, F, G, H, A, W[15], 0xC19BF174);
   P( A, B, C, D, EE, F, G, H, R(16), 0xE49B69C1);
   P( H, A, B, C, D, EE, F, G, R(17), 0xEFBE4786);
   P( G, H, A, B, C, D, EE, F, R(18), 0x0FC19DC6);
   P( F, G, H, A, B, C, D, EE, R(19), 0x240CA1CC);
   P( EE, F, G, H, A, B, C, D, R(20), 0x2DE92C6F);
   P( D, EE, F, G, H, A, B, C, R(21), 0x4A7484AA);
   P( C, D, EE, F, G, H, A, B, R(22), 0x5CB0A9DC);
   P( B, C, D, EE, F, G, H, A, R(23), 0x76F988DA);
   P( A, B, C, D, EE, F, G, H, R(24), 0x983E5152);
   P( H, A, B, C, D, EE, F, G, R(25), 0xA831C66D);
   P( G, H, A, B, C, D, EE, F, R(26), 0xB00327C8);
   P( F, G, H, A, B, C, D, EE, R(27), 0xBF597FC7);
   P( EE, F, G, H, A, B, C, D, R(28), 0xC6E00BF3);
   P( D, EE, F, G, H, A, B, C, R(29), 0xD5A79147);
   P( C, D, EE, F, G, H, A, B, R(30), 0x06CA6351);
   P( B, C, D, EE, F, G, H, A, R(31), 0x14292967);
   P( A, B, C, D, EE, F, G, H, R(32), 0x27B70A85);
   P( H, A, B, C, D, EE, F, G, R(33), 0x2E1B2138);
   P( G, H, A, B, C, D, EE, F, R(34), 0x4D2C6DFC);
   P( F, G, H, A, B, C, D, EE, R(35), 0x53380D13);
   P( EE, F, G, H, A, B, C, D, R(36), 0x650A7354);
   P( D, EE, F, G, H, A, B, C, R(37), 0x766A0ABB);
   P( C, D, EE, F, G, H, A, B, R(38), 0x81C2C92E);
   P( B, C, D, EE, F, G, H, A, R(39), 0x92722C85);
   P( A, B, C, D, EE, F, G, H, R(40), 0xA2BFE8A1);
   P( H, A, B, C, D, EE, F, G, R(41), 0xA81A664B);
   P( G, H, A, B, C, D, EE, F, R(42), 0xC24B8B70);
   P( F, G, H, A, B, C, D, EE, R(43), 0xC76C51A3);
   P( EE, F, G, H, A, B, C, D, R(44), 0xD192E819);
   P( D, EE, F, G, H, A, B, C, R(45), 0xD6990624);
   P( C, D, EE, F, G, H, A, B, R(46), 0xF40E3585);
   P( B, C, D, EE, F, G, H, A, R(47), 0x106AA070);
   P( A, B, C, D, EE, F, G, H, R(48), 0x19A4C116);
   P( H, A, B, C, D, EE, F, G, R(49), 0x1E376C08);
   P( G, H, A, B, C, D, EE, F, R(50), 0x2748774C);
   P( F, G, H, A, B, C, D, EE, R(51), 0x34B0BCB5);
   P( EE, F, G, H, A, B, C, D, R(52), 0x391C0CB3);
   P( D, EE, F, G, H, A, B, C, R(53), 0x4ED8AA4A);
   P( C, D, EE, F, G, H, A, B, R(54), 0x5B9CCA4F);
   P( B, C, D, EE, F, G, H, A, R(55), 0x682E6FF3);
   P( A, B, C, D, EE, F, G, H, R(56), 0x748F82EE);
   P( H, A, B, C, D, EE, F, G, R(57), 0x78A5636F);
   P( G, H, A, B, C, D, EE, F, R(58), 0x84C87814);
   P( F, G, H, A, B, C, D, EE, R(59), 0x8CC70208);
   P( EE, F, G, H, A, B, C, D, R(60), 0x90BEFFFA);
   P( D, EE, F, G, H, A, B, C, R(61), 0xA4506CEB);
   P( C, D, EE, F, G, H, A, B, R(62), 0xBEF9A3F7);
   P( B, C, D, EE, F, G, H, A, R(63), 0xC67178F2);

   ctx->state[0] += A;
   ctx->state[1] += B;
   ctx->state[2] += C;
   ctx->state[3] += D;
   ctx->state[4] += EE;
   ctx->state[5] += F;
   ctx->state[6] += G;
   ctx->state[7] += H;
}

void
sha256_update(ContextSha256 *ctx, CS input, Unt length) {
   Unt left, fill;

   if (length == 0)
      return;

   left = ctx->total[0] & 0x3F;
   fill = 64 - left;

   ctx->total[0] += length;
   ctx->total[0] &= 0xFFFFFFFF;

   if (ctx->total[0] < length)
      ctx->total[1]++;

   if (left && length >= fill) {
      memcpy((void *)(ctx->buffer + left), (void *)input, fill);
      sha256_process(ctx, ctx->buffer);
      length -= fill;
      input  += fill;
      left = 0;
   }

   while (length >= 64) {
      sha256_process(ctx, input);
      length -= 64;
      input  += 64;
   }

   if (length)
      memcpy((void *)(ctx->buffer + left), (void *)input, length);
}

private Byte sha256_padding[64] = {
   0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void
sha256_finish(ContextSha256 *ctx, Byte digest[32]) {
   Unt last, padn;
   Unt high, low;
   Byte   msglen[8];

   high = (ctx->total[0] >> 29) | (ctx->total[1] <<  3);
   low  = (ctx->total[0] <<  3);

   PUT_UINT32(high, msglen, 0);
   PUT_UINT32(low,  msglen, 4);

   last = ctx->total[0] & 0x3F;
   padn = (last < 56) ? (56 - last) : (120 - last);

   sha256_update(ctx, sha256_padding, padn);
   sha256_update(ctx, msglen, 8);

   PUT_UINT32(ctx->state[0], digest,  0);
   PUT_UINT32(ctx->state[1], digest,  4);
   PUT_UINT32(ctx->state[2], digest,  8);
   PUT_UINT32(ctx->state[3], digest, 12);
   PUT_UINT32(ctx->state[4], digest, 16);
   PUT_UINT32(ctx->state[5], digest, 20);
   PUT_UINT32(ctx->state[6], digest, 24);
   PUT_UINT32(ctx->state[7], digest, 28);
}

// Return hex digest of "buf[buf_len]" in a static array.
// if "salt" is not NULL also do "salt[salt_len]".
CS
sha256_bytes(CS buf, int buf_len, CS salt, int salt_len) {
   Byte  sha256sum[32];
   static Byte    hexit[65];
   int j;
   ContextSha256 ctx;

   sha256_self_test();

   sha256_start(&ctx);
   sha256_update(&ctx, buf, buf_len);
   if (salt != NULL)
      sha256_update(&ctx, salt, salt_len);
   sha256_finish(&ctx, sha256sum);
   for (j = 0; j < 32; j++)
      sprintf((char *)hexit + j * 2, "%02x", sha256sum[j]);
   hexit[sizeof(hexit) - 1] = '\0';
   return hexit;
}

// Return sha256(buf) as 64 hex chars in static array.
CS
sha256_key(CS buf, CS salt, int salt_len){
   // No passwd means don't encrypt
   if (!buf || *buf == ZERO)
      return S"";

   return sha256_bytes(buf, (int)STRLEN(buf), salt, salt_len);
}

// These are the standard FIPS-180-2 test vectors

private char* sha_self_test_msg[] = {
    "abc",
    "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
    NULL
};

private char *sha_self_test_vector[] = {
   "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
   "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
   "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"
};

// Perform a test on the SHA256 algorithm. Return FAIL or OK.
int
sha256_self_test(void) {
   int i, j;
   char output[65];
   ContextSha256 ctx;
   Byte buf[1000];
   Byte sha256sum[32];
   static int failures = 0;
   Byte* hexit;
   static int sha256_self_tested = 0;

   if (sha256_self_tested > 0)
      return failures > 0 ? FAIL : OK;
   sha256_self_tested = 1;

   for (i = 0; i < 3; i++) {
      if (i < 2) {
         hexit = sha256_bytes((CS)sha_self_test_msg[i],
            (int)STRLEN(sha_self_test_msg[i]),
            NULL, 0
         );
         STRCPY(output, hexit);
      } else {
          sha256_start(&ctx);
          memset(buf, 'a', 1000);
          for (j = 0; j < 1000; j++)
         sha256_update(&ctx, (CS)buf, 1000);
          sha256_finish(&ctx, sha256sum);
          for (j = 0; j < 32; j++)
         sprintf(output + j * 2, "%02x", sha256sum[j]);
      }
      if (memcmp(output, sha_self_test_vector[i], 64)) {
          failures++;
          output[sizeof(output) - 1] = '\0';
          // printf("sha256_self_test %d failed %s\n", i, output);
      }
    }
    return failures > 0 ? FAIL : OK;
}

private Unt
get_some_time(void) {
   return (Unt)time(NULL);
}

// Fill "header[header_len]" with random_data. Also "salt[salt_len]" when "salt" is not NULL.
void
sha2_seed(CS header, int header_len, CS salt, int salt_len) {
   static Byte random_data[1000];
   Byte sha256sum[32];
   ContextSha256 ctx;

   srand(get_some_time());

   for (int i = 0; i < (int)sizeof(random_data) - 1; i++)
      random_data[i] = (Byte)((get_some_time() ^ rand()) & 0xff);
   sha256_start(&ctx);
   sha256_update(&ctx, (CS)random_data, sizeof(random_data));
   sha256_finish(&ctx, sha256sum);

   //put first block into header.
   for (int i = 0; i < header_len; i++)
      header[i] = sha256sum[i % sizeof(sha256sum)];

   //put remaining block into salt.
   if (salt) {
      for (int i = 0; i < salt_len; i++)
         salt[i] = sha256sum[(i + header_len) % sizeof(sha256sum)];
   }
}

//}}}
//{{{searchin 'n' sortin'

// Return index of key in a sorted array, or -1 if not found.
int
binarySearch_Unt(Unt key, int start, int end, Arr(Unt) arr) {
   if (end <= start) {
      return -1;
   }
   int i = start;
   int j = end - 1;
   if (arr[start] == key) {
      return i;
   } ei (arr[j] == key) {
      return j;
   }

   while (i < j) {
      if (j - i == 1) {
         return -1;
      }
      int midInd = (i + j)/2;
      Unt mid = arr[midInd];
      if (mid > key) {
         j = midInd;
      } ei (mid < key) {
         i = midInd;
      } else {
         return midInd;
      }
   }
   return -1;
}

//}}}
//{{{key-value pair

// compare two Kv structs by case sensitive value
int
cmp_keyvalue_value(const void *a, const void *b) {
   return STRCMP(((Kv*)a)->value.c, ((Kv*)b)->value.c);
}

// compare two Kv structs by value with length
int
cmp_keyvalue_value_n(const void *a, const void *b) {
   Kv *kv1 = (Kv *)a;
   Kv *kv2 = (Kv *)b;

   return STRNCMP(kv1->value.c, kv2->value.c, MAX(kv1->value.len, kv2->value.len));
}

// compare two Kv structs by case insensitive value
int
cmp_keyvalue_value_i(const void *a, const void *b) {
    Kv *kv1 = (Kv *)a;
    Kv *kv2 = (Kv *)b;

    return caseInsensitiveCompare(kv1->value.c, kv2->value.c);
}

// compare two Kv structs by case insensitive ASCII value with value.length
int
cmp_keyvalue_value_ni(const void *a, const void *b) {
    Kv *kv1 = (Kv *)a;
    Kv *kv2 = (Kv *)b;
    return compareAscii((Byte *)kv1->value.c,
       (Byte *)kv2->value.c, MAX(kv1->value.len,
          kv2->value.len));
}

//}}}
//{{{DictStringInt

// Dictionary = a hash table that is filled once and then unchanged. As opposed to the more general
// term "hash table" which is a data structure with arbitrary usage patterns.
// This particular data structure is optimized in the following ways:
// - it relies on a byte array that lives longer than itself and contains all the keys separated by
// the zero char (e.g. "asdf\0bc jk\0" for the keys "asdf" and "bc jk")
// - it stores strings as simple integers (offsets into the said byte array)
// - its values are 4-byte ints
// - after construction its length doesn't change (no key insertions or removals), though the values
// themselves may be changed

private Unt
hashCode(Byte const* start) {
   Unt result = 5381;
   Byte const* p = start;
   for (int i = 0; p[i] != ZERO; i++) {
      result = ((result << 5) + result) + p[i]; // hash*33 + c
   }
   return result;
}

private Unt
hashOfText(Text s) {
   Unt result = 5381;
   Byte const* p = s.c;
   for (Unt i = 0; i < s.len; i++) {
      result = ((result << 5) + result) + p[i]; // hash*33 + c
   }
   return result;
}

private DictStringInt128*
initDict0(Arr(Byte const) text, Int size, Arena* a, OUT Arr(Unt)* temp) {
   DictStringInt128* dict = allocate(DictStringInt128, a);
   dict->c = allocateArray(size, DictStringIntItem, a),
   dict->hashes = allocateArray(size, Unt, a),
   dict->text = text;
   memset(dict->dict, 0, 128*4);
   dict->dict[128] = size;
   
   // Temporary array which we'll free at end of function (since it's at the very end of the arena)
   *temp = allocateOnArena(size*4, a);
   
   // calculate the hashes and keys
   Byte const* ch = text;
   for (Int i = 0; i < size; i++) {
      Unt hash = hashCode(ch);
      dict->hashes[i] = ch - text; // yep, initially the keys go into @hashes!
      (*temp)[i] = hash;
      dict->dict[hash >> 25]++;
      for (; *ch != ZERO; ch++) {
      }
      ch++;
   }
   dict->textLen = ch - text;
   
   // Bucket counts -> bucket start indices
   Unt sumBefore = dict->dict[0];
   dict->dict[0] = 0;
   for (Int i = 1; i < 128; i++) {
      Unt value = dict->dict[i];
      dict->dict[i] = sumBefore;
      sumBefore += value;
   }
   return dict;
}

private void
initDict1(Arena* a, int size, Arr(Unt) temp, OUT DictStringInt128* dict) {
   // After every key & value has been put, @dict now contains not starts of buckets but their ends.
   // So not [0 5 7 .. 100 101 size] but [5 7 ... 101 size size]
   // Shift all the elements right by 1 to restore
   for (Int i = 126; i > -1; i--) {
      dict->dict[i + 1] = dict->dict[i];
   }
   dict->dict[0] = 0;
   
   // Now store the hashes into their correct buckets
   for (Int i = 0; i < size; i++) {
      Unt hash = temp[i];
      Unt ind = dict->dict[hash >> 25];
      dict->hashes[ind] = hash; 
      dict->dict[hash >> 25]++;
   }
   
   // @dict needs the same shift again
   for (Int i = 126; i > -1; i--) {
      dict->dict[i + 1] = dict->dict[i];
   }
   dict->dict[0] = 0;
   
   arenaTryFree((void*)temp, size*4, a);
}

DictStringInt128* dictStringInt128New(Arr(Byte const) text, Arr(Int) values, Int size, Arena* a) {
   Arr(Unt) temp;
   DictStringInt128* dict = initDict0(text, size, a, OUT &temp);

   // Store the keys and values into their correct buckets
   for (Int i = 0; i < size; i++) {
      Unt bucket = temp[i] >> 25;
      Unt ind = dict->dict[bucket];
      dict->c[ind] = (DictStringIntItem){.key = dict->hashes[i], .value = values[i]};
      dict->dict[bucket]++;
   }
   
   initDict1(a, size, temp, OUT dict);
   return dict;
}

// Create a dictionary where values are just indices of names
DictStringInt128* dictStringInt128NewJustIndices(Arr(Byte const) text, Int size, Arena* a) {
   Arr(Unt) temp;
   DictStringInt128* dict = initDict0(text, size, a, OUT &temp);

   // Store the keys and values into their correct buckets
   for (Int i = 0; i < size; i++) {
      Unt bucket = temp[i] >> 25;
      Unt ind = dict->dict[bucket];
      dict->c[ind] = (DictStringIntItem){.key = dict->hashes[i], .value = i};
      dict->dict[bucket]++;
   }
   
   initDict1(a, size, temp, OUT dict);
   return dict;
}

#define dictGetterFn(ifFound, ifNotFound) \
   Unt needleHash = hashCode(needle);\
   Unt ind = needleHash >> 25;\
   Unt const end = haystack->dict[ind + 1];\
   for (Unt i = haystack->dict[ind]; i < end; i++) {\
      if (haystack->hashes[i] == needleHash \
            && STRCMP(haystack->text + haystack->c[i].key, needle) == 0\
      ) {\
         ifFound;\
      }\
   }\
   ifNotFound;
   
#define dictGetterFn_Text(ifFound, ifNotFound) \
   Unt needleHash = hashOfText(needle);\
   Unt ind = needleHash >> 25;\
   Unt const end = haystack->dict[ind + 1];\
   for (Unt i = haystack->dict[ind]; i < end; i++) {\
      if (haystack->hashes[i] == needleHash \
            && haystack->c[i].key + needle.len < haystack->textLen\
            && memcmp(haystack->text + haystack->c[i].key, needle.c, needle.len) == 0\
      ) {\
         ifFound;\
      }\
   }\
   ifNotFound;


Boole containsKey_DictStringInt128(Arr(Byte const) needle, DictStringInt128* restrict haystack) {
   dictGetterFn(return true, return false);
}

Int get_DictStringInt128(Arr(Byte const) needle, DictStringInt128* restrict haystack) {
   dictGetterFn(return haystack->c[i].value, return -1); // TODO throw exception
}

Int get_Text_DictStringInt128(Text needle, DictStringInt128* restrict haystack) {
   dictGetterFn_Text(return haystack->c[i].value, return -1); // TODO throw exception
}

Int getOrDefault_DictStringInt128(
   Arr(Byte const) needle, Int defaultValue, DictStringInt128* restrict haystack
) {
   dictGetterFn(return haystack->c[i].value, return defaultValue);
}

Int getOrDefault_Text_DictStringInt128(
   Text needle, Int defaultValue, DictStringInt128* restrict haystack
) {
   Unt needleHash = hashOfText(needle);
   Unt ind = needleHash >> 25;
   Unt const end = haystack->dict[ind + 1];
   for (Unt i = haystack->dict[ind]; i < end; i++) {
      if (haystack->hashes[i] == needleHash 
            && haystack->c[i].key + needle.len < haystack->textLen
            && memcmp(haystack->text + haystack->c[i].key, needle.c, needle.len) == 0
      ) {
         return haystack->c[i].value;
      }
   }
   return defaultValue;
}

Int getKv_Text_DictStringInt128(
   OUT Unt* key, Text needle, DictStringInt128* restrict haystack
) {
   Unt needleHash = hashOfText(needle);
   Unt ind = needleHash >> 25;
   Unt const end = haystack->dict[ind + 1];
   for (Unt i = haystack->dict[ind]; i < end; i++) {
      if (haystack->hashes[i] == needleHash 
            && haystack->c[i].key + needle.len < haystack->textLen
            && memcmp(haystack->text + haystack->c[i].key, needle.c, needle.len) == 0
      ) {
         *key = haystack->c[i].key;
         return haystack->c[i].value;
      }
   }
   return 0;
}

#undef dictGetterFn

//}}}
