#ifndef AVOID_DUPLICATION
typedef struct mf_hashtab_S {
	Ulong mask; //mask used for hash value (nr of items in array is "mht_mask" + 1)
	Ulong mht_count;       //nr of items inserted into hashtable
	MfHashItem** mht_buckets;  //points to mht_small_buckets or dynamically allocated array
	MfHashItem* mht_small_buckets[MHT_INIT_SIZE];   //initial buckets
	Byte mht_fixed;       //non-zero value forbids growth
} MfHashTable;
typedef enum {
	MF_DIRTY_NO = 0,      //no dirty blocks
	MF_DIRTY_YES,      //there are dirty blocks
	MF_DIRTY_YES_NOSYNC,   //there are dirty blocks, do not sync yet
} MfDirty;
struct MemFile {
	S fullFName;      //name of the file
	S fName;          //idem, full path
	int fd;         //file descriptor
	Unt mf_flags;      //flags used when opening this memfile
	int mf_reopen;      //mf_fd was closed, retry opening
	BlockHeader* freeFirst;      //first block_hdr in free list
	BlockHeader* usedFirst;      //mru block_hdr in used list
	BlockHeader* usedLast;      //lru block_hdr in used list
	Unt mf_used_count;      //number of pages in used list
	Unt usedCountMax;   //maximum number of pages in memory
	MfHashTable mf_hash;      //hash lists
	MfHashTable mf_trans;      //trans lists
	BlockId mf_blocknr_max;      //highest positive block number + 1
	BlockId mf_blocknr_min;      //lowest negative block number - 1
	BlockId mf_neg_count;      //number of negative blocks numbers
	BlockId pagesInFile;   //number of pages in the file
	Unt pageSize;      //number of bytes in a page
	MfDirty mf_dirty;
	Book* book;      //book this memfile is for
};
#define PT_FLAG_INS_START_INCL 1 //insert at start included in property
#define PT_FLAG_INS_END_INCL   2 //insert at end included in property
#define PT_FLAG_COMBINE        4 //combine with syntax highlight
#define PT_FLAG_OVERRIDE       8 //override any highlight
#endif

