
typedef unsigned char* CS; // ZERO-terminated byte arrays only
#define S (unsigned char*)

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h> //va_list etc

#define private static
#define OUT
#define Arr(T) T*
#define ei else if
#define null 0
#define NULLABLE

typedef int32_t Int;
typedef uint32_t Unt;
typedef uint16_t Short;
typedef int64_t Long;
typedef uint64_t Ulong;
typedef unsigned char Byte;
typedef uint8_t Boole;
typedef struct timeval TimeVal;

typedef Int LineNr;    // line number type
typedef Int ColNr;      // column number type

#define LONG_MAX       0xFFFFFFFFFFFFFFFF // 2^63 - 1
#define LONG_MIN (Long)0x8000000000000000 // -2^63
#define ULONG_MAX 18446744073709551615ULL // 2^64 - 1
#define INT_MIN -2147483648
#define INT_MAX  2147483647
#define UNT 4294967295       //2**32 - 1
#define UNT_NEG 2147483648   //2**31, minimal "negative" number within the Unt type
#define SHORT 65535          //2**16 - 1

#define MAXLNUM INT_MAX       // maximum (invalid) line number

#define declStruct(T) typedef struct T T

// Strings with length
typedef struct Text {
   CS c; //non-ZERO bytes followed by a ZERO (possibly outside of slice but in 
              //allocated memory). @c is non-null if len > 0.
   Unt  len;  //length of the slice
} Text;

// String builder containing multiple ZERO-terminated strings with free space at the tail
typedef struct {
   CS c;    //ZERO-terminated bytes, perhaps with ZEROes inside of it, too.
            //If len > 0, then c is non-null and points to (cap + 1) bytes, and .c[cap] = ZERO
   Unt len; //Length of the already filled part. Includes the final ZERO!
   Unt cap; //Total capacity. Can all be filled with any bytes
} Polystring;

// Structure used for growing arrays.
// This is used to store information that only grows, is deleted all at
// once, and needs to be accessed by index.  See ga_clear() and ga_grow().
typedef struct ArrayList {
   int len;          // current number of items used
   int cap;          // maximum number of items possible
   int ga_itemsize;  // sizeof(item)
   int ga_growsize;  // number of items to grow each time
   void* c;          // pointer to the first item
} ArrayList;

typedef struct Arena Arena;

// Mutable directory name. Always ends in a slash (unless empty). Can be appended or shortened
// (corresponds to descending into a sub-directory or coming back up). see string.c
typedef struct {
   CS c;
   Int len;
   Int cap;
   Arena* a;
} DirName;

declStruct(ChunkyString); // see string.c

typedef struct {
   Unt key; // index into text
   Int value;
} DictStringIntItem;

typedef struct {
   Unt dict[129]; // Indices into @hashes and @c. 0th element is 0, last element is size 
   Arr(Unt) hashes; // len == dict[128]
   Arr(DictStringIntItem) c; // len == dict[128]
   Arr(Byte const) text; 
   Unt textLen;
} DictStringInt128;

typedef struct {
   Unt total[2];
   Unt state[8];
   Byte buffer[64];
} ContextSha256;

// return values for functions
#if !(defined(OK) && (OK == 1))
# define OK         1
#endif
#define FAIL        0
#define NOTDONE     2   // not OK or FAIL but skipped

// EE_ISWHITE() differs from isspace() because it doesn't include <CR> and <LF> and the like.
#define SPACE_OR_TAB(x)   ((x) == ' ' || (x) == '\t')
#define IS_WHITE_OR_ZERO(x)   ((x) == ' ' || (x) == '\t' || (x) == ZERO)
#define IS_WHITE_NL_OR_ZERO(x)   ((x) == ' ' || (x) == '\t' || (x) == '\n' || (x) == ZERO)


//Use our own isdigit() replacement, because isdigit() crashes for numbers below 0 and above 255
#define EE_ISDIGIT(c) ((unsigned)(c) - '0' < 10)

//{{{defines of libc functions to avoid casts between Byte* and char*

//defines to avoid typecasts from (Byte *) to (char *) and back
#define STRLEN(s)       strlen((char *)(s))
// Length of a string literal
#define STRLEN_LITERAL(s) (sizeof(s) - 1)

