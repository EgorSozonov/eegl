//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

#ifndef EEGL__H
#define EEGL__H

#ifdef PROTO
// cproto runs into trouble when these types are missing
typedef double _Float16;
typedef double _Float32;
typedef double _Float64;
typedef double _Float128;
typedef double _Float32x;
typedef double _Float64x;
#endif

//Full GNU mode for glibc
#define _DEFAULT_SOURCE 1
#define _GNU_SOURCE 1

#include "base.h"

// ============ the header file puzzle: order matters =========

//{{{config.h

// Defined to the size of an int
#define EE_SIZEOF_INT 4

// Defined to the size of a long
#define EE_SIZEOF_LONG 8

// Defined to the size of off_t
#define SIZEOF_OFF_T 8

// Define to a typecast for select() arguments 2, 3 and 4.
#define SELECT_TYPE_ARG234 (fd_set *)

// Define to nanoseconds field of struct stat
#define ST_MTIM_NSEC st_mtim.tv_nsec

// Define if tgetent() returns zero for an error
#define TGETENT_ZERO_ERR 0

#define HAVE_INET_NTOP 1

// Define if you have the header file:
#define HAVE_SETJMP_H 1

//}}}

// user ID of root is usually zero, but not for everybody
#define ROOT_UID 0

//How many Unicode symbols to combine max
#define MAX_COMBINED_SYMBOLS 6

#ifndef EEGLPACKAGE
# define EEGLPACKAGE   "eegl"
#endif

//{{{unix header: lots of system header files

#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <inttypes.h> // for PRIu64

//# include <sys/param.h>

// always use unlink() to remove files
#ifndef PROTO
#define eeMkdir(x, y) mkdir((char *)(x), y)
#define mch_rmdir(x) rmdir((char *)(x))
#define mch_remove(x) unlink((char *)(x))
#endif

#define SIGPROTOARG   (int)
#define SIGDEFARG(s)   (int s UNUSED)
#define SIGDUMMYARG   0

typedef void (*sighandler_T) SIGPROTOARG;

#include <dirent.h>

#include <time.h>
#include <sys/time.h>

#include <signal.h>

#if defined(DIRSIZ) && !defined(MAXNAMLEN)
# define MAXNAMLEN DIRSIZ
#endif

//Note: if MAXNAMLEN has the wrong value, you will get error messages
//for not being able to open the swap file.
#if !defined(MAXNAMLEN)
# define MAXNAMLEN 512          // for all other Unix
#endif

#define BASENAMELEN   (MAXNAMLEN - 5)

#include <pwd.h>

#ifndef PROTO

#include <sys/file.h>

#endif // PROTO

#define MAIN_HELPFILE "/usr/share/doc/eegl/help.txt"

#ifndef EE_DEFAULTS_FILE
# define EE_DEFAULTS_FILE (CS)"$EEGLRUNTIME/defaults.vim"
#endif

#ifndef EEGLINFO_FILE
#define EEGLINFO_FILE (CS)"$HOME/.config/eegl/.eeglinfo"
#endif

#define SYNTAX_FNAME   "$EEGLRUNTIME/ftype/%s.vim"

#define DFLT_BDIR    "$HOME/.local/state/eegl"    // default for 'backupdir'

#define DFLT_DIR     ".,~/tmp,/var/tmp,/tmp" // default for 'directory'

#ifndef DFLT_VDIR
#define DFLT_VDIR    "$HOME/.config/eegl/view"       // default for 'viewdir'
#define XDG_VDIR    "~/.config/eegl/view"
#endif

#define DFLT_ERRORFILE      "errors.err"

// Try several directories to put the temp files.
#define TEMPDIRNAMES  "$TMPDIR", "/tmp", ".", "$HOME"
#define TEMPNAMELEN    256

// Special wildcards that need to be handled by the shell
#define SPECIAL_WILDCHAR    "`'{" //}

//Unix has plenty of memory, use large buffers
#define CMDBUFFSIZE 1024   // size of the command processing buffer


#ifndef DFLT_MAXMEM
#define DFLT_MAXMEM   (12*1024)    // use up to 12 Mbytes for a buffer
#endif


#ifndef PROTO
#define mch_rename(src, dst) rename(src, dst)
#define mch_getenv(x) (CS)getenv((char *)(x))
#define mch_setenv(name, val, x) setenv((char *)name, (char *)val, x)
#endif

#include <string.h>

#if defined(HAVE_SETJMP_H)
# include <setjmp.h>
# ifdef HAVE_SIGSETJMP
#  define JMP_BUF sigjmp_buf
#  define SETJMP(x) sigsetjmp((x), 1)
#  define LONGJMP siglongjmp
# else
#  define JMP_BUF jmp_buf
#  define SETJMP(x) setjmp(x)
#  define LONGJMP longjmp
# endif
#endif


#include <sys/ioctl.h>

// use fork/exec to start the shell

#include <sys/wait.h>

# ifndef WEXITSTATUS
#   define WEXITSTATUS(stat_val) (((stat_val) >> 8) & 0377)
# endif

# ifndef WIFEXITED
#   define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
# endif


#include <string.h>
#include <sys/utsname.h>
#include <termios.h>

//}}}

#ifndef SEEK_SET
# define SEEK_SET 0
#endif
#ifndef SEEK_END
# define SEEK_END 2
#endif

// Mark unused function arguments with UNUSED, so that gcc -Wunused-parameter
// can be used to check for mistakes.
#ifndef UNUSED
# define UNUSED __attribute__((unused))
#endif

#include <locale.h>

#define FMT_LONG PRI64
#define FMT_ULONG PRIu64
#define FMT_ULONG_HEX PRIx64
#define FMT_UNT "%" PRIu32
#define FMT_INT "%" PRId32

#define PATH_ESC_CHARS ((Byte *)" \t\n*?[{`$\\%#'\"|!<")
#define SHELL_ESC_CHARS ((Byte *)" \t\n*?[{`$\\%#'\"|!<>();&")
#define BUFFER_ESC_CHARS ((Byte *)" \t\n*?[`$\\%#'\"|!<")

// length of a buffer to store a number in ASCII (64 bits binary + NUL)
#define NUMBUFLEN 65

typedef unsigned char Byte;

#define SCANF_HEX_ULONG       "%lx"
#define SCANF_DECIMAL_ULONG   "%lu"
#define PRINTF_HEX_ULONG      "0x%lx"
#define PRINTF_DECIMAL_ULONG  SCANF_DECIMAL_ULONG

//Only systems which use configure will have SIZEOF_OFF_T and EE_SIZEOF_LONG defined, which is 
//ok since those are the same systems which can have varying sizes for off_t.  The other 
//systems will continue to use "%ld" to print off_t since off_t is simply a typedef to long for 
//them.
#if defined(SIZEOF_OFF_T) && (SIZEOF_OFF_T > EE_SIZEOF_LONG)
# define LONG_LONG_OFF_T
#endif

//We use 64-bit file functions here, if available.  E.g. ftello() returns
//off_t instead of long, which helps if long is 32 bit and off_t is 64 bit.
//We assume that when fseeko() is available then ftello() is too.
#ifdef PROTO
typedef long off_T;
#else
typedef off_t off_T;
#endif

//The characters and attributes cached for the screen.
typedef Byte Byte;
#define MAX_TYPENR 65535


//{{{version

//Define the version number, name, etc.
//The patchlevel is in included_patches[], in version.c.

// Trick to turn a number into a string.
#define EE_TOSTR_(a)  #a
#define EE_TOSTR(a)   EE_TOSTR_(a)

// Values that change for a new release.
#define EEGL_VERSION_MAJOR      0
#define EEGL_VERSION_MINOR      9
#define EEGL_VERSION_BUILD      285
#define EEGL_VERSION_BUILD_BCD      0x11d
#define EEGL_VERSION_DATE_ONLY      "2025 Oct 15"

// Values based on the above
#define EEGL_VERSION_MAJOR_STR  EE_TOSTR(EEGL_VERSION_MAJOR)
#define EEGL_VERSION_MINOR_STR  EE_TOSTR(EEGL_VERSION_MINOR)
#define EEGL_VERSION_100        (EEGL_VERSION_MAJOR * 100 + EEGL_VERSION_MINOR)

#define EEGL_VERSION_BUILD_STR  EE_TOSTR(EEGL_VERSION_BUILD)
#ifndef EEGL_VERSION_PATCHLEVEL
#define EEGL_VERSION_PATCHLEVEL 0
#endif

// Patchlevel with leading zeros
#if EEGL_VERSION_PATCHLEVEL < 10
#define LEADZERO(x) 000 ## x
#elif EEGL_VERSION_PATCHLEVEL < 100
#define LEADZERO(x) 00 ## x
#elif EEGL_VERSION_PATCHLEVEL < 1000
#define LEADZERO(x) 0 ## x
#else
#define LEADZERO(x) x
#endif

#define EEGL_VERSION_PATCHLEVEL_STR   EE_TOSTR(LEADZERO(EEGL_VERSION_PATCHLEVEL))

//EEGL_VERSION_NODOT is used for the runtime directory name.
//EEGL_VERSION_SHORT is copied into the swap file (max. length is 6 chars).
//EEGL_VERSION_MEDIUM is used for the startup-screen.
//EEGL_VERSION_LONG is used for the ":version" command and "eegl -h".
#define EEGL_VERSION_NODOT     "eegl" EEGL_VERSION_MAJOR_STR EEGL_VERSION_MINOR_STR
#define EEGL_VERSION_SHORT     EEGL_VERSION_MAJOR_STR "." EEGL_VERSION_MINOR_STR
#define EEGL_VERSION_MEDIUM    EEGL_VERSION_SHORT
#define EEGL_VERSION_LONG_ONLY "EEGL - Extensible Editor for GNU/Linux" EEGL_VERSION_MEDIUM
#define EEGL_VERSION_LONG_HEAD EEGL_VERSION_LONG_ONLY " (" EEGL_VERSION_DATE_ONLY
#define EEGL_VERSION_LONG      EEGL_VERSION_LONG_HEAD ")"
#define EEGL_VERSION_LONG_DATE EEGL_VERSION_LONG_HEAD ", compiled "

//}}}

// Minimal portal dimensions. Must be positive
#define MIN_PORTAL_WIDTH 10
#define MIN_PORTAL_HEIGHT 10

#define FOLD_NEST_MAX 32

//{{{:::macros

//Macros should be ALL_CAPS. An exception is for where a function is
//replaced and an argument is not used more than once.

//This will let macros expand before gluing them 
#define _GL(x, y) x ## y
#define GLUE(x, y) _GL(x, y)

//IF_DEF( macro )( code ) includes the bracketed code only if the specified macro is defined (as empty).
#define COMMA() ,
#define ARG_1_( _1, ... )     _1
#define ARG_1( ... )          ARG_1_( __VA_ARGS__ )
#define ARG_2_( _1, _2, ... ) _2
#define ARG_2( ... )          ARG_2_( __VA_ARGS__ )
#define INCL( ... )           __VA_OPT__( __VA_ARGS__)
#define OMIT( ... )
#define IF_DEF( macro )       ARG_2( COMMA macro () INCL, OMIT, )

//Position comparisons
#define LT_POS(a, b) (((a).lnum != (b).lnum) \
         ? (a).lnum < (b).lnum \
         : (a).col != (b).col \
             ? (a).col < (b).col \
             : (a).coladd < (b).coladd)
#define LT_POSP(a, b) (((a)->lnum != (b)->lnum) \
         ? (a)->lnum < (b)->lnum \
         : (a)->col != (b)->col \
             ? (a)->col < (b)->col \
             : (a)->coladd < (b)->coladd)
#define EQUAL_POS(a, b) (((a).lnum == (b).lnum) && ((a).col == (b).col) && ((a).coladd == (b).coladd))
#define CLEAR_POS(a) do {(a)->lnum = 0; (a)->col = 0; (a)->coladd = 0;} while (0)
#define EMPTY_POS(a) ((a).lnum == 0 && (a).col == 0 && (a).coladd == 0)

#define LTOREQ_POS(a, b) (LT_POS(a, b) || EQUAL_POS(a, b))

// LINEEMPTY() - return TRUE if the line is empty
#define LINEEMPTY(p) (*ml_get(p) == ZERO)

// return TRUE if the current book is empty
#define CURBOOK_EMPTY() (curBook->mem.lineCount == 1 && *ml_get((LineNr)1) == ZERO)

//{{{static maps

// This counts the number of args
#define NARGS_SEQ(_0,_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16,_17,_18,_19,_20,_21,\
      _22,_23,_24,_25,_26,_27,_28,_29,_30,_31,_32,_34,_35,_36,_37,_38,N,...) N
#define NARGS(...) NARGS_SEQ(__VA_ARGS__, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25,\
      24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)

// Map an operator to every argument at compile time
#define SMAP(op, ...) GLUE(SMAP_, NARGS(__VA_ARGS__))(op, __VA_ARGS__)
#define SMAP_1(m, x0) m(x0)
#define SMAP_2(m, x0, x1) m(x0), m(x1)
#define SMAP_3(m, x0, x1, x2) m(x0), m(x1), m(x2)
#define SMAP_4(m, x0, x1, x2, x3) m(x0), m(x1), m(x2), m(x3)
#define SMAP_5(m, x1, x2, x3, x4, x5) SMAP_4(m, x1, x2, x3, x4), m(x5)
#define SMAP_6(m, x1, x2, x3, x4, x5, x6) m(x1), m(x2), m(x3), m(x4), m(x5), m(x6)
#define SMAP_7(m, x1, x2, x3, x4, x5, x6, x7) SMAP_4(m, x1, x2, x3, x4), SMAP_3(m, x5, x6, x7)
#define SMAP_8(m, x0, x1, x2, x3, x4, x5, x6, x7) SMAP_4(m, x0, x1, x2, x3), SMAP_4(m, x4, x5, x6, x7)
#define SMAP_9(m, x0, x1, x2, x3, x4, x5, x6, x7, x8) \
   SMAP_8(m, x0, x1, x2, x3, x4, x5, x6, x7), m(x8)
#define SMAP_10(m, x0, x1, x2, x3, x4, x5, x6, x7, x8, x9) \
   SMAP_8(m, x0, x1, x2, x3, x4, x5, x6, x7), SMAP_2(m, x8, x9)
#define SMAP_11(m, x0, x1, x2, x3, x4, x5, x6, x7, \
                    x8, x9, x10) \
   SMAP_8(m, x0, x1, x2, x3, x4, x5, x6, x7), SMAP_3(m, x8, x9, x10)
#define SMAP_12(m, x0, x1, x2, x3, x4, x5, x6, x7, \
                    x8, x9, x10, x11) \
   SMAP_8(m, x0, x1, x2, x3, x4, x5, x6, x7), SMAP_4(m, x8, x9, x10, x11)
#define SMAP_13(m, x0, x1, x2, x3, x4, x5, x6, x7, \
                    x8, x9, x10, x11, x12) \
   SMAP_8(m, x0, x1, x2, x3, x4, x5, x6, x7), SMAP_5(m, x8, x9, x10, x11, x12)
#define SMAP_14(m, x0, x1, x2, x3, x4, x5, x6, x7, \
                    x8, x9, x10, x11, x12, x13) \
   SMAP_8(m, x0, x1, x2, x3, x4, x5, x6, x7), SMAP_6(m, x8, x9, x10, x11, x12, x13)
#define SMAP_15(m, x0, x1, x2, x3, x4, x5, x6, x7, \
                    x8, x9, x10, x11, x12, x13, x14) \
   SMAP_8(m, x0, x1, x2, x3, x4, x5, x6, x7), SMAP_7(m, x8, x9, x10, x11, x12, x13, x14)
#define SMAP_16(m, x0, x1, x2, x3, x4, x5, x6, x7, \
                    x8, x9, x10, x11, x12, x13, x14, x15 ) \
   SMAP_8(m, x0, x1, x2, x3, x4, x5, x6, x7), SMAP_8(m, x8, x9, x10, x11, x12, x13, x14, x15)
#define SMAP_17(m, x0, x1, x2, x3, x4, x5, x6, x7, \
                    x8, x9, x10, x11, x12, x13, x14, x15, x16 ) \
   SMAP_8(m, x0, x1, x2, x3, x4, x5, x6, x7), SMAP_8(m, x8, x9, x10, x11, x12, x13, x14, x15),\
   m(x16)
#define SMAP_18(m, x0, x1, x2, x3, x4, x5, x6, x7, \
                    x8, x9, x10, x11, x12, x13, x14, x15, x16, x17 ) \
   SMAP_8(m, x0, x1, x2, x3, x4, x5, x6, x7), SMAP_8(m, x8, x9, x10, x11, x12, x13, x14, x15),\
   SMAP_2(m, x16, x17)
#define SMAP_19(m, x0, x1, x2, x3, x4, x5, x6, x7, \
                    x8, x9, x10, x11, x12, x13, x14, x15, x16, x17, x18 ) \
   SMAP_8(m, x0, x1, x2, x3, x4, x5, x6, x7), SMAP_8(m, x8, x9, x10, x11, x12, x13, x14, x15),\
   SMAP_3(m, x16, x17, x18)
   
   
// Map an operator to first element of every pair, and assemble them into pairs
// SMAP1((int), a, b, c, d) => {(int)(a), b}, {(int)(c), d}
#define SMAP1(op, ...) GLUE(SMAP1_, NARGS(__VA_ARGS__))(op, __VA_ARGS__)
#define SMAP1_2(m, x0, y0) { m(x0), y0 }
#define SMAP1_4(m, x0, y0, x1, y1) {m(x0), y0}, {m(x1), y1}
#define SMAP1_6(m, x0, y0, x1, y1, x2, y2) { m(x0), y0 }, { m(x1), y1}, {m(x2), y2}
#define SMAP1_8(m, x0, y0, x1, y1, x2, y2, x3, y3) \
      {m(x0), y0}, {m(x1), y1}, {m(x2), y2}, {m(x3), y3}
#define SMAP1_10(m, x0, y0, x1, y1, x2, y2, x3, y3, x4, y4) \
      SMAP1_8(m, x0, y0, x1, y1, x2, y2, x3, y3), {m(x4), y4} 
#define SMAP1_12(m, x0, y0, x1, y1, x2, y2, x3, y3, x4, y4, x5) \
      SMAP1_8(m, x0, y0, x1, y1, x2, y2, x3, y3), SMAP1_4(m, x4, y4, x5, y5) 
#define SMAP1_14(m, x0, y0, x1, y1, x2, y2, x3, y3, x4, y4, x5, x7) \
      SMAP1_8(m, x0, y0, x1, y1, x2, y2, x3, y3), SMAP1_6(m, x4, y4, x5, y5, x6, y6) 
#define SMAP1_16(m, x0, y0, x1, y1, x2, y2, x3, y3, x4, y4, x5, y5, x6, y6, x7, y7) \
      SMAP1_8(m, x0, y0, x1, y1, x2, y2, x3, y3), SMAP1_8(m, x4, y4, x5, y5, x6, y6, x7, y7)
#define SMAP1_18(m, x0, y0, x1, y1, x2, y2, x3, y3, x4, y4, x5, y5, x6, y6, x7, y7, x8, y8) \
      SMAP1_8(m, x0, y0, x1, y1, x2, y2, x3, y3), SMAP1_8(m, x4, y4, x5, y5, x6, y6, x7, y7),\
      SMAP1_2(m, x8, y8)
#define SMAP1_20(m, x0, y0, x1, y1, x2, y2, x3, y3, x4, y4, x5, y5, x6, y6, x7, y7, x8, y8, \
                 x9, y9) \
      SMAP1_8(m, x0, y0, x1, y1, x2, y2, x3, y3), SMAP1_8(m, x4, y4, x5, y5, x6, y6, x7, y7),\
      SMAP1_4(m, x8, y8, x9, y9)
#define SMAP1_22(m, x0, y0, x1, y1, x2, y2, x3, y3, x4, y4, x5, y5, x6, y6, x7, y7, x8, y8, \
                 x9, y9, x10, y10) \
      SMAP1_8(m, x0, y0, x1, y1, x2, y2, x3, y3), SMAP1_8(m, x4, y4, x5, y5, x6, y6, x7, y7),\
      SMAP1_6(m, x8, y8, x9, y9, x10, y10)
#define SMAP1_24(m, x0, y0, x1, y1, x2, y2, x3, y3, x4, y4, x5, y5, x6, y6, x7, y7, x8, y8, \
                 x9, y9, x10, y10, x11, y11) \
      SMAP1_8(m, x0, y0, x1, y1, x2, y2, x3, y3), SMAP1_8(m, x4, y4, x5, y5, x6, y6, x7, y7),\
      SMAP1_8(m, x8, y8, x9, y9, x10, y10, x11, y11)
#define SMAP1_26(m, x0, y0, x1, y1, x2, y2, x3, y3, x4, y4, x5, y5, x6, y6, x7, y7, x8, y8, \
                 x9, y9, x10, y10, x11, y11, x12, y12) \
      SMAP1_8(m, x0, y0, x1, y1, x2, y2, x3, y3), SMAP1_8(m, x4, y4, x5, y5, x6, y6, x7, y7),\
      SMAP1_8(m, x8, y8, x9, y9, x10, y10, x11, y11), SMAP1_2(m, x12, y12)
#define SMAP1_28(m, x0, y0, x1, y1, x2, y2, x3, y3, x4, y4, x5, y5, x6, y6, x7, y7, x8, y8, \
                 x9, y9, x10, y10, x11, y11, x12, y12, x13, y13) \
      SMAP1_8(m, x0, y0, x1, y1, x2, y2, x3, y3), SMAP1_8(m, x4, y4, x5, y5, x6, y6, x7, y7),\
      SMAP1_8(m, x8, y8, x9, y9, x10, y10, x11, y11), SMAP1_4(m, x12, y12, x13, y13)
#define SMAP1_30(m, x0, y0, x1, y1, x2, y2, x3, y3, x4, y4, x5, y5, x6, y6, x7, y7, x8, y8, \
                 x9, y9, x10, y10, x11, y11, x12, y12, x13, y13, x14, y14) \
      SMAP1_8(m, x0, y0, x1, y1, x2, y2, x3, y3), SMAP1_8(m, x4, y4, x5, y5, x6, y6, x7, y7),\
      SMAP1_8(m, x8, y8, x9, y9, x10, y10, x11, y11), SMAP1_6(m, x12, y12, x13, y13, x14, y14)
#define SMAP1_32(m, x0, y0, x1, y1, x2, y2, x3, y3, x4, y4, x5, y5, x6, y6, x7, y7, x8, y8, \
                 x9, y9, x10, y10, x11, y11, x12, y12, x13, y13, x14, y14, x15, y15) \
      SMAP1_8(m, x0, y0, x1, y1, x2, y2, x3, y3), SMAP1_8(m, x4, y4, x5, y5, x6, y6, x7, y7),\
      SMAP1_8(m, x8, y8, x9, y9, x10, y10, x11, y11), \
      SMAP1_8(m, x12, y12, x13, y13, x14, y14, x15, y15)
#define SMAP1_34(m, x0, y0, x1, y1, x2, y2, x3, y3, x4, y4, x5, y5, x6, y6, x7, y7, x8, y8, \
                 x9, y9, x10, y10, x11, y11, x12, y12, x13, y13, x14, y14, x15, y15,\
                 x16, y16) \
      SMAP1_8(m, x0, y0, x1, y1, x2, y2, x3, y3), SMAP1_8(m, x4, y4, x5, y5, x6, y6, x7, y7),\
      SMAP1_8(m, x8, y8, x9, y9, x10, y10, x11, y11), \
      SMAP1_8(m, x12, y12, x13, y13, x14, y14, x15, y15), SMAP1_2(m, x16, y16)
#define SMAP1_36(m, x0, y0, x1, y1, x2, y2, x3, y3, x4, y4, x5, y5, x6, y6, x7, y7, x8, y8, \
                 x9, y9, x10, y10, x11, y11, x12, y12, x13, y13, x14, y14, x15, y15,\
                 x16, y16, x17, y17) \
      SMAP1_8(m, x0, y0, x1, y1, x2, y2, x3, y3), SMAP1_8(m, x4, y4, x5, y5, x6, y6, x7, y7),\
      SMAP1_8(m, x8, y8, x9, y9, x10, y10, x11, y11), \
      SMAP1_8(m, x12, y12, x13, y13, x14, y14, x15, y15), SMAP1_4(m, x16, y16, x17, y17)

   
//}}} 

//The is*() and to*() functions declared in <ctype.h> have undefined behavior for values other 
//than EOF outside the range of unsigned char. If plain char is signed, a call with a negative
//value has undefined behavior. These macros cast the argument to unsigned char. (Most 
//implementations behave more or less sanely with negative values, and most character values in 
//practice are positive, but we want to avoid undefined behavior anyway)
#define SAFE_isalnum(c)  (isalnum((unsigned char)(c)))
#define SAFE_isalpha(c)  (isalpha((unsigned char)(c)))
#define SAFE_isblank(c)  (isblank((unsigned char)(c)))
#define SAFE_iscntrl(c)  (iscntrl((unsigned char)(c)))
#define SAFE_isdigit(c)  (isdigit((unsigned char)(c)))
#define SAFE_isgraph(c)  (isgraph((unsigned char)(c)))
#define SAFE_islower(c)  (islower((unsigned char)(c)))
#define SAFE_isprint(c)  (isprint((unsigned char)(c)))
#define SAFE_ispunct(c)  (ispunct((unsigned char)(c)))
#define SAFE_isspace(c)  (isspace((unsigned char)(c)))
#define SAFE_isupper(c)  (isupper((unsigned char)(c)))
#define SAFE_isxdigit(c) (isxdigit((unsigned char)(c)))
#define SAFE_tolower(c)  (tolower((unsigned char)(c)))
#define SAFE_toupper(c)  (toupper((unsigned char)(c)))

//toupper() and tolower() that use the current locale.
//On some systems toupper()/tolower() only work on lower/uppercase characters, first use islower() 
//or isupper() then.
//Careful: Only call TOUPPER_LOC() and TOLOWER_LOC() with a character in the range 0 - 255.  
//toupper()/tolower() on some systems can't handle others.
//Note: It is often better to use MB_TOLOWER() and MB_TOUPPER(), because many
//toupper() and tolower() implementations only work for ASCII.
#ifdef BROKEN_TOUPPER
#define TOUPPER_LOC(c)   (SAFE_islower(c) ? SAFE_toupper(c) : (c))
#define TOLOWER_LOC(c)   (SAFE_isupper(c) ? SAFE_tolower(c) : (c))
#else
#define TOUPPER_LOC      SAFE_toupper
#define TOLOWER_LOC      SAFE_tolower
#endif

//MB_ISLOWER() and MB_ISUPPER() are to be used on multi-byte characters.  But
//don't use them for negative values!
#define MB_ISLOWER(c)   eeIsLower(c)
#define MB_ISUPPER(c)   eeIsUpper(c)
#define MB_TOLOWER(c)   eeglToLower(c)
#define MB_TOUPPER(c)   eeglToUpper(c)
#define MB_CASEFOLD(c)  (utf_fold(c))


// Returns empty string if it is NULL.
#define EMPTY_IF_NULL(x) ((x) ? (x) : (Byte *)"")

//Adjust chars in a language according to 'langmap' option. NOTE that there is no noticeable 
//overhead if 'langmap' is not set. When set the overhead for characters < 256 is small.
//Don't apply 'langmap' if the character comes from the Stuff buffer or from
//a mapping and the langnoremap option was set.
//The do-while is just to ignore a ';' after the macro.
# define LANGMAP_ADJUST(c, condition) \
   do { \
   if (p_langmap \
      && (condition) \
      && (p_lrm || (!p_lrm && keyWasTypedG)) \
      && !keyWasStuffedG \
      && (c) < UNT) \
   { \
    if ((c) < 256) \
       c = langmap_mapchar[c]; \
    else \
       c = langmap_adjust_mb(c); \
   } \
   } while (0)

//EE_ISBREAK() is used very often if 'linebreak' is set, use a macro to make
//it work fast.  Only works for single byte characters!
#define EE_ISBREAK(c) ((c) < 256 && breakat_flags[(Byte)(c)])

#define mch_access(n, p)   access((char*)(n), (p))

#define E (Byte*)""

#define TIME_MSG(s) do { if (time_fd != NULL) time_msg((CS)s, NULL); } while (0)



#define MB_CHARLEN(p)       (mb_charlen(p))
#define MB_CHAR2LEN(c)       (mb_char2len(c))
#define MB_CHAR2BYTES(c, b) do { (b) += mb_char2bytes((c), (b)); } while (0)

# define DO_AUTOCHDIR do { } while (0)

#define RESET_BINDING(po)  do { \
   (po)->o.scrollBind = FALSE; (po)->o.cursorBind = FALSE; } while (0)

# define PLINES_NOFILL(x) plines_nofill(x)
# define PLINES_WIN_NOFILL(w, l, h) plines_win_nofill((w), (l), (h))

#include <float.h>
  // for isnan() and isinf()
#include <math.h>

# ifdef USING_FLOAT_STUFF
#  if !defined(INFINITY)
#   if defined(DBL_MAX)
#     define INFINITY (DBL_MAX+DBL_MAX)
#   else
#    define INFINITY (1.0 / 0.0)
#   endif
#  endif
#  if !defined(NAN)
#   define NAN (INFINITY-INFINITY)
#  endif
#  if !defined(DBL_EPSILON)
#   define DBL_EPSILON 2.2204460492503131e-16
#  endif
# endif

#define FUNCARG(fp, j)   ((Byte **)(fp->args.c))[j]

//In a hashtab item "hi_key" points to "key" in a dictitem.
//This avoids adding a pointer to the hashtab item.
//DI2HIKEY() converts a dictitem pointer to a EeSetItem key pointer.
//HIKEY2DI() converts a EeSetItem key pointer to a dictitem pointer.
//HI2DI() converts a EeSetItem pointer to a dictitem pointer.
#define DI2HIKEY(di) ((di)->key)
#define HIKEY2DI(p)  ((DictItem *)((p) - offsetof(DictItem, key)))
#define HI2DI(hi)     HIKEY2DI((hi)->hi_key)

//Like eeglFree(), and also set the pointer to NULL.
#define EE_CLEAR(p) \
    do { \
   eeglFree(p); \
   (p) = NULL; \
    } while (0)

//Free a string and set it's pointer to NULL and length to 0
#define EE_CLEAR_STRING(s) \
    do { \
   EE_CLEAR(s.c); \
   s.len = 0; \
    } while (0)

// Whether a command index indicates a user command.
#define IS_USER_COMMAND(idx) ((int)(idx) < 0)

// Give an error in curPor is a popup window and evaluate to TRUE.
#define PORTAL_IS_POPUP(po) ((po)->pup.flags != 0)


#ifdef ABORT_ON_INTERNAL_ERROR
#define ESTACK_CHECK_DECLARATION int estack_len_before
#define ESTACK_CHECK_SETUP do { estack_len_before = exestack.len; } while (0)
#define ESTACK_CHECK_NOW \
    do { \
   if (estack_len_before != exestack.len) \
       internalErrFmtMsg("Exestack length expected: %d, actual: %d", estack_len_before, exestack.len); \
    } while (0)
# define CHECK_CURBOOK \
    do { \
   if (curPor && curPor->book != curBook) \
       internalErrMsg("curBook != curPor->book"); \
    } while (0)
#else
# define ESTACK_CHECK_DECLARATION do { /**/ } while (0)
# define ESTACK_CHECK_SETUP do { /**/ } while (0)
# define ESTACK_CHECK_NOW do { /**/ } while (0)
# define CHECK_CURBOOK do { /**/ } while (0)
#endif

//Inline the condition for performance.
#define CHECK_LIST_MATERIALIZE(l) \
    do { \
   if ((l)->first == &range_list_item) \
       range_list_materialize(l); \
    } while (0)

//Inlined version of ga_grow() with optimized condition that it fails.
#define GA_GROW_FAILS(gap, n) unlikely((((gap)->cap - (gap)->len < (n)) ? ga_grow_inner((gap), (n)) : OK) == FAIL)
//Inlined version of ga_grow() with optimized condition that it succeeds.
#define GA_GROW_OK(gap, n) likely((((gap)->cap - (gap)->len < (n)) ? ga_grow_inner((gap), (n)) : OK) == OK)


#define FOR_ALL_PORTALS(wp) \
    for ((wp) = firstPor; (wp) != NULL; (wp) = (wp)->next)
#define FOR_ALL_FRAMES(frp, first_frame) \
    for ((frp) = first_frame; (frp) != NULL; (frp) = (frp)->next)
#define FOR_ALL_TABS(tp) \
    for ((tp) = firstTabG; (tp) != NULL; (tp) = (tp)->next)
#define FOR_ALL_PORTALS_IN_TAB(tp, wp) \
    for ((wp) = ((tp) == NULL || (tp) == curtab) \
       ? firstPor : (tp)->firstPor; (wp); (wp) = (wp)->next)
/*
 * When using this macro "break" only breaks out of the inner loop. Use "goto"
 * to break out of the tabpage loop.
 */
#define FOR_ALL_TAB_PORTALS(tp, wp) \
   for ((tp) = firstTabG; (tp) != NULL; (tp) = (tp)->next) \
   for ((wp) = ((tp) == curtab) \
      ? firstPor : (tp)->firstPor; (wp); (wp) = (wp)->next)

#define FOR_ALL_POPUPPORTS(wp) \
    for ((wp) = firstPopupPortG; (wp) != NULL; (wp) = (wp)->next)
#define FOR_ALL_POPUPPORTS_IN_TAB(tp, wp) \
    for ((wp) = (tp)->firstPopupPort; (wp) != NULL; (wp) = (wp)->next)

#define FOR_ALL_BOOKS(buf) \
    for ((buf) = firstBook; (buf) != NULL; (buf) = (buf)->next)

#define FOR_ALL_BOOK_PORTINFOS(book, wip) \
    for ((wip) = (book)->portInfos; (wip) != NULL; (wip) = (wip)->next)

#define FOR_ALL_SPELL_LANGS(slang) \
    for ((slang) = first_lang; (slang) != NULL; (slang) = (slang)->sl_next)

// Iterate over all the items in a List
#define FOR_ALL_LIST_ITEMS(l, li) \
    for ((li) = (l) == NULL ? NULL : (l)->first; (li) != NULL; (li) = (li)->next)

// Iterate over all the items in a hash table
#define FOR_ALL_HASHTAB_ITEMS(ht, hi, todo) \
    for ((hi) = (ht)->array; (todo) > 0; ++(hi))

#define KEYVALUE_ENTRY(k, v) {(k), {((Byte *)v), STRLEN_LITERAL(v)}}

#ifndef RGB
#define RGB(r, g, b)   (((r)<<16) | ((g)<<8) | (b))
#endif

#define nameBuffTextG (Text){nameBuffG, MAXPATHL}

//}}}
//{{{:::generics

//To add a new generic type, add it and its constructor here.
//To add a generic function, add its body and overloading boilerplate into into generic.h.
//To instantiate a generic function in a module, do something like this:
//
//  typedef DIR* DirPtr;        // type parameter
//  LIST_TY(DirPtr)             // concrete type
//  private LIST_CREATE(DirPtr) // constructor for concrete type
//  
//  #define ADD_LIST_TY DirPtr  // which method to instantiate ("add" for the "list" type)
//  #include "generic.h"        // actual code generation, including adding it to the _Generic

//{{{List

#define LIST_TY(T) typedef struct {\
      T* c;\
      Unt len;\
      Unt cap;\
      Arena* a;\
   } _GL(L, T);
   
#define LIST_CREATE(T)\
_GL(L, T) * _GL(createL, T) (int initCapacity, Arena* a) {\
      int capacity = initCapacity < 4 ? 4 : initCapacity;\
      _GL(L, T) * result = allocate(_GL(L, T), a);\
      result->cap = capacity;\
      result->len = 0;\
      result->a = a;\
      T* arr = allocateArray(capacity, T, a);\
      result->c = arr;\
      return result;\
   }
   
LIST_TY(Int)
LIST_TY(Unt)

#define last(l) (l)->c[(l)->len - 1]
#define sLast(l) (l).c[(l).len - 1]
   
//}}}
//{{{equality

#define eq(a, b) _Generic((a),\
   Text: _Generic((b),\
         Text: eq_Text_Text,\
         CS: eq_Text_CString\
      ),\
   CS: eq_CString_CString\
)(a, b)

//}}}
//}}}

#include <errno.h>
#include <assert.h>

#include <inttypes.h>
#include <wctype.h>

// for offsetof()
#ifndef PROTO
#include <stddef.h>
#endif


#include <sys/select.h>

// ================ end of the header file puzzle ===============

// The _() stuff is for using gettext().  It is a no-op when libintl.h is not
// found or the +multilang feature is disabled.
// Use NGETTEXT(single, multi, number) to get plural behavior:
// - single - message for singular form
// - multi  - message for plural form
// - number - the count
#include <libintl.h>
#define _(x) (CS)gettext((char *)(x))
#define NGETTEXT(x, xs, n) (CS)ngettext((char*)(x), (char*)(xs), (n))
#define N_(x) (CS)x

//Flags for update_screen(). The higher the value, the higher the priority.
#define UPD_VALID_NO_UPDATE 5  // no new changes, keep the command line if possible
#define UPD_VALID          10  // book not changed, or changes marked with b_mod_*
#define UPD_INVERTED       20  // redisplay inverted part that changed
#define UPD_INVERTED_ALL   25  // redisplay whole inverted part
#define UPD_REDRAW_TOP     30  // display first w_upd_rows screen lines
#define UPD_SOME_VALID     35  // like UPD_NOT_VALID but may scroll
#define UPD_NOT_VALID      40  // book needs complete redraw
#define UPD_CLEAR          50  // screen messed up, clear it

// flags for screen_line()
#define SLF_RIGHTLEFT  1
#define SLF_POPUP      2
#define SLF_INC_VCOL   4

//Flags for w_valid.
//These are set when something in a window structure becomes invalid, except
//when the cursor is moved.  Call check_cursor_moved() before testing one of the flags.
//These are reset when that thing has been updated and is valid again.
//
//Every function that invalidates one of these must call one of the invalidate_* functions.
//
//w_valid is supposed to be used only in screen.c.  From other files, use the
//functions that set or reset the flags.
//
// VALID_BOTLINE    VALID_BOTLINE_AP
//     on                  on         w_botline valid
//    off                  on         w_botline approximated
//    off                 off         w_botline not valid
//     on                 off         not possible
#define VALID_WROW       0x01   //w_wrow (window row) is valid
#define VALID_WCOL       0x02   //w_wcol (window col) is valid
#define VALID_VIRTCOL    0x04   //w_virtcol (file col) is valid
#define VALID_CHEIGHT    0x08   //w_cline_height and w_cline_folded valid
#define VALID_CROW       0x10   //w_cline_row is valid
#define VALID_BOTLINE    0x20   //w_botline and w_empty_rows are valid
#define VALID_BOTLINE_AP 0x40   //w_botline is approximated
#define VALID_TOPLINE    0x80   //w_topline is valid (for cursor position)

// flags used in w_popup_handled
#define POPUP_HANDLED_1 0x01    //used by mouse_find_win()
#define POPUP_HANDLED_2 0x02    //used by popup_do_filter()
#define POPUP_HANDLED_3 0x04    //used by popup_check_cursor_pos()
#define POPUP_HANDLED_4 0x08    //used by may_update_popup_mask()
#define POPUP_HANDLED_5 0x10    //used by update_popups()

// Terminal hiliting decoration flags
#define DECO_NONE         0
#define DECO_INVERSE      1
#define DECO_BOLD         2
#define DECO_ITALIC       4
#define DECO_UNDERLINE    8
#define DECO_UNDERCURL   16
#define DECO_UNDERDASH   32 
#define DECO_ALTERED_BG  64

// special attribute addition: Put message in history
#define MSG_HIST     64

#define EMPTY_DECO  (Decoration){.hiId = SHORT, .fieldPresence = 0, .flags = 0}

//Values for State.
//
//The lower bits up to 0x80 are used to distinguish normal/visual/op_pending
///cmdline/insert/replace/terminal mode.  This is used for mapping.  If none
//of these bits are set, no mapping is done.  See the comment above do_map().
//The upper bits are used to distinguish between other states and variants of the base modes.
#define MODE_NORMAL      0x01  //Normal mode, command expected
#define MODE_VISUAL      0x02  //Visual mode - use get_real_state()
#define MODE_OP_PENDING  0x04  //Normal mode, operator is pending - use get_real_state()
#define MODE_COMMLINE    0x08  //Editing the command line
#define MODE_INSERT      0x10  //Insert mode, also for Replace mode
#define MODE_LANGMAP     0x20  //Language mapping, can be combined with MODE_INSERT and MODE_CMDLINE
#define MODE_TERMINAL    0x80  //Terminal mode

#define MAP_ALL_MODES    0xff    // all mode bits used for mapping

#define MODE_NORMAL_BUSY (0x1000 | MODE_NORMAL) // Normal mode, busy with a command
#define MODE_HITRETURN   (0x2000 | MODE_NORMAL) // waiting for return or command
#define MODE_ASKMORE     0x3000   // Asking if you want --more--
#define MODE_SETWSIZE    0x4000   // window size has changed
#define MODE_EXTERNCMD   0x5000   // executing an external command
#define MODE_SHOWMATCH   (0x6000 | MODE_INSERT) // show matching paren
#define MODE_CONFIRM     0x7000   // ":confirm" prompt
#define MODE_ALL         0xffff

#define MODE_MAX_LENGTH   4 // max mode length used by get_mode(), including the terminating NUL

// directions
#define FORWARD   1
#define BACKWARD 20

// flags for books
#define BF_RECOVERED   0x01  // book has been recovered
#define BF_CHECK_RO    0x02  // need to check readonly when loading file into book (set by ":e",
                             // may be reset by ":book")
#define BF_NEVERLOADED 0x04  // file has never been loaded into book,
                             // many variables still need to be set
#define BF_NOTEDITED   0x08  // Set when file name is changed after starting to edit, reset when 
                             // file is written out.
#define BF_NEW         0x10  // file didn't exist when editing started
#define BF_NEW_W       0x20  // Warned for BF_NEW and file created
#define BF_READERR     0x40  // got errors while reading the file 
#define BF_DUMMY       0x80  // dummy book, only used internally
#define BF_PRESERVED  0x100  // ":preserve" was used
#define BF_SYN_SET    0x200  // 'syntax' option was set
#define BF_NO_SEA     0x400  // no swap_exists_action (ATTENTION prompt)

// Mask to check for flags that prevent normal writing
#define BF_WRITE_MASK   (BF_NOTEDITED + BF_NEW + BF_READERR)

// Completion context kindss for xp_context when doing command line completion
#define EXPAND_UNSUCCESSFUL   2048
#define EXPAND_OK             1024
#define EXPAND_NOTHING           0
#define EXPAND_COMMANDS          1
#define EXPAND_FILES             2
#define EXPAND_DIRECTORIES       3
#define EXPAND_OPTION            4
#define EXPAND_TAGS              5
#define EXPAND_OLD_OPTION        6
#define EXPAND_HELP              7
#define EXPAND_BUFFERS           8
#define EXPAND_EVENTS            9
#define EXPAND_MENUS            10
#define EXPAND_SYNTAX           11
#define EXPAND_HILITE_GROUP     12
#define EXPAND_AUGROUP          13
#define EXPAND_USER_VARS        14
#define EXPAND_MAPPINGS         15
#define EXPAND_TAGS_LISTFILES   16
#define EXPAND_FUNCTIONS        17
#define EXPAND_USER_FUNC        18
#define EXPAND_EXPRESSION       19
#define EXPAND_MENUNAMES        20
#define EXPAND_USER_COMMANDS    21
#define EXPAND_USER_CMD_FLAGS   22
#define EXPAND_USER_NARGS       23
#define EXPAND_USER_COMPLETE    24
#define EXPAND_ENV_VARS         25
#define EXPAND_LANGUAGE         26
#define EXPAND_COLORS           27
#define EXPAND_COMPILER         28
#define EXPAND_USER_DEFINED     29
#define EXPAND_USER_LIST        30
#define EXPAND_SHELLCMD         31
#define EXPAND_CSCOPE           32
#define EXPAND_SIGN             33
#define EXPAND_PROFILE          34
#define EXPAND_FILETYPE         35
#define EXPAND_FILES_IN_PATH    36
#define EXPAND_OWNSYNTAX        37
#define EXPAND_LOCALES          38
#define EXPAND_HISTORY          39
#define EXPAND_USER             40
#define EXPAND_SYNTIME          41
#define EXPAND_USER_ADDR_TYPE   42
#define EXPAND_PACKADD          43
#define EXPAND_MESSAGES         44
#define EXPAND_MAPCLEAR         45
#define EXPAND_ARGLIST          46
#define EXPAND_DIFF_BUFFERS     47
#define EXPAND_DISASSEMBLE      48
#define EXPAND_BREAKPOINT       49
#define EXPAND_SCRIPTNAMES      50
#define EXPAND_RUNTIME          51
#define EXPAND_STRING_OPTION    52
#define EXPAND_ARGOPT           54
#define EXPAND_TERMINALOPT      55
#define EXPAND_KEYMAP           56
#define EXPAND_DIRS_IN_CDPATH   57
#define EXPAND_SHELLCMDLINE     58
#define EXPAND_FINDFUNC         59
#define EXPAND_FILETYPECMD      60
#define EXPAND_PATTERN_IN_BUF   61
#define EXPAND_RETAB            62

// Values for nextwild() and ExpandOne().  See ExpandOne() for meaning.
#define WILD_FREE                1
#define WILD_EXPAND_FREE         2
#define WILD_EXPAND_KEEP         3
#define WILD_NEXT                4
#define WILD_PREV                5
#define WILD_ALL                 6
#define WILD_LONGEST             7
#define WILD_ALL_KEEP            8
#define WILD_CANCEL              9
#define WILD_APPLY              10
#define WILD_PAGEUP             11
#define WILD_PAGEDOWN           12

#define WILD_LIST_NOTFOUND    0x01
#define WILD_HOME_REPLACE     0x02
#define WILD_USE_NL           0x04
#define WILD_NO_BEEP          0x08
#define WILD_ADD_SLASH        0x10
#define WILD_KEEP_ALL         0x20
#define WILD_SILENT           0x40
#define WILD_ESCAPE           0x80
#define WILD_ICASE           0x100
#define WILD_ALLLINKS        0x200
#define WILD_IGNORE_COMPLETESLASH   0x400
#define WILD_NOERROR         0x800  // sets EW_NOERROR
#define WILD_BUFLASTUSED    0x1000
#define BOOK_DIFF_FILTER     0x2000
#define WILD_KEEP_SOLE_ITEM 0x4000
#define WILD_MAY_EXPAND_PATTERN 0x8000
#define WILD_FUNC_TRIGGER  0x10000 // called from wildtrigger()

// Flags for expand_wildcards()
#define EW_DIR          0x01    // include directory names
#define EW_FILE         0x02   // include file names
#define EW_NOTFOUND     0x04  // include not found names
#define EW_ADDSLASH     0x08  // append slash to directory name
#define EW_KEEPALL      0x10   // keep all matches
#define EW_SILENT       0x20    // don't print "1 returned" from shell
#define EW_EXEC         0x40   // executable files
#define EW_PATH         0x80   // search in 'path' too
#define EW_ICASE       0x100    // ignore case
#define EW_NOERROR     0x200  // no error for bad regexp
#define EW_NOTWILD     0x400  // add match with literal name if exists
#define EW_KEEPDOLLAR  0x800 // do not escape $, $var is expanded
// Note: mostly EW_NOTFOUND and EW_SILENT are mutually exclusive: EW_NOTFOUND
// is used when executing commands and EW_SILENT for interactive expanding.
#define EW_ALLLINKS   0x1000  // also links not pointing to existing file
#define EW_SHELLCMD   0x2000  // called from expand_shellcmd(), don't check if executable in $PATH
#define EW_DODOT      0x4000  // also files starting with a dot
#define EW_EMPTYOK    0x8000  // no matches is not an error
#define EW_NOTENV    0x10000 // do not expand environment variables
#define EW_CDPATH    0x20000 // search in 'cdpath' too

// Flags for find_file_*() functions.
#define FINDFILE_FILE  0  // only files
#define FINDFILE_DIR   1  // only directories
#define FINDFILE_BOTH  2  // files and directories

#define COLUMNS_WITHOUT_TPL() (visibleColsG - tabpanel_width())
#define TPL_LCOL()  tabpanel_leftcol()

#define P_ENDCOL(wp)  ((wp)->portalCol + (wp)->width)

// Values for the find_pattern_in_path() function args 'type' and 'action':
#define FIND_ANY    1
#define FIND_DEFINE 2
#define CHECK_PATH  3

#define ACTION_SHOW     1
#define ACTION_GOTO     2
#define ACTION_SPLIT    3
#define ACTION_SHOW_ALL 4
#define ACTION_EXPAND   5

// Values for 'options' argument in do_search() and searchit()
#define SEARCH_REV    0x01  // go in reverse of previous dir.
#define SEARCH_ECHO   0x02  // echo the search command and handle options
#define SEARCH_MSG    0x0c  // give messages (yes, it's not 0x04)
#define SEARCH_NFMSG  0x08  // give all messages except not found
#define SEARCH_OPT    0x10  // interpret optional flags
#define SEARCH_HIS    0x20  // put search pattern in history
#define SEARCH_END    0x40  // put cursor at end of match
#define SEARCH_NOOF   0x80  // don't add offset to position
#define SEARCH_START 0x100  // start search without col offset
#define SEARCH_MARK  0x200  // set previous context mark
#define SEARCH_KEEP  0x400  // keep previous search pattern
#define SEARCH_PEEK  0x800  // peek for typed char, cancel search
#define SEARCH_COL  0x1000  // start at specified column instead of zero

// Values for find_ident_under_cursor()
#define FIND_IDENT   1 // find identifier (word)
#define FIND_STRING  2 // find any string (WORD)
#define FIND_EVAL    4 // include "->", "[]" and "."
#define FIND_NOERROR 8 // no error when no word found

// Values for file_name_in_line()
#define FNAME_MESS   1 // give error message
#define FNAME_EXP    2 // expand to path
#define FNAME_HYP    4 // check for hypertext link
#define FNAME_INCL   8 // apply 'includeexpr'
#define FNAME_REL   16 // ".." and "./" are relative to the (current)
                       // file instead of the current directory
#define FNAME_UNESC 32 // remove backslashes used for escaping

// Values for buflist_getfile()
#define GETF_SETMARK 0x01   // set pcmark before jumping
#define GETF_ALT     0x02   // jumping to alternate file (not buf num)
#define GETF_SWITCH  0x04   // respect 'switchbuf' settings when jumping

// Return values of getfile()
#define GETFILE_ERROR       1   // normal error
#define GETFILE_NOT_WRITTEN 2   // "not written" error
#define GETFILE_SAME_FILE   0   // success, same file
#define GETFILE_OPEN_OTHER (-1)   // success, opened another file
#define GETFILE_UNUSED       8
#define GETFILE_SUCCESS(x)  ((x) <= 0)

// Values for bookNew() flags
#define BLN_CURBOOK      1 //may re-use curBook for new book
#define BLN_LISTED       2 //put new book in book list
#define BLN_DUMMY        4 //allocating dummy book
#define BLN_NEW          8 //create a new book
#define BLN_NOOPT       16 //don't copy options to existing book
#define BLN_DUMMY_OK    32 //also find an existing dummy book
#define BLN_REUSE       64 //may re-use number from buf_reuse
#define BLN_NOCURWIN   128 //book is not associated with curPor
#define BLN_MODIFIABLE 256 //book should be modifiable

// Values for in_cinkeys()
#define KEY_OPEN_FORW  0x101
#define KEY_OPEN_BACK  0x102
#define KEY_COMPLETE   0x103 //end of completion

// Values for "noremap" argument of ins_typebuf().  Also used for
// map->m_noremap and menu->noremap[].
#define REMAP_YES      0    //allow remapping
#define REMAP_NONE   4294967295   //no remapping
#define REMAP_SCRIPT 4294967294   //remap script-local mappings only
#define REMAP_SKIP   4294967293   //no remapping for first char

// Values for channel:mch_call_shell() second argument
#define SHELL_FILTER     1  //filtering text
#define SHELL_EXPAND     2  //expanding wildcards
#define SHELL_COOKED     4  //set term to cooked mode
#define SHELL_DOOUT      8  //redirecting output
#define SHELL_SILENT    16  //don't print error returned by command
#define SHELL_READ      32  //read lines and insert into book
#define SHELL_WRITE     64  //write lines from book
#define SHELL_SHOW_MSG 128  //show shell messages

// Values returned by mch_nodetype()
#define NODE_NORMAL   0  //file or directory, check with mch_isdir()
#define NODE_WRITABLE 1  //something we can write to (character device, fifo, socket, ..)
#define NODE_OTHER    2  //non-writable thing (e.g., block device)

// Values for readfile() flags
#define READ_NEW        0x01   //read a file into a new book
#define READ_FILTER     0x02   //read filter output
#define READ_STDIN      0x04   //read from stdin
#define READ_BOOK       0x08   //read from curBook (converting stdin)
#define READ_DUMMY      0x10   //reading into a dummy book
#define READ_KEEP_UNDO  0x20   //keep undo info
#define READ_FIFO       0x40   //read from fifo or socket
#define READ_NOWINENTER 0x80   //do not trigger BufWinEnter
#define READ_NOFILE    0x100   //do not read a file, do trigger BufReadCmd

// Values for change_indent()
#define INDENT_SET   1 //set indent
#define INDENT_INC   2 //increase indent
#define INDENT_DEC   3 //decrease indent

// Values for flags argument for findmatchlimit()
#define FM_BACKWARD   0x01   // search backwards
#define FM_FORWARD    0x02   // search forwards
#define FM_BLOCKSTOP  0x04   // stop at start/end of block
#define FM_SKIPCOMM   0x08   // skip comments

// Values for action argument for bookDo() and closeBook()
#define DOBOOK_GOTO       0   // go to specified book
#define DOBOOK_SPLIT      1   // split portal and go to specified book
#define DOBOOK_UNLOAD     2   // unload specified book(s)
#define DOBOOK_DEL        3   // delete specified book(s) from buflist


#define DOBOOK_WIPE       4   // delete specified book(s) really
#define DOBOOK_WIPE_REUSE 5   // like DOBUF_WIPE and keep number for reuse

// Values for flags argument of bookDo()
#define DOBOOK_FORCEIT   1   // :cmd!
#define DOBOOK_NOPOPUP   2   // skip popup portal books
#define DOBOOK_SKIPHELP  4   // skip or keep help books depending on kind of the starting book

// Values for start argument for bookDo()
#define DOBOOK_CURRENT 0   // "count" book from current book
#define DOBOOK_FIRST   1   // "count" book from first book
#define DOBOOK_LAST    2   // "count" book from last book
#define DOBOOK_MOD     3   // "count" mod. book from current book

// Values for sub_cmd and which_pat argument for search_regcomp()
// Also used for which_pat argument for searchit()
#define RE_SEARCH  0   // save/use pat in/from search_pattern
#define RE_SUBST   1   // save/use pat in/from subst_pattern
#define RE_BOTH    2   // save pat in both patterns
#define RE_LAST    2   // use last used pattern if "pat" is NULL

// Second argument for eeRegexec().
#define RE_MAGIC   1   // 'magic' option
#define RE_STRING  2   // match in string instead of book text
#define RE_STRICT  4   // don't allow [abc] without ]
#define RE_AUTO    8   // automatic engine selection

// values for reg_do_extmatch
# define REX_SET   1   // to allow \z\(...\),
# define REX_USE   2   // to allow \z\1 et al.
# define REX_ALL   (REX_SET | REX_USE)

// Return values for fullpathcmp()
// Note: can use (fullpathcmp() & FPC_SAME) to check for equal files
#define FPC_SAME   1   // both exist and are the same file.
#define FPC_DIFF   2   // both exist and are different files.
#define FPC_NOTX   4   // both don't exist.
#define FPC_DIFFX  6   // one of them doesn't exist.
#define FPC_SAMEX  7   // both don't exist and file names are same.

// flags for do_ecmd()
#define ECMD_HIDE       0x01   //don't free the current book
#define ECMD_SET_HELP   0x02   //set kind = BUF_HELP flag of (new) book before opening file
#define ECMD_OLDBUF     0x04   //use existing book if it exists
#define ECMD_FORCEIT    0x08   //! used in a command
#define ECMD_ADDBUF     0x10   //don't edit, just add to book list
#define ECMD_ALTBUF     0x20   //like ECMD_ADDBUF and set the alternate file
#define ECMD_NOWINENTER 0x40   //do not trigger BufWinEnter
#define ECMD_MODIFIABLE 0x80   //set the @modifiable flag for the new book

// for lnum argument in do_ecmd()
#define ECMD_LASTL (LineNr)0   // use last position in loaded file
#define ECMD_LAST  ((LineNr)-1)   // use last position in all files
#define ECMD_ONE   (LineNr)1   // use first line

// flags for do_cmdline()
#define DOCMD_VERBOSE  0x01   // included command in error message
#define DOCMD_NOWAIT   0x02   // don't call wait_return() and friends
#define DOCMD_REPEAT   0x04   // repeat exec. until getline() returns NULL
#define DOCMD_KEYTYPED 0x08   // don't reset keyWasTypedG
#define DOCMD_EXCRESET 0x10   // reset exception environment (for debugging)
#define DOCMD_KEEPLINE 0x20   // keep typed line for repeating with "."

// flags for beginline()
#define BL_WHITE   1   // cursor on first non-white in the line
#define BL_SOL     2   // use 'sol' option
#define BL_FIX     4   // don't leave cursor on a NUL

// flags for buf_copy_options()
#define BCO_ENTER   1  //going to enter the book
#define BCO_ALWAYS  2  //always copy the options

// flags for do_put()
#define PUT_FIXINDENT     1   //make indent look nice
#define PUT_CURSEND       2   //leave cursor after end of new text
#define PUT_CURSLINE      4   //leave cursor on last line of new text
#define PUT_LINE          8   //put register as lines
#define PUT_LINE_SPLIT   16   //split line for linewise register
#define PUT_LINE_FORWARD 32   //put linewise register below Visual sel.
#define PUT_BLOCK_INNER  64   //in block mode, do not add trailing spaces

// flags for set_indent()
#define SIN_CHANGED  1   //call changed_bytes() when line changed
#define SIN_INSERT   2   //insert indent before existing text
#define SIN_UNDO     4   //save line for undo before changing it

// flags for insertchar()
#define INSCHAR_FORMAT    1   //force formatting
#define INSCHAR_DO_COM    2   //format comments
#define INSCHAR_CTRLV     4   //char typed just after CTRL-V
#define INSCHAR_NO_FEX    8   //don't use 'formatexpr'
#define INSCHAR_COM_LIST 16   //format comments with list/2nd line indent

// flags for openLine()
#define OPENLINE_DELSPACES    0x01    // delete spaces after cursor
#define OPENLINE_DO_COM       0x02    // format comments
#define OPENLINE_KEEPTRAIL    0x04    // keep trailing spaces
#define OPENLINE_MARKFIX      0x08    // fix mark positions
#define OPENLINE_COM_LIST     0x10    // format comments with list/2nd line indent
#define OPENLINE_FORMAT       0x20    // formatting long comment
#define OPENLINE_FORCE_INDENT 0x40    // use second_line_indent without indent logic

// Values for do_tag().
#define DT_TAG     1   // jump to newer position or same tag again
#define DT_POP     2   // jump to older position
#define DT_NEXT    3   // jump to next match of same tag
#define DT_PREV    4   // jump to previous match of same tag
#define DT_FIRST   5   // jump to first match of same tag
#define DT_LAST    6   // jump to first match of same tag
#define DT_SELECT  7   // jump to selection from list
#define DT_HELP    8   // like DT_TAG, but no wildcards
#define DT_JUMP    9   // jump to new tag or selection from list
#define DT_CSCOPE 10   // cscope find command (like tjump)
#define DT_LTAG   11   // tag using location list
#define DT_FREE   99   // free cached matches

// flags for find_tags().
#define TAG_HELP         1   // only search for help tags
#define TAG_NAMES        2   // only return name of tag
#define TAG_REGEXP       4   // use tag pattern as regexp
#define TAG_NOIC         8   // don't always ignore case
#define TAG_CSCOPE      16   // cscope tag
#define TAG_VERBOSE     32   // message verbosity
#define TAG_INS_COMP    64   // Currently doing insert completion
#define TAG_KEEP_LANG  128   // keep current language
#define TAG_NO_TAGFUNC 256   // do not use 'tagfunc'

#define TAG_MANY       300   // When finding many tags (for completion), find up to this many tags

//Types of dialogs passed to do_dialog().
#define EE_GENERIC   0
#define EE_ERROR     1
#define EE_WARNING   2
#define EE_INFO      3
#define EE_QUESTION  4
#define EE_LAST_TYPE 4   // sentinel value

// Return values for functions like gui_yesnocancel()
#define EE_YES        2
#define EE_NO         3
#define EE_CANCEL     4
#define EE_ALL        5
#define EE_DISCARDALL 6

// arguments for win_split()
#define WSP_ROOM        0x01    //require enough room
#define WSP_VERT        0x02    //split/equalize vertically
#define WSP_HOR         0x04    //equalize horizontally
#define WSP_TOP         0x08    //portal at top-left of shell
#define WSP_BOT         0x10    //portal at bottom-right of shell
#define WSP_HELP        0x20    //creating the help portal
#define WSP_BELOW       0x40    //put new portal below/right
#define WSP_ABOVE       0x80    //put new portal above/left
#define WSP_NEWLOC     0x100    //don't copy location list
#define WSP_FORCE_ROOM 0x200 //ignore "not enough room" errors

// flags for check_changed()
#define CCGD_AW       1   // do autowrite if book was changed
#define CCGD_MULTWIN  2   // check also when several wins for the buf
#define CCGD_FORCEIT  4   // ! used
#define CCGD_ALLBOOKS 8   // may write all books
#define CCGD_EXCMD   16   // may suggest using !

// "flags" values for option-setting functions.
// When OPT_GLOBAL and OPT_LOCAL are both missing, set both local and global values, get local value
#define OPT_GLOBAL    0x02 //use global value
#define OPT_LOCAL     0x04 //use local value
#define OPT_ONECOLUMN 0x40 //list options one per line
#define OPT_NO_REDRAW 0x80 //ignore redraw flags on option

// Values for "starting"
#define NO_SCREEN   2   //no screen updating yet
#define NO_BOOKS    1   //not all books loaded yet
//         0      not starting anymore

// Values for swap_exists_action: what to do when swap file already exists
#define SEA_NONE     0   //don't use dialog
#define SEA_DIALOG   1   //use dialog when possible
#define SEA_QUIT     2   //quit editing the file
#define SEA_RECOVER  3   //recover the file
#define SEA_READONLY 4   //no dialog, mark book as read-only

// Special values for current_sctx.sc_sid.
#define SID_CMDARG    (-2)   // for "--cmd" argument
#define SID_CARG      (-3)   // for "-c" argument
#define SID_ENV       (-4)   // for sourcing environment variable
#define SID_ERROR     (-5)   // option was reset because of an error
#define SID_NONE      (-6)   // don't set scriptID
#define SID_WINLAYOUT (-7)   // changing window size

// Events for autocommands. Must be kept in sync with "autocmd.c:autoEvents".
enum AutoEvent {
   EVENT_BUFADD = 0,      // after adding a book to the book list
   EVENT_BUFCREATE,       // UNUSED: BufCreate == BufAdd
   EVENT_BUFDELETE,       // deleting a book from the book list
   EVENT_BUFENTER,        // after entering a book
   EVENT_BUFFILEPOST,     // after renaming a book
   EVENT_BUFFILEPRE,      // before renaming a book
   EVENT_BUFHIDDEN,       // just after book becomes hidden
   EVENT_BUFLEAVE,        // before leaving a book
   EVENT_BUFNEW,          // after creating any book
   EVENT_BUFNEWFILE,      // when creating a book for a new file
   EVENT_BUFREAD,         // UNUSED: BufRead == BufReadPost
   EVENT_BUFREADCMD,      // read book using command
   EVENT_BUFREADPOST,     // after reading a book
   EVENT_BUFREADPRE,      // before reading a book
   EVENT_BUFUNLOAD,       // just before unloading a book
   EVENT_BUFWINENTER,     // after showing a book in a portal
   EVENT_BUFWINLEAVE,     // just after book removed from portal
   EVENT_BUFWIPEOUT,      // just before really deleting a book
   EVENT_BUFWRITE,        // UNUSED: BufWrite == BufWritePost
   EVENT_BUFWRITECMD,     // write book using command
   EVENT_BUFWRITEPOST,    // after writing a book
   EVENT_BUFWRITEPRE,     // before writing a book
   EVENT_CMDUNDEFINED,    // command undefined
   EVENT_COMMPORTENTER,   // after entering the command portal
   EVENT_COMMPORTLEAVE,   // before leaving the command portal
   EVENT_COMPLETECHANGED, // after completion popup menu changed
   EVENT_COMPLETEDONE,    // after finishing insert complete
   EVENT_COMPLETEDONEPRE, // idem, before clearing info
   EVENT_CURSORHOLD,      // cursor in same position for a while
   EVENT_CURSORHOLDI,     // idem, in Insert mode
   EVENT_DIFFUPDATED,     // after diffs were updated
   EVENT_DIRCHANGED,      // after user changed directory
   EVENT_DIRCHANGEDPRE,   // before directory changes
   EVENT_EXITPRE,         // before exiting
   EVENT_FILEAPPENDCMD,   //append to a file using command
   EVENT_FILEAPPENDPOST,  //after appending to a file
   EVENT_FILEAPPENDPRE,   //before appending to a file
   EVENT_FILECHANGEDRO,   //before first change to read-only file
   EVENT_FILECHANGEDSHELL,//after shell command that changed file
   EVENT_FILECHANGEDSHELLPOST,   // after (not) reloading changed file
   EVENT_FILEREADCMD,     //read from a file using command
   EVENT_FILEREADPOST,    //after reading a file
   EVENT_FILEREADPRE,     //before reading a file
   EVENT_FILETYPE,        //new file type detected (user defined)
   EVENT_FILEWRITECMD,    //write to a file using command
   EVENT_FILEWRITEPOST,   //after writing a file
   EVENT_FILEWRITEPRE,    //before writing a file
   EVENT_FILTERREADPOST,  //after reading from a filter
   EVENT_FILTERREADPRE,   //before reading from a filter
   EVENT_FILTERWRITEPOST, //after writing to a filter
   EVENT_FILTERWRITEPRE,  //before writing to a filter
   EVENT_FOCUSGAINED,     //got the focus
   EVENT_FOCUSLOST,       //lost the focus to another app
   EVENT_FUNCUNDEFINED,   //if calling a function which doesn't exist
   EVENT_INSERTENTER,     //when entering Insert mode
   EVENT_INSERTLEAVE,     //just after leaving Insert mode
   EVENT_INSERTLEAVEPRE,  //just before leaving Insert mode
   EVENT_MENUPOPUP,       //just before popup menu is displayed
   EVENT_MODECHANGED,     //after changing the mode
   EVENT_OPTIONSET,       //option was set
   EVENT_QUICKFIXCMDPOST, //after :make, :grep etc.
   EVENT_QUICKFIXCMDPRE,  //before :make, :grep etc.
   EVENT_QUITPRE,         //before :quit
   EVENT_REMOTEREPLY,     //upon string reception from a remote Eegl
   EVENT_SAFESTATE,       //going to wait for a character
   EVENT_SAFESTATEAGAIN,  //still waiting for a character
   EVENT_SESSIONLOADPOST, //after loading a session file
   EVENT_SESSIONWRITEPOST,//after writing a session file
   EVENT_SHELLCMDPOST,    //after ":!cmd"
   EVENT_SHELLFILTERPOST, //after ":1,2!cmd", ":w !cmd", ":r !cmd".
   EVENT_SIGUSR1,         //after the SIGUSR1 signal
   EVENT_SOURCECMD,       //sourcing a Vim script using command
   EVENT_SOURCEPOST,      //after sourcing a Vim script
   EVENT_SOURCEPRE,       //before sourcing a Vim script
   EVENT_SPELLFILEMISSING,//spell file missing
   EVENT_SWAPEXISTS,      //found existing swap file
   EVENT_SYNTAX,          //syntax selected
   EVENT_TABCLOSED,       //after closing a tab
   EVENT_TABCLOSEDPRE,    //before closing a tab
   EVENT_TABENTER,        //after entering a tab
   EVENT_TABLEAVE,        //before leaving a tab
   EVENT_TABNEW,          //when entering a new tab
   EVENT_TERMCHANGED,     //after changing 'term'
   EVENT_TERMINALOPEN,    //after a terminal book was created
   EVENT_TERMINALWINOPEN, //after a terminal book was created and entering its window
   EVENT_TERMRESPONSE,    //after setting "v:termresponse"
   EVENT_TERMRESPONSEALL, //after setting terminal response vars
   EVENT_TEXTYANKPOST,    //after some text was yanked
   EVENT_USER,            //user defined autocommand
   EVENT_EEGLENTER,        //after starting Eegl
   EVENT_EEGLLEAVE,        //before exiting Eegl
   EVENT_EEGLLEAVEPRE,     //before exiting Eegl and writing .eeglinfo
   EVENT_EEGLRESIZED,      //after Eegl window was resized
   EVENT_WINCLOSED,       //after closing a portal
   EVENT_PORTENTER,       //after entering a portal
   EVENT_PORTLEAVE,       //before leaving a portal
   EVENT_PORTNEW,         //after creating a new portal
   EVENT_PORTNEWPRE,      //before creating a new portal
   EVENT_WINRESIZED,      //after a portal was resized
   EVENT_PORTSCROLLED,    //after a portal was scrolled or resized

   NUM_EVENTS,            // MUST be the last one
};

typedef enum AutoEvent AutoEvent;

#define AUGROUP_DEFAULT  (4000000000) // default autocomm group
#define AUGROUP_ERROR    (4000000001) // erroneous autocomm group
#define AUGROUP_ALL      (4000000002) // all autocomm groups

//Values for index in highlight_attr[]. When making changes, also update HL_FLAGS below!
//And update the default value of 'highlight': HIGHLIGHT_INIT in option.c


#define HLF_NONE     0 //No decorations
#define HLF_NONTEXT  1 //Non-text
#define HLF_FLOAT    2 //Normal float
#define HLF_AT       3 // characters at end of screen, characters that don't really exist in the text
#define HLF_D        4 //directories in CTRL-D listing
#define HLF_E        5 //error messages
#define HLF_W        6 //warning messages
#define HLF_M        7 //"--More--" message
#define HLF_CM       8 //Mode (e.g., "-- INSERT --")
#define HLF_CLN      9 //current line number
#define HLF_CLS     10 //current line sign column
#define HLF_CLF     11 //current line fold
#define HLF_R       12 //return to continue message and yes/no questions
#define HLF_S       13 //status lines
#define HLF_SNC     14 //status lines of not-current portals
#define HLF_C       15 //column to separate vertically split windows
#define HLF_T       16 //Titles for output from ":set all", ":autocmd" etc.
#define HLF_V       17 //Visual mode
#define HLF_VNC     18 //Visual mode, autoselecting and not clipboard owner
#define HLF_WM      19 //Wildmenu highlight
#define HLF_FL      20 //Folded line
#define HLF_ADD     21 //Added diff line
#define HLF_CHD     22 //Changed diff line
#define HLF_TXD     23 //Text Changed in changed diff line
#define HLF_TXA     24 //Text Added in changed diff line
#define HLF_DED     25 //Deleted diff line
#define HLF_SC      26 //Sign column
#define HLF_PNI     27 //popup menu normal item
#define HLF_PSI     28 //popup menu selected item
#define HLF_PMNI    29 //popup menu matched text in normal item
#define HLF_PMSI    30 //popup menu matched text in selected item
#define HLF_PNK     31 //popup menu normal item "kind"
#define HLF_PSK     32 //popup menu selected item "kind"
#define HLF_PNX     33 //popup menu normal item "menu" (extra text)
#define HLF_PSX     34 //popup menu selected item "menu" (extra text)
#define HLF_PSB     35 //popup menu scrollbar
#define HLF_PST     36 //popup menu scrollbar thumb
#define HLF_TPL     37 //tabpanel
#define HLF_TPLS    38 //tabpanel selected
#define HLF_TPLF    39 //tabpanel filler
#define HLF_MC      41 //'colorcolumn'
#define HLF_QFL     42 //location portal line currently selected
#define HLF_ST      43 //status lines of terminal windows
#define HLF_STNC    44 //status lines of not-current terminal portals
#define HLF_TERMR   45 //status lines of not-current terminal portals
#define HLF_TERMG   46 //status lines of not-current terminal portals
#define HLF_TERMB   47 //status lines of not-current terminal portals
#define HLF_MSG     48 //message area
#define HLF_8       49 //Meta & special keys listed with ":map", text that is displayed different
#define HLF_N       50 //line number for ":number" and ":#" commands
#define HLF_LNA     51 //LineNrAbove
#define HLF_LNB     52 //LineNrBelow

typedef enum {
   EXTRA_DECO_NONE,
   EXTRA_DECO_INVERT,
   EXTRA_DECO_ALTERED_BG,
   EXTRA_DECO_UNDER,
   EXTRA_DECO_UNDERCURL,
   EXTRA_DECO_UNDERDASH
} ExtraDeco;

// Boolean constants
#ifndef TRUE
# define FALSE  0   // note: this is an int, not a long!
# define TRUE   1
#endif
#define MAYBE   2   // sometimes used for a variant on TRUE

//If "--log logfile" was used or ch_logfile() was called then log some or all
//terminal output.
# define MAY_WANT_TO_LOG_THIS if (ch_log_output == FALSE) ch_log_output = TRUE;

// Operator IDs; The order must correspond to opchars[] in ops.c!
#define OP_NOP           0   //no pending operation
#define OP_DELETE        1   //"d"  delete operator
#define OP_YANK          2   //"y"  yank operator
#define OP_CHANGE        3   //"c"  change operator
#define OP_LSHIFT        4   //"<"  left shift operator
#define OP_RSHIFT        5   //">"  right shift operator
#define OP_FILTER        6   //"!"  filter operator
#define OP_TILDE         7   //"g~" switch case operator
#define OP_INDENT        8   //"="  indent operator
#define OP_FORMAT        9   //"gq" format operator
#define OP_COLON        10   //":"  colon operator
#define OP_UPPER        11   //"gU" make upper case operator
#define OP_LOWER        12   //"gu" make lower case operator
#define OP_JOIN         13   //"J"  join operator, only for Visual mode
#define OP_JOIN_NS      14   //"gJ"  join operator, only for Visual mode
#define OP_ROT13        15   //"g?" rot-13 encoding
#define OP_REPLACE      16   //"r"  replace chars, only for Visual mode
#define OP_INSERT       17   //"I"  Insert column, only for Visual mode
#define OP_APPEND       18   //"A"  Append column, only for Visual mode
#define OP_FOLD         19   //"zf" define a fold
#define OP_FOLDOPEN     20  //"zo" open folds
#define OP_FOLDOPENREC  21  //"zO" open folds recursively
#define OP_FOLDCLOSE    22  //"zc" close folds
#define OP_FOLDCLOSEREC 23 //"zC" close folds recursively
#define OP_FOLDDEL      24 //"zd" delete folds
#define OP_FOLDDELREC   25 //"zD" delete folds recursively
#define OP_FORMAT2      26 //"gw" format operator, keeps cursor pos
#define OP_FUNCTION     27 //"g@" call 'operatorfunc'
#define OP_ADD          28 //"<C-A>" Add to the number or alphabetic character
#define OP_SUB          29 //"<C-X>" Subtract from the number or alphabetic character

// Motion types, used for operators and for yank/delete registers.
#define MCHAR   0      //character-wise movement/register
#define MLINE   1      //line-wise movement/register
#define MBLOCK  2      //block-wise register

#define MAUTO 0xff     //Decide between MLINE/MCHAR

// Minimum screen size
#define MIN_COLUMNS    12  //minimal columns for screen
#define MIN_LINES       2  //minimal lines for screen
#define MIN_COMMHEIGHT  1  //minimal height for command line
#define STATUS_HEIGHT   1  //height of a status line under a window
#define VISIBLE_HEIGHT(wp)   ((wp)->height)
#define QF_WINHEIGHT   10  //default height for quickfix window

// Buffer sizes
#ifndef CMDBUFFSIZE
# define CMDBUFFSIZE   256   // size of the command processing buffer
#endif

#define LSIZE       512      // max. size of a line in the tags file

#define IOSIZE      (1024+1)   // file i/o and sprintf buffer size

#define DIALOG_MSG_SIZE 1000   // buffer size for dialog_msg()

#define MSG_BUF_LEN 480   //length of buffer for small messages
#define MSG_BUF_CLEN (MSG_BUF_LEN / 6) //cell length (worst case: utf-8 takes 6 bytes for one cell)

//Maximum length of key sequence to be mapped.
#define MAXMAPLEN   50

// maximum length of a function name, including SID and NUL
#define MAX_FUNC_NAME_LEN   200

// Size in bytes of the hash used in the undo file.
#define UNDO_HASH_SIZE 32

#ifdef BINARY_FILE_IO
# define WRITEBIN   "wb"   // no CR-LF translation
# define READBIN    "rb"
# define APPENDBIN  "ab"
#else
# define WRITEBIN   "w"
# define READBIN    "r"
# define APPENDBIN  "a"
#endif

#define O_EXTRA    0

#ifndef O_NOFOLLOW
# define O_NOFOLLOW 0
#endif

#ifndef W_OK
# define W_OK 2      // for systems that don't have W_OK in unistd.h
#endif
#ifndef R_OK
# define R_OK 4      // for systems that don't have R_OK in unistd.h
#endif

// Allocate memory for one type and cast the returned pointer to have the
// compiler check the types.
#define ALLOC_ONE(type)  (type *)alloc(sizeof(type))
#define ALLOC_ONE_ID(type, id)  (type *)alloc_id(sizeof(type), id)
#define ALLOC_MULT(type, count)  (type *)alloc(sizeof(type) * (count))
#define ALLOC_CLEAR_ONE(type)  (type *)allocZeroed(sizeof(type))
#define ALLOC_CLEAR_ONE_ID(type, id)  (type *)allocZeroed_id(sizeof(type), id)
#define ALLOC_CLEAR_MULT(type, count)  (type *)allocZeroed(sizeof(type) * (count))
#define LALLOC_CLEAR_ONE(type)  (type *)lallocZeroed(sizeof(type), FALSE)
#define LALLOC_CLEAR_MULT(type, count)  (type *)lallocZeroed(sizeof(type) * (count), FALSE)
#define LALLOC_MULT(type, count)  (type *)lalloc(sizeof(type) * (count), FALSE)

#define CLEAR_FIELD(field)  memset(&(field), 0, sizeof(field))
#define CLEAR_POINTER(ptr)  memset((ptr), 0, sizeof(*(ptr)))

# define caseInsensitiveCompareMaxCol(d, s)    \
   caseInsensitiveCompareNChars((Byte *)(d), (Byte *)(s), (int)MAXCOL)


#define eeStrpbrk(s, cs) (CS)strpbrk((char *)(s), (char *)(cs))

#define OUT_STR(s)          out_str((Byte *)(s))
#define OUT_STR_NF(s)       out_str_nf((Byte *)(s))

#define GUI_FUNCTION(f)       termgui_##f
# define GUI_MCH_GET_RGB      GUI_FUNCTION(mch_get_rgb)
# define GUI_MCH_GET_COLOR    GUI_FUNCTION(mch_get_color)

//Prefer using emsgf(), because perror() may send the output to the wrong
//destination and mess up the screen.
#define PERROR(msg)          (void)showErrFmtMsg("%s: %s", (char *)(msg), strerror(errno))

typedef unsigned short DisplayTick;   // display tick type

// MAXCOL used to be INT_MAX, but with 64 bit ints that results in running
// out of memory when trying to allocate a very long line.
#define MAXCOL  0x7fffffffL    // maximum column number

#define SHOWCMD_COLS 10        // columns needed by shown command

#ifndef mch_memmove
# define mch_memmove(to, from, len) memmove((char*)(to), (char*)(from), (Unt)(len))
#endif

// fnamecmp() is used to compare file names.
// (this does not account for maximum name lengths and things like "../dir",
// thus it is not 100% accurate!)
#define fnamecmp(x, y) STRCMP((Byte *)(x), (Byte *)(y))

#define USE_INPUT_BUF

#define eeReadFromFile(fd, buf, count)   read((fd), (char *)(buf), (Unt) (count))
#define eeWriteToFile(fd, buf, count)  write((fd), (char *)(buf), (Unt) (count))

//EXTERN is only defined in main.c. That's where global variables are actually defined
#ifdef EXTERN // main.c
# ifndef INIT
#define INIT(x) x
#define INIT2(a, b) = {a, b}
#define INIT3(a, b, c) = {a, b, c}
#define INIT4(a, b, c, d) = {a, b, c, d}
#define INIT5(a, b, c, d, e) = {a, b, c, d, e}
#define INIT6(a, b, c, d, e, f) = {a, b, c, d, e, f}
#define MAIN_C
# endif
#else // all other files
#define EXTERN extern
#define INIT(x)
#define INIT2(a, b)
#define INIT3(a, b, c)
#define INIT4(a, b, c, d)
#define INIT5(a, b, c, d, e)
#define INIT6(a, b, c, d, e, f)
#endif

//Maximum number of bytes in a multi-byte character. It can be one 32-bit
//character of up to 6 bytes, or one 16-bit character of up to three bytes
//plus six following composing characters of three bytes each.
#define MB_MAXBYTES   21

#if !defined(PROTO)
// Use tv_fsec for fraction of second (micro or nano) of ProfTime
#define PROF_NSEC 1
typedef struct timespec ProfTime;
#define PROF_GET_TIME(tm) clock_gettime(CLOCK_MONOTONIC, tm)
#define tv_fsec tv_nsec
#define TV_FSEC_SEC 1000000000L
#define PROF_TIME_FORMAT "%3ld.%09ld"
#define PROF_TIME_BLANK "              "
#define PROF_TOTALS_HEADER "count     total (s)      self (s)"
#else
typedef int ProfTime;       // dummy for function prototypes
#endif

typedef time_t Tyme;
typedef int Socket;

//The :options must come before :structs, because the number of portal-local and
//book-local options is used there.

//{{{:::options and default values


// Formatting options for p_fo @formatoptions
#define FO_WRAP         't'
#define FO_WRAP_COMS    'c'
#define FO_RET_COMS     'r'
#define FO_OPEN_COMS    'o'
#define FO_NO_OPEN_COMS '/'
#define FO_Q_COMS       'q'
#define FO_Q_NUMBER     'n'
#define FO_Q_SECOND     '2'
#define FO_INS_VI       'v'
#define FO_INS_LONG     'l'
#define FO_INS_BLANK    'b'
#define FO_MBYTE_BREAK  'm'   // break before/after multi-byte char
#define FO_MBYTE_JOIN   'M'   // no space before/after multi-byte char
#define FO_MBYTE_JOIN2  'B'   // no space between multi-byte chars
#define FO_ONE_LETTER   '1'
#define FO_WHITE_PAR    'w'   // trailing white space continues paragr.
#define FO_AUTO         'a'   // automatic formatting
#define FO_RIGOROUS_TW  ']'     // respect textwidth rigorously
#define FO_REMOVE_COMS  'j'   // remove comment leaders when joining lines
#define FO_PERIOD_ABBR  'p'   // don't break a single space after a period

// flags for @comments option
#define COM_NEST     'n'   // comments strings nest
#define COM_BLANK    'b'   // needs blank after string
#define COM_START    's'   // start of comment
#define COM_MIDDLE   'm'   // middle of comment
#define COM_END      'e'   // end of comment
#define COM_AUTO_END 'x'   // last char of end closes comment
#define COM_FIRST    'f'      // first line comment only
#define COM_LEFT     'l'       // left adjusted
#define COM_RIGHT    'r'      // right adjusted
#define COM_NOBACK   'O'     // don't use for "O" command
#define COM_ALL      "nbsmexflrO"   // all flags for 'comments' option
#define COM_MAX_LEN  50      // maximum length of a part

// flags for @statusline option
#define STL_FILEPATH     'f'      // path of file in book
#define STL_FULLPATH     'F'      // full path of file in book
#define STL_FILENAME     't'      // last part (tail) of file path
#define STL_COLUMN       'c'      // column og cursor
#define STL_VIRTCOL      'v'      // virtual column
#define STL_VIRTCOL_ALT  'V'      // - with 'if different' display
#define STL_LINE         'l'      // line number of cursor
#define STL_NUMLINES     'L'      // number of lines in book
#define STL_BUFNO        'n'      // current book number
#define STL_KEYMAP       'k'      // 'keymap' when active
#define STL_OFFSET       'o'      // offset of character under cursor
#define STL_OFFSET_X     'O'      // - in hexadecimal
#define STL_BYTEVAL      'b'      // byte value of character
#define STL_BYTEVAL_X    'B'      // - in hexadecimal
#define STL_ROFLAG       'r'      // readonly flag
#define STL_ROFLAG_ALT   'R'      // - other display
#define STL_HELPFLAG     'h'      // window is showing a help file
#define STL_HELPFLAG_ALT 'H'      // - other display
#define STL_FILETYPE     'y'      // 'filetype'
#define STL_FILETYPE_ALT 'Y'      // - other display
#define STL_PREVIEWFLAG  'w'      // window is showing the preview buf
#define STL_PREVIEWFLAG_ALT 'W'      // - other display
#define STL_MODIFIED     'm'      // modified flag
#define STL_MODIFIED_ALT 'M'      // - other display
#define STL_QUICKFIX     'q'      // quickfix window description
#define STL_PERCENTAGE   'p'      // percentage through file
#define STL_ALTPERCENT   'P'      // percentage as TOP BOT ALL or NN%
#define STL_ARGLISTSTAT  'a'      // argument list status as (x of y)
#define STL_PAGENUM      'N'      // page number (when printing)
#define STL_SHOWCMD      'S'      // @showcmd book
#define STL_EE_EXPR      '{'      // start of expression to substitute
#define STL_SEPARATE     '='      // separation between alignment sections
#define STL_TRUNCMARK    '<'      // truncation mark if line is too long
#define STL_USER_HL      '*'      // highlight from (User)1..9 or 0
#define STL_HIGHLIGHT    '#'      // highlight name
#define STL_TABPAGENR    'T'      // tab label nr
#define STL_TABCLOSENR   'X'      // tab close nr
#define STL_ALL          (S"fFtcvVlLknoObBrRhHmYyWwMqpPaNS{#")

//Kinds of status lines
#define STATLINE_TABPANEL    1
#define STATLINE_STATUSLINE  2
#define STATLINE_RULERFORMAT 3

// flags used for parsed @wildmode
#define WIM_FULL        0x01
#define WIM_LONGEST     0x02
#define WIM_LIST        0x04
#define WIM_BUFLASTUSED 0x08
#define WIM_NOSELECT    0x10


// The following are actual variables for the options
EXTERN Boole p_asd;     // @autoshelldir
EXTERN CS p_tsrfu; // @thesaurusfunc
EXTERN Boole p_aw;      // @autowrite
EXTERN Boole p_awa;     // @autowriteall
EXTERN Boole p_bk;      // @backup
#define BKC_YES           0x001
#define BKC_AUTO          0x002
#define BKC_NO            0x004
#define BKC_BREAKSYMLINK  0x008
#define BKC_BREAKHARDLINK 0x010
EXTERN CS p_bdir;       //@backupdir
EXTERN CS p_bex;        //@backupext
EXTERN unsigned bo_flags;
EXTERN CS p_bsk;        //@backupskip
EXTERN long p_bdlay;    //@balloondelay
EXTERN Boole p_bevalterm; //@balloonevalterm
EXTERN CS p_bt;         //@booktype
EXTERN Boole p_delcomb; //@delcombine
EXTERN long p_cwh;      //@cmdwinheight
EXTERN long commlineHeightG;//@commheight
EXTERN CS p_cpt;        //@complete
EXTERN Boole p_confirm; //@confirm
EXTERN CS p_cfc;        //@completefuzzycollect
EXTERN unsigned cfc_flags; //flags from @completefuzzycollect
EXTERN Unt p_cia;       //@completeitemalign
EXTERN Unt cot_flags;   //flags from @completeopt
EXTERN Boole p_ac;      //@autocomplete
EXTERN long p_acl;      //@autocompletedelay

// Keep in sync with option.c:p_cot_values
#define COT_MENU        0x001
#define COT_MENUONE     0x002
#define COT_ANY_MENU    0x003   // combination of menu flags
#define COT_LONGEST     0x004   // FALSE: insert full match, TRUE: insert longest prefix
#define COT_POPUP       0x010
#define COT_POPUPHIDDEN 0x020
#define COT_ANY_PREVIEW 0x038   // combination of preview flags
#define COT_NOINSERT    0x040   // FALSE: select & insert, TRUE: noinsert
#define COT_NOSELECT    0x080   // FALSE: select & insert, TRUE: noselect
#define COT_NOSORT      0x200   // TRUE: fuzzy match without qsort score
#define COT_PREINSERT   0x400   // TRUE: preinsert
#define COT_NEAREST     0x800   // TRUE: prioritize matches close to cursor
#define COT_PREVIEW     0x008
#define COT_FUZZY       0x100   // TRUE: fuzzy match enabled

EXTERN long   p_ph;      //@pumheight
EXTERN long   p_pw;      //@pumwidth
EXTERN long   p_pmw;     //@pummaxwidth
EXTERN CS p_csprg;  //@cscopeprg
EXTERN Boole p_csre;     //@cscoperelative
EXTERN Boole p_cst;      //@cscopetag
EXTERN long   p_csto;    //@cscopetagorder
EXTERN long   p_cspc;    //@cscopepathcomp
EXTERN Boole p_csverbose;//@cscopeverbose
EXTERN CS p_debug;  //@debug
EXTERN CS p_dip;    //@diffopt
EXTERN CS p_dex;    //@diffexpr
EXTERN Byte p_ead;    //@eadirection
#define EAD_VERTICAL   1 // must be >0
#define EAD_HORIZONTAL 2
#define EAD_BOTH       3

EXTERN Boole p_ea;       //@equalalways
EXTERN CS globOpt;  //@errorfile
EXTERN int   p_eof;      //@endoffile
EXTERN CS p_ei;     //@eventignore
EXTERN long foldLevelStart; // @foldlevelstart
EXTERN Unt p_fdo;        //@foldopen
//keep in sync with option.c:p_fdo_values
#define FDO_ALL      0x001
#define FDO_BLOCK    0x002
#define FDO_HOR      0x004
#define FDO_MARK     0x008
#define FDO_PERCENT  0x010
#define FDO_LOCATION 0x020
#define FDO_SEARCH   0x040
#define FDO_TAG      0x080
#define FDO_INSERT   0x100
#define FDO_UNDO     0x200
#define FDO_JUMP     0x400
EXTERN CS p_fp;     // @formatprg
EXTERN Boole  p_fs;      // @fsync
EXTERN CS p_cpp;    // @completepopup
EXTERN Byte cursorNormalG;   //@cursorNormal
EXTERN Byte cursorInsertG;   //@cursorInsert
EXTERN long   p_hh;      // @helpheight
EXTERN CS p_hlg;    // @helplang
EXTERN Boole   p_hls;     // @hlsearch
EXTERN long   p_hi;      // @history
EXTERN Boole   p_ic;      // @ignorecase
EXTERN CS p_imaf;   // @imactivatefunc
EXTERN CS p_imsf;   // @imstatusfunc
EXTERN int   p_imcmdline;// @imcmdline
EXTERN int   p_imdisable;// @imdisable
EXTERN Boole   p_is;       // @incsearch
EXTERN CS p_isf;    //@isfname
EXTERN CS p_isi;    //@isident
EXTERN CS p_ise;    //@isexpand
EXTERN CS p_isk;    //@iskeyword
EXTERN CS p_kp;     //@keywordprg
EXTERN CS p_kpc;    //@keyprotocol
EXTERN CS p_langmap;//@langmap

// Characters from the @listchars option
typedef struct {
   Unt eol;
   Unt ext;
   Unt prec;
   Unt nbsp;
   Unt space;
   Unt tab1;
   Unt tab2;
   Unt tab3;
   Unt trail;
   Unt lead;
   Unt* multispace;
   Unt* leadmultispace;
   //Unt conceal;
} ListChars;

// Characters from the @fillchars option
typedef struct {
   Unt   stl;
   Unt   stlnc;
   Unt   vert;
   Unt   fold;
   Unt   foldopen;
   Unt   foldclosed;
   Unt   foldsep;
   Unt   diff;
   Unt   eob;
   Unt   lastline;
   Unt   tpl_vert;
   Unt trunc;
   Unt truncrl;
} FillChars;

EXTERN CS p_lcs;    // @listchars
EXTERN ListChars listCharsG;     // @listchars characters
EXTERN CS p_fcs;    //@fillchars
EXTERN FillChars fillCharsG;     // @fillchars characters
EXTERN Boole p_intro;  //@intro
EXTERN Boole p_lrm;     // @langremap
EXTERN Boole p_lz;       // @lazyredraw
EXTERN Boole p_more;     // @more
EXTERN Boole makeOpenWhenDoneG; // @makeOpenWhenDone
EXTERN CS p_mef;    // @makeef
EXTERN long p_mfd;     // @maxfuncdepth
EXTERN long p_mm;      // @maxmem
EXTERN long p_mmp;     // @maxmempattern
EXTERN CS p_mopt; // @messagesopt
EXTERN long p_msc;     // @maxsearchcount
EXTERN CS p_msm;  // @mkspellmem
EXTERN int p_modifiable; // @modifiable
EXTERN long p_mouset;  // @mousetime
EXTERN CS p_nf;   // @nrformats
EXTERN CS p_opfunc; // @operatorfunc
EXTERN CS p_pex;    // @patchexpr
EXTERN CS p_cdpath; // @cdpath
EXTERN long p_rdt;     // @redrawtime
EXTERN long p_pvh;     // @previewheight
EXTERN CS p_ruf;  // @rulerformat
EXTERN CS p_qftf; // @quickfixtextfunc
#define runtimePath (CS)"~/.config/eegl/runtime/"
EXTERN long p_sj;      // @scrolljump
#define SCR_VER  1 // keep in sync with scrolloptValues
#define SCR_HOR  2
#define SCR_JUMP 3
EXTERN Unt p_sbo;  // @scrollopt

EXTERN CS p_ef;   // @errorfile
EXTERN CS p_shcf; //@shellcmdflag
EXTERN CS p_sp;   //@shellpipe
EXTERN CS p_srr;  //@shellredir
EXTERN Boole p_stmp;   //@shelltemp
EXTERN CS p_sbr;  //@showbreak
EXTERN Byte p_sloc; //@showcmdloc
#define SHOW_COMM_LAST       1 //last screnline
#define SHOW_COMM_STATUSLINE 2 //portal statusline

EXTERN Boole p_sft;    //@showfulltag
EXTERN Boole p_smd;    //@showmode
EXTERN long  p_ss;     //@sidescroll
EXTERN Boole p_scs;    //@smartcase
EXTERN Boole p_swf;    //@swapfile
EXTERN CS p_sps;       //@spellsuggest
EXTERN Boole p_spr;    //@splitright
EXTERN Boole p_sb;     //@splitbelow
EXTERN Boole p_sol;    //@startofline
EXTERN CS p_lpSuff;    //@lowPrioSuffixes
EXTERN Boole p_sws;    //@swapsync
EXTERN Unt p_swb;      //@switchbuf
// Keep in sync with option.c:p_swb_values
#define SWB_USEOPEN    0x001
#define SWB_USETAB     0x002
#define SWB_SPLIT      0x004
#define SWB_NEWTAB     0x008
#define SWB_VSPLIT     0x010
#define SWB_USELAST    0x020

EXTERN CS p_tpl;  //@tabpanel
EXTERN Boole p_stpl;    //@showtabpanel
EXTERN CS p_tplo; //@tabpanelopt

EXTERN Byte p_tcl;  //@tabclose
EXTERN Boole   p_tbs;    //@tagbsearch
#define TC_FOLLOWIC    0x01
#define TC_IGNORE      0x02
#define TC_MATCH       0x04
#define TC_FOLLOWSCS   0x08
#define TC_SMART       0x10
EXTERN Boole p_tgst;     //@tagstack
EXTERN CS p_tenc;   //@termencoding
EXTERN long p_twsl;    //@termwinscroll
EXTERN int p_tx;       //@textmode
EXTERN long p_tw;      //@textwidth
EXTERN Boole p_timeout;  //@timeout
EXTERN long p_tm;      //@timeoutlen
EXTERN Boole p_ttimeout; //@ttimeout
EXTERN long   p_ttm;     //@ttimeoutlen
EXTERN long   p_ttyscroll; //@ttyscroll
EXTERN unsigned ttym_flags;
EXTERN long   p_ul;      //@undolevels
EXTERN long   p_ur;      //@undoreload
EXTERN long   p_ut;      //@updatetime
EXTERN CS p_eeglinfo; //@eeglinfo
EXTERN CS p_eeglinfofile; //@eeglinfofile
EXTERN long p_verbose; //@verbose
EXTERN CS p_vfile;   // @verbosefile
EXTERN Unt p_wop;   //@wildoptions

//Sync with option.c:p_wop_values
#define WILDOPT_EXACT   1
#define WILDOPT_FUZZY   2
#define WILDOPT_PUM     4
#define WILDOPT_TAGFILE 8

EXTERN CS p_wig;   //@wildignore
EXTERN CS p_ww;    //@whichwrap
EXTERN long p_wc;    //@wildchar
EXTERN long p_wcm;   //@wildcharm
EXTERN Boole p_wic;  //@wildignorecase
EXTERN CS p_wim;     //@wildmode
EXTERN Boole p_wmnu; //@wildmenu
EXTERN long p_wh;    //@winheight
EXTERN long p_wiw;   //@winwidth
EXTERN CS p_wse;     //@wlseat
EXTERN Boole p_wst;  //@wlsteal
EXTERN long p_wtm;   //@wltimeoutlen
EXTERN int p_wa;     //@writeany
EXTERN long p_wd;    //@writedelay


// Value for b_p_ul indicating the global value must be used.
#define NO_LOCAL_UNDOLEVEL (-123456)

#define ERR_BUFLEN 80

//}}}

#define DECLARE_COMMANDS_ENUM
#include "commands.h"       // Command declarations
#undef DECLARE_COMMANDS_ENUM

//{{{:::structs
//{{{ basics

typedef off_T FileSize;

//{{{ Slice

#define DEFINE_SLICE_HEADER(T) \
   typedef struct {\
      T* c;\
      Int len;\
   } Sli##T;\

#define sliceOf(list) ({.c = list->c, .len = list->len})

#define sliceOfInternal(list) {.c = list.c, .len = list.len}

DEFINE_SLICE_HEADER(Int)
DEFINE_SLICE_HEADER(Unt)
DEFINE_SLICE_HEADER(Ulong)

//}}}

EXTERN Sbuf globalStringOptionsG;      //Storage for all the global string options


// Position in file or book.
typedef struct {
   LineNr   lnum;  // line number
   ColNr   col;    // column number
   ColNr   coladd; // extra virtual column
} Pos;

// Same, but without coladd.
typedef struct {
   LineNr   lnum;   // line number
   ColNr   col;   // column number
} PosNoVirt;
#define GA_EMPTY    {0, 0, 0, 0, NULL}

// On rare systems "char" is unsigned, sometimes we really want a signed 8-bit value.
typedef signed char   SignedByte;

//}}}
//{{{ forward decls
typedef struct Var Var;
typedef struct List List;
declStruct(Bag);
declStruct(PartiallyApplied);
typedef struct Blob Blob;
typedef sighandler_T SigHandler;
declStruct(Portal); 
declStruct(Job); 

declStruct(PortInfo);
declStruct(Frame);
typedef int         ScriptId;      // script ID
declStruct(Book);
declStruct(Terminal); // defined in ui.c

typedef struct FuncDict FuncDict; // used in script.c
declStruct(Callback);

//}}}
//{{{data structures


#define get(needle, d) _Generic((needle),\
   CS: _Generic((d),\
               DictStringInt128*: get_DictStringInt128\
            ),\
   Text:  _Generic((d),\
               DictStringInt128*: get_Text_DictStringInt128\
            )\
)((needle), (d))

#define getOrDefault(needle, defa, d) _Generic((needle),\
   CS: _Generic((d),\
               DictStringInt128*: getOrDefault_DictStringInt128\
            ),\
   Text:  _Generic((d),\
               DictStringInt128*: getOrDefault_Text_DictStringInt128\
            )\
)((needle), (defa), (d))


#define getKv(outKey, needle, d) _Generic((needle),\
   Text:  _Generic((d),\
               DictStringInt128*: getKv_Text_DictStringInt128\
            )\
)((outKey), (needle), (d))

typedef struct {
   CS c;
   int len;
} ErrBuilder;


//}}}
//{{{options

typedef enum {
   OPTION_STRING,
   OPTION_NUM,
   OPTION_BOOLE,
   OPTION_ENUM,  // represented as a Byte
   OPTION_FLAGS, // represented as an Unt
   OPTION_CALLBACK     // pointer to callback function
} OptionValueTag;

typedef struct {
   OptionValueTag tag;
   union {
      CS string;
      long num;
      Boole boole;
      Unt flags;
      Byte enume;
      Callback* callback;
   };
} OptionValue;

#define getDefault(optField, optDef) _Generic((optField),\
   CS: optDef->defaultValue.string,\
   long: optDef->defaultValue.num,\
   Boole: optDef->defaultValue.boole,\
   Unt: optDef->defaultValue.flags,\
   Byte: optDef->defaultValue.enum\
)

#define optStr(x) (OptionValue){.tag = OPTION_STRING, .string = (CS)(x)}
#define optNum(x) (OptionValue){.tag = OPTION_NUM, .num = (x)}
#define optEnum(x) (OptionValue){.tag = OPTION_ENUM, .num = (long)(x)}
#define optBoole(x) (OptionValue){.tag = OPTION_BOOLE, .boole = (x)}
#define optFlag(x) (OptionValue){.tag = OPTION_FLAGS, .flags = (x)}
#define optCallback(x) (OptionValue){.tag = OPTION_CALLBACK, .callback = (x)}
#define optGlobalOfBook(fieldName) optGetBufOpt(offsetof(BookLocal, fieldName))

// For modifying
typedef struct {
   OptionValueTag tag;
   union {
      CS* string;
      long* num;
      Boole* boole;
      Byte* enume;
      Unt* flags;
      Callback** callback;
   };
} OptionRef;


//}}}
//{{{scripts

//SCript position (SPOS): identifies a script line.
//When sourcing a script "sc_lnum" is zero, "sourcing_lnum" is the current
//line number. When executing a user function "sc_lnum" is the line where the
//function was defined, "sourcing_lnum" is the line number inside the
//function.  When stored with a function, mapping, option, etc. "sc_lnum" is
//the line number in the script "sc_sid".
typedef struct {
   ScriptId sid;      // script ID
   int      seq;      // sourcing sequence number
   LineNr   lineNr;   // line number in script
} ScriptPos;

//}}}
//{{{portals

//Reference to a book that stores the value of buf_free_count.
//bookRefValid() only needs to check "buf" when the count differs.
typedef struct {
   Book* c;
   int fnum;
   int freeCount;
} BookRef;

typedef Byte VTermColor;

typedef struct {
   VTermColor fg;
   VTermColor bg;
} TermCellColor;

typedef Byte VTermDeco;

// This is ScreenCell without the characters, thus much smaller.
typedef struct {
   VTermDeco flags;
   VTermColor fg;
   VTermColor bg;
} CellDeco;

typedef struct {
   Unt chars[MAX_COMBINED_SYMBOLS];
   CellDeco deco;
} ScreenCell;

#define HI_HAS_FG    1
#define HI_HAS_BG    2
#define HI_HAS_UNDER 4
#define HI_IS_LINK   8

typedef struct {
   VTermColor fg; // foreground, always from original hiId group
   VTermColor bg; // background, always from original hiId group
   VTermColor under; // underline color, possibly from merging hilites
   VTermDeco flags; // HL_BOLD, underline etc. Result from possibly merging hilites
   Byte fieldPresence; //HI_* constants
   Short hiId; // original hilite group id
} Decoration;

// marks: positions in a file (a normal mark is a lnum/col pair, the same as a file position)
#define NMARKS      ('z' - 'a' + 1)   // max. # of named marks
#define EXTRA_MARKS   10      // marks 0-9
#define JUMPLISTSIZE   100      // max. # of marks in jump list
#define TAGSTACKSIZE   20      // max. # of tags in tag stack

// PortLocal.foldMethod values
#define FOLD_INDENT 0
#define FOLD_MARKER 1
#define FOLD_EXPR 2
#define FOLD_DIFF 3

//Info used to pass info about a fold from the fold-detection code to the
//code that displays the folds.
typedef struct foldinfo {
   int fi_level;     // level of the fold; when this is zero the other fields are invalid
   int fi_lnum;      // line number where fold starts
   int fi_low_level; // lowest fold level that starts in the same line
} FoldInfo;

typedef struct filemark {
   Pos   mark;      // cursor position
   int      fnum;      // file number
} FileMark;

// Xtended file mark: also has a file name
typedef struct xfilemark {
   FileMark   fmark;
   Byte   *fname;      // file name, used when fnum == 0
   Tyme   time_set;
} FileMarkExt;

// The taggy struct is used to store the information about a :tag command.
typedef struct taggy {
   CS tagname;   // tag name
   FileMark fmark;    // cursor position BEFORE ":tag"
   Unt cur_match;  // match number
   int cur_fnum;   // book number used for cur_match
   Arr(Byte) user_data;   // used with tagfunc
} Taggy;

#include "indices/optionCounts.h"

typedef enum {

#define OPTIONS_ENUM
#define OPTIONS_DEF_PORTAL
#include "defoption.h"
#undef OPTIONS_DEF_PORTAL
#undef OPTIONS_ENUM

} PortOption;


EXTERN Sbuf portalStringOptionsG;      //Storage for all the string options

//All portal-local options. Also used in PortInfo.
typedef struct {

#define OPTIONS_FIELDS
#define OPTIONS_DEF_PORTAL
#include "defoption.h"
#undef OPTIONS_DEF_PORTAL
#undef OPTIONS_FIELDS

   Boole foldEnableSave;  // @foldenable saved for diff mode
   int foldLevelSaved;
   Byte foldMethodSaved;  // @foldmethod saved for diff mode
   int scrollBindSave;   // @scrollbind saved for diff mode
   int diffSaved; // options were saved for starting diff mode
   int wrapSaved;   // @wrap state saved for diff mode
   int cursorBindSaved;   // @cursorbind state saved for diff mode
   ScriptPos scriptLocs[OPTION_PORTAL_COUNT];   // script locations for portal-local options
   Sbuf stringOptions;      //Storage for all the string options
} PortLocal;


//Portal info stored with a book.
//
//Two types of info are kept for a book which are associated with a specific portal:
//1. Each portal can have a different line number associated with a book.
//2. The portal-local options for a book work in a similar way.
//The portal-info is kept in a list at portInfos. It is kept in most-recently-used order.
struct PortInfo {
   PortInfo* next;   // next entry or NULL for last entry
   PortInfo* prev;   // previous entry or NULL for first entry
   Portal* portal;   // pointer to portal that did set wi_fpos
   Pos wi_fpos;   // last cursor position in the file
   PortLocal opt;      // portal-local options
   Boole isOptChanged;   // TRUE when wi_opt has useful values
   Boole foldManual;   // copy of Portal.foldManual
   ArrayList folds;   // clone of Portal.folds
   Unt wi_changelistidx; // copy of w_changelistidx
};

// Structure to store info about the Visual area.
typedef struct {
   Pos   vi_start;   // start pos of last VIsual
   Pos   vi_end;      // end position of last VIsual
   int      vi_mode;   // VIsual_mode of last VIsual
   ColNr   vi_curswant;   // MAXCOL from w_curswant
   int kind; // linewise, block or vertical visual mode?
} VisualInfo;

//structures used for undo

// One line saved for undo.  After the ZERO terminated text there might be text
// properties, thus ul_len can be larger than STRLEN(ul_line) + 1.
typedef struct {
   CS ul_line;   // text of the line
   long ul_len;      // length of the line including ZERO, plus text properties
   ColNr ul_textlen;   // length of the line excluding ZERO and any text properties
} UndoLine;

declStruct(UndoEntry);
declStruct(UndoHeader);
struct UndoEntry {
   UndoEntry* ue_next;   // pointer to next entry in list
   LineNr ue_top;      // number of line above undo block
   LineNr ue_bot;      // number of line below undo block
   LineNr ue_lcount;   // linecount when u_save called
   UndoLine* ue_array;   // array of lines in undo block
   long ue_size;   // number of lines in ue_array
#ifdef U_DEBUG
   int ue_magic;   // magic number to check allocation
#endif
};

struct UndoHeader {
   // The following have a pointer and a number. The number is used when
   // reading the undo file in u_read_undo()
   union {
      UndoHeader* ptr;   // pointer to next undo header in list
      long seq;
   } next;
   union {
      UndoHeader* ptr;   // pointer to previous header in list
      long seq;
   } prev;
   union {
      UndoHeader* ptr;   // pointer to next header for alt. redo
      long seq;
   } altNext;
   union {
      UndoHeader* ptr;   // pointer to previous header for alt. redo
      long seq;
   } altPrev;
   long   uh_seq;      // sequence number, higher == newer undo
   int      uh_walk;   // used by undo_time()
   UndoEntry* uh_entry;   // pointer to first entry
   UndoEntry* uh_getbot_entry; // pointer to where ue_bot must be set
   Pos   uh_cursor;   // cursor position before saving
   long   uh_cursor_vcol;
   int      uh_flags;   // see below
   Pos   uh_namedm[NMARKS];   // marks before undo/after redo
   VisualInfo uh_visual;   // Visual areas before undo/after redo
   Tyme   uh_time;   // timestamp when the change was made
   long   uh_save_nr;   // set when the file was saved after the changes in this block
#ifdef U_DEBUG
   int      uh_magic;   // magic number to check allocation
#endif
};

// values for uh_flags
#define UH_CHANGED  0x01   // wasModified flag before undo/after redo
#define UH_EMPTYBUF 0x02   // book was empty

// structures used in undo.c
#define ALIGN_LONG   // longword alignment and use filler byte
#define ALIGN_SIZE (sizeof(long))

#define ALIGN_MASK (ALIGN_SIZE - 1)

//}}}
//{{{memfile

// things used in memfile.c

typedef struct BlockHeader BlockHeader;
typedef struct MemFile MemFile;
typedef long BlockId;

//MfHashTable is a chained hashtable with BlockId key and arbitrary structures as items. This is 
//an intrusive data structure: we require that items begin with MfHashItem which contains the key 
//and linked list pointers.  List of items in each bucket is doubly-linked.
declStruct(MfHashItem);

struct MfHashItem {
   MfHashItem* next;
   MfHashItem* prev;
   BlockId key;
};

#define MHT_INIT_SIZE   64

typedef struct mf_hashtab_S {
   Ulong mask; // mask used for hash value (nr of items in array is "mht_mask" + 1)
   Ulong mht_count;       // nr of items inserted into hashtable
   MfHashItem** mht_buckets;  // points to mht_small_buckets or dynamically allocated array
   MfHashItem* mht_small_buckets[MHT_INIT_SIZE];   // initial buckets
   Byte mht_fixed;       // non-zero value forbids growth
} MfHashTable;

//for each (previously) used block in the memfile there is one block header.
//
//The block may be linked in the used list OR in the free list.
//The used blocks are also kept in hash lists.
//
//The used list is a doubly linked list, most recently used block first.
//  The blocks in the used list have a block of memory allocated.
//  mf_used_count is the number of pages in the used list.
//The hash lists are used to quickly find a block in the used list.
//The free list is a single linked list, not sorted.
//  The blocks in the free list have no block of memory allocated and
//  the contents of the block in the file (if any) is irrelevant.

struct BlockHeader {
   MfHashItem hashItem;      // header for hash table and key
#define bh_bnum hashItem.key // block number, part of hashItem

   BlockHeader* bh_next;       // next block_hdr in free or used list
   BlockHeader* bh_prev;       // previous block_hdr in used list
   Arr(Byte) bh_data;       // pointer to memory (for used block)
   int pageCount;       // number of pages in this block

#define BH_DIRTY    1
#define BH_LOCKED   2
   Byte bh_flags;       // BH_DIRTY or BH_LOCKED
};

declStruct(TextChunk);
declStruct(TextHeader);

// structure used to store one block of the stuff/redo/recording buffers
struct TextChunk {
   TextChunk* next;   // pointer to next text chunk
   Unt b_strlen;   // length of b_str, excluding the ZERO
   Byte b_str[1];   // contents (actually longer)
};

// header used for the stuff buffer and the redo buffer
struct TextHeader {
   TextChunk first;   // first (dummy) block of list
   TextChunk* bh_curr;   // text chunk for appending
   int bh_index;   // index for reading
   int bh_space;   // space in bh_curr for appending
   int bh_create_newblock;   // create a new block?
};

typedef struct {
   TextHeader sr_redobuff;
   TextHeader sr_old_redobuff;
} SaveRedo;

typedef enum {
   XP_PREFIX_NONE,   // prefix not used
   XP_PREFIX_NO,   // "no" prefix for bool option
   XP_PREFIX_INV,   // "inv" prefix for bool option
} ExpandPrefixKind;

//}}}
//{{{regexp

declStruct(RegProg);

//The number of sub-matches is limited to 10.
//The first one (index 0) is the whole match, referenced with "\0".
//The second one (index 1) is the first sub-match, referenced with "\1".
//This goes up to the tenth (index 9), referenced with "\9".
#define NSUBEXP  10

// Structure to be used for single-line matching. Sub-match "no" starts at "startp[no]" and ends 
// just before "endp[no]". When there is no match, the pointer is NULL.
typedef struct {
   RegProg* regprog;
   Byte* startp[NSUBEXP];
   Byte* endp[NSUBEXP];
   ColNr rm_matchcol;   // match start without "\zs"
   int rm_ic;
} RegMatch;


// Structure used to store external references: "\z\(\)" to "\z\1".
// Use a reference count to avoid the need to copy this around.  When it goes
// from 1 to zero the matches need to be freed.
typedef struct {
   short   refcnt;
   Byte* matches[NSUBEXP];
} RegExternalMatch;


//Structure to be used for multi-line matching.
//Sub-match "no" starts in line "startpos[no].lnum" column "startpos[no].col"
//and ends in line "endpos[no].lnum" just before column "endpos[no].col".
//The line numbers are relative to the first line, thus startpos[0].lnum is always 0.
//When there is no match, the line number is -1.
typedef struct {
   RegProg* regprog;
   PosNoVirt startpos[NSUBEXP];
   PosNoVirt endpos[NSUBEXP];
   ColNr rmm_matchcol;   // match start without "\zs"
   Boole rmm_ic;
   ColNr rmm_maxcol;   // when not zero: maximum column
} RegMultilineMatch;

// Flags used by eeRegsub() and eeRegsub_both()
#define REGSUB_COPY      1
#define REGSUB_MAGIC     2
#define REGSUB_BACKSLASH 4

//Struct used for highlighting @hlsearch matches, matches defined by
//":match" and matches defined by match functions.
//For @hlsearch there is one pattern for all portals.  For ":match" and the
//match functions there is a different pattern for each portal.
typedef struct {
   RegMultilineMatch rm; // points to the regexp program; contains last
            // found match (may continue in next line)
   Book* book;       // the book to search for a match
   LineNr lnum;       // the line to search for a match
   ExtraDeco extra; // decoration to be used for a match
   Short currHiId;   // decorations currently active in drawLineOnScreen()
   LineNr first_lnum; // first lnum to search for multi-line pat
   ColNr startcol;   // in win_line() points to char where HL starts
   ColNr endcol;       // in win_line() points to char where HL ends
   Boole is_addpos;  // position specified directly by matchaddpos(). TRUE/FALSE
   Boole has_cursor; // TRUE if the cursor is inside the match, used for CurSearch
} Match;

// Same as PosNoVirt, but with additional field len.
typedef struct {
   LineNr lnum;   // line number
   ColNr col;   // column number
   int len;   // length: 0 - to the end of line
} PosNoVirtLen;

// provides a linked list for storing match items for ":match", matchadd() and matchaddpos()
declStruct(MatchItem);
struct MatchItem {
   MatchItem* next;
   int id;      // match ID
   int priority;   // match priority

   // Either a pattern is defined (mit_pattern is not ZERO) or a list of
   // positions is given (mit_pos is not NULL and mit_pos_count > 0).
   Byte* pattern;   // pattern to hilite
   RegMultilineMatch match;   // regexp program for pattern

   Arr(PosNoVirtLen) pos; // positions
   int      posLen;   // nr of entries in mit_pos
   int      currPos;   // internal position counter
   LineNr   topLnum;   // top book line
   LineNr   bottLnum;   // bottom book line

   Match mit_hl;      // struct for doing the actual highlighting
   Short hiId;   // highlight group ID
};


//}}}
//{{{command line

// For conditional commands a stack is kept of nested conditionals. When cs_idx < 0, there is no 
// conditional command.
#define CSTACK_LEN   50

// Struct used by those that are using an item in a list.
typedef struct ListWatch ListWatch;
typedef struct ListItem ListItem;

struct ListWatch {
   ListItem* c;   // item being watched
   ListWatch* next;   // next watcher
};

declStruct(ForInfo);

// A list used for saving values of "emsg_silent".  Used by ex_try() to save the
// value of "emsg_silent" if it was non-zero.  When this is done, the CSF_SILENT flag below is set.

typedef struct EMsgList EMsgList;
struct EMsgList {
   int saved_emsg_silent;   // saved value of "emsg_silent"
   EMsgList* next;         // next element on the list
};

// type of getline() last argument
typedef enum {
   GETLINE_NONE,       // do not concatenate any lines
   GETLINE_CONCAT_CONT,    // concatenate continuation lines with backslash
   GETLINE_CONCAT_CONTBAR, // concatenate continuation lines with \ and |
   GETLINE_CONCAT_ALL      // concatenate continuation and Vim9 # comment lines
} GetlineAlgo;

typedef CS (*LineGetter)(Unt, void *, int, GetlineAlgo);

typedef struct {
   short flags[CSTACK_LEN];   // CSF_ flags
   char pending[CSTACK_LEN];   // CSTP_: what's pending in ":finally"
   union {
      void* csp_rv[CSTACK_LEN];   // return typeval for pending return
      void* csp_ex[CSTACK_LEN];   // exception for pending throw
   } pend;
   ForInfo* forInfo[CSTACK_LEN]; // info used by ":for"
   int cs_line[CSTACK_LEN];   // line nr of ":while"/":for" line
   int cs_block_id[CSTACK_LEN];    // block ID stack
   int cs_script_var_len[CSTACK_LEN];   // value of sn_var_vals.len
                  // when entering the block
   int ind;         // current entry, or -1 if none
   int loopLevel;      // nr of nested ":while"s and ":for"s
   int tryLevel;      // nr of nested ":try"s
   EMsgList* cs_emsg_silent_list;   // saved values of "emsg_silent"
   char loopFlags;      // the CSL_ flags
} CondStack;

// An invocation of a Command
struct Invocation {
   CS comm;      // the name of the command (except for :make)
   CommIndex id;      // the index for the command
   CS arg;      // argument of the command
   CS*  commline;   // pointer to pointer of allocated cmdline
   CS commlineToFree; // free later
   long   argFlags;      // flags for the command
   Boole skip;      // don't execute the command, only parse it
   Boole forceit;   // TRUE if ! present
   int addr_count;   // the number of addresses given
   LineNr line1;      // the first line number
   LineNr line2;      // the second line number or count
   CommandAddress addressKind;   // kind of the count/range
   Unt flags;      // extra flags after count: EXFLAG_
   CS higherOrderComm;   // +command arg to be used in edited file
   LineNr higherOrderLnum;   // the line number in an edited file
   int append;      // TRUE with ":w >>file" command
   int usefilter;   // TRUE with ":w !command" and ":r!command"
   int amount;      // number of '>' or '<' for shift command
   int regname;   // register name (NUL if none)
   Unt force_bin;   // 0, FORCE_BIN or FORCE_NOBIN
   Boole read_edit;   // ++edit argument
   Unt bad_char;   // BAD_KEEP, BAD_DROP or replacement byte
   Unt useridx;   // user command index
   CS errmsg;   // returned error message
   LineGetter ea_getline;
   void* cookie;   // argument for getline()
   CondStack* cstack;   // condition stack for ":if" etc.
};

typedef struct {
   Arr(CS) c;
   Unt len;
   Unt cap;
   Arena* a;
} ExpandMatch;

//Fuzzy matched string list item. Used for fuzzy match completion. Items are
//usually sorted by 'score'. The 'idx' member is used for stable-sort.
typedef struct {
   int idx;
   CS str;
   int score;
} FuzzyMatch;

typedef struct {
   Arr(FuzzyMatch) c;
   Unt len;
   Unt cap;
   Arena* a;
} Fuzzy;

// used for completion on the command line
typedef struct expand {
   Text input;   //start of item to expand, guaranteed to be part of fullInput
   CS fullInput; //text being completed
   Unt context;       //type of expansion, EXPAND_* constants
   ExpandPrefixKind xp_prefix;
   CS completionFn;
   ScriptPos scriptCtx;// SCTX for completion function
   int backslash;      // one of the XP_BS_ values
   int isShell;  // TRUE for a shell command, more characters need to be escaped
   int xp_col; // cursor position in line
   Unt xp_selected; // selected index in completion
   CS orig; // originally expanded string
   ExpandMatch files; // list of files
#define EXPAND_BUF_LEN 256
   Byte matchBuilder[EXPAND_BUF_LEN]; // buffer for returned match
   Byte searchDirection; // Direction of search
   Pos xp_pre_incsearch_pos; // Cursor position before incsearch
} Expand;

// values for backslash
#define XP_BS_NONE    0 //nothing special for backslashes
#define XP_BS_ONE   0x1 //uses one backslash before a space
#define XP_BS_THREE 0x2 //uses three backslashes before a space
#define XP_BS_COMMA 0x4 //commas need to be escaped with a backslash

//Variables shared between getcommline(), redrawcommline() and others.
//These need to be saved when using CTRL-R |, that's why they are in a structure.
typedef struct {
   CS commBuf;   // pointer to command line buffer
   int cmdbufflen;   // length of commbuff
   int cmdlen;      // number of chars in command line
   int cmdpos;      // current cursor position
   int cmdspos;   // cursor column on screen
   int cmdfirstc;   // ':', '/', '?', '=', '>' or ZERO
   int cmdindent;   // number of spaces before commline
   CS cmdprompt;   // message in front of commline
   int cmdattr;   // attributes for prompt
   int overstrike; // Typing mode on the command line. Shared by getcommline() and put_on_cmdline()
   Expand* xpc;      // struct being used for expansion, xp_pattern. may point into cmdbuff
   Unt context;   // type of expansion
   Arr(Byte) completionFn;   // user-defined expansion arg
   int input_fn;   // when TRUE Invoked for input() function
} CommlineInfo;

// Command modifiers ":vertical", ":browse", ":confirm" and ":hide" set a flag.
// This needs to be saved for recursive commands, put them in a structure for easy manipulation.
typedef struct {
   Unt cmod_flags;      // CMOD_ flags
#define CMOD_SILENT       0x0002   // ":silent"
#define CMOD_ERRSILENT    0x0004   // ":silent!"
#define CMOD_UNSILENT     0x0008   // ":unsilent"
#define CMOD_NOAUTOCMD    0x0010   // ":noautocmd"
#define CMOD_HIDE         0x0020   // ":hide"
#define CMOD_BROWSE       0x0040   // ":browse" - invoke file dialog
#define CMOD_CONFIRM      0x0080   // ":confirm" - invoke yes/no dialog
#define CMOD_KEEPALT      0x0100   // ":keepalt"
#define CMOD_KEEPMARKS    0x0200   // ":keepmarks"
#define CMOD_KEEPJUMPS    0x0400   // ":keepjumps"
#define CMOD_LOCKMARKS    0x0800   // ":lockmarks"
#define CMOD_KEEPPATTERNS 0x1000   // ":keeppatterns"
#define CMOD_NOSWAPFILE   0x2000   // ":noswapfile"

   int cmod_split;      // flags for win_split()
   int cmod_tab;      // > 0 when ":tab" was used
   RegMatch   cmod_filter_regmatch;   // set by :filter /pat/
   int cmod_filter_force;   // set for :filter!

   int cmod_verbose;      // 0 if not set, > 0 to set 'verbose' to cmod_verbose - 1

   // values for undo_cmdmod()
   CS cmod_save_ei;      // saved value of 'eventignore'
   long cmod_verbose_save;   // if 'verbose' was set: value of p_verbose plus one
   int cmod_save_msg_silent;   // if non-zero: saved value of msg_silent + 1
   int cmod_save_msg_scroll;   // for restoring msg_scroll
   int cmod_did_esilent;   // incremented when emsg_silent is
} CommandModifier;

//}}}
//{{{memfile

typedef enum {
   MF_DIRTY_NO = 0,      // no dirty blocks
   MF_DIRTY_YES,      // there are dirty blocks
   MF_DIRTY_YES_NOSYNC,   // there are dirty blocks, do not sync yet
} MfDirty;

struct MemFile {
   CS fullFName;      // name of the file
   CS fName;          // idem, full path
   int      fd;         // file descriptor
   int      mf_flags;      // flags used when opening this memfile
   int      mf_reopen;      // mf_fd was closed, retry opening
   BlockHeader   *freeFirst;      // first block_hdr in free list
   BlockHeader   *usedFirst;      // mru block_hdr in used list
   BlockHeader   *usedLast;      // lru block_hdr in used list
   unsigned   mf_used_count;      // number of pages in used list
   unsigned   usedCountMax;   // maximum number of pages in memory
   MfHashTable mf_hash;      // hash lists
   MfHashTable mf_trans;      // trans lists
   BlockId   mf_blocknr_max;      // highest positive block number + 1
   BlockId   mf_blocknr_min;      // lowest negative block number - 1
   BlockId   mf_neg_count;      // number of negative blocks numbers
   BlockId   pagesInFile;   // number of pages in the file
   unsigned   pageSize;      // number of bytes in a page
   MfDirty   mf_dirty;
   Book* book;      // book this memfile is for
};

// things used in memory.c

typedef struct ml_chunksize {
   int      mlcs_numlines;
   long   mlcs_totalsize;
} MemChunkSize;

// Flags when calling ml_updatechunk()
# define ML_CHNK_ADDLINE 1
# define ML_CHNK_DELLINE 2
# define ML_CHNK_UPDLINE 3
declStruct(InfoPtr);

// the memline structure holds all the information about a memline
typedef struct memline {
   LineNr   lineCount;   // number of lines in the book
   MemFile* mfile;   // pointer to associated memfile
   Arr(InfoPtr) ml_stack;   // stack of pointer blocks (array of IPTRs)
   int      ml_stack_top;   // current top of ml_stack
   int      ml_stack_size;   // total number of entries in ml_stack

#define ML_EMPTY        0x01   // empty book
#define ML_LINE_DIRTY   0x02   // cached line was changed and allocated
#define ML_LOCKED_DIRTY 0x04   // ml_locked was changed
#define ML_LOCKED_POS   0x08   // ml_locked needs positive block number
   Unt flags;

   ColNr   lineLen;   // length of the cached line + ZERO + text properties
   ColNr   lineTextLen;// length of the cached line + ZERO, 0 if not known yet
   LineNr   ml_line_lnum;   // line number of cached line, 0 if not valid
   CS cachedLine;

   BlockHeader* locked;   // block used by last ml_get
   LineNr   lockedLow;   // first line in locked
   LineNr   lockedHigh;   // last line in locked
   int      lockedInsertedLines;  // number of lines inserted in ml_locked
   MemChunkSize *ml_chunksize;
   int      ml_numchunks;
   int      ml_usedchunks;
} MemBuf;

// Values for the flags argument of ml_delete_flags().
#define ML_DEL_MESSAGE      1   // may give a "No lines in book" message
#define ML_DEL_UNDO         2   // called from undo, do not update textprops
#define ML_DEL_NOPROP       4   // splitting data block, do not update textprops

// Values for the flags argument of ml_append_int().
#define ML_APPEND_NEW       1   // starting to edit a new file
#define ML_APPEND_MARK      2   // mark the new line
#define ML_APPEND_UNDO      4   // called from undo
#define ML_APPEND_NOPROP    8   // do not continue textprop from previous line

// Filter to find a list of files. Useful for making lists of project files for grepping
typedef struct {
   Arr(Byte) subdir; // like "src", where to search for files
   Arr(Byte) includedExtensions; // like "c,h,cpp"
   Arr(Byte) excludedSubdirs; // like ".git,.vscode,node_modules"
} FileFilter;

//}}}
//{{{text properties & signs

//Structure defining text properties.  These stick with the text.
//When stored in memline they are after the text, lineLen is larger than STRLEN(ml_line_ptr) + 1.
typedef struct TextProp {
   ColNr col;    // start column (one based, in bytes)
   ColNr len;    // length in bytes, when tp_id is negative used for left padding plus 1
   int id;      // identifier
   int type;    // property type
   int flags;   // TEXT_PROP_ values
   int leftPad; // left padding between text line and virtual text
} TextProp;

#define TEXT_PROP_CONT_NEXT 0x1   // property continues in next line
#define TEXT_PROP_CONT_PREV 0x2   // property was continued from prev line

// without these text is placed after the end of the line
#define TEXT_PROP_ALIGN_RIGHT 0x010   // virtual text is right-aligned
#define TEXT_PROP_ALIGN_ABOVE 0x020   // virtual text above the line
#define TEXT_PROP_ALIGN_BELOW 0x040   // virtual text on next screen line
#define TEXT_PROP_WRAP        0x080   // virtual text wraps - when missing text is truncated
#define TEXT_PROP_START_INCL  0x100   // "start_incl" copied from proptype

#define PROP_TEXT_MIN_CELLS   4       // minimum number of cells to use for the text, even when truncating

// Structure defining a property type.
typedef struct PropType {
   int id;      // value used for tp_id
   int ty;    // number used for tp_type
   int hilite;   // hiliting
   int priority;// priority
   int flags;   // PT_FLAG_ values
   Byte name[1]; // property type name, actually longer
} PropType;

#define PT_FLAG_INS_START_INCL   1   // insert at start included in property
#define PT_FLAG_INS_END_INCL   2   // insert at end included in property
#define PT_FLAG_COMBINE      4   // combine with syntax highlight
#define PT_FLAG_OVERRIDE   8   // override any highlight

// Sign group
typedef struct signgroup_S {
   int sg_next_sign_id; //next sign id for this group
   Short sg_refcount;   //number of signs in this group
   Boole isPopupOnly;    //is this group for popup portals only?
   Byte sg_name[1];     //sign group name, actually longer
} SignGroup;

typedef struct SignEntry SignEntry;
struct SignEntry {
   int id;      // unique identifier for each placed sign
   int typeNr;   // typenr of sign
   int priority;   // priority for highlighting
   LineNr lnum;   // line number which has this sign
   SignGroup* group;   // sign group
   SignEntry* next;   // next entry in a list of signs
   SignEntry* prev;   // previous entry -- for easy reordering
};

// Sign hiliting. Used by the screen refresh routines.
typedef struct {
   int typeNr;
   void* icon;
   Arr(Byte) text;
   Short textHiId;
   Short lineHiId;
   Short cursorLineHiId;
   Short lineNumHiId;
   int priority;
} SignHilite;

// Macros to get the sign group structure from the group name
#define SGN_KEY_OFF   offsetof(SignGroup, sg_name)
#define HI2SG(hi)   ((SignGroup *)((hi)->hi_key - SGN_KEY_OFF))

// Default sign priority for highlighting
#define SIGN_DEF_PRIO   10

//}}}
//{{{messages

// Argument list: Array of file names.
// Used for the global argument list and the argument lists local to a window.
typedef struct {
   ArrayList   al_ga;      // growarray with the array of file names
   int      al_refcount;   // number of windows using this arglist
   int      id;      // id of this arglist
} EeArgList;

//For each argument remember the file name as it was given, and the book number that contains 
//the expanded file name (required for when ":cd" is used).
typedef struct ArgFileEntry {
   CS fname;   // file name as specified
   int fnum;   // book number with expanded file name
} ArgFileEntry;

#define GARGLIST   ((ArgFileEntry *)argListG.al_ga.c)
#define ARGLIST      ((ArgFileEntry *)curPor->argList->al_ga.c)
#define WARGLIST(wp)   ((ArgFileEntry *)wp->argList->al_ga.c)
#define AARGLIST(al)   ((ArgFileEntry *)((al)->al_ga.c))
#define GARGCOUNT   (argListG.al_ga.len)
#define ARGCOUNT   (curPor->argList->al_ga.len)
#define WARGCOUNT(wp)   (wp->argList->al_ga.len)

// There is no CSF_IF, the lack of CSF_WHILE, CSF_FOR and CSF_TRY means ":if" was used.
# define CSF_TRUE   0x0001   // condition was TRUE
# define CSF_ACTIVE   0x0002   // current state is active
# define CSF_ELSE   0x0004   // ":else" has been passed
# define CSF_WHILE   0x0008   // is a ":while"
# define CSF_FOR   0x0010   // is a ":for"
# define CSF_BLOCK   0x0020   // is a "{" block

# define CSF_TRY   0x0100   // is a ":try"
# define CSF_FINALLY   0x0200   // ":finally" has been passed
# define CSF_CATCH   0x0400   // ":catch" has been seen
# define CSF_THROWN   0x0800   // exception thrown to this try conditional
# define CSF_CAUGHT   0x1000  // exception caught by this try conditional
# define CSF_FINISHED   0x2000  // CSF_CAUGHT was handled by finish_exception()
# define CSF_SILENT   0x4000   // "emsg_silent" reset by ":try"
// Note that CSF_ELSE is only used when CSF_TRY and CSF_WHILE are unset
// (an ":if"), and CSF_SILENT is only used when CSF_TRY is set.

# define CSF_FUNC_DEF   0x8000   // a function was defined in this block

//What's pending for being reactivated at the ":endtry" of this try conditional:
# define CSTP_NONE   0   // nothing pending in ":finally" clause
# define CSTP_ERROR   1   // an error is pending
# define CSTP_INTERRUPT   2   // an interrupt is pending
# define CSTP_THROW   4   // a throw is pending
# define CSTP_BREAK   8   // ":break" is pending
# define CSTP_CONTINUE   16   // ":continue" is pending
# define CSTP_RETURN   24   // ":return" is pending
# define CSTP_FINISH   32   // ":finish" is pending

// Flags for the cs_lflags item in CondStack.
# define CSL_HAD_LOOP    1   // just found ":while" or ":for"
# define CSL_HAD_ENDLOOP 2   // just found ":endwhile" or ":endfor"
# define CSL_HAD_CONT    4   // just found ":continue"
# define CSL_HAD_FINA    8   // just found ":finally"

//A list of error messages that can be converted to an exception.  "throw_msg"
//is only set in the first element of the list.  Usually, it points to the
//original message stored in that element, but sometimes it points to a later
//message in the list.  See cause_errthrow().
typedef struct MsgList MsgList;
struct MsgList {
   MsgList   *next;      // next of several messages in a row
   CS msg;      // original message, allocated
   CS throw_msg;   // msg to throw: usually original one
   CS sfile;      // value from estack_sfile(), allocated
   long   slnum;      // line number for "sfile"
};

// The exception types.
typedef enum {
   ET_USER,      // exception caused by ":throw" command
   ET_ERROR,      // error exception
   ET_INTERRUPT,   // interrupt exception triggered by Ctrl-C
} ExceptionKind;

typedef struct Exception Exception;
struct Exception {
   ExceptionKind   type;      // exception type
   CS value;      // exception value
   MsgList* messages;   // message(s) causing error exception
   CS throw_name;   // name of the throw point
   LineNr      throw_lnum;   // line number of the throw point
   List* stacktrace;   // stacktrace
   Exception* caught;   // next exception on the caught stack
};

// Structure to save the error/interrupt/exception state between calls to
// enter_cleanup() and leave_cleanup().  Must be allocated as an automatic
// variable by the (common) caller of these functions.
typedef struct {
   int pending;      // error/interrupt/exception state
   Exception* exception;   // exception value
} Cleanup;

// Exception state that is saved and restored when calling timer callback
// functions and deferred functions.
typedef struct {
   Exception* currentException;
   int didThrow;
   int needRethrow;
   int tryLevel;
   int didEmsg;
} ExceptionState;

declStruct(SyntaxState);

// Used for the typeahead buffer: typebuf.
typedef struct {
   CS c;   // The contents. Vbuffer for typed characters
   CS noremap;   // mapping flags for characters in c[]
   int len;   // length of c[]
   int currPos;      // current position in c[]
   int validLen;   // number of valid bytes in tb_buf[]
   int mappedLen;   // nr of mapped bytes in tb_buf[]
   int silentCnt;   // nr of silently mapped bytes in tb_buf[]
   int noAbbrCnt; // nr of bytes without abbrev. in tb_buf[]
   int changeCnt;   // nr of time tb_buf was changed; never zero
} Typeahead;

// Struct to hold the saved typeahead for save_typeahead().
typedef struct {
   Typeahead      save_typebuf;
   int         typebuf_valid;       // TRUE when save_typebuf valid
   Unt         old_char;
   int         oldModMask;
   TextHeader   save_readbuf1;
   TextHeader   save_readbuf2;
#ifdef USE_INPUT_BUF
   Byte      *save_inputbuf;
#endif
} TypeaheadSave;

// Structure used for the command line history.
typedef struct HistoryEntry {
   int      hisnum;      // identifying number
   int      eeglinfo;   // when TRUE hisstr comes from eeglinfo
   Byte   *hisstr;   // actual entry, separator char after the ZERO
   Unt   hisstrlen;   // length of hisstr (excluding the ZERO)
   Tyme   time_set;   // when it was typed, zero if unknown
} HistoryEntry;

#define CONV_NONE      0

// Structure used for mappings and abbreviations.
typedef struct mapblock MapBlock;
struct mapblock {
   MapBlock* next;  // next mapblock in list
   MapBlock* alt;   // pointer to mapblock of the same mapping
                    // with an alternative form of m_keys, or NULL iff there is no such mapblock
   CS lhs;   // mapped from, lhs
   CS rhs;      // mapped to, rhs
   CS origRhs;   // rhs as entered by the user
   int keylen;   // strlen(m_keys)
   int mode;      // valid mode
   int simplified;   // lhs was simplified: don't use this mapping
   Unt noremap;   // if non-zero no re-mapping for m_str
   char silent;   // <silent> used, don't echo commands
   char nowait;   // <nowait> used
   char expr;      // <expr> used, m_str is an expression
   ScriptPos scriptCtx;   // SCTX where map was defined
};


// Used for hiliting in the status line
typedef struct {
   CS start;
   Short hiId;      // 0: no HL, 1-9: User HL, < 0 for syn ID
} StatusLineHilite;

//}}}
//{{{hash tables

// Syntax items - usually book-specific.

// Item for an EeSet hash set.  "hi_key" can be one of three values:
// NULL:      Never been used
// HI_KEY_REMOVED: Entry was removed
// Otherwise: Used item, pointer to the actual key; this usually is
//            inside the item, subtract an offset to locate the item.
//            This reduces the size of hashitem by 1/3.
typedef struct {
   Ulong hi_hash;   // cached hash number of hi_key
   Byte* hi_key;
   Unt len;
} EeSetItem;

//The address of "hash_removed" is used as a magic number for hi_key to indicate a removed item.
#define HI_KEY_REMOVED &hash_removed
#define HASHITEM_EMPTY(hi) ((hi)->len == 0 || (hi)->hi_key == &hash_removed)

// Initial size for a hashtable. Our items are relatively small and growing
// is expensive, thus use 16 as a start.  Must be a power of 2.
// This allows for storing 10 items (2/3 of 16) before a resize is needed.
#define HT_INIT_SIZE 16
#define HTFLAGS_ERROR  0x01 //Set when growing failed, can't add more items before growing works.
#define HTFLAGS_FROZEN 0x02 //Trying to add or remove an item will result in an error message.

//Hash set for arbitrary on-heap data
typedef struct EeSet {
   Ulong   mask;   // mask used for hash value (nr of items in array is "ht_mask" + 1)
   Ulong   count;   // number of items present
   Ulong   occupied;   // number of items used + removed
   int     changes;   // incremented when adding or removing an item
   int     ht_locked;   // counter for hash_lock()
   Unt     flags;   // HTFLAGS_ values
   EeSetItem* array;   // points to the array, allocated when it's not "ht_smallarray"
   EeSetItem smallArray[HT_INIT_SIZE];   // initial array
} EeSet;

typedef Ulong Hash;      // Type for hi_hash


//Struct that holds both a normal function name and a PartiallyApplied, as used for a callback 
//argument. When used temporarily, "name" is not allocated. The refcounts to either the 
//function or the partial are incremented and need to be decremented later with free_callback().
struct Callback {
   CS name;
   PartiallyApplied* cb_partial;
   Boole needsFreeing;
};
typedef Callback* CallbackPtr;


typedef struct TypeSpec TypeSpec;
typedef struct UserFunc UserFunc;

typedef struct ReadChunk ReadChunk;
declStruct(WriteQueue);
declStruct(JsonQ);
declStruct(CbNode);
declStruct(Channel);
typedef enum {
   VAR_UNKNOWN = 0,   // not set, any type or "void" allowed
   VAR_ANY,      // used for "any" type
   VAR_VOID,      // no value (function not returning anything)
   VAR_BOOL,      // "v_number" is used: VVAL_TRUE or VVAL_FALSE
   VAR_SPECIAL,   // "v_number" is used: VVAL_NULL or VVAL_NONE
   VAR_NUMBER,      // "v_number" is used
   VAR_FLOAT,      // "v_float" is used
   VAR_STRING,      // "v_string" is used
   VAR_BLOB,      // "v_blob" is used
   VAR_FUNC,      // "v_string" is function name
   VAR_PARTIAL,   // "v_partial" is used
   VAR_LIST,      // "v_list" is used
   VAR_BAG,      // "v_dict" is used
   VAR_JOB,      // "v_job" is used
   VAR_CHANNEL   // "v_channel" is used
} VarTag;

// A type specification.
struct TypeSpec {
   VarTag tag;
   SignedByte argCount;    // for func, incl. vararg, -1 for unknown
   SignedByte minArgCount; // number of non-optional arguments
   Byte flags;             // TTFLAG_ values
   TypeSpec* member;       // for list, dict, func return type
   TypeSpec** args;        // func argument types, allocated
};

#define TTFLAG_VARARGS    0x01 //func args ends with "..."
#define TTFLAG_BOOL_OK    0x02 //can be converted to bool
#define TTFLAG_FLOAT_OK   0x04 //number can be used/converted to float
#define TTFLAG_NUMBER_OK  0x08 //number can be used for a float
#define TTFLAG_STATIC     0x10 //one of the static types, e.g. t_any
#define TTFLAG_CONST      0x20 //cannot be changed
#define TTFLAG_SUPER      0x40 //object from "super".

//}}}
//{{{Vars

// Structure to hold an internal variable without a name.
struct Var {
   VarTag tag;
   char lock;       // see below: VAR_LOCKED, VAR_FIXED
   union {
      Long number;   // number value
      double floatt;   // floating point number value
      Arr(Byte) string;   // string value (can be NULL)
      List* list;   // list value (nullable)
      Bag* bag;   // dict value (can be NULL)
      PartiallyApplied* partial;   // closure: function with args
      Job* job;      // job value (can be NULL)
      Channel* channel;   // channel value (can be NULL)
      Blob* blob;   // blob value (can be NULL)
   };
};

typedef enum {
   VAR_GLOBAL,
   VAR_BOOK,
   VAR_PORTAL,
   VAR_TAB,
   VAR_SCRIPT, // To delete
   VAR_LOCAL,
   VAR_EEGL
} VarLevel;

// Values for "dv_scope".
#define VAR_SCOPE     1 //a:, e:, s:, etc. scope dictionaries
#define VAR_DEF_SCOPE 2 //l:, g: scope dictionaries: here funcrefs are not
                        // allowed to mask existing functions

// Values for "v_lock".
#define VAR_LOCKED       1   // locked with lock(), can use unlock()
#define VAR_FIXED        2   // locked forever
#define VAR_ITEMS_LOCKED 4   // items of non-materialized list locked

//Argument for {getLval()}
typedef struct {
   Text name; // variable name
   NULLABLE Var* returnVar;
   Boole unlet;
   Boole skip;
   Unt flags;       // GLV_ values
   int fneFlag;
} GetLval;

//}}}
//{{{List

// Structure to hold an item of a list: an internal variable without a name.
struct ListItem {
   ListItem* next;   // next item in list
   ListItem* prev;   // previous item in list
   Var c;      // the content of the list node
};


//Info about a list. Order of members is optimized to reduce padding.
//When created by range() it will at first have special value: first == &range_list_item;
//and use lv_start, lv_end, lv_stride.
struct List {
   ListItem* first; // first item, NULL if none, &range_list_item for a non-materialized list
   ListWatch* watcher;   // first watcher, NULL if none
   union {
      struct {   // used for non-materialized range list: "first" is &range_list_item
          Long start;
          Long end;
          int      stride;
      } nonmat;
      struct {   // used for materialized list
          ListItem* last;   // last item, NULL if none
          ListItem* cachedItem;   // when not NULL item at index "lv_idx"
          int      cachedInd;      // cached index of an item
      } mat;
   } lv_u;
   TypeSpec* ty;   // current type, allocated by alloc_type()
   List* copyList;   // copied list used by deepcopy()
   List* usedNext;   // next list in used lists list
   List* usedPrev;   // previous list in used lists list
   int refcount;   // reference count
   int len;      // number of items
   int withItems;   // number of items following this struct that should not be freed
   int copyId;   // ID used by deepcopy()
   char lock;   // zero, VAR_LOCKED, VAR_FIXED
};

// Static list with 10 items.  Use init_static_list() to initialize.
typedef struct {
   List list;   // must be first
   ListItem items[10];
} StaticList10;

//}}}
//{{{dictionaries

// Structure to hold an item of a Dictionary. Also used for a variable.
// The key is copied into "key" to avoid an extra alloc/free for it.
typedef struct {
   Var c; // content, a tagged variable
   Byte flags;   // DI_FLAGS_ flags (only used for variable)
   Unt len;
   Byte key[1];   // key (actually longer!)
} DictItem;

//A dictitem with a 16 character key (plus ZERO).  This is an efficient way to
//have a fixed-size dictitem.
#define DICTITEM16_KEY_LEN 16
typedef struct {
   Var c;      // type and value of the variable
   Byte flags;   // DI_FLAGS_ flags (only used for variable)
   Unt len;
   Byte key[DICTITEM16_KEY_LEN + 1];   // key
} DictItem16;

// Flags for "di_flags"
#define DI_FLAGS_RO      0x01       // read-only variable
#define DI_FLAGS_FIX      0x04       // fixed: no :unlet or remove()
#define DI_FLAGS_LOCK      0x08       // locked variable
#define DI_FLAGS_ALLOC      0x10       // separately allocated
#define DI_FLAGS_RELOAD      0x20       // set when script sourced again

// Bag (hash map for scripting Vars)
struct Bag {
   Byte lock;   // zero, VAR_LOCKED, VAR_FIXED
   Byte scope;   // zero, VAR_SCOPE, VAR_DEF_SCOPE
   int refcount;   // reference count
   int copyId;   // ID used by deepcopy()
   EeSet hashTable;   // hashtab that refers to the items
   TypeSpec* ty;   // current type, allocated by alloc_type()
   Bag* dv_copydict;   // copied bag used by deepcopy()
   Bag* dv_used_next;   // next bag in used bags list
   Bag* dv_used_prev;   // previous bag in used bags list
};

// Structure to hold info about a blob.
struct Blob {
   ArrayList c; // growarray with the data
   int refcount; // reference count
   char lock;    // zero, VAR_LOCKED, VAR_FIXED
};

typedef int (*cfunc_T)(int argcount, Var *argvars, Var *rettv, void *state);
typedef void (*cfunc_free_T)(void *state);

typedef struct FnCall FnCall;


// Structure to hold info for a user function. When adding a field, check 
// copy_lambda_to_global_func()
struct UserFunc {
   int uf_varargs;   // variable nr of arguments (old style)
   int uf_flags;   // FC_ flags
   int uf_calls;   // nr of active calls
   int uf_cleared;   // func_clear() was already called

   ArrayList   args;   // arguments, including optional arguments
   ArrayList   defaultArgs;   // default argument expressions
   int uf_args_visible; // normally uf_args.len, less when compiling default argument expression.

   Byte   *uf_va_name;   // name from "...name" or NULL
   TypeSpec   *uf_va_type;   // type from "...name: type" or NULL
   int      uf_block_depth;   // nr of entries in uf_block_ids
   int      *uf_block_ids;   // blocks a :def function is defined inside

   ArrayList   lines;   // function lines

   int      uf_debug_tick;   // when last checked for a breakpoint in this function.
   int      uf_has_breakpoint;  // TRUE when a breakpoint has been set in this function.
   ScriptPos scriptCtx;   // SCTX where function was defined, used for s: variables;
   int      refcount;   // reference count, see func_name_refcount()

   FnCall   *uf_scoped;   // l: local variables for closure

   Byte   *uf_name_exp;   // if "uf_name[]" starts with SNR the name with
            // "<SNR>" as a string, otherwise NULL
   Unt   uf_namelen;   // length of uf_name (excluding the ZERO)
   Byte   uf_name[4];   // name of function (actual size equals name);
            // can start with <SNR>123_ (<SNR> is K_SPECIAL KS_EXTRA KE_SNR)
};

// flags used in uf_flags
#define FC_ABORT      0x01   // abort function on error
#define FC_RANGE      0x02   // function accepts range
#define FC_DICT       0x04   // Bag function, uses "self"
#define FC_CLOSURE    0x08   // closure, uses outer scope variables
#define FC_DELETED    0x10   // :delfunction used while uf_refcount > 0
#define FC_REMOVED    0x20   // function redefined while uf_refcount > 0
#define FC_DEAD       0x40   // function kept only for reference to dfunc
#define FC_EXPORT     0x80   // "export def Func()"
#define FC_NOARGS    0x100   // no a: variables in lambda
#define FC_CFUNC     0x400   // defined as Lua C func
#define FC_LAMBDA    0x800   // one line "return {expr}"

#define MAX_FUNC_ARGS   20   // maximum number of function arguments

#define MAX_MAPPING_RECURSION 128
#define VAR_SHORT_LEN   20   // short variable name length
#define FIXVAR_CNT   12   // number of fixed variables


// Structure to hold info for a function that is currently being executed.
struct FnCall {
   UserFunc* fn;   // function being called
   int lineNr;   // next line to be executed
   int fc_returned;   // ":return" used
   struct  {       // fixed variables for arguments
      DictItem   var;      // variable (without room for name)
      Byte   room[VAR_SHORT_LEN];   // room for the name
   } fc_fixvar[FIXVAR_CNT];
   Bag localVars;   // l: local function variables
   DictItem localVarsVar;   // variable for l: scope
   
   Bag argVars;   // a: function argument variables
   DictItem argVarsVar;   // variable for the a: scope
   List arguments;   // list for a:000
   ListItem fc_l_listitems[MAX_FUNC_ARGS];   // listitems for a:000
   
   Var   *fc_returnVar;   // return value
   LineNr fc_breakpoint;   // next line with breakpoint or zero
   int fc_dbg_tick;   // debug_tick when breakpoint was set
   int fc_level;   // top nesting level of executed function

   ArrayList fc_defer;   // functions to be called on return

   FnCall* fc_caller;   // calling function or NULL; or next fncall in
            // list pointed to by previous_funccal.

   // for closure
   int      refcount;   // number of user functions that reference this funccal
   int      copyId;   // for garbage collection
   ArrayList   fc_ufuncs;   // list of UserFunc* which keep a reference to "fc_func"
};

declStruct(FnCallEntry);
struct FnCallEntry {
   void       *top_funccal;
   FnCallEntry* next;
};

// From user function to hashitem and back.
#define UF2HIKEY(fp) ((fp)->uf_name)
#define HIKEY2UF(p)  ((UserFunc *)((p) - offsetof(UserFunc, uf_name)))
#define HI2UF(hi)     HIKEY2UF((hi)->hi_key)

//}}}
//{{{scripting

// Holds the hash table with variables local to each sourced script.
// Each item holds a variable (nameless) that points to the Bag.
typedef struct {
   DictItem   sv_var;
   Bag   sv_dict;
} ScriptVar;

// Entry for "sn_all_vars".  Contains the s: variables from sn_vars plus the block-local ones.
typedef struct SnAllVars SnAllVars;
struct SnAllVars {
   SnAllVars* next;     // var with same name but different block
   int blockId;     // block ID where declared
   int indInVarVals; // index in sn_var_vals

   //So long as the variable is valid (block it was defined in is still
   //active) "sav_di" is used.  It is set to NULL when leaving the block,
   //then sav_tv and sav_flags are used.
   DictItem *sav_di;      // dictitem with di_key and c
   Var sav_tv;      // type and value of the variable
   Byte sav_flags;   // DI_FLAGS_ flags (only used for variable)
   Byte sav_key[1];   // key (actually longer!)
};

// In the sn_all_vars hashtab item "hi_key" points to "sav_key" in a SnAllVars.
// This makes it possible to store and find the SnAllVars.
// SAV2HIKEY() converts a SnAllVars pointer to a set value key pointer.
// HIKEY2SAV() converts a set value key pointer to a SnAllVars pointer.
// HI2SAV() converts a set value pointer to a SnAllVars pointer.
#define SAV2HIKEY(sav) ((sav)->sav_key)
#define HIKEY2SAV(p)  ((SnAllVars *)(p - offsetof(SnAllVars, sav_key)))
#define HI2SAV(hi)     HIKEY2SAV((hi)->hi_key)

#define SVFLAG_TYPE_ALLOCATED   1  // call free_type() for "sv_type"
#define SVFLAG_EXPORTED      2  // "export let var = val"
#define SVFLAG_ASSIGNED      4  // assigned a value

// Entry for "sn_var_vals".  Used for script-local variables.
typedef struct {
   Arr(Byte) sv_name;   // points into "sn_all_vars" di_key
   Var* sv_tv;      // points into "sn_vars" or "sn_all_vars" c
   TypeSpec* sv_type;
   int sv_flags;   // SVFLAG_ values above
   int sv_const;   // 0, ASSIGN_CONST or ASSIGN_FINAL
} Svar;

typedef struct {
   CS imp_name;       // name imported as (allocated)
   ScriptId imp_sid;       // script ID of "from"
   int imp_flags;       // IMP_FLAGS_ values
} Imported;

#define IMP_FLAGS_RELOAD   2   // script reloaded, OK to redefine
#define IMP_FLAGS_AUTOLOAD 4   // script still needs to be loaded

// Info about an encountered script.
// When sn_state has SN_STATE_NOT_LOADED, it has not been sourced yet.
typedef struct {
   CS sn_name;       // full path of script file
   int sn_script_seq;       // latest ScriptPos sc_seq value

   //When non-zero, the script ID of the actually sourced script. Used if a
   //script is used by a name which has a symlink, we list both names, but
   //only the linked-to script is actually sourced.
   int sn_sourced_sid;

   // "sn_vars" stores the s: variables currently valid.  When leaving a block
   // variables local to that block are removed.
   ScriptVar* sn_vars;

   // Stores all the existing variables as a list of Svar, so
   // that they can be quickly found by index. Also stores the type.
   ArrayList sn_var_vals; // Arr(Svar)

   ArrayList sn_imports;   // imported items, imported_T
   ArrayList sn_type_list;   // keeps types used by variables
   int sn_current_block_id; // ID for current block, 0 for outer
   int sn_last_block_id;  // Unique ID for each script block

   int sn_state;   // SN_STATE_ values
   Boole sn_syml_checked;// flag: this has been checked for sym link
} ScriptItem;

#define SN_STATE_NEW         0   // newly loaded script, nothing done
#define SN_STATE_NOT_LOADED  1   // script located but not loaded
#define SN_STATE_RELOAD      2   // script loaded before, nothing done
#define SN_STATE_HAD_COMMAND 9   // a command was executed

// Struct passed through eval() functions.
// See EVALARG_EVALUATE for a fixed value with eval_flags set to EVAL_EVALUATE.
typedef struct {
   int eval_flags;       // EVAL_ flag values below
   int eval_break_count;   // nr of line breaks consumed

   // copied from Invocation when "getline" is "getsourceline". Can be NULL.
   LineGetter eval_getline;
   void* eval_cookie;       // argument for eval_getline()

   // used when executing commands from a script, NULL otherwise
   CondStack* eval_cstack;

   // Used to collect lines while parsing them, so that they can be
   // concatenated later.  Used when "eval_ga.ga_itemsize" is not zero.
   // "eval_ga.c" is a list of pointers to lines.
   // "eval_freega" list pointers that need to be freed after concatenating.
   ArrayList eval_ga;
   ArrayList eval_freega;

   // pointer to the last line obtained with getsourceline()
   CS eval_tofree;

   // array with lines of an inline function
   ArrayList eval_tofree_ga;

   // set when "arg" points into the last entry of "eval_tofree_ga"
   int eval_using_cmdline;

   // pointer to the lines concatenated for a lambda.
   Byte* eval_tofree_lambda;
} EvalCtx;

// Flag for expression evaluation.
#define EVAL_EVALUATE       1       // when missing don't actually evaluate

// Struct passed between functions dealing with function call execution.
//
// "fe_argv_func", when not NULL, can be used to fill in arguments only when the
// invoked function uses them.  It is called like this:
//   new_argcount = fe_argv_func(current_argcount, argv, partial_argcount, called_func)
//
typedef struct {
   int (* fe_argv_func)(int, Var *, int, UserFunc *);
   LineNr fe_firstline; //first line of range
   LineNr fe_lastline;  //last line of range
   int* fe_doesrange;   //if not NULL: return: function handled range
   int fe_evaluate;     //actually evaluate expressions
   UserFunc* fe_ufunc;  //function to be called, when NULL lookup by name
   PartiallyApplied* fe_partial; //for "dict" and extra arguments
   Bag* fe_selfdict;  //Dictionary for "self"
   Var* fe_basetv;    //base for base->method()
   Boole fe_found_var;  //if the function is not found then give an
                      //error that a variable is not callable.
} FnExe;

// Structure to hold the variables declared in a loop that are possibly used in a closure.
declStruct(LoopVars);
struct LoopVars {
   LoopVars* lvs_next;   // linked list at "first_loopvars"
   LoopVars* lvs_prev;

   ArrayList lvs_ga;     // contains the variables
   int lvs_refcount;     // nr of closures referencing this loopvars
   int lvs_min_refcount; // nr of closures on this loopvars
   int lvs_copyID;       // for garbage collection
};

struct PartiallyApplied {
   int refcount; //reference count
   Boole isAuto; //when TRUE the partial is for using dict.member in handle_subscript()
   CS name;      //function name; when NULL use pt_func->uf_name
   UserFunc* fn; //function pointer; when NULL lookup function with pt_name
   Var* argv;    //arguments in allocated array
   int argc;     //number of arguments
   Bag* self;    //bag for "self"
};

typedef struct {
   Unt group; // augroup id or AUGROUP_ALL
   NULLABLE CS commandBody; // the body of the new command, or null if we are not creating
   Boole deleteExisting; // delete any existing autocommands?
   Boole once;
   Boole nested;
} AutoCommCreation;

typedef struct AutoPatComm AutoPatComm;

// Entry in the execution stack "exestack".
typedef enum {
   ETYPE_TOP,      //toplevel
   ETYPE_SCRIPT,   //sourcing script, use es_info.sctx
   ETYPE_UFUNC,    //user function, use es_info.ufunc
   ETYPE_AUCMD,    //autocomand, use es_info.aucmd
   ETYPE_EXCEPT,   //exception, use es_info.exception
   ETYPE_ARGS,     //command line argument
   ETYPE_ENV,      //environment variable
   ETYPE_INTERNAL, //internal operation
   ETYPE_SPELL     //loading spell file
} CallFrame;

typedef struct {
   long lnum;      //replaces "sourcing_lnum"
   CS name;     //replaces "sourcing_name"
   CallFrame ty;
   union {
      ScriptPos* sctx;    // script info
      UserFunc* ufunc;    // function info
      AutoPatComm* aucmd; // autocommand info
      Exception* except;  // exception info
   } info;
   
   ScriptPos sctxSaved;   // saved current_sctx when calling function
} Estack;

//}}}
//{{{channels & jobs

// Information returned by get_tty_info().
typedef struct {
   int backspace;   // what the Backspace key produces
   int enter;      // what the Enter key produces
   int interrupt;   // interrupt character
   int nl_does_cr;   // TRUE when a NL is expanded to CR-NL on output
} TtyInfo;

// Structures to hold info about a Channel.
struct ReadChunk {
   Arr(Byte) c;
   Ulong   len;
   ReadChunk* next;
   ReadChunk* prev;
};

struct WriteQueue {
   ArrayList   wq_ga;
   WriteQueue* next;
   WriteQueue* prev;
};

struct JsonQ {
   Var   *jq_value;
   JsonQ   *jq_next;
   JsonQ   *jq_prev;
   int      jq_no_callback; // TRUE when no callback was found
};

struct CbNode {
   Callback   cq_callback;
   int      cq_seq_nr;
   CbNode   *cq_next;
   CbNode   *cq_prev;
};

// mode for a channel
typedef enum {
   CH_MODE_NL = 0,
   CH_MODE_RAW,
   CH_MODE_JSON,
   CH_MODE_LSP      // Language Server Protocol (http + json)
} ChannelMode;

typedef enum {
   JIO_PIPE,       // default
   JIO_NULL,
   JIO_FILE,
   JIO_BUFFER,
   JIO_OUT
} JobIoMode;

// Ordering matters, it is used in for loops: IN is last, only SOCK/OUT/ERR are polled.
typedef enum {
   PART_SOCK = 0,
   PART_OUT,
   PART_ERR,
   PART_IN,
   PART_COUNT,
} ChannelFdKind;

#define INVALID_FD   (-1)

typedef struct timeval Elapsed;

// The per-fd info for a channel.
typedef struct {
   Socket   ch_fd;       // socket/stdin/stdout/stderr, -1 if not used

   ChannelMode   ch_mode;
   JobIoMode   ch_io;
   int      ch_timeout;   // request timeout in msec

   ReadChunk   head;   // header for circular raw read queue
   JsonQ   ch_json_head;   // header for circular json read queue
   ArrayList   ch_block_ids;   // list of IDs that channel_read_json_block() is waiting for
   // When ch_wait_len is non-zero use deadline to wait for incomplete message to be complete. 
   // The value is the length of the incomplete message when the deadline was set.  If it gets 
   // longer (something was received) the deadline is reset.
   Unt   ch_wait_len;
   TimeVal deadline;
   int ch_block_write; // for testing: 0 when not used, -1 when write
                       // does not block, 1 simulate blocking
   int ch_nonblocking; // write() is non-blocking
   WriteQueue ch_writeque;   // header for write queue

   CbNode ch_cb_head;   // dummy node for per-request callbacks
   void (*nativeCb)(Arr(Byte));
   Callback ch_callback;   // call when a msg is not handled

   BookRef   bookref;   // book to read from or write to
   int      ch_nomodifiable; // TRUE when book can be not 'modifiable'
   int      ch_nomod_error;   // TRUE when e_modifiable was given
   int      ch_buf_append;   // write appended lines instead top-bot
   LineNr   ch_buf_top;   // next line to send
   LineNr   ch_buf_bot;   // last line to send
} ChannelFd;

struct Channel {
   Channel   *next;
   Channel   *prev;

   int      id;      // ID of the channel
   int      lastMsgId;   // ID of the last message

   ChannelFd fds[PART_COUNT]; // info for socket, out, err and in
   int      writeTextMode; // write book lines with CR, not NL

   CS ch_hostname;   // only for socket, allocated
   int      ch_port;   // only for socket

   int      ch_to_be_closed; // bitset of readable fds to be closed.
            // When all readable fds have been closed, set to (1 << PART_COUNT).
   int      ch_to_be_freed; // When TRUE channel must be freed when it's safe to invoke callbacks
   int      error;   // When TRUE an error was reported.  Avoids giving pages full of error 
                        // messages when the other side has exited, only mention the first error 
                        // until the connection works again.

   Callback   ch_callback;   // call when any msg is not handled
   Callback   ch_close_cb;   // call when channel is closed
   int      ch_drop_never;
   int      ch_keep_open;   // do not close on read error
   int      ch_nonblock;

   Job* job;   // Job that uses this channel; this does not count as a reference to avoid a 
                  // circular reference, the job refers to the channel.
   int  ch_job_killed;   // TRUE when there was a job and it was killed or we know it died.
   int  ch_anonymous_pipe;  // ConPTY
   int  isBeingKilled;       // TerminateJobObject() was called

   int  refcount;   // reference count
   int  copyId;
};

#define JO_MODE          0x0001   // channel mode
#define JO_IN_MODE       0x0002   // stdin mode
#define JO_OUT_MODE       0x0004   // stdout mode
#define JO_ERR_MODE       0x0008   // stderr mode
#define JO_CALLBACK       0x0010   // channel callback
#define JO_OUT_CALLBACK       0x0020   // stdout callback
#define JO_ERR_CALLBACK       0x0040   // stderr callback
#define JO_CLOSE_CALLBACK   0x0080   // "close_cb"
#define JO_WAITTIME       0x0100   // only for ch_open()
#define JO_TIMEOUT       0x0200   // all timeouts
#define JO_OUT_TIMEOUT       0x0400   // stdout timeouts
#define JO_ERR_TIMEOUT       0x0800   // stderr timeouts
#define JO_PART          0x1000   // "part"
#define JO_ID          0x2000   // "id"
#define JO_STOPONEXIT       0x4000   // "stoponexit"
#define JO_EXIT_CB       0x8000   // "exit_cb"
#define JO_OUT_IO       0x10000   // "out_io"
#define JO_ERR_IO       0x20000   // "err_io" (JO_OUT_IO << 1)
#define JO_IN_IO       0x40000   // "in_io" (JO_OUT_IO << 2)
#define JO_OUT_NAME       0x80000   // "out_name"
#define JO_ERR_NAME       0x100000   // "err_name" (JO_OUT_NAME << 1)
#define JO_IN_NAME       0x200000   // "in_name" (JO_OUT_NAME << 2)
#define JO_IN_TOP       0x400000   // "in_top"
#define JO_IN_BOT       0x800000   // "in_bot"
#define JO_OUT_BUF       0x1000000   // "out_buf"
#define JO_ERR_BUF       0x2000000   // "err_buf" (JO_OUT_BUF << 1)
#define JO_IN_BUF       0x4000000   // "in_buf" (JO_OUT_BUF << 2)
#define JO_CHANNEL       0x8000000   // "channel"
#define JO_BLOCK_WRITE       0x10000000   // "block_write"
#define JO_OUT_MODIFIABLE   0x20000000   // "out_modifiable"
#define JO_ERR_MODIFIABLE   0x40000000   // "err_modifiable" (JO_OUT_ << 1)
#define JO_ALL          0x7fffffff

#define JO2_OUT_MSG       0x0001   // "out_msg"
#define JO2_ERR_MSG       0x0002   // "err_msg" (JO_OUT_ << 1)
#define JO2_TERM_NAME       0x0004   // "term_name"
#define JO2_TERM_FINISH       0x0008   // "term_finish"
#define JO2_ENV          0x0010   // "env"
#define JO2_CWD          0x0020   // "cwd"
#define JO2_TERM_ROWS       0x0040   // "term_rows"
#define JO2_TERM_COLS       0x0080   // "term_cols"
#define JO2_VERTICAL       0x0100   // "vertical"
#define JO2_CURPOR       0x0200   // "curpor"
#define JO2_HIDDEN       0x0400   // "hidden"
#define JO2_TERM_OPENCMD    0x0800   // "term_opencmd"
#define JO2_EOF_CHARS       0x1000   // "eof_chars"
#define JO2_NORESTORE       0x2000   // "norestore"
#define JO2_TERM_KILL       0x4000   // "term_kill"
#define JO2_ANSI_COLORS       0x8000   // "ansi_colors"
#define JO2_TTY_TYPE       0x10000   // "tty_type"
#define JO2_BUFNR       0x20000   // "bufnr"
#define JO2_TERM_API       0x40000   // "term_api"
#define JO2_TERM_HIGHLIGHT  0x80000   // "highlight"

#define JO_MODE_ALL   (JO_MODE + JO_IN_MODE + JO_OUT_MODE + JO_ERR_MODE)
#define JO_CB_ALL \
    (JO_CALLBACK + JO_OUT_CALLBACK + JO_ERR_CALLBACK + JO_CLOSE_CALLBACK)
#define JO_TIMEOUT_ALL   (JO_TIMEOUT + JO_OUT_TIMEOUT + JO_ERR_TIMEOUT)

// Options for job and channel commands.
typedef struct {
   int set;      // JO_ bits for values that were set
   int set1;   // JO2_ bits for values that were set

   ChannelMode mode;
   ChannelMode jo_in_mode;
   ChannelMode jo_out_mode;
   ChannelMode jo_err_mode;
   int      jo_noblock;

   JobIoMode  ioMode[4];   // PART_SOCK, PART_OUT, PART_ERR, PART_IN
   Byte nameText[4][NUMBUFLEN];
   Byte* name[4];   // not allocated!
   int      ioText[4];
   int      jo_pty;
   int      jo_modifiable[4];
   int      jo_message[4];
   Channel* jo_channel;

   LineNr   jo_in_top;
   LineNr   jo_in_bot;

   void (*finishNativeCb)(void); // native C function to call when the job finishes
   Callback   jo_callback;
   
   void (*outNativeCb)(Arr(Byte)); // callbacks for the "stdout" file descriptor. If the native one is present,
   Callback   jo_out_cb; // only it is called.
   
   void (*errNativeCb)(Arr(Byte)); // callbacks for the "error" file descriptor. If the native one is present,
   Callback   jo_err_cb; // only it is called.
   
   Callback   closeCb;
   Callback   exitCb;
   int      dropNever;
   int      jo_waittime;
   int      jo_timeout;
   int      jo_out_timeout;
   int      jo_err_timeout;
   int      jo_block_write;   // for testing only
   int      part;
   int      id;
   Byte   jo_stoponexit_buf[NUMBUFLEN];
   Byte   *jo_stoponexit;
   Bag* env;   // environment variables
   Byte   cwdText[NUMBUFLEN];
   Byte* currentWorkingDir;

   // when non-zero run the job in a terminal window of this size
   int      jo_term_rows;
   int      jo_term_cols;
   int      vertical;
   int      curPor;
   Book   *jo_bufnr_buf;
   int      jo_hidden;
   int      jo_term_norestore;
   Byte   jo_term_name_buf[NUMBUFLEN];
   Byte   *jo_term_name;
   Byte   jo_term_opencmd_buf[NUMBUFLEN];
   Byte   *jo_term_opencmd;
   int      jo_term_finish;
   Byte   jo_eof_chars_buf[NUMBUFLEN];
   Byte   *jo_eof_chars;
   Byte   jo_term_kill_buf[NUMBUFLEN];
   Byte   *jo_term_kill;
   Byte   jo_term_highlight_buf[NUMBUFLEN];
   Byte   *jo_term_highlight;
   int      jo_tty_type;       // first character of "tty_type"
   Byte   jo_term_api_buf[NUMBUFLEN];
   Byte   *jo_term_api;
} JobOptions;

// Structure used for listeners added with listener_add().
typedef struct Listener Listener;
struct Listener {
   Listener* next;
   int id;
   Callback callback;
};

// Status of a job.  Order matters!
typedef enum {
   JOB_FAILED,
   JOB_STARTED,
   JOB_ENDED,       // detected job done
   JOB_FINISHED,   // job done and cleanup done
} JobStatus;

// Structure to hold info about an async shell Job
struct Job {
   Job   *jv_next;
   Job   *jv_prev;
   pid_t   jv_pid;
   JobStatus jv_status;
   Byte   *jv_tty_in;   // controlling tty input, allocated
   Byte   *jv_tty_out;   // controlling tty output, allocated
   Byte   *jv_stoponexit;   // allocated
   Byte   *jv_termsig;   // allocated
   int      jv_exitval;
   void (*nativeCb)(void); // native C function to call when the job finishes
   Callback   jv_exit_cb;

   Book* inBook;   // book from "in-name"

   int      jv_refcount;   // reference count
   int      jv_copyID;

   Channel* jv_channel;   // channel for I/O, reference counted
   Byte   **jv_argv;   // command line used to start the job
};

//}}}
//{{{garbage collection

// structure used for explicit stack while garbage collecting hash tables
declStruct(HtStack);
struct HtStack {
   EeSet* ht;
   HtStack* prev;
};

// structure used for explicit stack while garbage collecting lists
declStruct(ListStack);
struct ListStack{
   List* list;
   ListStack* prev;
};

//}}}
//{{{timer

// Structure used for iterating over dictionary items. Initialize with dict_iterate_start().
typedef struct {
   Ulong dit_todo;
   EeSetItem* dit_hi;
} DictIterator;

// values for b_syn_spell: what to do with toplevel text
#define SYNSPL_DEFAULT 0   //spell check if @Spell not defined
#define SYNSPL_TOP     1   //spell check toplevel text
#define SYNSPL_NOTOP   2   //don't spell check toplevel text

// values for b_syn_foldlevel: how to compute foldlevel on a line
#define SYNFLD_START   0   // use level of item at start of line
#define SYNFLD_MINIMUM   1   // use lowest local minimum level on line

typedef struct LocationStack LocationStack;


typedef struct Timer Timer;
struct Timer {
   long   id;
   Timer* next;
   Timer* prev;
   ProfTime due;          // when the callback is to be invoked
   char   tr_firing;       // when TRUE callback is being called
   char   tr_paused;       // when TRUE callback is not invoked
   char   tr_keep;       // when TRUE keep timer after it fired
   int      tr_repeat;       // number of times to repeat, -1 forever
   long   tr_interval;       // msec
   Callback   callback;
   int      tr_emsg_count;
};

//}}}
//{{{ Popups

typedef enum {
   POPPOS_BOTLEFT,
   POPPOS_TOPLEFT,
   POPPOS_BOTRIGHT,
   POPPOS_TOPRIGHT,
   POPPOS_CENTER,
   POPPOS_BOTTOM,   // bottom of popup just above the command line
   POPPOS_NONE
} PopupPosition;

typedef enum {
   POPCLOSE_NONE,
   POPCLOSE_BUTTON,
   POPCLOSE_CLICK
} PopupClosing;

# define POPUPWIN_DEFAULT_ZINDEX    50
# define POPUPMENU_ZINDEX      100
# define POPUPWIN_DIALOG_ZINDEX      200
# define POPUPWIN_NOTIFICATION_ZINDEX   300

//}}}
//{{{c-indent

// values set from b_p_cino
typedef struct {
   int level;
   int open_imag;
   int no_brace;
   int first_open;
   int open_extra;
   int close_extra;
   int open_left_imag;
   int jump_label;
   int caseInd;
   int case_code;
   int case_break;
   int param;
   int func_type;
   int comment;
   int in_comment;
   int in_comment2;
   int cpp_baseclass;
   int continuation;
   int unclosed;
   int unclosed2;
   int unclosed_noignore;
   int unclosed_wrapped;
   int unclosed_whiteok;
   int matching_paren;
   int paren_prev;
   int maxparen;
   int maxcomment;
   int scopedecl;
   int scopedecl_code;
   int java;
   int js;
   int keep_case_label;
   int hash_comment;
   int cpp_namespace;
   int if_for_while;
   int cpp_extern_c;
   int pragma;
} CIndent;

//}}}
//{{{Book

// Syntax block data. These are items normally related to a book.  But when using ":ownsyntax",
// a portal may have its own instance.
typedef struct {
   EeSet keywords;      // syntax keywords hash table
   EeSet keywordsIgnoreCase;      // idem, ignore case
   int      b_syn_error;      // TRUE when error occurred in HL
   int      redrawTime;      // TRUE when 'redrawtime' reached
   int      b_syn_ic;      // ignore case for :syn cmds
   int      foldLevel;   // how to compute foldlevel on a line
   int      synSpell;      // SYNSPL_ values
   ArrayList   syntaxPatterns;      // table for syntax patterns
   ArrayList   syntaxClusters;      // table for syntax clusters
   int      spellClusterId;   // @Spell cluster ID or 0
   int      noSpellClusterId;   // @NoSpell cluster ID or 0
   int b_syn_containedin;   //TRUE when there is an item with a "containedin" argument
   int      syncFlags;   //flags about how to sync
   Short   syncHiId;     //group to sync on
   long   b_syn_sync_minlines; //minimal sync lines offset
   long   b_syn_sync_maxlines; //maximal sync lines offset
   long   syncLinebreaks;   //offset for multi-line pattern
   Arr(Byte) lineContinuationPattern;   //line continuation pattern
   RegProg   *lineContinProg;   //line continuation program
   int      lineContinIgnoreCase;   //ignore-case flag for above
   int      b_syn_topgrp;      //for ":syntax include"
   int      b_syn_conceal;     //auto-conceal for :syn cmds
   int      b_syn_folditems;   //number of patterns with the HL_FOLD flag set
   SyntaxState   *array; //the state stack for a number of lines, for the
                         //start of that line (col == 0).  This avoids having to recompute 
                         //the syntax state too often. It is allocated to hold the state for all 
                         //displayed lines, and states for 1 out of about 20 other lines.
   int len;   // number of entries in b_sst_array[]
   SyntaxState   *first; //pointer to first used entry in b_sst_array[] or NULL
   SyntaxState   *firstFree; //pointer to first free entry in b_sst_array[] or NULL
   int      freeCount; // number of free entries in array[]
   LineNr checkAfterLnum; //entries after this lnum need to be checked for validity 
                          //(MAXLNUM means no check needed)
   Short   lastDisplayTick;

   // for spell checking
   ArrayList   b_langp;     //list of pointers to slang_T, see spell.c
   Byte midwordFlags[256];  //flags: is midword char
   CS multibyteMidwordChars;   //multi-byte midword chars
   CS spellFile; 
   CS spellLang;
   CS spellOpts;       // 'spelloptions'
   Byte   b_syn_chartab[32];  // syntax iskeyword option
} SyntaxBlock;

// order must be synchronized with p_buftype_values below
#define BOOK_NORMAL   0  // ordinary book
#define BOOK_NOFILE   1
#define BOOK_NOWRITE  2
#define BOOK_LOCATION 3 // location list book
#define BOOK_HELP     4    // help book - used for help files, won't use a swap file.
#define BOOK_TERMINAL 5
#define BOOK_ACWRITE  6
#define BOOK_PROMPT   7
#define BOOK_POPUP    8

// Must be in sync with p_buftype_values
#define p_buftypeValuesLen 10

EXTERN CS p_buftype_values[] 
#ifdef MAIN_C
// must be in sync with eegl.h:p_buftypeValuesLen
= (CS[]){ SMAP((CS),
   "normal", "nofile", "nowrite", "location", "help", "terminal", "acwrite", "prompt", "popup", 
   "spell"
)}
#endif
;

//Info used in undo.c
typedef struct {
   UndoHeader* oldHead; // pointer to oldest header
   UndoHeader   *newHead; // newest header; may not be valid if b_u_curhead is not NULL
   UndoHeader   *currHead;   // pointer to current header
   int countHeaders;   // current number of headers
   int synced;   // entry lists are synced
   long seqLast;   // last used undo sequence number
   long saveNrLast; // counter for last file write
   long seqCurr;   // uh_seq of header below which we are now
   Tyme timeCurr;   // uh_time of header below which we are now
   long saveNrCurr; // file write nr after which we are now

   // variables for "U" command in undo.c
   UndoLine   line;   // saved line for "U" command
   LineNr   lineLnum; // line number of line in u_line
   ColNr   lineCol;   // optional column number
} Undo;

//{{{options local to a book

typedef enum {

#define OPTIONS_ENUM
#define OPTIONS_DEF_BOOK
#include "defoption.h"
#undef OPTIONS_DEF_BOOK
#undef OPTIONS_ENUM

} BookOption;

EXTERN Sbuf bookStringOptionsG;      //Storage for all the string options

// They're here because their value depends on the type or contents of the file being edited
typedef struct {
#define OPTIONS_FIELDS
#define OPTIONS_DEF_BOOK
#include "defoption.h"
#undef OPTIONS_DEF_BOOK
#undef OPTIONS_FIELDS
   
   
   // flags for use of ":lmap" 
   long b_p_iminsert;    //input mode for insert
   long b_p_imsearch;    //input mode for search
#define B_IMODE_USE_INSERT (-1)	//	Use b_p_iminsert value for search
#define B_IMODE_NONE 0   //Input via none
#define B_IMODE_LMAP 1   //Input via langmap
#define B_IMODE_IM   2   //Input via input method
#define B_IMODE_LAST 2
   
   Boole initialized;   //set when all options were initialized
   ScriptPos scriptLocs[OPTION_BOOK_COUNT]; // script locations for all book-local options
   Sbuf stringOptions;      //Storage for all the string options
} BookLocal;

//}}} end of book-local options

//Book: the structure that holds information about one file
//
//Several Portals can share a single Book
//A book is unallocated if there is no memfile for it.
//A book is new if the associated file has never been loaded yet.

struct Book { //:Book
   MemBuf   mem;      // associated memline (also contains line count)

   Book* next;   // links in list of books
   Book* prev;

   int countPortals;   // nr of portals open into this book

   Unt flags;   // various BF_ flags
   int locked;   // Book is being closed or referenced, don't let autocommands wipe it out
   int lockedSplit;  // Book is being closed, don't allow opening more portals into it

   //fullFileName has the full path of the file (NULL for no name).
   //shortFileName is the name as the user typed it (or NULL).
   //currFileName is the same as shortFileName, unless ":cd" has been done,
   //     then it is the same as fullFileName (NULL for no name).
   CS fullFileName;  //full path file name, allocated
   CS shortFileName; //short file name, allocated, may be equal to full name
   CS currFileName;  //current file name, points to short or full file name
   CS swapName;      //file name of the swap (temporary copy)

   int isDevNumValid; // TRUE when b_dev has a valid number
   dev_t devNum;      // device number
   ino_t inode;      // inode number
   int fiNum;    // book number for this file.
   Byte keyContainer[EE_SIZEOF_INT * 2 + 1]; // key used for buf_hashtab, holds fiNum as hex string
   Text key; // points into keyContainer

   Boole wasModified; // 'modified': Set to TRUE if something in the
                      // file has been changed and not written out.
   DictItem16 changedTick; // holds the b:changedtick value in
                           // changedTick.c.number; incremented for each change, also for undo
#define CHANGEDTICK(buf) ((buf)->changedTick.c.number)

   Long lastChangeTick;   // b:changedtick when TextChanged was last triggered.
   Long lastChangeTickPum; // b:changedtick for TextChangedP
   Long lastChangeTickInsert;   // b:changedtick for TextChangedI

   Boole isBeingSaved;   // Set to TRUE if we are in the middle of saving the book.

   // Changes to a book require updating of the display.  To minimize the
   // work, remember changes made and update everything at once.
   int needsRedraw;   // TRUE when there are changes since the last time the display was updated
   LineNr needsRedrawTop;   // topmost lnum that was changed
   LineNr needsRedrawBott;   // lnum below last changed line, AFTER the change
   long lineCountDiff;  // number of extra book lines inserted; negative when lines were deleted

   PortInfo* portInfos;   // list of last used info for each window

   long modifiedTime;   // last change time of original file
   long modifiedTimeNs;   // nanoseconds of last change time
   long readTime;   // last change time when reading
   long readTimeNs;  // nanoseconds of last read time
   FileSize origSize;   // size of original file in bytes
   int origMode;   // mode of original file
   Tyme lastUsed;   // time when the book was last used; used for eeglinfo

   Pos namedMarks[NMARKS]; // current named marks (mark.c)
   CS fileType;

   // These variables are set when VIsual_active becomes FALSE
   VisualInfo visual;
   Pos lastCursor;   // cursor position when last unloading this book
   Pos lastInsert;   // where Insert mode was left
   Pos lastChange;   // position of last change: '. mark

   // the changelist contains old change positions
   Pos changeList[JUMPLISTSIZE];
   Unt changeListLen;   // number of active entries
   Boole newChange;      // set by u_savecommon()

   //Character table, only used in book.c for @iskeyword
   //32 bytes of 8 bits: 1 bit per character 0-255.
   Byte charsForKeywords[32];

   // Table used for mappings local to a book.
   MapBlock* localMappings[256];

   // First abbreviation local to a book.
   MapBlock* firstAbbr;

   // User commands local to the book.
   ArrayList   userCommands;
   // start and end of an operator, also used for '[ and ']
   Pos opStart;
   Pos opStartOrig;  // used for Insstart_orig
   Pos opEnd;

   int haveReadEeglinfoMarks;   // Have we read eeglinfo marks yet?

   int modifiedWasSet;   // did ":set modified"
   int didFiletype;      // FileType event found
   int keepFiletype;   // value for did_filetype when starting to execute autocommands

   // Set by the apply_autocmds_group function if the given event is equal to
   // EVENT_FILETYPE. Used by the readfile function in order to determine if
   // EVENT_BUFREADPOST triggered the EVENT_FILETYPE.
   //
   // Relying on this value requires one to reset it prior calling apply_autocmds_group().
   int auDidFileType;

   Undo undo;

   int scanned;       // ^N/^P have scanned this book

   Byte kind;         // BOOK_ constants
   BookLocal o;
   
   Boole hasLocationEntry;
   LineNr noEolLnum; //non-zero lnum when last line of next binary
                     //write should not have an end-of-line

   int startEof;    //last line had eof (CTRL-Z) when it was read
   int startEol;    //last line had eol when it was read
   int badChar;     //"++bad=" argument when edit started or 0

   DictItem bookVar; //variable for "b:" Dictionary
   Bag* bVars;      //internal variables, local to book

   Listener* listener;
   List* recordedChanges;
   int hasTextprop;   // TRUE when text props were added
   EeSet* propTypes;   // text property types local to book
   PropType** propArray;   // entries of b_proptypes sorted on tp_id
   ArrayList   textPropText; // stores text for props, index by (-id - 1)

   //When a book is created, it starts without a swap file.  b_may_swap is then set to indicate
   //that a swap file may be opened later.  It is reset if a swap file could not be opened.
   int maySwap;
   int didWarnReadonly; // Set to 1 if user has been warned on first change of a read-only file

   CS promptText;      // set by prompt_setprompt()
   Callback promptCallback;   // set by prompt_setcallback()
   Callback promptInterrupt;   // set by prompt_setinterrupt()
   int promptInsert;   // value for restart_edit when entering a prompt book portal.

   SyntaxBlock syntax;      // Info related to syntax highlighting.  w_s
            // normally points to this, but some portals may use a different SyntaxBlock.

   SignEntry* signList;      // list of placed signs

   int writeToChannel; // TRUE when appended lines are written to a channel.

   int mappedCtrlC; // modes where CTRL-C is mapped
   CS syntaxName;

   Terminal* term;   // When not NULL this book is for a terminal portal.
   int diffFailed;   // internal diff failed for this book
}; // Book

//}}}
//{{{diff mode

// Stuff for diff mode.
# define DB_COUNT 8   // up to eight books can be diff'ed

//Each diffblock defines where a block of lines starts in each of the books and how many lines it
//occupies in that book.  When the lines are missing in the book the df_count[] is zero. This 
//is all counted in book lines. Usually there is always at least one unchanged line in between 
//the diffs as otherwise it would have been included in the diff above or below it. When linematch 
//or diff anchors are used, this is no longer guaranteed, and we may have adjacent diff blocks. In 
//all cases they will not overlap, although it is possible to have multiple 0-count diff blocks at
//the same line. df_lnum[] + df_count[] is the lnum below the change.  When in one book lines 
//have been inserted, in the other book df_lnum[] is the line below the insertion and df_count[]
//is zero.  When appending lines at the end of the book, df_lnum[] is one beyond the end!
//This is using a linked list, because the number of differences is expected to be reasonable 
//small. The list is sorted on lnum. Each diffblock also contains a cached list of inline diff of 
//changes within the block, used for highlighting.
declStruct(DiffBlock);

// Each entry stores a single inline change within a diff block. Line numbers
// are recorded as relative offsets, and columns are byte offsets, not character counts.
// Ranges are [start,end), with the end being exclusive.
typedef struct {
   ColNr dc_start[DB_COUNT];   // byte offset of start of range in the line
   ColNr dc_end[DB_COUNT];   // 1 past byte offset of end of range in line
   int dc_start_lnum_off[DB_COUNT];   // starting line offset
   int dc_end_lnum_off[DB_COUNT];   // end line offset
}DifflineChange;

// Describes a single line's list of inline changes. Use diff_change_parse() to parse this.
typedef struct {
   DifflineChange *changes;
   int num_changes;
   int bufidx;
   int lineoff;
} DiffLine;

typedef enum {
   LINE_STATUS_UNCHANGED,
   LINE_STATUS_CHANGED,
   LINE_STATUS_ADDED_OR_DELETED
} LineDiffStatus;

//}}}
//{{{tab

#define SNAP_HELP_IDX  0
#define SNAP_AUCMD_IDX 1
#define SNAP_COUNT     2

//Tabs point to the top frame of each tab. Note: Most values inside here are NOT valid! Use 
//"curPor", "firstPor", etc. for that. .topframe is always valid and can be compared against 
//topframeG to find the current tab.
declStruct(Tab);
struct Tab {        //:Tab
   Tab* next;       // next tab or NULL
   Frame* topframe;   // topframe for the portals
   Portal* curPor;     // current portal in this tab
   Portal* prevPor;    // previous portal in this tab
   Portal* firstPor;   // first portal in this tab
   Portal* lastPor;    // last portal in this tab
   Portal* firstPopupPort; // first popup portal in this Tab
   NULLABLE Portal* previewPortal; // the preview portal in this Tab
   long old_Rows;    // Rows when tab was left
   long old_Columns; // Columns when tab was left, -1 when calling shell_new_columns() postponed
   int old_coloff;  // Column offset when tab was left
   long  ch_used;       // value of @commheight when frame size was set
   CS localdir;   // absolute path of local directory or NULL
   CS prevdir;   // previous directory

   DiffBlock* first_diff;
   Book* diffbuf[DB_COUNT];
   int diff_invalid;   // list of diffs is outdated
   int diff_update;   // update diffs before redrawing
   Frame *(snapshot[SNAP_COUNT]);  // window layout snapshots
   DictItem tabVar;       // variable for "t:" Dictionary
   Arr(Bag) vars;       // internal variables, local to tab
};

//Structure to cache info for displayed lines in lines[]. Each logical line has one entry.
//The entry tells how the logical line is currently displayed in the portal. This is updated 
//when displaying the portal. When the display is changed (e.g., when clearing the screen) 
//validLines is changed to exclude invalid entries.
//When making changes to the book, wl_valid is reset to indicate wl_size may not reflect what 
//is actually in the book.  When wl_valid is FALSE, the entries can only be used to count the 
//number of displayed lines used. wl_lnum and wl_lastlnum are invalid too.
typedef struct w_line {
   LineNr   bookLnum;   // book line number for logical line
   Short   height;   // height in screen lines
   Boole   isValid;   // whether values are valid for text in book
   Boole   isFolded;   // whether this is a range of folded lines
   LineNr   lastBookLnum;   // last book line number for logical line
} PortLine;

//Portals are kept in a tree of frames. Each frame has a column (FR_COL)
//or row (FR_ROW) layout or is a leaf, which has a portal.
struct Frame { //:Frame
   char   layout;   // FR_LEAF, FR_COL or FR_ROW
   Unt      width;
   Unt      newWidth;   // new width used in win_equal_rec()
   Unt      height;
   Unt      newHeight;   // new height used in win_equal_rec()
   Frame* parent;   // containing frame or NULL
   Frame* next;   // frame right or below in same parent, NULL for last
   Frame* prev;   // frame left or above in same parent, NULL for first
   // child and port are mutually exclusive
   Frame* child;   // first contained frame
   Portal* port;   // window that fills this frame; for a snapshot set to the current portal
};

#define FR_LEAF  0   // frame is a leaf
#define FR_ROW   1   // frame with a row of windows
#define FR_COL   2   // frame with a column of windows

//}}}
//{{{portal
// Structure to store last cursor position and topline.  Used by check_lnums() and reset_lnums().
typedef struct {
   int topLineSave;   // original topline value
   int topLineCorr;   // corrected topline value
   Pos cursor_save;   // original cursor position
   Pos cursor_corr;   // corrected cursor position
} PosSave;

typedef struct {
   int min;       // minimum width for breakindent
   int shift;       // additional shift for breakindent
   int showBreak;       // sbr in 'briopt'
   int list;      // additional indent for lists
   int vcol;       // indent for specific column
} BreakIndent;

// Data about popup kind of portals
typedef struct {
   int flags;      // POPF_ values
   int handled;    // POPUP_HANDLE[0-9] flags
   Arr(Byte)    title;
   PopupPosition   pos;
   int      fixed;      // do not shift popup to fit on screen
   int      propType;  // when not zero: textprop type ID
   Portal* propPort;  // portal to search for textprop
   int propId;    // when not zero: textprop ID
   int zIndex;
   int minHeight;      // "minheight" for popup portal
   int minWidth;       // "minwidth" for popup portal
   int maxHeight;      // "maxheight" for popup portal
   int maxWidth;       // "maxwidth" for popup portal
   int maxWidthOpt;       // maxwidth from option
   int wantLine;       // "line" for popup portal
   int wantCol;        // "col" for popup portal
   int firstLine;      // "firstline" for popup portal
   int wantScrollbar; // when zero don't use a scrollbar
   int hasScrollbar;  // 1 if scrollbar displayed, 0 otherwise
   CS scrollbarHilite; // "scrollbarhighlight"
   CS thumbHilite; // "thumbhighlight"
   int padding[4]; // padding top/right/bot/left
   int border[4];  // border top/right/bot/left
   CS borderHilite[4];  // border highlight
   int borderChar[8];   // border characters

   int leftOff;    // columns left of the screen
   int rightOff;   // columns right of the screen
   Long lastChangedTick; // b:changedtick of popup book when position was computed
   Long propChangedTick; // b:changedtick of book with
                 // w_popup_prop_type when position was computed
   int propTopline; // w_topline of portal with
                  // w_popup_prop_type when position was computed
   LineNr lastCurline; // last known cursor.lnum of portal with "cursorline" set
   Callback closeCb;       // popup close callback
   Callback filterCb;       // popup filter callback
   int filterErrors;    // popup filter error count
   int filterMode;       // mode when filter callback is used

   Portal* curPor;    // close popup if curPor differs
   LineNr lnum;       // close popup if cursor not on this line
   ColNr minCol;    // close popup if cursor before this col
   ColNr maxCol;    // close popup if cursor after this col
   int mouseRow;  // close popup if mouse moves away
   int mouseMinCol;  // close popup if mouse moves away
   int mouseMaxCol;  // close popup if mouse moves away
   PopupClosing   close;     // allow closing the popup with the mouse

   List* mask;         // list of lists for "mask"
   CS maskCells; // cached mask cells
   int maskHeight; // height of w_popup_mask_cells
   int maskWidth;  // width of w_popup_mask_cells
   Timer* timer;     // timer for closing popup portal
} PortalPopup;

typedef struct {
   Pos pos;       // cursor position shown in ruler (ie. status line)
   ColNr virtCol;       // virtcol shown in ruler
   LineNr topLine;       // topline shown in ruler
   LineNr lineCount;    // line count used for ruler
   int topFill;       // topfill shown in ruler
   char isLineEmpty;       // TRUE if ruler shows 0-1 (empty line)
} Ruler; // row, col shown in status line

// Structure which contains all information that belongs to a portal
// All row numbers are relative to the start of the portal, except portalRow.
struct Portal { //:Portal
   int      id;        // unique portal ID
   Book* book;   // book we are a portal into
   Portal* prev;     // link to previous portal
   Portal* next;     // link to next portal
   SyntaxBlock* ownSyntax;  // for :ownsyntax
   int locked;    // don't let autocommands close the portal
   Frame* frame;   // frame containing this portal
   Pos   cursor;       // cursor position in book
   ColNr cursWant; // The column we'd like to be at. This is used to try to stay in the same 
                       // column for up/down cursor motions.
   Boole setCursWant; // If set, then update w_curswant the next time through cursupdate() 
                           // to the current virtual column
   LineNr lastCursorLine;  // where last time 'cursorline' was drawn
                // the next seven are used to update the Visual highlighting
   Byte prevVisualMode;  // last known VIsual_mode
   LineNr prevVisualEnd;  // last known end of visual part
   ColNr oldCursorFcol;  // first column for block visual part
   ColNr oldCursorLcol;  // last column for block visual part
   LineNr oldVisualLnum;  // last known start of visual part
   ColNr oldVisualCol;  // last known start of visual part
   ColNr oldCursWant;   // last known value of Curswant
   LineNr lastCursorLnumRnu; // cursor lnum when 'rnu' was last redrawn

   //"topLine", "leftCol" and "skipcol" specify the offsets for displaying the book.
   LineNr topLine;      //book line number of the line at the top of the portal
   char wasTopLineSet;  //flag set to TRUE when topline is set, e.g. by winrestview()
   LineNr bottomLine;   //number of the line below the bottom of the portal
   int topFill;      //number of filler lines above topLine
   int topFillOld;   //w_topfill at last redraw
   int bottFill;     //TRUE when filler lines are actually below w_topline (at end of file)
   int bottFillOld;  //w_botfill at last redraw
   ColNr leftCol; //screen col of the leftmost character in the portal; used if 'wrap' is off
   ColNr skipCol; //starting screen column for the first line in the portal; used when @wrap is 
                  //on; does not include win_col_off()

   int emptyRowCount;   // number of ~ rows in portal
   int fillerRowCount;  // number of filler rows at the end of the portal

   // six fields that are only used when there is a WinScrolled autocommand
   LineNr lastTopline; // last known value for topLine
   int lastTopFill; // last known value for topfill
   ColNr lastLeftCol;  //last known value for w_leftcol
   ColNr lastSkipCol;  //last known value for w_skipcol
   Unt lastWidth;        //last known value for w_width
   Unt lastHeight;       //last known value for w_height

   // Layout of the portal in the window.
   // May need to add "msg_scrolled" to "portalRow" in rare situations.
   int portalRow;    //first row of portal in window
   Unt height;       //number of rows in portal, excluding status/command line(s)
   int prevPortRow;  //previous portrow used for 'splitkeep'
   Unt prevHeight;   //previous height used for 'splitkeep'
   
   int scroll; // scroll height

   int statusHeight; //number of status lines (0 or 1)
   int portalCol;    //Leftmost column of portal in window
   Unt width;        //Width of portal, excluding separation.
   int vsepWidth;    //Number of separator columns (0 or 1).

   PosSave cursorSaved; // backup of cursor pos and topline
   int needFixCursor;//if TRUE cursor may be invalid

   PortalPopup pup;
   Boole isPreview;   // is this the preview portal of the tab?
   Unt flags;        // WFLAG_ flags

///////////////////////////////////////////////////////////////////
// === start of cached values ===
///////////////////////////////////////////////////////////////////
   // Recomputing is minimized by storing the result of computations. Use functions in draw.c to 
   // check if they are valid and to update. cacheState is a bitfield of flags, which indicate if 
   // specific values are valid or need to be recomputed. See draw.c for values.
   int cacheState;
   Pos lastKnownCursor;      // last known position of cursor, used to adjust cacheState
   ColNr lastKnownLeftCol; // last known w_leftcol
   ColNr lastKnownSkipCol; // last known w_skipcol

   // w_cline_height is the number of physical lines taken by the book line
   // that the cursor is on.  We use this to avoid extra calls to plines().
   Unt cursorLineHeight; // current size of cursor line
   int isCursorLineFolded; // cursor line is folded
   int cursorLineRow;    // starting row of the cursor line
   ColNr virtCol;  // column number of the cursor in the book line, as opposed to the column 
                     // number we're at on the screen. This makes a difference on lines which span
                     // more than one screen line or when w_leftcol is non-zero

   ColNr virtColFirstChar; // offset for w_virtcol when there are virtual text properties 
                               // above the line
#define WFLAG_WCOL_OFF_ADDED 1 // popup border and padding were added to cursorCol
#define WFLAG_WROW_OFF_ADDED 2 // popup border and padding were added to cursorRow
   // The cursor position in the portal. This is related to positions in 
   // the portal, not in the display or book, thus cursorRow is relative to portalRow.
   int cursorRow, cursorCol; // cursor position in portal

   // Info about the lines currently in the portal is remembered to avoid recomputing it every 
   // time.  The allocated size of lines[] is visibleRowsG. Only the validLines entries are 
   // actually valid. When the display is up-to-date lines[0].wl_lnum is equal to topLine
   // and lines[validLines - 1].wl_lnum is equal to bottomLine.
   // Between changing text and updating the display lines[] represents what is currently 
   // displayed.  wl_valid is reset to indicate this. This is used for efficient redrawing.
   int validLines;     // number of valid entries
   Arr(PortLine) lines;

   ArrayList folds;  // array of nested folds
   Boole foldManual; // when TRUE: some folds are opened/closed manually
   Boole foldNeedsRecomputation; // when TRUE: folding needs to be recomputed
   int numberColWidth;      // width of 'number' and 'relativenumber' column being used
   TermCellColor termHiliteGroupName;    // cache for term color of a portal's "hiliteGroupName"
///////////////////////////////////////////////////////////////////
// === end of cached values ===
///////////////////////////////////////////////////////////////////

   Unt redrawType;   // type of redraw to be performed on portal
   int rowsToUpdate;    // number of portal lines to update when w_redr_type is UPD_REDRAW_TOP
   LineNr redrawTop;  // when != 0: first line needing redraw
   LineNr redrawBott;  // when != 0: last line needing redraw
   Boole statusLineNeedsRedraw; // if TRUE status line must be redrawn

   // remember what is shown in the ruler for this portal (if 'ruler' set)
   Ruler ruler;
   int altFnum;       // alternate file (for # and CTRL-^)

   EeArgList* argList;       // pointer to arglist for this window
   int argListInd;       // current index in argument list (can be out of range!)
   Boole isNotValid;  // editing another file than w_arg_idx

   CS localDir;       // absolute path of local directory or NULL
   CS prevdir;       // previous directory

   //Options local to a portal.
   //They are local because they influence the layout of the portal or depend on the portal layout.
   PortLocal o;

   BreakIndent breakIndent;
   long scbindPos;
   DictItem wVar;          // variable for "w:" Dictionary
   Bag* internalVars;     // internal variables, local to portal

   // The prev_pcmark field is used to check whether we really did jump to
   // a new line after setting the w_pcmark.  If not, then we revert to using the previous w_pcmark.
   Pos prevContextMark;   // previous context mark
   Pos prevPrevContextMark;   // previous w_pcmark

   // the jumplist contains old cursor positions
   FileMarkExt   jumpList[JUMPLISTSIZE];
   int jumpListLen;      // number of active entries
   int jumpListInd;      // current position
   int changeListInd;   // current position in b_changelist
   MatchItem* firstMatch;      // head of match list
   int nextMatchId;   // next match ID

   // the tagstack grows from 0 upwards:
   // entry 0: older
   // entry 1: newer
   // entry 2: newest
   Taggy   tagStack[TAGSTACKSIZE];   // the tag stack
   Unt      tagStackInd;          // idx just below active entry
   Unt      tagStackLen;          // number of tags on stack

   // fraction is the fractional row of the cursor within the window, from
   // 0 at the top row to FRACTION_MULT at the last row.
   // prev_fraction_row was the actual cursor row when fraction was last calculated.
   int      fraction;
   int      prevFraction;

   LineNr   lineCountSaved;   // line count when ml_nrwidth_width was computed.
   long   numWidthCached;      // 'numberwidth' option cached
   int      charsInLineCount;   // nr of chars to print line count.

   // Location list reference used in the portal into a location list.
   // In an ordinary portal, locationStackRef is NULL.
   LocationStack* locationStackRef;
};

// Arguments for operators.
typedef struct Operator {
   Unt opTy;   // current pending operator type
   int regname;   // register to use for the operator
   int motion_type;   // type of the current cursor motion
   int motion_force;   // force motion type: 'v', 'V' or CTRL-V
   int end_adjusted;   // backed up b_op_end one char (only used by do_format())
   Pos start;      // start of the operator
   Pos end;      // end of the operator
   Pos cursor_start;   // cursor position before motion for "gw"
   long line_count;   // number of lines from op_start to op_end (inclusive)
   int empty;      // op_start and op_end the same (only used by do_change())
   Boole use_reg_one;   // TRUE if delete uses reg 1 even when not linewise
   Boole inclusive;   // TRUE if char motion is inclusive (only valid when motion_type is MCHAR)
   Boole is_VIsual;   // operator on Visual area
   Boole block_mode;   // current operator is Visual block mode
   ColNr start_vcol;   // start col for block mode operator
   ColNr end_vcol;   // end col for block mode operator
   long  prev_opcount;   // ca.opcount saved for K_CURSORHOLD
   long  prev_count0;   // ca.count0 saved for K_CURSORHOLD
   int excludeTrailingWhitespace;   // exclude trailing whitespace for yank of a block
} Operator;

// Arguments for Normal mode commands.
typedef struct {
   Arr(Operator) oper; //Operators
   Unt prechar;    //prefix character (optional, always 'g')
   Unt cmdchar;    //command character
   Unt nchar;      //next command character (optional)
   Unt ncharC1;    //first composing character (optional)
   Unt ncharC2;    //second composing character (optional)
   Unt extra_char; //yet another character (optional)
   long opcount;   //count before an operator
   long count0;    //count before command, default 0
   long count1;    //count before command, default 1
   int arg;        //extra argument from actions[]
   int retval;     //return: CA_* values
   CS searchbuf;//return: pointer to search pattern or NULL
} ActionArg;

//}}}
//{{{location lists

// Specific action on a location list
typedef enum {
   LL_ACTION_INVALID, // placeholder for ill-defined strings
   LL_ACTION_ADD, // add entry to location list
   LL_ACTION_REPLACE, 
   LL_ACTION_UPDATE,
   LL_ACTION_NEW, // create new location list
   LL_ACTION_FREE
} LocListAction;

//}}}
//{{{cursor

// values for retval:
#define CA_COMMAND_BUSY   1 //skip restarting edit() once
#define CA_NO_ADJ_OP_END  2 //don't adjust operator end

#define CURSOR_BLOCK      0
#define CURSOR_UNDERSCORE 1
#define CURSOR_BAR        2

//}}}
//{{{MainParams

// Struct to save values in before executing autocommands for a book that is not current.
typedef struct {
   int use_autoCommPort_idx;  // index in aucmd_win[] if >= 0
   int save_curPor_id;       // ID of saved curPor
   int new_curPor_id;       // ID of new curPor
   int save_prevPor_id;    // ID of saved prevPor
   BookRef   newCurBook;       // new curBook
   CS localdir;       // saved value of tp_localdir
   CS globaldir;       // saved value of globaldir
   int save_VIsual_active; // saved VIsual_active
   int save_State;       // saved State
   int save_prompt_insert; // saved b_prompt_insert
} AutocommSave;

// Used for popup menu items.
typedef struct {
   CS pum_text;      // main menu text
   CS pum_kind;      // extra kind text (may be truncated)
   CS pum_extra;      // extra menu text (may be truncated)
   CS pum_info;      // extra info
   int pum_cpt_source_idx;   // index of completion source in 'cpt'
   Decoration abbreviationDeco;   // hilite decoration for abbr
   Decoration kindDeco;   // hilite decoration for kind
} PopupItem;

declStruct(FileSearchCtx);

// Structure used for get_tagfname().
typedef struct {
   CS tn_tags;   // value of @tags when starting
   CS tn_np;      // current position in tn_tags
   int tn_did_filefind_init;
   int tn_hf_idx;
   FileSearchCtx* searchCtx;
} TagName;

// types for expressions.
typedef enum {
   EXPR_UNKNOWN = 0,
   EXPR_EQUAL,      // ==
   EXPR_NEQUAL,   // !=
   EXPR_GREATER,   // >
   EXPR_GEQUAL,   // >=
   EXPR_SMALLER,   // <
   EXPR_SEQUAL,   // <=
   EXPR_MATCH,      // =~
   EXPR_NOMATCH,  // !~
   EXPR_IS,       // is
   EXPR_ISNOT,    // isnot
   // used with ISN_OPNR
   EXPR_ADD,      // +
   EXPR_SUB,      // -
   EXPR_MULT,     // *
   EXPR_DIV,      // /
   EXPR_REM,      // %
   EXPR_LSHIFT,   // <<
   EXPR_RSHIFT,   // >>
   // used with ISN_ADDLIST
   EXPR_COPY,     // create new list
   EXPR_APPEND    // append to first list
} ExprType;

// Structure used for reading in json_decode().
declStruct(JsReader);
struct JsReader {
   Byte* js_buf;   // text to be decoded
   Byte* js_end;   // ZERO in js_buf
   int js_used;    // bytes used from js_buf
   int (*js_fill)(JsReader *);
            //function to fill the buffer or NULL; returns TRUE when the buffer was filled
   void* js_cookie;   // can be used by js_fill
   int      js_cookie_arg;   // can be used by js_fill
};

// Maximum number of commands from + or -c arguments.
#define MAX_ARG_CMDS 10

// values for "portalLayout"
#define WIN_HOR     1       // "-o" horizontally split portals
#define WIN_VER     2       // "-O" vertically split portals
#define WIN_TABS    3       // "-p" portals on tabs

// Struct for various parameters passed between main() and other functions.
typedef struct {
   int      argc;
   Arr(Arr(char)) argv;

   CS fname;         // first file to edit

   CS altInitFile;      // alternative init file name from -u argument
   int      clean;         // --clean argument

   int      n_commands;           // no. of commands from + or -c
   CS commands[MAX_ARG_CMDS];     // commands from + or -c arg.
   Byte cmds_tofree[MAX_ARG_CMDS];   // commands that need free()
   int      n_pre_commands;           // no. of commands from --cmd
   CS pre_commands[MAX_ARG_CMDS]; // commands from --cmd argument

   int      edit_type;      // type of editing to do
   CS tagname;      // tag from -t argument
   CS use_ef;      // 'errorfile' from -q argument

   int want_full_screen;
   int not_a_term;      // no warning for missing term?
   int tty_fail;      // exit if not a tty
   CS term;         // specified terminal name
   int no_swap_file;      // "-n" argument used
   int use_debug_break_level;
   Unt portalCount;      // number of portals to use
   int portalLayout;     // 0, WIN_HOR, WIN_VER or WIN_TABS

   int serverArg;      // TRUE when argument for a server
   CS serverName_arg;  // cmdline arg for server name
   CS serverStr;       // remote server command
   CS servername;      // allocated name for our server
   int diff_mode;      // start with 'diff' set
} MainParams;

//Lvalue. Structure returned by get_lval() and used by set_var_lval(). For a plain name:
//  "name"       points to the variable name.
//  "exp_name"  is NULL.
//  "tv"       is NULL
//For a magic braces name:
//  "name"       points to the expanded variable name.
//  "exp_name"  is non-NULL, to be freed later.
//  "tv"       is NULL
//For an index in a list:
//  "name"       points to the (expanded) variable name.
//  "exp_name"  NULL or non-NULL, to be freed later.
//  "tv"       points to the (first) list item value
//  "li"       points to the (first) list item
//  "range", "n1", "n2" and "empty2" indicate what items are used.
//For an existing Bag item:
//  "name"       points to the (expanded) variable name.
//  "exp_name"  NULL or non-NULL, to be freed later.
//  "tv"       points to the dict item value
//  "newkey"    is NULL
//For a non-existing Bag item:
//  "name"       points to the (expanded) variable name.
//  "exp_name"  NULL or non-NULL, to be freed later.
//  "tv"       points to the Dictionary Var
//  "newkey"    is the key for the new item.
typedef struct {
   Text name;   // start of variable name (can be NULL)
   Text expandedName;   // NULL or expanded name in allocated memory.
   Var* var;   // Typeval of item being used. 
               // If "newkey" isn't NULL, it's the Bag to which to add the item.
   ListItem   *ll_li;      // The list item or NULL.
   List   *ll_list;   // The list or NULL.
   int      ll_range;   // TRUE when a [i:j] range was used
   int      ll_empty2;   // Second index is empty: [i:]
   long   ll_n1;      // First index for list
   long   ll_n2;      // Second index for list range
   Bag* bag;   // The Bag or NULL
   DictItem   *ll_di;      // The dictitem or NULL
   Text newKey;   // New key for Bag in alloc. mem or NULL.
   Blob   *ll_blob;   // The Blob or NULL
   UserFunc   *ll_ufunc;   // The function or NULL
   int      isRoot;   //TRUE if ll_tv is the lval_root, like a plain object/class. ll_tv is variable
} Lval;

// Structure used to save the current state.  Used when executing Normal mode
// commands while in any other mode.
typedef struct {
   int save_msg_scroll;
   int save_restart_edit;
   int save_msg_didout;
   int save_State;
   int save_finish_op;
   int save_opcount;
   int save_reg_executing;
   int save_pending_end_reg_executing;
   TypeaheadSave   tabuf;
} SaveState;

typedef struct {
   Long prevCount;
   Long count;
   Long count1;
} EeglVarsSave;

// Scope for changing directory
typedef enum {
   CDSCOPE_GLOBAL,   // :cd
   CDSCOPE_TABPAGE,   // :tcd
   CDSCOPE_WINDOW   // :lcd
} CdScopeKind;

// argument for mouse_find_win()
typedef enum {
   IGNORE_POPUP,   // only check non-popup windows
   FIND_POPUP,      // also find popup windows
   FAIL_POPUP      // return NULL if mouse on popup window
} MouseFindKind;

// Symbolic names for some registers.
#define DELETION_REGISTER   36
#define STAR_REGISTER      37
#define PLUS_REGISTER   STAR_REGISTER       // there is only one
#define TILDE_REGISTER      (PLUS_REGISTER + 1)

#define NUM_REGISTERS      (TILDE_REGISTER + 1)

// Used by block_prep, op_delete and op_yank for blockwise operators.
// Also op_change, op_shift, op_insert, op_replace - AKelly
typedef struct BlockDef {
   int      startspaces;   // 'extra' cols before first char
   int      endspaces;   // 'extra' cols after last char
   int      textlen;   // chars in block
   Byte   *textstart;   // pointer to 1st char (partially) in block
   ColNr   textcol;   // index of chars (partially) in block
   ColNr   start_vcol;   // start col of 1st char wholly inside block
   ColNr   end_vcol;   // start col of 1st char wholly after block
   int      is_short;   // TRUE if line is too short to fit in block
   int      is_MAX;      // TRUE if curswant==MAXCOL when starting
   int      is_oneChar;   // TRUE if block within one character
   int      pre_whitesp;   // screen cols of ws before block
   int      pre_whitesp_c;   // chars of ws before block
   ColNr   end_char_vcols;   // number of vcols of post-block char
   ColNr   start_char_vcols; // number of vcols of pre-block char
} BlockDef;

// Each yank register has an array of pointers to lines.
typedef struct {
   Arr(Text) y_array;
   LineNr   y_size;      // number of lines in y_array
   Byte   y_type;      // MLINE, MCHAR or MBLOCK
   ColNr   y_width;   // only set if y_type == MBLOCK
   Tyme   y_time_set;
} YankReg;

// Optional extra arguments for searchit().
typedef struct {
   LineNr sa_stop_lnum;   // stop after this line number when != 0
   long sa_tm;      // timeout limit or zero
   int sa_timed_out;   // set when timed out
   int sa_wrapped;   // search wrapped around
} SearchitArg;

// The offset for a search command. Note: only spats[0].off is really used
typedef struct {
   int dir;      // search direction, '/' or '?'
   int line;      // search has line offset
   int end;      // search set cursor at end
   long   off;      // line or char offset
} SearchOffset;

// A search pattern and its attributes are stored in a spat struct
typedef struct {
   Text pat;// the pattern (in allocated memory) or NULL
   int magic;   // magicness of the pattern
   Boole no_scs;   // no smartcase for this pattern
   SearchOffset off;
} SearchPattern;

#define WRITEBUFSIZE   8192   // size of normal write buffer

// Magicness of a pattern, used by regexp code.
// The order and values matter:
//  magic <= MAGIC_OFF includes MAGIC_NONE
//  magic >= MAGIC_ON  includes MAGIC_ALL
typedef enum {
   MAGIC_NONE = 1,      // "\V" very unmagic
   MAGIC_OFF = 2,      // "\M" or 'magic' off
   MAGIC_ON = 3,      // "\m" or 'magic'
   MAGIC_ALL = 4      // "\v" very magic
} Magic;

#define WHERE_INIT {NULL, 0, WT_UNKNOWN}

// Struct passed to get_v_event() and restore_v_event().
typedef struct {
   int sve_did_save;
   EeSet   sve_hashtab;
} SaveVEvent;

// Enum used by filter(), map(), mapnew() and foreach()
typedef enum {
   FILTERMAP_FILTER,
   FILTERMAP_MAP,
   FILTERMAP_MAPNEW,
   FILTERMAP_FOREACH
} FilterMap;

// Structure used by switch_win() to pass values to restore_win()
typedef struct {
   Portal* curPor;
   Tab* currTab;
   int      samePortal;       // VIsual_active was not reset
   int      isVisualActive;
} SwitchPort;

// Argument for lbr_chartabsize().
typedef struct {
   Portal* cts_win;
   Byte* cts_line;      // start of the line
   Byte* cts_ptr;      // current position in line
   int      cts_bri_size;      // cached size of 'breakindent', or -1 if not computed yet
   int      cts_text_prop_count;   // number of text props; when zero cts_text_props is not used
   TextProp* cts_text_props;   // text props (allocated)
   char   cts_has_prop_with_text;   // TRUE if a property inserts text
   int cts_cur_text_width;   // width of current inserted text
   int cts_prop_lines;      // nr of properties above or below
   int cts_first_char;      // width text props above the line
   int cts_with_trailing;   // include size of trailing props with last character
   int cts_start_incl;      // prop has true "start_incl" arg
   int cts_vcol;      // virtual column at current position
   int cts_max_head_vcol;   // see win_lbr_chartabsize()
} CharTableSize;

typedef enum {
   SET_GLOBAL,
   SET_LOCAL
} SetScope;

//Argument for the callback function (OptionPostProcessor) invoked after an
//option value is modified.
typedef struct {
   //Reference to the option variable.
   OptionRef ref;
   SetScope setScope;

   //old value of the option (can be a string, number or a boolean)
   OptionValue oldVal;
    
   //new value of the option (can be a string, number or a boolean)
   OptionValue newVal;

   //Option value was checked to be safe, no need to set P_INSECURE
   //Used for the @keymap, @filetype and @syntax options.
   Boole wasValueChecked;
   //Option value changed.  Used for the @filetype and @syntax options.
   Boole wasValueChanged;
   Sbuf* buf; // Buffer for all the string options
   ErrBuilder errb;
} OptionChange;

typedef enum {
   POPUP_NORMAL,
   POPUP_ATCURSOR,
   POPUP_BEVAL,
   POPUP_NOTIFICATION,
   POPUP_MESSAGE_WIN,   // similar to POPUP_NOTIFICATION
   POPUP_DIALOG,
   POPUP_MENU,
   POPUP_PREVIEW,   // preview window
   POPUP_INFO      // popup menu info
} PopupKind;

// Defined as signed, to return -1 on error
typedef struct {
   int cs_xpixel;
   int cs_ypixel;
} CellSize;

typedef enum {
   WAYLAND_SELECTION_NONE       = 0x0,
   WAYLAND_SELECTION_REGULAR    = 0x1,
   WAYLAND_SELECTION_PRIMARY    = 0x2,
} WaylandSelection;

// Callback when another client wants us to send data to them
typedef void (*wayland_cb_send_data_func_T)(
   const char *mime_type,
   int fd,
   WaylandSelection type
);

// Callback when the selection is lost (data source object overwritten)
typedef void (*wayland_cb_selection_cancelled_func_T)(WaylandSelection type);

//}}}
//{{{spelling

declStruct(SpellLang);
declStruct(SpellTab);

// Values for "what" argument of spell_add_word()
#define SPELL_ADD_GOOD   0
#define SPELL_ADD_BAD   1
#define SPELL_ADD_RARE   2

//}}}
//{{{balloons

typedef enum {
   ShS_NEUTRAL,         // nothing showing or pending
   ShS_PENDING,         // data requested from debugger
   ShS_UPDATE_PENDING,         // switching information displayed
   ShS_SHOWING            // the balloon is being displayed
} BeState;

typedef struct BalloonEvalStruct {
   int         ts;      // tab size for this book
   Byte      *msg;      // allocated: current text
} BalloonEval;

#define EVAL_OFFSET_X 15 // displacement of balloon topleft corner from pointer
#define EVAL_OFFSET_Y 10

//}}}
//}}}

// enumeration of alloc IDs.
// Used by test_alloc_fail() to test memory allocation failures.
// Each entry must be on exactly one line, GetAllocId() depends on that.
typedef enum {
   aid_none = 0,
   aid_ll_dirname_start,
   aid_ll_dirname_now,
   aid_ll_namebuf,
   aid_ll_module,
   aid_ll_errmsg,
   aid_ll_pattern,
   aid_ll_efm_fmtstr,
   aid_ll_efm_fmtpart,
   aid_ll_title,
   aid_ll_mef_name,
   aid_ll_line,
   aid_ll_info,
   aid_ll_dirstack,
   aid_ll_multiline_pfx,
   aid_ll_makecmd,
   aid_ll_linebuf,
   aid_tagstack_items,
   aid_tagstack_from,
   aid_tagstack_details,
   aid_sign_getdefined,
   aid_sign_getplaced,
   aid_sign_define_by_name,
   aid_sign_getlist,
   aid_sign_getplaced_dict,
   aid_sign_getplaced_list,
   aid_insert_sign,
   aid_sign_getinfo,
   aid_newbuf_bvars,
   aid_newwin_wvars,
   aid_newtabpage_tvars,
   aid_blob_alloc,
   aid_get_func,
   aid_last
} AllocId;

// Values for "do_profiling".
#define PROF_NONE    0   // profiling not started
#define PROF_YES     1   // profiling busy
#define PROF_PAUSED  2   // profiling paused

// Codes for mouse button events in lower three bits:
#define MOUSE_LEFT    0x00
#define MOUSE_MIDDLE  0x01
#define MOUSE_RIGHT   0x02
#define MOUSE_RELEASE 0x03

#define MOUSE_X1  0x300 // Mouse-button X1 (6th)
#define MOUSE_X2  0x400 // Mouse-button X2

#define MOUSE_MOVE 0x700    // report mouse moved

// 0x20 is reserved by xterm
#define MOUSE_DRAG   (0x40 | MOUSE_RELEASE)

// flags for jump_to_mouse()
#define MOUSE_FOCUS        0x01 //need to stay in this window
#define MOUSE_MAY_VIS      0x02 //may start Visual mode
#define MOUSE_DID_MOVE     0x04 //only act when mouse has moved
#define MOUSE_SETPOS       0x08 //only set current mouse position
#define MOUSE_MAY_STOP_VIS 0x10 //may stop Visual mode
#define MOUSE_RELEASED     0x20 //button was released

// Defines for Eegl variables. These must match eval.c:eeglVars[]!
#define VV_COUNT         0
#define VV_COUNT1        1
#define VV_PREVCOUNT     2
#define VV_ERRMSG        3
#define VV_WARNINGMSG    4
#define VV_STATUSMSG     5
#define VV_SHELL_ERROR   6
#define VV_THIS_SESSION  7
#define VV_VERSION       8
#define VV_LNUM          9
#define VV_TERMRESPONSE 10
#define VV_FNAME        11
#define VV_LANG         12
#define VV_LC_TIME      13
#define VV_CTYPE        14
#define VV_FNAME_IN     15
#define VV_FNAME_OUT    16
#define VV_FNAME_NEW    17
#define VV_FNAME_DIFF   18
#define VV_CMDARG       19
#define VV_FOLDSTART    20
#define VV_FOLDEND      21
#define VV_FOLDDASHES   22
#define VV_FOLDLEVEL    23
#define VV_PROGNAME     24
#define VV_SEND_SERVER  25
#define VV_DYING        26
#define VV_EXCEPTION    27
#define VV_THROWPOINT   28
#define VV_REG          29
#define VV_CMDBANG      30
#define VV_INSERTMODE   31
#define VV_VAL          32
#define VV_KEY          33
#define VV_PROFILING    34
#define VV_FCS_REASON   35
#define VV_FCS_CHOICE   36
#define VV_BEVAL_BUFNR  37
#define VV_BEVAL_WINNR  38
#define VV_BEVAL_WINID  39
#define VV_BEVAL_LNUM   40
#define VV_BEVAL_COL    41
#define VV_BEVAL_TEXT   42
#define VV_SCROLLSTART  43
#define VV_SWAPNAME     44
#define VV_SWAPCHOICE   45
#define VV_SWAPCOMMAND  46
#define VV_CHAR         47
#define VV_MOUSE_WIN    48
#define VV_MOUSE_WINID  49
#define VV_MOUSE_LNUM   50
#define VV_MOUSE_COL    51
#define VV_OP           52
#define VV_SEARCHFORWARD 53
#define VV_HLSEARCH     54
#define VV_OLDFILES     55
#define VV_WINDOWID     56
#define VV_PROGPATH     57
#define VV_COMPLETED_ITEM 58
#define VV_ERRORS       65
#define VV_FALSE        66
#define VV_TRUE         67
#define VV_NONE         68
#define VV_NULL         69
#define VV_NUMBERMAX    70
#define VV_NUMBERMIN    71
#define VV_NUMBERSIZE   72
#define VV_EE_DID_ENTER 73
#define VV_TESTING      74
#define VV_TYPE_NUMBER  75
#define VV_TYPE_STRING  76
#define VV_TYPE_FUNC    77
#define VV_TYPE_LIST    78
#define VV_TYPE_DICT    79
#define VV_TYPE_FLOAT   80
#define VV_TYPE_BOOL    81
#define VV_TYPE_NONE    82
#define VV_TYPE_JOB     83
#define VV_TYPE_CHANNEL 84
#define VV_TYPE_BLOB    85
#define VV_TERMRFGRESP  86
#define VV_TERMRBGRESP  87
#define VV_TERMU7RESP   88
#define VV_TERMSTYLERESP 89
#define VV_TERMBLINKRESP 90
#define VV_EVENT        91
#define VV_VERSIONLONG  92
#define VV_ECHOSPACE    93
#define VV_ARGV         94
#define VV_COLLATE      95
#define VV_EXITING      96
#define VV_COLORNAMES   97
#define VV_SIZEOFINT    98
#define VV_SIZEOFLONG   99
#define VV_SIZEOFPOINTER 100
#define VV_MAXCOL      101
#define VV_TYPE_ENUM   102
#define VV_TYPE_ENUMVALUE  103
#define VV_STACKTRACE      104
#define VV_WAYLAND_DISPLAY 105
#define EV_LEN         107 // number of v: vars

// used for v_number in VAR_BOOL and VAR_SPECIAL
#define VVAL_FALSE  0L   // VAR_BOOL
#define VVAL_TRUE   1L   // VAR_BOOL
#define VVAL_NONE   2L   // VAR_SPECIAL
#define VVAL_NULL   3L   // VAR_SPECIAL

// There are five history tables:
#define HIST_CMD     0   // colon commands
#define HIST_SEARCH  1   // search commands
#define HIST_EXPR    2   // expressions (from entering = register)
#define HIST_INPUT   3   // input() lines
#define HIST_DEBUG   4   // debug commands
#define HIST_COUNT   5   // number of history tables

// Selection states for modeless selection
#define SELECT_CLEARED     0
#define SELECT_IN_PROGRESS 1
#define SELECT_DONE        2


// Info about selected text
typedef struct {
   Boole owned;      //Flag: do we own the selection?
   Pos start;      //Start of selected area
   Pos end;        //End of selected area
   Unt vmode;      //Visual mode character

   // Fields for selection that doesn't use Visual mode
   Short origin_row;
   Short origin_start_col;
   Short origin_end_col;
   Short word_start_col;
   Short word_end_col;
   // limits for selection inside a popup window
   Short min_col;
   Short max_col;
   Short min_row;
   Short max_row;

   Pos prev;      // Previous position
   Short state;   // Current selection state
   Short mode;    // Select by char, word, or line.
} ClipBoard;


#if (defined(__GNUC__) || defined(__clang__))
# define ATTRIBUTE_FORMAT_PRINTF(fmt_idx, arg_idx) \
    __attribute__((format(printf, fmt_idx, arg_idx)))
#else
# define ATTRIBUTE_FORMAT_PRINTF(fmt_idx, arg_idx)
#endif

#if defined(__GNUC__) || defined(__clang__)
# define likely(x)   __builtin_expect((x), 1)
# define unlikely(x)   __builtin_expect((x), 0)
# define ATTRIBUTE_COLD   __attribute__((cold))
#else
# define unlikely(x)   (x)
# define likely(x)   (x)
# define ATTRIBUTE_COLD
#endif

typedef enum {
   ASSERT_EQUAL,
   ASSERT_NOTEQUAL,
   ASSERT_MATCH,
   ASSERT_NOTMATCH,
   ASSERT_FAILS,
   ASSERT_OTHER
} AssertKind;

// Mode for bracketed_paste().
typedef enum {
   PASTE_INSERT,   // insert mode
   PASTE_CMDLINE,   // command line
   PASTE_EX,      // ex mode line
   PASTE_ONE_CHAR   // return first character
} PasteMode;

// Argument for flush_buffers().
typedef enum {
   FLUSH_MINIMAL,
   FLUSH_TYPEAHEAD,   // flush current typebuf contents
   FLUSH_INPUT      // flush typebuf and inchar() input
} FlushBuffers;

// Argument for prepare_tagpreview()
typedef enum {
   USEPOPUP_NONE,
   USEPOPUP_NORMAL,   // use info popup
   USEPOPUP_HIDDEN   // use info popup initially hidden
} UsePopup;

// Argument for estack_sfile().
typedef enum {
   ESTACK_NONE,
   ESTACK_SFILE,
   ESTACK_STACK,
   ESTACK_SCRIPT,
} EstackArg;

// errors for when calling a function
typedef enum {
   FCERR_NONE,      // no error
   FCERR_UNKNOWN,   // unknown function
   FCERR_TOOMANY,   // too many arguments
   FCERR_TOOFEW,   // too few arguments
   FCERR_SCRIPT,   // missing script context
   FCERR_DICT,      // missing dict
   FCERR_OTHER,   // another kind of error
   FCERR_DELETED,   // function was deleted
   FCERR_NOTMETHOD,   // function cannot be used as a method
   FCERR_FAILED,   // error while executing the function
} FnError;

//Array indexes used for cp_text[].
typedef enum {
   CPT_ABBR,      // "abbr"
   CPT_KIND,      // "kind"
   CPT_MENU,      // "menu"
   CPT_INFO,      // "info"
   CPT_COUNT,      // Number of entries
} CpItem;

//{{{termdefs

//This list contains the defines for the machine dependent escape sequences that the editor needs 
//to perform various operations. All of the sequences here are optional, except "cm" (cursor motion)


// Index of the terminfo codes, with their capability names and example values.
#define KS_NAME   0 //name of this terminal entry. foot
#define KS_CE     1 //clear to end of line. el. \e[K
#define KS_AL     2 //add new blank line. il1. \e[L
#define KS_CAL    3 //add number of blank lines. il. \e[%p1%dL
#define KS_DL     4 //delete line. dl1. \e[M
#define KS_CDL    5 //delete number of lines. dl. \e[%p1%dM
#define KS_CS     6 //scroll region. csr. \e[%i%p1%d;%p2%dr
#define KS_CL     7 //clear screen. clear. \e[H\e[2J
#define KS_CD     8 //clear to end of display. ed. \e [J
#define KS_UT     9 //clearing uses current background color. y???
#define KS_DA    10 //text may be scrolled down from up. Empty
#define KS_DB    11 //text may be scrolled up from down. Empty
#define KS_VI    12 //cursor invisible. civis. \e[?25l
#define KS_VE    13 //cursor visible. cnorm. \e[?12l\e[?25h
#define KS_VS    14 //cursor very visible (blink). cvvis. \e[?12;25h
#define KS_CVS   15 //cursor normally visible (no blink). Empty
#define KS_CSH   16 //cursor shape. Empty.
#define KS_CRC   17 //request cursor blinking. Empty
#define KS_CRS   18 //request cursor style. Empty
#define KS_ME    19 //normal mode. \e[0m
#define KS_MR    20 //reverse mode. rev. \e[7m
#define KS_MD    21 //bold mode. bold. \e[1m
#define KS_SE    22 //normal mode. rmso. \e[27m
#define KS_SO    23 //standout mode. rev. \e[7m. AGAIN???
#define KS_CZH   24 //italic mode start. sitm. \e[3m
#define KS_CZR   25 //italic mode end. ritm. \e[23m
#define KS_UE    26 //exit underscore (underline) mode. rmul. \e[24m
#define KS_US    27 //underscore (underline) mode. smul. \e[4m
#define KS_UCE   28 //exit undercurl mode. EMPTY
#define KS_UCS   29 //undercurl mode. Cs. `\e]12;%p1%s\e\`
#define KS_USS   30 //double underline mode. EMPTY
#define KS_DS    31 //dotted underline mode. dsl. `\e]2;\e\`
#define KS_MS    32 //save to move cur in reverse mode. y ???
#define KS_CM    33 //cursor motion. cursor_address. \e[%i%p1%d;%p2%dH
#define KS_SR    34 //scroll reverse (backward). scroll_reverse. \eM
#define KS_CRI   35 //cursor number of chars right. parm_right_cursor. \e[%p1%dC
#define KS_KS    36 //put term in "keypad transmit" mode. keypad_xmit. \e[?1h\e=
#define KS_KE    37 //out of "keypad transmit" mode. keypad_local. \e[?1l\e>
#define KS_TI    38 //put terminal in terminfo mode. enter_ca_mode. \e[?1049h\e[22;0;0t
#define KS_CTI   39 //put terminal in "raw" mode. EMPTY
#define KS_CRK   40 //request keyboard protocol state. EMPTY
#define KS_TE    41 //end of terminfo mode. exit_ca_mode. \e[?1049l\e[23;0;0t
#define KS_CTE   42 //end of "raw" mode. EMPTY
#define KS_CCS   43 //cur is relative to scroll region. EMPTY
#define KS_CSF   44 //set foreground color. EMPTY
#define KS_CSB   45 //set background color. EMPTY
#define KS_XS    46 //standout not erased by overwriting (hpterm). EMPTY
#define KS_XN    47 //newline glitch. y ???
#define KS_CAF   48 //set fg color (ANSI). set_a_foreground. \e[%?%p1%{8}%<%t3%p1%d%e%p1%{16}%<%t9%p1%{8}%-%d%e38:5:%p1%d%;m
#define KS_CAB   49 //set bg color (ANSI). set_a_background. \e[%?%p1%{8}%<%t4%p1%d%e%p1%{16}%<%t10%p1%{8}%-%d%e48:5:%p1%d%;m
#define KS_CAU   50 //set underline color (ANSI). EMPTY
#define KS_LE    51 //cursor left (mostly backspace). cub1. ^H (which is ASCII 08)
#define KS_ND    52 //cursor right. cuf1. \e[C
#define KS_CSC   53 //set cursor color start. EMPTY
#define KS_CEC   54 //set cursor color end. EMPTY
#define KS_TS    55 //set window title start (to status line). \e]2;
#define KS_FS    56 //set window title end (from status line). `\e\`
#define KS_CWP   57 //set window position in pixels. EMPTY
#define KS_CGP   58 //get window position. EMPTY
#define KS_CWS   59 //set window size in characters. EMPTY
#define KS_CRV   60 //request version string. \e[>c
#define KS_CXM   61 //enable/disable mouse reporting. \e[?1006;1000%?%p1%{1}%=%th%el%;
#define KS_CSV   62 //scroll region vertical. EMPTY
#define KS_OP    63 //original color pair. \e[39;49m
#define KS_U7    64 //request cursor position. \e[6n
#define KS_CBE   65 //enable bracketed paste mode. BE. \e[?2004h
#define KS_CBD   66 //disable bracketed paste mode. BD. \e[?2004l
#define KS_FD    67 //disable focus event tracking. EMPTY
#define KS_FE    68 //enable focus event tracking. EMPTY
#define KS_CF    69 //set terminal alternate font. EMPTY

#define KS_LAST  KS_CF

//the terminal capabilities are stored in this array
//IMPORTANT: When making changes, note the following:
//- there should be an entry for each code in the builtin terminfos
//- there should be an option for each code in option.c
//- there should be code in term.c to obtain the value from the termcap


typedef enum {
   TMODE_COOK,     //terminal mode for external commands
   TMODE_SLEEP,    //terminal mode for sleeping (cooked but no echo)
   TMODE_RAW,      //terminal mode for Normal and Insert mode
   TMODE_UNKNOWN   //after executing a shell
} TermInputMode;

//}}}
//{{{prototypes: include the (automatically generated) function prototypes

//Don't include these while generating prototypes.  Prevents problems when files are missing.
#if !defined(PROTO) && !defined(NOPROTO)

//Machine-dependent routines. avoid errors in function prototypes
//#define Display int
//#define Widget int
//#define XImage int

#include "proto/book.h"
#include "proto/data.h"
#include "proto/diff.h"
#include "proto/do.h"
#include "proto/draw.h"
#include "proto/eval.h"
#include "proto/fileio.h"
#include "proto/hilite.h"
#include "proto/input.h"
#include "proto/insert.h"
#include "proto/location.h"
#include "proto/main.h"
#include "proto/memory.h"

// These prototypes cannot be produced automatically.
int smsg0(char const*, ...) ATTRIBUTE_COLD ATTRIBUTE_FORMAT_PRINTF(1, 2);
#define smsg(a, ...) smsg0((char const*)(a), ##__VA_ARGS__)

int smsgDeco0(char, char const*, ...) ATTRIBUTE_FORMAT_PRINTF(2, 3);
#define smsgDeco(a, b, ...) smsgDeco0(a, (char const*)b, ##__VA_ARGS__)

int smsgDecoKeep0(char, char const*, ...) ATTRIBUTE_FORMAT_PRINTF(2, 3);
#define smsgDecoKeep(a, fmt, ...) smsgDecoKeep0(a, (char const*)fmt, ##__VA_ARGS__);

// These prototypes cannot be produced automatically.
int showErrFmtMsg0(char const*, ...) ATTRIBUTE_COLD ATTRIBUTE_FORMAT_PRINTF(1, 2);
#define showErrFmtMsg(a, ...) showErrFmtMsg0((char const*)a, ##__VA_ARGS__)

// These prototypes cannot be produced automatically.
void internalErrFmtMsg0(char const*, ...) ATTRIBUTE_COLD ATTRIBUTE_FORMAT_PRINTF(1, 2);
#define internalErrFmtMsg(a, ...) internalErrFmtMsg0((char const*)a, ##__VA_ARGS__)
int eeSnprintfAdd0(CS, Unt, char const *, ...) ATTRIBUTE_FORMAT_PRINTF(3, 4);
#define eeSnprintfAdd(a, b, fmt, ...) eeSnprintfAdd0(a, b, (char const*)fmt, ##__VA_ARGS__)

int eeSnprintf0(CS, Unt, char const* , ...) ATTRIBUTE_FORMAT_PRINTF(3, 4);
#define eeSnprintf(a, len, fmt, ...) eeSnprintf0(a, len, (char const*)(fmt), ##__VA_ARGS__)
Unt eeSnprintfSafelen0(CS, Unt, char const*, ...) ATTRIBUTE_FORMAT_PRINTF(3, 4);
#define eeSnprintfSafelen(a, b, fmt, ...) eeSnprintfSafelen0(a, b, (char const*)fmt, ##__VA_ARGS__)


int eeVarPrintf0(CS str, Unt str_m, char const* fmt, va_list ap, Var* tvs)
   ATTRIBUTE_FORMAT_PRINTF(3, 0);
#define eeVarPrintf(a, b, fmt, ...) eeVarPrintf0((char*)a, b, (char const*)fmt, ##__VA_ARGS__)

#include "proto/juggle.h"
#include "proto/message.h"
#include "proto/normal.h"
#include "proto/option.h"
#include "proto/persist.h"
#include "proto/portal.h"
#include "proto/regexp.h"
#include "proto/script.h"
#include "proto/search.h"
#include "proto/strings.h"
#include "proto/tag.h"
#include "proto/term.h"
#include "proto/ui.h"
#include "proto/window.h"
#include "proto/channel.h"

// Not generated automatically so that we can add an extra attribute.
void ch_log(Channel *ch, const char *fmt, ...) ATTRIBUTE_FORMAT_PRINTF(2, 3);
void lo(const char *fmt, ...) ATTRIBUTE_FORMAT_PRINTF(1, 2);
void ch_error(Channel *ch, const char *fmt, ...) ATTRIBUTE_FORMAT_PRINTF(2, 3);

#endif // !PROTO && !NOPROTO

//}}}

// This has to go after the include of proto.h, as proto/gui.h declares
// functions of these names. The declarations would break if the defines had
// been seen at that stage.  But it must be before globals, where errorsG is declared.
#if defined(PROTO)
#define USE_MCH_ERRMSG
#else
#define mch_errmsg(str)   fprintf(stderr, "%s", (str))
#define display_errors()   fflush(stderr)
#define mch_msg(str)      printf("%s", (str))
#endif

//{{{:::globals: global variables and messages

// Number of Rows and Columns in the screen.
// Must be long to be able to use them as options in option.c.
// Note: Use screenLinesRowsG and screenLinesColsG to access items in ScreenLines[].
// They may have different values when the screen wasn't (re)allocated yet
// after setting Rows or Columns (e.g., when starting up).
EXTERN long visibleRowsG         // nr of rows in the screen
#ifdef MAIN_C
          = 24L
#endif
;
EXTERN long visibleColsG INIT(= 80);   // nr of columns in the screen

EXTERN CS termCodesG[]; //current terminal output strings, defined in term.c
//Contains currently used terminal codes (strings used to communicate with the terminal).
//The values can be changed by setting the option with the same name.
//Nulls are not allowed! Only empty strings
EXTERN CS termCodesG[KS_LAST + 1];

//Note: before the screen is initialized and when out of memory these can be null.
EXTERN Boole wrapSearchG INIT(= true); // search wraps on file end

EXTERN int screenLinesRowsG INIT(= 0);   // actual size of ScreenLines[]
EXTERN int screenLinesColsG INIT(= 0);   // actual size of ScreenLines[]

// Last known cursor position. Positioning the cursor is reduced by remembering the last position.
// Mostly used by windgoto() and draw.c:screen_char().
EXTERN int screenCursRowG INIT(= 0);
EXTERN int screenCursColG INIT(= 0);

// last lnum where CurSearch was displayed
EXTERN LineNr searchLastLnumG INIT(= 0);

// do hilite search results?
EXTERN Boole hiliteSearchG INIT(= false);

// volatile because it is used in signal handler sig_winch().
typedef sig_atomic_t SigAtomic;
EXTERN volatile SigAtomic doResizeG INIT(= FALSE);
EXTERN Unt* tabIndsG INIT(= NULL);

// Array with size Rows x Columns containing zindex of popups.
EXTERN Arr(Short) popupMaskG INIT(= NULL);
EXTERN Arr(Short) popupMaskNextG INIT(= NULL);
//Array with flags for transparent cells of current popup.
EXTERN Arr(Byte) popupTransparencyG INIT(= NULL);

//Flag set to TRUE when popup_mask needs to be updated.
EXTERN int needRefreshPopupMaskG INIT(= TRUE);

//Zindex in for draw.c:screen_char(): if lower than the value in "popup_mask"
//drawing the character is skipped.
EXTERN int screenZindexG INIT(= 0);

//When vgetc() is called, it sets modMaskG to the set of modifiers that are
//held down based on the MOD_MASK_* symbols that are read first.
EXTERN Unt modMaskG INIT(= 0);      // current key modifiers

//The value of "mod_mask" and the unmodified character in vgetc() after it has
//called vgetorpeek() enough times.
EXTERN int vgetcModMaskG INIT(= 0);
EXTERN int vgetcOrigCharG INIT(= 0);

//CommlineRowG is the row where the command line starts, just below the last portal.
//When the commline gets longer than the available space, the screen gets scrolled up. After a
//CTRL-D (show matches), after hitting ':' after "hit return", and for the :global command, the
//command line is temporarily moved.  The old position is restored with the next call to 
//update_screen()
EXTERN Unt commlineRowG;

EXTERN Boole redrawCommlineG INIT(= false);   // commline must be redrawn
EXTERN Boole redrawModeG INIT(= false);   // mode must be redrawn
EXTERN Boole mustClearCommlineG INIT(= false);
EXTERN Boole isModeDisplayedG INIT(= false);   // mode is being displayed

EXTERN Boole executingFromRegG INIT(= false);   // executing register

// Variables for Insert mode completion.
EXTERN CS editSubmodeMsgG INIT(= NULL); // msg for CTRL-X submode
EXTERN CS editSubmodePreMsgG INIT(= NULL); // prepended to edit_submode
EXTERN CS editSubmodeExtraMsgG INIT(= NULL);// appended to edit_submode
EXTERN Unt editSubmodeHiG;   // hilite method for extra info

// Functions for putting characters into the command line while keeping ScreenLines[] updated.
EXTERN int msgColG;
EXTERN Unt msgRowG;
EXTERN int msg_scrolled; // Number of screen lines that portals have
                           // scrolled because of printing messages.
EXTERN int msg_scrolled_ign INIT(= FALSE);
            // when TRUE don't set need_wait_return in msgPutsDeco() when msg_scrolled is non-zero

EXTERN Arr(Byte) msgAfterRedrawG INIT(= NULL); // msg to be shown after redraw
EXTERN int decoAfterRedrawG INIT(= 0);  // hilite deco for msgAfterRedrawG
EXTERN int keep_msg_more INIT(= FALSE); // msgAfterRedrawG was set by msgmore()
EXTERN Boole needFileinfoG INIT(= false); // do fileinfo() after redraw
EXTERN int msg_scroll INIT(= FALSE);    // msg_start() will scroll
EXTERN int msg_didout INIT(= FALSE);    // msg_outstr() was used in line
EXTERN int msg_didany INIT(= FALSE);    // msg_outstr() was used at all
EXTERN int msg_nowait INIT(= FALSE);    // don't wait for this msg
EXTERN int emsg_off INIT(= 0);          // don't display errors for now, unless 'debug' is set
EXTERN int info_message INIT(= FALSE);  // printing informative message
EXTERN int msg_hist_off INIT(= FALSE);  // don't add messages to history
EXTERN int need_clr_eos INIT(= FALSE);  // need to clear text before displaying a message.
EXTERN int emsg_skip INIT(= 0);         // don't display errors for expression that is skipped
EXTERN int emsg_severe INIT(= FALSE);   // use message of next of several emsg() calls for throw
                                        // used by assert_fails()
EXTERN CS emsg_assert_fails_msg INIT(= NULL);
EXTERN long emsg_assert_fails_lnum INIT(= 0);
EXTERN CS emsg_assert_fails_context INIT(= NULL);

EXTERN int did_endif INIT(= FALSE);    // just had ":endif"
EXTERN int anyEmsgG; // incremented by emsg() when a message is displayed or thrown
EXTERN int uncaught_emsg; // number of times emsg() was called and did show a message
EXTERN int called_emsg;          // always incremented by emsg()
EXTERN int inEchoPortalG;          // executing ":echowindow"
EXTERN int ex_exitval INIT(= 0);       // exit value for ex mode
EXTERN int emsg_on_display INIT(= FALSE);   // there is an error message
EXTERN Boole anyRegexEmsgG INIT(= false);  // did eeRegexec() call emsg()?

EXTERN int no_wait_return INIT(= 0);   // don't wait for return for now
EXTERN int need_wait_return INIT(= 0); // need to wait for return later
EXTERN int did_wait_return INIT(= FALSE); //wait_return() was used and nothing written since then

EXTERN Boole quitMoreG INIT(= false);    // 'q' hit at "--more--" msg
EXTERN Boole newlineOnExitG INIT(= false);   // did msg in altern. screen
EXTERN Unt extraInterruptCharG INIT(= 0);       // extra interrupt character

EXTERN int vgetcBusyG INIT(= 0);         // when inside vgetc() then > 0

// Lines left before a "more" message.   Ex mode needs to be able to reset this
// after you type something.
EXTERN int   lines_left INIT(= -1);       // lines left for listing
EXTERN int   msg_no_more INIT(= FALSE);  // don't use more prompt, truncate messages

EXTERN Boole frozenOptionsG INIT(= false);

// Stack of execution contexts.  Each entry is an Estack.
// Current context is at len - 1.
EXTERN ArrayList   exestack INIT5(0, 0, sizeof(Estack), 50, NULL);
// name of error message source
#define SOURCING_NAME (((Estack *)exestack.c)[exestack.len - 1].name)
// line number in the message source or zero
#define SOURCING_LNUM (((Estack *)exestack.c)[exestack.len - 1].lnum)

// Script context being sourced or was sourced to define the current function.
EXTERN ScriptPos scriptPosG INIT3(0, 0, 0);

EXTERN int   ex_nesting_level INIT(= 0);   // nesting level
EXTERN int   debug_break_level INIT(= -1);   // break below this level
EXTERN int   debug_did_msg INIT(= FALSE);   // did "debug mode" message
EXTERN int   debug_tick INIT(= 0);      // breakpoint change count
EXTERN int   debug_backtrace_level INIT(= 0); // breakpoint backtrace level
EXTERN ArrayList script_items INIT5(0, 0, sizeof(ScriptItem *), 20, NULL);
# define SCRIPT_ITEM(id)    (((ScriptItem **)script_items.c)[(id) - 1])
# define SCRIPT_ID_VALID(id)    ((id) > 0 && (id) <= script_items.len)
# define SCRIPT_SV(id)      (SCRIPT_ITEM(id)->sn_vars)
# define SCRIPT_VARS(id)   (SCRIPT_SV(id)->sv_dict.hashTable)

# define FUNCLINE(fp, j)   ((Byte **)(fp->lines.c))[j]

//The exception currently being thrown.  Used to pass an exception to
//a different cstack.  Also used for discarding an exception before it is
//caught or made pending.  Only valid when did_throw is TRUE.
EXTERN Exception* current_exception;

// did_throw: An exception is being thrown.  Reset when the exception is caught
// or as long as it is pending in a finally clause.
EXTERN int did_throw INIT(= FALSE);

// need_rethrow: set to TRUE when a throw that cannot be handled in do_cmdline()
// must be propagated to the cstack of the previously called do_cmdline().
EXTERN int need_rethrow INIT(= FALSE);

// check_cstack: set to TRUE when a ":finish" or ":return" that cannot be
// handled in do_cmdline() must be propagated to the cstack of the previously called do_cmdline().
EXTERN int check_cstack INIT(= FALSE);

// Number of nested try conditionals (across function calls and ":source" commands).
EXTERN int trylevel INIT(= 0);

//When "force_abort" is TRUE, always skip commands after an error message, even after the outermost
//":endif", ":endwhile" or ":endfor" or for a function without the "abort" flag.  It is set to TRUE
//when "trylevel" is non-zero (and ":silent!" was not used) or an exception is being thrown at
//the time an error is detected. It is set to FALSE when "trylevel" gets zero again and there was 
//no error or interrupt or throw.
EXTERN Boole force_abort INIT(= false);

//"msg_list" points to a variable in the stack of do_cmdline() which keeps the list of arguments 
//of several emsg() calls, one of which is to be converted to an error exception immediately 
//after the failing command returns.  The message to be used for the exception value is pointed 
//to by the "throw_msg" field of the first element in the list.  It is usually the same as the 
//"msg" field of that element, but can be identical to the "msg" field of a later list element, 
//when the "emsg_severe" flag was set when the emsg() call was made.
EXTERN MsgList **msg_list INIT(= NULL);

//suppress_errthrow: When TRUE, don't convert an error to an exception.  Used when displaying the 
//interrupt message or reporting an exception that is still uncaught at the top level (which has 
//already been discarded then).  Also used for the error message when no exception can be thrown.
EXTERN Boole suppress_errthrow INIT(= false);

//The stack of all caught and not finished exceptions.  The exception on the top of the stack is 
//the one got by evaluation of v:exception.  The complete stack of all caught and pending 
//exceptions is embedded in the various cstacks; the pending exceptions, however, are not on the 
//caught stack.
EXTERN Exception* caught_stack INIT(= NULL);

//Garbage collection can only take place when we are sure there are no Lists or Dictionaries being 
//used internally. This is flagged with "may_garbage_collect" when we are at the toplevel.
//"want_garbage_collect" is set by the garbagecollect() function, which means
//we do garbage collection before waiting for a char at the toplevel.
//"garbage_collect_at_exit" indicates garbagecollect(1) was called.
EXTERN Boole may_garbage_collect INIT(= false);
EXTERN Boole want_garbage_collect INIT(= false);
EXTERN Boole garbage_collect_at_exit INIT(= false);

EXTERN Boole   did_source_packages INIT(= false);

// Magic number used for EeSetItem "hi_key" value indicating a deleted item. Only the address is used
EXTERN Byte hash_removed;

EXTERN int   scroll_region INIT(= FALSE); // term supports scroll region

// Flags to indicate an additional string for hilite name completion.
EXTERN int hiComplIncludeNoneG INIT(= 0);   // when 1 include "None"
EXTERN int hiComplIncludeDefaultG INIT(= 0);   // when 1 include "default"
EXTERN int hiComplIncludeLinkG INIT(= 0);   // when 2 include "link" and "clear"

// When highlight_match is TRUE, hilite a match starting at the cursor position. 
// Search_match_lines is the number of lines after the match (0 for a match within one line), 
// search_match_endcol the column number of the character just after the match in the last line.
EXTERN Boole highlight_match INIT(= false); // show search match pos
EXTERN LineNr search_match_lines;       // lines of matched string
EXTERN ColNr search_match_endcol;       // col nr of match end
EXTERN LineNr search_first_line INIT(= 0);     // for :{FIRST},{last}s/pat
EXTERN LineNr search_last_line INIT(= MAXLNUM); // for :{first},{LAST}s/pat

EXTERN Boole no_smartcase INIT(= false);   // don't use 'smartcase' once

EXTERN Boole need_check_timestamps INIT(= false); // need to check file timestamps asap
EXTERN Boole did_check_timestamps INIT(= false); // did check timestamps recently
EXTERN int no_check_timestamps INIT(= 0);   // Don't check timestamps


EXTERN Arr(Decoration) decorationsG; // The text decorations table used for drawing. See hilite.c
EXTERN int countDecosG; // length of decorationsG


// When TRUE skip calling terminal_loop() once.  Used when typing ':' at the more prompt
EXTERN Boole skip_term_loop INIT(= false);
EXTERN VTermColor defaultFgColorG INIT(= 7);
EXTERN VTermColor defaultBgColorG INIT(= 0);
EXTERN VTermColor defaultUnderlColorG INIT(= 1);
EXTERN Boole currentlyBoldG INIT(= false);

EXTERN Boole autocmd_busy INIT(= false);   // Is apply_autocmds() busy?
EXTERN Boole autocmd_no_enter INIT(= false); // Buf/WinEnter autocmds disabled
EXTERN Boole autocmd_no_leave INIT(= false); // Buf/WinLeave autocmds disabled
EXTERN Boole movingTabsForbiddenG INIT(= false);  // moving tabpages around disallowed

//When deleting the current book, another one must be loaded. If we know
//which one is preferred, auNewCurBookG is set to it
EXTERN BookRef auNewCurBookG INIT3(NULL, 0, 0);

//When deleting a book/portal and autocmd_busy is TRUE, do not free the
//book/portal. but link it in the list starting with au_pending_free_buf/ap_pending_free_win, 
//using b_next/next. Free the book/portal when autocmd_busy is being set to FALSE.
EXTERN Arr(Book) auPendingFreeBooksG INIT(= NULL);
EXTERN Arr(Portal) auPendingFreePortalsG INIT(= NULL);

// Mouse coordinates, set by check_termcode(). Can be negative if mouse is outside the window
EXTERN int mouseRowG;
EXTERN int mouseColG;
EXTERN int mouseDraggingG INIT(= 0);    //extending Visual area with mouse dragging

// Value set from 'diffopt'.
EXTERN int diff_context INIT(= 6);      //context for folds
EXTERN int linematch_lines INIT(= 0);   //number of lines for diff line match
EXTERN Boole diff_need_scrollbind INIT(= false);

// While redrawing the screen this flag is set.  It means the screen size
// ('lines' and 'rows') must not be changed and prevents recursive updating.
EXTERN Boole updating_screen INIT(= false);

// While computing a statusline and the like we do not want any w_redr_type or
// must_redraw to be set.
EXTERN Boole redraw_not_allowed INIT(= false);

//While closing portals or books, messages should not be handled to avoid
//using invalid portals or books.
EXTERN Boole dont_parse_messages INIT(= false);

EXTERN ClipBoard clipboard;   // CLIPBOARD selection in Wayland

//All regular portals are linked in a list. "firstpor" points to the first entry, "lastpor" to the 
//last entry (can be the same as firstwin) and "curpor" to the currently active portal.
//When switching tabs these swapped with the pointers in "Tab".
EXTERN Portal* firstPor;      // first portal
EXTERN Portal* lastPor;      // last portal
EXTERN Portal* prevPor INIT(= NULL);   // previous portal (may equal curPor)
#define ONLY_ONE_PORTAL (firstPor == lastPor)

EXTERN Portal* curPor;   // currently active portal

// When executing autocommands for a book without any portals, a special portal is created to 
// handle the side effects.  When autocommands nest we may need more than one.  Allow for up to 
// five, if more are needed something crazy is happening.
#define AUCMD_PORTAL_COUNT 5

typedef struct {
   Portal* port; //Portal used in aucmd_prepbuf(). When not NULL, the portal has been allocated.
   int isPortUsed; //This portal is being used.
} AutoCommPort;

EXTERN AutoCommPort autoCommPortG[AUCMD_PORTAL_COUNT];

EXTERN Portal* firstPopupPortG;      // first global popup portal
EXTERN Portal* popupDragPortG INIT(= NULL);   // popup window being dragged

// Set to TRUE if there is any visible popup portal.
EXTERN Boole popup_visible INIT(= false);

// Set to TRUE if a visible popup window may use a MOUSE_MOVE event
EXTERN Boole popup_uses_mouse_move INIT(= false);

EXTERN int textPropFrozenG INIT(= 0);

// when TRUE computing the cursor position ignores text properties.
EXTERN Boole ignore_text_props INIT(= false);

// When set the popup menu will redraw soon using the pum_win_ values. Do not
// draw over the poup menu area to avoid flicker.
EXTERN Boole pum_will_redraw INIT(= false);

// The portal layout is kept in a tree of frames. topframe points to the root of the tree.
EXTERN Frame   *topframeG;   // root of the portal frame tree

// Tabs are alternative topframes.  "firstTabG" points to the first
// one in the list, "curtab" is the current one. "lastUsedTabG" is the last used one.
EXTERN Tab* firstTabG;
EXTERN Tab* curtab;
EXTERN Tab* lastUsedTabG;
EXTERN Boole needRedrawTabpanelG INIT(= false); 

//All boks are linked in a list. 'firstBook' points to the first entry,
//'lastBook' to the last entry and 'curBook' to the currently active book.
EXTERN Book* firstBook INIT(= NULL);   // first book in the global linked list
EXTERN Book* lastBook INIT(= NULL);   // last book
EXTERN Book* curBook INIT(= NULL);   // currently active book

// List of files being edited (global argument list).  curPor->argList points
// to this when the window is using the global argument list.
EXTERN EeArgList argListG;          // global argument list
EXTERN int max_alist_id INIT(= 0);       // the previous argument list id
EXTERN int arg_had_last INIT(= FALSE); // accessed last file in argListG

EXTERN int rulerWidthG;      // @rulerformat: width of ruler when non-zero
EXTERN int shownCommandColG; // column for shown command

EXTERN DIR* eeTempDir_dpG INIT(= NULL); // File descriptor of temp dir
EXTERN CS eeTempDirG INIT(= NULL); // Name of Eegl's own temp dir. Ends with a slash.

// When starting or exiting some things are done differently (e.g. screen updating).
EXTERN int   starting INIT(= NO_SCREEN);
            // first NO_SCREEN, then NO_BUFFERS and then set to 0 when starting up finished
EXTERN Boole isExitingG INIT(= false); //TRUE when planning to exit Eegl. Might
                                       //still keep on running if there is a changed book.
EXTERN Boole really_exiting INIT(= false);
            // TRUE when we are sure to exit, e.g., after a deadly signal
EXTERN int   v_dying INIT(= 0); // internal value of v:dying
EXTERN Boole stdout_isatty INIT(= true);   // is stdout a terminal?

#if defined(EXITFREE)
EXTERN Boole   entered_free_all_mem INIT(= false); // TRUE when in or after free_all_mem()
#endif
// volatile because it is used in signal handler deathtrap().
EXTERN volatile sig_atomic_t fullScreenG INIT(= FALSE);
            // TRUE when doing full-screen output otherwise only writing some messages

EXTERN int textlock INIT(= 0);
            // non-zero when changing text and jumping to
            // another window or editing another book is not allowed

EXTERN int curBookLock INIT(= 0);
            // non-zero when the current book can't be changed.  Used for FileChangedRO.
EXTERN int allBookLock INIT(= 0);
            // non-zero when no book name can be changed, no book can be deleted and current 
            // directory can't be changed. Used for SwapExists et al.

EXTERN Boole silentModeG INIT(= false); // set to TRUE when "-s" commandline argument used for ex

EXTERN Pos VIsual;      // start position of active Visual selection
EXTERN Boole VIsual_active INIT(= false);
            // whether Visual mode is active
EXTERN Boole VIsual_select_exclu_adj INIT(= false);
            // whether incremented cursor during exclusive selection
EXTERN int VIsual_reselect;
            // whether to restart the selection after a
            // Select mode mapping or menu

EXTERN Unt VIsual_mode INIT(= 'v'); // type of Visual mode

EXTERN Boole isRedoVisualBusy INIT(= false); // TRUE when redoing Visual

// The Visual area is remembered for reselection.
EXTERN int   resel_VIsual_mode INIT(= ZERO);   // 'v', 'V', or Ctrl-V
EXTERN LineNr   resel_VIsual_line_count;   // number of lines
EXTERN ColNr   resel_VIsual_vcol;      // nr of cols or end col

// When pasting text with the middle mouse button in visual mode with
// restart_edit set, remember where it started so we can set Insstart.
EXTERN Pos   where_paste_started;

// This flag is used to make auto-indent work right on lines where only a
// <RETURN> or <ESC> is typed. It is set when an auto-indent is done, and
// reset when any other editing is done on the line. If an <ESC> or <RETURN>
// is received, and didAindentG is TRUE, the line is truncated.
EXTERN Boole didAindentG INIT(= false);

// Column of first char after autoindent.  0 when no autoindent done.  Used
// when 'backspace' is 0, to avoid backspacing over autoindent.
EXTERN ColNr ai_col INIT(= 0);

//This is a character which will end a start-middle-end comment when typed as
//the first character on a new line. It is taken from the last character of
//the "end" comment leader when the COM_AUTO_END flag is given for that
//comment end in 'comments'. It is only valid when didAindentG is TRUE.
EXTERN Unt end_comment_pending INIT(= ZERO);

// This flag is set after a ":syncbind" to let the check_scrollbind() function
// know that it should not attempt to perform scrollbinding due to the scroll
// that was a result of the ":syncbind." (Otherwise, check_scrollbind() will
// undo some of the work done by ":syncbind.")  -ralston
EXTERN Boole did_syncbind INIT(= false);

//This flag is set when a smart indent has been performed. When the next typed
//character is a '{', the inserted tab will be deleted again.
EXTERN Boole didSindentG INIT(= false);

// This flag is set after an auto indent. If the next typed character is a '}',
// one indent will be removed.
EXTERN Boole can_si INIT(= false);

// This flag is set after an "O" command. If the next typed character is a '{',
// one indent will be removed.
EXTERN Boole can_si_back INIT(= false);

EXTERN int   old_indent INIT(= 0);   // for ^^D command in insert mode

EXTERN Pos   saved_cursor      // w_cursor before formatting text.
#ifdef MAIN_C
          = {0, 0, 0}
#endif
          ;

// Stuff for insert mode.
EXTERN Pos   insertStartG;      // This is where the latest insert/append mode started.

// This is where the latest insert/append mode started. In contrast to
// Insstart, this won't be reset by certain keys and is needed for
// op_insert(), to detect correctly where inserting by the user started.
EXTERN Pos   insertStartOrigG;

// These flags are set based upon 'fileencoding'.
// The characters are internally stored as UTF-8 (to avoid trouble with ZERO)
#define DBCS_JPN    932   // japan
#define DBCS_JPNU  9932   // euc-jp
#define DBCS_KOR    949   // korea
#define DBCS_KORU  9949   // euc-kr
#define DBCS_CHS    936   // chinese
#define DBCS_CHSU  9936   // euc-cn
#define DBCS_CHT    950   // taiwan
#define DBCS_CHTU  9950   // euc-tw
#define DBCS_2BYTE    1   // 2byte-
#define DBCS_DEBUG (-1)

//To speed up BYTELEN() we fill a table with the byte lengths
EXTERN Byte utf8CharLens[256];

//"State" is the main state of Eegl.
//There are other variables that modify the state:
//"Visual_mode"   When State is MODE_NORMAL or MODE_INSERT.
//"finish_op"     When State is MODE_NORMAL, after typing the operator and
//                before typing the motion command.
//"motion_force"  Last motion_force  from visualOperator()
//"debug_mode"    Debug mode.
EXTERN int stateG INIT(= MODE_NORMAL);

EXTERN Boole debug_mode INIT(= false);

EXTERN Operator* currOperatorG INIT(= NULL);
EXTERN Boole finish_op INIT(= false);// TRUE while an operator is pending
EXTERN long opcount INIT(= 0);   // count for pending operator
EXTERN int motion_force INIT(= 0); // motion force for pending operator


EXTERN int ex_no_reprint INIT(= FALSE); // no need to print after z or p

EXTERN int reg_recording INIT(= 0);   // register for recording  or zero
EXTERN int reg_executing INIT(= 0);   // register being executed or zero
// Flag set when peeking a character and found the end of executed register
EXTERN int pending_end_reg_executing INIT(= FALSE);

// Set when a modifyOtherKeys sequence was seen, then simplified mappings will
// no longer be used.  To be combined with modify_otherkeys_state.
EXTERN int seenModifyOtherKeys INIT(= FALSE);

// The state for the modifyOtherKeys level
typedef enum {
   //Initially we have no clue if the protocol is on or off.
   MOKS_INITIAL,
   //Used when receiving the state and the level is not two.
   MOKS_OFF,
   //Used when receiving the state and the level is two.
   MOKS_ENABLED,
   //Used after outputting t_TE when the state was MOKS_ENABLED.  We do not
   //really know if t_TE actually disabled the protocol, the following t_TI
   //is expected to request the state, but the response may come only later.
   MOKS_DISABLED,
   //Used after outputting t_TE when the state was not MOKS_ENABLED.
   MOKS_AFTER_T_TE,
} mokstate_T;

// Set when a response to XTQMODKEYS was received. Only works for xterm
// version 377 and later.
EXTERN mokstate_T modify_otherkeys_state INIT(= MOKS_INITIAL);

EXTERN Boole no_mapping INIT(= false);   // currently no mapping allowed
EXTERN int isZeroJustANumberG INIT(= 0); // if "0" is interpreted as a number, not the action
EXTERN Boole allow_keys INIT(= false);   // allow key codes when no_mapping is set
EXTERN Boole no_reduce_keys INIT(= false);  // do not apply Ctrl, Shift and Alt to the key
EXTERN int no_u_sync INIT(= 0);      // Don't call u_sync()
EXTERN int u_sync_once INIT(= 0);   // Call u_sync() once when evaluating an expression

EXTERN int restart_edit INIT(= 0);   // call edit when next cmd finished
EXTERN int arrow_used; // Normally FALSE, set to TRUE after hitting cursor key in insert mode.
               // Used by vgetorpeek() to decide when to call u_sync()
EXTERN Boole ins_at_eol INIT(= false); // put cursor after eol when restarting edit after CTRL-O

EXTERN Boole no_abbr INIT(= true);   // TRUE when no abbreviations loaded

#ifdef USE_EXE_NAME
EXTERN CS exe_name;      // the name of the executable
#endif

EXTERN int   mapped_ctrl_c INIT(= FALSE); // modes where CTRL-C is mapped
EXTERN int   ctrl_c_interrupts INIT(= TRUE);   // CTRL-C sets gotInterruptG

EXTERN CommandModifier   commModifierG;         // Command modifiers
EXTERN int   stickyCommandModifiersG INIT(= 0); // used by :execute

EXTERN int   msg_silent INIT(= 0);   // don't print messages
EXTERN int   emsg_silent INIT(= 0);   // don't print error messages
EXTERN int   emsg_silent_def INIT(= 0);  // value of emsg_silent when a :def
                   // function is called
EXTERN int   emsg_noredir INIT(= 0);   // don't redirect error messages
EXTERN Boole   cmd_silent INIT(= false); // don't echo the command line

EXTERN Boole   in_assert_fails INIT(= false);   // assert_fails() active

EXTERN Boole  swapEnabledG INIT(= true); //Swap files enabled
EXTERN Text swapDirG; //Directory for swap files
EXTERN int   swap_exists_action INIT(= SEA_NONE); // For dialog when swap file already exists.
EXTERN Boole   swap_exists_did_quit INIT(= false); // Selected "quit" at the dialog.

EXTERN CS IObuff;      // sprintf's are done in this buffer, size is IOSIZE
EXTERN CS nameBuffG;      // file names are expanded in this array, size is MAXPATHL
EXTERN Byte msg_buf[MSG_BUF_LEN];   // small buffer for messages


// When non-zero, postpone redrawing.
EXTERN int   isRedrawingDisabledG INIT(= 0);

EXTERN Boole recoveryModeG INIT(= false); // Set to TRUE for "-r" option

// Typeahead buffer. Used for getting input from the keyboard
EXTERN Typeahead typeBufG

#ifdef MAIN_C
          = {NULL, NULL, 0, 0, 0, 0, 0, 0, 0}
#endif
          ;
// Flag used to indicate that vgetorpeek() returned a char like Esc when the
// :normal argument was exhausted.
EXTERN int   typebuf_was_empty INIT(= FALSE);

EXTERN int   ex_normal_busy INIT(= 0);   // recursiveness of ex_normal()
EXTERN int   in_feedkeys INIT(= 0);       // ex_normal_busy set in feedkeys()
EXTERN int   ex_normal_lock INIT(= 0);   // forbid use of ex_normal()

EXTERN int   ignore_script INIT(= FALSE);  // ignore script input
EXTERN int   stop_insert_mode;   // for ":stopinsert" and 'insertmode'

EXTERN Boole keyWasTypedG;      // TRUE if user typed current char
EXTERN Boole keyWasStuffedG;      // TRUE if current char from stuffbuf
EXTERN int maptick INIT(= 0);   // tick for each non-mapped char

EXTERN Unt   mustRedrawG INIT(= 0);       // type of redraw necessary (UPD_* constants)
EXTERN Boole skip_redraw INIT(= FALSE);  // skip redraw once
EXTERN Boole do_redraw INIT(= FALSE);    // extra redraw once
EXTERN Boole diffNeedsRedrawG INIT(= false); // need to call diff_redraw()
// flag set when 'redrawtime' timeout has been set
EXTERN Boole redrawtime_limit_set INIT(= FALSE);

EXTERN Boole need_highlight_changed INIT(= TRUE);

#define NSCRIPT 15
EXTERN FILE* scriptin[NSCRIPT];       // streams to read script from
EXTERN int curscript INIT(= 0);       // index in scriptin[]
EXTERN FILE* scriptout  INIT(= NULL);   // stream to write script to
EXTERN int  read_cmd_fd INIT(= 0);       // fd to read commands from

// Set to TRUE when an interrupt signal occurred.
// Volatile because it is used in signal handler catch_sigint().
EXTERN volatile sig_atomic_t gotInterruptG INIT(= FALSE);

// Set to TRUE when SIGUSR1 signal was detected.
// Volatile because it is used in signal handler catch_sigint().
EXTERN volatile sig_atomic_t got_sigusr1 INIT(= FALSE);

#ifdef USE_TERM_CONSOLE
EXTERN int   term_console INIT(= FALSE); // set to TRUE when console used
#endif

EXTERN int   termcap_active INIT(= FALSE);   // set by starttermcap()

EXTERN TermInputMode   cur_tmode INIT(= TMODE_COOK);   // input terminal mode
// Current terminal mode from mch_termSetMode(). Can differ from cur_tmode.
EXTERN TermInputMode mch_cur_tmode INIT(= TMODE_COOK);


EXTERN int   bangredo INIT(= FALSE);       // set to TRUE with ! command
EXTERN int   searchcmdlen;          // length of previous search cmd
EXTERN int   reg_do_extmatch INIT(= 0);  // Used when compiling regexp:
                   // REX_SET to allow \z\(...\),
                   // REX_USE to allow \z\1 et al.
EXTERN RegExternalMatch* re_extmatch_in INIT(= NULL); // Used by eeRegexec():
                   // strings for \z\1...\z\9
EXTERN RegExternalMatch* re_extmatch_out INIT(= NULL); // Set by eeRegexec()
                   // to store \z\(...\) matches

EXTERN int   did_outofmem_msg INIT(= FALSE); // set after out of memory msg
EXTERN int   did_swapwrite_msg INIT(= FALSE); // set after swap write error msg
EXTERN int   undo_off INIT(= FALSE);       // undo switched off for now
EXTERN int   global_busy INIT(= 0);       // set when :global is executing
EXTERN int   listcmd_busy INIT(= FALSE); // set when :argdo, :windo or :bufdo is executing
EXTERN Byte   last_mode[MODE_MAX_LENGTH] INIT(= "n"); // for ModeChanged event
EXTERN CS lastCommlineG INIT(= E); // last command line (for ":")
EXTERN CS repeatCommlineG INIT(= E); // command line for "."
EXTERN CS newLastCommlineG INIT(= E);   // new value for lastCommlineG
EXTERN CS autocmd_fname INIT(= E); // fname for <afile> on commline
EXTERN int   autocmd_fname_full;        // autocmd_fname is full path
EXTERN int   autocmd_bufnr INIT(= 0);     // fnum for <abuf> on commline
EXTERN CS autocmd_match INIT(= E); // name for <amatch> on commline

EXTERN int   did_cursorhold INIT(= TRUE);  // set when CursorHold triggered
EXTERN Pos   last_cursormoved         // for CursorMoved event
# ifdef MAIN_C
          = {0, 0, 0}
# endif
          ;

EXTERN int   postponed_split INIT(= 0);  // for CTRL-W CTRL-] command
EXTERN int   postponed_split_flags INIT(= 0);  // args for win_split()
EXTERN int   postponed_split_tab INIT(= 0);  // commModifierG.cmod_tab
EXTERN int   g_do_tagpreview INIT(= 0);  // for tag preview commands: height of preview portal
EXTERN int   g_tag_at_cursor INIT(= FALSE); // whether the tag command comes
                   // from the command line (0) or was invoked as a normal command (1)


EXTERN Byte   *escape_chars INIT(= (Byte *)" \t\\\"|"); // need backslash in cmd line
                   
EXTERN int concatenateBackslashesG INIT(= TRUE);                   

EXTERN int   keep_help_flag INIT(= FALSE); // doing :ta from help file

EXTERN int  redir_off INIT(= FALSE);   // no redirection for a moment
EXTERN FILE *redir_fd INIT(= NULL);   // message redirection file
EXTERN int  redir_reg INIT(= 0);   // message redirection register
EXTERN int  redir_vname INIT(= 0);   // message redirection variable
EXTERN int  redir_execute INIT(= 0);   // execute() redirection

EXTERN Byte   langmap_mapchar[256];   // mapping for language keys

EXTERN int  wild_menu_showing INIT(= 0);
#define WM_SHOWN     1      // wildmenu showing
#define WM_SCROLLED  2      // wildmenu showing with scroll

EXTERN Boole breakat_flags[256];   // which characters are in 'breakat'

// These are in main.c, call initLongVersion() before use.
extern CS Version;
EXTERN CS homedir INIT(= NULL);

// When a window has a local directory, the absolute path of the global
// current directory is stored here (in allocated memory).  If the current
// directory is not a local directory, globaldir is NULL.
EXTERN CS globaldir INIT(= NULL);

EXTERN int   disable_fold_update INIT(= 0);

// Whether 'keymodel' contains "stopsel" and "startsel".
EXTERN int   km_stopsel INIT(= FALSE);
EXTERN int   km_startsel INIT(= FALSE);

EXTERN int   commPortTypeG INIT(= 0);   // type of commline portal or 0
EXTERN Unt   commPortResultG INIT(= 0); // result of commline portal or 0
EXTERN Book* commPortBookG INIT(= NULL); // book of commline portal or NULL
EXTERN Book* msgG INIT(= NULL); // book of messages
EXTERN Portal* commPortPortG INIT(= NULL); // portal of commmline portal or NULL

EXTERN Byte no_lines_msg[]   INIT(= "--No lines in book--");

// When ":global" is used to number of substitutions and changed lines is
// accumulated until it's finished.
// Also used for ":spellrepall".
EXTERN long   sub_nsubs;   // total number of substitutions
EXTERN LineNr   sub_nlines;   // total number of lines changed

EXTERN BalloonEval   *balloonEval INIT(= NULL);
EXTERN int      balloonEvalForTerm INIT(= FALSE);

EXTERN int   typebuf_was_filled INIT(= FALSE); // received text from client or from feedkeys()

EXTERN CS serverName INIT(= NULL);   // name of the server

EXTERN int   term_is_xterm INIT(= FALSE);   // xterm-like 'term'

// Set to TRUE when an operator is being executed with virtual editing, MAYBE
// when no operator is being executed, FALSE otherwise.
EXTERN int   virtual_op INIT(= MAYBE);

// Display tick, incremented for each call to update_screen()
EXTERN DisplayTick   display_tick INIT(= 0);

// Line in which spell checking wasn't hilited because it touched the
// cursor position in Insert mode.
EXTERN LineNr      spell_redraw_lnum INIT(= 0);

// Set when the cursor line needs to be redrawn.
EXTERN int      need_cursor_line_redraw INIT(= FALSE);

#ifdef USE_MCH_ERRMSG
// Grow array to collect error messages in until they can be displayed.
EXTERN ArrayList errorsG
# ifdef MAIN_C
          = {0, 0, 0, 0, NULL}
# endif
          ;
#endif

// Some messages that can be shared are included here.
EXTERN char top_bot_msg[]   INIT(= "search hit TOP, continuing at BOTTOM");
EXTERN char bot_top_msg[]   INIT(= "search hit BOTTOM, continuing at TOP");
EXTERN char line_msg[]       INIT(= " line ");

EXTERN FILE *time_fd INIT(= NULL);  // where to write startup timing

// set by alloc_fail(): ID
EXTERN AllocId  alloc_fail_id INIT(= aid_none);
// set by alloc_fail(), when zero alloc() returns NULL
EXTERN int  alloc_fail_countdown INIT(= -1);
// set by alloc_fail(), number of times alloc() returns NULL
EXTERN int  alloc_fail_repeat INIT(= 0);

// flags set by test_override()
EXTERN int  disable_char_avail_for_testing INIT(= FALSE);
EXTERN int  disable_redraw_for_testing INIT(= FALSE);
EXTERN int  ignore_redraw_flag_for_testing INIT(= FALSE);
EXTERN int  nfa_fail_for_testing INIT(= FALSE);
EXTERN int  no_query_mouse_for_testing INIT(= FALSE);
EXTERN int  ui_delay_for_testing INIT(= 0);
EXTERN int  reset_term_props_on_termresponse INIT(= FALSE);
EXTERN int  disable_vterm_title_for_testing INIT(= FALSE);
EXTERN long overrideSysinfoUptimeG INIT(= -1);
EXTERN int  override_autoload INIT(= FALSE);
EXTERN int  override_defcompile INIT(= FALSE);
EXTERN int  ignore_unreachable_code_for_testing INIT(= FALSE);

EXTERN int  in_free_unref_items INIT(= FALSE);

EXTERN int  did_add_timer INIT(= FALSE);
EXTERN int  timer_busy INIT(= 0);   // when timer is inside vgetc() then > 0
EXTERN int  input_busy INIT(= 0);   // when inside get_user_input() then > 0

EXTERN int  bevalexpr_due_set INIT(= FALSE);
EXTERN ProfTime bevalexpr_due;

EXTERN Tyme time_for_testing INIT(= 0);

EXTERN char echoDecoFlagsG INIT(= 0);   // decoration flags used for ":echo"

// Abort conversion to string after a recursion error.
EXTERN int  did_echo_string_emsg INIT(= FALSE);

// Used for checking if local variables or arguments used in a lambda.
EXTERN int *eval_lavars_used INIT(= NULL);

// Used for lv_first in a non-materialized range() list.
EXTERN ListItem range_list_item;

// Passed to an eval() function to enable evaluation.
EXTERN EvalCtx EVALARG_EVALUATE
# ifdef MAIN_C
   = {EVAL_EVALUATE, 0, NULL, NULL, NULL, GA_EMPTY, GA_EMPTY, NULL,
          {0, 0, (int)sizeof(Byte *), 20, NULL}, 0, NULL}
# endif
   ;
   

EXTERN char *chanFdNames[]
# ifdef MAIN_C
      = {"sock", "out", "err", "in"}
# endif
      ;

// Whether a redraw is needed for appending a line to a book.
EXTERN int channel_need_redraw INIT(= FALSE);

// This flag is set when outputting a terminal control code and reset in
// out_flush() when characters have been written.
EXTERN int ch_log_output INIT(= FALSE);

EXTERN int did_repeated_msg INIT(= 0);
# define REPEATED_MSG_LOOKING       1
# define REPEATED_MSG_SAFESTATE     2


// Skip porta.fixCursor() call for 'splitkeep' when cmdwin is closed.
EXTERN int skipPortFixCursorG INIT(= FALSE);
// Skip portal.fixScroll() call for 'splitkeep' when closing tab page.
EXTERN int skipPortFixScrollG INIT(= FALSE);
// Skip update_topline() call while executing win_fix_scroll().
EXTERN int skipUpdateToplineG INIT(= FALSE);

// 'showcmd' buffer shared between normal.c and statusline code
#define SHOWCMD_BUFLEN (SHOWCMD_COLS + 1 + 30)
EXTERN Byte showcmd_buf[SHOWCMD_BUFLEN];

// If we've already warned about missing/unavailable clipboard
EXTERN int did_warn_clipboard INIT(= FALSE);

EXTERN int wayland_no_connect INIT(= FALSE); //Don't connect to Wayland compositor if TRUE
EXTERN NULLABLE CS wayland_display_name INIT(= NULL); //Wayland display name (ex. wayland-0)
EXTERN int wayland_display_fd; // Wayland display file descriptor; set by wayland_init_client()


//}}}
//{{{:::errors. Error message declarations

//Use PLURAL_MSG() for messages that are passed to ngettext(), so that the
//second one uses msgid_plural.
#ifdef MAIN_C
# define PLURAL_MSG(var1, msg1, var2, msg2) \
   char var1[] = msg1; \
   char var2[] = msg2;
#else
# define PLURAL_MSG(var1, msg1, var2, msg2) \
   extern char var1[]; \
   extern char var2[];
#endif

//Definition of error messages, sorted on error number.

EXTERN Byte e_interrupted[]
   INIT(= "Interrupted");



EXTERN Byte e_backslash_should_be_followed_by[]
   INIT(= "E10: \\ should be followed by /, ? or &");
EXTERN Byte e_invalid_in_commline_portal[]
   INIT(= "E11: Invalid in command-line portal; :q<CR> closes the portal");
EXTERN Byte e_command_not_allowed_from_vimrc_in_current_dir_or_tag_search[]
   INIT(= "E12: Command not allowed from vimrc in current dir or tag search");
EXTERN Byte e_file_exists[]
   INIT(= "E13: File exists (add ! to override)");
EXTERN Byte e_invalid_expression_str[]
   INIT(= "E15: Invalid expression: \"%s\"");
EXTERN Byte e_invalid_range[]
   INIT(= "E16: Invalid range");
EXTERN Byte e_str_is_directory[]
   INIT(= "E17: \"%s\" is a directory");
EXTERN Byte e_unexpected_characters_in_let[]
   INIT(= "E18: Unexpected characters in :let");
EXTERN Byte e_unexpected_characters_in_assignment[]
   INIT(= "E18: Unexpected characters in assignment");
EXTERN Byte e_mark_has_invalid_line_number[]
   INIT(= "E19: Mark has invalid line number");
EXTERN Byte e_mark_not_set[]
   INIT(= "E20: Mark not set");
EXTERN Byte e_cannot_make_changes_modifiable_is_off[]
   INIT(= "E21: Cannot make changes, 'modifiable' is off");
EXTERN Byte e_scripts_nested_too_deep[]
   INIT(= "E22: Scripts nested too deep");
EXTERN Byte e_no_alternate_file[]
   INIT(= "E23: No alternate file");
EXTERN Byte e_no_such_abbreviation[]
   INIT(= "E24: No such abbreviation");
EXTERN Byte e_no_such_highlight_group_name_str[]
   INIT(= "E28: No such highlight group name: %s");
EXTERN Byte e_no_inserted_text_yet[]
   INIT(= "E29: No inserted text yet");
EXTERN Byte e_no_previous_command_line[]
   INIT(= "E30: No previous command line");
EXTERN Byte e_no_such_mapping[]
   INIT(= "E31: No such mapping");
EXTERN Byte e_no_file_name[]
   INIT(= "E32: No file name");
EXTERN Byte e_no_previous_substitute_regular_expression[]
   INIT(= "E33: No previous substitute regular expression");
EXTERN Byte e_no_previous_command[]
   INIT(= "E34: No previous command");
EXTERN Byte e_no_previous_regular_expression[]
   INIT(= "E35: No previous regular expression");
EXTERN Byte e_not_enough_room[]
   INIT(= "E36: Not enough room");
EXTERN Byte e_no_write_since_last_change[]
   INIT(= "E37: No write since last change");
EXTERN Byte e_no_write_since_last_change_add_bang_to_override[]
   INIT(= "E37: No write since last change (add ! to override)");
EXTERN Byte e_null_argument[]
   INIT(= "E38: Null argument");
EXTERN Byte e_number_expected[]
   INIT(= "E39: Number expected");
EXTERN Byte e_cant_open_errorfile_str[]
   INIT(= "E40: Can't open errorfile %s");
EXTERN Byte e_out_of_memory[]
   INIT(= "E41: Out of memory!");
EXTERN Byte e_no_entries_in_location_list[]
   INIT(= "E42: No entries in this location list");
EXTERN Byte e_damaged_match_string[]
   INIT(= "E43: Damaged match string");
EXTERN Byte e_corrupted_regexp_program[]
   INIT(= "E44: Corrupted regexp program");
EXTERN Byte e_readonly_option_is_set_add_bang_to_override[]
   INIT(= "E45: book is not modifiable");
EXTERN Byte e_cannot_change_readonly_variable[]
   INIT(= "E46: Cannot change read-only variable");
EXTERN Byte e_cannot_change_readonly_variable_str[]
   INIT(= "E46: Cannot change read-only variable \"%s\"");
EXTERN Byte e_error_while_reading_errorfile[]
   INIT(= "E47: Error while reading errorfile");
EXTERN Byte e_invalid_scroll_size[]
   INIT(= "E49: Invalid scroll size");
EXTERN Byte e_too_many_z[]
   INIT(= "E50: Too many \\z(");
EXTERN Byte e_too_many_str_open[]
   INIT(= "E51: Too many %s(");
EXTERN Byte e_unmatched_z[]
   INIT(= "E52: Unmatched \\z(");
EXTERN Byte e_unmatched_str_percent_open[]
   INIT(= "E53: Unmatched %s%%(");
EXTERN Byte e_unmatched_str_open[]
   INIT(= "E54: Unmatched %s(");
EXTERN Byte e_unmatched_str_close[]
   INIT(= "E55: Unmatched %s)");
EXTERN Byte e_invalid_character_after_str_at[]
   INIT(= "E59: Invalid character after %s@");
EXTERN Byte e_too_many_complex_str_curly[]
   INIT(= "E60: Too many complex %s{...}s");
EXTERN Byte e_nested_str[]
   INIT(= "E61: Nested %s*");
EXTERN Byte e_nested_str_chr[]
   INIT(= "E62: Nested %s%c");
EXTERN Byte e_invalid_use_of_underscore[]
   INIT(= "E63: Invalid use of \\_");
EXTERN Byte e_str_chr_follows_nothing[]
   INIT(= "E64: %s%c follows nothing");
EXTERN Byte e_illegal_back_reference[]
   INIT(= "E65: Illegal back reference");
EXTERN Byte e_z_not_allowed_here[]
   INIT(= "E66: \\z( not allowed here");
EXTERN Byte e_z1_z9_not_allowed_here[]
   INIT(= "E67: \\z1 - \\z9 not allowed here");
EXTERN Byte e_invalid_character_after_bsl_z[]
   INIT(= "E68: Invalid character after \\z");
EXTERN Byte e_missing_sb_after_str[]
   INIT(= "E69: Missing ] after %s%%[");
EXTERN Byte e_empty_str_brackets[]
   INIT(= "E70: Empty %s%%[]");
EXTERN Byte e_invalid_character_after_str[]
   INIT(= "E71: Invalid character after %s%%");
EXTERN Byte e_close_error_on_swap_file[]
   INIT(= "E72: Close error on swap file");
EXTERN Byte e_tag_stack_empty[]
   INIT(= "E73: Tag stack empty");
EXTERN Byte e_command_too_complex[]
   INIT(= "E74: Command too complex");
EXTERN Byte e_name_too_long[]
   INIT(= "E75: Name too long");
EXTERN Byte e_too_many_brackets[]
   INIT(= "E76: Too many [");
EXTERN Byte e_too_many_file_names[]
   INIT(= "E77: Too many file names");
EXTERN Byte e_unknown_mark[]
   INIT(= "E78: Unknown mark");
EXTERN Byte e_cannot_expand_wildcards[]
   INIT(= "E79: Cannot expand wildcards");
EXTERN Byte e_error_while_writing[]
   INIT(= "E80: Error while writing");
EXTERN Byte e_using_sid_not_in_script_context[]
   INIT(= "E81: Using <SID> not in a script context");
EXTERN Byte e_cannot_allocate_any_buffer_exiting[]
   INIT(= "E82: Cannot allocate any book, exiting...");
EXTERN Byte e_cannot_allocate_book_using_other_one[]
   INIT(= "E83: Cannot allocate book, using other one...");
EXTERN Byte e_no_modified_buffer_found[]
   INIT(= "E84: No modified book found");
EXTERN Byte e_there_is_no_listed_buffer[]
   INIT(= "E85: There is no listed book");
EXTERN Byte e_book_nr_does_not_exist[]
   INIT(= "E86: Book %ld does not exist");
EXTERN Byte e_cannot_go_beyond_last_buffer[]
   INIT(= "E87: Cannot go beyond last book");
EXTERN Byte e_cannot_go_before_first_buffer[]
   INIT(= "E88: Cannot go before first book");
EXTERN Byte e_no_write_since_last_change_for_buffer_nr_add_bang_to_override[]
   INIT(= "E89: No write since last change for book %d (add ! to override)");
EXTERN Byte e_cannot_unload_last_buffer[]
   INIT(= "E90: Cannot unload last book");
EXTERN Byte e_book_nr_not_found[]
   INIT(= "E92: Book %d not found");
EXTERN Byte e_more_than_one_match_for_str[]
   INIT(= "E93: More than one match for %s");
EXTERN Byte e_no_matching_buffer_for_str[]
   INIT(= "E94: No matching book for %s");
EXTERN Byte e_buffer_with_this_name_already_exists[]
   INIT(= "E95: Book with this name already exists");
EXTERN Byte e_cannot_diff_more_than_nr_buffers[]
   INIT(= "E96: Cannot diff more than %d books");
EXTERN Byte e_cannot_create_diffs[]
   INIT(= "E97: Cannot create diffs");
EXTERN Byte e_cannot_read_diff_output[]
   INIT(= "E98: Cannot read diff output");
EXTERN Byte e_current_buffer_is_not_in_diff_mode[]
   INIT(= "E99: Current book is not in diff mode");
EXTERN Byte e_no_other_buffer_in_diff_mode[]
   INIT(= "E100: No other book in diff mode");
EXTERN Byte e_more_than_two_buffers_in_diff_mode_dont_know_which_one_to_use[]
   INIT(= "E101: More than 2 books in diff mode, don't know which one to use");
EXTERN Byte e_cant_find_book_str[]
   INIT(= "E102: Can't find book \"%s\"");
EXTERN Byte e_buffer_str_is_not_in_diff_mode[]
   INIT(= "E103: Book \"%s\" is not in diff mode");
EXTERN Byte e_using_loadkeymap_not_in_sourced_file[]
   INIT(= "E105: Using :loadkeymap not in a sourced file");
EXTERN Byte e_unsupported_diff_output_format_str[]
   INIT(= "E106: Unsupported diff output format: %s");
EXTERN Byte e_missing_parenthesis_str[]
   INIT(= "E107: Missing parentheses: %s");
EXTERN Byte e_no_such_variable_str[]
   INIT(= "E108: No such variable: \"%s\"");
EXTERN Byte e_missing_colon_after_questionmark[]
   INIT(= "E109: Missing ':' after '?'");
EXTERN Byte e_missing_closing_paren[]
   INIT(= "E110: Missing ')'");
EXTERN Byte e_missing_closing_square_brace[]
   INIT(= "E111: Missing ']'");
EXTERN Byte e_option_name_missing_str[]
   INIT(= "E112: Option name missing: %s");
EXTERN Byte e_unknown_option_str[]
   INIT(= "E113: Unknown option: %s");
EXTERN Byte e_missing_double_quote_str[]
   INIT(= "E114: Missing double quote: %s");
EXTERN Byte e_missing_single_quote_str[]
   INIT(= "E115: Missing single quote: %s");
EXTERN Byte e_invalid_arguments_for_function_str[]
   INIT(= "E116: Invalid arguments for function %s");
EXTERN Byte e_unknown_function_str[]
   INIT(= "E117: Unknown function: %s");
EXTERN Byte e_too_many_arguments_for_function_str[]
   INIT(= "E118: Too many arguments for function: %s");
EXTERN Byte e_not_enough_arguments_for_function_str[]
   INIT(= "E119: Not enough arguments for function: %s");
EXTERN Byte e_using_sid_not_in_script_context_str[]
   INIT(= "E120: Using <SID> not in a script context: %s");
EXTERN Byte e_undefined_variable_str[]
   INIT(= "E121: Undefined variable: %s");
EXTERN Byte e_undefined_variable_char_str[]
   INIT(= "E121: Undefined variable: %c:%s");
EXTERN Byte e_function_str_already_exists_add_bang_to_replace[]
   INIT(= "E122: Function %s already exists, add ! to replace it");
EXTERN Byte e_undefined_function_str[]
   INIT(= "E123: Undefined function: %s");
EXTERN Byte e_missing_paren_str[]
   INIT(= "E124: Missing '(': %s");
EXTERN Byte e_illegal_argument_str[]
   INIT(= "E125: Illegal argument: %s");
EXTERN Byte e_missing_endfunction[]
   INIT(= "E126: Missing :endfunction");
EXTERN Byte e_cannot_redefine_function_str_it_is_in_use[]
   INIT(= "E127: Cannot redefine function %s: It is in use");
EXTERN Byte e_function_name_must_start_with_capital_or_s_str[]
   INIT(= "E128: Function name must start with a capital or \"s:\": %s");
EXTERN Byte e_function_name_required[]
   INIT(= "E129: Function name required");
// E130 unused
EXTERN Byte e_cannot_delete_function_str_it_is_in_use[]
   INIT(= "E131: Cannot delete function %s: It is in use");
EXTERN Byte e_function_call_depth_is_higher_than_maxfuncdepth[]
   INIT(= "E132: Function call depth is higher than 'maxfuncdepth'");
EXTERN Byte e_return_not_inside_function[]
   INIT(= "E133: :return not inside a function");
EXTERN Byte e_cannot_move_range_of_lines_into_itself[]
   INIT(= "E134: Cannot move a range of lines into itself");
EXTERN Byte e_filter_autocommands_must_not_change_current_buffer[]
   INIT(= "E135: *Filter* Autocommands must not change current book");
EXTERN Byte e_eeglinfo_too_many_errors_skipping_rest_of_file[]
   INIT(= "E136: eeglinfo: Too many errors, skipping rest of file");
EXTERN Byte e_eeglinfo_file_is_not_writable_str[]
   INIT(= "E137: Eeglinfo file is not writable: %s");
EXTERN Byte e_cant_write_eeglinfo_file_str[]
   INIT(= "E138: Can't write eeglinfo file %s!");
EXTERN Byte e_file_is_loaded_in_another_buffer[]
   INIT(= "E139: File is loaded in another book");
EXTERN Byte e_use_bang_to_write_partial_buffer[]
   INIT(= "E140: Use ! to write partial book");
EXTERN Byte e_no_file_name_for_buffer_nr[]
   INIT(= "E141: No file name for buffer %ld");
EXTERN Byte e_file_not_written_writing_is_disabled_by_write_option[]
   INIT(= "E142: File not written: Writing is disabled by 'write' option");
EXTERN Byte e_autocommands_unexpectedly_deleted_new_buffer_str[]
   INIT(= "E143: Autocommands unexpectedly deleted new buffer %s");
EXTERN Byte e_non_numeric_argument_to_z[]
   INIT(= "E144: Non-numeric argument to :z");
EXTERN Byte e_regular_expressions_cant_be_delimited_by_letters[]
   INIT(= "E146: Regular expressions can't be delimited by letters");
EXTERN Byte e_cannot_do_global_recursive_with_range[]
   INIT(= "E147: Cannot do :global recursive with a range");
EXTERN Byte e_regular_expression_missing_from_global[]
   INIT(= "E148: Regular expression missing from :global");
EXTERN Byte e_sorry_no_help_for_str[]
   INIT(= "E149: Sorry, no help for %s");
EXTERN Byte e_not_a_directory_str[]
   INIT(= "E150: Not a directory: %s");
EXTERN Byte e_no_match_str_1[]
   INIT(= "E151: No match: %s");
EXTERN Byte e_cannot_open_str_for_writing_1[]
   INIT(= "E152: Cannot open %s for writing");
EXTERN Byte e_unable_to_open_str_for_reading[]
   INIT(= "E153: Unable to open %s for reading");
EXTERN Byte e_duplicate_tag_str_in_file_str_str[]
   INIT(= "E154: Duplicate tag \"%s\" in file %s/%s");
EXTERN Byte e_unknown_sign_str[]
   INIT(= "E155: Unknown sign: %s");
EXTERN Byte e_missing_sign_name[]
   INIT(= "E156: Missing sign name");
EXTERN Byte e_invalid_sign_id_nr[]
   INIT(= "E157: Invalid sign ID: %d");
EXTERN Byte e_invalid_buffer_name_str[]
   INIT(= "E158: Invalid buffer name: %s");
EXTERN Byte e_missing_sign_number[]
   INIT(= "E159: Missing sign number");
EXTERN Byte e_unknown_sign_command_str[]
   INIT(= "E160: Unknown sign command: %s");
EXTERN Byte e_breakpoint_not_found_str[]
   INIT(= "E161: Breakpoint not found: %s");
EXTERN Byte e_no_write_since_last_change_for_buffer_str[]
   INIT(= "E162: No write since last change for buffer \"%s\"");
EXTERN Byte e_there_is_only_one_file_to_edit[]
   INIT(= "E163: There is only one file to edit");
EXTERN Byte e_cannot_go_before_first_file[]
   INIT(= "E164: Cannot go before first file");
EXTERN Byte e_cannot_go_beyond_last_file[]
   INIT(= "E165: Cannot go beyond last file");
EXTERN Byte e_cant_open_linked_file_for_writing[]
   INIT(= "E166: Can't open linked file for writing");
EXTERN Byte e_finish_used_outside_of_sourced_file[]
   INIT(= "E168: :finish used outside of a sourced file");
EXTERN Byte e_command_too_recursive[]
   INIT(= "E169: Command too recursive");
EXTERN Byte e_missing_endwhile[]
   INIT(= "E170: Missing :endwhile");
EXTERN Byte e_missing_endfor[]
   INIT(= "E170: Missing :endfor");
EXTERN Byte e_missing_endif[]
   INIT(= "E171: Missing :endif");
EXTERN Byte e_missing_marker[]
   INIT(= "E172: Missing marker");

PLURAL_MSG(e_nr_more_file_to_edit, "E173: %d more file to edit",
      e_nr_more_files_to_edit, "E173: %d more files to edit")

EXTERN Byte e_command_already_exists_add_bang_to_replace_it_str[]
   INIT(= "E174: Command already exists: add ! to replace it: %s");
EXTERN Byte e_no_attribute_specified[]
   INIT(= "E175: No attribute specified");
EXTERN Byte e_invalid_number_of_arguments[]
   INIT(= "E176: Invalid number of arguments");
EXTERN Byte e_count_cannot_be_specified_twice[]
   INIT(= "E177: Count cannot be specified twice");
EXTERN Byte e_invalid_default_value_for_count[]
   INIT(= "E178: Invalid default value for count");
EXTERN Byte e_argument_required_for_str[]
   INIT(= "E179: Argument required for %s");
EXTERN Byte e_invalid_complete_value_str[]
   INIT(= "E180: Invalid complete value: %s");
EXTERN Byte e_invalid_address_type_value_str[]
   INIT(= "E180: Invalid address type value: %s");
EXTERN Byte e_invalidDecorationStr[]
   INIT(= "E181: Invalid decoration: %s");
EXTERN Byte e_invalid_command_name[]
   INIT(= "E182: Invalid command name");
EXTERN Byte e_user_defined_commands_must_start_with_an_uppercase_letter[]
   INIT(= "E183: User defined commands must start with an uppercase letter");
EXTERN Byte e_no_such_user_defined_command_str[]
   INIT(= "E184: No such user-defined command: %s");
EXTERN Byte e_cannot_find_color_scheme_str[]
   INIT(= "E185: Cannot find color scheme '%s'");
EXTERN Byte e_no_previous_directory[]
   INIT(= "E186: No previous directory");
EXTERN Byte e_directory_unknown[]
   INIT(= "E187: Directory unknown");
EXTERN Byte e_obtaining_window_position_not_implemented_for_this_platform[]
   INIT(= "E188: Obtaining window position not implemented for this platform");
EXTERN Byte e_str_exists_add_bang_to_override[]
   INIT(= "E189: \"%s\" exists (add ! to override)");
EXTERN Byte e_cannot_open_str_for_writing_2[]
   INIT(= "E190: Cannot open \"%s\" for writing");
EXTERN Byte e_argument_must_be_letter_or_forward_backward_quote[]
   INIT(= "E191: Argument must be a letter or forward/backward quote");
EXTERN Byte e_recursive_use_of_normal_too_deep[]
   INIT(= "E192: Recursive use of :normal too deep");
EXTERN Byte e_str_not_inside_function[]
   INIT(= "E193: %s not inside a function");
EXTERN Byte e_no_alternate_file_name_to_substitute_for_hash[]
   INIT(= "E194: No alternate file name to substitute for '#'");
EXTERN Byte e_cannot_open_eeglinfo_file_for_reading[]
   INIT(= "E195: Cannot open eeglinfo file for reading");
EXTERN Byte e_no_digraphs_version[]
   INIT(= "E196: No digraphs in this version");
EXTERN Byte e_cannot_set_language_to_str[]
   INIT(= "E197: Cannot set language to \"%s\"");
// E198 unused
EXTERN Byte e_active_window_or_buffer_changed_or_deleted[]
   INIT(= "E199: Active portal or book changed or deleted");
EXTERN Byte e_readpre_autocommands_made_file_unreadable[]
   INIT(= "E200: *ReadPre autocommands made the file unreadable");
EXTERN Byte e_readpre_autocommands_must_not_change_current_buffer[]
   INIT(= "E201: *ReadPre autocommands must not change current buffer");
EXTERN Byte e_conversion_mad_file_unreadable[]
   INIT(= "E202: Conversion made file unreadable!");
EXTERN Byte e_autocommands_deleted_or_unloaded_buffer_to_be_written[]
   INIT(= "E203: Autocommands deleted or unloaded buffer to be written");
EXTERN Byte e_autocommands_changed_number_of_lines_in_unexpected_way[]
   INIT(= "E204: Autocommand changed number of lines in unexpected way");
EXTERN Byte e_patchmode_cant_save_original_file[]
   INIT(= "E205: Patchmode: can't save original file");
EXTERN Byte e_patchmode_cant_touch_empty_original_file[]
   INIT(= "E206: Patchmode: can't touch empty original file");
EXTERN Byte e_cant_delete_backup_file[]
   INIT(= "E207: Can't delete backup file");
EXTERN Byte e_error_writing_to_str[]
   INIT(= "E208: Error writing to \"%s\"");
EXTERN Byte e_error_closing_str[]
   INIT(= "E209: Error closing \"%s\"");
EXTERN Byte e_error_reading_str[]
   INIT(= "E210: Error reading \"%s\"");
EXTERN Byte e_file_str_no_longer_available[]
   INIT(= "E211: File \"%s\" no longer available");
EXTERN Byte e_cant_open_file_for_writing[]
   INIT(= "E212: Can't open file for writing");
EXTERN Byte e_cannot_convert_add_bang_to_write_without_conversion[]
   INIT(= "E213: Cannot convert (add ! to write without conversion)");
EXTERN Byte e_cant_find_temp_file_for_writing[]
   INIT(= "E214: Can't find temp file for writing");
EXTERN Byte e_illegal_character_after_star_str[]
   INIT(= "E215: Illegal character after *: %s");
EXTERN Byte e_no_such_event_str[]
   INIT(= "E216: No such event: %s");
EXTERN Byte e_no_such_group_or_event_str[]
   INIT(= "E216: No such group or event: %s");
EXTERN Byte e_cant_execute_autocommands_for_all_events[]
   INIT(= "E217: Can't execute autocommands for ALL events");
EXTERN Byte e_autocommand_nesting_too_deep[]
   INIT(= "E218: Autocommand nesting too deep");
EXTERN Byte e_missing_open_curly[]
   INIT(= "E219: Missing {.");
EXTERN Byte e_missing_close_curly[]
   INIT(= "E220: Missing }.");
EXTERN Byte e_marker_cannot_start_with_lower_case_letter[]
   INIT(= "E221: Marker cannot start with lower case letter");
EXTERN Byte e_add_to_internal_buffer_that_was_already_read_from[]
   INIT(= "E222: Add to internal buffer that was already read from");
EXTERN Byte e_recursive_mapping[]
   INIT(= "E223: Recursive mapping");
EXTERN Byte e_global_abbreviation_already_exists_for_str[]
   INIT(= "E224: Global abbreviation already exists for %s");
EXTERN Byte e_global_mapping_already_exists_for_str[]
   INIT(= "E225: Global mapping already exists for %s");
EXTERN Byte e_abbreviation_already_exists_for_str[]
   INIT(= "E226: Abbreviation already exists for %s");
EXTERN Byte e_mapping_already_exists_for_str[]
   INIT(= "E227: Mapping already exists for %s");
EXTERN Byte e_makemap_illegal_mode[]
   INIT(= "E228: makemap: Illegal mode");

EXTERN Byte e_invalid_sign_text_str[]
   INIT(= "E239: Invalid sign text: %s");
EXTERN Byte e_unable_to_send_to_str[]
   INIT(= "E241: Unable to send to %s");
EXTERN Byte e_cant_split_portal_while_closing_another[]
   INIT(= "E242: Can't split a portal while closing another");
EXTERN Byte e_filechangedshell_autocommand_deleted_buffer[]
   INIT(= "E246: FileChangedShell autocommand deleted buffer");
EXTERN Byte e_no_registered_server_named_str[]
   INIT(= "E247: No registered server named \"%s\"");
EXTERN Byte e_failed_to_send_command_to_destination_program[]
   INIT(= "E248: Failed to send command to the destination program");
EXTERN Byte e_portal_layout_changed_unexpectedly[]
   INIT(= "E249: Portal layout changed unexpectedly");
EXTERN Byte e_eegl_instance_registry_property_is_badly_formed_deleted[]
   INIT(= "E251: EEGL instance registry property is badly formed.  Deleted!");
EXTERN Byte e_cannot_allocate_color_str[]
   INIT(= "E254: Cannot allocate color %s");
// E256 unused
EXTERN Byte e_cstag_tag_not_founc[]
   INIT(= "E257: cstag: Tag not found");
EXTERN Byte e_unable_to_send_to_client[]
   INIT(= "E258: Unable to send to client");
EXTERN Byte e_no_matches_found_for_cscope_query_str_of_str[]
   INIT(= "E259: No matches found for cscope query %s of %s");
EXTERN Byte e_missing_name_after_method[]
   INIT(= "E260: Missing name after ->");
EXTERN Byte e_cscope_connection_str_not_founc[]
   INIT(= "E261: Cscope connection %s not found");
EXTERN Byte e_error_reading_cscope_connection_nr[]
   INIT(= "E262: Error reading cscope connection %d");
EXTERN Byte e_no_white_space_allowed_before_parenthesis[]
   INIT(= "E274: No white space allowed before parenthesis");
EXTERN Byte e_cannot_add_text_property_to_unloaded_buffer[]
   INIT(= "E275: Cannot add text property to unloaded buffer");
EXTERN Byte e_cannot_use_function_as_method_str[]
   INIT(= "E276: Cannot use function as a method: %s");
EXTERN Byte e_unable_to_read_server_reply[]
   INIT(= "E277: Unable to read a server reply");
// E278 unused
// E281 unused
EXTERN Byte e_cannot_read_from_str_2[]
   INIT(= "E282: Cannot read from \"%s\"");
EXTERN Byte e_no_marks_matching_str[]
   INIT(= "E283: No marks matching \"%s\"");
EXTERN Byte e_list_or_number_required[]
   INIT(= "E290: List or number required");
// E291 unused
EXTERN Byte e_invalid_count_for_del_bytes_nr[]
   INIT(= "E292: Invalid count for del_bytes(): %ld");
EXTERN Byte e_block_was_not_locked[]
   INIT(= "E293: Block was not locked");
EXTERN Byte e_seek_error_in_swap_file_read[]
   INIT(= "E294: Seek error in swap file read");
EXTERN Byte e_read_error_in_swap_file[]
   INIT(= "E295: Read error in swap file");
EXTERN Byte e_seek_error_in_swap_file_write[]
   INIT(= "E296: Seek error in swap file write");
EXTERN Byte e_write_error_in_swap_file[]
   INIT(= "E297: Write error in swap file");
EXTERN Byte e_didnt_get_block_nr_zero[]
   INIT(= "E298: Didn't get block nr 0?");
EXTERN Byte e_didnt_get_block_nr_one[]
   INIT(= "E298: Didn't get block nr 1?");
EXTERN Byte e_didnt_get_block_nr_two[]
   INIT(= "E298: Didn't get block nr 2?");
EXTERN Byte e_swap_file_already_exists_symlink_attack[]
   INIT(= "E300: Swap file already exists (symlink attack?)");
EXTERN Byte e_oops_lost_the_swap_file[]
   INIT(= "E301: Oops, lost the swap file!!!");
EXTERN Byte e_could_not_rename_swap_file[]
   INIT(= "E302: Could not rename swap file");
EXTERN Byte e_unable_to_open_swap_file_for_str_recovery_impossible[]
   INIT(= "E303: Unable to open swap file for \"%s\", recovery impossible");
EXTERN Byte e_ml_upd_block0_didnt_get_block_zero[]
   INIT(= "E304: ml_upd_block0(): Didn't get block 0??");
EXTERN Byte e_no_swap_file_found_for_str[]
   INIT(= "E305: No swap file found for %s");
EXTERN Byte e_cannot_open_str[]
   INIT(= "E306: Cannot open %s");
EXTERN Byte e_str_does_not_look_like_eegl_swap_file[]
   INIT(= "E307: %s does not look like an Eegl swap file");
EXTERN Byte e_warning_original_file_may_have_been_changed[]
   INIT(= "E308: Warning: Original file may have been changed");
EXTERN Byte e_unable_to_read_block_one_from_str[]
   INIT(= "E309: Unable to read block 1 from %s");
EXTERN Byte e_block_one_id_wrong_str_not_swp_file[]
   INIT(= "E310: Block 1 ID wrong (%s not a .swp file?)");
EXTERN Byte e_recovery_interrupted[]
   INIT(= "E311: Recovery Interrupted");
EXTERN Byte e_errors_detected_while_recovering_look_for_lines_starting_with_questions[]
   INIT(= "E312: Errors detected while recovering; look for lines starting with ???");
EXTERN Byte e_cannot_preserve_there_is_no_swap_file[]
   INIT(= "E313: Cannot preserve, there is no swap file");
EXTERN Byte e_preserve_failed[]
   INIT(= "E314: Preserve failed");
EXTERN Byte e_ml_get_invalid_lnum_nr[]
   INIT(= "E315: ml_get: Invalid lnum: %ld");
EXTERN Byte e_ml_get_cannot_find_line_nr_in_buffer_nr_str[]
   INIT(= "E316: ml_get: Cannot find line %ld in buffer %d %s");
EXTERN Byte e_pointer_block_id_wrong[]
   INIT(= "E317: Pointer block id wrong");
EXTERN Byte e_pointer_block_id_wrong_two[]
   INIT(= "E317: Pointer block id wrong 2");
EXTERN Byte e_pointer_block_id_wrong_three[]
   INIT(= "E317: Pointer block id wrong 3");
EXTERN Byte e_pointer_block_id_wrong_four[]
   INIT(= "E317: Pointer block id wrong 4");
EXTERN Byte e_updated_too_many_blocks[]
   INIT(= "E318: Updated too many blocks?");
EXTERN Byte e_sorry_command_is_not_available_in_this_version[]
   INIT(= "E319: Sorry, the command is not available in this version");
EXTERN Byte e_cannot_find_line_nr[]
   INIT(= "E320: Cannot find line %ld");
EXTERN Byte e_could_not_reload_str[]
   INIT(= "E321: Could not reload \"%s\"");
EXTERN Byte e_line_number_out_of_range_nr_past_the_end[]
   INIT(= "E322: Line number out of range: %ld past the end");
EXTERN Byte e_line_count_wrong_in_block_nr[]
   INIT(= "E323: Line count wrong in block %ld");
EXTERN Byte e_attention[]
   INIT(= "E325: ATTENTION");
EXTERN Byte e_too_many_swap_files_found[]
   INIT(= "E326: Too many swap files found");
EXTERN Byte e_part_of_menu_item_path_is_not_sub_menu[]
   INIT(= "E327: Part of menu-item path is not sub-menu");
EXTERN Byte e_menu_only_exists_in_another_mode[]
   INIT(= "E328: Menu only exists in another mode");
EXTERN Byte e_no_menu_str[]
   INIT(= "E329: No menu \"%s\"");
EXTERN Byte e_menu_path_must_not_lead_to_sub_menu[]
   INIT(= "E330: Menu path must not lead to a sub-menu");
EXTERN Byte e_must_not_add_menu_items_directly_to_menu_bar[]
   INIT(= "E331: Must not add menu items directly to menu bar");
EXTERN Byte e_separator_cannot_be_part_of_menu_path[]
   INIT(= "E332: Separator cannot be part of a menu path");
EXTERN Byte e_menu_path_must_lead_to_menu_item[]
   INIT(= "E333: Menu path must lead to a menu item");
EXTERN Byte e_menu_not_found_str[]
   INIT(= "E334: Menu not found: %s");
EXTERN Byte e_menu_not_defined_for_str_mode[]
   INIT(= "E335: Menu not defined for %s mode");
EXTERN Byte e_menu_path_must_lead_to_sub_menu[]
   INIT(= "E336: Menu path must lead to a sub-menu");
EXTERN Byte e_menu_not_found_check_menu_names[]
   INIT(= "E337: Menu not found - check menu names");
EXTERN Byte e_pattern_too_long[]
   INIT(= "E339: Pattern too long");
EXTERN Byte e_internal_error_please_report_a_bug[]
   INIT(= "E340: Internal error; if you can reproduce please report a bug");
EXTERN Byte e_internal_error_lalloc_zero[]
   INIT(= "E341: Internal error: lalloc(0, )");
EXTERN Byte e_out_of_memory_allocating_nr_bytes[]
   INIT(= "E342: Out of memory!  (allocating %lu bytes)");
EXTERN Byte e_invalid_path_number_must_be_at_end_of_path_or_be_followed_by_str[]
   INIT(= "E343: Invalid path: '**[number]' must be at the end of the path or be followed by '%s'.");
EXTERN Byte e_cant_find_directory_str_in_cdpath[]
   INIT(= "E344: Can't find directory \"%s\" in cdpath");
EXTERN Byte e_cant_find_file_str_in_path[]
   INIT(= "E345: Can't find file \"%s\" in path");
EXTERN Byte e_no_more_directory_str_found_in_cdpath[]
   INIT(= "E346: No more directory \"%s\" found in cdpath");
EXTERN Byte e_no_more_file_str_found_in_path[]
   INIT(= "E347: No more file \"%s\" found in path");
EXTERN Byte e_no_string_under_cursor[]
   INIT(= "E348: No string under cursor");
EXTERN Byte e_no_identifier_under_cursor[]
   INIT(= "E349: No identifier under cursor");
EXTERN Byte e_cannot_create_fold_with_current_foldmethod[]
   INIT(= "E350: Cannot create fold with current 'foldmethod'");
EXTERN Byte e_cannot_delete_fold_with_current_foldmethod[]
   INIT(= "E351: Cannot delete fold with current 'foldmethod'");
EXTERN Byte e_cannot_erase_folds_with_current_foldmethod[]
   INIT(= "E352: Cannot erase folds with current 'foldmethod'");
EXTERN Byte e_nothing_in_register_str[]
   INIT(= "E353: Nothing in register %s");
EXTERN Byte e_invalid_register_name_str[]
   INIT(= "E354: Invalid register name: '%s'");
EXTERN Byte e_unknown_option_str_2[]
   INIT(= "E355: Unknown option: %s");
EXTERN Byte e_refOfError[]
   INIT(= "E356: refOf() ERROR");
EXTERN Byte e_langmap_matching_character_missing_for_str[]
   INIT(= "E357: 'langmap': Matching character missing for %s");
EXTERN Byte e_langmap_extra_characters_after_semicolon_str[]
   INIT(= "E358: 'langmap': Extra characters after semicolon: %s");
EXTERN Byte e_screen_mode_setting_not_supported[]
   INIT(= "E359: Screen mode setting not supported");
// E361 unused
EXTERN Byte e_using_boolean_value_as_float[]
   INIT(= "E362: Using a boolean value as a Float");
EXTERN Byte e_pattern_uses_more_memory_than_maxmempattern[]
   INIT(= "E363: Pattern uses more memory than 'maxmempattern'");
EXTERN Byte e_not_allowed_to_enter_popup_portal[]
   INIT(= "E366: Not allowed to enter a popup portal");
EXTERN Byte e_no_such_group_str[]
   INIT(= "E367: No such group: \"%s\"");
EXTERN Byte e_invalid_item_in_str_brackets[]
   INIT(= "E369: Invalid item in %s%%[]");
EXTERN Byte e_could_not_load_library_str_str[]
   INIT(= "E370: Could not load library %s: %s");
EXTERN Byte e_too_many_chr_in_format_string[]
   INIT(= "E372: Too many %%%c in format string");
EXTERN Byte e_unexpected_chr_in_format_str[]
   INIT(= "E373: Unexpected %%%c in format string");
EXTERN Byte e_missing_rsb_in_format_string[]
   INIT(= "E374: Missing ] in format string");
EXTERN Byte e_unsupported_chr_in_format_string[]
   INIT(= "E375: Unsupported %%%c in format string");
EXTERN Byte e_invalid_chr_in_format_string_prefix[]
   INIT(= "E376: Invalid %%%c in format string prefix");
EXTERN Byte e_invalid_chr_in_format_string[]
   INIT(= "E377: Invalid %%%c in format string");
EXTERN Byte e_errorformat_contains_no_pattern[]
   INIT(= "E378: 'errorformat' contains no pattern");
EXTERN Byte e_missing_or_empty_directory_name[]
   INIT(= "E379: Missing or empty directory name");
EXTERN Byte e_at_bottom_of_quickfix_stack[]
   INIT(= "E380: At bottom of quickfix stack");
EXTERN Byte e_at_top_of_quickfix_stack[]
   INIT(= "E381: At top of quickfix stack");
EXTERN Byte e_cannot_write_buftype_option_is_set[]
   INIT(= "E382: Cannot write, 'buftype' option is set");
EXTERN Byte e_invalid_search_string_str[]
   INIT(= "E383: Invalid search string: %s");
EXTERN Byte e_search_hit_top_without_match_for_str[]
   INIT(= "E384: Search hit TOP without match for: %s");
EXTERN Byte e_expected_question_or_slash_after_semicolon[]
   INIT(= "E386: Expected '?' or '/'  after ';'");
EXTERN Byte e_match_is_on_current_line[]
   INIT(= "E387: Match is on current line");
EXTERN Byte e_couldnt_find_definition[]
   INIT(= "E388: Couldn't find definition");
EXTERN Byte e_couldnt_find_pattern[]
   INIT(= "E389: Couldn't find pattern");
EXTERN Byte e_illegal_argument_str_2[]
   INIT(= "E390: Illegal argument: %s");
EXTERN Byte e_no_such_syntax_cluster_str_1[]
   INIT(= "E391: No such syntax cluster: %s");
EXTERN Byte e_no_such_syntax_cluster_str_2[]
   INIT(= "E392: No such syntax cluster: %s");
EXTERN Byte e_groupthere_not_accepted_here[]
   INIT(= "E393: group[t]here not accepted here");
EXTERN Byte e_didnt_find_region_item_for_str[]
   INIT(= "E394: Didn't find region item for %s");
EXTERN Byte e_contains_argument_not_accepted_here[]
   INIT(= "E395: Contains argument not accepted here");
// E396 unused
EXTERN Byte e_filename_required[]
   INIT(= "E397: Filename required");
EXTERN Byte e_missing_equal_str[]
   INIT(= "E398: Missing '=': %s");
EXTERN Byte e_not_enough_arguments_syntax_region_str[]
   INIT(= "E399: Not enough arguments: syntax region %s");
EXTERN Byte e_no_cluster_specified[]
   INIT(= "E400: No cluster specified");
EXTERN Byte e_pattern_delimiter_not_found_str[]
   INIT(= "E401: Pattern delimiter not found: %s");
EXTERN Byte e_garbage_after_pattern_str[]
   INIT(= "E402: Garbage after pattern: %s");
EXTERN Byte e_syntax_sync_line_continuations_pattern_specified_twice[]
   INIT(= "E403: syntax sync: Line continuations pattern specified twice");
EXTERN Byte e_illegal_arguments_str[]
   INIT(= "E404: Illegal arguments: %s");
EXTERN Byte e_missing_equal_sign_str[]
   INIT(= "E405: Missing equal sign: %s");
EXTERN Byte e_empty_argument_str[]
   INIT(= "E406: Empty argument: %s");
EXTERN Byte e_str_not_allowed_here[]
   INIT(= "E407: %s not allowed here");
EXTERN Byte e_str_must_be_first_in_contains_list[]
   INIT(= "E408: %s must be first in contains list");
EXTERN Byte e_unknown_group_name_str[]
   INIT(= "E409: Unknown group name: %s");
EXTERN Byte e_invalid_syntax_subcommand_str[]
   INIT(= "E410: Invalid :syntax subcommand: %s");
EXTERN Byte e_hilite_group_name_not_found_str[]
   INIT(= "E411: Hilite group not found: %s");
EXTERN Byte e_not_enough_arguments_highlight_link_str[]
   INIT(= "E412: Not enough arguments: \":highlight link %s\"");
EXTERN Byte e_too_many_arguments_highlight_link_str[]
   INIT(= "E413: Too many arguments: \":highlight link %s\"");
EXTERN Byte e_group_has_settings_highlight_link_ignored[]
   INIT(= "E414: Group has settings, highlight link ignored");
EXTERN Byte e_unexpected_equal_sign_str[]
   INIT(= "E415: Unexpected equal sign: %s");
EXTERN Byte e_missing_equal_sign_str_2[]
   INIT(= "E416: Missing equal sign: %s");
EXTERN Byte e_missing_argument_str[]
   INIT(= "E417: Missing argument: %s");
EXTERN Byte e_illegal_value_str[]
   INIT(= "E418: Illegal value: %s");
EXTERN Byte e_im_a_teapot[]
   INIT(= "E418: I'm a teapot");
EXTERN Byte e_fg_color_unknown[]
   INIT(= "E419: FG color unknown");
EXTERN Byte e_bg_color_unknown[]
   INIT(= "E420: BG color unknown");
EXTERN Byte e_color_name_or_number_not_recognized_str[]
   INIT(= "E421: Color name or number not recognized: %s");
EXTERN Byte e_terminal_code_too_long_str[]
   INIT(= "E422: Terminal code too long: %s");
EXTERN Byte e_illegal_argument_str_3[]
   INIT(= "E423: Illegal argument: %s");
EXTERN Byte e_too_many_different_highlighting_decos_in_use[]
   INIT(= "E424: Too many different highlighting decorations in use");
EXTERN Byte e_cannot_go_before_first_matching_tag[]
   INIT(= "E425: Cannot go before first matching tag");
EXTERN Byte e_tag_not_found_str[]
   INIT(= "E426: Tag not found: %s");
EXTERN Byte e_there_is_only_one_matching_tag[]
   INIT(= "E427: There is only one matching tag");
EXTERN Byte e_cannot_go_beyond_last_matching_tag[]
   INIT(= "E428: Cannot go beyond last matching tag");
EXTERN Byte e_file_str_does_not_exist[]
   INIT(= "E429: File \"%s\" does not exist");
EXTERN Byte e_format_error_in_tags_file_str[]
   INIT(= "E431: Format error in tags file \"%s\"");
EXTERN Byte e_tags_file_not_sorted_str[]
   INIT(= "E432: Tags file not sorted: %s");
EXTERN Byte e_no_tags_file[]
   INIT(= "E433: No tags file");
EXTERN Byte e_cannot_find_tag_pattern[]
   INIT(= "E434: Can't find tag pattern");
EXTERN Byte e_couldnt_find_tag_just_guessing[]
   INIT(= "E435: Couldn't find tag, just guessing!");
EXTERN Byte e_no_str_entry_in_termcap[]
   INIT(= "E436: No \"%s\" entry in termcap");
EXTERN Byte e_terminal_capability_cm_required[]
   INIT(= "E437: Terminal capability \"cm\" required");
EXTERN Byte e_u_undo_line_numbers_wrong[]
   INIT(= "E438: u_undo: Line numbers wrong");
EXTERN Byte e_undo_list_corrupt[]
   INIT(= "E439: Undo list corrupt");
EXTERN Byte e_undo_line_missing[]
   INIT(= "E440: Undo line missing");
EXTERN Byte e_there_is_no_preview_portal[]
   INIT(= "E441: There is no preview portal");
EXTERN Byte e_cant_split_topleft_and_botright_at_the_same_time[]
   INIT(= "E442: Can't split topleft and botright at the same time");
EXTERN Byte e_cannot_rotate_when_another_portal_is_split[]
   INIT(= "E443: Cannot rotate when another portal is split");
EXTERN Byte e_cannot_close_last_portal[]
   INIT(= "E444: Cannot close last portal of window");
EXTERN Byte e_other_portal_contains_changes[]
   INIT(= "E445: Other portal contains changes");
EXTERN Byte e_no_file_name_under_cursor[]
   INIT(= "E446: No file name under cursor");
EXTERN Byte e_cant_find_file_str_in_path_2[]
   INIT(= "E447: Can't find file \"%s\" in path");
EXTERN Byte e_could_not_load_library_function_str[]
   INIT(= "E448: Could not load library function %s");
EXTERN Byte e_invalid_expression_received[]
   INIT(= "E449: Invalid expression received");
EXTERN Byte e_buffer_number_text_or_list_required[]
   INIT(= "E450: Book number, text or a list required");
EXTERN Byte e_expected_right_curly_str[] //{
   INIT(= "E451: Expected }: %s");
EXTERN Byte e_double_semicolon_in_list_of_variables[]
   INIT(= "E452: Double ; in list of variables");
EXTERN Byte e_ul_color_unknown[]
   INIT(= "E453: UL color unknown");
EXTERN Byte e_function_list_was_modified[]
   INIT(= "E454: Function list was modified");
EXTERN Byte e_cannot_go_back_to_previous_directory[]
   INIT(= "E459: Cannot go back to previous directory");
EXTERN Byte e_entries_missing_in_mapset_dict_argument[]
   INIT(= "E460: Entries missing in mapset() dict argument");
EXTERN Byte e_illegal_variable_name_str[]
   INIT(= "E461: Illegal variable name: %s");
EXTERN Byte e_could_not_prepare_for_reloading_str[]
   INIT(= "E462: Could not prepare for reloading \"%s\"");
EXTERN Byte e_ambiguous_use_of_user_defined_command[]
   INIT(= "E464: Ambiguous use of user-defined command");
EXTERN Byte e_ambiguous_use_of_user_defined_command_str[]
   INIT(= "E464: Ambiguous use of user-defined command: %s");
EXTERN Byte e_winsize_requires_two_number_arguments[]
   INIT(= "E465: :winsize requires two number arguments");
EXTERN Byte e_winpos_requires_two_number_arguments[]
   INIT(= "E466: :winpos requires two number arguments");
EXTERN Byte e_custom_completion_requires_function_argument[]
   INIT(= "E467: Custom completion requires a function argument");
EXTERN Byte e_completion_argument_only_allowed_for_custom_completion[]
   INIT(= "E468: Completion argument only allowed for custom completion");
EXTERN Byte e_invalid_cscopequickfix_flag_chr_for_chr[]
   INIT(= "E469: Invalid cscopequickfix flag %c for %c");
EXTERN Byte e_command_aborted[]
   INIT(= "E470: Command aborted");
EXTERN Byte e_argument_required[]
   INIT(= "E471: Argument required");
EXTERN Byte e_command_failed[]
   INIT(= "E472: Command failed");
EXTERN Byte e_internal_error_in_regexp[]
   INIT(= "E473: Internal error in regexp");
EXTERN Byte e_invalid_argument[]
   INIT(= "E474: Invalid argument");
EXTERN Byte e_invalid_argument_str[]
   INIT(= "E475: Invalid argument: %s");
EXTERN Byte e_invalid_value_for_argument_str[]
   INIT(= "E475: Invalid value for argument %s");
EXTERN Byte e_invalid_value_for_argument_str_str[]
   INIT(= "E475: Invalid value for argument %s: %s");
EXTERN Byte e_invalid_command[]
   INIT(= "E476: Invalid command");
EXTERN Byte e_invalid_command_str[]
   INIT(= "E476: Invalid command: %s");
EXTERN Byte e_invalid_command_str_expected_str[]
   INIT(= "E476: Invalid command: %s, expected %s");
EXTERN Byte e_no_bang_allowed[]
   INIT(= "E477: No ! allowed");
EXTERN Byte e_dont_panic[]
   INIT(= "E478: Don't panic!");
EXTERN Byte e_no_match[]
   INIT(= "E479: No match");
EXTERN Byte e_no_match_str_2[]
   INIT(= "E480: No match: %s");
EXTERN Byte e_no_range_allowed[]
   INIT(= "E481: No range allowed");
EXTERN Byte e_cant_create_file_str[]
   INIT(= "E482: Can't create file %s");
EXTERN Byte e_cant_get_temp_file_name[]
   INIT(= "E483: Can't get temp file name");
EXTERN Byte e_cant_open_file_str[]
   INIT(= "E484: Can't open file %s");
EXTERN Byte e_cant_read_file_str[]
   INIT(= "E485: Can't read file %s");
EXTERN Byte e_pattern_not_found[]
   INIT(= "E486: Pattern not found");
EXTERN Byte e_pattern_not_found_str[]
   INIT(= "E486: Pattern not found: %s");
EXTERN Byte e_argument_must_be_positive[]
   INIT(= "E487: Argument must be positive");
EXTERN Byte e_argument_must_be_positive_str[]
   INIT(= "E487: Argument must be positive: %s");
EXTERN Byte e_trailing_characters[]
   INIT(= "E488: Trailing characters");
EXTERN Byte e_trailing_characters_str[]
   INIT(= "E488: Trailing characters: %s");
EXTERN Byte e_no_call_stack_to_substitute_for_stack[]
   INIT(= "E489: No call stack to substitute for \"<stack>\"");
EXTERN Byte e_no_fold_found[]
   INIT(= "E490: No fold found");
EXTERN Byte e_json_decode_error_at_str[]
   INIT(= "E491: JSON decode error at '%s'");
EXTERN Byte e_not_an_editor_command[]
   INIT(= "E492: Not an editor command");
EXTERN Byte e_backwards_range_given[]
   INIT(= "E493: Backwards range given");
EXTERN Byte e_use_w_or_w_gt_gt[]
   INIT(= "E494: Use w or w>>");
EXTERN Byte e_no_autocommand_file_name_to_substitute_for_afile[]
   INIT(= "E495: No autocommand file name to substitute for \"<afile>\"");
EXTERN Byte e_no_autocommand_buffer_number_to_substitute_for_abuf[]
   INIT(= "E496: No autocommand buffer number to substitute for \"<abuf>\"");
EXTERN Byte e_no_autocommand_match_name_to_substitute_for_amatch[]
   INIT(= "E497: No autocommand match name to substitute for \"<amatch>\"");
EXTERN Byte e_no_source_file_name_to_substitute_for_sfile[]
   INIT(= "E498: No :source file name to substitute for \"<sfile>\"");
EXTERN Byte e_empty_file_name_for_percent_or_hash_only_works_with_ph[]
   // xgettext:no-c-format
   INIT(= "E499: Empty file name for '%' or '#', only works with \":p:h\"");
EXTERN Byte e_evaluates_to_an_empty_string[]
   INIT(= "E500: Evaluates to an empty string");
EXTERN Byte e_at_end_of_file[]
   INIT(= "E501: At end-of-file");
   // E502
EXTERN Byte e_is_a_directory[]
   INIT(= "is a directory");
   // E503
EXTERN Byte e_is_not_file_or_writable_device[]
   INIT(= "is not a file or writable device");
EXTERN Byte e_str_is_not_file_or_writable_device[]
   INIT(= "E503: \"%s\" is not a file or writable device");
EXTERN Byte e_coffee_currently_not_available[]
   INIT(= "E503: Coffee is currently not available");
   // E504
   // E505
EXTERN Byte e_is_read_only_add_bang_to_override[]
   INIT(= "is read-only (add ! to override)");
EXTERN Byte e_str_is_read_only_add_bang_to_override[]
   INIT(= "E505: \"%s\" is read-only (add ! to override)");
EXTERN Byte e_cant_write_to_backup_file_add_bang_to_override[]
   INIT(= "E506: Can't write to backup file (add ! to override)");
EXTERN Byte e_close_error_for_backup_file_add_bang_to_write_anyway[]
   INIT(= "E507: Close error for backup file (add ! to write anyway)");
EXTERN Byte e_cant_read_file_for_backup_add_bang_to_write_anyway[]
   INIT(= "E508: Can't read file for backup (add ! to write anyway)");
EXTERN Byte e_cannot_create_backup_file_add_bang_to_write_anyway[]
   INIT(= "E509: Cannot create backup file (add ! to override)");
EXTERN Byte e_cant_make_backup_file_add_bang_to_write_anyway[]
   INIT(= "E510: Can't make backup file (add ! to write anyway)");
EXTERN Byte e_close_failed[]
   INIT(= "E512: Close failed");
EXTERN Byte e_write_error_conversion_failed_make_fenc_empty_to_override[]
   INIT(= "E513: Write error, conversion failed (make 'fenc' empty to override)");
EXTERN Byte e_write_error_conversion_failed_in_line_nr_make_fenc_empty_to_override[]
   INIT(= "E513: Write error, conversion failed in line %ld (make 'fenc' empty to override)");
EXTERN Byte e_write_error_file_system_full[]
   INIT(= "E514: Write error (file system full?)");
EXTERN Byte e_no_buffers_were_unloaded[]
   INIT(= "E515: No buffers were unloaded");
EXTERN Byte e_no_buffers_were_deleted[]
   INIT(= "E516: No buffers were deleted");
EXTERN Byte e_no_buffers_were_wiped_out[]
   INIT(= "E517: No buffers were wiped out");
EXTERN Byte e_unknown_option[]
   INIT(= "E518: Unknown option");
EXTERN Byte e_option_not_supported[]
   INIT(= "E519: Option not supported");
EXTERN Byte e_number_required_after_equal[]
   INIT(= "E521: Number required after =");
EXTERN Byte e_number_required_after_str_equal_str[]
   INIT(= "E521: Number required: &%s = '%s'");
EXTERN Byte e_not_found_in_termcap[]
   INIT(= "E522: Not found in termcap");
EXTERN Byte e_not_allowed_here[]
   INIT(= "E523: Not allowed here");
EXTERN Byte e_missing_colon[]
   INIT(= "E524: Missing colon");
EXTERN Byte e_zero_length_string[]
   INIT(= "E525: Zero length string");
EXTERN Byte e_missing_number_after_angle_str_angle[]
   INIT(= "E526: Missing number after <%s>");
EXTERN Byte e_missing_comma[]
   INIT(= "E527: Missing comma");
EXTERN Byte e_must_specify_a_value[]
   INIT(= "E528: Must specify a ' value");
EXTERN Byte e_cannot_set_term_to_empty_string[]
   INIT(= "E529: Cannot set 'term' to empty string");
EXTERN Byte e_illegal_character_after_chr[]
   INIT(= "E535: Illegal character after <%c>");
EXTERN Byte e_comma_required[]
   INIT(= "E536: Comma required");
EXTERN Byte e_commentstring_must_be_empty_or_contain_str[]
   INIT(= "E537: 'commentstring' must be empty or contain %s");
EXTERN Byte e_pattern_found_in_every_line_str[]
   INIT(= "E538: Pattern found in every line: %s");
EXTERN Byte e_illegal_character_str[]
   INIT(= "E539: Illegal character <%s>");
EXTERN Byte e_unclosed_expression_sequence[]
   INIT(= "E540: Unclosed expression sequence");
// E541 unused
EXTERN Byte e_unbalanced_groups[]
   INIT(= "E542: Unbalanced groups");
EXTERN Byte e_keymap_file_not_found[]
   INIT(= "E544: Keymap file not found");
EXTERN Byte e_missing_colon_2[]
   INIT(= "E545: Missing colon");
EXTERN Byte e_illegal_mode[]
   INIT(= "E546: Illegal mode");
EXTERN Byte e_digit_expected[]
   INIT(= "E548: Digit expected");
EXTERN Byte e_illegal_percentage[]
   INIT(= "E549: Illegal percentage");
EXTERN Byte e_no_more_items[]
   INIT(= "E553: No more items");
EXTERN Byte e_syntax_error_in_str_curlies[]
   INIT(= "E554: Syntax error in %s{...}");
EXTERN Byte e_at_bottom_of_tag_stack[]
   INIT(= "E555: At bottom of tag stack");
EXTERN Byte e_at_top_of_tag_stack[]
   INIT(= "E556: At top of tag stack");
EXTERN Byte e_cannot_open_termcap_file[]
   INIT(= "E557: Cannot open termcap file");
EXTERN Byte e_terminal_entry_not_found_in_terminfo[]
   INIT(= "E558: Terminal entry not found in terminfo");
EXTERN Byte e_usage_cscope_str[]
   INIT(= "E560: Usage: cs[cope] %s");
EXTERN Byte e_unknown_cscope_search_type[]
   INIT(= "E561: Unknown cscope search type");
EXTERN Byte e_usage_cstag_ident[]
   INIT(= "E562: Usage: cstag <ident>");
EXTERN Byte e_stat_str_error_nr[]
   INIT(= "E563: stat(%s) error: %d");
EXTERN Byte e_str_is_not_directory_or_valid_cscope_database[]
   INIT(= "E564: %s is not a directory or a valid cscope database");
EXTERN Byte e_not_allowed_to_change_text_or_change_portal[]
   INIT(= "E565: Not allowed to change text or change portal");
EXTERN Byte e_could_not_create_cscope_pipes[]
   INIT(= "E566: Could not create cscope pipes");
EXTERN Byte e_no_cscope_connections[]
   INIT(= "E567: No cscope connections");
EXTERN Byte e_duplicate_cscope_database_not_added[]
   INIT(= "E568: Duplicate cscope database not added");
// E569 unused
EXTERN Byte e_fatal_error_in_cs_manage_matches[]
   INIT(= "E570: Fatal error in cs_manage_matches");
EXTERN Byte e_invalid_server_id_used_str[]
   INIT(= "E573: Invalid server id used: %s");
EXTERN Byte e_unknown_register_type_nr[]
   INIT(= "E574: Unknown register type %d");
   // E575
EXTERN Byte e_illegal_starting_char[]
   INIT(= "Illegal starting char");
   // E576
EXTERN Byte e_nonr_missing_gt[]
   INIT(= "Missing '>'");
   // E577
EXTERN Byte e_illegal_register_name[]
   INIT(= "Illegal register name");
// E578 unused
EXTERN Byte e_if_nesting_too_deep[]
   INIT(= "E579: :if nesting too deep");
EXTERN Byte e_block_nesting_too_deep[]
   INIT(= "E579: Block nesting too deep");
EXTERN Byte e_endif_without_if[]
   INIT(= "E580: :endif without :if");
EXTERN Byte e_else_without_if[]
   INIT(= "E581: :else without :if");
EXTERN Byte e_elseif_without_if[]
   INIT(= "E582: :elseif without :if");
EXTERN Byte e_multiple_else[]
   INIT(= "E583: Multiple :else");
EXTERN Byte e_elseif_after_else[]
   INIT(= "E584: :elseif after :else");
EXTERN Byte e_while_for_nesting_too_deep[]
   INIT(= "E585: :while/:for nesting too deep");
EXTERN Byte e_continue_without_while_or_for[]
   INIT(= "E586: :continue without :while or :for");
EXTERN Byte e_break_without_while_or_for[]
   INIT(= "E587: :break without :while or :for");
EXTERN Byte e_endwhile_without_while[]
   INIT(= "E588: :endwhile without :while");
EXTERN Byte e_endfor_without_for[]
   INIT(= "E588: :endfor without :for");
EXTERN Byte e_backupext_and_patchmode_are_equal[]
   INIT(= "E589: 'backupext' and 'patchmode' are equal");
EXTERN Byte e_winheight_cannot_be_smaller_than_winminheight[]
   INIT(= "E591: 'winheight' cannot be smaller than 'winminheight'");
EXTERN Byte e_winwidth_cannot_be_smaller_than_winminwidth[]
   INIT(= "E592: 'winwidth' cannot be smaller than 'winminwidth'");
EXTERN Byte e_need_at_least_nr_lines[]
   INIT(= "E593: Need at least %d lines");
EXTERN Byte e_need_at_least_nr_columns[]
   INIT(= "E594: Need at least %d columns");
EXTERN Byte e_showbreak_contains_unprintable_or_wide_character[]
   INIT(= "E595: 'showbreak' contains unprintable or wide character");
EXTERN Byte e_missing_endtry[]
   INIT(= "E600: Missing :endtry");
EXTERN Byte e_try_nesting_too_deep[]
   INIT(= "E601: :try nesting too deep");
EXTERN Byte e_endtry_without_try[]
   INIT(= "E602: :endtry without :try");
EXTERN Byte e_catch_without_try[]
   INIT(= "E603: :catch without :try");
EXTERN Byte e_catch_after_finally[]
   INIT(= "E604: :catch after :finally");
EXTERN Byte e_exception_not_caught_str[]
   INIT(= "E605: Exception not caught: %s");
EXTERN Byte e_finally_without_try[]
   INIT(= "E606: :finally without :try");
EXTERN Byte e_multiple_finally[]
   INIT(= "E607: Multiple :finally");
EXTERN Byte e_cannot_throw_exceptions_with_eegl_prefix[]
   INIT(= "E608: Cannot :throw exceptions with 'Eegl' prefix");
EXTERN Byte e_cscope_error_str[]
   INIT(= "E609: Cscope error: %s");
EXTERN Byte e_no_argument_to_delete[]
   INIT(= "E610: No argument to delete");
EXTERN Byte e_using_special_as_number[]
   INIT(= "E611: Using a Special as a Number");
EXTERN Byte e_too_many_signs_defined[]
   INIT(= "E612: Too many signs defined");
// E614 unused (deleted)
// E615 unused
EXTERN Byte e_object_required_for_argument_nr[]
   INIT(= "E616: Object required for argument %d");
EXTERN Byte e_could_not_fork_for_cscope[]
   INIT(= "E622: Could not fork for cscope");
EXTERN Byte e_str_write_while_not_connected[]
   INIT(= "E630: %s(): Write while not connected");
EXTERN Byte e_str_write_failed[]
   INIT(= "E631: %s(): Write failed");
// E653 unused
EXTERN Byte e_missing_delimiter_after_search_pattern_str[]
   INIT(= "E654: Missing delimiter after search pattern: %s");
EXTERN Byte e_too_many_symbolic_links_cycle[]
   INIT(= "E655: Too many symbolic links (cycle?)");
EXTERN Byte e_sorry_no_str_help_for_str[]
   INIT(= "E661: Sorry, no '%s' help for %s");
EXTERN Byte e_at_start_of_changelist[]
   INIT(= "E662: At start of changelist");
EXTERN Byte e_at_end_of_changelist[]
   INIT(= "E663: At end of changelist");
EXTERN Byte e_changelist_is_empty[]
   INIT(= "E664: Changelist is empty");
EXTERN Byte e_compiler_not_supported_str[]
   INIT(= "E666: Compiler not supported: %s");
EXTERN Byte e_fsync_failed[]
   INIT(= "E667: Fsync failed");
EXTERN Byte e_unprintable_character_in_group_name[]
   INIT(= "E669: Unprintable character in group name");
EXTERN Byte e_mix_of_help_file_encodings_within_language_str[]
   INIT(= "E670: Mix of help file encodings within a language: %s");
EXTERN Byte e_no_matching_autocommands_for_buftype_str_buffer[]
   INIT(= "E676: No matching autocommands for buftype=%s buffer");
EXTERN Byte e_error_writing_temp_file[]
   INIT(= "E677: Error writing temp file");
EXTERN Byte e_invalid_character_after_str_2[]
   INIT(= "E678: Invalid character after %s%%[dxouU]");
EXTERN Byte e_book_nr_invalid_book_number[]
   INIT(= "E680: <book=%d>: invalid book number");
EXTERN Byte e_buffer_is_not_loaded[]
   INIT(= "E681: Book is not loaded");
EXTERN Byte e_invalid_search_pattern_or_delimiter[]
   INIT(= "E682: Invalid search pattern or delimiter");
EXTERN Byte e_file_name_missing_or_invalid_pattern[]
   INIT(= "E683: File name missing or invalid pattern");
EXTERN Byte e_list_index_out_of_range_nr[]
   INIT(= "E684: List index out of range: %ld");
EXTERN Byte e_internal_error_str[]
   INIT(= "E685: Internal error: %s");
EXTERN Byte e_argument_of_str_must_be_list[]
   INIT(= "E686: Argument of %s must be a List");
EXTERN Byte e_less_targets_than_list_items[]
   INIT(= "E687: Less targets than List items");
EXTERN Byte e_more_targets_than_list_items[]
   INIT(= "E688: More targets than List items");
EXTERN Byte e_index_not_allowed_after_str_str[]
   INIT(= "E689: Index not allowed after a %s: %s");
EXTERN Byte e_missing_in_after_for[]
   INIT(= "E690: Missing \"in\" after :for");
EXTERN Byte e_can_only_compare_list_with_list[]
   INIT(= "E691: Can only compare List with List");
EXTERN Byte e_invalid_operation_for_list[]
   INIT(= "E692: Invalid operation for List");
EXTERN Byte e_class_or_typealias_required_for_argument_nr[]
   INIT(= "E693: Class or class typealias required for argument %d");
EXTERN Byte e_invalid_operation_for_funcrefs[]
   INIT(= "E694: Invalid operation for Funcrefs");
EXTERN Byte e_cannot_index_a_funcref[]
   INIT(= "E695: Cannot index a Funcref");
EXTERN Byte e_missing_comma_in_list_str[]
   INIT(= "E696: Missing comma in List: %s");
EXTERN Byte e_missing_end_of_list_rsb_str[]
   INIT(= "E697: Missing end of List ']': %s");
EXTERN Byte e_variable_nested_too_deep_for_making_copy[]
   INIT(= "E698: Variable nested too deep for making a copy");
EXTERN Byte e_too_many_arguments[]
   INIT(= "E699: Too many arguments");
EXTERN Byte e_unknown_function_str_2[]
   INIT(= "E700: Unknown function: %s");
EXTERN Byte e_invalid_type_for_len[]
   INIT(= "E701: Invalid type for len()");
EXTERN Byte e_sort_compare_function_failed[]
   INIT(= "E702: Sort compare function failed");
EXTERN Byte e_using_funcref_as_number[]
   INIT(= "E703: Using a Funcref as a Number");
EXTERN Byte e_funcref_variable_name_must_start_with_capital_str[]
   INIT(= "E704: Funcref variable name must start with a capital: %s");
EXTERN Byte e_variable_name_conflicts_with_existing_function_str[]
   INIT(= "E705: Variable name conflicts with existing function: %s");
EXTERN Byte e_argument_of_str_must_be_list_string_or_dictionary[]
   INIT(= "E706: Argument of %s must be a List, String or Dictionary");
EXTERN Byte e_function_name_conflicts_with_variable_str[]
   INIT(= "E707: Function name conflicts with variable: %s");
EXTERN Byte e_slice_must_come_last[]
   INIT(= "E708: [:] must come last");
EXTERN Byte e_slice_requires_list_or_blob_value[]
   INIT(= "E709: [:] requires a List or Blob value");
EXTERN Byte e_list_value_has_more_items_than_targets[]
   INIT(= "E710: List value has more items than targets");
EXTERN Byte e_list_value_does_not_have_enough_items[]
   INIT(= "E711: List value does not have enough items");
EXTERN Byte e_argument_of_str_must_be_list_or_dictionary[]
   INIT(= "E712: Argument of %s must be a List or Dictionary");
EXTERN Byte e_cannot_use_empty_key_for_dictionary[]
   INIT(= "E713: Cannot use empty key for Dictionary");
EXTERN Byte e_list_required[]
   INIT(= "E714: List required");
EXTERN Byte e_dictionary_required[]
   INIT(= "E715: Dictionary required");
EXTERN Byte e_key_not_present_in_dictionary_str[]
   INIT(= "E716: Key not present in Dictionary: \"%s\"");
EXTERN Byte e_dictionary_entry_already_exists[]
   INIT(= "E717: Dictionary entry already exists");
EXTERN Byte e_funcref_required[]
   INIT(= "E718: Funcref required");
EXTERN Byte e_cannot_slice_dictionary[]
   INIT(= "E719: Cannot slice a Dictionary");
EXTERN Byte e_missing_colon_in_dictionary_str[]
   INIT(= "E720: Missing colon in Dictionary: %s");
EXTERN Byte e_duplicate_key_in_dictionary_str[]
   INIT(= "E721: Duplicate key in Dictionary: \"%s\"");
EXTERN Byte e_missing_comma_in_dictionary_str[]
   INIT(= "E722: Missing comma in Dictionary: %s");
EXTERN Byte e_missing_dict_end_str[] //{
   INIT(= "E723: Missing end of Dictionary '}': %s");
EXTERN Byte e_variable_nested_too_deep_for_displaying[]
   INIT(= "E724: Variable nested too deep for displaying");
EXTERN Byte e_calling_dict_function_without_dictionary_str[]
   INIT(= "E725: Calling dict function without Dictionary: %s");
EXTERN Byte e_stride_is_zero[]
   INIT(= "E726: Stride is zero");
EXTERN Byte e_start_past_end[]
   INIT(= "E727: Start past end");
EXTERN Byte e_using_dictionary_as_number[]
   INIT(= "E728: Using a Dictionary as a Number");
EXTERN Byte e_using_funcref_as_string[]
   INIT(= "E729: Using a Funcref as a String");
EXTERN Byte e_using_list_as_string[]
   INIT(= "E730: Using a List as a String");
EXTERN Byte e_using_dictionary_as_string[]
   INIT(= "E731: Using a Dictionary as a String");
EXTERN Byte e_using_endfor_with_while[]
   INIT(= "E732: Using :endfor with :while");
EXTERN Byte e_using_endwhile_with_for[]
   INIT(= "E733: Using :endwhile with :for");
EXTERN Byte e_wrong_variable_type_for_str_equal[]
   INIT(= "E734: Wrong variable type for %s=");
EXTERN Byte e_can_only_compare_dictionary_with_dictionary[]
   INIT(= "E735: Can only compare Dictionary with Dictionary");
EXTERN Byte e_invalid_operation_for_dictionary[]
   INIT(= "E736: Invalid operation for Dictionary");
EXTERN Byte e_key_already_exists_str[]
   INIT(= "E737: Key already exists: %s");
EXTERN Byte e_cant_list_variables_for_str[]
   INIT(= "E738: Can't list variables for %s");
EXTERN Byte e_cannot_create_directory_str[]
   INIT(= "E739: Cannot create directory: %s");
EXTERN Byte e_too_many_arguments_for_function_str_2[]
   INIT(= "E740: Too many arguments for function %s");
EXTERN Byte e_value_is_locked[]
   INIT(= "E741: Value is locked");
EXTERN Byte e_value_is_locked_str[]
   INIT(= "E741: Value is locked: %s");
EXTERN Byte e_cannot_change_value[]
   INIT(= "E742: Cannot change value");
EXTERN Byte e_cannot_change_value_of_str[]
   INIT(= "E742: Cannot change value of %s");
EXTERN Byte e_variable_nested_too_deep_for_unlock[]
   INIT(= "E743: Variable nested too deep for (un)lock");
EXTERN Byte e_using_list_as_number[]
   INIT(= "E745: Using a List as a Number");
EXTERN Byte e_function_name_does_not_match_script_file_name_str[]
   INIT(= "E746: Function name does not match script file name: %s");
EXTERN Byte e_cannot_change_directory_buffer_is_modified_add_bang_to_override[]
   INIT(= "E747: Cannot change directory, buffer is modified (add ! to override)");
EXTERN Byte e_no_previously_used_register[]
   INIT(= "E748: No previously used register");
EXTERN Byte e_empty_buffer[]
   INIT(= "E749: Empty buffer");
EXTERN Byte e_output_file_name_must_not_have_region_name[]
   INIT(= "E751: Output file name must not have region name");
EXTERN Byte e_no_previous_spell_replacement[]
   INIT(= "E752: No previous spell replacement");
EXTERN Byte e_not_found_str[]
   INIT(= "E753: Not found: %s");
EXTERN Byte e_only_up_to_nr_regions_supported[]
   INIT(= "E754: Only up to %d regions supported");
EXTERN Byte e_invalid_region_in_str[]
   INIT(= "E755: Invalid region in %s");
EXTERN Byte e_spell_checking_is_not_possible[]
   INIT(= "E756: Spell checking is not possible");
EXTERN Byte e_this_does_not_look_like_spell_file[]
   INIT(= "E757: This does not look like a spell file");
EXTERN Byte e_truncated_spell_file[]
   INIT(= "E758: Truncated spell file");
EXTERN Byte e_format_error_in_spell_file[]
   INIT(= "E759: Format error in spell file");
EXTERN Byte e_no_word_count_in_str[]
   INIT(= "E760: No word count in %s");
EXTERN Byte e_format_error_in_affix_file_fol_low_or_upp[]
   INIT(= "E761: Format error in affix file FOL, LOW or UPP");
EXTERN Byte e_character_in_fol_low_or_upp_is_out_of_range[]
   INIT(= "E762: Character in FOL, LOW or UPP is out of range");
EXTERN Byte e_word_characters_differ_between_spell_files[]
   INIT(= "E763: Word characters differ between spell files");
EXTERN Byte e_option_str_is_not_set[]
   INIT(= "E764: Option '%s' is not set");
EXTERN Byte e_spellfile_does_not_have_nr_entries[]
   INIT(= "E765: 'spellfile' does not have %d entries");
EXTERN Byte e_insufficient_arguments_for_printf[]
   INIT(= "E766: Insufficient arguments for printf()");
EXTERN Byte e_too_many_arguments_to_printf[]
   INIT(= "E767: Too many arguments for printf()");
EXTERN Byte e_swap_file_exists_str_silent_overrides[]
   INIT(= "E768: Swap file exists: %s (:silent! overrides)");
EXTERN Byte e_missing_rsb_after_str_lsb[]
   INIT(= "E769: Missing ] after %s[");
EXTERN Byte e_unsupported_section_in_spell_file[]
   INIT(= "E770: Unsupported section in spell file");
EXTERN Byte e_old_spell_file_needs_to_be_updated[]
   INIT(= "E771: Old spell file, needs to be updated");
EXTERN Byte e_spell_file_is_for_newer_version_of_eegl[]
   INIT(= "E772: Spell file is for newer version of Eegl");
EXTERN Byte e_symlink_loop_for_str[]
   INIT(= "E773: Symlink loop for \"%s\"");
EXTERN Byte e_operatorfunc_is_empty[]
   INIT(= "E774: 'operatorfunc' is empty");
EXTERN Byte e_no_location_stack[]
   INIT(= "E776: No such location stack exists. Options: g, m, h, c, b");
EXTERN Byte e_string_or_list_expected[]
   INIT(= "E777: String or List expected");
EXTERN Byte e_this_does_not_look_like_sug_file_str[]
   INIT(= "E778: This does not look like a .sug file: %s");
EXTERN Byte e_old_sug_file_needs_to_be_updated_str[]
   INIT(= "E779: Old .sug file, needs to be updated: %s");
EXTERN Byte e_sug_file_is_for_newer_version_of_eegl_str[]
   INIT(= "E780: .sug file is for newer version of Eegl: %s");
EXTERN Byte e_sug_file_doesnt_match_spl_file_str[]
   INIT(= "E781: .sug file doesn't match .spl file: %s");
EXTERN Byte e_error_while_reading_sug_file_str[]
   INIT(= "E782: Error while reading .sug file: %s");
EXTERN Byte e_duplicate_char_in_map_entry[]
   INIT(= "E783: Duplicate char in MAP entry");
EXTERN Byte e_cannot_close_last_tab_page[]
   INIT(= "E784: Cannot close last tab");
EXTERN Byte e_complete_can_only_be_used_in_insert_mode[]
   INIT(= "E785: complete() can only be used in Insert mode");
EXTERN Byte e_range_not_allowed[]
   INIT(= "E786: Range not allowed");
EXTERN Byte e_buffer_changed_unexpectedly[]
   INIT(= "E787: Book changed unexpectedly");
EXTERN Byte e_not_allowed_to_edit_another_buffer_now[]
   INIT(= "E788: Not allowed to edit another buffer now");
EXTERN Byte e_error_missing_rsb_str[]
   INIT(= "E789: Missing ']': %s");
EXTERN Byte e_undojoin_is_not_allowed_after_undo[]
   INIT(= "E790: undojoin is not allowed after undo");
EXTERN Byte e_empty_keymap_entry[]
   INIT(= "E791: Empty keymap entry");
EXTERN Byte e_empty_menu_name[]
   INIT(= "E792: Empty menu name");
EXTERN Byte e_no_other_buffer_in_diff_mode_is_modifiable[]
   INIT(= "E793: No other buffer in diff mode is modifiable");
EXTERN Byte e_cannot_delete_variable[]
   INIT(= "E795: Cannot delete variable");
EXTERN Byte e_cannot_delete_variable_str[]
   INIT(= "E795: Cannot delete variable %s");
EXTERN Byte e_spellfilemising_autocommand_deleted_buffer[]
   INIT(= "E797: SpellFileMissing autocommand deleted buffer");
EXTERN Byte e_id_is_reserved_for_match_nr[]
   INIT(= "E798: ID is reserved for \":match\": %d");
EXTERN Byte e_invalid_id_nr_must_be_greater_than_or_equal_to_one_1[]
   INIT(= "E799: Invalid ID: %d (must be greater than or equal to 1)");
EXTERN Byte e_id_already_taken_nr[]
   INIT(= "E801: ID already taken: %d");
EXTERN Byte e_invalid_id_nr_must_be_greater_than_or_equal_to_one_2[]
   INIT(= "E802: Invalid ID: %d (must be greater than or equal to 1)");
EXTERN Byte e_id_not_found_nr[]
   INIT(= "E803: ID not found: %d");
EXTERN Byte e_cannot_use_percent_with_float[]
   // xgettext:no-c-format
   INIT(= "E804: Cannot use '%' with Float");
EXTERN Byte e_using_float_as_number[]
   INIT(= "E805: Using a Float as a Number");
EXTERN Byte e_using_float_as_string[]
   INIT(= "E806: Using a Float as a String");
EXTERN Byte e_expected_float_argument_for_printf[]
   INIT(= "E807: Expected Float argument for printf()");
EXTERN Byte e_number_or_float_required[]
   INIT(= "E808: Number or Float required");
EXTERN Byte e_hashsmall_is_not_available_without_the_eval_feature[]
   INIT(= "E809: #< is not available without the +eval feature");
EXTERN Byte e_cannot_read_or_write_temp_files[]
   INIT(= "E810: Cannot read or write temp files");
EXTERN Byte e_not_allowed_to_change_buffer_information_now[]
   INIT(= "E811: Not allowed to change buffer information now");
EXTERN Byte e_autocommands_changed_buffer_or_buffer_name[]
   INIT(= "E812: Autocommands changed buffer or buffer name");
EXTERN Byte e_cannot_close_autocmd_or_popup_portal[]
   INIT(= "E813: Cannot close autocmd or popup portal");
EXTERN Byte e_cannot_close_portal_only_autocomm_portal_would_remain[]
   INIT(= "E814: Cannot close portal, only autocomm portal would remain");
EXTERN Byte e_cannot_read_patch_output[]
   INIT(= "E816: Cannot read patch output");
EXTERN Byte e_blowfish_big_little_endian_use_wrong[]
   INIT(= "E817: Blowfish big/little endian use wrong");
EXTERN Byte e_sha256_test_failed[]
   INIT(= "E818: sha256 test failed");
EXTERN Byte e_blowfish_test_failed[]
   INIT(= "E819: Blowfish test failed");
EXTERN Byte e_sizeof_uint32_isnot_four[]
   INIT(= "E820: sizeof(uint32_t) != 4");
EXTERN Byte e_cannot_open_undo_file_for_reading_str[]
   INIT(= "E822: Cannot open undo file for reading: %s");
EXTERN Byte e_not_an_undo_file_str[]
   INIT(= "E823: Not an undo file: %s");
EXTERN Byte e_incompatible_undo_file_str[]
   INIT(= "E824: Incompatible undo file: %s");
EXTERN Byte e_corrupted_undo_file_str_str[]
   INIT(= "E825: Corrupted undo file (%s): %s");
EXTERN Byte e_cannot_open_undo_file_for_writing_str[]
   INIT(= "E828: Cannot open undo file for writing: %s");
EXTERN Byte e_write_error_in_undo_file_str[]
   INIT(= "E829: Write error in undo file: %s");
EXTERN Byte e_undo_number_nr_not_found[]
   INIT(= "E830: Undo number %ld not found");
EXTERN Byte e_bf_key_init_called_with_empty_password[]
   INIT(= "E831: bf_key_init() called with empty password");
EXTERN Byte e_conflicts_with_value_of_listchars[]
   INIT(= "E834: Conflicts with value of 'listchars'");
EXTERN Byte e_conflicts_with_value_of_fillchars[]
   INIT(= "E835: Conflicts with value of 'fillchars'");
// E839 unused
EXTERN Byte e_complete_function_deleted_text[]
   INIT(= "E840: Completion function deleted text");
EXTERN Byte e_reserved_name_cannot_be_used_for_user_defined_command[]
   INIT(= "E841: Reserved name, cannot be used for user defined command");
EXTERN Byte e_no_line_number_to_use_for_slnum[]
   INIT(= "E842: No line number to use for \"<slnum>\"");
EXTERN Byte e_insufficient_memory_word_list_will_be_incomplete[]
   INIT(= "E845: Insufficient memory, word list will be incomplete");
EXTERN Byte e_key_code_not_set[]
   INIT(= "E846: Key code not set");
EXTERN Byte e_too_many_syntax_includes[]
   INIT(= "E847: Too many syntax includes");
EXTERN Byte e_too_many_syntax_clusters[]
   INIT(= "E848: Too many syntax clusters");
EXTERN Byte e_too_many_highlight_and_syntax_groups[]
   INIT(= "E849: Too many highlight and syntax groups");
EXTERN Byte e_invalid_register_name[]
   INIT(= "E850: Invalid register name");
EXTERN Byte e_duplicate_argument_name_str[]
   INIT(= "E853: Duplicate argument name: %s");
EXTERN Byte e_path_too_long_for_completion[]
   INIT(= "E854: Path too long for completion");
EXTERN Byte e_autocommands_caused_command_to_abort[]
   INIT(= "E855: Autocommands caused command to abort");
EXTERN Byte e_assert_fails_second_arg[]
   INIT(= "E856: \"assert_fails()\" second argument must be a string or a list with one or two strings");
EXTERN Byte e_dictionary_key_str_required[]
   INIT(= "E857: Dictionary key \"%s\" required");
EXTERN Byte e_need_id_and_type_or_types_with_both[]
   INIT(= "E860: Need 'id' and 'type' or 'types' with 'both'");
EXTERN Byte e_cannot_open_second_popup_with_terminal[]
   INIT(= "E861: Cannot open a second popup with a terminal");
EXTERN Byte e_cannot_use_g_here[]
   INIT(= "E862: Cannot use g: here");
EXTERN Byte e_not_allowed_for_terminal_in_popup_portal[]
   INIT(= "E863: Not allowed for a terminal in a popup portal");
EXTERN Byte e_percent_hash_can_only_be_followed_by_zero_one_two_automatic_engine_will_be_used[]
   // xgettext:no-c-format
   INIT(= "E864: \\%#= can only be followed by 0, 1, or 2. The automatic engine will be used");
EXTERN Byte e_nfa_regexp_end_encountered_prematurely[]
   INIT(= "E865: (NFA) Regexp end encountered prematurely");
EXTERN Byte e_nfa_regexp_misplaced_chr[]
   INIT(= "E866: (NFA regexp) Misplaced %c");
EXTERN Byte e_nfa_regexp_unknown_operator_z_chr[]
   INIT(= "E867: (NFA regexp) Unknown operator '\\z%c'");
EXTERN Byte e_nfa_regexp_unknown_operator_percent_chr[]
   INIT(= "E867: (NFA regexp) Unknown operator '\\%%%c'");
EXTERN Byte e_error_building_nfa_with_equivalence_class[]
   INIT(= "E868: Error building NFA with equivalence class!");
EXTERN Byte e_nfa_regexp_unknown_operator_at_chr[]
   INIT(= "E869: (NFA regexp) Unknown operator '\\@%c'");
EXTERN Byte e_nfa_regexp_error_reading_repetition_limits[]
   INIT(= "E870: (NFA regexp) Error reading repetition limits");
EXTERN Byte e_nfa_regexp_cant_have_multi_follow_multi[]
   INIT(= "E871: (NFA regexp) Can't have a multi follow a multi");
EXTERN Byte e_nfa_regexp_too_many_parens[]
   INIT(= "E872: (NFA regexp) Too many '('");
EXTERN Byte e_nfa_regexp_proper_termination_error[]
   INIT(= "E873: (NFA regexp) proper termination error");
EXTERN Byte e_nfa_regexp_could_not_pop_stack[]
   INIT(= "E874: (NFA regexp) Could not pop the stack!");
EXTERN Byte e_nfa_regexp_while_converting_from_postfix_to_nfa_too_many_stats_left_on_stack[]
   INIT(= "E875: (NFA regexp) (While converting from postfix to NFA), too many states left on stack");
EXTERN Byte e_nfa_regexp_not_enough_space_to_store_whole_nfa[]
   INIT(= "E876: (NFA regexp) Not enough space to store the whole NFA");
EXTERN Byte e_nfa_regexp_invalid_character_class_nr[]
   INIT(= "E877: (NFA regexp) Invalid character class: %d");
EXTERN Byte e_nfa_regexp_could_not_allocate_memory_for_branch_traversal[]
   INIT(= "E878: (NFA regexp) Could not allocate memory for branch traversal!");
EXTERN Byte e_nfa_regexp_too_many_z[]
   INIT(= "E879: (NFA regexp) Too many \\z(");
EXTERN Byte e_line_count_changed_unexpectedly[]
   INIT(= "E881: Line count changed unexpectedly");
EXTERN Byte e_uniq_compare_function_failed[]
   INIT(= "E882: Uniq compare function failed");
EXTERN Byte e_search_pattern_and_expression_register_may_not_contain_two_or_more_lines[]
   INIT(= "E883: Search pattern and expression register may not contain two or more lines");
EXTERN Byte e_function_name_cannot_contain_colon_str[]
   INIT(= "E884: Function name cannot contain a colon: %s");
EXTERN Byte e_not_possible_to_change_sign_str[]
   INIT(= "E885: Not possible to change sign %s");
EXTERN Byte e_cant_rename_eeglinfo_file_to_str[]
   INIT(= "E886: Can't rename eeglinfo file to %s!");
EXTERN Byte e_nfa_regexp_cannot_repeat_str[]
   INIT(= "E888: (NFA regexp) cannot repeat %s");
EXTERN Byte e_number_required[]
   INIT(= "E889: Number required");
EXTERN Byte e_trailing_char_after_rsb_str_str[]
   INIT(= "E890: Trailing char after ']': %s]%s");
EXTERN Byte e_using_funcref_as_float[]
   INIT(= "E891: Using a Funcref as a Float");
EXTERN Byte e_using_string_as_float[]
   INIT(= "E892: Using a String as a Float");
EXTERN Byte e_using_list_as_float[]
   INIT(= "E893: Using a List as a Float");
EXTERN Byte e_using_dictionary_as_float[]
   INIT(= "E894: Using a Dictionary as a Float");
EXTERN Byte e_argument_of_str_must_be_list_dictionary_or_blob[]
   INIT(= "E896: Argument of %s must be a List, Dictionary or Blob");
EXTERN Byte e_list_or_blob_required[]
   INIT(= "E897: List or Blob required");
EXTERN Byte e_socket_in_channel_connect[]
   INIT(= "E898: socket() in channel_connect()");
EXTERN Byte e_argument_of_str_must_be_list_or_blob[]
   INIT(= "E899: Argument of %s must be a List or Blob");
EXTERN Byte e_maxdepth_must_be_non_negative_number[]
   INIT(= "E900: maxdepth must be non-negative number");
EXTERN Byte e_getaddrinfo_in_channel_open_str[]
   INIT(= "E901: getaddrinfo() in channel_open(): %s");
EXTERN Byte e_cannot_connect_to_port[]
   INIT(= "E902: Cannot connect to port");
EXTERN Byte e_received_command_with_non_string_argument[]
   INIT(= "E903: Received command with non-string argument");
EXTERN Byte e_last_argument_for_expr_call_must_be_number[]
   INIT(= "E904: Last argument for expr/call must be a number");
EXTERN Byte e_third_argument_for_call_must_be_list[]
   INIT(= "E904: Third argument for call must be a list");
EXTERN Byte e_received_unknown_command_str[]
   INIT(= "E905: Received unknown command: %s");
EXTERN Byte e_not_an_open_channel[]
   INIT(= "E906: Not an open channel");
EXTERN Byte e_using_special_value_as_float[]
   INIT(= "E907: Using a special value as a Float");
EXTERN Byte e_using_invalid_value_as_string_str[]
   INIT(= "E908: Using an invalid value as a String: %s");
EXTERN Byte e_cannot_index_special_variable[]
   INIT(= "E909: Cannot index a special variable");
EXTERN Byte e_using_job_as_number[]
   INIT(= "E910: Using a Job as a Number");
EXTERN Byte e_using_job_as_float[]
   INIT(= "E911: Using a Job as a Float");
EXTERN Byte e_cannot_use_evalexpr_sendexpr_with_raw_or_nl_channel[]
   INIT(= "E912: Cannot use ch_evalexpr()/ch_sendexpr() with a raw or nl channel");
EXTERN Byte e_using_channel_as_number[]
   INIT(= "E913: Using a Channel as a Number");
EXTERN Byte e_using_channel_as_float[]
   INIT(= "E914: Using a Channel as a Float");
EXTERN Byte e_in_io_buffer_requires_in_buf_or_in_name_to_be_set[]
   INIT(= "E915: in_io buffer requires in_buf or in_name to be set");
EXTERN Byte e_not_valid_job[]
   INIT(= "E916: Not a valid job");
EXTERN Byte e_cannot_use_callback_with_str[]
   INIT(= "E917: Cannot use a callback with %s()");
EXTERN Byte e_buffer_must_be_loaded_str[]
   INIT(= "E918: Book must be loaded: %s");
EXTERN Byte e_directory_not_found_in_str_str[]
   INIT(= "E919: Directory not found in '%s': \"%s\"");
EXTERN Byte e_io_file_requires_name_to_be_set[]
   INIT(= "E920: _io file requires _name to be set");
EXTERN Byte e_invalid_callback_argument[]
   INIT(= "E921: Invalid callback argument");
// E922 unused
EXTERN Byte e_second_argument_of_function_must_be_list_or_dict[]
   INIT(= "E923: Second argument of function() must be a list or a dict");
EXTERN Byte e_current_window_was_closed[]
   INIT(= "E924: Current portal was closed");
EXTERN Byte e_current_location_list_was_changed[]
   INIT(= "E926: Current location list was changed");
EXTERN Byte e_invalid_action_str_1[]
   INIT(= "E927: Invalid action: '%s'");
EXTERN Byte e_string_required[]
   INIT(= "E928: String required");
EXTERN Byte e_too_many_eeglinfo_temp_files_like_str[]
   INIT(= "E929: Too many eeglinfo temp files, like %s!");
EXTERN Byte e_cannot_use_redir_inside_execute[]
   INIT(= "E930: Cannot use :redir inside execute()");
EXTERN Byte e_book_cannot_be_registered[]
   INIT(= "E931: Book cannot be registered");
EXTERN Byte e_closure_function_should_not_be_at_top_level_str[]
   INIT(= "E932: Closure function should not be at top level: %s");
EXTERN Byte e_function_was_deleted_str[]
   INIT(= "E933: Function was deleted: %s");
EXTERN Byte e_cannot_jump_to_buffer_that_does_not_have_name[]
   INIT(= "E934: Cannot jump to a buffer that does not have a name");
EXTERN Byte e_invalid_submatch_number_nr[]
   INIT(= "E935: Invalid submatch number: %d");
EXTERN Byte e_cannot_delete_current_group[]
   INIT(= "E936: Cannot delete the current group");
EXTERN Byte e_attempt_to_delete_buffer_that_is_in_use_str[]
   INIT(= "E937: Attempt to delete a buffer that is in use: %s");
EXTERN Byte e_duplicate_key_in_json_str[]
   INIT(= "E938: Duplicate key in JSON: \"%s\"");
EXTERN Byte e_positive_count_required[]
   INIT(= "E939: Positive count required");
EXTERN Byte e_cannot_lock_or_unlock_variable_str[]
   INIT(= "E940: Cannot lock or unlock variable %s");
EXTERN Byte e_already_started_server[]
   INIT(= "E941: Already started a server");
EXTERN Byte e_clientserver_feature_not_available[]
   INIT(= "E942: +clientserver feature not available");
EXTERN Byte e_command_table_needs_to_be_updated_run_make_ids[]
   INIT(= "E943: Command table needs to be updated, run 'make indices'");
EXTERN Byte e_reverse_range_in_character_class[]
   INIT(= "E944: Reverse range in character class");
EXTERN Byte e_range_too_large_in_character_class[]
   INIT(= "E945: Range too large in character class");
EXTERN Byte e_cannot_make_terminal_with_running_job_modifiable[]
   INIT(= "E946: Cannot make a terminal with running job modifiable");
EXTERN Byte e_job_still_running_in_buffer_str[]
   INIT(= "E947: Job still running in buffer \"%s\"");
EXTERN Byte e_job_still_running[]
   INIT(= "E948: Job still running");
EXTERN Byte e_job_still_running_add_bang_to_end_the_job[]
   INIT(= "E948: Job still running (add ! to end the job)");
EXTERN Byte e_file_changed_while_writing[]
   INIT(= "E949: File changed while writing");
EXTERN Byte e_cannot_convert_between_str_and_str[]
   INIT(= "E950: Cannot convert between %s and %s");
EXTERN Byte e_percent_value_too_large[]
   // xgettext:no-c-format
   INIT(= "E951: \\% value too large");
EXTERN Byte e_autocommand_caused_recursive_behavior[]
   INIT(= "E952: Autocommand caused recursive behavior");
EXTERN Byte e_file_exists_str[]
   INIT(= "E953: File exists: %s");
EXTERN Byte e_not_terminal_buffer[]
   INIT(= "E955: Not a terminal buffer");
EXTERN Byte e_cannot_use_pattern_recursively[]
   INIT(= "E956: Cannot use pattern recursively");
EXTERN Byte e_invalid_portal_number[]
   INIT(= "E957: Invalid portal number");
EXTERN Byte e_job_already_finished[]
   INIT(= "E958: Job already finished");
EXTERN Byte e_invalid_diff_format[]
   INIT(= "E959: Invalid diff format.");
EXTERN Byte e_problem_creating_internal_diff[]
   INIT(= "E960: Problem creating the internal diff");
EXTERN Byte e_no_line_number_to_use_for_sflnum[]
   INIT(= "E961: No line number to use for \"<sflnum>\"");
EXTERN Byte e_invalid_action_str_2[]
   INIT(= "E962: Invalid action: '%s'");
EXTERN Byte e_setting_v_str_to_value_with_wrong_type[]
   INIT(= "E963: Setting v:%s to value with wrong type");
EXTERN Byte e_invalid_column_number_nr[]
   INIT(= "E964: Invalid column number: %ld");
EXTERN Byte e_missing_property_type_name[]
   INIT(= "E965: Missing property type name");
EXTERN Byte e_invalid_line_number_nr[]
   INIT(= "E966: Invalid line number: %ld");
EXTERN Byte e_text_property_info_corrupted[]
   INIT(= "E967: Text property info corrupted");
EXTERN Byte e_need_at_least_one_of_id_or_type[]
   INIT(= "E968: Need at least one of 'id' or 'type'");
EXTERN Byte e_property_type_str_already_defined[]
   INIT(= "E969: Property type %s already defined");
EXTERN Byte e_unknown_highlight_group_name_str[]
   INIT(= "E970: Unknown highlight group name: '%s'");
EXTERN Byte e_property_type_str_does_not_exist[]
   INIT(= "E971: Property type %s does not exist");
EXTERN Byte e_blob_value_does_not_have_right_number_of_bytes[]
   INIT(= "E972: Blob value does not have the right number of bytes");
EXTERN Byte e_blob_literal_should_have_an_even_number_of_hex_characters[]
   INIT(= "E973: Blob literal should have an even number of hex characters");
EXTERN Byte e_using_blob_as_number[]
   INIT(= "E974: Using a Blob as a Number");
EXTERN Byte e_using_blob_as_float[]
   INIT(= "E975: Using a Blob as a Float");
EXTERN Byte e_using_blob_as_string[]
   INIT(= "E976: Using a Blob as a String");
EXTERN Byte e_can_only_compare_blob_with_blob[]
   INIT(= "E977: Can only compare Blob with Blob");
EXTERN Byte e_invalid_operation_for_blob[]
   INIT(= "E978: Invalid operation for Blob");
EXTERN Byte e_blob_index_out_of_range_nr[]
   INIT(= "E979: Blob index out of range: %ld");
# ifndef USE_INPUT_BUF
EXTERN Byte e_lowlevel_input_not_supported[]
   INIT(= "E980: Lowlevel input not supported");
# endif
EXTERN Byte e_duplicate_argument_str[]
   INIT(= "E983: Duplicate argument: %s");
EXTERN Byte e_cannot_modify_tag_stack_within_tagfunc[]
   INIT(= "E986: Cannot modify the tag stack within tagfunc");
EXTERN Byte e_invalid_return_value_from_tagfunc[]
   INIT(= "E987: Invalid return value from tagfunc");
EXTERN Byte e_non_default_argument_follows_default_argument[]
   INIT(= "E989: Non-default argument follows default argument");
EXTERN Byte e_missing_end_marker_str[]
   INIT(= "E990: Missing end marker '%s'");
EXTERN Byte e_cannot_use_heredoc_here[]
   INIT(= "E991: Cannot use =<< here");
EXTERN Byte e_window_nr_is_not_popup_portal[]
   INIT(= "E993: Portal %d is not a popup portal");
EXTERN Byte e_not_allowed_in_popup_portal[]
   INIT(= "E994: Not allowed in a popup portal");
EXTERN Byte e_cannot_modify_existing_variable[]
   INIT(= "E995: Cannot modify existing variable");
EXTERN Byte e_cannot_lock_range[]
   INIT(= "E996: Cannot lock a range");
EXTERN Byte e_cannot_lock_option[]
   INIT(= "E996: Cannot lock an option");
EXTERN Byte e_cannot_lock_list_or_dict[]
   INIT(= "E996: Cannot lock a list or dict");
EXTERN Byte e_cannot_lock_environment_variable[]
   INIT(= "E996: Cannot lock an environment variable");
EXTERN Byte e_cannot_lock_register[]
   INIT(= "E996: Cannot lock a register");
EXTERN Byte e_tabpage_not_found_nr[]
   INIT(= "E997: Tabpage not found: %d");
EXTERN Byte e_reduce_of_an_empty_str_with_no_initial_value[]
   INIT(= "E998: Reduce of an empty %s with no initial value");
// E1000 unused
EXTERN Byte e_variable_not_found_str[]
   INIT(= "E1001: Variable not found: %s");
EXTERN Byte e_syntax_error_at_str[]
   INIT(= "E1002: Syntax error at %s");
EXTERN Byte e_missing_return_value[]
   INIT(= "E1003: Missing return value");
EXTERN Byte e_white_space_required_before_and_after_str_at_str[]
   INIT(= "E1004: White space required before and after '%s' at \"%s\"");
EXTERN Byte e_too_many_argument_types[]
   INIT(= "E1005: Too many argument types");
EXTERN Byte e_str_is_used_as_argument[]
   INIT(= "E1006: %s is used as an argument");
EXTERN Byte e_mandatory_argument_after_optional_argument[]
   INIT(= "E1007: Mandatory argument after optional argument");
EXTERN Byte e_missing_type_after_str[]
   INIT(= "E1008: Missing <type> after %s");
EXTERN Byte e_missing_gt_after_type_str[]
   INIT(= "E1009: Missing > after type: %s");
EXTERN Byte e_type_not_recognized_str[]
   INIT(= "E1010: Type not recognized: %s");
EXTERN Byte e_name_too_long_str[]
   INIT(= "E1011: Name too long: %s");
EXTERN Byte e_type_mismatch_expected_str_but_got_str[]
   INIT(= "E1012: Type mismatch; expected %s but got %s");
EXTERN Byte e_type_mismatch_expected_str_but_got_str_in_str[]
   INIT(= "E1012: Type mismatch; expected %s but got %s in %s");
EXTERN Byte e_argument_nr_type_mismatch_expected_str_but_got_str[]
   INIT(= "E1013: Argument %d: type mismatch, expected %s but got %s");
EXTERN Byte e_argument_nr_type_mismatch_expected_str_but_got_str_in_str[]
   INIT(= "E1013: Argument %d: type mismatch, expected %s but got %s in %s");
EXTERN Byte e_invalid_key_str[]
   INIT(= "E1014: Invalid key: %s");
EXTERN Byte e_name_expected_str[]
   INIT(= "E1015: Name expected: %s");
EXTERN Byte e_cannot_declare_a_scope_variable_str[]
   INIT(= "E1016: Cannot declare a %s variable: %s");
EXTERN Byte e_cannot_declare_an_environment_variable_str[]
   INIT(= "E1016: Cannot declare an environment variable: %s");
EXTERN Byte e_variable_already_declared_str[]
   INIT(= "E1017: Variable already declared: %s");
EXTERN Byte e_cannot_assign_to_constant_str[]
   INIT(= "E1018: Cannot assign to a constant: %s");
EXTERN Byte e_can_only_concatenate_to_string[]
   INIT(= "E1019: Can only concatenate to string");
EXTERN Byte e_cannot_use_operator_on_new_variable_str[]
   INIT(= "E1020: Cannot use an operator on a new variable: %s");
EXTERN Byte e_const_requires_a_value[]
   INIT(= "E1021: Const requires a value");
EXTERN Byte e_type_or_initialization_required[]
   INIT(= "E1022: Type or initialization required");
EXTERN Byte e_using_number_as_bool_nr[]
   INIT(= "E1023: Using a Number as a Boole: %lld");
EXTERN Byte e_using_number_as_string[]
   INIT(= "E1024: Using a Number as a String");
EXTERN Byte e_using_rcurly_outside_if_block_scope[] //{
   INIT(= "E1025: Using } outside of a block scope");
EXTERN Byte e_missing_rcurly[] //{
   INIT(= "E1026: Missing }");
EXTERN Byte e_missing_return_statement[]
   INIT(= "E1027: Missing return statement");
EXTERN Byte e_compiling_def_function_failed[]
   INIT(= "E1028: Compiling :def function failed");
EXTERN Byte e_expected_str_but_got_str[]
   INIT(= "E1029: Expected %s but got %s");
EXTERN Byte e_using_string_as_number_str[]
   INIT(= "E1030: Using a String as a Number: \"%s\"");
EXTERN Byte e_cannot_use_void_value[]
   INIT(= "E1031: Cannot use void value");
EXTERN Byte e_missing_catch_or_finally[]
   INIT(= "E1032: Missing :catch or :finally");
EXTERN Byte e_catch_unreachable_after_catch_all[]
   INIT(= "E1033: Catch unreachable after catch-all");
EXTERN Byte e_cannot_use_reserved_name_str[]
   INIT(= "E1034: Cannot use reserved name %s");
EXTERN Byte e_percent_requires_number_arguments[]
   // xgettext:no-c-format
   INIT(= "E1035: % requires number arguments");
EXTERN Byte e_char_requires_number_or_float_arguments[]
   INIT(= "E1036: %c requires number or float arguments");
EXTERN Byte e_cannot_use_str_with_str[]
   INIT(= "E1037: Cannot use \"%s\" with %s");
EXTERN Byte e_redefining_script_item_str[]
   INIT(= "E1041: Redefining script item: \"%s\"");
EXTERN Byte e_invalid_command_after_export[]
   INIT(= "E1043: Invalid command after :export");
EXTERN Byte e_export_with_invalid_argument[]
   INIT(= "E1044: Export with invalid argument");
// E1045 not used
// E1046 not used
EXTERN Byte e_syntax_error_in_import_str[]
   INIT(= "E1047: Syntax error in import: %s");
EXTERN Byte e_item_not_found_in_script_str[]
   INIT(= "E1048: Item not found in script: %s");
EXTERN Byte e_item_not_exported_in_script_str[]
   INIT(= "E1049: Item not exported in script: %s");
EXTERN Byte e_colon_required_before_range_str[]
   INIT(= "E1050: Colon required before a range: %s");
EXTERN Byte e_wrong_argument_type_for_plus[]
   INIT(= "E1051: Wrong argument type for +");
EXTERN Byte e_cannot_declare_an_option_str[]
   INIT(= "E1052: Cannot declare an option: %s");
EXTERN Byte e_could_not_import_str[]
   INIT(= "E1053: Could not import \"%s\"");
EXTERN Byte e_variable_already_declared_in_script_str[]
   INIT(= "E1054: Variable already declared in the script: %s");
EXTERN Byte e_missing_name_after_dots[]
   INIT(= "E1055: Missing name after ...");
EXTERN Byte e_expected_type_str[]
   INIT(= "E1056: Expected a type: %s");
EXTERN Byte e_missing_enddef[]
   INIT(= "E1057: Missing :enddef");
EXTERN Byte e_function_nesting_too_deep[]
   INIT(= "E1058: Function nesting too deep");
EXTERN Byte e_no_white_space_allowed_before_colon_str[]
   INIT(= "E1059: No white space allowed before colon: %s");
EXTERN Byte e_expected_dot_after_name_str[]
   INIT(= "E1060: Expected dot after name: %s");
EXTERN Byte e_cannot_find_function_str[]
   INIT(= "E1061: Cannot find function %s");
EXTERN Byte e_cannot_index_number[]
   INIT(= "E1062: Cannot index a Number");
EXTERN Byte e_type_mismatch_for_v_variable[]
   INIT(= "E1063: Type mismatch for v: variable");
EXTERN Byte e_yank_register_changed_while_using_it[]
   INIT(= "E1064: Yank register changed while using it");
EXTERN Byte e_command_cannot_be_shortened_str[]
   INIT(= "E1065: Command cannot be shortened: %s");
EXTERN Byte e_cannot_declare_a_register_str[]
   INIT(= "E1066: Cannot declare a register: %s");
EXTERN Byte e_separator_mismatch_str[]
   INIT(= "E1067: Separator mismatch: %s");
EXTERN Byte e_no_white_space_allowed_before_str_str[]
   INIT(= "E1068: No white space allowed before '%s': %s");
EXTERN Byte e_white_space_required_after_str_str[]
   INIT(= "E1069: White space required after '%s': %s");
EXTERN Byte e_invalid_string_for_import_str[]
   INIT(= "E1071: Invalid string for :import: %s");
EXTERN Byte e_cannot_compare_str_with_str[]
   INIT(= "E1072: Cannot compare %s with %s");
EXTERN Byte e_name_already_defined_str[]
   INIT(= "E1073: Name already defined: %s");
EXTERN Byte e_no_white_space_allowed_after_dot[]
   INIT(= "E1074: No white space allowed after dot");
EXTERN Byte e_namespace_not_supported_str[]
   INIT(= "E1075: Namespace not supported: %s");
// E1076 unused (was deleted)
EXTERN Byte e_missing_argument_type_for_str[]
   INIT(= "E1077: Missing argument type for %s");
EXTERN Byte e_invalid_command_nested_did_you_mean_plusplus_nested[]
   INIT(= "E1078: Invalid command \"nested\", did you mean \"++nested\"?");
EXTERN Byte e_cannot_declare_variable_on_command_line[]
   INIT(= "E1079: Cannot declare a variable on the command line");
EXTERN Byte e_invalid_assignment[]
   INIT(= "E1080: Invalid assignment");
EXTERN Byte e_cannot_unlet_str[]
   INIT(= "E1081: Cannot unlet %s");
EXTERN Byte e_command_modifier_without_command[]
   INIT(= "E1082: Command modifier without command");
EXTERN Byte e_missing_backtick[]
   INIT(= "E1083: Missing backtick");
EXTERN Byte e_not_callable_type_str[]
   INIT(= "E1085: Not a callable type: %s");
// E1086 unused
EXTERN Byte e_cannot_use_index_when_declaring_variable[]
   INIT(= "E1087: Cannot use an index when declaring a variable");
EXTERN Byte e_script_cannot_import_itself[]
   INIT(= "E1088: Script cannot import itself");
EXTERN Byte e_unknown_variable_str[]
   INIT(= "E1089: Unknown variable: %s");
EXTERN Byte e_cannot_assign_to_argument_str[]
   INIT(= "E1090: Cannot assign to argument %s");
EXTERN Byte e_function_is_not_compiled_str[]
   INIT(= "E1091: Function is not compiled: %s");
EXTERN Byte e_cannot_nest_redir[]
   INIT(= "E1092: Cannot nest :redir");
EXTERN Byte e_expected_nr_items_but_got_nr[]
   INIT(= "E1093: Expected %d items but got %d");
EXTERN Byte e_import_can_only_be_used_in_script[]
   INIT(= "E1094: Import can only be used in a script");
EXTERN Byte e_unreachable_code_after_str[]
   INIT(= "E1095: Unreachable code after :%s");
EXTERN Byte e_returning_value_in_function_without_return_type[]
   INIT(= "E1096: Returning a value in a function without a return type");
EXTERN Byte e_line_incomplete[]
   INIT(= "E1097: Line incomplete");
EXTERN Byte e_string_list_or_blob_required[]
   INIT(= "E1098: String, List or Blob required");
EXTERN Byte e_unknown_error_while_executing_str[]
   INIT(= "E1099: Unknown error while executing %s");
EXTERN Byte e_cannot_declare_script_variable_in_function_str[]
   INIT(= "E1101: Cannot declare a script variable in a function: %s");
EXTERN Byte e_lambda_function_not_found_str[]
   INIT(= "E1102: Lambda function not found: %s");
EXTERN Byte e_dictionary_not_set[]
   INIT(= "E1103: Dictionary not set");
EXTERN Byte e_missing_gt[]
   INIT(= "E1104: Missing >");
EXTERN Byte e_cannot_convert_str_to_string[]
   INIT(= "E1105: Cannot convert %s to string");

PLURAL_MSG(e_one_argument_too_many, "E1106: One argument too many",
      e_nr_arguments_too_many, "E1106: %d arguments too many")

EXTERN Byte e_string_list_dict_or_blob_required[]
   INIT(= "E1107: String, List, Bag or Blob required");
// E1108 unused
EXTERN Byte e_list_item_nr_is_not_list[]
   INIT(= "E1109: List item %d is not a List");
EXTERN Byte e_list_item_nr_does_not_contain_3_numbers[]
   INIT(= "E1110: List item %d does not contain 3 numbers");
EXTERN Byte e_list_item_nr_range_invalid[]
   INIT(= "E1111: List item %d range invalid");
EXTERN Byte e_list_item_nr_cell_width_invalid[]
   INIT(= "E1112: List item %d cell width invalid");
EXTERN Byte e_overlapping_ranges_for_nr[]
   INIT(= "E1113: Overlapping ranges for 0x%lx");
EXTERN Byte e_only_values_of_0x80_and_higher_supported[]
   INIT(= "E1114: Only values of 0x80 and higher supported");
EXTERN Byte e_assert_fails_fourth_argument[]
   INIT(= "E1115: \"assert_fails()\" fourth argument must be a number");
EXTERN Byte e_assert_fails_fifth_argument[]
   INIT(= "E1116: \"assert_fails()\" fifth argument must be a string");
EXTERN Byte e_cannot_use_bang_with_nested_def_str[]
   INIT(= "E1117: Cannot use ! with nested %s");
EXTERN Byte e_cannot_change_locked_list[]
   INIT(= "E1118: Cannot change locked list");
EXTERN Byte e_cannot_change_locked_list_item[]
   INIT(= "E1119: Cannot change locked list item");
EXTERN Byte e_cannot_change_dict[]
   INIT(= "E1120: Cannot change dict");
EXTERN Byte e_cannot_change_dict_item[]
   INIT(= "E1121: Cannot change dict item");
EXTERN Byte e_variable_is_locked_str[]
   INIT(= "E1122: Variable is locked: %s");
EXTERN Byte e_missing_comma_before_argument_str[]
   INIT(= "E1123: Missing comma before argument: %s");
EXTERN Byte e_str_cannot_be_used_in_vim_script[]
   INIT(= "E1124: \"%s\" cannot be used in Vim script");
EXTERN Byte e_final_requires_a_value[]
   INIT(= "E1125: Final requires a value");
EXTERN Byte e_missing_name_after_dot[]
   INIT(= "E1127: Missing name after dot");
EXTERN Byte e_endblock_without_block[] //{
   INIT(= "E1128: } without {"); //}
EXTERN Byte e_throw_with_empty_string[]
   INIT(= "E1129: Throw with empty string");
EXTERN Byte e_cannot_add_to_null_list[]
   INIT(= "E1130: Cannot add to null list");
EXTERN Byte e_cannot_add_to_null_blob[]
   INIT(= "E1131: Cannot add to null blob");
EXTERN Byte e_missing_function_argument[]
   INIT(= "E1132: Missing function argument");
EXTERN Byte e_cannot_extend_null_dict[]
   INIT(= "E1133: Cannot extend a null dict");
EXTERN Byte e_cannot_extend_null_list[]
   INIT(= "E1134: Cannot extend a null list");
EXTERN Byte e_using_string_as_bool_str[]
   INIT(= "E1135: Using a String as a Boole: \"%s\"");
EXTERN Byte e_cmd_mapping_must_end_with_cr_before_second_cmd[]
   INIT(= "E1136: <Cmd> mapping must end with <CR> before second <Cmd>");
// E1137 unused
EXTERN Byte e_using_bool_as_number[]
   INIT(= "E1138: Using a Boole as a Number");
EXTERN Byte e_missing_matching_bracket_after_dict_key[]
   INIT(= "E1139: Missing matching bracket after dict key");
EXTERN Byte e_for_argument_must_be_sequence_of_lists_or_tuples[]
   INIT(= "E1140: :for argument must be a sequence of lists");
EXTERN Byte e_indexable_type_required[]
   INIT(= "E1141: Indexable type required");
EXTERN Byte e_calling_test_garbagecollect_now_while_v_testing_is_not_set[]
   INIT(= "E1142: Calling test_garbagecollect_now() while v:testing is not set");
EXTERN Byte e_empty_expression_str[]
   INIT(= "E1143: Empty expression: \"%s\"");
EXTERN Byte e_command_str_not_followed_by_white_space_str[]
   INIT(= "E1144: Command \"%s\" is not followed by white space: %s");
EXTERN Byte e_missing_heredoc_end_marker_str[]
   INIT(= "E1145: Missing heredoc end marker: %s");
EXTERN Byte e_command_not_recognized_str[]
   INIT(= "E1146: Command not recognized: %s");
EXTERN Byte e_list_not_set[]
   INIT(= "E1147: List not set");
EXTERN Byte e_cannot_index_str[]
   INIT(= "E1148: Cannot index a %s");
EXTERN Byte e_script_variable_invalid_after_reload_in_function_str[]
   INIT(= "E1149: Script variable is invalid after reload in function %s");
EXTERN Byte e_script_variable_type_changed[]
   INIT(= "E1150: Script variable type changed");
EXTERN Byte e_mismatched_endfunction[]
   INIT(= "E1151: Mismatched endfunction");
EXTERN Byte e_mismatched_enddef[]
   INIT(= "E1152: Mismatched enddef");
EXTERN Byte e_invalid_operation_for_str[]
   INIT(= "E1153: Invalid operation for %s");
EXTERN Byte e_divide_by_zero[]
   INIT(= "E1154: Divide by zero");
EXTERN Byte e_cannot_define_autocommands_for_all_events[]
   INIT(= "E1155: Cannot define autocommands for ALL events");
EXTERN Byte e_cannot_change_arglist_recursively[]
   INIT(= "E1156: Cannot change the argument list recursively");
EXTERN Byte e_missing_return_type[]
   INIT(= "E1157: Missing return type");
EXTERN Byte e_cannot_split_portal_when_closing_buffer[]
   INIT(= "E1159: Cannot split a portal when closing the buffer");
EXTERN Byte e_cannot_use_default_for_variable_arguments[]
   INIT(= "E1160: Cannot use a default for variable arguments");
EXTERN Byte e_cannot_json_encode_str[]
   INIT(= "E1161: Cannot json encode a %s");
EXTERN Byte e_register_name_must_be_one_char_str[]
   INIT(= "E1162: Register name must be one character: %s");
EXTERN Byte e_variable_nr_type_mismatch_expected_str_but_got_str[]
   INIT(= "E1163: Variable %d: type mismatch, expected %s but got %s");
EXTERN Byte e_variable_nr_type_mismatch_expected_str_but_got_str_in_str[]
   INIT(= "E1163: Variable %d: type mismatch, expected %s but got %s in %s");
EXTERN Byte e_cannot_use_range_with_assignment_str[]
   INIT(= "E1165: Cannot use a range with an assignment: %s");
EXTERN Byte e_cannot_use_range_with_dictionary[]
   INIT(= "E1166: Cannot use a range with a dictionary");
EXTERN Byte e_argument_name_shadows_existing_variable_str[]
   INIT(= "E1167: Argument name shadows existing variable: %s");
EXTERN Byte e_argument_already_declared_in_script_str[]
   INIT(= "E1168: Argument already declared in the script: %s");
EXTERN Byte e_expression_too_recursive_str[]
   INIT(= "E1169: Expression too recursive: %s");
EXTERN Byte e_cannot_use_hash_curly_to_start_comment[]
   INIT(= "E1170: Cannot use #{ to start a comment");
EXTERN Byte e_missing_end_block[]
   INIT(= "E1171: Missing } after inline function");
EXTERN Byte e_cannot_use_default_values_in_lambda[]
   INIT(= "E1172: Cannot use default values in a lambda");
EXTERN Byte e_text_found_after_str_str[]
   INIT(= "E1173: Text found after %s: %s");
EXTERN Byte e_string_required_for_argument_nr[]
   INIT(= "E1174: String required for argument %d");
EXTERN Byte e_non_empty_string_required_for_argument_nr[]
   INIT(= "E1175: Non-empty string required for argument %d");
EXTERN Byte e_misplaced_command_modifier[]
   INIT(= "E1176: Misplaced command modifier");
EXTERN Byte e_for_loop_on_str_not_supported[]
   INIT(= "E1177: For loop on %s not supported");
EXTERN Byte e_cannot_lock_unlock_local_variable[]
   INIT(= "E1178: Cannot lock or unlock a local variable");
EXTERN Byte e_failed_to_extract_pwd_from_str_check_your_shell_config[]
   INIT(= "E1179: Failed to extract PWD from %s, check your shell's config related to OSC 7");
EXTERN Byte e_variable_arguments_type_must_be_list_str[]
   INIT(= "E1180: Variable arguments type must be a list: %s");
EXTERN Byte e_cannot_use_underscore_here[]
   INIT(= "E1181: Cannot use an underscore here");
EXTERN Byte e_cannot_use_range_with_assignment_operator_str[]
   INIT(= "E1183: Cannot use a range with an assignment operator: %s");
EXTERN Byte e_blob_not_set[]
   INIT(= "E1184: Blob not set");
EXTERN Byte e_missing_redir_end[]
   INIT(= "E1185: Missing :redir END");
EXTERN Byte e_expression_does_not_result_in_value_str[]
   INIT(= "E1186: Expression does not result in a value: %s");
EXTERN Byte e_failed_to_source_defaults[]
   INIT(= "E1187: Failed to source defaults.vim");
EXTERN Byte e_cannot_open_terminal_from_command_line_window[]
   INIT(= "E1188: Cannot open a terminal from the command line window");
EXTERN Byte e_cannot_use_legacy_with_command_str[]
   INIT(= "E1189: Cannot use :legacy with this command: %s");

PLURAL_MSG(e_one_argument_too_few, "E1190: One argument too few",
      e_nr_arguments_too_few, "E1190: %d arguments too few")

EXTERN Byte e_call_to_function_that_failed_to_compile_str[]
   INIT(= "E1191: Call to function that failed to compile: %s");
EXTERN Byte e_empty_function_name[]
   INIT(= "E1192: Empty function name");
EXTERN Byte e_no_white_space_allowed_after_str_str[]
   INIT(= "E1202: No white space allowed after '%s': %s");
EXTERN Byte e_dot_not_allowed_after_str_str[]
   INIT(= "E1203: Dot not allowed after a %s: %s");
EXTERN Byte e_regexp_number_after_dot_pos_search_chr[]
   INIT(= "E1204: No Number allowed after .: '\\%%%c'");
EXTERN Byte e_no_white_space_allowed_between_option_and[]
   INIT(= "E1205: No white space allowed between option and");
EXTERN Byte e_dict_required_for_argument_nr[]
   INIT(= "E1206: Dictionary required for argument %d");
EXTERN Byte e_expression_without_effect_str[]
   INIT(= "E1207: Expression without an effect: %s");
EXTERN Byte e_complete_used_without_allowing_arguments[]
   INIT(= "E1208: -complete used without allowing arguments");
EXTERN Byte e_invalid_value_for_line_number_str[]
   INIT(= "E1209: Invalid value for a line number: \"%s\"");
EXTERN Byte e_number_required_for_argument_nr[]
   INIT(= "E1210: Number required for argument %d");
EXTERN Byte e_list_required_for_argument_nr[]
   INIT(= "E1211: List required for argument %d");
EXTERN Byte e_bool_required_for_argument_nr[]
   INIT(= "E1212: Boole required for argument %d");
EXTERN Byte e_redefining_imported_item_str[]
   INIT(= "E1213: Redefining imported item \"%s\"");
EXTERN Byte e_chan_or_job_required_for_argument_nr[]
   INIT(= "E1217: Channel or Job required for argument %d");
EXTERN Byte e_job_required_for_argument_nr[]
   INIT(= "E1218: Job required for argument %d");
EXTERN Byte e_float_or_number_required_for_argument_nr[]
   INIT(= "E1219: Float or Number required for argument %d");
EXTERN Byte e_string_or_number_required_for_argument_nr[]
   INIT(= "E1220: String or Number required for argument %d");
EXTERN Byte e_string_or_blob_required_for_argument_nr[]
   INIT(= "E1221: String or Blob required for argument %d");
EXTERN Byte e_string_or_list_required_for_argument_nr[]
   INIT(= "E1222: String or List required for argument %d");
EXTERN Byte e_string_or_dict_required_for_argument_nr[]
   INIT(= "E1223: String or Dictionary required for argument %d");
EXTERN Byte e_string_number_or_list_required_for_argument_nr[]
   INIT(= "E1224: String, Number or List required for argument %d");
EXTERN Byte e_string_list_or_dict_required_for_argument_nr[]
   INIT(= "E1225: String, List or Dictionary required for argument %d");
EXTERN Byte e_list_or_blob_required_for_argument_nr[]
   INIT(= "E1226: List or Blob required for argument %d");
EXTERN Byte e_list_or_dict_required_for_argument_nr[]
   INIT(= "E1227: List or Dictionary required for argument %d");
EXTERN Byte e_list_dict_or_blob_required_for_argument_nr[]
   INIT(= "E1228: List, Dictionary or Blob required for argument %d");
EXTERN Byte e_expected_dictionary_for_using_key_str_but_got_str[]
   INIT(= "E1229: Expected dictionary for using key \"%s\", but got %s");
EXTERN Byte e_cannot_use_bar_to_separate_commands_here_str[]
   INIT(= "E1231: Cannot use a bar to separate commands here: %s");
EXTERN Byte e_argument_of_exists_compiled_must_be_literal_string[]
   INIT(= "E1232: Argument of exists_compiled() must be a literal string");
EXTERN Byte e_exists_compiled_can_only_be_used_in_def_function[]
   INIT(= "E1233: exists_compiled() can only be used in a :def function");
EXTERN Byte e_legacy_must_be_followed_by_command[]
   INIT(= "E1234: legacy must be followed by a command");
EXTERN Byte e_bool_or_number_required_for_argument_nr[]
   INIT(= "E1235: Boole or Number required for argument %d");
EXTERN Byte e_cannot_use_str_itself_it_is_imported[]
   INIT(= "E1236: Cannot use %s itself, it is imported");
EXTERN Byte e_no_such_user_defined_command_in_current_buffer_str[]
   INIT(= "E1237: No such user-defined command in current buffer: %s");
EXTERN Byte e_blob_required_for_argument_nr[]
   INIT(= "E1238: Blob required for argument %d");
EXTERN Byte e_invalid_value_for_blob_nr[]
   INIT(= "E1239: Invalid value for blob: %d");
EXTERN Byte e_resulting_text_too_long[]
   INIT(= "E1240: Resulting text too long");
EXTERN Byte e_separator_not_supported_str[]
   INIT(= "E1241: Separator not supported: %s");
EXTERN Byte e_no_white_space_allowed_before_separator_str[]
   INIT(= "E1242: No white space allowed before separator: %s");
EXTERN Byte e_bad_color_string_str[]
   INIT(= "E1244: Bad color string: %s");
EXTERN Byte e_cannot_find_variable_to_unlock_str[]
   INIT(= "E1246: Cannot find variable to (un)lock: %s");
EXTERN Byte e_line_number_out_of_range[]
   INIT(= "E1247: Line number out of range");
EXTERN Byte e_closure_called_from_invalid_context[]
   INIT(= "E1248: Closure called from invalid context");
EXTERN Byte e_highlight_group_name_too_long[]
   INIT(= "E1249: Highlight group name too long: %s");
EXTERN Byte e_argument_of_str_must_be_list_string_dictionary_or_blob[]
   INIT(= "E1250: Argument of %s must be a List, String, Dictionary or Blob");
EXTERN Byte e_list_tuple_dict_blob_or_string_required_for_argument_nr[]
   INIT(= "E1251: List, Dictionary, Blob or String required for argument %d");
EXTERN Byte e_string_list_or_blob_required_for_argument_nr[]
   INIT(= "E1252: String, List or Blob required for argument %d");
EXTERN Byte e_string_list_tuple_or_blob_required_for_argument_nr[]
   INIT(= "E1253: String, List or Blob required for argument %d");
// E1253 unused
EXTERN Byte e_cannot_use_script_variable_in_for_loop[]
   INIT(= "E1254: Cannot use script variable in for loop");
EXTERN Byte e_cmd_mapping_must_end_with_cr[]
   INIT(= "E1255: <Cmd> mapping must end with <CR>");
EXTERN Byte e_string_or_function_required_for_argument_nr[]
   INIT(= "E1256: String or function required for argument %d");
EXTERN Byte e_imported_script_must_use_as_or_end_in_dot_vim_str[]
   INIT(= "E1257: Imported script must use \"as\" or end in .vim: %s");
EXTERN Byte e_no_dot_after_imported_name_str[]
   INIT(= "E1258: No '.' after imported name: %s");
EXTERN Byte e_missing_name_after_imported_name_str[]
   INIT(= "E1259: Missing name after imported name: %s");
EXTERN Byte e_cannot_unlet_imported_item_str[]
   INIT(= "E1260: Cannot unlet an imported item: %s");
EXTERN Byte e_cannot_import_dot_vim_without_using_as[]
   INIT(= "E1261: Cannot import .vim without using \"as\"");
EXTERN Byte e_cannot_import_same_script_twice_str[]
   INIT(= "E1262: Cannot import the same script twice: %s");
EXTERN Byte e_autoload_import_cannot_use_absolute_or_relative_path[]
   INIT(= "E1264: Autoload import cannot use absolute or relative path: %s");
EXTERN Byte e_cannot_use_partial_here[]
   INIT(= "E1265: Cannot use a partial here");
EXTERN Byte e_function_name_must_start_with_capital_str[]
   INIT(= "E1267: Function name must start with a capital: %s");
EXTERN Byte e_compiling_closure_without_context_str[]
   INIT(= "E1271: Compiling closure without context: %s");
EXTERN Byte e_using_type_not_in_script_context_str[]
   INIT(= "E1272: Using type not in a script context: %s");
EXTERN Byte e_nfa_regexp_missing_value_in_chr[]
   INIT(= "E1273: (NFA regexp) missing value in '\\%%%c'");
EXTERN Byte e_no_script_file_name_to_substitute_for_script[]
   INIT(= "E1274: No script file name to substitute for \"<script>\"");
EXTERN Byte e_string_or_function_required_for_arrow_parens_expr[]
   INIT(= "E1275: String or function required for ->(expr)");
EXTERN Byte e_illegal_map_mode_string_str[]
   INIT(= "E1276: Illegal map mode string: '%s'");
EXTERN Byte e_stray_closing_curly_str[] //{
   INIT(= "E1278: Stray '}' without a matching '{': %s");
EXTERN Byte e_missing_close_curly_str[]
   INIT(= "E1279: Missing '}': %s");
EXTERN Byte e_illegal_character_in_word[]
   INIT(= "E1280: Illegal character in word");
EXTERN Byte e_atom_engine_must_be_at_start_of_pattern[]
   INIT(= "E1281: Atom '\\%%#=%c' must be at the start of the pattern");
EXTERN Byte e_bitshift_ops_must_be_number[]
   INIT(= "E1282: Bitshift operands must be numbers");
EXTERN Byte e_bitshift_ops_must_be_positive[]
   INIT(= "E1283: Bitshift amount must be a positive number");
EXTERN Byte e_argument_1_list_item_nr_dictionary_required[]
   INIT(= "E1284: Argument 1, list item %d: Dictionary required");
EXTERN Byte e_could_not_clear_timeout_str[]
   INIT(= "E1285: Could not clear timeout: %s");
EXTERN Byte e_could_not_set_timeout_str[]
   INIT(= "E1286: Could not set timeout: %s");
#ifndef PROF_NSEC
EXTERN Byte e_could_not_set_handler_for_timeout_str[]
   INIT(= "E1287: Could not set handler for timeout: %s");
EXTERN Byte e_could_not_reset_handler_for_timeout_str[]
   INIT(= "E1288: Could not reset handler for timeout: %s");
EXTERN Byte e_could_not_check_for_pending_sigalrm_str[]
   INIT(= "E1289: Could not check for pending SIGALRM: %s");
#endif
EXTERN Byte e_substitute_nesting_too_deep[]
   INIT(= "E1290: substitute nesting too deep");
EXTERN Byte e_cmdline_window_already_open[]
   INIT(= "E1292: Command-line window is already open");
EXTERN Byte e_cannot_use_negative_id_after_adding_textprop_with_text[]
   INIT(= "E1293: Cannot use a negative id after adding a textprop with text");
EXTERN Byte e_can_only_use_text_align_when_column_is_zero[]
   INIT(= "E1294: Can only use text_align when column is zero");
EXTERN Byte e_cannot_specify_both_type_and_types[]
   INIT(= "E1295: Cannot specify both 'type' and 'types'");
EXTERN Byte e_can_only_use_left_padding_when_column_is_zero[]
   INIT(= "E1296: Can only use left padding when column is zero");
EXTERN Byte e_non_null_dict_required_for_argument_nr[]
   INIT(= "E1297: Non-NULL Dictionary required for argument %d");
EXTERN Byte e_non_null_list_required_for_argument_nr[]
   INIT(= "E1298: Non-NULL List required for argument %d");
EXTERN Byte e_window_unexpectedly_close_while_searching_for_tags[]
   INIT(= "E1299: Window unexpectedly closed while searching for tags");
EXTERN Byte e_cannot_use_partial_with_dictionary_for_defer[]
   INIT(= "E1300: Cannot use a partial with dictionary for :defer");
EXTERN Byte e_repeatable_type_required_for_argument_nr[]
   INIT(= "E1301: String, Number, List or Blob required for argument %d");
EXTERN Byte e_script_variable_was_deleted[]
   INIT(= "E1302: Script variable was deleted");
EXTERN Byte e_custom_list_completion_function_does_not_return_list_but_str[]
   INIT(= "E1303: Custom list completion function does not return a List but a %s");
EXTERN Byte e_cannot_use_type_with_this_variable_str[]
   INIT(= "E1304: Cannot use type with this variable: %s");
EXTERN Byte e_cannot_use_length_endcol_and_endlnum_with_text[]
   INIT(= "E1305: Cannot use \"length\", \"end_col\" and \"end_lnum\" with \"text\"");
EXTERN Byte e_loop_nesting_too_deep[]
   INIT(= "E1306: Loop nesting too deep");
EXTERN Byte e_argument_nr_trying_to_modify_const_str[]
   INIT(= "E1307: Argument %d: Trying to modify a const %s");
EXTERN Byte e_cannot_resize_portal_in_another_tab[]
   INIT(= "E1308: Cannot resize a portal in another tab");
EXTERN Byte e_cannot_change_mappings_while_listing[]
   INIT(= "E1309: Cannot change mappings while listing");
EXTERN Byte e_cannot_change_menus_while_listing[]
   INIT(= "E1310: Cannot change menus while listing");
EXTERN Byte e_cannot_change_user_commands_while_listing[]
   INIT(= "E1311: Cannot change user commands while listing");
EXTERN Byte e_not_allowed_to_change_portal_layout_in_this_autocmd[]
   INIT(= "E1312: Not allowed to change the portal layout in this autocmd");
EXTERN Byte e_not_allowed_to_add_or_remove_entries_str[]
   INIT(= "E1313: Not allowed to add or remove entries (%s)");
EXTERN Byte e_class_name_must_start_with_uppercase_letter_str[]
   INIT(= "E1314: Class name must start with an uppercase letter: %s");
EXTERN Byte e_white_space_required_after_name_str[]
   INIT(= "E1315: White space required after name: %s");
EXTERN Byte e_invalid_object_variable_declaration_str[]
   INIT(= "E1317: Invalid object variable declaration: %s");
EXTERN Byte e_not_valid_command_in_class_str[]
   INIT(= "E1318: Not a valid command in a class: %s");
// E1319 unused
EXTERN Byte e_using_object_as_number[]
   INIT(= "E1320: Using an Object as a Number");
// E1321 unused
EXTERN Byte e_using_object_as_float[]
   INIT(= "E1322: Using an Object as a Float");
// E1323 unused
EXTERN Byte e_using_object_as_string[]
   INIT(= "E1324: Using an Object as a String");
EXTERN Byte e_method_not_found_on_class_str_str[]
   INIT(= "E1325: Method \"%s\" not found in class \"%s\"");
EXTERN Byte e_variable_not_found_on_object_str_str[]
   INIT(= "E1326: Variable \"%s\" not found in object \"%s\"");
EXTERN Byte e_object_required_found_str[]
   INIT(= "E1327: Object required, found %s");
EXTERN Byte e_constructor_default_value_must_be_vnone_str[]
   INIT(= "E1328: Constructor default value must be v:none: %s");
EXTERN Byte e_invalid_class_variable_declaration_str[]
   INIT(= "E1329: Invalid class variable declaration: %s");
EXTERN Byte e_invalid_type_for_object_variable_str[]
   INIT(= "E1330: Invalid type for object variable: %s");
EXTERN Byte e_public_must_be_followed_by_var_static_final_or_const[]
   INIT(= "E1331: public must be followed by \"var\" or \"static\" or \"final\" or \"const\"");
EXTERN Byte e_public_variable_name_cannot_start_with_underscore_str[]
   INIT(= "E1332: public variable name cannot start with underscore: %s");
EXTERN Byte e_cannot_access_protected_variable_str[]
   INIT(= "E1333: Cannot access protected variable \"%s\" in class \"%s\"");
// E1334 unused
EXTERN Byte e_variable_is_not_writable_str[]
   INIT(= "E1335: Variable \"%s\" in class \"%s\" is not writable");
EXTERN Byte e_internal_error_shortmess_too_long[]
   INIT(= "E1336: Internal error: shortmess too long");
EXTERN Byte e_class_variable_str_not_found_in_class_str[]
   INIT(= "E1337: Class variable \"%s\" not found in class \"%s\"");
// E1338 unused
EXTERN Byte e_cannot_add_textprop_with_text_after_using_textprop_with_negative_id[]
   INIT(= "E1339: Cannot add a textprop with text after using a textprop with a negative id");
EXTERN Byte e_argument_already_declared_in_class_str[]
   INIT(= "E1340: Argument already declared in the class: %s");
EXTERN Byte e_variable_already_declared_in_class_str[]
   INIT(= "E1341: Variable already declared in the class: %s");
EXTERN Byte e_interface_name_must_start_with_uppercase_letter_str[]
   INIT(= "E1343: Interface name must start with an uppercase letter: %s");
EXTERN Byte e_cannot_initialize_variable_in_interface[]
   INIT(= "E1344: Cannot initialize a variable in an interface");
EXTERN Byte e_not_valid_command_in_interface_str[]
   INIT(= "E1345: Not a valid command in an interface: %s");
EXTERN Byte e_interface_name_not_found_str[]
   INIT(= "E1346: Interface name not found: %s");
EXTERN Byte e_not_valid_interface_str[]
   INIT(= "E1347: Not a valid interface: %s");
EXTERN Byte e_variable_str_of_interface_str_not_implemented[]
   INIT(= "E1348: Variable \"%s\" of interface \"%s\" is not implemented");
EXTERN Byte e_method_str_of_interface_str_not_implemented[]
   INIT(= "E1349: Method \"%s\" of interface \"%s\" is not implemented");
EXTERN Byte e_duplicate_implements[]
   INIT(= "E1350: Duplicate \"implements\"");
EXTERN Byte e_duplicate_interface_after_implements_str[]
   INIT(= "E1351: Duplicate interface after \"implements\": %s");
EXTERN Byte e_duplicate_extends[]
   INIT(= "E1352: Duplicate \"extends\"");
EXTERN Byte e_class_name_not_found_str[]
   INIT(= "E1353: Class name not found: %s");
EXTERN Byte e_cannot_extend_str[]
   INIT(= "E1354: Cannot extend %s");
EXTERN Byte e_duplicate_function_str[]
   INIT(= "E1355: Duplicate function: %s");
EXTERN Byte e_super_must_be_followed_by_dot[]
   INIT(= "E1356: \"super\" must be followed by a dot");
EXTERN Byte e_using_super_not_in_class_method[]
   INIT(= "E1357: Using \"super\" not in a class method");
EXTERN Byte e_using_super_not_in_child_class[]
   INIT(= "E1358: Using \"super\" not in a child class");
EXTERN Byte e_cannot_define_new_method_in_abstract_class[]
   INIT(= "E1359: Cannot define a \"new\" method in an abstract class");
EXTERN Byte e_using_null_object[]
   INIT(= "E1360: Using a null object");
EXTERN Byte e_cannot_use_color_none_did_you_mean_none[]
   INIT(= "E1361: Cannot use color \"none\", did you mean \"NONE\"?");
EXTERN Byte e_cannot_use_non_null_object[]
   INIT(= "E1362: Cannot use a non-null object");
EXTERN Byte e_incomplete_type[]
   INIT(= "E1363: Incomplete type");
EXTERN Byte e_warning_pointer_block_corrupted[]
   INIT(= "E1364: Warning: Pointer block corrupted");
EXTERN Byte e_cannot_use_a_return_type_with_new_method[]
   INIT(= "E1365: Cannot use a return type with the \"new\" method");
EXTERN Byte e_cannot_access_protected_method_str[]
   INIT(= "E1366: Cannot access protected method: %s");
EXTERN Byte e_variable_str_of_interface_str_has_different_access[]
   INIT(= "E1367: Access level of variable \"%s\" of interface \"%s\" is different");
EXTERN Byte e_static_must_be_followed_by_var_def_final_or_const[]
   INIT(= "E1368: Static must be followed by \"var\" or \"def\" or \"final\" or \"const\"");
EXTERN Byte e_duplicate_variable_str[]
   INIT(= "E1369: Duplicate variable: %s");
EXTERN Byte e_cannot_define_new_method_as_static[]
   INIT(= "E1370: Cannot define a \"new\" method as static");
EXTERN Byte e_abstract_must_be_followed_by_def[]
   INIT(= "E1371: Abstract must be followed by \"def\"");
EXTERN Byte e_abstract_method_in_concrete_class[]
   INIT(= "E1372: Abstract method \"%s\" cannot be defined in a concrete class");
EXTERN Byte e_abstract_method_str_not_implemented[]
   INIT(= "E1373: Abstract method \"%s\" is not implemented");
EXTERN Byte e_class_variable_str_accessible_only_inside_class_str[]
   INIT(= "E1374: Class variable \"%s\" accessible only inside class \"%s\"");
EXTERN Byte e_class_variable_str_accessible_only_using_class_str[]
   INIT(= "E1375: Class variable \"%s\" accessible only using class \"%s\"");
EXTERN Byte e_object_variable_str_accessible_only_using_object_str[]
   INIT(= "E1376: Object variable \"%s\" accessible only using class \"%s\" object");
EXTERN Byte e_method_str_of_class_str_has_different_access[]
   INIT(= "E1377: Access level of method \"%s\" is different in class \"%s\"");
EXTERN Byte e_static_member_not_supported_in_interface[]
   INIT(= "E1378: Static member not supported in an interface");
EXTERN Byte e_protected_variable_not_supported_in_interface[]
   INIT(= "E1379: Protected variable not supported in an interface");
EXTERN Byte e_protected_method_not_supported_in_interface[]
   INIT(= "E1380: Protected method not supported in an interface");
EXTERN Byte e_interface_cannot_use_implements[]
   INIT(= "E1381: Interface cannot use \"implements\"");
EXTERN Byte e_variable_str_type_mismatch_expected_str_but_got_str[]
   INIT(= "E1382: Variable \"%s\": type mismatch, expected %s but got %s");
EXTERN Byte e_method_str_type_mismatch_expected_str_but_got_str[]
   INIT(= "E1383: Method \"%s\": type mismatch, expected %s but got %s");
EXTERN Byte e_class_method_str_accessible_only_inside_class_str[]
   INIT(= "E1384: Class method \"%s\" accessible only inside class \"%s\"");
EXTERN Byte e_class_method_str_accessible_only_using_class_str[]
   INIT(= "E1385: Class method \"%s\" accessible only using class \"%s\"");
EXTERN Byte e_object_method_str_accessible_only_using_object_str[]
   INIT(= "E1386: Object method \"%s\" accessible only using class \"%s\" object");
EXTERN Byte e_public_variable_not_supported_in_interface[]
   INIT(= "E1387: public variable not supported in an interface");
EXTERN Byte e_public_keyword_not_supported_for_method[]
   INIT(= "E1388: public keyword not supported for a method");
EXTERN Byte e_missing_name_after_implements[]
   INIT(= "E1389: Missing name after implements");
EXTERN Byte e_cannot_use_an_object_variable_except_with_the_new_method_str[]
   INIT(= "E1390: Cannot use an object variable \"this.%s\" except with the \"new\" method");
EXTERN Byte e_cannot_lock_object_variable_str[]
   INIT(= "E1391: Cannot (un)lock variable \"%s\" in class \"%s\"");
EXTERN Byte e_cannot_lock_class_variable_str[]
   INIT(= "E1392: Cannot (un)lock class variable \"%s\" in class \"%s\"");
EXTERN Byte e_type_name_must_start_with_uppercase_letter_str[]
   INIT(= "E1394: Type name must start with an uppercase letter: %s");
EXTERN Byte e_using_null_class[]
   INIT(= "E1395: Using a null class");
EXTERN Byte e_typealias_already_exists_for_str[]
   INIT(= "E1396: Type alias \"%s\" already exists");
EXTERN Byte e_missing_typealias_name[]
   INIT(= "E1397: Missing type alias name");
EXTERN Byte e_missing_typealias_type[]
   INIT(= "E1398: Missing type alias type");
EXTERN Byte e_type_can_only_be_used_in_script[]
   INIT(= "E1399: Type can only be used in a script");
EXTERN Byte e_using_typealias_as_value_str[]
   INIT(= "E1403: Type alias \"%s\" cannot be used as a value");
EXTERN Byte e_abstract_cannot_be_used_in_interface[]
   INIT(= "E1404: Abstract cannot be used in an interface");
EXTERN Byte e_using_class_as_value_str[]
   INIT(= "E1405: Class \"%s\" cannot be used as a value");
EXTERN Byte e_using_typealias_as_var_val[]
   INIT(= "E1407: Cannot use a Typealias as a variable or value");
EXTERN Byte e_final_variable_not_supported_in_interface[]
   INIT(= "E1408: Final variable not supported in an interface");
EXTERN Byte e_cannot_change_readonly_variable_str_in_class_str[]
   INIT(= "E1409: Cannot change read-only variable \"%s\" in class \"%s\"");
EXTERN Byte e_const_variable_not_supported_in_interface[]
   INIT(= "E1410: Const variable not supported in an interface");
EXTERN Byte e_missing_dot_after_object_str[]
   INIT(= "E1411: Missing dot after object \"%s\"");
EXTERN Byte e_builtin_object_method_str_not_supported[]
   INIT(= "E1412: Builtin object method \"%s\" not supported");
EXTERN Byte e_builtin_class_method_not_supported[]
   INIT(= "E1413: Builtin class method not supported");
EXTERN Byte e_enum_name_must_start_with_uppercase_letter_str[]
   INIT(= "E1415: Enum name must start with an uppercase letter: %s");
EXTERN Byte e_enum_cannot_extend_class[]
   INIT(= "E1416: Enum cannot extend a class or enum");
EXTERN Byte e_abstract_cannot_be_used_in_enum[]
   INIT(= "E1417: Abstract cannot be used in an Enum");
EXTERN Byte e_invalid_enum_value_declaration_str[]
   INIT(= "E1418: Invalid enum value declaration: %s");
EXTERN Byte e_not_valid_command_in_enum_str[]
   INIT(= "E1419: Not a valid command in an Enum: %s");
EXTERN Byte e_missing_endenum[]
   INIT(= "E1420: Missing :endenum");
EXTERN Byte e_using_enum_as_value_str[]
   INIT(= "E1421: Enum \"%s\" cannot be used as a value");
EXTERN Byte e_enum_value_str_not_found_in_enum_str[]
   INIT(= "E1422: Enum value \"%s\" not found in enum \"%s\"");
EXTERN Byte e_enumvalue_str_cannot_be_modified[]
   INIT(= "E1423: Enum value \"%s.%s\" cannot be modified");
EXTERN Byte e_using_enum_str_as_number[]
   INIT(= "E1424: Using an Enum \"%s\" as a Number");
EXTERN Byte e_using_enum_str_as_string[]
   INIT(= "E1425: Using an Enum \"%s\" as a String");
EXTERN Byte e_enum_str_ordinal_cannot_be_modified[]
   INIT(= "E1426: Enum \"%s\" ordinal value cannot be modified");
EXTERN Byte e_enum_str_name_cannot_be_modified[]
   INIT(= "E1427: Enum \"%s\" name cannot be modified");
EXTERN Byte e_duplicate_enum_str[]
   INIT(= "E1428: Duplicate enum value: %s");
EXTERN Byte e_class_can_only_be_used_in_script[]
   INIT(= "E1429: Class can only be used in a script");
EXTERN Byte e_uninitialized_object_var_reference[]
   INIT(= "E1430: Uninitialized object variable '%s' referenced");
EXTERN Byte e_abstract_method_str_direct[]
   INIT(= "E1431: Abstract method \"%s\" in class \"%s\" cannot be accessed directly");
EXTERN Byte e_generic_method_str_override_with_concrete_method_in_class_str[]
   INIT(= "E1432: Overriding generic method \"%s\" in class \"%s\" with a concrete method");
EXTERN Byte e_concrete_method_str_override_with_generic_method_in_class_str[]
   INIT(= "E1433: Overriding concrete method \"%s\" in class \"%s\" with a generic method");
EXTERN Byte e_generic_method_str_type_arguments_mismatch_in_class_str[]
   INIT(= "E1434: Mismatched number of type variables for generic method  \"%s\" in class \"%s\"");
EXTERN Byte e_enum_can_only_be_used_in_script[]
   INIT(= "E1435: Enum can only be used in a script");
EXTERN Byte e_interface_can_only_be_used_in_script[]
   INIT(= "E1436: Interface can only be used in a script");
EXTERN Byte e_cannot_mix_positional_and_non_positional_str[]
   INIT(= "E1500: Cannot mix positional and non-positional arguments: %s");
EXTERN Byte e_fmt_arg_nr_unused_str[]
   INIT(= "E1501: format argument %d unused in $-style format: %s");
EXTERN Byte e_positional_num_field_spec_reused_str_str[]
   INIT(= "E1502: Positional argument %d used as field width reused as different type: %s/%s");
EXTERN Byte e_positional_nr_out_of_bounds_str[]
   INIT(= "E1503: Positional argument %d out of bounds: %s");
EXTERN Byte e_positional_arg_num_type_inconsistent_str_str[]
   INIT(= "E1504: Positional argument %d type used inconsistently: %s/%s");
EXTERN Byte e_invalid_format_specifier_str[]
   INIT(= "E1505: Invalid format specifier: %s");
EXTERN Byte e_xattr_erange[]
   INIT(= "E1506: Book too small to copy xattr value or key");
EXTERN Byte e_aptypes_is_null_nr_str[]
   INIT(= "E1507: Internal error: ap_types or ap_types[idx] is NULL: %d: %s");
EXTERN Byte e_xattr_e2big[]
   INIT(= "E1508: Size of the extended attribute value is larger than the maximum size allowed");
EXTERN Byte e_xattr_other[]
   INIT(= "E1509: Error occurred when reading or writing extended attribute");
EXTERN Byte e_val_too_large[]
   INIT(= "E1510: Value too large: %s");
EXTERN Byte e_wrong_number_of_characters_for_field_str[]
   INIT(= "E1511: Wrong number of characters for field \"%s\"");
EXTERN Byte e_wrong_character_width_for_field_str[]
   INIT(= "E1512: Wrong character width for field \"%s\"");
EXTERN Byte e_portfixbuf_cannot_go_to_buffer[]
   INIT(= "E1513: Cannot switch buffer. 'portfixbuf' is enabled");
EXTERN Byte e_invalid_return_type_from_findfunc[]
   INIT(= "E1514: 'findfunc' did not return a List type");
EXTERN Byte e_string_list_tuple_or_blob_required[]
   INIT(= "E1523: String, List or Blob required");
EXTERN Byte e_cannot_use_tuple_with_function_str[]
   INIT(= "E1524: Cannot use a tuple with function %s");
EXTERN Byte e_argument_of_str_must_be_list_tuple_string_dictionary_or_blob[]
   INIT(= "E1525: Argument of %s must be a List, String, Dictionary or Blob");
EXTERN Byte e_list_or_tuple_or_blob_required_for_argument_nr[]
   INIT(= "E1528: List or Blob required for argument %d");
EXTERN Byte e_list_or_tuple_required_for_argument_nr[]
   INIT(= "E1529: List required for argument %d");
EXTERN Byte e_list_or_tuple_or_dict_required_for_argument_nr[]
   INIT(= "E1530: List or Dictionary required for argument %d");
EXTERN Byte e_argument_of_str_must_be_list_tuple_dictionary_or_blob[]
   INIT(= "E1531: Argument of %s must be a List, Dictionary or Blob");
EXTERN Byte e_tuple_is_immutable[]
   INIT(= "E1532: Cannot modify a tuple");
EXTERN Byte e_cannot_slice_tuple[]
   INIT(= "E1533: Cannot slice a tuple");
EXTERN Byte e_list_or_tuple_required[]
   INIT(= "E1535: List required");
EXTERN Byte e_unicode_val_too_large[]
   INIT(= "E1541: Value too large, max Unicode codepoint is U+10FFFF");
EXTERN Byte e_cannot_have_negative_or_zero_number_of_quickfix[]
   INIT(= "E1542: Cannot have a negative or zero number of quickfix/location lists");
EXTERN Byte e_cannot_have_more_than_hundred_quickfix[]
   INIT(= "E1543: Cannot have more than a hundred quickfix/location lists");
EXTERN Byte e_failed_resizing_quickfix_stack[]
   INIT(= "E1544: Failed resizing the quickfix/location list stack");
EXTERN Byte e_cannot_switch_to_a_closing_buffer[]
   INIT(= "E1546: Cannot switch to a closing buffer");
EXTERN Byte e_wayland_connection_unavailable[]
   INIT(= "E1548: Wayland connection is unavailable");
EXTERN Byte e_cannot_have_more_than_nr_diff_anchors[]
   INIT(= "E1549: Cannot have more than %d diff anchors");
EXTERN Byte e_failed_to_find_all_diff_anchors[]
   INIT(= "E1550: Failed to find all diff anchors");
EXTERN Byte e_cannot_open_a_popup_portal_to_a_closing_buffer[]
   INIT(= "E1551: Cannot open a popup portal into a closing buffer");
EXTERN Byte e_type_var_name_must_start_with_uppercase_letter_str[]
   INIT(= "E1552: Type variable name must start with an uppercase letter: %s");
EXTERN Byte e_missing_comma_in_generic_function_str[]
   INIT(= "E1553: Missing comma after type in generic function: %s");
EXTERN Byte e_missing_closing_angle_bracket_in_generic_function_str[]
   INIT(= "E1554: Missing '>' in generic function: %s");
EXTERN Byte e_empty_type_list_for_generic_function_str[]
   INIT(= "E1555: Empty type list specified for generic function '%s'");
EXTERN Byte e_too_many_types_for_generic_function_str[]
   INIT(= "E1556: Too many types specified for generic function '%s'");
EXTERN Byte e_not_enough_types_for_generic_function_str[]
   INIT(= "E1557: Not enough types specified for generic function '%s'");
EXTERN Byte e_unknown_generic_function_str[]
   INIT(= "E1558: Unknown generic function: %s");
EXTERN Byte e_generic_func_missing_type_args_str[]
   INIT(= "E1559: Type arguments missing for generic function '%s'");
EXTERN Byte e_not_a_generic_function_str[]
   INIT(= "E1560: Not a generic function: %s");
EXTERN Byte e_duplicate_type_var_name_str[]
   INIT(= "E1561: Duplicate type variable name: %s");
EXTERN Byte e_diff_anchors_with_hidden_windows[]
   INIT(= "E1562: Diff anchors cannot be used with hidden diff portals");
EXTERN Byte e_cannot_disable_unmodifiable[]
   INIT(= "E1563: The 'modifiable' option cannot be disabled once it's set");
EXTERN Byte e_use_get_not_set_for_reading_options[]
   INIT(= "E1564: Use :get, not :set, for reading option values");
EXTERN Byte e_argument_must_not_be_negative[]
   INIT(= "E1565: Argument must not be negative");
EXTERN Byte e_illegal_combination_of_flags_str[]
   INIT(= "E1567: Illegal combination of flags in the option %s");
EXTERN Byte e_trying_to_set_option_to_wrong_type[]
   INIT(= "E1569: Trying to set option to wrong type");
EXTERN Byte e_bool_required_after_equal[]
   INIT(= "E1570: Boole required after =");
EXTERN Byte e_enum_required_after_equal[]
   INIT(= "E1571: Enum required after =");
EXTERN Byte e_flags_required_after_equal[]
   INIT(= "E1572: Flags required after =");
EXTERN Byte e_setter_required_for_enum_or_flag_option[]
   INIT(= "E1573: Enumeration, flags and callback options require a setter function!");
EXTERN Byte e_options_are_frozen[]
   INIT(= "E1574: Options are frozen and cannot be changed!");
EXTERN Byte e_symlink_dereference_error[]
   INIT(= "E1575: Symlink derefence error");
EXTERN Byte e_symlink_dereference_buffer_overflow[]
   INIT(= "E1576: Buffer overflow during symlink derefence");
   
EXTERN CS e_printf INIT(= e_insufficient_arguments_for_printf);
   
//}}}

// Note: a NULL argument for eeRealloc() is not portable, don't use it.
#if defined(MEM_PROFILE)
#define eeRealloc(ptr, size) memReallocWithProfiling((ptr), (size))
#else
#define eeRealloc(ptr, size) realloc((ptr), (size))
#endif

//Return byte length of character that starts with byte "b".
//Return 1 for a single-byte character.
//MB_BYTE2LEN_CHECK() can be used to count a special key as one byte.
//Don't call MB_BYTE2LEN(b) with b > 255!
#define MB_BYTE2LEN_CHECK(b) (((b) > 255) ? 1 : utf8CharLens[b])

// values for eeHandleSignal() that are not a signal
#define SIGNAL_BLOCK   (-1)
#define SIGNAL_UNBLOCK  (-2)

// flags for skipEeglGrepPat()
#define VGR_GLOBAL  1
#define VGR_NOJUMP  2
#define VGR_FUZZY   4

// behavior for bad character, "++bad=" argument
#define BAD_REPLACE   '?'   // replace it with '?' (default)
#define BAD_KEEP    1000   // leave it
#define BAD_DROP    1002   // erase it

// last argument for do_source()
#define DOSO_NONE  0
#define DOSO_INIT  1   // loading init.vim file

// flags for read_eeglinfo() and children
#define EIF_WANT_INFO       1   // load non-mark info
#define EIF_WANT_MARKS      2   // load file marks
#define EIF_ONLY_CURBOOK    4   // bail out after loading marks for curBook
#define EIF_FORCEIT         8   // overwrite info already read
#define EIF_GET_OLDFILES   16   // load v:oldfiles

// flags for buf_freeall()
#define BFA_DEL          1   // bbook is going to be deleted
#define BFA_WIPE         2   // book is going to be wiped out
#define BFA_KEEP_UNDO    4   // do not free undo information
#define BFA_IGNORE_ABORT 8   // do not abort for aborting()

// direction for nv_mousescroll() and ins_mousescroll()
#define MSCR_DOWN      0   // DOWN must be FALSE
#define MSCR_UP        1
#define MSCR_LEFT   (-1)
#define MSCR_RIGHT  (-2)

#define KEYLEN_INCOMPLETE_KEYCODE (-1)
#define KEYLEN_INCOMPLETE_MAPPING (-2)
#define KEYLEN_REMOVED  9999   // keylen value for removed sequence

// Flags for get_reg_contents
#define GREG_NO_EXPR  1 //Do not allow expression register
#define GREG_EXPR_SRC 2 //Return expression itself for "=" register
#define GREG_LIST     4 //Return list

// Options for json_encode() and json_decode.
#define JSON_NO_NONE 1   // v:none item not allowed
#define JSON_NL      2   // append a NL

// Used for flags of doInPath()
#define DIP_ALL       0x01   // all matches, not just the first one
#define DIP_DIR       0x02   // find directories instead of files.
#define DIP_ERR       0x04   // give an error message when none found.
#define DIP_START   0x08   // also use "start" directory in 'packpath'
#define DIP_OPT       0x10   // also use "opt" directory in 'packpath'
#define DIP_NORTP   0x20   // do not use 'runtimepath'
#define DIP_NOAFTER 0x40   // skip "after" directories
#define DIP_AFTER   0x80   // only use "after" directories

// Used by the garbage collector.
#define COPYID_INC 2
#define COPYID_MASK (~0x1)

#define FOLD_TEXT_LEN 51  //buffer size for get_foldtext()

// Values for trans_function_name() argument:
#define TFN_INT         0x01   // internal function name OK
#define TFN_QUIET       0x02   // no error messages
#define TFN_NO_AUTOLOAD 0x04   // do not use script autoloading
#define TFN_NO_DEREF    0x08   // do not dereference a Funcref
#define TFN_READ_ONLY   0x10   // will not change the var
#define TFN_NO_DECL     0x20   // only used for GLV_NO_DECL
#define TFN_NEW_FUNC    0x40   // defining a new function
#define TFN_ASSIGN_WITH_OP 0x80  // only for GLV_ASSIGN_WITH_OP
#define TFN_IN_CLASS    0x100   // function in a class

// Values for get_lval() flags argument:
#define GLV_QUIET   TFN_QUIET   // no error messages
#define GLV_NO_AUTOLOAD   TFN_NO_AUTOLOAD   // do not use script autoloading
#define GLV_READ_ONLY   TFN_READ_ONLY   // will not change the var
#define GLV_NO_DECL   TFN_NO_DECL   // assignment without :var or :let
#define GLV_ASSIGN_WITH_OP TFN_ASSIGN_WITH_OP // assignment with operator
#define GLV_PREFER_FUNC   0x10000      // prefer function above variable
#define GLV_FOR_LOOP   0x20000      // assigning to a loop variable

#define DO_NOT_FREE_CNT 99999   //refcount for dict or list that should not be freed.

// flags for find_name_end()
#define FNE_INCL_BR       1   //include [] in name
#define FNE_CHECK_START   2   //check name starts with valid character

// stat macros
#ifndef S_ISREG
# ifdef S_IFREG
#  define S_ISREG(m)   (((m) & S_IFMT) == S_IFREG)
# else
#  define S_ISREG(m)   0
# endif
#endif
#ifndef S_ISBLK
# ifdef S_IFBLK
#  define S_ISBLK(m)   (((m) & S_IFMT) == S_IFBLK)
# else
#  define S_ISBLK(m)   0
# endif
#endif
#ifndef S_ISSOCK
# ifdef S_IFSOCK
#  define S_ISSOCK(m)   (((m) & S_IFMT) == S_IFSOCK)
# else
#  define S_ISSOCK(m)   0
# endif
#endif
#ifndef S_ISFIFO
# ifdef S_IFIFO
#  define S_ISFIFO(m)   (((m) & S_IFMT) == S_IFIFO)
# else
#  define S_ISFIFO(m)   0
# endif
#endif
#ifndef S_ISLNK
# ifdef S_IFLNK
#  define S_ISLNK(m)   (((m) & S_IFMT) == S_IFLNK)
# else
#  define S_ISLNK(m)   0
# endif
#endif

#define ELAPSED_TIMEVAL
#define ELAPSED_INIT(v) gettimeofday(&(v), NULL)
#define ELAPSED_FUNC(v) elapsed(&(v))
long elapsed(TimeVal* start_tv);

// Replacement for nchar used by nv_replace().
#define REPLACE_CR_NCHAR    4294967295
#define REPLACE_NL_NCHAR    4294967294

// Flags for adjust_prop_columns()
#define APC_SAVE_FOR_UNDO  1   //call u_savesub() before making changes
#define APC_SUBSTITUTE     2   //text is replaced, not inserted
#define APC_INDENT         4   //changing indent

// Flags for replace_termcodes()
#define REPTERM_FROM_PART   1
#define REPTERM_DO_LT       2
#define REPTERM_SPECIAL     4
#define REPTERM_NO_SIMPLIFY 8

// Flags for termFindSpecialKey()
#define FSK_KEYCODE     0x01   // prefer key code, e.g. K_DEL instead of DEL
#define FSK_KEEP_X_KEY  0x02   // don't translate xHome to Home key
#define FSK_IN_STRING   0x04   // TRUE in string, double quote is escaped
#define FSK_SIMPLIFY    0x08   // simplify <C-H> and <A-x>
#define FSK_FROM_PART   0x10   // left-hand-side of mapping

// Flags for mch_delay.
#define MCH_DELAY_IGNOREINPUT 1
#define MCH_DELAY_SETTMODE    2

//Fuzzy matching
#define FUZZY_MATCH_MAX_LEN   1024 // max characters that can be matched
#define FUZZY_SCORE_NONE   INT_MIN // invalid fuzzy score

//flags for equal_type()
#define ETYPE_ARG_UNKNOWN 1

// flags used by copyStr_fnameescape()
#define VSE_NONE   0
#define VSE_SHELL  1   //escape for a shell command
#define VSE_BOOK   2   //escape for a ":book" command

#define SYNTAX_MAX_COL 256 //maximum column for syntax coloring

#ifndef EEGLINFO_FILE
#define EEGLINFO_FILE  "$HOME/foo/.eeglinfo"
#define EEGLINFO_FILE2 "~/bar/.eeglinfo"
#endif

#define LOC_LIST_MAKE      0 //selectable with ":list m"
#define LOC_LIST_GREP      1 //selectable with ":list g"
#define LOC_LIST_HELP      2 //selectable with ":list h"
#define LOC_LIST_TAGS      3 //selectable with ":list t"
#define LOC_LIST_BOOKMARKS 4 //selectable with ":list b"
#define LOC_LIST_CSCOPE    5 //selectable with ":list c"
#define COUNT_LOC_LISTS    6 //= 1 + highest LIST_...value

typedef struct dirent DirEntry;

#define FNAME_ILLEGAL "\"*?><|" // illegal characters in a file name
#define _bp(cond) if (cond) {__bp();}
#define tConst(literal) (Text){.c = (CS)literal, .len = sizeof(literal) - 1}

#define INIT_FILE S"~/.config/eegl/init.vim"
#define FILETYPES_FILE S"~/.config/eegl/filetype.vim"
#define SESSION_FILE   "Session.vim"
#ifndef FILETYPE_FILE //used for file type detection
# define FILETYPE_FILE   "filetype.vim"
#endif
#ifndef FTPLUGIN_FILE //used for loading filetype plugin files
# define FTPLUGIN_FILE   "ftplugin.vim"
#endif
#ifndef INDENT_FILE   //used for loading indent files
# define INDENT_FILE     "indent.vim"
#endif
#ifndef FTOFF_FILE    //used for file type detection
# define FTOFF_FILE      "ftoff.vim"
#endif
#ifndef FTPLUGOF_FILE //used for loading settings files
# define FTPLUGOF_FILE   "ftplugof.vim"
#endif
#ifndef INDOFF_FILE   //used for loading indent files
# define INDOFF_FILE     "indoff.vim"
#endif
#ifndef SYS_OPTWIN_FILE
# define SYS_OPTWIN_FILE "$EEGLRUNTIME/optwin.vim"
#endif
//RUNTIME_DIRNAME Generic name for the directory of the runtime files.
#ifndef RUNTIME_DIRNAME
# define RUNTIME_DIRNAME "runtime"
#endif

#ifndef SIG_ERR
# define SIG_ERR   ((SigHandler)-1)
#endif
#ifndef SIG_HOLD
# define SIG_HOLD   ((SigHandler)-2)
#endif


#endif // EEGL__H