int get_user_name(S builder, int len);
int ml_open(Book *book);
void ml_setname(Book* book);
void memOpenSwapFiles(void);
void memOpenSwapFile(Book* book);
void check_need_swap(int newfile) ;
void ml_close(Book* book, Boole del_file);
void ml_close_all(Boole del_file);
void ml_close_notmod(void);
void ml_timestamp(Book* book);
void ml_recover(Boole checkext);
void get_b0_dict(S fname, Bag *bag);
void ml_sync_all(int check_file, int check_char);
void ml_preserve(Book* book, int message);
S ml_get(LineNr lnum);
S ml_get_pos(Pos* pos);
S ml_get_curline(void);
S ml_get_cursor(void);
ColNr ml_get_len(LineNr lnum);
ColNr ml_get_pos_len(Pos *pos);
ColNr ml_get_curline_len(void);
ColNr ml_get_cursor_len(void);
ColNr memGetBookLen(Book* book, LineNr lnum);
S memGetLine(Book* book, LineNr lnum, Boole willChange);
int ml_line_alloced(void);
int ml_append(
	LineNr lnum, //append after this line (can be 0)
	S newContent, //text of the new line
	ColNr len, //number of bytes to copy, or if 0 - will be replaced by strlen(newContent),
	int   newfile //flag, see above
);
int ml_append_flags(
	LineNr   lnum,      //append after this line (can be 0)
	S newContent,      //text of the new line
	ColNr   len,        //length of new line, including ZERO, or 0
	Unt      flags      //ML_APPEND_ values
);
int memAppendBook(
	Book* book,
	LineNr lnum,  //append after this line (can be 0)
	S line,      //text of the new line
	ColNr len,    //length of new line, including ZERO, or 0
	int newfile  //flag, see above
);
int ml_replace(LineNr lnum, S line, int copy);
int ml_replace_len(
	LineNr lnum,
	S line_arg,
	ColNr len_arg,
	int has_props,
	int copy
);
int ml_delete(LineNr lnum);
int ml_deleteBufLine(Book* book, LineNr lnum);
int ml_delete_flags(LineNr lnum, int flags);
void ml_setmarked(LineNr lnum);
LineNr ml_firstmarked(void);
void ml_clearmarked(void);
void ml_setflags(Book* book);
long ml_find_line_or_offset(Book* book, LineNr lnum, long *offp);
void goto_byte(long cnt);
S memMakePercentSwapName(S dir, S dir_end, S name);
MemFile * mf_open(S fname, Unt flags);
int mf_open_file(MemFile* mfp, S fname);
void mf_close(MemFile* mfp, int del_file);
void mf_close_file(Book* book, int getlines);
void mf_new_page_size(MemFile* mfp, unsigned new_size);
BlockHeader * mf_new(MemFile* mfp, int negative, int page_count);
BlockHeader * mf_get(MemFile* mfp, BlockId nr, int page_count);
void mf_put(MemFile* mfp, BlockHeader* hp, int dirty, int infile);
void mf_free(MemFile* mfp, BlockHeader* hp);
int mf_sync(MemFile* mfp, Unt flags);
void mf_set_dirty(MemFile* mfp);
int mf_release_all(void);
BlockId mf_trans_del(MemFile* mfp, BlockId old_nr);
void mf_set_ffname(MemFile* mfp);
void mf_fullname(MemFile* mfp);
int mf_need_trans(MemFile* mfp);
Book* bookFindByName(S name, Boole curtab_only);
Book* findBook(Var* avar);
void f_append(Var *argvars, OUT Var* returnVar);
void f_appendbufline(Var *argvars, OUT Var* returnVar);
void f_bufadd(Var *argvars, OUT Var* returnVar);
void f_bufexists(Var *argvars, OUT Var* returnVar);
void f_buflisted(Var *argvars, OUT Var* returnVar);
void f_bufload(Arr(Var) argvars, OUT Var*);
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
S transchar(Unt c);
S transchar_buf(Unt c);
int byte2cells(Unt b);
int bookChar2Cells(Unt c);
int bookPtr2Cells(S p);
S bookTranscharByte(Unt c);
int chartabsize(S p, ColNr col);
Unt linetabsize_str(S s);
int linetabsize_col(int startcol, S s);
Unt drawLineOnScreentabsize(Portal* po, LineNr lnum, S line, ColNr len);
int linetabsize(Portal *po, LineNr lnum);
int linetabsize_eol(Portal *po, LineNr lnum);
int linetabsize_no_outer(Portal *po, LineNr lnum);
void drawLineOnScreentabsize_cts(CharTableSize *cts, ColNr len);
int bookIsIdentifierChar(int c);
int bookIsAsciiIdentifierChar(int c);
int bookIsWordChar(Unt c, Book* book);
int eeIsWordc(Unt c);
int eeIsWordPtr_buf(S p, Book* book);
int eeIsWordPtr(S p);
int eeIsFnameChar(Unt c);
Boole eeIsFnameCharForGf(Unt c);
Boole bookIsCharPrintable(Unt c);
void bookInitCharsForKeywordsSizeArg(
	OUT CharTableSize* cts,
	Portal* po,
	LineNr lnum,
	ColNr col,
	S line,
	S ptr
);
void clear_chartabsize_arg(OUT CharTableSize* cts);
int lbr_chartabsize(CharTableSize* cts);
int lbr_chartabsize_adv(CharTableSize *cts);
int win_lbr_chartabsize(CharTableSize* cts);
void getvcol(
	Portal* po,
	Pos* pos,
	ColNr* start,
	ColNr* cursor,
	ColNr* end
);
ColNr bookGetVcolNoList(Pos* posp);
void bookGetVirtualColInVirtualMode(
	Portal* po,
	Pos* pos,
	OUT ColNr* start,
	OUT ColNr* cursor,
	OUT ColNr* end
);
void bookGetLeftRightVcols(
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
	Portal* port,      //if not NULL, set lastCursor
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
	Unt dir,      //FORWARD or BACKWARD
	int count,      //book number
	Unt flags   //DOBOOK_FORCEIT when using !, etc
);
S do_bufdel(
	int command,
	S arg,      //pointer to extra arguments
	int addrCount,
	int start_bnr,   //first book number in a range
	int end_bnr,   //book nr or last book nr in a range
	Boole forceit
);
void bookSetCurBook(Book* book, int action);
void no_write_message(void);
void no_write_message_nobang(Book* book);
Book* bookNew(
	S ffname_arg, //full path of fname or relative
	S sfname_arg, //short fname or NULL
	LineNr lnum,   //preferred cursor line
	Unt flags
);
int booklistGetFile(
	int n,
	LineNr   lnum,
	int options,
	int forceit
);
Book * booklistFindByNameExpandingLinks(S fname);
Book* booklistFindName(S fullFName);
int booklistFindPattern(
	S pattern,
	S pattern_end,   //pointer to first char after pattern
	int unlisted,   //find unlisted books
	int diffmode, //find diff-mode books only
	int curtab_only  //find books in current tab only
);
int bufExpandBufnames(
	S pat,
	Unt options,
	OUT ExpandMatch* matches
);
Book* bookFindFileByBookNr(int nr);
S bookGetNameByBookNr(int n, int fullname, Boole helptail);
void bookSetPosInPort(
	Book* book,
	Portal* port,      //may be NULL when using :badd
	LineNr lnum,
	ColNr col,
	Boole copy_options
);
void get_winopts(Book* book);
Pos * bookFindFpos(Book* book);
void bookListFiles(Invocation* invo);
int bookGetFnameByFileId(int fnum, OUT S* fname, OUT LineNr* lnum);
int setfname(Book* book, S ffname_arg, S sfname_arg, Boole message);
void bookSetName(int fnum, S name);
void bookHandleNameChange(Book* book);
Book * setaltfname(S fullFName, S sfname, LineNr lnum);
S getaltfname(int errmsg);
int bookOpen(S fname, Unt flags);
Boole fNameMatchesCurBook(S fullFName);
void buf_setino(Book* book);
void fileinfo(
	Boole fullname,       //when true, print full path; whan > 1, include book number
	Boole shorthelp,
	Boole dont_truncate
);
int col_print(S buf, Unt  buflen, int col, int vcol);
int bookRenderStatusLine(
	Portal* po,
	OUT S out,      //string book to write into != nameBuffG
	Unt outlen,      //length of out[]
	S fmt,
	Byte oname,      //one of STATLINE_* constants
	int opt_scope,   //scope for "oname"
	Unt fillchar,
	int maxwidth
);
int get_rel_pos(Portal* po, S buf, int buflen);
void fname_expand(S* fullFName, S* sfname);
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
S bookSpName(Book* book);
S bookGetFname(Book* book);
void bookSetBooklisted(Boole on);
Boole bookContentsChanged(Book* book);
void bookWipe(Book* book, int aucmd);
void printMsgWithWrap(S s);
int bookCompare(const void* s0, const void* s1);
Boole bookWasChanged(Book* book);
Boole bookWasChangedNotTerm(Book* book);
void drawGetTranslatedBookName(Book* book);
int bookStartEditingFile(
	int fnum,
	S fullFName,
	S sfname,
	Invocation* invo,         //can be NULL!
	LineNr newlnum,
	Unt flags,
	Portal* oldPort
);
void c_swapname(Invocation*);
Boole bookNoMemfile(Book* b);
Boole bookNoFname(Book* book);
S bookGetSwapName(Book* book);
void f_swapname(Arr(Var) argvars, Var* returnVar);
int bookCheckTimestamp(Book* book);
void bookShortenName(Book* book, S dirname, int force);
void shorten_fnames(Boole force);
int bookGetFd(Book* b);
void bookSetDirtyFlag(Book* book);
S new_file_message(void);
int bookWrite(
	Book* book,
	S fname,
	S sfname,
	LineNr start,
	LineNr end,
	Invocation* invo,      //for forced 'ff', can be NULL!
	Boole append,      //append to the file
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
	 S fname,
	 int set_fnum   //1: set book number; 2: re-use curBook
);
int bookParseAndExpandFnames(S str, Boole omitWildignore, OUT ExpandMatch* matches);
void set_arglist(S str);
int editing_arg_idx(Portal *port);
void check_arg_idx(Portal* port);
void c_args(Invocation* invo);
void c_previous(Invocation* invo);
void c_rewind(Invocation* invo);
void c_last(Invocation* invo);
void c_argument(Invocation* invo);
void do_argfile(Invocation* invo, int argn);
void c_next(Invocation* invo);
void c_argdedupe(Invocation*);
void c_argedit(Invocation* invo);
void c_argadd(Invocation* invo);
void c_argdelete(Invocation* invo);
S get_arglist_name(Expand*, int idx);
S alist_name(ArgFileEntry *afe);
void c_all(Invocation* invo);
S arg_all(void);
void f_argc(Var* argvars, Var* returnVar);
void f_argidx(Var*, OUT Var* returnVar);
void f_arglistid(Var *argvars, OUT Var* returnVar);
void f_argv(Var *argvars, OUT Var* returnVar);
int findPropTypeIdByName(Text name, Book* book);
void f_prop_add(Var *argvars, OUT Var* returnVar);
void f_prop_add_list(Var *argvars, OUT Var*);
int prop_add_common(
	LineNr startLnum,
	ColNr startCol,
	Bag* dict,
	Book* defaultBook,
	Var* dictArg
);
int get_text_props(OUT S* props, Book* book, LineNr lnum, Boole will_change);
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
void f_prop_clear(Var *argvars, OUT Var*);
void f_prop_find(Var *argvars, OUT Var* returnVar);
void f_prop_list(Var *argvars, OUT Var* returnVar);
void f_prop_remove(Var *argvars, OUT Var* returnVar);
void f_prop_type_add(Var *argvars, OUT Var*);
void f_prop_type_change(Var *argvars, OUT Var*);
void f_prop_type_delete(Var *argvars, OUT Var*);
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
	S new_props,
	int       propcount,
	OUT int* props_remaining,
	LineNr    lnum,
	int       last_line,
	long       col,
	int       removed
);