#define STRCPY(d, s)       strcpy((char *)(d), (char *)(s))
#define STRNCPY(d, s, n)   strncpy((char *)(d), (char *)(s), (Unt)(n))
#define STRCMP(d, s)       strcmp((char *)(d), (char *)(s))
#define STRNCMP(d, s, n)   strncmp((char *)(d), (char *)(s), (Unt)(n))
#define SPRINTF(a, fmt, ...) sprintf((char* restrict)a, (const char* restrict)fmt, ##__VA_ARGS__)
#define SNPRINTF(a, b, fmt, ...) snprintf((char* restrict)(a), b, (char*)(fmt), ##__VA_ARGS__)
#define VSNPRINTF(a, len, fmt, ...) vsnprintf((char* restrict)(a), len, (char*)(fmt), ##__VA_ARGS__)
#define caseInsensitiveCompare(d, s)       strcasecmp((char *)(d), (char *)(s))
#define STRCOLL(d, s)      strcoll((char *)(d), (char *)(s))
#define STRTOD(a, b)       strtod((char*)a, (char**) b)
#define STRTOL(a, b, c)    strtol((char*)a, (char**) b, c)
#define STRCHR(a, b)       (CS)strchr((char*)a, (char)b)
#define STRSTR(a, b)       (CS)strstr((char*)a, (char*)b)
#define STRFTIME(a, b, fmt, ...)    strftime((char*)a, b, (char*)fmt, ##__VA_ARGS__)
#define BINDTEXTDOMAIN(a, b) bindtextdomain((char*)a, (char*)b)
#define FGETS(a, b, c) (CS)fgets((char*)a, b, c)
#define FREAD(a, b, c, d) fread((char*)a, b, c, d)
#define FPUTS(a, b)    fputs((char*)a, b)
#define FOPEN(a, b)    fopen((char*)a, (char*)b)
#define ATOL(a)    atol((char*)a)
#define ATOI(a)    atoi((char*)a)
#define STAT(a, b)    stat((char*)a, b)
#define LSTAT(a, b)    lstat((char*)a, b)
#define TGETSTR(a, b)  (CS)tgetstr((char*)a, (char**)b)
#define TGOTO(a, b, c) (CS)tgoto((char*)a, b, c)

// Like strcpy() but allows overlapped source and destination.
#define STRMOVE(d, s)      memmove((char*)(d), (char*)(s), STRLEN(s) + 1)

#define STRNICMP(d, s, n)  strncasecmp((char *)(d), (char *)(s), (Unt)(n))

//We need to call mb_stricmp() because mb_stricmp() takes care of all ascii and non-ascii
//encodings, including characters with umlauts in latin1, etc., while
//STRICMP() only handles the system locale version, which often does not
//handle non-ascii properly.

#define STRCAT(d, s)       strcat((char *)(d), (char *)(s))
#define STRNCAT(d, s, n)    strncat((char *)(d), (char *)(s), (Unt)(n))

#define MEMMOVE(to, from, len) memmove((char *)(to), (char *)(from), len)

//}}}
//{{{ASCII
//Definitions of various common control characters.

#define indexInLatinAlfabet(x)   ((x) < 'a' ? (x) - 'A' : (x) - 'a')
#define ROT13(c, a)   (((((c) - (a)) + 13) % 26) + (a))

#define ZERO        0
#define BELL        7
#define BS          8
#define TAB         9
#define NL         10
#define NL_STR     S"\012"
#define FF         12
#define ENTER      13
#define ESC        27
#define ESC_STR    S"\033"
#define ESC_STR_nc "\033"
#define DEL        0x7f
#define DEL_STR    S"\177"

#define POUND      0xA3

#define charMinusCtrl(x)   (TOUPPER_ASC(x) ^ 0x40) // '?' -> DEL, '@' -> ^@, etc.
#define Meta(x)      ((x) | 0x80)

#define CTRL_F_STR   "\006"
#define CTRL_H_STR   "\010"
#define CTRL_V_STR   "\026"

#define Ctrl_AT     0   // Ctrl-@
#define Ctrl_A      1
#define Ctrl_B      2
#define Ctrl_C      3
#define Ctrl_D      4
#define Ctrl_E      5
#define Ctrl_F      6
#define Ctrl_G      7
#define Ctrl_H      8
#define Ctrl_I      9
#define Ctrl_J     10
#define Ctrl_K     11
#define Ctrl_L     12
#define Ctrl_M     13
#define Ctrl_N     14
#define Ctrl_O     15
#define Ctrl_P     16
#define Ctrl_Q     17
#define Ctrl_R     18
#define Ctrl_S     19
#define Ctrl_T     20
#define Ctrl_U     21
#define Ctrl_V     22
#define Ctrl_W     23
#define Ctrl_X     24
#define Ctrl_Y     25
#define Ctrl_Z     26 //CTRL- [ Left Square Bracket == ESC
#define Ctrl_BSL   28 //\ BackSLash
#define Ctrl_RSB   29 //] Right Square Bracket
#define Ctrl_HAT   30 //^
#define Ctrl__     31

#define asciiA 97 

#define CSI     155   //Control Sequence Introducer
#define CSI_STR "\233"
#define DCS     0x90  //Device Control String
#define OSC     0x9d  //Operating System Command
#define STERM   0x9c  //String Terminator


// toupper() and tolower() for ASCII only and ignore the current locale.
#define TOUPPER_ASC(c)   (((c) < 'a' || (c) > 'z') ? (c) : (c) - ('a' - 'A'))
#define TOLOWER_ASC(c)   (((c) < 'A' || (c) > 'Z') ? (c) : (c) + ('a' - 'A'))

