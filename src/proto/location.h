int fstat(int fd, struct stat* statbuf);
int lstat(const char* restrict, struct stat* restrict);
int llInitFromFile(
   OUT LocationStack* st,
   CS errorFName,
   NULLABLE CS errorformat,
   Boole newlist,      // true: start a new error list
   CS title
);
int qf_stack_get_bufnr(void);
void check_qfBusynessG(void);
void llInitStacksOnce(void);
LocationStack* getLocationStack(int ind);
void llJump(LocationStack* stack, Unt dir, int errornr, Boole forceit);
void c_list(Invocation* invo);
void c_llAge(Invocation* invo);
void qf_history(Invocation* invo);
void llViewLocation(int split);
void c_cPortal(Invocation* invo);
void c_lClose(Invocation* invo);
void c_lOpen(Invocation* invo);
void c_lBottom(Invocation* invo);
LineNr llCurrentEntry(Portal* po);
CS setQuickfixtextfunc(OptionChange* cha);
int grepIsActuallyInternal(CommIndex id);
void c_elgrep(Invocation* invo);
void c_grep(Invocation* invo);
void initInProgressLl();
void c_make(Invocation* invo UNUSED);
int llGetSize(Invocation* invo);
int llGetValidSize(Invocation* invo);
int llGetCurrIndex(Invocation* invo);
int llGetCurrValidIndex(Invocation* invo);
void c_lMove(Invocation* invo);
void c_lNext(Invocation* invo);
void c_lBelow(Invocation* invo);
void c_lFile(Invocation* invo);
void c_vimgrep(Invocation* invo);
int setLocationList(
   OUT LocationStack* stack,
   List* newContent,
   LocListAction action,
   CS title,
   Bag* specific
);
Boole llSetRef(int copyId);
void c_lbook(Invocation* invo);
CS cexpr_get_auname(CommIndex id);
int trigger_cexpr_autocmd(int id);
int cexpr_core(Invocation* invo, Var *tv);
void c_lExpr(Invocation* invo);
void c_helpgrep(Invocation* invo);
void free_quickfix(void);
void f_getloclist(Arr(Var) argvars, OUT Var* returnVar);
void f_setloclist(Var* argvars, Var* returnVar);
int setmark(int c);
int setmark_pos(int c, Pos *pos, int fnum);
void mark_forget_file(Portal *wp, int fnum);
void setpcmark(void);
void checkpcmark(void);
Pos * movemark(int count);
Pos * movechangelist(int count);
Pos * markGetBook(Book* book, int c, int changefile);
Pos * getmark(int c, int changefile);
Pos * markGetBookFnum(Book* book, int c, int changefile, int* fnum);
Pos * getnextmark(Pos* startpos, Unt dir, int begin_line);
void fmarks_check_names(Book* book);
int check_mark(Pos* pos);
void clrallmarks(Book* book);
CS fm_getname(FileMark* fmark, int lead_len);
void c_marks(Invocation *invo);
void c_delmarks(Invocation* invo);
void c_jumps(Invocation* invo UNUSED);
void c_clearjumps(Invocation* invo UNUSED);
void c_changes(Invocation* invo UNUSED);
void markAdjust(
   LineNr line1,
   LineNr line2,
   long amount,
   long amount_after,
   Boole adjust_folds
);
void mark_col_adjust(
   LineNr lnum,
   ColNr mincol,
   long lnum_amount,
   long col_amount,
   int spaces_removed
);
void cleanup_jumplist(Portal* wp, int loadfiles);
void copy_jumplist(Portal* from, Portal* to);
void free_jumplist(Portal *wp);
void set_last_cursor(Portal *port);
void free_all_marks(void);
FileMarkExt * get_namedfm(void);
void f_getmarklist(Var *argvars, Var* returnVar);
void init_signs(void);
int markGetSignDecorations(Portal *wp, LineNr lnum, OUT SignHilite* signHilites);
void llDeleteSigns(Book* book, CS group);
int sign_define_by_name(
   CS name,
   CS linehl,
   CS textt,
   CS texthl,
   CS culhl,
   CS numhl,
   int prio
);
int sign_exists_by_name(CS name);
int sign_undefine_by_name(CS name, Boole give_error);
int sign_place(
   int *sign_id,
   CS sign_group,
   CS sign_name,
   Book* book,
   LineNr lnum,
   int prio
);
void c_sign(Invocation* invo);
void llGetBookSigns(Book *book, List *l);
void free_signs(void);
CS get_sign_name(Expand *xp UNUSED, int idx);
void set_context_in_sign_cmd(Expand* xp, CS arg);
void f_sign_define(Var *argvars, Var* returnVar);
void f_sign_getdefined(Var *argvars, Var* returnVar);
void f_sign_getplaced(Var *argvars, Var* returnVar);
void f_sign_jump(Var *argvars, Var* returnVar);
void f_sign_place(Var *argvars, Var* returnVar);
void f_sign_placelist(Var *argvars, Var* returnVar);
void f_sign_undefine(Var *argvars, Var* returnVar);
Boole isSigncolumnOn(Portal *wp);
void f_sign_unplace(Var *argvars, Var* returnVar);
void f_sign_unplacelist(Var *argvars, Var* returnVar);
