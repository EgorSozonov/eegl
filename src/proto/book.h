Boole setRefInBooks(int copyID);
Book* bookFindByName(CS name, Boole curtab_only);
Book* findBook(Var* avar);
void f_append(Var *argvars, OUT Var* returnVar);
void f_appendbufline(Var *argvars, OUT Var* returnVar);
void f_bufadd(Var *argvars, OUT Var* returnVar);
void f_bufexists(Var *argvars, OUT Var* returnVar);
void f_buflisted(Var *argvars, OUT Var* returnVar);
void f_bufload(Arr(Var) argvars, OUT Var* returnVar UNUSED);
void f_bufloaded(Var *argvars, OUT Var* returnVar);
void f_bufname(Var *argvars, OUT Var* returnVar);
void f_bufnr(Var *argvars, OUT Var* returnVar);
void f_bufwinid(Var *argvars, Var* returnVar);
void f_bufwinnr(Var *argvars, OUT Var* returnVar);
void f_deletebufline(Var *argvars, OUT Var* returnVar);
void f_getbufinfo(Var *argvars, OUT Var* returnVar);
void f_getbufline(Var *argvars, OUT Var* returnVar);
void f_getbufoneline(Var *argvars, OUT Var* returnVar);
void f_getline(Var *argvars, OUT Var* returnVar);
void f_setbufline(Var *argvars, OUT Var* returnVar);
void f_setline(Var *argvars, OUT Var* returnVar);
int bookInitCharsForKeywordsForCurbook(void);
void bookInitGlobalCharTable();
int bookIsCharPrintable_strict(int c);
CS transchar(Unt c);
CS transchar_buf(Unt c);
int byte2cells(Unt b);
int bookChar2Cells(Unt c);
int bookPtr2Cells(CS p);
CS bookTranscharByte(Unt c);
int chartabsize(CS p, ColNr col);
int win_chartabsize(Portal *po, CS p, ColNr col);
Unt linetabsize_str(CS s);
int linetabsize_col(int startcol, CS s);
Unt drawLineOnScreentabsize(Portal* po, LineNr lnum, CS line, ColNr len);
int linetabsize(Portal *po, LineNr lnum);
int linetabsize_eol(Portal *po, LineNr lnum);
int linetabsize_no_outer(Portal *po, LineNr lnum);
void drawLineOnScreentabsize_cts(CharTableSize *cts, ColNr len);
int eeIsIdentifierChar(int c);
int eeIsNormalIdentifierChar(int c);
int eeIsWordc_buf(Unt c, Book* book);
int eeIsWordc(Unt c);
int eeIsWordPtr_buf(CS p, Book* book);
int eeIsWordPtr(CS p);
int eeIsFnameChar(Unt c);
Boole eeIsFnameCharForGf(Unt c);
Boole bookIsCharPrintable(Unt c);
void bookInitCharsForKeywordsSizeArg(
   OUT CharTableSize* cts,
   Portal* po,
   LineNr lnum,
   ColNr col,
   CS line,
   CS ptr
);
void clear_chartabsize_arg(OUT CharTableSize* cts);
int lbr_chartabsize(CharTableSize* cts);
int lbr_chartabsize_adv(CharTableSize *cts);
int win_lbr_chartabsize(CharTableSize* cts, int* headp);
void getvcol(
   Portal* po,
   Pos* pos,
   ColNr* start,
   ColNr* cursor,
   ColNr* end
);
ColNr getvcol_nolist(Pos* posp);
void bookGetVirtualColInVirtualMode(
   Portal* po,
   Pos* pos,
   OUT ColNr* start,
   OUT ColNr* cursor,
   OUT ColNr* end
);
void getvcols(
   Portal* po,
   Pos* pos1,
   Pos* pos2,
   ColNr* left,
   ColNr* right
);
int get_highest_fnum(void);
int bookCharidxToByteidx(Book* book, int lnum, int charidx);
void bookEnsureLoaded(Book* book);
Unt bookOpenFromInvo(
   Boole read_stdin,     //read file from stdin
   Invocation* invo, //for forced 'ff' or NULL
   Unt flags        //extra flags for readfile()
);
void bookStoreInRef(OUT BookRef *bookRef, Book* book);
Boole bookRefValid(BookRef* bookRef);
Boole bookIsValid(Book* book);
int bookClose(
   Portal* port,      // if not NULL, set lastCursor
   Book* book,
   Unt action,
   int abort_if_last,
   int ignore_abort
);
void buf_clear_file(Book* book);
void bookFreeAll(Book* book, Unt flags);
void free_wininfo(PortInfo *poInfo);
void bookGoto(Invocation* invo, int start, int dir, int count);
void handle_swap_exists(BookRef *oldCurBook);
int bookDo(
   Unt action,
   Unt start,
   Unt dir,      // FORWARD or BACKWARD
   int count,      // book number
   Unt flags   // DOBOOK_FORCEIT when using !, etc
);
CS do_bufdel(
   int command,
   CS arg,      // pointer to extra arguments
   int addr_count,
   int start_bnr,   // first book number in a range
   int end_bnr,   // book nr or last book nr in a range
   Boole forceit
);
void bookSetCurBook(Book* book, int action);
void no_write_message(void);
void no_write_message_nobang(Book* book);
Book* bookNew(
   CS ffname_arg, // full path of fname or relative
   CS sfname_arg, // short fname or NULL
   LineNr lnum,   // preferred cursor line
   Unt flags
);
int booklistGetFile(
   int n,
   LineNr   lnum,
   int options,
   int forceit
);
Book * booklistFindByNameExpandingLinks(CS fname);
Book* booklistFindName(CS fullFName);
int booklistFindPattern(
   CS pattern,
   CS pattern_end,   // pointer to first char after pattern
   int unlisted,   // find unlisted books
   int diffmode UNUSED, // find diff-mode books only
   int curtab_only  // find books in current tab only
);
int bufExpandBufnames(
   CS pat,
   Unt options,
   OUT ExpandMatch* matches
);
Book* bookFindFileByBookNr(int nr);
CS bookGetNameByBookNr(int n, int fullname, Boole helptail);
void bookSetPosInPort(
   Book* book,
   Portal* port,      // may be NULL when using :badd
   LineNr lnum,
   ColNr col,
   Boole copy_options
);
void get_winopts(Book* book);
Pos * bookFindFpos(Book* book);
void bookListFiles(Invocation* invo);
int bookGetFnameByFileId(int fnum, OUT CS* fname, OUT LineNr* lnum);
int setfname(Book* book, CS ffname_arg, CS sfname_arg, Boole message);
void bookSetName(int fnum, CS name);
void bookHandleNameChange(Book* book);
Book * setaltfname(CS fullFName, CS sfname, LineNr lnum);
CS getaltfname(int errmsg);
int bookOpen(CS fname, Unt flags);
Boole fNameMatchesCurBook(CS fullFName);
void buf_setino(Book* book);
void fileinfo(
   Boole fullname,       // when true, print full path; whan > 1, include book number
   Boole shorthelp,
   Boole dont_truncate
);
int col_print(CS buf, Unt  buflen, int col, int vcol);
int bookRenderStatusLine(
   Portal* po,
   OUT CS out,      // string book to write into != nameBuffG
   Unt outlen,      // length of out[]
   CS fmt,
   Byte oname,      // one of STATLINE_* constants
   int opt_scope,   // scope for "oname"
   Unt fillchar,
   int maxwidth,
   OUT Arr(StatusLineHilite)* labels   // return: tab numbers (can be NULL)
);
int get_rel_pos(Portal* po, CS buf, int buflen);
void fname_expand(CS* fullFName, CS* sfname);
void c_bookAll(Invocation* invo);
int bt_normal(Book* book);
Boole isLocationListBook(Book* book);
int bt_terminal(Book* book);
int bookIsHelp(Book* book);
int bt_prompt(Book* book);
int bt_popup(Book* book);
int bt_nofilename(Book* book);
int bt_nofile(Book* book);
Boole bookDontWrite(Book* book);
int bookDontWrite_msg(Book* book);
CS bookSpName(Book* book);
CS bookGetFname(Book* book);
void bookSetBooklisted(Boole on);
Boole bookContentsChanged(Book* book);
void bookWipe(Book* book, int aucmd);
void printMsgWithWrap(CS s);
int bookCompare(const void* s0, const void* s1);
CS new_file_message(void);
int bookWrite(
   Book* book,
   CS fname,
   CS sfname,
   LineNr start,
   LineNr end,
   Invocation* invo,      // for forced 'ff', can be NULL!
   Boole append,      // append to the file
   Boole forceit,
   Boole reset_changed,
   Boole filtering
);
void alist_clear(EeArgList* al);
void alist_init(EeArgList *al);
void alist_unlink(EeArgList *al);
void alist_new(void);
void arglistIngest(
    EeArgList* al,
    CS fname,
    int set_fnum   // 1: set book number; 2: re-use curBook
);
int bookParseAndExpandFnames(CS str, Boole omitWildignore, OUT ExpandMatch* matches);
void set_arglist(CS str);
int editing_arg_idx(Portal *port);
void check_arg_idx(Portal* port);
void c_args(Invocation* invo);
void c_previous(Invocation* invo);
void c_rewind(Invocation* invo);
void c_last(Invocation* invo);
void c_argument(Invocation* invo);
void do_argfile(Invocation* invo, int argn);
void c_next(Invocation* invo);
void c_argdedupe(Invocation* invo UNUSED);
void c_argedit(Invocation* invo);
void c_argadd(Invocation* invo);
void c_argdelete(Invocation* invo);
CS get_arglist_name(Expand *xp UNUSED, int idx);
CS alist_name(ArgFileEntry *afe);
void c_all(Invocation* invo);
CS arg_all(void);
void f_argc(Var* argvars, Var* returnVar);
void f_argidx(Var *argvars UNUSED, OUT Var* returnVar);
void f_arglistid(Var *argvars, OUT Var* returnVar);
void f_argv(Var *argvars, OUT Var* returnVar);
int findPropTypeIdByName(Text name, Book* book);
void f_prop_add(Var *argvars, OUT Var* returnVar);
void f_prop_add_list(Var *argvars, OUT Var* returnVar UNUSED);
int prop_add_common(
   LineNr startLnum,
   ColNr startCol,
   Bag* dict,
   Book* defaultBook,
   Var* dictArg
);
int get_text_props(OUT CS* props, Book* book, LineNr lnum, Boole will_change);
int prop_count_above_below(Book* book, LineNr lnum);
int count_props(LineNr lnum, int only_starting, int last_line);
void sort_text_props(
   Book* book,
   TextProp* props,
   int* idxs,
   int count
);
int find_visible_prop(
   Portal       *wp,
   int       type_id,
   int       id,
   TextProp  *prop,
   LineNr    *found_lnum
);
void add_text_props(LineNr lnum, TextProp *text_props, int text_prop_count);
PropType * text_prop_type_by_id(Book* book, int id);
void f_prop_clear(Var *argvars, OUT Var* returnVar UNUSED);
void f_prop_find(Var *argvars, OUT Var* returnVar);
void f_prop_list(Var *argvars, OUT Var* returnVar);
void f_prop_remove(Var *argvars, OUT Var* returnVar);
void f_prop_type_add(Var *argvars, OUT Var* returnVar UNUSED);
void f_prop_type_change(Var *argvars, OUT Var* returnVar UNUSED);
void f_prop_type_delete(Var *argvars, OUT Var* returnVar UNUSED);
void f_prop_type_get(Var *argvars, OUT Var* returnVar);
void f_prop_type_list(Var *argvars, OUT Var* returnVar);
void clear_global_prop_types(void);
Boole adjustPropColumns(LineNr lnum, ColNr col, int bytes_added, Unt flags);
void adjustPropsForSplit(
   LineNr    lnumProps,
   LineNr    lnumTop,
   int       kept,
   int       deleted,
   int       atEol
);
void prepend_joined_props(
   CS new_props,
   int       propcount,
   OUT int* props_remaining,
   LineNr    lnum,
   int       last_line,
   long       col,
   int       removed
);