//Like isalpha() but reject non-ASCII characters. Can't be used with a
//special key (negative value).
#define ASCII_ISLOWER(c) ((unsigned)(c) - 'a' < 26)
#define ASCII_ISUPPER(c) ((unsigned)(c) - 'A' < 26)
#define ASCII_ISALPHA(c) (ASCII_ISUPPER(c) || ASCII_ISLOWER(c))
#define ASCII_ISALNUM(c) (ASCII_ISALPHA(c) || EE_ISDIGIT(c))

//}}}
//{{{simple macros

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

// A constant array bundled up with its length for passing into a function
#define CONST_ARRAY_ARG(a) a, (sizeof(a) / sizeof((a)[0]))

// Length of the array.
#define ARRAY_LENGTH(a) (sizeof(a) / sizeof((a)[0]))

//}}}

// flags for strings.c:readLongNumber()
#define STR2NR_BIN  0x01
#define STR2NR_HEX  0x04
#define STR2NR_ALL (STR2NR_BIN + STR2NR_HEX)

#define STR2NR_FORCE 0x80   // only when ONE of the above is used
#define STR2NR_QUOTE 0x10   // ignore embedded single quotes

//{{{keymap

// Keycode definitions for special keys.
// Any special key code sequences are replaced by these codes.

//K_SPECIAL is the first byte of a special key code and is always followed by two bytes.
//The second byte can have any value. ASCII is used for normal termcap
//entries, 0x80 and higher for special keys, see below.
//The third byte is guaranteed to be between 0x02 and 0x7f.

#define K_SPECIAL      (0x80)

//Positive characters are "normal" characters.
//Negative characters are special key codes.  Only characters below -0x200
//are used to so that the absolute value can't be mistaken for a single-byte character.
#define IS_SPECIAL(c)      ((c) >= UNT_NEG)

//Characters 0x0100 - 0x01ff have a special meaning for abbreviations.
//Multi-byte characters also have ABBR_OFF added, thus are above 0x0200.
#define ABBR_OFF       0x100

//ZERO cannot be in the input string, therefore it is replaced by (K_SPECIAL KS_ZERO KE_FILLER)
#define KS_ZERO          255

//K_SPECIAL cannot be in the input string, therefore it is replaced by (K_SPECIAL KS_SPECIAL KE_FILLER)
#define KS_SPECIAL       254

//KS_EXTRA is used for keys that have no termcap name (K_SPECIAL KS_EXTRA KE_xxx)
#define KS_EXTRA         253

//KS_MODIFIER is used when a modifier is given for a (special) key (K_SPECIAL KS_MODIFIER bitmask)
#define KS_MODIFIER      252

// These were used for the GUI K_SPECIAL   KS_xxx   KE_FILLER
#define KS_MOUSE         251
#define KS_MENU          250
#define KS_VER_SCROLLBAR 249
#define KS_HOR_SCROLLBAR 248

//These are used for DEC mouse
#define KS_NETTERM_MOUSE 247
#define KS_DEC_MOUSE     246

//Used for tearing off a menu.
#define KS_TEAROFF       244

// Used for JSB term mouse.
#define KS_JSBTERM_MOUSE 243

// Used a termcap entry that produces a normal character.
#define KS_KEY           242

// Used for click in a tabs label
#define KS_TABLINE       240

// Used for menu in a tab line.
#define KS_TABMENU       239

// Used for the urxvt mouse.
#define KS_URXVT_MOUSE   238

// Used for the sgr mouse.
#define KS_SGR_MOUSE          237
#define KS_SGR_MOUSE_RELEASE  236

//Filler used after KS_SPECIAL and others
#define KE_FILLER      ('X')

// translation of three byte code "K_SPECIAL a b" into int "K_xxx" and back
#define TERMCAP2KEY(a, b)   (4294967295 - ((a) + ((Unt)(b) << 8)) + 1)
#define KEY2TERMCAP0(x)     ((4294967295 - (x) + 1) & 0xff)
#define KEY2TERMCAP1(x)     (((4294967295 - (x) + 1) >> 8) & 0xff)

//get second or third byte when translating special key code into three bytes
#define K_SECOND(c)   ((c) == K_SPECIAL ? KS_SPECIAL : (c) == ZERO ? KS_ZERO : KEY2TERMCAP0(c))

#define K_THIRD(c)   (((c) == K_SPECIAL || (c) == ZERO) ? KE_FILLER : KEY2TERMCAP1(c))

//get single int code from second byte after K_SPECIAL
#define TO_SPECIAL(a, b)    ((a) == KS_SPECIAL ? K_SPECIAL : (a) == KS_ZERO ? K_ZERO : TERMCAP2KEY(a, b))

