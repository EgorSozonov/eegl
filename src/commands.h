//This file defines the commands (stuff you type like `:ls`).
//It can be run in 3 modes:
//- When DO_DECLARE_COMMANDS is defined, the table with command names and options results.
//- When DECLARE_COMMANDS_FOR_INDEXING is defined, the table for generating indices results.
//- When DECLARE_COMMANDS_ENUM is defined, the enum with all the commands results.
//- When DECLARE_COMMANDS_FLAGS is defined, the enum with all the commands results.
//This clever trick was invented by Ron Aaron.

//When adding a command:
//1. Add an entry in the table below.  Keep it sorted on the shortest version of the command name
//that works.  If it doesn't start with a lowercase letter, add it at the end.
//2. Run "make indices" to re-generate src/indices/commands.h.
//3. Add a "case: C_xxx" in the big switch in {script.c:setContextByCommandName}
//4. Add an entry in the index for Commands at ":help ex-cmd-index".
//5. Add documentation in doc/xxx.txt. Add a tag for both the short and long name of the command

#if defined(DO_DECLARE_COMMANDS) || defined(DECLARE_COMMANDS_FOR_INDEXING)\
   || defined(DECLARE_COMMANDS_FLAGS)

#define RANGE     0x001 // allow a linespecs
#define BANG      0x002 // allow a ! after the command name
#define EXTRA     0x004 // allow extra args after command name
#define XFILE     0x008 // expand wildcards in extra part
#define NOSPC_IN_EXTRA  0x010 // no spaces allowed in the extra part
#define DFLALL    0x020 // default file range is 1,$
#define WHOLEFOLD 0x040 // extend range to include whole fold also when less than 2 numbers given
#define NEEDARG   0x080 // argument required
#define TRLBAR    0x100 // check for trailing vertical bar
#define REGSTR    0x200 // allow "x for register designation
#define COUNT     0x400 // allow count in argument, after command
#define NOTRLCOM  0x800 // no trailing comment allowed
#define ZERO_LINE_OK 0x1000  // zero line number allowed
#define CTRLV     0x2000  // do not remove CTRL-V from argument
#define CMDARG    0x4000  // allow "+command" argument
#define BUFNAME   0x8000  // accepts book name
#define BUFUNL    0x10000 // accepts unlisted book too
#define ARGOPT    0x20000 // allow "++opt=val" argument
#define COMMPORT  0x80000 // allowed in command line portal
#define MODIFY    0x100000 // forbidden in non-'modifiable' book
#define FLAGS     0x200000 // allow flags after count in argument
#define EXPAND    0x800000 // expands wildcards later
#define LOCK_OK   0x1000000 // command can be executed when textlock is set; when missing, 
                               // disallows editing another book when curbuf_lock is set
#define NONWHITE_OK 0x2000000 // command can be followed by non-white
#define KEEPSCRIPT  0x4000000 // keep sctx of where command was invoked
#define EXPR_ARG    0x8000000 // argument is an expression

#define FILES (XFILE | EXTRA) // multiple extra files allowed
#define FILE1 (FILES | NOSPC_IN_EXTRA) // 1 file, defaults to current file
#define WORD1 (EXTRA | NOSPC_IN_EXTRA) // one extra word allowed

#endif


#ifdef DO_DECLARE_COMMANDS // Full table 

#define C(a, b, c, d, e) {(CS)b, STRLEN_LITERAL(b), c, (Ulong)(d), e}

typedef void (*CommFn) (Invocation* invo);

typedef struct {
   Arr(Byte) name;   // name of the command
   Unt   nameLen;   // length of the command name
   CommFn   fn;   // function for this command
   Ulong   flags;   // flags declared above
   CommandAddress   addressKind;   // flag for address type
} CommandDef; 

static CommandDef commands[] =

#endif


#ifdef DECLARE_COMMANDS_FOR_INDEXING // List for generation of indices in indices/createIndices.c

typedef struct {
   Arr(Byte) name;   // name of the command
   Ulong flags;   // flags declared above
   CommandAddress   addressKind;   // flag for address type
} CommandForIndexing;

#define C(a, b, c, d, e)  {(Byte *)b, (Ulong)(d), e}

static CommandForIndexing commands[] =
#endif


#ifdef DECLARE_COMMANDS_ENUM

typedef enum {
   ADDR_LINES,       // book line numbers
   ADDR_PORTALS,    // portal number
   ADDR_ARGUMENTS,    // argument number
   ADDR_LOADED_BUFFERS, // book number of loaded book
   ADDR_BUFFERS,    // book number
   ADDR_TABS,       // tab number
   ADDR_TABS_RELATIVE,    // Tab page that only relative
   ADDR_QUICKFIX_VALID, // quickfix list valid entry number
   ADDR_QUICKFIX,    // quickfix list entry number
   ADDR_UNSIGNED,    // positive count or zero, defaults to 1
   ADDR_OTHER,       // something else, use line number for '$', '%', etc.
   ADDR_NONE       // no range used
} CommandAddress;


typedef struct Invocation Invocation;

#define C(a, b, c, d, e)  a

enum CommIndex
#endif

#if defined(DO_DECLARE_COMMANDS) || defined(DECLARE_COMMANDS_ENUM)\
   || defined(DECLARE_COMMANDS_FOR_INDEXING)
