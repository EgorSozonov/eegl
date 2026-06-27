
typedef unsigned char* CS; // ZERO-terminated byte arrays only
#define S (unsigned char*)

#include <stdint.h>
#include <stdbool.h>

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

//{{{defines of libc functions to avoid casts between Byte* and char*

//defines to avoid typecasts from (Byte *) to (char *) and back
#define STRLEN(s)       strlen((char *)(s))
// Length of a string literal
#define STRLEN_LITERAL(s) (sizeof(s) - 1)

#define STRCPY(d, s)       strcpy((char *)(d), (char *)(s))
#define STRNCPY(d, s, n)   strncpy((char *)(d), (char *)(s), (Unt)(n))
#define STRCMP(d, s)       strcmp((char *)(d), (char *)(s))
#define STRNCMP(d, s, n)   strncmp((char *)(d), (char *)(s), (Unt)(n))
#define SPRINTF(a, fmt, ...) sprintf((char* restrict)a, (char*)fmt, ##__VA_ARGS__)
#define SNPRINTF(a, b, fmt, ...) snprintf((char* restrict)(a), b, (char*)(fmt), ##__VA_ARGS__)
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
#define STRMOVE(d, s)      mch_memmove((d), (s), STRLEN(s) + 1)

#define STRNICMP(d, s, n)  strncasecmp((char *)(d), (char *)(s), (Unt)(n))

//We need to call mb_stricmp() because mb_stricmp() takes care of all ascii and non-ascii
//encodings, including characters with umlauts in latin1, etc., while
//STRICMP() only handles the system locale version, which often does not
//handle non-ascii properly.

#define STRCAT(d, s)       strcat((char *)(d), (char *)(s))
#define STRNCAT(d, s, n)    strncat((char *)(d), (char *)(s), (Unt)(n))

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

//}}}