//Codes for keys that do not have a termcap name.
//The numbers are fixed to make sure that recorded key sequences remain valid.
//Add new entries at the end, not halfway.
//
//K_SPECIAL KS_EXTRA KE_xxx
enum key_extra{
   KE_S_UP = 4,      // shift-up
   KE_S_DOWN = 5,      // shift-down

   KE_S_F1 = 6,      // shifted function keys
   KE_S_F2 = 7,
   KE_S_F3 = 8,
   KE_S_F4 = 9,
   KE_S_F5 = 10,
   KE_S_F6 = 11,
   KE_S_F7 = 12,
   KE_S_F8 = 13,
   KE_S_F9 = 14,
   KE_S_F10 = 15,

   KE_S_F11 = 16,
   KE_S_F12 = 17,
   KE_S_F13 = 18,
   KE_S_F14 = 19,
   KE_S_F15 = 20,
   KE_S_F16 = 21,
   KE_S_F17 = 22,
   KE_S_F18 = 23,
   KE_S_F19 = 24,
   KE_S_F20 = 25,

   KE_S_F21 = 26,
   KE_S_F22 = 27,
   KE_S_F23 = 28,
   KE_S_F24 = 29,
   KE_S_F25 = 30,
   KE_S_F26 = 31,
   KE_S_F27 = 32,
   KE_S_F28 = 33,
   KE_S_F29 = 34,
   KE_S_F30 = 35,

   KE_S_F31 = 36,
   KE_S_F32 = 37,
   KE_S_F33 = 38,
   KE_S_F34 = 39,
   KE_S_F35 = 40,
   KE_S_F36 = 41,
   KE_S_F37 = 42,

   KE_MOUSE = 43,      // mouse event start

// Symbols for pseudo keys which are translated from the real key symbols above.
   KE_LEFTMOUSE = 44,      // Left mouse button click
   KE_LEFTDRAG = 45,      // Drag with left mouse button down
   KE_LEFTRELEASE = 46,   // Left mouse button release
   KE_MIDDLEMOUSE = 47,   // Middle mouse button click
   KE_MIDDLEDRAG = 48,   // Drag with middle mouse button down
   KE_MIDDLERELEASE = 49, // Middle mouse button release
   KE_RIGHTMOUSE = 50,   // Right mouse button click
   KE_RIGHTDRAG = 51,      // Drag with right mouse button down
   KE_RIGHTRELEASE = 52,   // Right mouse button release

   KE_IGNORE = 53,     // Ignored mouse drag/release

   KE_TAB = 54,      // unshifted TAB key
   KE_S_TAB_OLD = 55,      // shifted TAB key (no longer used)

   KE_SNIFF_UNUSED = 56,   // obsolete
   KE_XF1 = 57,      // extra vt100 function keys for xterm
   KE_XF2 = 58,
   KE_XF3 = 59,
   KE_XF4 = 60,
   KE_XEND = 61,      // extra (vt100) end key for xterm
   KE_ZEND = 62,     // extra (vt100) end key for xterm
   KE_XHOME = 63,     // extra (vt100) home key for xterm
   KE_ZHOME = 64,      // extra (vt100) home key for xterm
   KE_XUP = 65,      // extra vt100 cursor keys for xterm
   KE_XDOWN = 66,
   KE_XLEFT = 67,
   KE_XRIGHT = 68,

   KE_LEFTMOUSE_NM = 69,   // non-mappable Left mouse button click
   KE_LEFTRELEASE_NM = 70,   // non-mappable left mouse button release

   KE_S_XF1 = 71,     // vt100 shifted function keys for xterm
   KE_S_XF2 = 72,
   KE_S_XF3 = 73,
   KE_S_XF4 = 74,

   // NOTE: The scroll wheel events are inverted: i.e. UP is the same as moving the actual scroll 
   // wheel down, LEFT is the same as moving the scroll wheel right.
   KE_MOUSEDOWN = 75,      // scroll wheel pseudo-button Down
   KE_MOUSEUP = 76,      // scroll wheel pseudo-button Up
   KE_MOUSELEFT = 77,      // scroll wheel pseudo-button Left
   KE_MOUSERIGHT = 78,   // scroll wheel pseudo-button Right

   KE_KINS = 79,      // keypad Insert key
   KE_KDEL = 80,      // keypad Delete key

   KE_CSI = 81,      // CSI typed directly
   KE_SNR = 82,      // <SNR>
   KE_PLUG = 83,      // <Plug>
   KE_COMMPORT = 84,      // open command-line window from Command-line Mode

   KE_C_LEFT = 85,      // control-left
   KE_C_RIGHT = 86,      // control-right
   KE_C_HOME = 87,      // control-home
   KE_C_END = 88,      // control-end

   KE_X1MOUSE = 89,      // X1/X2 mouse-buttons
   KE_X1DRAG = 90,
   KE_X1RELEASE = 91,
   KE_X2MOUSE = 92,
   KE_X2DRAG = 93,
   KE_X2RELEASE = 94,