// This array declares and defines all built-in Commands. The order in which commands are listed 
// is SIGNIFICANT -- ambiguous abbreviations are always resolved to be the first possible match
// (e.g. "r" is taken to mean "read", not "rewind", because "read" comes before "rewind").
// Unsupported commands are included to avoid ambiguities.
{
C(C_abbreviate, "abbreviate", c_abbreviate, 
      EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE
),
C(C_abclear, "abclear", c_abclear, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_aboveleft, "aboveleft",   c_wrongmodifier, NEEDARG|EXTRA|NOTRLCOM, ADDR_NONE),
C(C_all, "all", c_all, BANG|RANGE|COUNT|TRLBAR, ADDR_OTHER),
C(C_append,   "append", c_append,
   BANG|RANGE|ZERO_LINE_OK|TRLBAR|COMMPORT|LOCK_OK|MODIFY, ADDR_LINES
),
C(C_args, "args", c_args, BANG|FILES|CMDARG|ARGOPT|TRLBAR, ADDR_NONE),
C(C_argadd, "argadd", c_argadd, BANG|RANGE|ZERO_LINE_OK|FILES|TRLBAR, ADDR_ARGUMENTS),
C(C_argdedupe, "argdedupe",   c_argdedupe, TRLBAR, ADDR_NONE),
C(C_argdelete,   "argdelete", c_argdelete, BANG|RANGE|FILES|TRLBAR, ADDR_ARGUMENTS),
C(C_argdo, "argdo", c_listDo, BANG|NEEDARG|EXTRA|NOTRLCOM|RANGE|DFLALL|EXPAND, ADDR_ARGUMENTS),
C(C_argedit, "argedit", c_argedit,
   BANG|NEEDARG|RANGE|ZERO_LINE_OK|FILES|CMDARG|ARGOPT|TRLBAR, ADDR_ARGUMENTS
),
C(C_argglobal, "argglobal", c_args, BANG|FILES|CMDARG|ARGOPT|TRLBAR, ADDR_NONE),
C(C_arglocal,   "arglocal",   c_args, BANG|FILES|CMDARG|ARGOPT|TRLBAR, ADDR_NONE),
C(C_argument, "argument", c_argument, BANG|RANGE|COUNT|EXTRA|CMDARG|ARGOPT|TRLBAR, ADDR_ARGUMENTS),
C(C_ascii, "ascii", do_ascii, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_augroup, "augroup", &c_autocmd, BANG|WORD1|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_autocmd,   "autocmd",   &c_autocmd, BANG|EXTRA|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_badd, "badd",      c_edit, NEEDARG|FILE1|CMDARG|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_ball,      "ball",      c_bookAll, RANGE|COUNT|TRLBAR, ADDR_OTHER),
C(C_balt,      "balt",      c_edit, NEEDARG|FILE1|CMDARG|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_bNext,   "bNext",   &c_bprevious, BANG|RANGE|COUNT|CMDARG|TRLBAR, ADDR_OTHER),
C(C_bdelete,   "bdelete",   c_bunload, BANG|RANGE|BUFNAME|COUNT|EXTRA|TRLBAR, ADDR_BUFFERS),
C(C_belowright,   "belowright",   c_wrongmodifier, NEEDARG|EXTRA|NOTRLCOM, ADDR_NONE),
C(C_bfirst,   "bfirst",   &c_brewind, BANG|RANGE|CMDARG|TRLBAR, ADDR_OTHER),
C(C_blast,   "blast",   c_blast, BANG|RANGE|CMDARG|TRLBAR, ADDR_OTHER),
C(C_bmodified,   "bmodified",   &c_bmodified, BANG|RANGE|COUNT|CMDARG|TRLBAR, ADDR_OTHER),
C(C_bnext, "bnext", &c_bnext, BANG|RANGE|COUNT|CMDARG|TRLBAR, ADDR_OTHER),
C(C_book, "book", &c_book, BANG|RANGE|BUFNAME|BUFUNL|COUNT|EXTRA|CMDARG|TRLBAR, ADDR_BUFFERS),
C(C_books,   "books",   bookListFiles, BANG|EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_botright,   "botright",   c_wrongmodifier, NEEDARG|EXTRA|NOTRLCOM, ADDR_NONE),
C(C_bprevious,   "bprevious",   &c_bprevious, BANG|RANGE|COUNT|CMDARG|TRLBAR, ADDR_OTHER),
C(C_brewind,   "brewind",   &c_brewind, BANG|RANGE|CMDARG|TRLBAR, ADDR_OTHER),
C(C_break,   "break",   c_break, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_breakadd,   "breakadd",   c_breakadd, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_breakdel,   "breakdel",   c_breakdel, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_breaklist,   "breaklist", c_breaklist, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_browse, "browse", c_wrongmodifier, NEEDARG|EXTRA|NOTRLCOM|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_bufdo,   "bufdo",   c_listDo, BANG|NEEDARG|EXTRA|NOTRLCOM|RANGE|DFLALL|EXPAND, ADDR_BUFFERS),
C(C_bunload,   "bunload",   c_bunload, BANG|RANGE|BUFNAME|COUNT|EXTRA|TRLBAR, ADDR_LOADED_BUFFERS),
C(C_bwipeout, "bwipeout", c_bunload, BANG|RANGE|BUFNAME|BUFUNL|COUNT|EXTRA|TRLBAR, ADDR_BUFFERS),
C(C_change, "change", c_change, 
      BANG|WHOLEFOLD|RANGE|COUNT|TRLBAR|COMMPORT|LOCK_OK|MODIFY, ADDR_LINES
),
C(C_cabbrev, "cabbrev", c_abbreviate, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_cabclear,   "cabclear",   c_abclear, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_call,      "call",      c_call, 
      RANGE|NEEDARG|EXTRA|NOTRLCOM|EXPR_ARG|COMMPORT|LOCK_OK, ADDR_LINES
),
C(C_catch,   "catch",   c_catch, EXTRA|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_cd,      "cd",      c_cd, BANG|FILE1|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_center, "center", c_align, TRLBAR|RANGE|WHOLEFOLD|EXTRA|COMMPORT|LOCK_OK|MODIFY, ADDR_LINES),
C(C_chdir,   "chdir", c_cd, BANG|FILE1|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_changes, "changes", c_changes, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_checkpath,   "checkpath",   c_checkpath, TRLBAR|BANG|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_checktime,   "checktime",   c_checktime, RANGE|BUFNAME|COUNT|EXTRA|TRLBAR, ADDR_OTHER),
C(C_clast,   "clast",   c_lMove, RANGE|COUNT|TRLBAR|BANG, ADDR_UNSIGNED),
C(C_close,   "close",   c_close, BANG|RANGE|COUNT|TRLBAR|COMMPORT|LOCK_OK, ADDR_PORTALS),
C(C_clearjumps,   "clearjumps",   c_clearjumps, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_cmap,      "cmap",      c_map, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_cmapclear,   "cmapclear",   c_mapclear, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_cnoremap,   "cnoremap",   c_map, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_cnoreabbrev, "cnoreabbrev", c_abbreviate, 
   EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE
),
C(C_copy, "copy", c_copymove, RANGE|WHOLEFOLD|EXTRA|TRLBAR|COMMPORT|LOCK_OK|MODIFY, ADDR_LINES),
C(C_comclear,   "comclear",   c_comclear, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_command,   "command",   c_command, EXTRA|BANG|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_compiler,   "compiler",   c_compiler, BANG|TRLBAR|WORD1|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_continue,   "continue",   c_continue, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_confirm,   "confirm",   c_wrongmodifier, NEEDARG|EXTRA|NOTRLCOM|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_const,   "const",   c_let, EXTRA|BANG|NOTRLCOM|EXPR_ARG|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_cquit,   "cquit",   c_cquit, RANGE|COUNT|ZERO_LINE_OK|TRLBAR|BANG, ADDR_UNSIGNED),
C(C_cscope,   "cscope",   c_cscope, EXTRA|NOTRLCOM|XFILE, ADDR_NONE),
C(C_cstag,   "cstag",   c_cstag, BANG|TRLBAR|WORD1, ADDR_NONE),
C(C_cunmap,   "cunmap",   c_unmap, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_cunabbrev, "cunabbrev", c_abbreviate, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_cwindow,   "cwindow",   c_cPortal, RANGE|COUNT|TRLBAR, ADDR_OTHER),
C(C_delete,   "delete",   c_operators,
   RANGE|WHOLEFOLD|REGSTR|COUNT|TRLBAR|COMMPORT|LOCK_OK|MODIFY, ADDR_LINES
),
C(C_delmarks,   "delmarks",   c_delmarks, BANG|EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_debug,   "debug",   c_debug, NEEDARG|EXTRA|NOTRLCOM|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_debuggreedy, "debuggreedy", c_debuggreedy, 
      RANGE|ZERO_LINE_OK|TRLBAR|COMMPORT|LOCK_OK, ADDR_OTHER
),
C(C_defer,   "defer", c_call, NEEDARG|EXTRA|NOTRLCOM|EXPR_ARG|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_delcommand,   "delcommand",   c_delcommand, NEEDARG|WORD1|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_delfunction, "delfunction",   c_delfunction, BANG|NEEDARG|WORD1|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_display,   "display",   c_display, EXTRA|NOTRLCOM|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_diffupdate,   "diffupdate",   c_diffupdate, BANG|TRLBAR, ADDR_NONE),
C(C_diffget,   "diffget",   c_diffgetput, RANGE|ZERO_LINE_OK|EXTRA|TRLBAR|MODIFY, ADDR_LINES),
C(C_diffoff,   "diffoff",   c_diffoff, BANG|TRLBAR, ADDR_NONE),
C(C_diffpatch,   "diffpatch",   c_diffpatch, EXTRA|FILE1|TRLBAR|MODIFY, ADDR_NONE),
C(C_diffput,   "diffput",   c_diffgetput, RANGE|ZERO_LINE_OK|EXTRA|TRLBAR, ADDR_LINES),
C(C_diffsplit,   "diffsplit",   c_diffsplit, EXTRA|FILE1|TRLBAR, ADDR_NONE),
C(C_diffthis,   "diffthis",   c_diffthis, TRLBAR, ADDR_NONE),
C(C_djump,   "djump",   c_findpat, BANG|RANGE|DFLALL|WHOLEFOLD|EXTRA, ADDR_LINES),
C(C_dlist,   "dlist",   c_findpat, BANG|RANGE|DFLALL|WHOLEFOLD|EXTRA|COMMPORT|LOCK_OK, ADDR_LINES),
C(C_doautoall,   "doautoall",   c_doautoall, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_doautocmd,   "doautocmd",   c_doautocmd, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_drop,      "drop",      c_drop, BANG|FILES|CMDARG|NEEDARG|ARGOPT|TRLBAR, ADDR_NONE),
C(C_dsearch, "dsearch", c_findpat, BANG|RANGE|DFLALL|WHOLEFOLD|EXTRA|COMMPORT|LOCK_OK, ADDR_LINES),
C(C_dsplit,   "dsplit",   c_findpat, BANG|RANGE|DFLALL|WHOLEFOLD|EXTRA, ADDR_LINES),
C(C_edit,      "edit",      c_edit, BANG|FILE1|CMDARG|ARGOPT|TRLBAR, ADDR_NONE),
C(C_earlier,   "earlier",   c_later, TRLBAR|EXTRA|NOSPC_IN_EXTRA|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_echo,      "echo",      c_echo, EXTRA|NOTRLCOM|EXPR_ARG|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_echoerr,   "echoerr",   c_execute, EXTRA|NOTRLCOM|EXPR_ARG|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_echohl,   "echohl",   c_echohl, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_echomsg,   "echomsg",   c_execute, EXTRA|NOTRLCOM|EXPR_ARG|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_echoconsole,   "echoconsole",   c_execute, 
      EXTRA|NOTRLCOM|EXPR_ARG|COMMPORT|LOCK_OK, ADDR_NONE
),
C(C_echon,   "echon",   c_echo, EXTRA|NOTRLCOM|EXPR_ARG|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_echowindow,   "echowindow",   c_execute, 
      RANGE|EXTRA|NOTRLCOM|EXPR_ARG|COMMPORT|LOCK_OK, ADDR_OTHER
),
C(C_elck,   "elck",   c_elgrep, RANGE|BANG|NEEDARG|EXTRA|NOTRLCOM|TRLBAR|XFILE, ADDR_OTHER),
C(C_elckadd,   "elckadd",   c_elgrep, RANGE|BANG|NEEDARG|EXTRA|NOTRLCOM|TRLBAR|XFILE, ADDR_OTHER),
C(C_else,      "else",      c_else, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_elseif,   "elseif",   c_else, EXTRA|NOTRLCOM|EXPR_ARG|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_endif,   "endif",   c_endif, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_endinterface,   "endinterface",   c_wrongmodifier, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_endclass,   "endclass",   c_wrongmodifier, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_enddef,   "enddef",   c_endfunction, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_endenum,   "endenum",   c_wrongmodifier, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_endfunction,   "endfunction",   c_endfunction, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_endfor,   "endfor",   c_endwhile, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_endtry,   "endtry",   c_endtry, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_endwhile,   "endwhile",   c_endwhile, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_enew,      "enew",      c_edit, BANG|TRLBAR, ADDR_NONE),
C(C_eval,      "eval",      c_eval, EXTRA|NOTRLCOM|EXPR_ARG|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_ex,      "ex",      c_edit, BANG|FILE1|CMDARG|ARGOPT|TRLBAR, ADDR_NONE),
C(C_execute,   "execute",   c_execute, EXTRA|NOTRLCOM|EXPR_ARG|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_exit,      "exit", c_exit, 
      RANGE|WHOLEFOLD|BANG|FILE1|ARGOPT|DFLALL|TRLBAR|COMMPORT|LOCK_OK, ADDR_LINES
),
C(C_exusage,   "exusage",   c_exusage, TRLBAR, ADDR_NONE),
C(C_file,      "file",      c_file, RANGE|ZERO_LINE_OK|BANG|FILE1|TRLBAR, ADDR_OTHER),
C(C_files,   "files",   bookListFiles, BANG|EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_filetype,   "filetype",   c_filetype, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_filter,   "filter",   c_wrongmodifier, BANG|NEEDARG|EXTRA|NOTRLCOM, ADDR_NONE),
C(C_find,      "find",      c_find, RANGE|BANG|FILE1|CMDARG|ARGOPT|TRLBAR|NEEDARG, ADDR_OTHER),
C(C_final,   "final",   c_let, EXTRA|NOTRLCOM|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_finally,   "finally",   c_finally, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_finish, "finish", c_finish, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_first,   "first",   c_rewind, EXTRA|BANG|CMDARG|ARGOPT|TRLBAR, ADDR_NONE),
C(C_fixdel,   "fixdel",   do_fixdel, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_fold,      "fold",      c_fold, RANGE|WHOLEFOLD|TRLBAR|COMMPORT|LOCK_OK, ADDR_LINES),
C(C_foldclose,   "foldclose",   c_foldopen, 
      RANGE|BANG|WHOLEFOLD|TRLBAR|COMMPORT|LOCK_OK, ADDR_LINES
),
C(C_folddoopen,   "folddoopen",   c_folddo, RANGE|DFLALL|NEEDARG|EXTRA|NOTRLCOM, ADDR_LINES),
C(C_folddoclosed,   "folddoclosed",   c_folddo, RANGE|DFLALL|NEEDARG|EXTRA|NOTRLCOM, ADDR_LINES),
C(C_foldopen,   "foldopen",   c_foldopen, 
      RANGE|BANG|WHOLEFOLD|TRLBAR|COMMPORT|LOCK_OK, ADDR_LINES
),
C(C_for,      "for",      c_while, EXTRA|NOTRLCOM|EXPR_ARG|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_function, "function", c_function, EXTRA|BANG|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_get,   "get",   c_get, EXTRA|COMMPORT|LOCK_OK|NONWHITE_OK, ADDR_NONE),
C(C_getGlobal,   "getglobal",   c_get, EXTRA|COMMPORT|LOCK_OK|NONWHITE_OK, ADDR_NONE),
C(C_global,   "global",   c_global, 
      RANGE|WHOLEFOLD|BANG|EXTRA|DFLALL|COMMPORT|LOCK_OK|NONWHITE_OK, ADDR_LINES
),
C(C_goto,      "goto",      c_goto, RANGE|COUNT|TRLBAR|COMMPORT|LOCK_OK, ADDR_OTHER),
C(C_grep,      "grep",      c_make, RANGE|BANG|NEEDARG|EXTRA|NOTRLCOM|TRLBAR|XFILE, ADDR_OTHER),
C(C_grepadd,   "grepadd",   c_make, RANGE|BANG|NEEDARG|EXTRA|NOTRLCOM|TRLBAR|XFILE, ADDR_OTHER),
C(C_help,      "help",      c_help, BANG|EXTRA|NOTRLCOM, ADDR_NONE),
C(C_helpclose,   "helpclose",   c_helpclose, RANGE|COUNT|TRLBAR, ADDR_OTHER),
C(C_helpgrep,   "helpgrep",   c_helpgrep, EXTRA|NOTRLCOM|NEEDARG, ADDR_NONE),
C(C_helptags,   "helptags",   c_helptags, NEEDARG|FILES|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_highlight,   "highlight",   &c_hilite, BANG|EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_hide,  "hide",      c_hide, BANG|RANGE|COUNT|EXTRA|TRLBAR, ADDR_PORTALS),
C(C_history,   "history",   c_history, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_horizontal, "horizontal",   c_wrongmodifier, NEEDARG|EXTRA|NOTRLCOM, ADDR_NONE),
C(C_insert,   "insert",   c_append, BANG|RANGE|TRLBAR|COMMPORT|LOCK_OK|MODIFY, ADDR_LINES),
C(C_iabbrev, "iabbrev",   c_abbreviate, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_iabclear,   "iabclear",   c_abclear, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_if,      "if",      c_if, EXTRA|NOTRLCOM|EXPR_ARG|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_ijump,   "ijump",   c_findpat, BANG|RANGE|DFLALL|WHOLEFOLD|EXTRA, ADDR_LINES),
C(C_ilist,   "ilist", c_findpat, BANG|RANGE|DFLALL|WHOLEFOLD|EXTRA|COMMPORT|LOCK_OK, ADDR_LINES),
C(C_imap,      "imap",      c_map, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_imapclear,   "imapclear",   c_mapclear, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_inoremap,   "inoremap",   c_map, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_inoreabbrev,   "inoreabbrev",   
      c_abbreviate, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE
),
C(C_intro,   "intro",   c_intro, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_iput,      "iput",      c_iput, 
      RANGE|WHOLEFOLD|BANG|REGSTR|TRLBAR|ZERO_LINE_OK|COMMPORT|LOCK_OK|MODIFY, ADDR_LINES
),
C(C_isearch, "isearch", c_findpat, BANG|RANGE|DFLALL|WHOLEFOLD|EXTRA|COMMPORT|LOCK_OK, ADDR_LINES),
C(C_isplit,   "isplit",   c_findpat, BANG|RANGE|DFLALL|WHOLEFOLD|EXTRA, ADDR_LINES),
C(C_iunmap,   "iunmap",   c_unmap, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_iunabbrev, "iunabbrev", c_abbreviate, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_join,      "join",      c_join,
   BANG|RANGE|WHOLEFOLD|COUNT|FLAGS|TRLBAR|COMMPORT|LOCK_OK|MODIFY, ADDR_LINES
),
C(C_jumps,   "jumps",   c_jumps, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_k,      "k",      c_mark, RANGE|WORD1|TRLBAR|COMMPORT|LOCK_OK|NONWHITE_OK, ADDR_LINES),
C(C_keepmarks,   "keepmarks",   c_wrongmodifier, NEEDARG|EXTRA|NOTRLCOM, ADDR_NONE),
C(C_keepjumps, "keepjumps", c_wrongmodifier, NEEDARG|EXTRA|NOTRLCOM, ADDR_NONE),
C(C_keeppatterns, "keeppatterns", c_wrongmodifier, NEEDARG|EXTRA|NOTRLCOM, ADDR_NONE),
C(C_keepalt, "keepalt", c_wrongmodifier, NEEDARG|EXTRA|NOTRLCOM, ADDR_NONE),
C(C_list, "list", c_print, RANGE|WHOLEFOLD|COUNT|FLAGS|TRLBAR|COMMPORT|LOCK_OK, ADDR_LINES),
C(C_last,      "last",      c_last, EXTRA|BANG|CMDARG|ARGOPT|TRLBAR, ADDR_NONE),
C(C_labove,   "labove",   c_lBelow, RANGE|COUNT|TRLBAR, ADDR_UNSIGNED),
C(C_language,   "language",   c_language, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_laddexpr,   "laddexpr",   c_lExpr, NEEDARG|WORD1|NOTRLCOM|EXPR_ARG, ADDR_NONE),
C(C_laddbook,   "laddbook",   c_lbook, RANGE|WORD1|TRLBAR, ADDR_LINES),
C(C_laddfile,   "laddfile",   c_lFile, TRLBAR|FILE1, ADDR_NONE),
C(C_later,   "later",   c_later, TRLBAR|EXTRA|NOSPC_IN_EXTRA|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_lbook,   "lbuffer",   c_lbook, BANG|RANGE|WORD1|TRLBAR, ADDR_LINES),
C(C_lbelow,   "lbelow",   c_lBelow, RANGE|COUNT|TRLBAR, ADDR_UNSIGNED),
C(C_lbottom,   "lbottom",   c_lBottom, TRLBAR, ADDR_NONE),
C(C_lcd,      "lcd",      c_cd, BANG|FILE1|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_lchdir,   "lchdir",   c_cd, BANG|FILE1|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_lclose,   "lclose",   c_lClose, RANGE|COUNT|TRLBAR, ADDR_OTHER),
C(C_lcscope,   "lcscope",   c_cscope, EXTRA|NOTRLCOM|XFILE, ADDR_NONE),
C(C_ldo,   "ldo",   c_listDo, EXTRA|NOTRLCOM|XFILE, ADDR_NONE),
C(C_left,      "left", c_align, TRLBAR|RANGE|WHOLEFOLD|EXTRA|COMMPORT|LOCK_OK|MODIFY, ADDR_LINES),
C(C_leftabove, "leftabove",   c_wrongmodifier, NEEDARG|EXTRA|NOTRLCOM, ADDR_NONE),
C(C_let,      "let",      c_let, EXTRA|NOTRLCOM|EXPR_ARG|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_lexpr,   "lexpr",   c_lExpr, NEEDARG|WORD1|NOTRLCOM|EXPR_ARG|BANG, ADDR_NONE),
C(C_legacy,   "legacy",   c_wrongmodifier, NEEDARG|EXTRA|NOTRLCOM|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_lfile,   "lfile",   c_lFile, TRLBAR|FILE1|BANG, ADDR_NONE),
C(C_lfdo, "lfdo", &c_listDo, BANG|NEEDARG|EXTRA|NOTRLCOM|RANGE|DFLALL|EXPAND, ADDR_QUICKFIX_VALID),
C(C_lfirst,   "lfirst",   c_lMove, RANGE|COUNT|TRLBAR|BANG, ADDR_UNSIGNED),
C(C_lgetexpr,   "lgetexpr",   c_lExpr, NEEDARG|WORD1|NOTRLCOM|EXPR_ARG, ADDR_NONE),
C(C_ll,      "ll",      c_lMove, RANGE|COUNT|TRLBAR|BANG, ADDR_QUICKFIX),
C(C_llast,   "llast",   c_lMove, RANGE|COUNT|TRLBAR|BANG, ADDR_UNSIGNED),
C(C_llist,   "llist",   c_list, BANG|EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_lmap,      "lmap",      c_map, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_lmapclear,   "lmapclear",   c_mapclear, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_lnoremap, "lnoremap",   c_map, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_lnext, "lnext", &c_lNext, RANGE|COUNT|NEEDARG|EXTRA|TRLBAR|BANG, ADDR_UNSIGNED),
C(C_lnewer,   "lnewer", &c_llAge, RANGE|COUNT|TRLBAR, ADDR_UNSIGNED),
C(C_lnfile,   "lnfile",   &c_lNext, RANGE|COUNT|TRLBAR|BANG, ADDR_UNSIGNED),
C(C_lockmarks,   "lockmarks",   c_wrongmodifier, NEEDARG|EXTRA|NOTRLCOM, ADDR_NONE),
C(C_lockvar,   "lockvar",   c_lockvar, BANG|EXTRA|NEEDARG|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_lolder,   "lolder",   c_llAge, RANGE|COUNT|TRLBAR, ADDR_UNSIGNED),
C(C_lopen,   "lopen",   c_lOpen, RANGE|COUNT|TRLBAR, ADDR_OTHER),
C(C_lprevious,   "lprevious",   c_lNext, RANGE|COUNT|NEEDARG|EXTRA|TRLBAR|BANG, ADDR_UNSIGNED),
C(C_lpfile,   "lpfile",   c_lNext, RANGE|COUNT|TRLBAR|BANG, ADDR_OTHER),
C(C_lrewind,   "lrewind",   c_lMove, RANGE|COUNT|TRLBAR|BANG, ADDR_UNSIGNED),
C(C_ls, "ls", bookListFiles, BANG|EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_ltag,      "ltag",   c_tag, TRLBAR|BANG|WORD1, ADDR_NONE),
C(C_lunmap,   "lunmap",   c_unmap, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_lwindow,   "lwindow",   c_cPortal, RANGE|COUNT|TRLBAR, ADDR_OTHER),
C(C_maddbuffer,   "maddbuffer",   c_lbook, RANGE|WORD1|TRLBAR, ADDR_LINES),
C(C_maddexpr,   "maddexpr",   c_lExpr, NEEDARG|WORD1|NOTRLCOM|EXPR_ARG, ADDR_NONE),
C(C_maddfile,   "maddfile",   c_lFile, TRLBAR|FILE1, ADDR_NONE),
C(C_make,      "make",      c_make, BANG|EXTRA|NOTRLCOM|TRLBAR|XFILE, ADDR_NONE),
C(C_mark,      "mark",      c_mark, RANGE|WORD1|TRLBAR|COMMPORT|LOCK_OK, ADDR_LINES),
C(C_mabove,   "mabove",   c_lBelow, RANGE|COUNT|TRLBAR, ADDR_UNSIGNED),
C(C_map,      "map",      c_map,  BANG|EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_mapclear,   "mapclear",   c_mapclear, EXTRA|BANG|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_marks,   "marks",   c_marks, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_match,   "match",   c_match, RANGE|EXTRA|COMMPORT|LOCK_OK, ADDR_OTHER),
C(C_messages,   "messages",   c_messages, EXTRA|TRLBAR|RANGE|COMMPORT|LOCK_OK, ADDR_OTHER),
C(C_mkexrc,   "mkexrc",   c_mkrc, BANG|FILE1|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_mksession,   "mksession",   c_mkrc, BANG|FILE1|TRLBAR, ADDR_NONE),
C(C_mkvimrc,   "mkvimrc",   c_mkrc, BANG|FILE1|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_mode,      "mode",      c_mode, WORD1|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_move, "move", c_copymove, RANGE|WHOLEFOLD|EXTRA|TRLBAR|COMMPORT|LOCK_OK|MODIFY, ADDR_LINES),
C(C_next,      "next",      c_next, RANGE|BANG|FILES|CMDARG|ARGOPT|TRLBAR, ADDR_OTHER),
C(C_new,      "new",      c_splitview, BANG|FILE1|RANGE|CMDARG|ARGOPT|TRLBAR, ADDR_OTHER),
C(C_nmap,      "nmap",      c_map, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_nmapclear,   "nmapclear",   c_mapclear, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_nnoremap,   "nnoremap",   c_map, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_noremap,   "noremap",   c_map, BANG|EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_noautocmd,   "noautocmd",   c_wrongmodifier, NEEDARG|EXTRA|NOTRLCOM, ADDR_NONE),
C(C_nohlsearch,   "nohlsearch",   c_nohlsearch, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_noreabbrev, "noreabbrev", c_abbreviate, 
      EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE
),
C(C_noswapfile,   "noswapfile",   c_wrongmodifier, NEEDARG|EXTRA|NOTRLCOM, ADDR_NONE),
C(C_normal, "normal", c_normal, 
      RANGE|BANG|EXTRA|NEEDARG|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_LINES
),
C(C_number,   "number",   c_print, RANGE|WHOLEFOLD|COUNT|FLAGS|TRLBAR|COMMPORT|LOCK_OK, ADDR_LINES),
C(C_nunmap,   "nunmap",   c_unmap, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_open,      "open",      c_open, RANGE|BANG|EXTRA, ADDR_LINES),
C(C_oldfiles,   "oldfiles",   c_oldfiles, BANG|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_omap,      "omap",      c_map, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_omapclear,   "omapclear",   c_mapclear, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_only,      "only",      c_only, BANG|RANGE|COUNT|TRLBAR, ADDR_PORTALS),
C(C_onoremap,   "onoremap",   c_map, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_options,   "options",   c_options, TRLBAR, ADDR_NONE),
C(C_ounmap,   "ounmap",   c_unmap, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_ownsyntax,   "ownsyntax",   c_ownsyntax, EXTRA|NOTRLCOM|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_print,   "print",   c_print, 
      RANGE|WHOLEFOLD|COUNT|FLAGS|TRLBAR|COMMPORT|LOCK_OK, ADDR_LINES
),
C(C_packadd, "packadd", c_packadd, BANG|FILE1|NEEDARG|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_packloadall,   "packloadall", c_packloadall, BANG|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_pbuffer, "pbuffer", c_pbuffer, 
      BANG|RANGE|BUFNAME|BUFUNL|COUNT|EXTRA|CMDARG|TRLBAR, ADDR_BUFFERS
),
C(C_pclose,   "pclose", c_pclose, BANG|TRLBAR, ADDR_NONE),
C(C_pedit,   "pedit",   c_pedit, BANG|FILE1|CMDARG|ARGOPT|TRLBAR, ADDR_NONE),
C(C_pop,      "pop",    c_tag, RANGE|BANG|COUNT|TRLBAR|ZERO_LINE_OK, ADDR_OTHER),
C(C_ppop,      "ppop",  c_ptag, RANGE|BANG|COUNT|TRLBAR|ZERO_LINE_OK, ADDR_OTHER),
C(C_preserve,   "preserve", c_preserve, TRLBAR, ADDR_NONE),
C(C_previous,   "previous", c_previous, EXTRA|RANGE|COUNT|BANG|CMDARG|ARGOPT|TRLBAR, ADDR_OTHER),
C(C_psearch,   "psearch",   c_psearch, BANG|RANGE|WHOLEFOLD|DFLALL|EXTRA, ADDR_LINES),
C(C_ptag,      "ptag",      c_ptag, RANGE|BANG|WORD1|TRLBAR|ZERO_LINE_OK, ADDR_OTHER),
C(C_ptNext,   "ptNext",   c_ptag, RANGE|BANG|TRLBAR|ZERO_LINE_OK, ADDR_OTHER),
C(C_ptfirst,   "ptfirst", c_ptag, RANGE|BANG|TRLBAR|ZERO_LINE_OK, ADDR_OTHER),
C(C_ptjump,   "ptjump",   c_ptag, BANG|TRLBAR|WORD1, ADDR_NONE),
C(C_ptlast,   "ptlast",   c_ptag, BANG|TRLBAR, ADDR_NONE),
C(C_ptnext,   "ptnext",   c_ptag, RANGE|BANG|TRLBAR|ZERO_LINE_OK, ADDR_OTHER),
C(C_ptprevious,   "ptprevious", c_ptag, RANGE|BANG|TRLBAR|ZERO_LINE_OK, ADDR_OTHER),
C(C_ptrewind,   "ptrewind",   c_ptag, RANGE|BANG|TRLBAR|ZERO_LINE_OK, ADDR_OTHER),
C(C_ptselect,   "ptselect",   c_ptag, BANG|TRLBAR|WORD1, ADDR_NONE),
C(C_put,      "put",      c_put,
   RANGE|WHOLEFOLD|BANG|REGSTR|TRLBAR|ZERO_LINE_OK|COMMPORT|LOCK_OK|MODIFY, ADDR_LINES
),
C(C_public,   "public",   c_wrongmodifier, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_pwd,      "pwd",      c_pwd, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_quit,      "quit",      c_quit, BANG|RANGE|COUNT|TRLBAR|COMMPORT|LOCK_OK, ADDR_PORTALS),
C(C_quitall,   "quitall",   c_quit_all, BANG|TRLBAR, ADDR_NONE),
C(C_qall,      "qall",      c_quit_all, BANG|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_read,      "read",      c_read,
   BANG|RANGE|WHOLEFOLD|FILE1|ARGOPT|TRLBAR|ZERO_LINE_OK|COMMPORT|LOCK_OK|MODIFY, ADDR_LINES
),
C(C_recover,   "recover",   c_recover, BANG|FILE1|TRLBAR, ADDR_NONE),
C(C_redo,      "redo",      c_redo, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_redir,   "redir",   c_redir, BANG|FILES|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_redraw,   "redraw",   c_redraw, BANG|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_redrawstatus,   "redrawstatus",   c_redrawstatus, BANG|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_redrawtabpanel, "redrawtabpanel", c_redrawtabpanel, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_registers, "registers", c_display, EXTRA|NOTRLCOM|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_resize,   "resize",   c_resize, RANGE|TRLBAR|WORD1|COMMPORT|LOCK_OK, ADDR_OTHER),
C(C_retab,   "retab",   c_retab, 
      TRLBAR|RANGE|WHOLEFOLD|DFLALL|BANG|WORD1|COMMPORT|LOCK_OK|MODIFY, ADDR_LINES
),
C(C_return,   "return",   c_return, EXTRA|NOTRLCOM|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_rewind,   "rewind",   c_rewind, EXTRA|BANG|CMDARG|ARGOPT|TRLBAR, ADDR_NONE),
C(C_right,   "right",   c_align, TRLBAR|RANGE|WHOLEFOLD|EXTRA|COMMPORT|LOCK_OK|MODIFY, ADDR_LINES),
C(C_rightbelow,   "rightbelow",   c_wrongmodifier, NEEDARG|EXTRA|NOTRLCOM, ADDR_NONE),
C(C_runtime, "runtime", c_runtime, BANG|NEEDARG|FILES|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_rundo,   "rundo",   c_rundo, NEEDARG|FILE1, ADDR_NONE),
C(C_substitute,   "substitute",   c_substitute, 
      RANGE|WHOLEFOLD|EXTRA|COMMPORT|LOCK_OK|NONWHITE_OK, ADDR_LINES
),
C(C_sNext,   "sNext",   c_previous, EXTRA|RANGE|COUNT|BANG|CMDARG|ARGOPT|TRLBAR, ADDR_OTHER),
C(C_sargument, "sargument", c_argument, 
      BANG|RANGE|COUNT|EXTRA|CMDARG|ARGOPT|TRLBAR, ADDR_ARGUMENTS
),
C(C_sall,      "sall",      c_all, BANG|RANGE|COUNT|TRLBAR, ADDR_OTHER),
C(C_saveas,   "saveas",   c_write, BANG|FILE1|ARGOPT|COMMPORT|LOCK_OK|TRLBAR, ADDR_NONE),
C(C_sbuffer, "sbuffer", c_book, 
      BANG|RANGE|BUFNAME|BUFUNL|COUNT|EXTRA|CMDARG|TRLBAR, ADDR_BUFFERS
),
C(C_sbNext,   "sbNext",   &c_bprevious, RANGE|COUNT|CMDARG|TRLBAR, ADDR_OTHER),
C(C_sball,   "sball",   c_bookAll, RANGE|COUNT|CMDARG|TRLBAR, ADDR_OTHER),
C(C_sbfirst,   "sbfirst",   &c_brewind, CMDARG|TRLBAR, ADDR_NONE),
C(C_sblast,   "sblast",   c_blast, CMDARG|TRLBAR, ADDR_NONE),
C(C_sbmodified,   "sbmodified",   &c_bmodified, RANGE|COUNT|CMDARG|TRLBAR, ADDR_OTHER),
C(C_sbnext,   "sbnext",   &c_bnext, RANGE|COUNT|CMDARG|TRLBAR, ADDR_OTHER),
C(C_sbprevious,   "sbprevious",   &c_bprevious, RANGE|COUNT|CMDARG|TRLBAR, ADDR_OTHER),
C(C_sbrewind,   "sbrewind",   &c_brewind, CMDARG|TRLBAR, ADDR_NONE),
C(C_scriptnames,   "scriptnames",   c_scriptnames,
   BANG|FILES|RANGE|COUNT|TRLBAR|COMMPORT|LOCK_OK, ADDR_OTHER
),
C(C_scscope,   "scscope",   c_scscope, EXTRA|NOTRLCOM, ADDR_NONE),
C(C_set,      "set",   c_set, BANG|TRLBAR|EXTRA|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_setfiletype, "setfiletype", c_setfiletype, TRLBAR|EXTRA|NEEDARG|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_setglobal, "setglobal", c_set, BANG|TRLBAR|EXTRA|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_sfind,   "sfind",   c_splitview, BANG|FILE1|RANGE|CMDARG|ARGOPT|TRLBAR|NEEDARG, ADDR_OTHER),
C(C_sfirst,   "sfirst",   c_rewind, EXTRA|BANG|CMDARG|ARGOPT|TRLBAR, ADDR_NONE),
C(C_shell,   "shell",   c_shell, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_sign,      "sign",      c_sign, NEEDARG|RANGE|EXTRA|COMMPORT|LOCK_OK, ADDR_OTHER),
C(C_silent,   "silent",   c_wrongmodifier,
   NEEDARG|EXTRA|BANG|NOTRLCOM|COMMPORT|LOCK_OK,
   ADDR_NONE),
C(C_sleep,   "sleep",   c_sleep, BANG|RANGE|COUNT|EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_OTHER),
C(C_slast,   "slast",   c_last, EXTRA|BANG|CMDARG|ARGOPT|TRLBAR, ADDR_NONE),
C(C_smile,   "smile",   c_smile, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_snext,   "snext",   c_next, RANGE|BANG|FILES|CMDARG|ARGOPT|TRLBAR, ADDR_OTHER),
C(C_snoremap,   "snoremap",   c_map, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_source, "source", c_source, 
      RANGE|DFLALL|BANG|FILE1|TRLBAR|COMMPORT|LOCK_OK, ADDR_LINES
),
C(C_sort,      "sort", c_sort, RANGE|DFLALL|WHOLEFOLD|BANG|EXTRA|NOTRLCOM|MODIFY, ADDR_LINES),
C(C_split,   "split",   c_splitview, BANG|FILE1|RANGE|CMDARG|ARGOPT|TRLBAR, ADDR_OTHER),
C(C_sprevious, "sprevious", c_previous, EXTRA|RANGE|COUNT|BANG|CMDARG|ARGOPT|TRLBAR, ADDR_OTHER),
C(C_srewind,   "srewind",   c_rewind, EXTRA|BANG|CMDARG|ARGOPT|TRLBAR, ADDR_NONE),
C(C_stag,      "stag",      c_stag, RANGE|BANG|WORD1|TRLBAR|ZERO_LINE_OK, ADDR_OTHER),
C(C_startinsert,   "startinsert",   c_startinsert, BANG|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_static,   "static",   c_wrongmodifier, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_stopinsert,   "stopinsert",   c_stopinsert, BANG|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_stjump,   "stjump",   c_stag, BANG|TRLBAR|WORD1, ADDR_NONE),
C(C_stselect,   "stselect",   c_stag, BANG|TRLBAR|WORD1, ADDR_NONE),
C(C_sunhide,  "sunhide", c_bookAll, RANGE|COUNT|TRLBAR, ADDR_OTHER),
C(C_sunmap,   "sunmap",   c_unmap, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_sview,   "sview",   c_splitview, BANG|FILE1|RANGE|CMDARG|ARGOPT|TRLBAR, ADDR_OTHER),
C(C_swapname,   "swapname",   c_swapname, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_syntax,   "syntax",   c_syntax, EXTRA|NOTRLCOM|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_syncbind,   "syncbind",   c_syncbind, TRLBAR, ADDR_NONE),
C(C_t, "t", c_copymove, RANGE|WHOLEFOLD|EXTRA|TRLBAR|COMMPORT|LOCK_OK|MODIFY, ADDR_LINES),
C(C_tag,      "tag",      c_tag, RANGE|BANG|WORD1|TRLBAR|ZERO_LINE_OK, ADDR_OTHER),
C(C_tags,      "tags",      do_tags, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_tab,      "tab",      c_wrongmodifier, NEEDARG|EXTRA|NOTRLCOM, ADDR_NONE),
C(C_tabclose,   "tabclose",   c_tabclose,
   BANG|RANGE|ZERO_LINE_OK|EXTRA|NOSPC_IN_EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_TABS
),
C(C_tabdo,   "tabdo",   c_listDo, NEEDARG|EXTRA|NOTRLCOM|RANGE|DFLALL|EXPAND, ADDR_TABS),
C(C_tabedit, "tabedit", c_splitview, BANG|FILE1|RANGE|ZERO_LINE_OK|CMDARG|ARGOPT|TRLBAR, ADDR_TABS),
C(C_tabfind,   "tabfind", c_splitview, 
      BANG|FILE1|RANGE|ZERO_LINE_OK|CMDARG|ARGOPT|NEEDARG|TRLBAR, ADDR_TABS
),
C(C_tabfirst,   "tabfirst",   c_tabnext, TRLBAR, ADDR_NONE),
C(C_tabmove,   "tabmove",   c_tabmove, RANGE|ZERO_LINE_OK|EXTRA|NOSPC_IN_EXTRA|TRLBAR, ADDR_TABS),
C(C_tablast,   "tablast",   c_tabnext, TRLBAR, ADDR_NONE),
C(C_tabnext,   "tabnext", c_tabnext, RANGE|ZERO_LINE_OK|EXTRA|NOSPC_IN_EXTRA|TRLBAR, ADDR_TABS),
C(C_tabnew, "tabnew", c_splitview, BANG|FILE1|RANGE|ZERO_LINE_OK|CMDARG|ARGOPT|TRLBAR, ADDR_TABS),
C(C_tabonly, "tabonly", c_tabonly,
   BANG|RANGE|ZERO_LINE_OK|EXTRA|NOSPC_IN_EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_TABS
),
C(C_tabprevious,   "tabprevious",  c_tabnext,
   RANGE|ZERO_LINE_OK|EXTRA|NOSPC_IN_EXTRA|TRLBAR, ADDR_TABS_RELATIVE
),
C(C_tabNext,   "tabNext",   c_tabnext, 
      RANGE|ZERO_LINE_OK|EXTRA|NOSPC_IN_EXTRA|TRLBAR, ADDR_TABS_RELATIVE
),
C(C_tabrewind,   "tabrewind",   c_tabnext, TRLBAR, ADDR_NONE),
C(C_tabs,      "tabs",      c_tabs, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_tcd,      "tcd",      c_cd, BANG|FILE1|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_tchdir,   "tchdir",   c_cd, BANG|FILE1|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_terminal,   "terminal",   c_terminal, RANGE|BANG|FILES|COMMPORT|LOCK_OK, ADDR_LINES),
C(C_tfirst,   "tfirst",   c_tag, RANGE|BANG|TRLBAR|ZERO_LINE_OK, ADDR_OTHER),
C(C_throw,   "throw",   c_throw, EXTRA|NEEDARG|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_this,      "this",      c_wrongmodifier, EXTRA|NEEDARG|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_tjump,   "tjump",   c_tag, BANG|TRLBAR|WORD1, ADDR_NONE),
C(C_tlast,   "tlast",   c_tag, BANG|TRLBAR, ADDR_NONE),
C(C_tmap,      "tmap", c_map, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_tmapclear,   "tmapclear",   c_mapclear, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_tnext,   "tnext", c_tag, RANGE|BANG|TRLBAR|ZERO_LINE_OK, ADDR_OTHER),
C(C_tNext,   "tNext", c_tag, RANGE|BANG|TRLBAR|ZERO_LINE_OK, ADDR_OTHER),
C(C_tnoremap,   "tnoremap", c_map, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_topleft,   "topleft", c_wrongmodifier, NEEDARG|EXTRA|NOTRLCOM, ADDR_NONE),
C(C_tprevious,   "tprevious", c_tag, RANGE|BANG|TRLBAR|ZERO_LINE_OK, ADDR_OTHER),
C(C_trewind,   "trewind", c_tag, RANGE|BANG|TRLBAR|ZERO_LINE_OK, ADDR_OTHER),
C(C_try, "try", c_try, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_tselect,   "tselect", c_tag, BANG|TRLBAR|WORD1, ADDR_NONE),
C(C_tunmap,   "tunmap", c_unmap, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_undo,      "undo", c_undo, RANGE|COUNT|ZERO_LINE_OK|TRLBAR|COMMPORT|LOCK_OK, ADDR_OTHER),
C(C_undojoin,   "undojoin", c_undojoin, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_undolist,   "undolist", c_undolist, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_unabbreviate, "unabbreviate", c_abbreviate, 
      EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE
),
C(C_unhide,   "unhide",   c_bookAll, RANGE|COUNT|TRLBAR, ADDR_OTHER),
C(C_uniq,      "uniq", c_uniq, RANGE|DFLALL|WHOLEFOLD|BANG|EXTRA|NOTRLCOM|MODIFY, ADDR_LINES),
C(C_unlet,   "unlet",   c_unlet, BANG|EXTRA|NEEDARG|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_unlockvar,   "unlockvar",   c_lockvar, BANG|EXTRA|NEEDARG|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_unmap,   "unmap",   c_unmap, BANG|EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_unsilent,   "unsilent",   c_wrongmodifier,
   NEEDARG|EXTRA|NOTRLCOM|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_update,   "update",   c_update, RANGE|WHOLEFOLD|BANG|FILE1|ARGOPT|DFLALL|TRLBAR, ADDR_LINES),
C(C_viusage,   "usage",   c_usage, TRLBAR, ADDR_NONE),
C(C_vglobal, "vglobal", c_global, 
      RANGE|WHOLEFOLD|EXTRA|DFLALL|COMMPORT|LOCK_OK|NONWHITE_OK, ADDR_LINES
),
C(C_version,   "version",   c_version, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_verbose,   "verbose",   c_wrongmodifier, 
      NEEDARG|RANGE|EXTRA|NOTRLCOM|COMMPORT|LOCK_OK, ADDR_OTHER
),
C(C_vertical,   "vertical",   c_wrongmodifier, NEEDARG|EXTRA|NOTRLCOM, ADDR_NONE),
C(C_visual,   "visual",   c_edit, BANG|FILE1|CMDARG|ARGOPT|TRLBAR, ADDR_NONE),
C(C_view,      "view",      c_edit, BANG|FILE1|CMDARG|ARGOPT|TRLBAR, ADDR_NONE),
C(C_vimgrep, "vimgrep", c_vimgrep, 
      RANGE|BANG|NEEDARG|EXTRA|NOTRLCOM|TRLBAR|XFILE|LOCK_OK, ADDR_OTHER
),
C(C_vimgrepadd, "vimgrepadd", c_vimgrep,
   RANGE|BANG|NEEDARG|EXTRA|NOTRLCOM|TRLBAR|XFILE|LOCK_OK, ADDR_OTHER
),
C(C_vmap,      "vmap",      c_map, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_vmapclear,   "vmapclear",   c_mapclear, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_vnoremap,   "vnoremap",   c_map, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_vnew,      "vnew",      c_splitview, BANG|FILE1|RANGE|CMDARG|ARGOPT|TRLBAR, ADDR_OTHER),
C(C_vsplit,   "vsplit",   c_splitview, BANG|FILE1|RANGE|CMDARG|ARGOPT|TRLBAR, ADDR_OTHER),
C(C_vunmap,   "vunmap",   c_unmap, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_write,   "write",   c_write, 
      RANGE|WHOLEFOLD|BANG|FILE1|ARGOPT|DFLALL|TRLBAR|COMMPORT|LOCK_OK, ADDR_LINES
),
C(C_wNext,   "wNext",   c_wnext, RANGE|WHOLEFOLD|BANG|FILE1|ARGOPT|TRLBAR, ADDR_OTHER),
C(C_wall,      "wall",      do_wqall, BANG|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_while,   "while",   c_while, EXTRA|NOTRLCOM|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_wincmd,   "wincmd",   c_wincmd, NEEDARG|WORD1|RANGE|COMMPORT|LOCK_OK, ADDR_OTHER),
C(C_windo,   "windo",   c_listDo, NEEDARG|EXTRA|NOTRLCOM|RANGE|DFLALL|EXPAND, ADDR_PORTALS),
C(C_winpos, "winpos", c_portPos, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_wlrestore,   "wlrestore",   c_wlrestore, EXTRA|TRLBAR|COMMPORT|LOCK_OK|BANG, ADDR_NONE),
C(C_wnext,   "wnext",   c_wnext, RANGE|BANG|FILE1|ARGOPT|TRLBAR, ADDR_OTHER),
C(C_wprevious,   "wprevious",   c_wnext, RANGE|BANG|FILE1|ARGOPT|TRLBAR, ADDR_OTHER),
C(C_wq,  "wq", c_exit, RANGE|WHOLEFOLD|BANG|FILE1|ARGOPT|DFLALL|TRLBAR, ADDR_LINES),
C(C_wqall,   "wqall", do_wqall, BANG|FILE1|ARGOPT|TRLBAR, ADDR_NONE),
C(C_wundo,   "wundo", c_wundo, BANG|NEEDARG|FILE1, ADDR_NONE),
C(C_xall,      "xall",      do_wqall, BANG|TRLBAR, ADDR_NONE),
C(C_xmap,      "xmap", c_map, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_xmapclear,   "xmapclear",   c_mapclear, EXTRA|TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_xnoremap,   "xnoremap",   c_map, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_xunmap,   "xunmap", c_unmap, EXTRA|TRLBAR|NOTRLCOM|CTRLV|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_yank, "yank", c_operators, RANGE|WHOLEFOLD|REGSTR|COUNT|TRLBAR|COMMPORT|LOCK_OK, ADDR_LINES),
C(C_z, "z",      c_z, RANGE|WHOLEFOLD|BANG|EXTRA|FLAGS|TRLBAR|COMMPORT|LOCK_OK, ADDR_LINES),

// commands that don't start with a letter
C(C_bang,      "!", c_bang, RANGE|WHOLEFOLD|BANG|FILES|COMMPORT|LOCK_OK|NONWHITE_OK, ADDR_LINES),
C(C_pound,   "#", c_print, RANGE|WHOLEFOLD|COUNT|FLAGS|TRLBAR|COMMPORT|LOCK_OK, ADDR_LINES),
C(C_and, "&", c_substitute, RANGE|WHOLEFOLD|EXTRA|COMMPORT|LOCK_OK|MODIFY|NONWHITE_OK, ADDR_LINES),
C(C_star, "*", c_at, RANGE|WHOLEFOLD|EXTRA|TRLBAR|COMMPORT|LOCK_OK|NONWHITE_OK, ADDR_LINES),
C(C_lshift, "<", c_operators, 
      RANGE|WHOLEFOLD|COUNT|FLAGS|TRLBAR|COMMPORT|LOCK_OK|MODIFY, ADDR_LINES
),
C(C_equal,   "=",      c_equal, RANGE|TRLBAR|DFLALL|FLAGS|COMMPORT|LOCK_OK, ADDR_LINES),
C(C_rshift, ">", c_operators, 
      RANGE|WHOLEFOLD|COUNT|FLAGS|TRLBAR|COMMPORT|LOCK_OK|MODIFY, ADDR_LINES
),
C(C_at, "@", c_at, RANGE|WHOLEFOLD|EXTRA|TRLBAR|COMMPORT|LOCK_OK|NONWHITE_OK, ADDR_LINES),
C(C_block,   "{",      c_block,  // not found normally
   TRLBAR|LOCK_OK|COMMPORT, ADDR_NONE),
C(C_endblock,   "}",      c_endblock, TRLBAR|COMMPORT|LOCK_OK, ADDR_NONE),
C(C_tilde, "~", c_substitute, 
      RANGE|WHOLEFOLD|EXTRA|COMMPORT|LOCK_OK|MODIFY|NONWHITE_OK, ADDR_LINES
),

// commands that start with an uppercase letter
C(C_Next,      "Next", c_previous, EXTRA|RANGE|COUNT|BANG|CMDARG|ARGOPT|TRLBAR, ADDR_OTHER),
C(C_Print,   "Print",   c_print, RANGE|WHOLEFOLD|COUNT|FLAGS|TRLBAR|COMMPORT|LOCK_OK, ADDR_LINES),
C(C_StrictSubstitute,   "Substitute",   c_substitute, 
      RANGE|WHOLEFOLD|EXTRA|COMMPORT|LOCK_OK|NONWHITE_OK, ADDR_LINES
),


#ifdef DECLARE_COMMANDS_ENUM
   COUNT_COMMANDS,      // MUST be after all real commands!
   C_USER = -1,   // User-defined command
   C_USER_BUF = -2   // User-defined command local to buffer
#endif
};

#endif
#undef C

#ifdef DECLARE_COMMANDS_ENUM

typedef enum CommIndex CommIndex;
#define FORCE_BIN 1      // ":edit ++bin file"
#define FORCE_NOBIN 2      // ":edit ++nobin file"

// Values for "flags"
#define EXFLAG_LIST   0x01  // 'l': list
#define EXFLAG_NR   0x02    // '#': number
#define EXFLAG_PRINT   0x04 // 'p': print

#endif
