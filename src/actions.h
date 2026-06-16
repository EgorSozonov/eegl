//EEGL - the Extensible development Environment for GNU/Linux
//Licensed under GPLv3, see the LICENSE file (c) Egor Sozonov

//This file defines the Normal and Visual mode actions. Stuff that is mapped to keys like "o"
//When adding an Action:
// 1. Add an entry in the table `actions[]` below.
// 2. Run "make indices" to re-generate indices/actions.h.
// 3. Add an entry in the index for Normal/Visual commands at
//    ":help normal-index" and ":help visual-index" .
// 4. Add documentation in ../doc/xxx.txt.  Add a tag for both the short and
//    long name of the command.

#ifdef DO_DECLARE_ACTIONS

// Used when building Eegl.
# define ACTION(a, b, c, d) \
	{a, b, c, d}

#define NV_VER_SCROLLBAR nvError
#define NV_HOR_SCROLLBAR nvError

#define NV_TABLINE	nvError
#define NV_TABMENU	nvError

#define NV_NBCMD	nvError

#define NV_DROP		nv_drop

// Function to be called for a Normal or Visual mode Action. The argument is an ActionArg.
typedef void (*nv_func_T)(ActionArg *cap);

// Values for cmd_flags.
#define NV_NCH	    0x01	  // may need to get a second char
#define NV_NCH_NOP  (0x02|NV_NCH) // get second char when no operator pending
#define NV_NCH_ALW  (0x04|NV_NCH) // always get a second char
#define NV_LANG	    0x08	// second char needs language adjustment

#define NV_SS	    0x10	// may start selection
#define NV_SSS	    0x20	// may start selection with shift modifier
#define NV_STS	    0x40	// may stop selection without shift modif.
#define NV_RL	    0x80	// 'rightleft' modifies command
#define NV_KEEPREG  0x100	// don't clear regname
#define NV_NCW	    0x200	// not allowed in command-line window

//Generally speaking, every Normal mode command should either clear any pending operator (with 
//*clearop*()), or set the motion type variable oap->motion_type.
//
//When a cursor motion command is made, it is marked as being a character or line oriented motion.
//Then, if an operator is in effect, the operation becomes character or line oriented accordingly.

//This table contains one entry for every Normal or Visual mode command. The order doesn't matter,
//this will be sorted by the create_nvcmdidx.vim script to generate the nv_cmd_idx[] lookup table.
//It is faster when all keys from zero to '~' are present.
static const struct Action {
   int		actionChar;	// (first) command character
   nv_func_T   fn;	// function for this command
   Short	cmd_flags;	// NV_ flags
   short	cmd_arg;	// value for ca.arg
} actions[] =

#else  // DO_DECLARE_ACTIONS

// Used to build indices/actions.h.

# define ACTION(a, b, c, d)  a
private const int actions[] =