   KE_DROP = 95,      // DnD data is available
   KE_CURSORHOLD = 96,   // CursorHold event
   KE_NOP = 97,      // doesn't do something
   KE_FOCUSGAINED = 98,   // focus gained
   KE_FOCUSLOST = 99,     // focus lost
   KE_MOUSEMOVE = 100,   // mouse moved with no button down
   KE_MOUSEMOVE_XY = 101,   // KE_MOUSEMOVE with coordinates
   KE_CANCEL = 102,      // return from vgetc()
   KE_COMMAND = 103,      // <Cmd> special key
   KE_SCRIPT_COMMAND = 104,   // <ScriptCmd> special key
   KE_S_BS = 105,      // shift + <BS>
   KE_SID = 106,      // <SID> special key, followed by {nr};
   KE_ESC = 107,      // used for K_ESC
   KE_WILD = 108      // triggers wildmode completion
};

// The three-byte codes are replaced with a negative number when using vgetc().
#define K_ZERO    TERMCAP2KEY(KS_ZERO, KE_FILLER)

#define K_ESC     TERMCAP2KEY(KS_EXTRA, KE_ESC)

#define K_UP      TERMCAP2KEY('k', 'u')
#define K_DOWN    TERMCAP2KEY('k', 'd')
#define K_LEFT    TERMCAP2KEY('k', 'l')
#define K_RIGHT   TERMCAP2KEY('k', 'r')
#define K_S_UP    TERMCAP2KEY(KS_EXTRA, KE_S_UP)
#define K_S_DOWN  TERMCAP2KEY(KS_EXTRA, KE_S_DOWN)
#define K_S_LEFT  TERMCAP2KEY('#', '4')
#define K_C_LEFT  TERMCAP2KEY(KS_EXTRA, KE_C_LEFT)
#define K_S_RIGHT TERMCAP2KEY('%', 'i')
#define K_C_RIGHT TERMCAP2KEY(KS_EXTRA, KE_C_RIGHT)

#define K_S_HOME  TERMCAP2KEY('#', '2')
#define K_C_HOME  TERMCAP2KEY(KS_EXTRA, KE_C_HOME)
#define K_S_END   TERMCAP2KEY('*', '7')
#define K_C_END   TERMCAP2KEY(KS_EXTRA, KE_C_END)

#define K_TAB     TERMCAP2KEY(KS_EXTRA, KE_TAB)
#define K_S_TAB   TERMCAP2KEY('k', 'B')
#define K_S_BS    TERMCAP2KEY(KS_EXTRA, KE_S_BS)

// extra set of function keys F1-F4, for vt100 compatible xterm
#define K_XF1     TERMCAP2KEY(KS_EXTRA, KE_XF1)
#define K_XF2     TERMCAP2KEY(KS_EXTRA, KE_XF2)
#define K_XF3     TERMCAP2KEY(KS_EXTRA, KE_XF3)
#define K_XF4     TERMCAP2KEY(KS_EXTRA, KE_XF4)

// extra set of cursor keys for vt100 compatible xterm
#define K_XUP     TERMCAP2KEY(KS_EXTRA, KE_XUP)
#define K_XDOWN   TERMCAP2KEY(KS_EXTRA, KE_XDOWN)
#define K_XLEFT   TERMCAP2KEY(KS_EXTRA, KE_XLEFT)
#define K_XRIGHT  TERMCAP2KEY(KS_EXTRA, KE_XRIGHT)

#define K_F1      TERMCAP2KEY('k', '1')   // function keys
#define K_F2      TERMCAP2KEY('k', '2')
#define K_F3      TERMCAP2KEY('k', '3')
#define K_F4      TERMCAP2KEY('k', '4')
#define K_F5      TERMCAP2KEY('k', '5')
#define K_F6      TERMCAP2KEY('k', '6')
#define K_F7      TERMCAP2KEY('k', '7')
#define K_F8      TERMCAP2KEY('k', '8')
#define K_F9      TERMCAP2KEY('k', '9')
#define K_F10      TERMCAP2KEY('k', ';')

#define K_F11      TERMCAP2KEY('F', '1')
#define K_F12      TERMCAP2KEY('F', '2')
#define K_F13      TERMCAP2KEY('F', '3')
#define K_F14      TERMCAP2KEY('F', '4')
#define K_F15      TERMCAP2KEY('F', '5')
#define K_F16      TERMCAP2KEY('F', '6')
#define K_F17      TERMCAP2KEY('F', '7')
#define K_F18      TERMCAP2KEY('F', '8')
#define K_F19      TERMCAP2KEY('F', '9')
#define K_F20      TERMCAP2KEY('F', 'A')

