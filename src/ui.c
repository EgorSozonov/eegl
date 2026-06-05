//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//## ui.c: terminal-based user interface

//Terminal portal support, see ":help :terminal".
//
//There are three parts:
//1. Generic code for all systems. It's the libvterm for the terminal emulator.
//3. The Unix-like implementation. Uses pseudo-tty's (pty's).
//
//For each terminal one VTerm is constructed. This uses libvterm. A version of this library is in 
//this file.
//
//When a terminal portal is opened, a job is started that will be connected to the terminal 
//emulator.
//
//If the terminal portal has keyboard focus, typed keys are converted to the terminal encoding 
//and writing to the job over a channel.
//
//If the job produces output, it is written to the terminal emulator.  The terminal emulator 
//invokes callbacks when its screen content changes.  The line range is stored in 
//dirtyRowStart and dirtyRowEnd.  Once in a while, if the terminal portal is visible, 
//the screen contents is drawn.
//
//When the job ends the text is put in a buffer.  Redrawing then happens from that buffer, 
//decorations come from the scrollback buffer scrollback.
//When the buffer is changed it is turned into a normal buffer, the decorations in scrollback 
//are no longer used.

#include "eegl.h"
#include <stdarg.h>
int fstat(int fd, struct stat* statbuf); //from sys/stat.h
int stat(const char* restrict path, struct stat* restrict buf);

# define XT_TRACE_DELAY   50   // delay for xterm tracing

//{{{libvterm
//{{{types

typedef struct {
  Unt row;
  Unt col;
} VTermPos;

typedef struct {
  // libvterm relies on this memory to be zeroed out before it is returned
  // by the allocator
  void *(*malloc)(Unt size, void *allocdata);
  void  (*free)(void *ptr, void *allocdata);
} VTermAllocatorFunctions;

// Specifies a rectangular screen area.
typedef struct {
   Unt start_row;
   Unt end_row;
   Unt start_col;
   Unt end_col;
} VTermRect;

typedef struct {
   CS str;
   Unt      len : 30;
   unsigned int  initial : 1;
   unsigned int  final : 1;
} VTermStringFragment;

struct VTermBuilder {
  int ver; // currently unused but reserved for some sort of ABI version flag

  int rows, cols;

  const VTermAllocatorFunctions *allocator;
  void *allocdata;

  // Override default sizes for various structures
  Unt outbuffer_len;  // default: 4096
  Unt tmpbuffer_len;  // default: 4096
};

//Bit-field describing the value of VTermColor.type
typedef enum {
   //If the lower bit of `type` is not set, the colour is 24-bit RGB.
   VTERM_COLOR_RGB = 0x00,

   //The colour is an index into a palette of 256 colours.
   VTERM_COLOR_INDEXED = 0x01,

   //Mask that can be used to extract the RGB/Indexed bit.
   VTERM_COLOR_TYPE_MASK = 0x01,

   //If set, indicates that this colour should be the default foreground
   //color, i.e. there was no SGR request for another colour. When
   //rendering this colour it is possible to ignore "idx" and just use a
   //color that is not in the palette.
   VTERM_COLOR_DEFAULT_FG = 0x02,

   //If set, indicates that this colour should be the default background
   //color, i.e. there was no SGR request for another colour. A common
   //option when rendering this colour is to not render a background at
   //all, for example by rendering the window transparently at this spot.
   VTERM_COLOR_DEFAULT_BG = 0x04,

   //Mask that can be used to extract the default foreground/background bit.
   VTERM_COLOR_DEFAULT_MASK = 0x06,

   //If set, indicates that the color is invalid.
   VTERM_COLOR_INVALID = 0x08
} VTermColorType;

enum {
  VTERM_UNDERLINE_OFF,
  VTERM_UNDERLINE_SINGLE,
  VTERM_UNDERLINE_DOUBLE,
  VTERM_UNDERLINE_CURLY,
};

enum {
  VTERM_BASELINE_NORMAL,
  VTERM_BASELINE_RAISE,
  VTERM_BASELINE_LOWER,
};

typedef struct {
  VTermPos pos;
  int	   buttons;
#define MOUSE_BUTTON_LEFT 0x01
#define MOUSE_BUTTON_MIDDLE 0x02
#define MOUSE_BUTTON_RIGHT 0x04
  int      flags;
#define MOUSE_WANT_CLICK 0x01
#define MOUSE_WANT_DRAG  0x02
#define MOUSE_WANT_MOVE  0x04
  // useful to add protocol?
} VTermMouseState;


typedef enum {
  /* VTERM_PROP_NONE = 0 */
  VTERM_PROP_CURSORVISIBLE = 1, // bool
  VTERM_PROP_CURSORBLINK,       // bool
  VTERM_PROP_ALTSCREEN,         // bool
  VTERM_PROP_TITLE,             // string
  VTERM_PROP_ICONNAME,          // string
  VTERM_PROP_REVERSE,           // bool
  VTERM_PROP_CURSORSHAPE,       // number
  VTERM_PROP_MOUSE,             // number
  VTERM_PROP_FOCUSREPORT,       // bool
  VTERM_PROP_CURSORCOLOR,       // string

  VTERM_N_PROPS
} VTermProp;

typedef struct {
    unsigned int bold      : 1;
    unsigned int underline : 2;
    unsigned int italic    : 1;
    unsigned int blink     : 1;
    unsigned int reverse   : 1;
    unsigned int conceal   : 1;
    unsigned int strike    : 1;
    unsigned int font      : 4; /* 0 to 9 */
    unsigned int dwl       : 1; /* On a DECDWL or DECDHL line */
    unsigned int dhl       : 2; /* On a DECDHL line (1=top 2=bottom) */
    unsigned int small     : 1;
    unsigned int baseline  : 2;
} VTermScreenCellAttrs;

typedef enum {
  VTERM_ATTR_BOLD_MASK       = 1 << 0,
  VTERM_ATTR_UNDERLINE_MASK  = 1 << 1,
  VTERM_ATTR_ITALIC_MASK     = 1 << 2,
  VTERM_ATTR_BLINK_MASK      = 1 << 3,
  VTERM_ATTR_REVERSE_MASK    = 1 << 4,
  VTERM_ATTR_STRIKE_MASK     = 1 << 5,
  VTERM_ATTR_FONT_MASK       = 1 << 6,
  VTERM_ATTR_FOREGROUND_MASK = 1 << 7,
  VTERM_ATTR_BACKGROUND_MASK = 1 << 8,
  VTERM_ATTR_CONCEAL_MASK    = 1 << 9,
  VTERM_ATTR_SMALL_MASK      = 1 << 10,
  VTERM_ATTR_BASELINE_MASK   = 1 << 11,

  VTERM_ALL_ATTRS_MASK = (1 << 12) - 1
} VTermAttrMask;

typedef union {
  int boolean;
  int number;
  VTermStringFragment string;
  VTermColor color;
} VTermValue;

// Any cell can contain at most one basic printing character and 5 combining
// characters. This number could be changed but will be ABI-incompatible if you do
#define VTERM_MAX_CHARS_PER_CELL 6

typedef struct {
  uint32_t chars[VTERM_MAX_CHARS_PER_CELL];
  char     width;
  VTermScreenCellAttrs attrs;
  VTermColor fg, bg;
} VTermScreenCell;

// All fields are optional, NULL when not used.
typedef struct {
   int (*damage)(VTermRect rect, void *user);
   int (*moverect)(VTermRect dest, VTermRect src, void *user);
   int (*movecursor)(VTermPos pos, VTermPos oldpos, int visible, void *user);
   int (*settermprop)(VTermProp prop, VTermValue *val, void *user);
   int (*bell)(void *user);
   int (*resize)(int rows, int cols, void *user);
   // A line was pushed off the top of the window.
   // "cells[cols]" contains the cells of that line. Return value is unused.
   int (*sb_pushline)(int cols, const VTermScreenCell *cells, void *user);
   int (*sb_popline)(int cols, VTermScreenCell *cells, void *user);
   int (*sb_clear)(void* user);
} VTermScreenCallbacks;

typedef enum {
   VTERM_DAMAGE_CELL,    /* every cell */
   VTERM_DAMAGE_ROW,     /* entire rows */
   VTERM_DAMAGE_SCREEN,  /* entire screen */
   VTERM_DAMAGE_SCROLL,  /* entire screen + scrollrect */

   VTERM_N_DAMAGES
} VTermDamageSize;

typedef enum {
   // VTERM_ATTR_NONE = 0
   VTERM_ATTR_BOLD = 1,   // bool:   1, 22
   VTERM_ATTR_UNDERLINE,  // number: 4, 21, 24
   VTERM_ATTR_ITALIC,     // bool:   3, 23
   VTERM_ATTR_BLINK,      // bool:   5, 25
   VTERM_ATTR_REVERSE,    // bool:   7, 27
   VTERM_ATTR_CONCEAL,    // bool:   8, 28
   VTERM_ATTR_STRIKE,     // bool:   9, 29
   VTERM_ATTR_FONT,       // number: 10-19
   VTERM_ATTR_FOREGROUND, // color:  30-39 90-97
   VTERM_ATTR_BACKGROUND, // color:  40-49 100-107
   VTERM_ATTR_SMALL,      // bool:   73, 74, 75
   VTERM_ATTR_BASELINE,   // number: 73, 74, 75

   VTERM_N_ATTRS
} VTermAttr;

typedef enum {
   // VTERM_VALUETYPE_NONE = 0 */
   VTERM_VALUETYPE_BOOL = 1,
   VTERM_VALUETYPE_INT,
   VTERM_VALUETYPE_STRING,
   VTERM_VALUETYPE_COLOR,

   VTERM_N_VALUETYPES
} VTermValueType;

declStruct(VTermLineInfo);

//Copies of VTermState fields that the 'resize' callback might have reason to edit. 'resize' 
//callback gets total control of these fields and may free-and-reallocate them if required. They
//will be copied back from the struct after the callback has returned.
typedef struct {
   VTermPos pos;                // current cursor position
   VTermLineInfo *lineinfos[2]; // [1] may be NULL
} VTermStateFields;

enum {
   VTERM_PROP_CURSORSHAPE_BLOCK = 1,
   VTERM_PROP_CURSORSHAPE_UNDERLINE,
   VTERM_PROP_CURSORSHAPE_BAR_LEFT,

   VTERM_N_PROP_CURSORSHAPES
};

enum {
  VTERM_PROP_MOUSE_NONE = 0,
  VTERM_PROP_MOUSE_CLICK,
  VTERM_PROP_MOUSE_DRAG,
  VTERM_PROP_MOUSE_MOVE,

  VTERM_N_PROP_MICE
};

//}}}
//{{{includes
//{{{utilities

#define STRFrect "(%d,%d-%d,%d)"
#define ARGSrect(r) (r).start_row, (r).start_col, (r).end_row, (r).end_col


// Expand dst to contain src as well
private void 
rect_expand(VTermRect *dst, VTermRect *src) {
  if (dst->start_row > src->start_row) dst->start_row = src->start_row;
  if (dst->start_col > src->start_col) dst->start_col = src->start_col;
  if (dst->end_row   < src->end_row)   dst->end_row   = src->end_row;
  if (dst->end_col   < src->end_col)   dst->end_col   = src->end_col;
}

// Clip the dst to ensure it does not step outside of bounds
private void 
rect_clip(VTermRect *dst, VTermRect *bounds) {
  if (dst->start_row < bounds->start_row) dst->start_row = bounds->start_row;
  if (dst->start_col < bounds->start_col) dst->start_col = bounds->start_col;
  if (dst->end_row   > bounds->end_row)   dst->end_row   = bounds->end_row;
  if (dst->end_col   > bounds->end_col)   dst->end_col   = bounds->end_col;
  // Ensure it doesn't end up negatively-sized
  if (dst->end_row < dst->start_row) dst->end_row = dst->start_row;
  if (dst->end_col < dst->start_col) dst->end_col = dst->start_col;
}

// True if the two rectangles are equal
private int rect_equal(VTermRect *a, VTermRect *b) {
  return (a->start_row == b->start_row) &&
         (a->start_col == b->start_col) &&
         (a->end_row   == b->end_row)   &&
         (a->end_col   == b->end_col);
}

/* True if small is contained entirely within big */
private int 
rect_contains(VTermRect *big, VTermRect *small) {
  if (small->start_row < big->start_row) return 0;
  if (small->start_col < big->start_col) return 0;
  if (small->end_row   > big->end_row)   return 0;
  if (small->end_col   > big->end_col)   return 0;
  return 1;
}

// True if the rectangles overlap at all
private int 
rect_intersects(VTermRect *a, VTermRect *b) {
   if (a->start_row > b->end_row || b->start_row > a->end_row)
      return 0;
   if (a->start_col > b->end_col || b->start_col > a->end_col)
      return 0;
   return 1;
}

// A handy macro for defaulting values out of builder fields
#define DEFAULT(v, def)  ((v) ? (v) : (def))

//True if the VTERM_COLOR_INDEXED `type` flag is set, indicating that the
//given VTermColor instance is an indexed colour.
#define VTERM_COLOR_IS_INDEXED(col) \
  (((col)->type & VTERM_COLOR_TYPE_MASK) == VTERM_COLOR_INDEXED)

//True if the VTERM_COLOR_RGB `type` flag is set, indicating that
//the given VTermColor instance is an rgb colour.
#define VTERM_COLOR_IS_RGB(col) \
  (((col)->type & VTERM_COLOR_TYPE_MASK) == VTERM_COLOR_RGB)

//True if the VTERM_COLOR_DEFAULT_FG `type` flag is set, indicating
//that the given VTermColor instance corresponds to the default foreground color.
#define VTERM_COLOR_IS_DEFAULT_FG(col) \
  (!!((col)->type & VTERM_COLOR_DEFAULT_FG))

//True if the VTERM_COLOR_DEFAULT_BG `type` flag is set, indicating
//that the given VTermColor instance corresponds to the default background color.
#define VTERM_COLOR_IS_DEFAULT_BG(col) \
  (!!((col)->type & VTERM_COLOR_DEFAULT_BG))

//Return true if the VTERM_COLOR_INVALID `type` flag is set, indicating
//that the given VTermColor instance is an invalid color.
#define VTERM_COLOR_IS_INVALID(col) (!!((col)->type & VTERM_COLOR_INVALID))

#define VTERM_VERSION_MAJOR 0
#define VTERM_VERSION_MINOR 3
#define VTERM_VERSION_PATCH 3

#define VTERM_CHECK_VERSION vterm_check_version(VTERM_VERSION_MAJOR, VTERM_VERSION_MINOR)

#define VTERM_KEY_FUNCTION(n) (VTERM_KEY_FUNCTION_0+(n))

//}}}
//{{{internal

#define VTERM_MAX_COLS 1000
#define VTERM_MAX_ROWS 1000

#if defined(__GNUC__)
# define UNUSED __attribute__((unused))
#else
# define UNUSED
#endif

#ifdef DEBUG
# define DEBUG_LOG(s) fprintf(stderr, s)
# define DEBUG_LOG1(s, a) fprintf(stderr, s, a)
# define DEBUG_LOG2(s, a, b) fprintf(stderr, s, a, b)
# define DEBUG_LOG3(s, a, b, c) fprintf(stderr, s, a, b, c)
#else
# define DEBUG_LOG(s)
# define DEBUG_LOG1(s, a)
# define DEBUG_LOG2(s, a, b)
# define DEBUG_LOG3(s, a, b, c)
#endif

#define ESC_S "\x1b"

#define INTERMED_MAX 16

#define CSI_ARGS_MAX 16
#define CSI_LEADER_MAX 16

#define BUFIDX_PRIMARY   0
#define BUFIDX_ALTSCREEN 1


struct VTermPen {
   VTermColor fg;
   VTermColor bg;
   unsigned int bold:1;
   unsigned int underline:2;
   unsigned int italic:1;
   unsigned int blink:1;
   unsigned int reverse:1;
   unsigned int conceal:1;
   unsigned int strike:1;
   unsigned int font:4; /* To store 0-9 */
   unsigned int small:1;
   unsigned int baseline:2;
};

declStruct(VTermState);
declStruct(VTermScreen);
declStruct(VTerm);

typedef void VTermOutputCallback(const char *s, Unt len, void *user);

declStruct(VTermGlyphInfo);

typedef struct {
   int (*text)(Byte *bytes, Unt len, void *user);
   int (*control)(Byte control, void *user);
   int (*escape)(Byte *bytes, Unt len, void *user);
   int (*csi)(Byte* leader, const long args[], int argcount, CS intermed, Byte command, void *user);
   int (*osc)(int command, VTermStringFragment frag, void *user);
   int (*dcs)(Byte* command, Unt commandlen, VTermStringFragment frag, void *user);
   int (*apc)(VTermStringFragment frag, void *user);
   int (*pm)(VTermStringFragment frag, void *user);
   int (*sos)(VTermStringFragment frag, void *user);
   Boole (*resize)(Unt rows, Unt cols, void *user);
} VTermParserCallbacks;

struct VTermLineInfo {
  unsigned int    doublewidth:1;     /* DECDWL or DECDHL line */
  unsigned int    doubleheight:2;    /* DECDHL line (1=top 2=bottom) */
  unsigned int    continuation:1;    /* Line is a flow continuation of the previous */
};

typedef struct {
   int (*putglyph)(VTermGlyphInfo *info, VTermPos pos, void *user);
   int (*movecursor)(VTermPos pos, VTermPos oldpos, int visible, void *user);
   int (*scrollrect)(VTermRect rect, int downward, int rightward, void *user);
   int (*moverect)(VTermRect dest, VTermRect src, void *user);
   int (*erase)(VTermRect rect, int selective, void *user);
   int (*initpen)(void *user);
   int (*setpenattr)(VTermAttr attr, VTermValue *val, void *user);
   // Callback for setting various properties.  Must return 1 if the property
   // was accepted, 0 otherwise.
   int (*settermprop)(VTermProp prop, VTermValue *val, void *user);
   int (*bell)(void *user);
   int (*resize)(int rows, int cols, VTermStateFields *fields, void *user);
   int (*setlineinfo)(int row, const VTermLineInfo *newinfo, const VTermLineInfo *oldinfo, void *user);
   int (*sb_clear)(void *user);
} VTermStateCallbacks;

struct VTerm {
   const VTermAllocatorFunctions *allocator;
   void *allocdata;

   int rows;
   int cols;

   struct {
      enum VTermParserState {
         VT_NORMAL,
         VT_CSI_LEADER,
         VT_CSI_ARGS,
         VT_CSI_INTERMED,
         VT_DCS_COMMAND,
         // below here are the "string states"
         VT_OSC_COMMAND,
         VT_OSC,
         VT_DCS,
         VT_APC,
         VT_PM,
         VT_SOS,
   } state;

   unsigned int in_esc : 1;

   int intermedlen;
   Byte intermed[INTERMED_MAX];

   union {
      struct {
        int leaderlen;
        Byte leader[CSI_LEADER_MAX];

        int argi;
        long args[CSI_ARGS_MAX];
      } csi;
      struct {
        int command;
      } osc;
      struct {
        int commandlen;
        Byte command[CSI_LEADER_MAX];
      } dcs;
    } v;

    const VTermParserCallbacks *callbacks;
    void *cbdata;

    int string_initial;

    int emit_nul;
  } parser;

  // len == malloc()ed size; cur == number of valid bytes

  VTermOutputCallback *outfunc;
  void                *outdata;

  char  *outbuffer;
  Unt outbuffer_len;
  Unt outbuffer_cur;

  char  *tmpbuffer;
  Unt tmpbuffer_len;

  VTermState *state;
  VTermScreen *screen;

  int in_backspace;
};


typedef struct {
  int (*control)(unsigned char control, void *user);
  int (*csi)(CS leader, const long args[], int argcount, CS intermed, Byte command, void *user);
  int (*osc)(int command, VTermStringFragment frag, void *user);
  int (*dcs)(CS command, Unt commandlen, VTermStringFragment frag, void *user);
  int (*apc)(VTermStringFragment frag, void *user);
  int (*pm)(VTermStringFragment frag, void *user);
  int (*sos)(VTermStringFragment frag, void *user);
} VTermStateFallbacks;

typedef enum {
  VTERM_SELECTION_CLIPBOARD = (1<<0),
  VTERM_SELECTION_PRIMARY   = (1<<1),
  VTERM_SELECTION_SECONDARY = (1<<2),
  VTERM_SELECTION_SELECT    = (1<<3),
  VTERM_SELECTION_CUT0      = (1<<4), /* also CUT1 .. CUT7 by bitshifting */
} VTermSelectionMask;

typedef struct {
  int (*set)(VTermSelectionMask mask, VTermStringFragment frag, void *user);
  int (*query)(VTermSelectionMask mask, void *user);
} VTermSelectionCallbacks;

struct VTermState {
   VTerm *vt;

   const VTermStateCallbacks *callbacks;
   void *cbdata;

   const VTermStateFallbacks *fallbacks;
   void *fbdata;

   Unt rows;
   Unt cols;

   // Current cursor position */
   VTermPos pos;

   int at_phantom; /* True if we're on the "81st" phantom column to defer a wraparound */

   Unt scrollregion_top;
   Unt scrollregion_bottom; // UNT means unbounded
#define SCROLLREGION_BOTTOM(state) \
      ((state)->scrollregion_bottom < UNT ? (state)->scrollregion_bottom : (state)->rows)
   Unt scrollregion_left;
#define SCROLLREGION_LEFT(state)  \
      ((state)->mode.leftrightmargin ? (state)->scrollregion_left : 0)
   Unt scrollregion_right; // UNT means unbounded
#define SCROLLREGION_RIGHT(state) ((state)->mode.leftrightmargin \
      && (state)->scrollregion_right < UNT ? (state)->scrollregion_right : (state)->cols)

   // Bitvector of tab stops
   unsigned char *tabstops;

   // Primary and Altscreen; lineinfos[1] is lazily allocated as needed 
   VTermLineInfo *lineinfos[2];

   // lineinfo will == lineinfos[0] or lineinfos[1], depending on altscreen
   VTermLineInfo *lineinfo;
#define ROWWIDTH(state,row) ((state)->lineinfo[(row)].doublewidth ? ((state)->cols / 2) : (state)->cols)
#define THISROWWIDTH(state) ROWWIDTH(state, (state)->pos.row)

   // Mouse state
   int mouse_col, mouse_row;
   int mouse_buttons;
   int mouse_flags;

   // Last glyph output, for Unicode recombining purposes
   uint32_t *combine_chars;
   Unt combine_chars_size; // Number of ELEMENTS in the above
   int combine_width; // The width of the glyph above
   VTermPos combine_pos;   // Position before movement

   struct {
      unsigned int keypad:1;
      unsigned int cursor:1;
      unsigned int autowrap:1;
      unsigned int insert:1;
      unsigned int newline:1;
      unsigned int cursor_visible:1;
      unsigned int cursor_blink:1;
      unsigned int cursor_shape:2;
      unsigned int alt_screen:1;
      unsigned int origin:1;
      unsigned int screen:1;
      unsigned int leftrightmargin:1;
      unsigned int bracketpaste:1;
      unsigned int report_focus:1;
      unsigned int modify_other_keys:1;
      unsigned int kitty_keyboard:1;
   } mode;

   int gl_set, gr_set, gsingle_set;

   struct VTermPen pen;

   VTermColor default_fg;
   VTermColor default_bg;
   VTermColor colors[16]; // Store the 8 ANSI and the 8 ANSI high-brights only

   int bold_is_highbright;

   unsigned int protected_cell : 1;

   // Saved state under DEC mode 1048/1049
   struct {
      VTermPos pos;
      struct VTermPen pen;

      struct {
         unsigned int cursor_visible:1;
         unsigned int cursor_blink:1;
         unsigned int cursor_shape:2;
      } mode;
   } saved;

  // Temporary state for DECRQSS parsing
  union {
    char decrqss[4];
    struct {
      uint16_t mask;
      enum {
        SELECTION_INITIAL,
        SELECTION_SELECTED,
        SELECTION_QUERY,
        SELECTION_SET_INITIAL,
        SELECTION_SET,
        SELECTION_INVALID,
      } state : 8;
      uint32_t recvpartial;
      uint32_t sendpartial;
    } selection;
  } tmp;

  struct {
    const VTermSelectionCallbacks *callbacks;
    void *user;
    CS buffer;
    Unt buflen;
  } selection;
};

struct VTermGlyphInfo {
  const uint32_t *chars;
  int             width;
  unsigned int    protected_cell:1;  /* DECSCA-protected against DECSEL/DECSED */
  unsigned int    dwl:1;             /* DECDWL or DECDHL double-width line */
  unsigned int    dhl:2;             /* DECDHL double-height line (1=top 2=bottom) */
};



enum {
  C1_SS3 = 0x8f,
  C1_DCS = 0x90,
  C1_CSI = 0x9b,
  C1_ST  = 0x9c,
  C1_OSC = 0x9d,
};

//private void vterm_state_push_output_sprintf_CSI(VTermState *vts, const char *format, ...);

private void vterm_screen_free(VTermScreen *screen);


private int vterm_unicode_width(uint32_t codepoint);
private int vterm_unicode_is_combining(uint32_t codepoint);
private int vterm_unicode_is_ambiguous(uint32_t codepoint);
private int vterm_get_special_pty_type(void);

#if (defined(_XOPEN_SOURCE) && _XOPEN_SOURCE >= 500) \
	|| defined(_ISOC99_SOURCE) || defined(_BSD_SOURCE)
# undef VSNPRINTF
# define VSNPRINTF vsnprintf
# undef SNPRINTF
#else
# ifdef VSNPRINTF
// Use a provided vsnprintf() function.
int VSNPRINTF(char *str, Unt str_m, const char *fmt, va_list ap);
# endif
# ifdef SNPRINTF
// Use a provided snprintf() function.
int SNPRINTF(char *str, Unt str_m, const char *fmt, ...);
# endif
#endif
#ifndef SNPRINTF
# define SNPRINTF snprintf
#endif

//}}}
//{{{utf8

private unsigned int 
utf8_seqlen(long codepoint) {
  if (codepoint < 0x0000080) return 1;
  if (codepoint < 0x0000800) return 2;
  if (codepoint < 0x0010000) return 3;
  if (codepoint < 0x0200000) return 4;
  if (codepoint < 0x4000000) return 5;
  return 6;
}

// Does NOT ZERO-terminate the buffer
private int 
fill_utf8(long codepoint, char *str) {
  int nbytes = utf8_seqlen(codepoint);

  // This is easier done backwards
  int b = nbytes;
  while(b > 1) {
    b--;
    str[b] = 0x80 | (codepoint & 0x3f);
    codepoint >>= 6;
  }

   switch(nbytes) {
    case 1: str[0] =        (codepoint & 0x7f); break;
    case 2: str[0] = 0xc0 | (codepoint & 0x1f); break;
    case 3: str[0] = 0xe0 | (codepoint & 0x0f); break;
    case 4: str[0] = 0xf0 | (codepoint & 0x07); break;
    case 5: str[0] = 0xf8 | (codepoint & 0x03); break;
    case 6: str[0] = 0xfc | (codepoint & 0x01); break;
  }

  return nbytes;
}

//}}}
//{{{parsing layer


// Flag to indicate non-final subparameters in a single CSI parameter.
//Consider
//  CSI 1;2:3:4;5a
//1 4 and 5 are final.
//2 and 3 are non-final and will have this bit set
//
//Don't confuse this with the final byte of the CSI escape; 'a' in this case.
#define CSI_ARG_FLAG_MORE (1U<<31)
#define CSI_ARG_MASK      (~(1U<<31))

#define CSI_ARG_HAS_MORE(a) ((a) & CSI_ARG_FLAG_MORE)
#define CSI_ARG(a)          ((a) & CSI_ARG_MASK)

// Can't use -1 to indicate a missing argument; use this instead.
// Changed 31 to 30 to avoid an overflow warning
#define CSI_ARG_MISSING ((1<<30)-1)

#define CSI_ARG_IS_MISSING(a) (CSI_ARG(a) == CSI_ARG_MISSING)
#define CSI_ARG_OR(a,def)     (CSI_ARG(a) == CSI_ARG_MISSING ? (def) : CSI_ARG(a))
#define CSI_ARG_COUNT(a)      (CSI_ARG(a) == CSI_ARG_MISSING || CSI_ARG(a) == 0 ? 1 : CSI_ARG(a))

//}}}
//{{{forward decls

private void vterm_screen_flush_damage(VTermScreen *screen);
private int vterm_screen_get_cell(const VTermScreen *screen, VTermPos pos, VTermScreenCell *cell);
private VTermLineInfo const* vterm_state_get_lineinfo(const VTermState *state, int row);
private VTermState* vterm_obtain_state(VTerm *vt);
private void vterm_state_set_callbacks(
      VTermState *state, const VTermStateCallbacks *callbacks, void *user
);
private void vterm_state_reset(VTermState *state, int hard);
//private void * vterm_state_get_unrecognized_fbdata(VTermState *state);
private void
vterm_state_set_unrecognized_fallbacks(
      VTermState *state, const VTermStateFallbacks *fallbacks, void *user
);
private int vterm_state_set_termprop(VTermState *state, VTermProp prop, VTermValue *val);
private void *vterm_allocator_malloc(VTerm *vt, Unt size);
private void  vterm_allocator_free(VTerm *vt, void *ptr);

private void vterm_push_output_bytes(VTerm *vt, const char *bytes, Unt len);
private void vterm_push_output_vsprintf(VTerm *vt, const char *format, va_list args);
private void vterm_push_output_sprintf(VTerm *vt, const char *format, ...);
private void vterm_push_output_sprintf_ctrl(VTerm *vt, unsigned char ctrl, const char *fmt, ...);
private void vterm_push_output_sprintf_str(VTerm *vt, unsigned char ctrl, int term, const char *fmt, ...);

private void vterm_state_free(VTermState *state);

private void vterm_state_newpen(VTermState *state);
private void vterm_state_resetpen(VTermState *state);
private void vterm_state_setpen(VTermState *state, const long args[], int argcount);
private int  vterm_state_getpen(VTermState *state, long args[], int argcount);
private void vterm_state_savepen(VTermState *state, int save);

//}}}
//}}}
//{{{encoding
#define UNICODE_INVALID 0xFFFD

#if defined(DEBUG) && DEBUG > 1
# define DEBUG_PRINT_UTF8
#endif

struct UTF8DecoderData {
  // number of bytes remaining in this codepoint
  int bytes_remaining;

  // number of bytes total in this codepoint once it's finished
  // (for detecting overlongs)
  int bytes_total;

  int this_cp;
};

//private void init_utf8(void *data_) {
//  struct UTF8DecoderData *data = data_;
//  data->bytes_remaining = 0;
//  data->bytes_total     = 0;
//}

//private void decode_utf8(void *data_,
//                        uint32_t cp[], int *cpi, int cplen,
//                        const char bytes[], Unt *pos, Unt bytelen)
//{
//  struct UTF8DecoderData *data = data_;
//
//#ifdef DEBUG_PRINT_UTF8
//  printf("BEGIN UTF-8\n");
//#endif
//
//  for(; *pos < bytelen && *cpi < cplen; (*pos)++) {
//      unsigned char c = bytes[*pos];
//
//#ifdef DEBUG_PRINT_UTF8
//      printf(" pos=%zd c=%02x rem=%d\n", *pos, c, data->bytes_remaining);
//#endif
//
//      if (c < 0x20) // C0
//         return;
//      ei (c >= 0x20 && c < 0x7f) {
//         if (data->bytes_remaining) {
//            data->bytes_remaining = 0;
//            cp[(*cpi)++] = UNICODE_INVALID;
//            //avoid going over the end
//            if (*cpi >= cplen)
//               break;
//         }
//         cp[(*cpi)++] = c;
//#ifdef DEBUG_PRINT_UTF8
//         printf(" UTF-8 char: U+%04x\n", c);
//#endif
//      } ei(c == 0x7f) // DEL
//         return;
//      ei(c >= 0x80 && c < 0xc0) {
//         if (!data->bytes_remaining) {
//            cp[(*cpi)++] = UNICODE_INVALID;
//            continue;
//         }
//
//         data->this_cp <<= 6;
//         data->this_cp |= c & 0x3f;
//         data->bytes_remaining--;
//
//         if (!data->bytes_remaining) {
//#ifdef DEBUG_PRINT_UTF8
//            printf(" UTF-8 raw char U+%04x bytelen=%d ", data->this_cp, data->bytes_total);
//#endif
//            // Check for overlong sequences
//            switch(data->bytes_total) {
//            case 2:
//               if (data->this_cp <  0x0080) data->this_cp = UNICODE_INVALID;
//               break;
//            case 3:
//               if (data->this_cp <  0x0800) data->this_cp = UNICODE_INVALID;
//               break;
//            case 4:
//               if (data->this_cp < 0x10000) data->this_cp = UNICODE_INVALID;
//               break;
//            case 5:
//               if (data->this_cp < 0x200000) data->this_cp = UNICODE_INVALID;
//               break;
//            case 6:
//               if (data->this_cp < 0x4000000) data->this_cp = UNICODE_INVALID;
//               break;
//            }
//            // Now look for plain invalid ones
//            if ((data->this_cp >= 0xD800 && data->this_cp <= 0xDFFF) 
//                  || data->this_cp == 0xFFFE || data->this_cp == 0xFFFF
//            )
//               data->this_cp = UNICODE_INVALID;
//#ifdef DEBUG_PRINT_UTF8
//            printf(" char: U+%04x\n", data->this_cp);
//#endif
//            cp[(*cpi)++] = data->this_cp;
//         }
//      } ei(c >= 0xc0 && c < 0xe0) {
//         if (data->bytes_remaining)
//           cp[(*cpi)++] = UNICODE_INVALID;
//
//         data->this_cp = c & 0x1f;
//         data->bytes_total = 2;
//         data->bytes_remaining = 1;
//      } ei(c >= 0xe0 && c < 0xf0) {
//         if (data->bytes_remaining)
//           cp[(*cpi)++] = UNICODE_INVALID;
//
//         data->this_cp = c & 0x0f;
//         data->bytes_total = 3;
//         data->bytes_remaining = 2;
//      } ei(c >= 0xf0 && c < 0xf8) {
//         if (data->bytes_remaining)
//           cp[(*cpi)++] = UNICODE_INVALID;
//
//         data->this_cp = c & 0x07;
//         data->bytes_total = 4;
//         data->bytes_remaining = 3;
//      } ei(c >= 0xf8 && c < 0xfc) {
//         if (data->bytes_remaining)
//           cp[(*cpi)++] = UNICODE_INVALID;
//
//         data->this_cp = c & 0x03;
//         data->bytes_total = 5;
//         data->bytes_remaining = 4;
//      } ei(c >= 0xfc && c < 0xfe) {
//         if (data->bytes_remaining)
//           cp[(*cpi)++] = UNICODE_INVALID;
//
//         data->this_cp = c & 0x01;
//         data->bytes_total = 6;
//         data->bytes_remaining = 5;
//      } else {
//         cp[(*cpi)++] = UNICODE_INVALID;
//      }
//   }
//}

//}}}
//{{{keyboard

typedef enum {
  VTERM_MOD_NONE  = 0x00,
  VTERM_MOD_SHIFT = 0x01,
  VTERM_MOD_ALT   = 0x02,
  VTERM_MOD_CTRL  = 0x04,

  VTERM_ALL_MODS_MASK = 0x07
} VTermModifier;

// The order here must match keycodes[] in src/keyboard.c!
typedef enum {
  VTERM_KEY_NONE,

  VTERM_KEY_ENTER,
  VTERM_KEY_TAB,
  VTERM_KEY_BACKSPACE,
  VTERM_KEY_ESCAPE,

  VTERM_KEY_UP,
  VTERM_KEY_DOWN,
  VTERM_KEY_LEFT,
  VTERM_KEY_RIGHT,

  VTERM_KEY_INS,
  VTERM_KEY_DEL,
  VTERM_KEY_HOME,
  VTERM_KEY_END,
  VTERM_KEY_PAGEUP,
  VTERM_KEY_PAGEDOWN,

  // F1 is VTERM_KEY_FUNCTION(1), F2 VTERM_KEY_FUNCTION(2), etc.
  VTERM_KEY_FUNCTION_0   = 256,
  VTERM_KEY_FUNCTION_MAX = VTERM_KEY_FUNCTION_0 + 255,

  // keypad keys
  VTERM_KEY_KP_0,
  VTERM_KEY_KP_1,
  VTERM_KEY_KP_2,
  VTERM_KEY_KP_3,
  VTERM_KEY_KP_4,
  VTERM_KEY_KP_5,
  VTERM_KEY_KP_6,
  VTERM_KEY_KP_7,
  VTERM_KEY_KP_8,
  VTERM_KEY_KP_9,
  VTERM_KEY_KP_MULT,
  VTERM_KEY_KP_PLUS,
  VTERM_KEY_KP_COMMA,
  VTERM_KEY_KP_MINUS,
  VTERM_KEY_KP_PERIOD,
  VTERM_KEY_KP_DIVIDE,
  VTERM_KEY_KP_ENTER,
  VTERM_KEY_KP_EQUAL,

  VTERM_KEY_MAX, // Must be last
  VTERM_N_KEYS = VTERM_KEY_MAX
} VTermKey;

private int
vterm_is_modify_other_keys(VTerm *vt) {
  return vt->state->mode.modify_other_keys;
}

private int 
vterm_is_kitty_keyboard(VTerm *vt) {
  return vt->state->mode.kitty_keyboard;
}


private void 
vterm_keyboard_unichar(VTerm *vt, uint32_t c, VTermModifier mod) {
  if (vterm_is_modify_other_keys(vt) && mod != 0) {
    vterm_push_output_sprintf_ctrl(vt, C1_CSI, "27;%d;%d~", mod+1, c);
    return;
  }

  if (vterm_is_kitty_keyboard(vt) && mod != 0) {
    vterm_push_output_sprintf_ctrl(vt, C1_CSI, "%d;%du", c, mod+1);
    return;
  }

  //The shift modifier is never important for Unicode characters apart from Space
  if (c != ' ')
    mod &= ~VTERM_MOD_SHIFT;

  if (mod == 0) {
    // Normal text - ignore just shift
    char str[6];
    int seqlen = fill_utf8(c, str);
    vterm_push_output_bytes(vt, str, seqlen);
    return;
  }

  int needs_CSIu;
   switch(c) {
    /* Special Ctrl- letters that can't be represented elsewise */
    case 'i': case 'j': case 'm': case '[':
      needs_CSIu = 1;
      break;
    /* Ctrl-\ ] ^ _ don't need CSUu */
    case '\\': case ']': case '^': case '_':
      needs_CSIu = 0;
      break;
    // Shift-space needs CSIu
    case ' ':
      needs_CSIu = !!(mod & VTERM_MOD_SHIFT);
      break;
    // All other characters needs CSIu except for letters a-z
    default:
      needs_CSIu = (c < 'a' || c > 'z');
  }

  // ALT we can just prefix with ESC; anything else requires CSI u
  if (needs_CSIu && (mod & ~VTERM_MOD_ALT)) {
    vterm_push_output_sprintf_ctrl(vt, C1_CSI, "%d;%du", c, mod+1);
    return;
  }

  if (mod & VTERM_MOD_CTRL)
    c &= 0x1f;

  vterm_push_output_sprintf(vt, "%s%c", mod & VTERM_MOD_ALT ? ESC_S : "", c);
}

typedef struct {
  enum {
    KEYCODE_NONE,
    KEYCODE_LITERAL,
    KEYCODE_TAB,
    KEYCODE_ENTER,
    KEYCODE_SS3,
    KEYCODE_CSI,
    KEYCODE_CSI_CURSOR,
    KEYCODE_CSINUM,
    KEYCODE_KEYPAD,
  } type;
  char literal;
  int csinum;
} keycodes_s;

// Order here must be exactly the same as VTermKey enum!
static keycodes_s keycodes[] = {
  { KEYCODE_NONE,       0, 0 }, // NONE

  { KEYCODE_ENTER,      '\r', 0 }, // ENTER
  { KEYCODE_TAB,        '\t',  0 }, // TAB
  { KEYCODE_LITERAL,    '\x7f', 0 }, // BACKSPACE == ASCII DEL
  { KEYCODE_LITERAL,    '\x1b', 0 }, // ESCAPE

  { KEYCODE_CSI_CURSOR, 'A', 0 }, // UP
  { KEYCODE_CSI_CURSOR, 'B', 0 }, // DOWN
  { KEYCODE_CSI_CURSOR, 'D', 0 }, // LEFT
  { KEYCODE_CSI_CURSOR, 'C', 0 }, // RIGHT

  { KEYCODE_CSINUM, '~', 2 },  // INS
  { KEYCODE_CSINUM, '~', 3 },  // DEL
  { KEYCODE_CSI_CURSOR, 'H', 0 }, // HOME
  { KEYCODE_CSI_CURSOR, 'F', 0 }, // END
  { KEYCODE_CSINUM, '~', 5 },  // PAGEUP
  { KEYCODE_CSINUM, '~', 6 },  // PAGEDOWN
};

private keycodes_s keycodes_fn[] = {
  { KEYCODE_NONE,       0, 0 },   // F0 - shouldn't happen
  { KEYCODE_SS3,	'P', 0 }, // F1
  { KEYCODE_SS3,	'Q', 0 }, // F2
  { KEYCODE_SS3,	'R', 0 }, // F3
  { KEYCODE_SS3,	'S', 0 }, // F4
  { KEYCODE_CSINUM, '~', 15 }, // F5
  { KEYCODE_CSINUM, '~', 17 }, // F6
  { KEYCODE_CSINUM, '~', 18 }, // F7
  { KEYCODE_CSINUM, '~', 19 }, // F8
  { KEYCODE_CSINUM, '~', 20 }, // F9
  { KEYCODE_CSINUM, '~', 21 }, // F10
  { KEYCODE_CSINUM, '~', 23 }, // F11
  { KEYCODE_CSINUM, '~', 24 }, // F12
};

static keycodes_s keycodes_kp[] = {
  { KEYCODE_KEYPAD, '0', 'p' }, // KP_0
  { KEYCODE_KEYPAD, '1', 'q' }, // KP_1
  { KEYCODE_KEYPAD, '2', 'r' }, // KP_2
  { KEYCODE_KEYPAD, '3', 's' }, // KP_3
  { KEYCODE_KEYPAD, '4', 't' }, // KP_4
  { KEYCODE_KEYPAD, '5', 'u' }, // KP_5
  { KEYCODE_KEYPAD, '6', 'v' }, // KP_6
  { KEYCODE_KEYPAD, '7', 'w' }, // KP_7
  { KEYCODE_KEYPAD, '8', 'x' }, // KP_8
  { KEYCODE_KEYPAD, '9', 'y' }, // KP_9
  { KEYCODE_KEYPAD, '*', 'j' }, // KP_MULT
  { KEYCODE_KEYPAD, '+', 'k' }, // KP_PLUS
  { KEYCODE_KEYPAD, ',', 'l' }, // KP_COMMA
  { KEYCODE_KEYPAD, '-', 'm' }, // KP_MINUS
  { KEYCODE_KEYPAD, '.', 'n' }, // KP_PERIOD
  { KEYCODE_KEYPAD, '/', 'o' }, // KP_DIVIDE
  { KEYCODE_KEYPAD, '\n', 'M' }, // KP_ENTER
  { KEYCODE_KEYPAD, '=', 'X' }, // KP_EQUAL
};

private void 
vterm_keyboard_key(VTerm *vt, VTermKey key, VTermModifier mod) {
  if (key == VTERM_KEY_NONE)
    return;

  keycodes_s k;
  if (key < VTERM_KEY_FUNCTION_0) {
    if (key >= sizeof(keycodes)/sizeof(keycodes[0]))
      return;
    k = keycodes[key];
  }
  ei(key >= VTERM_KEY_FUNCTION_0 && key <= VTERM_KEY_FUNCTION_MAX) {
    if ((key - VTERM_KEY_FUNCTION_0) >= sizeof(keycodes_fn)/sizeof(keycodes_fn[0]))
      return;
    k = keycodes_fn[key - VTERM_KEY_FUNCTION_0];
  }
  ei(key >= VTERM_KEY_KP_0) {
    if ((key - VTERM_KEY_KP_0) >= sizeof(keycodes_kp)/sizeof(keycodes_kp[0]))
      return;
    k = keycodes_kp[key - VTERM_KEY_KP_0];
  }

   switch(k.type) {
   case KEYCODE_NONE:
    break;

   case KEYCODE_TAB:
    if (vterm_is_kitty_keyboard(vt) && mod != 0)
      vterm_push_output_sprintf_ctrl(vt, C1_CSI, "9;%du", mod+1);
    // Shift-Tab is CSI Z but plain Tab is 0x09
    ei(mod == VTERM_MOD_SHIFT)
      vterm_push_output_sprintf_ctrl(vt, C1_CSI, "Z");
    ei(mod & VTERM_MOD_SHIFT)
      vterm_push_output_sprintf_ctrl(vt, C1_CSI, "1;%dZ", mod+1);
    else
      goto case_LITERAL;
    break;

   case KEYCODE_ENTER:
    // Enter is CRLF in newline mode, but just LF in linefeed
    if (vt->state->mode.newline)
      vterm_push_output_sprintf(vt, "\r\n");
    else
      goto case_LITERAL;
    break;

   case KEYCODE_LITERAL: case_LITERAL:
      if (vterm_is_modify_other_keys(vt) && mod != 0)
         vterm_push_output_sprintf_ctrl(vt, C1_CSI, "27;%d;%d~", mod+1, k.literal);
      ei (vterm_is_kitty_keyboard(vt) && mod == 0 && k.literal == '\x1b')
         vterm_push_output_sprintf_ctrl(vt, C1_CSI, "%du", k.literal);
      ei ((vterm_is_kitty_keyboard(vt) && mod != 0) || (mod & (VTERM_MOD_SHIFT|VTERM_MOD_CTRL)))
         vterm_push_output_sprintf_ctrl(vt, C1_CSI, "%d;%du", k.literal, mod+1);
      else
         vterm_push_output_sprintf(vt, mod & VTERM_MOD_ALT ? ESC_S "%c" : "%c", k.literal);
      break;

   case KEYCODE_SS3: case_SS3:
    if (mod == 0)
      vterm_push_output_sprintf_ctrl(vt, C1_SS3, "%c", k.literal);
    else
      goto case_CSI;
    break;

   case KEYCODE_CSI: case_CSI:
    if (mod == 0)
      vterm_push_output_sprintf_ctrl(vt, C1_CSI, "%c", k.literal);
    else
      vterm_push_output_sprintf_ctrl(vt, C1_CSI, "1;%d%c", mod + 1, k.literal);
    break;

   case KEYCODE_CSINUM:
    if (mod == 0)
      vterm_push_output_sprintf_ctrl(vt, C1_CSI, "%d%c", k.csinum, k.literal);
    else
      vterm_push_output_sprintf_ctrl(vt, C1_CSI, "%d;%d%c", k.csinum, mod + 1, k.literal);
    break;

   case KEYCODE_CSI_CURSOR:
    if (vt->state->mode.cursor)
      goto case_SS3;
    else
      goto case_CSI;

   case KEYCODE_KEYPAD:
    if (vt->state->mode.keypad) {
      k.literal = k.csinum;
      goto case_SS3;
    }
    else
      goto case_LITERAL;
  }
}

private void 
vterm_keyboard_start_paste(VTerm *vt) {
  if (vt->state->mode.bracketpaste)
    vterm_push_output_sprintf_ctrl(vt, C1_CSI, "200~");
}

private void 
vterm_keyboard_end_paste(VTerm *vt) {
  if (vt->state->mode.bracketpaste)
    vterm_push_output_sprintf_ctrl(vt, C1_CSI, "201~");
}

//}}}
//{{{vterm

// API functions

static void *
default_malloc(Unt size, void *allocdata UNUSED) {
  void *ptr = malloc(size);
  if (ptr)
    memset(ptr, 0, size);
  return ptr;
}

static void
default_free(void *ptr, void *allocdata UNUSED) {
  free(ptr);
}

static VTermAllocatorFunctions default_allocator = {
  &default_malloc, // malloc
  &default_free // free
};

private VTerm *
vterm_build(const struct VTermBuilder *builder) {
   const VTermAllocatorFunctions *allocator = DEFAULT(builder->allocator, &default_allocator);

   // Need to bootstrap using the allocator function directly
   VTerm *vt = (*allocator->malloc)(sizeof(VTerm), builder->allocdata);

   vt->allocator = allocator;
   vt->allocdata = builder->allocdata;

   vt->rows = builder->rows;
   vt->cols = builder->cols;

   vt->parser.state = VT_NORMAL;

   vt->parser.callbacks = NULL;
   vt->parser.cbdata    = NULL;

   vt->parser.emit_nul  = FALSE;

   vt->outfunc = NULL;
   vt->outdata = NULL;

   vt->outbuffer_len = DEFAULT(builder->outbuffer_len, 4096);
   vt->outbuffer_cur = 0;
   vt->outbuffer = vterm_allocator_malloc(vt, vt->outbuffer_len);

   vt->tmpbuffer_len = DEFAULT(builder->tmpbuffer_len, 4096);
   vt->tmpbuffer = vterm_allocator_malloc(vt, vt->tmpbuffer_len);

   if (!vt->tmpbuffer || !vt->outbuffer || !vt->tmpbuffer) {
      vterm_allocator_free(vt, vt->outbuffer);
      vterm_allocator_free(vt, vt->tmpbuffer);
      vterm_allocator_free(vt, vt);
      return NULL;
   }

   return vt;
}

//private VTerm *
//vterm_new(int rows, int cols) {
//   struct VTermBuilder builder;
//   memset(&builder, 0, sizeof(builder));
//   builder.rows = rows;
//   builder.cols = cols;
//   return vterm_build(&builder);
//}

private VTerm *
vterm_new_with_allocator(int rows, int cols, VTermAllocatorFunctions *funcs, void *allocdata) {
  struct VTermBuilder builder;
  memset(&builder, 0, sizeof(builder));
  builder.rows = rows;
  builder.cols = cols;
  builder.allocator = funcs;
  builder.allocdata = allocdata;
  return vterm_build(&builder);
}


private void 
vterm_free(VTerm *vt) {
  if (vt->screen)
    vterm_screen_free(vt->screen);

  if (vt->state)
    vterm_state_free(vt->state);

  vterm_allocator_free(vt, vt->outbuffer);
  vterm_allocator_free(vt, vt->tmpbuffer);

  vterm_allocator_free(vt, vt);
}

private void *
vterm_allocator_malloc(VTerm *vt, Unt size) {
  return (*vt->allocator->malloc)(size, vt->allocdata);
}

//Free "ptr" unless it is NULL.
private void
vterm_allocator_free(VTerm *vt, void *ptr) {
  if (ptr)
    (*vt->allocator->free)(ptr, vt->allocdata);
}

private void 
vterm_get_size(const VTerm *vt, Unt *rowsp, Unt *colsp) {
   if (rowsp)
      *rowsp = vt->rows;
   if (colsp)
      *colsp = vt->cols;
}

private void 
vterm_set_size(VTerm *vt, int rows, int cols) {
  if (rows < 1 || cols < 1)
    return;

  vt->rows = rows;
  vt->cols = cols;

  if (vt->parser.callbacks && vt->parser.callbacks->resize)
    (*vt->parser.callbacks->resize)(rows, cols, vt->parser.cbdata);
}

//private void 
//vterm_output_set_callback(VTerm *vt, VTermOutputCallback *func, void *user) {
//  vt->outfunc = func;
//  vt->outdata = user;
//}

private void
vterm_push_output_bytes(VTerm *vt, const char *bytes, Unt len) {
  if (vt->outfunc) {
    (vt->outfunc)(bytes, len, vt->outdata);
    return;
  }

  if (len > vt->outbuffer_len - vt->outbuffer_cur) {
    DEBUG_LOG("vterm_push_output_bytes(): buffer overflow; dropping output\n");
    return;
  }

  memcpy(vt->outbuffer + vt->outbuffer_cur, bytes, len);
  vt->outbuffer_cur += len;
}

private void
vterm_push_output_vsprintf(VTerm *vt, const char *format, va_list args) {
   Unt len;
#ifndef VSNPRINTF
   // When vsnprintf() is not available (C90) fall back to vsprintf().
   char buffer[1024]; // 1Kbyte is enough for everybody, right?
#endif

#ifdef VSNPRINTF
   len = VSNPRINTF(vt->tmpbuffer, vt->tmpbuffer_len, format, args);
   vterm_push_output_bytes(vt, vt->tmpbuffer, len);
#else
   len = vsprintf(buffer, format, args);
   vterm_push_output_bytes(vt, buffer, len);
#endif
}

private void
vterm_push_output_sprintf(VTerm *vt, const char *format, ...) {
  va_list args;
  va_start(args, format);
  vterm_push_output_vsprintf(vt, format, args);
  va_end(args);
}

private void
vterm_push_output_sprintf_ctrl(VTerm *vt, unsigned char ctrl, const char *fmt, ...) {
  Unt cur;

  if (ctrl >= 0x80)
    cur = SNPRINTF(vt->tmpbuffer, vt->tmpbuffer_len,
        ESC_S "%c", ctrl - 0x40);
  else
    cur = SNPRINTF(vt->tmpbuffer, vt->tmpbuffer_len,
        "%c", ctrl);
  if (cur >= vt->tmpbuffer_len)
    return;
  vterm_push_output_bytes(vt, vt->tmpbuffer, cur);

  va_list args;
  va_start(args, fmt);
  vterm_push_output_vsprintf(vt, fmt, args);
  va_end(args);
}

private void 
vterm_push_output_sprintf_str(VTerm *vt, unsigned char ctrl, int term, const char *fmt, ...) {
   Unt cur;
   if (ctrl) {
      if (ctrl >= 0x80)
         cur = SNPRINTF(vt->tmpbuffer, vt->tmpbuffer_len, ESC_S "%c", ctrl - 0x40);
      else
         cur = SNPRINTF(vt->tmpbuffer, vt->tmpbuffer_len, "%c", ctrl);

      if (cur >= vt->tmpbuffer_len)
         return;
      vterm_push_output_bytes(vt, vt->tmpbuffer, cur);
   }
 
   va_list args;
   va_start(args, fmt);
   vterm_push_output_vsprintf(vt, fmt, args);
   va_end(args);

   if (term) {
      cur = SNPRINTF(vt->tmpbuffer, vt->tmpbuffer_len, ESC_S "\\"); // ST
      if (cur >= vt->tmpbuffer_len)
         return;
      vterm_push_output_bytes(vt, vt->tmpbuffer, cur);
   }
}

private Unt 
vterm_output_get_buffer_current(const VTerm *vt) {
   return vt->outbuffer_cur;
}

private Unt 
vterm_output_read(VTerm *vt, CS builder, Unt len) {
   if (len > vt->outbuffer_cur)
      len = vt->outbuffer_cur;

   memcpy(builder, vt->outbuffer, len);

   if (len < vt->outbuffer_cur)
      memmove(vt->outbuffer, vt->outbuffer + len, vt->outbuffer_cur - len);

   vt->outbuffer_cur -= len;

   return len;
}

//private VTermValueType 
//vterm_get_attr_type(VTermAttr attr) {
//   switch(attr) {
//   case VTERM_ATTR_BOLD:       return VTERM_VALUETYPE_BOOL;
//   case VTERM_ATTR_UNDERLINE:  return VTERM_VALUETYPE_INT;
//   case VTERM_ATTR_ITALIC:     return VTERM_VALUETYPE_BOOL;
//   case VTERM_ATTR_BLINK:      return VTERM_VALUETYPE_BOOL;
//   case VTERM_ATTR_REVERSE:    return VTERM_VALUETYPE_BOOL;
//   case VTERM_ATTR_CONCEAL:    return VTERM_VALUETYPE_BOOL;
//   case VTERM_ATTR_STRIKE:     return VTERM_VALUETYPE_BOOL;
//   case VTERM_ATTR_FONT:       return VTERM_VALUETYPE_INT;
//   case VTERM_ATTR_FOREGROUND: return VTERM_VALUETYPE_COLOR;
//   case VTERM_ATTR_BACKGROUND: return VTERM_VALUETYPE_COLOR;
//   case VTERM_ATTR_SMALL:      return VTERM_VALUETYPE_BOOL;
//   case VTERM_ATTR_BASELINE:   return VTERM_VALUETYPE_INT;
//
//   case VTERM_N_ATTRS: return 0;
//   }
//   return 0; /* UNREACHABLE */
//}

//private VTermValueType 
//vterm_get_prop_type(VTermProp prop) {
//   switch(prop) {
//    case VTERM_PROP_CURSORVISIBLE: return VTERM_VALUETYPE_BOOL;
//    case VTERM_PROP_CURSORBLINK:   return VTERM_VALUETYPE_BOOL;
//    case VTERM_PROP_ALTSCREEN:     return VTERM_VALUETYPE_BOOL;
//    case VTERM_PROP_TITLE:         return VTERM_VALUETYPE_STRING;
//    case VTERM_PROP_ICONNAME:      return VTERM_VALUETYPE_STRING;
//    case VTERM_PROP_REVERSE:       return VTERM_VALUETYPE_BOOL;
//    case VTERM_PROP_CURSORSHAPE:   return VTERM_VALUETYPE_INT;
//    case VTERM_PROP_MOUSE:         return VTERM_VALUETYPE_INT;
//    case VTERM_PROP_FOCUSREPORT:   return VTERM_VALUETYPE_BOOL;
//    case VTERM_PROP_CURSORCOLOR:   return VTERM_VALUETYPE_STRING;
//
//    case VTERM_N_PROPS: return 0;
//  }
//  return 0; /* UNREACHABLE */
//}

private void 
vterm_scroll_rect(VTermRect rect,
    int downward,
    int rightward,
    int (*moverect)(VTermRect src, VTermRect dest, void *user),
    int (*eraserect)(VTermRect rect, int selective, void *user),
    void *user)
{
  VTermRect src;
  VTermRect dest;

  if (abs(downward)  >= rect.end_row - rect.start_row ||
     abs(rightward) >= rect.end_col - rect.start_col) {
    /* Scroll more than area; just erase the lot */
    (*eraserect)(rect, 0, user);
    return;
  }

  if (rightward >= 0) {
    /* rect: [XXX................]
     * src:     [----------------]
     * dest: [----------------]
     */
    dest.start_col = rect.start_col;
    dest.end_col   = rect.end_col   - rightward;
    src.start_col  = rect.start_col + rightward;
    src.end_col    = rect.end_col;
  }
  else {
    /* rect: [................XXX]
     * src:  [----------------]
     * dest:    [----------------]
     */
    int leftward = -rightward;
    dest.start_col = rect.start_col + leftward;
    dest.end_col   = rect.end_col;
    src.start_col  = rect.start_col;
    src.end_col    = rect.end_col - leftward;
  }

  if (downward >= 0) {
    dest.start_row = rect.start_row;
    dest.end_row   = rect.end_row   - downward;
    src.start_row  = rect.start_row + downward;
    src.end_row    = rect.end_row;
  }
  else {
    int upward = -downward;
    dest.start_row = rect.start_row + upward;
    dest.end_row   = rect.end_row;
    src.start_row  = rect.start_row;
    src.end_row    = rect.end_row - upward;
  }

  if (moverect)
    (*moverect)(dest, src, user);

  if (downward > 0)
    rect.start_row = rect.end_row - downward;
  ei(downward < 0)
    rect.end_row = rect.start_row - downward;

  if (rightward > 0)
    rect.start_col = rect.end_col - rightward;
  ei(rightward < 0)
    rect.end_col = rect.start_col - rightward;

  (*eraserect)(rect, 0, user);
}

//private void 
//vterm_copy_cells(VTermRect dest,
//    VTermRect src,
//    void (*copycell)(VTermPos dest, VTermPos src, void *user),
//    void *user)
//{
//  int downward  = src.start_row - dest.start_row;
//  int rightward = src.start_col - dest.start_col;
//
//  int init_row, test_row, init_col, test_col;
//  int inc_row, inc_col;
//
//  if (downward < 0) {
//    init_row = dest.end_row - 1;
//    test_row = dest.start_row - 1;
//    inc_row = -1;
//  }
//  else /* downward >= 0 */ {
//    init_row = dest.start_row;
//    test_row = dest.end_row;
//    inc_row = +1;
//  }
//
//  if (rightward < 0) {
//    init_col = dest.end_col - 1;
//    test_col = dest.start_col - 1;
//    inc_col = -1;
//  } else /* rightward >= 0 */ {
//    init_col = dest.start_col;
//    test_col = dest.end_col;
//    inc_col = +1;
//  }
//
//  VTermPos pos;
//  for(pos.row = init_row; pos.row != (Unt)test_row; pos.row += inc_row)
//     for(pos.col = init_col; pos.col != (Unt)test_col; pos.col += inc_col) {
//        VTermPos srcpos;
//        srcpos.row = pos.row + downward;
//        srcpos.col = pos.col + rightward;
//        (*copycell)(pos, srcpos, user);
//     }
//}

//}}}
//{{{unicode

struct interval {
  int first;
  int last;
};

#if !defined(WCWIDTH_FUNCTION) || !defined(IS_COMBINING_FUNCTION)

// sorted list of non-overlapping intervals of non-spacing characters
// generated by "uniset +cat=Me +cat=Mn +cat=Cf -00AD +1160-11FF +200B c"
// Replaced by the combining table from Eegl.
static const struct interval combining[] = {
   {0X0300, 0X036F},
   {0X0483, 0X0489},
   {0X0591, 0X05BD},
   {0X05BF, 0X05BF},
   {0X05C1, 0X05C2},
   {0X05C4, 0X05C5},
   {0X05C7, 0X05C7},
   {0X0610, 0X061A},
   {0X064B, 0X065F},
   {0X0670, 0X0670},
   {0X06D6, 0X06DC},
   {0X06DF, 0X06E4},
   {0X06E7, 0X06E8},
   {0X06EA, 0X06ED},
   {0X0711, 0X0711},
   {0X0730, 0X074A},
   {0X07A6, 0X07B0},
   {0X07EB, 0X07F3},
   {0X07FD, 0X07FD},
   {0X0816, 0X0819},
   {0X081B, 0X0823},
   {0X0825, 0X0827},
   {0X0829, 0X082D},
   {0X0859, 0X085B},
   {0X08D3, 0X08E1},
   {0X08E3, 0X0903},
   {0X093A, 0X093C},
   {0X093E, 0X094F},
   {0X0951, 0X0957},
   {0X0962, 0X0963},
   {0X0981, 0X0983},
   {0X09BC, 0X09BC},
   {0X09BE, 0X09C4},
   {0X09C7, 0X09C8},
   {0X09CB, 0X09CD},
   {0X09D7, 0X09D7},
   {0X09E2, 0X09E3},
   {0X09FE, 0X09FE},
   {0X0A01, 0X0A03},
   {0X0A3C, 0X0A3C},
   {0X0A3E, 0X0A42},
   {0X0A47, 0X0A48},
   {0X0A4B, 0X0A4D},
   {0X0A51, 0X0A51},
   {0X0A70, 0X0A71},
   {0X0A75, 0X0A75},
   {0X0A81, 0X0A83},
   {0X0ABC, 0X0ABC},
   {0X0ABE, 0X0AC5},
   {0X0AC7, 0X0AC9},
   {0X0ACB, 0X0ACD},
   {0X0AE2, 0X0AE3},
   {0X0AFA, 0X0AFF},
   {0X0B01, 0X0B03},
   {0X0B3C, 0X0B3C},
   {0X0B3E, 0X0B44},
   {0X0B47, 0X0B48},
   {0X0B4B, 0X0B4D},
   {0X0B56, 0X0B57},
   {0X0B62, 0X0B63},
   {0X0B82, 0X0B82},
   {0X0BBE, 0X0BC2},
   {0X0BC6, 0X0BC8},
   {0X0BCA, 0X0BCD},
   {0X0BD7, 0X0BD7},
   {0X0C00, 0X0C04},
   {0X0C3E, 0X0C44},
   {0X0C46, 0X0C48},
   {0X0C4A, 0X0C4D},
   {0X0C55, 0X0C56},
   {0X0C62, 0X0C63},
   {0X0C81, 0X0C83},
   {0X0CBC, 0X0CBC},
   {0X0CBE, 0X0CC4},
   {0X0CC6, 0X0CC8},
   {0X0CCA, 0X0CCD},
   {0X0CD5, 0X0CD6},
   {0X0CE2, 0X0CE3},
   {0X0D00, 0X0D03},
   {0X0D3B, 0X0D3C},
   {0X0D3E, 0X0D44},
   {0X0D46, 0X0D48},
   {0X0D4A, 0X0D4D},
   {0X0D57, 0X0D57},
   {0X0D62, 0X0D63},
   {0X0D82, 0X0D83},
   {0X0DCA, 0X0DCA},
   {0X0DCF, 0X0DD4},
   {0X0DD6, 0X0DD6},
   {0X0DD8, 0X0DDF},
   {0X0DF2, 0X0DF3},
   {0X0E31, 0X0E31},
   {0X0E34, 0X0E3A},
   {0X0E47, 0X0E4E},
   {0X0EB1, 0X0EB1},
   {0X0EB4, 0X0EBC},
   {0X0EC8, 0X0ECD},
   {0X0F18, 0X0F19},
   {0X0F35, 0X0F35},
   {0X0F37, 0X0F37},
   {0X0F39, 0X0F39},
   {0X0F3E, 0X0F3F},
   {0X0F71, 0X0F84},
   {0X0F86, 0X0F87},
   {0X0F8D, 0X0F97},
   {0X0F99, 0X0FBC},
   {0X0FC6, 0X0FC6},
   {0X102B, 0X103E},
   {0X1056, 0X1059},
   {0X105E, 0X1060},
   {0X1062, 0X1064},
   {0X1067, 0X106D},
   {0X1071, 0X1074},
   {0X1082, 0X108D},
   {0X108F, 0X108F},
   {0X109A, 0X109D},
   {0X135D, 0X135F},
   {0X1712, 0X1714},
   {0X1732, 0X1734},
   {0X1752, 0X1753},
   {0X1772, 0X1773},
   {0X17B4, 0X17D3},
   {0X17DD, 0X17DD},
   {0X180B, 0X180D},
   {0X1885, 0X1886},
   {0X18A9, 0X18A9},
   {0X1920, 0X192B},
   {0X1930, 0X193B},
   {0X1A17, 0X1A1B},
   {0X1A55, 0X1A5E},
   {0X1A60, 0X1A7C},
   {0X1A7F, 0X1A7F},
   {0X1AB0, 0X1ABE},
   {0X1B00, 0X1B04},
   {0X1B34, 0X1B44},
   {0X1B6B, 0X1B73},
   {0X1B80, 0X1B82},
   {0X1BA1, 0X1BAD},
   {0X1BE6, 0X1BF3},
   {0X1C24, 0X1C37},
   {0X1CD0, 0X1CD2},
   {0X1CD4, 0X1CE8},
   {0X1CED, 0X1CED},
   {0X1CF4, 0X1CF4},
   {0X1CF7, 0X1CF9},
   {0X1DC0, 0X1DF9},
   {0X1DFB, 0X1DFF},
   {0X20D0, 0X20F0},
   {0X2CEF, 0X2CF1},
   {0X2D7F, 0X2D7F},
   {0X2DE0, 0X2DFF},
   {0X302A, 0X302F},
   {0X3099, 0X309A},
   {0XA66F, 0XA672},
   {0XA674, 0XA67D},
   {0XA69E, 0XA69F},
   {0XA6F0, 0XA6F1},
   {0XA802, 0XA802},
   {0XA806, 0XA806},
   {0XA80B, 0XA80B},
   {0XA823, 0XA827},
   {0XA880, 0XA881},
   {0XA8B4, 0XA8C5},
   {0XA8E0, 0XA8F1},
   {0XA8FF, 0XA8FF},
   {0XA926, 0XA92D},
   {0XA947, 0XA953},
   {0XA980, 0XA983},
   {0XA9B3, 0XA9C0},
   {0XA9E5, 0XA9E5},
   {0XAA29, 0XAA36},
   {0XAA43, 0XAA43},
   {0XAA4C, 0XAA4D},
   {0XAA7B, 0XAA7D},
   {0XAAB0, 0XAAB0},
   {0XAAB2, 0XAAB4},
   {0XAAB7, 0XAAB8},
   {0XAABE, 0XAABF},
   {0XAAC1, 0XAAC1},
   {0XAAEB, 0XAAEF},
   {0XAAF5, 0XAAF6},
   {0XABE3, 0XABEA},
   {0XABEC, 0XABED},
   {0XFB1E, 0XFB1E},
   {0XFE00, 0XFE0F},
   {0XFE20, 0XFE2F},
   {0X101FD, 0X101FD},
   {0X102E0, 0X102E0},
   {0X10376, 0X1037A},
   {0X10A01, 0X10A03},
   {0X10A05, 0X10A06},
   {0X10A0C, 0X10A0F},
   {0X10A38, 0X10A3A},
   {0X10A3F, 0X10A3F},
   {0X10AE5, 0X10AE6},
   {0X10D24, 0X10D27},
   {0X10F46, 0X10F50},
   {0X11000, 0X11002},
   {0X11038, 0X11046},
   {0X1107F, 0X11082},
   {0X110B0, 0X110BA},
   {0X11100, 0X11102},
   {0X11127, 0X11134},
   {0X11145, 0X11146},
   {0X11173, 0X11173},
   {0X11180, 0X11182},
   {0X111B3, 0X111C0},
   {0X111C9, 0X111CC},
   {0X1122C, 0X11237},
   {0X1123E, 0X1123E},
   {0X112DF, 0X112EA},
   {0X11300, 0X11303},
   {0X1133B, 0X1133C},
   {0X1133E, 0X11344},
   {0X11347, 0X11348},
   {0X1134B, 0X1134D},
   {0X11357, 0X11357},
   {0X11362, 0X11363},
   {0X11366, 0X1136C},
   {0X11370, 0X11374},
   {0X11435, 0X11446},
   {0X1145E, 0X1145E},
   {0X114B0, 0X114C3},
   {0X115AF, 0X115B5},
   {0X115B8, 0X115C0},
   {0X115DC, 0X115DD},
   {0X11630, 0X11640},
   {0X116AB, 0X116B7},
   {0X1171D, 0X1172B},
   {0X1182C, 0X1183A},
   {0X119D1, 0X119D7},
   {0X119DA, 0X119E0},
   {0X119E4, 0X119E4},
   {0X11A01, 0X11A0A},
   {0X11A33, 0X11A39},
   {0X11A3B, 0X11A3E},
   {0X11A47, 0X11A47},
   {0X11A51, 0X11A5B},
   {0X11A8A, 0X11A99},
   {0X11C2F, 0X11C36},
   {0X11C38, 0X11C3F},
   {0X11C92, 0X11CA7},
   {0X11CA9, 0X11CB6},
   {0X11D31, 0X11D36},
   {0X11D3A, 0X11D3A},
   {0X11D3C, 0X11D3D},
   {0X11D3F, 0X11D45},
   {0X11D47, 0X11D47},
   {0X11D8A, 0X11D8E},
   {0X11D90, 0X11D91},
   {0X11D93, 0X11D97},
   {0X11EF3, 0X11EF6},
   {0X16AF0, 0X16AF4},
   {0X16B30, 0X16B36},
   {0X16F4F, 0X16F4F},
   {0X16F51, 0X16F87},
   {0X16F8F, 0X16F92},
   {0X1BC9D, 0X1BC9E},
   {0X1D165, 0X1D169},
   {0X1D16D, 0X1D172},
   {0X1D17B, 0X1D182},
   {0X1D185, 0X1D18B},
   {0X1D1AA, 0X1D1AD},
   {0X1D242, 0X1D244},
   {0X1DA00, 0X1DA36},
   {0X1DA3B, 0X1DA6C},
   {0X1DA75, 0X1DA75},
   {0X1DA84, 0X1DA84},
   {0X1DA9B, 0X1DA9F},
   {0X1DAA1, 0X1DAAF},
   {0X1E000, 0X1E006},
   {0X1E008, 0X1E018},
   {0X1E01B, 0X1E021},
   {0X1E023, 0X1E024},
   {0X1E026, 0X1E02A},
   {0X1E130, 0X1E136},
   {0X1E2EC, 0X1E2EF},
   {0X1E8D0, 0X1E8D6},
   {0X1E944, 0X1E94A},
   {0XE0100, 0XE01EF}
};
#endif

// auxiliary function for binary search in interval table
private int bisearch(uint32_t ucs, const struct interval* table, int max) {
   int min = 0;
   int mid;

   if ((int)ucs < table[0].first || (int)ucs > table[max].last)
      return 0;
   while (max >= min) {
      mid = (min + max) / 2;
      if ((int)ucs > table[mid].last)
         min = mid + 1;
      ei ((int)ucs < table[mid].first)
         max = mid - 1;
      else
         return 1;
   }

   return 0;
}

//The following two functions define the column width of an ISO 10646 character as follows:
//  - The null character (U+0000) has a column width of 0.
//
//  - Other C0/C1 control characters and DEL will lead to a return value of -1.
//
//  - Non-spacing and enclosing combining characters (general
//    category code Mn or Me in the Unicode database) have a column width of 0.
//
//  - SOFT HYPHEN (U+00AD) has a column width of 1.
//
//  - Other format characters (general category code Cf in the Unicode
//    database) and ZERO WIDTH SPACE (U+200B) have a column width of 0.
//
//  - Hangul Jamo medial vowels and final consonants (U+1160-U+11FF) have a column width of 0.
//
//  - Spacing characters in the East Asian Wide (W) or East Asian
//    Full-width (F) category as defined in Unicode Technical
//    Report #11 have a column width of 2.
//
//  - All remaining characters (including all printable
//    ISO 8859-1 and WGL4 characters, Unicode control characters,
//    etc.) have a column width of 1.
//
//This implementation assumes that uint32_t characters are encoded in ISO 10646.

#ifdef WCWIDTH_FUNCTION
// use a provided wcwidth() function
int WCWIDTH_FUNCTION(uint32_t ucs);
#else
# define WCWIDTH_FUNCTION mk_wcwidth

private int 
mk_wcwidth(uint32_t ucs) {
   // test for 8-bit control characters
   if (ucs == 0)
      return 0;
   if (ucs < 32 || (ucs >= 0x7f && ucs < 0xa0))
      return -1;

   // binary search in table of non-spacing characters
   if (bisearch(ucs, combining, sizeof(combining) / sizeof(struct interval) - 1))
      return 0;

   // if we arrive here, ucs is not a combining or C0/C1 control character

   return 1 + 
    (ucs >= 0x1100 &&
     (ucs <= 0x115f                    // Hangul Jamo init. consonants
         || ucs == 0x2329 || ucs == 0x232a
         || (ucs >= 0x2e80 && ucs <= 0xa4cf && ucs != 0x303f) // CJK ... Yi
         || (ucs >= 0xac00 && ucs <= 0xd7a3) // Hangul Syllables
         || (ucs >= 0xf900 && ucs <= 0xfaff) // CJK Compatibility Ideographs
         || (ucs >= 0xfe10 && ucs <= 0xfe19) // Vertical forms
         || (ucs >= 0xfe30 && ucs <= 0xfe6f) // CJK Compatibility Forms
         || (ucs >= 0xff00 && ucs <= 0xff60) // Fullwidth Forms
         || (ucs >= 0xffe0 && ucs <= 0xffe6)
         || (ucs >= 0x20000 && ucs <= 0x2fffd)
         || (ucs >= 0x30000 && ucs <= 0x3fffd)));
}
#endif

// sorted list of non-overlapping intervals of East Asian Ambiguous
// characters, generated by "uniset +WIDTH-A -cat=Me -cat=Mn -cat=Cf c"
private const struct interval ambiguous[] = {
{ 0x00A1, 0x00A1 }, { 0x00A4, 0x00A4 }, { 0x00A7, 0x00A8 },
{ 0x00AA, 0x00AA }, { 0x00AE, 0x00AE }, { 0x00B0, 0x00B4 },
{ 0x00B6, 0x00BA }, { 0x00BC, 0x00BF }, { 0x00C6, 0x00C6 },
{ 0x00D0, 0x00D0 }, { 0x00D7, 0x00D8 }, { 0x00DE, 0x00E1 },
{ 0x00E6, 0x00E6 }, { 0x00E8, 0x00EA }, { 0x00EC, 0x00ED },
{ 0x00F0, 0x00F0 }, { 0x00F2, 0x00F3 }, { 0x00F7, 0x00FA },
{ 0x00FC, 0x00FC }, { 0x00FE, 0x00FE }, { 0x0101, 0x0101 },
{ 0x0111, 0x0111 }, { 0x0113, 0x0113 }, { 0x011B, 0x011B },
{ 0x0126, 0x0127 }, { 0x012B, 0x012B }, { 0x0131, 0x0133 },
{ 0x0138, 0x0138 }, { 0x013F, 0x0142 }, { 0x0144, 0x0144 },
{ 0x0148, 0x014B }, { 0x014D, 0x014D }, { 0x0152, 0x0153 },
{ 0x0166, 0x0167 }, { 0x016B, 0x016B }, { 0x01CE, 0x01CE },
{ 0x01D0, 0x01D0 }, { 0x01D2, 0x01D2 }, { 0x01D4, 0x01D4 },
{ 0x01D6, 0x01D6 }, { 0x01D8, 0x01D8 }, { 0x01DA, 0x01DA },
{ 0x01DC, 0x01DC }, { 0x0251, 0x0251 }, { 0x0261, 0x0261 },
{ 0x02C4, 0x02C4 }, { 0x02C7, 0x02C7 }, { 0x02C9, 0x02CB },
{ 0x02CD, 0x02CD }, { 0x02D0, 0x02D0 }, { 0x02D8, 0x02DB },
{ 0x02DD, 0x02DD }, { 0x02DF, 0x02DF }, { 0x0391, 0x03A1 },
{ 0x03A3, 0x03A9 }, { 0x03B1, 0x03C1 }, { 0x03C3, 0x03C9 },
{ 0x0401, 0x0401 }, { 0x0410, 0x044F }, { 0x0451, 0x0451 },
{ 0x2010, 0x2010 }, { 0x2013, 0x2016 }, { 0x2018, 0x2019 },
{ 0x201C, 0x201D }, { 0x2020, 0x2022 }, { 0x2024, 0x2027 },
{ 0x2030, 0x2030 }, { 0x2032, 0x2033 }, { 0x2035, 0x2035 },
{ 0x203B, 0x203B }, { 0x203E, 0x203E }, { 0x2074, 0x2074 },
{ 0x207F, 0x207F }, { 0x2081, 0x2084 }, { 0x20AC, 0x20AC },
{ 0x2103, 0x2103 }, { 0x2105, 0x2105 }, { 0x2109, 0x2109 },
{ 0x2113, 0x2113 }, { 0x2116, 0x2116 }, { 0x2121, 0x2122 },
{ 0x2126, 0x2126 }, { 0x212B, 0x212B }, { 0x2153, 0x2154 },
{ 0x215B, 0x215E }, { 0x2160, 0x216B }, { 0x2170, 0x2179 },
{ 0x2190, 0x2199 }, { 0x21B8, 0x21B9 }, { 0x21D2, 0x21D2 },
{ 0x21D4, 0x21D4 }, { 0x21E7, 0x21E7 }, { 0x2200, 0x2200 },
{ 0x2202, 0x2203 }, { 0x2207, 0x2208 }, { 0x220B, 0x220B },
{ 0x220F, 0x220F }, { 0x2211, 0x2211 }, { 0x2215, 0x2215 },
{ 0x221A, 0x221A }, { 0x221D, 0x2220 }, { 0x2223, 0x2223 },
{ 0x2225, 0x2225 }, { 0x2227, 0x222C }, { 0x222E, 0x222E },
{ 0x2234, 0x2237 }, { 0x223C, 0x223D }, { 0x2248, 0x2248 },
{ 0x224C, 0x224C }, { 0x2252, 0x2252 }, { 0x2260, 0x2261 },
{ 0x2264, 0x2267 }, { 0x226A, 0x226B }, { 0x226E, 0x226F },
{ 0x2282, 0x2283 }, { 0x2286, 0x2287 }, { 0x2295, 0x2295 },
{ 0x2299, 0x2299 }, { 0x22A5, 0x22A5 }, { 0x22BF, 0x22BF },
{ 0x2312, 0x2312 }, { 0x2460, 0x24E9 }, { 0x24EB, 0x254B },
{ 0x2550, 0x2573 }, { 0x2580, 0x258F }, { 0x2592, 0x2595 },
{ 0x25A0, 0x25A1 }, { 0x25A3, 0x25A9 }, { 0x25B2, 0x25B3 },
{ 0x25B6, 0x25B7 }, { 0x25BC, 0x25BD }, { 0x25C0, 0x25C1 },
{ 0x25C6, 0x25C8 }, { 0x25CB, 0x25CB }, { 0x25CE, 0x25D1 },
{ 0x25E2, 0x25E5 }, { 0x25EF, 0x25EF }, { 0x2605, 0x2606 },
{ 0x2609, 0x2609 }, { 0x260E, 0x260F }, { 0x2614, 0x2615 },
{ 0x261C, 0x261C }, { 0x261E, 0x261E }, { 0x2640, 0x2640 },
{ 0x2642, 0x2642 }, { 0x2660, 0x2661 }, { 0x2663, 0x2665 },
{ 0x2667, 0x266A }, { 0x266C, 0x266D }, { 0x266F, 0x266F },
{ 0x273D, 0x273D }, { 0x2776, 0x277F }, { 0xE000, 0xF8FF },
{ 0xFFFD, 0xFFFD }, { 0xF0000, 0xFFFFD }, { 0x100000, 0x10FFFD }
};

private int 
vterm_unicode_is_ambiguous(uint32_t codepoint) {
   return (bisearch(codepoint, ambiguous, sizeof(ambiguous) / sizeof(struct interval) - 1)) ? 1 : 0;
}

#ifdef IS_COMBINING_FUNCTION
// Use a provided is_combining() function.
int IS_COMBINING_FUNCTION(uint32_t codepoint);
#else
# define IS_COMBINING_FUNCTION vterm_is_combining
private int
vterm_is_combining(uint32_t codepoint) {
   return bisearch(codepoint, combining, sizeof(combining) / sizeof(struct interval) - 1);
}
#endif

#ifdef GET_SPECIAL_PTY_TYPE_FUNCTION
int GET_SPECIAL_PTY_TYPE_FUNCTION(void);
#else
# define GET_SPECIAL_PTY_TYPE_FUNCTION vterm_get_special_pty_type_placeholder
private int
vterm_get_special_pty_type_placeholder(void) {
   return 0;
}
#endif

// ################################
// ### The rest added by Paul Evans

static const struct interval fullwidth[] = {
   { 0x1100, 0x115f },
   { 0x231a, 0x231b },
   { 0x2329, 0x232a },
   { 0x23e9, 0x23ec },
   { 0x23f0, 0x23f0 },
   { 0x23f3, 0x23f3 },
   { 0x25fd, 0x25fe },
   { 0x2614, 0x2615 },
   { 0x2648, 0x2653 },
   { 0x267f, 0x267f },
   { 0x2693, 0x2693 },
   { 0x26a1, 0x26a1 },
   { 0x26aa, 0x26ab },
   { 0x26bd, 0x26be },
   { 0x26c4, 0x26c5 },
   { 0x26ce, 0x26ce },
   { 0x26d4, 0x26d4 },
   { 0x26ea, 0x26ea },
   { 0x26f2, 0x26f3 },
   { 0x26f5, 0x26f5 },
   { 0x26fa, 0x26fa },
   { 0x26fd, 0x26fd },
   { 0x2705, 0x2705 },
   { 0x270a, 0x270b },
   { 0x2728, 0x2728 },
   { 0x274c, 0x274c },
   { 0x274e, 0x274e },
   { 0x2753, 0x2755 },
   { 0x2757, 0x2757 },
   { 0x2795, 0x2797 },
   { 0x27b0, 0x27b0 },
   { 0x27bf, 0x27bf },
   { 0x2b1b, 0x2b1c },
   { 0x2b50, 0x2b50 },
   { 0x2b55, 0x2b55 },
   { 0x2e80, 0x2e99 },
   { 0x2e9b, 0x2ef3 },
   { 0x2f00, 0x2fd5 },
   { 0x2ff0, 0x2ffb },
   { 0x3000, 0x303e },
   { 0x3041, 0x3096 },
   { 0x3099, 0x30ff },
   { 0x3105, 0x312f },
   { 0x3131, 0x318e },
   { 0x3190, 0x31ba },
   { 0x31c0, 0x31e3 },
   { 0x31f0, 0x321e },
   { 0x3220, 0x3247 },
   { 0x3250, 0x4dbf },
   { 0x4e00, 0xa48c },
   { 0xa490, 0xa4c6 },
   { 0xa960, 0xa97c },
   { 0xac00, 0xd7a3 },
   { 0xf900, 0xfaff },
   { 0xfe10, 0xfe19 },
   { 0xfe30, 0xfe52 },
   { 0xfe54, 0xfe66 },
   { 0xfe68, 0xfe6b },
   { 0xff01, 0xff60 },
   { 0xffe0, 0xffe6 },
   { 0x16fe0, 0x16fe3 },
   { 0x17000, 0x187f7 },
   { 0x18800, 0x18af2 },
   { 0x1b000, 0x1b11e },
   { 0x1b150, 0x1b152 },
   { 0x1b164, 0x1b167 },
   { 0x1b170, 0x1b2fb },
   { 0x1f004, 0x1f004 },
   { 0x1f0cf, 0x1f0cf },
   { 0x1f18e, 0x1f18e },
   { 0x1f191, 0x1f19a },
   { 0x1f200, 0x1f202 },
   { 0x1f210, 0x1f23b },
   { 0x1f240, 0x1f248 },
   { 0x1f250, 0x1f251 },
   { 0x1f260, 0x1f265 },
   { 0x1f300, 0x1f320 },
   { 0x1f32d, 0x1f335 },
   { 0x1f337, 0x1f37c },
   { 0x1f37e, 0x1f393 },
   { 0x1f3a0, 0x1f3ca },
   { 0x1f3cf, 0x1f3d3 },
   { 0x1f3e0, 0x1f3f0 },
   { 0x1f3f4, 0x1f3f4 },
   { 0x1f3f8, 0x1f43e },
   { 0x1f440, 0x1f440 },
   { 0x1f442, 0x1f4fc },
   { 0x1f4ff, 0x1f53d },
   { 0x1f54b, 0x1f54e },
   { 0x1f550, 0x1f567 },
   { 0x1f57a, 0x1f57a },
   { 0x1f595, 0x1f596 },
   { 0x1f5a4, 0x1f5a4 },
   { 0x1f5fb, 0x1f64f },
   { 0x1f680, 0x1f6c5 },
   { 0x1f6cc, 0x1f6cc },
   { 0x1f6d0, 0x1f6d2 },
   { 0x1f6d5, 0x1f6d5 },
   { 0x1f6eb, 0x1f6ec },
   { 0x1f6f4, 0x1f6fa },
   { 0x1f7e0, 0x1f7eb },
   { 0x1f90d, 0x1f971 },
   { 0x1f973, 0x1f976 },
   { 0x1f97a, 0x1f9a2 },
   { 0x1f9a5, 0x1f9aa },
   { 0x1f9ae, 0x1f9ca },
   { 0x1f9cd, 0x1f9ff },
   { 0x1fa70, 0x1fa73 },
   { 0x1fa78, 0x1fa7a },
   { 0x1fa80, 0x1fa82 },
   { 0x1fa90, 0x1fa95 },
};

private int 
vterm_unicode_width(uint32_t codepoint) {
   if (bisearch(codepoint, fullwidth, sizeof(fullwidth) / sizeof(fullwidth[0]) - 1))
      return 2;

   return WCWIDTH_FUNCTION(codepoint);
}

private int 
vterm_unicode_is_combining(uint32_t codepoint) {
   return IS_COMBINING_FUNCTION(codepoint);
}

private int 
vterm_get_special_pty_type(void) {
   return GET_SPECIAL_PTY_TYPE_FUNCTION();
}

//}}}
//{{{pen

//Structure used to store RGB triples without the additional metadata stored in VTermColor.
typedef struct {
   uint8_t red, green, blue;
} VTermRGB;

static const VTermRGB ansi_colors[] = {
  /* R    G    B */
  {   0,   0,   0 }, // black
  { 224,   0,   0 }, // red
  {   0, 224,   0 }, // green
  { 224, 224,   0 }, // yellow
  {   0,   0, 224 }, // blue
  { 224,   0, 224 }, // magenta
  {   0, 224, 224 }, // cyan
  { 224, 224, 224 }, // white == light grey

  // high intensity
  { 128, 128, 128 }, // black
  { 255,  64,  64 }, // red
  {  64, 255,  64 }, // green
  { 255, 255,  64 }, // yellow
  {  64,  64, 255 }, // blue
  { 255,  64, 255 }, // magenta
  {  64, 255, 255 }, // cyan
  { 255, 255, 255 }, // white for real
};

static int ramp6[] = {
  0x00, 0x5F, 0x87, 0xAF, 0xD7, 0xFF,
};

// Use 0x81 instead of 0x80 to be able to distinguish from ansi black
static int ramp24[] = {
  0x08, 0x12, 0x1C, 0x26, 0x30, 0x3A, 0x44, 0x4E, 0x58, 0x62, 0x6C, 0x76,
  0x81, 0x8A, 0x94, 0x9E, 0xA8, 0xB2, 0xBC, 0xC6, 0xD0, 0xDA, 0xE4, 0xEE,
};

private void 
vterm_color_rgb(VTermColor *col, uint8_t red, uint8_t green, uint8_t blue) {
  col->type = VTERM_COLOR_RGB;
  col->red   = red;
  col->green = green;
  col->blue  = blue;
}

//private void 
//vterm_color_indexed(VTermColor *col, uint8_t idx) {
//  col->type = VTERM_COLOR_INDEXED;
//  col->index = idx;
//}

private int 
vterm_color_is_equal(const VTermColor *a, const VTermColor *b) {
  /* First make sure that the two colours are of the same type (RGB/Indexed) */
  if (a->type != b->type) {
    return FALSE;
  }

  /* Depending on the type inspect the corresponding members */
  if (VTERM_COLOR_IS_INDEXED(a)) {
    return a->index == b->index;
  } ei (VTERM_COLOR_IS_RGB(a)) {
    return    (a->red   == b->red)
           && (a->green == b->green)
           && (a->blue  == b->blue);
  }

  return 0;
}


static void 
lookup_default_colour_ansi(long idx, VTermColor *col) {
  //store both RGB color and index
  vterm_color_rgb(
      col,
      ansi_colors[idx].red, ansi_colors[idx].green, ansi_colors[idx].blue);
  col->index = (uint8_t)idx;
  col->type = VTERM_COLOR_INDEXED;
}

static int 
lookup_colour_ansi(const VTermState *state, long index, VTermColor *col) {
  if (index >= 0 && index < 16) {
    *col = state->colors[index];
    return TRUE;
  }

  return FALSE;
}

static int 
lookup_colour_palette(const VTermState *state, long index, VTermColor *col) {
  if (index >= 0 && index < 16) {
    // Normal 8 colours or high intensity - parse as palette 0
    return lookup_colour_ansi(state, index, col);
  }
  ei(index >= 16 && index < 232) {
    // 216-colour cube
    index -= 16;

    vterm_color_rgb(col, ramp6[index/6/6 % 6],
                         ramp6[index/6   % 6],
                         ramp6[index     % 6]);

    return TRUE;
  } ei(index >= 232 && index < 256) {
    // 24 greyscales
    index -= 232;

    vterm_color_rgb(col, ramp24[index], ramp24[index], ramp24[index]);

    return TRUE;
  }

  return FALSE;
}

static int 
lookup_colour(
      const VTermState *state, int palette, const long args[], int argcount, VTermColor *col
) {
   switch(palette) {
   case 2: // RGB mode - 3 args contain colour values directly
    if (argcount < 3)
      return argcount;

    vterm_color_rgb(col, (uint8_t)CSI_ARG(args[0]), (uint8_t)CSI_ARG(args[1]), (uint8_t)CSI_ARG(args[2]));

    return 3;

   case 5: // XTerm 256-colour mode
    if (!argcount || CSI_ARG_IS_MISSING(args[0])) {
      return argcount ? 1 : 0;
    }

    lookup_colour_palette(state, args[0], col);
    return 1;

  default:
    DEBUG_LOG1("Unrecognized colour palette %d\n", palette);
    return 0;
  }
}

// Some conveniences

private void 
penSetpenattr(VTermState *state, VTermAttr attr, VTermValueType type UNUSED, VTermValue *val) {
#ifdef DEBUG
  if (type != vterm_get_attr_type(attr)) {
    DEBUG_LOG3("Cannot set attr %d as it has type %d, not type %d\n",
        attr, vterm_get_attr_type(attr), type);
    return;
  }
#endif
  if (state->callbacks && state->callbacks->setpenattr)
    (*state->callbacks->setpenattr)(attr, val, state->cbdata);
}

private void 
setpenattr_bool(VTermState *state, VTermAttr attr, int boolean) {
  VTermValue val;
  val.boolean = boolean;
  penSetpenattr(state, attr, VTERM_VALUETYPE_BOOL, &val);
}

private void 
setpenattr_int(VTermState *state, VTermAttr attr, int number) {
  VTermValue val;
  val.number = number;
  penSetpenattr(state, attr, VTERM_VALUETYPE_INT, &val);
}

private void 
setpenattr_col(VTermState *state, VTermAttr attr, VTermColor color) {
  VTermValue val;
  val.color = color;
  penSetpenattr(state, attr, VTERM_VALUETYPE_COLOR, &val);
}

private void 
set_pen_col_ansi(VTermState *state, VTermAttr attr, long col) {
  VTermColor *colp = (attr == VTERM_ATTR_BACKGROUND) ? &state->pen.bg : &state->pen.fg;

  lookup_colour_ansi(state, col, colp);

  setpenattr_col(state, attr, *colp);
}

private void 
vterm_state_set_default_colors(
      VTermState *state, const VTermColor *default_fg, const VTermColor *default_bg
) {
  if (default_fg) {
    state->default_fg = *default_fg;
    state->default_fg.type = (state->default_fg.type & ~VTERM_COLOR_DEFAULT_MASK)
                           | VTERM_COLOR_DEFAULT_FG;
  }

  if (default_bg) {
    state->default_bg = *default_bg;
    state->default_bg.type = (state->default_bg.type & ~VTERM_COLOR_DEFAULT_MASK)
                           | VTERM_COLOR_DEFAULT_BG;
  }
}

private void 
vterm_state_newpen(VTermState *state) {
  // 90% grey so that pure white is brighter
  vterm_color_rgb(&state->default_fg, 240, 240, 240);
  vterm_color_rgb(&state->default_bg, 0, 0, 0);
  vterm_state_set_default_colors(state, &state->default_fg, &state->default_bg);

  for(int col = 0; col < 16; col++)
    lookup_default_colour_ansi(col, &state->colors[col]);
}

private void 
vterm_state_resetpen(VTermState *state) {
  state->pen.bold = 0;      setpenattr_bool(state, VTERM_ATTR_BOLD, 0);
  state->pen.underline = 0; setpenattr_int (state, VTERM_ATTR_UNDERLINE, 0);
  state->pen.italic = 0;    setpenattr_bool(state, VTERM_ATTR_ITALIC, 0);
  state->pen.blink = 0;     setpenattr_bool(state, VTERM_ATTR_BLINK, 0);
  state->pen.reverse = 0;   setpenattr_bool(state, VTERM_ATTR_REVERSE, 0);
  state->pen.conceal = 0;   setpenattr_bool(state, VTERM_ATTR_CONCEAL, 0);
  state->pen.strike = 0;    setpenattr_bool(state, VTERM_ATTR_STRIKE, 0);
  state->pen.font = 0;      setpenattr_int (state, VTERM_ATTR_FONT, 0);
  state->pen.small = 0;     setpenattr_bool(state, VTERM_ATTR_SMALL, 0);
  state->pen.baseline = 0;  setpenattr_int (state, VTERM_ATTR_BASELINE, 0);

  state->pen.fg = state->default_fg;  setpenattr_col(state, VTERM_ATTR_FOREGROUND, state->default_fg);
  state->pen.bg = state->default_bg;  setpenattr_col(state, VTERM_ATTR_BACKGROUND, state->default_bg);
}

private void 
vterm_state_savepen(VTermState *state, int save) {
  if (save) {
    state->saved.pen = state->pen;
  } else {
    state->pen = state->saved.pen;

    setpenattr_bool(state, VTERM_ATTR_BOLD,      state->pen.bold);
    setpenattr_int (state, VTERM_ATTR_UNDERLINE, state->pen.underline);
    setpenattr_bool(state, VTERM_ATTR_ITALIC,    state->pen.italic);
    setpenattr_bool(state, VTERM_ATTR_BLINK,     state->pen.blink);
    setpenattr_bool(state, VTERM_ATTR_REVERSE,   state->pen.reverse);
    setpenattr_bool(state, VTERM_ATTR_CONCEAL,   state->pen.conceal);
    setpenattr_bool(state, VTERM_ATTR_STRIKE,    state->pen.strike);
    setpenattr_int (state, VTERM_ATTR_FONT,      state->pen.font);
    setpenattr_bool(state, VTERM_ATTR_SMALL,     state->pen.small);
    setpenattr_int (state, VTERM_ATTR_BASELINE,  state->pen.baseline);

    setpenattr_col( state, VTERM_ATTR_FOREGROUND, state->pen.fg);
    setpenattr_col( state, VTERM_ATTR_BACKGROUND, state->pen.bg);
  }
}

private void 
vterm_state_get_default_colors(
      const VTermState *state, VTermColor *default_fg, VTermColor *default_bg
) {
  *default_fg = state->default_fg;
  *default_bg = state->default_bg;
}

private void 
vterm_state_get_palette_color(const VTermState *state, int index, VTermColor *col) {
  lookup_colour_palette(state, index, col);
}

private void 
vterm_state_set_palette_color(VTermState *state, int index, const VTermColor *col) {
  if (index >= 0 && index < 16)
    state->colors[index] = *col;
}

//private void 
//vterm_state_convert_color_to_rgb(const VTermState *state, VTermColor *col) {
//  if (VTERM_COLOR_IS_INDEXED(col)) { /* Convert indexed colors to RGB */
//    lookup_colour_palette(state, col->index, col);
//  }
//  col->type &= VTERM_COLOR_TYPE_MASK; /* Reset any metadata but the type */
//}

private void 
vterm_state_setpen(VTermState *state, const long args[], int argcount) {
  // SGR - ECMA-48 8.3.117

  int argi = 0;
  int value;

  while(argi < argcount) {
    // This logic is easier to do 'done' backwards; set it true, and make it
    // false again in the 'default' case
    int done = 1;

    long arg;
    switch(arg = CSI_ARG(args[argi])) {
    case CSI_ARG_MISSING:
    case 0: // Reset
      vterm_state_resetpen(state);
      break;

   case 1: { // Bold on
      const VTermColor *fg = &state->pen.fg;
      state->pen.bold = 1;
      setpenattr_bool(state, VTERM_ATTR_BOLD, 1);
      if (!VTERM_COLOR_IS_DEFAULT_FG(fg) && VTERM_COLOR_IS_INDEXED(fg) && fg->index < 8 && state->bold_is_highbright)
        set_pen_col_ansi(state, VTERM_ATTR_FOREGROUND, fg->index + (state->pen.bold ? 8 : 0));
      break;
   }

   case 3: // Italic on
      state->pen.italic = 1;
      setpenattr_bool(state, VTERM_ATTR_ITALIC, 1);
      break;

   case 4: // Underline
      state->pen.underline = VTERM_UNDERLINE_SINGLE;
      if (CSI_ARG_HAS_MORE(args[argi])) {
        argi++;
        switch(CSI_ARG(args[argi])) {
          case 0:
            state->pen.underline = 0;
            break;
          case 1:
            state->pen.underline = VTERM_UNDERLINE_SINGLE;
            break;
          case 2:
            state->pen.underline = VTERM_UNDERLINE_DOUBLE;
            break;
          case 3:
            state->pen.underline = VTERM_UNDERLINE_CURLY;
            break;
        }
      }
      setpenattr_int(state, VTERM_ATTR_UNDERLINE, state->pen.underline);
      break;

    case 5: // Blink
      state->pen.blink = 1;
      setpenattr_bool(state, VTERM_ATTR_BLINK, 1);
      break;

    case 7: // Reverse on
      state->pen.reverse = 1;
      setpenattr_bool(state, VTERM_ATTR_REVERSE, 1);
      break;

    case 8: // Conceal on
      state->pen.conceal = 1;
      setpenattr_bool(state, VTERM_ATTR_CONCEAL, 1);
      break;

    case 9: // Strikethrough on
      state->pen.strike = 1;
      setpenattr_bool(state, VTERM_ATTR_STRIKE, 1);
      break;

    case 10: case 11: case 12: case 13: case 14:
    case 15: case 16: case 17: case 18: case 19: // Select font
      state->pen.font = CSI_ARG(args[argi]) - 10;
      setpenattr_int(state, VTERM_ATTR_FONT, state->pen.font);
      break;

    case 21: // Underline double
      state->pen.underline = VTERM_UNDERLINE_DOUBLE;
      setpenattr_int(state, VTERM_ATTR_UNDERLINE, state->pen.underline);
      break;

    case 22: // Bold off
      state->pen.bold = 0;
      setpenattr_bool(state, VTERM_ATTR_BOLD, 0);
      break;

    case 23: // Italic and Gothic (currently unsupported) off
      state->pen.italic = 0;
      setpenattr_bool(state, VTERM_ATTR_ITALIC, 0);
      break;

    case 24: // Underline off
      state->pen.underline = 0;
      setpenattr_int(state, VTERM_ATTR_UNDERLINE, 0);
      break;

    case 25: // Blink off
      state->pen.blink = 0;
      setpenattr_bool(state, VTERM_ATTR_BLINK, 0);
      break;

    case 27: // Reverse off
      state->pen.reverse = 0;
      setpenattr_bool(state, VTERM_ATTR_REVERSE, 0);
      break;

    case 28: // Conceal off (Reveal)
      state->pen.conceal = 0;
      setpenattr_bool(state, VTERM_ATTR_CONCEAL, 0);
      break;

    case 29: // Strikethrough off
      state->pen.strike = 0;
      setpenattr_bool(state, VTERM_ATTR_STRIKE, 0);
      break;

    case 30: case 31: case 32: case 33:
    case 34: case 35: case 36: case 37: // Foreground colour palette
      value = CSI_ARG(args[argi]) - 30;
      if (state->pen.bold && state->bold_is_highbright)
        value += 8;
      set_pen_col_ansi(state, VTERM_ATTR_FOREGROUND, value);
      break;

    case 38: // Foreground colour alternative palette
      if (argcount - argi < 1)
        return;
      argi += 1 + lookup_colour(state, CSI_ARG(args[argi+1]), args+argi+2, argcount-argi-2, &state->pen.fg);
      setpenattr_col(state, VTERM_ATTR_FOREGROUND, state->pen.fg);
      break;

    case 39: // Foreground colour default
      state->pen.fg = state->default_fg;
      setpenattr_col(state, VTERM_ATTR_FOREGROUND, state->pen.fg);
      break;

    case 40: case 41: case 42: case 43:
    case 44: case 45: case 46: case 47: // Background colour palette
      value = CSI_ARG(args[argi]) - 40;
      set_pen_col_ansi(state, VTERM_ATTR_BACKGROUND, value);
      break;

    case 48: // Background colour alternative palette
      if (argcount - argi < 1)
        return;
      argi += 1 + lookup_colour(state, CSI_ARG(args[argi+1]), args+argi+2, argcount-argi-2, &state->pen.bg);
      setpenattr_col(state, VTERM_ATTR_BACKGROUND, state->pen.bg);
      break;

    case 49: // Default background
      state->pen.bg = state->default_bg;
      setpenattr_col(state, VTERM_ATTR_BACKGROUND, state->pen.bg);
      break;

    case 73: // Superscript
    case 74: // Subscript
    case 75: // Superscript/subscript off
      state->pen.small = (arg != 75);
      state->pen.baseline =
        (arg == 73) ? VTERM_BASELINE_RAISE :
        (arg == 74) ? VTERM_BASELINE_LOWER :
                      VTERM_BASELINE_NORMAL;
      setpenattr_bool(state, VTERM_ATTR_SMALL,    state->pen.small);
      setpenattr_int (state, VTERM_ATTR_BASELINE, state->pen.baseline);
      break;

    case 90: case 91: case 92: case 93:
    case 94: case 95: case 96: case 97: // Foreground colour high-intensity palette
      value = CSI_ARG(args[argi]) - 90 + 8;
      set_pen_col_ansi(state, VTERM_ATTR_FOREGROUND, value);
      break;

    case 100: case 101: case 102: case 103:
    case 104: case 105: case 106: case 107: // Background colour high-intensity palette
      value = CSI_ARG(args[argi]) - 100 + 8;
      set_pen_col_ansi(state, VTERM_ATTR_BACKGROUND, value);
      break;

    default:
      done = 0;
      break;
    }

    if (!done)
    {
      DEBUG_LOG1("libvterm: Unhandled CSI SGR %ld\n", arg);
    }

    while(CSI_ARG_HAS_MORE(args[argi++]))
      ;
  }
}

static int 
vterm_state_getpen_color(const VTermColor *col, int argi, long args[], int fg) {
    /* Do nothing if the given color is the default color */
    if (( fg && VTERM_COLOR_IS_DEFAULT_FG(col)) ||
        (!fg && VTERM_COLOR_IS_DEFAULT_BG(col))) {
        return argi;
    }

    /* Decide whether to send an indexed color or an RGB color */
    if (VTERM_COLOR_IS_INDEXED(col)) {
        const uint8_t idx = col->index;
        if (idx < 8) {
            args[argi++] = (idx + (fg ? 30 : 40));
        } ei (idx < 16) {
            args[argi++] = (idx - 8 + (fg ? 90 : 100));
        } else {
            args[argi++] = CSI_ARG_FLAG_MORE | (fg ? 38 : 48);
            args[argi++] = CSI_ARG_FLAG_MORE | 5;
            args[argi++] = idx;
        }
    } ei (VTERM_COLOR_IS_RGB(col)) {
        args[argi++] = CSI_ARG_FLAG_MORE | (fg ? 38 : 48);
        args[argi++] = CSI_ARG_FLAG_MORE | 2;
        args[argi++] = CSI_ARG_FLAG_MORE | col->red;
        args[argi++] = CSI_ARG_FLAG_MORE | col->green;
        args[argi++] = col->blue;
    }
    return argi;
}

private int 
vterm_state_getpen(VTermState *state, long args[], int argcount UNUSED) {
  int argi = 0;

  if (state->pen.bold)
    args[argi++] = 1;

  if (state->pen.italic)
    args[argi++] = 3;

  if (state->pen.underline == VTERM_UNDERLINE_SINGLE)
    args[argi++] = 4;
  if (state->pen.underline == VTERM_UNDERLINE_CURLY)
    args[argi++] = 4 | CSI_ARG_FLAG_MORE, args[argi++] = 3;

  if (state->pen.blink)
    args[argi++] = 5;

  if (state->pen.reverse)
    args[argi++] = 7;

  if (state->pen.conceal)
    args[argi++] = 8;

  if (state->pen.strike)
    args[argi++] = 9;

  if (state->pen.font)
    args[argi++] = 10 + state->pen.font;

  if (state->pen.underline == VTERM_UNDERLINE_DOUBLE)
    args[argi++] = 21;

  argi = vterm_state_getpen_color(&state->pen.fg, argi, args, TRUE);

  argi = vterm_state_getpen_color(&state->pen.bg, argi, args, FALSE);

  if (state->pen.small) {
    if (state->pen.baseline == VTERM_BASELINE_RAISE)
      args[argi++] = 73;
    ei(state->pen.baseline == VTERM_BASELINE_LOWER)
      args[argi++] = 74;
  }

  return argi;
}

//private int 
//vterm_state_get_penattr(const VTermState *state, VTermAttr attr, VTermValue *val) {
//   switch(attr) {
//   case VTERM_ATTR_BOLD:
//    val->boolean = state->pen.bold;
//    return 1;
//
//   case VTERM_ATTR_UNDERLINE:
//    val->number = state->pen.underline;
//    return 1;
//
//   case VTERM_ATTR_ITALIC:
//    val->boolean = state->pen.italic;
//    return 1;
//
//   case VTERM_ATTR_BLINK:
//    val->boolean = state->pen.blink;
//    return 1;
//
//   case VTERM_ATTR_REVERSE:
//    val->boolean = state->pen.reverse;
//    return 1;
//
//   case VTERM_ATTR_CONCEAL:
//    val->boolean = state->pen.conceal;
//    return 1;
//
//   case VTERM_ATTR_STRIKE:
//    val->boolean = state->pen.strike;
//    return 1;
//
//   case VTERM_ATTR_FONT:
//    val->number = state->pen.font;
//    return 1;
//
//   case VTERM_ATTR_FOREGROUND:
//    val->color = state->pen.fg;
//    return 1;
//
//   case VTERM_ATTR_BACKGROUND:
//    val->color = state->pen.bg;
//    return 1;
//
//   case VTERM_ATTR_SMALL:
//    val->boolean = state->pen.small;
//    return 1;
//
//   case VTERM_ATTR_BASELINE:
//    val->number = state->pen.baseline;
//    return 1;
//
//   case VTERM_N_ATTRS:
//    return 0;
//  }
//
//  return 0;
//}

//}}}
//{{{mouse

static void 
output_mouse(VTermState *state, int code, int pressed, int modifiers, int col, int row) {
   modifiers <<= 2;

   vterm_push_output_sprintf_ctrl(
         state->vt, C1_CSI, "<%d;%d;%d%c", code | modifiers, col + 1, row + 1, pressed ? 'M' : 'm'
   );
}

private void 
vterm_mouse_move(VTerm *vt, int row, int col, VTermModifier mod) {
  VTermState *state = vt->state;

  if (col == state->mouse_col && row == state->mouse_row)
    return;

  state->mouse_col = col;
  state->mouse_row = row;

  if ((state->mouse_flags & MOUSE_WANT_DRAG && state->mouse_buttons) ||
     (state->mouse_flags & MOUSE_WANT_MOVE)) {
    int button = state->mouse_buttons & MOUSE_BUTTON_LEFT ? 1 :
                 state->mouse_buttons & MOUSE_BUTTON_MIDDLE ? 2 :
                 state->mouse_buttons & MOUSE_BUTTON_RIGHT ? 3 : 4;
    output_mouse(state, button - 1 + 0x20, 1, mod, col, row);
  }
}

private void 
vterm_mouse_button(VTerm *vt, int button, int pressed, VTermModifier mod) {
  VTermState *state = vt->state;

  int old_buttons = state->mouse_buttons;

  if (button > 0 && button <= 3) {
    if (pressed)
      state->mouse_buttons |= (1 << (button - 1));
    else
      state->mouse_buttons &= ~(1 << (button - 1));
  }

  /* Most of the time we don't get button releases from 4/5/6/7 */
  if (state->mouse_buttons == old_buttons && button < 4)
    return;
  if (!(state->mouse_flags & MOUSE_WANT_CLICK))
    return;

  if (!state->mouse_flags)
    return;

  if (button < 4) {
    output_mouse(state, button - 1, pressed, mod, state->mouse_col, state->mouse_row);
  } ei(button < 8) {
    output_mouse(state, button-4 + 0x40, pressed, mod, state->mouse_col, state->mouse_row);
  }
}

//}}}
//{{{screen

#define UNICODE_SPACE 0x20
#define UNICODE_LINEFEED 0x0a

#undef DEBUG_REFLOW

// State of the pen at some moment in time, also used in a cell */
typedef struct {
  // After the bitfield
  VTermColor   fg, bg;

  unsigned int bold      : 1;
  unsigned int underline : 2;
  unsigned int italic    : 1;
  unsigned int blink     : 1;
  unsigned int reverse   : 1;
  unsigned int conceal   : 1;
  unsigned int strike    : 1;
  unsigned int font      : 4; /* 0 to 9 */
  unsigned int small     : 1;
  unsigned int baseline  : 2;

  /* Extra state storage that isn't strictly pen-related */
  unsigned int protected_cell : 1;
  unsigned int dwl            : 1; /* on a DECDWL or DECDHL line */
  unsigned int dhl            : 2; /* on a DECDHL line (1=top 2=bottom) */
} ScreenPen;

// Internal representation of a screen cell
typedef struct {
  Unt chars[VTERM_MAX_CHARS_PER_CELL];
  ScreenPen pen;
} ScreenCell;

struct VTermScreen {
  VTerm *vt;
  VTermState *state;

  const VTermScreenCallbacks *callbacks;
  void *cbdata;

  VTermDamageSize damage_merge;
  // start_row == -1 => no damage
  VTermRect damaged;
  VTermRect pending_scrollrect;
  int pending_scroll_downward, pending_scroll_rightward;

  Unt rows;
  Unt cols;

  unsigned int global_reverse : 1;
  unsigned int reflow : 1;

  /* Primary and Altscreen. buffers[1] is lazily allocated as needed */
  ScreenCell *buffers[2];

  /* buffer will == buffers[0] or buffers[1], depending on altscreen */
  ScreenCell *buffer;

  /* buffer for a single screen row used in scrollback storage callbacks */
  VTermScreenCell *sb_buffer;

  ScreenPen pen;
};

static void 
clearcell(const VTermScreen *screen, ScreenCell *cell) {
  cell->chars[0] = 0;
  cell->pen = screen->pen;
}

private ScreenCell *
getcell(const VTermScreen *screen, int row, int col) {
   if (row < 0 || (Unt)row >= screen->rows)
      return NULL;
   if (col < 0 || (Unt)col >= screen->cols)
      return NULL;
   if (!screen->buffer)
      return NULL;
   return screen->buffer + (screen->cols * row) + col;
}

static ScreenCell *
alloc_buffer(VTermScreen *screen, int rows, int cols) {
   ScreenCell *new_buffer = vterm_allocator_malloc(screen->vt, sizeof(ScreenCell) * rows * cols);

   for(int row = 0; row < rows; row++) {
      for(int col = 0; col < cols; col++) {
         clearcell(screen, &new_buffer[row * cols + col]);
      }
   }

   return new_buffer;
}

static void 
damagerect(VTermScreen *screen, VTermRect rect) {
   VTermRect emit;

   switch(screen->damage_merge) {
   case VTERM_DAMAGE_CELL:
      // Always emit damage event */
      emit = rect;
      break;

   case VTERM_DAMAGE_ROW:
      // Emit damage longer than one row. Try to merge with existing damage in the same row */
      if (rect.end_row > rect.start_row + 1) {
         // Bigger than 1 line - flush existing, emit this
         vterm_screen_flush_damage(screen);
         emit = rect;
      } ei(screen->damaged.start_row == UNT) {
         // None stored yet
         screen->damaged = rect;
         return;
      } ei(rect.start_row == screen->damaged.start_row) {
         // Merge with the stored line
         if (screen->damaged.start_col > rect.start_col)
            screen->damaged.start_col = rect.start_col;
         if (screen->damaged.end_col < rect.end_col)
            screen->damaged.end_col = rect.end_col;
         return;
      } else {
         // Emit the currently stored line, store a new one
         emit = screen->damaged;
         screen->damaged = rect;
      }
      break;

   case VTERM_DAMAGE_SCREEN:
   case VTERM_DAMAGE_SCROLL:
      // Never emit damage event
      if (screen->damaged.start_row == UNT)
        screen->damaged = rect;
      else {
        rect_expand(&screen->damaged, &rect);
      }
      return;

  default:
     DEBUG_LOG1("TODO: Maybe merge damage for level %d\n", screen->damage_merge);
     return;
  }

  if (screen->callbacks && screen->callbacks->damage)
     (*screen->callbacks->damage)(emit, screen->cbdata);
}

static void 
damagescreen(VTermScreen *screen) {
  VTermRect rect = {0,0,0,0};
  rect.end_row = screen->rows;
  rect.end_col = screen->cols;

  damagerect(screen, rect);
}

static int 
putglyph(VTermGlyphInfo *info, VTermPos pos, void *user) {
  VTermScreen *screen = user;
  ScreenCell *cell = getcell(screen, pos.row, pos.col);

  if (!cell)
    return 0;

  Unt i;
  for(i = 0; i < VTERM_MAX_CHARS_PER_CELL && info->chars[i]; i++) {
     cell->chars[i] = info->chars[i];
     cell->pen = screen->pen;
  }
  if (i < VTERM_MAX_CHARS_PER_CELL)
     cell->chars[i] = 0;

  for(int col = 1; col < info->width; col++) {
     ScreenCell *onecell = getcell(screen, pos.row, pos.col + col);
     if (onecell == NULL)
        break;
     onecell->chars[0] = UNT;
  }

  VTermRect rect;
  rect.start_row = pos.row;
  rect.end_row   = pos.row+1;
  rect.start_col = pos.col;
  rect.end_col   = pos.col+info->width;

  cell->pen.protected_cell = info->protected_cell;
  cell->pen.dwl            = info->dwl;
  cell->pen.dhl            = info->dhl;

  damagerect(screen, rect);

  return 1;
}

static void 
sb_pushline_from_row(VTermScreen *screen, int row) {
   VTermPos pos;
   pos.row = row;
   for(pos.col = 0; pos.col < screen->cols; pos.col++)
      vterm_screen_get_cell(screen, pos, screen->sb_buffer + pos.col);

   (screen->callbacks->sb_pushline)(screen->cols, screen->sb_buffer, screen->cbdata);
}

static int 
moverect_internal(VTermRect dest, VTermRect src, void *user) {
   VTermScreen *screen = user;

   if (screen->callbacks && screen->callbacks->sb_pushline &&
      dest.start_row == 0 && dest.start_col == 0 &&        // starts top-left corner
      dest.end_col == screen->cols &&                      // full width
      screen->buffer == screen->buffers[BUFIDX_PRIMARY]) { // not altscreen
      for(Unt row = 0; row < src.start_row; row++)
         sb_pushline_from_row(screen, row);
   }

   int cols = src.end_col - src.start_col;
   int downward = src.start_row - dest.start_row;

   int init_row, test_row, inc_row;
   if (downward < 0) {
      init_row = dest.end_row - 1;
      test_row = dest.start_row - 1;
      inc_row  = -1;
   } else {
      init_row = dest.start_row;
      test_row = dest.end_row;
      inc_row  = +1;
   }

  for(int row = init_row; row != test_row; row += inc_row)
    memmove(getcell(screen, row, dest.start_col),
            getcell(screen, row + downward, src.start_col),
            cols * sizeof(ScreenCell));

  return 1;
}

static int 
moverect_user(VTermRect dest, VTermRect src, void *user) {
  VTermScreen *screen = user;

  if (screen->callbacks && screen->callbacks->moverect) {
    if (screen->damage_merge != VTERM_DAMAGE_SCROLL)
      // Avoid an infinite loop
      vterm_screen_flush_damage(screen);

    if ((*screen->callbacks->moverect)(dest, src, screen->cbdata))
      return 1;
  }

  damagerect(screen, dest);

  return 1;
}

static int 
erase_internal(VTermRect rect, int selective, void *user) {
   VTermScreen *screen = user;

   for(Unt row = rect.start_row; row < screen->state->rows && row < rect.end_row; row++) {
      const VTermLineInfo *info = vterm_state_get_lineinfo(screen->state, row);

    for(Unt col = rect.start_col; col < rect.end_col; col++) {
      ScreenCell *cell = getcell(screen, row, col);

      if (!cell) {
         DEBUG_LOG2("libvterm: erase_internal() position invalid: %d / %d", row, col);
         return 1;
      }
      if (selective && cell->pen.protected_cell)
        continue;

      cell->chars[0] = 0;
      cell->pen = (ScreenPen){
        /* Only copy .fg and .bg; leave things like rv in reset state */
        .fg = screen->pen.fg,
        .bg = screen->pen.bg,
      };
      cell->pen.dwl = info->doublewidth;
      cell->pen.dhl = info->doubleheight;
    }
  }

  return 1;
}

static int 
erase_user(VTermRect rect, int selective UNUSED, void *user) {
  VTermScreen *screen = user;

  damagerect(screen, rect);

  return 1;
}

// Move "rect" "row_delta" down and "col_delta" right. Do not check boundaries.
private void 
vterm_rect_move(VTermRect *rect, int row_delta, int col_delta) {
  rect->start_row += row_delta; rect->end_row += row_delta;
  rect->start_col += col_delta; rect->end_col += col_delta;
}

static int 
erase(VTermRect rect, int selective, void *user) {
  erase_internal(rect, selective, user);
  return erase_user(rect, 0, user);
}

static int 
scrollrect(VTermRect rect, int downward, int rightward, void *user) {
   VTermScreen *screen = user;

   if (screen->damage_merge != VTERM_DAMAGE_SCROLL) {
      vterm_scroll_rect(rect, downward, rightward, moverect_internal, erase_internal, screen);
      vterm_screen_flush_damage(screen);
      vterm_scroll_rect(rect, downward, rightward, moverect_user, erase_user, screen);
      return 1;
   }

   if (screen->damaged.start_row != UNT && !rect_intersects(&rect, &screen->damaged)) {
      vterm_screen_flush_damage(screen);
   }

   if (screen->pending_scrollrect.start_row == UNT) {
      screen->pending_scrollrect = rect;
      screen->pending_scroll_downward  = downward;
      screen->pending_scroll_rightward = rightward;
   } ei(rect_equal(&screen->pending_scrollrect, &rect) 
         && ((screen->pending_scroll_downward  == 0 && downward  == 0) 
            || (screen->pending_scroll_rightward == 0 && rightward == 0))
   ) {
      screen->pending_scroll_downward  += downward;
      screen->pending_scroll_rightward += rightward;
   } else {
      vterm_screen_flush_damage(screen);

      screen->pending_scrollrect = rect;
      screen->pending_scroll_downward  = downward;
      screen->pending_scroll_rightward = rightward;
   }

   vterm_scroll_rect(rect, downward, rightward, moverect_internal, erase_internal, screen);

   if (screen->damaged.start_row == UNT)
      return 1;

   if (rect_contains(&rect, &screen->damaged)) {
       // Scroll region entirely contains the damage; just move it
       vterm_rect_move(&screen->damaged, -downward, -rightward);
       rect_clip(&screen->damaged, &rect);
   }
   //There are a number of possible cases here, but lets restrict this to only
   //the common case where we might actually gain some performance by
   //optimising it. Namely, a vertical scroll that neatly cuts the damage region in half.
   ei (rect.start_col <= screen->damaged.start_col 
          && rect.end_col >= screen->damaged.end_col 
          && rightward == 0
   ) {
      if (screen->damaged.start_row >= rect.start_row 
             && screen->damaged.start_row  < rect.end_row
      ) {
         screen->damaged.start_row -= downward;
         if (screen->damaged.start_row < rect.start_row)
           screen->damaged.start_row = rect.start_row;
         if (screen->damaged.start_row > rect.end_row)
           screen->damaged.start_row = rect.end_row;
         }
         if (screen->damaged.end_row >= rect.start_row && screen->damaged.end_row  < rect.end_row) {
            screen->damaged.end_row -= downward;
            if (screen->damaged.end_row < rect.start_row)
               screen->damaged.end_row = rect.start_row;
            if (screen->damaged.end_row > rect.end_row)
               screen->damaged.end_row = rect.end_row;
         }
   } else {
      DEBUG_LOG2("TODO: Just flush and redo damaged=" STRFrect " rect=" STRFrect "\n",
        ARGSrect(screen->damaged), ARGSrect(rect));
   }

   return 1;
}

static int 
movecursor(VTermPos pos, VTermPos oldpos, int visible, void *user) {
  VTermScreen *screen = user;

  if (screen->callbacks && screen->callbacks->movecursor)
    return (*screen->callbacks->movecursor)(pos, oldpos, visible, screen->cbdata);

  return 0;
}

private int 
setpenattr(VTermAttr attr, VTermValue *val, void *user) {
  VTermScreen *screen = user;

   switch(attr) {
   case VTERM_ATTR_BOLD:
    screen->pen.bold = val->boolean;
    return 1;
   case VTERM_ATTR_UNDERLINE:
    screen->pen.underline = val->number;
    return 1;
   case VTERM_ATTR_ITALIC:
    screen->pen.italic = val->boolean;
    return 1;
   case VTERM_ATTR_BLINK:
    screen->pen.blink = val->boolean;
    return 1;
   case VTERM_ATTR_REVERSE:
    screen->pen.reverse = val->boolean;
    return 1;
   case VTERM_ATTR_CONCEAL:
    screen->pen.conceal = val->boolean;
    return 1;
   case VTERM_ATTR_STRIKE:
    screen->pen.strike = val->boolean;
    return 1;
   case VTERM_ATTR_FONT:
    screen->pen.font = val->number;
    return 1;
   case VTERM_ATTR_FOREGROUND:
    screen->pen.fg = val->color;
    return 1;
   case VTERM_ATTR_BACKGROUND:
    screen->pen.bg = val->color;
    return 1;
   case VTERM_ATTR_SMALL:
    screen->pen.small = val->boolean;
    return 1;
   case VTERM_ATTR_BASELINE:
    screen->pen.baseline = val->number;
    return 1;

   case VTERM_N_ATTRS:
    return 0;
  }

  return 0;
}

static int 
settermprop(VTermProp prop, VTermValue *val, void *user) {
  VTermScreen *screen = user;

   switch(prop) {
   case VTERM_PROP_ALTSCREEN:
    if (val->boolean && !screen->buffers[BUFIDX_ALTSCREEN])
      return 0;

    screen->buffer = val->boolean ? screen->buffers[BUFIDX_ALTSCREEN] : screen->buffers[BUFIDX_PRIMARY];
    //only send a damage event on disable; because during enable there's an
    //erase that sends a damage anyway
    if (!val->boolean)
      damagescreen(screen);
    break;
   case VTERM_PROP_REVERSE:
    screen->global_reverse = val->boolean;
    damagescreen(screen);
    break;
  default:
    ; /* ignore */
  }

  if (screen->callbacks && screen->callbacks->settermprop)
    return (*screen->callbacks->settermprop)(prop, val, screen->cbdata);

  return 1;
}

static int 
bell(void *user) {
  VTermScreen *screen = user;

  if (screen->callbacks && screen->callbacks->bell)
    return (*screen->callbacks->bell)(screen->cbdata);

  return 0;
}

// How many cells are non-blank
// Returns the position of the first blank cell in the trailing blank end
static int 
line_popcount(ScreenCell *buffer, int row, int rows UNUSED, int cols) {
  int col = cols - 1;
  while(col >= 0 && buffer[row * cols + col].chars[0] == 0)
    col--;
  return col + 1;
}

#define REFLOW (screen->reflow)

static void 
resize_buffer(
    VTermScreen *screen, int bufidx, int new_rows, int new_cols, int active, 
    VTermStateFields *statefields
) {
  int old_rows = screen->rows;
  int old_cols = screen->cols;

  ScreenCell *old_buffer = screen->buffers[bufidx];
  VTermLineInfo *old_lineinfo = statefields->lineinfos[bufidx];

  ScreenCell *new_buffer = vterm_allocator_malloc(screen->vt, sizeof(ScreenCell) * new_rows * new_cols);
  VTermLineInfo *new_lineinfo = vterm_allocator_malloc(screen->vt, sizeof(new_lineinfo[0]) * new_rows);

  // Find the final row of old buffer content
  int old_row = old_rows - 1;
  int new_row = new_rows - 1;

  VTermPos old_cursor = statefields->pos;
  VTermPos new_cursor = { UNT, UNT };

#ifdef DEBUG_REFLOW
  fprintf(stderr, "Resizing from %dx%d to %dx%d; cursor was at (%d,%d)\n",
      old_cols, old_rows, new_cols, new_rows, old_cursor.col, old_cursor.row);
#endif

   //Keep track of the final row that is knonw to be blank, so we know what
   //spare space we have for scrolling into
   Unt final_blank_row = new_rows;

   while(old_row >= 0) {
      int old_row_end = old_row;
      // TODO: Stop if dwl or dhl
      while(REFLOW && old_lineinfo && old_row >= 0 && old_lineinfo[old_row].continuation)
         old_row--;
      int old_row_start = old_row;

      int width = 0;
      for(int row = old_row_start; row <= old_row_end; row++) {
        if (REFLOW && row < (old_rows - 1) && old_lineinfo[row + 1].continuation)
          width += old_cols;
        else
          width += line_popcount(old_buffer, row, old_rows, old_cols);
      }

      if (final_blank_row == (Unt)(new_row + 1) && width == 0)
         final_blank_row = new_row;

      int new_height = REFLOW
         ? width ? (width + new_cols - 1) / new_cols : 1
         : 1;

      int new_row_end = new_row;
      int new_row_start = new_row - new_height + 1;

      old_row = old_row_start;
      int old_col = 0;
  
      int spare_rows = new_rows - final_blank_row;
  
      if (new_row_start < 0 && /* we'd fall off the top */
           spare_rows >= 0 && /* we actually have spare rows */
           (!active || new_cursor.row == UNT || ((int)new_cursor.row - new_row_start) < new_rows)
      ){
         //Attempt to scroll content down into the blank rows at the bottom to make it fit
         int downwards = -new_row_start;
         if (downwards > spare_rows)
           downwards = spare_rows;
         int rowcount = new_rows - downwards;

#ifdef DEBUG_REFLOW
         fprintf(stderr, "  scroll %d rows +%d downwards\n", rowcount, downwards);
#endif

         memmove(
            &new_buffer[downwards*new_cols], &new_buffer[0], rowcount*new_cols*sizeof(ScreenCell)
         );
         memmove(
            &new_lineinfo[downwards], &new_lineinfo[0], rowcount*sizeof(new_lineinfo[0])
         );

         new_row += downwards;
         new_row_start += downwards;
         new_row_end += downwards;

         if (new_cursor.row != UNT)
            new_cursor.row += downwards;

         final_blank_row += downwards;
   }

#ifdef DEBUG_REFLOW
   fprintf(stderr, "  rows [%d..%d] <- [%d..%d] width=%d\n",
        new_row_start, new_row_end, old_row_start, old_row_end, width);
#endif

   if (new_row_start < 0) {
       if ((Unt)old_row_start <= old_cursor.row && old_cursor.row < (Unt)old_row_end) {
         new_cursor.row = 0;
         new_cursor.col = old_cursor.col;
         if (new_cursor.col >= (Unt)new_cols)
            new_cursor.col = new_cols - 1;
      }
      break;
    }

      for (new_row = new_row_start, old_row = old_row_start; new_row <= new_row_end; new_row++) {
         int count = width >= new_cols ? new_cols : width;
         width -= count;

         int new_col = 0;

         while(count) {
           //TODO: This could surely be done a lot faster by memcpy()'ing the entire range
           new_buffer[new_row * new_cols + new_col] = old_buffer[old_row * old_cols + old_col];

           if (old_cursor.row == (Unt)old_row && old_cursor.col == (Unt)old_col)
              new_cursor.row = new_row, new_cursor.col = new_col;

           old_col++;
           if (old_col == old_cols) {
              old_row++;

              if (!REFLOW) {
                 new_col++;
                 break;
              }
              old_col = 0;
           }

           new_col++;
           count--;
         }

         if (old_cursor.row == (Unt)old_row && old_cursor.col >= (Unt)old_col) {
             new_cursor.row = new_row, new_cursor.col = (old_cursor.col - old_col + new_col);
             if (new_cursor.col >= (Unt)new_cols)
                new_cursor.col = new_cols - 1;
         }

         while(new_col < new_cols) {
           clearcell(screen, &new_buffer[new_row * new_cols + new_col]);
           new_col++;
         }

         new_lineinfo[new_row].continuation = (new_row > new_row_start);
      }

      old_row = old_row_start - 1;
      new_row = new_row_start - 1;
   }

   if ((int)old_cursor.row <= old_row) {
      //cursor would have moved entirely off the top of the screen; lets just
      //bring it within range
      new_cursor.row = 0, new_cursor.col = old_cursor.col;
      if (new_cursor.col >= (Unt)new_cols)
         new_cursor.col = new_cols - 1;
   }

   // We really expect the cursor position to be set by now */
   // Unfortunately we do get here when "new_rows" is one.  We don't want
   // to crash, so until the above code is fixed let's just set the cursor. */
   if (active && (new_cursor.row == UNT || new_cursor.col == UNT)) {
      // fprintf(stderr, "screen_resize failed to update cursor position\n");
      // abort();
      if (new_cursor.row == UNT)
         new_cursor.row = 0;
      if (new_cursor.col == UNT)
         new_cursor.col = 0;
   }

   if (old_row >= 0 && bufidx == BUFIDX_PRIMARY) {
      // Push spare lines to scrollback buffer
      if (screen->callbacks && screen->callbacks->sb_pushline)
         for(int row = 0; row <= old_row; row++)
           sb_pushline_from_row(screen, row);
      if (active)
         statefields->pos.row -= (old_row + 1);
   }
   if (new_row >= 0 && bufidx == BUFIDX_PRIMARY &&
      screen->callbacks && screen->callbacks->sb_popline) {
      // Try to backfill rows by popping scrollback buffer */
      while(new_row >= 0) {
         VTermPos pos;
         if (!(screen->callbacks->sb_popline(old_cols, screen->sb_buffer, screen->cbdata)))
           break;

         pos.row = new_row;
         for(pos.col = 0; 
             pos.col < (Unt)old_cols && pos.col < (Unt)new_cols; 
             pos.col += screen->sb_buffer[pos.col].width
         ) {
           VTermScreenCell *src = &screen->sb_buffer[pos.col];
           ScreenCell *dst = &new_buffer[pos.row * new_cols + pos.col];

           for(int i = 0; i < VTERM_MAX_CHARS_PER_CELL; i++) {
             dst->chars[i] = src->chars[i];
             if (!src->chars[i])
               break;
           }

           dst->pen.bold      = src->attrs.bold;
           dst->pen.underline = src->attrs.underline;
           dst->pen.italic    = src->attrs.italic;
           dst->pen.blink     = src->attrs.blink;
           dst->pen.reverse   = src->attrs.reverse ^ screen->global_reverse;
           dst->pen.conceal   = src->attrs.conceal;
           dst->pen.strike    = src->attrs.strike;
           dst->pen.font      = src->attrs.font;
           dst->pen.small     = src->attrs.small;
           dst->pen.baseline  = src->attrs.baseline;

           dst->pen.fg = src->fg;
           dst->pen.bg = src->bg;

           if (src->width == 2 && new_cols > 1 && pos.col < (Unt)(new_cols - 1))
              (dst + 1)->chars[0] = UNT;
         }
         for( ; (int)pos.col < new_cols; pos.col++)
            clearcell(screen, &new_buffer[pos.row * new_cols + pos.col]);
         new_row--;

         if (active)
           statefields->pos.row++;
      }
   }
   if (new_row >= 0) {
      // Scroll new rows back up to the top and fill in blanks at the bottom
      int moverows = new_rows - new_row - 1;
      memmove(
         &new_buffer[0], 
         &new_buffer[(new_row + 1) * new_cols], 
         moverows * new_cols * sizeof(ScreenCell)
      );
      memmove(&new_lineinfo[0], &new_lineinfo[new_row + 1], moverows * sizeof(new_lineinfo[0]));

      new_cursor.row -= (new_row + 1);

      for(new_row = moverows; new_row < new_rows; new_row++) {
         for(int col = 0; col < new_cols; col++)
            clearcell(screen, &new_buffer[new_row * new_cols + col]);
         new_lineinfo[new_row] = (VTermLineInfo){ 0 };
      }
   }

   vterm_allocator_free(screen->vt, old_buffer);
   screen->buffers[bufidx] = new_buffer;

   vterm_allocator_free(screen->vt, old_lineinfo);
   statefields->lineinfos[bufidx] = new_lineinfo;

   if (active)
      statefields->pos = new_cursor;

   return;
}

static int 
resize(int new_rows, int new_cols, VTermStateFields *fields, void *user) {
   VTermScreen *screen = user;

   int altscreen_active = (screen->buffers[BUFIDX_ALTSCREEN] && screen->buffer == screen->buffers[BUFIDX_ALTSCREEN]);

   int old_rows = (int)screen->rows;
   int old_cols = (int)screen->cols;

   if (new_cols > old_cols) {
      // Ensure that ->sb_buffer is large enough for a new or and old row */
      if (screen->sb_buffer)
         vterm_allocator_free(screen->vt, screen->sb_buffer);

      if (new_cols > VTERM_MAX_COLS)
         new_cols = VTERM_MAX_COLS;

      screen->sb_buffer = vterm_allocator_malloc(screen->vt, sizeof(VTermScreenCell) * new_cols);
   }

  if (new_rows > VTERM_MAX_ROWS)
    new_rows = VTERM_MAX_ROWS;

   resize_buffer(screen, 0, new_rows, new_cols, !altscreen_active, fields);
   if (screen->buffers[BUFIDX_ALTSCREEN])
      resize_buffer(screen, 1, new_rows, new_cols, altscreen_active, fields);
   ei(new_rows != (int)old_rows) {
      // We don't need a full resize of the altscreen because it isn't enabled
      // but we should at least keep the lineinfo the right size */
      vterm_allocator_free(screen->vt, fields->lineinfos[BUFIDX_ALTSCREEN]);

      VTermLineInfo *new_lineinfo = vterm_allocator_malloc(screen->vt, sizeof(new_lineinfo[0]) * new_rows);
      for(int row = 0; row < new_rows; row++)
        new_lineinfo[row] = (VTermLineInfo){ 0 };

      fields->lineinfos[BUFIDX_ALTSCREEN] = new_lineinfo;
   }

   screen->buffer = altscreen_active ? screen->buffers[BUFIDX_ALTSCREEN] : screen->buffers[BUFIDX_PRIMARY];

   screen->rows = new_rows;
   screen->cols = new_cols;

   if (new_cols <= (int)old_cols) {
      if (screen->sb_buffer)
         vterm_allocator_free(screen->vt, screen->sb_buffer);

      screen->sb_buffer = vterm_allocator_malloc(screen->vt, sizeof(VTermScreenCell) * new_cols);
   }

   damagescreen(screen);

   if (screen->callbacks && screen->callbacks->resize)
      return (*screen->callbacks->resize)(new_rows, new_cols, screen->cbdata);

   return 1;
}

static int 
setlineinfo(int row, const VTermLineInfo *newinfo, const VTermLineInfo *oldinfo, void *user) {
   VTermScreen *screen = user;

   if (newinfo->doublewidth != oldinfo->doublewidth 
        || newinfo->doubleheight != oldinfo->doubleheight
   ) {
      for(Unt col = 0; col < screen->cols; col++) {
         ScreenCell *cell = getcell(screen, row, col);
         if (!cell) {
            DEBUG_LOG2("libvterm: setlineinfo() position invalid: %d / %d", row, col);
            return 1;
         }
         cell->pen.dwl = newinfo->doublewidth;
         cell->pen.dhl = newinfo->doubleheight;
      }

      VTermRect rect;
      rect.start_row = row;
      rect.end_row   = row + 1;
      rect.start_col = 0;
      rect.end_col   = newinfo->doublewidth ? screen->cols / 2 : screen->cols;
      damagerect(screen, rect);

      if (newinfo->doublewidth) {
         rect.start_col = screen->cols / 2;
         rect.end_col   = screen->cols;

         erase_internal(rect, 0, user);
      }
   }

  return 1;
}

static int 
sb_clear(void *user) {
  VTermScreen *screen = user;

  if (screen->callbacks && screen->callbacks->sb_clear)
    if ((*screen->callbacks->sb_clear)(screen->cbdata))
      return 1;

  return 0;
}

static VTermStateCallbacks state_cbs = {
  &putglyph, // putglyph
  &movecursor, // movecursor
  &scrollrect, // scrollrect
  NULL, // moverect
  &erase, // erase
  NULL, // initpen
  &setpenattr, // setpenattr
  &settermprop, // settermprop
  &bell, // bell
  &resize, // resize
  &setlineinfo, // setlineinfo
  &sb_clear, //sb_clear
};

//Allocate a new screen and return it. Return NULL when out of memory.
static VTermScreen *
screen_new(VTerm *vt) {
  VTermState *state = vterm_obtain_state(vt);
  if (!state)
    return NULL;

   VTermScreen *screen = vterm_allocator_malloc(vt, sizeof(VTermScreen));
   if (screen == NULL)
     return NULL;
   Unt rows, cols;
   vterm_get_size(vt, OUT &rows, OUT &cols);

   screen->vt = vt;
   screen->state = state;

   screen->damage_merge = VTERM_DAMAGE_CELL;
   screen->damaged.start_row = -1;
   screen->pending_scrollrect.start_row = -1;

  screen->rows = rows;
  screen->cols = cols;

  screen->global_reverse = FALSE;
  screen->reflow = FALSE;

  screen->callbacks = NULL;
  screen->cbdata    = NULL;

  screen->buffers[BUFIDX_PRIMARY] = alloc_buffer(screen, rows, cols);

  screen->buffer = screen->buffers[BUFIDX_PRIMARY];

  screen->sb_buffer = vterm_allocator_malloc(screen->vt, sizeof(VTermScreenCell) * cols);
  if (screen->buffer == NULL || screen->sb_buffer == NULL) {
    vterm_screen_free(screen);
    return NULL;
  }

  vterm_state_set_callbacks(screen->state, &state_cbs, screen);

  return screen;
}

private void 
vterm_screen_free(VTermScreen *screen) {
  vterm_allocator_free(screen->vt, screen->buffers[BUFIDX_PRIMARY]);
  if (screen->buffers[BUFIDX_ALTSCREEN])
    vterm_allocator_free(screen->vt, screen->buffers[BUFIDX_ALTSCREEN]);

  vterm_allocator_free(screen->vt, screen->sb_buffer);

  vterm_allocator_free(screen->vt, screen);
}

private void 
vterm_screen_reset(VTermScreen *screen, int hard) {
  screen->damaged.start_row = -1;
  screen->pending_scrollrect.start_row = -1;
  vterm_state_reset(screen->state, hard);
  vterm_screen_flush_damage(screen);
}

static Unt 
_get_chars(
    const VTermScreen *screen, const int utf8, void *buffer, Unt len, const VTermRect rect
) {
  Unt outpos = 0;
  int padding = 0;

#define PUT(c)                                             \
  if (utf8) {                                               \
    Unt thislen = utf8_seqlen(c);                       \
    if (buffer && outpos + thislen <= len)                  \
      outpos += fill_utf8((c), (char *)buffer + outpos);   \
    else                                                   \
      outpos += thislen;                                   \
  } else {                                                 \
    if (buffer && outpos + 1 <= len)                        \
      ((uint32_t*)buffer)[outpos++] = (c);                 \
    else                                                   \
      outpos++;                                            \
  }

   for(Unt row = rect.start_row; row < rect.end_row; row++) {
      for(Unt col = rect.start_col; col < rect.end_col; col++) {
         ScreenCell *cell = getcell(screen, row, col);

         if (!cell) {
            DEBUG_LOG2("libvterm: _get_chars() position invalid: %d / %d", row, col);
            return 1;
         }
         if (cell->chars[0] == 0)
            // Erased cell, might need a space
            padding++;
         ei(cell->chars[0] == (uint32_t)-1)
            // Gap behind a double-width char, do nothing
            ;
         else {
            while(padding) {
               PUT(UNICODE_SPACE);
               padding--;
            }
            for(int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell->chars[i]; i++) {
               PUT(cell->chars[i]);
            }
         }
      }

      if (row < rect.end_row - 1) {
         PUT(UNICODE_LINEFEED);
         padding = 0;
      }
   }

   return outpos;
}

private Unt 
vterm_screen_get_text(const VTermScreen *screen, char *str, Unt len, const VTermRect rect) {
  return _get_chars(screen, 1, str, len, rect);
}

// Copy internal to external representation of a screen cell
private int 
vterm_screen_get_cell(const VTermScreen *screen, VTermPos pos, VTermScreenCell *cell) {
  ScreenCell *intcell = getcell(screen, pos.row, pos.col);

  if (!intcell)
    return 0;

  for(int i = 0; i < VTERM_MAX_CHARS_PER_CELL; i++) {
    cell->chars[i] = intcell->chars[i];
    if (!intcell->chars[i])
      break;
  }

  cell->attrs.bold      = intcell->pen.bold;
  cell->attrs.underline = intcell->pen.underline;
  cell->attrs.italic    = intcell->pen.italic;
  cell->attrs.blink     = intcell->pen.blink;
  cell->attrs.reverse   = intcell->pen.reverse ^ screen->global_reverse;
  cell->attrs.conceal   = intcell->pen.conceal;
  cell->attrs.strike    = intcell->pen.strike;
  cell->attrs.font      = intcell->pen.font;
  cell->attrs.small     = intcell->pen.small;
  cell->attrs.baseline  = intcell->pen.baseline;

  cell->attrs.dwl = intcell->pen.dwl;
  cell->attrs.dhl = intcell->pen.dhl;

  cell->fg = intcell->pen.fg;
  cell->bg = intcell->pen.bg;

  if (vterm_get_special_pty_type() == 2) {
    // Get correct cell width from cell information contained in line buffer
    if (pos.col < (screen->cols - 1) &&
       getcell(screen, pos.row, pos.col + 1)->chars[0] == (uint32_t)-1) {
      if (getcell(screen, pos.row, pos.col)->chars[0] == 0x20) {
        getcell(screen, pos.row, pos.col)->chars[0] = 0;
        cell->width = 2;
      } ei(getcell(screen, pos.row, pos.col)->chars[0] == 0) {
        getcell(screen, pos.row, pos.col + 1)->chars[0] = 0;
        cell->width = 1;
      } else {
        cell->width = 2;
      }
    } else
      cell->width = 1;
  } else {
    if (pos.col < (screen->cols - 1) &&
       getcell(screen, pos.row, pos.col + 1)->chars[0] == (uint32_t)-1)
      cell->width = 2;
    else
      cell->width = 1;
  }

  return 1;
}

//private int 
//vterm_screen_is_eol(VTermScreen const* screen, VTermPos pos) {
//   // This cell is EOL if this and every cell to the right is black
//   for(; pos.col < screen->cols; pos.col++) {
//      ScreenCell* cell = getcell(screen, pos.row, pos.col);
//      if (cell->chars[0] != 0)
//         return 0;
//   }
//
//   return 1;
//}

private VTermScreen *
vterm_obtain_screen(VTerm *vt) {
   if (vt->screen)
      return vt->screen;

   vt->screen = screen_new(vt);
   return vt->screen;
}

private void 
vterm_screen_enable_altscreen(VTermScreen *screen, int altscreen) {
  if (!screen->buffers[BUFIDX_ALTSCREEN] && altscreen) {
    Unt rows, cols;
    vterm_get_size(screen->vt, &rows, &cols);

    screen->buffers[BUFIDX_ALTSCREEN] = alloc_buffer(screen, rows, cols);
  }
}

private void 
vterm_screen_set_callbacks(VTermScreen *screen, const VTermScreenCallbacks *callbacks, void *user) {
  screen->callbacks = callbacks;
  screen->cbdata = user;
}

private void 
vterm_screen_flush_damage(VTermScreen *screen) {
  if (screen->pending_scrollrect.start_row != UNT) {
    vterm_scroll_rect(screen->pending_scrollrect, screen->pending_scroll_downward, screen->pending_scroll_rightward,
        moverect_user, erase_user, screen);

    screen->pending_scrollrect.start_row = UNT;
  }

  if (screen->damaged.start_row != UNT) {
     if (screen->callbacks && screen->callbacks->damage)
        (*screen->callbacks->damage)(screen->damaged, screen->cbdata);

     screen->damaged.start_row = UNT;
   }
}

//private void 
//vterm_screen_set_damage_merge(VTermScreen *screen, VTermDamageSize size) {
//   vterm_screen_flush_damage(screen);
//   screen->damage_merge = size;
//}

//static int 
//attrs_differ(VTermAttrMask attrs, ScreenCell *a, ScreenCell *b) {
//   if ((attrs & VTERM_ATTR_BOLD_MASK)       && (a->pen.bold != b->pen.bold))
//      return 1;
//   if ((attrs & VTERM_ATTR_UNDERLINE_MASK)  && (a->pen.underline != b->pen.underline))
//      return 1;
//   if ((attrs & VTERM_ATTR_ITALIC_MASK)     && (a->pen.italic != b->pen.italic))
//      return 1;
//   if ((attrs & VTERM_ATTR_BLINK_MASK)      && (a->pen.blink != b->pen.blink))
//      return 1;
//   if ((attrs & VTERM_ATTR_REVERSE_MASK)    && (a->pen.reverse != b->pen.reverse))
//      return 1;
//   if ((attrs & VTERM_ATTR_CONCEAL_MASK)    && (a->pen.conceal != b->pen.conceal))
//      return 1;
//   if ((attrs & VTERM_ATTR_STRIKE_MASK)     && (a->pen.strike != b->pen.strike))
//      return 1;
//   if ((attrs & VTERM_ATTR_FONT_MASK)       && (a->pen.font != b->pen.font))
//      return 1;
//   if ((attrs & VTERM_ATTR_FOREGROUND_MASK) && !vterm_color_is_equal(&a->pen.fg, &b->pen.fg))
//      return 1;
//   if ((attrs & VTERM_ATTR_BACKGROUND_MASK) && !vterm_color_is_equal(&a->pen.bg, &b->pen.bg))
//      return 1;
//   if ((attrs & VTERM_ATTR_SMALL_MASK)    && (a->pen.small != b->pen.small))
//      return 1;
//   if ((attrs & VTERM_ATTR_BASELINE_MASK)    && (a->pen.baseline != b->pen.baseline))
//      return 1;
//
//   return 0;
//}

//private int 
//vterm_screen_get_attrs_extent(
//    const VTermScreen *screen, VTermRect *extent, VTermPos pos, VTermAttrMask attrs
//) {
//   ScreenCell *target = getcell(screen, pos.row, pos.col);
//
//   // TODO: bounds check
//   extent->start_row = pos.row;
//   extent->end_row   = pos.row + 1;
//
//   if (extent->start_col == UNT)
//      extent->start_col = 0;
//   if (extent->end_col == UNT)
//      extent->end_col = screen->cols;
//
//   Unt col;
//   for(col = pos.col - 1; col >= extent->start_col; col--) {
//      if (attrs_differ(attrs, target, getcell(screen, pos.row, col)))
//         break;
//   } 
//   extent->start_col = col + 1;
//
//   for(col = pos.col + 1; col < extent->end_col; col++) {
//      if (attrs_differ(attrs, target, getcell(screen, pos.row, col)))
//         break;
//   } 
//   extent->end_col = col - 1;
//
//   return 1;
//}

//private void 
//vterm_screen_convert_color_to_rgb(const VTermScreen *screen, VTermColor *col) {
//   vterm_state_convert_color_to_rgb(screen->state, col);
//}

//private void 
//reset_default_colours(VTermScreen *screen, ScreenCell *buffer) {
//   for (Unt row = 0; row <= screen->rows - 1; row++) {
//      for (Unt col = 0; col <= screen->cols - 1; col++) {
//         ScreenCell *cell = &buffer[row * screen->cols + col];
//         if (VTERM_COLOR_IS_DEFAULT_FG(&cell->pen.fg))
//            cell->pen.fg = screen->pen.fg;
//         if (VTERM_COLOR_IS_DEFAULT_BG(&cell->pen.bg))
//            cell->pen.bg = screen->pen.bg;
//      }
//   } 
//}

//private void 
//vterm_screen_set_default_colors(
//    VTermScreen *screen, const VTermColor* default_fg, const VTermColor *default_bg
//) {
//  vterm_state_set_default_colors(screen->state, default_fg, default_bg);
//
//  if (default_fg && VTERM_COLOR_IS_DEFAULT_FG(&screen->pen.fg)) {
//    screen->pen.fg = *default_fg;
//    screen->pen.fg.type = (screen->pen.fg.type & ~VTERM_COLOR_DEFAULT_MASK)
//                        | VTERM_COLOR_DEFAULT_FG;
//  }
//
//  if (default_bg && VTERM_COLOR_IS_DEFAULT_BG(&screen->pen.bg)) {
//    screen->pen.bg = *default_bg;
//    screen->pen.bg.type = (screen->pen.bg.type & ~VTERM_COLOR_DEFAULT_MASK)
//                        | VTERM_COLOR_DEFAULT_BG;
//  }
//
//  reset_default_colours(screen, screen->buffers[0]);
//  if (screen->buffers[1])
//    reset_default_colours(screen, screen->buffers[1]);
//}

//}}}
//{{{parser

#undef DEBUG_PARSER

static int
is_intermed(unsigned char c) {
   return c >= 0x20 && c <= 0x2f;
}

static void 
do_control(VTerm *vt, unsigned char control) {
   if (vt->parser.callbacks && vt->parser.callbacks->control)
      if ((*vt->parser.callbacks->control)(control, vt->parser.cbdata))
         return;

   DEBUG_LOG1("libvterm: Unhandled control 0x%02x\n", control);
}

private void 
do_csi(VTerm *vt, char command) {
#ifdef DEBUG_PARSER
   printf("Parsed CSI args as:\n", arglen, args);
   printf(" leader: %s\n", vt->parser.v.csi.leader);
   for (int argi = 0; argi < vt->parser.v.csi.argi; argi++) {
      printf(" %lu", CSI_ARG(vt->parser.v.csi.args[argi]));
      if (!CSI_ARG_HAS_MORE(vt->parser.v.csi.args[argi]))
         printf("\n");
     printf(" intermed: %s\n", vt->parser.intermed);
   }
#endif

   if (vt->parser.callbacks && vt->parser.callbacks->csi)
      if ((*vt->parser.callbacks->csi)(
            vt->parser.v.csi.leaderlen ? vt->parser.v.csi.leader : NULL,
            vt->parser.v.csi.args,
            vt->parser.v.csi.argi,
            vt->parser.intermedlen ? vt->parser.intermed : NULL,
            command,
            vt->parser.cbdata)
      )
         return;

  DEBUG_LOG1("libvterm: Unhandled CSI %c\n", command);
}

private void 
do_escape(VTerm* vt, Byte command) {
   Byte seq[INTERMED_MAX + 1];

   Unt len = vt->parser.intermedlen;
   strncpy((char*)seq, (char*)vt->parser.intermed, len);
   seq[len++] = command;
   seq[len]   = 0;

   if (vt->parser.callbacks && vt->parser.callbacks->escape) {
      if ((*vt->parser.callbacks->escape)(seq, len, vt->parser.cbdata))
         return;
   } 

   DEBUG_LOG1("libvterm: Unhandled escape ESC 0x%02x\n", command);
}

private void 
string_fragment(VTerm *vt, CS str, Unt len, int final) {
   VTermStringFragment frag;

   frag.str = str;
   frag.len = len;
   frag.initial = vt->parser.string_initial;
   frag.final = final;

   switch(vt->parser.state) {
   case VT_OSC:
      if (vt->parser.callbacks && vt->parser.callbacks->osc)
         (*vt->parser.callbacks->osc)(vt->parser.v.osc.command, frag, vt->parser.cbdata);
      break;

   case VT_DCS:
      if (vt->parser.callbacks && vt->parser.callbacks->dcs)
         (*vt->parser.callbacks->dcs)(
               vt->parser.v.dcs.command, vt->parser.v.dcs.commandlen, frag, vt->parser.cbdata
         );
      break;

   case VT_APC:
      if (vt->parser.callbacks && vt->parser.callbacks->apc)
         (*vt->parser.callbacks->apc)(frag, vt->parser.cbdata);
      break;

   case VT_PM:
      if (vt->parser.callbacks && vt->parser.callbacks->pm)
         (*vt->parser.callbacks->pm)(frag, vt->parser.cbdata);
      break;

   case VT_SOS:
      if (vt->parser.callbacks && vt->parser.callbacks->sos)
         (*vt->parser.callbacks->sos)(frag, vt->parser.cbdata);
      break;

   case VT_NORMAL:
   case VT_CSI_LEADER:
   case VT_CSI_ARGS:
   case VT_CSI_INTERMED:
   case VT_OSC_COMMAND:
   case VT_DCS_COMMAND:
      break;
   }

   vt->parser.string_initial = FALSE;
}

private Unt 
vterm_input_write(VTerm *vt, CS bytes, Unt len) {
   Unt pos = 0;
   CS string_start = NULL;  // init to avoid gcc warning

   vt->in_backspace = 0;		    // Count down with BS key and activate when it reaches 1

   switch(vt->parser.state) {
   case VT_NORMAL:
   case VT_CSI_LEADER:
   case VT_CSI_ARGS:
   case VT_CSI_INTERMED:
   case VT_OSC_COMMAND:
   case VT_DCS_COMMAND:
      string_start = NULL;
      break;
   case VT_OSC:
   case VT_DCS:
   case VT_APC:
   case VT_PM:
   case VT_SOS:
      string_start = bytes;
      break;
   }

#define ENTER_STATE(st)        do { vt->parser.state = st; string_start = NULL; } while(0)
#define ENTER_NORMAL_STATE()   ENTER_STATE(VT_NORMAL)

#define IS_STRING_STATE()      (vt->parser.state >= VT_OSC_COMMAND)

   for( ; pos < len; pos++) {
      Byte c = bytes[pos];
      int c1_allowed = 0;

      if (c == 0x00 || c == 0x7f) { // NUL, DEL
         if (IS_STRING_STATE()) {
            string_fragment(vt, string_start, bytes + pos - string_start, FALSE);
            string_start = bytes + pos + 1;
         }
         if (vt->parser.emit_nul)
            do_control(vt, c);
         continue;
      }
      if (c == 0x18 || c == 0x1a) { // CAN, SUB
         vt->parser.in_esc = FALSE;
         ENTER_NORMAL_STATE();
         if (vt->parser.emit_nul)
            do_control(vt, c);
         continue;
      } ei (c == 0x1b) { // ESC
         vt->parser.intermedlen = 0;
         if (!IS_STRING_STATE())
            vt->parser.state = VT_NORMAL;
         vt->parser.in_esc = TRUE;
         continue;
      } ei (c == 0x07 && IS_STRING_STATE()) {// BEL, can stand for ST in VT_OSC or VT_DCS state
         // fallthrough
      } ei (c < 0x20) { // other C0
         if (vt->parser.state == VT_SOS)
            continue; // All other C0s permitted in SOS

         if (vterm_get_special_pty_type() == 2) {
            if (c == 0x08) // BS
               // Set the trick for BS output after a sequence, to delay backspace activation
               if (pos + 2 < len && bytes[pos + 1] == 0x20 && bytes[pos + 2] == 0x08)
               vt->in_backspace = 2; // Trigger when count down to 1
         }
         if (IS_STRING_STATE())
            string_fragment(vt, string_start, bytes + pos - string_start, FALSE);
         do_control(vt, c);
         if (IS_STRING_STATE())
            string_start = bytes + pos + 1;
         continue;
      }
      // else fallthrough

      Unt string_len = bytes + pos - string_start;

      if (vt->parser.in_esc) {
         // Hoist an ESC letter into a C1 if we're not in a string mode
         // Always accept ESC \ == ST even in string mode
         if (!vt->parser.intermedlen &&
             c >= 0x40 && c < 0x60 &&
             ((!IS_STRING_STATE() || c == 0x5c))) {
           c += 0x40;
           c1_allowed = TRUE;
           if (string_len)
             string_len -= 1;
           vt->parser.in_esc = FALSE;
         } else {
            string_start = NULL;
            vt->parser.state = VT_NORMAL;
         }
      }

      switch(vt->parser.state) {
      case VT_CSI_LEADER:
         // Extract leader bytes 0x3c to 0x3f
         if (c >= 0x3c && c <= 0x3f) {
           if (vt->parser.v.csi.leaderlen < CSI_LEADER_MAX-1)
             vt->parser.v.csi.leader[vt->parser.v.csi.leaderlen++] = c;
           break;
         }

         // else fallthrough
         vt->parser.v.csi.leader[vt->parser.v.csi.leaderlen] = 0;

         vt->parser.v.csi.argi = 0;
         vt->parser.v.csi.args[0] = CSI_ARG_MISSING;
         vt->parser.state = VT_CSI_ARGS;

         // fallthrough
      case VT_CSI_ARGS:
         // Numerical value of argument
         if (c >= '0' && c <= '9') {
            if (vt->parser.v.csi.args[vt->parser.v.csi.argi] == CSI_ARG_MISSING)
               vt->parser.v.csi.args[vt->parser.v.csi.argi] = 0;
            vt->parser.v.csi.args[vt->parser.v.csi.argi] *= 10;
            vt->parser.v.csi.args[vt->parser.v.csi.argi] += c - '0';
            break;
         }
         if (c == ':') {
            vt->parser.v.csi.args[vt->parser.v.csi.argi] |= CSI_ARG_FLAG_MORE;
            c = ';';
         }
         if (c == ';') {
            vt->parser.v.csi.argi++;
            vt->parser.v.csi.args[vt->parser.v.csi.argi] = CSI_ARG_MISSING;
            break;
         }

         // else fallthrough
         vt->parser.v.csi.argi++;
         vt->parser.intermedlen = 0;
         vt->parser.state = VT_CSI_INTERMED;
         // FALLTHROUGH
      case VT_CSI_INTERMED:
         if (is_intermed(c)) {
            if (vt->parser.intermedlen < INTERMED_MAX-1)
               vt->parser.intermed[vt->parser.intermedlen++] = c;
            break;
         } ei(c == 0x1b) {
            // ESC in CSI cancels
         } ei(c >= 0x40 && c <= 0x7e) {
            vt->parser.intermed[vt->parser.intermedlen] = 0;
            do_csi(vt, c);
         }
         // else was invalid CSI

         ENTER_NORMAL_STATE();
         break;

      case VT_OSC_COMMAND:
         // Numerical value of command
         if (c >= '0' && c <= '9') {
            if (vt->parser.v.osc.command == -1)
               vt->parser.v.osc.command = 0;
            else
               vt->parser.v.osc.command *= 10;
            vt->parser.v.osc.command += c - '0';
            break;
         }
         if (c == ';') {
            vt->parser.state = VT_OSC;
            string_start = bytes + pos + 1;
            break;
         }

         // else fallthrough
         string_start = bytes + pos;
         string_len   = 0;
         vt->parser.state = VT_OSC;
         goto string_state;

      case VT_DCS_COMMAND:
         if (vt->parser.v.dcs.commandlen < CSI_LEADER_MAX)
            vt->parser.v.dcs.command[vt->parser.v.dcs.commandlen++] = c;

         if (c >= 0x40 && c<= 0x7e) {
            string_start = bytes + pos + 1;
            vt->parser.state = DCS;
         }
         break;

   string_state:
      case VT_OSC:
      case VT_DCS:
      case VT_APC:
      case VT_PM:
      case VT_SOS:
         if (c == 0x07 || (c1_allowed && c == 0x9c)) {
            string_fragment(vt, string_start, string_len, TRUE);
            ENTER_NORMAL_STATE();
         }
         break;

      case VT_NORMAL:
         if (vt->parser.in_esc) {
            if (is_intermed(c)) {
               if (vt->parser.intermedlen < INTERMED_MAX-1)
                  vt->parser.intermed[vt->parser.intermedlen++] = c;
            } ei(c >= 0x30 && c < 0x7f) {
               do_escape(vt, c);
               vt->parser.in_esc = 0;
               ENTER_NORMAL_STATE();
            } else {
               DEBUG_LOG1("TODO: Unhandled byte %02x in Escape\n", c);
            }
            break;
         }
         if (c1_allowed && c >= 0x80 && c < 0xa0) {
            switch(c) {
            case 0x90: // DCS
               vt->parser.string_initial = TRUE;
               vt->parser.v.dcs.commandlen = 0;
               ENTER_STATE(VT_DCS_COMMAND);
               break;
            case 0x98: // SOS
               vt->parser.string_initial = TRUE;
               ENTER_STATE(VT_SOS);
               string_start = bytes + pos + 1;
               string_len = 0;
               break;
            case 0x9b: // CSI
               vt->parser.v.csi.leaderlen = 0;
               ENTER_STATE(VT_CSI_LEADER);
               break;
            case 0x9d: // OSC
               vt->parser.v.osc.command = -1;
               vt->parser.string_initial = TRUE;
               string_start = bytes + pos + 1;
               ENTER_STATE(VT_OSC_COMMAND);
               break;
            case 0x9e: // PM
               vt->parser.string_initial = TRUE;
               ENTER_STATE(VT_PM);
               string_start = bytes + pos + 1;
               string_len = 0;
               break;
            case 0x9f: // APC
               vt->parser.string_initial = TRUE;
               ENTER_STATE(VT_APC);
               string_start = bytes + pos + 1;
               string_len = 0;
               break;
            default:
               do_control(vt, c);
               break;
            }
         } else {
            Unt eaten = 0;
            if (vt->parser.callbacks && vt->parser.callbacks->text)
               eaten = (*vt->parser.callbacks->text)(bytes + pos, len - pos, vt->parser.cbdata);

            if (!eaten) {
               DEBUG_LOG("libvterm: Text callback did not consume any input\n");
               // force it to make progress
               eaten = 1;
            }

            pos += (eaten - 1); // we'll ++ it again in a moment
         }
         break;
      }
   }

   if (string_start) {
      Unt string_len = bytes + pos - string_start;
      if (vt->parser.in_esc)
         string_len -= 1;
      string_fragment(vt, string_start, string_len, FALSE);
   }

   return len;
}

private void 
vterm_parser_set_callbacks(VTerm *vt, VTermParserCallbacks const *callbacks, void *user) {
   vt->parser.callbacks = callbacks;
   vt->parser.cbdata = user;
}

//}}}
//{{{state

#define strneq(a,b,n) (strncmp(a,b,n)==0)

#if defined(DEBUG) && DEBUG > 1
# define DEBUG_GLYPH_COMBINE
#endif

static Boole on_resize(Unt rows, Unt cols, void *user);

// Some convenient wrappers to make callback functions easier

private void 
statePutglyph(VTermState *state, const uint32_t chars[], int width, VTermPos pos) {
   VTermGlyphInfo info;

   info.chars = chars;
   info.width = width;
   info.protected_cell = state->protected_cell;
   info.dwl = state->lineinfo[pos.row].doublewidth;
   info.dhl = state->lineinfo[pos.row].doubleheight;

   if (state->callbacks && state->callbacks->putglyph) {
      if ((*state->callbacks->putglyph)(&info, pos, state->cbdata))
         return;
   } 

   DEBUG_LOG3("libvterm: Unhandled putglyph U+%04x at (%d,%d)\n", chars[0], pos.col, pos.row);
}

static void 
updatecursor(VTermState *state, VTermPos *oldpos, int cancel_phantom) {
   if (state->pos.col == oldpos->col && state->pos.row == oldpos->row)
      return;

   if (cancel_phantom)
      state->at_phantom = 0;

   if (state->callbacks && state->callbacks->movecursor)
      if ((*state->callbacks->movecursor)(state->pos, *oldpos, state->mode.cursor_visible, state->cbdata))
         return;
}

static void 
stateErase(VTermState *state, VTermRect rect, int selective) {
   if (rect.end_col == state->cols) {
      //If we're erasing the final cells of any lines, cancel the continuation
      //marker on the subsequent line
      for(Unt row = rect.start_row + 1; row < rect.end_row + 1 && row < state->rows; row++)
         state->lineinfo[row].continuation = 0;
   }

   if (state->callbacks && state->callbacks->erase)
     if ((*state->callbacks->erase)(rect, selective, state->cbdata))
        return;
}

static VTermState *vterm_state_new(VTerm *vt) {
  VTermState *state = vterm_allocator_malloc(vt, sizeof(VTermState));

   if (state == NULL)
      return NULL;
   state->vt = vt;

   state->rows = vt->rows;
   state->cols = vt->cols;

   state->mouse_col     = 0;
   state->mouse_row     = 0;
   state->mouse_buttons = 0;
 
   state->callbacks = NULL;
   state->cbdata    = NULL;
 
   state->selection.callbacks = NULL;
   state->selection.user      = NULL;
   state->selection.buffer    = NULL;
 
   vterm_state_newpen(state);
 
   state->bold_is_highbright = 0;
 
   state->combine_chars_size = 16;
   state->combine_chars = vterm_allocator_malloc(
         state->vt, state->combine_chars_size * sizeof(state->combine_chars[0])
   );
 
   state->tabstops = vterm_allocator_malloc(state->vt, (state->cols + 7) / 8);

   state->lineinfos[BUFIDX_PRIMARY]   = 
      vterm_allocator_malloc(state->vt, state->rows * sizeof(VTermLineInfo));
   // TODO: Make an 'enable' function
   state->lineinfos[BUFIDX_ALTSCREEN] = 
      vterm_allocator_malloc(state->vt, state->rows * sizeof(VTermLineInfo));
   state->lineinfo = state->lineinfos[BUFIDX_PRIMARY];

   return state;
}

private void 
vterm_state_free(VTermState *state) {
   vterm_allocator_free(state->vt, state->tabstops);
   vterm_allocator_free(state->vt, state->lineinfos[BUFIDX_PRIMARY]);
   if (state->lineinfos[BUFIDX_ALTSCREEN])
      vterm_allocator_free(state->vt, state->lineinfos[BUFIDX_ALTSCREEN]);
   vterm_allocator_free(state->vt, state->combine_chars);
   vterm_allocator_free(state->vt, state);
}

static void 
scroll(VTermState *state, VTermRect rect, int downward, int rightward) {
   int rows;
   int cols;
   if (!downward && !rightward)
      return;

   rows = rect.end_row - rect.start_row;
   if (downward > rows)
      downward = rows;
   ei(downward < -rows)
      downward = -rows;

   cols = rect.end_col - rect.start_col;
   if (rightward > cols)
      rightward = cols;
   ei(rightward < -cols)
      rightward = -cols;

   // Update lineinfo if full line
   if (rect.start_col == 0 && rect.end_col == state->cols && rightward == 0) {
     int height = rect.end_row - rect.start_row - abs(downward);
     Unt row;
     VTermLineInfo zeroLineInfo = {0x0};

     if (downward > 0) {
      memmove(state->lineinfo + rect.start_row,
              state->lineinfo + rect.start_row + downward,
              height * sizeof(state->lineinfo[0]));
      for(row = rect.end_row - downward; row < rect.end_row; row++)
        state->lineinfo[row] = zeroLineInfo;
    }
    else {
      memmove(state->lineinfo + rect.start_row - downward,
              state->lineinfo + rect.start_row,
              height * sizeof(state->lineinfo[0]));
      for(row = rect.start_row; row < rect.start_row - downward; row++)
         state->lineinfo[row] = zeroLineInfo;
      }
   }

   if (state->callbacks && state->callbacks->scrollrect)
      if ((*state->callbacks->scrollrect)(rect, downward, rightward, state->cbdata))
         return;

   if (state->callbacks)
      vterm_scroll_rect(
         rect, downward, rightward, state->callbacks->moverect, state->callbacks->erase, state->cbdata
      );
}

static void 
linefeed(VTermState *state) {
  if (state->pos.row == SCROLLREGION_BOTTOM(state) - 1) {
    VTermRect rect;
    rect.start_row = state->scrollregion_top;
    rect.end_row   = SCROLLREGION_BOTTOM(state);
    rect.start_col = SCROLLREGION_LEFT(state);
    rect.end_col   = SCROLLREGION_RIGHT(state);

    scroll(state, rect, 1, 0);
  } ei(state->pos.row < state->rows-1)
    state->pos.row++;
}

static void 
grow_combine_buffer(VTermState *state) {
  Unt    new_size = state->combine_chars_size * 2;
  uint32_t *new_chars = vterm_allocator_malloc(state->vt, new_size * sizeof(new_chars[0]));

  memcpy(new_chars, state->combine_chars, state->combine_chars_size * sizeof(new_chars[0]));

  vterm_allocator_free(state->vt, state->combine_chars);

  state->combine_chars = new_chars;
  state->combine_chars_size = new_size;
}

static void 
set_col_tabstop(VTermState *state, int col) {
  unsigned char mask = 1 << (col & 7);
  state->tabstops[col >> 3] |= mask;
}

static void 
clear_col_tabstop(VTermState *state, int col) {
  unsigned char mask = 1 << (col & 7);
  state->tabstops[col >> 3] &= ~mask;
}

static int
is_col_tabstop(VTermState *state, int col) {
  unsigned char mask = 1 << (col & 7);
  return state->tabstops[col >> 3] & mask;
}

static int
is_cursor_in_scrollregion(const VTermState *state) {
   if (state->pos.row < state->scrollregion_top 
        || state->pos.row >= SCROLLREGION_BOTTOM(state))
      return 0;
   if (state->pos.col < SCROLLREGION_LEFT(state) 
         || state->pos.col >= SCROLLREGION_RIGHT(state))
     return 0;

   return 1;
}

static void
tab(VTermState *state, int count, int direction) {
  while(count > 0) {
    if (direction > 0) {
      if (state->pos.col >= THISROWWIDTH(state)-1)
        return;

      state->pos.col++;
    } ei(direction < 0) {
      if (state->pos.col < 1)
        return;

      state->pos.col--;
    }

    if (is_col_tabstop(state, state->pos.col))
      count--;
  }
}

#define NO_FORCE 0
#define FORCE    1

#define DWL_OFF 0
#define DWL_ON  1

#define DHL_OFF    0
#define DHL_TOP    1
#define DHL_BOTTOM 2

static void
set_lineinfo(VTermState *state, int row, int force, int dwl, int dhl) {
  VTermLineInfo info = state->lineinfo[row];

  if (dwl == DWL_OFF)
    info.doublewidth = DWL_OFF;
  ei(dwl == DWL_ON)
    info.doublewidth = DWL_ON;
  // else -1 to ignore

  if (dhl == DHL_OFF)
    info.doubleheight = DHL_OFF;
  ei(dhl == DHL_TOP)
    info.doubleheight = DHL_TOP;
  ei(dhl == DHL_BOTTOM)
    info.doubleheight = DHL_BOTTOM;

  if ((state->callbacks &&
      state->callbacks->setlineinfo &&
      (*state->callbacks->setlineinfo)(row, &info, state->lineinfo + row, state->cbdata))
      || force)
    state->lineinfo[row] = info;
}

static int
on_text(CS bytes UNUSED, Unt len UNUSED, void *user) {
   VTermState *state = user;
   int npoints = 0;
   Unt eaten = 0;
   int i = 0;

   VTermPos oldpos = state->pos;

   // We'll have at most len codepoints, plus one from a previous incomplete sequence.
   uint32_t *codepoints = (uint32_t *)(state->vt->tmpbuffer);

   //There's a chance an encoding (e.g. UTF-8) hasn't found enough bytes yet
   //for even a single codepoint
   if (!npoints) {
      return (int)eaten;
   }

   if (state->gsingle_set && npoints)
      state->gsingle_set = 0;

   //This is a combining char. that needs to be merged with the previous glyph output
   if (vterm_unicode_is_combining(codepoints[i])) {
      //See if the cursor has moved since
      if (state->pos.row == state->combine_pos.row 
          && state->pos.col == state->combine_pos.col + state->combine_width
      ) {
#ifdef DEBUG_GLYPH_COMBINE
      int printpos;
      printf("DEBUG: COMBINING SPLIT GLYPH of chars {");
      for(printpos = 0; state->combine_chars[printpos]; printpos++)
        printf("U+%04x ", state->combine_chars[printpos]);
      printf("} + {");
#endif

      // Find where we need to append these combining chars
      int saved_i = 0;
      while(state->combine_chars[saved_i])
        saved_i++;

      // Add extra ones
      while(i < npoints && vterm_unicode_is_combining(codepoints[i])) {
         if (saved_i >= (int)state->combine_chars_size)
            grow_combine_buffer(state);
         state->combine_chars[saved_i++] = codepoints[i++];
      }
      if (saved_i >= (int)state->combine_chars_size)
         grow_combine_buffer(state);
      state->combine_chars[saved_i] = 0;

#ifdef DEBUG_GLYPH_COMBINE
      for(; state->combine_chars[printpos]; printpos++)
        printf("U+%04x ", state->combine_chars[printpos]);
      printf("}\n");
#endif

      // Now render it
      statePutglyph(state, state->combine_chars, state->combine_width, state->combine_pos);
    }
    else {
      DEBUG_LOG("libvterm: TODO: Skip over split char+combining\n");
    }
   }

   for(; i < npoints; i++) {
      // Try to find combining characters following this
      int glyph_starts = i;
      int glyph_ends;
      int width = 0;

      for(glyph_ends = i + 1;
          (glyph_ends < npoints) && (glyph_ends < glyph_starts + VTERM_MAX_CHARS_PER_CELL);
          glyph_ends++) {
         if (!vterm_unicode_is_combining(codepoints[glyph_ends]))
            break;
      } 

      uint32_t *chars = vterm_allocator_malloc(
            state->vt, (VTERM_MAX_CHARS_PER_CELL + 1) * sizeof(uint32_t)
      );
      if (!chars)
         break;

      for( ; i < glyph_ends; i++) {
         int this_width;
         if (vterm_get_special_pty_type() == 2) {
            state->vt->in_backspace -= (state->vt->in_backspace > 0) ? 1 : 0;
            if (state->vt->in_backspace == 1)
               codepoints[i] = 0; // codepoints under this condition must be 0
         }
         chars[i - glyph_starts] = codepoints[i];
         this_width = vterm_unicode_width(codepoints[i]);
#ifdef DEBUG
         if (this_width < 0) {
            fprintf(stderr, "Text with negative-width codepoint U+%04x\n", codepoints[i]);
            abort();
         }
#endif
         if (i == glyph_starts || this_width > width)
            width = this_width;  // TODO: should be += ?
      }

      while(i < npoints && vterm_unicode_is_combining(codepoints[i]))
         i++;

      chars[glyph_ends - glyph_starts] = 0;
      i--;

#ifdef DEBUG_GLYPH_COMBINE
      int printpos;
      printf("DEBUG: COMBINED GLYPH of %d chars {", glyph_ends - glyph_starts);
   for(printpos = 0; printpos < glyph_ends - glyph_starts; printpos++)
         printf("U+%04x ", chars[printpos]);
      printf("}, onscreen width %d\n", width);
#endif

   if (state->at_phantom || state->pos.col + width > THISROWWIDTH(state)) {
      linefeed(state);
      state->pos.col = 0;
      state->at_phantom = 0;
      state->lineinfo[state->pos.row].continuation = 1;
   }

   if (state->mode.insert) {
      // TODO: This will be a little inefficient for large bodies of text, as
      // it'll have to 'ICH' effectively before every glyph. We should scan
      // ahead and ICH as many times as required
      VTermRect rect;
      rect.start_row = state->pos.row;
      rect.end_row   = state->pos.row + 1;
      rect.start_col = state->pos.col;
      rect.end_col   = THISROWWIDTH(state);
      scroll(state, rect, 0, -1);
   }

   statePutglyph(state, chars, width, state->pos);

   if (i == npoints - 1) {
      //End of the buffer. Save the chars in case we have to combine with more on the next call
      int save_i;
      for(save_i = 0; chars[save_i]; save_i++) {
         if (save_i >= (int)state->combine_chars_size)
            grow_combine_buffer(state);
         state->combine_chars[save_i] = chars[save_i];
      }
      if (save_i >= (int)state->combine_chars_size)
         grow_combine_buffer(state);
      state->combine_chars[save_i] = 0;
      state->combine_width = width;
      state->combine_pos = state->pos;
   }

   if (state->pos.col + width >= THISROWWIDTH(state)) {
      if (state->mode.autowrap)
         state->at_phantom = 1;
      } else {
         state->pos.col += width;
      }
      vterm_allocator_free(state->vt, chars);
   }

   updatecursor(state, &oldpos, 0);

#ifdef DEBUG
   if (state->pos.row < 0 || state->pos.row >= state->rows ||
       state->pos.col < 0 || state->pos.col >= state->cols) {
      fprintf(stderr, "Position out of bounds after text: (%d,%d)\n",
        state->pos.row, state->pos.col);
      abort();
   }
#endif

   return (int)eaten;
}

static int
on_control(unsigned char control, void *user) {
  VTermState *state = user;

  VTermPos oldpos = state->pos;

  VTermScreenCell cell;

  // Preparing to see the leading byte
  VTermPos leadpos = state->pos;
  leadpos.col -= (leadpos.col >= 2 ? 2 : 0);

   switch(control) {
   case 0x07: // BEL - ECMA-48 8.3.3
    if (state->callbacks && state->callbacks->bell)
       (*state->callbacks->bell)(state->cbdata);
    break;

   case 0x08: // BS - ECMA-48 8.3.5
    if (state->pos.col > 0)
       state->pos.col--;
    if (vterm_get_special_pty_type() == 2) {
       // In 2 cell letters, go back 2 cells
       vterm_screen_get_cell(state->vt->screen, leadpos, &cell);
       if (vterm_unicode_width(cell.chars[0]) == 2)
          state->pos.col--;
     }
     break;

   case 0x09: // HT - ECMA-48 8.3.60
      tab(state, 1, +1);
      break;

   case 0x0a: // LF - ECMA-48 8.3.74
   case 0x0b: // VT
   case 0x0c: // FF
     linefeed(state);
     if (state->mode.newline)
        state->pos.col = 0;
     break;

   case 0x0d: // CR - ECMA-48 8.3.15
     state->pos.col = 0;
     break;

   case 0x0e: // LS1 - ECMA-48 8.3.76
     state->gl_set = 1;
     break;

   case 0x0f: // LS0 - ECMA-48 8.3.75
     state->gl_set = 0;
     break;

   case 0x84: // IND - DEPRECATED but implemented for completeness
     linefeed(state);
     break;

   case 0x85: // NEL - ECMA-48 8.3.86
     linefeed(state);
     state->pos.col = 0;
     break;

   case 0x88: // HTS - ECMA-48 8.3.62
    set_col_tabstop(state, state->pos.col);
    break;

   case 0x8d: // RI - ECMA-48 8.3.104
      if (state->pos.row == state->scrollregion_top) {
         VTermRect rect;
         rect.start_row = state->scrollregion_top;
         rect.end_row   = SCROLLREGION_BOTTOM(state);
         rect.start_col = SCROLLREGION_LEFT(state);
         rect.end_col   = SCROLLREGION_RIGHT(state);

         scroll(state, rect, -1, 0);
      } ei(state->pos.row > 0)
         state->pos.row--;
      break;

   case 0x8e: // SS2 - ECMA-48 8.3.141
      state->gsingle_set = 2;
      break;

   case 0x8f: // SS3 - ECMA-48 8.3.142
      state->gsingle_set = 3;
      break;

   default:
      if (state->fallbacks && state->fallbacks->control
            && (*state->fallbacks->control)(control, state->fbdata)
      )
         return 1;

      return 0;
   }

   updatecursor(state, &oldpos, 1);

#ifdef DEBUG
   if (state->pos.row < 0 || state->pos.row >= state->rows 
         || state->pos.col < 0 || state->pos.col >= state->cols
   ) {
      fprintf(stderr, "Position out of bounds after Ctrl %02x: (%d,%d)\n",
         control, state->pos.row, state->pos.col);
      abort();
   }
#endif

   return 1;
}

private int
settermprop_bool(VTermState *state, VTermProp prop, int v) {
   VTermValue val;
   val.boolean = v;
   return vterm_state_set_termprop(state, prop, &val);
}

private int
settermprop_int(VTermState *state, VTermProp prop, int v) {
   VTermValue val;
   val.number = v;
   return vterm_state_set_termprop(state, prop, &val);
}

private int
settermprop_string(VTermState *state, VTermProp prop, VTermStringFragment frag) {
   VTermValue val;

   val.string = frag;
   return vterm_state_set_termprop(state, prop, &val);
}

static void
savecursor(VTermState *state, int save) {
   if (save) {
      state->saved.pos = state->pos;
      state->saved.mode.cursor_visible = state->mode.cursor_visible;
      state->saved.mode.cursor_blink   = state->mode.cursor_blink;
      state->saved.mode.cursor_shape   = state->mode.cursor_shape;
 
      vterm_state_savepen(state, 1);
   } else {
      VTermPos oldpos = state->pos;

      state->pos = state->saved.pos;

      settermprop_bool(state, VTERM_PROP_CURSORVISIBLE, state->saved.mode.cursor_visible);
      settermprop_bool(state, VTERM_PROP_CURSORBLINK,   state->saved.mode.cursor_blink);
      settermprop_int (state, VTERM_PROP_CURSORSHAPE,   state->saved.mode.cursor_shape);
 
      vterm_state_savepen(state, 0);
 
      updatecursor(state, &oldpos, 1);
   }
}

private int
on_escape(Byte* bytes, Unt len, void *user) {
   VTermState *state = user;
 
   //Easier to decode this from the first byte, even though the final byte terminates it
   switch(bytes[0]) {
   case ' ':
      if (len != 2)
         return 0;

      switch(bytes[1]) {
      case 'F': // S7C1T
         break;

      case 'G': // S8C1T
         break;

      default:
         return 0;
      }
      return 2;

   case '#':
      if (len != 2)
         return 0;

      switch(bytes[1]) {
      case '3': // DECDHL top
         if (state->mode.leftrightmargin)
            break;
         set_lineinfo(state, state->pos.row, NO_FORCE, DWL_ON, DHL_TOP);
         break;

      case '4': // DECDHL bottom
         if (state->mode.leftrightmargin)
            break;
         set_lineinfo(state, state->pos.row, NO_FORCE, DWL_ON, DHL_BOTTOM);
         break;

      case '5': // DECSWL
         if (state->mode.leftrightmargin)
             break;
         set_lineinfo(state, state->pos.row, NO_FORCE, DWL_OFF, DHL_OFF);
         break;

      case '6': // DECDWL
         if (state->mode.leftrightmargin)
           break;
         set_lineinfo(state, state->pos.row, NO_FORCE, DWL_ON, DHL_OFF);
         break;

      case '8': {// DECALN
         VTermPos pos;
         uint32_t eGlyph[] = { 'E', 0 };
         for(pos.row = 0; pos.row < state->rows; pos.row++) {
            for(pos.col = 0; pos.col < ROWWIDTH(state, pos.row); pos.col++)
               statePutglyph(state, eGlyph, 1, pos);
         } 
         break;
      }

      default:
         return 0;
      }
      return 2;

   case '(': 
   case ')': 
   case '*': 
   case '+': // SCS
      if (len != 2)
         return 0;
      return 2;

   case '7': // DECSC
      savecursor(state, 1);
      return 1;

   case '8': // DECRC
      savecursor(state, 0);
      return 1;

   case '<': // Ignored by VT100. Used in VT52 mode to switch up to VT100
      return 1;

   case '=': // DECKPAM
      state->mode.keypad = 1;
      return 1;

   case '>': // DECKPNM
      state->mode.keypad = 0;
      return 1;

   case 'c': { // RIS - ECMA-48 8.3.105
      VTermPos oldpos = state->pos;
      vterm_state_reset(state, 1);
      if (state->callbacks && state->callbacks->movecursor)
         (*state->callbacks->movecursor)(
               state->pos, oldpos, state->mode.cursor_visible, state->cbdata
         );
      return 1;
   }

   case 'n': // LS2 - ECMA-48 8.3.78
      state->gl_set = 2;
      return 1;

   case 'o': // LS3 - ECMA-48 8.3.80
      state->gl_set = 3;
      return 1;

   case '~': // LS1R - ECMA-48 8.3.77
      state->gr_set = 1;
      return 1;

   case '}': // LS2R - ECMA-48 8.3.79
      state->gr_set = 2;
      return 1;

   case '|': // LS3R - ECMA-48 8.3.81
      state->gr_set = 3;
      return 1;

   default:
      return 0;
   }
}

static void
set_mode(VTermState *state, int num, int val) {
   switch(num) {
   case 4: // IRM - ECMA-48 7.2.10
     state->mode.insert = val;
     break;

   case 20: // LNM - ANSI X3.4-1977
     state->mode.newline = val;
      break;

   default:
      DEBUG_LOG1("libvterm: Unknown mode %d\n", num);
      return;
   }
}

static void
request_dec_mode(VTermState *state, int num) {
  int reply;

   switch(num) {
   case 1:
      reply = state->mode.cursor;
      break;

 case 5:
   reply = state->mode.screen;
   break;

 case 6:
   reply = state->mode.origin;
   break;

 case 7:
   reply = state->mode.autowrap;
   break;

 case 12:
   reply = state->mode.cursor_blink;
   break;

 case 25:
   reply = state->mode.cursor_visible;
   break;

 case 69:
   reply = state->mode.leftrightmargin;
   break;

 case 1000:
   reply = state->mouse_flags == MOUSE_WANT_CLICK;
   break;

 case 1002:
   reply = state->mouse_flags == (MOUSE_WANT_CLICK|MOUSE_WANT_DRAG);
   break;

 case 1003:
   reply = state->mouse_flags == (MOUSE_WANT_CLICK|MOUSE_WANT_MOVE);
   break;

 case 1004:
   reply = state->mode.report_focus;
   break;

 case 1005:
   break;

 case 1006:
    reply = TRUE;
   break;

 case 1015:
   break;

 case 1047:
   reply = state->mode.alt_screen;
   break;

 case 2004:
   reply = state->mode.bracketpaste;
   break;

   default:
      vterm_push_output_sprintf_ctrl(state->vt, C1_CSI, "?%d;%d$y", num, 0);
      return;
   }

   vterm_push_output_sprintf_ctrl(state->vt, C1_CSI, "?%d;%d$y", num, reply ? 1 : 2);
}

private void request_version_string(VTermState *state) {
   vterm_push_output_sprintf_str(state->vt, C1_DCS, TRUE, ">|libvterm(%d.%d)",
      VTERM_VERSION_MAJOR, VTERM_VERSION_MINOR);
}

private int
on_csi(
   CS leader, 
   const long args[], 
   int argcount, 
   CS intermed, 
   Byte command, 
   void *user
) {
   VTermState *state = user;
   int leader_byte = 0;
   int intermed_byte = 0;
   int cancel_phantom = 1;
   VTermPos oldpos = state->pos;
   int handled = 1;
 
   // Some temporaries for later code
   int count, val;
   Unt row, col;
   VTermRect rect;
   int selective;
 
   if (leader && leader[0]) {
      if (leader[1]) // longer than 1 char
         return 0;
  
      switch(leader[0]) {
      case '?':
      case '>':
         leader_byte = leader[0];
         break;
      default:
         return 0;
      }
   }

   if (intermed && intermed[0]) {
      if (intermed[1]) // longer than 1 char
         return 0;

      switch(intermed[0]) {
      case ' ':
      case '!':
      case '"':
      case '$':
      case '\'':
         intermed_byte = intermed[0];
         break;
      default:
         return 0;
      }
   }

   oldpos = state->pos;

#define LBOUND(v,min) if ((v) < (min)) (v) = (min)
#define UBOUND(v,max) if ((v) > (max)) (v) = (max)

#define LEADER(l,b) ((l << 8) | b)
#define INTERMED(i,b) ((i << 16) | b)

   switch(intermed_byte << 16 | leader_byte << 8 | command) {
   case 0x40: // ICH - ECMA-48 8.3.64
      count = CSI_ARG_COUNT(args[0]);

      if (!is_cursor_in_scrollregion(state))
         break;

      rect.start_row = state->pos.row;
      rect.end_row   = state->pos.row + 1;
      rect.start_col = state->pos.col;
      if (state->mode.leftrightmargin)
         rect.end_col = SCROLLREGION_RIGHT(state);
      else
         rect.end_col = THISROWWIDTH(state);

      scroll(state, rect, 0, -count);

      break;

   case 0x41: // CUU - ECMA-48 8.3.22
      count = CSI_ARG_COUNT(args[0]);
      state->pos.row -= count;
      state->at_phantom = 0;
      break;

   case 0x42: // CUD - ECMA-48 8.3.19
      count = CSI_ARG_COUNT(args[0]);
      state->pos.row += count;
      state->at_phantom = 0;
      break;

   case 0x43: // CUF - ECMA-48 8.3.20
      count = CSI_ARG_COUNT(args[0]);
      state->pos.col += count;
      state->at_phantom = 0;
      break;

   case 0x44: // CUB - ECMA-48 8.3.18
      count = CSI_ARG_COUNT(args[0]);
      state->pos.col -= count;
      state->at_phantom = 0;
      break;

   case 0x45: // CNL - ECMA-48 8.3.12
      count = CSI_ARG_COUNT(args[0]);
      state->pos.col = 0;
      state->pos.row += count;
      state->at_phantom = 0;
      break;

   case 0x46: // CPL - ECMA-48 8.3.13
      count = CSI_ARG_COUNT(args[0]);
      state->pos.col = 0;
      state->pos.row -= count;
      state->at_phantom = 0;
      break;

   case 0x47: // CHA - ECMA-48 8.3.9
      val = CSI_ARG_OR(args[0], 1);
      state->pos.col = val-1;
      state->at_phantom = 0;
      break;

   case 0x48: // CUP - ECMA-48 8.3.21
      row = CSI_ARG_OR(args[0], 1);
      col = argcount < 2 || CSI_ARG_IS_MISSING(args[1]) ? 1 : CSI_ARG(args[1]);
      // zero-based
      if (vterm_get_special_pty_type() == 2) {
         // Fix a sequence that is not correct right now
         if (state->pos.row == row - 1) {
            Unt ptr = 0;
            for(Unt cnt = 0; cnt < col - 1; ++cnt) {
               VTermPos p;
               VTermScreenCell c0, c1;
               p.row = row - 1;
               p.col = ptr;
               vterm_screen_get_cell(state->vt->screen, p, &c0);
               p.col++;
               vterm_screen_get_cell(state->vt->screen, p, &c1);
               Unt diff = (c1.chars[0] == UNT)		    // double cell?
                  ? ((vterm_unicode_is_ambiguous(c0.chars[0]))    // is ambiguous?
                     ? vterm_unicode_width(0x00a1) 
                     : 1)		    // &ambiwidth
                  : 1;	
               ptr += diff;
            }
            col = ptr + 1;
         }
      }
      state->pos.row = row-1;
      state->pos.col = col-1;
      if (state->mode.origin) {
         state->pos.row += state->scrollregion_top;
         state->pos.col += SCROLLREGION_LEFT(state);
      }
      state->at_phantom = 0;
      break;

   case 0x49: // CHT - ECMA-48 8.3.10
      count = CSI_ARG_COUNT(args[0]);
      tab(state, count, +1);
      break;

   case 0x4a: // ED - ECMA-48 8.3.39
   case LEADER('?', 0x4a): // DECSED - Selective Erase in Display
      selective = (leader_byte == '?');
      switch(CSI_ARG(args[0])) {
      case CSI_ARG_MISSING:
      case 0:
         rect.start_row = state->pos.row; rect.end_row = state->pos.row + 1;
         rect.start_col = state->pos.col; rect.end_col = state->cols;
         if (rect.end_col > rect.start_col)
            stateErase(state, rect, selective);

         rect.start_row = state->pos.row + 1; rect.end_row = state->rows;
         rect.start_col = 0;
         for (row = rect.start_row; row < rect.end_row; row++)
            set_lineinfo(state, row, FORCE, DWL_OFF, DHL_OFF);
         if (rect.end_row > rect.start_row)
            stateErase(state, rect, selective);
         break;

    case 1:
      rect.start_row = 0; rect.end_row = state->pos.row;
      rect.start_col = 0; rect.end_col = state->cols;
      for(row = rect.start_row; row < rect.end_row; row++)
         set_lineinfo(state, row, FORCE, DWL_OFF, DHL_OFF);
      if (rect.end_col > rect.start_col)
         stateErase(state, rect, selective);

      rect.start_row = state->pos.row; rect.end_row = state->pos.row + 1;
                          rect.end_col = state->pos.col + 1;
      if (rect.end_row > rect.start_row)
         stateErase(state, rect, selective);
      break;

    case 2:
      rect.start_row = 0; rect.end_row = state->rows;
      rect.start_col = 0; rect.end_col = state->cols;
      for(row = rect.start_row; row < rect.end_row; row++)
         set_lineinfo(state, row, FORCE, DWL_OFF, DHL_OFF);
      stateErase(state, rect, selective);
      break;

      case 3:
         if (state->callbacks && state->callbacks->sb_clear 
               && (*state->callbacks->sb_clear)(state->cbdata))
             return 1;
         break;
      }
      break;

   case 0x4b: // EL - ECMA-48 8.3.41
   case LEADER('?', 0x4b): // DECSEL - Selective Erase in Line
    selective = (leader_byte == '?');
    rect.start_row = state->pos.row;
    rect.end_row   = state->pos.row + 1;

    switch(CSI_ARG(args[0])) {
    case CSI_ARG_MISSING:
    case 0:
      rect.start_col = state->pos.col; rect.end_col = THISROWWIDTH(state); break;
    case 1:
      rect.start_col = 0; rect.end_col = state->pos.col + 1; break;
    case 2:
      rect.start_col = 0; rect.end_col = THISROWWIDTH(state); break;
    default:
      return 0;
    }

    if (rect.end_col > rect.start_col)
      stateErase(state, rect, selective);

    break;

   case 0x4c: // IL - ECMA-48 8.3.67
    count = CSI_ARG_COUNT(args[0]);

    if (!is_cursor_in_scrollregion(state))
      break;

    rect.start_row = state->pos.row;
    rect.end_row   = SCROLLREGION_BOTTOM(state);
    rect.start_col = SCROLLREGION_LEFT(state);
    rect.end_col   = SCROLLREGION_RIGHT(state);

    scroll(state, rect, -count, 0);

    break;

   case 0x4d: // DL - ECMA-48 8.3.32
    count = CSI_ARG_COUNT(args[0]);

    if (!is_cursor_in_scrollregion(state))
      break;

    rect.start_row = state->pos.row;
    rect.end_row   = SCROLLREGION_BOTTOM(state);
    rect.start_col = SCROLLREGION_LEFT(state);
      rect.end_col   = SCROLLREGION_RIGHT(state);

      scroll(state, rect, count, 0);

      break;

   case 0x50: // DCH - ECMA-48 8.3.26
      count = CSI_ARG_COUNT(args[0]);

      if (!is_cursor_in_scrollregion(state))
         break;

       rect.start_row = state->pos.row;
       rect.end_row   = state->pos.row + 1;
       rect.start_col = state->pos.col;
       if (state->mode.leftrightmargin)
          rect.end_col = SCROLLREGION_RIGHT(state);
       else
          rect.end_col = THISROWWIDTH(state);

       scroll(state, rect, 0, count);

       break;

   case 0x53: // SU - ECMA-48 8.3.147
      count = CSI_ARG_COUNT(args[0]);

      rect.start_row = state->scrollregion_top;
      rect.end_row   = SCROLLREGION_BOTTOM(state);
      rect.start_col = SCROLLREGION_LEFT(state);
      rect.end_col   = SCROLLREGION_RIGHT(state);

      scroll(state, rect, count, 0);

      break;

   case 0x54: // SD - ECMA-48 8.3.113
    count = CSI_ARG_COUNT(args[0]);

    rect.start_row = state->scrollregion_top;
    rect.end_row   = SCROLLREGION_BOTTOM(state);
    rect.start_col = SCROLLREGION_LEFT(state);
    rect.end_col   = SCROLLREGION_RIGHT(state);

    scroll(state, rect, -count, 0);

    break;

   case 0x58: // ECH - ECMA-48 8.3.38
    count = CSI_ARG_COUNT(args[0]);

    rect.start_row = state->pos.row;
    rect.end_row   = state->pos.row + 1;
    rect.start_col = state->pos.col;
    rect.end_col   = state->pos.col + count;
    UBOUND(rect.end_col, THISROWWIDTH(state));

    stateErase(state, rect, 0);
    break;

   case 0x5a: // CBT - ECMA-48 8.3.7
    count = CSI_ARG_COUNT(args[0]);
    tab(state, count, -1);
    break;

   case 0x60: // HPA - ECMA-48 8.3.57
    col = CSI_ARG_OR(args[0], 1);
    state->pos.col = col-1;
    state->at_phantom = 0;
    break;

   case 0x61: // HPR - ECMA-48 8.3.59
    count = CSI_ARG_COUNT(args[0]);
    state->pos.col += count;
    state->at_phantom = 0;
    break;

   case 0x62: { // REP - ECMA-48 8.3.103
      const Unt row_width = THISROWWIDTH(state);
      count = CSI_ARG_COUNT(args[0]);
      col = state->pos.col + count;
      UBOUND(col, row_width);
      while (state->pos.col < col) {
         statePutglyph(state, state->combine_chars, state->combine_width, state->pos);
         state->pos.col += state->combine_width;
      }
      if (state->pos.col + state->combine_width >= row_width && state->mode.autowrap) {
         state->at_phantom = 1;
         cancel_phantom = 0;
      }
      break;
   }

   case 0x63: // DA - ECMA-48 8.3.24
    val = CSI_ARG_OR(args[0], 0);
    if (val == 0)
      // DEC VT100 response
      vterm_push_output_sprintf_ctrl(state->vt, C1_CSI, "?1;2c");
    break;

   case LEADER('>', 0x63): // DEC secondary Device Attributes
    // This returns xterm version number 100.
    vterm_push_output_sprintf_ctrl(state->vt, C1_CSI, ">%d;%d;%dc", 0, 100, 0);
    break;

   case 0x64: // VPA - ECMA-48 8.3.158
      row = CSI_ARG_OR(args[0], 1);
      state->pos.row = row-1;
      if (state->mode.origin)
         state->pos.row += state->scrollregion_top;
      state->at_phantom = 0;
      break;

   case 0x65: // VPR - ECMA-48 8.3.160
    count = CSI_ARG_COUNT(args[0]);
    state->pos.row += count;
    state->at_phantom = 0;
    break;

   case 0x66: // HVP - ECMA-48 8.3.63
    row = CSI_ARG_OR(args[0], 1);
    col = argcount < 2 || CSI_ARG_IS_MISSING(args[1]) ? 1 : CSI_ARG(args[1]);
    // zero-based
    state->pos.row = row-1;
    state->pos.col = col-1;
    if (state->mode.origin) {
      state->pos.row += state->scrollregion_top;
      state->pos.col += SCROLLREGION_LEFT(state);
    }
    state->at_phantom = 0;
    break;

   case 0x67: // TBC - ECMA-48 8.3.154
    val = CSI_ARG_OR(args[0], 0);

   switch(val) {
   case 0:
      clear_col_tabstop(state, state->pos.col);
      break;
   case 3:
   case 5:
       for(Unt col = 0; col < state->cols; col++)
          clear_col_tabstop(state, col);
       break;
   case 1:
   case 2:
   case 4:
      break;
    /* TODO: 1, 2 and 4 aren't meaningful yet without line tab stops */
    default:
      return 0;
    }
    break;

   case 0x68: // SM - ECMA-48 8.3.125
    if (!CSI_ARG_IS_MISSING(args[0]))
      set_mode(state, CSI_ARG(args[0]), 1);
    break;

   case LEADER('?', 0x68): // DEC private mode set
    break;

   case 0x6a: // HPB - ECMA-48 8.3.58
    count = CSI_ARG_COUNT(args[0]);
    state->pos.col -= count;
    state->at_phantom = 0;
    break;

   case 0x6b: // VPB - ECMA-48 8.3.159
    count = CSI_ARG_COUNT(args[0]);
    state->pos.row -= count;
    state->at_phantom = 0;
    break;

   case 0x6c: // RM - ECMA-48 8.3.106
    if (!CSI_ARG_IS_MISSING(args[0]))
      set_mode(state, CSI_ARG(args[0]), 0);
    break;

   case LEADER('?', 0x6c): // DEC private mode reset
    break;

   case 0x6d: // SGR - ECMA-48 8.3.117
    vterm_state_setpen(state, args, argcount);
    break;

   case LEADER('?', 0x6d): // DECSGR and XTQMODKEYS
    // CSI ? 4 m  XTQMODKEYS: request modifyOtherKeys level
    if (argcount == 1 && CSI_ARG(args[0]) == 4)
    {
      vterm_push_output_sprintf_ctrl(state->vt, C1_CSI, ">4;%dm",
					state->mode.modify_other_keys ? 2 : 0);
      break;
    }

    /* No actual DEC terminal recognized these, but some printers did. These
     * are alternative ways to request subscript/superscript/off
     */
    for(int argi = 0; argi < argcount; argi++) {
      long arg;
      switch(arg = CSI_ARG(args[argi])) {
        case 4: // Superscript on
          arg = 73;
          vterm_state_setpen(state, &arg, 1);
          break;
        case 5: // Subscript on
          arg = 74;
          vterm_state_setpen(state, &arg, 1);
          break;
        case 24: // Super+subscript off
          arg = 75;
          vterm_state_setpen(state, &arg, 1);
          break;
      }
    }
    break;

   case LEADER('>', 0x6d): // CSI > 4 ; Pv m   xterm resource modifyOtherKeys
    if (argcount == 2 && CSI_ARG(args[0]) == 4)
    {
      // can't have both modify_other_keys and kitty_keyboard
      state->mode.kitty_keyboard = 0;

      state->mode.modify_other_keys = CSI_ARG(args[1]) == 2;
    }
    break;

   case LEADER('>', 0x75): // CSI > 1 u  enable kitty keyboard protocol
    if (argcount == 1 && CSI_ARG(args[0]) == 1)
    {
      // can't have both modify_other_keys and kitty_keyboard
      state->mode.modify_other_keys = 0;

      state->mode.kitty_keyboard = 1;
    }
    break;

   case LEADER('<', 0x75): // CSI < u  disable kitty keyboard protocol
    if (argcount <= 1)
      state->mode.kitty_keyboard = 0;
    break;

   case LEADER('?', 0x75): // CSI ? u  request kitty keyboard protocol state
    if (argcount <= 1)
      // TODO: this only uses the values zero and one.  The protocol specifies
      // more values, the progressive enhancement flags.
      vterm_push_output_sprintf_ctrl(state->vt, C1_CSI, "?%du",
						   state->mode.kitty_keyboard);
    break;

   case 0x6e: // DSR - ECMA-48 8.3.35
   case LEADER('?', 0x6e): // DECDSR
    val = CSI_ARG_OR(args[0], 0);

    {
      char *qmark = (leader_byte == '?') ? "?" : "";

      switch(val) {
      case 0: case 1: case 2: case 3: case 4:
        // ignore - these are replies
        break;
      case 5:
        vterm_push_output_sprintf_ctrl(state->vt, C1_CSI, "%s0n", qmark);
        break;
      case 6: // CPR - cursor position report
        vterm_push_output_sprintf_ctrl(state->vt, C1_CSI, "%s%d;%dR", qmark, state->pos.row + 1, state->pos.col + 1);
        break;
      }
    }
    break;


   case INTERMED('!', 0x70): // DECSTR - DEC soft terminal reset
    vterm_state_reset(state, 0);
    break;

   case LEADER('?', INTERMED('$', 0x70)):
    request_dec_mode(state, CSI_ARG(args[0]));
    break;

   case LEADER('>', 0x71): // XTVERSION - xterm query version string
    request_version_string(state);
    break;

   case INTERMED(' ', 0x71): // DECSCUSR - DEC set cursor shape
    val = CSI_ARG_OR(args[0], 1);

    switch(val) {
    case 0: case 1:
      settermprop_bool(state, VTERM_PROP_CURSORBLINK, 1);
      settermprop_int (state, VTERM_PROP_CURSORSHAPE, VTERM_PROP_CURSORSHAPE_BLOCK);
      break;
    case 2:
      settermprop_bool(state, VTERM_PROP_CURSORBLINK, 0);
      settermprop_int (state, VTERM_PROP_CURSORSHAPE, VTERM_PROP_CURSORSHAPE_BLOCK);
      break;
    case 3:
      settermprop_bool(state, VTERM_PROP_CURSORBLINK, 1);
      settermprop_int (state, VTERM_PROP_CURSORSHAPE, VTERM_PROP_CURSORSHAPE_UNDERLINE);
      break;
    case 4:
      settermprop_bool(state, VTERM_PROP_CURSORBLINK, 0);
      settermprop_int (state, VTERM_PROP_CURSORSHAPE, VTERM_PROP_CURSORSHAPE_UNDERLINE);
      break;
    case 5:
      settermprop_bool(state, VTERM_PROP_CURSORBLINK, 1);
      settermprop_int (state, VTERM_PROP_CURSORSHAPE, VTERM_PROP_CURSORSHAPE_BAR_LEFT);
      break;
    case 6:
      settermprop_bool(state, VTERM_PROP_CURSORBLINK, 0);
      settermprop_int (state, VTERM_PROP_CURSORSHAPE, VTERM_PROP_CURSORSHAPE_BAR_LEFT);
      break;
    }

    break;

   case INTERMED('"', 0x71): // DECSCA - DEC select character protection attribute
    val = CSI_ARG_OR(args[0], 0);

    switch(val) {
    case 0: case 2:
      state->protected_cell = 0;
      break;
    case 1:
      state->protected_cell = 1;
      break;
    }

    break;

   case 0x72: // DECSTBM - DEC custom
      state->scrollregion_top = CSI_ARG_OR(args[0], 1) - 1;
      state->scrollregion_bottom = argcount < 2 || CSI_ARG_IS_MISSING(args[1]) 
            ? UNT : CSI_ARG(args[1]);
      UBOUND(state->scrollregion_top, state->rows);
      if (state->scrollregion_top == 0 && state->scrollregion_bottom == state->rows)
         state->scrollregion_bottom = UNT;
      else
         UBOUND(state->scrollregion_bottom, state->rows);

    if (SCROLLREGION_BOTTOM(state) <= state->scrollregion_top) {
      // Invalid
      state->scrollregion_top    = 0;
      state->scrollregion_bottom = UNT;
    }

    // Setting the scrolling region restores the cursor to the home position
    state->pos.row = 0;
    state->pos.col = 0;
    if (state->mode.origin) {
      state->pos.row += state->scrollregion_top;
      state->pos.col += SCROLLREGION_LEFT(state);
    }

    break;

   case 0x73: // DECSLRM - DEC custom
      // Always allow setting these margins, just they won't take effect without DECVSSM
      state->scrollregion_left = CSI_ARG_OR(args[0], 1) - 1;
      state->scrollregion_right = argcount < 2 || CSI_ARG_IS_MISSING(args[1]) 
         ? -1 : CSI_ARG(args[1]);
      UBOUND(state->scrollregion_left, state->cols);
      if (state->scrollregion_left == 0 && state->scrollregion_right == state->cols)
         state->scrollregion_right = UNT;
      else
         UBOUND(state->scrollregion_right, state->cols);

      if (state->scrollregion_right < UNT && state->scrollregion_right <= state->scrollregion_left
      ) {
         // Invalid
         state->scrollregion_left  = 0;
         state->scrollregion_right = UNT;
      }

       // Setting the scrolling region restores the cursor to the home position
       state->pos.row = 0;
       state->pos.col = 0;
       if (state->mode.origin) {
         state->pos.row += state->scrollregion_top;
         state->pos.col += SCROLLREGION_LEFT(state);
       }

       break;

   case 0x74:
      switch(CSI_ARG(args[0])) {
      case 8: // CSI 8 ; rows ; cols t  set size
         if (argcount == 3)
            on_resize(CSI_ARG(args[1]), CSI_ARG(args[2]), state);
         break;
      default:
         handled = 0;
         break;
      }
      break;

   case INTERMED('\'', 0x7D): // DECIC
    count = CSI_ARG_COUNT(args[0]);

    if (!is_cursor_in_scrollregion(state))
      break;

    rect.start_row = state->scrollregion_top;
    rect.end_row   = SCROLLREGION_BOTTOM(state);
    rect.start_col = state->pos.col;
    rect.end_col   = SCROLLREGION_RIGHT(state);

    scroll(state, rect, 0, -count);

    break;

   case INTERMED('\'', 0x7E): // DECDC
      count = CSI_ARG_COUNT(args[0]);

      if (!is_cursor_in_scrollregion(state))
         break;

      rect.start_row = state->scrollregion_top;
      rect.end_row   = SCROLLREGION_BOTTOM(state);
      rect.start_col = state->pos.col;
      rect.end_col   = SCROLLREGION_RIGHT(state);

      scroll(state, rect, 0, count);

      break;

   default:
      handled = 0;
      break;
   }

   if (!handled) {
      if (state->fallbacks && state->fallbacks->csi
         && (*state->fallbacks->csi)(leader, args, argcount, intermed, command, state->fbdata
            )
      )
         return 1;

      return 0;
   }
   if (state->mode.origin) {
      LBOUND(state->pos.row, state->scrollregion_top);
      UBOUND(state->pos.row, SCROLLREGION_BOTTOM(state) - 1);
      LBOUND(state->pos.col, SCROLLREGION_LEFT(state));
      UBOUND(state->pos.col, SCROLLREGION_RIGHT(state) - 1);
   } else {
      UBOUND(state->pos.row, state->rows - 1);
      UBOUND(state->pos.col, THISROWWIDTH(state) - 1);
   }

  updatecursor(state, &oldpos, cancel_phantom);

#ifdef DEBUG
  if (state->pos.row < 0 || state->pos.row >= state->rows ||
     state->pos.col < 0 || state->pos.col >= state->cols) {
    fprintf(stderr, "Position out of bounds after CSI %c: (%d,%d)\n",
        command, state->pos.row, state->pos.col);
    abort();
  }

  if (SCROLLREGION_BOTTOM(state) <= state->scrollregion_top) {
    fprintf(stderr, "Scroll region height out of bounds after CSI %c: %d <= %d\n",
        command, SCROLLREGION_BOTTOM(state), state->scrollregion_top);
    abort();
  }

   if (SCROLLREGION_RIGHT(state) <= SCROLLREGION_LEFT(state)) {
      fprintf(stderr, "Scroll region width out of bounds after CSI %c: %d <= %d\n",
         command, SCROLLREGION_RIGHT(state), SCROLLREGION_LEFT(state)
      );
     abort();
   }
#endif

   return 1;
}

//private char
//base64_one(uint8_t b) {
//   if (b < 26)
//      return 'A' + b;
//   ei(b < 52)
//      return 'a' + b - 26;
//   ei(b < 62)
//      return '0' + b - 52;
//   ei(b == 62)
//      return '+';
//   ei(b == 63)
//      return '/';
//   return 0;
//}

static uint8_t
unbase64one(char c) {
  if (c >= 'A' && c <= 'Z')
    return c - 'A';
  ei(c >= 'a' && c <= 'z')
    return c - 'a' + 26;
  ei(c >= '0' && c <= '9')
    return c - '0' + 52;
  ei(c == '+')
    return 62;
  ei(c == '/')
    return 63;

  return 0xFF;
}

static void 
osc_selection(VTermState *state, VTermStringFragment frag) {
  if (frag.initial) {
    state->tmp.selection.mask = 0;
    state->tmp.selection.state = SELECTION_INITIAL;
  }

  while(!state->tmp.selection.state && frag.len) {
    /* Parse selection parameter */
    switch(frag.str[0]) {
      case 'c':
        state->tmp.selection.mask |= VTERM_SELECTION_CLIPBOARD;
        break;
      case 'p':
        state->tmp.selection.mask |= VTERM_SELECTION_PRIMARY;
        break;
      case 'q':
        state->tmp.selection.mask |= VTERM_SELECTION_SECONDARY;
        break;
      case 's':
        state->tmp.selection.mask |= VTERM_SELECTION_SELECT;
        break;
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
        state->tmp.selection.mask |= (VTERM_SELECTION_CUT0 << (frag.str[0] - '0'));
        break;

      case ';':
        state->tmp.selection.state = SELECTION_SELECTED;
        if (!state->tmp.selection.mask)
          state->tmp.selection.mask = VTERM_SELECTION_SELECT|VTERM_SELECTION_CUT0;
        break;
    }

    frag.str++;
    frag.len--;
  }

  if (!frag.len) {
    /* Clear selection if we're already finished but didn't do anything */
    if (frag.final && state->selection.callbacks->set) {
      (*state->selection.callbacks->set)(state->tmp.selection.mask, (VTermStringFragment){
              .str     = NULL,
              .len     = 0,
              .initial = state->tmp.selection.state != SELECTION_SET,
              .final   = TRUE,
            }, state->selection.user);
    }
    return;
  }

  if (state->tmp.selection.state == SELECTION_SELECTED) {
    if (frag.str[0] == '?') {
      state->tmp.selection.state = SELECTION_QUERY;
    }
    else {
      state->tmp.selection.state = SELECTION_SET_INITIAL;
      state->tmp.selection.recvpartial = 0;
    }
  }

  if (state->tmp.selection.state == SELECTION_QUERY) {
    if (state->selection.callbacks->query)
      (*state->selection.callbacks->query)(state->tmp.selection.mask, state->selection.user);
    return;
  }

  if (state->tmp.selection.state == SELECTION_INVALID)
    return;

  if (state->selection.callbacks->set) {
    Unt bufcur = 0;
    CS buffer = state->selection.buffer;

    uint32_t x = 0; /* Current decoding value */
    int n = 0;      /* Number of sextets consumed */

    if (state->tmp.selection.recvpartial) {
      n = state->tmp.selection.recvpartial >> 24;
      x = state->tmp.selection.recvpartial & 0x03FFFF; /* could be up to 18 bits of state in here */

      state->tmp.selection.recvpartial = 0;
   }

   while((state->selection.buflen - bufcur) >= 3 && frag.len) {
      if (frag.str[0] == '=') {
        if (n == 2) {
          buffer[0] = (x >> 4) & 0xFF;
          buffer += 1, bufcur += 1;
        }
        if (n == 3) {
          buffer[0] = (x >> 10) & 0xFF;
          buffer[1] = (x >>  2) & 0xFF;
          buffer += 2, bufcur += 2;
        }

        while(frag.len && frag.str[0] == '=')
          frag.str++, frag.len--;

        n = 0;
      } else {
        uint8_t b = unbase64one(frag.str[0]);
        if (b == 0xFF) {
          DEBUG_LOG1("base64decode bad input %02X\n", (uint8_t)frag.str[0]);

          state->tmp.selection.state = SELECTION_INVALID;
          if (state->selection.callbacks->set) {
            (*state->selection.callbacks->set)(state->tmp.selection.mask, (VTermStringFragment){
                .str     = NULL,
                .len     = 0,
                .initial = TRUE,
                .final   = TRUE,
                }, state->selection.user);
          }
          break;
        }

        x = (x << 6) | b;
        n++;
        frag.str++, frag.len--;

        if (n == 4) {
            buffer[0] = (x >> 16) & 0xFF;
            buffer[1] = (x >>  8) & 0xFF;
            buffer[2] = (x >>  0) & 0xFF;

            buffer += 3, bufcur += 3;
            x = 0;
            n = 0;
         }
      }

      if (!frag.len || (state->selection.buflen - bufcur) < 3) {
        if (bufcur) {
	  VTermStringFragment setfrag = {
	    state->selection.buffer, // str
	    bufcur, // len
	    state->tmp.selection.state == SELECTION_SET_INITIAL, // initial
	    frag.final && !frag.len // final
	  };
          (*state->selection.callbacks->set)(state->tmp.selection.mask,
	      setfrag, state->selection.user);
          state->tmp.selection.state = SELECTION_SET;
        }

        buffer = state->selection.buffer;
        bufcur = 0;
      }
    }

    if (n)
      state->tmp.selection.recvpartial = (n << 24) | x;
  }
}

static int
on_osc(int command, VTermStringFragment frag, void *user) {
  VTermState *state = user;

   switch(command) {
    case 0:
      settermprop_string(state, VTERM_PROP_ICONNAME, frag);
      settermprop_string(state, VTERM_PROP_TITLE, frag);
      return 1;

    case 1:
      settermprop_string(state, VTERM_PROP_ICONNAME, frag);
      return 1;

    case 2:
      settermprop_string(state, VTERM_PROP_TITLE, frag);
      return 1;

    case 10:
      {
        // request foreground color: <Esc>]10;?<0x07>
        int red = state->default_fg.red;
        int blue = state->default_fg.blue;
        int green = state->default_fg.green;
        vterm_push_output_sprintf_ctrl(state->vt, C1_OSC, "10;rgb:%02x%02x/%02x%02x/%02x%02x\x07", red, red, green, green, blue, blue);
        return 1;
      }

    case 11:
      {
	// request background color: <Esc>]11;?<0x07>
	int red = state->default_bg.red;
	int blue = state->default_bg.blue;
	int green = state->default_bg.green;
	vterm_push_output_sprintf_ctrl(state->vt, C1_OSC, "11;rgb:%02x%02x/%02x%02x/%02x%02x\x07", red, red, green, green, blue, blue);
	return 1;
      }
    case 12:
      settermprop_string(state, VTERM_PROP_CURSORCOLOR, frag);
      return 1;

    case 52:
      if (state->selection.callbacks)
        osc_selection(state, frag);

      return 1;

    default:
      if (state->fallbacks && state->fallbacks->osc)
        if ((*state->fallbacks->osc)(command, frag, state->fbdata))
          return 1;
  }

  return 0;
}

static void
request_status_string(VTermState *state, VTermStringFragment frag) {
   VTerm *vt = state->vt;

   char *tmp = state->tmp.decrqss;
 
   if (frag.initial)
      tmp[0] = tmp[1] = tmp[2] = tmp[3] = 0;

   Unt i = 0;
   while(i < sizeof(state->tmp.decrqss)-1 && tmp[i])
      i++;
   while(i < sizeof(state->tmp.decrqss)-1 && frag.len--)
      tmp[i++] = (frag.str++)[0];
   tmp[i] = 0;

   if (!frag.final)
      return;

   switch(tmp[0] | tmp[1]<<8 | tmp[2]<<16) {
   case 'm': {
      // Query SGR
      long args[20];
      int argc = vterm_state_getpen(state, args, sizeof(args)/sizeof(args[0]));
      Unt cur = 0;

      cur += SNPRINTF( vt->tmpbuffer + cur, vt->tmpbuffer_len - cur, ESC_S "P" "1$r"); //DCS 1$r...
      if (cur >= vt->tmpbuffer_len)
        return;

      for(int argi = 0; argi < argc; argi++) {
        cur += SNPRINTF(vt->tmpbuffer + cur, vt->tmpbuffer_len - cur,
            argi == argc - 1             ? "%ld" :
            CSI_ARG_HAS_MORE(args[argi]) ? "%ld:" :
                                           "%ld;",
            CSI_ARG(args[argi]));
        if (cur >= vt->tmpbuffer_len)
          return;
      }

      cur += SNPRINTF(vt->tmpbuffer + cur, vt->tmpbuffer_len - cur, "m" ESC_S "\\"); //... m ST
      if (cur >= vt->tmpbuffer_len)
         return;

      vterm_push_output_bytes(vt, vt->tmpbuffer, cur);
      return;
    }

    case 'r':
      // Query DECSTBM
      vterm_push_output_sprintf_str(vt, C1_DCS, TRUE,
          "1$r%d;%dr", state->scrollregion_top + 1, SCROLLREGION_BOTTOM(state));
      return;

   case 's':
      // Query DECSLRM
      vterm_push_output_sprintf_str(vt, C1_DCS, TRUE,
          "1$r%d;%ds", SCROLLREGION_LEFT(state)+1, SCROLLREGION_RIGHT(state)
      );
      return;

   case ' '|('q'<<8): {
      // Query DECSCUSR
      int reply;
      switch(state->mode.cursor_shape) {
      case VTERM_PROP_CURSORSHAPE_BLOCK:     reply = 2; break;
      case VTERM_PROP_CURSORSHAPE_UNDERLINE: reply = 4; break;
      default: /* VTERM_PROP_CURSORSHAPE_BAR_LEFT */ reply = 6; break;
      }
      if (state->mode.cursor_blink)
         reply--;
      vterm_push_output_sprintf_str(vt, C1_DCS, TRUE, "1$r%d q", reply);
      return;
   }

   case '\"'|('q'<<8):
      // Query DECSCA
      vterm_push_output_sprintf_str(vt, C1_DCS, TRUE, "1$r%d\"q", state->protected_cell ? 1 : 2);
      return;
   }

   vterm_push_output_sprintf_str(state->vt, C1_DCS, TRUE, "0$r");
}

static int
on_dcs(CS command, Unt commandlen, VTermStringFragment frag, void *user) {
   VTermState *state = user;

   if (commandlen == 2 && strneq((char*)command, "$q", 2)) {
      request_status_string(state, frag);
      return 1;
   } ei(state->fallbacks && state->fallbacks->dcs)
      if ((*state->fallbacks->dcs)(command, commandlen, frag, state->fbdata))
         return 1;

   DEBUG_LOG2("libvterm: Unhandled DCS %.*s\n", (int)commandlen, command);
   return 0;
}

static int
on_apc(VTermStringFragment frag, void *user) {
   VTermState *state = user;

   if (state->fallbacks && state->fallbacks->apc && (*state->fallbacks->apc)(frag, state->fbdata))
      return 1;

   // No DEBUG_LOG because all APCs are unhandled
   return 0;
}

static int
on_pm(VTermStringFragment frag, void *user) {
   VTermState *state = user;

   if (state->fallbacks && state->fallbacks->pm
          && (*state->fallbacks->pm)(frag, state->fbdata))
      return 1;

   // No DEBUG_LOG because all PMs are unhandled
   return 0;
}

static int
on_sos(VTermStringFragment frag, void *user) {
   VTermState *state = user;

   if (state->fallbacks && state->fallbacks->sos && (*state->fallbacks->sos)(frag, state->fbdata))
      return 1;

   // No DEBUG_LOG because all SOSs are unhandled
   return 0;
}

// Return true if success
private Boole
on_resize(Unt rows, Unt cols, void *user) {
   VTermState* state = user;
   VTermPos oldpos = state->pos;

   if (cols != state->cols) {
      CS newtabstops = vterm_allocator_malloc(state->vt, (cols + 7) / 8);
      if (!newtabstops)
         return false;

      // TODO: This can all be done much more efficiently bytewise
      Unt col;
      for(col = 0; col < state->cols && col < cols; col++) {
         unsigned char mask = 1 << (col & 7);
         if (state->tabstops[col >> 3] & mask)
            newtabstops[col >> 3] |= mask;
         else
           newtabstops[col >> 3] &= ~mask;
      }

      for( ; col < cols; col++) {
         unsigned char mask = 1 << (col & 7);
         if (col % 8 == 0)
            newtabstops[col >> 3] |= mask;
         else
            newtabstops[col >> 3] &= ~mask;
      }

      vterm_allocator_free(state->vt, state->tabstops);
      state->tabstops = newtabstops;
   }

   state->rows = rows;
   state->cols = cols;

   if (state->scrollregion_bottom < UNT)
      UBOUND(state->scrollregion_bottom, state->rows);
   if (state->scrollregion_right < UNT )
      UBOUND(state->scrollregion_right, state->cols);

   VTermStateFields fields;
   fields.pos = state->pos;
   fields.lineinfos[0] = state->lineinfos[0];
   fields.lineinfos[1] = state->lineinfos[1];

   if (state->callbacks && state->callbacks->resize) {
      (*state->callbacks->resize)(rows, cols, OUT &fields, state->cbdata);
      state->pos = fields.pos;

      state->lineinfos[0] = fields.lineinfos[0];
      state->lineinfos[1] = fields.lineinfos[1];
   } else {
      if (rows != state->rows) {
         for(int bufidx = BUFIDX_PRIMARY; bufidx <= BUFIDX_ALTSCREEN; bufidx++) {
            VTermLineInfo *oldlineinfo = state->lineinfos[bufidx];
            if (!oldlineinfo)
               continue;

            VTermLineInfo *newlineinfo = vterm_allocator_malloc(
                  state->vt, rows * sizeof(VTermLineInfo)
            );

            Unt row;
            for(row = 0; row < state->rows && row < rows; row++) {
               newlineinfo[row] = oldlineinfo[row];
            }

            for( ; row < rows; row++) {
               newlineinfo[row] = (VTermLineInfo){ .doublewidth = 0, };
            }

            vterm_allocator_free(state->vt, state->lineinfos[bufidx]);
            state->lineinfos[bufidx] = newlineinfo;
         }
      }
   }

   state->lineinfo = state->lineinfos[state->mode.alt_screen ? BUFIDX_ALTSCREEN : BUFIDX_PRIMARY];

   if (state->at_phantom && state->pos.col < cols-1) {
      state->at_phantom = 0;
      state->pos.col++;
   }

   if (state->pos.row >= rows)
      state->pos.row = rows - 1;
   if (state->pos.col >= cols)
      state->pos.col = cols - 1;

   updatecursor(state, &oldpos, 1);

   return true;
}

private VTermParserCallbacks const PARSER_TABLE = {
   on_text, // text
   on_control, // control
   on_escape, // escape
   on_csi, // csi
   on_osc, // osc
   on_dcs, // dcs
   on_apc, // apc
   on_pm, // pm
   on_sos, // sos
   on_resize // resize
};

//Return the existing state or create a new one. Return NULL when out of memory.
private VTermState *
vterm_obtain_state(VTerm *vt) {
   if (vt->state)
      return vt->state;

   VTermState *state = vterm_state_new(vt);
   if (state == NULL)
      return NULL;
   vt->state = state;

   vterm_parser_set_callbacks(vt, &PARSER_TABLE, state);

   return state;
}

private void
vterm_state_reset(VTermState *state, int hard) {
   state->scrollregion_top = 0;
   state->scrollregion_bottom = UNT;
   state->scrollregion_left = 0;
   state->scrollregion_right = UNT;

   state->mode.keypad          = 0;
   state->mode.cursor          = 0;
   state->mode.autowrap        = 1;
   state->mode.insert          = 0;
   state->mode.newline         = 0;
   state->mode.alt_screen      = 0;
   state->mode.origin          = 0;
   state->mode.leftrightmargin = 0;
   state->mode.bracketpaste    = 0;
   state->mode.report_focus    = 0;

   state->mouse_flags = 0;

  for(Unt col = 0; col < state->cols; col++)
     if (col % 8 == 0)
        set_col_tabstop(state, col);
     else
        clear_col_tabstop(state, col);

  for (Unt row = 0; row < state->rows; row++)
     set_lineinfo(state, row, FORCE, DWL_OFF, DHL_OFF);

  if (state->callbacks && state->callbacks->initpen)
     (*state->callbacks->initpen)(state->cbdata);

  vterm_state_resetpen(state);

  state->gl_set = 0;
  state->gr_set = 1;
  state->gsingle_set = 0;

  state->protected_cell = 0;

  // Initialise the props
  settermprop_bool(state, VTERM_PROP_CURSORVISIBLE, 1);
  settermprop_bool(state, VTERM_PROP_CURSORBLINK,   1);
  settermprop_int (state, VTERM_PROP_CURSORSHAPE,   VTERM_PROP_CURSORSHAPE_BLOCK);

  if (hard) {
    state->pos.row = 0;
    state->pos.col = 0;
    state->at_phantom = 0;

    VTermRect rect = { 0, 0, 0, 0 };
    rect.end_row = state->rows;
    rect.end_col =  state->cols;
    stateErase(state, rect, 0);
  }
}

private void 
vterm_state_get_cursorpos(const VTermState *state, VTermPos *cursorpos) {
   *cursorpos = state->pos;
}

private void
vterm_state_get_mousestate(const VTermState *state, VTermMouseState *mousestate) {
   mousestate->pos.col = state->mouse_col;
   mousestate->pos.row = state->mouse_row;
   mousestate->buttons = state->mouse_buttons;
   mousestate->flags = state->mouse_flags;
}

private void
vterm_state_set_callbacks(VTermState *state, const VTermStateCallbacks *callbacks, void *user) {
   if (callbacks) {
      state->callbacks = callbacks;
      state->cbdata = user;

      if (state->callbacks && state->callbacks->initpen)
         (*state->callbacks->initpen)(state->cbdata);
   } else {
      state->callbacks = NULL;
      state->cbdata = NULL;
   }
}


private void
vterm_state_set_unrecognized_fallbacks(
      VTermState *state, const VTermStateFallbacks *fallbacks, void *user
) {
   if (fallbacks) {
      state->fallbacks = fallbacks;
      state->fbdata = user;
   } else {
      state->fallbacks = NULL;
      state->fbdata = NULL;
   }
}

private int
vterm_state_set_termprop(VTermState *state, VTermProp prop, VTermValue *val) {
   //Only store the new value of the property if usercode said it was happy.
   //This is especially important for altscreen switching */
   if (state->callbacks && state->callbacks->settermprop
        && !(*state->callbacks->settermprop)(prop, val, state->cbdata))
      return 0;

   switch(prop) {
   case VTERM_PROP_TITLE:
   case VTERM_PROP_ICONNAME:
   case VTERM_PROP_CURSORCOLOR:
      // we don't store these, just transparently pass through
      return 1;
   case VTERM_PROP_CURSORVISIBLE:
      state->mode.cursor_visible = val->boolean;
      return 1;
   case VTERM_PROP_CURSORBLINK:
      state->mode.cursor_blink = val->boolean;
      return 1;
   case VTERM_PROP_CURSORSHAPE:
      state->mode.cursor_shape = val->number;
      return 1;
   case VTERM_PROP_REVERSE:
      state->mode.screen = val->boolean;
      return 1;
   case VTERM_PROP_ALTSCREEN:
      state->mode.alt_screen = val->boolean;
      state->lineinfo = state->lineinfos[state->mode.alt_screen ? BUFIDX_ALTSCREEN : BUFIDX_PRIMARY];
      if (state->mode.alt_screen) {
         VTermRect rect = {0, 0, 0, 0};
         rect.end_row = state->rows;
         rect.end_col = state->cols;
         stateErase(state, rect, 0);
      }
      return 1;
   case VTERM_PROP_MOUSE:
      state->mouse_flags = 0;
      if (val->number)
         state->mouse_flags |= MOUSE_WANT_CLICK;
      if (val->number == VTERM_PROP_MOUSE_DRAG)
         state->mouse_flags |= MOUSE_WANT_DRAG;
      if (val->number == VTERM_PROP_MOUSE_MOVE)
         state->mouse_flags |= MOUSE_WANT_MOVE;
      return 1;
   case VTERM_PROP_FOCUSREPORT:
      state->mode.report_focus = val->boolean;
      return 1;

   case VTERM_N_PROPS:
      return 0;
   }

   return 0;
}

private void
vterm_state_focus_in(VTermState *state) {
   if (state->mode.report_focus)
      vterm_push_output_sprintf_ctrl(state->vt, C1_CSI, "I");
}

private void
vterm_state_focus_out(VTermState *state) {
   if (state->mode.report_focus)
      vterm_push_output_sprintf_ctrl(state->vt, C1_CSI, "O");
}

private const VTermLineInfo *
vterm_state_get_lineinfo(const VTermState *state, int row) {
   return state->lineinfo + row;
}

//private void 
//vterm_state_set_selection_callbacks(
//   VTermState *state, const VTermSelectionCallbacks *callbacks, void *user,
//   char *buffer, Unt buflen
//) {
//  if (buflen && !buffer)
//    buffer = vterm_allocator_malloc(state->vt, buflen);
//
//  state->selection.callbacks = callbacks;
//  state->selection.user      = user;
//  state->selection.buffer    = buffer;
//  state->selection.buflen    = buflen;
//}

//private void
//vterm_state_send_selection(VTermState *state, VTermSelectionMask mask, VTermStringFragment frag) {
//  VTerm *vt = state->vt;
//
//  if (frag.initial) {
//    // TODO: support sending more than one mask bit
//    static char selection_chars[] = "cpqs";
//    int idx;
//    for(idx = 0; idx < 4; idx++)
//      if (mask & (1 << idx))
//        break;
//
//    vterm_push_output_sprintf_str(vt, C1_OSC, FALSE, "52;%c;", selection_chars[idx]);
//
//    state->tmp.selection.sendpartial = 0;
//  }
//
//   if (frag.len) {
//      Unt bufcur = 0;
//      char *buffer = state->selection.buffer;
//
//      uint32_t x = 0;
//      int n = 0;
//
//      if (state->tmp.selection.sendpartial) {
//         n = state->tmp.selection.sendpartial >> 24;
//         x = state->tmp.selection.sendpartial & 0xFFFFFF;
//
//         state->tmp.selection.sendpartial = 0;
//      }
//
//      while((state->selection.buflen - bufcur) >= 4 && frag.len) {
//         x = (x << 8) | frag.str[0];
//         n++;
//         frag.str++, frag.len--;
//
//         if (n == 3) {
//            buffer[0] = base64_one((x >> 18) & 0x3F);
//            buffer[1] = base64_one((x >> 12) & 0x3F);
//            buffer[2] = base64_one((x >>  6) & 0x3F);
//            buffer[3] = base64_one((x >>  0) & 0x3F);
//
//            buffer += 4, bufcur += 4;
//            x = 0;
//            n = 0;
//         }
//
//         if (!frag.len || (state->selection.buflen - bufcur) < 4) {
//            if (bufcur)
//               vterm_push_output_bytes(vt, state->selection.buffer, bufcur);
//
//            buffer = state->selection.buffer;
//            bufcur = 0;
//         }
//      }
//
//      if (n)
//         state->tmp.selection.sendpartial = (n << 24) | x;
//   }
//
//   if (frag.final) {
//      if (state->tmp.selection.sendpartial) {
//         int n      = state->tmp.selection.sendpartial >> 24;
//         uint32_t x = state->tmp.selection.sendpartial & 0xFFFFFF;
//         char *buffer = state->selection.buffer;
//  
//         // n is either 1 or 2 now
//         x <<= (n == 1) ? 16 : 8;
//  
//         buffer[0] = base64_one((x >> 18) & 0x3F);
//         buffer[1] = base64_one((x >> 12) & 0x3F);
//         buffer[2] = (n == 1) ? '=' : base64_one((x >>  6) & 0x3F);
//         buffer[3] = '=';
//  
//         vterm_push_output_sprintf_str(vt, 0, TRUE, "%.*s", 4, buffer);
//     } else
//         vterm_push_output_sprintf_str(vt, 0, TRUE, "");
//   }
//}

//}}}
//}}}
//{{{auxiliary functions

// flags for term_start()
#define TERM_START_NOJOB   1
#define TERM_START_FORCEIT 2
#define TERM_START_SYSTEM  4

// This is VTermScreenCell without the characters, thus much smaller.
typedef struct {
   VTermScreenCellAttrs flags;
   char width;
   VTermColor fg;
   VTermColor bg;
} CellDeco;

typedef struct sb_line_S {
   Unt cols;   // can differ per line
   Arr(CellDeco) sb_cells;   // allocated
   CellDeco sb_fillDeco;   // for short line
   CS sb_text;   // for scrollbackPostponed
} ScrollbackLine;

// typedef Terminal in eegl.h@@structs
struct Terminal {
   Terminal* next;

   VTerm* vterm;
   Job* job;
   Book* book;

   // Set when setting the size of a vterm, reset after redrawing.
   int vterm_size_changed;

   int isNormalMode; // TRUE: Terminal-Normal mode
   int isChannelClosing;
   int isChannelClosed;
   int isChannelRecentlyClosed; // still need to handle tl_finish

   int tl_finish;
#define TL_FINISH_UNSET   ZERO
#define TL_FINISH_CLOSE     'c'   // ++close or :terminal without argument
#define TL_FINISH_NOCLOSE   'n'   // ++noclose
#define TL_FINISH_OPEN      'o'   // ++open
   CS openComm;
   CS tl_eof_chars;
   CS tl_api;   // prefix for terminal API function

   CS tl_arg0_cmd;   // To format the status bar

   CS command;
   CS tl_kill;

   // last known vterm size
   Unt rows;
   Unt cols;

   CS title; // NULL or allocated
   CS tl_status_text; // NULL or allocated

   // Range of screen rows to update.  Zero based.
   int dirtyRowStart; // MAX_ROW if nothing dirty
   int dirtyRowEnd;   // row below last one to update
   Boole dirtySnapshot;  // text updated after making snapshot
   Boole timerSet;
   ProfTime timerDue;
   Unt postponedScroll;   // to be scrolled up

   ArrayList scrollback;
   int scrollbackScrolled;
   ArrayList scrollbackPostponed;

   CS hiliteName; // replaces "Terminal"; allocated

   CellDeco cellDeco;

   LineNr topDiffRows;   // rows of top diff file or zero
   LineNr bottDiffRows;   // rows of bottom diff file

   VTermPos   cursorPos;
   int tl_cursor_visible;
   int tl_cursor_blink;
   int tl_cursor_shape;  // 1: block, 2: underline, 3: bar
   Byte* tl_cursor_color; // NULL or allocated

   Ulong* palette; // array of 16 colors specified by term_start, can be NULL
   int tl_using_altscreen;
   ArrayList oscBuilder;       // incomplete OSC string
};

#define TMODE_ONCE 1       // CTRL-\ CTRL-N used
#define TMODE_LOOP 2       // CTRL-W N used

// List of all active terminals.
private Terminal *first_term = NULL;

// Terminal active in terminal_loop().
private Terminal *in_terminal_loop = NULL;

#define MAX_ROW 999999       // used for dirtyRowEnd to update all rows
#define KEY_BUF_LEN 200

#define FOR_ALL_TERMS(term)   \
    for ((term) = first_term; (term) != NULL; (term) = (term)->next)

private int term_and_job_init(
      Terminal *term, Var *argvar, Byte **argv, JobOptions *opt, JobOptions *orig_opt
);
private int create_pty_only(Terminal *term, JobOptions *opt);
private void term_report_winsize(Terminal *term, int rows, int cols);
private void term_free_vterm(Terminal *term);

private void handle_postponed_scrollback(Terminal *term);

// The character that we know (or assume) that the terminal expects for the
// backspace key.
private int term_backspace_char = BS;

// Store the last set and the desired cursor properties, so that we only update
// them when needed.  Doing it unnecessary may result in flicker.
private CS last_set_cursor_color = NULL;
private CS desired_cursor_color = NULL;
private int   last_set_cursor_shape = -1;
private int   desired_cursor_shape = -1;
private int   last_set_cursor_blink = -1;
private int   desired_cursor_blink = -1;

private int
cursor_color_equal(CS lhs_color, CS rhs_color) {
   if (lhs_color && rhs_color)
      return STRCMP(lhs_color, rhs_color) == 0;
   return !lhs_color && !rhs_color;
}

private void
cursor_color_copy(OUT CS* to_color, CS from_color) {
   // Avoid a free & alloc if the value is already right.
   if (cursor_color_equal(*to_color, from_color))
      return;
   eeglFree(*to_color);
   *to_color = (from_color == NULL) ? NULL : copyStr(from_color);
}

private CS
cursor_color_get(CS color) {
   return color ? color : S"";
}


// Parse 'termwinsize' and set "rows" and "cols" for the terminal size in the current portal.
// Set "rows" and/or "cols" to 0 when it should follow the portal size.
// Return TRUE if the size is the minimum size: "24*80".
private Boole
parse_termwinsize(Portal *po, OUT Unt* rows, OUT Unt* cols) {

   *rows = 0;
   *cols = 0;
   if (*po->o.termWinSize == ZERO)
      return false;

   CS p = firstOccurrence(po->o.termWinSize, 'x');

   // Syntax of value was already checked when it's set.
   Boole minsize = false;
   if (!p) {
      minsize = true;
      p = firstOccurrence(po->o.termWinSize, '*');
   }
   *rows = atoi((char *)po->o.termWinSize);
   *cols = atoi((char *)p + 1);
   if (*rows > VTERM_MAX_ROWS)
      *rows = VTERM_MAX_ROWS;
   if (*cols > VTERM_MAX_COLS)
      *cols = VTERM_MAX_COLS;
   return minsize;
}

// Determine the terminal size from 'termwinsize' and the current portal.
private void
set_term_and_win_size(Terminal *term, JobOptions *opt) {

   term->rows = curPor->height;
   term->cols = curPor->width;

   Unt rows, cols;
   Boole minsize = parse_termwinsize(curPor, &rows, &cols);
   if (minsize) {
      if (term->rows < rows)
         term->rows = rows;
      if (term->cols < cols)
         term->cols = cols;
   }
   if ((opt->set1 & JO2_TERM_ROWS))
      term->rows = opt->jo_term_rows;
   ei (rows != 0)
      term->rows = rows;
   if ((opt->set1 & JO2_TERM_COLS))
   term->cols = opt->jo_term_cols;
    ei (cols != 0)
   term->cols = cols;

   if (!opt->jo_hidden) {
      if (term->rows != (Unt)curPor->height)
         portSetHeight(term->rows, curPor);
      if (term->cols != (Unt)curPor->width)
         portSetWidth(term->cols, curPor);

      //Set 'winsize' now to avoid a resize at the next redraw.
      if (!minsize && curPor->o.termWinSize) {
         Byte buf[100];

         eeSnprintf(buf, 100, "%dx%d", term->rows, term->cols);
         optChangeAndReportError(S"termwinsize", optStr(buf), SET_LOCAL);
      }
   }
}

// Initialize job options for a terminal job. Caller may overrule some of them.
void
init_job_options(JobOptions *opt) {
   CLEAR_POINTER(opt);

   opt->mode = CH_MODE_RAW;
   opt->jo_out_mode = CH_MODE_RAW;
   opt->jo_err_mode = CH_MODE_RAW;
   opt->set = JO_MODE | JO_OUT_MODE | JO_ERR_MODE;
}

// Set job options mandatory for a terminal job.
private void
setup_job_options(JobOptions *opt, int rows, int cols) {
   if (!(opt->set & JO_OUT_IO)) {
      // Connect stdout to the terminal.
      opt->ioMode[PART_OUT] = JIO_BUFFER;
      opt->ioText[PART_OUT] = curBook->fiNum;
      opt->jo_modifiable[PART_OUT] = 0;
      opt->set |= JO_OUT_IO + JO_OUT_BUF + JO_OUT_MODIFIABLE;
   }

   if (!(opt->set & JO_ERR_IO)) {
      // Connect stderr to the terminal.
      opt->ioMode[PART_ERR] = JIO_BUFFER;
      opt->ioText[PART_ERR] = curBook->fiNum;
      opt->jo_modifiable[PART_ERR] = 0;
      opt->set |= JO_ERR_IO + JO_ERR_BUF + JO_ERR_MODIFIABLE;
   }

   opt->jo_pty = TRUE;
   if ((opt->set1 & JO2_TERM_ROWS) == 0)
      opt->jo_term_rows = rows;
   if ((opt->set1 & JO2_TERM_COLS) == 0)
      opt->jo_term_cols = cols;
}

//Check for any pending input or messages.
private int
mch_check_messages(void) {
   return waitForChar(0L, NULL, TRUE);
}


// Flush messages on channels.
private void
term_flush_messages(void) {
   mch_check_messages();
   parse_queued_messages();
}

// Close a terminal book (and its portal). Used when creating the terminal fails.
private void
closeFailedTerminalBook(Book* book, Book* old_curBook) {
   free_terminal(book);
   if (old_curBook) {
      --curBook->countPortals;
      curBook = old_curBook;
      curPor->book = curBook;
      ++curBook->countPortals;
   }
   CHECK_CURBOOK;

   // Wiping out the book will also close the portal and call free_terminal().
   bookDo(DOBOOK_WIPE, DOBOOK_FIRST, FORWARD, book->fiNum, DOBOOK_FORCEIT);
}

//Start a terminal portal and return its book. Use either "argvar" or "argv", the other must be 
//NULL. When "flags" has TERM_START_NOJOB only create the buffer, term and open the portal.
//Return NULL when failed.
Book*
term_start(Var* argvar, Byte** argv, JobOptions* opt, Unt flags){
   Invocation splitInvo;
   Portal* old_curPor = curPor;
   Book* curBookSaved = NULL; 
   Book* newBook;
   int vertical = opt->vertical || (commModifierG.cmod_split & WSP_VERT);
   JobOptions orig_opt;  // only partly filled
   Pos save_cursor;

   if (commPortTypeG != 0) {
      emsg(_(e_cannot_open_terminal_from_command_line_window));
      return NULL;
   }

   if ((opt->set & (JO_IN_IO + JO_OUT_IO + JO_ERR_IO)) == (JO_IN_IO + JO_OUT_IO + JO_ERR_IO)
         || (!(opt->set & JO_OUT_IO) && (opt->set & JO_OUT_BUF))
         || (!(opt->set & JO_ERR_IO) && (opt->set & JO_ERR_BUF))
         || (argvar != NULL
             && argvar->tag == VAR_LIST
             && argvar->list != NULL
             && argvar->list->first == &range_list_item)
   ) {
      emsg(_(e_invalid_argument));
      return NULL;
   }

   Terminal* term = ALLOC_CLEAR_ONE(Terminal);
   term->dirtyRowEnd = MAX_ROW;
   term->tl_cursor_visible = TRUE;
   term->tl_cursor_shape = VTERM_PROP_CURSORSHAPE_BLOCK;
   term->tl_finish = opt->jo_term_finish;
   ga_init2(&term->scrollback, sizeof(ScrollbackLine), 300);
   ga_init2(&term->scrollbackPostponed, sizeof(ScrollbackLine), 300);
   ga_init2(&term->oscBuilder, sizeof(char), 300);

   setpcmark();
   CLEAR_FIELD(splitInvo);
   if (opt->curPor) {
      // Create a new buffer in the current portal.
      if (startEditingFile(
              0, NULL, NULL, OUT &splitInvo, ECMD_ONE,
              ECMD_HIDE + ((flags & TERM_START_FORCEIT) ? ECMD_FORCEIT : 0), curPor
          ) == FAIL) {
         eeglFree(term);
         return NULL;
      }
    } ei (opt->jo_hidden || (flags & TERM_START_SYSTEM)) {
      //Create a new buffer without a portal. Make it the current buffer for
      //a moment to be able to do the initializations.
      Book* book = bookNew(Em, NULL, (LineNr)0, BLN_NEW | BLN_LISTED);
      if (!book || ml_open(book) == FAIL) {
          eeglFree(term);
          return NULL;
      }
      curBookSaved = curBook;
      --curBook->countPortals;
      curBook = book;
      save_cursor = curPor->cursor;
      curPor->book = book;
      ++curBook->countPortals;
   } else {
      //Open a new portal or tab.
      splitInvo.id = C_new;
      splitInvo.comm = S"new";
      splitInvo.arg = Em;
      if (opt->jo_term_rows > 0 && !vertical) {
         splitInvo.line2 = opt->jo_term_rows;
         splitInvo.addr_count = 1;
      }
      if (opt->jo_term_cols > 0 && vertical) {
         splitInvo.line2 = opt->jo_term_cols;
         splitInvo.addr_count = 1;
      }

      int cmod_split_modified = FALSE;
      if (vertical) {
         if (!(commModifierG.cmod_split & WSP_VERT))
            cmod_split_modified = TRUE;
         commModifierG.cmod_split |= WSP_VERT;
      }
      c_splitview(&splitInvo);
      if (cmod_split_modified)
         commModifierG.cmod_split &= ~WSP_VERT;
      if (curPor == old_curPor) {
         // split failed
         eeglFree(term);
         return NULL;
      }
   }
   term->book = curBook;
   curBook->term = term;

   if (!opt->jo_hidden) {
      // Only one size was taken care of with :new, do the other one. With "curPor" both needed
      if (opt->jo_term_rows > 0 && (opt->curPor || vertical))
          portSetHeight(opt->jo_term_rows, curPor);
      if (opt->jo_term_cols > 0 && (opt->curPor || !vertical))
          portSetWidth(opt->jo_term_cols, curPor);
   }

   // Link the new terminal in the list of active terminals.
   term->next = first_term;
   first_term = term;

   applyAutocomms(EVENT_BUFFILEPRE, NULL, NULL, false, curBook);

   if (opt->jo_term_name != NULL) {
      eeglFree(curBook->fullFileName);
      curBook->fullFileName = copyStr(opt->jo_term_name);
   } ei (argv) {
      eeglFree(curBook->fullFileName);
      curBook->fullFileName = copyStr(S"!system");
   } else {
      int i;
      Unt len;
      CS cmd;

      if (argvar->tag == VAR_STRING) {
         cmd = argvar->string;
         if (!cmd)
            cmd = S"";
         ei (STRCMP(cmd, "NONE") == 0)
            cmd = S"pty";
      } ei (argvar->tag != VAR_LIST
            || argvar->list == NULL
            || argvar->list->len == 0
            || (cmd = convertVarToStringSingleUse( &argvar->list->first->c)) == NULL)
         cmd = Em;

      len = STRLEN(cmd) + 10;
      CS p = alloc(len);

      for (i = 0; p; ++i) {
         //Prepend a ! to the command name to avoid the buffer name equals
         //the executable, otherwise ":w!" would overwrite it.
         if (i == 0)
            eeSnprintf(p, len, "!%s", cmd);
         else
            eeSnprintf(p, len, "!%s (%d)", cmd, i);
         if (booklistFindName(p) == NULL) {
            eeglFree(curBook->fullFileName);
            curBook->fullFileName = p;
            break;
         }
      }
   }
   eeglFree(curBook->shortFileName);
   curBook->shortFileName = copyStr(curBook->fullFileName);
   curBook->currFileName = curBook->fullFileName;

   applyAutocomms(EVENT_BUFFILEPOST, NULL, NULL, false, curBook);

   if (opt->jo_term_opencmd)
      term->openComm = copyStr(opt->jo_term_opencmd);

   if (opt->jo_eof_chars)
      term->tl_eof_chars = copyStr(opt->jo_eof_chars);

   optSetByName(S"booktype", optEnum(BOOK_TERMINAL), SET_LOCAL);
   //Avoid that @buftype is reset when this buffer is entered.
   curBook->o.initialized = true;

   //Mark the buffer as immutable. It can only be made modifiable after the job finished.
   curBook->o.modifiable = false;

   set_term_and_win_size(term, opt);
   setup_job_options(opt, term->rows, term->cols);

   if (flags & TERM_START_NOJOB)
      return curBook;

   // Remember the command for the session file.
   if (opt->jo_term_norestore || argv != NULL)
      term->command = copyStr(S"NONE");
   ei (argvar->tag == VAR_STRING) {
      CS cmd = argvar->string;

      if (cmd && STRCMP(cmd, "bash") != 0)
         term->command = copyStr(cmd);
   } ei (argvar->tag == VAR_LIST && argvar->list != NULL && argvar->list->len > 0) {
      ArrayList   ga;
      ListItem   *item;

      ga_init2(&ga, 1, 100);
      FOR_ALL_LIST_ITEMS(argvar->list, item) {
         CS s = convertVarToStringSingleUse(&item->c);
         if (!s)
            break;
         CS p = copyStr_fnameescape(s, VSE_NONE);
         ga_concat(&ga, p);
         eeglFree(p);
         ga_append(&ga, ' ');
      }
      if (item == NULL) {
         ga_append(&ga, ZERO);
         term->command = ga.c;
      } else
         ga_clear(&ga);
   }

   if (opt->jo_term_kill) {
      CS p = skiptowhite(opt->jo_term_kill);
      term->tl_kill = copySubstr(opt->jo_term_kill, p - opt->jo_term_kill);
   }

   if (opt->jo_term_api) {
      CS p = skiptowhite(opt->jo_term_api);
      term->tl_api = copySubstr(opt->jo_term_api, p - opt->jo_term_api);
   } else
      term->tl_api = copyStr(S"Tapi_");

   if (opt->set1 & JO2_TERM_HIGHLIGHT)
      term->hiliteName = copyStr(opt->jo_term_highlight);

   // Save the user-defined palette, it is only used in GUI (or 'tgc' is on).
   if (opt->set1 & JO2_ANSI_COLORS) {
      term->palette = ALLOC_MULT(Ulong, 16);
      memcpy(term->palette, opt->jo_ansi_colors, sizeof(Ulong) * 16);
   }

   // System dependent: setup the vterm and maybe start the job in it.
   int res;
   if (!argv && argvar->tag == VAR_STRING && argvar->string && STRCMP(argvar->string, "NONE") == 0)
      res = create_pty_only(term, opt);
   else
      res = term_and_job_init(term, argvar, argv, opt, &orig_opt);

   newBook = curBook;
   if (res == OK) {
      // Get and remember the size we ended up with.  Update the pty.
      vterm_get_size(term->vterm, &term->rows, &term->cols);
      term_report_winsize(term, term->rows, term->cols);

      //Make sure we don't get stuck on sending keys to the job, it leads to
      //a deadlock if the job is waiting for Eegl to read.
      channel_set_nonblock(term->job->jv_channel, PART_IN);

      if (curBookSaved) {
         --curBook->countPortals;
         curBook = curBookSaved;
         curPor->book = curBook;
         curPor->cursor = save_cursor;
         ++curBook->countPortals;
      } ei (vgetcBusyG || timer_busy || input_busy) {
         // When waiting for input need to return and possibly end up in terminal_loop() instead
         Byte ignore[4] = {K_SPECIAL, KS_EXTRA, KE_IGNORE, ZERO};
         insertIntoTypebuf(ignore, REMAP_NONE, 0, TRUE, FALSE);
         typebuf_was_filled = TRUE;
      }
   } else {
      closeFailedTerminalBook(curBook, curBookSaved);
      return NULL;
   }

   applyAutocomms(EVENT_TERMINALOPEN, NULL, NULL, false, newBook);
   if (!opt->jo_hidden && !(flags & TERM_START_SYSTEM))
      applyAutocomms(EVENT_TERMINALWINOPEN, NULL, NULL, false, newBook);
   return newBook;
}

// ":terminal": open a terminal portal and execute a job in it.
void
c_terminal(Invocation* invo) {
   Var argvar[2];
   JobOptions opt;
   Boole opt_shell = false;

   init_job_options(&opt);

   CS cmd = invo->arg;
   while (*cmd == '+' && *(cmd + 1) == '+') {
      cmd += 2;
      CS p = skiptowhite(cmd);
      CS ep = firstOccurrence(cmd, '=');
      if (ep) {
         if (ep < p)
            p = ep;
         else
            ep = NULL;
      }

// Note: Keep this in sync with get_terminaloname.

# define OPTARG_HAS(name) ((int)(p - cmd) == sizeof(name) - 1 \
                && STRNICMP(cmd, name, sizeof(name) - 1) == 0)
                
      if (OPTARG_HAS("close"))
         opt.jo_term_finish = 'c';
      ei (OPTARG_HAS("noclose"))
         opt.jo_term_finish = 'n';
      ei (OPTARG_HAS("open"))
         opt.jo_term_finish = 'o';
      ei (OPTARG_HAS("curPor"))
         opt.curPor = 1;
      ei (OPTARG_HAS("hidden"))
         opt.jo_hidden = 1;
      ei (OPTARG_HAS("norestore"))
         opt.jo_term_norestore = 1;
      ei (OPTARG_HAS("shell"))
         opt_shell = true;
      ei (OPTARG_HAS("kill") && ep != NULL) {
         opt.set1 |= JO2_TERM_KILL;
         opt.jo_term_kill = ep + 1;
         p = skiptowhite(cmd);
      } ei (OPTARG_HAS("api")) {
         opt.set1 |= JO2_TERM_API;
         if (ep) {
            opt.jo_term_api = ep + 1;
            p = skiptowhite(cmd);
         } else
            opt.jo_term_api = NULL;
      } ei (OPTARG_HAS("rows") && ep != NULL && SAFE_isdigit(ep[1])) {
         opt.set1 |= JO2_TERM_ROWS;
         opt.jo_term_rows = atoi((char *)ep + 1);
         p = skiptowhite(cmd);
      } ei (OPTARG_HAS("cols") && ep != NULL && SAFE_isdigit(ep[1])) {
         opt.set1 |= JO2_TERM_COLS;
         opt.jo_term_cols = atoi((char *)ep + 1);
         p = skiptowhite(cmd);
      } ei (OPTARG_HAS("eof") && ep != NULL) {
         CS buf = NULL;
         CS keys;

         eeglFree(opt.jo_eof_chars);
         p = skiptowhite(cmd);
         *p = ZERO;
         keys = replace_termcodes(
            ep + 1, &buf, 0, REPTERM_FROM_PART | REPTERM_DO_LT | REPTERM_SPECIAL, NULL, false
         );
         opt.set1 |= JO2_EOF_CHARS;
         opt.jo_eof_chars = copyStr(keys);
         eeglFree(buf);
         *p = ' ';
      } else {
         if (*p)
            *p = ZERO;
         showErrFmtMsg(_(e_invalidDecorationStr), cmd);
         goto theend;
      }
# undef OPTARG_HAS
      cmd = skipwhite(p);
   }
   if (*cmd == ZERO) {
      // default to close when the shell exits
      if (opt.jo_term_finish == ZERO)
         opt.jo_term_finish = TL_FINISH_CLOSE;
   }

   if (invo->addr_count > 0) {
      // Write lines from current buffer to the job.
      opt.set |= JO_IN_IO | JO_IN_BUF | JO_IN_TOP | JO_IN_BOT;
      opt.ioMode[PART_IN] = JIO_BUFFER;
      opt.ioText[PART_IN] = curBook->fiNum;
      opt.jo_in_top = invo->line1;
      opt.jo_in_bot = invo->line2;
   }

   if (opt_shell) {
      Byte** argv = NULL;
      CS tofree2 = NULL;

      // :term ++shell command
      if (unix_build_argv(cmd, &argv, null, &tofree2) == OK)
         term_start(NULL, argv, &opt, invo->forceit ? TERM_START_FORCEIT : 0);
      eeglFree(argv);
      eeglFree(tofree2);
      goto theend;
   }
   argvar[0].tag = VAR_STRING;
   argvar[0].string = cmd;
   argvar[1].tag = VAR_UNKNOWN;
   term_start(argvar, NULL, &opt, invo->forceit ? TERM_START_FORCEIT : 0);

theend:
   eeglFree(opt.jo_eof_chars);
}

private CS
get_terminaloname(Expand* xp UNUSED, int idx) {
   // Note: Keep this in sync with c_terminal.
   static char *(p_termopt_values[]) = {
      "close",
      "noclose",
      "open",
      "curPor",
      "hidden",
      "norestore",
      "shell",
      "kill=",
      "rows=",
      "cols=",
      "eof=",
      "type=",
      "api=",
   };

   if (idx < (int)ARRAY_LENGTH(p_termopt_values))
      return (CS)p_termopt_values[idx];
   return NULL;
}

private CS
get_termkill_name(Expand *xp UNUSED, int idx) {
   // These are platform-specific values used for job_stop(). They are defined
   // in each platform's mch_signal_job(). Just use a unified auto-complete list for simplicity.
   static char *(p_termkill_values[]) = {
      "term",
      "hup",
      "quit",
      "int",
      "kill",
      "winch",
   };

   if (idx < (int)ARRAY_LENGTH(p_termkill_values))
      return (Byte*)p_termkill_values[idx];
   return NULL;
}

// Command-line expansion for :terminal [options]
int
expand_terminal_opt(CS pat, Expand* xp, RegMatch* rmp, OUT ExpandMatch* matches) {
   if (xp->input.c > xp->fullInput && *(xp->input.c - 1) == '=') {
      Byte *(*cb)(Expand *, int) = NULL;

      CS name_end = xp->input.c - 1;
      if (name_end - xp->fullInput >= 4 && STRNCMP(name_end - 4, "kill", 4) == 0)
          cb = get_termkill_name;

      if (cb) {
         return expandGeneric(
             pat,
             xp,
             rmp,
             cb,
             FALSE,
             OUT matches
         );
      }
      return FAIL;
   }
   return expandGeneric(
       pat,
       xp,
       rmp,
       get_terminaloname,
       FALSE,
       OUT matches
   );
}

//Write a :terminal command to the session file to restore the terminal in portal "po".
//Return FAIL if writing fails.
int
term_write_session(FILE* fd, Portal* po, EeSet* terminal_bufs){
   const int   bufnr = po->book->fiNum;
   Terminal   *term = po->book->term;

   if (terminal_bufs && po->book->countPortals > 1) {
      //There are multiple views into this terminal buffer. We don't want to create the terminal 
      //multiple times. If it's the first time, create, otherwise link to the first buffer.
      Byte id_as_str[NUMBUFLEN];

      eeSnprintf(id_as_str, sizeof(id_as_str), "%d", bufnr);

      EeSetItem* entry = hash_find(terminal_bufs, mbText(id_as_str));
      if (!HASHITEM_EMPTY(entry)) {
         // we've already opened this terminal buffer
         if (fprintf(fd, "execute 'buffer ' . s:term_buf_%d", bufnr) < 0)
            return FAIL;
         return put_eol(fd);
      }
   }

   // Create the terminal and run the command. This is not without
   // risk, but let's assume the user only creates a session when this will be OK.
   if (fprintf(fd, "terminal ++curPor ++cols=%d ++rows=%d ", term->cols, term->rows) < 0)
      return FAIL;
   if (term->command != NULL && fputs((char *)term->command, fd) < 0)
      return FAIL;
   if (put_eol(fd) != OK)
      return FAIL;

   if (fprintf(fd, "let s:term_buf_%d = bufnr()", bufnr) < 0)
      return FAIL;

   if (terminal_bufs != NULL && po->book->countPortals > 1) {
      CS hash_key = alloc(NUMBUFLEN);

      eeSnprintf(hash_key, NUMBUFLEN, "%d", bufnr);
      hash_add(terminal_bufs, text(hash_key), (CS)"terminal session");
   }

   return put_eol(fd);
}

// Return TRUE if "buf" has a terminal that should be restored.
int
term_should_restore(Book* book) {
   Terminal* term = book->term;
   return term && (term->command == NULL || STRCMP(term->command, "NONE") != 0);
}

// Free the scrollback buffer for "term".
private void
free_scrollback(Terminal* term) {
   int i;

   for (i = 0; i < term->scrollback.len; ++i)
      eeglFree(((ScrollbackLine *)term->scrollback.c + i)->sb_cells);
   ga_clear(&term->scrollback);
   for (i = 0; i < term->scrollbackPostponed.len; ++i)
      eeglFree(((ScrollbackLine *)term->scrollbackPostponed.c + i)->sb_cells);
   ga_clear(&term->scrollbackPostponed);
}


// Terminals that need to be freed soon.
private Terminal* terminals_to_free = NULL;

// Free a terminal and everything it refers to. Kills the job if there is one.
// Called when wiping out a buffer. The actual terminal structure is freed later in 
// free_unused_terminals(), because callbacks may wipe out a buffer while the terminal is still
// referenced.
void
free_terminal(Book* book) {
   Terminal* term = book->term;
   if (!term)
      return;

   // Unlink the terminal form the list of terminals.
   if (first_term == term)
      first_term = term->next;
   else {
      for (Terminal* tp = first_term; tp->next != NULL; tp = tp->next) {
         if (tp->next == term) {
            tp->next = term->next;
            break;
         }
      } 
   }

   if (term->job) {
      if (term->job->jv_status != JOB_ENDED
            && term->job->jv_status != JOB_FINISHED
            && term->job->jv_status != JOB_FAILED)
         job_stop(term->job, NULL, S"kill");
      job_unref(term->job);
   }
   term->next = terminals_to_free;
   terminals_to_free = term;

   book->term = NULL;
   if (in_terminal_loop == term)
      in_terminal_loop = NULL;
}

void
free_unused_terminals(void) {
   while (terminals_to_free) {
      Terminal* term = terminals_to_free;

      terminals_to_free = term->next;

      free_scrollback(term);
      ga_clear(&term->oscBuilder);

      term_free_vterm(term);
      eeglFree(term->tl_api);
      eeglFree(term->title);
      eeglFree(term->command);
      eeglFree(term->tl_kill);
      eeglFree(term->tl_status_text);
      eeglFree(term->openComm);
      eeglFree(term->tl_eof_chars);
      eeglFree(term->tl_arg0_cmd);
      eeglFree(term->hiliteName);
      eeglFree(term->tl_cursor_color);
      eeglFree(term->palette);
      eeglFree(term);
   }
}

// Get the part that is connected to the tty. Normally this is PART_IN, but
// when writing buffer lines to the job it can be another. This makes it possible to do 
// "1,5term vim -".
private ChannelFdKind
get_tty_part(Terminal *term UNUSED) {
   ChannelFdKind   parts[3] = {PART_IN, PART_OUT, PART_ERR};

   for (int i = 0; i < 3; ++i) {
      int fd = term->job->jv_channel->fds[parts[i]].ch_fd;

      if (mch_isatty(fd))
         return parts[i];
   }
   return PART_IN;
}

// Read any vterm output and send it on the channel.
private void
term_forward_output(Terminal *term) {
   VTerm* vterm = term->vterm;
   Byte buf[KEY_BUF_LEN];
   Unt curlen = vterm_output_read(vterm, buf, KEY_BUF_LEN);

   if (curlen > 0)
      channel_send(term->job->jv_channel, get_tty_part(term), buf, (int)curlen, NULL);
}

// Write job output "msg[len]" to the vterm.
private void
term_write_job_output(Terminal* term, CS msg_arg, Unt len_arg) {
   CS msg = msg_arg;
   Unt len = len_arg;
   VTerm* vterm = term->vterm;
   Unt prevlen = vterm_output_get_buffer_current(vterm);
   Unt limit = p_twsl * term->cols * 3;

   // Limit the length to @termwinscroll * cols * 3 bytes.  Keep the text at the end.
   if (len > limit) {
      CS p = msg + len - limit;

      p -= mb_head_off(msg, p);
      len -= p - msg;
      msg = p;
   }

   vterm_input_write(vterm, msg, len);

   // flush vterm buffer when vterm responded to control sequence
   if (prevlen != vterm_output_get_buffer_current(vterm))
      term_forward_output(term);

   // this invokes the damage callbacks
   vterm_screen_flush_damage(vterm_obtain_screen(vterm));
}

private void
position_cursor(Portal *po, VTermPos *pos) {
   po->cursorRow = MIN((int)pos->row, MAX(0, (int)po->height - 1));
   po->cursorCol = MIN((int)pos->col, MAX(0, (int)po->width - 1));
   if (popup_is_popup(po)) {
      po->cursorRow += popup_top_extra(po);
      po->cursorCol += popup_left_extra(po);
      po->flags |= WFLAG_WCOL_OFF_ADDED | WFLAG_WROW_OFF_ADDED;
   } else
      po->flags &= ~(WFLAG_WCOL_OFF_ADDED | WFLAG_WROW_OFF_ADDED);
   po->cacheState |= (VALID_WCOL|VALID_WROW);
}

private void
update_cursor(Terminal *term, int redraw) {
   if (term->isNormalMode)
      return;
   if (!term_job_running(term)) {
      // avoid the cursor positioned below the last used line
      setcursor();
   } else {
      // do not use the portal cursor position
      position_cursor(curPor, &curBook->term->cursorPos);
      windgoto(curPor->portalRow + curPor->cursorRow, curPor->portalCol + curPor->cursorCol);
   }
   if (redraw) {
      if (term->book == curBook && term->tl_cursor_visible)
         cursor_on();
      out_flush();
   }
}

// Invoked when "msg" output from a job was received.  Write it to the terminal of "buffer"
void
write_to_term(Book *book, CS msg, Channel* channel) {
   Unt   len = STRLEN(msg);
   Terminal* term = book->term;


   if (term->vterm == NULL) {
      ch_log(channel, "NOT writing %d bytes to terminal", (int)len);
      return;
   }
   ch_log(channel, "writing %d bytes to terminal", (int)len);
   cursor_off();
   term_write_job_output(term, msg, len);

   // In Terminal-Normal mode we are displaying the book, not the terminal
   // contents, thus no screen update is needed.
   if (!term->isNormalMode) {
      // Don't use drawUpdateScreen() when editing the command line, it gets
      // cleared.
      // TODO: only update once in a while.
      ch_log(term->job->jv_channel, "updating screen");
      if (book == curBook && (stateG & MODE_COMMLINE) == 0) {
         drawUpdateScreen(UPD_VALID_NO_UPDATE);
         if (needRedrawTabpanelG) 
             draw_tabpanel();
         // drawUpdateScreen() can be slow, check the terminal wasn't closed already
         if (book == curBook && curBook->term != NULL)
            update_cursor(curBook->term, TRUE);
      } else
         redraw_after_callback(TRUE, FALSE);
   }
}

// Send a mouse position and click to the vterm
private int
sendMouse(VTerm *vterm, int button, int pressed) {
   VTermModifier   mod = VTERM_MOD_NONE;
   int row = mouseRowG - curPor->portalRow;
   int col = mouseColG - curPor->portalCol;

   if (popup_is_popup(curPor)) {
      row -= popup_top_extra(curPor);
      col -= popup_left_extra(curPor);
   }
   vterm_mouse_move(vterm, row, col, mod);
   if (button != 0)
      vterm_mouse_button(vterm, button, pressed, mod);
   return TRUE;
}

private int enter_mouse_col = -1;
private int enter_mouse_row = -1;

// Handle a mouse click, drag or release. Return TRUE when a mouse event is sent to the terminal.
private int
handleMouseEvent(VTerm *vterm, Unt key) {
   // For modeless selection mouse drag and release events are ignored, unless they are preceded 
   // with a mouse down event
   static int       ignore_drag_release = TRUE;
   VTermMouseState mouse_state;

   vterm_state_get_mousestate(vterm_obtain_state(vterm), &mouse_state);
   if (mouse_state.flags == 0) {
      // Terminal is not using the mouse, use modeless selection.
      switch (key) {
      case K_LEFTDRAG:
      case K_LEFTRELEASE:
      case K_RIGHTDRAG:
      case K_RIGHTRELEASE:
         // Ignore drag and release events when the button-down wasn't seen before.
         if (ignore_drag_release) {
            int save_mouse_col, save_mouse_row;

            if (enter_mouse_col < 0)
               break;

            // mouse click in the portal gave us focus, handle that click now
            save_mouse_col = mouseColG;
            save_mouse_row = mouseRowG;
            mouseColG = enter_mouse_col;
            mouseRowG = enter_mouse_row;
            clip_modeless(MOUSE_LEFT, TRUE, FALSE);
            mouseColG = save_mouse_col;
            mouseRowG = save_mouse_row;
         }
         // FALLTHROUGH
      case K_LEFTMOUSE:
      case K_RIGHTMOUSE:
         if (key == K_LEFTRELEASE || key == K_RIGHTRELEASE)
            ignore_drag_release = TRUE;
         else
            ignore_drag_release = FALSE;
         if (clipboard.available) {
            Boole is_click, is_drag;
            int button = get_mouse_button(KEY2TERMCAP1(key), &is_click, &is_drag);
            clip_modeless(button, is_click, is_drag);
         }
         break;

      case K_MIDDLEMOUSE:
         if (clipboard.available)
            insert_reg('*', TRUE);
         break;
      }
      enter_mouse_col = -1;
      return FALSE;
   }
   enter_mouse_col = -1;

   switch (key) {
   case K_LEFTMOUSE:
   case K_LEFTMOUSE_NM:   sendMouse(vterm, 1, 1); break;
   case K_LEFTDRAG:   sendMouse(vterm, 1, 1); break;
   case K_LEFTRELEASE:
   case K_LEFTRELEASE_NM:   sendMouse(vterm, 1, 0); break;
   case K_MOUSEMOVE:   sendMouse(vterm, 0, 0); break;
   case K_MIDDLEMOUSE:   sendMouse(vterm, 2, 1); break;
   case K_MIDDLEDRAG:   sendMouse(vterm, 2, 1); break;
   case K_MIDDLERELEASE:   sendMouse(vterm, 2, 0); break;
   case K_RIGHTMOUSE:   sendMouse(vterm, 3, 1); break;
   case K_RIGHTDRAG:   sendMouse(vterm, 3, 1); break;
   case K_RIGHTRELEASE:   sendMouse(vterm, 3, 0); break;
   }
   return TRUE;
}

// Convert typed key "c" with modifiers "modmask" into bytes to send to the job.
// Return the number of bytes in "buf".
private int
term_convert_key(Terminal *term, Unt c, int modmask, CS buf) {
   VTerm       *vterm = term->vterm;
   VTermKey       key = VTERM_KEY_NONE;
   VTermModifier   mod = VTERM_MOD_NONE;
   int          other = FALSE;

   switch (c) {
   // don't use VTERM_KEY_ENTER, it may do an unwanted conversion

   // don't use VTERM_KEY_BACKSPACE, it always becomes 0x7f DEL
   case K_BS:      c = term_backspace_char; break;

   case ESC:      key = VTERM_KEY_ESCAPE; break;
   case K_DEL:      key = VTERM_KEY_DEL; break;
   case K_DOWN:      key = VTERM_KEY_DOWN; break;
   case K_S_DOWN:      mod = VTERM_MOD_SHIFT;
            key = VTERM_KEY_DOWN; break;
   case K_END:      key = VTERM_KEY_END; break;
   case K_S_END:      mod = VTERM_MOD_SHIFT;
            key = VTERM_KEY_END; break;
   case K_C_END:      mod = VTERM_MOD_CTRL;
            key = VTERM_KEY_END; break;
   case K_F10:      key = VTERM_KEY_FUNCTION(10); break;
   case K_F11:      key = VTERM_KEY_FUNCTION(11); break;
   case K_F12:      key = VTERM_KEY_FUNCTION(12); break;
   case K_F1:      key = VTERM_KEY_FUNCTION(1); break;
   case K_F2:      key = VTERM_KEY_FUNCTION(2); break;
   case K_F3:      key = VTERM_KEY_FUNCTION(3); break;
   case K_F4:      key = VTERM_KEY_FUNCTION(4); break;
   case K_F5:      key = VTERM_KEY_FUNCTION(5); break;
   case K_F6:      key = VTERM_KEY_FUNCTION(6); break;
   case K_F7:      key = VTERM_KEY_FUNCTION(7); break;
   case K_F8:      key = VTERM_KEY_FUNCTION(8); break;
   case K_F9:      key = VTERM_KEY_FUNCTION(9); break;
   case K_HOME:      key = VTERM_KEY_HOME; break;
   case K_S_HOME:      mod = VTERM_MOD_SHIFT;
            key = VTERM_KEY_HOME; break;
   case K_C_HOME:      mod = VTERM_MOD_CTRL;
            key = VTERM_KEY_HOME; break;
   case K_INS:      key = VTERM_KEY_INS; break;
   case K_K0:      key = VTERM_KEY_KP_0; break;
   case K_K1:      key = VTERM_KEY_KP_1; break;
   case K_K2:      key = VTERM_KEY_KP_2; break;
   case K_K3:      key = VTERM_KEY_KP_3; break;
   case K_K4:      key = VTERM_KEY_KP_4; break;
   case K_K5:      key = VTERM_KEY_KP_5; break;
   case K_K6:      key = VTERM_KEY_KP_6; break;
   case K_K7:      key = VTERM_KEY_KP_7; break;
   case K_K8:      key = VTERM_KEY_KP_8; break;
   case K_K9:      key = VTERM_KEY_KP_9; break;
   case K_KDEL:      key = VTERM_KEY_DEL; break; // TODO
   case K_KDIVIDE:      key = VTERM_KEY_KP_DIVIDE; break;
   case K_KEND:      key = VTERM_KEY_KP_1; break; // TODO
   case K_KENTER:      key = VTERM_KEY_KP_ENTER; break;
   case K_KHOME:      key = VTERM_KEY_KP_7; break; // TODO
   case K_KINS:      key = VTERM_KEY_KP_0; break; // TODO
   case K_KMINUS:      key = VTERM_KEY_KP_MINUS; break;
   case K_KMULTIPLY:   key = VTERM_KEY_KP_MULT; break;
   case K_KPAGEDOWN:   key = VTERM_KEY_KP_3; break; // TODO
   case K_KPAGEUP:      key = VTERM_KEY_KP_9; break; // TODO
   case K_KPLUS:      key = VTERM_KEY_KP_PLUS; break;
   case K_KPOINT:      key = VTERM_KEY_KP_PERIOD; break;
   case K_LEFT:      key = VTERM_KEY_LEFT; break;
   case K_S_LEFT:      mod = VTERM_MOD_SHIFT;
            key = VTERM_KEY_LEFT; break;
   case K_C_LEFT:      mod = VTERM_MOD_CTRL;
            key = VTERM_KEY_LEFT; break;
   case K_PAGEDOWN:   key = VTERM_KEY_PAGEDOWN; break;
   case K_PAGEUP:      key = VTERM_KEY_PAGEUP; break;
   case K_RIGHT:      key = VTERM_KEY_RIGHT; break;
   case K_S_RIGHT:      mod = VTERM_MOD_SHIFT;
            key = VTERM_KEY_RIGHT; break;
   case K_C_RIGHT:      mod = VTERM_MOD_CTRL;
            key = VTERM_KEY_RIGHT; break;
   case K_UP:      key = VTERM_KEY_UP; break;
   case K_S_UP:      mod = VTERM_MOD_SHIFT;
            key = VTERM_KEY_UP; break;
   case TAB:      key = VTERM_KEY_TAB; break;
   case K_S_TAB:      mod = VTERM_MOD_SHIFT;
            key = VTERM_KEY_TAB; break;

   case K_MOUSEUP:      other = sendMouse(vterm, 5, 1); break;
   case K_MOUSEDOWN:   other = sendMouse(vterm, 4, 1); break;
   case K_MOUSELEFT:   other = sendMouse(vterm, 7, 1); break;
   case K_MOUSERIGHT:   other = sendMouse(vterm, 6, 1); break;

   case K_LEFTMOUSE:
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
      if (!handleMouseEvent(vterm, c))
         return 0;
      other = TRUE;
      break;

   case K_X1MOUSE:      /* TODO */ return 0;
   case K_X1DRAG:      /* TODO */ return 0;
   case K_X1RELEASE:   /* TODO */ return 0;
   case K_X2MOUSE:      /* TODO */ return 0;
   case K_X2DRAG:      /* TODO */ return 0;
   case K_X2RELEASE:   /* TODO */ return 0;

   case K_IGNORE:      return 0;
   case K_NOP:      return 0;
   case K_UNDO:      return 0;
   case K_HELP:      return 0;
   case K_XF1:      key = VTERM_KEY_FUNCTION(1); break;
   case K_XF2:      key = VTERM_KEY_FUNCTION(2); break;
   case K_XF3:      key = VTERM_KEY_FUNCTION(3); break;
   case K_XF4:      key = VTERM_KEY_FUNCTION(4); break;
   case K_DROP:      return 0;
   case K_CURSORHOLD:   return 0;
   case K_PS:      
      vterm_keyboard_start_paste(vterm);
      other = TRUE;
      break;
   case K_PE:
         vterm_keyboard_end_paste(vterm);
         other = TRUE;
         break;
   }

   // add modifiers for the typed key
   if (modmask & MOD_MASK_SHIFT)
      mod |= VTERM_MOD_SHIFT;
   if (modmask & MOD_MASK_CTRL)
      mod |= VTERM_MOD_CTRL;
   if (modmask & (MOD_MASK_ALT | MOD_MASK_META))
      mod |= VTERM_MOD_ALT;

   // Ctrl-Shift-i may have the key "I" instead of "i", but for the kitty
   // keyboard protocol should use "i".  Applies to all ascii letters.
   if (ASCII_ISUPPER(c)
          && vterm_is_kitty_keyboard(vterm)
          && mod == (VTERM_MOD_CTRL | VTERM_MOD_SHIFT))
      c = TOLOWER_ASC(c);

   // Convert special keys to vterm keys:
   // - Write keys to vterm: vterm_keyboard_key()
   // - Write output to channel.
   if (key != VTERM_KEY_NONE)
      // Special key, let vterm convert it.
      vterm_keyboard_key(vterm, key, mod);
   ei (!other)
      // Normal character, let vterm convert it.
      vterm_keyboard_unichar(vterm, c, mod);

   // Read back the converted escape sequence.
   return (int)vterm_output_read(vterm, buf, KEY_BUF_LEN);
}

// Return TRUE if the job for "term" is still running. If "check_job_status" is TRUE update the 
// job status. NOTE: "term" may be freed by callbacks.
private int
term_job_running_check(Terminal *term, int check_job_status) {
   // Also consider the job finished when the channel is closed, to avoid a
   // race condition when updating the title.
   if (!term || !term->job || !channel_is_open(term->job->jv_channel))
      return FALSE;

   Job *job = term->job;

   // Careful: Checking the job status may invoke callbacks, which close
   // the book and terminate "term".  However, "job" will not be freed yet.
   if (check_job_status)
      job_status(job);
   return (job->jv_status == JOB_STARTED
          || (job->jv_channel != NULL && job->jv_channel->ch_keep_open));
}

// Return TRUE if the job for "term" is still running.
int
term_job_running(Terminal *term) {
   return term_job_running_check(term, FALSE);
}

// Return TRUE if the job for "term" is still running, ignoring the job was "NONE".
int
term_job_running_not_none(Terminal *term) {
   return term_job_running(term) && !term_none_open(term);
}

// Return TRUE if "term" has an active channel and used ":term NONE".
int
term_none_open(Terminal *term) {
   // Also consider the job finished when the channel is closed, to avoid a
   // race condition when updating the title.
   return term != NULL
      && term->job != NULL
      && channel_is_open(term->job->jv_channel)
      && term->job->jv_channel->ch_keep_open;
}

// Used to confirm whether we would like to kill a terminal. Return OK when the user confirms to 
// kill it. Return FAIL if the user selects otherwise.
int
term_confirm_stop(Book* book) {
   Byte buff[DIALOG_MSG_SIZE];

   dialog_msg(buff, _("Kill job in \"%s\"?"), bookGetFname(book));
   int ret = eeDialog_yesno(EE_QUESTION, NULL, buff, 1);
   if (ret == EE_YES)
      return OK;
   else
      return FAIL;
}

// Used when exiting: kill the job in "book" if so desired. Return OK when the job finished.
// Return FAIL when the job is still running.
int
term_try_stop_job(Book* book) {
   int       count;
   CS  how = book->term->tl_kill;

   if ((how == NULL || *how == ZERO) && (p_confirm || (commModifierG.cmod_flags & CMOD_CONFIRM))) {
      if (term_confirm_stop(book) == OK)
         how = S"kill";
      else
         return FAIL;
   }
   if (how == NULL || *how == ZERO)
      return FAIL;

   job_stop(book->term->job, NULL, how);

   // wait for up to a second for the job to die
   for (count = 0; count < 100; ++count) {
      Job *job;

      // book, terminal and job may be cleaned up while waiting
      if (!bookIsValid(book) || book->term == NULL || book->term->job == NULL)
         return OK;
      job = book->term->job;

      // Call job_status() to update jv_status. It may cause the job to be
      // cleaned up but it won't be freed.
      job_status(job);
      if (job->jv_status >= JOB_ENDED)
         return OK;

      ui_delay(10L, TRUE);
      term_flush_messages();
   }
   return FAIL;
}

// Add the last line of the scrollback buffer to the buffer in the portal.
private void
add_scrollback_line_to_buffer(Terminal *term, CS text, Unt len) {
   Book* book = term->book;
   int      empty = (book->mem.flags & ML_EMPTY);
   LineNr   lnum = book->mem.lineCount;

   memAppendBook(term->book, lnum, text, len + 1, FALSE);
   if (empty) {
      // Delete the empty line that was in the empty buffer.
      ml_deleteBufLine(book, 1);
   }
}

private void
convertCellDecoFromVterm(OUT CellDeco* deco, const VTermScreenCell* cell) {
   deco->width = cell->width;
   deco->flags = cell->attrs;
   deco->fg = cell->fg;
   deco->bg = cell->bg;
}

private int
equal_celattr(CellDeco *a, CellDeco *b) {
   // We only compare the RGB colors, ignoring the ANSI index and type.
   // Thus black set explicitly is equal the background black.
   return a->fg.red == b->fg.red
      && a->fg.green == b->fg.green
      && a->fg.blue == b->fg.blue
      && a->bg.red == b->bg.red
      && a->bg.green == b->bg.green
      && a->bg.blue == b->bg.blue;
}

// Add an empty scrollback line to "term".  When "lnum" is not zero, add the
// line at this position.  Otherwise at the end.
private int
add_empty_scrollback(Terminal *term, CellDeco *fillDeco, int lnum){
   if (ga_grow(&term->scrollback, 1) == FAIL)
      return FALSE;

   ScrollbackLine *line = (ScrollbackLine *)term->scrollback.c + term->scrollback.len;

   if (lnum > 0) {
      int i;

      for (i = 0; i < term->scrollback.len - lnum; ++i) {
         *line = *(line - 1);
         --line;
      }
   }
   line->cols = 0;
   line->sb_cells = NULL;
   line->sb_fillDeco = *fillDeco;
   ++term->scrollback.len;
   return OK;
}

// Remove the terminal contents from the scrollback and the buffer. Used before adding a new 
// scrollback line or updating the buffer for lines displayed in the terminal.
private void
cleanup_scrollback(Terminal *term) {
   ScrollbackLine   *line;
   ArrayList   *gap;

   curBook = term->book;
   gap = &term->scrollback;
   while (curBook->mem.lineCount > term->scrollbackScrolled && gap->len > 0) {
      ml_delete(curBook->mem.lineCount);
      line = (ScrollbackLine *)gap->c + gap->len - 1;
      eeglFree(line->sb_cells);
      --gap->len;
   }
   curBook = curPor->book;
   if (curBook == term->book)
      check_cursor();
}

// Add the current lines of the terminal to scrollback and to the buffer.
private void
update_snapshot(Terminal *term) {
   VTermScreen       *screen;
   int          lines_skipped = 0;
   VTermPos       pos;
   VTermScreenCell cell;
   CellDeco       fillDeco, newFillDeco;
   CellDeco       *p;

   ch_log(term->job == NULL ? NULL : term->job->jv_channel,
              "Adding terminal portal snapshot to buffer");

   // First remove the lines that were appended before, they might be outdated.
   cleanup_scrollback(term);

   screen = vterm_obtain_screen(term->vterm);
   fillDeco = newFillDeco = term->cellDeco;
   for (pos.row = 0; pos.row < term->rows; ++pos.row) {
      Unt len = 0;
      for (pos.col = 0; pos.col < term->cols; ++pos.col)
         if (vterm_screen_get_cell(screen, pos, &cell) != 0 && cell.chars[0] != ZERO) {
            len = pos.col + 1;
            newFillDeco = term->cellDeco;
         } else
            // Assume the last deco is the filler deco
            convertCellDecoFromVterm(OUT &newFillDeco, &cell);

      if (len == 0 && equal_celattr(&newFillDeco, &fillDeco))
          ++lines_skipped;
      else {
         while (lines_skipped > 0) {
            // Line was skipped, add an empty line.
            --lines_skipped;
            if (add_empty_scrollback(term, &fillDeco, 0) == OK)
                add_scrollback_line_to_buffer(term, (CS)"", 0);
         }

         if (len == 0)
            p = NULL;
         else
            p = ALLOC_MULT(CellDeco, len);
         if ((p || len == 0) && ga_grow(&term->scrollback, 1) == OK) {
            ArrayList    ga;
            int       width;
            ScrollbackLine *line = (ScrollbackLine *)term->scrollback.c + term->scrollback.len;

            ga_init2(&ga, 1, 100);
            for (pos.col = 0; pos.col < len; pos.col += width) {
               if (vterm_screen_get_cell(screen, pos, &cell) == 0) {
               width = 1;
               CLEAR_POINTER(p + pos.col);
               if (ga_grow(&ga, 1) == OK)
                   ga.len += mb_char2bytes(' ', (CS)ga.c + ga.len);
                } else {
                  width = cell.width;

                  convertCellDecoFromVterm(OUT &p[pos.col], &cell);
                  if (width == 2)
                      // second cell of double-width character has the same decorations.
                      p[pos.col + 1] = p[pos.col];

                  // Each character can be up to 6 bytes.
                  if (ga_grow(&ga, VTERM_MAX_CHARS_PER_CELL * 6) == OK) {
                     int i;
                     int c;

                     for (i = 0; (c = cell.chars[i]) > 0 || i == 0; ++i)
                        ga.len += mb_char2bytes(c == ZERO ? ' ' : c, (CS)ga.c + ga.len);
                  }
               }
            }
            line->cols = len;
            line->sb_cells = p;
            line->sb_fillDeco = newFillDeco;
            fillDeco = newFillDeco;
            ++term->scrollback.len;

            if (ga_grow(&ga, 1) == FAIL)
               add_scrollback_line_to_buffer(term, (CS)"", 0);
            else {
               *((CS)ga.c + ga.len) = ZERO;
               add_scrollback_line_to_buffer(term, ga.c, ga.len);
            }
            ga_clear(&ga);
          } else
            eeglFree(p);
      }
   }

   // Add trailing empty lines.
   for (pos.row = term->scrollback.len;
        pos.row < term->scrollbackScrolled + term->cursorPos.row;
        ++pos.row
   ) {
      if (add_empty_scrollback(term, &fillDeco, 0) == OK)
         add_scrollback_line_to_buffer(term, (CS)"", 0);
   }

   term->dirtySnapshot = false;
   term->timerSet = false;
}

// Loop over all portals in the current tab, and also curPor, which is not
// encountered when using a terminal in a popup portal.
// Return TRUE if "*po" was set to the next portal.
private int
forAllPortalsAndCurPort(OUT Portal **po, OUT int *did_curPor) {
   if (!*po)
      *po = firstPor;
   ei ((*po)->next)
      *po = (*po)->next;
   ei (!*did_curPor)
      *po = curPor;
   else
      return FALSE;
   if (*po == curPor)
      *did_curPor = TRUE;
   return TRUE;
}

// If needed, add the current lines of the terminal to scrollback and to the
// buffer.  Called after the job has ended and when switching to Terminal-Normal mode.
// When "redraw" is TRUE redraw the portals that show the terminal.
private void
may_move_terminal_to_buffer(Terminal *term, int redraw) {
   if (term->vterm == NULL)
      return;

   // Update the snapshot only if something changes or the buffer does not have all the lines.
   if (term->dirtySnapshot || term->book->mem.lineCount <= term->scrollbackScrolled) {
      update_snapshot(term);
   } 

   // Obtain the current background color.
   vterm_state_get_default_colors(
      vterm_obtain_state(term->vterm), &term->cellDeco.fg, &term->cellDeco.bg
   );

   if (redraw) {
      Portal* po = NULL;
      int did_curPor = FALSE;

      while (forAllPortalsAndCurPort(OUT &po, OUT &did_curPor)) {
         if (po->book == term->book) {
            po->cursor.lnum = term->book->mem.lineCount;
            po->cursor.col = 0;
            po->cacheState = 0;
            if (po->cursor.lnum >= po->height) {
               LineNr min_topline = po->cursor.lnum - po->height + 1;

               if (po->topLine < min_topline)
                  po->topLine = min_topline;
            }
            redrawPortLater(po, UPD_NOT_VALID);
         }
      }
   }
}

// Check if any terminal timer expired.  If so, copy text from the terminal to the buffer.
// Return the time until the next timer will expire.
int
term_check_timers(int next_due_arg, ProfTime *now) {
   Terminal* term;
   int       next_due = next_due_arg;

   FOR_ALL_TERMS(term) {
      if (term->timerSet && !term->isNormalMode) {
         long    this_due = proftime_time_left(&term->timerDue, now);

         if (this_due <= 1) {
            term->timerSet = false;
            may_move_terminal_to_buffer(term, FALSE);
         } ei (next_due == -1 || next_due > this_due)
            next_due = this_due;
      }
   }

   return next_due;
}

// When "normal_mode" is TRUE set the terminal to Terminal-Normal mode, otherwise end it.
private void
set_terminal_mode(Terminal *term, int normal_mode) {
   term->isNormalMode = normal_mode;
   may_trigger_modechanged();
   if (!normal_mode)
      handle_postponed_scrollback(term);
   EE_CLEAR(term->tl_status_text);
}

// Called after the job is finished and Terminal mode is not active:
// Move the vterm contents into the scrollback buffer and free the vterm.
private void
cleanup_vterm(Terminal *term) {
   set_terminal_mode(term, FALSE);
   if (term->tl_finish != TL_FINISH_CLOSE)
      may_move_terminal_to_buffer(term, TRUE);
   term_free_vterm(term);
}

// Switch from Terminal-Job mode to Terminal-Normal mode. Suspend updating the terminal portal.
private void
term_enter_normal_mode(void) {
   Terminal *term = curBook->term;

   set_terminal_mode(term, TRUE);

   // Append the current terminal contents to the buffer.
   may_move_terminal_to_buffer(term, TRUE);

   // Move the portal cursor to the position of the cursor in the terminal.
   curPor->cursor.lnum = term->scrollbackScrolled
                    + term->cursorPos.row + 1;
   check_cursor();
   if (coladvance(term->cursorPos.col) == FAIL)
      coladvance(MAXCOL);
   curPor->setCursWant = true;

   // Display the same lines as in the terminal.
   curPor->topLine = term->scrollbackScrolled + 1;
}

// Return TRUE if the current portal contains a terminal and we are in Terminal-Normal mode.
int
term_in_normal_mode(void) {
   Terminal *term = curBook->term;
   return term && term->isNormalMode;
}

// Switch from Terminal-Normal mode to Terminal-Job mode. Restores updating the terminal portal.
void
term_enter_job_mode(void) {
   Terminal   *term = curBook->term;

   set_terminal_mode(term, FALSE);

   if (term->isChannelClosed)
      cleanup_vterm(term);
   drawBookAndStatusLater(curBook, UPD_NOT_VALID);
   if (PORTAL_IS_POPUP(curPor))
      redraw_later(UPD_NOT_VALID);
}

//When "modify_other_keys" is set, then vgetc() should not reduce a key with modifiers into a basic
//key.  However, we may only find out after calling vgetc().  Therefore vgetorpeek() will call 
//check_no_reduce_keys() to update "no_reduce_keys" before using it.
typedef enum {
   NRKS_NONE,   // initial value
   NRKS_CHECK,  // modify_other_keys was off before calling vgetc()
   NRKS_SET,    // no_reduce_keys was incremented in term_vgetc() or
             // check_no_reduce_keys(), must be decremented.
} reduce_key_state_T;

private reduce_key_state_T  no_reduce_key_state = NRKS_NONE;

// Return TRUE if the term is using modifyOtherKeys level 2 or the kitty keyboard protocol.
private int
vterm_using_key_protocol(void) {
    return curBook->term
      && curBook->term->vterm
      && (vterm_is_modify_other_keys(curBook->term->vterm)
         || vterm_is_kitty_keyboard(curBook->term->vterm));
}

void
check_no_reduce_keys(void) {
   if (no_reduce_key_state != NRKS_CHECK
       || no_reduce_keys >= 1
       || !curBook->term
       || !curBook->term->vterm
   )
      return;

   if (vterm_using_key_protocol()) {
      // "modify_other_keys" or kitty keyboard protocol was enabled while waiting.
      no_reduce_key_state = NRKS_SET;
      ++no_reduce_keys;
   }
}

// Get a key from the user with terminal mode mappings.
// Note: while waiting a terminal may be closed and freed if the channel is
// closed and ++close was used.  This may even happen before we get here.
private int
term_vgetc(void) {
   int save_State = stateG;

   stateG = MODE_TERMINAL;
   gotInterruptG = FALSE;

   if (vterm_using_key_protocol()) {
      ++no_reduce_keys;
      no_reduce_key_state = NRKS_SET;
   } else {
      no_reduce_key_state = NRKS_CHECK;
   }

   Unt c = vgetc();
   gotInterruptG = FALSE;
   stateG = save_State;

   if (no_reduce_key_state == NRKS_SET)
      --no_reduce_keys;
   no_reduce_key_state = NRKS_NONE;

   return c;
}

private int   mouse_was_outside = FALSE;

// Send key "c" with modifiers "modmask" to terminal. FAIL when the key needs to be handled in 
// Normal mode. OK when the key was dropped or sent to the terminal.
int
send_keys_to_term(Terminal *term, Unt c, int modmask, int typed) {
   Byte msg[KEY_BUF_LEN];
   Unt len;
   int dragging_outside = FALSE;

   // Catch keys that need to be handled as in Normal mode.
   switch (c) {
   case ZERO:
   case K_ZERO:
      if (typed)
         stuffcharReadbuff(c);
      return FAIL;

   case K_TABLINE:
      stuffcharReadbuff(c);
      return FAIL;

   case K_IGNORE:
   case K_CANCEL:  // used for :normal when running out of chars
      return FAIL;

   case K_LEFTDRAG:
   case K_MIDDLEDRAG:
   case K_RIGHTDRAG:
   case K_X1DRAG:
   case K_X2DRAG:
      dragging_outside = mouse_was_outside;
      // FALLTHROUGH
   case K_LEFTMOUSE:
   case K_LEFTMOUSE_NM:
   case K_LEFTRELEASE:
   case K_LEFTRELEASE_NM:
   case K_MOUSEMOVE:
   case K_MIDDLEMOUSE:
   case K_MIDDLERELEASE:
   case K_RIGHTMOUSE:
   case K_RIGHTRELEASE:
   case K_X1MOUSE:
   case K_X1RELEASE:
   case K_X2MOUSE:
   case K_X2RELEASE:

   case K_MOUSEUP:
   case K_MOUSEDOWN:
   case K_MOUSELEFT:
   case K_MOUSERIGHT: {
      int   row = mouseRowG;
      int   col = mouseColG;

      if (popup_is_popup(curPor)) {
         row -= popup_top_extra(curPor);
         col -= popup_left_extra(curPor);
      }
      if (row < curPor->portalRow
         || row >= (int)(curPor->portalRow + curPor->height)
         || col < curPor->portalCol
         || col >= (int)P_ENDCOL(curPor)
         || dragging_outside
      ) {
        // click or scroll outside the current portal or on status line or vertical separator
        if (typed) {
            stuffcharReadbuff(c);
            mouse_was_outside = TRUE;
         }
         return FAIL;
      }
      break;
      }

   case K_COMMAND:
   case K_SCRIPT_COMMAND:
      return do_cmdkey_command(c, 0);
   }
   if (typed)
      mouse_was_outside = FALSE;

   // Convert the typed key to a sequence of bytes for the job.
   len = term_convert_key(term, c, modmask, msg);
   if (len > 0)
      // TODO: if FAIL is returned, stop?
      channel_send(term->job->jv_channel, get_tty_part(term), (CS)msg, (int)len, NULL);

   return OK;
}

// Handle CTRL-W "": send register contents to the job.
private void
term_paste_register(Unt prev_c) {
   ListItem   *item;
   long   reglen = 0;
   int      type;

   if (add_to_showcmd(prev_c)) {
      if (add_to_showcmd('"'))
         out_flush();
   } 

   Unt c = term_vgetc();
   clear_showcmd();

   if (!term_use_loop())
      // job finished while waiting for a character
      return;

   // CTRL-W "= prompt for expression to evaluate.
   if (c == '=' && get_expr_register() != '=')
      return;
   if (!term_use_loop())
      // job finished while waiting for a character
      return;

   List* l = (List *)get_reg_contents(c, GREG_LIST);
   if (!l)
      return;

   type = get_reg_type(c, &reglen);
   FOR_ALL_LIST_ITEMS(l, item) {
      CS s = tv_get_string(&item->c);
      channel_send(curBook->term->job->jv_channel, PART_IN, s, (int)STRLEN(s), NULL);

      if (item->next || type == MLINE)
         channel_send(curBook->term->job->jv_channel, PART_IN, S"\r", 1, NULL);
   }
   list_free(l);
}

//Return TRUE when waiting for a character in the terminal, the cursor of the terminal should be 
//displayed.
int
terminal_is_active(void) {
   return in_terminal_loop != NULL;
}

// Return the hilite group ID for the terminal and the portal.
private int
term_get_highlight_id(Terminal* term, Portal* po) {
   CS name;

   if (po && po->o.hiliteGroupName)
      name = po->o.hiliteGroupName;
   ei (term->hiliteName)
      name = term->hiliteName;
   else
      name = S"Terminal";

   return hiliteGroupByName(mbText(name));
}

private void
may_output_cursor_props(void) {
   if (!cursor_color_equal(last_set_cursor_color, desired_cursor_color)
       || last_set_cursor_shape != desired_cursor_shape
       || last_set_cursor_blink != desired_cursor_blink
   ) {
      cursor_color_copy(OUT &last_set_cursor_color, desired_cursor_color);
      last_set_cursor_shape = desired_cursor_shape;
      last_set_cursor_blink = desired_cursor_blink;
      term_cursor_color(cursor_color_get(desired_cursor_color));
      if (desired_cursor_shape == -1 || desired_cursor_blink == -1)
         // this will restore the initial cursor style, if possible
         ui_cursor_shape_forced(TRUE);
      else
         term_cursor_shape(desired_cursor_shape, desired_cursor_blink);
   }
}

// Set the cursor color and shape, if not last set to these.
private void
may_set_cursor_props(Terminal *term) {
   if (in_terminal_loop == term) {
      cursor_color_copy(OUT &desired_cursor_color, term->tl_cursor_color);
      desired_cursor_shape = term->tl_cursor_shape;
      desired_cursor_blink = term->tl_cursor_blink;
      may_output_cursor_props();
   }
}

// Reset the desired cursor properties and restore them when needed.
private void
prepare_restoreCursor_props(void) {
   cursor_color_copy(OUT &desired_cursor_color, NULL);
   desired_cursor_shape = -1;
   desired_cursor_blink = -1;
   may_output_cursor_props();
}

//Return TRUE if the current portal contains a terminal and we are sending keys to the job.
//If "check_job_status" is TRUE update the job status.
private int
term_use_loop_check(int check_job_status) {
   Terminal *term = curBook->term;

   return term && !term->isNormalMode && term->vterm
      && term_job_running_check(term, check_job_status);
}

// Return TRUE if the current portal contains a terminal and we are sending keys to the job.
int
term_use_loop(void) {
   return term_use_loop_check(FALSE);
}

// Called when entering a portal with the mouse. If this is a terminal portal, we may 
// want to change state.
void
term_enterPortaled(void) {
   Terminal *term = curBook->term;

   if (!term)
      return;

   if (term_use_loop_check(TRUE)) {
      reset_VIsual_and_resel();
      if ((stateG & MODE_INSERT) != 0)
         stop_insert_mode = TRUE;
   }
   mouse_was_outside = FALSE;
   enter_mouse_col = mouseColG;
   enter_mouse_row = mouseRowG;
}

void
term_focus_change(int in_focus) {
   Terminal *term = curBook->term;

   if (!term || !term->vterm)
      return;

   VTermState* state = vterm_obtain_state(term->vterm);

   if (in_focus)
      vterm_state_focus_in(state);
   else
      vterm_state_focus_out(state);
   term_forward_output(term);
}

//vgetc() may not include CTRL in the key when modify_other_keys is set.
//Return the Ctrl-key value in that case.
private Unt
raw_c_to_ctrl(Unt c) {
   if ((modMaskG & MOD_MASK_CTRL) && ((c >= '`' && c <= 0x7f) || (c >= '@' && c <= '_')))
      return c & 0x1f;
   return c;
}

//When modify_other_keys is set then do the reverse of raw_c_to_ctrl().
//Also when the Kitty keyboard protocol is used. May set "modMaskG".
private int
ctrl_to_raw_c(int c) {
   if (c < 0x20 && vterm_using_key_protocol()) {
      modMaskG |= MOD_MASK_CTRL;
      return c + '@';
   }
   return c;
}

//Wait for input and send it to the job.
//When "blocking" is TRUE wait for a character to be typed.  Otherwise return when there is no more
//typahead. Return when the start of a CTRL-W command is typed or anything else that should be 
//handled as a Normal mode command. Returns OK if a typed character is to be handled in Normal 
//mode, FAIL if the terminal was closed.
int
terminal_loop(int blocking) {
   Unt c;
   Unt raw_c;
   Unt termwinkey = 0;
   int ret;
   int tty_fd = curBook->term->job->jv_channel->fds[get_tty_part(curBook->term)].ch_fd;
   Boole restoreCursor = false;

   // Remember the terminal we are sending keys to.  However, the terminal might be closed while 
   // waiting for a character, e.g. typing "exit" in a shell and ++close was used.  Therefore use 
   // curBook->term instead of a stored reference.
   in_terminal_loop = curBook->term;

   if (curPor->o.termWinKey) {
      termwinkey = stringToChar(curPor->o.termWinKey, TRUE);

      if (termwinkey == Ctrl_W)
         termwinkey = 0;
   }
   position_cursor(curPor, &curBook->term->cursorPos);
   may_set_cursor_props(curBook->term);

   while (blocking || vpeekc_nomap() != ZERO) {
      // TODO: skip screen update when handling a sequence of keys.
      // Repeat redrawing in case a message is received while redrawing.
      while (must_redraw != 0) {
         if (drawUpdateScreen(0) == FAIL)
            break;
      }
      if (!term_use_loop_check(TRUE) || in_terminal_loop != curBook->term)
         // job finished while redrawing
         break;

      update_cursor(curBook->term, FALSE);
      restoreCursor = true;

      raw_c = term_vgetc();
      if (!term_use_loop_check(TRUE) || in_terminal_loop != curBook->term) {
          // Job finished while waiting for a character.  Push back the received character.
          if (raw_c != K_IGNORE)
             vungetc(raw_c);
          break;
      }
      if (raw_c == K_IGNORE)
         continue;
      c = raw_c_to_ctrl(raw_c);

      // The shell or another program may change the tty settings.  Getting them for every typed 
      // character is a bit of overhead, but it's needed for the first character typed, e.g. when 
      // Eegl starts in a shell.
      if (mch_isatty(tty_fd)) {
         TtyInfo info;

         // Get the current backspace character of the pty.
         if (get_tty_info(tty_fd, &info) == OK)
            term_backspace_char = info.backspace;
      }

      // Was either CTRL-W (termwinkey) or CTRL-\ pressed? Not in a system terminal.
      if ((c == (termwinkey == 0 ? Ctrl_W : termwinkey) || c == Ctrl_BSL)) {
         Unt prev_c = c;
         Unt prev_raw_c = raw_c;
         int prev_modMaskG = modMaskG;

         if (add_to_showcmd(c))
            out_flush();

         raw_c = term_vgetc();
         c = raw_c_to_ctrl(raw_c);
      
         clear_showcmd();

         if (!term_use_loop_check(TRUE) || in_terminal_loop != curBook->term)
            // job finished while waiting for a character
            break;

         if (prev_c == Ctrl_BSL) {
            if (c == Ctrl_N) {
               // CTRL-\ CTRL-N : go to Terminal-Normal mode.
               term_enter_normal_mode();
               ret = FAIL;
               goto theend;
            }
            // Send both keys to the terminal, first one here, second one below.
            send_keys_to_term(curBook->term, prev_raw_c, prev_modMaskG, TRUE);
         } ei (c == Ctrl_C) {
            // "CTRL-W CTRL-C" or 'termwinkey' CTRL-C: end the job
            mch_signal_job(curBook->term->job, (CS)"kill");
         } ei (c == '.') {
            // "CTRL-W .": send CTRL-W to the job
            // "'termwinkey' .": send 'termwinkey' to the job
            raw_c = ctrl_to_raw_c(termwinkey == 0 ? Ctrl_W : termwinkey);
         } ei (c == Ctrl_BSL) {
            // "CTRL-W CTRL-\": send CTRL-\ to the job
            raw_c = ctrl_to_raw_c(Ctrl_BSL);
         } ei (c == 'N') {
            // CTRL-W N : go to Terminal-Normal mode.
            term_enter_normal_mode();
            ret = FAIL;
            goto theend;
        } ei (c == '"') {
            term_paste_register(prev_c);
            continue;
        } ei (termwinkey == 0 || c != termwinkey) {
            // space for CTRL-W, modifier, multi-byte char and ZERO
            Byte buf[1 + 3 + MB_MAXBYTES + 1];

            // Put the command into the typeahead buffer, when using the
            // stuff buffer KeyStuffed is set and 'langmap' won't be used.
            buf[0] = Ctrl_W;
            buf[special_to_buf(c, modMaskG, FALSE, buf + 1) + 1] = ZERO;
            insertIntoTypebuf(buf, REMAP_NONE, 0, TRUE, FALSE);
            ret = OK;
            goto theend;
         }
      }
      if (send_keys_to_term(curBook->term, raw_c, modMaskG, TRUE) != OK) {
           if (raw_c == K_MOUSEMOVE)
              // We are sure to come back here, don't reset the cursor color
              // and shape to avoid flickering.
              restoreCursor = false;

           ret = OK;
           goto theend;
      }
   }
   ret = FAIL;

theend:
   in_terminal_loop = NULL;
   if (restoreCursor)
      prepare_restoreCursor_props();

   // Move a snapshot of the screen contents to the buffer, so that completion
   // works in other buffers.
   if (curBook->term != NULL && !curBook->term->isNormalMode)
      may_move_terminal_to_buffer(curBook->term, FALSE);

   return ret;
}

private void
may_toggle_cursor(Terminal *term) {
   if (in_terminal_loop != term)
      return;

   if (term->tl_cursor_visible)
      cursor_on();
   else
      cursor_off();
}

//Convert Vterm decorations to hilite decorations.
private char
convertDecoFlagsFromVterm(VTermScreenCellAttrs* cellDecoFlags) {
   char flags = 0;

   if (cellDecoFlags->bold)
      flags |= HL_BOLD;
   if (cellDecoFlags->underline)
      flags |= HL_UNDERLINE;
   if (cellDecoFlags->italic)
      flags |= HL_ITALIC;
   if (cellDecoFlags->reverse)
      flags |= HL_INVERSE;
   return flags;
}

// Store Vterm decorations in "cell" from highlight flags.
private void
copyDecorationsToVterm(OUT CellDeco *cell, char decoFlags) {
   CLEAR_FIELD(cell->flags);
   if (decoFlags & HL_BOLD)
      cell->flags.bold = 1;
   if (decoFlags & HL_UNDERLINE)
      cell->flags.underline = 1;
   if (decoFlags & HL_ITALIC)
      cell->flags.italic = 1;
   if (decoFlags & HL_INVERSE)
      cell->flags.reverse = 1;
}

//Convert the decorations of a vterm cell into a Decoration
// TODO get rid of it through type unification
private Decoration
cellToDecoration(
   Terminal* term,
   Portal* po,
   VTermScreenCellAttrs* cellattrs,
   VTermColor* cellfg,
   VTermColor* cellbg
){
   char decoFlags = convertDecoFlagsFromVterm(cellattrs);
   VTermColor* fg = cellfg;
   VTermColor* bg = cellbg;
   int is_default_fg = VTERM_COLOR_IS_DEFAULT_FG(fg);
   int is_default_bg = VTERM_COLOR_IS_DEFAULT_BG(bg);

   if (is_default_fg || is_default_bg) {
      if (po && po->o.hiliteGroupName) {
         if (is_default_fg)
            fg = &po->termHiliteGroupName.fg;
         if (is_default_bg)
            bg = &po->termHiliteGroupName.bg;
      } else {
         if (is_default_fg)
            fg = &term->cellDeco.fg;
         if (is_default_bg)
            bg = &term->cellDeco.bg;
      }
   }

   UiColor tgcfg = VTERM_COLOR_IS_INVALID(fg)
       ? INVALCOLOR
       : toUiColor(fg->red, fg->green, fg->blue);
   UiColor tgcbg = VTERM_COLOR_IS_INVALID(bg)
       ? INVALCOLOR
       : toUiColor(bg->red, bg->green, bg->blue);
   return (Decoration) {
      .fg = tgcfg, .bg = tgcbg, .under = INVALCOLOR, .flags = decoFlags, .hiId = SHORT
   };
}

private void
set_dirty_snapshot(Terminal* term) {
   term->dirtySnapshot = true;
   if (!term->isNormalMode) {
      // Update the snapshot after 100 msec of not getting updates.
      profile_setlimit(100L, &term->timerDue);
      term->timerSet = true;
   }
}

private int
handle_damage(VTermRect rect, void *user) {
    Terminal *term = (Terminal *)user;

    term->dirtyRowStart = MIN(term->dirtyRowStart, (int)rect.start_row);
    term->dirtyRowEnd = MAX(term->dirtyRowEnd, (int)rect.end_row);
    set_dirty_snapshot(term);
    drawBookLater(term->book, UPD_SOME_VALID);
    return 1;
}

private void
term_scroll_up(Terminal *term, int start_row, int count) {
   Portal       *po = NULL;
   int          did_curPor = FALSE;
   VTermColor       fg, bg;
   VTermScreenCellAttrs cellAttr;

   CLEAR_FIELD(cellAttr);

   while (forAllPortalsAndCurPort(OUT &po, OUT &did_curPor)) {
      if (po->book == term->book) {
         // Set the color to clear lines with.
         vterm_state_get_default_colors(vterm_obtain_state(term->vterm), &fg, &bg);
         Decoration clearDeco = cellToDecoration(term, po, &cellAttr, &fg, &bg);
         deleteLinesFromPortal(po, start_row, count, FALSE, FALSE, clearDeco.flags);
      }
   }
}

private int
handle_moverect(VTermRect dest, VTermRect src, void *user) {
   Terminal   *term = (Terminal *)user;
   int      count = src.start_row - dest.start_row;

   // Scrolling up is done much more efficiently by deleting lines instead of
   // redrawing the text. But avoid doing this multiple times, postpone until
   // the redraw happens.
   if (dest.start_col == src.start_col
       && dest.end_col == src.end_col
       && dest.start_row < src.start_row)
    {
      if (dest.start_row == 0)
          term->postponedScroll += count;
      else
          term_scroll_up(term, dest.start_row, count);
    }

   term->dirtyRowStart = MIN(term->dirtyRowStart, (int)dest.start_row);
   term->dirtyRowEnd = MIN(term->dirtyRowEnd, (int)dest.end_row);
   set_dirty_snapshot(term);

   // Note sure if the scrolling will work correctly, let's do a complete redraw later.
   drawBookLater(term->book, UPD_NOT_VALID);
   return 1;
}

private int
handle_movecursor(
   VTermPos pos,
   VTermPos oldpos UNUSED,
   int visible,
   void *user
){
   Terminal   *term = (Terminal *)user;
   Portal   *po = NULL;
   int      did_curPor = FALSE;

   term->cursorPos = pos;
   term->tl_cursor_visible = visible;

   while (forAllPortalsAndCurPort(OUT &po, OUT &did_curPor)) {
      if (po->book == term->book)
          position_cursor(po, &pos);
   }
   if (term->book == curBook && !term->isNormalMode)
   update_cursor(term, term->tl_cursor_visible);

    return 1;
}

private int
handle_settermprop(VTermProp prop, VTermValue* value, void* user) {
   Terminal* term = (Terminal *)user;
   CS strval = NULL;

   switch (prop) {
   case VTERM_PROP_TITLE:
       if (disable_vterm_title_for_testing)
      break;
       strval = copySubstr((CS)value->string.str,
                         value->string.len);
      if (strval == NULL)
         break;
      eeglFree(term->title);
      // a blank title isn't useful, make it empty, so that "running" is
      // displayed
      if (*skipwhite(strval) == ZERO)
         term->title = NULL;
      // Same as blank
      ei (term->tl_arg0_cmd != NULL
          && STRNCMP(term->tl_arg0_cmd, strval, (int)STRLEN(term->tl_arg0_cmd)) == 0)
         term->title = NULL;
         // Empty corrupted data of winpty
      ei (STRNCMP("  - ", strval, 4) == 0)
         term->title = NULL;
      else {
         term->title = strval;
         strval = NULL;
      }
      EE_CLEAR(term->tl_status_text);
      if (term == curBook->term) {
         curPor->statusLineNeedsRedraw = TRUE;
      }
      break;

   case VTERM_PROP_CURSORVISIBLE:
       term->tl_cursor_visible = value->boolean;
       may_toggle_cursor(term);
       out_flush();
       break;

   case VTERM_PROP_CURSORBLINK:
       term->tl_cursor_blink = value->boolean;
       may_set_cursor_props(term);
       break;

   case VTERM_PROP_CURSORSHAPE:
       term->tl_cursor_shape = value->number;
       may_set_cursor_props(term);
       break;

   case VTERM_PROP_CURSORCOLOR:
      strval = copySubstr((CS)value->string.str, value->string.len);
      if (strval == NULL)
         break;
      cursor_color_copy(OUT &term->tl_cursor_color, strval);
      may_set_cursor_props(term);
      break;

   case VTERM_PROP_ALTSCREEN:
      // TODO: do anything else?
      term->tl_using_altscreen = value->boolean;
      break;

   default:
       break;
   }
   eeglFree(strval);

   // Always return 1, otherwise vterm doesn't store the value internally.
   return 1;
}

private void
handleShellResize(void) {
   doResizeG = FALSE;
   shell_resized();
}

// The job running in the terminal resized the terminal.
private int
handle_resize(int rows, int cols, void *user) {
   Terminal   *term = (Terminal *)user;

   term->rows = rows;
   term->cols = cols;
   if (term->vterm_size_changed)
      // Size was set by vterm_set_size(), don't set the portal size.
      term->vterm_size_changed = FALSE;
   else {
      Portal* po;
      FOR_ALL_PORTALS(po) {
         if (po->book == term->book) {
            portSetHeight(rows, po);
            portSetWidth(cols, po);
         }
      }
      drawBookLater(term->book, UPD_NOT_VALID);
   }
    return 1;
}

// If the number of lines that are stored goes over 'termwinscroll' then delete the first 10%.
// "scrollback" points to scrollback or scrollbackPostponed.
// "update_buffer" is TRUE when the buffer should be updated.
private void
limit_scrollback(Terminal *term, ArrayList* scrollback, int update_buffer) {
   if (scrollback->len < p_twsl)
      return;

   int   todo = MAX(p_twsl / 10, scrollback->len - p_twsl);

   for (int i = 0; i < todo; ++i) {
      eeglFree(((ScrollbackLine *)scrollback->c + i)->sb_cells);
      if (update_buffer)
         ml_deleteBufLine(term->book, 1);
   }

   scrollback->len -= todo;
   mch_memmove(
         scrollback->c, (ScrollbackLine *)scrollback->c + todo, sizeof(ScrollbackLine) * scrollback->len
   );
   if (update_buffer) {
      Portal *curPor_save = curPor;
      Portal *po = NULL;

      term->scrollbackScrolled -= todo;

      FOR_ALL_PORTALS(po) {
         if (po->book == term->book) {
            curPor = po;
            check_cursor();
            update_topline();
         }
      }
      curPor = curPor_save;
   }
}

// Handle a line that is pushed off the top of the screen.
private int
handle_pushline(int cols, const VTermScreenCell *cells, void *user) {
   Terminal   *term = (Terminal *)user;
   ArrayList* scrollback;
   int      update_buffer;

   if (term->isNormalMode) {
      //In Terminal-Normal mode the user interacts with the buffer, thus we
      //must not change it. Postpone adding the scrollback lines.
      scrollback = &term->scrollbackPostponed;
      update_buffer = FALSE;
   } else {
      //First remove the lines that were appended before, the pushed line goes above it.
      cleanup_scrollback(term);
      scrollback = &term->scrollback;
      update_buffer = TRUE;
   }

   limit_scrollback(term, scrollback, update_buffer);

   if (ga_grow(scrollback, 1) == FAIL)
      return 0;

   CellDeco* p = NULL;
   int len = 0;
   int i;
   int c;
   int col;
   CS text;
   ScrollbackLine   *line;
   ArrayList   ga;
   CellDeco   fillDeco = term->cellDeco;

   // do not store empty cells at the end
   for (i = 0; i < cols; ++i) {
      if (cells[i].chars[0] != 0)
         len = i + 1;
      else
         convertCellDecoFromVterm(OUT &fillDeco, &cells[i]);
   } 

   ga_init2(&ga, 1, 100);
   if (len > 0)
      p = ALLOC_MULT(CellDeco, len);
   if (p) {
      for (col = 0; col < len; col += cells[col].width) {
         if (ga_grow(&ga, MB_MAXBYTES) == FAIL) {
            ga.len = 0;
            break;
         }
         for (i = 0; (c = cells[col].chars[i]) > 0 || i == 0; ++i) {
            ga.len += mb_char2bytes(c == ZERO ? ' ' : c, (CS)ga.c + ga.len);
         } 
         convertCellDecoFromVterm(OUT p + col, &cells[col]);
      }
   }
   Unt text_len;
   if (ga_grow(&ga, 1) == FAIL) {
      if (update_buffer)
         text = E;
      else
         text = copyStr(E);
      text_len = 0;
   } else {
      text = ga.c;
      text_len = ga.len;
      *(text + text_len) = ZERO;
   }
   if (update_buffer)
      add_scrollback_line_to_buffer(term, text, text_len);

   line = (ScrollbackLine *)scrollback->c + scrollback->len;
   line->cols = len;
   line->sb_cells = p;
   line->sb_fillDeco = fillDeco;
   if (update_buffer) {
      line->sb_text = NULL;
      ++term->scrollbackScrolled;
      ga_clear(&ga);  // free the text
   } else {
      line->sb_text = text;
      ga_init(&ga);  // text is kept in scrollbackPostponed
   }
   ++scrollback->len;
   return 0; // ignored
}

//Called when leaving Terminal-Normal mode: deal with any scrollback that was
//received and stored in scrollbackPostponed.
private void
handle_postponed_scrollback(Terminal *term) {

   if (term->scrollbackPostponed.len == 0)
      return;
   lo("Moving postponed scrollback to scrollback");

   // First remove the lines that were appended before, the pushed lines go above it.
   cleanup_scrollback(term);

   for (int i = 0; i < term->scrollbackPostponed.len; ++i) {
      ScrollbackLine* pp_line;

      if (ga_grow(&term->scrollback, 1) == FAIL)
         break;
      pp_line = (ScrollbackLine *)term->scrollbackPostponed.c + i;

      CS text = pp_line->sb_text;
      if (text == NULL)
          text = (CS)"";
      add_scrollback_line_to_buffer(term, text, (int)STRLEN(text));
      eeglFree(pp_line->sb_text);

      ScrollbackLine* line = (ScrollbackLine *)term->scrollback.c + term->scrollback.len;
      line->cols = pp_line->cols;
      line->sb_cells = pp_line->sb_cells;
      line->sb_fillDeco = pp_line->sb_fillDeco;
      line->sb_text = NULL;
      ++term->scrollbackScrolled;
      ++term->scrollback.len;
   }

   ga_clear(&term->scrollbackPostponed);
   limit_scrollback(term, &term->scrollback, TRUE);
}

// Called when the terminal wants to ring the system bell.
private int
handle_bell(void *user UNUSED) {
   return 0;
}

private VTermScreenCallbacks screen_callbacks = {
   handle_damage,      // damage
   handle_moverect,      // moverect
   handle_movecursor,      // movecursor
   handle_settermprop,      // settermprop
   handle_bell,      // bell
   handle_resize,      // resize
   handle_pushline,      // sb_pushline
   NULL,         // sb_popline
   NULL         // sb_clear
};

//Do the work after the channel of a terminal was closed. Must be called only when updating_screen
//is FALSE. Returns TRUE when a buffer was closed (list of terminals may have changed).
private int
term_after_channel_closed(Terminal* term) {
    // Unless in Terminal-Normal mode: clear the vterm.
   if (!term->isNormalMode) {
      int   fnum = term->book->fiNum;

      cleanup_vterm(term);

      if (term->tl_finish == TL_FINISH_CLOSE) {
         AutocommSave aco;
         int do_set_locked = term->book->countPortals == 0;
         Portal* po = NULL;

         // If this was a terminal in a popup portal, go back to the previous portal.
         if (popup_is_popup(curPor) && curBook == term->book) {
            po = curPor;
            if (portalIsValid(prevPor))
                enterPortal(prevPor, FALSE);
         } else
            // If this is the last normal portal: exit Em.
            if (term->book->countPortals > 0 && onlyOnePortal()) {
               Invocation ea;

               CLEAR_FIELD(ea);
               c_quit(&ea);
               return TRUE;
            }

         // ++close or term_finish == "close"
         lo("terminal job finished, closing portal");
         auCommPrepareBook(&aco, term->book);
         if (curBook == term->book) {
            // Avoid closing the portal if we temporarily use it.
            if (is_autoCommPort(curPor))
               do_set_locked = TRUE;
            if (do_set_locked)
               curPor->locked = TRUE;
            do_bufdel(DOBOOK_WIPE, Em, 1, fnum, fnum, FALSE);
            if (do_set_locked)
                curPor->locked = FALSE;
            auCommRestoreBook(&aco);
         }
         if (po)
            popup_close_with_retval(po, 0);
         return TRUE;
      }
      if (term->tl_finish == TL_FINISH_OPEN && term->book->countPortals == 0) {
         CS comm = term->openComm ? term->openComm : S"botright sbuf %d";
         Unt len = STRLEN(comm) + 50;
         CS buf = alloc(len);

         lo("terminal job finished, opening portal");
         eeSnprintf(buf, len, comm, fnum);
         executeCommLine(buf);
         eeglFree(buf);
      } else
         lo("terminal job finished");
   }

   drawBookAndStatusLater(term->book, UPD_NOT_VALID);
   return FALSE;
}

//If the current portal is a terminal in a popup portal and the job has finished, close the 
//popup and to back to the previous portal. Otherwise return FAIL.
int
may_close_term_popup(void) {
   if (!popup_is_popup(curPor) || !curBook->term || term_job_running_not_none(curBook->term))
      return FAIL;

   Portal* po = curPor;

   if (portalIsValid(prevPor))
      enterPortal(prevPor, FALSE);
   popup_close_with_retval(po, 0);
   return OK;
}

// Called when a channel is going to be closed, before invoking the close callback.
void
term_channel_closing(Channel* ch) {
   for (Terminal* term = first_term; term != NULL; term = term->next) {
      if (term->job == ch->job && !term->isChannelClosed)
          term->isChannelClosing = TRUE;
   } 
}

// Called when a channel has been closed. If this was a terminal portal's chan, then finish it up
void
term_channel_closed(Channel* ch) {
   Terminal* term;
   Terminal* next_term;
   int did_one = FALSE;

   for (term = first_term; term != NULL; term = next_term) {
      next_term = term->next;
      if (term->job == ch->job && !term->isChannelClosed) {
         term->isChannelClosed = TRUE;
         did_one = TRUE;

         EE_CLEAR(term->title);
         EE_CLEAR(term->tl_status_text);

         if (updating_screen) {
            // Cannot open or close portals now.  Can happen when 'lazyredraw' is set.
            term->isChannelRecentlyClosed = TRUE;
            continue;
         }

         if (term_after_channel_closed(term))
            next_term = first_term;
      }
   }

   if (did_one) {
      redraw_statuslines();

      // Need to break out of vgetc().
      ins_char_typebuf(K_IGNORE, 0);
      typebuf_was_filled = TRUE;

      term = curBook->term;
      if (term) {
         update_cursor(term, term->tl_cursor_visible);
      }
   }
}

//To be called after resetting updating_screen: handle any terminal where the channel was closed.
void
term_check_channel_closed_recently(void) {
   Terminal* next_term;

   for (Terminal* term = first_term; term != NULL; term = next_term) {
      next_term = term->next;
      if (term->isChannelRecentlyClosed) {
          term->isChannelRecentlyClosed = FALSE;
          if (term_after_channel_closed(term))
         // start over, the list may have changed
         next_term = first_term;
      }
   }
}

//Fill one screen line from a line of the terminal. Advances "pos" to past the last column.
private void
term_line2screenline(
   Terminal* term,
   Portal* po,
   VTermScreen* screen,
   VTermPos* pos,
   Unt max_col
) {
   int off = screen_get_current_line_off();

   for (pos->col = 0; pos->col < max_col; ) {
      VTermScreenCell cell;
      int      c;

      if (vterm_screen_get_cell(screen, *pos, &cell) == 0)
          CLEAR_FIELD(cell);

      c = cell.chars[0];
      if (c == ZERO) {
         screenLinesG[off] = ' ';
         screenLinesUCG[off] = ZERO;
      } else {
         int i;

         // composing chars
         for (i = 0; i < MAX_COMBINED_SYMBOLS && i + 1 < VTERM_MAX_CHARS_PER_CELL; ++i) {
            screenLinesCG[i][off] = cell.chars[i + 1];
            if (cell.chars[i + 1] == 0)
               break;
         }
         if (c >= 0x80 || (MAX_COMBINED_SYMBOLS > 0 && screenLinesCG[0][off] != 0)) {
             screenLinesG[off] = ' ';
             screenLinesUCG[off] = c;
         }
         else {
             screenLinesG[off] = c;
             screenLinesUCG[off] = ZERO;
         }
      }
      screenDecosG[off] = cellToDecoration(term, po, &cell.attrs, &cell.fg, &cell.bg);

      ++pos->col;
      ++off;
      if (cell.width == 2) {
         screenLinesUCG[off] = ZERO;
         screenLinesG[off] = ZERO;

         ++pos->col;
         ++off;
      }
   }
}

//Return TRUE if portal "po" is to be redrawn with term_update_window().
//Return FALSE when there is no terminal running in this portal or it is in Terminal-Normal mode.
int
termDoUpdatePortal(Portal* po) {
   Terminal* term = po->book->term;
   return term != NULL && term->vterm != NULL && !term->isNormalMode;
}

// Called to update a portal that contains an active terminal.
void
termUpdatePortal(Portal* po) {
   Terminal* term = po->book->term;
   VTerm   *vterm;
   VTermScreen *screen;
   VTermState   *state;
   VTermPos   pos;
   Portal   *twp;

   vterm = term->vterm;
   screen = vterm_obtain_screen(vterm);
   state = vterm_obtain_state(vterm);

   // We use UPD_NOT_VALID on a resize or scroll, redraw everything then.
   // With UPD_SOME_VALID only redraw what was marked dirty.
   if (po->redrawType > UPD_SOME_VALID) {
      term->dirtyRowStart = 0;
      term->dirtyRowEnd = MAX_ROW;

      if (term->postponedScroll > 0 && term->postponedScroll < term->rows / 3)
         // Scrolling is usually faster than redrawing, when there are only
         // a few lines to scroll.
         term_scroll_up(term, 0, term->postponedScroll);
      term->postponedScroll = 0;
   }

   //If the portal was resized a redraw will be triggered and we get here.
   //Adjust the size of the vterm unless 'termwinsize' specifies a fixed size.
   Unt rows, cols;
   Boole minsize = parse_termwinsize(po, &rows, &cols);

   Unt newrows = 99999;
   Unt newcols = 99999;
   for (twp = firstPor; ; twp = twp->next) {
      // Always use curPor, it may be a popup portal.
      Portal *wwp = twp == NULL ? curPor : twp;

      // When more than one portal shows the same terminal, use the smallest size.
      if (wwp->book == term->book) {
         newrows = MIN(newrows, wwp->height);
         newcols = MIN(newcols, wwp->width);
      }
      if (!twp)
         break;
   }
   if (newrows == 99999 || newcols == 99999)
      return; // safety exit
   newrows = rows == 0 ? newrows : (minsize ? MAX(rows, newrows) : rows);
   newcols = cols == 0 ? newcols : (minsize ? MAX(cols, newcols) : cols);

   // If no cell is visible there is no point in resizing.  Also, vterm can't
   // handle a zero height.
   if (newrows == 0 || newcols == 0)
      return;

   if (term->rows != newrows || term->cols != newcols) {
      term->vterm_size_changed = TRUE;
      vterm_set_size(vterm, newrows, newcols);
      ch_log(term->job->jv_channel, "Resizing terminal to %d lines", newrows);
      term_report_winsize(term, newrows, newcols);

      //Updating the terminal size will cause the snapshot to be cleared.
      //When not in terminal_loop() we need to restore it.
      if (term != in_terminal_loop)
          may_move_terminal_to_buffer(term, FALSE);
   }

   // The cursor may have been moved when resizing.
   vterm_state_get_cursorpos(state, &pos);
   position_cursor(po, &pos);

   for (pos.row = term->dirtyRowStart; 
        pos.row < (Unt)term->dirtyRowEnd && pos.row < (Unt)po->height; 
        ++pos.row
   ){
      if (pos.row < term->rows) {
         Unt max_col = MIN(po->width, term->cols);
         term_line2screenline(term, po, screen, &pos, max_col);
      } else
         pos.col = 0;

      screen_line(
         po->portalRow + pos.row, po->portalCol, pos.col, po->width, -1, 
         popup_is_popup(po) ? SLF_POPUP : 0
      );
   }
}

//Called after updating all portals: may reset dirty rows.
void
termDidUpdatePortal(Portal* po) {
   Terminal* term = po->book->term;

   if (!term || !term->vterm || term->isNormalMode || po->redrawType != 0)
      return;

   term->dirtyRowStart = MAX_ROW;
   term->dirtyRowEnd = 0;
}

//Return TRUE if "po" is a terminal portals where the job has finished.
int
term_is_finished(Book* book) {
   return book->term && book->term->vterm == NULL;
}

//Return TRUE if "po" is a terminal portals where the job has finished or we
//are in Terminal-Normal mode, thus we show the buffer contents.
int
term_shobuffer(Book* book) {
   Terminal* term = book->term;

   return term && (term->vterm == NULL || term->isNormalMode);
}

//The current book is going to be changed. If there is terminal hiliting, remove it now.
void
uiBeforeLeavingTerminal(void) {
   Terminal* term = curBook->term;

   if (!term_is_finished(curBook) || term->scrollback.len <= 0)
      return;

   free_scrollback(term);
   drawBookLater(term->book, UPD_NOT_VALID);

   //The book is now like a normal book, it cannot be easily abandoned when changed.
   optChangeStringOptionDirect(S"booktype", BOOK_NORMAL, OPT_LOCAL, 0);
}

//Get the screen decoration for a position in the buffer. Use a negative "col" to get the 
//filler bg color
Decoration
termGetDeco(Portal* po, LineNr lnum, int col) {
   Book* book = po->book;
   Terminal* term = book->term;
   ScrollbackLine* line;
   CellDeco* cellattr;

   if (lnum > term->scrollback.len)
      cellattr = &term->cellDeco;
   else {
      line = (ScrollbackLine *)term->scrollback.c + lnum - 1;
      if (col < 0 || (Unt)col >= line->cols)
         cellattr = &line->sb_fillDeco;
      else
         cellattr = line->sb_cells + col;
   }
   return cellToDecoration(term, po, &cellattr->flags, &cellattr->fg, &cellattr->bg);
}

//Convert a cterm color number 0 - 255 to RGB. This is compatible with xterm.
private void
cterm_color2vterm(int nr, VTermColor *rgb) {
   cterm_color2rgb(nr, &rgb->red, &rgb->green, &rgb->blue, &rgb->index);
   if (rgb->index == 0)
      rgb->type = VTERM_COLOR_RGB;
   else {
      rgb->type = VTERM_COLOR_INDEXED;
      --rgb->index;
   }
}

//Initialize vterm color from the synID. Return TRUE if color is set to "fg" and "bg", or FALSE
private int
get_vterm_color_from_synid(int id, VTermColor* fg, VTermColor* bg) {
   UiColor fgRgb = INVALCOLOR;
   UiColor bgRgb = INVALCOLOR;

   if (id > 0)
      syn_id2colors(id, OUT &fgRgb, OUT &bgRgb);

   if (fgRgb != INVALCOLOR) {
      Ulong rgb = GUI_MCH_GET_RGB(fgRgb);
      fg->red = (unsigned)(rgb >> 16);
      fg->green = (unsigned)(rgb >> 8) & 255;
      fg->blue = (unsigned)rgb & 255;
      fg->type = VTERM_COLOR_RGB | VTERM_COLOR_DEFAULT_FG;
   } else
      fg->type = VTERM_COLOR_INVALID | VTERM_COLOR_DEFAULT_FG;

   if (bgRgb != INVALCOLOR) {
      Ulong rgb = GUI_MCH_GET_RGB(bgRgb);
      bg->red = (unsigned)(rgb >> 16);
      bg->green = (unsigned)(rgb >> 8) & 255;
      bg->blue = (unsigned)rgb & 255;
      bg->type = VTERM_COLOR_RGB | VTERM_COLOR_DEFAULT_BG;
   } else
      bg->type = VTERM_COLOR_INVALID | VTERM_COLOR_DEFAULT_BG;

   return TRUE;
}

void
termResetPortcolor(Portal *po) {
   po->termHiliteGroupName.fg.type = VTERM_COLOR_INVALID | VTERM_COLOR_DEFAULT_FG;
   po->termHiliteGroupName.bg.type = VTERM_COLOR_INVALID | VTERM_COLOR_DEFAULT_BG;
}

//Cache the color of 'portcolor'.
void
termUpdatePortcolor(Portal* po) {
   int id = 0;

   if (po->o.hiliteGroupName)
      id = hiliteGroupByName(mbText(po->o.hiliteGroupName));
   if (id == 0 
         || !get_vterm_color_from_synid(id, &po->termHiliteGroupName.fg, &po->termHiliteGroupName.bg)
   )
      termResetPortcolor(po);
}

//Called when any hilite group is changed
void
termUpdatePortColorAll(void) {
   int did_curPor = FALSE;

   Portal* po = null;
   while (forAllPortalsAndCurPort(OUT &po, OUT &did_curPor))
      termUpdatePortcolor(po);
}

//Initialize term->cellDeco from the environment.
private void
init_default_colors(Terminal* term) {
   int fgval, bgval;
   int id;

   CLEAR_FIELD(term->cellDeco.flags);
   term->cellDeco.width = 1;
   VTermColor* fg = &term->cellDeco.fg;
   VTermColor* bg = &term->cellDeco.bg;

   // Vterm uses a default black background. Set it to white when 'liteTheme' is set
   fgval = 255;
   bgval = 0;
   fg->red = fg->green = fg->blue = fgval;
   bg->red = bg->green = bg->blue = bgval;
   fg->type = VTERM_COLOR_RGB | VTERM_COLOR_DEFAULT_FG;
   bg->type = VTERM_COLOR_RGB | VTERM_COLOR_DEFAULT_BG;

   // The highlight group overrules the defaults.
   id = term_get_highlight_id(term, NULL);

   if (!get_vterm_color_from_synid(id, fg, bg)) {

      if (defaultFgColorG > 0) {
         cterm_color2vterm(defaultFgColorG - 1, fg);
      } else
        term_get_fg_color(&fg->red, &fg->green, &fg->blue);

      if (defaultBgColorG > 0) {
         cterm_color2vterm(defaultBgColorG - 1, bg);
      } else
         term_get_bg_color(&bg->red, &bg->green, &bg->blue);
   }
}

//Set the 16 ANSI colors from array of RGB values
private void
set_vterm_palette(VTerm* vterm, Ulong* rgb) {
   int index = 0;
   VTermState* state = vterm_obtain_state(vterm);

   for (; index < 16; index++) {
      VTermColor   color;
      color.type = VTERM_COLOR_RGB;
      color.red = (unsigned)(rgb[index] >> 16);
      color.green = (unsigned)(rgb[index] >> 8) & 255;
      color.blue = (unsigned)rgb[index] & 255;
      color.index = 0;
      vterm_state_set_palette_color(state, index, &color);
   }
}

//Set the ANSI color palette from a list of colors
private int
set_ansi_colors_list(VTerm* vterm, List* list) {
   int      n = 0;
   Ulong   rgb[16];

   ListItem* li;
   for (li = list->first; li != NULL && n < 16; li = li->next, n++) {
      CS colorName = convertVarToStringSingleUse(&li->c);
      if (!colorName)
         return FAIL;

      UiColor uiColor = hiColorByName(text(colorName));
      if (uiColor == INVALCOLOR)
         return FAIL;

      rgb[n] = GUI_MCH_GET_RGB(uiColor);
   }

   if (n != 16 || li != NULL)
      return FAIL;

   set_vterm_palette(vterm, rgb);

   return OK;
}

//Initialize the ANSI color palette from g:terminal_ansi_colors[0:15]
private void
init_vterm_ansi_colors(VTerm *vterm) {
   DictItem   *var = findVar(S"g:terminal_ansi_colors", true);

   if (!var)
      return;

   if (var->c.tag != VAR_LIST
        || var->c.list == NULL
        || var->c.list->first == &range_list_item
        || set_ansi_colors_list(vterm, var->c.list) == FAIL)
      showErrFmtMsg(_(e_invalid_argument_str), "g:terminal_ansi_colors");
}

//Handles a "drop" command from the job in the terminal. "item" is the file name, 
//"item->next" may have options.
private void
handle_drop_command(ListItem* item) {
   CS fname = tv_get_string(&item->c);
   ListItem* opt_item = item->next;
   Portal* po;
   Tab* tp;
   Invocation   invo;
   Byte* tofree = NULL;

   int bufnr = bookOpen(fname, BLN_LISTED | BLN_NOOPT);
   FOR_ALL_TAB_PORTALS(tp, po) {
      if (po->book->fiNum == bufnr) {
          // buffer is in a portal already, go there
          goto_tab_port(tp, po);
          return;
      }
   }

   CLEAR_FIELD(invo);

   if (opt_item && opt_item->c.tag == VAR_BAG && opt_item->c.bag != NULL) {
      Bag* dict = opt_item->c.bag;
      CS p = bagGetString(dict, tConst("bad"), false);
      if (p)
         get_bad_opt(p, &invo);

      if (bagHasKey(dict, tConst("bin")))
          invo.force_bin = FORCE_BIN;
      if (bagHasKey(dict, tConst("binary")))
          invo.force_bin = FORCE_BIN;
      if (bagHasKey(dict, tConst("nobin")))
          invo.force_bin = FORCE_NOBIN;
      if (bagHasKey(dict, tConst("nobinary")))
          invo.force_bin = FORCE_NOBIN;
   }

   // open in new portal, like ":split fname"
   if (!invo.comm)
      invo.comm = (CS)"split";
   invo.arg = fname;
   invo.id = C_split;
   c_splitview(&invo);

   eeglFree(tofree);
}

//Return TRUE if "func" starts with "pat" and "pat" isn't empty.
private int
is_permitted_term_api(CS func, CS pat) {
   return pat != NULL && *pat != ZERO && STRNICMP(func, pat, STRLEN(pat)) == 0;
}

//Handles a function call from the job running in a terminal.
//"item" is the function name, "item->next" has the arguments.
private void
handle_call_command(Terminal* term, Channel* channel, ListItem* item) {
   Var argvars[2];
   Var returnVar;
   FnExe funcexe;

   if (item->next == NULL) {
      ch_log(channel, "Missing function arguments for call");
      return;
   }
   CS func = tv_get_string(&item->c);

   if (!is_permitted_term_api(func, term->tl_api)) {
      ch_log(channel, "Unpermitted function: %s", func);
      return;
   }

   argvars[0].tag = VAR_NUMBER;
   argvars[0].number = term->book->fiNum;
   argvars[1] = item->next->c;
   CLEAR_FIELD(funcexe);
   funcexe.fe_firstline = 1L;
   funcexe.fe_lastline = 1L;
   funcexe.fe_evaluate = TRUE;
   if (call_func(func, -1, &returnVar, 2, argvars, &funcexe) == OK) {
      clearVar(&returnVar);
      ch_log(channel, "Function %s called", func);
   } else
      ch_log(channel, "Calling function %s failed", func);
}

// URL decoding (also know as Percent-encoding).
//
// Note this function currently is only used for decoding shell's OSC 7 escape sequence which we 
// can assume all bytes are valid UTF-8 bytes. Thus we don't need to deal with invalid UTF-8
// encoding bytes like 0xfe, 0xff.
private Unt
url_decode(const char *src, const Unt len, CS dst) {
   Unt i = 0, j = 0;

   while (i < len) {
      if (src[i] == '%' && i + 2 < len) {
          dst[j] = hexhex2nr((CS)&src[i + 1]);
          j++;
          i += 3;
      } else {
          dst[j] = src[i];
          i++;
          j++;
      }
   }
   dst[j] = '\0';
   return j;
}

//Sync terminal buffer's cwd with shell's pwd with the help of OSC 7.
//
//The OSC 7 sequence has the format of "\033]7;file://HOSTNAME/CURRENT/DIR\033\\"
//and what VTerm provides via VTermStringFragment is "file://HOSTNAME/CURRENT/DIR"
private void
sync_shell_dir(ArrayList* gap) {
   int offset = 7;  // len of "file://" is 7
   char* pos = (char *)gap->c + offset;
   CS new_dir;

   // remove HOSTNAME to get PWD
   for (; offset < (int)gap->len && *pos != '/'; ++offset, ++pos ) 
      {}

   if (offset >= (int)gap->len) {
      showErrFmtMsg(_(e_failed_to_extract_pwd_from_str_check_your_shell_config), gap->c);
      return;
   }

   new_dir = alloc(gap->len - offset + 1);
   url_decode(pos, gap->len-offset, new_dir);
   changedir_func(new_dir, CDSCOPE_WINDOW);
   eeglFree(new_dir);
}

// Called by libvterm when it cannot recognize an OSC sequence. We recognize a terminal API command
private int
parse_osc(int command, VTermStringFragment frag, void *user) {
   Terminal* term = (Terminal *)user;
   JsReader reader;
   Var   tv;
   Channel* channel = term->job == NULL ? NULL : term->job->jv_channel;
   ArrayList* gap = &term->oscBuilder;

   // We recognize only OSC 5 1 ; {command} and OSC 7 ; {command}
   if (command != 51 && (command != 7 || !p_asd))
      return 0;

   // Concatenate what was received until the final piece is found.
   if (ga_grow(gap, (int)frag.len + 1) == FAIL) {
      ga_clear(gap);
      return 1;
   }
   mch_memmove((char *)gap->c + gap->len, frag.str, frag.len);
   gap->len += (int)frag.len;
   if (!frag.final)
      return 1;

   ((char *)gap->c)[gap->len] = 0;

   if (command == 7) {
      sync_shell_dir(gap);
      ga_clear(gap);
      return 1;
   }

   reader.js_buf = gap->c;
   reader.js_fill = NULL;
   reader.js_used = 0;
   if (json_decode(OUT &tv, &reader) == OK && tv.tag == VAR_LIST && tv.list != NULL) {
      ListItem *item = tv.list->first;

      if (!item)
         ch_log(channel, "Missing command");
      else {
         CS comm = tv_get_string(&item->c);

         // Make sure an invoked command doesn't delete the buffer (and the
         // terminal) under our fingers.
         ++term->book->locked;

         item = item->next;
         if (!item)
            ch_log(channel, "Missing argument for %s", comm);
         ei (STRCMP(comm, "drop") == 0)
            handle_drop_command(item);
         ei (STRCMP(comm, "call") == 0)
            handle_call_command(term, channel, item);
         else
            ch_log(channel, "Invalid command received: %s", comm);
          --term->book->locked;
      }
   } else
      ch_log(channel, "Invalid JSON received");

   ga_clear(gap);
   clearVar(&tv);
   return 1;
}

//Called when we cannot recognize a CSI sequence. We recognize the portal position report.
private int
parse_csi(
   CS leader UNUSED,
   const long  args[],
   int argcount,
   CS intermed UNUSED,
   Byte command,
   void* user
){
   Terminal* term = (Terminal *)user;
   int len;
   int x = 0;
   int y = 0;
   Portal* po;

   // We recognize only CSI 13 t
   if (command != 't' || argcount != 1 || args[0] != 13)
      return 0; // not handled

   // When getting the portal position is not possible or it fails it results in 0/0.
   (void)uiGetPortPos(&x, &y, (Long)100);

   FOR_ALL_PORTALS(po) {
      if (po->book == term->book)
         break;
   } 
   if (po) {
       // We roughly estimate the position of the terminal portal inside
       // the Eegl portal by assuming a 10 x 7 character cell.
       x += po->portalCol * 7;
       y += po->portalRow * 10;
   }

   Byte buf[100];
   len = eeSnprintf(buf, 100, "\x1b[3;%d;%dt", x, y);
   channel_send(term->job->jv_channel, get_tty_part(term), buf, len, NULL);
   return 1;
}

private VTermStateFallbacks state_fallbacks = {
   NULL,      // control
   parse_csi,      // csi
   parse_osc,      // osc
   NULL,      // dcs
   NULL,      // apc
   NULL,      // pm
   NULL      // sos
};

// Use Eegl's allocation functions for vterm so profiling works.
private void *
vterm_malloc(Unt size, void* data UNUSED) {
   // make sure that the length is not zero
   return allocZeroed(size == 0 ? 1L : size);
}

private void
vterm_memfree(void *ptr, void *data UNUSED) {
   eeglFree(ptr);
}

private VTermAllocatorFunctions vterm_allocator = {
   &vterm_malloc,
   &vterm_memfree
};

// Create a new vterm and initialize it. Return FAIL when out of memory.
private int
create_vterm(Terminal* term, int rows, int cols) {
   VTermValue       value;

   VTerm* vterm = vterm_new_with_allocator(rows, cols, &vterm_allocator, NULL);
   term->vterm = vterm;
   if (!vterm)
      return FAIL;

   // Allocate screen and state here, so we can bail out if that fails.
   VTermState* state = vterm_obtain_state(vterm);
   VTermScreen* screen = vterm_obtain_screen(vterm);
   if (state == NULL || screen == NULL) {
      vterm_free(vterm);
      return FAIL;
   }

   vterm_screen_set_callbacks(screen, &screen_callbacks, term);

   init_default_colors(term);

   vterm_state_set_default_colors(state, &term->cellDeco.fg, &term->cellDeco.bg);

   //Required to initialize most things.
   vterm_screen_reset(screen, 1); // hard reset

   //Allow using alternate screen.
   vterm_screen_enable_altscreen(screen, 1);

   //Do not use a blinking cursor. In an xterm this causes the cursor to blink if it's blinking in 
   //the xterm. For Portals we respect the system wide setting.
   value.boolean = 0;
   vterm_state_set_termprop(state, VTERM_PROP_CURSORBLINK, &value);
   vterm_state_set_unrecognized_fallbacks(state, &state_fallbacks, term);

   return OK;
}

// Reset the terminal palette to its default value.
private void
term_reset_palette(VTerm* vterm) {
   VTermState* state = vterm_obtain_state(vterm);

   for (int index = 0; index < 16; index++) {
      VTermColor color;
      color.type = VTERM_COLOR_INDEXED;
      ansi_color2rgb(index, OUT &color.red, OUT &color.green, OUT &color.blue, OUT &color.index);
      // The first valid index starts at 1.
      color.index -= 1;

      vterm_state_set_palette_color(state, index, &color);
   }
}

private void
term_update_palette(Terminal* term) {
   if (term->palette || findVar(S"g:terminal_ansi_colors", true) != NULL
   ) {
      if (term->palette != NULL)
         set_vterm_palette(term->vterm, term->palette);
      else
         init_vterm_ansi_colors(term->vterm);
   } else
      term_reset_palette(term->vterm);
}

// Called when option 'termguicolors' is changed.
void
term_update_palette_all(void) {
   Terminal *term;

   FOR_ALL_TERMS(term) {
      if (term->vterm == NULL)
          continue;
      term_update_palette(term);
   }
}

// Called when option 'liteTheme' was set, or when any hilite is changed.
void
term_update_colors_all(void) {
   Terminal* term;

   FOR_ALL_TERMS(term) {
      if (term->vterm == NULL)
         continue;
      init_default_colors(term);
      vterm_state_set_default_colors(
         vterm_obtain_state(term->vterm), &term->cellDeco.fg, &term->cellDeco.bg
      );
   }
}

// Return the text to show for the book name and status.
CS
term_get_status_text(Terminal* term) {
   if (term->tl_status_text)
      return term->tl_status_text;

   CS txt;
   if (term->isNormalMode) {
      if (term_job_running(term))
          txt = (CS)_("Terminal");
      else
          txt = (CS)_("Terminal-finished");
   } ei (term->title != NULL)
      txt = term->title;
   ei (term_none_open(term))
      txt = (CS)_("active");
   ei (term_job_running(term))
      txt = (CS)_("running");
   else
      txt = (CS)_("finished");
   CS fname = bookGetFname(term->book);
   Unt len = 9 + STRLEN(fname) + STRLEN(txt);
   term->tl_status_text = alloc(len);
   if (term->tl_status_text != NULL)
      eeSnprintf(term->tl_status_text, len, "%s [%s]", fname, txt);
   return term->tl_status_text;
}

// Clear the cached value of the status text.
void
term_clear_status_text(Terminal* term) {
   EE_CLEAR(term->tl_status_text);
}

// Mark references in jobs of terminals.
int
set_ref_in_term(int copyID) {
   int      abort = FALSE;

   for (Terminal* term = first_term; !abort && term; term = term->next) {
      if (term->job) {
         Var tv;
         tv.tag = VAR_JOB;
         tv.job = term->job;
         abort = abort || set_ref_in_item(&tv, copyID, NULL, NULL);
      }
   } 
   return abort;
}

// Get the buffer from the first argument in "argvars".
// Return NULL when the buffer is not for a terminal portal and logs a message with "where".
private Book *
term_get_buf(Var* argvars, CS where) {
   ++emsg_off;
   Book* book = daGetBook(&argvars[0], FALSE);
   --emsg_off;
   if (!book || !book->term) {
      (void)tv_get_number(&argvars[0]);    // issue errmsg if type error
      lo("%s: invalid buffer argument", where);
      return NULL;
   }
   return book;
}

private void
clear_cell(VTermScreenCell* cell) {
   CLEAR_FIELD(*cell);
   cell->fg.type = VTERM_COLOR_INVALID | VTERM_COLOR_DEFAULT_FG;
   cell->bg.type = VTERM_COLOR_INVALID | VTERM_COLOR_DEFAULT_BG;
}

private void
dump_term_color(FILE *fd, VTermColor *color) {
   int index;

   if (VTERM_COLOR_IS_INDEXED(color))
      index = color->index + 1;
   ei (color->type == 0)
      // use RGB values
      index = 255;
   else
      // default color
      index = 0;
   fprintf(fd, "%02x%02x%02x%d", (int)color->red, (int)color->green, (int)color->blue, index);
}

// "term_dumpwrite(book, filename, options)" function
//
// Each screen cell in full is:
//    |{characters}+{decorations}#{fg-color}{color-idx}#{bg-color}{color-idx}
// {characters} is a space for an empty cell
// For a double-width character "+" is changed to "*" and the next cell is
// skipped.
// {decorations} is the decimal value of HL_BOLD + HL_UNDERLINE, etc.
//           when "&" use the same as the previous cell.
// {fg-color} is hex RGB, when "&" use the same as the previous cell.
// {bg-color} is hex RGB, when "&" use the same as the previous cell.
// {color-idx} is a number from 0 to 255
//
// Screen cell with same width, decorations and color as the previous one:
//    |{characters}
//
// To use the color of the previous cell, use "&" instead of {color}-{idx}.
//
// Repeating the previous screen cell:
//    @{count}
void
f_term_dumpwrite(Var* argvars, Var* returnVar UNUSED) {
   Unt max_height = 0;
   Unt max_width = 0;
   FileStat   st;
   FILE   *fd;
   VTermPos   cursor_pos;

   Book* book = term_get_buf(argvars, S"term_dumpwrite()");
   if (!book)
      return;
   Terminal* term = book->term;
   if (term->vterm == NULL) {
      emsg(_(e_job_already_finished));
      return;
   }

   if (argvars[2].tag != VAR_UNKNOWN) {
      if (check_for_dict_arg(argvars, 2) == FAIL)
         return;
      Bag* d = argvars[2].bag;
      if (d) {
         max_height = bagGetNumber(d, tConst("rows"));
         max_width = bagGetNumber(d, tConst("columns"));
      }
   }

   CS fname = convertVarToStringSingleUse(&argvars[1]);
   if (!fname)
      return;
   if (stat((char *)fname, &st) >= 0) {
      showErrFmtMsg(_(e_file_exists_str), fname);
      return;
   }

   if (*fname == ZERO || (fd = fopen((char *)fname, WRITEBIN)) == NULL) {
      showErrFmtMsg(_(e_cant_create_file_str), *fname == ZERO ? (CS)_("<empty>") : fname);
      return;
   }

   VTermScreenCell prev_cell;
   clear_cell(&prev_cell);

   VTermScreen* screen = vterm_obtain_screen(term->vterm);
   VTermState* state = vterm_obtain_state(term->vterm);
   vterm_state_get_cursorpos(state, &cursor_pos);
   
   VTermPos pos;
   for (pos.row = 0;
        (max_height == 0 || pos.row < max_height) && pos.row < term->rows;
        ++pos.row
   ){
      int repeat = 0;

      for (pos.col = 0; 
         (max_width == 0 || pos.col < max_width) && pos.col < term->cols; 
         ++pos.col
      ){
         VTermScreenCell cell;
         Boole sameFlags;
         int same_chars = TRUE;
         int i;
         int is_cursor_pos = (pos.col == cursor_pos.col && pos.row == cursor_pos.row);

         if (vterm_screen_get_cell(screen, pos, &cell) == 0)
            clear_cell(&cell);

         for (i = 0; i < VTERM_MAX_CHARS_PER_CELL; ++i) {
            int c = cell.chars[i];
            int pc = prev_cell.chars[i];
            int should_break = c == ZERO || pc == ZERO;

            // For the first character ZERO is the same as space.
            if (i == 0) {
                c = (c == ZERO) ? ' ' : c;
                pc = (pc == ZERO) ? ' ' : pc;
            }
            if (c != pc)
               same_chars = FALSE;
            if (should_break)
               break;
         }
         sameFlags = convertDecoFlagsFromVterm(&cell.attrs) == convertDecoFlagsFromVterm(&prev_cell.attrs)
            && vterm_color_is_equal(&cell.fg, &prev_cell.fg)
            && vterm_color_is_equal(&cell.bg, &prev_cell.bg);
         if (same_chars && cell.width == prev_cell.width && sameFlags && !is_cursor_pos) {
            ++repeat;
         } else {
            if (repeat > 0) {
               fprintf(fd, "@%d", repeat);
               repeat = 0;
            }
            fputs(is_cursor_pos ? ">" : "|", fd);

            if (cell.chars[0] == ZERO)
               fputs(" ", fd);
            else {
               Byte charbuf[10];
               int len;

               for (i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i] != ZERO; ++i) {
                  len = mb_char2bytes(cell.chars[i], charbuf);
                  fwrite(charbuf, len, 1, fd);
               }
            }

            // When only the characters differ we don't write anything, the
            // following "|", "@" or NL will indicate using the same decorations.
            if (cell.width != prev_cell.width || !sameFlags) {
               if (cell.width == 2)
                  fputs("*", fd);
               else
                  fputs("+", fd);

               if (sameFlags) {
                  fputs("&", fd);
               } else {
                  fprintf(fd, "%d", convertDecoFlagsFromVterm(&cell.attrs));
                  if (vterm_color_is_equal(&cell.fg, &prev_cell.fg))
                     fputs("&", fd);
                  else {
                     fputs("#", fd);
                     dump_term_color(fd, &cell.fg);
                  }
                  if (vterm_color_is_equal(&cell.bg, &prev_cell.bg))
                     fputs("&", fd);
                  else {
                     fputs("#", fd);
                     dump_term_color(fd, &cell.bg);
                  }
               }
            }

            prev_cell = cell;
         }

         if (cell.width == 2)
            ++pos.col;
      }
      if (repeat > 0)
         fprintf(fd, "@%d", repeat);
      fputs("\n", fd);
   }

   fclose(fd);
}

// Called when a dump is corrupted.  Put a breakpoint here when debugging.
private void
dump_is_corrupt(ArrayList *gap) {
   ga_concat(gap, S"CORRUPT");
}

private void
append_cell(ArrayList *gap, CellDeco *cell) {
   if (ga_grow(gap, 1) == FAIL)
      return;

   *(((CellDeco *)gap->c) + gap->len) = *cell;
   ++gap->len;
}

private void
clear_cellattr(CellDeco *cell) {
   CLEAR_FIELD(*cell);
   cell->fg.type = VTERM_COLOR_DEFAULT_FG;
   cell->bg.type = VTERM_COLOR_DEFAULT_BG;
}

// Read the dump file from "fd" and append lines to the current buffer.
// Return the cell width of the longest line.
private Unt
read_dump_file(FILE *fd, VTermPos *cursor_pos) {
   ArrayList       ga_text;
   ArrayList       ga_cell;
   Byte* prev_char = NULL;
   char decoFlags = 0;
   CellDeco       cell;
   CellDeco       empty_cell;
   Terminal       *term = curBook->term;
   int          max_cells = 0;
   int          start_row = term->scrollback.len;

   ga_init2(&ga_text, 1, 90);
   ga_init2(&ga_cell, sizeof(CellDeco), 90);
   clear_cellattr(&cell);
   clear_cellattr(&empty_cell);
   cursor_pos->row = UNT;
   cursor_pos->col = UNT;

   Unt c = fgetc(fd);
   for (;;) {
      if (c == (Unt)EOF)
          break;
      if (c == '\r') {
         // DOS line endings?  Ignore.
         c = fgetc(fd);
      } ei (c == '\n') {
         // End of a line: append it to the buffer.
         if (ga_text.c == NULL)
            dump_is_corrupt(&ga_text);
         if (ga_grow(&term->scrollback, 1) == OK) {
            ScrollbackLine   *line = (ScrollbackLine *)term->scrollback.c + term->scrollback.len;

            if (max_cells < ga_cell.len)
                max_cells = ga_cell.len;
            line->cols = ga_cell.len;
            line->sb_cells = ga_cell.c;
            line->sb_fillDeco = term->cellDeco;
            ++term->scrollback.len;
            ga_init(&ga_cell);

            ga_append(&ga_text, ZERO);
            ml_append(curBook->mem.lineCount, ga_text.c, ga_text.len, FALSE);
         } else
            ga_clear(&ga_cell);
          ga_text.len = 0;

          c = fgetc(fd);
      } ei (c == '|' || c == '>') {
         int prev_len = ga_text.len;

         if (c == '>') {
            if (cursor_pos->row != UNT)
               dump_is_corrupt(&ga_text);   // duplicate cursor
            cursor_pos->row = term->scrollback.len - start_row;
            cursor_pos->col = ga_cell.len;
         }

         // normal character(s) followed by "+", "*", "|", "@" or NL
         c = fgetc(fd);
         if (c != (Unt)EOF)
            ga_append(&ga_text, c);
         for (;;) {
            c = fgetc(fd);
            if (c == '+' || c == '*' || c == '|' || c == '>' || c == '@' || c == (Unt)EOF || c == '\n')
               break;
            ga_append(&ga_text, c);
         }

         // save the character for repeating it
         eeglFree(prev_char);
         if (ga_text.c)
            prev_char = copySubstr(((CS)ga_text.c) + prev_len, ga_text.len - prev_len);

         if (c == '@' || c == '|' || c == '>' || c == '\n') {
            // use all decorations from previous cell
         } ei (c == '+' || c == '*') {
            int is_bg;

            cell.width = c == '+' ? 1 : 2;

            c = fgetc(fd);
            if (c == '&') {
               // use same deco as previous cell
               c = fgetc(fd);
            } ei (SAFE_isdigit(c)) {
               // get the decimal decoration
               decoFlags = 0;
               while (SAFE_isdigit(c)) {
                  decoFlags = decoFlags * 10 + (c - '0');
                  c = fgetc(fd);
               }
               copyDecorationsToVterm(OUT &cell, decoFlags);

               // is_bg == 0: fg, is_bg == 1: bg
               for (is_bg = 0; is_bg <= 1; ++is_bg) {
                  if (c == '&') {
                     // use same color as previous cell
                     c = fgetc(fd);
                  } ei (c == '#') {
                     int red, green, blue, index = 0, type;
                     c = fgetc(fd);
                     red = hex2nr(c);
                     c = fgetc(fd);
                     red = (red << 4) + hex2nr(c);
                     c = fgetc(fd);
                     green = hex2nr(c);
                     c = fgetc(fd);
                     green = (green << 4) + hex2nr(c);
                     c = fgetc(fd);
                     blue = hex2nr(c);
                     c = fgetc(fd);
                     blue = (blue << 4) + hex2nr(c);
                     c = fgetc(fd);
                     if (!SAFE_isdigit(c))
                        dump_is_corrupt(&ga_text);
                     while (SAFE_isdigit(c)) {
                        index = index * 10 + (c - '0');
                        c = fgetc(fd);
                     }
                     if (index == 0 || index == 255) {
                        type = VTERM_COLOR_RGB;
                        if (index == 0) {
                           if (is_bg)
                              type |= VTERM_COLOR_DEFAULT_BG;
                           else
                              type |= VTERM_COLOR_DEFAULT_FG;
                        }
                     } else {
                        type = VTERM_COLOR_INDEXED;
                        index -= 1;
                     }
                     if (is_bg) {
                        cell.bg.type = type;
                        cell.bg.red = red;
                        cell.bg.green = green;
                        cell.bg.blue = blue;
                        cell.bg.index = index;
                      } else {
                        cell.fg.type = type;
                        cell.fg.red = red;
                        cell.fg.green = green;
                        cell.fg.blue = blue;
                        cell.fg.index = index;
                     }
                  } else
                     dump_is_corrupt(&ga_text);
                }
            } else
               dump_is_corrupt(&ga_text);
         } else
            dump_is_corrupt(&ga_text);

         append_cell(&ga_cell, &cell);
         if (cell.width == 2)
            append_cell(&ga_cell, &empty_cell);
      } ei (c == '@') {
         if (prev_char == NULL)
            dump_is_corrupt(&ga_text);
         else {
            int count = 0;

            // repeat previous character, get the count
            for (;;) {
               c = fgetc(fd);
               if (!SAFE_isdigit(c))
                  break;
               count = count * 10 + (c - '0');
            }

            while (count-- > 0) {
               ga_concat(&ga_text, prev_char);
               append_cell(&ga_cell, &cell);
            }
         }
      } else {
         dump_is_corrupt(&ga_text);
         c = fgetc(fd);
      }
   }

   if (ga_text.len > 0) {
      // trailing characters after last NL
      dump_is_corrupt(&ga_text);
      ga_append(&ga_text, ZERO);
      ml_append(curBook->mem.lineCount, ga_text.c, ga_text.len, FALSE);
   }

   ga_clear(&ga_text);
   ga_clear(&ga_cell);
   eeglFree(prev_char);

   return max_cells;
}

// Return an allocated string with at least "text_width" "=" characters and
// "fname" inserted in the middle.
private CS
get_separator(int text_width, CS fname) {
   int width = MAX(text_width, (int)curPor->width);
   CS p = fname;
   int       i;
   Unt  off;

   CS textline = alloc(width + (int)STRLEN(fname) + 1);
   if (!textline)
      return NULL;

   int fname_size = eeglStrSize(fname);
   if (fname_size < width - 8) {
      // enough room, don't use the full portal width
      width = MAX(text_width, fname_size + 8);
   } ei (fname_size > width - 8) {
      // full name doesn't fit, use only the tail
      p = fiGetShortFiName(fname);
      fname_size = eeglStrSize(p);
   }
   // skip characters until the name fits
   while (fname_size > width - 8) {
      p += utfCharLen(p);
      fname_size = eeglStrSize(p);
   }

   for (i = 0; i < (width - fname_size) / 2 - 1; ++i)
      textline[i] = '=';
   textline[i++] = ' ';

   STRCPY(textline + i, p);
   off = STRLEN(textline);
   textline[off] = ' ';
   for (i = 1; i < (width - fname_size) / 2; ++i)
      textline[off + i] = '=';
   textline[off + i] = ZERO;

   return textline;
}

// Common for "term_dumpdiff()" and "term_dumpload()".
private void
term_load_dump(Arr(Var) argvars, Var* returnVar, int do_diff) {
   JobOptions   opt;
   Book* book = NULL;
   Byte buf1[NUMBUFLEN];
   Byte buf2[NUMBUFLEN];
   CS fname2 = NULL;
   CS fname_tofree = NULL;
   FILE* fd1;
   FILE* fd2 = NULL;

   // First open the files.  If this fails bail out.
   CS fname1 = convertVarToString(&argvars[0], buf1);
   if (do_diff)
      fname2 = convertVarToString(&argvars[1], buf2);
   if (fname1 == NULL || (do_diff && !fname2)) {
      emsg(_(e_invalid_argument));
      return;
   }
   fd1 = fopen((char *)fname1, READBIN);
   if (fd1 == NULL) {
      showErrFmtMsg(_(e_cant_read_file_str), fname1);
      return;
   }
   if (do_diff) {
      fd2 = fopen((char *)fname2, READBIN);
      if (fd2 == NULL) {
         fclose(fd1);
         showErrFmtMsg(_(e_cant_read_file_str), fname2);
         return;
      }
   }

   init_job_options(&opt);
   if (argvars[do_diff ? 2 : 1].tag != VAR_UNKNOWN
       && get_job_options(&argvars[do_diff ? 2 : 1], &opt, 0,
             JO2_TERM_NAME + JO2_TERM_COLS + JO2_TERM_ROWS
             + JO2_VERTICAL + JO2_CURPOR + JO2_NORESTORE) == FAIL
   )
      goto theend;

   if (opt.jo_term_name == NULL) {
      Unt len = STRLEN(fname1) + 12;

      fname_tofree = alloc(len);
      if (fname_tofree != NULL) {
         eeSnprintf(fname_tofree, len, "dump diff %s", fname1);
         opt.jo_term_name = fname_tofree;
      }
   }

   if (opt.jo_bufnr_buf != NULL) {
      Portal *po = portTryFindOpenBook(opt.jo_bufnr_buf);

      // With "bufnr" argument: enter the portal into this buffer and make it empty.
      if (!po)
         showErrFmtMsg(_(e_invalid_argument_str), "bufnr");
      else {
         book = curBook;
         while (!(curBook->mem.flags & ML_EMPTY))
            ml_delete((LineNr)1);
         free_scrollback(curBook->term);
         redraw_later(UPD_NOT_VALID);
      }
   } else
      // Create a new terminal portal.
      book = term_start(&argvars[0], NULL, &opt, TERM_START_NOJOB);

   if (!book || !book->term) {
      goto theend;
   }
   LineNr   lnum;
   Terminal      *term = book->term;
   VTermPos   cursor_pos1;
   VTermPos   cursor_pos2;

   init_default_colors(term);

   returnVar->number = book->fiNum;

   // read the files, fill the buffer with the diff
   Unt width = read_dump_file(fd1, &cursor_pos1);

   // position the cursor
   if (cursor_pos1.row != UNT) {
      curPor->cursor.lnum = cursor_pos1.row + 1;
      coladvance(cursor_pos1.col);
   }

   // Delete the empty line that was in the empty buffer.
   ml_delete(1);

   // For term_dumpload() we are done here.
   if (!do_diff)
      goto theend;

   term->topDiffRows = curBook->mem.lineCount;

   CS textline = get_separator(width, fname1);
   if (!textline)
      goto theend;
   if (add_empty_scrollback(term, &term->cellDeco, 0) == OK)
      ml_append(curBook->mem.lineCount, textline, 0, FALSE);
   eeglFree(textline);

   textline = get_separator(width, fname2);
   if (!textline)
      goto theend;
   if (add_empty_scrollback(term, &term->cellDeco, 0) == OK)
      ml_append(curBook->mem.lineCount, textline, 0, FALSE);
   textline[width] = ZERO;

   LineNr bot_lnum = curBook->mem.lineCount;
   Unt width2 = read_dump_file(fd2, &cursor_pos2);
   if (width2 > width) {
      eeglFree(textline);
      textline = alloc(width2 + 1);
      if (!textline)
         goto theend;
      width = width2;
      textline[width] = ZERO;
   }
   term->bottDiffRows = curBook->mem.lineCount - bot_lnum;

   for (lnum = 1; lnum <= term->topDiffRows; ++lnum) {
      if (lnum + bot_lnum > curBook->mem.lineCount) {
         // bottom part has fewer rows, fill with "-"
         for (Unt i = 0; i < width; ++i)
            textline[i] = '-';
      } else {
         CS line2;
         CS p1;
         CS p2;
         Unt col;
         ScrollbackLine* sb_line = (ScrollbackLine *)term->scrollback.c;
         CellDeco* cellattr1 = (sb_line + lnum - 1)->sb_cells;
         CellDeco* cellattr2 = (sb_line + lnum + bot_lnum - 1) ->sb_cells;

         // Make a copy, getting the second line will invalidate it.
         CS line1 = copyStr(ml_get(lnum));
         p1 = line1;

         line2 = ml_get(lnum + bot_lnum);
         p2 = line2;
         for (col = 0; col < width && *p1 != ZERO && *p2 != ZERO; ++col) {
            int len1 = utfCharLen(p1);
            int len2 = utfCharLen(p2);

            textline[col] = ' ';
            if (len1 != len2 || STRNCMP(p1, p2, len1) != 0)
               // text differs
               textline[col] = 'X';
            ei (lnum == cursor_pos1.row + 1
                   && col == cursor_pos1.col
                      && (cursor_pos1.row != cursor_pos2.row || cursor_pos1.col != cursor_pos2.col))
               // cursor in first but not in second
               textline[col] = '>';
            ei (lnum == cursor_pos2.row + 1
                   && col == cursor_pos2.col
                      && (cursor_pos1.row != cursor_pos2.row || cursor_pos1.col != cursor_pos2.col))
               // cursor in second but not in first
               textline[col] = '<';
            ei (cellattr1 != NULL && cellattr2 != NULL) {
               if ((cellattr1 + col)->width != (cellattr2 + col)->width)
                  textline[col] = 'w';
               ei (!vterm_color_is_equal(&(cellattr1 + col)->fg, &(cellattr2 + col)->fg))
                  textline[col] = 'f';
               ei (!vterm_color_is_equal(&(cellattr1 + col)->bg, &(cellattr2 + col)->bg))
                  textline[col] = 'b';
               ei (convertDecoFlagsFromVterm(&(cellattr1 + col)->flags)
                    != convertDecoFlagsFromVterm(&((cellattr2 + col)->flags)))
                  textline[col] = 'a';
            }
            p1 += len1;
            p2 += len2;
            // TODO: handle different width
         }

         while (col < width) {
            if (*p1 == ZERO && *p2 == ZERO)
               textline[col] = '?';
            ei (*p1 == ZERO) {
               textline[col] = '+';
               p2 += utfCharLen(p2);
            } else {
               textline[col] = '-';
               p1 += utfCharLen(p1);
            }
            ++col;
         }

         eeglFree(line1);
      }
      if (add_empty_scrollback(term, &term->cellDeco, term->topDiffRows) == OK)
         ml_append(term->topDiffRows + lnum, textline, 0, FALSE);
      ++bot_lnum;
   }

   while (lnum + bot_lnum <= curBook->mem.lineCount) {
      // bottom part has more rows, fill with "+"
      for (Unt i = 0; i < width; ++i)
         textline[i] = '+';
      if (add_empty_scrollback(term, &term->cellDeco, term->topDiffRows) == OK)
         ml_append(term->topDiffRows + lnum, textline, 0, FALSE);
      ++lnum;
      ++bot_lnum;
   }

   term->cols = width;

   // looks better without wrapping
   curPor->o.wrap = 0;

theend:
   eeglFree(textline);
   eeglFree(fname_tofree);
   fclose(fd1);
   if (fd2)
      fclose(fd2);
}

// If the current buffer shows the output of term_dumpdiff(), swap the top and bottom files.
// Return FAIL when this is not possible.
int
term_swap_diff(void) {
   Terminal* term = curBook->term;
   LineNr line_count;
   LineNr top_rows;
   LineNr bot_rows;
   LineNr bot_start;
   LineNr lnum;
   CS p;
   ScrollbackLine* sb_line;

   if (!term || !term_is_finished(curBook) || term->topDiffRows == 0 || term->scrollback.len == 0)
      return FAIL;

   line_count = curBook->mem.lineCount;
   top_rows = term->topDiffRows;
   bot_rows = term->bottDiffRows;
   bot_start = line_count - bot_rows;
   sb_line = (ScrollbackLine *)term->scrollback.c;

   // move lines from top to above the bottom part
   for (lnum = 1; lnum <= top_rows; ++lnum) {
      p = copyStr(ml_get(1));
      ml_append(bot_start, p, 0, FALSE);
      ml_delete(1);
      eeglFree(p);
   }

   // move lines from bottom to the top
   for (lnum = 1; lnum <= bot_rows; ++lnum) {
      p = copyStr(ml_get(bot_start + lnum));
      ml_delete(bot_start + lnum);
      ml_append(lnum - 1, p, 0, FALSE);
      eeglFree(p);
   }

   // move top title to bottom
   p = copyStr(ml_get(bot_rows + 1));
   ml_append(line_count - top_rows - 1, p, 0, FALSE);
   ml_delete(bot_rows + 1);
   eeglFree(p);

   // move bottom title to top
   p = copyStr(ml_get(line_count - top_rows));
   ml_delete(line_count - top_rows);
   ml_append(bot_rows, p, 0, FALSE);
   eeglFree(p);

   if (top_rows == bot_rows) {
      // rows counts are equal, can swap cell properties
      for (lnum = 0; lnum < top_rows; ++lnum) {
          ScrollbackLine   temp;

          temp = *(sb_line + lnum);
          *(sb_line + lnum) = *(sb_line + bot_start + lnum);
          *(sb_line + bot_start + lnum) = temp;
      }
   } else {
      Unt      size = sizeof(ScrollbackLine) * term->scrollback.len;
      ScrollbackLine* temp = alloc(size);

      // need to copy cell properties into temp memory
      if (temp) {
         mch_memmove(temp, term->scrollback.c, size);
         mch_memmove(term->scrollback.c, temp + bot_start, sizeof(ScrollbackLine) * bot_rows);
         mch_memmove((ScrollbackLine *)term->scrollback.c + bot_rows,
             temp + top_rows,
             sizeof(ScrollbackLine) * (line_count - top_rows - bot_rows));
         mch_memmove((ScrollbackLine *)term->scrollback.c + line_count - top_rows,
             temp,
             sizeof(ScrollbackLine) * top_rows
         );
         eeglFree(temp);
      }
   }

   term->topDiffRows = bot_rows;
   term->bottDiffRows = top_rows;

   drawUpdateScreen(UPD_NOT_VALID);
   return OK;
}

// "term_dumpdiff(filename, filename, options)" function
void
f_term_dumpdiff(Arr(Var) argvars, Var* returnVar) {
   term_load_dump(argvars, returnVar, TRUE);
}

// "term_dumpload(filename, options)" function
void
f_term_dumpload(Arr(Var) argvars, Var* returnVar) {
   term_load_dump(argvars, returnVar, FALSE);
}

// "term_getaltscreen(book)" function
void
f_term_getaltscreen(Var* argvars, Var* returnVar) {
   Book* book = term_get_buf(argvars, S"term_getaltscreen()");
   if (book == NULL)
      return;
   returnVar->number = book->term->tl_using_altscreen;
}

// "term_getcursor(book)" function
void
f_term_getcursor(Var* argvars, Var* returnVar) {
   Book* book = term_get_buf(argvars, S"term_getcursor()");
   if (!book)
      return;
      
   allocReturnList(returnVar);
   Terminal* term = book->term;

   List* l = returnVar->list;
   list_append_number(l, term->cursorPos.row + 1);
   list_append_number(l, term->cursorPos.col + 1);

   Bag* b = allocBag();

   bagAddNumber(b, S"visible", term->tl_cursor_visible);
   bagAddNumber(b, S"blink", blink_state_is_inverted()
       ? !term->tl_cursor_blink : term->tl_cursor_blink);
   bagAddNumber(b, S"shape", term->tl_cursor_shape);
   bagAddString(b, S"color", cursor_color_get(term->tl_cursor_color));
   listAppendBag(l, b);
}

// "term_getjob(book)" function
void
f_term_getjob(Arr(Var) argvars, Var* returnVar) {
   Book* book = term_get_buf(argvars, S"term_getjob()");
   if (book == NULL) {
      returnVar->tag = VAR_SPECIAL;
      returnVar->number = VVAL_NULL;
      return;
   }

   returnVar->tag = VAR_JOB;
   returnVar->job = book->term->job;
   if (returnVar->job != NULL)
      ++returnVar->job->jv_refcount;
}

private int
get_row_number(Var *tv, Terminal *term) {
   if (tv->tag == VAR_STRING && tv->string && STRCMP(tv->string, ".") == 0)
      return term->cursorPos.row;
   return (int)tv_get_number(tv) - 1;
}

// "term_getline(book, row)" function
void
f_term_getline(Arr(Var) argvars, Var* returnVar) {
   Terminal* term;
   int row;

   returnVar->tag = VAR_STRING;

   Book* book = term_get_buf(argvars, S"term_getline()");
   if (!book)
      return;
   term = book->term;
   row = get_row_number(&argvars[1], term);

   if (term->vterm == NULL) {
      LineNr lnum = row + term->scrollbackScrolled + 1;

      // vterm is finished, get the text from the book
      if (lnum > 0 && lnum <= book->mem.lineCount)
         returnVar->string = copyStr(memGetLine(book, lnum, FALSE));
   } else {
      VTermScreen* screen = vterm_obtain_screen(term->vterm);
      VTermRect rect;

      if (row < 0 || (Unt)row >= term->rows)
          return;
      int len = term->cols * MB_MAXBYTES + 1;
      CS p = alloc(len);
      returnVar->string = p;

      rect.start_col = 0;
      rect.end_col = term->cols;
      rect.start_row = row;
      rect.end_row = row + 1;
      p[vterm_screen_get_text(screen, (char *)p, len, rect)] = ZERO;
    }
}

// "term_getscrolled(book)" function
void
f_term_getscrolled(Arr(Var) argvars, Var* returnVar) {
   Book* book = term_get_buf(argvars, S"term_getscrolled()");
   if (!book)
      return;
   returnVar->number = book->term->scrollbackScrolled;
}

// "term_getsize(book)" function
void
f_term_getsize(Arr(Var) argvars, Var* returnVar) {
   Book* book = term_get_buf(argvars, S"term_getsize()");
   if (!book)
      return;

   allocReturnList(returnVar);
   List* l = returnVar->list;
   list_append_number(l, book->term->rows);
   list_append_number(l, book->term->cols);
}

// "term_setsize(book, rows, cols)" function
void
f_term_setsize(Arr(Var) argvars, Var* returnVar UNUSED) {
   Terminal* term;
   Long rows, cols;

   Book* book = term_get_buf(argvars, S"term_setsize()");
   if (!book) {
      emsg(_(e_not_terminal_buffer));
      return;
   }
   if (!book->term->vterm)
      return;
   term = book->term;
   rows = tv_get_number(&argvars[1]);
   rows = rows <= 0 ? term->rows : rows;
   cols = tv_get_number(&argvars[2]);
   cols = cols <= 0 ? term->cols : cols;
   vterm_set_size(term->vterm, rows, cols);
   // handleShellResize() will resize the portals

   // Get and remember the size we ended up with.  Update the pty.
   vterm_get_size(term->vterm, &term->rows, &term->cols);
   term_report_winsize(term, term->rows, term->cols);
}

// "term_getstatus(book)" function
void
f_term_getstatus(Arr(Var) argvars, Var* returnVar) {
   Terminal* term;
   Byte val[100];

   returnVar->tag = VAR_STRING;

   Book* book = term_get_buf(argvars, S"term_getstatus()");
   if (!book)
      return;
   term = book->term;

   if (term_job_running(term))
      STRCPY(val, "running");
   else
      STRCPY(val, "finished");
   if (term->isNormalMode)
      STRCAT(val, ",normal");
   returnVar->string = copyStr(val);
}

// "term_gettitle(book)" function
void
f_term_gettitle(Arr(Var) argvars, Var* returnVar) {

   returnVar->tag = VAR_STRING;

   Book* book = term_get_buf(argvars, S"term_gettitle()");
   if (!book)
      return;

   if (book->term->title)
      returnVar->string = copyStr(book->term->title);
}

// "term_gettty(book)" function
void
f_term_gettty(Arr(Var) argvars, Var* returnVar) {
   Book* book = term_get_buf(argvars, S"term_gettty()");
   if (!book)
      return;
      
   CS p = NULL;
   int      num = 0;
   returnVar->tag = VAR_STRING;
   if (argvars[1].tag != VAR_UNKNOWN)
      num = tv_get_bool(&argvars[1]);

   switch (num) {
   case 0:
      if (book->term->job != NULL)
         p = book->term->job->jv_tty_out;
      break;
   case 1:
      if (book->term->job != NULL)
         p = book->term->job->jv_tty_in;
      break;
   default:
      showErrFmtMsg(_(e_invalid_argument_str), tv_get_string(&argvars[1]));
      return;
   }
   if (p)
      returnVar->string = copyStr(p);
}

// "term_list()" function
void
f_term_list(Arr(Var) argvars UNUSED, Var* returnVar) {
   if (first_term == NULL)
      return;

   allocReturnList(returnVar);
   List* l = returnVar->list;
   Terminal* term;
   FOR_ALL_TERMS(term) {
      if (term->book && list_append_number(l, (Long)term->book->fiNum) == FAIL)
         return;
   } 
}

// "term_scrape(book, row)" function
void
f_term_scrape(Arr(Var) argvars, Var* returnVar) {
   VTermScreen       *screen = NULL;
   VTermPos       pos;
   List       *l;
   Terminal       *term;
   CS p;
   ScrollbackLine       *line;

   allocReturnList(returnVar);

   Book* book = term_get_buf(argvars, S"term_scrape()");
   if (!book)
      return;
      
   term = book->term;

   l = returnVar->list;
   pos.row = get_row_number(&argvars[1], term);

   if (term->vterm != NULL) {
      screen = vterm_obtain_screen(term->vterm);
      if (screen == NULL)  // can't really happen
         return;
      p = NULL;
      line = NULL;
    } else {
      LineNr   lnum = pos.row + term->scrollbackScrolled;

      if (lnum < 0 || lnum >= term->scrollback.len)
          return;
      p = memGetLine(book, lnum + 1, FALSE);
      line = (ScrollbackLine *)term->scrollback.c + lnum;
   }

   for (pos.col = 0; pos.col < term->cols; ) {
      Bag      *dcell;
      int      width;
      VTermScreenCellAttrs attrs;
      VTermColor   fg, bg;
      Byte rgb[8];
      Byte mbs[MB_MAXBYTES * VTERM_MAX_CHARS_PER_CELL + 1];
      int off = 0;
      int i;

      if (!screen) {
         CellDeco* cellattr;
         int len;

         // vterm has finished, get the cell from scrollback
         if (pos.col >= line->cols)
            break;
         cellattr = line->sb_cells + pos.col;
         width = cellattr->width;
         attrs = cellattr->flags;
         fg = cellattr->fg;
         bg = cellattr->bg;
         len = utfCharLen(p);
         mch_memmove(mbs, p, len);
         mbs[len] = ZERO;
         p += len;
      } else {
         VTermScreenCell cell;

         if (vterm_screen_get_cell(screen, pos, &cell) == 0)
            break;
         for (i = 0; i < VTERM_MAX_CHARS_PER_CELL; ++i) {
            if (cell.chars[i] == 0)
                break;
            off += mb_char2bytes((int)cell.chars[i], mbs + off);
         }
         mbs[off] = ZERO;
         width = cell.width;
         attrs = cell.attrs;
         fg = cell.fg;
         bg = cell.bg;
      }
      dcell = allocBag();
      listAppendBag(l, dcell);

      bagAddString(dcell, S"chars", mbs);

      eeSnprintf(rgb, 8, "#%02x%02x%02x", fg.red, fg.green, fg.blue);
      bagAddString(dcell, S"fg", rgb);
      eeSnprintf(rgb, 8, "#%02x%02x%02x", bg.red, bg.green, bg.blue);
      bagAddString(dcell, S"bg", rgb);

      bagAddNumber(dcell, S"deco", cellToDecoration(term, NULL, &attrs, &fg, &bg).flags);
      bagAddNumber(dcell, S"width", width);

      ++pos.col;
      if (width == 2)
          ++pos.col;
   }
}

// "term_sendkeys(book, keys)" function
void
f_term_sendkeys(Arr(Var) argvars, Var* returnVar UNUSED) {
   Book* book = term_get_buf(argvars, S"term_sendkeys()");
   if (!book)
      return;

   CS msg = convertVarToStringSingleUse(&argvars[1]);
   if (msg == NULL)
      return;
   Terminal* term = book->term;
   if (term->vterm == NULL)
      return;

   while (*msg != ZERO) {
      int c;

      if (*msg == K_SPECIAL && msg[1] != ZERO && msg[2] != ZERO) {
         c = TO_SPECIAL(msg[1], msg[2]);
         msg += 3;
      } else {
         c = mb_ptr2char(msg);
         msg += MB_CPTR2LEN(msg);
      }
      send_keys_to_term(term, c, 0, FALSE);
   }
}

// "term_getansicolors(book)" function
void
f_term_getansicolors(Arr(Var) argvars, Var* returnVar) {
   VTermState* state;
   VTermColor color;
   Byte hexbuf[10];
   int index;
   List* list;

   allocReturnList(returnVar);

   Book* book = term_get_buf(argvars, S"term_getansicolors()");
   if (book == NULL)
      return;
   Terminal* term = book->term;
   if (term->vterm == NULL)
      return;

   list = returnVar->list;
   state = vterm_obtain_state(term->vterm);
   for (index = 0; index < 16; index++) {
      vterm_state_get_palette_color(state, index, &color);
      sprintf((char *)hexbuf, "#%02x%02x%02x", color.red, color.green, color.blue);
      if (list_append_string(list, hexbuf, 7) == FAIL)
          return;
   }
}

// "term_setansicolors(book, list)" function
void
f_term_setansicolors(Arr(Var) argvars, Var* returnVar UNUSED) {
   Book   *book;
   Terminal   *term;
   ListItem   *li;
   int      n = 0;

   book = term_get_buf(argvars, S"term_setansicolors()");
   if (book == NULL)
      return;
   term = book->term;
   if (term->vterm == NULL)
      return;

   if (confirmVarIsNonnullList(argvars, 1) == FAIL)
      return;

   if (argvars[1].list->first == &range_list_item || argvars[1].list->len != 16) {
      showErrFmtMsg(_(e_invalid_value_for_argument_str), "\"colors\"");
      return;
   }

   if (term->palette == NULL)
      term->palette = ALLOC_MULT(Ulong, 16);
   if (term->palette == NULL)
      return;

   FOR_ALL_LIST_ITEMS(argvars[1].list, li) {

      CS colorName = convertVarToStringSingleUse(&li->c);
      if (!colorName)
         return;

      UiColor color = hiColorByName(text(colorName));
      if (color == INVALCOLOR) {
         showErrFmtMsg(_(e_cannot_allocate_color_str), colorName);
         return;
      }

      term->palette[n++] = GUI_MCH_GET_RGB(color);
   }

   term_update_palette(term);
}

// "term_setapi(book, api)" function
void
f_term_setapi(Arr(Var) argvars, Var* returnVar UNUSED) {
   Book* book = term_get_buf(argvars, S"term_setapi()");
   if (!book)
      return;
   Terminal* term = book->term;
   eeglFree(term->tl_api);
   CS api = convertVarToStringSingleUse(&argvars[1]);
   term->tl_api = api ? copyStr(api) : null;
}

// "term_setrestore(book, command)" function
void
f_term_setrestore(Arr(Var) argvars UNUSED, Var* returnVar UNUSED) {
   Book* book = term_get_buf(argvars, S"term_setrestore()");
   if (!book)
      return;
   Terminal* term = book->term;
   eeglFree(term->command);
   CS comm = convertVarToStringSingleUse(&argvars[1]);
   term->command = comm ? copyStr(comm) : null;
}

// "term_setkill(book, how)" function
void
f_term_setkill(Arr(Var) argvars UNUSED, Var* returnVar UNUSED) {
   Book* book = term_get_buf(argvars, S"term_setkill()");
   if (!book)
      return;
   Terminal* term = book->term;
   eeglFree(term->tl_kill);
   CS how = convertVarToStringSingleUse(&argvars[1]);
   term->tl_kill = how ? copyStr(how) : null;
}

// "term_start(command, options)" function
void
f_term_start(Arr(Var) argvars, Var* returnVar) {
   JobOptions   opt;

   init_job_options(OUT &opt);
   if (argvars[1].tag != VAR_UNKNOWN
       && get_job_options(&argvars[1], OUT &opt,
            JO_TIMEOUT_ALL + JO_STOPONEXIT
                + JO_CALLBACK + JO_OUT_CALLBACK + JO_ERR_CALLBACK
                + JO_EXIT_CB + JO_CLOSE_CALLBACK + JO_OUT_IO,
            JO2_TERM_NAME + JO2_TERM_FINISH + JO2_HIDDEN + JO2_TERM_OPENCMD
                + JO2_TERM_COLS + JO2_TERM_ROWS + JO2_VERTICAL + JO2_CURPOR
                + JO2_CWD + JO2_ENV + JO2_EOF_CHARS
                + JO2_NORESTORE + JO2_TERM_KILL + JO2_TERM_HIGHLIGHT
                + JO2_ANSI_COLORS + JO2_TTY_TYPE + JO2_TERM_API) == FAIL
   )
      return;

   Book* book = term_start(&argvars[0], NULL, &opt, 0);
   if (book && book->term)
      returnVar->number = book->fiNum;
}

void
f_term_wait(Arr(Var) argvars, Var* returnVar UNUSED) {
   Book* book = term_get_buf(argvars, S"term_wait()");
   if (!book)
      return;
   if (book->term->job == NULL) {
      lo("term_wait(): no job to wait for");
      return;
   }
   if (book->term->job->jv_channel == NULL)
      // channel is closed, nothing to do
      return;

   // Get the job status, this will detect a job that finished.
   if (!book->term->job->jv_channel->ch_keep_open
       && STRCMP(job_status(book->term->job), "dead") == 0
   ){
      // The job is dead, keep reading channel I/O until the channel is
      // closed. book->term may become NULL if the terminal was closed while waiting.
      lo("term_wait(): waiting for channel to close");
      while (book->term != NULL && !book->term->isChannelClosed) {
         term_flush_messages();

         ui_delay(10L, FALSE);
         if (!bookIsValid(book))
            // If the terminal is closed when the channel is closed the buffer disappears.
            break;
         if (book->term == NULL || book->term->isChannelClosing)
            // came here from a close callback, only wait one time
            break;
      }

      term_flush_messages();
   } else {
      long wait = 10L;

      term_flush_messages();

      // Wait for some time for any channel I/O.
      if (argvars[1].tag != VAR_UNKNOWN)
         wait = tv_get_number(&argvars[1]);
      ui_delay(wait, TRUE);

      // Flushing messages on channels is hopefully sufficient. TODO: is there a better way?
      term_flush_messages();
   }
}

// Called when a channel has sent all the lines to a terminal.
// Send a CTRL-D to mark the end of the text.
void
term_send_eof(Channel* ch) {
   Terminal* term;
   FOR_ALL_TERMS(term) {
      if (term->job == ch->job) {
         if (term->tl_eof_chars != NULL) {
            channel_send(ch, PART_IN, term->tl_eof_chars, (int)STRLEN(term->tl_eof_chars), NULL);
            channel_send(ch, PART_IN, (CS)"\r", 1, NULL);
         }
      }
   } 
}

///////////////////////////////////////
// Create a new terminal of "rows" by "cols" cells. Start job for "cmd".
// Store the pointers in "term". When "argv" is not NULL then "argvar" is not used. OK or FAIL.
private int
term_and_job_init(
	Terminal* term,
	Var* argvar,
	Byte** argv,
	JobOptions* opt,
	JobOptions* orig_opt UNUSED
) {
   term->tl_arg0_cmd = NULL;

   if (create_vterm(term, term->rows, term->cols) == FAIL)
      return FAIL;

   if (term->palette != NULL)
      set_vterm_palette(term->vterm, term->palette);
   else
      init_vterm_ansi_colors(term->vterm);

   // This may change a string in "argvar".
   term->job = startJob(argvar, argv, opt, &term->job);
   if (term->job != NULL)
      ++term->job->jv_refcount;

   return term->job
      && term->job->jv_channel != NULL
      && term->job->jv_status != JOB_FAILED ? OK : FAIL;
}

private int
create_pty_only(Terminal* term, JobOptions* opt) {
   if (create_vterm(term, term->rows, term->cols) == FAIL)
      return FAIL;

   term->job = job_alloc();
   if (term->job == NULL)
      return FAIL;
   ++term->job->jv_refcount;

   // behave like the job is already finished
   term->job->jv_status = JOB_FINISHED;

   return mch_create_pty_channel(term->job, opt);
}

// Free the terminal emulator part of "term".
private void
term_free_vterm(Terminal* term) {
   if (term->vterm)
      vterm_free(term->vterm);
   term->vterm = NULL;
}

// Report the size to the terminal.
private void
term_report_winsize(Terminal* term, int rows, int cols) {
   // Use an ioctl() to report the new portal size to the job.
   if (!term->job || term->job->jv_channel == NULL)
      return;

   int fd = -1;
   int part;

   for (part = PART_OUT; part < PART_COUNT; ++part) {
      fd = term->job->jv_channel->fds[part].ch_fd;
      if (mch_isatty(fd))
         break;
   }
   if (part < PART_COUNT && mch_report_winsize(fd, rows, cols) == OK)
      mch_signal_job(term->job, (CS)"winch");
}


#if defined(PROTO)
Job*
term_getjob(Terminal* term) {
   return term ? term->job : NULL;
}
#endif

private void
prepare_to_exit(void) {
   // Ignore SIGHUP, because a dropped connection causes a read error, which
   // makes Eegl exit and then handling SIGHUP causes various reentrance problems.
   mch_signal(SIGHUP, SIG_IGN);

   windgoto((int)visibleRowsG - 1, 0);

   //Switch terminal mode back now, so messages end up on the "normal" screen (if 
   //there are two screens)
   termSetMode(TMODE_COOK);
   termStopTerminfo();
   out_flush();
}

//Preserve files and exit. When called IObuff must contain a message.
//NOTE: This may be called from deathtrap() in a signal handler, so avoid unsafe
//functions, such as allocating memory.
void
preserve_exit(void) {
   prepare_to_exit();

   // Setting this will prevent free() calls.  That avoids calling free()
   // recursively when free() was invoked with a bad pointer.
   really_exiting = TRUE;

   out_str(IObuff);
   screen_start();          // don't know where cursor is now
   out_flush();

   ml_close_notmod();          // close all not-modified buffers

   Book* book;
   FOR_ALL_BOOKS(book) {
      if (book->mem.mfile && book->mem.mfile->fName != NULL) {
         OUT_STR("Eegl: preserving files...\r\n");
         screen_start();       // don't know where cursor is now
         out_flush();
         ml_sync_all(FALSE, FALSE);   // preserve all swap files
         break;
      }
   }

   ml_close_all(false);       // close all memfiles, without deleting

   OUT_STR("Eegl: Finished.\r\n");

   exitEegl(1);
}

//Wait "msec" msec until a character is available from file descriptor "fd".
//"msec" == 0 will check for characters once.
//"msec" == -1 will block until a character is available.
//When a GUI is being used, this will not be used for input -- webb
//Or when a Linux GPM mouse event is waiting.
//Or when a clientserver message is on the queue.
//"interrupted" (if not NULL) is set to TRUE when no character is available
//but something else needs to be done.
int
realWaitForChar(int fd, long msec, int* check_for_gpm UNUSED, int* interrupted) {
   int      ret;
   int      result;
   static int   busy = FALSE;

   // Remember at what time we started, so that we know how much longer we
   // should wait after being interrupted.
   long   start_msec = msec;
   Elapsed   start_tv;

   if (msec > 0)
      ELAPSED_INIT(start_tv);

   // Handle being called recursively.  This may happen for the session
   // manager stuff, it may save the file, which does a breakcheck.
   if (busy)
      return 0;

   for (;;) {
      int finished = TRUE; // default is to 'loop' just once
      TimeVal tv;
      TimeVal* tvp;
      // These are static because they can take 8 Kbyte each and cause the
      // signal stack to run out with -O3.
      static fd_set   rfds, wfds, efds;
      int      maxfd;
      long      towait = msec;


      if (towait >= 0) {
         tv.tv_sec = towait / 1000;
         tv.tv_usec = (towait % 1000) * (1000000/1000);
         tvp = &tv;
      } else
         tvp = NULL;

      //Select on ready for reading and exceptional condition (end of file).
   select_eintr:
      FD_ZERO(&rfds);
      FD_ZERO(&wfds);
      FD_ZERO(&efds);
      FD_SET(fd, &rfds);
      FD_SET(fd, &efds);
      maxfd = fd;

      if (wayland_may_restore_connection()) {
         FD_SET(wayland_display_fd, &rfds);

         if (maxfd < wayland_display_fd)
            maxfd = wayland_display_fd;
      }

      maxfd = channel_select_setup(maxfd, &rfds, &wfds, &tv, &tvp);
      if (interrupted != NULL)
         *interrupted = FALSE;

      ret = select(
         maxfd + 1, SELECT_TYPE_ARG234 &rfds, SELECT_TYPE_ARG234 &wfds, SELECT_TYPE_ARG234 &efds, tvp
      );
      result = ret > 0 && FD_ISSET(fd, &rfds);
      if (result)
         --ret;
      ei (interrupted != NULL && ret > 0)
         *interrupted = TRUE;

# ifdef EINTR
      if (ret == -1 && errno == EINTR) {

         // Check whether window has been resized, EINTR may be caused by SIGWINCH.
         if (doResizeG) {
            lo("calling handleShellResize() in realWaitForChar()");
            handleShellResize();
         }

         // Interrupted by a signal, need to try again. We ignore msec here, because we do want to 
         // check even after a timeout if characters are available.  Needed for reading output of an
         // external command after the process has finished.
         goto select_eintr;
      }
# endif

      //Technically we should first call wl_display_prepare_read() before
      //polling the fd, then read and dispatch after we poll. However that is
      //only needed for multi threaded environments to prevent deadlocks so we are fine.
      if (ret > 0 && FD_ISSET(wayland_display_fd, &rfds))
          wayland_client_update();

      // also call when ret == 0, we may be polling a keep-open channel
      if (ret >= 0)
         (void)channel_select_check(ret, &rfds, &wfds);


      if (finished || msec == 0)
         break;

      // We're going to loop around again, find out for how long
      if (msec > 0) {
         // Compute remaining wait time.
         msec = start_msec - ELAPSED_FUNC(start_tv);
         if (msec <= 0)
            break;   // waited long enough
      }
   }

   return result;
}


//Write s[len] to the screen (stdout).
private void
mch_write(CS s, int len) {
   (void)write(1, (char *)s, len);
   if (p_wd)      // Unix is too fast, slow down a bit more
      realWaitForChar(read_cmd_fd, p_wd, NULL, NULL);
}

//Called when Eegl is going to sleep or execute a shell command. We can't respond to requests for 
//the X or Wayland selections. Lose them, otherwise other applications will hang.
//Wayland users must have a clipboard manager to replicate such behavior.
//private void
//lose_clipboard(void){
//   if (clipboard.owned) {
//      clip_lose_selection(&clipboard);
//   }
//}

//Return TRUE if the input comes from a terminal, FALSE otherwise.
int
mch_input_isatty(void) {
   if (isatty(read_cmd_fd))
      return TRUE;
   return FALSE;
}

//}}}
//{{{ui proper

// ui.c: functions that handle the user interface.
// 1. Keyboard input stuff, and a bit of windowing stuff.  These are called
//    before the machine specific stuff (mch_*) so that we can call the GUI
//    stuff instead if the GUI is running.
// 2. Input buffer stuff.

void
uiInit(void) {
   visibleColsG = 80;
   visibleRowsG = 24;

   out_flush();

   // Check whether we were invoked with SIGTSTP set to be ignored. If it is
   // that indicates the shell (or program) that launched us does not support
   // tty job control and thus we should ignore that signal.
   setIgnoreSigTstp(SIG_IGN == mch_signal(SIGTSTP, SIG_ERR));
   set_signals();
}

void
ui_write(CS s, int len, int console UNUSED) {
   // Don't output anything in silent mode ("ex -s") unless 'verbose' set
   if (!(silentModeG && p_verbose == 0)) {
      mch_write(s, len);
      if (console && s[len - 1] == '\n')
         eeFsync(1);
   }
}

// When executing an external program, there may be some typed characters that
// are not consumed by it. Give them back to ui_inchar() and they are stored here for the next call.
private CS ta_str = NULL;
private int ta_off;   // offset for next char to use when ta_str != NULL
private int ta_len;   // length of ta_str when it's not NULL

void
ui_inBytendo(CS s, int len) {
   int newlen = len;
   if (ta_str)
      newlen += ta_len - ta_off;
   CS new = alloc(newlen);

   if (ta_str) {
      mch_memmove(new, ta_str + ta_off, (Unt)(ta_len - ta_off));
      mch_memmove(new + ta_len - ta_off, s, (Unt)len);
      eeglFree(ta_str);
   } else 
      { mch_memmove(new, s, (Unt)len); }
   ta_str = new;
   ta_len = newlen;
   ta_off = 0;
}

//Function passed to inchar_loop() to handle window resizing.
//If "check_only" is TRUE: Return whether there was a resize.
//If "check_only" is FALSE: Deal with the window resized.
private int
resize_func(int check_only) {
   if (check_only)
      return doResizeG;
   while (doResizeG) {
      lo("calling handleShellResize() in resize_func()");
      handleShellResize();
   }
   return FALSE;
}

//mch_inchar(): low level input function. Get characters from the keyboard.
//Return the number of characters that are available.
//If wtime == 0 do not wait for characters.
//If wtime == n wait a short time for characters.
//If wtime == -1 wait forever for characters.
private int
mch_inchar(
   OUT CS buf,
   int maxlen,
   long wtime,       // don't use "time", MIPS cannot handle it
   int tb_change_cnt
) {
   return inchar_loop(OUT buf, maxlen, wtime, tb_change_cnt, waitForChar, resize_func);
}

// ui_inchar(): low level input function. Get characters from the keyboard.
// Return the number of characters that are available.
// If "wtime" == 0 do not wait for characters.
// If "wtime" == -1 wait forever for characters.
// If "wtime" > 0 wait "wtime" milliseconds for a character.
//
// "tb_change_cnt" is the value of typeBufG.tb_change_cnt iff "buf" points into
// it (null otherwise).  When typeBufG.tb_change_cnt changes (e.g., when a message is received
// from a remote client) "buf" can no longer be used.
int
ui_inchar(
   OUT CS buf,
   int maxlen,
   long wtime,       // don't use "time", MIPS cannot handle it
   int tb_change_cnt
){
   int      retval = 0;

   // If we are going to wait for some time or block...
   if (wtime == -1 || wtime > 100L) {
      // ... allow signals to kill us.
      (void)eeHandleSignal(SIGNAL_UNBLOCK);

      // ... there is no need for CTRL-C to interrupt something, don't let
      // it set gotInterruptG when it was mapped.
      if ((mapped_ctrl_c | curBook->mappedCtrlC) & get_real_state())
          ctrl_c_interrupts = FALSE;
   }

   //Here we call gui_inchar() or mch_inchar(), the GUI or machine-dependent
   //input function.  The functionality they implement is like this:
   //
   //while (not timed out)
   //{
   //   handle-resize;
   //   parse-queued-messages;
   //   if (waited for 'updatetime')
   //      trigger-cursorhold;
   //   ui_wait_for_chars_or_timer()
   //   if (character available)
   //     break;
   //}
   //
   //ui_wait_for_chars_or_timer() does:
   //
   //while (not timed out)
   //{
   //    if (anyTimerTriggered)
   //        invokeTimerCallback;
   //    waitForCharacter();
   //    if (character available)
   //        break;
   //}
   //
   //wait-for-character() does:
   //while (not timed out)
   //{
   //    Wait for event;
   //    if (something on channel)
   //        read/write channel;
   //     ei (resized)
   //        handleResize();
   //     ei (system event)
   //        dealWithSystemEvent;
   //     ei (character available)
   //        break;
   //}

   retval = mch_inchar(OUT buf, maxlen, wtime, tb_change_cnt);

   if (wtime == -1 || wtime > 100L) {
      // block SIGHUP et al.
      (void)eeHandleSignal(SIGNAL_BLOCK);
   }

   ctrl_c_interrupts = TRUE;
   return retval;
}

//Common code for mch_inchar() and gui_inchar(): Wait for a while or indefinitely until 
//characters are available, dealing with timers and messages on channels.
//"buf" may be NULL if the available characters are not to be returned, only check if they are 
//available.
//Return the number of characters that are available.
//If "wtime" == 0 do not wait for characters.
//If "wtime" == n wait a short time for characters.
//If "wtime" == -1 wait forever for characters.
int
inchar_loop(
   OUT CS buf,
   int maxlen,
   long wtime,       // don't use "time", MIPS cannot handle it
   int tb_change_cnt,
   int (*wait_func)(long wtime, int *interrupted, int ignore_input),
   int (*resize_func)(int check_only)
){
   int len;
   int interrupted = FALSE;
   int did_call_wait_func = FALSE;
   int did_start_blocking = FALSE;
   long wait_time;
   long elapsed_time = 0;
   Elapsed start_tv;

   ELAPSED_INIT(start_tv);

   //repeat until we got a character or waited long enough
   for (;;) {
      //Check if portal changed size while we were busy, perhaps the ":set
      //columns=99" command was used.
      if (resize_func)
         resize_func(FALSE);

      //Only process messages when waiting.
      if (wtime != 0) {
         parse_queued_messages();
         //If input was put directly in typeahead buffer bail out here.
         if (typebuf_changed(tb_change_cnt))
            return 0;
      }
      if (wtime < 0 && did_start_blocking)
         // blocking and already waited for p_ut
         wait_time = -1;
      else {
         if (wtime >= 0)
            wait_time = wtime;
         else
            // going to block after p_ut
            wait_time = p_ut;
         elapsed_time = ELAPSED_FUNC(start_tv);
         wait_time -= elapsed_time;

         // If the waiting time is now zero or less, we timed out. However, loop at least once to
         // check for characters and events. Matters when "wtime" is zero.
         if (wait_time <= 0 && did_call_wait_func) {
            if (wtime >= 0)
               // no character available within "wtime"
               return 0;

            // No character available within 'updatetime'.
            did_start_blocking = TRUE;
            if (trigger_cursorhold() && maxlen >= 3 && !typebuf_changed(tb_change_cnt)) {
               // Put K_CURSORHOLD in the input buffer or return it.
               if (!buf) {
                  Byte ibuf[3];
                  ibuf[0] = CSI;
                  ibuf[1] = KS_EXTRA;
                  ibuf[2] = (int)KE_CURSORHOLD;
                  add_to_input_buf(ibuf, 3);
               } else {
                  buf[0] = K_SPECIAL;
                  buf[1] = KS_EXTRA;
                  buf[2] = (int)KE_CURSORHOLD;
               }
               return 3;
            }

            //There is no character available within 'updatetime' seconds: flush all the swap 
            //files to disk. Also done when interrupted by SIGWINCH.
            before_blocking();
            continue;
         }
      }

      if (wait_time < 0 || wait_time > 100L) {
         // Checking if a job ended requires polling. Do this at least every 100 msec.
         if (has_pending_job())
            wait_time = 100L;

         // If there is readahead then parse_queued_messages() timed out and
         // we should call it again soon.
         if (channel_any_readahead())
            wait_time = 10L;
      }

      // Wait for a character to be typed or another event, such as the winch
      // signal or an event on the monitored file descriptors.
      did_call_wait_func = TRUE;
      if (wait_func(wait_time, &interrupted, FALSE)) {
         // If input was put directly in typeahead buffer bail out here.
         if (typebuf_changed(tb_change_cnt))
            return 0;

         // We might have something to return now.
         if (!buf) {
            // "buf" is NULL, we were just waiting, not actually getting input.
            return input_available();
         }

         len = read_from_input_buf(buf, (long)maxlen);
         if (len > 0) { // here we get raw keyboard input
            return len;
         }
         continue;
      }
      // Timed out or interrupted with no character available.

      // estimate the elapsed time
      elapsed_time += wait_time;

      if ((resize_func && resize_func(TRUE))
            || interrupted
            || wait_time > 0
            || (wtime < 0 && !did_start_blocking)
      )
         // no character available, but something to be done, keep going
         continue;

      // no character available or interrupted, return zero
      break;
   }
   return 0;
}

// Wait for a timer to fire or "wait_func" to return non-zero.
// Return OK when something was read. Return FAIL when it timed out or was interrupted.
private int
ui_wait_for_chars_or_timer(
   long wtime,
   int (*wait_func)(long wtime, int *interrupted, int ignore_input),
   int *interrupted,
   int ignore_input
){
   int due_time;
   long remaining = wtime;
   int tb_change_cnt = typeBufG.tb_change_cnt;
   int brief_wait = FALSE;

   // When waiting very briefly don't trigger timers.
   if (wtime >= 0 && wtime < 10L)
      return wait_func(wtime, NULL, ignore_input);

   while (wtime < 0 || remaining > 0) {
      //Trigger timers and then get the time in wtime until the next one is due. Wait up to that 
      //time.
      due_time = check_due_timer();
      if (typeBufG.tb_change_cnt != tb_change_cnt) {
         // timer may have used feedkeys()
         return FAIL;
      }
      if (due_time <= 0 || (wtime > 0 && due_time > remaining))
         due_time = remaining;
      if ((due_time < 0 || due_time > 10L) && (has_pending_job() || channel_any_readahead())) {
         //There is a pending job or channel, should return soon in order
         //to handle them ASAP. Do check for input briefly.
         due_time = 10L;
         brief_wait = TRUE;
      }
      if (wait_func(due_time, interrupted, ignore_input))
         return OK;
      if ((interrupted != NULL && *interrupted) || brief_wait)
         // Nothing available, but need to return so that side effects get
         // handled, such as handling a message on a channel.
         return FAIL;
      if (wtime > 0)
          remaining -= due_time;
   }
   return FAIL;
}
//Wait "msec" msec until a character is available from the mouse or keyboard or from inbuf[].
//"msec" == -1 will block forever.
//for "ignore_input" see WaitForCharOr().
//"interrupted" (if not NULL) is set to TRUE when no character is available
//but something else needs to be done.
private int
waitForCharOrMouse(long msec, int *interrupted, int ignore_input) {
   if (!ignore_input && input_available())       // something in inbuf[]
      return 1;

   int avail = realWaitForChar(read_cmd_fd, msec, NULL, interrupted);
   return avail;
}

//Wait "msec" msec until a character is available from the mouse, keyboard, from inbuf[].
//"msec" == -1 will block forever.
//Invokes timer callbacks when needed.
//When "ignore_input" is TRUE even check for pending input when input is already available.
//"interrupted" (if not NULL) is set to TRUE when no character is available
//but something else needs to be done.
//Return TRUE when a character is available.
//When a GUI is being used, this will never get called -- webb
int
waitForChar(long msec, int *interrupted, int ignore_input) {
   return ui_wait_for_chars_or_timer(msec, waitForCharOrMouse, interrupted, ignore_input) == OK;
}

//Return non-zero if a character is available.
private int
mch_char_avail(void) {
   return waitForChar(0L, NULL, FALSE);
}

// Return non-zero if a character is available.
int
ui_char_avail(void) {
   return mch_char_avail();
}

//Delay for the given number of milliseconds. If ignoreinput is FALSE then we
//cancel the delay if a key is hit.
void
ui_delay(long msec_arg, int ignoreinput) {
   long msec = msec_arg;

   if (ui_delay_for_testing > 0)
      msec = ui_delay_for_testing;
   lo("ui_delay(%ld)", msec);
   mch_delay(msec, ignoreinput ? MCH_DELAY_IGNOREINPUT : 0);
}

//Try to get the current Eegl shell size. Put the result in visibleRowsG and visibleColsG.
//Use the new sizes as defaults for @columns and @lines. Return OK when size could be 
//determined, FAIL otherwise.
int
ui_get_shellsize(void) {
   int retval = mch_get_shellsize();
   check_shellsize();
   return retval;
}

//Set the size of the Eegl shell according to visibleRowsG and visibleColsG, if possible.
//The mch_set_shellsize() function will try to set the new size. If this is not possible, 
//it will adjust visibleRowsG and visibleColsG.
void
ui_set_shellsize(int mustset UNUSED) {  // set by the user
   mch_set_shellsize();
}

// Get the portal position in pixels, if possible. Return FAIL when not possible.
int
uiGetPortPos(int* x, int* y, Long timeout UNUSED) {
   return term_get_winpos(x, y, timeout);
}

void
ui_breakcheck(void) {
   ui_breakcheck_force(false);
}

// When "force" is true also check when the terminal is not in raw mode.
// This is useful to read input on channels.
void
ui_breakcheck_force(Boole force) {
   static int recursive = FALSE;
   int save_updating_screen = updating_screen;

   // We could be called recursively if stderr is redirected, calling
   // fill_input_buf() calls termSetMode() when stdin isn't a tty.  termSetMode()
   // calls vgetorpeek() which calls ui_breakcheck() again.
   if (recursive)
      return;
   recursive = TRUE;

   // We do not want gui_resize_shell() to redraw the screen here.
   ++updating_screen;

   chBreakcheck(force);

   if (save_updating_screen)
      updating_screen = TRUE;
   else
      after_updating_screen(FALSE);

   recursive = FALSE;
}

//////////////////////////////////////////////////////////////////////////////
// Functions that handle the input buffer.
//
// The input characters are buffered to be able to check for a CTRL-C. This should be done with 
// signals, but I don't know how to do that in a portable way for a tty in RAW mode.
//
// For the client-server code in the console the received keys are put in the input buffer.

#if defined(USE_INPUT_BUF) || defined(PROTO)

// Internal typeahead buffer. Includes extra space for long key code descriptions which would 
// otherwise overflow.  The buffer is considered full when only this extra space (or part of it) 
// remains.
# define INBUFLEN 4096

private Byte inbuf[INBUFLEN + MAX_KEY_CODE_LEN];
private int inbufcount = 0;       // number of chars in inbuf[]

// eeIsInputBufFull(), eeIsInputBufEmpty(), add_to_input_buf(), and
// trash_input_buf() are functions for manipulating the input buffer.  These
// are used by the gui_* calls when a GUI is used to handle keyboard input.

int
eeIsInputBufFull(void) {
   return (inbufcount >= INBUFLEN);
}

int
eeIsInputBufEmpty(void) {
   return (inbufcount == 0);
}

#if defined(PROTO)
int
eeglFree_in_input_buf(void) {
   return (INBUFLEN - inbufcount);
}
#endif

// Return the current contents of the input buffer and make it empty.
// The returned pointer must be passed to set_input_buf() later.
CS
get_input_buf(void) {
   // We use an arraylist to store the data pointer and the length.
   ArrayList* gap = ALLOC_ONE(ArrayList);
   if (gap) {
      // Add one to avoid a zero size.
      gap->c = alloc(inbufcount + 1);
      mch_memmove(gap->c, inbuf, (Unt)inbufcount);
      gap->len = inbufcount;
   }
   trash_input_buf();
   return (CS)gap;
}

// Restore the input buffer with a pointer returned from get_input_buf(). The allocated memory is 
// freed, this only works once! When "overwrite" is FALSE input typed later is kept.
void
set_input_buf(CS p, Boole overwrite) {
   ArrayList   *gap = (ArrayList *)p;
   if (!gap)
      return;

   if (gap->c != NULL) {
      if (overwrite || inbufcount + gap->len >= INBUFLEN) {
         mch_memmove(inbuf, gap->c, gap->len);
         inbufcount = gap->len;
      } else {
         mch_memmove(inbuf + gap->len, inbuf, inbufcount);
         mch_memmove(inbuf, gap->c, gap->len);
         inbufcount += gap->len;
      }
      eeglFree(gap->c);
   }
   eeglFree(gap);
}

// Add the given bytes to the input buffer Special keys start with CSI. A real CSI must have 
// been translated to CSI KS_EXTRA KE_CSI.  K_SPECIAL doesn't require translation.
void
add_to_input_buf(CS s, int len) {
   if (inbufcount + len > INBUFLEN + MAX_KEY_CODE_LEN)
       return;       // Shouldn't ever happen!

   while (len--)
      inbuf[inbufcount++] = *s++;
}

// Add "str[len]" to the input buffer while escaping CSI bytes.
void
add_to_input_buf_csi(CS str, int len) {
   Byte buf[2];

   for (int i = 0; i < len; ++i) {
      add_to_input_buf(str + i, 1);
      if (str[i] == CSI) {
         // Turn CSI into K_CSI.
         buf[0] = KS_EXTRA;
         buf[1] = (int)KE_CSI;
         add_to_input_buf(buf, 2);
      }
   }
}

// Remove everything from the input buffer.  Called when ^C is found.
void
trash_input_buf(void) {
   inbufcount = 0;
}

// Read as much data from the input buffer as possible up to maxlen, and store it in buf.
int
read_from_input_buf(CS buf, long maxlen) {
   if (inbufcount == 0)   // if the buffer is empty, fill it
      fill_input_buf(true);
   if (maxlen > inbufcount)
      maxlen = inbufcount;
   mch_memmove(buf, inbuf, (Unt)maxlen);
   inbufcount -= maxlen;
   // check "maxlen" to avoid clang warning
   if (inbufcount > 0 && maxlen > 0)
      mch_memmove(inbuf, inbuf + maxlen, (Unt)inbufcount);
   return (int)maxlen;
}

void
fill_input_buf(Boole exit_on_error) {
   int try;
   static int   did_read_something = FALSE;
   static CS rest = NULL;       // unconverted rest of previous read
   static int   restlen = 0;
   int      unconverted;

   if (eeIsInputBufFull())
      return;
   //Fill_input_buf() is only called when we really need a character.
   //If we can't get any, but there is some in the buffer, just return.
   //If we can't get any, and there isn't any in the buffer, we give up and exit Eegl.
   if (rest) {
      // Use remainder of previous call, starts with an invalid character
      // that may become valid when reading more.
      if (restlen > INBUFLEN - inbufcount)
         unconverted = INBUFLEN - inbufcount;
      else
         unconverted = restlen;
      mch_memmove(inbuf + inbufcount, rest, unconverted);
      
      if (unconverted == restlen)
         EE_CLEAR(rest);
      else {
         restlen -= unconverted;
         mch_memmove(rest, rest + unconverted, restlen);
      }
      inbufcount += unconverted;
   } else
      unconverted = 0;

   int len = 0;  
   for (try = 0; try < 100; ++try)  {
      Unt readlen = (Unt)(INBUFLEN - inbufcount);
      len = read(read_cmd_fd, (char *)inbuf + inbufcount, readlen);
      if (len > 0) {
         inbuf[inbufcount + len] = ZERO;
         lo("raw key input: \"%s\" len %d", inbuf, len);
      } 

      if (len > 0 || gotInterruptG)
         break;
      // If reading stdin results in an error, continue reading stderr.
      // This helps when using "foo | xargs eegl".
      if (!did_read_something && !isatty(read_cmd_fd) && read_cmd_fd == 0) {
          int m = cur_tmode;

          // We probably set the wrong file descriptor to raw mode.  Switch
          // back to cooked mode, use another descriptor and set the mode to what it was.
          termSetMode(TMODE_COOK);
          // Use stderr for stdin, also works for shell commands.
          close(0);
          (void)dup(2);
          termSetMode(m);
      }
      if (!exit_on_error)
         return;
   }
   if (len <= 0 && !gotInterruptG)
      read_error_exit();
   if (len > 0)
      did_read_something = TRUE;
   if (gotInterruptG) {
      // Interrupted, pretend a CTRL-C was typed.
      inbuf[0] = 3;
      inbufcount = 1;
   } else {
      while (len > 0) {
         // If a CTRL-C was typed, remove it from the buffer and set gotInterruptG. Also recognize 
         // CTRL-C with modifyOtherKeys set, lower and upper case, in two forms.
         // If terminal key protocols are in use, we expect to receive
         // Ctrl_C as an escape sequence, ignore a raw Ctrl_C as this could be paste data.
         if (ctrl_c_interrupts
           && ((len >= 10 && STRNCMP(inbuf + inbufcount, "\033[27;5;99~", 10) == 0)
              || (len >= 10 && STRNCMP(inbuf + inbufcount, "\033[27;5;67~", 10) == 0)
              || (len >= 7 && STRNCMP(inbuf + inbufcount, "\033[99;5u", 7) == 0)
              || (len >= 7 && STRNCMP(inbuf + inbufcount, "\033[67;5u", 7) == 0))
         ) {
            // remove everything typed before the CTRL-C
            mch_memmove(inbuf, inbuf + inbufcount, (Unt)(len));
            inbufcount = 0;
            gotInterruptG = TRUE;
         }
         --len;
         ++inbufcount;
     }
   }
}
#endif // USE_INPUT_BUF

// Exit because of an input read error.
void
read_error_exit(void) {
   if (silentModeG)   // Normal way to exit for "ex -s"
   exitEegl(0);
    STRCPY(IObuff, _("Eegl: Error reading input, exiting...\n"));
    preserve_exit();
}

// May update the shape of the cursor.
void
ui_cursor_shape_forced(int forced) {
   term_cursor_mode(forced);
}

void
ui_cursor_shape(void) {
   ui_cursor_shape_forced(FALSE);
}

// Check bounds for column number
Unt
check_col(Unt col) {
   if ((int)col >= screenLinesColsG)
      return screenLinesColsG - 1;
   return col;
}

// Check bounds for row number
Unt
check_row(Unt row) {
   if ((int)row >= screenLinesRowsG)
      return screenLinesRowsG - 1;
   return row;
}

// Return length of line "lnum" in screen cells for horizontal scrolling.
long
scroll_line_len(LineNr lnum) {
   CS p = ml_get(lnum);
   ColNr   col = 0;

   if (*p != ZERO) {
      for (;;) {
         int       w = chartabsize(p, col);
         MB_PTR_ADV(p);
         if (*p == ZERO)      // don't count the last character
            break;
         col += w;
      }
   } 
   return col;
}

// Find the longest visible line number.  This is used for horizontal
// scrolling.  If this is not possible (or not desired, by setting 'h' in
// "guioptions") then the current line number is returned.
LineNr
ui_find_longest_lnum(void) {
   LineNr ret = 0;

   // Calculate maximum for horizontal scrollbar.  Check for reasonable
   // line numbers, topline and botline can be invalid when displaying is postponed.
   if (
       curPor->topLine <= curPor->cursor.lnum
       && curPor->bottomLine > curPor->cursor.lnum
       && curPor->bottomLine <= curBook->mem.lineCount + 1
   ) {
      long n;
      long max = 0;

      // Use maximum of all visible lines.  Remember the lnum of the
      // longest line, closest to the cursor line.  Used when scrolling below.
      for (LineNr lnum = curPor->topLine; lnum < curPor->bottomLine; ++lnum) {
         n = scroll_line_len(lnum);
         if (n > max) {
            max = n;
            ret = lnum;
         } ei (n == max && abs((int)(lnum - curPor->cursor.lnum))
                    < abs((int)(ret - curPor->cursor.lnum)))
            ret = lnum;
      }
   } else
      // Use cursor line only.
      ret = curPor->cursor.lnum;

   return ret;
}

// Called when focus changed. 
void
ui_focus_change(int in_focus) {  // TRUE if focus gained.
   static time_t   last_time = (time_t)0;
   int need_redraw = FALSE;

   //When activated: Check if any file was modified outside of Eegl. Only do this when not done 
   //within the last two seconds (could get several events in a row).
   if (in_focus && last_time + 2 < time(NULL)) {
      need_redraw = check_timestamps( FALSE);
      last_time = time(NULL);
   }

   term_focus_change(in_focus);

   // Fire the focus gained/lost autocommand.
   need_redraw |= applyAutocomms(in_focus ? EVENT_FOCUSGAINED
            : EVENT_FOCUSLOST, NULL, NULL, false, curBook);

   if (need_redraw)
      redraw_after_callback(TRUE, TRUE);
}

// Report the windows size "rows" and "cols" to tty "fd".
int
mch_report_winsize(int fd, int rows, int cols) {
   int retval = -1;

   if (fd < 0)
      return FAIL;

# if defined(TIOCSWINSZ)
   struct winsize ws;

   ws.ws_col = cols;
   ws.ws_row = rows;

   // calculate and set tty pixel size
   CellSize cs;
   mch_calc_cell_size(&cs);

   if (cs.cs_xpixel == -1) {
      // failed get pixel size.
      ws.ws_xpixel = 0;
      ws.ws_ypixel = 0;
   } else {
      ws.ws_xpixel = cols * cs.cs_xpixel;
      ws.ws_ypixel = rows * cs.cs_ypixel;
   }

   retval = ioctl(fd, TIOCSWINSZ, &ws);
   lo("ioctl(TIOCSWINSZ) %s", retval == 0 ? "success" : "failed");
# elif defined(TIOCSSIZE)
   struct ttysize ts;

   ts.ts_cols = cols;
   ts.ts_lines = rows;
   retval = ioctl(fd, TIOCSSIZE, &ts);
   lo("ioctl(TIOCSSIZE) %s", retval == 0 ? "success" : "failed");
# endif
   return retval == 0 ? OK : FAIL;
}

// Try to set the window size to visibleRowsG and visibleColsG.
void
mch_set_shellsize(void) {
   if (*termCodeS[KS_CWS] != ZERO) {
      // NOTE: if you get an error here that term_set_winsize() is undefined, check the output of 
      // configure.  It could probably not find a ncurses, termcap or termlib library.
      term_set_winsize((int)visibleRowsG, (int)visibleColsG);
      out_flush();
      screen_start();         // don't know where cursor is now
   }
}

// Try to get the current terminal cell size. On failure, returns -1x-1
void
mch_calc_cell_size(CellSize* cs_out) {
   // get current tty size.
   struct winsize ws;
   int fd = 1;
   int retval = -1;
   retval = ioctl(fd, TIOCGWINSZ, &ws);

   lo("ioctl(TIOCGWINSZ) %s", retval == 0 ? "success" : "failed");

   if (retval == -1 || ws.ws_col == 0 || ws.ws_row == 0) {
      cs_out->cs_xpixel = -1;
      cs_out->cs_ypixel = -1;
      return;
   }

   // calculate parent tty's pixel per cell.
   int x_cell_size = ws.ws_xpixel / ws.ws_col;
   int y_cell_size = ws.ws_ypixel / ws.ws_row;

   // calculate current tty's pixel
   cs_out->cs_xpixel = x_cell_size;
   cs_out->cs_ypixel = y_cell_size;

   lo("Got cell pixel size with TIOCGWINSZ: %d x %d", x_cell_size, y_cell_size);
}

//Try to get the current window size:
//1. with an ioctl(), most accurate method
//2. from the environment variables LINES and COLUMNS
//3. from the termcap
//4. keep using the old values
//Return OK when size could be determined, FAIL otherwise.
int
mch_get_shellsize(void) {
   long   rows = 0;
   long   columns = 0;
   CS p;

   // 1. try using an ioctl. It is the most accurate method.
   // Try using TIOCGWINSZ first, some systems that have it also define
   // TIOCGSIZE but don't have a struct ttysize.
# ifdef TIOCGWINSZ
   {
   struct winsize ws;
   int fd = 1;

   // When stdout is not a tty, use stdin for the ioctl().
   if (!isatty(fd) && isatty(read_cmd_fd))
       fd = read_cmd_fd;
   if (ioctl(fd, TIOCGWINSZ, &ws) == 0) {
       columns = ws.ws_col;
       rows = ws.ws_row;
       lo("Got size with TIOCGWINSZ: %ld x %ld", columns, rows);
   }
    }
# else // TIOCGWINSZ
#  ifdef TIOCGSIZE
    {
   struct ttysize   ts;
   int fd = 1;

   // When stdout is not a tty, use stdin for the ioctl().
   if (!isatty(fd) && isatty(read_cmd_fd))
      fd = read_cmd_fd;
   if (ioctl(fd, TIOCGSIZE, &ts) == 0) {
      columns = ts.ts_cols;
      rows = ts.ts_lines;
      lo("Got size with TIOCGSIZE: %ld x %ld", columns, rows);
   }
   }
#  endif // TIOCGSIZE
# endif // TIOCGWINSZ

   // 2. get size from environment
   if (columns == 0 || rows == 0) {
      if ((p = (CS)getenv("LINES"))) {
         rows = atoi((char *)p);
         lo("Got 'lines' from $LINES: %ld", rows);
      }
      if ((p = (CS)getenv("COLUMNS"))) {
         columns = atoi((char *)p);
         lo("Got 'columns' from $COLUMNS: %ld", columns);
      }
   }

   // 3. try reading "co" and "li" entries from termcap
   if (columns == 0 || rows == 0) {
      getlinecol(&columns, &rows);
      lo("Got size from termcap: %ld x %ld", columns, rows);
   }

   //4. If everything fails, use the old values
   if (columns <= 0 || rows <= 0)
      return FAIL;

   visibleRowsG = rows;
   visibleColsG = columns;
   clampScreenSize();
   return OK;
}

//}}}
//{{{tabpanel

// The panel with the list of tabs on the left

private void do_by_tplmode(Unt tplmode, Unt col_start, Unt col_end,
   OUT Unt* pcurtab_row, OUT Unt* tabNr);

// set pcurtab_row. don't redraw tabpanel.
#define TPLMODE_GET_CURTAB_ROW 0
// set ptabNr. don't redraw tabpanel.
#define TPLMODE_GET_TAB_NR     1
// redraw tabpanel.
#define TPLMODE_REDRAW         2

#define TPL_FILLCHAR      ' '

#define VERT_LEN    1

// tabPanelAlignS's values
#define ALIGN_LEFT  0
#define ALIGN_RIGHT 1

private int opt_scope = OPT_LOCAL;
private int tabPanelAlignS = ALIGN_LEFT;
private int tpl_columns = 20;
private int tpl_is_vert = FALSE;

typedef struct {
   Portal*po;
   Portal* currPort;
   CS user_defined;
   Unt   maxrow;
   Unt   offsetrow;
   Unt* prow;
   Unt* pcol;
   Unt col_start;
   Unt col_end;
} Tabpanel;

int
uiValidateTabpanelopt(CS new) {
   if (!new) {
      return OK;
   } 
   
   int      new_align = ALIGN_LEFT;
   int      new_columns = 20;
   int      new_is_vert = FALSE;

   CS p = new;
   while (*p != ZERO) {
      if (STRNCMP(p, "align:", 6) == 0) {
         p += 6;
         if (STRNCMP(p, "left", 4) == 0) {
            p += 4;
            new_align = ALIGN_LEFT;
         } ei (STRNCMP(p, "right", 5) == 0) {
            p += 5;
            new_align = ALIGN_RIGHT;
         } else
            return FAIL;
      } ei (STRNCMP(p, "columns:", 8) == 0 && EE_ISDIGIT(p[8])) {
         p += 8;
         new_columns = parseLong(&p);
      } ei (STRNCMP(p, "vert", 4) == 0) {
         p += 4;
         new_is_vert = TRUE;
      }

      if (*p != ',' && *p != ZERO)
         return FAIL;
      if (*p == ',')
         ++p;
   }
   tabPanelAlignS = new_align;
   tpl_columns = new_columns;
   tpl_is_vert = new_is_vert;

   shell_new_columns();
   return OK;
}

// Return the width of tabpanel.
int
tabpanel_width(void) {
   if (p_stpl) {
      if (firstTabG->next == NULL)
         return 0;
   } else {
      return 0;
   }
   if (visibleColsG < tpl_columns)
      return 0;
   else
      return tpl_columns;
}

// Return the offset of a portal considering the width of tabpanel.
int
tabpanel_leftcol(void) {
   return tabPanelAlignS == ALIGN_RIGHT ? 0 : tabpanel_width();
}

// draw the tabpanel.
void
draw_tabpanel(void) {
   int saved_KeyTyped = KeyTyped;
   int saved_gotInterruptG = gotInterruptG;
   Unt maxwidth = tabpanel_width();
   char vsDecoFlags = getDecoFlags(HLF_C);
   Unt curtab_row = 0;
   Boole is_right = tabPanelAlignS == ALIGN_RIGHT;

   if (maxwidth == 0)
      return;

   // Reset gotInterruptG to avoid bookRenderStatusLine() isn't evaluted.
   gotInterruptG = FALSE;

   if (tpl_is_vert) {
      if (is_right) {
         // draw main contents in tabpanel
         do_by_tplmode(
            TPLMODE_GET_CURTAB_ROW, VERT_LEN, maxwidth - VERT_LEN, OUT &curtab_row, NULL
         );
         do_by_tplmode(TPLMODE_REDRAW, VERT_LEN, maxwidth, OUT &curtab_row, NULL);
         // draw vert separator in tabpanel
         for (Unt vsrow = 0; vsrow < commlineRowG; vsrow++)
            screen_putchar(fillCharsG.tpl_vert, vsrow, topframeG->width, vsDecoFlags);
      } else {
         // draw main contents in tabpanel
         do_by_tplmode(TPLMODE_GET_CURTAB_ROW, 0, maxwidth - VERT_LEN, OUT &curtab_row, NULL);
         do_by_tplmode(TPLMODE_REDRAW, 0, maxwidth - VERT_LEN, OUT &curtab_row, NULL);
         // draw vert separator in tabpanel
         for (Unt vsrow = 0; vsrow < commlineRowG; vsrow++)
            screen_putchar(fillCharsG.tpl_vert, vsrow, maxwidth - VERT_LEN, vsDecoFlags);
      }
   } else {
      do_by_tplmode(TPLMODE_GET_CURTAB_ROW, 0, maxwidth, OUT &curtab_row, NULL);
      do_by_tplmode(TPLMODE_REDRAW, 0, maxwidth, OUT &curtab_row, NULL);
   }

   gotInterruptG |= saved_gotInterruptG;

   // A user function may reset KeyTyped, restore it.
   KeyTyped = saved_KeyTyped;

   needRedrawTabpanelG = FALSE;
}

// Return tabNr when clicking and dragging in tabpanel. UNT if not found.
Unt
get_tabNr_on_tabpanel(void) {
   Unt maxwidth = tabpanel_width();

   if (maxwidth == 0)
      return UNT;

   Unt curtab_row = 0;
   Unt tabNr = 0;
   do_by_tplmode(TPLMODE_GET_CURTAB_ROW, 0, maxwidth, OUT &curtab_row, NULL);
   do_by_tplmode(TPLMODE_GET_TAB_NR, 0, maxwidth, OUT &curtab_row, OUT &tabNr);

   return tabNr;
}

// Fill tailing area between {start_row} and {end_row - 1}.
private void
fillRowsWithTwoCharsWithTailingArea(
   int   tplmode,
   int   row_start,
   int   row_end,
   int   col_start,
   int   col_end,
   char  decoFlags
) {
   int is_right = tabPanelAlignS == ALIGN_RIGHT;
   if (tplmode == TPLMODE_REDRAW)
      fillRowsWithTwoChars(
         row_start, row_end,
         (is_right ? topframeG->width : 0) + col_start,
         (is_right ? topframeG->width : 0) + col_end,
         TPL_FILLCHAR, TPL_FILLCHAR, decoFlags
      );
}

private void
drawTextLen_for_tabpanel(
   Unt       tplmode,
   CS p,
   Unt       len,
   char decoFlags,
   Tabpanel* tapa
){
   Unt      chcells;
   Byte buf[IOSIZE];
   CS temp;

   for (Unt j = 0; j < len;) {
      if (tplmode != TPLMODE_GET_CURTAB_ROW && tapa->maxrow + tapa->offsetrow <= *tapa->prow)
         break;

      if (p[j] == '\n' || p[j] == '\r') {
         // fill the tailing area of current row.
         if (*tapa->prow >= tapa->offsetrow && *tapa->prow < tapa->offsetrow + tapa->maxrow) {
            fillRowsWithTwoCharsWithTailingArea(tplmode,
               *tapa->prow - tapa->offsetrow,
               *tapa->prow - tapa->offsetrow + 1,
               *tapa->pcol, tapa->col_end, decoFlags
            );
         } 
         (*tapa->prow)++;
         *tapa->pcol = tapa->col_start;
         j++;
      } else {
         Unt charLen = utfCharLen(p + j);

         for (Unt k = 0; k < charLen; k++)
            buf[k] = p[j + k];
         buf[charLen] = ZERO;
         j += charLen;

         // Make all characters printable.
         temp = sanitizeStr(buf);
         if (temp != NULL) {
            copySubstrToAllocation(buf, (Text){temp, sizeof(buf) - 1});
            eeglFree(temp);
         }

         chcells = mb_ptr2cells(buf);

         if (tapa->col_end < (*tapa->pcol) + chcells) {
            // fill the tailing area of current row.
            if (*tapa->prow >= tapa->offsetrow && *tapa->prow - tapa->offsetrow < tapa->maxrow)
               fillRowsWithTwoCharsWithTailingArea(
                  tplmode,
                  *tapa->prow - tapa->offsetrow,
                  *tapa->prow - tapa->offsetrow + 1,
                  *tapa->pcol, tapa->col_end, decoFlags
               );
            *tapa->pcol = tapa->col_end;

            if (tapa->col_end < chcells)
               break;
         }

         if (*tapa->pcol + chcells <= tapa->col_end) {
            int off = (tabPanelAlignS == ALIGN_RIGHT) ? topframeG->width : 0;
            if (TPLMODE_REDRAW == tplmode
                  && (*tapa->prow >= tapa->offsetrow
                     && *tapa->prow < tapa->offsetrow + tapa->maxrow)
            )
               drawText(buf, *tapa->prow - tapa->offsetrow, *tapa->pcol + off, decoFlags);
            *tapa->pcol += chcells;
         }
      }
   }
}

// default tabpanel drawing behavior if 'tabpanel' option is empty.
private void
draw_tabpanel_default(int tplmode, Tabpanel* tapa) {
   int modified;
   Unt countPortals;
   Unt len = 0;
   Byte buf[2] = { ZERO, ZERO };

   modified = FALSE;
   for (countPortals = 0; tapa->po; tapa->po = tapa->po->next, ++countPortals) {
      if (doWasBookChanged(tapa->po->book))
         modified = TRUE;
   } 

   if (modified || countPortals > 1) {
      if (countPortals > 1) {
         eeSnprintf(nameBuffG, MAXPATHL, "%d", countPortals);
         len = (Unt)STRLEN(nameBuffG);
         drawTextLen_for_tabpanel(tplmode, nameBuffG, len, getDecoFlags(HLF_T), tapa);
      }
      if (modified) {
         buf[0] = '+';
         drawTextLen_for_tabpanel(tplmode, buf, 1, 0, tapa);
      }

      buf[0] = TPL_FILLCHAR;
      drawTextLen_for_tabpanel(tplmode, buf, 1, 0, tapa);
   }

   drawGetTranslatedBookName(tapa->currPort->book);
   shorten_dir(nameBuffG);
   len = (int)STRLEN(nameBuffG);
   drawTextLen_for_tabpanel(tplmode, nameBuffG, len, 0, tapa);

   // fill the tailing area of current row.
   if (*tapa->prow >= tapa->offsetrow && *tapa->prow < tapa->offsetrow + tapa->maxrow) {
      fillRowsWithTwoCharsWithTailingArea(
         tplmode, *tapa->prow - tapa->offsetrow, *tapa->prow - tapa->offsetrow + 1,
         *tapa->pcol, tapa->col_end, 0
      );
   } 
   *tapa->pcol = tapa->col_end;
}

// default tabpanel drawing behavior if 'tabpanel' option is NOT empty.
private void
drawTabpanelUserdefined(int tplmode, Tabpanel* tapa) {
   int      p_crb_save;
   Byte buf[IOSIZE];
   StatusLineHilite* hilites;
   StatusLineHilite* labels;
   char currDecoFlags;
   int n;

   //Temporarily reset 'cursorbind', we don't want a side effect from moving the cursor away & back
   p_crb_save = tapa->currPort->o.cursorBind;
   tapa->currPort->o.cursorBind = FALSE;

   // Make a copy, because the statusline may include a function call that
   // might change the option value and free the memory.
   CS p = copyStr(tapa->user_defined);

   bookRenderStatusLine(tapa->currPort, buf, sizeof(buf),
      p, STATLINE_TABPANEL, opt_scope,
      TPL_FILLCHAR, tapa->col_end - tapa->col_start, OUT &hilites, OUT &labels
   );

   eeglFree(p);
   tapa->currPort->o.cursorBind = p_crb_save;

   currDecoFlags = 0;
   p = buf;
   for (n = 0; hilites[n].start; n++) {
      drawTextLen_for_tabpanel(tplmode, p, (int)(hilites[n].start - p), currDecoFlags, tapa);
      p = hilites[n].start;
      if (hilites[n].hiId == SHORT)
         currDecoFlags = 0;
      else
         currDecoFlags = decorationsG[hilites[n].hiId].flags;
   }
   drawTextLen_for_tabpanel(tplmode, p, (int)STRLEN(p), currDecoFlags, tapa);

   // fill the tailing area of current row.
   if (*tapa->prow >= tapa->offsetrow && *tapa->prow < tapa->offsetrow + tapa->maxrow) {
      fillRowsWithTwoCharsWithTailingArea(
         tplmode, *tapa->prow - tapa->offsetrow, *tapa->prow + 1 - tapa->offsetrow, 
         *tapa->pcol, tapa->col_end, currDecoFlags
      );
   } 
   *tapa->pcol = tapa->col_end;
}

private CS
startsWithPercentAndBang(Tabpanel* tapa) {
   if (!p_tpl)
      return NULL;
      
   CS usefmt = p_tpl;
   int anyEmsgG_before = anyEmsgG;

   // When the format starts with "%!" then evaluate it as an expression and
   // use the result as the actual format string.
   if (usefmt[0] == '%' && usefmt[1] == '!') {
      Var tv = {};
      tv.tag = VAR_NUMBER;
      tv.number = tapa->currPort->id;
      set_var(tConst("g:tabpanel_winid"), &tv, FALSE);

      CS p = eval_to_string_safe(usefmt + 2, FALSE);
      if (p)
         usefmt = p;

      unletImpl(S"g:tabpanel_winid", true);

      if (anyEmsgG > anyEmsgG_before) {
         usefmt = NULL;
         optChangeStringOptionDirect(S"tabpanel", Em, opt_scope, SID_ERROR);
      }
   }

   return usefmt;
}

// do something by tplmode for drawing tabpanel.
private void
do_by_tplmode(
   Unt tplmode,
   Unt col_start,
   Unt col_end,
   OUT Unt* pcurtab_row,
   OUT Unt* tabNr
){
   char fillerFlags = getDecoFlags(HLF_TPLF);
   Unt      col = col_start;
   Unt row = 0;
   Tab* tp = NULL;
   Var   v;
   Tabpanel tapa;
   tapa.maxrow = commlineRowG;
   tapa.offsetrow = 0;
   tapa.col_start = col_start;
   tapa.col_end = col_end;

   if (tplmode != TPLMODE_GET_CURTAB_ROW && tapa.maxrow > 0) {
      while (tapa.offsetrow + tapa.maxrow <= *pcurtab_row)
         tapa.offsetrow += tapa.maxrow;
   } 

   tp = firstTabG;

   for (row = 0; tp; row++) {
      if (tplmode != TPLMODE_GET_CURTAB_ROW && tapa.maxrow <= row - tapa.offsetrow)
         break;

      col = col_start;

      v.tag = VAR_NUMBER;
      v.number = indexOfTab(tp);
      set_var(tConst("g:actual_curtabpage"), &v, TRUE);

      if (tp->topframe == topframeG) {
         if (tplmode == TPLMODE_GET_CURTAB_ROW) {
            *pcurtab_row = row;
            break;
         }
      }

      if (tp == curtab) {
         tapa.currPort = curPor;
         tapa.po = firstPor;
      } else {
         tapa.currPort = tp->curPor;
         tapa.po = tp->firstPor;
      }

      CS usefmt = startsWithPercentAndBang(&tapa);
      if (usefmt) {
         Byte buf[IOSIZE];
         CS p = usefmt;
         Unt i = 0;

         while (p[i] != ZERO) {
            while (p[i] == '\n' || p[i] == '\r') {
               // fill the tailing area of current row.
               if (row >= tapa.offsetrow && row < tapa.offsetrow + tapa.maxrow) {
                  fillRowsWithTwoCharsWithTailingArea(
                     tplmode,
                     row - tapa.offsetrow,
                     row - tapa.offsetrow + 1,
                     col, tapa.col_end, 0
                  );
               } 

               row++;
               col = col_start;
               p++;
            }

            while (p[i] != '\n' && p[i] != '\r' && p[i] != ZERO) {
               if (i + 1 >= sizeof(buf))
                  break;
               buf[i] = p[i];
               i++;
            }
            buf[i] = ZERO;

            tapa.user_defined = buf;
            tapa.prow = &row;
            tapa.pcol = &col;
            drawTabpanelUserdefined(tplmode, &tapa);
            // p_tpl could have been freed in bookRenderStatusLine()
            if (!p_tpl) {
               usefmt = NULL;
               break;
            }

            p += i;
            i = 0;
         }
         if (usefmt != p_tpl)
            EE_CLEAR(usefmt);
      } else {
         tapa.user_defined = NULL;
         tapa.prow = &row;
         tapa.pcol = &col;
         draw_tabpanel_default(tplmode, &tapa);
      }

      unletImpl(S"g:actual_curtabpage", true);

      tp = tp->next;

      if ((tplmode == TPLMODE_GET_TAB_NR) 
            && row >= tapa.offsetrow && (mouseRowG <= ((int)row - (int)tapa.offsetrow))
      ) {
         *tabNr = v.number;
         break;
      }
   }

   // fill the area of TabPanelFill.
   fillRowsWithTwoCharsWithTailingArea(
      tplmode, row - tapa.offsetrow, tapa.maxrow, tapa.col_start, tapa.col_end, fillerFlags
   );
}

//}}}