#endif // DO_DECLARE_ACTION
{
   ACTION(ZERO,		nvError,	0,			0),
   ACTION(Ctrl_A,	nvAddSub,	0,			0),
   ACTION(Ctrl_B,	nvPage,	NV_STS,			BACKWARD),
   ACTION(Ctrl_C,	nv_esc,		0,			TRUE),
   ACTION(Ctrl_D,	nv_halfpage,	0,			0),
   ACTION(Ctrl_E,	nv_scroll_line,	0,			TRUE),
   ACTION(Ctrl_F,	nvPage,	NV_STS,			FORWARD),
   ACTION(Ctrl_G,	nv_ctrlg,	0,			0),
   ACTION(Ctrl_H,	nv_ctrlh,	0,			0),
   ACTION(Ctrl_I,	nv_pcmark,	0,			0),
   ACTION(NL,		nv_down,	0,			FALSE),
   ACTION(Ctrl_K,	nvError,	0,			0),
   ACTION(Ctrl_L,	nv_clear,	0,			0),
   ACTION(ENTER,		nv_down,	0,			TRUE),
   ACTION(Ctrl_N,	nv_down,	NV_STS,			FALSE),
   ACTION(Ctrl_O,	nv_ctrlo,	0,			0),
   ACTION(Ctrl_P,	nv_up,		NV_STS,			FALSE),
   ACTION(Ctrl_Q,	nv_visual,	0,			FALSE),
   ACTION(Ctrl_R,	nv_redo_or_register, 0,			0),
   ACTION(Ctrl_S,	nv_ignore,	0,			0),
   ACTION(Ctrl_T,	nv_tagpop,	NV_NCW,			0),
   ACTION(Ctrl_U,	nv_halfpage,	0,			0),
   ACTION(Ctrl_V,	nv_visual,	0,			FALSE),
   ACTION(Ctrl_W,	nv_portal,	0,			0),
   ACTION(Ctrl_X,	nvAddSub,	0,			0),
   ACTION(Ctrl_Y,	nv_scroll_line,	0,			FALSE),
   ACTION(Ctrl_Z,	nv_suspend,	0,			0),
   ACTION(ESC,		nv_esc,		0,			FALSE),
   ACTION(Ctrl_BSL,	nv_normal,	NV_NCH_ALW,		0),
   ACTION(Ctrl_RSB,	nv_ident,	NV_NCW,			0),
   ACTION(Ctrl_HAT,	nv_hat,		NV_NCW,			0),
   ACTION(Ctrl__,	nvError,	0,			0),
   ACTION(' ',		nv_right,	0,			0),
   ACTION('!',		nv_operator,	0,			0),
   ACTION('"',		nv_regname,	NV_NCH_NOP|NV_KEEPREG,	0),
   ACTION('#',		nv_ident,	0,			0),
   ACTION('$',		nv_dollar,	0,			0),
   ACTION('%',		nv_percent,	0,			0),
   ACTION('&',		nvOperatorAliases,	0,			0),
   ACTION('\'',		nv_gomark,	NV_NCH_ALW,		TRUE),
   ACTION('(',		nv_brace,	0,			BACKWARD),
   ACTION(')',		nv_brace,	0,			FORWARD),
   ACTION('*',		nv_ident,	0,			0),
   ACTION('+',		nv_down,	0,			TRUE),
   ACTION(',',		nv_csearch,	0,			TRUE),
   ACTION('-',		nv_up,		0,			TRUE),
   ACTION('.',		nvDot,		NV_KEEPREG,		0),
   ACTION('/',		nv_search,	0,			FALSE),
   ACTION('0',		nv_beginline,	0,			0),
   ACTION('1',		nv_ignore,	0,			0),
   ACTION('2',		nv_ignore,	0,			0),
   ACTION('3',		nv_ignore,	0,			0),
   ACTION('4',		nv_ignore,	0,			0),
   ACTION('5',		nv_ignore,	0,			0),
   ACTION('6',		nv_ignore,	0,			0),
   ACTION('7',		nv_ignore,	0,			0),
   ACTION('8',		nv_ignore,	0,			0),
   ACTION('9',		nv_ignore,	0,			0),
   ACTION(':',		nv_colon,	0,			0),
   ACTION(';',		nv_csearch,	0,			FALSE),
   ACTION('<',		nv_operator,	NV_RL,			0),
   ACTION('=',		nv_operator,	0,			0),
   ACTION('>',		nv_operator,	NV_RL,			0),
   ACTION('?',		nv_search,	0,			FALSE),
   ACTION('@',		nv_at,		NV_NCH_NOP,		FALSE),
   ACTION('A',		nv_edit,	0,			0),
   ACTION('B',		nv_bck_word,	0,			1),
   ACTION('C',		nv_abbrev,	NV_KEEPREG,		0),
   ACTION('D',		nv_abbrev,	NV_KEEPREG,		0),
   ACTION('E',		nv_wordcmd,	0,			TRUE),
   ACTION('F',		nv_csearch,	NV_NCH_ALW|NV_LANG,	BACKWARD),
   ACTION('G',		nv_goto,	0,			TRUE),
   ACTION('H',		nv_scroll,	0,			0),
   ACTION('I',		nv_edit,	0,			0),
   ACTION('J',		nvJoin,	0,			0),
   ACTION('K',		nv_ident,	0,			0),
   ACTION('L',		nv_scroll,	0,			0),
   ACTION('M',		nv_scroll,	0,			0),
   ACTION('N',		nv_next,	0,			SEARCH_REV),
   ACTION('O',		nvOpen,	0,			0),
   ACTION('P',		nv_put,		0,			0),
   ACTION('S',		nv_subst,	NV_KEEPREG,		0),
   ACTION('T',		nv_csearch,	NV_NCH_ALW|NV_LANG,	BACKWARD),
   ACTION('U',		nv_Undo,	0,			0),
   ACTION('V',		nv_visual,	0,			FALSE),
   ACTION('W',		nv_wordcmd,	0,			TRUE),
   ACTION('X',		nv_abbrev,	NV_KEEPREG,		0),
   ACTION('Y',		nv_abbrev,	NV_KEEPREG,		0),
   ACTION('Z',		nv_Zet,		NV_NCH_NOP|NV_NCW,	0),
   ACTION('[',		nv_brackets,	NV_NCH_ALW,		BACKWARD),
   ACTION('\\',		nvError,	0,			0),
   ACTION(']',		nv_brackets,	NV_NCH_ALW,		FORWARD),
   ACTION('^',		nv_beginline,	0,		    BL_WHITE | BL_FIX),
   ACTION('_',		a_linewiseOperator,	0,			0),
   ACTION('`',		nv_gomark,	NV_NCH_ALW,		FALSE),
   ACTION('a',		nv_edit,	NV_NCH,			0),
   ACTION('b',		nv_bck_word,	0,			0),
   ACTION('c',		nv_operator,	0,			0),
   ACTION('d',		nv_operator,	0,			0),
   ACTION('e',		nv_wordcmd,	0,			FALSE),
   ACTION('f',		nv_csearch,	NV_NCH_ALW|NV_LANG,	FORWARD),
   ACTION('g',		nv_g_cmd,	NV_NCH_ALW,		FALSE),
   ACTION('h',		nv_left,	NV_RL,			0),
   ACTION('i',		nv_edit,	NV_NCH,			0),
   ACTION('j',		nv_down,	0,			FALSE),
   ACTION('k',		nv_up,		0,			FALSE),
   ACTION('l',		nv_right,	NV_RL,			0),
   ACTION('m',		nv_mark,	NV_NCH_NOP,		0),
   ACTION('n',		nv_next,	0,			0),
   ACTION('o',		nvOpen,	0,			0),
   ACTION('p',		nv_put,		0,			0),
   ACTION('q',		nv_record,	NV_NCH,			0),
   ACTION('r',		nv_replace,	NV_NCH_NOP|NV_LANG,	0),
   ACTION('s',		nv_subst,	NV_KEEPREG,		0),
   ACTION('t',		nv_csearch,	NV_NCH_ALW|NV_LANG,	FORWARD),
   ACTION('u',		nv_undo,	0,			0),
   ACTION('v',		nv_visual,	0,			FALSE),
   ACTION('w',		nv_wordcmd,	0,			FALSE),
   ACTION('x',		nv_abbrev,	NV_KEEPREG,		0),
   ACTION('y',		nv_operator,	0,			0),
   ACTION('z',		nv_zet,		NV_NCH_ALW,		0),
   ACTION('{',		nv_findpar,	0,			BACKWARD),
   ACTION('|',		nv_pipe,	0,			0),
   ACTION('}',		nv_findpar,	0,			FORWARD),
   ACTION('~',		nv_tilde,	0,			0),

   // pound sign
   ACTION(POUND,	nv_ident,	0,			0),
   ACTION(K_MOUSEUP,	nv_mousescroll,	0,			MSCR_UP),
   ACTION(K_MOUSEDOWN,	nv_mousescroll, 0,			MSCR_DOWN),
   ACTION(K_MOUSELEFT,	nv_mousescroll, 0,			MSCR_LEFT),
   ACTION(K_MOUSERIGHT, nv_mousescroll, 0,			MSCR_RIGHT),
   ACTION(K_LEFTMOUSE,	nv_mouse,	0,			0),
   ACTION(K_LEFTMOUSE_NM, nv_mouse,	0,			0),
   ACTION(K_LEFTDRAG,	nv_mouse,	0,			0),
   ACTION(K_LEFTRELEASE, nv_mouse,	0,			0),
   ACTION(K_LEFTRELEASE_NM, nv_mouse,	0,			0),
   ACTION(K_MOUSEMOVE,	nv_mouse,	0,			0),
   ACTION(K_MIDDLEMOUSE, nv_mouse,	0,			0),
   ACTION(K_MIDDLEDRAG, nv_mouse,	0,			0),
   ACTION(K_MIDDLERELEASE, nv_mouse,	0,			0),
   ACTION(K_RIGHTMOUSE, nv_mouse,	0,			0),
   ACTION(K_RIGHTDRAG,	nv_mouse,	0,			0),
   ACTION(K_RIGHTRELEASE, nv_mouse,	0,			0),
   ACTION(K_X1MOUSE,	nv_mouse,	0,			0),
   ACTION(K_X1DRAG,	nv_mouse,	0,			0),
   ACTION(K_X1RELEASE,	nv_mouse,	0,			0),
   ACTION(K_X2MOUSE,	nv_mouse,	0,			0),
   ACTION(K_X2DRAG,	nv_mouse,	0,			0),
   ACTION(K_X2RELEASE,	nv_mouse,	0,			0),
   ACTION(K_IGNORE,	nv_ignore,	NV_KEEPREG,		0),
   ACTION(K_NOP,	nv_nop,		0,			0),
   ACTION(K_INS,	nv_edit,	0,			0),
   ACTION(K_KINS,	nv_edit,	0,			0),
   ACTION(K_BS,		nv_ctrlh,	0,			0),
   ACTION(K_UP,		nv_up,		NV_SSS|NV_STS,		FALSE),
   ACTION(K_S_UP,	nvPage,	NV_SS,			BACKWARD),
   ACTION(K_DOWN,	nv_down,	NV_SSS|NV_STS,		FALSE),
   ACTION(K_S_DOWN,	nvPage,	NV_SS,			FORWARD),
   ACTION(K_LEFT,	nv_left,	NV_SSS|NV_STS|NV_RL,	0),
   ACTION(K_S_LEFT,	nv_bck_word,	NV_SS|NV_RL,		0),
   ACTION(K_C_LEFT,	nv_bck_word,	NV_SSS|NV_RL|NV_STS,	1),
   ACTION(K_RIGHT,	nv_right,	NV_SSS|NV_STS|NV_RL,	0),
   ACTION(K_S_RIGHT,	nv_wordcmd,	NV_SS|NV_RL,		FALSE),
   ACTION(K_C_RIGHT,	nv_wordcmd,	NV_SSS|NV_RL|NV_STS,	TRUE),
   ACTION(K_PAGEUP,	nvPage,	NV_SSS|NV_STS,		BACKWARD),
   ACTION(K_KPAGEUP,	nvPage,	NV_SSS|NV_STS,		BACKWARD),
   ACTION(K_PAGEDOWN,	nvPage,	NV_SSS|NV_STS,		FORWARD),
   ACTION(K_KPAGEDOWN,	nvPage,	NV_SSS|NV_STS,		FORWARD),
   ACTION(K_END,	nv_end,		NV_SSS|NV_STS,		FALSE),
   ACTION(K_KEND,	nv_end,		NV_SSS|NV_STS,		FALSE),
   ACTION(K_S_END,	nv_end,		NV_SS,			FALSE),
   ACTION(K_C_END,	nv_end,		NV_SSS|NV_STS,		TRUE),
   ACTION(K_HOME,	nv_home,	NV_SSS|NV_STS,		0),
   ACTION(K_KHOME,	nv_home,	NV_SSS|NV_STS,		0),
   ACTION(K_S_HOME,	nv_home,	NV_SS,			0),
   ACTION(K_C_HOME,	nv_goto,	NV_SSS|NV_STS,		FALSE),
   ACTION(K_DEL,	nv_abbrev,	0,			0),
   ACTION(K_KDEL,	nv_abbrev,	0,			0),
   ACTION(K_UNDO,	nv_kundo,	0,			0),
   ACTION(K_HELP,	nv_help,	NV_NCW,			0),
   ACTION(K_F1,		nv_help,	NV_NCW,			0),
   ACTION(K_XF1,	nv_help,	NV_NCW,			0),
   ACTION(K_VER_SCROLLBAR, NV_VER_SCROLLBAR, 0,			0),
   ACTION(K_HOR_SCROLLBAR, NV_HOR_SCROLLBAR, 0,			0),
   ACTION(K_TABLINE,	NV_TABLINE,	0,			0),
   ACTION(K_TABMENU,	NV_TABMENU,	0,			0),
   ACTION(K_F21,	NV_NBCMD,	NV_NCH_ALW,		0),
   ACTION(K_DROP,	NV_DROP,	NV_STS,			0),
   ACTION(K_CURSORHOLD, nv_cursorhold,	NV_KEEPREG,		0),
   ACTION(K_PS,		nv_edit,	0,			0),
   ACTION(K_COMMAND,	nv_colon,	0,			0),
   ACTION(K_SCRIPT_COMMAND, nv_colon,	0,			0),
};

// Number of commands in actions[].
#define ACTIONS_SIZE sizeof(actions)/sizeof(actions[0])