#define K_F21      TERMCAP2KEY('F', 'B')
#define K_F22      TERMCAP2KEY('F', 'C')
#define K_F23      TERMCAP2KEY('F', 'D')
#define K_F24      TERMCAP2KEY('F', 'E')
#define K_F25      TERMCAP2KEY('F', 'F')
#define K_F26      TERMCAP2KEY('F', 'G')
#define K_F27      TERMCAP2KEY('F', 'H')
#define K_F28      TERMCAP2KEY('F', 'I')
#define K_F29      TERMCAP2KEY('F', 'J')
#define K_F30      TERMCAP2KEY('F', 'K')

#define K_F31      TERMCAP2KEY('F', 'L')
#define K_F32      TERMCAP2KEY('F', 'M')
#define K_F33      TERMCAP2KEY('F', 'N')
#define K_F34      TERMCAP2KEY('F', 'O')
#define K_F35      TERMCAP2KEY('F', 'P')
#define K_F36      TERMCAP2KEY('F', 'Q')
#define K_F37      TERMCAP2KEY('F', 'R')

// extra set of shifted function keys F1-F4, for vt100 compatible xterm
#define K_S_XF1      TERMCAP2KEY(KS_EXTRA, KE_S_XF1)
#define K_S_XF2      TERMCAP2KEY(KS_EXTRA, KE_S_XF2)
#define K_S_XF3      TERMCAP2KEY(KS_EXTRA, KE_S_XF3)
#define K_S_XF4      TERMCAP2KEY(KS_EXTRA, KE_S_XF4)

#define K_S_F1      TERMCAP2KEY(KS_EXTRA, KE_S_F1)   // shifted func. keys
#define K_S_F2      TERMCAP2KEY(KS_EXTRA, KE_S_F2)
#define K_S_F3      TERMCAP2KEY(KS_EXTRA, KE_S_F3)
#define K_S_F4      TERMCAP2KEY(KS_EXTRA, KE_S_F4)
#define K_S_F5      TERMCAP2KEY(KS_EXTRA, KE_S_F5)
#define K_S_F6      TERMCAP2KEY(KS_EXTRA, KE_S_F6)
#define K_S_F7      TERMCAP2KEY(KS_EXTRA, KE_S_F7)
#define K_S_F8      TERMCAP2KEY(KS_EXTRA, KE_S_F8)
#define K_S_F9      TERMCAP2KEY(KS_EXTRA, KE_S_F9)
#define K_S_F10     TERMCAP2KEY(KS_EXTRA, KE_S_F10)

#define K_S_F11      TERMCAP2KEY(KS_EXTRA, KE_S_F11)
#define K_S_F12      TERMCAP2KEY(KS_EXTRA, KE_S_F12)
// K_S_F13 to K_S_F37  are currently not used

#define K_HELP      TERMCAP2KEY('%', '1')
#define K_UNDO      TERMCAP2KEY('&', '8')

#define K_BS      TERMCAP2KEY('k', 'b')

#define K_INS      TERMCAP2KEY('k', 'I')
#define K_KINS      TERMCAP2KEY(KS_EXTRA, KE_KINS)
#define K_DEL      TERMCAP2KEY('k', 'D')
#define K_KDEL      TERMCAP2KEY(KS_EXTRA, KE_KDEL)
#define K_HOME      TERMCAP2KEY('k', 'h')
#define K_KHOME      TERMCAP2KEY('K', '1')   // keypad home (upper left)
#define K_XHOME      TERMCAP2KEY(KS_EXTRA, KE_XHOME)
#define K_ZHOME      TERMCAP2KEY(KS_EXTRA, KE_ZHOME)
#define K_END      TERMCAP2KEY('@', '7')
#define K_KEND      TERMCAP2KEY('K', '4')   // keypad end (lower left)
#define K_XEND      TERMCAP2KEY(KS_EXTRA, KE_XEND)
#define K_ZEND      TERMCAP2KEY(KS_EXTRA, KE_ZEND)
#define K_PAGEUP   TERMCAP2KEY('k', 'P')
#define K_PAGEDOWN   TERMCAP2KEY('k', 'N')
#define K_KPAGEUP   TERMCAP2KEY('K', '3')   // keypad pageup (upper R.)
#define K_KPAGEDOWN   TERMCAP2KEY('K', '5')   // keypad pagedown (lower R.)

#define K_KPLUS      TERMCAP2KEY('K', '6')   // keypad plus
#define K_KMINUS   TERMCAP2KEY('K', '7')   // keypad minus
#define K_KDIVIDE   TERMCAP2KEY('K', '8')   // keypad /
#define K_KMULTIPLY   TERMCAP2KEY('K', '9')   // keypad *
#define K_KENTER   TERMCAP2KEY('K', 'A')   // keypad Enter
#define K_KPOINT   TERMCAP2KEY('K', 'B')   // keypad . or ,
#define K_PS      TERMCAP2KEY('P', 'S')   // paste start
#define K_PE      TERMCAP2KEY('P', 'E')   // paste end

#define K_K0      TERMCAP2KEY('K', 'C')   // keypad 0
#define K_K1      TERMCAP2KEY('K', 'D')   // keypad 1
#define K_K2      TERMCAP2KEY('K', 'E')   // keypad 2
#define K_K3      TERMCAP2KEY('K', 'F')   // keypad 3
#define K_K4      TERMCAP2KEY('K', 'G')   // keypad 4
#define K_K5      TERMCAP2KEY('K', 'H')   // keypad 5
#define K_K6      TERMCAP2KEY('K', 'I')   // keypad 6
#define K_K7      TERMCAP2KEY('K', 'J')   // keypad 7
#define K_K8      TERMCAP2KEY('K', 'K')   // keypad 8
#define K_K9      TERMCAP2KEY('K', 'L')   // keypad 9

#define K_MOUSE      TERMCAP2KEY(KS_MOUSE, KE_FILLER)
#define K_MENU      TERMCAP2KEY(KS_MENU, KE_FILLER)
#define K_VER_SCROLLBAR   TERMCAP2KEY(KS_VER_SCROLLBAR, KE_FILLER)
#define K_HOR_SCROLLBAR   TERMCAP2KEY(KS_HOR_SCROLLBAR, KE_FILLER)

#define K_NETTERM_MOUSE   TERMCAP2KEY(KS_NETTERM_MOUSE, KE_FILLER)
#define K_DEC_MOUSE   TERMCAP2KEY(KS_DEC_MOUSE, KE_FILLER)
#define K_JSBTERM_MOUSE   TERMCAP2KEY(KS_JSBTERM_MOUSE, KE_FILLER)
#define K_URXVT_MOUSE   TERMCAP2KEY(KS_URXVT_MOUSE, KE_FILLER)
#define K_SGR_MOUSE   TERMCAP2KEY(KS_SGR_MOUSE, KE_FILLER)
#define K_SGR_MOUSERELEASE TERMCAP2KEY(KS_SGR_MOUSE_RELEASE, KE_FILLER)

#define K_TEAROFF   TERMCAP2KEY(KS_TEAROFF, KE_FILLER)

#define K_TABLINE   TERMCAP2KEY(KS_TABLINE, KE_FILLER)
#define K_TABMENU   TERMCAP2KEY(KS_TABMENU, KE_FILLER)

//Symbols for pseudo keys which are translated from the real key symbols above.
#define K_LEFTMOUSE   TERMCAP2KEY(KS_EXTRA, KE_LEFTMOUSE)
#define K_LEFTMOUSE_NM   TERMCAP2KEY(KS_EXTRA, KE_LEFTMOUSE_NM)
#define K_LEFTDRAG   TERMCAP2KEY(KS_EXTRA, KE_LEFTDRAG)
#define K_LEFTRELEASE   TERMCAP2KEY(KS_EXTRA, KE_LEFTRELEASE)
#define K_LEFTRELEASE_NM TERMCAP2KEY(KS_EXTRA, KE_LEFTRELEASE_NM)
#define K_MOUSEMOVE   TERMCAP2KEY(KS_EXTRA, KE_MOUSEMOVE)
#define K_MIDDLEMOUSE  TERMCAP2KEY(KS_EXTRA, KE_MIDDLEMOUSE)
#define K_MIDDLEDRAG   TERMCAP2KEY(KS_EXTRA, KE_MIDDLEDRAG)
#define K_MIDDLERELEASE   TERMCAP2KEY(KS_EXTRA, KE_MIDDLERELEASE)
#define K_RIGHTMOUSE   TERMCAP2KEY(KS_EXTRA, KE_RIGHTMOUSE)
#define K_RIGHTDRAG   TERMCAP2KEY(KS_EXTRA, KE_RIGHTDRAG)
#define K_RIGHTRELEASE   TERMCAP2KEY(KS_EXTRA, KE_RIGHTRELEASE)
#define K_X1MOUSE    TERMCAP2KEY(KS_EXTRA, KE_X1MOUSE)
#define K_X1MOUSE    TERMCAP2KEY(KS_EXTRA, KE_X1MOUSE)
#define K_X1DRAG     TERMCAP2KEY(KS_EXTRA, KE_X1DRAG)
#define K_X1RELEASE  TERMCAP2KEY(KS_EXTRA, KE_X1RELEASE)
#define K_X2MOUSE    TERMCAP2KEY(KS_EXTRA, KE_X2MOUSE)
#define K_X2DRAG     TERMCAP2KEY(KS_EXTRA, KE_X2DRAG)
#define K_X2RELEASE  TERMCAP2KEY(KS_EXTRA, KE_X2RELEASE)

#define K_IGNORE   TERMCAP2KEY(KS_EXTRA, KE_IGNORE)
#define K_NOP      TERMCAP2KEY(KS_EXTRA, KE_NOP)
#define K_CANCEL   TERMCAP2KEY(KS_EXTRA, KE_CANCEL)

#define K_MOUSEDOWN   TERMCAP2KEY(KS_EXTRA, KE_MOUSEDOWN)
#define K_MOUSEUP     TERMCAP2KEY(KS_EXTRA, KE_MOUSEUP)
#define K_MOUSELEFT   TERMCAP2KEY(KS_EXTRA, KE_MOUSELEFT)
#define K_MOUSERIGHT  TERMCAP2KEY(KS_EXTRA, KE_MOUSERIGHT)

#define K_CSI        TERMCAP2KEY(KS_EXTRA, KE_CSI)
#define K_SNR        TERMCAP2KEY(KS_EXTRA, KE_SNR)
#define K_PLUG       TERMCAP2KEY(KS_EXTRA, KE_PLUG)
#define K_COMMPORT   TERMCAP2KEY(KS_EXTRA, KE_COMMPORT)

#define K_DROP        TERMCAP2KEY(KS_EXTRA, KE_DROP)
#define K_FOCUSGAINED TERMCAP2KEY(KS_EXTRA, KE_FOCUSGAINED)
#define K_FOCUSLOST   TERMCAP2KEY(KS_EXTRA, KE_FOCUSLOST)

#define K_CURSORHOLD TERMCAP2KEY(KS_EXTRA, KE_CURSORHOLD)

#define K_COMMAND   TERMCAP2KEY(KS_EXTRA, KE_COMMAND)
#define K_SCRIPT_COMMAND TERMCAP2KEY(KS_EXTRA, KE_SCRIPT_COMMAND)
#define K_SID      TERMCAP2KEY(KS_EXTRA, KE_SID)

#define K_WILD      TERMCAP2KEY(KS_EXTRA, KE_WILD)

// Bits for modifier mask. 0x01 cannot be used, because the modifier must be 0x02 or higher
#define MOD_MASK_SHIFT       0x02
#define MOD_MASK_CTRL       0x04
#define MOD_MASK_ALT       0x08   // aka META
#define MOD_MASK_META       0x10   // META when it's different from ALT
#define MOD_MASK_2CLICK       0x20   // use MOD_MASK_MULTI_CLICK
#define MOD_MASK_3CLICK       0x40   // use MOD_MASK_MULTI_CLICK
#define MOD_MASK_4CLICK       0x60   // use MOD_MASK_MULTI_CLICK

#define MOD_MASK_MULTI_CLICK   (MOD_MASK_2CLICK|MOD_MASK_3CLICK|MOD_MASK_4CLICK)

//The length of the longest special key name, including modifiers.
//Current longest is <M-C-S-T-D-A-4-ScrollWheelRight> (length includes '<' and '>').
#define MAX_KEY_NAME_LEN    32

//Maximum length of a special key event as tokens.  This includes modifiers.
//The longest event is something like <M-C-S-T-4-LeftDrag> which would be the
//following string of tokens:
//
//<K_SPECIAL> <KS_MODIFIER> bitmask <K_SPECIAL> <KS_EXTRA> <KT_LEFTDRAG>.
//
//This is a total of 6 tokens, and is currently the longest one possible.
#define MAX_KEY_CODE_LEN    6

//}}}
//{{{UTF-8

//MB_PTR_ADV(): advance a pointer to the next character, taking care of multi-byte characters.
//MB_PTR_BACK(): backup a pointer to the previous character, taking care of multi-byte characters.
//MB_COPY_CHAR(f, t): copy one char from "f" to "t" and advance the pointers.
//PTR2CHAR(): get character from pointer.
// Advance multi-byte pointer, skip over composing chars.
#define MB_PTR_ADV(p)       p += utfCharLen(p)
// Advance multi-byte pointer, do not skip over composing chars.
#define MB_CPTR_ADV(p)       p += utf_ptr2len(p)
// Backup multi-byte pointer. Only use with "p" > "s" !
#define MB_PTR_BACK(s, p)  p -= ((*mb_head_off)(s, (p) - 1) + 1)
// get length of multi-byte char, not including composing chars
#define MB_CPTR2LEN(p)       (utf_ptr2len(p))

#define UTF_COMPOSINGLIKE(p1, p2)  utf_iscomposing(mb_ptr2char(p2))

#define MB_COPY_CHAR(f, t) do { mb_copy_char(&(f), &(t)); } while (0)

//}}}
//{{{arena

Arena* createArena();
void deleteArena(Arena* ar);
void* allocateOnArena(Unt, Arena*);
#define allocate(T, a) (T*)allocateOnArena(sizeof(T), a)
#define allocateArray(cap, T, a) (T*)allocateOnArena(cap*sizeof(T), a)

//}}}

// Store a key/value (string) pair
typedef struct {
   int key;
   Text value;
} Kv;

//Max chars in a path name including ZERO, see linux/limits.h
#define MAXPATHL   4096
typedef struct stat FileStat;


// defines for evalVars()
#define VALID_PATH      1
#define VALID_HEAD      2

// Character used as separator in autoload function/variable names.
#define AUTOLOAD_CHAR '#'


